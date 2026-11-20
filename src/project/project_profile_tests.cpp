#include "project_profile.hpp"
#include "project_fixture.hpp"
#include "resource_family_fixture.hpp"
#include "resource_families.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
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
        require(catalog.sources.authoringFiles == std::vector<fs::path>{fs::canonical(familyRoot / "jkl/relay.jkl")},
            "insertion targets the declared campaign document even when other files share its name");
        const auto family = project::resourceFamily(catalog, actor);
        for (const char* expected : {"relay_sentry.3do", "relay_skin.mat", "relay_motion.pup", "relay_point.key",
                "relay_logic.cog", "relay_audio.snd", "relay_ping.wav"})
            require(family.contains(expected), "selection expands through the complete original enemy family");
        require(catalog.rows.at("relay_skin.mat").shadows.size() == 1
                && project::affectedResources(catalog, "relay_skin.mat").contains(actor),
            "replacement identifies the actor that uses this material through its model and names the shadowed winner");
        Level authored{};
        assets::ResourceLocator absent;
        const auto insertion = project::prepareFamily(catalog, actor, authored, absent, "relay_sentry");
        require(insertion.provisions.size() == 7 && insertion.templates.size() == 1,
            "the reviewed actor provisions every original asset and carries the selected template");
        require(project::bindFamily(authored, insertion, 0, {}) == false && authored.things.empty()
                && authored.templates.empty(),
            "no geometry is mutated when the insertion sector does not exist");
        authored.sectors.push_back(level::makeSectorPrimitiveShape(ProjectType::JKDF2, level::SectorPrimitive::Cube));
        const auto collision = *catalog.rows.at(actor).authoringTemplate;
        authored.templates.push_back(collision);
        authored.templates.front().values.clear();
        bool nameRefused = false;
        try { (void)project::prepareFamily(catalog, actor, authored, absent, "relay_sentry"); }
        catch (const std::exception&) { nameRefused = true; }
        require(nameRefused, "same-name templates with different values require an explicit decision");
        const auto renamed = project::prepareFamily(catalog, actor, authored, absent, "relay_sentry_trial");
        require(renamed.name == "relay_sentry_trial" && renamed.templates.at(0).name == renamed.name,
            "a requested new template name remaps the actor binding without changing the existing definition");
        require(project::bindFamily(authored, renamed, 0, level::sectorCalcCenter(authored.sectors.front()))
                && authored.templates.size() == 2 && authored.things.back().templateName == "relay_sentry_trial",
            "the selected authored template is bound to an actual Thing inside the selected sector");
        const auto reuse = project::prepareFamily(catalog, actor, Level{}, catalog.previewResources, "relay_sentry");
        require(reuse.provisions.empty(), "identical live resource revisions are reused");
        assets::ResourceLocator conflict;
        write(familyRoot / "conflict.gob", assets::buildGobBytes({{"mat/relay_skin.mat","different bytes"}}));
        conflict.mountGob(familyRoot / "conflict.gob");
        bool wrongLive = false;
        try { (void)project::prepareFamily(catalog, actor, Level{}, conflict, "relay_sentry"); }
        catch (const std::exception&) { wrongLive = true; }
        require(wrongLive, "an unrelated live resource with the same name is preserved");
        const auto inspection = project::inspectFamilyModel(catalog, "relay_sentry.3do");
        require(inspection.find("LOD 0") != std::string::npos && inspection.find("LOD 1") != std::string::npos
                && inspection.find("boom") != std::string::npos && inspection.find("relay_skin.mat") != std::string::npos
                && inspection.find("512 vertices") != std::string::npos,
            "JK model inspection walks both authored LODs, their material, hierarchy and documented loader limits");
        auto otherTarget = catalog;
        otherTarget.sources.target.game = "mots";
        require(project::inspectFamilyModel(otherTarget, "relay_sentry.3do").find("512 vertices") == std::string::npos,
            "a different target does not inherit the JK loader's limits");
        auto inheritedSources = catalog.sources;
        inheritedSources.levels.emplace_back("jkl/inherited.jkl",
            "SECTION: TEMPLATES\nWORLD TEMPLATES 2\n_relay_base none model3d=relay_sentry.3do\n"
            "relay_guard _relay_base puppet=relay_motion.pup\nEND\n");
        const auto inherited = project::indexProjectAssets(std::move(inheritedSources));
        const auto inheritance = project::prepareFamily(inherited, "template:level:jkl/inherited.jkl#relay_guard",
            Level{}, inherited.previewResources, "relay_guard");
        require(inheritance.templates.size() == 2 && inheritance.templates[0].name == "_relay_base"
                && inheritance.templates[1].parent == inheritance.templates[0].name
                && project::resourceFamily(inherited, "template:level:jkl/inherited.jkl#relay_guard").contains("relay_skin.mat"),
            "an inherited model is fully traced and the parent is bound before the child");
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
