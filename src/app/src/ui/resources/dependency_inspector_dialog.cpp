#include "ui/resources/dependency_inspector_dialog.hpp"

#include "sed/assets/archive_admission.hpp"
#include "sed/level/dependencies.hpp"
#include "ui/components/dialog_window_policy.hpp"
#include "ui/components/preview_worker_pool.hpp"

// Share the core asset publication protocol in the application adapter.
// The inspector itself stays Qt-free and never publishes files.
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sed::ui::resources
{
namespace
{
constexpr std::size_t kMaxLevelBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMaxArchiveBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxArchiveEntries = 100000;
constexpr int kMaxArchives = 16;
constexpr int kMaxExtraAssets = 1024;

QString escaped(const std::string_view value)
{
    constexpr char kHex[] = "0123456789ABCDEF";
    QString result;
    result.reserve(static_cast<qsizetype>(value.size()));
    for ( const unsigned char byte : value )
    {
        if ( byte == '\\' )
        {
            result += QStringLiteral("\\\\");
        }
        else if ( byte < 0x20 || byte > 0x7e )
        {
            result += QStringLiteral("\\x");
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

QString flag(const bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

QString number(const std::size_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}

QString resolution(const level::DependencyResolution value)
{
    switch ( value )
    {
    case level::DependencyResolution::Source: return QStringLiteral("source");
    case level::DependencyResolution::Resolved: return QStringLiteral("resolved");
    case level::DependencyResolution::Missing: return QStringLiteral("missing");
    case level::DependencyResolution::Ambiguous: return QStringLiteral("ambiguous");
    }
    return {};
}

QString scan(const level::DependencyScan value)
{
    switch ( value )
    {
    case level::DependencyScan::Pending: return QStringLiteral("pending");
    case level::DependencyScan::Scanned: return QStringLiteral("scanned");
    case level::DependencyScan::Leaf: return QStringLiteral("leaf");
    case level::DependencyScan::Unsupported: return QStringLiteral("unsupported");
    case level::DependencyScan::Incomplete: return QStringLiteral("incomplete");
    case level::DependencyScan::BudgetStopped: return QStringLiteral("budget stopped");
    }
    return {};
}

QStringList nodeColumns(const level::DependencyNode& node)
{
    QStringList row{number(node.id), escaped(node.path), number(node.depth), resolution(node.resolution),
        scan(node.inspection), escaped(node.sha256), flag(node.stock)};
    if ( node.provenance )
    {
        const auto& p = *node.provenance;
        row << number(p.archiveOrdinal) << number(p.entryOrdinal) << escaped(p.entryName)
            << number(p.offset) << number(p.bytes) << escaped(p.lookupTier);
    }
    else
    {
        for ( int i = 0; i < 6; ++i )
        {
            row << QStringLiteral("-");
        }
    }
    return row;
}

QStringList edgeColumns(const level::DependencyEdge& edge)
{
    return {number(edge.parent), number(edge.target), escaped(edge.relation), escaped(edge.authoredName),
        escaped(edge.folder), flag(edge.reused)};
}

QStringList issueColumns(const level::DependencyIssue& issue)
{
    return {number(issue.node), escaped(issue.code), escaped(issue.message)};
}

QStringList archiveColumns(const level::DependencyArchiveInfo& archive)
{
    return {number(archive.ordinal), escaped(archive.label), escaped(archive.sha256),
        number(archive.bytes), number(archive.entries), flag(archive.stock)};
}

const QStringList kNodeHeaders{QStringLiteral("ID"), QStringLiteral("Path"), QStringLiteral("Depth"),
    QStringLiteral("Resolution"), QStringLiteral("Scan"), QStringLiteral("Payload SHA-256"),
    QStringLiteral("Stock"), QStringLiteral("Archive ordinal"), QStringLiteral("Physical row"),
    QStringLiteral("Entry name"), QStringLiteral("Offset"), QStringLiteral("Bytes"), QStringLiteral("Lookup tier")};
const QStringList kEdgeHeaders{QStringLiteral("Parent ID"), QStringLiteral("Target ID"), QStringLiteral("Relation"),
    QStringLiteral("Authored name"), QStringLiteral("Resource folder"), QStringLiteral("Reused")};
const QStringList kIssueHeaders{QStringLiteral("Node ID"), QStringLiteral("Code"), QStringLiteral("Message")};
const QStringList kArchiveHeaders{QStringLiteral("Ordinal"), QStringLiteral("Label"), QStringLiteral("Raw SHA-256"),
    QStringLiteral("Bytes"), QStringLiteral("Physical rows"), QStringLiteral("Stock")};

struct ArchiveSource
{
    QString path;
    bool stock{};
};

struct Inputs
{
    QString source;
    std::vector<ArchiveSource> archives;
    std::vector<level::PackageExtraResource> extras;
    level::DependencyLimits limits;
};

void checkCancelled(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if ( cancelled->load(std::memory_order_acquire) )
    {
        throw std::runtime_error("Inspection cancelled.");
    }
}

std::vector<std::byte> capture(const QString& path, const std::size_t maximum,
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    checkCancelled(cancelled);
    if ( path.isEmpty() || path.contains(QChar::Null) || path.toUtf8().size() > 4096 )
    {
        throw std::runtime_error("Choose a nonempty file path of at most 4096 UTF-8 bytes.");
    }
    const QFileInfo before(path);
    if ( !before.isFile() )
    {
        throw std::runtime_error("The selected input is not a readable regular file.");
    }
    if ( before.size() < 0 || static_cast<qulonglong>(before.size()) > maximum )
    {
        throw std::runtime_error("The selected input exceeds the remaining capture-byte allowance.");
    }
    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error(file.errorString().toStdString());
    }
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(before.size()));
    while ( !file.atEnd() )
    {
        checkCancelled(cancelled);
        const QByteArray chunk = file.read(static_cast<qint64>(std::min<std::size_t>(1024 * 1024,
            maximum - bytes.size() + 1)));
        if ( file.error() != QFile::NoError || (chunk.isEmpty() && !file.atEnd()) )
        {
            throw std::runtime_error("The input could not be captured completely.");
        }
        if ( static_cast<std::size_t>(chunk.size()) > maximum - bytes.size() )
        {
            throw std::runtime_error("The selected input grew beyond the capture-byte allowance.");
        }
        const auto offset = bytes.size();
        bytes.resize(offset + static_cast<std::size_t>(chunk.size()));
        if ( !chunk.isEmpty() )
        {
            std::memcpy(bytes.data() + offset, chunk.constData(), static_cast<std::size_t>(chunk.size()));
        }
    }
    checkCancelled(cancelled);
    const QFileInfo after(path);
    if ( before.size() != after.size() || before.lastModified() != after.lastModified()
        || static_cast<qulonglong>(after.size()) != bytes.size() )
    {
        throw std::runtime_error("An input changed during capture. Retry inspection after saving finishes.");
    }
    return bytes;
}

std::string_view view(const std::vector<std::byte>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

QString summaryText(const level::DependencyInspection& report)
{
    const QString qualifier = report.scanComplete ? QStringLiteral("exact") : QStringLiteral("lower bound");
    QString text = QStringLiteral("Input SHA-256: %1\nScan complete: %2\nDetails complete: %3\n"
        "References resolved: %4\nRuntime closure verified: no\n")
        .arg(escaped(report.inputSha256), flag(report.scanComplete), flag(report.detailsComplete),
            report.allReferencesResolved ? flag(*report.allReferencesResolved) : QStringLiteral("unknown"));
    const auto count = [&](const char* label, const std::size_t value)
    {
        text += QStringLiteral("%1: %2 (%3)\n").arg(QString::fromLatin1(label), number(value), qualifier);
    };
    count("Nodes", report.counts.nodes);
    count("Edges", report.counts.edges);
    count("Issues", report.counts.issues);
    count("Missing", report.counts.missing);
    count("Ambiguous", report.counts.ambiguous);
    count("Unsupported", report.counts.unsupported);
    text += QStringLiteral("\nReturned detail rows: %1 nodes, %2 edges, %3 issues.\n")
        .arg(report.nodes.size()).arg(report.edges.size()).arg(report.issues.size());
    if ( !report.detailsComplete )
    {
        text += QStringLiteral("Some detail was omitted. Increase permitted detail limits and inspect again; these rows are not paged.\n");
    }
    const auto reasons = [&](const char* label, const std::vector<std::string>& values)
    {
        text += QLatin1Char('\n') + QString::fromLatin1(label) + QLatin1Char(':');
        if ( values.empty() )
        {
            text += QStringLiteral(" none\n");
        }
        else
        {
            for ( const auto& value : values )
            {
                text += QStringLiteral("\n  ") + escaped(value);
            }
        }
        text += QLatin1Char('\n');
    };
    reasons("Unverified scopes", report.unverifiedScopes);
    reasons("Scan stop reasons", report.scanReasons);
    reasons("Detail omission reasons", report.detailReasons);
    text += QStringLiteral("\nOperational limits:\n");
    for ( const auto& spec : level::dependencyLimitSpecs() )
    {
        text += escaped(spec.name) + QStringLiteral(": ") + number(report.limits.*spec.member) + QLatin1Char('\n');
    }
    const auto& u = report.usage;
    text += QStringLiteral("\nObserved usage:\nworkUnits: %1\nindexEntries: %2\nscanBytes: %3\nresourceBytes: %4\n"
        "stringBytes: %5\npeakPending: %6\ndetailRecords: %7\ndetailBytes: %8\n")
        .arg(u.workUnits).arg(u.indexEntries).arg(u.scanBytes).arg(u.resourceBytes)
        .arg(u.stringBytes).arg(u.peakPending).arg(u.detailRecords).arg(u.detailBytes);
    return text;
}

QString reportText(const Inputs& inputs, const level::DependencyInspection& report)
{
    QString text = QStringLiteral("RED dependency inspection — captured exported JKL and ordered archives\n\n"
        "This supported lexical-reference scan does not establish runtime closure or validate complete asset grammars.\n"
        "The open document, loose files and native project drafts are outside this inspection.\n"
        "Paths are not locked or revalidated after capture; this is not an atomic multi-file snapshot.\n"
        "Raw names use reversible ASCII byte escapes: backslash is doubled; other nonprintable/non-ASCII bytes use \\xHH.\n"
        "Tables below contain every detail row returned by this captured inspection, independent of GUI sorting.\n\n"
        "Source path: ") + escaped(inputs.source.toStdString()) + QLatin1Char('\n');
    for ( std::size_t index = 0; index < inputs.archives.size(); ++index )
    {
        text += QStringLiteral("Archive path %1: %2\n").arg(index).arg(escaped(inputs.archives[index].path.toStdString()));
    }
    text += QStringLiteral("\nAdditional referenced assets (logical name / resource folder):\n");
    for ( const auto& extra : inputs.extras )
    {
        text += escaped(extra.name) + QLatin1Char('\t') + escaped(extra.folder) + QLatin1Char('\n');
    }
    text += QLatin1Char('\n') + summaryText(report);
    const auto section = [&](const char* title, const QStringList& headers)
    {
        text += QStringLiteral("\n%1\n").arg(QString::fromLatin1(title)) + headers.join(QLatin1Char('\t')) + QLatin1Char('\n');
    };
    section("Captured archive evidence", kArchiveHeaders);
    for ( const auto& row : report.sourceArchives )
    {
        text += archiveColumns(row).join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    section("Nodes and physical provenance", kNodeHeaders);
    for ( const auto& row : report.nodes )
    {
        text += nodeColumns(row).join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    section("Reference edges", kEdgeHeaders);
    for ( const auto& row : report.edges )
    {
        text += edgeColumns(row).join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    section("Issues", kIssueHeaders);
    for ( const auto& row : report.issues )
    {
        text += issueColumns(row).join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    return text;
}

struct Result
{
    std::optional<level::DependencyInspection> inspection;
    QString summary;
    QString text;
    QString error;
};

Result inspectCaptured(const Inputs& inputs, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    try
    {
        const QFileInfo levelFile(inputs.source);
        const std::string levelName = levelFile.fileName().toStdString();
        if ( levelFile.suffix().compare(QStringLiteral("jkl"), Qt::CaseInsensitive) != 0
            || levelName.size() > 255 || levelName.find_first_of("/\\") != std::string::npos )
        {
            throw std::runtime_error("Choose an exported .jkl file with a plain basename of at most 255 UTF-8 bytes.");
        }
        const auto source = capture(inputs.source, kMaxLevelBytes, cancelled);
        std::size_t remainingBytes = kMaxArchiveBytes;
        std::size_t remainingEntries = kMaxArchiveEntries;
        std::vector<level::DependencyArchiveInput> archives;
        for ( const auto& input : inputs.archives )
        {
            const QFileInfo archiveFile(input.path);
            if ( archiveFile.suffix().compare(QStringLiteral("gob"), Qt::CaseInsensitive) != 0
                && archiveFile.suffix().compare(QStringLiteral("goo"), Qt::CaseInsensitive) != 0 )
            {
                throw std::runtime_error("Each archive must be an explicit .gob or .goo file.");
            }
            auto bytes = capture(input.path, remainingBytes, cancelled);
            const auto admission = assets::admitCapturedArchiveIndex(view(bytes), remainingEntries);
            if ( admission.status == assets::ArchiveIndexAdmissionStatus::Malformed )
            {
                throw std::runtime_error("Invalid or truncated archive index.");
            }
            if ( admission.status == assets::ArchiveIndexAdmissionStatus::EntryLimitExceeded )
            {
                throw std::runtime_error("Captured archive indexes exceed the 100,000 physical-row allowance.");
            }
            remainingBytes -= bytes.size();
            remainingEntries -= admission.entries;
            checkCancelled(cancelled);
            archives.push_back({assets::GobArchive::fromBytes(std::move(bytes)),
                archiveFile.fileName().toStdString(), input.stock});
        }
        checkCancelled(cancelled);
        auto report = level::inspectDependencies({view(source), levelName, archives, inputs.extras, inputs.limits});
        // The core has no cancellation callback. Cancellation discards the
        // completed result; it does not interrupt the in-flight core scan.
        checkCancelled(cancelled);
        Result result;
        result.summary = summaryText(report);
        result.text = reportText(inputs, report);
        result.inspection = std::move(report);
        return result;
    }
    catch ( const std::exception& error )
    {
        return {{}, {}, {}, escaped(error.what())};
    }
}

QTableWidget* table(QWidget* parent, const char* name, const QStringList& headers, const bool editable = false)
{
    auto* widget = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setHorizontalHeaderLabels(headers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    if ( !editable )
    {
        widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }
    widget->horizontalHeader()->setStretchLastSection(true);
    widget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    widget->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    return widget;
}

template<class Row, class Columns>
void fill(QTableWidget* widget, const std::vector<Row>& rows, Columns columns)
{
    widget->setSortingEnabled(false);
    widget->setRowCount(static_cast<int>(rows.size()));
    for ( std::size_t index = 0; index < rows.size(); ++index )
    {
        const auto values = columns(rows[index]);
        for ( qsizetype column = 0; column < values.size(); ++column )
        {
            widget->setItem(static_cast<int>(index), static_cast<int>(column), new QTableWidgetItem(values[column]));
        }
    }
    widget->setSortingEnabled(true);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
}

struct DependencyInspectorDialog::Impl
{
    DependencyInspectorDialog& owner;
    Context context;
    QLineEdit* source{};
    QLineEdit* output{};
    QTableWidget* archives{};
    QTableWidget* extras{};
    QTableWidget* nodes{};
    QTableWidget* edges{};
    QTableWidget* issues{};
    QTableWidget* archiveEvidence{};
    QPlainTextEdit* summary{};
    QLabel* status{};
    QPushButton* inspect{};
    QPushButton* cancel{};
    QPushButton* save{};
    std::vector<std::pair<level::DependencyLimitSpec, QSpinBox*>> limits;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::uint64_t generation{};
    bool working{};
    QString currentReport;

    explicit Impl(DependencyInspectorDialog& dialog, Context initial) : owner(dialog), context(std::move(initial))
    {
        owner.setObjectName(QStringLiteral("dependencyInspectorDialog"));
        owner.setWindowTitle(QStringLiteral("Dependencies — captured JKL and archives"));
        owner.resize(1050, 760);
        applyDialogWindowPolicy(owner);
        auto* layout = new QVBoxLayout(&owner);
        const auto label = [&](QVBoxLayout* container, const QString& text)
        {
            auto* widget = new QLabel(text, &owner);
            widget->setWordWrap(true);
            widget->setTextFormat(Qt::PlainText);
            container->addWidget(widget);
        };
        label(layout, QStringLiteral("Inspect a saved/exported JKL and explicitly ordered GOB/GOO files. "
            "This does not inspect unsaved/native drafts or discover loose files. Runtime closure remains unverified."));
        auto* tabs = new QTabWidget(&owner);
        layout->addWidget(tabs, 1);
        auto* inputPage = new QWidget(tabs);
        auto* inputsLayout = new QVBoxLayout(inputPage);
        source = new QLineEdit(inputPage);
        source->setObjectName(QStringLiteral("dependencySource"));
        auto* sourceLine = new QHBoxLayout;
        sourceLine->addWidget(new QLabel(QStringLiteral("Exported JKL:"), inputPage));
        sourceLine->addWidget(source, 1);
        auto* browseSource = new QPushButton(QStringLiteral("Browse…"), inputPage);
        sourceLine->addWidget(browseSource);
        inputsLayout->addLayout(sourceLine);
        label(inputsLayout, QStringLiteral("Archives in lookup order (maximum 16):"));
        archives = table(inputPage, "dependencyArchives", {QStringLiteral("File path"), QStringLiteral("Stock")}, true);
        archives->setColumnWidth(0, 700);
        inputsLayout->addWidget(archives, 1);
        const auto button = [&](QHBoxLayout* row, const char* name, const QString& text)
        {
            auto* value = new QPushButton(text, &owner);
            value->setObjectName(QString::fromLatin1(name));
            row->addWidget(value);
            return value;
        };
        auto* archiveButtons = new QHBoxLayout;
        auto* addArchive = button(archiveButtons, "dependencyAddArchive", QStringLiteral("Add archive row"));
        auto* browseArchive = button(archiveButtons, "dependencyBrowseArchive", QStringLiteral("Browse selected…"));
        auto* removeArchive = button(archiveButtons, "dependencyRemoveArchive", QStringLiteral("Remove"));
        auto* up = button(archiveButtons, "dependencyArchiveUp", QStringLiteral("Move up"));
        auto* down = button(archiveButtons, "dependencyArchiveDown", QStringLiteral("Move down"));
        archiveButtons->addStretch();
        inputsLayout->addLayout(archiveButtons);
        label(inputsLayout, QStringLiteral("Additional referenced assets: logical names and resource folders "
            "(for example wall.mat / mat). These are not filesystem roots."));
        extras = table(inputPage, "dependencyExtras", {QStringLiteral("Referenced asset name"), QStringLiteral("Resource folder")}, true);
        extras->setColumnWidth(0, 500);
        inputsLayout->addWidget(extras, 1);
        auto* extraButtons = new QHBoxLayout;
        auto* addExtra = button(extraButtons, "dependencyAddExtra", QStringLiteral("Add referenced asset"));
        auto* removeExtra = button(extraButtons, "dependencyRemoveExtra", QStringLiteral("Remove"));
        extraButtons->addStretch();
        inputsLayout->addLayout(extraButtons);
        label(inputsLayout, QStringLiteral("Capture bounds: 128 MiB JKL; 512 MiB and 100,000 physical archive rows in total. "
            "Inputs are read once; no atomic multi-file snapshot is claimed."));
        tabs->addTab(inputPage, QStringLiteral("Inputs"));

        auto* limitPage = new QWidget(tabs);
        auto* limitLayout = new QFormLayout(limitPage);
        auto* limitExplanation = new QLabel(QStringLiteral("Zero stops the corresponding work (minimum one node). "
            "Detail limits only truncate returned detail. These are operational bounds, not total memory or time guarantees."), limitPage);
        limitExplanation->setWordWrap(true);
        limitLayout->addRow(limitExplanation);
        const level::DependencyLimits defaults;
        for ( const auto& spec : level::dependencyLimitSpecs() )
        {
            auto* spin = new QSpinBox(limitPage);
            const QString name = QString::fromLatin1(spec.name.data(), static_cast<qsizetype>(spec.name.size()));
            spin->setObjectName(QStringLiteral("dependencyLimit_") + name);
            spin->setRange(static_cast<int>(spec.minimum), static_cast<int>(spec.maximum));
            spin->setValue(static_cast<int>(defaults.*spec.member));
            spin->setKeyboardTracking(false);
            limitLayout->addRow(name, spin);
            limits.emplace_back(spec, spin);
            QObject::connect(spin, &QSpinBox::valueChanged, &owner, [this]
            {
                invalidate();
            });
        }
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setWidget(limitPage);
        tabs->addTab(scroll, QStringLiteral("Limits"));
        summary = new QPlainTextEdit(tabs);
        summary->setObjectName(QStringLiteral("dependencySummary"));
        summary->setReadOnly(true);
        tabs->addTab(summary, QStringLiteral("Summary and coverage"));
        nodes = table(tabs, "dependencyNodes", kNodeHeaders);
        edges = table(tabs, "dependencyEdges", kEdgeHeaders);
        issues = table(tabs, "dependencyIssues", kIssueHeaders);
        archiveEvidence = table(tabs, "dependencyArchiveEvidence", kArchiveHeaders);
        tabs->addTab(nodes, QStringLiteral("Nodes and provenance"));
        tabs->addTab(edges, QStringLiteral("Reference edges"));
        tabs->addTab(issues, QStringLiteral("Issues"));
        tabs->addTab(archiveEvidence, QStringLiteral("Archive hashes"));
        label(layout, QStringLiteral("Report names are byte-preserving: \\xHH encodes non-ASCII/control bytes; \\\\ encodes a backslash. "
            "Archive ordinals and physical rows are zero-based."));
        status = new QLabel(QStringLiteral("Choose explicit captured inputs, then Inspect."), &owner);
        status->setObjectName(QStringLiteral("dependencyStatus"));
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        layout->addWidget(status);
        auto* actions = new QHBoxLayout;
        inspect = button(actions, "dependencyInspect", QStringLiteral("Inspect dependencies"));
        cancel = button(actions, "dependencyCancel", QStringLiteral("Cancel inspection"));
        cancel->setEnabled(false);
        actions->addStretch();
        layout->addLayout(actions);
        auto* exportRow = new QHBoxLayout;
        output = new QLineEdit(&owner);
        output->setObjectName(QStringLiteral("dependencyOutput"));
        output->setPlaceholderText(QStringLiteral("New report .txt path (existing files are preserved)"));
        exportRow->addWidget(output, 1);
        auto* browseOutput = button(exportRow, "dependencyBrowseOutput", QStringLiteral("Choose new report…"));
        save = button(exportRow, "dependencySave", QStringLiteral("Export captured report"));
        save->setEnabled(false);
        layout->addLayout(exportRow);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &owner);
        layout->addWidget(close);

        QObject::connect(source, &QLineEdit::textChanged, &owner, [this]
        {
            invalidate();
        });
        QObject::connect(archives, &QTableWidget::itemChanged, &owner, [this]
        {
            invalidate();
        });
        QObject::connect(extras, &QTableWidget::itemChanged, &owner, [this]
        {
            invalidate();
        });
        QObject::connect(addArchive, &QPushButton::clicked, &owner, [this]
        {
            addRow(archives, kMaxArchives, true);
        });
        QObject::connect(addExtra, &QPushButton::clicked, &owner, [this]
        {
            addRow(extras, kMaxExtraAssets, false);
        });
        QObject::connect(removeArchive, &QPushButton::clicked, &owner, [this]
        {
            removeRow(archives);
        });
        QObject::connect(removeExtra, &QPushButton::clicked, &owner, [this]
        {
            removeRow(extras);
        });
        QObject::connect(up, &QPushButton::clicked, &owner, [this]
        {
            moveArchive(-1);
        });
        QObject::connect(down, &QPushButton::clicked, &owner, [this]
        {
            moveArchive(1);
        });
        QObject::connect(browseSource, &QPushButton::clicked, &owner, [this]
        {
            const QString path = QFileDialog::getOpenFileName(&owner, QStringLiteral("Choose exported JKL"),
                source->text().isEmpty() ? context.initialDirectory : source->text(), QStringLiteral("JKL levels (*.jkl)"));
            if ( !path.isEmpty() )
            {
                source->setText(path);
            }
        });
        QObject::connect(browseArchive, &QPushButton::clicked, &owner, [this]
        {
            const int row = archives->currentRow();
            if ( row < 0 )
            {
                status->setText(QStringLiteral("Add or select an archive row first."));
                return;
            }
            const QString path = QFileDialog::getOpenFileName(&owner, QStringLiteral("Choose resource archive"),
                archives->item(row, 0)->text().isEmpty() ? context.initialDirectory : archives->item(row, 0)->text(),
                QStringLiteral("Sith archives (*.gob *.goo)"));
            if ( !path.isEmpty() )
            {
                archives->item(row, 0)->setText(path);
            }
        });
        QObject::connect(browseOutput, &QPushButton::clicked, &owner, [this]
        {
            const QString path = QFileDialog::getSaveFileName(&owner, QStringLiteral("Choose a new report path"),
                output->text().isEmpty() ? context.initialDirectory : output->text(), QStringLiteral("Text reports (*.txt)"),
                nullptr, QFileDialog::DontConfirmOverwrite);
            if ( !path.isEmpty() )
            {
                output->setText(path);
            }
        });
        QObject::connect(inspect, &QPushButton::clicked, &owner, [this]
        {
            guardOperation([this]
            {
                start();
            });
        });
        QObject::connect(cancel, &QPushButton::clicked, &owner, [this]
        {
            invalidate();
            status->setText(QStringLiteral("Cancellation requested. Capture stops between chunks; an active core scan finishes and its result is discarded."));
        });
        QObject::connect(save, &QPushButton::clicked, &owner, [this]
        {
            guardOperation([this]
            {
                publishReport();
            });
        });
        QObject::connect(close, &QDialogButtonBox::rejected, &owner, &QDialog::reject);
        QObject::connect(&owner, &QDialog::finished, &owner, [this]
        {
            invalidate();
        });
    }

    ~Impl()
    {
        if ( cancelled )
        {
            cancelled->store(true, std::memory_order_release);
        }
    }

    bool current() const
    {
        return !context.isCurrent || context.isCurrent();
    }

    void operationFailed(const QString& error)
    {
        working = false;
        inspect->setEnabled(true);
        cancel->setEnabled(false);
        invalidate();
        status->setText(QStringLiteral("Dependency operation failed: %1. Retry inspection or reopen Dependencies after restoring the project source context.")
            .arg(error));
    }

    template<class Operation>
    void guardOperation(Operation&& operation)
    {
        try
        {
            std::forward<Operation>(operation)();
        }
        catch ( const std::exception& error )
        {
            operationFailed(escaped(error.what()));
        }
        catch ( ... )
        {
            operationFailed(QStringLiteral("An unexpected non-standard exception prevented completion"));
        }
    }

    void invalidate()
    {
        ++generation;
        if ( cancelled )
        {
            cancelled->store(true, std::memory_order_release);
        }
        currentReport.clear();
        save->setEnabled(false);
        summary->clear();
        for ( auto* widget : {nodes, edges, issues, archiveEvidence} )
        {
            widget->setRowCount(0);
        }
        status->setText(working ? QStringLiteral("Inputs changed. Waiting for the previous worker to finish; its result will be discarded.")
            : QStringLiteral("Inputs changed. Inspect to obtain a new captured report."));
    }

    void addRow(QTableWidget* widget, const int maximum, const bool stock)
    {
        if ( widget->rowCount() >= maximum )
        {
            status->setText(QStringLiteral("This input list is limited to %1 rows.").arg(maximum));
            return;
        }
        const QSignalBlocker block(widget);
        const int row = widget->rowCount();
        widget->insertRow(row);
        widget->setItem(row, 0, new QTableWidgetItem);
        auto* second = new QTableWidgetItem;
        if ( stock )
        {
            second->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            second->setCheckState(Qt::Unchecked);
        }
        widget->setItem(row, 1, second);
        widget->setCurrentCell(row, 0);
        invalidate();
    }

    void removeRow(QTableWidget* widget)
    {
        if ( widget->currentRow() < 0 )
        {
            return;
        }
        widget->removeRow(widget->currentRow());
        invalidate();
    }

    void moveArchive(const int direction)
    {
        const int row = archives->currentRow();
        const int target = row + direction;
        if ( row < 0 || target < 0 || target >= archives->rowCount() )
        {
            return;
        }
        const QSignalBlocker block(archives);
        for ( int column = 0; column < archives->columnCount(); ++column )
        {
            auto* first = archives->takeItem(row, column);
            auto* second = archives->takeItem(target, column);
            archives->setItem(row, column, second);
            archives->setItem(target, column, first);
        }
        archives->setCurrentCell(target, 0);
        invalidate();
    }

    Inputs inputs() const
    {
        Inputs result;
        result.source = source->text();
        for ( int row = 0; row < archives->rowCount(); ++row )
        {
            result.archives.push_back({archives->item(row, 0)->text(), archives->item(row, 1)->checkState() == Qt::Checked});
        }
        for ( int row = 0; row < extras->rowCount(); ++row )
        {
            const auto name = extras->item(row, 0)->text().toUtf8();
            const auto folder = extras->item(row, 1)->text().toUtf8();
            if ( name.isEmpty() || name.size() > 1024 || folder.size() > 128
                || name.contains('\0') || folder.contains('\0') )
            {
                throw std::runtime_error("Each extra asset needs a logical name of 1–1024 UTF-8 bytes and a folder of 0–128 bytes, without NUL.");
            }
            result.extras.push_back({name.toStdString(), folder.toStdString()});
        }
        for ( const auto& [spec, spin] : limits )
        {
            result.limits.*spec.member = static_cast<std::size_t>(spin->value());
        }
        return result;
    }

    void start()
    {
        if ( working )
        {
            return;
        }
        invalidate();
        if ( !current() )
        {
            status->setText(QStringLiteral("The project source context changed. Reopen Dependencies from the current document."));
            return;
        }
        Inputs capturedInputs;
        try
        {
            capturedInputs = inputs();
        }
        catch ( const std::exception& error )
        {
            status->setText(QString::fromUtf8(error.what()));
            return;
        }
        working = true;
        inspect->setEnabled(false);
        cancel->setEnabled(true);
        cancelled = std::make_shared<std::atomic_bool>(false);
        const auto token = cancelled;
        const auto requestGeneration = generation;
        auto ownedWatcher = std::make_unique<QFutureWatcher<Result>>(&owner);
        auto* watcher = ownedWatcher.get();
        status->setText(QStringLiteral("Capturing inputs and inspecting supported references…"));
        QObject::connect(watcher, &QFutureWatcher<Result>::finished, &owner, [this, watcher, token, requestGeneration]
        {
            watcher->deleteLater();
            working = false;
            inspect->setEnabled(true);
            cancel->setEnabled(false);
            guardOperation([&]
            {
                const Result result = watcher->result();
                if ( !current() )
                {
                    invalidate();
                    status->setText(QStringLiteral("The project source context changed; the completed result was discarded."));
                    return;
                }
                if ( requestGeneration != generation || token->load(std::memory_order_acquire) )
                {
                    status->setText(QStringLiteral("Cancelled or changed inputs: completed result discarded. Inspect again to capture current inputs."));
                    return;
                }
                if ( !result.inspection )
                {
                    status->setText(QStringLiteral("Inspection failed: ") + result.error);
                    return;
                }
                const auto& report = *result.inspection;
                summary->setPlainText(result.summary);
                fill(nodes, report.nodes, nodeColumns);
                fill(edges, report.edges, edgeColumns);
                fill(issues, report.issues, issueColumns);
                fill(archiveEvidence, report.sourceArchives, archiveColumns);
                currentReport = result.text;
                save->setEnabled(true);
                status->setText(QStringLiteral("Captured report ready. Scan: %1; details: %2; references resolved: %3. Runtime closure unverified.")
                    .arg(report.scanComplete ? QStringLiteral("complete") : QStringLiteral("partial — counts are lower bounds"),
                        report.detailsComplete ? QStringLiteral("complete") : QStringLiteral("partial — omitted rows"),
                        report.allReferencesResolved ? flag(*report.allReferencesResolved) : QStringLiteral("unknown")));
            });
        });
        watcher->setFuture(detail::runPreviewWorkerTask([capturedInputs = std::move(capturedInputs), token]
        {
            return inspectCaptured(capturedInputs, token);
        }));
        ownedWatcher.release();
    }

    void publishReport()
    {
        if ( currentReport.isEmpty() || working )
        {
            return;
        }
        if ( !current() )
        {
            invalidate();
            status->setText(QStringLiteral("The project source context changed. Reinspect before exporting."));
            return;
        }
        const QString path = output->text();
        if ( path.isEmpty() || path.contains(QChar::Null) )
        {
            status->setText(QStringLiteral("Choose a new report file path."));
            return;
        }
        if ( QFileInfo::exists(path) )
        {
            status->setText(QStringLiteral("The report destination already exists. Choose a new filename; existing files are preserved."));
            return;
        }
        try
        {
            const auto bytes = currentReport.toUtf8();
            const auto destination = nativePath(path);
            assets::detail::StagedAssetFile stage(destination);
            stage.writeAndClose({reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size())});
            if ( !current() )
            {
                throw std::runtime_error("The project source context changed before publication. Reinspect before exporting.");
            }
            stage.publishNew(destination);
            status->setText(QStringLiteral("Captured report exported to %1. Inputs were not modified.").arg(path));
        }
        catch ( const std::exception& error )
        {
            invalidate();
            status->setText(QStringLiteral("Could not create the report: %1. Choose a new filename in a writable local folder supporting atomic creation.")
                .arg(QString::fromUtf8(error.what())));
        }
    }
};

DependencyInspectorDialog::DependencyInspectorDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}

DependencyInspectorDialog::~DependencyInspectorDialog() = default;
} // namespace sed::ui::resources
