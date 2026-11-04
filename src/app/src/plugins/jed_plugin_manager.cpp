#include "plugins/jed_plugin_manager.hpp"

#include "sed/plugins/jed_plugin_abi.hpp"
#include "sed/plugins/plugin_binary.hpp"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMenu>
#include <QTimer>
#include <QUrl>

#include <exception>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#endif

namespace sed::app_plugins
{
namespace
{

constexpr int kMaximumFolderDepth = 8;
constexpr qint64 kMaximumDescriptionBytes = 64 * 1024;
constexpr qint64 kArchitectureProbeBytes = 4096;

bool isLibrarySuffix(const QString& suffix)
{
#ifdef Q_OS_WIN
    return suffix == QStringLiteral("dll");
#else
    return suffix == QStringLiteral("so");
#endif
}

bool isLauncherSuffix([[maybe_unused]] const QString& suffix)
{
#ifdef Q_OS_WIN
    return suffix == QStringLiteral("exe") || suffix == QStringLiteral("bat") || suffix == QStringLiteral("js")
        || suffix == QStringLiteral("vbs") || suffix == QStringLiteral("lnk");
#else
    return false;
#endif
}

void applyDescription(PluginMenuEntry& entry, const QString& descriptionPath)
{
    QFile file(descriptionPath);
    if ( file.size() > kMaximumDescriptionBytes || !file.open(QIODevice::ReadOnly | QIODevice::Text) )
    {
        return;
    }
    const QStringList lines = QString::fromLocal8Bit(file.readAll()).split(QLatin1Char('\n'));
    for ( const QString& rawLine : lines )
    {
        const qsizetype equals = rawLine.indexOf(QLatin1Char('='));
        if ( equals <= 0 )
        {
            continue;
        }
        const QString key = rawLine.left(equals).trimmed().toLower();
        const QString value = rawLine.mid(equals + 1).trimmed();
        if ( key == QStringLiteral("name") && !value.isEmpty() )
        {
            entry.title = value;
        }
        else if ( key == QStringLiteral("key") )
        {
            entry.shortcut = QKeySequence(value, QKeySequence::PortableText);
        }
    }
}

PluginMenuFolder scanFolder(const QDir& directory, const int depth)
{
    PluginMenuFolder folder{};
    folder.title = directory.dirName();
    if ( depth < kMaximumFolderDepth )
    {
        const QFileInfoList subfolders =
            directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
        for ( const QFileInfo& info : subfolders )
        {
            PluginMenuFolder child = scanFolder(QDir(info.absoluteFilePath()), depth + 1);
            if ( !child.empty() )
            {
                folder.folders.push_back(std::move(child));
            }
        }
    }
    const QFileInfoList files = directory.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for ( const QFileInfo& info : files )
    {
        const QString suffix = info.suffix().toLower();
        const bool library = isLibrarySuffix(suffix);
        if ( !library && !isLauncherSuffix(suffix) )
        {
            continue;
        }
        PluginMenuEntry entry{};
        entry.path = info.absoluteFilePath();
        entry.library = library;
        const bool shortcut = suffix == QStringLiteral("lnk");
        entry.title = shortcut ? info.completeBaseName() : info.fileName();
        const QString descriptionBase =
            shortcut ? info.absoluteDir().filePath(info.completeBaseName()) : entry.path;
        applyDescription(entry, descriptionBase + QStringLiteral(".dsc"));
        folder.entries.push_back(std::move(entry));
    }
    return folder;
}

plugins::PluginBinaryArchitecture hostArchitecture()
{
#if defined(Q_PROCESSOR_ARM_64)
    return plugins::PluginBinaryArchitecture::arm64;
#elif defined(Q_PROCESSOR_X86_64)
    return plugins::PluginBinaryArchitecture::x64;
#elif defined(Q_PROCESSOR_X86_32)
    return plugins::PluginBinaryArchitecture::x86;
#else
    return plugins::PluginBinaryArchitecture::unknown;
#endif
}

plugins::PluginBinaryArchitecture fileArchitecture(const QString& path)
{
    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        return plugins::PluginBinaryArchitecture::unknown;
    }
    const QByteArray head = file.read(kArchitectureProbeBytes);
    return plugins::detectPeArchitecture(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(head.constData()), static_cast<std::size_t>(head.size())));
}

} // namespace

