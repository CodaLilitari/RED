#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"

#include <algorithm>
#include "sed/level/package.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace sed::tests
{

namespace
{

const char* kPackLevel =
    "SECTION: GEORESOURCE\n"
    "World Colormaps 1\n"
    "dflt.cmp\n"
    "END\n"
    "SECTION: MATERIALS\n"
    "World Materials 1\n"
    "wall.mat\n"
    "END\n"
    "SECTION: MODELS\n"
    "World Models 1\n"
    "crate.3do\n"
    "END\n"
    "SECTION: TEMPLATES\n"
    "WORLD TEMPLATES 1\n"
    "crate none model3d=crate.3do\n"
    "END\n"
    "SECTION: COGS\n"
    "WORLD COGS 1\n"
    "0: gate.cog 0 0\n"
    "END\n";

sed::level::PackageRequest baseRequest()
{
    sed::level::PackageRequest request{};
    request.inputBytes = kPackLevel;
    request.levelFileName = "level.jkl";
    request.episodeTitle = "Test Episode";
    sed::level::PackageArchiveInput archive{};
    archive.label = "res.gob";
    archive.entries = {
        { "3do/crate.3do", "SECTION: MODEL\nMATERIALS\nwall.mat\n" },
        { "mat/wall.mat", "MATBYTES" },
        { "cog/gate.cog", "COGBYTES" },
        { "misc/cmp/dflt.cmp", "CMPBYTES" },
    };
    request.archives.push_back(std::move(archive));
    return request;
}

bool hasEntry(const sed::level::PackagePlan& plan, const std::string& path)
{
    for (const sed::level::PackageEntry& entry : plan.entries)
    {
        if (entry.path == path)
        {
            return true;
        }
    }
    return false;
}

// A one-mesh JK 3DO 2.1 with `vertices` corners in a fan of triangles.
std::string fan3do(const int vertices)
{
    std::string text = "SECTION: HEADER\n3DO 2.1\nSECTION: MODELRESOURCE\nMATERIALS 1\n0: wall.mat\n"
        "SECTION: GEOMETRYDEF\nRADIUS 1.0\nINSERT OFFSET 0.0 0.0 0.0\nGEOSETS 1\nGEOSET 0\nMESHES 1\nMESH 0\n"
        "NAME fan\nRADIUS 1.0\nGEOMETRYMODE 4\nLIGHTINGMODE 3\nTEXTUREMODE 3\n";
    text += "VERTICES " + std::to_string(vertices) + "\n";
    for (int i = 0; i < vertices; ++i)
    {
        const double a = 6.283185307179586 * i / vertices;
        text += std::to_string(i) + ": " + std::to_string(std::cos(a)) + " " + std::to_string(std::sin(a)) + " 0.0 0.0\n";
    }
    text += "TEXTURE VERTICES 1\n0: 0.0 0.0\nVERTEX NORMALS\n";
    for (int i = 0; i < vertices; ++i) text += std::to_string(i) + ": 0.0 0.0 1.0\n";
    text += "FACES " + std::to_string(vertices - 2) + "\n";
    for (int i = 0; i < vertices - 2; ++i)
    {
        text += std::to_string(i) + ": 0 0x0 4 3 3 0.0 3 0, 0 " + std::to_string(i + 1) + ", 0 "
            + std::to_string(i + 2) + ", 0\n";
    }
    text += "FACE NORMALS\n";
    for (int i = 0; i < vertices - 2; ++i) text += std::to_string(i) + ": 0.0 0.0 1.0\n";
    text += "SECTION: HIERARCHYDEF\nHIERARCHY NODES 1\n"
        "0: 0x0000 0x00001 0 -1 -1 -1 0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 fan\n";
    return text;
}

// `meshes` fans of `vertices` corners each, every mesh a child of the first.
std::string fans3do(const int vertices, const int meshes)
{
    const std::string one = fan3do(vertices);
    const std::size_t meshStart = one.find("MESH 0\n");
    const std::size_t meshEnd = one.find("SECTION: HIERARCHYDEF");
    std::string text = one.substr(0, meshStart);
    text.replace(text.find("MESHES 1"), 8, "MESHES " + std::to_string(meshes));
    for (int m = 0; m < meshes; ++m)
    {
        std::string mesh = one.substr(meshStart, meshEnd - meshStart);
        mesh.replace(0, 6, "MESH " + std::to_string(m));
        mesh.replace(mesh.find("NAME fan"), 8, "NAME fan" + std::to_string(m));
        text += mesh;
    }
    text += "SECTION: HIERARCHYDEF\nHIERARCHY NODES " + std::to_string(meshes) + "\n";
    for (int m = 0; m < meshes; ++m)
    {
        // flags, type, mesh, parent, child, sibling, number of children
        text += std::to_string(m) + ": 0x0000 0x00001 " + std::to_string(m) + " " + (m == 0 ? "-1" : "0") + " "
            + (m == 0 && meshes > 1 ? "1" : "-1") + " " + (m > 0 && m + 1 < meshes ? std::to_string(m + 1) : "-1")
            + " " + (m == 0 ? std::to_string(meshes - 1) : "0")
            + " 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 fan" + std::to_string(m) + "\n";
    }
    return text;
}

bool hasIssue(const sed::level::PackagePlan& plan, const std::string& code)
{
    for (const sed::level::PackageIssue& issue : plan.issues)
    {
        if (issue.code == code)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void runPackageTests()
{
    {
        auto request = baseRequest();
        request.inputBytes = "SECTION: SECTORS\nWORLD SECTORS 1\nSECTOR 0\n"
            "SOUND Vent.wav 0.5\nSURFACES 0 0\nEND\n";
        request.archives[0].entries = {{"sound/vent.wav", "synthetic vent sample"}};
        const auto included = sed::level::planLevelPackage(request);
        check(included.canBuild && hasEntry(included, "sound/vent.wav"),
            "package: a sector-only ambient sound is included");
        if ( included.canBuild && hasEntry(included, "sound/vent.wav") )
        {
            const auto* data = reinterpret_cast<const std::byte*>(included.outputData.data());
            const auto archive = sed::assets::GobArchive::fromBytes({data, data + included.outputData.size()});
            const auto bytes = archive.read("sound/vent.wav");
            check(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                    == "synthetic vent sample",
                "package: the physical archive contains the resolved ambient bytes");
        }
        request.archives[0].stock = true;
        const auto stock = sed::level::planLevelPackage(request);
        check(stock.canBuild && !hasEntry(stock, "sound/vent.wav") && stock.stockOmissions.size() == 1
                && stock.stockOmissions[0].path == "sound/vent.wav",
            "package: a stock ambient sound satisfies the sector without including its bytes");
        request.archives.clear();
        const auto missing = sed::level::planLevelPackage(request);
        check(!missing.canBuild && missing.outputData.empty() && hasIssue(missing, "MissingResource"),
            "package: an unresolved sector-only ambient sound blocks the archive");
    }
    {
        for (const std::string separator : {" ", "\t", " \t "})
        {
            auto request = baseRequest();
            request.inputBytes = "SECTION: GEORESOURCE\nWORLD" + separator + "COLORMAPS" + separator
                + "1\n0:\tdflt.cmp\nWORLD" + separator + "VERTICES 0\n0: outside.cmp\n"
                  "WORLD\tCOLORMAPS_BACKUP 1\n0: other.cmp\nEND\n";
            const auto plan = sed::level::planLevelPackage(request);
            check(plan.canBuild && hasEntry(plan, "misc/cmp/dflt.cmp")
                    && !hasEntry(plan, "misc/cmp/outside.cmp") && !hasEntry(plan, "misc/cmp/other.cmp"),
                "package: compact and spaced GEORESOURCE headers collect only the active colormap table");
        }
    }
    {
        auto req = baseRequest();
        req.inputBytes = "SECTION: TEMPLATES\nWorld templates 1\nrelay_sentry none soundclass=relay_audio.snd\nEND\n";
        req.archives[0].entries = {{"misc/snd/relay_audio.snd", "alert relay_ping.wav\n"}, {"sound/relay_ping.wav", "original wave"}};
        const auto complete = sed::level::planLevelPackage(req);
        check(complete.canBuild && hasEntry(complete, "misc/snd/relay_audio.snd") && hasEntry(complete, "sound/relay_ping.wav"),
            "an enemy template's soundclass brings its SND and associated action sound into a clean package");
        req.archives[0].entries.pop_back();
        const auto incomplete = sed::level::planLevelPackage(req);
        check(!incomplete.canBuild && hasIssue(incomplete, "MissingResource"),
            "an enemy with a missing soundclass action refuses package publication");
    }
    {
        // Runtime stdGob lowercases the query but indexes physical names verbatim.
        // RED's case-folded find/read alone cannot certify engine reachability.
        sed::level::PackageRequest request;
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nTile.MAT\nEND\n";
        request.levelFileName = "MiXeD.JKL";
        request.episodeTitle = "Physical archive names";
        request.extraResources.push_back({ "cogStrings.uni", "misc" });
        request.archives.push_back({ "captured-lowercase.gob", false,
            { { "mat/tile.mat", "MATERIAL_BYTES" },
                { "misc/cogstrings.uni", "COG_96001 original fixture text" } } });
        std::vector<std::string> authoredQueries;
        request.resolveResource = [&](std::string_view name, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            authoredQueries.emplace_back(name);
            return std::nullopt;
        };
        const auto plan = sed::level::planLevelPackage(request);
        check(plan.canBuild, "package: mixed authored names remain resolvable");
        check(hasEntry(plan, "jkl/mixed.jkl") && hasEntry(plan, "mat/tile.mat")
                && hasEntry(plan, "misc/cogstrings.uni"),
            "package: receipts identify canonical runtime output paths");
        check(std::find(authoredQueries.begin(), authoredQueries.end(), "Tile.MAT") != authoredQueries.end()
                && std::find(authoredQueries.begin(), authoredQueries.end(), "cogStrings.uni") != authoredQueries.end(),
            "package: physical naming preserves original authored resolver queries");
        if ( plan.canBuild )
        {
            const auto* data = reinterpret_cast<const std::byte*>(plan.outputData.data());
            const auto archive = sed::assets::GobArchive::fromBytes({ data, data + plan.outputData.size() });
            const auto physicalEntry = [&](std::string_view name)
            {
                return std::any_of(archive.entries().begin(), archive.entries().end(),
                    [&](const auto& entry) { return entry.name == name; });
            };
            check(physicalEntry("misc\\cogstrings.uni") && physicalEntry("mat\\tile.mat")
                    && physicalEntry("jkl\\mixed.jkl"),
                "package: physical GOB names match lowercase engine lookups");
            const auto textOf = [&](std::string_view name)
            {
                const auto bytes = archive.read(name);
                return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            };
            check(textOf("jkl/mixed.jkl") == request.inputBytes
                    && textOf("misc/cogstrings.uni") == "COG_96001 original fixture text"
                    && textOf("mat/tile.mat") == "MATERIAL_BYTES",
                "package: physical canonicalization preserves exact resource and level payloads");
            check(textOf("episode.jk").find("MiXeD.JKL") != std::string::npos,
                "package: episode logical map spelling remains authored");
        }

        const auto raw = sed::assets::buildGobBytes({ { "misc/cogStrings.uni", "SOURCE_BYTES" } });
        const auto* rawData = reinterpret_cast<const std::byte*>(raw.data());
        const auto preserved = sed::assets::GobArchive::fromBytes({ rawData, rawData + raw.size() });
        check(preserved.entries().front().name == "misc\\cogStrings.uni",
            "raw archive serializer preserves physical spelling for archival callers");
        check(sed::assets::canonicalRuntimeGobEntryName("MiSc/cogStrings.uni") == "misc\\cogstrings.uni",
            "runtime name boundary canonicalizes ASCII case and slash direction");
        for ( const std::string name : { "", "misc/", "misc/./text.uni", "misc/a b.uni",
                  "misc/a:b.uni", "misc/a?.uni", "misc/\x7f.uni", "misc/\xc3\xa9.uni" } )
        {
            bool refused = false;
            try
            {
                (void)sed::assets::canonicalRuntimeGobEntryName(name);
            }
            catch ( const std::invalid_argument& )
            {
                refused = true;
            }
            check(refused, "runtime name boundary refuses nonrepresentable paths");
        }
    }
    {
        sed::level::PackageRequest request;
        request.levelFileName = "level.jkl";
        request.episodeTitle = "Runtime entry admission";
        request.resolveResource = [](std::string_view, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            return sed::level::PackageResolvedResource{ "synthetic resource", "BYTES", false };
        };
        for ( const std::string folder : { "/misc", "../misc", "misc/../x", "misc//x", "misc\\..\\x", "C:/misc" } )
        {
            request.extraResources = { { "text.uni", folder } };
            const auto plan = sed::level::planLevelPackage(request);
            check(!plan.canBuild && plan.outputData.empty()
                    && hasIssue(plan, "InvalidPackageEntryName"),
                "package: invalid runtime destination refuses without output");
        }
        request.extraResources = { { std::string(118, 'a') + ".uni", "misc" } };
        check(sed::level::planLevelPackage(request).canBuild,
            "package: 127-byte physical name leaves the engine terminator byte");
        request.extraResources = { { std::string(119, 'a') + ".uni", "misc" } };
        const auto tooLong = sed::level::planLevelPackage(request);
        check(!tooLong.canBuild && tooLong.outputData.empty()
                && hasIssue(tooLong, "PackageEntryNameTooLong"),
            "package: 128-byte physical name cannot fit a terminated engine lookup");
    }
    {
        sed::level::PackageRequest request;
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nstone.mat\nEND\n";
        request.levelFileName = "level.jkl";
        request.episodeTitle = "Exact resource winner";
        // Aliases must still resolve and detect collisions at a full node budget.
        request.options.maxNodes = 3;
        request.resolveResource = [](const std::string_view name, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            return sed::level::PackageResolvedResource{ "two distinct fixture winners",
                name == "stone.mat" ? "AUTHORED_NAME_WINNER" : "SHADOWED_CANONICAL_WINNER", false };
        };
        const auto plan = sed::level::planLevelPackage(request);
        check(plan.canBuild && plan.outputData.find("AUTHORED_NAME_WINNER") != std::string::npos
                && plan.outputData.find("SHADOWED_CANONICAL_WINNER") == std::string::npos,
            "package resolver preserves authored-name precedence over a distinct canonical-path file");
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nvariants/stone.mat\nEND\n";
        const auto nested = sed::level::planLevelPackage(request);
        check(!nested.canBuild && hasIssue(nested, "UnsupportedResourcePath") && nested.outputData.empty(),
            "unsupported nested resource identity refuses instead of substituting a same-basename file");
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nmat/stone.mat\nEND\n";
        check(sed::level::planLevelPackage(request).canBuild,
            "canonical resource folder prefixes remain supported");
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 2\nstone.mat\nmat/stone.mat\nEND\n";
        const auto conflict = sed::level::planLevelPackage(request);
        check(!conflict.canBuild && hasIssue(conflict, "PackagePathCollision") && conflict.outputData.empty(),
            "distinct authored winners cannot silently collapse to one staged resource path");
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 2\nstone.mat\nSTONE.mat\nEND\n";
        const auto caseConflict = sed::level::planLevelPackage(request);
        check(!caseConflict.canBuild && hasIssue(caseConflict, "PackagePathCollision") && caseConflict.outputData.empty(),
            "case-distinct authored winners cannot silently collapse on case-sensitive resource sources");
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 2\nstone.mat\nmat/stone.mat\nEND\n";
        std::size_t reads = 0;
        request.resolveResource = [&](std::string_view, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            ++reads;
            return sed::level::PackageResolvedResource{ "identical alias fixture", "SAME_BYTES", false };
        };
        const auto identical = sed::level::planLevelPackage(request);
        check(identical.canBuild && reads == 2,
            "different authored aliases sharing one output path must prove identical bytes");
        reads = 0;
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 2\nstone.mat\nstone.mat\nEND\n";
        check(sed::level::planLevelPackage(request).canBuild && reads == 1,
            "ordinary repeated references retain one bounded resource capture");
    }
    {
        // Cross-archive precedence: an exact folder/name entry in a later
        // archive outranks a basename-only match in an earlier archive,
        // so a same-named file in the wrong folder cannot shadow it.
        auto request = baseRequest();
        request.archives.clear();
        sed::level::PackageArchiveInput early{};
        early.label = "early.gob";
        early.entries = {
            { "other/wall.mat", "WRONG_BASENAME_ONLY_BYTES" },
        };
        sed::level::PackageArchiveInput late{};
        late.label = "late.gob";
        late.entries = {
            { "3do/crate.3do", "SECTION: MODEL\nMATERIALS\nwall.mat\n" },
            { "mat/wall.mat", "RIGHT_EXACT_PATH_BYTES" },
            { "cog/gate.cog", "COGBYTES" },
            { "misc/cmp/dflt.cmp", "CMPBYTES" },
        };
        request.archives.push_back(std::move(early));
        request.archives.push_back(std::move(late));
        const auto plan = sed::level::planLevelPackage(request);
        check(plan.canBuild
                && plan.outputData.find("RIGHT_EXACT_PATH_BYTES") != std::string::npos
                && plan.outputData.find("WRONG_BASENAME_ONLY_BYTES") == std::string::npos,
            "package: exact archive path outranks earlier basename-only match");
    }
    {
        auto request = baseRequest();
        const auto source = request.archives.front();
        request.archives.clear();
        std::vector<std::string> calls;
        request.resolveResource = [&](const std::string_view name, const std::size_t maxBytes)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            calls.emplace_back(name);
            check(maxBytes > 0 && maxBytes <= request.options.maxPackageBytes,
                "package resolver receives a bounded remaining byte allowance");
            for ( const auto& entry : source.entries )
            {
                if ( entry.first == name || std::filesystem::path(entry.first).filename().string() == name )
                {
                    return sed::level::PackageResolvedResource{ "captured locator", entry.second, false };
                }
            }
            return std::nullopt;
        };
        const auto plan = sed::level::planLevelPackage(request);
        check(plan.canBuild && calls.size() == 4,
            "package resolver supplies each distinct direct/transitive resource winner");
        check(plan.outputData == sed::level::planLevelPackage(baseRequest()).outputData,
            "source-resolved graph emits the same exact resources as equivalent archive inputs");
        request.resolveResource = [](std::string_view, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource> { return std::nullopt; };
        const auto missing = sed::level::planLevelPackage(request);
        check(!missing.canBuild && hasIssue(missing, "MissingResource") && missing.outputData.empty(),
            "missing source-resolved resources never publish a partial package");
        request.options.maxPackageBytes = 64;
        request.resolveResource = [](std::string_view, std::size_t maxBytes)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            return sed::level::PackageResolvedResource{ "oversized winner", std::string(maxBytes + 1, 'x'), false };
        };
        const auto oversized = sed::level::planLevelPackage(request);
        check(!oversized.canBuild && hasIssue(oversized, "PackageResourceTooLarge")
                && oversized.outputData.empty(),
            "planner independently refuses a resolver that exceeds its byte allowance");
    }
    {
        const sed::level::PackagePlan plan =
            sed::level::planLevelPackage(baseRequest());
        check(plan.canBuild, "package: minimal level builds");
        check(!plan.runtimeClosureVerified,
            "package: runtime closure is never claimed");
        check(hasEntry(plan, "episode.jk") && hasEntry(plan, "jkl/level.jkl")
                && hasEntry(plan, "misc/cmp/dflt.cmp")
                && hasEntry(plan, "mat/wall.mat")
                && hasEntry(plan, "3do/crate.3do")
                && hasEntry(plan, "cog/gate.cog"),
            "package: episode, level and resolved resources are entries");
        check(plan.nodes == 6 && plan.edges == 6,
            "package: graph shape counts level, episode and four resources");
        check(!plan.outputSha256.empty() && !plan.manifestSha256.empty()
                && plan.outputBytes == plan.outputData.size(),
            "package: hashes and size are reported");

        const sed::level::PackagePlan again =
            sed::level::planLevelPackage(baseRequest());
        check(again.manifestSha256 == plan.manifestSha256
                && again.outputSha256 == plan.outputSha256
                && again.outputData == plan.outputData,
            "package: identical inputs are deterministic");

        const std::filesystem::path temp =
            std::filesystem::temp_directory_path() / "sed-package-test.gob";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << plan.outputData;
        }
        const sed::assets::GobArchive archive =
            sed::assets::GobArchive::open(temp);
        check(archive.find("episode.jk").has_value()
                && archive.find("jkl/level.jkl").has_value()
                && archive.find("mat/wall.mat").has_value(),
            "package: output reads back through the GOB reader");
        check(archive.entrySha256Hex("mat/wall.mat")
                == sed::assets::jklSourceSha256Hex("MATBYTES"),
            "package: entry bytes survive the round trip");
        std::error_code ec;
        std::filesystem::remove(temp, ec);
    }

    {
        sed::level::PackageRequest request{};
        request.inputBytes =
            "SECTION: SPRITES\n"
            "World Sprites 1\n"
            "0: glow.spr\n"
            "END\n"
            "SECTION: SOUNDCLASS\n"
            "World SoundClass 1\n"
            "0: actor.snd\n"
            "END\n"
            "SECTION: ANIMCLASS\n"
            "World AnimClass 1\n"
            "0: actor.pup\n"
            "END\n";
        request.levelFileName = "level.jkl";
        request.episodeTitle = "Test Episode";
        sed::level::PackageArchiveInput archive{};
        archive.label = "res.gob";
        archive.entries = {
            { "misc/spr/glow.spr", "spark.mat 0 1 1 0 0 0 0 0 0 0\n" },
            { "mat/spark.mat", "SPARKMAT" },
            { "misc/snd/actor.snd", "step step.wav\n" },
            { "sound/step.wav", "WAVBYTES" },
            { "misc/pup/actor.pup", "stand stand.key\n" },
            { "3do/key/stand.key", "KEYBYTES" },
        };
        request.archives.push_back(std::move(archive));
        const sed::level::PackagePlan plan =
            sed::level::planLevelPackage(request);
        check(plan.canBuild,
            "package: sprite and sound-class dependencies build");
        check(hasEntry(plan, "misc/spr/glow.spr")
                && hasEntry(plan, "mat/spark.mat")
                && hasEntry(plan, "misc/snd/actor.snd")
                && hasEntry(plan, "sound/step.wav")
                && hasEntry(plan, "misc/pup/actor.pup")
                && hasEntry(plan, "3do/key/stand.key"),
            "package: sprite, sound-class and puppet dependencies close over");
        check(std::find(plan.unverifiedScopes.begin(),
                  plan.unverifiedScopes.end(), "subresource-dependencies")
                == plan.unverifiedScopes.end(),
            "package: closed subresources leave no unverified scope");

        request.archives[0].entries.erase(
            std::remove_if(request.archives[0].entries.begin(),
                request.archives[0].entries.end(),
                [](const auto& entry) { return entry.first == "sound/step.wav"; }),
            request.archives[0].entries.end());
        const sed::level::PackagePlan blocked =
            sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "MissingResource"),
            "package: a missing sound-class wav blocks the build");
    }

    {
        sed::level::PackageRequest request{};
        request.inputBytes =
            "SECTION: COGS\n"
            "WORLD COGS 1\n"
            "0: gate.cog 0 0\n"
            "END\n";
        request.levelFileName = "level.jkl";
        request.episodeTitle = "Test Episode";
        sed::level::PackageArchiveInput archive{};
        archive.label = "res.gob";
        archive.entries = {
            { "cog/gate.cog",
              "symbols\nsound alarmSnd=alarm.wav\nkeyframe alarmKey=alarm.key\ncog helper=helper.cog\nend\n" },
            { "sound/alarm.wav", "WAVBYTES" },
            { "3do/key/alarm.key", "KEYBYTES" },
            { "cog/helper.cog", "symbols\nend\n" },
        };
        request.archives.push_back(std::move(archive));
        const sed::level::PackagePlan plan =
            sed::level::planLevelPackage(request);
        check(plan.canBuild, "package: cog default dependencies build");
        check(hasEntry(plan, "cog/gate.cog")
                && hasEntry(plan, "sound/alarm.wav")
                && hasEntry(plan, "3do/key/alarm.key")
                && hasEntry(plan, "cog/helper.cog"),
            "package: cog symbol defaults are closed over");
        check(std::find(plan.unverifiedScopes.begin(),
                  plan.unverifiedScopes.end(), "cog-resource-defaults")
                == plan.unverifiedScopes.end(),
            "package: parsed cog defaults leave no unverified scope");
        check(std::find(plan.unverifiedScopes.begin(),
                  plan.unverifiedScopes.end(), "cog-literal-references")
                != plan.unverifiedScopes.end(),
            "package: cog code literal references stay unverified");
        check(!plan.runtimeClosureVerified,
            "package: literal scope keeps runtime closure unverified");

        request.archives[0].entries.erase(
            std::remove_if(request.archives[0].entries.begin(),
                request.archives[0].entries.end(),
                [](const auto& entry) { return entry.first == "sound/alarm.wav"; }),
            request.archives[0].entries.end());
        const sed::level::PackagePlan blocked =
            sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "MissingResource"),
            "package: a missing cog-default wav blocks the build");

        request.archives[0].entries[0].second = "symbols\nsound alarmSnd=alarm.wav\n";
        sed::level::PackageRequest stockRequest{};
        stockRequest.inputBytes =
            "SECTION: SOUNDCLASS\n"
            "World SoundClass 1\n"
            "0: actor.snd\n"
            "END\n";
        stockRequest.levelFileName = "level.jkl";
        stockRequest.episodeTitle = "Test Episode";
        sed::level::PackageArchiveInput mainArchive{};
        mainArchive.label = "res.gob";
        mainArchive.entries = { { "sound/step.wav", "WAVBYTES" } };
        sed::level::PackageArchiveInput stockArchive{};
        stockArchive.label = "stock.gob";
        stockArchive.stock = true;
        stockArchive.entries = { { "misc/snd/actor.snd", "step step.wav\n" } };
        stockRequest.archives.push_back(std::move(mainArchive));
        stockRequest.archives.push_back(std::move(stockArchive));
        const sed::level::PackagePlan stockPlan =
            sed::level::planLevelPackage(stockRequest);
        check(stockPlan.canBuild && stockPlan.stockOmissions.size() == 1
                && hasEntry(stockPlan, "sound/step.wav"),
            "package: stock sound-class wavs are closed over");
        check(std::find(stockPlan.unverifiedScopes.begin(),
                  stockPlan.unverifiedScopes.end(), "stock-dependency-closure")
                == stockPlan.unverifiedScopes.end(),
            "package: traversed stock dependencies leave no unverified scope");
        check(std::find(stockPlan.unverifiedScopes.begin(),
                  stockPlan.unverifiedScopes.end(), "cog-literal-references")
                == stockPlan.unverifiedScopes.end(),
            "package: no cog in closure leaves no literal scope");
        const sed::level::PackagePlan unparsed =
            sed::level::planLevelPackage(request);
        check(std::find(unparsed.unverifiedScopes.begin(),
                  unparsed.unverifiedScopes.end(), "cog-resource-defaults")
                != unparsed.unverifiedScopes.end(),
            "package: an incomplete cog schema stays unverified");
    }

    {
        // Jedi Knight's loader refuses a mesh over 512 vertices, so the model would not draw.
        const auto withCrate = [](const std::string& model)
        {
            sed::level::PackageRequest request = baseRequest();
            for (auto& entry : request.archives[0].entries)
            {
                if (entry.first == "3do/crate.3do") entry.second = model;
            }
            return sed::level::planLevelPackage(request);
        };
        const sed::level::PackagePlan fits = withCrate(fan3do(512));
        check(fits.canBuild && !hasIssue(fits, "ModelOverEngineLimits"),
            "package: a 512-vertex mesh is within Jedi Knight's model limits");
        const sed::level::PackagePlan over = withCrate(fan3do(513));
        check(!over.canBuild && hasIssue(over, "ModelOverEngineLimits"),
            "package: a mesh over 512 vertices blocks the build");
        check(!hasIssue(fits, "ModelOverRetailTestedSize"),
            "package: a 510-face model is within the size retail JK has drawn");
        // Two meshes that each fit, 596 faces in all: built, with a warning.
        const sed::level::PackagePlan large = withCrate(fans3do(300, 2));
        check(large.canBuild && !hasIssue(large, "ModelOverEngineLimits") && hasIssue(large, "ModelOverRetailTestedSize"),
            "package: a model over 512 faces in all is built with a warning to test it in retail");
        sed::assets::Model3doLoadOptions raw{};
        raw.applyHierarchyTransforms = false;
        const std::string big = fan3do(600);
        const auto issues = sed::assets::model3doEngineLimitIssues(
            sed::assets::loadFrom3DO(std::as_bytes(std::span(big.data(), big.size())), raw));
        check(issues.size() == 2 && issues[0].what == "vertices" && issues[0].count == 600
                && issues[0].limit == 512 && issues[1].what == "faces" && issues[1].count == 598,
            "3DO limits: a 600-corner fan is over the vertex and face limits");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.archives[0].entries.erase(
            std::remove_if(request.archives[0].entries.begin(),
                request.archives[0].entries.end(),
                [](const auto& entry) { return entry.first == "mat/wall.mat"; }),
            request.archives[0].entries.end());
        const sed::level::PackagePlan blocked =
            sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "MissingResource")
                && blocked.outputData.empty(),
            "package: a missing resource blocks the build");

        request.options.allowUnverifiedReferences = true;
        const sed::level::PackagePlan allowed =
            sed::level::planLevelPackage(request);
        check(allowed.canBuild && hasIssue(allowed, "MissingResource"),
            "package: unverified references may proceed as warnings");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        sed::level::PackageArchiveInput stock{};
        stock.label = "stock.gob";
        stock.stock = true;
        stock.entries = { { "mat/wall.mat", "STOCKMAT" } };
        request.archives[0].entries.erase(
            std::remove_if(request.archives[0].entries.begin(),
                request.archives[0].entries.end(),
                [](const auto& entry) { return entry.first == "mat/wall.mat"; }),
            request.archives[0].entries.end());
        request.archives.push_back(std::move(stock));
        const sed::level::PackagePlan plan =
            sed::level::planLevelPackage(request);
        check(plan.canBuild && plan.stockOmissions.size() == 1
                && !hasEntry(plan, "mat/wall.mat"),
            "package: stock resources are omitted with a receipt");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.extraResources.push_back(
            sed::level::PackageExtraResource{ "level.jkl", "jkl" });
        check(hasIssue(sed::level::planLevelPackage(request),
                  "PackagePathCollision"),
            "package: generated-path collisions block the build");
    }

    {
        // The generated episode and level consume two nodes; one resolved
        // material requires a third.
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes =
            "SECTION: MATERIALS\n"
            "World Materials 1\n"
            "wall.mat\n"
            "END\n";
        request.archives.front().entries = {
            { "mat/wall.mat", "MATBYTES" },
        };
        request.options.maxNodes = 3;

        const sed::level::PackagePlan exact =
            sed::level::planLevelPackage(request);
        check(exact.canBuild && exact.nodes == 3
                && hasEntry(exact, "mat/wall.mat")
                && !exact.outputData.empty(),
            "package: node budget admits episode, level and one resolved material");

        request.options.maxNodes = 2;
        const sed::level::PackagePlan blocked =
            sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "PackageBudgetExceeded")
                && blocked.outputData.empty() && blocked.outputBytes == 0
                && blocked.outputSha256.empty(),
            "package: node budget refuses a third node without package output");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 0\nEND\n";
        request.archives.clear();
        request.options.maxNodes = 2;

        const auto exact = sed::level::planLevelPackage(request);
        check(exact.canBuild && exact.nodes == 2 && exact.entries.size() == 2
                && !exact.outputData.empty() && !exact.manifestSha256.empty(),
            "package: two mandatory nodes fit without resource nodes");

        for ( const std::size_t limit : { 0u, 1u } )
        {
            request.options.maxNodes = limit;
            const auto blocked = sed::level::planLevelPackage(request);
            check(!blocked.canBuild && hasIssue(blocked, "PackageBudgetExceeded")
                    && blocked.outputData.empty() && blocked.outputBytes == 0
                    && blocked.outputSha256.empty() && !blocked.manifestSha256.empty(),
                limit == 0
                    ? "package: zero-node budget refuses mandatory nodes with a receipt"
                    : "package: one-node budget refuses mandatory nodes with a receipt");
        }
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes =
            "SECTION: MATERIALS\nWorld Materials 2\nwall.mat\nwall.mat\nEND\n";
        request.archives.front().entries = { { "mat/wall.mat", "MATBYTES" } };
        request.options.maxNodes = 3;

        const auto plan = sed::level::planLevelPackage(request);
        check(plan.canBuild && plan.nodes == 3 && plan.entries.size() == 3
                && hasEntry(plan, "mat/wall.mat") && !plan.outputData.empty(),
            "package: duplicate references share the final available resource node");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n";
        request.archives.front().entries = { { "mat/wall.mat", "MATBYTES" } };
        request.archives.front().stock = true;
        request.options.maxNodes = 3;

        const auto exact = sed::level::planLevelPackage(request);
        check(exact.canBuild && exact.nodes == 3 && exact.entries.size() == 2
                && exact.stockOmissions.size() == 1
                && exact.stockOmissions.front().path == "mat/wall.mat"
                && exact.stockOmissions.front().omittedStock
                && !hasEntry(exact, "mat/wall.mat") && !exact.outputData.empty(),
            "package: an omitted stock resource still occupies the final graph node");

        request.options.maxNodes = 2;
        const auto blocked = sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "PackageBudgetExceeded")
                && blocked.outputData.empty() && blocked.outputBytes == 0
                && blocked.outputSha256.empty(),
            "package: stock omission cannot bypass the node budget");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nwall.mat\nEND\n";
        request.archives.clear();
        std::vector<std::string> calls;
        request.resolveResource = [&](const std::string_view name, std::size_t)
            -> std::optional<sed::level::PackageResolvedResource>
        {
            calls.emplace_back(name);
            if ( name != "wall.mat" )
            {
                return std::nullopt;
            }
            return sed::level::PackageResolvedResource{ "captured material", "MATBYTES", false };
        };

        for ( const std::size_t limit : { 0u, 1u, 2u } )
        {
            request.options.maxNodes = limit;
            calls.clear();
            const auto blocked = sed::level::planLevelPackage(request);
            check(!blocked.canBuild && hasIssue(blocked, "PackageBudgetExceeded")
                    && blocked.outputData.empty() && blocked.outputBytes == 0
                    && blocked.outputSha256.empty() && calls.empty(),
                "package: denied resource nodes refuse before resolver capture");
        }

        request.options.maxNodes = 3;
        calls.clear();
        const auto exact = sed::level::planLevelPackage(request);
        check(exact.canBuild && exact.nodes == 3 && hasEntry(exact, "mat/wall.mat")
                && !exact.outputData.empty() && calls == std::vector<std::string>{ "wall.mat" },
            "package: an admitted resource invokes the resolver once with its authored name");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.inputBytes = "SECTION: MATERIALS\nWorld Materials 1\nmissing.mat\nEND\n";
        request.archives.clear();
        request.options.allowUnverifiedReferences = true;
        request.options.maxNodes = 3;

        const auto allowed = sed::level::planLevelPackage(request);
        check(allowed.canBuild && allowed.nodes == 2
                && hasIssue(allowed, "MissingResource") && !allowed.outputData.empty(),
            "package: admitted missing references retain the explicit warning policy");

        request.options.maxNodes = 2;
        const auto blocked = sed::level::planLevelPackage(request);
        check(!blocked.canBuild && hasIssue(blocked, "PackageBudgetExceeded")
                && blocked.outputData.empty() && blocked.outputBytes == 0
                && blocked.outputSha256.empty(),
            "package: unverified-reference policy cannot bypass full node capacity");
    }

    {
        sed::level::PackageRequest request = baseRequest();
        request.options.maxPackageBytes = 16;
        check(hasIssue(sed::level::planLevelPackage(request),
                  "PackageTooLarge"),
            "package: the byte budget blocks oversized output");
    }

    {
        bool refused = false;
        sed::level::PackageRequest request = baseRequest();
        request.levelFileName = "level.txt";
        try
        {
            (void)sed::level::planLevelPackage(request);
        }
        catch (const std::invalid_argument&)
        {
            refused = true;
        }
        check(refused, "package: non-JKL level names are invalid requests");
    }
}

} // namespace sed::tests
