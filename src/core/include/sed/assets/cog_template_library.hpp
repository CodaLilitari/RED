#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

/**
 * The COG template library: reusable controller scripts for vehicles and
 * enemies, with the template rows they need, shipped in RED's
 * `data/cog_templates`. The scripts are Lily's (Lilitari) original work for
 * KnightCop's level; RED ships no models, animations or sounds. Each set
 * lists the files its templates and scripts name that the user supplies.
 */

struct CogTemplateScript
{
    std::string file{};
    /** "shared" (one per level), "placed" (one per vehicle or enemy) or "class" (bound by a template's cog=). */
    std::string role{};
    /** The exposed values in placed-COG order, with what each one is. */
    std::string bindings{};
};

struct CogTemplateSet
{
    std::string id{};
    std::string title{};
    /** "vehicle", "enemy" or "item". */
    std::string kind{};
    /** "single player", or "single player and multiplayer". */
    std::string modes{};
    std::string summary{};
    std::vector<CogTemplateScript> scripts{};
    /** Rows of knightcop_vehicles.tpl the set needs, in order. */
    std::vector<std::string> templates{};
    std::string credits{};
};

const std::vector<CogTemplateSet>& cogTemplateCatalog();
const CogTemplateSet* findCogTemplateSet(std::string_view id);

inline constexpr std::string_view kCogTemplateRows = "knightcop_vehicles.tpl";

/** The first candidate directory that holds the library (its knightcop_vehicles.tpl). */
std::optional<std::filesystem::path> findCogTemplateLibrary(const std::vector<std::filesystem::path>& candidates);

/** The set's template rows, read from the library, in the set's order; missing names are left out. */
std::vector<std::string> cogTemplateRows(const std::filesystem::path& library, const CogTemplateSet& set);

/**
 * Files the set's templates and scripts name that RED doesn't ship: models,
 * puppets, keys, sounds, sprites, AI classes and scripts from elsewhere
 * (such as kcs_tusk.cog). Lowercase, sorted, without duplicates.
 */
std::vector<std::string> cogTemplateUserFiles(const std::filesystem::path& library, const CogTemplateSet& set);

struct CogTemplateCopy
{
    /** Files written, or that would be written. */
    std::vector<std::filesystem::path> written{};
    /** Files already in the project with the same bytes. */
    std::vector<std::filesystem::path> unchanged{};
    /** Files already in the project with different bytes: nothing is written while there are any. */
    std::vector<std::filesystem::path> conflicts{};
    std::string error{};
};

/**
 * Copies a set into a project: the scripts into `cog/`, and the set's
 * template rows and notes into `templates/<id>.tpl` and `templates/<id>.md`
 * for merging into the project's master.tpl. Never overwrites a different
 * file; with any conflict, nothing is written.
 */
CogTemplateCopy copyCogTemplateSet(const std::filesystem::path& library, const CogTemplateSet& set,
    const std::filesystem::path& projectDirectory, bool dryRun = false);

/** A script's placed values in order (from its symbols), after any hand-written notes. */
std::string cogTemplateBindings(const std::filesystem::path& library, const CogTemplateScript& script);

/** Templates the set's scripts name in their symbols, in script order, without duplicates. */
std::vector<std::string> cogTemplateNamedTemplates(const std::filesystem::path& library, const CogTemplateSet& set);

/** The set's notes as Markdown: what it is, what to place, the bindings, the files to supply, credits. */
std::string cogTemplateNotes(const std::filesystem::path& library, const CogTemplateSet& set);

} // namespace sed::assets
