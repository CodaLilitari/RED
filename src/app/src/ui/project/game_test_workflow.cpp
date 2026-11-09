#include "ui/project/game_test_workflow.hpp"

#include "app_settings.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/package.hpp"
#include "ui/resources/project_resource_settings.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sed::ui::project
{
namespace
{
namespace fs = std::filesystem;
constexpr qint64 kMaximumStageBytes = 128ll * 1024 * 1024;

QString display(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
fs::path path(const QString& value) { return fs::path(value.toStdWString()); }

void writeAtomic(const fs::path& destination, const std::vector<std::byte>& bytes)
{
    fs::create_directories(destination.parent_path());
    QSaveFile file(display(destination));
    file.setDirectWriteFallback(false);
    if ( !file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()))
            != static_cast<qint64>(bytes.size())
        || !file.commit() )
    {
        throw std::runtime_error("Could not write the owned test stage: " + file.errorString().toStdString());
    }
}

std::string shortPath(const fs::path& value)
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetShortPathNameW(value.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if ( count > 0 && count < buffer.size() )
    {
        buffer.resize(count);
        return fs::path(buffer).string();
    }
#else
    (void)value;
#endif
    return {};
}

fs::path defaultExecutable(const fs::path& directory, const ProjectType game, const bool openJk)
{
#ifdef _WIN32
    const std::vector<std::string> names = openJk
        ? std::vector<std::string>{ "openjkdf2-64.exe", "openjkdf2.exe", "openjkdf2-32.exe" }
        : game == ProjectType::MOTS ? std::vector<std::string>{ "jkm.exe" }
                                  : std::vector<std::string>{ "jk.exe", "JediKnight.EXE" };
#else
    (void)game;
    const std::vector<std::string> names = openJk
        ? std::vector<std::string>{ "openjkdf2", "openjkdf2-64" }
        : std::vector<std::string>{ "jk.exe" };
#endif
    for ( const auto& name : names )
    {
        std::error_code error;
        if ( fs::is_regular_file(directory / name, error) && !error ) return directory / name;
    }
    return directory.empty() ? fs::path{} : directory / names.front();
}

std::string suggestedMapName(const fs::path& source)
{
    std::string name;
    for ( const unsigned char c : source.stem().string() )
    {
        if ( name.size() == 27 ) break;
        name += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-') ? static_cast<char>(c) : '_';
    }
    return (name.empty() ? "level" : name) + ".jkl";
}

level::PackagePlan buildStagePackage(const GameTestContext& context,
    const level::GameLaunchRequest& launch, const fs::path& exportedMap)
{
    QFile file(display(exportedMap));
    if ( !file.open(QIODevice::ReadOnly) || file.size() > kMaximumStageBytes )
        throw std::runtime_error("The exported test map is missing or exceeds the 128 MiB staging budget.");
    const QByteArray bytes = file.read(kMaximumStageBytes + 1);
    if ( bytes.size() > kMaximumStageBytes || !file.atEnd() || file.error() != QFileDevice::NoError )
        throw std::runtime_error("Could not read the complete exported test map.");

    assets::ResourceLocator runtimeResources;
    const auto detected = resources::findDefaultProjectResources(context.game, launch.gameDirectory);
    for ( const auto& root : detected.roots ) runtimeResources.addRoot(root);
    for ( const auto& archive : detected.gobs ) runtimeResources.mountGob(archive);

    level::PackageRequest package;
    package.inputBytes.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    package.levelFileName = launch.levelFileName;
    package.episodeTitle = "RED Test: " + launch.levelFileName;
    package.archiveExtension = context.game == ProjectType::MOTS ? ".goo" : ".gob";
    package.resolveResource = [&](const std::string_view name, const std::size_t budget)
        -> std::optional<level::PackageResolvedResource>
    {
        bool exceeded = false;
        auto selected = context.resources ? context.resources->tryReadBinary(name, budget, exceeded)
            : std::optional<std::vector<std::byte>>{};
        if ( exceeded )
            throw std::runtime_error("Selected editor resource exceeds the staging budget: " + std::string(name));
        bool runtimeExceeded = false;
        auto runtime = runtimeResources.tryReadBinary(name, budget, runtimeExceeded);
        if ( !selected && runtimeExceeded )
            throw std::runtime_error("Selected game resource exceeds the staging budget: " + std::string(name));
        if ( !selected ) selected = runtime;
        if ( !selected ) return std::nullopt;
        const bool stock = runtime && *selected == *runtime;
        return level::PackageResolvedResource{
            stock ? "selected game installation" : "editor resource snapshot",
            std::string(reinterpret_cast<const char*>(selected->data()), selected->size()), stock };
    };
    auto result = level::planLevelPackage(package);
    if ( !result.canBuild )
    {
        std::string error = "Cannot prepare the current test episode.";
        for ( const auto& issue : result.issues )
        {
            if ( issue.severity == "error" ) error += "\n" + issue.reference + ": " + issue.message;
        }
        throw std::runtime_error(error);
    }
    return result;
}
} // namespace

