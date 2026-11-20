#include "sed/level/ray_lighting.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/mirror.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
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

int face(const Sector& room, const TVector& direction)
{
    for ( std::size_t i = 0; i < room.surfaces.size(); ++i )
    {
        if ( room.surfaces[i].normal.Dot(direction) > 0.99 )
        {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("fixture face missing");
}

float meanRed(const Surface& surface)
{
    float sum = 0;
    for ( const auto& corner : surface.texVertices )
    {
        sum += corner.color.r;
    }
    return sum / static_cast<float>(surface.texVertices.size());
}

void roomTests()
{
    Level level;
    const std::array<int, 1> targets{0};
    levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
    const int east = face(level.sectors[0], {-1, 0, 0});
    const int west = face(level.sectors[0], {1, 0, 0});
    RayLightingOptions options;
    options.samples = 64;
    options.bounces = 0;
    options.skyColor = {1, 0, 0, 1};
    auto bake = [&] { return bakeRayLighting(level, targets, options); };
    check(bake().succeeded, "sealed room must bake");
    check(meanRed(level.sectors[0].surfaces[west]) == 0, "sealed room must remain dark");
    level.sectors[0].surfaces[east].surfaceFlags |= SF_SkyHorizon;
    level.kind = ProjectType::MOTS;
    for ( auto& v : level.sectors[0].surfaces[west].texVertices )
    {
        v.color.a = 0.37f;
    }
    check(bake().succeeded, "side sky must bake");
    for ( const auto& v : level.sectors[0].surfaces[west].texVertices )
    {
        check(v.color.r > 0 && v.color.g == 0 && v.color.b == 0 && v.color.a == 0.37f,
              "side sky must preserve MotS independent white and admit red at every corner");
    }
    const auto zeroBounce = meanRed(level.sectors[0].surfaces[west]);
    options.bounces = 2;
    check(bake().succeeded && meanRed(level.sectors[0].surfaces[west]) > zeroBounce,
          "diffuse bounce must increase indirect room light");
    const float previous = meanRed(level.sectors[0].surfaces[west]);
    check(bake().succeeded && meanRed(level.sectors[0].surfaces[west]) == previous,
          "repeated ray bakes must never feed their prior output into new illumination");
    level.kind = ProjectType::JKDF2;
    check(bake().succeeded, "JK bake must succeed");
    for ( const auto& v : level.sectors[0].surfaces[west].texVertices )
    {
        check(v.color.a > 0 && v.color.a == v.color.r && v.color.r == v.color.g && v.color.g == v.color.b,
              "JK scalar wire slot must match its grayscale corner value");
    }
    const auto saved = level.sectors[0].surfaces[west].texVertices[0].color;
    options.samples = kRayLightingMaxSamples + 1;
    check(!bake().succeeded, "reject excessive sample count");
    options.samples = 16;
    check(bakeRayLighting(level, targets, options, {}, {}, {}, [](auto, auto) { return false; }).cancelled,
          "cancel before publication");
    bool traced = false;
    const RayLightIntersector delegate = [&](auto triangles, auto nodes, auto rays, auto hits)
    {
        traced = true;
        return intersectRayLightingCpu(triangles, nodes, rays, hits);
    };
    const auto lateCancel = [&](std::size_t done, std::size_t total) { return !(traced && done == 1 && total == 1); };
    check(bakeRayLighting(level, targets, options, {}, {}, delegate, lateCancel).cancelled &&
              level.sectors[0].surfaces[west].texVertices[0].color.a == saved.a,
          "final callback veto after tracing must restore entire baseline");
    const auto nanHit = [](auto, auto, auto, auto hits)
    {
        for ( auto& hit : hits )
        {
            hit = {-1, std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return true;
    };
    check(!bakeRayLighting(level, targets, options, {}, {}, nanHit).succeeded,
          "non-finite backend output must abort even on a reported miss");
    const auto broken = [](auto, auto, auto, auto hits)
    {
        for ( auto& h : hits )
        {
            h.triangle = 1000000000;
        }
        return true;
    };
    check(!bakeRayLighting(level, targets, options, {}, {}, broken).succeeded, "reject invalid backend hit");
    check(level.sectors[0].surfaces[west].texVertices[0].color.a == saved.a,
          "rejected bake must leave corner untouched");
}

void portalAndBlockerTests()
{
    Level level;
    level.kind = ProjectType::MOTS;
    levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
    levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {1, 0, 0}, {2, 1, 1});
    levelAdjoinSectors(level, 0, 1);
    const int east = face(level.sectors[1], {-1, 0, 0});
    const int west = face(level.sectors[0], {1, 0, 0});
    const int door = face(level.sectors[0], {-1, 0, 0});
    level.sectors[1].surfaces[east].surfaceFlags |= SF_SkyHorizon;
    RayLightingOptions options;
    options.samples = 128;
    options.bounces = 0;
    const std::array<int, 1> targets{0};
    const float unselected = level.sectors[1].surfaces[east].texVertices[0].color.r;
    RayLightingContributions samples{};
    Level reported = level;
    const auto accepted = bakeRayLightingWithReport(reported, targets, options, {}, {}, {}, {}, samples);
    check(accepted.succeeded && samples.corners > 0 && samples.surfaces > 0 && samples.longestEdge > 0 &&
        samples.sky > 0 && samples.emission == 0, "sky-only receiver has measured sky, density and no emitted contribution");

    check(bakeRayLighting(level, targets, options).succeeded && meanRed(level.sectors[0].surfaces[west]) > 0,
          "side sky through a reciprocal portal");
    check(level.sectors[1].surfaces[east].texVertices[0].color.r == unselected,
          "unselected sector is read-only even if it supplies sky");
    level.sectors[0].surfaces[door].adjoinFlags |= SAF_BlockLight;
    level.sectors[0].surfaces[door].surfaceFlags |= SF_SkyHorizon;
    check(bakeRayLighting(level, targets, options).succeeded && meanRed(level.sectors[0].surfaces[west]) == 0,
          "BlockLight portal masks the sky");
    level.sectors[0].surfaces[door].adjoinFlags = 0;
    level.sectors[0].surfaces[door].surfaceFlags &= ~SF_SkyHorizon;
    const RayLightVec a{.5f, 0, 0}, b{.5f, 1, 0}, c{.5f, 1, 1}, d{.5f, 0, 1};
    const std::array<RayLightingBlocker, 2> blockers{{{0, {a, b, c}}, {0, {a, c, d}}}};
    check(bakeRayLighting(level, targets, options, {}, blockers).succeeded &&
              meanRed(level.sectors[0].surfaces[west]) == 0,
          "captured model geometry must shadow sky rays");
    // A wall one representable coordinate beyond a portal is still a wall.
    // Advancing the continuation by several float ULPs used to skip it.
    float behindPortal = std::nextafter(1.0f, 2.0f);
    auto thinGap = blockers;
    for ( auto& blocker : thinGap )
    {
        blocker.sector = 1;
        blocker.triangle.a.x = behindPortal;
        blocker.triangle.b.x = behindPortal;
        blocker.triangle.c.x = behindPortal;
    }
    check(bakeRayLighting(level, targets, options, {}, thinGap).succeeded &&
              meanRed(level.sectors[0].surfaces[west]) == 0,
          "a near-portal model blocker must not be jumped over");

    level.sectors[0].surfaces[door].adjoin.reset();
    check(!bakeRayLighting(level, targets, options).succeeded, "orphan adjoin must refuse without writing");

    Level overlapping;
    for ( int i = 0; i < 2; ++i )
    {
        levelCreateSectorPrimitiveBox(overlapping, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
    }
    overlapping.sectors[1].surfaces[face(overlapping.sectors[1], {-1, 0, 0})].surfaceFlags |= SF_SkyHorizon;
    check(bakeRayLighting(overlapping, targets, options).succeeded &&
              meanRed(overlapping.sectors[0].surfaces[west]) == 0,
          "disconnected overlapping sector cannot leak sky");
}

void corridorTests()
{
    Level level;
    level.kind = ProjectType::MOTS;
    const std::array<std::pair<int, int>, 9> tiles{
        {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 3}, {0, 4}}};
    for ( const auto& [x, y] : tiles )
    {
        const int index = levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {double(x), double(y), 0},
                                                        {double(x + 1), double(y + 1), 1});
        if ( index > 0 )
        {
            levelAdjoinSectors(level, index - 1, index);
        }
    }
    level.sectors.front().surfaces[face(level.sectors.front(), {1, 0, 0})].surfaceFlags |= SF_SkyHorizon;
    const std::array<int, 1> targets{8};
    const int floor = face(level.sectors.back(), {0, 0, 1});
    RayLightingOptions options;
    options.samples = 128;
    options.reflectivity = 0.9;
    options.skyColor = {1, 0, 0, 1};
    for ( unsigned order = 0; order <= 2; ++order )
    {
        options.bounces = order;
        const auto status = bakeRayLighting(level, targets, options);
        check(status.succeeded, "nine-room corridor must bake");
        const float light = meanRed(level.sectors.back().surfaces[floor]);
        check(order < 2 ? light == 0 : light > 0.0000001f,
              "final floor beyond corridor bends receives light only on second bounce");
    }
    std::vector<RayLightingMaterial> green;
    for ( std::size_t s = 0; s < level.sectors.size(); ++s )
    {
        for ( std::size_t f = 0; f < level.sectors[s].surfaces.size(); ++f )
        {
            const auto& surface = level.sectors[s].surfaces[f];
            if ( !surface.adjoin && !(surface.surfaceFlags & SF_SkyHorizon) )
            {
                green.push_back({static_cast<int>(s), static_cast<int>(f), {0, 1, 0, 1}});
            }
        }
    }
    check(bakeRayLighting(level, targets, options, green).succeeded &&
              meanRed(level.sectors.back().surfaces[floor]) == 0,
          "green diffuse walls absorb red in an indirect-only receiver");
}

void brightDirectSeedTests()
{
    for ( auto kind : {ProjectType::JKDF2, ProjectType::MOTS} )
    {
        Level level;
        level.kind = kind;
        levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
        auto& light = level.newLight();
        light.position = {.06, .06, .06};
        light.range = .5;
        light.color = ColorWhite;
        light.intensity = 4;
        const std::array<int, 1> targets{0};
        const int receiving = face(level.sectors[0], {-1, 0, 0});
        Level legacy = level;
        levelCalcLightingNew(legacy, targets);
        float maximum = 0;
        for ( const auto& surface : legacy.sectors[0].surfaces )
        {
            for ( const auto& corner : surface.texVertices )
            {
                maximum = std::max(maximum, corner.color.r);
            }
        }
        check(maximum == 1, "legacy direct result still normalizes bright corners");
        RayLightingOptions options;
        options.samples = 128;
        options.bounces = 1;
        options.skyIntensity = 0;
        options.reflectivity = .02;
        check(bakeRayLighting(level, targets, options).succeeded, "bright direct seed must bake");
        const float first = meanRed(level.sectors[0].surfaces[receiving]);
        light.intensity = 8;
        check(bakeRayLighting(level, targets, options).succeeded, "brighter direct seed must bake");
        const float second = meanRed(level.sectors[0].surfaces[receiving]);
        check(first > 0 && std::abs(second - 2 * first) < 1e-6f,
              "below-saturation reflected corner must retain energy from an overbright direct source");
    }
}

void worldCornerScopeTests()
{
    for ( const int flags : {0, SECF_NOAMBINETLIGHT, SECF_NORGBAMBINETLIGHT} )
    {
        Level level;
        levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
        level.sectors[0].flags |= flags;
        const ColorF original{.61f, .42f, .23f, .84f};
        level.sectors[0].ambient = original;
        const std::array<int, 1> targets{0};
        check(bakeRayLighting(level, targets, {}).succeeded && level.sectors[0].ambient == original,
              "ray lighting changes only world corners, preserving all sector ambient channels");
    }
    Level mirrored;
    levelCreateSectorPrimitiveBox(mirrored, SectorPrimitive::Cube, {0, 0, 0}, {1, 1, 1});
    const int floor = face(mirrored.sectors[0], {0, 0, 1});
    check(levelAddMirrorFloor(mirrored, {0, floor}, {}), "mirror fixture must build");
    const std::array<int, 1> targets{0};
    const auto rejected = bakeRayLighting(mirrored, targets, {});
    check(!rejected.succeeded && rejected.message.find("mirror") != std::string::npos,
          "ray transport must reject generated mirror floors before publication");
}

void directPortalBudgetTests()
{
    Level level;
    levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, {0,0,0}, {1,1,1});
    const Sector unit = level.sectors.front();
    const int east = face(unit, {-1,0,0});
    const int west = face(unit, {1,0,0});
    constexpr int rooms = 260;
    level.sectors.resize(rooms, unit);
    for ( int i = 0; i < rooms; ++i )
    {
        auto& sector = level.sectors[i];
        sector.id = i + 1;
        sector.num = i;
        for ( auto& v : sector.vertices )
        {
            v.x += i;
        }
        if ( i > 0 )
        {
            sector.surfaces[west].adjoin = SurfaceRef{i - 1, east};
        }
        if ( i + 1 < rooms )
        {
            sector.surfaces[east].adjoin = SurfaceRef{i + 1, west};
        }
    }
    level.sectors.back().surfaces[east].surfaceFlags |= SF_SkyHorizon;
    auto& light = level.lights.emplace_back();
    light.position = {.5,.5,.5};
    light.range = rooms + 1;
    light.intensity = 1;
    RayLightingOptions options;
    options.samples = 1;
    options.bounces = 0;
    const std::array<int,1> targets{0};
    auto& first = level.sectors[0].surfaces[west].texVertices[0].color;
    first = {.2f,.3f,.4f,.5f};
    const auto before = first;
    const auto point = bakeRayLighting(level, targets, options);
    const bool pointAtomic = first == before;
    first = before;
    light.flags = LF_Sun;
    light.position = {1,0,0};
    const auto sun = bakeRayLighting(level, targets, options);
    const auto depthRefusal = [](const auto& result)
    {
        return !result.succeeded && !result.cancelled
            && result.message.find("direct-light portal depth") != std::string::npos;
    };
    check(depthRefusal(point), "direct point-light portal depth reports distinct resource exhaustion");
    check(depthRefusal(sun), "direct sunlight portal depth reports distinct resource exhaustion");
    check(pointAtomic && first == before, "direct-budget refusals publish no partial light");
    Level shorter = level;
    shorter.sectors.resize(2);
    shorter.sectors.back().surfaces[east].adjoin.reset();
    shorter.sectors.back().surfaces[east].surfaceFlags |= SF_SkyHorizon;
    check(bakeRayLighting(shorter, targets, options).succeeded, "short direct sun path remains admissible");

    light.flags = 0;
    light.position = {.5,.5,.5};
    const std::array<int,1> last{rooms - 1};
    levelCalcLightingNew(level, last);
    check(meanRed(level.sectors.back().surfaces[east]) > 0, "legacy direct solver retains its original long path");
}

void intersectionTests()
{
    const RayLightTriangle t{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}};
    const std::array triangles{t, t};
    const std::array<RayLightNode, 1> nodes{{{{0, 0, 1}, {1, 1, 1}, -1, -1, 0, 2}}};
    const std::array<RayLightRay, 3> rays{{{{.25f, .25f, 0}, {0, 0, 1}, 0, 1, 0},
                                           {{.25f, .25f, 0}, {0, 0, 1}, 1, 2, 0},
                                           {{1, 1, 0}, {0, 0, 1}, 0, 2, 0}}};
    std::array<RayLightHit, 3> hits;
    check(intersectRayLightingCpu(triangles, nodes, rays, hits) && hits[0].triangle == 0 && hits[0].u == .25f &&
              hits[0].v == .25f && hits[0].distance == 1 && hits[1].triangle == -1 && hits[2].triangle == -1,
          "nearest hit, barycentrics, exact tie and ray interval");

    const std::array<RayLightTriangle, 1> fine{{{{0, 0, 1}, {.00001f, 0, 1}, {0, .00001f, 1}}}};
    const std::array<RayLightNode, 1> bounds{{{{0, 0, 1}, {.00001f, .00001f, 1}, -1, -1, 0, 1}}};
    const std::array<RayLightRay, 1> smallRay{{{{.000002f, .000002f, 0}, {0, 0, 1}, 0, 2, 0}}};
    std::array<RayLightHit, 1> smallHit;
    check(intersectRayLightingCpu(fine, bounds, smallRay, smallHit) && smallHit[0].triangle == 0,
          "small opaque blocker cannot vanish under a perpendicular ray");
}

} // namespace

int main()
{
    try
    {
        roomTests();
        portalAndBlockerTests();
        corridorTests();
        brightDirectSeedTests();
        worldCornerScopeTests();
        directPortalBudgetTests();
        intersectionTests();
    }
    catch ( const std::exception& exc )
    {
        std::fprintf(stderr, "%s\n", exc.what());
        return 1;
    }
    return 0;
}
