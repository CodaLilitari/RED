#include "project_profile.hpp"
#include "project_fixture.hpp"
#include "resource_family_fixture.hpp"
#include "resource_families.hpp"
#include <cstring>

int main()
{
    using namespace sed;
    const fs::path sandbox = fs::temp_directory_path() /
        ("red-portable-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        const auto familyRoot = sandbox / "family";
        const auto familyStock = sandbox / "family-stock";
        createRelayProject(familyRoot, familyStock, true);
        const auto familyProfile = familyRoot / "red-project.json";
        const auto catalog = project::indexProjectAssets(project::captureAssetSources(familyProfile, familyStock));
        const std::string actor = "template:relay_sentry.tpl#relay_sentry";
        const auto family = project::resourceFamily(catalog, actor);
        for (const char* expected : {"relay_sentry.3do", "relay_skin.mat", "relay_motion.pup", "relay_point.key",
                "relay_logic.cog", "relay_audio.snd", "relay_ping.wav"})
            require(family.contains(expected), "selection expands through the complete original enemy family");
        require(catalog.rows.at("relay_skin.mat").shadows.size() == 1
                && project::affectedResources(catalog, "relay_skin.mat").contains(actor),
            "replacement identifies the actor that uses this material through its model and names the shadowed winner");
        const auto clean = familyRoot / "clean/relay.gob";
        fs::create_directories(clean.parent_path());
        const auto reviewed = project::prepare(familyProfile, familyStock);
        project::publish(familyProfile, familyStock, reviewed.revision, clean);
        const auto packaged = assets::GobArchive::open(clean);
        for (const char* name : {"3do/relay_sentry.3do","mat/relay_skin.mat","misc/pup/relay_motion.pup",
                "3do/key/relay_point.key","cog/relay_logic.cog","misc/snd/relay_audio.snd","sound/relay_ping.wav"})
        {
            const auto bytes = packaged.read(name);
            const auto original = familyFixtureBytes(fs::path(name).filename().string());
            require(bytes.size() == original.size() && std::memcmp(bytes.data(), original.data(), bytes.size()) == 0,
                "a clean package contains each intended original resource byte for byte");
        }
        createRelayProject(familyRoot, familyStock, false);
        const auto missing = project::indexProjectAssets(project::captureAssetSources(familyProfile, familyStock));
        require(missing.rows.at("relay_ping.wav").missing && project::resourceFamily(missing, actor).contains("relay_ping.wav"),
            "missing leaf stays visible on the actor before insertion");
        const auto workspace = sandbox / "first";
        const auto stock = sandbox / "installed-game";
        const auto custom = createCampaignFixture(workspace, stock);
        const auto first = project::prepare(workspace / "red-project.json", stock);
        require(first.levels.size() == 2 && first.target.game == "jk", "campaign and explicit game profile");
        require(first.resources.size() == 1 && first.resources[0].winner == "project:art.gob"
            && first.resources[0].shadowed == std::vector<std::string>{"stock:stock.gob"}, "ordered winners");
        require(first.scenarios.empty(), "an untested target has no observations");
        const auto moved = sandbox / "second";
        fs::copy(workspace, moved, fs::copy_options::recursive);
        const auto second = project::prepare(moved / "red-project.json", stock);
        require(first.revision == second.revision && first.package == second.package, "portable identical build");
        const auto out = moved / "test.gob";
        project::publish(moved / "red-project.json", stock, first.revision, out);
        auto gob = assets::GobArchive::open(out);
        require(gob.entries().size() == 4 && gob.contains("episode.jk") && gob.contains("jkl/one.jkl")
            && gob.contains("jkl/two.jkl") && gob.contains("mat/wall.mat"), "two levels, one shared asset");
        const auto mat = gob.read("mat/wall.mat");
        require(std::string(reinterpret_cast<const char*>(mat.data()), mat.size()) == "custom", "winning bytes");
        write(moved / "art.gob", assets::buildGobBytes({{"mat/wall.mat", "changed"}}));
        bool refused = false;
        try { project::publish(moved / "red-project.json", stock, first.revision, moved / "stale.gob"); }
        catch ( const std::exception& ) { refused = true; }
        require(refused && !fs::exists(moved / "stale.gob"), "changed winning archive invalidates prepare");
        write(moved / "art.gob", custom);
        fs::remove(moved / "jkl/two.jkl");
        refused = false;
        try { (void)project::prepare(moved / "red-project.json", stock); }
        catch ( const std::exception& ) { refused = true; }
        require(refused, "missing campaign level refused");
        fs::copy_file(workspace / "jkl/two.jkl", moved / "jkl/two.jkl");
        auto broken = assets::saveToEpisodeJkText(assets::loadFromEpisodeJk(moved / "episode.jk"));
        const auto branch = broken.find("\t20\t-1");
        require(branch != std::string::npos, "fixture has a valid first branch");
        broken.replace(branch, 6, "\t99\t-1");
        write(moved / "episode.jk", broken);
        refused = false;
        try { (void)project::prepare(moved / "red-project.json", stock); }
        catch ( const std::exception& ) { refused = true; }
        require(refused, "broken campaign progression refused");
        fs::remove_all(sandbox);
        std::cout << "Portable project profile tests passed\n";
        return 0;
    }
    catch ( const std::exception& err )
    {
        std::cerr << err.what() << '\n';
        return 1;
    }
}
