// Real transport dispatch linked to the complete sed-agent/sed_core domain.
#define main sedMcpTransportMain
#include "../main.cpp"
#undef main

#include "../../../src/core/tests/assets/raven_bsp_fixture.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/sed.hpp"
#include "sed/values.hpp"

#include <utility>
#include <cstdint>
#include <set>
#include <regex>

namespace
{
int failures{};
void check(bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
Json object(const std::string& source) { return sed_agent::parseJson(source); }
std::string stringField(const Json& value, const char* key)
{
    const auto* field = value.find(key);
    return field && field->type == Json::Type::String ? field->str : "";
}
Json call(const std::string& name, const Json& arguments, bool expectedError = false)
{
    bool isError{}, known{};
    Json result = callTool(name, arguments, isError, known);
    check(known && isError == expectedError, name + " expected dispatch/error status: " + sed_agent::toJsonString(result));
    return result;
}
void writeFile(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if ( !out ) throw std::runtime_error("cannot write parity fixture");
}
Json bound(const std::string& path)
{
    Json result = Json::makeObject();
    result.set("path", Json::makeString(path));
    result.set("sha256", Json::makeString(sed_agent::sha256Hex(sed_agent::readFileBytes(g_workspace / path))));
    return result;
}
Json args(const char* path = "level.jkl")
{
    Json result = Json::makeObject();
    result.set("path", Json::makeString(path));
    return result;
}
Json requestArgs(const char* path, Json request)
{
    Json result = args(path);
    result.set("request", std::move(request));
    return result;
}
void checkNativeOutputNames(const Json& reviewed);

void nativeModelParity()
{
    auto original = sed_agent::readFileBytes(NATIVE_RELAY_FIXTURE);
    const auto shared = original.find("0x00001 1 0 -1");
    original.replace(shared, std::string("0x00001 1").size(), "0x00001 0");
    const auto model = g_workspace / "shared.3do";
    writeFile(model, original);
    const Json inspect = call("sed_model_native_inspect", args("shared.3do"));
    const auto& rows = inspect.object.at("items").array;
    check(rows.size() == 4 && rows[2].object.at("affectedNodeCount").number == 2
        && rows[3].object.at("affectedNodeCount").number == 0, "raw source mesh ordinals retain shared instances and an orphan");
    auto request = object(R"({"schemaVersion":1,"profile":"classic2.1","operations":[{"op":"transformMesh","geoset":1,
        "sourceMesh":1,"scale":1.25,"translation":[0.125,-0.25,0.5],"radiusPolicy":"preserve"}]})");
    request.set("inputSha256", inspect.object.at("inputSha256"));
    auto mcpArgs = requestArgs("shared.3do", request);
    const auto requestPath = g_workspace / "mesh-edit.json";
    writeFile(requestPath, sed_agent::toJsonString(request));
    const auto cliPlan = sed_agent::runCommand("model-native-plan", { model.string(), requestPath.string() });
    const auto mcpPlan = call("sed_model_native_plan", mcpArgs);
    check(sed_agent::toJsonString(mcpPlan) == sed_agent::toJsonString(cliPlan), "MCP and CLI transform plans agree byte for byte");
    const auto& change = mcpPlan.object.at("operations").array.front();
    check(change.object.at("affectedNodeCount").number == 0 && change.object.at("authoredRadius").number == 0.1
        && change.object.at("beforeBounds").object.at("min").array[2].number == 0
        && change.object.at("afterBounds").object.at("min").array[2].number == 0.5
        && change.object.at("changedComponents").number == 9 && change.object.at("changedPositionRows").number == 3,
        "orphan transform receipt exposes stored changes and retained mesh radius");
    mcpArgs.set("expectedRequestSha256", cliPlan.object.at("requestSha256"));
    mcpArgs.set("expectedOutputSha256", cliPlan.object.at("outputSha256"));
    mcpArgs.set("output", Json::makeString("orphan-new.3do"));
    auto invalidId = mcpArgs;
    invalidId.set("operationId", Json::makeString(std::string("bad\xff", 4)));
    const auto refusal = call("sed_model_native_apply", invalidId, true);
    check(stringField(refusal, "code") == "invalidArguments" && !fs::exists(g_workspace / "orphan-new.3do"),
        "native journal refuses an operation ID that could not be returned as JSON");
    (void)call("sed_model_native_apply", mcpArgs);
    checkNativeOutputNames(mcpArgs);
    auto expected = original;
    const auto start = expected.rfind("VERTICES 3\n0: 0.000000");
    expected.replace(start, expected.find("TEXTURE VERTICES", start) - start,
        "VERTICES 3\n0: 0.125 -0.25 0.5 1.000000\n1: 0.1625 -0.25 0.5 1.000000\n"
        "2: 0.125 -0.2125 0.5 1.000000\n");
    check(sed_agent::readFileBytes(g_workspace / "orphan-new.3do") == expected && sed_agent::readFileBytes(model) == original,
        "MCP publishes exact mesh-local XYZ tokens and preserves all unrelated source bytes");
}

