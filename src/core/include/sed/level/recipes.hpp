#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed::level
{

/** Outcome of Remove Redundant Vertices (the JED Vertex Assassin plug-in). */
struct VertexCleanupResult
{
    std::size_t mergedDuplicates{};
    std::size_t removedUnused{};
    std::size_t removedCollinear{};
    std::size_t skippedSectors{};

    std::size_t removed() const noexcept
    {
        return mergedDuplicates + removedUnused + removedCollinear;
    }
};

/** Removes redundant vertices from each referenced sector.
 *
 * Coincident vertices merge into the lowest index, vertices no surface uses
 * are dropped, and edge vertices that are collinear in every surface using
 * them are removed through the safe single-vertex delete. Geometry, UVs, and
 * surface indices (and so COG surface references) are unchanged. A sector
 * whose merge would leave a polygon with fewer than three corners, or repeat
 * a corner, is skipped and counted in `skippedSectors`. Invalid and repeated
 * indices are ignored.
 */
VertexCleanupResult levelRemoveRedundantVertices(Level& level, std::span<const std::int32_t> sectors);

/** Places one source light in each referenced sector (the JED Light Fantastic plug-in).
 *
 * The light sits at the vertex centroid, which lies inside every convex
 * sector; a sector whose centroid is not inside it is skipped. The range
 * reaches the farthest sector vertex, and the light takes the sector's layer.
 * Intensity must be finite and within 0.01..1000; an invalid value returns no
 * new indices and leaves the Level unchanged. Returns the new light indices.
 */
std::vector<std::int32_t> levelPlaceSectorLights(
    Level& level, std::span<const std::int32_t> sectors, double intensity);

enum class StairDirection : std::uint8_t
{
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
};

/** Parameters for Build Stairs (the JED Stair Crazy plug-in). */
struct StairParameters
{
    TVector origin{};
    StairDirection direction{ StairDirection::PositiveX };
    double width{ 1.0 };
    double run{ 0.25 };
    double rise{ 0.125 };
    double headroom{ 1.0 };
    std::int32_t steps{ 4 };
};

inline constexpr std::int32_t kMaximumStairSteps = 256;

/** Builds a flight of box sectors, each `rise` higher than the last, and
 * adjoins each step to the next through the overlap of their walls.
 *
 * The first step's near bottom corner is `origin`; the flight climbs along
 * `direction`, and its width extends along +Y for X flights and +X for Y
 * flights. Sizes must be finite within 0.001..100, `headroom > rise`, and
 * the step count within 1..kMaximumStairSteps. Origin components must be
 * finite and bounded to magnitude 1,000,000. The Level is unchanged on
 * refusal. Returns the new sector indices in climbing order.
 */
std::optional<std::vector<std::int32_t>> levelBuildStairs(Level& level, const StairParameters& parameters);

/** Moves `fit`'s corners onto `anchor`'s and adjoins the pair (the JED Adjoin Jiffy plug-in).
 *
 * Both surfaces must be unadjoined, in different sectors, and have the same
 * corner count. Corners pair in reverse winding at the rotation with the
 * least total movement. The fitted sector must stay planar and convex, and
 * no moved vertex may belong to another adjoined surface. The Level is
 * unchanged on refusal.
 */
bool levelFitAndAdjoinSurfaces(Level& level, SurfaceRef anchor, SurfaceRef fit);

/** Underwater sector settings (the JED Water Works plug-in). */
struct UnderwaterSettings
{
    ColorF tint{ ColorWhite };
    std::string sound{};
    double soundVolume{ 1.0 };
    // Configure every adjoin between a selected sector and a dry sector as a
    // water surface: both sides visible, passable, sound-passing, drawn
    // translucent, and without solid collision.
    bool configureWaterSurfaces{ true };
    // Material for the water surface; empty keeps each side's material.
    std::string waterSurfaceMaterial{};
};

struct UnderwaterResult
{
    std::size_t sectors{};
    std::size_t boundarySurfaces{};

    std::size_t changed() const noexcept
    {
        return sectors + boundarySurfaces;
    }
};

/** Marks each referenced sector underwater and applies the tint and ambient
 * sound, preserving other sector flags. When `configureWaterSurfaces` is set,
 * each adjoin from a selected sector into a sector that stays dry becomes a
 * water surface on both sides. The engine only lets a swimmer surface through
 * an adjoin that allows movement (OpenJKDF2 `sithPhysics`); authored AI and
 * player movement limits are kept. Tint channels and sound volume must be
 * finite within 0..1; invalid settings return no changes and leave the Level
 * unchanged. Returns what changed.
 */
UnderwaterResult levelMakeUnderwaterSectors(
    Level& level, std::span<const std::int32_t> sectors, const UnderwaterSettings& settings);

enum class BreakableGlassError
{
    None,
    InvalidSurface,
    NotAdjoined,
    MissingSymbols,
    AlreadyGlass,
    IdSpaceExhausted,
    InvalidSettings,
};

struct BreakableGlassSettings
{
    // Material for both sides of the pane; empty keeps each side's material.
    std::string material{};
    // Overrides for the script's `spawnpoints` and `density` symbols.
    std::optional<std::int32_t> spawnPoints{};
    std::optional<std::int32_t> density{};
};

struct BreakableGlassResult
{
    BreakableGlassError error{ BreakableGlassError::None };
    std::int32_t cog{ -1 };
};

/** Turns an adjoined surface and its adjoin into a breakable glass pane
 * driven by a script with the retail `00_breakingglass.cog` contract: the
 * first two surface symbols (`glass`, `glass2`) receive the selected side and
 * its adjoin. On `damaged` that script hides both faces and adds ADJOIN_MOVE,
 * so the pane is set up drawn, translucent, visible, impassable, COG-linked,
 * and without the solid-collision flag that would keep the broken pane
 * blocking. Refuses, leaving the level unchanged, for an invalid or
 * unadjoined surface, a script without two surface symbols, a pane already
 * bound to `scriptName`, exhausted entity IDs, or density/spawn-point
 * overrides outside 0..64 and 0..16 respectively.
 */
BreakableGlassResult levelMakeBreakableGlass(
    Level& level,
    SurfaceRef glass,
    std::string_view scriptName,
    const std::vector<CogValue>& scriptSymbols,
    const BreakableGlassSettings& settings);

} // namespace sed::level
