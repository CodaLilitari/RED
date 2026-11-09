#include "sed/level/recipes.hpp"

#include "sed/level/recipe_requests.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/edit.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/thing_frames.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

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
    if ( validateSectorLightIntensity(intensity) )
    {
        return {};
    }
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
    if ( validateStairParameters(parameters) )
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

namespace
{

Surface* surfaceIfValid(Level& level, const SurfaceRef ref) noexcept
{
    if ( ref.sector < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size() )
    {
        return nullptr;
    }
    Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    if ( ref.surface < 0 || static_cast<std::size_t>(ref.surface) >= sector.surfaces.size() )
    {
        return nullptr;
    }
    return &sector.surfaces[static_cast<std::size_t>(ref.surface)];
}

// Returns the reciprocal side of `ref`, or nothing for a missing or one-sided adjoin.
std::optional<SurfaceRef> reciprocalAdjoin(Level& level, const SurfaceRef ref) noexcept
{
    Surface* const surface = surfaceIfValid(level, ref);
    if ( !surface || !surface->adjoin )
    {
        return std::nullopt;
    }
    Surface* const other = surfaceIfValid(level, *surface->adjoin);
    if ( !other || !other->adjoin || *other->adjoin != ref )
    {
        return std::nullopt;
    }
    return surface->adjoin;
}

// Draws a portal face as a translucent textured surface without the solid
// collision flag; returns whether anything changed.
bool makeTranslucentPortalFace(Surface& surface, const std::string& material, const std::int32_t adjoinFlags)
{
    Surface updated = surface;
    updated.adjoinFlags = adjoinFlags;
    updated.geo = static_cast<std::int32_t>(TGeoMode::Texture);
    updated.faceFlags |= FF_Transluent;
    updated.surfaceFlags &= ~SF_Collision;
    if ( !material.empty() )
    {
        updated.material = material;
    }
    const bool changed = updated.adjoinFlags != surface.adjoinFlags || updated.geo != surface.geo
        || updated.faceFlags != surface.faceFlags || updated.surfaceFlags != surface.surfaceFlags
        || updated.material != surface.material;
    surface = std::move(updated);
    return changed;
}

bool referencesSurface(const Cog& cog, const std::int32_t globalSurface)
{
    return std::any_of(cog.values.begin(), cog.values.end(), [globalSurface](const CogValue& value)
        {
            return value.type == ValueType::ptr && value.cogType == CogType::surface
                && value.intValue == globalSurface;
        });
}

} // namespace

UnderwaterResult levelMakeUnderwaterSectors(
    Level& level, const std::span<const std::int32_t> sectors, const UnderwaterSettings& settings)
{
    if ( validateUnderwaterSettings(settings) )
    {
        return {};
    }
    UnderwaterResult result{};
    const std::set<std::int32_t> targets = validSectors(level, sectors);
    for ( const std::int32_t sectorIndex : targets )
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
        ++result.sectors;
    }
    if ( !settings.configureWaterSurfaces )
    {
        return result;
    }
    for ( const std::int32_t sectorIndex : targets )
    {
        const std::size_t count = level.sectors[static_cast<std::size_t>(sectorIndex)].surfaces.size();
        for ( std::size_t i = 0; i < count; ++i )
        {
            const SurfaceRef water{ sectorIndex, static_cast<std::int32_t>(i) };
            const std::optional<SurfaceRef> dry = reciprocalAdjoin(level, water);
            if ( !dry || (level.sectors[static_cast<std::size_t>(dry->sector)].flags & SECF_Underwater) != 0 )
            {
                continue;
            }
            for ( const SurfaceRef side : { water, *dry } )
            {
                Surface& surface = *surfaceIfValid(level, side);
                const std::int32_t flags = surface.adjoinFlags | SAF_Visible | SAF_Move | SAF_AllowSoundPass;
                if ( makeTranslucentPortalFace(surface, settings.waterSurfaceMaterial, flags) )
                {
                    ++result.boundarySurfaces;
                }
            }
        }
    }
    return result;
}