PluginMenuFolder scanPluginFolder(const QString& directory)
{
    const QDir dir(directory);
    if ( directory.isEmpty() || !dir.exists() )
    {
        return {};
    }
    return scanFolder(dir, 0);
}

JedPluginManager::JedPluginManager(PluginEditorServices services, QObject* parent)
    : QObject(parent)
    , services_(std::move(services))
    , host_(*this)
{
    setObjectName(QStringLiteral("jedPluginManager"));
}

JedPluginManager::~JedPluginManager()
{
    // Never unload plug-in modules: their windows and threads may still run.
    for ( std::unique_ptr<QLibrary>& library : libraries_ )
    {
        (void)library.release();
    }
}

void JedPluginManager::setPluginDirectory(const QString& directory)
{
    pluginDirectory_ = directory;
}

void JedPluginManager::populateMenu(QMenu* menu)
{
    if ( menu == nullptr )
    {
        return;
    }
    menu->clear();
    const PluginMenuFolder root = scanPluginFolder(pluginDirectory_);
    if ( root.empty() )
    {
        QAction* none = menu->addAction(tr("(No plug-ins installed)"));
        none->setEnabled(false);
    }
    else
    {
        addFolder(menu, root);
    }
    menu->addSeparator();
    QAction* rescan = menu->addAction(tr("&Rescan Plug-ins"));
    rescan->setObjectName(QStringLiteral("pluginsRescanAction"));
    connect(rescan, &QAction::triggered, this, [this, menu]()
        {
            // Rebuilding deletes the triggering action; defer until its signal returns.
            QTimer::singleShot(0, this, [this, menu]() { populateMenu(menu); });
        });
    QAction* openFolder = menu->addAction(tr("Open Plug-ins &Folder"));
    openFolder->setObjectName(QStringLiteral("pluginsOpenFolderAction"));
    connect(openFolder, &QAction::triggered, this, [this]()
        {
            if ( pluginDirectory_.isEmpty() || !QDir().mkpath(pluginDirectory_) )
            {
                report(plugins::JedMessageType::Warning, tr("Couldn't create the plug-ins folder %1").arg(pluginDirectory_));
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(pluginDirectory_));
        });
}

void JedPluginManager::addFolder(QMenu* menu, const PluginMenuFolder& folder)
{
    for ( const PluginMenuFolder& child : folder.folders )
    {
        addFolder(menu->addMenu(child.title), child);
    }
    for ( const PluginMenuEntry& entry : folder.entries )
    {
        QAction* action = menu->addAction(entry.title);
        action->setData(entry.path);
        action->setStatusTip(QDir::toNativeSeparators(entry.path));
        if ( !entry.shortcut.isEmpty() )
        {
            action->setShortcut(entry.shortcut);
        }
        connect(action, &QAction::triggered, this, [this, entry]() { run(entry); });
    }
}

bool JedPluginManager::run(const PluginMenuEntry& entry)
{
    const bool ok = entry.library ? runLibrary(entry) : launchExternal(entry);
    commitPendingEdit();
    return ok;
}

