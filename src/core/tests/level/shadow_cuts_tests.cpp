#include "test_common.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/lighting.hpp"
#include "sed/level/package.hpp"
#include "sed/level/ray_lighting.hpp"
#include "sed/level/shadow_cuts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <sstream>
#include <string>

namespace sed::tests
{
namespace
{
using namespace sed::level;

std::int32_t room(Level& level, const TVector& low, const TVector& high)
{
    return levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, low, high);
}

template <typename Predicate>
std::vector<std::int32_t> facesWhere(const Level& level, const std::int32_t sector, Predicate inPlane)
{
    std::vector<std::int32_t> out;
    const Sector& owner = level.sectors[static_cast<std::size_t>(sector)];
    for ( std::size_t f = 0; f < owner.surfaces.size(); ++f )
    {
        bool all = !owner.surfaces[f].vertices.empty();
        for ( const std::int32_t index : owner.surfaces[f].vertices )
        {
            all = all && inPlane(owner.vertices[static_cast<std::size_t>(index)]);
        }
        if ( all ) out.push_back(static_cast<std::int32_t>(f));
    }
    return out;
}

bool hasCorner(const Level& level, const std::int32_t sector, const std::int32_t surface, const double x, const double y,
    const double z)
{
    const Sector& owner = level.sectors[static_cast<std::size_t>(sector)];
    for ( const std::int32_t index : owner.surfaces[static_cast<std::size_t>(surface)].vertices )
    {
        const Vertex& v = owner.vertices[static_cast<std::size_t>(index)];
        if ( std::abs(v.x - x) < 1e-6 && std::abs(v.y - y) < 1e-6 && std::abs(v.z - z) < 1e-6 ) return true;
    }
    return false;
}

/** Bakes a copy and counts the distinct corners of room B's floor the light reaches. */
std::size_t litFloorCorners(Level level)
{
    std::vector<std::int32_t> sectors(level.sectors.size());
    for ( std::size_t i = 0; i < sectors.size(); ++i ) sectors[i] = static_cast<std::int32_t>(i);
    levelCalcLightingNew(level, sectors);
    std::vector<std::int32_t> lit;
    for ( const std::int32_t face : facesWhere(level, 1, [](const Vertex& v) { return v.z == 0.0; }) )
    {
        const Surface& surface = level.sectors[1].surfaces[static_cast<std::size_t>(face)];
        for ( std::size_t k = 0; k < surface.vertices.size() && k < surface.texVertices.size(); ++k )
        {
            if ( surface.texVertices[k].color.intensity() > 0.01f
                && std::find(lit.begin(), lit.end(), surface.vertices[k]) == lit.end() )
            {
                lit.push_back(surface.vertices[k]);
            }
        }
    }
    return lit.size();
}

/** Room A (light inside) opens into the wider room B through a doorway one unit wide. */
Level doorway(bool& ok)
{
    Level level;
    room(level, { 0, 0, 0 }, { 2, 2, 1 });
    room(level, { 2, -1, 0 }, { 4, 3, 1 });
    const auto east = facesWhere(level, 0, [](const Vertex& v) { return v.x == 2.0; });
    ok = east.size() == 1 && levelCleaveSurface(level, { 0, east[0] }, TVector{ 0, 1, 0 }, 2, 1, 0);
    const auto west = facesWhere(level, 1, [](const Vertex& v) { return v.x == 2.0; });
    ok = ok && west.size() == 1 && levelCleaveSurface(level, { 1, west[0] }, TVector{ 0, 1, 0 }, 2, 0, 0);
    const auto westHigh = facesWhere(level, 1, [](const Vertex& v) { return v.x == 2.0 && v.y >= 0.0; });
    ok = ok && westHigh.size() == 1 && levelCleaveSurface(level, { 1, westHigh[0] }, TVector{ 0, 1, 0 }, 2, 1, 0);
    const auto door = facesWhere(level, 0, [](const Vertex& v) { return v.x == 2.0 && v.y <= 1.0; });
    const auto other = facesWhere(level, 1, [](const Vertex& v) { return v.x == 2.0 && v.y >= 0.0 && v.y <= 1.0; });
    ok = ok && door.size() == 1 && other.size() == 1
        && levelSysAdjoinSurfaces(level, { 0, door[0] }, { 1, other[0] }, true);
    SedLight light;
    light.position = TVector{ 1, 0.5, 0.5 };
    light.range = 10.0;
    light.intensity = 1.0;
    level.lights.push_back(light);
    return level;
}

void runShadowEdgeTests()
{
    bool ok = false;
    Level level = doorway(ok);
    check(ok, "shadow cuts: the doorway fixture builds");
    if ( !ok ) return;
    const std::size_t floorsBefore = facesWhere(level, 1, [](const Vertex& v) { return v.z == 0.0; }).size();
    const std::size_t litBefore = litFloorCorners(level);
    const ShadowCutResult result = levelShadowCuts(level, ShadowCutOptions{});
    const auto floors = facesWhere(level, 1, [](const Vertex& v) { return v.z == 0.0; });
    check(result.ok && floorsBefore == 1 && floors.size() == 3,
        "shadow cuts: room B's floor is cut in three by the planes through the light and the doorway's sides");
    bool lineFound = false;
    for ( const std::int32_t face : floors )
    {
        lineFound = lineFound || (hasCorner(level, 1, face, 2, 1, 0) && hasCorner(level, 1, face, 4, 2, 0));
    }
    check(lineFound, "shadow cuts: a cut runs where the light past the doorway's edge meets the floor, (2,1) to (4,2)");
    const std::size_t litAfter = litFloorCorners(level);
    check(litAfter == litBefore + 1,
        ("shadow cuts: the bake lights the new corner at (4,2), where the light's edge meets the far side; before, "
         "only the floor's corners at the doorway and on the other edge were lit (" + std::to_string(litBefore) + " -> " + std::to_string(litAfter) + ")").c_str());
    check(facesWhere(level, 0, [](const Vertex& v) { return v.z == 0.0; }).size() == 1,
        "shadow cuts: the lit room's own floor isn't cut");
    check(result.surfacesCut == 3 && result.newSurfaces == 5 && result.refusedCuts == 0 && result.lightsOutside == 0,
        ("shadow cuts: room B's floor and ceiling are cut twice and its far wall once (the other line meets that wall at its corner); nothing is refused (cut "
            + std::to_string(result.surfacesCut) + ", new " + std::to_string(result.newSurfaces) + ", refused "
            + std::to_string(result.refusedCuts) + ", planned " + std::to_string(result.plannedCuts) + ")").c_str());

    // Manually cleave the source-side floor, independently of the generated
    // families on the far side. The general cleanup would merge this edge.
    Level owned = level;
    const auto firstFloor = facesWhere(owned, 0, [](const Vertex& v) { return v.z == 0; });
    const auto& originalFace = owned.sectors[0].surfaces[firstFloor.front()];
    double middleX{};
    for ( const auto i : originalFace.vertices ) middleX += owned.sectors[0].vertices[i].x;
    middleX /= originalFace.vertices.size();
    check(levelCleaveSurface(owned, {0, firstFloor.front()}, {1, 0, 0}, middleX, 0, 0),
        "owned cuts: place an independently authored coplanar edge");
    Level seam = owned;
    const auto piece = result.families.front().front();
    seam.sectors[piece.sector].surfaces[piece.surface].extraLight = ColorRed;
    const auto count = seam.sectors[1].surfaces.size();
    check(!levelRemoveOwnedShadowCuts(seam, result.families) && seam.sectors[1].surfaces.size() == count,
        "owned cuts: a discontinuity retains the whole generation");
    check(levelRemoveOwnedShadowCuts(owned, result.families) &&
        facesWhere(owned, 0, [](const Vertex& v) { return v.z == 0; }).size() == 2 &&
        facesWhere(owned, 1, [](const Vertex& v) { return v.z == 0; }).size() == 1,
        "owned cuts: merge only the pieces of each generated family, leaving the authored split");
    const ShadowCutRemoval removal = levelRemoveShadowCuts(level, {});
    check(removal.merges == 5 && facesWhere(level, 1, [](const Vertex& v) { return v.z == 0.0; }).size() == 1,
        ("shadow cuts: removing them merges room B's floor back into one surface (merges "
            + std::to_string(removal.merges) + ", floors "
            + std::to_string(facesWhere(level, 1, [](const Vertex& v) { return v.z == 0.0; }).size()) + ")").c_str());

    Level blocked = doorway(ok);
    for ( Surface& surface : blocked.sectors[0].surfaces )
    {
        if ( surface.adjoin ) surface.adjoinFlags |= SAF_BlockLight;
    }
    const ShadowCutResult none = levelShadowCuts(blocked, ShadowCutOptions{});
    check(none.ok && none.newSurfaces == 0, "shadow cuts: a Blocks Light doorway stops the light, so nothing is cut");

    Level budget = doorway(ok);
    const Level before = budget;
    ShadowCutOptions small;
    small.maxNewSurfaces = 2;
    const ShadowCutResult refused = levelShadowCuts(budget, small);
    check(!refused.ok && refused.refusal == "overBudget" && budget.sectors[1].surfaces.size() == before.sectors[1].surfaces.size(),
        "shadow cuts: over budget, it refuses and changes nothing");
    ShadowCutOptions badLight;
    badLight.lights = { 4 };
    check(levelShadowCuts(budget, badLight).refusal == "badLight", "shadow cuts: a light that doesn't exist is refused");
}

void runSizeLimitTests()
{
    bool ok = false;
    Level level = doorway(ok);
    if ( !ok ) return;
    ShadowCutOptions options;
    options.mode = ShadowCutMode::sizeLimit;
    options.maxEdge = 0.75;
    const ShadowCutResult result = levelShadowCuts(level, options);
    const auto floors = facesWhere(level, 0, [](const Vertex& v) { return v.z == 0.0; });
    bool small = !floors.empty();
    for ( const std::int32_t face : floors )
    {
        const Sector& sector = level.sectors[0];
        const auto& corners = sector.surfaces[static_cast<std::size_t>(face)].vertices;
        for ( std::size_t i = 0; i < corners.size(); ++i )
        {
            const Vertex& a = sector.vertices[static_cast<std::size_t>(corners[i])];
            const Vertex& b = sector.vertices[static_cast<std::size_t>(corners[(i + 1) % corners.size()])];
            small = small && std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) <= 0.75 + 1e-9;
        }
    }
    check(result.ok && floors.size() >= 9 && small,
        "shadow cuts (size limit): the lit room's floor is split until no edge is longer than the limit");
    std::size_t openings = 0;
    for ( const Surface& surface : level.sectors[0].surfaces )
    {
        if ( surface.adjoin && surface.adjoin->sector == 1 ) ++openings;
    }
    check(openings == 1, "shadow cuts (size limit): the doorway itself is never cut, and stays one opening");
}