void joJaRoutes()
{
    const auto bytes = sed::tests::makeRavenBspFixture(false);
    writeFile(g_workspace / "scene.bsp", { reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    auto inspected = call("sed_jo_ja_inspect", args("scene.bsp"));
    check(stringField(inspected, "sourceSha256") == stringField(bound("scene.bsp"), "sha256"),
        "JO/JA inspect hashes the admitted BSP");
    Json request = Json::makeObject();
    request.set("sourceProfile", Json::makeString("jo-mp"));
    request.set("worldModelRenderPass", Json::makeString("ordinaryThing"));
    request.set("source", bound("scene.bsp"));
    request.set("allowPlaceholders", Json::makeBool(true));
    Json wrap = Json::makeObject();
    wrap.set("request", request);
    const auto plan = call("sed_jo_ja_plan", wrap);
    check(stringField(plan, "sourceProfile") == "jo-mp", "MCP preserves the selected source multiplayer profile");
    check(stringField(plan, "worldModelRenderPass") == "ordinaryThing",
          "MCP plans and publishes the selected JK world model render pass");
    check(!fs::exists(g_workspace / "scene.gob"), "JO/JA plan publishes no archive");
    wrap.set("output", Json::makeString("scene.gob"));
    wrap.set("expectedOutputSha256", Json::makeString(stringField(plan, "outputSha256")));
    wrap.set("expectedManifestSha256", Json::makeString(stringField(plan, "manifestSha256")));
    wrap.set("operationId", Json::makeString("jo-ja-parity"));
    call("sed_jo_ja_apply", wrap);
    const auto replay = call("sed_jo_ja_apply", wrap);
    check(replay.find("journaled") && replay.find("journaled")->boolean
        && sed_agent::sha256Hex(sed_agent::readFileBytes(g_workspace / "scene.gob")) == stringField(plan, "outputSha256"),
        "JO/JA replay returns the journaled outcome and preserves the archive");
    wrap.set("expectedManifestSha256", Json::makeString(std::string(64, '0')));
    auto collision = call("sed_jo_ja_apply", wrap, true);
    check(stringField(collision, "errorCode") == "operationIdConflict", "MCP operation identity binds the reviewed manifest hash");

    const auto rooms = sed::tests::makeRavenBspRoomsFixture(true);
    writeFile(g_workspace / "rooms.bsp", { reinterpret_cast<const char*>(rooms.data()), rooms.size() });
    const auto visibility = object(R"({"viewCluster":0})");
    auto sourceArgs = args("rooms.bsp");
    sourceArgs.set("sourceVisibility", visibility);
    const auto sourceInfo = call("sed_jo_ja_inspect", sourceArgs);
    check(sourceInfo.object.at("sourceVisibility").object.at("surfaces").array.at(1).object.at("state").str == "outside-pvs",
          "MCP passes the source PVS inspection through its workspace policy");
    request.set("sourceVisibility", visibility);
    request.set("source", bound("rooms.bsp"));
    request.set("geometryMode", Json::makeString("hybrid-sectors"));
    request.set("trianglesPerModel", Json::makeNumber(400));
    Json hybrid = Json::makeObject();
    hybrid.set("request", request);
    const auto plannedRooms = call("sed_jo_ja_plan", hybrid);
    check(plannedRooms.object.at("sourceVisibility").object.at("rowUsable").boolean,
          "MCP hybrid plan returns the same generated-target visibility evidence");
    check(stringField(plannedRooms, "geometryMode") == "hybrid-sectors"
            && plannedRooms.object.at("sectors").number == 2 && plannedRooms.object.at("portalPairs").number > 0,
        "MCP passes hybrid geometry options and returns the native rooms and portals");
}

const Json* property(const Json& tools, const std::string& tool, const char* key)
{
    for ( const auto& entry : tools.array )
    {
        if ( stringField(entry, "name") != tool ) continue;
        const auto* schema = entry.find("inputSchema");
        const auto* props = schema ? schema->find("properties") : nullptr;
        return props ? props->find(key) : nullptr;
    }
    return nullptr;
}
void checkNativeOutputNames(const Json& reviewed)
{
    const auto catalog = toolCatalog();
    const auto* outputSchema = property(catalog, "sed_model_native_apply", "output");
    if ( !outputSchema )
    {
        check(false, "native apply advertises an output schema");
        return;
    }
    const std::regex pattern(outputSchema->object.at("pattern").str);
    fs::create_directory(g_workspace / "subdir");
    fs::create_directory(g_workspace / "backslash-dir");
    for ( const auto& [name, admitted] : { std::pair{ ".3do", false }, std::pair{ "subdir/.3do", false },
            std::pair{ ".edited.3do", true }, std::pair{ "asset.3DO", true },
            std::pair{ R"(backslash-dir\.3do)", fs::path::preferred_separator != '\\' } } )
    {
        check(std::regex_search(name, pattern) == admitted, std::string("native output schema basename: ") + name);
        auto args = reviewed;
        args.set("output", Json::makeString(name));
        const auto result = call("sed_model_native_apply", args, !admitted);
        check(fs::is_regular_file(g_workspace / name) == admitted &&
                (admitted || stringField(result, "code") == "modelNativeInvalidRequest"),
            std::string("native output publication basename: ") + name);
    }
}

void campaignRoutes(const fs::path& outside)
{
    writeFile(g_workspace / "episode.jk", "\"Example\"\nTYPE 1\nSEQ 1\n10: 0 1 LEVEL room.jkl 0 0 -1 -1\nEND\n");
    writeFile(g_workspace / "room.jkl", "original synthetic export");
    Json args = object(R"({"request":{"schemaVersion":1,"episode":{"path":"episode.jk"},
        "inputs":[{"id":"room","path":"room.jkl"}],
        "sequences":[{"line":10,"input":"room","entry":{},"exits":{}}]}})");
    const auto report = call("sed_campaign_check", args);
    check(report.find("inputsComplete") && report.find("inputsComplete")->boolean,
        "campaign check reads the episode and its declared relative inputs");
    for ( const char* key : {"schemaVersion", "entry"} )
    {
        auto rounded = args;
        auto& request = rounded.object.at("request");
        if ( std::string_view(key) == "schemaVersion" )
        {
            request.set(key, object("1.0000000000000001"));
        }
        else
        {
            request.object.at("sequences").array.front().set(key, object(R"({"score":4503599627370496.25})"));
        }
        check(stringField(call("sed_campaign_check", rounded, true), "code") == "campaignContractInvalid",
            "campaign MCP rejects exact decimal fractions before staging its request");
    }
    auto exact = args;
    exact.object.at("request").object.at("sequences").array.front().set("entry", object(R"({"score":120e-1})"));
    check(call("sed_campaign_check", exact).find("sequences") != nullptr,
        "campaign MCP accepts an exact integer written with a negative exponent");
    const auto baseline = sed_agent::toJsonString(report);
    writeFile(g_workspace / "campaign-baseline.json", baseline);
    auto& request = args.object.at("request");
    request.set("baseline", bound("campaign-baseline.json"));
    const auto current = call("sed_campaign_check", args);
    check(current.find("sequences") && stringField(current.object.at("sequences").array.at(0), "freshness") == "current",
        "MCP admits the hash-bound baseline and returns its comparison");
    for ( const char* key : {"episode", "inputs", "baseline"} )
    {
        auto escaped = args;
        auto& descriptor = escaped.object.at("request").object.at(key);
        auto& file = descriptor.isObject() ? descriptor : descriptor.array.front();
        file.set("path", Json::makeString(sed_agent::pathToUtf8(outside / "outside.dat")));
        check(stringField(call("sed_campaign_check", escaped, true), "code") == "outsideWorkspace",
            "each nested campaign filesystem path is admitted before use");
    }
    auto missing = args;
    const auto missingName = sed_agent::pathToUtf8(fs::path(u8"missing-\u00e9pisode.jk"));
    missing.object.at("request").object.at("episode").set("path", Json::makeString(missingName));
    const auto absent = call("sed_campaign_check", missing, true);
    const auto diagnostic = stringField(absent, "error");
    check(sed_agent::isValidJsonUtf8(diagnostic) && diagnostic.find(missingName) != std::string::npos,
        "a missing non-ASCII episode retains its UTF-8 name in the error receipt");

    auto extra = args;
    extra.set("path", Json::makeString("unrelated.jkl"));
    check(stringField(call("sed_campaign_check", extra, true), "code") == "invalidArguments",
        "the read-only campaign route takes only the request");
    const auto external = outside / "room.jkl";
    writeFile(external, "original synthetic export");
    g_resourceRoots.push_back(outside);
    request.object.at("inputs").array.front().set("path", Json::makeString(sed_agent::pathToUtf8(external)));
    const auto admitted = call("sed_campaign_check", args);
    check(admitted.find("sequences") && stringField(admitted.object.at("sequences").array.at(0), "freshness") == "current",
        "an explicitly admitted resource root with identical named bytes reproduces the receipt");
    g_resourceRoots.pop_back();
}

