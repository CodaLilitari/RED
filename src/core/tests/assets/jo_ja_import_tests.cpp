#include "raven_bsp_fixture.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/level/ray_lighting_resources.hpp"

#include <algorithm>
#include <array>
#include <clocale>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

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
    {
        Fixture fixture;
        fixture.lumps[0].pop_back();
        for ( char c : std::string_view(R"({"classname" "weapon_blaster" "origin" "1 2 3"}
{"classname" "item_medpak_instant" "origin" "1 2 3"}
{"classname" "item_medpac" "origin" "1 2 3"}
{"classname" "weapon_thermal" "origin" "1 2 3"}
{"classname" "weapon_bowcaster" "origin" "1 2 3"}
{"classname" "item_shield_sm_instant" "origin" "1 2 3"}
{"classname" "ammo_blaster" "origin" "1 2 3"}
{"classname" "weapon_bowcaster" "origin" "999999 0 0"}
{"classname" "weapon_disruptor" "origin" "2 2 3"}
{"classname" "weapon_blaster" "origin" "1 2 3" "targetname" "locked"})") )
            fixture.lumps[0].push_back(static_cast<std::byte>(c));
        fixture.lumps[0].push_back(std::byte{});
        for ( auto game : {ProjectType::JKDF2, ProjectType::MOTS} )
        {
            auto recipe = options;
            recipe.target = game;
            recipe.stockPickups = true;
            recipe.sourceProfile = JoJaSourceProfile::JediOutcastMultiplayer;
            const auto imported = prepareJoJaImport(fixture.bytes(), recipe);
            auto packed = archiveOf(imported);
            const auto jkl = textOf(packed.read("jkl/joja_scene.jkl"));
            std::istringstream input(jkl);
            const auto scene = loadFromJKL(input, {.projectType = game});
            require(std::count_if(scene.things.begin(), scene.things.end(), [](const auto& thing)
                { return thing.templateName == "strifle" && thing.sector == 0 && std::abs(thing.x - 1.0/320) < 1e-6; }) == 1,
                "Only unconditional E-11 placed at source coordinates");
            for ( const char* name : {"healthpack", "bactatank", "detonator", "crossbow", "shieldrecharge", "energycell"} )
            {
                require(std::count_if(scene.things.begin(), scene.things.end(), [&](const auto& thing)
                    { return thing.templateName == name && thing.archLightingIndex == -1; }) == 1,
                        "Stock pickup is serialized once with no manufactured ARCH record");
            }
            if ( game == ProjectType::MOTS )
            {
                require(scene.motsArchLighting.size() == imported.models.size() &&
                        scene.things.front().archLightingIndex >= 0, "Generated world ARCH records retained");
            }
            require(jkl.find(game == ProjectType::JKDF2 ? "pow_strifle.cog" : "pow_strifle_m.cog") != std::string::npos,
                    "Serialized pickup uses target edition COG");
            require(imported.report.find("Projectile strifle +stlaser: missing-template") != std::string::npos &&
                    imported.report.find("Projectile stock-start/bryar +bryarbolt: missing-template") != std::string::npos,
                    "A placed E-11 and default start do not imply serialized shot templates");
            require(imported.report.find("weapon_disruptor: review") != std::string::npos &&
                    imported.report.find("weapon_blaster: review") != std::string::npos,
                    "Unsupported and conditional entities identified separately");
        }
    }
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

void runSourceProfiles()
{
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    const auto checkProfile = [&](std::uint32_t flags, JoJaSourceProfile profile, const char *cue, bool review)
    {
        Fixture fixture;
        write32(fixture.lumps[1], 64, flags);
        options.sourceProfile = profile;
        const auto result = prepareJoJaImport(fixture.bytes(), options);
        require(result.materials.at(0).footstep == cue && result.materials.at(0).footstepNeedsReview == review,
                "Stock footstep must use the selected multiplayer game's dispatch");
        return result.manifestSha256;
    };
    using Profile = JoJaSourceProfile;
    checkProfile(3, Profile::JediOutcastMultiplayer, "hard", false);
    checkProfile(3, Profile::JediAcademyMultiplayer, "metal", false);
    checkProfile(0x8000, Profile::JediOutcastMultiplayer, "metal", false);
    checkProfile(0x8000, Profile::JediAcademyMultiplayer, "hard", false);
    checkProfile(0x8007, Profile::JediOutcastMultiplayer, "metal", false);
    checkProfile(0x8007, Profile::JediAcademyMultiplayer, "earth", false);
    checkProfile(0x408003, Profile::JediOutcastMultiplayer, "hard", true);
    checkProfile(0x408003, Profile::JediAcademyMultiplayer, "metal", true);
    checkProfile(5, Profile::JediAcademyMultiplayer, "hard", true);
    require(checkProfile(0, Profile::JediOutcastMultiplayer, "hard", false) !=
                checkProfile(0, Profile::JediAcademyMultiplayer, "hard", false),
            "Reviewed manifests bind the profile even when the stock cue agrees");
}

void runModelSceneArchReadback()
{
    Fixture source;
    for ( std::size_t i = 1; i < 10; ++i )
    {
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            source.lumps[10][i * 80 + 64 + channel] = static_cast<std::byte>((i * 21 + channel * 13) % 256);
        }
    }
    JoJaImportOptions options;
    options.target = ProjectType::MOTS;
    options.allowPlaceholders = true;
    options.trianglesPerModel = 2;
    const auto result = prepareJoJaImport(source.bytes(), options);
    auto archive = archiveOf(result);
    std::istringstream wire(textOf(archive.read("jkl/joja_scene.jkl")));
    const auto level = loadFromJKL(wire, {.projectType = ProjectType::MOTS});
    Templates templates;
    for ( const auto &value : level.templates )
    {
        templates.addOrUpdate(value);
    }
    require(result.models.size() > 1 && level.motsArchLighting.size() == result.models.size(),
            "The default model scene fixture has a distinct ARCH record for each chunk");
    float darkest = 1.f, brightest = 0.f;
    for ( std::size_t index = 0; index < result.models.size(); ++index )
    {
        const auto &thing = level.things.at(index);
        const auto *modelName = templates.findField(thing.templateName, "model3d");
        require(modelName && modelName->asString() == result.models[index].name &&
                    thing.archLightingIndex == static_cast<int>(index),
                "Each serialized chunk binds its own model and ARCH index");
        const auto model = loadFrom3DO(archive.read("3do/" + result.models[index].name),
                                       {.applyHierarchyTransforms = false});
        const auto &record = level.motsArchLighting.at(index);
        require(record.wireIndex == thing.archLightingIndex && record.meshes.size() == 1 &&
                    record.meshes[0].wireIndex == 0 && model.meshes.size() == 1 &&
                    record.meshes[0].vertices.size() == model.meshes[0].vertices.size(),
                "Each bound ARCH record covers every vertex of its stored model mesh");
        for ( std::size_t i = 0; i < model.meshes[0].vertices.size(); ++i )
        {
            const float scalar = model.meshes[0].vertices[i].color.r;
            darkest = std::min(darkest, scalar);
            brightest = std::max(brightest, scalar);
            const auto &light = record.meshes[0].vertices[i];
            for ( const float channel : {light.mono, light.red, light.green, light.blue} )
            {
                require(std::abs(channel - scalar) <= 0.00001f,
                        "All four serialized ARCH channels match their model vertex's stored 3DO scalar");
            }
        }
    }
    require(brightest - darkest > 0.25f, "Readback exercises varied source intensities");
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

void runMaskedHybridMaterial()
{
    const auto source = sed::tests::makeRavenBspRoomsFixture();
    JoJaImportOptions options;
    RgbaImage image{8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 0)};
    image.rgba[0] = image.rgba[1] = image.rgba[2] = image.rgba[3] = 255;
    auto mat = buildIndexedMat({image}, makeGrayscalePalette()).bytes;
    options.materials = {{"a", "cutout.mat", mat}};
    options.palette.assign(64 + 256 * 3 + 64 * 256, '\0');
    options.palette.replace(0, 4, "CMP ");
    for ( int i = 0; i < 256; ++i )
    {
        for ( int channel = 0; channel < 3; ++channel )
        {
            options.palette[64 + 3 * i + channel] = static_cast<char>(i);
        }
    }
    // A shipped cutout sets both fields. The classic texture loader also enables
    // colour keying from the per-cel flag independently of the MAT colour layout.
    for ( const auto& [headerAlpha, celKey] : {std::pair{false, false}, std::pair{true, false},
                                               std::pair{false, true}, std::pair{true, true}} )
    {
        options.materials[0].bytes.at(64) = headerAlpha ? 1 : 0;
        options.materials[0].bytes.at(124) = celKey ? 1 : 0;
        options.geometryMode = JoJaGeometryMode::ModelScene;
        const auto legacy = prepareJoJaImport(source, options);
        require(textOf(archiveOf(legacy).read("mat/cutout.mat")) == options.materials[0].bytes,
                "Model Scene must preserve the captured masked MAT");
        options.geometryMode = JoJaGeometryMode::HybridSectors;
        if ( !headerAlpha && !celKey )
        {
            require(!prepareJoJaImport(source, options).models.empty(), "Hybrid must accept opaque indexed MATs");
            continue;
        }
        try
        {
            prepareJoJaImport(source, options);
        }
        catch ( const std::runtime_error& error )
        {
            require(std::string_view(error.what()).find("cutout.mat") != std::string_view::npos,
                    "Hybrid refusal must identify the masked MAT");
            continue;
        }
        throw std::runtime_error(headerAlpha ? "Hybrid accepted a masked MAT" :
                                              "Hybrid accepted a MAT with independent per-cel colour keying");
    }
}

