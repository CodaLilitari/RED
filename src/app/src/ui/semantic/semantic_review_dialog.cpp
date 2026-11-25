#include "ui/semantic/semantic_review_dialog.hpp"
#include "ui/semantic/semantic_review_dialog_status.hpp"

#include "ui/components/dialog_window_policy.hpp"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace sed::ui::semantic_review
{
namespace
{
namespace preview = semantic_preview;
constexpr std::size_t kPageSize = 32;

QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString valueType(const preview::Descriptor& descriptor)
{
    if ( !descriptor.value )
    {
        return QStringLiteral("absent");
    }
    return std::visit([](const auto& value) -> QString
    {
        using T = std::decay_t<decltype(value)>;
        if constexpr ( std::is_same_v<T, bool> )
        {
            return QStringLiteral("bool");
        }
        else if constexpr ( std::is_same_v<T, std::int32_t> )
        {
            return QStringLiteral("int32");
        }
        else if constexpr ( std::is_same_v<T, std::uint32_t> )
        {
            return QStringLiteral("uint32");
        }
        else if constexpr ( std::is_same_v<T, preview::EnumValue> )
        {
            return QStringLiteral("enum");
        }
        else if constexpr ( std::is_same_v<T, preview::Float32Value> )
        {
            return QStringLiteral("float32");
        }
        else if constexpr ( std::is_same_v<T, preview::Float64Value> )
        {
            return QStringLiteral("float64");
        }
        else if constexpr ( std::is_same_v<T, preview::SizeValue> )
        {
            return QStringLiteral("size");
        }
        else
        {
            return QStringLiteral("string");
        }
    }, *descriptor.value);
}

QString descriptorText(const preview::Descriptor& descriptor)
{
    if ( !descriptor.value )
    {
        return QStringLiteral("absent");
    }
    const QString body = std::visit([](const auto& value) -> QString
    {
        using T = std::decay_t<decltype(value)>;
        if constexpr ( std::is_same_v<T, bool> )
        {
            return value ? QStringLiteral("true") : QStringLiteral("false");
        }
        else if constexpr ( std::is_same_v<T, std::int32_t> )
        {
            return QString::number(static_cast<qlonglong>(value));
        }
        else if constexpr ( std::is_same_v<T, std::uint32_t> )
        {
            return QString::number(static_cast<qulonglong>(value));
        }
        else if constexpr ( std::is_same_v<T, preview::EnumValue> )
        {
            return QString::number(static_cast<qlonglong>(value.value));
        }
        else if constexpr ( std::is_same_v<T, preview::SizeValue> )
        {
            return QString::number(static_cast<qulonglong>(value.value));
        }
        else if constexpr ( std::is_same_v<T, preview::StringValue> )
        {
            if ( !value.encodedValue )
            {
                return QStringLiteral("omitted (%1 bytes)").arg(static_cast<qulonglong>(value.byteCount));
            }
            QString encoded;
            if ( value.encoding == preview::StringEncoding::Hex )
            {
                encoded = QStringLiteral("hex: ") + text(*value.encodedValue);
            }
            else
            {
                // Quote control characters visibly; the full admitted string
                // remains in the selectable cell, tooltip and detail pane.
                const QByteArray json = QJsonDocument(QJsonArray{text(*value.encodedValue)})
                    .toJson(QJsonDocument::Compact);
                encoded = QString::fromUtf8(json.mid(1, json.size() - 2));
            }
            return QStringLiteral("%1 (%2 bytes)").arg(encoded,
                QString::number(static_cast<qulonglong>(value.byteCount)));
        }
        else
        {
            const auto classification = preview::classify(value);
            QString number = text(preview::name(classification));
            if ( classification == preview::FloatClassification::NegativeZero )
            {
                number = QStringLiteral("-0");
            }
            else if ( classification == preview::FloatClassification::Finite )
            {
                if constexpr ( std::is_same_v<T, preview::Float32Value> )
                {
                    number = QString::number(std::bit_cast<float>(value.bits), 'g',
                        std::numeric_limits<float>::max_digits10);
                }
                else
                {
                    number = QString::number(std::bit_cast<double>(value.bits), 'g',
                        std::numeric_limits<double>::max_digits10);
                }
            }
            constexpr int width = std::is_same_v<T, preview::Float32Value> ? 8 : 16;
            const QString bits = QString::number(static_cast<qulonglong>(value.bits), 16)
                .rightJustified(width, QLatin1Char('0'));
            return QStringLiteral("%1 [%2; bits %3]").arg(number,
                text(preview::name(classification)), bits);
        }
    }, *descriptor.value);
    return valueType(descriptor) + QLatin1Char(' ') + body;
}

std::size_t omittedValues(const preview::Comparison& comparison)
{
    std::size_t count{};
    for ( const auto& change : comparison.changes )
    {
        for ( const auto* descriptor : {&change.before, &change.after} )
        {
            if ( descriptor->value )
            {
                const auto* string = std::get_if<preview::StringValue>(&*descriptor->value);
                count += string && !string->encodedValue ? 1u : 0u;
            }
        }
    }
    return count;
}

QString summaryText(const preview::Comparison& comparison)
{
    QString result;
    const auto emitted = comparison.changes.size();
    if ( comparison.totalChanges )
    {
        const auto omitted = *comparison.totalChanges >= emitted ? *comparison.totalChanges - emitted : 0;
        result = QStringLiteral("Exact total: %1 change records. Emitted details: %2; omitted records: %3.")
            .arg(static_cast<qulonglong>(*comparison.totalChanges))
            .arg(static_cast<qulonglong>(emitted))
            .arg(static_cast<qulonglong>(omitted));
    }
    else
    {
        const auto omitted = comparison.observedChanges >= emitted ? comparison.observedChanges - emitted : 0;
        result = QStringLiteral("Observed lower bound: at least %1 change records; exact total unknown. "
            "Emitted details: %2; at least %3 observed records omitted; further changes may be unscanned.")
            .arg(static_cast<qulonglong>(comparison.observedChanges))
            .arg(static_cast<qulonglong>(emitted))
            .arg(static_cast<qulonglong>(omitted));
    }
    const QString changed = !comparison.changed.has_value() ? QStringLiteral("unknown")
        : *comparison.changed ? QStringLiteral("yes") : QStringLiteral("no (covered fields only)");
    result += QStringLiteral("\nChanged: %1. Comparison %2; details %3. Omitted values in emitted rows: %4.")
        .arg(changed, comparison.comparisonComplete ? QStringLiteral("complete") : QStringLiteral("incomplete"),
            comparison.detailsComplete ? QStringLiteral("complete") : QStringLiteral("incomplete"))
        .arg(static_cast<qulonglong>(omittedValues(comparison)));
    QStringList reasons;
    for ( const auto reason : comparison.reasons )
    {
        reasons.push_back(text(preview::name(reason)));
    }
    result += QStringLiteral("\nReasons: %1. Work: %2 slots; %3 compared string bytes; %4 inline bytes.")
        .arg(reasons.isEmpty() ? QStringLiteral("none") : reasons.join(QStringLiteral(", ")))
        .arg(static_cast<qulonglong>(comparison.work.visitedSlots))
        .arg(static_cast<qulonglong>(comparison.work.comparedStringBytes))
        .arg(static_cast<qulonglong>(comparison.work.inlineStringBytes));
    return result;
}

QLabel* plainLabel(const QString& value, QWidget* parent)
{
    auto* label = new QLabel(value, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}
} // namespace

struct SemanticReviewDialog::Impl
{
    struct View
    {
        QLabel* description{};
        QLabel* summary{};
        QTableWidget* table{};
        QPlainTextEdit* details{};
        QLabel* pageLabel{};
        QPushButton* previous{};
        QPushButton* next{};
        std::size_t page{};
        std::vector<std::size_t> filtered;
    };

    enum class JobKind { Load, Export };
    struct Outcome
    {
        Result loaded;
        ExportResult exported;
    };

    SemanticReviewDialog& owner;
    Loader loader;
    QComboBox* mode{};
    QStackedWidget* inputs{};
    std::array<QLineEdit*, 3> snapshotPaths{};
    QLineEdit* receiptPath{};
    QLineEdit* exportPath{};
    QLineEdit* pathFilter{};
    QComboBox* typeFilter{};
    QPushButton* run{};
    QPushButton* cancel{};
    QPushButton* exportButton{};
    QPushButton* exportBrowse{};
    QLabel* provenance{};
    QLabel* status{};
    QPlainTextEdit* identities{};
    QPlainTextEdit* scope{};
    std::array<View, 2> views;
    std::shared_ptr<const Review> review;
    QFutureWatcher<Outcome>* watcher{};
    sed::StopSource cancellation;
    std::uint64_t generation{};
    std::uint64_t jobGeneration{};
    JobKind jobKind{JobKind::Load};
    bool busy{};
    QString exportingPath;

    Impl(SemanticReviewDialog& dialog, Loader load) : owner(dialog), loader(std::move(load))
    {
        if ( !loader )
        {
            throw std::invalid_argument("A semantic review loader is required.");
        }
        buildWidgets();
        watcher = new QFutureWatcher<Outcome>(&owner);
        QObject::connect(watcher, &QFutureWatcher<Outcome>::finished, &owner, [this] { finish(); });
    }

    ~Impl()
    {
        cancellation.request_stop();
        ++generation;
        // The worker owns only its request/service/stop token or immutable
        // result. It never captures Impl, the dialog, or any live document.
    }

    QLineEdit* addFileRow(QGridLayout* grid, int row, const QString& label,
        const char* objectName, const char* browseName, const QString& filter)
    {
        auto* edit = new QLineEdit(&owner);
        edit->setObjectName(QString::fromLatin1(objectName));
        edit->setMaxLength(static_cast<int>(kMaxPathCharacters));
        auto* caption = plainLabel(label, &owner);
        caption->setBuddy(edit);
        auto* browse = new QPushButton(owner.tr("Browse…"), &owner);
        browse->setObjectName(QString::fromLatin1(browseName));
        grid->addWidget(caption, row, 0);
        grid->addWidget(edit, row, 1);
        grid->addWidget(browse, row, 2);
        QObject::connect(browse, &QPushButton::clicked, &owner, [this, edit, filter]
        {
            const QString selected = QFileDialog::getOpenFileName(&owner,
                owner.tr("Choose a saved review input"), edit->text(), filter);
            if ( !selected.isEmpty() )
            {
                edit->setText(selected);
            }
        });
        QObject::connect(edit, &QLineEdit::textChanged, &owner, [this] { invalidate(); });
        return edit;
    }

    void buildView(QTabWidget* tabs, std::size_t index, const QString& title, const char* key)
    {
        auto* page = new QWidget(tabs);
        auto* layout = new QVBoxLayout(page);
        View& view = views[index];
        const QString prefix = QStringLiteral("semantic.") + QString::fromLatin1(key);
        view.description = plainLabel({}, page);
        view.summary = plainLabel(owner.tr("No review loaded."), page);
        view.summary->setObjectName(prefix + QStringLiteral(".summary"));
        layout->addWidget(view.description);
        layout->addWidget(view.summary);
        view.table = new QTableWidget(0, 4, page);
        view.table->setObjectName(prefix + QStringLiteral(".table"));
        view.table->setHorizontalHeaderLabels({owner.tr("Path (snapshot slots)"), owner.tr("Kind"),
            owner.tr("Before (stored type/value)"), owner.tr("After (stored type/value)")});
        view.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view.table->setSelectionBehavior(QAbstractItemView::SelectRows);
        view.table->setSelectionMode(QAbstractItemView::SingleSelection);
        view.table->setSortingEnabled(false);
        view.table->setWordWrap(false);
        view.table->setAlternatingRowColors(true);
        view.table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view.table->horizontalHeader()->setStretchLastSection(true);
        view.table->setColumnWidth(0, 290);
        view.table->setColumnWidth(1, 100);
        view.table->setColumnWidth(2, 290);
        layout->addWidget(view.table, 1);
        auto* paging = new QHBoxLayout;
        view.previous = new QPushButton(owner.tr("Previous"), page);
        view.next = new QPushButton(owner.tr("Next"), page);
        view.previous->setObjectName(prefix + QStringLiteral(".previous"));
        view.next->setObjectName(prefix + QStringLiteral(".next"));
        view.pageLabel = plainLabel({}, page);
        view.pageLabel->setObjectName(prefix + QStringLiteral(".page"));
        paging->addWidget(view.previous);
        paging->addWidget(view.pageLabel, 1);
        paging->addWidget(view.next);
        layout->addLayout(paging);
        view.details = new QPlainTextEdit(page);
        view.details->setObjectName(prefix + QStringLiteral(".details"));
        view.details->setReadOnly(true);
        view.details->setMaximumHeight(130);
        view.details->setPlaceholderText(owner.tr("Select a row to inspect its complete admitted values and exact float bits."));
        layout->addWidget(view.details);
        QObject::connect(view.previous, &QPushButton::clicked, &owner, [this, index]
        {
            if ( views[index].page != 0 )
            {
                --views[index].page;
                renderPage(index);
            }
        });
        QObject::connect(view.next, &QPushButton::clicked, &owner, [this, index]
        {
            if ( (views[index].page + 1) * kPageSize < views[index].filtered.size() )
            {
                ++views[index].page;
                renderPage(index);
            }
        });
        QObject::connect(view.table, &QTableWidget::itemSelectionChanged, &owner, [this, index]
        {
            showDetails(index);
        });
        tabs->addTab(page, title);
    }

    void buildWidgets()
    {
        owner.setWindowTitle(owner.tr("Semantic change review"));
        owner.resize(1120, 860);
        owner.setMinimumSize(720, 600);
        applyDialogWindowPolicy(owner);
        auto* root = new QVBoxLayout(&owner);
        mode = new QComboBox(&owner);
        mode->setObjectName(QStringLiteral("semantic.mode"));
        mode->addItem(owner.tr("Compare saved native snapshots (.sed)"));
        mode->addItem(owner.tr("Inspect an existing plan/apply receipt (.json)"));
        root->addWidget(mode);
        inputs = new QStackedWidget(&owner);
        auto* snapshots = new QWidget(inputs);
        auto* snapshotGrid = new QGridLayout(snapshots);
        const QString nativeFilter = owner.tr("Native RED projects (*.sed)");
        snapshotPaths[0] = addFileRow(snapshotGrid, 0, owner.tr("&Baseline (saved)"),
            "semantic.baseline", "semantic.browseBaseline", nativeFilter);
        snapshotPaths[1] = addFileRow(snapshotGrid, 1, owner.tr("&Candidate (saved)"),
            "semantic.candidate", "semantic.browseCandidate", nativeFilter);
        snapshotPaths[2] = addFileRow(snapshotGrid, 2, owner.tr("&Output (saved)"),
            "semantic.output", "semantic.browseOutput", nativeFilter);
        inputs->addWidget(snapshots);
        auto* receipt = new QWidget(inputs);
        auto* receiptGrid = new QGridLayout(receipt);
        receiptPath = addFileRow(receiptGrid, 0, owner.tr("&Receipt"),
            "semantic.receipt", "semantic.browseReceipt", owner.tr("JSON receipts (*.json);;All files (*)"));
        inputs->addWidget(receipt);
        root->addWidget(inputs);
        auto* actions = new QHBoxLayout;
        run = new QPushButton(owner.tr("Compare saved snapshots"), &owner);
        run->setObjectName(QStringLiteral("semantic.run"));
        cancel = new QPushButton(owner.tr("Cancel work"), &owner);
        cancel->setObjectName(QStringLiteral("semantic.cancel"));
        actions->addWidget(run);
        actions->addWidget(cancel);
        actions->addStretch();
        root->addLayout(actions);
        provenance = plainLabel(owner.tr("Choose explicit saved inputs or a recorded receipt. "
            "This viewer has no access to the live document."), &owner);
        provenance->setObjectName(QStringLiteral("semantic.provenance"));
        root->addWidget(provenance);
        auto* filters = new QHBoxLayout;
        pathFilter = new QLineEdit(&owner);
        pathFilter->setObjectName(QStringLiteral("semantic.pathFilter"));
        pathFilter->setMaxLength(1024);
        pathFilter->setPlaceholderText(owner.tr("Filter emitted paths (case-sensitive substring)"));
        typeFilter = new QComboBox(&owner);
        typeFilter->setObjectName(QStringLiteral("semantic.typeFilter"));
        typeFilter->addItem(owner.tr("All stored types"), QString{});
        for ( const char* type : {"absent", "bool", "int32", "uint32", "enum", "float32", "float64", "size", "string"} )
        {
            typeFilter->addItem(QString::fromLatin1(type), QString::fromLatin1(type));
        }
        filters->addWidget(pathFilter, 1);
        filters->addWidget(typeFilter);
        root->addLayout(filters);
        auto* tabs = new QTabWidget(&owner);
        tabs->setObjectName(QStringLiteral("semantic.views"));
        buildView(tabs, 0, owner.tr("Authored"), "authored");
        buildView(tabs, 1, owner.tr("Persisted"), "persisted");
        auto* metadata = new QWidget(tabs);
        auto* metadataLayout = new QVBoxLayout(metadata);
        identities = new QPlainTextEdit(metadata);
        identities->setObjectName(QStringLiteral("semantic.identities"));
        identities->setReadOnly(true);
        scope = new QPlainTextEdit(metadata);
        scope->setObjectName(QStringLiteral("semantic.scope"));
        scope->setReadOnly(true);
        metadataLayout->addWidget(identities, 1);
        metadataLayout->addWidget(scope, 2);
        tabs->addTab(metadata, owner.tr("Scope and hash identity"));
        root->addWidget(tabs, 1);
        status = plainLabel(owner.tr("Ready. Native snapshots: 64 MiB per file. Receipts: 4 MiB."), &owner);
        status->setObjectName(QStringLiteral("semantic.status"));
        root->addWidget(status);
        auto* exportRow = new QHBoxLayout;
        exportPath = new QLineEdit(&owner);
        exportPath->setObjectName(QStringLiteral("semantic.exportPath"));
        exportPath->setMaxLength(static_cast<int>(kMaxPathCharacters));
        exportPath->setPlaceholderText(owner.tr("New report path — existing destinations are refused"));
        exportBrowse = new QPushButton(owner.tr("Browse…"), &owner);
        exportBrowse->setObjectName(QStringLiteral("semantic.browseExport"));
        exportButton = new QPushButton(owner.tr("Export new report"), &owner);
        exportButton->setObjectName(QStringLiteral("semantic.export"));
        exportRow->addWidget(exportPath, 1);
        exportRow->addWidget(exportBrowse);
        exportRow->addWidget(exportButton);
        root->addLayout(exportRow);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &owner);
        QObject::connect(close, &QDialogButtonBox::rejected, &owner, &SemanticReviewDialog::reject);
        root->addWidget(close);

        QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int index)
        {
            inputs->setCurrentIndex(index);
            run->setText(index == 0 ? owner.tr("Compare saved snapshots") : owner.tr("Inspect recorded receipt"));
            invalidate();
        });
        QObject::connect(run, &QPushButton::clicked, &owner, [this] { startLoad(); });
        QObject::connect(cancel, &QPushButton::clicked, &owner, [this] { cancelWork(); });
        QObject::connect(pathFilter, &QLineEdit::textChanged, &owner, [this] { filterRows(); });
        QObject::connect(typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this] { filterRows(); });
        QObject::connect(exportBrowse, &QPushButton::clicked, &owner, [this] { chooseExportPath(); });
        QObject::connect(exportButton, &QPushButton::clicked, &owner, [this] { startExport(); });
        updateActions();
        filterRows();
    }

    void updateActions()
    {
        const bool complete = mode->currentIndex() == 1 ? !receiptPath->text().isEmpty()
            : std::all_of(snapshotPaths.begin(), snapshotPaths.end(), [](const auto* path)
            {
                return !path->text().isEmpty();
            });
        run->setEnabled(!busy && complete);
        cancel->setEnabled(busy);
        exportButton->setEnabled(!busy && static_cast<bool>(review));
        exportPath->setEnabled(!busy);
        exportBrowse->setEnabled(!busy);
        const bool exporting = busy && jobKind == JobKind::Export;
        mode->setEnabled(!exporting);
        inputs->setEnabled(!exporting);
    }

    void clearReview()
    {
        review.reset();
        identities->clear();
        scope->clear();
        for ( auto& view : views )
        {
            view.description->clear();
            view.summary->setText(owner.tr("No review loaded."));
            view.filtered.clear();
            view.page = 0;
            view.table->setRowCount(0);
            view.details->clear();
        }
        filterRows();
    }

    void invalidate()
    {
        ++generation;
        cancellation.request_stop();
        clearReview();
        provenance->setText(owner.tr("Inputs changed. Run the review to capture their bytes and identities."));
        status->setText(busy ? owner.tr("Inputs changed; cancelling the previous work. Its result will be discarded.")
            : owner.tr("Ready to review explicit inputs."));
        updateActions();
    }

    void cancelWork()
    {
        if ( !busy )
        {
            return;
        }
        cancellation.request_stop();
        if ( jobKind == JobKind::Load )
        {
            ++generation;
        }
        status->setText(owner.tr("Cancellation requested. Waiting for the current bounded read/decode step to return."));
    }

    void abandon()
    {
        cancellation.request_stop();
        ++generation;
    }

    void startJob(JobKind kind, std::function<Outcome(sed::StopToken)> work)
    {
        if ( busy )
        {
            return;
        }
        cancellation = sed::StopSource{};
        jobGeneration = ++generation;
        jobKind = kind;
        busy = true;
        updateActions();
        const sed::StopToken token = cancellation.get_token();
        try
        {
            watcher->setFuture(QtConcurrent::run([work = std::move(work), token, kind]()
            {
                try
                {
                    return work(token);
                }
                catch ( const std::exception& error )
                {
                    Outcome failed;
                    const QString message = QStringLiteral("Review work failed: ") + QString::fromUtf8(error.what());
                    if ( kind == JobKind::Load )
                    {
                        failed.loaded = {{}, message, ErrorCode::ReadFailed};
                    }
                    else
                    {
                        failed.exported = {false, message, ErrorCode::WriteFailed};
                    }
                    return failed;
                }
                catch ( ... )
                {
                    Outcome failed;
                    const QString message = QStringLiteral("Review work failed with an unknown exception.");
                    failed.loaded = {{}, message, ErrorCode::ReadFailed};
                    failed.exported = {false, message, ErrorCode::WriteFailed};
                    return failed;
                }
            }));
        }
        catch ( const std::exception& error )
        {
            busy = false;
            status->setText(owner.tr("Could not start review work: %1").arg(QString::fromUtf8(error.what())));
            updateActions();
        }
        catch ( ... )
        {
            busy = false;
            status->setText(owner.tr("Could not start review work: unknown exception."));
            updateActions();
        }
    }

    void startLoad()
    {
        if ( busy )
        {
            return;
        }
        const Request request = mode->currentIndex() == 0
            ? Request{SnapshotRequest{snapshotPaths[0]->text(), snapshotPaths[1]->text(), snapshotPaths[2]->text()}}
            : Request{ReceiptRequest{receiptPath->text()}};
        clearReview();
        status->setText(owner.tr("Capturing bounded inputs and preparing the review…"));
        // Capture every input and the service by value. Background work has no
        // path back to this object or to any writable editor model.
        startJob(JobKind::Load, [request, load = loader](sed::StopToken token)
        {
            Outcome result;
            result.loaded = load(request, token);
            return result;
        });
    }

    void chooseExportPath()
    {
        const QString selected = QFileDialog::getSaveFileName(&owner,
            owner.tr("Create a new review report"), exportPath->text(), owner.tr("JSON report (*.json)"),
            nullptr, QFileDialog::DontConfirmOverwrite);
        if ( !selected.isEmpty() )
        {
            exportPath->setText(selected);
        }
    }

    void startExport()
    {
        if ( busy || !review )
        {
            return;
        }
        if ( exportPath->text().isEmpty() )
        {
            chooseExportPath();
        }
        if ( exportPath->text().isEmpty() )
        {
            return;
        }
        exportingPath = exportPath->text();
        status->setText(owner.tr("Creating a new report from the settled review…"));
        startJob(JobKind::Export, [captured = review, destination = exportingPath](sed::StopToken token)
        {
            Outcome result;
            result.exported = exportReviewCreateOnly(*captured, destination, token);
            return result;
        });
    }

    void finish()
    {
        busy = false;
        try
        {
            const Outcome result = watcher->result();
            if ( jobKind == JobKind::Export )
            {
                // Cancellation can race the final successful file close. Report
                // that real outcome instead of claiming an already-created file
                // was cancelled. This never publishes a new comparison result.
                status->setText(detail::exportResultStatus(result.exported, exportingPath));
            }
            else if ( jobGeneration != generation || cancellation.stop_requested() )
            {
                status->setText(owner.tr("Previous review cancelled or superseded; its result was discarded."));
            }
            else if ( result.loaded.review && result.loaded.code == ErrorCode::None )
            {
                review = result.loaded.review;
                presentReview();
                status->setText(review->sourceKind == SourceKind::ImportedReceipt
                    ? owner.tr("Recorded receipt loaded. Contents were not recomputed.")
                    : owner.tr("Loaded saved snapshots compared. Source files were read only."));
            }
            else
            {
                status->setText(result.loaded.error.isEmpty() ? owner.tr("Review failed without a result.") : result.loaded.error);
            }
        }
        catch ( const std::exception& error )
        {
            clearReview();
            status->setText(owner.tr("Review failed: %1").arg(QString::fromUtf8(error.what())));
        }
        catch ( ... )
        {
            clearReview();
            status->setText(owner.tr("Review failed with an unknown exception."));
        }
        updateActions();
    }

    void presentReview()
    {
        const bool imported = review->sourceKind == SourceKind::ImportedReceipt;
        provenance->setText(review->provenance);
        views[0].description->setText(imported
            ? owner.tr("Authored: source versus patch candidate, as recorded in the receipt; not recomputed.")
            : owner.tr("Authored tab: loaded baseline versus loaded saved candidate. Both are file snapshots."));
        views[1].description->setText(imported
            ? owner.tr("Persisted: source versus reopened proposed output, as recorded in the receipt; not recomputed.")
            : owner.tr("Persisted tab: loaded baseline versus loaded saved output. No serializer effect is inferred."));
        views[0].summary->setText(summaryText(review->authored));
        views[1].summary->setText(summaryText(review->persisted));
        QStringList identityLines;
        for ( const auto& file : review->files )
        {
            identityLines << QStringLiteral("%1 — %2 (%3 captured bytes)").arg(file.role, file.path,
                QString::number(static_cast<qulonglong>(file.byteCount)));
            identityLines << QStringLiteral("SHA-256 of captured bytes: %1").arg(file.sha256);
            if ( file.nativeHadCorruption || file.nativeRepairedChunks != 0 )
            {
                identityLines << QStringLiteral("Native integrity: corruption/repair reported; %1 repaired chunks; sections %2.")
                    .arg(file.nativeRepairedChunks).arg(file.nativeRepairedSections.join(QStringLiteral(", ")));
            }
        }
        if ( imported )
        {
            identityLines << QStringLiteral("Receipt-asserted input SHA-256 (not verified): %1").arg(review->inputSha256);
            identityLines << QStringLiteral("Receipt-asserted output SHA-256 (not verified): %1").arg(review->outputSha256);
        }
        identityLines << review->warnings;
        identities->setPlainText(identityLines.join(QLatin1Char('\n')));
        QStringList scopeLines{
            QStringLiteral("Comparison version: %1\nCoverage: %2\nAddressing: %3")
                .arg(preview::kVersion).arg(text(preview::kCoverage), text(preview::kAddressing)),
            owner.tr("Paths address storage positions separately in each snapshot. Counts are change records, not entity counts."),
            QStringLiteral("Limits per view: %1 visited slots; %2 compared string bytes; %3 emitted records; "
                "%4 raw bytes per inline string; %5 aggregate raw inline string bytes.")
                .arg(static_cast<qulonglong>(review->limits.maxVisitedSlots))
                .arg(static_cast<qulonglong>(review->limits.maxComparedStringBytes))
                .arg(static_cast<qulonglong>(review->limits.maxChanges))
                .arg(static_cast<qulonglong>(review->limits.maxInlineStringBytes))
                .arg(static_cast<qulonglong>(review->limits.maxTotalInlineStringBytes)),
            owner.tr("Filters and pages cover emitted rows only. They cannot recover omitted records or unscanned changes."),
            owner.tr("Excluded fields:")};
        for ( const auto field : preview::kExcludedFields )
        {
            scopeLines << text(field);
        }
        scopeLines << owner.tr("Excluded scopes:");
        for ( const auto excluded : preview::kExcludedScopes )
        {
            scopeLines << text(excluded);
        }
        scopeLines << owner.tr("A complete zero-change comparison only covers this model contract. "
            "It does not certify preservation, validation, or gameplay.");
        scope->setPlainText(scopeLines.join(QLatin1Char('\n')));
        filterRows();
    }

    const preview::Comparison& comparison(std::size_t index) const
    {
        return index == 0 ? review->authored : review->persisted;
    }

    void filterRows()
    {
        for ( std::size_t index = 0; index < views.size(); ++index )
        {
            View& view = views[index];
            view.filtered.clear();
            view.page = 0;
            if ( review )
            {
                const QString path = pathFilter->text();
                const QString type = typeFilter->currentData().toString();
                const auto& changes = comparison(index).changes;
                for ( std::size_t row = 0; row < changes.size(); ++row )
                {
                    const auto& change = changes[row];
                    if ( text(change.path).contains(path, Qt::CaseSensitive)
                        && (type.isEmpty() || valueType(change.before) == type || valueType(change.after) == type) )
                    {
                        view.filtered.push_back(row);
                    }
                }
            }
            renderPage(index);
        }
    }

    void renderPage(std::size_t index)
    {
        View& view = views[index];
        const std::size_t first = std::min(view.page * kPageSize, view.filtered.size());
        const std::size_t count = std::min(kPageSize, view.filtered.size() - first);
        view.table->setRowCount(0);
        view.details->clear();
        view.table->setRowCount(static_cast<int>(count));
        for ( std::size_t row = 0; row < count; ++row )
        {
            const auto& change = comparison(index).changes[view.filtered[first + row]];
            const std::array<QString, 4> cells{text(change.path), text(preview::name(change.kind)),
                descriptorText(change.before), descriptorText(change.after)};
            for ( int column = 0; column < 4; ++column )
            {
                auto* item = new QTableWidgetItem(cells[static_cast<std::size_t>(column)]);
                item->setToolTip(item->text());
                view.table->setItem(static_cast<int>(row), column, item);
            }
        }
        view.previous->setEnabled(first != 0);
        view.next->setEnabled(first + count < view.filtered.size());
        view.pageLabel->setText(QStringLiteral("%1–%2 of %3 matching emitted rows (%4 per page).")
            .arg(static_cast<qulonglong>(count == 0 ? 0 : first + 1))
            .arg(static_cast<qulonglong>(first + count))
            .arg(static_cast<qulonglong>(view.filtered.size()))
            .arg(static_cast<qulonglong>(kPageSize)));
    }

    void showDetails(std::size_t index)
    {
        View& view = views[index];
        const int row = view.table->currentRow();
        const auto position = view.page * kPageSize + static_cast<std::size_t>(std::max(row, 0));
        if ( !review || row < 0 || position >= view.filtered.size() )
        {
            view.details->clear();
            return;
        }
        const auto& change = comparison(index).changes[view.filtered[position]];
        view.details->setPlainText(QStringLiteral("Path: %1\nKind: %2\nBefore: %3\nAfter: %4")
            .arg(text(change.path), text(preview::name(change.kind)),
                descriptorText(change.before), descriptorText(change.after)));
    }
};

SemanticReviewDialog::SemanticReviewDialog(QWidget* parent)
    : SemanticReviewDialog(loadReview, parent)
{
}

SemanticReviewDialog::SemanticReviewDialog(Loader loader, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(loader)))
{
}

SemanticReviewDialog::~SemanticReviewDialog() = default;

void SemanticReviewDialog::reject()
{
    impl_->abandon();
    QDialog::reject();
}

void SemanticReviewDialog::closeEvent(QCloseEvent* event)
{
    impl_->abandon();
    QDialog::closeEvent(event);
}

} // namespace sed::ui::semantic_review
