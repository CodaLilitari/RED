// Reconstructed harness around the retained focusedContracts() regression fixtures.
#include "../agent_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <string>
#include <stdexcept>

namespace
{
using sed_agent::Json;
int failures = 0;
void check(bool result, const std::string& message)
{
    if ( !result )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
const Json& property(const Json& value, const std::string& name)
{
    const auto* found = value.find(name);
    if ( !found ) throw std::runtime_error("missing property: " + name);
    return *found;
}
bool equal(const Json& a, const Json& b)
{
    return sed_agent::toJsonString(a) == sed_agent::toJsonString(b);
}
bool matches(const Json& schema, const Json& value)
{
    if ( schema.type == Json::Type::Bool ) return schema.boolean;
    if ( const auto* t = schema.find("type") )
    {
        const auto typeMatches = [&](const std::string& name)
        {
            if ( name == "object" ) return value.type == Json::Type::Object;
            if ( name == "array" ) return value.type == Json::Type::Array;
            if ( name == "string" ) return value.type == Json::Type::String;
            if ( name == "boolean" ) return value.type == Json::Type::Bool;
            if ( name == "null" ) return value.type == Json::Type::Null;
            if ( name == "number" ) return value.type == Json::Type::Number && std::isfinite(value.number);
            if ( name == "integer" ) return value.type == Json::Type::Number && std::isfinite(value.number) && std::floor(value.number) == value.number;
            throw std::runtime_error("unsupported schema type: " + name);
        };
        bool valid = false;
        if ( t->type == Json::Type::Array )
            for ( const auto& name : t->array ) valid = valid || typeMatches(name.str);
        else valid = typeMatches(t->str);
        if ( !valid ) return false;
    }
    if ( const auto* c = schema.find("const"); c && !equal(*c, value) ) return false;
    if ( const auto* e = schema.find("enum") )
    {
        bool valid = false;
        for ( const auto& option : e->array ) valid = valid || equal(option, value);
        if ( !valid ) return false;
    }
    for ( const auto& keyword : { "allOf", "anyOf", "oneOf" } )
    {
        if ( const auto* alternatives = schema.find(keyword) )
        {
            std::size_t count = 0;
            for ( const auto& alternative : alternatives->array ) count += matches(alternative, value) ? 1 : 0;
            if ( std::string(keyword) == "allOf" && count != alternatives->array.size() ) return false;
            if ( std::string(keyword) == "anyOf" && count == 0 ) return false;
            if ( std::string(keyword) == "oneOf" && count != 1 ) return false;
        }
    }
    if ( const auto* n = schema.find("not"); n && matches(*n, value) ) return false;
    if ( const auto* test = schema.find("if") )
    {
        const auto* branch = schema.find(matches(*test, value) ? "then" : "else");
        if ( branch && !matches(*branch, value) ) return false;
    }
    if ( value.type == Json::Type::Number )
    {
        if ( const auto* n = schema.find("minimum"); n && value.number < n->number ) return false;
        if ( const auto* n = schema.find("maximum"); n && value.number > n->number ) return false;
        if ( const auto* n = schema.find("exclusiveMinimum"); n && value.number <= n->number ) return false;
        if ( const auto* n = schema.find("exclusiveMaximum"); n && value.number >= n->number ) return false;
    }
    if ( value.type == Json::Type::String )
    {
        if ( const auto* n = schema.find("minLength"); n && value.str.size() < n->number ) return false;
        if ( const auto* n = schema.find("maxLength"); n && value.str.size() > n->number ) return false;
        if ( const auto* p = schema.find("pattern"); p && !std::regex_search(value.str, std::regex(p->str)) ) return false;
    }
    if ( value.type == Json::Type::Object )
    {
        if ( const auto* n = schema.find("minProperties"); n && value.object.size() < n->number ) return false;
        if ( const auto* n = schema.find("maxProperties"); n && value.object.size() > n->number ) return false;
        if ( const auto* names = schema.find("propertyNames") )
            for ( const auto& [name, unused] : value.object )
            {
                (void)unused;
                if ( !matches(*names, Json::makeString(name)) ) return false;
            }
        if ( const auto* r = schema.find("required") )
            for ( const auto& name : r->array ) if ( !value.find(name.str) ) return false;
        const auto* properties = schema.find("properties");
        if ( properties )
            for ( const auto& [name, rule] : properties->object )
                if ( const auto* item = value.find(name); item && !matches(rule, *item) ) return false;
        if ( const auto* a = schema.find("additionalProperties") )
            for ( const auto& [name, unused] : value.object )
            {
                (void)unused;
                if ( (!properties || !properties->find(name)) && !matches(*a, unused) ) return false;
            }
    }
    if ( value.type == Json::Type::Array )
    {
        if ( const auto* n = schema.find("minItems"); n && value.array.size() < n->number ) return false;
        if ( const auto* n = schema.find("maxItems"); n && value.array.size() > n->number ) return false;
        if ( const auto* u = schema.find("uniqueItems"); u && u->boolean )
            for ( std::size_t i = 0; i < value.array.size(); ++i )
                for ( std::size_t j = 0; j < i; ++j ) if ( equal(value.array[i], value.array[j]) ) return false;
        const auto* prefix = schema.find("prefixItems");
        const std::size_t offset = prefix ? prefix->array.size() : 0;
        if ( prefix )
            for ( std::size_t i = 0; i < std::min(offset, value.array.size()); ++i )
                if ( !matches(prefix->array[i], value.array[i]) ) return false;
        if ( const auto* items = schema.find("items") )
            for ( std::size_t i = offset; i < value.array.size(); ++i ) if ( !matches(*items, value.array[i]) ) return false;
        if ( const auto* contains = schema.find("contains") )
        {
            std::size_t count = 0;
            for ( const auto& item : value.array ) count += matches(*contains, item) ? 1 : 0;
            const auto* min = schema.find("minContains");
            const auto* max = schema.find("maxContains");
            if ( count < (min ? min->number : 1) || (max && count > max->number) ) return false;
        }
    }
    return true;
}
void fixture(const Json& schema, const std::string& source, bool expected, const std::string& message)
{
    check(matches(schema, sed_agent::parseJson(source)) == expected, message);
}
void focusedContracts()
{
    const auto patch = sed_agent::runCommand("schema", { "patch" });
    const auto* dialect = patch.find("$schema");
    check(dialect && dialect->str == "https://json-schema.org/draft/2020-12/schema",
        "schema patch returns a Draft 2020-12 schema, not the legacy inventory");
    if ( !dialect )
    {
        return;
    }

    const auto inventory = sed_agent::runCommand("schema", {});
    std::set<std::string> operationNames;
    for ( const auto& entry : property(inventory, "operations").array )
    {
        const auto name = entry.str.substr(0, entry.str.find('{'));
        check(operationNames.insert(name).second, "legacy inventory has no duplicate operation: " + name);
        const auto schema = sed_agent::runCommand("schema", { name });
        check(property(schema, "$schema").str == dialect->str, name + " has a standalone dialect declaration");
        check(property(property(property(schema, "properties"), "op"), "const").str == name,
            name + " has an exact operation discriminator");
        check(schema.find("additionalProperties") != nullptr, name + " explicitly documents unknown-field admission");
    }
    check(operationNames.size() == 63, "all 57 existing and six restored patch operations are discoverable");
    const auto& operations = property(property(patch, "properties"), "operations");
    check(property(operations, "minItems").number == 1 && property(operations, "maxItems").number == 1000,
        "patch operation cardinality matches dispatcher admission");
    check(property(property(operations, "items"), "oneOf").array.size() == operationNames.size(),
        "patch alternatives and legacy descriptors come from the same registry");

    fixture(patch, R"({"operations":[{"op":"setGravity","value":9}]})", true,
        "legacy patch hashes and schemaVersion remain optional");
    fixture(patch, R"({"schemaVersion":2,"operations":[{"op":"setGravity","value":9}]})", false,
        "unsupported patch version is rejected");
    fixture(patch, R"({"operations":[]})", false, "empty patches are rejected");
    fixture(patch, R"({"operations":[{"op":"notAnOperation"}]})", false, "unknown operation is rejected");
    fixture(patch, R"({"operations":[{"op":"setGravity","value":9,"legacyExtension":true}],"legacyRoot":true})", true,
        "schemas preserve existing permissive root and legacy-operation admission");
    fixture(patch, R"({"operations":[{"op":"importTemplates","templates":["actor"]}]})", false,
        "template import requires source evidence on the operation or patch");
    fixture(patch, R"({"operations":[{"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"}]})", false,
        "ARCH operations require patch-root captured resources");
    fixture(patch, R"({"resolveExportDependencies":true,"operations":[{"op":"setGravity","value":9}]})", false,
        "explicit export resolution requires root archives");

    const auto box = sed_agent::runCommand("schema", { "addBox" });
    fixture(box, R"({"op":"addBox","center":[0,0,0],"halfSize":1})", true, "scalar box size is accepted");
    fixture(box, R"({"op":"addBox","center":[0,0,0],"halfSize":[1,2,3]})", true, "vector box size is accepted");
    fixture(box, R"({"op":"addBox","min":[0,0,0],"max":[1,2,3]})", true, "exact box corners are accepted");
    fixture(box, R"({"op":"addBox","center":[0,0,0],"halfSize":0})", false, "zero box size is rejected");
    fixture(box, R"({"op":"addBox","center":[0,0,0],"halfSize":1,"min":[0,0,0],"max":[1,1,1]})", false,
        "box coordinate spellings are mutually exclusive");
    fixture(box, R"({"op":"addBox","center":[0,0,0],"halfSize":1,"halffSize":2})", false,
        "closed basic operations reject unknown controls");

    const auto light = sed_agent::runCommand("schema", { "setLight" });
    fixture(light, R"({"op":"setLight","light":0})", false, "setLight requires an actual field to edit");
    fixture(light, R"({"op":"setLight","light":0,"rgbIntensity":null,"legacyRgbRange":-3})", true,
        "nullable independent RGB values retain their contract");
    fixture(light, R"({"op":"setLight","light":0,"flags":4294967296})", false,
        "light flags cannot overflow uint32");
    const auto scale = sed_agent::runCommand("schema", { "scaleSectors" });
    fixture(scale, R"({"op":"scaleSectors","sectors":[0],"pivot":[0,0,0],"factor":1,"factors":[1,1,1]})", false,
        "uniform and per-axis scale are mutually exclusive");
    fixture(scale, R"({"op":"scaleSectors","sectors":[0,0],"pivot":[0,0,0],"factors":[1,2,3]})", true,
        "duplicate transform selectors remain accepted and deduplicated");
    const auto uv = sed_agent::runCommand("schema", { "setSurfaceUv" });
    fixture(uv, R"({"op":"setSurfaceUv","sector":0,"surface":0,"uv":[[0,0],[1,0],[1,"bad"]]})", false,
        "nested UV components are numeric");
    const auto material = sed_agent::runCommand("schema", { "registerMaterial" });
    fixture(material, R"({"op":"registerMaterial","name":"wall.MaT"})", true,
        "material suffix is case-insensitive");
    fixture(material, R"({"op":"registerMaterial","name":"../wall.mat"})", false,
        "material names refuse traversal");
    fixture(material, R"({"op":"registerMaterial","name":"wall texture.mat"})", false,
        "material names refuse whitespace");
    fixture(material, R"({"op":"registerMaterial","name":"wall.mat\n"})", false,
        "material names refuse a trailing newline across validator regex dialects");
    const auto thingName = sed_agent::runCommand("schema", { "setThingName" });
    fixture(thingName, R"({"op":"setThingName","thing":0,"name":"actor\n"})", false,
        "Thing tokens refuse a trailing newline");
    const auto frames = sed_agent::runCommand("schema", { "setThingFrames" });
    fixture(frames, R"({"op":"setThingFrames","thing":0,"frames":[{"position":[0,0,0],"orientation":[0,90,0]}]})", true,
        "frames expose position and orientation objects");
    fixture(frames, R"({"op":"setThingFrames","thing":0,"frames":[{"position":[0,0,0]}]})", false,
        "nested frame orientation is required");
    const auto path = sed_agent::runCommand("schema", { "buildThingPath" });
    fixture(path, R"({"op":"buildThingPath","thing":0,"frames":[{"position":[0,0,0],"orientation":[0,0,0]}]})", true,
        "path recipe retains the legacy frames fallback");
    fixture(path, R"({"op":"buildThingPath","thing":0,"waypoints":[{"position":[0,0,0],"orientation":[0,0,0]}],"frames":"ignored"})", true,
        "path waypoints retain priority over an ignored frames alias");
    const auto array = sed_agent::runCommand("schema", { "placeLightArray" });
    fixture(array, R"({"op":"placeLightArray","layout":{"count":1},"rgbIntensity":0})", true,
        "light arrays expose independent RGB zero");
    fixture(array, R"({"op":"placeLightArray","layout":{"count":1},"rgbIntensity":null})", true,
        "light arrays expose inherited RGB as null");
    fixture(array, R"({"op":"placeLightArray","layout":{"count":1},"rgbIntensity":-1})", false,
        "light array RGB uses the shared nonnegative authoring range");
    fixture(array, R"({"op":"placeLightArray","layout":{"kind":"grid","countU":2,"countV":"two"}})", false,
        "nested placement dimensions are typed");
    fixture(array, R"({"op":"placeLightArray","layout":{"kind":"radial","count":4,"axis":[0,0,1],"radius":1}})", true,
        "layout defaults remain optional");
    fixture(array, R"({"op":"placeLightArray","layout":{"kind":"linear","count":1,"radius":-2}})", true,
        "inactive layout fields retain legacy numeric admission without active radial bounds");
    fixture(array, R"({"op":"placeLightArray","layout":{"kind":"radial","count":4,"axis":[0,0,0]}})", false,
        "active radial axes must be nonzero");
    const auto masked = sed_agent::runCommand("schema", { "applyMaskedSurfaceRecipe" });
    fixture(masked, R"({"op":"applyMaskedSurfaceRecipe","surfaces":[[0,0]],"faceFlags":{"all":"1"}})", false,
        "nested mask fields are uint32 values");
    const auto cog = sed_agent::runCommand("schema", { "setCogValue" });
    fixture(cog, R"({"op":"setCogValue","cog":0,"symbol":"speed","value":2,"default":true})", false,
        "explicit COG values and defaults are mutually exclusive");
    fixture(cog, R"({"op":"setCogValue","cog":0,"symbol":"speed","default":false})", false,
        "COG default must be true when given");
    const auto addCog = sed_agent::runCommand("schema", { "addCog" });
    fixture(addCog, R"({"op":"addCog","script":"switch.cog","bindings":[{"symbolName":"target","kind":"SURFACE","sector":0,"surface":2}]})", true,
        "COG surface pair and case-insensitive kind are typed");
    fixture(addCog, R"({"op":"addCog","script":"switch.cog","bindings":[{"symbol":"target","kind":"surface","sector":0,"surface":2,"targetIndex":3}]})", false,
        "COG surface pair and numbered target are mutually exclusive");
    fixture(addCog, R"({"op":"addCog","script":"switch.cog","bindings":[{"symbol":"count","kind":"int","value":1.5}]})", false,
        "explicit numeric COG integers cannot be fractional");
    fixture(addCog, R"({"op":"addCog","script":"switch.cog","bindings":[{"symbol":"count","kind":"int","value":true}]})", false,
        "nested COG values cannot be booleans");
    const auto bakeWorld = sed_agent::runCommand("schema", { "bakeLighting" });
    fixture(bakeWorld, R"({"op":"bakeLighting","skyIntensity":0,"skyColor":"unused"})", true,
        "disabled sky preserves ignored-field legacy behavior");
    fixture(bakeWorld, R"({"op":"bakeLighting","skyIntensity":1,"skyColor":"invalid"})", false,
        "enabled sky requires typed color components");
    fixture(bakeWorld, R"({"op":"bakeLighting","bounceStrength":0,"bounceRange":-1})", true,
        "disabled bounce accepts an inactive numeric range");
    fixture(bakeWorld, R"({"op":"bakeLighting","bounceStrength":1,"bounceRange":-1})", false,
        "enabled bounce requires positive range");
    const auto stairs = sed_agent::runCommand("schema", { "buildSwitchbackStairs" });
    fixture(stairs, R"({"op":"buildSwitchbackStairs","origin":[0,0,0],"direction":"+y","width":1,"run":1,"rise":0.2,"headroom":2,"landingRun":1,"stepsPerFlight":3,"steps":6})", false,
        "switchback schema refuses the unused steps spelling");

    const auto vertex = sed_agent::runCommand("schema", { "setVertexPosition" });
    fixture(vertex, R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[1,2,3]})", true,
        "restored vertex edit is discoverable");
    fixture(vertex, R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[1000001,2,3]})", false,
        "restored vertex edit publishes the coordinate limit");
    const auto fit = sed_agent::runCommand("schema", { "fitSurfaceTexture" });
    fixture(fit, R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":32,"height":64})", true,
        "restored Fit requires explicit texture dimensions");
    fixture(fit, R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":32.5,"height":64})", false,
        "Fit dimensions are bounded integers");
    const auto align = sed_agent::runCommand("schema", { "alignSurfaceTexture" });
    fixture(align, R"({"op":"alignSurfaceTexture","sector":0,"surface":0,"otherSector":1,"otherSurface":2})", true,
        "restored Align names target and reference");
    const auto bind = sed_agent::runCommand("schema", { "bindThingArchitecture" });
    fixture(bind, R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})", true,
        "ARCH bind names the source lighting record");
    const auto reset = sed_agent::runCommand("schema", { "rebindThingArchitecture" });
    fixture(reset, R"({"op":"rebindThingArchitecture","thing":0})", false, "ARCH reset policy is explicit");
    fixture(reset, R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})", true,
        "ARCH reset policy is discoverable");
    const auto bake = sed_agent::runCommand("schema", { "bakeThingArchitecture" });
    fixture(bake, R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-world-occluded-v1"})", true,
        "ARCH occlusion profile is discoverable");
    fixture(bake, R"({"op":"bakeThingArchitecture","thing":0,"profile":"retail"})", false,
        "unknown ARCH profiles are rejected");

    const auto commands = sed_agent::runCommand("schema", { "commands" });
    check(property(commands, "$schema").str == dialect->str, "command discovery is a schema document");
    std::set<std::string> commandNames;
    for ( const auto& command : property(commands, "enum").array )
    {
        commandNames.insert(command.str);
    }
    for ( const auto& command : property(inventory, "commands").array )
    {
        check(commandNames.contains(command.str), "focused catalog contains command " + command.str);
    }
    check(commandNames.size() == property(inventory, "commands").array.size(),
        "command names derive from one inventory");
    for ( const auto& command : property(commands, "x-commands").array )
    {
        const auto& name = property(command, "name").str;
        check(property(command, "usage").str.starts_with("red-agent " + name),
            "command catalog supplies exact positional usage for " + name);
        check(!property(command, "description").str.empty(), "command catalog describes " + name);
    }
}
// Additional recovered-source regression; kept outside the 55 retained fixtures.
void recoveredSourceContracts()
{
    const auto schema = sed_agent::runCommand("schema", { "alignSurfaceTexture" });
    if ( !schema.find("$schema") ) return;
    fixture(schema, R"({"op":"alignSurfaceTexture","sector":0,"surface":0,"referenceSector":1,"referenceSurface":2})", false,
        "Align refuses prose-only reference aliases; actual fields are otherSector/otherSurface");
    const auto dome = sed_agent::runCommand("schema", { "buildDome" });
    fixture(dome, R"({"op":"buildDome","center":[0,0,0],"radius":1,"headroom":2,"segments":3,"rings":1})", true,
        "dome accepts the smallest supported segment and ring counts");
    fixture(dome, R"({"op":"buildDome","center":[0,0,0],"radius":1,"headroom":2,"segments":65,"rings":1})", false,
        "dome publishes the core 64-segment ceiling");
    const auto terrain = sed_agent::runCommand("schema", { "buildHeightmapTerrain" });
    fixture(terrain, R"({"op":"buildHeightmapTerrain","origin":[0,0,0],"cellSize":1,"headroom":2,"columns":1,"rows":1,"heights":[0]})", true,
        "terrain types its flat cell heights");
    fixture(terrain, R"({"op":"buildHeightmapTerrain","origin":[0,0,0],"cellSize":1,"headroom":2,"columns":1,"rows":1,"heights":[1001]})", false,
        "terrain bounds individual heights to 1000");
    const auto stairs = sed_agent::runCommand("schema", { "buildSwitchbackStairs" });
    fixture(stairs, R"({"op":"buildSwitchbackStairs","origin":[0,0,0],"direction":"+x","width":1,"run":1,"rise":0.2,"headroom":2,"landingRun":1,"stepsPerFlight":129})", false,
        "switchback counts at most 128 steps per flight");
    const auto layout = sed_agent::runCommand("schema", { "placeLightArray" });
    fixture(layout, R"({"op":"placeLightArray","layout":{"kind":"grid","countU":257,"countV":1}})", false,
        "grid dimensions cannot individually exceed the output ceiling");
    fixture(layout, R"({"op":"placeLightArray","layout":{"kind":"linear","count":1,"countU":-1,"axis":[0,0,0],"radius":-1}})", true,
        "inactive linear layout fields keep their typed legacy admission");
    const auto frames = sed_agent::runCommand("schema", { "setThingFrames" });
    fixture(frames, R"({"op":"setThingFrames","thing":0,"frames":[{"position":[0,0,0],"orientation":[0,"bad",0]}]})", false,
        "frame orientation components remain typed");
    const auto fit = sed_agent::runCommand("schema", { "fitSurfaceTexture" });
    fixture(fit, R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":16384,"height":1})", true,
        "Fit includes its maximum integer dimension");
    fixture(fit, R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":16385,"height":1})", false,
        "Fit rejects a dimension above its explicit maximum");
    const auto patch = sed_agent::runCommand("schema", { "patch" });
    fixture(patch, R"({"resourceArchives":[{"path":"synthetic.gob","sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}],"operations":[{"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"}]})", true,
        "captured root evidence satisfies ARCH source inheritance");
    fixture(patch, R"({"resourceArchives":[],"operations":[{"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"}]})", false,
        "ARCH cannot inherit an empty archive list");
    fixture(patch, R"({"resourceArchives":[{"path":"synthetic.gob","sha256":"bad"}],"operations":[{"op":"setGravity","value":1}]})", false,
        "supplied archive evidence documents the supported hash shape");
    const auto bakeWorld = sed_agent::runCommand("schema", { "bakeLighting" });
    fixture(bakeWorld, R"({"op":"bakeLighting","skyIntensity":1,"skyColor":[2,-1,0]})", true,
        "enabled sky retains finite stored-float channels beyond placement RGB bounds");
    const auto commands = sed_agent::runCommand("schema", { "commands" });
    fixture(commands, R"("schema")", true, "commands is a string enum");
    fixture(commands, R"("invented-command")", false, "unknown command names are rejected");
    fixture(commands, R"({"command":"schema"})", false, "command discovery does not claim request body schemas");
}
// Independent-review regressions, derived from the current adapter and core validators.
void reviewedContracts()
{
    const auto addCog = sed_agent::runCommand("schema", { "addCog" });
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"actor","kind":"thing","value":0}]})", false,
        "reference COG kinds cannot select a primitive value");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"SURFACE","default":true}]})", false,
        "case-insensitive reference COG kinds cannot select a default");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"cog","default":true}]})", false,
        "placed COG references must select an explicit target");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"count","kind":"int","targetIndex":0}]})", false,
        "primitive COG kinds cannot select a reference target");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"actor","kind":"THING","targetIndex":-1}]})", true,
        "full reference kind names remain case insensitive");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"count","kind":"integer","value":2}]})", true,
        "primitive integer aliases retain explicit typed values");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"speed","kind":"FLEX","default":true}]})", true,
        "primitive COG kinds retain explicit defaults");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"actor","kind":"thg","targetIndex":-1}]})", false,
        "addCog does not advertise the unsupported thg reference alias");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"room","kind":"SEC","targetIndex":-1}]})", false,
        "addCog does not advertise the unsupported sec reference alias");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"srf","targetIndex":-1}]})", false,
        "addCog does not advertise the unsupported srf reference alias");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"room","kind":"sector","targetIndex":-1}]})", true,
        "addCog full sector reference remains supported");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"surface","sector":0,"surface":2,"type":"legacy-extension"}]})", true,
        "addCog ignores its undeclared type extension even for surface pairs");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"SURFACE","sector":0,"surface":2,"type":{"legacy":true}}]})", true,
        "ignored addCog type extensions retain arbitrary JSON admission");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"thing","sector":0,"surface":2}]})", false,
        "an active non-surface add binding cannot name a surface pair");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"surface","sector":0}]})", false,
        "surface pair bindings require both local indices");
    fixture(addCog, R"({"op":"addCog","script":"example.cog","bindings":[{"symbol":"target","kind":"surface","sector":0,"surface":2,"targetIndex":1}]})", false,
        "surface pair and numbered target remain mutually exclusive");

    const auto rebind = sed_agent::runCommand("schema", { "rebindCogReference" });
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","kind":"surface","type":"thing","sector":0,"surface":2})", true,
        "active rebind kind takes precedence over a different type alias");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","kind":"SURFACE","type":{"ignored":true},"sector":0,"surface":2})", true,
        "active rebind kind ignores the type alias shape");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"actor","kind":"thing","type":"legacy-extension","targetIndex":-1})", true,
        "ignored rebind type aliases do not constrain numbered targets");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","type":"SURFACE","sector":0,"surface":2})", true,
        "rebind type remains a case-insensitive active fallback");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","kind":"thing","type":"surface","sector":0,"surface":2})", false,
        "an ignored surface alias cannot override active non-surface kind");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","type":"srf","sector":0,"surface":2})", false,
        "surface pair syntax requires the full active surface spelling");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"actor","type":"THG","targetIndex":-1})", true,
        "rebind retains reference aliases for numbered targets");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"room","kind":"sec","targetIndex":-1})", true,
        "rebind retains the numbered sec alias");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","kind":"srf","targetIndex":-1})", true,
        "rebind retains the numbered srf alias");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","kind":"invalid","type":"surface","targetIndex":0})", false,
        "rebind does not fall back when an invalid active kind exists");
    fixture(rebind, R"({"op":"rebindCogReference","cog":0,"parameter":0,"symbol":"target","type":true,"targetIndex":0})", false,
        "active type fallback must be a supported string kind");

    fixture(addCog, R"({"op":"addCog","script":"a.CoG","bindings":[]})", true,
        "authored COG names allow the shortest mixed-case suffix form");
    fixture(addCog, R"({"op":"addCog","script":"aaaaaaaaaaaaaaaaaaaaaaaaaaa.cog","bindings":[]})", true,
        "authored COG names include the exact31-character maximum");
    fixture(addCog, R"({"op":"addCog","script":".cog","bindings":[]})", false,
        "authored COG names require a nonempty basename");
    fixture(addCog, R"({"op":"addCog","script":"aaaaaaaaaaaaaaaaaaaaaaaaaaaa.cog","bindings":[]})", false,
        "authored COG names reject32 characters");
    fixture(addCog, R"({"op":"addCog","script":"bad.txt","bindings":[]})", false,
        "authored COG names require the actual cog suffix");
    fixture(addCog, R"({"op":"addCog","script":"path/name.cog","bindings":[]})", false,
        "authored COG names refuse forward-slash paths");
    fixture(addCog, R"({"op":"addCog","script":"path\\name.cog","bindings":[]})", false,
        "authored COG names refuse backslash paths");
    fixture(addCog, R"({"op":"addCog","script":"bad name.cog","bindings":[]})", false,
        "authored COG names refuse whitespace");
    fixture(addCog, R"({"op":"addCog","script":"ok.cog\n","bindings":[]})", false,
        "authored COG names refuse a trailing newline");
    fixture(addCog, R"({"op":"addCog","script":"caf\u00e9.cog","bindings":[]})", false,
        "authored COG names remain printable ASCII");
    fixture(addCog, R"({"op":"addCog","script":"#..,='x.cog","bindings":[]})", true,
        "authored COG grammar retains punctuation allowed by its actual validator");
    fixture(addCog, R"({"op":"addCog","script":"bad:name.cog","bindings":[]})", false,
        "authored COG names refuse colon controls");
    fixture(addCog, R"({"op":"addCog","script":"bad*name.cog","bindings":[]})", false,
        "authored COG names refuse wildcard controls");
    fixture(addCog, R"({"op":"addCog","script":"bad\"name.cog","bindings":[]})", false,
        "authored COG names refuse double quotes");

    const auto thingValue = sed_agent::runCommand("schema", { "setThingValue" });
    fixture(thingValue, R"json({"op":"setThingValue","thing":0,"key":"frame","value":"(0/0/0)"})json", false,
        "ordered frame records require the dedicated operation");
    fixture(thingValue, R"json({"op":"setThingValue","thing":0,"key":"FrAmE","value":"(0/0/0)"})json", false,
        "reserved frame parameter spelling is case insensitive");
    fixture(thingValue, R"({"op":"setThingValue","thing":0,"key":"NUMFRAMES","value":"2"})", false,
        "frame allocation controls require the dedicated operation");
    fixture(thingValue, R"({"op":"setThingValue","thing":0,"key":"numframes","value":"2"})", false,
        "lowercase frame allocation controls remain reserved");
    fixture(thingValue, R"({"op":"setThingValue","thing":0,"key":"move","value":"path"})", true,
        "ordinary Thing parameter keys retain their lexical admission");
    fixture(thingValue, R"({"op":"setThingValue","thing":0,"key":"frame_2","value":"custom"})", true,
        "reserved-key exclusion does not ban ordinary lexical extensions");
    fixture(thingValue, R"({"op":"setThingValue","thing":0,"key":"custom_key","value":"custom"})", true,
        "unrecognized but lexically valid Thing keys remain supported");
}
void invalidTopics()
{
    for ( const auto& args : std::vector<std::vector<std::string>>{ { "unknownOperation" }, { "patch", "commands" } } )
    {
        bool refused = false;
        try { (void)sed_agent::runCommand("schema", args); }
        catch ( const sed_agent::AgentError& error )
        {
            refused = error.code == sed_agent::kExitUsage
                && error.errorCode == (args.size() == 1 ? "schemaTopicUnknown" : "schemaTopicInvalid");
        }
        check(refused, args.size() == 1 ? "unknown schema topic is rejected" : "extra schema topic is rejected");
    }
}

