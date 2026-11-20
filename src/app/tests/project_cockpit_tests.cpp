#include "ui/project/project_cockpit.hpp"
#include "project_fixture.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
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
        QMainWindow main;
        auto* tools = main.menuBar()->addMenu("Tools");
        sed::ui::addProjectCockpit(tools, &main, [&] { return workspace; });
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
        auto* search = dialog->findChild<QLineEdit*>("projectAssetSearch");
        require(search, "the Project Cockpit exposes one search across resource providers");
        auto* inspect = dialog->findChild<QPushButton*>("inspectProjectAssets");
        require(inspect, "a project source inspection checks its declared mounts");
        inspect->click();
        search->setText(QStringLiteral("wall"));
        auto* matches = dialog->findChild<QTreeWidget*>("projectAssetResults");
        timeout.restart();
        while (matches && !matches->topLevelItemCount() && timeout.elapsed() < 10000)
        {
            app.processEvents(); QThread::msleep(5);
        }
        require(matches && matches->topLevelItemCount() == 1 && matches->topLevelItem(0)->text(0).contains("wall.mat"),
            "search finds the material with the captured project winner");
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
