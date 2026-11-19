#include "test_common.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/lighting.hpp"
#include "sed/level/shadow_cuts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

} // namespace

void runShadowCutTests()
{
    runShadowEdgeTests();
    runSizeLimitTests();
}

} // namespace sed::tests
