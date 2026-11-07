#include "test_common.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/statistics.hpp"

#include <array>
#include <cmath>

namespace sed::tests
{
namespace
{

std::int32_t addBox(Level& level, const TVector& minCorner, const TVector& maxCorner)
{
    return level::levelCreateSectorPrimitiveBox(level, level::SectorPrimitive::Cube, minCorner, maxCorner);
}

void testLevelStatistics()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(level, 0, 1), "statistics setup adjoins the pair");
    level.sectors[1].layer = 1;
    level.layers = { "base", "upper" };
    Thing thing{};
    thing.sector = 0;
    level.things.push_back(thing);
    SedLight light{};
    light.position = TVector::Create(1.5, 0.5, 0.5);
    level.lights.push_back(light);
    level.sectors[0].surfaces[0].surfaceFlags |= SF_SkyCeiling;

    const level::LevelStatistics whole = level::levelComputeStatistics(level);
    check(whole.sectors == 2u && whole.surfaces == level.sectors[0].surfaces.size()
            + level.sectors[1].surfaces.size(),
        "statistics: whole-level sector and surface counts");
    check(whole.things == 1u && whole.lights == 1u && whole.cogs == 0u,
        "statistics: whole-level thing, light and cog counts");
    check(std::abs(whole.surfaceArea - 12.0) < 1e-9,
        "statistics: two unit cubes have twelve units of surface area");
    check(whole.hasBounds && std::abs(whole.bounds.x1) < 1e-9 && std::abs(whole.bounds.x2 - 2.0) < 1e-9
            && std::abs(whole.bounds.z2 - 1.0) < 1e-9,
        "statistics: bounds span both cubes");
    check(whole.adjoinedSurfaces >= 2u && whole.skySurfaces == 1u,
        "statistics: adjoined and sky surfaces are counted");
    check(whole.sectorsByLayer.size() == 2u && whole.sectorsByLayer.at(0) == 1u
            && whole.sectorsByLayer.at(1) == 1u,
        "statistics: sectors are aggregated per layer");

    const std::array<std::int32_t, 1> onlySecond{ 1 };
    const level::LevelStatistics scoped = level::levelComputeStatistics(level, onlySecond);
    check(scoped.sectors == 1u && scoped.things == 0u && scoped.lights == 1u
            && std::abs(scoped.surfaceArea - 6.0) < 1e-9
            && std::abs(scoped.bounds.x1 - 1.0) < 1e-9,
        "statistics: a sector filter scopes counts, things, lights, area and bounds");

    const std::array<std::int32_t, 1> bogus{ 9 };
    const level::LevelStatistics empty = level::levelComputeStatistics(level, bogus);
    check(empty.sectors == 0u && !empty.hasBounds && empty.surfaceArea == 0.0,
        "statistics: an unknown sector filter aggregates nothing");

    const Level blank{};
    const level::LevelStatistics none = level::levelComputeStatistics(blank);
    check(none.sectors == 0u && !none.hasBounds,
        "statistics: an empty level has no bounds");
}

} // namespace

void runStatisticsTests()
{
    testLevelStatistics();
}

} // namespace sed::tests
