#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/thing_frames.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
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
    stairs.width = 0.0005;
    check(!level::levelBuildStairs(level, stairs) && level.sectors.size() == before,
        "Build Stairs refuses a width below the editor's minimum without adding sectors");
    stairs.width = 100.001;
    check(!level::levelBuildStairs(level, stairs) && level.sectors.size() == before,
        "Build Stairs refuses a width above the editor's maximum without adding sectors");
    stairs.width = 1.0;
    stairs.origin = TVector::Create(2'000'000.0, 0.0, 0.0);
    check(!level::levelBuildStairs(level, stairs) && level.sectors.size() == before,
        "Build Stairs refuses an out-of-bounds origin without adding sectors");
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
    check(level::levelMakeUnderwaterSectors(level, selection, settings).sectors == 1u,
        "Make Underwater changes each valid sector once");
    const Sector& sector = level.sectors[0];
    check(sector.flags == (SECF_CollideBox | SECF_Underwater), "Make Underwater sets the flag and keeps other flags");
    check(sector.tint.r == 0.1f && sector.tint.g == 0.3f && sector.tint.b == 0.5f && sector.sound == "underwater01.wav"
            && sector.soundVolume == 0.75,
        "Make Underwater applies the tint and ambient sound");
    check(level::levelMakeUnderwaterSectors(level, selection, settings).changed() == 0u,
        "Make Underwater reports no change when the sector already matches");
}

// A pool [0,1]^3 below a dry room [0,1]^2 x [1,2], adjoined through the
// pool's ceiling. Both boundary sides start hidden and impassable.
Level poolLevel(SurfaceRef& poolSide, SurfaceRef& airSide)
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(0, 0, 1), TVector::Create(1, 1, 2));
    check(level::levelConnectSectors(level, 0, 1), "test setup adjoins the pool and the room");
    poolSide = SurfaceRef{ 0, surfaceFacing(level.sectors[0], TVector::Create(0, 0, -1)) };
    airSide = SurfaceRef{ 1, surfaceFacing(level.sectors[1], TVector::Create(0, 0, 1)) };
    for ( const SurfaceRef ref : { poolSide, airSide } )
    {
        Surface& surface = level.sectors[static_cast<std::size_t>(ref.sector)]
            .surfaces[static_cast<std::size_t>(ref.surface)];
        surface.adjoinFlags = SAF_NoAiMove;
        surface.geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
        surface.faceFlags = 0;
        surface.surfaceFlags = SF_Collision;
    }
    return level;
}

const Surface& surfaceAt(const Level& level, const SurfaceRef ref)
{
    return level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
}

void testMakeUnderwaterConfiguresWaterSurface()
{
    SurfaceRef poolSide{};
    SurfaceRef airSide{};
    Level level = poolLevel(poolSide, airSide);
    check(poolSide.surface >= 0 && airSide.surface >= 0
            && surfaceAt(level, poolSide).adjoin && *surfaceAt(level, poolSide).adjoin == airSide,
        "test setup finds the water boundary");
    level::UnderwaterSettings settings{};
    settings.waterSurfaceMaterial = "water.mat";
    const std::array<std::int32_t, 1> pool{ 0 };
    const level::UnderwaterResult result = level::levelMakeUnderwaterSectors(level, pool, settings);
    check(result.sectors == 1u && result.boundarySurfaces == 2u,
        "Make Underwater configures both sides of the water boundary");
    for ( const SurfaceRef ref : { poolSide, airSide } )
    {
        const Surface& surface = surfaceAt(level, ref);
        check(surface.adjoinFlags == (SAF_Visible | SAF_Move | SAF_AllowSoundPass | SAF_NoAiMove),
            "water boundary is visible and passable and keeps authored movement limits");
        check(surface.geo == static_cast<std::int32_t>(TGeoMode::Texture) && (surface.faceFlags & FF_Transluent) != 0
                && surface.material == "water.mat",
            "water boundary is drawn translucent with the water material");
        check((surface.surfaceFlags & SF_Collision) == 0, "water boundary is not a solid collision face");
    }
    check((level.sectors[1].flags & SECF_Underwater) == 0, "the dry room stays dry");
    check(level::levelMakeUnderwaterSectors(level, pool, settings).changed() == 0u,
        "Make Underwater is a no-op once the pool and its boundary match");

    Level both = poolLevel(poolSide, airSide);
    const std::array<std::int32_t, 2> selection{ 0, 1 };
    const level::UnderwaterResult interior = level::levelMakeUnderwaterSectors(both, selection, settings);
    check(interior.sectors == 2u && interior.boundarySurfaces == 0u
            && surfaceAt(both, poolSide).adjoinFlags == SAF_NoAiMove,
        "adjoins between two underwater sectors are not water boundaries");

    Level untouched = poolLevel(poolSide, airSide);
    settings.configureWaterSurfaces = false;
    const level::UnderwaterResult flagsOnly = level::levelMakeUnderwaterSectors(untouched, pool, settings);
    check(flagsOnly.sectors == 1u && flagsOnly.boundarySurfaces == 0u
            && surfaceAt(untouched, poolSide).geo == static_cast<std::int32_t>(TGeoMode::NotDrawn),
        "Make Underwater can leave the boundary surfaces alone");

    Level keepMaterial = poolLevel(poolSide, airSide);
    settings.configureWaterSurfaces = true;
    settings.waterSurfaceMaterial.clear();
    const std::string original = surfaceAt(keepMaterial, poolSide).material;
    (void)level::levelMakeUnderwaterSectors(keepMaterial, pool, settings);
    check(surfaceAt(keepMaterial, poolSide).material == original,
        "an empty water material keeps the boundary's material");
}

