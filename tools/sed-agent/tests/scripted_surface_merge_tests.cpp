#include "../agent_workspace.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/editor/document.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/merge.hpp"
#include "sed/level/recipes.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using sed::SurfaceRef;
using sed_agent::Json;

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

sed::CogValue surfaceArgument(const sed::Level& level, const std::string& name, const SurfaceRef ref)
{
    sed::CogValue value{};
    value.name = name;
    value.type = sed::ValueType::ptr;
    value.cogType = sed::CogType::surface;
    value.intValue = level.getGlobalSurfaceNumber(ref.sector, ref.surface);
    value.description = "Retain this authored link.";
    value.mask = 0x14;
    return value;
}

sed::Level scriptedRooms()
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;
    level.layers = {"Main"};
    level.lvisstring = "1";
    level.materials = {"stone.mat", "door-a.mat", "switch.mat", "wrong-door.mat"};
    for (int sector = 0; sector < 3; ++sector)
    {
        sed::level::levelCreateCube(level, 4.0 * sector, 0, 0, {0,0,1}, {2,0,0});
        for (auto& face : level.sectors.back().surfaces)
        {
            face.nMat = 0;
            face.material = level.materials[0];
        }
    }
    for (const auto [sector, surface, material] : {std::array{1,0,1}, std::array{2,0,2}, std::array{2,3,3}})
    {
        auto& face = level.sectors[sector].surfaces[surface];
        face.nMat = material;
        face.material = level.materials[material];
    }
    const std::array<SurfaceRef, 2> split{{{0,0},{1,0}}};
    expect(sed::level::levelSubdivideSurfaces(level, split) == 2, "two independently mergeable faces");
    expect(level.sectors[0].surfaces.size() == 9 && level.sectors[1].surfaces.size() == 9,
        "each fan appends exactly three triangles to the starter cube");
    sed::Cog cog{};
    cog.name = "paired-switch.cog";
    cog.values = {surfaceArgument(level,"door",{1,8}), surfaceArgument(level,"button",{2,0})};
    auto scalar = cog.values[1];
    scalar.name = "count";
    scalar.type = sed::ValueType::integer;
    scalar.cogType = sed::CogType::int_;
    cog.values.push_back(scalar);
    level.cogs.push_back(std::move(cog));
    level.things.emplace_back().sector = 2;
    level.things.back().archLightingIndex = 0;
    level.motsArchLighting.emplace_back().wireIndex = 31;
    return level;
}

std::string operation(const int room)
{
    return "{\"op\":\"mergeSurfaces\",\"surfaces\":[[" + std::to_string(room) + ",0],[" +
        std::to_string(room) + ",6],[" + std::to_string(room) + ",7],[" + std::to_string(room) + ",8]]}";
}

const Json& member(const Json& object, const char* name)
{
    const auto* field = object.find(name);
    expect(field != nullptr, std::string("receipt missing ") + name);
    return *field;
}

void writePlan(const fs::path& path, const int room)
{
    std::ofstream out(path);
    out << "{\"schemaVersion\":1,\"operations\":[" << operation(room) << "]}";
    expect(out.good(), "write review request");
}