template <ProjectType target> void runNativeSurfaceLight()
{
    auto source = sed::tests::makeRavenBspRoomsFixture();
    std::uint32_t vertices = 0;
    std::memcpy(&vertices, source.data() + 8 + 10 * 8, sizeof(vertices));
    // The native floor's source vertices at (0,0), (1,0) and (0,1) have
    // distinct greys, so this checks both channel selection and corner order.
    for ( const auto& [index, grey] : {std::pair{1u, 64u}, std::pair{2u, 128u}, std::pair{4u, 192u}} )
    {
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            source.at(vertices + 80 * index + 64 + channel) = static_cast<std::byte>(grey);
        }
    }
    JoJaImportOptions options;
    options.target = target;
    options.geometryMode = JoJaGeometryMode::HybridSectors;
    options.allowPlaceholders = true;
    const auto result = prepareJoJaImport(source, options);
    std::istringstream wire(textOf(archiveOf(result).read("jkl/joja_scene.jkl")));
    const auto level = loadFromJKL(wire, {.projectType = target});
    std::size_t litCorners = 0;
    for ( const auto& sector : level.sectors )
    {
        for ( const auto& surface : sector.surfaces )
        {
            if ( surface.geo != 4 )
            {
                continue;
            }
            for ( std::size_t k = 0; k < surface.vertices.size(); ++k )
            {
                const auto& point = sector.vertices.at(surface.vertices[k]);
                const double expected = (64 + 64 * point.x / options.scale + 128 * point.y / options.scale) / 255;
                const auto& light = surface.texVertices.at(k).color;
                for ( const float channel : {light.a, light.r, light.g, light.b} )
                {
                    // Classic world vertex lighting uses four decimal places on the wire.
                    require(std::abs(channel - expected) <= 5.1e-5, target == ProjectType::JKDF2
                        ? "Serialized JK native surface intensity must retain the source floor's per-corner light"
                        : "All four serialized MotS native surface light channels must retain the source intensity");
                }
                ++litCorners;
            }
        }
    }
    require(litCorners >= 3, "Native surface light test requires a serialized textured floor");
}

