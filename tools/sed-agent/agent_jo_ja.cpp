#include "agent_jo_ja.hpp"
#include "agent_jo_ja_overlay.hpp"
#include "agent_workspace.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/raven_lightmap.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <map>
#include <set>
#include <span>

namespace sed_agent
{
namespace
{
namespace fs = std::filesystem;
using namespace sed::assets;

[[noreturn]] void invalid(std::string message)
{
    throw AgentError{kExitUsage, std::move(message), "joJaInvalidRequest"};
}
void fields(const Json& object, std::initializer_list<std::string_view> names)
{
    if ( !object.isObject() )
    {
        invalid("Expected an object");
    }
    for ( const auto& field : object.object )
    {
        if ( std::find(names.begin(), names.end(), field.first) == names.end() )
        {
            invalid("Unknown JO/JA import field: " + field.first);
        }
    }
}
const Json& required(const Json& object, const char* key)
{
    if ( const Json* item = object.find(key) )
    {
        return *item;
    }
    invalid(std::string("Missing required field: ") + key);
}
std::string stringField(const Json& object, const char* key)
{
    const auto& value = required(object, key);
    if ( value.type != Json::Type::String || value.str.empty() || value.str.find('\0') != std::string::npos )
    {
        invalid(std::string(key) + " must be a nonempty string without NUL");
    }
    return value.str;
}
std::string lower(std::string text)
{
    for ( char& c : text )
    {
        if ( c >= 'A' && c <= 'Z' )
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return text;
}
std::string digest(std::string value)
{
    value = lower(std::move(value));
    if ( value.size() != 64 || value.find_first_not_of("0123456789abcdef") != std::string::npos )
    {
        invalid("SHA-256 must contain exactly 64 hexadecimal digits");
    }
    return value;
}
unsigned whole(const Json& object, const char* name, unsigned fallback, unsigned max)
{
    const Json* value = object.find(name);
    if ( !value )
    {
        return fallback;
    }
    if ( value->type != Json::Type::Number || !std::isfinite(value->number) || value->number < 1 ||
         value->number > max || std::floor(value->number) != value->number )
    {
        invalid(std::string(name) + " must be an integer from 1 to " + std::to_string(max));
    }
    return static_cast<unsigned>(value->number);
}
bool flag(const Json& object, const char* name)
{
    const Json* value = object.find(name);
    if ( !value )
    {
        return false;
    }
    if ( value->type != Json::Type::Bool )
    {
        invalid(std::string(name) + " must be boolean");
    }
    return value->boolean;
}
std::string capture(const Json& file, std::size_t limit, std::size_t& remaining)
{
    fields(file, {"path", "sha256"});
    const auto path = stringField(file, "path");
    const auto expected = digest(stringField(file, "sha256"));
    auto data = readFileBytes(path, std::min(remaining, limit));
    if ( sha256Hex(data) != expected )
    {
        throw AgentError{kExitConflict, "Source SHA-256 changed: " + path, "joJaSourceChanged"};
    }
    remaining -= data.size();
    return data;
}
std::span<const std::byte> asBytes(const std::string& data)
{
    return std::as_bytes(std::span(data));
}
Json numbers(const sed::TVector& point)
{
    Json out = Json::makeArray();
    for ( const auto value : {point.x, point.y, point.z} )
    {
        out.array.push_back(Json::makeNumber(value));
    }
    return out;
}
Json limitations(bool modelScene, bool sourceClips)
{
    Json out = Json::makeArray();
    if ( modelScene )
    {
        out.array.push_back(Json::makeString("nativeConvexSectorReconstruction"));
    }
    for ( const auto name : {"sourceVisibilityReproduction", "brushCollision", "movingSubmodels", "shaderStages",
                             "dynamicLightStyles", "gameplayLogic", "sourceAudio", "runtimeAcceptance"} )
    {
        out.array.push_back(Json::makeString(sourceClips && std::string_view(name) == "brushCollision" ?
                                             "fullSourceBrushCollision" : name));
    }
    return out;
}
Json envelope(const char* command, bool modelScene = false, bool sourceClips = false)
{
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(command));
    out.set("runtimeVerified", Json::makeBool(false));
    out.set("audioConverted", Json::makeBool(false));
    out.set("unsupported", limitations(modelScene, sourceClips));
    return out;
}

RavenVisibilityQuery visibilityQuery(const Json& value)
{
    fields(value, {"viewCluster", "offset"});
    const auto integer = [](const Json& n, int low, int high)
    {
        if ( n.type != Json::Type::Number || !std::isfinite(n.number) || n.number < low ||
             n.number > high || std::floor(n.number) != n.number )
        {
            invalid("Invalid source visibility cluster or page offset");
        }
        return static_cast<int>(n.number);
    };
    RavenVisibilityQuery query;
    query.viewCluster = integer(required(value, "viewCluster"), -1, 1048575);
    if ( const auto* offset = value.find("offset") )
    {
        query.offset = static_cast<std::size_t>(integer(*offset, 0, 2147483647));
    }
    return query;
}

const char* visibilityName(RavenPvsState state)
{
    switch ( state )
    {
        case RavenPvsState::PotentiallyVisible: return "potentially-visible";
        case RavenPvsState::Outside: return "outside-pvs";
        default: return "unknown";
    }
}

template <class Range>
Json numberArray(const Range& values)
{
    auto out = Json::makeArray();
    for ( auto value : values )
    {
        out.array.push_back(Json::makeNumber(value));
    }
    return out;
}

Json visibilityPage(const RavenVisibilityQuery& query, bool usable, std::size_t total, std::size_t returned)
{
    Json result = Json::makeObject();
    result.set("viewCluster", Json::makeNumber(query.viewCluster));
    result.set("rowUsable", Json::makeBool(usable));
    result.set("unknownRemainsVisible", Json::makeBool(true));
    result.set("total", Json::makeNumber(total));
    result.set("offset", Json::makeNumber(query.offset));
    result.set("nextOffset", query.offset < total && returned < total - query.offset
                                ? Json::makeNumber(query.offset + returned) : Json{});
    return result;
}

JoJaImportOptions optionsFrom(const Json& request)
{
    fields(request, {"schemaVersion", "source", "target", "levelName", "scale", "patchSubdivisions",
                     "trianglesPerModel", "maxTriangles", "maxModels", "allowPlaceholders", "renderMeshCollision",
                     "audioPolicy", "sourceProfile", "materials", "palette", "skipShaders", "geometryMode", "maxSectors", "maxFragments", "reimport",
                     "meshOptimization", "sourceVisibility", "worldModelRenderPass", "sourcePlayerClip", "movementBarrierBrushes"});
    (void)whole(request, "schemaVersion", 1, 1);
    JoJaImportOptions out;
    if ( const auto* visibility = request.find("sourceVisibility") )
    {
        out.sourceVisibility = visibilityQuery(*visibility);
    }
    if ( request.find("sourceProfile") )
    {
        const auto value = stringField(request, "sourceProfile");
        if ( value == "jo-mp" )
        {
            out.sourceProfile = JoJaSourceProfile::JediOutcastMultiplayer;
        }
        else if ( value == "ja-mp" )
        {
            out.sourceProfile = JoJaSourceProfile::JediAcademyMultiplayer;
        }
        else if ( value != "legacy" )
        {
            invalid("sourceProfile must be jo-mp, ja-mp or legacy");
        }
    }
    if ( request.find("target") )
    {
        const auto target = stringField(request, "target");
        if ( target == "mots" )
        {
            out.target = sed::ProjectType::MOTS;
        }
        else if ( target != "jk" )
        {
            invalid("target must be jk or mots");
        }
    }
    if ( request.find("levelName") )
    {
        out.levelName = stringField(request, "levelName");
    }
    if ( const Json* scale = request.find("scale") )
    {
        if ( scale->type != Json::Type::Number || !std::isfinite(scale->number) ||
             !(scale->number > 0 && scale->number <= 1) )
        {
            invalid("scale must be finite in (0, 1]");
        }
        out.scale = scale->number;
    }
    if ( request.find("geometryMode") )
    {
        const auto mode = stringField(request, "geometryMode");
        if ( mode == "hybrid-sectors" )
        {
            out.geometryMode = JoJaGeometryMode::HybridSectors;
        }
        else if ( mode != "model-scene" )
        {
            invalid("geometryMode must be model-scene or hybrid-sectors");
        }
    }
    if (request.find("sourcePlayerClip"))
    {
        const auto mode = stringField(request, "sourcePlayerClip");
        if (mode == "standable")
            out.sourcePlayerClip = JoJaPlayerClipMode::Standable;
        else if (mode == "movementBarrier")
            out.sourcePlayerClip = JoJaPlayerClipMode::MovementBarrier;
        else if (mode != "omit")
            invalid("sourcePlayerClip must be omit, standable or movementBarrier");
    }
    if (const auto* ids = request.find("movementBarrierBrushes"))
    {
        if (ids->type != Json::Type::Array || ids->array.size() > 512)
            invalid("movementBarrierBrushes must be an array of at most 512 brush IDs");
        for (const auto& id : ids->array)
        {
            if (id.type != Json::Type::Number || !std::isfinite(id.number) || id.number < 0 ||
                id.number > 1048575 || std::floor(id.number) != id.number)
                invalid("movementBarrierBrushes entries must be nonnegative integer source brush IDs");
            out.movementBarrierBrushes.push_back(static_cast<std::size_t>(id.number));
        }
    }
    if ( request.find("worldModelRenderPass") )
    {
        const auto pass = stringField(request, "worldModelRenderPass");
        if ( pass == "ordinaryThing" )
            out.worldModelRenderPass = JoJaWorldModelRenderPass::OrdinaryThing;
        else if ( pass != "levelGeometry" )
            invalid("worldModelRenderPass must be levelGeometry or ordinaryThing");
    }
    if ( request.find("meshOptimization") )
    {
        const auto level = stringField(request, "meshOptimization");
        if ( level == "strict" )
        {
            out.meshOptimization = MeshOptimization::Strict;
        }
        else if ( level == "loose" )
        {
            out.meshOptimization = MeshOptimization::Loose;
        }
        else if ( level != "off" )
        {
            invalid("meshOptimization must be off, strict or loose");
        }
    }
    const bool hybrid = out.geometryMode == JoJaGeometryMode::HybridSectors;
    out.patchSubdivisions = whole(request, "patchSubdivisions", out.patchSubdivisions, 16);
    out.trianglesPerModel = whole(request, "trianglesPerModel", out.trianglesPerModel, hybrid ? 479 : 170);
    out.maxTriangles = whole(request, "maxTriangles", static_cast<unsigned>(out.maxTriangles), hybrid ? 250000 : 100000);
    out.maxSectors = whole(request, "maxSectors", static_cast<unsigned>(out.maxSectors), 96);
    out.maxFragments = whole(request, "maxFragments", static_cast<unsigned>(out.maxFragments), 1000000);
    out.maxModels = whole(request, "maxModels", static_cast<unsigned>(out.maxModels), 512);
    out.allowPlaceholders = flag(request, "allowPlaceholders");
    out.renderMeshCollision = flag(request, "renderMeshCollision");
    if ( request.find("audioPolicy") )
    {
        const auto policy = stringField(request, "audioPolicy");
        if ( policy == "source-import" )
        {
            out.audioPolicy = JoJaAudioPolicy::SourceImport;
        }
        else if ( policy != "stock-equivalents" )
        {
            invalid("audioPolicy must be stock-equivalents or source-import");
        }
    }
    if ( const Json* skips = request.find("skipShaders") )
    {
        if ( skips->type != Json::Type::Array || skips->array.size() > kJoJaMaxMaterials )
        {
            invalid("skipShaders requires at most 256 names");
        }
        std::set<std::string> seen;
        for ( const auto& name : skips->array )
        {
            if ( name.type != Json::Type::String || name.str.empty() || name.str.size() > 63 ||
                 name.str.find('\0') != std::string::npos || !seen.insert(name.str).second )
            {
                invalid("skipShaders requires unique shader names of 1 to 63 bytes");
            }
            out.skipShaders.push_back(name.str);
        }
    }
    return out;
}

Json importReport(const JoJaImportResult& result, const JoJaImportOptions& options, bool apply)
{
    const bool modelScene = options.geometryMode == JoJaGeometryMode::ModelScene;
    Json out = envelope(apply ? "jo-ja-apply" : "jo-ja-plan", modelScene, options.sourcePlayerClip != JoJaPlayerClipMode::Omit);
    out.set("geometryMode", Json::makeString(modelScene ? "model-scene" : "hybrid-sectors"));
    out.set("nativeBoundaryCollision", Json::makeBool(true));
    out.set("renderMeshCollision", Json::makeBool(options.renderMeshCollision));
    out.set("sectors", Json::makeNumber(result.sectors));
    out.set("portalPairs", Json::makeNumber(result.portalPairs));
    out.set("nativeFaces", Json::makeNumber(result.nativeFaces));
    out.set("residualFaces", Json::makeNumber(result.residualFaces));
    out.set("residualTriangles", Json::makeNumber(result.residualTriangles));
    out.set("splitSourceTriangles", Json::makeNumber(result.splitSourceTriangles));
    out.set("untrimmedBoundaryPlanes", Json::makeNumber(result.untrimmedBoundaryPlanes));
    out.set("nativePromotionFallbacks", Json::makeNumber(result.nativePromotionFallbacks));
    out.set("targetCollapsedFragments", Json::makeNumber(result.targetCollapsedFragments));
    out.set("targetCollapsedArea", Json::makeNumber(result.targetCollapsedArea));
    out.set("sourceArea", Json::makeNumber(result.sourceArea));
    out.set("outputArea", Json::makeNumber(result.outputArea));
    out.set("maximumRelativeCoverageError", Json::makeNumber(result.maximumRelativeCoverageError));
    if (options.sourcePlayerClip != JoJaPlayerClipMode::Omit)
    {
        out.set("sourcePlayerClip", Json::makeString(joJaPlayerClipName(options.sourcePlayerClip)));
        auto sources = Json::makeArray();
        for (const auto& clip : result.playerClips)
        {
            auto row = Json::makeObject();
            row.set("brush", Json::makeNumber(clip.brush));
            row.set("mode", Json::makeString(joJaPlayerClipName(clip.mode)));
            row.set("model", Json::makeString(clip.model));
            row.set("thing", Json::makeNumber(clip.thing));
            row.set("faces", Json::makeNumber(clip.faces));
            if ( clip.footsteps )
            {
                const auto& steps = *clip.footsteps;
                auto contact = Json::makeObject();
                contact.set("cue", Json::makeString(steps.cue));
                contact.set("status", Json::makeString(steps.status()));
                contact.set("needsReview", Json::makeBool(steps.needsReview));
                auto areas = Json::makeObject();
                for ( const auto& [cue, area] : steps.areaByCue )
                {
                    areas.set(cue, Json::makeNumber(area));
                }
                contact.set("areaByCueJKU2", std::move(areas));
                row.set("footsteps", std::move(contact));
            }
            sources.array.push_back(std::move(row));
        }
        out.set("playerClips", std::move(sources));
    }
    if ( options.worldModelRenderPass == JoJaWorldModelRenderPass::OrdinaryThing )
    {
        out.set("worldModelRenderPass", Json::makeString("ordinaryThing"));
    }
    out.set("meshOptimization", Json::makeString(meshOptimizationName(options.meshOptimization)));
    if ( options.meshOptimization != MeshOptimization::Off )
    {
        const auto change = [](std::size_t before, std::size_t after)
        {
            Json row = Json::makeObject();
            row.set("before", Json::makeNumber(before));
            row.set("after", Json::makeNumber(after));
            return row;
        };
        out.set("optimizedVertices", change(result.optimizedVerticesIn, result.optimizedVerticesOut));
        out.set("optimizedFaces", change(result.optimizedFacesIn, result.optimizedFacesOut));
        out.set("worstNormalTurnDegrees", Json::makeNumber(result.worstNormalTurnDegrees));
        if ( options.meshOptimization == MeshOptimization::Loose )
        {
            out.set("straightCornersRemoved", Json::makeNumber(result.straightCornersRemoved));
            out.set("fanTriangles", change(result.optimizedFanTrianglesIn, result.optimizedFanTrianglesOut));
        }
    }
    if ( result.sourceVisibility )
    {
        const auto& report = *result.sourceVisibility;
        auto page = visibilityPage(*options.sourceVisibility, report.rowUsable, report.total, report.page.size());
        page.set("outsidePvs", Json::makeNumber(report.outside));
        page.set("unknown", Json::makeNumber(report.unknown));
        auto targets = Json::makeArray();
        for ( const auto& entry : report.page )
        {
            auto row = Json::makeObject();
            row.set("sector", Json::makeNumber(entry.sector));
            if ( entry.model )
            {
                row.set("model", Json::makeString(result.models.at(*entry.model).name));
                row.set("thing", Json::makeNumber(entry.index));
                row.set("mesh", Json::makeNumber(0));
            }
            else
            {
                row.set("surface", Json::makeNumber(entry.index));
            }
            row.set("sourceSurfaces", numberArray(entry.sourceSurfaces));
            row.set("state", Json::makeString(visibilityName(entry.state)));
            targets.array.push_back(std::move(row));
        }
        page.set("targets", std::move(targets));
        out.set("sourceVisibility", std::move(page));
    }
    out.set("written", Json::makeBool(apply));
    out.set(
        "audioPolicy",
        Json::makeString(options.audioPolicy == JoJaAudioPolicy::SourceImport ? "source-import" : "stock-equivalents"));
    out.set("target", Json::makeString(options.target == sed::ProjectType::MOTS ? "mots" : "jk"));
    out.set("sourceProfile", Json::makeString(joJaSourceProfileName(options.sourceProfile)));
    out.set("sourceSha256", Json::makeString(result.sourceSha256));
    out.set("outputSha256", Json::makeString(result.outputSha256));
    out.set("manifestSha256", Json::makeString(result.manifestSha256));
    out.set("outputBytes", Json::makeNumber(result.archive.outputData.size()));
    out.set("report", Json::makeString(result.report));
    out.set("triangles", Json::makeNumber(result.triangles));
    out.set("patches", Json::makeNumber(result.patches));
    out.set("skippedSurfaces", Json::makeNumber(result.skippedSurfaces));
    out.set("omittedSubmodels", Json::makeNumber(result.omittedSubmodels));
    out.set("sourceEntities", Json::makeNumber(result.sourceEntities));
    out.set("sourceBrushes", Json::makeNumber(result.sourceBrushes));
    out.set("playerStarts", Json::makeNumber(result.playerStarts));
    out.set("lightmapCorners", Json::makeNumber(result.lightmapCorners));
    out.set("vertexLitCorners", Json::makeNumber(result.vertexLitCorners));
    Json models = Json::makeArray();
    for ( const auto& model : result.models )
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(model.name));
        row.set("triangles", Json::makeNumber(model.triangles));
        row.set("sector", Json::makeNumber(model.sector));
        row.set("position", numbers(model.position));
        row.set("radius", Json::makeNumber(model.radius));
        row.set("footstep", Json::makeString(model.footstep));
        Json sources = Json::makeArray();
        for ( auto id : model.sourceSurfaces )
        {
            sources.array.push_back(Json::makeNumber(id));
        }
        row.set("sourceSurfaces", std::move(sources));
        models.array.push_back(std::move(row));
    }
    out.set("models", std::move(models));
    Json materials = Json::makeArray();
    for ( const auto& material : result.materials )
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(material.name));
        row.set("shader", Json::makeString(material.shader));
        row.set("sha256", Json::makeString(material.sha256));
        row.set("width", Json::makeNumber(material.width));
        row.set("height", Json::makeNumber(material.height));
        row.set("placeholder", Json::makeBool(material.placeholder));
        row.set("sourceMaterial", Json::makeNumber(material.sourceMaterial));
        row.set("footstep", Json::makeString(material.footstep));
        if ( !material.sourceFootstep.empty() )
        {
            row.set("sourceFootstep", Json::makeString(material.sourceFootstep));
            row.set("noStepsFlag", Json::makeBool(material.noStepsFlag));
        }
        row.set("footstepNeedsReview", Json::makeBool(material.footstepNeedsReview));
        materials.array.push_back(std::move(row));
    }
    out.set("materials", std::move(materials));
    Json entries = Json::makeArray();
    for ( const auto& entry : result.archive.entries )
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(entry.name));
        row.set("bytes", Json::makeNumber(entry.bytes));
        row.set("sha256", Json::makeString(entry.sha256));
        entries.array.push_back(std::move(row));
    }
    out.set("entries", std::move(entries));
    Json warnings = Json::makeArray();
    for ( const auto& warning : result.warnings )
    {
        warnings.array.push_back(Json::makeString(warning));
    }
    out.set("warnings", std::move(warnings));
    return out;
}
} // namespace

