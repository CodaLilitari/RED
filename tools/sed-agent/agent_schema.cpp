#include "agent_schema.hpp"

#include "agent_workspace.hpp"
#include "sed/level/overview.hpp"
#include "sed/level/package.hpp"
#include "sed/render/software_snapshot.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/obj_import.hpp"
#include "sed/assets/runtime_archive.hpp"

#include <algorithm>
#include <cmath>
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
    if ( name == "buildStairs" || name == "buildSwitchbackStairs" )
    {
        runtime += " Optional walkability.maxStepRise is a caller-supplied advisory only; exceeding a valid limit never clamps or refuses structurally valid stairs. Reports compare recipe-internal rises at creation, exclude external joins, later edits, clearance and player/engine settings, and always leave gameplayVerified false. The level output hash does not bind advisory settings.";
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
    if ( name == "adjoinSurfaces" || name == "alignSurfaceTexture" || name == "connectSurfaces" )
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
    if ( name == "carveSurfaceRect" )
    {
        Json axis = type("string");
        Json values = Json::makeArray();
        for ( const char* a : { "x", "y", "z" } ) values.array.push_back(Json::makeString(a));
        axis.set("enum", std::move(values));
        return object({ { "sector", index() }, { "axis", axis }, { "at", numeric() },
            { "min", tuple(numeric(), 2) }, { "max", tuple(numeric(), 2) }, { "materialIndex", index() },
            { "uvSize", tuple(numeric(1e-3, 65536), 2) }, { "lightMode", numeric(0, 3, true) } },
            { "sector", "axis", "at", "min", "max" });
    }
    if ( name == "deleteThings" || name == "deleteLights" || name == "deleteCogs" || name == "placeSectorLights"
        || name == "removeRedundantVertices" || name == "setSectorProperties" || name == "makeUnderwater" )
    {
        const char* list = name == "deleteThings" ? "things" : name == "deleteLights" ? "lights"
            : name == "deleteCogs" ? "cogs" : "sectors";
        Json indices = array(index(), 1, 4096);
        indices.set("uniqueItems", Json::makeBool(true));
        Json light = choice("oneOf", { numeric(0, 1), tuple(numeric(0, 1), 3) });
        if ( name == "placeSectorLights" )
            return object({ { list, indices }, { "intensity", numeric(0.01, 1000) } }, { list, "intensity" });
        if ( name == "setSectorProperties" )
            return object({ { list, indices }, { "extraLight", light }, { "tint", light }, { "sound", type("string") },
                { "soundVolume", numeric(0, 1) } }, { list });
        if ( name == "makeUnderwater" )
            return object({ { list, indices }, { "tint", light }, { "sound", type("string") }, { "soundVolume", numeric(0, 1) },
                { "waterSurfaces", type("boolean") }, { "waterMaterialIndex", index() } }, { list });
        return object({ { list, indices } }, { list });
    }
    if ( name == "setSurfaceProperties" || name == "unadjoinSurfaces" )
    {
        Json surfaces = array(tuple(index(), 2), 1, 4096);
        if ( name == "unadjoinSurfaces" )
            return object({ { "surfaces", surfaces }, { "materialIndex", index() } }, { "surfaces" });
        return object({ { "surfaces", surfaces }, { "extraLight", choice("oneOf", { numeric(0, 1), tuple(numeric(0, 1), 3) }) },
            { "uvScale", tuple(numeric(0.001, 1000), 2) } }, { "surfaces" });
    }
    if ( name == "shadowCuts" )
    {
        return object({ { "mode", enumeration({ "shadowEdges", "sizeLimit" }) },
            { "lights", array(index(), 1, 4096) }, { "maxEdge", numeric(0.001, 1000) },
            { "minPiece", numeric(0, 10) }, { "maxNewSurfaces", numeric(0, 100000, true) },
            { "maxDepth", numeric(0, 64, true) } });
    }
    if ( name == "removeShadowCuts" )
    {
        return object({ { "sectors", array(index(), 1, 65536) } });
    }
    if ( name == "flipSurfaces" || name == "mergeSurfaces" || name == "subdivideSurfaces"
        || name == "planarizeSurfaces" || name == "repairSurfaces" )
    {
        Json surfaces = array(tuple(index(), 2), 1, 4096);
        if ( name == "repairSurfaces" )
            return object({ { "surfaces", surfaces }, { "solid", type("boolean") }, { "passable", type("boolean") } },
                { "surfaces" });
        return object({ { "surfaces", surfaces } }, { "surfaces" });
    }
    if ( name == "mirrorSectors" || name == "mirrorThings" || name == "mirrorLights"
        || name == "rotateThings" || name == "rotateLights" )
    {
        const char* list = name == "mirrorSectors" ? "sectors"
            : (name == "mirrorThings" || name == "rotateThings") ? "things" : "lights";
        Json indices = array(index(), 1, 4096);
        indices.set("uniqueItems", Json::makeBool(true));
        if ( name == "rotateThings" )
            return object({ { list, indices }, { "pivot", coordinate() }, { "angleDegrees", numeric(-36000, 36000) },
                { "axis", nonzero(coordinate()) }, { "orientation", type("boolean") } }, { list, "pivot", "angleDegrees" });
        if ( name == "rotateLights" )
            return object({ { list, indices }, { "pivot", coordinate() }, { "angleDegrees", numeric(-36000, 36000) },
                { "axis", nonzero(coordinate()) } }, { list, "pivot", "angleDegrees" });
        return object({ { list, indices }, { "normal", nonzero(coordinate()) }, { "point", coordinate() } },
            { list, "normal", "point" });
    }
    if ( name == "duplicateSector" )
        return object({ { "sector", index() }, { "offset", coordinate() } }, { "sector", "offset" });
    if ( name == "fitAndAdjoin" || name == "stitchSurfaces" )
        return object({ { "sector", index() }, { "surface", index() }, { "otherSector", index() },
            { "otherSurface", index() } }, { "sector", "surface", "otherSector", "otherSurface" });
    if ( name == "mergeSectors" )
        return object({ { "sector", index() }, { "otherSector", index() } }, { "sector", "otherSector" });
    if ( name == "insetSurface" )
        return object({ { "sector", index() }, { "surface", index() }, { "factor", numeric(1e-6, 1 - 1e-6) },
            { "adjoinPair", type("boolean") } }, { "sector", "surface", "factor" });
    if ( name == "mirrorFloor" )
        return object({ { "sector", index() }, { "surface", index() }, { "reflectivity", numeric(0, 1) },
            { "openingMaterialIndex", index() }, { "allowOverlap", type("boolean") } }, { "sector", "surface" });
    if ( name == "deleteSectors" )
    {
        Json result = object({ { "sectors", array(index(), 1) }, { "materialIndex", index() } }, { "sectors" });
        result.object["properties"].object["sectors"].set("uniqueItems", Json::makeBool(true));
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
    if ( name == "doorAssistant" )
        return object({ { "pieces", array(index(), 1, 4) }, { "openOffset", vector(-200, 200) },
            { "speed", positive(100) }, { "wait", numeric(0, 600) }, { "light", numeric(0, 2) },
            { "resourceArchives", archives() }, { "scriptSha256", hash() }, { "expectedScriptSha256", hash() } },
            { "pieces", "openOffset" }, false);
    if ( name == "switchAssistant" )
        return object({ { "pieces", array(index(), 1, 4) }, { "openOffset", vector(-200, 200) },
            { "switches", array(surfacePair(), 1, 2) }, { "speed", positive(100) }, { "wait", numeric(0, 600) },
            { "light", numeric(0, 2) }, { "resourceArchives", archives() }, { "scriptSha256", hash() },
            { "expectedScriptSha256", hash() } }, { "pieces", "openOffset", "switches" }, false);
    if ( name == "elevatorAssistant" )
        return object({ { "elevator", index() }, { "travel", vector(-200, 200) }, { "button", surfacePair() },
            { "callAdjoins", array(surfacePair(), 0, 3) }, { "speed", positive(100) }, { "wait", numeric(0, 600) },
            { "startWait", numeric(0, 60) }, { "resourceArchives", archives() }, { "scriptSha256", hash() },
            { "expectedScriptSha256", hash() } }, { "elevator", "travel" }, false);
    if ( name == "objectivesAssistant" )
        return object({ { "goals", array(object({ { "trigger", enumeration({ "reach", "use", "kill" }) }, { "target", index() } },
                            { "trigger", "target" }), 1, 8) },
            { "goalBase", numeric(0, 99000, true) }, { "exit", surfacePair() }, { "endWhenDone", type("boolean") } },
            { "goals" }, false);
    if ( name == "pursuitAssistant" )
        return object({ { "enemy", index() }, { "route", array(index(), 0, 4) }, { "wakeSector", index() },
            { "wakeDistance", numeric(0.1, 100) }, { "moveSpeed", numeric(0.01, 20) }, { "ramDamage", numeric(0, 1000) },
            { "fireInterval", numeric(0.05, 60) }, { "survivalChance", numeric(0, 1) } }, { "enemy" }, false);
    if ( name == "cutsceneAssistant" )
        return object({ { "camera", index() }, { "trigger", index() }, { "speed", positive(100) }, { "hold", numeric(0, 600) },
            { "once", type("boolean") } }, { "camera" }, false);
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
            { "rgbIntensity", nullable(numeric(0, 1000)) },
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
            { "lightFloor", numeric(0, 1) }, { "lightCap", numeric(0, 1) },
            { "mirrorReflectivity", numeric(0, 1) },
            { "angleShading", type("boolean") }, { "waterAbsorption", type("boolean") } }, {}, false);
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
        properties.set("walkability", object({ { "maxStepRise", positive(100) } }, { "maxStepRise" }));
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
    if ( name == "conformSeams" )
    {
        Json limits = object({ { "maxSectorPairs", numeric(0, 1000000000, true) },
            { "maxSurfacePairs", numeric(0, 32000000, true) }, { "maxEdgePairs", numeric(0, 256000000, true) } }, {});
        return object({ { "maxInsertions", numeric(1, 100000, true) }, { "allowApproximate", type("boolean") },
            { "allowRemaining", type("boolean") }, { "seamLimits", std::move(limits) } }, {});
    }
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
        // published cmdSchema inventory; the six restored shapes are appended,
        // and operations added since follow them.
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
            "doorAssistant{pieces,openOffset,speed?,wait?,light?}",
            "elevatorAssistant{elevator,travel,button?,callAdjoins?,speed?,wait?,startWait?}",
            "switchAssistant{pieces,openOffset,switches,speed?,wait?,light?}",
            "objectivesAssistant{goals[{trigger,target}],goalBase?,exit?,endWhenDone?}",
            "cutsceneAssistant{camera,trigger?,speed?,hold?,once?}",
            "pursuitAssistant{enemy,route?,wakeSector?,wakeDistance?,moveSpeed?,ramDamage?,fireInterval?,survivalChance?}",
            "batchUvProject{surfaces,direction,origin,texelsPerUnit}",
            "batchUvCylindrical{surfaces,axisOrigin,axisDirection,texelsPerUnit}",
            "batchUvSpherical{surfaces,center,poleDirection,texelsPerUnit}",
            "batchUvTransform{surfaces,pivot,scale,rotationDegrees,offset}",
            "placeThingArray{layout,template,namePrefix}",
            "placeLightArray{layout,color?,intensity?,rgbIntensity?,range?,flags?,layer?}",
            "applyMaskedSurfaceRecipe{surfaces,surfaceFlags,faceFlags,adjoinFlags,minSlopeDegrees,maxSlopeDegrees,copyFrom,copyFields,setSurfaceFlags,clearSurfaceFlags,setFaceFlags,clearFaceFlags,setAdjoinFlags,clearAdjoinFlags,material}",
            "bakeLighting{resourceRoots,gobs,intensity,range,bounceStrength,bounceRange,skyIntensity,skyRange,skyColor,lightFloor?,lightCap?,mirrorReflectivity?,angleShading?,waterAbsorption?}",
            "buildHeightmapTerrain{origin,cellSize,headroom,columns,rows,heights}",
            "buildSpiralStairs{center,innerRadius,outerRadius,startAngleDegrees,stepAngleDegrees,rise,headroom,steps}",
            "buildStairs{origin,direction,width,run,rise,headroom,steps,walkability?}",
            "buildSwitchbackStairs{origin,direction,width,run,rise,headroom,landingRun,stepsPerFlight,walkability?}",
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
            "conformSeams{maxInsertions?,allowApproximate?,allowRemaining?,seamLimits?}",
            "connectSurfaces{sector,surface,otherSector,otherSurface}",
            "deleteSectors{sectors,materialIndex?}",
            "carveSurfaceRect{sector,axis,at,min,max,materialIndex?,uvSize?,lightMode?}",
            "mirrorFloor{sector,surface,reflectivity?,openingMaterialIndex?,allowOverlap?}",
            "deleteThings{things}",
            "deleteLights{lights}",
            "deleteCogs{cogs}",
            "setSectorProperties{sectors,extraLight?,tint?,sound?,soundVolume?}",
            "setSurfaceProperties{surfaces,extraLight?,uvScale?}",
            "unadjoinSurfaces{surfaces,materialIndex?}",
            "makeUnderwater{sectors,tint?,sound?,soundVolume?,waterSurfaces?,waterMaterialIndex?}",
            "placeSectorLights{sectors,intensity}",
            "removeRedundantVertices{sectors}",
            "flipSurfaces{surfaces}",
            "mirrorSectors{sectors,normal,point}",
            "duplicateSector{sector,offset}",
            "fitAndAdjoin{sector,surface,otherSector,otherSurface}",
            "stitchSurfaces{sector,surface,otherSector,otherSurface}",
            "mergeSurfaces{surfaces}",
            "mergeSectors{sector,otherSector}",
            "subdivideSurfaces{surfaces}",
            "insetSurface{sector,surface,factor,adjoinPair?}",
            "planarizeSurfaces{surfaces}",
            "repairSurfaces{surfaces,solid?,passable?}",
            "rotateThings{things,pivot,angleDegrees,axis?,orientation?}",
            "rotateLights{lights,pivot,angleDegrees,axis?}",
            "mirrorThings{things,normal,point}",
            "mirrorLights{lights,normal,point}",
            "shadowCuts{mode?,lights?,maxEdge?,minPiece?,maxNewSurfaces?,maxDepth?}",
            "removeShadowCuts{sectors?}",
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
        { "schema", "red-agent schema [commands|patch|dependencies|package-plan|package-apply|jo-ja-inspect|jo-ja-plan|jo-ja-apply|mat-create|cmp-create|model-import|archive-create|snapshot|overview|<operation>]", "Discover command names or standalone request/operation JSON Schemas without a level; no topic retains the legacy inventory." },
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
        { "snapshot", "red-agent snapshot <level> <snapshot.json|-> <new-output.png>", "Render world surfaces with software-v1; optional thingMode=static adds bounded LOD0/rest-pose models and classic cel0 sprites, not runtime lighting or animation." },
        { "bindings", "red-agent bindings <level> <bindings.json|->", "Inspect typed placed-COG references using captured script resources." },
        { "addCog", "red-agent addCog <level> <addCog.json|-> [new-output [expected-output-sha256]]", "Plan or apply one typed COG append under the normal preservation/publication boundary." },
        { "rebindCogReference", "red-agent rebindCogReference <level> <rebind.json|-> [new-output [expected-output-sha256]]", "Plan or apply one typed placed-COG reference change." },
        { "overview", "red-agent overview <level> <overview.json|->", "Compute deterministic world-surface framing and optionally publish a snapshot using request.output; thingMode=static adds bounded static Thing geometry." },
        { "dependencies", "red-agent dependencies <level.jkl> <request.json|->", "Inspect captured lexical dependencies, exact archive provenance and bounded graph completeness without publication." },
        { "package-plan", "red-agent package-plan <level.jkl> <package.json|->", "Plan explicit resolved package entries, hashes and unresolved dependency scopes." },
        { "package-apply", "red-agent package-apply <level.jkl> <package.json|-> <new-output.gob|.goo> <expected-output-sha256> <expected-manifest-sha256>", "Publish a fresh archive only after both reviewed hashes match the recomputed package." },
        { "native-inspect", "red-agent native-inspect <input> <jk|mots|ijim|droidworks> <jkl|native-sed> <worker-config.json>", "Use the declared bounded inspection worker; this is not a live GUI editing bridge." },
        { "prefab-capture", "red-agent prefab-capture <level> <request.json|-> <new-output.prefab>", "Capture selected sectors as an internally hash-checked prefab." },
        { "export-dxf", "red-agent export-dxf <level> <new-output.dxf>", "Write world geometry to a fresh DXF output." },
        { "dxf-inspect", "red-agent dxf-inspect <file.dxf>", "Inspect supported DXF faces and bounds." },
        { "dxf-import", "red-agent dxf-import <input.dxf> <height> <new-output.jkl>", "Extrude supported horizontal DXF footprints into a new JK level." },
        { "cog-check", "red-agent cog-check <file.cog> <jk|mots|ijim> [--strict]", "Run bounded COG preflight for the declared game; this is not a compiler or runtime." },
        { "cog-compile", "red-agent cog-compile <file.cog> <jk|mots> [--strict] [--listing]", "Compile a COG the way retail JK or MotS loads it: whether it loads, syntax errors with the engine's line numbers, unknown verbs and messages, wrong argument counts, missing labels and exhausted engine tables. Nothing is run." },
        { "assistant-check", "red-agent assistant-check <request.json|->", "Run a placed assistant COG (door, elevator, switch, objectives or cutscene) in the COG runtime with its kind's scenario: press the door, call the lift, try the locked door and then its switch, complete each goal and take the exit, play the cutscene. Reports each expectation and the timeline; strict exits 5 when one fails." },
        { "assistant-script", "red-agent assistant-script <red_goals.cog|red_cutscene.cog> <new-output.cog>", "Write one of the scripts RED's objectives and cutscene assistants place, for the level's project or GOB." },
        { "cog-templates", "red-agent cog-templates [set|-] [library-folder]", "List RED's COG template library (vehicles and enemies: the player swoop, enemy swoop pursuit, bowcaster Tusken on a Kell, rideable Kell), or one set: its scripts and roles, the placed-COG bindings in order, its template rows, and the models, keys and sounds the user supplies." },
        { "cog-template-write", "red-agent cog-template-write <set> <project-folder> [library-folder]", "Copy a template set into a project: scripts into cog/, template rows and notes into templates/. Never overwrites a different file; with any conflict nothing is written." },
        { "cog-run", "red-agent cog-run <request.json|->", "Run compiled COGs on a simulated clock against a stand-in world built from a level: activate, enter, cross, damage and message events in, a timed log of messages, verbs, prints and arrivals out, with the things that moved. Over a hundred common verbs are simulated; the rest are logged as not simulated. Nothing is written." },
        { "mat-inspect", "red-agent mat-inspect <file.mat>", "Inspect MAT format, cels and mipmap metadata." },
        { "mat-export-png", "red-agent mat-export-png <file.mat> <new-output.png> [cel] [palette.cmp]", "Decode one MAT cel to PNG using an optional palette." },
        { "mat-create", "red-agent mat-create <request.json|-> <new-output.mat>", "Create an opaque MAT from 1 to 16 captured PNG/JPEG cels: default indexed8 with CMP, or explicit rgb565 without palette-only options." },
        { "cmp-create", "red-agent cmp-create <request.json|-> <new-output.cmp>", "Create a tinted colormap from a captured master CMP and an RGB tint, for a sector's colormap in both renderers." },
        { "cmp-master-create", "red-agent cmp-master-create <request.json|-> <new-output.cmp>", "Create a level master colormap with new colours in the per-level palette slots, light and transparency tables made by the stock rules, every other entry kept." },
        { "archive-create", "red-agent archive-create <request.json|-> <new-output.gob|.goo>", "Pack captured loose resource entries and read supported asset types back before publication." },
        { "model-import", "red-agent model-import <request.json|-> <new-output.3do>", "Import supported OBJ/GLB geometry to 3DO 2.1 with explicit material mapping and preserved normals by default." },
        { "jo-ja-inspect", "red-agent jo-ja-inspect <source.bsp>", "Inspect Raven RBSP entities, world shaders, patches, submodels and brushes." },
        { "jo-ja-plan", "red-agent jo-ja-plan <request.json|->", "Prepare a hash-bound JK or MotS scene scaffold without publishing an archive." },
        { "jo-ja-apply", "red-agent jo-ja-apply <request.json|-> <new.gob|new.goo> <expected-output-sha256> <expected-manifest-sha256>", "Revalidate captured inputs and the reviewed archive and manifest hashes, then publish one new archive." },
        { "model-lighting-inspect", "red-agent model-lighting-inspect <input.3do>", "Inspect all-LOD stored lighting, normals and render modes." },
        { "model-lighting-plan", "red-agent model-lighting-plan <input.3do> <request.json|->", "Plan a source-preserving all-LOD bind-pose model-lighting variant." },
        { "model-lighting-apply", "red-agent model-lighting-apply <input.3do> <request.json|-> <expected-output-sha256> <new-output.3do>", "Publish the source- and reviewed-output-hash-bound model-lighting variant." },
        { "terrain-3do", "red-agent terrain-3do <request.json|-> <new-output.3do>", "Build a standalone heightmap terrain model from typed cells." },
        { "statistics", "red-agent statistics <level>", "Read level statistics and per-project limits." },
        { "key-inspect", "red-agent key-inspect <file.key> [file.3do]", "Inspect KEY timeline data and optionally compare model hierarchy compatibility; with a model, modelIssues lists joints the model lacks, renamed parts, entries past the end and stale deltas." },
        { "key-plan", "red-agent key-plan <input.key> <request.json|->", "Plan supported standalone KEY timeline edits without publication: insertEntry, eraseEntry and computeDeltas (every entry's deltas from the next one, angles the short way)." },
        { "model-hierarchy", "red-agent model-hierarchy <model.3do>", "List a 3DO's hierarchy as a tree (name, parent, mesh, position, rotation, pivot) and what the engine would trip over: parents, cycles, meshes, the root and names." },
        { "model-hierarchy-edit", "red-agent model-hierarchy-edit <model.3do> <request.json|-> <new-output.3do>", "Rename, reparent, move, add and remove 3DO hierarchy nodes ({inputSha256, operations}); writes a fresh 3DO with the geometry unchanged, or refuses." },
        { "key-from-model", "red-agent key-from-model <model.3do> <new-output.key> [frames] [fps]", "Start a KEY for a model: one track per hierarchy node, named after it, at its rest pose." },
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

