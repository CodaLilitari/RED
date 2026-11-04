#include "test_common.hpp"

#include "sed/assets/tpl.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool refusedWith(const std::optional<level::WorkbenchFailure>& failure, const char* code)
{
    return failure.has_value() && failure->code == code && !failure->message.empty();
}

void testStairParameterDomain()
{
    const level::StairParameters defaults{};
    check(!level::validateStairParameters(defaults), "stair domain: the dialog defaults are valid");

    const auto refused = [](level::StairParameters parameters, const char* message)
        {
            check(refusedWith(level::validateStairParameters(parameters), "invalidStairParameters"), message);
        };
    level::StairParameters p = defaults;
    p.width = level::kMinStairDimension * 0.5;
    refused(p, "stair domain: a width below the minimum is refused");
    p = defaults;
    p.run = level::kMaxStairDimension * 1.01;
    refused(p, "stair domain: a run above the maximum is refused");
    p = defaults;
    p.rise = kNan;
    refused(p, "stair domain: a NaN rise is refused");
    p = defaults;
    p.headroom = p.rise;
    refused(p, "stair domain: headroom must exceed the rise");
    p = defaults;
    p.steps = 0;
    refused(p, "stair domain: zero steps are refused");
    p = defaults;
    p.steps = level::kMaximumStairSteps + 1;
    refused(p, "stair domain: more than the step ceiling is refused");
    p = defaults;
    p.origin = TVector::Create(0.0, kInf, 0.0);
    refused(p, "stair domain: a non-finite origin is refused");
    p = defaults;
    p.origin = TVector::Create(level::kMaxWorkbenchCoordinate * 2.0, 0.0, 0.0);
    refused(p, "stair domain: an origin beyond the coordinate bound is refused");

    p = defaults;
    p.width = level::kMinStairDimension;
    p.run = level::kMaxStairDimension;
    p.rise = level::kMinStairDimension;
    p.headroom = level::kMaxStairDimension;
    p.steps = level::kMaximumStairSteps;
    check(!level::validateStairParameters(p), "stair domain: the bounds are inclusive");
}

void testLightAndUnderwaterDomains()
{
    check(!level::validateSectorLightIntensity(level::kDefaultSectorLightIntensity)
            && !level::validateSectorLightIntensity(level::kMinSectorLightIntensity)
            && !level::validateSectorLightIntensity(level::kMaxSectorLightIntensity),
        "light domain: the default and inclusive bounds are valid");
    for ( const double intensity : { 0.0, 0.009, 1000.5, kNan, kInf, -1.0 } )
    {
        check(refusedWith(level::validateSectorLightIntensity(intensity), "invalidSectorLightIntensity"),
            "light domain: an intensity outside the dialog range is refused");
    }

    const level::UnderwaterSettings defaults = level::defaultUnderwaterSettings();
    check(defaults.tint.r == static_cast<float>(level::kDefaultUnderwaterTintRed)
            && defaults.tint.g == static_cast<float>(level::kDefaultUnderwaterTintGreen)
            && defaults.tint.b == static_cast<float>(level::kDefaultUnderwaterTintBlue) && defaults.sound.empty()
            && defaults.soundVolume == 1.0 && defaults.configureWaterSurfaces && defaults.waterSurfaceMaterial.empty(),
        "underwater domain: defaults match the Make Underwater dialog");
    check(!level::validateUnderwaterSettings(defaults), "underwater domain: the defaults are valid");
    level::UnderwaterSettings bad = defaults;
    bad.tint.g = 1.01f;
    check(refusedWith(level::validateUnderwaterSettings(bad), "invalidUnderwaterSettings"),
        "underwater domain: a tint channel above one is refused");
    bad = defaults;
    bad.tint.r = -0.01f;
    check(refusedWith(level::validateUnderwaterSettings(bad), "invalidUnderwaterSettings"),
        "underwater domain: a negative tint channel is refused");
    bad = defaults;
    bad.soundVolume = kNan;
    check(refusedWith(level::validateUnderwaterSettings(bad), "invalidUnderwaterSettings"),
        "underwater domain: a NaN volume is refused");
    bad = defaults;
    bad.soundVolume = 1.5;
    check(refusedWith(level::validateUnderwaterSettings(bad), "invalidUnderwaterSettings"),
        "underwater domain: a volume above one is refused");
}

