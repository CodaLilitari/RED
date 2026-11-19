#include "../agent_workspace.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/prefab.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void scaleAndRotateSectors(const fs::path& root, const fs::path& starter)
{
    const auto twoRooms = root / "transform-two-rooms.jkl";
    apply(root, starter, twoRooms, R"({"op":"addBox","center":[4,0,0],"halfSize":[1,1,1]})");
    const auto hasVertex = [](const sed::Sector& sector, const double x, const double y, const double z) {
        for ( const auto& v : sector.vertices )
        {
            if ( v.x == x && v.y == y && v.z == z )
            {
                return true;
            }
        }
        return false;
    };
    const auto scaled = root / "scale-moved.jkl";
    apply(root, twoRooms, scaled,
        R"({"op":"scaleSectors","sectors":[1],"pivot":[3,-1,-1],"factors":[2,3,4]})");
    const auto scaledLevel = sed::assets::loadFromJKL(scaled);
    check(hasVertex(scaledLevel.sectors[1], 3.0, -1.0, -1.0),
        "scale keeps the pivot corner in place");
    check(hasVertex(scaledLevel.sectors[1], 7.0, 5.0, 7.0),
        "per-axis factors land the far corner exactly");
    check(hasVertex(scaledLevel.sectors[0], -1.0, -1.0, -1.0)
            && hasVertex(scaledLevel.sectors[0], 1.0, 1.0, 1.0),
        "scale leaves the unlisted sector in place");
    const auto uniform = root / "scale-uniform.jkl";
    apply(root, twoRooms, uniform,
        R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0],"factor":2,"scaleTextures":false})");
    const auto uniformLevel = sed::assets::loadFromJKL(uniform);
    check(hasVertex(uniformLevel.sectors[1], 6.0, -2.0, -2.0)
            && hasVertex(uniformLevel.sectors[1], 10.0, 2.0, 2.0),
        "uniform factor scales about the pivot exactly");
    const auto rotated = root / "rotate-moved.jkl";
    apply(root, twoRooms, rotated,
        R"({"op":"rotateSectors","sectors":[1],"pivot":[0,0,0],"axis":[0,0,1],"angleDegrees":90})");
    const auto rotatedLevel = sed::assets::loadFromJKL(rotated);
    // Core rotation convention: +90 degrees about +Z maps +X towards -Y.
    check(hasVertex(rotatedLevel.sectors[1], 1.0, -5.0, 1.0)
            && hasVertex(rotatedLevel.sectors[1], -1.0, -3.0, -1.0),
        "rotation follows the core angle convention exactly");
    check(hasVertex(rotatedLevel.sectors[0], -1.0, -1.0, -1.0),
        "rotation leaves the unlisted sector in place");
    expectRefusal(root, twoRooms, R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0],"factor":0})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0],"factor":-2})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms,
        R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0],"factor":2,"factors":[1,1,1]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms, R"({"op":"scaleSectors","sectors":[1],"pivot":[0,0,0],"factors":[1,0,1]})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms,
        R"({"op":"rotateSectors","sectors":[1],"pivot":[0,0,0],"axis":[0,0,0],"angleDegrees":45})",
        "basicOperationInvalid");
    expectRefusal(root, twoRooms,
        R"({"op":"rotateSectors","sectors":[1],"pivot":[0,0,0],"axis":[0,0,1],"angleDegrees":40000})",
        "basicOperationInvalid");
}

void adjoinedMatesFollowTransforms(const fs::path& root, const fs::path& starter)
{
    // Extrude an adjoined second room off the starter's -X face, then move
    // only the new sector: the starter's adjoined mate face must follow
    // (core transform semantics), its far wall must not.
    const auto original = sed::assets::loadFromJKL(starter);
    const int sourceFace = face(original.sectors[0], -1.0);
    const auto joined = root / "adjoined-rooms.jkl";
    apply(root, starter, joined, "{\"op\":\"extrudeSurface\",\"sector\":0,\"surface\":"
        + std::to_string(sourceFace) + ",\"distance\":2}");
    const auto moved = root / "adjoined-moved.jkl";
    apply(root, joined, moved, R"({"op":"translateSectors","sectors":[1],"delta":[0,0,10]})");
    const auto after = sed::assets::loadFromJKL(moved);
    const auto hasVertex = [](const sed::Sector& sector, const double x, const double y, const double z) {
        for ( const auto& v : sector.vertices )
        {
            if ( v.x == x && v.y == y && v.z == z )
            {
                return true;
            }
        }
        return false;
    };
    // The extruded room spans x in [1, 3]; its mate face is the starter
    // wall at x = 1 (inward normal -X, as face() selects it).
    check(hasVertex(after.sectors[0], 1.0, -1.0, 9.0),
        "adjoined mate face corners follow the moved sector");
    check(!hasVertex(after.sectors[0], 1.0, -1.0, -1.0),
        "no stale mate corner remains at the old portal plane");
    check(hasVertex(after.sectors[0], -1.0, -1.0, -1.0),
        "unconnected starter corners stay in place");
    check(hasVertex(after.sectors[1], 3.0, -1.0, 9.0),
        "moved sector far corner translated exactly");
}

std::string exactNumber(double value);

double reportNumber(const sed_agent::Json& receipt, const char* name)
{
    const auto* reports = receipt.find("seamConformReports");
    if ( !reports || reports->type != sed_agent::Json::Type::Array || reports->array.size() != 1 )
    {
        return -1.0;
    }
    const auto* value = reports->array[0].find(name);
    return value && value->type == sed_agent::Json::Type::Number ? value->number : -1.0;
}

std::size_t seamIssues(const fs::path& level)
{
    const auto receipt = sed_agent::runCommand("validate", { level.string() });
    std::size_t count = 0;
    if ( const auto* issues = receipt.find("issues") )
    {
        for ( const auto& issue : issues->array )
        {
            const auto* code = issue.find("code");
            if ( code && code->str == "sealed-seam" )
            {
                ++count;
            }
        }
    }
    return count;
}

