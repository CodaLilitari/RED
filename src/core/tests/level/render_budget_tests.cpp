#include "test_common.hpp"

#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/render_budget.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

std::int32_t faceToward(const Sector& sector, const TVector& inwardNormal)
{
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        if ( sector.surfaces[i].normal.dot(inwardNormal) > 0.999 )
        {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

// `count` unit rooms in a row along +Y, each open to the next.
Level corridor(const std::size_t count)
{
    Level level{};
    level.kind = ProjectType::JKDF2;
    for ( std::size_t i = 0; i < count; ++i )
    {
        const double y = static_cast<double>(i);
        (void)level::levelCreateSectorPrimitiveBox(level, level::SectorPrimitive::Cube, TVector::Create(0, y, 0),
            TVector::Create(1, y + 1, 1));
    }
    for ( std::size_t i = 0; i + 1 < count; ++i )
    {
        const auto near = static_cast<std::int32_t>(i);
        const std::int32_t out = faceToward(level.sectors[i], TVector::Create(0, -1, 0));
        const std::int32_t in = faceToward(level.sectors[i + 1], TVector::Create(0, 1, 0));
        check(out >= 0 && in >= 0 && level::levelSysAdjoinSurfaces(level, SurfaceRef{ near, out }, SurfaceRef{ near + 1, in }, true),
            "test setup joins the corridor's rooms");
        for ( const SurfaceRef ref : { SurfaceRef{ near, out }, SurfaceRef{ near + 1, in } } )
        {
            Surface& surface = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
            surface.geo = 0;
            surface.adjoinFlags = 7;
            surface.faceFlags = 0;
        }
    }
    return level;
}

Surface& portal(Level& level, const std::size_t room, const double towardY)
{
    Sector& sector = level.sectors[room];
    return sector.surfaces[static_cast<std::size_t>(faceToward(sector, TVector::Create(0, -towardY, 0)))];
}

void testFrameFollowsOpenPortals()
{
    Level level = corridor(6);
    const TVector eye = TVector::Create(0.5, 0.5, 0.5);
    const auto ahead = level::traceRenderFrame(level, 0, eye, 0.0, 0.0);
    check(ahead.complete && ahead.sectors == std::vector<std::int32_t>({ 0, 1, 2, 3, 4, 5 }) && ahead.alphaSurfaces == 0,
        "a frame along the corridor lists every room in the order the portals reach them");
    const auto behind = level::traceRenderFrame(level, 0, eye, 180.0, 0.0);
    check(behind.sectors == std::vector<std::int32_t>{ 0 }, "a frame facing the end wall lists only the camera's room");
    const auto up = level::traceRenderFrame(level, 0, eye, 0.0, 89.0);
    check(up.sectors == std::vector<std::int32_t>{ 0 }, "pitched at the ceiling, the portal is outside the view");
    const auto middle = level::traceRenderFrame(level, 3, TVector::Create(0.5, 3.5, 0.5), 180.0, 0.0);
    check(middle.sectors == std::vector<std::int32_t>({ 3, 2, 1, 0 }),
        "a frame looking back lists the rooms behind it and none ahead of it");

    // A drawn, opaque portal face is a wall to the renderer.
    portal(level, 2, 1.0).geo = 4;
    portal(level, 2, 1.0).material = "wall.mat";
    const auto blocked = level::traceRenderFrame(level, 0, eye, 0.0, 0.0);
    check(blocked.sectors == std::vector<std::int32_t>({ 0, 1, 2 }), "a drawn opaque portal face ends the walk");
    // The same face marked translucent is seen through, and queued as a translucent face.
    portal(level, 2, 1.0).faceFlags = 2;
    const auto glass = level::traceRenderFrame(level, 0, eye, 0.0, 0.0);
    check(glass.sectors.size() == 6 && glass.alphaSurfaces == 1,
        "a translucent portal face is seen through and counted once, from the side that faces the camera");
    // Without the adjoin's visible flag nothing is seen through it.
    portal(level, 2, 1.0).adjoinFlags = 6;
    check(level::traceRenderFrame(level, 0, eye, 0.0, 0.0).sectors.size() == 3,
        "a portal without the visible adjoin flag ends the walk");
    // Standing in the portal, the renderer passes the view on whatever the face is.
    const auto atPortal = level::traceRenderFrame(level, 2, TVector::Create(0.5, 2.995, 0.5), 0.0, 0.0);
    check(atPortal.sectors.size() == 4 && atPortal.sectors[1] == 3,
        "a portal that crosses the near plane is entered even when it is not see-through");
}

void testFrameRefusesBadInput()
{
    const Level level = corridor(2);
    const TVector eye = TVector::Create(0.5, 0.5, 0.5);
    check(!level::traceRenderFrame(level, 5, eye, 0.0, 0.0).complete
            && !level::traceRenderFrame(level, -1, eye, 0.0, 0.0).complete
            && level::traceRenderFrame(level, 5, eye, 0.0, 0.0).sectors.empty(),
        "a frame from a sector that does not exist lists nothing and is not complete");
    level::RenderCamera wide{};
    wide.horizontalFovDegrees = 0.0;
    check(!level::traceRenderFrame(level, 0, eye, 0.0, 0.0, wide).complete, "a frame needs a usable field of view");
    const auto starved = level::traceRenderFrame(corridor(8), 0, eye, 0.0, 0.0, {}, 3);
    check(!starved.complete && starved.sectors.size() == 4, "the work limit ends a walk early and says so");
}

void testAuditFindsFramesOverTheRetailLimit()
{
    const Level level = corridor(140);
    const auto viewpoints = level::sectorCentreViewpoints(level);
    check(viewpoints.size() == 140 && viewpoints[7].sector == 7 && viewpoints[7].position.y == 7.5,
        "there is a viewpoint in the middle of every room");
    check(level::sectorContainingPoint(level, TVector::Create(0.5, 61.5, 0.5)) == 61
            && !level::sectorContainingPoint(level, TVector::Create(5.0, 61.5, 0.5)),
        "a point is placed in the room whose faces all look at it");
    level::RenderBudgetOptions options{};
    options.yawSteps = 4;
    options.pitchDegrees = { 0.0 };
    const auto report = level::auditRenderBudget(level, viewpoints, options);
    check(report.complete && report.viewpoints == 140 && report.frames == 560 && report.views.size() == 140,
        "the audit traces every heading from every viewpoint");
    check(report.views.front().visibleSectors == 140 && report.views.front().sector == 0
            && report.views.front().yawDegrees == 0.0 && report.views[1].visibleSectors == 140
            && report.views[1].sector == 139 && report.views[1].yawDegrees == 180.0,
        "the heaviest frames come first: the corridor seen from either end");
    // A room sees itself and the longer run to one end: more than 128 for the twelve rooms nearest each end.
    check(report.overSectorLimit == 24 && report.overAlphaLimit == 0 && level::kRetailVisibleSectors == 128
            && level::kRetailAlphaSurfaces == 32,
        "the audit counts the viewpoints with a frame over the retail renderer's 128 sectors");
    options.workLimit = 500;
    const auto stopped = level::auditRenderBudget(level, viewpoints, options);
    check(!stopped.complete && stopped.viewpoints < 140 && stopped.viewpoints > 0,
        "an audit that runs out of work stops and says it is incomplete");
    options.yawSteps = 0;
    check(!level::auditRenderBudget(level, viewpoints, options).complete, "an audit needs at least one heading");
}

} // namespace

void runRenderBudgetTests()
{
    testFrameFollowsOpenPortals();
    testFrameRefusesBadInput();
    testAuditFindsFramesOverTheRetailLimit();
}

} // namespace sed::tests
