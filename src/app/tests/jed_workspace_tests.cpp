#include "test_common.hpp"
#include "workspace_portal_fixture.hpp"
#include "main_window/main_window.hpp"
#include "gl_view.hpp"
#include "ui/shortcuts/workspace_selection.hpp"
#include "sed/assets/sed.hpp"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

namespace sed::app_tests
{
void runJedWorkspaceTests()
{
    using namespace workspace_test;
    QTemporaryDir directory;
    const auto root = std::filesystem::path(directory.path().toStdString());
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(root/"recovery");
    editor::DocumentState state;
    state.level = twoRoomPortal();
    state.level.sectors[back.sector].surfaces[back.surface].adjoin.reset();
    state.selection.surfaces.insert(front);
    window.setDocumentStateForTest(state);
    const auto action = [&](const char* name) { return window.findChild<QAction*>(name); };
    auto* preset = action("view.jed_workspace");
    auto* isolate = action("selection.isolate");
    auto* showAll = action("selection.clear_isolation");
    auto* repair = action("edit.preview_adjoin_repair");
    auto* store = action("selection.store_set");
    auto* repeat = action("edit.repeat_last_operation");
    auto* place = action("sector.place_lights");
    auto* save = action("file.save");
    auto* checksDock = window.findChild<QDockWidget*>("checksDock");
    auto* checksButton = window.findChild<QPushButton*>("checks.previewAdjoinRepair");
    check(preset && isolate && showAll && repair && store && repeat && place && save && checksDock && checksButton,
        "workspace commands are attached to the native menus and Checks dock");
    if (!(preset && isolate && showAll && repair && store && repeat && place && save && checksDock && checksButton)) return;
    preset->trigger();
    auto* view = window.glViewForTest();
    auto* align = action("viewport.view_align_to_grid");
    check(view->cameraProjectionType() == app::ViewportCamera::ProjectionType::Orthographic &&
        !checksDock->isHidden() && align && align->shortcuts().contains(QKeySequence(Qt::Key_F2)),
        "legacy preset restores orthographic navigation, dock and shortcut");
    auto& selection = window.mutableDocumentStateForTest().selection;
    selection.clear(); selection.surfaces.insert(front);
    window.updateEditActionsForTest();
    isolate->trigger();
    check(view->sectorVisibleInEditor(state.level.sectors[0]) && !view->sectorVisibleInEditor(state.level.sectors[1]),
        "native isolation distinguishes two sectors on the same visible layer");
    showAll->trigger();
    check(view->sectorVisibleInEditor(state.level.sectors[1]), "Show All restores second room");
    int confirmations = 0;
    const auto modalAnswer = [&confirmations](QMessageBox::StandardButton answer)
    {
        QTimer::singleShot(0,[answer, &confirmations]
        {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            { ++confirmations; box->button(answer)->click(); }
        });
    };
    const auto depth = window.undoDepthForTest();
    const auto baseline = window.documentStateForTest().level;
    const auto beforeSelection = window.documentStateForTest().selection;
    check(ui::missingAdjoinBacklink(baseline,front)==back,"the selected face is the unique missing return link");

    modalAnswer(QMessageBox::No);
    checksButton->click();
    check(confirmations==1 && window.undoDepthForTest()==depth && assets::sedLevelsEqual(window.documentStateForTest().level,baseline) &&
        window.documentStateForTest().selection.surfaces==beforeSelection.surfaces && window.documentStateForTest().selection.sectors==beforeSelection.sectors, "cancelled repair preserves level and selection");
    modalAnswer(QMessageBox::Yes);
    repair->trigger();
    auto fixed = baseline;
    level::levelSysAdjoinSurfaces(fixed,front,back,false);
    check(confirmations==2 && window.undoDepthForTest()==depth+1 && assets::sedLevelsEqual(window.documentStateForTest().level,fixed),
        "repair changes only the missing reciprocal link");
    check(window.undoForTest() && assets::sedLevelsEqual(window.documentStateForTest().level,baseline),
        "one Undo restores the original fault");
    check(window.redoForTest(), "repair can be redone");
    window.activateSectorEditModeForTest();
    const auto selectRoom = [&](int index)
    {
        auto& selected = window.mutableDocumentStateForTest().selection;
        selected.clear(); selected.sectors.insert(index);
        view->setSelectedSector(index);
        window.updateEditActionsForTest();
    };
    selectRoom(0);
    QTimer::singleShot(0,[] {
        if (auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
        { dialog->setDoubleValue(0.75); dialog->accept(); }
    });
    place->trigger();
    selectRoom(1);
    check(repeat->isEnabled(), "the previous operation is available for the newly selected room");
    repeat->trigger();
    const auto& lights = window.documentStateForTest().level.lights;
    check(lights.size()==2 && lights[0].intensity==0.75 && lights[1].intensity==0.75 &&
        lights[0].position.x != lights[1].position.x, "Repeat reuses the numerical value on the current room");
    selectRoom(1);
    QTimer::singleShot(0,[] {
        if (auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
        { dialog->setTextValue("Landing"); dialog->accept(); }
    });
    store->trigger();
    const auto path = root/"selected.sed";
    window.setCurrentPathForTitleForTest(path);
    save->trigger();
    check(std::filesystem::is_regular_file(path), "named selection saved in native project");
    if (!std::filesystem::is_regular_file(path)) return;
    MainWindow reopened;
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    reopened.setRecoveryDirectoryForTest(root/"reopened");
    reopened.openLevelFromFilenameForTest(QString::fromStdString(path.string()));
    auto* recall = reopened.findChild<QMenu*>("selection.recall_set");
    check(recall != nullptr, "reopened project exposes Recall Selection");
    if (!recall) return;
    reopened.mutableDocumentStateForTest().selection.clear();
    QMetaObject::invokeMethod(recall,"aboutToShow");
    check(recall->actions().size()==1 && recall->actions().front()->text()=="Landing", "reopened named set is listed");
    if (!recall->actions().empty()) recall->actions().front()->trigger();
    check(reopened.documentStateForTest().selection.sectors.contains(1), "Recall restores the named room");
}
} // namespace sed::app_tests
