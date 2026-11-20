#include "project_profile.hpp"
#include "project_fixture.hpp"

int main()
{
    using namespace sed;
    const fs::path sandbox = fs::temp_directory_path() /
        ("red-portable-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        const auto workspace = sandbox / "first";
        const auto stock = sandbox / "installed-game";
        const auto custom = createCampaignFixture(workspace, stock);
        const auto first = project::prepare(workspace / "red-project.json", stock);
        require(first.levels.size() == 2 && first.target.game == "jk", "campaign and explicit game profile");
        require(first.resources.size() == 1 && first.resources[0].winner == "project:art.gob"
            && first.resources[0].shadowed == std::vector<std::string>{"stock:stock.gob"}, "ordered winners");
        require(first.scenarios.empty(), "an untested target has no observations");
        const auto editedText = std::string("# Unpublished live surface change\nSECTION: MATERIALS\n")
            + "World Materials 1\nwall.mat\nEND\n";
        project::LiveExport live{workspace / "jkl/one.jkl", editedText};
        const auto edited = project::prepare(workspace / "red-project.json", stock, &live);
        require(edited.revision != first.revision && edited.package != first.package && edited.liveLevel == "one.jkl",
            "a live export replaces its campaign level and identifies the edited revision");
        require(project::prepare(workspace / "red-project.json", stock).package == first.package,
            "testing live source does not change the project's exports");
        live.source = sandbox / "unrelated.jkl";
        bool unrelatedRefused = false;
        try { (void)project::prepare(workspace / "red-project.json", stock, &live); }
        catch ( const std::exception& ) { unrelatedRefused = true; }
        require(unrelatedRefused, "an unrelated open level cannot qualify a campaign scenario");
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
