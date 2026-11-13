#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include "ui/components/preview_worker_pool.hpp"
#include <iostream>

#include "ui/resources/package_authoring_dialog.hpp"
#include "sed/assets/gob.hpp"
#include "sed/level/package.hpp"

namespace
{
int failures{};
void check(const bool value, const char* text)
{
    if ( !value )
    {
        ++failures;
        std::cerr << "FAIL: " << text << '\n';
    }
}
using Dialog = sed::ui::resources::PackageAuthoringDialog;
template<class T> T* widget(Dialog& dialog, const char* name)
{
    return dialog.findChild<T*>(QString::fromLatin1(name));
}
void write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "write owned package fixture");
}
QByteArray read(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read owned package output");
    return file.readAll();
}
bool ready(Dialog& dialog)
{
    QElapsedTimer timer;
    timer.start();
    while ( !widget<QPushButton>(dialog, "packagePrepare")->isEnabled() && timer.elapsed() < 5000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    const bool completed = widget<QPushButton>(dialog, "packagePrepare")->isEnabled();
    check(completed, "bounded package worker completes");
    return completed;
}
bool controls(Dialog& dialog)
{
    const bool present = widget<QTabWidget>(dialog, "packageMode")
        && widget<QTableWidget>(dialog, "packageEntries")
        && widget<QTableWidget>(dialog, "packageArchives")
        && widget<QTableWidget>(dialog, "packageExtras")
        && widget<QLineEdit>(dialog, "packageLevel")
        && widget<QLineEdit>(dialog, "packageLevelName")
        && widget<QLineEdit>(dialog, "packageEpisodeTitle")
        && widget<QSpinBox>(dialog, "packageEpisodeType")
        && widget<QSpinBox>(dialog, "packageMaxNodes")
        && widget<QSpinBox>(dialog, "packageMaxEdges")
        && widget<QSpinBox>(dialog, "packageMaxDepth")
        && widget<QSpinBox>(dialog, "packageMaxBytes")
        && widget<QCheckBox>(dialog, "packageAllowMissing")
        && widget<QComboBox>(dialog, "packageFormat")
        && widget<QLineEdit>(dialog, "packageOutput")
        && widget<QLineEdit>(dialog, "packageReportPath")
        && widget<QPlainTextEdit>(dialog, "packageReport")
        && widget<QPushButton>(dialog, "packagePrepare")
        && widget<QPushButton>(dialog, "packageCreate")
        && widget<QPushButton>(dialog, "packageExportReport")
        && widget<QPushButton>(dialog, "packageCancel");
    check(present, "ordinary GUI exposes archive and package workflows without JSON");
    return present;
}
void archiveWorkflow()
{
    QTemporaryDir directory;
    const QString source = directory.filePath(QString::fromUtf8("source-\xCE\xB1.bin"));
    const QString output = directory.filePath("new.gob");
    const QByteArray payload("abc\0\xff", 5);
    write(source, payload);
    Dialog dialog({directory.path(), [] { return true; }, {}});
    if ( !controls(dialog) )
    {
        return;
    }
    auto* table = widget<QTableWidget>(dialog, "packageEntries");
    table->setRowCount(1);
    table->setItem(0, 0, new QTableWidgetItem(source));
    table->setItem(0, 1, new QTableWidgetItem("Misc/Hello.BIN"));
    widget<QLineEdit>(dialog, "packageOutput")->setText(output);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(widget<QPushButton>(dialog, "packageCreate")->isEnabled(), "valid archive review enables creation");
    check(!QFile::exists(output), "prepare does not publish output");
    check(widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().contains("misc\\hello.bin"),
        "archive review exposes canonical physical destination");
    widget<QPushButton>(dialog, "packageCreate")->click();
    ready(dialog);
    check(QFile::exists(output), "Create publishes a reviewed new archive");
    if ( QFile::exists(output) )
    {
        const auto bytes = read(output);
        const auto* start = reinterpret_cast<const std::byte*>(bytes.constData());
        const auto gob = sed::assets::GobArchive::fromBytes({start, start + bytes.size()});
        const auto entry = gob.read("misc/hello.bin");
        check(QByteArray(reinterpret_cast<const char*>(entry.data()), static_cast<qsizetype>(entry.size())) == payload,
            "GUI archive retains captured binary payload exactly");
        const auto before = bytes;
        widget<QPushButton>(dialog, "packageCreate")->click();
        ready(dialog);
        check(read(output) == before, "existing destination remains byte-identical");
    }
    check(read(source) == payload, "archive authoring preserves source file");
}
void packageWorkflow()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("source.jkl");
    const QString output = directory.filePath("package.goo");
    const QByteArray level("SECTION: MATERIALS\nWorld Materials 0\nEND\n");
    write(source, level);
    Dialog dialog({directory.path(), [] { return true; }, {}});
    if ( !controls(dialog) )
    {
        return;
    }
    widget<QTabWidget>(dialog, "packageMode")->setCurrentIndex(1);
    widget<QLineEdit>(dialog, "packageLevel")->setText(source);
    widget<QLineEdit>(dialog, "packageLevelName")->setText("Level.JKL");
    widget<QLineEdit>(dialog, "packageEpisodeTitle")->setText("Owned episode");
    widget<QComboBox>(dialog, "packageFormat")->setCurrentIndex(1);
    widget<QLineEdit>(dialog, "packageOutput")->setText(output);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(widget<QPushButton>(dialog, "packageCreate")->isEnabled(), "valid explicit JKL plan enables package creation");
    const QString report = widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText();
    check(report.contains("Manifest SHA-256:") && report.contains("Runtime closure verified: no"),
        "package review reports binding hash and honest runtime boundary");
    widget<QPushButton>(dialog, "packageCreate")->click();
    ready(dialog);
    check(QFile::exists(output), "GUI package creation publishes selected GOO output");
    if ( QFile::exists(output) )
    {
        sed::level::PackageRequest request;
        request.inputBytes = level.toStdString();
        request.levelFileName = "Level.JKL";
        request.episodeTitle = "Owned episode";
        request.archiveExtension = ".goo";
        const auto expected = sed::level::planLevelPackage(request);
        check(read(output) == QByteArray::fromStdString(expected.outputData),
            "GUI output is byte-identical to the shared package planner");
    }
}
void setupArchive(Dialog& dialog, const QString& source, const QString& output)
{
    auto* table = widget<QTableWidget>(dialog, "packageEntries");
    table->setRowCount(1);
    table->setItem(0, 0, new QTableWidgetItem(source));
    table->setItem(0, 1, new QTableWidgetItem("misc/input.bin"));
    widget<QLineEdit>(dialog, "packageOutput")->setText(output);
}
void refusalAndLifetimeBoundaries()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("source.bin");
    write(source, "before");
    {
        Dialog dialog({});
        setupArchive(dialog, source, directory.filePath("stale.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        write(source, "after!");
        widget<QPushButton>(dialog, "packageCreate")->click();
        ready(dialog);
        check(!QFile::exists(directory.filePath("stale.gob"))
                && widget<QLabel>(dialog, "packageStatus")->text().contains("changed"),
            "changed source refuses reviewed archive publication");
        check(!widget<QPushButton>(dialog, "packageCreate")->isEnabled(),
            "source failure requires a fresh review");
    }
    for ( const bool unknown : {false, true} )
    {
        Dialog dialog({{}, [unknown]() -> bool
        {
            if ( unknown )
            {
                throw 7;
            }
            throw std::runtime_error("specific owner failure");
        }, {}});
        setupArchive(dialog, source, directory.filePath("owner.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        check(widget<QPushButton>(dialog, "packagePrepare")->isEnabled()
                && !widget<QPushButton>(dialog, "packageCreate")->isEnabled()
                && widget<QLabel>(dialog, "packageStatus")->text().contains(
                    unknown ? "unexpected" : "specific owner failure"),
            "owner exception is contained and remains actionable at command entry");
    }
    {
        bool current = true;
        Dialog dialog({{}, [&] { return current; }, {}});
        setupArchive(dialog, source, directory.filePath("context.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        current = false;
        ready(dialog);
        check(widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().isEmpty()
                && !widget<QPushButton>(dialog, "packageCreate")->isEnabled(),
            "changed owner context discards a completed worker report");
    }
    {
        int calls{};
        Dialog dialog({{}, [&]() -> bool
        {
            if ( ++calls > 1 )
            {
                throw std::runtime_error("completion owner failure");
            }
            return true;
        }, {}});
        setupArchive(dialog, source, directory.filePath("completion.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        check(widget<QLabel>(dialog, "packageStatus")->text().contains("completion owner failure")
                && widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().isEmpty(),
            "completion callback failure publishes no report and restores controls");
    }
    {
        Dialog dialog({});
        setupArchive(dialog, source, directory.filePath("cancel.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        widget<QPushButton>(dialog, "packageCancel")->click();
        ready(dialog);
        check(widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().isEmpty()
                && !QFile::exists(directory.filePath("cancel.gob")),
            "cancelled preparation publishes neither report nor archive");
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        widget<QPushButton>(dialog, "packageCreate")->click();
        widget<QPushButton>(dialog, "packageCancel")->click();
        ready(dialog);
        check(!QFile::exists(directory.filePath("cancel.gob")),
            "cancelled staging never creates final output");
    }
    {
        Dialog dialog({{}, {}, [](const QString&) { throw std::runtime_error("notification failed"); }});
        const QString output = directory.filePath("notified.gob");
        setupArchive(dialog, source, output);
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        widget<QPushButton>(dialog, "packageCreate")->click();
        ready(dialog);
        check(QFile::exists(output) && widget<QLabel>(dialog, "packageStatus")->text().contains("Created")
                && widget<QLabel>(dialog, "packageStatus")->text().contains("notification failed"),
            "publication notification failure retains truthful Created result");
    }
    {
        QPointer<Dialog> dialog = new Dialog({});
        setupArchive(*dialog, source, directory.filePath("destroyed.gob"));
        widget<QPushButton>(*dialog, "packagePrepare")->click();
        delete dialog.data();
        sed::ui::detail::previewWorkerPool().waitForDone();
        QApplication::processEvents();
        check(!QFile::exists(directory.filePath("destroyed.gob")), "destroyed dialog cannot publish worker output");
    }
    {
        Dialog dialog({});
        const QString output = directory.filePath("closed.gob");
        setupArchive(dialog, source, output);
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        widget<QPushButton>(dialog, "packageCreate")->click();
        dialog.reject();
        sed::ui::detail::previewWorkerPool().waitForDone();
        QApplication::processEvents();
        check(!QFile::exists(output), "closing a staging dialog prevents final publication");
    }
    {
        Dialog dialog({});
        const QString original = directory.filePath("old-target.gob");
        const QString changed = directory.filePath("new-target.gob");
        setupArchive(dialog, source, original);
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        widget<QPushButton>(dialog, "packageCreate")->click();
        widget<QLineEdit>(dialog, "packageOutput")->setText(changed);
        ready(dialog);
        check(!QFile::exists(original) && !QFile::exists(changed),
            "destination edit during staging cancels publication to either path");
    }
    {
        Dialog dialog({});
        setupArchive(dialog, source, directory.filePath("edited.gob"));
        widget<QPushButton>(dialog, "packagePrepare")->click();
        ready(dialog);
        widget<QTableWidget>(dialog, "packageEntries")->item(0, 1)->setText("misc/new.bin");
        check(!widget<QPushButton>(dialog, "packageCreate")->isEnabled()
                && widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().isEmpty(),
            "changed logical destination invalidates the reviewed candidate");
        dialog.resize(640, 480);
        dialog.show();
        QApplication::processEvents();
        check(dialog.width() <= 640 && dialog.height() <= 480,
            "package form remains usable within a compact window");
    }
}
void packageReportAndStockBoundaries()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("source.jkl");
    write(source, "SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n");
    const QString archive = directory.filePath("stock.gob");
    const std::string gob = sed::assets::buildGobBytes({{"mat/wall.mat", "owned stock material"}});
    write(archive, QByteArray::fromStdString(gob));
    Dialog dialog({});
    widget<QTabWidget>(dialog, "packageMode")->setCurrentIndex(1);
    widget<QLineEdit>(dialog, "packageLevel")->setText(source);
    widget<QLineEdit>(dialog, "packageEpisodeTitle")->setText("Reviewed stock episode");
    widget<QSpinBox>(dialog, "packageMaxNodes")->setValue(2);
    auto* table = widget<QTableWidget>(dialog, "packageArchives");
    table->setRowCount(1);
    table->setItem(0, 0, new QTableWidgetItem(archive));
    auto* role = new QTableWidgetItem;
    role->setCheckState(Qt::Checked);
    table->setItem(0, 1, role);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(!widget<QPushButton>(dialog, "packageCreate")->isEnabled()
            && widget<QPushButton>(dialog, "packageExportReport")->isEnabled()
            && widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().contains("PackageBudgetExceeded"),
        "blocked node budget remains fully reviewable and reportable");
    const QString reportPath = directory.filePath("blocked.txt");
    widget<QLineEdit>(dialog, "packageReportPath")->setText(reportPath);
    const QString report = widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText();
    widget<QPushButton>(dialog, "packageExportReport")->click();
    ready(dialog);
    check(QFile::exists(reportPath) && read(reportPath) == report.toUtf8(),
        "report export contains the complete captured report");
    widget<QSpinBox>(dialog, "packageMaxNodes")->setValue(3);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    const QString stockReport = widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText();
    check(widget<QPushButton>(dialog, "packageCreate")->isEnabled() && stockReport.contains("Stock; omitted"),
        "stock omission remains a traversed graph node at its exact node budget");
    check(stockReport.contains("Reviewed stock episode") && stockReport.contains("Maximum nodes: 3")
            && stockReport.contains("Stock archive:"),
        "portable review records episode/options and ordered archive roles");
}
void packageOrderingAndExtraRoots()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("source.jkl");
    const QByteArray level("SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n");
    write(source, level);
    const QString stockPath = directory.filePath("stock.gob");
    const QString resourcePath = directory.filePath("custom.gob");
    const std::vector<std::pair<std::string, std::string>> stock{{"mat/wall.mat", "stock"}};
    const std::vector<std::pair<std::string, std::string>> custom{
        {"mat/wall.mat", "custom"}, {"misc/extra.bin", "extra"}};
    write(stockPath, QByteArray::fromStdString(sed::assets::buildGobBytes(stock)));
    write(resourcePath, QByteArray::fromStdString(sed::assets::buildGobBytes(custom)));
    Dialog dialog({});
    widget<QTabWidget>(dialog, "packageMode")->setCurrentIndex(1);
    widget<QLineEdit>(dialog, "packageLevel")->setText(source);
    auto* archives = widget<QTableWidget>(dialog, "packageArchives");
    archives->setRowCount(2);
    archives->setItem(0, 0, new QTableWidgetItem(stockPath));
    auto* stockRole = new QTableWidgetItem;
    stockRole->setCheckState(Qt::Checked);
    archives->setItem(0, 1, stockRole);
    archives->setItem(1, 0, new QTableWidgetItem(resourcePath));
    auto* resourceRole = new QTableWidgetItem;
    resourceRole->setCheckState(Qt::Unchecked);
    archives->setItem(1, 1, resourceRole);
    auto* extras = widget<QTableWidget>(dialog, "packageExtras");
    extras->setRowCount(1);
    extras->setItem(0, 0, new QTableWidgetItem("extra.bin"));
    extras->setItem(0, 1, new QTableWidgetItem("misc"));
    const QString output = directory.filePath("ordered.gob");
    widget<QLineEdit>(dialog, "packageOutput")->setText(output);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(widget<QPushButton>(dialog, "packageCreate")->isEnabled(),
        "ordinary extra reference and ordered role controls produce a package plan");
    widget<QPushButton>(dialog, "packageCreate")->click();
    ready(dialog);
    sed::level::PackageRequest expected;
    expected.inputBytes = level.toStdString();
    expected.levelFileName = "level.jkl";
    expected.episodeTitle = "New episode";
    expected.archives = {{"custom.gob", false, custom}, {"stock.gob", true, stock}};
    expected.extraResources = {{"extra.bin", "misc"}};
    const auto plan = sed::level::planLevelPackage(expected);
    check(QFile::exists(output) && read(output) == QByteArray::fromStdString(plan.outputData),
        "GUI places resource archives before stock and retains exact extra-root planner bytes");
    extras->item(0, 0)->setText("missing.bin");
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(!widget<QPushButton>(dialog, "packageCreate")->isEnabled(),
        "missing extra reference blocks publication by default");
    widget<QCheckBox>(dialog, "packageAllowMissing")->setChecked(true);
    widget<QPushButton>(dialog, "packagePrepare")->click();
    ready(dialog);
    check(widget<QPushButton>(dialog, "packageCreate")->isEnabled()
            && widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().contains("missing.bin")
            && widget<QPlainTextEdit>(dialog, "packageReport")->toPlainText().contains("Runtime closure verified: no"),
        "explicit unverified-reference option retains missing evidence and runtime limit");
}
void ownerReentrancyAndDestruction()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("source.bin");
    write(source, "input");
    QPointer<Dialog> owner;
    int calls{};
    owner = new Dialog({{}, [&]
    {
        ++calls;
        if ( calls == 1 )
        {
            widget<QPushButton>(*owner, "packagePrepare")->click();
        }
        return true;
    }, {}});
    setupArchive(*owner, source, directory.filePath("recursive.gob"));
    widget<QPushButton>(*owner, "packagePrepare")->click();
    ready(*owner);
    check(calls == 2 && widget<QPushButton>(*owner, "packageCreate")->isEnabled(),
        "owner callback cannot recursively admit a second preparation");
    delete owner.data();
    owner = new Dialog({{}, [&]
    {
        delete owner.data();
        return true;
    }, {}});
    setupArchive(*owner, source, directory.filePath("deleted-by-owner.gob"));
    widget<QPushButton>(*owner, "packagePrepare")->click();
    check(!owner && !QFile::exists(directory.filePath("deleted-by-owner.gob")),
        "synchronous owner deletion prevents worker dispatch and publication");
}
}
int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    archiveWorkflow();
    packageWorkflow();
    refusalAndLifetimeBoundaries();
    packageReportAndStockBoundaries();
    packageOrderingAndExtraRoots();
    ownerReentrancyAndDestruction();
    std::cout << "Package authoring failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