void runHybridImport()
{
    const auto source = sed::tests::makeRavenBspRoomsFixture();
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    const auto legacy = prepareJoJaImport(source, options);
    options.geometryMode = JoJaGeometryMode::HybridSectors;
    options.trianglesPerModel = 400;
    options.maxTriangles = 200000;
    options.renderMeshCollision = true;
    const auto hybrid = prepareJoJaImport(source, options);
    require(hybrid.sectors == 2 && hybrid.portalPairs > 0 && hybrid.nativeFaces > 0,
            "Hybrid import must publish actual native rooms, doorway and a floor");
    require(hybrid.residualTriangles >= 32 && hybrid.maximumRelativeCoverageError < 1e-6,
            "Hybrid import retains curved detail with complete admitted-source coverage");
    require(hybrid.outputSha256 != legacy.outputSha256 && hybrid.manifestSha256 != legacy.manifestSha256,
            "Hybrid geometry policy changes the reviewed recipe and output");
    auto archive = archiveOf(hybrid);
    std::istringstream input(textOf(archive.read("jkl/joja_scene.jkl")));
    const auto level = loadFromJKL(input);
    require(level.sectors.size() == 2 && level.things.size() == hybrid.models.size() + 1,
            "Native sectors survive archive serialization with all static detail and source starts");
    std::size_t native = 0, adjoins = 0;
    for ( const auto& sector : level.sectors )
    {
        for ( const auto& surface : sector.surfaces )
        {
            require(surface.vertices.size() >= 3 && surface.vertices.size() <= 24, "Classic world-face corner cap");
            native += surface.geo == 4;
            adjoins += surface.adjoin.has_value();
            if ( surface.geo == 4 )
            {
                require((surface.surfaceFlags & (SF_Floor | SF_Collision)) == (SF_Floor | SF_Collision),
                        "Promoted floor uses native floor and collision flags");
            }
        }
    }
    require(native == hybrid.nativeFaces && adjoins == 2 * hybrid.portalPairs, "Native topology report matches JKL");
    for ( std::size_t i = 0; i < hybrid.models.size(); ++i )
    {
        require(level.things[i].sector == static_cast<std::int32_t>(hybrid.models[i].sector),
                "Each residual Thing belongs to its reported room");
    }
    options.maxSectors = 1;
    const auto coarse = prepareJoJaImport(source, options);
    require(coarse.sectors == 1 && coarse.portalPairs == 0 && coarse.manifestSha256 != hybrid.manifestSha256,
            "Sector budget is honored and hash-bound");
    options.maxSectors = 97;
    mustFail([&] { prepareJoJaImport(source, options); }, "Hybrid refuses unbounded sector counts");
    options.maxSectors = 2;
    options.maxFragments = 1;
    mustFail([&] { prepareJoJaImport(source, options); }, "Hybrid fragment budget is failure-atomic");
    options.maxFragments = 500000;
    options.target = ProjectType::MOTS;
    const auto mots = prepareJoJaImport(source, options);
    std::istringstream motsInput(textOf(archiveOf(mots).read("jkl/joja_scene.jkl")));
    const auto motsLevel = loadFromJKL(motsInput, {.projectType = ProjectType::MOTS});
    require(motsLevel.motsArchLighting.size() == mots.models.size(), "Hybrid MotS retains architecture lighting");
}