Json matCreateRequestSchema()
{
    // Match readBoundFile rather than the permissive package archive objects.
    // Its optional name is ignored at every JSON type; it is not a path alias.
    const Json file = object({
        { "path", described(text(1), "Nonempty filesystem path to the captured source file.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; hexadecimal case is ignored and equality is checked at runtime.") },
        { "name", described(Json::makeObject(), "Legacy ignored metadata; any JSON value remains accepted.") }
    }, { "path", "sha256" });
    const sed::assets::IndexedMatOptions defaults;
    Json format = enumeration({ "indexed8", "rgb565" });
    format.set("default", Json::makeString("indexed8"));
    Json mipmaps = numeric(1, 4, true);
    mipmaps.set("default", Json::makeNumber(defaults.mipmaps));
    Json dither = type("boolean");
    dither.set("default", Json::makeBool(defaults.dither));
    Json reserveIndexZero = type("boolean");
    reserveIndexZero.set("default", Json::makeBool(!defaults.allowed[0]));
    Json side = parseJson(R"({"type":"integer","enum":[8,16,32,64,128,256,512,1024]})");
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "cels", described(array(file, 1, 16), "Ordered captured PNG or baseline/extended-sequential JPEG images.") },
        { "format", std::move(format) },
        { "palette", described(file, "Captured CMP palette, required for indexed8 (including omitted format) and forbidden for rgb565.") },
        { "mipmaps", std::move(mipmaps) },
        { "dither", described(std::move(dither), "Indexed8 only; Floyd-Steinberg error diffusion.") },
        { "reserveIndexZero", described(std::move(reserveIndexZero), "Indexed8 only; reserving palette index zero does not create transparency.") },
        { "resize", described(tuple(std::move(side), 2), "Optional common output width and height; area-filter each cel before MAT conversion.") },
        { "paint", described([] {
            const Json rgb = described(tuple(numeric(0, 255, true), 3), "Red, green and blue, 0 to 255; default [255, 171, 0].");
            Json glowList = type("array");
            glowList.set("items", object({ { "center", tuple(numeric(), 2) }, { "radius", tuple(numeric(1e-6, 65536), 2) },
                { "rgb", rgb }, { "strength", numeric(0, 1) } }, { "center", "radius" }));
            glowList.set("maxItems", Json::makeNumber(256));
            Json fillList = type("array");
            fillList.set("items", object({ { "min", tuple(numeric(), 2) }, { "max", tuple(numeric(), 2) }, { "rgb", rgb } }, { "min", "max" }));
            fillList.set("maxItems", Json::makeNumber(256));
            return object({ { "glows", glowList }, { "fills", fillList },
                { "wrap", [] { Json w = type("boolean"); w.set("default", Json::makeBool(true));
                    return described(std::move(w), "Glows and fills continue across the cel's edges, as the texture repeats."); }() } }, {});
        }(), "Lamp glows and lens fills painted into every cel after resize, in texels of the final cel: each glow screens its "
             "colour over its ellipse weighted strength*(1-d^2)^2; each fill sets its texels. Make the 16-bit MAT and an 8-bit "
             "twin of one name from the same request.") }
    }, { "cels" });
    Json formatRule = condition(hasProperty("format", constant("rgb565")),
        absent({ "palette", "dither", "reserveIndexZero" }));
    formatRule.set("else", required({ "palette" }));
    allOf(result, std::move(formatRule));
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED MAT creation request"));
    result.set("description", Json::makeString(
        "Shared mat-create and sed_mat_create request for a new opaque indexed8 or RGB565 MAT. "
        "schemaVersion remains optional; omitted format selects indexed8. Root and captured-file unknown fields "
        "are refused. RGB565 refuses palette, dither and reserveIndexZero by presence, even false or null. "
        "Alpha is ignored in both formats. Output path and optional MCP operationId are outside this request."));
    result.set("x-runtimeConstraints", Json::makeString(
        "The CLI caps request JSON at 1 MiB, each captured cel at 64 MiB and CMP input at 1 MiB; "
        "the GUI has separate admission budgets. File readability, actual source hash equality, supported image "
        "decoding and compatible cel dimensions are runtime checks. Without resize, all cels must already share "
        "power-of-two dimensions from 8 through 1024. Actual mip count is clamped by the smallest 8-pixel side. "
        "The CLI currently reports a nonempty malformed digest as a runtime hash conflict, not a schema error. "
        "CLI paths retain ordinary filesystem semantics; MCP admits sources only inside its workspace or "
        "declared resource roots and restricts output to its workspace. The destination must be a fresh .mat; "
        "source revalidation and create-only publication remain runtime checks. A schema-valid request neither "
        "proves image validity nor qualifies target-engine or retail compatibility."));
    return result;
}

