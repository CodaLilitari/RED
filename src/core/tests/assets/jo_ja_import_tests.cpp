#include "raven_bsp_fixture.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/tpl.hpp"

#include <cmath>
#include <clocale>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::assets;
using namespace sed::tests::raven_fixture;

void require(bool condition, const char *message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

template <typename F> void mustFail(F &&action, const char *message)
{
    try
    {
        action();
    }
    catch ( const std::exception & )
    {
        return;
    }
    throw std::runtime_error(message);
}

GobArchive archiveOf(const JoJaImportResult &result)
{
    std::vector<std::byte> bytes(result.archive.outputData.size());
    std::memcpy(bytes.data(), result.archive.outputData.data(), bytes.size());
    return GobArchive::fromBytes(std::move(bytes));
}

std::string textOf(const std::vector<std::byte> &bytes)
{
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void run()
{
    const auto source = sed::tests::makeRavenBspFixture();
    JoJaImportOptions options;
    mustFail([&] { prepareJoJaImport(source, options); }, "Unmapped material must require explicit placeholders");
    options.allowPlaceholders = true;
    const auto result = prepareJoJaImport(source, options);
    require(result.triangles == 33 && result.patches == 1, "World triangle and patch accounting");
    require(result.playerStarts == 1 && result.sourceEntities == 2, "Source player start conversion");
    require(result.materials.size() == 1 && result.materials[0].placeholder, "Explicit placeholder report");
    require(result.materials[0].footstep == "hard",
            "Unspecified Bespin-style surface uses hard footsteps, never blanket metal");
    require(result.sourceSha256 == jklSourceSha256Hex(textOf(source)), "Source identity");
    require(result.outputSha256 == jklSourceSha256Hex(result.archive.outputData), "Output identity");
    require(result.archive.outputData == prepareJoJaImport(source, options).archive.outputData,
            "Repeated preparation must be deterministic");
    auto archive = archiveOf(result);
    require(archive.contains("jkl/joja_scene.jkl") && archive.contains("misc/joja-source.ent"),
            "Archive contains level and source entities");
    require(textOf(archive.read("misc/joja-source.ent")).find("info_player_deathmatch") != std::string::npos,
            "Original source entity semantics are retained for follow-up");
    std::istringstream levelStream(textOf(archive.read("jkl/joja_scene.jkl")));
    const auto level = loadFromJKL(levelStream);
    const auto capacities = inspectJklExportBuffers(level);
    require(capacities.materials.total >= result.materials.size() && capacities.sounds.total >= 512 &&
                capacities.cogs.total >= 128,
            "Model-only level reserves runtime capacity for materials and class-script resources");
    require(level.sectors.size() == 1 && level.things.size() == result.models.size() + 1,
            "Level has one scaffold room, static chunks and a player start");
    require(std::abs(level.things.back().z - 0.1) < 1e-9 && std::abs(level.things.back().yaw) < 1e-9,
            "Native BSP scale and player yaw use direct Z-up coordinates");
    require(result.report.find("unverified") != std::string::npos && !result.warnings.empty(),
            "Successful import still states its runtime and semantic limits");
    for ( const auto &model : result.models )
    {
        const auto modelBytes = archive.read("3do/" + model.name);
        const auto parsed = loadFrom3DO(modelBytes, {.applyHierarchyTransforms = false});
        std::istringstream syntax(textOf(modelBytes));
        std::string line;
        while ( std::getline(syntax, line) )
        {
            if ( !line.starts_with("VERTICES ") )
            {
                continue;
            }
            for ( std::size_t i = 0; i < parsed.meshes[0].vertices.size(); ++i )
            {
                require(static_cast<bool>(std::getline(syntax, line)), "Vertex row is present");
                std::istringstream row(line);
                std::string index, trailing;
                double x, y, z, intensity;
                require(static_cast<bool>(row >> index >> x >> y >> z >> intensity) && !(row >> trailing),
                        "Imported 3DO 2.1 uses the portable XYZ plus scalar intensity syntax");
            }
        }
        require(parsed.meshes.size() == 1 && parsed.meshes[0].vertices.size() <= 420,
                "Conservative per-model flat-corner budget");
        require(model3doEngineLimitIssues(parsed).empty(), "Generated model fits loader limits");
    }
    options.trianglesPerModel = 1;
    const auto split = prepareJoJaImport(source, options);
    require(split.models.size() == 33, "Requested chunk size is respected");
    options.maxModels = 1;
    mustFail([&] { prepareJoJaImport(source, options); }, "Model budget must reject the entire plan");
    options = JoJaImportOptions{};
    options.allowPlaceholders = true;
    options.maxTriangles = 2;
    mustFail([&] { prepareJoJaImport(source, options); }, "Triangle budget must reject tessellation expansion");
    options.maxTriangles = 100000;
    options.scale = 0;
    mustFail([&] { prepareJoJaImport(source, options); }, "Zero scale must be rejected");
    options.scale = std::numeric_limits<double>::quiet_NaN();
    mustFail([&] { prepareJoJaImport(source, options); }, "Nonfinite scale must be rejected");
    options.scale = 1.0 / 320;
    options.levelName = "../unsafe";
    mustFail([&] { prepareJoJaImport(source, options); }, "Unsafe target entry name must be rejected");
    options.levelName = "joja_scene";
    options.target = ProjectType::MOTS;
    const auto mots = prepareJoJaImport(source, options);
    const auto motsText = textOf(archiveOf(mots).read("jkl/joja_scene.jkl"));
    require(motsText.find("kyle_m.cog") != std::string::npos, "MotS uses expansion-native stock player references");
    std::istringstream motsStream(motsText);
    const auto motsLevel = loadFromJKL(motsStream, {.projectType = ProjectType::MOTS});
    require(motsLevel.motsArchLighting.size() == mots.models.size() && motsLevel.things.front().archLightingIndex == 0,
            "MotS architecture receives explicit per-corner source lighting");

    Fixture lit;
    lit.lumps[14].assign(128u * 128u * 3u, std::byte{128});
    write32(lit.lumps[13], 36, 0);
    write32(lit.lumps[13], 148 + 36, 0);
    options.target = ProjectType::JKDF2;
    const auto lighting = prepareJoJaImport(lit.bytes(), options);
    require(lighting.lightmapCorners == 99 && lighting.vertexLitCorners == 0,
            "Lightmap sampling covers every emitted corner");
    auto litArchive = archiveOf(lighting);
    const auto litModel =
        loadFrom3DO(litArchive.read("3do/" + lighting.models[0].name), {.applyHierarchyTransforms = false});
    require(std::abs(litModel.meshes[0].vertices[0].color.r - 128.0 / 255.0) < 1e-5,
            "Baked lightmap luminance survives target serialization");

    RgbaImage image{8, 8, std::vector<std::uint8_t>(8u * 8u * 4u, 255)};
    options.materials = {{"a", "mapped.mat", buildRgb565Mat({image}).bytes}};
    options.allowPlaceholders = false;
    const auto mapped = prepareJoJaImport(source, options);
    require(!mapped.materials[0].placeholder && archiveOf(mapped).contains("mat/mapped.mat"),
            "Explicit captured target MAT is retained byte-for-byte");
    options.materials.push_back(options.materials[0]);
    mustFail([&] { prepareJoJaImport(source, options); }, "Duplicate source mapping must reject");
    options.materials.resize(1);
    options.materials[0].bytes = buildIndexedMat({image}, makeGrayscalePalette()).bytes;
    mustFail([&] { prepareJoJaImport(source, options); }, "Indexed material requires explicit matching CMP");

    options = JoJaImportOptions{};
    options.allowPlaceholders = true;
    Fixture metal;
    write32(metal.lumps[1], 64, 3);
    const auto metallic = prepareJoJaImport(metal.bytes(), options);
    require(metallic.materials[0].footstep == "metal", "Source metal material maps to target metal cue");
    Fixture grass;
    write32(grass.lumps[1], 64, 5);
    const auto grassy = prepareJoJaImport(grass.bytes(), options);
    require(grassy.materials[0].footstepNeedsReview, "Grass cannot claim a faithful stock equivalent");
    options.audioPolicy = JoJaAudioPolicy::SourceImport;
    mustFail([&] { prepareJoJaImport(source, options); }, "Source sound import cannot succeed without sound routing");
    options.audioPolicy = JoJaAudioPolicy::StockEquivalents;
    options.skipShaders = {"a"};
    mustFail([&] { prepareJoJaImport(source, options); }, "An entirely skipped world must not publish an empty scene");
    options.skipShaders.clear();
    Fixture degenerate;
    for ( std::size_t i = 0; i < 10; ++i )
    {
        writeFloat(degenerate.lumps[10], i * 80, 0);
        writeFloat(degenerate.lumps[10], i * 80 + 4, 0);
        writeFloat(degenerate.lumps[10], i * 80 + 8, 0);
    }
    options.maxTriangles = 1;
    mustFail([&] { prepareJoJaImport(degenerate.bytes(), options); },
             "Tessellation work budget counts degenerate candidates too");
}

void runMaterialLayout()
{
    JoJaImportOptions options;
    RgbaImage image{8, 8, std::vector<std::uint8_t>(8u * 8u * 4u, 255)};
    auto mat = buildRgb565Mat({image}).bytes;
    // Valid RGB555 data must not masquerade as the promised RGB565 edition.
    mat[32] = 5;  // green width
    mat[40] = 10; // red shift
    options.materials = {{"a", "rgb555.mat", std::move(mat)}};
    mustFail([&] { prepareJoJaImport(sed::tests::makeRavenBspFixture(), options); },
             "Non-RGB565 16-bit material must be refused");
}

void runFootstepDisclosure()
{
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    for ( const auto material : {1u, 2u, 5u, 6u, 21u, 22u, 24u, 25u, 27u} )
    {
        Fixture fixture;
        write32(fixture.lumps[1], 64, material);
        const auto result = prepareJoJaImport(fixture.bytes(), options);
        require(result.materials[0].footstep == "hard" && result.materials[0].footstepNeedsReview,
                "Source wood, grass and rug families require review of the hard stock fallback");
    }
    Fixture water;
    write32(water.lumps[1], 68, 4);
    require(prepareJoJaImport(water.bytes(), options).materials[0].footstepNeedsReview,
            "Water contents require review even without a water material number");
}

void runMixedFootstepsAndStarts()
{
    Fixture fixture;
    fixture.lumps[1].resize(144);
    fixture.lumps[1][72] = std::byte{'b'};
    write32(fixture.lumps[1], 72 + 64, 3);
    write32(fixture.lumps[13], 148, 1);
    fixture.lumps[0].pop_back();
    for ( char c : std::string_view("\n{\"classname\" \"info_player_start\" \"origin\" \"32 0 32\"}\n") )
    {
        fixture.lumps[0].push_back(static_cast<std::byte>(c));
    }
    fixture.lumps[0].push_back(std::byte{});
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    options.renderMeshCollision = true;
    const auto result = prepareJoJaImport(fixture.bytes(), options);
    require(result.models.size() == 2 && result.playerStarts == 1,
            "Shared-cell metal and hard surfaces get separate chunks; FFA starts take precedence");
    std::istringstream input(textOf(archiveOf(result).read("jkl/joja_scene.jkl")));
    const auto level = loadFromJKL(input);
    Templates templates;
    for ( const auto &row : level.templates )
    {
        templates.addOrUpdate(row);
    }
    for ( std::size_t i = 0; i < result.models.size(); ++i )
    {
        const auto *flags = templates.findField(level.things[i].templateName, "thingflags");
        const auto *collision = templates.findField(level.things[i].templateName, "collide");
        require(flags && collision && collision->asString() == "3",
                "Model contact is enabled only by the explicit collision option");
        require(std::stoul(flags->asString(), nullptr, 0) == (result.models[i].footstep == "metal" ? 0x400048u : 0x48u),
                "Serialized Thing flags independently select each chunk's stock footstep");
    }
    require(level.things.back().x == 0, "FFA source position is preserved instead of the SP marker");
}

void runTargetPrecision()
{
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    options.scale = 1e-46;
    mustFail([&] { prepareJoJaImport(sed::tests::makeRavenBspFixture(), options); },
             "Geometry that collapses in target single precision must be refused");
}

void runManifestBinding()
{
    Fixture fixture;
    fixture.lumps[1].resize(144);
    fixture.lumps[1][72] = std::byte{'b'};
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    const auto first = prepareJoJaImport(fixture.bytes(), options);
    options.skipShaders = {"b"};
    const auto second = prepareJoJaImport(fixture.bytes(), options);
    require(first.outputSha256 == second.outputSha256 && first.manifestSha256 != second.manifestSha256,
            "Manifest must bind an explicit skip decision even if the shader has no world faces");
}

void runLocaleDeterminism()
{
    const std::string original = std::setlocale(LC_NUMERIC, nullptr);
    struct RestoreLocale
    {
        std::string name;
        ~RestoreLocale()
        {
            std::setlocale(LC_NUMERIC, name.c_str());
        }
    } restore{original};
    std::setlocale(LC_NUMERIC, "C");
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    const auto source = sed::tests::makeRavenBspFixture();
    const auto first = prepareJoJaImport(source, options);
    for ( const char *name : {"de_DE.utf8", "de_DE.UTF-8", "fr_FR.UTF-8", "German_Germany.1252"} )
    {
        if ( !std::setlocale(LC_NUMERIC, name) )
        {
            continue;
        }
        const auto second = prepareJoJaImport(source, options);
        require(first.archive.outputData == second.archive.outputData && first.manifestSha256 == second.manifestSha256,
                "Changing the process numeric locale must not alter import output or reviewed hashes");
        return;
    }
    std::cout << "Comma-decimal locale unavailable; locale-specific branch not exercised.\n";
}
} // namespace

int main()
{
    int failures = 0;
    for ( auto test : {run, runMaterialLayout, runManifestBinding, runLocaleDeterminism, runFootstepDisclosure,
                       runMixedFootstepsAndStarts, runTargetPrecision} )
    {
        try
        {
            test();
        }
        catch ( const std::exception &error )
        {
            std::cerr << error.what() << '\n';
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
