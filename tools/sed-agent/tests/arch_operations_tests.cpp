#include "../agent_arch_operations.hpp"
#include "../agent_workspace.hpp"

#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level/arch_validation.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures = 0;
int fixtureNumber = 0;

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void writeText(const fs::path& path, const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if ( !output )
    {
        throw std::runtime_error("cannot write synthetic ARCH fixture");
    }
}

std::vector<std::byte> nativeBytes(const sed::Level& level)
{
    std::vector<sed::assets::SedOptionalSection> sections;
    if ( const auto rgb = sed::assets::makeSedLightRgbSection(level) )
    {
        sections.push_back(*rgb);
    }
    if ( const auto arch = sed::assets::makeSedArchAuthoringSection(level) )
    {
        sections.push_back(*arch);
    }
    return sed::assets::saveToSedBytes(level, sections);
}

void writeNative(const fs::path& path, const sed::Level& level)
{
    const auto bytes = nativeBytes(level);
    writeText(path, { reinterpret_cast<const char*>(bytes.data()), bytes.size() });
}

sed::Level readNative(const fs::path& path)
{
    const auto text = sed_agent::readFileBytes(path);
    std::vector<std::byte> bytes(text.size());
    std::transform(text.begin(), text.end(), bytes.begin(), [](const char c)
    {
        return static_cast<std::byte>(c);
    });
    auto file = sed::assets::loadSedFileBytes(bytes);
    (void)sed::assets::applySedLightRgbSection(file.level, file.optionalSections);
    (void)sed::assets::applySedArchAuthoringSection(file.level, file.optionalSections);
    return file.level;
}

// Complete original synthetic 3DOs, including every declared LOD. The first
// source vertex is the origin; the hierarchy may move it one unit along X.
std::string modelText(const std::vector<std::size_t>& lodVertices = { 3 },
    const bool translatedNode = false, const bool sharedNode = false)
{
    std::string text = "SECTION: HEADER\n3DO 2.1\nSECTION: MODELRESOURCE\nMATERIALS 1\n"
        "0: arch_fixture.mat\nSECTION: GEOMETRYDEF\nRADIUS 4\nINSERT OFFSET 0 0 0\nGEOSETS "
        + std::to_string(lodVertices.size()) + "\n";
    for ( std::size_t lod = 0; lod < lodVertices.size(); ++lod )
    {
        text += "GEOSET " + std::to_string(lod) + "\nMESHES 1\nMESH 0\nNAME probe\n"
            "RADIUS 4\nGEOMETRYMODE 4\nLIGHTINGMODE 3\nTEXTUREMODE 1\nVERTICES "
            + std::to_string(lodVertices[lod]) + "\n";
        for ( std::size_t vertex = 0; vertex < lodVertices[lod]; ++vertex )
        {
            text += std::to_string(vertex) + ": "
                + (vertex == 1 ? "1 0 0" : vertex == 2 ? "0 1 0" : "0 0 0")
                + " 0.25\n";
        }
        text += "TEXTURE VERTICES 3\n0: 0 0\n1: 16 0\n2: 0 16\nVERTEX NORMALS\n";
        for ( std::size_t vertex = 0; vertex < lodVertices[lod]; ++vertex )
        {
            text += std::to_string(vertex) + ": 0 0 1\n";
        }
        text += "FACES 1\n0: 0 0x0 4 3 1 0 3 0, 0 1, 1 2, 2\n"
            "FACE NORMALS\n0: 0 0 1\n";
    }
    text += "SECTION: HIERARCHYDEF\nHIERARCHY NODES "
        + std::to_string(sharedNode ? 2 : 1) + "\n"
        "0: 0x0 0x1 0 -1 -1 -1 0 "
        + std::string(translatedNode ? "1" : "0")
        + " 0 0 0 0 0 0 0 0 probe\n";
    if ( sharedNode )
    {
        text += "1: 0x0 0x1 0 -1 -1 -1 0 2 0 0 0 0 0 0 0 0 clone\n";
    }
    return text;
}

