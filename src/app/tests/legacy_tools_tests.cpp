#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/thing.hpp"
#include "app_settings.hpp"
#include "ui/sector/sector_shape_utils.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QInputDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string>

namespace sed::app_tests
{
namespace
{

namespace fs = std::filesystem;

struct DialogLog
{
    int messageBoxes = 0;
    QStringList texts{};
};

// Answers every modal message box with `answer` (or its default when the
// box lacks that button) and rejects any other modal dialog.
DialogLog triggerAnswering(QAction* action, const QMessageBox::StandardButton answer)
{
    DialogLog log{};
    QTimer driver{};
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
        {
            QWidget* const modal = QApplication::activeModalWidget();
            if ( auto* message = qobject_cast<QMessageBox*>(modal) )
            {
                ++log.messageBoxes;
                log.texts.push_back(message->text());
                if ( QAbstractButton* const button = message->button(answer) )
                {
                    button->click();
                }
                else
                {
                    message->accept();
                }
            }
            else if ( auto* dialog = qobject_cast<QDialog*>(modal) )
            {
                dialog->reject();
            }
        });
    driver.start();
    if ( action )
    {
        action->trigger();
    }
    driver.stop();
    return log;
}

QAction* action(MainWindow& window, const char* name)
{
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

std::string readBytes(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

editor::DocumentState cubeState()
{
    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("World");
    level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    state.level.sectors[0].layer = 0;
    return state;
}

fs::path toPath(const QString& text)
{
    return fs::path(text.toStdWString());
}

bool saveWindow(MainWindow& window, const fs::path& path)
{
    window.setCurrentPathForTitleForTest(path);
    const DialogLog saved = triggerAnswering(action(window, "file.save"), QMessageBox::Ok);
    return saved.messageBoxes == 0 && fs::is_regular_file(path)
        && !window.hasUnsavedAuthoredLevelChangesForTest();
}

void runRevertToSaved()
{
    QTemporaryDir root{};
    check(root.isValid(), "revert: scratch directory");
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(toPath(root.filePath(QStringLiteral("recovery"))));
    window.setDocumentStateForTest(cubeState());
    QAction* const revert = action(window, "file.revert_to_saved");
    check(revert != nullptr, "File menu exposes Revert to Saved");
    if ( !revert )
    {
        return;
    }

    const fs::path levelPath = toPath(root.filePath(QStringLiteral("revert.sed")));
    check(saveWindow(window, levelPath), "revert: fixture level saves cleanly");
    const double savedX = window.documentStateForTest().level.sectors[0].vertices[0].x;

    check(window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
              {
                  state.level.sectors[0].vertices[0].x += 10.0;
                  return true;
              })
            && window.hasUnsavedAuthoredLevelChangesForTest(),
        "revert: an edit makes the document dirty");

    const DialogLog declined = triggerAnswering(revert, QMessageBox::No);
    check(declined.messageBoxes == 1
            && window.hasUnsavedAuthoredLevelChangesForTest()
            && close(window.documentStateForTest().level.sectors[0].vertices[0].x, savedX + 10.0),
        "revert: declining the confirmation keeps the unsaved edit");

    const DialogLog accepted = triggerAnswering(revert, QMessageBox::Yes);
    check(accepted.messageBoxes == 1,
        "revert: one confirmation replaces the Save/Discard prompt and recovery offer");
    check(close(window.documentStateForTest().level.sectors[0].vertices[0].x, savedX)
            && !window.hasUnsavedAuthoredLevelChangesForTest(),
        "revert: accepting reloads the saved level and leaves it clean");
    check(window.currentPathForTest() == levelPath, "revert: the document keeps its saved path");

    // A clean document reverts without any prompt.
    const DialogLog clean = triggerAnswering(revert, QMessageBox::No);
    check(clean.messageBoxes == 0 && !window.hasUnsavedAuthoredLevelChangesForTest(),
        "revert: a clean document reloads without prompting");

    MainWindow unsaved{};
    unsaved.setAttribute(Qt::WA_DontShowOnScreen);
    unsaved.setRecoveryDirectoryForTest(toPath(root.filePath(QStringLiteral("recovery-unsaved"))));
    unsaved.setDocumentStateForTest(cubeState());
    const std::size_t sectorCount = unsaved.documentStateForTest().level.sectors.size();
    const DialogLog refused = triggerAnswering(action(unsaved, "file.revert_to_saved"), QMessageBox::Ok);
    check(refused.messageBoxes == 1 && unsaved.documentStateForTest().level.sectors.size() == sectorCount,
        "revert: an unsaved level reports there is nothing to revert to");
}

void runBackupCopy()
{
    QTemporaryDir root{};
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(toPath(root.filePath(QStringLiteral("recovery"))));
    window.setDocumentStateForTest(cubeState());
    QAction* const backup = action(window, "file.backup_copy");
    check(backup != nullptr, "File menu exposes Make a Backup Copy");
    if ( !backup )
    {
        return;
    }
    check(backup->shortcuts().contains(QKeySequence(Qt::ControlModifier | Qt::Key_B)),
        "Make a Backup Copy defaults to the legacy Ctrl+B shortcut");

    const fs::path levelPath = toPath(root.filePath(QStringLiteral("level.sed")));
    check(saveWindow(window, levelPath), "backup: fixture level saves cleanly");
    const std::string savedBytes = readBytes(levelPath);

    const DialogLog first = triggerAnswering(backup, QMessageBox::Ok);
    const fs::path slot0 = levelPath.parent_path() / "backup" / "level00.sed";
    check(first.messageBoxes == 0 && fs::is_regular_file(slot0) && readBytes(slot0) == savedBytes,
        "backup: first copy is backup/level00.sed with the saved bytes");

    const DialogLog second = triggerAnswering(backup, QMessageBox::Ok);
    check(second.messageBoxes == 0 && fs::is_regular_file(levelPath.parent_path() / "backup" / "level01.sed"),
        "backup: the next copy uses slot 01");

    // Backups copy the file on disk; unsaved edits are not included.
    (void)window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.sectors[0].vertices[0].x += 3.0;
            return true;
        });
    (void)triggerAnswering(backup, QMessageBox::Ok);
    check(readBytes(levelPath.parent_path() / "backup" / "level02.sed") == savedBytes,
        "backup: a dirty document still copies the saved file");

    MainWindow unsaved{};
    unsaved.setAttribute(Qt::WA_DontShowOnScreen);
    unsaved.setRecoveryDirectoryForTest(toPath(root.filePath(QStringLiteral("recovery-unsaved"))));
    unsaved.setDocumentStateForTest(cubeState());
    const DialogLog refused = triggerAnswering(action(unsaved, "file.backup_copy"), QMessageBox::Ok);
    check(refused.messageBoxes == 1, "backup: an unsaved level reports that it must be saved first");
}

