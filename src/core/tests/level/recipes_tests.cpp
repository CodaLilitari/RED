#include "test_common.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/recipes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sed::tests
{
namespace
{

std::int32_t addBox(Level& level, const TVector& minCorner, const TVector& maxCorner)
{
    return level::levelCreateSectorPrimitiveBox(level, level::SectorPrimitive::Cube, minCorner, maxCorner);
}

// Sector surface normals face into the sector.
std::int32_t surfaceFacing(const Sector& sector, const TVector& inwardNormal)
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

std::size_t adjoinsBetween(const Level& level, const std::int32_t a, const std::int32_t b)
{
    std::size_t count = 0;
    for ( const Surface& surface : level.sectors[static_cast<std::size_t>(a)].surfaces )
    {
        if ( surface.adjoin && surface.adjoin->sector == b )
        {
            const Surface& other = level.sectors[static_cast<std::size_t>(b)]
                                       .surfaces[static_cast<std::size_t>(surface.adjoin->surface)];
            if ( other.adjoin && other.adjoin->sector == a )
            {
                ++count;
            }
        }
    }
    return count;
}

double minimumZ(const Sector& sector)
{
    double z = sector.vertices.front().z;
    for ( const Vertex& vertex : sector.vertices )
    {
        z = std::min(z, vertex.z);
    }
    return z;
}

bool sameGeometry(const Sector& a, const Sector& b)
{
    if ( a.surfaces.size() != b.surfaces.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < a.surfaces.size(); ++i )
    {
        const Surface& sa = a.surfaces[i];
        const Surface& sb = b.surfaces[i];
        if ( sa.vertices.size() != sb.vertices.size() || sa.texVertices.size() != sb.texVertices.size() )
        {
            return false;
        }
        for ( std::size_t v = 0; v < sa.vertices.size(); ++v )
        {
            const Vertex& va = a.vertices[static_cast<std::size_t>(sa.vertices[v])];
            const Vertex& vb = b.vertices[static_cast<std::size_t>(sb.vertices[v])];
            if ( va.x != vb.x || va.y != vb.y || va.z != vb.z
                || sa.texVertices[v].u != sb.texVertices[v].u || sa.texVertices[v].v != sb.texVertices[v].v )
            {
                return false;
            }
        }
    }
    return true;
}

void testRemoveRedundantVertices()
{
    Level level{};
    const std::int32_t box = addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const Sector original = level.sectors[0];
    const std::array<std::int32_t, 3> selection{ box, 7, -1 };

    check(level::levelRemoveRedundantVertices(level, selection).removed() == 0u
            && level.sectors[0].vertices.size() == 8u,
        "Remove Redundant Vertices leaves a clean box unchanged");

    Sector& sector = level.sectors[0];
    (void)sector.newVertex(5.0, 5.0, 5.0);
    const Vertex first = sector.vertices[0];
    sector.vertices.push_back(Vertex{ first.x, first.y, first.z, 0 });
    const std::int32_t duplicate = static_cast<std::int32_t>(sector.vertices.size() - 1);
    for ( Surface& surface : sector.surfaces )
    {
        for ( std::int32_t& index : surface.vertices )
        {
            if ( index == 0 )
            {
                index = duplicate;
                break;
            }
        }
        if ( std::find(surface.vertices.begin(), surface.vertices.end(), duplicate) != surface.vertices.end() )
        {
            break;
        }
    }
    const std::int32_t top = surfaceFacing(sector, TVector::Create(0, 0, -1));
    check(level::levelCleaveEdge(level, SurfaceRef{ 0, top }, 0, TVector::Create(1, 0, 0), 0.5, 0.0, 0.0)
            || level::levelCleaveEdge(level, SurfaceRef{ 0, top }, 0, TVector::Create(0, 1, 0), 0.0, 0.5, 0.0),
        "test setup splits a box edge");
    check(level.sectors[0].vertices.size() == 11u, "test setup adds unused, duplicate, and collinear vertices");

    const level::VertexCleanupResult result = level::levelRemoveRedundantVertices(level, selection);
    check(result.mergedDuplicates == 1u && result.removedUnused == 1u && result.removedCollinear == 1u
            && result.skippedSectors == 0u,
        "Remove Redundant Vertices counts one merged, one unused, and one collinear vertex");
    check(level.sectors[0].vertices.size() == 8u && sameGeometry(level.sectors[0], original)
            && level::sectorIsConvex(level.sectors[0]),
        "Remove Redundant Vertices restores the box's corners, surfaces, and UVs");
}

void testRemoveRedundantVerticesSkipsCollapsingMerge()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    Sector& sector = level.sectors[0];
    const Surface& face = sector.surfaces[0];
    const Vertex a = sector.vertices[static_cast<std::size_t>(face.vertices[0])];
    // Move the next corner onto the first: merging would collapse the face's edge.
    sector.vertices[static_cast<std::size_t>(face.vertices[1])] = a;
    sector.vertices[static_cast<std::size_t>(face.vertices[2])] = a;
    const Level before = level;
    const std::array<std::int32_t, 1> selection{ 0 };
    const level::VertexCleanupResult result = level::levelRemoveRedundantVertices(level, selection);
    check(result.skippedSectors == 1u && result.removed() == 0u
            && level.sectors[0].vertices.size() == before.sectors[0].vertices.size(),
        "Remove Redundant Vertices skips a sector whose merge would collapse a polygon");
}

void testPlaceSectorLights()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(2, 2, 2));
    (void)addBox(level, TVector::Create(4, 0, 0), TVector::Create(5, 1, 3));
    level.sectors[1].layer = 2;
    const std::array<std::int32_t, 4> selection{ 0, 1, 1, 9 };
    const std::vector<std::int32_t> lights = level::levelPlaceSectorLights(level, selection, 7.5);
    check(lights.size() == 2u && level.lights.size() == 2u, "Place Sector Lights adds one light per valid sector");
    if ( level.lights.size() != 2u )
    {
        return;
    }
    const SedLight& first = level.lights[0];
    check(std::abs(first.position.x - 1.0) < 1e-9 && std::abs(first.position.y - 1.0) < 1e-9
            && std::abs(first.position.z - 1.0) < 1e-9,
        "Place Sector Lights centres the light in the sector");
    check(std::abs(first.range - std::sqrt(3.0)) < 1e-9 && first.intensity == 7.5,
        "Place Sector Lights reaches the farthest corner at the requested intensity");
    const SedLight& second = level.lights[1];
    check(second.layer == 2 && std::abs(second.position.z - 1.5) < 1e-9,
        "Place Sector Lights uses the sector's layer and centroid");
}