sed::Level fixture(const fs::path& root)
{
    const auto starter = root / "arch-starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    auto level = sed::assets::loadFromJKL(starter);
    sed::ThingTemplate parent{};
    parent.name = "arch_parent";
    parent.parent = "none";
    parent.values.push_back(sed::deserializeTplValue("thingflags", "0x8"));
    parent.values.push_back(sed::deserializeTplValue("model3d", "arch_fixture.3do"));
    level.templates.push_back(parent);
    sed::ThingTemplate child{};
    child.name = "arch_child";
    child.parent = "arch_parent";
    level.templates.push_back(child);
    for ( const std::string name : { "target", "sharer" } )
    {
        auto& thing = level.newThing();
        thing.name = name;
        thing.templateName = "arch_child";
        thing.sector = 0;
        thing.archLightingIndex = 0;
    }
    sed::MotsArchLightObject object{};
    object.wireIndex = 7;
    sed::MotsArchLightMesh mesh{};
    mesh.wireIndex = 0;
    mesh.vertices = {
        { -0.125f, 2.5f, -0.0f, 0.375f },
        { 0.25f, 0.5f, 0.75f, 1.0f },
        { 0.5f, 0.125f, 0.25f, 0.75f } };
    object.meshes.push_back(mesh);
    level.motsArchLighting.push_back(object);
    auto& light = level.newLight();
    light.position = { 0.0, 0.0, 1.0 };
    light.range = 2.0;
    light.intensity = 8.0;
    light.rgbIntensity = 4.0;
    light.color = { 0.5f, 0.25f, 1.0f, 1.0f };
    return level;
}

struct RequestFiles
{
    fs::path source;
    fs::path archive;
    fs::path request;
    fs::path output;
    std::string sourceBytes;
    std::string archiveBytes;
};

RequestFiles requestFiles(const fs::path& root, const sed::Level& level,
    const std::string& operations, const std::string& model = modelText(),
    const std::string& entryName = "3do\\arch_fixture.3do")
{
    const auto stem = "arch-" + std::to_string(++fixtureNumber);
    RequestFiles files{ root / (stem + ".sed"), root / (stem + ".gob"),
        root / (stem + ".json"), root / (stem + "-edited.sed"), {}, {} };
    writeNative(files.source, level);
    files.sourceBytes = sed_agent::readFileBytes(files.source);
    files.archiveBytes = sed::assets::buildGobBytes({ { entryName, model } });
    writeText(files.archive, files.archiveBytes);
    const auto hash = sed_agent::sha256Hex(files.sourceBytes);
    auto patch = sed_agent::parseJson("{\"schemaVersion\":1,\"operations\":[" + operations + "]}");
    patch.set("inputSha256", Json::makeString(hash));
    patch.set("sourceRevision", Json::makeString(hash));
    auto archive = Json::makeObject();
    archive.set("path", Json::makeString(files.archive.string()));
    archive.set("sha256", Json::makeString(sed_agent::sha256Hex(files.archiveBytes)));
    auto archives = Json::makeArray();
    archives.array.push_back(std::move(archive));
    patch.set("resourceArchives", std::move(archives));
    writeText(files.request, sed_agent::toJsonString(patch));
    return files;
}

sed::Level apply(const RequestFiles& files)
{
    const auto planned = sed_agent::runCommand("plan", { files.source.string(), files.request.string() });
    const auto* hash = planned.find("outputSha256");
    if ( !hash || hash->type != Json::Type::String )
    {
        throw std::runtime_error("ARCH plan omitted outputSha256");
    }
    check(!fs::exists(files.output), "ARCH planning writes no output");
    (void)sed_agent::runCommand("apply",
        { files.source.string(), files.request.string(), files.output.string(), hash->str });
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(files.output)) == hash->str,
        "ARCH apply matches the reviewed plan bytes");
    check(sed_agent::readFileBytes(files.source) == files.sourceBytes,
        "ARCH edit preserves source file bytes");
    check(sed_agent::readFileBytes(files.archive) == files.archiveBytes,
        "ARCH edit preserves captured archive bytes");
    return readNative(files.output);
}

