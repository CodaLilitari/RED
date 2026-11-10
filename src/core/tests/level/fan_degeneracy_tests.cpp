#include "test_common.hpp"

#include "sed/level/fan_degeneracies.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace sed::tests
{
namespace
{

// One quad whose corner 2 sits exactly on the corner-0..corner-3 diagonal
// fan line pattern variants are built per case from raw positions.
Level fanLevel(std::vector<std::array<double, 3>> points,
    std::vector<std::int32_t> loop)
{
    Level level{};
    Sector sector{};
    for (const auto& p : points)
    {
        sector.vertices.push_back({ p[0], p[1], p[2], 0 });
    }
    Surface surface{};
    surface.vertices = std::move(loop);
    sector.surfaces.push_back(std::move(surface));
    level.sectors.push_back(std::move(sector));
    return level;
}

} // namespace

void runFanDegeneracyTests()
{
    using namespace sed::level;

    {
        // Ordinary quad: no exact degeneracies, complete scan.
        const Level level = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 2, 0 }, { 0, 2, 0 } },
            { 0, 1, 2, 3 });
        const FanDegeneracyReport report = inspectFanDegeneracies(level);
        check(report.status == FanDegeneracyStatus::Complete && report.issues.empty(),
            "fan degeneracy: an ordinary quad completes clean");
    }
    {
        // Corner 2 lies exactly on segment corner1..corner3's fan edge:
        // loop (0,0) (2,0) (1,0) (0,2): fan triangle (0,1,2) is collinear.
        const Level level = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 1, 0, 0 }, { 0, 2, 0 } },
            { 0, 1, 2, 3 });
        const FanDegeneracyReport report = inspectFanDegeneracies(level);
        check(report.status == FanDegeneracyStatus::Complete
                && report.issues.size() == 1u
                && report.issues[0].kind == FanDegeneracyKind::CollinearCorners
                && report.issues[0].fanIndex == 1u
                && report.issues[0].surface.sector == 0
                && report.issues[0].surface.surface == 0,
            "fan degeneracy: exactly collinear fan corner is named");
    }
    {
        // Repeated position and repeated index are both coincident.
        const Level level = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 } },
            { 0, 1, 2, 3 });
        const FanDegeneracyReport report = inspectFanDegeneracies(level);
        check(report.status == FanDegeneracyStatus::Complete
                && report.issues.size() == 1u
                && report.issues[0].kind == FanDegeneracyKind::CoincidentCorners,
            "fan degeneracy: coincident stored positions are named");
        const Level byIndex = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 } },
            { 0, 1, 1, 2 });
        const FanDegeneracyReport byIndexReport = inspectFanDegeneracies(byIndex);
        check(byIndexReport.status == FanDegeneracyStatus::Complete
                && byIndexReport.issues.size() == 1u
                && byIndexReport.issues[0].kind == FanDegeneracyKind::CoincidentCorners,
            "fan degeneracy: a repeated corner index is coincident");
    }
    {
        // Near-degenerate is not exact: a hair off the line stays clean.
        const Level level = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 1, 1e-9, 0 }, { 0, 2, 0 } },
            { 0, 1, 2, 3 });
        const FanDegeneracyReport report = inspectFanDegeneracies(level);
        check(report.status == FanDegeneracyStatus::Complete && report.issues.empty(),
            "fan degeneracy: near-zero area without exact zero is not claimed");
    }
    {
        // Invalid input refuses as InvalidGeometry, never as clean.
        const Level badIndex = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 } },
            { 0, 1, 7 });
        check(inspectFanDegeneracies(badIndex).status
                == FanDegeneracyStatus::InvalidGeometry,
            "fan degeneracy: an out-of-range corner refuses");
        const Level nonFinite = fanLevel(
            { { 0, 0, 0 }, { std::numeric_limits<double>::quiet_NaN(), 0, 0 },
              { 0, 2, 0 } },
            { 0, 1, 2 });
        check(inspectFanDegeneracies(nonFinite).status
                == FanDegeneracyStatus::InvalidGeometry,
            "fan degeneracy: a non-finite coordinate refuses");
    }
    {
        // Budgets stop the scan honestly instead of truncating silently.
        const Level level = fanLevel(
            { { 0, 0, 0 }, { 2, 0, 0 }, { 1, 0, 0 }, { 0, 2, 0 } },
            { 0, 1, 2, 3 });
        FanDegeneracyLimits noTriangles{};
        noTriangles.maxFanTriangles = 1;
        check(inspectFanDegeneracies(level, noTriangles).status
                == FanDegeneracyStatus::BudgetExceeded,
            "fan degeneracy: the triangle budget refuses");
        FanDegeneracyLimits noIssues{};
        noIssues.maxIssues = 0;
        const FanDegeneracyReport capped = inspectFanDegeneracies(level, noIssues);
        check(capped.status == FanDegeneracyStatus::BudgetExceeded && capped.issues.empty(),
            "fan degeneracy: the issue budget refuses before publishing");
    }
}

} // namespace sed::tests
