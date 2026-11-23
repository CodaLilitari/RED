#include "sed/level/render_budget.hpp"

#include "sed/level.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sed::level
{
namespace
{

struct Point
{
    double x{};
    double y{};
    double z{};
};

// A view rectangle in canvas pixels, y down, as the engine keeps it.
struct View
{
    double left{};
    double top{};
    double right{};
    double bottom{};
};

constexpr std::size_t kMaxCorners = 96;

struct Polygon
{
    std::array<Point, kMaxCorners> corners{};
    std::size_t count{};
};

struct Plane
{
    double a{};
    double b{};
    double c{};

    double side(const Point& point) const noexcept
    {
        return a * point.x + b * point.y + c * point.z;
    }
};

// Keeps the part of `source` with side(point) >= 0.
void clip(const Polygon& source, const Plane& plane, Polygon& out) noexcept
{
    out.count = 0;
    for ( std::size_t i = 0; i < source.count; ++i )
    {
        const Point& from = source.corners[i];
        const Point& to = source.corners[(i + 1) % source.count];
        const double fromSide = plane.side(from);
        const double toSide = plane.side(to);
        if ( fromSide >= 0.0 && out.count < kMaxCorners )
        {
            out.corners[out.count++] = from;
        }
        if ( (fromSide >= 0.0) != (toSide >= 0.0) && out.count < kMaxCorners )
        {
            const double t = fromSide / (fromSide - toSide);
            out.corners[out.count++] = { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
                from.z + (to.z - from.z) * t };
        }
    }
}

TVector position(const Vertex& vertex) noexcept
{
    return TVector{ vertex.x, vertex.y, vertex.z };
}

bool usable(const Sector& sector, const Surface& surface) noexcept
{
    if ( surface.vertices.size() < 3 || surface.vertices.size() > kMaxCorners - 8 )
    {
        return false;
    }
    return std::all_of(surface.vertices.begin(), surface.vertices.end(), [&sector](const std::int32_t corner)
        { return corner >= 0 && static_cast<std::size_t>(corner) < sector.vertices.size(); });
}

TVector inwardNormal(const Sector& sector, const Surface& surface) noexcept
{
    return surface.normal.lengthSqr() > 0.25 ? surface.normal : surfaceCalcNormalNewell(sector, surface);
}

struct Walk
{
    const Level& level;
    const RenderCamera& camera;
    TVector eye{};
    TVector right{};
    TVector forward{};
    TVector up{};
    double focal{};
    std::int32_t cameraSector{};
    std::size_t workLimit{};
    RenderFrame& frame;
    std::vector<char> listed{};
    std::vector<char> onPath{};

    Point toCamera(const Vertex& vertex) const noexcept
    {
        const TVector offset = position(vertex) - eye;
        return { offset.dot(right), offset.dot(forward), offset.dot(up) };
    }

    void sector(const std::int32_t index, const View& view)
    {
        const auto slot = static_cast<std::size_t>(index);
        if ( !listed[slot] )
        {
            listed[slot] = 1;
            frame.sectors.push_back(index);
        }
        const char saved = onPath[slot];
        onPath[slot] = 1;
        const Sector& room = level.sectors[slot];
        const double centreX = camera.width / 2.0;
        const double centreY = camera.height / 2.0;
        for ( const Surface& surface : room.surfaces )
        {
            if ( !frame.complete )
            {
                break;
            }
            if ( !surface.adjoin || !usable(room, surface) )
            {
                continue;
            }
            const std::int32_t next = surface.adjoin->sector;
            if ( next < 0 || static_cast<std::size_t>(next) >= level.sectors.size()
                || onPath[static_cast<std::size_t>(next)] )
            {
                continue;
            }
            if ( ++frame.work > workLimit )
            {
                frame.complete = false;
                break;
            }
            const TVector normal = inwardNormal(room, surface);
            const double distance =
                normal.dot(eye - position(room.vertices[static_cast<std::size_t>(surface.vertices[0])]));
            if ( !(distance > 0.0 || (distance == 0.0 && index == cameraSector)) )
            {
                continue;
            }

            Polygon first{};
            Polygon second{};
            first.count = surface.vertices.size();
            for ( std::size_t i = 0; i < first.count; ++i )
            {
                first.corners[i] = toCamera(room.vertices[static_cast<std::size_t>(surface.vertices[i])]);
            }
            // The engine clips to the four sides of the view first, then to the near plane.
            clip(first, Plane{ focal, -(view.left - centreX), 0.0 }, second);
            clip(second, Plane{ -focal, view.right - centreX, 0.0 }, first);
            clip(first, Plane{ 0.0, centreY - view.top, -focal }, second);
            clip(second, Plane{ 0.0, view.bottom - centreY, focal }, first);
            if ( first.count < 3 )
            {
                continue;
            }
            const bool crossesNear = std::any_of(first.corners.begin(), first.corners.begin() + first.count,
                [this](const Point& point) { return point.y < camera.nearPlane; });
            if ( crossesNear )
            {
                // At the portal: the view passes on unchanged, see-through or not.
                sector(next, view);
                continue;
            }
            const bool seeThrough = (surface.adjoinFlags & 1) != 0
                && (surface.geo == 0 || (surface.faceFlags & 2U) != 0 || surface.material.empty());
            if ( !seeThrough )
            {
                continue;
            }
            double minX = first.corners[0].x / first.corners[0].y;
            double maxX = minX;
            double minY = -first.corners[0].z / first.corners[0].y;
            double maxY = minY;
            for ( std::size_t i = 1; i < first.count; ++i )
            {
                const double x = first.corners[i].x / first.corners[i].y;
                const double y = -first.corners[i].z / first.corners[i].y;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
            // sithRender widens the rectangle by a pixel or two and truncates it to whole pixels.
            const View narrowed{ std::trunc(centreX + focal * minX - 1.5), std::trunc(centreY + focal * minY - 1.5),
                std::trunc(centreX + focal * maxX + 1.5), std::trunc(centreY + focal * maxY + 1.5) };
            sector(next, narrowed);
        }
        onPath[slot] = saved;
    }
};

bool finite(const TVector& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

RenderFrame traceRenderFrame(const Level& level, const std::int32_t cameraSector, const TVector& eye,
    const double yawDegrees, const double pitchDegrees, const RenderCamera& camera, const std::size_t workLimit)
{
    RenderFrame frame{};
    const bool validCamera = std::isfinite(camera.horizontalFovDegrees) && camera.horizontalFovDegrees >= 5.0
        && camera.horizontalFovDegrees <= 179.0 && std::isfinite(camera.width) && camera.width >= 1.0
        && std::isfinite(camera.height) && camera.height >= 1.0 && std::isfinite(camera.nearPlane)
        && camera.nearPlane > 0.0;
    if ( cameraSector < 0 || static_cast<std::size_t>(cameraSector) >= level.sectors.size() || !finite(eye)
        || !std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees) || !validCamera )
    {
        frame.complete = false;
        return frame;
    }
    const double yaw = yawDegrees * std::numbers::pi / 180.0;
    const double pitch = pitchDegrees * std::numbers::pi / 180.0;
    Walk walk{ level, camera, eye,
        TVector{ std::cos(yaw), std::sin(yaw), 0.0 },
        TVector{ -std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch) },
        TVector{ std::sin(yaw) * std::sin(pitch), -std::cos(yaw) * std::sin(pitch), std::cos(pitch) },
        camera.width / 2.0 / std::tan(camera.horizontalFovDegrees * std::numbers::pi / 360.0), cameraSector, workLimit,
        frame, std::vector<char>(level.sectors.size(), 0), std::vector<char>(level.sectors.size(), 0) };
    walk.sector(cameraSector, View{ 0.0, 0.0, camera.width, camera.height });

    for ( const std::int32_t index : frame.sectors )
    {
        const Sector& room = level.sectors[static_cast<std::size_t>(index)];
        for ( const Surface& surface : room.surfaces )
        {
            if ( !surface.adjoin || surface.geo == 0 || surface.material.empty() || (surface.faceFlags & 2U) == 0
                || !usable(room, surface) )
            {
                continue;
            }
            const double distance = inwardNormal(room, surface)
                .dot(eye - position(room.vertices[static_cast<std::size_t>(surface.vertices[0])]));
            frame.alphaSurfaces += distance > 0.0 ? 1 : 0;
        }
    }
    return frame;
}

std::vector<RenderViewpoint> sectorCentreViewpoints(const Level& level)
{
    std::vector<RenderViewpoint> viewpoints{};
    viewpoints.reserve(level.sectors.size());
    for ( std::size_t index = 0; index < level.sectors.size(); ++index )
    {
        const Sector& sector = level.sectors[index];
        if ( sector.vertices.size() < 4 || sector.surfaces.size() < 4 )
        {
            continue;
        }
        TVector sum{};
        bool usablePoint = true;
        for ( const Vertex& vertex : sector.vertices )
        {
            usablePoint = usablePoint && std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
            sum = sum + position(vertex);
        }
        if ( !usablePoint )
        {
            continue;
        }
        const double scale = 1.0 / static_cast<double>(sector.vertices.size());
        viewpoints.push_back({ static_cast<std::int32_t>(index), TVector{ sum.x * scale, sum.y * scale, sum.z * scale } });
    }
    return viewpoints;
}

std::optional<std::int32_t> sectorContainingPoint(const Level& level, const TVector& point)
{
    if ( !finite(point) )
    {
        return std::nullopt;
    }
    for ( std::size_t index = 0; index < level.sectors.size(); ++index )
    {
        const Sector& sector = level.sectors[index];
        if ( sector.surfaces.size() < 4 )
        {
            continue;
        }
        const bool inside = std::all_of(sector.surfaces.begin(), sector.surfaces.end(), [&](const Surface& surface)
        {
            return usable(sector, surface)
                && inwardNormal(sector, surface)
                        .dot(point - position(sector.vertices[static_cast<std::size_t>(surface.vertices[0])]))
                    >= -1e-9;
        });
        if ( inside )
        {
            return static_cast<std::int32_t>(index);
        }
    }
    return std::nullopt;
}

RenderBudgetReport auditRenderBudget(const Level& level, const std::span<const RenderViewpoint> viewpoints,
    const RenderBudgetOptions& options)
{
    RenderBudgetReport report{};
    if ( options.yawSteps == 0 || options.pitchDegrees.empty() )
    {
        report.complete = false;
        return report;
    }
    std::size_t work = 0;
    for ( const RenderViewpoint& viewpoint : viewpoints )
    {
        if ( work >= options.workLimit )
        {
            report.complete = false;
            break;
        }
        RenderBudgetView view{};
        view.sector = viewpoint.sector;
        view.position = viewpoint.position;
        bool first = true;
        for ( const double pitch : options.pitchDegrees )
        {
            for ( std::size_t step = 0; step < options.yawSteps; ++step )
            {
                const double yaw = 360.0 * static_cast<double>(step) / static_cast<double>(options.yawSteps);
                const RenderFrame frame = traceRenderFrame(level, viewpoint.sector, viewpoint.position, yaw, pitch,
                    options.camera, options.frameWorkLimit);
                work += frame.work;
                ++report.frames;
                view.complete = view.complete && frame.complete;
                view.alphaSurfaces = std::max(view.alphaSurfaces, frame.alphaSurfaces);
                if ( first || frame.sectors.size() > view.visibleSectors )
                {
                    view.visibleSectors = frame.sectors.size();
                    view.yawDegrees = yaw;
                    view.pitchDegrees = pitch;
                    first = false;
                }
            }
        }
        ++report.viewpoints;
        report.complete = report.complete && view.complete;
        report.overSectorLimit += view.visibleSectors > kRetailVisibleSectors ? 1 : 0;
        report.overAlphaLimit += view.alphaSurfaces > kRetailAlphaSurfaces ? 1 : 0;
        report.views.push_back(view);
    }
    std::stable_sort(report.views.begin(), report.views.end(), [](const RenderBudgetView& a, const RenderBudgetView& b)
    {
        return a.visibleSectors != b.visibleSectors ? a.visibleSectors > b.visibleSectors : a.sector < b.sector;
    });
    return report;
}

} // namespace sed::level