void sectorLifecycle(const fs::path& root)
{
    sed::Level seed{};
    seed.kind = sed::ProjectType::MOTS;
    seed.header.version = 1;
    seed.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
    seed.layers = {"Main"};
    seed.lvisstring = "1";
    auto& room = sed::level::levelCreateCube(seed, 0, 0, 0, {0,0,1}, {2,0,0});
    room.id = 9001;
    const auto engineFile = root/"identity.jkl";
    sed::assets::saveToJKL(seed, engineFile);
    const auto imported = sed::assets::loadFromJKL(engineFile);
    expect(imported.sectors.size() == 1 && imported.sectors[0].id > 0 && imported.sectors[0].id != 9001,
        "engine import assigns a native sector ID independent of the previous native instance");
    const auto originalId = imported.sectors[0].id;
    auto current = root/"identity.sed";
    sed::assets::saveToSED(imported, current);
    auto perform = [&](const std::string& label, const std::string& operation)
    {
        const auto patch = root/(label+".json");
        const auto output = root/(label+".sed");
        { std::ofstream request(patch); request << "{\"schemaVersion\":1,\"operations\":[" << operation << "]}"; }
        const auto plan = sed_agent::runCommand("plan",{current.string(),patch.string()});
        const auto& row = member(plan,"editReports").array.at(0);
        const auto map = member(row,"sectorLineage");
        expect(member(map,"kind").str == "sectorLineageV1", "operation records a sector correspondence");
        sed_agent::runCommand("apply",{current.string(),patch.string(),output.string(),member(plan,"outputSha256").str});
        current = output;
        return map;
    };
    const auto duplicate = perform("duplicate",R"({"op":"duplicateSector","sector":0,"offset":[12,0,0]})");
    const auto two = sed::assets::loadFromSED(current);
    const auto copyId = two.sectors.at(1).id;
    expect(two.sectors.at(0).id == originalId && copyId != originalId && copyId > 0
            && member(member(duplicate,"inputs").array.at(0),"nativeId").number == originalId
            && member(member(duplicate,"outputs").array.at(1),"nativeId").number == copyId,
        "duplication keeps the imported owner and allocates its independent copy a new identity");
    const auto centre = sed::level::sectorCalcCenter(two.sectors[0]);
    const auto split = perform("split","{\"op\":\"cleaveSector\",\"sector\":0,\"normal\":[1,0,0],\"point\":["
        + std::to_string(centre.x + 0.1) + ",0,0]}");
    const auto three = sed::assets::loadFromSED(current);
    const auto splitId = three.sectors.at(2).id;
    expect(three.sectors.at(0).id == originalId && three.sectors.at(1).id == copyId && splitId > 0
            && splitId != originalId && splitId != copyId
            && member(member(split,"outputs").array.at(0),"nativeId").number == originalId
            && member(member(split,"outputs").array.at(1),"nativeId").number == splitId,
        "split retains the original body and identifies its newly allocated child across native Save/Open");
    const auto merged = perform("merge",R"({"op":"mergeSectors","sector":0,"otherSector":2})");
    const auto back = sed::assets::loadFromSED(current);
    expect(back.sectors.size() == 2 && back.sectors[0].id == originalId && back.sectors[1].id == copyId
            && member(member(merged,"inputs").array.at(1),"nativeId").number == splitId
            && member(member(merged,"outputs").array.at(0),"nativeId").number == originalId,
        "merge retires the split child's native ID and preserves the independently duplicated sector");

    sed::editor::Document history({three, {}});
    expect(history.applyUndoable("Merge split child",sed::editor::DocumentDirty::Structure,
        [](sed::editor::DocumentState& state) { return sed::level::levelMergeSectors(state.level,0,2).has_value(); })
            && history.undo() && history.current().level.sectors.at(2).id == splitId
            && history.redo() && history.current().level.sectors.size() == 2
            && history.current().level.sectors[0].id == originalId,
        "native history restores and retires the same child identity when navigating the merge");

    auto collision = two;
    collision.sectors[1].id = originalId;
    current = root/"legacy-id.sed";
    sed::assets::saveToSED(collision,current);
    const auto legacy = perform("duplicate-legacy",R"({"op":"duplicateSector","sector":1,"offset":[12,0,0]})");
    const auto& ambiguous = member(legacy,"inputs").array.at(0);
    expect(member(ambiguous,"nativeId").type == Json::Type::Null
            && member(ambiguous,"reason").str == "duplicateId" && member(ambiguous,"slot").number == 1,
        "a duplicate legacy ID is explicit and cannot masquerade as durable identity");
}

