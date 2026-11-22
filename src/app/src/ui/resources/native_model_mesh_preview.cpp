#include "ui/resources/native_model_mesh_preview.hpp"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace sed::ui::resources
{
namespace
{
QPointF project(const assets::Model3doVertex& vertex)
{
    // Top-oblique mesh-local view. X/Y use equal scale; Z rises diagonally.
    // Lerp keeps the projection finite even near the coordinate limits.
    return {std::lerp(vertex.x, vertex.z, 0.25), std::lerp(vertex.y, vertex.z, 0.25)};
}

template<class Visit>
void forEachDrawableFace(const assets::Model3doMesh& mesh, Visit&& visit)
{
    for ( const auto& face : mesh.faces )
    {
        if ( face.vertices.size() >= 2 && std::all_of(face.vertices.begin(), face.vertices.end(),
            [&](auto index) { return index >= 0 && static_cast<std::size_t>(index) < mesh.vertices.size(); }) )
        {
            visit(face.vertices);
        }
    }
}
} // namespace

NativeModelMeshPreview::NativeModelMeshPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("nativeModelMeshPreview"));
    setAccessibleName(QStringLiteral("Selected source mesh wireframe"));
    setToolTip(QStringLiteral("Mesh-local wireframe. X runs right, Y runs up, and Z rises diagonally."));
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void NativeModelMeshPreview::setMesh(assets::NativeModelDocument snapshot, std::size_t geoset,
    std::size_t sourceMesh, bool fit)
{
    const auto geosets = snapshot.geosets();
    if ( geoset >= geosets.size() || sourceMesh >= geosets[geoset].meshes.size() )
    {
        clear();
        return;
    }
    const bool targetChanged = !snapshot_ || geoset != geoset_ || sourceMesh != sourceMesh_;
    snapshot_ = std::move(snapshot);
    geoset_ = geoset;
    sourceMesh_ = sourceMesh;
    if ( fit || targetChanged ) reframe();
    else update();
}

void NativeModelMeshPreview::clear()
{
    snapshot_.reset();
    center_ = {};
    halfExtent_ = {};
    update();
}

void NativeModelMeshPreview::reframe()
{
    if ( !snapshot_ ) return;
    const auto& mesh = snapshot_->geosets()[geoset_].meshes[sourceMesh_];
    bool found = false;
    QPointF low, high;
    forEachDrawableFace(mesh, [&](const auto& indices)
    {
        for ( const auto index : indices )
        {
            const auto point = project(mesh.vertices[static_cast<std::size_t>(index)]);
            if ( !found )
            {
                low = high = point;
                found = true;
            }
            else
            {
                low.setX(std::min(low.x(), point.x()));
                low.setY(std::min(low.y(), point.y()));
                high.setX(std::max(high.x(), point.x()));
                high.setY(std::max(high.y(), point.y()));
            }
        }
    });
    center_ = {std::midpoint(low.x(), high.x()), std::midpoint(low.y(), high.y())};
    halfExtent_ = high - center_;
    update();
}

void NativeModelMeshPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if ( !snapshot_ ) return;
    const auto& mesh = snapshot_->geosets()[geoset_].meshes[sourceMesh_];
    const double extent = std::max(halfExtent_.x(), halfExtent_.y());
    if ( extent <= 0 ) return;
    const double visibleWidth = width() * 0.44;
    const double visibleHeight = height() * 0.44;
    const double pixels = 1.0 / std::max((halfExtent_.x() / extent) / visibleWidth,
        (halfExtent_.y() / extent) / visibleHeight);
    const QPointF middle(width() * 0.5, height() * 0.5);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().text().color(), 1));
    forEachDrawableFace(mesh, [&](const auto& indices)
    {
        QPolygonF polygon;
        polygon.reserve(static_cast<qsizetype>(indices.size()));
        for ( const auto index : indices )
        {
            const auto delta = project(mesh.vertices[static_cast<std::size_t>(index)]) - center_;
            const QPointF screen(middle.x() + (delta.x() / extent) * pixels,
                middle.y() - (delta.y() / extent) * pixels);
            if ( !std::isfinite(screen.x()) || !std::isfinite(screen.y()) ) return;
            polygon.push_back(screen);
        }
        painter.drawPolygon(polygon);
    });
}

} // namespace sed::ui::resources
