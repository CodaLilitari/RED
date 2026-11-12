#include "agent_model_lighting.hpp"

#include "agent_workspace.hpp"
#include "sed/assets/model_lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string_view>

namespace sed_agent
{
namespace
{

using sed::assets::ModelLightingLimits;
using sed::assets::ModelLightingOptions;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "modelLightingInvalidRequest" };
}

void fields(const Json& value, const std::initializer_list<std::string_view> allowed,
    const std::string& context)
{
    if ( !value.isObject() )
    {
        invalid(context + " must be an object");
    }
    for ( const auto& [key, item] : value.object )
    {
        (void)item;
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            invalid(context + ": unknown field '" + key + "'");
        }
    }
}

const Json& required(const Json& value, const char* name)
{
    const Json* item = value.find(name);
    if ( !item )
    {
        invalid(std::string("Missing required field '") + name + "'");
    }
    return *item;
}

std::string stringValue(const Json& value, const char* name)
{
    if ( value.type != Json::Type::String )
    {
        invalid(std::string(name) + " must be a string");
    }
    return value.str;
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

std::string hashValue(const Json& value, const char* name)
{
    const std::string hash = lower(stringValue(value, name));
    if ( hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](const char character)
        {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        }) )
    {
        invalid(std::string(name) + " must contain exactly 64 hexadecimal characters");
    }
    return hash;
}

double numberValue(const Json& value, const char* name, const double minimum, const double maximum)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || value.number < minimum || value.number > maximum )
    {
        invalid(std::string(name) + " is outside its finite numeric range");
    }
    return value.number;
}

sed::TVector vectorValue(const Json& value, const char* name, const double minimum, const double maximum)
{
    if ( value.type != Json::Type::Array || value.array.size() != 3 )
    {
        invalid(std::string(name) + " must contain exactly three numbers");
    }
    return { numberValue(value.array[0], name, minimum, maximum),
        numberValue(value.array[1], name, minimum, maximum),
        numberValue(value.array[2], name, minimum, maximum) };
}

sed::ColorF colorValue(const Json& value, const char* name)
{
    const sed::TVector vector = vectorValue(value, name, 0.0, 1.0);
    return { static_cast<float>(vector.x), static_cast<float>(vector.y),
        static_cast<float>(vector.z), 1.0f };
}

bool boolValue(const Json& value, const char* name)
{
    if ( value.type != Json::Type::Bool )
    {
        invalid(std::string(name) + " must be a Boolean");
    }
    return value.boolean;
}

struct LimitField
{
    const char* name;
    std::size_t ModelLightingLimits::* member;
};

constexpr LimitField kLimitFields[]{
    { "sourceBytes", &ModelLightingLimits::sourceBytes },
    { "lines", &ModelLightingLimits::lines },
    { "tokensPerLine", &ModelLightingLimits::tokensPerLine },
    { "geosets", &ModelLightingLimits::geosets },
    { "meshes", &ModelLightingLimits::meshes },
    { "vertices", &ModelLightingLimits::vertices },
    { "triangles", &ModelLightingLimits::triangles },
    { "hierarchyNodes", &ModelLightingLimits::hierarchyNodes },
    { "lights", &ModelLightingLimits::lights },
    { "workUnits", &ModelLightingLimits::workUnits },
};

