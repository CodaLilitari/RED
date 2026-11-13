#include "ui/project/engine_capture_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace
{
using Dialog = sed::ui::project::EngineCaptureDialog;
int failures{};

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
template <class T> T* control(Dialog& dialog, const char* name)
{
    auto* found = dialog.findChild<T*>(QString::fromLatin1(name));
    check(found != nullptr, name);
    return found;
}
void controls()
{
    Dialog dialog({});
    control<QLineEdit>(dialog, "capturePython");
    control<QLineEdit>(dialog, "captureRunner");
    control<QComboBox>(dialog, "captureProfile");
    control<QComboBox>(dialog, "captureBackend");
    control<QLineEdit>(dialog, "captureEpisode");
    control<QLineEdit>(dialog, "captureMap");
    control<QSpinBox>(dialog, "captureWidth");
    control<QSpinBox>(dialog, "captureHeight");
    control<QDoubleSpinBox>(dialog, "captureX");
    control<QDoubleSpinBox>(dialog, "captureYaw");
    control<QTableWidget>(dialog, "captureFiles");
    control<QPushButton>(dialog, "capturePrepare");
    control<QPushButton>(dialog, "captureRun");
    control<QPushButton>(dialog, "captureCancel");
    control<QPushButton>(dialog, "captureImport");
    control<QPushButton>(dialog, "captureExport");
    auto* warning = control<QLabel>(dialog, "captureWarning");
    if ( warning )
    {
        check(warning->text().contains("unverified") && warning->text().contains("retail"),
            "capture and game acceptance stay distinct");
    }
}

void compactLayout()
{
    for ( const QSize size : { QSize(1200, 660), QSize(1200, 708), QSize(1000, 536) } )
    {
        Dialog dialog({});
        dialog.ensurePolished();
        dialog.resize(size);
        dialog.layout()->activate();
        QPixmap initialLayout(dialog.size());
        dialog.render(&initialLayout);
        check(dialog.width() <= size.width() && dialog.height() <= size.height(),
            "capture controls fit 720p, 768p and scaled desktop client areas");
        for ( const char* name : { "capturePrepare", "captureRun", "captureCancel", "captureImport", "captureExport" } )
        {
            auto* action = control<QPushButton>(dialog, name);
            if ( action )
            {
                check(dialog.rect().contains(QRect(action->mapTo(&dialog, QPoint()), action->size())),
                    "capture actions remain inside compact dialog bounds");
            }
        }
        auto* area = control<QScrollArea>(dialog, "captureRequestScroll");
        if ( area )
        {
            check(area->verticalScrollBar()->maximum() > 0, "compact request form can scroll");
            auto* lastInput = control<QCheckBox>(dialog, "captureMots");
            area->ensureWidgetVisible(lastInput);
            const QRect lastRect(lastInput->mapTo(area->viewport(), QPoint()), lastInput->size());
            if ( !area->viewport()->rect().contains(lastRect) )
            {
                std::cerr << "Scroll viewport: " << area->viewport()->width() << 'x' << area->viewport()->height()
                          << "; last option: " << lastRect.x() << ',' << lastRect.y() << ',' << lastRect.width()
                          << ',' << lastRect.height() << '\n';
            }
            check(area->viewport()->rect().contains(lastRect),
                "last request option remains reachable by scrolling");
        }
    }
}

bool idle(Dialog& dialog)
{
    auto* prepare = control<QPushButton>(dialog, "capturePrepare");
    if ( !prepare )
    {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while ( !prepare->isEnabled() && timer.elapsed() < 10000 )
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(prepare->isEnabled(), "capture GUI reaches idle within deadline");
    return prepare->isEnabled();
}
void write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "open synthetic file");
    check(file.write(data) == data.size(), "complete synthetic write");
}
QByteArray read(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read generated file");
    return file.readAll();
}
Dialog::Context context()
{
    return { .pythonExecutable = qEnvironmentVariable("SED_CAPTURE_TEST_PYTHON"),
        .runnerScript = qEnvironmentVariable("SED_CAPTURE_TEST_RUNNER") };
}
bool configure(Dialog& dialog, const QString& root)
{
    auto* profile = control<QComboBox>(dialog, "captureProfile");
    auto* episode = control<QLineEdit>(dialog, "captureEpisode");
    auto* map = control<QLineEdit>(dialog, "captureMap");
    auto* width = control<QSpinBox>(dialog, "captureWidth");
    auto* height = control<QSpinBox>(dialog, "captureHeight");
    auto* required = control<QPushButton>(dialog, "captureAddRequired");
    auto* table = control<QTableWidget>(dialog, "captureFiles");
    if ( !profile || !episode || !map || !width || !height || !required || !table )
    {
        return false;
    }
#ifdef Q_OS_WIN
    profile->setCurrentIndex(profile->findData("windows"));
#else
    profile->setCurrentIndex(profile->findData("linux"));
#endif
    episode->setText("test");
    map->setText("level.jkl");
    width->setValue(64);
    height->setValue(64);
    required->click();
    check(table->rowCount() >= 5, "required profile/config/scenario rows are provided");
    for ( int row = 0; row < table->rowCount(); ++row )
    {
        const QString source = root + QString("/source-%1.bin").arg(row);
        write(source, "synthetic " + table->item(row, 1)->text().toUtf8());
        table->item(row, 0)->setText(source);
    }
    return true;
}
void requestRoundTripAndInvalidation()
{
    QTemporaryDir root;
    Dialog dialog(context());
    if ( !configure(dialog, root.path()) )
    {
        return;
    }
    auto* prepare = control<QPushButton>(dialog, "capturePrepare");
    auto* exportButton = control<QPushButton>(dialog, "captureExport");
    auto* path = control<QLineEdit>(dialog, "captureRequestPath");
    if ( !prepare || !exportButton || !path )
    {
        return;
    }
    prepare->click();
    if ( !idle(dialog) )
    {
        return;
    }
    check(exportButton->isEnabled(), "authoritative runner validation enables export");
    const QString request = root.path() + "/new-request.json";
    path->setText(request);
    exportButton->click();
    if ( !QFile::exists(request) )
    {
        check(false, "request export created a file");
        return;
    }
    const QByteArray original = read(request);
    const auto json = QJsonDocument::fromJson(original).object();
    check(json["schemaVersion"].toInt() == 1 && json["episode"] == "test" && json["map"] == "level.jkl",
        "typed request uses unchanged capture schema");
    check(json["camera"].toObject()["width"].toInt() == 64 && json["camera"].toObject()["height"].toInt() == 64,
        "camera controls reach the request");
    check(!json.contains("engineProfile") && json["files"].toArray()[0].toObject()["sha256"].toString().size() == 64,
        "serialized request binds actual source bytes without internal normalized keys");
    exportButton->click();
    check(read(request) == original, "request export is create-only");
    control<QDoubleSpinBox>(dialog, "captureX")->setValue(0.25);
    check(!exportButton->isEnabled() && !control<QPushButton>(dialog, "captureRun")->isEnabled(),
        "typed edits invalidate prepared snapshots immediately");
    const QString bad = root.path() + "/unknown.json";
    auto badJson = json;
    badJson["unrecognizedField"] = true;
    write(bad, QJsonDocument(badJson).toJson());
    path->setText(bad);
    control<QPushButton>(dialog, "captureImport")->click();
    idle(dialog);
    check(control<QLabel>(dialog, "captureStatus")->text().contains("unsupported"),
        "import preserves authoritative unknown-field refusal instead of dropping data");
    auto precise = json;
    auto camera = precise["camera"].toObject();
    camera["position"] = QJsonArray{ 0.1234567891234567, 0, 0 };
    precise["camera"] = camera;
    const QString precisePath = root.path() + "/precise.json";
    write(precisePath, QJsonDocument(precise).toJson());
    path->setText(precisePath);
    control<QPushButton>(dialog, "captureImport")->click();
    idle(dialog);
    check(control<QDoubleSpinBox>(dialog, "captureX")->value() == 0.1234567891234567,
        "imported camera precision remains visible in the typed control");
}
void changedSourceCannotLaunch()
{
    QTemporaryDir root;
    Dialog dialog(context());
    if ( !configure(dialog, root.path()) )
    {
        return;
    }
    auto* prepare = control<QPushButton>(dialog, "capturePrepare");
    auto* run = control<QPushButton>(dialog, "captureRun");
    auto* output = control<QLineEdit>(dialog, "captureOutput");
    if ( !prepare || !run || !output )
    {
        return;
    }
    prepare->click();
    idle(dialog);
#ifdef Q_OS_MACOS
    check(!run->isEnabled(), "macOS cannot execute either engine capture profile");
#else
    check(run->isEnabled(), "supported host can run prepared capture");
    write(root.path() + "/source-0.bin", "changed after prepare");
    const QString destination = root.path() + "/capture-output";
    output->setText(destination);
    run->click();
    idle(dialog);
    const QString receiptPath = destination + "/launcher-receipt.json";
    if ( !QFile::exists(receiptPath) )
    {
        check(false, "runner failure receipt exists");
        return;
    }
    const auto receipt = QJsonDocument::fromJson(read(receiptPath)).object();
    check(receipt["status"] == "failed" && !receipt["engineStarted"].toBool() &&
              receipt["error"].toString().contains("Source hash mismatch"),
        "Run uses prepared hashes and refuses a changed source before engine launch");
    check(control<QPlainTextEdit>(dialog, "captureReceipt")->toPlainText().contains("Source hash mismatch"),
        "failure receipt is visible to a human");
#endif
}
void missingToolsAndReceiptInspection()
{
    QTemporaryDir root;
    Dialog dialog(
        { .pythonExecutable = root.path() + "/absent-python", .runnerScript = root.path() + "/absent-runner" });
    auto* prepare = control<QPushButton>(dialog, "capturePrepare");
    auto* status = control<QLabel>(dialog, "captureStatus");
    auto* receiptPath = control<QLineEdit>(dialog, "captureReceiptPath");
    auto* inspect = control<QPushButton>(dialog, "captureInspect");
    if ( !prepare || !status || !receiptPath || !inspect )
    {
        return;
    }
    prepare->click();
    idle(dialog);
    check(status->text().contains("Python"), "missing Python is reported without dependency installation");
    const QString path = root.path() + "/receipt.json";
    write(path, R"({"status":"failed","acceptance":"unverified","error":"Synthetic failure","engineStarted":false})");
    receiptPath->setText(path);
    inspect->click();
    check(control<QPlainTextEdit>(dialog, "captureReceipt")->toPlainText().contains("Synthetic failure") &&
              status->text().contains("unverified"),
        "receipt inspection remains available without Python and preserves acceptance limits");
}