void catalogAndDiscovery()
{
    const std::set<std::string> expected{
        "sed_discover", "sed_schema", "sed_inspect", "sed_statistics", "sed_query", "sed_select", "sed_diagnose",
        "sed_workbench_query", "sed_arch_validate", "sed_plan", "sed_apply", "sed_new", "sed_convert", "sed_archive",
        "sed_campaign_check", "sed_archive_create", "sed_dependencies", "sed_package_plan", "sed_package_apply", "sed_snapshot", "sed_overview", "sed_bindings",
        "sed_cog_check", "sed_cog_compile", "sed_cog_run", "sed_assistant_check", "sed_assistant_script", "sed_model_hierarchy", "sed_model_hierarchy_edit", "sed_key_from_model", "sed_prefab_capture", "sed_export_dxf", "sed_dxf_inspect", "sed_dxf_import", "sed_mat_inspect",
        "sed_mat_export_png", "sed_mat_create", "sed_cmp_create", "sed_cmp_master_create", "sed_model_import", "sed_terrain_3do", "sed_model_lighting_inspect",
        "sed_model_lighting_plan", "sed_model_lighting_apply", "sed_model_native_inspect", "sed_model_native_plan", "sed_model_native_apply", "sed_key_inspect", "sed_key_plan", "sed_key_apply",
        "sed_jo_ja_inspect", "sed_jo_ja_plan", "sed_jo_ja_apply",
        "sed_launch_plan", "sed_native_inspect", "sed_outcome"
    };
    const auto catalog = toolCatalog();
    std::set<std::string> actual;
    for ( const auto& entry : catalog.array ) actual.insert(stringField(entry, "name"));
    check(catalog.array.size() == expected.size() && actual == expected, "catalog has exactly the documented named routes");
    for ( const auto& name : expected )
    {
        bool error{}, known{};
        (void)callTool(name, Json::makeObject(), error, known);
        check(known, name + " is implemented, not merely advertised");
    }
    bool error{}, known{};
    (void)callTool("sed_exec", object(R"({"command":"inspect","argv":[]})"), error, known);
    check(!known && error, "arbitrary CLI execution has no route");
    const auto inventory = call("sed_discover", Json::makeObject());
    check(inventory.find("commands") != nullptr, "path-free discovery returns command inventory");
    const auto* preview = inventory.find("semanticPreview");
    check(preview && preview->isObject() && stringField(*preview, "coverage") == "level-model-v1",
        "MCP discovery advertises the shared semantic preview scope");
    for ( const char* topic : { "commands", "patch", "setVertexPosition", "bakeThingArchitecture", "package-plan", "package-apply", "mat-create", "cmp-create", "cmp-master-create",
             "model-import", "archive-create", "snapshot", "overview", "buildStairs", "buildSwitchbackStairs" } )
    {
        Json argument = Json::makeObject(); argument.set("topic", Json::makeString(topic));
        const auto discovered = call("sed_schema", argument);
        check(sed_agent::toJsonString(discovered) == sed_agent::toJsonString(sed_agent::runCommand("schema", { topic })),
            std::string("focused schema is the domain schema: ") + topic);
    }
    const auto bad = call("sed_schema", object(R"({"topic":"unknown-operation"})"), true);
    check(stringField(bad, "code") == "schemaTopicUnknown" && bad.find("exitCode"), "unknown topic retains domain code and exit");
    (void)call("sed_discover", object(R"({"path":"level.jkl","topic":"patch"})"), true);
    (void)call("sed_schema", args(), true);
    const auto typed = call("sed_schema", object(R"({"topic":"patch"})"));
    for ( const char* name : { "sed_plan", "sed_apply" } )
    {
        const auto* patch = property(catalog, name, "request");
        check(patch && sed_agent::toJsonString(*patch) == sed_agent::toJsonString(typed), std::string(name) + " embeds shared typed patch schema");
    }
    const auto querySchema = sed_agent::runCommand("schema", { "model-native-inspect" });
    const auto* nativeQuery = property(catalog, "sed_model_native_inspect", "query");
    check(nativeQuery && sed_agent::toJsonString(*nativeQuery) == sed_agent::toJsonString(querySchema),
        "native inspect query schema matches the CLI");
    const auto matCreate = call("sed_schema", object(R"({"topic":"mat-create"})"));
    const auto matDiscover = call("sed_discover", object(R"({"topic":"mat-create"})"));
    const auto* matRequest = property(catalog, "sed_mat_create", "request");
    check(matRequest && sed_agent::toJsonString(*matRequest) == sed_agent::toJsonString(matCreate),
        "MAT creation embeds the exact focused request schema");
    check(sed_agent::toJsonString(matDiscover) == sed_agent::toJsonString(matCreate),
        "path-free MAT schema discovery agrees across MCP discovery routes");
    for ( const auto& [topic, toolName] : { std::pair{ "model-import", "sed_model_import" },
             std::pair{ "model-native", "sed_model_native_plan" }, std::pair{ "model-native", "sed_model_native_apply" },
             std::pair{ "campaign-check", "sed_campaign_check" }, std::pair{ "cmp-create", "sed_cmp_create" }, std::pair{ "cmp-master-create", "sed_cmp_master_create" },
             std::pair{ "archive-create", "sed_archive_create" }, std::pair{ "snapshot", "sed_snapshot" },
             std::pair{ "overview", "sed_overview" }, std::pair{ "jo-ja-plan", "sed_jo_ja_plan" },
             std::pair{ "jo-ja-apply", "sed_jo_ja_apply" } } )
    {
        Json argument = Json::makeObject(); argument.set("topic", Json::makeString(topic));
        const auto focused = call("sed_schema", argument);
        const auto* embedded = property(catalog, toolName, "request");
        check(embedded && sed_agent::toJsonString(*embedded) == sed_agent::toJsonString(focused),
            std::string(toolName) + " embeds the exact focused request schema");
        check(sed_agent::toJsonString(call("sed_discover", argument)) == sed_agent::toJsonString(focused),
            std::string("path-free discovery agrees across MCP routes: ") + topic);
    }
    const auto package = call("sed_schema", object(R"({"topic":"package-plan"})"));
    check(sed_agent::toJsonString(package)
        == sed_agent::toJsonString(call("sed_discover", object(R"({"topic":"package-apply"})"))),
        "both discovery routes expose the shared package request");
    for ( const char* name : { "sed_package_plan", "sed_package_apply" } )
    {
        const auto* request = property(catalog, name, "request");
        check(request && sed_agent::toJsonString(*request) == sed_agent::toJsonString(package),
            std::string(name) + " embeds the exact focused package request schema");
    }
    const auto* height = property(catalog, "sed_dxf_import", "height");
    const auto* cel = property(catalog, "sed_mat_export_png", "cel");
    const auto* strict = property(catalog, "sed_cog_check", "strict");
    check(height && stringField(*height, "type") == "number", "DXF height schema is numeric");
    check(cel && stringField(*cel, "type") == "integer", "MAT cel schema is integral");
    check(strict && stringField(*strict, "type") == "boolean", "COG strict schema is boolean");
}

void stairWalkabilityRoutes()
{
    const auto original = sed_agent::readFileBytes(g_workspace / "level.jkl");
    Json patch = object(R"({"schemaVersion":1,"operations":[{"op":"buildStairs","origin":[10,0,0],"direction":"+x","width":0.5,"run":0.1,"rise":0.0625,"headroom":0.5,"steps":9,"walkability":{"maxStepRise":0.03125}}]})");
    patch.set("inputSha256", Json::makeString(sed_agent::sha256Hex(original)));
    Json arguments = requestArgs("level.jkl", patch);
    const auto planned = call("sed_plan", arguments);
    const auto* reports = planned.find("stairWalkabilityReports");
    check(reports && reports->array.size() == 1, "MCP plan forwards the shared stair advisory");
    if ( !reports )
    {
        return;
    }
    writeFile(g_workspace / "stair-patch.json", sed_agent::toJsonString(patch));
    const auto direct = sed_agent::runCommand("plan",
        { (g_workspace / "level.jkl").string(), (g_workspace / "stair-patch.json").string() });
    const auto* directReports = direct.find("stairWalkabilityReports");
    check(directReports && sed_agent::toJsonString(*directReports) == sed_agent::toJsonString(*reports),
        "MCP retains exact shared-domain advisory content");
    arguments.set("output", Json::makeString("stair-advisory.jkl"));
    arguments.set("expectedOutputSha256", Json::makeString(stringField(planned, "outputSha256")));
    arguments.set("operationId", Json::makeString("stair-advisory-apply"));
    const auto applied = call("sed_apply", arguments);
    const auto* appliedReports = applied.find("stairWalkabilityReports");
    check(appliedReports && sed_agent::toJsonString(*appliedReports) == sed_agent::toJsonString(*reports),
        "MCP apply returns the same non-blocking report");
    const auto replayed = call("sed_apply", arguments);
    const auto* replayedReports = replayed.find("stairWalkabilityReports");
    check(replayedReports && replayed.find("journaled") && replayed.find("journaled")->boolean
        && sed_agent::toJsonString(*replayedReports) == sed_agent::toJsonString(*reports),
        "journal replay preserves the original stair advisory");
    Json changed = arguments;
    changed.object.at("request").object.at("operations").array[0].object.at("walkability")
        .set("maxStepRise", Json::makeNumber(0.125));
    check(stringField(call("sed_apply", changed, true), "code") == "operationIdConflict",
        "changed advisory configuration still conflicts under an existing operation ID");
    check(sed_agent::readFileBytes(g_workspace / "level.jkl") == original, "MCP advisory preserves source bytes");
    const auto catalog = toolCatalog();
    const auto* requestSchema = property(catalog, "sed_plan", "request");
    check(requestSchema && sed_agent::toJsonString(*requestSchema).find("maxStepRise") != std::string::npos,
        "MCP catalog exposes the optional stair policy through shared patch schema");
}

