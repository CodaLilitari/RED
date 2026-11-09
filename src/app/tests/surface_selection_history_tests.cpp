#include "test_common.hpp"

#include <QAction>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"

namespace sed::app_tests
{
namespace
{

constexpr SurfaceRef kSplitOwner{0, 0};

editor::DocumentState cubeState()
{
    editor::DocumentState state{};
    state.level.kind = ProjectType::IJIM;
    state.level.layers.emplace_back("Layer0");
    Sector& sector = level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    sector.layer = 0;
    return state;
}

bool splitSurfaceEdgeAtMidpoint(editor::DocumentState& state, const SurfaceRef owner, const std::int32_t localEdge)
{
    if ( owner.sector < 0 || owner.surface < 0 || localEdge < 0 ||
         static_cast<std::size_t>(owner.sector) >= state.level.sectors.size() )
    {
        return false;
    }
    const Sector& sector = state.level.sectors[static_cast<std::size_t>(owner.sector)];
    if ( static_cast<std::size_t>(owner.surface) >= sector.surfaces.size() )
    {
        return false;
    }
    const Surface& surface = sector.surfaces[static_cast<std::size_t>(owner.surface)];
    if ( static_cast<std::size_t>(localEdge) >= surface.vertices.size() )
    {
        return false;
    }

    const std::size_t first = static_cast<std::size_t>(localEdge);
    const std::size_t second = (first + 1u) % surface.vertices.size();
    const Vertex& a = sector.vertices[static_cast<std::size_t>(surface.vertices[first])];
    const Vertex& b = sector.vertices[static_cast<std::size_t>(surface.vertices[second])];
    TVector planeNormal{b.x - a.x, b.y - a.y, b.z - a.z};
    planeNormal.Normalize();
    const TVector midpoint{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
    return level::levelCleaveEdge(state.level, owner, localEdge, planeNormal, midpoint.x, midpoint.y, midpoint.z);
}

std::optional<std::int32_t> findLocalVertex(const Level& level, const SurfaceRef owner, const std::int32_t sectorVertex)
{
    const Surface& surface =
        level.sectors[static_cast<std::size_t>(owner.sector)].surfaces[static_cast<std::size_t>(owner.surface)];
    const auto found = std::find(surface.vertices.begin(), surface.vertices.end(), sectorVertex);
    if ( found == surface.vertices.end() )
    {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(std::distance(surface.vertices.begin(), found));
}

std::pair<std::int32_t, std::int32_t> edgeEndpoints(const Level& level, const SurfaceRef owner,
                                                    const std::int32_t localEdge)
{
    const Surface& surface =
        level.sectors[static_cast<std::size_t>(owner.sector)].surfaces[static_cast<std::size_t>(owner.surface)];
    const std::size_t first = static_cast<std::size_t>(localEdge);
    const std::size_t second = (first + 1u) % surface.vertices.size();
    return {surface.vertices[first], surface.vertices[second]};
}

QAction* historyAction(MainWindow& window, const char* name)
{
    window.updateEditActionsForTest();
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

bool isOnlySurface(const editor::Selection& selection, const SurfaceRef expected)
{
    return selection.surfaces.size() == 1u && selection.surfaces.contains(expected);
}

bool isOnlySurfaceVertex(const editor::Selection& selection, const editor::SurfaceVertexRef expected)
{
    return selection.surfaceVertices.size() == 1u && selection.surfaceVertices.contains(expected);
}

} // namespace

void runSurfaceSelectionHistoryTests()
{
    {
        // Split edge zero so every old corner after it changes local index.
        // Known structural history invalidates component context and returns to
        // Face. Neither an old corner nor the transient midpoint may survive as
        // an exact positional reference into the restored topology.
        auto window = std::make_unique<MainWindow>();
        window->setAttribute(Qt::WA_DontShowOnScreen);
        editor::DocumentState state = cubeState();
        const Surface& beforeSurface = state.level.sectors[0].surfaces[0];
        const std::int32_t survivingSectorVertex = beforeSurface.vertices.back();
        const std::int32_t beforeSurvivingLocal = static_cast<std::int32_t>(beforeSurface.vertices.size() - 1u);
        const std::size_t beforeSectorVertexCount = state.level.sectors[0].vertices.size();

        window->setDocumentStateForTest(std::move(state));
        window->activateSurfaceEditModeForTest();
        window->activateSurfaceVertexSubModeForTest();
        const bool editApplied = window->applyStructuralHistoryEditForTest(
            [](editor::DocumentState& edited) { return splitSurfaceEdgeAtMidpoint(edited, kSplitOwner, 0); });
        GlView* view = window->glViewForTest();
        if ( view )
        {
            view->notifyLevelGeometryChanged();
        }

        const Level& afterLevel = window->documentStateForTest().level;
        const auto afterSurvivingLocal = findLocalVertex(afterLevel, kSplitOwner, survivingSectorVertex);
        const std::int32_t addedSectorVertex = static_cast<std::int32_t>(beforeSectorVertexCount);
        const auto addedLocal = findLocalVertex(afterLevel, kSplitOwner, addedSectorVertex);

        editor::Selection& selection = window->mutableDocumentStateForTest().selection;
        selection.clear();
        if ( afterSurvivingLocal && addedLocal && view )
        {
            selection.surfaceVertices.insert(editor::SurfaceVertexRef{kSplitOwner, *afterSurvivingLocal});
            selection.surfaceVertices.insert(editor::SurfaceVertexRef{kSplitOwner, *addedLocal});
            selection.surfaces.insert(kSplitOwner);
            view->setSelectionState(0, 0, -1, -1, selection);
            view->setActiveSurfaceVertexHint(0, 0, *addedLocal);
        }

        QAction* undo = historyAction(*window, "edit.undo");
        const bool undoReady = undo && undo->isEnabled();
        if ( undoReady )
        {
            undo->trigger();
        }
        const editor::Selection& undoSelection = window->documentStateForTest().selection;
        const bool undoInvalidated =
            window->surfaceSelectionModeForTest() == window->surfaceFaceModeValueForTest() &&
            undoSelection.surfaceVertices.empty() && isOnlySurface(undoSelection, kSplitOwner) && view &&
            !view->activeSurfaceVertexForTest() && !view->activeSurfaceEdgeForTest() &&
            view->selectedSurfaceEdgeCountForTest() == 0u &&
            view->selectedSurfaceRef() == std::optional<SurfaceRef>{kSplitOwner} &&
            window->documentStateForTest().level.sectors[0].vertices.size() == beforeSectorVertexCount;

        // Re-enter Vertex before Redo so it independently crosses the component
        // invalidation boundary rather than inheriting Undo's already safe Face mode.
        window->activateSurfaceVertexSubModeForTest();
        editor::Selection& beforeRedoSelection = window->mutableDocumentStateForTest().selection;
        beforeRedoSelection.surfaceVertices.insert(editor::SurfaceVertexRef{kSplitOwner, beforeSurvivingLocal});
        if ( view )
        {
            view->setSelectionState(0, 0, -1, -1, beforeRedoSelection);
            view->setActiveSurfaceVertexHint(0, 0, beforeSurvivingLocal);
        }

        QAction* redo = historyAction(*window, "edit.redo");
        const bool redoReady = redo && redo->isEnabled();
        if ( redoReady )
        {
            redo->trigger();
        }
        const editor::Selection& redoSelection = window->documentStateForTest().selection;
        check(editApplied && afterSurvivingLocal && addedLocal && *afterSurvivingLocal != beforeSurvivingLocal &&
                  undoReady && undoInvalidated && redoReady && redoSelection.surfaceVertices.empty() &&
                  window->surfaceSelectionModeForTest() == window->surfaceFaceModeValueForTest() &&
                  isOnlySurface(redoSelection, kSplitOwner) && view &&
                  !view->activeSurfaceVertexForTest() && !view->activeSurfaceEdgeForTest() &&
                  view->selectedSurfaceEdgeCountForTest() == 0u &&
                  view->selectedSurfaceRef() == std::optional<SurfaceRef>{kSplitOwner} &&
                  window->documentStateForTest().level.sectors[0].vertices.size() == beforeSectorVertexCount + 1u,
              "Topology Undo/Redo returns Surface Vertex to Face, retains its valid owner, "
              "and clears every exact component without retargeting a local vertex");
    }

    {
        // A closing-edge split keeps local zero stable but changes the old closing
        // local's endpoints. Keep a second, unaffected face selected to verify
        // that structural Undo/Redo clears both exact edges while preserving
        // their valid committed Face owners.
        auto window = std::make_unique<MainWindow>();
        window->setAttribute(Qt::WA_DontShowOnScreen);
        editor::DocumentState state = cubeState();
        const Surface& beforeSplit = state.level.sectors[0].surfaces[0];
        const std::int32_t closingEdge = static_cast<std::int32_t>(beforeSplit.vertices.size() - 1u);
        const auto beforeClosingEndpoints = edgeEndpoints(state.level, kSplitOwner, closingEdge);

        window->setDocumentStateForTest(std::move(state));
        window->activateSurfaceEditModeForTest();
        window->activateSurfaceEdgeSubModeForTest();
        const bool editApplied = window->applyStructuralHistoryEditForTest(
            [closingEdge](editor::DocumentState& edited)
            { return splitSurfaceEdgeAtMidpoint(edited, kSplitOwner, closingEdge); });
        GlView* view = window->glViewForTest();
        if ( view )
        {
            view->notifyLevelGeometryChanged();
        }

        const Level& afterLevel = window->documentStateForTest().level;
        const Sector& afterSector = afterLevel.sectors[0];
        const std::int32_t addedSectorVertex = static_cast<std::int32_t>(afterSector.vertices.size() - 1u);
        std::optional<SurfaceRef> stableOwner{};
        for ( std::size_t surfaceIndex = 1; surfaceIndex < afterSector.surfaces.size(); ++surfaceIndex )
        {
            const Surface& surface = afterSector.surfaces[surfaceIndex];
            if ( std::find(surface.vertices.begin(), surface.vertices.end(), addedSectorVertex) ==
                 surface.vertices.end() )
            {
                stableOwner = SurfaceRef{0, static_cast<std::int32_t>(surfaceIndex)};
                break;
            }
        }
        const bool staleEdgeChanged = edgeEndpoints(afterLevel, kSplitOwner, closingEdge) != beforeClosingEndpoints;
        const auto installEdgeSelection = [&]()
        {
            window->activateSurfaceEdgeSubModeForTest();
            editor::Selection& selection = window->mutableDocumentStateForTest().selection;
            selection.clear();
            if ( !stableOwner || !view )
            {
                return;
            }
            selection.surfaces.insert(*stableOwner);
            selection.surfaces.insert(kSplitOwner);
            view->setSelectionState(0, 0, -1, -1, selection);
            view->setSelectedSurfaceEdgesForTest({{*stableOwner, 0}, {kSplitOwner, closingEdge}}, 1u);
        };
        installEdgeSelection();
        const auto faceContextIsSafe = [&]()
        {
            const editor::Selection& current = window->documentStateForTest().selection;
            return stableOwner && view &&
                   window->surfaceSelectionModeForTest() == window->surfaceFaceModeValueForTest() &&
                   current.surfaceVertices.empty() && current.surfaces.size() == 2u &&
                   current.surfaces.contains(kSplitOwner) && current.surfaces.contains(*stableOwner) &&
                   view->selectedSurfaceEdgeCountForTest() == 0u && !view->activeSurfaceEdgeForTest() &&
                   !view->activeSurfaceVertexForTest() &&
                   view->selectedSurfaceRef() == std::optional<SurfaceRef>{kSplitOwner};
        };

        QAction* undo = historyAction(*window, "edit.undo");
        const bool undoReady = undo && undo->isEnabled();
        if ( undoReady )
        {
            undo->trigger();
        }
        const bool undoInvalidated = faceContextIsSafe() &&
                                     edgeEndpoints(window->documentStateForTest().level, kSplitOwner, closingEdge) ==
                                         beforeClosingEndpoints;

        // Reinstall a transient edge in the pre-edit topology so Redo exercises
        // the inverse endpoint mismatch instead of merely carrying Undo's result.
        installEdgeSelection();
        QAction* redo = historyAction(*window, "edit.redo");
        const bool redoReady = redo && redo->isEnabled();
        if ( redoReady )
        {
            redo->trigger();
        }
        const bool redoInvalidated = faceContextIsSafe() &&
                                     edgeEndpoints(window->documentStateForTest().level, kSplitOwner, closingEdge) !=
                                         beforeClosingEndpoints;

        check(editApplied && stableOwner && staleEdgeChanged && undoReady && undoInvalidated && redoReady &&
                  redoInvalidated,
              "Topology Undo/Redo returns Surface Edge to Face, retains valid owners, "
              "and clears transient and surviving exact edge sidecars");
    }

    for ( const bool vertexMode : {true, false} )
    {
        // Geometry-only translation retains the same topology and must preserve
        // compatible exact component selections in both history directions.
        auto window = std::make_unique<MainWindow>();
        window->setAttribute(Qt::WA_DontShowOnScreen);
        window->setDocumentStateForTest(cubeState());
        window->activateSurfaceEditModeForTest();
        if ( vertexMode )
        {
            window->activateSurfaceVertexSubModeForTest();
        }
        else
        {
            window->activateSurfaceEdgeSubModeForTest();
        }
        const int componentMode = window->surfaceSelectionModeForTest();
        const editor::SurfaceVertexRef exactVertex{kSplitOwner, 1};
        editor::Selection& selection = window->mutableDocumentStateForTest().selection;
        selection.clear();
        selection.surfaces.insert(kSplitOwner);
        if ( vertexMode )
        {
            selection.surfaceVertices.insert(exactVertex);
        }
        GlView* view = window->glViewForTest();
        if ( view )
        {
            view->setSelectionState(0, 0, -1, -1, selection);
            if ( vertexMode )
            {
                view->setActiveSurfaceVertexHint(0, 0, exactVertex.localVertex);
            }
            else
            {
                view->setSelectedSurfaceEdgesForTest({{kSplitOwner, 1}}, 0u);
            }
        }
        const double beforeX = window->documentStateForTest().level.sectors[0].vertices[0].x;
        window->pushSectorSubsetUndoForTitleForTest({0});
        for ( Vertex& vertex : window->mutableDocumentStateForTest().level.sectors[0].vertices )
        {
            vertex.x += 2.0;
        }
        if ( view )
        {
            view->notifyLevelGeometryChanged();
        }
        const auto compatibleContextIsPreserved = [&]()
        {
            const editor::Selection& current = window->documentStateForTest().selection;
            if ( !view || window->surfaceSelectionModeForTest() != componentMode ||
                 !isOnlySurface(current, kSplitOwner) ||
                 view->selectedSurfaceRef() != std::optional<SurfaceRef>{kSplitOwner} )
            {
                return false;
            }
            return vertexMode
                       ? isOnlySurfaceVertex(current, exactVertex) &&
                             view->activeSurfaceVertexForTest() ==
                                 std::optional<editor::SurfaceVertexRef>{exactVertex}
                       : current.surfaceVertices.empty() && view->selectedSurfaceEdgeCountForTest() == 1u &&
                             view->activeSurfaceEdgeForTest() ==
                                 std::optional<std::pair<SurfaceRef, std::int32_t>>{std::pair{kSplitOwner, 1}};
        };
        QAction* undo = historyAction(*window, "edit.undo");
        const bool undoReady = undo && undo->isEnabled();
        if ( undoReady )
        {
            undo->trigger();
        }
        const bool undoPreserved = compatibleContextIsPreserved() &&
                                   window->documentStateForTest().level.sectors[0].vertices[0].x == beforeX;
        QAction* redo = historyAction(*window, "edit.redo");
        const bool redoReady = redo && redo->isEnabled();
        if ( redoReady )
        {
            redo->trigger();
        }
        check(undoReady && undoPreserved && redoReady && compatibleContextIsPreserved() &&
                  window->documentStateForTest().level.sectors[0].vertices[0].x == beforeX + 2.0,
              vertexMode ? "Geometry-only Move Undo/Redo preserves exact Surface Vertex selection and active identity"
                         : "Geometry-only Move Undo/Redo preserves exact Surface Edge selection and active identity");
    }
}

} // namespace sed::app_tests