ModelLightingOptions parseOptions(const Json& request)
{
    fields(request, { "schemaVersion", "inputSha256", "profile", "blend", "ambient", "lights",
        "selfOcclusion", "shadowBias", "limits" }, "Model lighting request");
    if ( numberValue(required(request, "schemaVersion"), "schemaVersion", 1, 1) != 1 )
    {
        invalid("schemaVersion must be 1");
    }
    (void)hashValue(required(request, "inputSha256"), "inputSha256");
    ModelLightingOptions options;
    const std::string profile = stringValue(required(request, "profile"), "profile");
    if ( profile == "jk" || profile == "mots" )
    {
        options.profile = sed::assets::ModelLightingProfile::ClassicScalar;
    }
    else if ( profile == "ijim" )
    {
        options.profile = sed::assets::ModelLightingProfile::IjimRgb;
    }
    else
    {
        invalid("profile must be 'jk', 'mots' or 'ijim'; DroidWorks is not verified by this workflow");
    }
    if ( const Json* blend = request.find("blend") )
    {
        const std::string name = stringValue(*blend, "blend");
        if ( name != "replace" && name != "add" )
        {
            invalid("blend must be 'replace' or 'add'");
        }
        options.blend = name == "add" ? sed::assets::ModelLightingBlend::Add
            : sed::assets::ModelLightingBlend::Replace;
    }
    if ( const Json* ambient = request.find("ambient") )
    {
        options.ambient = colorValue(*ambient, "ambient");
    }
    if ( const Json* occlusion = request.find("selfOcclusion") )
    {
        options.selfOcclusion = boolValue(*occlusion, "selfOcclusion");
    }
    if ( const Json* bias = request.find("shadowBias") )
    {
        options.shadowBias = numberValue(*bias, "shadowBias", 1e-12, 1000);
    }
    if ( const Json* limits = request.find("limits") )
    {
        fields(*limits, { "sourceBytes", "lines", "tokensPerLine", "geosets", "meshes", "vertices",
            "triangles", "hierarchyNodes", "lights", "workUnits" }, "limits");
        const ModelLightingLimits ceiling;
        for ( const LimitField& field : kLimitFields )
        {
            if ( const Json* value = limits->find(field.name) )
            {
                const double limit = numberValue(*value, field.name, 1,
                    static_cast<double>(ceiling.*(field.member)));
                if ( limit != std::floor(limit) )
                {
                    invalid(std::string(field.name) + " must be an integer");
                }
                options.limits.*(field.member) = static_cast<std::size_t>(limit);
            }
        }
    }
    const Json& lights = required(request, "lights");
    if ( lights.type != Json::Type::Array || lights.array.size() > ModelLightingLimits{}.lights )
    {
        invalid("lights must be an array of at most 256 light objects");
    }
    for ( const Json& item : lights.array )
    {
        const std::string type = stringValue(required(item, "type"), "light.type");
        sed::assets::ModelLightingLight light;
        if ( type == "point" )
        {
            fields(item, { "type", "position", "range", "color", "intensity" }, "Point light");
            light.kind = sed::assets::ModelLightKind::Point;
            light.position = vectorValue(required(item, "position"), "position", -1e6, 1e6);
            light.range = numberValue(required(item, "range"), "range", 1e-12, 1e6);
        }
        else if ( type == "directional" )
        {
            fields(item, { "type", "directionToLight", "color", "intensity" }, "Directional light");
            light.kind = sed::assets::ModelLightKind::Directional;
            light.directionToLight = vectorValue(required(item, "directionToLight"), "directionToLight", -1e6, 1e6);
            if ( light.directionToLight.x == 0 && light.directionToLight.y == 0 && light.directionToLight.z == 0 )
            {
                invalid("directionToLight must be nonzero");
            }
        }
        else
        {
            invalid("light.type must be 'point' or 'directional'");
        }
        if ( const Json* color = item.find("color") )
        {
            light.color = colorValue(*color, "color");
        }
        if ( const Json* intensity = item.find("intensity") )
        {
            light.intensity = numberValue(*intensity, "intensity", 0, 1e6);
        }
        options.lights.push_back(light);
    }
    if ( options.profile == sed::assets::ModelLightingProfile::ClassicScalar )
    {
        const auto grey = [](const sed::ColorF& color)
        {
            return color.r == color.g && color.g == color.b;
        };
        if ( !grey(options.ambient) || std::any_of(options.lights.begin(), options.lights.end(),
            [&](const auto& light) { return !grey(light.color); }) )
        {
            invalid("Classic model lighting requires equal RGB components; colored input is not silently averaged");
        }
    }
    return options;
}

Json count(const std::size_t value)
{
    return Json::makeNumber(static_cast<double>(value));
}

Json rgb(const sed::ColorF& color)
{
    Json values = Json::makeArray();
    for ( const float component : { color.r, color.g, color.b } )
    {
        values.array.push_back(Json::makeNumber(component));
    }
    return values;
}

