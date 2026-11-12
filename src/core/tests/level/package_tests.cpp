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