BreakableGlassResult levelMakeBreakableGlass(
    Level& level,
    const SurfaceRef glass,
    const std::string_view scriptName,
    const std::vector<CogValue>& scriptSymbols,
    const BreakableGlassSettings& settings)
{
    if ( !surfaceIfValid(level, glass) )
    {
        return BreakableGlassResult{ BreakableGlassError::InvalidSurface, -1 };
    }
    const std::optional<SurfaceRef> other = reciprocalAdjoin(level, glass);
    if ( !other )
    {
        return BreakableGlassResult{ BreakableGlassError::NotAdjoined, -1 };
    }
    std::vector<std::size_t> surfaceSymbols{};
    for ( std::size_t i = 0; i < scriptSymbols.size(); ++i )
    {
        if ( scriptSymbols[i].cogType == CogType::surface && !scriptSymbols[i].local )
        {
            surfaceSymbols.push_back(i);
        }
    }
    if ( surfaceSymbols.size() < 2u )
    {
        return BreakableGlassResult{ BreakableGlassError::MissingSymbols, -1 };
    }
    const std::int32_t glassNumber = level.getGlobalSurfaceNumber(glass.sector, glass.surface);
    const std::int32_t otherNumber = level.getGlobalSurfaceNumber(other->sector, other->surface);
    const bool alreadyGlass = std::any_of(level.cogs.begin(), level.cogs.end(), [&](const Cog& cog)
        {
            return sed::text::iequalsAscii(cog.name, scriptName)
                && (referencesSurface(cog, glassNumber) || referencesSurface(cog, otherNumber));
        });
    if ( alreadyGlass )
    {
        return BreakableGlassResult{ BreakableGlassError::AlreadyGlass, -1 };
    }
    if ( !level.canAllocateIds(1) )
    {
        return BreakableGlassResult{ BreakableGlassError::IdSpaceExhausted, -1 };
    }
    if ( validateBreakableGlassSettings(settings) )
    {
        return BreakableGlassResult{ BreakableGlassError::InvalidSettings, -1 };
    }

    std::vector<CogValue> values = scriptSymbols;
    const auto bindSurface = [&values](const std::size_t index, const std::int32_t number)
        {
            values[index].type = ValueType::ptr;
            values[index].intValue = number;
            values[index].stringValue.clear();
        };
    bindSurface(surfaceSymbols[0], glassNumber);
    bindSurface(surfaceSymbols[1], otherNumber);
    const auto overrideInt = [&values](const std::string_view name, const std::optional<std::int32_t> value)
        {
            if ( !value )
            {
                return;
            }
            for ( CogValue& symbol : values )
            {
                if ( sed::text::iequalsAscii(symbol.name, name) && symbol.cogType == CogType::int_ )
                {
                    symbol.intValue = *value;
                }
            }
        };
    overrideInt("spawnpoints", settings.spawnPoints);
    overrideInt("density", settings.density);

    for ( const SurfaceRef side : { glass, *other } )
    {
        Surface& surface = *surfaceIfValid(level, side);
        (void)makeTranslucentPortalFace(surface, settings.material,
            (surface.adjoinFlags | SAF_Visible) & ~SAF_Move);
        surface.surfaceFlags |= SF_CogLinked;
    }
    Cog& cog = level.newCog();
    cog.name = std::string(scriptName);
    cog.values = std::move(values);
    return BreakableGlassResult{ BreakableGlassError::None, cog.num };
}

