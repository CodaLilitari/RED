#include "../agent_workspace.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/prefab.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures = 0;
int requestNumber = 0;

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
    if ( !output )
    {
        throw std::runtime_error("cannot write synthetic request/fixture");
    }
}

fs::path request(const fs::path& root, const std::string& operations)
{
    const auto path = root / ("request-" + std::to_string(++requestNumber) + ".json");
    writeText(path, "{\"schemaVersion\":1,\"operations\":[" + operations + "]}");
    return path;
}

void apply(const fs::path& root, const fs::path& source, const fs::path& output,
    const std::string& operations)
{
    const auto patch = request(root, operations);
    const std::string before = sed_agent::readFileBytes(source);
    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto* hash = planned.find("outputSha256");
    check(hash && hash->type == sed_agent::Json::Type::String, "plan provides output hash");
    check(!fs::exists(output), "plan publishes no level output");
    if ( !hash )
    {
        return;
    }
    (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string(), hash->str });
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(output)) == hash->str,
        "applied output matches reviewed bytes");
    check(sed_agent::readFileBytes(source) == before, "editing leaves source bytes unchanged");
}

int face(const sed::Sector& sector, const double xNormal)
{
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        if ( sector.surfaces[i].normal.x * xNormal > 0.99 )
        {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("synthetic room has no requested X face");
}

void expectRefusal(const fs::path& root, const fs::path& source,
    const std::string& operations, const std::string& code)
{
    const auto patch = request(root, operations);
    const auto output = root / ("refused-" + std::to_string(requestNumber) + source.extension().string());
    const std::string before = sed_agent::readFileBytes(source);
    try
    {
        (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string() });
        check(false, "operation must refuse with " + code);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == code, "expected " + code + ", received " + error.errorCode
            + ": " + error.message);
    }
    check(!fs::exists(output), "refusal publishes no output");
    check(sed_agent::readFileBytes(source) == before, "refusal leaves source unchanged");
}

