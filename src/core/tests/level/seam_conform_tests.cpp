#include "test_common.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/sealed_seams.hpp"
#include "sed/level/seam_conform.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

namespace sed::tests
{
namespace
{
using namespace sed::level;

// Real closed rooms, built the way the agent's addBox builds them, so the
// pass is exercised through the same edge insertion a level build uses.
std::int32_t room(Level& level, const TVector& low, const TVector& high)
{
    return levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, low, high);
}

// A room with a larger wall beside a smaller room: the classic T-junction.
// The big room's floor, ceiling and east wall each have an edge the small
// room ends in the middle of.
Level teeFixture()
{
    Level level;
    room(level, {0, 0, 0}, {2, 2, 1});
    room(level, {2, 0, 0}, {3, 1, 1});
    return level;
}

std::uint64_t bits(const double value)
{
    return std::bit_cast<std::uint64_t>(value);
}

bool sameLevelGeometry(const Level& a, const Level& b)
{
    if ( a.sectors.size() != b.sectors.size() ) return false;
    for ( std::size_t s = 0; s < a.sectors.size(); ++s )
    {
        const Sector& as = a.sectors[s];
        const Sector& bs = b.sectors[s];
        if ( as.vertices.size() != bs.vertices.size() || as.surfaces.size() != bs.surfaces.size() ) return false;
        for ( std::size_t v = 0; v < as.vertices.size(); ++v )
        {
            if ( bits(as.vertices[v].x) != bits(bs.vertices[v].x) || bits(as.vertices[v].y) != bits(bs.vertices[v].y)
                || bits(as.vertices[v].z) != bits(bs.vertices[v].z) ) return false;
        }
        for ( std::size_t f = 0; f < as.surfaces.size(); ++f )
        {
            if ( as.surfaces[f].vertices != bs.surfaces[f].vertices ) return false;
            if ( as.surfaces[f].adjoin != bs.surfaces[f].adjoin ) return false;
        }
    }
    return true;
}

std::size_t surfaceCount(const Level& level)
{
    std::size_t count = 0;
    for ( const Sector& sector : level.sectors ) count += sector.surfaces.size();
    return count;
}

// The surface of `sector` whose corners all satisfy `inPlane`, or -1.
template <typename Predicate>
std::int32_t surfaceWhere(const Level& level, const std::int32_t sector, Predicate inPlane)
{
    const Sector& owner = level.sectors[static_cast<std::size_t>(sector)];
    for ( std::size_t f = 0; f < owner.surfaces.size(); ++f )
    {
        bool all = !owner.surfaces[f].vertices.empty();
        for ( const std::int32_t index : owner.surfaces[f].vertices )
        {
            all = all && inPlane(owner.vertices[static_cast<std::size_t>(index)]);
        }
        if ( all ) return static_cast<std::int32_t>(f);
    }
    return -1;
}

bool hasCornerBits(const Level& level, const SurfaceRef ref, const double x, const double y, const double z)
{
    const Sector& owner = level.sectors[static_cast<std::size_t>(ref.sector)];
    for ( const std::int32_t index : owner.surfaces[static_cast<std::size_t>(ref.surface)].vertices )
    {
        const Vertex& vertex = owner.vertices[static_cast<std::size_t>(index)];
        if ( bits(vertex.x) == bits(x) && bits(vertex.y) == bits(y) && bits(vertex.z) == bits(z) ) return true;
    }
    return false;
}

std::size_t corners(const Level& level, const SurfaceRef ref)
{
    return level.sectors[static_cast<std::size_t>(ref.sector)]
        .surfaces[static_cast<std::size_t>(ref.surface)].vertices.size();
}

std::size_t issueCount(const Level& level)
{
    const SealedSeamReport report = inspectSealedSeams(level);
    return report.status == SealedSeamStatus::Complete ? report.issues.size() : static_cast<std::size_t>(-1);
}

void rotateAboutZ(Level& level, const double radians)
{
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    for ( Sector& sector : level.sectors )
    {
        for ( Vertex& vertex : sector.vertices )
        {
            const double x = vertex.x * c - vertex.y * s;
            const double y = vertex.x * s + vertex.y * c;
            vertex.x = x;
            vertex.y = y;
        }
    }
}
}

