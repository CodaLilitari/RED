#include "native_model_uv_preview.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <utility>

namespace sed::ui::resources
{
namespace
{
constexpr std::size_t kCornerLimit = 262144;

std::optional<QPointF> uvAt(const assets::Model3doMesh& mesh, std::int32_t row)
{
    if ( row < 0 || static_cast<std::size_t>(row) >= mesh.texVertices.size() ) return {};
    const auto point = mesh.texVertices[static_cast<std::size_t>(row)];
    if ( !std::isfinite(point.u) || !std::isfinite(point.v) ) return {};
    return QPointF(point.u, point.v);
}

void include(std::optional<QRectF>& bounds, QPointF point)
{
    if ( !bounds )
    {
        bounds.emplace(point, point);
        return;
    }
    bounds->setCoords(std::min(bounds->left(), point.x()), std::min(bounds->top(), point.y()),
        std::max(bounds->right(), point.x()), std::max(bounds->bottom(), point.y()));
}

// Clip in source UV units. Keep the original native float endpoints for each
// intersection: their products fit exactly in double (at most 48 mantissa bits).
bool clipEdge(QPointF& a, QPointF& b, QPointF center, const QRectF& visible)
{
    const QPointF start = a;
    const QPointF end = b;
    a -= center;
    b -= center;
    const auto clip = [&](bool horizontal, bool lower, double edge)
    {
        const double x = horizontal ? a.x() : a.y();
        const double y = horizontal ? b.x() : b.y();
        const bool outsideA = lower ? x < edge : x > edge;
        const bool outsideB = lower ? y < edge : y > edge;
        if ( outsideA && outsideB ) return false;
        if ( outsideA == outsideB ) return true;
        const double x0 = horizontal ? start.x() : start.y();
        const double y0 = horizontal ? start.y() : start.x();
        const double x1 = horizontal ? end.x() : end.y();
        const double y1 = horizontal ? end.y() : end.x();
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double cx = horizontal ? center.x() : center.y();
        const double cy = horizontal ? center.y() : center.x();
        const double slope = dy / dx;
        // Work in offsets from the frame center: its absolute U or V may
        // be too large to add one visible pixel in double precision. Anchor
        // the line at the nearest of its endpoints or its axis intercept.
        const double d0 = std::abs(cx - x0);
        const double d1 = std::abs(cx - x1);
        double atCenter;
        if ( std::abs(cx) < std::min(d0, d1) )
        {
            const double intercept = (y0 * x1 - y1 * x0) / dx;
            atCenter = std::fma(cx, slope, -cy) + intercept;
        }
        else if ( d0 <= d1 )
        {
            atCenter = std::fma(cx - x0, slope, y0 - cy);
        }
        else
        {
            atCenter = std::fma(cx - x1, slope, y1 - cy);
        }
        const double other = std::fma(edge, slope, atCenter);
        (outsideA ? a : b) = horizontal ? QPointF(edge, other) : QPointF(other, edge);
        return true;
    };
    return clip(true, true, visible.left()) && clip(true, false, visible.right())
        && clip(false, true, visible.top()) && clip(false, false, visible.bottom());
}
} // namespace

NativeModelUvPreview::NativeModelUvPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("nativeModelUvPreview"));
    setAccessibleName(tr("Source mesh UV inspection"));
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

const assets::Model3doMesh* NativeModelUvPreview::mesh() const
{
    if ( !document_ ) return nullptr;
    const auto geosets = document_->geosets();
    if ( selection_.geoset >= geosets.size() ) return nullptr;
    const auto& meshes = geosets[selection_.geoset].meshes;
    return selection_.sourceMesh < meshes.size() ? &meshes[selection_.sourceMesh] : nullptr;
}

void NativeModelUvPreview::setModel(assets::NativeModelDocument document, editor::NativeModelSelection selection,
    int materialSlot, bool fit)
{
    const bool targetChanged = !document_ || selection.geoset != selection_.geoset
        || selection.sourceMesh != selection_.sourceMesh || materialSlot != slot_;
    document_ = std::move(document);
    selection_ = selection;
    slot_ = materialSlot;
    if ( targetChanged )
    {
        image_ = {};
        sourceSize_ = {};
    }
    corners_.clear();
    totalUses_ = 0;
    marker_.reset();
    overlayBounds_.reset();
    error_.clear();
    const auto* target = mesh();
    available_ = target != nullptr;
    if ( !target )
    {
        clear();
        return;
    }
    std::size_t visited = 0;
    for ( const auto& face : target->faces )
    {
        if ( face.texVertices.size() > kCornerLimit - visited )
        {
            available_ = false;
            error_ = tr("UV overlay unavailable: mesh exceeds the 262,144 face-corner limit.");
            break;
        }
        visited += face.texVertices.size();
    }
    if ( available_ )
    {
        for ( std::size_t i = 0; i < target->faces.size(); ++i )
        {
            const auto& face = target->faces[i];
            for ( std::size_t j = 0; j < face.texVertices.size(); ++j )
            {
                const auto row = face.texVertices[j];
                const auto point = uvAt(*target, row);
                if ( !point ) continue;
                const bool selected = selection.textureVertex && static_cast<std::size_t>(row) == *selection.textureVertex;
                if ( selected ) ++totalUses_;
                if ( face.material != slot_ ) continue;
                include(overlayBounds_, *point);
                if ( selected ) corners_.push_back({i,j});
            }
        }
        if ( selection.textureVertex && *selection.textureVertex < target->texVertices.size()
            && (!corners_.empty() || totalUses_ == 0) )
        {
            marker_ = uvAt(*target, static_cast<std::int32_t>(*selection.textureVertex));
            if ( marker_ ) include(overlayBounds_, *marker_);
        }
    }
    if ( fit || targetChanged ) reframe();
    else update();
}

