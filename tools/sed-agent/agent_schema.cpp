#include "agent_schema.hpp"

#include "agent_workspace.hpp"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sed_agent
{
namespace
{
// Reconstructed from the published validators. The retained command catalog,
// archive helper and patch envelope below preserve recovered source text.
constexpr std::string_view kDialect = "https://json-schema.org/draft/2020-12/schema";
constexpr double kInt32Minimum = -2147483648.0;
constexpr double kInt32Maximum = 2147483647.0;
constexpr double kUint32Maximum = 4294967295.0;
constexpr double kFinite = std::numeric_limits<double>::max();
constexpr double kFloat = std::numeric_limits<float>::max();
using Names = std::initializer_list<const char*>;
using Properties = std::initializer_list<std::pair<const char*, Json>>;

Json type(const char* name)
{
    Json result = Json::makeObject();
    result.set("type", Json::makeString(name));
    return result;
}
Json strings(const Names names)
{
    Json result = Json::makeArray();
    for ( const auto* name : names ) result.array.push_back(Json::makeString(name));
    return result;
}
Json numeric(double minimum = -kFinite, double maximum = kFinite, bool integer = false)
{
    Json result = type(integer ? "integer" : "number");
    result.set("minimum", Json::makeNumber(minimum));
    result.set("maximum", Json::makeNumber(maximum));
    return result;
}
Json positive(double maximum = kFinite)
{
    Json result = numeric(0, maximum);
    result.set("exclusiveMinimum", Json::makeNumber(0));
    return result;
}
Json index()
{
    return numeric(0, kInt32Maximum, true);
}
Json signedIndex()
{
    return numeric(kInt32Minimum, kInt32Maximum, true);
}
Json uint32()
{
    return numeric(0, kUint32Maximum, true);
}
Json text(std::size_t minimum = 0, std::size_t maximum = 0)
{
    Json result = type("string");
    if ( minimum ) result.set("minLength", Json::makeNumber(static_cast<double>(minimum)));
    if ( maximum ) result.set("maxLength", Json::makeNumber(static_cast<double>(maximum)));
    return result;
}
Json pattern(const std::string& expression, std::size_t minimum = 0, std::size_t maximum = 0)
{
    Json result = text(minimum, maximum);
    // '$' alone can match before a final newline in standards validators.
    result.set("pattern", Json::makeString(expression + "(?![\\s\\S])"));
    return result;
}
Json token(std::size_t maximum = 127)
{
    return pattern(R"(^(?!.*["#])[!-~]+$)", 1, maximum);
}
Json resourceName(bool material)
{
    return pattern(material
        ? R"(^(?!.*\.\.)(?!.*[/\\:<>|*?,="'#])[!-~]+\.[mM][aA][tT]$)"
        : R"(^(?!.*\.\.)(?!.*[/\\:<>|*?,="'#])[!-~]+\.[cC][mM][pP]$)", 5, 127);
}
Json hash()
{
    return pattern("^[0-9a-fA-F]{64}$", 64, 64);
}
Json authoredCogScriptName()
{
    // Match isValidAuthoredCogScriptName, whose punctuation domain differs
    // from material/colormap registration (for example #, .., comma and =).
    return pattern(R"(^(?!.*[/\\:*?"<>|])[!-~]+\.[cC][oO][gG]$)", 5, 31);
}
Json enumeration(const Names names)
{
    Json result = type("string");
    result.set("enum", strings(names));
    return result;
}
Json constant(const char* value)
{
    Json result = type("string");
    result.set("const", Json::makeString(value));
    return result;
}
Json described(Json schema, const char* description)
{
    schema.set("description", Json::makeString(description));
    return schema;
}
Json array(Json item, std::size_t minimum = 0, std::size_t maximum = 0)
{
    Json result = type("array");
    result.set("items", std::move(item));
    if ( minimum ) result.set("minItems", Json::makeNumber(static_cast<double>(minimum)));
    if ( maximum ) result.set("maxItems", Json::makeNumber(static_cast<double>(maximum)));
    return result;
}
Json tuple(Json item, std::size_t count)
{
    return array(std::move(item), count, count);
}
Json vector(double minimum = -kFinite, double maximum = kFinite)
{
    return tuple(numeric(minimum, maximum), 3);
}
Json coordinate()
{
    return vector(-1000000, 1000000);
}
Json choice(const char* keyword, std::initializer_list<Json> choices)
{
    Json result = Json::makeObject();
    Json list = Json::makeArray();
    for ( const auto& item : choices ) list.array.push_back(item);
    result.set(keyword, std::move(list));
    return result;
}
Json nullable(Json schema)
{
    return choice("anyOf", { std::move(schema), type("null") });
}
Json required(const Names names)
{
    Json result = Json::makeObject();
    result.set("required", strings(names));
    return result;
}
Json absent(const Names names)
{
    Json choices = Json::makeArray();
    for ( const auto* name : names ) choices.array.push_back(required({ name }));
    Json present = Json::makeObject();
    present.set("anyOf", std::move(choices));
    Json result = Json::makeObject();
    result.set("not", std::move(present));
    return result;
}
Json propertyRule(const Properties properties)
{
    Json result = Json::makeObject();
    Json fields = Json::makeObject();
    for ( const auto& [name, schema] : properties ) fields.set(name, schema);
    result.set("properties", std::move(fields));
    return result;
}
Json object(const Properties properties, const Names names = {}, bool closed = true)
{
    Json result = propertyRule(properties);
    result.set("type", Json::makeString("object"));
    result.set("required", strings(names));
    result.set("additionalProperties", Json::makeBool(!closed));
    return result;
}
void allOf(Json& schema, Json rule)
{
    Json list = schema.find("allOf") ? *schema.find("allOf") : Json::makeArray();
    list.array.push_back(std::move(rule));
    schema.set("allOf", std::move(list));
}
Json condition(Json test, Json consequence)
{
    Json result = Json::makeObject();
    result.set("if", std::move(test));
    result.set("then", std::move(consequence));
    return result;
}
Json hasProperty(const char* name, Json rule)
{
    Json result = propertyRule({ { name, std::move(rule) } });
    result.set("required", strings({ name }));
    return result;
}
Json atLeastOne(const Names names)
{
    Json list = Json::makeArray();
    for ( const auto* name : names ) list.array.push_back(required({ name }));
    Json result = Json::makeObject();
    result.set("anyOf", std::move(list));
    return result;
}
Json exactlyOne(const char* first, const char* second)
{
    return choice("oneOf", { required({ first }), required({ second }) });
}
Json nonzero(Json schema)
{
    schema.set("not", parseJson(R"({"const":[0,0,0]})"));
    return schema;
}
Json nonzeroNumber(Json schema)
{
    schema.set("not", parseJson(R"({"const":0})"));
    return schema;
}
Json boundFile(bool closed)
{
    return object({ { "path", text(1) }, { "sha256", hash() } }, { "path", "sha256" }, closed);
}
Json archives()
{
    return described(array(boundFile(false), 1, 16),
        "Explicit captured archives in precedence order. Entry extension fields remain accepted by the legacy loader.");
}

Json surfacePair(const bool validIndex = true)
{
    return tuple(validIndex ? index() : numeric(kInt32Minimum, kInt32Maximum, true), 2);
}

Json surfaces(const bool batch)
{
    Json result = array(surfacePair(batch), 1, batch ? 256 : 0);
    if ( batch ) result.set("uniqueItems", Json::makeBool(true));
    return result;
}
Json color(bool unitRgb = true)
{
    Json result = type("array");
    result.set("minItems", Json::makeNumber(3));
    result.set("maxItems", Json::makeNumber(4));
    Json prefix = Json::makeArray();
    for ( int i = 0; i < 3; ++i ) prefix.array.push_back(unitRgb ? numeric(0, 1) : numeric(-kFloat, kFloat));
    prefix.array.push_back(numeric(-kFloat, kFloat));
    result.set("prefixItems", std::move(prefix));
    result.set("items", Json::makeBool(false));
    return result;
}
Json frameList()
{
    return array(object({ { "position", coordinate() }, { "orientation", coordinate() } },
        { "position", "orientation" }, false), 1, 256);
}
Json placementLayout()
{
    Json result = object({
        { "kind", enumeration({ "linear", "grid", "radial" }) },
        { "origin", vector() }, { "step", vector() }, { "stepU", vector() }, { "stepV", vector() },
        { "axis", vector() }, { "count", signedIndex() }, { "countU", signedIndex() }, { "countV", signedIndex() },
        { "radius", numeric() }, { "startDegrees", numeric() }, { "sweepDegrees", numeric() }
    }, {}, false);
    allOf(result, propertyRule({ { "origin", coordinate() } }));
    Json linear = propertyRule({ { "kind", constant("linear") } });
    allOf(result, condition(std::move(linear), propertyRule({
        { "count", numeric(1, 256, true) }, { "step", coordinate() } })));
    allOf(result, condition(hasProperty("kind", constant("grid")), propertyRule({
        { "countU", numeric(1, 256, true) }, { "countV", numeric(1, 256, true) },
        { "stepU", coordinate() }, { "stepV", coordinate() } })));
    allOf(result, condition(hasProperty("kind", constant("radial")), propertyRule({
        { "count", numeric(1, 256, true) }, { "axis", nonzero(coordinate()) },
        { "radius", positive(1000000) }, { "startDegrees", numeric(-1000000, 1000000) },
        { "sweepDegrees", nonzeroNumber(numeric(-360, 360)) } })));
    result.set("x-runtimeConstraints", Json::makeString(
        "Active grid countU*countV must be <=256; generated positions must be distinct and within +/-1000000. Layout defaults are applied by runtime. Inactive fields retain typed legacy admission."));
    return result;
}
Json flagMask()
{
    Json result = object({ { "any", uint32() }, { "all", uint32() }, { "none", uint32() } }, {}, false);
    allOf(result, atLeastOne({ "any", "all", "none" }));
    return result;
}
std::string foldedPattern(const std::string& textValue)
{
    std::string result;
    for ( char c : textValue )
    {
        if ( c >= 'a' && c <= 'z' )
        {
            result += '[';
            result += c;
            result += static_cast<char>(c - 'a' + 'A');
            result += ']';
        }
        else result += c;
    }
    return result;
}
Json kinds(bool referencesOnly = false)
{
    std::string expression = "^(?:";
    bool first = true;
    const Names references{ "thing", "thg", "sector", "sec", "surface", "srf", "cog" };
    const Names all{ "message", "int", "integer", "flex", "float", "vector", "vect", "thing",
        "sector", "surface", "sound", "wav", "material", "mat", "model", "3do",
        "template", "tpl", "keyframe", "key", "ai", "cog" };
    for ( const auto* name : referencesOnly ? references : all )
    {
        if ( !first ) expression += '|';
        first = false;
        expression += foldedPattern(name);
    }
    return pattern(expression + ")$");
}
Json cogValue()
{
    return choice("anyOf", { text(), numeric() });
}
Json cogTarget()
{
    Json pair = required({ "sector", "surface" });
    allOf(pair, absent({ "targetIndex" }));
    Json numbered = required({ "targetIndex" });
    allOf(numbered, absent({ "sector", "surface" }));
    return choice("oneOf", { std::move(pair), std::move(numbered) });
}
void cogTargetRules(Json& result, bool rebind = false)
{
    Json pairSeen = atLeastOne({ "sector", "surface" });
    Json pair = required({ "sector", "surface" });
    allOf(pair, absent({ "targetIndex" }));
    allOf(pair, propertyRule({ { "kind", pattern("^" + foldedPattern("surface") + "$") } }));
    if ( rebind )
    {
        // kind takes precedence; type is read only when kind is absent.
        allOf(pair, condition(absent({ "kind" }), propertyRule({
            { "type", pattern("^" + foldedPattern("surface") + "$") } })));
    }
    allOf(result, condition(std::move(pairSeen), std::move(pair)));
}
Json cogBinding()
{
    Json result = object({ { "symbol", text() }, { "symbolName", text() }, { "name", text() },
        { "kind", kinds() }, { "targetIndex", numeric(-1, kInt32Maximum, true) },
        { "sector", index() }, { "surface", index() }, { "value", cogValue() },
        { "default", parseJson(R"({"type":"boolean","const":true})") } }, { "kind" }, false);
    allOf(result, atLeastOne({ "symbol", "symbolName", "name" }));
    cogTargetRules(result);
    Json valueSelection = exactlyOne("value", "default");
    allOf(valueSelection, absent({ "targetIndex", "sector", "surface" }));
    Json referenceKind = hasProperty("kind", kinds(true));
    Json nonReferenceKind = Json::makeObject();
    nonReferenceKind.set("not", referenceKind);
    allOf(valueSelection, std::move(nonReferenceKind));
    Json targetSelection = cogTarget();
    allOf(targetSelection, absent({ "value", "default" }));
    allOf(targetSelection, std::move(referenceKind));
    allOf(result, choice("oneOf", { std::move(valueSelection), std::move(targetSelection) }));
    allOf(result, condition(hasProperty("kind", pattern("^(?:" + foldedPattern("int") + "|"
        + foldedPattern("integer") + ")$")), propertyRule({ { "value", choice("anyOf", {
            text(), numeric(kInt32Minimum, kInt32Maximum, true) }) } })));
    allOf(result, condition(hasProperty("kind", pattern("^(?:" + foldedPattern("flex") + "|"
        + foldedPattern("float") + ")$")), propertyRule({ { "value", choice("anyOf", {
            text(), numeric(-kFloat, kFloat) }) } })));
    return result;
}
Json operation(const std::string& name, Json result)
{
    Json properties = *result.find("properties");
    properties.set("op", constant(name.c_str()));
    result.set("properties", std::move(properties));
    Json names = *result.find("required");
    names.array.insert(names.array.begin(), Json::makeString("op"));
    result.set("required", std::move(names));
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED patch operation " + name));
    std::string runtime = "Current record ranges, project support, geometry validity, source/resource hashes, COG declaration compatibility, generated counts and cross-field arithmetic are runtime checks. A schema match does not prove gameplay or geometry safety.";
    if ( name == "bindThingArchitecture" || name == "rebindThingArchitecture" || name == "bakeThingArchitecture" )
    {
        runtime += " Requires captured patch-root resourceArchives and an effective MotS Arch owner with a resolvable model; operation-local archives are not supported. Samples must exactly match geoset-zero mesh/vertex counts and every lower LOD must fit those prefixes. A larger lower LOD refuses with archLowerLodOverflow. Successful authoring creates private samples and clears only the selected owner's automatic-bake baseline.";
        if ( name == "bakeThingArchitecture" )
        {
            runtime += " Hierarchy/world addressing, source-light and blocker budgets remain runtime checks. These explicit editor profiles do not claim retail-baker equivalence; the v1 bake-input digest excludes blocker geometry.";
        }
    }
    if ( name == "addBox" )
    {
        runtime += " Corner spelling requires each max-min extent in 0.0002..200000; all final coordinates must remain within +/-1000000. Geometry must be closed, convex and non-overlapping.";
    }
    if ( name == "defineTemplate" )
    {
        runtime += " Template tokens must have balanced parentheses and parse through the shared template loader; parent availability and preservation of referenced templates are checked against the current document.";
    }
    if ( name == "setLevelHeader" )
    {
        runtime += " mipmapDistances and lodDistances must be non-decreasing.";
    }
    if ( name == "setSurfaceUv" || name == "fitSurfaceTexture" )
    {
        runtime += " Requires declared texture-pixel UV space. Surface corner count/order and supported geometry are checked against the current document; texture dimensions are never guessed.";
    }
    if ( name == "alignSurfaceTexture" )
    {
        runtime += " Requires one exact complete shared edge, a compatible affine reference mapping and shared-core representability. The otherSector/otherSurface face is the reference.";
    }
    if ( name == "bakeLighting" )
    {
        runtime += " Disabled sky may ignore skyColor; enabled sky accepts finite stored-float components. Disabled bounce retains numeric inactive range admission. Light limits require floor<=cap; resource capture, winner identity, emission and occlusion remain runtime checks.";
    }
    result.set("x-runtimeConstraints", Json::makeString(std::move(runtime)));
    return result;
}
Json operationShape(const std::string_view name)
{
    if ( name == "addLight" || name == "setLight" )
    {
        Json result = object({ { "light", index() }, { "position", coordinate() },
            { "rgb", vector(0, 1) }, { "intensity", numeric(0, 1000) }, { "range", numeric(0.0001, 100000) },
            { "rgbIntensity", nullable(numeric(0, 1000)) },
            { "legacyRgbRange", nullable(numeric(-100000, 100000)) }, { "flags", uint32() }, { "layer", index() } },
            name == "addLight" ? Names{ "position", "rgb", "intensity", "range" } : Names{ "light" });
        if ( name == "addLight" )
        {
            result.object["properties"].object.erase("light");
        }
        else allOf(result, atLeastOne({ "position", "rgb", "intensity", "range", "rgbIntensity", "legacyRgbRange", "flags", "layer" }));
        return result;
    }
    if ( name == "setThingPosition" )
        return object({ { "thing", index() }, { "position", coordinate() }, { "sector", index() } },
            { "thing", "position", "sector" });
    if ( name == "setThingOrientation" )
        return object({ { "thing", index() }, { "orientation", coordinate() } }, { "thing", "orientation" });
    if ( name == "setThingName" )
        return object({ { "thing", index() }, { "name", token() } }, { "thing", "name" });
    if ( name == "setThingTemplate" )
        return object({ { "thing", index() }, { "template", token() } }, { "thing", "template" });
    if ( name == "setThingValue" )
    {
        Json key = pattern("^[a-zA-Z0-9_]+$", 1, 127);
        key.set("not", pattern("^(?:" + foldedPattern("frame") + "|" + foldedPattern("numframes") + ")$"));
        return object({ { "thing", index() }, { "key", std::move(key) },
            { "value", token() } }, { "thing", "key", "value" });
    }
    if ( name == "clearThingArchitecture" ) return object({ { "thing", index() } }, { "thing" });
    if ( name == "addThing" )
        return object({ { "sector", index() }, { "template", token() }, { "name", token() },
            { "position", coordinate() } }, { "sector", "template", "name", "position" });
    if ( name == "addBox" )
    {
        Json result = object({ { "center", coordinate() },
            { "halfSize", choice("oneOf", { numeric(0.0001, 100000), vector(0.0001, 100000) }) },
            { "min", coordinate() }, { "max", coordinate() } });
        Json centered = required({ "center", "halfSize" });
        allOf(centered, absent({ "min", "max" }));
        Json corners = required({ "min", "max" });
        allOf(corners, absent({ "center", "halfSize" }));
        allOf(result, choice("oneOf", { centered, corners }));
        return result;
    }
    if ( name == "extrudeSurface" )
        return object({ { "sector", index() }, { "surface", index() }, { "distance", numeric(0.0001, 100000) } },
            { "sector", "surface", "distance" });
    if ( name == "adjoinSurfaces" || name == "alignSurfaceTexture" )
        return object({ { "sector", index() }, { "surface", index() }, { "otherSector", index() },
            { "otherSurface", index() } }, { "sector", "surface", "otherSector", "otherSurface" });
    if ( name == "cleaveSurface" || name == "cleaveSector" )
    {
        Json result = object({ { "sector", index() }, { "surface", index() },
            { "normal", nonzero(coordinate()) }, { "point", coordinate() } },
            name == "cleaveSurface" ? Names{ "sector", "surface", "normal", "point" } : Names{ "sector", "normal", "point" });
        if ( name == "cleaveSector" ) result.object["properties"].object.erase("surface");
        return result;
    }
    if ( name == "translateSectors" )
        return object({ { "sectors", array(index(), 1) }, { "delta", coordinate() } }, { "sectors", "delta" });
    if ( name == "scaleSectors" )
    {
        const Json factor = positive(1000);
        Json result = object({ { "sectors", array(index(), 1) }, { "pivot", coordinate() },
            { "factor", factor }, { "factors", tuple(factor, 3) }, { "scaleTextures", type("boolean") } },
            { "sectors", "pivot" });
        allOf(result, exactlyOne("factor", "factors"));
        return result;
    }
    if ( name == "rotateSectors" )
        return object({ { "sectors", array(index(), 1) }, { "pivot", coordinate() },
            { "axis", nonzero(coordinate()) }, { "angleDegrees", numeric(-36000, 36000) } },
            { "sectors", "pivot", "axis", "angleDegrees" });
    if ( name == "translateThings" )
        return object({ { "things", array(index(), 1) }, { "delta", coordinate() },
            { "moveFrames", type("boolean") } }, { "things", "delta" });
    if ( name == "translateLights" )
        return object({ { "lights", array(index(), 1) }, { "delta", coordinate() } }, { "lights", "delta" });
    if ( name == "registerMaterial" || name == "registerColormap" )
        return object({ { "name", resourceName(name == "registerMaterial") } }, { "name" });
    if ( name == "renameMaterial" )
        return object({ { "materialIndex", index() }, { "name", resourceName(true) } }, { "materialIndex", "name" });
    if ( name == "renameColormap" )
        return object({ { "colormapIndex", index() }, { "name", resourceName(false) } }, { "colormapIndex", "name" });
    if ( name == "setSurfaceMaterial" )
        return object({ { "sector", index() }, { "surface", index() }, { "materialIndex", index() } },
            { "sector", "surface", "materialIndex" });
    if ( name == "setSectorColormap" )
        return object({ { "sector", index() }, { "colormapIndex", index() } }, { "sector", "colormapIndex" });
    if ( name == "setSurfaceRenderModes" )
        return object({ { "sector", index() }, { "surface", index() }, { "geometryMode", numeric(0, 4, true) },
            { "lightMode", numeric(0, 3, true) }, { "textureMode", numeric(0, 3, true) } },
            { "sector", "surface", "geometryMode", "lightMode", "textureMode" });
    if ( name == "setSurfaceUv" )
        return object({ { "sector", index() }, { "surface", index() },
            { "uv", array(tuple(numeric(-1000000, 1000000), 2), 3, 64) } }, { "sector", "surface", "uv" });
    if ( name == "setVertexLight" )
        return object({ { "sector", index() }, { "vertex", index() },
            { "channel", enumeration({ "mono", "red", "green", "blue" }) }, { "value", numeric(0, 1000) } },
            { "sector", "vertex", "channel", "value" });
    if ( name == "setVertexPosition" )
        return object({ { "sector", index() }, { "vertex", index() }, { "position", coordinate() } },
            { "sector", "vertex", "position" });
    if ( name == "fitSurfaceTexture" )
        return object({ { "sector", index() }, { "surface", index() }, { "width", numeric(1, 16384, true) },
            { "height", numeric(1, 16384, true) } }, { "sector", "surface", "width", "height" });
    if ( name == "bindThingArchitecture" )
        return object({ { "thing", index() }, { "lightingIndex", index() } }, { "thing", "lightingIndex" });
    if ( name == "rebindThingArchitecture" )
        return object({ { "thing", index() }, { "policy", constant("reset") } }, { "thing", "policy" });
    if ( name == "bakeThingArchitecture" )
        return object({ { "thing", index() }, { "profile", enumeration({ "source-points-v1", "source-points-world-occluded-v1" }) } },
            { "thing", "profile" });
    if ( name == "setGravity" ) return object({ { "value", numeric() } }, { "value" }, false);
    if ( name == "setLevelHeader" )
    {
        Json result = object({ { "gravity", numeric(-1000, 1000) }, { "ceilingSkyHeight", numeric(-1000000, 1000000) },
            { "ceilingSkyOffset", tuple(numeric(-1000000, 1000000), 2) },
            { "horizonDistance", numeric(0, 1000000) }, { "horizonPixelsPerRev", numeric(0, 1000000) },
            { "horizonSkyOffset", tuple(numeric(-1000000, 1000000), 2) },
            { "mipmapDistances", tuple(numeric(0, 1000000), 4) }, { "lodDistances", tuple(numeric(0, 1000000), 4) },
            { "perspectiveDistance", numeric(0, 1000000) }, { "gouraudDistance", numeric(0, 1000000) } });
        allOf(result, atLeastOne({ "gravity", "ceilingSkyHeight", "ceilingSkyOffset", "horizonDistance", "horizonPixelsPerRev",
            "horizonSkyOffset", "mipmapDistances", "lodDistances", "perspectiveDistance", "gouraudDistance" }));
        return result;
    }
    if ( name == "importTemplates" )
        return object({ { "templates", array(token(), 1, 256) }, { "templateSource", boundFile(true) } }, { "templates" });
    if ( name == "defineTemplate" )
    {
        Json values = type("object");
        values.set("minProperties", Json::makeNumber(1));
        values.set("maxProperties", Json::makeNumber(64));
        const Json parameterToken = pattern(R"(^(?!.*["#=,\\])[!-~]+$)", 1, 31);
        values.set("propertyNames", parameterToken);
        values.set("additionalProperties", choice("anyOf", {
            pattern(R"(^(?!.*["#=,\\])[!-~]+$)", 1, 127), numeric() }));
        return object({ { "name", pattern(R"(^(?!;)(?!.*["#=,\\])[!-~]+$)", 1, 31) },
            { "parent", parameterToken }, { "values", std::move(values) } }, { "name", "parent", "values" });
    }
    if ( name == "setThingFrames" )
        return object({ { "thing", index() }, { "frames", frameList() } }, { "thing", "frames" }, false);
    if ( name == "buildThingPath" )
    {
        Json result = object({ { "thing", index() }, { "waypoints", frameList() },
            { "frames", Json::makeBool(true) } }, { "thing" }, false);
        allOf(result, atLeastOne({ "waypoints", "frames" }));
        allOf(result, condition(absent({ "waypoints" }), propertyRule({ { "frames", frameList() } })));
        return result;
    }
    if ( name == "setSectorAmbient" )
        return object({ { "sector", index() }, { "value", numeric(-kFloat, kFloat) } }, { "sector", "value" }, false);
    if ( name == "setSectorFlags" )
    {
        Json result = object({ { "sector", index() }, { "flags", numeric(kInt32Minimum, kUint32Maximum, true) },
            { "value", numeric(kInt32Minimum, kUint32Maximum, true) } }, { "sector" }, false);
        allOf(result, exactlyOne("flags", "value"));
        return result;
    }
    if ( name == "setLightRgb" )
    {
        Json result = object({ { "light", index() }, { "rgbIntensity", nullable(numeric()) },
            { "legacyRgbRange", nullable(numeric()) } }, { "light" }, false);
        allOf(result, atLeastOne({ "rgbIntensity", "legacyRgbRange" }));
        return result;
    }
    if ( name == "addCog" )
        return object({ { "script", authoredCogScriptName() }, { "scriptSha256", hash() }, { "expectedScriptSha256", hash() },
            { "cogSchemaSha256Hex", hash() }, { "defaults", type("boolean") }, { "bindings", array(cogBinding(), 0, 256) },
            { "resourceArchives", archives() } }, { "script", "bindings" }, false);
    if ( name == "rebindCogReference" )
    {
        Json result = object({ { "cog", index() }, { "parameter", index() }, { "symbol", text() },
            { "symbolName", text() }, { "kind", kinds(true) }, { "type", Json::makeBool(true) },
            { "targetIndex", numeric(-1, kInt32Maximum, true) }, { "sector", index() }, { "surface", index() },
            { "scriptSha256", hash() }, { "expectedScriptSha256", hash() }, { "cogSchemaSha256Hex", hash() },
            { "resourceArchives", archives() } }, { "cog", "parameter" }, false);
        allOf(result, atLeastOne({ "symbol", "symbolName" }));
        allOf(result, atLeastOne({ "kind", "type" }));
        allOf(result, condition(absent({ "kind" }), propertyRule({ { "type", kinds(true) } })));
        allOf(result, cogTarget());
        cogTargetRules(result, true);
        return result;
    }
    if ( name == "setCogValue" )
    {
        Json result = object({ { "cog", index() }, { "symbol", text(1) }, { "value", cogValue() },
            { "default", parseJson(R"({"type":"boolean","const":true})") }, { "scriptSha256", hash() },
            { "expectedScriptSha256", hash() }, { "resourceArchives", archives() } }, { "cog", "symbol" });
        allOf(result, exactlyOne("value", "default"));
        return result;
    }
    if ( name == "makeDoor" )
        return object({ { "thing", index() }, { "height", positive(100000) }, { "resourceArchives", archives() },
            { "scriptSha256", hash() }, { "expectedScriptSha256", hash() }, { "cogSchemaSha256Hex", hash() } },
            { "thing" }, false);
    if ( name == "batchUvProject" )
        return object({ { "surfaces", surfaces(true) }, { "direction", enumeration({ "+x", "-x", "+y", "-y", "+z", "-z" }) },
            { "origin", coordinate() }, { "texelsPerUnit", positive(1000000) } },
            { "surfaces", "direction", "origin", "texelsPerUnit" }, false);
    if ( name == "batchUvCylindrical" )
        return object({ { "surfaces", surfaces(true) }, { "axisOrigin", coordinate() },
            { "axisDirection", nonzero(vector()) }, { "texelsPerUnit", positive(1000000) } },
            { "surfaces", "axisOrigin", "axisDirection", "texelsPerUnit" }, false);
    if ( name == "batchUvSpherical" )
        return object({ { "surfaces", surfaces(true) }, { "center", coordinate() },
            { "poleDirection", nonzero(vector()) }, { "texelsPerUnit", positive(1000000) } },
            { "surfaces", "center", "poleDirection", "texelsPerUnit" }, false);
    if ( name == "batchUvTransform" )
        return object({ { "surfaces", surfaces(true) }, { "pivot", tuple(numeric(), 2) },
            { "scale", tuple(nonzeroNumber(numeric()), 2) }, { "rotationDegrees", numeric() },
            { "offset", tuple(numeric(), 2) } }, { "surfaces", "pivot", "scale" }, false);
    if ( name == "placeThingArray" )
        return object({ { "layout", placementLayout() }, { "template", token() }, { "namePrefix", token(123) },
            { "orientation", coordinate() }, { "layer", index() }, { "sector", index() } },
            { "layout", "template", "namePrefix" }, false);
    if ( name == "placeLightArray" )
        return object({ { "layout", placementLayout() }, { "color", color() }, { "intensity", numeric(0, 1000) },
            { "range", numeric(0.0001, 100000) }, { "flags", numeric(0, kInt32Maximum, true) }, { "layer", index() } },
            { "layout" }, false);
    if ( name == "applyMaskedSurfaceRecipe" )
    {
        Json result = object({ { "surfaces", surfaces(false) }, { "surfaceFlags", flagMask() },
            { "faceFlags", flagMask() }, { "adjoinFlags", flagMask() },
            { "minSlopeDegrees", numeric(0, 90) }, { "maxSlopeDegrees", numeric(0, 90) },
            { "copyFrom", surfacePair(false) }, { "copyFields", array(enumeration({ "material", "geometry", "extraLight",
                "surfaceFlags", "faceFlags", "adjoinFlags", "uvScale" })) },
            { "setSurfaceFlags", uint32() }, { "clearSurfaceFlags", uint32() },
            { "setFaceFlags", uint32() }, { "clearFaceFlags", uint32() },
            { "setAdjoinFlags", uint32() }, { "clearAdjoinFlags", uint32() }, { "material", text() } }, { "surfaces" }, false);
        allOf(result, condition(hasProperty("copyFields", array(text(), 1)), required({ "copyFrom" })));
        return result;
    }
    if ( name == "bakeLighting" )
    {
        Json result = object({ { "resourceRoots", array(text()) }, { "gobs", array(text()) },
            { "intensity", positive() }, { "range", positive() },
            { "bounceStrength", numeric() }, { "bounceRange", numeric() },
            { "skyIntensity", numeric() }, { "skyRange", numeric() }, { "skyColor", Json::makeBool(true) },
            { "lightFloor", numeric(0, 1) }, { "lightCap", numeric(0, 1) } }, {}, false);
        allOf(result, condition(hasProperty("skyIntensity", positive()), propertyRule({
            { "skyRange", positive() }, { "skyColor", color(false) } })));
        allOf(result, condition(hasProperty("bounceStrength", positive()), propertyRule({ { "bounceRange", positive() } })));
        return result;
    }
    if ( name == "buildHeightmapTerrain" )
        return object({ { "origin", coordinate() }, { "cellSize", numeric(0.001, 100) }, { "headroom", numeric(0.001, 100) },
            { "columns", numeric(1, 128, true) }, { "rows", numeric(1, 128, true) },
            { "heights", array(numeric(-1000, 1000), 1, 4096) } },
            { "origin", "cellSize", "headroom", "columns", "rows", "heights" }, false);
    if ( name == "buildSpiralStairs" )
    {
        Json angle = nonzeroNumber(numeric(-180, 180));
        angle.set("exclusiveMinimum", Json::makeNumber(-180));
        angle.set("exclusiveMaximum", Json::makeNumber(180));
        return object({ { "center", coordinate() }, { "innerRadius", numeric(0.001, 100) },
            { "outerRadius", numeric(0.001, 100) }, { "startAngleDegrees", numeric() },
            { "stepAngleDegrees", std::move(angle) }, { "rise", numeric(0.001, 100) },
            { "headroom", numeric(0.001, 100) }, { "steps", numeric(1, 256, true) } },
            { "center", "innerRadius", "outerRadius", "startAngleDegrees", "stepAngleDegrees", "rise", "headroom", "steps" }, false);
    }
    if ( name == "buildStairs" || name == "buildSwitchbackStairs" )
    {
        Json result = object({ { "origin", coordinate() }, { "direction", enumeration({ "+x", "-x", "+y", "-y" }) },
            { "width", numeric(0.001, 100) }, { "run", numeric(0.001, 100) }, { "rise", numeric(0.001, 100) },
            { "headroom", numeric(0.001, 100) } }, { "origin", "direction", "width", "run", "rise", "headroom" });
        Json properties = *result.find("properties");
        Json names = *result.find("required");
        if ( name == "buildStairs" )
        {
            properties.set("steps", numeric(1, 256, true));
            names.array.push_back(Json::makeString("steps"));
        }
        else
        {
            properties.set("landingRun", numeric(0.001, 100));
            properties.set("stepsPerFlight", numeric(1, 128, true));
            names.array.push_back(Json::makeString("landingRun"));
            names.array.push_back(Json::makeString("stepsPerFlight"));
        }
        result.set("properties", std::move(properties));
        result.set("required", std::move(names));
        return result;
    }
    if ( name == "buildSectorArray" )
        return object({ { "sectors", array(index(), 1) }, { "layout", placementLayout() } }, { "sectors", "layout" }, false);
    if ( name == "buildDome" )
        return object({ { "center", coordinate() }, { "radius", numeric(0.001, 100) }, { "headroom", numeric(0.001, 100) },
            { "segments", numeric(3, 64, true) }, { "rings", numeric(1, 64, true) } },
            { "center", "radius", "headroom", "segments", "rings" }, false);
    if ( name == "buildSweptCorridor" )
        return object({ { "center", coordinate() }, { "innerRadius", numeric(0.001, 100) },
            { "outerRadius", numeric(0.001, 100) }, { "startAngleDegrees", numeric() },
            { "sweepDegrees", nonzeroNumber(numeric()) }, { "segments", numeric(1, 256, true) },
            { "risePerSegment", numeric() }, { "headroom", numeric(0.001, 100) } },
            { "center", "innerRadius", "outerRadius", "startAngleDegrees", "sweepDegrees", "segments", "risePerSegment", "headroom" }, false);
    if ( name == "insertPrefab" )
        return object({ { "prefabFile", text() }, { "target", vector() } }, { "prefabFile" }, false);
    throw std::logic_error("Missing schema for registered operation: " + std::string(name));
}
struct StoredOperation
{
    AgentOperationDescriptor descriptor;
    Json schema;
};
const std::vector<StoredOperation>& operations()
{
    static const std::vector<StoredOperation> entries = []
    {
        // First 57 strings and their order are copied verbatim from the pinned
        // published cmdSchema inventory; the six restored shapes are appended.
        constexpr std::string_view descriptors[]{
            "addLight{position,rgb,intensity,range,rgbIntensity?,legacyRgbRange?,flags?,layer?}",
            "setLight{light,position?,rgb?,intensity?,range?,rgbIntensity?,legacyRgbRange?,flags?,layer?}",
            "setThingPosition{thing,position,sector}",
            "setThingOrientation{thing,orientation}",
            "setThingName{thing,name}",
            "setThingTemplate{thing,template}",
            "setThingValue{thing,key,value}",
            "clearThingArchitecture{thing}",
            "addBox{center,halfSize|min,max}",
            "extrudeSurface{sector,surface,distance}",
            "cleaveSurface{sector,surface,normal,point}",
            "cleaveSector{sector,normal,point}",
            "translateSectors{sectors,delta}",
            "scaleSectors{sectors,pivot,factor|factors,scaleTextures?}",
            "rotateSectors{sectors,pivot,axis,angleDegrees}",
            "translateThings{things,delta,moveFrames?}",
            "translateLights{lights,delta}",
            "adjoinSurfaces{sector,surface,otherSector,otherSurface}",
            "registerMaterial{name}",
            "registerColormap{name}",
            "renameMaterial{materialIndex,name}",
            "renameColormap{colormapIndex,name}",
            "setSurfaceMaterial{sector,surface,materialIndex}",
            "setSectorColormap{sector,colormapIndex}",
            "setSurfaceRenderModes{sector,surface,geometryMode,lightMode,textureMode}",
            "setSurfaceUv{sector,surface,uv}",
            "setVertexLight{sector,vertex,channel,value}",
            "setGravity{value}",
            "setLevelHeader{gravity?,ceilingSkyHeight?,ceilingSkyOffset?,horizonDistance?,horizonPixelsPerRev?,horizonSkyOffset?,mipmapDistances?,lodDistances?,perspectiveDistance?,gouraudDistance?}",
            "importTemplates{templates,templateSource?}",
            "defineTemplate{name,parent,values{}}",
            "addThing{sector,template,name,position}",
            "setThingFrames{thing,frames}",
            "setSectorAmbient{sector,value}",
            "setSectorFlags{sector,flags|value}",
            "setLightRgb{light,rgbIntensity,legacyRgbRange}",
            "addCog{script,scriptSha256,defaults?,bindings[{symbol,kind,targetIndex|sector+surface|value|default}]}",
            "makeDoor{thing,height?}",
            "batchUvProject{surfaces,direction,origin,texelsPerUnit}",
            "batchUvCylindrical{surfaces,axisOrigin,axisDirection,texelsPerUnit}",
            "batchUvSpherical{surfaces,center,poleDirection,texelsPerUnit}",
            "batchUvTransform{surfaces,pivot,scale,rotationDegrees,offset}",
            "placeThingArray{layout,template,namePrefix}",
            "placeLightArray{layout}",
            "applyMaskedSurfaceRecipe{surfaces,surfaceFlags,faceFlags,adjoinFlags,minSlopeDegrees,maxSlopeDegrees,copyFrom,copyFields,setSurfaceFlags,clearSurfaceFlags,setFaceFlags,clearFaceFlags,setAdjoinFlags,clearAdjoinFlags,material}",
            "bakeLighting{resourceRoots,gobs,intensity,range,bounceStrength,bounceRange,skyIntensity,skyRange,skyColor,lightFloor?,lightCap?}",
            "buildHeightmapTerrain{origin,cellSize,headroom,columns,rows,heights}",
            "buildSpiralStairs{center,innerRadius,outerRadius,startAngleDegrees,stepAngleDegrees,rise,headroom,steps}",
            "buildStairs{origin,direction,width,run,rise,headroom,steps}",
            "buildSwitchbackStairs{origin,direction,width,run,rise,headroom,landingRun,stepsPerFlight}",
            "buildSectorArray{sectors,layout}",
            "buildDome{center,radius,headroom,segments,rings}",
            "buildSweptCorridor{center,innerRadius,outerRadius,startAngleDegrees,sweepDegrees,segments,risePerSegment,headroom}",
            "buildThingPath{thing,waypoints}",
            "insertPrefab{prefabFile,target}",
            "rebindCogReference{cog,parameter,symbol,kind,targetIndex|sector+surface,scriptSha256}",
            "setCogValue{cog,symbol,value|default,scriptSha256?}",
            "setVertexPosition{sector,vertex,position}",
            "fitSurfaceTexture{sector,surface,width,height}",
            "alignSurfaceTexture{sector,surface,otherSector,otherSurface}",
            "bindThingArchitecture{thing,lightingIndex}",
            "rebindThingArchitecture{thing,policy}",
            "bakeThingArchitecture{thing,profile}",
        };
        std::vector<StoredOperation> result;
        result.reserve(std::size(descriptors));
        for ( const auto descriptor : descriptors )
        {
            const auto name = descriptor.substr(0, descriptor.find('{'));
            result.push_back({ { name, descriptor }, operation(std::string(name), operationShape(name)) });
        }
        return result;
    }();
    return entries;
}
} // namespace

const std::vector<AgentCommandDescriptor>& agentCommandDescriptors()
{
    static const std::vector<AgentCommandDescriptor> commands{
        { "help", "red-agent help", "List principal CLI invocation forms and exit codes." },
        { "schema", "red-agent schema [commands|patch|<operation>]", "Discover command names or standalone patch JSON Schemas without a level; no topic retains the legacy inventory." },
        { "inspect", "red-agent inspect <level>", "Read document identity, counts and header summary." },
        { "query", "red-agent query <level> <query.json|->", "Read bounded record pages from one source revision." },
        { "select", "red-agent select <level> <select.json|->", "Resolve a spatial target or revision-bound selection token." },
        { "validate", "red-agent validate <level> [validate.json|-] [--strict]", "Inspect structural, seam and requested resource checks without authorizing repair." },
        { "workbench-query", "red-agent workbench-query <level> <query.json|->", "Run exact typed filters over the supported workbench collections." },
        { "arch-validate", "red-agent arch-validate <level> <request.json|->", "Inspect MotS ARCH binding and resource compatibility." },
        { "plan", "red-agent plan <level> <patch.json|->", "Prepare a patch and return the proposed output hash; publish nothing." },
        { "apply", "red-agent apply <level> <patch.json|-> <new-output> [expected-output-sha256]", "Recompute a patch, check an optional reviewed output hash and publish a new level." },
        { "new", "red-agent new <new-output.jkl|new-output.sed> [mots|jk|droidworks|new.json|-]", "Create an asset-free closed-room scaffold with placeholder resources." },
        { "convert", "red-agent convert <input> <new-output> [expected-output-sha256]", "Convert a supported document container through preservation admission; project identity is retained." },
        { "archive", "red-agent archive <archive.gob|archive.goo>", "List archive entry identities and hashes." },
        { "extract", "red-agent extract <archive> <exact-entry-name> <new-output>", "Extract one exact archive entry into a fresh output." },
        { "launch-plan", "red-agent launch-plan <launch-request.json|->", "Prepare an engine launch plan without exporting a level or starting a process." },
        { "snapshot", "red-agent snapshot <level> <snapshot.json|-> <new-output.png>", "Render world surfaces with software-v1; Things/models, sprites and dynamic lighting are excluded." },
        { "bindings", "red-agent bindings <level> <bindings.json|->", "Inspect typed placed-COG references using captured script resources." },
        { "addCog", "red-agent addCog <level> <addCog.json|-> [new-output [expected-output-sha256]]", "Plan or apply one typed COG append under the normal preservation/publication boundary." },
        { "rebindCogReference", "red-agent rebindCogReference <level> <rebind.json|-> [new-output [expected-output-sha256]]", "Plan or apply one typed placed-COG reference change." },
        { "overview", "red-agent overview <level> <overview.json|->", "Compute deterministic overhead/three-quarter framing and optionally publish a world-surface snapshot using request.output." },
        { "dependencies", "red-agent dependencies <level.jkl> <request.json|->", "Inspect captured lexical dependencies, exact archive provenance and bounded graph completeness without publication." },
        { "package-plan", "red-agent package-plan <level.jkl> <package.json|->", "Plan explicit resolved package entries, hashes and unresolved dependency scopes." },
        { "package-apply", "red-agent package-apply <level.jkl> <package.json|-> <new-output.gob|.goo> <expected-output-sha256> <expected-manifest-sha256>", "Publish a fresh archive only after both reviewed hashes match the recomputed package." },
        { "native-inspect", "red-agent native-inspect <input> <jk|mots|ijim|droidworks> <jkl|native-sed> <worker-config.json>", "Use the declared bounded inspection worker; this is not a live GUI editing bridge." },
        { "prefab-capture", "red-agent prefab-capture <level> <request.json|-> <new-output.prefab>", "Capture selected sectors as an internally hash-checked prefab." },
        { "export-dxf", "red-agent export-dxf <level> <new-output.dxf>", "Write world geometry to a fresh DXF output." },
        { "dxf-inspect", "red-agent dxf-inspect <file.dxf>", "Inspect supported DXF faces and bounds." },
        { "dxf-import", "red-agent dxf-import <input.dxf> <height> <new-output.jkl>", "Extrude supported horizontal DXF footprints into a new JK level." },
        { "cog-check", "red-agent cog-check <file.cog> <jk|mots|ijim> [--strict]", "Run bounded COG preflight for the declared game; this is not a compiler or runtime." },
        { "mat-inspect", "red-agent mat-inspect <file.mat>", "Inspect MAT format, cels and mipmap metadata." },
        { "mat-export-png", "red-agent mat-export-png <file.mat> <new-output.png> [cel] [palette.cmp]", "Decode one MAT cel to PNG using an optional palette." },
        { "mat-create", "red-agent mat-create <request.json|-> <new-output.mat>", "Map captured PNG cels to an indexed MAT under a captured CMP palette." },
        { "archive-create", "red-agent archive-create <request.json|-> <new-output.gob|.goo>", "Pack captured loose resource entries and read supported asset types back before publication." },
        { "model-import", "red-agent model-import <request.json|-> <new-output.3do>", "Import supported OBJ/GLB geometry to 3DO 2.1 with explicit material mapping and preserved normals by default." },
        { "model-lighting-inspect", "red-agent model-lighting-inspect <input.3do>", "Inspect all-LOD stored lighting, normals and render modes." },
        { "model-lighting-plan", "red-agent model-lighting-plan <input.3do> <request.json|->", "Plan a source-preserving all-LOD bind-pose model-lighting variant." },
        { "model-lighting-apply", "red-agent model-lighting-apply <input.3do> <request.json|-> <expected-output-sha256> <new-output.3do>", "Publish the source- and reviewed-output-hash-bound model-lighting variant." },
        { "terrain-3do", "red-agent terrain-3do <request.json|-> <new-output.3do>", "Build a standalone heightmap terrain model from typed cells." },
        { "statistics", "red-agent statistics <level>", "Read level statistics and per-project limits." },
        { "key-inspect", "red-agent key-inspect <file.key> [file.3do]", "Inspect KEY timeline data and optionally compare model hierarchy compatibility." },
        { "key-plan", "red-agent key-plan <input.key> <request.json|->", "Plan supported standalone KEY timeline edits without publication." },
        { "key-apply", "red-agent key-apply <input.key> <request.json|-> <expected-output-sha256> <new-output.key>", "Publish a fresh KEY after recomputing and matching the reviewed hash." }
    };
    return commands;
}

const std::vector<AgentOperationDescriptor>& agentOperationDescriptors()
{
    static const std::vector<AgentOperationDescriptor> descriptors = []
    {
        std::vector<AgentOperationDescriptor> result;
        for ( const auto& entry : operations() )
        {
            result.push_back(entry.descriptor);
        }
        return result;
    }();
    return descriptors;
}

Json patchOperationSchema(const std::string& name)
{
    const auto& entries = operations();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const StoredOperation& entry)
    {
        return entry.descriptor.name == name;
    });
    if ( found == entries.end() )
    {
        throw AgentError{ kExitUsage, "Unknown schema topic '" + name
            + "'; use commands, patch, or an exact patch operation name", "schemaTopicUnknown" };
    }
    return found->schema;
}

Json patchRequestSchema()
{
    Json choices = Json::makeArray();
    for ( const auto& entry : operations() )
    {
        choices.array.push_back(entry.schema);
    }
    Json item = Json::makeObject();
    item.set("oneOf", std::move(choices));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "inputSha256", hash() }, { "sourceRevision", hash() },
        { "operations", array(std::move(item), 1, 1000) },
        { "resourceArchives", archives() }, { "templateSource", boundFile(true) },
        { "resolveExportDependencies", type("boolean") },
        { "expectedScriptSha256", hash() }, { "scriptSha256", hash() }, { "cogSchemaSha256Hex", hash() }
    }, { "operations" }, false);
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED level patch request"));
    result.set("description", Json::makeString(
        "Request for plan/apply. Source hashes and schemaVersion remain optional for legacy compatibility; supply source bindings for reviewed edits. Unknown root fields remain accepted. This schema describes supported field shapes; geometry, resources, script types, source identity and cross-field arithmetic remain runtime checks."));
    const auto needsRootField = [&](const Names operationNames, const char* fieldName,
        const bool allowOperationOverride)
    {
        Json operation = propertyRule({ { "op", enumeration(operationNames) } });
        operation.set("required", strings({ "op" }));
        if ( allowOperationOverride )
        {
            allOf(operation, absent({ fieldName }));
        }
        Json list = type("array");
        list.set("contains", std::move(operation));
        allOf(result, condition(propertyRule({ { "operations", std::move(list) } }), required({ fieldName })));
    };
    needsRootField({ "importTemplates" }, "templateSource", true);
    needsRootField({ "addCog", "rebindCogReference", "setCogValue", "makeDoor" }, "resourceArchives", true);
    needsRootField({ "bindThingArchitecture", "rebindThingArchitecture", "bakeThingArchitecture" },
        "resourceArchives", false);
    Json exportEnabled = propertyRule({ { "resolveExportDependencies",
        parseJson(R"({"type":"boolean","const":true})") } });
    exportEnabled.set("required", strings({ "resolveExportDependencies" }));
    allOf(result, condition(std::move(exportEnabled), required({ "resourceArchives" })));
    result.set("x-operationCount", Json::makeNumber(static_cast<double>(operations().size())));
    result.set("x-runtimeConstraints", Json::makeString(
        "resourceArchives/templateSource and script hashes are consumed only by operations/export paths that need them; resource closure, precedence aliases and exact digest matches are checked then. importTemplates needs operation/root templateSource. COG operations and makeDoor need operation/root resourceArchives; ARCH uses patch-root archives. resolveExportDependencies defaults true for non-native output with root archives; explicit true requires archives and a text output container."));
    return result;
}

Json focusedSchema(const std::string& topic)
{
    if ( topic == "dependencies" )
    {
        return dependencyRequestSchema();
    }
    if ( topic == "patch" )
    {
        return patchRequestSchema();
    }
    if ( topic == "commands" )
    {
        Json result = type("string");
        Json names = Json::makeArray();
        Json catalog = Json::makeArray();
        for ( const auto& command : agentCommandDescriptors() )
        {
            names.array.push_back(Json::makeString(std::string(command.name)));
            Json row = Json::makeObject();
            row.set("name", Json::makeString(std::string(command.name)));
            row.set("usage", Json::makeString(std::string(command.usage)));
            row.set("description", Json::makeString(std::string(command.description)));
            catalog.array.push_back(std::move(row));
        }
        result.set("$schema", Json::makeString(std::string(kDialect)));
        result.set("title", Json::makeString("RED CLI command name"));
        result.set("enum", std::move(names));
        result.set("x-commandCount", Json::makeNumber(static_cast<double>(agentCommandDescriptors().size())));
        result.set("x-commands", std::move(catalog));
        result.set("description", Json::makeString("CLI command names with exact positional usage. Command body domains retain their individual runtime contracts."));
        return result;
    }
    return patchOperationSchema(topic);
}
} // namespace sed_agent
