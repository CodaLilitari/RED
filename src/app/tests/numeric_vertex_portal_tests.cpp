#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "ui/components/scrub_float_edit.hpp"

#include <QMetaObject>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace sed::app_tests
{
namespace
{

// Synthetic tetrahedra use the same exact reciprocal boundary as the agent
// vertex-position regression. The third tetrahedron is coincident but unjoined.
Sector tetrahedron(const Sector& prototype, const bool lower, const int id)
{
    auto sector = prototype;
    sector.id = id;
    sector.vertices = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, lower ? -2.0 : 2.0 } };
    const std::array<std::array<int, 3>, 4> faces = { { { 0, 1, 2 }, { 0, 3, 1 },
        { 0, 2, 3 }, { 1, 3, 2 } } };
    sector.surfaces.clear();
    for ( auto corners : faces )
    {
        if ( lower )
        {
            std::reverse(corners.begin(), corners.end());
        }
        auto surface = prototype.surfaces.front();
        surface.adjoin.reset();
        surface.vertices.assign(corners.begin(), corners.end());
        surface.texVertices.resize(3);
        surface.texVertices[0].u = -17;
        surface.texVertices[0].v = 9;
        surface.texVertices[1].u = 41;
        surface.texVertices[1].v = 13;
        surface.texVertices[2].u = 5;
        surface.texVertices[2].v = 97;
        for ( auto& corner : surface.texVertices )
        {
            corner.color = ColorF{ 0.125f, 0.25f, 0.5f, 0.75f };
        }
        surface.normal = level::surfaceCalcNormalNewell(sector, surface);
        sector.surfaces.push_back(std::move(surface));
    }
    return sector;
}

editor::DocumentState portalState()
{
    editor::DocumentState state{};
    state.level.kind = ProjectType::IJIM;
    state.level.layers = { "Layer0" };
    const Sector prototype = level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    state.level.sectors = { tetrahedron(prototype, false, 7011), tetrahedron(prototype, true, 7012),
        tetrahedron(prototype, false, 7013) };
    state.level.sectors[0].surfaces[0].adjoin = SurfaceRef{ 1, 0 };
    state.level.sectors[1].surfaces[0].adjoin = SurfaceRef{ 0, 0 };
    return state;
}

QTreeWidgetItem* propertyRow(QTreeWidgetItem* parent, const QString& name)
{
    if ( !parent )
    {
        return nullptr;
    }
    for ( int i = 0; i < parent->childCount(); ++i )
    {
        auto* child = parent->child(i);
        if ( child->text(0).trimmed() == name )
        {
            return child;
        }
        if ( auto* found = propertyRow(child, name) )
        {
            return found;
        }
    }
    return nullptr;
}

ui::ScrubFloatEdit* vertexAxis(MainWindow& window, const SurfaceRef ref, const std::int32_t corner,
    const QString& axis)
{
    window.activateSurfaceEditModeForTest();
    window.activateSurfaceVertexSubModeForTest();
    auto* view = window.glViewForTest();
    if ( !view )
    {
        return nullptr;
    }
    const auto& sector = window.documentStateForTest().level.sectors[ref.sector];
    const auto& vertex = sector.vertices[sector.surfaces[ref.surface].vertices[corner]];
    const QVector3D point(static_cast<float>(vertex.x), static_cast<float>(vertex.y), static_cast<float>(vertex.z));
    const bool picked = QMetaObject::invokeMethod(view, "selectionPicked", Qt::DirectConnection,
        Q_ARG(std::int32_t, ref.sector), Q_ARG(std::int32_t, ref.surface), Q_ARG(std::int32_t, -1),
        Q_ARG(std::int32_t, -1), Q_ARG(Qt::KeyboardModifiers, Qt::NoModifier))
        && QMetaObject::invokeMethod(view, "surfaceClicked", Qt::DirectConnection, Q_ARG(QVector3D, point),
            Q_ARG(std::int32_t, ref.sector), Q_ARG(std::int32_t, ref.surface), Q_ARG(QPoint, QPoint(12, 12)),
            Q_ARG(Qt::KeyboardModifiers, Qt::NoModifier));
    if ( !picked )
    {
        return nullptr;
    }
    auto* properties = window.findChild<QTreeWidget*>(QStringLiteral("propertiesTree"));
    if ( !properties )
    {
        return nullptr;
    }
    auto* position = propertyRow(properties->invisibleRootItem(), QStringLiteral("Position"));
    // Surface properties use flat top-level rows with visual indentation;
    // addChild(pos, ...) does not create QTreeWidgetItem children of pos.
    const int positionIndex = properties->indexOfTopLevelItem(position);
    if ( positionIndex < 0 )
    {
        return nullptr;
    }
    for ( int offset = 1; offset <= 3 && positionIndex + offset < properties->topLevelItemCount(); ++offset )
    {
        auto* row = properties->topLevelItem(positionIndex + offset);
        if ( row->text(0).trimmed() == axis )
        {
            return qobject_cast<ui::ScrubFloatEdit*>(properties->itemWidget(row, 1));
        }
    }
    return nullptr;
}

