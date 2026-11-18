#include "sed/assets/cog_template_library.hpp"

#include "sed/assets/cog_schema.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace sed::assets
{
namespace
{

const char* const kCredits =
    "Lily (Lilitari) wrote these controllers for Andrei and KnightCop's level in November 2026, building on "
    "the accepted swoop work of Lily, Coda and Andrei; the player swoop's boost mod is Red's. KnightCop's bike "
    "textures, and the LucasArts models and animations they drive, are not part of RED.";

std::vector<CogTemplateSet> buildCatalog()
{
    std::vector<CogTemplateSet> out;
    out.push_back(CogTemplateSet{ "swoop-player", "Player swoop (mountable)", "vehicle", "single player and multiplayer",
        "A Classic swoop the player mounts by using it: normal movement drives, primary fire shoots twin blasters, "
        "alternate fire toggles boost, crouch brakes, lands and dismounts on dry ground. It hovers over water and "
        "rams for damage. Several bikes share one fleet coordinator; each bike has its own ride, aux and ram COGs.",
        { { "swp_fleet.cog", "shared", "No values. One per level, shared by every player swoop and rideable Kell." },
            { "swp_ride.cog", "placed",
                "bike0 (the swp_classic Thing), fleet (the swp_fleet COG), helper (this bike's swp_aux COG), protocolbase "
                "(62000 for the first bike, then 62100, 62200, ...), mount0 (swp_mount.key), seat0 (swp_seat.key), motor0 "
                "(sw61loop.wav), startSound (swp_start.wav), dismountSound (swp_stop.wav), offset0 ((0/0/0))" },
            { "swp_aux.cog", "placed",
                "ride (this bike's swp_ride COG), bike (the Thing), classic (1), nose (0.486088), tail (0.101182), halfwidth "
                "(0.1284785), centerX (0.0661435), handModel (swpovcl.3do, with a lowercase L), povView (1), ramHelper (this "
                "bike's swp_ram COG)" },
            { "swp_ram.cog", "placed", "aux (this bike's swp_aux COG). The aux and ram COGs must name each other." } },
        { "swphands", "swpcam", "+swpgunroot", "swp_classic", "+swpbolt", "+swpshard2", "+swpshard1", "+swpboom" }, kCredits });
    out.push_back(CogTemplateSet{ "swoop-enemy-pursuit", "Enemy swoop pursuit", "enemy", "single player",
        "A Tusken already mounted on a Classic swoop. It wakes when the player comes into sight within wakeDistance, "
        "enters wakeSector within range, or damages it; holds still while its startup animation plays; follows up to "
        "four exit markers in order with the game's own AI steering; then pursues the player, firing and ramming. A "
        "player already in its sector, or close and visible, skips the route. A destroyed bike breaks apart and throws "
        "its rider, who lives on as a bowcaster Tusken with survivalChance.",
        { { "kcswoop.cog", "placed",
            "bike (this kcswoop Thing), wakeDistance (5.5), moveSpeed (1.5), ramDamage (18), fireInterval (0.65), wakeSector "
            "(a sector, or -1), exit0-exit3 (+kcsway marker Things in travel order, -1 after the last), survivalChance (0.5). "
            "One COG per bike; never share one." } },
        { "+kcsway", "kcswoop", "+kcsbody", "+kcstusk", "+kcsfrag1", "+kcsfrag2", "+kcsfrag3", "+kcsfrag4", "+kcsfrag5",
            "+kcsfrag6", "+kcsfrag7", "+kcsfrag8", "+kcsboom" },
        kCredits });
    out.push_back(CogTemplateSet{ "kell-bowcaster-enemy", "Bowcaster Tusken on a Kell", "enemy", "single player",
        "A Grave Tusken riding a Kell, with the game's AI. On death the Kell collapses, the rider falls and stays down, "
        "and the bowcaster drops. Place kc_kbow; its template binds the class COGs, so there is nothing to bind.",
        { { "kc_bow.cog", "class", "Bound by the kc_kbow template's cog=. Don't add a placed COG." },
            { "kckbody.cog", "class", "Bound by the +kckbody template's cog=. Don't place the body helper yourself." } },
        { "+kckbody", "kc_kbow" }, kCredits });
    out.push_back(CogTemplateSet{ "kell-rideable", "Rideable Kell", "vehicle", "single player",
        "A calm Kell the player mounts by using it. Normal movement, run and jump; with fists, primary fire bites and "
        "alternate fire toggles bounding; other weapons work as usual. Crouch on clear, level ground to dismount. It "
        "shares the player swoops' fleet coordinator, so the player can't own a Kell and a swoop at once.",
        { { "swp_fleet.cog", "shared", "No values. The same instance as the player swoops'; add one even with no swoop." },
            { "kc_geom.cog", "placed", "No values." },
            { "kc_mount.cog", "placed", "parked (the +kc_kell Thing), fleet (the swp_fleet COG), geometry (this Kell's kc_geom COG)" } },
        { "+kc_kell", "+kc_ride", "+kc_hit" }, kCredits });
    const char* const demolitionCredits =
        "Lily (Lilitari) wrote these for her Star Wars Demolition port to Jedi Knight (October 2026), adapting the "
        "revision11 swoop controllers of the Seven Swoops work by Lily, Coda and Andrei. The arena hazards "
        "re-create the behaviour of Star Wars Demolition's own hazards in original COG; its models, textures and "
        "sounds are not part of RED.";
    out.push_back(CogTemplateSet{ "demolition-vehicle", "Demolition vehicle (snowspeeder, speeders, walkers)", "vehicle",
        "single player and multiplayer",
        "The vehicle controllers from the Demolition arenas: any rideable vehicle, from the snowspeeder and the "
        "speeders to the walkers, with a shared fleet coordinator, a hull that takes damage and respawns, native "
        "terrain movement, and an optional animation helper for walkers' gaits. Each vehicle has its own ride, "
        "move and aux COGs; the Demolition arenas' JKLs show complete bindings for every vehicle.",
        { { "dm_fleet.cog", "shared", "" }, { "dm_ride.cog", "placed", "" }, { "dm_move.cog", "placed", "" },
            { "dm_aux.cog", "placed", "" }, { "dm_anim.cog", "placed", "Optional, for walkers and creatures with a baked gait." } },
        {}, demolitionCredits });
    out.push_back(CogTemplateSet{ "demolition-arena-hazards", "Demolition arena hazards", "enemy", "single player and multiplayer",
        "The arena hazards from the Demolition port, one script each: Hoth's ion cannon (dmhi_ion) and AT-AT gun and "
        "route (dmatgun, dmatroute), Cloud City's wind fans and boost gates (dmo0fan, dmo0gate), Dagobah's bat hive "
        "and swamp creature (dmo1_hive, dmo1_swamp), the Dune Sea's sarlacc (dmo2_sarlacc), the Death Star II gun "
        "towers (dmo3gun), Mos Eisley's junk engines (dmo5_junk), Naboo's Atlas statues (dmo6atlas, dmo6impact) and "
        "Yavin 4's Sith crushers (dmo7sith). The host owns each hazard in multiplayer. Use them as models for your "
        "own hazards; the arenas show their bindings.",
        { { "dmhi_ion.cog", "placed", "" }, { "dmatgun.cog", "placed", "" }, { "dmatroute.cog", "placed", "" },
            { "dmo0fan.cog", "placed", "" }, { "dmo0gate.cog", "placed", "" }, { "dmo1_hive.cog", "placed", "" },
            { "dmo1_swamp.cog", "placed", "" }, { "dmo2_sarlacc.cog", "placed", "" }, { "dmo3gun.cog", "placed", "" },
            { "dmo5_junk.cog", "placed", "" }, { "dmo6atlas.cog", "placed", "" }, { "dmo6impact.cog", "placed", "" },
            { "dmo7sith.cog", "placed", "" } },
        {}, demolitionCredits });
    return out;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string readText(const std::filesystem::path& path);

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

std::string cogTemplateBindings(const std::filesystem::path& library, const CogTemplateScript& script)
{
    std::string listed;
    const CogSchema schema = parseCogSchema(script.file, readText(library / script.file));
    for (const CogSymbol& symbol : schema.symbols)
    {
        if (!symbol.takesLevelValue()) continue;
        if (!listed.empty()) listed += ", ";
        listed += symbol.name;
        if (symbol.defaultText && !symbol.defaultText->empty()) listed += " (" + *symbol.defaultText + ")";
    }
    const std::string order = listed.empty() ? "No values." : "Values in order: " + listed + ".";
    return script.bindings.empty() ? order : script.bindings + (script.role == "class" || listed.empty() ? "" : " " + order);
}

const std::vector<CogTemplateSet>& cogTemplateCatalog()
{
    static const std::vector<CogTemplateSet> catalog = buildCatalog();
    return catalog;
}

const CogTemplateSet* findCogTemplateSet(const std::string_view id)
{
    for (const CogTemplateSet& set : cogTemplateCatalog())
    {
        if (lower(set.id) == lower(std::string(id))) return &set;
    }
    return nullptr;
}

std::optional<std::filesystem::path> findCogTemplateLibrary(const std::vector<std::filesystem::path>& candidates)
{
    for (const std::filesystem::path& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate / kCogTemplateRows, ec)) return candidate;
    }
    return std::nullopt;
}

std::vector<std::string> cogTemplateRows(const std::filesystem::path& library, const CogTemplateSet& set)
{
    std::istringstream in(readText(library / kCogTemplateRows));
    std::vector<std::pair<std::string, std::string>> rows;
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        const auto end = line.find_first_of(" \t", start);
        rows.emplace_back(lower(line.substr(start, end - start)), line);
    }
    std::vector<std::string> out;
    for (const std::string& name : set.templates)
    {
        for (const auto& [key, row] : rows)
        {
            if (key == lower(name))
            {
                out.push_back(row);
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> cogTemplateUserFiles(const std::filesystem::path& library, const CogTemplateSet& set)
{
    std::set<std::string> files;
    std::set<std::string> shipped;
    for (const CogTemplateScript& script : set.scripts) shipped.insert(lower(script.file));
    for (const std::string& row : cogTemplateRows(library, set))
    {
        std::istringstream words(row);
        for (std::string word; words >> word;)
        {
            const auto eq = word.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower(word.substr(0, eq));
            const std::string value = lower(word.substr(eq + 1));
            if (value.empty() || value == "none") continue;
            if (key == "model3d" || key == "puppet" || key == "soundclass" || key == "sprite" || key == "aiclass" || key == "cog"
                || key == "material")
                files.insert(value);
        }
    }
    for (const CogTemplateScript& script : set.scripts)
    {
        const CogSchema schema = parseCogSchema(script.file, readText(library / script.file));
        for (const CogSymbol& symbol : schema.symbols)
        {
            const bool resource = symbol.kind == CogSchemaSymbolKind::sound || symbol.kind == CogSchemaSymbolKind::keyframe
                || symbol.kind == CogSchemaSymbolKind::model || symbol.kind == CogSchemaSymbolKind::material
                || symbol.kind == CogSchemaSymbolKind::ai;
            if (resource && symbol.defaultText && !symbol.defaultText->empty() && *symbol.defaultText != "-1")
                files.insert(lower(*symbol.defaultText));
        }
    }
    std::vector<std::string> out;
    for (const std::string& file : files)
    {
        if (!shipped.count(file)) out.push_back(file);
    }
    return out;
}

std::string cogTemplateNotes(const std::filesystem::path& library, const CogTemplateSet& set)
{
    std::ostringstream out;
    out << "# " << set.title << "\n\n" << set.summary << "\n\nFor " << set.modes << ".\n\n## Scripts\n\n";
    for (const CogTemplateScript& script : set.scripts)
    {
        const char* role = script.role == "shared" ? "one placed COG for the whole level"
            : script.role == "placed"              ? "one placed COG for each"
                                                   : "a template class COG";
        out << "- `" << script.file << "`: " << role << ". " << cogTemplateBindings(library, script) << "\n";
    }
    out << "\n## Templates\n\nMerge these rows of `" << kCogTemplateRows << "` into the project's master.tpl, in this order:\n\n";
    for (const std::string& name : set.templates) out << "- `" << name << "`\n";
    out << "\n## Files you supply\n\nRED doesn't ship models, animations or sounds. The templates and scripts name these; "
           "stock files come from the game, and the rest from the original kit or your own work:\n\n";
    for (const std::string& file : cogTemplateUserFiles(library, set)) out << "- `" << file << "`\n";
    out << "\nRaise the level's COG capacity (World cogs) for every placed COG and every template cog= field, and start "
           "a fresh game after changing templates or bindings.\n\n## Credits\n\n" << set.credits << "\n";
    return out.str();
}

CogTemplateCopy copyCogTemplateSet(const std::filesystem::path& library, const CogTemplateSet& set,
    const std::filesystem::path& projectDirectory, const bool dryRun)
{
    CogTemplateCopy result;
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    for (const CogTemplateScript& script : set.scripts)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(library / script.file, ec))
        {
            result.error = "The library has no " + script.file + ".";
            return result;
        }
        files.emplace_back(projectDirectory / "cog" / script.file, readText(library / script.file));
    }
    std::string rows = "# " + set.title + ": rows from RED's COG template library. Merge into master.tpl in this order.\n";
    for (const std::string& row : cogTemplateRows(library, set)) rows += row + "\n";
    files.emplace_back(projectDirectory / "templates" / (set.id + ".tpl"), rows);
    files.emplace_back(projectDirectory / "templates" / (set.id + ".md"), cogTemplateNotes(library, set));
    for (const auto& [path, bytes] : files)
    {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            (readText(path) == bytes ? result.unchanged : result.conflicts).push_back(path);
        else
            result.written.push_back(path);
    }
    if (dryRun || !result.conflicts.empty()) return result;
    for (const auto& [path, bytes] : files)
    {
        if (std::find(result.written.begin(), result.written.end(), path) == result.written.end()) continue;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        out << bytes;
        if (!out)
        {
            result.error = "Couldn't write " + path.string() + ".";
            return result;
        }
    }
    return result;
}

} // namespace sed::assets
