#include "ui/resources/package_authoring_dialog.hpp"
#include "sed/assets/archive_admission.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/runtime_archive.hpp"
#include "sed/level/package.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "ui/components/ui_decimal_format.hpp"

// Reuse the established create-only publication protocol in the Qt adapter.
// Portable archive/package planners never perform filesystem publication.
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sed::ui::resources
{
namespace
{
constexpr std::size_t kMaxLevelBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMaxCaptureBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxExpandedBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxArchiveRows = 100000;
constexpr int kMaxArchives = 16;
constexpr int kMaxExtras = 1024;
using Guard = detail::PreviewWorkerPublicationGuard;
using Stage = assets::detail::StagedAssetFile;

std::filesystem::path filePath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toUtf8().toStdString());
#endif
}
QString text(const std::string_view bytes)
{
    constexpr char kHex[] = "0123456789ABCDEF";
    QString result;
    for ( const unsigned char byte : bytes )
    {
        if ( byte < 0x20 || byte > 0x7e )
        {
            result += "\\x";
            result += QLatin1Char(kHex[byte >> 4]);
            result += QLatin1Char(kHex[byte & 15]);
        }
        else
        {
            result += QLatin1Char(static_cast<char>(byte));
        }
    }
    return result;
}
QString number(const std::size_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}
void active(const Guard& guard)
{
    if ( !detail::previewWorkerMayPublish(guard, guard.generation) )
    {
        throw std::runtime_error("Cancelled. No output was published.");
    }
}
struct Source
{
    QString path;
    QString canonical;
    std::size_t bytes{};
    QByteArray hash;
};
struct ArchiveSource
{
    QString path;
    bool stock{};
};
struct Request
{
    bool package{};
    QString extension;
    std::vector<std::pair<QString, QString>> entries;
    QString level;
    level::PackageRequest packageRequest;
    std::vector<ArchiveSource> archives;
};
struct Prepared
{
    std::vector<Source> sources;
    std::string data;
    QString extension;
    QString outputHash;
    QString manifestHash;
    QString report;
    std::vector<QStringList> contents;
    std::vector<QStringList> issues;
    bool canBuild{};
};
struct Result
{
    std::shared_ptr<Prepared> prepared;
    std::shared_ptr<Stage> staged;
    QString destination;
    QString error;
};