int faceWhere(const sed::Sector& sector, const double sed::Vertex::* axis, const double value)
{
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        bool all = true;
        for ( const auto index : sector.surfaces[i].vertices )
        {
            all = all && sector.vertices[static_cast<std::size_t>(index)].*axis == value;
        }
        if ( all )
        {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("synthetic room has no requested axis face");
}

// conformSeams exists so that the T-junctions validate reports do not need an
// outer validate/cleave loop: a small room built against half of the
// starter's wall leaves the starter's floor, ceiling and wall each missing a
// corner the small room has.
void conformSeams(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    double low = original.sectors[0].vertices[0].x;
    double high = low;
    for ( const auto& vertex : original.sectors[0].vertices )
    {
        low = std::min({ low, vertex.x, vertex.y, vertex.z });
        high = std::max({ high, vertex.x, vertex.y, vertex.z });
    }
    const double middle = (low + high) / 2.0;
    const auto number = [](const double value) { return exactNumber(value); };
    const std::string annex = "{\"op\":\"addBox\",\"min\":[" + number(high) + "," + number(low) + "," + number(low)
        + "],\"max\":[" + number(high + 1.0) + "," + number(middle) + "," + number(high) + "]}";
    const auto tee = root / "seam-tee.jkl";
    apply(root, starter, tee, annex);
    check(seamIssues(tee) == 3, "the annex leaves floor, ceiling and wall sealed seams for validate to report");

    const std::string conform = R"({"op":"conformSeams"})";
    const auto planned = sed_agent::runCommand("plan", { tee.string(), request(root, conform).string() });
    check(reportNumber(planned, "operation") == 0 && reportNumber(planned, "issuesBefore") == 3
        && reportNumber(planned, "issuesRemaining") == 0 && reportNumber(planned, "insertions") == 2
        && reportNumber(planned, "exactInsertions") == 2 && reportNumber(planned, "approximateInsertions") == 0
        && reportNumber(planned, "maxDeviation") == 0 && reportNumber(planned, "changedSectorCount") == 1
        && reportNumber(planned, "uncheckedSectors") == 0,
        "the plan receipt reports what conformSeams found, inserted and checked");
    const auto conformed = root / "seam-conformed.jkl";
    apply(root, tee, conformed, conform);
    check(seamIssues(conformed) == 0, "validate finds no sealed seam after conformSeams");
    const auto before = sed::assets::loadFromJKL(tee);
    const auto after = sed::assets::loadFromJKL(conformed);
    check(after.sectors.size() == before.sectors.size()
        && after.sectors[0].surfaces.size() == before.sectors[0].surfaces.size()
        && after.sectors[1].surfaces.size() == before.sectors[1].surfaces.size(),
        "conformSeams adds no sector and splits no surface");
    const int floor = faceWhere(after.sectors[0], &sed::Vertex::z, low);
    const int wall = faceWhere(after.sectors[0], &sed::Vertex::x, high);
    check(after.sectors[0].surfaces[floor].vertices.size() == 5
        && after.sectors[0].surfaces[wall].vertices.size() == 6
        && after.sectors[0].surfaces[floor].texVertices.size() == 5,
        "the starter's floor and wall hold the annex's corners, each with a texture corner");
    check(after.sectors[1].vertices.size() == before.sectors[1].vertices.size(),
        "the room that already had the corner is not edited");

    // A conformed room is still a room the agent can build on.
    const auto extended = root / "seam-extended.jkl";
    apply(root, conformed, extended, "{\"op\":\"extrudeSurface\",\"sector\":0,\"surface\":"
        + std::to_string(face(after.sectors[0], 1.0)) + ",\"distance\":2}");
    check(sed::assets::loadFromJKL(extended).sectors.size() == 3, "a conformed room still extrudes");

    // Running it again is a no-op with an honest receipt, not a second edit.
    const auto again = sed_agent::runCommand("plan", { conformed.string(), request(root, conform).string() });
    check(reportNumber(again, "issuesBefore") == 0 && reportNumber(again, "insertions") == 0,
        "conformSeams on a conformed level inserts nothing");

    // Refusals leave nothing behind and say which limit or rule stopped them.
    expectRefusal(root, tee, R"({"op":"conformSeams","maxInsertions":1})", "seamConformBudgetExceeded");
    expectRefusal(root, tee, R"({"op":"conformSeams","seamLimits":{"maxSurfacePairs":0}})",
        "seamConformInspectionIncomplete");
    expectRefusal(root, tee, R"({"op":"conformSeams","maxInsertions":0})", "basicOperationInvalid");
    expectRefusal(root, tee, R"({"op":"conformSeams","maxInsertions":1.5})", "basicOperationInvalid");
    expectRefusal(root, tee, R"({"op":"conformSeams","allowApproximate":1})", "basicOperationInvalid");
    expectRefusal(root, tee, R"({"op":"conformSeams","sector":0})", "basicOperationInvalid");
    expectRefusal(root, tee, R"({"op":"conformSeams","seamLimits":{"maxCorners":1}})", "");

    // Rotated rooms carry no certificate that the corner lies on the edge:
    // refused whole by default, conformed and labelled on request.
    const auto turned = root / "seam-turned.jkl";
    apply(root, tee,
        turned, R"({"op":"rotateSectors","sectors":[0,1],"pivot":[0,0,0],"axis":[0,0,1],"angleDegrees":30})");
    check(seamIssues(turned) == 3, "rotating both rooms keeps their sealed seams");
    expectRefusal(root, turned, conform, "seamConformUnresolved");
    const std::string relaxed = R"({"op":"conformSeams","allowApproximate":true})";
    const auto turnedPlan = sed_agent::runCommand("plan", { turned.string(), request(root, relaxed).string() });
    check(reportNumber(turnedPlan, "insertions") == 2 && reportNumber(turnedPlan, "approximateInsertions") == 2
        && reportNumber(turnedPlan, "issuesRemaining") == 0 && reportNumber(turnedPlan, "maxDeviation") >= 0
        && reportNumber(turnedPlan, "maxDeviation") < 0.002,
        "allowApproximate conforms rotated rooms and reports the deviation");
    const auto turnedOut = root / "seam-turned-conformed.jkl";
    apply(root, turned, turnedOut, relaxed);
    check(seamIssues(turnedOut) == 0, "validate finds no sealed seam after an approximate conform");

    // A cleave renumbers surfaces and is refused once COGs are placed.
    // conformSeams renumbers nothing, so the typed reference keeps its face.
    auto scripted = sed::assets::loadFromJKL(tee);
    auto& cog = scripted.newCog();
    cog.name = "review_seam.cog";
    sed::CogValue target{};
    target.name = "target";
    target.type = sed::ValueType::ptr;
    target.cogType = sed::CogType::surface;
    target.intValue = wall;
    target.stringValue = std::to_string(wall);
    cog.values.push_back(target);
    const auto scriptedSource = root / "seam-scripted.sed";
    {
        const auto bytes = sed::assets::saveToSedBytes(scripted);
        std::ofstream stream(scriptedSource, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    expectRefusal(root, scriptedSource, "{\"op\":\"cleaveSurface\",\"sector\":0,\"surface\":"
        + std::to_string(wall) + ",\"normal\":[0,1,0],\"point\":[" + number(high) + "," + number(middle) + ","
        + number(middle) + "]}", "basicCleaveCogBindings");
    const auto scriptedOut = root / "seam-scripted-conformed.sed";
    apply(root, scriptedSource, scriptedOut, conform);
    const auto encoded = sed_agent::readFileBytes(scriptedOut);
    std::vector<std::byte> buffer(encoded.size());
    std::transform(encoded.begin(), encoded.end(), buffer.begin(),
        [](const char c) { return static_cast<std::byte>(c); });
    const auto scriptedResult = sed::assets::loadSedFileBytes(buffer).level;
    check(scriptedResult.cogs.size() == 1 && scriptedResult.cogs[0].values.size() == 1
        && scriptedResult.cogs[0].values[0].intValue == wall
        && scriptedResult.sectors[0].surfaces[static_cast<std::size_t>(wall)].vertices.size() == 6,
        "a placed COG keeps its surface while that surface receives the corners");
}

// Shortest text that reads back as the same double, so a box built from it
// shares a wall plane exactly.
std::string exactNumber(const double value)
{
    char text[32]{};
    const auto end = std::to_chars(text, text + sizeof text, value).ptr;
    return std::string(text, end);
}

std::string boxOperation(const double x0, const double y0, const double z0,
    const double x1, const double y1, const double z1)
{
    return "{\"op\":\"addBox\",\"min\":[" + exactNumber(x0) + "," + exactNumber(y0) + "," + exactNumber(z0)
        + "],\"max\":[" + exactNumber(x1) + "," + exactNumber(y1) + "," + exactNumber(z1) + "]}";
}

std::string surfacePair(const char* op, const int sector, const int surface, const int otherSector,
    const int otherSurface)
{
    return std::string("{\"op\":\"") + op + "\",\"sector\":" + std::to_string(sector) + ",\"surface\":"
        + std::to_string(surface) + ",\"otherSector\":" + std::to_string(otherSector) + ",\"otherSurface\":"
        + std::to_string(otherSurface) + "}";
}

std::size_t adjoinedFaces(const sed::Sector& sector)
{
    return static_cast<std::size_t>(std::count_if(sector.surfaces.begin(), sector.surfaces.end(),
        [](const sed::Surface& surface) { return surface.adjoin.has_value(); }));
}

bool levelValid(const fs::path& level)
{
    const auto receipt = sed_agent::runCommand("validate", { level.string() });
    const auto* valid = receipt.find("valid");
    return valid && valid->type == sed_agent::Json::Type::Bool && valid->boolean;
}

// connectSurfaces is the editor's Connect for the agent: where two rooms'
// walls overlap, both are cut along each other's outline and the shared area
// becomes the portal. Before it, a doorway was four cleaves and an adjoin.
void connectSurfaces(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    double low = original.sectors[0].vertices[0].x;
    double high = low;
    for ( const auto& vertex : original.sectors[0].vertices )
    {
        low = std::min({ low, vertex.x, vertex.y, vertex.z });
        high = std::max({ high, vertex.x, vertex.y, vertex.z });
    }
    const double middle = (low + high) / 2.0;

    // Two walls that already match are the adjoinSurfaces case; Connect must
    // produce the same level, not a second way to spell a portal.
    const auto twins = root / "connect-twins.jkl";
    apply(root, starter, twins, boxOperation(high, low, low, high + 2.0, high, high));
    const auto twinLevel = sed::assets::loadFromJKL(twins);
    const int twinWall = faceWhere(twinLevel.sectors[0], &sed::Vertex::x, high);
    const int twinOther = faceWhere(twinLevel.sectors[1], &sed::Vertex::x, high);
    const auto adjoinPlan = sed_agent::runCommand("plan", { twins.string(),
        request(root, surfacePair("adjoinSurfaces", 0, twinWall, 1, twinOther)).string() });
    const auto connectPlan = sed_agent::runCommand("plan", { twins.string(),
        request(root, surfacePair("connectSurfaces", 0, twinWall, 1, twinOther)).string() });
    const auto* adjoinHash = adjoinPlan.find("outputSha256");
    const auto* connectHash = connectPlan.find("outputSha256");
    check(adjoinHash && connectHash && adjoinHash->str == connectHash->str,
        "connecting two matching walls writes the level adjoinSurfaces writes");

    // A small room against half of a larger wall: the larger wall is cut at
    // the small room's edge and only the shared half becomes the portal.
    const auto tee = root / "connect-tee.jkl";
    apply(root, starter, tee, boxOperation(high, low, low, high + 1.0, middle, high));
    const auto teeLevel = sed::assets::loadFromJKL(tee);
    const int wall = faceWhere(teeLevel.sectors[0], &sed::Vertex::x, high);
    const int annexWall = faceWhere(teeLevel.sectors[1], &sed::Vertex::x, high);
    expectRefusal(root, tee, surfacePair("adjoinSurfaces", 0, wall, 1, annexWall), "basicGeometryUnsupported");
    const std::string connect = surfacePair("connectSurfaces", 0, wall, 1, annexWall);
    const auto teePlan = sed_agent::runCommand("plan", { tee.string(), request(root, connect).string() });
    const auto* reports = teePlan.find("connectSurfacesReports");
    const sed_agent::Json* portals = reports && reports->array.size() == 1
        ? reports->array[0].find("portals") : nullptr;
    check(portals && portals->array.size() == 1 && reports->array[0].find("addedSurfaces")->number == 1
        && reports->array[0].find("otherAddedSurfaces")->number == 0
        && reports->array[0].find("operation")->number == 0,
        "the receipt names the one portal and the face the larger wall gained");
    const auto joined = root / "connect-tee-joined.jkl";
    apply(root, tee, joined, connect);
    const auto joinedLevel = sed::assets::loadFromJKL(joined);
    check(joinedLevel.sectors.size() == 2
        && joinedLevel.sectors[0].surfaces.size() == teeLevel.sectors[0].surfaces.size() + 1
        && joinedLevel.sectors[1].surfaces.size() == teeLevel.sectors[1].surfaces.size(),
        "the larger wall is split once and the smaller wall is used whole");
    check(adjoinedFaces(joinedLevel.sectors[0]) == 1 && adjoinedFaces(joinedLevel.sectors[1]) == 1,
        "exactly the shared area is adjoined");
    if ( portals && portals->array.size() == 1 )
    {
        const auto portal = static_cast<std::size_t>(portals->array[0].find("surface")->number);
        const auto otherPortal = static_cast<std::size_t>(portals->array[0].find("otherSurface")->number);
        check(portal < joinedLevel.sectors[0].surfaces.size()
            && joinedLevel.sectors[0].surfaces[portal].adjoin == sed::SurfaceRef{ 1, static_cast<std::int32_t>(otherPortal) }
            && joinedLevel.sectors[1].surfaces[otherPortal].adjoin == sed::SurfaceRef{ 0, static_cast<std::int32_t>(portal) },
            "the reported portal faces are the adjoined pair in the written level");
    }
    check(seamIssues(joined) == 0 && levelValid(joined),
        "the cut gives the floor and ceiling the new corners, so the joined level validates");

    // A doorway: a frame between two rooms, connected to each in one patch.
    const auto rooms = root / "connect-door-rooms.jkl";
    apply(root, starter, rooms, boxOperation(high + 0.5, low, low, high + 2.5, high, high) + ","
        + boxOperation(high, middle - 0.25, low, high + 0.5, middle + 0.25, low + 1.0));
    const auto roomsLevel = sed::assets::loadFromJKL(rooms);
    check(roomsLevel.sectors.size() == 3, "two rooms and a door frame are built");
    const int nearWall = faceWhere(roomsLevel.sectors[0], &sed::Vertex::x, high);
    const int farWall = faceWhere(roomsLevel.sectors[1], &sed::Vertex::x, high + 0.5);
    const int frameNear = faceWhere(roomsLevel.sectors[2], &sed::Vertex::x, high);
    const int frameFar = faceWhere(roomsLevel.sectors[2], &sed::Vertex::x, high + 0.5);
    const auto door = root / "connect-door.jkl";
    apply(root, rooms, door, surfacePair("connectSurfaces", 0, nearWall, 2, frameNear) + ","
        + surfacePair("connectSurfaces", 2, frameFar, 1, farWall));
    const auto doorLevel = sed::assets::loadFromJKL(door);
    check(adjoinedFaces(doorLevel.sectors[2]) == 2 && adjoinedFaces(doorLevel.sectors[0]) == 1
        && adjoinedFaces(doorLevel.sectors[1]) == 1 && doorLevel.sectors[2].surfaces.size() == 6,
        "the frame is a portal on both sides and is not cut itself");
    check(doorLevel.sectors[0].surfaces.size() > roomsLevel.sectors[0].surfaces.size() + 1
        && doorLevel.sectors[1].surfaces.size() > roomsLevel.sectors[1].surfaces.size() + 1,
        "each room's wall is cut around the doorway");
    check(seamIssues(door) == 0 && levelValid(door), "the doorway level validates without a conform pass");

    // Refusals publish nothing and say why.
    const int farSide = faceWhere(teeLevel.sectors[1], &sed::Vertex::x, high + 1.0);
    expectRefusal(root, tee, surfacePair("connectSurfaces", 0, wall, 1, farSide), "basicConnectNoOverlap");
    expectRefusal(root, tee, surfacePair("connectSurfaces", 0, wall, 0, wall), "basicGeometryUnsupported");
    expectRefusal(root, joined, surfacePair("connectSurfaces", 0,
        static_cast<int>(portals && portals->array.size() == 1 ? portals->array[0].find("surface")->number : 0),
        1, annexWall), "basicGeometryUnsupported");
    expectRefusal(root, tee, R"({"op":"connectSurfaces","sector":0,"surface":0,"otherSector":1})",
        "basicOperationInvalid");
    expectRefusal(root, tee, R"({"op":"connectSurfaces","sector":0,"surface":0,"otherSector":1,"otherSurface":0,"distance":1})",
        "basicOperationInvalid");

    // Cutting a face renumbers surfaces, so like the cleaves it is refused
    // once a COG is placed.
    auto scripted = sed::assets::loadFromJKL(tee);
    auto& cog = scripted.newCog();
    cog.name = "review_connect.cog";
    sed::CogValue target{};
    target.name = "target";
    target.type = sed::ValueType::ptr;
    target.cogType = sed::CogType::surface;
    target.intValue = 0;
    target.stringValue = "0";
    cog.values.push_back(target);
    const auto scriptedSource = root / "connect-scripted.sed";
    {
        const auto bytes = sed::assets::saveToSedBytes(scripted);
        std::ofstream stream(scriptedSource, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    expectRefusal(root, scriptedSource, connect, "basicCleaveCogBindings");
}

// deleteSectors is the carving half of cleave-and-delete: a room cut out of a
// larger volume (the slab under a bridge, a pillar) is removed, and each face
// that was a portal into it becomes a wall with the given material.
void carveSurfaceRect(const fs::path& root, const fs::path& starter)
{
    // A lamp face cut into the west wall: band first, so the floor keeps its corners.
    const auto original = sed::assets::loadFromJKL(starter);
    const auto& room = original.sectors[0];
    double lo[3] = { 1e9, 1e9, 1e9 }, hi[3] = { -1e9, -1e9, -1e9 };
    for ( const auto& v : room.vertices )
    {
        const double c[3] = { v.x, v.y, v.z };
        for ( int k = 0; k < 3; ++k ) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    const auto floorCorners = [](const sed::Sector& sector, const double z) {
        for ( const auto& s : sector.surfaces )
        {
            bool flat = !s.vertices.empty();
            for ( const auto vi : s.vertices ) flat = flat && std::abs(sector.vertices[static_cast<std::size_t>(vi)].z - z) < 1e-9;
            if ( flat ) return s.vertices.size();
        }
        return std::size_t{ 0 };
    };
    const double y0 = lo[1] + (hi[1] - lo[1]) * 0.25, y1 = lo[1] + (hi[1] - lo[1]) * 0.5;
    const double z0 = lo[2] + (hi[2] - lo[2]) * 0.4, z1 = lo[2] + (hi[2] - lo[2]) * 0.6;
    const auto num = [](const double v) { std::ostringstream s; s << std::setprecision(17) << v; return s.str(); };
    const std::string material = std::to_string(original.materials.size());
    const std::string carve = "{\"op\":\"carveSurfaceRect\",\"sector\":0,\"axis\":\"x\",\"at\":" + num(lo[0])
        + ",\"min\":[" + num(y0) + "," + num(z0) + "],\"max\":[" + num(y1) + "," + num(z1) + "],\"materialIndex\":" + material
        + ",\"uvSize\":[64,16],\"lightMode\":0}";
    const auto plan = sed_agent::runCommand("plan", { starter.string(),
        request(root, R"({"op":"registerMaterial","name":"lamp.mat"},)" + carve).string() });
    const auto* reports = plan.find("carveSurfaceRectReports");
    check(reports && reports->array.size() == 1 && reports->array[0].find("surfaces")
              && !reports->array[0].find("surfaces")->array.empty(),
        "carveSurfaceRect reports the pieces");
    const auto out = root / "carved.jkl";
    apply(root, starter, out, R"({"op":"registerMaterial","name":"lamp.mat"},)" + carve);
    check(levelValid(out), "the carved room validates");
    const auto carved = sed::assets::loadFromJKL(out);
    const auto& s0 = carved.sectors[0];
    bool inside = true, lit = true, uvSpan = true;
    double umin = 1e9, umax = -1e9, vmin = 1e9, vmax = -1e9;
    for ( const auto& piece : reports->array[0].find("surfaces")->array )
    {
        const auto& f = s0.surfaces[static_cast<std::size_t>(piece.number)];
        lit = lit && f.light == 0 && f.material == "lamp.mat";
        for ( std::size_t i = 0; i < f.vertices.size(); ++i )
        {
            const auto& v = s0.vertices[static_cast<std::size_t>(f.vertices[i])];
            inside = inside && std::abs(v.x - lo[0]) < 1e-6 && v.y > y0 - 1e-6 && v.y < y1 + 1e-6 && v.z > z0 - 1e-6 && v.z < z1 + 1e-6;
            umin = std::min(umin, static_cast<double>(f.texVertices[i].u)); umax = std::max(umax, static_cast<double>(f.texVertices[i].u));
            vmin = std::min(vmin, static_cast<double>(f.texVertices[i].v)); vmax = std::max(vmax, static_cast<double>(f.texVertices[i].v));
        }
    }
    uvSpan = std::abs(umin) < 1e-3 && std::abs(umax - 64) < 1e-3 && std::abs(vmin) < 1e-3 && std::abs(vmax - 16) < 1e-3;
    check(inside, "the pieces lie inside the rectangle on the wall plane");
    check(lit, "the pieces take the material and the light mode");
    check(uvSpan, "uvSize stretches the texture across the rectangle exactly");
    check(floorCorners(s0, lo[2]) == floorCorners(room, lo[2]),
        "the floor keeps its corners: the cuts across stay inside the lamp band");
    expectRefusal(root, starter, "{\"op\":\"carveSurfaceRect\",\"sector\":0,\"axis\":\"w\",\"at\":0,\"min\":[0,0],\"max\":[1,1]}",
        "basicOperationInvalid");
    expectRefusal(root, starter, "{\"op\":\"carveSurfaceRect\",\"sector\":0,\"axis\":\"x\",\"at\":" + num(lo[0] - 5)
        + ",\"min\":[" + num(y0) + "," + num(z0) + "],\"max\":[" + num(y1) + "," + num(z1) + "]}", "basicCarveEmpty");
    expectRefusal(root, starter, "{\"op\":\"carveSurfaceRect\",\"sector\":0,\"axis\":\"x\",\"at\":" + num(lo[0])
        + ",\"min\":[" + num(y1) + "," + num(z0) + "],\"max\":[" + num(y0) + "," + num(z1) + "]}", "basicOperationInvalid");
}

// mirrorFloor: the room copied upside down under a translucent, walk-on floor
// portal; the bake keeps the reflection lit like the room.
void mirrorFloor(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    double lo[3] = { 1e9, 1e9, 1e9 }, hi[3] = { -1e9, -1e9, -1e9 };
    for ( const auto& v : original.sectors[0].vertices )
    {
        const double c[3] = { v.x, v.y, v.z };
        for ( int k = 0; k < 3; ++k ) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    const int floor = faceWhere(original.sectors[0], &sed::Vertex::z, lo[2]);
    const int wall = faceWhere(original.sectors[0], &sed::Vertex::x, lo[0]);
    check(floor >= 0 && wall >= 0, "mirror floor: the starter room has a floor and a wall");
    if ( floor < 0 || wall < 0 ) return;
    const auto mirrorOp = [](const int sector, const int surface, const std::string& extra = "") {
        return "{\"op\":\"mirrorFloor\",\"sector\":" + std::to_string(sector) + ",\"surface\":" + std::to_string(surface)
            + extra + "}";
    };
    const auto plan = sed_agent::runCommand("plan", { starter.string(), request(root, mirrorOp(0, floor)).string() });
    const auto* reports = plan.find("mirrorFloorReports");
    const sed_agent::Json* report = reports && reports->array.size() == 1 ? &reports->array[0] : nullptr;
    check(report && report->find("mirrorSector")->number == 1 && report->find("mirrorSurface")->number == floor
            && report->find("reflectivity")->number == 0.5 && report->find("overlapsSectors")->array.empty(),
        "mirrorFloor reports the copy, the portal face and the default reflectivity");

    const auto mirrored = root / "mirrored.jkl";
    apply(root, starter, mirrored, mirrorOp(0, floor));
    check(levelValid(mirrored), "the mirrored level validates");
    const auto level = sed::assets::loadFromJKL(mirrored);
    check(level.sectors.size() == 2, "the copy is one new sector");
    if ( level.sectors.size() != 2 ) return;
    const auto& walkOn = level.sectors[0].surfaces[static_cast<std::size_t>(floor)];
    check(walkOn.adjoin == sed::SurfaceRef{ 1, floor } && walkOn.adjoinFlags == sed::SAF_Visible
            && (walkOn.faceFlags & 0x2) != 0 && (walkOn.surfaceFlags & 0x5) == 0x5 && walkOn.geo == 4,
        "the written floor is a visible, translucent portal that does not let anyone through");
    bool reflected = true;
    for ( std::size_t v = 0; v < level.sectors[0].vertices.size(); ++v )
    {
        reflected = reflected && std::abs(level.sectors[1].vertices[v].z - (2 * lo[2] - level.sectors[0].vertices[v].z)) < 1e-6;
    }
    check(reflected, "the copy is the room reflected across the floor plane");

    // The bake lights the room as if the floor were solid, then copies the
    // room's light into the reflection; the editor's light-blocking bit it
    // sets for the bake is not left in the level.
    const auto baked = root / "mirrored-baked.jkl";
    const std::string light = "{\"op\":\"addLight\",\"position\":[" + exactNumber((lo[0] + hi[0]) / 2) + ","
        + exactNumber((lo[1] + hi[1]) / 2) + "," + exactNumber(lo[2] + (hi[2] - lo[2]) * 0.8) + "],\"rgb\":[1,1,1],\"intensity\":1,\"range\":"
        + exactNumber((hi[0] - lo[0]) * 2) + "}";
    const auto bakePlan = sed_agent::runCommand("plan", { mirrored.string(),
        request(root, light + R"(,{"op":"bakeLighting","mirrorReflectivity":0.25})").string() });
    const auto* bakeReports = bakePlan.find("bakeLightingReports");
    check(bakeReports && bakeReports->array.size() == 1 && bakeReports->array[0].find("mirrorFloors")
            && bakeReports->array[0].find("mirrorFloors")->number == 1,
        "the bake reports the mirror it refreshed");
    apply(root, mirrored, baked, light + R"(,{"op":"bakeLighting","mirrorReflectivity":0.25})");
    const auto bakedLevel = sed::assets::loadFromJKL(baked);
    bool copied = bakedLevel.sectors.size() == 2;
    bool lit = false;
    for ( std::size_t s = 0; copied && s < bakedLevel.sectors[0].surfaces.size(); ++s )
    {
        const auto& a = bakedLevel.sectors[0].surfaces[s].texVertices;
        const auto& b = bakedLevel.sectors[1].surfaces[s].texVertices;
        copied = a.size() == b.size();
        for ( std::size_t k = 0; copied && k < a.size(); ++k )
        {
            lit = lit || a[k].color.r > 0.05f;
            copied = std::abs(b[k].color.r - a[a.size() - 1 - k].color.r * 0.25f) < 1e-4f;
        }
    }
    check(copied && lit, "the baked reflection carries the room's light at mirrorReflectivity, corner for corner");
    const auto& bakedFloor = bakedLevel.sectors[0].surfaces[static_cast<std::size_t>(floor)];
    check(bakedFloor.adjoinFlags == sed::SAF_Visible
            && bakedLevel.sectors[1].surfaces[static_cast<std::size_t>(floor)].adjoinFlags == sed::SAF_Visible,
        "the bake restores the portal's flags: no light-blocking bit reaches the level");
    // During the bake the floor stands solid: a light inside the reflection
    // (a mirrored lamp, say) does not light the room up through it.
    const auto underLit = root / "mirrored-under-lit.jkl";
    const std::string lightBelow = "{\"op\":\"addLight\",\"position\":[" + exactNumber((lo[0] + hi[0]) / 2) + ","
        + exactNumber((lo[1] + hi[1]) / 2) + "," + exactNumber(lo[2] - (hi[2] - lo[2]) * 0.2) + "],\"rgb\":[1,1,1],\"intensity\":1,\"range\":"
        + exactNumber((hi[0] - lo[0]) * 2) + "}";
    apply(root, mirrored, underLit, lightBelow + R"(,{"op":"bakeLighting"})");
    const auto underLevel = sed::assets::loadFromJKL(underLit);
    bool roomDark = !underLevel.sectors.empty();
    for ( const auto& surface : underLevel.sectors[0].surfaces )
    {
        for ( const auto& corner : surface.texVertices ) roomDark = roomDark && corner.color.r < 0.01f;
    }
    check(roomDark, "a light inside the reflection does not light the room through the floor during the bake");

    expectRefusal(root, starter, mirrorOp(0, wall), "mirrorFloorNotFloor");
    expectRefusal(root, mirrored, mirrorOp(0, floor), "mirrorFloorAdjoined");
    expectRefusal(root, starter, mirrorOp(0, floor, ",\"reflectivity\":2"), "basicOperationInvalid");
    expectRefusal(root, starter, mirrorOp(0, floor, ",\"allowOverlap\":1"), "basicOperationInvalid");
    // A room below the floor: refused unless asked for, and then reported.
    const auto below = root / "mirror-below.jkl";
    apply(root, starter, below, boxOperation(lo[0], lo[1], lo[2] - 0.5, hi[0], hi[1], lo[2] - 0.1));
    expectRefusal(root, below, mirrorOp(0, floor), "mirrorFloorOverlap");
    const auto overlapPlan = sed_agent::runCommand("plan", { below.string(),
        request(root, mirrorOp(0, floor, ",\"allowOverlap\":true")).string() });
    const auto* overlapReports = overlapPlan.find("mirrorFloorReports");
    check(overlapReports && overlapReports->array.size() == 1
            && overlapReports->array[0].find("overlapsSectors")->array.size() == 1
            && overlapReports->array[0].find("overlapsSectors")->array[0].number == 1,
        "allowOverlap builds the mirror and names the room it overlaps");
}

// The app's remaining Edit, Sector and Surface commands, as patch operations.
void parityOperations(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    double lo[3] = { 1e9, 1e9, 1e9 }, hi[3] = { -1e9, -1e9, -1e9 };
    for ( const auto& v : original.sectors[0].vertices )
    {
        const double c[3] = { v.x, v.y, v.z };
        for ( int k = 0; k < 3; ++k ) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
    const auto mid = [&](const int k) { return exactNumber((lo[k] + hi[k]) / 2); };
    // Two rooms joined by a portal, two lights and two Things.
    const auto rooms = root / "parity-rooms.jkl";
    apply(root, starter, rooms, boxOperation(hi[0], lo[1], lo[2], hi[0] + 1.0, hi[1], hi[2]));
    const auto roomsLevel = sed::assets::loadFromJKL(rooms);
    const int east = faceWhere(roomsLevel.sectors[0], &sed::Vertex::x, hi[0]);
    const int west = faceWhere(roomsLevel.sectors[1], &sed::Vertex::x, hi[0]);
    const std::string light = "{\"op\":\"addLight\",\"position\":[" + mid(0) + "," + mid(1) + "," + mid(2)
        + "],\"rgb\":[1,1,1],\"intensity\":1,\"range\":2}";
    const auto thing = [&](const std::string& name) {
        return "{\"op\":\"addThing\",\"sector\":0,\"template\":\"parity_crate\",\"name\":\"" + name + "\",\"position\":["
            + mid(0) + "," + mid(1) + "," + exactNumber(lo[2] + 0.1) + "]}";
    };
    const auto base = root / "parity-base.jkl";
    apply(root, rooms, base, surfacePair("adjoinSurfaces", 0, east, 1, west) + "," + light + "," + light
        + R"(,{"op":"defineTemplate","name":"parity_crate","parent":"none","values":{"type":"cog","collide":1}},)" + thing("crate_a") + "," + thing("crate_b")
        + R"(,{"op":"registerMaterial","name":"wall.mat"})");
    const auto baseLevel = sed::assets::loadFromJKL(base);
    check(baseLevel.lights.size() == original.lights.size() + 2 && baseLevel.things.size() == original.things.size() + 2,
        "parity fixture: two rooms, two more lights and Things");
    const std::string wall = std::to_string(baseLevel.materials.size() - 1);

    const auto edited = root / "parity-edited.jkl";
    const std::string ops = R"({"op":"deleteLights","lights":[0]},{"op":"deleteThings","things":[)"
        + std::to_string(baseLevel.things.size() - 1) + R"(]},)"
        R"({"op":"setSectorProperties","sectors":[0],"extraLight":0.25,"tint":[0.2,0.4,0.6],"sound":"drip.wav","soundVolume":0.5},)"
        R"({"op":"setSurfaceProperties","surfaces":[[0,0]],"extraLight":0.3},)"
        R"({"op":"placeSectorLights","sectors":[0,1],"intensity":0.5},)"
        R"({"op":"removeRedundantVertices","sectors":[0,1]})";
    const auto plan = sed_agent::runCommand("plan", { base.string(), request(root, ops).string() });
    const auto* reports = plan.find("editReports");
    check(reports && reports->array.size() == 6 && reports->array[4].find("lights")
            && reports->array[4].find("lights")->array.size() == 2,
        "each parity operation reports in editReports; placeSectorLights names the two new lights");
    apply(root, base, edited, ops);
    check(levelValid(edited), "the edited level validates");
    const auto level = sed::assets::loadFromJKL(edited);
    check(level.lights.size() == baseLevel.lights.size() - 1 + 2, "deleteLights removes one; placeSectorLights adds one per room");
    check(level.things.size() == baseLevel.things.size() - 1, "deleteThings removes the Thing");
    const auto& sector = level.sectors[0];
    check(std::abs(sector.extraLight.r - 0.25f) < 1e-4f && std::abs(sector.tint.g - 0.4f) < 1e-4f && sector.sound == "drip.wav"
            && std::abs(sector.soundVolume - 0.5) < 1e-4,
        "setSectorProperties writes extra light, tint and sound that survive the JKL");
    check(std::abs(level.sectors[0].surfaces[0].extraLight.r - 0.3f) < 1e-4f, "setSurfaceProperties writes the face's extra light");

    // Unadjoining a portal with no drawn material needs a wall material.
    const auto hidden = root / "parity-hidden.jkl";
    apply(root, base, hidden, "{\"op\":\"setSurfaceRenderModes\",\"sector\":0,\"surface\":" + std::to_string(east)
        + ",\"geometryMode\":0,\"lightMode\":0,\"textureMode\":0},{\"op\":\"setSurfaceRenderModes\",\"sector\":1,\"surface\":"
        + std::to_string(west) + ",\"geometryMode\":0,\"lightMode\":0,\"textureMode\":0}");
    const std::string unadjoin = "{\"op\":\"unadjoinSurfaces\",\"surfaces\":[[0," + std::to_string(east) + "]]";
    expectRefusal(root, hidden, unadjoin + "}", "parityUnadjoinNeedsMaterial");
    const auto split = root / "parity-split.jkl";
    apply(root, hidden, split, unadjoin + ",\"materialIndex\":" + wall + "}");
    const auto splitLevel = sed::assets::loadFromJKL(split);
    const auto& a = splitLevel.sectors[0].surfaces[static_cast<std::size_t>(east)];
    const auto& b = splitLevel.sectors[1].surfaces[static_cast<std::size_t>(west)];
    check(!a.adjoin && !b.adjoin && a.material == "wall.mat" && b.material == "wall.mat" && a.geo == 4 && b.geo == 4,
        "unadjoinSurfaces seals both sides as drawn walls");
    check(levelValid(split), "the unadjoined level validates");
    expectRefusal(root, split, unadjoin + ",\"materialIndex\":" + wall + "}", "parityNotAdjoined");

    // Water: the second room underwater, its portal a water surface.
    const auto wet = root / "parity-wet.jkl";
    apply(root, base, wet, R"({"op":"makeUnderwater","sectors":[1],"tint":[0.1,0.3,0.5]})");
    const auto wetLevel = sed::assets::loadFromJKL(wet);
    check((wetLevel.sectors[1].flags & 0x2) != 0 && (wetLevel.sectors[0].flags & 0x2) == 0
            && std::abs(wetLevel.sectors[1].tint.b - 0.5f) < 1e-4f,
        "makeUnderwater floods the named room and tints it");
    const auto& waterFace = wetLevel.sectors[0].surfaces[static_cast<std::size_t>(east)];
    check(waterFace.adjoin && (waterFace.faceFlags & 0x2) != 0, "the portal into the water becomes a translucent water surface");

    expectRefusal(root, base, R"({"op":"deleteLights","lights":[0,0]})", "parityOperationInvalid");
    expectRefusal(root, base, R"({"op":"setSectorProperties","sectors":[0]})", "parityOperationInvalid");
    expectRefusal(root, base, R"({"op":"setSectorProperties","sectors":[0],"sound":"../x.wav"})", "parityOperationInvalid");
    expectRefusal(root, base, R"({"op":"placeSectorLights","sectors":[0],"intensity":0})", "parityOperationInvalid");

    // Second batch: transforms, merges and repairs.
    {
        // duplicateSector appends a copy of a room, moved by the offset.
        const auto copied = root / "parity-duplicate.jkl";
        apply(root, base, copied, R"({"op":"duplicateSector","sector":1,"offset":[0,0,20]})");
        const auto copyLevel = sed::assets::loadFromJKL(copied);
        const auto& source = baseLevel.sectors[1];
        const auto& copy = copyLevel.sectors.back();
        check(copyLevel.sectors.size() == baseLevel.sectors.size() + 1 && copy.vertices.size() == source.vertices.size()
                && std::abs(copy.vertices[0].z - (source.vertices[0].z + 20.0)) < 1e-6
                && std::abs(copy.vertices[0].x - source.vertices[0].x) < 1e-6,
            "duplicateSector appends the room moved by its offset");
        check(levelValid(copied), "the level with a duplicated room validates");
    }
    {
        // flipSurfaces reverses a face's winding; twice restores it.
        const auto once = root / "parity-flip1.jkl";
        apply(root, base, once, R"({"op":"flipSurfaces","surfaces":[[1,0]]})");
        const auto onceLevel = sed::assets::loadFromJKL(once);
        check(onceLevel.sectors[1].surfaces[0].normal.Dot(baseLevel.sectors[1].surfaces[0].normal) < -0.99,
            "flipSurfaces turns the face around");
        const auto twice = root / "parity-flip2.jkl";
        apply(root, once, twice, R"({"op":"flipSurfaces","surfaces":[[1,0]]})");
        check(sed::assets::loadFromJKL(twice).sectors[1].surfaces[0].vertices == baseLevel.sectors[1].surfaces[0].vertices,
            "flipping twice restores the corner order");
    }
    {
        // rotateLights and mirrorLights move lights about a pivot or over a plane.
        const auto& l0 = baseLevel.lights[0].position;
        const auto moved = root / "parity-lights.jkl";
        apply(root, base, moved, "{\"op\":\"rotateLights\",\"lights\":[0],\"pivot\":[" + exactNumber(l0.x - 0.5) + ","
            + exactNumber(l0.y) + "," + exactNumber(l0.z) + "],\"angleDegrees\":90},{\"op\":\"mirrorLights\",\"lights\":[1],"
            "\"normal\":[1,0,0],\"point\":[" + exactNumber(hi[0]) + ",0,0]}");
        const auto movedLevel = sed::assets::loadFromJKL(moved);
        const auto& r = movedLevel.lights[0].position;
        const auto& m = movedLevel.lights[1].position;
        // the core's convention, as for rotateSectors: +90 degrees about +Z turns +X towards -Y
        check(std::abs(r.x - (l0.x - 0.5)) < 1e-6 && std::abs(r.y - (l0.y - 0.5)) < 1e-6 && std::abs(r.z - l0.z) < 1e-6,
            "rotateLights turns a light 90 degrees about the pivot");
        check(std::abs(m.x - (2 * hi[0] - baseLevel.lights[1].position.x)) < 1e-6, "mirrorLights reflects a light over the plane");
    }
    {
        // rotateThings moves a Thing about a pivot and keeps it in its room.
        const int crate = static_cast<int>(baseLevel.things.size()) - 2;
        const auto& t = baseLevel.things[static_cast<std::size_t>(crate)];
        const auto turned = root / "parity-things.jkl";
        apply(root, base, turned, "{\"op\":\"rotateThings\",\"things\":[" + std::to_string(crate) + "],\"pivot\":["
            + exactNumber(t.x + 0.1) + "," + exactNumber(t.y) + "," + exactNumber(t.z) + "],\"angleDegrees\":180}");
        const auto turnedLevel = sed::assets::loadFromJKL(turned);
        const auto& after = turnedLevel.things[static_cast<std::size_t>(crate)];
        check(std::abs(after.x - (t.x + 0.2)) < 1e-6 && after.sector == t.sector, "rotateThings moves the crate about the pivot");
        check(levelValid(turned), "the level with a rotated Thing validates");
        // A Thing sent out of the level is refused.
        expectRefusal(root, base, "{\"op\":\"rotateThings\",\"things\":[" + std::to_string(crate)
            + "],\"pivot\":[" + exactNumber(t.x + 50) + "," + exactNumber(t.y) + "," + exactNumber(t.z)
            + "],\"angleDegrees\":180}", "parityThingOutsideLevel");
    }
    {
        // mergeSectors joins the two rooms into one; their Things move with them.
        const auto merged = root / "parity-merged.jkl";
        apply(root, base, merged, R"({"op":"mergeSectors","sector":0,"otherSector":1})");
        const auto mergedLevel = sed::assets::loadFromJKL(merged);
        check(mergedLevel.sectors.size() == baseLevel.sectors.size() - 1, "mergeSectors leaves one room");
        check(levelValid(merged), "the merged level validates");
        expectRefusal(root, base, R"({"op":"mergeSectors","sector":0,"otherSector":0})", "parityOperationInvalid");
    }
    {
        // subdivideSurfaces fans a face around its centre; mergeSurfaces puts it back together.
        const auto fanned = root / "parity-subdivided.jkl";
        apply(root, base, fanned, R"({"op":"subdivideSurfaces","surfaces":[[1,0]]})");
        const auto fannedLevel = sed::assets::loadFromJKL(fanned);
        const std::size_t fannedFaces = fannedLevel.sectors[1].surfaces.size();
        check(fannedFaces > baseLevel.sectors[1].surfaces.size(), "subdivideSurfaces adds faces");
        check(levelValid(fanned), "the subdivided level validates");
        std::string all = "[";
        for ( std::size_t f = 0; f < fannedFaces; ++f ) all += (f ? ",[1," : "[1,") + std::to_string(f) + "]";
        all += "]";
        const auto rejoined = root / "parity-rejoined.jkl";
        apply(root, fanned, rejoined, "{\"op\":\"mergeSurfaces\",\"surfaces\":" + all + "}");
        check(sed::assets::loadFromJKL(rejoined).sectors[1].surfaces.size() < fannedFaces, "mergeSurfaces merges the fan");
        check(levelValid(rejoined), "the re-merged level validates");
    }
    {
        // shadowCuts splits the faces a light reaches; removeShadowCuts merges them back.
        const auto& room = baseLevel.sectors[1];
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for ( const auto& v : room.vertices ) { cx += v.x; cy += v.y; cz += v.z; }
        const double n = static_cast<double>(room.vertices.size());
        const auto lit = root / "parity-lit.jkl";
        apply(root, base, lit, "{\"op\":\"addLight\",\"position\":[" + std::to_string(cx / n) + "," + std::to_string(cy / n)
            + "," + std::to_string(cz / n) + "],\"rgb\":[1,1,1],\"intensity\":1,\"range\":20}");
        const auto cut = root / "parity-shadow-cut.jkl";
        apply(root, lit, cut, R"({"op":"shadowCuts","mode":"sizeLimit","maxEdge":0.25})");
        const std::size_t cutFaces = sed::assets::loadFromJKL(cut).sectors[1].surfaces.size();
        check(cutFaces > room.surfaces.size(), "shadowCuts (size limit) splits the faces the light reaches");
        check(levelValid(cut), "the shadow-cut level validates: no surface passes the 24-corner limit");
        const auto removed = root / "parity-shadow-removed.jkl";
        apply(root, cut, removed, R"({"op":"removeShadowCuts","sectors":[1]})");
        check(sed::assets::loadFromJKL(removed).sectors[1].surfaces.size() < cutFaces, "removeShadowCuts merges the pieces back");
        expectRefusal(root, lit, R"({"op":"shadowCuts","mode":"sizeLimit","maxEdge":0.01,"maxNewSurfaces":1})",
            "shadowCutsOverBudget");
    }
    {
        // planarizeSurfaces leaves flat faces alone; repairSurfaces reports what it fixed.
        const auto plan = sed_agent::runCommand("plan", { base.string(), request(root,
            R"({"op":"planarizeSurfaces","surfaces":[[0,0],[1,0]]},{"op":"repairSurfaces","surfaces":[[0,0],[1,0]]})").string() });
        const auto* reports = plan.find("editReports");
        check(reports && reports->array.size() == 2 && reports->array[0].find("changed")
                && !reports->array[0].find("changed")->boolean && reports->array[1].find("solidRepaired"),
            "planarizeSurfaces leaves flat faces alone and repairSurfaces reports its counts");
    }
    {
        // fitAndAdjoin joins the two coincident walls the unadjoin sealed; stitch refuses faces without a shared edge.
        const auto refit = root / "parity-refit.jkl";
        apply(root, split, refit, surfacePair("fitAndAdjoin", 0, east, 1, west));
        const auto refitLevel = sed::assets::loadFromJKL(refit);
        check(refitLevel.sectors[0].surfaces[static_cast<std::size_t>(east)].adjoin
                && refitLevel.sectors[1].surfaces[static_cast<std::size_t>(west)].adjoin,
            "fitAndAdjoin adjoins two coincident walls");
        check(levelValid(refit), "the refitted level validates");
        // an adjoined face insets only as a pair
        expectRefusal(root, base, "{\"op\":\"insetSurface\",\"sector\":0,\"surface\":" + std::to_string(east)
            + ",\"factor\":0.5}", "parityInsetRefused");
    }
    {
        // mirrorSectors reflects a room over a plane through its middle: a box maps onto itself.
        const auto mirrored = root / "parity-mirrored.jkl";
        apply(root, rooms, mirrored, "{\"op\":\"mirrorSectors\",\"sectors\":[1],\"normal\":[0,1,0],\"point\":[0,"
            + mid(1) + ",0]}");
        check(levelValid(mirrored), "a room mirrored over its own middle validates");
        expectRefusal(root, rooms, R"({"op":"mirrorSectors","sectors":[1],"normal":[0,0,0],"point":[0,0,0]})",
            "parityOperationInvalid");
    }
}

void deleteSectors(const fs::path& root, const fs::path& starter)
{
    const auto original = sed::assets::loadFromJKL(starter);
    double low = original.sectors[0].vertices[0].x;
    double high = low;
    for ( const auto& vertex : original.sectors[0].vertices )
    {
        low = std::min({ low, vertex.x, vertex.y, vertex.z });
        high = std::max({ high, vertex.x, vertex.y, vertex.z });
    }
    const auto row = root / "delete-row.jkl";
    apply(root, starter, row, boxOperation(high, low, low, high + 1.0, high, high) + ","
        + boxOperation(high + 1.0, low, low, high + 2.0, high, high));
    const auto rowLevel = sed::assets::loadFromJKL(row);
    const auto joined = root / "delete-row-joined.jkl";
    apply(root, row, joined,
        surfacePair("adjoinSurfaces", 0, faceWhere(rowLevel.sectors[0], &sed::Vertex::x, high),
            1, faceWhere(rowLevel.sectors[1], &sed::Vertex::x, high)) + ","
        + surfacePair("adjoinSurfaces", 1, faceWhere(rowLevel.sectors[1], &sed::Vertex::x, high + 1.0),
            2, faceWhere(rowLevel.sectors[2], &sed::Vertex::x, high + 1.0)));
    check(levelValid(joined), "three rooms in a row validate");
    // Portals as the game usually has them: not drawn. A second material
    // shows that the sealed walls take the one asked for.
    const auto portals = root / "delete-row-portals.jkl";
    {
        const auto joinedLevel = sed::assets::loadFromJKL(joined);
        const auto hidden = [&](const int sector, const double x)
        {
            return "{\"op\":\"setSurfaceRenderModes\",\"sector\":" + std::to_string(sector) + ",\"surface\":"
                + std::to_string(faceWhere(joinedLevel.sectors[static_cast<std::size_t>(sector)], &sed::Vertex::x, x))
                + ",\"geometryMode\":0,\"lightMode\":0,\"textureMode\":0}";
        };
        apply(root, joined, portals, R"({"op":"registerMaterial","name":"carved.mat"},)" + hidden(0, high) + ","
            + hidden(2, high + 1.0));
    }

    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[1]})", "basicDeleteNeedsMaterial");
    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[],"materialIndex":0})", "basicOperationInvalid");
    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[1,1],"materialIndex":0})", "basicOperationInvalid");
    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[3],"materialIndex":0})", "basicOperationInvalid");
    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[1],"materialIndex":9})", "basicOperationInvalid");
    expectRefusal(root, portals, R"({"op":"deleteSectors","sectors":[0,1,2],"materialIndex":0})", "basicOperationInvalid");

    const auto plan = sed_agent::runCommand("plan", { portals.string(),
        request(root, R"({"op":"deleteSectors","sectors":[1],"materialIndex":1})").string() });
    const auto* reports = plan.find("deleteSectorsReports");
    const sed_agent::Json* sealed = reports && reports->array.size() == 1 ? reports->array[0].find("sealedFaces") : nullptr;
    check(sealed && sealed->array.size() == 2, "the receipt names the two faces that were portals into the room");
    const auto carved = root / "delete-carved.jkl";
    apply(root, portals, carved, R"({"op":"deleteSectors","sectors":[1],"materialIndex":1})");
    const auto carvedLevel = sed::assets::loadFromJKL(carved);
    check(carvedLevel.sectors.size() == 2, "one room is deleted and two remain");
    check(adjoinedFaces(carvedLevel.sectors[0]) == 0 && adjoinedFaces(carvedLevel.sectors[1]) == 0,
        "no portal points at the deleted room or past it");
    if ( sealed && sealed->array.size() == 2 )
    {
        for ( const auto& face : sealed->array )
        {
            const auto sector = static_cast<std::size_t>(face.find("sector")->number);
            const auto surface = static_cast<std::size_t>(face.find("surface")->number);
            const auto& wall = carvedLevel.sectors.at(sector).surfaces.at(surface);
            check(wall.nMat == 1 && wall.material == "carved.mat" && wall.geo == 4 && wall.light == 3
                && (wall.surfaceFlags & 0x4) != 0 && !wall.adjoin,
                "a former portal becomes a textured wall with the given material");
        }
    }
    check(levelValid(carved), "the carved level validates");

    // A Thing standing in the room would lose its sector; it is refused.
    auto occupied = sed::assets::loadFromJKL(joined);
    sed::Thing thing{};
    thing.sector = 1;
    thing.templateName = "walkplayer";
    thing.x = high + 0.5;
    thing.y = (low + high) / 2.0;
    thing.z = low + 0.1;
    occupied.things.push_back(thing);
    const auto withThing = root / "delete-occupied.jkl";
    sed::assets::saveToJKL(occupied, withThing);
    expectRefusal(root, withThing, R"({"op":"deleteSectors","sectors":[1],"materialIndex":0})", "basicDeleteOccupied");
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
        scaleAndRotateSectors(root, starter);
        adjoinedMatesFollowTransforms(root, starter);
        remainingBoundaries(root, starter);
        futureGeometryReferences(root, starter);
        opaqueGeometryRecipes(root, starter);
        conformSeams(root, starter);
        connectSurfaces(root, starter);
        deleteSectors(root, starter);
        carveSurfaceRect(root, starter);
        mirrorFloor(root, starter);
        parityOperations(root, starter);
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
