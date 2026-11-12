#include "agent_arch_operations.hpp"

#include "agent_workspace.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/level/arch_authoring.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed_agent
{
namespace
{
constexpr std::size_t kMaxWorkUnits = 50000000u;
constexpr std::size_t kMaxModelBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxLights = 256u;
constexpr std::size_t kMaxWorldTriangles = 262144u;
constexpr std::size_t kMaxWorldSurfaces = 262144u;
constexpr std::size_t kMaxFaceCorners = 4096u;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "archOperationInvalid" };
}

[[noreturn]] void refuse(const std::string& code, const std::string& message)
{
    throw AgentError{ kExitRefusal, message, code };
}

void fields(const Json& operation, const std::initializer_list<std::string_view> allowed)
{
    for ( const auto& [name, value] : operation.object )
    {
        (void)value;
        if ( std::find(allowed.begin(), allowed.end(), name) == allowed.end() )
        {
            invalid("Unknown ARCH operation field '" + name + "'");
        }
    }
}

std::int32_t index(const Json& operation, const char* name)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != Json::Type::Number || !std::isfinite(value->number)
        || value->number < 0.0 || value->number > std::numeric_limits<std::int32_t>::max()
        || std::floor(value->number) != value->number )
    {
        invalid(std::string(name) + " must be a nonnegative 32-bit integer");
    }
    return static_cast<std::int32_t>(value->number);
}

std::string_view token(const Json& operation, const char* name)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != Json::Type::String )
    {
        invalid(std::string("Missing or invalid '") + name + "'");
    }
    return value->str;
}

void charge(std::size_t& remaining, const std::size_t amount)
{
    if ( amount > remaining )
    {
        refuse("archBakeBudgetExceeded", "ARCH operations exceed the patch's bounded resource/work budget");
    }
    remaining -= amount;
}

void product(ArchOperationBudget& budget, const std::size_t count, const std::size_t each)
{
    if ( each != 0 && count > budget.remainingWorkUnits / each )
    {
        refuse("archBakeBudgetExceeded", "ARCH operations exceed the patch's bounded work budget");
    }
    charge(budget.remainingWorkUnits, count * each);
}