std::string capture(const QString& entered, const std::size_t maximum, Source& source, const Guard& guard)
{
    active(guard);
    if ( entered.isEmpty() || entered.contains(QChar::Null) || entered.toUtf8().size() > 4096 )
    {
        throw std::runtime_error("Choose a nonempty input path of at most 4096 UTF-8 bytes.");
    }
    const QFileInfo before(entered);
    if ( !before.isFile() || before.size() < 0 || static_cast<qulonglong>(before.size()) > maximum )
    {
        throw std::runtime_error("Input is not a regular file or exceeds the capture-byte limit.");
    }
    QFile file(entered);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(file.errorString().toUtf8().toStdString());
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::string data;
    data.reserve(static_cast<std::size_t>(before.size()));
    while ( !file.atEnd() )
    {
        active(guard);
        const auto count = static_cast<qint64>(std::min<std::size_t>(1024u * 1024u, maximum - data.size() + 1u));
        const QByteArray chunk = file.read(count);
        if ( file.error() != QFileDevice::NoError || (chunk.isEmpty() && !file.atEnd()) )
        {
            throw std::runtime_error("Input could not be captured completely.");
        }
        if ( static_cast<std::size_t>(chunk.size()) > maximum - data.size() )
        {
            throw std::runtime_error("Input grew beyond the capture-byte limit.");
        }
        hash.addData(chunk);
        data.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
    }
    const QFileInfo after(entered);
    if ( !after.isFile() || before.size() != after.size() || before.lastModified() != after.lastModified()
        || before.canonicalFilePath() != after.canonicalFilePath()
        || static_cast<std::size_t>(after.size()) != data.size() )
    {
        throw std::runtime_error("Input changed during capture; prepare again.");
    }
    source = {before.absoluteFilePath(), before.canonicalFilePath(), data.size(), hash.result()};
    return data;
}
void verifySources(const Prepared& prepared, const Guard& guard)
{
    for ( const auto& source : prepared.sources )
    {
        Source current;
        (void)capture(source.path, source.bytes, current, guard);
        if ( current.canonical != source.canonical || current.bytes != source.bytes || current.hash != source.hash )
        {
            throw std::runtime_error("A captured source changed; prepare and review again.");
        }
    }
}
void requireNew(const Prepared& prepared, const QString& destination)
{
    const QFileInfo target(destination);
    if ( destination.isEmpty() || destination.contains(QChar::Null) || destination.toUtf8().size() > 4096 )
    {
        throw std::runtime_error("Choose a nonempty output path of at most 4096 UTF-8 bytes.");
    }
    if ( target.exists() || target.isSymLink() )
    {
        throw std::runtime_error("Output already exists; choose a new filename.");
    }
    if ( !QFileInfo(target.absolutePath()).isDir() )
    {
        throw std::runtime_error("The output directory does not exist.");
    }
    for ( const auto& source : prepared.sources )
    {
        if ( target.absoluteFilePath() == source.path || target.absoluteFilePath() == source.canonical )
        {
            throw std::runtime_error("Output must differ from every source file.");
        }
    }
}
QString hashOf(const std::string_view bytes)
{
    // Prepared archive bytes are capped at 512 MiB, within Qt 5's int length.
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromRawData(bytes.data(), static_cast<int>(bytes.size())), QCryptographicHash::Sha256).toHex());
}
void appendSources(Prepared& prepared)
{
    prepared.report += "\nCaptured sources (raw file SHA-256):\n";
    for ( const auto& source : prepared.sources )
    {
        prepared.report += source.path + "\n" + number(source.bytes) + " bytes; "
            + QString::fromLatin1(source.hash.toHex()) + '\n';
    }
    prepared.report += "\nOutput SHA-256: " + prepared.outputHash + "\n";
    if ( !prepared.manifestHash.isEmpty() )
    {
        prepared.report += "Manifest SHA-256: " + prepared.manifestHash + "\n";
    }
    prepared.report += "Output bytes: " + number(prepared.data.size()) + "\nRuntime closure verified: no\n";
}
std::shared_ptr<Result> prepareFiles(Request request, const Guard guard)
{
    auto result = std::make_shared<Result>();
    try
    {
        auto prepared = std::make_shared<Prepared>();
        prepared->extension = request.extension;
        if ( !request.package )
        {
            assets::RuntimeArchiveBuilder builder(request.entries.size());
            std::size_t remaining = assets::kRuntimeArchiveMaxBytes - 16u - 136u * request.entries.size();
            for ( const auto& [path, name] : request.entries )
            {
                Source source;
                auto bytes = capture(path, std::min(assets::kRuntimeArchiveMaxSourceBytes, remaining), source, guard);
                remaining -= bytes.size();
                builder.add({name.toUtf8().toStdString(), std::move(bytes)});
                prepared->sources.push_back(std::move(source));
            }
            active(guard);
            auto built = builder.finish();
            prepared->data = std::move(built.outputData);
            prepared->canBuild = true;
            prepared->outputHash = hashOf(prepared->data);
            prepared->report = "Resource archive\nPhysical name\tBytes\tSHA-256\tReadback\n";
            for ( const auto& entry : built.entries )
            {
                prepared->contents.push_back({text(entry.name), "Captured file", number(entry.bytes),
                    text(entry.sha256), text(entry.readBack)});
                prepared->report += text(entry.name) + '\t' + number(entry.bytes) + '\t'
                    + text(entry.sha256) + '\t' + text(entry.readBack) + '\n';
            }
            for ( const auto& warning : built.warnings )
            {
                prepared->issues.push_back({"Warning", "Archive layout", "", text(warning)});
                prepared->report += "Warning: " + text(warning) + '\n';
            }
        }
        else
        {
            if ( QFileInfo(request.level).suffix().compare("jkl", Qt::CaseInsensitive) != 0 )
            {
                throw std::runtime_error("Package input must be an explicit exported .jkl file.");
            }
            Source levelSource;
            request.packageRequest.inputBytes = capture(request.level, kMaxLevelBytes, levelSource, guard);
            prepared->sources.push_back(std::move(levelSource));
            std::size_t remainingCapture = kMaxCaptureBytes;
            std::size_t remainingExpanded = kMaxExpandedBytes;
            std::size_t remainingRows = kMaxArchiveRows;
            // The agent searches resource archives before stock archives.
            std::stable_sort(request.archives.begin(), request.archives.end(),
                [](const auto& left, const auto& right) { return left.stock < right.stock; });
            for ( const auto& archiveSource : request.archives )
            {
                Source source;
                auto bytes = capture(archiveSource.path, remainingCapture, source, guard);
                remainingCapture -= bytes.size();
                const auto admission = assets::admitCapturedArchiveIndex(bytes, remainingRows);
                if ( admission.status != assets::ArchiveIndexAdmissionStatus::Accepted )
                {
                    throw std::runtime_error("Archive index is malformed or exceeds the remaining entry budget.");
                }
                std::vector<std::byte> raw(bytes.size());
                std::memcpy(raw.data(), bytes.data(), raw.size());
                bytes.clear();
                bytes.shrink_to_fit();
                const auto archive = assets::GobArchive::fromBytes(std::move(raw), filePath(source.path));
                level::PackageArchiveInput input;
                input.label = QFileInfo(source.path).fileName().toUtf8().toStdString();
                input.stock = archiveSource.stock;
                for ( const auto& row : archive.entries() )
                {
                    active(guard);
                    // Historical package materialization resolves by name. Charge
                    // that selected payload, including aliases, before allocating.
                    const auto found = archive.find(row.name);
                    if ( !found || remainingRows == 0 || found->size > remainingExpanded )
                    {
                        throw std::runtime_error("Archive entries exceed the expansion or entry budget.");
                    }
                    --remainingRows;
                    remainingExpanded -= found->size;
                    const auto payload = archive.read(row.name);
                    input.entries.emplace_back(row.name,
                        std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
                }
                request.packageRequest.archives.push_back(std::move(input));
                prepared->sources.push_back(std::move(source));
            }
            active(guard);
            auto plan = level::planLevelPackage(request.packageRequest);
            active(guard);
            prepared->canBuild = plan.canBuild;
            prepared->data = std::move(plan.outputData);
            prepared->outputHash = text(plan.outputSha256);
            prepared->manifestHash = text(plan.manifestSha256);
            prepared->report = QString("Exported level package\nBuildable: %1\nNodes: %2; edges: %3\n")
                .arg(plan.canBuild ? "yes" : "no", number(plan.nodes), number(plan.edges));
            const auto& reviewed = request.packageRequest;
            prepared->report += "Level filename: " + text(reviewed.levelFileName)
                + "\nEpisode title: " + text(reviewed.episodeTitle)
                + "\nEpisode type: " + QString::number(reviewed.episodeType)
                + "\nArchive format: " + text(reviewed.archiveExtension)
                + "\nMaximum nodes: " + number(reviewed.options.maxNodes)
                + "\nMaximum edges: " + number(reviewed.options.maxEdges)
                + "\nMaximum depth: " + number(reviewed.options.maxDepth)
                + "\nMaximum package bytes: " + number(reviewed.options.maxPackageBytes)
                + "\nAllow unverified references: "
                + (reviewed.options.allowUnverifiedReferences ? "yes\n" : "no\n");
            prepared->report += "Archive search order (resources before stock):\n";
            for ( const auto& archive : request.archives )
            {
                prepared->report += (archive.stock ? "Stock archive: " : "Resource archive: ")
                    + archive.path + '\n';
            }
            prepared->report += "Extra referenced assets (name / resource folder):\n";
            for ( const auto& extra : reviewed.extraResources )
            {
                prepared->report += text(extra.name) + " / " + text(extra.folder) + '\n';
            }
            prepared->report += "Physical path\tSource\tBytes\tSHA-256\tDisposition\n";
            const auto append = [&](const level::PackageEntry& entry, const QString& disposition)
            {
                const QStringList fields{text(entry.path), text(entry.source), number(entry.bytes),
                    text(entry.sha256), disposition};
                prepared->contents.push_back(fields);
                prepared->report += fields.join('\t') + '\n';
            };
            for ( const auto& entry : plan.entries )
            {
                append(entry, "Included");
            }
            for ( const auto& entry : plan.stockOmissions )
            {
                append(entry, "Stock; omitted");
            }
            for ( const auto& issue : plan.issues )
            {
                const QStringList fields{text(issue.severity), text(issue.code), text(issue.reference), text(issue.message)};
                prepared->issues.push_back(fields);
                prepared->report += fields.join('\t') + '\n';
            }
            prepared->report += "Unverified scopes:\n";
            for ( const auto& scope : plan.unverifiedScopes )
            {
                prepared->report += text(scope) + '\n';
            }
            prepared->report += "Manifest archive digests describe normalized entry data, not raw archive files.\n";
        }
        active(guard);
        appendSources(*prepared);
        result->prepared = std::move(prepared);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    catch ( ... )
    {
        result->error = "Preparation failed with an unexpected exception.";
    }
    return result;
}
std::shared_ptr<Result> stageFiles(const std::shared_ptr<const Prepared> prepared,
    const QString destination, const bool report, const Guard guard)
{
    auto result = std::make_shared<Result>();
    try
    {
        active(guard);
        requireNew(*prepared, destination);
        verifySources(*prepared, guard);
        if ( !report && (!prepared->canBuild || hashOf(prepared->data) != prepared->outputHash) )
        {
            throw std::runtime_error("The reviewed output identity changed; prepare again.");
        }
        const QByteArray reportBytes = report ? prepared->report.toUtf8() : QByteArray{};
        const auto bytes = report
            ? std::as_bytes(std::span(reportBytes.constData(), static_cast<std::size_t>(reportBytes.size())))
            : std::as_bytes(std::span(prepared->data.data(), prepared->data.size()));
        auto staged = std::make_shared<Stage>(filePath(destination));
        staged->writeAndClose(bytes);
        verifySources(*prepared, guard);
        active(guard);
        result->staged = std::move(staged);
        result->destination = destination;
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    catch ( ... )
    {
        result->error = "Staging failed with an unexpected exception.";
    }
    return result;
}
} // namespace

class PackageAuthoringDialog::Impl
{
public:
    Impl(PackageAuthoringDialog& dialog, Context context) : dialog_(dialog), context_(std::move(context))
    {
        dialog_.setWindowTitle("Archive and Package Authoring");
        dialog_.resize(900, 760);
        auto* outer = new QVBoxLayout(&dialog_);
        auto* help = new QLabel("Prepare and review captured files, then explicitly create a new GOB/GOO. "
            "Sources and the live level are unchanged. Runtime dependency closure is not certified.", &dialog_);
        help->setWordWrap(true);
        outer->addWidget(help);
        auto* scroll = new QScrollArea(&dialog_);
        scroll->setWidgetResizable(true);
        auto* body = new QWidget(scroll);
        auto* layout = new QVBoxLayout(body);
        mode_ = new QTabWidget(body);
        mode_->setObjectName("packageMode");
        auto* archivePage = new QWidget(mode_);
        auto* archiveLayout = new QVBoxLayout(archivePage);
        entries_ = table("packageEntries", {"Source file", "Logical archive entry"}, archiveLayout);
        addRows(entries_, archiveLayout, "packageEntry", 4096, false);
        auto* archiveHint = new QLabel("1–4096 explicit files; 256 MiB per file, 512 MiB archive including index. "
            "Names become lowercase with backslashes; payloads are preserved.", archivePage);
        archiveHint->setWordWrap(true);
        archiveLayout->addWidget(archiveHint);
        mode_->addTab(archivePage, "Resource Archive");
        auto* packagePage = new QWidget(mode_);
        auto* packageLayout = new QVBoxLayout(packagePage);
        auto* form = new QFormLayout;
        level_ = line("packageLevel");
        form->addRow("Exported JKL", pathRow(level_, "packageBrowseLevel", false, "Exported levels (*.jkl)"));
        levelName_ = line("packageLevelName");
        levelName_->setText("level.jkl");
        form->addRow("Level name in package", levelName_);
        episode_ = line("packageEpisodeTitle");
        episode_->setMaxLength(80);
        episode_->setText("New episode");
        form->addRow("Episode title", episode_);
        episodeType_ = spin("packageEpisodeType", std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1);
        form->addRow("Episode type", episodeType_);
        packageLayout->addLayout(form);
        archives_ = table("packageArchives", {"Source GOB / GOO", "Omit as stock"}, packageLayout);
        addRows(archives_, packageLayout, "packageArchive", kMaxArchives, true);
        auto* orderHint = new QLabel("Resource archives are searched first, then stock archives, preserving order within each group. "
            "Stock payloads are traversed but omitted. Archive capture and expanded payloads are each limited to 512 MiB; "
            "100000 physical rows and 16 archives maximum.", packagePage);
        orderHint->setWordWrap(true);
        packageLayout->addWidget(orderHint);
        extras_ = table("packageExtras", {"Referenced asset name", "Resource folder"}, packageLayout);
        addRows(extras_, packageLayout, "packageExtra", kMaxExtras, false, false);
        auto* limits = new QFormLayout;
        nodes_ = spin("packageMaxNodes", 0, 100000, 4096);
        edges_ = spin("packageMaxEdges", 0, 1000000, 8192);
        depth_ = spin("packageMaxDepth", 0, 100000, 0);
        bytes_ = spin("packageMaxBytes", 0, 512 * 1024 * 1024, 128 * 1024 * 1024);
        limits->addRow("Maximum nodes (includes level + episode)", nodes_);
        limits->addRow("Maximum edges", edges_);
        limits->addRow("Maximum depth (0: no depth limit)", depth_);
        limits->addRow("Maximum output bytes", bytes_);
        allowMissing_ = new QCheckBox("Allow missing references as warnings", packagePage);
        allowMissing_->setObjectName("packageAllowMissing");
        limits->addRow(allowMissing_);
        packageLayout->addLayout(limits);
        mode_->addTab(packagePage, "Exported Level");
        layout->addWidget(mode_);
        auto* outputForm = new QFormLayout;
        format_ = new QComboBox(body);
        format_->setObjectName("packageFormat");
        format_->addItems({"GOB", "GOO"});
        outputForm->addRow("Archive format", format_);
        output_ = line("packageOutput");
        outputForm->addRow("New archive filename", pathRow(output_, "packageBrowseOutput", true, "Archives (*.gob *.goo)"));
        reportPath_ = line("packageReportPath");
        outputForm->addRow("New report filename", pathRow(reportPath_, "packageBrowseReport", true, "Text report (*.txt)"));
        layout->addLayout(outputForm);
        auto* reviews = new QTabWidget(body);
        auto* contentsPage = new QWidget(reviews);
        auto* contentsLayout = new QVBoxLayout(contentsPage);
        contents_ = table("packageContents", {"Physical path", "Source", "Bytes", "SHA-256", "Disposition / readback"}, contentsLayout);
        contents_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        reviews->addTab(contentsPage, "Contents");
        auto* issuesPage = new QWidget(reviews);
        auto* issuesLayout = new QVBoxLayout(issuesPage);
        issues_ = table("packageIssues", {"Severity", "Code", "Reference", "Message"}, issuesLayout);
        issues_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        reviews->addTab(issuesPage, "Issues");
        report_ = new QPlainTextEdit(reviews);
        report_->setObjectName("packageReport");
        report_->setReadOnly(true);
        report_->setMinimumHeight(160);
        reviews->addTab(report_, "Full report");
        layout->addWidget(reviews);
        scroll->setWidget(body);
        outer->addWidget(scroll);
        status_ = new QLabel("Choose a workflow and inputs, then Prepare.", &dialog_);
        status_->setObjectName("packageStatus");
        status_->setWordWrap(true);
        outer->addWidget(status_);
        auto* actions = new QHBoxLayout;
        prepare_ = button("Prepare", "packagePrepare", actions);
        create_ = button("Create new archive", "packageCreate", actions);
        cancel_ = button("Cancel work", "packageCancel", actions);
        outer->addLayout(actions);
        auto* reportActions = new QHBoxLayout;
        export_ = button("Export full report", "packageExportReport", reportActions);
        reportActions->addStretch();
        outer->addLayout(reportActions);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
        outer->addWidget(close);
        QObject::connect(close, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
        QObject::connect(prepare_, &QPushButton::clicked, &dialog_, [this] { invoke([this] { prepare(); }); });
        QObject::connect(create_, &QPushButton::clicked, &dialog_, [this] { invoke([this] { publish(false); }); });
        QObject::connect(export_, &QPushButton::clicked, &dialog_, [this] { invoke([this] { publish(true); }); });
        QObject::connect(cancel_, &QPushButton::clicked, &dialog_, [this]
        {
            invalidate();
            status_->setText("Cancelled. The bounded worker may finish, but its result will not publish.");
        });
        for ( auto* value : {level_, levelName_, episode_} )
        {
            QObject::connect(value, &QLineEdit::textChanged, &dialog_, [this] { invalidate(); });
        }
        for ( auto* value : {episodeType_, nodes_, edges_, depth_, bytes_} )
        {
            QObject::connect(value, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                &dialog_, [this] { invalidate(); });
        }
        for ( auto* value : {entries_, archives_, extras_} )
        {
            QObject::connect(value, &QTableWidget::cellChanged, &dialog_, [this] { invalidate(); });
        }
        QObject::connect(mode_, &QTabWidget::currentChanged, &dialog_, [this] { invalidate(); });
        QObject::connect(format_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            &dialog_, [this] { invalidate(); });
        QObject::connect(allowMissing_, &QCheckBox::toggled, &dialog_, [this] { invalidate(); });
        for ( auto* value : {output_, reportPath_} )
        {
            QObject::connect(value, &QLineEdit::textChanged, &dialog_, [this]
            {
                if ( publishing_ )
                {
                    invalidate(false);
                    status_->setText("Destination changed; staging cancelled. Choose a new filename and retry.");
                }
            });
        }
        watcher_ = new QFutureWatcher<std::shared_ptr<Result>>(&dialog_);
        QObject::connect(watcher_, &QFutureWatcherBase::finished, &dialog_, [this] { finish(); });
        updateActions();
    }
    ~Impl()
    {
        detail::cancelPreviewWorkerPublication(guard_);
    }
    void close()
    {
        closed_ = true;
        invalidate();
    }
private:
    template<class Action> void invoke(Action action)
    {
        const QPointer<PackageAuthoringDialog> owner(&dialog_);
        try
        {
            action();
        }
        catch ( const std::exception& error )
        {
            if ( owner )
            {
                busy_ = false;
                publishing_ = false;
                status_->setText(QString::fromUtf8(error.what()));
                updateActions();
            }
        }
        catch ( ... )
        {
            if ( owner )
            {
                busy_ = false;
                publishing_ = false;
                status_->setText("Operation failed with an unexpected exception; prepare again.");
                updateActions();
            }
        }
    }
    QLineEdit* line(const char* name)
    {
        auto* result = new QLineEdit(&dialog_);
        result->setObjectName(QString::fromLatin1(name));
        result->setMaxLength(4096);
        return result;
    }
    QSpinBox* spin(const char* name, const int minimum, const int maximum, const int value)
    {
        auto* result = new QSpinBox(&dialog_);
        result->setObjectName(QString::fromLatin1(name));
        result->setLocale(uiNumericLocale());
        result->setRange(minimum, maximum);
        result->setValue(value);
        onUiNumericFormatChanged(result, [result] { result->setLocale(uiNumericLocale()); });
        return result;
    }
    QPushButton* button(const QString& label, const char* name, QHBoxLayout* layout)
    {
        auto* result = new QPushButton(label, &dialog_);
        result->setObjectName(QString::fromLatin1(name));
        layout->addWidget(result);
        return result;
    }
    QTableWidget* table(const char* name, const QStringList& headers, QVBoxLayout* layout)
    {
        auto* result = new QTableWidget(0, static_cast<int>(headers.size()), &dialog_);
        result->setObjectName(QString::fromLatin1(name));
        result->setHorizontalHeaderLabels(headers);
        result->setSelectionBehavior(QAbstractItemView::SelectRows);
        result->setSelectionMode(QAbstractItemView::SingleSelection);
        result->horizontalHeader()->setStretchLastSection(true);
        result->setMinimumHeight(100);
        result->setMinimumWidth(0);
        layout->addWidget(result);
        return result;
    }
    QWidget* pathRow(QLineEdit* edit, const char* name, const bool save, const QString& filter)
    {
        auto* row = new QWidget(&dialog_);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(edit, 1);
        auto* browse = button("Browse…", name, layout);
        QObject::connect(browse, &QPushButton::clicked, &dialog_, [this, edit, save, filter]
        {
            invoke([&]
            {
                if ( busy_ || closed_ || checkingContext_ )
                {
                    return;
                }
                const QPointer<PackageAuthoringDialog> owner(&dialog_);
                const auto generation = generation_;
                const QString selected = save
                    ? QFileDialog::getSaveFileName(&dialog_, "Choose a new filename", context_.initialDirectory,
                        filter, nullptr, QFileDialog::DontConfirmOverwrite)
                    : QFileDialog::getOpenFileName(&dialog_, "Choose source file", context_.initialDirectory, filter);
                if ( owner && !closed_ && generation == generation_ && !selected.isEmpty() )
                {
                    edit->setText(selected);
                }
            });
        });
        return row;
    }
    void addRows(QTableWidget* target, QVBoxLayout* layout, const char* prefix,
        const int limit, const bool stock, const bool files = true)
    {
        auto* actions = new QHBoxLayout;
        auto* add = button("Add row", (std::string(prefix) + "Add").c_str(), actions);
        auto* remove = button("Remove", (std::string(prefix) + "Remove").c_str(), actions);
        auto* up = button("Up", (std::string(prefix) + "Up").c_str(), actions);
        auto* down = button("Down", (std::string(prefix) + "Down").c_str(), actions);
        layout->addLayout(actions);
        QObject::connect(add, &QPushButton::clicked, &dialog_, [this, target, stock, limit]
        {
            if ( target->rowCount() >= limit || closed_ || checkingContext_ )
            {
                return;
            }
            const int row = target->rowCount();
            target->insertRow(row);
            target->setItem(row, 0, new QTableWidgetItem);
            auto* second = new QTableWidgetItem;
            if ( stock )
            {
                second->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                second->setCheckState(Qt::Unchecked);
            }
            target->setItem(row, 1, second);
            target->setCurrentCell(row, 0);
            invalidate();
        });
        QObject::connect(remove, &QPushButton::clicked, &dialog_, [this, target]
        {
            if ( target->currentRow() >= 0 && !checkingContext_ )
            {
                target->removeRow(target->currentRow());
                invalidate();
            }
        });
        const auto move = [this, target](const int direction)
        {
            const int row = target->currentRow();
            const int destination = row + direction;
            if ( checkingContext_ || row < 0 || destination < 0 || destination >= target->rowCount() )
            {
                return;
            }
            const QSignalBlocker blocker(target);
            for ( int column = 0; column < target->columnCount(); ++column )
            {
                auto* first = target->takeItem(row, column);
                auto* second = target->takeItem(destination, column);
                target->setItem(row, column, second);
                target->setItem(destination, column, first);
            }
            target->setCurrentCell(destination, 0);
            invalidate();
        };
        QObject::connect(up, &QPushButton::clicked, &dialog_, [move] { move(-1); });
        QObject::connect(down, &QPushButton::clicked, &dialog_, [move] { move(1); });
        if ( files )
        {
            auto* chooserRow = new QHBoxLayout;
            auto* choose = button("Choose file for selected row…", (std::string(prefix) + "Choose").c_str(), chooserRow);
            layout->addLayout(chooserRow);
            QObject::connect(choose, &QPushButton::clicked, &dialog_, [this, target, stock]
            {
                invoke([&]
                {
                    if ( busy_ || closed_ || checkingContext_ || target->currentRow() < 0 )
                    {
                        return;
                    }
                    const auto generation = generation_;
                    const int row = target->currentRow();
                    const QPointer<PackageAuthoringDialog> owner(&dialog_);
                    const QString selected = QFileDialog::getOpenFileName(&dialog_, "Choose explicit source",
                        context_.initialDirectory, stock ? "Archives (*.gob *.goo)" : "All files (*)");
                    if ( owner && !closed_ && generation == generation_ && !selected.isEmpty() )
                    {
                        target->setItem(row, 0, new QTableWidgetItem(selected));
                    }
                });
            });
        }
    }
    void updateActions()
    {
        prepare_->setEnabled(!busy_ && !closed_);
        create_->setEnabled(!busy_ && !closed_ && prepared_ && prepared_->canBuild);
        export_->setEnabled(!busy_ && !closed_ && prepared_);
        cancel_->setEnabled(busy_ && !closed_);
    }
    void invalidate(const bool clear = true)
    {
        ++generation_;
        detail::cancelPreviewWorkerPublication(guard_);
        if ( clear )
        {
            prepared_.reset();
            if ( report_ )
            {
                report_->clear();
                contents_->setRowCount(0);
                issues_->setRowCount(0);
            }
        }
        if ( prepare_ )
        {
            updateActions();
        }
    }
    bool current()
    {
        const QPointer<PackageAuthoringDialog> owner(&dialog_);
        const auto generation = generation_;
        const auto callback = context_.isCurrent;
        checkingContext_ = true;
        const auto reset = qScopeGuard([&]
        {
            if ( owner )
            {
                checkingContext_ = false;
            }
        });
        bool valid = true;
        QString problem;
        try
        {
            valid = !callback || callback();
        }
        catch ( const std::exception& error )
        {
            valid = false;
            problem = QString::fromUtf8(error.what());
        }
        catch ( ... )
        {
            valid = false;
            problem = "The host context check failed with an unexpected exception.";
        }
        if ( !owner || closed_ || generation_ != generation )
        {
            return false;
        }
        if ( !valid )
        {
            invalidate();
            status_->setText(problem.isEmpty() ? "The source context changed; reopen this dialog." : problem);
        }
        return valid;
    }
    QString cell(QTableWidget* source, const int row, const int column) const
    {
        const auto* item = source->item(row, column);
        return item ? item->text() : QString{};
    }
    void prepare()
    {
        if ( busy_ || closed_ || checkingContext_ )
        {
            return;
        }
        invalidate();
        if ( !current() )
        {
            return;
        }
        Request request;
        request.package = mode_->currentIndex() == 1;
        request.extension = format_->currentIndex() == 0 ? ".gob" : ".goo";
        if ( !request.package )
        {
            if ( entries_->rowCount() < 1 || entries_->rowCount() > 4096 )
            {
                throw std::runtime_error("Add 1 to 4096 explicit source rows.");
            }
            for ( int row = 0; row < entries_->rowCount(); ++row )
            {
                request.entries.emplace_back(cell(entries_, row, 0), cell(entries_, row, 1));
            }
        }
        else
        {
            if ( archives_->rowCount() > kMaxArchives || extras_->rowCount() > kMaxExtras )
            {
                throw std::runtime_error("Too many archive or extra-reference rows.");
            }
            request.level = level_->text();
            auto& package = request.packageRequest;
            package.levelFileName = levelName_->text().toUtf8().toStdString();
            package.episodeTitle = episode_->text().toUtf8().toStdString();
            package.episodeType = episodeType_->value();
            package.archiveExtension = request.extension.toStdString();
            package.options = {static_cast<std::size_t>(nodes_->value()), static_cast<std::size_t>(edges_->value()),
                static_cast<std::size_t>(depth_->value()), static_cast<std::size_t>(bytes_->value()), allowMissing_->isChecked()};
            for ( int row = 0; row < archives_->rowCount(); ++row )
            {
                const auto* role = archives_->item(row, 1);
                request.archives.push_back({cell(archives_, row, 0), role && role->checkState() == Qt::Checked});
            }
            for ( int row = 0; row < extras_->rowCount(); ++row )
            {
                package.extraResources.push_back({cell(extras_, row, 0).toUtf8().toStdString(),
                    cell(extras_, row, 1).toUtf8().toStdString()});
            }
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        busy_ = true;
        publishing_ = false;
        updateActions();
        status_->setText("Capturing inputs and preparing a review. No output is published yet…");
        watcher_->setFuture(detail::runPreviewWorkerTask(prepareFiles, std::move(request), guard_));
    }
    void publish(const bool report)
    {
        if ( busy_ || closed_ || checkingContext_ || !prepared_ || (!report && !prepared_->canBuild) || !current() )
        {
            return;
        }
        const QString entered = (report ? reportPath_ : output_)->text();
        if ( entered.isEmpty() )
        {
            throw std::runtime_error("Choose a new output filename.");
        }
        if ( !report && QFileInfo(entered).suffix().toLower() != prepared_->extension.mid(1) )
        {
            throw std::runtime_error("Choose a new output filename matching the reviewed GOB/GOO format.");
        }
        const QString destination = QFileInfo(entered).absoluteFilePath();
        requireNew(*prepared_, destination);
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        busy_ = true;
        publishing_ = true;
        updateActions();
        status_->setText("Rechecking captured sources and staging reviewed bytes…");
        watcher_->setFuture(detail::runPreviewWorkerTask(stageFiles,
            std::shared_ptr<const Prepared>(prepared_), destination, report, guard_));
    }
    void populate(QTableWidget* target, const std::vector<QStringList>& rows)
    {
        target->setRowCount(static_cast<int>(rows.size()));
        for ( std::size_t row = 0; row < rows.size(); ++row )
        {
            for ( int column = 0; column < rows[row].size(); ++column )
            {
                auto* item = new QTableWidgetItem(rows[row][column]);
                item->setToolTip(rows[row][column]);
                target->setItem(static_cast<int>(row), column, item);
            }
        }
    }
    void finish()
    {
        busy_ = false;
        publishing_ = false;
        const QPointer<PackageAuthoringDialog> owner(&dialog_);
        invoke([&]
        {
            const auto result = watcher_->result();
            const auto cleanup = qScopeGuard([&]
            {
                result->staged.reset();
                result->prepared.reset();
            });
            if ( closed_ || !detail::previewWorkerMayPublish(guard_, generation_) )
            {
                updateActions();
                return;
            }
            if ( !current() )
            {
                if ( owner )
                {
                    updateActions();
                }
                return;
            }
            if ( !result->error.isEmpty() )
            {
                invalidate();
                status_->setText(result->error);
            }
            else if ( result->staged )
            {
                requireNew(*prepared_, result->destination);
                result->staged->publishNew(filePath(result->destination));
                result->staged.reset();
                status_->setText("Created " + result->destination);
                updateActions();
                const auto callback = context_.published;
                if ( callback )
                {
                    try
                    {
                        callback(result->destination);
                    }
                    catch ( const std::exception& error )
                    {
                        if ( owner )
                        {
                            status_->setText("Created " + result->destination + ". Notification failed: " + QString::fromUtf8(error.what()));
                        }
                    }
                    catch ( ... )
                    {
                        if ( owner )
                        {
                            status_->setText("Created " + result->destination + ". Notification failed with an unexpected exception.");
                        }
                    }
                }
            }
            else
            {
                prepared_ = result->prepared;
                populate(contents_, prepared_->contents);
                populate(issues_, prepared_->issues);
                report_->setPlainText(prepared_->report);
                status_->setText(prepared_->canBuild ? "Prepared. Review the full contents and issues, then create a new archive."
                    : "The plan is blocked. Review issues and adjust inputs; its report can be exported.");
            }
            if ( owner )
            {
                updateActions();
            }
        });
    }
    PackageAuthoringDialog& dialog_;
    Context context_;
    std::uint64_t generation_{};
    bool busy_{};
    bool publishing_{};
    bool closed_{};
    bool checkingContext_{};
    Guard guard_;
    std::shared_ptr<Prepared> prepared_;
    QFutureWatcher<std::shared_ptr<Result>>* watcher_{};
    QTabWidget* mode_{};
    QTableWidget *entries_{}, *archives_{}, *extras_{}, *contents_{}, *issues_{};
    QLineEdit *level_{}, *levelName_{}, *episode_{}, *output_{}, *reportPath_{};
    QSpinBox *episodeType_{}, *nodes_{}, *edges_{}, *depth_{}, *bytes_{};
    QCheckBox* allowMissing_{};
    QComboBox* format_{};
    QPlainTextEdit* report_{};
    QLabel* status_{};
    QPushButton *prepare_{}, *create_{}, *export_{}, *cancel_{};
};

PackageAuthoringDialog::PackageAuthoringDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
}
PackageAuthoringDialog::~PackageAuthoringDialog() = default;
void PackageAuthoringDialog::done(const int result)
{
    impl_->close();
    QDialog::done(result);
}
} // namespace sed::ui::resources