Json joJaInspectSchema()
{
    auto result = object({ { "path", text(1) } }, { "path" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED Raven RBSP inspection"));
    return result;
}
Json joJaRequestSchema()
{
    const auto file = boundFile(true);
    auto skips = array(text(1, 63), 0, 256);
    skips.set("uniqueItems", Json::makeBool(true));
    const auto mat = object({ { "shader", text(1, 63) }, { "name", pattern(R"(^[a-z0-9_-]{1,27}\.mat$)", 5, 31) }, { "source", file } },
        { "shader", "name", "source" });
    auto result = object({
        { "source", file }, { "schemaVersion", numeric(1, 1, true) }, { "target", enumeration({"jk", "mots"}) },
        { "levelName", pattern("^[a-z0-9_-]{1,24}$", 1, 24) }, { "scale", positive(1) }, { "patchSubdivisions", numeric(1, 16, true) },
        { "trianglesPerModel", numeric(1, 170, true) }, { "maxTriangles", numeric(1, 100000, true) },
        { "maxModels", numeric(1, 512, true) }, { "allowPlaceholders", type("boolean") },
        { "renderMeshCollision", type("boolean") }, { "materials", array(mat, 0, 256) }, { "palette", file },
        { "audioPolicy", enumeration({"stock-equivalents", "source-import"}) }, { "skipShaders", std::move(skips) }
    }, { "source" });
    auto& props = result.object.at("properties").object;
    props.at("target").set("default", Json::makeString("jk"));
    props.at("levelName").set("default", Json::makeString("joja_scene"));
    props.at("scale").set("default", Json::makeNumber(1.0 / 320));
    props.at("schemaVersion").set("default", Json::makeNumber(1));
    props.at("patchSubdivisions").set("default", Json::makeNumber(4));
    props.at("trianglesPerModel").set("default", Json::makeNumber(140));
    props.at("maxTriangles").set("default", Json::makeNumber(100000));
    props.at("maxModels").set("default", Json::makeNumber(512));
    props.at("allowPlaceholders").set("default", Json::makeBool(false));
    props.at("renderMeshCollision").set("default", Json::makeBool(false));
    props.at("audioPolicy").set("default", Json::makeString("stock-equivalents"));
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED JO/JA scene import request"));
    result.set("description", Json::makeString("World model 0 only. Every input binds its raw SHA-256. Indexed MAT inputs require a CMP. The stock-equivalents audio policy maps stock footstep cues. Source-import currently refuses with joJaSourceAudioUnavailable."));
    result.set("x-runtimeConstraints", Json::makeString("Total captured input 256 MiB. BSP 128 MiB, each MAT 16 MiB and all MATs 128 MiB, CMP 1 MiB. Apply also requires both reviewed digests and a fresh target archive path."));
    return result;
}

Json modelImportRequestSchema()
{
    // Captured files follow readBoundFile: the optional name is ignored at
    // every JSON type and other fields are refused.
    const Json file = object({
        { "path", described(text(1), "Nonempty filesystem path to the captured source file.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; hexadecimal case is ignored and equality is checked at runtime.") },
        { "name", described(Json::makeObject(), "Legacy ignored metadata; any JSON value remains accepted.") }
    }, { "path", "sha256" });
    const sed::assets::ObjImportOptions defaults;
    Json material = object({
        { "name", described(text(1, 255),
            "Exact, case-sensitive OBJ usemtl name or glTF material name; an unnamed glTF material is \"#<index>\". "
            "Printable characters only; each source material is mapped at most once.") },
        { "mat", described(pattern(R"(^[A-Za-z0-9_.-]+\.[mM][aA][tT]$)", 5, 31),
            "MAT file name the 3DO lists, such as crate.mat: at most 31 letters, digits, '_', '-' or '.', no folder.") },
        { "size", described(tuple(numeric(1, 4096, true), 2), "Texture width and height in texels.") },
        { "matFile", described(file, "Captured texture MAT whose first cel supplies the texel size; a colour MAT is refused.") }
    }, { "name", "mat" });
    allOf(material, exactlyOne("size", "matFile"));
    Json scale = positive(1000000);
    scale.set("default", Json::makeNumber(defaults.scale));
    Json upAxis = enumeration({ "y", "z" });
    upAxis.set("default", Json::makeString(defaults.upAxis == sed::assets::ObjUpAxis::Z ? "z" : "y"));
    Json origin = enumeration({ "base", "center", "none" });
    origin.set("default", Json::makeString(defaults.origin == sed::assets::ObjOrigin::Center ? "center"
        : defaults.origin == sed::assets::ObjOrigin::None ? "none" : "base"));
    Json normalPolicy = enumeration({ "preserve", "recalculate" });
    normalPolicy.set("default", Json::makeString(
        defaults.normalPolicy == sed::assets::ObjNormalPolicy::Recalculate ? "recalculate" : "preserve"));
    Json allowUntextured = type("boolean");
    allowUntextured.set("default", Json::makeBool(defaults.allowUntextured));
    Json split = type("boolean");
    split.set("default", Json::makeBool(defaults.split));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "source", described(file, "Captured Wavefront OBJ text or binary glTF 2.0 (.glb); the format is recognised from the bytes, not the file name.") },
        { "materials", described(array(std::move(material), 0, 256),
            "One mapping for every material the source uses on textured faces. The list may be empty for a source without materials when allowUntextured is set.") },
        { "scale", described(std::move(scale), "Uniform scale applied after the axis change.") },
        { "upAxis", described(std::move(upAxis), "Source up axis: y maps (x, y, z) to JK (x, -z, y); z keeps the axes.") },
        { "origin", described(std::move(origin), "base puts the bottom centre of the bounds at the model origin, center the centre; none keeps the transformed coordinates.") },
        { "meshName", described(pattern(R"(^[A-Za-z0-9_.-]+$)", 1, 24), "Optional prefix of every mesh and hierarchy node name.") },
        { "allowUntextured", described(std::move(allowUntextured), "Write faces without a material with no material and geometry mode 0; JK does not draw them.") },
        { "split", described(std::move(split), "Split a source mesh over a JK mesh limit into meshes that fit; false refuses it instead.") },
        { "normalPolicy", described(std::move(normalPolicy), "preserve keeps authored normals and hard edges; recalculate replaces them with smooth geometric normals.") },
        { "exportBaseColor", described(text(1), "Path of a new .jpg, .jpeg or .png file that receives, unchanged, the one base colour image the used glTF materials share. Binary glTF sources only.") }
    }, { "source", "materials" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED model import request"));
    result.set("description", Json::makeString(
        "Shared model-import and sed_model_import request that turns a game-ready OBJ or GLB mesh into a new "
        "JK 3DO 2.1. schemaVersion remains optional. Root, material and captured-file unknown fields are refused. "
        "Each material gives exactly one of size or matFile. Output path and optional MCP operationId are outside "
        "this request."));
    result.set("x-runtimeConstraints", Json::makeString(
        "The CLI caps request JSON at 1 MiB, an OBJ source at 16 MiB and 1,048,576 lines, a GLB source at 64 MiB "
        "and each matFile at 64 MiB. File readability, source hash equality and the source grammar are runtime "
        "checks. A material name mapped twice, one MAT name given two different sizes, a non-printable material "
        "name, an unmapped source material, faces without texture coordinates, vertex colours, curves, lines and "
        "non-finite numbers are refused when the source is read. JK limits apply to the result: 64 meshes; 512 "
        "faces, 512 vertices and 768 texture vertices per mesh; 24 corners per face. exportBaseColor is refused "
        "for an OBJ source, for a model whose used materials show no or several base colour images, for an "
        "extension that does not match the image type and for an existing file. Material-name length is counted "
        "in bytes. CLI paths retain ordinary filesystem semantics; MCP admits sources only inside its workspace or "
        "declared resource roots and restricts outputs to its workspace. The CLI currently reports a nonempty "
        "malformed digest as a runtime hash conflict, not a schema error. The destination must be a fresh .3do; "
        "create-only publication remains a runtime check. A schema-valid request does not prove the model imports "
        "or qualifies its appearance in any engine."));
    return result;
}

Json archiveCreateRequestSchema()
{
    // canonicalRuntimeGobEntryName: 1..127 printable ASCII bytes without
    // spaces or '"', ':', '*', '?', '<', '>', '|'; '/' and '\\' separate
    // components, none of which is empty, "." or "..".
    Json name = pattern(
        R"(^(?!.*["*:<>?|])(?!\.{1,2}(?:[/\\]|(?![\s\S])))[!-.0-\[\]-~]+(?:[/\\](?!\.{1,2}(?:[/\\]|(?![\s\S])))[!-.0-\[\]-~]+)*$)",
        1, 127);
    // The entry reader copies only path and sha256 into its captured-file
    // check, so other entry fields are ignored rather than refused.
    Json entry = object({
        { "name", described(std::move(name),
            "Destination inside the archive, such as mat/wall.mat or 3do\\crate.3do. Stored lowercase with backslashes; "
            "names that differ only by case or slash direction are duplicates.") },
        { "path", described(text(1), "Nonempty filesystem path to the captured source file.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; hexadecimal case is ignored and equality is checked at runtime.") }
    }, { "name", "path", "sha256" }, false);
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "entries", described(array(std::move(entry), 1, sed::assets::kRuntimeArchiveMaxEntries),
            "Loose files in the physical order the archive stores them.") }
    }, { "entries" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED archive creation request"));
    result.set("description", Json::makeString(
        "Shared archive-create and sed_archive_create request that packs captured loose files into a new GOB or "
        "GOO. schemaVersion remains optional. Unknown root fields are refused; fields of an entry other than "
        "name, path and sha256 are ignored. Output path and optional MCP operationId are outside this request."));
    result.set("x-runtimeConstraints", Json::makeString(
        "The CLI caps request JSON at 1 MiB, each source at 256 MiB and the archive, with its header and index, "
        "at 512 MiB (the most the agent reads back as a resource archive). File readability and source hash "
        "equality are runtime checks. Duplicate destinations after canonicalisation are refused. MAT, 3DO, COG "
        "and template entries are read back by type before publication, and an entry that does not read as its "
        "type refuses the archive. An entry at the archive root (other than episode.jk) or in an unusual top "
        "folder is reported as a warning, not refused. A source that is the output file is refused. CLI paths "
        "retain ordinary filesystem semantics; MCP admits sources only inside its workspace or declared resource "
        "roots and restricts output to its workspace. The CLI currently reports a nonempty malformed digest as "
        "a runtime hash conflict, not a schema error. The destination must be a fresh .gob or .goo; create-only "
        "publication remains a runtime check. A schema-valid request does not establish that the game finds or "
        "loads the packed resources."));
    return result;
}

