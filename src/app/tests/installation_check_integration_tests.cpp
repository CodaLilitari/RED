#include "app_settings.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>

int runInstallationCheckIntegrationTests()
{
    int failures = 0;
    const auto check = [&failures](const bool condition, const char* message)
    {
        if ( !condition )
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };
    QTemporaryDir temporary;
    QFile ini(temporary.filePath("red.ini"));
    check(temporary.isValid() && ini.open(QIODevice::WriteOnly), "installation GUI fixture owns settings");
    ini.close();
    const auto previous = sed::appSettingsIniPath();
    sed::setAppSettingsIniPathOverrideForTest(std::filesystem::path(ini.fileName().toStdWString()));
    const QByteArray oldJk = qgetenv("SED_JKDF2_DIR");
    const QByteArray oldMots = qgetenv("SED_MOTS_DIR");
    qputenv("SED_JKDF2_DIR", temporary.path().toUtf8());
    qputenv("SED_MOTS_DIR", temporary.path().toUtf8());
    if ( sed::isolatedAppStorageActive() )
    {
        sed::MainWindow diagnostic(nullptr, true, sed::MainWindow::StartupPurpose::installationCheck);
        diagnostic.setAttribute(Qt::WA_DontShowOnScreen);
        check(diagnostic.installationCheckViewport() != nullptr, "explicit diagnostic purpose exposes its owned viewport");
        check(diagnostic.locatorResourceRootsForTest().empty() && diagnostic.locatorResourceGobsForTest().empty(),
            "installation startup does not mount automatic resource roots or game archives");
        check(diagnostic.setInstallationCheckFrame(0), "owned diagnostic admits first synthetic frame");
        const auto first = sed::assets::saveToSedBytes(diagnostic.documentStateForTest().level);
        check(diagnostic.setInstallationCheckFrame(1)
            && sed::assets::saveToSedBytes(diagnostic.documentStateForTest().level) != first,
            "second synthetic frame changes the real document geometry");
        const auto second = sed::assets::saveToSedBytes(diagnostic.documentStateForTest().level);
        check(!diagnostic.setInstallationCheckFrame(2)
            && sed::assets::saveToSedBytes(diagnostic.documentStateForTest().level) == second,
            "unknown diagnostic frame is failure-atomic");
    }
    {
        sed::MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setRecoveryDirectoryForTest(std::filesystem::path(temporary.filePath("recovery").toStdWString()));
        const auto level = sed::assets::saveToSedBytes(window.documentStateForTest().level);
        const auto undo = window.undoDepthForTest();
        const bool dirty = window.hasUnsavedChangesForTest();
        check(!window.setInstallationCheckFrame(0), "normal document cannot be replaced by diagnostic geometry");
        auto* action = window.findChild<QAction*>("help.check_installation");
        check(action && action->isEnabled(), "Help exposes the ordinary installation check");
        if ( action )
        {
            bool visited = false;
            QTimer::singleShot(0, &window, [&]()
            {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(dialog && dialog->findChild<QPushButton*>("installationRun")
                    && dialog->findChild<QPushButton*>("installationCancel")
                    && dialog->findChild<QPushButton*>("installationSave"),
                    "Help opens the real bounded diagnostic controls");
                if ( dialog )
                {
                    visited = true;
                    dialog->reject();
                }
            });
            action->trigger();
            check(visited, "installation dialog responds through the real menu");
        }
        check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == level
            && window.undoDepthForTest() == undo && window.hasUnsavedChangesForTest() == dirty,
            "opening and closing installation diagnostics preserves authored level and history");
    }
    qputenv("SED_JKDF2_DIR", oldJk);
    qputenv("SED_MOTS_DIR", oldMots);
    sed::setAppSettingsIniPathOverrideForTest(previous);
    std::cout << "Installation integration failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}

#ifdef SED_INSTALLATION_INTEGRATION_STANDALONE
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QTemporaryDir storage;
    QString error;
    if ( !sed::initializeIsolatedAppStorage(
            std::filesystem::path(storage.filePath("isolated").toStdWString()), &error) )
    {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    return runInstallationCheckIntegrationTests();
}
#endif