void verify(const fs::path& root)
{
    fs::create_directory(root/"cog");
    {
        std::ofstream script(root/"cog"/"paired-switch.cog");
        script << "symbols\nsurface door\nsurface button\nint count\nmessage activated\nend\n"
                  "code\nactivated:\nif (GetSenderRef() == button) SetWallCel(door, 1);\nreturn;\nend\n";
        expect(script.good(), "write the paired switch script beside the native level");
    }
    auto source = scriptedRooms();
    auto current = root / "source.sed";
    sed::assets::saveToSED(source, current);
    source = sed::assets::loadFromSED(current);
    const int staleSwitch = source.cogs[0].values[1].intValue;
    for (const int sector : {0,1})
    {
        const auto patch = root / ("patch-" + std::to_string(sector) + ".json");
        const auto output = root / (sector == 0 ? "unrelated.sed" : "agent.sed");
        writePlan(patch, sector);
        const auto plan = sed_agent::runCommand("plan", {current.string(),patch.string()});
        const auto& reports = member(plan, "editReports").array;
        expect(reports.size() == 1, "one merge receipt");
        const auto& topology = member(reports.front(), "topology");
        const auto& before = member(topology, "before");
        const auto& proposed = member(topology, "after");
        expect(member(before, "sectors").number == source.sectors.size()
            && member(proposed, "sectors").number == source.sectors.size(),
            "surface merge keeps all sector slots");
        expect(member(before, "surfaces").number - member(proposed, "surfaces").number
            == 3, "topology counts reflect three solid face removals");
        expect(member(before, "adjoinedFaces").number - member(proposed, "adjoinedFaces").number
            == 0, "solid face operations do not count as portal reductions");
        expect(!member(topology, "scriptCodeAnalyzed").boolean,
            "typed reference review does not claim to analyze script code");
        const auto& map = member(reports.front(), "referenceMap");
        expect(member(map,"kind").str == "surfaceMergeV1" && member(map,"steps").array.size() == 3,
            "three ordered topology steps map the complete original fan to its final survivor");
        const auto& bindings = member(map,"surfaceCogBindings").array;
        expect(bindings.size() == 2, "the map lists both typed bindings, excluding the ordinary integer");
        const auto sourceBytes = sed_agent::readFileBytes(current);
        sed_agent::runCommand("apply", {current.string(), patch.string(), output.string(), member(plan,"outputSha256").str});
        expect(sourceBytes == sed_agent::readFileBytes(current), "the reviewed source survives publication");
        const auto after = sed::assets::loadFromSED(output);
        const auto& cog = after.cogs.at(0);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto address = after.tryGetSurfaceRefByGlobalSurfaceNumber(cog.values.at(i).intValue);
            expect(address.has_value(), "the reloaded script argument resolves");
            const auto& face = after.sectors.at(address->sector).surfaces.at(address->surface);
            expect(face.material == (i == 0 ? "door-a.mat" : "switch.mat"), "the script still targets the authored door/switch");
            const auto& row = bindings[i];
            const bool addressChanged = source.cogs[0].values[i].intValue != cog.values[i].intValue
                || member(row, "oldSurface").array.at(0).number != address->sector
                || member(row, "oldSurface").array.at(1).number != address->surface;
            expect(member(row, "addressChanged").boolean == addressChanged,
                "receipt distinguishes numeric address changes from unchanged slots");
            expect(member(row,"cog").number == 0 && member(row,"argument").number == i
                    && member(row,"oldGlobal").number == source.cogs[0].values[i].intValue
                    && member(row,"newGlobal").number == cog.values[i].intValue
                    && member(row,"newSurface").array.at(0).number == address->sector
                    && member(row,"newSurface").array.at(1).number == address->surface
                    && cog.values[i].mask == source.cogs[0].values[i].mask && cog.values[i].description == "Retain this authored link.",
                "receipt, persisted target and authored COG metadata agree");
        }
        expect(cog.values[2].intValue == staleSwitch && after.things.at(0).sector == 2
                && after.things[0].archLightingIndex == 0 && after.motsArchLighting == source.motsArchLighting,
            "scalar integers, Thing sector and ARCH associations keep their meanings");
        if (sector == 0)
        {
            const auto wrong = after.tryGetSurfaceRefByGlobalSurfaceNumber(staleSwitch);
            expect(wrong && after.sectors[wrong->sector].surfaces[wrong->surface].material == "wrong-door.mat",
                "retaining the old number would have activated the explicit wrong door");
        }
        current = output;
        source = after;
    }

    auto opaque = scriptedRooms();
    opaque.cogs[0].values[0].cogType = sed::CogType::unk;
    const auto refused = root / "opaque.sed";
    sed::assets::saveToSED(opaque, refused);
    const auto inputBytes = sed_agent::readFileBytes(refused);
    try
    {
        sed_agent::runCommand("plan", {refused.string(), (root/"patch-0.json").string()});
        throw std::runtime_error("an opaque surface argument was accepted");
    }
    catch (const sed_agent::AgentError& e)
    {
        expect(e.errorCode == "parityOpaqueSurfaceReference" && e.message.find("paired-switch.cog") != std::string::npos
                && e.message.find("argument 0 (door)") != std::string::npos, "refusal identifies the blocking script and argument");
    }
    expect(sed_agent::readFileBytes(refused) == inputBytes, "opaque refusal preserves all source bytes");
    const auto splitPlan = root/"ambiguous-split.json";
    auto splitControl = scriptedRooms();
    const auto& sector = splitControl.sectors[1];
    const auto& face = sector.surfaces[8];
    sed::TVector centre{};
    expect(sed::level::surfaceCalcCenter(sector, face, centre), "the door fan has a valid centre");
    const auto& a = sector.vertices[face.vertices[0]];
    const auto& b = sector.vertices[face.vertices[1]];
    const sed::TVector normal{b.x-a.x,b.y-a.y,b.z-a.z};
    splitControl.cogs.clear();
    expect(sed::level::levelCleaveSurface(splitControl,{1,8},normal,centre.x,centre.y,centre.z),
        "the same cut really splits the designated door face without a scripted binding");
    {
        std::ofstream out(splitPlan);
        out << R"({"schemaVersion":1,"operations":[{"op":"cleaveSurface","sector":1,"surface":8,"normal":[)"
            << normal.x << ',' << normal.y << ',' << normal.z << "],\"point\":["
            << centre.x << ',' << centre.y << ',' << centre.z << "]}]}";
    }
    try
    {
        sed_agent::runCommand("plan", {(root/"source.sed").string(),splitPlan.string()});
        throw std::runtime_error("a one-to-many scripted target was accepted without resolving its descendant");
    }
    catch (const sed_agent::AgentError& e)
    {
        expect(e.errorCode == "basicCleaveCogBindings" && e.message.find("argument 0 (door)") != std::string::npos,
            "ambiguous split identifies the exact affected door initializer");
    }
    sectorLifecycle(root);
}
}

int main(int argc, char** argv)
{
    const bool keep = argc == 3 && std::string(argv[1]) == "--write-fixture";
    if (argc != 1 && !keep) return 2;
    const auto root = keep ? fs::path(argv[2]) : fs::temp_directory_path() /
        ("red-scripted-surface-merge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        expect(fs::create_directory(root), "fixture destination must be a new directory");
        verify(root);
        if (!keep) fs::remove_all(root);
        return 0;
    }
    catch (const sed_agent::AgentError& e) { std::cerr << e.errorCode << " " << e.message << '\n'; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
