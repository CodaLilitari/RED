#include "sed/level/sealed_seams.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>

namespace sed::level
{

namespace
{
constexpr double kDistance = 0.002;
constexpr double kDistanceSquared = kDistance * kDistance;

struct Stop
{
    SealedSeamStatus status;
};

void charge(std::size_t& used, const std::size_t limit, const std::size_t count = 1)
{
    if ( used > limit || count > limit - used ) throw Stop{SealedSeamStatus::BudgetExceeded};
    used += count;
}

double finite(const double value)
{
    if ( !std::isfinite(value) ) throw Stop{SealedSeamStatus::ArithmeticUnresolved};
    return value;
}

TVector point(const Sector& sector, const Surface& surface, const std::size_t corner)
{
    const auto& v = sector.vertices[static_cast<std::size_t>(surface.vertices[corner])];
    return {v.x, v.y, v.z};
}

bool samePoint(const TVector& a, const TVector& b)
{
    return finite((a - b).LengthSqr()) < kDistanceSquared;
}

struct Work
{
    const SealedSeamLimits& limits;
    std::size_t geometry{};
    std::size_t pairs{};
    std::size_t edgeWork{};
    std::size_t contacts{};
    std::size_t insertions{};

    void step() { charge(edgeWork, limits.maxEdgePairs); }
};

bool coplanar(const Sector& aSector, const Surface& aSurface,
    const Sector& bSector, const Surface& bSurface, Work& work)
{
    if ( aSurface.vertices.size() < 3 || bSurface.vertices.size() < 3 ) return false;
    const TVector origin = point(aSector, aSurface, 0);
    TVector normal{};
    double lengthSquared = 0;
    for ( std::size_t i = 1; i + 1 < aSurface.vertices.size(); ++i )
    {
        work.step();
        normal = (point(aSector, aSurface, i) - origin).Cross(point(aSector, aSurface, i + 1) - origin);
        lengthSquared = finite(normal.LengthSqr());
        if ( lengthSquared > 1e-12 ) break;
        lengthSquared = 0;
    }
    if ( lengthSquared <= 1e-12 ) return false;
    const auto within = [&](const Sector& sector, const Surface& surface)
    {
        for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
        {
            work.step();
            const double distance = finite((point(sector, surface, i) - origin).Dot(normal));
            if ( finite(distance * distance) >= finite(kDistanceSquared * lengthSquared) ) return false;
        }
        return true;
    };
    return within(aSector, aSurface) && within(bSector, bSurface);
}

std::pair<double, double> offset(const TVector& a, const TVector& b, const TVector& p)
{
    const auto ab = b - a;
    const double lengthSquared = finite(ab.LengthSqr());
    if ( !(lengthSquared > 0) ) return {0, 0};
    const auto ap = p - a;
    return {finite(finite(ap.Dot(ab)) / lengthSquared),
        finite(finite(ap.Cross(ab).Length()) / std::sqrt(lengthSquared))};
}

bool overlaps(const TVector& a, const TVector& b, const TVector& c, const TVector& d)
{
    const double lengthSquared = finite((b - a).LengthSqr());
    if ( lengthSquared < kDistanceSquared ) return false;
    if ( (samePoint(a, c) && samePoint(b, d)) || (samePoint(a, d) && samePoint(b, c)) ) return false;
    const auto [tc, ec] = offset(a, b, c);
    const auto [td, ed] = offset(a, b, d);
    if ( ec >= kDistance || ed >= kDistance ) return false;
    return std::min(std::max(tc, td), 1.0) - std::max(std::min(tc, td), 0.0) > 0.001;
}

bool axisExact(const TVector& a, const TVector& b, const TVector& p)
{
    const auto between = [](const double x, const double y, const double z)
    {
        return z > std::min(x, y) && z < std::max(x, y);
    };
    return (a.y == b.y && a.z == b.z && p.y == a.y && p.z == a.z && between(a.x, b.x, p.x))
        || (a.x == b.x && a.z == b.z && p.x == a.x && p.z == a.z && between(a.y, b.y, p.y))
        || (a.x == b.x && a.y == b.y && p.x == a.x && p.y == a.y && between(a.z, b.z, p.z));
}

void observe(SealedSeamIssue& issue, const SealedSeamEdge target,
    const TVector& a, const TVector& b, const VertexRef donor, const TVector& p, Work& work)
{
    work.step();
    const auto [t, distance] = offset(a, b, p);
    if ( !(t > 0 && t < 1) || distance >= kDistance || samePoint(a, p) || samePoint(b, p) ) return;
    charge(work.insertions, work.limits.maxInsertions);
    issue.insertions.push_back({target, donor, p,
        axisExact(a, b, p) ? SealedSeamCandidateKind::ExactOnEdge : SealedSeamCandidateKind::ApproximateOnly});
}

auto targetKey(const SealedSeamInsertion* insertion)
{
    return std::tuple{insertion->target.surface.sector, insertion->target.surface.surface, insertion->target.edge};
}

void classifyAmbiguities(SealedSeamReport& report, Work& work)
{
    std::vector<SealedSeamInsertion*> candidates;
    candidates.reserve(work.insertions);
    for ( auto& issue : report.issues )
        for ( auto& insertion : issue.insertions ) candidates.push_back(&insertion);
    std::sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b)
    {
        return targetKey(a) < targetKey(b);
    });
    for ( std::size_t begin = 0; begin < candidates.size(); )
    {
        std::size_t end = begin + 1;
        while ( end < candidates.size() && targetKey(candidates[begin]) == targetKey(candidates[end]) ) ++end;
        for ( std::size_t i = begin; i < end; ++i )
        {
            for ( std::size_t j = i + 1; j < end; ++j )
            {
                work.step();
                const auto& a = candidates[i]->point;
                const auto& b = candidates[j]->point;
                if ( a.x == b.x && a.y == b.y && a.z == b.z ) continue;
                if ( samePoint(a, b) )
                {
                    candidates[i]->kind = SealedSeamCandidateKind::Ambiguous;
                    candidates[j]->kind = SealedSeamCandidateKind::Ambiguous;
                }
            }
        }
        begin = end;
    }
}
}