bool finite(const sed::TVector& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

void checkPrepared(const sed::level::PreparedArchAuthoring& prepared)
{
    if ( prepared.failure )
    {
        refuse(prepared.failure->code, prepared.failure->message);
    }
}

std::string modelName(const sed::Level& level, const sed::Thing& thing,
    const sed::assets::Templates* templates)
{
    const auto* value = sed::level::thingFindValue(level, templates, thing, "model3d");
    if ( !value )
    {
        value = sed::level::thingFindValue(level, templates, thing, "model3do");
    }
    std::string name = value ? sed::text::lowerAscii(sed::text::trim(value->asString())) : std::string{};
    if ( name.empty() || name == "none" )
    {
        refuse("archModelMissing", "ARCH authoring requires the Thing's effective model resource");
    }
    if ( !sed::text::iendsWithAscii(name, ".3do") )
    {
        name += ".3do";
    }
    return name;
}

struct AdmittedModel
{
    ArchModelResource resource;
    sed::assets::ModelLightingInspection inspection;
    sed::level::MotsArchLightingTopology topology;
};

std::span<const std::byte> modelBytes(const ArchModelResource& resource)
{
    return std::as_bytes(std::span(resource.bytes.data(), resource.bytes.size()));
}

AdmittedModel admitModel(const std::string& name, const ArchModelResolver& resolver,
    ArchOperationBudget& budget)
{
    auto resource = resolver ? resolver(name) : std::nullopt;
    if ( !resource )
    {
        refuse("archModelMissing", "ARCH model '" + name + "' is missing from the captured resources");
    }
    sed::assets::ModelLightingLimits limits{};
    if ( resource->bytes.size() > limits.sourceBytes )
    {
        refuse("archModelBudgetExceeded", "ARCH model exceeds the 16 MiB source limit");
    }
    charge(budget.remainingModelBytes, resource->bytes.size());
    charge(budget.remainingWorkUnits, resource->bytes.size());
    if ( resource->sha256Hex != sha256Hex(resource->bytes) )
    {
        throw AgentError{ kExitConflict, "ARCH model digest does not match its captured bytes",
            "archModelRevisionMismatch" };
    }

    AdmittedModel model{ std::move(*resource), {}, {} };
    limits.workUnits = std::min(limits.workUnits, budget.remainingWorkUnits);
    try
    {
        // Reuse strict bounded admission and every-LOD source identity checks;
        // the topology-only scanner cannot certify complete model geometry.
        model.inspection = sed::assets::inspectModelLighting(modelBytes(model.resource), limits);
    }
    catch ( const std::runtime_error& error )
    {
        refuse("archModelInvalid", "ARCH model '" + name + "' was refused: " + error.what());
    }
    // Inspection charges one source scan and one raw parse per LOD. Its two
    // hierarchy walks have at most two visits per admitted node. Reserve that
    // fixed upper bound so repeated small models share the same command cap.
    product(budget, model.resource.bytes.size(), model.inspection.geosets + 1u);
    product(budget, model.inspection.sourceMeshes, 2u);
    product(budget, limits.hierarchyNodes, 2u);
    if ( model.inspection.geosets == 0 || model.inspection.vertices == 0 )
    {
        refuse("archModelInvalid", "ARCH model has no authored vertex geometry");
    }
    model.topology.geosets.resize(model.inspection.geosets);
    for ( const auto& mesh : model.inspection.meshes )
    {
        if ( mesh.geoset >= model.topology.geosets.size()
            || mesh.sourceMesh != model.topology.geosets[mesh.geoset].size() )
        {
            refuse("archModelInvalid", "ARCH model source mesh identity is inconsistent");
        }
        model.topology.geosets[mesh.geoset].push_back(mesh.vertices);
    }
    if ( model.topology.geosets.front().empty() )
    {
        refuse("archModelInvalid", "ARCH model geoset zero has no meshes");
    }
    return model;
}

void validateSourceSamples(const sed::Level& level, const std::int32_t lightingIndex,
    const sed::level::MotsArchLightingTopology& topology, ArchOperationBudget& budget)
{
    if ( static_cast<std::size_t>(lightingIndex) >= level.motsArchLighting.size() )
    {
        refuse("archSourceInvalid", "ARCH source lighting index is invalid");
    }
    const auto& source = level.motsArchLighting[static_cast<std::size_t>(lightingIndex)];
    std::string topologyCode;
    if ( !sed::level::archTopologyAllowsBinding(source, topology, &topologyCode) )
    {
        refuse(topologyCode, "ARCH source samples do not fit the resolved model's all-LOD topology");
    }
    for ( const auto& mesh : source.meshes )
    {
        charge(budget.remainingWorkUnits, mesh.vertices.size());
        for ( const auto& sample : mesh.vertices )
        {
            if ( !std::isfinite(sample.mono) || !std::isfinite(sample.red)
                || !std::isfinite(sample.green) || !std::isfinite(sample.blue) )
            {
                refuse("archSourceInvalid", "ARCH binding source has a non-finite sample");
            }
        }
    }
}

void admitHierarchyWork(const sed::assets::Model3do& raw, ArchOperationBudget& budget)
{
    // The loader applies each ancestor to every vertex/normal. Admission uses
    // the same ancestry, before asking it to perform the transformed load.
    for ( const auto& node : raw.nodes )
    {
        if ( node.mesh < 0 )
        {
            continue;
        }
        if ( static_cast<std::size_t>(node.mesh) >= raw.meshes.size() )
        {
            refuse("archModelInvalid", "ARCH hierarchy refers to a missing source mesh");
        }
        std::size_t depth = 1;
        auto parent = node.parent;
        while ( parent != -1 )
        {
            charge(budget.remainingWorkUnits, 1u);
            if ( parent < 0 || static_cast<std::size_t>(parent) >= raw.nodes.size()
                || depth >= raw.nodes.size() )
            {
                refuse("archModelInvalid", "ARCH hierarchy has an invalid or cyclic parent chain");
            }
            ++depth;
            parent = raw.nodes[static_cast<std::size_t>(parent)].parent;
        }
        const auto& mesh = raw.meshes[static_cast<std::size_t>(node.mesh)];
        product(budget, depth, 2u * mesh.vertices.size() + mesh.faces.size());
    }
}

void validateLodPrefixes(const sed::level::MotsArchLightingTopology& topology)
{
    const auto& first = topology.geosets.front();
    for ( const auto& geoset : topology.geosets )
    {
        if ( geoset.size() > first.size() )
        {
            refuse("archLowerLodOverflow", "ARCH lower LOD mesh count exceeds geoset zero");
        }
        for ( std::size_t mesh = 0; mesh < geoset.size(); ++mesh )
        {
            if ( geoset[mesh] > first[mesh] )
            {
                refuse("archLowerLodOverflow", "ARCH lower LOD vertex count exceeds geoset zero");
            }
        }
    }
}

std::vector<std::vector<sed::TVector>> worldSamples(const sed::Thing& thing,
    const AdmittedModel& model, ArchOperationBudget& budget)
{
    if ( !finite({ thing.x, thing.y, thing.z }) || !finite({ thing.pch, thing.yaw, thing.rol }) )
    {
        refuse("archBakeInputInvalid", "ARCH Thing transform must be finite");
    }
    if ( model.inspection.sharedMeshes != 0 || model.inspection.orphanMeshes != 0 )
    {
        refuse("archBakeModelAmbiguous", "ARCH baking requires one hierarchy placement per source mesh");
    }
    validateLodPrefixes(model.topology);
    const auto& first = model.topology.geosets.front();
    sed::assets::Model3do transformed;
    try
    {
        charge(budget.remainingWorkUnits, model.resource.bytes.size());
        const auto raw = sed::assets::loadFrom3DO(modelBytes(model.resource), { 0, false });
        admitHierarchyWork(raw, budget);
        charge(budget.remainingWorkUnits, model.resource.bytes.size());
        transformed = sed::assets::loadFrom3DO(modelBytes(model.resource), { 0, true });
    }
    catch ( const std::runtime_error& error )
    {
        refuse("archModelInvalid", std::string("ARCH transformed model was refused: ") + error.what());
    }
    sed::TVector pyr{};
    pyr.pitch = thing.pch;
    pyr.yaw = thing.yaw;
    pyr.roll = thing.rol;
    const auto transform = sed::TMatrix43::Create({ thing.x, thing.y, thing.z }, pyr);
    std::vector<std::vector<sed::TVector>> result(first.size());
    std::vector<bool> seen(first.size(), false);
    for ( std::size_t mesh = 0; mesh < transformed.meshes.size(); ++mesh )
    {
        const auto source = sed::level::motsArchLightingSourceMeshIndex(transformed, mesh);
        if ( !source || *source >= first.size() || seen[*source]
            || transformed.meshes[mesh].vertices.size() != first[*source] )
        {
            refuse("archBakeModelAmbiguous", "ARCH transformed source mesh mapping is ambiguous");
        }
        seen[*source] = true;
        auto& points = result[*source];
        charge(budget.remainingWorkUnits, first[*source]);
        points.reserve(first[*source]);
        for ( const auto& vertex : transformed.meshes[mesh].vertices )
        {
            const auto point = transform.TransformPoint({ vertex.x, vertex.y, vertex.z });
            if ( !finite(point) )
            {
                refuse("archBakeInputInvalid", "ARCH model world transform produced a non-finite vertex");
            }
            points.push_back(point);
        }
    }
    if ( std::find(seen.begin(), seen.end(), false) != seen.end() )
    {
        refuse("archBakeModelAmbiguous", "ARCH model has a source mesh without a world placement");
    }
    return result;
}

void appendWorldFace(const sed::Sector& sector, const sed::Surface& surface,
    std::vector<sed::level::ArchOccluderTriangle>& triangles, ArchOperationBudget& budget)
{
    const auto count = surface.vertices.size();
    if ( count < 3 || count > kMaxFaceCorners )
    {
        refuse("archOccluderInvalid", "ARCH occlusion requires world faces with 3..4096 corners");
    }
    if ( count - 2u > kMaxWorldTriangles - triangles.size() )
    {
        refuse("archBakeBudgetExceeded", "ARCH world triangle budget exceeded");
    }
    product(budget, count, count);
    std::vector<sed::TVector> points;
    points.reserve(count);
    for ( const auto index : surface.vertices )
    {
        if ( index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size() )
        {
            refuse("archOccluderInvalid", "ARCH world face references a missing vertex");
        }
        const auto& vertex = sector.vertices[static_cast<std::size_t>(index)];
        const sed::TVector point{ vertex.x, vertex.y, vertex.z };
        if ( !finite(point) )
        {
            refuse("archOccluderInvalid", "ARCH world face has a non-finite vertex");
        }
        points.push_back(point);
    }
    const auto anchor = points.front();
    auto normalized = points;
    double scale = 0.0;
    for ( auto& point : normalized )
    {
        point = point - anchor;
        scale = std::max(scale, std::hypot(point.x, point.y, point.z));
    }
    if ( !std::isfinite(scale) || scale == 0.0 )
    {
        refuse("archOccluderInvalid", "ARCH world face is degenerate or overflows");
    }
    for ( auto& point : normalized )
    {
        point = { point.x / scale, point.y / scale, point.z / scale };
    }
    auto normal = normalized[1].Cross(normalized[2]);
    const double length = std::hypot(normal.x, normal.y, normal.z);
    if ( !std::isfinite(length) || length <= 1e-10 )
    {
        refuse("archOccluderInvalid", "ARCH world face has a degenerate triangle fan");
    }
    normal = { normal.x / length, normal.y / length, normal.z / length };
    // A triangle fan is an honest opaque proxy only for a planar, simple,
    // strictly convex polygon. Admit all edge half-spaces before adding it.
    constexpr double epsilon = 1e-10;
    for ( const auto& point : normalized )
    {
        if ( std::abs(point.Dot(normal)) > epsilon )
        {
            refuse("archOccluderInvalid", "ARCH occlusion requires planar world faces");
        }
    }
    for ( std::size_t edge = 0; edge < count; ++edge )
    {
        const auto next = (edge + 1u) % count;
        const auto direction = normalized[next] - normalized[edge];
        for ( std::size_t point = 0; point < count; ++point )
        {
            if ( point != edge && point != next
                && direction.Cross(normalized[point] - normalized[edge]).Dot(normal) <= epsilon )
            {
                refuse("archOccluderInvalid", "ARCH occlusion requires simple, strictly convex world faces");
            }
        }
    }
    for ( std::size_t point = 1; point + 1u < count; ++point )
    {
        triangles.push_back({ anchor, points[point], points[point + 1u] });
    }
}

std::vector<sed::level::ArchOccluderTriangle> worldOccluders(
    const sed::Level& level, ArchOperationBudget& budget)
{
    std::vector<sed::level::ArchOccluderTriangle> triangles;
    std::size_t surfaces = 0;
    for ( std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex )
    {
        charge(budget.remainingWorkUnits, 1u);
        const auto& sector = level.sectors[sectorIndex];
        if ( sector.surfaces.size() > kMaxWorldSurfaces - surfaces )
        {
            refuse("archBakeBudgetExceeded", "ARCH world surface budget exceeded");
        }
        surfaces += sector.surfaces.size();
        for ( std::size_t surfaceIndex = 0; surfaceIndex < sector.surfaces.size(); ++surfaceIndex )
        {
            charge(budget.remainingWorkUnits, 1u);
            const auto& surface = sector.surfaces[surfaceIndex];
            if ( surface.adjoin )
            {
                const auto peer = *surface.adjoin;
                if ( peer.sector < 0 || static_cast<std::size_t>(peer.sector) >= level.sectors.size()
                    || peer.surface < 0
                    || static_cast<std::size_t>(peer.surface) >= level.sectors[peer.sector].surfaces.size() )
                {
                    refuse("archOccluderInvalid", "ARCH world has an invalid adjoin reference");
                }
                const auto& other = level.sectors[peer.sector].surfaces[peer.surface];
                if ( other.adjoin != sed::SurfaceRef{ static_cast<std::int32_t>(sectorIndex),
                        static_cast<std::int32_t>(surfaceIndex) } )
                {
                    refuse("archOccluderInvalid", "ARCH world has a nonreciprocal adjoin");
                }
                const auto firstCount = surface.vertices.size();
                const auto secondCount = other.vertices.size();
                if ( firstCount < 3 || firstCount > kMaxFaceCorners
                    || secondCount < 3 || secondCount > kMaxFaceCorners )
                {
                    refuse("archOccluderInvalid", "ARCH adjoin faces require 3..4096 corners");
                }
                product(budget, firstCount + secondCount, firstCount + secondCount);
                if ( !sed::level::surfaceBoundariesMatchForDiagnostics(
                        sector, surface, level.sectors[peer.sector], other) )
                {
                    refuse("archOccluderInvalid", "ARCH adjoin boundaries do not match");
                }
                if ( (surface.adjoinFlags & sed::SAF_BlockLight) == 0 )
                {
                    continue;
                }
            }
            appendWorldFace(sector, surface, triangles, budget);
        }
    }
    return triangles;
}

} // namespace