void commitPortalVertex(const bool scrub)
{
    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->setDocumentStateForTest(portalState());
    auto* editor = vertexAxis(*window, { 0, 0 }, 1, QStringLiteral("X"));
    check(editor && editor->value() == 2.0, "numeric portal fixture selects the actual vertex X property");
    if ( !editor )
    {
        return;
    }
    const auto baseline = window->documentStateForTest().level;
    const auto before = assets::saveToSedBytes(baseline);
    const auto undoDepth = window->undoDepthForTest();
    if ( scrub )
    {
        editor->scrubFinished(2.5);
    }
    else
    {
        editor->valueCommitted(2.5);
    }
    // Commit rebuilds the property widgets; do not use editor after its signal.
    const auto edited = window->documentStateForTest().level;
    check(edited.sectors[0].vertices[1].x == 2.5 && edited.sectors[1].vertices[1].x == 2.5,
        "numeric vertex commit moves both reciprocal portal copies exactly");
    check(edited.sectors[2].vertices[1].x == 2.0,
        "numeric vertex commit leaves unjoined coincident geometry independent");
    check(window->undoDepthForTest() == undoDepth + 1,
        "numeric portal edit publishes one undo transaction");
    auto restored = edited;
    for ( std::size_t s = 0; s < restored.sectors.size(); ++s )
    {
        restored.sectors[s].vertices = baseline.sectors[s].vertices;
        for ( std::size_t f = 0; f < restored.sectors[s].surfaces.size(); ++f )
        {
            restored.sectors[s].surfaces[f].normal = baseline.sectors[s].surfaces[f].normal;
        }
    }
    check(assets::saveToSedBytes(restored) == before,
        "numeric portal edit preserves IDs, references, corner order and authored UV/light values");
    const auto after = assets::saveToSedBytes(edited);
    check(window->undoForTest() && assets::saveToSedBytes(window->documentStateForTest().level) == before,
        "one undo restores every numeric portal edit endpoint");
    check(window->redoForTest() && assets::saveToSedBytes(window->documentStateForTest().level) == after,
        "one redo restores the complete numeric portal edit");
}

void refuseInvalidVertexEdit(const bool brokenPortal)
{
    auto state = portalState();
    if ( brokenPortal )
    {
        state.level.sectors[1].surfaces[0].adjoin.reset();
    }
    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->setDocumentStateForTest(std::move(state));
    auto* editor = vertexAxis(*window, brokenPortal ? SurfaceRef{ 0, 0 } : SurfaceRef{ 0, 1 }, 1,
        brokenPortal ? QStringLiteral("X") : QStringLiteral("Z"));
    check(editor != nullptr, "invalid numeric vertex fixture exposes its actual coordinate editor");
    if ( !editor )
    {
        return;
    }
    const auto before = assets::saveToSedBytes(window->documentStateForTest().level);
    const auto depth = window->undoDepthForTest();
    editor->valueCommitted(brokenPortal ? 2.5 : -2.0);
    check(assets::saveToSedBytes(window->documentStateForTest().level) == before,
        brokenPortal ? "numeric vertex edit refuses a broken reciprocal portal without mutation"
                     : "numeric vertex edit refuses sector inversion without mutation");
    check(window->undoDepthForTest() == depth,
        "refused numeric vertex edit creates no undo transaction");
}

void refuseReadOnlyPortalPeer(const bool hidden)
{
    auto state = portalState();
    state.level.layers.push_back("ReadOnlyPeer");
    state.level.sectors[1].layer = 1;
    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->setDocumentStateForTest(std::move(state));
    window->setLayerVisualStateForTest({ true, !hidden }, { false, !hidden });
    auto* editor = vertexAxis(*window, { 0, 0 }, 1, QStringLiteral("X"));
    check(editor != nullptr, "editable portal endpoint exposes its numeric coordinate beside a read-only peer");
    if ( !editor )
    {
        return;
    }
    const auto before = assets::saveToSedBytes(window->documentStateForTest().level);
    const auto depth = window->undoDepthForTest();
    editor->valueCommitted(2.5);
    check(assets::saveToSedBytes(window->documentStateForTest().level) == before
            && window->undoDepthForTest() == depth,
        hidden ? "numeric portal edit refuses a hidden reciprocal endpoint without tearing the portal"
               : "numeric portal edit refuses a locked reciprocal endpoint without tearing the portal");
}

} // namespace

void runNumericVertexPortalTests()
{
    commitPortalVertex(false);
    commitPortalVertex(true);
    refuseInvalidVertexEdit(true);
    refuseInvalidVertexEdit(false);
    refuseReadOnlyPortalPeer(false);
    refuseReadOnlyPortalPeer(true);
}

} // namespace sed::app_tests