void semanticPreviewRoutes()
{
    const auto sourceBytes = sed_agent::readFileBytes(g_workspace / "level.jkl");
    Json patch = object(R"({"schemaVersion":1,"operations":[{"op":"setLevelHeader","gravity":6}]})");
    patch.set("inputSha256", Json::makeString(sed_agent::sha256Hex(sourceBytes)));
    Json arguments = requestArgs("level.jkl", patch);
    const auto planned = call("sed_plan", arguments);
    const auto* preview = planned.find("semanticPreview");
    check(preview && preview->isObject(), "MCP plan returns semantic preview");
    if ( !preview || !preview->isObject() ) return;
    check(stringField(*preview, "inputSha256") == sed_agent::sha256Hex(sourceBytes)
        && stringField(*preview, "outputSha256") == stringField(planned, "outputSha256"),
        "MCP preview binds the actual source and reviewed output bytes");
    writeFile(g_workspace / "semantic-patch.json", sed_agent::toJsonString(patch));
    const auto direct = sed_agent::runCommand("plan",
        { (g_workspace / "level.jkl").string(), (g_workspace / "semantic-patch.json").string() });
    const auto* directPreview = direct.find("semanticPreview");
    check(directPreview && sed_agent::toJsonString(*directPreview) == sed_agent::toJsonString(*preview),
        "MCP plan preserves both exact domain comparison views");
    arguments.set("output", Json::makeString("semantic-edited.jkl"));
    arguments.set("expectedOutputSha256", Json::makeString(stringField(planned, "outputSha256")));
    arguments.set("operationId", Json::makeString("semantic-apply"));
    const auto applied = call("sed_apply", arguments);
    const auto* appliedPreview = applied.find("semanticPreview");
    check(appliedPreview && sed_agent::toJsonString(*appliedPreview) == sed_agent::toJsonString(*preview),
        "MCP apply returns the preview reviewed by plan");
    const auto outputBytes = sed_agent::readFileBytes(g_workspace / "semantic-edited.jkl");
    check(sed_agent::sha256Hex(outputBytes) == stringField(planned, "outputSha256"),
        "MCP apply publishes the exact preview-bound bytes");
    const auto recorded = lookupOperation("semantic-apply");
    check(recorded.state == "completed" && recorded.fingerprint == fingerprintOf("sed_apply", arguments),
        "semantic receipt is journaled under the original relative arguments");
    const auto replayed = call("sed_apply", arguments);
    const auto* replayedPreview = replayed.find("semanticPreview");
    check(replayed.find("journaled") && replayed.find("journaled")->boolean
        && replayedPreview && sed_agent::toJsonString(*replayedPreview) == sed_agent::toJsonString(*preview),
        "completed operation replays its original semantic receipt");
    check(sed_agent::readFileBytes(g_workspace / "semantic-edited.jkl") == outputBytes
        && sed_agent::readFileBytes(g_workspace / "level.jkl") == sourceBytes,
        "semantic receipt replay preserves output and source bytes");

    Json legacyArguments = arguments;
    legacyArguments.set("operationId", Json::makeString("semantic-legacy"));
    Json legacyResult = object(R"({"ok":true,"legacy":true})");
    appendJournal("semantic-legacy", "sed_apply", "completed",
        fingerprintOf("sed_apply", legacyArguments), &legacyResult);
    const auto legacy = call("sed_apply", legacyArguments);
    check(legacy.find("legacy") && !legacy.find("semanticPreview") && legacy.find("journaled"),
        "older completed receipts are replayed without inventing a new preview");
}
void dependencyRoutes(const fs::path& outside)
{
    // Lexical inspection accepts raw section text without loading a Level.
    const std::string source = "not a complete JKL\nSECTION: MATERIALS\nWORLD MATERIALS 1\n0: wall.mat\nEND\n";
    const std::string archive = sed::assets::buildGobBytes({ { "mat/wall.mat", "opaque material" } });
    writeFile(g_workspace / "dependency.jkl", source);
    writeFile(g_workspace / "dependency.gob", archive);
    Json request = object(R"({"schemaVersion":1,"archives":[]})");
    request.set("inputSha256", Json::makeString(sed_agent::sha256Hex(source)));
    request.object.at("archives").array.push_back(bound("dependency.gob"));
    Json arguments = requestArgs("dependency.jkl", request);
    const auto receipt = call("sed_dependencies", arguments);
    check(stringField(receipt, "command") == "dependencies" && receipt.find("nodes") && receipt.find("edges"),
        "dependency route returns a raw-JKL graph");
    if ( stringField(receipt, "command") != "dependencies" ) return;
    check(receipt.find("scanComplete") && receipt.find("scanComplete")->boolean
        && receipt.find("allReferencesResolved") && receipt.find("allReferencesResolved")->boolean,
        "dependency fixture resolves the selected material");
    Json directRequest = request;
    directRequest.object.at("archives").array.front().set("path", Json::makeString((g_workspace / "dependency.gob").string()));
    writeFile(g_workspace / "dependency-request.json", sed_agent::toJsonString(directRequest));
    const auto direct = sed_agent::runCommand("dependencies",
        { (g_workspace / "dependency.jkl").string(), (g_workspace / "dependency-request.json").string() });
    check(sed_agent::toJsonString(receipt) == sed_agent::toJsonString(direct), "dependency MCP receipt equals CLI domain receipt");
    const auto catalog = toolCatalog();
    const auto* embedded = property(catalog, "sed_dependencies", "request");
    check(embedded && sed_agent::toJsonString(*embedded)
        == sed_agent::toJsonString(sed_agent::runCommand("schema", { "dependencies" })),
        "dependency catalog embeds the exact shared request schema");
    const auto* sourcePathSchema = property(catalog, "sed_dependencies", "path");
    const auto* requestProperties = embedded ? embedded->find("properties") : nullptr;
    const auto* archivesSchema = requestProperties ? requestProperties->find("archives") : nullptr;
    const auto* archiveSchema = archivesSchema ? archivesSchema->find("items") : nullptr;
    const auto* archiveProperties = archiveSchema ? archiveSchema->find("properties") : nullptr;
    const auto* archivePathSchema = archiveProperties ? archiveProperties->find("path") : nullptr;
    check(sourcePathSchema && archivePathSchema
        && sed_agent::toJsonString(*sourcePathSchema) == sed_agent::toJsonString(*archivePathSchema),
        "dependency source and archive paths expose identical NUL exclusion and UTF-8 byte constraints");
    Json nulSource = arguments;
    nulSource.set("path", Json::makeString(std::string("dependency.jkl") + '\0' + "ignored"));
    const auto nulSourceError = call("sed_dependencies", nulSource, true);
    check(nulSourceError.find("exitCode") && nulSourceError.find("exitCode")->number == sed_agent::kExitUsage,
        "dependency source path rejects embedded NUL before filesystem access");
    for ( const char* key : { "output", "operationId", "surprise" } )
    {
        Json bad = arguments; bad.set(key, Json::makeString("must-not-exist"));
        check(stringField(call("sed_dependencies", bad, true), "code") == "invalidArguments",
            std::string("dependency outer request rejects ") + key);
    }
    Json nonUtf8Argument = arguments;
    nonUtf8Argument.set(std::string(1, static_cast<char>(0xff)), Json::makeBool(true));
    const auto nonUtf8Error = call("sed_dependencies", nonUtf8Argument, true);
    check(stringField(nonUtf8Error, "code") == "invalidArguments"
        && sed_agent::toJsonString(nonUtf8Error).find(static_cast<char>(0xff)) == std::string::npos,
        "dependency unknown outer argument returns JSON-safe diagnostics for arbitrary key bytes");
    for ( const char* invalid : { R"({"maxNodes":0})", R"({"maxDepth":0.5})", R"({"maxEdges":-1})",
        R"({"unknown":1})", R"({"maxWorkUnits":4000001})" } )
    {
        Json bad = arguments; bad.object.at("request").set("limits", object(invalid));
        const auto refused = call("sed_dependencies", bad, true);
        check(refused.find("exitCode") && refused.find("exitCode")->number == sed_agent::kExitUsage,
            "dependency nested limits reject with shared usage exit");
    }
    Json malformed = arguments;
    malformed.object.at("request").set("unknown", Json::makeBool(true));
    malformed.set("path", Json::makeString((outside / "missing.jkl").string()));
    const auto preflight = call("sed_dependencies", malformed, true);
    check(preflight.find("exitCode") && preflight.find("exitCode")->number == sed_agent::kExitUsage,
        "dependency request validation precedes filesystem admission");
    Json invalidHash = arguments;
    invalidHash.object.at("request").set("inputSha256", Json::makeString(std::string(64, '0')));
    const auto mismatch = call("sed_dependencies", invalidHash, true);
    check(mismatch.find("exitCode") && mismatch.find("exitCode")->number == sed_agent::kExitConflict,
        "dependency source hash conflict retains CLI exit");
    invalidHash = arguments;
    invalidHash.object.at("request").object.at("archives").array.front().set("sha256", Json::makeString(std::string(64, '0')));
    const auto archiveMismatch = call("sed_dependencies", invalidHash, true);
    check(archiveMismatch.find("exitCode") && archiveMismatch.find("exitCode")->number == sed_agent::kExitConflict,
        "dependency archive hash conflict retains CLI exit");
    Json limited = arguments;
    limited.object.at("request").set("limits", object(R"({"maxEdges":0})"));
    const auto stopped = call("sed_dependencies", limited);
    check(stopped.find("scanComplete") && !stopped.find("scanComplete")->boolean,
        "dependency budget stop is a successful diagnostic report");
    writeFile(outside / "dependency.jkl", source); writeFile(outside / "dependency.gob", archive);
    Json external = arguments; external.set("path", Json::makeString((outside / "dependency.jkl").string()));
    check(stringField(call("sed_dependencies", external, true), "code") == "outsideWorkspace", "dependency source path policy");
    external = arguments;
    external.object.at("request").object.at("archives").array.front().set("path", Json::makeString((outside / "dependency.gob").string()));
    check(stringField(call("sed_dependencies", external, true), "code") == "outsideWorkspace", "dependency archive path policy before capture");
    g_resourceRoots.push_back(outside);
    (void)call("sed_dependencies", external);
    g_resourceRoots.clear();
#ifndef _WIN32
    fs::create_symlink(outside / "dependency.jkl", g_workspace / "dependency-link.jkl");
    external = arguments; external.set("path", Json::makeString("dependency-link.jkl"));
    check(stringField(call("sed_dependencies", external, true), "code") == "outsideWorkspace", "dependency source symlink policy");
    fs::create_symlink(outside / "dependency.gob", g_workspace / "dependency-link.gob");
    external = arguments;
    external.object.at("request").object.at("archives").array.front().set("path", Json::makeString("dependency-link.gob"));
    check(stringField(call("sed_dependencies", external, true), "code") == "outsideWorkspace", "dependency archive symlink policy");
#endif
    Json logical = arguments;
    logical.object.at("request").set("extraResources", object(R"([{"name":"../../outside/absent.wav","folder":"../../outside"}])"));
    const auto missing = call("sed_dependencies", logical);
    check(missing.find("allReferencesResolved") && !missing.find("allReferencesResolved")->boolean,
        "dependency logical names are scanned without filesystem admission");
    const fs::path savedWorkspace = g_workspace;
    g_workspace /= "dependency-no-journal";
    fs::create_directories(g_workspace);
    writeFile(g_workspace / ".sed-mcp", "journal directory blocked");
    writeFile(g_workspace / "dependency.jkl", source); writeFile(g_workspace / "dependency.gob", archive);
    const auto unjournaled = call("sed_dependencies", arguments);
    check(stringField(unjournaled, "command") == "dependencies"
        && sed_agent::readFileBytes(g_workspace / ".sed-mcp") == "journal directory blocked",
        "dependency inspection works with an unavailable journal and leaves it untouched");
    check(sed_agent::readFileBytes(g_workspace / "dependency.jkl") == source
        && sed_agent::readFileBytes(g_workspace / "dependency.gob") == archive,
        "dependency inspection preserves captured inputs");
    g_workspace = savedWorkspace;
}
void packageRoutes(const fs::path& outside)
{
    const std::string source = "SECTION: MATERIALS\nWORLD MATERIALS 1\n0: wall.mat\nEND\n";
    const std::string archive = sed::assets::buildGobBytes({ { "mat/wall.mat", "opaque material" } });
    const std::string stock = sed::assets::buildGobBytes({ { "sound/stock.wav", "opaque sound" } });
    writeFile(g_workspace / "package-schema.jkl", source);
    writeFile(g_workspace / "package-schema.gob", archive);
    writeFile(g_workspace / "package-stock.gob", stock);
    Json request = object(R"({"schemaVersion":1,"episode":{"episodeTitle":"Package schema","levelFileName":"Mission.JKL","episodeType":1},"archives":[],"stockArchives":["package-stock.gob"],"extraResources":[{"fileName":"stock.wav","folder":"sound"}],"options":{"maxNodes":4,"maxDepth":0}})");
    request.set("inputSha256", Json::makeString(sed_agent::sha256Hex(source)));
    Json archiveBinding = bound("package-schema.gob");
    archiveBinding.set("fileName", *archiveBinding.find("path")); archiveBinding.object.erase("path");
    request.object.at("archives").array.push_back(archiveBinding);
    auto arguments = requestArgs("package-schema.jkl", request);
    const auto planned = call("sed_package_plan", arguments);
    check(planned.find("canBuild") && planned.find("canBuild")->boolean
        && planned.find("stockOmissions") && planned.find("stockOmissions")->array.size() == 1,
        "legacy alias and stock-string fixture produces a buildable package with a stock omission");
    Json directRequest = request;
    directRequest.object.at("archives").array.front().set("fileName", Json::makeString((g_workspace / "package-schema.gob").string()));
    directRequest.object.at("stockArchives").array.front() = Json::makeString((g_workspace / "package-stock.gob").string());
    const auto direct = [&](const Json& body)
    {
        const auto path = g_workspace / "package-schema-request.json";
        writeFile(path, sed_agent::toJsonString(body));
        try { return sed_agent::runCommand("package-plan", { (g_workspace / "package-schema.jkl").string(), path.string() }); }
        catch ( const sed_agent::AgentError& error ) { return agentErrorPayload(error); }
    };
    check(sed_agent::toJsonString(planned) == sed_agent::toJsonString(direct(directRequest)),
        "package MCP and CLI domain receipts remain byte-equivalent");
    Json legacy = directRequest;
    auto& primary = legacy.object.at("archives").array.front();
    primary.set("path", *primary.find("fileName")); primary.set("fileName", Json::makeNumber(17));
    legacy.object.at("extraResources").array.front().set("name", Json::makeString("stock.wav"));
    legacy.object.at("extraResources").array.front().set("fileName", Json::makeBool(false));
    legacy.set("legacyExtension", Json::makeBool(true));
    check(sed_agent::toJsonString(planned) == sed_agent::toJsonString(direct(legacy)),
        "CLI primary aliases shadow ignored fields without altering the reviewed package receipt");
    Json shadowed = arguments;
    shadowed.object.at("request").object.at("archives").array.front().set("path", Json::makeString("package-schema.gob"));
    shadowed.object.at("request").object.at("archives").array.front().set("fileName", Json::makeNumber(17));
    check(stringField(call("sed_package_plan", shadowed, true), "code") == "invalidArguments",
        "MCP still admits every supplied filesystem alias even when CLI would ignore one");
    for ( const char* option : { "maxNodes", "maxPackageBytes" } )
    {
        Json limited = arguments;
        limited.object.at("request").object.at("options").set(option, Json::makeNumber(0));
        const auto blocked = call("sed_package_plan", limited, true);
        Json directLimited = directRequest;
        directLimited.object.at("options").set(option, Json::makeNumber(0));
        check(sed_agent::toJsonString(blocked) == sed_agent::toJsonString(direct(directLimited))
            && blocked.find("exitCode") && blocked.find("exitCode")->number == sed_agent::kExitInput
            && blocked.find("canBuild") && !blocked.find("canBuild")->boolean,
            std::string(option) + " zero remains a schema-valid blocked plan with the same CLI/MCP receipt");
    }
    Json stale = arguments;
    stale.object.at("request").set("inputSha256", Json::makeString(std::string(64, '0')));
    const auto conflict = call("sed_package_plan", stale, true);
    check(conflict.find("exitCode") && conflict.find("exitCode")->number == sed_agent::kExitConflict,
        "schema-shaped but stale source hash remains a runtime conflict");
    Json external = arguments;
    external.object.at("request").object.at("stockArchives").array.front() = Json::makeString((outside / "stock.gob").string());
    check(stringField(call("sed_package_plan", external, true), "code") == "outsideWorkspace",
        "stock archive filesystem strings retain MCP resource-root admission");
    Json apply = arguments;
    apply.set("output", Json::makeString("package-schema-output.gob"));
    apply.set("expectedOutputSha256", *planned.find("outputSha256"));
    apply.set("expectedManifestSha256", *planned.find("manifestSha256"));
    const auto written = call("sed_package_apply", apply);
    check(written.find("written") && written.find("written")->boolean
        && sed_agent::sha256Hex(sed_agent::readFileBytes(g_workspace / "package-schema-output.gob")) == stringField(planned, "outputSha256"),
        "shared package request applies only the exact reviewed archive bytes");
    const auto duplicate = call("sed_package_apply", apply, true);
    check(duplicate.find("exitCode") && duplicate.find("exitCode")->number == sed_agent::kExitRefusal,
        "package apply remains create-only after schema discovery");
    check(sed_agent::readFileBytes(g_workspace / "package-schema.jkl") == source
        && sed_agent::readFileBytes(g_workspace / "package-schema.gob") == archive
        && sed_agent::readFileBytes(g_workspace / "package-stock.gob") == stock,
        "package schema workflow preserves all captured inputs");
}
void documentRoutes()
{
    const auto inspect = call("sed_inspect", args());
    check(sed_agent::toJsonString(inspect) == sed_agent::toJsonString(call("sed_discover", args())), "legacy path discovery still inspects");
    for ( const auto& mapping : { std::pair{ "sed_statistics", "statistics" }, { "sed_inspect", "inspect" } } )
        check(sed_agent::toJsonString(call(mapping.first, args())) == sed_agent::toJsonString(sed_agent::runCommand(mapping.second, { (g_workspace / "level.jkl").string() })), std::string(mapping.first) + " matches real domain output");
    const auto binding = call("sed_bindings", requestArgs("level.jkl", object(R"({"cog":0})")), true);
    check(binding.find("exitCode") && binding.find("exitCode")->number == sed_agent::kExitInput, "bindings reaches real cog selector validation");
    Json overview = requestArgs("level.jkl", object(R"({"view":"overhead","width":64,"height":64,"fullbright":true,"output":"overview.png"})"));
    overview.set("operationId", Json::makeString("overview-output"));
    (void)call("sed_overview", overview);
    check(fs::exists(g_workspace / "overview.png") && lookupOperation("overview-output").state == "completed", "nested overview output is published and journaled");
    (void)call("sed_overview", overview);
    Json capture = requestArgs("level.jkl", object(R"({"sectors":[0]})")); capture.set("output", Json::makeString("room.prefab"));
    (void)call("sed_prefab_capture", capture);
    check(fs::exists(g_workspace / "room.prefab"), "prefab capture forwards output after request");
    Json dxf = args(); dxf.set("output", Json::makeString("room.dxf")); (void)call("sed_export_dxf", dxf);
    (void)call("sed_dxf_inspect", args("room.dxf"));
    Json import = args("room.dxf"); import.set("height", Json::makeString("2")); import.set("output", Json::makeString("bad.jkl"));
    const auto refused = call("sed_dxf_import", import, true);
    check(stringField(refused, "code") == "invalidArguments" && !fs::exists(g_workspace / "bad.jkl"), "DXF rejects string height without output");
    writeFile(g_workspace / "outline.dxf", "0\nSECTION\n2\nENTITIES\n0\n3DFACE\n8\nWALL\n10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n12\n1\n22\n1\n32\n0\n13\n0\n23\n1\n33\n0\n0\nENDSEC\n0\nEOF\n");
    import.set("path", Json::makeString("outline.dxf")); import.set("height", Json::makeNumber(1.5)); import.set("output", Json::makeString("outline.jkl"));
    const auto imported = call("sed_dxf_import", import);
    check(fs::exists(g_workspace / "outline.jkl") && stringField(imported, "command") == "dxf-import", "DXF numeric height precedes explicit output in domain dispatch");
    Json strict = args("fixture.cog"); strict.set("game", Json::makeString("jk")); strict.set("strict", Json::makeString("false"));
    check(stringField(call("sed_cog_check", strict, true), "code") == "invalidArguments", "COG booleans do not coerce strings");
    writeFile(g_workspace / "fixture.cog", "symbols\nend\ncode\nend\n");
    strict.set("strict", Json::makeBool(false));
    const auto cog = call("sed_cog_check", strict);
    check(stringField(cog, "command") == "cog-check" && stringField(cog, "target") == "jk", "COG wrapper forwards real game target and boolean option");
    Json snapshot = requestArgs("level.jkl", object(R"({"width":64,"height":64,"camera":{"position":[3,-3,3],"target":[0,0,0]},"fullbright":true})"));
    snapshot.set("output", Json::makeString("snapshot.png")); snapshot.set("operationId", Json::makeString("snapshot-output"));
    (void)call("sed_snapshot", snapshot);
    check(fs::exists(g_workspace / "snapshot.png") && lookupOperation("snapshot-output").state == "completed", "snapshot uses write journal");
    auto failure = call("sed_package_plan", requestArgs("level.jkl", object(R"({"episode":{"episodeTitle":"Parity","levelFileName":"level.jkl"}})")), true);
    check(failure.find("exitCode") && failure.find("issues") && failure.find("canBuild") && failure.find("unverifiedScopes"), "blocked package preserves full real structured domain payload and exit status");
}
void assetRoutes()
{
    writeFile(g_workspace / "anim.key", "SECTION: HEADER\nFLAGS 16\nTYPE 0x3\nFRAMES 100\nFPS 29.97\nJOINTS 4\nSECTION: MARKERS\nMARKERS 1\n2.5 7\nSECTION: KEYFRAME\nNODES 1\nNODE 3\nMESH NAME mesh0\nENTRIES 1\n0: 0.25 0x1 1 2 3 10 20 30\n0.1 0.2 0.3 1 2 3\n");
    (void)call("sed_key_inspect", args("anim.key"));
    Json key = requestArgs("anim.key", object(R"({"operations":[{"op":"sortNode","node":0}]})"));
    const auto plan = call("sed_key_plan", key);
    key.set("output", Json::makeString("edited.key")); key.set("expectedOutputSha256", Json::makeString(stringField(plan, "outputSha256")));
    (void)call("sed_key_apply", key);
    check(fs::exists(g_workspace / "edited.key") && sed_agent::sha256Hex(sed_agent::readFileBytes(g_workspace / "edited.key")) == stringField(plan, "outputSha256"), "KEY apply passes expected hash before output");
    key.object.at("request") = object(R"({"operations":[{"op":"sortNode","node":99}]})"); key.object.erase("output"); key.object.erase("expectedOutputSha256");
    const auto bad = call("sed_key_plan", key, true);
    check(stringField(bad, "code") == "keyNodeOutOfRange" && bad.find("exitCode"), "KEY domain refusal retains stable code");
    std::string mat = "MAT ";
    const auto word = [&](std::uint32_t value) { for ( int shift = 0; shift < 32; shift += 8 ) mat.push_back(static_cast<char>((value >> shift) & 255)); };
    word(0x32); word(2); word(1); word(1); word(0); word(8); for ( int i = 0; i < 12; ++i ) word(0);
    word(8); word(0); for ( int i = 0; i < 8; ++i ) word(0);
    word(16); word(8); word(0); word(0); word(0); word(1); mat.append(128, '\1');
    writeFile(g_workspace / "seed.mat", mat);
    std::string cmp = "CMP "; cmp.resize(64, '\0'); for ( int i = 0; i < 256; ++i ) cmp.append(3, static_cast<char>(i));
    writeFile(g_workspace / "pal.cmp", cmp);
    (void)call("sed_mat_inspect", args("seed.mat"));
    Json png = args("seed.mat"); png.set("output", Json::makeString("cel.png"));
    (void)call("sed_mat_export_png", png);
    check(fs::exists(g_workspace / "cel.png"), "MAT export supports omitted cel and palette");
    png.set("output", Json::makeString("palette.png")); png.set("palette", Json::makeString("pal.cmp")); (void)call("sed_mat_export_png", png);
    png.set("cel", Json::makeNumber(0.5)); png.set("output", Json::makeString("fractional.png"));
    check(stringField(call("sed_mat_export_png", png, true), "code") == "invalidArguments", "MAT cel rejects fractions");
    Json create = Json::makeObject(); Json request = Json::makeObject(); Json cels = Json::makeArray(); cels.array.push_back(bound("cel.png"));
    request.set("cels", cels); request.set("palette", bound("pal.cmp")); create.set("request", request); create.set("output", Json::makeString("made.mat"));
    create.set("operationId", Json::makeString("relative-cels"));
    (void)call("sed_mat_create", create);
    const auto prior = lookupOperation("relative-cels");
    check(prior.state == "completed" && prior.fingerprint == fingerprintOf("sed_mat_create", create), "journal fingerprints original relative nested arguments");
    (void)call("sed_mat_create", create);
    Json legacy = create; legacy.set("operationId", Json::makeString("legacy-raw"));
    Json result = Json::makeObject(); result.set("ok", Json::makeBool(true)); result.set("legacy", Json::makeBool(true));
    appendJournal("legacy-raw", "sed_mat_create", "completed", fingerprintOf("sed_mat_create", legacy), &result);
    const auto replay = call("sed_mat_create", legacy);
    check(replay.find("legacy") && replay.find("journaled"), "preexisting raw fingerprint replays after request path normalization");
    Json rgbRequest = object(R"({"format":"rgb565"})");
    rgbRequest.set("cels", cels);
    Json rgb = Json::makeObject(); rgb.set("request", rgbRequest);
    rgb.set("output", Json::makeString("rgb565.mat"));
    rgb.set("operationId", Json::makeString("rgb565-cels"));
    const auto rgbResult = call("sed_mat_create", rgb);
    check(stringField(rgbResult, "format") == "rgb565" && rgbResult.find("bitsPerPixel")
            && rgbResult.find("bitsPerPixel")->number == 16 && !rgbResult.find("paletteSha256"),
        "MCP RGB565 forwards palette-free request and preserves format receipt");
    if ( fs::exists(g_workspace / "rgb565.mat") )
    {
        const auto bytes = sed_agent::readFileBytes(g_workspace / "rgb565.mat");
        check(bytes.size() == 396 && bytes.substr(140, 2) == std::string(2, '\0')
                && sed_agent::sha256Hex(bytes) == stringField(rgbResult, "outputSha256"),
            "MCP RGB565 publishes expected pixels and captured output hash");
        const auto rgbReplay = call("sed_mat_create", rgb);
        check(lookupOperation("rgb565-cels").state == "completed"
                && stringField(rgbReplay, "outputSha256") == stringField(rgbResult, "outputSha256"),
            "MCP RGB565 journal replays without recreating the asset");
    }
    Json archive = Json::makeObject(); Json entry = bound("made.mat"); entry.set("name", Json::makeString("mat/made.mat")); Json entries = Json::makeArray(); entries.array.push_back(entry);
    Json archiveRequest = Json::makeObject(); archiveRequest.set("entries", entries); archive.set("request", archiveRequest); archive.set("output", Json::makeString("assets.gob"));
    (void)call("sed_archive_create", archive); (void)call("sed_archive", args("assets.gob"));
    writeFile(g_workspace / "triangle.obj", "o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl ground\nf 1/1 2/2 3/3\n");
    Json modelRequest = object(R"({"materials":[{"name":"ground","mat":"ground.mat"}]})"); modelRequest.object.at("materials").array.front().set("matFile", bound("seed.mat")); modelRequest.set("source", bound("triangle.obj"));
    Json model = Json::makeObject(); model.set("request", modelRequest); model.set("output", Json::makeString("triangle.3do"));
    (void)call("sed_model_import", model);
    check(fs::exists(g_workspace / "triangle.3do"), "model import consumes workspace-relative bound source and publishes 3DO");
    Json terrain = Json::makeObject(); terrain.set("request", object(R"({"origin":[0,0,0],"cellSize":1,"headroom":2,"columns":2,"rows":2,"heights":[0,0,0,0],"material":"ground.mat"})")); terrain.set("output", Json::makeString("terrain.3do"));
    (void)call("sed_terrain_3do", terrain);
    check(fs::exists(g_workspace / "terrain.3do"), "terrain wrapper publishes actual 3DO");
}