std::vector<CogValue> glassSymbols()
{
    std::vector<CogValue> symbols(4);
    symbols[0].name = "glass";
    symbols[1].name = "glass2";
    for ( std::size_t i = 0; i < 2; ++i )
    {
        symbols[i].type = ValueType::ptr;
        symbols[i].cogType = CogType::surface;
        symbols[i].intValue = -1;
        symbols[i].explicitMask = 0x448u;
    }
    symbols[2].name = "spawnpoints";
    symbols[3].name = "density";
    for ( std::size_t i = 2; i < 4; ++i )
    {
        symbols[i].type = ValueType::integer;
        symbols[i].cogType = CogType::int_;
    }
    symbols[2].intValue = 2;
    symbols[3].intValue = 5;
    return symbols;
}

// Two rooms side by side, adjoined through A's +x wall.
Level windowLevel(SurfaceRef& near, SurfaceRef& far)
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(level, 0, 1), "test setup adjoins the two rooms");
    near = SurfaceRef{ 0, surfaceFacing(level.sectors[0], TVector::Create(-1, 0, 0)) };
    far = SurfaceRef{ 1, surfaceFacing(level.sectors[1], TVector::Create(1, 0, 0)) };
    for ( const SurfaceRef ref : { near, far } )
    {
        Surface& surface = level.sectors[static_cast<std::size_t>(ref.sector)]
            .surfaces[static_cast<std::size_t>(ref.surface)];
        surface.adjoinFlags = SAF_Visible | SAF_Move | SAF_AllowSoundPass;
        surface.geo = static_cast<std::int32_t>(TGeoMode::NotDrawn);
        surface.surfaceFlags = SF_Collision;
    }
    return level;
}

void testMakeBreakableGlass()
{
    SurfaceRef near{};
    SurfaceRef far{};
    Level level = windowLevel(near, far);
    check(near.surface >= 0 && far.surface >= 0 && surfaceAt(level, near).adjoin
            && *surfaceAt(level, near).adjoin == far,
        "test setup finds the window adjoin");
    level::BreakableGlassSettings settings{};
    settings.material = "glass.mat";
    settings.density = 8;
    const std::size_t cogsBefore = level.cogs.size();
    const level::BreakableGlassResult result =
        level::levelMakeBreakableGlass(level, far, "00_breakingglass.cog", glassSymbols(), settings);
    check(result.error == level::BreakableGlassError::None && level.cogs.size() == cogsBefore + 1u
            && result.cog == level.cogs.back().num,
        "Make Breakable Glass appends one COG");
    if ( level.cogs.size() != cogsBefore + 1u )
    {
        return;
    }
    const Cog& cog = level.cogs.back();
    check(cog.name == "00_breakingglass.cog" && cog.values.size() == 4u, "the glass COG uses the script's symbols");
    check(cog.values[0].intValue == level.getGlobalSurfaceNumber(far.sector, far.surface)
            && cog.values[1].intValue == level.getGlobalSurfaceNumber(near.sector, near.surface)
            && cog.values[0].stringValue.empty() && cog.values[1].stringValue.empty(),
        "glass is the selected side and glass2 is its adjoin");
    check(cog.values[2].intValue == 2 && cog.values[3].intValue == 8,
        "the density override applies and other script defaults are kept");
    for ( const SurfaceRef ref : { near, far } )
    {
        const Surface& surface = surfaceAt(level, ref);
        check(surface.adjoinFlags == (SAF_Visible | SAF_AllowSoundPass),
            "the pane is visible but blocks movement until it breaks");
        check(surface.geo == static_cast<std::int32_t>(TGeoMode::Texture) && (surface.faceFlags & FF_Transluent) != 0
                && surface.material == "glass.mat",
            "the pane is drawn translucent with the glass material");
        check((surface.surfaceFlags & SF_Collision) == 0 && (surface.surfaceFlags & SF_CogLinked) != 0,
            "the pane is COG-linked and has no solid collision once broken");
    }

    const Level before = level;
    const level::BreakableGlassResult again =
        level::levelMakeBreakableGlass(level, near, "00_breakingglass.cog", glassSymbols(), settings);
    check(again.error == level::BreakableGlassError::AlreadyGlass && level.cogs.size() == before.cogs.size(),
        "Make Breakable Glass refuses a pane that already has the glass script");
}

void testMakeBreakableGlassRefusals()
{
    SurfaceRef near{};
    SurfaceRef far{};
    Level level = windowLevel(near, far);
    const level::BreakableGlassSettings settings{};
    const SurfaceRef wall{ 0, surfaceFacing(level.sectors[0], TVector::Create(0, 1, 0)) };
    const std::size_t cogs = level.cogs.size();
    const std::uint32_t wallFace = surfaceAt(level, wall).faceFlags;
    check(level::levelMakeBreakableGlass(level, wall, "00_breakingglass.cog", glassSymbols(), settings).error
                == level::BreakableGlassError::NotAdjoined
            && level.cogs.size() == cogs && surfaceAt(level, wall).faceFlags == wallFace,
        "Make Breakable Glass refuses an unadjoined surface");
    check(level::levelMakeBreakableGlass(level, SurfaceRef{ 7, 0 }, "00_breakingglass.cog", glassSymbols(), settings)
                .error == level::BreakableGlassError::InvalidSurface,
        "Make Breakable Glass refuses an invalid surface");
    std::vector<CogValue> oneSurface = glassSymbols();
    oneSurface.erase(oneSurface.begin() + 1);
    check(level::levelMakeBreakableGlass(level, near, "00_breakingglass.cog", oneSurface, settings).error
                == level::BreakableGlassError::MissingSymbols
            && level.cogs.size() == cogs && surfaceAt(level, near).geo == static_cast<std::int32_t>(TGeoMode::NotDrawn),
        "Make Breakable Glass refuses a script without two surface symbols");
}