void runSaveKeepsBak()
{
    QTemporaryDir root{};
    {
        QSettings settings = settingsStore();
        settings.remove(QStringLiteral("files/keep_bak_on_save"));
    }
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(toPath(root.filePath(QStringLiteral("recovery"))));
    window.setDocumentStateForTest(cubeState());

    const fs::path levelPath = toPath(root.filePath(QStringLiteral("level.sed")));
    const fs::path bakPath = toPath(root.filePath(QStringLiteral("level.sed.bak")));
    check(saveWindow(window, levelPath), "save .bak: first save succeeds");
    check(!fs::exists(bakPath), "save .bak: a brand-new file has no previous version to keep");
    const std::string firstBytes = readBytes(levelPath);

    (void)window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.sectors[0].vertices[0].x += 2.0;
            return true;
        });
    check(saveWindow(window, levelPath), "save .bak: second save succeeds");
    check(fs::is_regular_file(bakPath) && readBytes(bakPath) == firstBytes
            && readBytes(levelPath) != firstBytes,
        "save .bak: overwriting a level keeps the previous file as level.sed.bak by default");
    const std::string secondBytes = readBytes(levelPath);

    {
        QSettings settings = settingsStore();
        settings.setValue(QStringLiteral("files/keep_bak_on_save"), false);
    }
    (void)window.applyStructuralHistoryEditForTest([](editor::DocumentState& state)
        {
            state.level.sectors[0].vertices[0].x += 2.0;
            return true;
        });
    check(saveWindow(window, levelPath), "save .bak: third save succeeds");
    check(readBytes(bakPath) == firstBytes && readBytes(levelPath) != secondBytes,
        "save .bak: turning the preference off leaves the existing .bak untouched");
    {
        QSettings settings = settingsStore();
        settings.remove(QStringLiteral("files/keep_bak_on_save"));
    }
}

// Drives the item prompt (records its default, then picks `item`) and
// answers any numeric prompt with `value`.
struct GridTransformLog
{
    QStringList offeredItems{};
    int numericPrompts = 0;
};

GridTransformLog triggerGridTransform(QAction* action, const QString& item, const double value)
{
    GridTransformLog log{};
    QTimer driver{};
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
        {
            QWidget* const modal = QApplication::activeModalWidget();
            if ( auto* input = qobject_cast<QInputDialog*>(modal) )
            {
                if ( input->inputMode() == QInputDialog::TextInput )
                {
                    log.offeredItems.push_back(input->textValue());
                    input->setTextValue(item);
                }
                else
                {
                    ++log.numericPrompts;
                    input->setDoubleValue(value);
                }
                input->accept();
            }
            else if ( auto* dialog = qobject_cast<QDialog*>(modal) )
            {
                dialog->reject();
            }
        });
    driver.start();
    if ( action )
    {
        action->trigger();
    }
    driver.stop();
    return log;
}

editor::DocumentState thingsAt(std::initializer_list<TVector> points)
{
    editor::DocumentState state = cubeState();
    for ( const TVector& point : points )
    {
        Thing thing{};
        thing.name = "ghost";
        thing.templateName = "ghost";
        thing.sector = 0;
        thing.x = point.x;
        thing.y = point.y;
        thing.z = point.z;
        state.selection.things.insert(static_cast<std::int32_t>(state.level.things.size()));
        state.level.things.push_back(thing);
    }
    return state;
}

bool thingAt(const Level& level, const std::size_t index, const double x, const double y, const double z)
{
    return index < level.things.size() && close(level.things[index].x, x, 1e-5)
        && close(level.things[index].y, y, 1e-5) && close(level.things[index].z, z, 1e-5);
}

void runGridMirrorAndScaleAlong()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();
    GlView* const view = window.glViewForTest();
    QAction* const mirror = action(window, "transform.mirror");
    QAction* const scaleAlong = action(window, "transform.scale_along");
    check(view && mirror, "grid transforms: fixture exposes the viewport and Mirror");
    check(scaleAlong != nullptr, "Transform menu exposes Scale Along");
    if ( !view || !mirror || !scaleAlong )
    {
        return;
    }
    // A diagonal grid through the origin: no X/Y/Z mirror or scale matches it.
    const float half = std::sqrt(0.5f);
    check(view->alignGridToBasis(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
              QVector3D(half, half, 0.0f)),
        "grid transforms: diagonal grid is accepted");

    window.setDocumentStateForTest(thingsAt({ TVector{ 1.0, 0.5, 0.25 } }));
    window.updateEditActionsForTest();
    const GridTransformLog mirrored = triggerGridTransform(mirror, QStringLiteral("Grid"), 0.0);
    check(mirrored.offeredItems.size() == 1 && mirrored.numericPrompts == 0,
        "grid mirror: Mirror asks only for the axis");
    check(thingAt(window.documentStateForTest().level, 0, -0.5, -1.0, 0.25),
        "grid mirror: Grid reflects across the grid plane through the grid origin");
    check(window.undoDepthForTest() == 1u, "grid mirror: one undo command");

    window.setDocumentStateForTest(thingsAt({ TVector{ 1.0, 1.0, 0.0 }, TVector{ -1.0, -1.0, 0.0 },
        TVector{ 1.0, -1.0, 0.0 }, TVector{ -1.0, 1.0, 0.0 } }));
    window.updateEditActionsForTest();
    check(scaleAlong->isEnabled(), "scale along: enabled with Things selected");
    const std::size_t undoBefore = window.undoDepthForTest();
    const GridTransformLog scaled = triggerGridTransform(scaleAlong, QStringLiteral("Grid normal"), 2.0);
    check(scaled.offeredItems == QStringList{ QStringLiteral("Grid normal") } && scaled.numericPrompts == 1,
        "scale along: defaults to the grid normal, then asks for one factor");
    const Level& level = window.documentStateForTest().level;
    check(thingAt(level, 0, 2.0, 2.0, 0.0) && thingAt(level, 1, -2.0, -2.0, 0.0),
        "scale along: points on the grid normal move away from the selection center");
    check(thingAt(level, 2, 1.0, -1.0, 0.0) && thingAt(level, 3, -1.0, 1.0, 0.0),
        "scale along: points in the grid plane through the center stay put");
    check(window.undoDepthForTest() == undoBefore + 1u, "scale along: one undo command");

    window.setDocumentStateForTest(thingsAt({ TVector{ 1.0, 1.0, 1.0 }, TVector{ -1.0, -1.0, -1.0 } }));
    window.updateEditActionsForTest();
    (void)triggerGridTransform(scaleAlong, QStringLiteral("Z"), 3.0);
    check(thingAt(window.documentStateForTest().level, 0, 1.0, 1.0, 3.0),
        "scale along: Z scales only the Z coordinate");

    window.setDocumentStateForTest(thingsAt({ TVector{ 1.0, 1.0, 1.0 } }));
    window.updateEditActionsForTest();
    const std::size_t undoBeforeCancel = window.undoDepthForTest();
    QTimer canceller{};
    canceller.setInterval(5);
    QObject::connect(&canceller, &QTimer::timeout, []()
        {
            if ( auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()) )
            {
                dialog->reject();
            }
        });
    canceller.start();
    scaleAlong->trigger();
    canceller.stop();
    check(window.undoDepthForTest() == undoBeforeCancel
            && thingAt(window.documentStateForTest().level, 0, 1.0, 1.0, 1.0),
        "scale along: cancelling the direction leaves the level and history untouched");
}

