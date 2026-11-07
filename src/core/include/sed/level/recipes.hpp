#pragma once

#include "sed/assets/3do.hpp"
#include "sed/level.hpp"
#include "sed/level/workbench.hpp"
#include "sed/parse_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{
class Templates;
} // namespace sed::assets

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

/** Surface fields Copy SF can take from a source surface (E-02). */
enum class MaskedCopyField : std::uint32_t
{
    None = 0,
    Material = 1u << 0,
    Geometry = 1u << 1,
    ExtraLight = 1u << 2,
    SurfaceFlags = 1u << 3,
    FaceFlags = 1u << 4,
    AdjoinFlags = 1u << 5,
    UvScale = 1u << 6,
};

/** Repeatable masked batch edit over candidate surfaces (E-02).
 *
 * A candidate matches when every present any/all/none selector matches the
 * surface's flag words and the slope of its stored normal — the angle from
 * horizontal, so 0 degrees is a floor or ceiling and 90 degrees a wall —
 * lies inside [minSlopeDegrees, maxSlopeDegrees]. Matched surfaces first
 * copy the `copyFields` selected from `copyFrom`, then gain `set*` bits and
 * lose `clear*` bits, then take the direct `material` override when present.
 * Unrelated bits and unselected fields are preserved, so applying the same
 * recipe twice changes nothing the second time.
 */
struct MaskedSurfaceRecipe
{
    std::optional<FlagMaskPredicate> surfaceFlags{};
    std::optional<FlagMaskPredicate> faceFlags{};
    std::optional<FlagMaskPredicate> adjoinFlags{};
    double minSlopeDegrees{ 0.0 };
    double maxSlopeDegrees{ 90.0 };
    std::optional<SurfaceRef> copyFrom{};
    std::uint32_t copyFields{};
    std::uint32_t setSurfaceFlags{};
    std::uint32_t clearSurfaceFlags{};
    std::uint32_t setFaceFlags{};
    std::uint32_t clearFaceFlags{};
    std::uint32_t setAdjoinFlags{};
    std::uint32_t clearAdjoinFlags{};
    std::optional<std::string> material{};
};

struct MaskedSurfaceResult
{
    std::size_t matched{};
    std::size_t changed{};
};

/** Applies one masked recipe to the candidate surfaces. Invalid candidates
 * are ignored; an invalid recipe changes nothing. */
MaskedSurfaceResult levelApplyMaskedSurfaceRecipe(
    Level& level, std::span<const SurfaceRef> candidates, const MaskedSurfaceRecipe& recipe);

/** Parameters for Build Switchback Stairs (E-05 landing stairs). */
struct SwitchbackStairParameters
{
    TVector origin{};
    StairDirection direction{ StairDirection::PositiveX };
    double width{ 1.0 };
    double run{ 0.25 };
    double rise{ 0.125 };
    double headroom{ 1.0 };
    double landingRun{ 0.5 };
    std::int32_t stepsPerFlight{ 2 };
};

/** Builds two straight flights joined by a landing: the second flight
 * returns over the first, offset by one width, and keeps rising. Sectors
 * are returned in climbing order (flight, landing, flight). The Level is
 * unchanged on refusal. */
std::optional<std::vector<std::int32_t>> levelBuildSwitchbackStairs(
    Level& level, const SwitchbackStairParameters& parameters);

/** Parameters for Build Spiral Stairs (E-05).
 *
 * Each step is a wedge prism around the vertical axis through `center`:
 * its footprint spans `innerRadius..outerRadius` and one signed
 * `stepAngleDegrees` increment starting at `startAngleDegrees`, and its
 * floor sits `rise` above the previous step's floor. A negative step
 * angle winds clockwise. */
struct SpiralStairParameters
{
    TVector center{};
    double innerRadius{ 0.5 };
    double outerRadius{ 1.5 };
    double startAngleDegrees{ 0.0 };
    double stepAngleDegrees{ 30.0 };
    double rise{ 0.125 };
    double headroom{ 1.0 };
    std::int32_t steps{ 4 };
};

/** Builds a spiral flight of wedge sectors, each adjoined to the next
 * through the overlap of their shared radial wall, in climbing order.
 * The Level is unchanged on refusal. */
std::optional<std::vector<std::int32_t>> levelBuildSpiralStairs(
    Level& level, const SpiralStairParameters& parameters);

/** Parameters for Build Dome (E-05).
 *
 * The dome is a stepped hemisphere of annular wedge rings around the
 * vertical axis through `center`: ring k spans the elevation band
 * [k/rings, (k+1)/rings] of the quarter circle, so its outer radius is
 * `radius * cos(phi)` and its floor `center.z + radius * sin(phi)` at
 * the band's lower edge. Each ring has `segments` wedge sectors. The
 * top ring closes to a small flat polygon (its inner radius is the
 * stair minimum), not a point. */
struct DomeParameters
{
    TVector center{};
    double radius{ 2.0 };
    double headroom{ 1.0 };
    std::int32_t segments{ 8 };
    std::int32_t rings{ 4 };
};

/** Builds the dome's wedge sectors ring by ring (base ring first,
 * segment order within a ring), adjoining neighbours around each ring
 * (including the wrap) and each sector to the one below it. The Level
 * is unchanged on refusal. */