namespace
{
// snapshotStaticThings: two exact, case-sensitive values.
Json thingModeSchema()
{
    Json result = enumeration({ "none", "static" });
    result.set("default", Json::makeString("none"));
    return described(std::move(result),
        "none draws world surfaces only. static adds placed 3DO models (LOD 0, rest pose) and classic sprites "
        "found in resourceArchives; a Thing whose resource is not found is reported in thingPreview, not drawn.");
}
Json playerStartsSchema()
{
    Json result = enumeration({ "omit", "draw" });
    result.set("default", Json::makeString("omit"));
    return described(std::move(result),
        "With thingMode static: omit leaves out Things whose effective type is player, which the game never draws from "
        "the player's view (reported as omitted-player-start); draw shows them, to see where spawns are. Refused "
        "without thingMode static.");
}
Json flag(const bool value, const char* description)
{
    Json result = type("boolean");
    result.set("default", Json::makeBool(value));
    return described(std::move(result), description);
}
Json lightingSchema(const char* description)
{
    Json result = enumeration({ "ambient", "baked" });
    result.set("default", Json::makeString("ambient"));
    return described(std::move(result), description);
}
// Baked lighting and fullbright contradict each other; both commands refuse
// the pair as a usage error.
Json bakedRefusesFullbright()
{
    Json off = type("boolean");
    off.set("const", Json::makeBool(false));
    return condition(hasProperty("lighting", constant("baked")), propertyRule({ { "fullbright", std::move(off) } }));
}
// The limits cmdSnapshot applies before it opens the level, which a
// rendered overview shares.
constexpr double kSnapshotMinimumSide = 64;
constexpr double kSnapshotMaximumSide = 2048;
constexpr char kSnapshotPixelBudget[] = "2097152";
} // namespace

