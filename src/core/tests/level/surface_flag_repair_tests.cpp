#include "test_common.hpp"

#include "sed/level/surface_flag_repair.hpp"

#include <array>
#include <cstdint>

namespace sed::tests
{
namespace
{

constexpr std::int32_t kUnrelatedSurfaceFlag = 0x40;
constexpr std::int32_t kUnrelatedAdjoinFlags = SAF_AllowSoundPass | SAF_BlockLight;

// Sector 0: an unadjoined floor (+Z), an unadjoined wall (+X), and a portal
// adjoined to sector 1's portal. Every face starts with deliberately broken
// flags so each repair has something to fix.
Level brokenLevel()
{
    Level level{};
    (void)level.newSector();
    (void)level.newSector();
    for ( Sector& sector : level.sectors )
    {
        sector.vertices = { { 0.0, 0.0, 0.0, 0 }, { 1.0, 0.0, 0.0, 0 }, { 1.0, 1.0, 0.0, 0 } };
    }
    const auto addSurface = [](Sector& sector, const TVector& normal)
    {
        Surface surface{};
        surface.vertices = { 0, 1, 2 };
        surface.texVertices = std::vector<TexVertex>(3);
        surface.normal = normal;
        surface.geo = static_cast<std::int32_t>(TGeoMode::Solid);
        surface.surfaceFlags = kUnrelatedSurfaceFlag;
        surface.adjoinFlags = SAF_NoAiMove | SAF_NoPlayerMove | kUnrelatedAdjoinFlags;
        sector.surfaces.push_back(surface);
    };
    addSurface(level.sectors[0], TVector::up);
    addSurface(level.sectors[0], TVector::Create(1.0, 0.0, 0.0));
    addSurface(level.sectors[0], TVector::Create(0.0, 1.0, 0.0));
    addSurface(level.sectors[1], TVector::Create(0.0, -1.0, 0.0));
    level.sectors[0].surfaces[2].adjoin = SurfaceRef{ 1, 0 };
    level.sectors[1].surfaces[0].adjoin = SurfaceRef{ 0, 2 };
    return level;
}

void testFixSolidSurfacesRepairsOnlyUnadjoinedFaces()
{
    Level level = brokenLevel();
    const Surface portalBefore = level.sectors[0].surfaces[2];
    const std::array<SurfaceRef, 3> refs{ SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }, SurfaceRef{ 0, 2 } };
    check(level::levelFixSolidSurfaces(level, refs) == 2u,
        "Fix solid surfaces changes the two unadjoined faces");

    const Surface& floor = level.sectors[0].surfaces[0];
    const Surface& wall = level.sectors[0].surfaces[1];
    check(floor.geo == static_cast<std::int32_t>(TGeoMode::Texture)
            && wall.geo == static_cast<std::int32_t>(TGeoMode::Texture),
        "Fix solid surfaces draws repaired faces textured");
    check((floor.surfaceFlags & SF_Collision) && (wall.surfaceFlags & SF_Collision),
        "Fix solid surfaces makes repaired faces collide");
    check((floor.surfaceFlags & SF_Floor) && !(wall.surfaceFlags & SF_Floor),
        "Fix solid surfaces marks only upward faces as floors, as legacy CheckIfFloor does");
    check((floor.surfaceFlags & kUnrelatedSurfaceFlag) && (wall.surfaceFlags & kUnrelatedSurfaceFlag),
        "Fix solid surfaces preserves unrelated surface flags");
    check(floor.adjoinFlags == portalBefore.adjoinFlags,
        "Fix solid surfaces leaves adjoin flags alone");
    const Surface& portal = level.sectors[0].surfaces[2];
    check(portal.geo == portalBefore.geo && portal.surfaceFlags == portalBefore.surfaceFlags,
        "Fix solid surfaces never turns an adjoined portal solid");

    check(level::levelFixSolidSurfaces(level, refs) == 0u,
        "Fix solid surfaces reports no change when the faces are already solid");
}

void testFixPassableAdjoinsRepairsOnlyPortals()
{
    Level level = brokenLevel();
    const Surface wallBefore = level.sectors[0].surfaces[1];
    const Surface otherSideBefore = level.sectors[1].surfaces[0];
    const std::array<SurfaceRef, 2> refs{ SurfaceRef{ 0, 1 }, SurfaceRef{ 0, 2 } };
    check(level::levelFixPassableAdjoins(level, refs) == 1u,
        "Fix passable adjoins changes only the adjoined face");

    const Surface& portal = level.sectors[0].surfaces[2];
    check(portal.geo == static_cast<std::int32_t>(TGeoMode::NotDrawn),
        "Fix passable adjoins makes the portal not drawn");
    check((portal.adjoinFlags & SAF_Visible) && (portal.adjoinFlags & SAF_Move),
        "Fix passable adjoins makes the portal visible and passable");
    check(!(portal.adjoinFlags & SAF_NoAiMove) && !(portal.adjoinFlags & SAF_NoPlayerMove),
        "Fix passable adjoins clears the AI and player movement blocks");
    check((portal.adjoinFlags & kUnrelatedAdjoinFlags) == kUnrelatedAdjoinFlags,
        "Fix passable adjoins preserves sound and light adjoin flags");
    check(portal.surfaceFlags == kUnrelatedSurfaceFlag, "Fix passable adjoins leaves surface flags alone");

    const Surface& wall = level.sectors[0].surfaces[1];
    check(wall.geo == wallBefore.geo && wall.adjoinFlags == wallBefore.adjoinFlags,
        "Fix passable adjoins ignores unadjoined faces");
    const Surface& otherSide = level.sectors[1].surfaces[0];
    check(otherSide.geo == otherSideBefore.geo && otherSide.adjoinFlags == otherSideBefore.adjoinFlags,
        "Fix passable adjoins changes only the selected side, like the legacy command");

    check(level::levelFixPassableAdjoins(level, refs) == 0u,
        "Fix passable adjoins reports no change when the portal is already passable");
}

void testFlagRepairsIgnoreInvalidReferences()
{
    Level level = brokenLevel();
    const std::array<SurfaceRef, 3> refs{ SurfaceRef{ -1, 0 }, SurfaceRef{ 5, 0 }, SurfaceRef{ 0, 9 } };
    check(level::levelFixSolidSurfaces(level, refs) == 0u
            && level::levelFixPassableAdjoins(level, refs) == 0u,
        "flag repairs skip out-of-range surface references");
}

} // namespace

void runSurfaceFlagRepairTests()
{
    testFixSolidSurfacesRepairsOnlyUnadjoinedFaces();
    testFixPassableAdjoinsRepairsOnlyPortals();
    testFlagRepairsIgnoreInvalidReferences();
}

} // namespace sed::tests