MaskedSurfaceResult levelApplyMaskedSurfaceRecipe(
    Level& level, const std::span<const SurfaceRef> candidates, const MaskedSurfaceRecipe& recipe)
{
    if ( validateMaskedSurfaceRecipe(recipe) )
    {
        return {};
    }
    std::optional<Surface> source{};
    if ( recipe.copyFrom )
    {
        Surface* const from = surfaceIfValid(level, *recipe.copyFrom);
        if ( !from )
        {
            return {};
        }
        source = *from;
    }
    std::vector<SurfaceRef> targets(candidates.begin(), candidates.end());
    std::sort(targets.begin(), targets.end(), [](const SurfaceRef a, const SurfaceRef b)
        {
            return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
        });
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

    MaskedSurfaceResult result{};
    for ( const SurfaceRef ref : targets )
    {
        Surface* const surface = surfaceIfValid(level, ref);
        if ( !surface )
        {
            continue;
        }
        const auto matches = [](const std::optional<FlagMaskPredicate>& selector, const std::int32_t bits)
        {
            return !selector || selector->matches(static_cast<std::uint32_t>(bits));
        };
        if ( !matches(recipe.surfaceFlags, surface->surfaceFlags)
            || !matches(recipe.faceFlags, static_cast<std::int32_t>(surface->faceFlags))
            || !matches(recipe.adjoinFlags, surface->adjoinFlags) )
        {
            continue;
        }
        const double length = surface->normal.Length();
        const double slope = length > 1e-12
            ? std::acos(std::clamp(std::abs(surface->normal.z) / length, 0.0, 1.0)) * 57.29577951308232
            : 90.0;
        if ( slope < recipe.minSlopeDegrees || slope > recipe.maxSlopeDegrees )
        {
            continue;
        }
        ++result.matched;
        const Surface before = *surface;
        if ( source )
        {
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::Material)) != 0u )
            {
                surface->material = source->material;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::Geometry)) != 0u )
            {
                surface->geo = source->geo;
                surface->light = source->light;
                surface->tex = source->tex;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::ExtraLight)) != 0u )
            {
                surface->extraLight = source->extraLight;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::SurfaceFlags)) != 0u )
            {
                surface->surfaceFlags = source->surfaceFlags;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::FaceFlags)) != 0u )
            {
                surface->faceFlags = source->faceFlags;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::AdjoinFlags)) != 0u )
            {
                surface->adjoinFlags = source->adjoinFlags;
            }
            if ( (recipe.copyFields & static_cast<std::uint32_t>(MaskedCopyField::UvScale)) != 0u )
            {
                surface->uScale = source->uScale;
                surface->vScale = source->vScale;
            }
        }
        surface->surfaceFlags = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(surface->surfaceFlags) & ~recipe.clearSurfaceFlags)
            | recipe.setSurfaceFlags);
        surface->faceFlags = (surface->faceFlags & ~recipe.clearFaceFlags) | recipe.setFaceFlags;
        surface->adjoinFlags = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(surface->adjoinFlags) & ~recipe.clearAdjoinFlags)
            | recipe.setAdjoinFlags);
        if ( recipe.material )
        {
            surface->material = *recipe.material;
        }
        const bool changed = surface->material != before.material || surface->geo != before.geo
            || surface->light != before.light || surface->tex != before.tex
            || surface->surfaceFlags != before.surfaceFlags || surface->faceFlags != before.faceFlags
            || surface->adjoinFlags != before.adjoinFlags || surface->uScale != before.uScale
            || surface->vScale != before.vScale
            || surface->extraLight.r != before.extraLight.r || surface->extraLight.g != before.extraLight.g
            || surface->extraLight.b != before.extraLight.b || surface->extraLight.a != before.extraLight.a;
        if ( changed )
        {
            ++result.changed;
        }
    }
    return result;
}

std::optional<std::vector<std::int32_t>> levelBuildSwitchbackStairs(
    Level& level, const SwitchbackStairParameters& parameters)
{
    if ( validateSwitchbackStairParameters(parameters) )
    {
        return std::nullopt;
    }
    const bool alongX = parameters.direction == StairDirection::PositiveX
        || parameters.direction == StairDirection::NegativeX;
    const double sign = parameters.direction == StairDirection::PositiveX
            || parameters.direction == StairDirection::PositiveY
        ? 1.0
        : -1.0;
    const std::int32_t steps = parameters.stepsPerFlight;

    Level candidate = level;
    std::vector<std::int32_t> built{};
    const auto addBox = [&](const double along0, const double along1, const double across0,
                            const double across1, const double floor)
    {
        const TVector minCorner = alongX
            ? TVector::Create(parameters.origin.x + std::min(along0, along1),
                parameters.origin.y + std::min(across0, across1), floor)
            : TVector::Create(parameters.origin.x + std::min(across0, across1),
                parameters.origin.y + std::min(along0, along1), floor);
        const TVector maxCorner = alongX
            ? TVector::Create(parameters.origin.x + std::max(along0, along1),
                parameters.origin.y + std::max(across0, across1), floor + parameters.headroom)
            : TVector::Create(parameters.origin.x + std::max(across0, across1),
                parameters.origin.y + std::max(along0, along1), floor + parameters.headroom);
        const std::int32_t sector =
            levelCreateSectorPrimitiveBox(candidate, SectorPrimitive::Cube, minCorner, maxCorner);
        if ( sector < 0 )
        {
            return false;
        }
        if ( !built.empty() && !levelConnectSectors(candidate, built.back(), sector) )
        {
            return false;
        }
        built.push_back(sector);
        return true;
    };

    for ( std::int32_t i = 0; i < steps; ++i )
    {
        if ( !addBox(sign * parameters.run * i, sign * parameters.run * (i + 1), 0.0, parameters.width,
                parameters.origin.z + parameters.rise * i) )
        {
            return std::nullopt;
        }
    }
    const double landingNear = sign * parameters.run * steps;
    const double landingFar = landingNear + sign * parameters.landingRun;
    if ( !addBox(landingNear, landingFar, 0.0, 2.0 * parameters.width,
            parameters.origin.z + parameters.rise * steps) )
    {
        return std::nullopt;
    }
    for ( std::int32_t j = 0; j < steps; ++j )
    {
        if ( !addBox(landingNear - sign * parameters.run * j, landingNear - sign * parameters.run * (j + 1),
                parameters.width, 2.0 * parameters.width,
                parameters.origin.z + parameters.rise * (steps + 1 + j)) )
        {
            return std::nullopt;
        }
    }
    level = std::move(candidate);
    return built;
}