void existingOutputCannotMasqueradeAsNewCapture()
{
#ifndef Q_OS_MACOS
    QTemporaryDir root;
    Dialog dialog(context());
    if ( !configure(dialog, root.path()) )
    {
        return;
    }
    control<QPushButton>(dialog, "capturePrepare")->click();
    idle(dialog);
    const QString destination = root.path() + "/existing";
    QDir().mkpath(destination);
    const QString receipt = destination + "/launcher-receipt.json";
    const QByteArray original =
        R"({"status":"completed","acceptance":"unverified","oldEvidence":"OLD CAPTURE MUST NOT BE REPLAYED"})";
    write(receipt, original);
    control<QLineEdit>(dialog, "captureOutput")->setText(destination);
    control<QPushButton>(dialog, "captureRun")->click();
    idle(dialog);
    check(read(receipt) == original, "existing capture evidence remains byte identical");
    check(!control<QPlainTextEdit>(dialog, "captureReceipt")
                  ->toPlainText()
                  .contains("OLD CAPTURE MUST NOT BE REPLAYED") &&
              control<QLabel>(dialog, "captureStatus")->text().contains("already exists"),
        "refused run does not display an old receipt as its own result");
#endif
}

bool waitPath(const QString& path)
{
    QElapsedTimer timer;
    timer.start();
    while ( !QFile::exists(path) && timer.elapsed() < 6000 )
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(QFile::exists(path), "owned runner checkpoint or receipt arrives within deadline");
    return QFile::exists(path);
}
void cancellationAndOwnerLoss()
{
#ifndef Q_OS_MACOS
    for ( bool destroy : { false, true } )
    {
        QTemporaryDir root;
        auto config = context();
        QByteArray quoted =
            QJsonDocument(QJsonArray{ QFileInfo(config.runnerScript).absolutePath() }).toJson(QJsonDocument::Compact);
        quoted = quoted.mid(1, quoted.size() - 2);
        const QString harness = root.path() + "/capture-harness.py";
        write(harness,
            "import sys,time\nfrom pathlib import Path\nsys.path.insert(0," + quoted +
                ")\nimport invoke_engine_capture as runner\n"
                "def stalled(request,output):\n Path(output,'entered').write_text('staging')\n time.sleep(20)\n return "
                "[]\n"
                "runner.stage_files=stalled\nrunner._worker_command=lambda "
                "request,output:[sys.executable,__file__,str(request),'--output',str(output),'--capture-worker']\n"
                "raise SystemExit(runner.main())\n");
        config.runnerScript = harness;
        auto dialog = std::make_unique<Dialog>(config);
        if ( !configure(*dialog, root.path()) )
        {
            return;
        }
        control<QSpinBox>(*dialog, "captureRunnerTimeout")->setValue(90000);
        control<QPushButton>(*dialog, "capturePrepare")->click();
        idle(*dialog);
        const QString output = root.path() + "/owned";
        control<QLineEdit>(*dialog, "captureOutput")->setText(output);
        control<QPushButton>(*dialog, "captureRun")->click();
        waitPath(output + "/entered");
        if ( destroy )
        {
            dialog.reset();
        }
        else
        {
            control<QPushButton>(*dialog, "captureCancel")->click();
        }
        const QString receipt = output + "/launcher-receipt.json";
        if ( waitPath(receipt) )
        {
            const auto result = QJsonDocument::fromJson(read(receipt)).object();
            check(result["cancelled"].toBool() && result["cleanupComplete"].toBool() && result["status"] == "failed" &&
                      result["inputsUnchanged"].isNull(),
                "Cancel/owner destruction delegates cleanup and an honest failed receipt to the runner");
        }
        if ( dialog )
        {
            idle(*dialog);
        }
    }
#endif
}

