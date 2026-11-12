#include "../agent_workspace.hpp"
#include "../agent_edit_operations.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
namespace fs = std::filesystem;
int failures{};
int sequence{};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(const double actual, const double expected)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= 0.0001;
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if ( !stream )
    {
        throw std::runtime_error("cannot write synthetic edit fixture");
    }
}

void saveNative(const fs::path& path, const sed::Level& level)
{
    const auto bytes = sed::assets::saveToSedBytes(level);
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if ( !stream )
    {
        throw std::runtime_error("cannot write native edit fixture");
    }
}

sed::Level readNative(const fs::path& path)
{
    const auto encoded = sed_agent::readFileBytes(path);
    std::vector<std::byte> bytes(encoded.size());
    std::transform(encoded.begin(), encoded.end(), bytes.begin(),
        [](const char value) { return static_cast<std::byte>(value); });
    return sed::assets::loadSedFileBytes(bytes).level;
}

fs::path request(const fs::path& root, const std::string& operations)
{
    const auto path = root / ("request-" + std::to_string(++sequence) + ".json");
    writeText(path, "{\"schemaVersion\":1,\"operations\":[" + operations + "]}");
    return path;
}

sed::Level apply(const fs::path& root, const fs::path& source, const std::string& operations)
{
    const auto patch = request(root, operations);
    const auto output = root / ("edited-" + std::to_string(sequence) + source.extension().string());
    const auto original = sed_agent::readFileBytes(source);
    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto* hash = planned.find("outputSha256");
    check(hash && hash->type == sed_agent::Json::Type::String, "plan returns reviewed output hash");
    check(!fs::exists(output), "plan publishes no edited level");
    if ( !hash || hash->type != sed_agent::Json::Type::String )
    {
        throw std::runtime_error("plan has no output hash");
    }
    (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string(), hash->str });
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(output)) == hash->str,
        "saved edit exactly matches its reviewed plan");
    check(sed_agent::readFileBytes(source) == original, "successful edit preserves its source bytes");
    return source.extension() == ".sed" ? readNative(output) : sed::assets::loadFromJKL(output);
}

void refuse(const fs::path& root, const fs::path& source, const std::string& operations,
    const std::string& errorCode, const int exitCode = sed_agent::kExitRefusal)
{
    const auto patch = request(root, operations);
    const auto output = root / ("refused-" + std::to_string(sequence) + source.extension().string());
    const auto original = sed_agent::readFileBytes(source);
    for ( const bool planning : { true, false } )
    {
        try
        {
            const auto args = planning ? std::vector<std::string>{ source.string(), patch.string() }
                : std::vector<std::string>{ source.string(), patch.string(), output.string() };
            (void)sed_agent::runCommand(planning ? "plan" : "apply", args);
            check(false, "edit must refuse with " + errorCode);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == errorCode && error.code == exitCode,
                "expected " + errorCode + ", received " + error.errorCode + ": " + error.message);
        }
        check(!fs::exists(output), "refused edit publishes no output");
        check(sed_agent::readFileBytes(source) == original, "refused edit preserves source bytes");
    }
}