namespace
{

// Creates one wedge-prism sector: the annular footprint between the two
// radii and the two angles (either order), extruded from `floor` up by
// `headroom`. Starts from a unit cube so the wedge inherits the
// primitive's topology, winding, material and UVs, then moves its
// corners. Returns the new sector index, or -1 when the wedge would not
// stay planar/convex (the caller discards its candidate level).
std::int32_t addWedgePrism(Level& candidate, const TVector& center,
    const double innerRadius, const double outerRadius,
    const double angleA, const double angleB,
    const double floor, const double headroom)
{
    const double low = std::min(angleA, angleB);
    const double high = std::max(angleA, angleB);
    const auto polar = [&center](const double radius, const double degrees)
    {
        // Normalize so a full turn lands exactly on angle 0: sin(2*pi)
        // is not 0, and a wrap-around neighbour computed at 0 degrees
        // must share the wall vertices exactly.
        const double radians = std::fmod(degrees, 360.0) * std::acos(-1.0) / 180.0;
        return TVector::Create(center.x + radius * std::cos(radians),
            center.y + radius * std::sin(radians), 0.0);
    };
    const std::int32_t sector = levelCreateSectorPrimitiveBox(candidate, SectorPrimitive::Cube,
        TVector::Create(0, 0, floor), TVector::Create(1, 1, floor + headroom));
    if ( sector < 0 )
    {
        return -1;
    }
    Sector& wedge = candidate.sectors[static_cast<std::size_t>(sector)];
    const TVector innerLow = polar(innerRadius, low);
    const TVector outerLow = polar(outerRadius, low);
    const TVector outerHigh = polar(outerRadius, high);
    const TVector innerHigh = polar(innerRadius, high);
    for ( Vertex& vertex : wedge.vertices )
    {
        const TVector footprint = vertex.x < 0.5
            ? (vertex.y < 0.5 ? innerLow : innerHigh)
            : (vertex.y < 0.5 ? outerLow : outerHigh);
        vertex.x = footprint.x;
        vertex.y = footprint.y;
    }
    bool valid = true;
    for ( Surface& surface : wedge.surfaces )
    {
        surface.normal = surfaceCalcNormalNewell(wedge, surface);
        valid = valid && surfaceIsPlanar(wedge, surface) && surfaceIsConvex(wedge, surface);
    }
    if ( !valid || !sectorIsConvex(wedge) )
    {
        return -1;
    }
    return sector;
}

} // namespace