void dependencyContracts()
{
    Json schema;
    try
    {
        schema = sed_agent::runCommand("schema", { "dependencies" });
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "dependency request discovery must exist: " + error.message);
        return;
    }
    check(property(schema, "$schema").str == "https://json-schema.org/draft/2020-12/schema",
        "dependency discovery supplies a standalone schema");
    const auto inventory = sed_agent::runCommand("schema", {});
    check(equal(property(inventory, "dependencyRequestSchema"), schema),
        "focused and inventory dependency request schemas agree");
    const std::string digest(64, 'a');
    const std::string prefix = "{\"schemaVersion\":1,\"inputSha256\":\"" + digest + "\",\"archives\":[]";
    fixture(schema, prefix + "}", true, "empty ordered archive set is inspectable");
    fixture(schema, prefix + ",\"unknown\":true}", false, "dependency root fields are closed");
    fixture(schema, "{\"inputSha256\":\"" + digest + "\",\"archives\":[]}", false,
        "dependency schema requires schemaVersion");
    fixture(schema, "{\"schemaVersion\":1,\"archives\":[]}", false,
        "dependency schema requires the source digest");
    fixture(schema, "{\"schemaVersion\":1,\"inputSha256\":\"" + digest + "\"}", false,
        "dependency schema requires explicit archive ordering");
    fixture(schema, "{\"schemaVersion\":2,\"inputSha256\":\"" + digest + "\",\"archives\":[]}", false,
        "dependency schema refuses an unknown version");
    fixture(schema, prefix + ",\"limits\":{\"maxNodes\":0}}", false,
        "dependency graph reserves a source node");
    fixture(schema, prefix + ",\"limits\":{\"maxEdges\":0,\"maxNodes\":1.0,\"maxDepth\":1e1}}", true,
        "limits accept decoded integral decimal/exponent numbers and zero work");
    fixture(schema, prefix + ",\"limits\":{\"maxEdges\":1.5}}", false,
        "fractional limits are invalid");
    fixture(schema, prefix + ",\"limits\":{\"maxUnknown\":1}}", false,
        "dependency limit names are closed");
    const std::pair<const char*, int> maxima[]{
        { "maxNodes", 16384 }, { "maxEdges", 65536 }, { "maxDepth", 1024 },
        { "maxWorkUnits", 4000000 }, { "maxIndexEntries", 100000 }, { "maxPending", 16384 },
        { "maxScanBytes", 134217728 }, { "maxResourceBytes", 536870912 }, { "maxStringBytes", 67108864 },
        { "maxDetailRecords", 4096 }, { "maxDetailBytes", 4194304 }, { "maxIssues", 4096 }
    };
    for ( const auto& [name, maximum] : maxima )
    {
        fixture(schema, prefix + ",\"limits\":{\"" + name + "\":" + std::to_string(maximum) + "}}", true,
            std::string("documented maximum accepted: ") + name);
        fixture(schema, prefix + ",\"limits\":{\"" + name + "\":" + std::to_string(maximum + 1) + "}}", false,
            std::string("one over documented maximum rejected: ") + name);
    }
    const std::string archives = "{\"schemaVersion\":1,\"inputSha256\":\"" + digest + "\",\"archives\":[";
    const std::string archive = "{\"path\":\"resources.gob\",\"sha256\":\"" + digest + "\"";
    fixture(schema, archives + archive + ",\"stock\":true}]}", true, "stock archives remain hash-bound");
    fixture(schema, archives + archive + ",\"stock\":1}]}", false, "stock is a Boolean");
    fixture(schema, archives + archive + ",\"fileName\":\"legacy.gob\"}]}", false,
        "legacy archive aliases are excluded from the new closed request");
    fixture(schema, archives + "{\"path\":\"resources.gob\"}]}", false, "every archive needs a digest");
    fixture(schema, archives + "{\"path\":\"\",\"sha256\":\"" + digest + "\"}]}", false,
        "archive paths are nonempty");
    fixture(schema, prefix + ",\"extraResources\":[{\"name\":\"sprite.spr\",\"folder\":\"\"}]}", true,
        "extra resource folder may be empty");
    fixture(schema, prefix + ",\"extraResources\":[{\"name\":\"sprite.spr\"}]}", false,
        "extra resources declare their logical folder");
    fixture(schema, prefix + ",\"extraResources\":[{\"name\":\"sprite.spr\",\"folder\":\"spr\",\"path\":\"disk\"}]}", false,
        "extra resource fields cannot request disk reads");
}
void packageContracts()
{
    Json schema;
    for ( const char* topic : { "package-plan", "package-apply" } )
    {
        try
        {
            const auto discovered = sed_agent::runCommand("schema", { topic });
            if ( schema.type == Json::Type::Null ) schema = discovered;
            check(equal(schema, discovered), "plan/apply share the same package request schema");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(false, std::string(topic) + " request discovery must exist: " + error.message);
        }
    }
    if ( schema.type == Json::Type::Null ) return;
    check(property(schema, "$schema").str == "https://json-schema.org/draft/2020-12/schema",
        "package discovery supplies a standalone schema");
    check(property(schema, "x-runtimeConstraints").type == Json::Type::String,
        "package discovery distinguishes runtime admission from schema validation");

    const std::string digest(64, 'A');
    const std::string prefix = R"({"episode":{"episodeTitle":"Package"})";
    fixture(schema, prefix + "}", true, "legacy minimal package request needs only the episode title");
    fixture(schema, R"({})", false, "package request requires an episode");
    fixture(schema, R"({"episode":[]})", false, "episode is an object");
    fixture(schema, R"({"episode":{}})", false, "episode requires its title");
    for ( const char* title : { "", "Bad#title", "Bad\\\"title", "Bad\\ntitle" } )
        fixture(schema, std::string(R"({"episode":{"episodeTitle":")") + title + R"("}})", false,
            "episode titles follow the printable ASCII token contract");
    fixture(schema, std::string(R"({"episode":{"episodeTitle":")") + std::string(80, 'x') + R"("}})", true,
        "episode title accepts the 80-byte boundary");
    fixture(schema, std::string(R"({"episode":{"episodeTitle":")") + std::string(81, 'x') + R"("}})", false,
        "episode title refuses one over its boundary");
    fixture(schema, R"({"episode":{"episodeTitle":"Package","levelFileName":"Level.JKL","episodeType":-2147483648}})", true,
        "package level suffix is case-insensitive and episodeType is signed 32-bit");
    fixture(schema, R"({"episode":{"episodeTitle":"Package","levelFileName":"../level.jkl"}})", false,
        "episode map name is a plain JKL filename");
    fixture(schema, R"({"episode":{"episodeTitle":"Package","levelFileName":"level.sed"}})", false,
        "episode map name must use JKL");
    fixture(schema, R"({"episode":{"episodeTitle":"Package","episodeType":2147483648}})", false,
        "episodeType refuses signed overflow");
    fixture(schema, R"({"episode":{"episodeTitle":"Package","episodeType":1.5}})", false,
        "episodeType refuses fractions");
    fixture(schema, prefix + R"(,"schemaVersion":1.0,"archiveExtension":".GoO","archives":[],"stockArchives":[],"extraResources":[]})", true,
        "empty optional arrays, integral version and mixed-case GOO are accepted");
    fixture(schema, prefix + R"(,"schemaVersion":2})", false, "unsupported schema version refuses");
    fixture(schema, prefix + R"(,"archiveExtension":"gob"})", false, "archive extension includes the dot");
    fixture(schema, prefix + R"(,"archiveExtension":".gob\n"})", false, "archive extension must consume the complete string");
    fixture(schema, prefix + ",\"inputSha256\":\"" + digest + "\"}", true, "input digest accepts uppercase hex");
    fixture(schema, prefix + R"(,"inputSha256":"not-a-sha256"})", false, "source digest needs 64 hexadecimal digits");
    fixture(schema, prefix + R"(,"stockArchives":["stock.gob","stock.goo"]})", true,
        "stock archives use the existing string-path array contract");
    fixture(schema, prefix + R"(,"stockArchives":[{"path":"stock.gob"}]})", false,
        "stock archives do not invent bound-file objects");
    for ( const char* field : { "archives", "stockArchives", "extraResources" } )
        fixture(schema, prefix + ",\"" + field + "\":{}}", false, std::string(field) + " is an array");

    const std::string archives = prefix + R"(,"archives":[)";
    const std::string archive = R"({"sha256":")" + digest + '"';
    fixture(schema, archives + archive + R"(,"path":"resources.gob"}]})", true, "package archives are hash-bound");
    fixture(schema, archives + archive + R"(,"fileName":"resources.gob"}]})", true, "legacy archive fileName alias remains valid");
    fixture(schema, archives + archive + R"(,"path":"resources.gob","fileName":17,"extensionField":true}]})", true,
        "CLI path takes precedence over an ignored alias and preserves extension fields");
    fixture(schema, archives + archive + R"(,"path":17,"fileName":"resources.gob"}]})", false,
        "invalid primary archive path does not fall back to the alias");
    fixture(schema, archives + archive + R"(,"fileName":17}]})", false, "active archive alias is a string");
    fixture(schema, archives + archive + R"(}]})", false, "archive entry needs a path or its existing alias");
    fixture(schema, archives + R"({"path":"resources.gob"}]})", false, "archive entry requires its digest");
    fixture(schema, archives + R"("resources.gob"]})", false, "non-stock archive entry is an object");
    Json many = sed_agent::parseJson(prefix + ",\"archives\":[]}");
    for ( int i = 0; i < 17; ++i ) many.object.at("archives").array.push_back(
        sed_agent::parseJson(archive + R"(,"path":"resources.gob"})"));
    check(matches(schema, many), "package schema does not import the GUI's 16-archive admission cap");

    fixture(schema, prefix + R"(,"extraResources":[{"name":"wall.mat","folder":"mat"}]})", true,
        "explicit logical roots carry name and folder");
    fixture(schema, prefix + R"(,"extraResources":[{"fileName":"wall.mat","folder":"mat"}]})", true,
        "extra resource fileName alias remains supported");
    fixture(schema, prefix + R"(,"extraResources":[{"name":"wall.mat","fileName":false,"folder":"mat","other":null}]})", true,
        "primary logical name shadows its alias and permits extension fields");
    fixture(schema, prefix + R"(,"extraResources":[{"name":3,"fileName":"wall.mat","folder":"mat"}]})", false,
        "invalid primary logical name does not fall back");
    fixture(schema, prefix + R"(,"extraResources":[{"name":"wall.mat"}]})", false, "extra logical root needs a folder");
    fixture(schema, prefix + R"(,"extraResources":[{"name":"wall.mat","folder":""}]})", false,
        "package folder is nonempty unlike dependency inspection's folder");
    fixture(schema, prefix + R"(,"extraResources":[{"folder":"mat"}]})", false, "extra logical root needs a name or alias");

    for ( const char* option : { "maxNodes", "maxEdges", "maxDepth", "maxPackageBytes" } )
    {
        const auto options = prefix + ",\"options\":{\"" + option + "\":";
        fixture(schema, options + "0}}", true, std::string(option) + " admits zero; runtime decides buildability");
        fixture(schema, options + "-1}}", false, std::string(option) + " refuses negative work");
        fixture(schema, options + "1.5}}", false, std::string(option) + " refuses fractional work");
        const double bound = std::ldexp(1.0, std::numeric_limits<std::size_t>::digits);
        fixture(schema, options + sed_agent::toJsonString(Json::makeNumber(bound)) + "}}", false,
            std::string(option) + " refuses the first unrepresentable size_t");
        fixture(schema, options + sed_agent::toJsonString(Json::makeNumber(std::nextafter(bound, 0.0))) + "}}", true,
            std::string(option) + " accepts representable integral values below the platform bound");
    }
    fixture(schema, prefix + R"(,"options":{"allowUnverifiedReferences":true,"extensionField":0},"extensionField":null})", true,
        "package root and options preserve legacy extension fields");
    fixture(schema, prefix + R"(,"options":{"allowUnverifiedReferences":1}})", false,
        "allowUnverifiedReferences is a Boolean");
    fixture(schema, prefix + R"(,"options":[]})", false, "package options are an object");
}
} // namespace
int main()
{
    try { focusedContracts(); recoveredSourceContracts(); reviewedContracts(); invalidTopics(); dependencyContracts(); packageContracts(); }
    catch ( const std::exception& error ) { check(false, error.what()); }
    catch ( const sed_agent::AgentError& error ) { check(false, error.message); }
    std::cout << "failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