std::string nativeBytes(const Level& level)
{
    std::ostringstream stream;
    assets::saveToSED(level, stream);
    return stream.str();
}

void runSelectedReceiverTests()
{
    Level original;
    room(original, {0, 0, 0}, {2, 2, 1});
    room(original, {4, 0, 0}, {5, 1, 1});
    const auto floors = facesWhere(original, 0, [](const Vertex& v) { return v.z == 0; });
    check(floors.size() == 1, "receivers: synthetic room has one floor");
    if ( floors.size() != 1 ) return;
    const SurfaceRef floor{0, floors.front()};
    Surface& face = original.sectors[0].surfaces[floor.surface];
    face.material = "receiver.mat";
    face.geo = 4;
    face.light = 3;
    for ( std::size_t i = 0; i < face.vertices.size(); ++i )
    {
        const Vertex& vertex = original.sectors[0].vertices[face.vertices[i]];
        face.texVertices[i].u = static_cast<float>(vertex.x + 2 * vertex.y);
        face.texVertices[i].v = static_cast<float>(3 * vertex.x - vertex.y);
        face.texVertices[i].color = {static_cast<float>(0.1 + 0.02 * vertex.x),
            static_cast<float>(0.2 + 0.03 * vertex.y), 0.3f, 0.4f};
    }
    original.sectors[0].ambient = {0.125f, 0.125f, 0.125f};
    original.sectors[0].flags |= SECF_3DO; // Editor preview flag does not change JKL world geometry.
    ShadowCutOptions options;
    options.mode = ShadowCutMode::selectedReceivers;
    options.surfaces = {floor};
    options.maxEdge = 0.5;
    Level refined = original;
    CogValue reference;
    reference.type = ValueType::ptr;
    reference.cogType = CogType::surface;
    reference.intValue = refined.getGlobalSurfaceNumber(1, 0);
    refined.newCog().values.push_back(reference);
    const auto result = levelShadowCuts(refined, options);
    check(result.ok && result.newSurfaces > 0 && result.families.size() == 1,
        "receivers: selected floor gains interior samples without any baked lights");
    if ( !result.ok ) return;
    check(refined.cogs.front().values.front().intValue == refined.getGlobalSurfaceNumber(1, 0)
            && refined.cogs.front().values.front().intValue != reference.intValue,
        "receivers: the complete transaction renumbers a typed COG reference to the next sector");
    check(refined.lights.empty() && refined.things.empty() && refined.sectors.size() == original.sectors.size(),
        "receivers: refinement creates no source lights, model Things or sectors");
    check(refined.sectors[1].surfaces.size() == original.sectors[1].surfaces.size()
            && refined.sectors[0].ambient.r == 0.125f && refined.sectors[0].flags == original.sectors[0].flags,
        "receivers: unrelated faces, ambient and sector flags remain authored");
    bool interior = false;
    double greatestSpan = 0;
    for ( const auto ref : result.pieces )
    {
        const auto& sector = refined.sectors[ref.sector];
        const auto& piece = sector.surfaces[ref.surface];
        check(piece.material == face.material && piece.geo == 4 && piece.light == 3,
            "receivers: pieces retain material and render modes");
        for ( std::size_t i = 0; i < piece.vertices.size(); ++i )
        {
            const Vertex& a = sector.vertices[piece.vertices[i]];
            interior = interior || (a.x > 0 && a.x < 2 && a.y > 0 && a.y < 2);
            const auto& uv = piece.texVertices[i];
            check(std::abs(uv.u - (a.x + 2 * a.y)) < 1e-5 && std::abs(uv.v - (3 * a.x - a.y)) < 1e-5
                    && std::abs(uv.color.r - (0.1 + 0.02 * a.x)) < 1e-6
                    && std::abs(uv.color.g - (0.2 + 0.03 * a.y)) < 1e-6
                    && uv.color.b == 0.3f && uv.color.a == 0.4f,
                "receivers: UV and independent light channels interpolate rather than being rebaked");
            for ( const auto index : piece.vertices )
            {
                const Vertex& b = sector.vertices[index];
                greatestSpan = std::max(greatestSpan, std::hypot(a.x - b.x, a.y - b.y, a.z - b.z));
            }
        }
    }
    check(interior && greatestSpan <= options.maxEdge && result.maxReceiverSpan == greatestSpan,
        "receivers: every piece meets its full polygon span, with interior vertices and a truthful report");
    auto repeated = options;
    repeated.surfaces = result.pieces;
    const auto beforeRepeat = nativeBytes(refined);
    check(levelShadowCuts(refined, repeated).ok && nativeBytes(refined) == beforeRepeat,
        "receivers: repeating the satisfied density is an exact no-op");

    // A short boundary can still surround a large unsampled interior.
    Level round;
    const auto roundOwner = levelCreateSectorPrimitiveBox(round, SectorPrimitive::Cylinder,
        {-1, -1, 0}, {1, 1, 2}, 12);
    auto cap = std::find_if(round.sectors[roundOwner].surfaces.begin(), round.sectors[roundOwner].surfaces.end(),
        [](const Surface& s) { return s.vertices.size() == 12; });
    check(cap != round.sectors[roundOwner].surfaces.end(), "receivers: twelve-sided cap fixture exists");
    if ( cap != round.sectors[roundOwner].surfaces.end() )
    {
        cap->material = "receiver.mat";
        cap->geo = 4;
        cap->light = 3;
        double longestBoundary = 0;
        for ( std::size_t i = 0; i < cap->vertices.size(); ++i )
        {
            const auto& a = round.sectors[roundOwner].vertices[cap->vertices[i]];
            const auto& b = round.sectors[roundOwner].vertices[cap->vertices[(i + 1) % cap->vertices.size()]];
            longestBoundary = std::max(longestBoundary, std::hypot(a.x - b.x, a.y - b.y, a.z - b.z));
        }
        auto shortEdges = options;
        shortEdges.surfaces = {{roundOwner, static_cast<std::int32_t>(cap - round.sectors[roundOwner].surfaces.begin())}};
        shortEdges.maxEdge = 0.75;
        const auto rounded = levelShadowCuts(round, shortEdges);
        bool addedInterior = false;
        bool bounded = true;
        for ( const auto ref : rounded.pieces )
        {
            const auto& sector = round.sectors[ref.sector];
            const auto& piece = sector.surfaces[ref.surface];
            bounded = bounded && piece.vertices.size() <= 24 && surfaceHasSimpleBoundary(sector, piece);
            for ( const auto index : piece.vertices )
            {
                const auto& a = sector.vertices[index];
                addedInterior = addedInterior || std::hypot(a.x, a.y) < 0.9;
                for ( const auto other : piece.vertices )
                {
                    const auto& b = sector.vertices[other];
                    bounded = bounded && std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) <= shortEdges.maxEdge;
                }
            }
        }
        check(longestBoundary < shortEdges.maxEdge && rounded.ok && addedInterior && bounded,
            ("receivers: short perimeter still gains valid bounded interior samples (" + rounded.refusal + ")").c_str());
    }

    // Local turn signs alone accept a five-point star. An already-small
    // malformed receiver must reject a mixed batch before any edits publish.
    Level star = original;
    auto& starSector = star.sectors.emplace_back();
    for ( int i = 0; i < 5; ++i )
    {
        const double angle = 2 * std::acos(-1.0) * i / 5;
        starSector.vertices.push_back({6 + std::cos(angle) * 0.1, std::sin(angle) * 0.1, 0});
    }
    auto& starFace = starSector.surfaces.emplace_back();
    starFace.vertices = {0, 2, 4, 1, 3};
    starFace.normal = {0, 0, 1};
    starFace.material = "receiver.mat";
    starFace.geo = 4;
    starFace.light = 3;
    starFace.texVertices.resize(5);
    auto mixed = options;
    mixed.surfaces.push_back({2, 0});
    const auto starBefore = nativeBytes(star);
    const auto starResult = levelShadowCuts(star, mixed);
    check(!starResult.ok && starResult.refusal == "badReceiver" && nativeBytes(star) == starBefore,
        "receivers: an already-small self-intersecting receiver refuses the whole mixed batch");

    const auto refuses = [&](auto changeLevel, auto changeOptions, const std::string& reason)
    {
        Level candidate = original;
        ShadowCutOptions request = options;
        changeLevel(candidate);
        changeOptions(request);
        const auto before = nativeBytes(candidate);
        const auto rejected = levelShadowCuts(candidate, request);
        check(!rejected.ok && rejected.refusal == reason && nativeBytes(candidate) == before,
            ("receivers: atomic refusal " + reason + " (actual " + rejected.refusal
                + ", ok=" + std::to_string(rejected.ok) + ")").c_str());
    };
    const auto keepLevel = [](Level&) {};
    const auto keepOptions = [](ShadowCutOptions&) {};
    refuses(keepLevel, [](auto& o) { o.maxNewSurfaces = 1; }, "overBudget");
    // The output count alone would admit this dense floor. The same
    // growing owner must be validated and copied again for every cut.
    auto dense = options;
    dense.maxEdge = 0.2;
    dense.maxNewSurfaces = 100000;
    const auto denseBaseline = nativeBytes(original);
    Level denseFloor = original;
    const auto first = levelShadowCuts(denseFloor, dense);
    const auto second = levelShadowCuts(denseFloor, dense);
    check(!first.ok && first.refusal == "receiverWorkLimit" && first.newSurfaces > 0
            && first.newSurfaces < dense.maxNewSurfaces && second.refusal == first.refusal
            && second.newSurfaces == first.newSurfaces && nativeBytes(denseFloor) == denseBaseline,
        "receivers: dense floor exhausts cumulative work deterministically and preserves all source bytes");
    refuses(keepLevel, [](auto& o) { o.surfaces.clear(); }, "badReceiver");
    refuses(keepLevel, [](auto& o) { o.surfaces.push_back(o.surfaces.front()); }, "badReceiver");
    refuses(keepLevel, [](auto& o) { o.surfaces = {{-1, 0}}; }, "badReceiver");
    refuses(keepLevel, [](auto& o) { o.lights = {0}; }, "badOptions");
    refuses(keepLevel, [](auto& o) { o.maxEdge = std::numeric_limits<double>::infinity(); }, "badOptions");
    refuses(keepLevel, [](auto& o) { o.mode = static_cast<ShadowCutMode>(99); }, "badOptions");
    for ( const int mode : {0, 1, 2, 4, 5} )
    {
        refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].light = mode; }, keepOptions, "badReceiver");
    }
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].geo = 0; }, keepOptions, "badReceiver");
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].surfaceFlags |= SF_SkyHorizon; }, keepOptions, "badReceiver");
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].faceFlags |= 0x2u; }, keepOptions, "badReceiver");
    for ( const auto* material : {"", " NoNe "} )
    {
        refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].material = material; }, keepOptions, "badReceiver");
    }
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].vertices[0] = 99999; }, keepOptions, "badReceiver");
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].texVertices.pop_back(); }, keepOptions, "badReceiver");
    refuses([&](auto& l) { l.sectors[0].surfaces[floor.surface].adjoin = SurfaceRef{1, 0}; }, keepOptions, "badReceiver");
    refuses([&](auto& l)
        {
            auto& repeatedCorner = l.sectors[0].surfaces[floor.surface];
            repeatedCorner.vertices.push_back(repeatedCorner.vertices.front());
            repeatedCorner.texVertices.push_back(repeatedCorner.texVertices.front());
        }, [](auto& o) { o.maxEdge = 10; }, "badReceiver");
    // A corrupt unselected neighbour must reach guarded cleave validation,
    // never the legacy fast corner estimate's unchecked indices.
    refuses([&](auto& l)
        {
            const auto walls = facesWhere(l, 0, [](const Vertex& v) { return v.x == 0; });
            auto& wall = l.sectors[0].surfaces[walls.front()];
            // Keep the shared floor edge intact so the neighbour is in the
            // affected scope; corrupt a top corner of that same wall.
            auto top = std::find_if(wall.vertices.begin(), wall.vertices.end(),
                [&](const auto index) { return l.sectors[0].vertices[index].z == 1; });
            *top = 99999;
        }, keepOptions, "receiverGeometry");

    Level crowded = original;
    const auto owner = levelCreateSectorPrimitiveBox(crowded, SectorPrimitive::Cylinder, {10, 0, 0}, {12, 2, 4}, 25);
    auto wall = std::find_if(crowded.sectors[owner].surfaces.begin(), crowded.sectors[owner].surfaces.end(),
        [](const Surface& s) { return s.vertices.size() == 4; });
    if ( wall != crowded.sectors[owner].surfaces.end() )
    {
        wall->material = "receiver.mat";
        wall->geo = 4;
        wall->light = 3;
        auto capped = options;
        capped.surfaces = {{owner, static_cast<std::int32_t>(wall - crowded.sectors[owner].surfaces.begin())}};
        capped.maxEdge = 3;
        const auto before = nativeBytes(crowded);
        const auto refused = levelShadowCuts(crowded, capped);
        check(!refused.ok && refused.refusal == "cornerLimit" && nativeBytes(crowded) == before,
            "receivers: a neighbour over the corner cap prevents the complete candidate");
    }
    else check(false, "receivers: cylinder fixture supplies a wall");
}

