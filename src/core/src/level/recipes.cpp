#include "sed/level/recipes.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/edit.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace sed::level
{
namespace
{

std::set<std::int32_t> validSectors(const Level& level, const std::span<const std::int32_t> sectors)
{
    std::set<std::int32_t> out{};
    for ( const std::int32_t index : sectors )
    {
        if ( index >= 0 && static_cast<std::size_t>(index) < level.sectors.size() )
        {
            out.insert(index);
        }
    }
    return out;
}

bool samePosition(const Vertex& a, const Vertex& b) noexcept
{
    return SameValue(a.x, b.x, CloseEnough) && SameValue(a.y, b.y, CloseEnough) && SameValue(a.z, b.z, CloseEnough);
}

TVector position(const Vertex& vertex) noexcept
{
    return TVector::Create(vertex.x, vertex.y, vertex.z);
}

// Merges coincident vertices and drops unused ones. Returns false, leaving
// `sector` unchanged, when a merge would collapse or self-touch a polygon.
bool compactSectorVertices(Sector& sector, std::size_t& merged, std::size_t& unused)
{
    const std::size_t count = sector.vertices.size();
    std::vector<std::int32_t> representative(count);
    for ( std::size_t i = 0; i < count; ++i )
    {
        representative[i] = static_cast<std::int32_t>(i);
        for ( std::size_t j = 0; j < i; ++j )
        {
            if ( representative[j] == static_cast<std::int32_t>(j) && samePosition(sector.vertices[i], sector.vertices[j]) )
            {
                representative[i] = static_cast<std::int32_t>(j);
                break;
            }
        }
    }

    std::vector<Surface> surfaces = sector.surfaces;
    for ( Surface& surface : surfaces )
    {
        if ( surface.texVertices.size() != surface.vertices.size() )
        {
            return false;
        }
        std::vector<std::int32_t> vertices{};
        std::vector<TexVertex> texVertices{};
        for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
        {
            const std::int32_t index = surface.vertices[i];
            if ( index < 0 || static_cast<std::size_t>(index) >= count )
            {
                return false;
            }
            const std::int32_t mapped = representative[static_cast<std::size_t>(index)];
            if ( !vertices.empty() && vertices.back() == mapped )
            {
                continue;
            }
            vertices.push_back(mapped);
            texVertices.push_back(surface.texVertices[i]);
        }
        while ( vertices.size() > 1 && vertices.back() == vertices.front() )
        {
            vertices.pop_back();
            texVertices.pop_back();
        }
        std::vector<std::int32_t> sorted = vertices;
        std::sort(sorted.begin(), sorted.end());
        if ( vertices.size() < 3 || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() )
        {
            return false;
        }
        surface.vertices = std::move(vertices);
        surface.texVertices = std::move(texVertices);
    }

    std::vector<bool> used(count, false);
    for ( const Surface& surface : surfaces )
    {
        for ( const std::int32_t index : surface.vertices )
        {
            used[static_cast<std::size_t>(index)] = true;
        }
    }
    std::vector<std::int32_t> newIndex(count, -1);
    std::vector<Vertex> vertices{};
    for ( std::size_t i = 0; i < count; ++i )
    {
        if ( used[i] )
        {
            newIndex[i] = static_cast<std::int32_t>(vertices.size());
            vertices.push_back(sector.vertices[i]);
        }
        else if ( representative[i] != static_cast<std::int32_t>(i) )
        {
            ++merged;
        }
        else
        {
            ++unused;
        }
    }
    for ( Surface& surface : surfaces )
    {
        for ( std::int32_t& index : surface.vertices )
        {
            index = newIndex[static_cast<std::size_t>(index)];
        }
    }
    sector.vertices = std::move(vertices);
    sector.surfaces = std::move(surfaces);
    return true;
}

bool isFinitePositive(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

VertexCleanupResult levelRemoveRedundantVertices(Level& level, const std::span<const std::int32_t> sectors)
{
    VertexCleanupResult result{};
    for ( const std::int32_t sectorIndex : validSectors(level, sectors) )
    {
        Sector candidate = level.sectors[static_cast<std::size_t>(sectorIndex)];
        std::size_t merged = 0;
        std::size_t unused = 0;
        if ( !compactSectorVertices(candidate, merged, unused) )
        {
            ++result.skippedSectors;
            continue;
        }
        level.sectors[static_cast<std::size_t>(sectorIndex)] = std::move(candidate);
        result.mergedDuplicates += merged;
        result.removedUnused += unused;

        // The safe delete removes a vertex only when it is collinear in every
        // polygon that uses it.
        const std::size_t count = level.sectors[static_cast<std::size_t>(sectorIndex)].vertices.size();
        for ( std::size_t vertex = count; vertex-- > 0; )
        {
            if ( levelDeleteSectorVertex(level, sectorIndex, static_cast<std::int32_t>(vertex), true) )
            {
                ++result.removedCollinear;
            }
        }
    }
    return result;
}

std::vector<std::int32_t> levelPlaceSectorLights(
    Level& level, const std::span<const std::int32_t> sectors, const double intensity)
{
    std::vector<std::int32_t> created{};
    for ( const std::int32_t sectorIndex : validSectors(level, sectors) )
    {
        const Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
        if ( sector.vertices.empty() )
        {
            continue;
        }
        const TVector center = sectorCalcCenter(sector);
        if ( !sectorContainsPoint(sector, center.x, center.y, center.z) )
        {
            continue;
        }
        double range = 0.0;
        for ( const Vertex& vertex : sector.vertices )
        {
            range = std::max(range, (position(vertex) - center).Length());
        }
        const std::int32_t layer = sector.layer;
        SedLight& light = level.newLight();
        light.position = center;
        light.range = range;
        light.intensity = intensity;
        light.layer = layer;
        created.push_back(static_cast<std::int32_t>(level.lights.size() - 1));
    }
    return created;
}

std::optional<std::vector<std::int32_t>> levelBuildStairs(Level& level, const StairParameters& parameters)
{
    if ( parameters.steps < 1 || parameters.steps > kMaximumStairSteps || !isFinitePositive(parameters.width)
        || !isFinitePositive(parameters.run) || !isFinitePositive(parameters.rise)
        || !isFinitePositive(parameters.headroom) || parameters.headroom <= parameters.rise
        || !std::isfinite(parameters.origin.x) || !std::isfinite(parameters.origin.y)
        || !std::isfinite(parameters.origin.z) )
    {
        return std::nullopt;
    }

    const bool alongX = parameters.direction == StairDirection::PositiveX
        || parameters.direction == StairDirection::NegativeX;
    const double sign = parameters.direction == StairDirection::PositiveX
            || parameters.direction == StairDirection::PositiveY
        ? 1.0
        : -1.0;

    Level candidate = level;
    std::vector<std::int32_t> built{};
    for ( std::int32_t step = 0; step < parameters.steps; ++step )
    {
        const double near = sign * parameters.run * static_cast<double>(step);
        const double far = near + sign * parameters.run;
        const double along0 = std::min(near, far);
        const double along1 = std::max(near, far);
        const double floor = parameters.origin.z + parameters.rise * static_cast<double>(step);
        const TVector minCorner = alongX
            ? TVector::Create(parameters.origin.x + along0, parameters.origin.y, floor)
            : TVector::Create(parameters.origin.x, parameters.origin.y + along0, floor);
        const TVector maxCorner = alongX
            ? TVector::Create(parameters.origin.x + along1, parameters.origin.y + parameters.width, floor + parameters.headroom)
            : TVector::Create(parameters.origin.x + parameters.width, parameters.origin.y + along1, floor + parameters.headroom);
        const std::int32_t sector = levelCreateSectorPrimitiveBox(candidate, SectorPrimitive::Cube, minCorner, maxCorner);
        if ( sector < 0 )
        {
            return std::nullopt;
        }
        if ( !built.empty() && !levelConnectSectors(candidate, built.back(), sector) )
        {
            return std::nullopt;
        }
        built.push_back(sector);
    }
    level = std::move(candidate);
    return built;
}

bool levelFitAndAdjoinSurfaces(Level& level, const SurfaceRef anchor, const SurfaceRef fit)
{
    const auto valid = [&level](const SurfaceRef ref)
    {
        return ref.sector >= 0 && static_cast<std::size_t>(ref.sector) < level.sectors.size() && ref.surface >= 0
            && static_cast<std::size_t>(ref.surface) < level.sectors[static_cast<std::size_t>(ref.sector)].surfaces.size();
    };
    if ( !valid(anchor) || !valid(fit) || anchor.sector == fit.sector )
    {
        return false;
    }
    const Sector& anchorSector = level.sectors[static_cast<std::size_t>(anchor.sector)];
    const Surface& anchorSurface = anchorSector.surfaces[static_cast<std::size_t>(anchor.surface)];
    const Surface& fitSurface =
        level.sectors[static_cast<std::size_t>(fit.sector)].surfaces[static_cast<std::size_t>(fit.surface)];
    const std::size_t corners = anchorSurface.vertices.size();
    if ( anchorSurface.adjoin || fitSurface.adjoin || corners < 3 || fitSurface.vertices.size() != corners )
    {
        return false;
    }

    Sector candidate = level.sectors[static_cast<std::size_t>(fit.sector)];
    const auto vertexOf = [](const Sector& sector, const std::int32_t index) -> const Vertex*
    {
        return index >= 0 && static_cast<std::size_t>(index) < sector.vertices.size()
            ? &sector.vertices[static_cast<std::size_t>(index)]
            : nullptr;
    };
    for ( std::size_t i = 0; i < corners; ++i )
    {
        if ( !vertexOf(anchorSector, anchorSurface.vertices[i]) || !vertexOf(candidate, fitSurface.vertices[i]) )
        {
            return false;
        }
    }

    // Facing surfaces wind in opposite directions: anchor corner i pairs with
    // fit corner (offset - i). Choose the offset that moves the least.
    std::size_t bestOffset = 0;
    double bestCost = std::numeric_limits<double>::infinity();
    for ( std::size_t offset = 0; offset < corners; ++offset )
    {
        double cost = 0.0;
        for ( std::size_t i = 0; i < corners; ++i )
        {
            const std::size_t j = (offset + corners - i) % corners;
            const TVector delta = position(*vertexOf(anchorSector, anchorSurface.vertices[i]))
                - position(*vertexOf(candidate, fitSurface.vertices[j]));
            cost += delta.Dot(delta);
        }
        if ( cost < bestCost )
        {
            bestCost = cost;
            bestOffset = offset;
        }
    }
    if ( !std::isfinite(bestCost) )
    {
        return false;
    }

    std::set<std::int32_t> moved{};
    for ( std::size_t i = 0; i < corners; ++i )
    {
        const std::int32_t target = fitSurface.vertices[(bestOffset + corners - i) % corners];
        const Vertex& source = *vertexOf(anchorSector, anchorSurface.vertices[i]);
        Vertex& vertex = candidate.vertices[static_cast<std::size_t>(target)];
        vertex.x = source.x;
        vertex.y = source.y;
        vertex.z = source.z;
        moved.insert(target);
    }

    for ( std::size_t s = 0; s < candidate.surfaces.size(); ++s )
    {
        Surface& surface = candidate.surfaces[s];
        const bool touched = std::any_of(surface.vertices.begin(), surface.vertices.end(),
            [&moved](const std::int32_t index) { return moved.contains(index); });
        if ( !touched )
        {
            continue;
        }
        if ( surface.adjoin && static_cast<std::int32_t>(s) != fit.surface )
        {
            return false;
        }
        // The planarity check uses the stored normal, so refresh it first.
        surface.normal = surfaceCalcNormalNewell(candidate, surface);
        if ( !surfaceIsPlanar(candidate, surface) || !surfaceIsConvex(candidate, surface) )
        {
            return false;
        }
    }
    if ( !sectorIsConvex(candidate) )
    {
        return false;
    }
    const Surface& fittedSurface = candidate.surfaces[static_cast<std::size_t>(fit.surface)];
    if ( !surfaceDoOverlap(anchorSector, anchorSurface, candidate, fittedSurface) )
    {
        return false;
    }

    level.sectors[static_cast<std::size_t>(fit.sector)] = std::move(candidate);
    return levelSysAdjoinSurfaces(level, anchor, fit, true);
}

std::size_t levelMakeUnderwaterSectors(
    Level& level, const std::span<const std::int32_t> sectors, const UnderwaterSettings& settings)
{
    std::size_t changed = 0;
    for ( const std::int32_t sectorIndex : validSectors(level, sectors) )
    {
        Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
        const std::int32_t flags = sector.flags | SECF_Underwater;
        const bool same = sector.flags == flags && sector.tint.r == settings.tint.r && sector.tint.g == settings.tint.g
            && sector.tint.b == settings.tint.b && sector.sound == settings.sound
            && sector.soundVolume == settings.soundVolume;
        if ( same )
        {
            continue;
        }
        sector.flags = flags;
        sector.tint.r = settings.tint.r;
        sector.tint.g = settings.tint.g;
        sector.tint.b = settings.tint.b;
        sector.sound = settings.sound;
        sector.soundVolume = settings.soundVolume;
        ++changed;
    }
    return changed;
}

} // namespace sed::level