int normalFace(const sed::Sector& sector, const sed::TVector wanted)
{
    for ( std::size_t i = 0; i < sector.surfaces.size(); ++i )
    {
        if ( sed::level::surfaceCalcNormalNewell(sector, sector.surfaces[i]).dot(wanted) > 0.99 )
        {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("fixture face missing");
}

std::string surfaceFields(const int surface)
{
    return ",\"sector\":0,\"surface\":" + std::to_string(surface);
}

void fitDimensions(const fs::path& root, const sed::Level& starter)
{
    auto sourceLevel = starter;
    auto& sector = sourceLevel.sectors[0];
    sector.id = 7001;
    for ( auto& vertex : sector.vertices )
    {
        vertex.x = 10 + 2 * vertex.x;
        vertex.y = 20 + 3 * vertex.y;
        vertex.z = 30 + 4 * vertex.z;
    }
    for ( auto& surface : sector.surfaces )
    {
        surface.normal = sed::level::surfaceCalcNormalNewell(sector, surface);
        surface.uScale = -0.25f;
        surface.vScale = 0.5f;
        for ( auto& corner : surface.texVertices )
        {
            corner.color = sed::ColorF{ 0.125f, 0.25f, 0.75f, 0.5f };
        }
    }
    const auto source = root / "fit-dimensions.sed";
    saveNative(source, sourceLevel);
    const std::array<sed::TVector, 3> normals = { sed::TVector{ 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
    for ( std::size_t plane = 0; plane < normals.size(); ++plane )
    {
        const int face = normalFace(sector, normals[plane]);
        const auto edited = apply(root, source, "{\"op\":\"fitSurfaceTexture\"" + surfaceFields(face)
            + ",\"width\":64,\"height\":96}");
        const auto& fitted = edited.sectors[0].surfaces[face];
        for ( std::size_t c = 0; c < fitted.vertices.size(); ++c )
        {
            const auto& position = edited.sectors[0].vertices[fitted.vertices[c]];
            // Independent fixed rectangular fixture: X=8..12, Y=17..23, Z=26..34.
            const double expectedU = plane == 2 ? (position.y - 17) * (64.0 / 6.0) : (position.x - 8) * 16;
            const double expectedV = plane == 0 ? (position.y - 17) * 16 : (position.z - 26) * 12;
            check(near(fitted.texVertices[c].u, expectedU) && near(fitted.texVertices[c].v, expectedV),
                "fit maps all three dominant planes to explicit 64 by 96 texel bounds");
            check(fitted.texVertices[c].color == sector.surfaces[face].texVertices[c].color,
                "fitting preserves each independent corner light channel");
        }
        auto restored = edited;
        restored.sectors[0].surfaces[face].texVertices = sourceLevel.sectors[0].surfaces[face].texVertices;
        check(sed::assets::saveToSedBytes(restored) == sed::assets::saveToSedBytes(sourceLevel),
            "fit changes only target UV values, retaining IDs, scale, flags and unrelated surfaces");
    }
    auto normalized = sourceLevel;
    normalized.uvCoordinateSpace = sed::UvCoordinateSpace::Normalized;
    const auto normalizedPath = root / "fit-normalized.sed";
    saveNative(normalizedPath, normalized);
    refuse(root, normalizedPath,
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":64,"height":64})",
        "editUvCoordinateSpaceUnsupported");
    refuse(root, source,
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":64,"height":64},{"op":"setVertexPosition","sector":0,"vertex":999,"position":[0,0,0]})",
        "editOperationInvalid", sed_agent::kExitUsage);
}

void crossedFaceRefusal(const fs::path& root, const sed::Level& starter)
{
    auto sourceLevel = starter;
    sourceLevel.importedJklSurfaceRefs.clear();
    sourceLevel.importedJklCogRowsPresent = false;
    auto& sector = sourceLevel.sectors[0];
    // A five-point star has distinct corners and consistent local turns, but
    // crossing boundary edges make it invalid for convex-face editing.
    sector.vertices = { { 0, 3, 0 }, { -2, -3, 0 }, { 3, 1, 0 }, { -3, 1, 0 }, { 2, -3, 0 } };
    sector.surfaces.resize(1);
    auto& face = sector.surfaces[0];
    face.adjoin.reset();
    face.vertices = { 0, 1, 2, 3, 4 };
    face.texVertices.resize(5);
    face.normal = { 0, 0, 1 };
    const auto source = root / "crossed-face.sed";
    saveNative(source, sourceLevel);
    refuse(root, source,
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":64,"height":64})",
        "editTextureUnsupported");
}

sed::Sector tetrahedron(const sed::Sector& prototype, const bool lower, const int id)
{
    auto sector = prototype;
    sector.id = id;
    sector.vertices = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, lower ? -2.0 : 2.0 } };
    const std::array<std::array<int, 3>, 4> upperFaces = { { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } } };
    sector.surfaces.clear();
    for ( auto corners : upperFaces )
    {
        if ( lower )
        {
            std::reverse(corners.begin(), corners.end());
        }
        auto surface = prototype.surfaces[0];
        surface.adjoin.reset();
        surface.vertices.assign(corners.begin(), corners.end());
        surface.texVertices.resize(3);
        surface.texVertices[0].u = -17;
        surface.texVertices[0].v = 9;
        surface.texVertices[1].u = 41;
        surface.texVertices[1].v = 13;
        surface.texVertices[2].u = 5;
        surface.texVertices[2].v = 97;
        for ( auto& corner : surface.texVertices )
        {
            corner.color = sed::ColorF{ 0.125f, 0.25f, 0.5f, 0.75f };
        }
        surface.normal = sed::level::surfaceCalcNormalNewell(sector, surface);
        sector.surfaces.push_back(std::move(surface));
    }
    return sector;
}

void vertexPosition(const fs::path& root, const sed::Level& starter)
{
    auto sourceLevel = starter;
    sourceLevel.sectors = { tetrahedron(starter.sectors[0], false, 7011),
        tetrahedron(starter.sectors[0], true, 7012), tetrahedron(starter.sectors[0], false, 7013) };
    sourceLevel.importedJklSurfaceRefs.clear();
    sourceLevel.importedJklCogRowsPresent = false;
    sourceLevel.sectors[0].surfaces[0].adjoin = sed::SurfaceRef{ 1, 0 };
    sourceLevel.sectors[1].surfaces[0].adjoin = sed::SurfaceRef{ 0, 0 };
    const auto source = root / "welded-vertices.sed";
    saveNative(source, sourceLevel);
    const auto edited = apply(root, source,
        R"({"op":"setVertexPosition","sector":0,"vertex":1,"position":[2.5,0,0]})");
    check(edited.sectors[0].vertices[1].x == 2.5 && edited.sectors[1].vertices[1].x == 2.5,
        "vertex positioning moves both reciprocal portal copies exactly");
    check(edited.sectors[2].vertices[1].x == 2,
        "coincident vertices without an adjoin remain independent");
    check(edited.sectors[0].surfaces[0].adjoin == sed::SurfaceRef{ 1, 0 }
            && edited.sectors[1].surfaces[0].adjoin == sed::SurfaceRef{ 0, 0 },
        "vertex positioning retains both portal identities");
    for ( const auto& sector : edited.sectors )
    {
        check(sed::level::sectorIsConvex(sector), "edited tetrahedra remain convex");
        for ( const auto& surface : sector.surfaces )
        {
            check(sed::level::surfaceIsPlanar(sector, surface), "edited faces remain planar");
        }
    }
    auto restored = edited;
    for ( std::size_t s = 0; s < restored.sectors.size(); ++s )
    {
        restored.sectors[s].vertices = sourceLevel.sectors[s].vertices;
        for ( std::size_t f = 0; f < restored.sectors[s].surfaces.size(); ++f )
        {
            restored.sectors[s].surfaces[f].normal = sourceLevel.sectors[s].surfaces[f].normal;
        }
    }
    check(sed::assets::saveToSedBytes(restored) == sed::assets::saveToSedBytes(sourceLevel),
        "vertex edit preserves native IDs, corner order, UVs and independent lighting");
    refuse(root, source, R"({"op":"setVertexPosition","sector":0,"vertex":3,"position":[0,0,-2]})",
        "editGeometryUnsupported");
    auto broken = sourceLevel;
    broken.sectors[1].surfaces[0].adjoin.reset();
    const auto brokenPath = root / "broken-portal.sed";
    saveNative(brokenPath, broken);
    refuse(root, brokenPath, R"({"op":"setVertexPosition","sector":0,"vertex":1,"position":[2.5,0,0]})",
        "editAdjoinUnsupported");
    const auto cube = root / "warped-cube.sed";
    saveNative(cube, starter);
    refuse(root, cube, R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[-0.5,-0.5,-0.5]})",
        "editGeometryUnsupported");
    const auto textSource = root / "single-tetrahedron.jkl";
    auto textLevel = starter;
    textLevel.sectors = { tetrahedron(starter.sectors[0], false, 7021) };
    textLevel.importedJklSurfaceRefs.clear();
    textLevel.importedJklCogRowsPresent = false;
    writeText(textSource, sed::assets::buildJklExport(textLevel).text);
    const auto textEdited = apply(root, textSource,
        R"({"op":"setVertexPosition","sector":0,"vertex":3,"position":[0,0,3]})");
    check(textEdited.sectors[0].vertices[3].z == 3, "vertex edit survives actual JKL output readback");
}

