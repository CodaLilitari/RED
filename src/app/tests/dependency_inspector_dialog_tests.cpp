#include "ui/resources/dependency_inspector_dialog.hpp"

#include "sed/assets/gob.hpp"
#include "sed/level/dependencies.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
int failures{};

void check(const bool condition, const char* name)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

bool waitFor(const std::function<bool()>& ready)
{
    QElapsedTimer timer;
    timer.start();
    while ( !ready() && timer.elapsed() < 10000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

bool write(const QString& path, const std::string& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes.data(), static_cast<qint64>(bytes.size()))
        == static_cast<qint64>(bytes.size());
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString hash(const std::string& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(bytes),
        QCryptographicHash::Sha256).toHex());
}

template<class T>
T* control(QWidget& dialog, const char* name)
{
    return dialog.findChild<T*>(QString::fromLatin1(name));
}

QString cell(QTableWidget* table, const int row, const int column)
{
    const auto* item = table->item(row, column);
    return item ? item->text() : QString{};
}

void run()
{
    QTemporaryDir directory;
    check(directory.isValid(), "dependency test has isolated temporary inputs");
    if ( !directory.isValid() )
    {
        return;
    }
    const std::string source = "SECTION: MODELS\nWorld Models 1\ncrate.3do\nEND\n";
    const auto firstBytes = sed::assets::buildGobBytes({{"3do/crate.3do", "wall.mat\n"}, {"mat/wall.mat", "FIRST"}});
    const auto secondBytes = sed::assets::buildGobBytes({{"3do/crate.3do", "wall.mat\n"}, {"mat/wall.mat", "SECOND"}});
    const QString levelPath = directory.filePath("level.jkl");
    const QString firstPath = directory.filePath("first.gob");
    const QString secondPath = directory.filePath("second.goo");
    check(write(levelPath, source) && write(firstPath, firstBytes) && write(secondPath, secondBytes),
        "synthetic dependency inputs are written");
    bool current = true;
    sed::ui::resources::DependencyInspectorDialog dialog({directory.path(), [&]
    {
        return current;
    }});
    auto* sourceControl = control<QLineEdit>(dialog, "dependencySource");
    auto* archives = control<QTableWidget>(dialog, "dependencyArchives");
    auto* extras = control<QTableWidget>(dialog, "dependencyExtras");
    auto* inspect = control<QPushButton>(dialog, "dependencyInspect");
    auto* cancel = control<QPushButton>(dialog, "dependencyCancel");
    auto* status = control<QLabel>(dialog, "dependencyStatus");
    auto* summary = control<QPlainTextEdit>(dialog, "dependencySummary");
    auto* nodes = control<QTableWidget>(dialog, "dependencyNodes");
    auto* edges = control<QTableWidget>(dialog, "dependencyEdges");
    auto* issues = control<QTableWidget>(dialog, "dependencyIssues");
    auto* evidence = control<QTableWidget>(dialog, "dependencyArchiveEvidence");
    auto* output = control<QLineEdit>(dialog, "dependencyOutput");
    auto* save = control<QPushButton>(dialog, "dependencySave");
    auto* addArchive = control<QPushButton>(dialog, "dependencyAddArchive");
    auto* archiveUp = control<QPushButton>(dialog, "dependencyArchiveUp");
    auto* addExtra = control<QPushButton>(dialog, "dependencyAddExtra");
    const bool controls = sourceControl && archives && extras && inspect && cancel && status && summary && nodes
        && edges && issues && evidence && output && save && addArchive && archiveUp && addExtra;
    check(controls, "ordinary GUI exposes captured inputs, graph, provenance, issues and report export");
    if ( !controls )
    {
        return;
    }
    for ( const auto& spec : sed::level::dependencyLimitSpecs() )
    {
        const auto* spin = dialog.findChild<QSpinBox*>("dependencyLimit_" + QString::fromUtf8(spec.name.data(),
            static_cast<qsizetype>(spec.name.size())));
        check(spin && spin->minimum() == static_cast<int>(spec.minimum)
            && spin->maximum() == static_cast<int>(spec.maximum), "every core operational limit is available with exact bounds");
    }
    auto* maxNodes = control<QSpinBox>(dialog, "dependencyLimit_maxNodes");
    auto* maxDetails = control<QSpinBox>(dialog, "dependencyLimit_maxDetailRecords");
    if ( !maxNodes || !maxDetails )
    {
        return;
    }
    sourceControl->setText(levelPath);
    addArchive->click();
    addArchive->click();
    check(archives->rowCount() == 2, "archive rows can be added without opening a picker");
    if ( archives->rowCount() != 2 )
    {
        return;
    }
    archives->item(0, 0)->setText(firstPath);
    archives->item(0, 1)->setCheckState(Qt::Checked);
    archives->item(1, 0)->setText(secondPath);
    const auto runInspection = [&]
    {
        inspect->click();
        check(waitFor([&]
        {
            return inspect->isEnabled();
        }), "dependency worker finishes within test deadline");
    };
    runInspection();
    check(nodes->rowCount() == 3 && edges->rowCount() == 2 && issues->rowCount() == 0,
        "captured synthetic model-to-material graph is shown");
    check(evidence->rowCount() == 2 && cell(evidence, 0, 2) == hash(firstBytes)
        && cell(evidence, 1, 2) == hash(secondBytes), "archive table preserves ordered raw capture hashes");
    check(cell(nodes, 2, 5) == hash("FIRST") && cell(nodes, 2, 6) == "yes"
        && cell(nodes, 2, 7) == "0" && cell(nodes, 2, 8) == "1",
        "node exposes exact payload hash, stock flag and physical row provenance");
    check(summary->toPlainText().contains(hash(source)) && summary->toPlainText().contains("Scan complete: yes")
        && summary->toPlainText().contains("Details complete: yes")
        && summary->toPlainText().contains("References resolved: yes")
        && summary->toPlainText().contains("Runtime closure verified: no"), "completeness is independent from runtime closure");
    const QString outputPath = directory.filePath("report.txt");
    output->setText(outputPath);
    save->click();
    const QByteArray report = read(outputPath);
    check(report.contains("modelMaterial") && report.contains(hash("FIRST").toLatin1())
        && report.contains("folderExact") && report.contains("maxWorkUnits")
        && report.contains("Raw names use reversible ASCII byte escapes"), "export includes full captured graph, provenance, limits and encoding contract");
    nodes->sortItems(0, Qt::DescendingOrder);
    output->setText(directory.filePath("sorted-report.txt"));
    save->click();
    check(read(output->text()) == report, "sorting a GUI table cannot truncate or reorder the captured report");
    nodes->sortItems(0, Qt::AscendingOrder);
    check(write(levelPath, source + "# changed after capture\n"), "post-capture source change is staged");
    output->setText(directory.filePath("historical-report.txt"));
    save->click();
    check(read(output->text()) == report && report.contains("not locked or revalidated after capture"),
        "historical report remains hash-bound to its capture and discloses later path drift");
    check(write(levelPath, source), "source fixture is restored after historical-report check");
    output->setText(outputPath);
    check(write(outputPath, "KEEP"), "existing report replacement test sets its sentinel");
    save->click();
    check(read(outputPath) == "KEEP" && status->text().contains("exist", Qt::CaseInsensitive),
        "report export refuses to replace an existing target");
    archives->setCurrentCell(1, 0);
    archiveUp->click();
    check(!save->isEnabled() && nodes->rowCount() == 0, "archive order changes invalidate the prior graph and export");
    runInspection();
    check(cell(nodes, 2, 5) == hash("SECOND") && cell(nodes, 2, 6) == "no",
        "archive reordering changes the actual winner and stock attribution");

    maxDetails->setValue(0);
    runInspection();
    check(nodes->rowCount() == 0 && edges->rowCount() == 0
        && summary->toPlainText().contains("Scan complete: yes")
        && summary->toPlainText().contains("Details complete: no")
        && summary->toPlainText().contains("Nodes: 3 (exact)"), "detail truncation retains exact completed counts and is not an empty graph");
    output->setText(directory.filePath("partial-details.txt"));
    save->click();
    check(read(output->text()).contains("Details complete: no") && read(output->text()).contains("Nodes: 3 (exact)"),
        "partial detail export retains completeness and exact totals");
    maxDetails->setValue(512);
    maxNodes->setValue(1);
    runInspection();
    check(summary->toPlainText().contains("Scan complete: no")
        && summary->toPlainText().contains("References resolved: unknown")
        && summary->toPlainText().contains("lower bound"), "operational stop explicitly discloses unknown resolution and lower-bound counts");
    maxNodes->setValue(4096);

    addExtra->click();
    if ( extras->rowCount() == 1 )
    {
        extras->item(0, 0)->setText("absent.mat");
        extras->item(0, 1)->setText("mat");
        runInspection();
        check(nodes->rowCount() == 4 && issues->rowCount() == 1
            && summary->toPlainText().contains("References resolved: no")
            && summary->toPlainText().contains("Missing: 1"), "logical additional asset exposes missing reference without opening loose files");
    }
    else
    {
        check(false, "additional referenced asset row can be added");
    }

    const auto coverageBytes = sed::assets::buildGobBytes({{"3do/crate.3do", "wall.mat\n"},
        {"mat/wall.mat", "SECOND"}, {"a/ambiguous.mat", "A"}, {"b/ambiguous.mat", "B"}, {"unknown.bin", "data"}});
    check(write(secondPath, coverageBytes), "ambiguous and unsupported archive fixture is written");
    extras->item(0, 0)->setText("ambiguous.mat");
    runInspection();
    check(summary->toPlainText().contains("Ambiguous: 1") && summary->toPlainText().contains("Missing: 0")
        && summary->toPlainText().contains("References resolved: no"), "ambiguous archive aliases remain distinct from missing assets");
    extras->item(0, 0)->setText("unknown.bin");
    extras->item(0, 1)->setText("");
    runInspection();
    check(summary->toPlainText().contains("Unsupported: 1")
        && summary->toPlainText().contains("References resolved: yes") && issues->rowCount() == 1,
        "resolved unsupported dependencies show coverage issues separately from resolution");
    extras->item(0, 0)->setText(QString(1025, QLatin1Char('x')));
    runInspection();
    check(!save->isEnabled() && nodes->rowCount() == 0, "oversized logical reference refuses before worker publication");
    auto* removeExtra = control<QPushButton>(dialog, "dependencyRemoveExtra");
    check(removeExtra != nullptr, "logical reference rows can be removed");
    if ( !removeExtra )
    {
        return;
    }
    extras->setCurrentCell(0, 0);
    removeExtra->click();
    check(extras->rowCount() == 0, "removing an additional asset removes its request row");
    const std::string byteName = "odd" + std::string(1, static_cast<char>(0xff)) + ".mat";
    const auto byteArchive = sed::assets::buildGobBytes({{"3do/crate.3do", byteName + "\n"}, {"mat/" + byteName, "raw"}});
    check(write(secondPath, byteArchive), "raw archive-name byte fixture is written");
    runInspection();
    check(cell(nodes, 2, 1).contains("\\xFF") && cell(nodes, 2, 9).contains("\\xFF"),
        "arbitrary captured name bytes survive reversible GUI display encoding");
    output->setText(directory.filePath("raw-names.txt"));
    save->click();
    check(read(output->text()).contains("odd\\xFF.mat"), "report preserves arbitrary captured name bytes without invalid UTF-8");
    check(write(secondPath, secondBytes), "normal archive fixture is restored after coverage checks");

    sourceControl->setText(directory.filePath("missing.jkl"));
    runInspection();
    check(nodes->rowCount() == 0 && !save->isEnabled() && !status->text().isEmpty(),
        "failed source capture cannot reuse an earlier report");
    sourceControl->setText(levelPath);
    const QString oversized = directory.filePath("oversized.jkl");
    {
        QFile file(oversized);
        check(file.open(QIODevice::WriteOnly) && file.resize(128ll * 1024 * 1024 + 1),
            "sparse source over capture allowance can be created");
    }
    sourceControl->setText(oversized);
    runInspection();
    check(!save->isEnabled() && status->text().contains("allowance"), "source byte cap rejects oversized files before capture");
    sourceControl->setText(levelPath);
    check(write(secondPath, "not a gob"), "malformed capture fixture can be written");
    runInspection();
    check(nodes->rowCount() == 0 && !save->isEnabled(), "malformed archive capture fails closed before graph publication");
    check(write(secondPath, secondBytes), "archive fixture is restored");
    inspect->click();
    cancel->click();
    check(waitFor([&]
    {
        return inspect->isEnabled();
    }) && nodes->rowCount() == 0 && !save->isEnabled(),
        "cancel discards results even when the core scan finishes after cancellation");
    inspect->click();
    current = false;
    check(waitFor([&]
    {
        return inspect->isEnabled();
    }) && nodes->rowCount() == 0 && !save->isEnabled()
        && status->text().contains("context", Qt::CaseInsensitive), "owner context drift discards a completed worker snapshot");
    current = true;
    runInspection();
    output->setText(directory.filePath("stale.txt"));
    current = false;
    save->click();
    check(!QFile::exists(output->text()) && !save->isEnabled(), "context drift prevents report publication");
    current = true;
    {
        auto closing = std::make_unique<sed::ui::resources::DependencyInspectorDialog>(
            sed::ui::resources::DependencyInspectorDialog::Context{directory.path(), [&]
            {
                return current;
            }});
        control<QLineEdit>(*closing, "dependencySource")->setText(levelPath);
        control<QPushButton>(*closing, "dependencyInspect")->click();
        closing.reset();
        QApplication::processEvents();
    }
    check(read(levelPath) == QByteArray::fromStdString(source)
        && read(firstPath) == QByteArray::fromStdString(firstBytes)
        && read(secondPath) == QByteArray::fromStdString(secondBytes), "inspection, cancellation and export preserve every source input");
    const auto filesBeforeRowEdits = QDir(directory.path()).entryList(QDir::Files);
    for ( int index = archives->rowCount(); index < 17; ++index )
    {
        addArchive->click();
    }
    check(archives->rowCount() == 16 && status->text().contains("limited to 16"), "archive input admission enforces the sixteen-file contract");
    check(QDir(directory.path()).entryList(QDir::Files) == filesBeforeRowEdits, "editing input controls never writes files");
}