void receiptImageIdentityAndLimits()
{
    QTemporaryDir root;
    Dialog dialog({});
    auto* inspect = control<QPushButton>(dialog, "captureInspect");
    auto* path = control<QLineEdit>(dialog, "captureReceiptPath");
    auto* image = control<QLabel>(dialog, "captureImage");
    if ( !inspect || !path || !image )
    {
        return;
    }
    const QByteArray pixels = "P6\n64 64\n255\n" + QByteArray(64 * 64 * 3, '\x44');
    write(root.path() + "/capture.ppm", pixels);
    QJsonObject receipt{ { "status", "completed" }, { "acceptance", "unverified" }, { "validPpm", true },
        { "imageSha256", QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex()) } };
    const QString receiptPath = root.path() + "/launcher-receipt.json";
    write(receiptPath, QJsonDocument(receipt).toJson());
    path->setText(receiptPath);
    inspect->click();
    check(!image->pixmap().isNull(), "bounded matching receipt image is displayed");
    auto changed = pixels;
    changed[changed.size() - 1] = '\x55';
    write(root.path() + "/capture.ppm", changed);
    inspect->click();
    check(image->pixmap().isNull() && control<QLabel>(dialog, "captureStatus")->text().contains("hash"),
        "changed image cannot be presented as the receipt's capture");
    write(receiptPath, QByteArray(8 * 1024 * 1024 + 1, 'x'));
    inspect->click();
    check(control<QLabel>(dialog, "captureStatus")->text().contains("limit") &&
              control<QPlainTextEdit>(dialog, "captureReceipt")->toPlainText().isEmpty(),
        "oversized receipt refuses without replaying previous contents");
}