void expectRefusal(const RequestFiles& files, const std::string& code,
    const int exitCode = sed_agent::kExitRefusal)
{
    try
    {
        (void)sed_agent::runCommand("apply",
            { files.source.string(), files.request.string(), files.output.string() });
        check(false, "ARCH operation must refuse with " + code);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == exitCode && (code.empty() || error.errorCode == code),
            "ARCH expected " + code + " exit " + std::to_string(exitCode)
                + ", received " + error.errorCode + " exit " + std::to_string(error.code)
                + ": " + error.message);
    }
    check(!fs::exists(files.output), "refused ARCH patch publishes no output");
    check(sed_agent::readFileBytes(files.source) == files.sourceBytes,
        "refused ARCH patch leaves source bytes intact");
}

void binding(const fs::path& root, const sed::Level& level)
{
    const auto files = requestFiles(root, level,
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})");
    const auto result = apply(files);
    check(result.motsArchLighting.size() == 2 && result.things[0].archLightingIndex == 1
            && result.things[1].archLightingIndex == 0,
        "binding makes a private copy and leaves the other owner bound to its original");
    check(result.motsArchLighting.at(0) == level.motsArchLighting.at(0)
            && result.motsArchLighting.at(1).meshes == level.motsArchLighting.at(0).meshes,
        "binding retains imported mono, HDR and RGB samples exactly");
    check(std::bit_cast<std::uint32_t>(result.motsArchLighting.at(1).meshes.at(0).vertices.at(0).green)
            == 0x80000000u,
        "binding retains the authored negative-zero channel");
    check(result.motsArchAuthoring.size() == 1
            && result.motsArchAuthoring[0].origin == sed::MotsArchSampleOrigin::BoundExistingSamples
            && result.motsArchAuthoring[0].modelName == "arch_fixture.3do"
            && result.motsArchAuthoring[0].modelEntrySha256Hex == sed_agent::sha256Hex(modelText()),
        "binding records the resolved model entry identity and an honest origin");
}

void reset(const fs::path& root, sed::Level level)
{
    level.things[0].archLightingIndex = -1;
    const auto result = apply(requestFiles(root, level,
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})", modelText({ 4, 3 })));
    check(result.things[0].archLightingIndex == 1 && result.things[1].archLightingIndex == 0,
        "reset materializes only the selected virtual owner");
    const auto& vertices = result.motsArchLighting.at(1).meshes.at(0).vertices;
    check(vertices.size() == 4 && std::all_of(vertices.begin(), vertices.end(), [](const auto& v)
        {
            return v.mono == 0.0f && v.red == 0.0f && v.green == 0.0f && v.blue == 0.0f;
        }), "reset allocates zero samples for descending LODs without losing source slots");
    sed::level::MotsArchLightingTopology topology;
    topology.geosets = { { 4 }, { 3 } };
    check(sed::level::archTopologyAllowsBinding(result.motsArchLighting.at(1), topology),
        "reset output passes the same topology predicate as arch-validate");
    check(result.motsArchLighting.at(0) == level.motsArchLighting.at(0),
        "reset does not erase another owner's imported samples");
    check(result.motsArchAuthoring.at(0).origin == sed::MotsArchSampleOrigin::ResetZeroed,
        "reset retains its distinct authoring provenance");
}

const sed::MotsArchLightVertex& firstSample(const sed::Level& level)
{
    return level.motsArchLighting.at(static_cast<std::size_t>(level.things.at(0).archLightingIndex))
        .meshes.at(0).vertices.at(0);
}

