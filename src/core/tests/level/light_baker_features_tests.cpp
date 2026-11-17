#include "test_common.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/lighting.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// The bake features adopted from LadyEebs' JED Light Baker: angle shading,
// water absorption, suns and multiply lights.
namespace sed::tests
{
namespace
{
using namespace sed::level;

std::int32_t room(Level& level, const TVector& low, const TVector& high)
{
    return levelCreateSectorPrimitiveBox(level, SectorPrimitive::Cube, low, high);
}

// The surface of `sector` whose normal points along `normal`.
std::int32_t faceFacing(const Level& level, const std::int32_t sector, const TVector& normal)
{
    const Sector& owner = level.sectors[static_cast<std::size_t>(sector)];
    for ( std::size_t f = 0; f < owner.surfaces.size(); ++f )
    {
        if ( owner.surfaces[f].normal.Dot(normal) > 0.999 ) return static_cast<std::int32_t>(f);
    }
    return -1;
}

// The baked color at the corner of `sector`'s face along `normal` that sits at `at`.
ColorF cornerLight(const Level& level, const std::int32_t sector, const TVector& normal, const TVector& at)
{
    const Sector& owner = level.sectors[static_cast<std::size_t>(sector)];
    const std::int32_t face = faceFacing(level, sector, normal);
    if ( face < 0 ) return ColorF{ -1.0f, -1.0f, -1.0f, -1.0f };
    const Surface& surface = owner.surfaces[static_cast<std::size_t>(face)];
    for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
    {
        const Vertex& v = owner.vertices[static_cast<std::size_t>(surface.vertices[i])];
        if ( std::abs(v.x - at.x) < 1e-9 && std::abs(v.y - at.y) < 1e-9 && std::abs(v.z - at.z) < 1e-9 )
            return surface.texVertices[i].color;
    }
    return ColorF{ -1.0f, -1.0f, -1.0f, -1.0f };
}

std::vector<std::int32_t> allSectors(const Level& level)
{
    std::vector<std::int32_t> sectors(level.sectors.size());
    for ( std::size_t i = 0; i < sectors.size(); ++i ) sectors[i] = static_cast<std::int32_t>(i);
    return sectors;
}

SedLight& addLight(Level& level, const TVector& at, const double intensity, const double range,
    const std::int32_t flags = 0, const ColorF& color = ColorWhite)
{
    SedLight& light = level.newLight();
    light.position = at;
    light.intensity = intensity;
    light.range = range;
    light.flags = flags;
    light.color = color;
    return light;
}

bool near(const double a, const double b, const double eps = 1e-4)
{
    return std::abs(a - b) <= eps;
}

const TVector kUp{ 0.0, 0.0, 1.0 };

void runAngleShadingTests()
{
    // A light 45 degrees off the floor's normal: angle shading scales the floor corner by cos 45.
    Level level{};
    level.kind = ProjectType::MOTS;
    const std::int32_t r = room(level, { -2.0, -2.0, 0.0 }, { 2.0, 2.0, 4.0 });
    addLight(level, { -1.5, -1.5, std::sqrt(0.5) }, 0.2, 10.0);
    const auto sectors = allSectors(level);
    const TVector corner{ -2.0, -2.0, 0.0 };

    levelCalcLightingNew(level, sectors);
    const float flat = cornerLight(level, r, kUp, corner).r;
    LightingBakeOptions options{};
    options.angleShading = true;
    levelCalcLightingNew(level, sectors, {}, options);
    const float shaded = cornerLight(level, r, kUp, corner).r;
    check(flat > 0.0f && near(shaded / flat, std::sqrt(0.5)), "angle shading: a light 45 degrees off the normal gives cos 45");
}

// Air above a pool, joined through the pool's surface. The pool may be split into stacked sectors.
Level makePool(const double airTop, const double waterTop, const std::vector<double>& splits)
{
    Level level{};
    level.kind = ProjectType::MOTS;
    room(level, { -2.0, -2.0, waterTop }, { 2.0, 2.0, airTop });
    double top = waterTop;
    std::vector<double> bottoms = splits;
    bottoms.push_back(0.0);
    for ( const double bottom : bottoms )
    {
        const std::int32_t water = room(level, { -2.0, -2.0, bottom }, { 2.0, 2.0, top });
        Sector& sector = level.sectors[static_cast<std::size_t>(water)];
        sector.flags |= SECF_Underwater;
        sector.tint = ColorF{ 0.2f, 0.5f, 0.8f, 1.0f };
        levelAdjoinSectors(level, water - 1, water);
        top = bottom;
    }
    addLight(level, { 0.0, 0.0, 2.0 }, 0.3, 10.0);
    return level;
}

void runWaterAbsorptionTests()
{
    const TVector corner{ -2.0, -2.0, 0.0 };
    // The segment from the light (z 2) to the pool's floor corner spends 1% of its length below z 0.02.
    const double waterLength = std::sqrt(12.0) * 0.01;
    const double share = waterLength / 0.1;
    for ( const bool split : { false, true } )
    {
        Level level = makePool(3.0, 0.02, split ? std::vector<double>{ 0.01 } : std::vector<double>{});
        const auto sectors = allSectors(level);
        const std::int32_t floorSector = static_cast<std::int32_t>(level.sectors.size()) - 1;
        levelCalcLightingNew(level, sectors);
        const ColorF clear = cornerLight(level, floorSector, kUp, corner);
        LightingBakeOptions options{};
        options.waterAbsorption = true;
        levelCalcLightingNew(level, sectors, {}, options);
        const ColorF absorbed = cornerLight(level, floorSector, kUp, corner);
        check(clear.r > 0.0f
                && near(absorbed.r / clear.r, std::pow(0.2, share), 1e-3)
                && near(absorbed.g / clear.g, std::pow(0.5, share), 1e-3)
                && near(absorbed.b / clear.b, std::pow(0.8, share), 1e-3),
            split ? "water absorption: a pool split in two absorbs the same as one sector"
                  : "water absorption: 0.35 m of water absorbs tint^0.35");
    }
    {
        // A metre or more of water settles at the tint.
        Level level = makePool(3.0, 1.0, { 0.5 });
        const auto sectors = allSectors(level);
        const std::int32_t floorSector = static_cast<std::int32_t>(level.sectors.size()) - 1;
        levelCalcLightingNew(level, sectors);
        const ColorF clear = cornerLight(level, floorSector, kUp, corner);
        LightingBakeOptions options{};
        options.waterAbsorption = true;
        levelCalcLightingNew(level, sectors, {}, options);
        const ColorF absorbed = cornerLight(level, floorSector, kUp, corner);
        check(clear.r > 0.0f && near(absorbed.r / clear.r, 0.2, 1e-4) && near(absorbed.b / clear.b, 0.8, 1e-4),
            "water absorption: deep water absorbs exactly its tint");
        // Corners in the air are untouched.
        const ColorF airCorner = cornerLight(level, 0, kUp.Scaled(-1.0), { -2.0, -2.0, 3.0 });
        Level plain = makePool(3.0, 1.0, { 0.5 });
        levelCalcLightingNew(plain, sectors);
        check(near(airCorner.r, cornerLight(plain, 0, kUp.Scaled(-1.0), { -2.0, -2.0, 3.0 }).r, 1e-6),
            "water absorption: light that never meets water is unchanged");
    }
}

void runSunTests()
{
    const TVector corner{ -2.0, -2.0, 0.0 };
    const auto skyRoom = [](const std::int32_t ceilingFlags)
    {
        Level level{};
        level.kind = ProjectType::MOTS;
        const std::int32_t r = room(level, { -2.0, -2.0, 0.0 }, { 2.0, 2.0, 2.0 });
        level.sectors[static_cast<std::size_t>(r)].surfaces[static_cast<std::size_t>(
            faceFacing(level, r, kUp.Scaled(-1.0)))].surfaceFlags |= ceilingFlags;
        return level;
    };
    {
        Level level = skyRoom(SF_SkyCeiling);
        addLight(level, { 0.0, 0.0, 100.0 }, 0.4, 1.0, LF_Sun);
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, 0, kUp, corner).r, 0.4f), "sun: overhead, through a sky ceiling, at full strength");
        check(near(cornerLight(level, 0, kUp.Scaled(-1.0), { -2.0, -2.0, 2.0 }).r, 0.0f),
            "sun: the ceiling itself faces away and stays dark");
    }
    {
        Level level = skyRoom(SF_SkyCeiling);
        addLight(level, { 100.0, 0.0, 100.0 }, 0.4, 1.0, LF_Sun);
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, 0, kUp, corner).r, 0.4f * std::sqrt(0.5f)), "sun: at 45 degrees, scaled by cos 45");
    }
    {
        Level level = skyRoom(SF_SkyCeiling);
        addLight(level, { 10.0, 10.0, 0.5 }, 1.0, 1.0, LF_SunAnchor);
        addLight(level, { 10.0, 10.0, 100.0 }, 0.4, 1.0, LF_Sun);
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, 0, kUp, corner).r, 0.4f),
            "sun: the anchor sets where its position is measured from, and bakes no light itself");
    }
    {
        Level level = skyRoom(0);
        addLight(level, { 0.0, 0.0, 100.0 }, 0.4, 1.0, LF_Sun);
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, 0, kUp, corner).r, 0.0f), "sun: a solid ceiling keeps it out");
    }
    {
        // Through an adjoin: a room below a sky room.
        Level level{};
        level.kind = ProjectType::MOTS;
        const std::int32_t below = room(level, { -2.0, -2.0, 0.0 }, { 2.0, 2.0, 2.0 });
        const std::int32_t above = room(level, { -2.0, -2.0, 2.0 }, { 2.0, 2.0, 4.0 });
        levelAdjoinSectors(level, below, above);
        level.sectors[static_cast<std::size_t>(above)].surfaces[static_cast<std::size_t>(
            faceFacing(level, above, kUp.Scaled(-1.0)))].surfaceFlags |= SF_SkyHorizon;
        addLight(level, { 0.0, 0.0, 100.0 }, 0.4, 1.0, LF_Sun);
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, below, kUp, corner).r, 0.4f), "sun: reaches through an adjoin to the sky beyond");
        // A BlockLight adjoin stops it.
        for ( Surface& surface : level.sectors[static_cast<std::size_t>(below)].surfaces )
            if ( surface.adjoin ) surface.adjoinFlags |= SAF_BlockLight;
        levelCalcLightingNew(level, allSectors(level));
        check(near(cornerLight(level, below, kUp, corner).r, 0.0f), "sun: a BlockLight adjoin stops it");
        // A sun bakes in full even when the caller asks for a partial re-bake far away.
        for ( Surface& surface : level.sectors[static_cast<std::size_t>(below)].surfaces )
            surface.adjoinFlags &= ~SAF_BlockLight;
        const std::array<LightingInfluenceSphere, 1> elsewhere{ LightingInfluenceSphere{ { 50.0, 50.0, 50.0 }, 1.0 } };
        check(levelCalcLightingNewInterruptible(level, allSectors(level), {}, elsewhere)
                && near(cornerLight(level, below, kUp, corner).r, 0.4f),
            "sun: a partial re-bake still bakes the sun everywhere");
    }
    {
        // Water on the way to the sky absorbs it too.
        Level level{};
        level.kind = ProjectType::MOTS;
        const std::int32_t pool = room(level, { -2.0, -2.0, 0.0 }, { 2.0, 2.0, 1.0 });
        const std::int32_t air = room(level, { -2.0, -2.0, 1.0 }, { 2.0, 2.0, 3.0 });
        levelAdjoinSectors(level, pool, air);
        level.sectors[static_cast<std::size_t>(pool)].flags |= SECF_Underwater;
        level.sectors[static_cast<std::size_t>(pool)].tint = ColorF{ 0.5f, 0.5f, 0.5f, 1.0f };
        level.sectors[static_cast<std::size_t>(air)].surfaces[static_cast<std::size_t>(
            faceFacing(level, air, kUp.Scaled(-1.0)))].surfaceFlags |= SF_SkyCeiling;
        addLight(level, { 0.0, 0.0, 100.0 }, 0.4, 1.0, LF_Sun);
        LightingBakeOptions options{};
        options.waterAbsorption = true;
        levelCalcLightingNew(level, allSectors(level), {}, options);
        check(near(cornerLight(level, pool, kUp, corner).r, 0.2f), "sun: water on the way to the sky absorbs it");
    }
}