void runNudgeAlongGridNormal()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();
    GlView* const view = window.glViewForTest();
    QAction* const nudgeUp = action(window, "transform.nudge_up");
    QAction* const nudgeDown = action(window, "transform.nudge_down");
    check(view && nudgeUp && nudgeDown, "Transform menu exposes Nudge Up and Nudge Down");
    if ( !view || !nudgeUp || !nudgeDown )
    {
        return;
    }
    check(nudgeUp->shortcuts().contains(QKeySequence(Qt::ControlModifier | Qt::Key_Up))
            && nudgeUp->shortcuts().contains(QKeySequence(Qt::Key_BracketLeft)),
        "nudge: Up defaults to the legacy Ctrl+Up and [ keys");
    check(nudgeDown->shortcuts().contains(QKeySequence(Qt::ControlModifier | Qt::Key_Down))
            && nudgeDown->shortcuts().contains(QKeySequence(Qt::Key_BracketRight)),
        "nudge: Down defaults to the legacy Ctrl+Down and ] keys");

    settingsStore().remove("transform/nudge_step");
    window.setDocumentStateForTest(thingsAt({ TVector{ 0.25, 0.25, 0.25 } }));
    window.updateEditActionsForTest();
    check(nudgeUp->isEnabled(), "nudge: enabled with a Thing selected");
    const std::size_t undoBefore = window.undoDepthForTest();
    DialogLog log = triggerAnswering(nudgeUp, QMessageBox::Ok);
    check(log.messageBoxes == 0 && thingAt(window.documentStateForTest().level, 0, 0.25, 0.25, 0.27),
        "nudge: the default step is the legacy 0.02 along the grid normal");
    check(window.undoDepthForTest() == undoBefore + 1u, "nudge: records exactly one undo step");
    check(window.documentStateForTest().selection.things.contains(0), "nudge: keeps the selection for repeated nudges");

    const float half = std::sqrt(0.5f);
    check(view->alignGridToBasis(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f),
              QVector3D(half, half, 0.0f)),
        "nudge: diagonal grid is accepted");
    settingsStore().setValue("transform/nudge_step", 0.1);
    (void)triggerAnswering(nudgeDown, QMessageBox::Ok);
    const double offset = 0.1 * std::sqrt(0.5);
    check(thingAt(window.documentStateForTest().level, 0, 0.25 - offset, 0.25 - offset, 0.27),
        "nudge: Down moves against the grid normal by the preferred step");
    check(window.undoDocumentForTest()
            && thingAt(window.documentStateForTest().level, 0, 0.25, 0.25, 0.27),
        "nudge: undo restores the previous position");
    view->resetGridOverrides();

    MainWindow sectorWindow{};
    sectorWindow.setAttribute(Qt::WA_DontShowOnScreen);
    sectorWindow.activateSectorEditModeForTest();
    editor::DocumentState sectorState = cubeState();
    const double baseZ = sectorState.level.sectors[0].vertices[0].z;
    sectorState.selection.sectors.insert(0);
    sectorWindow.setDocumentStateForTest(std::move(sectorState));
    sectorWindow.updateEditActionsForTest();
    QAction* const sectorNudge = action(sectorWindow, "transform.nudge_up");
    check(sectorNudge && sectorNudge->isEnabled(), "nudge: enabled with a sector selected");
    if ( sectorNudge )
    {
        (void)triggerAnswering(sectorNudge, QMessageBox::Ok);
    }
    check(close(sectorWindow.documentStateForTest().level.sectors[0].vertices[0].z, baseZ + 0.1, 1e-9)
            && sectorWindow.documentStateForTest().selection.sectors.contains(0),
        "nudge: a selected sector moves along the grid normal and stays selected");
    settingsStore().remove("transform/nudge_step");

    sectorWindow.setDocumentStateForTest(cubeState());
    sectorWindow.updateEditActionsForTest();
    check(sectorNudge && !sectorNudge->isEnabled(), "nudge: disabled without a selection");
}

std::optional<SurfaceRef> firstSurfaceWithNormal(const Level& level, const TVector& normal)
{
    const Sector& sector = level.sectors[0];
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        if ( sector.surfaces[i].normal.dot(normal) > 0.99 )
        {
            return SurfaceRef{ 0, static_cast<std::int32_t>(i) };
        }
    }
    return std::nullopt;
}

void runPlanarize()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSurfaceEditModeForTest();
    editor::DocumentState state = cubeState();
    const std::optional<SurfaceRef> floor = firstSurfaceWithNormal(state.level, TVector::down);
    check(floor.has_value(), "planarize: cube fixture has a floor");
    if ( !floor )
    {
        return;
    }
    Sector& sector = state.level.sectors[0];
    const std::int32_t lifted = sector.surfaces[static_cast<std::size_t>(floor->surface)].vertices[0];
    const double baselineZ = sector.vertices[static_cast<std::size_t>(lifted)].z;
    // Warp a single floor vertex upwards without touching the stored normal.
    const std::int32_t warped = sector.surfaces[static_cast<std::size_t>(floor->surface)].vertices[1];
    sector.vertices[static_cast<std::size_t>(warped)].z += 0.05;
    check(!level::surfaceIsPlanar(sector, sector.surfaces[static_cast<std::size_t>(floor->surface)]),
        "planarize: fixture floor is warped");
    state.selection.surfaces.insert(*floor);
    window.setDocumentStateForTest(std::move(state));
    window.updateEditActionsForTest();

    QAction* const planarize = action(window, "surface.planarize");
    check(planarize != nullptr, "Surface menu exposes Planarize");
    if ( !planarize )
    {
        return;
    }
    check(planarize->shortcuts().contains(QKeySequence(Qt::ControlModifier | Qt::Key_P)),
        "Planarize defaults to the legacy Ctrl+P shortcut");
    check(planarize->isEnabled(), "Planarize is enabled with a surface selected");

    const std::size_t undoBefore = window.undoDepthForTest();
    const DialogLog applied = triggerAnswering(planarize, QMessageBox::Ok);
    const Sector& result = window.documentStateForTest().level.sectors[0];
    check(applied.messageBoxes == 0
            && level::surfaceIsPlanar(result, result.surfaces[static_cast<std::size_t>(floor->surface)]),
        "planarize: the warped floor becomes planar");
    check(close(result.vertices[static_cast<std::size_t>(warped)].z, baselineZ, 1e-9),
        "planarize: the warped vertex returns to the floor plane");
    check(window.undoDepthForTest() == undoBefore + 1u, "planarize: records exactly one undo step");
    check(window.documentStateForTest().selection.surfaces.contains(*floor),
        "planarize: keeps the surface selected");

    const std::size_t undoAfter = window.undoDepthForTest();
    (void)triggerAnswering(planarize, QMessageBox::Ok);
    check(window.undoDepthForTest() == undoAfter, "planarize: an already planar surface records no undo step");

    check(window.undoDocumentForTest()
            && close(window.documentStateForTest().level.sectors[0].vertices[static_cast<std::size_t>(warped)].z,
                baselineZ + 0.05, 1e-9),
        "planarize: undo restores the warped vertex");
}

