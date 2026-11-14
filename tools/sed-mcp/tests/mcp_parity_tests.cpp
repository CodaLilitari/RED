// Real transport dispatch linked to the complete sed-agent/sed_core domain.
#define main sedMcpTransportMain
#include "../main.cpp"
#undef main

#include "sed/assets/jkl.hpp"
#include "sed/assets/gob.hpp"

#include <cstdint>
#include <set>

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
void catalogAndDiscovery()
{
    const std::set<std::string> expected{
        "sed_discover", "sed_schema", "sed_inspect", "sed_statistics", "sed_query", "sed_select", "sed_diagnose",
        "sed_workbench_query", "sed_arch_validate", "sed_plan", "sed_apply", "sed_new", "sed_convert", "sed_archive",
        "sed_archive_create", "sed_dependencies", "sed_package_plan", "sed_package_apply", "sed_snapshot", "sed_overview", "sed_bindings",
        "sed_cog_check", "sed_prefab_capture", "sed_export_dxf", "sed_dxf_inspect", "sed_dxf_import", "sed_mat_inspect",
        "sed_mat_export_png", "sed_mat_create", "sed_model_import", "sed_terrain_3do", "sed_model_lighting_inspect",
        "sed_model_lighting_plan", "sed_model_lighting_apply", "sed_key_inspect", "sed_key_plan", "sed_key_apply",
        "sed_launch_plan", "sed_native_inspect", "sed_outcome"
    };
    const auto catalog = toolCatalog();
    std::set<std::string> actual;
    for ( const auto& entry : catalog.array ) actual.insert(stringField(entry, "name"));
    check(catalog.array.size() == 40 && actual == expected, "catalog has exactly the 40 documented named routes");
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
    for ( const char* topic : { "commands", "patch", "setVertexPosition", "bakeThingArchitecture", "package-plan", "package-apply", "mat-create" } )
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
    const auto matCreate = call("sed_schema", object(R"({"topic":"mat-create"})"));
    const auto matDiscover = call("sed_discover", object(R"({"topic":"mat-create"})"));
    const auto* matRequest = property(catalog, "sed_mat_create", "request");
    check(matRequest && sed_agent::toJsonString(*matRequest) == sed_agent::toJsonString(matCreate),
        "MAT creation embeds the exact focused request schema");
    check(sed_agent::toJsonString(matDiscover) == sed_agent::toJsonString(matCreate),
        "path-free MAT schema discovery agrees across MCP discovery routes");
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
        (void)sed_agent::runCommand("new", { (g_workspace / "level.jkl").string() });
        dependencyRoutes(root / "outside"); packageRoutes(root / "outside"); semanticPreviewRoutes(); documentRoutes(); assetRoutes(); pathsAndResourcePolicy(root / "outside");
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