void checkPointSample(const sed::Level& result, const std::string& label)
{
    const auto& sample = firstSample(result);
    // Distance 1, range 2: squared falloff = 1/4. Mono intensity is 8;
    // independent RGB intensity is 4 with colour (1/2, 1/4, 1).
    check(std::abs(sample.mono - 2.0f) < 1e-6f && std::abs(sample.red - 0.5f) < 1e-6f
            && std::abs(sample.green - 0.25f) < 1e-6f && std::abs(sample.blue - 1.0f) < 1e-6f,
        label + " uses independent mono/RGB intensity and squared point falloff");
}

void bake(const fs::path& root, const sed::Level& level)
{
    const auto result = apply(requestFiles(root, level,
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})"));
    checkPointSample(result, "source-point ARCH bake");
    check(result.things[0].archLightingIndex == 1 && result.things[1].archLightingIndex == 0
            && result.motsArchLighting.at(0) == level.motsArchLighting.at(0),
        "baking isolates shared imported samples");
    check(result.motsArchAuthoring.at(0).bakeProfile == "source-points-v1"
            && result.motsArchAuthoring.at(0).origin == sed::MotsArchSampleOrigin::BakedSourcePointsV1,
        "source bake persists the explicit recipe and origin");
    auto moved = level;
    moved.things[0].x = 10.0;
    moved.things[0].yaw = 90.0;
    moved.lights[0].position = { 10.0, 1.0, 1.0 };
    const auto transformed = apply(requestFiles(root, moved,
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})",
        modelText({ 3 }, true)));
    checkPointSample(transformed, "hierarchy + placed Thing world transform");
}

void addBlocker(sed::Level& level, const bool openAdjoin, const bool blockLight)
{
    sed::Sector blocker{};
    blocker.id = level.curId++;
    for ( const auto& position : std::vector<sed::TVector>{
        { -3.0, -3.0, 0.5 }, { 3.0, -3.0, 0.5 },
        { 3.0, 3.0, 0.5 }, { -3.0, 3.0, 0.5 } } )
    {
        blocker.addVertex(position.x, position.y, position.z);
    }
    sed::Surface surface{};
    surface.geo = 4;
    surface.vertices = { 0, 1, 2, 3 };
    surface.texVertices.resize(4);
    surface.normal = { 0.0, 0.0, 1.0 };
    if ( openAdjoin )
    {
        surface.adjoin = sed::SurfaceRef{ static_cast<std::int32_t>(level.sectors.size() + 1), 0 };
        surface.adjoinFlags = sed::SAF_Visible | sed::SAF_Move
            | (blockLight ? sed::SAF_BlockLight : 0);
    }
    blocker.surfaces.push_back(surface);
    level.sectors.push_back(blocker);
    if ( openAdjoin )
    {
        blocker.id = level.curId++;
        blocker.surfaces[0].adjoin->sector = static_cast<std::int32_t>(level.sectors.size() - 1);
        std::reverse(blocker.surfaces[0].vertices.begin(), blocker.surfaces[0].vertices.end());
        blocker.surfaces[0].normal.z = -1.0;
        level.sectors.push_back(std::move(blocker));
    }
}

void occlusion(const fs::path& root, const sed::Level& level)
{
    for ( const int scenario : { 0, 1, 2, 3 } )
    {
        auto world = level;
        addBlocker(world, scenario >= 2, scenario == 3);
        if ( scenario == 1 )
        {
            world.lights[0].flags |= 0x1;
        }
        const auto result = apply(requestFiles(root, world,
            R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-world-occluded-v1"})"));
        if ( scenario == 1 || scenario == 2 )
        {
            checkPointSample(result, scenario == 1 ? "NoBlock source" : "open adjoin");
        }
        else
        {
            check(firstSample(result) == sed::MotsArchLightVertex{},
                scenario == 0 ? "world face occludes ARCH point samples"
                              : "BlockLight adjoin occludes ARCH point samples");
        }
    }
}

