#include "../agent_workspace.hpp"

#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
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
