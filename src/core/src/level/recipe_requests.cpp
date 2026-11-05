#include "sed/level/recipe_requests.hpp"

#include "sed/assets/tpl.hpp"
#include "sed/level/geometry.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace sed::level
{
namespace
{

bool unitInterval(const double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

bool stairDimension(const double value) noexcept
{
    return std::isfinite(value)
        && value >= kMinStairDimension
        && value <= kMaxStairDimension;
}

bool workbenchCoordinate(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= kMaxWorkbenchCoordinate;
}

const Surface* surfaceIfValid(const Level& level, const SurfaceRef ref) noexcept
{
    if (ref.sector < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size())
    {
        return nullptr;
    }
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    if (ref.surface < 0 || static_cast<std::size_t>(ref.surface) >= sector.surfaces.size())
    {
        return nullptr;
    }
    return &sector.surfaces[static_cast<std::size_t>(ref.surface)];
}

bool surfaceRefLess(const SurfaceRef left, const SurfaceRef right) noexcept
{
    return left.sector != right.sector
        ? left.sector < right.sector
        : left.surface < right.surface;
}

double usableBoxHeight(const ThingBox& box) noexcept
{
    const double height = box.z2 - box.z1;
    return std::isfinite(height) && height > 0.0 ? height : 0.0;
}

} // namespace

std::optional<WorkbenchFailure> validateStairParameters(const StairParameters& parameters)
{
    if (!stairDimension(parameters.width)
        || !stairDimension(parameters.run)
        || !stairDimension(parameters.rise)
        || !stairDimension(parameters.headroom)
        || parameters.headroom <= parameters.rise
        || parameters.steps < 1
        || parameters.steps > kMaximumStairSteps
        || !workbenchCoordinate(parameters.origin.x)
        || !workbenchCoordinate(parameters.origin.y)
        || !workbenchCoordinate(parameters.origin.z))
    {
        return WorkbenchFailure{
            "invalidStairParameters",
            "Stair dimensions must be within 0.001..100, headroom must exceed the rise, "
            "the step count must be within 1..256, and the origin must be finite and bounded.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateSectorLightIntensity(const double intensity)
{
    if (!std::isfinite(intensity)
        || intensity < kMinSectorLightIntensity
        || intensity > kMaxSectorLightIntensity)
    {
        return WorkbenchFailure{
            "invalidSectorLightIntensity",
            "Sector light intensity must be finite and within 0.01..1000.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateUnderwaterSettings(const UnderwaterSettings& settings)
{
    if (!unitInterval(settings.tint.r)
        || !unitInterval(settings.tint.g)
        || !unitInterval(settings.tint.b)
        || !unitInterval(settings.soundVolume))
    {
        return WorkbenchFailure{
            "invalidUnderwaterSettings",
            "Underwater tint channels and sound volume must be finite and within 0..1.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateBreakableGlassSettings(const BreakableGlassSettings& settings)
{
    const auto validCount = [](const std::optional<std::int32_t> value, const std::int32_t maximum)
    {
        return !value || (*value >= 0 && *value <= maximum);
    };
    if (!validCount(settings.density, kMaxBreakableGlassDensity)
        || !validCount(settings.spawnPoints, kMaxBreakableGlassSpawnPoints))
    {
        return WorkbenchFailure{
            "invalidBreakableGlassSettings",
            "Breakable glass density must be within 0..64 and spawn points within 0..16.",
        };
    }
    return std::nullopt;
}

UnderwaterSettings defaultUnderwaterSettings()
{
    UnderwaterSettings settings{};
    settings.tint = ColorF(
        static_cast<float>(kDefaultUnderwaterTintRed),
        static_cast<float>(kDefaultUnderwaterTintGreen),
        static_cast<float>(kDefaultUnderwaterTintBlue));
    settings.sound.clear();
    settings.soundVolume = 1.0;
    settings.configureWaterSurfaces = true;
    settings.waterSurfaceMaterial.clear();
    return settings;
}

double makeDoorHeight(
    const Level& level,
    const assets::Templates* templates,
    const std::size_t thingIndex) noexcept
{
    if (thingIndex >= level.things.size())
    {
        return 0.0;
    }
    const Thing& thing = level.things[thingIndex];
    if (!thing.bbox.IsZero())
    {
        return usableBoxHeight(thing.bbox);
    }

    const auto embedded = std::find_if(
        level.templates.begin(),
        level.templates.end(),
        [&thing](const ThingTemplate& thingTemplate)
        {
            return text::iequalsAscii(thingTemplate.name, thing.templateName);
        });
    if (embedded != level.templates.end())
    {
        return usableBoxHeight(embedded->bbox);
    }
    const ThingTemplate* const projectTemplate = templates
        ? templates->find(thing.templateName)
        : nullptr;
    return projectTemplate ? usableBoxHeight(projectTemplate->bbox) : 0.0;
}

BreakableGlassSettings breakableGlassDefaults(
    const Level& level,
    const SurfaceRef glass,
    const std::span<const CogValue> scriptSymbols)
{
    BreakableGlassSettings settings{};
    settings.density = kDefaultBreakableGlassDensity;
    settings.spawnPoints = kDefaultBreakableGlassSpawnPoints;
    if (const Surface* const surface = surfaceIfValid(level, glass))
    {
        settings.material = std::string(text::trimView(surface->material));
    }
    for (const CogValue& symbol : scriptSymbols)
    {
        if (text::iequalsAscii(symbol.name, "density"))
        {
            settings.density = std::clamp(symbol.intValue, 0, kMaxBreakableGlassDensity);
        }
        else if (text::iequalsAscii(symbol.name, "spawnpoints"))
        {
            settings.spawnPoints = std::clamp(symbol.intValue, 0, kMaxBreakableGlassSpawnPoints);
        }
    }
    return settings;
}

std::vector<SurfaceRef> planarizeTargets(
    const Level& level,
    const std::span<const SurfaceRef> surfaces)
{
    std::vector<SurfaceRef> targets{};
    targets.reserve(surfaces.size());
    for (const SurfaceRef ref : surfaces)
    {
        const Surface* const surface = surfaceIfValid(level, ref);
        if (surface
            && !surfaceIsPlanar(
                level.sectors[static_cast<std::size_t>(ref.sector)], *surface))
        {
            targets.push_back(ref);
        }
    }
    std::sort(targets.begin(), targets.end(), surfaceRefLess);
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    return targets;
}

PlanarizeResult levelPlanarizeSurfaces(
    Level& level,
    const std::span<const SurfaceRef> targets)
{
    PlanarizeResult result{};
    for (const SurfaceRef ref : targets)
    {
        const Surface* const surface = surfaceIfValid(level, ref);
        if (!surface)
        {
            continue;
        }

        Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
        std::vector<std::size_t> vertexIndices{};
        vertexIndices.reserve(surface->vertices.size());
        for (const std::int32_t index : surface->vertices)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size())
            {
                vertexIndices.clear();
                break;
            }
            vertexIndices.push_back(static_cast<std::size_t>(index));
        }
        if (vertexIndices.size() != surface->vertices.size())
        {
            (void)levelFlattenSurface(level, ref);
            continue;
        }

        std::vector<std::array<double, 3>> positions{};
        positions.reserve(vertexIndices.size());
        for (const std::size_t index : vertexIndices)
        {
            const Vertex& vertex = sector.vertices[index];
            positions.push_back({ vertex.x, vertex.y, vertex.z });
        }
        std::vector<TVector> normals{};
        normals.reserve(sector.surfaces.size());
        for (const Surface& current : sector.surfaces)
        {
            normals.push_back(current.normal);
        }

        (void)levelFlattenSurface(level, ref);
        for (std::size_t index = 0; index < vertexIndices.size(); ++index)
        {
            const Vertex& vertex = sector.vertices[vertexIndices[index]];
            const std::array<double, 3>& before = positions[index];
            result.changed = result.changed
                || vertex.x != before[0] || vertex.y != before[1] || vertex.z != before[2];
        }
        for (std::size_t index = 0; index < sector.surfaces.size(); ++index)
        {
            const TVector& before = normals[index];
            const TVector& after = sector.surfaces[index].normal;
            result.changed = result.changed
                || after.x != before.x || after.y != before.y || after.z != before.z;
        }
    }
    for (const SurfaceRef ref : targets)
    {
        const Surface* const surface = surfaceIfValid(level, ref);
        if (!surface
            || !surfaceIsPlanar(
                level.sectors[static_cast<std::size_t>(ref.sector)], *surface))
        {
            ++result.stillNonPlanar;
        }
    }
    return result;
}

std::vector<SurfaceRef> surfaceFlagRepairTargets(
    const Level& level,
    const std::span<const std::int32_t> sectors,
    const std::span<const SurfaceRef> surfaces)
{
    std::vector<SurfaceRef> targets{};
    for (const std::int32_t sectorIndex : sectors)
    {
        if (sectorIndex < 0
            || static_cast<std::size_t>(sectorIndex) >= level.sectors.size())
        {
            continue;
        }
        const std::size_t count =
            level.sectors[static_cast<std::size_t>(sectorIndex)].surfaces.size();
        for (std::size_t index = 0; index < count; ++index)
        {
            targets.push_back(SurfaceRef{
                sectorIndex,
                static_cast<std::int32_t>(index),
            });
        }
    }
    for (const SurfaceRef ref : surfaces)
    {
        if (surfaceIfValid(level, ref))
        {
            targets.push_back(ref);
        }
    }
    std::sort(targets.begin(), targets.end(), surfaceRefLess);
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    return targets;
}

} // namespace sed::level