void nativePolicyRootSpelling(const fs::path& parent)
{
    const fs::path configured = parent / u8"Case-\u6f22";
    const fs::path otherSpelling = parent / u8"CASE-\u6f22";
    fs::create_directory(configured);
    if ( fs::exists(otherSpelling) )
    {
#ifdef _WIN32
        // Exercise the prefix comparison directly even when weakly_canonical
        // happens to normalize the spelling before isWithin calls it.
        check(fs::equivalent(configured, otherSpelling)
                && hasPolicyRoot(otherSpelling / "new.3do", configured)
                && isWithin(otherSpelling / "new.3do", configured),
            "the same Unicode policy root with a different spelling admits its child");
#endif
    }
    else
    {
        fs::create_directory(otherSpelling);
        check(!fs::equivalent(configured, otherSpelling)
                && !hasPolicyRoot(otherSpelling / "new.3do", configured)
                && !isWithin(otherSpelling / "new.3do", configured),
            "distinct similarly spelled Unicode roots stay separate on a case-sensitive filesystem");
    }
}

void staticSnapshotRoutes()
{
    auto level = sed::assets::loadFromJKL(g_workspace / "level.jkl");
    level.things.clear();
    sed::Thing thing;
    thing.sector = 0;
    thing.values = { sed::deserializeTplValue("model3d", "triangle.3do") };
    level.things.push_back(thing);
    const auto levelBytes = sed::assets::saveToSedBytes(level);
    writeFile(g_workspace / "static.sed", { reinterpret_cast<const char*>(levelBytes.data()), levelBytes.size() });
    writeFile(g_workspace / "static.gob", sed::assets::buildGobBytes({
        { "3do\\triangle.3do", sed_agent::readFileBytes(g_workspace / "triangle.3do") },
        { "mat\\ground.mat", sed_agent::readFileBytes(g_workspace / "seed.mat") } }));
    Json request = object(R"({"width":64,"height":64,"fullbright":true,"thingMode":"static"})");
    Json archives = Json::makeArray();
    archives.array.push_back(bound("static.gob"));
    request.set("resourceArchives", archives);
    auto arguments = requestArgs("static.sed", request);
    arguments.set("output", Json::makeString("static.png"));
    arguments.set("operationId", Json::makeString("static-preview"));
    const auto result = call("sed_snapshot", arguments);
    const auto* preview = result.find("thingPreview");
    const auto* models = preview ? preview->find("modelsPrepared") : nullptr;
    check(models && models->type == Json::Type::Number && models->number == 1,
        "MCP forwards static mode and actual captured model resources");
    const auto replay = call("sed_snapshot", arguments);
    check(stringField(replay, "imageSha256") == stringField(result, "imageSha256")
        && lookupOperation("static-preview").state == "completed", "MCP static image replays without overwrite");
    arguments.object.at("request").set("thingMode", Json::makeString("none"));
    check(stringField(call("sed_snapshot", arguments, true), "code") == "operationIdConflict",
        "MCP journal binds static versus world-only rendering choice");
    request.set("view", Json::makeString("overhead"));
    request.set("output", Json::makeString("static-overview.png"));
    const auto overview = call("sed_overview", requestArgs("static.sed", request));
    check(overview.find("thingPreview") && stringField(overview, "renderScope") == "world-surfaces-and-static-things",
        "MCP overview retains static render receipt");
}