Json snapshotRequestSchema()
{
    // cmdSnapshot compares projection after lower-casing it.
    const char* const orthographic = "^[oO][rR][tT][hH][oO][gG][rR][aA][pP][hH][iI][cC]$";
    Json projection = pattern(
        "^(?:[pP][eE][rR][sS][pP][eE][cC][tT][iI][vV][eE]|[oO][rR][tT][hH][oO][gG][rR][aA][pP][hH][iI][cC])$");
    projection.set("default", Json::makeString("perspective"));
    const sed::render::SnapshotCamera defaults;
    Json fieldOfView = type("number");
    fieldOfView.set("exclusiveMinimum", Json::makeNumber(0));
    fieldOfView.set("exclusiveMaximum", Json::makeNumber(180));
    fieldOfView.set("default", Json::makeNumber(defaults.fieldOfViewDegrees));
    const Json point = tuple(type("number"), 3);
    Json nearPlane = type("number");
    nearPlane.set("default", Json::makeNumber(defaults.nearPlane));
    // The camera reader requires position and target and ignores other fields.
    const Json camera = object({
        { "position", described(point, "Eye position [x, y, z] in level units.") },
        { "target", described(point, "Point the camera looks at; it must differ from position.") },
        { "nearPlane", described(std::move(nearPlane),
            "Near clip distance in level units; a value below 0.000001 is raised to it.") },
        { "farPlane", described(type("number"),
            "Accepted so that an overview's snapshotRequest replays unchanged; the software renderer does not clip at it.") }
    }, { "position", "target" }, false);
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "width", described(numeric(kSnapshotMinimumSide, kSnapshotMaximumSide, true), "Image width in pixels.") },
        { "height", described(numeric(kSnapshotMinimumSide, kSnapshotMaximumSide, true), "Image height in pixels.") },
        { "projection", described(std::move(projection),
            "perspective or orthographic; letter case is ignored. Without camera, perspective frames the level "
            "from three quarters and orthographic from overhead.") },
        { "fieldOfViewDegrees", described(std::move(fieldOfView),
            "Vertical field of view of a perspective image. With an automatically framed camera the default is the framed value.") },
        { "orthographicHeight", described(numeric(0.001, 1000000),
            "Height in level units that an orthographic image covers; required for orthographic and refused for perspective.") },
        { "camera", described(camera,
            "Explicit camera. Omit it to frame the level's drawn world surfaces automatically.") },
        { "fullbright", flag(false, "true draws textures at full brightness; false shades them as lighting says.") },
        { "lighting", lightingSchema(
            "ambient shades each sector by its ambient light. baked shades each corner by its stored light as the game "
            "combines it: max(sector extra light, min(1, corner light + face extra light)), interpolated across the face, "
            "light mode 0 at full light, JK reading one value per corner and MotS/IJIM RGB. A sector on a colormap other "
            "than the master is multiplied by that colormap's header tint, as the hardware renderer does; the tints are "
            "read from resourceArchives and reported in colormapTints. Things keep sector ambient light.") },
        { "cullBackFaces", flag(false, "true skips world faces whose normal points away from the camera.") },
        { "thingMode", thingModeSchema() },
        { "playerStarts", playerStartsSchema() },
        { "resourceArchives", archives() }
    }, { "width", "height" }, false);
    allOf(result, bakedRefusesFullbright());
    Json orthographicRules = absent({ "fieldOfViewDegrees" });
    orthographicRules.set("required", strings({ "orthographicHeight" }));
    Json lens = condition(hasProperty("projection", pattern(orthographic)), std::move(orthographicRules));
    lens.set("else", absent({ "orthographicHeight" }));
    allOf(result, std::move(lens));
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED snapshot request"));
    result.set("description", Json::makeString(
        "Shared snapshot and sed_snapshot request that renders a level to a new PNG with the built-in software "
        "renderer. schemaVersion remains optional. Unknown root and camera fields are ignored. Materials and the "
        "master colormap come from resourceArchives; a face whose material is not found is drawn flat grey and "
        "counted in materialsMissing. The level path, output path and optional MCP operationId are outside this "
        "request."));
    result.set("x-runtimeConstraints", Json::makeString(std::string(
        "The CLI caps request JSON at 1 MiB. width x height may not exceed ") + kSnapshotPixelBudget
        + " pixels, and the camera position and target must differ; both are refused as usage errors. Without "
        "camera, a level with no drawn world surfaces is refused. At most 16 archives share a 512 MiB and "
        "100,000-entry budget; file readability and hash equality are runtime checks, and the CLI currently "
        "reports a nonempty malformed digest as a runtime hash conflict, not a schema error. static thingMode is "
        "limited to 512 distinct model/sprite references and 256,000,000 raster samples. Textures are drawn with the game's handedness: as stored where "
        "U x V points away from the viewer and mirrored left to right where it points at the viewer. The image "
        "shows world surfaces (and static Things when asked) under sector ambient or baked light; it is not engine "
        "evidence for dynamic lights, the 8-bit light tables, animation or gameplay. CLI paths retain ordinary filesystem semantics; "
        "MCP admits archives only inside its workspace or declared resource roots and restricts the output to "
        "its workspace. The destination must be a fresh file; create-only publication remains a runtime check."));
    return result;
}

