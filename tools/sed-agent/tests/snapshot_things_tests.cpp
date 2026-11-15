#include "../agent_workspace.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/png.hpp"
#include "sed/assets/sed.hpp"
#include "sed/values.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures{};
void check(bool ok, const char *message)
{
    if ( !ok )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
void write(const fs::path &path, const std::string &value)
{
    std::ofstream out(path, std::ios::binary);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if ( !out )
        throw std::runtime_error("fixture write failed");
}
const Json &field(const Json &json, const char *name)
{
    const auto *value = json.find(name);
    if ( !value )
        throw std::runtime_error(std::string("missing field: ") + name);
    return *value;
}
std::string bytes(const std::vector<std::byte> &data)
{
    return {reinterpret_cast<const char *>(data.data()), data.size()};
}
Json run(const fs::path &root, const fs::path &level, Json request, const char *command, const char *output)
{
    if ( std::string(command) == "overview" )
        request.set("output", Json::makeString((root / output).string()));
    write(root / "request.json", sed_agent::toJsonString(request));
    std::vector<std::string> arguments{level.string(), (root / "request.json").string()};
    if ( std::string(command) == "snapshot" )
        arguments.push_back((root / output).string());
    return sed_agent::runCommand(command, arguments);
}
void tests(const fs::path &root)
{
    sed::Level level;
    sed::Sector sector;
    sector.ambient = sed::ColorWhite;
    sector.vertices = {{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}};
    sed::Surface floor;
    floor.geo = 4;
    floor.material = "red.mat";
    floor.normal = {0, 0, 1};
    floor.vertices = {0, 1, 2, 3};
    floor.texVertices = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    sector.surfaces = {floor};
    level.sectors = {sector};
    sed::Thing thing;
    thing.sector = 0;
    thing.z = 1;
    thing.values = {sed::deserializeTplValue("model3d", "triangle.3do")};
    level.things = {thing};
    const auto source = root / "fixture.sed";
    write(source, bytes(sed::assets::saveToSedBytes(level)));
    const auto before = sed_agent::readFileBytes(source);
    sed::assets::Model3do model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    model.materials = {"green.mat"};
    sed::assets::Model3doMesh mesh;
    mesh.name = "triangle";
    mesh.geo = 4;
    mesh.light = 3;
    mesh.tex = 1;
    mesh.vertices = {{-0.8, -0.8, 0}, {0.8, -0.8, 0}, {0, 0.8, 0}};
    mesh.texVertices = {{0, 0}, {8, 0}, {4, 8}};
    sed::assets::Model3doFace face;
    face.geo = 4;
    face.vertices = {0, 1, 2};
    face.texVertices = {0, 1, 2};
    mesh.faces = {face};
    model.meshes = {mesh};
    sed::assets::Model3doNode node;
    node.name = "triangle";
    node.mesh = 0;
    model.nodes = {node};
    const auto mat = [](std::uint8_t r, std::uint8_t g)
    {
        sed::assets::RgbaImage image{8, 8, {}};
        for ( int i = 0; i < 64; ++i )
            image.rgba.insert(image.rgba.end(), {r, g, 0, 255});
        return sed::assets::buildRgb565Mat({image}).bytes;
    };
    const auto archive = root / "assets.gob";
    const auto gob = sed::assets::buildGobBytes({{"3do\\triangle.3do", sed::assets::saveTo3DO(model)},
                                                 {"mat\\red.mat", mat(255, 0)},
                                                 {"3do\\mat\\green.mat", mat(0, 255)}});
    write(archive, gob);
    Json request = sed_agent::parseJson(
        R"({"width":64,"height":64,"projection":"orthographic","orthographicHeight":4,"camera":{"position":[0,0,5],"target":[0,0,0]},"fullbright":true})");
    Json archives = Json::makeArray();
    Json binding = Json::makeObject();
    binding.set("path", Json::makeString(archive.string()));
    binding.set("sha256", Json::makeString(sed_agent::sha256Hex(gob)));
    archives.array.push_back(binding);
    request.set("resourceArchives", archives);
    const auto legacy = run(root, source, request, "snapshot", "legacy.png");
    check(field(legacy, "renderScope").str == "world-surfaces" && !legacy.find("thingPreview"),
          "legacy receipt stays world-only");
    request.set("thingMode", Json::makeString("none"));
    (void)run(root, source, request, "snapshot", "none.png");
    check(sed_agent::readFileBytes(root / "legacy.png") == sed_agent::readFileBytes(root / "none.png"),
          "explicit none equals legacy bytes");
    request.set("thingMode", Json::makeString("static"));
    const auto preview = run(root, source, request, "snapshot", "static.png");
    check(field(preview, "renderScope").str == "world-surfaces-and-static-things",
          "static scope explicitly changed");
    const auto *report = preview.find("thingPreview");
    check(report && field(*report, "modelsPrepared").type == Json::Type::Number &&
              field(*report, "modelsPrepared").number == 1,
          "real model prepared and reported");
    const auto png = sed_agent::readFileBytes(root / "static.png");
    const auto image = sed::assets::decodePng(std::as_bytes(std::span(png.data(), png.size())));
    const auto center = (32u * 64u + 32u) * 4u;
    check(image.rgba[center] == 0 && image.rgba[center + 1] == 255,
          "static model really covers red floor with green texel");
    (void)run(root, source, request, "snapshot", "repeat.png");
    check(png == sed_agent::readFileBytes(root / "repeat.png"), "static rendering deterministic");
    Json overview = sed_agent::parseJson(
        R"({"view":"overhead","width":64,"height":64,"fullbright":true,"thingMode":"static"})");
    overview.set("resourceArchives", archives);
    const auto framed = run(root, source, overview, "overview", "overview.png");
    const auto replay = field(framed, "snapshotRequest");
    check(replay.find("thingMode") && replay.find("resourceArchives"),
          "overview replay retains static mode and captured resource bindings");
    (void)run(root, source, replay, "snapshot", "replayed.png");
    check(sed_agent::readFileBytes(root / "overview.png") == sed_agent::readFileBytes(root / "replayed.png"),
          "overview replay exact pixels");
    for ( const char *value : {"true", "null", "42", "\"model\""} )
    {
        request.set("thingMode", sed_agent::parseJson(value));
        bool refused = false;
        try
        {
            (void)run(root, source, request, "snapshot", "invalid.png");
        }
        catch ( const sed_agent::AgentError &e )
        {
            refused = e.code == 2;
        }
        check(refused && !fs::exists(root / "invalid.png"), "bad Thing mode refuses atomically");
    }
    request.set("thingMode", Json::makeString("static"));
    std::erase_if(request.object, [](const auto &entry) { return entry.first == "resourceArchives"; });
    const auto missing = run(root, source, request, "snapshot", "missing.png");
    const auto *missingReport = missing.find("thingPreview");
    check(missingReport && field(*missingReport, "diagnostics").array.size() == 1 &&
              field(field(*missingReport, "diagnostics").array[0], "status").str == "missing-resource",
          "missing model reports skip, no marker");
    request.set("resourceArchives", archives);
    // Rendering a supplied malformed model must report omission, not an icon.
    const auto badArchive = root / "bad.gob";
    const auto badGob = sed::assets::buildGobBytes({{"3do\\triangle.3do", "not a model"}});
    write(badArchive, badGob);
    Json badBinding = Json::makeObject();
    badBinding.set("path", Json::makeString(badArchive.string()));
    badBinding.set("sha256", Json::makeString(sed_agent::sha256Hex(badGob)));
    Json badArchives = Json::makeArray();
    badArchives.array.push_back(badBinding);
    request.set("resourceArchives", badArchives);
    const auto invalid = run(root, source, request, "snapshot", "malformed.png");
    const auto &invalidRows = field(field(invalid, "thingPreview"), "diagnostics");
    check(invalidRows.type == Json::Type::Array && invalidRows.array.size() == 1 &&
              field(invalidRows.array[0], "status").str == "invalid-resource",
          "malformed model is explicitly skipped");
    request.set("resourceArchives", archives);
    level.things.resize(4097, thing);
    const auto large = root / "many.sed";
    write(large, bytes(sed::assets::saveToSedBytes(level)));
    bool budgetRefused = false;
    try
    {
        (void)run(root, large, request, "snapshot", "budget.png");
    }
    catch ( const sed_agent::AgentError &e )
    {
        budgetRefused = e.code == 3;
    }
    check(budgetRefused && !fs::exists(root / "budget.png"),
          "aggregate Thing budget refusal publishes no PNG");
    check(sed_agent::readFileBytes(source) == before && sed_agent::readFileBytes(archive) == gob,
          "source and archive unchanged");
}
} // namespace
int main()
{
    const auto root = fs::temp_directory_path() /
                      ("red-snapshot-things-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try
    {
        tests(root);
    }
    catch ( const sed_agent::AgentError &e )
    {
        ++failures;
        std::cerr << "Agent error " << e.code << ": " << e.message << '\n';
    }
    catch ( const std::exception &e )
    {
        ++failures;
        std::cerr << e.what() << '\n';
    }
    fs::remove_all(root);
    return failures ? 1 : 0;
}