sed::Sector triangulatedPyramid(const sed::Sector& prototype, const int sides,
    const bool lower, const int id)
{
    auto sector = prototype;
    sector.id = id;
    // A regular polygon, its base center (0), and its apex (1). Both the
    // base and the side wall have one triangle per polygon edge, so a small
    // face-corner limit cannot hide the sector-wide convexity workload.
    sector.vertices = { { 0, 0, 0 }, { 0, 0, lower ? -10.0 : 10.0 } };
    const double turn = 2 * std::acos(-1.0);
    for ( int side = 0; side < sides; ++side )
    {
        const double angle = turn * side / sides;
        sector.vertices.push_back({ 10 * std::cos(angle), 10 * std::sin(angle), 0 });
    }
    sector.surfaces.clear();
    for ( int side = 0; side < sides; ++side )
    {
        const int first = side + 2;
        const int second = (side + 1) % sides + 2;
        const std::array<std::array<int, 3>, 2> triangles = { { { 0, first, second }, { 1, second, first } } };
        for ( auto corners : triangles )
        {
            if ( lower )
            {
                std::reverse(corners.begin(), corners.end());
            }
            auto face = prototype.surfaces[0];
            face.adjoin.reset();
            face.vertices.assign(corners.begin(), corners.end());
            face.texVertices.resize(3);
            face.normal = sed::level::surfaceCalcNormalNewell(sector, face);
            sector.surfaces.push_back(std::move(face));
        }
    }
    return sector;
}

