#include "sed/level/seam_conform.hpp"

#include "sed/level/cleave.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace sed::level
{

namespace
{

// An inserted corner is recognised again by its target surface and the
// donor's exact coordinates; the edge index is not part of the key because
// earlier insertions on the same surface shift it.
using AppliedKey = std::tuple<std::int32_t, std::int32_t, std::uint64_t, std::uint64_t, std::uint64_t>;

AppliedKey appliedKey(const SealedSeamInsertion& insertion) noexcept
{
    return { insertion.target.surface.sector, insertion.target.surface.surface,
        std::bit_cast<std::uint64_t>(insertion.point.x), std::bit_cast<std::uint64_t>(insertion.point.y),
        std::bit_cast<std::uint64_t>(insertion.point.z) };
}

bool sameBits(const Vertex& vertex, const TVector& point) noexcept
{
    return std::bit_cast<std::uint64_t>(vertex.x) == std::bit_cast<std::uint64_t>(point.x)
        && std::bit_cast<std::uint64_t>(vertex.y) == std::bit_cast<std::uint64_t>(point.y)
        && std::bit_cast<std::uint64_t>(vertex.z) == std::bit_cast<std::uint64_t>(point.z);
}

// The edge cleave computes the corner as a plane and line intersection, which
// can round a unit in the last place away from the donor even on an
// axis-aligned edge. Give every vertex with exactly those computed coordinates
// (the target sector's and its adjoined mirrors') the donor's.
void snapToDonor(Level& level, const TVector& computed, const TVector& donor) noexcept
{
    for ( Sector& sector : level.sectors )
    {
        for ( Vertex& vertex : sector.vertices )
        {
            if ( sameBits(vertex, computed) )
            {
                vertex.x = donor.x;
                vertex.y = donor.y;
                vertex.z = donor.z;
            }
        }
    }
}

// Corner counts of every surface at the start of one inspection, with the
// surfaces an insertion has since changed marked. A candidate names its edge
// by corner index, so it is only still addressed correctly while its target
// surface has not gained a corner since the inspection that produced it.
class PassSurfaces
{
public:
    explicit PassSurfaces(const Level& level)
    {
        counts_.reserve(level.sectors.size());
        for ( const Sector& sector : level.sectors )
        {
            std::vector<std::size_t>& row = counts_.emplace_back();
            row.reserve(sector.surfaces.size());
            for ( const Surface& surface : sector.surfaces )
            {
                row.push_back(surface.vertices.size());
            }
        }
    }

    [[nodiscard]] bool changed(const SurfaceRef ref) const noexcept
    {
        return counts_[static_cast<std::size_t>(ref.sector)][static_cast<std::size_t>(ref.surface)] == kChanged;
    }

    // Marks every surface whose corner count differs from the inspected one.
    // False when a sector or surface was added or removed, which an edge
    // insertion never does.
    [[nodiscard]] bool update(const Level& level) noexcept
    {
        if ( level.sectors.size() != counts_.size() )
        {
            return false;
        }
        for ( std::size_t s = 0; s < counts_.size(); ++s )
        {
            const Sector& sector = level.sectors[s];
            std::vector<std::size_t>& row = counts_[s];
            if ( sector.surfaces.size() != row.size() )
            {
                return false;
            }
            for ( std::size_t f = 0; f < row.size(); ++f )
            {
                if ( row[f] != kChanged && sector.surfaces[f].vertices.size() != row[f] )
                {
                    row[f] = kChanged;
                }
            }
        }
        return true;
    }

private:
    static constexpr std::size_t kChanged = std::numeric_limits<std::size_t>::max();
    std::vector<std::vector<std::size_t>> counts_{};
};

} // namespace

SeamConformResult levelConformSealedSeams(Level& level, const SeamConformOptions& options) noexcept
{
    SeamConformResult result;
    try
    {
        Level candidate = level;
        std::set<AppliedKey> applied;
        bool firstInspection = true;
        // Every refusal discards the candidate, so its insertions are only
        // counted, never listed as made.
        const auto stop = [&result](const SeamConformStatus status)
        {
            result.status = status;
            result.insertionsBeforeStop = result.insertions.size();
            result.insertions.clear();
            return result;
        };
        const auto refuseInsertion = [&result, &stop](const SealedSeamInsertion& insertion,
            const SeamConformStatus status)
        {
            result.hasBlockingIssue = true;
            result.blockingFirst = insertion.target.surface;
            result.blockingSecond = SurfaceRef{ insertion.donor.sector, -1 };
            return stop(status);
        };
        for ( ;; )
        {
            const SealedSeamReport report = inspectSealedSeams(candidate, options.limits);
            result.inspection = report.status;
            ++result.inspections;
            if ( report.status != SealedSeamStatus::Complete )
            {
                return stop(report.status == SealedSeamStatus::AllocationFailure
                    ? SeamConformStatus::AllocationFailure : SeamConformStatus::InspectionIncomplete);
            }
            if ( firstInspection )
            {
                result.issuesBefore = report.issues.size();
                firstInspection = false;
            }
            result.issuesRemaining = report.issues.size();

            // Admitted candidates are inserted in report order; the first
            // issue without one is what a strict pass stops on.
            std::vector<const SealedSeamInsertion*> admitted;
            const SealedSeamIssue* blocking = nullptr;
            result.ambiguousCandidates = 0;
            result.approximateCandidates = 0;
            for ( const SealedSeamIssue& issue : report.issues )
            {
                bool any = false;
                for ( const SealedSeamInsertion& insertion : issue.insertions )
                {
                    if ( insertion.kind == SealedSeamCandidateKind::Ambiguous )
                    {
                        ++result.ambiguousCandidates;
                        continue;
                    }
                    if ( insertion.kind == SealedSeamCandidateKind::ApproximateOnly && !options.allowApproximate )
                    {
                        ++result.approximateCandidates;
                        continue;
                    }
                    any = true;
                    admitted.push_back(&insertion);
                }
                if ( !any && blocking == nullptr )
                {
                    blocking = &issue;
                }
            }
            result.hasBlockingIssue = blocking != nullptr;
            if ( blocking != nullptr )
            {
                result.blockingFirst = blocking->first;
                result.blockingSecond = blocking->second;
            }
            if ( report.issues.empty() )
            {
                break;
            }
            if ( blocking != nullptr && !options.allowRemaining )
            {
                return stop(SeamConformStatus::Unconformable);
            }
            if ( admitted.empty() )
            {
                // allowRemaining: nothing admitted is left to insert.
                break;
            }

            // One inspection serves every candidate whose target surface is
            // still as inspected. The rest wait for the next inspection, which
            // also finds the junctions these insertions expose on neighbours.
            PassSurfaces surfaces(candidate);
            for ( const SealedSeamInsertion* const chosen : admitted )
            {
                // A complete report only names index-valid surfaces and edges.
                const SurfaceRef target = chosen->target.surface;
                if ( surfaces.changed(target) )
                {
                    continue;
                }
                if ( result.insertions.size() >= options.maxInsertions )
                {
                    return stop(SeamConformStatus::BudgetExceeded);
                }
                if ( !applied.insert(appliedKey(*chosen)).second )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::NoProgress);
                }

                const std::size_t edge = chosen->target.edge;
                const TVector donor = chosen->point;
                const Sector& sector = candidate.sectors[static_cast<std::size_t>(target.sector)];
                const Surface& surface = sector.surfaces[static_cast<std::size_t>(target.surface)];
                const std::size_t corners = surface.vertices.size();
                if ( edge >= corners || corners < 3 )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                const Vertex& from = sector.vertices[static_cast<std::size_t>(surface.vertices[edge])];
                const Vertex& to = sector.vertices[static_cast<std::size_t>(surface.vertices[(edge + 1) % corners])];
                const TVector direction{ to.x - from.x, to.y - from.y, to.z - from.z };
                const double length = std::sqrt(direction.x * direction.x + direction.y * direction.y
                    + direction.z * direction.z);
                if ( !(length > 0.0) || !std::isfinite(length) )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                // The plane through the donor across the edge: for an
                // axis-aligned edge its normal is exactly a unit axis.
                const TVector normal{ direction.x / length, direction.y / length, direction.z / length };
                if ( !levelCleaveEdge(candidate, target, static_cast<std::int32_t>(edge), normal,
                         donor.x, donor.y, donor.z) )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                if ( !surfaces.update(candidate) )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                Sector& changedSector = candidate.sectors[static_cast<std::size_t>(target.sector)];
                const Surface& changed = changedSector.surfaces[static_cast<std::size_t>(target.surface)];
                if ( changed.vertices.size() != corners + 1 )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                const std::int32_t insertedIndex = changed.vertices[edge + 1];
                if ( insertedIndex < 0 || static_cast<std::size_t>(insertedIndex) >= changedSector.vertices.size() )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                const Vertex computedVertex = changedSector.vertices[static_cast<std::size_t>(insertedIndex)];
                const TVector computed{ computedVertex.x, computedVertex.y, computedVertex.z };
                double deviation = std::sqrt((computed.x - donor.x) * (computed.x - donor.x)
                    + (computed.y - donor.y) * (computed.y - donor.y)
                    + (computed.z - donor.z) * (computed.z - donor.z));
                // Rounding-sized differences are removed so both sides of the
                // seam hold one coordinate. A certified candidate that lands
                // further away than rounding explains is not the insertion
                // that was certified, and is refused rather than reported as
                // exact.
                const double scale = std::max({ 1.0, std::abs(donor.x), std::abs(donor.y), std::abs(donor.z) });
                if ( deviation > 0.0 && deviation <= 1e-9 * scale )
                {
                    snapToDonor(candidate, computed, donor);
                    deviation = 0.0;
                }
                else if ( chosen->kind == SealedSeamCandidateKind::ExactOnEdge && deviation != 0.0 )
                {
                    return refuseInsertion(*chosen, SeamConformStatus::InsertionFailed);
                }
                result.insertions.push_back(SeamConformInsertion{
                    chosen->target, chosen->donor, donor, chosen->kind, deviation });
            }
        }
        if ( !result.insertions.empty() )
        {
            level = std::move(candidate);
        }
        result.status = SeamConformStatus::Complete;
        return result;
    }
    catch ( const std::bad_alloc& )
    {
        result.status = SeamConformStatus::AllocationFailure;
    }
    catch ( ... )
    {
        result.status = SeamConformStatus::InsertionFailed;
    }
    result.insertionsBeforeStop = result.insertions.size();
    result.insertions.clear();
    return result;
}

} // namespace sed::level
