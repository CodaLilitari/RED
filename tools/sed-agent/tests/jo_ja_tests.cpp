#include "../agent_workspace.hpp"
#include "sed/assets/gob.hpp"
#include "../../../src/core/tests/assets/raven_bsp_fixture.hpp"
#include "sed/assets/mat_write.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
using namespace sed_agent;

void check(bool value, const char* message)
{
    if ( !value )
    {
        throw std::runtime_error(message);
    }
}
void write(const fs::path& path, std::string_view data)
{
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(out.good(), "write test fixture");
}
template <class Fn> void rejects(Fn action, int expected)
{
    try
    {
        action();
    }
    catch ( const AgentError& e )
    {
        check(e.code == expected, e.message.c_str());
        return;
    }
    throw std::runtime_error("Expected refusal");
}
Json file(const fs::path& path, std::string_view data)
{
    Json row = Json::makeObject();
    row.set("path", Json::makeString(path.string()));
    row.set("sha256", Json::makeString(sha256Hex(data)));
    return row;
}
void lightmapFidelityTest(const fs::path& dir)
{
    using namespace sed::tests::raven_fixture;
    Fixture fixture;
    // The nondegenerate triangle uses source-local vertices 0, 3 and 1.
    const auto texel = [&](int vertex, int x, int y)
    {
        const auto start = static_cast<std::size_t>(vertex + 1) * 80;
        writeFloat(fixture.lumps[10], start + 20, (x + 0.5f) / 128);
        writeFloat(fixture.lumps[10], start + 24, (y + 0.5f) / 128);
    };
    texel(0, 16, 16);
    texel(3, 64, 16);
    texel(1, 16, 64);
    write32(fixture.lumps[13], 36, 0);
    auto& atlas = fixture.lumps[14];
    atlas.resize(128 * 128 * 3);
    std::fill_n(atlas.begin() + (128 * 32 + 32) * 3, 3, std::byte{255});
    const auto path = dir / "lightmap.bsp";
    const auto inspect = [&](std::string surface = "0", std::string steps = "4")
    {
        const auto bytes = fixture.bytes();
        const std::string data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        write(path, data);
        const auto result = runCommand("jo-ja-inspect", {path.string(), "--lightmap", surface, steps});
        check(result.object.at("sourceSha256").str == sha256Hex(data), "lightmap witness is bound to source bytes");
        return result.object.at("sourceLightmap");
    };
    const auto schema = runCommand("schema", {"jo-ja-inspect"});
    const auto& fields = schema.object.at("properties").object.at("sourceLightmap").object.at("properties").object;
    check(fields.contains("surface") && fields.at("patchSubdivisions").object.at("minimum").number == 1 &&
              fields.at("patchSubdivisions").object.at("maximum").number == 16,
          "discovery advertises the source surface selector and bounded patch steps");
    const auto spot = inspect();
    const auto& loss = spot.object.at("rgbInterpolation").object;
    check(std::abs(loss.at("maxChannelError").number - 1.0) < 1e-10 &&
              loss.at("triangle").number == 0 && spot.object.at("sampleCount").number == 28,
          "black corners miss the white interior of the synthetic source triangle");
    for ( const auto& weight : loss.at("barycentric").array )
    {
        check(std::abs(weight.number - 1.0 / 3.0) < 1e-10, "the strongest witness is the triangle centroid");
    }
    check(spot.object.at("status").str == "sampled" && spot.object.at("sampledTriangles").number == 1 &&
              spot.object.at("probesPerTriangle").number == 28 && spot.object.at("sampleLimit").number == 28 * 65536 &&
              loss.at("barycentric").array.size() == 3 && loss.at("sourceRgb").array.at(0).number > 0.999999 &&
              loss.at("approximationRgb").array.at(0).number == 0 &&
              spot.object.at("cornerScalar").object.at("maxChannelError").number == 0,
          "interior loss is independent of corner color loss and the degenerate triangle uses no probes");
    const auto baseline = runCommand("jo-ja-inspect", {path.string()});
    const auto defaultSteps = runCommand("jo-ja-inspect", {path.string(), "--lightmap", "0"});
    check(!baseline.find("sourceLightmap") && toJsonString(defaultSteps.object.at("sourceLightmap")) == toJsonString(spot),
          "plain inspection stays unchanged and default patch steps reproduce the explicit request");

    for ( std::size_t at = 0; at < atlas.size(); at += 3 )
    {
        const auto x = (at / 3) % 128;
        const auto y = at / (3 * 128);
        std::fill_n(atlas.begin() + at, 3, static_cast<std::byte>(x + y));
    }
    const auto linear = inspect();
    check(linear.object.at("rgbInterpolation").object.at("maxChannelError").number < 1e-12 &&
              linear.object.at("cornerScalar").object.at("maxChannelError").number < 1e-7,
          "affine neutral lighting is reproduced to numeric precision");
    for ( std::size_t at = 0; at < atlas.size(); at += 3 )
    {
        atlas[at] = std::byte{255};
        atlas[at + 1] = atlas[at + 2] = std::byte{};
    }
    const auto red = inspect();
    check(red.object.at("rgbInterpolation").object.at("maxChannelError").number < 1e-12 &&
              std::abs(red.object.at("cornerScalar").object.at("maxChannelError").number - 0.7874) < 1e-7,
          "uniform red loses only its corner chroma under the importer's scalar conversion");

    const auto unlit = inspect("1");
    check(unlit.object.at("status").str == "no-first-atlas" && unlit.object.at("sampleCount").number == 0 &&
              unlit.object.at("rgbInterpolation").type == Json::Type::Null &&
              unlit.object.at("cornerScalar").type == Json::Type::Null,
          "vertex-lit surfaces report unavailable atlas evidence");
    write32(fixture.lumps[13], 148 + 36, 0);
    const auto patch = inspect("1", "2");
    check(patch.object.at("patchSubdivisions").number == 2 && patch.object.at("sampledTriangles").number == 8 &&
              patch.object.at("sampleCount").number == 224, "patch sampling follows the requested tessellation");
    rejects([&] { inspect("2"); }, kExitUsage);
    rejects([&] { inspect("-1"); }, kExitUsage);
    rejects([&] { inspect("0", "0"); }, kExitUsage);
    rejects([&] { inspect("0", "17"); }, kExitUsage);

    write32(fixture.lumps[13], 8, 4);
    const auto flare = inspect();
    check(flare.object.at("status").str == "no-triangles" && flare.object.at("sampleCount").number == 0 &&
              flare.object.at("rgbInterpolation").type == Json::Type::Null,
          "a surface with an atlas and no drawable triangles reports no measurements");
    // A 25 x 25 grid has 144 quadratic blocks: at 16 steps it exceeds the per-surface limit.
    fixture.lumps[10].resize((25 * 25 + 1) * 80);
    write32(fixture.lumps[13], 148 + 16, 25 * 25);
    write32(fixture.lumps[13], 148 + 140, 25);
    write32(fixture.lumps[13], 148 + 144, 25);
    rejects([&] { inspect("1", "16"); }, kExitRefusal);
}