void runMultiplyLightTests()
{
    const auto bake = [](const ProjectType kind, const bool withMultiply)
    {
        Level level{};
        level.kind = kind;
        room(level, { 0.0, 0.0, 0.0 }, { 4.0, 4.0, 4.0 });
        addLight(level, { 2.0, 2.0, 2.0 }, 0.3, 20.0, LF_NoBlock);
        if ( withMultiply ) addLight(level, { 0.5, 0.5, 0.5 }, 0.25, 2.0, LF_Multiply);
        levelCalcLightingNew(level, allSectors(level));
        return std::array<float, 2>{ cornerLight(level, 0, kUp, { 0.0, 0.0, 0.0 }).r,
            cornerLight(level, 0, kUp, { 4.0, 0.0, 0.0 }).r };
    };
    const double reach = (2.0 - std::sqrt(0.75)) / 2.0;
    const double weight = reach * reach;
    for ( const ProjectType kind : { ProjectType::JKDF2, ProjectType::IJIM } )
    {
        const auto plain = bake(kind, false);
        const auto multiplied = bake(kind, true);
        check(plain[0] > 0.0f && near(multiplied[0] / plain[0], 1.0 + (0.25 - 1.0) * weight, 1e-4),
            "multiply light: scales the light inside its range, fading with its falloff");
        check(near(multiplied[1], plain[1], 1e-6), "multiply light: leaves corners outside its range alone");
    }
    {
        // In MotS 0x20 is SED's LF_AffectArchThings, so the light stays additive.
        const auto plain = bake(ProjectType::MOTS, false);
        const auto flagged = bake(ProjectType::MOTS, true);
        check(flagged[0] > plain[0], "multiply light: in MotS a 0x20 light is still an additive light");
    }
}

} // namespace

void runLightBakerFeatureTests()
{
    runAngleShadingTests();
    runWaterAbsorptionTests();
    runSunTests();
    runMultiplyLightTests();
}

} // namespace sed::tests