void runRemovalSeamTests()
{
    Level split;
    room(split, {0, 0, 0}, {2, 2, 1});
    const auto floor = facesWhere(split, 0, [](const Vertex& v) { return v.z == 0; });
    const bool cut = floor.size() == 1 && levelCleaveSurface(split, {0, floor.front()}, {1, 0, 0}, 1, 0, 0);
    check(cut, "shadow cut removal: build adjacent floor halves");
    if ( !cut )
    {
        return;
    }
    for ( const auto f : facesWhere(split, 0, [](const Vertex& v) { return v.z == 0; }) )
    {
        Surface& surface = split.sectors[0].surfaces[f];
        for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
        {
            const auto& v = split.sectors[0].vertices[surface.vertices[i]];
            surface.texVertices[i].u = static_cast<float>(v.x + 2 * v.y);
            surface.texVertices[i].v = static_cast<float>(v.y - v.x);
        }
    }
    std::stringstream native;
    assets::saveToSED(split, native);
    Level continuous = assets::loadFromSED(native);
    std::array<SurfaceRef, 1> tracked{SurfaceRef{0, static_cast<std::int32_t>(split.sectors[0].surfaces.size() - 1)}};
    check(levelRemoveShadowCuts(continuous, {}, tracked).merges == 1,
          "shadow cut removal: continuous transformed UVs merge after native save and reload");
    check(tracked.front() == SurfaceRef{0, floor.front()},
          "shadow cut removal: a tracked donor follows the surviving face");
    const auto staysSplit = [&](auto change)
    {
        Level altered = split;
        Surface& donor = altered.sectors[0].surfaces.back();
        change(donor, altered.sectors[0]);
        return levelRemoveShadowCuts(altered, {}).merges == 0;
    };
    check(staysSplit([](Surface& s, const Sector&) { s.extraLight = ColorRed; }),
          "shadow cut removal: preserve different authored extra light");
    check(staysSplit(
              [](Surface& s, const Sector&)
              {
                  for ( auto& uv : s.texVertices )
                  {
                      uv.u += 1.0f;
                  }
              }),
          "shadow cut removal: preserve a translated texture seam");
    check(staysSplit(
              [](Surface& s, const Sector& sector)
              {
                  for ( std::size_t i = 0; i < s.vertices.size(); ++i )
                  {
                      s.texVertices[i].v += static_cast<float>(sector.vertices[s.vertices[i]].x - 1.0);
                  }
              }),
          "shadow cut removal: preserve a different texture gradient that agrees along the shared edge");
    check(staysSplit(
              [](Surface& s, const Sector&)
              {
                  for ( auto& uv : s.texVertices )
                  {
                      uv.color = ColorBlue;
                  }
              }),
          "shadow cut removal: preserve a shared-edge corner lighting discontinuity");
}