void testMakeBreakableGlassRefusesInvalidSettings()
{
    SurfaceRef near{};
    SurfaceRef far{};
    Level baseline = windowLevel(near, far);
    baseline.sectors[static_cast<std::size_t>(near.sector)]
        .surfaces[static_cast<std::size_t>(near.surface)].material = "near.mat";
    baseline.sectors[static_cast<std::size_t>(far.sector)]
        .surfaces[static_cast<std::size_t>(far.surface)].material = "far.mat";
    Cog& existing = baseline.newCog();
    existing.name = "existing.cog";
    existing.values = glassSymbols();
    baseline.things.emplace_back();
    baseline.lights.emplace_back();

    level::BreakableGlassSettings valid{};
    valid.material = "replacement.mat";
    valid.density = 5;
    valid.spawnPoints = 2;
    std::array<level::BreakableGlassSettings, 4> invalid{ valid, valid, valid, valid };
    invalid[0].density = -1;
    invalid[1].density = 65;
    invalid[2].spawnPoints = -1;
    invalid[3].spawnPoints = 17;

    const auto sameValue = [](const CogValue& a, const CogValue& b)
        {
            return a.name == b.name && a.type == b.type && a.intValue == b.intValue
                && a.floatValue == b.floatValue && a.stringValue == b.stringValue
                && a.cogType == b.cogType && a.local == b.local && a.description == b.description
                && a.mask == b.mask && a.explicitLinkId == b.explicitLinkId
                && a.explicitMask == b.explicitMask && a.noLink == b.noLink;
        };
    for ( const level::BreakableGlassSettings& settings : invalid )
    {
        Level candidate = baseline;
        const level::BreakableGlassResult result = level::levelMakeBreakableGlass(
            candidate, near, "00_breakingglass.cog", glassSymbols(), settings);
        check(result.error == level::BreakableGlassError::InvalidSettings && result.cog == -1,
            "Make Breakable Glass rejects invalid density or spawn counts with no new COG");

        bool unchanged = candidate.cogs.size() == baseline.cogs.size()
            && candidate.sectors.size() == baseline.sectors.size()
            && candidate.things.size() == baseline.things.size()
            && candidate.lights.size() == baseline.lights.size()
            && candidate.curId == baseline.curId;
        if ( candidate.cogs.size() == baseline.cogs.size() )
        {
            for ( std::size_t c = 0; c < baseline.cogs.size(); ++c )
            {
                const Cog& before = baseline.cogs[c];
                const Cog& after = candidate.cogs[c];
                unchanged = unchanged && before.id == after.id && before.num == after.num
                    && before.name == after.name && before.values.size() == after.values.size();
                if ( before.values.size() != after.values.size() )
                {
                    continue;
                }
                for ( std::size_t v = 0; v < before.values.size(); ++v )
                {
                    unchanged = unchanged && sameValue(before.values[v], after.values[v]);
                }
            }
        }
        if ( candidate.sectors.size() == baseline.sectors.size() )
        {
            for ( std::size_t s = 0; s < baseline.sectors.size(); ++s )
            {
                const Sector& before = baseline.sectors[s];
                const Sector& after = candidate.sectors[s];
                unchanged = unchanged && before.vertices.size() == after.vertices.size()
                    && before.surfaces.size() == after.surfaces.size();
                if ( before.vertices.size() == after.vertices.size()
                    && before.surfaces.size() == after.surfaces.size() )
                {
                    unchanged = unchanged && sameGeometry(before, after);
                }
            }
        }
        for ( const SurfaceRef ref : { near, far } )
        {
            const Surface& before = surfaceAt(baseline, ref);
            const Surface& after = surfaceAt(candidate, ref);
            unchanged = unchanged && before.material == after.material
                && before.surfaceFlags == after.surfaceFlags && before.faceFlags == after.faceFlags
                && before.adjoinFlags == after.adjoinFlags && before.adjoin == after.adjoin
                && before.geo == after.geo && before.vertices == after.vertices
                && before.texVertices.size() == after.texVertices.size();
        }
        check(unchanged,
            "Make Breakable Glass refusal preserves existing COGs, both portal sides, geometry, and entities");
    }
}

void testMakeBreakableGlassInvalidSettingsPrecedence()
{
    SurfaceRef near{};
    SurfaceRef far{};
    Level level = windowLevel(near, far);
    level::BreakableGlassSettings invalid{};
    invalid.density = 65;
    const SurfaceRef wall{ 0, surfaceFacing(level.sectors[0], TVector::Create(0, 1, 0)) };
    check(level::levelMakeBreakableGlass(
            level, SurfaceRef{ 7, 0 }, "00_breakingglass.cog", glassSymbols(), invalid).error
            == level::BreakableGlassError::InvalidSurface,
        "invalid glass settings do not displace the invalid-surface refusal");
    check(level::levelMakeBreakableGlass(level, wall, "00_breakingglass.cog", glassSymbols(), invalid).error
            == level::BreakableGlassError::NotAdjoined,
        "invalid glass settings do not displace the unadjoined-surface refusal");
    std::vector<CogValue> oneSurface = glassSymbols();
    oneSurface.erase(oneSurface.begin() + 1);
    check(level::levelMakeBreakableGlass(level, near, "00_breakingglass.cog", oneSurface, invalid).error
            == level::BreakableGlassError::MissingSymbols && level.cogs.empty(),
        "invalid glass settings do not displace the missing-symbols refusal");

    Level existing = windowLevel(near, far);
    const level::BreakableGlassSettings valid{};
    check(level::levelMakeBreakableGlass(
            existing, near, "00_breakingglass.cog", glassSymbols(), valid).error
            == level::BreakableGlassError::None,
        "test setup creates an existing glass pane");
    check(level::levelMakeBreakableGlass(
            existing, near, "00_breakingglass.cog", glassSymbols(), invalid).error
            == level::BreakableGlassError::AlreadyGlass && existing.cogs.size() == 1u,
        "invalid glass settings do not displace the duplicate-glass refusal");

    Level exhausted = windowLevel(near, far);
    exhausted.curId = std::numeric_limits<std::int32_t>::max();
    check(level::levelMakeBreakableGlass(
            exhausted, near, "00_breakingglass.cog", glassSymbols(), invalid).error
            == level::BreakableGlassError::IdSpaceExhausted && exhausted.cogs.empty(),
        "invalid glass settings do not displace the entity-ID refusal");
}