void refusals(const fs::path& root, const sed::Level& level)
{
    const std::string bind = R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})";
    const std::string bake = R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})";
    auto wrongGame = level;
    wrongGame.kind = sed::ProjectType::JKDF2;
    wrongGame.motsArchLighting.clear();
    wrongGame.things[0].archLightingIndex = -1;
    wrongGame.things[1].archLightingIndex = -1;
    expectRefusal(requestFiles(root, wrongGame, bind), "archRequiresMots");
    expectRefusal(requestFiles(root, level,
        R"({"op":"bindThingArchitecture","thing":99,"lightingIndex":0})"), "archThingIndexInvalid");
    expectRefusal(requestFiles(root, level,
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":99})"), "archSourceInvalid");
    auto ineligible = level;
    ineligible.things[0].values.push_back(sed::deserializeTplValue("thingflags", "0"));
    expectRefusal(requestFiles(root, ineligible, bind), "archThingNotEligible");
    auto noModel = level;
    noModel.things[0].values.push_back(sed::deserializeTplValue("model3d", "none"));
    expectRefusal(requestFiles(root, noModel, bind), "archModelMissing");
    expectRefusal(requestFiles(root, level, bind, modelText(), "3do\\different.3do"), "archModelMissing");
    expectRefusal(requestFiles(root, level, bind, "not a 3DO"), "archModelInvalid");
    expectRefusal(requestFiles(root, level, bind, modelText({ 4 })), "archGeosetZeroMismatch");
    expectRefusal(requestFiles(root, level, bind, modelText({ 3, 4 })), "archLowerLodOverflow");
    expectRefusal(requestFiles(root, level,
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})",
        modelText({ 3, 4 })), "archLowerLodOverflow");
    expectRefusal(requestFiles(root, level, bake, modelText({ 3, 4 })), "archLowerLodOverflow");
    expectRefusal(requestFiles(root, level, bake, modelText({ 3 }, false, true)), "archBakeModelAmbiguous");
    const std::string occluded =
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-world-occluded-v1"})";
    auto invalidFace = level;
    addBlocker(invalidFace, false, false);
    invalidFace.sectors.back().surfaces[0].vertices[1] = 999;
    expectRefusal(requestFiles(root, invalidFace, occluded), "archOccluderInvalid");
    auto nonplanar = level;
    addBlocker(nonplanar, false, false);
    nonplanar.sectors.back().vertices[1].z += 0.1;
    expectRefusal(requestFiles(root, nonplanar, occluded), "archOccluderInvalid");
    auto brokenAdjoin = level;
    addBlocker(brokenAdjoin, true, false);
    brokenAdjoin.sectors.back().surfaces[0].adjoin.reset();
    expectRefusal(requestFiles(root, brokenAdjoin, occluded), "archOccluderInvalid");
    auto displacedAdjoin = level;
    addBlocker(displacedAdjoin, true, false);
    for ( auto& vertex : displacedAdjoin.sectors.back().vertices )
    {
        vertex.z += 0.25;
    }
    expectRefusal(requestFiles(root, displacedAdjoin, occluded), "archOccluderInvalid");
    auto tooManyLights = level;
    tooManyLights.lights.resize(257, level.lights[0]);
    expectRefusal(requestFiles(root, tooManyLights, bake), "archBakeBudgetExceeded");
    for ( const std::string invalid : {
        R"({"op":"bindThingArchitecture","thing":-1,"lightingIndex":0})",
        R"({"op":"bindThingArchitecture","thing":0.5,"lightingIndex":0})",
        R"({"op":"bindThingArchitecture","thing":2147483648,"lightingIndex":0})",
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0,"ignored":true})",
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"keep"})",
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"retail"})" } )
    {
        expectRefusal(requestFiles(root, level, invalid), "archOperationInvalid", sed_agent::kExitUsage);
    }
    auto staleArchive = requestFiles(root, level, bind);
    writeText(staleArchive.archive, sed::assets::buildGobBytes({ { "3do\\arch_fixture.3do", modelText({ 4 }) } }));
    expectRefusal(staleArchive, "", sed_agent::kExitConflict);
    auto staleInput = requestFiles(root, level, bind);
    auto patch = sed_agent::parseJson(sed_agent::readFileBytes(staleInput.request));
    patch.set("inputSha256", Json::makeString(std::string(64, '0')));
    writeText(staleInput.request, sed_agent::toJsonString(patch));
    expectRefusal(staleInput, "", sed_agent::kExitConflict);
    // A later refusal must discard a successful earlier staged binding.
    expectRefusal(requestFiles(root, level, bind + ","
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":99})"), "archSourceInvalid");
}