Json inspectionJson(const sed::assets::ModelLightingInspection& inspection)
{
    Json output = Json::makeObject();
    output.set("version", Json::makeNumber(inspection.version));
    output.set("geosets", count(inspection.geosets));
    output.set("sourceMeshes", count(inspection.sourceMeshes));
    output.set("renderMeshes", count(inspection.renderMeshes));
    output.set("vertices", count(inspection.vertices));
    output.set("orphanMeshes", count(inspection.orphanMeshes));
    output.set("sharedMeshes", count(inspection.sharedMeshes));
    Json meshes = Json::makeArray();
    for ( const auto& mesh : inspection.meshes )
    {
        Json row = Json::makeObject();
        row.set("geoset", count(mesh.geoset));
        row.set("sourceMesh", count(mesh.sourceMesh));
        row.set("name", Json::makeString(mesh.name));
        row.set("vertices", count(mesh.vertices));
        row.set("faces", count(mesh.faces));
        row.set("renderInstances", count(mesh.renderInstances));
        row.set("meshLightMode", Json::makeNumber(mesh.meshLightMode));
        row.set("minimumVertexLight", rgb(mesh.minimumVertexLight));
        row.set("maximumVertexLight", rgb(mesh.maximumVertexLight));
        row.set("minimumFaceExtra", rgb(mesh.minimumFaceExtra));
        row.set("maximumFaceExtra", rgb(mesh.maximumFaceExtra));
        row.set("missingNormals", count(mesh.missingNormals));
        row.set("zeroNormals", count(mesh.zeroNormals));
        row.set("nonUnitNormals", count(mesh.nonUnitNormals));
        row.set("saturatedVertices", count(mesh.saturatedVertices));
        Json modes = Json::makeArray();
        for ( const auto& mode : mesh.effectiveLightModes )
        {
            Json entry = Json::makeObject();
            entry.set("mode", Json::makeNumber(mode.mode));
            entry.set("faces", count(mode.faces));
            modes.array.push_back(std::move(entry));
        }
        row.set("effectiveLightModes", std::move(modes));
        meshes.array.push_back(std::move(row));
    }
    output.set("meshes", std::move(meshes));
    return output;
}

void addInterpretation(Json& output, const bool classic)
{
    output.set("coordinateSpace", Json::makeString("model-bind-pose"));
    output.set("channel", Json::makeString(classic ? "scalar" : "rgb"));
    output.set("runtimeVerified", Json::makeBool(false));
    Json notes = Json::makeArray();
    for ( const char* note : {
        "Inspection describes stored asset data, not a rendered game frame.",
        "Effective modes 0, 1 and 2 (Fully Lit, Lit, Diffuse) bypass Gouraud vertex-light shape; modes and face extra are preserved.",
        "Mode -1 (VertexDiffuseOnly) uses authored color directly in the editor; this does not establish retail support.",
        "Runtime lights, sector ambient, camera distance, textures and animation are not evaluated.",
        "Lighting belongs to the model asset; every instance using that asset receives the same values." } )
    {
        notes.array.push_back(Json::makeString(note));
    }
    if ( classic )
    {
        notes.array.push_back(Json::makeString(
            "Classic vertex intensity is additive; the sector ambient floor can erase dark contrast. This is not multiplicative AO."));
    }
    output.set("interpretation", std::move(notes));
}

Json numericSchema(const double minimum, const double maximum, const char* type = "number")
{
    Json schema = Json::makeObject();
    schema.set("type", Json::makeString(type));
    schema.set("minimum", Json::makeNumber(minimum));
    schema.set("maximum", Json::makeNumber(maximum));
    return schema;
}

Json vectorSchema(const double minimum, const double maximum)
{
    Json schema = Json::makeObject();
    schema.set("type", Json::makeString("array"));
    schema.set("minItems", count(3));
    schema.set("maxItems", count(3));
    schema.set("items", numericSchema(minimum, maximum));
    return schema;
}