editor::DocumentState adjoinedCubesState(SurfaceRef& ceiling, SurfaceRef& floor)
{
    editor::DocumentState state = cubeState();
    level::levelCreateCube(state.level, 0.0, 0.0, 1.0, TVector::up, TVector::right);
    state.level.sectors[1].layer = 0;
    ceiling = firstSurfaceWithNormal(state.level, TVector::down).value_or(SurfaceRef{ 0, 0 });
    floor = SurfaceRef{ 1, 0 };
    const Sector& upper = state.level.sectors[1];
    for ( std::size_t i = 0; i < upper.surfaces.size(); ++i )
    {
        if ( upper.surfaces[i].normal.dot(TVector::up) > 0.99 )
        {
            floor.surface = static_cast<std::int32_t>(i);
        }
    }
    Surface& lowerSide = state.level.sectors[0].surfaces[static_cast<std::size_t>(ceiling.surface)];
    Surface& upperSide = state.level.sectors[1].surfaces[static_cast<std::size_t>(floor.surface)];
    lowerSide.adjoin = floor;
    upperSide.adjoin = ceiling;
    lowerSide.geo = static_cast<std::int32_t>(TGeoMode::Texture);
    upperSide.geo = static_cast<std::int32_t>(TGeoMode::Texture);
    lowerSide.adjoinFlags = SAF_NoPlayerMove | SAF_AllowSoundPass;
    upperSide.adjoinFlags = SAF_NoAiMove;
    return state;
}

void runFixSurfaceFlags()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSurfaceEditModeForTest();
    SurfaceRef ceiling{};
    SurfaceRef upperFloor{};
    editor::DocumentState state = adjoinedCubesState(ceiling, upperFloor);
    const std::optional<SurfaceRef> floor = firstSurfaceWithNormal(state.level, TVector::up);
    check(floor.has_value(), "fix flags: fixture has a floor");
    if ( !floor )
    {
        return;
    }
    Surface& floorSurface = state.level.sectors[0].surfaces[static_cast<std::size_t>(floor->surface)];
    floorSurface.geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
    floorSurface.surfaceFlags = 0x10;
    state.selection.surfaces.insert(*floor);
    state.selection.surfaces.insert(ceiling);
    window.setDocumentStateForTest(std::move(state));
    window.updateEditActionsForTest();

    QAction* const fixSolid = action(window, "surface.fix_solid");
    QAction* const fixPassable = action(window, "surface.fix_passable_adjoins");
    check(fixSolid != nullptr && fixPassable != nullptr, "Surface menu exposes the flag repair commands");
    if ( !fixSolid || !fixPassable )
    {
        return;
    }
    // Legacy Ctrl+R stays with Rotate; the repair is reachable from the Surface menu.
    check(fixSolid->shortcut().isEmpty(), "Fix Solid Surfaces does not steal Rotate's Ctrl+R");
    check(fixSolid->isEnabled() && fixPassable->isEnabled(), "flag repairs are enabled with surfaces selected");

    const std::size_t undoBefore = window.undoDepthForTest();
    DialogLog log = triggerAnswering(fixSolid, QMessageBox::Ok);
    const auto surfaceAt = [&window](const SurfaceRef ref) -> const Surface&
    {
        return window.documentStateForTest().level.sectors[static_cast<std::size_t>(ref.sector)]
            .surfaces[static_cast<std::size_t>(ref.surface)];
    };
    check(log.messageBoxes == 0 && window.undoDepthForTest() == undoBefore + 1u,
        "fix solid: records exactly one undo step");
    check(surfaceAt(*floor).geo == static_cast<std::int32_t>(TGeoMode::Texture)
            && surfaceAt(*floor).surfaceFlags == (0x10 | SF_Collision | SF_Floor),
        "fix solid: the floor is textured, collides, and is marked as floor");
    check(surfaceAt(ceiling).geo == static_cast<std::int32_t>(TGeoMode::Texture),
        "fix solid: leaves the adjoined surface alone");

    const std::size_t afterSolid = window.undoDepthForTest();
    (void)triggerAnswering(fixSolid, QMessageBox::Ok);
    check(window.undoDepthForTest() == afterSolid, "fix solid: already solid surfaces record no undo step");

    log = triggerAnswering(fixPassable, QMessageBox::Ok);
    check(log.messageBoxes == 0 && window.undoDepthForTest() == afterSolid + 1u,
        "fix passable: records exactly one undo step");
    check(surfaceAt(ceiling).geo == static_cast<std::int32_t>(TGeoMode::NotDrawn)
            && surfaceAt(ceiling).adjoinFlags == (SAF_Visible | SAF_Move | SAF_AllowSoundPass),
        "fix passable: the adjoin is not drawn, passable, and keeps unrelated flags");
    check(surfaceAt(upperFloor).adjoinFlags == SAF_NoAiMove
            && surfaceAt(upperFloor).geo == static_cast<std::int32_t>(TGeoMode::Texture),
        "fix passable: only the selected side changes");
    check(surfaceAt(*floor).geo == static_cast<std::int32_t>(TGeoMode::Texture),
        "fix passable: leaves solid surfaces alone");

    check(window.undoDocumentForTest() && surfaceAt(ceiling).adjoinFlags == (SAF_NoPlayerMove | SAF_AllowSoundPass),
        "fix passable: undo restores the adjoin flags");
    check(window.undoDocumentForTest() && surfaceAt(*floor).geo == static_cast<std::int32_t>(TGeoMode::NotDrawn)
            && surfaceAt(*floor).surfaceFlags == 0x10,
        "fix solid: undo restores the surface");

    MainWindow sectorWindow{};
    sectorWindow.setAttribute(Qt::WA_DontShowOnScreen);
    sectorWindow.activateSectorEditModeForTest();
    editor::DocumentState sectorState = adjoinedCubesState(ceiling, upperFloor);
    for ( Surface& surface : sectorState.level.sectors[1].surfaces )
    {
        surface.geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
    }
    sectorState.level.sectors[0].surfaces[0].geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
    sectorState.selection.sectors.insert(1);
    sectorWindow.setDocumentStateForTest(std::move(sectorState));
    sectorWindow.updateEditActionsForTest();
    QAction* const sectorFixSolid = action(sectorWindow, "surface.fix_solid");
    check(sectorFixSolid && sectorFixSolid->isEnabled(), "fix solid: enabled with a sector selected");
    if ( !sectorFixSolid )
    {
        return;
    }
    (void)triggerAnswering(sectorFixSolid, QMessageBox::Ok);
    std::size_t textured = 0;
    const Sector& upper = sectorWindow.documentStateForTest().level.sectors[1];
    for ( std::size_t i = 0; i < upper.surfaces.size(); ++i )
    {
        if ( upper.surfaces[i].geo == static_cast<std::int32_t>(TGeoMode::Texture) )
        {
            ++textured;
        }
    }
    check(textured == upper.surfaces.size() - 1u
            && upper.surfaces[static_cast<std::size_t>(upperFloor.surface)].geo
                == static_cast<std::int32_t>(TGeoMode::NotDrawn),
        "fix solid: sector mode repairs every unadjoined surface of the sector");
    check(sectorWindow.documentStateForTest().level.sectors[0].surfaces[0].geo
            == static_cast<std::int32_t>(TGeoMode::NotDrawn),
        "fix solid: sector mode leaves unselected sectors alone");
}