namespace
{
std::size_t indexArgument(std::string_view text)
{
    std::size_t index{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);
    if (error != std::errc{} || end != text.data() + text.size())
        invalid("An inspection index must be an unsigned decimal integer");
    return index;
}

Json lightmapWitness(const std::optional<RavenLightmapWitness>& witness)
{
    if ( !witness )
    {
        return {};
    }
    auto out = Json::makeObject();
    out.set("triangle", Json::makeNumber(witness->triangle));
    out.set("barycentric", numberArray(witness->barycentric));
    out.set("sourceRgb", numberArray(witness->sourceRgb));
    out.set("approximationRgb", numberArray(witness->approximationRgb));
    out.set("maxChannelError", Json::makeNumber(witness->maxChannelError));
    return out;
}

Json lightmapFidelity(const RavenBsp& bsp, std::size_t surface, std::size_t patchSubdivisions)
{
    if ( surface >= bsp.surfaces.size() || patchSubdivisions < 1 || patchSubdivisions > 16 )
    {
        invalid("Choose an existing source surface and patch subdivisions from 1 to 16");
    }
    RavenLightmapFidelity result;
    try
    {
        result = inspectRavenLightmap(bsp, surface, static_cast<std::uint32_t>(patchSubdivisions));
    }
    catch ( const std::runtime_error& e )
    {
        throw AgentError{kExitRefusal, e.what(), "joJaLightmapRefused"};
    }
    const auto atlas = bsp.surfaces[surface].lightmapIndex;
    auto out = Json::makeObject();
    out.set("sourceSurface", Json::makeNumber(surface));
    out.set("firstAtlas", Json::makeNumber(atlas));
    out.set("status", Json::makeString(atlas < 0 ? "no-first-atlas" :
                                       result.triangleCount == 0 ? "no-triangles" : "sampled"));
    out.set("patchSubdivisions", Json::makeNumber(patchSubdivisions));
    out.set("gridSteps", Json::makeNumber(kRavenLightmapGridSteps));
    out.set("probesPerTriangle", Json::makeNumber(kRavenLightmapProbesPerTriangle));
    out.set("sampleLimit", Json::makeNumber(kRavenLightmapProbesPerTriangle * kRavenMaxTrianglesPerSurface));
    out.set("sampleCount", Json::makeNumber(result.triangleCount * kRavenLightmapProbesPerTriangle));
    out.set("sampledTriangles", Json::makeNumber(result.triangleCount));
    out.set("rgbInterpolation", lightmapWitness(result.rgbInterpolation));
    out.set("cornerScalar", lightmapWitness(result.cornerScalar));
    return out;
}

Json brushConstraints(const RavenBsp& bsp, std::size_t index, std::size_t offset)
{
    if (index >= bsp.brushes.size())
        invalid("Source brush index out of range");
    const auto& brush = bsp.brushes[index];
    const auto& shader = bsp.shaders[brush.shader];
    const auto sideCount = static_cast<std::size_t>(brush.sideCount);
    if (offset > sideCount)
        invalid("Brush side offset out of range");
    Json out = Json::makeObject();
    out.set("brush", Json::makeNumber(index));
    out.set("shader", Json::makeNumber(brush.shader));
    out.set("shaderName", Json::makeString(shader.name));
    out.set("contents", Json::makeNumber(shader.contents));
    out.set("solid", Json::makeBool(shader.contents & 0x1u));
    out.set("water", Json::makeBool(shader.contents & 0x4u));
    out.set("playerClip", Json::makeBool(shader.contents & 0x10u));
    out.set("shotClip", Json::makeBool(shader.contents & 0x80u));
    out.set("noShot", Json::makeBool(shader.contents & 0x00200000u));
    out.set("targetEquivalence", Json::makeString("unverified"));
    out.set("firstSide", Json::makeNumber(brush.firstSide));
    out.set("sideCount", Json::makeNumber(sideCount));
    const auto first = bsp.brushSides.begin() + brush.firstSide;
    out.set("hasLinkedDrawSurface", Json::makeBool(std::any_of(first, first + sideCount,
        [](const RavenBspBrushSide& side) { return side.surface >= 0; })));
    const auto end = std::min(sideCount, offset + 128);
    auto sides = Json::makeArray();
    for (auto i = offset; i < end; ++i)
    {
        const auto& side = first[i];
        const auto& plane = bsp.planes[side.plane];
        Json row = Json::makeObject();
        row.set("index", Json::makeNumber(brush.firstSide + i));
        row.set("plane", Json::makeNumber(side.plane));
        row.set("normal", numbers(plane.normal));
        row.set("distance", Json::makeNumber(plane.distance));
        row.set("sideShader", Json::makeNumber(side.shader));
        row.set("drawSurface", side.surface < 0 ? Json{} : Json::makeNumber(side.surface));
        sides.array.push_back(std::move(row));
    }
    out.set("offset", Json::makeNumber(offset));
    out.set("nextOffset", end == sideCount ? Json{} : Json::makeNumber(end));
    out.set("sides", std::move(sides));
    return out;
}
} // namespace