void resolverGuards(const sed::Level& level)
{
    const auto sourceBytes = nativeBytes(level);
    const auto operation = sed_agent::parseJson(
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})");
    for ( const int scenario : { 0, 1, 2, 3 } )
    {
        auto candidate = level;
        sed_agent::ArchOperationBudget budget;
        const auto text = modelText();
        if ( scenario == 1 )
        {
            budget.remainingWorkUnits = 0;
        }
        if ( scenario == 2 )
        {
            budget.remainingModelBytes = text.size() - 1;
        }
        const sed_agent::ArchModelResolver resolver = [&](const std::string_view name)
            -> std::optional<sed_agent::ArchModelResource>
        {
            check(name == "arch_fixture.3do", "resolver receives the effective inherited model name");
            if ( scenario == 3 )
            {
                return std::nullopt;
            }
            return sed_agent::ArchModelResource{ text,
                scenario == 0 ? std::string(64, '0') : sed_agent::sha256Hex(text) };
        };
        const std::string expected = scenario == 0 ? "archModelRevisionMismatch"
            : scenario == 3 ? "archModelMissing" : "archBakeBudgetExceeded";
        try
        {
            (void)sed_agent::tryApplyArchOperation(candidate, operation, resolver, nullptr, &budget);
            check(false, "captured resolver boundary must reject " + expected);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == expected, "captured resolver boundary reports " + expected);
        }
        check(nativeBytes(candidate) == sourceBytes,
            "resource/budget refusal leaves the in-memory candidate byte-identical");
    }

    // The resource allowance belongs to the patch, not to a single operation.
    auto candidate = level;
    const auto text = modelText();
    sed_agent::ArchOperationBudget budget;
    budget.remainingModelBytes = text.size();
    const sed_agent::ArchModelResolver resolver = [&](const std::string_view)
        -> std::optional<sed_agent::ArchModelResource>
    {
        return sed_agent::ArchModelResource{ text, sed_agent::sha256Hex(text) };
    };
    check(sed_agent::tryApplyArchOperation(candidate, operation, resolver, nullptr, &budget),
        "first ARCH operation uses the admitted source allowance");
    const auto afterFirst = nativeBytes(candidate);
    try
    {
        (void)sed_agent::tryApplyArchOperation(candidate, operation, resolver, nullptr, &budget);
        check(false, "a second ARCH operation cannot replenish the patch resource allowance");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == "archBakeBudgetExceeded", "repeated ARCH work shares one patch budget");
    }
    check(nativeBytes(candidate) == afterFirst,
        "aggregate budget refusal preserves the previously staged state");
}