void authoritativeInputLimits()
{
    QTemporaryDir root;
    Dialog dialog(context());
    if ( !configure(dialog, root.path()) )
    {
        return;
    }
    auto* prepare = control<QPushButton>(dialog, "capturePrepare");
    auto* status = control<QLabel>(dialog, "captureStatus");
    control<QSpinBox>(dialog, "captureWidth")->setValue(2048);
    control<QSpinBox>(dialog, "captureHeight")->setValue(2048);
    prepare->click();
    idle(dialog);
    check(status->text().contains("pixel budget") && !control<QPushButton>(dialog, "captureRun")->isEnabled(),
        "runner refuses combined camera pixel budget");
    control<QSpinBox>(dialog, "captureWidth")->setValue(64);
    control<QSpinBox>(dialog, "captureHeight")->setValue(64);
    control<QSpinBox>(dialog, "captureRunnerTimeout")->setValue(1000);
    prepare->click();
    idle(dialog);
    check(status->text().contains("cover engineTimeoutMs"), "runner validates total/engine budget relationship");
    control<QSpinBox>(dialog, "captureRunnerTimeout")->setValue(45000);
    control<QTableWidget>(dialog, "captureFiles")->item(1, 1)->setText("C:outside.txt");
    prepare->click();
    idle(dialog);
    check(status->text().contains("Unsafe file destination"), "GUI cannot bypass authoritative destination admission");
    const QString huge = root.path() + "/huge.json";
    write(huge, QByteArray(2 * 1024 * 1024 + 1, ' '));
    control<QLineEdit>(dialog, "captureRequestPath")->setText(huge);
    control<QPushButton>(dialog, "captureImport")->click();
    idle(dialog);
    check(status->text().contains("limit"), "GUI import enforces its bounded request read");
    auto* table = control<QTableWidget>(dialog, "captureFiles");
    auto* add = control<QPushButton>(dialog, "captureAddFile");
    for ( int row = table->rowCount(); row < 513; ++row )
    {
        add->click();
    }
    check(table->rowCount() == 512, "explicit input rows remain bounded");
    check(status->text().contains("512"), "row limit refusal remains visible");
}