sed::Level pyramidPair(const sed::Level& starter, const int sides)
{
    auto level = starter;
    level.importedJklSurfaceRefs.clear();
    level.importedJklCogRowsPresent = false;
    level.sectors = { triangulatedPyramid(starter.sectors[0], sides, false, 7101),
        triangulatedPyramid(starter.sectors[0], sides, true, 7102) };
    // Every other face is a base triangle. All contain the central vertex,
    // exercising repeated portal validation and exact reverse-cyclic matches.
    for ( int face = 0; face < sides * 2; face += 2 )
    {
        level.sectors[0].surfaces[face].adjoin = sed::SurfaceRef{ 1, face };
        level.sectors[1].surfaces[face].adjoin = sed::SurfaceRef{ 0, face };
    }
    return level;
}

void vertexWorkAdmission(const fs::path& root, const sed::Level& starter)
{
    const auto apexMove = sed_agent::parseJson(
        R"({"op":"setVertexPosition","sector":0,"vertex":1,"position":[0,0,11]})");
    // Regression: a closed convex sector with thousands of individually tiny
    // faces must refuse before unbounded sector-wide geometry work begins.
    auto large = pyramidPair(starter, 900);
    large.sectors.resize(1);
    for ( auto& face : large.sectors[0].surfaces )
    {
        face.adjoin.reset();
    }
    const auto original = sed::assets::saveToSedBytes(large);
    try
    {
        (void)sed_agent::tryApplyEditOperation(large, apexMove);
        check(false, "large triangulated vertex edit requires a work-limit input error");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitInput && error.errorCode == "editWorkLimitExceeded",
            "large triangulated vertex edit reports editWorkLimitExceeded");
    }
    check(sed::assets::saveToSedBytes(large) == original,
        "work-limit rejection preserves every in-memory level byte");

    // Each 600-sided sector fits independently; the welded operation must
    // share one budget across both sectors and their many portal matches.
    auto paired = pyramidPair(starter, 600);
    auto independent = paired;
    independent.sectors.resize(1);
    for ( auto& face : independent.sectors[0].surfaces )
    {
        face.adjoin.reset();
    }
    check(sed_agent::tryApplyEditOperation(independent, apexMove)
            && independent.sectors[0].vertices[1].z == 11,
        "ordinary triangulated pyramid remains editable within its own budget");
    const auto pairedSource = root / "aggregate-vertex-work.sed";
    saveNative(pairedSource, paired);
    const auto centerMove = sed_agent::parseJson(
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0.01,0,0]})");
    const auto pairedOriginal = sed::assets::saveToSedBytes(paired);
    try
    {
        (void)sed_agent::tryApplyEditOperation(paired, centerMove);
        check(false, "welded sectors share one cumulative vertex work budget");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitInput && error.errorCode == "editWorkLimitExceeded",
            "aggregate welded work reports editWorkLimitExceeded");
    }
    check(sed::assets::saveToSedBytes(paired) == pairedOriginal,
        "aggregate work rejection preserves sectors staged before the limit");
    refuse(root, pairedSource,
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0.01,0,0]})",
        "editWorkLimitExceeded", sed_agent::kExitInput);

    // A modest many-portal edit still welds precisely and preserves closure.
    auto modest = pyramidPair(starter, 64);
    check(sed_agent::tryApplyEditOperation(modest, centerMove), "bounded welded edit is recognized");
    check(modest.sectors[0].vertices[0].x == 0.01 && modest.sectors[1].vertices[0].x == 0.01,
        "within-budget many-portal edit moves both copies exactly");
    for ( const auto& sector : modest.sectors )
    {
        check(sed::level::sectorIsConvex(sector), "within-budget pyramid remains convex");
    }
}