void automaticBaselines(const fs::path& root, sed::Level level)
{
    for ( const auto& thing : level.things )
    {
        sed::MotsArchLightingBakeBaseline baseline;
        baseline.thingId = thing.id;
        baseline.modelName = "arch_fixture.3do";
        baseline.originalArchLightingIndex = 0;
        baseline.originalArchLighting = level.motsArchLighting[0];
        level.motsArchLightingBakeBaselines.push_back(std::move(baseline));
    }
    const auto unrelated = level.motsArchLightingBakeBaselines.at(1);
    for ( const std::string operation : {
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})",
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})",
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})" } )
    {
        const auto result = apply(requestFiles(root, level, operation));
        check(result.motsArchLightingBakeBaselines.size() == 1
                && result.motsArchLightingBakeBaselines[0] == unrelated,
            "explicit ARCH authoring discards only the selected owner's automatic-bake baseline");
    }
    const auto before = nativeBytes(level);
    const auto invalid = sed_agent::parseJson(
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":999})");
    const sed_agent::ArchModelResolver resolver = [](const std::string_view)
        -> std::optional<sed_agent::ArchModelResource>
    {
        const auto text = modelText();
        return sed_agent::ArchModelResource{ text, sed_agent::sha256Hex(text) };
    };
    // Native input permits duplicate Thing IDs when there is only one baseline
    // keyed to that ID. Explicit authoring cannot identify which owner retains
    // that history and must refuse without losing its distinctive old samples.
    auto ambiguous = level;
    ambiguous.things[0].id = 42;
    ambiguous.things[1].id = 42;
    ambiguous.motsArchLightingBakeBaselines.resize(1);
    auto& sharedBaseline = ambiguous.motsArchLightingBakeBaselines[0];
    sharedBaseline.thingId = 42;
    sharedBaseline.originalArchLighting->meshes[0].vertices[0].mono = 7.25f;
    for ( const std::string operation : {
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})",
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})",
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})" } )
    {
        const auto files = requestFiles(root, ambiguous, operation);
        auto candidate = readNative(files.source);
        check(candidate.things[0].id == 42 && candidate.things[1].id == 42
                && candidate.motsArchLightingBakeBaselines.size() == 1
                && candidate.motsArchLightingBakeBaselines[0] == sharedBaseline,
            "native reopen accepts duplicate Thing IDs with one valid shared-ID baseline");
        const auto candidateBefore = nativeBytes(candidate);
        check(candidateBefore == nativeBytes(ambiguous),
            "ambiguous-owner synthetic native fixture round-trips exactly");
        try
        {
            (void)sed_agent::tryApplyArchOperation(candidate,
                sed_agent::parseJson(operation), resolver);
            check(false, "ambiguous automatic-baseline owner must refuse in-memory authoring");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.code == sed_agent::kExitRefusal
                    && error.errorCode == "archBaselineOwnerAmbiguous",
                "ambiguous automatic-baseline owner reports a stable refusal");
        }
        check(nativeBytes(candidate) == candidateBefore,
            "ambiguous-owner refusal preserves the entire candidate byte-for-byte");
        try
        {
            (void)sed_agent::runCommand("plan", { files.source.string(), files.request.string() });
            check(false, "ARCH planning must refuse an ambiguous automatic-baseline owner");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.code == sed_agent::kExitRefusal
                    && error.errorCode == "archBaselineOwnerAmbiguous",
                "ambiguous-owner plan reports the stable refusal");
        }
        check(!fs::exists(files.output), "ambiguous-owner plan publishes no output");
        expectRefusal(files, "archBaselineOwnerAmbiguous");
        check(sed_agent::readFileBytes(files.archive) == files.archiveBytes,
            "ambiguous-owner refusal preserves captured archive bytes");
    }
    auto noBaseline = ambiguous;
    noBaseline.motsArchLightingBakeBaselines.clear();
    auto unrelatedDuplicates = level;
    unrelatedDuplicates.things.push_back(level.things[1]);
    unrelatedDuplicates.things.back().name = "other_sharer";
    for ( const std::string operation : {
        R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})",
        R"({"op":"rebindThingArchitecture","thing":0,"policy":"reset"})",
        R"({"op":"bakeThingArchitecture","thing":0,"profile":"source-points-v1"})" } )
    {
        const auto noBaselineResult = apply(requestFiles(root, noBaseline, operation));
        check(noBaselineResult.things[0].id == 42 && noBaselineResult.things[1].id == 42
                && noBaselineResult.things[0].archLightingIndex == 1
                && noBaselineResult.things[1].archLightingIndex == 0
                && noBaselineResult.motsArchLightingBakeBaselines.empty()
                && noBaselineResult.motsArchLighting[0] == noBaseline.motsArchLighting[0],
            "shared selected ID without an affected baseline permits private ARCH authoring");
        const auto unrelatedResult = apply(requestFiles(root, unrelatedDuplicates, operation));
        check(unrelatedResult.things[0].archLightingIndex == 1
                && unrelatedResult.things[1].archLightingIndex == 0
                && unrelatedResult.things[2].archLightingIndex == 0
                && unrelatedResult.things[1].id == unrelatedResult.things[2].id
                && unrelatedResult.motsArchLightingBakeBaselines.size() == 1
                && unrelatedResult.motsArchLightingBakeBaselines[0] == unrelated
                && unrelatedResult.motsArchLighting[0] == level.motsArchLighting[0],
            "unrelated shared IDs retain their baseline while selected-owner authoring succeeds");
    }
    try
    {
        (void)sed_agent::tryApplyArchOperation(level, invalid, resolver);
        check(false, "failed explicit authoring must refuse before changing automatic baselines");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == "archSourceInvalid", "invalid binding reports its source error");
    }
    check(nativeBytes(level) == before,
        "ARCH refusal preserves selected and unrelated automatic baselines byte-for-byte");
}