Json stringEnum(const std::initializer_list<const char*> options)
{
    Json schema = Json::makeObject();
    schema.set("type", Json::makeString("string"));
    Json values = Json::makeArray();
    for ( const char* option : options )
    {
        values.array.push_back(Json::makeString(option));
    }
    schema.set("enum", std::move(values));
    return schema;
}

Json objectSchema(Json properties, const std::initializer_list<const char*> requiredFields)
{
    Json schema = Json::makeObject();
    schema.set("type", Json::makeString("object"));
    schema.set("additionalProperties", Json::makeBool(false));
    schema.set("properties", std::move(properties));
    Json names = Json::makeArray();
    for ( const char* name : requiredFields )
    {
        names.array.push_back(Json::makeString(name));
    }
    schema.set("required", std::move(names));
    return schema;
}

} // namespace

Json modelLightingInspect(const std::vector<std::string>& args)
{
    if ( args.size() != 1 )
    {
        invalid("model-lighting-inspect requires <input.3do>");
    }
    const std::string source = readFileBytes(args[0], ModelLightingLimits{}.sourceBytes);
    sed::assets::ModelLightingInspection inspection;
    try
    {
        inspection = sed::assets::inspectModelLighting(std::as_bytes(std::span(source.data(), source.size())));
    }
    catch ( const std::exception& error )
    {
        throw AgentError{ kExitInput, error.what(), "modelLightingInputUnsupported" };
    }
    Json output = inspectionJson(inspection);
    output.set("ok", Json::makeBool(true));
    output.set("command", Json::makeString("model-lighting-inspect"));
    output.set("inputSha256", Json::makeString(sha256Hex(source)));
    addInterpretation(output, inspection.version == 2.1);
    return output;
}

Json modelLightingPlanOrApply(const std::vector<std::string>& args, const bool apply)
{
    const std::string command = apply ? "model-lighting-apply" : "model-lighting-plan";
    if ( args.size() != (apply ? 4u : 2u) )
    {
        invalid(command + (apply ? " requires <input.3do> <request.json|-> <expected-output-sha256> <new-output.3do>"
            : " requires <input.3do> <request.json|->"));
    }
    // Capture the source before reading a potentially interactive stdin request.
    // runCommand's read set rechecks both sources immediately before publication.
    const std::string source = readFileBytes(args[0], ModelLightingLimits{}.sourceBytes);
    const std::string sourceHash = sha256Hex(source);
    const Json request = readRequest(args[1]);
    const ModelLightingOptions options = parseOptions(request);
    if ( hashValue(required(request, "inputSha256"), "inputSha256") != sourceHash )
    {
        throw AgentError{ kExitConflict, "Model input changed; inspect it and plan again", "modelLightingStaleInput" };
    }
    std::string expected;
    if ( apply )
    {
        expected = hashValue(Json::makeString(args[2]), "expectedOutputSha256");
        if ( lower(std::filesystem::path(args[3]).extension().string()) != ".3do" )
        {
            invalid("model-lighting-apply output must end in .3do");
        }
    }
    sed::assets::ModelLightingBakeResult result;
    try
    {
        result = sed::assets::bakeModelLighting(std::as_bytes(std::span(source.data(), source.size())), options);
    }
    catch ( const std::exception& error )
    {
        throw AgentError{ kExitRefusal, error.what(), "modelLightingRefused" };
    }
    const std::string outputHash = sha256Hex(result.text);
    if ( apply && expected != outputHash )
    {
        throw AgentError{ kExitConflict, "Reviewed model output does not match; plan again", "modelLightingPlanMismatch" };
    }
    Json output = Json::makeObject();
    output.set("ok", Json::makeBool(true));
    output.set("command", Json::makeString(command));
    output.set("inputSha256", Json::makeString(sourceHash));
    output.set("outputSha256", Json::makeString(outputHash));
    output.set("requestSha256", Json::makeString(sha256Hex(toJsonString(request))));
    output.set("profile", required(request, "profile"));
    output.set("blend", Json::makeString(options.blend == sed::assets::ModelLightingBlend::Add ? "add" : "replace"));
    output.set("selfOcclusion", Json::makeBool(options.selfOcclusion));
    output.set("occlusionPolicy", Json::makeString(options.selfOcclusion ? "opaque-two-sided-same-lod" : "none"));
    output.set("changedVertices", count(result.changedVertices));
    output.set("clampedVertices", count(result.clampedVertices));
    output.set("occludedSamples", count(result.occludedSamples));
    output.set("zeroNormalVertices", count(result.zeroNormalVertices));
    output.set("before", inspectionJson(result.before));
    output.set("after", inspectionJson(result.after));
    Json limits = Json::makeObject();
    for ( const LimitField& field : kLimitFields )
    {
        limits.set(field.name, count(options.limits.*(field.member)));
    }
    output.set("limits", std::move(limits));
    addInterpretation(output, options.profile == sed::assets::ModelLightingProfile::ClassicScalar);
    if ( apply )
    {
        writeNewFileAtomic(args[3], result.text);
        output.set("output", Json::makeString(args[3]));
    }
    output.set("written", Json::makeBool(apply));
    return output;
}

