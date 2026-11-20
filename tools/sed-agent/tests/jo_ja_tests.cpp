#include "../agent_workspace.hpp"
#include "../../../src/core/tests/assets/raven_bsp_fixture.hpp"
#include "sed/assets/mat_write.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using namespace sed_agent;

void check(bool value, const char* message)
{
    if ( !value )
    {
        throw std::runtime_error(message);
    }
}
void write(const fs::path& path, std::string_view data)
{
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(out.good(), "write test fixture");
}
template <class Fn> void rejects(Fn action, int expected)
{
    try
    {
        action();
    }
    catch ( const AgentError& e )
    {
        check(e.code == expected, e.message.c_str());
        return;
    }
    throw std::runtime_error("Expected refusal");
}
Json file(const fs::path& path, std::string_view data)
{
    Json row = Json::makeObject();
    row.set("path", Json::makeString(path.string()));
    row.set("sha256", Json::makeString(sha256Hex(data)));
    return row;
}
void test(const fs::path& dir)
{
    const auto bytes = sed::tests::makeRavenBspFixture();
    const std::string input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto source = dir / "scene.bsp";
    const auto request = dir / "import.json";
    const auto output = dir / "new.gob";
    write(source, input);
    Json spec = Json::makeObject();
    spec.set("source", file(source, input));
    spec.set("allowPlaceholders", Json::makeBool(true));
    auto plan = [&](const Json& value)
    {
        write(request, toJsonString(value));
        return runCommand("jo-ja-plan", {request.string()});
    };
    const auto inventory = runCommand("jo-ja-inspect", {source.string()});
    check(inventory.object.at("sourceSha256").str == sha256Hex(input) &&
              inventory.object.at("worldShaders").array.size() == 1 &&
              inventory.object.at("entityClasses").object.at("worldspawn").number == 1,
          "inspection inventory");
    const auto reviewed = plan(spec);
    check(!reviewed.object.at("runtimeVerified").boolean && reviewed.object.at("triangles").number == 33 &&
              !fs::exists(output),
          "plan without publication");
    std::vector<std::string> args{request.string(), output.string(), reviewed.object.at("outputSha256").str,
                                  reviewed.object.at("manifestSha256").str};
    const auto schema = runCommand("schema", {"jo-ja-plan"});
    check(schema.object.at("properties").object.contains("skipShaders"), "schema exposes explicit shader skips");
    const auto& properties = schema.object.at("properties").object;
    check(properties.contains("geometryMode") && properties.at("geometryMode").object.at("default").str == "model-scene"
              && properties.at("maxSectors").object.at("maximum").number == 96
              && properties.at("maxFragments").object.at("maximum").number == 1000000,
          "hybrid request schema preserves model-scene defaults and bounds sectors and fragments");
    auto explicitDefaults = spec;
    explicitDefaults.set("sourceProfile", Json::makeString("legacy"));
    const auto legacy = plan(explicitDefaults);
    check(legacy.object.at("sourceProfile").str == "legacy" &&
              legacy.object.at("outputSha256").str == reviewed.object.at("outputSha256").str &&
              legacy.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "explicit legacy source rules reproduce archives and review hashes for older recipes");
    auto profiled = spec;
    profiled.set("sourceProfile", Json::makeString("ja-mp"));
    const auto academy = plan(profiled);
    check(academy.object.at("sourceProfile").str == "ja-mp" &&
              academy.object.at("materials").array.at(0).object.at("sourceFootstep").str == "stone" &&
              academy.object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str,
          "agent receipt identifies the source profile and selected sound family");
    profiled.set("sourceProfile", Json::makeString("guess"));
    rejects([&] { plan(profiled); }, kExitUsage);
    explicitDefaults.set("geometryMode", Json::makeString("model-scene"));
    explicitDefaults.set("maxSectors", Json::makeNumber(64));
    explicitDefaults.set("maxFragments", Json::makeNumber(500000));
    check(plan(explicitDefaults).object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "explicit model-scene defaults preserve the original recipe");
    explicitDefaults.set("maxSectors", Json::makeNumber(32));
    check(plan(explicitDefaults).object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str,
          "a nondefault inactive budget is still bound by the model-scene manifest");
    const auto rooms = sed::tests::makeRavenBspRoomsFixture();
    const std::string roomsInput(reinterpret_cast<const char*>(rooms.data()), rooms.size());
    const auto roomsPath = dir / "rooms.bsp";
    write(roomsPath, roomsInput);
    auto hybrid = spec;
    hybrid.set("source", file(roomsPath, roomsInput));
    hybrid.set("geometryMode", Json::makeString("hybrid-sectors"));
    hybrid.set("trianglesPerModel", Json::makeNumber(479));
    hybrid.set("maxTriangles", Json::makeNumber(250000));
    hybrid.set("maxSectors", Json::makeNumber(2));
    hybrid.set("maxFragments", Json::makeNumber(1000000));
    const auto hybridPlan = plan(hybrid);
    check(hybridPlan.object.at("geometryMode").str == "hybrid-sectors" && hybridPlan.object.at("sectors").number == 2
              && hybridPlan.object.at("portalPairs").number > 0 && hybridPlan.object.at("nativeFaces").number > 0
              && hybridPlan.object.at("nativePromotionFallbacks").number == 0
              && hybridPlan.object.at("residualFaces").number > 0
              && hybridPlan.object.at("residualTriangles").number >= hybridPlan.object.at("residualFaces").number
              && hybridPlan.object.at("sourceArea").number > 0
              && !hybridPlan.object.at("models").array.empty() && hybridPlan.object.at("models").array.front().object.contains("sector")
              && hybridPlan.object.at("nativeBoundaryCollision").boolean && !hybridPlan.object.at("renderMeshCollision").boolean
              && !hybridPlan.object.at("runtimeVerified").boolean && !fs::exists(output),
          "hybrid plan accounts for native rooms, portals, residual detail and model ownership");
    hybrid.set("maxFragments", Json::makeNumber(999999));
    write(request, toJsonString(hybrid));
    rejects([&] { runCommand("jo-ja-apply", {request.string(), output.string(), hybridPlan.object.at("outputSha256").str,
                                             hybridPlan.object.at("manifestSha256").str}); }, kExitConflict);
    check(!fs::exists(output), "changing the hybrid fragment budget revokes reviewed output");
    for ( const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
              {"maxFragments", 1000001}, {"maxSectors", 97}, {"maxTriangles", 250001}, {"trianglesPerModel", 480}} )
    {
        auto invalid = hybrid;
        invalid.set(name, Json::makeNumber(value));
        rejects([&] { plan(invalid); }, kExitUsage);
    }
    auto unknownMode = spec;
    unknownMode.set("geometryMode", Json::makeString("automatic"));
    rejects([&] { plan(unknownMode); }, kExitUsage);
    Json audioSpec = spec;
    audioSpec.set("audioPolicy", Json::makeString("source-import"));
    bool unavailable = false;
    try
    {
        (void)plan(audioSpec);
    }
    catch ( const AgentError& e )
    {
        unavailable = e.code == kExitRefusal && e.errorCode == "joJaSourceAudioUnavailable";
    }
    check(unavailable && !fs::exists(output), "source audio import explicitly refuses without an archive");
    write(request, toJsonString(spec));
    auto stale = args;
    stale[3] = std::string(64, '0');
    rejects([&] { runCommand("jo-ja-apply", stale); }, kExitConflict);
    stale = args;
    stale[2] = std::string(64, '0');
    rejects([&] { runCommand("jo-ja-apply", stale); }, kExitConflict);
    write(source, input + "x");
    rejects([&] { runCommand("jo-ja-apply", args); }, kExitConflict);
    check(!fs::exists(output), "stale review leaves no output");
    write(source, input);
    runCommand("jo-ja-apply", args);
    auto wrongTarget = args;
    wrongTarget[1] = (dir / "wrong.goo").string();
    rejects([&] { runCommand("jo-ja-apply", wrongTarget); }, kExitUsage);
    check(!fs::exists(wrongTarget[1]), "wrong target never publishes");
    const auto alias = dir / "source-alias.gob";
    std::error_code linkError;
    fs::create_hard_link(source, alias, linkError);
    if ( !linkError )
    {
        auto aliased = args;
        aliased[1] = alias.string();
        rejects([&] { runCommand("jo-ja-apply", aliased); }, kExitRefusal);
        check(readFileBytes(source) == input && readFileBytes(alias) == input, "physical input aliases remain intact");
    }
    const auto published = readFileBytes(output);
    check(sha256Hex(published) == args[2], "apply publishes reviewed bytes");
    rejects([&] { runCommand("jo-ja-apply", args); }, kExitRefusal);
    check(readFileBytes(output) == published, "existing archive preserved");
    for ( const auto& [name, value] :
          std::initializer_list<std::pair<const char*, double>>{{"unknown", 1},
                                                                {"maxModels", 513},
                                                                {"patchSubdivisions", 1.5},
                                                                {"maxTriangles", 100001},
                                                                {"scale", 0},
                                                                {"trianglesPerModel", 171}} )
    {
        Json invalid = spec;
        invalid.set(name, Json::makeNumber(value));
        rejects([&] { plan(invalid); }, kExitUsage);
    }
    Json invalidSkips = spec;
    invalidSkips.set("skipShaders", parseJson(R"(["a","a"])"));
    rejects([&] { plan(invalidSkips); }, kExitUsage);
    Json unknownSourceField = spec;
    unknownSourceField.object.at("source").set("guess", Json::makeBool(true));
    rejects([&] { plan(unknownSourceField); }, kExitUsage);
    sed::assets::RgbaImage image{};
    image.width = image.height = 8;
    image.rgba.assign(8 * 8 * 4, 255);
    const auto indexed = sed::assets::buildIndexedMat({image}, {}).bytes;
    const std::string mat(reinterpret_cast<const char*>(indexed.data()), indexed.size());
    write(dir / "wall.mat", mat);
    Json mapping = Json::makeObject();
    mapping.set("shader", Json::makeString("a"));
    mapping.set("name", Json::makeString("wall.mat"));
    mapping.set("source", file(dir / "wall.mat", mat));
    Json maps = Json::makeArray();
    maps.array.push_back(mapping);
    spec.set("materials", maps);
    rejects([&] { plan(spec); }, kExitRefusal);
}
} // namespace
int main()
{
    const auto dir = fs::temp_directory_path() /
                     ("red-jo-ja-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(dir);
    try
    {
        test(dir);
    }
    catch ( const sed_agent::AgentError& e )
    {
        std::cerr << e.message << '\n';
        fs::remove_all(dir);
        return 1;
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        fs::remove_all(dir);
        return 1;
    }
    fs::remove_all(dir);
    return 0;
}