TVector toTVector(const QVector3D& v)
{
    return TVector{ static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z()) };
}

bool thingFacesCamera(const Thing& thing, const QVector3D& forward)
{
    TVector pyr{};
    pyr.pitch = thing.pch;
    pyr.yaw = thing.yaw;
    pyr.roll = thing.rol;
    const TVector look = TMatrix43::Create(TVector{}, pyr).lvec;
    return look.dot(toTVector(forward.normalized())) > 0.9999 && close(thing.rol, 0.0, 1e-9);
}

bool prepareCameraInsideCube(MainWindow& window)
{
    GlView* const view = window.glViewForTest();
    if ( !view )
    {
        return false;
    }
    view->setCameraProjectionType(app::ViewportCamera::ProjectionType::Perspective);
    const QVector3D low(0.1f, 0.15f, 0.2f);
    const QVector3D high(0.4f, 0.45f, 0.3f);
    view->frameWorldBounds(low, high, false);
    // An oblique view so sign or axis errors in the derived yaw and pitch are detected.
    view->orbitRotate(37.0f, -20.0f);
    view->setCameraOrbitDistance(0.01f);
    const QVector3D forward = view->cameraForward().normalized();
    if ( std::abs(forward.x()) < 0.2f || std::abs(forward.y()) < 0.2f || std::abs(forward.z()) < 0.2f )
    {
        return false;
    }
    const TVector eye = toTVector(view->cameraEye());
    return level::levelFindSectorForXYZ(window.documentStateForTest().level, eye.x, eye.y, eye.z) == 0;
}

void runCameraHelpers()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();
    window.setDocumentStateForTest(thingsAt({ TVector{ 0.25, 0.25, 0.25 }, TVector{ 0.5, 0.25, 0.25 } }));
    GlView* const view = window.glViewForTest();
    QAction* const setToCamera = action(window, "thing.set_to_camera");
    QAction* const insertAtCamera = action(window, "thing.insert_at_camera");
    QAction* const copyFrame = action(window, "thing.copy_camera_as_frame");
    check(view && setToCamera && insertAtCamera && copyFrame, "camera helpers: commands are exposed");
    if ( !view || !setToCamera || !insertAtCamera || !copyFrame )
    {
        return;
    }
    check(prepareCameraInsideCube(window), "camera helpers: fixture camera is inside the cube");
    const QVector3D eyeF = view->cameraEye();
    const TVector eye = toTVector(eyeF);

    view->setSelectionState(-1, -1, 1, -1, window.documentStateForTest().selection);
    window.updateEditActionsForTest();
    check(setToCamera->isEnabled(), "set thing to camera: enabled with Things selected");
    const std::size_t undoBefore = window.undoDepthForTest();
    DialogLog log = triggerAnswering(setToCamera, QMessageBox::Ok);
    const Level& moved = window.documentStateForTest().level;
    check(log.messageBoxes == 0 && thingAt(moved, 1, eye.x, eye.y, eye.z),
        "set thing to camera: the active Thing moves to the camera position");
    check(thingAt(moved, 0, eye.x - 0.25, eye.y, eye.z),
        "set thing to camera: other selected Things keep their offset from the active Thing");
    check(window.undoDepthForTest() == undoBefore + 1u, "set thing to camera: records exactly one undo step");
    check(window.documentStateForTest().selection.things.size() == 2u, "set thing to camera: keeps the selection");

    QApplication::clipboard()->clear();
    (void)triggerAnswering(copyFrame, QMessageBox::Ok);
    const QString frameText = QApplication::clipboard()->text();
    const QRegularExpressionMatch frame = QRegularExpression(
        QStringLiteral(R"(^\((-?\d+\.\d{5})/(-?\d+\.\d{5})/(-?\d+\.\d{5}):(-?\d+\.\d{5})/(-?\d+\.\d{5})/(-?\d+\.\d{5})\)$)"))
        .match(frameText);
    check(frame.hasMatch(), "copy camera as frame: clipboard holds a legacy (x/y/z:p/y/r) frame");
    if ( frame.hasMatch() )
    {
        Thing pose{};
        pose.pch = frame.captured(4).toDouble();
        pose.yaw = frame.captured(5).toDouble();
        pose.rol = frame.captured(6).toDouble();
        check(close(frame.captured(1).toDouble(), eye.x, 1e-5) && close(frame.captured(2).toDouble(), eye.y, 1e-5)
                && close(frame.captured(3).toDouble(), eye.z, 1e-5),
            "copy camera as frame: position is the camera position");
        check(thingFacesCamera(pose, view->cameraForward()), "copy camera as frame: orientation looks along the camera");
    }
    check(window.undoDepthForTest() == undoBefore + 1u, "copy camera as frame: does not edit the level");

    window.setThingPlacementTemplateForTest("ghost");
    window.updateEditActionsForTest();
    check(insertAtCamera->isEnabled(), "insert thing at camera: enabled with a placement template");
    const std::size_t thingsBefore = window.documentStateForTest().level.things.size();
    const std::size_t undoBeforeInsert = window.undoDepthForTest();
    log = triggerAnswering(insertAtCamera, QMessageBox::Ok);
    const Level& inserted = window.documentStateForTest().level;
    check(log.messageBoxes == 0 && inserted.things.size() == thingsBefore + 1u
            && thingAt(inserted, thingsBefore, eye.x, eye.y, eye.z),
        "insert thing at camera: adds one Thing at the camera position");
    if ( inserted.things.size() == thingsBefore + 1u )
    {
        const Thing& thing = inserted.things.back();
        check(thing.templateName == "ghost" && thing.sector == 0, "insert thing at camera: uses the template and sector");
        check(thingFacesCamera(thing, view->cameraForward()), "insert thing at camera: faces along the camera");
    }
    check(window.undoDepthForTest() == undoBeforeInsert + 1u, "insert thing at camera: records exactly one undo step");
    check(window.documentStateForTest().selection.things.size() == 1u
            && window.documentStateForTest().selection.things.contains(static_cast<std::int32_t>(thingsBefore)),
        "insert thing at camera: selects the new Thing");
    check(window.undoDocumentForTest() && window.documentStateForTest().level.things.size() == thingsBefore,
        "insert thing at camera: undo removes the Thing");

    view->setCameraOrbitDistance(50.0f);
    const TVector outside = toTVector(view->cameraEye());
    const std::size_t undoBeforeOutside = window.undoDepthForTest();
    if ( level::levelFindSectorForXYZ(window.documentStateForTest().level, outside.x, outside.y, outside.z) < 0 )
    {
        (void)triggerAnswering(insertAtCamera, QMessageBox::Ok);
        check(window.documentStateForTest().level.things.size() == thingsBefore
                && window.undoDepthForTest() == undoBeforeOutside,
            "insert thing at camera: a camera outside the level adds nothing");
    }
}