void runSourceClips()
{
    const auto f = playerClipFixture();
    const auto bytes = f.bytes();
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    auto original = prepareJoJaImport(bytes, options);
    require(original.playerClips.empty(), "Existing recipes omit source brushes");
    options.sourcePlayerClip = JoJaPlayerClipMode::Standable;
    options.movementBarrierBrushes = {1};
    const auto result = prepareJoJaImport(bytes, options);
    require(result.playerClips.size() == 2 && result.models.size() == original.models.size(),
            "Pure world player clips become separate hidden collision Things");
    auto gob = archiveOf(result);
    std::string jkl = textOf(gob.read("jkl/joja_scene.jkl"));
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& clip = result.playerClips[i];
        const auto mesh = loadFrom3DO(gob.read("3do/" + clip.model), {.applyHierarchyTransforms = false});
        require(clip.brush == i && mesh.meshes.size() == 1 && mesh.meshes[0].faces.size() == 7 &&
                mesh.meshes[0].vertices.size() == 10 && mesh.nodes[0].mesh == 0,
                "An oblique clipped corner must survive archive readback; an AABB is insufficient");
        require(clip.mode == (i == 0 ? JoJaPlayerClipMode::Standable : JoJaPlayerClipMode::MovementBarrier),
                "Explicit brush IDs select the movement-only representation");
    }
    require(jkl.find("type=weapon") != std::string::npos && jkl.find("damage=-1") != std::string::npos &&
            jkl.find("thingflags=0x50") != std::string::npos, "Selective and standable native templates");
    // Compiler rounding leaves a sub-ULP flat ledge between an axial bevel
    // and an oblique plane. It disappears at JK world precision; the wedge survives.
    auto thin = f;
    auto& p = thin.lumps[2];
    writeFloat(p, 12, -560);
    writeFloat(p, 16 + 12, 568);
    writeFloat(p, 4 * 16 + 12, 8);
    writeFloat(p, 5 * 16 + 12, 0);
    writeFloat(p, 6 * 16 + 0, 0.7f);
    writeFloat(p, 6 * 16 + 4, 0);
    writeFloat(p, 6 * 16 + 8, 1);
    writeFloat(p, 6 * 16 + 12, 392);
    const auto beveled = prepareJoJaImport(thin.bytes(), options);
    require(beveled.playerClips[0].faces == 6, "A float-aliased bevel can disappear only within a closed hull");
    options.movementBarrierBrushes = {2};
    mustFail([&] { prepareJoJaImport(bytes, options); }, "An ordinary solid cannot be named as a player clip");
    options.movementBarrierBrushes = {1};
    options.maxModels = original.models.size() + 1;
    mustFail([&] { prepareJoJaImport(bytes, options); }, "Clip Things share the model budget");
    options.maxModels = 512;
    options.target = ProjectType::MOTS;
    mustFail([&] { prepareJoJaImport(bytes, options); }, "JK movement filters have no verified MotS contract");
}