void NativeModelUvPreview::setMaterial(QImage image, QSize sourceSize)
{
    const bool dimensionsChanged = sourceSize_ != sourceSize;
    sourceSize_ = sourceSize;
    image_ = std::move(image);
    if ( dimensionsChanged ) reframe();
    else update();
}

void NativeModelUvPreview::clear()
{
    document_.reset();
    corners_.clear();
    marker_.reset();
    overlayBounds_.reset();
    image_ = {};
    sourceSize_ = {};
    frame_ = {};
    available_ = false;
    error_.clear();
    totalUses_ = 0;
    update();
}

void NativeModelUvPreview::reframe()
{
    auto bounds = overlayBounds_;
    if ( !sourceSize_.isEmpty() )
    {
        include(bounds, {});
        include(bounds, {static_cast<double>(sourceSize_.width()), static_cast<double>(sourceSize_.height())});
    }
    frame_ = bounds.value_or(QRectF{});
    update();
}

QRectF NativeModelUvPreview::plotRect() const
{
    return QRectF(rect()).adjusted(12, 26, -12, -12);
}

double NativeModelUvPreview::plotScale() const
{
    const auto plot = plotRect();
    return std::min(plot.width() / std::max(1.0, frame_.width()),
        plot.height() / std::max(1.0, frame_.height())) * 0.92;
}

QPointF NativeModelUvPreview::uvToWidget(QPointF uv) const
{
    return plotRect().center() + (uv - frame_.center()) * plotScale();
}

void NativeModelUvPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    const auto* target = mesh();
    if ( !target ) return;
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(12, 0, width() - 24, 24), Qt::AlignLeft | Qt::AlignVCenter,
        tr("U →     V ↓    (texture pixels)"));
    const auto plot = plotRect();
    const double scale = plotScale();
    if ( scale <= 0 ) return;
    const QPointF middle = plot.center();
    const QRectF relativeVisible((plot.topLeft() - middle) / scale, (plot.bottomRight() - middle) / scale);
    const auto uvFor = [&](QPointF point) { return frame_.center() + (point - plot.center()) / scale; };
    const QRectF visible(uvFor(plot.topLeft()), uvFor(plot.bottomRight()));
    painter.setClipRect(plot);
    if ( !sourceSize_.isEmpty() )
    {
        const QRectF tile(0, 0, sourceSize_.width(), sourceSize_.height());
        const auto portion = tile.intersected(visible);
        if ( !portion.isEmpty() )
        {
            const QRectF dest(uvToWidget(portion.topLeft()), uvToWidget(portion.bottomRight()));
            painter.fillRect(dest, palette().alternateBase());
            if ( !image_.isNull() )
            {
                const double sx = static_cast<double>(image_.width()) / tile.width();
                const double sy = static_cast<double>(image_.height()) / tile.height();
                painter.drawImage(dest, image_, QRectF(portion.x() * sx, portion.y() * sy,
                    portion.width() * sx, portion.height() * sy));
            }
            painter.setPen(palette().mid().color());
            painter.drawRect(dest);
        }
    }
    if ( !available_ )
    {
        painter.setPen(palette().text().color());
        painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap, error_);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(48, 200, 225), 1.5));
    for ( const auto& face : target->faces )
    {
        if ( face.material != slot_ || face.texVertices.size() < 2 ) continue;
        auto prev = uvAt(*target, face.texVertices.back());
        for ( const auto row : face.texVertices )
        {
            const auto next = uvAt(*target, row);
            if ( prev && next )
            {
                auto a = *prev;
                auto b = *next;
                if ( clipEdge(a, b, frame_.center(), relativeVisible) )
                {
                    painter.drawLine(middle + a * scale, middle + b * scale);
                }
            }
            prev = next;
        }
    }
    if ( marker_ )
    {
        const auto screen = uvToWidget(*marker_);
        if ( plot.contains(screen) )
        {
            painter.setPen(QPen(Qt::black, 1.5));
            painter.setBrush(QColor(255, 181, 55));
            painter.drawEllipse(screen, 4.5, 4.5);
        }
    }
}
} // namespace sed::ui::resources