// One sector holding two disjoint cubes: the shape Build Sector separates.
editor::DocumentState twoCubesInOneSectorState()
{
    editor::DocumentState state = cubeState();
    level::levelCreateCube(state.level, 4.0, 0.0, 0.0, TVector::up, TVector::right);
    Sector second = std::move(state.level.sectors[1]);
    state.level.sectors.pop_back();
    Sector& merged = state.level.sectors[0];
    const auto offset = static_cast<std::int32_t>(merged.vertices.size());
    for ( Surface& face : second.surfaces )
    {
        for ( std::int32_t& index : face.vertices )
        {
            index += offset;
        }
        face.num = static_cast<std::int32_t>(merged.surfaces.size());
        merged.surfaces.push_back(std::move(face));
    }
    merged.vertices.insert(merged.vertices.end(), second.vertices.begin(), second.vertices.end());
    return state;
}

void runBuildSurfaceAndSector()
{
    {
        MainWindow window{};
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.activateSurfaceEditModeForTest();
        GlView* const view = window.glViewForTest();
        if ( view )
        {
            view->setSurfaceSubMode(GlView::SurfaceSubMode::Vertex);
        }
        editor::DocumentState state = cubeState();
        const std::optional<SurfaceRef> ceiling = firstSurfaceWithNormal(state.level, TVector::down);
        check(view && ceiling.has_value(), "build surface: fixture");
        if ( !view || !ceiling )
        {
            return;
        }
        Sector& sector = state.level.sectors[0];
        const std::vector<std::int32_t> corners = sector.surfaces[static_cast<std::size_t>(ceiling->surface)].vertices;
        sector.surfaces.erase(sector.surfaces.begin() + ceiling->surface);
        for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
        {
            sector.surfaces[i].num = static_cast<std::int32_t>(i);
        }
        // Select the open top's corners through the walls that still use them.
        for ( const std::int32_t corner : corners )
        {
            for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
            {
                const std::vector<std::int32_t>& loop = sector.surfaces[i].vertices;
                const auto found = std::find(loop.begin(), loop.end(), corner);
                if ( found != loop.end() )
                {
                    state.selection.surfaceVertices.insert(editor::SurfaceVertexRef{
                        SurfaceRef{ 0, static_cast<std::int32_t>(i) },
                        static_cast<std::int32_t>(found - loop.begin()) });
                    break;
                }
            }
        }
        window.setDocumentStateForTest(std::move(state));
        window.updateEditActionsForTest();

        QAction* const build = action(window, "surface.build");
        check(build != nullptr, "Surface menu exposes Build Surface/Sector");
        if ( !build )
        {
            return;
        }
        check(build->shortcut() == QKeySequence(QStringLiteral("Alt+B")), "Build keeps the legacy Alt+B shortcut");
        check(build->isEnabled(), "build surface: enabled with three or more vertices selected in Vertex mode");
        const std::size_t undoBefore = window.undoDepthForTest();
        const DialogLog log = triggerAnswering(build, QMessageBox::Ok);
        const Sector& closed = window.documentStateForTest().level.sectors[0];
        check(log.messageBoxes == 0 && closed.surfaces.size() == 6u, "build surface: closes the open cube");
        check(closed.surfaces.size() == 6u && close(closed.surfaces.back().normal.z, -1.0, 1e-9),
            "build surface: the new face points into the sector");
        check(window.undoDepthForTest() == undoBefore + 1u, "build surface: records exactly one undo step");
        check(window.undoDocumentForTest() && window.documentStateForTest().level.sectors[0].surfaces.size() == 5u,
            "build surface: undo removes the face");
    }

    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSurfaceEditModeForTest();
    editor::DocumentState state = twoCubesInOneSectorState();
    for ( std::int32_t i = 6; i < 12; ++i )
    {
        state.selection.surfaces.insert(SurfaceRef{ 0, i });
    }
    window.setDocumentStateForTest(std::move(state));
    window.updateEditActionsForTest();
    QAction* const build = action(window, "surface.build");
    if ( !build )
    {
        return;
    }
    check(build->isEnabled(), "build sector: enabled with four or more faces selected");
    const std::size_t undoBefore = window.undoDepthForTest();
    const DialogLog log = triggerAnswering(build, QMessageBox::Ok);
    const editor::DocumentState& split = window.documentStateForTest();
    check(log.messageBoxes == 0 && split.level.sectors.size() == 2u && split.level.sectors[0].surfaces.size() == 6u
            && split.level.sectors[1].surfaces.size() == 6u,
        "build sector: moves the selected faces into a new sector");
    bool selectsNewSector = split.selection.surfaces.size() == 6u;
    for ( const SurfaceRef& ref : split.selection.surfaces )
    {
        selectsNewSector = selectsNewSector && ref.sector == 1;
    }
    check(selectsNewSector, "build sector: the moved faces stay selected in their new sector");
    check(window.undoDepthForTest() == undoBefore + 1u, "build sector: records exactly one undo step");
    check(window.undoDocumentForTest() && window.documentStateForTest().level.sectors.size() == 1u
            && window.documentStateForTest().level.sectors[0].surfaces.size() == 12u,
        "build sector: undo restores the original sector");

    editor::DocumentState mixed = cubeState();
    level::levelCreateCube(mixed.level, 4.0, 0.0, 0.0, TVector::up, TVector::right);
    for ( std::int32_t i = 0; i < 2; ++i )
    {
        mixed.selection.surfaces.insert(SurfaceRef{ 0, i });
        mixed.selection.surfaces.insert(SurfaceRef{ 1, i });
    }
    window.setDocumentStateForTest(std::move(mixed));
    window.updateEditActionsForTest();
    const std::size_t undoMixed = window.undoDepthForTest();
    const DialogLog refused = triggerAnswering(build, QMessageBox::Ok);
    check(refused.messageBoxes == 1 && window.documentStateForTest().level.sectors.size() == 2u
            && window.undoDepthForTest() == undoMixed,
        "build sector: faces from two sectors are refused without editing the level");
}