Json joJaInspect(const std::vector<std::string>& args)
{
    if (args.size() > 1 && args[1] == "--target")
        return inspectTargetCollision(args);
    const bool brush = (args.size() == 3 || args.size() == 4) && args[1] == "--brush";
    const bool visibility = (args.size() == 3 || args.size() == 4) && args[1] == "--visibility";
    const bool lightmap = (args.size() == 3 || args.size() == 4) && args[1] == "--lightmap";
    if ( (!brush && !visibility && !lightmap && args.size() != 1) ||
         args[0].empty() || args[0].find('\0') != std::string::npos )
    {
        invalid("jo-ja-inspect requires <source.bsp> [--brush index [side-offset] | "
                "--visibility cluster [surface-offset] | --lightmap surface [patch-subdivisions]]");
    }
    const auto source = readFileBytes(args[0], kRavenBspMaxBytes);
    RavenBsp bsp;
    try
    {
        bsp = parseRavenBsp(asBytes(source));
    }
    catch ( const std::runtime_error& e )
    {
        throw AgentError{kExitInput, e.what(), "joJaInvalidSource"};
    }
    Json out = envelope("jo-ja-inspect");
    out.set("sourceSha256", Json::makeString(sha256Hex(source)));
    out.set("sourceBytes", Json::makeNumber(source.size()));
    if ( lightmap )
    {
        out.set("sourceLightmap", lightmapFidelity(bsp, indexArgument(args[2]),
                                                   args.size() == 4 ? indexArgument(args[3]) : 4));
    }
    if (brush)
        out.set("collisionBrush", brushConstraints(bsp, indexArgument(args[2]), args.size() == 4 ? indexArgument(args[3]) : 0));
    out.set("format", Json::makeString("RBSP v1"));
    out.set("sourceBrushes", Json::makeNumber(bsp.brushCount));
    out.set("sourceEntities", Json::makeNumber(bsp.entities.size()));
    out.set("sourceNodes", Json::makeNumber(bsp.nodes.size()));
    out.set("sourceLeaves", Json::makeNumber(bsp.leaves.size()));
    out.set("visibilityClusters", Json::makeNumber(bsp.visibility.clusterCount));
    const auto& world = bsp.models.at(0);
    if ( visibility )
    {
        auto arguments = Json::makeObject();
        arguments.set("viewCluster", args[2] == "-1" ? Json::makeNumber(-1) : Json::makeNumber(indexArgument(args[2])));
        if ( args.size() == 4 )
        {
            arguments.set("offset", Json::makeNumber(indexArgument(args[3])));
        }
        const auto query = visibilityQuery(arguments);
        const auto view = ravenPvsView(bsp, query.viewCluster, query.offset);
        auto page = visibilityPage(query, view.rowUsable, static_cast<std::size_t>(world.surfaceCount), view.ownerPage.size());
        auto rows = Json::makeArray();
        for ( std::size_t i = 0; i < view.ownerPage.size(); ++i )
        {
            const auto id = static_cast<std::size_t>(world.firstSurface) + query.offset + i;
            const auto& owners = view.ownerPage[i];
            auto row = Json::makeObject();
            row.set("sourceSurface", Json::makeNumber(id));
            row.set("clusters", numberArray(owners.clusters));
            row.set("negativeClusterOwner", Json::makeBool(owners.unknownCluster));
            row.set("clustersTruncated", Json::makeBool(owners.truncated));
            row.set("state", Json::makeString(visibilityName(view.surfaces.at(id))));
            rows.array.push_back(std::move(row));
        }
        page.set("surfaces", std::move(rows));
        out.set("sourceVisibility", std::move(page));
    }
    std::map<int, std::size_t> shaders;
    std::size_t patches = 0;
    for ( int offset = 0; offset < world.surfaceCount; ++offset )
    {
        const auto& surface = bsp.surfaces[static_cast<std::size_t>(world.firstSurface + offset)];
        ++shaders[surface.shader];
        if ( surface.type == 2 )
        {
            ++patches;
        }
    }
    out.set("patches", Json::makeNumber(patches));
    out.set("worldSurfaces", Json::makeNumber(world.surfaceCount));
    auto rows = Json::makeArray();
    for ( const auto& [index, count] : shaders )
    {
        const auto& shader = bsp.shaders[static_cast<std::size_t>(index)];
        Json row = Json::makeObject();
        row.set("index", Json::makeNumber(index));
        row.set("name", Json::makeString(shader.name));
        row.set("surfaceFlags", Json::makeNumber(shader.surfaceFlags));
        row.set("sourceMaterial", Json::makeNumber(shader.surfaceFlags & 0x1fu));
        row.set("noSteps", Json::makeBool((shader.surfaceFlags & 0x00400000u) != 0));
        row.set("metalSteps", Json::makeBool((shader.surfaceFlags & 0x8000u) != 0));
        row.set("contents", Json::makeNumber(shader.contents));
        row.set("surfaceCount", Json::makeNumber(count));
        rows.array.push_back(std::move(row));
    }
    out.set("worldShaders", std::move(rows));
    // Brush contents come from the volume shader, including invisible clip brushes.
    std::map<int, std::pair<std::size_t, std::size_t>> brushShaders;
    for (std::size_t i = 0; i < bsp.brushes.size(); ++i)
    {
        auto [group, inserted] = brushShaders.try_emplace(bsp.brushes[i].shader, i, 0);
        ++group->second.second;
    }
    auto collisionShaders = Json::makeArray();
    for (const auto& [shaderIndex, group] : brushShaders)
    {
        const auto& shader = bsp.shaders[shaderIndex];
        auto row = Json::makeObject();
        row.set("shader", Json::makeNumber(shaderIndex));
        row.set("shaderName", Json::makeString(shader.name));
        row.set("contents", Json::makeNumber(shader.contents));
        row.set("firstBrush", Json::makeNumber(group.first));
        row.set("brushCount", Json::makeNumber(group.second));
        collisionShaders.array.push_back(std::move(row));
    }
    out.set("collisionBrushShaders", std::move(collisionShaders));
    Json submodels = Json::makeArray();
    for ( std::size_t i = 1; i < bsp.models.size(); ++i )
    {
        const auto& model = bsp.models[i];
        Json row = Json::makeObject();
        row.set("index", Json::makeNumber(i));
        row.set("minimum", numbers(model.minimum));
        row.set("maximum", numbers(model.maximum));
        row.set("firstSurface", Json::makeNumber(model.firstSurface));
        row.set("surfaces", Json::makeNumber(model.surfaceCount));
        row.set("brushes", Json::makeNumber(model.brushCount));
        submodels.array.push_back(std::move(row));
    }
    out.set("submodels", std::move(submodels));
    std::map<std::string, std::size_t> classes;
    for ( const auto& entity : bsp.entities )
    {
        auto field = std::find_if(entity.fields.begin(), entity.fields.end(),
                                  [](const auto& pair) { return pair.first == "classname"; });
        ++classes[field == entity.fields.end() ? "(unspecified)" : field->second];
    }
    Json counts = Json::makeObject();
    for ( const auto& [name, count] : classes )
    {
        counts.set(name, Json::makeNumber(count));
    }
    out.set("entityClasses", std::move(counts));
    return out;
}

