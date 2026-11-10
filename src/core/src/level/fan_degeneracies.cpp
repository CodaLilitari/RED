#include "sed/level/fan_degeneracies.hpp"

#include <cmath>
#include <new>

namespace sed::level
{

FanDegeneracyReport inspectFanDegeneracies(
    const Level& level, const FanDegeneracyLimits& limits) noexcept
{
    FanDegeneracyReport report{};
    try
    {
        // Count the admitted work first, without overflow, so a budget
        // refusal is decided before any finding is published.
        std::size_t totalTriangles = 0;
        for (const Sector& sector : level.sectors)
        {
            for (const Surface& surface : sector.surfaces)
            {
                if (surface.vertices.size() < 3u)
                {
                    continue;
                }
                const std::size_t fan = surface.vertices.size() - 2u;
                if (totalTriangles > limits.maxFanTriangles
                    || fan > limits.maxFanTriangles - totalTriangles)
                {
                    report.status = FanDegeneracyStatus::BudgetExceeded;
                    return report;
                }
                totalTriangles += fan;
            }
        }

        for (std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex)
        {
            const Sector& sector = level.sectors[sectorIndex];
            for (std::size_t surfaceIndex = 0; surfaceIndex < sector.surfaces.size(); ++surfaceIndex)
            {
                const Surface& surface = sector.surfaces[surfaceIndex];
                if (surface.vertices.size() < 3u)
                {
                    continue;
                }
                bool valid = true;
                for (const std::int32_t vertexIndex : surface.vertices)
                {
                    if (vertexIndex < 0
                        || static_cast<std::size_t>(vertexIndex) >= sector.vertices.size())
                    {
                        valid = false;
                        break;
                    }
                    const Vertex& v = sector.vertices[static_cast<std::size_t>(vertexIndex)];
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                    {
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                {
                    report.status = FanDegeneracyStatus::InvalidGeometry;
                    return report;
                }

                const Vertex& origin = sector.vertices[static_cast<std::size_t>(surface.vertices[0])];
                for (std::size_t i = 1; i + 1 < surface.vertices.size(); ++i)
                {
                    const Vertex& a = sector.vertices[static_cast<std::size_t>(surface.vertices[i])];
                    const Vertex& b = sector.vertices[static_cast<std::size_t>(surface.vertices[i + 1])];
                    const auto samePosition = [](const Vertex& lhs, const Vertex& rhs)
                    {
                        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
                    };
                    FanDegeneracyKind kind{};
                    bool degenerate = false;
                    if (samePosition(origin, a) || samePosition(a, b) || samePosition(origin, b))
                    {
                        kind = FanDegeneracyKind::CoincidentCorners;
                        degenerate = true;
                    }
                    else
                    {
                        const TVector ab = TVector::Create(
                            a.x - origin.x, a.y - origin.y, a.z - origin.z);
                        const TVector ac = TVector::Create(
                            b.x - origin.x, b.y - origin.y, b.z - origin.z);
                        const TVector cross = ab.Cross(ac);
                        if (cross.x == 0.0 && cross.y == 0.0 && cross.z == 0.0)
                        {
                            kind = FanDegeneracyKind::CollinearCorners;
                            degenerate = true;
                        }
                    }
                    if (!degenerate)
                    {
                        continue;
                    }
                    if (report.issues.size() >= limits.maxIssues)
                    {
                        report.status = FanDegeneracyStatus::BudgetExceeded;
                        return report;
                    }
                    report.issues.push_back(FanDegeneracyIssue{
                        SurfaceRef{ static_cast<std::int32_t>(sectorIndex),
                            static_cast<std::int32_t>(surfaceIndex) },
                        i, kind });
                }
            }
        }
        report.status = FanDegeneracyStatus::Complete;
        return report;
    }
    catch (const std::bad_alloc&)
    {
        report.status = FanDegeneracyStatus::AllocationFailure;
        return report;
    }
    catch (...)
    {
        // Allocation is the only throwing operation above; anything else is
        // still reported conservatively instead of escaping noexcept.
        report.status = FanDegeneracyStatus::AllocationFailure;
        return report;
    }
}

} // namespace sed::level
