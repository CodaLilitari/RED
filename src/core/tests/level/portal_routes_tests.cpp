#include "sed/level/portal_routes.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::level;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

SurfaceRef addFace(Level& level, int sector, int flags)
{
    auto& faces = level.sectors[sector].surfaces;
    faces.emplace_back().adjoinFlags = flags;
    return { sector, static_cast<int>(faces.size()) - 1 };
}

SurfaceRef pair(Level& level, int a, int b, int forward = 2, int backward = 2)
{
    auto from = addFace(level, a, forward);
    auto to = addFace(level, b, backward);
    level.sectors[a].surfaces[from.surface].adjoin = to;
    level.sectors[b].surfaces[to.surface].adjoin = from;
    return from;
}

void testRoutes()
{
    Level level{};
    level.sectors.resize(5);
    const auto left = pair(level, 0, 1, 2, 1);
    const auto right = pair(level, 0, 2, 6, 2);
    pair(level, 1, 3);
    pair(level, 2, 3);
    const auto exit = pair(level, 3, 4);
    pair(level, 2, 2);
    PortalRouteRequest request{};
    request.startSector = 0;
    request.targetSector = 4;
    auto result = analyzePortalRoutes(level, request);
    check(result.baseline.reachable == std::vector<int>({ 0, 1, 2, 3, 4 }), "sorted reachable set");
    const auto& route = result.after.targetPath.value();
    check(route.size() == 3 && route.front().from == left && route.back().from == exit,
        "outgoing surface order breaks shortest-path ties");
    check(route.front().fromFlags == 2 && route.front().toFlags == 1, "witness preserves raw flags");
    const auto& review = result.probes.value();
    check(review.complete && review.checked == 3 && review.unavoidable.size() == 1
        && review.unavoidable.front().from == exit && review.alternative.value().front().from == right,
        "diamond reroute and shared unavoidable exit");
    check(result.asymmetricCount == 2 && result.movementMismatchCount == 1, "flag mismatch kinds");
    request.excludePortals = { left, { 1, 0 }, left, right };
    result = analyzePortalRoutes(level, request);
    check(result.excluded.size() == 2 && result.lost == std::vector<int>({ 1, 2, 3, 4 })
        && !result.after.targetPath && !result.probes, "closure aliases and unreachable target");
    request.excludePortals.clear();
    request.startSector = 1;
    request.targetSector = 0;
    result = analyzePortalRoutes(level, request);
    check(result.after.targetPath.value().size() == 3, "movement bit on departing face only");
    request.policy = PortalRoutePolicy::paired;
    result = analyzePortalRoutes(level, request);
    check(result.after.targetPath.value().size() == 1, "paired mode ignores visibility and movement bits");
    request.targetSector = 1;
    result = analyzePortalRoutes(level, request);
    check(result.after.targetPath.value().empty() && result.probes->complete && !result.probes->alternative,
        "start equals target is a zero-hop witness");
}

void testBrokenAndParallel()
{
    Level level{};
    level.sectors.resize(3);
    const auto first = pair(level, 0, 1);
    const auto second = pair(level, 0, 1);
    auto& faces = level.sectors[2].surfaces;
    faces.resize(5);
    faces[0].adjoin = SurfaceRef{ -1, 0 };
    faces[1].adjoin = SurfaceRef{ 0, 99 };
    faces[2].adjoin = SurfaceRef{ 2, 2 };
    faces[3].adjoin = first;
    faces[4].adjoin = addFace(level, 1, 7);
    PortalRouteRequest request{};
    request.targetSector = 1;
    auto result = analyzePortalRoutes(level, request);
    check(result.invalidCount == 5 && result.invalid.size() == 5, "all malformed pointer kinds diagnosed");
    check(result.invalid[0].reason == "sectorOutOfRange" && result.invalid[1].reason == "surfaceOutOfRange"
        && result.invalid[2].reason == "selfReference" && result.invalid[3].reason == "wrongMirror"
        && result.invalid[4].reason == "missingMirror", "exact diagnostics identify offending face");
    check(result.probes->unavoidable.empty() && result.probes->alternative.value().front().from == second,
        "parallel portals give distinct witnesses");
    request.excludePortals = { { 1, 0 } };
    result = analyzePortalRoutes(level, request);
    check(result.after.targetPath.value().front().from == second && result.lost.empty()
        && result.probes->unavoidable.size() == 1, "closing one parallel pair preserves its neighbor");
    request.targetSector.reset();
    result = analyzePortalRoutes(level, request);
    check(!result.probes && !result.after.targetPath, "reachability-only query");
    request.excludePortals = { { 2, 0 } };
    try
    {
        (void)analyzePortalRoutes(level, request);
    }
    catch ( const std::invalid_argument& )
    {
        return;
    }
    throw std::runtime_error("a malformed exclusion must refuse");
}

void testInvalidPolicy()
{
    Level level{};
    level.sectors.resize(1);
    PortalRouteRequest request;
    request.policy = static_cast<PortalRoutePolicy>(99);
    try
    {
        (void)analyzePortalRoutes(level, request);
    }
    catch ( const std::invalid_argument& )
    {
        return;
    }
    throw std::runtime_error("unrecognized core policy must refuse");
}

void testDiagnosticBounds()
{
    Level level{};
    level.sectors.resize(2);
    for ( int i = 0; i < 131; ++i )
    {
        pair(level, 0, 1, -1, 4);
        const auto bad = addFace(level, 1, 2);
        level.sectors[1].surfaces[bad.surface].adjoin = SurfaceRef{ 3, 1 };
    }
    const auto result = analyzePortalRoutes(level, {});
    check(result.pairCount == 131 && result.asymmetricCount == 131 && result.movementMismatchCount == 131
        && result.asymmetric.size() == 128 && result.asymmetric.back().from.surface == 127
        && result.asymmetric.front().fromFlags == 0xffffffffu, "bounded unequal raw flag rows have exact totals");
    check(result.invalidCount == 131 && result.invalid.size() == 128
        && result.invalid.back().from == SurfaceRef{ 1, 255 }, "invalid detail cap keeps first faces in stored order");

    PortalRouteRequest request;
    request.excludePortals.resize(kPortalRouteMaxExclusions, SurfaceRef{ 0, 0 });
    check(analyzePortalRoutes(level, request).excluded.size() == 1, "exact endpoint cap admits aliases");
    request.excludePortals.push_back({ 0, 0 });
    try
    {
        (void)analyzePortalRoutes(level, request);
    }
    catch ( const std::length_error& )
    {
        return;
    }
    throw std::runtime_error("exclusion budget must apply before deduplication");
}

void testLimits()
{
    Level level{};
    level.sectors.resize(1100);
    for ( int i = 1; i < 1100; ++i )
    {
        pair(level, i - 1, i);
    }
    PortalRouteRequest request{};
    request.targetSector = 1099;
    const auto result = analyzePortalRoutes(level, request);
    check(result.after.targetPath.value().size() == 1099 && !result.probes->complete
        && result.probes->checked > 0 && result.probes->checked < 1099
        && result.probes->checked == result.probes->unavoidable.size(), "bounded probes mark incomplete evidence");
    level.sectors.resize(4097);
    try
    {
        (void)analyzePortalRoutes(level, request);
    }
    catch ( const std::length_error& )
    {
        return;
    }
    throw std::runtime_error("level exceeding the whole-graph sector allowance must refuse");
}
} // namespace

int main()
{
    try
    {
        testRoutes();
        testBrokenAndParallel();
        testInvalidPolicy();
        testDiagnosticBounds();
        testLimits();
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
