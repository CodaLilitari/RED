#include "ui/project/project_cockpit.hpp"
#include "project_fixture.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary project");
        const auto root = fs::path(dir.path().toStdWString());
        const auto workspace = root / "campaign";
        const auto stock = root / "game";
        createCampaignFixture(workspace, stock);
        QCoreApplication::setOrganizationName("REDTests");
        QCoreApplication::setApplicationName("ProjectPlaytest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        const auto profilePath = workspace / "red-project.json";
        QFile profile(QString::fromStdU16String(profilePath.u16string()));
        require(profile.open(QIODevice::ReadOnly), "read fixture profile");
        auto manifest = QJsonDocument::fromJson(profile.readAll()).object();
        profile.close();
        manifest["scenarios"] = QJsonArray{QJsonObject{{"name","walk"}, {"level","one.jkl"},
            {"definition","walk.json"}, {"receipts",QJsonArray{}}}};
        write(profilePath, QJsonDocument(manifest).toJson().toStdString());
        write(workspace / "walk.json", R"({"launch":{"game":"jk","map":"one.jkl","episode":"one"}})");
        const auto machine = workspace / "machine.json";
        write(machine, R"({"files":[]})");
        QByteArray stamp = "first";
        fs::path ownedExport;
        sed::ui::PlaytestEditor editor{
            [&] { return workspace / "jkl/one.jkl"; }, [&] { return stamp; },
            [&](const fs::path& destination)
            {
                ownedExport = destination;
                fs::copy_file(workspace / "jkl/one.jkl", destination);
                return true;
            }};
        QMainWindow main;
        auto* tools = main.menuBar()->addMenu("Tools");
        sed::ui::addProjectCockpit(tools, &main, [&] { return workspace; }, editor);
        auto* action = main.findChild<QAction*>("tools.project_cockpit");
        require(action, "reachable Project Cockpit action");
        action->trigger();
        auto* dialog = main.findChild<QDialog*>("projectCockpit");
        require(dialog, "native project dialog");
        auto* rootEdit = dialog->findChild<QLineEdit*>("projectStockRoot");
        auto* prepare = dialog->findChild<QPushButton*>("prepareProject");
        auto* create = dialog->findChild<QPushButton*>("createProjectPackage");
        auto* tree = dialog->findChild<QTreeWidget*>();
        require(rootEdit && prepare && create && tree, "project review controls");
        rootEdit->setText(QString::fromStdWString(stock.wstring()));
        prepare->click();
        QElapsedTimer timeout;
        timeout.start();
        while ( !create->isEnabled() && timeout.elapsed() < 10000 )
        {
            app.processEvents();
            QThread::msleep(5);
        }
        require(create->isEnabled(), "prepared campaign can be packaged");
        bool customAndStock = false;
        for ( int i = 0; i < tree->topLevelItemCount(); ++i )
        {
            const auto* row = tree->topLevelItem(i);
            if ( row->text(0) == "wall.mat" && row->text(1).contains("project:art.gob")
                && row->childCount() == 1 && row->child(0)->text(1) == "stock:stock.gob" ) customAndStock = true;
        }
        require(customAndStock, "editor displays the winning and shadowed material");
        auto* test = dialog->findChild<QPushButton*>("projectRunTest");
        auto* scenario = dialog->findChild<QComboBox*>("projectTestScenario");
        auto* host = dialog->findChild<QLineEdit*>("projectMachineRequest");
        auto* status = dialog->findChild<QLabel*>("projectStatus");
        require(test && test->isEnabled() && scenario && scenario->currentText() == "walk" && host && status,
            "named project scenario reachable from ordinary Test action");
        host->setText(QString::fromStdU16String(machine.u16string()));
        test->click();
        stamp = "changed-after-export";
        timeout.restart();
        while ( !status->text().contains("changed during") && timeout.elapsed() < 10000 )
        {
            app.processEvents();
            QThread::msleep(5);
        }
        require(status->text().contains("changed during") && !ownedExport.empty() && !fs::exists(ownedExport)
            && fs::exists(workspace / "jkl/one.jkl") && fs::exists(machine),
            "stale preparation releases only its private export and preserves the original editor inputs");
        dialog->close();
        std::cout << "Project Cockpit native workflow passed\n";
        return 0;
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
