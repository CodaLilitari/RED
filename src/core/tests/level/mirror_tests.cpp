#include "test_common.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/mirror.hpp"

#include <cmath>
#include <cstdint>

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
std::int32_t faceWhere(const Level& level, const std::int32_t sector, Predicate inPlane)
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

bool close(const double a, const double b, const double eps = 1e-6)
{
    return std::abs(a - b) <= eps;
}

// Every corner gets its own light so a lost reversal shows.
void paintCorners(Level& level, const std::int32_t sector, const float base)
{
    float value = base;
    for ( Surface& surface : level.sectors[static_cast<std::size_t>(sector)].surfaces )
    {
        for ( TexVertex& corner : surface.texVertices )
        {
            corner.color = ColorF{ value, value * 0.5f, value * 0.25f, value };
            value += 0.01f;
        }
    }
}

bool mirrorCornersMatch(const Level& level, const std::int32_t roomIndex, const std::int32_t mirrorIndex, const double k)
{
    const Sector& roomSector = level.sectors[static_cast<std::size_t>(roomIndex)];
    const Sector& mirror = level.sectors[static_cast<std::size_t>(mirrorIndex)];
    for ( std::size_t s = 0; s < roomSector.surfaces.size(); ++s )
    {
        const auto& a = roomSector.surfaces[s].texVertices;
        const auto& b = mirror.surfaces[s].texVertices;
        if ( a.size() != b.size() ) return false;
        for ( std::size_t c = 0; c < a.size(); ++c )
        {
            const ColorF& from = a[a.size() - 1 - c].color;
            const ColorF& to = b[c].color;
            if ( !close(to.r, from.r * k, 1e-5) || !close(to.g, from.g * k, 1e-5) || !close(to.b, from.b * k, 1e-5)
                || !close(to.a, from.a * k, 1e-5) )
                return false;
        }
    }
    return true;
}