void connectedRooms(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    const int sourceFace = face(original.sectors[0], -1.0);
    const auto twoRooms = root / "two-rooms.jkl";
    apply(root, starter, twoRooms, "{\"op\":\"extrudeSurface\",\"sector\":0,\"surface\":"
        + std::to_string(sourceFace) + ",\"distance\":2}");
    auto two = sed::assets::loadFromJKL(twoRooms);
    check(two.sectors.size() == 2, "extrusion appends one room");
    check(two.sectors[0].surfaces[sourceFace].adjoin.has_value(), "extrusion connects source");
    const auto threeRooms = root / "three-rooms.jkl";
    apply(root, twoRooms, threeRooms, R"({"op":"addBox","center":[4,0,0],"halfSize":[1,1,1]})");
    auto three = sed::assets::loadFromJKL(threeRooms);
    check(three.sectors.size() == 3, "box appends third room");
    const int joinFrom = face(three.sectors[1], -1.0);
    const int joinTo = face(three.sectors[2], 1.0);
    const auto finished = root / "connected-material-lit.jkl";
    int wall = 0;
    while ( three.sectors[0].surfaces[wall].adjoin )
    {
        ++wall;
    }
    const int vertex = three.sectors[0].surfaces[wall].vertices[0];
    const std::string target = ",\"sector\":0,\"surface\":" + std::to_string(wall);
    const std::string vertexTarget = ",\"sector\":0,\"vertex\":" + std::to_string(vertex);
    const std::string operations = "{\"op\":\"adjoinSurfaces\",\"sector\":1,\"surface\":"
        + std::to_string(joinFrom) + ",\"otherSector\":2,\"otherSurface\":" + std::to_string(joinTo) + "},"
        + R"({"op":"registerMaterial","name":"review-wall.mat"},{"op":"registerColormap","name":"review-room.cmp"},)"
        + "{\"op\":\"setSurfaceMaterial\"" + target + ",\"materialIndex\":1},"
        + R"({"op":"setSectorColormap","sector":0,"colormapIndex":1},)"
        + "{\"op\":\"setSurfaceRenderModes\"" + target + ",\"geometryMode\":3,\"lightMode\":3,\"textureMode\":1},"
        + "{\"op\":\"setVertexLight\"" + vertexTarget + ",\"channel\":\"mono\",\"value\":0.125},"
        + "{\"op\":\"setVertexLight\"" + vertexTarget + ",\"channel\":\"red\",\"value\":0.75},"
        + "{\"op\":\"setVertexLight\"" + vertexTarget + ",\"channel\":\"green\",\"value\":0.5},"
        + "{\"op\":\"setVertexLight\"" + vertexTarget + ",\"channel\":\"blue\",\"value\":0.25},"
        + "{\"op\":\"setSurfaceUv\"" + target + ",\"uv\":[[-16,0],[64,0],[64,96],[-16,96]]}";
    apply(root, threeRooms, finished, operations);
    const auto result = sed::assets::loadFromJKL(finished);
    check(result.sectors.size() == 3, "three connected rooms reopen");
    int portals = 0;
    for ( std::size_t s = 0; s < result.sectors.size(); ++s )
    {
        const auto& sector = result.sectors[s];
        check(sed::level::sectorIsConvex(sector), "each reopened room is convex");
        for ( std::size_t f = 0; f < sector.surfaces.size(); ++f )
        {
            const auto& surface = sector.surfaces[f];
            if ( surface.adjoin )
            {
                ++portals;
                const auto& peer = result.sectors[surface.adjoin->sector].surfaces[surface.adjoin->surface];
                check(peer.adjoin == sed::SurfaceRef{ static_cast<int>(s), static_cast<int>(f) },
                    "every portal is reciprocal");
                check(surface.geo == 0, "joined surface is not drawn");
            }
        }
    }
    check(portals == 4, "three-room route has exactly two reciprocal connections");
    check(result.materials.size() == 2 && result.materials[0] == original.materials[0]
            && result.materials[1] == "review-wall.mat", "registration preserves ordered material identity");
    check(result.colormaps.size() == 2 && result.colormaps[0] == original.colormaps[0]
            && result.sectors[0].colormapIndex == 1, "colormap identity survives reopen");
    const auto& wallResult = result.sectors[0].surfaces[wall];
    check(wallResult.material == "review-wall.mat" && wallResult.nMat == 1, "material assignment survives reopen");
    check(wallResult.geo == 3 && wallResult.light == 3 && wallResult.tex == 1, "render modes survive reopen");
    check(wallResult.texVertices[0].u == -16 && wallResult.texVertices[2].v == 96, "UVs retain signed texel coordinates");
    int affectedCorners = 0;
    for ( std::size_t f = 0; f < result.sectors[0].surfaces.size(); ++f )
    {
        const auto& surface = result.sectors[0].surfaces[f];
        for ( std::size_t c = 0; c < surface.vertices.size(); ++c )
        {
            const auto color = surface.texVertices[c].color;
            if ( surface.vertices[c] == vertex )
            {
                ++affectedCorners;
                check(color == sed::ColorF{ 0.75f, 0.5f, 0.25f, 0.125f },
                    "shared vertex corners preserve independent mono/RGB after UV edit");
            }
            else
            {
                check(color == three.sectors[0].surfaces[f].texVertices[c].color,
                    "unselected vertex light is unchanged");
            }
        }
    }
    check(affectedCorners == 3, "cube vertex edits every incident corner");
    expectRefusal(root, finished, "{\"op\":\"extrudeSurface\",\"sector\":1,\"surface\":"
        + std::to_string(joinFrom) + ",\"distance\":2}", "basicGeometryUnsupported");
}