void runSeamConformTests()
{
    // The fixture must be the defect the pass exists for, or every later
    // check would pass on a level that never needed conforming.
    const Level tee = teeFixture();
    check(tee.sectors.size() == 2, "seam conform: fixture builds two rooms");
    if ( tee.sectors.size() != 2 ) return;
    const std::int32_t floorSurface = surfaceWhere(tee, 0, [](const Vertex& v) { return v.z == 0.0; });
    const std::int32_t ceilingSurface = surfaceWhere(tee, 0, [](const Vertex& v) { return v.z == 1.0; });
    const std::int32_t eastSurface = surfaceWhere(tee, 0, [](const Vertex& v) { return v.x == 2.0; });
    check(floorSurface >= 0 && ceilingSurface >= 0 && eastSurface >= 0,
        "seam conform: fixture room has a floor, a ceiling and an east wall");
    if ( floorSurface < 0 || ceilingSurface < 0 || eastSurface < 0 ) return;
    const SurfaceRef floorRef{ 0, floorSurface };
    const SurfaceRef ceilingRef{ 0, ceilingSurface };
    const SurfaceRef eastRef{ 0, eastSurface };
    check(issueCount(tee) == 3, "seam conform: fixture has floor, ceiling and wall T-junctions");

    // Catches a pass that reports success without inserting, inserts beside
    // the donor instead of on it, or leaves the wall that shares the edge
    // without the corner its floor received.
    {
        Level level = tee;
        const SeamConformResult result = levelConformSealedSeams(level);
        check(result.succeeded() && result.inspection == SealedSeamStatus::Complete,
            "seam conform: an axis-aligned T-junction conforms");
        check(result.issuesBefore == 3 && result.issuesRemaining == 0 && !result.hasBlockingIssue,
            "seam conform: counts the issues it started with and leaves none");
        check(result.insertions.size() == 2 && result.insertionsBeforeStop == 0,
            "seam conform: one corner per shared edge, the wall follows its floor and ceiling");
        check(issueCount(level) == 0, "seam conform: a fresh inspection of the result is clean");
        check(level.sectors.size() == tee.sectors.size() && surfaceCount(level) == surfaceCount(tee),
            "seam conform: no sector or surface is added, removed or renumbered");
        check(corners(level, floorRef) == 5 && corners(level, ceilingRef) == 5 && corners(level, eastRef) == 6,
            "seam conform: floor and ceiling gain one corner, the wall between them two");
        check(hasCornerBits(level, floorRef, 2, 1, 0) && hasCornerBits(level, eastRef, 2, 1, 0)
            && hasCornerBits(level, ceilingRef, 2, 1, 1) && hasCornerBits(level, eastRef, 2, 1, 1),
            "seam conform: inserted corners carry the donor's coordinates bit for bit");
        bool exact = !result.insertions.empty();
        for ( const SeamConformInsertion& insertion : result.insertions )
        {
            exact = exact && insertion.kind == SealedSeamCandidateKind::ExactOnEdge && insertion.deviation == 0.0
                && insertion.target.surface.sector == 0 && insertion.donor.sector == 1
                && insertion.point.x == 2.0 && insertion.point.y == 1.0;
        }
        check(exact, "seam conform: reports each insertion's target, donor and exactness");
        Level donorOnly;
        donorOnly.sectors.push_back(level.sectors[1]);
        Level donorBefore;
        donorBefore.sectors.push_back(tee.sectors[1]);
        check(sameLevelGeometry(donorOnly, donorBefore), "seam conform: the donor room is not edited");
    }

    // The edge cleave interpolates the corner along the edge, which for these
    // coordinates rounds one unit in the last place away from the donor (about
    // one edge in six hundred does). The corner must still be the donor's
    // coordinate exactly, or the two rooms keep a hairline between them and
    // the insertion is not the one the inspector certified.
    {
        const double south = 0.4174640401007821;
        const double north = 3.787678980736944;
        const double donorY = 1.7407108115181071;
        Level level;
        room(level, {0, south, 0}, {2, north, 1});
        room(level, {2, south, 0}, {3, donorY, 1});
        const Level before = level;
        const SeamConformResult result = levelConformSealedSeams(level);
        check(result.succeeded() && result.insertions.size() == 2 && issueCount(level) == 0,
            "seam conform: off-grid axis-aligned rooms conform");
        bool donorExact = result.insertions.size() == 2;
        for ( const SeamConformInsertion& insertion : result.insertions )
        {
            donorExact = donorExact && insertion.deviation == 0.0 && bits(insertion.point.y) == bits(donorY)
                && hasCornerBits(level, insertion.target.surface, insertion.point.x, insertion.point.y,
                    insertion.point.z);
        }
        check(donorExact, "seam conform: off-grid corners are the donor's bits, not a rounded neighbour");
        const std::int32_t wall = surfaceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0; });
        check(wall >= 0 && hasCornerBits(level, SurfaceRef{ 0, wall }, 2, donorY, 0)
            && hasCornerBits(level, SurfaceRef{ 0, wall }, 2, donorY, 1),
            "seam conform: the wall sharing the edge holds the donor's bits too");
        check(level.sectors[1].vertices.size() == before.sectors[1].vertices.size(),
            "seam conform: off-grid donor room is not edited");
    }

    // One inspection has to serve every surface it reported, or a level with
    // hundreds of junctions costs hundreds of whole-level inspections. Four
    // separate junction pairs need eight corners and the same two inspections
    // (one to find them, one to confirm) as a single pair.
    {
        Level level;
        for ( int pair = 0; pair < 4; ++pair )
        {
            const double x = 10.0 * pair;
            room(level, {x, 0, 0}, {x + 2, 2, 1});
            room(level, {x + 2, 0, 0}, {x + 3, 1, 1});
        }
        const SeamConformResult result = levelConformSealedSeams(level);
        check(result.succeeded() && result.insertions.size() == 8 && result.issuesBefore == 12
            && issueCount(level) == 0, "seam conform: independent junctions all conform");
        check(result.inspections == 2, "seam conform: one inspection serves every surface it reported");
        Level single = tee;
        check(levelConformSealedSeams(single).inspections == 2,
            "seam conform: a single junction pair is found once and confirmed once");
    }

    // Several rooms ending along one edge: each corner shifts the edge indices
    // of the surface it lands on, so later candidates for that surface must be
    // taken from a fresh inspection, not inserted at a stale index.
    {
        Level level;
        room(level, {0, 0, 0}, {2, 4, 1});
        room(level, {2, 0, 0}, {3, 1, 1});
        room(level, {2, 1, 0}, {3, 2, 1});
        room(level, {2, 2, 0}, {3, 3, 1});
        room(level, {2, 3, 0}, {3, 4, 1});
        const SeamConformResult result = levelConformSealedSeams(level);
        const std::int32_t longFloor = surfaceWhere(level, 0, [](const Vertex& v) { return v.z == 0.0; });
        const std::int32_t longWall = surfaceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0; });
        check(result.succeeded() && result.insertions.size() == 6 && issueCount(level) == 0,
            "seam conform: three corners per shared edge conform");
        check(longFloor >= 0 && longWall >= 0 && corners(level, SurfaceRef{ 0, longFloor }) == 7
            && corners(level, SurfaceRef{ 0, longWall }) == 10,
            "seam conform: every corner lands once on the floor and the wall");
        bool ordered = longFloor >= 0;
        for ( const double y : { 1.0, 2.0, 3.0 } )
        {
            ordered = ordered && hasCornerBits(level, SurfaceRef{ 0, longFloor }, 2, y, 0)
                && hasCornerBits(level, SurfaceRef{ 0, longWall }, 2, y, 1);
        }
        check(ordered, "seam conform: stale edge indices do not misplace later corners");
    }

    // A level without the defect must come back untouched, not rewritten.
    {
        Level level;
        room(level, {0, 0, 0}, {2, 2, 1});
        room(level, {2, 0, 0}, {3, 2, 1});
        const Level before = level;
        const SeamConformResult result = levelConformSealedSeams(level);
        check(result.succeeded() && result.insertions.empty() && result.issuesBefore == 0
            && result.issuesRemaining == 0, "seam conform: matching rooms need nothing");
        check(sameLevelGeometry(level, before), "seam conform: a clean level is left exactly as it was");
    }

    // The budget is a refusal, not a partial edit: a caller that asked for at
    // most one insertion must not receive a level with one of two made.
    {
        Level level = tee;
        SeamConformOptions options;
        options.maxInsertions = 1;
        const SeamConformResult result = levelConformSealedSeams(level, options);
        check(result.status == SeamConformStatus::BudgetExceeded && !result.succeeded(),
            "seam conform: needing more insertions than allowed is refused");
        check(result.insertions.empty() && result.insertionsBeforeStop == 1 && result.issuesRemaining > 0,
            "seam conform: a refused pass lists no insertion as made");
        check(sameLevelGeometry(level, tee), "seam conform: a budget refusal leaves the level unchanged");
        options.maxInsertions = 2;
        check(levelConformSealedSeams(level, options).succeeded() && issueCount(level) == 0,
            "seam conform: the exact budget completes");
    }

    // An inspection that did not finish proves nothing, so nothing is done.
    {
        Level level = tee;
        SeamConformOptions options;
        options.limits.maxCorners = 3;
        const SeamConformResult result = levelConformSealedSeams(level, options);
        check(result.status == SeamConformStatus::InspectionIncomplete
            && result.inspection == SealedSeamStatus::BudgetExceeded && result.insertions.empty(),
            "seam conform: an incomplete inspection is reported, not treated as clean");
        check(sameLevelGeometry(level, tee), "seam conform: an incomplete inspection changes nothing");
    }

    // Rotated rooms have no certificate that the donor lies on the edge. The
    // default must refuse them whole; the opt-in must conform them.
    {
        Level rotated = tee;
        rotateAboutZ(rotated, 0.5);
        const Level before = rotated;
        const SeamConformResult strict = levelConformSealedSeams(rotated);
        check(strict.status == SeamConformStatus::Unconformable && strict.hasBlockingIssue
            && strict.approximateCandidates > 0 && strict.insertions.empty() && strict.issuesRemaining == 3,
            "seam conform: uncertified candidates are refused by default");
        check(sameLevelGeometry(rotated, before), "seam conform: an unconformable level is unchanged");

        SeamConformOptions options;
        options.allowApproximate = true;
        const SeamConformResult relaxed = levelConformSealedSeams(rotated, options);
        check(relaxed.succeeded() && relaxed.insertions.size() == 2 && issueCount(rotated) == 0,
            "seam conform: allowApproximate conforms rotated rooms");
        bool reported = relaxed.insertions.size() == 2;
        for ( const SeamConformInsertion& insertion : relaxed.insertions )
        {
            reported = reported && insertion.kind == SealedSeamCandidateKind::ApproximateOnly
                && insertion.deviation >= 0.0 && insertion.deviation < 0.002;
        }
        check(reported, "seam conform: approximate insertions report their kind and deviation");
    }

    // Two neighbours ending within the point tolerance of each other on one
    // edge: picking either would silently weld them. Never inserted; with
    // allowRemaining the pass still says what it left.
    {
        Level level;
        room(level, {0, 0, 0}, {2, 2, 1});
        room(level, {2, 0, 0}, {3, 1, 1});
        room(level, {2, 1.001, 0}, {3, 2, 1});
        const Level before = level;
        const SeamConformResult strict = levelConformSealedSeams(level);
        check(strict.status == SeamConformStatus::Unconformable && strict.ambiguousCandidates > 0
            && strict.hasBlockingIssue && strict.blockingFirst.sector == 0,
            "seam conform: competing donors are refused and the issue is named");
        check(sameLevelGeometry(level, before), "seam conform: an ambiguous level is unchanged");

        SeamConformOptions options;
        options.allowRemaining = true;
        const SeamConformResult partial = levelConformSealedSeams(level, options);
        check(partial.succeeded() && partial.issuesRemaining > 0 && partial.ambiguousCandidates > 0
            && partial.hasBlockingIssue,
            "seam conform: allowRemaining completes and counts what it could not resolve");
        bool noneAmbiguous = true;
        for ( const SeamConformInsertion& insertion : partial.insertions )
        {
            noneAmbiguous = noneAmbiguous && insertion.kind != SealedSeamCandidateKind::Ambiguous;
        }
        check(noneAmbiguous, "seam conform: an ambiguous candidate is never inserted");
        check(issueCount(level) == partial.issuesRemaining,
            "seam conform: issuesRemaining is what a fresh inspection finds");
    }

    // The shared edge belongs to a portal: the corner has to reach the
    // adjoined mirror too, or the adjoin pair stops matching.
    {
        Level level;
        const std::int32_t west = room(level, {0, 0, 0}, {2, 2, 1});
        const std::int32_t east = room(level, {2, 0, 0}, {3, 2, 1});
        room(level, {2, 1, -1}, {4, 3, 0});
        check(levelConnectSectors(level, west, east), "seam conform: portal fixture adjoins");
        const std::int32_t portal = surfaceWhere(level, west, [](const Vertex& v) { return v.x == 2.0; });
        check(portal >= 0 && level.sectors[0].surfaces[static_cast<std::size_t>(portal)].adjoin.has_value(),
            "seam conform: portal fixture has an adjoined wall");
        if ( portal >= 0 && level.sectors[0].surfaces[static_cast<std::size_t>(portal)].adjoin )
        {
            const std::size_t surfacesBefore = surfaceCount(level);
            const SeamConformResult result = levelConformSealedSeams(level);
            check(result.succeeded() && result.issuesBefore > 0 && result.issuesRemaining == 0
                && issueCount(level) == 0, "seam conform: a T-junction on a portal edge conforms");
            const Surface& near = level.sectors[0].surfaces[static_cast<std::size_t>(portal)];
            check(near.adjoin.has_value(), "seam conform: the portal keeps its adjoin");
            if ( near.adjoin )
            {
                const SurfaceRef mirrorRef = *near.adjoin;
                const Surface& mirror = level.sectors[static_cast<std::size_t>(mirrorRef.sector)]
                    .surfaces[static_cast<std::size_t>(mirrorRef.surface)];
                check(mirror.adjoin == SurfaceRef{ 0, portal } && mirror.vertices.size() == near.vertices.size()
                    && near.vertices.size() == 5,
                    "seam conform: both sides of the portal receive the corner");
                check(hasCornerBits(level, SurfaceRef{ 0, portal }, 2, 1, 0)
                    && hasCornerBits(level, mirrorRef, 2, 1, 0),
                    "seam conform: portal mirrors hold the same donor corner");
            }
            check(surfaceCount(level) == surfacesBefore, "seam conform: portal conform splits no surface");
        }
    }

    // Malformed input is the inspector's refusal, surfaced, never a crash.
    {
        Level level = tee;
        level.sectors[0].surfaces[0].vertices[0] = -1;
        const Level before = level;
        const SeamConformResult result = levelConformSealedSeams(level);
        check(result.status == SeamConformStatus::InspectionIncomplete
            && result.inspection == SealedSeamStatus::InvalidGeometry,
            "seam conform: invalid geometry is refused through the inspection status");
        check(level.sectors[0].surfaces[0].vertices[0] == -1 && level.sectors.size() == before.sectors.size(),
            "seam conform: invalid geometry is not edited");
    }
}

} // namespace sed::tests