GameTestResult saveJklAndTest(QWidget* parent, const GameTestContext& context)
{
    const QPointer<QWidget> ownerGuard(parent);
    const bool hasOwner = parent != nullptr;
    const auto ownerAlive = [&]() { return !hasOwner || !ownerGuard.isNull(); };
    if ( context.game != ProjectType::JKDF2 && context.game != ProjectType::MOTS )
        throw std::runtime_error("Save JKL & Test supports Jedi Knight and Mysteries of the Sith.");
    const QString projectKey = context.game == ProjectType::MOTS ? QStringLiteral("mots") : QStringLiteral("jkdf2");
    const QString key = QStringLiteral("game_test/%1/").arg(projectKey);
    auto settings = settingsStore();
    QPointer<QDialog> dialogGuard(new QDialog(parent));
    const auto dialogCleanup = qScopeGuard([&dialogGuard]()
    {
        if ( dialogGuard ) dialogGuard->deleteLater();
    });
    QDialog& dialog = *dialogGuard;
    dialog.setObjectName(QStringLiteral("gameTestDialog"));
    dialog.setWindowTitle(QStringLiteral("Save JKL & Test"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* engine = new QComboBox(&dialog);
    engine->setObjectName(QStringLiteral("gameTestEngine"));
    engine->addItem(QStringLiteral("OpenJKDF2 (v0.9.9 compatible)"), QStringLiteral("OpenJKDF2"));
    engine->addItem(QStringLiteral("Retail JK / MotS"), QStringLiteral("Retail"));
    engine->setCurrentIndex(settings.value(key + "engine").toString() == "Retail" ? 1 : 0);
    form->addRow(QStringLiteral("Engine"), engine);
    const auto addPathRow = [&](const QString& label, const char* name, const QString& initial, const bool directory)
    {
        auto* row = new QWidget(&dialog);
        auto* horizontal = new QHBoxLayout(row);
        horizontal->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(initial, row);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setMinimumWidth(360);
        auto* browse = new QPushButton(QStringLiteral("Browse..."), row);
        horizontal->addWidget(edit);
        horizontal->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&, edit, directory]()
        {
            const QString chosen = directory
                ? QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Choose folder"), edit->text())
                : QFileDialog::getOpenFileName(&dialog, QStringLiteral("Choose game executable"), edit->text());
            if ( dialogGuard && !chosen.isEmpty() ) edit->setText(chosen);
        });
        form->addRow(label, row);
        return edit;
    };
    auto* game = addPathRow(QStringLiteral("Game installation"), "gameTestGameDirectory",
        settings.value(key + "gameDirectory", display(context.defaultGameDirectory)).toString(), true);
    const auto executableSetting = [&]()
    {
        return key + (engine->currentData().toString() == "Retail" ? "retailExecutable" : "openjkExecutable");
    };
    auto* executable = addPathRow(QStringLiteral("Executable"), "gameTestExecutable",
        settings.value(executableSetting(), display(defaultExecutable(path(game->text()), context.game,
            engine->currentIndex() == 0))).toString(), false);
    auto* stageRoot = addPathRow(QStringLiteral("Test output folder"), "gameTestStageRoot",
        settings.value(key + "stageRoot", QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("RED-tests"))).toString(), true);
    auto* levelName = new QLineEdit(QString::fromStdString(suggestedMapName(context.sourcePath)), &dialog);
    levelName->setObjectName(QStringLiteral("gameTestLevelName"));
    form->addRow(QStringLiteral("Test map filename"), levelName);
    auto* description = new QLabel(&dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    const auto updateDescription = [&]()
    {
        description->setText(engine->currentIndex() == 0
            ? QStringLiteral("OpenJKDF2 starts this map directly. Select a v0.9.9-compatible executable; RED does not infer feature support from its filename. A new isolated test episode will be created. Your editable source path and unsaved changes are preserved.")
            : QStringLiteral("Retail has no direct-map command-line option. After launch, choose New Game and the RED Test episode. A new isolated override will be created; the game installation and editable source are preserved."));
    };
    QObject::connect(engine, &QComboBox::currentIndexChanged, &dialog, [&]()
    {
        executable->setText(settings.value(executableSetting(), display(defaultExecutable(path(game->text()),
            context.game, engine->currentIndex() == 0))).toString());
        updateDescription();
    });
    updateDescription();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* run = buttons->addButton(QStringLiteral("Save JKL && Test"), QDialogButtonBox::AcceptRole);
    run->setObjectName(QStringLiteral("gameTestRun"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    const int decision = dialog.exec();
    if ( !ownerAlive() || !dialogGuard || decision != QDialog::Accepted ) return {};

    level::GameLaunchRequest launch;
    launch.game = context.game;
    launch.engine = engine->currentIndex() == 0 ? level::GameTestEngine::OpenJKDF2 : level::GameTestEngine::Retail;
    launch.gameDirectory = path(game->text());
    launch.executable = path(executable->text());
    launch.levelFileName = levelName->text().toStdString();
    level::validateGameTestLevelName(launch.levelFileName);
    if ( game->text().isEmpty() || executable->text().isEmpty() || stageRoot->text().isEmpty() )
        throw std::runtime_error("Game installation, executable and test output folder are required.");
    const auto canonicalGame = fs::weakly_canonical(launch.gameDirectory);
    const auto canonicalStageRoot = fs::weakly_canonical(path(stageRoot->text()));
    if ( resources::pathIsUnderInstallDir(canonicalStageRoot, canonicalGame) )
        throw std::runtime_error("Choose a test output folder outside the game installation.");
    if ( !QDir().mkpath(display(canonicalStageRoot)) )
        throw std::runtime_error("Could not create the test output folder.");
    QTemporaryDir ownedStage(QDir(display(canonicalStageRoot)).filePath(QStringLiteral("redtest-XXXXXX")));
    if ( !ownedStage.isValid() ) throw std::runtime_error("Could not allocate an owned test stage.");
    launch.projectDirectory = path(ownedStage.path());
    launch.shortProjectPath = shortPath(launch.projectDirectory);
    const auto initialPlan = level::prepareGameLaunch(launch);
    if ( !initialPlan.gameDirectoryExists || !initialPlan.executableExists || !initialPlan.installMarkerExists )
        throw std::runtime_error("The selected game installation or executable is missing. OpenJKDF2 also requires resource/jk_.cd.");
    settings.setValue(key + "engine", engine->currentData());
    settings.setValue(key + "gameDirectory", game->text());
    settings.setValue(executableSetting(), executable->text());
    settings.setValue(key + "stageRoot", stageRoot->text());
    settings.sync();
    const auto exportedMap = launch.projectDirectory / "jkl" / launch.levelFileName;
    fs::create_directories(exportedMap.parent_path());
    const bool exported = context.exportMap && context.exportMap(exportedMap);
    if ( !ownerAlive() ) return {};
    if ( !exported )
        throw std::runtime_error("The current map was not exported; the game was not started.");

    auto pending = std::async(std::launch::async, [context, launch, exportedMap]()
    {
        return buildStagePackage(context, launch, exportedMap);
    });
    // Keep progress lifetime independent from the owner: deleting a parent during
    // the synchronous Show event must not destroy the widget inside show().
    QPointer<QProgressDialog> progressGuard(new QProgressDialog(
        QStringLiteral("Preparing test resources..."), QString{}, 0, 0, nullptr));
    const auto progressCleanup = qScopeGuard([&progressGuard]()
    {
        if ( progressGuard )
        {
            progressGuard->close();
            if ( progressGuard ) progressGuard->deleteLater();
        }
    });
    QProgressDialog& progress = *progressGuard;
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.show();
    if ( !ownerAlive() || !progressGuard ) return {};
    while ( pending.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready )
    {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
        if ( !ownerAlive() || !progressGuard ) return {};
    }
    const auto package = pending.get();
    progress.close();
    if ( !ownerAlive() ) return {};
    const bool snapshotCurrent = context.snapshotStillCurrent && context.snapshotStillCurrent();
    if ( !ownerAlive() ) return {};
    if ( !snapshotCurrent )
        throw std::runtime_error("The document changed during export or staging; test it again to run the current map.");
    const auto* data = reinterpret_cast<const std::byte*>(package.outputData.data());
    auto archive = assets::GobArchive::fromBytes(std::vector<std::byte>(data, data + package.outputData.size()));
    for ( const auto& entry : archive.entries() )
    {
        std::string name = entry.name;
        std::replace(name.begin(), name.end(), '\\', '/');
        if ( name.size() + 1 + initialPlan.projectPathToken.size() > 127 )
            throw std::runtime_error("A test resource exceeds the engine path budget: " + name);
        writeAtomic(launch.projectDirectory / fs::path(name), archive.read(entry.name));
    }
    if ( !package.unverifiedScopes.empty() )
    {
        QPointer<QDialog> reviewGuard(new QDialog(ownerGuard.data()));
        const auto reviewCleanup = qScopeGuard([&reviewGuard]()
        {
            if ( reviewGuard )
            {
                reviewGuard->deleteLater();
            }
        });
        QDialog& review = *reviewGuard;
        review.setObjectName(QStringLiteral("gameTestDependencyReview"));
        review.setWindowTitle(QStringLiteral("Review test resources"));
        auto* reviewLayout = new QVBoxLayout(&review);
        auto* explanation = new QLabel(QStringLiteral(
            "The current map and its known resources are prepared. Some AI or COG resources can load "
            "additional files that RED cannot verify before running the game. Test this prepared episode "
            "and check for missing resources in game, or cancel."), &review);
        explanation->setWordWrap(true);
        explanation->setMinimumWidth(440);
        reviewLayout->addWidget(explanation);
        QStringList scopes;
        for ( const auto& scope : package.unverifiedScopes )
        {
            scopes.append(QString::fromStdString(scope));
        }
        auto* details = new QLabel(QStringLiteral("Unverified checks: %1").arg(scopes.join(", ")), &review);
        details->setWordWrap(true);
        reviewLayout->addWidget(details);
        auto* reviewButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &review);
        auto* testPrepared = reviewButtons->addButton(QStringLiteral("Test prepared episode"), QDialogButtonBox::AcceptRole);
        testPrepared->setObjectName(QStringLiteral("gameTestDependencyRun"));
        QObject::connect(reviewButtons, &QDialogButtonBox::accepted, &review, &QDialog::accept);
        QObject::connect(reviewButtons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
        reviewLayout->addWidget(reviewButtons);
        const int reviewDecision = review.exec();
        if ( !ownerAlive() || !reviewGuard || reviewDecision != QDialog::Accepted )
        {
            return {};
        }
    }
    const auto plan = level::prepareGameLaunch(launch);
    const bool readyAndCurrent = plan.ready && context.snapshotStillCurrent();
    if ( !ownerAlive() ) return {};
    if ( !readyAndCurrent )
        throw std::runtime_error("The prepared test stage is no longer ready or the current document changed.");
    QStringList arguments;
    for ( const auto& argument : plan.arguments ) arguments.append(QString::fromStdString(argument));
    QProcess process;
    process.setProgram(display(plan.executable));
    process.setArguments(arguments);
    process.setWorkingDirectory(display(plan.workingDirectory));
    ownedStage.setAutoRemove(false);
    qint64 processId = 0;
    if ( !process.startDetached(&processId) )
        throw std::runtime_error("Could not start the selected game: " + process.errorString().toStdString()
            + "\nPrepared test stage: " + launch.projectDirectory.string());
    return { true, plan.directMapStart, launch.projectDirectory };
}
} // namespace sed::ui::project