Json overviewRequestSchema()
{
    Json framing = enumeration({ sed::level::kOverviewWren.data(), sed::level::kOverviewContain.data() });
    framing.set("default", Json::makeString(std::string(sed::level::kOverviewWren)));
    Json margin = numeric(0, sed::level::kOverviewMaximumMargin);
    margin.set("default", Json::makeNumber(sed::level::kOverviewDefaultMargin));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "view", described(enumeration({ sed::level::kOverviewOverhead.data(), sed::level::kOverviewThreeQuarter.data() }),
            "overhead is an orthographic plan with north (+Y) up and east (+X) right; threequarter is a perspective view from the south-east, above.") },
        { "framing", described(std::move(framing),
            "wren places the camera at a fixed distance, three quarters of the bounding-box diagonal, and may crop near corners; contain backs off until every corner of the bounding box is inside the margin. Both give the same overhead plan.") },
        { "width", described(numeric(1, kInt32Maximum, true), "Image width in pixels the camera is fitted to.") },
        { "height", described(numeric(1, kInt32Maximum, true), "Image height in pixels the camera is fitted to.") },
        { "margin", described(std::move(margin), "Empty border on each side as a fraction of the image.") },
        { "fullbright", flag(false, "Copied into snapshotRequest and used when output is rendered.") },
        { "lighting", lightingSchema("Copied into snapshotRequest and used when output is rendered; see the snapshot request.") },
        { "cullBackFaces", flag(true, "Copied into snapshotRequest and used when output is rendered.") },
        { "thingMode", thingModeSchema() },
        { "playerStarts", playerStartsSchema() },
        { "resourceArchives", described(Json::makeObject(),
            "Captured archives for materials and static Things. Read, and held to the archive-list rules, only when output is given.") },
        { "output", described(type("string"),
            "Path of a new PNG. When present the framed camera is rendered as snapshot would render snapshotRequest; without it nothing is drawn or written.") }
    }, { "view", "width", "height" }, false);
    // A rendered overview allocates the image, so it takes snapshot's upper
    // limits; framing alone does not.
    Json side = type("integer");
    side.set("maximum", Json::makeNumber(kSnapshotMaximumSide));
    allOf(result, condition(required({ "output" }),
        propertyRule({ { "width", side }, { "height", side }, { "resourceArchives", archives() } })));
    allOf(result, bakedRefusesFullbright());
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED overview request"));
    result.set("description", Json::makeString(
        "Shared overview and sed_overview request that fits a camera to the level's drawn world surfaces and "
        "returns it with a snapshotRequest for exact replay. schemaVersion remains optional. Unknown root fields "
        "are ignored. The level path and optional MCP operationId are outside this request."));
    result.set("x-runtimeConstraints", Json::makeString(std::string(
        "The CLI caps request JSON at 1 MiB. A level with no drawn world surfaces is refused. With output, "
        "width and height are at most 2048 and width x height at most ") + kSnapshotPixelBudget
        + " pixels (a usage error otherwise), the archive-list rules of the snapshot request apply, and the "
        "destination must be a fresh file. An image narrower or lower than 64 pixels can be rendered here, but "
        "its snapshotRequest is then outside what snapshot accepts. Without output any positive size is framed "
        "and resourceArchives is copied, unread, into snapshotRequest when thingMode is static or lighting is baked. CLI paths retain "
        "ordinary filesystem semantics; MCP restricts output to its workspace and admits archives only inside "
        "its workspace or declared resource roots. The framing uses world surfaces only, not Thing bounds."));
    return result;
}