void runCornerRollbackReferenceTest()
{
    Level level;
    level.kind = ProjectType::IJIM;
    const auto owner = levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cylinder, {-1, -1, 0}, {1, 1, 4}, 25);
    const auto other = room(level, {10, 0, 0}, {11, 1, 1});
    check(owner == 0 && other == 1, "shadow cut rollback: build IJIM cylinder and reference owner");
    if ( owner != 0 || other != 1 )
    {
        return;
    }
    for ( Surface& s : level.sectors[0].surfaces )
    {
        s.light = 0;
    }
    auto wall = std::find_if(level.sectors[0].surfaces.begin(), level.sectors[0].surfaces.end(),
                             [](const Surface& s) { return s.vertices.size() == 4; });
    if ( wall == level.sectors[0].surfaces.end() )
    {
        check(false, "shadow cut rollback: cylinder has a vertical wall");
        return;
    }
    wall->light = 3;
    SedLight light;
    light.position = {0, 0, 2};
    light.range = 8;
    light.intensity = 1;
    level.lights.push_back(light);
    CogValue reference;
    reference.type = ValueType::ptr;
    reference.cogType = CogType::surface;
    reference.intValue = level.getGlobalSurfaceNumber(1, 0);
    level.newCog().values.push_back(reference);

    ShadowCutOptions options;
    options.mode = ShadowCutMode::sizeLimit;
    options.maxEdge = 3;
    const auto result = levelShadowCuts(level, options);
    check(result.ok && result.cornerLimitSkips == 1 && result.newSurfaces == 0,
          "shadow cut rollback: 25-corner cap in the owner causes the provisional cut to be undone");
    check(level.cogs.front().values.front().intValue == level.getGlobalSurfaceNumber(1, 0),
          "shadow cut rollback: the COG still points at the same floor after a provisional cut is undone");
}

