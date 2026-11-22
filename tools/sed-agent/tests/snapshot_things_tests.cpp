#include "../agent_workspace.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/png.hpp"
#include "sed/assets/sed.hpp"
#include "sed/values.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <tuple>
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
    // A drawn exterior roof and an interior floor both face skyward. Ordinary
    // back-face culling therefore keeps both; the section plane exposes the floor.
    {
        auto room = level;
        auto& shell = room.sectors.front();
        auto roof = floor;
        roof.material = "green.mat";
        for ( auto vertex : sector.vertices )
        {
            vertex.z = 2;
            shell.vertices.push_back(vertex);
        }
        roof.vertices = { 4, 5, 6, 7 };
        shell.surfaces.push_back(roof);
        const auto roomPath = root / "roofed.sed";
        const auto inputBytes = bytes(sed::assets::saveToSedBytes(room));
        write(roomPath, inputBytes);
        auto overview = sed_agent::parseJson(R"({"view":"overhead","width":64,"height":64,"fullbright":true})");
        overview.set("resourceArchives", archives);
        const auto planColor = [&](const fs::path& path, const int x)
        {
            const auto png = sed_agent::readFileBytes(path);
            const auto img = sed::assets::decodePng(std::as_bytes(std::span(png.data(), png.size())));
            const auto i = (32 * img.width + x) * 4;
            return std::pair{ img.rgba[i], img.rgba[i + 1] };
        };
        (void)run(root, roomPath, overview, "overview", "roof.png");
        check(planColor(root / "roof.png", 32) == std::pair<std::uint8_t, std::uint8_t>{ 0, 255 },
            "ordinary overhead sees the green exterior roof");
        overview.set("cutHeight", Json::makeNumber(1));
        const auto cut = run(root, roomPath, overview, "overview", "section.png");
        check(planColor(root / "section.png", 32) == std::pair<std::uint8_t, std::uint8_t>{ 255, 0 },
            "overhead cut exposes the red floor");
        check(cut.find("cutHeight") && cut.find("cutHeight")->number == 1,
            "the framing receipt identifies the applied world-space cut");
        const auto replay = run(root, roomPath, field(cut, "snapshotRequest"), "snapshot", "section-replay.png");
        check(field(cut, "imageSha256").str == field(replay, "imageSha256").str,
            "overhead cut replays its clip plane and materials through snapshot");
        // The left half of this inclined roof is below the same section.
        // A per-face hide would either keep or discard the entire surface.
        for ( int i = 4; i < 8; ++i )
        {
            shell.vertices[i].z = shell.vertices[i].x < 0 ? 0.5 : 1.5;
        }
        const auto slope = root / "sloped.sed";
        write(slope, bytes(sed::assets::saveToSedBytes(room)));
        (void)run(root, slope, overview, "overview", "sloped.png");
        check(planColor(root / "sloped.png", 16) == std::pair<std::uint8_t, std::uint8_t>{ 0, 255 }
            && planColor(root / "sloped.png", 48) == std::pair<std::uint8_t, std::uint8_t>{ 255, 0 },
            "the near plane clips a crossing roof polygon at the requested height");
        overview.set("cutHeight", Json::makeNumber(0));
        (void)run(root, roomPath, overview, "overview", "on-plane.png");
        check(planColor(root / "on-plane.png", 32) == std::pair<std::uint8_t, std::uint8_t>{ 255, 0 },
            "floor lying exactly on the cut plane remains visible");
        for ( const auto& invalid : { Json::makeNumber(-1), Json::makeNumber(3), Json::makeString("1") } )
        {
            overview.set("cutHeight", invalid);
            bool refused = false;
            try
            {
                (void)run(root, roomPath, overview, "overview", "bad-section.png");
            }
            catch ( const sed_agent::AgentError& error )
            {
                refused = error.code == sed_agent::kExitUsage;
            }
            check(refused && !fs::exists(root / "bad-section.png"), "invalid cut refuses before publication");
        }
        check(sed_agent::readFileBytes(roomPath) == inputBytes, "cutaway preserves all source geometry");
    }
    {
        auto pickupLevel = level;
        pickupLevel.kind = sed::ProjectType::JKDF2;
        pickupLevel.sectors[0].surfaces[0].surfaceFlags = sed::SF_Floor;
        pickupLevel.things[0].z = -0.1;
        pickupLevel.things[0].values.push_back(sed::deserializeTplValue("type", "item"));
        const auto pickupPath = root / "pickup.sed";
        write(pickupPath, bytes(sed::assets::saveToSedBytes(pickupLevel)));
        auto audit = sed_agent::parseJson(R"({"schemaVersion":1,"things":[0]})");
        audit.set("resourceArchives", archives);
        const auto report = run(root, pickupPath, audit, "item-placement-check", "");
        const auto& row = field(report, "items").array.at(0);
        check(field(row, "status").str == "belowSectorFloor"
              && field(row, "clearance").number < -0.09 && field(row, "clearance").number > -0.11,
              "exact static model vertex penetrates a nearby flagged floor");
        check(!field(report, "gameplayVerified").boolean,
              "geometric report is not native gameplay verification");
        const auto measure = [&](const std::string& modelText)
        {
            const auto candidate = sed::assets::buildGobBytes({{"3do\\triangle.3do", modelText}});
            const auto candidatePath = root / "candidate-item.gob";
            write(candidatePath, candidate);
            auto bindings = archives;
            bindings.array[0].set("path", Json::makeString(candidatePath.string()));
            bindings.array[0].set("sha256", Json::makeString(sed_agent::sha256Hex(candidate)));
            auto request = audit;
            request.set("resourceArchives", bindings);
            const auto result = run(root, pickupPath, request, "item-placement-check", "");
            return field(result, "items").array.at(0);
        };
        const auto original = sed::assets::saveTo3DO(model);
        const auto rowStart = [&](const char* table, const char* id)
        {
            return original.find(id, original.find(table));
        };
        const auto vertex = rowStart("VERTICES 3", "0:");
        auto damaged = original;
        damaged.replace(vertex, damaged.find('\n', vertex) - vertex, "0: 0 0 unreadable 1");
        const auto item = measure(damaged);
        check(field(item, "status").str == "modelUnresolved"
                && field(item, "modelSha256").str == sed_agent::sha256Hex(damaged)
                && !item.find("lowestVertex") && !item.find("floorZ") && !item.find("clearance"),
            "unreadable referenced XYZ preserves model identity without a geometric conclusion");
        auto duplicateSlot = original;
        duplicateSlot[rowStart("VERTICES 3", "1:")] = '0';
        check(field(measure(duplicateSlot), "status").str == "modelUnresolved",
            "duplicate vertex slots cannot leave a zero-filled missing position");
        auto permuted = original;
        for ( const auto* table : { "VERTICES 3", "TEXTURE VERTICES 3" } )
        {
            const auto first = rowStart(table, "0:");
            const auto second = rowStart(table, "1:");
            const auto third = rowStart(table, "2:");
            permuted.replace(first, third - first,
                original.substr(second, third - second) + original.substr(first, second - first));
        }
        const auto same = measure(permuted);
        check(field(same, "status").str == "belowSectorFloor"
                && field(same, "clearance").number == field(row, "clearance").number,
            "valid coordinate and UV rows can be reordered without changing the sampled clearance");
        auto untextured = model;
        untextured.materials.clear();
        untextured.meshes[0].texVertices.clear();
        untextured.meshes[0].faces[0].material = -1;
        untextured.meshes[0].faces[0].texVertices = {-1, -1, -1};
        check(field(measure(sed::assets::saveTo3DO(untextured)), "status").str == "belowSectorFloor",
            "material-less faces without texture coordinates still contribute sampled vertices");
        const auto noModelGob = sed::assets::buildGobBytes({{"mat\\red.mat", mat(255, 0)}});
        write(root / "no-model.gob", noModelGob);
        auto noModelArchives = archives;
        noModelArchives.array[0].set("path", Json::makeString((root / "no-model.gob").string()));
        noModelArchives.array[0].set("sha256", Json::makeString(sed_agent::sha256Hex(noModelGob)));
        audit.set("resourceArchives", noModelArchives);
        const auto unresolved = run(root, pickupPath, audit, "item-placement-check", "");
        check(field(field(unresolved, "items").array.at(0), "status").str == "modelUnresolved",
              "missing stock bytes cannot produce a clearance result");
        pickupLevel.sectors[0].vertices[0].z = -20;
        write(pickupPath, bytes(sed::assets::saveToSedBytes(pickupLevel)));
        {
            auto collisionModel = model;
            collisionModel.nodes[0].position.z = 0.03;
            collisionModel.meshes[0].geo = 0;
            collisionModel.meshes[0].faces[0].geo = 0;
            auto& verts = collisionModel.meshes[0].vertices;
            verts = {{-1,-1,0}, {-0.6,-1,0}, {-0.8,-0.6,0}};
            const auto colliderGob = sed::assets::buildGobBytes({{"3do\\collision.3do", sed::assets::saveTo3DO(collisionModel)}});
            write(root / "collision.gob", colliderGob);
            auto supportArchives = archives;
            auto shiftedPickup = model;
            shiftedPickup.nodes[0].position.z = 0.04;
            const auto shiftedGob = sed::assets::buildGobBytes({
                {"3do\\triangle.3do", sed::assets::saveTo3DO(shiftedPickup)}});
            write(root / "shifted-pickup.gob", shiftedGob);
            supportArchives.array[0].set("path", Json::makeString((root / "shifted-pickup.gob").string()));
            supportArchives.array[0].set("sha256", Json::makeString(sed_agent::sha256Hex(shiftedGob)));
            auto supportEntry = archives.array[0];
            supportEntry.set("path", Json::makeString((root / "collision.gob").string()));
            supportEntry.set("sha256", Json::makeString(sed_agent::sha256Hex(colliderGob)));
            supportArchives.array.push_back(supportEntry);
            auto withSupport = pickupLevel;
            withSupport.templates.push_back({"+stand", "none"});
            withSupport.things[0].yaw = 90;
            sed::Thing collider;
            collider.templateName = "+stand";
            collider.sector = 0;
            collider.z = 0.02;
            collider.yaw = 90;
            collider.values = {sed::deserializeTplValue("type","cog"),
                sed::deserializeTplValue("collide","3"), sed::deserializeTplValue("thingflags","0x40"),
                sed::deserializeTplValue("move","path"), sed::deserializeTplValue("model3d","collision.3do")};
            withSupport.things.push_back(collider);
            write(pickupPath, bytes(sed::assets::saveToSedBytes(withSupport)));
            auto collisionAudit = sed_agent::parseJson(R"({"schemaVersion":1,"things":[0],"supportThings":[1]})");
            collisionAudit.set("resourceArchives", supportArchives);
            const auto supported = run(root, pickupPath, collisionAudit, "item-placement-check", "");
            const auto& contact = field(supported, "items").array[0];
            check(field(contact,"status").str == "belowColliderAtSample" &&
                field(contact, "supportGeometryBasis").str == "rawGeoset0" &&
                field(contact,"supportThing").number == 1 && std::abs(field(contact,"floorZ").number - 0.02) < 1e-6,
                "explicit native face support uses raw invisible vertices under the Thing pose, not draw-node translation");
            check(std::abs(field(contact, "lowestVertex").array[0].number - 0.8) < 1e-6 &&
                std::abs(field(contact, "lowestVertex").array[1].number + 0.8) < 1e-6 &&
                std::abs(field(contact, "lowestVertex").array[2].number + 0.06) < 1e-6 &&
                std::abs(field(contact, "clearance").number + 0.08) < 1e-6,
                "pickup clearance retains the visible model hierarchy while support uses raw collision geometry");
            {
                auto levelWithFloor = withSupport;
                levelWithFloor.sectors[0].vertices[0].z = 0;
                const auto sameSector = root / "sector-and-collider.sed";
                write(sameSector, bytes(sed::assets::saveToSedBytes(levelWithFloor)));
                const auto inspectSupport = [&](const std::string& modelText, const bool sectorFloor = true)
                {
                    const auto candidate = sed::assets::buildGobBytes({{"3do\\collision.3do", modelText}});
                    write(root / "collision.gob", candidate);
                    auto bindings = supportArchives;
                    bindings.array[1].set("sha256", Json::makeString(sed_agent::sha256Hex(candidate)));
                    auto request = collisionAudit;
                    request.set("resourceArchives", bindings);
                    const auto result = run(root, sectorFloor ? sameSector : pickupPath, request, "item-placement-check", "");
                    return field(result, "items").array.at(0);
                };
                auto fourPositions = collisionModel;
                fourPositions.meshes[0].vertices.push_back({0, 0, 3});
                auto nativeRows = sed::assets::saveTo3DO(fourPositions);
                nativeRows.replace(nativeRows.find("GEOSET 0"), 8, "GEOSET 7");
                const auto rows = nativeRows.find("VERTICES 4");
                std::swap(nativeRows.at(nativeRows.find("2: ", rows)), nativeRows.at(nativeRows.find("3: ", rows)));
                const auto raw = inspectSupport(nativeRows, false);
                check(field(raw, "status").str == "belowColliderAtSample" && std::abs(field(raw, "floorZ").number - 0.02) < 1e-6,
                    "support reads physical positions in the first declared geoset regardless of labels");
                auto downward = collisionModel;
                std::swap(downward.meshes[0].faces[0].vertices[0], downward.meshes[0].faces[0].vertices[1]);
                const auto evaluated = inspectSupport(sed::assets::saveTo3DO(downward));
                check(field(evaluated, "status").str == "belowSectorFloor" && field(evaluated, "surface").number == 0,
                    "complete downward triangle leaves the sector floor eligible");
                auto missingMesh = sed::assets::saveTo3DO(collisionModel);
                missingMesh.erase(missingMesh.find("MESH 0"));
                const auto incomplete = inspectSupport(missingMesh);
                check(field(incomplete, "status").str == "supportThingUnresolved"
                        && !incomplete.find("surface") && !incomplete.find("floorZ") && !incomplete.find("clearance"),
                    "declared but missing collider mesh precludes a sector floor comparison");
                auto unreadable = sed::assets::saveTo3DO(collisionModel);
                const auto x = unreadable.find("0: ", unreadable.find("VERTICES 3")) + 3;
                unreadable.replace(x, unreadable.find(' ', x) - x, "unreadable");
                const auto damaged = inspectSupport(unreadable);
                check(field(damaged, "status").str == "supportThingUnresolved"
                        && !damaged.find("surface") && !damaged.find("floorZ") && !damaged.find("clearance"),
                    "unreadable collider XYZ cannot establish a floor or a collider clearance");
                write(root / "collision.gob", colliderGob);
            }
            {
                auto inherited = withSupport;
                auto& nominated = inherited.things[1];
                nominated.templateName = "+stand";
                nominated.values[3] = sed::deserializeTplValue("move", "none");
                inherited.templates = {{"+stand", "_base"}, {"_base", "none"}};
                const auto inspectParents = [&]()
                {
                    write(pickupPath, bytes(sed::assets::saveToSedBytes(inherited)));
                    const auto result = run(root, pickupPath, collisionAudit, "item-placement-check", "");
                    return field(result, "items").array.at(0);
                };
                check(field(inspectParents(), "status").str == "belowColliderAtSample",
                    "named collider with complete parent chain remains eligible");
                for (const auto* parent : {"+missing", "+STAND"})
                {
                    inherited.templates[1].parent = parent;
                    const auto item = inspectParents();
                    check(field(item, "status").str == "supportThingUnresolved"
                            && !item.find("supportThing") && !item.find("floorZ") && !item.find("clearance"),
                        "missing or cyclic collider parent cannot prove absence of inherited motion");
                }
            }
            {
                const auto jklPath = root / "collider-templates.jkl";
                const auto inspectJkl = [&](const std::string& templates, const std::string& mode = "3")
                {
                    write(jklPath, "SECTION: HEADER\nVersion 1\nEND\n"
                        "SECTION: SECTORS\nWorld sectors 1\nSector 0\nSurfaces 0 0\nEND\n"
                        "SECTION: TEMPLATES\n" + templates + "END\n"
                        "SECTION: THINGS\nWorld things 2\n"
                        "0: +pickup item 0 0 -0.1 0 0 0 0 type=item model3d=triangle.3do\n"
                        "1: +stand stand 0 0 0 0 0 0 0 type=cog collide=" + mode
                        + " thingflags=0x40 move=none model3d=collision.3do\nEND\n");
                    const auto result = run(root, jklPath, collisionAudit, "item-placement-check", "");
                    return field(result, "items").array.at(0);
                };
                const std::string definitions = "+pickup none\n+stand _base\n_base none\n";
                check(field(inspectJkl("World templates 3\n" + definitions), "status").str == "belowColliderAtSample",
                    "JKL support resolves an unambiguous complete chain");
                for (const auto* spelling : {"03", "+3"})
                    check(field(inspectJkl("World templates 3\n" + definitions, spelling), "status").str == "belowColliderAtSample",
                        "standable support accepts complete decimal spellings of native face collision");
                check(field(inspectJkl("World templates 3\n" + definitions, "3x"), "status").str == "supportThingUnresolved",
                    "a partial numeric collision token cannot establish face support");
                const auto ambiguous = inspectJkl("World templates 4\n" + definitions + "_BASE none cog=moved.cog\n");
                check(field(ambiguous, "status").str == "supportThingUnresolved" && !ambiguous.find("clearance"),
                    "conflicting JKL parent declarations cannot establish a static support");
            }
            withSupport.things[1].values.push_back(sed::deserializeTplValue("cog","moved.cog"));
            write(pickupPath, bytes(sed::assets::saveToSedBytes(withSupport)));
            const auto rejected = run(root, pickupPath, collisionAudit, "item-placement-check", "");
            check(field(field(rejected, "items").array[0], "status").str == "supportThingUnresolved",
                "scripted support candidate cannot certify an authored comparison");
            write(pickupPath, bytes(sed::assets::saveToSedBytes(pickupLevel)));
        }
        audit.set("resourceArchives", archives);
        const auto unsupported = run(root, pickupPath, audit, "item-placement-check", "");
        check(field(field(unsupported, "items").array.at(0), "status").str == "floorUnresolved",
              "unsupported non-horizontal support remains unresolved");
    }
    const auto legacy = run(root, source, request, "snapshot", "legacy.png");
    check(field(legacy, "lighting").str == "ambient", "legacy fullbright retains its ambient lighting receipt");
    check(field(legacy, "renderScope").str == "world-surfaces" && !legacy.find("thingPreview"),
          "legacy receipt stays world-only");
    {
        std::string cmp(64 + 768 + 64 * 256, '\0');
        cmp.replace(0, 4, "CMP ");
        cmp[4] = 30;
        const auto palette = sed::assets::makeGrayscalePalette();
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 3; ++c) cmp[64 + 3 * i + c] = static_cast<char>(i);
        sed::assets::RgbaImage gray{8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 255)};
        const auto indexedMat = sed::assets::buildIndexedMat({gray}, palette).bytes;
        const auto captured = sed::assets::buildGobBytes({{"dflt.cmp", cmp}, {"red.mat", indexedMat}});
        write(root / "indexed.gob", captured);
        auto indexed = request;
        indexed.set("renderProfile", Json::makeString("indexed8bit"));
        auto inputs = archives;
        inputs.array[0].set("path", Json::makeString((root / "indexed.gob").string()));
        inputs.array[0].set("sha256", Json::makeString(sed_agent::sha256Hex(captured)));
        indexed.set("resourceArchives", inputs);
        auto receipt = run(root, source, indexed, "snapshot", "indexed.png");
        check(receipt.find("appliedProfile") && field(receipt, "appliedProfile").str == "indexed8bit"
            && field(receipt, "renderer").str == "software-indexed8bit-v1"
            && receipt.find("lightingSource") && field(receipt, "lightingSource").str == "fullbright"
            && sed_agent::toJsonString(receipt).find(sed_agent::sha256Hex(indexedMat)) != std::string::npos
            && sed_agent::toJsonString(receipt).find(sed_agent::sha256Hex(cmp)) != std::string::npos,
            "indexed receipt identifies the applied profile and exact source bytes");
        check(!receipt.find("lighting") && field(receipt, "fullbright").boolean,
            "indexed fullbright receipt omits the incompatible lighting selector");
        auto baked = indexed;
        baked.set("fullbright", Json::makeBool(false));
        const auto bakedReceipt = run(root, source, baked, "snapshot", "indexed-baked.png");
        check(field(bakedReceipt, "lighting").str == "baked" && field(bakedReceipt, "lightingSource").str == "baked-scalar",
            "indexed baked receipt retains the replayable lighting selector");
        indexed.set("view", Json::makeString("overhead"));
        const auto overview = run(root, source, indexed, "overview", "indexed-plan.png");
        check(!field(overview, "snapshotRequest").find("lighting"), "indexed fullbright replay omits lighting");
        const auto replay = run(root, source, field(overview, "snapshotRequest"), "snapshot", "indexed-replay.png");
        check(overview.find("appliedProfile") && replay.find("appliedProfile") &&
            field(overview, "imageSha256").str == field(replay, "imageSha256").str,
            "indexed overview replays through snapshot with the same profile and pixels");
        auto mots = level;
        mots.kind = sed::ProjectType::MOTS;
        write(root / "mots.sed", bytes(sed::assets::saveToSedBytes(mots)));
        write(root / "frame.json", R"({"view":"overhead","width":64,"height":64,"renderProfile":"indexed8bit"})");
        bool rejected = false;
        try { (void)sed_agent::runCommand("overview", {(root / "mots.sed").string(), (root / "frame.json").string()}); }
        catch (const sed_agent::AgentError& error) { rejected = error.message.find("JKDF2") != std::string::npos; }
        check(rejected, "framing-only indexed overview rejects non-JK targets");
        const auto refused = [&](const std::string& cmpBytes, const std::string& matBytes, const char* expected)
        {
            const auto contents = sed::assets::buildGobBytes({{"dflt.cmp", cmpBytes}, {"red.mat", matBytes}});
            write(root / "invalid-assets.gob", contents);
            auto captured = inputs;
            captured.array[0].set("path", Json::makeString((root / "invalid-assets.gob").string()));
            captured.array[0].set("sha256", Json::makeString(sed_agent::sha256Hex(contents)));
            auto request = indexed;
            request.set("resourceArchives", captured);
            bool failed = false;
            try { (void)run(root, source, request, "snapshot", "bad-indexed.png"); }
            catch (const sed_agent::AgentError& error)
            {
                failed = error.code == 3 && error.message.find(expected) != std::string::npos;
            }
            check(failed && !fs::exists(root / "bad-indexed.png"), expected);
        };
        auto invalidSelector = indexedMat;
        invalidSelector[112] = 1; // word 9 of the first texinfo selects absent texture 1
        refused(cmp, invalidSelector, "cel texture selector out of range");
        refused(cmp, indexedMat.substr(0, indexedMat.size() - 1), "truncated");
        auto ambient = indexed;
        ambient.set("lighting", Json::makeString("ambient"));
        for (const auto* command : {"snapshot", "overview"})
        {
            bool rejected = false;
            try { (void)run(root, source, ambient, command, "ambient-fullbright.png"); }
            catch (const sed_agent::AgentError& error) { rejected = error.code == 2; }
            check(rejected && !fs::exists(root / "ambient-fullbright.png"),
                  "indexed ambient plus fullbright refuses before publication even with valid resources");
        }
        refused(cmp.substr(0, cmp.size() - 1), indexedMat, "incomplete CMP: dflt.cmp");
        refused(cmp, mat(255, 0), "indexed8bit MAT 'red.mat'");
        refused(cmp, std::string(16u * 1024u * 1024u + 1, 'X'), "source budget exceeded: red.mat");
    }
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
    // Same normalized basename in two captured archives: the first matching
    // resource still wins when the static model directory is indexed.
    {
        const auto shadow = root / "shadow.gob";
        const auto shadowGob = sed::assets::buildGobBytes({{"alternate\\TRIANGLE.3DO", "invalid shadow model"}});
        write(shadow, shadowGob);
        auto second = Json::makeObject();
        second.set("path", Json::makeString(shadow.string()));
        second.set("sha256", Json::makeString(sed_agent::sha256Hex(shadowGob)));
        auto ordered = archives;
        ordered.array.push_back(second);
        auto duplicate = request;
        duplicate.set("resourceArchives", ordered);
        (void)run(root, source, duplicate, "snapshot", "precedence.png");
        check(sed_agent::readFileBytes(root / "precedence.png") == png,
              "indexed visual lookup preserves first archive match");
    }
    {
        auto district = level;
        district.things.clear();
        std::vector<std::pair<std::string, std::string>> entries;
        const auto meshBytes = sed::assets::saveTo3DO(model);
        for ( int i = 0; i < 129; ++i )
        {
            const auto name = "part" + std::to_string(i) + ".3do";
            entries.emplace_back("3do\\" + name, meshBytes);
            auto part = thing;
            part.values = {sed::deserializeTplValue("model3d", name)};
            district.things.push_back(part);
        }
        const auto parts = root / "parts.gob";
        const auto data = sed::assets::buildGobBytes(entries);
        write(parts, data);
        auto ordered = archives;
        auto additional = Json::makeObject();
        additional.set("path", Json::makeString(parts.string()));
        additional.set("sha256", Json::makeString(sed_agent::sha256Hex(data)));
        ordered.array.push_back(additional);
        auto req = request;
        req.set("resourceArchives", ordered);
        const auto districtPath = root / "district.sed";
        write(districtPath, bytes(sed::assets::saveToSedBytes(district)));
        const auto rendered = run(root, districtPath, req, "snapshot", "district.png");
        check(field(field(rendered, "thingPreview"), "modelsPrepared").number == 129,
              "static snapshot uses indexed profile for 129 distinct small models");
    }
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
    check(report && field(*report, "playerStarts").str == "omit", "player starts are omitted by default");
    {
        request.set("playerStarts", Json::makeString("draw"));
        const auto drawn = run(root, source, request, "snapshot", "starts.png");
        const auto *drawnReport = drawn.find("thingPreview");
        check(drawnReport && field(*drawnReport, "playerStarts").str == "draw", "playerStarts draw is reported");
        for ( const auto &[mode, value, name] : {std::tuple{"static", "\"show\"", "bad-starts.png"},
                                                 std::tuple{"static", "true", "bad-starts2.png"},
                                                 std::tuple{"none", "\"draw\"", "bad-starts3.png"}} )
        {
            request.set("thingMode", Json::makeString(mode));
            request.set("playerStarts", sed_agent::parseJson(value));
            bool refused = false;
            try { (void)run(root, source, request, "snapshot", name); }
            catch ( const sed_agent::AgentError &e ) { refused = e.code == 2; }
            check(refused && !fs::exists(root / name), "bad playerStarts, or playerStarts without static Things, refuses");
        }
        request.set("thingMode", Json::makeString("static"));
        std::erase_if(request.object, [](const auto &entry) { return entry.first == "playerStarts"; });
    }
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