void renderSnapshots(const QString& base)
{
    QTemporaryDir root;
    Dialog dialog(context());
    if ( !configure(dialog, root.path()) )
    {
        return;
    }
    control<QPushButton>(dialog, "capturePrepare")->click();
    idle(dialog);
    dialog.ensurePolished();
    dialog.layout()->activate();
    auto* tabs = control<QTabWidget>(dialog, "captureTabs");
    if ( !tabs )
    {
        return;
    }
    for ( int index = 0; index < 2; ++index )
    {
        tabs->setCurrentIndex(index);
        dialog.layout()->activate();
        QPixmap image(dialog.size());
        dialog.render(&image);
        check(image.save(base + QString("-%1.png").arg(index), "PNG"), "render synthetic capture controls");
    }
}
} // namespace
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const QString fontFile = qEnvironmentVariable("SED_TEST_FONT_FILE");
    if ( !fontFile.isEmpty() )
    {
        const int fontId = QFontDatabase::addApplicationFont(fontFile);
        check(fontId >= 0, "load explicit fixture font");
        if ( fontId >= 0 )
        {
            application.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 10));
        }
    }
    controls();
    compactLayout();
    requestRoundTripAndInvalidation();
    changedSourceCannotLaunch();
    missingToolsAndReceiptInspection();
    existingOutputCannotMasqueradeAsNewCapture();
    cancellationAndOwnerLoss();
    receiptImageIdentityAndLimits();
    authoritativeInputLimits();
    if ( argc == 3 && QString::fromLocal8Bit(argv[1]) == "--snapshot" )
    {
        renderSnapshots(QString::fromLocal8Bit(argv[2]));
    }
    std::cout << "Capture dialog failures: " << failures << '\n';
    return failures ? 1 : 0;
}
