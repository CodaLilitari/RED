#include "app_settings.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "test_common.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTabWidget>
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

void runPackageAuthoringIntegrationTests()
{
    QTemporaryDir temporary;
    check(temporary.isValid(), "package authoring menu fixture owns its temporary directory");
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
    check(createdSettings, "package authoring menu fixture precreates settings to prevent legacy migration");
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
    auto* action = window.findChild<QAction*>("tools.package_authoring");
    check(action && action->isEnabled(), "Tools exposes ordinary archive and package authoring as an enabled action");
    bool visited = false;
    const QString output = temporary.filePath("must-not-be-created.gob");
    const QString report = temporary.filePath("must-not-be-created.txt");
    if ( action )
    {
        QTimer::singleShot(0, &window, [&]
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog != nullptr, "package authoring action opens an ordinary modal dialog");
            if ( !dialog )
            {
                return;
            }
            visited = true;
            auto* modes = dialog->findChild<QTabWidget*>("packageMode");
            auto* entries = dialog->findChild<QTableWidget*>("packageEntries");
            auto* archives = dialog->findChild<QTableWidget*>("packageArchives");
            auto* extras = dialog->findChild<QTableWidget*>("packageExtras");
            auto* review = dialog->findChild<QPlainTextEdit*>("packageReport");
            auto* prepare = dialog->findChild<QPushButton*>("packagePrepare");
            auto* create = dialog->findChild<QPushButton*>("packageCreate");
            auto* exportReport = dialog->findChild<QPushButton*>("packageExportReport");
            auto* destination = dialog->findChild<QLineEdit*>("packageOutput");
            auto* reportDestination = dialog->findChild<QLineEdit*>("packageReportPath");
            check(modes && modes->count() == 2 && modes->tabText(0) == "Resource Archive"
                    && modes->tabText(1) == "Exported Level" && entries && archives && extras
                    && review && review->isReadOnly() && prepare && prepare->isEnabled()
                    && create && !create->isEnabled() && exportReport && !exportReport->isEnabled()
                    && destination && destination->text().isEmpty()
                    && reportDestination && reportDestination->text().isEmpty(),
                "Tools opens both real package workflows with review required before publication");
            if ( destination )
            {
                destination->setText(output);
            }
            if ( reportDestination )
            {
                reportDestination->setText(report);
            }
            if ( modes )
            {
                modes->setCurrentIndex(1);
                check(modes->currentIndex() == 1 && archives && extras,
                    "exported-level workflow is accessible through the ordinary tab");
                modes->setCurrentIndex(0);
            }
            check(create && !create->isEnabled() && exportReport && !exportReport->isEnabled(),
                "choosing filenames and switching workflows never prepares or publishes implicitly");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            auto* close = buttons ? buttons->button(QDialogButtonBox::Close) : nullptr;
            check(close != nullptr, "package authoring exposes its ordinary Close control");
            if ( close )
            {
                close->click();
            }
            else
            {
                dialog->reject();
            }
        });
        action->trigger();
        check(visited, "package authoring dialog opens and closes through the real menu action");
    }
    check(!QFile::exists(output) && !QFile::exists(report),
        "closing package authoring without Prepare/Create publishes neither archive nor report");
    check(assets::saveToSedBytes(window.documentStateForTest().level) == before
            && window.undoDepthForTest() == undo && window.redoDepthForTest() == redo
            && window.hasUnsavedChangesForTest() == dirty,
        "package authoring menu preserves authored level, undo/redo and dirty state");
}
} // namespace sed::app_tests