void pathsAndResourcePolicy(const fs::path& outside)
{
    fs::create_directories(g_workspace / "game"); fs::create_directories(g_workspace / "project"); writeFile(g_workspace / "game" / "openjkdf2", "fixture");
    Json launch = Json::makeObject(); launch.set("request", object(R"({"game":"JediKnight","gameDirectory":"game","projectDirectory":"project","executable":"openjkdf2","levelFileName":"level.jkl","engine":"OpenJKDF2"})"));
    const auto plan = call("sed_launch_plan", launch);
    check(fs::path(stringField(plan, "executable")) == g_workspace / "game" / "openjkdf2", "launch executable is relative to admitted game directory");
    const std::string outsidePath = (outside / "escape.bin").string(); writeFile(outside / "escape.bin", "private fixture bytes");
    for ( const auto& item : { std::pair{ "sed_plan", R"({"operations":[{"op":"insertPrefab","prefabFile":"OUTSIDE"}]})" },
        { "sed_plan", R"({"operations":[{"op":"bakeLighting","gobs":["OUTSIDE"]}]})" },
        { "sed_plan", R"({"operations":[{"op":"addCog","resourceArchives":[{"path":"OUTSIDE"}]}]})" },
        { "sed_plan", R"({"templateSource":{"path":"OUTSIDE"},"operations":[]})" },
        { "sed_diagnose", R"({"resourceArchives":[{"path":"OUTSIDE","sha256":"x"}]})" },
        { "sed_arch_validate", R"({"models":[{"path":"OUTSIDE"}]})" },
        { "sed_package_plan", R"({"archives":[{"fileName":"OUTSIDE"}]})" },
        { "sed_model_import", R"({"source":{"path":"OUTSIDE"}})" },
        { "sed_mat_create", R"({"palette":{"path":"OUTSIDE"}})" },
        { "sed_archive_create", R"({"entries":[{"path":"OUTSIDE"}]})" },
        { "sed_overview", R"({"output":"OUTSIDE"})" } } )
    {
        std::string source = item.second; source.replace(source.find("\"OUTSIDE\""), 9, sed_agent::toJsonString(Json::makeString(outsidePath)));
        auto argument = requestArgs("level.jkl", object(source)); argument.set("output", Json::makeString("must-not-exist.bin"));
        check(stringField(call(item.first, argument, true), "code") == "outsideWorkspace", std::string(item.first) + " admits documented nested disk paths");
    }
#ifndef _WIN32
    // Windows symbolic links require an elevated or Developer Mode host;
    // the portable path-policy cases above remain enabled on every platform.
    writeFile(g_workspace / "worker" / "worker-config.json", "{}");
    fs::create_symlink(outside / "escape.bin", g_workspace / "worker" / "sed-cpp-bridge.exe");
    Json native = args(); native.set("game", Json::makeString("jk")); native.set("format", Json::makeString("jkl")); native.set("workerConfig", Json::makeString("worker/worker-config.json"));
    check(stringField(call("sed_native_inspect", native, true), "code") == "outsideWorkspace", "native inspection admits the adjacent worker executable");
#endif
    Json logical = requestArgs("level.jkl", object(R"({"episode":{"episodeTitle":"Parity"},"extraResources":[{"name":"missing.wav","folder":"logical/sound"}]})"));
    check(stringField(call("sed_package_plan", logical, true), "code") != "outsideWorkspace", "package logical resource folders are not disk read paths");
    // The real document names this material; the bake must refuse the winning
    // external target before loading it, rather than accepting omitted emissives.
    auto level = sed::assets::loadFromJKL(g_workspace / "level.jkl");
    check(!level.materials.empty() && !level.sectors.empty(), "starter document actually references a material");
    if ( level.materials.empty() || level.sectors.empty() ) return;
    for ( auto& sector : level.sectors )
        for ( auto& surface : sector.surfaces ) { surface.light = 0; surface.material = level.materials.front(); }
    writeFile(g_workspace / "emissive.jkl", sed::assets::buildJklExport(level).text);
    fs::create_directories(g_workspace / "resources");
    const auto referenced = fs::path(level.materials.front()).filename();
#ifndef _WIN32
    writeFile(outside / referenced, sed_agent::readFileBytes(g_workspace / "seed.mat"));
    fs::create_symlink(outside / referenced, g_workspace / "resources" / referenced);
    const auto bake = requestArgs("emissive.jkl", object(R"({"operations":[{"op":"bakeLighting","resourceRoots":["resources"]}]})"));
    const auto rayBake = requestArgs("emissive.jkl",
        object(R"({"operations":[{"op":"bakeLighting","transport":"ray","resourceRoots":["resources"]}]})"));
    check(stringField(call("sed_plan", rayBake, true), "code") == "outsideWorkspace",
        "ray material lookup enforces MCP workspace policy before decoding");
    const auto refused = call("sed_plan", bake, true);
    check(stringField(refused, "code") == "outsideWorkspace", "actual document-referenced external MAT is refused");
    Json apply = bake; apply.set("output", Json::makeString("external-refused.jkl")); apply.set("expectedOutputSha256", Json::makeString(std::string(64, '0')));
    check(stringField(call("sed_apply", apply, true), "code") == "outsideWorkspace" && !fs::exists(g_workspace / "external-refused.jkl"), "actual external MAT refuses apply without publishing output");
    check(sed_agent::toJsonString(refused).find("private fixture bytes") == std::string::npos, "resource refusal does not expose outside bytes");
    fs::remove(g_workspace / "resources" / referenced);
#endif
    writeFile(g_workspace / "resources" / referenced, sed_agent::readFileBytes(g_workspace / "seed.mat"));
    // An unrelated alias is deliberately irrelevant to the winning resource.
    // Repeating the same roots/operations must not recursively inspect it again.
#ifndef _WIN32
    fs::create_directory_symlink(outside, g_workspace / "resources" / "unused-outside");
#endif
    Json repeated = requestArgs("emissive.jkl", object(R"({"operations":[]})"));
    Json roots = Json::makeArray(); for ( int i = 0; i < 64; ++i ) roots.array.push_back(Json::makeString("resources"));
    for ( int i = 0; i < 8; ++i ) { Json op = object(R"({"op":"bakeLighting"})"); op.set("resourceRoots", roots); repeated.object.at("request").object.at("operations").array.push_back(op); }
    const auto admitted = call("sed_plan", repeated);
    check(admitted.find("outputSha256") != nullptr, "repeated resource roots do not recursively scan unused descendants");
    Json forbidden = requestArgs("level.jkl", object(R"({"operations":[{"op":"bakeLighting","resourceRoots":[]}]})"));
    forbidden.object.at("request").object.at("operations").array.front().set("resourceRoots", Json::makeArray());
    forbidden.object.at("request").object.at("operations").array.front().object.at("resourceRoots").array.push_back(Json::makeString(outside.string()));
    check(stringField(call("sed_plan", forbidden, true), "code") == "outsideWorkspace", "initial resource-root admission remains enforced");
    g_resourceRoots.push_back(outside);
    (void)call("sed_plan", forbidden);
    g_resourceRoots.clear();
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("red-mcp-parity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    g_workspace = root / "workspace"; fs::create_directories(g_workspace); fs::create_directories(root / "outside");
    try
    {
        // Run catalog tests before domain fixtures so missing parity fails clearly.
        catalogAndDiscovery();
        campaignRoutes(root / "outside");
        nativeModelParity();
        nativePolicyRootSpelling(root);
        (void)sed_agent::runCommand("new", { (g_workspace / "level.jkl").string() });
        dependencyRoutes(root / "outside"); packageRoutes(root / "outside"); stairWalkabilityRoutes(); semanticPreviewRoutes(); joJaRoutes(); documentRoutes(); assetRoutes(); staticSnapshotRoutes(); pathsAndResourcePolicy(root / "outside");
        sed_agent::AgentError rich{ sed_agent::kExitRefusal, "rich refusal", "richCode", object(R"({"ok":false,"details":{"preserved":true}})"), true };
        const auto payload = agentErrorPayload(rich);
        check(payload.find("details") && payload.find("exitCode") && stringField(payload, "code") == "richCode", "full structured AgentError payload survives transport");
    }
    catch ( const sed_agent::AgentError& error ) { check(false, "unexpected domain error: " + error.message); }
    catch ( const std::exception& error ) { check(false, std::string("unexpected error: ") + error.what()); }
    std::error_code cleanup; fs::remove_all(root, cleanup);
    std::cout << "MCP parity failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
