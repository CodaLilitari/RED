#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sed/geometry.hpp"
#include "sed/level.hpp"

namespace sed::level
{

/** Default plane/point distance tolerance for spatial selection. */
inline constexpr double kSpatialDistanceTolerance = 1.0e-7;

/** Default outward-direction tolerance for surface selection, in degrees. */
inline constexpr double kSpatialAngularToleranceDegrees = 0.1;

/** Stable code reported when a token no longer matches its document. */
inline constexpr std::string_view kStaleSelectionTokenCode = "staleSelectionToken";

/** Stable code reported when a token string cannot be read at all. */
inline constexpr std::string_view kMalformedSelectionTokenCode = "malformedSelectionToken";

/** Stable code reported when geometry cannot be classified safely. */
inline constexpr std::string_view kCannotClassifySpatialCode = "cannotClassifySpatialSelection";

/** Query tolerances; the defaults are the contract values above. */
struct SpatialTolerances
{
    double distance{ kSpatialDistanceTolerance };
    double angularDegrees{ kSpatialAngularToleranceDegrees };
};

/** Which level entity a spatial selection addresses. */
enum class SelectionTargetKind
{
    sector,
    surface,
    vertex,
    thing,
};

std::string_view selectionTargetKindName(SelectionTargetKind kind) noexcept;

/** Resolved level address of one selected entity (indices, not numbers). */
struct SelectionTarget
{
    SelectionTargetKind kind{ SelectionTargetKind::sector };
    std::int32_t sector{ -1 };
    std::int32_t surface{ -1 };
    std::int32_t vertex{ -1 };
    std::int32_t thing{ -1 };

    constexpr bool operator==(const SelectionTarget&) const noexcept = default;
};

/**
 * Document identity a token is bound to. The agent supplies the file hash
 * for both fields; in-memory callers supply their document revision.
 */
struct SelectionContext
{
    std::string inputSha256{};
    std::string sourceRevision{};
};

/** Opaque selection token; callers store and replay the text verbatim. */
struct SelectionToken
{
    std::string text{};

    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
};

/** Outcome of one spatial query. */
enum class SpatialMatch
{
    unique,
    none,
    ambiguous,
    cannotClassify,
};

std::string_view spatialMatchName(SpatialMatch match) noexcept;

/**
 * Result of one spatial query. Only `unique` carries a token; `none` and
 * `ambiguous` are successful read-only results with no token, never a
 * guessed nearest match.
 */
struct SpatialSelection
{
    SpatialMatch match{ SpatialMatch::none };
    SelectionTarget target{};
    std::optional<SelectionToken> token{};
    std::size_t candidateCount{};
    std::string code{};
    std::string diagnostic{};
};

/** Finds the sector whose closed volume contains an interior world point. */
SpatialSelection selectSectorAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances = {});

/** Finds the surface on `point` facing `outwardDirection`. */
SpatialSelection selectSurfaceByPointAndDirection(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const TVector& outwardDirection,
    const SpatialTolerances& tolerances = {});

/** Finds the vertex coincident with a world point. */
SpatialSelection selectVertexAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances = {});

/** Finds the thing positioned at a world point. */
SpatialSelection selectThingAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances = {});

/** Whether a replayed token still matches the document it was minted from. */
enum class SelectionTokenStatus
{
    valid,
    stale,
    malformed,
};

std::string_view selectionTokenStatusName(SelectionTokenStatus status) noexcept;

/** Outcome of replaying one token against a (possibly changed) document. */
struct SelectionResolution
{
    SelectionTokenStatus status{ SelectionTokenStatus::malformed };
    SelectionTarget target{};
    std::string code{};
};

/**
 * Replays a token against the current document and bindings. A geometry or
 * revision change reports `stale` with `kStaleSelectionTokenCode`; an
 * unreadable token reports `malformed`. Predicates are never rerun: the
 * stored address is returned only when its bound geometry still matches.
 */
SelectionResolution resolveSelectionToken(
    const Level& level,
    const SelectionContext& context,
    std::string_view tokenText);

} // namespace sed::level