void runPlayerClipFootsteps()
{
    auto fixture = playerClipFixture();
    fixture.lumps[1].resize(3 * 72);
    fixture.lumps[1][2 * 72] = std::byte{'t'};
    // The top side has its own shader. The eligibility shader stays generic.
    write32(fixture.lumps[9], 5 * 12 + 4, 2);
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    options.sourcePlayerClip = JoJaPlayerClipMode::Standable;
    options.movementBarrierBrushes = {1};
    using Profile = JoJaSourceProfile;
    const auto check = [&](Profile profile, std::uint32_t topFlags, std::uint32_t expected)
    {
        options.sourceProfile = profile;
        write32(fixture.lumps[1], 2 * 72 + 64, topFlags);
        const auto result = prepareJoJaImport(fixture.bytes(), options);
        std::istringstream input(textOf(archiveOf(result).read("jkl/joja_scene.jkl")));
        const auto level = loadFromJKL(input);
        Templates templates;
        for ( const auto& row : level.templates )
        {
            templates.addOrUpdate(row);
        }
        const auto flags = [&](std::size_t clip)
        {
            const auto& owner = level.things.at(result.playerClips.at(clip).thing);
            const auto* field = templates.findField(owner.templateName, "thingflags");
            require(field != nullptr, "Clip owner has an effective Thing flag value");
            return std::stoul(field->asString(), nullptr, 0);
        };
        if ( profile == Profile::Legacy )
        {
            require(result.outputSha256 == "3088083601542114771412f76de68df709fed22c63aa390da11fd45abff60eb2" &&
                    result.manifestSha256 == "bbd95c65010c344d4416016b6efa0822bea32ca9a6518f8831796e838626febb",
                    "Legacy standable and barrier recipe retains its reviewed GOB and manifest bytes");
        }
        require(flags(1) == 0x50u, "Movement-only barriers retain their existing flags");
        require(flags(0) == expected, "Standable clip owner must use its emitted top-side shader's stock cue");
        return result;
    };
    check(Profile::Legacy, 0x8000u, 0x50u);
    check(Profile::JediOutcastMultiplayer, 0x8000u, 0x400050u);
    check(Profile::JediAcademyMultiplayer, 3u, 0x400050u);
    // All vertical walls and the bottom are metal. The walkable top is dirt.
    write32(fixture.lumps[1], 64, 3u);
    const auto dirt = check(Profile::JediAcademyMultiplayer, 0x8007u, 0x800050u);
    const auto& top = dirt.playerClips[0].footsteps.value();
    require(top.cue == "earth" && top.areaByCue.size() == 1 && !top.needsReview &&
                !dirt.playerClips[1].footsteps,
            "Only the standable owner's upward polygons contribute to class coverage");
    const auto suppressed = check(Profile::JediOutcastMultiplayer, 0x408000u, 0x50u);
    require(suppressed.playerClips[0].footsteps->needsReview, "Source NOSTEPS requests audio review");

    // Replace the vertical bevel by x+z<=12. Half of the roof is horizontal dirt,
    // half spans the same XY footprint at 45 degrees, with a metal side shader.
    writeFloat(fixture.lumps[2], 6 * 16 + 4, 0);
    writeFloat(fixture.lumps[2], 6 * 16 + 8, 1);
    const auto mixed = check(Profile::JediAcademyMultiplayer, 7u, 0x400050u);
    const auto& steps = mixed.playerClips[0].footsteps.value();
    const double flatArea = 32 * options.scale * options.scale;
    require(steps.cue == "metal" && steps.status() == std::string_view("mixed") && steps.needsReview &&
                steps.areaByCue.size() == 2 &&
                std::abs(steps.areaByCue.at("earth") - flatArea) < 1e-10 &&
                std::abs(steps.areaByCue.at("metal") - flatArea * std::sqrt(2.0)) < 1e-10,
            "Mixed owners disclose actual areas of retained walkable facets and choose the dominant cue");

    // A symmetric roof has a real class tie. Use binary-exact geometry so
    // both slopes have the same measured area after target rounding.
    options.scale = 0.125;
    writeFloat(fixture.lumps[2], 5 * 16, -0.5f);
    writeFloat(fixture.lumps[2], 5 * 16 + 12, 6);
    writeFloat(fixture.lumps[2], 6 * 16, 0.5f);
    writeFloat(fixture.lumps[2], 6 * 16 + 12, 10);
    const auto tie = check(Profile::JediAcademyMultiplayer, 0u, 0x50u);
    const auto& tied = tie.playerClips[0].footsteps.value();
    require(tied.needsReview && tied.areaByCue.at("hard") == tied.areaByCue.at("metal"),
            "Equal hard and metal support chooses hard and discloses both areas");

    // Steep caps z<=2x and z<=16-2x leave no qualifying support.
    writeFloat(fixture.lumps[2], 5 * 16, -2);
    writeFloat(fixture.lumps[2], 5 * 16 + 12, 0);
    writeFloat(fixture.lumps[2], 6 * 16, 2);
    writeFloat(fixture.lumps[2], 6 * 16 + 12, 16);
    const auto steep = check(Profile::JediAcademyMultiplayer, 3u, 0x50u);
    const auto& unsupported = steep.playerClips[0].footsteps.value();
    require(unsupported.status() == std::string_view("noWalkableFaces") && unsupported.needsReview &&
                unsupported.areaByCue.empty(),
            "A steep hull retains hard fallback with no claimed support area");
}

void runWorldModelRenderPass()
{
    const auto checkArchive = [](const std::vector<std::byte>& source, JoJaGeometryMode mode, const char* footstep)
    {
        JoJaImportOptions options;
        options.allowPlaceholders = true;
        options.renderMeshCollision = true;
        options.sourceProfile = JoJaSourceProfile::JediAcademyMultiplayer;
        options.geometryMode = mode;
        const auto prior = prepareJoJaImport(source, options);
        require(!prior.models.empty() && prior.models.front().footstep == footstep,
                "Render pass test requires an emitted model with the expected material cue");
        options.worldModelRenderPass = JoJaWorldModelRenderPass::LevelGeometry;
        const auto explicitDefault = prepareJoJaImport(source, options);
        require(prior.archive.outputData == explicitDefault.archive.outputData &&
                prior.manifestSha256 == explicitDefault.manifestSha256, "Existing recipes replay exactly");
        options.worldModelRenderPass = JoJaWorldModelRenderPass::OrdinaryThing;
        const auto ordinary = prepareJoJaImport(source, options);
        auto before = archiveOf(prior);
        auto after = archiveOf(ordinary);
        require(prior.manifestSha256 != ordinary.manifestSha256 && before.entries().size() == after.entries().size(),
                "Opt-in changes the recipe identity and preserves the archive inventory");
        const auto jkl = before.find("jkl/joja_scene.jkl").value().name;
        const auto report = before.find("misc/joja-import.txt").value().name;
        std::string expected = textOf(before.read(jkl));
        std::istringstream rows(expected);
        std::string row;
        std::size_t offset = 0, edits = 0;
        while ( std::getline(rows, row) )
        {
            std::istringstream words(row);
            std::string name, parent;
            words >> name >> parent;
            if ( name == "_jjworld" || parent == "_jjworld" )
            {
                const auto start = row.find("thingflags=");
                require(start != std::string::npos, "Every generated world template declares its own flags");
                const auto value = start + std::string_view("thingflags=").size();
                const auto stop = row.find_first_of(" \t\r", value);
                const auto flags = row.substr(value, stop - value);
                require(flags == "0x48" || flags == "0x400048" || flags == "0x800048",
                        "Original static model flags include standability, LEVELGEO and the material cue");
                const auto ordinaryFlags = flags == "0x48" ? "0x4000040"
                                         : flags == "0x400048" ? "0x4400040" : "0x4800040";
                expected.replace(offset + value, flags.size(), ordinaryFlags);
                offset += std::string_view(ordinaryFlags).size() - flags.size();
                ++edits;
            }
            offset += row.size() + 1;
        }
        require(edits == prior.models.size() + 1 && expected == textOf(after.read(jkl)),
                "Only LEVELGEO and IGNOREGOURAUDDISTANCE change in static templates; all other JKL bytes survive");
        for ( const auto& entry : before.entries() )
        {
            require(after.contains(entry.name), "All original archive members survive");
            if ( entry.name != jkl && entry.name != report )
                require(before.read(entry.name) == after.read(entry.name),
                        "3DO geometry, baked lights, materials, palette and all other assets must match exactly");
        }
    };
    for ( const auto& [flags, cue] : {std::pair{0u, "hard"}, {3u, "metal"}, {7u, "earth"}} )
    {
        Fixture fixture;
        write32(fixture.lumps[1], 64, flags);
        checkArchive(fixture.bytes(), JoJaGeometryMode::ModelScene, cue);
    }
    checkArchive(sed::tests::makeRavenBspRoomsFixture(), JoJaGeometryMode::HybridSectors, "hard");
}