void runSingleRoomTests()
{
    Level level;
    const std::int32_t r = room(level, { 0, 0, 0 }, { 2, 2, 1 });
    check(r == 0, "mirror floor: fixture builds a room");
    if ( r != 0 ) return;
    level.sectors[0].flags |= SECF_CollideBox;
    level.sectors[0].ambient = ColorF{ 0.8f, 0.6f, 0.4f };
    paintCorners(level, 0, 0.5f);
    const std::int32_t floor = faceWhere(level, 0, [](const Vertex& v) { return v.z == 0.0; });
    const std::int32_t ceiling = faceWhere(level, 0, [](const Vertex& v) { return v.z == 1.0; });
    const std::int32_t wall = faceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0; });
    check(floor >= 0 && ceiling >= 0 && wall >= 0, "mirror floor: fixture has a floor, a ceiling and a wall");
    if ( floor < 0 || ceiling < 0 || wall < 0 ) return;

    // Faces that are not a floor looking up into the room are refused, and
    // a refusal leaves the level as it was.
    MirrorFloorReport report{};
    check(!levelAddMirrorFloor(level, { 0, wall }, {}, &report) && report.error == MirrorFloorError::NotFloor
            && level.sectors.size() == 1,
        "mirror floor: a wall is refused");
    check(!levelAddMirrorFloor(level, { 0, ceiling }, {}, &report) && report.error == MirrorFloorError::NotFloor,
        "mirror floor: a ceiling (looking down) is refused");
    MirrorFloorOptions bright{};
    bright.reflectivity = 1.5;
    check(!levelAddMirrorFloor(level, { 0, floor }, bright, &report) && report.error == MirrorFloorError::InvalidReflectivity,
        "mirror floor: reflectivity above 1 is refused");
    MirrorFloorOptions missing{};
    missing.openingMaterialIndex = static_cast<std::int32_t>(level.materials.size()) + 3;
    check(!levelAddMirrorFloor(level, { 0, floor }, missing, &report) && report.error == MirrorFloorError::InvalidMaterial,
        "mirror floor: an opening material outside the table is refused");
    check(!levelAddMirrorFloor(level, { 0, 99 }, {}, &report) && report.error == MirrorFloorError::InvalidSurface,
        "mirror floor: a missing surface is refused");
    check(level.sectors.size() == 1 && !level.sectors[0].surfaces[static_cast<std::size_t>(floor)].adjoin,
        "mirror floor: refusals leave the level untouched");

    MirrorFloorOptions options{};
    options.reflectivity = 0.5;
    const bool built = levelAddMirrorFloor(level, { 0, floor }, options, &report);
    check(built && report.error == MirrorFloorError::None && level.sectors.size() == 2 && report.mirrorSector == 1
            && report.mirrorSurface == floor && report.sealedOpenings.empty(),
        "mirror floor: builds the copy as a new last sector");
    if ( !built || level.sectors.size() != 2 ) return;
    const Sector& roomSector = level.sectors[0];
    const Sector& mirror = level.sectors[1];

    bool reflected = mirror.vertices.size() == roomSector.vertices.size();
    for ( std::size_t v = 0; reflected && v < roomSector.vertices.size(); ++v )
    {
        reflected = mirror.vertices[v].x == roomSector.vertices[v].x && mirror.vertices[v].y == roomSector.vertices[v].y
            && mirror.vertices[v].z == -roomSector.vertices[v].z;
    }
    check(reflected, "mirror floor: the copy's vertices are the room's, reflected across the floor plane");

    bool reversed = mirror.surfaces.size() == roomSector.surfaces.size();
    bool normalsReflected = reversed;
    bool uvsCarried = reversed;
    for ( std::size_t s = 0; reversed && s < roomSector.surfaces.size(); ++s )
    {
        const Surface& a = roomSector.surfaces[s];
        const Surface& b = mirror.surfaces[s];
        const std::size_t n = a.vertices.size();
        reversed = b.vertices.size() == n;
        for ( std::size_t k = 0; reversed && k < n; ++k )
        {
            reversed = b.vertices[k] == a.vertices[n - 1 - k];
            uvsCarried = uvsCarried && b.texVertices[k].u == a.texVertices[n - 1 - k].u
                && b.texVertices[k].v == a.texVertices[n - 1 - k].v;
        }
        const TVector na = surfaceCalcNormalNewell(roomSector, a);
        const TVector nb = surfaceCalcNormalNewell(mirror, b);
        normalsReflected = normalsReflected && close(na.x, nb.x) && close(na.y, nb.y) && close(na.z, -nb.z);
    }
    check(reversed, "mirror floor: each face's corners are reversed, as a reflection flips winding");
    check(normalsReflected, "mirror floor: every face of the copy faces into the copy");
    check(uvsCarried, "mirror floor: each corner keeps its texture coordinates, which mirrors the texture");
    check(mirrorCornersMatch(level, 0, 1, 0.5), "mirror floor: the room's corner light is copied at the reflectivity");
    check(close(mirror.ambient.r, 0.4) && close(mirror.ambient.g, 0.3) && close(mirror.ambient.b, 0.2),
        "mirror floor: the copy's ambient light is the room's at the reflectivity");
    check((mirror.flags & SECF_CollideBox) == 0, "mirror floor: the copy carries no collide box");

    const Surface& walkOn = roomSector.surfaces[static_cast<std::size_t>(floor)];
    const Surface& top = mirror.surfaces[static_cast<std::size_t>(floor)];
    check(walkOn.adjoin && walkOn.adjoin->sector == 1 && walkOn.adjoin->surface == floor
            && top.adjoin && top.adjoin->sector == 0 && top.adjoin->surface == floor,
        "mirror floor: the floor and the copy's top are one portal");
    check(walkOn.adjoinFlags == SAF_Visible && (walkOn.faceFlags & FF_Transluent) != 0
            && (walkOn.surfaceFlags & SF_Floor) != 0 && (walkOn.surfaceFlags & SF_Collision) != 0 && walkOn.geo == 4,
        "mirror floor: the floor is a visible, translucent, walk-on portal that blocks movement");
    check(top.geo == 0 && top.material.empty() && top.adjoinFlags == SAF_Visible,
        "mirror floor: the copy's top is an undrawn portal face");
    std::size_t mirrorAdjoins = 0;
    for ( const Surface& surface : mirror.surfaces ) mirrorAdjoins += surface.adjoin ? 1 : 0;
    check(mirrorAdjoins == 1, "mirror floor: the copy adjoins nothing but the floor");

    // The second mirror on the same floor is refused: it is a portal now.
    check(!levelAddMirrorFloor(level, { 0, floor }, options, &report) && report.error == MirrorFloorError::Adjoined
            && level.sectors.size() == 2,
        "mirror floor: a floor that is already a portal is refused");

    const auto found = levelFindMirrorFloors(level);
    check(found.size() == 1 && found[0].floor.sector == 0 && found[0].floor.surface == floor
            && found[0].mirror.sector == 1 && found[0].mirror.surface == floor,
        "mirror floor: the mirror is found again by geometry");

    // A new bake changes the room; the refresh copies it across again.
    paintCorners(level, 0, 0.2f);
    check(levelRefreshMirrorFloors(level, 0.25) == 1 && mirrorCornersMatch(level, 0, 1, 0.25),
        "mirror floor: refresh copies the room's new light at the given reflectivity");
    check(levelRefreshMirrorFloors(level, -0.1) == 0 && mirrorCornersMatch(level, 0, 1, 0.25),
        "mirror floor: refresh with an invalid reflectivity changes nothing");

    // A room edited after its mirror was built no longer matches it.
    level.sectors[0].vertices[0].x += 0.01;
    check(levelFindMirrorFloors(level).empty() && levelRefreshMirrorFloors(level, 0.5) == 0,
        "mirror floor: an edited room's stale mirror is not found or refreshed");
}