Json packageRequestSchema()
{
    // These are the legacy package parser's request shapes, not the newer
    // dependency inspector or the GUI's narrower admission policy. In
    // particular the primary key shadows fileName without reading it.
    const auto aliasRules = [](Json& result, const char* primary)
    {
        allOf(result, atLeastOne({ primary, "fileName" }));
        allOf(result, condition(absent({ primary }), propertyRule({ { "fileName", text() } })));
    };
    Json archive = object({
        { "path", described(text(), "Filesystem archive path; takes precedence over fileName in the CLI parser.") },
        { "fileName", described(Json::makeObject(), "Legacy filesystem-path alias, read as a string only when path is absent. MCP admits both supplied path fields independently.") },
        { "sha256", described(hash(), "Required SHA-256 of the raw captured archive file; hexadecimal case is ignored.") }
    }, { "sha256" }, false);
    aliasRules(archive, "path");
    Json extra = object({
        { "name", described(text(), "Logical resource name; takes precedence over fileName. This is not a filesystem path.") },
        { "fileName", described(Json::makeObject(), "Legacy logical-name alias, read as a string only when name is absent.") },
        { "folder", described(text(1), "Nonempty logical destination folder, such as mat or cog; not a loose filesystem search directory.") }
    }, { "folder" }, false);
    aliasRules(extra, "name");

    const sed::level::PackageOptions defaults;
    const auto sizeOption = [](std::size_t fallback, const char* description)
    {
        Json result = type("integer");
        result.set("minimum", Json::makeNumber(0));
        // packageOptSize checks the decoded number against 2^size_t_bits
        // before narrowing. A rounded SIZE_MAX double is not an inclusive bound.
        result.set("exclusiveMaximum", Json::makeNumber(std::ldexp(1.0, std::numeric_limits<std::size_t>::digits)));
        result.set("default", Json::makeNumber(static_cast<double>(fallback)));
        return described(std::move(result), description);
    };
    Json allow = type("boolean");
    allow.set("default", Json::makeBool(defaults.allowUnverifiedReferences));
    Json options = object({
        { "maxNodes", sizeOption(defaults.maxNodes, "Counts the generated episode, level and distinct resources, including stock omissions. Values below two yield a blocked plan.") },
        { "maxEdges", sizeOption(defaults.maxEdges, "Maximum examined reference occurrences; zero admits no resource edge.") },
        { "maxDepth", sizeOption(defaults.maxDepth, "Maximum resource depth; zero adds no depth restriction.") },
        { "maxPackageBytes", sizeOption(defaults.maxPackageBytes, "Maximum output archive bytes; zero cannot produce a buildable package.") },
        { "allowUnverifiedReferences", described(std::move(allow), "Downgrades supported missing-reference errors to warnings; does not waive invalid paths, collisions or work budgets and never certifies runtime closure.") }
    }, {}, false);
    Json episodeType = signedIndex();
    episodeType.set("default", Json::makeNumber(1));
    Json episode = object({
        { "episodeTitle", described(pattern(R"(^(?!.*["#])[ -~]+$)", 1, 80), "1 to 80 printable ASCII characters, excluding quote and #.") },
        { "levelFileName", described(pattern(R"(^[^/\\]*\.[jJ][kK][lL]$)"), "Plain JKL filename inside the package; defaults to the source file basename. Physical GOB entry admission is checked at runtime.") },
        { "episodeType", std::move(episodeType) }
    }, { "episodeTitle" }, false);
    Json extension = pattern(R"(^\.[gG](?:[oO][bB]|[oO][oO])$)");
    extension.set("default", Json::makeString(".gob"));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "inputSha256", described(hash(), "Optional raw JKL SHA-256. Supply it to bind the request to the reviewed source bytes; hexadecimal case is ignored.") },
        { "archives", described(array(std::move(archive)), "Ordered hash-bound resource archives; an omitted or empty array is valid. Searched before stockArchives.") },
        { "stockArchives", described(array(text()), "Ordered filesystem path strings, not hash-bound objects. Resolved stock entries are omitted from output while their dependencies are inspected.") },
        { "extraResources", array(std::move(extra)) },
        { "options", std::move(options) },
        { "episode", std::move(episode) },
        { "archiveExtension", std::move(extension) }
    }, { "episode" }, false);
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED package plan/apply request"));
    result.set("description", Json::makeString(
        "Shared JSON request for package-plan and package-apply over an explicit saved .jkl. Planning writes nothing. "
        "Apply additionally requires a new output path, expected output SHA-256 and expected manifest SHA-256 as CLI arguments "
        "or MCP wrapper fields. Legacy unknown fields and optional source hashes remain accepted."));
    result.set("x-runtimeConstraints", Json::makeString(
        "Requests are capped at 1 MiB and JKL source files at 128 MiB. Captured archives share 512 MiB of raw bytes "
        "and 100000 physical entries; materialized archive payloads share a further 512 MiB cap. These byte/entry "
        "budgets do not impose the GUI's archive-count limit. Options must be finite representable size_t integers "
        "after JSON number decoding; the exclusive schema bound reflects this build's size_t width. Source/archive "
        "hash equality, file readability, physical runtime entry names, resource resolution, destination collisions, "
        "source revalidation and graph/output budgets remain runtime checks. MCP requires nonempty path "
        "strings inside its workspace or declared resource roots, checks every supplied path/fileName alias and stock "
        "path independently (including shadowed aliases), and restricts output to its workspace. CLI paths retain "
        "ordinary filesystem semantics. Apply refuses existing outputs and mismatched reviewed output/manifest "
        "hashes. A schema-valid request can produce a blocked plan; inspect canBuild, issues, stockOmissions and "
        "unverifiedScopes. Manifest hashes identify normalized inputs/decisions, not raw archive file hashes. "
        "runtimeClosureVerified remains false; successful packaging does not prove JKL validity or gameplay."));
    return result;
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
        { "resolveExportDependencies", type("boolean") }, { "outputFormat", enumeration({ "jkl", "jed" }) },
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
    needsRootField({ "addCog", "rebindCogReference", "setCogValue", "makeDoor", "doorAssistant", "elevatorAssistant",
                       "switchAssistant" }, "resourceArchives", true);
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

Json cmpCreateRequestSchema()
{
    const Json file = object({
        { "path", described(text(1), "Nonempty filesystem path to the captured master CMP.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; hexadecimal case is ignored and equality is checked at runtime.") },
        { "name", described(Json::makeObject(), "Legacy ignored metadata; any JSON value remains accepted.") }
    }, { "path", "sha256" });
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "master", described(file, "The level's master colormap (World Colormap 0, usually dflt.cmp): its palette is kept and its light tables are tinted.") },
        { "tint", described(tuple(numeric(0, 4), 3),
            "Red, green and blue multipliers. Each lit master colour is multiplied by them and mapped to the nearest palette colour; "
            "the header keeps them for the hardware renderer. The stock red maps use [1, 0.2, 0.2].") }
    }, { "master", "tint" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED tinted colormap request"));
    result.set("description", Json::makeString(
        "Shared cmp-create and sed_cmp_create request for a new tinted CMP made from a master, for a sector's colormap. "
        "schemaVersion remains optional. Root and captured-file unknown fields are refused. The output path and "
        "optional MCP operationId are outside this request."));
    result.set("x-runtimeConstraints", Json::makeString(
        "The CLI caps request JSON at 1 MiB and the master at 1 MiB. File readability, actual hash equality and a "
        "complete master (header, palette and 64 light tables) are runtime checks; a master without light tables is "
        "an input error. The output carries the master's transparency table when the master has one. CLI paths retain ordinary "
        "filesystem semantics; MCP admits the master only inside its workspace or declared resource roots and restricts "
        "output to its workspace. The destination must be a fresh .cmp. A schema-valid request does not qualify how "
        "either renderer will look; compare in the game."));
    return result;
}

