#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/geometry.hpp"
#include "sed/level.hpp"
#include "sed/level/cog_bindings.hpp"

namespace sed::level
{

/** Batch UV ceiling: one operation addresses at most this many surfaces. */
inline constexpr std::size_t kMaxBatchUvSurfaces = 256;

/** Batch UV ceiling: one operation addresses at most this many corners. */
inline constexpr std::size_t kMaxBatchUvCorners = 4096;

/** Placement ceiling: one array creates at most this many outputs. */
inline constexpr std::size_t kMaxPlacementArrayOutputs = 256;

/** Placement ceiling: one patch creates at most this many array outputs. */
inline constexpr std::size_t kMaxPlacementPatchOutputs = 1024;

/** Finite magnitude bound for workbench inputs and produced UVs. */
inline constexpr double kMaxWorkbenchCoordinate = 1'000'000.0;

/** Stable refusal for one workbench preparation or query. */
struct WorkbenchFailure
{
    std::string code{};
    std::string message{};
};

// ---------------------------------------------------------------- queries

/**
 * Unsigned flag masks combined with AND. `any` needs one listed bit set,
 * `all` needs every listed bit set, `none` forbids every listed bit, so
 * `any: 0` matches nothing while `all: 0` and `none: 0` are vacuous.
 */
struct FlagMaskPredicate
{
    std::optional<std::uint32_t> any{};
    std::optional<std::uint32_t> all{};
    std::optional<std::uint32_t> none{};

    [[nodiscard]] bool empty() const noexcept
    {
        return !any.has_value() && !all.has_value() && !none.has_value();
    }

    [[nodiscard]] bool matches(std::uint32_t bits) const noexcept
    {
        if (any.has_value() && (bits & *any) == 0u)
        {
            return false;
        }
        if (all.has_value() && (bits & *all) != *all)
        {
            return false;
        }
        if (none.has_value() && (bits & *none) != 0u)
        {
            return false;
        }
        return true;
    }
};

/**
 * Typed exact-match predicates. Text equality is ordinal case-insensitive
 * (ASCII) with significant whitespace; different predicates combine with
 * AND, and with the substring `search` where a collection supports one.
 * Which fields a collection accepts is part of the contract: supplying a
 * field another collection owns refuses instead of being ignored.
 */
struct ExactQueryFilter
{
    std::optional<std::int32_t> materialIndex{};
    std::optional<std::int32_t> layer{};
    std::optional<std::string> name{};
    std::optional<std::string> templateName{};
    std::optional<FlagMaskPredicate> flags{};
    std::optional<FlagMaskPredicate> surfFlags{};
    std::optional<FlagMaskPredicate> faceFlags{};
    std::optional<FlagMaskPredicate> adjoinFlags{};

    [[nodiscard]] bool empty() const noexcept
    {
        return !materialIndex.has_value() && !layer.has_value()
            && !name.has_value() && !templateName.has_value()
            && !flags.has_value() && !surfFlags.has_value()
            && !faceFlags.has_value() && !adjoinFlags.has_value();
    }
};

/** Collections the exact predicates address. */
enum class WorkbenchCollection
{
    surfaces,
    things,
    thingContainment,
    sectors,
    lights,
};

/** Page window over matching rows; `limit` above 200 clamps to 200. */
struct QueryPageRequest
{
    std::size_t offset{};
    std::size_t limit{ 50 };
};

/**
 * One exact query page. `indices` are original collection indices (surface
 * indices are local to the queried sector) in ascending order; paging
 * counts matching rows, not the unfiltered sequence. Only a refusal sets
 * `failure`; an unmatched filter is a successful empty page.
 */
struct QueryResult
{
    std::optional<WorkbenchFailure> failure{};
    std::vector<std::int32_t> indices{};
    std::size_t totalMatches{};
    std::optional<std::size_t> nextOffset{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }
};

/**
 * Runs one exact query. `filter` absent lists every row; a present but
 * empty filter refuses. `sectorIndex` is required for `surfaces` and must
 * be absent otherwise. `search` is a case-insensitive substring over thing
 * name or template, supported only by the thing collections.
 * `thingContainment` accepts the same predicates and returns the same
 * thing rows; containment status itself stays with spatial selection.
 */
QueryResult queryWorkbench(
    const Level& level,
    WorkbenchCollection collection,
    std::optional<std::int32_t> sectorIndex,
    const std::optional<ExactQueryFilter>& filter,
    std::string_view search,
    const QueryPageRequest& page = {});

/** Material-table slot of one surface; -1 untextured, -2 named but absent. */
std::int32_t surfaceMaterialIndex(const Level& level, const Surface& surface) noexcept;

// ------------------------------------------------------------- batch UV

/** Directed projection normal; the basis U cross V equals this direction. */
enum class UvProjectionDirection
{
    positiveX,
    negativeX,
    positiveY,
    negativeY,
    positiveZ,
    negativeZ,
};

/** A UV-space point for pivots, scales and offsets. */
struct WorkbenchUvPoint
{
    double u{};
    double v{};
};

/** Computed corner UVs for one surface, in corner order. */
struct BatchUvSurfacePlan
{
    SurfaceRef ref{};
    std::vector<std::int32_t> vertexIndices{};
    std::vector<TVector> positions{};
    std::vector<TexVertex> before{};
    std::vector<TexVertex> after{};
};

/**
 * A preflighted batch UV edit. Preparation computes every output before
 * any write; `apply` revalidates the exact prior geometry and UV state of
 * every surface, then writes only corner U/V values (vertex colours are
 * preserved) as one step, or writes nothing and returns false. Apply it
 * through `Document::applyUndoable` so the batch is one undo step.
 */
struct PreparedBatchUv
{
    std::optional<WorkbenchFailure> failure{};
    std::vector<BatchUvSurfacePlan> surfaces{};
    std::size_t cornerCount{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }

    /** Writes every prepared UV; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * Projects each addressed world position from one shared origin at one
 * texel density. Equal world positions receive equal UVs across sectors.
 * Edge-on, degenerate, non-planar or self-intersecting surfaces refuse.
 */
PreparedBatchUv prepareProjectSurfaceUv(
    const Level& level,
    std::span<const SurfaceRef> surfaces,
    UvProjectionDirection direction,
    const TVector& origin,
    double texelsPerUnit);

/**
 * Cylindrical projection (E-08): unwraps each corner around the axis
 * through `axisOrigin` along `axisDirection`. With A the unit axis and
 * E1/E2 the deterministic perpendicular frame (E1 from world X unless
 * the axis is near it, else world Y; E2 = A x E1), a corner at axial
 * height h and radial vector q gets u = atan2(q.E2, q.E1) * |q| and
 * v = h, both scaled by `texelsPerUnit` — arc length at the corner's
 * own radius. Corners on the axis (|q| = 0) get u = 0. Surfaces
 * crossing the +/-pi seam wrap in U (the classic seam artefact) rather
 * than refusing. Same preflight, limits and staleness contract as the
 * planar projection; a zero/non-finite axis or density refuses with
 * `invalidBatchUvInput`. */
PreparedBatchUv prepareCylindricalSurfaceUv(
    const Level& level,
    std::span<const SurfaceRef> surfaces,
    const TVector& axisOrigin,
    const TVector& axisDirection,
    double texelsPerUnit);

/**
 * Spherical projection (E-08): equirectangular unwrap around `center`
 * with pole along `poleDirection`. With A the unit pole and E1/E2 the
 * deterministic perpendicular frame (same rule as the cylindrical
 * projection), a corner at offset q with r = |q| gets polar angle
 * phi = acos((q.A) / r) in [0, pi] and azimuth
 * theta = atan2(q.E2, q.E1); u = theta * r and v = phi * r, both scaled
 * by `texelsPerUnit` — arc length at the corner's own radius. Corners
 * at the centre or on the pole axis get theta = 0. Surfaces crossing
 * the +/-pi seam or containing a pole wrap/collapse in UV rather than
 * refusing. Same preflight, limits and staleness contract as the
 * cylindrical projection; a zero/non-finite pole or density refuses
 * with `invalidBatchUvInput`. */
PreparedBatchUv prepareSphericalSurfaceUv(
    const Level& level,
    std::span<const SurfaceRef> surfaces,
    const TVector& center,
    const TVector& poleDirection,
    double texelsPerUnit);

/**
 * Scales about one shared pivot, rotates counter-clockwise in UV space
 * about that pivot, then offsets, for every addressed corner. Negative
 * scales mirror; zero scales refuse. Existing coincident UVs stay
 * coincident; unrelated UVs are never welded.
 */
PreparedBatchUv prepareTransformSurfaceUv(
    const Level& level,
    std::span<const SurfaceRef> surfaces,
    const WorkbenchUvPoint& pivot,
    const WorkbenchUvPoint& scale,
    double rotationDegrees,
    const WorkbenchUvPoint& offset);

// ------------------------------------------------------------ placements

/**
 * Checks a point against the same explicit-sector plane-distance guard used
 * by Thing arrays. Boundary points within kSpatialDistanceTolerance refuse.
 * Validates finite bounded coordinates and the sector index; callers needing
 * unique ownership/full topology classification also use spatial selection.
 */
std::optional<WorkbenchFailure> validatePlacementPointInsideSector(
    const Level& level, const TVector& point, std::int32_t sectorIndex);

/** Deterministic array shapes; generated order is part of the contract. */
enum class PlacementLayoutKind
{
    linear,
    grid,
    radial,
};

/**
 * One array shape. Linear walks `origin + i * step`; grid varies U
 * fastest; radial walks `startDegrees + sweepDegrees * i / count` with
 * the endpoint excluded. Radial placement never rotates its outputs:
 * thing orientation comes only from the array's fixed orientation.
 */
struct PlacementLayout
{
    PlacementLayoutKind kind{ PlacementLayoutKind::linear };
    TVector origin{};
    TVector step{};
    TVector stepU{};
    TVector stepV{};
    std::int32_t count{ 1 };
    std::int32_t countU{ 1 };
    std::int32_t countV{ 1 };
    TVector axis{ 0.0, 0.0, 1.0 };
    double radius{ 1.0 };
    double startDegrees{};
    double sweepDegrees{ 360.0 };
};

/** Positions for one layout, in deterministic generation order. */
struct PlacementPositions
{
    std::optional<WorkbenchFailure> failure{};
    std::vector<TVector> positions{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }
};

/**
 * Computes one layout's positions without touching a level. Counts above
 * 256, duplicate positions (including zero steps), non-finite or
 * out-of-bound inputs and degenerate radial shapes refuse.
 */
PlacementPositions computePlacementPositions(const PlacementLayout& layout);

/**
 * A validated thing array. Preparation resolves the template, generated
 * names (`namePrefix_000` upwards), layers and strict single-sector
 * containment for every point before any write; `apply` revalidates and
 * appends fresh things (no copied values or ARCH binding) in layout
 * order as one step, or appends nothing and returns false. When the
 * level has placed COGs the thing-append guard must pass: supply the
 * schema resolver, or preparation refuses. Apply through
 * `Document::applyUndoable`.
 */
struct PreparedThingArray
{
    std::optional<WorkbenchFailure> failure{};
    std::vector<TVector> positions{};
    std::vector<std::string> names{};
    std::vector<std::int32_t> sectorIndices{};
    std::string templateName{};
    TVector orientation{};
    std::int32_t layer{};
    std::optional<CogSchemaResolver> schemaResolver{};
    mutable std::optional<std::size_t> firstCreatedIndex{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }

    /** Appends every prepared thing; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * A validated light array; the light-side twin of `PreparedThingArray`.
 * Lights carry no containment requirement. MotS may specify independent
 * RGB intensity; absence inherits white, while explicit zero remains zero.
 */
struct PreparedLightArray
{
    std::optional<WorkbenchFailure> failure{};
    std::vector<TVector> positions{};
    ColorF color{ ColorWhite };
    double intensity{ 10.0 };
    std::optional<double> rgbIntensity{};
    double range{ 2.0 };
    std::int32_t flags{};
    std::int32_t layer{};
    mutable std::optional<std::size_t> firstCreatedIndex{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }

    /** Appends every prepared light; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * Prepares one thing array from precomputed positions (see
 * `computePlacementPositions`). `patchOutputsBefore` counts outputs
 * already planned in the same patch; the 1024-per-patch ceiling refuses
 * rather than truncates. `explicitSector` pins every point to one sector
 * for deliberate overlapping geometry.
 */
PreparedThingArray prepareThingArray(
    const Level& level,
    std::span<const TVector> positions,
    std::string_view templateName,
    std::string_view namePrefix,
    const TVector& orientation,
    std::int32_t layer,
    std::optional<std::int32_t> explicitSector,
    std::size_t patchOutputsBefore = 0,
    std::optional<CogSchemaResolver> schemaResolver = std::nullopt);

/**
 * Prepares one fresh Thing with the exact requested name (no generated
 * suffix). Shares the array's template, ID, name, strict-placement and
 * COG append guards and apply-time revalidation. Fresh instances have
 * empty local parameters and no ARCH binding; template values remain
 * inherited. Explicit sector has the same overlap policy as arrays.
 */
PreparedThingArray prepareThingPlacement(
    const Level& level,
    const TVector& position,
    std::string_view templateName,
    std::string_view name,
    const TVector& orientation,
    std::int32_t layer,
    std::optional<std::int32_t> explicitSector,
    std::size_t patchOutputsBefore = 0,
    std::optional<CogSchemaResolver> schemaResolver = std::nullopt);

/** Prepares one light array; see `prepareThingArray` for patch budget. */
PreparedLightArray prepareLightArray(
    const Level& level,
    std::span<const TVector> positions,
    const ColorF& color,
    double intensity,
    double range,
    std::int32_t flags,
    std::int32_t layer,
    std::size_t patchOutputsBefore = 0,
    std::optional<double> rgbIntensity = std::nullopt);

} // namespace sed::level