void runMeshOptimization()
{
    // The Sith Council lessons: weld near-identical normals, share texture vertices and merge
    // coplanar faces only where light and texture coordinates stay linear. Off is the default
    // and reproduces existing recipes.
    const auto source = sed::tests::makeRavenBspFixture();
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    options.target = ProjectType::MOTS;
    const auto off = prepareJoJaImport(source, options);
    options.meshOptimization = MeshOptimization::Off;
    require(prepareJoJaImport(source, options).manifestSha256 == off.manifestSha256 &&
                off.optimizedVerticesIn == 0 && off.report.find("Mesh optimization") == std::string::npos,
            "Off is the default: same manifest, no optimization report");
    for ( const auto level : {MeshOptimization::Strict, MeshOptimization::Loose} )
    {
        options.meshOptimization = level;
        const auto optimized = prepareJoJaImport(source, options);
        require(optimized.archive.outputData == prepareJoJaImport(source, options).archive.outputData,
                "Optimized preparation is deterministic");
        require(optimized.manifestSha256 != off.manifestSha256 &&
                    optimized.report.find(std::string("Mesh optimization: ") + meshOptimizationName(level)) !=
                        std::string::npos,
                "The recipe identity and report name the optimization");
        require(optimized.triangles == off.triangles && optimized.models.size() == off.models.size(),
                "Chunking and source triangle accounting are unchanged");
        require(optimized.optimizedVerticesOut < optimized.optimizedVerticesIn &&
                    optimized.optimizedFacesOut <= optimized.optimizedFacesIn &&
                    optimized.worstNormalTurnDegrees <= 0.5,
                "Vertices weld within the normal limit");
        auto before = archiveOf(off);
        auto after = archiveOf(optimized);
        std::istringstream wire(textOf(after.read("jkl/joja_scene.jkl")));
        const auto level3 = loadFromJKL(wire, {.projectType = ProjectType::MOTS});
        for ( std::size_t index = 0; index < off.models.size(); ++index )
        {
            const auto a = loadFrom3DO(before.read("3do/" + off.models[index].name), {.applyHierarchyTransforms = false});
            const auto b = loadFrom3DO(after.read("3do/" + optimized.models[index].name), {.applyHierarchyTransforms = false});
            require(model3doEngineLimitIssues(b).empty(), "Optimized model fits loader limits");
            const auto& ma = a.meshes.at(0);
            const auto& mb = b.meshes.at(0);
            require(mb.vertices.size() <= ma.vertices.size() && mb.faces.size() <= ma.faces.size(),
                    "Optimization never adds vertices or faces");
            std::set<std::tuple<double, double, double, float, float, float>> ca, cb;
            std::map<std::int32_t, double> areaA, areaB;
            std::size_t fanA = 0, fanB = 0;
            const auto collect = [](const Model3doMesh& mesh, auto& corners, auto& areas, std::size_t& fan)
            {
                for ( const auto& face : mesh.faces )
                {
                    fan += face.vertices.size() - 2;
                    double sx = 0, sy = 0, sz = 0;
                    const auto& p0 = mesh.vertices[static_cast<std::size_t>(face.vertices[0])];
                    for ( std::size_t c = 0; c < face.vertices.size(); ++c )
                    {
                        const auto& v = mesh.vertices[static_cast<std::size_t>(face.vertices[c])];
                        const auto& t = mesh.texVertices[static_cast<std::size_t>(face.texVertices[c])];
                        corners.insert({v.x, v.y, v.z, v.color.r, t.u, t.v});
                        if ( c >= 1 && c + 1 < face.vertices.size() )
                        {
                            const auto& q = mesh.vertices[static_cast<std::size_t>(face.vertices[c + 1])];
                            const double ux = v.x - p0.x, uy = v.y - p0.y, uz = v.z - p0.z;
                            const double wx = q.x - p0.x, wy = q.y - p0.y, wz = q.z - p0.z;
                            sx += uy * wz - uz * wy;
                            sy += uz * wx - ux * wz;
                            sz += ux * wy - uy * wx;
                        }
                    }
                    areas[face.material] += 0.5 * std::sqrt(sx * sx + sy * sy + sz * sz);
                }
            };
            collect(ma, ca, areaA, fanA);
            collect(mb, cb, areaB, fanB);
            require(ca == cb, "Every corner keeps its position, light and texture coordinates");
            require(fanA == fanB && fanB == optimized.models[index].triangles,
                    "Merged faces keep the fan-equivalent triangle count the report states");
            require(areaA.size() == areaB.size(), "Materials are unchanged");
            for ( const auto& [material, value] : areaA )
            {
                require(std::abs(areaB.at(material) - value) <= 1e-9 * std::max(1.0, value),
                        "Each material covers the same area");
            }
            const auto& record = level3.motsArchLighting.at(index);
            require(record.meshes.at(0).vertices.size() == mb.vertices.size(),
                    "MotS ARCH lighting follows the welded vertex table");
        }
    }
    JoJaImportOptions rooms;
    rooms.allowPlaceholders = true;
    rooms.geometryMode = JoJaGeometryMode::HybridSectors;
    rooms.trianglesPerModel = 400;
    rooms.maxTriangles = 200000;
    const auto plainHybrid = prepareJoJaImport(sed::tests::makeRavenBspRoomsFixture(), rooms);
    rooms.meshOptimization = MeshOptimization::Strict;
    const auto hybrid = prepareJoJaImport(sed::tests::makeRavenBspRoomsFixture(), rooms);
    require(hybrid.optimizedFacesIn > 0 && hybrid.optimizedFacesOut <= hybrid.optimizedFacesIn &&
                hybrid.models.size() == plainHybrid.models.size() &&
                hybrid.residualTriangles == plainHybrid.residualTriangles,
            "Hybrid residual models are optimized too, with the same chunks and coverage");
}