void remainingBoundaries(const fs::path& root, const fs::path& starter)
{
    auto level = sed::assets::loadFromJKL(starter);
    auto& cog = level.newCog();
    cog.name = "review_binding.cog";
    sed::CogValue parameter{};
    parameter.name = "param0";
    parameter.type = sed::ValueType::str;
    parameter.stringValue = "4";
    cog.values.push_back(parameter);
    const auto source = root / "raw-cog-source.jkl";
    writeText(source, sed::assets::buildJklExport(level).text);
    const auto before = sed::assets::loadFromJKL(source);
    check(before.hasPendingJklCogValues(), "COG fixture actually has an unresolved imported binding");
    const auto output = root / "raw-cog-box.jkl";
    apply(root, source, output, R"({"op":"addBox","center":[4,0,0],"halfSize":1})");
    const auto result = sed::assets::loadFromJKL(output);
    check(result.sectors.size() == 2, "legacy scalar halfSize appends a cube");
    check(result.cogs.size() == 1 && result.cogs[0].name == before.cogs[0].name
            && result.cogs[0].values[0].asString() == "4", "geometry append preserves raw COG arguments");
    check(result.importedJklSurfaceRefs[4] == before.importedJklSurfaceRefs[4],
        "retained COG surface slot still addresses the same existing face");
    const auto& oldFace = before.sectors[0].surfaces[4];
    const auto& keptFace = result.sectors[0].surfaces[4];
    check(oldFace.vertices == keptFace.vertices, "existing face corner identity is unchanged after append");
    expectRefusal(root, output, "{\"op\":\"adjoinSurfaces\",\"sector\":0,\"surface\":"
        + std::to_string(face(result.sectors[0], -1.0)) + ",\"otherSector\":1,\"otherSurface\":"
        + std::to_string(face(result.sectors[1], 1.0)) + "}", "basicGeometryUnsupported");

    auto malformed = sed::assets::loadFromJKL(starter);
    malformed.sectors[0].surfaces.pop_back();
    const auto openRoom = root / "open-room.jkl";
    writeText(openRoom, sed::assets::buildJklExport(malformed).text);
    expectRefusal(root, openRoom,
        R"({"op":"extrudeSurface","sector":0,"surface":0,"distance":1})", "basicGeometryUnsupported");

    auto normalized = sed::assets::loadFromJKL(starter);
    normalized.uvCoordinateSpace = sed::UvCoordinateSpace::Normalized;
    const auto native = root / "normalized-uv.sed";
    const auto bytes = sed::assets::saveToSedBytes(normalized);
    std::ofstream stream(native, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    expectRefusal(root, native,
        R"({"op":"setSurfaceUv","sector":0,"surface":0,"uv":[[0,0],[64,0],[64,64],[0,64]]})",
        "basicUvCoordinateSpaceUnsupported");
}

void futureGeometryReferences(const fs::path& root, const fs::path& starter)
{
    struct Scenario
    {
        const char* name;
        sed::CogType cogType;
        sed::ValueType valueType;
        std::int32_t value;
        const char* spelling;
        bool textContainer;
        bool refuse;
    };
    const Scenario scenarios[] = {
        { "raw-future-surface", sed::CogType::unk, sed::ValueType::str, 6, "6", true, true },
        { "raw-future-sector", sed::CogType::unk, sed::ValueType::str, 1, "1", true, true },
        { "typed-future-surface", sed::CogType::surface, sed::ValueType::ptr, 6, "6", false, true },
        { "typed-future-sector", sed::CogType::sec, sed::ValueType::ptr, 1, "1", false, true },
        { "typed-scalar-six", sed::CogType::int_, sed::ValueType::integer, 6, "6", false, false },
        { "typed-existing-surface-one", sed::CogType::surface, sed::ValueType::ptr, 1, "1", false, false },
        { "typed-still-invalid-sector-six", sed::CogType::sec, sed::ValueType::ptr, 6, "6", false, false },
        { "typed-negative-surface", sed::CogType::surface, sed::ValueType::ptr, -1, "-1", false, false },
        { "raw-negative-sentinel", sed::CogType::unk, sed::ValueType::str, -1, "-1", true, false },
        { "typed-future-surface-pair", sed::CogType::surface, sed::ValueType::ptr, -1, "1 0", false, true },
        { "typed-stable-surface-pair", sed::CogType::surface, sed::ValueType::ptr, 6, "0 1", false, false }
    };
    for ( const auto& scenario : scenarios )
    {
        auto level = sed::assets::loadFromJKL(starter);
        auto& cog = level.newCog();
        cog.name = "review_future.cog";
        sed::CogValue value{};
        value.name = scenario.textContainer ? "param0" : "target";
        value.type = scenario.valueType;
        value.cogType = scenario.cogType;
        value.intValue = scenario.value;
        value.stringValue = scenario.spelling;
        cog.values.push_back(value);
        const std::string extension = scenario.textContainer ? ".jkl" : ".sed";
        const auto source = root / (std::string(scenario.name) + extension);
        if ( scenario.textContainer )
        {
            writeText(source, sed::assets::buildJklExport(level).text);
        }
        else
        {
            const auto bytes = sed::assets::saveToSedBytes(level);
            std::ofstream stream(source, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        const std::string box = R"({"op":"addBox","center":[4,0,0],"halfSize":1})";
        if ( scenario.refuse )
        {
            expectRefusal(root, source, box, "basicGeometryReferenceCapture");
            if ( std::string(scenario.name) == "raw-future-surface" )
            {
                expectRefusal(root, source, "{\"op\":\"extrudeSurface\",\"sector\":0,\"surface\":"
                    + std::to_string(face(level.sectors[0], -1.0)) + ",\"distance\":2}",
                    "basicGeometryReferenceCapture");
                expectRefusal(root, source,
                    R"({"op":"buildSectorArray","sectors":[0],"layout":{"kind":"linear","origin":[10,0,0],"step":[3,0,0],"count":1}})",
                    "basicGeometryReferenceCapture");
            }
        }
        else
        {
            // Typed references use their own index space; known scalars and
            // negative sentinels do not acquire geometry-reference semantics.
            const auto output = root / (std::string(scenario.name) + "-box" + extension);
            apply(root, source, output, box);
            sed::Level result;
            if ( scenario.textContainer )
            {
                result = sed::assets::loadFromJKL(output);
            }
            else
            {
                const auto encoded = sed_agent::readFileBytes(output);
                std::vector<std::byte> buffer(encoded.size());
                std::transform(encoded.begin(), encoded.end(), buffer.begin(),
                    [](const char c) { return static_cast<std::byte>(c); });
                result = sed::assets::loadSedFileBytes(buffer).level;
            }
            const auto& retained = result.cogs[0].values[0];
            check(retained.cogType == scenario.cogType && retained.type == scenario.valueType
                    && retained.asString() == value.asString(),
                std::string(scenario.name) + " retains its original declaration and value");
            if ( !scenario.textContainer )
            {
                check(retained.stringValue == value.stringValue,
                    std::string(scenario.name) + " retains preferred native reference text");
            }
        }
    }
}

void opaqueGeometryRecipes(const fs::path& root, const fs::path& starter)
{
    const auto clean = sed::assets::loadFromJKL(starter);
    check(clean.cogs.empty(), "opaque recipe fixture has no COGs to trigger the independent COG guard");
    const auto source = root / "opaque-recipe-source.jkl";
    writeText(source, sed_agent::readFileBytes(starter)
        + "\nSection: FUTURE_GEOMETRY\nsector_target 1\nsurface_target 6\nend\n");
    const std::int32_t selected[] = { 0 };
    const auto prefab = sed::level::makePrefab(clean, selected, 0);
    if ( !prefab )
    {
        throw std::runtime_error("cannot create the synthetic recipe prefab");
    }
    const auto prefabText = sed::level::savePrefabText(*prefab);
    if ( !prefabText )
    {
        throw std::runtime_error("cannot encode the synthetic recipe prefab");
    }
    const auto prefabPath = root / "opaque-recipe-prefab.txt";
    writeText(prefabPath, *prefabText);
    const std::string operations[] = {
        R"({"op":"buildSweptCorridor","center":[10,0,0],"innerRadius":1,"outerRadius":2,"startAngleDegrees":0,"sweepDegrees":90,"segments":3,"risePerSegment":0,"headroom":1})",
        R"({"op":"buildDome","center":[10,0,0],"radius":1,"headroom":1,"segments":4,"rings":2})",
        R"({"op":"buildSectorArray","sectors":[0],"layout":{"kind":"linear","origin":[10,0,0],"step":[3,0,0],"count":1}})",
        R"({"op":"buildSpiralStairs","center":[10,0,0],"innerRadius":1,"outerRadius":2,"startAngleDegrees":0,"stepAngleDegrees":30,"rise":0.25,"headroom":1,"steps":3})",
        R"({"op":"buildHeightmapTerrain","origin":[10,0,0],"cellSize":1,"headroom":2,"columns":1,"rows":1,"heights":[0]})",
        "{\"op\":\"insertPrefab\",\"prefabFile\":"
            + sed_agent::toJsonString(sed_agent::Json::makeString(prefabPath.string())) + ",\"target\":[10,0,0]}"
    };
    int recipe = 0;
    for ( const auto& operation : operations )
    {
        // The same valid recipe succeeds without opaque dependencies, so the
        // refusal proves source admission rather than malformed parameters.
        const auto output = root / ("clean-recipe-" + std::to_string(recipe++) + ".jkl");
        apply(root, starter, output, operation);
        expectRefusal(root, source, operation, "basicGeometryUnknownState");
    }
}

void translateSectors(const fs::path& root, const fs::path& starter)
{
    const auto twoRooms = root / "translate-two-rooms.jkl";
    apply(root, starter, twoRooms, R"({"op":"addBox","center":[4,0,0],"halfSize":[1,1,1]})");
    const auto before = sed::assets::loadFromJKL(twoRooms);
    check(before.sectors.size() == 2, "translate fixture has two rooms");
    const auto moved = root / "translate-moved.jkl";
    apply(root, twoRooms, moved, R"({"op":"translateSectors","sectors":[1],"delta":[10,20,30]})");
    const auto after = sed::assets::loadFromJKL(moved);
    check(after.sectors.size() == 2, "translation keeps the sector count");
    bool still = after.sectors[0].vertices.size() == before.sectors[0].vertices.size();
    for ( std::size_t i = 0; still && i < before.sectors[0].vertices.size(); ++i )
    {
        const auto& a = after.sectors[0].vertices[i];
        const auto& b = before.sectors[0].vertices[i];
        still = a.x == b.x && a.y == b.y && a.z == b.z;
    }
    check(still, "unlisted sector vertices are unchanged by translation");
    bool shifted = after.sectors[1].vertices.size() == before.sectors[1].vertices.size();
    for ( std::size_t i = 0; shifted && i < before.sectors[1].vertices.size(); ++i )
    {
        const auto& a = after.sectors[1].vertices[i];
        const auto& b = before.sectors[1].vertices[i];
        shifted = a.x == b.x + 10.0 && a.y == b.y + 20.0 && a.z == b.z + 30.0;
    }
    check(shifted, "listed sector vertices move by exactly delta");
    const auto movedOnce = root / "translate-once.jkl";
    apply(root, twoRooms, movedOnce, R"({"op":"translateSectors","sectors":[1,1],"delta":[1,0,0]})");
    const auto once = sed::assets::loadFromJKL(movedOnce);
    check(once.sectors[1].vertices.front().x == before.sectors[1].vertices.front().x + 1.0,
        "duplicate sector entries move once");
    expectRefusal(root, twoRooms, R"({"op":"translateSectors","sectors":[],"delta":[1,0,0]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"translateSectors","sectors":[7],"delta":[1,0,0]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"translateSectors","sectors":[1],"delta":[1,0]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"translateSectors","sectors":[1],"delta":[1,0,0],"ignored":true})",
        "basicOperationInvalid");
}

void runTests(const fs::path& root)
{
    const auto starter = root / "starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    try
    {
        connectedRooms(root, starter);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "connected-room recipe: " + error.message);
    }
    expectRefusal(root, starter, R"({"op":"addBox","center":[0,0,0],"halfSize":[1,1,1]})", "basicGeometryOverlap");
    expectRefusal(root, starter, R"({"op":"addBox","center":[4,0,0],"halfSize":[1,0,1]})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"registerMaterial","name":"../bad.mat"})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"registerMaterial","name":"SCAFFOLD.MAT"})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"setSurfaceMaterial","sector":0,"surface":0,"materialIndex":99})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"setSurfaceUv","sector":0,"surface":0,"uv":[[0,0]]})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"setSurfaceRenderModes","sector":0,"surface":0,"geometryMode":5,"lightMode":3,"textureMode":1})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"setVertexLight","sector":0,"vertex":0,"channel":"alpha","value":0.5})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"registerColormap","name":"valid.cmp","ignored":true})", "basicOperationInvalid");
    expectRefusal(root, starter, R"({"op":"adjoinSurfaces","sector":0,"surface":0,"otherSector":0,"otherSurface":1})", "basicGeometryUnsupported");
    const auto unknown = root / "unknown.jkl";
    writeText(unknown, sed_agent::readFileBytes(starter) + "\nSection: FUTURE_GEOMETRY\nslots 0 1 2\nend\n");
    expectRefusal(root, unknown, R"({"op":"addBox","center":[4,0,0],"halfSize":[1,1,1]})", "basicGeometryUnknownState");
    try
    {
        translateSectors(root, starter);
        remainingBoundaries(root, starter);
        futureGeometryReferences(root, starter);
        opaqueGeometryRecipes(root, starter);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "remaining geometry boundaries: " + error.message);
    }
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-basic-operations-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        runTests(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected agent error: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "basic operation failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