void testBuildStairs()
{
    Level level{};
    level::StairParameters stairs{};
    stairs.origin = TVector::Create(0, 0, 0);
    stairs.direction = level::StairDirection::PositiveX;
    stairs.width = 1.0;
    stairs.run = 0.5;
    stairs.rise = 0.25;
    stairs.headroom = 1.0;
    stairs.steps = 4;
    const auto built = level::levelBuildStairs(level, stairs);
    check(built && built->size() == 4u && level.sectors.size() == 4u, "Build Stairs adds one sector per step");
    if ( !built || built->size() != 4u )
    {
        return;
    }
    bool convex = true;
    bool climbing = true;
    bool connected = true;
    for ( std::size_t i = 0; i < built->size(); ++i )
    {
        const Sector& step = level.sectors[static_cast<std::size_t>((*built)[i])];
        convex = convex && level::sectorIsConvex(step);
        climbing = climbing && std::abs(minimumZ(step) - 0.25 * static_cast<double>(i)) < 1e-9;
        if ( i + 1 < built->size() )
        {
            connected = connected && adjoinsBetween(level, (*built)[i], (*built)[i + 1]) == 1u;
        }
    }
    check(convex, "Build Stairs keeps every step convex");
    check(climbing, "Build Stairs raises each step by the rise");
    check(connected, "Build Stairs adjoins each step to the next");
    check(adjoinsBetween(level, (*built)[0], (*built)[2]) == 0u, "Build Stairs adjoins only neighbouring steps");

    Level down{};
    stairs.direction = level::StairDirection::NegativeY;
    stairs.steps = 2;
    const auto south = level::levelBuildStairs(down, stairs);
    check(south && south->size() == 2u && level::sectorCalcCenter(down.sectors[1]).y < -0.5,
        "Build Stairs climbs along -Y");
}

void testBuildStairsRefusesInvalidParameters()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = level.sectors.size();
    level::StairParameters stairs{};
    stairs.steps = 0;
    check(!level::levelBuildStairs(level, stairs), "Build Stairs refuses zero steps");
    stairs.steps = level::kMaximumStairSteps + 1;
    check(!level::levelBuildStairs(level, stairs), "Build Stairs refuses too many steps");
    stairs.steps = 3;
    stairs.headroom = stairs.rise;
    check(!level::levelBuildStairs(level, stairs), "Build Stairs refuses headroom no taller than the rise");
    stairs.headroom = 1.0;
    stairs.rise = std::nan("");
    check(!level::levelBuildStairs(level, stairs), "Build Stairs refuses a non-finite rise");
    stairs.rise = 0.1;
    stairs.width = -1.0;
    check(!level::levelBuildStairs(level, stairs) && level.sectors.size() == before,
        "Build Stairs refuses a negative width and leaves the level unchanged");
}