bool JedPluginManager::runLibrary(const PluginMenuEntry& entry)
{
    const QString name = QFileInfo(entry.path).fileName();
    const plugins::PluginBinaryArchitecture pluginArch = fileArchitecture(entry.path);
    const plugins::PluginBinaryArchitecture hostArch = hostArchitecture();
    if ( pluginArch != plugins::PluginBinaryArchitecture::unknown && hostArch != plugins::PluginBinaryArchitecture::unknown
        && pluginArch != hostArch )
    {
        report(plugins::JedMessageType::Error,
            tr("Couldn't load plugin %1: it is a %2 DLL, but RED is %3. Install a %3 build of this plug-in.")
                .arg(name, QString::fromLatin1(plugins::pluginArchitectureName(pluginArch)),
                    QString::fromLatin1(plugins::pluginArchitectureName(hostArch))));
        return false;
    }

    const QString canonical = QFileInfo(entry.path).canonicalFilePath();
    QLibrary* library = nullptr;
    for ( const std::unique_ptr<QLibrary>& loaded : libraries_ )
    {
        if ( QFileInfo(loaded->fileName()).canonicalFilePath() == canonical )
        {
            library = loaded.get();
            break;
        }
    }
    if ( library == nullptr )
    {
        auto candidate = std::make_unique<QLibrary>(entry.path);
        if ( !candidate->load() )
        {
            report(plugins::JedMessageType::Error, tr("Couldn't load plugin %1: %2").arg(name, candidate->errorString()));
            return false;
        }
        const bool jedPlugin = candidate->resolve(plugins::jed::kJedPluginLoadStdCallSymbol) != nullptr
            || candidate->resolve(plugins::jed::kJedPluginLoadSymbol) != nullptr;
        if ( !jedPlugin )
        {
            const bool sedPlugin = candidate->resolve(plugins::jed::kSedPluginLoadStdCallSymbol) != nullptr
                || candidate->resolve(plugins::jed::kSedPluginLoadSymbol) != nullptr;
            candidate->unload();
            report(plugins::JedMessageType::Error,
                sedPlugin ? tr("The DLL %1 is a SED (ISED) plug-in. RED supports JED (IJED) plug-ins only.").arg(name)
                          : tr("The DLL %1 is not a SED or JED plug-in: No load function found!").arg(name));
            return false;
        }
        library = candidate.get();
        libraries_.push_back(std::move(candidate));
    }

    bool result = false;
    try
    {
        if ( auto stdCall = reinterpret_cast<plugins::jed::JedPluginLoadStdCall>(
                 library->resolve(plugins::jed::kJedPluginLoadStdCallSymbol)) )
        {
            result = stdCall(host_.jed());
        }
        else if ( auto plain = reinterpret_cast<plugins::jed::JedPluginLoad>(
                      library->resolve(plugins::jed::kJedPluginLoadSymbol)) )
        {
            result = plain(host_.jed());
        }
    }
    catch ( const std::exception& error )
    {
        report(plugins::JedMessageType::Error, tr("Exception raised by %1: %2").arg(name, QString::fromLocal8Bit(error.what())));
        return false;
    }
    catch ( ... )
    {
        report(plugins::JedMessageType::Error, tr("Exception raised by %1: unknown exception").arg(name));
        return false;
    }
    if ( !result )
    {
        report(plugins::JedMessageType::Warning, tr("Plug-in %1 returned error while executing!").arg(name));
    }
    return result;
}

bool JedPluginManager::launchExternal(const PluginMenuEntry& entry)
{
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(entry.path).toStdWString();
    const std::wstring directory = QDir::toNativeSeparators(QFileInfo(entry.path).absolutePath()).toStdWString();
    const auto owner = reinterpret_cast<HWND>(windowHandle(plugins::jed::jw_Main));
    const auto code = reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, nullptr, path.c_str(), L"/JED", directory.c_str(), SW_SHOWNORMAL));
    if ( code > 32 )
    {
        return true;
    }
#endif
    report(plugins::JedMessageType::Error, tr("Couldn't start plugin %1").arg(QFileInfo(entry.path).fileName()));
    return false;
}

void JedPluginManager::ensurePending()
{
    if ( pending_ )
    {
        return;
    }
    pending_ = editor::DocumentState{ services_.level(), services_.selection() };
    pendingSignature_ = services_.documentSignature();
    pendingLabel_ = nextLabel_.empty() ? std::string("Plugin Edit") : nextLabel_;
    nextLabel_.clear();
}

void JedPluginManager::scheduleCommit()
{
    if ( commitScheduled_ )
    {
        return;
    }
    commitScheduled_ = true;
    QTimer::singleShot(0, this, [this]()
        {
            commitScheduled_ = false;
            commitPendingEdit();
        });
}

void JedPluginManager::flushSelection()
{
    if ( selectionDirty_ )
    {
        selectionDirty_ = false;
        services_.selectionChanged();
    }
}