void testPlaceSectorLightsRefusesInvalidIntensity()
{
    Level baseline{};
    (void)addBox(baseline, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    baseline.lights.emplace_back();
    const std::array<std::int32_t, 1> selection{ 0 };
    for ( const double intensity : { 0.0, 0.009, 1000.001, -1.0,
                 std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() } )
    {
        Level candidate = baseline;
        const std::vector<std::int32_t> created = level::levelPlaceSectorLights(candidate, selection, intensity);
        check(created.empty() && candidate.lights.size() == baseline.lights.size()
                && candidate.lights[0].intensity == baseline.lights[0].intensity,
            "Place Sector Lights rejects an out-of-domain intensity without adding or changing lights");
    }
}

void testMakeUnderwaterRefusesInvalidSettings()
{
    SurfaceRef poolSide{};
    SurfaceRef airSide{};
    const Level baseline = poolLevel(poolSide, airSide);
    level::UnderwaterSettings valid{};
    valid.tint = ColorF{ 0.0f, 0.3f, 0.5f };
    std::array<level::UnderwaterSettings, 5> invalid{ valid, valid, valid, valid, valid };
    invalid[0].tint.g = 1.01f;
    invalid[1].tint.r = -0.01f;
    invalid[2].tint.b = std::numeric_limits<float>::quiet_NaN();
    invalid[3].soundVolume = 1.5;
    invalid[4].soundVolume = std::numeric_limits<double>::infinity();
    const std::array<std::int32_t, 1> selection{ 0 };
    for ( const level::UnderwaterSettings& settings : invalid )
    {
        Level candidate = baseline;
        const level::UnderwaterResult result = level::levelMakeUnderwaterSectors(candidate, selection, settings);
        bool unchanged = result.changed() == 0u;
        for ( std::size_t s = 0; s < baseline.sectors.size(); ++s )
        {
            const Sector& before = baseline.sectors[s];
            const Sector& after = candidate.sectors[s];
            unchanged = unchanged && sameGeometry(before, after)
                && before.flags == after.flags && before.tint.r == after.tint.r
                && before.tint.g == after.tint.g && before.tint.b == after.tint.b
                && before.sound == after.sound && before.soundVolume == after.soundVolume;
            for ( std::size_t f = 0; f < before.surfaces.size(); ++f )
            {
                const Surface& a = before.surfaces[f];
                const Surface& b = after.surfaces[f];
                unchanged = unchanged && a.adjoinFlags == b.adjoinFlags && a.faceFlags == b.faceFlags
                    && a.surfaceFlags == b.surfaceFlags && a.geo == b.geo && a.material == b.material;
            }
        }
        check(unchanged, "Make Underwater rejects invalid tint or volume without changing sectors or boundary surfaces");
    }
}

void testMaskedSurfaceRecipe()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    Sector& sector = level.sectors[0];
    const std::int32_t floor = surfaceFacing(sector, TVector::Create(0, 0, 1));
    const std::int32_t ceiling = surfaceFacing(sector, TVector::Create(0, 0, -1));
    const std::int32_t wall = surfaceFacing(sector, TVector::Create(-1, 0, 0));
    check(floor >= 0 && ceiling >= 0 && wall >= 0, "test setup finds floor, ceiling, and wall");
    sector.surfaces[static_cast<std::size_t>(wall)].material = "sky.mat";
    for ( Surface& surface : sector.surfaces )
    {
        surface.faceFlags = 0x5u;
    }
    std::vector<SurfaceRef> all{};
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        all.push_back(SurfaceRef{ 0, static_cast<std::int32_t>(i) });
    }
    level::MaskedSurfaceRecipe recipe{};
    recipe.maxSlopeDegrees = 10.0;
    recipe.copyFrom = SurfaceRef{ 0, wall };
    recipe.copyFields = static_cast<std::uint32_t>(level::MaskedCopyField::Material);
    recipe.setFaceFlags = 0x2u;
    const level::MaskedSurfaceResult result = level::levelApplyMaskedSurfaceRecipe(level, all, recipe);
    check(result.matched == 2u && result.changed == 2u,
        "Masked recipe matches only the horizontal surfaces inside the slope band");
    check(sector.surfaces[static_cast<std::size_t>(floor)].material == "sky.mat"
            && sector.surfaces[static_cast<std::size_t>(ceiling)].material == "sky.mat"
            && sector.surfaces[static_cast<std::size_t>(wall)].material == "sky.mat",
        "Masked recipe copies the selected field to matched surfaces");
    check(sector.surfaces[static_cast<std::size_t>(floor)].faceFlags == 0x7u
            && sector.surfaces[static_cast<std::size_t>(wall)].faceFlags == 0x5u,
        "Masked recipe sets masked bits and preserves unrelated bits");
    const level::MaskedSurfaceResult again = level::levelApplyMaskedSurfaceRecipe(level, all, recipe);
    check(again.matched == 2u && again.changed == 0u, "Masked recipe is repeatable: a second run changes nothing");

    Level selected{};
    (void)addBox(selected, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    for ( Surface& surface : selected.sectors[0].surfaces )
    {
        surface.surfaceFlags = SF_Collision;
    }
    selected.sectors[0].surfaces[0].surfaceFlags = SF_Collision | SF_Floor;
    std::vector<SurfaceRef> selectedAll{};
    for ( std::size_t i = 0; i < selected.sectors[0].surfaces.size(); ++i )
    {
        selectedAll.push_back(SurfaceRef{ 0, static_cast<std::int32_t>(i) });
    }
    level::MaskedSurfaceRecipe byMask{};
    byMask.surfaceFlags = level::FlagMaskPredicate{ std::nullopt,
        static_cast<std::uint32_t>(SF_Floor), static_cast<std::uint32_t>(SF_Collision) };
    byMask.material = std::string("floor.mat");
    check(level::levelApplyMaskedSurfaceRecipe(selected, selectedAll, byMask).matched == 0u,
        "Masked recipe all/none masks combine: no surface has Floor without Collision excluded");

    Level invalid = level;
    const Level invalidBefore = invalid;
    level::MaskedSurfaceRecipe badSlope{};
    badSlope.minSlopeDegrees = 45.0;
    badSlope.maxSlopeDegrees = 10.0;
    check(level::levelApplyMaskedSurfaceRecipe(invalid, all, badSlope).changed == 0u
            && invalid.sectors[0].surfaces.size() == invalidBefore.sectors[0].surfaces.size(),
        "Masked recipe refuses an inverted slope band without changes");
}