void runCalculateAngle()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSurfaceEditModeForTest();
    editor::DocumentState state = cubeState();
    const std::optional<SurfaceRef> floor = firstSurfaceWithNormal(state.level, TVector::up);
    const std::optional<SurfaceRef> ceiling = firstSurfaceWithNormal(state.level, TVector::down);
    const Sector& sector = state.level.sectors[0];
    std::optional<SurfaceRef> wall{};
    for ( std::size_t i = 0; i < sector.surfaces.size() && !wall; ++i )
    {
        if ( std::abs(sector.surfaces[i].normal.z) < 0.01 )
        {
            wall = SurfaceRef{ 0, static_cast<std::int32_t>(i) };
        }
    }
    check(floor && ceiling && wall, "calculate angle: cube fixture has floor, ceiling and wall");
    if ( !floor || !ceiling || !wall )
    {
        return;
    }
    const Level levelCopy = state.level;
    state.selection.surfaces.insert(*floor);
    state.selection.surfaces.insert(*wall);
    window.setDocumentStateForTest(std::move(state));
    window.updateEditActionsForTest();

    QAction* const calculate = action(window, "surface.calculate_angle");
    check(calculate != nullptr && calculate->isEnabled(),
        "Surface menu exposes Calculate Angle, enabled for two surfaces");
    GlView* const view = window.glViewForTest();
    if ( !calculate || !view )
    {
        return;
    }

    const DialogLog measured = triggerAnswering(calculate, QMessageBox::Ok);
    const std::optional<QVector3D> origin = view->gridCenterOverrideValue();
    check(measured.messageBoxes == 0 && view->gridAlignedToBasis() && origin.has_value(),
        "calculate angle: aligns and centers the grid without a modal dialog");
    if ( origin )
    {
        const TVector point = TVector::Create(origin->x(), origin->y(), origin->z());
        const auto onPlane = [&levelCopy, &point](const SurfaceRef ref)
        {
            const Sector& s = levelCopy.sectors[0];
            const Surface& surface = s.surfaces[static_cast<std::size_t>(ref.surface)];
            const Vertex& v = s.vertices[static_cast<std::size_t>(surface.vertices[0])];
            return std::abs(surface.normal.dot(point.x - v.x, point.y - v.y, point.z - v.z)) < 1e-4;
        };
        check(onPlane(*floor) && onPlane(*wall), "calculate angle: grid origin lies on the shared edge");
    }
    const QVector3D gridNormal = view->gridPlaneNormal();
    const auto toQt = [](const TVector& v)
    {
        return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
    };
    const Sector& s = levelCopy.sectors[0];
    check(std::abs(QVector3D::dotProduct(gridNormal, toQt(s.surfaces[static_cast<std::size_t>(floor->surface)].normal))) < 1e-5f
            && std::abs(QVector3D::dotProduct(gridNormal, toQt(s.surfaces[static_cast<std::size_t>(wall->surface)].normal))) < 1e-5f,
        "calculate angle: grid normal runs along the shared edge");

    // Rotate offers the prepared Grid axis and angle.
    QString offeredAxis{};
    double offeredAngle = 0.0;
    int prompt = 0;
    QTimer driver{};
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
        {
            QWidget* const modal = QApplication::activeModalWidget();
            if ( auto* input = qobject_cast<QInputDialog*>(modal) )
            {
                if ( prompt++ == 0 )
                {
                    offeredAxis = input->textValue();
                    input->accept();
                }
                else
                {
                    offeredAngle = input->doubleValue();
                    input->reject();
                }
            }
            else if ( auto* dialog = qobject_cast<QDialog*>(modal) )
            {
                dialog->reject();
            }
        });
    driver.start();
    if ( QAction* const rotate = action(window, "transform.rotate") )
    {
        rotate->trigger();
    }
    driver.stop();
    check(offeredAxis == QStringLiteral("Grid"), "calculate angle: Rotate defaults to the Grid axis");
    check(close(offeredAngle, 90.0, 1e-6), "calculate angle: Rotate defaults to the measured angle");

    // Parallel surfaces have no shared edge.
    editor::DocumentState parallel = cubeState();
    parallel.selection.surfaces.insert(*floor);
    parallel.selection.surfaces.insert(*ceiling);
    window.setDocumentStateForTest(std::move(parallel));
    window.updateEditActionsForTest();
    view->resetGridOverrides();
    const DialogLog rejected = triggerAnswering(calculate, QMessageBox::Ok);
    check(rejected.messageBoxes == 1 && !view->gridAlignedToBasis(),
        "calculate angle: parallel surfaces are reported and leave the grid untouched");
}

void installDoorScript(MainWindow& window)
{
    const std::string script = "symbols\nthing door0\nfloat movespeed=8.0\nend\n";
    std::vector<std::byte> bytes{};
    for ( const char ch : script )
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    window.setPrimaryResourceOverrideForTest("00_door.cog", [bytes]()
        {
            return std::optional<std::vector<std::byte>>{ bytes };
        });
}

editor::DocumentState doorState(const double height)
{
    editor::DocumentState state = thingsAt({ TVector{ 0.25, 0.25, 0.25 }, TVector{ 0.5, 0.5, 0.25 } });
    ThingBox& box = state.level.things[1].bbox;
    box.x1 = -0.1;
    box.y1 = -0.1;
    box.z1 = -0.05;
    box.x2 = 0.1;
    box.y2 = 0.1;
    box.z2 = -0.05 + height;
    state.level.things[1].values.push_back(deserializeTplValue("health", "40"));
    state.selection.things = { 1 };
    return state;
}

QAction* prepareDoorWindow(MainWindow& window, const double height)
{
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();
    window.setDocumentStateForTest(doorState(height));
    if ( GlView* const view = window.glViewForTest() )
    {
        view->setSelectionState(-1, -1, 1, -1, window.documentStateForTest().selection);
    }
    window.updateEditActionsForTest();
    return action(window, "thing.make_door");
}

void runMakeDoor()
{
    MainWindow window{};
    QAction* const makeDoor = prepareDoorWindow(window, 0.4);
    installDoorScript(window);
    check(makeDoor && makeDoor->isEnabled(), "make door: enabled for the active Thing in Thing mode");
    if ( !makeDoor )
    {
        return;
    }
    const std::size_t undoBefore = window.undoDepthForTest();
    const DialogLog log = triggerAnswering(makeDoor, QMessageBox::Ok);
    const Level& level = window.documentStateForTest().level;
    check(log.messageBoxes == 0 && level.cogs.size() == 1u, "make door: adds the door COG without prompting");
    if ( level.cogs.size() == 1u )
    {
        check(level.cogs[0].name == "00_door.cog" && level.cogs[0].values.size() == 2u
                && level.cogs[0].values[0].intValue == 1,
            "make door: the COG's first symbol references the door Thing");
    }
    const Thing& door = level.things[1];
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double p = 0.0;
    double yw = 0.0;
    double r = 0.0;
    check(level::thingCountFrames(door) == 2u && door.values.size() == 4u
            && door.values[3].tryGetFrame(x, y, z, p, yw, r) && close(z, 0.65, 1e-6),
        "make door: the Thing gets flags, a frame count and closed/open frames one bbox height apart");
    check(level.things[0].values.empty(), "make door: other Things are untouched");
    check(window.undoDepthForTest() == undoBefore + 1u, "make door: records one undo step");
    check(window.undoForTest() && window.documentStateForTest().level.cogs.empty()
            && window.documentStateForTest().level.things[1].values.size() == 1u,
        "make door: one undo restores both the Thing values and the COG list");

    MainWindow missing{};
    QAction* const missingAction = prepareDoorWindow(missing, 0.4);
    const DialogLog missingLog = triggerAnswering(missingAction, QMessageBox::Ok);
    check(missingLog.messageBoxes == 1 && missing.documentStateForTest().level.cogs.empty()
            && missing.documentStateForTest().level.things[1].values.size() == 1u
            && missing.undoDepthForTest() == 0u,
        "make door: an unavailable 00_door.cog is reported and changes nothing");

    MainWindow flat{};
    QAction* const flatAction = prepareDoorWindow(flat, 0.0);
    installDoorScript(flat);
    const DialogLog flatLog = triggerAnswering(flatAction, QMessageBox::Ok);
    check(flatLog.messageBoxes == 1 && flat.documentStateForTest().level.cogs.empty()
            && flat.undoDepthForTest() == 0u,
        "make door: a flat bounding box is reported and changes nothing");

    MainWindow templated{};
    editor::DocumentState templatedState = doorState(0.0);
    templatedState.level.things[1].bbox = ThingBox{};
    ThingTemplate slab{};
    slab.name = "ghost";
    slab.bbox.z1 = 0.0;
    slab.bbox.z2 = 0.3;
    templatedState.level.templates.push_back(slab);
    templated.setAttribute(Qt::WA_DontShowOnScreen);
    templated.activateThingEditModeForTest();
    templated.setDocumentStateForTest(std::move(templatedState));
    installDoorScript(templated);
    templated.updateEditActionsForTest();
    (void)triggerAnswering(action(templated, "thing.make_door"), QMessageBox::Ok);
    const Thing& templatedDoor = templated.documentStateForTest().level.things[1];
    check(templatedDoor.values.size() == 4u && templatedDoor.values[3].tryGetFrame(x, y, z, p, yw, r)
            && close(z, 0.55, 1e-6),
        "make door: a Thing without its own bbox uses its template's bounding-box height");

    MainWindow already{};
    QAction* const alreadyAction = prepareDoorWindow(already, 0.4);
    installDoorScript(already);
    (void)triggerAnswering(alreadyAction, QMessageBox::Ok);
    const std::size_t undoAfterFirst = already.undoDepthForTest();
    const DialogLog alreadyLog = triggerAnswering(alreadyAction, QMessageBox::Ok);
    check(alreadyLog.messageBoxes == 1 && already.documentStateForTest().level.cogs.size() == 1u
            && already.undoDepthForTest() == undoAfterFirst,
        "make door: a Thing that already has frames is reported and changes nothing");
}