void runDoorwayTests()
{
    // Room A's east wall is cut in two; its southern half is a doorway into
    // room B. In the copy that doorway becomes wall, textured as the
    // continuation of the northern half beside it.
    Level level;
    room(level, { 0, 0, 0 }, { 2, 2, 1 });
    const std::int32_t east = faceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0; });
    check(east >= 0 && levelCleaveSurface(level, { 0, east }, TVector{ 0, 1, 0 }, 2, 1, 0),
        "mirror doorway: fixture cuts the east wall");
    room(level, { 2, 0, 0 }, { 3, 1, 1 });
    const std::int32_t door = faceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0 && v.y <= 1.0; });
    const std::int32_t beside = faceWhere(level, 0, [](const Vertex& v) { return v.x == 2.0 && v.y >= 1.0; });
    const std::int32_t west = faceWhere(level, 1, [](const Vertex& v) { return v.x == 2.0; });
    check(door >= 0 && beside >= 0 && west >= 0 && door != beside, "mirror doorway: fixture has both halves and room B's face");
    if ( door < 0 || beside < 0 || west < 0 ) return;
    // The halves of one cut face share one continuous mapping, so the door's
    // own coordinates before it became a portal are what the continuation
    // must reproduce.
    const std::vector<TexVertex> doorUv = level.sectors[0].surfaces[static_cast<std::size_t>(door)].texVertices;
    check(levelSysAdjoinSurfaces(level, { 0, door }, { 1, west }, true), "mirror doorway: fixture adjoins the doorway");
    const std::int32_t floor = faceWhere(level, 0, [](const Vertex& v) { return v.z == 0.0; });
    MirrorFloorReport report{};
    const bool built = levelAddMirrorFloor(level, { 0, floor }, {}, &report);
    check(built && report.sealedOpenings.size() == 1 && report.sealedOpenings[0] == door && report.openingsContinued == 1,
        "mirror doorway: the doorway is sealed and continued from the wall beside it");
    if ( !built ) return;
    const Surface& sealedFace = level.sectors[2].surfaces[static_cast<std::size_t>(door)];
    const Surface& besideFace = level.sectors[0].surfaces[static_cast<std::size_t>(beside)];
    bool continued = !sealedFace.adjoin && sealedFace.material == besideFace.material && sealedFace.geo == besideFace.geo
        && sealedFace.texVertices.size() == doorUv.size();
    for ( std::size_t k = 0; continued && k < doorUv.size(); ++k )
    {
        const TexVertex& expected = doorUv[doorUv.size() - 1 - k];
        continued = close(sealedFace.texVertices[k].u, expected.u, 1e-4) && close(sealedFace.texVertices[k].v, expected.v, 1e-4);
    }
    check(continued, "mirror doorway: the sealed doorway takes the wall's material and its mapping, continued");
    check(levelFindMirrorFloors(level).size() == 1, "mirror doorway: the mirror with a sealed doorway is still found");
    check(level.sectors[0].surfaces[static_cast<std::size_t>(door)].adjoin
            && level.sectors[0].surfaces[static_cast<std::size_t>(door)].adjoin->sector == 1,
        "mirror doorway: the room's own doorway is untouched");
}

} // namespace

void runMirrorFloorTests()
{
    runSingleRoomTests();
    runDoorwayTests();
}

} // namespace sed::tests