void testBuildSwitchbackStairs()
{
    Level level{};
    level::SwitchbackStairParameters parameters{};
    parameters.origin = TVector::Create(0, 0, 0);
    parameters.direction = level::StairDirection::PositiveX;
    parameters.width = 1.0;
    parameters.run = 0.5;
    parameters.rise = 0.25;
    parameters.headroom = 1.0;
    parameters.landingRun = 0.5;
    parameters.stepsPerFlight = 2;
    const auto built = level::levelBuildSwitchbackStairs(level, parameters);
    check(built && built->size() == 5u && level.sectors.size() == 5u,
        "Switchback Stairs builds two flights plus a landing");
    if ( !built || built->size() != 5u )
    {
        return;
    }
    const double floors[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
    bool climbing = true;
    bool convex = true;
    bool connected = true;
    for ( std::size_t i = 0; i < built->size(); ++i )
    {
        const Sector& step = level.sectors[static_cast<std::size_t>((*built)[i])];
        climbing = climbing && std::abs(minimumZ(step) - floors[i]) < 1e-9;
        convex = convex && level::sectorIsConvex(step);
        if ( i + 1 < built->size() )
        {
            connected = connected && adjoinsBetween(level, (*built)[i], (*built)[i + 1]) >= 1u;
        }
    }
    check(climbing, "Switchback Stairs keeps rising across the landing");
    check(convex, "Switchback Stairs keeps every sector convex");
    check(connected, "Switchback Stairs adjoins each sector to the next");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    parameters.stepsPerFlight = 0;
    check(!level::levelBuildSwitchbackStairs(refused, parameters) && refused.sectors.size() == before,
        "Switchback Stairs refuses zero steps per flight without adding sectors");
    parameters.stepsPerFlight = 2;
    parameters.landingRun = 0.0005;
    check(!level::levelBuildSwitchbackStairs(refused, parameters) && refused.sectors.size() == before,
        "Switchback Stairs refuses a landing below the minimum size without adding sectors");
}

void testBuildSpiralStairs()
{
    Level level{};
    level::SpiralStairParameters parameters{};
    parameters.center = TVector::Create(0, 0, 0);
    parameters.innerRadius = 0.5;
    parameters.outerRadius = 1.5;
    parameters.startAngleDegrees = 0.0;
    parameters.stepAngleDegrees = 30.0;
    parameters.rise = 0.25;
    parameters.headroom = 1.0;
    parameters.steps = 4;
    const auto built = level::levelBuildSpiralStairs(level, parameters);
    check(built && built->size() == 4u && level.sectors.size() == 4u,
        "Spiral Stairs builds one wedge sector per step");
    if ( !built || built->size() != 4u )
    {
        return;
    }
    bool climbing = true;
    bool convex = true;
    bool wedgeShape = true;
    bool connected = true;
    for ( std::size_t i = 0; i < built->size(); ++i )
    {
        const Sector& step = level.sectors[static_cast<std::size_t>((*built)[i])];
        climbing = climbing && std::abs(minimumZ(step) - 0.25 * static_cast<double>(i)) < 1e-9;
        convex = convex && level::sectorIsConvex(step);
        std::set<std::tuple<double, double, double>> corners{};
        for ( const Vertex& vertex : step.vertices )
        {
            corners.emplace(vertex.x, vertex.y, vertex.z);
        }
        // Connecting a step cleaves its shared radial wall, so the wedge
        // prism's eight corners and six faces are a lower bound.
        wedgeShape = wedgeShape && corners.size() >= 8u && step.surfaces.size() >= 6u;
        if ( i + 1 < built->size() )
        {
            connected = connected && adjoinsBetween(level, (*built)[i], (*built)[i + 1]) >= 1u;
        }
    }
    check(climbing, "Spiral Stairs rises by the step rise each step");
    check(convex, "Spiral Stairs keeps every wedge sector convex");
    check(wedgeShape, "Spiral Stairs steps are eight-vertex wedge prisms");
    check(connected, "Spiral Stairs adjoins each step to the next");

    Level clockwise{};
    parameters.stepAngleDegrees = -30.0;
    const auto builtClockwise = level::levelBuildSpiralStairs(clockwise, parameters);
    check(builtClockwise && builtClockwise->size() == 4u,
        "Spiral Stairs builds clockwise with a negative step angle");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    parameters.stepAngleDegrees = 0.0;
    check(!level::levelBuildSpiralStairs(refused, parameters) && refused.sectors.size() == before,
        "Spiral Stairs refuses a zero step angle without adding sectors");
    parameters.stepAngleDegrees = 180.0;
    check(!level::levelBuildSpiralStairs(refused, parameters) && refused.sectors.size() == before,
        "Spiral Stairs refuses a half-turn step angle without adding sectors");
    parameters.stepAngleDegrees = 30.0;
    parameters.innerRadius = 1.5;
    parameters.outerRadius = 0.5;
    check(!level::levelBuildSpiralStairs(refused, parameters) && refused.sectors.size() == before,
        "Spiral Stairs refuses an inner radius beyond the outer radius");
    parameters.innerRadius = 0.5;
    parameters.outerRadius = 1.5;
    parameters.steps = 0;
    check(!level::levelBuildSpiralStairs(refused, parameters) && refused.sectors.size() == before,
        "Spiral Stairs refuses zero steps without adding sectors");
}

void testBuildDome()
{
    Level level{};
    level::DomeParameters parameters{};
    parameters.center = TVector::Create(0, 0, 0);
    parameters.radius = 1.0;
    parameters.headroom = 1.0;
    parameters.segments = 4;
    parameters.rings = 2;
    const auto built = level::levelBuildDome(level, parameters);
    check(built && built->size() == 8u && level.sectors.size() == 8u,
        "Dome builds segments x rings wedge sectors");
    if ( !built || built->size() != 8u )
    {
        return;
    }
    bool convex = true;
    bool ringFloors = true;
    bool ringAdjoined = true;
    bool stacked = true;
    const double ring1Floor = std::sin(std::acos(-1.0) / 4.0); // sin(45 deg) * radius 1
    for ( std::size_t i = 0; i < built->size(); ++i )
    {
        const Sector& sector = level.sectors[static_cast<std::size_t>((*built)[i])];
        convex = convex && level::sectorIsConvex(sector);
        const double expectedFloor = i < 4u ? 0.0 : ring1Floor;
        ringFloors = ringFloors && std::abs(minimumZ(sector) - expectedFloor) < 1e-9;
        const std::size_t ring = i / 4u;
        const std::size_t next = ring * 4u + (i + 1u) % 4u;
        ringAdjoined = ringAdjoined && adjoinsBetween(level, (*built)[i], (*built)[next]) >= 1u;
        if ( ring == 0u )
        {
            stacked = stacked && adjoinsBetween(level, (*built)[i], (*built)[i + 4u]) >= 1u;
        }
    }
    check(convex, "Dome keeps every wedge sector convex");
    check(ringFloors, "Dome rings sit on the hemisphere profile floors");
    check(ringAdjoined, "Dome adjoins each ring around, including the wrap");
    check(stacked, "Dome adjoins each upper sector to the one below it");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    parameters.segments = 2;
    check(!level::levelBuildDome(refused, parameters) && refused.sectors.size() == before,
        "Dome refuses fewer than three segments without adding sectors");
    parameters.segments = 4;
    parameters.radius = 2.0;
    check(!level::levelBuildDome(refused, parameters) && refused.sectors.size() == before,
        "Dome refuses a headroom not exceeding the ring rise without adding sectors");
    parameters.radius = 1.0;
    parameters.rings = 0;
    check(!level::levelBuildDome(refused, parameters) && refused.sectors.size() == before,
        "Dome refuses zero rings without adding sectors");
}

void testBuildSweptCorridor()
{
    Level level{};
    level::SweptCorridorParameters parameters{};
    parameters.center = TVector::Create(0, 0, 0);
    parameters.innerRadius = 1.0;
    parameters.outerRadius = 2.0;
    parameters.startAngleDegrees = 0.0;
    parameters.sweepDegrees = 90.0;
    parameters.segments = 3;
    parameters.risePerSegment = 0.0;
    parameters.headroom = 1.0;
    const auto built = level::levelBuildSweptCorridor(level, parameters);
    check(built && built->size() == 3u && level.sectors.size() == 3u,
        "Swept Corridor builds one wedge sector per segment");
    if ( !built || built->size() != 3u )
    {
        return;
    }
    bool flat = true;
    bool convex = true;
    bool connected = true;
    for ( std::size_t i = 0; i < built->size(); ++i )
    {
        const Sector& segment = level.sectors[static_cast<std::size_t>((*built)[i])];
        flat = flat && std::abs(minimumZ(segment)) < 1e-9;
        convex = convex && level::sectorIsConvex(segment);
        if ( i + 1 < built->size() )
        {
            connected = connected && adjoinsBetween(level, (*built)[i], (*built)[i + 1]) >= 1u;
        }
    }
    check(flat && convex, "Swept Corridor stays flat and convex at zero rise");
    check(connected, "Swept Corridor adjoins each segment to the next");

    Level ramp{};
    parameters.risePerSegment = 0.25;
    const auto ramped = level::levelBuildSweptCorridor(ramp, parameters);
    check(ramped && ramped->size() == 3u
            && std::abs(minimumZ(ramp.sectors[static_cast<std::size_t>((*ramped)[2])]) - 0.5) < 1e-9,
        "Swept Corridor rises by the per-segment rise");

    Level clockwise{};
    parameters.risePerSegment = 0.0;
    parameters.sweepDegrees = -90.0;
    check(level::levelBuildSweptCorridor(clockwise, parameters).has_value(),
        "Swept Corridor sweeps clockwise with a negative sweep");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    parameters.sweepDegrees = 0.0;
    check(!level::levelBuildSweptCorridor(refused, parameters) && refused.sectors.size() == before,
        "Swept Corridor refuses a zero sweep without adding sectors");
    parameters.sweepDegrees = 180.0;
    parameters.segments = 1;
    check(!level::levelBuildSweptCorridor(refused, parameters) && refused.sectors.size() == before,
        "Swept Corridor refuses a half-turn segment without adding sectors");
    parameters.sweepDegrees = 90.0;
    parameters.segments = 3;
    parameters.innerRadius = 2.0;
    parameters.outerRadius = 1.0;
    check(!level::levelBuildSweptCorridor(refused, parameters) && refused.sectors.size() == before,
        "Swept Corridor refuses an inner radius beyond the outer radius");
    parameters.innerRadius = 1.0;
    parameters.outerRadius = 2.0;
    parameters.risePerSegment = 1.0;
    parameters.headroom = 1.0;
    check(!level::levelBuildSweptCorridor(refused, parameters) && refused.sectors.size() == before,
        "Swept Corridor refuses a rise the headroom cannot span");
}

void testInsetSurfaceAndPair()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::int32_t top = surfaceFacing(level.sectors[0], TVector::Create(0, 0, -1));
    check(level::levelInsetSurface(level, SurfaceRef{ 0, top }, 0.5), "Inset scales a surface toward its centroid");
    const Surface& inset = level.sectors[0].surfaces[static_cast<std::size_t>(top)];
    bool shrunk = true;
    for ( const std::int32_t index : inset.vertices )
    {
        const Vertex& vertex = level.sectors[0].vertices[static_cast<std::size_t>(index)];
        shrunk = shrunk && vertex.x >= 0.25 - 1e-9 && vertex.x <= 0.75 + 1e-9
            && vertex.y >= 0.25 - 1e-9 && vertex.y <= 0.75 + 1e-9;
    }
    check(shrunk && level::sectorIsConvex(level.sectors[0]), "Inset shrinks the face and keeps the sector convex");
    check(!level::levelInsetSurface(level, SurfaceRef{ 0, top }, 1.0), "Inset refuses a factor of one");
    check(!level::levelInsetSurface(level, SurfaceRef{ 0, top }, 0.0), "Inset refuses a factor of zero");

    Level pair{};
    (void)addBox(pair, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(pair, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(pair, 0, 1), "test setup adjoins the pair");
    const std::int32_t nearSide = surfaceFacing(pair.sectors[0], TVector::Create(-1, 0, 0));
    const SurfaceRef nearRef{ 0, nearSide };
    check(!level::levelInsetSurface(pair, nearRef, 0.5), "Inset refuses an adjoined surface on its own");
    check(level::levelInsetAdjoinPair(pair, nearRef, 0.5), "Paired inset insets both sides of an adjoin together");
    const Surface& a = pair.sectors[0].surfaces[static_cast<std::size_t>(nearSide)];
    check(a.adjoin.has_value(), "Paired inset keeps the adjoin");
    const Surface& b = pair.sectors[1].surfaces[static_cast<std::size_t>(a.adjoin->surface)];
    bool coincident = a.vertices.size() == b.vertices.size();
    for ( std::size_t i = 0; coincident && i < a.vertices.size(); ++i )
    {
        const Vertex& va = pair.sectors[0].vertices[static_cast<std::size_t>(a.vertices[i])];
        bool found = false;
        for ( const std::int32_t index : b.vertices )
        {
            const Vertex& vb = pair.sectors[1].vertices[static_cast<std::size_t>(index)];
            found = found || (std::abs(va.x - vb.x) < 1e-9 && std::abs(va.y - vb.y) < 1e-9
                && std::abs(va.z - vb.z) < 1e-9);
        }
        coincident = found;
    }
    check(coincident, "Paired inset keeps both sides coincident");
}

void testReportAndSubdivide()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    Sector& sector = level.sectors[0];
    (void)sector.newVertex(5.0, 5.0, 5.0);
    const std::size_t verticesBefore = sector.vertices.size();
    const std::array<std::int32_t, 1> selection{ 0 };
    const std::vector<level::VertexCleanupCandidate> report =
        level::levelReportRedundantVertices(level, selection);
    check(report.size() == 1u && report[0].removedUnused == 1u && !report[0].wouldSkip,
        "Cleanup report lists candidates without applying them");
    check(level.sectors[0].vertices.size() == verticesBefore, "Cleanup report leaves the level unchanged");

    const std::int32_t top = surfaceFacing(level.sectors[0], TVector::Create(0, 0, -1));
    const std::size_t surfacesBefore = level.sectors[0].surfaces.size();
    const std::array<SurfaceRef, 1> targets{ SurfaceRef{ 0, top } };
    check(level::levelSubdivideSurfaces(level, targets) == 1u
            && level.sectors[0].surfaces.size() == surfacesBefore + 3u,
        "Subdivide fans a quad into four triangles");
    bool triangles = true;
    for ( const Surface& surface : level.sectors[0].surfaces )
    {
        triangles = triangles && surface.vertices.size() == surface.texVertices.size();
    }
    check(triangles, "Subdivide keeps corner and UV counts matched");
    check(level::levelSubdivideSurfaces(level, targets) == 1u, "Subdivide can run again on the result");
}