sed::Level subdividedPrismRing(const sed::Level& starter, const int count)
{
    auto level = starter;
    level.importedJklSurfaceRefs.clear();
    level.importedJklCogRowsPresent = false;
    level.sectors.clear();
    std::vector<sed::Vertex> ring;
    for ( int index = 0; index < count; ++index )
    {
        const double angle = 2 * std::acos(-1.0) * index / count;
        ring.push_back({ 10 * std::cos(angle), 10 * std::sin(angle), 0 });
    }
    for ( int index = 0; index < count; ++index )
    {
        auto sector = starter.sectors[0];
        sector.id = 7200 + index;
        const auto a = ring[index];
        const auto b = ring[(index + 1) % count];
        const std::array<sed::Vertex, 6> points = {
            sed::Vertex{ 0, 0, 0 }, a, b,
            sed::Vertex{ 0, 0, 10 }, sed::Vertex{ a.x, a.y, 10 }, sed::Vertex{ b.x, b.y, 10 }
        };
        sector.vertices.assign(points.begin(), points.end());
        sector.surfaces.clear();
        const std::vector<std::vector<int>> faces = {
            { 0, 1, 2 }, { 3, 5, 4 }, { 0, 3, 4, 1 }, { 1, 4, 5, 2 }, { 0, 2, 5, 3 }
        };
        std::map<std::pair<int, int>, std::vector<int>> edgeVertices;
        for ( const auto& corners : faces )
        {
            auto face = starter.sectors[0].surfaces[0];
            face.adjoin.reset();
            face.vertices.clear();
            for ( std::size_t corner = 0; corner < corners.size(); ++corner )
            {
                const int first = corners[corner];
                const int second = corners[(corner + 1) % corners.size()];
                const std::pair<int, int> key = std::minmax(first, second);
                auto [entry, inserted] = edgeVertices.try_emplace(key);
                if ( inserted )
                {
                    // Canonical world endpoint order makes the 15 interior
                    // samples identical in adjacent independent vertex pools.
                    const auto& start = points[key.first];
                    const auto& end = points[key.second];
                    const bool reverse = std::tie(start.x, start.y, start.z) > std::tie(end.x, end.y, end.z);
                    const auto& low = reverse ? end : start;
                    const auto& high = reverse ? start : end;
                    for ( int step = 1; step < 16; ++step )
                    {
                        const double fraction = (reverse ? 16 - step : step) / 16.0;
                        entry->second.push_back(static_cast<int>(sector.vertices.size()));
                        sector.vertices.push_back({ low.x * (1 - fraction) + high.x * fraction,
                            low.y * (1 - fraction) + high.y * fraction,
                            low.z * (1 - fraction) + high.z * fraction });
                    }
                }
                face.vertices.push_back(first);
                if ( first == key.first )
                {
                    face.vertices.insert(face.vertices.end(), entry->second.begin(), entry->second.end());
                }
                else
                {
                    face.vertices.insert(face.vertices.end(), entry->second.rbegin(), entry->second.rend());
                }
            }
            face.texVertices.resize(face.vertices.size());
            face.normal = sed::level::surfaceCalcNormalNewell(sector, face);
            sector.surfaces.push_back(std::move(face));
        }
        level.sectors.push_back(std::move(sector));
    }
    for ( int index = 0; index < count; ++index )
    {
        const int next = (index + 1) % count;
        level.sectors[index].surfaces[4].adjoin = sed::SurfaceRef{ next, 2 };
        level.sectors[next].surfaces[2].adjoin = sed::SurfaceRef{ index, 4 };
    }
    return level;
}

