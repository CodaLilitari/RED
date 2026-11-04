#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "ui/sector/sector_shape_utils.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QInputDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#include <filesystem>
#include <fstream>
#include <functional>
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

void runLegacyToolsTests()
{
    runRevertToSaved();
    runBackupCopy();
    runPlanarize();
    runCalculateAngle();
    runDeleteSavedShape();
}

} // namespace sed::app_tests