SealedSeamReport inspectSealedSeams(const Level& level, const SealedSeamLimits& limits) noexcept
{
    SealedSeamReport report;
    try
    {
        Work work{limits};
        // Include empty owners and unused vertices in admission, so malformed
        // sparse models cannot evade the corner work budget with empty records.
        charge(work.geometry, limits.maxCorners, level.sectors.size());
        if ( level.sectors.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) )
            throw Stop{SealedSeamStatus::BudgetExceeded};
        for ( const auto& sector : level.sectors )
        {
            charge(work.geometry, limits.maxCorners, sector.vertices.size());
            charge(work.geometry, limits.maxCorners, sector.surfaces.size());
            if ( sector.surfaces.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
                || sector.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) )
                throw Stop{SealedSeamStatus::BudgetExceeded};
            for ( const auto& vertex : sector.vertices )
                if ( !std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) )
                    throw Stop{SealedSeamStatus::InvalidGeometry};
            for ( const auto& surface : sector.surfaces )
            {
                charge(work.geometry, limits.maxCorners, surface.vertices.size());
                for ( const auto index : surface.vertices )
                    if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() )
                        throw Stop{SealedSeamStatus::InvalidGeometry};
            }
        }
        for ( std::size_t a = 0; a < level.sectors.size(); ++a )
        {
            const auto& aSector = level.sectors[a];
            for ( std::size_t af = 0; af < aSector.surfaces.size(); ++af )
            {
                const auto& aSurface = aSector.surfaces[af];
                for ( std::size_t b = a + 1; b < level.sectors.size(); ++b )
                {
                    // Charge traversals of empty sectors as well as face pairs.
                    if ( level.sectors[b].surfaces.empty() ) charge(work.pairs, limits.maxSurfacePairs);
                    const auto& bSector = level.sectors[b];
                    for ( std::size_t bf = 0; bf < bSector.surfaces.size(); ++bf )
                    {
                        charge(work.pairs, limits.maxSurfacePairs);
                        const auto& bSurface = bSector.surfaces[bf];
                        if ( aSurface.adjoin && bSurface.adjoin ) continue;
                        if ( !coplanar(aSector, aSurface, bSector, bSurface, work) ) continue;
                        SealedSeamIssue issue;
                        issue.first = {static_cast<std::int32_t>(a), static_cast<std::int32_t>(af)};
                        issue.second = {static_cast<std::int32_t>(b), static_cast<std::int32_t>(bf)};
                        for ( std::size_t ae = 0; ae < aSurface.vertices.size(); ++ae )
                        {
                            const auto an = (ae + 1) % aSurface.vertices.size();
                            const auto p = point(aSector, aSurface, ae), q = point(aSector, aSurface, an);
                            for ( std::size_t be = 0; be < bSurface.vertices.size(); ++be )
                            {
                                work.step();
                                const auto bn = (be + 1) % bSurface.vertices.size();
                                const auto r = point(bSector, bSurface, be), s = point(bSector, bSurface, bn);
                                if ( !overlaps(p, q, r, s) ) continue;
                                if ( issue.contacts.empty() && report.issues.size() >= limits.maxIssues )
                                    throw Stop{SealedSeamStatus::BudgetExceeded};
                                charge(work.contacts, limits.maxContacts);
                                const SealedSeamEdge first{issue.first, ae}, second{issue.second, be};
                                issue.contacts.push_back({first, second});
                                observe(issue, first, p, q, {issue.second.sector, bSurface.vertices[be]}, r, work);
                                observe(issue, first, p, q, {issue.second.sector, bSurface.vertices[bn]}, s, work);
                                observe(issue, second, r, s, {issue.first.sector, aSurface.vertices[ae]}, p, work);
                                observe(issue, second, r, s, {issue.first.sector, aSurface.vertices[an]}, q, work);
                            }
                        }
                        if ( !issue.contacts.empty() ) report.issues.push_back(std::move(issue));
                    }
                }
            }
        }
        classifyAmbiguities(report, work);
        report.status = SealedSeamStatus::Complete;
    }
    catch ( const Stop& stop ) { report.status = stop.status; }
    catch ( const std::bad_alloc& ) { report.status = SealedSeamStatus::AllocationFailure; }
    catch ( const std::length_error& ) { report.status = SealedSeamStatus::BudgetExceeded; }
    return report;
}

} // namespace sed::level