std::optional<std::vector<std::int32_t>> levelBuildSpiralStairs(
    Level& level, const SpiralStairParameters& parameters)
{
    if ( validateSpiralStairParameters(parameters) )
    {
        return std::nullopt;
    }
    Level candidate = level;
    std::vector<std::int32_t> built{};
    for ( std::int32_t step = 0; step < parameters.steps; ++step )
    {
        const double angle0 = parameters.startAngleDegrees
            + parameters.stepAngleDegrees * static_cast<double>(step);
        const double angle1 = angle0 + parameters.stepAngleDegrees;
        const double floor = parameters.center.z + parameters.rise * static_cast<double>(step);
        const std::int32_t sector = addWedgePrism(candidate, parameters.center,
            parameters.innerRadius, parameters.outerRadius, angle0, angle1,
            floor, parameters.headroom);
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

std::optional<std::vector<std::int32_t>> levelBuildDome(
    Level& level, const DomeParameters& parameters)
{
    if ( validateDomeParameters(parameters) )
    {
        return std::nullopt;
    }
    const double quarter = std::acos(-1.0) / 2.0;
    const double segmentStep = 360.0 / static_cast<double>(parameters.segments);
    Level candidate = level;
    std::vector<std::int32_t> built{};
    std::vector<std::int32_t> below{};
    for ( std::int32_t ring = 0; ring < parameters.rings; ++ring )
    {
        const double phi0 = quarter * static_cast<double>(ring)
            / static_cast<double>(parameters.rings);
        const double phi1 = quarter * static_cast<double>(ring + 1)
            / static_cast<double>(parameters.rings);
        const double outerRadius = parameters.radius * std::cos(phi0);
        const double innerRadius = std::max(
            parameters.radius * std::cos(phi1), kMinStairDimension);
        const double floor = parameters.center.z + parameters.radius * std::sin(phi0);
        std::vector<std::int32_t> current{};
        for ( std::int32_t segment = 0; segment < parameters.segments; ++segment )
        {
            const double angle0 = segmentStep * static_cast<double>(segment);
            const std::int32_t sector = addWedgePrism(candidate, parameters.center,
                innerRadius, outerRadius, angle0, angle0 + segmentStep,
                floor, parameters.headroom);
            if ( sector < 0 )
            {
                return std::nullopt;
            }
            if ( !current.empty() && !levelConnectSectors(candidate, current.back(), sector) )
            {
                return std::nullopt;
            }
            if ( !below.empty()
                && !levelConnectSectors(candidate, below[static_cast<std::size_t>(segment)], sector) )
            {
                return std::nullopt;
            }
            current.push_back(sector);
        }
        if ( parameters.segments > 1
            && !levelConnectSectors(candidate, current.back(), current.front()) )
        {
            return std::nullopt;
        }
        built.insert(built.end(), current.begin(), current.end());
        below = std::move(current);
    }
    level = std::move(candidate);
    return built;
}

std::optional<std::vector<std::int32_t>> levelBuildSweptCorridor(
    Level& level, const SweptCorridorParameters& parameters)
{
    if ( validateSweptCorridorParameters(parameters) )
    {
        return std::nullopt;
    }
    const double segmentStep = parameters.sweepDegrees
        / static_cast<double>(parameters.segments);
    Level candidate = level;
    std::vector<std::int32_t> built{};
    for ( std::int32_t segment = 0; segment < parameters.segments; ++segment )
    {
        const double angle0 = parameters.startAngleDegrees
            + segmentStep * static_cast<double>(segment);
        const double floor = parameters.center.z
            + parameters.risePerSegment * static_cast<double>(segment);
        const std::int32_t sector = addWedgePrism(candidate, parameters.center,
            parameters.innerRadius, parameters.outerRadius, angle0, angle0 + segmentStep,
            floor, parameters.headroom);
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

namespace
{

bool insetSurfaceInSector(Sector& sector, const std::int32_t surfaceIndex, const double factor)
{
    Surface& surface = sector.surfaces[static_cast<std::size_t>(surfaceIndex)];
    TVector centroid{};
    for ( const std::int32_t index : surface.vertices )
    {
        const Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
        centroid.x += vertex.x;
        centroid.y += vertex.y;
        centroid.z += vertex.z;
    }
    const double count = static_cast<double>(surface.vertices.size());
    centroid.x /= count;
    centroid.y /= count;
    centroid.z /= count;
    std::set<std::int32_t> moved(surface.vertices.begin(), surface.vertices.end());
    for ( const std::int32_t index : moved )
    {
        Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
        vertex.x = centroid.x + (vertex.x - centroid.x) * factor;
        vertex.y = centroid.y + (vertex.y - centroid.y) * factor;
        vertex.z = centroid.z + (vertex.z - centroid.z) * factor;
    }
    for ( Surface& current : sector.surfaces )
    {
        const bool touched = std::any_of(current.vertices.begin(), current.vertices.end(),
            [&moved](const std::int32_t index) { return moved.contains(index); });
        if ( !touched )
        {
            continue;
        }
        // Moving shared corners also deforms adjacent faces. Only the selected
        // portal may move: its reciprocal is transformed by the paired caller.
        // Other portal boundaries would be left behind in neighboring sectors.
        if ( current.adjoin && &current != &surface )
        {
            return false;
        }
        current.normal = surfaceCalcNormalNewell(sector, current);
        if ( !surfaceIsPlanar(sector, current) || !surfaceIsConvex(sector, current) )
        {
            return false;
        }
    }
    return sectorIsConvex(sector);
}

bool validInsetTarget(const Level& level, const SurfaceRef ref)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const Surface& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];
    if ( surface.vertices.size() < 3 )
    {
        return false;
    }
    return std::all_of(surface.vertices.begin(), surface.vertices.end(), [&sector](const std::int32_t index)
        {
            return index >= 0 && static_cast<std::size_t>(index) < sector.vertices.size();
        });
}

} // namespace

bool levelInsetSurface(Level& level, const SurfaceRef surface, const double factor)
{
    if ( validateInsetFactor(factor) || !surfaceIfValid(level, surface) )
    {
        return false;
    }
    if ( level.sectors[static_cast<std::size_t>(surface.sector)]
            .surfaces[static_cast<std::size_t>(surface.surface)].adjoin
        || !validInsetTarget(level, surface) )
    {
        return false;
    }
    Sector candidate = level.sectors[static_cast<std::size_t>(surface.sector)];
    if ( !insetSurfaceInSector(candidate, surface.surface, factor) )
    {
        return false;
    }
    level.sectors[static_cast<std::size_t>(surface.sector)] = std::move(candidate);
    return true;
}

bool levelInsetAdjoinPair(Level& level, const SurfaceRef surface, const double factor)
{
    if ( validateInsetFactor(factor) || !surfaceIfValid(level, surface) )
    {
        return false;
    }
    const std::optional<SurfaceRef> other = reciprocalAdjoin(level, surface);
    if ( !other || !validInsetTarget(level, surface) || !validInsetTarget(level, *other) )
    {
        return false;
    }
    Level candidate = level;
    if ( !insetSurfaceInSector(candidate.sectors[static_cast<std::size_t>(surface.sector)], surface.surface, factor)
        || !insetSurfaceInSector(candidate.sectors[static_cast<std::size_t>(other->sector)], other->surface, factor) )
    {
        return false;
    }
    const Sector& firstSector = candidate.sectors[static_cast<std::size_t>(surface.sector)];
    const Sector& secondSector = candidate.sectors[static_cast<std::size_t>(other->sector)];
    if ( !surfaceDoOverlap(firstSector, firstSector.surfaces[static_cast<std::size_t>(surface.surface)],
            secondSector, secondSector.surfaces[static_cast<std::size_t>(other->surface)]) )
    {
        // Equal boundaries can have different corner centroids when only one
        // side contains redundant collinear vertices. Do not publish a split.
        return false;
    }
    level = std::move(candidate);
    return true;
}

std::vector<VertexCleanupCandidate> levelReportRedundantVertices(
    const Level& level, const std::span<const std::int32_t> sectors)
{
    std::vector<VertexCleanupCandidate> report{};
    for ( const std::int32_t sectorIndex : validSectors(level, sectors) )
    {
        Level copy = level;
        const std::array<std::int32_t, 1> one{ sectorIndex };
        const VertexCleanupResult result = levelRemoveRedundantVertices(copy, one);
        report.push_back(VertexCleanupCandidate{ sectorIndex, result.mergedDuplicates, result.removedUnused,
            result.removedCollinear, result.skippedSectors > 0u });
    }
    return report;
}

std::size_t levelSubdivideSurfaces(Level& level, const std::span<const SurfaceRef> surfaces)
{
    std::vector<SurfaceRef> targets(surfaces.begin(), surfaces.end());
    std::sort(targets.begin(), targets.end(), [](const SurfaceRef a, const SurfaceRef b)
        {
            return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
        });
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    std::size_t subdivided = 0;
    for ( const SurfaceRef ref : targets )
    {
        Surface* const found = surfaceIfValid(level, ref);
        if ( !found || found->adjoin || found->vertices.size() < 3
            || found->vertices.size() != found->texVertices.size() )
        {
            continue;
        }
        const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
        const Surface& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];
        const bool indicesValid = std::all_of(surface.vertices.begin(), surface.vertices.end(),
            [&sector](const std::int32_t index)
            {
                return index >= 0 && static_cast<std::size_t>(index) < sector.vertices.size();
            });
        if ( !indicesValid || !surfaceIsConvex(sector, surface) )
        {
            continue;
        }
        Sector candidate = sector;
        Surface& original = candidate.surfaces[static_cast<std::size_t>(ref.surface)];
        TVector centroid{};
        TexVertex centroidTex{};
        std::array<double, 4> centroidColor{};
        for ( std::size_t i = 0; i < original.vertices.size(); ++i )
        {
            const Vertex& vertex = candidate.vertices[static_cast<std::size_t>(original.vertices[i])];
            centroid.x += vertex.x;
            centroid.y += vertex.y;
            centroid.z += vertex.z;
            centroidTex.u += original.texVertices[i].u;
            centroidTex.v += original.texVertices[i].v;
            const ColorF color = original.texVertices[i].color;
            centroidColor[0] += color.r;
            centroidColor[1] += color.g;
            centroidColor[2] += color.b;
            centroidColor[3] += color.a;
        }
        const double count = static_cast<double>(original.vertices.size());
        centroid.x /= count;
        centroid.y /= count;
        centroid.z /= count;
        centroidTex.u = static_cast<float>(centroidTex.u / count);
        centroidTex.v = static_cast<float>(centroidTex.v / count);
        centroidTex.color = ColorF{
            static_cast<float>(centroidColor[0] / count),
            static_cast<float>(centroidColor[1] / count),
            static_cast<float>(centroidColor[2] / count),
            static_cast<float>(centroidColor[3] / count),
        };
        candidate.vertices.push_back(Vertex{ centroid.x, centroid.y, centroid.z, 0 });
        const std::int32_t center = static_cast<std::int32_t>(candidate.vertices.size() - 1);
        const std::vector<std::int32_t> corners = original.vertices;
        const std::vector<TexVertex> cornerTex = original.texVertices;
        std::vector<Surface> triangles{};
        for ( std::size_t i = 0; i < corners.size(); ++i )
        {
            Surface triangle = original;
            triangle.vertices = { center, corners[i], corners[(i + 1) % corners.size()] };
            triangle.texVertices = { centroidTex, cornerTex[i], cornerTex[(i + 1) % cornerTex.size()] };
            triangle.adjoin.reset();
            triangles.push_back(std::move(triangle));
        }
        original = triangles.front();
        for ( std::size_t i = 1; i < triangles.size(); ++i )
        {
            triangles[i].num = static_cast<std::int32_t>(candidate.surfaces.size());
            candidate.surfaces.push_back(std::move(triangles[i]));
        }
        level.sectors[static_cast<std::size_t>(ref.sector)] = std::move(candidate);
        ++subdivided;
    }
    return subdivided;
}

std::optional<std::vector<std::int32_t>> levelBuildHeightmapTerrain(
    Level& level, const TerrainParameters& parameters)
{
    if ( validateTerrainParameters(parameters) )
    {
        return std::nullopt;
    }

    Level candidate = level;
    std::vector<std::int32_t> built{};
    built.reserve(parameters.heights.size());
    for ( std::int32_t row = 0; row < parameters.rows; ++row )
    {
        for ( std::int32_t column = 0; column < parameters.columns; ++column )
        {
            const std::size_t cell = static_cast<std::size_t>(row)
                    * static_cast<std::size_t>(parameters.columns)
                + static_cast<std::size_t>(column);
            const double floor = parameters.origin.z + parameters.heights[cell];
            const TVector minCorner = TVector::Create(
                parameters.origin.x + parameters.cellSize * static_cast<double>(column),
                parameters.origin.y + parameters.cellSize * static_cast<double>(row),
                floor);
            const TVector maxCorner = TVector::Create(
                minCorner.x + parameters.cellSize,
                minCorner.y + parameters.cellSize,
                floor + parameters.headroom);
            const std::int32_t sector =
                levelCreateSectorPrimitiveBox(candidate, SectorPrimitive::Cube, minCorner, maxCorner);
            if ( sector < 0 )
            {
                return std::nullopt;
            }
            built.push_back(sector);
        }
    }
    for ( std::int32_t row = 0; row < parameters.rows; ++row )
    {
        for ( std::int32_t column = 0; column < parameters.columns; ++column )
        {
            const std::size_t cell = static_cast<std::size_t>(row)
                    * static_cast<std::size_t>(parameters.columns)
                + static_cast<std::size_t>(column);
            if ( column + 1 < parameters.columns )
            {
                (void)levelConnectSectors(candidate, built[cell], built[cell + 1]);
            }
            if ( row + 1 < parameters.rows )
            {
                (void)levelConnectSectors(
                    candidate, built[cell], built[cell + static_cast<std::size_t>(parameters.columns)]);
            }
        }
    }
    level = std::move(candidate);
    return built;
}

std::optional<assets::Model3do> levelBuildHeightmapModel(
    const TerrainParameters& parameters, const std::string_view materialName)
{
    if ( validateTerrainParameters(parameters) )
    {
        return std::nullopt;
    }
    const std::string material = materialName.empty() ? "dflt.mat" : std::string(materialName);
    if ( material.find_first_of(" \t\r\n") != std::string::npos )
    {
        return std::nullopt;
    }
    const std::int32_t columns = parameters.columns;
    const std::int32_t rows = parameters.rows;
    const std::int32_t nodeColumns = columns + 1;
    const auto nodeIndex = [nodeColumns](const std::int32_t column, const std::int32_t row)
    {
        return row * nodeColumns + column;
    };

    assets::Model3do model{};
    model.version = 2.3;
    model.hierarchyTransformsApplied = false;
    model.geosetCount = 1;
    model.materials = { material };

    assets::Model3doMesh mesh{};
    mesh.name = "terrain";
    mesh.vertices.reserve(static_cast<std::size_t>(nodeColumns) * static_cast<std::size_t>(rows + 1));
    mesh.texVertices.reserve(mesh.vertices.capacity());
    for ( std::int32_t row = 0; row <= rows; ++row )
    {
        for ( std::int32_t column = 0; column <= columns; ++column )
        {
            double heightSum = 0.0;
            std::int32_t touching = 0;
            for ( const std::int32_t cellRow : { row - 1, row } )
            {
                for ( const std::int32_t cellColumn : { column - 1, column } )
                {
                    if ( cellRow < 0 || cellRow >= rows || cellColumn < 0 || cellColumn >= columns )
                    {
                        continue;
                    }
                    heightSum += parameters.heights[static_cast<std::size_t>(cellRow)
                            * static_cast<std::size_t>(columns)
                        + static_cast<std::size_t>(cellColumn)];
                    ++touching;
                }
            }
            assets::Model3doVertex vertex{};
            vertex.x = parameters.origin.x + parameters.cellSize * static_cast<double>(column);
            vertex.y = parameters.origin.y + parameters.cellSize * static_cast<double>(row);
            vertex.z = parameters.origin.z + heightSum / static_cast<double>(touching);
            mesh.vertices.push_back(vertex);
            assets::Model3doTexVertex uv{};
            uv.u = static_cast<float>(column) / static_cast<float>(columns);
            uv.v = static_cast<float>(row) / static_cast<float>(rows);
            mesh.texVertices.push_back(uv);
        }
    }
    const auto faceNormal = [&mesh](const std::int32_t a, const std::int32_t b, const std::int32_t c)
    {
        const assets::Model3doVertex& va = mesh.vertices[static_cast<std::size_t>(a)];
        const assets::Model3doVertex& vb = mesh.vertices[static_cast<std::size_t>(b)];
        const assets::Model3doVertex& vc = mesh.vertices[static_cast<std::size_t>(c)];
        const TVector ab = TVector::Create(vb.x - va.x, vb.y - va.y, vb.z - va.z);
        const TVector ac = TVector::Create(vc.x - va.x, vc.y - va.y, vc.z - va.z);
        TVector normal = ab.Cross(ac);
        const double length = normal.Length();
        if ( length > 0.0 )
        {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }
        return normal;
    };
    for ( std::int32_t row = 0; row < rows; ++row )
    {
        for ( std::int32_t column = 0; column < columns; ++column )
        {
            const std::int32_t a = nodeIndex(column, row);
            const std::int32_t b = nodeIndex(column + 1, row);
            const std::int32_t c = nodeIndex(column + 1, row + 1);
            const std::int32_t d = nodeIndex(column, row + 1);
            for ( const std::array<std::int32_t, 3> triangle : { std::array{ a, b, c }, std::array{ a, c, d } } )
            {
                assets::Model3doFace face{};
                face.material = 0;
                face.vertices = { triangle[0], triangle[1], triangle[2] };
                face.texVertices = { triangle[0], triangle[1], triangle[2] };
                mesh.faces.push_back(std::move(face));
                mesh.faceNormals.push_back(faceNormal(triangle[0], triangle[1], triangle[2]));
            }
        }
    }
    mesh.vertexNormals.assign(mesh.vertices.size(), TVector{});
    for ( std::size_t face = 0; face < mesh.faces.size(); ++face )
    {
        for ( const std::int32_t index : mesh.faces[face].vertices )
        {
            TVector& sum = mesh.vertexNormals[static_cast<std::size_t>(index)];
            sum.x += mesh.faceNormals[face].x;
            sum.y += mesh.faceNormals[face].y;
            sum.z += mesh.faceNormals[face].z;
        }
    }
    for ( std::size_t i = 0; i < mesh.vertexNormals.size(); ++i )
    {
        TVector& normal = mesh.vertexNormals[i];
        const double length = normal.Length();
        if ( length > 0.0 )
        {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }
    }
    double radius = 0.0;
    const TVector center = TVector::Create(
        parameters.origin.x + parameters.cellSize * static_cast<double>(columns) / 2.0,
        parameters.origin.y + parameters.cellSize * static_cast<double>(rows) / 2.0,
        parameters.origin.z);
    for ( const assets::Model3doVertex& vertex : mesh.vertices )
    {
        radius = std::max(radius,
            TVector::Create(vertex.x - center.x, vertex.y - center.y, vertex.z - center.z).Length());
    }
    mesh.authoredRadius = radius;
    mesh.radius = radius;
    model.radius = radius;
    model.meshes.push_back(std::move(mesh));
    assets::Model3doNode node{};
    node.id = 0;
    node.type = 1;
    node.mesh = 0;
    node.name = "terrain";
    model.nodes.push_back(std::move(node));
    return model;
}

std::optional<std::size_t> levelBuildThingPath(
    Level& level,
    const assets::Templates* templates,
    const std::int32_t thing,
    const std::span<const FrameValue> waypoints)
{
    if ( thing < 0 )
    {
        return std::nullopt;
    }
    const PreparedThingFrames prepared =
        prepareSetThingFrames(level, templates, static_cast<std::size_t>(thing), waypoints);
    if ( !prepared.ok() || !prepared.apply(level) )
    {
        return std::nullopt;
    }
    return prepared.frames.size();
}

} // namespace sed::level
