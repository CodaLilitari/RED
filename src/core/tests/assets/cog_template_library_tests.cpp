#include "test_common.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_template_library.hpp"

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
    check(cogTemplateCatalog().size() == 4 && findCogTemplateSet("Kell-Rideable") != nullptr,
        "COG templates: four sets, found by id in any case");
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
    std::filesystem::remove_all(project, ec);
}

} // namespace

void runCogTemplateLibraryTests()
{
    testCatalog();
    testCopy();
}

} // namespace sed::tests
