#include "../agent_workspace.hpp"

#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures{};

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
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write synthetic export fixture");
    }
}

void writeU32(std::ostream& output, const std::uint32_t value)
{
    for ( unsigned i = 0; i < 4; ++i )
    {
        output.put(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

// Original minimal GOB v2 fixtures; no installed game or licensed assets.
void writeArchive(const fs::path& path,
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::ofstream output(path, std::ios::binary);
    output.write("GOB ", 4);
    writeU32(output, 0x14);
    writeU32(output, 12);
    writeU32(output, static_cast<std::uint32_t>(entries.size()));
    auto offset = 16u + static_cast<std::uint32_t>(entries.size()) * 136u;
    for ( const auto& [name, data] : entries )
    {
        if ( name.size() >= 128 )
        {
            throw std::runtime_error("Synthetic archive name too long");
        }
        writeU32(output, offset);
        writeU32(output, static_cast<std::uint32_t>(data.size()));
        std::array<char, 128> nameBytes{};
        std::copy(name.begin(), name.end(), nameBytes.begin());
        output.write(nameBytes.data(), static_cast<std::streamsize>(nameBytes.size()));
        offset += static_cast<std::uint32_t>(data.size());
    }
    for ( const auto& entry : entries )
    {
        output.write(entry.second.data(), static_cast<std::streamsize>(entry.second.size()));
    }
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write synthetic archive");
    }
}

Json archiveList(const std::vector<fs::path>& paths)
{
    Json result = Json::makeArray();
    for ( const auto& path : paths )
    {
        Json item = Json::makeObject();
        item.set("path", Json::makeString(path.string()));
        item.set("sha256", Json::makeString(sed_agent::sha256Hex(sed_agent::readFileBytes(path))));
        result.array.push_back(std::move(item));
    }
    return result;
}

Json gravityPatch(const std::string& source)
{
    Json request = sed_agent::parseJson(
        R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":5}]})");
    request.set("inputSha256", Json::makeString(sed_agent::sha256Hex(source)));
    return request;
}

const Json* member(const Json* object, const char* name)
{
    return object ? object->find(name) : nullptr;
}

bool number(const Json* value, const double expected)
{
    return value && value->type == Json::Type::Number && value->number == expected;
}

void expectRefusal(const std::string& command, const std::vector<std::string>& args,
    const std::string& code, const std::string& message)
{
    bool refused{};
    try
    {
        (void)sed_agent::runCommand(command, args);
    }
    catch ( const sed_agent::AgentError& error )
    {
        refused = error.code == sed_agent::kExitRefusal && error.errorCode == code;
        if ( !refused )
        {
            std::cerr << "Unexpected refusal: " << error.errorCode << ": " << error.message << '\n';
        }
    }
    check(refused, message);
}

sed::Level emptyLevel()
{
    sed::Level level;
    level.clear();
    level.kind = sed::ProjectType::JKDF2;
    level.header.version = 1;
    level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
    return level;
}

void retainedMaterialCapacity(const fs::path& root)
{
    std::string importedText = sed::assets::buildJklExport(emptyLevel()).text;
    const std::string marker = "WORLD MATERIALS 0\n";
    const auto materialHeader = importedText.find(marker);
    if ( materialHeader == std::string::npos )
    {
        throw std::runtime_error("Synthetic empty JKL material header is missing");
    }
    importedText.replace(materialHeader, marker.size(),
        "WORLD MATERIALS 5\n0: retained-one.mat 1 1\n1: retained-two.mat 1 1\n");
    std::istringstream imported(importedText);
    const auto level = sed::assets::loadFromJKL(imported);
    const auto built = sed::assets::buildJklExport(level);
    check(built.text.find("WORLD MATERIALS 5\n") != std::string::npos,
        "wire capacity retains two authored rows plus three spare slots");
    check(built.buffers.materials.collected == 0 && built.buffers.materials.total == 5,
        "built material capacity includes retained rows independently of dependency count");
    check(sed::assets::inspectJklExportBuffers(level).materials.total == 5,
        "read-only material capacity equals the actual wire capacity");

    const auto source = root / "retained.jkl";
    const auto requestPath = root / "retained.json";
    const auto output = root / "retained-output.jkl";
    writeText(source, built.text);
    writeText(requestPath, sed_agent::toJsonString(gravityPatch(built.text)));
    const auto plan = sed_agent::runCommand("plan", { source.string(), requestPath.string() });
    const auto* materials = member(member(&plan, "exportBuffers"), "materials");
    check(number(member(materials, "collected"), 0) && number(member(materials, "total"), 5),
        "agent material receipt describes retained rows plus spare wire capacity");
    const auto* hash = plan.find("outputSha256");
    check(hash && hash->type == Json::Type::String, "material plan has a reviewed hash");
    if ( !hash )
    {
        return;
    }
    (void)sed_agent::runCommand("apply",
        { source.string(), requestPath.string(), output.string(), hash->str });
    check(sed_agent::readFileBytes(output).find("WORLD MATERIALS 5\n") != std::string::npos,
        "applied material header agrees with the plan capacity");
}

void cogWinnerIdentity(const fs::path& root)
{
    const std::string selected = "symbols\nmaterial skin=selected.mat local\nend\ncode\nend\n";
    const std::string other = "symbols\nmaterial skin=other.mat local\nend\ncode\nend\n";
    const auto source = root / "aliases.jkl";
    writeText(source, sed::assets::buildJklExport(emptyLevel()).text);
    const auto archive = root / "aliases.gob";
    const auto second = root / "second.gob";
    const auto requestPath = root / "aliases.json";
    const auto refusedOutput = root / "aliases-output.jkl";
    Json request = sed_agent::parseJson(R"({"schemaVersion":1,"script":"same.cog","bindings":[]})");
    request.set("scriptSha256", Json::makeString(sed_agent::sha256Hex(selected)));
    writeArchive(archive, { { "cog/same.cog", selected }, { "same.cog", other } });
    request.set("resourceArchives", archiveList({ archive }));
    writeText(requestPath, sed_agent::toJsonString(request));
    expectRefusal("addCog", { source.string(), requestPath.string(), refusedOutput.string() },
        "exportCogResourceAmbiguous", "schema-prefixed and export-bare conflicting COG aliases refuse");
    check(!fs::exists(refusedOutput), "conflicting COG payloads publish no output");

    writeArchive(archive, { { "cog/same.cog", selected } });
    writeArchive(second, { { "cog/same.cog", other } });
    request.set("resourceArchives", archiveList({ archive, second }));
    writeText(requestPath, sed_agent::toJsonString(request));
    const auto consistent = sed_agent::runCommand("addCog", { source.string(), requestPath.string() });
    check(number(member(member(member(&consistent, "exportBuffers"), "materials"), "collected"), 1),
        "consistent first-archive winner remains admitted when lower-priority bytes differ");

    writeArchive(archive, { { "cog/same.cog", selected }, { "same.cog", selected } });
    request.set("resourceArchives", archiveList({ archive }));
    writeText(requestPath, sed_agent::toJsonString(request));
    const auto identical = sed_agent::runCommand("addCog", { source.string(), requestPath.string() });
    check(number(member(member(member(&identical, "exportBuffers"), "materials"), "collected"), 1),
        "identical prefixed and bare payloads remain admitted");
}

void importedCogTemplateArgument(const fs::path& root)
{
    const auto archive = root / "template-argument.gob";
    writeArchive(archive, { { "cog/spawn.cog", "symbols\ntemplate spawned=default_template\nend\n" } });
    sed::assets::ResourceLocator resources;
    resources.mountGob(archive);
    sed::assets::JklSaveOptions options;
    options.resources = &resources;
    auto level = emptyLevel();
    for ( const char* name : { "default_template", "selected_template" } )
    {
        sed::ThingTemplate item;
        item.name = name;
        item.parent = "none";
        level.templates.push_back(std::move(item));
    }
    auto& cog = level.newCog();
    cog.name = "spawn.cog";
    sed::CogValue value;
    value.name = "spawned";
    value.type = sed::ValueType::str;
    value.cogType = sed::CogType::template_;
    value.stringValue = "selected_template";
    cog.values.push_back(value);
    const auto built = sed::assets::buildJklExport(level, options);
    check(built.buffers.templates.collected == 2, "fixture includes default and instance template references");
    const auto source = root / "template-argument.jkl";
    const auto requestPath = root / "template-argument.json";
    const auto output = root / "template-argument-output.jkl";
    writeText(source, built.text);
    auto request = gravityPatch(built.text);
    request.set("resourceArchives", archiveList({ archive }));
    writeText(requestPath, sed_agent::toJsonString(request));
    const auto plan = sed_agent::runCommand("plan", { source.string(), requestPath.string() });
    check(number(member(member(member(&plan, "exportBuffers"), "templates"), "collected"), 2),
        "raw imported COG instance template argument remains an export dependency");
    const auto* hash = plan.find("outputSha256");
    if ( !hash )
    {
        check(false, "template plan has a reviewed hash");
        return;
    }
    (void)sed_agent::runCommand("apply", { source.string(), requestPath.string(), output.string(), hash->str });
    const auto reopened = sed::assets::loadFromJKL(output);
    check(reopened.templates.size() == 2
        && std::any_of(reopened.templates.begin(), reopened.templates.end(),
            [](const auto& item) { return item.name == "selected_template"; }),
        "actual published JKL retains the instance-selected template");
    check(sed_agent::readFileBytes(source) == built.text, "export hydration leaves source bytes unchanged");
}

void strictCogReferencePreservation(const fs::path& root)
{
    const sed::assets::RgbaImage image{ 16, 16, std::vector<std::uint8_t>(16 * 16 * 4, 255) };
    const auto mat = sed::assets::buildIndexedMat({ image }, sed::assets::makeGrayscalePalette());
    auto level = emptyLevel();
    (void)sed::level::levelCreateCube(level, -1, -1, -1, { 0, 0, 1 }, { 2, 0, 0 });
    level.materials = { "tile.mat" };
    for ( auto& surface : level.sectors.front().surfaces )
    {
        surface.material = "tile.mat";
        surface.nMat = 0;
    }
    level.sectors.front().surfaces.front().texVertices.front().u = 13.25;
    level.sectors.front().surfaces.front().texVertices.front().v = -3.5;
    level.newCog().name = "reference.cog";
    const auto archive = root / "reference.gob";
    const auto source = root / "reference.jkl";
    const auto requestPath = root / "reference.json";
    struct ReferenceCase
    {
        const char* declaration;
        const char* argument;
        bool refuse;
        const char* expected;
    };
    std::size_t caseIndex{};
    for ( const auto& test : std::array{
        ReferenceCase{ "surface target", "999", true, "" },
        ReferenceCase{ "surface target", "0x3e7", true, "" },
        ReferenceCase{ "surface target", "-2", true, "" },
        ReferenceCase{ "sector target", "-2", true, "" },
        ReferenceCase{ "thing target", "-2", true, "" },
        ReferenceCase{ "surface target=999", nullptr, true, "" },
        // Positive out-of-range non-surface references are retained by the
        // ordinary parser, not repaired. Strict hydration is not validation.
        ReferenceCase{ "sector target", "999", false, "999" },
        ReferenceCase{ "thing target", "999", false, "999" },
        ReferenceCase{ "cog target", "999", false, "999" },
        ReferenceCase{ "surface target", "0x2", false, "2" },
        ReferenceCase{ "surface target", "-1", false, "-1" },
        ReferenceCase{ "surface target=-1", nullptr, false, "-1" } } )
    {
        auto candidate = level;
        if ( test.argument )
        {
            sed::CogValue raw;
            raw.name = "param0";
            raw.type = sed::ValueType::str;
            raw.cogType = sed::CogType::unk;
            raw.stringValue = test.argument;
            candidate.cogs.front().values.push_back(raw);
        }
        const auto text = sed::assets::buildJklExport(candidate).text;
        writeText(source, text);
        writeArchive(archive, {
            { "cog/reference.cog", std::string("symbols\n") + test.declaration + "\nend\n" },
            { "mat/tile.mat", mat.bytes } });
        auto request = gravityPatch(text);
        request.set("resourceArchives", archiveList({ archive }));
        writeText(requestPath, sed_agent::toJsonString(request));
        const std::string label = std::string(test.declaration) + ": "
            + (test.argument ? test.argument : "omitted argument");
        auto parsed = sed::assets::loadFromJKL(source);
        sed::assets::ResourceLocator resources;
        resources.mountGob(archive);
        if ( test.refuse )
        {
            const auto before = sed::assets::saveToSedBytes(parsed);
            check(!sed::assets::finalizeJklImportCogValues(parsed, resources, true)
                && sed::assets::saveToSedBytes(parsed) == before,
                "strict hydration refuses before changing state for " + label);
            expectRefusal("plan", { source.string(), requestPath.string() },
                "exportCogReferenceUnresolved", "agent refuses implicit reference repair for " + label);
        }
        else
        {
            const auto plan = sed_agent::runCommand("plan", { source.string(), requestPath.string() });
            const auto output = root / ("reference-output-" + std::to_string(caseIndex) + ".jkl");
            (void)sed_agent::runCommand("apply", { source.string(), requestPath.string(), output.string(),
                plan.find("outputSha256")->str });
            const auto result = sed::assets::loadFromJKL(output);
            check(result.cogs.front().values.front().stringValue == test.expected,
                "reference that requires no repair keeps its target for " + label);
            const auto& originalSurface = parsed.sectors.front().surfaces.front();
            const auto& resultSurface = result.sectors.front().surfaces.front();
            check(result.uvCoordinateSpace == parsed.uvCoordinateSpace
                && resultSurface.texVertices.front().u == originalSurface.texVertices.front().u
                && resultSurface.texVertices.front().v == originalSurface.texVertices.front().v,
                "COG-only hydration does not normalize UVs through available MATs");
            check(sed::assets::finalizeJklImportCogValues(parsed, resources, true)
                && parsed.uvCoordinateSpace == sed::UvCoordinateSpace::TexturePixels
                && parsed.sectors.front().surfaces.front().texVertices.front().u == 13.25
                && parsed.sectors.front().surfaces.front().texVertices.front().v == -3.5,
                "core COG-only hydration preserves authored pixel UV values and tag");
        }
        check(sed_agent::readFileBytes(source) == text, "reference export leaves source unchanged for " + label);
        ++caseIndex;
    }

    level.cogs.clear();
    level.newCog().name = "first.cog";
    level.newCog().name = "second.cog";
    writeArchive(archive, {
        { "cog/first.cog", "symbols\nsurface target=2\nend\n" },
        { "cog/second.cog", "symbols\nsurface target=999\nend\n" } });
    std::istringstream sourceBytes(sed::assets::buildJklExport(level).text);
    auto staged = sed::assets::loadFromJKL(sourceBytes);
    const auto before = sed::assets::saveToSedBytes(staged);
    sed::assets::ResourceLocator resources;
    resources.mountGob(archive);
    check(!sed::assets::finalizeJklImportCogValues(staged, resources, true)
        && sed::assets::saveToSedBytes(staged) == before,
        "later reference refusal does not publish earlier staged COG defaults");
    check(sed::assets::finalizeJklImportCogValues(staged, resources)
        && staged.cogs[0].values[0].intValue == 2 && staged.cogs[1].values[0].intValue == -1,
        "ordinary import retains established recovery of invalid references");
}
void runtimeArchiveAuthoring(const fs::path& root)
{
    const auto source = root / "authored-cogStrings.uni";
    const auto requestPath = root / "runtime-archive.json";
    const auto output = root / "runtime-names.gob";
    const std::string sourceBytes = "COG_96001 unchanged synthetic text\n";
    writeText(source, sourceBytes);
    Json entry = Json::makeObject();
    entry.set("name", Json::makeString("MiSc/cogStrings.uni"));
    entry.set("path", Json::makeString(source.string()));
    entry.set("sha256", Json::makeString(sed_agent::sha256Hex(sourceBytes)));
    Json request = Json::makeObject();
    request.set("schemaVersion", Json::makeNumber(1));
    Json entries = Json::makeArray();
    entries.array.push_back(entry);
    request.set("entries", entries);
    writeText(requestPath, sed_agent::toJsonString(request));
    const auto receipt = sed_agent::runCommand("archive-create", { requestPath.string(), output.string() });
    const auto archive = sed::assets::GobArchive::open(output);
    check(archive.entries().size() == 1 && archive.entries().front().name == "misc\\cogstrings.uni",
        "archive-create emits physical names reachable by lowercase engine lookup");
    const auto payload = archive.read("misc/cogstrings.uni");
    check(std::string(reinterpret_cast<const char*>(payload.data()), payload.size()) == sourceBytes
            && sed_agent::readFileBytes(source) == sourceBytes,
        "archive-create canonicalizes names without changing source or payload bytes");
    const auto* rows = receipt.find("entries");
    check(rows && rows->array.size() == 1 && rows->array.front().find("name")
            && rows->array.front().find("name")->str == "misc\\cogstrings.uni",
        "archive-create receipt names the actual canonical physical row");

    entry.set("name", Json::makeString("misc\\COGSTRINGS.UNI"));
    entries.array.push_back(entry);
    request.set("entries", entries);
    writeText(requestPath, sed_agent::toJsonString(request));
    const auto duplicateOutput = root / "runtime-duplicate.gob";
    expectRefusal("archive-create", { requestPath.string(), duplicateOutput.string() },
        "duplicateArchiveEntry", "archive-create still refuses case-colliding physical destinations");
    check(!fs::exists(duplicateOutput), "archive-create case collision creates no partial archive");
}
} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("red-export-reconciliation-" + std::to_string(nonce));
    fs::create_directories(root);
    const std::array tests{ retainedMaterialCapacity, cogWinnerIdentity, importedCogTemplateArgument,
        strictCogReferencePreservation, runtimeArchiveAuthoring };
    for ( const auto test : tests )
    {
        try
        {
            test(root);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(false, "unexpected agent error: " + error.errorCode + ": " + error.message);
        }
        catch ( const std::exception& error )
        {
            check(false, std::string("unexpected test error: ") + error.what());
        }
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return failures == 0 ? 0 : 1;
}
