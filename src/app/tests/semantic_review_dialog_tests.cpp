#include "ui/semantic/semantic_review_dialog.hpp"
#include "ui/semantic/semantic_review_dialog_status.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <bit>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using namespace sed::ui::semantic_review;
namespace preview = sed::semantic_preview;
int failures{};

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template<typename Widget>
Widget* widget(SemanticReviewDialog& dialog, const char* name)
{
    auto* found = dialog.findChild<Widget*>(QString::fromLatin1(name));
    check(found != nullptr, name);
    return found;
}

bool until(const std::function<bool()>& ready)
{
    QElapsedTimer deadline;
    deadline.start();
    while ( !ready() && deadline.elapsed() < 10000 )
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return ready();
}

std::shared_ptr<const Review> recordedReview(bool partial = false)
{
    auto review = std::make_shared<Review>();
    review->sourceKind = SourceKind::ImportedReceipt;
    review->provenance = QStringLiteral("Imported plan receipt; recorded contents were not recomputed.");
    review->receiptCommand = QStringLiteral("plan");
    review->inputSha256 = QString(64, QLatin1Char('1'));
    review->outputSha256 = QString(64, QLatin1Char('2'));
    review->files.push_back({QStringLiteral("receipt"), QStringLiteral("review.json"),
        QString(64, QLatin1Char('3')), 100});
    for ( int i = 0; i < 70; ++i )
    {
        preview::Change change;
        change.path = "/things/" + std::to_string(i) + "/sector";
        change.before.value = preview::Value{std::int32_t{-1}};
        change.after.value = preview::Value{std::int32_t{i}};
        review->authored.changes.push_back(std::move(change));
    }
    preview::Change absent;
    absent.path = "/lights/0/rgbIntensity";
    absent.kind = preview::ChangeKind::Presence;
    absent.after.value = preview::Value{preview::Float64Value{0x8000000000000000ull}};
    review->authored.changes.push_back(std::move(absent));
    preview::Change omitted;
    omitted.path = "/lvisstring";
    omitted.before.value = preview::Value{preview::StringValue{600, std::nullopt}};
    omitted.after.value = preview::Value{preview::StringValue{700, std::nullopt}};
    review->authored.changes.push_back(std::move(omitted));
    review->authored.observedChanges = 140;
    review->authored.changed = true;
    review->authored.totalChanges = partial ? std::nullopt : std::optional<std::size_t>{140};
    review->authored.comparisonComplete = !partial;
    review->authored.detailsComplete = false;
    review->authored.reasons = {preview::Reason::ChangeLimit, preview::Reason::InlineStringLength};
    if ( partial )
    {
        review->authored.reasons.push_back(preview::Reason::ComparisonSlots);
    }
    review->authored.work = {1000000, 3000, 2000};
    review->persisted.comparisonComplete = false;
    review->persisted.detailsComplete = false;
    review->persisted.changed = std::nullopt;
    review->persisted.totalChanges = std::nullopt;
    review->persisted.reasons = {preview::Reason::ComparisonStringBytes};
    return review;
}

bool startReceipt(SemanticReviewDialog& dialog)
{
    auto* mode = widget<QComboBox>(dialog, "semantic.mode");
    auto* path = widget<QLineEdit>(dialog, "semantic.receipt");
    auto* run = widget<QPushButton>(dialog, "semantic.run");
    if ( !mode || !path || !run )
    {
        return false;
    }
    mode->setCurrentIndex(1);
    path->setText(QStringLiteral("review.json"));
    run->click();
    return true;
}

