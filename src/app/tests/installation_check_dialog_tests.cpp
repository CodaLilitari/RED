#include "ui/diagnostics/installation_check_dialog.hpp"
#include "../../core/src/assets/atomic_file_write.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace
{
int failures{};
void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const QString fontFile = qEnvironmentVariable("SED_TEST_FONT_FILE");
    if ( !fontFile.isEmpty() )
    {
        QFontDatabase::addApplicationFont(fontFile);
    }
    using Dialog = sed::ui::diagnostics::InstallationCheckDialog;
    QTemporaryDir temporary(QDir::tempPath()
        + QStringLiteral("/red-installation-\u00e9-\u0394-XXXXXX"));
    if ( application.arguments().contains("--chooser-destroys-owner")
        || application.arguments().contains("--chooser-closes-owner") )
    {
        const QString output = temporary.filePath("must-not-exist.json");
        QPointer<Dialog> victim;
        Dialog::Context destruction;
        destruction.executable = temporary.filePath("missing-executable");
        destruction.reportParentDirectory = temporary.path();
        const bool closeOwner = application.arguments().contains("--chooser-closes-owner");
        destruction.selectJsonReportDestination = [&victim, &output, closeOwner](QWidget*)
        {
            if ( closeOwner )
            {
                victim->reject();
            }
            else
            {
                delete victim.data();
            }
            return output;
        };
        victim = new Dialog(destruction);
        victim->findChild<QPushButton*>("installationRun")->click();
        victim->findChild<QPushButton*>("installationSave")->click();
        check((closeOwner || !victim) && !QFile::exists(output), "closing or destroying the report chooser owner prevents publication");
        delete victim.data();
        return failures == 0 ? 0 : 1;
    }
    QImage before(160, 120, QImage::Format_ARGB32);
    QImage after(160, 120, QImage::Format_ARGB32);
    before.fill(Qt::black);
    after.fill(Qt::black);
    {
        QPainter first(&before);
        first.fillRect(QRect(25, 45, 31, 31), Qt::white);
        QPainter second(&after);
        second.fillRect(QRect(105, 45, 31, 31), Qt::white);
    }
    check(before.save(temporary.filePath("before.png")) && after.save(temporary.filePath("after.png")),
        "synthetic diagnostic frames saved");
    QString destination = temporary.filePath(QStringLiteral("saved-report-\u00e9-\u0394.json"));
    Dialog::Context context;
    context.executable = QCoreApplication::applicationDirPath() + "/installation_process_fixture"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
    context.reportParentDirectory = temporary.path();
    bool restartDuringChooser = false;
    context.selectJsonReportDestination = [&destination, &restartDuringChooser](QWidget* owner)
    {
        if ( restartDuringChooser )
        {
            owner->findChild<QPushButton*>("installationRun")->click();
        }
        return destination;
    };
    Dialog dialog(context);
    dialog.resize(640, 480);
    dialog.show();
    QApplication::processEvents();
    auto* run = dialog.findChild<QPushButton*>("installationRun");
    auto* cancel = dialog.findChild<QPushButton*>("installationCancel");
    auto* save = dialog.findChild<QPushButton*>("installationSave");
    auto* status = dialog.findChild<QPlainTextEdit*>("installationStatus");
    auto* preview = dialog.findChild<QLabel*>("installationPreview");
    check(run && cancel && save && status && preview, "ordinary Run/Cancel/Save/status/frame controls are present");
    if ( run && cancel && save && status && preview )
    {
        check(run->isEnabled() && !cancel->isEnabled() && !save->isEnabled(), "idle controls reflect no prior result");
        check(dialog.minimumSizeHint().height() <= 500 && dialog.rect().contains(save->mapTo(&dialog, save->rect().bottomRight())),
            "small-screen dialog keeps actions within its visible bounds");
        run->click();
        check(!run->isEnabled() && cancel->isEnabled(), "owned diagnostic exposes active cancellation");
        QElapsedTimer elapsed;
        elapsed.start();
        while ( !save->isEnabled() && elapsed.elapsed() < 5000 )
        {
            QApplication::processEvents();
            QThread::msleep(2);
        }
        check(save->isEnabled() && run->isEnabled() && !cancel->isEnabled(), "completed diagnostic restores usable controls");
        check(status->toPlainText().contains("passed", Qt::CaseInsensitive), "human report states the actual terminal outcome");
        check(!preview->pixmap().isNull(), "human report shows the inspected synthetic frame");
        save->click();
        QFile saved(destination);
        check(saved.open(QIODevice::ReadOnly), "Save JSON Report publishes the selected destination");
        const QByteArray original = saved.readAll();
        saved.close();
        check(QJsonDocument::fromJson(original).object().value("status") == "passed", "saved report preserves actual result");
        save->click();
        check(saved.open(QIODevice::ReadOnly) && saved.readAll() == original, "saving cannot overwrite an existing report");
        saved.close();
        destination = temporary.filePath("failed-report.json");
        {
            sed::assets::detail::ScopedStagedAssetFileFaultForTests fault(
                sed::assets::detail::StagedAssetFileFaultPointForTests::flush);
            save->click();
            check(fault.triggered(), "report saving uses the shared failure-atomic writer");
            check(!QFile::exists(destination), "a failed report flush leaves no partial published report");
        }
        destination = temporary.filePath("snapshot-report.json");
        restartDuringChooser = true;
        save->click();
        QFile snapshot(destination);
        check(snapshot.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(snapshot.readAll()).object() == QJsonDocument::fromJson(original).object(),
            "a reentrant new run cannot replace the result selected for export");
    }
    dialog.close();
    std::cout << "installation dialog failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