void testBuildHeightmapTerrain()
{
    Level level{};
    level::TerrainParameters parameters{};
    parameters.origin = TVector::Create(0, 0, 0);
    parameters.cellSize = 1.0;
    parameters.headroom = 1.0;
    parameters.columns = 2;
    parameters.rows = 2;
    parameters.heights = { 0.0, 0.0, 0.0, 0.0 };
    const auto built = level::levelBuildHeightmapTerrain(level, parameters);
    check(built && built->size() == 4u && level.sectors.size() == 4u,
        "Heightmap Terrain builds one sector per cell");
    if (!built || built->size() != 4u)
    {
        return;
    }
    bool flat = true;
    bool convex = true;
    for (const std::int32_t index : *built)
    {
        const Sector& cell = level.sectors[static_cast<std::size_t>(index)];
        flat = flat && std::abs(minimumZ(cell)) < 1e-9;
        convex = convex && level::sectorIsConvex(cell);
    }
    check(flat && convex, "Heightmap Terrain cells sit on their heights and stay convex");
    check(adjoinsBetween(level, (*built)[0], (*built)[1]) >= 1u
            && adjoinsBetween(level, (*built)[0], (*built)[2]) >= 1u,
        "Heightmap Terrain adjoins neighbouring cells");

    Level slope{};
    parameters.columns = 2;
    parameters.rows = 1;
    parameters.heights = { 0.0, 0.25 };
    const auto stepped = level::levelBuildHeightmapTerrain(slope, parameters);
    check(stepped && stepped->size() == 2u
            && std::abs(minimumZ(slope.sectors[1]) - 0.25) < 1e-9
            && adjoinsBetween(slope, (*stepped)[0], (*stepped)[1]) >= 1u,
        "Heightmap Terrain steps up and adjoins the overlapping wall");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    parameters.heights = { 0.0 };
    check(!level::levelBuildHeightmapTerrain(refused, parameters)
            && refused.sectors.size() == before,
        "Heightmap Terrain refuses a height count mismatch without adding sectors");
    parameters.heights = { 0.0, std::numeric_limits<double>::quiet_NaN() };
    check(!level::levelBuildHeightmapTerrain(refused, parameters)
            && refused.sectors.size() == before,
        "Heightmap Terrain refuses a non-finite height without adding sectors");
    parameters.heights = { 0.0, 0.25 };
    parameters.columns = 0;
    check(!level::levelBuildHeightmapTerrain(refused, parameters)
            && refused.sectors.size() == before,
        "Heightmap Terrain refuses zero columns without adding sectors");
}

