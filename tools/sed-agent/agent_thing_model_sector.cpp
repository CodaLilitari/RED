#include "agent_query_details.hpp"
#include "agent_workspace.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/sector_boundary.hpp"
#include "sed/level/thing.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sed_agent
{
namespace
{
constexpr double tolerance = 1e-5; // JK units; includes corners on the owning room's walls.
struct Plane
{
    sed::TVector normal, anchor;
};

void spend(std::size_t& remaining, std::size_t count)
{
    if (count > remaining)
        throw AgentError{kExitRefusal, "thingModelSector geometry work limit exceeded", "modelSectorWorkLimit"};
    remaining -= count;
}

std::vector<Plane> roomPlanes(const sed::Sector& sector, std::size_t& work)
{
    std::vector<Plane> planes;
    spend(work, sector.vertices.size());
    for (const auto& face : sector.surfaces)
    {
        const auto corners = face.vertices.size();
        // Reserve the quadratic boundary check and convex-sector scan before running them.
        if (corners < 3) return {};
        spend(work, corners);
        if (corners > work / corners || sector.vertices.size() > work / corners)
            throw AgentError{kExitRefusal, "thingModelSector room work limit exceeded", "modelSectorWorkLimit"};
        spend(work, corners * corners);
        spend(work, sector.vertices.size() * corners);
        if (!sed::level::surfaceIsPlanarForDiagnostics(sector, face) ||
            !sed::level::surfaceIsConvexForDiagnostics(sector, face) ||
            !sed::level::surfaceHasSimpleBoundary(sector, face))
            return {};
        const auto& anchor = sector.vertices[face.vertices.front()];
        auto normal = face.normal;
        const auto length = normal.normalize();
        if (!std::isfinite(length) || length <= 0) return {};
        planes.push_back({normal, {anchor.x, anchor.y, anchor.z}});
    }
    if (!sed::level::sectorIsConvexForDiagnostics(sector) ||
        !sed::level::sectorHasClosedVolumeBoundary(sector, [&](std::size_t n) {
            spend(work, n);
            return true;
        }))
        return {};
    return planes;
}
} // namespace

Json queryThingModelSector(const sed::Level& level, std::size_t index,
                           const sed::assets::ResourceLocator& resources)
{
    const auto& thing = level.things.at(index);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("query"));
    out.set("collection", Json::makeString("thingModelSector"));
    out.set("thing", Json::makeNumber(index));
    out.set("sector", Json::makeNumber(thing.sector));
    out.set("tolerance", Json::makeNumber(tolerance));
    const auto finish = [&](std::string status) {
        out.set("status", Json::makeString(std::move(status)));
        return std::move(out);
    };
    if (thing.sector < 0 || static_cast<std::size_t>(thing.sector) >= level.sectors.size())
        return finish("sectorUnresolved");
    const std::string& leaf = thing.templateName.empty() ? thing.name : thing.templateName;
    const auto chain = sed::level::TemplateChainIndex(level).resolve(leaf);
    if (!leaf.empty() && !chain) return finish("templateUnresolved");
    const auto value = [&](std::string_view name) -> const sed::TplValue* {
        for (auto it = thing.values.rbegin(); it != thing.values.rend(); ++it)
            if (sed::text::iequalsAscii(it->name, name)) return &*it;
        return chain ? sed::level::findTemplateChainField(*chain, name) : nullptr;
    };
    const auto text = [&](std::string_view name) {
        const auto* v = value(name);
        return v ? sed::text::lowerAscii(v->asString()) : std::string{};
    };
    const auto flags = value("thingflags");
    out.set("levelGeometry", Json::makeBool(flags && (flags->intValue & 0x8)));
    // Both native Thing draw passes skip destroyed, hidden and disabled placements.
    if (flags && (flags->intValue & (0x2 | 0x10 | 0x80000))) return finish("hiddenOrDisabled");
    const auto move = text("move"), puppet = text("puppet");
    const auto type = text("type");
    if ((!move.empty() && move != "none" && move != "path") || (move == "path" && value("frame")) ||
        (!puppet.empty() && puppet != "none") || type == "actor" || type == "player" || type == "weapon")
        return finish("dynamic");

    auto modelName = text("model3d");
    if (modelName.empty()) modelName = text("model3do");
    if (modelName.empty() || modelName == "none") return finish("noModel");
    if (!modelName.ends_with(".3do")) modelName += ".3do";
    sed::assets::ResourceReadOrigin origin;
    auto bytes = resources.tryReadCurrentBinary(modelName, 8 * 1024 * 1024, origin);
    out.set("model", Json::makeString(modelName));
    if (!bytes) return finish("modelUnresolved");
    out.set("archiveEntry", Json::makeString(origin.archiveEntry));
    out.set("modelSha256", Json::makeString(sha256Hex(*bytes)));
    sed::assets::Model3do model;
    try
    {
        model = sed::assets::loadCheckedModel3doBindPose(*bytes);
    }
    catch (const std::runtime_error&)
    {
        return finish("modelUnresolved");
    }
    std::size_t work = 4'000'000;
    const auto planes = roomPlanes(level.sectors[thing.sector], work);
    if (planes.empty()) return finish("sectorUnresolved");
    const auto transform = sed::TMatrix43::Create({thing.x, thing.y, thing.z}, {thing.pch, thing.yaw, thing.rol});
    std::size_t count = 0;
    double worst = tolerance;
    Json witness;
    for (std::size_t m = 0; m < model.meshes.size(); ++m)
    {
        const auto& mesh = model.meshes[m];
        if (!mesh.geo) continue;
        std::vector<bool> seen(mesh.vertices.size());
        for (std::size_t f = 0; f < mesh.faces.size(); ++f)
        {
            const auto& face = mesh.faces[f];
            if (!face.geo || face.vertices.size() < 3) continue;
            for (auto v : face.vertices)
            {
                if (v < 0 || static_cast<std::size_t>(v) >= seen.size()) return finish("modelUnresolved");
                if (seen[v]) continue;
                seen[v] = true;
                ++count;
                spend(work, planes.size());
                const auto& p = mesh.vertices[v];
                const auto world = transform.TransformPoint({p.x, p.y, p.z});
                for (std::size_t s = 0; s < planes.size(); ++s)
                {
                    const double distance = -planes[s].normal.dot(world - planes[s].anchor);
                    if (!std::isfinite(distance)) return finish("transformUnresolved");
                    if (distance <= worst) continue;
                    worst = distance;
                    witness = Json::makeObject();
                    witness.set("mesh", Json::makeString(mesh.name));
                    witness.set("meshInstance", Json::makeNumber(m));
                    witness.set("face", Json::makeNumber(f));
                    witness.set("vertex", Json::makeNumber(v));
                    witness.set("surface", Json::makeNumber(s));
                    witness.set("distance", Json::makeNumber(distance));
                    Json point = Json::makeArray();
                    for (double coord : {world.x, world.y, world.z})
                        point.array.push_back(Json::makeNumber(coord));
                    witness.set("worldPosition", std::move(point));
                }
            }
        }
    }
    out.set("testedVertices", Json::makeNumber(count));
    if (!count) return finish("noDrawableFaces");
    if (!witness.isObject()) return finish("contained");
    out.set("witness", std::move(witness));
    return finish("extendsOutside");
}
} // namespace sed_agent
