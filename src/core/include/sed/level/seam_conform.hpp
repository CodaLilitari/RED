#pragma once

#include <cstddef>
#include <vector>

#include "sed/level.hpp"
#include "sed/level/sealed_seams.hpp"

namespace sed::level
{

/** Why a conform pass stopped. Only Complete changes the Level. */
enum class SeamConformStatus
{
    /** Every insertion was made; with `allowRemaining` issues may be left. */
    Complete,
    /** The inspector could not finish; nothing it found is acted on. */
    InspectionIncomplete,
    /** An issue is left that no admitted insertion candidate resolves. */
    Unconformable,
    /** More insertions were needed than `maxInsertions` allows. */
    BudgetExceeded,
    /** The core edge insertion refused a candidate. */
    InsertionFailed,
    /** An inserted candidate was reported again: the pass would not end. */
    NoProgress,
    AllocationFailure,
};

struct SeamConformOptions
{
    /** Edge vertices one call may insert. */
    std::size_t maxInsertions{ 1024 };
    /**
     * Admit candidates the inspector could not certify as lying exactly on
     * their target edge (edges that are not axis-aligned). Their vertex is
     * placed at the foot of the donor on the edge, so the donor and the new
     * corner can differ by up to the inspector's 0.002 contact distance.
     */
    bool allowApproximate{ false };
    /**
     * Make the admitted insertions and report what is left instead of
     * refusing the whole pass when some issue cannot be resolved.
     */
    bool allowRemaining{ false };
    SealedSeamLimits limits{};
};

struct SeamConformInsertion
{
    /** The surface and directed edge that received the corner. */
    SealedSeamEdge target{};
    /** The donor corner whose position the new corner takes. */
    VertexRef donor{};
    TVector point{};
    SealedSeamCandidateKind kind{ SealedSeamCandidateKind::ExactOnEdge };
    /** Distance between the donor and the inserted corner (0 when exact). */
    double deviation{};
};

struct SeamConformResult
{
    SeamConformStatus status{ SeamConformStatus::Complete };
    SealedSeamStatus inspection{ SealedSeamStatus::NotImplemented };
    /** The corners inserted; empty unless the pass completed. */
    std::vector<SeamConformInsertion> insertions{};
    /** Insertions a refused pass had made on its discarded working copy. */
    std::size_t insertionsBeforeStop{};
    /** Sealed-seam inspections the pass ran, the last one included. */
    std::size_t inspections{};
    /** Sealed-seam issues before the pass and when it stopped. */
    std::size_t issuesBefore{};
    std::size_t issuesRemaining{};
    /** Candidates that were not admitted, by reason, when it stopped. */
    std::size_t ambiguousCandidates{};
    std::size_t approximateCandidates{};
    /** The first issue that stopped the pass, when one did. */
    bool hasBlockingIssue{ false };
    SurfaceRef blockingFirst{};
    SurfaceRef blockingSecond{};

    [[nodiscard]] bool succeeded() const noexcept
    {
        return status == SeamConformStatus::Complete;
    }
};

/**
 * Resolves sealed-seam T-junctions by giving each target edge the corner its
 * neighbour already has there.
 *
 * The pass repeats until an inspection is clean: inspect with
 * inspectSealedSeams, then insert each admitted candidate, in report order,
 * whose target surface has not gained a corner since that inspection. The
 * insertion is levelCleaveEdge, which also gives the corner to the same-sector
 * faces that share the edge and to adjoined mirrors, interpolating texture
 * coordinates and baked light; junctions that exposes on other neighbours are
 * found by the next inspection. No surface is
 * split and no surface or sector is renumbered, so placed COG references keep
 * their targets. A corner the inspector certified as exactly on its edge takes
 * the donor's coordinates bit for bit; an approximate one (only with
 * `allowApproximate`) is the donor's foot on the edge, and takes the donor's
 * coordinates too when the two differ only by rounding. Candidates the
 * inspector calls ambiguous are never used.
 *
 * Failure-atomic: unless the result is Complete the Level is unchanged. A
 * Complete result leaves no sealed-seam issue, except with `allowRemaining`,
 * where `issuesRemaining` counts what the admitted candidates could not fix.
 * Like the inspector, this certifies nothing about closure, convexity or
 * engine behaviour; callers run their usual validation afterwards.
 */
[[nodiscard]] SeamConformResult levelConformSealedSeams(
    Level& level, const SeamConformOptions& options = {}) noexcept;

} // namespace sed::level