void testBuildHeightmapModel()
{
    level::TerrainParameters parameters{};
    parameters.origin = TVector::Create(0, 0, 0);
    parameters.cellSize = 1.0;
    parameters.headroom = 1.0;
    parameters.columns = 2;
    parameters.rows = 2;
    parameters.heights = { 0.0, 0.0, 0.0, 1.0 };
    const auto model = level::levelBuildHeightmapModel(parameters, "");
    check(model && model->meshes.size() == 1u, "Heightmap Model builds one terrain mesh");
    if (!model || model->meshes.size() != 1u)
    {
        return;
    }
    const assets::Model3doMesh& mesh = model->meshes[0];
    check(mesh.vertices.size() == 9u && mesh.faces.size() == 8u,
        "Heightmap Model grids (columns+1) x (rows+1) nodes into two triangles per cell");
    check(model->materials.size() == 1u && model->materials[0] == "dflt.mat",
        "Heightmap Model defaults to dflt.mat");
    // Node heights average the touching cells: center touches all four.
    check(std::abs(mesh.vertices[4].z - 0.25) < 1e-9 && std::abs(mesh.vertices[8].z - 1.0) < 1e-9
            && std::abs(mesh.vertices[0].z) < 1e-9,
        "Heightmap Model node heights average the adjacent cell heights");
    bool upward = true;
    for (const TVector& normal : mesh.faceNormals)
    {
        upward = upward && normal.z > 0.0;
    }
    check(upward && mesh.faceNormals.size() == mesh.faces.size(),
        "Heightmap Model faces wind upward with face normals");
    const std::string text = assets::saveTo3DO(*model);
    std::istringstream in(text);
    const assets::Model3do reloaded = assets::loadFrom3DO(in);
    check(reloaded.meshes.size() == 1u && reloaded.meshes[0].vertices.size() == 9u
            && reloaded.meshes[0].faces.size() == 8u,
        "Heightmap Model round-trips through the 3DO writer");

    const auto named = level::levelBuildHeightmapModel(parameters, "terrain.mat");
    check(named && named->materials.size() == 1u && named->materials[0] == "terrain.mat",
        "Heightmap Model honours an explicit material");
    check(!level::levelBuildHeightmapModel(parameters, "bad name.mat"),
        "Heightmap Model refuses a material name the 3DO text format cannot hold");
    parameters.heights = { 0.0 };
    check(!level::levelBuildHeightmapModel(parameters, ""),
        "Heightmap Model refuses a height count mismatch");
}

