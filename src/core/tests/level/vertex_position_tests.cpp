#include "test_common.hpp"

#include "sed/assets/sed.hpp"
#include "sed/level/vertex_position.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace sed::tests
{
namespace
{
Level pyramid(const std::size_t corners, const double offset = 0)
{
    Level level;
    Sector sector;
    for ( std::size_t i = 0; i < corners; ++i )
    {
        const auto angle = 2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(corners);
        sector.vertices.push_back({ offset + 2 * std::cos(angle), 2 * std::sin(angle), 0 });
    }
    const auto apex = static_cast<std::int32_t>(corners);
    sector.vertices.push_back({ offset, 0, 4 });
    Surface base;
    for ( std::int32_t i = 0; i < apex; ++i )
    {
        base.vertices.push_back(i);
    }
    base.texVertices.resize(corners);
    base.normal = level::surfaceCalcNormalNewell(sector, base);
    sector.surfaces.push_back(std::move(base));
    for ( std::int32_t i = 0; i < apex; ++i )
    {
        appendTestSurface(sector, { i, apex, (i + 1) % apex });
        sector.surfaces.back().texVertices.resize(3);
    }
    level.sectors.push_back(std::move(sector));
    return level;
}
} // namespace

void runVertexPositionTests()
{
    using Status = level::VertexPositionStatus;
    {
        auto model = pyramid(65);
        auto agentModel = model;
        const auto result = level::levelSetVertexPositionChecked(model, { 0, 65 }, { 0, 0, 5 });
        check(result.status == Status::Changed && model.sectors[0].vertices[65].z == 5,
            "vertex position: model policy admits a valid face above the agent corner limit");
        level::VertexPositionOptions agentPolicy;
        agentPolicy.maxFaceCorners = 64;
        agentPolicy.coordinateLimit = 1000000;
        const auto before = assets::saveToSedBytes(agentModel);
        check(level::levelSetVertexPositionChecked(agentModel, { 0, 65 }, { 0, 0, 5 }, agentPolicy).status
                == Status::GeometryUnsupported && assets::saveToSedBytes(agentModel) == before,
            "vertex position: caller corner policy refuses atomically");
    }
    {
        auto model = pyramid(4, 2000000);
        auto agentModel = model;
        check(level::levelSetVertexPositionChecked(model, { 0, 4 }, { 2000000, 0, 5 }).status == Status::Changed,
            "vertex position: model policy admits finite coordinates above agent coordinate limit");
        level::VertexPositionOptions agentPolicy;
        agentPolicy.coordinateLimit = 1000000;
        const auto before = assets::saveToSedBytes(agentModel);
        check(level::levelSetVertexPositionChecked(agentModel, { 0, 4 }, { 2000000, 0, 5 }, agentPolicy).status
                == Status::InvalidPosition && assets::saveToSedBytes(agentModel) == before,
            "vertex position: caller coordinate policy refuses atomically");
    }
    {
        auto model = pyramid(4);
        const auto before = assets::saveToSedBytes(model);
        level::VertexPositionOptions options;
        options.workLimit = 1;
        check(level::levelSetVertexPositionChecked(model, { 0, 4 }, { 0, 0, 4 }, options).status
                == Status::WorkLimitExceeded && assets::saveToSedBytes(model) == before,
            "vertex position: unchanged requests still obey work admission");
        options = {};
        const std::array<std::int32_t, 1> readOnly{ 0 };
        options.readOnlySectors = readOnly;
        check(level::levelSetVertexPositionChecked(model, { 0, 4 }, { 0, 0, 5 }, options).status
                == Status::ReadOnlySector && assets::saveToSedBytes(model) == before,
            "vertex position: noneditable sector refuses without partial state");
        check(level::levelSetVertexPositionChecked(model, { 9, 4 }, { 0, 0, 5 }).status == Status::InvalidTarget
                && assets::saveToSedBytes(model) == before,
            "vertex position: invalid target refuses atomically");
        check(level::levelSetVertexPositionChecked(model, { 0, 4 },
                  { 0, 0, std::numeric_limits<double>::infinity() }).status == Status::InvalidPosition
                && assets::saveToSedBytes(model) == before,
            "vertex position: nonfinite requested coordinates refuse atomically");
        check(level::levelSetVertexPositionChecked(model, { 0, 4 },
                  { std::numeric_limits<double>::max(), 0, 4 }).status == Status::GeometryUnsupported
                && assets::saveToSedBytes(model) == before,
            "vertex position: finite but unrepresentable derived geometry refuses atomically");
        check(level::levelSetVertexPositionChecked(model, { 0, 4 }, { 0, 0, 4 }).status == Status::Unchanged
                && assets::saveToSedBytes(model) == before,
            "vertex position: validated no-op keeps exact authored bytes");
    }
}
} // namespace sed::tests
