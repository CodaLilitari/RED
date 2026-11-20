#include "test_common.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_template_library.hpp"
#include "../../src/assets/atomic_file_write.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sed::tests
{
namespace
{

using namespace sed::assets;

std::filesystem::path library()
{
    const auto found = findCogTemplateLibrary({ std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / ".." / "data" / "cog_templates",
        std::filesystem::current_path() / "data" / "cog_templates" });
    return found.value_or(std::filesystem::path{});
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool has(const std::vector<std::string>& list, const std::string& item)
{
    return std::find(list.begin(), list.end(), item) != list.end();
}

void testCatalog()
{
    const std::filesystem::path lib = library();
    check(!lib.empty(), "COG templates: the library is found in data/cog_templates");
    if (lib.empty()) return;
    check(cogTemplateCatalog().size() == 10 && findCogTemplateSet("Kell-Rideable") != nullptr,
        "COG templates: ten sets, found by id in any case");
    for (const CogTemplateSet& set : cogTemplateCatalog())
    {
        check(cogTemplateRows(lib, set).size() == set.templates.size(), ("COG templates: every template row of " + set.id + " is in the library").c_str());
        for (const CogTemplateScript& script : set.scripts)
        {
            const auto compiled = compileCog(script.file, read(lib / script.file), CogPreflightTarget::jk);
            check(compiled.loads && compiled.count(CogDiagnosticSeverity::error) == 0 && compiled.count(CogDiagnosticSeverity::warning) == 0,
                ("COG templates: " + script.file + " compiles cleanly for JK").c_str());
        }
    }
    const auto files = cogTemplateUserFiles(lib, *findCogTemplateSet("swoop-enemy-pursuit"));
    check(has(files, "kcs_tusk.cog") && has(files, "ts.3do") && !has(files, "kcswoop.cog"),
        "COG templates: the enemy swoop asks for its rider script and model, not the script RED ships");
    const auto player = cogTemplateUserFiles(lib, *findCogTemplateSet("swoop-player"));
    check(has(player, "swpclanim.3do"), "COG templates: the player swoop asks for its bike model");
    const auto fighter = cogTemplateNamedTemplates(lib, *findCogTemplateSet("sote-space-fighter"));
    check(has(fighter, "+swo_pilotbolt") && has(cogTemplateUserFiles(lib, *findCogTemplateSet("sote-jetpack")), "dr1jp.3do"),
        "COG templates: a set without template rows names the templates and models its scripts use");
    const std::string enemies = cogTemplateNotes(lib, *findCogTemplateSet("sote-enemies"));
    check(enemies.find("define them in the project's master.tpl") != std::string::npos
            && enemies.find("`+soteatst_exp`") != std::string::npos,
        "COG templates: the notes list the named templates when the library has no rows for the set");
    const std::string demolition = cogTemplateNotes(lib, *findCogTemplateSet("demolition-vehicle"));
    check(demolition.find("Values in order: ride, helper, bike") != std::string::npos,
        "COG templates: a script without notes lists its placed values in order, from its symbols");
    const std::string notes = cogTemplateNotes(lib, *findCogTemplateSet("kell-rideable"));
    check(notes.find("kc_mount.cog") != std::string::npos && notes.find("Lily") != std::string::npos,
        "COG templates: the notes list the scripts and credit Lily");
}

void testCopy()
{
    const std::filesystem::path lib = library();
    if (lib.empty()) return;
    const std::filesystem::path project = std::filesystem::temp_directory_path() / "red-cog-template-copy";
    std::error_code ec;
    std::filesystem::remove_all(project, ec);
    const CogTemplateSet& set = *findCogTemplateSet("kell-rideable");
    const auto dry = copyCogTemplateSet(lib, set, project, true);
    check(dry.written.size() == 5 && !std::filesystem::exists(project / "cog"), "COG templates: a dry run writes nothing");
    const auto changed = copyCogTemplateSet(lib, set, project, false, ReviewedCogScript{ "kc_mount.cog", "# earlier review" });
    check(!changed.error.empty() && changed.written.empty() && !std::filesystem::exists(project / "cog"),
        "COG templates: an assistant's reviewed controller must match the supplied library bytes before any write");
    using Fault = detail::StagedAssetFileFaultPointForTests;
    for ( const auto point : { Fault::write, Fault::flush, Fault::close, Fault::publish } )
    {
        detail::ScopedStagedAssetFileFaultForTests fault(point);
        const auto refused = copyCogTemplateSet(lib, set, project);
        check(fault.triggered() && !refused.error.empty() && refused.written.empty()
                && !std::filesystem::exists(project / "cog" / "swp_fleet.cog"),
            "COG templates: write, flush, close or first publication failure exposes no script or false success");
    }
    {
        const auto firstPath = project / "interrupted" / "cog" / "swp_fleet.cog";
        detail::ScopedStagedAssetFileFaultForTests secondPublish(Fault::publish, 1);
        const auto partial = copyCogTemplateSet(lib, set, project / "interrupted");
        check(secondPublish.triggered() && !partial.error.empty() && partial.written == std::vector{ firstPath }
                && read(firstPath) == read(lib / "swp_fleet.cog")
                && !std::filesystem::exists(firstPath.parent_path() / "kc_geom.cog"),
            "COG templates: interruption between artifacts reports the complete retained file");
        const auto retry = copyCogTemplateSet(lib, set, project / "interrupted");
        check(retry.error.empty() && retry.written.size() == 4 && retry.unchanged == std::vector{ firstPath },
            "COG templates: retry reuses an untampered partial publication");
    }
    const auto first = copyCogTemplateSet(lib, set, project);
    check(first.error.empty() && first.written.size() == 5 && std::filesystem::exists(project / "cog" / "kc_mount.cog")
            && read(project / "templates" / "kell-rideable.tpl").find("+kc_kell") != std::string::npos,
        "COG templates: copying writes the scripts, the template rows and the notes");
    const auto again = copyCogTemplateSet(lib, set, project);
    check(again.written.empty() && again.unchanged.size() == 5 && again.conflicts.empty(), "COG templates: copying again changes nothing");
    { std::ofstream(project / "cog" / "kc_geom.cog", std::ios::binary) << "# mine\n"; }
    std::filesystem::remove(project / "cog" / "kc_mount.cog", ec);
    const auto conflict = copyCogTemplateSet(lib, set, project);
    check(conflict.conflicts.size() == 1 && !std::filesystem::exists(project / "cog" / "kc_mount.cog")
            && read(project / "cog" / "kc_geom.cog") == "# mine\n",
        "COG templates: a different file in the way stops the copy, and nothing is overwritten or written");
    const auto generated = project / "cog" / "red_goals.cog";
    const auto created = publishAssistantScript(generated, "reviewed script\n");
    const auto reused = publishAssistantScript(generated, "reviewed script\n");
    const auto userConflict = publishAssistantScript(generated, "another version\n");
    check(created.written == std::vector{ generated } && reused.unchanged == std::vector{ generated }
            && userConflict.conflicts == std::vector{ generated } && read(generated) == "reviewed script\n",
        "assistant scripts: create-only, identical reuse and differing user bytes are distinct outcomes");
    const auto blocked = publishAssistantScript(generated / "nested.cog", "reviewed script\n");
    check(!blocked.error.empty() && blocked.written.empty() && read(generated) == "reviewed script\n",
        "assistant scripts: a non-directory parent refuses the write without changing its file");
    std::filesystem::remove_all(project, ec);
}

} // namespace

void runCogTemplateLibraryTests()
{
    testCatalog();
    testCopy();
}

} // namespace sed::tests