void vertexMatchingWorkAdmission(const sed::Level& starter)
{
    // Forty disjoint wedge interiors meet along one axis. Each prism has
    // 141 vertices and five faces: 48-corner caps and 64-corner side walls.
    // Their geometry checks fit collectively; repeated portal validation and
    // matching must also consume the shared budget. Omitting portal charges
    // would therefore admit this otherwise supported operation.
    const auto unchanged = sed_agent::parseJson(
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0,0,0]})");
    auto modest = subdividedPrismRing(starter, 8);
    check(sed_agent::tryApplyEditOperation(modest, unchanged),
        "valid subdivided portal faces remain supported within budget");
    auto dense = subdividedPrismRing(starter, 40);
    const auto original = sed::assets::saveToSedBytes(dense);
    try
    {
        (void)sed_agent::tryApplyEditOperation(dense, unchanged);
        check(false, "dense portal matching shares the cumulative vertex work budget");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitInput && error.errorCode == "editWorkLimitExceeded",
            "dense portal matching reports editWorkLimitExceeded");
    }
    check(sed::assets::saveToSedBytes(dense) == original,
        "portal-matching work rejection preserves every in-memory level byte");
}

void textureAlignment(const fs::path& root, const sed::Level& starter)
{
    auto sourceLevel = starter;
    auto& sector = sourceLevel.sectors[0];
    const int reference = normalFace(sector, { 0, 0, 1 });
    const int target = normalFace(sector, { 0, 1, 0 });
    for ( auto& corner : sector.surfaces[reference].texVertices )
    {
        const auto index = static_cast<std::size_t>(&corner - sector.surfaces[reference].texVertices.data());
        const auto& position = sector.vertices[sector.surfaces[reference].vertices[index]];
        corner.u = static_cast<float>(10 + 320 * (position.x + 1));
        corner.v = static_cast<float>(20 + 320 * (position.y + 1));
    }
    for ( auto& corner : sector.surfaces[target].texVertices )
    {
        corner.u = 73;
        corner.v = -11;
        corner.color = sed::ColorF{ 0.75f, 0.25f, 0.125f, 0.5f };
    }
    const auto source = root / "edge-alignment.sed";
    saveNative(source, sourceLevel);
    const auto alignment = "{\"op\":\"alignSurfaceTexture\"" + surfaceFields(target)
        + ",\"otherSector\":0,\"otherSurface\":" + std::to_string(reference) + "}";
    const auto edited = apply(root, source, alignment);
    const auto& aligned = edited.sectors[0].surfaces[target];
    for ( std::size_t c = 0; c < aligned.vertices.size(); ++c )
    {
        const auto& position = sector.vertices[aligned.vertices[c]];
        check(near(aligned.texVertices[c].u, 10 + 320 * (position.x + 1))
                && near(aligned.texVertices[c].v, 20 - 320 * (position.z + 1)),
            "stitch continues the reference mapping across the folded edge with exact seam values");
        check(aligned.texVertices[c].color == sector.surfaces[target].texVertices[c].color,
            "alignment preserves all target corner light channels");
    }
    auto restored = edited;
    restored.sectors[0].surfaces[target] = sourceLevel.sectors[0].surfaces[target];
    check(sed::assets::saveToSedBytes(restored) == sed::assets::saveToSedBytes(sourceLevel),
        "alignment changes only its target, retaining reference, geometry and native identity");
    const int disconnected = normalFace(sector, { 0, 0, -1 });
    refuse(root, source, "{\"op\":\"alignSurfaceTexture\"" + surfaceFields(disconnected)
        + ",\"otherSector\":0,\"otherSurface\":" + std::to_string(reference) + "}", "editTextureUnsupported");
    refuse(root, source, "{\"op\":\"alignSurfaceTexture\"" + surfaceFields(reference)
        + ",\"otherSector\":0,\"otherSurface\":" + std::to_string(reference) + "}", "editTextureUnsupported");
    auto nonAffine = sourceLevel;
    nonAffine.sectors[0].surfaces[reference].texVertices[2].u += 17;
    const auto nonAffinePath = root / "non-affine-reference.sed";
    saveNative(nonAffinePath, nonAffine);
    refuse(root, nonAffinePath, alignment, "editTextureUnsupported");
}