static JoJaImportResult generate(const Json& request, JoJaImportOptions& options)
{
    std::size_t remaining = kJoJaMaxCapturedInputBytes;
    const auto source = capture(required(request, "source"), kRavenBspMaxBytes, remaining);
    if ( const auto* materials = request.find("materials") )
    {
        if ( materials->type != Json::Type::Array || materials->array.size() > kJoJaMaxMaterials )
        {
            invalid("materials requires at most 256 mappings");
        }
        std::size_t matRemaining = kJoJaMaxCapturedMaterialBytes;
        for ( const auto& mapping : materials->array )
        {
            fields(mapping, {"shader", "name", "source"});
            options.materials.push_back(
                {stringField(mapping, "shader"), stringField(mapping, "name"),
                 capture(required(mapping, "source"), std::min(kJoJaMaxMaterialBytes, matRemaining), remaining)});
            matRemaining -= options.materials.back().bytes.size();
        }
    }
    if ( const auto* palette = request.find("palette") )
    {
        options.palette = capture(*palette, kJoJaMaxPaletteBytes, remaining);
    }
    JoJaImportResult result;
    try
    {
        result = prepareJoJaImport(asBytes(source), options);
    }
    catch ( const std::exception& e )
    {
        const bool unavailable = std::string_view(e.what()).starts_with("joJaSourceAudioUnavailable:");
        throw AgentError{kExitRefusal, e.what(), unavailable ? "joJaSourceAudioUnavailable" : "joJaImportRefused"};
    }
    return result;
}