void testBuildThingPath()
{
    Level level{};
    (void)level.newThing();
    const std::array<FrameValue, 3> waypoints{
        FrameValue{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
        FrameValue{ 1.0, 0.0, 0.0, 0.0, 90.0, 0.0 },
        FrameValue{ 1.0, 1.0, 0.5, 0.0, 90.0, 0.0 },
    };
    const auto written = level::levelBuildThingPath(level, nullptr, 0, waypoints);
    check(written && *written == 3u, "Build Thing Path writes every waypoint");
    std::size_t frames = 0;
    for (const TplValue& value : level.things[0].values)
    {
        if (value.type == ValueType::frame)
        {
            ++frames;
        }
    }
    check(frames == 3u, "Build Thing Path stores typed frame values");

    const std::vector<TplValue> before = level.things[0].values;
    const std::vector<FrameValue> tooMany(level::kMaxThingFrames + 1, FrameValue{});
    check(!level::levelBuildThingPath(level, nullptr, 0, tooMany)
            && level.things[0].values.size() == before.size(),
        "Build Thing Path refuses an over-limit path without changes");
    check(!level::levelBuildThingPath(level, nullptr, 9, waypoints)
            && level.things[0].values.size() == before.size(),
        "Build Thing Path refuses an unknown Thing without changes");
}

} // namespace

void runRecipeTests()
{
    testRemoveRedundantVertices();
    testRemoveRedundantVerticesSkipsCollapsingMerge();
    testPlaceSectorLights();
    testPlaceSectorLightsRefusesInvalidIntensity();
    testBuildStairs();
    testBuildStairsRefusesInvalidParameters();
    testFitAndAdjoinSurfaces();
    testFitAndAdjoinRefusesMismatchAndConcaveResult();
    testMakeUnderwaterSectors();
    testMakeUnderwaterRefusesInvalidSettings();
    testMakeUnderwaterConfiguresWaterSurface();
    testMakeBreakableGlass();
    testMakeBreakableGlassRefusals();
    testMakeBreakableGlassRefusesInvalidSettings();
    testMakeBreakableGlassInvalidSettingsPrecedence();
    testMaskedSurfaceRecipe();
    testBuildSwitchbackStairs();
    testBuildSpiralStairs();
    testBuildDome();
    testBuildSweptCorridor();
    testInsetSurfaceAndPair();
    testReportAndSubdivide();
    testBuildHeightmapTerrain();
    testBuildHeightmapModel();
    testBuildThingPath();
}

} // namespace sed::tests