// Catches missing ordinary file controls, editable result cells, hidden
// provenance/omissions, entity-count language, and filters over unseen rows.
void controlsAndPresentation()
{
    SemanticReviewDialog dialog([](const Request&, std::stop_token)
    {
        return Result{recordedReview(), {}, ErrorCode::None};
    });
    for ( const char* name : {"semantic.baseline", "semantic.candidate", "semantic.output",
        "semantic.receipt", "semantic.exportPath"} )
    {
        (void)widget<QLineEdit>(dialog, name);
    }
    for ( const char* name : {"semantic.browseBaseline", "semantic.browseCandidate",
        "semantic.browseOutput", "semantic.browseReceipt", "semantic.browseExport"} )
    {
        (void)widget<QPushButton>(dialog, name);
    }
    if ( !startReceipt(dialog) )
    {
        return;
    }
    auto* table = widget<QTableWidget>(dialog, "semantic.authored.table");
    auto* total = widget<QLabel>(dialog, "semantic.authored.summary");
    auto* provenance = widget<QLabel>(dialog, "semantic.provenance");
    auto* identity = widget<QPlainTextEdit>(dialog, "semantic.identities");
    auto* scope = widget<QPlainTextEdit>(dialog, "semantic.scope");
    auto* page = widget<QLabel>(dialog, "semantic.authored.page");
    auto* next = widget<QPushButton>(dialog, "semantic.authored.next");
    auto* pathFilter = widget<QLineEdit>(dialog, "semantic.pathFilter");
    auto* typeFilter = widget<QComboBox>(dialog, "semantic.typeFilter");
    if ( !table || !total || !provenance || !identity || !scope || !page || !next
        || !pathFilter || !typeFilter )
    {
        return;
    }
    check(until([&] { return table->rowCount() == 32; }), "first page has exactly 32 rows");
    check(table->editTriggers() == QAbstractItemView::NoEditTriggers, "review rows cannot edit level values");
    check(provenance->text().contains(QStringLiteral("not recomputed")), "receipt provenance is explicit");
    check(identity->toPlainText().contains(QString(64, QLatin1Char('1')))
        && identity->toPlainText().contains(QString(64, QLatin1Char('2')))
        && identity->toPlainText().contains(QString(64, QLatin1Char('3'))),
        "source/output assertions and receipt byte hash are all inspectable");
    check(total->text().contains(QStringLiteral("140"))
        && total->text().contains(QStringLiteral("exact"), Qt::CaseInsensitive)
        && total->text().contains(QStringLiteral("72"))
        && total->text().contains(QStringLiteral("omitted"), Qt::CaseInsensitive),
        "exact record total and emitted/omitted details are labelled separately");
    check(scope->toPlainText().contains(QStringLiteral("snapshot-storage-slots"))
        && scope->toPlainText().contains(QStringLiteral("nativeOptionalSectionBytes"))
        && scope->toPlainText().contains(QStringLiteral("/sectors/*/surfaces/*/d3dId"))
        && scope->toPlainText().contains(QStringLiteral("1000000")),
        "coverage, exclusions, addressing and limits are inspectable");
    next->click();
    check(table->rowCount() == 32 && table->item(0, 0)->text() == QStringLiteral("/things/32/sector"),
        "second page retains deterministic emitted row order");
    next->click();
    check(table->rowCount() == 8 && !next->isEnabled(), "last bounded page does not invent missing rows");
    pathFilter->setText(QStringLiteral("/lights/"));
    check(table->rowCount() == 1 && page->text().contains(QStringLiteral("emitted")),
        "path filter resets page and states emitted-row scope");
    check(table->item(0, 2)->text().contains(QStringLiteral("absent"), Qt::CaseInsensitive)
        && table->item(0, 3)->text().contains(QStringLiteral("8000000000000000")),
        "optional absence and negative zero bits are distinct in visible cells");
    typeFilter->setCurrentIndex(typeFilter->findData(QStringLiteral("int32")));
    check(table->rowCount() == 0, "path and type filters intersect");
    pathFilter->clear();
    check(table->rowCount() == 32, "type filter includes signed integer records");
    typeFilter->setCurrentIndex(typeFilter->findData(QStringLiteral("string")));
    check(table->rowCount() == 1 && table->item(0, 2)->text().contains(QStringLiteral("600"))
        && table->item(0, 2)->text().contains(QStringLiteral("omitted"), Qt::CaseInsensitive),
        "over-budget string is explicitly omitted with byte count");
    auto* persisted = widget<QLabel>(dialog, "semantic.persisted.summary");
    check(persisted && persisted->text().contains(QStringLiteral("unknown"), Qt::CaseInsensitive)
        && persisted->text().contains(QStringLiteral("lower bound"), Qt::CaseInsensitive),
        "incomplete zero observations never say unchanged");
}