Json cmpMasterCreateRequestSchema()
{
    const Json file = object({
        { "path", described(text(1), "Nonempty filesystem path to the captured file.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; hexadecimal case is ignored and equality is checked at runtime.") },
        { "name", described(Json::makeObject(), "Legacy ignored metadata; any JSON value remains accepted.") }
    }, { "path", "sha256" });
    const Json rgb = described(tuple(numeric(0, 255, true), 3), "Red, green and blue, 0 to 255.");
    const Json fullbright = flag(false, "true keeps the colour at full brightness at every light level, like the stock emissive entries (8-bit lamps).");
    Json filesList = type("array");
    filesList.set("items", file);
    filesList.set("maxItems", Json::makeNumber(64));
    Json archiveList = type("array");
    archiveList.set("items", file);
    archiveList.set("maxItems", Json::makeNumber(16));
    Json colorList = type("array");
    colorList.set("items", object({ { "rgb", rgb }, { "fullbright", fullbright } }, { "rgb" }));
    colorList.set("maxItems", Json::makeNumber(255));
    Json rampList = type("array");
    rampList.set("items", object({ { "rgb", described(tuple(numeric(0, 255, true), 3), "The brightest colour of the ramp.") },
        { "steps", described(numeric(2, 63, true), "Colours from rgb/steps to rgb, evenly spaced in brightness.") },
        { "fullbright", fullbright } }, { "rgb", "steps" }));
    rampList.set("maxItems", Json::makeNumber(63));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "master", described(file, "The master colormap to start from (usually dflt.cmp). Its shared slots, header and every untouched table entry are kept.") },
        { "colors", described(colorList, "Colours to add, in slot order.") },
        { "ramps", described(rampList, "Brightness ramps to add after colors, each darkest first.") },
        { "slots", described(tuple(numeric(0, 255, true), 2),
            "First and last candidate slot, inclusive. Default [192, 254]: the slots the stock level masters give each level its own colours in, and dflt.cmp leaves magenta.") },
        { "protectMaterials", described(filesList, "Captured MATs whose palette indices must keep their colour: the level's own 8-bit MATs.") },
        { "protectArchives", described(archiveList, "Captured archives; every MAT inside is protected as above.") }
    }, { "master" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED master colormap request"));
    result.set("description", Json::makeString(
        "Shared cmp-master-create and sed_cmp_master_create request for a level master colormap with new colours. "
        "At least one colour or ramp is required. schemaVersion remains optional. Root and captured-file unknown fields are "
        "refused. The output path and optional MCP operationId are outside this request."));
    result.set("x-runtimeConstraints", Json::makeString(
        "The CLI caps request JSON at 1 MiB, the master at 1 MiB and each protected MAT at 16 MiB. File readability, hash "
        "equality, a complete master and readable protected MATs are runtime checks. A slot a protected MAT draws, or one the "
        "master keeps fullbright, is passed over and reported; fewer usable slots than colours is an input error. Stock "
        "static.jkl resources draw none of 192-254; textures borrowed from a stock level were made for that level's own "
        "master and should be protected if the level uses them. 8-bit MATs must be made against the new master to use its "
        "colours. The destination must be a fresh .cmp. Compare both renderers in the game."));
    return result;
}

Json cogRunRequestSchema()
{
    const Json file = object({
        { "path", described(text(1), "Nonempty filesystem path.") },
        { "sha256", described(hash(), "Required raw-file SHA-256; checked at runtime.") }
    }, { "path", "sha256" });
    Json bindingValue = Json::makeObject();
    {
        Json any = Json::makeArray();
        any.array.push_back(type("number"));
        any.array.push_back(type("string"));
        bindingValue.set("anyOf", std::move(any));
    }
    Json bindings = type("object");
    bindings.set("additionalProperties", described(std::move(bindingValue),
        "A thing, sector or surface index (surfaces numbered sector by sector), a number, \"(x/y/z)\" for a vector, or a resource name."));
    const Json cog = object({
        { "script", described(file, "The .cog script.") },
        { "placedCog", described(numeric(0, 1.0e6, true), "Take the bindings of this placed COG in 'level'.") },
        { "bindings", described(std::move(bindings), "Symbol values; these override placedCog's.") }
    }, { "script" });
    const Json ref = object({
        { "thing", numeric(0, 1.0e6, true) },
        { "sector", numeric(0, 1.0e6, true) },
        { "surface", numeric(0, 1.0e7, true) }
    });
    const Json damage = object({
        { "thing", numeric(0, 1.0e6, true) },
        { "amount", numeric() },
        { "type", described(numeric(0, 1.0e6, true), "Damage type flags; 1 (impact) when omitted.") }
    }, { "thing", "amount" });
    const Json event = object({
        { "at", described(numeric(0, 3600), "Seconds on the simulated clock.") },
        { "activate", described(ref, "The player activates a thing or surface: 'activated' to the scripts linked to it.") },
        { "enter", described(ref, "The player enters a sector or touches a surface: 'entered'.") },
        { "cross", described(ref, "The player crosses an adjoin surface: 'crossed'.") },
        { "damage", described(damage, "The player damages a thing: 'damaged', then 'killed' if its health runs out.") },
        { "message", described(text(1), "A message name sent to one script.") },
        { "cog", described(numeric(0, 1.0e6, true), "With 'message': the script, by its position in 'cogs'.") },
        { "params", described(array(type("number"), 0, 4), "With 'message': GetParam(0..3).") }
    }, { "at" });
    Json target = enumeration({ "jk", "mots" });
    target.set("default", Json::makeString("jk"));
    Json duration = numeric(0, 3600);
    duration.set("default", Json::makeNumber(10));
    Json step = numeric(0.001, 1);
    step.set("default", Json::makeNumber(0.01));
    Json budget = numeric(100, 1.0e8, true);
    budget.set("default", Json::makeNumber(200000));
    Json result = object({
        { "schemaVersion", parseJson(R"({"type":"integer","const":1})") },
        { "target", std::move(target) },
        { "level", described(file, "A .jkl whose things, path frames, sectors and surfaces make the world; optional.") },
        { "cogs", described(array(cog, 1, 256), "Scripts in the order they start; a cog symbol's value is a position here.") },
        { "events", array(event, 0, 10000) },
        { "duration", described(std::move(duration), "Seconds to run in all.") },
        { "step", described(std::move(step), "Clock step in seconds.") },
        { "instructionBudget", described(std::move(budget), "Instructions a script may run without sleeping before it is stopped as a runaway.") }
    }, { "cogs" });
    result.set("$schema", Json::makeString(std::string(kDialect)));
    result.set("title", Json::makeString("RED COG run request"));
    result.set("description", Json::makeString(
        "Shared cog-run and sed_cog_run request. Each script is compiled for the target first; one that the game wouldn't "
        "load is reported and not run. Each event needs exactly one of activate, enter, cross, damage or message."));
    result.set("x-runtimeConstraints", Json::makeString(
        "A stand-in, not the engine: no physics, collision, AI, weapons or multiplayer. Things move along their path frames "
        "at the speed a script asks and send 'arrived'. Unsimulated verbs take their arguments, leave 0 and are logged."));
    return result;
}

Json focusedSchema(const std::string& topic)
{
    if ( topic == "mat-create" )
    {
        return matCreateRequestSchema();
    }
    if ( topic == "cmp-create" )
    {
        return cmpCreateRequestSchema();
    }
    if ( topic == "cmp-master-create" )
    {
        return cmpMasterCreateRequestSchema();
    }
    if ( topic == "jo-ja-inspect" ) return joJaInspectSchema();
    if ( topic == "jo-ja-plan" || topic == "jo-ja-apply" ) return joJaRequestSchema();
    if ( topic == "model-import" )
    {
        return modelImportRequestSchema();
    }
    if ( topic == "archive-create" )
    {
        return archiveCreateRequestSchema();
    }
    if ( topic == "snapshot" )
    {
        return snapshotRequestSchema();
    }
    if ( topic == "overview" )
    {
        return overviewRequestSchema();
    }
    if ( topic == "package-plan" || topic == "package-apply" )
    {
        return packageRequestSchema();
    }
    if ( topic == "dependencies" )
    {
        return dependencyRequestSchema();
    }
    if ( topic == "cog-run" )
    {
        return cogRunRequestSchema();
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
