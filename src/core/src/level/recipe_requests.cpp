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

std::optional<WorkbenchFailure> validateMaskedSurfaceRecipe(const MaskedSurfaceRecipe& recipe)
{
    const auto refused = []()
    {
        return std::optional<WorkbenchFailure>{ WorkbenchFailure{
            "invalidMaskedSurfaceRecipe",
            "Slope bounds must satisfy 0 <= min <= max <= 90, selector masks must be non-empty, "
            "no bit may be both set and cleared, and copied fields require a source surface.",
        } };
    };
    if (!std::isfinite(recipe.minSlopeDegrees) || !std::isfinite(recipe.maxSlopeDegrees)
        || recipe.minSlopeDegrees < 0.0 || recipe.maxSlopeDegrees > 90.0
        || recipe.minSlopeDegrees > recipe.maxSlopeDegrees)
    {
        return refused();
    }
    for (const std::optional<FlagMaskPredicate>* mask :
        { &recipe.surfaceFlags, &recipe.faceFlags, &recipe.adjoinFlags })
    {
        if (mask->has_value() && (*mask)->empty())
        {
            return refused();
        }
    }
    if ((recipe.setSurfaceFlags & recipe.clearSurfaceFlags) != 0u
        || (recipe.setFaceFlags & recipe.clearFaceFlags) != 0u
        || (recipe.setAdjoinFlags & recipe.clearAdjoinFlags) != 0u)
    {
        return refused();
    }
    if (recipe.copyFields != 0u && !recipe.copyFrom.has_value())
    {
        return refused();
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateSwitchbackStairParameters(const SwitchbackStairParameters& parameters)
{
    if (!stairDimension(parameters.width)
        || !stairDimension(parameters.run)
        || !stairDimension(parameters.rise)
        || !stairDimension(parameters.headroom)
        || !stairDimension(parameters.landingRun)
        || parameters.headroom <= parameters.rise
        || parameters.stepsPerFlight < 1
        || parameters.stepsPerFlight > kMaximumStairSteps / 2
        || !workbenchCoordinate(parameters.origin.x)
        || !workbenchCoordinate(parameters.origin.y)
        || !workbenchCoordinate(parameters.origin.z))
    {
        return WorkbenchFailure{
            "invalidSwitchbackStairParameters",
            "Stair dimensions and the landing run must be within 0.001..100, headroom must exceed "
            "the rise, each flight must have 1..128 steps, and the origin must be finite and bounded.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateSpiralStairParameters(const SpiralStairParameters& parameters)
{
    if (!stairDimension(parameters.innerRadius)
        || !stairDimension(parameters.outerRadius)
        || parameters.innerRadius >= parameters.outerRadius
        || !stairDimension(parameters.rise)
        || !stairDimension(parameters.headroom)
        || parameters.headroom <= parameters.rise
        || !std::isfinite(parameters.startAngleDegrees)
        || !std::isfinite(parameters.stepAngleDegrees)
        || parameters.stepAngleDegrees == 0.0
        || std::abs(parameters.stepAngleDegrees) >= 180.0
        || parameters.steps < 1
        || parameters.steps > kMaximumStairSteps
        || !workbenchCoordinate(parameters.center.x)
        || !workbenchCoordinate(parameters.center.y)
        || !workbenchCoordinate(parameters.center.z))
    {
        return WorkbenchFailure{
            "invalidSpiralStairParameters",
            "Spiral inner/outer radii, rise and headroom must be within 0.001..100 with the inner "
            "radius below the outer, headroom must exceed the rise, the step angle must be finite, "
            "non-zero and below 180 degrees in magnitude, the step count must be within 1..256, "
            "and the center must be finite and bounded.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateDomeParameters(const DomeParameters& parameters)
{
    bool valid = stairDimension(parameters.radius)
        && stairDimension(parameters.headroom)
        && parameters.segments >= 3 && parameters.segments <= 64
        && parameters.rings >= 1 && parameters.rings <= 64
        && workbenchCoordinate(parameters.center.x)
        && workbenchCoordinate(parameters.center.y)
        && workbenchCoordinate(parameters.center.z);
    if (valid)
    {
        valid = static_cast<std::int64_t>(parameters.segments)
                * static_cast<std::int64_t>(parameters.rings)
            <= static_cast<std::int64_t>(kMaximumStairSteps);
    }
    if (valid)
    {
        const double quarter = std::acos(-1.0) / 2.0;
        double maxRise = 0.0;
        for (std::int32_t ring = 0; ring < parameters.rings; ++ring)
        {
            const double low = std::sin(quarter * static_cast<double>(ring)
                / static_cast<double>(parameters.rings));
            const double high = std::sin(quarter * static_cast<double>(ring + 1)
                / static_cast<double>(parameters.rings));
            maxRise = std::max(maxRise, parameters.radius * (high - low));
        }
        valid = parameters.headroom > maxRise;
    }
    if (!valid)
    {
        return WorkbenchFailure{
            "invalidDomeParameters",
            "Dome radius and headroom must be within 0.001..100, segments within 3..64, rings "
            "within 1..64, segments x rings at most 256, headroom must exceed the largest ring "
            "rise, and the center must be finite and bounded.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateSweptCorridorParameters(const SweptCorridorParameters& parameters)
{
    const double perSegment = parameters.segments != 0
        ? parameters.sweepDegrees / static_cast<double>(parameters.segments)
        : 0.0;
    if (!stairDimension(parameters.innerRadius)
        || !stairDimension(parameters.outerRadius)
        || parameters.innerRadius >= parameters.outerRadius
        || !stairDimension(parameters.headroom)
        || !std::isfinite(parameters.startAngleDegrees)
        || !std::isfinite(parameters.sweepDegrees)
        || parameters.sweepDegrees == 0.0
        || !std::isfinite(perSegment) || std::abs(perSegment) >= 180.0
        || !std::isfinite(parameters.risePerSegment)
        || std::abs(parameters.risePerSegment) >= parameters.headroom
        || parameters.segments < 1
        || parameters.segments > kMaximumStairSteps
        || !workbenchCoordinate(parameters.center.x)
        || !workbenchCoordinate(parameters.center.y)
        || !workbenchCoordinate(parameters.center.z))
    {
        return WorkbenchFailure{
            "invalidSweptCorridorParameters",
            "Swept radii and headroom must be within 0.001..100 with the inner radius below "
            "the outer, the sweep must be finite and non-zero with under 180 degrees per "
            "segment, the rise must be finite and below the headroom in magnitude, the "
            "segment count must be within 1..256, and the center must be finite and bounded.",
        };
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> validateInsetFactor(const double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0 || factor >= 1.0)
    {
        return WorkbenchFailure{
            "invalidInsetFactor",
            "Inset factor must be finite and strictly inside (0, 1).",
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

std::optional<WorkbenchFailure> validateTerrainParameters(const TerrainParameters& parameters)
{
    const bool gridValid = parameters.columns >= 1 && parameters.columns <= 128
        && parameters.rows >= 1 && parameters.rows <= 128
        && static_cast<std::size_t>(parameters.columns) * static_cast<std::size_t>(parameters.rows)
            <= kMaxTerrainCells;
    const bool heightsValid = gridValid
        && parameters.heights.size()
            == static_cast<std::size_t>(parameters.columns) * static_cast<std::size_t>(parameters.rows)
        && std::all_of(parameters.heights.begin(), parameters.heights.end(),
            [](const double height)
            {
                return std::isfinite(height) && std::abs(height) <= kMaxTerrainHeight;
            });
    if (!workbenchCoordinate(parameters.origin.x)
        || !workbenchCoordinate(parameters.origin.y)
        || !workbenchCoordinate(parameters.origin.z)
        || !stairDimension(parameters.cellSize)
        || !stairDimension(parameters.headroom)
        || !heightsValid)
    {
        return WorkbenchFailure{
            "invalidTerrainParameters",
            "Terrain needs a finite bounded origin, a cell size and headroom within "
            "0.001..100, a grid of 1..128 cells per side and at most 4096 cells, and "
            "one finite height per cell within 1000 of the origin height.",
        };
    }
    return std::nullopt;
}

} // namespace sed::level