void lowerBoundAndFailures()
{
    SemanticReviewDialog partial([](const Request&, std::stop_token)
    {
        return Result{recordedReview(true), {}, ErrorCode::None};
    });
    if ( startReceipt(partial) )
    {
        auto* total = widget<QLabel>(partial, "semantic.authored.summary");
        check(total && until([&] { return total->text().contains(QStringLiteral("140")); })
            && total->text().contains(QStringLiteral("lower bound"), Qt::CaseInsensitive)
            && !total->text().contains(QStringLiteral("Exact total")),
            "incomplete comparison shows observed lower bound");
    }
    SemanticReviewDialog throwing([](const Request&, std::stop_token) -> Result
    {
        throw std::runtime_error("synthetic loader exception");
    });
    if ( startReceipt(throwing) )
    {
        auto* status = widget<QLabel>(throwing, "semantic.status");
        auto* run = widget<QPushButton>(throwing, "semantic.run");
        auto* exportButton = widget<QPushButton>(throwing, "semantic.export");
        check(status && run && until([&] { return run->isEnabled(); })
            && status->text().contains(QStringLiteral("synthetic loader exception"))
            && exportButton && !exportButton->isEnabled(),
            "worker exception is a visible failure and cannot export stale data");
    }
    SemanticReviewDialog unknown([](const Request&, std::stop_token) -> Result
    {
        throw 7;
    });
    if ( startReceipt(unknown) )
    {
        auto* status = widget<QLabel>(unknown, "semantic.status");
        auto* run = widget<QPushButton>(unknown, "semantic.run");
        check(status && run && until([&] { return run->isEnabled(); })
            && status->text().contains(QStringLiteral("failed"), Qt::CaseInsensitive),
            "non-standard worker exception remains guarded");
    }
}

// A blocked loader is a controlled dependency boundary, not a simulated Qt
// event loop: user edits and Cancel are delivered through real widget actions.
void staleCancellationAndLifetime()
{
    auto started = std::make_shared<std::atomic_bool>(false);
    auto finished = std::make_shared<std::atomic_bool>(false);
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto release = std::make_shared<QSemaphore>();
    auto loader = [started, finished, cancelled, release](const Request&, std::stop_token token)
    {
        started->store(true);
        release->acquire();
        cancelled->store(token.stop_requested());
        finished->store(true);
        return Result{recordedReview(), {}, ErrorCode::None};
    };
    SemanticReviewDialog dialog(loader);
    if ( !startReceipt(dialog) )
    {
        return;
    }
    check(until([&] { return started->load(); }), "worker started asynchronously");
    auto* path = widget<QLineEdit>(dialog, "semantic.receipt");
    auto* run = widget<QPushButton>(dialog, "semantic.run");
    auto* exportButton = widget<QPushButton>(dialog, "semantic.export");
    auto* table = widget<QTableWidget>(dialog, "semantic.authored.table");
    check(run && !run->isEnabled(), "one active job prevents queued unbounded copies");
    if ( path )
    {
        path->setText(QStringLiteral("different-receipt.json"));
    }
    release->release();
    check(until([&] { return finished->load() && run && run->isEnabled(); }) && cancelled->load(),
        "input edit cancels the immutable prior request");
    check(table && table->rowCount() == 0 && exportButton && !exportButton->isEnabled(),
        "late successful prior generation cannot publish or enable export");

    started->store(false);
    finished->store(false);
    cancelled->store(false);
    if ( run )
    {
        run->click();
    }
    check(until([&] { return started->load(); }), "second job starts only after first finishes");
    if ( auto* cancel = widget<QPushButton>(dialog, "semantic.cancel") )
    {
        cancel->click();
    }
    release->release();
    check(until([&] { return finished->load() && run && run->isEnabled(); }) && cancelled->load()
        && table && table->rowCount() == 0, "Cancel discards a late success");

    started->store(false);
    finished->store(false);
    cancelled->store(false);
    auto owned = std::make_unique<SemanticReviewDialog>(loader);
    if ( !startReceipt(*owned) )
    {
        return;
    }
    check(until([&] { return started->load(); }), "lifetime worker started");
    owned.reset();
    release->release();
    check(until([&] { return finished->load(); }) && cancelled->load(),
        "dialog destruction requests cancellation without accessing dead widgets");
    QApplication::processEvents();
}

void exportDiagnostics()
{
    const QString cleanup = QStringLiteral("Report export cancelled. The incomplete new report could not be removed.");
    check(detail::exportResultStatus({false, cleanup, ErrorCode::Cancelled}, {}) == cleanup,
        "cancelled export preserves its cleanup-failure diagnostic verbatim");
    check(detail::exportResultStatus({false, {}, ErrorCode::Cancelled}, {})
        == QStringLiteral("Report export cancelled."),
        "empty cancellation diagnostic has an explicit fallback");
    check(detail::exportResultStatus({true, cleanup, ErrorCode::Cancelled}, QStringLiteral("report%1.json"))
        == QStringLiteral("Created new report: report%1.json"),
        "completed creation takes precedence over a late cancellation");
}

