#include "test_common.hpp"

#include "sed/level/geometry.hpp"

#include <cmath>
#include <numbers>

namespace sed::tests
{
namespace
{

Surface surfaceWith(const std::vector<std::int32_t>& vertices, const TVector& normal)
{
    Surface surface{};
    surface.vertices = vertices;
    surface.texVertices = std::vector<TexVertex>(vertices.size());
    surface.normal = normal;
    return surface;
}

// Sector 0 holds a floor on z = 0 and a second surface whose normal is
// rotated about +X by the requested opening angle.
Level hingedLevel(const double openingDegrees)
{
    Level level{};
    (void)level.newSector();
    Sector& sector = level.sectors[0];
    const double radians = openingDegrees * std::numbers::pi / 180.0;
    sector.vertices = {
        { 0.0, 0.0, 0.0, 0 },
        { 2.0, 0.0, 0.0, 0 },
        { 2.0, 2.0, 0.0, 0 },
        { 0.0, 2.0, 0.0, 0 },
        // Second surface: a hinge at y = 2 rising away from the floor.
        { 0.0, 2.0, 1.0, 0 },
        { 2.0, 2.0, 1.0, 0 },
    };
    sector.surfaces.push_back(surfaceWith({ 0, 1, 2, 3 }, TVector::up));
    const TVector tilted = TVector::Create(0.0, -std::sin(radians), std::cos(radians));
    sector.surfaces.push_back(surfaceWith({ 4, 5, 2, 3 }, tilted));
    return level;
}

double planeDistance(const Level& level, const SurfaceRef ref, const TVector& point)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(ref.sector)];
    const Surface& surface = sector.surfaces[static_cast<std::size_t>(ref.surface)];
    const Vertex& v = sector.vertices[static_cast<std::size_t>(surface.vertices[0])];
    return surface.normal.dot(point.x - v.x, point.y - v.y, point.z - v.z);
}

void testAngleAndGridForPerpendicularSurfaces()
{
    const Level level = hingedLevel(90.0);
    const std::optional<level::SurfaceAngleGrid> grid =
        level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 });
    check(grid.has_value(), "surfaceAngle.perpendicular.valid");
    if ( !grid )
    {
        return;
    }
    check(close(grid->angleDegrees, 90.0, 1e-9), "surfaceAngle.perpendicular.angle90");
    check(close(grid->normal.length(), 1.0, 1e-12), "surfaceAngle.perpendicular.normalUnit");
    check(close(grid->xAxis.length(), 1.0, 1e-12), "surfaceAngle.perpendicular.xAxisUnit");
    check(close(grid->normal.dot(grid->xAxis), 0.0, 1e-12),
        "surfaceAngle.perpendicular.axesOrthogonal");
    // The grid normal is the hinge direction, so it lies in both planes.
    check(close(std::abs(grid->normal.x), 1.0, 1e-12), "surfaceAngle.perpendicular.normalAlongHinge");
    check(close(planeDistance(level, SurfaceRef{ 0, 0 }, grid->origin), 0.0, 1e-9)
            && close(planeDistance(level, SurfaceRef{ 0, 1 }, grid->origin), 0.0, 1e-9),
        "surfaceAngle.perpendicular.originOnHingeLine");
}

void testLegacyAngleConventionForObliqueSurfaces()
{
    // Legacy SED reports 180 - acos(n1 . n2): the angle between the faces.
    for ( const double opening : { 30.0, 60.0, 135.0 } )
    {
        const Level level = hingedLevel(opening);
        const std::optional<level::SurfaceAngleGrid> grid =
            level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 });
        check(grid && close(grid->angleDegrees, 180.0 - opening, 1e-9),
            "surfaceAngle.oblique.legacyAngle");
        check(grid
                && close(planeDistance(level, SurfaceRef{ 0, 0 }, grid->origin), 0.0, 1e-9)
                && close(planeDistance(level, SurfaceRef{ 0, 1 }, grid->origin), 0.0, 1e-9),
            "surfaceAngle.oblique.originOnBothPlanes");
        check(grid && close(grid->xAxis.dot(level.sectors[0].surfaces[1].normal), 0.0, 1e-12),
            "surfaceAngle.oblique.xAxisLiesInSecondSurface");
    }
}

void testParallelSurfacesAreRejected()
{
    Level level = hingedLevel(90.0);
    level.sectors[0].surfaces[1].normal = TVector::up;
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.coplanar.rejected");
    level.sectors[0].surfaces[1].normal = TVector::down;
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.antiparallel.rejected");
}

void testInvalidInputIsRejected()
{
    Level level = hingedLevel(90.0);
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 0 }).has_value(),
        "surfaceAngle.sameSurface.rejected");
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 9 }).has_value(),
        "surfaceAngle.badSurfaceIndex.rejected");
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 3, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.badSectorIndex.rejected");
    level.sectors[0].surfaces[1].normal = TVector::zero;
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.zeroNormal.rejected");
    level = hingedLevel(90.0);
    level.sectors[0].surfaces[0].vertices.clear();
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.emptySurface.rejected");
    level = hingedLevel(90.0);
    level.sectors[0].surfaces[1].vertices[0] = 42;
    check(!level::levelSurfaceAngleGrid(level, SurfaceRef{ 0, 0 }, SurfaceRef{ 0, 1 }).has_value(),
        "surfaceAngle.badVertexIndex.rejected");
}

} // namespace

void runSurfaceAngleTests()
{
    testAngleAndGridForPerpendicularSurfaces();
    testLegacyAngleConventionForObliqueSurfaces();
    testParallelSurfacesAreRejected();
    testInvalidInputIsRejected();
}

} // namespace sed::tests
