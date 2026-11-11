#pragma once

#include <cstddef>
#include <vector>

#include "sed/level.hpp"

namespace sed::level
{

/** Completion is separate from findings: incomplete scans never mean clean. */
enum class SealedSeamStatus
{
    Complete,
    BudgetExceeded,
    InvalidGeometry,
    ArithmeticUnresolved,
    AllocationFailure,
    NotImplemented,
};

/** Advisory geometric classification only, never authorization to mutate. */
enum class SealedSeamCandidateKind
{
    ExactOnEdge,
    ApproximateOnly,
    Ambiguous,
};

struct SealedSeamEdge
{
    SurfaceRef surface{};
    std::size_t edge{}; // Corner index of the directed edge's first endpoint.
};

struct SealedSeamContact
{
    SealedSeamEdge first{};
    SealedSeamEdge second{};
};

struct SealedSeamInsertion
{
    SealedSeamEdge target{};
    VertexRef donor{};
    TVector point{}; // Original donor coordinate, never snapped/projected.
    SealedSeamCandidateKind kind{SealedSeamCandidateKind::ApproximateOnly};
};

struct SealedSeamIssue
{
    SurfaceRef first{};
    SurfaceRef second{};
    std::vector<SealedSeamContact> contacts{};
    std::vector<SealedSeamInsertion> insertions{};
};

/** Aggregate per-call limits. Count before work/allocation, without overflow. */
struct SealedSeamLimits
{
    // Sum of sector, vertex, surface and referenced-corner records admitted.
    std::size_t maxCorners{1000000};
    // Every pair of sectors is charged once for its bounds test, including
    // empty and distant sectors.
    std::size_t maxSectorPairs{64000000};
    // Surface pairs whose bounds, grown by the contact distance, meet. Pairs
    // that cannot touch are rejected by the broad phase and cost one unit of
    // maxEdgePairs instead.
    std::size_t maxSurfacePairs{1000000};
    // Geometric work units: bounds tests, plane corners, edge pairs, endpoint
    // observations and same-target candidate comparisons share this budget.
    std::size_t maxEdgePairs{16000000};
    std::size_t maxIssues{100000};
    std::size_t maxContacts{200000};
    std::size_t maxInsertions{200000};
};

struct SealedSeamReport
{
    SealedSeamStatus status{SealedSeamStatus::NotImplemented};
    std::vector<SealedSeamIssue> issues{};
};

/**
 * Read-only legacy-compatible sealed-seam inspection and insertion observations.
 *
 * Complete reports preserve the agent's sector/surface pair order and diagnostic
 * classifications on finite, index-valid input: 0.002 world-distance thresholds,
 * 0.001 dimensionless overlap threshold, and exclusion when both faces have an
 * adjoin. Contacts enumerate every positive edge pair in source traversal order.
 * Invalid/nonfinite or arithmetically unresolved input explicitly refuses rather
 * than silently presenting a complete empty report. No report certifies closure,
 * convexity, portal correctness, corner attributes, references or engine safety.
 *
 * Insertions retain original donor endpoints whose projections lie strictly
 * inside a target edge; a donor matching either endpoint under the 0.002 distance
 * rule is not an insertion. ExactOnEdge requires a proof for the represented
 * coordinates, not merely a zero rounded cross product. A conservative first
 * implementation may certify only axis-aligned edges with identical transverse
 * coordinates and strict interval containment. Other diagnostic candidates remain
 * ApproximateOnly. Distinct donor coordinates within the legacy point tolerance
 * proposing the same target edge are all Ambiguous, across all issue pairs.
 * Identical donor coordinates are not ambiguous. No transitive clustering/welding.
 *
 * Budget/refusal results may carry a diagnostic prefix; callers MUST inspect
 * status before interpreting absence or completeness. Candidate classifications
 * in incomplete reports are provisional. Work to compare competing candidates
 * must also be charged against maxEdgePairs. Surface-only synthetic input is
 * permitted because this is a diagnostic, not a topology mutation planner.
 */
[[nodiscard]] SealedSeamReport inspectSealedSeams(
    const Level& level, const SealedSeamLimits& limits = {}) noexcept;

} // namespace sed::level