void JedPluginManager::commitPendingEdit()
{
    if ( pending_ )
    {
        editor::DocumentState next = std::move(*pending_);
        pending_.reset();
        if ( services_.documentSignature() != pendingSignature_ )
        {
            pendingCursor_.reset();
            report(plugins::JedMessageType::Warning,
                tr("A plug-in edit was discarded because the level changed before it finished."));
        }
        else
        {
            services_.commit(pendingLabel_, std::move(next));
            selectionDirty_ = false;
        }
    }
    if ( pendingCursor_ )
    {
        const plugins::JedCursor cursor = *pendingCursor_;
        pendingCursor_.reset();
        services_.setCursor(cursor);
    }
    flushSelection();
}

const Level& JedPluginManager::level() const
{
    return pending_ ? pending_->level : services_.level();
}

Level& JedPluginManager::mutableLevel()
{
    ensurePending();
    scheduleCommit();
    return pending_->level;
}

const editor::Selection& JedPluginManager::selection() const
{
    return pending_ ? pending_->selection : services_.selection();
}

editor::Selection& JedPluginManager::mutableSelection()
{
    scheduleCommit();
    if ( pending_ )
    {
        return pending_->selection;
    }
    selectionDirty_ = true;
    return services_.liveSelection();
}

void JedPluginManager::startUndo(const std::string& label)
{
    commitPendingEdit();
    nextLabel_ = label.empty() ? std::string{} : "Plugin: " + label;
}

void JedPluginManager::updateMap()
{
    commitPendingEdit();
}

void JedPluginManager::message(const plugins::JedMessageType type, const std::string& text)
{
    report(type, QString::fromLocal8Bit(text.data(), static_cast<qsizetype>(text.size())));
}

void JedPluginManager::report(const plugins::JedMessageType type, const QString& text)
{
    if ( services_.message )
    {
        services_.message(type, text);
    }
}

std::int32_t JedPluginManager::mapMode() const
{
    return services_.mapMode();
}

void JedPluginManager::setMapMode(const std::int32_t mode)
{
    commitPendingEdit();
    services_.setMapMode(mode);
}

plugins::JedCursor JedPluginManager::cursor() const
{
    return pendingCursor_ ? *pendingCursor_ : services_.cursor();
}

void JedPluginManager::setCursor(const plugins::JedCursor& cursor)
{
    if ( pending_ )
    {
        // Defer until the edit commits: the cursor may name items that only exist in the pending level.
        pendingCursor_ = cursor;
        return;
    }
    services_.setCursor(cursor);
}

std::optional<std::vector<std::uint8_t>> JedPluginManager::readFile(const std::string& name, const bool gameFile)
{
    if ( gameFile )
    {
        std::optional<std::vector<std::byte>> bytes = services_.readGameFile(name);
        if ( !bytes )
        {
            return std::nullopt;
        }
        std::vector<std::uint8_t> out(bytes->size());
        for ( std::size_t i = 0; i < bytes->size(); ++i )
        {
            out[i] = std::to_integer<std::uint8_t>((*bytes)[i]);
        }
        return out;
    }
    QFile file(QString::fromLocal8Bit(name.data(), static_cast<qsizetype>(name.size())));
    if ( !file.open(QIODevice::ReadOnly) || file.size() < 0
        || static_cast<std::uint64_t>(file.size()) > kMaximumPluginFileBytes )
    {
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    if ( data.size() != file.size() )
    {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

std::optional<std::vector<CogValue>> JedPluginManager::cogScriptSymbols(const std::string& scriptName)
{
    return services_.cogScriptSymbols(scriptName);
}

std::string JedPluginManager::jedString(const std::int32_t what) const
{
    return services_.jedString(what).toLocal8Bit().toStdString();
}

std::intptr_t JedPluginManager::windowHandle(const std::int32_t which) const
{
    return services_.windowHandle ? services_.windowHandle(which) : 0;
}

bool JedPluginManager::isLayerVisible(const std::int32_t layer) const
{
    return services_.isLayerVisible ? services_.isLayerVisible(layer) : true;
}

} // namespace sed::app_plugins