void runDeleteSavedShape()
{
    QTemporaryDir root{};
    const fs::path tpl = toPath(root.filePath(QStringLiteral("shapes.tpl")));
    const std::string block =
        "SEC %s\n"
        "dflt.cmp - 0 0 5 0 0 0 0 0\n"
        "VXS 4\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n"
        "SURFS 4\n"
        "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n 0 0 0 5 1 0 0 5 2 0 0 5\n"
        "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n 0 0 0 5 3 0 0 5 1 0 0 5\n"
        "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n 0 0 0 5 2 0 0 5 3 0 0 5\n"
        "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n 1 0 0 5 3 0 0 5 2 0 0 5\n"
        "END\n";
    const auto named = [&block](const std::string& name)
    {
        std::string text = block;
        text.replace(text.find("%s"), 2, name);
        return text;
    };
    {
        std::ofstream out(tpl, std::ios::binary);
        out << named("Keep Me") << "\n" << named("Sector Shape 1");
    }

    QString error{};
    const std::optional<std::size_t> removed =
        ui::sector::removeShapeFromTplFile(tpl, "sector shape 1", &error);
    check(removed && *removed == 1u && error.isEmpty(), "delete shape: removes the named block");
    check(readBytes(tpl) == named("Keep Me"), "delete shape: other shapes stay byte-exact");

    const std::optional<std::size_t> missing = ui::sector::removeShapeFromTplFile(tpl, "Nope", &error);
    check(missing && *missing == 0u && readBytes(tpl) == named("Keep Me"),
        "delete shape: a missing name leaves the file untouched");

    check(!ui::sector::removeShapeFromTplFile(root.filePath(QStringLiteral("absent.tpl")).toStdWString(), "Keep Me", &error)
            && !error.isEmpty(),
        "delete shape: an unreadable file is reported");

    error.clear();
    check(!ui::sector::deleteShapeTpl("Cube", &error) && error.contains(QStringLiteral("Built-in")),
        "delete shape: built-in primitives cannot be deleted");
}

} // namespace

void runPreviewTextureGamma()
{
    {
        QSettings settings = settingsStore();
        settings.remove(QStringLiteral("viewport/preview_texture_gamma"));
    }
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setDocumentStateForTest(cubeState());
    GlView* const view = window.glViewForTest();
    QAction* const brighter = action(window, "viewport.preview_gamma_up");
    QAction* const darker = action(window, "viewport.preview_gamma_down");
    QAction* const reset = action(window, "viewport.preview_gamma_reset");
    check(view && brighter && darker && reset, "preview gamma: View commands are exposed");
    if ( !view || !brighter || !darker || !reset )
    {
        return;
    }
    check(brighter->shortcut() == QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Plus)
            && darker->shortcut() == QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_Minus),
        "preview gamma: Ctrl+Num+ and Ctrl+Num- are the defaults, beside the Num+/Num- zoom commands");
    check(view->previewTextureGamma() == 1.0, "preview gamma: a fresh profile starts at the legacy 1.0");

    view->setRenderPipeline(GlView::RenderPipeline::Editor);
    window.updateEditActionsForTest();
    check(!brighter->isEnabled() && !darker->isEnabled(),
        "preview gamma: the commands are disabled outside Game Preview");

    view->setRenderPipeline(GlView::RenderPipeline::GamePreview);
    window.updateEditActionsForTest();
    check(brighter->isEnabled() && darker->isEnabled() && reset->isEnabled(),
        "preview gamma: the commands are enabled in Game Preview");
    const std::size_t undoBefore = window.undoDepthForTest();
    const bool dirtyBefore = window.hasUnsavedAuthoredLevelChangesForTest();
    brighter->trigger();
    brighter->trigger();
    darker->trigger();
    check(std::abs(view->previewTextureGamma() - 1.1) < 1e-9, "preview gamma: each command moves gamma by 0.1");
    check(window.undoDepthForTest() == undoBefore && window.hasUnsavedAuthoredLevelChangesForTest() == dirtyBefore,
        "preview gamma: a presentation setting adds no undo step and leaves the dirty state unchanged");
    {
        QSettings settings = settingsStore();
        check(std::abs(settings.value(QStringLiteral("viewport/preview_texture_gamma")).toDouble() - 1.1) < 1e-9,
            "preview gamma: the value persists like Pascal SED's 3D Preview Gamma option");
    }

    MainWindow reopened{};
    reopened.setAttribute(Qt::WA_DontShowOnScreen);
    GlView* const reopenedView = reopened.glViewForTest();
    check(reopenedView && std::abs(reopenedView->previewTextureGamma() - 1.1) < 1e-9,
        "preview gamma: a new window restores the stored value");

    reset->trigger();
    check(view->previewTextureGamma() == 1.0, "preview gamma: Reset returns to 1.0");
    {
        QSettings settings = settingsStore();
        settings.setValue(QStringLiteral("viewport/preview_texture_gamma"), QStringLiteral("garbage"));
    }
    MainWindow corrupt{};
    corrupt.setAttribute(Qt::WA_DontShowOnScreen);
    check(corrupt.glViewForTest() && corrupt.glViewForTest()->previewTextureGamma() == 1.0,
        "preview gamma: an unreadable stored value falls back to 1.0");
    {
        QSettings settings = settingsStore();
        settings.remove(QStringLiteral("viewport/preview_texture_gamma"));
    }
}

void runLegacyToolsTests()
{
    runPreviewTextureGamma();
    runRevertToSaved();
    runSaveKeepsBak();
    runGridMirrorAndScaleAlong();
    runNudgeAlongGridNormal();
    runCameraHelpers();
    runBuildSurfaceAndSector();
    runBackupCopy();
    runPlanarize();
    runFixSurfaceFlags();
    runCalculateAngle();
    runDeleteSavedShape();
    runMakeDoor();
}

} // namespace sed::app_tests
