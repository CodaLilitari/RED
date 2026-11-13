#include "app_settings.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "test_common.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

namespace sed::app_tests
{
namespace
{
std::filesystem::path testPath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::u8path(value.toUtf8().toStdString());
#endif
}
} // namespace

void runModelImportIntegrationTests()
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "model import menu fixture owns its temporary directory");
    if ( !temporary.isValid() )
    {
        return;
    }
    const auto previousSettings = appSettingsIniPath();
    const QByteArray oldJk = qgetenv("SED_JKDF2_DIR");
    const QByteArray oldMots = qgetenv("SED_MOTS_DIR");
    const auto restore = qScopeGuard([&]
    {
        setAppSettingsIniPathOverrideForTest(previousSettings);
        qputenv("SED_JKDF2_DIR", oldJk);
        qputenv("SED_MOTS_DIR", oldMots);
    });
    QFile settingsFile(temporary.filePath("red.ini"));
    const bool createdSettings = settingsFile.open(QIODevice::WriteOnly | QIODevice::NewOnly);
    check(createdSettings, "model import menu fixture precreates settings to prevent legacy migration");
    if ( !createdSettings )
    {
        return;
    }
    settingsFile.close();
    setAppSettingsIniPathOverrideForTest(testPath(settingsFile.fileName()));
    qputenv("SED_JKDF2_DIR", temporary.path().toUtf8());
    qputenv("SED_MOTS_DIR", temporary.path().toUtf8());
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(testPath(temporary.filePath("recovery")));
    const auto before = assets::saveToSedBytes(window.documentStateForTest().level);
    const auto undo = window.undoDepthForTest();
    const auto redo = window.redoDepthForTest();
    const bool dirty = window.hasUnsavedChangesForTest();
    auto* action = window.findChild<QAction*>("tools.model_import");
    check(action && action->isEnabled(), "Tools exposes the ordinary model importer as an enabled action");
    bool visited = false;
    const QString output = temporary.filePath("must-not-be-created.3do");
    if ( action )
    {
        QTimer::singleShot(0, &window, [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog && dialog->findChild<QLineEdit*>("modelImportSource")
                    && dialog->findChild<QTableWidget*>("modelImportMaterials")
                    && dialog->findChild<QPlainTextEdit*>("modelImportReport")
                    && dialog->findChild<QPushButton*>("modelImportPrepare")
                    && dialog->findChild<QPushButton*>("modelImportCreate")
                    && dialog->findChild<QPushButton*>("modelImportSaveImage"),
                "Tools opens the actual model import controls");
            if ( dialog )
            {
                visited = true;
                if ( auto* destination = dialog->findChild<QLineEdit*>("modelImportOutput") )
                {
                    destination->setText(output);
                }
                dialog->reject();
            }
        });
        action->trigger();
        check(visited, "model import dialog opens and closes through the real menu action");
    }
    check(!QFile::exists(output), "opening and closing model import never implicitly publishes a file");
    check(assets::saveToSedBytes(window.documentStateForTest().level) == before
            && window.undoDepthForTest() == undo && window.redoDepthForTest() == redo
            && window.hasUnsavedChangesForTest() == dirty,
        "model import menu preserves authored level, undo/redo and dirty state");
}
} // namespace sed::app_tests