void runRenderPassBakeBlockers()
{
    // Drawing the generated world models in JK's ordinary Thing pass must not change which of them cast shadows
    // in a later ray bake, and JO/JA world models (move=path with no frames) must count as static architecture.
    std::vector<std::vector<sed::level::RayLightingBlocker>> blockers;
    for ( const auto pass : {JoJaWorldModelRenderPass::LevelGeometry, JoJaWorldModelRenderPass::OrdinaryThing} )
    {
        JoJaImportOptions options;
        options.allowPlaceholders = true;
        options.worldModelRenderPass = pass;
        const auto result = prepareJoJaImport(sed::tests::makeRavenBspFixture(), options);
        auto archive = archiveOf(result);
        std::istringstream wire(textOf(archive.read("jkl/joja_scene.jkl")));
        const auto level = loadFromJKL(wire, {.projectType = ProjectType::JKDF2});
        const sed::level::EmissiveResourceReader reader =
            [&](std::string_view name, std::string_view extension) -> std::optional<std::vector<std::byte>>
        {
            for ( const std::string folder : {"3do/", "mat/"} )
            {
                for ( const auto& candidate : {folder + std::string(name), folder + std::string(name) + std::string(extension)} )
                {
                    if ( archive.contains(candidate) )
                    {
                        return archive.read(candidate);
                    }
                }
            }
            return std::nullopt;
        };
        const auto resources = sed::level::resolveRayLightingResources(level, reader);
        require(resources.modelInstances == result.models.size() && !resources.blockers.empty(),
                "Every generated world model is static architecture for the ray bake");
        blockers.push_back(resources.blockers);
    }
    require(blockers[0].size() == blockers[1].size(), "Both render passes give the bake the same blockers");
    for ( std::size_t i = 0; i < blockers[0].size(); ++i )
    {
        const auto& a = blockers[0][i];
        const auto& b = blockers[1][i];
        const auto same = [](const auto& p, const auto& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
        require(a.sector == b.sector && same(a.triangle.a, b.triangle.a) && same(a.triangle.b, b.triangle.b) &&
                    same(a.triangle.c, b.triangle.c),
                "Both render passes give the bake the same blocker triangles");
    }
}

void runStraightCornerPruning()
{
    // A flat patch with even light: merging its grid leaves corners on straight edges. Loose drops
    // them only where no face of any generated model keeps a corner at the same world position.
    sed::tests::raven_fixture::Fixture fixture;
    sed::tests::raven_fixture::writeFloat(fixture.lumps[10], 5 * 80 + 8, 0.f);
    fixture.lumps[10][5 * 80 + 64] = std::byte{0};
    const auto source = fixture.bytes();
    JoJaImportOptions options;
    options.allowPlaceholders = true;
    options.target = ProjectType::MOTS;
    options.trianglesPerModel = 20;
    options.meshOptimization = MeshOptimization::Strict;
    const auto strict = prepareJoJaImport(source, options);
    options.meshOptimization = MeshOptimization::Loose;
    const auto loose = prepareJoJaImport(source, options);
    require(loose.archive.outputData == prepareJoJaImport(source, options).archive.outputData,
            "Straight-corner pruning is deterministic");
    require(strict.straightCornersRemoved == 0 && strict.report.find("fan-equivalent triangles unchanged") !=
                                                      std::string::npos,
            "Strict leaves straight corners and says the fan triangles are unchanged");
    require(loose.models.size() > 1 && loose.straightCornersRemoved > 0 &&
                loose.optimizedFanTrianglesOut + loose.straightCornersRemoved == loose.optimizedFanTrianglesIn &&
                loose.report.find("straight corners removed " + std::to_string(loose.straightCornersRemoved)) !=
                    std::string::npos,
            "Loose removes straight corners across models and reports them");
    struct Scene
    {
        std::vector<std::array<double, 3>> corners;
        std::vector<std::pair<std::array<double, 3>, std::array<double, 3>>> edges;
        std::size_t fan{};
    };
    const auto sceneOf = [](const JoJaImportResult& result)
    {
        auto archive = archiveOf(result);
        Scene scene;
        std::istringstream wire(textOf(archive.read("jkl/joja_scene.jkl")));
        const auto level = loadFromJKL(wire, {.projectType = ProjectType::MOTS});
        for ( std::size_t index = 0; index < result.models.size(); ++index )
        {
            const auto& report = result.models[index];
            const auto model = loadFrom3DO(archive.read("3do/" + report.name), {.applyHierarchyTransforms = false});
            require(model3doEngineLimitIssues(model).empty(), "Pruned models fit loader limits");
            const auto& mesh = model.meshes.at(0);
            require(level.motsArchLighting.at(index).meshes.at(0).vertices.size() == mesh.vertices.size(),
                    "MotS ARCH lighting follows the pruned vertex table");
            const auto world = [&](std::int32_t v)
            {
                const auto& p = mesh.vertices.at(static_cast<std::size_t>(v));
                return std::array<double, 3>{p.x + report.position.x, p.y + report.position.y, p.z + report.position.z};
            };
            for ( const auto& face : mesh.faces )
            {
                scene.fan += face.vertices.size() - 2;
                for ( std::size_t c = 0; c < face.vertices.size(); ++c )
                {
                    scene.corners.push_back(world(face.vertices[c]));
                    scene.edges.push_back({world(face.vertices[c]), world(face.vertices[(c + 1) % face.vertices.size()])});
                }
            }
        }
        return scene;
    };
    // Positions of corners that lie strictly inside another face's edge: T-junctions. The fixture's
    // flat triangle overlaps the patch, so the unpruned scene already has a few.
    const auto tJunctions = [](const Scene& scene)
    {
        std::set<std::tuple<long long, long long, long long>> found;
        for ( const auto& p : scene.corners )
        {
            for ( const auto& [a, b] : scene.edges )
            {
                double len2 = 0, s = 0;
                std::array<double, 3> ab{}, ap{};
                for ( int k = 0; k < 3; ++k )
                {
                    ab[k] = b[k] - a[k];
                    ap[k] = p[k] - a[k];
                    len2 += ab[k] * ab[k];
                    s += ap[k] * ab[k];
                }
                if ( !(len2 > 0) )
                {
                    continue;
                }
                s /= len2;
                double off = 0;
                for ( int k = 0; k < 3; ++k )
                {
                    off += (ap[k] - s * ab[k]) * (ap[k] - s * ab[k]);
                }
                if ( s * s * len2 > 1e-10 && (1 - s) * (1 - s) * len2 > 1e-10 && s > 0 && s < 1 && off < 1e-12 )
                {
                    found.insert({std::llround(p[0] * 1e4), std::llround(p[1] * 1e4), std::llround(p[2] * 1e4)});
                }
            }
        }
        return found;
    };
    const auto before = sceneOf(strict);
    const auto after = sceneOf(loose);
    require(after.fan == loose.optimizedFanTrianglesOut && before.fan == loose.optimizedFanTrianglesIn,
            "The written models match the reported fan triangles");
    const auto existing = tJunctions(before);
    for ( const auto& position : tJunctions(after) )
    {
        require(existing.contains(position), "Pruning adds no T-junction in any model");
    }
}
} // namespace

int main()
{
    int failures = 0;
    for ( auto test : {run, runMaterialLayout, runManifestBinding, runLocaleDeterminism, runFootstepDisclosure,
                       runSourceProfiles, runModelSceneArchReadback,
                       runMixedFootstepsAndStarts, runTargetPrecision, runMaskedHybridMaterial, runHybridImport,
                       runNativeSurfaceLight<ProjectType::JKDF2>, runNativeSurfaceLight<ProjectType::MOTS>,
                       runWorldModelRenderPass, runSourceClips, runPlayerClipFootsteps, runMeshOptimization, runStraightCornerPruning,
                       runRenderPassBakeBlockers} )
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