Json modelLightingRequestSchema()
{
    Json properties = Json::makeObject();
    properties.set("schemaVersion", numericSchema(1, 1, "integer"));
    Json hash = Json::makeObject();
    hash.set("type", Json::makeString("string"));
    hash.set("pattern", Json::makeString("^[0-9a-fA-F]{64}$"));
    properties.set("inputSha256", std::move(hash));
    properties.set("profile", stringEnum({ "jk", "mots", "ijim" }));
    properties.set("blend", stringEnum({ "replace", "add" }));
    properties.set("ambient", vectorSchema(0, 1));
    Json boolean = Json::makeObject();
    boolean.set("type", Json::makeString("boolean"));
    properties.set("selfOcclusion", std::move(boolean));
    properties.set("shadowBias", numericSchema(1e-12, 1000));
    Json limitProperties = Json::makeObject();
    const ModelLightingLimits ceiling;
    for ( const LimitField& field : kLimitFields )
    {
        limitProperties.set(field.name, numericSchema(1, static_cast<double>(ceiling.*(field.member)), "integer"));
    }
    properties.set("limits", objectSchema(std::move(limitProperties), {}));
    Json alternatives = Json::makeArray();
    for ( const bool point : { true, false } )
    {
        Json light = Json::makeObject();
        light.set("type", stringEnum({ point ? "point" : "directional" }));
        light.set("color", vectorSchema(0, 1));
        light.set("intensity", numericSchema(0, 1e6));
        if ( point )
        {
            light.set("position", vectorSchema(-1e6, 1e6));
            light.set("range", numericSchema(1e-12, 1e6));
            alternatives.array.push_back(objectSchema(std::move(light), { "type", "position", "range" }));
        }
        else
        {
            Json direction = vectorSchema(-1e6, 1e6);
            direction.set("description", Json::makeString("Nonzero direction from sample toward source; normalized by the baker."));
            Json zero = Json::makeArray();
            zero.array.assign(3, Json::makeNumber(0));
            Json forbidden = Json::makeObject();
            forbidden.set("const", std::move(zero));
            direction.set("not", std::move(forbidden));
            light.set("directionToLight", std::move(direction));
            alternatives.array.push_back(objectSchema(std::move(light), { "type", "directionToLight" }));
        }
    }
    Json itemSchema = Json::makeObject();
    itemSchema.set("oneOf", std::move(alternatives));
    Json lights = Json::makeObject();
    lights.set("type", Json::makeString("array"));
    lights.set("maxItems", count(ceiling.lights));
    lights.set("items", std::move(itemSchema));
    properties.set("lights", std::move(lights));
    Json schema = objectSchema(std::move(properties), { "schemaVersion", "inputSha256", "profile", "lights" });
    schema.set("$schema", Json::makeString("https://json-schema.org/draft/2020-12/schema"));
    schema.set("description", Json::makeString(
        "Bind-pose 3DO relighting. JK/MotS require grayscale ambient and light colors and version 2.1; IJIM requires version 2.3. "
        "Generated light is clamped to 0..1. Plan writes nothing; apply also requires the reviewed output hash and a fresh .3do path."));
    return schema;
}

} // namespace sed_agent
