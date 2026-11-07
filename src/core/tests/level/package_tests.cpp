#include "test_common.hpp"

#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"

#include <algorithm>
#include "sed/level/package.hpp"

#include <filesystem>
#include <fstream>

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
        const sed::level::PackagePlan unparsed =
            sed::level::planLevelPackage(request);
        check(std::find(unparsed.unverifiedScopes.begin(),
                  unparsed.unverifiedScopes.end(), "cog-resource-defaults")
                != unparsed.unverifiedScopes.end(),
            "package: an incomplete cog schema stays unverified");
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