void archiveResolution(const fs::path& root, const sed::Level& baseline)
{
    for ( const int scenario : { 0, 1 } )
    {
        const std::string label = scenario == 0 ? "explicit 3do/ model path"
                                               : "canonical resource beats unrelated-folder basename";
        try
        {
            auto level = baseline;
            if ( scenario == 0 )
            {
                level.things[0].values.push_back(
                    sed::deserializeTplValue("model3d", "3do/arch_fixture.3do"));
            }
            auto files = requestFiles(root, level,
                R"({"op":"bindThingArchitecture","thing":0,"lightingIndex":0})");
            if ( scenario == 1 )
            {
                // Archive order intentionally puts a different, incompatible
                // model first. The normal locator never searches this folder
                // for a bare model name; a basename-only map must not choose it.
                files.archiveBytes = sed::assets::buildGobBytes({
                    { "unrelated\\arch_fixture.3do", modelText({ 4 }) },
                    { "3do\\arch_fixture.3do", modelText() } });
                writeText(files.archive, files.archiveBytes);
                auto patch = sed_agent::parseJson(sed_agent::readFileBytes(files.request));
                patch.object.at("resourceArchives").array[0].set("sha256",
                    Json::makeString(sed_agent::sha256Hex(files.archiveBytes)));
                writeText(files.request, sed_agent::toJsonString(patch));
            }
            const auto result = apply(files);
            check(result.things[0].archLightingIndex == 1
                    && result.motsArchLighting.at(1).meshes == level.motsArchLighting.at(0).meshes
                    && result.motsArchAuthoring.at(0).modelEntrySha256Hex == sed_agent::sha256Hex(modelText()),
                label + " resolves the captured bytes selected by the standard resource locator");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(false, label + ": " + error.message);
        }
    }
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-arch-operations-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        const auto level = fixture(root);
        for ( const int scenario : { 0, 1, 2, 3, 4, 5, 6, 7 } )
        {
            try
            {
                if ( scenario == 0 )
                {
                    binding(root, level);
                }
                else if ( scenario == 1 )
                {
                    reset(root, level);
                }
                else if ( scenario == 2 )
                {
                    bake(root, level);
                }
                else if ( scenario == 3 )
                {
                    occlusion(root, level);
                }
                else if ( scenario == 4 )
                {
                    refusals(root, level);
                }
                else if ( scenario == 5 )
                {
                    resolverGuards(level);
                }
                else if ( scenario == 6 )
                {
                    automaticBaselines(root, level);
                }
                else
                {
                    archiveResolution(root, level);
                }
            }
            catch ( const sed_agent::AgentError& error )
            {
                check(false, "ARCH scenario " + std::to_string(scenario) + ": " + error.message);
            }
            catch ( const std::exception& error )
            {
                check(false, "ARCH scenario " + std::to_string(scenario) + ": " + error.what());
            }
        }
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "ARCH fixture: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("ARCH fixture: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "ARCH operation failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