Json joJaPlanOrApply(const std::vector<std::string>& args, bool apply)
{
    if ( args.size() != (apply ? 4u : 1u) )
    {
        invalid(apply ? "jo-ja-apply requires <request.json|-> <new.gob|new.goo> <expected-output-sha256> "
                        "<expected-manifest-sha256>"
                      : "jo-ja-plan requires <request.json|->");
    }
    const Json request = readRequest(args[0]);
    auto options = optionsFrom(request);
    std::string expectedOutput, expectedManifest;
    if ( apply )
    {
        const auto ext = lower(fs::path(args[1]).extension().string());
        if ( args[1].find('\0') != std::string::npos ||
             ext != (options.target == sed::ProjectType::MOTS ? ".goo" : ".gob") )
        {
            invalid("Archive extension must match the target (.gob for JK, .goo for MotS)");
        }
        expectedOutput = digest(args[2]);
        expectedManifest = digest(args[3]);
    }
    const auto* record = request.find("reimport");
    std::optional<JoJaImportResult> baseline;
    std::map<std::string, std::string> baselineLevels;
    if (record)
    {
        fields(*record, { "baselineRecipe", "baselineOutputSha256", "authoredArchive", "overrides" });
        const auto& previous = required(*record, "baselineRecipe");
        if (previous.find("reimport"))
            invalid("A baseline recipe must describe an original generator run");
        auto priorOptions = optionsFrom(previous);
        if (priorOptions.target != options.target || priorOptions.levelName != options.levelName)
            invalid("Reimport requires the same target game and level name as its baseline");
        baseline = generate(previous, priorOptions);
        if (baseline->outputSha256 != digest(stringField(*record, "baselineOutputSha256")))
            throw AgentError { kExitConflict, "The baseline recipe did not reproduce the recorded archive.",
                "joJaReimportBaselineChanged" };
        baselineLevels = takeGeneratedLevels(*baseline);
    }
    auto result = generate(request, options);
    const auto generatedOutputSha256 = result.outputSha256;
    Json overlay;
    if (record)
    {
        std::size_t remaining = kRuntimeArchiveMaxBytes;
        auto authored = capture(required(*record, "authoredArchive"), kRuntimeArchiveMaxBytes, remaining);
        try
        {
            overlay = applyJoJaOverlays(
                *baseline, result, std::move(authored), required(*record, "overrides"), baselineLevels);
        }
        catch (const std::exception& e)
        {
            throw AgentError { kExitRefusal, e.what(), "joJaOverlayArchiveInvalid" };
        }
    }
    Json out = importReport(result, options, apply);
    if ( result.sourceVisibility )
    {
        out.object.at("sourceVisibility").set("generatedOutputSha256", Json::makeString(generatedOutputSha256));
    }
    if (overlay.isObject())
        out.set("reimport", std::move(overlay));
    if ( apply )
    {
        if ( expectedOutput != result.outputSha256 || expectedManifest != result.manifestSha256 )
        {
            throw AgentError{kExitConflict, "Reviewed output or manifest SHA-256 changed; plan again",
                             "joJaReviewChanged"};
        }
        // The workspace tracks every readFileBytes/readRequest above and re-reads
        // each source before its create-only atomic publisher commits the output.
        writeNewFileAtomic(args[1], result.archive.outputData);
        out.set("output", Json::makeString(args[1]));
    }
    return out;
}
} // namespace sed_agent