bool tryApplyArchOperation(sed::Level& level, const Json& operation,
    const ArchModelResolver& resolver, const sed::assets::Templates* templates,
    ArchOperationBudget* sharedBudget)
{
    const auto* name = operation.find("op");
    if ( !name || name->type != Json::Type::String
        || (name->str != "bindThingArchitecture" && name->str != "rebindThingArchitecture"
            && name->str != "bakeThingArchitecture") )
    {
        return false;
    }
    const bool bind = name->str == "bindThingArchitecture";
    const bool reset = name->str == "rebindThingArchitecture";
    fields(operation, bind ? std::initializer_list<std::string_view>{ "op", "thing", "lightingIndex" }
        : reset ? std::initializer_list<std::string_view>{ "op", "thing", "policy" }
                : std::initializer_list<std::string_view>{ "op", "thing", "profile" });
    const auto thingIndex = index(operation, "thing");
    const auto lightingIndex = bind ? index(operation, "lightingIndex") : -1;
    auto profile = sed::level::ArchBakeProfile::SourcePointsV1;
    if ( reset && token(operation, "policy") != "reset" )
    {
        invalid("ARCH rebind policy must be 'reset'");
    }
    if ( !bind && !reset )
    {
        const auto requested = token(operation, "profile");
        if ( requested == "source-points-world-occluded-v1" )
        {
            profile = sed::level::ArchBakeProfile::SourcePointsWorldOccludedV1;
        }
        else if ( requested != "source-points-v1" )
        {
            invalid("Unsupported ARCH bake profile");
        }
    }
    // Validate the common MotS owner/index preconditions without changing it.
    checkPrepared(sed::level::prepareUnbindArch(level, thingIndex, templates));
    if ( bind && static_cast<std::size_t>(lightingIndex) >= level.motsArchLighting.size() )
    {
        refuse("archSourceInvalid", "ARCH source lighting index is invalid");
    }
    if ( !bind && !reset && level.lights.size() > kMaxLights )
    {
        refuse("archBakeBudgetExceeded", "ARCH baking admits at most 256 source lights");
    }
    ArchOperationBudget localBudget;
    auto& budget = sharedBudget ? *sharedBudget : localBudget;
    if ( budget.remainingWorkUnits > kMaxWorkUnits || budget.remainingModelBytes > kMaxModelBytes )
    {
        refuse("archBakeBudgetExceeded", "ARCH patch budgets may only be lowered");
    }
    const auto& thing = level.things[static_cast<std::size_t>(thingIndex)];
    const auto resolvedName = modelName(level, thing, templates);
    const auto model = admitModel(resolvedName, resolver, budget);
    charge(budget.remainingWorkUnits, level.motsArchLighting.size());
    charge(budget.remainingWorkUnits, level.motsArchAuthoring.size());
    charge(budget.remainingWorkUnits, level.motsArchLightingBakeBaselines.size());
    // Baselines use editor IDs, whereas this request selects a Thing by index.
    // A shared ID cannot identify whose retained automatic-bake history would
    // be cleared. Refuse only when this edit would erase an affected baseline.
    if ( std::any_of(level.motsArchLightingBakeBaselines.begin(),
            level.motsArchLightingBakeBaselines.end(), [&](const auto& baseline)
            {
                return baseline.thingId == thing.id;
            }) )
    {
        charge(budget.remainingWorkUnits, level.things.size());
        if ( std::any_of(level.things.begin(), level.things.end(), [&](const auto& owner)
            {
                return &owner != &thing && owner.id == thing.id;
            }) )
        {
            refuse("archBaselineOwnerAmbiguous",
                "ARCH automatic-bake baseline owner has a shared Thing ID");
        }
    }
    sed::level::PreparedArchAuthoring prepared;
    if ( bind )
    {
        validateSourceSamples(level, lightingIndex, model.topology, budget);
        prepared = sed::level::prepareBindArchSamples(level, thingIndex, lightingIndex,
            model.topology, resolvedName, model.resource.sha256Hex, templates);
    }
    else if ( reset )
    {
        // The generic reset prepares maximum counts. Require those maxima to
        // remain the exact geoset-zero shape accepted by ARCH validation.
        validateLodPrefixes(model.topology);
        prepared = sed::level::prepareResetArchBinding(level, thingIndex,
            model.topology, resolvedName, model.resource.sha256Hex, templates);
    }
    else
    {
        const auto points = worldSamples(thing, model, budget);
        std::size_t vertices = 0;
        for ( const auto& mesh : points )
        {
            vertices += mesh.size();
        }
        product(budget, vertices, level.lights.size());
        std::vector<sed::level::ArchOccluderTriangle> occluders;
        if ( profile == sed::level::ArchBakeProfile::SourcePointsWorldOccludedV1 )
        {
            occluders = worldOccluders(level, budget);
            product(budget, vertices * level.lights.size(), occluders.size());
        }
        prepared = sed::level::prepareBakeArchSamples(level, thingIndex,
            model.topology, points, level.lights, profile, occluders,
            resolvedName, model.resource.sha256Hex, templates);
    }
    checkPrepared(prepared);
    std::string topologyCode;
    if ( !sed::level::archTopologyAllowsBinding(prepared.newObject, model.topology, &topologyCode) )
    {
        refuse(topologyCode, "Prepared ARCH samples do not satisfy the shared all-LOD validator");
    }
    if ( !prepared.apply(level) )
    {
        throw AgentError{ kExitConflict, "ARCH owner changed before the prepared edit could apply",
            "archBindingStale" };
    }
    // An explicit edit establishes a new authored baseline for this owner.
    // Keeping an older automatic-bake baseline would restore pre-edit samples
    // on its next GUI bake. Erasure cannot allocate and follows successful
    // preparation/publication, so every refusal keeps all baselines intact.
    std::erase_if(level.motsArchLightingBakeBaselines, [&](const auto& baseline)
    {
        return baseline.thingId == thing.id;
    });
    return true;
}

} // namespace sed_agent