void malformedRequests(const fs::path& root, const sed::Level& starter)
{
    const auto source = root / "malformed-edits.sed";
    saveNative(source, starter);
    const std::string malformed[] = {
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0,0]})",
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0,"0",0]})",
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[1000001,0,0]})",
        R"({"op":"setVertexPosition","sector":-1,"vertex":0,"position":[0,0,0]})",
        R"({"op":"setVertexPosition","sector":0,"vertex":0.5,"position":[0,0,0]})",
        R"({"op":"setVertexPosition","sector":0,"vertex":2147483648,"position":[0,0,0]})",
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[0,0,0],"extra":true})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":0,"height":64})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":16385,"height":64})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":64.5,"height":64})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":"64","height":64})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":999,"width":64,"height":64})",
        R"({"op":"fitSurfaceTexture","sector":0,"surface":0,"width":64})",
        R"({"op":"alignSurfaceTexture","sector":0,"surface":0,"otherSector":0,"otherSurface":999})",
        R"({"op":"alignSurfaceTexture","sector":0,"surface":0,"otherSector":0})",
        R"({"op":"alignSurfaceTexture","sector":0,"surface":0,"otherSector":0,"otherSurface":1,"width":64})"
    };
    for ( const auto& operation : malformed )
    {
        refuse(root, source, operation, "editOperationInvalid", sed_agent::kExitUsage);
    }
    refuse(root, source,
        R"({"op":"setVertexPosition","sector":0,"vertex":0,"position":[1e309,0,0]})",
        "", sed_agent::kExitUsage);
}

template <typename Function>
void scenario(const char* name, Function run)
{
    try
    {
        run();
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, std::string(name) + ": " + error.errorCode + ": " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string(name) + ": " + error.what());
    }
}
}

int main()
{
    const auto root = fs::temp_directory_path() / ("red-agent-edits-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    scenario("fixtures", [&]
    {
        const auto starterPath = root / "starter.jkl";
        (void)sed_agent::runCommand("new", { starterPath.string(), "mots" });
        const auto starter = sed::assets::loadFromJKL(starterPath);
        scenario("dimension-based texture fit", [&] { fitDimensions(root, starter); });
        scenario("crossed polygon refusal", [&] { crossedFaceRefusal(root, starter); });
        scenario("absolute vertex positioning", [&] { vertexPosition(root, starter); });
        scenario("cumulative vertex work admission", [&] { vertexWorkAdmission(root, starter); });
        scenario("cumulative portal matching work admission", [&] { vertexMatchingWorkAdmission(starter); });
        scenario("shared-edge texture alignment", [&] { textureAlignment(root, starter); });
        scenario("malformed edit requests", [&] { malformedRequests(root, starter); });
    });
    fs::remove_all(root);
    if ( failures )
    {
        std::cerr << failures << " edit-operation assertions failed\n";
    }
    return failures == 0 ? 0 : 1;
}
