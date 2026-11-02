#include "sed/level/overview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sed::level
{

bool isDrawnWorldSurface(const Surface& surface) noexcept
{
    return surface.geo != 0 && surface.vertices.size() >= 3 && !surface.material.empty();
}

std::optional<TBox> worldSurfaceBounds(const Level& level)
{
    TBox box{};
    bool any = false;
    for (const Sector& sector : level.sectors)
    {
        for (const Surface& surface : sector.surfaces)
        {
            if (!isDrawnWorldSurface(surface))
            {
                continue;
            }
            for (const std::int32_t index : surface.vertices)
            {
                if (index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size())
                {
                    throw std::runtime_error("Surface vertex index out of range");
                }
                const Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                {
                    throw std::runtime_error(
                        "Level geometry contains a nonfinite vertex; nothing can be framed.");
                }
                if (!any)
                {
                    box.Set(vertex.x, vertex.y, vertex.z, vertex.x, vertex.y, vertex.z);
                    any = true;
                }
                else
                {
                    box.x1 = std::min(box.x1, vertex.x);
                    box.y1 = std::min(box.y1, vertex.y);
                    box.z1 = std::min(box.z1, vertex.z);
                    box.x2 = std::max(box.x2, vertex.x);
                    box.y2 = std::max(box.y2, vertex.y);
                    box.z2 = std::max(box.z2, vertex.z);
                }
            }
        }
    }
    if (!any)
    {
        return std::nullopt;
    }
    return box;
}

OverviewCamera computeOverviewCamera(const TBox& bounds, const std::string_view view,
                                      const std::string_view framing, const int width,
                                      const int height, const double margin)
{
    if (view != kOverviewOverhead && view != kOverviewThreeQuarter)
    {
        throw std::invalid_argument("view must be one of: overhead, threequarter.");
    }
    if (framing != kOverviewWren && framing != kOverviewContain)
    {
        throw std::invalid_argument("framing must be one of: wren, contain.");
    }
    if (width < 1 || height < 1)
    {
        throw std::invalid_argument("Overview image dimensions must be positive.");
    }
    if (!std::isfinite(margin) || margin < 0.0 || margin > kOverviewMaximumMargin)
    {
        throw std::invalid_argument("margin must be finite and from 0 through 0.25.");
    }
    const TVector min{ bounds.x1, bounds.y1, bounds.z1 };
    const TVector max{ bounds.x2, bounds.y2, bounds.z2 };
    const auto finite = [](const TVector& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(min) || !finite(max) || min.x > max.x || min.y > max.y || min.z > max.z)
    {
        throw std::runtime_error("Level has no drawn world surfaces to frame.");
    }
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const TVector center = min.Add(max).Scaled(0.5);
    const TVector size = max.Sub(min);
    const double diagonal = size.Length();
    const double radius = diagonal / 2.0;
    if (std::max(size.x, size.y) < 1e-6)
    {
        throw std::runtime_error("Level bounds are degenerate in X and Y; nothing to frame.");
    }

    if (view == kOverviewOverhead)
    {
        // Straight down, north (+Y) up and east (+X) right. Both fits
        // are identical: an orthographic plan already shows the whole
        // footprint at its fitted extent.
        const double h = std::max(size.x, size.y);
        const TVector position{ center.x, center.y, max.z + h };
        const double orthographicHeight = std::max(size.y, size.x / aspect) * (1.0 + 2.0 * margin);
        const double nearPlane = std::clamp(0.5 * h, 0.001, 1000.0);
        const double farPlane = (position.z - min.z) + nearPlane;
        return OverviewCamera{ position, center, true, 0.0, orthographicHeight, nearPlane, farPlane };
    }

    // Blender lens 35 mm on a 36 mm sensor with AUTO fit: the wider
    // image side spans 36 mm.
    const double tanV = 18.0 / 35.0 / std::max(aspect, 1.0);
    const double fovY = 2.0 * std::atan(tanV);
    const double tanH = aspect * tanV;
    const TVector direction = TVector{ 0.7, -0.7, 0.6 }.Normalized(); // target -> eye: south-east, above
    TVector eye{};
    double distance = 0.0;
    if (framing == kOverviewWren)
    {
        // Wren's own placement: three-quarters of the box diagonal
        // along (0.7, -0.7, 0.6) from the centre.
        const double d = 0.75 * diagonal;
        eye = center.Add(TVector{ 0.7 * d, -0.7 * d, 0.6 * d });
        distance = eye.Sub(center).Length();
    }
    else
    {
        const TVector forward = direction.Scaled(-1.0);
        const TVector right = forward.Cross(TVector::up).Normalized();
        const TVector up = right.Cross(forward);
        const double kH = (1.0 - 2.0 * margin) * tanH;
        const double kV = (1.0 - 2.0 * margin) * tanV;
        double d = 0.0;
        for (const double x : { min.x, max.x })
        {
            for (const double y : { min.y, max.y })
            {
                for (const double z : { min.z, max.z })
                {
                    const TVector q = TVector{ x, y, z }.Sub(center);
                    const double depth = forward.Dot(q);
                    d = std::max(d, std::max(std::abs(right.Dot(q)) / kH - depth,
                                             std::abs(up.Dot(q)) / kV - depth));
                }
            }
        }
        eye = center.Add(direction.Scaled(d));
        distance = d;
    }
    const double nearPlane = distance > radius
        ? std::clamp(0.5 * (distance - radius), 0.001, 1000.0)
        : std::max(0.001, 0.001 * radius);
    const double farPlane = distance + 1.01 * radius;
    constexpr double kPi = 3.14159265358979323846264338327950288;
    return OverviewCamera{ eye, center, false, fovY * 180.0 / kPi, 0.0, nearPlane, farPlane };
}

} // namespace sed::level
