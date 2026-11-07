#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "sed/level.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/workbench.hpp"

namespace sed::assets
{
class Templates;
} // namespace sed::assets

namespace sed::level
{

// Request domains and input derivation for the Thing, Sector and Surface
// recipe commands. The existing GUI consumes the Planarize and flag-repair
// helpers; the remaining operations must be integrated with one shared contract.

/** Most sectors, surfaces, vertices or Things one recipe request addresses. */
inline constexpr std::size_t kMaxRecipeTargets = 4096;

/** Build Stairs width, run, rise and headroom domain. */
inline constexpr double kMinStairDimension = 0.001;
inline constexpr double kMaxStairDimension = 100.0;

/** Place Sector Lights intensity domain and default. */
inline constexpr double kMinSectorLightIntensity = 0.01;
inline constexpr double kMaxSectorLightIntensity = 1000.0;
inline constexpr double kDefaultSectorLightIntensity = 1.0;

/** Make Underwater default tint; channels and the sound volume lie in [0, 1]. */
inline constexpr double kDefaultUnderwaterTintRed = 0.0;
inline constexpr double kDefaultUnderwaterTintGreen = 0.3;
inline constexpr double kDefaultUnderwaterTintBlue = 0.5;

/** Make Breakable Glass shards-per-point and spawn-points-per-corner domains. */
inline constexpr std::int32_t kMaxBreakableGlassDensity = 64;
inline constexpr std::int32_t kDefaultBreakableGlassDensity = 5;
inline constexpr std::int32_t kMaxBreakableGlassSpawnPoints = 16;
inline constexpr std::int32_t kDefaultBreakableGlassSpawnPoints = 2;

/** Scripts the door and glass recipes bind. */
inline constexpr std::string_view kMakeDoorScript = "00_door.cog";
inline constexpr std::string_view kBreakableGlassScript = "00_breakingglass.cog";

/**
 * Refuses (`invalidStairParameters`) an origin that is not finite within
 * kMaxWorkbenchCoordinate, a dimension outside the stair domain, a headroom
 * not above the rise, or a step count outside 1..kMaximumStairSteps.
 */
std::optional<WorkbenchFailure> validateStairParameters(const StairParameters& parameters);

/** Refuses (`invalidSectorLightIntensity`) an intensity outside its domain. */
std::optional<WorkbenchFailure> validateSectorLightIntensity(double intensity);

/** Refuses (`invalidUnderwaterSettings`) a tint channel or volume outside [0, 1]. */
std::optional<WorkbenchFailure> validateUnderwaterSettings(const UnderwaterSettings& settings);

/** Refuses (`invalidBreakableGlassSettings`) a density or spawn count outside its domain. */
std::optional<WorkbenchFailure> validateBreakableGlassSettings(const BreakableGlassSettings& settings);

/** Refuses (`invalidMaskedSurfaceRecipe`) an inverted/out-of-range slope
 * band, an empty selector mask, a bit both set and cleared, or copied
 * fields without a source surface. */
std::optional<WorkbenchFailure> validateMaskedSurfaceRecipe(const MaskedSurfaceRecipe& recipe);

/** Refuses (`invalidSwitchbackStairParameters`) switchback dimensions
 * outside the stair domain, headroom not above the rise, a landing below
 * the stair minimum, or a flight size outside 1..kMaximumStairSteps/2. */
std::optional<WorkbenchFailure> validateSwitchbackStairParameters(const SwitchbackStairParameters& parameters);

/** Refuses (`invalidSpiralStairParameters`) spiral radii outside the
 * stair domain or not strictly ordered, a step angle that is zero or a
 * half-turn or more in magnitude (the wedge would stop being convex),
 * headroom not above the rise, or a step count outside
 * 1..kMaximumStairSteps. */
std::optional<WorkbenchFailure> validateSpiralStairParameters(const SpiralStairParameters& parameters);

/** Refuses (`invalidDomeParameters`) a dome radius/headroom outside
 * the stair domain, fewer than 3 or more than 64 segments, rings
 * outside 1..64, more than kMaximumStairSteps wedge sectors in total,
 * or a headroom that does not exceed the largest ring rise
 * (`radius * max(sin step)` over the elevation bands). */
std::optional<WorkbenchFailure> validateDomeParameters(const DomeParameters& parameters);

/** Refuses (`invalidSweptCorridorParameters`) swept radii outside the
 * stair domain or not strictly ordered, a zero sweep or a per-segment
 * angle of a half-turn or more in magnitude (the wedge would stop
 * being convex), a non-finite rise, a headroom that does not exceed
 * the rise magnitude (neighbour walls would not overlap), or a segment
 * count outside 1..kMaximumStairSteps. */
std::optional<WorkbenchFailure> validateSweptCorridorParameters(const SweptCorridorParameters& parameters);

/** Refuses (`invalidInsetFactor`) a factor outside (0, 1). */
std::optional<WorkbenchFailure> validateInsetFactor(double factor);

/** Most cells one heightmap-terrain request may build. */
inline constexpr std::size_t kMaxTerrainCells = 4096;

/** Largest single-cell height offset magnitude for terrain. */
inline constexpr double kMaxTerrainHeight = 1000.0;

/** Refuses (`invalidTerrainParameters`) a non-finite or out-of-bound
 * origin, a cell size or headroom outside the stair dimension domain,
 * a grid outside 1..128 per side or above kMaxTerrainCells cells, a
 * height count that is not columns * rows, or a non-finite height
 * beyond kMaxTerrainHeight in magnitude. */
std::optional<WorkbenchFailure> validateTerrainParameters(const TerrainParameters& parameters);

/** Make Underwater's default settings: the default tint, no sound, full volume, water surfaces on. */
UnderwaterSettings defaultUnderwaterSettings();

/**
 * The opening height Make Door uses for a Thing: its own bounding box, or
 * when that is empty the box of its template (embedded Level templates by
 * case-insensitive name first, then the project database). Zero when the
 * Thing is unknown or no box has height; levelMakeDoor refuses that.
 */
double makeDoorHeight(const Level& level, const assets::Templates* templates, std::size_t thingIndex) noexcept;

/**
 * Make Breakable Glass defaults for one pane: the selected side's current
 * material, and the script's `density` and `spawnpoints` symbols (else the
 * defaults above) clamped to their domains.
 */
BreakableGlassSettings breakableGlassDefaults(
    const Level& level,
    SurfaceRef glass,
    std::span<const CogValue> scriptSymbols);

/** Planarize's work list: the valid, non-planar surfaces in (sector, surface) order. */
std::vector<SurfaceRef> planarizeTargets(const Level& level, std::span<const SurfaceRef> surfaces);

struct PlanarizeResult
{
    bool changed{};
    std::size_t stillNonPlanar{};
};

/**
 * Flattens each target in order. `changed` reflects actual vertex/normal
 * differences, not the primitive's success flag. After all operations,
 * `stillNonPlanar` counts unresolved targets, including invalid references.
 */
PlanarizeResult levelPlanarizeSurfaces(Level& level, std::span<const SurfaceRef> targets);

/**
 * Fix Solid Surfaces / Fix Passable Adjoins work list: every surface of each
 * valid sector plus each listed surface, sorted by (sector, surface), unique.
 */
std::vector<SurfaceRef> surfaceFlagRepairTargets(
    const Level& level,
    std::span<const std::int32_t> sectors,
    std::span<const SurfaceRef> surfaces);

} // namespace sed::level