void reimportTest(const fs::path& dir, const Json& original, const fs::path& generated)
{
    using sed::assets::GobArchive;
    const auto base = GobArchive::open(generated);
    std::vector<std::pair<std::string, std::string>> files;
    std::string sky, skyName;
    for (const auto& e : base.entries())
    {
        const auto bytes = base.read(e.name);
        files.emplace_back(e.name, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (e.name.ends_with(".mat") && skyName.empty())
        {
            skyName = sed::assets::canonicalRuntimeGobEntryName(e.name);
            sky = files.back().second;
            sky.back() ^= 0x11; // A pixel in the generated single-mip indexed material.
            files.back().second = sky;
        }
    }
    check(!sky.empty(), "the conversion has a real indexed MAT for the authored sky");
    std::string palette(64 + 256 * 3 + 64 * 256, '\0');
    palette.replace(0, 4, "CMP ");
    palette[64 + 3] = 96; // Palette index 1 is the mapper's red accent.
    const char soundBytes[] = "RIFF\x26\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x40\x1f\0\0\x40\x1f\0\0\x01\0\x08\0" "data\x02\0\0\0\x80\x80";
    const std::string sound(soundBytes, sizeof(soundBytes) - 1);
    files.emplace_back("sound/wind.wav", sound);
    files.emplace_back("misc/authored.cmp", palette);
    const auto declaration = [&](const std::string& entry, const std::string& role)
    {
        Json row = Json::makeObject();
        row.set("entry", Json::makeString(entry));
        row.set("role", Json::makeString(role));
        return row;
    };
    Json roles = Json::makeArray();
    roles.array.push_back(declaration(skyName, "sky"));
    roles.array.push_back(declaration("sound/wind.wav", "ambience"));
    roles.array.push_back(declaration("misc/authored.cmp", "palette"));
    const auto authored = dir / "authored.gob";
    auto edited = sed::assets::buildGobBytes(files);
    write(authored, edited);
    Json recipe = original;
    recipe.set("patchSubdivisions", Json::makeNumber(2));
    recipe.set("trianglesPerModel", Json::makeNumber(1));
    Json overlay = Json::makeObject();
    overlay.set("baselineRecipe", original);
    overlay.set("baselineOutputSha256", Json::makeString(sha256Hex(readFileBytes(generated))));
    overlay.set("authoredArchive", file(authored, edited));
    overlay.set("overrides", roles);
    const auto request = dir / "overlay.json";
    const auto output = dir / "overlay.gob";
    const auto plan = [&]()
    {
        recipe.set("reimport", overlay);
        write(request, toJsonString(recipe));
        return runCommand("jo-ja-plan", { request.string() });
    };
    auto visibility = Json::makeObject();
    visibility.set("viewCluster", Json::makeNumber(-1));
    recipe.set("sourceVisibility", visibility);
    const auto positive = plan();
    const auto stable = plan();
    const auto& review = positive.object.at("reimport").object;
    check(positive.object.at("sourceVisibility").object.at("generatedOutputSha256").str ==
              review.at("regenerated").object.at("generatedArchiveSha256").str &&
              positive.object.at("outputSha256").str != review.at("regenerated").object.at("generatedArchiveSha256").str,
          "visibility provenance identifies generator geometry separately from the authored overlay");
    check(positive.object.at("outputSha256").str == stable.object.at("outputSha256").str
            && positive.object.at("manifestSha256").str == stable.object.at("manifestSha256").str
            && review.at("changes").array.size() == 3
            && std::all_of(review.at("changes").array.begin(), review.at("changes").array.end(),
                   [](const Json& row) { return row.object.at("decision").str == "preserve"; })
            && review.at("regenerated").object.at("generatedArchiveSha256").str
                != overlay.object.at("baselineOutputSha256").str,
        "changed tessellation/partitioning and a declared authored sky produce a deterministic review");
    const auto args = std::vector<std::string> { request.string(), output.string(),
        positive.object.at("outputSha256").str, positive.object.at("manifestSha256").str };
    runCommand("jo-ja-apply", args);
    const auto applied = GobArchive::open(output);
    check(applied.entrySha256Hex(skyName) == sha256Hex(sky)
            && applied.entrySha256Hex("sound/wind.wav") == sha256Hex(sound)
            && applied.entrySha256Hex("misc/authored.cmp") == sha256Hex(palette) && readFileBytes(authored) == edited
            && sha256Hex(readFileBytes(generated)) == overlay.object.at("baselineOutputSha256").str,
        "create-only reimport preserves the authored bytes and both original archives");
    const auto refused = dir / "conflict.gob";
    auto expectConflict = [&](const std::string& reason, const std::string& entry)
    {
        try
        {
            plan();
            throw std::runtime_error("expected blocked reimport");
        }
        catch (const AgentError& e)
        {
            check(e.errorCode == "joJaReimportConflict" && e.hasPayload,
                "reimport provides structured conflict evidence");
            const auto& conflicts = e.payload.object.at("reimport").object.at("conflicts").array;
            const auto found = std::find_if(conflicts.begin(), conflicts.end(), [&](const Json& row)
                { return row.object.at("entry").str == entry && row.object.at("reason").str == reason; });
            check(found != conflicts.end(), "conflict identifies the exact entry and cause");
        }
        rejects([&] { runCommand("jo-ja-apply", { request.string(), refused.string(), args[2], args[3] }); },
            kExitConflict);
        check(!fs::exists(refused), "conflicting reimport publishes no episode");
    };
    overlay.set("overrides", Json::makeArray());
    expectConflict("undeclaredManualChange", skyName);
    overlay.set("overrides", roles);
    const auto changed = sed::tests::makeRavenBspFixture(false);
    const std::string changedSource(reinterpret_cast<const char*>(changed.data()), changed.size());
    const auto replacedSource = dir / "changed-source.bsp";
    write(replacedSource, changedSource);
    recipe.set("source", file(replacedSource, changedSource));
    expectConflict("sourceIdentityChanged", skyName);
    recipe.set("source", original.object.at("source"));
    files.emplace_back("cog/throwables.cog", "symbols\nmessage startup\nend\ncode\nstartup:\nreturn;\nend\n");
    edited = sed::assets::buildGobBytes(files);
    write(authored, edited);
    overlay.set("authoredArchive", file(authored, edited));
    roles.array.push_back(declaration("cog/throwables.cog", "throwables"));
    overlay.set("overrides", roles);
    expectConflict("topologyChanged", "cog\\throwables.cog");
    std::string levelName;
    for (auto& [name, bytes] : files)
    {
        if (!name.ends_with(".jkl"))
            continue;
        levelName = sed::assets::canonicalRuntimeGobEntryName(name);
        const auto gravity = bytes.find("WORLD GRAVITY ");
        check(gravity != std::string::npos, "generated level exposes its authored gravity header");
        bytes.replace(gravity, bytes.find('\n', gravity) - gravity, "WORLD GRAVITY 3.125");
    }
    check(!levelName.empty(), "baseline conversion contains a native engine level");
    roles.array.push_back(declaration(levelName, "gameplay"));
    overlay.set("overrides", roles);
    edited = sed::assets::buildGobBytes(files);
    write(authored, edited);
    overlay.set("authoredArchive", file(authored, edited));
    try
    {
        plan();
        throw std::runtime_error("expected authored level conflict");
    }
    catch (const AgentError& e)
    {
        const auto& rows = e.payload.object.at("reimport").object.at("conflicts").array;
        const auto match = std::find_if(
            rows.begin(), rows.end(), [&](const Json& row) { return row.object.at("entry").str == levelName; });
        check(match != rows.end() && match->find("authoredFields") && match->find("regeneratedFields")
                && toJsonString(*match->find("authoredFields")).find("gravity") != std::string::npos,
            "conflict on changed level geometry identifies the authored header field using the bounded semantic "
            "comparator");
    }
    recipe = original;
    const auto untouched = plan();
    check(untouched.object.at("reimport").object.at("conflicts").array.empty(),
        "the original source and topology admit a declared gameplay script alongside the sky");
    std::erase_if(files, [](const auto& entry) { return entry.first.ends_with(".jkl"); });
    edited = sed::assets::buildGobBytes(files);
    write(authored, edited);
    overlay.set("authoredArchive", file(authored, edited));
    expectConflict("requiredEpisodeEntry", levelName);
    overlay.set("baselineOutputSha256", Json::makeString(std::string(64, '0')));
    rejects([&] { plan(); }, kExitConflict);
}

void invisibleClipTest(const fs::path& dir)
{
    using namespace sed::tests::raven_fixture;
    Fixture fixture;
    auto& shaders = fixture.lumps[1];
    shaders.resize(144);
    std::string_view clipName = "system/player_and_shot_clip";
    for (std::size_t i = 0; i < clipName.size(); ++i)
        shaders[72 + i] = static_cast<std::byte>(clipName[i]);
    write32(shaders, 72 + 68, 0x90);
    fixture.lumps[2].resize(6 * 16);
    fixture.lumps[8].resize(12);
    write32(fixture.lumps[7], 36, 1);
    write32(fixture.lumps[8], 4, 6);
    write32(fixture.lumps[8], 8, 1);
    fixture.lumps[9].resize(6 * 12);
    for (std::size_t i = 0; i < 6; ++i)
    {
        writeFloat(fixture.lumps[2], i * 16 + (i / 2) * 4, i % 2 ? 1.f : -1.f);
        writeFloat(fixture.lumps[2], i * 16 + 12, 4.f);
        write32(fixture.lumps[9], i * 12, i);
        write32(fixture.lumps[9], i * 12 + 4, 1);
        write32(fixture.lumps[9], i * 12 + 8, UINT32_MAX);
    }
    const auto bytes = fixture.bytes();
    const auto path = dir / "invisible.bsp";
    write(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    const auto result = runCommand("jo-ja-inspect", {path.string(), "--brush", "0", "1"});
    const auto& brush = result.object.at("collisionBrush").object;
    check(result.object.at("worldShaders").array.size() == 1
            && result.object.at("collisionBrushShaders").array[0].object.at("shader").number == 1
            && !brush.at("hasLinkedDrawSurface").boolean && brush.at("playerClip").boolean && brush.at("shotClip").boolean
            && !brush.at("solid").boolean && brush.at("sides").array.size() == 5
            && brush.at("sides").array.front().object.at("distance").number == 4
            && brush.at("sides").array.front().object.at("drawSurface").type == Json::Type::Null,
        "a non-rendering clip brush reports its own mask, exact planes and independent draw mapping");
    rejects([&]{ runCommand("jo-ja-inspect", {path.string(),"--brush","1"}); }, kExitUsage);
}

void test(const fs::path& dir)
{
    invisibleClipTest(dir);
    const auto bytes = sed::tests::makeRavenBspFixture();
    const std::string input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto source = dir / "scene.bsp";
    const auto request = dir / "import.json";
    const auto output = dir / "new.gob";
    write(source, input);
    Json spec = Json::makeObject();
    spec.set("source", file(source, input));
    spec.set("allowPlaceholders", Json::makeBool(true));
    auto plan = [&](const Json& value)
    {
        write(request, toJsonString(value));
        return runCommand("jo-ja-plan", {request.string()});
    };
    const auto inventory = runCommand("jo-ja-inspect", {source.string()});
    check(inventory.object.at("sourceSha256").str == sha256Hex(input) &&
              inventory.object.at("worldShaders").array.size() == 1 &&
              inventory.object.at("entityClasses").object.at("worldspawn").number == 1,
          "inspection inventory");
    const auto reviewed = plan(spec);
    check(!reviewed.object.at("runtimeVerified").boolean && reviewed.object.at("triangles").number == 33 &&
              !fs::exists(output),
          "plan without publication");
    std::vector<std::string> args{request.string(), output.string(), reviewed.object.at("outputSha256").str,
                                  reviewed.object.at("manifestSha256").str};
    const auto schema = runCommand("schema", {"jo-ja-plan"});
    check(schema.object.at("properties").object.contains("skipShaders"), "schema exposes explicit shader skips");
    const auto& properties = schema.object.at("properties").object;
    check(properties.contains("geometryMode") && properties.at("geometryMode").object.at("default").str == "model-scene"
              && properties.at("maxSectors").object.at("maximum").number == 96
              && properties.at("maxFragments").object.at("maximum").number == 1000000,
          "hybrid request schema preserves model-scene defaults and bounds sectors and fragments");
    check(properties.at("sourcePlayerClip").object.at("default").str == "omit" &&
          properties.at("movementBarrierBrushes").object.at("uniqueItems").boolean,
          "JK source brush collision is explicit and per-brush IDs are unique");
    auto clipRequest = spec;
    clipRequest.set("sourcePlayerClip", Json::makeString("omit"));
    const auto omitted = plan(clipRequest);
    check(omitted.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str &&
          omitted.object.at("outputSha256").str == reviewed.object.at("outputSha256").str,
          "Explicit omission replays existing recipes byte for byte");
    clipRequest.set("sourcePlayerClip", Json::makeString("standable"));
    const auto clipReview = plan(clipRequest);
    check(clipReview.object.at("playerClips").array.empty() &&
          clipReview.object.at("sourcePlayerClip").str == "standable" &&
          clipReview.object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str,
          "An explicit clip recipe reports the actual selected brushes and binds the policy");
    clipRequest.set("sourcePlayerClip", Json::makeString("guess"));
    rejects([&] { plan(clipRequest); }, kExitUsage);
    check(properties.contains("sourceVisibility"), "schema exposes optional source visibility evidence");
    auto query = Json::makeObject();
    query.set("viewCluster", Json::makeNumber(0));
    auto evidenceSpec = spec;
    evidenceSpec.set("sourceVisibility", query);
    const auto evidence = plan(evidenceSpec);
    check(!reviewed.find("sourceVisibility") &&
          evidence.object.at("outputSha256").str == reviewed.object.at("outputSha256").str &&
          evidence.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "source visibility only affects the requested diagnostic");
    const auto& page = evidence.object.at("sourceVisibility");
    check(!page.object.at("rowUsable").boolean && page.object.at("outsidePvs").number == 0 &&
          page.object.at("unknown").number == page.object.at("total").number,
          "missing source PVS retains every native surface and generated mesh as unknown");
    auto explicitDefaults = spec;
    explicitDefaults.set("sourceProfile", Json::makeString("legacy"));
    const auto legacy = plan(explicitDefaults);
    check(legacy.object.at("sourceProfile").str == "legacy" &&
              legacy.object.at("outputSha256").str == reviewed.object.at("outputSha256").str &&
              legacy.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "explicit legacy source rules reproduce archives and review hashes for older recipes");
    auto profiled = spec;
    profiled.set("sourceProfile", Json::makeString("ja-mp"));
    const auto academy = plan(profiled);
    check(academy.object.at("sourceProfile").str == "ja-mp" &&
              academy.object.at("materials").array.at(0).object.at("sourceFootstep").str == "stone" &&
              academy.object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str,
          "agent receipt identifies the source profile and selected sound family");
    profiled.set("sourceProfile", Json::makeString("guess"));
    rejects([&] { plan(profiled); }, kExitUsage);
    check(properties.at("worldModelRenderPass").object.at("default").str == "levelGeometry",
          "the original world model render pass remains the schema default");
    auto renderPass = spec;
    renderPass.set("worldModelRenderPass", Json::makeString("levelGeometry"));
    const auto explicitPass = plan(renderPass);
    check(explicitPass.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str &&
          explicitPass.object.at("outputSha256").str == reviewed.object.at("outputSha256").str,
          "explicit levelGeometry replays existing recipes byte for byte");
    renderPass.set("worldModelRenderPass", Json::makeString("ordinaryThing"));
    const auto ordinary = plan(renderPass);
    check(ordinary.object.at("worldModelRenderPass").str == "ordinaryThing" &&
          ordinary.object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str &&
          ordinary.object.at("outputSha256").str != reviewed.object.at("outputSha256").str,
          "JK ordinary Thing rendering is explicit and bound to the reviewed recipe and archive");
    renderPass.set("target", Json::makeString("mots"));
    rejects([&] { plan(renderPass); }, kExitRefusal);
    renderPass.set("worldModelRenderPass", Json::makeString("guess"));
    rejects([&] { plan(renderPass); }, kExitUsage);
    auto meshOff = spec;
    meshOff.set("meshOptimization", Json::makeString("off"));
    const auto unoptimized = plan(meshOff);
    check(unoptimized.object.at("meshOptimization").str == "off" &&
              !unoptimized.object.contains("optimizedVertices") &&
              unoptimized.object.at("outputSha256").str == reviewed.object.at("outputSha256").str &&
              unoptimized.object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "explicit meshOptimization off reproduces existing recipes");
    for ( const char* level : {"strict", "loose"} )
    {
        auto optimizedSpec = spec;
        optimizedSpec.set("meshOptimization", Json::makeString(level));
        const auto optimized = plan(optimizedSpec);
        const auto& vertices = optimized.object.at("optimizedVertices").object;
        const auto& faces = optimized.object.at("optimizedFaces").object;
        check(optimized.object.at("meshOptimization").str == level &&
                  vertices.at("after").number < vertices.at("before").number &&
                  faces.at("after").number <= faces.at("before").number &&
                  optimized.object.at("worstNormalTurnDegrees").number <= 0.5 &&
                  optimized.object.at("triangles").number == reviewed.object.at("triangles").number &&
                  optimized.object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str &&
                  optimized.object.contains("straightCornersRemoved") == (std::string(level) == "loose") &&
                  optimized.object.contains("fanTriangles") == (std::string(level) == "loose"),
              "mesh optimization welds vertices, keeps source triangles and binds the recipe; loose reports "
              "the straight corners it removed");
    }
    check(properties.at("meshOptimization").object.at("default").str == "off",
          "the request schema documents meshOptimization with off as the default");
    meshOff.set("meshOptimization", Json::makeString("aggressive"));
    rejects([&] { plan(meshOff); }, kExitUsage);
    explicitDefaults.set("geometryMode", Json::makeString("model-scene"));
    explicitDefaults.set("maxSectors", Json::makeNumber(64));
    explicitDefaults.set("maxFragments", Json::makeNumber(500000));
    check(plan(explicitDefaults).object.at("manifestSha256").str == reviewed.object.at("manifestSha256").str,
          "explicit model-scene defaults preserve the original recipe");
    explicitDefaults.set("maxSectors", Json::makeNumber(32));
    check(plan(explicitDefaults).object.at("manifestSha256").str != reviewed.object.at("manifestSha256").str,
          "a nondefault inactive budget is still bound by the model-scene manifest");
    const auto rooms = sed::tests::makeRavenBspRoomsFixture(true);
    const std::string roomsInput(reinterpret_cast<const char*>(rooms.data()), rooms.size());
    const auto roomsPath = dir / "rooms.bsp";
    write(roomsPath, roomsInput);
    auto hybrid = spec;
    hybrid.set("source", file(roomsPath, roomsInput));
    hybrid.set("geometryMode", Json::makeString("hybrid-sectors"));
    hybrid.set("trianglesPerModel", Json::makeNumber(479));
    hybrid.set("maxTriangles", Json::makeNumber(250000));
    hybrid.set("maxSectors", Json::makeNumber(2));
    hybrid.set("maxFragments", Json::makeNumber(1000000));
    const auto hybridPlan = plan(hybrid);
    const auto references = runCommand("jo-ja-inspect", {roomsPath.string(), "--visibility", "0"});
    const auto& owners = references.object.at("sourceVisibility").object.at("surfaces").array;
    check(owners.size() == 2 && owners[0].object.at("clusters").array.at(0).number == 0 &&
          owners[1].object.at("clusters").array.at(0).number == 1 &&
          owners[1].object.at("state").str == "outside-pvs", "read the actual padded source PVS and leaf links");
    auto hybridDiagnostic = hybrid;
    hybridDiagnostic.set("sourceVisibility", query);
    const auto hp = plan(hybridDiagnostic);
    const auto& diagnostic = hp.object.at("sourceVisibility");
    check(diagnostic.object.at("generatedOutputSha256").str == hybridPlan.object.at("outputSha256").str &&
          hp.object.at("manifestSha256").str == hybridPlan.object.at("manifestSha256").str,
          "diagnostics preserve the generated hybrid archive and recipe");
    bool native = false, residual = false, generated = false;
    for ( const auto& target : diagnostic.object.at("targets").array )
    {
        const auto& sourceIds = target.object.at("sourceSurfaces").array;
        bool visible = false;
        for ( const auto& id : sourceIds ) visible |= id.number == 0;
        const auto state = target.object.at("state").str;
        check(state == (sourceIds.empty() ? "unknown" : visible ? "potentially-visible" : "outside-pvs"),
              "each emitted target uses its own contributing draw surfaces");
        if ( const auto* model = target.find("model") )
        {
            const auto index = static_cast<std::size_t>(target.object.at("thing").number);
            const auto& modelRow = hp.object.at("models").array.at(index);
            check(model->str == modelRow.object.at("name").str && target.object.at("mesh").number == 0 &&
                  toJsonString(target.object.at("sourceSurfaces")) == toJsonString(modelRow.object.at("sourceSurfaces")),
                  "residual packet maps to its placed model and actual mesh");
            residual = true;
        }
        else
        {
            check(target.find("surface") && target.find("sector"), "native reference identifies its archived local surface");
            native |= !sourceIds.empty();
            generated |= sourceIds.empty();
        }
    }
    check(native && residual && generated, "review promoted native faces, residual models and generated boundaries");
    auto paged = query;
    paged.set("offset", Json::makeNumber(1));
    hybridDiagnostic.set("sourceVisibility", paged);
    check(toJsonString(plan(hybridDiagnostic).object.at("sourceVisibility").object.at("targets").array.front()) ==
          toJsonString(diagnostic.object.at("targets").array.at(1)), "target page offsets address the same generated order");
    const auto diagnosticOutput = dir / "diagnostic.gob";
    const auto applied = runCommand("jo-ja-apply", {request.string(), diagnosticOutput.string(),
        hp.object.at("outputSha256").str, hp.object.at("manifestSha256").str});
    check(applied.object.at("sourceVisibility").object.at("offset").number == 1 &&
          sha256Hex(readFileBytes(diagnosticOutput)) == hybridPlan.object.at("outputSha256").str,
          "normal hash-bound apply publishes the identical archive with its selected evidence page");

    check(hybridPlan.object.at("geometryMode").str == "hybrid-sectors" && hybridPlan.object.at("sectors").number == 2
              && hybridPlan.object.at("portalPairs").number > 0 && hybridPlan.object.at("nativeFaces").number > 0
              && hybridPlan.object.at("nativePromotionFallbacks").number == 0
              && hybridPlan.object.at("residualFaces").number > 0
              && hybridPlan.object.at("residualTriangles").number >= hybridPlan.object.at("residualFaces").number
              && hybridPlan.object.at("sourceArea").number > 0
              && !hybridPlan.object.at("models").array.empty() && hybridPlan.object.at("models").array.front().object.contains("sector")
              && hybridPlan.object.at("nativeBoundaryCollision").boolean && !hybridPlan.object.at("renderMeshCollision").boolean
              && !hybridPlan.object.at("runtimeVerified").boolean && !fs::exists(output),
          "hybrid plan accounts for native rooms, portals, residual detail and model ownership");
    hybrid.set("maxFragments", Json::makeNumber(999999));
    write(request, toJsonString(hybrid));
    rejects([&] { runCommand("jo-ja-apply", {request.string(), output.string(), hybridPlan.object.at("outputSha256").str,
                                             hybridPlan.object.at("manifestSha256").str}); }, kExitConflict);
    check(!fs::exists(output), "changing the hybrid fragment budget revokes reviewed output");
    for ( const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
              {"maxFragments", 1000001}, {"maxSectors", 97}, {"maxTriangles", 250001}, {"trianglesPerModel", 480}} )
    {
        auto invalid = hybrid;
        invalid.set(name, Json::makeNumber(value));
        rejects([&] { plan(invalid); }, kExitUsage);
    }
    auto unknownMode = spec;
    unknownMode.set("geometryMode", Json::makeString("automatic"));
    rejects([&] { plan(unknownMode); }, kExitUsage);
    Json audioSpec = spec;
    audioSpec.set("audioPolicy", Json::makeString("source-import"));
    bool unavailable = false;
    try
    {
        (void)plan(audioSpec);
    }
    catch ( const AgentError& e )
    {
        unavailable = e.code == kExitRefusal && e.errorCode == "joJaSourceAudioUnavailable";
    }
    check(unavailable && !fs::exists(output), "source audio import explicitly refuses without an archive");
    write(request, toJsonString(spec));
    auto stale = args;
    stale[3] = std::string(64, '0');
    rejects([&] { runCommand("jo-ja-apply", stale); }, kExitConflict);
    stale = args;
    stale[2] = std::string(64, '0');
    rejects([&] { runCommand("jo-ja-apply", stale); }, kExitConflict);
    write(source, input + "x");
    rejects([&] { runCommand("jo-ja-apply", args); }, kExitConflict);
    check(!fs::exists(output), "stale review leaves no output");
    write(source, input);
    runCommand("jo-ja-apply", args);
    auto wrongTarget = args;
    wrongTarget[1] = (dir / "wrong.goo").string();
    rejects([&] { runCommand("jo-ja-apply", wrongTarget); }, kExitUsage);
    check(!fs::exists(wrongTarget[1]), "wrong target never publishes");
    const auto alias = dir / "source-alias.gob";
    std::error_code linkError;
    fs::create_hard_link(source, alias, linkError);
    if ( !linkError )
    {
        auto aliased = args;
        aliased[1] = alias.string();
        rejects([&] { runCommand("jo-ja-apply", aliased); }, kExitRefusal);
        check(readFileBytes(source) == input && readFileBytes(alias) == input, "physical input aliases remain intact");
    }
    const auto published = readFileBytes(output);
    const auto captured = runCommand("jo-ja-inspect", {output.string(), "--target", "jkl/joja_scene.jkl", "0", "0"});
    check(captured.object.at("archiveSha256").str == sha256Hex(readFileBytes(output)) &&
          !captured.object.at("runtimeVerified").boolean &&
          !captured.object.at("native").object.at("surfaces").array.empty() &&
          !captured.object.at("model").object.at("faces").array.empty(),
          "target inspection uses actual archived native and model polygons");
    const auto player = runCommand("jo-ja-inspect", {output.string(), "--target", "jkl/joja_scene.jkl", "0", "@walkplayer"});
    check(player.object.at("templateOnly").boolean && player.object.at("model").object.at("shape").str == "sphere"
              && player.object.at("model").object.at("modelGeometry").str == "externalModelUnresolved",
          "inherited stock player shape is explicit about external mesh data");
    rejects([&] {runCommand("jo-ja-inspect",{output.string(),"--target","jkl/joja_scene.jkl","999","0"});},kExitUsage);
    reimportTest(dir,spec,output);
    check(sha256Hex(published) == args[2], "apply publishes reviewed bytes");
    rejects([&] { runCommand("jo-ja-apply", args); }, kExitRefusal);
    check(readFileBytes(output) == published, "existing archive preserved");
    for ( const auto& [name, value] :
          std::initializer_list<std::pair<const char*, double>>{{"unknown", 1},
                                                                {"maxModels", 513},
                                                                {"patchSubdivisions", 1.5},
                                                                {"maxTriangles", 100001},
                                                                {"scale", 0},
                                                                {"trianglesPerModel", 171}} )
    {
        Json invalid = spec;
        invalid.set(name, Json::makeNumber(value));
        rejects([&] { plan(invalid); }, kExitUsage);
    }
    Json invalidSkips = spec;
    invalidSkips.set("skipShaders", parseJson(R"(["a","a"])"));
    rejects([&] { plan(invalidSkips); }, kExitUsage);
    Json unknownSourceField = spec;
    unknownSourceField.object.at("source").set("guess", Json::makeBool(true));
    rejects([&] { plan(unknownSourceField); }, kExitUsage);
    sed::assets::RgbaImage image{};
    image.width = image.height = 8;
    image.rgba.assign(8 * 8 * 4, 255);
    const auto indexed = sed::assets::buildIndexedMat({image}, {}).bytes;
    const std::string mat(reinterpret_cast<const char*>(indexed.data()), indexed.size());
    write(dir / "wall.mat", mat);
    Json mapping = Json::makeObject();
    mapping.set("shader", Json::makeString("a"));
    mapping.set("name", Json::makeString("wall.mat"));
    mapping.set("source", file(dir / "wall.mat", mat));
    Json maps = Json::makeArray();
    maps.array.push_back(mapping);
    spec.set("materials", maps);
    rejects([&] { plan(spec); }, kExitRefusal);
}
} // namespace
int main()
{
    const auto dir = fs::temp_directory_path() /
                     ("red-jo-ja-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(dir);
    try
    {
        lightmapFidelityTest(dir);
        test(dir);
    }
    catch ( const sed_agent::AgentError& e )
    {
        std::cerr << e.message << '\n';
        fs::remove_all(dir);
        return 1;
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        fs::remove_all(dir);
        return 1;
    }
    fs::remove_all(dir);
    return 0;
}