// Catches a real queued export being cancelled before service entry and
// clearing the status to an empty error string. The barrier is deterministic;
// it does not rely on sleeps or a slow filesystem to win the cancellation race.
void queuedExportCancellation()
{
    SemanticReviewDialog dialog([](const Request&, std::stop_token)
    {
        return Result{recordedReview(), {}, ErrorCode::None};
    });
    if ( !startReceipt(dialog) )
    {
        return;
    }
    auto* run = widget<QPushButton>(dialog, "semantic.run");
    auto* exportButton = widget<QPushButton>(dialog, "semantic.export");
    auto* exportPath = widget<QLineEdit>(dialog, "semantic.exportPath");
    auto* cancel = widget<QPushButton>(dialog, "semantic.cancel");
    auto* status = widget<QLabel>(dialog, "semantic.status");
    if ( !run || !exportButton || !exportPath || !cancel || !status )
    {
        return;
    }
    check(until([&] { return run->isEnabled() && exportButton->isEnabled(); }),
        "settled review is ready for queued export");
    QTemporaryDir directory;
    check(directory.isValid(), "isolated cancellation export directory exists");
    if ( !directory.isValid() )
    {
        return;
    }
    const QString destination = directory.filePath(QStringLiteral("cancelled.json"));
    exportPath->setText(destination);
    auto* pool = QThreadPool::globalInstance();
    const int oldMaximum = pool->maxThreadCount();
    pool->waitForDone();
    pool->setMaxThreadCount(1);
    auto started = std::make_shared<std::atomic_bool>(false);
    auto release = std::make_shared<QSemaphore>();
    const auto barrier = QtConcurrent::run(pool, [started, release]
    {
        started->store(true);
        release->acquire();
    });
    check(until([&] { return started->load(); }), "export cancellation barrier is active");
    exportButton->click();
    cancel->click();
    release->release();
    check(until([&] { return run->isEnabled(); })
        && status->text().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)
        && !QFile::exists(destination),
        "queued export cancellation reports cancellation and creates no file");
    pool->waitForDone();
    pool->setMaxThreadCount(oldMaximum);
}

// Catches QString's successive arg() calls treating admitted authored strings
// or file paths as new format placeholders instead of preserving their text.
void literalPlaceholderValues()
{
    SemanticReviewDialog dialog([](const Request&, std::stop_token)
    {
        auto review = std::make_shared<Review>();
        review->sourceKind = SourceKind::ImportedReceipt;
        review->provenance = QStringLiteral("Recorded receipt, not recomputed.");
        review->files.push_back({QStringLiteral("receipt"), QStringLiteral("review%3.json"),
            QString(64, QLatin1Char('4')), 100});
        preview::Change change;
        change.path = "/lvisstring";
        change.before.value = preview::Value{preview::StringValue{2, std::string("%2")}};
        change.after.value = preview::Value{preview::StringValue{2, std::string("%1")}};
        review->authored.changes.push_back(change);
        review->authored.changed = true;
        review->authored.observedChanges = 1;
        review->authored.totalChanges = 1;
        return Result{review, {}, ErrorCode::None};
    });
    if ( !startReceipt(dialog) )
    {
        return;
    }
    auto* table = widget<QTableWidget>(dialog, "semantic.authored.table");
    auto* details = widget<QPlainTextEdit>(dialog, "semantic.authored.details");
    auto* identities = widget<QPlainTextEdit>(dialog, "semantic.identities");
    if ( !table || !details || !identities )
    {
        return;
    }
    check(until([&] { return table->rowCount() == 1; }), "literal authored string review loaded");
    if ( table->rowCount() != 1 )
    {
        return;
    }
    table->selectRow(0);
    check(table->item(0, 2)->text() == QStringLiteral("string \"%2\" (2 bytes)")
        && table->item(0, 3)->text() == QStringLiteral("string \"%1\" (2 bytes)")
        && table->item(0, 2)->toolTip() == table->item(0, 2)->text()
        && details->toPlainText().contains(QStringLiteral("string \"%2\" (2 bytes)")),
        "literal percent placeholders survive exact cell, tooltip and detail rendering");
    check(identities->toPlainText().contains(QStringLiteral("review%3.json")),
        "literal percent placeholder in input path survives identity rendering");
}