void testGlassDomainAndDefaults()
{
    level::BreakableGlassSettings settings{};
    check(!level::validateBreakableGlassSettings(settings), "glass domain: unset overrides are valid");
    settings.density = level::kMaxBreakableGlassDensity;
    settings.spawnPoints = 0;
    check(!level::validateBreakableGlassSettings(settings), "glass domain: the bounds are inclusive");
    settings.density = level::kMaxBreakableGlassDensity + 1;
    check(refusedWith(level::validateBreakableGlassSettings(settings), "invalidBreakableGlassSettings"),
        "glass domain: a density above the dialog maximum is refused");
    settings.density = -1;
    check(refusedWith(level::validateBreakableGlassSettings(settings), "invalidBreakableGlassSettings"),
        "glass domain: a negative density is refused");
    settings.density = 1;
    settings.spawnPoints = level::kMaxBreakableGlassSpawnPoints + 1;
    check(refusedWith(level::validateBreakableGlassSettings(settings), "invalidBreakableGlassSettings"),
        "glass domain: too many spawn points are refused");

    Level level{};
    const std::int32_t box = level::levelCreateSectorPrimitiveBox(
        level, level::SectorPrimitive::Cube, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    level.sectors[static_cast<std::size_t>(box)].surfaces[2].material = " pane.mat ";

    const level::BreakableGlassSettings plain = level::breakableGlassDefaults(level, SurfaceRef{ box, 2 }, {});
    check(plain.material == "pane.mat" && plain.density == level::kDefaultBreakableGlassDensity
            && plain.spawnPoints == level::kDefaultBreakableGlassSpawnPoints,
        "glass defaults: the selected side's trimmed material and the dialog defaults");

    std::vector<CogValue> symbols(3);
    symbols[0].name = "glass";
    symbols[0].cogType = CogType::surface;
    symbols[1].name = "Density";
    symbols[1].type = ValueType::integer;
    symbols[1].intValue = 9;
    symbols[2].name = "spawnpoints";
    symbols[2].type = ValueType::integer;
    symbols[2].intValue = 40;
    const level::BreakableGlassSettings scripted = level::breakableGlassDefaults(level, SurfaceRef{ box, 2 }, symbols);
    check(scripted.density == 9 && scripted.spawnPoints == level::kMaxBreakableGlassSpawnPoints,
        "glass defaults: script symbols seed the counts, clamped to the dialog range");

    const level::BreakableGlassSettings missing = level::breakableGlassDefaults(level, SurfaceRef{ box, 99 }, {});
    check(missing.material.empty(), "glass defaults: an unknown surface keeps each side's material");
}

void testMakeDoorHeight()
{
    Level level{};
    Thing& boxed = level.newThing();
    boxed.templateName = "crate";
    boxed.bbox.Set(-0.5, -0.5, -0.25, 0.5, 0.5, 1.75);
    Thing& templated = level.newThing();
    templated.templateName = "SLAB";
    Thing& bare = level.newThing();
    bare.templateName = "nothing";

    ThingTemplate slab{};
    slab.name = "slab";
    slab.parent = "none";
    slab.bbox.Set(-1.0, -0.1, 0.0, 1.0, 0.1, 0.75);
    level.templates.push_back(slab);

    check(close(level::makeDoorHeight(level, nullptr, 0), 2.0), "door height: the Thing's own box wins");
    check(close(level::makeDoorHeight(level, nullptr, 1), 0.75),
        "door height: an empty Thing box falls back to its embedded template by case-insensitive name");
    check(level::makeDoorHeight(level, nullptr, 2) == 0.0, "door height: no box anywhere yields zero");
    check(level::makeDoorHeight(level, nullptr, 7) == 0.0, "door height: an unknown Thing yields zero");

    assets::Templates database{};
    ThingTemplate tall{};
    tall.name = "nothing";
    tall.parent = "none";
    tall.bbox.Set(0.0, 0.0, -1.0, 0.2, 0.2, 2.0);
    database.addOrUpdate(tall);
    check(close(level::makeDoorHeight(level, &database, 2), 3.0),
        "door height: the project template database is the last fallback");
}

void testPlanarizeAndFlagRepairTargets()
{
    Level level{};
    const std::int32_t a = level::levelCreateSectorPrimitiveBox(
        level, level::SectorPrimitive::Cube, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::int32_t b = level::levelCreateSectorPrimitiveBox(
        level, level::SectorPrimitive::Cube, TVector::Create(3, 0, 0), TVector::Create(4, 1, 1));
    Sector& warped = level.sectors[static_cast<std::size_t>(b)];
    std::int32_t topVertex = -1;
    for ( std::size_t i = 0; i < warped.vertices.size(); ++i )
    {
        if ( warped.vertices[i].z > 0.5 )
        {
            topVertex = static_cast<std::int32_t>(i);
            break;
        }
    }
    check(topVertex >= 0, "planarize targets: fixture has a top vertex");
    warped.vertices[static_cast<std::size_t>(topVertex)].z += 0.25;

    std::vector<SurfaceRef> nonPlanar{};
    for ( std::size_t i = 0; i < warped.surfaces.size(); ++i )
    {
        if ( !level::surfaceIsPlanar(warped, warped.surfaces[i]) )
        {
            nonPlanar.push_back(SurfaceRef{ b, static_cast<std::int32_t>(i) });
        }
    }
    check(nonPlanar.size() >= 2u, "planarize targets: lifting a corner warps several surfaces");

    std::vector<SurfaceRef> selection{ SurfaceRef{ a, 0 }, SurfaceRef{ 9, 0 }, SurfaceRef{ b, -1 } };
    for ( auto it = nonPlanar.rbegin(); it != nonPlanar.rend(); ++it )
    {
        selection.push_back(*it);
    }
    const std::vector<SurfaceRef> targets = level::planarizeTargets(level, selection);
    check(targets == nonPlanar, "planarize targets: only valid non-planar surfaces, in sector/surface order");

    Level manual = level;
    bool manualChanged = false;
    for ( const SurfaceRef ref : targets )
    {
        manualChanged = level::levelFlattenSurface(manual, ref) || manualChanged;
    }
    std::size_t manualStill = 0;
    for ( const SurfaceRef ref : targets )
    {
        const Sector& sector = manual.sectors[static_cast<std::size_t>(ref.sector)];
        manualStill += level::surfaceIsPlanar(sector, sector.surfaces[static_cast<std::size_t>(ref.surface)]) ? 0u : 1u;
    }
    Level shared = level;
    const level::PlanarizeResult result = level::levelPlanarizeSurfaces(shared, targets);
    check(result.changed == manualChanged && result.stillNonPlanar == manualStill,
        "planarize: the shared loop reports what flattening each target in order reports");
    bool sameVertices = shared.sectors.size() == manual.sectors.size();
    for ( std::size_t s = 0; sameVertices && s < shared.sectors.size(); ++s )
    {
        const auto& lhs = shared.sectors[s].vertices;
        const auto& rhs = manual.sectors[s].vertices;
        sameVertices = lhs.size() == rhs.size();
        for ( std::size_t v = 0; sameVertices && v < lhs.size(); ++v )
        {
            sameVertices = lhs[v].x == rhs[v].x && lhs[v].y == rhs[v].y && lhs[v].z == rhs[v].z;
        }
    }
    check(sameVertices, "planarize: the shared loop moves exactly the vertices flattening each target moves");

    const std::array<std::int32_t, 3> sectors{ b, 42, -1 };
    const std::array<SurfaceRef, 3> surfaces{ SurfaceRef{ b, 1 }, SurfaceRef{ a, 3 }, SurfaceRef{ a, 3 } };
    const std::vector<SurfaceRef> repair = level::surfaceFlagRepairTargets(level, sectors, surfaces);
    std::vector<SurfaceRef> expected{ SurfaceRef{ a, 3 } };
    for ( std::size_t i = 0; i < level.sectors[static_cast<std::size_t>(b)].surfaces.size(); ++i )
    {
        expected.push_back(SurfaceRef{ b, static_cast<std::int32_t>(i) });
    }
    check(repair == expected, "flag repair targets: whole sectors plus surfaces, sorted and unique");
}

} // namespace

void runRecipeRequestTests()
{
    testStairParameterDomain();
    testLightAndUnderwaterDomains();
    testGlassDomainAndDefaults();
    testMakeDoorHeight();
    testPlanarizeAndFlagRepairTargets();
}

} // namespace sed::tests