void runRayBakedPackageTest()
{
    bool ok = false;
    Level level = doorway(ok);
    check(ok, "cut-and-bake package: build the two-room doorway");
    if ( !ok ) return;
    level.kind = ProjectType::MOTS;
    level.header.version = 1;
    level.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
    for ( auto& sector : level.sectors )
    {
        for ( auto& surface : sector.surfaces )
        {
            for ( auto& corner : surface.texVertices ) corner.color = {0, 0, 0, 0.375f};
        }
    }
    const auto ceiling = facesWhere(level, 1, [](const Vertex& v) { return v.z == 1; });
    check(ceiling.size() == 1, "cut-and-bake package: one sky ceiling before cuts");
    if ( ceiling.size() != 1 ) return;
    level.sectors[1].surfaces[ceiling.front()].surfaceFlags |= SF_SkyCeiling;
    const auto cuts = levelShadowCuts(level, {});
    check(cuts.ok && cuts.newSurfaces > 0, "cut-and-bake package: doorway generates shadow cuts");
    if ( !cuts.ok || !cuts.newSurfaces ) return;

    // Use the light for cuts, then remove it so only sampled sky can light the new corners.
    level.lights.clear();
    RayLightingOptions options;
    options.skyColor = {1, 0, 0, 1};
    options.samples = 32;
    options.bounces = 1;
    const std::array<std::int32_t, 2> targets{0, 1};
    const auto bake = bakeRayLighting(level, targets, options);
    check(bake.succeeded && bake.rays != 0, "cut-and-bake package: CPU transported sky radiance");
    if ( !bake.succeeded ) return;

    std::ostringstream exported;
    assets::saveToJKL(level, exported);
    PackageRequest package;
    package.inputBytes = exported.str();
    package.levelFileName = "doorway.jkl";
    package.episodeTitle = "Cut and bake regression";
    package.archiveExtension = ".goo";
    package.archives.push_back({"synthetic stock", true,
        {{"mat/dflt.mat", "fixture MAT"}, {"misc/cmp/dflt.cmp", "fixture CMP"}}});
    const auto plan = planLevelPackage(package);
    check(plan.canBuild, "cut-and-bake package: exported level can be packaged");
    if ( !plan.canBuild ) return;
    const auto bytes = std::as_bytes(std::span(plan.outputData));
    const auto archive = assets::GobArchive::fromBytes({bytes.begin(), bytes.end()});
    const auto entry = archive.read("jkl/doorway.jkl");
    std::string packaged(reinterpret_cast<const char*>(entry.data()), entry.size());
    check(packaged == package.inputBytes && archive.contains("episode.jk"),
          "cut-and-bake package: episode contains the exact exported map");

    std::istringstream input(packaged);
    const auto reopened = assets::loadFromJKL(input, {.projectType = ProjectType::MOTS});
    check(reopened.sectors.size() == 2, "cut-and-bake package: both rooms reopen");
    if ( reopened.sectors.size() != 2 ) return;
    const auto floors = facesWhere(reopened, 1, [](const Vertex& v) { return v.z == 0; });
    check(floors.size() == 3, "cut-and-bake package: the three cut floor pieces survive export");
    std::size_t illuminated = 0;
    for ( const auto f : floors )
    {
        const auto& sector = reopened.sectors[1];
        const auto& floor = sector.surfaces[f];
        for ( std::size_t i = 0; i < floor.vertices.size(); ++i )
        {
            const auto& v = sector.vertices.at(floor.vertices[i]);
            if ( v.x != 4 || v.y != 2 ) continue; // Intersection of the projected doorway edge and the far wall.
            const auto& color = floor.texVertices.at(i).color;
            check(color.r > 0 && color.g == 0 && color.b == 0 && std::abs(color.a - 0.375f) < 0.0001f,
                  "cut-and-bake package: a new MotS floor corner retains red sky light and independent white");
            ++illuminated;
        }
    }
    check(illuminated >= 2, "cut-and-bake package: adjacent pieces share the generated corner");
}