void savedSnapshotsAndExport(const QString& capturePath)
{
    QTemporaryDir directory;
    check(directory.isValid(), "isolated native GUI fixture directory exists");
    if ( !directory.isValid() )
    {
        return;
    }
    sed::Level source;
    source.kind = sed::ProjectType::MOTS;
    source.header.gravity = 4.0;
    source.lights.resize(1);
    auto save = [&](const char* name, const sed::Level& level)
    {
        std::vector<sed::assets::SedOptionalSection> sections;
        if ( auto rgb = sed::assets::makeSedLightRgbSection(level) )
        {
            sections.push_back(std::move(*rgb));
        }
        const auto bytes = sed::assets::saveToSedBytes(level, sections,
            sed::assets::SedSerializationProfile::ProtectedV1, 0);
        const QString path = directory.filePath(QString::fromLatin1(name));
        QFile file(path);
        if ( !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || file.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) )
        {
            throw std::runtime_error("cannot create native GUI fixture");
        }
        return path;
    };
    const QString baseline = save("baseline.sed", source);
    source.header.gravity = 6.0;
    source.lights[0].rgbIntensity = -0.0;
    const QString candidate = save("candidate.sed", source);
    source.header.gravity = 8.0;
    const QString output = save("output.sed", source);
    SemanticReviewDialog dialog;
    auto* before = widget<QLineEdit>(dialog, "semantic.baseline");
    auto* after = widget<QLineEdit>(dialog, "semantic.candidate");
    auto* persisted = widget<QLineEdit>(dialog, "semantic.output");
    auto* run = widget<QPushButton>(dialog, "semantic.run");
    auto* exportButton = widget<QPushButton>(dialog, "semantic.export");
    auto* exportPath = widget<QLineEdit>(dialog, "semantic.exportPath");
    if ( !before || !after || !persisted || !run || !exportButton || !exportPath )
    {
        return;
    }
    before->setText(baseline);
    after->setText(candidate);
    persisted->setText(output);
    run->click();
    check(until([&] { return run->isEnabled(); }) && exportButton->isEnabled(),
        "ordinary compare button loads real explicit native files");
    auto* label = widget<QLabel>(dialog, "semantic.provenance");
    auto* authored = widget<QTableWidget>(dialog, "semantic.authored.table");
    check(label && label->text().contains(QStringLiteral("loaded"), Qt::CaseInsensitive)
        && label->text().contains(QStringLiteral("snapshot"), Qt::CaseInsensitive),
        "file comparison says loaded snapshots");
    check(authored && authored->rowCount() == 2, "real loader retains RGB absence-to-negative-zero alongside gravity");
    if ( !capturePath.isEmpty() )
    {
        // Neutral names only in the public presentation capture. They are not
        // changed input requests: render a copy of the settled presentation.
        for ( auto* input : {before, after, persisted} )
        {
            const QSignalBlocker blocker(input);
            input->setText(QFileInfo(input->text()).fileName());
        }
        dialog.resize(1120, 900);
        dialog.show();
        QApplication::processEvents();
        check(dialog.grab().save(capturePath), "offscreen screenshot saved");
        dialog.hide();
    }
    const QString report = directory.filePath(QStringLiteral("new-review.json"));
    exportPath->setText(report);
    exportButton->click();
    check(until([&] { return QFile::exists(report) && run->isEnabled(); }),
        "explicit report export creates a separate report");
    QFile file(report);
    check(file.open(QIODevice::ReadOnly), "created report readable");
    const QByteArray first = file.readAll();
    file.close();
    exportButton->click();
    auto* status = widget<QLabel>(dialog, "semantic.status");
    check(until([&] { return run->isEnabled(); }) && status
        && status->text().contains(QStringLiteral("exist"), Qt::CaseInsensitive),
        "ordinary export refuses an existing destination");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == first,
        "failed report export preserves exact existing bytes");
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QString capture;
    if ( argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture") )
    {
        capture = QString::fromLocal8Bit(argv[2]);
    }
    try
    {
        controlsAndPresentation();
        lowerBoundAndFailures();
        staleCancellationAndLifetime();
        exportDiagnostics();
        queuedExportCancellation();
        literalPlaceholderValues();
        savedSnapshotsAndExport(capture);
    }
    catch ( const std::exception& error )
    {
        check(false, error.what());
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "semantic review dialog failures=" << failures << '\n';
    return failures ? 1 : 0;
}
