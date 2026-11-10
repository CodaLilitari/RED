#include "test_common.hpp"
#include "sed/level/sealed_seams.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sed::tests
{
namespace
{
using namespace sed::level;

// Original synthetic surface-only diagnostic fixtures, not valid rooms and
// deliberately not evidence that an insertion can safely mutate a Level.
void appendSeamFace(Sector& sector, std::initializer_list<TVector> points)
{
    Surface face;
    for ( const auto& point : points )
    {
        face.vertices.push_back(static_cast<std::int32_t>(sector.vertices.size()));
        sector.vertices.push_back(Vertex{point.x, point.y, point.z, 0});
        face.texVertices.push_back(TexVertex{});
    }
    sector.surfaces.push_back(std::move(face));
}

Level seamFixture()
{
    Level level;
    level.sectors.resize(2);
    appendSeamFace(level.sectors[0], {{0,0,0}, {2,0,0}, {2,2,0}, {0,2,0}});
    appendSeamFace(level.sectors[1], {{0,0,0}, {0,1,0}, {2,1,0}, {2,0,0}});
    appendSeamFace(level.sectors[1], {{0,1,0}, {0,2,0}, {2,2,0}, {2,1,0}});
    return level;
}

bool completeWithIssues(const SealedSeamReport& report, const std::size_t count)
{
    return report.status == SealedSeamStatus::Complete && report.issues.size() == count;
}

bool hasInsertion(const SealedSeamReport& report, const std::size_t edge,
    const double x, const double y, const SealedSeamCandidateKind kind)
{
    for ( const auto& issue : report.issues )
    {
        for ( const auto& candidate : issue.insertions )
        {
            if ( candidate.target.surface == SurfaceRef{0, 0} && candidate.target.edge == edge
                && candidate.point.x == x && candidate.point.y == y && candidate.kind == kind )
            {
                return true;
            }
        }
    }
    return false;
}
}

void runSealedSeamTests()
{
    // Catches dropped legacy positive findings, unstable surface-pair order,
    // missing exact edge addresses, and wrongly claiming mutation eligibility.
    const Level baseline = seamFixture();
    const auto report = inspectSealedSeams(baseline);
    check(completeWithIssues(report, 2), "sealed seams: two split-wall pair findings");
    if ( report.issues.size() == 2 )
    {
        check(report.issues[0].first == SurfaceRef{0,0}
            && report.issues[0].second == SurfaceRef{1,0}
            && report.issues[1].second == SurfaceRef{1,1},
            "sealed seams: preserve legacy sector/surface pair order");
        check(report.issues[0].contacts.size() == 2
            && report.issues[0].contacts[0].first.edge == 1
            && report.issues[0].contacts[0].second.edge == 2
            && report.issues[0].contacts[1].first.edge == 3
            && report.issues[0].contacts[1].second.edge == 0,
            "sealed seams: report both overlapping edge pairs in traversal order");
    }
    check(hasInsertion(report, 1, 2, 1, SealedSeamCandidateKind::ExactOnEdge)
        && hasInsertion(report, 3, 0, 1, SealedSeamCandidateKind::ExactOnEdge),
        "sealed seams: name exact interior neighbour points without snapping");

    // Matching sealed walls and noncoplanar partial edges are legacy negatives.
    Level matching;
    matching.sectors.resize(2);
    appendSeamFace(matching.sectors[0], {{0,0,0}, {2,0,0}, {2,2,0}, {0,2,0}});
    appendSeamFace(matching.sectors[1], {{0,0,0}, {0,2,0}, {2,2,0}, {2,0,0}});
    check(completeWithIssues(inspectSealedSeams(matching), 0),
        "sealed seams: matching reversed boundaries are not seams");
    Level angled = baseline;
    for ( auto& vertex : angled.sectors[1].vertices ) vertex.z = vertex.y;
    check(completeWithIssues(inspectSealedSeams(angled), 0),
        "sealed seams: angled partial edge contact is not a coplanar seam");

    // Same geometry at binary-exact scales and translations must remain found.
    for ( const double scale : {0.5, 8.0} )
    {
        Level transformed = baseline;
        for ( auto& sector : transformed.sectors )
        {
            for ( auto& vertex : sector.vertices )
            {
                vertex.x = 1024.0 + scale * vertex.x;
                vertex.y = -512.0 + scale * vertex.y;
                vertex.z = 64.0;
            }
        }
        check(completeWithIssues(inspectSealedSeams(transformed), 2),
            "sealed seams: preserve ordinary translated/scaled positives");
    }
    Level swapped = baseline;
    std::swap(swapped.sectors[0], swapped.sectors[1]);
    check(completeWithIssues(inspectSealedSeams(swapped), 2),
        "sealed seams: swapped split/full wall retains two findings");

    // Diagnostic proximity is not an exact insertion certificate.
    Level approximate = baseline;
    for ( auto& vertex : approximate.sectors[1].vertices ) vertex.z = 0.001;
    const auto approximateReport = inspectSealedSeams(approximate);
    check(completeWithIssues(approximateReport, 2)
        && hasInsertion(approximateReport, 1, 2, 1, SealedSeamCandidateKind::ApproximateOnly),
        "sealed seams: off-edge diagnostic points are approximate-only");
    Level atBoundary = baseline;
    for ( auto& vertex : atBoundary.sectors[1].vertices ) vertex.z = 0.002;
    check(completeWithIssues(inspectSealedSeams(atBoundary), 0),
        "sealed seams: legacy plane-distance boundary remains exclusive");
    Level beyondBoundary = baseline;
    for ( auto& vertex : beyondBoundary.sectors[1].vertices ) vertex.z = 0.004;
    check(completeWithIssues(inspectSealedSeams(beyondBoundary), 0),
        "sealed seams: separated planes do not become repairs");

    // Conflicting nearby donor points may not silently collapse to one point.
    Level ambiguous = baseline;
    ambiguous.sectors.push_back(baseline.sectors[1]);
    for ( auto& vertex : ambiguous.sectors[2].vertices ) vertex.z = 0.001;
    const auto ambiguousReport = inspectSealedSeams(ambiguous);
    check(ambiguousReport.status == SealedSeamStatus::Complete
        && hasInsertion(ambiguousReport, 1, 2, 1, SealedSeamCandidateKind::Ambiguous),
        "sealed seams: competing nearby donor coordinates are ambiguous");

    // Preserve the legacy both-adjoined filter, without interpreting it as a
    // claim that these deliberately dangling fixture links are valid portals.
    Level linked = baseline;
    linked.sectors[0].surfaces[0].adjoin = SurfaceRef{1,0};
    for ( auto& surface : linked.sectors[1].surfaces ) surface.adjoin = SurfaceRef{0,0};
    check(completeWithIssues(inspectSealedSeams(linked), 0),
        "sealed seams: pairs where both faces have adjoins are excluded");
    linked.sectors[0].surfaces[0].adjoin.reset();
    check(completeWithIssues(inspectSealedSeams(linked), 2),
        "sealed seams: one-adjoined pairs retain legacy diagnostics");

    // Budget exhaustion must be visible even when no finding has been stored.
    SealedSeamLimits limits;
    limits.maxSurfacePairs = 0;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: pair budget cannot return clean completion");
    limits = {};
    limits.maxEdgePairs = 0;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: edge budget cannot return clean completion");
    limits = {};
    limits.maxIssues = 1;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: issue truncation is explicit incomplete status");
    limits.maxIssues = 2;
    check(completeWithIssues(inspectSealedSeams(baseline, limits), 2),
        "sealed seams: exact issue-budget capacity completes");
    limits = {};
    limits.maxCorners = 3;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: geometry admission budget bounds corner work");
    limits = {};
    limits.maxContacts = 0;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: contact output budget cannot silently truncate");
    limits = {};
    limits.maxInsertions = 0;
    check(inspectSealedSeams(baseline, limits).status == SealedSeamStatus::BudgetExceeded,
        "sealed seams: insertion output budget cannot silently truncate");

    Level malformed = baseline;
    malformed.sectors[0].surfaces[0].vertices[1] = -1;
    check(inspectSealedSeams(malformed).status == SealedSeamStatus::InvalidGeometry,
        "sealed seams: invalid indices are an explicit refusal, not clean output");
    malformed = baseline;
    malformed.sectors[0].vertices[0].x = std::numeric_limits<double>::infinity();
    check(inspectSealedSeams(malformed).status == SealedSeamStatus::InvalidGeometry,
        "sealed seams: nonfinite input is an explicit refusal");
    Level overflowing = baseline;
    for ( auto& sector : overflowing.sectors )
    {
        for ( auto& vertex : sector.vertices )
        {
            vertex.x *= 1.0e200;
            vertex.y *= 1.0e200;
        }
    }
    check(inspectSealedSeams(overflowing).status == SealedSeamStatus::ArithmeticUnresolved,
        "sealed seams: finite coordinates with overflowing legacy products refuse");
    Level endpoint;
    endpoint.sectors.resize(2);
    appendSeamFace(endpoint.sectors[0], {{0,0,0}, {2,0,0}, {2,2,0}, {0,2,0}});
    appendSeamFace(endpoint.sectors[1], {{2,2,0}, {4,2,0}, {4,4,0}, {2,4,0}});
    check(completeWithIssues(inspectSealedSeams(endpoint), 0),
        "sealed seams: endpoint-only contact is not positive overlap");
    check(completeWithIssues(inspectSealedSeams(Level{}), 0),
        "sealed seams: empty geometry completes without findings");
}
} // namespace sed::tests