void runSizeLimitReachTests()
{
    ShadowCutOptions options;
    options.mode = ShadowCutMode::sizeLimit;
    options.maxEdge = 0.8;
    bool ok = false;
    Level occluded = doorway(ok);
    const auto floor = facesWhere(occluded, 1, [](const Vertex& v) { return v.z == 0; });
    ok = ok && floor.size() == 1 && levelCleaveSurface(occluded, {1, floor.front()}, {0, 1, 0}, 0, 2.5, 0);
    check(ok, "shadow cut reach: build a receiver wholly outside the doorway cone");
    if ( !ok )
    {
        return;
    }
    auto hiddenFloor = [](const Vertex& v) { return v.z == 0 && v.y >= 2.5; };
    const auto hidden = facesWhere(occluded, 1, hiddenFloor);
    check(hidden.size() == 1, "shadow cut reach: one occluded receiver");
    if ( hidden.size() != 1 )
    {
        return;
    }
    for ( Sector& sector : occluded.sectors )
    {
        for ( Surface& surface : sector.surfaces )
        {
            surface.light = 0;
        }
    }
    occluded.sectors[1].surfaces[hidden.front()].light = 3;
    const auto cut = levelShadowCuts(occluded, options);
    check(cut.ok && cut.newSurfaces == 0, "shadow cut reach: wholly occluded receiver stays one face");

    Level ranged;
    room(ranged, {0, 0, 0}, {4, 4, 1});
    const auto bottom = facesWhere(ranged, 0, [](const Vertex& v) { return v.z == 0; });
    ok = bottom.size() == 1 && levelCleaveSurface(ranged, {0, bottom.front()}, {1, 0, 0}, 2, 0, 0);
    check(ok, "shadow cut reach: build coplanar in-range and out-of-range receivers");
    if ( !ok )
    {
        return;
    }
    SedLight light;
    light.position = {1, 1.7, 0.25};
    light.range = 0.5;
    light.intensity = 1;
    ranged.lights.push_back(light);
    const auto result = levelShadowCuts(ranged, options);
    check(result.ok && facesWhere(ranged, 0, [](const Vertex& v) { return v.z == 0 && v.x >= 2; }).size() == 1,
          "shadow cut reach: light range is measured to the finite receiver");
    check(result.ok && facesWhere(ranged, 0, [](const Vertex& v) { return v.z == 0 && v.x <= 2; }).size() > 1,
          "shadow cut reach: a light can reach the interior even when every original corner is out of range");
}

} // namespace

void runShadowCutTests()
{
    runShadowEdgeTests();
    runRayBakedPackageTest();
    runSizeLimitTests();
    runSelectedReceiverTests();
    runSizeLimitReachTests();
    runRemovalSeamTests();
    runCornerRollbackReferenceTest();
}

} // namespace sed::tests