void testFitAndAdjoinSurfaces()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1.1, 0.05, 0), TVector::Create(2, 1.05, 1));
    const std::int32_t anchor = surfaceFacing(level.sectors[0], TVector::Create(-1, 0, 0));
    const std::int32_t fit = surfaceFacing(level.sectors[1], TVector::Create(1, 0, 0));
    check(anchor >= 0 && fit >= 0, "test setup finds facing walls");

    check(!level::levelFitAndAdjoinSurfaces(level, SurfaceRef{ 0, anchor }, SurfaceRef{ 0, 0 }),
        "Fit and Adjoin refuses two surfaces of one sector");

    check(level::levelFitAndAdjoinSurfaces(level, SurfaceRef{ 0, anchor }, SurfaceRef{ 1, fit }),
        "Fit and Adjoin fits two facing walls");
    const Surface& a = level.sectors[0].surfaces[static_cast<std::size_t>(anchor)];
    const Surface& b = level.sectors[1].surfaces[static_cast<std::size_t>(fit)];
    check(a.adjoin == SurfaceRef{ 1, fit } && b.adjoin == SurfaceRef{ 0, anchor },
        "Fit and Adjoin creates a reciprocal adjoin");
    bool moved = true;
    for ( const std::int32_t index : b.vertices )
    {
        const Vertex& vertex = level.sectors[1].vertices[static_cast<std::size_t>(index)];
        moved = moved && std::abs(vertex.x - 1.0) < 1e-9 && (std::abs(vertex.y) < 1e-9 || std::abs(vertex.y - 1.0) < 1e-9);
    }
    check(moved && level::sectorIsConvex(level.sectors[1]), "Fit and Adjoin moves the fitted corners onto the anchor");
    check(level::surfaceDoOverlap(level.sectors[0], a, level.sectors[1], b), "Fit and Adjoin leaves coincident faces");

    check(!level::levelFitAndAdjoinSurfaces(level, SurfaceRef{ 0, anchor }, SurfaceRef{ 1, fit }),
        "Fit and Adjoin refuses surfaces that are already adjoined");
}

void testFitAndAdjoinRefusesMismatchAndConcaveResult()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1.5, 0, 0), TVector::Create(2, 1, 1));
    const std::int32_t anchor = surfaceFacing(level.sectors[0], TVector::Create(-1, 0, 0));
    const std::int32_t wrongWay = surfaceFacing(level.sectors[1], TVector::Create(-1, 0, 0));
    const Level before = level;
    // Folding the far wall back onto x = 1 would turn the sector inside out.
    check(!level::levelFitAndAdjoinSurfaces(level, SurfaceRef{ 0, anchor }, SurfaceRef{ 1, wrongWay })
            && sameGeometry(level.sectors[1], before.sectors[1]) && !level.sectors[0].surfaces[static_cast<std::size_t>(anchor)].adjoin,
        "Fit and Adjoin refuses a fit that breaks convexity and leaves the level unchanged");

    const std::int32_t fit = surfaceFacing(level.sectors[1], TVector::Create(1, 0, 0));
    const std::int32_t top = surfaceFacing(level.sectors[1], TVector::Create(0, 0, -1));
    (void)top;
    check(level::levelCleaveEdge(level, SurfaceRef{ 1, fit }, 0, TVector::Create(0, 0, 1), 0.0, 0.0, 0.5)
            || level::levelCleaveEdge(level, SurfaceRef{ 1, fit }, 1, TVector::Create(0, 0, 1), 0.0, 0.0, 0.5)
            || level::levelCleaveEdge(level, SurfaceRef{ 1, fit }, 0, TVector::Create(0, 1, 0), 0.0, 0.5, 0.0),
        "test setup adds a corner to the fitted wall");
    check(!level::levelFitAndAdjoinSurfaces(level, SurfaceRef{ 0, anchor }, SurfaceRef{ 1, fit }),
        "Fit and Adjoin refuses surfaces with different corner counts");
}

void testMakeUnderwaterSectors()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    level.sectors[0].flags = SECF_CollideBox;
    level::UnderwaterSettings settings{};
    settings.tint = ColorF{ 0.1f, 0.3f, 0.5f };
    settings.sound = "underwater01.wav";
    settings.soundVolume = 0.75;
    const std::array<std::int32_t, 3> selection{ 0, 0, 4 };
    check(level::levelMakeUnderwaterSectors(level, selection, settings) == 1u,
        "Make Underwater changes each valid sector once");
    const Sector& sector = level.sectors[0];
    check(sector.flags == (SECF_CollideBox | SECF_Underwater), "Make Underwater sets the flag and keeps other flags");
    check(sector.tint.r == 0.1f && sector.tint.g == 0.3f && sector.tint.b == 0.5f && sector.sound == "underwater01.wav"
            && sector.soundVolume == 0.75,
        "Make Underwater applies the tint and ambient sound");
    check(level::levelMakeUnderwaterSectors(level, selection, settings) == 0u,
        "Make Underwater reports no change when the sector already matches");
}

} // namespace

void runRecipeTests()
{
    testRemoveRedundantVertices();
    testRemoveRedundantVerticesSkipsCollapsingMerge();
    testPlaceSectorLights();
    testBuildStairs();
    testBuildStairsRefusesInvalidParameters();
    testFitAndAdjoinSurfaces();
    testFitAndAdjoinRefusesMismatchAndConcaveResult();
    testMakeUnderwaterSectors();
}

} // namespace sed::tests
