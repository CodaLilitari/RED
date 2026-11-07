#include "sed/level/statistics.hpp"

#include "sed/level/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace sed::level
{

LevelStatistics levelComputeStatistics(
    const Level& level,
    const std::span<const std::int32_t> sectorFilter)
{
    LevelStatistics out{};
    out.cogs = level.cogs.size();

    std::set<std::int32_t> scope;
    if (sectorFilter.empty())
    {
        for (std::size_t i = 0; i < level.sectors.size(); ++i)
        {
            scope.insert(static_cast<std::int32_t>(i));
        }
    }
    else
    {
        for (const std::int32_t index : sectorFilter)
        {
            if (index >= 0 && static_cast<std::size_t>(index) < level.sectors.size())
            {
                scope.insert(index);
            }
        }
    }

    for (const std::int32_t index : scope)
    {
        const Sector& sector = level.sectors[static_cast<std::size_t>(index)];
        ++out.sectors;
        ++out.sectorsByLayer[sector.layer];
        out.vertices += sector.vertices.size();
        for (const Surface& surface : sector.surfaces)
        {
            ++out.surfaces;
            out.texVertices += surface.texVertices.size();
            if (surface.adjoin)
            {
                ++out.adjoinedSurfaces;
            }
            if ((surface.surfaceFlags & (SF_SkyCeiling | SF_SkyHorizon)) != 0)
            {
                ++out.skySurfaces;
            }
            if (surface.light == static_cast<std::int32_t>(TLightMode::None))
            {
                ++out.fullyLitSurfaces;
            }
            // Fan-triangulated polygon area from the stored loop.
            bool valid = surface.vertices.size() >= 3u;
            for (const std::int32_t vertexIndex : surface.vertices)
            {
                valid = valid && vertexIndex >= 0
                    && static_cast<std::size_t>(vertexIndex) < sector.vertices.size();
            }
            if (valid)
            {
                const Vertex& origin = sector.vertices[static_cast<std::size_t>(surface.vertices[0])];
                TVector accumulated{};
                for (std::size_t i = 1; i + 1 < surface.vertices.size(); ++i)
                {
                    const Vertex& a = sector.vertices[static_cast<std::size_t>(surface.vertices[i])];
                    const Vertex& b = sector.vertices[static_cast<std::size_t>(surface.vertices[i + 1])];
                    const TVector ab = TVector::Create(a.x - origin.x, a.y - origin.y, a.z - origin.z);
                    const TVector ac = TVector::Create(b.x - origin.x, b.y - origin.y, b.z - origin.z);
                    const TVector cross = ab.Cross(ac);
                    accumulated.x += cross.x;
                    accumulated.y += cross.y;
                    accumulated.z += cross.z;
                }
                out.surfaceArea += 0.5 * accumulated.Length();
            }
        }
        for (const Vertex& vertex : sector.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
            {
                continue;
            }
            if (!out.hasBounds)
            {
                out.bounds.Set(vertex.x, vertex.y, vertex.z, vertex.x, vertex.y, vertex.z);
                out.hasBounds = true;
            }
            else
            {
                out.bounds.x1 = std::min(out.bounds.x1, vertex.x);
                out.bounds.y1 = std::min(out.bounds.y1, vertex.y);
                out.bounds.z1 = std::min(out.bounds.z1, vertex.z);
                out.bounds.x2 = std::max(out.bounds.x2, vertex.x);
                out.bounds.y2 = std::max(out.bounds.y2, vertex.y);
                out.bounds.z2 = std::max(out.bounds.z2, vertex.z);
            }
        }
    }

    for (const Thing& thing : level.things)
    {
        if (scope.contains(thing.sector))
        {
            ++out.things;
        }
    }
    for (const SedLight& light : level.lights)
    {
        if (sectorFilter.empty())
        {
            ++out.lights;
            continue;
        }
        const std::int32_t containing = levelFindSectorForXYZ(
            level, light.position.x, light.position.y, light.position.z);
        if (containing >= 0 && scope.contains(containing))
        {
            ++out.lights;
        }
    }
    return out;
}

} // namespace sed::level
