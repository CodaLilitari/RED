#include "test_common.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/lighting.hpp"
#include "sed/level/shadow_cuts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
    runSizeLimitTests();
    runSizeLimitReachTests();
    runRemovalSeamTests();
    runCornerRollbackReferenceTest();
}

} // namespace sed::tests