void throwingContext()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("source.jkl");
    check(directory.isValid() && write(path, ""), "throwing context fixture is isolated");
    bool throwContext = true;
    sed::ui::resources::DependencyInspectorDialog dialog({directory.path(), [&]
    {
        if ( throwContext )
        {
            throw std::runtime_error("owner source context unavailable");
        }
        return true;
    }});
    auto* source = control<QLineEdit>(dialog, "dependencySource");
    auto* inspect = control<QPushButton>(dialog, "dependencyInspect");
    auto* cancel = control<QPushButton>(dialog, "dependencyCancel");
    auto* save = control<QPushButton>(dialog, "dependencySave");
    auto* status = control<QLabel>(dialog, "dependencyStatus");
    auto* nodes = control<QTableWidget>(dialog, "dependencyNodes");
    auto* output = control<QLineEdit>(dialog, "dependencyOutput");
    source->setText(path);
    bool escapedException{};
    try
    {
        inspect->click();
    }
    catch ( const std::exception& )
    {
        escapedException = true;
    }
    check(!escapedException && inspect->isEnabled() && !cancel->isEnabled() && !save->isEnabled()
        && status->text().contains("owner source context unavailable"), "throwing context at Inspect is reported inline without escaping the GUI boundary");
    throwContext = false;
    inspect->click();
    throwContext = true;
    escapedException = false;
    try
    {
        check(waitFor([&]
        {
            return inspect->isEnabled();
        }), "throwing completion restores idle buttons");
    }
    catch ( const std::exception& )
    {
        escapedException = true;
    }
    check(!escapedException && inspect->isEnabled() && !cancel->isEnabled() && !save->isEnabled()
        && nodes->rowCount() == 0 && status->text().contains("owner source context unavailable"),
        "throwing context during worker completion discards results, cleans up and preserves the diagnostic");
    throwContext = false;
    inspect->click();
    check(waitFor([&]
    {
        return inspect->isEnabled();
    }) && save->isEnabled(), "successful retry restores the captured report after context failure");
    output->setText(directory.filePath("refused-report.txt"));
    throwContext = true;
    escapedException = false;
    try
    {
        save->click();
    }
    catch ( const std::exception& )
    {
        escapedException = true;
    }
    check(!escapedException && !QFile::exists(output->text()) && !save->isEnabled()
        && status->text().contains("owner source context unavailable"), "throwing context at export refuses publication without escaping the GUI boundary");
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    run();
    throwingContext();
    return failures == 0 ? 0 : 1;
}
