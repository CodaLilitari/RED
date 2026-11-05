#include "test_common.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/recipes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
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
}

} // namespace sed::tests