std::optional<std::vector<std::int32_t>> levelBuildDome(
    Level& level, const DomeParameters& parameters);

/** Parameters for Build Swept Corridor (E-05, the Get Bent /
 * TransSectoral sweep).
 *
 * A corridor cross-section (`outerRadius - innerRadius` wide around
 * the vertical axis through `center`) is swept along an arc: each of
 * the `segments` wedge sectors spans `sweepDegrees / segments` (signed;
 * negative sweeps clockwise) starting at `startAngleDegrees`, and its
 * floor sits `risePerSegment` above the previous segment's floor. Zero
 * rise gives a flat bent corridor; a signed rise gives a ramp. */
struct SweptCorridorParameters
{
    TVector center{};
    double innerRadius{ 1.0 };
    double outerRadius{ 2.0 };
    double startAngleDegrees{ 0.0 };
    double sweepDegrees{ 90.0 };
    std::int32_t segments{ 3 };
    double risePerSegment{ 0.0 };
    double headroom{ 1.0 };
};

/** Builds the swept corridor's wedge sectors in sweep order, each
 * adjoined to the next through their shared radial wall. The Level is
 * unchanged on refusal. */
std::optional<std::vector<std::int32_t>> levelBuildSweptCorridor(
    Level& level, const SweptCorridorParameters& parameters);

/** Moves a surface's corners toward its centroid by `factor` in (0, 1)
 * (E-06 inset/scale). Refuses adjoined surfaces, invalid references, and
 * results that would break planarity or convexity, leaving the Level
 * unchanged. */
bool levelInsetSurface(Level& level, SurfaceRef surface, double factor);

/** Insets both sides of an adjoin by the same factor atomically (E-06
 * paired-adjoin transformation). The Level is unchanged on refusal. */
bool levelInsetAdjoinPair(Level& level, SurfaceRef surface, double factor);

/** One sector's redundant-vertex candidates, reported without edits (E-07). */
struct VertexCleanupCandidate
{
    std::int32_t sector{ -1 };
    std::size_t mergedDuplicates{};
    std::size_t removedUnused{};
    std::size_t removedCollinear{};
    bool wouldSkip{};

    std::size_t removed() const noexcept
    {
        return mergedDuplicates + removedUnused + removedCollinear;
    }
};

/** Reports what Remove Redundant Vertices would do per valid sector,
 * without changing the Level. */
std::vector<VertexCleanupCandidate> levelReportRedundantVertices(
    const Level& level, std::span<const std::int32_t> sectors);

/** Fans each referenced convex, unadjoined surface into triangles around a
 * new centroid vertex, interpolating UVs (E-07 subdivision). Surfaces that
 * refuse are skipped; returns how many were subdivided. */
std::size_t levelSubdivideSurfaces(Level& level, std::span<const SurfaceRef> surfaces);

/** Parameters for Build Heightmap Terrain (E-14).
 *
 * `heights` holds one floor height per cell in row-major order
 * (row * columns + column), each added to `origin.z`. Every cell becomes
 * one box sector whose floor sits at its height and whose ceiling is
 * `headroom` above that floor. */
struct TerrainParameters
{
    TVector origin{};
    double cellSize{ 1.0 };
    double headroom{ 1.0 };
    std::int32_t columns{ 1 };
    std::int32_t rows{ 1 };
    std::vector<double> heights{};
};

/** Builds a grid of box sectors from per-cell floor heights (E-14).
 *
 * Neighbouring cells are adjoined where their shared walls overlap;
 * a steep step whose walls do not overlap stays a solid cliff face.
 * Invalid parameters (see `validateTerrainParameters`) return no
 * sectors and leave the Level unchanged. Returns the new sector
 * indices in row-major cell order. */
std::optional<std::vector<std::int32_t>> levelBuildHeightmapTerrain(
    Level& level, const TerrainParameters& parameters);

/** Builds the same heightmap as a single-mesh 3DO model (E-14 model
 * terrain output, the 3DO Heightmap Generator plug-in).
 *
 * The mesh grids (columns + 1) x (rows + 1) nodes; each node's height is
 * the mean of the touching cells' heights, and each cell becomes two
 * upward-wound triangles with normalized UVs. The model is 3DO 2.3 with
 * one material: `materialName`, or `dflt.mat` when empty. A material
 * name the 3DO text format cannot hold (empty after the default, or
 * containing whitespace) and invalid terrain parameters return nothing.
 * Node heights ignore `headroom`, which only sizes sector ceilings. */
std::optional<assets::Model3do> levelBuildHeightmapModel(
    const TerrainParameters& parameters, std::string_view materialName);

/** Replaces one Thing's path frames with the given waypoints (E-09
 * Pathfinder recipe).
 *
 * The waypoints become the Thing's local `frame` values in order,
 * composed through the same validated preparation the GUI frame
 * editor and the agent `setThingFrames` operation use, so inherited
 * Path capacity, typed-frame parsing, and coordinate bounds all apply
 * unchanged. Returns the number of frames written. On any refusal the
 * Level is unchanged and the result is empty. */
std::optional<std::size_t> levelBuildThingPath(
    Level& level,
    const assets::Templates* templates,
    std::int32_t thing,
    std::span<const FrameValue> waypoints);

} // namespace sed::level
