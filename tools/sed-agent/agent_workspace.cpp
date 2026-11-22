#include "agent_model_native.hpp"
#include "agent_workspace.hpp"
#include "atomic_output.hpp"
#include "agent_basic_operations.hpp"
#include "agent_parity_operations.hpp"
#include "agent_entity_operations.hpp"
#include "agent_query_details.hpp"
#include "agent_model_lighting.hpp"
#include "agent_jo_ja.hpp"
#include "agent_schema.hpp"
#include "agent_edit_operations.hpp"
#include "agent_arch_operations.hpp"
#include "agent_semantic_preview.hpp"
#include "agent_dependencies.hpp"
#include "bounded_input.hpp"
#include "source_read_set.hpp"
#include "utf8_path.hpp"
#include "bake_resource_snapshot.hpp"
#include "archive_admission.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/level/door.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/runtime_archive.hpp"
#include "sed/assets/asc.hpp"
#include "sed/assets/jed.hpp"
#include "sed/assets/lev.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/jkl_structure.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/jpeg.hpp"
#include "sed/assets/obj_import.hpp"
#include "sed/assets/png.hpp"
#include "sed/level/overview.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/text.hpp"
#include "sed/render/software_snapshot.hpp"
#include "sed/render/snapshot_things.hpp"

#include <bit>
#include <cstring>  // RED: std::memcpy; MSVC pulled it in transitively
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <limits>
#include <memory>
#include <span>
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/gameplay_assistants.hpp"
#include "sed/level/dynamic_vertex_colors.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/sealed_seams.hpp"
#include "sed/level/thing.hpp"
#include "sed/level/thing_frames.hpp"
#include "sed/level/workbench.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/arch_validation.hpp"
#include "sed/level/package.hpp"
#include "sed/level/fan_degeneracies.hpp"
#include "sed/level/transform.hpp"
#include "sed/level/game_launch.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/emissive_resolution.hpp"
#include "sed/level/lighting.hpp"
#include "sed/level/mirror.hpp"
#include "sed/level/light_limits.hpp"
#include "sed/level/ray_lighting_resources.hpp"
#include "sed/level/project_defaults.hpp"
#include "sed/level/prefab.hpp"
#include "sed/level/statistics.hpp"
#include "sed/assets/dxf.hpp"
#include "sed/level/dxf_import.hpp"
#include "sed/assets/key.hpp"
#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_runtime.hpp"
#include "sed/assets/model_hierarchy.hpp"
#include "sed/assets/cog_template_library.hpp"
#include "sed/assets/cog_preflight.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/model_lighting.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sed_agent
{
namespace fs = std::filesystem;

namespace
{

[[noreturn]] void fail(int code, std::string message) { throw AgentError{ code, std::move(message) }; }

[[noreturn]] void failCoded(int code, std::string errorCode, std::string message)
{
    throw AgentError{ code, std::move(message), std::move(errorCode) };
}

thread_local detail::SourceReadSet* activeSourceReads = nullptr;
thread_local std::vector<std::function<void()>>* activeResourceChecks = nullptr;
thread_local const CommandOptions* activeCommandOptions = nullptr;
constexpr std::size_t kMaxArchiveBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxExpandedArchiveBytes = 512u * 1024u * 1024u;

struct LoadedTemplateSource;

struct CommandResourceBudget
{
    std::size_t archiveBytes{ kMaxArchiveBytes };
    std::size_t archiveEntries{ 100000 };
    std::shared_ptr<detail::BakeResourceBudget> bake{ std::make_shared<detail::BakeResourceBudget>() };
    // Hash-bound resourceArchives captured by this command, keyed by path
    // and sha256: every later use shares the capture and is not charged to
    // the budget again (each op, the placement resolver and the export
    // scan name the same archives).
    std::map<std::string, sed::assets::GobArchive> capturedArchives{};
    // Retain every payload validated by a mutation/append guard. Different
    // operation-local archives must not overwrite an earlier schema choice.
    std::set<std::pair<std::string, std::string>> cogSchemaChoices{};
    // Template sources (path and sha256 -> parsed database), read and
    // charged once per command however many operations name them.
    std::map<std::string, std::shared_ptr<const LoadedTemplateSource>> templateSources{};
};
thread_local CommandResourceBudget* activeResourceBudget = nullptr;

std::string readArchiveBytes(const fs::path& path)
{
    return readFileBytes(path, activeResourceBudget ? activeResourceBudget->archiveBytes : kMaxArchiveBytes);
}

void admitCapturedArchive(const std::string_view bytes, const fs::path& path)
{
    const auto availableEntries = activeResourceBudget ? activeResourceBudget->archiveEntries : 100000u;
    const auto availableBytes = activeResourceBudget ? activeResourceBudget->archiveBytes : kMaxArchiveBytes;
    if (bytes.size() > availableBytes)
        failCoded(kExitInput, "archiveCaptureLimit", "Captured archives exceed the command byte budget");
    std::uint32_t entries{};
    try { entries = detail::admitArchiveIndex(bytes, availableEntries); }
    catch (const detail::InputReadError& error)
    {
        if (error.kind == detail::InputReadError::Kind::LimitExceeded)
            failCoded(kExitInput, "archiveIndexLimit", error.what());
        failCoded(kExitUsage, "invalid_request",
            "Resource archive is not a readable GOB/GOO archive: " + path.string() + ". " + error.what());
    }
    if (activeResourceBudget)
    {
        activeResourceBudget->archiveBytes -= bytes.size();
        activeResourceBudget->archiveEntries -= entries;
    }
}

void admitArchiveEntries(const sed::assets::GobArchive& archive,
    std::size_t& remainingBytes, std::size_t& remainingEntries)
{
    try
    {
        detail::admitArchivePayloads(archive, remainingBytes, remainingEntries);
    }
    catch (const detail::InputReadError& error)
    {
        failCoded(kExitInput, "archiveExpansionLimit", error.what());
    }
}

class CommandSourceScope
{
public:
    CommandSourceScope(const CommandOptions& options, detail::PathFormatter formatPath)
        : sources_(formatPath), previous_(activeSourceReads), previousResources_(activeResourceChecks),
        previousBudget_(activeResourceBudget), previousOptions_(activeCommandOptions)
    {
        activeSourceReads = &sources_;
        activeResourceChecks = &resourceChecks_;
        activeResourceBudget = &budget_;
        activeCommandOptions = &options;
    }
    ~CommandSourceScope()
    {
        activeSourceReads = previous_;
        activeResourceChecks = previousResources_;
        activeResourceBudget = previousBudget_;
        activeCommandOptions = previousOptions_;
    }
    CommandSourceScope(const CommandSourceScope&) = delete;
    CommandSourceScope& operator=(const CommandSourceScope&) = delete;
private:
    detail::SourceReadSet sources_;
    detail::SourceReadSet* previous_;
    std::vector<std::function<void()>> resourceChecks_;
    std::vector<std::function<void()>>* previousResources_;
    CommandResourceBudget budget_;
    CommandResourceBudget* previousBudget_;
    const CommandOptions* previousOptions_;
};

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string extensionOf(const fs::path& p) { return lower(p.extension().string()); }

struct OpenedDocument
{
    sed::Level level;
    std::string inputSha256;
    std::string sourceRevision;
    std::vector<sed::assets::SedOptionalSection> nativeSections{};
    std::int64_t nativeTimestamp{};
    bool isJkl{};
    bool isSed{};
    bool isJed{};
};

std::vector<std::byte> toBytes(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// Opens a JKL/NDY (text) or native SED (binary) document. Any unreadable
// or unparseable input is an input error (exit 3), never a guess.
OpenedDocument openDocumentBytes(const fs::path& path, const std::string& bytes,
    detail::PathFormatter formatExtension = detail::legacyPathBytes)
{
    OpenedDocument doc;
    doc.inputSha256 = sha256Hex(bytes);
    doc.sourceRevision = doc.inputSha256; // JKL contract: revision == input hash.
    const std::string ext = lower(formatExtension(path.extension()));
    try
    {
        if (ext == ".sed")
        {
            doc.isSed = true;
            sed::assets::SedFile file = sed::assets::loadSedFileBytes(toBytes(bytes));
            doc.level = std::move(file.level);
            (void)sed::assets::applySedLightRgbSection(doc.level, file.optionalSections);
            (void)sed::assets::applySedArchAuthoringSection(doc.level, file.optionalSections);
            doc.nativeSections = std::move(file.optionalSections);
            doc.nativeTimestamp = file.timestamp;
        }
        else if (ext == ".jed" || ext == ".ned")
        {
            std::istringstream in(bytes);
            doc.level = sed::assets::loadFromJED(in);
            doc.isJkl = false;
            doc.isJed = true;
        }
        else if (ext == ".asc")
        {
            // The legacy editor's ASCII tri-mesh import, as RED's Open does.
            std::istringstream in(bytes);
            doc.level = sed::assets::loadFromASC(in);
        }
        else if (ext == ".lev")
        {
            // A Dark Forces level imported as JK geometry, as RED's Open does
            // with its default options. Only the .LEV bytes bound by the input
            // hash are read: the objects of a same-name .O file are not.
            std::istringstream in(bytes);
            sed::assets::LevLoadOptions options{};
            options.loadObjects = false;
            doc.level = sed::assets::loadFromLEV(in, options);
        }
        else // .jkl / .ndy and extension-less text levels
        {
            // The core text parser is lenient about foreign text; the
            // agent contract refuses input it cannot represent instead.
            if (bytes.find("SECTION:") == std::string::npos)
                fail(kExitInput, "input is not a JKL/NDY document (no SECTION header)");
            std::istringstream in(bytes);
            doc.level = sed::assets::loadFromJKL(in);
            doc.isJkl = true;
        }
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot open document: ") + e.what());
    }
    return doc;
}

OpenedDocument openDocument(const fs::path& path, detail::PathFormatter formatExtension = detail::legacyPathBytes)
{
    return openDocumentBytes(path, readFileBytes(path), formatExtension);
}

std::vector<sed::assets::SedOptionalSection> nativeAuthoringSections(const sed::Level& level)
{
    std::vector<sed::assets::SedOptionalSection> sections{};
    if ( const auto rgb = sed::assets::makeSedLightRgbSection(level) )
    {
        sections.push_back(*rgb);
    }
    if ( const auto arch = sed::assets::makeSedArchAuthoringSection(level) )
    {
        sections.push_back(*arch);
    }
    return sections;
}

// EDIT can contain the only copy of an unsaved script/resource buffer; HIST
// binds an undo timeline to the current Level. Retaining either unchanged
// after a model edit can also make its references or baseline stale. The
// Qt-free agent cannot safely rewrite these application-owned payloads.
std::string buildNativeOutput(const OpenedDocument& doc, const sed::Level& level,
    sed::Level* reopenedLevel = nullptr)
{
    auto sections = nativeAuthoringSections(level);
    if ( doc.isSed )
    {
        const auto baseline = sed::assets::saveToSedBytes(doc.level,
            nativeAuthoringSections(doc.level),
            sed::assets::SedSerializationProfile::ProtectedV1);
        const auto proposed = sed::assets::saveToSedBytes(level, sections,
            sed::assets::SedSerializationProfile::ProtectedV1);
        if ( baseline == proposed )
        {
            // Preserve even future optional sections byte-for-byte, including
            // their original ordering, for native copies and true no-op edits.
            sections = doc.nativeSections;
        }
        else if ( std::any_of(doc.nativeSections.begin(), doc.nativeSections.end(),
            [](const sed::assets::SedOptionalSection& section)
            {
                return section.type != sed::assets::SedLightRgbSectionType
                    && section.type != sed::assets::SedArchAuthoringSectionType;
            }) )
        {
            failCoded(kExitRefusal, "nativeWorkspaceEditUnsupported",
                "Cannot edit this native SED document through the agent: "
                "its workspace, history or unknown optional sections require "
                "a document-aware transaction. Edit it in RED, or explicitly "
                "convert a separate working copy to an engine format first.");
        }
    }
    const auto bytes = sed::assets::saveToSedBytes(level, sections,
        sed::assets::SedSerializationProfile::ProtectedV1, doc.nativeTimestamp);
    auto reopened = sed::assets::loadSedFileBytes(bytes);
    (void)sed::assets::applySedLightRgbSection(reopened.level, reopened.optionalSections);
    (void)sed::assets::applySedArchAuthoringSection(reopened.level, reopened.optionalSections);
    if ( reopenedLevel )
    {
        *reopenedLevel = std::move(reopened.level);
    }
    std::string output(bytes.size(), '\0');
    std::memcpy(output.data(), bytes.data(), bytes.size());
    return output;
}

void checkApplyOutputFormat(const OpenedDocument& doc, const fs::path& path, const bool writeJed = false,
    detail::PathFormatter formatExtension = detail::legacyPathBytes)
{
    const std::string expected = doc.isSed ? ".sed" : writeJed ? ".jed"
        : doc.level.kind == sed::ProjectType::IJIM ? ".ndy" : ".jkl";
    if ( lower(formatExtension(path.extension())) != expected )
    {
        failCoded(kExitRefusal, "outputFormatMismatch",
            "Apply output must end in " + expected
            + " to match the reviewed serialization; use convert for a format change.");
    }
}

// Optional resource evidence for the JKL writer's dependency scan. Without
// it the writer can only count what the document names directly, so the
// declared SOUNDS, MATERIALS and KEYFRAMES capacities omit everything a
// model, puppet, sound class or script pulls in.
struct ExportDependencyScan
{
    static constexpr std::size_t kMaxWarnings = 64;
    const sed::assets::ResourceLocator* resources{};
    const sed::assets::Templates* templates{};
    sed::assets::JklExportBufferReport* buffers{};
    std::vector<std::string>* warnings{};
    bool* warningsTruncated{};
};

void requireMatchingExportCogSources(const sed::assets::ResourceLocator& resources)
{
    if ( !activeResourceBudget )
    {
        return;
    }
    std::map<std::string, std::string> exportHashes;
    for ( const auto& [name, schemaHash] : activeResourceBudget->cogSchemaChoices )
    {
        auto found = exportHashes.find(name);
        if ( found == exportHashes.end() )
        {
            const auto payload = resources.tryReadTextWithExt(name, ".cog");
            found = exportHashes.emplace(name, payload ? sha256Hex(*payload) : std::string{}).first;
        }
        if ( found->second != schemaHash )
        {
            failCoded(kExitRefusal, "exportCogResourceAmbiguous",
                "COG schema and export resource payloads differ for '" + name
                    + "'; supply unambiguous hash-bound archives for both the operation and export.");
        }
    }
}

// JED, the editor format JED, NJED and RED's own Save read and write, holds JK
// and MotS levels. The bytes are read back before they are reported: a reload
// that changes the project or loses sectors, faces, corners, Things (or their
// templates), lights or COGs is refused outright. What the format itself
// cannot hold is listed in `losses` (each {what, count, examples}): JED keeps
// no level templates (JED keeps them in the project's template file) and no
// Thing instance names (a JED Thing row names only its template). Callers
// write a lossy JED only against the reviewed outputSha256.
std::string buildJedOutput(const sed::Level& level, sed::Level* reopenedLevel = nullptr, Json* losses = nullptr)
{
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        failCoded(kExitRefusal, "outputFormatMismatch", "JED holds JK and MotS levels; write this project as .jkl, .ndy or .sed");
    }
    std::string text;
    sed::Level back;
    try
    {
        std::ostringstream out;
        sed::assets::saveToJED(level, out);
        text = out.str();
        std::istringstream in(text);
        back = sed::assets::loadFromJED(in);
    }
    catch ( const std::exception& error )
    {
        failCoded(kExitRefusal, "jedWriteFailed", std::string("cannot write this level as JED: ") + error.what());
    }
    const auto corners = [](const sed::Level& l) {
        std::size_t surfaces = 0, total = 0;
        for ( const auto& sector : l.sectors )
        {
            surfaces += sector.surfaces.size();
            for ( const auto& surface : sector.surfaces ) total += surface.vertices.size();
        }
        return std::pair<std::size_t, std::size_t>{ surfaces, total };
    };
    const auto templateOf = [](const sed::Thing& thing) -> const std::string& {
        return thing.templateName.empty() ? thing.name : thing.templateName;
    };
    std::string lost;
    const auto note = [&](const bool same, const char* what) { if ( !same ) lost += (lost.empty() ? "" : ", ") + std::string(what); };
    note(back.kind == level.kind, "project type");
    note(back.sectors.size() == level.sectors.size(), "sectors");
    note(corners(back) == corners(level), "faces or corners");
    note(back.things.size() == level.things.size(), "Things");
    bool templatesKept = back.things.size() == level.things.size();
    for ( std::size_t i = 0; templatesKept && i < level.things.size(); ++i )
    {
        templatesKept = sed::text::iequalsAscii(templateOf(back.things[i]), templateOf(level.things[i]));
    }
    note(templatesKept, "Thing templates");
    note(back.lights.size() == level.lights.size(), "lights");
    note(back.cogs.size() == level.cogs.size(), "COGs");
    if ( !lost.empty() )
    {
        failCoded(kExitRefusal, "jedRoundTripLoss", "Reading the JED back changes the " + lost + "; it was not written");
    }
    if ( losses )
    {
        *losses = Json::makeArray();
        const auto add = [&](const char* what, const std::size_t count, Json examples) {
            Json row = Json::makeObject();
            row.set("what", Json::makeString(what));
            row.set("count", Json::makeNumber(static_cast<double>(count)));
            row.set("examples", std::move(examples));
            losses->array.push_back(std::move(row));
        };
        if ( back.templates.size() < level.templates.size() )
        {
            Json names = Json::makeArray();
            for ( std::size_t i = 0; i < level.templates.size() && names.array.size() < 8; ++i )
                names.array.push_back(Json::makeString(level.templates[i].name));
            add("levelTemplates", level.templates.size() - back.templates.size(), std::move(names));
        }
        std::size_t renamed = 0;
        Json examples = Json::makeArray();
        for ( const auto& thing : level.things )
        {
            if ( thing.templateName.empty() || sed::text::iequalsAscii(thing.templateName, thing.name) ) continue;
            ++renamed;
            if ( examples.array.size() < 8 ) examples.array.push_back(Json::makeString(thing.templateName + " " + thing.name));
        }
        if ( renamed ) add("thingNames", renamed, std::move(examples));
    }
    if ( reopenedLevel ) *reopenedLevel = std::move(back);
    return text;
}

// Serializes an isolated document copy and reopens it, so a plan can
// never report bytes the reader would reject. Returns the output bytes.
std::string buildOutput(const OpenedDocument& doc, const sed::Level& level,
    const ExportDependencyScan* scan = nullptr, sed::Level* reopenedLevel = nullptr)
{
    try
    {
        if (doc.isSed)
        {
            return buildNativeOutput(doc, level, reopenedLevel);
        }
        sed::assets::JklSaveOptions options{};
        std::optional<sed::Level> hydrated;
        if (scan)
        {
            options.resources = scan->resources;
            options.templatesDb = scan->templates;
            if ( scan->resources )
            {
                requireMatchingExportCogSources(*scan->resources);
                if ( !level.cogs.empty() )
                {
                    hydrated.emplace(level);
                    if ( !sed::assets::finalizeJklImportCogValues(*hydrated, *scan->resources, true) )
                    {
                        failCoded(kExitRefusal, "exportCogReferenceUnresolved",
                            "Cannot hydrate imported COG values/defaults without repairing a target; repair it explicitly first.");
                    }
                }
            }
            if (scan->warnings)
            {
                options.onDiagnostic = [scan](const sed::DiagnosticSeverity severity, const std::string_view message)
                {
                    if (severity != sed::DiagnosticSeverity::warning) return;
                    if (scan->warnings->size() < ExportDependencyScan::kMaxWarnings)
                        scan->warnings->emplace_back(message);
                    else if (scan->warningsTruncated)
                        *scan->warningsTruncated = true;
                };
            }
        }
        const sed::Level& exportLevel = hydrated ? *hydrated : level;
        const auto built = sed::assets::buildJklExport(exportLevel, options);
        if ( scan && scan->buffers )
        {
            *scan->buffers = built.buffers;
            if ( scan->resources && exportLevel.importedJklCogRowsPresent )
            {
                scan->buffers->dependencyScanComplete = false;
                sed::diagnosticEmit(options.onDiagnostic, sed::DiagnosticSeverity::warning,
                    "Imported COG arguments remain unresolved; export dependencies may be incomplete.");
            }
        }
        std::istringstream in(built.text);
        auto reopened = sed::assets::loadFromJKL(in);
        if ( reopenedLevel )
        {
            *reopenedLevel = std::move(reopened);
        }
        return built.text;
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot serialize document: ") + e.what());
    }
}

void checkRevision(const OpenedDocument& doc, const Json& request)
{
    if (const Json* v = request.find("inputSha256"))
    {
        if (v->type != Json::Type::String) fail(kExitUsage, "inputSha256 must be a string");
        if (lower(v->str) != doc.inputSha256)
            fail(kExitConflict, "inputSha256 does not match the current input (stale request)");
    }
    if (const Json* v = request.find("sourceRevision"))
    {
        if (v->type != Json::Type::String) fail(kExitUsage, "sourceRevision must be a string");
        if (lower(v->str) != doc.sourceRevision)
            fail(kExitConflict, "sourceRevision does not match the current input (stale request)");
    }
}

void checkSchemaVersion(const Json& request)
{
    if (const Json* v = request.find("schemaVersion"))
    {
        if (v->type != Json::Type::Number || v->number != 1)
            fail(kExitUsage, "unsupported schemaVersion (expected 1)");
    }
}

std::string requiredString(const Json& obj, const char* key, const char* what)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::String || v->str.empty())
        fail(kExitUsage, std::string(what) + " requires string '" + key + "'");
    return v->str;
}

std::optional<std::string> optionalString(const Json& obj, const char* key)
{
    const Json* v = obj.find(key);
    if (!v) return std::nullopt;
    if (v->type != Json::Type::String) fail(kExitUsage, std::string("'") + key + "' must be a string");
    return v->str;
}

long long requiredInt(const Json& obj, const char* key, const char* what)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::Number || !std::isfinite(v->number)
        || v->number != std::floor(v->number)
        || v->number < std::numeric_limits<std::int32_t>::min()
        || v->number > std::numeric_limits<std::int32_t>::max())
        fail(kExitUsage, std::string(what) + " requires integer '" + key + "'");
    return static_cast<long long>(v->number);
}

// --- COG script archives (A-23) -------------------------------------------
// Explicit, hash-bound GOB/GOO archives only; nothing is discovered.

struct CogArchiveSet
{
    std::vector<sed::assets::CogScriptArchive> archives{};
    std::string revision{}; // sha256 over ordered archive hashes + entries read
};

// D-07 receipt plumbing: retail mip-fallback notes from every MAT in the
// supplied archives, surfaced as assetNotices on validation outcomes.
Json matAssetNotices(const CogArchiveSet& set)
{
    Json notices = Json::makeArray();
    for (std::size_t a = 0; a < set.archives.size(); ++a)
    {
        for (const sed::assets::CogScriptCandidate& candidate : set.archives[a])
        {
            if (!lower(candidate.entryName).ends_with(".mat"))
            {
                continue;
            }
            try
            {
                std::vector<std::byte> raw(candidate.text.size());
                for (std::size_t i = 0; i < raw.size(); ++i)
                {
                    raw[i] = static_cast<std::byte>(candidate.text[i]);
                }
                const sed::assets::MatFile mat =
                    sed::assets::MatFile::parse(std::move(raw));
                if (mat.mipFallbackNote().empty())
                {
                    continue;
                }
                Json row = Json::makeObject();
                row.set("archive", Json::makeNumber(static_cast<double>(a)));
                row.set("entry", Json::makeString(candidate.entryName));
                row.set("note", Json::makeString(mat.mipFallbackNote()));
                notices.array.push_back(std::move(row));
            }
            catch (const std::exception&)
            {
                // An unparsable MAT is a validation finding elsewhere,
                // never a mip notice.
            }
        }
    }
    return notices;
}

// Reads, hash-checks, admits and opens one resourceArchives entry once per
// command; a repeated path and hash returns the same capture.
sed::assets::GobArchive captureResourceArchive(const std::string& path, const std::string& expectedSha)
{
    const std::string key = path + '\n' + expectedSha;
    if (activeResourceBudget)
    {
        const auto found = activeResourceBudget->capturedArchives.find(key);
        if (found != activeResourceBudget->capturedArchives.end()) return found->second;
    }
    const std::string bytes = readArchiveBytes(path);
    if (sha256Hex(bytes) != expectedSha)
        fail(kExitConflict, "resource archive sha256 does not match (stale archive): " + path);
    admitCapturedArchive(bytes, path);
    sed::assets::GobArchive gob;
    try { gob = sed::assets::GobArchive::fromBytes(toBytes(bytes), path); }
    catch (const std::exception& e) {
        failCoded(kExitUsage, "invalid_request",
                  "Resource archive is not a readable GOB/GOO archive: " + path + ". " + e.what());
    }
    if (activeResourceBudget) activeResourceBudget->capturedArchives.emplace(key, gob);
    return gob;
}

CogArchiveSet loadCogArchives(const Json& request)
{
    const Json* list = request.find("resourceArchives");
    if (!list || list->type != Json::Type::Array || list->array.empty())
        fail(kExitUsage, "request requires non-empty 'resourceArchives' (explicit hash-bound archives)");
    if (list->array.size() > 16) fail(kExitUsage, "too many resourceArchives (max 16)");
    CogArchiveSet set;
    std::string revisionInput;
    std::size_t remainingBytes = kMaxExpandedArchiveBytes;
    std::size_t remainingEntries = 100000;
    for (const Json& item : list->array)
    {
        if (!item.isObject()) fail(kExitUsage, "resourceArchives entries must be objects");
        const std::string path = requiredString(item, "path", "resourceArchives entry");
        const std::string expected = lower(requiredString(item, "sha256", "resourceArchives entry"));
        const sed::assets::GobArchive gob = captureResourceArchive(path, expected);
        sed::assets::CogScriptArchive archive;
        admitArchiveEntries(gob, remainingBytes, remainingEntries);
        for (const auto& entry : gob.entries())
        {
            std::string text;
            try
            {
                const auto raw = gob.read(entry.name);
                text.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
            }
            catch (const std::exception& e) { fail(kExitInput, std::string("cannot read archive entry: ") + e.what()); }
            revisionInput += expected + "|" + entry.name + "|" + sha256Hex(text) + "\n";
            archive.push_back(sed::assets::CogScriptCandidate{ entry.name, std::move(text) });
        }
        set.archives.push_back(std::move(archive));
    }
    set.revision = sha256Hex(revisionInput);

    return set;
}

// Mounts the patch's hash-bound resourceArchives for the JKL writer. The
// list is ordered like COG and package resolution (an earlier archive wins),
// while a locator gives the last mount priority, so mount in reverse. The
// locator is archive-only: a name in the level (an absolute path, say) can
// never open a file on disk, and reads as a missing resource instead, which
// marks the scan incomplete. Each archive's payloads are admitted against
// the expansion budget first, as COG resolution does, so index rows that
// alias one payload cannot multiply the scan's work without bound.
sed::assets::ResourceLocator exportResourceLocator(const Json& request)
{
    const Json* list = request.find("resourceArchives");
    if (!list || list->type != Json::Type::Array || list->array.empty())
        fail(kExitUsage, "resolveExportDependencies requires non-empty 'resourceArchives' (explicit hash-bound archives)");
    if (list->array.size() > 16) fail(kExitUsage, "too many resourceArchives (max 16)");
    std::vector<sed::assets::GobArchive> archives;
    std::size_t remainingBytes = kMaxExpandedArchiveBytes;
    std::size_t remainingEntries = 100000;
    for (const Json& item : list->array)
    {
        if (!item.isObject()) fail(kExitUsage, "resourceArchives entries must be objects");
        const std::string path = requiredString(item, "path", "resourceArchives entry");
        const std::string expected = lower(requiredString(item, "sha256", "resourceArchives entry"));
        archives.push_back(captureResourceArchive(path, expected));
        admitArchiveEntries(archives.back(), remainingBytes, remainingEntries);
    }
    sed::assets::ResourceLocator locator;
    locator.setArchivesOnly(true);
    for (auto it = archives.rbegin(); it != archives.rend(); ++it) locator.mountGob(std::move(*it));
    return locator;
}

std::optional<sed::assets::CogSchema> resolveCommandCogSchema(
    const std::string_view name, const CogArchiveSet& set)
{
    auto schema = sed::assets::resolveCogSchema(name, set.archives);
    if ( schema && activeResourceBudget )
    {
        activeResourceBudget->cogSchemaChoices.emplace(
            std::string(name), sed::level::cogSchemaSha256Hex(*schema));
    }
    return schema;
}

sed::level::CogSchemaResolver cogResolver(CogArchiveSet set)
{
    const auto captured = std::make_shared<const CogArchiveSet>(std::move(set));
    return [captured](std::string_view name) -> std::optional<sed::assets::CogSchema> {
        return resolveCommandCogSchema(name, *captured);
    };
}

std::string_view symbolKindName(sed::assets::CogSchemaSymbolKind kind)
{
    using K = sed::assets::CogSchemaSymbolKind;
    switch (kind)
    {
    case K::message: return "message"; case K::int_: return "int";
    case K::flex: return "flex"; case K::float_: return "float";
    case K::vector: return "vector"; case K::thing: return "thing";
    case K::sector: return "sector"; case K::surface: return "surface";
    case K::sound: return "sound"; case K::material: return "material";
    case K::model: return "model"; case K::template_: return "template";
    case K::keyframe: return "keyframe"; case K::ai: return "ai";
    case K::cog: return "cog"; default: return "unknown";
    }
}

sed::assets::CogSchemaSymbolKind parseSymbolKind(const std::string& text)
{
    using K = sed::assets::CogSchemaSymbolKind;
    const std::string s = lower(text);
    if (s == "message") return K::message; if (s == "int" || s == "integer") return K::int_;
    if (s == "flex") return K::flex; if (s == "float") return K::float_;
    if (s == "vector" || s == "vect") return K::vector; if (s == "thing" || s == "thg") return K::thing;
    if (s == "sector" || s == "sec") return K::sector; if (s == "surface" || s == "srf") return K::surface;
    if (s == "sound" || s == "wav") return K::sound; if (s == "material" || s == "mat") return K::material;
    if (s == "model" || s == "3do") return K::model; if (s == "template" || s == "tpl") return K::template_;
    if (s == "keyframe" || s == "key") return K::keyframe; if (s == "ai") return K::ai;
    if (s == "cog") return K::cog;
    return K::unknown;
}

sed::level::CogReferenceKind parseReferenceKind(const std::string& text)
{
    const std::string s = lower(text);
    if (s == "thing") return sed::level::CogReferenceKind::thing;
    if (s == "sector") return sed::level::CogReferenceKind::sector;
    if (s == "surface") return sed::level::CogReferenceKind::surface;
    if (s == "cog") return sed::level::CogReferenceKind::cog;
    return sed::level::CogReferenceKind::other;
}

std::optional<std::string> expectedScriptHash(const Json& obj)
{
    if (auto v = optionalString(obj, "expectedScriptSha256")) return v;
    if (auto v = optionalString(obj, "scriptSha256")) return v;
    if (auto v = optionalString(obj, "cogSchemaSha256Hex")) return v;
    return std::nullopt;
}

// Stale script bytes are a conflict (exit 4), never a typed refusal.
void checkScriptHash(const sed::assets::CogSchema* schema, const std::optional<std::string>& expected)
{
    if (!expected) return;
    if (!schema) fail(kExitConflict, "expectedScriptSha256 supplied but the script cannot be resolved (stale request)");
    const std::string actual = sed::level::cogSchemaSha256Hex(*schema);
    if (lower(*expected) != lower(actual))
        fail(kExitConflict, "expectedScriptSha256 does not match the resolved script (stale script hash)");
}

[[noreturn]] void failCogRefusal(const sed::level::CogMutationFailure& failure)
{
    failCoded(kExitRefusal, failure.code, failure.code + ": " + failure.message);
}

// JSON numbers for typed COG values: integral values exactly (so an int of
// 2000000000 stays an int), others in shortest round-trip form; the typed
// parser then decides whether the symbol's kind accepts them.
std::string cogNumberText(const double number)
{
    if (number == std::floor(number) && std::fabs(number) < 9.0e15)
        return std::to_string(static_cast<long long>(number));
    // Shortest fixed or scientific spelling that reads back exactly.
    for (int digits = 6; digits <= 17; ++digits)
    {
        std::ostringstream shorter;
        shorter.imbue(std::locale::classic());
        shorter << std::setprecision(digits) << number;
        double back = 0.0;
        std::istringstream in(shorter.str());
        in.imbue(std::locale::classic());
        if ((in >> back) && back == number) return shorter.str();
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(17) << number;
    return text.str();
}

// A COG surface target may be named as {sector, surface} instead of the
// level's surface number, which an agent would otherwise have to count.
std::int32_t cogTargetIndex(const sed::Level& level, const Json& obj, const std::string& kind,
    const char* context)
{
    const bool hasPair = obj.find("sector") != nullptr || obj.find("surface") != nullptr;
    if (hasPair)
    {
        if (lower(kind) != "surface")
            fail(kExitUsage, std::string(context) + ": 'sector'/'surface' name a surface target; use targetIndex for other kinds");
        if (obj.find("targetIndex"))
            fail(kExitUsage, std::string(context) + ": give either targetIndex or sector/surface, not both");
        const long long sector = requiredInt(obj, "sector", context);
        const long long surface = requiredInt(obj, "surface", context);
        const auto index = (sector < 0 || surface < 0 || sector > std::numeric_limits<std::int32_t>::max()
                || surface > std::numeric_limits<std::int32_t>::max())
            ? std::nullopt
            : sed::level::cogSurfaceTargetIndex(level, static_cast<std::int32_t>(sector),
                static_cast<std::int32_t>(surface));
        if (!index)
            failCoded(kExitRefusal, "cogTargetOutOfRange", "cogTargetOutOfRange: sector "
                + std::to_string(sector) + " surface " + std::to_string(surface)
                + " is not a surface with a provable COG surface number");
        return *index;
    }
    return static_cast<std::int32_t>(requiredInt(obj, "targetIndex", context));
}

void applyAddCogOp(sed::Level& level, const Json& op, const Json& request, int opIndex)
{
    (void)opIndex;
    const std::string script = requiredString(op, "script", "addCog operation");
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    const CogArchiveSet set = [&] {
        // Archives may live on the patch root or on the operation.
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    auto schema = resolveCommandCogSchema(script, set);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    const Json* bindingsJson = op.find("bindings");
    if (!bindingsJson || bindingsJson->type != Json::Type::Array)
        fail(kExitUsage, "addCog operation requires a 'bindings' array");
    // Refuse an oversized request before resolving any target.
    if (bindingsJson->array.size() > 256)
        failCoded(kExitRefusal, "cogBindingBudget", "cogBindingBudget: a new COG accepts at most 256 bindings");
    std::vector<sed::level::CogNamedBinding> bindings;
    for (const Json& b : bindingsJson->array)
    {
        if (!b.isObject()) fail(kExitUsage, "addCog bindings entries must be objects");
        std::string symbol;
        if (auto s = optionalString(b, "symbol")) symbol = *s;
        else if (auto s = optionalString(b, "symbolName")) symbol = *s;
        else if (auto s = optionalString(b, "name")) symbol = *s;
        else fail(kExitUsage, "addCog binding requires 'symbol'");
        const std::string kind = requiredString(b, "kind", "addCog binding");
        sed::level::CogNamedBinding binding{ symbol, parseReferenceKind(kind), -1 };
        // Non-reference symbols (flex, float, int, sound, ...) take a typed
        // value or the script's declared default instead of a target.
        const Json* value = b.find("value");
        const Json* useDefault = b.find("default");
        if (value || useDefault)
        {
            if (b.find("targetIndex") || b.find("sector") || b.find("surface"))
                fail(kExitUsage, "addCog binding '" + symbol + "': a value binding has no target");
            if (useDefault && (useDefault->type != Json::Type::Bool || !useDefault->boolean))
                fail(kExitUsage, "addCog binding '" + symbol + "': 'default' must be true when given");
            if (value)
            {
                if (value->type == Json::Type::String) binding.valueText = value->str;
                else if (value->type == Json::Type::Number && std::isfinite(value->number))
                    binding.valueText = cogNumberText(value->number);
                else fail(kExitUsage, "addCog binding '" + symbol + "': 'value' must be a string or a finite number");
            }
            binding.useDefault = useDefault != nullptr;
        }
        else if (binding.kind != sed::level::CogReferenceKind::other)
        {
            binding.targetIndex = cogTargetIndex(level, b, kind, "addCog binding");
        }
        if (binding.kind == sed::level::CogReferenceKind::other)
        {
            const auto declared = parseSymbolKind(kind);
            if (declared == sed::assets::CogSchemaSymbolKind::unknown)
                fail(kExitUsage, "addCog binding '" + symbol + "': unknown kind '" + kind + "'");
            binding.declaredKind = declared;
        }
        // A typed symbol with neither value nor default is refused by the
        // core with its own code, naming the symbol.
        bindings.push_back(std::move(binding));
    }
    // `defaults: true` gives every non-reference symbol the caller did not
    // bind its declared default. References (thing, sector, surface, cog)
    // are never filled in: each one is bound explicitly, -1 for none.
    if (const Json* defaults = op.find("defaults"))
    {
        if (defaults->type != Json::Type::Bool)
            fail(kExitUsage, "addCog 'defaults' must be a boolean");
        if (defaults->boolean && schema)
        {
            for (const sed::assets::CogSymbol* symbol : schema->levelValues())
            {
                if (sed::level::cogReferenceKindForSymbol(symbol->kind) != sed::level::CogReferenceKind::other)
                    continue;
                const bool bound = std::any_of(bindings.begin(), bindings.end(),
                    [&](const sed::level::CogNamedBinding& item)
                    { return sed::text::iequalsAscii(item.symbolName, symbol->name); });
                if (bound) continue;
                sed::level::CogNamedBinding binding{ symbol->name, sed::level::CogReferenceKind::other, -1 };
                binding.useDefault = true;
                bindings.push_back(std::move(binding));
            }
        }
    }
    auto prepared = sed::level::prepareAddCog(level, script, schema ? &*schema : nullptr,
        bindings, cogResolver(set), expected);
    if (!prepared.ok()) failCogRefusal(*prepared.failure);
    if (!prepared.apply(level)) fail(kExitConflict, "addCog prepared step is stale for this document");
}

// Headless counterpart of the GUI's Make Door: same core conversion, same
// fixed stock script, symbols loaded with defaults from the patch
// resources. Height defaults to the core bounding-box resolution over
// embedded level templates (no project template database headlessly);
// an explicit 'height' overrides it.
// Headless counterpart of the editor's project template database. A new
// document carries no templates, so nothing can be placed until the templates
// a design needs (and everything they inherit from or name) are copied in.
// The source is an explicit hash-bound .tpl file; the bundled data/master.tpl
// and data/mots.tpl are the usual choices. Templates the document already
// defines are kept exactly as authored and are never overwritten.
// defineTemplate: a new template for this level, e.g. a prop whose model
// lives in a custom archive. The definition is parsed by the same template
// reader that loads master.tpl, so it is typed exactly as an imported one.
void applyDefineTemplateOp(sed::Level& level, const Json& op, Json& reports, const std::size_t opIndex)
{
    for (const auto& [key, value] : op.object)
    {
        (void)value;
        if (key != "op" && key != "name" && key != "parent" && key != "values")
            fail(kExitUsage, "Unknown field '" + key + "' in defineTemplate");
    }
    // The game reads template lines with its own tokenizer: commas separate
    // arguments like spaces, a trailing backslash joins the next line, and a
    // line starting with ';' is a comment. A token must read back as exactly
    // one argument there, so none of those characters are accepted, and
    // parentheses (frames) must balance.
    const auto token = [](const std::string& text, const std::size_t maxLength)
    {
        if (text.empty() || text.size() > maxLength) return false;
        int depth = 0;
        for (const unsigned char c : text)
        {
            if (c <= 32 || c >= 127 || c == '"' || c == '#' || c == '=' || c == ',' || c == '\\') return false;
            if (c == '(') ++depth;
            if (c == ')' && --depth < 0) return false;
        }
        return depth == 0;
    };
    const std::string name = requiredString(op, "name", "defineTemplate");
    const std::string parent = requiredString(op, "parent", "defineTemplate");
    if (!token(name, 31) || name.front() == ';')
        fail(kExitUsage, "defineTemplate 'name' must be an ASCII token of at most 31 characters "
                         "(no commas, backslashes or quotes, not starting with ';')");
    if (!token(parent, 31)) fail(kExitUsage, "defineTemplate 'parent' must be a template name or \"none\"");
    const auto present = [&level](const std::string_view wanted)
    {
        return std::any_of(level.templates.begin(), level.templates.end(),
            [&](const sed::ThingTemplate& item) { return sed::text::iequalsAscii(item.name, wanted); });
    };
    if (present(name))
        failCoded(kExitRefusal, "templateExists", "templateExists: the level already defines '" + name + "'");
    if (!sed::text::iequalsAscii(parent, "none") && !present(parent))
        failCoded(kExitRefusal, "templateParentMissing", "templateParentMissing: '" + parent
            + "' is not defined in the level; import it first (importTemplates)");
    const Json* values = op.find("values");
    if (!values || !values->isObject() || values->object.empty() || values->object.size() > 64)
        fail(kExitUsage, "defineTemplate requires 'values': an object of 1..64 template parameters");
    std::string line = name + " " + parent;
    for (const auto& [key, value] : values->object)
    {
        if (!token(key, 31)) fail(kExitUsage, "defineTemplate parameter names must be ASCII tokens");
        std::string text;
        if (value.type == Json::Type::String) text = value.str;
        else if (value.type == Json::Type::Number && std::isfinite(value.number))
            text = cogNumberText(value.number); // an integral value stays an integer the game's atoi reads
        else fail(kExitUsage, "defineTemplate parameter '" + key + "' must be a string or a finite number");
        if (!token(text, 127))
            fail(kExitUsage, "defineTemplate parameter '" + key + "' has an invalid value (a single token without "
                             "spaces, commas, backslashes, quotes, '#' or '=', with balanced parentheses)");
        line += " " + key + "=" + text;
    }
    sed::assets::Templates parsed;
    try
    {
        std::istringstream in(line + "\n");
        parsed = sed::assets::loadTemplates(in);
    }
    catch (const std::exception& e)
    {
        failCoded(kExitUsage, "invalid_request", std::string("defineTemplate does not parse as a template: ") + e.what());
    }
    const sed::ThingTemplate* made = parsed.find(name);
    if (!made || made->values.size() != values->object.size())
        failCoded(kExitUsage, "invalid_request", "defineTemplate does not parse as a template: " + line);
    level.templates.push_back(*made);
    Json report = Json::makeObject();
    report.set("operation", Json::makeNumber(static_cast<double>(opIndex)));
    report.set("defined", Json::makeString(name));
    report.set("line", Json::makeString(line));
    reports.array.push_back(std::move(report));
}

// A hash-bound template file ({path, sha256}), as named by an importTemplates
// operation or by the patch root.
struct LoadedTemplateSource
{
    sed::assets::Templates database;
    std::string sha256;
};

std::shared_ptr<const LoadedTemplateSource> loadTemplateSource(const Json& source)
{
    constexpr std::size_t kMaxTemplateSourceBytes = 16u * 1024u * 1024u;
    if (!source.isObject())
        fail(kExitUsage, "'templateSource' must be an object {path, sha256}");
    for (const auto& [key, value] : source.object)
    {
        (void)value;
        if (key != "path" && key != "sha256")
            fail(kExitUsage, "Unknown field '" + key + "' in templateSource");
    }
    const std::string path = requiredString(source, "path", "templateSource");
    const std::string expected = lower(requiredString(source, "sha256", "templateSource"));
    const std::string cacheKey = path + '\n' + expected;
    if (activeResourceBudget)
    {
        const auto found = activeResourceBudget->templateSources.find(cacheKey);
        if (found != activeResourceBudget->templateSources.end())
            return found->second;
    }
    auto loaded = std::make_shared<LoadedTemplateSource>();
    loaded->sha256 = expected;
    const std::size_t available = activeResourceBudget ? activeResourceBudget->archiveBytes : kMaxArchiveBytes;
    const std::string bytes = readFileBytes(path, std::min(kMaxTemplateSourceBytes, available));
    if (sha256Hex(bytes) != loaded->sha256)
        fail(kExitConflict, "template source sha256 does not match (stale template file): " + path);
    try
    {
        std::istringstream in(bytes);
        loaded->database = sed::assets::loadTemplates(in);
    }
    catch (const std::exception& e)
    {
        failCoded(kExitUsage, "invalid_request",
            std::string("templateSource is not a readable template file: ") + e.what());
    }
    if (activeResourceBudget)
    {
        // Each distinct source is charged once, like a captured archive.
        activeResourceBudget->archiveBytes -= std::min(bytes.size(), activeResourceBudget->archiveBytes);
        activeResourceBudget->templateSources.emplace(cacheKey, loaded);
    }
    return loaded;
}

void applyImportTemplatesOp(sed::Level& level, const Json& op, const Json& request,
    const std::size_t opIndex, Json& reports)
{
    constexpr std::size_t kMaxRequestedTemplates = 256;
    constexpr std::size_t kMaxImportedTemplates = 4096;
    for (const auto& [key, value] : op.object)
    {
        (void)value;
        if (key != "op" && key != "templates" && key != "templateSource")
            fail(kExitUsage, "Unknown field '" + key + "' in importTemplates");
    }
    const Json* names = op.find("templates");
    if (!names || names->type != Json::Type::Array || names->array.empty()
        || names->array.size() > kMaxRequestedTemplates)
        fail(kExitUsage, "importTemplates requires 'templates': 1..256 template names");
    const Json* source = op.find("templateSource");
    if (!source) source = request.find("templateSource");
    if (!source || !source->isObject())
        fail(kExitUsage, "importTemplates requires 'templateSource' {path, sha256} on the operation or the patch");
    const std::shared_ptr<const LoadedTemplateSource> loaded = loadTemplateSource(*source);
    const sed::assets::Templates& database = loaded->database;
    const std::string& expected = loaded->sha256;

    // Every name the walk meets (imported, already present or unresolved)
    // counts against this cap, so one template naming a million distinct
    // templates is refused rather than walked.
    constexpr std::size_t kMaxVisitedNames = 65536;
    std::unordered_set<std::string> documentNames;    // lowercase, trimmed
    documentNames.reserve(level.templates.size());
    for (const sed::ThingTemplate& item : level.templates)
        documentNames.insert(lower(std::string(sed::text::trimView(item.name))));
    const auto isNone = [](const std::string_view name)
    {
        const auto trimmed = sed::text::trimView(name);
        return trimmed.empty() || sed::text::iequalsAscii(trimmed, "none");
    };

    // Depth-first over parents and template-typed values with an explicit
    // stack (a deep inheritance chain must not exhaust the call stack);
    // dependencies are emitted before the templates that name them.
    struct Frame
    {
        const sed::ThingTemplate* tpl{};
        std::string key{};
        std::size_t next{}; // 0: parent not yet visited; n > 0: values[n - 1] is next
    };
    std::vector<const sed::ThingTemplate*> ordered;   // dependencies first
    std::vector<Frame> stack;
    std::unordered_set<std::string> visiting;         // keys on the stack
    std::unordered_set<std::string> finished;
    Json alreadyPresent = Json::makeArray();
    Json unresolved = Json::makeArray();
    const auto enter = [&](const std::string_view rawName, const bool requested)
    {
        const std::string name{ sed::text::trimView(rawName) };
        const std::string key = lower(name);
        if (finished.contains(key)) return;
        if (visiting.contains(key))
            failCoded(kExitRefusal, "templateDependencyCycle",
                "Template '" + name + "' takes part in a dependency cycle in the template source");
        if (finished.size() + visiting.size() >= kMaxVisitedNames)
            failCoded(kExitRefusal, "templateImportLimit",
                "importTemplates would follow more than 65536 template names in one operation");
        if (documentNames.contains(key))
        {
            // The document's own definition wins, including for inherited
            // and referenced templates of an imported one.
            finished.insert(key);
            alreadyPresent.array.push_back(Json::makeString(name));
            return;
        }
        const sed::ThingTemplate* found = database.find(name);
        if (!found)
        {
            if (requested)
                failCoded(kExitRefusal, "templateNotFound",
                    "Template '" + name + "' is not defined in the template source");
            // A value may name a template the source never defines (stock
            // files contain a few). It is reported, not invented.
            finished.insert(key);
            unresolved.array.push_back(Json::makeString(name));
            return;
        }
        if (ordered.size() + stack.size() >= kMaxImportedTemplates)
            failCoded(kExitRefusal, "templateImportLimit",
                "importTemplates would add more than 4096 templates in one operation");
        if (!isNone(found->parent))
        {
            const std::string parent{ sed::text::trimView(found->parent) };
            if (!documentNames.contains(lower(parent)) && !database.find(parent))
                failCoded(kExitRefusal, "templateParentMissing",
                    "Template '" + found->name + "' inherits from '" + found->parent
                        + "', which neither the document nor the template source defines");
        }
        visiting.insert(key);
        stack.push_back(Frame{ found, key, 0 });
    };
    for (const Json& entry : names->array)
    {
        if (entry.type != Json::Type::String || entry.str.empty() || entry.str.size() > 127
            || std::any_of(entry.str.begin(), entry.str.end(), [](const unsigned char c)
                { return c < 33 || c > 126 || c == '"' || c == '#'; }))
            fail(kExitUsage, "importTemplates names must be ASCII tokens of at most 127 characters");
        enter(entry.str, true);
        while (!stack.empty())
        {
            // `enter` may grow the stack, so the top is re-read after each call.
            if (stack.back().next == 0)
            {
                stack.back().next = 1;
                const sed::ThingTemplate* tpl = stack.back().tpl;
                if (!isNone(tpl->parent))
                {
                    enter(tpl->parent, false);
                    continue;
                }
            }
            bool descended = false;
            while (stack.back().next - 1 < stack.back().tpl->values.size())
            {
                const sed::TplValue& value = stack.back().tpl->values[stack.back().next - 1];
                ++stack.back().next;
                if (value.adjType != sed::AdjType::tpl || isNone(value.stringValue)) continue;
                const std::size_t depth = stack.size();
                enter(value.stringValue, false);
                if (stack.size() != depth)
                {
                    descended = true;
                    break;
                }
            }
            if (descended) continue;
            const Frame done = std::move(stack.back());
            stack.pop_back();
            visiting.erase(done.key);
            finished.insert(done.key);
            ordered.push_back(done.tpl);
        }
    }
    if (level.templates.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - ordered.size())
        failCoded(kExitRefusal, "templateImportLimit", "The document's template table is full");

    Json added = Json::makeArray();
    for (const sed::ThingTemplate* item : ordered)
    {
        level.templates.push_back(*item);
        added.array.push_back(Json::makeString(item->name));
    }
    Json report = Json::makeObject();
    report.set("operation", Json::makeNumber(static_cast<double>(opIndex)));
    report.set("templateSourceSha256", Json::makeString(expected));
    report.set("added", std::move(added));
    report.set("alreadyPresent", std::move(alreadyPresent));
    report.set("unresolvedReferences", std::move(unresolved));
    reports.array.push_back(std::move(report));
}

void applyMakeDoorOp(sed::Level& level, const Json& op, const Json& request, int opIndex)
{
    const long long thing = requiredInt(op, "thing", "makeDoor operation");
    if (thing < 0 || static_cast<std::size_t>(thing) >= level.things.size())
        fail(kExitInput, "thing selector out of range for this input");
    const std::size_t thingIndex = static_cast<std::size_t>(thing);
    if (level.things[thingIndex].archLightingIndex >= 0
        || std::any_of(level.motsArchAuthoring.begin(), level.motsArchAuthoring.end(),
            [thing](const auto& entry) { return entry.thingIndex == static_cast<std::int32_t>(thing); }))
        failCoded(kExitRefusal, "entityArchBound",
            "Clear this Thing's ARCH binding explicitly before changing its pose, template, or parameters; stored sample arrays will be retained");
    const std::string script{ sed::level::kMakeDoorScript };
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    const CogArchiveSet set = [&] {
        // Archives may live on the patch root or on the operation.
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    auto schema = resolveCommandCogSchema(script, set);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    if (!schema)
        failCoded(kExitRefusal, "doorScriptUnavailable",
            "makeDoor requires the stock door script " + script + " in the patch resources");
    const auto symbols = sed::assets::loadCogSymbolsFromTextResult(schema->sourceText, false);
    if (!symbols.loaded())
        failCoded(kExitRefusal, "doorScriptUnavailable",
            "makeDoor could not load symbols from " + script + ": " + symbols.diagnostic);
    double height = 0.0;
    if (const Json* heightJson = op.find("height"))
    {
        (void)opIndex;
        if (heightJson->type != Json::Type::Number)
            fail(kExitUsage, "makeDoor 'height' must be a number");
        height = heightJson->number;
        if (!std::isfinite(height) || height <= 0.0 || height > 100000.0)
            fail(kExitUsage, "makeDoor 'height' must be finite and in (0, 100000]");
    }
    else
    {
        height = sed::level::makeDoorHeight(level, nullptr, thingIndex);
    }
    const auto result = sed::level::levelMakeDoor(level, thingIndex, height, script, symbols.symbols);
    switch (result.error)
    {
    case sed::level::MakeDoorError::None:
        return;
    case sed::level::MakeDoorError::InvalidThing:
        fail(kExitInput, "thing selector out of range for this input");
    case sed::level::MakeDoorError::AlreadyDoor:
        failCoded(kExitRefusal, "entityAlreadyDoor", "The Thing already declares numframes (already a door)");
    case sed::level::MakeDoorError::FlatBoundingBox:
        failCoded(kExitRefusal, "entityDoorNoHeight",
            "makeDoor could not resolve a positive door height; supply 'height' or a template bounding box");
    case sed::level::MakeDoorError::IdSpaceExhausted:
        failCoded(kExitRefusal, "entityOperationUnsupported", "Entity identity capacity is exhausted");
    }
}

// The gameplay assistants: doors, elevators, switches, objectives and cutscenes.
std::filesystem::path cogTemplateLibraryOrFail(const std::vector<std::string>& args, std::size_t libraryArg);

std::vector<sed::CogValue> stockAssistantSymbols(const Json& op, const Json& request, std::string_view script, const std::string& opName)
{
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    const CogArchiveSet set = [&] {
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    auto schema = resolveCommandCogSchema(std::string(script), set);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    if (!schema)
        failCoded(kExitRefusal, "assistantScriptUnavailable",
            opName + " requires the stock script " + std::string(script) + " in the patch resources");
    const auto symbols = sed::assets::loadCogSymbolsFromTextResult(schema->sourceText, false);
    if (!symbols.loaded())
        failCoded(kExitRefusal, "assistantScriptUnavailable",
            opName + " could not load symbols from " + std::string(script) + ": " + symbols.diagnostic);
    return symbols.symbols;
}

void applyAssistantOp(sed::Level& level, const Json& op, const Json& request, const std::string& opName)
{
    const auto number = [&](const char* key, const double fallback) {
        const Json* j = op.find(key);
        if (!j) return fallback;
        if (j->type != Json::Type::Number) fail(kExitUsage, opName + " '" + key + "' must be a number");
        return j->number;
    };
    const auto integer = [&](const Json& j, const char* what) {
        if (j.type != Json::Type::Number || std::floor(j.number) != j.number || std::abs(j.number) > 2147483647.0)
            fail(kExitUsage, opName + " " + what + " must be an integer");
        return static_cast<std::int32_t>(j.number);
    };
    const auto vector3 = [&](const char* key, const std::array<double, 3> fallback) {
        const Json* j = op.find(key);
        if (!j) return fallback;
        if (j->type != Json::Type::Array || j->array.size() != 3) fail(kExitUsage, opName + " '" + key + "' must be [x, y, z]");
        std::array<double, 3> v{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (j->array[i].type != Json::Type::Number) fail(kExitUsage, opName + " '" + key + "' must be [x, y, z]");
            v[i] = j->array[i].number;
        }
        return v;
    };
    const auto surfaceOf = [&](const Json& j, const char* what) {
        if (j.type != Json::Type::Array || j.array.size() != 2) fail(kExitUsage, opName + " " + what + " must be [sector, surface]");
        return sed::SurfaceRef{ integer(j.array[0], what), integer(j.array[1], what) };
    };
    const auto surfaceList = [&](const char* key) {
        std::vector<sed::SurfaceRef> out;
        if (const Json* j = op.find(key))
        {
            if (j->type != Json::Type::Array) fail(kExitUsage, opName + " '" + key + "' must be a list of [sector, surface]");
            for (const Json& item : j->array) out.push_back(surfaceOf(item, key));
        }
        return out;
    };
    const auto intList = [&](const char* key) {
        std::vector<std::int32_t> out;
        const Json* j = op.find(key);
        if (!j || j->type != Json::Type::Array) fail(kExitUsage, opName + " requires '" + key + "' (a list of Thing indices)");
        for (const Json& item : j->array) out.push_back(integer(item, key));
        return out;
    };
    const auto boolean = [&](const char* key, const bool fallback) {
        const Json* j = op.find(key);
        if (!j) return fallback;
        if (j->type != Json::Type::Bool) fail(kExitUsage, opName + " '" + key + "' must be true or false");
        return j->boolean;
    };
    sed::level::AssistantResult result;
    if (opName == "doorAssistant")
    {
        sed::level::DoorAssistantRequest r;
        r.pieces = intList("pieces");
        r.openOffset = vector3("openOffset", r.openOffset);
        r.speed = number("speed", r.speed);
        r.wait = number("wait", r.wait);
        r.light = number("light", r.light);
        const auto symbols = stockAssistantSymbols(op, request, sed::level::kDoorAssistantScript, opName);
        result = sed::level::levelDoorAssistant(level, nullptr, r, symbols);
    }
    else if (opName == "elevatorAssistant")
    {
        sed::level::ElevatorAssistantRequest r;
        r.elevator = static_cast<std::int32_t>(requiredInt(op, "elevator", "elevatorAssistant operation"));
        r.travel = vector3("travel", r.travel);
        if (const Json* button = op.find("button")) r.button = surfaceOf(*button, "button");
        r.callAdjoins = surfaceList("callAdjoins");
        r.speed = number("speed", r.speed);
        r.wait = number("wait", r.wait);
        r.startWait = number("startWait", r.startWait);
        const auto symbols = stockAssistantSymbols(op, request, sed::level::kElevatorAssistantScript, opName);
        result = sed::level::levelElevatorAssistant(level, nullptr, r, symbols);
    }
    else if (opName == "switchAssistant")
    {
        sed::level::SwitchAssistantRequest r;
        r.pieces = intList("pieces");
        r.openOffset = vector3("openOffset", r.openOffset);
        r.switches = surfaceList("switches");
        r.speed = number("speed", r.speed);
        r.wait = number("wait", r.wait);
        r.light = number("light", r.light);
        const auto symbols = stockAssistantSymbols(op, request, sed::level::kSwitchAssistantScript, opName);
        result = sed::level::levelSwitchAssistant(level, nullptr, r, symbols);
    }
    else if (opName == "objectivesAssistant")
    {
        sed::level::ObjectivesAssistantRequest r;
        if (const Json* base = op.find("goalBase")) r.goalBase = integer(*base, "goalBase");
        const Json* goals = op.find("goals");
        if (!goals || goals->type != Json::Type::Array) fail(kExitUsage, "objectivesAssistant requires 'goals' [{trigger, target}]");
        for (const Json& goal : goals->array)
        {
            if (!goal.isObject()) fail(kExitUsage, "objectivesAssistant goals must be objects {trigger, target}");
            const auto trigger = sed::level::parseGoalTrigger(requiredString(goal, "trigger", "objectivesAssistant goal"));
            if (!trigger) fail(kExitUsage, "objectivesAssistant goal trigger must be reach, use or kill");
            r.goals.push_back({ *trigger, static_cast<std::int32_t>(requiredInt(goal, "target", "objectivesAssistant goal")) });
        }
        if (const Json* exit = op.find("exit")) r.exit = surfaceOf(*exit, "exit");
        r.endWhenDone = boolean("endWhenDone", true);
        result = sed::level::levelObjectivesAssistant(level, r);
    }
    else if (opName == "pursuitAssistant")
    {
        sed::level::PursuitAssistantRequest r;
        r.enemy = static_cast<std::int32_t>(requiredInt(op, "enemy", "pursuitAssistant operation"));
        if (op.find("route")) r.route = intList("route");
        if (const Json* sector = op.find("wakeSector")) r.wakeSector = integer(*sector, "wakeSector");
        r.wakeDistance = number("wakeDistance", r.wakeDistance);
        r.moveSpeed = number("moveSpeed", r.moveSpeed);
        r.ramDamage = number("ramDamage", r.ramDamage);
        r.fireInterval = number("fireInterval", r.fireInterval);
        r.survivalChance = number("survivalChance", r.survivalChance);
        const fs::path library = cogTemplateLibraryOrFail({}, 0);
        std::ifstream in(library / std::string(sed::level::kPursuitAssistantScript), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        result = sed::level::levelPursuitAssistant(level, r, sed::assets::loadCogSymbolsFromTextResult(text.str(), false).symbols);
    }
    else
    {
        sed::level::CutsceneAssistantRequest r;
        r.camera = static_cast<std::int32_t>(requiredInt(op, "camera", "cutsceneAssistant operation"));
        if (const Json* trigger = op.find("trigger")) r.trigger = integer(*trigger, "trigger");
        r.speed = number("speed", r.speed);
        r.hold = number("hold", r.hold);
        r.once = boolean("once", true);
        result = sed::level::levelCutsceneAssistant(level, nullptr, r);
    }
    if (!result.ok()) failCoded(kExitRefusal, result.failure->code, result.failure->code + ": " + result.failure->message);
}

void applyRebindOp(sed::Level& level, const Json& op, const Json& request, int opIndex)
{
    (void)opIndex;
    const long long cog = requiredInt(op, "cog", "rebindCogReference operation");
    const long long parameter = requiredInt(op, "parameter", "rebindCogReference operation");
    std::string symbol;
    if (auto s = optionalString(op, "symbol")) symbol = *s;
    else if (auto s = optionalString(op, "symbolName")) symbol = *s;
    else fail(kExitUsage, "rebindCogReference requires 'symbol'");
    std::string kindText;
    if (auto s = optionalString(op, "kind")) kindText = *s;
    else if (auto s = optionalString(op, "type")) kindText = *s;
    else fail(kExitUsage, "rebindCogReference requires 'kind'");
    const long long target = cogTargetIndex(level, op, kindText, "rebindCogReference operation");
    if (cog < 0 || static_cast<std::size_t>(cog) >= level.cogs.size())
        failCoded(kExitRefusal, "cogParameterOutOfRange", "cogParameterOutOfRange: COG index is outside the placed COG list");
    const CogArchiveSet set = [&] {
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    const std::string& scriptName = level.cogs[static_cast<std::size_t>(cog)].name;
    auto schema = resolveCommandCogSchema(scriptName, set);
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    auto prepared = sed::level::prepareRebindCogReference(level,
        static_cast<std::int32_t>(cog), static_cast<std::int32_t>(parameter),
        symbol, parseSymbolKind(kindText), static_cast<std::int32_t>(target),
        schema ? &*schema : nullptr, expected);
    if (!prepared.ok()) failCogRefusal(*prepared.failure);
    if (!prepared.apply(level)) fail(kExitConflict, "rebindCogReference prepared step is stale for this document");
}

// setCogValue: change one typed value (flex, float, int, sound, ...) of a
// placed COG, e.g. a door's sleep time, without rebuilding the level.
void applySetCogValueOp(sed::Level& level, const Json& op, const Json& request)
{
    for (const auto& [key, value] : op.object)
    {
        (void)value;
        if (key != "op" && key != "cog" && key != "symbol" && key != "value" && key != "default"
            && key != "scriptSha256" && key != "expectedScriptSha256" && key != "resourceArchives")
            fail(kExitUsage, "Unknown field '" + key + "' in setCogValue");
    }
    const long long cog = requiredInt(op, "cog", "setCogValue operation");
    const std::string symbol = requiredString(op, "symbol", "setCogValue operation");
    std::optional<std::string> valueText;
    bool useDefault = false;
    if (const Json* value = op.find("value"))
    {
        if (value->type == Json::Type::String) valueText = value->str;
        else if (value->type == Json::Type::Number && std::isfinite(value->number))
            valueText = cogNumberText(value->number);
        else fail(kExitUsage, "setCogValue 'value' must be a string or a finite number");
    }
    if (const Json* flag = op.find("default"))
    {
        if (flag->type != Json::Type::Bool || !flag->boolean)
            fail(kExitUsage, "setCogValue 'default' must be true when given");
        useDefault = true;
    }
    if (valueText.has_value() == useDefault)
        fail(kExitUsage, "setCogValue requires exactly one of 'value' or 'default': true");
    if (cog < 0 || static_cast<std::size_t>(cog) >= level.cogs.size())
        failCoded(kExitRefusal, "cogParameterOutOfRange", "cogParameterOutOfRange: COG index is outside the placed COG list");
    const CogArchiveSet set = [&] {
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    const std::string& scriptName = level.cogs[static_cast<std::size_t>(cog)].name;
    auto schema = resolveCommandCogSchema(scriptName, set);
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    const auto prepared = sed::level::prepareSetCogValue(level, static_cast<std::int32_t>(cog), symbol,
        valueText, useDefault, schema ? &*schema : nullptr, expected);
    if (!prepared.ok()) failCogRefusal(*prepared.failure);
    if (!prepared.apply(level)) fail(kExitConflict, "setCogValue prepared step is stale for this document");
}

double numberField(const Json& obj, const char* key, int opIndex)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::Number)
        fail(kExitUsage, "operation " + std::to_string(opIndex) + " requires numeric '" + key + "'");
    return v->number;
}

float checkedFloat(const double value, const char* field)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        fail(kExitUsage, std::string(field) + " must fit a finite stored float");
    return static_cast<float>(value);
}

std::int32_t checkedInt32(const double value, const char* field)
{
    if (!std::isfinite(value) || value != std::floor(value)
        || value < std::numeric_limits<std::int32_t>::min()
        || value > std::numeric_limits<std::int32_t>::max())
        fail(kExitUsage, std::string(field) + " must be a signed 32-bit integer");
    return static_cast<std::int32_t>(value);
}

std::int32_t int32Field(const Json& obj, const char* key, int opIndex)
{
    return checkedInt32(numberField(obj, key, opIndex), key);
}

std::uint32_t checkedUint32(const double value, const char* field)
{
    if (!std::isfinite(value) || value != std::floor(value) || value < 0
        || value > std::numeric_limits<std::uint32_t>::max())
        fail(kExitUsage, std::string(field) + " must be an unsigned 32-bit integer");
    return static_cast<std::uint32_t>(value);
}

long long intField(const Json& obj, const char* key, int opIndex)
{
    const double v = numberField(obj, key, opIndex);
    // LLONG_MAX rounds up when represented as double. Use an exclusive
    // power-of-two upper bound so conversion never enters undefined range.
    if (!std::isfinite(v) || v != std::floor(v) || v < -0x1p63 || v >= 0x1p63)
        fail(kExitUsage, "operation requires a representable integer '" + std::string(key) + "'");
    return static_cast<long long>(v);
}

sed::TVector pointField(const Json& obj, const char* key);

std::string stringField(const Json& obj, const char* key, int opIndex)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::String)
        fail(kExitUsage, "operation " + std::to_string(opIndex) + " requires string '" + key + "'");
    return v->str;
}

sed::level::WorkbenchUvPoint uvPointField(const Json& obj, const char* key, int opIndex)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::Array || v->array.size() != 2
        || v->array[0].type != Json::Type::Number || v->array[1].type != Json::Type::Number)
        fail(kExitUsage, "operation " + std::to_string(opIndex) + " requires '" + key + "' as [u, v]");
    return sed::level::WorkbenchUvPoint{ v->array[0].number, v->array[1].number };
}

std::vector<sed::SurfaceRef> surfacesField(const Json& obj, const char* key, int opIndex)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::Array || v->array.empty())
        fail(kExitUsage, "operation " + std::to_string(opIndex) + " requires a non-empty '" + key + "' array");
    std::vector<sed::SurfaceRef> out{};
    for (const Json& row : v->array)
    {
        if (row.type != Json::Type::Array || row.array.size() != 2
            || row.array[0].type != Json::Type::Number || row.array[1].type != Json::Type::Number)
            fail(kExitUsage, "surfaces entries must be [sector, surface]");
        out.push_back(sed::SurfaceRef{ checkedInt32(row.array[0].number, "sector"),
                                       checkedInt32(row.array[1].number, "surface") });
    }
    return out;
}

sed::level::PlacementLayout layoutField(const Json& obj, int opIndex)
{
    sed::level::PlacementLayout layout{};
    if (const Json* v = obj.find("kind"))
    {
        if (v->type != Json::Type::String) fail(kExitUsage, "layout kind must be a string");
        if (v->str == "linear") layout.kind = sed::level::PlacementLayoutKind::linear;
        else if (v->str == "grid") layout.kind = sed::level::PlacementLayoutKind::grid;
        else if (v->str == "radial") layout.kind = sed::level::PlacementLayoutKind::radial;
        else fail(kExitUsage, "layout kind must be linear, grid or radial");
    }
    auto point = [&](const char* key, sed::TVector& target) {
        if (obj.find(key)) target = pointField(obj, key);
    };
    auto num = [&](const char* key, double& target) {
        if (const Json* v = obj.find(key))
        {
            if (v->type != Json::Type::Number) fail(kExitUsage, std::string("layout '") + key + "' must be a number");
            target = v->number;
        }
    };
    auto count = [&](const char* key, std::int32_t& target) {
        if (obj.find(key)) target = int32Field(obj, key, opIndex);
    };
    point("origin", layout.origin); point("step", layout.step);
    point("stepU", layout.stepU); point("stepV", layout.stepV); point("axis", layout.axis);
    count("count", layout.count); count("countU", layout.countU); count("countV", layout.countV);
    num("radius", layout.radius); num("startDegrees", layout.startDegrees); num("sweepDegrees", layout.sweepDegrees);
    return layout;
}

std::optional<sed::level::FlagMaskPredicate> maskField(const Json& obj, const char* key)
{
    const Json* v = obj.find(key);
    if (!v) return std::nullopt;
    if (!v->isObject()) fail(kExitUsage, std::string("'") + key + "' must be an object {any,all,none}");
    sed::level::FlagMaskPredicate mask{};
    auto part = [&](const char* name, std::optional<std::uint32_t>& target) {
        if (const Json* p = v->find(name))
        {
            if (p->type != Json::Type::Number) fail(kExitUsage, std::string("mask '") + name + "' must be a number");
            target = checkedUint32(p->number, name);
        }
    };
    part("any", mask.any); part("all", mask.all); part("none", mask.none);
    return mask;
}

// A caller-supplied advisory, not a game profile or a geometry constraint.
// Keep this outside the core builders: it describes recipe parameters only.
std::optional<double> stairWalkabilityLimit(const Json& op, const int opIndex)
{
    const Json* policy = op.find("walkability");
    if ( !policy )
    {
        return std::nullopt;
    }
    if ( !policy->isObject() )
    {
        fail(kExitUsage, "stair walkability must be an object with maxStepRise");
    }
    for ( const auto& [key, value] : policy->object )
    {
        (void)value;
        if ( key != "maxStepRise" )
        {
            fail(kExitUsage, "Unknown field in stair walkability; only maxStepRise is supported");
        }
    }
    const double maximum = numberField(*policy, "maxStepRise", opIndex);
    if ( !std::isfinite(maximum) || maximum <= 0.0 || maximum > 100.0 )
    {
        fail(kExitUsage, "stair walkability maxStepRise must be finite and in (0,100]");
    }
    return maximum;
}

Json stairWalkabilityReport(const std::string& operation, const std::size_t operationIndex,
    const double requestedRise, const std::int32_t internalRisers, const double maximum)
{
    const double comparedRise = internalRisers > 0 ? requestedRise : 0.0;
    const bool exceeds = comparedRise > maximum;
    Json report = Json::makeObject();
    report.set("operationIndex", Json::makeNumber(static_cast<double>(operationIndex)));
    report.set("operation", Json::makeString(operation));
    report.set("scope", Json::makeString("recipe-internal-risers-at-creation"));
    report.set("measurementBasis", Json::makeString("recipe-parameters"));
    report.set("requestedRise", Json::makeNumber(requestedRise));
    report.set("comparedRise", Json::makeNumber(comparedRise));
    report.set("internalRiserCount", Json::makeNumber(internalRisers));
    report.set("maxStepRise", Json::makeNumber(maximum));
    report.set("exceedsConfiguredLimit", Json::makeBool(exceeds));
    report.set("gameplayVerified", Json::makeBool(false));
    Json warnings = Json::makeArray();
    if ( exceeds )
    {
        Json warning = Json::makeObject();
        warning.set("code", Json::makeString("stairRiseExceedsConfiguredLimit"));
        warning.set("severity", Json::makeString("warning"));
        warning.set("message", Json::makeString(
            "Requested internal stair rise exceeds caller-supplied maxStepRise. "
            "Geometry was not clamped; test movement in the target game."));
        warnings.array.push_back(std::move(warning));
    }
    report.set("warnings", std::move(warnings));
    return report;
}

// Applies the supported edit operations to an isolated copy. Numeric
// selectors are indices into the document they were read from; callers
// bind them with inputSha256/sourceRevision (checked before this runs).
sed::Level applyOperations(const OpenedDocument& doc, const Json& request, Json& bakeReports,
    Json& templateReports, Json& seamReports, Json& connectReports, Json& stairReports, Json& deleteReports,
    Json& carveReports, Json& mirrorReports, Json& editReports)
{
    const Json* ops = request.find("operations");
    if (!ops || ops->type != Json::Type::Array || ops->array.empty())
        fail(kExitUsage, "request requires a non-empty 'operations' array");
    if (ops->array.size() > 1000) fail(kExitUsage, "too many operations (max 1000)");
    sed::Level level = doc.level;
    std::size_t patchOutputs = 0;
    std::size_t remainingLightDiagnostics = 256;
    bool appendIdentityChecked = false;
    ArchOperationBudget archBudget;
    std::optional<sed::assets::ResourceLocator> archResources;
    std::shared_ptr<const LoadedTemplateSource> archTemplates;
    const ArchModelResolver archResolver = [&](const std::string_view modelName) -> std::optional<ArchModelResource>
    {
        if ( !request.find("resourceArchives") )
        {
            return std::nullopt;
        }
        if ( !archResources )
        {
            archResources = exportResourceLocator(request);
        }
        bool exceededBudget{};
        // Reuse the shared candidate order (including explicit 3do/ names),
        // with the size limit enforced before copying an archive payload.
        const auto bytes = archResources->tryReadBinary(modelName,
            sed::assets::ModelLightingLimits{}.sourceBytes, exceededBudget);
        if ( exceededBudget )
        {
            failCoded(kExitRefusal, "archModelBudgetExceeded", "ARCH model exceeds the 16 MiB source limit");
        }
        if ( !bytes ) return std::nullopt;
        std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        const auto digest = sha256Hex(text);
        return ArchModelResource{ std::move(text), digest };
    };
    std::optional<sed::level::CogSchemaResolver> placementSchemas;
    bool placementSchemasLoaded = false;
    const auto placementResolver = [&]() -> const std::optional<sed::level::CogSchemaResolver>& {
        if (!placementSchemasLoaded)
        {
            if (request.find("resourceArchives")) placementSchemas = cogResolver(loadCogArchives(request));
            placementSchemasLoaded = true;
        }
        return placementSchemas;
    };
    for (std::size_t i = 0; i < ops->array.size(); ++i)
    {
        const Json& op = ops->array[i];
        if (!op.isObject()) fail(kExitUsage, "operation " + std::to_string(i) + " must be an object");
        const Json* name = op.find("op");
        if (!name || name->type != Json::Type::String)
            fail(kExitUsage, "operation " + std::to_string(i) + " requires string 'op'");
        const int idx = static_cast<int>(i);
        constexpr std::string_view geometryRecipes[] = { "buildSweptCorridor", "buildDome",
            "buildSectorArray", "buildSpiralStairs", "buildStairs", "buildSwitchbackStairs",
            "buildHeightmapTerrain", "insertPrefab" };
        std::optional<sed::Level> geometryBefore;
        if ( std::find(std::begin(geometryRecipes), std::end(geometryRecipes),
                name->str) != std::end(geometryRecipes) )
        {
            validateGeometryAppendSource(level);
            if ( !level.cogs.empty() )
            {
                geometryBefore = level;
            }
        }
        constexpr std::string_view allocatingOperations[] = { "addBox", "extrudeSurface", "addThing",
            "addLight", "placeThingArray", "placeLightArray", "buildSweptCorridor", "buildDome",
            "buildSectorArray", "buildSpiralStairs", "buildStairs", "buildSwitchbackStairs",
            "buildHeightmapTerrain", "insertPrefab", "addCog", "makeDoor", "doorAssistant", "elevatorAssistant", "switchAssistant", "objectivesAssistant",
            "cutsceneAssistant", "pursuitAssistant", "cleaveSector", "mirrorFloor", "placeSectorLights",
            "duplicateSector" };
        if (!appendIdentityChecked && std::find(std::begin(allocatingOperations),
                std::end(allocatingOperations), name->str) != std::end(allocatingOperations))
        {
            validateAppendIdentitySource(level);
            appendIdentityChecked = true;
        }
        if ( name->str == "bindThingArchitecture" || name->str == "rebindThingArchitecture"
            || name->str == "bakeThingArchitecture" )
        {
            if ( const auto* source = request.find("templateSource"); source && !archTemplates )
            {
                archTemplates = loadTemplateSource(*source);
            }
            (void)tryApplyArchOperation(level, op, archResolver,
                archTemplates ? &archTemplates->database : nullptr, &archBudget);
            continue;
        }
        if ( tryApplyEditOperation(level, op) )
        {
            continue;
        }
        if ( name->str == "conformSeams" )
        {
            Json report = applyConformSeams(level, op);
            report.set("operation", Json::makeNumber(static_cast<double>(i)));
            seamReports.array.push_back(std::move(report));
            continue;
        }
        if ( name->str == "connectSurfaces" )
        {
            Json report = applyConnectSurfaces(level, op);
            report.set("operation", Json::makeNumber(static_cast<double>(i)));
            connectReports.array.push_back(std::move(report));
            continue;
        }
        if ( name->str == "carveSurfaceRect" )
        {
            Json report = applyCarveSurfaceRect(level, op);
            report.set("operation", Json::makeNumber(static_cast<double>(i)));
            carveReports.array.push_back(std::move(report));
            continue;
        }
        {
            Json report;
            if ( tryApplyParityOperation(level, op, report) )
            {
                report.set("operation", Json::makeNumber(static_cast<double>(i)));
                editReports.array.push_back(std::move(report));
                continue;
            }
        }
        if ( name->str == "mirrorFloor" )
        {
            Json report = applyMirrorFloor(level, op);
            report.set("operation", Json::makeNumber(static_cast<double>(i)));
            mirrorReports.array.push_back(std::move(report));
            continue;
        }
        if ( name->str == "deleteSectors" )
        {
            Json report = applyDeleteSectors(level, op);
            report.set("operation", Json::makeNumber(static_cast<double>(i)));
            deleteReports.array.push_back(std::move(report));
            continue;
        }
        Json basicReport;
        if (tryApplyBasicOperation(level, op, basicReport))
        {
            if (basicReport.isObject())
            {
                basicReport.set("operation", Json::makeNumber(static_cast<double>(i)));
                editReports.array.push_back(std::move(basicReport));
            }
            continue;
        }
        const bool appendsEntity = name->str == "addThing" || name->str == "addLight";
        if (appendsEntity && patchOutputs >= 1024)
            failCoded(kExitRefusal, "placementPatchLimit", "Entity creation exceeds the 1024-per-patch budget");
        if (tryApplyEntityOperation(level, op,
                name->str == "addThing" ? placementResolver() : std::nullopt))
        {
            if (appendsEntity) ++patchOutputs;
            continue;
        }
        if (name->str == "setGravity")
        {
            level.header.gravity = numberField(op, "value", idx);
        }
        else if (name->str == "setLevelHeader")
        {
            // The JKL HEADER values that the engines read for sky placement
            // and for texture, model and shading detail by distance. Every
            // field is optional; omitted fields keep their stored values.
            bool any = false;
            const auto scalar = [&](const char* key, double& target, const double minimum, const double maximum)
            {
                if (!op.find(key)) return;
                const double value = numberField(op, key, idx);
                if (!std::isfinite(value) || value < minimum || value > maximum)
                    fail(kExitUsage, std::string("setLevelHeader '") + key + "' is outside its supported range");
                target = value;
                any = true;
            };
            const auto pair = [&](const char* key, sed::TPoint2& target)
            {
                const Json* value = op.find(key);
                if (!value) return;
                if (value->type != Json::Type::Array || value->array.size() != 2
                    || value->array[0].type != Json::Type::Number || value->array[1].type != Json::Type::Number
                    || !std::isfinite(value->array[0].number) || !std::isfinite(value->array[1].number)
                    || std::abs(value->array[0].number) > 1000000.0 || std::abs(value->array[1].number) > 1000000.0)
                    fail(kExitUsage, std::string("setLevelHeader '") + key + "' must be two finite numbers");
                target.x = value->array[0].number;
                target.y = value->array[1].number;
                any = true;
            };
            const auto ladder = [&](const char* key, std::array<double, 4>& target)
            {
                const Json* value = op.find(key);
                if (!value) return;
                if (value->type != Json::Type::Array || value->array.size() != 4)
                    fail(kExitUsage, std::string("setLevelHeader '") + key + "' must be four numbers");
                std::array<double, 4> parsed{};
                for (std::size_t n = 0; n < 4; ++n)
                {
                    const Json& entry = value->array[n];
                    if (entry.type != Json::Type::Number || !std::isfinite(entry.number)
                        || entry.number < 0.0 || entry.number > 1000000.0
                        || (n > 0 && entry.number < parsed[n - 1]))
                        fail(kExitUsage, std::string("setLevelHeader '") + key
                            + "' must be four finite non-decreasing distances in 0..1000000");
                    parsed[n] = entry.number;
                }
                target = parsed;
                any = true;
            };
            for (const auto& [key, value] : op.object)
            {
                (void)value;
                constexpr std::string_view allowed[] = { "op", "gravity", "ceilingSkyHeight", "ceilingSkyOffset",
                    "horizonDistance", "horizonPixelsPerRev", "horizonSkyOffset", "mipmapDistances",
                    "lodDistances", "perspectiveDistance", "gouraudDistance" };
                if (std::find(std::begin(allowed), std::end(allowed), key) == std::end(allowed))
                    fail(kExitUsage, "Unknown field '" + key + "' in setLevelHeader");
            }
            scalar("gravity", level.header.gravity, -1000.0, 1000.0);
            scalar("ceilingSkyHeight", level.header.ceilingSky.height, -1000000.0, 1000000.0);
            pair("ceilingSkyOffset", level.header.ceilingSky.offset);
            scalar("horizonDistance", level.header.horizonSky.distance, 0.0, 1000000.0);
            scalar("horizonPixelsPerRev", level.header.horizonSky.pixelsPerRev, 0.0, 1000000.0);
            pair("horizonSkyOffset", level.header.horizonSky.offset);
            ladder("mipmapDistances", level.header.mipmapDistances);
            ladder("lodDistances", level.header.lodDistances);
            scalar("perspectiveDistance", level.header.perspectiveDistance, 0.0, 1000000.0);
            scalar("gouraudDistance", level.header.gouraudDistance, 0.0, 1000000.0);
            if (!any) fail(kExitUsage, "setLevelHeader requires at least one header field");
        }
        else if (name->str == "setThingFrames" || name->str == "buildThingPath")
        {
            // E-09: buildThingPath is the Pathfinder spelling of the same
            // validated frame replacement; both share prepareSetThingFrames.
            const bool isPathRecipe = name->str == "buildThingPath";
            const long long thing = intField(op, "thing", idx);
            const Json* rows = op.find(isPathRecipe ? "waypoints" : "frames");
            if (!rows && isPathRecipe) rows = op.find("frames");
            if (!rows || rows->type != Json::Type::Array || rows->array.empty()
                || rows->array.size() > sed::level::kMaxThingFrames)
            {
                fail(kExitUsage, "setThingFrames frames must contain 1 through 256 absolute position/orientation pairs");
            }
            std::vector<sed::FrameValue> frames{};
            frames.reserve(rows->array.size());
            for (const Json& row : rows->array)
            {
                if (!row.isObject()) fail(kExitUsage, "setThingFrames frame entries must be objects");
                const sed::TVector position = pointField(row, "position");
                const sed::TVector orientation = pointField(row, "orientation");
                frames.push_back(sed::FrameValue{ position.x, position.y, position.z,
                    orientation.x, orientation.y, orientation.z });
            }
            auto prepared = sed::level::prepareSetThingFrames(level, nullptr,
                static_cast<std::size_t>(thing), frames);
            if (!prepared.ok())
                failCoded(kExitRefusal, prepared.failure->code,
                    prepared.failure->code + ": " + prepared.failure->message);
            if (level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS)
                failCoded(kExitRefusal, "thingFrameProjectUnsupported",
                    "thingFrameProjectUnsupported: setThingFrames supports JK and MotS path movers");
            if (!prepared.pathMove)
                failCoded(kExitRefusal, "thingFrameRequiresPath",
                    "thingFrameRequiresPath: setThingFrames requires an explicit or inherited move=path Thing");
            if (level.things[static_cast<std::size_t>(thing)].archLightingIndex >= 0)
                failCoded(kExitRefusal, "thingFrameArchBound",
                    "thingFrameArchBound: clear the architectural-lighting binding before authoring a moving path");
            if (!prepared.apply(level))
                failCoded(kExitConflict, "staleThingFrames",
                    "staleThingFrames: prepared Thing frames no longer match the document");
        }
        else if (name->str == "setSectorAmbient")
        {
            const long long sector = intField(op, "sector", idx);
            if (sector < 0 || static_cast<std::size_t>(sector) >= level.sectors.size())
                fail(kExitInput, "sector selector out of range for this input");
            const double v = numberField(op, "value", idx);
            auto& s = level.sectors[static_cast<std::size_t>(sector)];
            const float color = checkedFloat(v, "ambient");
            s.ambient = sed::ColorF{ color, color, color, 1.0f };
        }
        else if (name->str == "setSectorFlags")
        {
            const long long sector = intField(op, "sector", idx);
            if (sector < 0 || static_cast<std::size_t>(sector) >= level.sectors.size())
                fail(kExitInput, "sector selector out of range for this input");
            if (op.find("flags") && op.find("value"))
                fail(kExitUsage, "setSectorFlags accepts flags or value, not both");
            const auto flags = intField(op, op.find("flags") ? "flags" : "value", idx);
            if (flags < std::numeric_limits<std::int32_t>::min()
                || flags > std::numeric_limits<std::uint32_t>::max())
                fail(kExitUsage, "sector flags must fit 32 bits");
            level.sectors[static_cast<std::size_t>(sector)].flags =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(flags));
        }
        else if (name->str == "setLightRgb")
        {
            const long long lightIndex = intField(op, "light", idx);
            if (lightIndex < 0 || static_cast<std::size_t>(lightIndex) >= level.lights.size())
                fail(kExitInput, "light selector out of range for this input");
            const Json* rgb = op.find("rgbIntensity");
            const Json* legacy = op.find("legacyRgbRange");
            if (!rgb && !legacy)
                fail(kExitUsage, "setLightRgb requires rgbIntensity and/or legacyRgbRange");
            auto applyField = [&](const Json* v, std::optional<double>& target, const char* key) {
                if (!v) return;
                if (v->type == Json::Type::Null) { target.reset(); return; }
                if (v->type != Json::Type::Number || !std::isfinite(v->number))
                    fail(kExitUsage, std::string("setLightRgb '") + key + "' must be a finite number or null");
                target = v->number;
            };
            auto& light = level.lights[static_cast<std::size_t>(lightIndex)];
            applyField(rgb, light.rgbIntensity, "rgbIntensity");
            applyField(legacy, light.legacyRgbRange, "legacyRgbRange");
        }
        else if (name->str == "batchUvCylindrical")
        {
            // E-08: cylindrical UV unwrap around an arbitrary axis, with
            // the planar projection's preflight/staleness contract.
            const auto surfaces = surfacesField(op, "surfaces", idx);
            auto prepared = sed::level::prepareCylindricalSurfaceUv(level, surfaces,
                pointField(op, "axisOrigin"), pointField(op, "axisDirection"),
                numberField(op, "texelsPerUnit", idx));
            if (!prepared.ok())
                failCoded(kExitRefusal, prepared.failure->code,
                    prepared.failure->code + ": " + prepared.failure->message);
            if (!prepared.apply(level))
                failCoded(kExitRefusal, "staleWorkbenchPlan",
                    "staleWorkbenchPlan: prepared UV edit no longer matches the document");
        }
        else if (name->str == "batchUvSpherical")
        {
            // E-08: spherical UV unwrap around a centre with a pole axis,
            // with the cylindrical projection's preflight contract.
            const auto surfaces = surfacesField(op, "surfaces", idx);
            auto prepared = sed::level::prepareSphericalSurfaceUv(level, surfaces,
                pointField(op, "center"), pointField(op, "poleDirection"),
                numberField(op, "texelsPerUnit", idx));
            if (!prepared.ok())
                failCoded(kExitRefusal, prepared.failure->code,
                    prepared.failure->code + ": " + prepared.failure->message);
            if (!prepared.apply(level))
                failCoded(kExitRefusal, "staleWorkbenchPlan",
                    "staleWorkbenchPlan: prepared UV edit no longer matches the document");
        }
        else if (name->str == "batchUvProject" || name->str == "batchUvTransform")
        {
            const auto surfaces = surfacesField(op, "surfaces", idx);
            sed::level::PreparedBatchUv prepared;
            if (name->str == "batchUvProject")
            {
                const std::string direction = stringField(op, "direction", idx);
                sed::level::UvProjectionDirection dir{};
                if (direction == "+x") dir = sed::level::UvProjectionDirection::positiveX;
                else if (direction == "-x") dir = sed::level::UvProjectionDirection::negativeX;
                else if (direction == "+y") dir = sed::level::UvProjectionDirection::positiveY;
                else if (direction == "-y") dir = sed::level::UvProjectionDirection::negativeY;
                else if (direction == "+z") dir = sed::level::UvProjectionDirection::positiveZ;
                else if (direction == "-z") dir = sed::level::UvProjectionDirection::negativeZ;
                else fail(kExitUsage, "batchUvProject direction must be one of +x,-x,+y,-y,+z,-z");
                prepared = sed::level::prepareProjectSurfaceUv(level, surfaces, dir,
                    pointField(op, "origin"), numberField(op, "texelsPerUnit", idx));
            }
            else
            {
                double rotation = 0.0;
                if (const Json* v = op.find("rotationDegrees"))
                {
                    if (v->type != Json::Type::Number) fail(kExitUsage, "rotationDegrees must be a number");
                    rotation = v->number;
                }
                sed::level::WorkbenchUvPoint offset{};
                if (op.find("offset")) offset = uvPointField(op, "offset", idx);
                prepared = sed::level::prepareTransformSurfaceUv(level, surfaces,
                    uvPointField(op, "pivot", idx), uvPointField(op, "scale", idx), rotation, offset);
            }
            if (!prepared.ok())
                failCoded(kExitRefusal, prepared.failure->code,
                    prepared.failure->code + ": " + prepared.failure->message);
            if (!prepared.apply(level))
                failCoded(kExitRefusal, "staleWorkbenchPlan",
                    "staleWorkbenchPlan: prepared UV edit no longer matches the document");
        }
        else if (name->str == "placeThingArray" || name->str == "placeLightArray")
        {
            const Json* layoutJson = op.find("layout");
            if (!layoutJson || !layoutJson->isObject())
                fail(kExitUsage, "placement operation requires a 'layout' object");
            const auto positions = sed::level::computePlacementPositions(layoutField(*layoutJson, idx));
            if (!positions.ok())
                failCoded(kExitRefusal, positions.failure->code,
                    positions.failure->code + ": " + positions.failure->message);
            if (name->str == "placeThingArray")
            {
                validateEntityAppendSource(level);
                const std::string templateName = stringField(op, "template", idx);
                const std::string namePrefix = stringField(op, "namePrefix", idx);
                sed::TVector orientation{};
                if (op.find("orientation")) orientation = pointField(op, "orientation");
                std::int32_t layer = 0;
                if (op.find("layer")) layer = int32Field(op, "layer", idx);
                std::optional<std::int32_t> sector{};
                if (op.find("sector")) sector = int32Field(op, "sector", idx);
                auto prepared = sed::level::prepareThingArray(level, positions.positions,
                    templateName, namePrefix, orientation, layer, sector, patchOutputs, placementResolver());
                if (!prepared.ok())
                    failCoded(kExitRefusal, prepared.failure->code,
                        prepared.failure->code + ": " + prepared.failure->message);
                if (!prepared.apply(level))
                    failCoded(kExitRefusal, "staleWorkbenchPlan",
                        "staleWorkbenchPlan: prepared thing array no longer matches the document");
                patchOutputs += prepared.positions.size();
            }
            else
            {
                sed::ColorF color = sed::ColorWhite;
                if (const Json* v = op.find("color"))
                {
                    if (v->type != Json::Type::Array || (v->array.size() != 3 && v->array.size() != 4))
                        fail(kExitUsage, "color must be [r,g,b] or [r,g,b,a]");
                    for (const Json& c : v->array)
                        if (c.type != Json::Type::Number) fail(kExitUsage, "color components must be numbers");
                    color = sed::ColorF{ checkedFloat(v->array[0].number, "color"),
                        checkedFloat(v->array[1].number, "color"), checkedFloat(v->array[2].number, "color"),
                        v->array.size() == 4 ? checkedFloat(v->array[3].number, "color") : 1.0f };
                }
                double intensity = 10.0, range = 2.0;
                std::int32_t flags = 0, layer = 0;
                if (op.find("intensity")) intensity = numberField(op, "intensity", idx);
                if (op.find("range")) range = numberField(op, "range", idx);
                if (op.find("flags")) flags = static_cast<std::int32_t>(checkedUint32(numberField(op, "flags", idx), "flags"));
                if (op.find("layer")) layer = int32Field(op, "layer", idx);
                std::optional<double> rgbIntensity;
                if ( const Json* rgb = op.find("rgbIntensity"); rgb && rgb->type != Json::Type::Null )
                {
                    rgbIntensity = numberField(op, "rgbIntensity", idx);
                }
                auto prepared = sed::level::prepareLightArray(level, positions.positions,
                    color, intensity, range, flags, layer, patchOutputs, rgbIntensity);
                if (!prepared.ok())
                    failCoded(kExitRefusal, prepared.failure->code,
                        prepared.failure->code + ": " + prepared.failure->message);
                if (!prepared.apply(level))
                    failCoded(kExitRefusal, "staleWorkbenchPlan",
                        "staleWorkbenchPlan: prepared light array no longer matches the document");
                patchOutputs += prepared.positions.size();
            }
        }
        else if (name->str == "buildSweptCorridor")
        {
            // E-05: swept bent corridor/ramp of adjoined wedge segments,
            // validated by the shared core domain before the build.
            sed::level::SweptCorridorParameters parameters{};
            parameters.center = pointField(op, "center");
            parameters.innerRadius = numberField(op, "innerRadius", idx);
            parameters.outerRadius = numberField(op, "outerRadius", idx);
            parameters.startAngleDegrees = numberField(op, "startAngleDegrees", idx);
            parameters.sweepDegrees = numberField(op, "sweepDegrees", idx);
            parameters.segments = int32Field(op, "segments", idx);
            parameters.risePerSegment = numberField(op, "risePerSegment", idx);
            parameters.headroom = numberField(op, "headroom", idx);
            if (const auto refusal = sed::level::validateSweptCorridorParameters(parameters))
                failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
            if (!sed::level::levelBuildSweptCorridor(level, parameters))
                failCoded(kExitRefusal, "invalidSweptCorridorParameters",
                    "invalidSweptCorridorParameters: the corridor could not be built with these parameters");
        }
        else if (name->str == "buildDome")
        {
            // E-05: stepped-hemisphere dome of adjoined wedge rings,
            // validated by the shared core domain before the build.
            sed::level::DomeParameters parameters{};
            parameters.center = pointField(op, "center");
            parameters.radius = numberField(op, "radius", idx);
            parameters.headroom = numberField(op, "headroom", idx);
            parameters.segments = int32Field(op, "segments", idx);
            parameters.rings = int32Field(op, "rings", idx);
            if (const auto refusal = sed::level::validateDomeParameters(parameters))
                failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
            if (!sed::level::levelBuildDome(level, parameters))
                failCoded(kExitRefusal, "invalidDomeParameters",
                    "invalidDomeParameters: the dome could not be built with these parameters");
        }
        else if (name->str == "buildSectorArray")
        {
            // E-03: geometry array â€” one prefab copy of the named sectors
            // per placement-layout position, failure-atomic in core.
            const Json* sectorsJson = op.find("sectors");
            if (!sectorsJson || sectorsJson->type != Json::Type::Array || sectorsJson->array.empty())
                fail(kExitUsage, "buildSectorArray requires a non-empty 'sectors' array");
            std::vector<std::int32_t> sectors{};
            for (const Json& value : sectorsJson->array)
            {
                if (value.type != Json::Type::Number || value.number != std::floor(value.number))
                    fail(kExitUsage, "buildSectorArray 'sectors' entries must be integers");
                sectors.push_back(checkedInt32(value.number, "sector"));
            }
            const Json* layoutJson = op.find("layout");
            if (!layoutJson || !layoutJson->isObject())
                fail(kExitUsage, "buildSectorArray requires a 'layout' object");
            const auto layout = layoutField(*layoutJson, idx);
            const auto positions = sed::level::computePlacementPositions(layout);
            if (!positions.ok())
                failCoded(kExitRefusal, positions.failure->code,
                    positions.failure->code + ": " + positions.failure->message);
            if (!sed::level::levelBuildSectorArray(level, sectors, layout))
                failCoded(kExitRefusal, "sectorArrayRefused",
                    "sectorArrayRefused: the sector selection cannot be arrayed with this layout");
        }
        else if (name->str == "buildSpiralStairs")
        {
            // E-05: one adjoined wedge sector per spiral step, validated
            // by the shared core domain before the failure-atomic build.
            sed::level::SpiralStairParameters parameters{};
            parameters.center = pointField(op, "center");
            parameters.innerRadius = numberField(op, "innerRadius", idx);
            parameters.outerRadius = numberField(op, "outerRadius", idx);
            parameters.startAngleDegrees = numberField(op, "startAngleDegrees", idx);
            parameters.stepAngleDegrees = numberField(op, "stepAngleDegrees", idx);
            parameters.rise = numberField(op, "rise", idx);
            parameters.headroom = numberField(op, "headroom", idx);
            parameters.steps = int32Field(op, "steps", idx);
            if (const auto refusal = sed::level::validateSpiralStairParameters(parameters))
                failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
            if (!sed::level::levelBuildSpiralStairs(level, parameters))
                failCoded(kExitRefusal, "invalidSpiralStairParameters",
                    "invalidSpiralStairParameters: spiral stairs could not be built with these parameters");
        }
        else if (name->str == "buildStairs" || name->str == "buildSwitchbackStairs")
        {
            // E-05: the straight and landing flights the editor's Build
            // Stairs commands produce, through the same core builders.
            const bool switchback = name->str == "buildSwitchbackStairs";
            for (const auto& [key, value] : op.object)
            {
                (void)value;
                const bool common = key == "op" || key == "origin" || key == "direction" || key == "width"
                    || key == "run" || key == "rise" || key == "headroom" || key == "walkability";
                const bool own = switchback ? (key == "landingRun" || key == "stepsPerFlight") : key == "steps";
                if (!common && !own)
                    fail(kExitUsage, "Unknown field '" + key + "' in " + name->str
                        + (switchback && key == "steps" ? " (a switchback counts steps per flight: stepsPerFlight)" : ""));
            }
            const auto walkabilityLimit = stairWalkabilityLimit(op, idx);
            const std::string directionText = stringField(op, "direction", idx);
            sed::level::StairDirection direction{};
            if (directionText == "+x") direction = sed::level::StairDirection::PositiveX;
            else if (directionText == "-x") direction = sed::level::StairDirection::NegativeX;
            else if (directionText == "+y") direction = sed::level::StairDirection::PositiveY;
            else if (directionText == "-y") direction = sed::level::StairDirection::NegativeY;
            else fail(kExitUsage, name->str + " 'direction' must be \"+x\", \"-x\", \"+y\" or \"-y\"");
            if (switchback)
            {
                sed::level::SwitchbackStairParameters parameters{};
                parameters.origin = pointField(op, "origin");
                parameters.direction = direction;
                parameters.width = numberField(op, "width", idx);
                parameters.run = numberField(op, "run", idx);
                parameters.rise = numberField(op, "rise", idx);
                parameters.headroom = numberField(op, "headroom", idx);
                parameters.landingRun = numberField(op, "landingRun", idx);
                parameters.stepsPerFlight = int32Field(op, "stepsPerFlight", idx);
                if (const auto refusal = sed::level::validateSwitchbackStairParameters(parameters))
                    failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
                if (!sed::level::levelBuildSwitchbackStairs(level, parameters))
                    failCoded(kExitRefusal, "invalidSwitchbackStairParameters",
                        "invalidSwitchbackStairParameters: switchback stairs could not be built with these parameters");
                if ( walkabilityLimit )
                {
                    stairReports.array.push_back(stairWalkabilityReport(name->str, i,
                        parameters.rise, parameters.stepsPerFlight * 2, *walkabilityLimit));
                }
            }
            else
            {
                sed::level::StairParameters parameters{};
                parameters.origin = pointField(op, "origin");
                parameters.direction = direction;
                parameters.width = numberField(op, "width", idx);
                parameters.run = numberField(op, "run", idx);
                parameters.rise = numberField(op, "rise", idx);
                parameters.headroom = numberField(op, "headroom", idx);
                parameters.steps = int32Field(op, "steps", idx);
                if (const auto refusal = sed::level::validateStairParameters(parameters))
                    failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
                if (!sed::level::levelBuildStairs(level, parameters))
                    failCoded(kExitRefusal, "invalidStairParameters",
                        "invalidStairParameters: stairs could not be built with these parameters");
                if ( walkabilityLimit )
                {
                    stairReports.array.push_back(stairWalkabilityReport(name->str, i,
                        parameters.rise, parameters.steps - 1, *walkabilityLimit));
                }
            }
        }
        else if (name->str == "buildHeightmapTerrain")
        {
            // E-14: one box sector per heightmap cell, validated by the
            // shared core domain before the failure-atomic build.
            sed::level::TerrainParameters parameters{};
            parameters.origin = pointField(op, "origin");
            parameters.cellSize = numberField(op, "cellSize", idx);
            parameters.headroom = numberField(op, "headroom", idx);
            parameters.columns = int32Field(op, "columns", idx);
            parameters.rows = int32Field(op, "rows", idx);
            const Json* heights = op.find("heights");
            if (!heights || heights->type != Json::Type::Array)
                fail(kExitUsage, "buildHeightmapTerrain requires a 'heights' array of numbers");
            parameters.heights.reserve(heights->array.size());
            for (const Json& value : heights->array)
            {
                if (value.type != Json::Type::Number)
                    fail(kExitUsage, "buildHeightmapTerrain 'heights' entries must be numbers");
                parameters.heights.push_back(value.number);
            }
            if (const auto refusal = sed::level::validateTerrainParameters(parameters))
                failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
            if (!sed::level::levelBuildHeightmapTerrain(level, parameters))
                failCoded(kExitRefusal, "invalidTerrainParameters",
                    "invalidTerrainParameters: terrain could not be built with these parameters");
        }
        else if (name->str == "applyMaskedSurfaceRecipe")
        {
            const auto surfaces = surfacesField(op, "surfaces", idx);
            sed::level::MaskedSurfaceRecipe recipe{};
            recipe.surfaceFlags = maskField(op, "surfaceFlags");
            recipe.faceFlags = maskField(op, "faceFlags");
            recipe.adjoinFlags = maskField(op, "adjoinFlags");
            if (op.find("minSlopeDegrees")) recipe.minSlopeDegrees = numberField(op, "minSlopeDegrees", idx);
            if (op.find("maxSlopeDegrees")) recipe.maxSlopeDegrees = numberField(op, "maxSlopeDegrees", idx);
            if (const Json* v = op.find("copyFrom"))
            {
                if (v->type != Json::Type::Array || v->array.size() != 2
                    || v->array[0].type != Json::Type::Number || v->array[1].type != Json::Type::Number)
                    fail(kExitUsage, "copyFrom must be [sector, surface]");
                recipe.copyFrom = sed::SurfaceRef{ checkedInt32(v->array[0].number, "sector"),
                                                   checkedInt32(v->array[1].number, "surface") };
            }
            if (const Json* v = op.find("copyFields"))
            {
                if (v->type != Json::Type::Array) fail(kExitUsage, "copyFields must be an array of field names");
                for (const Json& field : v->array)
                {
                    if (field.type != Json::Type::String) fail(kExitUsage, "copyFields entries must be strings");
                    if (field.str == "material") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::Material);
                    else if (field.str == "geometry") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::Geometry);
                    else if (field.str == "extraLight") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::ExtraLight);
                    else if (field.str == "surfaceFlags") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::SurfaceFlags);
                    else if (field.str == "faceFlags") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::FaceFlags);
                    else if (field.str == "adjoinFlags") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::AdjoinFlags);
                    else if (field.str == "uvScale") recipe.copyFields |= static_cast<std::uint32_t>(sed::level::MaskedCopyField::UvScale);
                    else fail(kExitUsage, "unknown copyFields entry '" + field.str + "'");
                }
            }
            const auto bits = [&](const char* key, std::uint32_t& target) {
                if (op.find(key)) target = checkedUint32(numberField(op, key, idx), key);
            };
            bits("setSurfaceFlags", recipe.setSurfaceFlags);
            bits("clearSurfaceFlags", recipe.clearSurfaceFlags);
            bits("setFaceFlags", recipe.setFaceFlags);
            bits("clearFaceFlags", recipe.clearFaceFlags);
            bits("setAdjoinFlags", recipe.setAdjoinFlags);
            bits("clearAdjoinFlags", recipe.clearAdjoinFlags);
            if (op.find("material")) recipe.material = stringField(op, "material", idx);
            if (const auto refusal = sed::level::validateMaskedSurfaceRecipe(recipe))
                failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
            (void)sed::level::levelApplyMaskedSurfaceRecipe(level, surfaces, recipe);
        }
        else if (name->str == "bakeLighting")
        {
            const auto transport = op.find("transport") ? stringField(op, "transport", idx) : "legacy";
            if ( transport != "legacy" && transport != "ray" )
            {
                fail(kExitUsage, "bakeLighting transport must be legacy or ray");
            }
            const bool ray = transport == "ray";
            sed::level::RayLightingOptions rayOptions;
            if ( ray )
            {
                for ( const auto* key : {"intensity", "range", "skyRange", "bounceStrength", "bounceRange", "mirrorReflectivity"} )
                {
                    if ( op.find(key) )
                    {
                        fail(kExitUsage, std::string("bakeLighting ray transport does not support ") + key);
                    }
                }
                auto count = [&](const char* key, unsigned minimum, unsigned maximum, std::uint32_t& value)
                {
                    if ( op.find(key) )
                    {
                        const auto n = intField(op, key, idx);
                        if ( n < minimum || n > maximum )
                        {
                            fail(kExitUsage, std::string("bakeLighting ray option outside limits: ") + key);
                        }
                        value = static_cast<std::uint32_t>(n);
                    }
                };
                count("raySamples", 1, sed::level::kRayLightingMaxSamples, rayOptions.samples);
                count("rayBounces", 0, sed::level::kRayLightingMaxBounces, rayOptions.bounces);
                if ( op.find("rayReflectivity") )
                {
                    rayOptions.reflectivity = numberField(op, "rayReflectivity", idx);
                }
                if ( op.find("skyIntensity") )
                {
                    rayOptions.skyIntensity = numberField(op, "skyIntensity", idx);
                }
                if ( !(rayOptions.reflectivity >= 0 && rayOptions.reflectivity <= 1 &&
                       rayOptions.skyIntensity >= 0 && rayOptions.skyIntensity <= 1000) )
                {
                    fail(kExitUsage, "rayReflectivity requires 0..1 and ray skyIntensity requires 0..1000");
                }
                if ( const Json* sky = op.find("skyColor") )
                {
                    if ( sky->type != Json::Type::Array || (sky->array.size() != 3 && sky->array.size() != 4) )
                    {
                        fail(kExitUsage, "ray skyColor requires three or four components in 0..1");
                    }
                    for ( const auto& c : sky->array )
                    {
                        if ( c.type != Json::Type::Number || !(c.number >= 0 && c.number <= 1) )
                        {
                            fail(kExitUsage, "ray skyColor requires three or four components in 0..1");
                        }
                    }
                    rayOptions.skyColor = {static_cast<float>(sky->array[0].number),
                        static_cast<float>(sky->array[1].number), static_cast<float>(sky->array[2].number), 1};
                }
            }
            else if ( op.find("raySamples") || op.find("rayBounces") || op.find("rayReflectivity") )
            {
                fail(kExitUsage, "raySamples, rayBounces and rayReflectivity require ray transport");
            }
            sed::level::LightLimits limits;
            const bool applyLimits = op.find("lightFloor") || op.find("lightCap");
            if ( applyLimits && level.kind == sed::ProjectType::DroidWorks )
                fail(kExitUsage, "bakeLighting light limits are not supported for DroidWorks");
            if ( op.find("lightFloor") ) limits.floor = numberField(op, "lightFloor", idx);
            if ( op.find("lightCap") ) limits.cap = numberField(op, "lightCap", idx);
            if ( !std::isfinite(limits.floor) || !std::isfinite(limits.cap)
                || limits.floor < 0.0 || limits.floor > limits.cap || limits.cap > 1.0 )
                fail(kExitUsage, "bakeLighting requires 0 <= lightFloor <= lightCap <= 1");
            // E-17: resolve FULLYLIT emission from MAT bytes under the
            // explicit resource roots/GOBs, then run the core bake over all
            // sectors. Unresolvable MATs omit only that surface.
            double intensity = sed::level::kDefaultEmissiveIntensity;
            double range = sed::level::kDefaultEmissiveRange;
            if (op.find("intensity")) intensity = numberField(op, "intensity", idx);
            if (op.find("range")) range = numberField(op, "range", idx);
            if (!sed::level::emissiveProfileValid(intensity, range))
                fail(kExitUsage, "bakeLighting intensity/range must be finite and positive");
            sed::assets::ResourceLocator locator;
            if (const Json* roots = op.find("resourceRoots"))
            {
                if (roots->type != Json::Type::Array)
                    fail(kExitUsage, "bakeLighting 'resourceRoots' must be an array of paths");
                for (const Json& root : roots->array)
                {
                    if (root.type != Json::Type::String)
                        fail(kExitUsage, "bakeLighting 'resourceRoots' entries must be strings");
                    locator.addRoot(fs::path(root.str));
                }
            }
            if (const Json* gobs = op.find("gobs"))
            {
                if (gobs->type != Json::Type::Array)
                    fail(kExitUsage, "bakeLighting 'gobs' must be an array of paths");
                for (const Json& gob : gobs->array)
                {
                    if (gob.type != Json::Type::String)
                        fail(kExitUsage, "bakeLighting 'gobs' entries must be strings");
                    const auto archivePath = fs::path(gob.str);
                    const auto bytes = readArchiveBytes(archivePath);
                    admitCapturedArchive(bytes, archivePath);
                    locator.mountGob(sed::assets::GobArchive::fromBytes(toBytes(bytes), archivePath));
                }
            }
            auto captured = std::make_shared<detail::BakeResourceSnapshot>(std::move(locator),
                activeResourceBudget ? activeResourceBudget->bake : std::make_shared<detail::BakeResourceBudget>(),
                activeCommandOptions ? activeCommandOptions->resourceReadPolicy : std::function<void(const fs::path&)>{});
            const sed::level::EmissiveResourceReader reader = [captured](std::string_view resource, std::string_view extension) {
                return captured->read(resource, extension);
            };
            sed::level::RayLightingResources rayResources;
            sed::level::EmissiveResolution emissive;
            std::string resourceError;
            if ( ray )
            {
                if ( const Json* source = request.find("templateSource"); source && !archTemplates )
                {
                    archTemplates = loadTemplateSource(*source);
                }
                try
                {
                    rayResources = sed::level::resolveRayLightingResources(level, reader, {},
                        archTemplates ? &archTemplates->database : nullptr);
                }
                catch ( const std::exception& error )
                {
                    resourceError = error.what();
                }
            }
            else
            {
                emissive = sed::level::resolveEmissiveSurfaces(level, reader, intensity, range);
            }
            try { captured->throwIfReadFailed(); }
            catch (const detail::ResourceRevisionError& error) { fail(kExitConflict, error.what()); }
            catch (const std::exception& error) { fail(kExitInput, error.what()); }
            if ( !resourceError.empty() )
            {
                failCoded(kExitRefusal, "rayLightingResourceRefused", resourceError);
            }
            if (activeResourceChecks)
                activeResourceChecks->push_back([captured] { captured->requireUnchanged(); });
            std::vector<sed::level::LightingEmissiveSurface> emissiveSurfaces = emissive.surfaces;
            // E-17 sun/sky profile: FULLYLIT sky-flagged surfaces emit the
            // uniform sky colour alongside MAT-resolved emissives.
            double skyIntensity = 0.0;
            double skyRange = sed::level::kDefaultEmissiveRange;
            if (op.find("skyIntensity")) skyIntensity = numberField(op, "skyIntensity", idx);
            if (op.find("skyRange")) skyRange = numberField(op, "skyRange", idx);
            if (!ray && skyIntensity > 0.0)
            {
                if (!sed::level::emissiveProfileValid(skyIntensity, skyRange))
                    fail(kExitUsage, "bakeLighting skyIntensity/skyRange must be finite and positive");
                sed::ColorF skyColor = sed::ColorWhite;
                if (const Json* v = op.find("skyColor"))
                {
                    if (v->type != Json::Type::Array || (v->array.size() != 3 && v->array.size() != 4))
                        fail(kExitUsage, "skyColor must be [r,g,b] or [r,g,b,a]");
                    for (const Json& c : v->array)
                        if (c.type != Json::Type::Number) fail(kExitUsage, "skyColor components must be numbers");
                    skyColor = sed::ColorF{ checkedFloat(v->array[0].number, "skyColor"),
                        checkedFloat(v->array[1].number, "skyColor"), checkedFloat(v->array[2].number, "skyColor"),
                        v->array.size() == 4 ? checkedFloat(v->array[3].number, "skyColor") : 1.0f };
                }
                const auto sky = sed::level::resolveSkyEmissiveSurfaces(
                    level, skyColor, skyIntensity, skyRange);
                emissiveSurfaces.insert(emissiveSurfaces.end(),
                    sky.surfaces.begin(), sky.surfaces.end());
            }
            std::vector<std::int32_t> allSectors(level.sectors.size());
            for (std::size_t s = 0; s < allSectors.size(); ++s)
                allSectors[s] = static_cast<std::int32_t>(s);
            // Mirror floors: during the bake the floor portal blocks light both
            // ways, as the solid floor it stands for (the reflection's lamps
            // must not light the room from below, nor the room's lights the
            // reflection); afterwards the flags are restored and each
            // reflection takes the room's new light at mirrorReflectivity.
            double mirrorReflectivity = sed::level::kDefaultMirrorReflectivity;
            if (op.find("mirrorReflectivity")) mirrorReflectivity = numberField(op, "mirrorReflectivity", idx);
            if (!std::isfinite(mirrorReflectivity) || mirrorReflectivity < 0.0 || mirrorReflectivity > 1.0)
                fail(kExitUsage, "bakeLighting mirrorReflectivity must be within 0..1");
            const auto mirrors = sed::level::levelFindMirrorFloors(level);
            if ( ray && !mirrors.empty() )
            {
                failCoded(kExitRefusal, "rayLightingMirrorUnsupported", "Ray transport does not support mirror floors.");
            }
            std::vector<std::pair<sed::SurfaceRef, std::int32_t>> mirrorFlags;
            for (const auto& portal : mirrors)
            {
                for (const sed::SurfaceRef ref : { portal.floor, portal.mirror })
                {
                    auto& face = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
                    mirrorFlags.emplace_back(ref, face.adjoinFlags);
                    face.adjoinFlags |= sed::SAF_BlockLight;
                }
            }
            // Shading adopted from LadyEebs' JED Light Baker: angle (N.L) and
            // water absorption are opt-in; suns, anchors and multiply lights
            // follow their light flags.
            sed::level::LightingBakeOptions bakeOptions;
            const auto boolOption = [&](const char* key, bool& target) {
                if (const Json* v = op.find(key))
                {
                    if (v->type != Json::Type::Bool)
                        fail(kExitUsage, std::string("bakeLighting ") + key + " must be a boolean");
                    target = v->boolean;
                }
            };
            boolOption("angleShading", bakeOptions.angleShading);
            boolOption("waterAbsorption", bakeOptions.waterAbsorption);
            std::size_t sunLights = 0;
            std::size_t multiplyLights = 0;
            for (const sed::SedLight& light : level.lights)
            {
                if ((light.flags & sed::LF_Sun) != 0) ++sunLights;
                else if ((light.flags & sed::LF_Multiply) != 0 && (light.flags & sed::LF_SunAnchor) == 0
                    && level.kind != sed::ProjectType::MOTS) ++multiplyLights;
            }
            // E-17 bounce profile: optional one-bounce diffuse pass.
            double bounceStrength = 0.0;
            double bounceRange = sed::level::kDefaultEmissiveRange;
            if (op.find("bounceStrength")) bounceStrength = numberField(op, "bounceStrength", idx);
            if (op.find("bounceRange")) bounceRange = numberField(op, "bounceRange", idx);
            sed::level::RayLightingResult rayResult;
            if ( ray )
            {
                rayOptions.direct = bakeOptions;
                rayResult = sed::level::bakeRayLighting(level, allSectors, rayOptions,
                    rayResources.materials, rayResources.blockers);
                if ( !rayResult.succeeded )
                {
                    failCoded(kExitRefusal, "rayLightingRefused", rayResult.message);
                }
            }
            else if (bounceStrength > 0.0)
            {
                if (!sed::level::emissiveProfileValid(bounceStrength, bounceRange))
                    fail(kExitUsage, "bakeLighting bounceStrength/bounceRange must be finite and positive");
                (void)sed::level::levelCalcLightingNewWithBounce(
                    level, allSectors, emissiveSurfaces, bounceStrength, bounceRange, bakeOptions);
            }
            else
            {
                sed::level::levelCalcLightingNew(level, allSectors, emissiveSurfaces, bakeOptions);
            }
            for (const auto& [ref, flags] : mirrorFlags)
                level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)].adjoinFlags = flags;
            const std::size_t mirrorsRefreshed = ray ? 0 : sed::level::levelRefreshMirrorFloors(level, mirrorReflectivity);
            if ( applyLimits && sed::level::applyLightLimits(level, limits)
                != sed::level::LightLimitsStatus::Ok )
                failCoded(kExitRefusal, "nonFiniteBakedLighting",
                    "bakeLighting cannot apply limits to nonfinite stored RGB");
            sed::level::LightDiagnosticOptions diagnosticOptions;
            diagnosticOptions.cap = limits.cap;
            diagnosticOptions.nearBlack = std::min(0.01, limits.cap);
            diagnosticOptions.maxDiagnostics = remainingLightDiagnostics;
            const auto report = sed::level::inspectLightLimits(level, diagnosticOptions);
            if ( report.status != sed::level::LightLimitsStatus::Ok )
                failCoded(kExitRefusal, "nonFiniteBakedLighting",
                    "bakeLighting produced nonfinite stored RGB");
            remainingLightDiagnostics -= report.diagnostics.size();
            Json summary = Json::makeObject();
            summary.set("operation", Json::makeNumber(static_cast<double>(i)));
            if ( ray )
            {
                summary.set("transport", Json::makeString("ray"));
                summary.set("backend", Json::makeString("cpu"));
                summary.set("raySamples", Json::makeNumber(rayOptions.samples));
                summary.set("rayBounces", Json::makeNumber(rayOptions.bounces));
                summary.set("rayReflectivity", Json::makeNumber(rayOptions.reflectivity));
                summary.set("rays", Json::makeNumber(static_cast<double>(rayResult.rays)));
                summary.set("maxRayQueries", Json::makeNumber(sed::level::kRayLightingMaxQueries));
                summary.set("maxRaySamples", Json::makeNumber(sed::level::kRayLightingMaxSamples));
                summary.set("maxRayBounces", Json::makeNumber(sed::level::kRayLightingMaxBounces));
                summary.set("maxTriangles", Json::makeNumber(sed::level::kRayLightingMaxTriangles));
                summary.set("maxCorners", Json::makeNumber(sed::level::kRayLightingMaxCorners));
                summary.set("maxTraversalWork", Json::makeNumber(sed::level::kRayLightingMaxTraversalWork));
                summary.set("modelInstances", Json::makeNumber(static_cast<double>(rayResources.modelInstances)));
                summary.set("blockerTriangles", Json::makeNumber(static_cast<double>(rayResources.blockers.size())));
                summary.set("resourceDependencies", Json::makeNumber(static_cast<double>(rayResources.dependencies.size())));
            }
            summary.set("scope", Json::makeString("stored-corner-rgb"));
            summary.set("policyApplied", Json::makeBool(applyLimits));
            summary.set("lightFloor", Json::makeNumber(limits.floor));
            summary.set("lightCap", Json::makeNumber(limits.cap));
            summary.set("mirrorFloors", Json::makeNumber(static_cast<double>(mirrorsRefreshed)));
            summary.set("angleShading", Json::makeBool(bakeOptions.angleShading));
            summary.set("waterAbsorption", Json::makeBool(bakeOptions.waterAbsorption));
            summary.set("sunLights", Json::makeNumber(static_cast<double>(sunLights)));
            summary.set("multiplyLights", Json::makeNumber(static_cast<double>(multiplyLights)));
            summary.set("corners", Json::makeNumber(static_cast<double>(report.corners)));
            summary.set("minimum", Json::makeNumber(report.minimum));
            summary.set("maximum", Json::makeNumber(report.maximum));
            summary.set("nearBlackCorners", Json::makeNumber(static_cast<double>(report.nearBlackCorners)));
            summary.set("cappedCorners", Json::makeNumber(static_cast<double>(report.cappedCorners)));
            summary.set("overUnitCorners", Json::makeNumber(static_cast<double>(report.overUnitCorners)));
            summary.set("diagnosticsTruncated", Json::makeBool(report.truncated));
            Json diagnostics = Json::makeArray();
            for ( const auto& diagnostic : report.diagnostics )
            {
                Json row = Json::makeObject();
                row.set("sector", Json::makeNumber(static_cast<double>(diagnostic.sectorIndex)));
                row.set("sectorId", Json::makeNumber(diagnostic.sectorId));
                row.set("surface", Json::makeNumber(static_cast<double>(diagnostic.surfaceIndex)));
                row.set("surfaceNum", Json::makeNumber(diagnostic.surfaceNum));
                row.set("corner", Json::makeNumber(static_cast<double>(diagnostic.cornerIndex)));
                row.set("nearBlack", Json::makeBool(diagnostic.nearBlack));
                row.set("atOrAboveCap", Json::makeBool(diagnostic.atOrAboveCap));
                row.set("overUnit", Json::makeBool(diagnostic.overUnit));
                diagnostics.array.push_back(std::move(row));
            }
            summary.set("diagnostics", std::move(diagnostics));
            bakeReports.array.push_back(std::move(summary));
        }
        else if (name->str == "insertPrefab")
        {
            // E-04: insert a hash-bound prefab file captured by
            // prefab-capture (or the editor's Save Sectors as Prefab).
            const std::string prefabPath = stringField(op, "prefabFile", idx);
            const std::string prefabText = readFileBytes(fs::path(prefabPath));
            const auto prefab = sed::level::loadPrefabFromText(prefabText);
            if (!prefab)
                failCoded(kExitRefusal, "prefabInvalidHash",
                    "prefabInvalidHash: prefab file is malformed or its content hash does not verify");
            std::optional<sed::TVector> target{};
            if (op.find("target")) target = pointField(op, "target");
            if (!sed::level::insertPrefab(level, *prefab, target))
                failCoded(kExitRefusal, "prefabProjectMismatch",
                    "prefabProjectMismatch: prefab source project differs from this level");
        }
        else if (name->str == "addCog")
        {
            applyAddCogOp(level, op, request, idx);
        }
        else if (name->str == "rebindCogReference")
        {
            applyRebindOp(level, op, request, idx);
        }
        else if (name->str == "setCogValue")
        {
            applySetCogValueOp(level, op, request);
        }
        else if (name->str == "makeDoor")
        {
            applyMakeDoorOp(level, op, request, idx);
        }
        else if (name->str == "doorAssistant" || name->str == "elevatorAssistant" || name->str == "switchAssistant"
            || name->str == "objectivesAssistant" || name->str == "cutsceneAssistant" || name->str == "pursuitAssistant")
        {
            applyAssistantOp(level, op, request, name->str);
        }
        else if (name->str == "defineTemplate")
        {
            applyDefineTemplateOp(level, op, templateReports, i);
        }
        else if (name->str == "importTemplates")
        {
            applyImportTemplatesOp(level, op, request, i, templateReports);
        }
        else
        {
            fail(kExitUsage, "unsupported operation '" + name->str + "'");
        }
        if (geometryBefore) validateGeometryAppendReferences(*geometryBefore, level);
    }
    return level;
}

Json paginate(Json items, const Json& request, std::size_t total)
{
    long long offset = 0, limit = 50;
    if (const Json* v = request.find("offset"))
    {
        if (v->type != Json::Type::Number) fail(kExitUsage, "offset must be a number");
        offset = intField(request, "offset", 0);
    }
    if (const Json* v = request.find("limit"))
    {
        if (v->type != Json::Type::Number) fail(kExitUsage, "limit must be a number");
        limit = intField(request, "limit", 0);
    }
    if (offset < 0 || limit < 1) fail(kExitUsage, "offset/limit out of range");
    if (limit > 200) limit = 200;
    Json page = Json::makeArray();
    const auto& all = items.array;
    for (long long i = offset; i < static_cast<long long>(all.size())
         && static_cast<long long>(page.array.size()) < limit; ++i)
        page.array.push_back(all[static_cast<std::size_t>(i)]);
    const long long next = offset + static_cast<long long>(page.array.size());
    Json out = Json::makeObject();
    out.set("items", std::move(page));
    out.set("total", Json::makeNumber(static_cast<double>(total)));
    out.set("offset", Json::makeNumber(static_cast<double>(offset)));
    if (next < static_cast<long long>(total))
        out.set("nextOffset", Json::makeNumber(static_cast<double>(next)));
    else
        out.set("nextOffset", Json{});
    return out;
}

bool matchesSearch(const std::string& haystack, const std::string& needle)
{
    return needle.empty() || lower(haystack).find(lower(needle)) != std::string::npos;
}

fs::path levelPath(std::string_view text)
{
    if ( !isUtf8PathText(text) )
    {
        fail(kExitUsage, "Level command paths must be valid UTF-8 without NUL bytes");
    }
    return pathFromUtf8(text);
}

Json cmdInspect(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "inspect requires a level path");
    const auto path = levelPath(args[0]);
    const auto doc = openDocument(path, pathToUtf8);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("inspect"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("kind", Json::makeNumber(static_cast<double>(static_cast<int>(doc.level.kind))));
    Json counts = Json::makeObject();
    counts.set("sectors", Json::makeNumber(static_cast<double>(doc.level.sectors.size())));
    counts.set("things", Json::makeNumber(static_cast<double>(doc.level.things.size())));
    counts.set("cogs", Json::makeNumber(static_cast<double>(doc.level.cogs.size())));
    counts.set("lights", Json::makeNumber(static_cast<double>(doc.level.lights.size())));
    counts.set("templates", Json::makeNumber(static_cast<double>(doc.level.templates.size())));
    counts.set("materials", Json::makeNumber(static_cast<double>(doc.level.materials.size())));
    counts.set("colormaps", Json::makeNumber(static_cast<double>(doc.level.colormaps.size())));
    out.set("counts", std::move(counts));
    out.set("gravity", Json::makeNumber(doc.level.header.gravity));
    return out;
}

Json cmdQuery(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "query requires <level> <request.json|->");
    const auto doc = openDocument(levelPath(args[0]), pathToUtf8);
    const Json request = readRequestFile(levelPath(args[1]));
    if (!request.isObject()) fail(kExitUsage, "query request must be a JSON object");
    checkRevision(doc, request);
    const Json* coll = request.find("collection");
    if (!coll || coll->type != Json::Type::String) fail(kExitUsage, "query requires 'collection'");
    if ( auto detailed = queryRecordDetails(doc.level, request) )
    {
        detailed->set("inputSha256", Json::makeString(doc.inputSha256));
        detailed->set("sourceRevision", Json::makeString(doc.sourceRevision));
        return *detailed;
    }
    if (coll->str == "surfaces" || coll->str == "vertices")
    {
        auto out = queryGeometryDetails(doc.level, request);
        out.set("inputSha256", Json::makeString(doc.inputSha256));
        out.set("sourceRevision", Json::makeString(doc.sourceRevision));
        return out;
    }
    std::string search;
    if (const Json* s = request.find("search"))
    {
        if (s->type != Json::Type::String) fail(kExitUsage, "search must be a string");
        search = s->str;
    }
    Json items = Json::makeArray();
    const auto& level = doc.level;
    auto row = [](std::initializer_list<std::pair<std::string, Json>> fields) {
        Json o = Json::makeObject();
        for (auto& f : fields) o.set(f.first, std::move(f.second));
        return o;
    };
    if (coll->str == "summary")
    {
        Json s = Json::makeObject();
        s.set("sectors", Json::makeNumber(static_cast<double>(level.sectors.size())));
        s.set("things", Json::makeNumber(static_cast<double>(level.things.size())));
        s.set("cogs", Json::makeNumber(static_cast<double>(level.cogs.size())));
        s.set("lights", Json::makeNumber(static_cast<double>(level.lights.size())));
        s.set("gravity", Json::makeNumber(level.header.gravity));
        s.set("inputSha256", Json::makeString(doc.inputSha256));
        s.set("sourceRevision", Json::makeString(doc.sourceRevision));
        items.array.push_back(std::move(s));
    }
    else if (coll->str == "things")
    {
        for (std::size_t i = 0; i < level.things.size(); ++i)
        {
            const auto& t = level.things[i];
            if (!matchesSearch(t.name, search) && !matchesSearch(t.templateName, search)) continue;
            items.array.push_back(describeThingDetails(level, i));
        }
    }
    else if (coll->str == "lights")
    {
        for (std::size_t i = 0; i < level.lights.size(); ++i)
        {
            items.array.push_back(describeLightDetails(level, i));
        }
    }
    else if (coll->str == "materials" || coll->str == "colormaps")
    {
        const auto& table = coll->str == "materials" ? level.materials : level.colormaps;
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            if (!matchesSearch(table[i], search)) continue;
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "name", Json::makeString(table[i]) },
            }));
        }
    }
    else fail(kExitUsage, "unknown query collection '" + coll->str + "'");

    Json out = paginate(items, request, items.array.size());
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("query"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    return out;
}

sed::TVector pointField(const Json& obj, const char* key)
{
    const Json* v = obj.find(key);
    if (!v || v->type != Json::Type::Array || v->array.size() != 3)
        fail(kExitUsage, std::string("'") + key + "' must be an [x, y, z] array");
    for (const Json& component : v->array)
        if (component.type != Json::Type::Number)
            fail(kExitUsage, std::string("'") + key + "' components must be numbers");
    return sed::TVector{ v->array[0].number, v->array[1].number, v->array[2].number };
}

Json targetJson(const sed::level::SelectionTarget& target)
{
    Json out = Json::makeObject();
    out.set("kind", Json::makeString(std::string(
        sed::level::selectionTargetKindName(target.kind))));
    if (target.sector >= 0)
        out.set("sector", Json::makeNumber(static_cast<double>(target.sector)));
    if (target.surface >= 0)
        out.set("surface", Json::makeNumber(static_cast<double>(target.surface)));
    if (target.vertex >= 0)
        out.set("vertex", Json::makeNumber(static_cast<double>(target.vertex)));
    if (target.thing >= 0)
        out.set("thing", Json::makeNumber(static_cast<double>(target.thing)));
    return out;
}

// Geometry-bound spatial selection (F-07). Minting is read-only: `none`
// and `ambiguous` succeed with a null token. Replaying a token against a
// changed document reports the stale code with exit 4, never a re-match.
Json cmdSelect(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "select requires <level> <request.json|->");
    const auto doc = openDocument(levelPath(args[0]), pathToUtf8);
    const Json request = readRequestFile(levelPath(args[1]));
    if (!request.isObject()) fail(kExitUsage, "select request must be a JSON object");
    checkRevision(doc, request);
    const sed::level::SelectionContext context{ doc.inputSha256, doc.sourceRevision };

    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("select"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));

    if (const Json* token = request.find("resolve"))
    {
        if (token->type != Json::Type::String) fail(kExitUsage, "resolve must be a token string");
        const auto resolution = sed::level::resolveSelectionToken(doc.level, context, token->str);
        if (resolution.status == sed::level::SelectionTokenStatus::malformed)
            fail(kExitUsage, std::string(sed::level::kMalformedSelectionTokenCode)
                + ": selection token cannot be read");
        if (resolution.status == sed::level::SelectionTokenStatus::stale)
            fail(kExitConflict, std::string(sed::level::kStaleSelectionTokenCode)
                + ": selection token no longer matches this document");
        out.set("resolved", Json::makeBool(true));
        out.set("target", targetJson(resolution.target));
        return out;
    }

    const Json* target = request.find("target");
    if (!target || target->type != Json::Type::String)
        fail(kExitUsage, "select requires 'target' (or 'resolve')");
    const sed::TVector point = pointField(request, "point");
    sed::level::SpatialSelection selection;
    if (target->str == "sector")
        selection = sed::level::selectSectorAtPoint(doc.level, context, point);
    else if (target->str == "surface")
        selection = sed::level::selectSurfaceByPointAndDirection(
            doc.level, context, point, pointField(request, "direction"));
    else if (target->str == "vertex")
        selection = sed::level::selectVertexAtPoint(doc.level, context, point);
    else if (target->str == "thing")
        selection = sed::level::selectThingAtPoint(doc.level, context, point);
    else
        fail(kExitUsage, "unknown select target '" + target->str + "'");

    out.set("match", Json::makeString(std::string(
        sed::level::spatialMatchName(selection.match))));
    out.set("candidates", Json::makeNumber(static_cast<double>(selection.candidateCount)));
    if (selection.token)
        out.set("token", Json::makeString(selection.token->text));
    else
        out.set("token", Json{});
    if (selection.match == sed::level::SpatialMatch::unique)
        out.set("target", targetJson(selection.target));
    else
        out.set("target", Json{});
    if (selection.match == sed::level::SpatialMatch::cannotClassify)
    {
        out.set("code", Json::makeString(selection.code));
        out.set("error", Json::makeString(selection.diagnostic));
    }
    return out;
}

// The export dependency scan shared by plan/apply and the standalone COG
// commands. The writer can only declare the SOUNDS, MATERIALS, KEYFRAMES and
// COG capacities a level needs when it can read what its models, puppets,
// sound classes and scripts pull in. A JKL written without that scan
// declares too little room and the game fails while loading the player's own
// resources, so the scan runs by default for every JKL/NDY output (JKL, NDY,
// JED or NED input) whose request has patch-root resourceArchives.
// `resolveExportDependencies: false` keeps the document-only counts for
// callers that need the earlier bytes. Archives given on one operation are
// that operation's own and do not enable the scan.
struct ExportScanSetup
{
    bool resolve{};
    bool explicitChoice{};
    bool operationArchivesOnly{};
    std::optional<sed::assets::ResourceLocator> resources{};
    std::shared_ptr<const LoadedTemplateSource> templates{};
    sed::assets::JklExportBufferReport buffers{};
    std::vector<std::string> warnings{};
    bool warningsTruncated{};

    ExportScanSetup() = default;
    ExportScanSetup(const ExportScanSetup&) = delete;
    ExportScanSetup& operator=(const ExportScanSetup&) = delete;

    ExportDependencyScan scan()
    {
        return ExportDependencyScan{ resources ? &*resources : nullptr, templates ? &templates->database : nullptr,
            &buffers, &warnings, &warningsTruncated };
    }
};

void prepareExportScan(const OpenedDocument& doc, const Json& request, ExportScanSetup& setup)
{
    const Json* exportArchives = request.find("resourceArchives");
    const bool hasExportArchives = exportArchives && exportArchives->type == Json::Type::Array
        && !exportArchives->array.empty();
    setup.resolve = !doc.isSed && hasExportArchives;
    if (const Json* flag = request.find("resolveExportDependencies"))
    {
        if (flag->type != Json::Type::Bool)
            fail(kExitUsage, "resolveExportDependencies must be a boolean");
        setup.resolve = flag->boolean;
        setup.explicitChoice = true;
    }
    if (setup.resolve && doc.isSed)
        fail(kExitUsage, "resolveExportDependencies applies to JKL and NDY output only; a native .sed document "
                         "stores no export capacities");
    if (!hasExportArchives)
    {
        if (const Json* ops = request.find("operations"); ops && ops->type == Json::Type::Array)
            setup.operationArchivesOnly = std::any_of(ops->array.begin(), ops->array.end(),
                [](const Json& op) { return op.isObject() && op.find("resourceArchives") != nullptr; });
    }
    if (!setup.resolve) return;
    setup.resources = exportResourceLocator(request);
    // A patch-root templateSource is the master template file. With it the
    // writer emits the static templates every level carries and resolves
    // templates that only scripts name, as the editor does on save.
    if (const Json* source = request.find("templateSource"))
        setup.templates = loadTemplateSource(*source);
}

// Declared runtime capacities of the exact output bytes. A scan that was
// not requested, or that could not read a named resource, is never
// presented as a complete one.
void setExportBuffersReceipt(Json& out, const ExportScanSetup& setup)
{
    Json buffers = Json::makeObject();
    buffers.set("dependencyScan", Json::makeString(!setup.resolve
        ? (setup.explicitChoice ? "disabled" : "not-requested")
        : setup.buffers.dependencyScanComplete ? "complete" : "incomplete"));
    if (!setup.resolve)
        buffers.set("note", Json::makeString(std::string(
            "Capacities count only what the document names. Supply patch-root resourceArchives (and the master "
            "templateSource) so the writer can count what models, puppets, sound classes and scripts load; a "
            "level exported without that scan can fail to load in the game.")
            + (setup.operationArchivesOnly
                ? " Archives given on a single operation serve that operation only and do not enable the scan."
                : "")));
    else if (!setup.templates)
        buffers.set("templateNote", Json::makeString(
            "No templateSource was given: static templates and templates that only scripts name are not "
            "written, and a stock level reports missing templates. Give the master template file "
            "(templateSource) for a complete scan."));
    if (setup.templates)
        buffers.set("templateSourceSha256", Json::makeString(setup.templates->sha256));
    const auto usage = [&buffers](const char* name, const sed::assets::JklExportBufferUsage& value)
    {
        Json row = Json::makeObject();
        row.set("collected", Json::makeNumber(static_cast<double>(value.collected)));
        row.set("additional", Json::makeNumber(static_cast<double>(value.additional)));
        row.set("total", Json::makeNumber(static_cast<double>(value.total)));
        buffers.set(name, std::move(row));
    };
    const sed::assets::JklExportBufferReport& report = setup.buffers;
    usage("sounds", report.sounds);
    usage("materials", report.materials);
    usage("aiClasses", report.aiClasses);
    usage("models", report.models);
    usage("sprites", report.sprites);
    usage("keyframes", report.keyframes);
    usage("puppets", report.puppets);
    usage("soundClasses", report.soundClasses);
    usage("cogScripts", report.cogScripts);
    usage("cogs", report.cogs);
    usage("templates", report.templates);
    usage("things", report.things);
    if (setup.resolve)
    {
        Json warnings = Json::makeArray();
        for (const std::string& warning : setup.warnings)
            warnings.array.push_back(Json::makeString(warning));
        buffers.set("warnings", std::move(warnings));
        buffers.set("warningsTruncated", Json::makeBool(setup.warningsTruncated));
    }
    out.set("exportBuffers", std::move(buffers));
}

Json planOrApply(const std::vector<std::string>& args, bool isApply)
{
    if (args.size() < (isApply ? 3u : 2u))
        fail(kExitUsage, isApply ? "apply requires <level> <patch.json|-> <new-output>"
                                 : "plan requires <level> <patch.json|->");
    const fs::path inputPath = levelPath(args[0]);
    const auto doc = openDocument(inputPath, pathToUtf8);
    const Json request = readRequestFile(levelPath(args[1]));
    if (!request.isObject()) fail(kExitUsage, "patch request must be a JSON object");
    if (const Json* v = request.find("schemaVersion"))
    {
        if (v->type != Json::Type::Number || v->number != 1)
            fail(kExitUsage, "unsupported schemaVersion (expected 1)");
    }
    checkRevision(doc, request);
    Json bakeReports = Json::makeArray();
    Json templateReports = Json::makeArray();
    Json seamReports = Json::makeArray();
    Json connectReports = Json::makeArray();
    Json stairReports = Json::makeArray();
    Json deleteReports = Json::makeArray();
    Json carveReports = Json::makeArray();
    Json mirrorReports = Json::makeArray();
    Json editReports = Json::makeArray();
    const sed::Level edited = applyOperations(doc, request, bakeReports, templateReports, seamReports,
        connectReports, stairReports, deleteReports, carveReports, mirrorReports, editReports);
    // outputFormat "jed" writes the edited level as JED (JK and MotS, from a
    // JKL or JED input); "jkl" or omitted keeps the engine text format, and a
    // native .sed stays native (convert changes a document's format).
    bool writeJed = false;
    if (const Json* format = request.find("outputFormat"))
    {
        if (format->type != Json::Type::String || (format->str != "jkl" && format->str != "jed"))
            fail(kExitUsage, "outputFormat must be \"jkl\" or \"jed\"");
        writeJed = format->str == "jed";
        if (doc.isSed)
            failCoded(kExitRefusal, "outputFormatMismatch",
                "A native .sed document is written as .sed; use convert for a format change");
        if (format->str == "jkl" && doc.level.kind == sed::ProjectType::IJIM)
            failCoded(kExitRefusal, "outputFormatMismatch", "IJIM levels are written as .ndy");
        if (writeJed)
        {
            if (const Json* flag = request.find("resolveExportDependencies"); flag && flag->type == Json::Type::Bool && flag->boolean)
                fail(kExitUsage, "resolveExportDependencies applies to JKL and NDY output; JED stores no export capacities");
        }
    }
    ExportScanSetup exportScan;
    if (!writeJed) prepareExportScan(doc, request, exportScan);
    const ExportDependencyScan scan = exportScan.scan();
    sed::Level persisted;
    Json jedLosses = Json::makeArray();
    const std::string output = writeJed ? buildJedOutput(edited, &persisted, &jedLosses) : buildOutput(doc, edited, &scan, &persisted);
    const std::string outputSha = sha256Hex(output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(isApply ? "apply" : "plan"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("outputSha256", Json::makeString(outputSha));
    out.set("semanticPreview", buildSemanticPreview(doc.level, edited, persisted,
        doc.inputSha256, outputSha));
    if ( !bakeReports.array.empty() ) out.set("bakeLightingReports", std::move(bakeReports));
    if ( !templateReports.array.empty() ) out.set("templateImportReports", std::move(templateReports));
    if ( !seamReports.array.empty() ) out.set("seamConformReports", std::move(seamReports));
    if ( !connectReports.array.empty() ) out.set("connectSurfacesReports", std::move(connectReports));
    if ( !deleteReports.array.empty() ) out.set("deleteSectorsReports", std::move(deleteReports));
    if ( !carveReports.array.empty() ) out.set("carveSurfaceRectReports", std::move(carveReports));
    if ( !mirrorReports.array.empty() ) out.set("mirrorFloorReports", std::move(mirrorReports));
    if ( !editReports.array.empty() ) out.set("editReports", std::move(editReports));
    if ( !stairReports.array.empty() )
    {
        out.set("stairWalkabilityReports", std::move(stairReports));
    }
    if (writeJed)
    {
        out.set("outputFormat", Json::makeString("jed"));
        out.set("jedLosses", jedLosses);
    }
    if (!doc.isSed && !writeJed)
    {
        setExportBuffersReceipt(out, exportScan);
        // F-13: assess the exact bytes this plan would write, so the
        // aggregate stays bound to outputSha256. Read-only; no edits.
        const auto colors = sed::level::evaluateDynamicVertexColorsSerialized(
            output, edited.kind == sed::ProjectType::MOTS);
        Json summary = Json::makeObject();
        summary.set("status", Json::makeString(colors.status));
        summary.set("proposedOutputSha256", Json::makeString(colors.proposedOutputSha256));
        summary.set("surfaces", Json::makeNumber(static_cast<double>(colors.surfaces.size())));
        summary.set("ready", Json::makeNumber(static_cast<double>(
            sed::level::countVertexColorReadiness(colors, sed::level::VertexColorReadiness::Ready))));
        summary.set("notReady", Json::makeNumber(static_cast<double>(
            sed::level::countVertexColorReadiness(colors, sed::level::VertexColorReadiness::NotReady))));
        summary.set("unknown", Json::makeNumber(static_cast<double>(
            sed::level::countVertexColorReadiness(colors, sed::level::VertexColorReadiness::Unknown))));
        out.set("dynamicVertexColors", std::move(summary));
    }
    if (!isApply) return out; // plan writes NOTHING.

    const fs::path outputPath = levelPath(args[2]);
    checkApplyOutputFormat(doc, outputPath, writeJed, pathToUtf8);
    if (writeJed && !jedLosses.array.empty() && args.size() <= 3)
        failCoded(kExitRefusal, "jedLossUnacknowledged",
            "This JED drops what the format cannot hold (see the plan's jedLosses); apply it with the planned outputSha256");
    if (args.size() > 3)
    {
        if (lower(args[3]) != outputSha)
            fail(kExitConflict, "expected outputSha256 does not match the recomputed plan");
    }
    std::error_code ec;
    const auto absIn = fs::absolute(inputPath, ec);
    const auto absOut = fs::absolute(outputPath, ec);
    if (!ec && absIn == absOut) fail(kExitRefusal, "output must differ from the input file");
    if (fs::exists(outputPath)) fail(kExitRefusal, "output already exists; refusing to overwrite");
    if (!outputPath.parent_path().empty() && !fs::is_directory(outputPath.parent_path()))
        fail(kExitRefusal, "output directory does not exist");
    out.set("output", Json::makeString(pathToUtf8(outputPath)));
    writeNewFileAtomic(outputPath, output);
    return out;
}

// --- F-01 completion commands ---------------------------------------------

void checkNewOutputTarget(const fs::path& inputPath, const fs::path& outputPath, bool hasInput)
{
    std::error_code ec;
    if (hasInput)
    {
        const auto absIn = fs::absolute(inputPath, ec);
        const auto absOut = fs::absolute(outputPath, ec);
        if (!ec && absIn == absOut) fail(kExitRefusal, "output must differ from the input file");
    }
    if (fs::exists(outputPath)) fail(kExitRefusal, "output already exists; refusing to overwrite");
    if (!outputPath.parent_path().empty() && !fs::is_directory(outputPath.parent_path()))
        fail(kExitRefusal, "output directory does not exist");
}

Json cmdValidate(const std::vector<std::string>& args)
{
    bool strict = false;
    std::vector<std::string> positional;
    for ( const auto& argument : args )
    {
        if ( argument == "--strict" )
        {
            if ( strict ) fail(kExitUsage, "validate --strict cannot be repeated");
            strict = true;
        }
        else positional.push_back(argument);
    }
    if ( positional.empty() || positional.size() > 2 )
        fail(kExitUsage, "validate requires <level> [validate.json|-] [--strict]");
    const auto inputPath = levelPath(positional[0]);
    Json request = Json::makeObject();
    if ( positional.size() == 2 )
    {
        request = readRequestFile(levelPath(positional[1]));
        if ( !request.isObject() ) fail(kExitUsage, "validate request must be a JSON object");
        checkSchemaVersion(request);
        if ( const auto* value = request.find("strict") )
        {
            if ( value->type != Json::Type::Bool )
                fail(kExitUsage, "validate strict must be boolean");
            strict = strict || value->boolean;
        }
    }
    std::string strictBytes;
    sed::level::SealedSeamLimits seamLimits;
    if ( const auto* limits = request.find("seamLimits") )
    {
        applySeamLimits(*limits, seamLimits, "validate");
    }
    if ( strict )
    {
        const auto extension = lower(pathToUtf8(inputPath.extension()));
        if ( !extension.empty() && extension != ".jkl" )
            fail(kExitUsage, "validate --strict supports raw JKL text only");
        strictBytes = readFileBytes(inputPath);
        OpenedDocument identity;
        identity.inputSha256 = sha256Hex(strictBytes);
        identity.sourceRevision = identity.inputSha256;
        checkRevision(identity, request);
        auto structure = sed::assets::inspectJklStructure(strictBytes);
        if ( !structure.inspectedMaterials )
            structure.diagnostics.push_back({"missing-section", "MATERIALS", 1,
                "Strict JKL inspection requires SECTION: MATERIALS"});
        if ( !structure.inspectedGeoResource )
            structure.diagnostics.push_back({"missing-section", "GEORESOURCE", 1,
                "Strict JKL inspection requires SECTION: GEORESOURCE"});
        if ( !structure.ok() )
        {
            Json rawIssues = Json::makeArray();
            for ( const auto& diagnostic : structure.diagnostics )
            {
                Json row = Json::makeObject();
                row.set("severity", Json::makeString("error"));
                row.set("code", Json::makeString(diagnostic.code));
                row.set("section", Json::makeString(diagnostic.section));
                row.set("line", Json::makeNumber(static_cast<double>(diagnostic.line)));
                row.set("message", Json::makeString(diagnostic.message));
                rawIssues.array.push_back(std::move(row));
            }
            Json out = Json::makeObject();
            out.set("ok", Json::makeBool(true));
            out.set("command", Json::makeString("validate"));
            out.set("valid", Json::makeBool(false));
            out.set("inputSha256", Json::makeString(identity.inputSha256));
            out.set("sourceRevision", Json::makeString(identity.sourceRevision));
            out.set("rawStructureStatus", Json::makeString(structure.complete ? "checked" : "incomplete"));
            out.set("rawStructureScope", Json::makeString("materials-and-georesource"));
            out.set("cogReferenceStatus", Json::makeString("not-run-raw-structure"));
            out.set("seamStatus", Json::makeString("not-run-raw-structure"));
            out.set("issues", std::move(rawIssues));
            return out;
        }
    }
    const auto doc = strict ? openDocumentBytes(inputPath, strictBytes, pathToUtf8)
                            : openDocument(inputPath, pathToUtf8);
    Json issues = Json::makeArray();
    bool hasErrors = false;
    std::string cogStatus = "unverified-no-resources";
    Json assetNotices = Json::makeArray();
    sed::level::FanDegeneracyLimits fanLimits;
    if (positional.size() > 1)
    {
        checkRevision(doc, request);
        if (const auto* limits = request.find("fanDegeneracyLimits"))
        {
            if (!limits->isObject()) fail(kExitUsage, "validate fanDegeneracyLimits must be an object");
            for (const auto& [name, value] : limits->object)
            {
                const double maximum = name == "maxFanTriangles" ? 64000000.0
                    : name == "maxIssues" ? 1000000.0 : -1.0;
                if (maximum < 0.0) fail(kExitUsage, "unknown fanDegeneracyLimits field: " + name);
                if (value.type != Json::Type::Number || !std::isfinite(value.number)
                    || value.number < 0.0 || value.number > maximum || value.number != std::floor(value.number))
                    fail(kExitUsage, "fanDegeneracyLimits field must be an integer within its documented work bound: " + name);
                auto& target = name == "maxFanTriangles" ? fanLimits.maxFanTriangles : fanLimits.maxIssues;
                target = static_cast<std::size_t>(value.number);
            }
        }
        if (request.find("resourceArchives"))
        {
            const CogArchiveSet set = loadCogArchives(request);
            cogStatus = "checked";
            assetNotices = matAssetNotices(set);
            for (const auto& issue : sed::level::checkCogReferences(doc.level, cogResolver(set)))
            {
                Json row = Json::makeObject();
                row.set("severity", Json::makeString(issue.severity == sed::level::CogReferenceIssueSeverity::error ? "error" : "warning"));
                row.set("cog", Json::makeNumber(static_cast<double>(issue.cogIndex)));
                row.set("parameter", Json::makeNumber(static_cast<double>(issue.parameterIndex)));
                row.set("message", Json::makeString(issue.message));
                if (issue.severity == sed::level::CogReferenceIssueSeverity::error) hasErrors = true;
                issues.array.push_back(std::move(row));
            }
        }
    }
    const auto seams = sed::level::inspectSealedSeams(doc.level, seamLimits);
    const char* const seamStatus = sealedSeamStatusName(seams.status);
    for ( const auto& seam : seams.issues )
    {
        Json row = Json::makeObject();
        row.set("severity", Json::makeString("error"));
        row.set("code", Json::makeString("sealed-seam"));
        row.set("sector", Json::makeNumber(seam.first.sector));
        row.set("surface", Json::makeNumber(seam.first.surface));
        row.set("otherSector", Json::makeNumber(seam.second.sector));
        row.set("otherSurface", Json::makeNumber(seam.second.surface));
        row.set("message", Json::makeString(
            "A surface edge overlaps another sector's coplanar edge without matching endpoints"));
        row.set("repairAuthorized", Json::makeBool(false));
        Json contacts = Json::makeArray();
        for ( const auto& contact : seam.contacts )
        {
            Json item = Json::makeObject();
            item.set("edge", Json::makeNumber(static_cast<double>(contact.first.edge)));
            item.set("otherEdge", Json::makeNumber(static_cast<double>(contact.second.edge)));
            contacts.array.push_back(std::move(item));
        }
        row.set("edgeContacts", std::move(contacts));
        Json insertions = Json::makeArray();
        for ( const auto& candidate : seam.insertions )
        {
            Json item = Json::makeObject();
            item.set("sector", Json::makeNumber(candidate.target.surface.sector));
            item.set("surface", Json::makeNumber(candidate.target.surface.surface));
            item.set("edge", Json::makeNumber(static_cast<double>(candidate.target.edge)));
            item.set("donorSector", Json::makeNumber(candidate.donor.sector));
            item.set("donorVertex", Json::makeNumber(candidate.donor.vertex));
            const char* classification = "approximate-only";
            if ( candidate.kind == sed::level::SealedSeamCandidateKind::ExactOnEdge )
                classification = "exact-on-edge";
            else if ( candidate.kind == sed::level::SealedSeamCandidateKind::Ambiguous )
                classification = "ambiguous";
            item.set("classification", Json::makeString(classification));
            Json point = Json::makeArray();
            point.array = {Json::makeNumber(candidate.point.x), Json::makeNumber(candidate.point.y),
                Json::makeNumber(candidate.point.z)};
            item.set("point", std::move(point));
            insertions.array.push_back(std::move(item));
        }
        row.set("insertionCandidates", std::move(insertions));
        issues.array.push_back(std::move(row));
        hasErrors = true;
    }
    if ( seams.status != sed::level::SealedSeamStatus::Complete )
    {
        Json row = Json::makeObject();
        row.set("severity", Json::makeString("error"));
        row.set("code", Json::makeString("sealed-seam-scan-incomplete"));
        row.set("message", Json::makeString(
            "Seam inspection did not complete; findings and candidate classifications are provisional"));
        row.set("status", Json::makeString(seamStatus));
        issues.array.push_back(std::move(row));
        hasErrors = true;
    }
    const auto fans = sed::level::inspectFanDegeneracies(doc.level, fanLimits);
    const auto fanStatus = [&]() -> const char*
    {
        using Status = sed::level::FanDegeneracyStatus;
        switch (fans.status)
        {
        case Status::Complete: return "complete";
        case Status::BudgetExceeded: return "budget-exceeded";
        case Status::InvalidGeometry: return "invalid-geometry";
        case Status::AllocationFailure: return "allocation-failure";
        case Status::NotImplemented: return "not-implemented";
        }
        return "unknown";
    }();
    for (const auto& finding : fans.issues)
    {
        const bool coincident = finding.kind == sed::level::FanDegeneracyKind::CoincidentCorners;
        Json row = Json::makeObject();
        // A repeated corner is a defect. An exactly collinear fan triangle
        // is what every edge vertex next to the fan origin produces: the
        // shipped JK levels carry hundreds each, and cleaving a wall for a
        // doorway adds more, because the neighbouring faces receive the cut
        // points that keep the room closed. It is reported, as a warning,
        // and does not make a level invalid.
        row.set("severity", Json::makeString(coincident ? "error" : "warning"));
        row.set("code", Json::makeString("fan-degeneracy"));
        row.set("sector", Json::makeNumber(finding.surface.sector));
        row.set("surface", Json::makeNumber(finding.surface.surface));
        row.set("fanIndex", Json::makeNumber(static_cast<double>(finding.fanIndex)));
        row.set("kind", Json::makeString(coincident ? "coincident-corners" : "collinear-corners"));
        row.set("message", Json::makeString(coincident
            ? "A surface fan triangle has coincident corners and zero area"
            : "A surface fan triangle is exactly collinear and has zero area"));
        row.set("repairAuthorized", Json::makeBool(false));
        issues.array.push_back(std::move(row));
        if (coincident) hasErrors = true;
    }
    {
        // A surface whose every fan triangle is degenerate has no area and no
        // plane at all; it renders as a hole. That is an error, unlike a
        // collinear triangle beside the fan origin of a surface with area.
        std::map<std::pair<std::int32_t, std::int32_t>, std::set<std::size_t>> degenerateFans;
        for (const auto& finding : fans.issues)
            degenerateFans[{ finding.surface.sector, finding.surface.surface }].insert(finding.fanIndex);
        for (const auto& [ref, fanIndices] : degenerateFans)
        {
            if (ref.first < 0 || static_cast<std::size_t>(ref.first) >= doc.level.sectors.size()) continue;
            const sed::Sector& sector = doc.level.sectors[static_cast<std::size_t>(ref.first)];
            if (ref.second < 0 || static_cast<std::size_t>(ref.second) >= sector.surfaces.size()) continue;
            const std::size_t corners = sector.surfaces[static_cast<std::size_t>(ref.second)].vertices.size();
            if (corners < 3 || fanIndices.size() < corners - 2) continue;
            Json row = Json::makeObject();
            row.set("severity", Json::makeString("error"));
            row.set("code", Json::makeString("zero-area-surface"));
            row.set("sector", Json::makeNumber(ref.first));
            row.set("surface", Json::makeNumber(ref.second));
            row.set("message", Json::makeString(
                "Every fan triangle of this surface is degenerate: the surface has no area and no plane"));
            row.set("repairAuthorized", Json::makeBool(false));
            issues.array.push_back(std::move(row));
            hasErrors = true;
        }
    }
    if (doc.level.kind == sed::ProjectType::JKDF2 || doc.level.kind == sed::ProjectType::MOTS)
    {
        // A level declares its world-material capacity, and the game refuses a new material once the
        // table is full. Joining players bring model and saber materials at runtime: Sith Council
        // declared exactly its 519 listed materials, played alone, and made the host exit when a
        // second player joined. LucasArts' six FFA maps each keep 32 spare.
        constexpr std::uint32_t kRecommendedSpareMaterials = 32;
        std::size_t playerStarts = 0;
        for (const auto& thing : doc.level.things)
        {
            const auto* type = sed::level::thingFindValue(doc.level, thing, "type");
            const std::string typeText = type ? (type->stringValue.empty() ? type->asString() : type->stringValue)
                                              : std::string{};
            if (sed::text::iequalsAscii(typeText, "player") || sed::text::iequalsAscii(thing.templateName, "walkplayer"))
                ++playerStarts;
        }
        const std::uint32_t spare = doc.level.exportBufferExtras.materials;
        if (playerStarts >= 2 && spare < kRecommendedSpareMaterials)
        {
            Json row = Json::makeObject();
            row.set("severity", Json::makeString("warning"));
            row.set("code", Json::makeString("multiplayer-material-headroom"));
            row.set("playerStarts", Json::makeNumber(static_cast<double>(playerStarts)));
            row.set("spareMaterials", Json::makeNumber(static_cast<double>(spare)));
            row.set("recommended", Json::makeNumber(static_cast<double>(kRecommendedSpareMaterials)));
            row.set("message", Json::makeString(
                "This multiplayer level declares " + std::to_string(spare) + " spare world-material slots. A joining "
                "player's model and saber need slots at runtime, and with none spare the host can exit when a second "
                "player joins. Raise Materials in Level Settings > Extra Buffers; LucasArts' FFA maps keep 32."));
            row.set("evidence", Json::makeString("docs/jo_ja_import.md#qualifying-a-playable-conversion"));
            row.set("repairAuthorized", Json::makeBool(false));
            issues.array.push_back(std::move(row));
        }
    }
    if (doc.level.kind == sed::ProjectType::JKDF2 || doc.level.kind == sed::ProjectType::MOTS
        || doc.level.kind == sed::ProjectType::DroidWorks)
    {
        // The Sith engine's surface loader refuses a face with more than 24
        // corners, and a refused face fails the whole level load. Cleaving a
        // room hands its cut points to the neighbouring faces, so a busy wall
        // can pass the limit without anyone drawing a 25-sided face.
        constexpr std::size_t kSithMaxSurfaceCorners = 24;
        for (std::size_t s = 0; s < doc.level.sectors.size(); ++s)
        {
            const auto& surfaces = doc.level.sectors[s].surfaces;
            for (std::size_t f = 0; f < surfaces.size(); ++f)
            {
                const std::size_t corners = surfaces[f].vertices.size();
                if (corners <= kSithMaxSurfaceCorners) continue;
                Json row = Json::makeObject();
                row.set("severity", Json::makeString("error"));
                row.set("code", Json::makeString("surface-corner-limit"));
                row.set("sector", Json::makeNumber(static_cast<double>(s)));
                row.set("surface", Json::makeNumber(static_cast<double>(f)));
                row.set("corners", Json::makeNumber(static_cast<double>(corners)));
                row.set("limit", Json::makeNumber(static_cast<double>(kSithMaxSurfaceCorners)));
                row.set("message", Json::makeString(
                    "The engine refuses to load a level with a surface of more than 24 corners"));
                row.set("repairAuthorized", Json::makeBool(false));
                issues.array.push_back(std::move(row));
                hasErrors = true;
            }
        }
    }
    if (fans.status != sed::level::FanDegeneracyStatus::Complete)
    {
        Json row = Json::makeObject();
        row.set("severity", Json::makeString("error"));
        row.set("code", Json::makeString("fan-degeneracy-scan-incomplete"));
        row.set("message", Json::makeString(
            "Fan inspection did not complete; any fan findings are a prefix, not a clean bill"));
        row.set("status", Json::makeString(fanStatus));
        issues.array.push_back(std::move(row));
        hasErrors = true;
    }
    // Serializability is part of structural validity: exact bytes checked.
    (void)buildOutput(doc, doc.level);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("validate"));
    out.set("valid", Json::makeBool(!hasErrors));
    if ( strict )
    {
        out.set("rawStructureStatus", Json::makeString("checked"));
        out.set("rawStructureScope", Json::makeString("materials-and-georesource"));
    }
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("seamStatus", Json::makeString(seamStatus));
    Json seamWorkLimits = Json::makeObject();
    seamWorkLimits.set("maxSectorPairs", Json::makeNumber(static_cast<double>(seamLimits.maxSectorPairs)));
    seamWorkLimits.set("maxSurfacePairs", Json::makeNumber(static_cast<double>(seamLimits.maxSurfacePairs)));
    seamWorkLimits.set("maxEdgePairs", Json::makeNumber(static_cast<double>(seamLimits.maxEdgePairs)));
    out.set("seamLimits", std::move(seamWorkLimits));
    out.set("fanDegeneracyStatus", Json::makeString(fanStatus));
    Json fanWorkLimits = Json::makeObject();
    fanWorkLimits.set("maxFanTriangles", Json::makeNumber(static_cast<double>(fanLimits.maxFanTriangles)));
    fanWorkLimits.set("maxIssues", Json::makeNumber(static_cast<double>(fanLimits.maxIssues)));
    out.set("fanDegeneracyLimits", std::move(fanWorkLimits));
    out.set("cogReferenceStatus", Json::makeString(cogStatus));
    out.set("assetNotices", std::move(assetNotices));
    out.set("issues", std::move(issues));
    Json counts = Json::makeObject();
    counts.set("sectors", Json::makeNumber(static_cast<double>(doc.level.sectors.size())));
    counts.set("things", Json::makeNumber(static_cast<double>(doc.level.things.size())));
    counts.set("cogs", Json::makeNumber(static_cast<double>(doc.level.cogs.size())));
    out.set("counts", std::move(counts));
    return out;
}

Json cmdNew(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "new requires <new-output.jkl|new-output.sed|new-output.jed> [mots|jk|droidworks|new.json|-]");
    const auto outputPath = levelPath(args[0]);
    const std::string outExt = lower(pathToUtf8(outputPath.extension()));
    if (outExt != ".jkl" && outExt != ".ndy" && outExt != ".sed" && outExt != ".jed")
        fail(kExitUsage, "new output must end in .jkl, .ndy, .sed or .jed");
    std::string kindText = "mots";
    // Optional starter-room geometry. Omitted fields keep the legacy
    // scaffold exactly; either field opts into the configured build.
    bool roomConfigured = false;
    sed::TVector roomOrigin{ -1.0, -1.0, -1.0 };
    sed::TVector roomSize{ 2.0, 2.0, 2.0 };
    if (args.size() > 1)
    {
        const auto requestPath = levelPath(args[1]);
        if (requestPath == "-" || lower(pathToUtf8(requestPath.extension())) == ".json")
        {
            const Json request = readRequestFile(requestPath);
            if (!request.isObject()) fail(kExitUsage, "new request must be a JSON object");
            checkSchemaVersion(request);
            if (auto k = optionalString(request, "kind")) kindText = lower(*k);
            else if (auto k = optionalString(request, "project")) kindText = lower(*k);
            else if (auto k = optionalString(request, "game")) kindText = lower(*k);
            if (request.find("origin"))
            {
                roomOrigin = pointField(request, "origin");
                roomConfigured = true;
            }
            if (request.find("size"))
            {
                roomSize = pointField(request, "size");
                roomConfigured = true;
            }
            if (roomConfigured)
            {
                const auto finite = [](const sed::TVector& v) {
                    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
                };
                if (!finite(roomOrigin))
                    fail(kExitUsage, "new 'origin' components must be finite");
                if (!finite(roomSize) || roomSize.x <= 0.0 || roomSize.y <= 0.0 || roomSize.z <= 0.0
                    || roomSize.x > 100000.0 || roomSize.y > 100000.0 || roomSize.z > 100000.0)
                    fail(kExitUsage, "new 'size' components must be finite and in (0, 100000]");
            }
        }
        else kindText = lower(args[1]);
    }
    if ( !isNewLevelKind(kindText) )
        fail(kExitUsage, "new kind must be 'mots', 'jk' or 'droidworks'");
    const bool isDroidWorks = kindText == "droidworks";
    if (outExt == ".ndy")
        failCoded(kExitRefusal, "outputFormatMismatch", "NDY is the IJIM text format; new JK, MotS and DroidWorks projects require .jkl or .sed");
    checkNewOutputTarget(outputPath, outputPath, false);
    // Reuse the GUI/core primitive builder: a usable new level needs a closed,
    // convex volume so spatial selection and connected extrusion can work.
    sed::Level level;
    level.kind = isDroidWorks
        ? sed::ProjectType::DroidWorks
        : kindText == "mots" ? sed::ProjectType::MOTS : sed::ProjectType::JKDF2;
    // Start from the editor's new-project header and spare resource slots.
    // A zeroed header exports MIPMAP/LOD/PERSPECTIVE/GOURAUD distances of 0,
    // which makes the engines use the coarsest texture and model detail.
    sed::level::applyNewProjectHeaderDefaults(level, level.kind);
    level.exportBufferExtras = sed::level::defaultNewProjectExportBufferExtras(level.kind);
    // No real MAT is mounted for an asset-free scaffold. Keep explicit pixel
    // UVs, as JKL imports do, rather than requiring invented MAT dimensions
    // during its first engine-format export.
    level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
    level.masterCMP = "scaffold.cmp";
    level.colormaps = { level.masterCMP };
    level.materials = { "scaffold.mat" };
    try
    {
        if (!roomConfigured)
        {
            (void)sed::level::levelCreateCube(
                level, -1.0, -1.0, -1.0, { 0.0, 0.0, 1.0 }, { 2.0, 0.0, 0.0 });
        }
        else
        {
            // Compose the requested box from checked core operations only:
            // a unit cube at the world origin, one checked scale per axis
            // (unit factors keep the far corner at exactly the requested
            // size), then a checked translation to the requested origin.
            (void)sed::level::levelCreateCube(
                level, 0.0, 0.0, 0.0, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 });
            const std::array<std::int32_t, 1> onlySector{ 0 };
            const auto scaleAxis = [&level, &onlySector](sed::ScaleHow how, double factor) {
                if (factor == 1.0) return;
                sed::ScaleData scale;
                scale.sfactor = factor;
                scale.how = how;
                // The room is being sized, not retextured: keep its density
                // factor at 1 and map it once it has its final corners.
                if (!sed::level::levelScaleSectorsChecked(level, std::span(onlySector), scale, false))
                    throw std::runtime_error("checked starter-room scale was refused");
            };
            scaleAxis(sed::ScaleHow::x, roomSize.x);
            scaleAxis(sed::ScaleHow::y, roomSize.y);
            scaleAxis(sed::ScaleHow::z, roomSize.z);
            if (roomOrigin.x != 0.0 || roomOrigin.y != 0.0 || roomOrigin.z != 0.0)
            {
                if (!sed::level::levelTranslateSectorsChecked(
                        level, std::span(onlySector), roomOrigin.x, roomOrigin.y, roomOrigin.z))
                    throw std::runtime_error("checked starter-room translation was refused");
            }
        }
        auto& room = level.sectors.front();
        // levelCreateCube keeps the classic per-face CreateCubeSec mapping for
        // JED parity, and scaling moves corners but not texture coordinates.
        // Give the room the new-face mapping every RED builder uses, instead
        // of one 320-texel tile stretched across each configured face.
        for (auto& surface : room.surfaces)
        {
            if (!sed::level::surfaceApplyNewFaceUv(sed::level::levelPixelPerUnit(level), room, surface))
                throw std::runtime_error("starter-room texture mapping was refused");
        }
        room.ambient = { 0.25f, 0.25f, 0.25f, 0.25f };
        room.colormap = level.masterCMP;
        for (auto& surface : room.surfaces)
        {
            surface.material = "scaffold.mat";
            surface.nMat = 0;
        }
    }
    catch (const std::exception& e) { fail(kExitInput, std::string("cannot build scaffold: ") + e.what()); }
    if (isDroidWorks)
    {
        // This scaffold is model-created, not an imported retail map. Clear its
        // import baseline before applying DroidWorks-specific creation defaults.
        level.retainedJklSourceText.clear();
        level.retainedJklSourceHash.clear();
        level.retainedJklUnknownSections.clear();
        level.masterCMP = "mission.cmp";
        level.colormaps = { "mission.cmp" };
        level.materials = { "dflt.mat" };
        level.sectors.front().colormap = level.masterCMP;
        for (auto& surface : level.sectors.front().surfaces)
        {
            surface.material = "dflt.mat";
            surface.nMat = 0;
        }
        sed::Thing& player = level.newThing();
        player.sector = 0;
        if (roomConfigured)
        {
            player.x = roomOrigin.x + roomSize.x / 2.0;
            player.y = roomOrigin.y + roomSize.y / 2.0;
            player.z = roomOrigin.z + roomSize.z / 2.0;
        }
        player.templateName = "walkplayer";
        player.name = "walkplayer";
    }
    std::string output;
    try
    {
        if (outExt == ".sed")
        {
            const auto bytes = sed::assets::saveToSedBytes(level);
            output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            (void)sed::assets::loadFromSedBytes(bytes);
        }
        else if (outExt == ".jed")
        {
            output = buildJedOutput(level);
        }
        else
        {
            const auto built = sed::assets::buildJklExport(level);
            std::istringstream in(built.text);
            sed::assets::JklLoadOptions validationOptions{};
            validationOptions.projectType = level.kind;
            (void)sed::assets::loadFromJKL(in, validationOptions);
            output = built.text;
        }
    }
    catch (const std::exception& e) { fail(kExitInput, std::string("cannot serialize new level: ") + e.what()); }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("new"));
    out.set("output", Json::makeString(pathToUtf8(outputPath)));
    out.set("outputSha256", Json::makeString(sha256Hex(output)));
    out.set("kind", Json::makeString(
        isDroidWorks ? "droidworks" : kindText == "mots" ? "mots" : "jk"));
    Json originJson = Json::makeArray();
    originJson.array = { Json::makeNumber(roomOrigin.x), Json::makeNumber(roomOrigin.y),
        Json::makeNumber(roomOrigin.z) };
    out.set("origin", std::move(originJson));
    Json sizeJson = Json::makeArray();
    sizeJson.array = { Json::makeNumber(roomSize.x), Json::makeNumber(roomSize.y),
        Json::makeNumber(roomSize.z) };
    out.set("size", std::move(sizeJson));
    writeNewFileAtomic(outputPath, output);
    return out;
}

Json cmdConvert(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "convert requires <input> <new-output> [expected-output-sha256]");
    const fs::path inputPath = args[0];
    const fs::path outputPath = args[1];
    const std::string outExt = extensionOf(outputPath);
    if (outExt != ".jkl" && outExt != ".ndy" && outExt != ".sed" && outExt != ".jed")
        fail(kExitUsage, "convert output must end in .jkl, .ndy, .sed or .jed");
    const auto doc = openDocument(inputPath);
    std::string output;
    Json jedLosses = Json::makeArray();
    if (outExt != ".sed" && outExt != ".jed")
    {
        const std::string expected = doc.level.kind == sed::ProjectType::IJIM ? ".ndy" : ".jkl";
        if (outExt != expected)
            failCoded(kExitRefusal, "outputFormatMismatch", "Conversion output must end in " + expected + " for this project's text grammar");
    }
    try
    {
        if (outExt == ".sed")
        {
            output = buildNativeOutput(doc, doc.level);
        }
        else if (outExt == ".jed")
        {
            output = buildJedOutput(doc.level, nullptr, &jedLosses);
        }
        else
        {
            const auto built = sed::assets::buildJklExport(doc.level);
            std::istringstream in(built.text);
            (void)sed::assets::loadFromJKL(in);
            output = built.text;
        }
    }
    catch (const std::exception& e) { fail(kExitInput, std::string("cannot convert document: ") + e.what()); }
    const std::string outputSha = sha256Hex(output);
    if (args.size() > 2 && lower(args[2]) != outputSha)
        fail(kExitConflict, "expected outputSha256 does not match the converted output");
    if (!jedLosses.array.empty() && args.size() <= 2)
    {
        std::string what;
        for (const Json& row : jedLosses.array)
            what += (what.empty() ? "" : ", ") + std::to_string(static_cast<long long>(row.find("count")->number)) + " "
                + row.find("what")->str;
        failCoded(kExitRefusal, "jedLossUnacknowledged", "JED cannot hold this level's " + what
            + "; convert again with expected outputSha256 " + outputSha + " to write it anyway");
    }
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath, output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("convert"));
    if (outExt == ".jed") out.set("jedLosses", jedLosses);
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(outputSha));
    return out;
}

sed::assets::GobArchive openCapturedGobOrFail(const fs::path& path, const std::string& bytes)
{
    admitCapturedArchive(bytes, path);
    try { return sed::assets::GobArchive::fromBytes(toBytes(bytes), path); }
    catch (const std::exception& e) {
        failCoded(kExitUsage, "invalid_request",
                  "Resource archive is not a readable GOB/GOO archive: " + path.string() + ". " + e.what());
    }
}

sed::assets::GobArchive openGobOrFail(const fs::path& path)
{
    return openCapturedGobOrFail(path, readArchiveBytes(path));
}

Json cmdArchive(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "archive requires <archive.gob|archive.goo>");
    const fs::path path = args[0];
    const std::string bytes = readArchiveBytes(path);
    const auto gob = openCapturedGobOrFail(path, bytes);
    std::size_t remainingBytes = kMaxExpandedArchiveBytes;
    for (const auto& entry : gob.entries())
    {
        if (entry.size > remainingBytes)
            failCoded(kExitInput, "archiveExpansionLimit", "Archive row hashes exceed the aggregate payload budget");
        remainingBytes -= entry.size;
    }
    Json entries = Json::makeArray();
    for (const auto& entry : gob.entries())
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(entry.name));
        row.set("size", Json::makeNumber(static_cast<double>(entry.size)));
        row.set("offset", Json::makeNumber(static_cast<double>(entry.offset)));
        const auto sha = sha256Hex(std::string_view(bytes).substr(entry.offset, entry.size));
        row.set("sha256", Json::makeString(sha));
        entries.array.push_back(std::move(row));
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("archive"));
    out.set("archive", Json::makeString(path.string()));
    out.set("archiveSha256", Json::makeString(sha256Hex(bytes)));
    out.set("count", Json::makeNumber(static_cast<double>(gob.entries().size())));
    out.set("entries", std::move(entries));
    return out;
}

Json cmdExtract(const std::vector<std::string>& args)
{
    if (args.size() < 3) fail(kExitUsage, "extract requires <archive> <exact-entry-name> <new-output>");
    const fs::path archivePath = args[0];
    const auto gob = openGobOrFail(archivePath);
    std::vector<std::byte> raw;
    try { raw = gob.read(args[1]); }
    catch (const std::exception& e) { fail(kExitInput, std::string("cannot extract entry: ") + e.what()); }
    std::string bytes(reinterpret_cast<const char*>(raw.data()), raw.size());
    const fs::path outputPath = args[2];
    checkNewOutputTarget(archivePath, outputPath, true);
    writeNewFileAtomic(outputPath, bytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("extract"));
    out.set("entry", Json::makeString(args[1]));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("size", Json::makeNumber(static_cast<double>(bytes.size())));
    out.set("outputSha256", Json::makeString(sha256Hex(bytes)));
    out.set("entrySha256", Json::makeString(sha256Hex(bytes)));
    return out;
}

Json cmdLaunchPlan(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "launch-plan requires <launch-request.json|->");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "launch-plan request must be a JSON object");
    checkSchemaVersion(request);
    const std::string game = requiredString(request, "game", "launch-plan request");
    if (game != "JediKnight" && game != "MysteriesOfTheSith")
        fail(kExitUsage, "launch-plan game must be JediKnight or MysteriesOfTheSith");
    const std::string gameDir = requiredString(request, "gameDirectory", "launch-plan request");
    const std::string projectDir = requiredString(request, "projectDirectory", "launch-plan request");
    const std::string levelName = requiredString(request, "levelFileName", "launch-plan request");
    const std::string executable = requiredString(request, "executable", "launch-plan request");
    std::string engine = "Auto";
    if (auto e = optionalString(request, "engine")) engine = *e;
    if (engine != "Auto" && engine != "OpenJKDF2" && engine != "Retail")
        fail(kExitUsage, "launch-plan engine must be Auto, OpenJKDF2 or Retail");
    sed::level::GameLaunchRequest input{};
    input.game = game == "MysteriesOfTheSith" ? sed::ProjectType::MOTS : sed::ProjectType::JKDF2;
    input.engine = engine == "Retail" ? sed::level::GameTestEngine::Retail
        : engine == "OpenJKDF2" ? sed::level::GameTestEngine::OpenJKDF2
        : sed::level::GameTestEngine::Auto;
    input.gameDirectory = fs::path(gameDir);
    input.projectDirectory = fs::path(projectDir);
    input.executable = fs::path(executable);
    input.levelFileName = levelName;
#ifdef _WIN32
    std::wstring shortPath(32768, L'\0');
    const DWORD count = GetShortPathNameW(input.projectDirectory.c_str(),
        shortPath.data(), static_cast<DWORD>(shortPath.size()));
    if ( count > 0 && count < shortPath.size() )
    {
        shortPath.resize(count);
        input.shortProjectPath = fs::path(shortPath).string();
    }
#endif
    sed::level::GameLaunchPlan plan;
    try
    {
        plan = sed::level::prepareGameLaunch(input);
    }
    catch ( const std::invalid_argument& error )
    {
        fail(kExitUsage, error.what());
    }
    Json requiredFiles = Json::makeArray();
    for ( const auto& path : plan.requiredFiles )
    {
        requiredFiles.array.push_back(Json::makeString(path.string()));
    }
    Json checks = Json::makeObject();
    checks.set("gameDirectoryExists", Json::makeBool(plan.gameDirectoryExists));
    checks.set("executableExists", Json::makeBool(plan.executableExists));
    checks.set("levelExists", Json::makeBool(plan.levelExists));
    checks.set("episodeExists", Json::makeBool(plan.episodeExists));
    checks.set("installMarkerExists", Json::makeBool(plan.installMarkerExists));
    Json arguments = Json::makeArray();
    for ( const auto& argument : plan.arguments )
    {
        arguments.array.push_back(Json::makeString(argument));
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("launch-plan"));
    out.set("game", Json::makeString(game));
    out.set("engine", Json::makeString(plan.engine == sed::level::GameTestEngine::OpenJKDF2
        ? "OpenJKDF2" : "Retail"));
    out.set("executable", Json::makeString(plan.executable.string()));
    out.set("workingDirectory", Json::makeString(plan.workingDirectory.string()));
    out.set("projectPathToken", Json::makeString(plan.projectPathToken));
    out.set("arguments", std::move(arguments));
    out.set("requiredFiles", std::move(requiredFiles));
    out.set("checks", std::move(checks));
    out.set("ready", Json::makeBool(plan.ready));
    out.set("directMapStart", Json::makeBool(plan.directMapStart));
    out.set("launches", Json::makeBool(false));
    out.set("note", Json::makeString(plan.directMapStart
        ? "Read-only plan for OpenJKDF2 v0.9.9-compatible direct map start; no process was launched and nothing was written."
        : "Read-only retail plan; start a new game and select the test episode in the game menu. No process was launched and nothing was written."));
    return out;
}

struct SnapshotRenderOutcome
{
    sed::render::SoftwareSnapshotResult result{};
    int materialsResolved{};
    int materialsMissing{};
    Json thingPreview{};
    // Baked lighting only: one row per non-master colormap slot.
    Json colormapTints{};
    Json indexedResources{};
};

void reportIndexedSnapshot(Json& out, const SnapshotRenderOutcome& render, const bool fullbright)
{
    out.set("appliedProfile", Json::makeString("indexed8bit"));
    out.set("renderer", Json::makeString("software-indexed8bit-v1"));
    out.set("target", Json::makeString("JKDF2"));
    out.set("lightingSource", Json::makeString(fullbright ? "fullbright" : "baked-scalar"));
    out.set("indexedResources", render.indexedResources);
}

std::string entryBasename(const std::string& name)
{
    const std::size_t at = name.find_last_of("\\/");
    return lower(at == std::string::npos ? name : name.substr(at + 1));
}

// The indexed profile resolves every selected resource from captured bytes.
SnapshotRenderOutcome renderIndexedSnapshot(const OpenedDocument& doc, const CogArchiveSet& set,
                                             sed::render::SoftwareSnapshotOptions options)
{
    if (doc.level.kind != sed::ProjectType::JKDF2)
        throw std::runtime_error("indexed8bit requires a JKDF2 level");
    constexpr std::size_t maxResourceBytes = 16u * 1024u * 1024u;
    constexpr std::size_t totalBudget = 64u * 1024u * 1024u;
    std::size_t sourceBytes{}, retainedBytes{};
    SnapshotRenderOutcome outcome{};
    outcome.indexedResources = Json::makeArray();
    const auto capture = [&](const std::string& name, const int cmpSlot) -> std::string_view
    {
        const std::string key = entryBasename(name);
        for (std::size_t a = 0; a < set.archives.size(); ++a)
        {
            for (const auto& entry : set.archives[a])
            {
                if (entryBasename(entry.entryName) != key) continue;
                if (entry.text.size() > maxResourceBytes || entry.text.size() > totalBudget - sourceBytes)
                    throw std::runtime_error("indexed8bit source budget exceeded: " + name);
                sourceBytes += entry.text.size();
                Json row = Json::makeObject();
                row.set("archiveIndex", Json::makeNumber(static_cast<double>(a)));
                row.set("entry", Json::makeString(entry.entryName));
                row.set("sha256", Json::makeString(sha256Hex(entry.text)));
                if (cmpSlot >= 0) row.set("colormapSlot", Json::makeNumber(cmpSlot));
                outcome.indexedResources.array.push_back(std::move(row));
                return entry.text;
            }
        }
        throw std::runtime_error("indexed8bit missing resource: " + name);
    };
    auto& slots = options.indexedColormapNames;
    slots = doc.level.colormaps;
    if (slots.empty()) slots.push_back(doc.level.getMasterCMP());
    if (slots.size() > 256) throw std::runtime_error("indexed8bit CMP slot budget exceeded");
    for (const auto& sector : doc.level.sectors)
    {
        if (sector.colormapIndex != -1 || sector.colormap.empty() || lower(sector.colormap) == "none") continue;
        const auto found = std::find_if(slots.begin(), slots.end(), [&](const std::string& slot) {
            return sed::text::iequalsAscii(slot, sector.colormap);
        });
        if (found != slots.end()) continue;
        if (slots.size() >= 256) throw std::runtime_error("indexed8bit CMP slot budget exceeded: " + sector.colormap);
        slots.push_back(sector.colormap);
    }
    for (std::size_t slot = 0; slot < slots.size(); ++slot)
    {
        const auto& name = slots[slot];
        const auto bytes = capture(name, static_cast<int>(slot));
        const auto cmp = sed::assets::parseCmp(std::as_bytes(std::span(bytes)));
        if (!cmp) throw std::runtime_error("indexed8bit invalid or incomplete CMP: " + name);
        options.indexedColormaps.push_back(*cmp);
    }
    std::map<std::string, sed::render::SnapshotMaterial> cache;
    const sed::render::SnapshotMaterialResolver resolve = [&](const std::string_view name)
        -> const sed::render::SnapshotMaterial*
    {
        const auto key = entryBasename(std::string(name));
        if (const auto it = cache.find(key); it != cache.end()) return &it->second;
        if (cache.size() >= 512) throw std::runtime_error("indexed8bit MAT count budget exceeded: " + key);
        const auto bytes = std::as_bytes(std::span(capture(key, -1)));
        sed::render::SnapshotMaterial material;
        try
        {
            material.indexed = sed::assets::MatFile::parseIndexedSnapshotCel({bytes.begin(), bytes.end()});
        }
        catch (const std::runtime_error& error)
        {
            throw std::runtime_error("indexed8bit MAT '" + key + "': " + error.what());
        }
        if (material.indexed->indices.size() > totalBudget - retainedBytes)
            throw std::runtime_error("indexed8bit texture plane budget exceeded: " + key);
        retainedBytes += material.indexed->indices.size();
        return &cache.emplace(key, std::move(material)).first->second;
    };
    options.indexed8bit = true;
    options.maxRasterSamples = 256000000;
    outcome.result = sed::render::renderSoftwareSnapshot(doc.level, resolve, options);
    outcome.materialsResolved = outcome.result.materialsUsed;
    return outcome;
}

// Renders one snapshot PNG in memory. Materials resolve from the
// supplied archives by entry basename; the master palette (level
// World Colormap 0 else dflt.cmp, per getMasterCMP) decodes indexed
// MATs. Unresolvable materials flat-shade and are counted.
SnapshotRenderOutcome renderSnapshotPng(const OpenedDocument& doc, const CogArchiveSet& set,
                                        const sed::render::SnapshotCamera& camera,
                                        const int width, const int height,
                                        const bool fullbright, const bool cullBackFaces,
                                        const bool staticThings,
                                        const sed::render::SnapshotLighting lighting,
                                        const bool drawPlayerStarts, const bool indexed)
{
    if (indexed)
    {
        sed::render::SoftwareSnapshotOptions options{};
        options.width = width;
        options.height = height;
        options.camera = camera;
        options.lighting = lighting;
        options.cullBackFaces = cullBackFaces;
        options.fullbright = fullbright;
        return renderIndexedSnapshot(doc, set, std::move(options));
    }
    std::optional<sed::assets::CmpPalette> palette{};
    Json resourceUses = Json::makeArray();
    const auto recordResource = [&](const std::size_t archiveIndex,
                                    const sed::assets::CogScriptCandidate& entry)
    {
        if ( !staticThings ) return;
        Json row = Json::makeObject();
        row.set("archiveIndex", Json::makeNumber(static_cast<double>(archiveIndex)));
        row.set("entry", Json::makeString(entry.entryName));
        row.set("sha256", Json::makeString(sha256Hex(entry.text)));
        resourceUses.array.push_back(std::move(row));
    };
    const std::string master = entryBasename(doc.level.getMasterCMP());
    const sed::assets::CogScriptCandidate* paletteSource = nullptr;
    std::size_t paletteArchive{};
    for ( std::size_t a = 0; a < set.archives.size(); ++a )
    {
        for (const auto& e : set.archives[a])
        {
            if (entryBasename(e.entryName) == master)
            {
                std::vector<std::byte> raw(e.text.size());
                for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::byte>(e.text[i]);
                palette = sed::assets::parseCmpPalette(raw);
                paletteSource = &e;
                paletteArchive = a;
            }
        }
    }
    if ( paletteSource ) recordResource(paletteArchive, *paletteSource);
    std::map<std::string, sed::render::SnapshotMaterial> cache{};
    std::size_t textureSourceBytes{};
    std::size_t textureDecodedBytes{};
    constexpr std::size_t maxTextureBytes = 64u * 1024u * 1024u;
    Json materialIssues = Json::makeArray();
    std::map<std::string, bool> tried{};
    const sed::render::SnapshotMaterialResolver resolver =
        [&](const std::string_view name) -> const sed::render::SnapshotMaterial* {
        const std::string key = entryBasename(std::string(name));
        if (auto it = cache.find(key); it != cache.end()) return &it->second;
        if ( tried.contains(key) ) return nullptr;
        if ( staticThings && tried.size() >= 512 )
            throw std::runtime_error("snapshot: texture lookup budget exceeded");
        tried[key] = true;
        for ( std::size_t a = 0; a < set.archives.size(); ++a )
        {
            for (const auto& e : set.archives[a])
            {
                if (entryBasename(e.entryName) != key) continue;
                if ( staticThings )
                {
                    if ( e.text.size() > 16u * 1024u * 1024u
                        || e.text.size() > maxTextureBytes - textureSourceBytes )
                        throw std::runtime_error("snapshot: texture source budget exceeded");
                    textureSourceBytes += e.text.size();
                    recordResource(a, e);
                }
                try
                {
                    std::vector<std::byte> raw(e.text.size());
                    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::byte>(e.text[i]);
                    const sed::assets::MatFile mat = sed::assets::MatFile::parse(
                        std::move(raw), palette ? &*palette : nullptr);
                    sed::render::SnapshotMaterial material{};
                    if ( staticThings )
                    {
                        const auto& cel = mat.cel(0);
                        const auto decoded = static_cast<std::uint64_t>(cel.width)
                            * static_cast<std::uint64_t>(cel.height) * 4u;
                        if ( cel.width <= 0 || cel.height <= 0 || decoded > maxTextureBytes - textureDecodedBytes )
                            fail(kExitInput, "snapshot: decoded texture cache budget exceeded");
                        textureDecodedBytes += static_cast<std::size_t>(decoded);
                    }
                    material.image = mat.decodeCelRgba8(0);
                    material.trueColor = mat.isTrueColor();
                    material.alphaBpp = static_cast<std::int32_t>(mat.colorInfo().alphaBPP);
                    auto [it, inserted] = cache.emplace(key, std::move(material));
                    return &it->second;
                }
                catch (const std::exception& error)
                {
                    if ( staticThings )
                    {
                        Json issue = Json::makeObject();
                        issue.set("resource", Json::makeString(key));
                        issue.set("status", Json::makeString("invalid-material"));
                        issue.set("message", Json::makeString(error.what()));
                        materialIssues.array.push_back(std::move(issue));
                    }
                    return nullptr;
                }
            }
        }
        if ( staticThings )
        {
            Json issue = Json::makeObject();
            issue.set("resource", Json::makeString(key));
            issue.set("status", Json::makeString("missing-material"));
            materialIssues.array.push_back(std::move(issue));
        }
        return nullptr;
    };
    sed::render::SoftwareSnapshotOptions options{};
    options.width = width;
    options.height = height;
    options.camera = camera;
    options.fullbright = fullbright;
    options.cullBackFaces = cullBackFaces;
    options.lighting = lighting;
    SnapshotRenderOutcome outcome{};
    if ( lighting == sed::render::SnapshotLighting::baked )
    {
        // The hardware renderer multiplies a sector on any colormap but the
        // master (slot 0) by that colormap's header tint. Each slot's CMP
        // resolves like a material: first archive/entry basename match.
        outcome.colormapTints = Json::makeArray();
        options.colormapTints.assign(doc.level.colormaps.size(), sed::ColorF{ 1.0f, 1.0f, 1.0f });
        for ( std::size_t slot = 1; slot < doc.level.colormaps.size(); ++slot )
        {
            const std::string key = entryBasename(doc.level.colormaps[slot]);
            Json row = Json::makeObject();
            row.set("colormap", Json::makeNumber(static_cast<double>(slot)));
            row.set("name", Json::makeString(doc.level.colormaps[slot]));
            std::string status = "missing";
            for ( std::size_t a = 0; a < set.archives.size() && status == "missing"; ++a )
            {
                for ( const auto& e : set.archives[a] )
                {
                    if ( entryBasename(e.entryName) != key ) continue;
                    recordResource(a, e);
                    const auto tint = sed::assets::parseCmpTint(std::as_bytes(std::span(e.text.data(), e.text.size())));
                    if ( !tint )
                    {
                        status = "invalid";
                        break;
                    }
                    status = "resolved";
                    options.colormapTints[slot] = sed::ColorF{ tint->r, tint->g, tint->b };
                    Json rgb = Json::makeArray();
                    rgb.array.push_back(Json::makeNumber(tint->r));
                    rgb.array.push_back(Json::makeNumber(tint->g));
                    rgb.array.push_back(Json::makeNumber(tint->b));
                    row.set("tint", std::move(rgb));
                    break;
                }
            }
            row.set("status", Json::makeString(status));
            outcome.colormapTints.array.push_back(std::move(row));
        }
    }
    sed::render::SnapshotThingScene things;
    if ( staticThings )
    {
        // A converted level may have hundreds of small, independently named
        // architecture models. Index only visual assets, once per archive set.
        // try_emplace preserves the old first-archive, first-entry precedence.
        using VisualEntry = std::pair<std::size_t, const sed::assets::CogScriptCandidate*>;
        std::map<std::string, VisualEntry> visuals;
        for ( std::size_t a = 0; a < set.archives.size(); ++a )
        {
            for ( const auto& entry : set.archives[a] )
            {
                auto key = entryBasename(entry.entryName);
                if ( key.ends_with(".3do") || key.ends_with(".spr") )
                {
                    visuals.try_emplace(std::move(key), a, &entry);
                }
            }
        }
        const sed::render::SnapshotThingResourceResolver resources = [&](const std::string_view name)
            -> std::optional<std::string_view>
        {
            const auto found = visuals.find(entryBasename(std::string(name)));
            if ( found == visuals.end() ) return std::nullopt;
            const auto& [archive, entry] = found->second;
            recordResource(archive, *entry);
            return entry->text;
        };
        sed::render::SnapshotThingOptions thingOptions;
        thingOptions.drawPlayerStarts = drawPlayerStarts;
        things = sed::render::prepareSnapshotThings(doc.level, camera, resources,
            sed::render::SnapshotThingLimits::forIndexedArchives(), thingOptions);
        options.maxRasterSamples = 256000000;
    }
    outcome.result = sed::render::renderSoftwareSnapshot(doc.level, resolver, options, things.geometry);
    if ( staticThings )
    {
        Json report = Json::makeObject();
        report.set("profile", Json::makeString("static-lod0-cel0-sector-ambient"));
        report.set("playerStarts", Json::makeString(drawPlayerStarts ? "draw" : "omit"));
        report.set("modelsPrepared", Json::makeNumber(static_cast<double>(things.models)));
        report.set("spritesPrepared", Json::makeNumber(static_cast<double>(things.sprites)));
        report.set("surfacesSubmitted", Json::makeNumber(outcome.result.thingSurfacesSubmitted));
        report.set("trianglesSubmitted", Json::makeNumber(outcome.result.thingTrianglesSubmitted));
        report.set("rasterSamples", Json::makeNumber(static_cast<double>(outcome.result.rasterSamples)));
        report.set("gameplayVerified", Json::makeBool(false));
        report.set("engineRenderingParity", Json::makeBool(false));
        report.set("resourceSelection", Json::makeString("first archive/entry basename match; master CMP last match (legacy policy)"));
        report.set("resolvedResources", std::move(resourceUses));
        report.set("materialIssues", std::move(materialIssues));
        Json rows = Json::makeArray();
        for ( const auto& item : things.diagnostics )
        {
            Json row = Json::makeObject();
            row.set("thingIndex", Json::makeNumber(static_cast<double>(item.thingIndex)));
            row.set("resource", Json::makeString(item.resource));
            row.set("status", Json::makeString(item.status));
            row.set("message", Json::makeString(item.message));
            rows.array.push_back(std::move(row));
        }
        report.set("diagnostics", std::move(rows));
        outcome.thingPreview = std::move(report);
    }
    outcome.materialsResolved = outcome.result.materialsUsed;
    outcome.materialsMissing = outcome.result.materialsMissing;
    return outcome;
}

// "ambient" (default): each surface by its sector's ambient light.
// "baked": each corner by its stored light, as the game combines it.
bool snapshotIndexedProfile(const Json& request, const bool staticThings)
{
    const auto* value = request.find("renderProfile");
    if (!value) return false;
    if (value->type != Json::Type::String || (value->str != "indexed8bit" && value->str != "software-v1"))
        fail(kExitUsage, "renderProfile must be indexed8bit or software-v1");
    if (value->str == "indexed8bit" && staticThings)
        fail(kExitUsage, "indexed8bit supports world surfaces only");
    return value->str == "indexed8bit";
}

sed::render::SnapshotLighting snapshotLighting(const Json& request, const std::string& command, const bool indexed)
{
    const Json* value = request.find("lighting");
    const bool fullbright = request.find("fullbright") && request.find("fullbright")->boolean;
    if (indexed && value && value->str == "ambient")
        fail(kExitUsage, "indexed8bit requires baked light or fullbright");
    if (!value) return indexed && !fullbright ? sed::render::SnapshotLighting::baked
                                              : sed::render::SnapshotLighting::sectorAmbient;
    if ( value->type != Json::Type::String || (value->str != "ambient" && value->str != "baked") )
        fail(kExitUsage, command + " 'lighting' must be ambient or baked");
    return value->str == "baked" ? sed::render::SnapshotLighting::baked
                                 : sed::render::SnapshotLighting::sectorAmbient;
}

const char* snapshotLightingName(const sed::render::SnapshotLighting lighting)
{
    return lighting == sed::render::SnapshotLighting::baked ? "baked" : "ambient";
}

bool snapshotStaticThings(const Json& request)
{
    const Json* mode = request.find("thingMode");
    if ( !mode ) return false;
    if ( mode->type != Json::Type::String || (mode->str != "none" && mode->str != "static") )
        fail(kExitUsage, "thingMode must be none or static");
    return mode->str == "static";
}

// Player starts are spawn points; the game never draws one in front of the
// player's eyes. Omitted unless the request asks to see where spawns are.
bool snapshotDrawPlayerStarts(const Json& request, const bool staticThings)
{
    const Json* value = request.find("playerStarts");
    if ( !value ) return false;
    if ( value->type != Json::Type::String || (value->str != "omit" && value->str != "draw") )
        fail(kExitUsage, "playerStarts must be omit or draw");
    if ( !staticThings ) fail(kExitUsage, "playerStarts requires thingMode static");
    return value->str == "draw";
}

Json cmdItemPlacementCheck(const std::vector<std::string>& args)
{
    if (args.size() != 2) fail(kExitUsage, "item-placement-check requires <level> <request.json|->");
    const auto request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "item-placement-check request must be an object");
    checkSchemaVersion(request);
    const auto* slots = request.find("things");
    if (!slots || slots->type != Json::Type::Array || slots->array.empty() || slots->array.size() > 128)
        fail(kExitUsage, "item-placement-check requires 1..128 Thing indices");
    const auto doc = openDocument(args[0]);
    checkRevision(doc, request);
    if (doc.level.kind != sed::ProjectType::JKDF2 && doc.level.kind != sed::ProjectType::MOTS)
        fail(kExitUsage, "item-placement-check requires a JK or MotS level");
    if (doc.level.templates.size() > 1024) fail(kExitUsage, "item-placement-check template budget exceeded");
    std::set<std::size_t> selected;
    for (const auto& slot : slots->array)
        if (slot.type != Json::Type::Number || !std::isfinite(slot.number) || slot.number < 0
            || slot.number >= static_cast<double>(doc.level.things.size()) || std::floor(slot.number) != slot.number
            || !selected.insert(static_cast<std::size_t>(slot.number)).second)
            fail(kExitUsage, "item-placement-check requires distinct valid integer Thing indices");
    std::set<std::size_t> supportSlots;
    if (const auto* support = request.find("supportThings"))
    {
        if (support->type != Json::Type::Array || support->array.size() > 128)
            fail(kExitUsage, "supportThings must be an array of at most 128 Thing indices");
        for (const auto& slot : support->array)
            if (slot.type != Json::Type::Number || !std::isfinite(slot.number) || slot.number < 0
                || slot.number >= static_cast<double>(doc.level.things.size()) || std::floor(slot.number) != slot.number
                || selected.contains(static_cast<std::size_t>(slot.number))
                || !supportSlots.insert(static_cast<std::size_t>(slot.number)).second)
                fail(kExitUsage, "supportThings require distinct valid Thing indices outside things");
    }
    const auto resources = loadCogArchives(request);
    std::map<std::string, const sed::assets::CogScriptCandidate*> models;
    for (const auto& archive : resources.archives)
        for (const auto& entry : archive)
            if (const auto name = entryBasename(entry.entryName); name.ends_with(".3do"))
                models.try_emplace(name, &entry);
    Json items = Json::makeArray();
    std::size_t work = 0, bytes = 0;
    const auto charge = [&](const std::size_t count) {
        if (count > 1000000 - work) fail(kExitUsage, "item-placement-check geometry budget exceeded");
        work += count;
    };
    const auto value = [&](const sed::Thing& thing, const std::string_view field) {
        return sed::level::thingFindValue(doc.level, thing, field);
    };
    const auto text = [&](const sed::Thing& thing, const std::string_view field) {
        const auto* v = value(thing, field);
        return v ? (v->stringValue.empty() ? v->asString() : v->stringValue) : std::string{};
    };
    const auto modelFor = [&](const sed::Thing& thing) -> const sed::assets::CogScriptCandidate* {
        auto name = lower(text(thing, "model3d"));
        if (name.empty()) name = lower(text(thing, "model3do"));
        if (!name.empty() && !name.ends_with(".3do")) name += ".3do";
        const auto found = models.find(entryBasename(name));
        return found == models.end() ? nullptr : found->second;
    };
    const auto capture = [&](const sed::assets::CogScriptCandidate& entry) {
        if (entry.text.size() > 8 * 1024 * 1024 || (bytes += entry.text.size()) > 64 * 1024 * 1024)
            fail(kExitUsage, "item-placement-check model byte budget exceeded");
        return sed::assets::loadFrom3DO(std::as_bytes(std::span(entry.text.data(), entry.text.size())));
    };
    struct SupportTriangle
    {
        std::size_t thing;
        std::int32_t sector;
        std::array<sed::TVector, 3> vertices;
        std::string_view sha256;
    };
    std::vector<SupportTriangle> triangles;
    std::map<std::size_t, std::string> supportHashes;
    std::set<std::int32_t> unresolvedSectors;
    for (const auto index : supportSlots)
    {
        const auto& thing = doc.level.things[index];
        if (thing.sector < 0 || static_cast<std::size_t>(thing.sector) >= doc.level.sectors.size())
            fail(kExitUsage, "supportThings contains a Thing with an invalid sector");
        const auto movement = lower(text(thing, "move"));
        const auto* flags = value(thing, "thingflags");
        const auto collide = text(thing, "collide");
        const auto* asset = modelFor(thing);
        const auto cog = text(thing, "cog");
        if (lower(text(thing, "type")) != "cog" || !flags || (flags->intValue & 0x40) == 0
            || collide != "3"
            || (movement != "none" && movement != "path")
            || (movement == "path" && (value(thing, "frame") || value(thing, "numframes")))
            || (!cog.empty() && lower(cog) != "none")
            || value(thing, "vel") || value(thing, "angvel") || value(thing, "puppet") || !asset
            || !std::isfinite(thing.x) || !std::isfinite(thing.y) || !std::isfinite(thing.z)
            || !std::isfinite(thing.pch) || !std::isfinite(thing.yaw) || !std::isfinite(thing.rol))
        { unresolvedSectors.insert(thing.sector); continue; }
        sed::assets::Model3do model;
        try { model = capture(*asset); }
        catch (const AgentError&) { throw; }
        catch (const std::runtime_error&) { unresolvedSectors.insert(thing.sector); continue; }
        if (model.insertOffset.x != 0 || model.insertOffset.y != 0 || model.insertOffset.z != 0
            || model.geosetCount != 1)
        { unresolvedSectors.insert(thing.sector); continue; }
        const auto& hash = supportHashes.emplace(index, sha256Hex(asset->text)).first->second;
        const auto transform = sed::TMatrix43::Create({thing.x, thing.y, thing.z}, {thing.pch, thing.yaw, thing.rol});
        charge(model.meshes.size());
        for (const auto& mesh : model.meshes)
        {
            charge(mesh.faces.size());
            for (const auto& face : mesh.faces)
            {
                // An invisible authored face still supplies geometry for collide=3.
                if (face.vertices.size() != 3) { unresolvedSectors.insert(thing.sector); continue; }
                std::array<sed::TVector, 3> vertices{};
                bool valid = true;
                for (std::size_t i = 0; i < vertices.size(); ++i)
                {
                    const auto v = face.vertices[i];
                    if (v < 0 || static_cast<std::size_t>(v) >= mesh.vertices.size()) { valid = false; break; }
                    const auto& p = mesh.vertices[static_cast<std::size_t>(v)];
                    vertices[i] = transform.TransformPoint({p.x, p.y, p.z});
                    valid &= std::isfinite(vertices[i].x) && std::isfinite(vertices[i].y) && std::isfinite(vertices[i].z);
                }
                if (!valid) { unresolvedSectors.insert(thing.sector); continue; }
                if (std::abs(vertices[1].z - vertices[0].z) > 1e-6
                    || std::abs(vertices[2].z - vertices[0].z) > 1e-6) continue;
                const double area = (vertices[1].x - vertices[0].x)*(vertices[2].y - vertices[0].y)
                    - (vertices[1].y - vertices[0].y)*(vertices[2].x - vertices[0].x);
                if (area > 1e-6) triangles.push_back({index, thing.sector, vertices, hash});
                if (triangles.size() > 65536) fail(kExitUsage, "supportThings triangle budget exceeded");
            }
        }
    }
    for (const auto index : selected)
    {
        const auto& thing = doc.level.things[index];
        Json row = Json::makeObject();
        row.set("thing", Json::makeNumber(static_cast<double>(index)));
        const auto finish = [&](const char* status) {
            row.set("status", Json::makeString(status));
            items.array.push_back(std::move(row));
        };
        const auto get = [&](const std::string_view field) {
            const auto* v = sed::level::thingFindValue(doc.level, thing, field);
            return v ? (v->stringValue.empty() ? v->asString() : v->stringValue) : std::string{};
        };
        const auto type = lower(get("type"));
        if (type != "item") { finish(type.empty() ? "typeUnresolved" : "notAnItem"); continue; }
        if (thing.sector < 0 || static_cast<std::size_t>(thing.sector) >= doc.level.sectors.size()
            || !std::isfinite(thing.x) || !std::isfinite(thing.y) || !std::isfinite(thing.z)
            || !std::isfinite(thing.pch) || !std::isfinite(thing.yaw) || !std::isfinite(thing.rol))
        { finish("invalidPlacement"); continue; }
        auto name = lower(get("model3d"));
        if (name.empty()) name = lower(get("model3do"));
        if (!name.empty() && !name.ends_with(".3do")) name += ".3do";
        const auto found = models.find(entryBasename(name));
        if (found == models.end()) { finish("modelUnresolved"); continue; }
        const auto& entry = *found->second;
        if (entry.text.size() > 8 * 1024 * 1024 || (bytes += entry.text.size()) > 64 * 1024 * 1024)
            fail(kExitUsage, "item-placement-check model byte budget exceeded");
        row.set("model", Json::makeString(entry.entryName));
        row.set("modelSha256", Json::makeString(sha256Hex(entry.text)));
        sed::assets::Model3do model;
        try
        {
            model = sed::assets::loadCheckedModel3doBindPose(std::as_bytes(std::span(entry.text)));
        }
        catch ( const std::runtime_error& )
        {
            finish("modelUnresolved");
            continue;
        }
        const auto transform = sed::TMatrix43::Create({thing.x, thing.y, thing.z}, {thing.pch, thing.yaw, thing.rol});
        std::optional<sed::TVector> low;
        bool invalid = false;
        charge(model.meshes.size());
        for (const auto& mesh : model.meshes)
        {
            if (!mesh.geo) continue;
            charge(mesh.faces.size());
            for (const auto& face : mesh.faces)
            {
                if (!face.geo || face.vertices.size() < 3) continue;
                charge(face.vertices.size());
                for (const auto i : face.vertices)
                {
                    if (i < 0 || static_cast<std::size_t>(i) >= mesh.vertices.size()) { invalid = true; continue; }
                    const auto& v = mesh.vertices[static_cast<std::size_t>(i)];
                    const auto p = transform.TransformPoint({v.x, v.y, v.z});
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) invalid = true;
                    else if (!low || p.z < low->z) low = p;
                }
            }
        }
        if (invalid || !low) { finish("modelUnresolved"); continue; }
        Json lowestVertex = Json::makeArray();
        for (const auto component : {low->x, low->y, low->z})
            lowestVertex.array.push_back(Json::makeNumber(component));
        row.set("lowestVertex", std::move(lowestVertex));
        const auto& sector = doc.level.sectors[static_cast<std::size_t>(thing.sector)];
        std::optional<double> floor;
        bool ambiguous = false;
        constexpr double eps = 1e-6;
        charge(sector.surfaces.size());
        std::size_t floorIndex = 0;
        for (std::size_t faceIndex = 0; faceIndex < sector.surfaces.size(); ++faceIndex)
        {
            const auto& face = sector.surfaces[faceIndex];
            if (!(face.surfaceFlags & sed::SF_Floor) || face.adjoin || !(face.normal.z > 0.999)
                || face.vertices.size() < 3 || face.vertices.size() > 24) continue;
            bool valid = true, positive = false, negative = false;
            double height = 0, area = 0;
            charge(face.vertices.size());
            for (std::size_t i = 0; i < face.vertices.size(); ++i)
            {
                const auto a = face.vertices[i], b = face.vertices[(i+1) % face.vertices.size()];
                if (a < 0 || b < 0 || static_cast<std::size_t>(std::max(a,b)) >= sector.vertices.size())
                { valid = false; break; }
                const auto& p = sector.vertices[static_cast<std::size_t>(a)];
                const auto& q = sector.vertices[static_cast<std::size_t>(b)];
                if (i == 0) height = p.z;
                if (!std::isfinite(p.z) || std::abs(p.z - height) > eps || std::abs(height - thing.z) > 0.5)
                { valid = false; break; }
                const double e = (q.x-p.x)*(low->y-p.y) - (q.y-p.y)*(low->x-p.x);
                positive |= e > eps;
                negative |= e < -eps;
                area += p.x*q.y - q.x*p.y;
            }
            if (!valid || (positive && negative) || !std::isfinite(area) || std::abs(area) < eps) continue;
            ambiguous |= floor && std::abs(*floor - height) > eps;
            if (!floor) { floor = height; floorIndex = faceIndex; }
        }
        if (unresolvedSectors.contains(thing.sector)) { finish("supportThingUnresolved"); continue; }
        const SupportTriangle* support = nullptr;
        charge(triangles.size());
        for (const auto& tri : triangles)
        {
            const double z = tri.vertices[0].z;
            if (tri.sector != thing.sector || std::abs(thing.z - z) > 0.5) continue;
            bool inside = true;
            for (std::size_t edge = 0; edge < 3; ++edge)
            {
                const auto& p = tri.vertices[edge];
                const auto& q = tri.vertices[(edge + 1) % 3];
                const double value = (q.x-p.x)*(low->y-p.y) - (q.y-p.y)*(low->x-p.x);
                if (!std::isfinite(value) || value < -eps) { inside = false; break; }
            }
            if (!inside) continue;
            ambiguous |= floor && std::abs(*floor - z) > eps;
            ambiguous |= support && support->thing != tri.thing;
            if (!floor) { floor = z; support = &tri; }
        }
        if (ambiguous) { finish("floorAmbiguous"); continue; }
        if (!floor) { finish("floorUnresolved"); continue; }
        row.set("sector", Json::makeNumber(thing.sector));
        if (support)
        {
            row.set("supportThing", Json::makeNumber(static_cast<double>(support->thing)));
            row.set("supportModelSha256", Json::makeString(std::string(support->sha256)));
        }
        else row.set("surface", Json::makeNumber(static_cast<double>(floorIndex)));
        row.set("floorZ", Json::makeNumber(*floor));
        row.set("clearance", Json::makeNumber(low->z - *floor));
        const bool below = low->z < *floor - 1e-4;
        finish(support ? (below ? "belowColliderAtSample" : "notBelowColliderAtSample")
                       : (below ? "belowSectorFloor" : "notBelowSectorFloorAtSample"));
    }
    Json report = Json::makeObject();
    report.set("command", Json::makeString("item-placement-check"));
    report.set("inputSha256", Json::makeString(doc.inputSha256));
    report.set("resourceRevision", Json::makeString(resources.revision));
    report.set("profile", Json::makeString("static-lod0-lowest-vertex-horizontal-sector-floor"));
    report.set("collisionThingsEvaluated", Json::makeBool(!triangles.empty()));
    report.set("supportThingsRequested", Json::makeNumber(static_cast<double>(supportSlots.size())));
    report.set("gameplayVerified", Json::makeBool(false));
    report.set("items", std::move(items));
    return report;
}


Json cmdSnapshot(const std::vector<std::string>& args)
{
    if (args.size() < 3) fail(kExitUsage, "snapshot requires <level> <snapshot.json|-> <new-output.png>");
    // D-08 request contract is validated before the renderer refusal:
    // projection selects exactly one of fieldOfViewDegrees /
    // orthographicHeight, and image dimensions are bounded.
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "snapshot request must be a JSON object");
    checkSchemaVersion(request);
    const bool staticThings = snapshotStaticThings(request);
    const bool indexed = snapshotIndexedProfile(request, staticThings);
    const bool drawPlayerStarts = snapshotDrawPlayerStarts(request, staticThings);
    long long width = 0, height = 0;
    if (request.find("width")) width = requiredInt(request, "width", "snapshot request");
    if (request.find("height")) height = requiredInt(request, "height", "snapshot request");
    if ((width && (width < 64 || width > 2048)) || (height && (height < 64 || height > 2048)))
        fail(kExitUsage, "snapshot width/height must be within 64..2048");
    if (width && height && width * height > 2097152)
        fail(kExitUsage, "snapshot pixel budget exceeded (max 2097152)");
    std::string projection = "perspective";
    if (auto p = optionalString(request, "projection")) projection = lower(*p);
    if (projection != "perspective" && projection != "orthographic")
        fail(kExitUsage, "snapshot 'projection' must be perspective or orthographic");
    const Json* fov = request.find("fieldOfViewDegrees");
    const Json* ortho = request.find("orthographicHeight");
    if (fov && fov->type != Json::Type::Number) fail(kExitUsage, "snapshot 'fieldOfViewDegrees' must be a number");
    if (fov && (fov->number <= 0.0 || fov->number >= 180.0))
        fail(kExitUsage, "snapshot 'fieldOfViewDegrees' must be within (0, 180)");
    if (ortho && ortho->type != Json::Type::Number) fail(kExitUsage, "snapshot 'orthographicHeight' must be a number");
    if (ortho && (ortho->number < 0.001 || ortho->number > 1000000.0))
        fail(kExitUsage, "snapshot 'orthographicHeight' must be within 0.001..1000000");
    if (projection == "orthographic" && !ortho)
        fail(kExitUsage, "snapshot orthographic projection requires 'orthographicHeight'");
    if (projection == "orthographic" && fov)
        fail(kExitUsage, "snapshot orthographic projection refuses 'fieldOfViewDegrees'");
    if (projection == "perspective" && ortho)
        fail(kExitUsage, "snapshot perspective projection refuses 'orthographicHeight'");
    for (const char* key : { "fullbright", "cullBackFaces" })
    {
        if (const Json* b = request.find(key); b && b->type != Json::Type::Bool)
            fail(kExitUsage, std::string("snapshot '") + key + "' must be a boolean");
    }
    const sed::render::SnapshotLighting lighting = snapshotLighting(request, "snapshot", indexed);
    // Render with the built-in software renderer (world surfaces
    // only; the outcome reports the scope). A PNG is written only
    // when rasterization actually happened.
    const auto doc = openDocument(args[0]);
    CogArchiveSet set;
    if (request.find("resourceArchives")) set = loadCogArchives(request);
    if (!width || !height) fail(kExitUsage, "snapshot requires integer 'width' and 'height'");
    bool fullbright = false;
    if (const Json* b = request.find("fullbright")) fullbright = b->boolean;
    bool cullBackFaces = false;
    if (const Json* b = request.find("cullBackFaces")) cullBackFaces = b->boolean;
    if ( fullbright && lighting == sed::render::SnapshotLighting::baked )
        fail(kExitUsage, "snapshot 'fullbright' and baked 'lighting' contradict each other; choose one");

    sed::render::SnapshotCamera camera{};
    camera.orthographic = (projection == "orthographic");
    if (fov) camera.fieldOfViewDegrees = fov->number;
    if (ortho) camera.orthographicHeight = ortho->number;
    const Json* camObj = request.find("camera");
    if (camObj)
    {
        if (!camObj->isObject()) fail(kExitUsage, "snapshot 'camera' must be an object");
        auto vec3Field = [&](const char* key, sed::TVector& outVec) {
            const Json* v = camObj->find(key);
            if (!v || v->type != Json::Type::Array || v->array.size() != 3)
                fail(kExitUsage, std::string("snapshot camera requires '") + key + "' [x,y,z]");
            for (int i = 0; i < 3; ++i)
            {
                if (v->array[i].type != Json::Type::Number)
                    fail(kExitUsage, std::string("snapshot camera '") + key + "' must be numbers");
            }
            outVec = sed::TVector{ v->array[0].number, v->array[1].number, v->array[2].number };
        };
        vec3Field("position", camera.position);
        vec3Field("target", camera.target);
        if (const Json* n = camObj->find("nearPlane"))
        {
            if (n->type != Json::Type::Number) fail(kExitUsage, "snapshot camera 'nearPlane' must be a number");
            camera.nearPlane = n->number;
        }
        if (const Json* fr = camObj->find("farPlane"))
        {
            if (fr->type != Json::Type::Number) fail(kExitUsage, "snapshot camera 'farPlane' must be a number");
            camera.farPlane = fr->number;
        }
    }
    else
    {
        // No explicit camera: auto-frame like overview (wren fit).
        try
        {
            const auto bounds = sed::level::worldSurfaceBounds(doc.level);
            if (!bounds) fail(kExitInput, "snapshot: level has no drawn world surfaces to frame");
            const auto framed = sed::level::computeOverviewCamera(
                *bounds,
                camera.orthographic ? sed::level::kOverviewOverhead : sed::level::kOverviewThreeQuarter,
                sed::level::kOverviewWren, static_cast<int>(width), static_cast<int>(height),
                sed::level::kOverviewDefaultMargin);
            camera.position = framed.position;
            camera.target = framed.target;
            camera.nearPlane = framed.nearPlane;
            camera.farPlane = framed.farPlane;
            if (camera.orthographic && !ortho) camera.orthographicHeight = framed.orthographicHeight;
            if (!camera.orthographic && !fov) camera.fieldOfViewDegrees = framed.fieldOfViewDegrees;
        }
        catch (const std::invalid_argument& e) { fail(kExitUsage, std::string("snapshot: ") + e.what()); }
        catch (const std::runtime_error& e) { fail(kExitInput, std::string("snapshot: ") + e.what()); }
    }

    SnapshotRenderOutcome rendered;
    try
    {
        rendered = renderSnapshotPng(doc, set, camera, static_cast<int>(width),
                                     static_cast<int>(height), fullbright, cullBackFaces, staticThings, lighting,
                                     drawPlayerStarts, indexed);
    }
    catch (const std::invalid_argument& e) { fail(kExitUsage, std::string("snapshot: ") + e.what()); }
    catch (const std::runtime_error& e) { fail(kExitInput, std::string("snapshot: ") + e.what()); }

    const fs::path outputPath = args[2];
    if (fs::exists(outputPath)) fail(kExitRefusal, "snapshot output already exists; refusing to overwrite");
    const std::string pngBytes(
        reinterpret_cast<const char*>(rendered.result.png.data()), rendered.result.png.size());
    writeNewFileAtomic(outputPath, pngBytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("snapshot"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("width", Json::makeNumber(static_cast<double>(width)));
    out.set("height", Json::makeNumber(static_cast<double>(height)));
    out.set("projection", Json::makeString(projection));
    out.set("imageSha256", Json::makeString(sha256Hex(pngBytes)));
    out.set("imageBytes", Json::makeNumber(static_cast<double>(pngBytes.size())));
    out.set("surfacesDrawn", Json::makeNumber(static_cast<double>(rendered.result.surfacesDrawn)));
    out.set("trianglesDrawn", Json::makeNumber(static_cast<double>(rendered.result.trianglesDrawn)));
    out.set("materialsResolved", Json::makeNumber(static_cast<double>(rendered.materialsResolved)));
    out.set("materialsMissing", Json::makeNumber(static_cast<double>(rendered.materialsMissing)));
    out.set("portalsSkipped", Json::makeNumber(static_cast<double>(rendered.result.portalsSkipped)));
    out.set("renderer", Json::makeString(staticThings ? "software-v1-static-things" : "software-v1"));
    out.set("renderScope", Json::makeString(staticThings ? "world-surfaces-and-static-things" : "world-surfaces"));
    if (indexed) reportIndexedSnapshot(out, rendered, fullbright);
    if ( staticThings ) out.set("thingPreview", std::move(rendered.thingPreview));
    out.set("fullbright", Json::makeBool(fullbright));
    out.set("cullBackFaces", Json::makeBool(cullBackFaces));
    if (!indexed || !fullbright) out.set("lighting", Json::makeString(snapshotLightingName(lighting)));
    if ( !indexed && lighting == sed::render::SnapshotLighting::baked )
        out.set("colormapTints", std::move(rendered.colormapTints));
    return out;
}

Json cmdOverview(const std::vector<std::string>& args)
{
    // Compute world-surface framing and a replayable snapshot request.
    // Rendering is optional and retains the selected static-Thing profile.
    if (args.size() < 2) fail(kExitUsage, "overview requires <level> <overview.json|->");
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "overview request must be a JSON object");
    checkSchemaVersion(request);
    const bool staticThings = snapshotStaticThings(request);
    const bool indexed = snapshotIndexedProfile(request, staticThings);
    if (indexed && doc.level.kind != sed::ProjectType::JKDF2)
        fail(kExitInput, "indexed8bit requires a JKDF2 level");
    const bool drawPlayerStarts = snapshotDrawPlayerStarts(request, staticThings);
    const std::string view = requiredString(request, "view", "overview request");
    std::string framing{ sed::level::kOverviewWren };
    if (auto f = optionalString(request, "framing")) framing = *f;
    const long long width = requiredInt(request, "width", "overview request");
    const long long height = requiredInt(request, "height", "overview request");
    if (indexed)
    {
        if (width < 64 || width > 2048 || height < 64 || height > 2048)
            fail(kExitUsage, "indexed overview replay requires width/height within 64..2048");
        if (width * height > 2097152)
            fail(kExitUsage, "indexed overview replay pixel budget exceeded (max 2097152)");
    }
    double margin = sed::level::kOverviewDefaultMargin;
    if (const Json* m = request.find("margin"))
    {
        if (m->type != Json::Type::Number) fail(kExitUsage, "overview 'margin' must be a number");
        margin = m->number;
    }
    bool fullbright = false;
    if (const Json* b = request.find("fullbright"))
    {
        if (b->type != Json::Type::Bool) fail(kExitUsage, "overview 'fullbright' must be a boolean");
        fullbright = b->boolean;
    }
    bool cullBackFaces = true;
    if (const Json* b = request.find("cullBackFaces"))
    {
        if (b->type != Json::Type::Bool) fail(kExitUsage, "overview 'cullBackFaces' must be a boolean");
        cullBackFaces = b->boolean;
    }
    const sed::render::SnapshotLighting lighting = snapshotLighting(request, "overview", indexed);
    const bool baked = lighting == sed::render::SnapshotLighting::baked;
    if ( fullbright && baked )
        fail(kExitUsage, "overview 'fullbright' and baked 'lighting' contradict each other; choose one");
    std::optional<sed::TBox> bounds;
    sed::level::OverviewCamera camera{};
    try
    {
        bounds = sed::level::worldSurfaceBounds(doc.level);
        if (!bounds) fail(kExitInput, "overview: level has no drawn world surfaces to frame");
        camera = sed::level::computeOverviewCamera(
            *bounds, view, framing, static_cast<int>(width), static_cast<int>(height), margin);
    }
    catch (const std::invalid_argument& e) { fail(kExitUsage, std::string("overview: ") + e.what()); }
    catch (const std::runtime_error& e) { fail(kExitInput, std::string("overview: ") + e.what()); }

    auto vec3 = [](const sed::TVector& v) {
        Json a = Json::makeArray();
        a.array.push_back(Json::makeNumber(v.x));
        a.array.push_back(Json::makeNumber(v.y));
        a.array.push_back(Json::makeNumber(v.z));
        return a;
    };
    Json cam = Json::makeObject();
    cam.set("position", vec3(camera.position));
    cam.set("target", vec3(camera.target));
    cam.set("projection", Json::makeString(camera.orthographic ? "orthographic" : "perspective"));
    cam.set("fieldOfViewDegrees", Json::makeNumber(camera.fieldOfViewDegrees));
    cam.set("orthographicHeight", Json::makeNumber(camera.orthographicHeight));
    cam.set("near", Json::makeNumber(camera.nearPlane));
    cam.set("far", Json::makeNumber(camera.farPlane));

    // The embedded request must replay through `snapshot` as the same
    // camera: projection and its one lens parameter sit at the top level
    // and the clip planes are camera nearPlane/farPlane, as snapshot reads them.
    Json snapCam = Json::makeObject();
    snapCam.set("position", vec3(camera.position));
    snapCam.set("target", vec3(camera.target));
    snapCam.set("nearPlane", Json::makeNumber(camera.nearPlane));
    snapCam.set("farPlane", Json::makeNumber(camera.farPlane));
    Json snapshotRequest = Json::makeObject();
    snapshotRequest.set("schemaVersion", Json::makeNumber(1));
    snapshotRequest.set("width", Json::makeNumber(static_cast<double>(width)));
    snapshotRequest.set("height", Json::makeNumber(static_cast<double>(height)));
    snapshotRequest.set("projection", Json::makeString(camera.orthographic ? "orthographic" : "perspective"));
    if (camera.orthographic)
        snapshotRequest.set("orthographicHeight", Json::makeNumber(camera.orthographicHeight));
    else
        snapshotRequest.set("fieldOfViewDegrees", Json::makeNumber(camera.fieldOfViewDegrees));
    snapshotRequest.set("camera", std::move(snapCam));
    snapshotRequest.set("fullbright", Json::makeBool(fullbright));
    snapshotRequest.set("cullBackFaces", Json::makeBool(cullBackFaces));
    if (!indexed || !fullbright) snapshotRequest.set("lighting", Json::makeString(snapshotLightingName(lighting)));
    if ( request.find("renderProfile") ) snapshotRequest.set("renderProfile", *request.find("renderProfile"));
    if ( request.find("thingMode") ) snapshotRequest.set("thingMode", *request.find("thingMode"));
    if ( request.find("playerStarts") ) snapshotRequest.set("playerStarts", *request.find("playerStarts"));
    // Baked lighting reads colormap tints from the archives, so the replay
    // needs them as static Things do.
    if ( (staticThings || baked || indexed) && request.find("resourceArchives") )
        snapshotRequest.set("resourceArchives", *request.find("resourceArchives"));

    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("overview"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("view", Json::makeString(view));
    out.set("framing", Json::makeString(framing));
    out.set("margin", Json::makeNumber(margin));
    out.set("width", Json::makeNumber(static_cast<double>(width)));
    out.set("height", Json::makeNumber(static_cast<double>(height)));
    out.set("camera", std::move(cam));
    out.set("snapshotRequest", std::move(snapshotRequest));
    out.set("renderStatus", Json::makeString("snapshotRequiresRenderer"));
    bool imageCreated = false;
    if (auto outputPath = optionalString(request, "output"))
    {
        // With an output path, overview renders its framed camera
        // with the software renderer (world surfaces only). Rendering
        // allocates the image and a depth buffer, so it takes no larger
        // an image than snapshot does; framing alone has no such limit.
        if (width > 2048 || height > 2048)
            fail(kExitUsage, "overview output requires width/height of at most 2048");
        if (width * height > 2097152)
            fail(kExitUsage, "overview output pixel budget exceeded (max 2097152)");
        CogArchiveSet set;
        if (request.find("resourceArchives")) set = loadCogArchives(request);
        sed::render::SnapshotCamera snapCam{};
        snapCam.position = camera.position;
        snapCam.target = camera.target;
        snapCam.orthographic = camera.orthographic;
        snapCam.fieldOfViewDegrees = camera.fieldOfViewDegrees;
        snapCam.orthographicHeight = camera.orthographicHeight;
        snapCam.nearPlane = camera.nearPlane;
        snapCam.farPlane = camera.farPlane;
        try
        {
            const SnapshotRenderOutcome rendered = renderSnapshotPng(
                doc, set, snapCam, static_cast<int>(width), static_cast<int>(height),
                fullbright, cullBackFaces, staticThings, lighting, drawPlayerStarts, indexed);
            const std::string pngBytes(
                reinterpret_cast<const char*>(rendered.result.png.data()),
                rendered.result.png.size());
            writeNewFileAtomic(fs::path(*outputPath), pngBytes);
            out.set("renderStatus", Json::makeString("rendered"));
            imageCreated = true;
            out.set("output", Json::makeString(*outputPath));
            out.set("imageSha256", Json::makeString(sha256Hex(pngBytes)));
            out.set("imageBytes", Json::makeNumber(static_cast<double>(pngBytes.size())));
            out.set("renderer", Json::makeString(staticThings ? "software-v1-static-things" : "software-v1"));
            out.set("renderScope", Json::makeString(staticThings ? "world-surfaces-and-static-things" : "world-surfaces"));
            if ( staticThings ) out.set("thingPreview", rendered.thingPreview);
            if (indexed) reportIndexedSnapshot(out, rendered, fullbright);
            if ( baked && !indexed ) out.set("colormapTints", rendered.colormapTints);
        }
        catch (const std::invalid_argument& e) { fail(kExitUsage, std::string("overview: ") + e.what()); }
        catch (const std::runtime_error& e) { fail(kExitInput, std::string("overview: ") + e.what()); }
    }
    out.set("imageCreated", Json::makeBool(imageCreated));
    return out;
}

Json cmdBindings(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "bindings requires <level> <bindings.json|->");
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "bindings request must be a JSON object");
    checkSchemaVersion(request);
    checkRevision(doc, request);
    const long long cog = requiredInt(request, "cog", "bindings request");
    if (cog < 0 || static_cast<std::size_t>(cog) >= doc.level.cogs.size())
        fail(kExitInput, "cog selector out of range for this input");
    const CogArchiveSet set = loadCogArchives(request);
    const std::string& scriptName = doc.level.cogs[static_cast<std::size_t>(cog)].name;
    auto schema = resolveCommandCogSchema(scriptName, set);
    checkScriptHash(schema ? &*schema : nullptr, expectedScriptHash(request));
    sed::level::CogBindingDescription description;
    try { description = sed::level::describeBindings(doc.level, static_cast<std::int32_t>(cog), schema ? &*schema : nullptr); }
    catch (const std::out_of_range&) { fail(kExitInput, "cog selector out of range for this input"); }
    Json items = Json::makeArray();
    for (const auto& row : description.rows)
    {
        Json r = Json::makeObject();
        r.set("parameterIndex", Json::makeNumber(static_cast<double>(row.parameterIndex)));
        if (row.symbolName) r.set("symbol", Json::makeString(*row.symbolName)); else r.set("symbol", Json{});
        r.set("kind", Json::makeString(std::string(symbolKindName(row.symbolKind))));
        r.set("type", Json::makeString(std::string(symbolKindName(row.symbolKind))));
        if (row.rawValue) r.set("rawValue", Json::makeString(*row.rawValue)); else r.set("rawValue", Json{});
        Json target = Json::makeObject();
        target.set("kind", Json::makeString(std::string(sed::level::cogReferenceKindName(row.target.kind))));
        const char* status = "unknown";
        using S = sed::level::CogBindingTargetStatus;
        switch (row.target.status)
        {
        case S::notReference: status = "notReference"; break;
        case S::missing: status = "missing"; break;
        case S::invalid: status = "invalid"; break;
        case S::null: status = "null"; break;
        case S::level: status = "level"; break;
        case S::unplacedUnchecked: status = "unplacedUnchecked"; break;
        case S::staticUnchecked: status = "staticUnchecked"; break;
        default: status = "unknown"; break;
        }
        target.set("status", Json::makeString(status));
        if (row.target.index) target.set("index", Json::makeNumber(static_cast<double>(*row.target.index)));
        if (row.target.sectorIndex) target.set("sector", Json::makeNumber(static_cast<double>(*row.target.sectorIndex)));
        if (row.target.surfaceIndex) target.set("surface", Json::makeNumber(static_cast<double>(*row.target.surfaceIndex)));
        target.set("detail", Json::makeString(row.target.detail));
        r.set("target", std::move(target));
        if (row.eventLinkCandidate) r.set("eventLinkCandidate", Json::makeBool(*row.eventLinkCandidate));
        else r.set("eventLinkCandidate", Json{});
        if (row.symbol)
        {
            Json mods = Json::makeObject();
            mods.set("noLink", Json::makeBool(row.symbol->noLink));
            if (row.symbol->explicitLinkId) mods.set("explicitLinkId", Json::makeNumber(static_cast<double>(*row.symbol->explicitLinkId)));
            else mods.set("explicitLinkId", Json{});
            if (row.symbol->explicitMask) mods.set("explicitMask", Json::makeNumber(static_cast<double>(*row.symbol->explicitMask)));
            else mods.set("explicitMask", Json{});
            mods.set("effectiveLinkId", Json::makeNumber(static_cast<double>(row.symbol->effectiveLinkId)));
            mods.set("effectiveMask", Json::makeNumber(static_cast<double>(row.symbol->effectiveMask)));
            mods.set("local", Json::makeBool(row.symbol->local));
            r.set("modifiers", std::move(mods));
        }
        items.array.push_back(std::move(r));
    }
    const std::size_t total = items.array.size();
    Json out = paginate(std::move(items), request, total);
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("bindings"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("cog", Json::makeNumber(static_cast<double>(cog)));
    out.set("script", Json::makeString(description.scriptName));
    out.set("scriptStatus", Json::makeString(std::string(sed::level::cogScriptStatusName(description.scriptStatus))));
    out.set("schemaComplete", Json::makeBool(description.schemaComplete));
    out.set("assetNotices", matAssetNotices(set));
    if (schema)
    {
        const std::string hash = sed::level::cogSchemaSha256Hex(*schema);
        out.set("scriptSha256", Json::makeString(hash));
        out.set("cogSchemaSha256Hex", Json::makeString(hash));
    }
    else { out.set("scriptSha256", Json{}); out.set("cogSchemaSha256Hex", Json{}); }
    out.set("actualParameterCount", Json::makeNumber(static_cast<double>(description.actualParameterCount)));
    if (description.expectedParameterCount) out.set("expectedParameterCount", Json::makeNumber(static_cast<double>(*description.expectedParameterCount)));
    else out.set("expectedParameterCount", Json{});
    if (description.parameterCountMatches) out.set("parameterCountMatches", Json::makeBool(*description.parameterCountMatches));
    else out.set("parameterCountMatches", Json{});
    Json diags = Json::makeArray();
    for (const auto& d : description.diagnostics)
    {
        Json row = Json::makeObject();
        row.set("code", Json::makeString(d.code));
        row.set("message", Json::makeString(d.message));
        diags.array.push_back(std::move(row));
    }
    out.set("diagnostics", std::move(diags));
    out.set("eventLinkSemantics", Json::makeString(std::string(sed::level::kCogEventLinkSemantics)));
    out.set("bindingResourceRevision", Json::makeString(set.revision));
    return out;
}

// Shared plan/apply tail for the COG commands: plan writes nothing and
// returns outputSha256; apply creates a new file and never overwrites.
Json cogPlanOrApply(const std::vector<std::string>& args, bool isAdd)
{
    const char* commandName = isAdd ? "addCog" : "rebindCogReference";
    if (args.size() < 2) fail(kExitUsage, std::string(commandName) + " requires <level> <request.json|-> [new-output [expected-output-sha256]]");
    const fs::path inputPath = args[0];
    const auto doc = openDocument(inputPath);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, std::string(commandName) + " request must be a JSON object");
    checkSchemaVersion(request);
    checkRevision(doc, request);
    sed::Level edited = doc.level;
    Json syntheticOp = request;
    syntheticOp.set("op", Json::makeString(commandName));
    if (isAdd)
    {
        validateAppendIdentitySource(edited);
        applyAddCogOp(edited, syntheticOp, request, 0);
    }
    else applyRebindOp(edited, syntheticOp, request, 0);
    // Same export dependency scan as plan/apply: these requests always carry
    // resourceArchives, so the default scan runs unless the caller opts out.
    ExportScanSetup exportScan;
    prepareExportScan(doc, request, exportScan);
    const ExportDependencyScan scan = exportScan.scan();
    std::string cogScriptSha;
    if (const auto expected = expectedScriptHash(request))
    {
        // The hash was already verified against the resolved schema above.
        cogScriptSha = lower(*expected);
    }
    const std::string output = buildOutput(doc, edited, &scan);
    const std::string outputSha = sha256Hex(output);
    const bool isApply = args.size() > 2;
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(commandName));
    out.set("mode", Json::makeString(isApply ? "apply" : "plan"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("outputSha256", Json::makeString(outputSha));
    if (!cogScriptSha.empty())
    {
        out.set("scriptSha256", Json::makeString(cogScriptSha));
        out.set("cogSchemaSha256Hex", Json::makeString(cogScriptSha));
    }
    if (isAdd) out.set("newCogIndex", Json::makeNumber(static_cast<double>(doc.level.cogs.size())));
    if (!doc.isSed) setExportBuffersReceipt(out, exportScan);
    if (!isApply) return out; // plan writes NOTHING.
    const fs::path outputPath = args[2];
    checkApplyOutputFormat(doc, outputPath);
    if (args.size() > 3 && lower(args[3]) != outputSha)
        fail(kExitConflict, "expected outputSha256 does not match the recomputed plan");
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath, output);
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}


// Exact workbench query (F-12 core) over one collection.
Json cmdWorkbenchQuery(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "workbench-query requires <level> <request.json|->");
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "workbench-query request must be a JSON object");
    checkSchemaVersion(request);
    checkRevision(doc, request);
    const Json* coll = request.find("collection");
    if (!coll || coll->type != Json::Type::String) fail(kExitUsage, "request requires string 'collection'");
    sed::level::WorkbenchCollection collection{};
    if (coll->str == "surfaces") collection = sed::level::WorkbenchCollection::surfaces;
    else if (coll->str == "things") collection = sed::level::WorkbenchCollection::things;
    else if (coll->str == "thingContainment") collection = sed::level::WorkbenchCollection::thingContainment;
    else if (coll->str == "sectors") collection = sed::level::WorkbenchCollection::sectors;
    else if (coll->str == "lights") collection = sed::level::WorkbenchCollection::lights;
    else fail(kExitUsage, "unknown workbench collection '" + coll->str + "'");
    std::optional<std::int32_t> sector{};
    if (request.find("sector")) sector = int32Field(request, "sector", 0);
    std::optional<sed::level::ExactQueryFilter> filter{};
    if (const Json* f = request.find("filter"))
    {
        if (!f->isObject()) fail(kExitUsage, "'filter' must be an object");
        sed::level::ExactQueryFilter parsed{};
        if (f->find("materialIndex")) parsed.materialIndex = int32Field(*f, "materialIndex", 0);
        if (f->find("layer")) parsed.layer = int32Field(*f, "layer", 0);
        if (auto s = optionalString(*f, "name")) parsed.name = *s;
        if (auto s = optionalString(*f, "templateName")) parsed.templateName = *s;
        parsed.flags = maskField(*f, "flags");
        parsed.surfFlags = maskField(*f, "surfFlags");
        parsed.faceFlags = maskField(*f, "faceFlags");
        parsed.adjoinFlags = maskField(*f, "adjoinFlags");
        filter = parsed;
    }
    std::string search;
    if (auto s = optionalString(request, "search")) search = *s;
    sed::level::QueryPageRequest page{};
    if (request.find("offset"))
    {
        const auto offset = intField(request, "offset", 0);
        if (offset < 0) fail(kExitUsage, "offset must be nonnegative");
        page.offset = static_cast<std::size_t>(offset);
    }
    if (request.find("limit"))
    {
        const auto limit = intField(request, "limit", 0);
        if (limit < 1) fail(kExitUsage, "limit must be positive");
        page.limit = static_cast<std::size_t>(limit);
    }
    const auto result = sed::level::queryWorkbench(doc.level, collection, sector, filter,
        std::string_view(search), page);
    if (!result.ok())
        failCoded(kExitRefusal, result.failure->code,
            result.failure->code + ": " + result.failure->message);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("workbench-query"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    Json indices = Json::makeArray();
    for (const std::int32_t i : result.indices) indices.array.push_back(Json::makeNumber(static_cast<double>(i)));
    out.set("indices", std::move(indices));
    out.set("total", Json::makeNumber(static_cast<double>(result.totalMatches)));
    if (result.nextOffset) out.set("nextOffset", Json::makeNumber(static_cast<double>(*result.nextOffset)));
    else out.set("nextOffset", Json{});
    return out;
}

// ARCH binding validation (F-05 core) against explicitly supplied 3DO models.
Json cmdArchValidate(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "arch-validate requires <level> <request.json|->");
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "arch-validate request must be a JSON object");
    checkSchemaVersion(request);
    checkRevision(doc, request);
    const Json* models = request.find("models");
    if (!models || models->type != Json::Type::Array || models->array.empty())
        fail(kExitUsage, "arch-validate requires a non-empty 'models' array");
    std::vector<sed::level::ArchBindingExpectation> expectations{};
    for (const Json& m : models->array)
    {
        if (!m.isObject()) fail(kExitUsage, "models entries must be objects");
        sed::level::ArchBindingExpectation exp{};
        exp.thingIndex = int32Field(m, "thing", 0);
        exp.modelName = stringField(m, "name", 0);
        const std::string modelPath = stringField(m, "path", 0);
        const std::string text = readFileBytes(modelPath);
        if (const Json* v = m.find("sha256"))
        {
            if (v->type != Json::Type::String) fail(kExitUsage, "model sha256 must be a string");
            if (lower(v->str) != sha256Hex(text))
                fail(kExitConflict, "model sha256 does not match the current model file (stale request)");
        }
        std::string error;
        const auto topology = sed::level::readArchModelTopology(text, &error);
        if (!topology)
            fail(kExitInput, "cannot read ARCH model topology from " + modelPath + ": " + error);
        exp.topology = *topology;
        exp.resolvedFingerprintHex = sed::level::archModelFingerprintHex(*topology);
        expectations.push_back(std::move(exp));
    }
    std::vector<sed::level::ArchStoredFingerprint> stored{};
    if (const Json* s = request.find("storedFingerprints"))
    {
        if (s->type != Json::Type::Array) fail(kExitUsage, "'storedFingerprints' must be an array");
        for (const Json& row : s->array)
        {
            if (!row.isObject()) fail(kExitUsage, "storedFingerprints entries must be objects");
            stored.push_back(sed::level::ArchStoredFingerprint{
                int32Field(row, "thing", 0), stringField(row, "fingerprint", 0) });
        }
    }
    const auto report = sed::level::validateArchBindings(doc.level, expectations, stored, nullptr);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("arch-validate"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("usable", Json::makeBool(report.usable()));
    Json bindings = Json::makeArray();
    for (const auto& b : report.bindings)
    {
        Json row = Json::makeObject();
        row.set("thing", Json::makeNumber(static_cast<double>(b.thingIndex)));
        row.set("usable", Json::makeBool(b.usable));
        row.set("fingerprintChecked", Json::makeBool(b.fingerprintChecked));
        Json issues = Json::makeArray();
        for (const auto& issue : b.issues)
        {
            Json item = Json::makeObject();
            item.set("code", Json::makeString(issue.code));
            item.set("message", Json::makeString(issue.message));
            item.set("blocking", Json::makeBool(issue.blocking));
            item.set("thing", Json::makeNumber(static_cast<double>(issue.thingIndex)));
            item.set("geoset", Json::makeNumber(static_cast<double>(issue.geosetIndex)));
            issues.array.push_back(std::move(item));
        }
        row.set("issues", std::move(issues));
        bindings.array.push_back(std::move(row));
    }
    out.set("bindings", std::move(bindings));
    return out;
}

bool isSha256Hex(const std::string& text)
{
    if (text.size() != 64) return false;
    return std::all_of(text.begin(), text.end(), [](const char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

#ifdef _WIN32
struct WorkerResult
{
    bool timedOut{ false };
    unsigned long exitCode{ 0 };
    std::string stdOut{};
};

WorkerResult runBridgeWorker(
    const fs::path& exe, const std::string& arguments,
    const std::string& input, const int timeoutSeconds)
{
    WorkerResult result{};
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE stdinRead = nullptr, stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)
        || !CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0))
    {
        fail(kExitRefusal, "cannot create native worker pipes");
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullFile = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = nullFile;
    PROCESS_INFORMATION pi{};
    const std::wstring commandLine =
        (L"\"" + exe.wstring() + L"\" " + std::wstring(arguments.begin(), arguments.end()));
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(L'\0');
    if (!CreateProcessW(exe.c_str(), mutableLine.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        fail(kExitRefusal, "cannot start the native inspection worker");
    }
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    if (nullFile) CloseHandle(nullFile);

    // Feed stdin fully (the worker drains continuously), then close it.
    std::size_t written = 0;
    while (written < input.size())
    {
        DWORD chunk = 0;
        if (!WriteFile(stdinWrite, input.data() + written,
                static_cast<DWORD>(input.size() - written), &chunk, nullptr))
        {
            break;
        }
        written += chunk;
    }
    CloseHandle(stdinWrite);

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(timeoutSeconds);
    bool exited = false;
    while (true)
    {
        DWORD avail = 0;
        if (PeekNamedPipe(stdoutRead, nullptr, 0, nullptr, &avail, nullptr)
            && avail > 0)
        {
            char buffer[8192];
            DWORD read = 0;
            const DWORD want = std::min<DWORD>(avail, sizeof(buffer));
            if (ReadFile(stdoutRead, buffer, want, &read, nullptr) && read > 0)
            {
                if (result.stdOut.size() <= 65537)
                {
                    result.stdOut.append(buffer, read);
                }
            }
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
        {
            exited = true;
            // Drain what remains.
            DWORD avail = 0;
            while (PeekNamedPipe(stdoutRead, nullptr, 0, nullptr, &avail, nullptr)
                && avail > 0)
            {
                char buffer[8192];
                DWORD read = 0;
                if (!ReadFile(stdoutRead, buffer,
                        std::min<DWORD>(avail, sizeof(buffer)), &read, nullptr)
                    || read == 0)
                {
                    break;
                }
                if (result.stdOut.size() <= 65537)
                {
                    result.stdOut.append(buffer, read);
                }
            }
            break;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            result.timedOut = true;
            break;
        }
        Sleep(1);
    }
    if (exited)
    {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result.exitCode = code;
    }
    CloseHandle(stdoutRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}
#endif

Json cmdNativeInspect(const std::vector<std::string>& args)
{
    if (args.size() != 4)
        fail(kExitUsage, "native-inspect requires <input> <jk|mots|ijim|droidworks> <jkl|native-sed> <worker-config.json>");
    const fs::path inputPath = args[0];
    const std::string kind = lower(args[1]);
    const std::string format = lower(args[2]);
    if (kind != "jk" && kind != "mots" && kind != "ijim" && kind != "droidworks")
        fail(kExitUsage, "native inspection project kind must be jk, mots, ijim or droidworks");
    if (format != "jkl" && format != "native-sed")
        fail(kExitUsage, "native inspection format must be jkl or native-sed");

    const fs::path configPath = fs::absolute(fs::path(args[3]));
    const std::string configBytes = readFileBytes(configPath);
    if (configBytes.size() > 16 * 1024)
        fail(kExitUsage, "worker-config.json exceeds the 16 KiB limit");
    Json config;
    try
    {
        config = parseJson(configBytes);
    }
    catch (const std::exception& e)
    {
        fail(kExitUsage, std::string("worker-config.json is not valid JSON: ") + e.what());
    }
    if (!config.isObject()) fail(kExitUsage, "worker-config.json must be a JSON object");
    const Json* schemaVersion = config.find("schemaVersion");
    if (!schemaVersion || schemaVersion->type != Json::Type::Number
        || schemaVersion->number != 1)
    {
        fail(kExitUsage, "native worker configuration needs schemaVersion 1");
    }
    const std::string expectedExeSha =
        lower(requiredString(config, "executableSha256", "worker-config"));
    const std::string expectedManifestSha =
        lower(requiredString(config, "sourceManifestSha256", "worker-config"));
    if (!isSha256Hex(expectedExeSha) || !isSha256Hex(expectedManifestSha))
        fail(kExitUsage, "worker-config requires sha256 identities for the executable and source manifest");
    int timeoutSeconds = 20;
    if (const Json* timeout = config.find("timeoutSeconds"))
    {
        if (timeout->type != Json::Type::Number)
            fail(kExitUsage, "timeoutSeconds must be a number");
        timeoutSeconds = checkedInt32(timeout->number, "timeoutSeconds");
    }
    if (timeoutSeconds < 1 || timeoutSeconds > 60)
        fail(kExitUsage, "timeoutSeconds must be between 1 and 60");

    const fs::path exe = configPath.parent_path() / "sed-cpp-bridge.exe";
    if (!fs::exists(exe))
        fail(kExitInput, "The configured native inspection component is not installed beside worker-config.json: " + exe.string());
    const std::string exeSha = sha256Hex(readFileBytes(exe));
    if (exeSha != expectedExeSha)
        fail(kExitConflict, "native worker executable sha256 does not match worker-config.json");

    std::error_code sizeEc;
    const auto inputSize = fs::file_size(inputPath, sizeEc);
    if (sizeEc || inputSize == 0 || inputSize > 16777216)
        fail(kExitUsage, "native inspection requires a nonempty file no larger than 16777216 bytes");
    const std::string inputBytes = readFileBytes(inputPath);
    const std::string inputSha256 = sha256Hex(inputBytes);
    const std::string requestId = inputSha256.substr(0, 32);

#ifndef _WIN32
    fail(kExitRefusal, "native inspection worker hosting is Windows-only in this build");
#else
    const std::string arguments = "--protocol 1 "
        + std::string(format == "jkl" ? "inspect-jkl" : "inspect-native-sed")
        + " --project-kind " + kind + " --request-id " + requestId;
    const WorkerResult worker = runBridgeWorker(exe, arguments, inputBytes, timeoutSeconds);
    if (worker.timedOut)
        failCoded(kExitRefusal, "deadlineExceeded",
            "Native inspection worker exceeded its configured deadline and was terminated.");

    Json observation;
    bool valid = false;
    if (!worker.stdOut.empty() && worker.stdOut.size() <= 65536
        && worker.stdOut.back() == '\n')
    {
        try
        {
            observation = parseJson(worker.stdOut);
            valid = observation.isObject()
                && observation.find("status")
                && observation.find("sourceManifestSha256");
        }
        catch (const std::exception&)
        {
            valid = false;
        }
    }
    Json out = Json::makeObject();
    out.set("schemaVersion", Json::makeNumber(1));
    out.set("command", Json::makeString("native-inspect"));
    out.set("inputSha256", Json::makeString(inputSha256));
    out.set("executableSha256", Json::makeString(exeSha));
    out.set("note", Json::makeString(
        "Read-only upstream core observation. Resources are not validated; no source, document or undo state was changed."));
    if (!valid)
    {
        out.set("ok", Json::makeBool(false));
        out.set("status", Json::makeString("ProcessFailed"));
        throw AgentError{ kExitRefusal,
            "native inspection worker did not produce a complete protocol response",
            "process_error", std::move(out), true };
    }
    const std::string manifestSha =
        lower(observation.find("sourceManifestSha256")->str);
    if (manifestSha != expectedManifestSha)
    {
        out.set("ok", Json::makeBool(false));
        out.set("status", Json::makeString("ProcessFailed"));
        throw AgentError{ kExitRefusal,
            "native worker source manifest does not match worker-config.json",
            "process_error", std::move(out), true };
    }
    const std::string workerStatus = observation.find("status")->str;
    const std::string outcome = workerStatus == "ok" ? "Inspected"
        : workerStatus == "rejected" ? "DocumentRejected" : "ProcessFailed";
    out.set("ok", Json::makeBool(outcome == "Inspected"));
    out.set("status", Json::makeString(outcome));
    out.set("observation", std::move(observation));
    if (outcome == "Inspected")
    {
        return out;
    }
    const int exitCode = outcome == "DocumentRejected" ? 3 : kExitRefusal;
    out.set("exitCode", Json::makeNumber(exitCode));
    throw AgentError{ exitCode, "native inspection " + outcome, {}, std::move(out), true };
#endif
}

Json cmdDependencies(const std::vector<std::string>& args)
{
    try
    {
        if ( args.size() != 2 )
        {
            fail(kExitUsage, "dependencies requires <level.jkl> <request.json|->");
        }
        validateDependencyPath(args[0]);
        const fs::path levelPath = args[0];
        const std::string levelName = levelPath.filename().string();
        if ( extensionOf(levelPath) != ".jkl" || levelName.size() > 255
            || levelName.find_first_of("/\\") != std::string::npos )
        {
            failCoded(kExitUsage, "dependencyRequestInvalid", "Dependency source requires a plain .jkl basename of at most 255 bytes");
        }
        const ParsedDependencyRequest request = parseDependencyRequest(readRequest(args[1]));
        const std::string levelBytes = readFileBytes(levelPath);
        if ( sha256Hex(levelBytes) != request.inputSha256 )
        {
            failCoded(kExitConflict, "dependencyInputHashMismatch", "Dependency inputSha256 does not match the captured source");
        }
        std::vector<sed::level::DependencyArchiveInput> archives;
        archives.reserve(request.archives.size());
        for ( const auto& source : request.archives )
        {
            archives.push_back({ captureResourceArchive(source.path, source.sha256),
                fs::path(source.path).filename().string(), source.stock });
        }
        return inspectDependencyRequest(levelBytes, levelName, request, archives);
    }
    catch ( AgentError& error )
    {
        // Archive diagnostics and request-file errors may contain arbitrary bytes.
        // Keep the shared exit/code/payload contract while encoding dependency text once.
        error.message = encodeDependencyText(error.message);
        throw;
    }
}

std::size_t packageOptSize(const Json& obj, const char* key, const std::size_t fallback)
{
    const Json* v = obj.find(key);
    if (!v) return fallback;
    if (v->type != Json::Type::Number || !std::isfinite(v->number)
        || v->number < 0 || v->number != std::floor(v->number)
        || v->number >= std::ldexp(1.0, std::numeric_limits<std::size_t>::digits))
        fail(kExitUsage, std::string(key) + " must be a representable non-negative integer");
    return static_cast<std::size_t>(v->number);
}

sed::level::PackageArchiveInput loadPackageArchive(
    const std::string& pathText, const bool stock, const std::string& bytes,
    std::size_t& remainingBytes, std::size_t& remainingEntries)
{
    const fs::path path = pathText;
    admitCapturedArchive(bytes, path);
    sed::assets::GobArchive archive;
    try
    {
        archive = sed::assets::GobArchive::fromBytes(toBytes(bytes), path);
    }
    catch (const std::exception& e)
    {
        failCoded(kExitUsage, "invalid_request",
            "Resource archive is not a readable GOB/GOO archive: "
                + path.filename().string() + ". " + e.what());
    }
    sed::level::PackageArchiveInput input{};
    input.label = path.filename().string();
    input.stock = stock;
    admitArchiveEntries(archive, remainingBytes, remainingEntries);
    for (const sed::assets::GobEntry& entry : archive.entries())
    {
        const std::vector<std::byte> raw = archive.read(entry.name);
        input.entries.emplace_back(entry.name,
            std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
    }
    return input;
}

sed::level::PackageRequest parsePackageRequest(
    const Json& req, const std::string& levelBytes, const fs::path& levelPath)
{
    if (const Json* sha = req.find("inputSha256"))
    {
        if (sha->type != Json::Type::String)
            fail(kExitUsage, "inputSha256 must be a string");
        if (lower(sha->str) != sha256Hex(levelBytes))
            fail(kExitConflict, "inputSha256 does not match the level file");
    }

    sed::level::PackageRequest core{};
    core.inputBytes = levelBytes;
    core.levelFileName = levelPath.filename().string();
    std::size_t remainingBytes = kMaxExpandedArchiveBytes;
    std::size_t remainingEntries = 100000;

    if (const Json* archives = req.find("archives"))
    {
        if (archives->type != Json::Type::Array)
            fail(kExitUsage, "archives must be an array");
        for (const Json& item : archives->array)
        {
            if (!item.isObject())
                fail(kExitUsage, "archives entries must be objects");
            std::string pathText;
            if (auto p = optionalString(item, "path")) pathText = *p;
            else if (auto p = optionalString(item, "fileName")) pathText = *p;
            else fail(kExitUsage, "archives entry requires path (fileName)");
            const std::string expected =
                lower(requiredString(item, "sha256", "archives entry"));
            const std::string bytes = readArchiveBytes(fs::path(pathText));
            if (sha256Hex(bytes) != expected)
                fail(kExitConflict, "archive sha256 mismatch: " + pathText);
            core.archives.push_back(loadPackageArchive(pathText, false, bytes, remainingBytes, remainingEntries));
        }
    }
    if (const Json* stock = req.find("stockArchives"))
    {
        if (stock->type != Json::Type::Array)
            fail(kExitUsage, "stockArchives must be an array of paths");
        for (const Json& item : stock->array)
        {
            if (item.type != Json::Type::String)
                fail(kExitUsage, "stockArchives must be an array of paths");
            const std::string bytes = readArchiveBytes(fs::path(item.str));
            core.archives.push_back(loadPackageArchive(item.str, true, bytes, remainingBytes, remainingEntries));
        }
    }
    if (const Json* extras = req.find("extraResources"))
    {
        if (extras->type != Json::Type::Array)
            fail(kExitUsage, "extraResources must be an array");
        for (const Json& item : extras->array)
        {
            if (!item.isObject())
                fail(kExitUsage, "extraResources entries must be objects");
            std::string name;
            if (auto n = optionalString(item, "name")) name = *n;
            else if (auto n = optionalString(item, "fileName")) name = *n;
            else fail(kExitUsage, "extraResources entry requires name (fileName)");
            core.extraResources.push_back(sed::level::PackageExtraResource{
                name, requiredString(item, "folder", "extraResources entry") });
        }
    }
    if (const Json* options = req.find("options"))
    {
        if (!options->isObject()) fail(kExitUsage, "options must be an object");
        core.options.maxNodes = packageOptSize(*options, "maxNodes", core.options.maxNodes);
        core.options.maxEdges = packageOptSize(*options, "maxEdges", core.options.maxEdges);
        core.options.maxDepth = packageOptSize(*options, "maxDepth", core.options.maxDepth);
        core.options.maxPackageBytes = packageOptSize(*options, "maxPackageBytes", core.options.maxPackageBytes);
        if (const Json* allow = options->find("allowUnverifiedReferences"))
        {
            if (allow->type != Json::Type::Bool)
                fail(kExitUsage, "allowUnverifiedReferences must be a bool");
            core.options.allowUnverifiedReferences = allow->boolean;
        }
    }
    const Json* episode = req.find("episode");
    if (!episode || !episode->isObject())
        fail(kExitUsage, "package request requires an episode object");
    core.episodeTitle = requiredString(*episode, "episodeTitle", "episode");
    if (auto name = optionalString(*episode, "levelFileName"))
        core.levelFileName = *name;
    if (const Json* type = episode->find("episodeType"))
    {
        if (type->type != Json::Type::Number)
            fail(kExitUsage, "episodeType must be a number");
        core.episodeType = checkedInt32(type->number, "episodeType");
    }
    if (auto ext = optionalString(req, "archiveExtension"))
        core.archiveExtension = lower(*ext);
    return core;
}

Json packagePlanJson(
    const sed::level::PackagePlan& plan, const std::string& inputSha256,
    const char* command)
{
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(plan.canBuild));
    out.set("command", Json::makeString(command));
    out.set("canBuild", Json::makeBool(plan.canBuild));
    out.set("runtimeClosureVerified", Json::makeBool(false));
    out.set("inputSha256", Json::makeString(inputSha256));
    out.set("archiveExtension", Json::makeString(plan.archiveExtension));
    if (plan.canBuild)
        out.set("outputSha256", Json::makeString(plan.outputSha256));
    else
        out.set("outputSha256", Json{});
    out.set("manifestSha256", Json::makeString(plan.manifestSha256));
    out.set("outputBytes", Json::makeNumber(static_cast<double>(plan.outputBytes)));
    out.set("nodes", Json::makeNumber(static_cast<double>(plan.nodes)));
    out.set("edges", Json::makeNumber(static_cast<double>(plan.edges)));
    Json entries = Json::makeArray();
    for (const sed::level::PackageEntry& entry : plan.entries)
    {
        Json row = Json::makeObject();
        row.set("path", Json::makeString(entry.path));
        row.set("source", Json::makeString(entry.source));
        row.set("bytes", Json::makeNumber(static_cast<double>(entry.bytes)));
        row.set("sha256", Json::makeString(entry.sha256));
        entries.array.push_back(std::move(row));
    }
    out.set("entries", std::move(entries));
    Json omissions = Json::makeArray();
    for (const sed::level::PackageEntry& entry : plan.stockOmissions)
    {
        Json row = Json::makeObject();
        row.set("path", Json::makeString(entry.path));
        row.set("source", Json::makeString(entry.source));
        row.set("bytes", Json::makeNumber(static_cast<double>(entry.bytes)));
        row.set("sha256", Json::makeString(entry.sha256));
        omissions.array.push_back(std::move(row));
    }
    out.set("stockOmissions", std::move(omissions));
    Json issues = Json::makeArray();
    for (const sed::level::PackageIssue& issue : plan.issues)
    {
        Json row = Json::makeObject();
        row.set("severity", Json::makeString(issue.severity));
        row.set("code", Json::makeString(issue.code));
        row.set("reference", Json::makeString(issue.reference));
        row.set("message", Json::makeString(issue.message));
        issues.array.push_back(std::move(row));
    }
    out.set("issues", std::move(issues));
    Json scopes = Json::makeArray();
    for (const std::string& scope : plan.unverifiedScopes)
        scopes.array.push_back(Json::makeString(scope));
    out.set("unverifiedScopes", std::move(scopes));
    return out;
}

sed::level::PackagePlan runPackagePlan(
    const std::vector<std::string>& args, const bool forApply,
    std::string& inputSha256, fs::path& levelPath)
{
    if (args.size() < (forApply ? 5u : 2u))
        fail(kExitUsage, forApply
            ? "package-apply requires <level.jkl> <package.json|-> <new-output.gob|.goo> <expected-output-sha256> <expected-manifest-sha256>"
            : "package-plan requires <level.jkl> <package.json|->");
    levelPath = args[0];
    if (extensionOf(levelPath) != ".jkl")
        fail(kExitUsage, "package input must be a .jkl level (JED/SED/NDY inputs are refused)");
    const std::string levelBytes = readFileBytes(levelPath);
    inputSha256 = sha256Hex(levelBytes);
    const Json req = readRequest(args[1]);
    if (!req.isObject()) fail(kExitUsage, "package request must be a JSON object");
    checkSchemaVersion(req);
    sed::level::PackageRequest core = parsePackageRequest(req, levelBytes, levelPath);
    try
    {
        return sed::level::planLevelPackage(core);
    }
    catch (const std::invalid_argument& e)
    {
        fail(kExitUsage, e.what());
    }
}

Json cmdPackagePlan(const std::vector<std::string>& args)
{
    std::string inputSha256;
    fs::path levelPath;
    const sed::level::PackagePlan plan =
        runPackagePlan(args, false, inputSha256, levelPath);
    Json out = packagePlanJson(plan, inputSha256, "package-plan");
    if (!plan.canBuild)
    {
        out.set("exitCode", Json::makeNumber(3));
        throw AgentError{ 3, "package plan is blocked", {}, std::move(out), true };
    }
    return out;
}

Json cmdPackageApply(const std::vector<std::string>& args)
{
    std::string inputSha256;
    fs::path levelPath;
    const sed::level::PackagePlan plan =
        runPackagePlan(args, true, inputSha256, levelPath);
    const fs::path outputPath = args[2];
    if (extensionOf(outputPath) != plan.archiveExtension)
        fail(kExitUsage, "package output must end in " + plan.archiveExtension);
    checkNewOutputTarget(levelPath, outputPath, true);
    Json out = packagePlanJson(plan, inputSha256, "package-apply");
    if (!plan.canBuild)
    {
        out.set("exitCode", Json::makeNumber(3));
        throw AgentError{ 3, "package plan is blocked", {}, std::move(out), true };
    }
    if (lower(args[3]) != plan.outputSha256)
        fail(kExitConflict, "expected output sha256 does not match the current plan; re-run package-plan");
    if (lower(args[4]) != plan.manifestSha256)
        fail(kExitConflict, "expected manifest sha256 does not match the current plan; re-run package-plan");
    writeNewFileAtomic(outputPath, plan.outputData);
    out.set("output", Json::makeString(outputPath.string()));
    out.set("written", Json::makeBool(true));
    out.set("bytesWritten", Json::makeNumber(static_cast<double>(plan.outputBytes)));
    return out;
}

Json cmdPrefabCapture(const std::vector<std::string>& args)
{
    if (args.size() < 3)
        fail(kExitUsage, "prefab-capture requires <level> <request.json|-> <new-out.prefab>");
    const fs::path inputPath = args[0];
    const auto doc = openDocument(inputPath);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "prefab-capture request must be a JSON object");
    checkSchemaVersion(request);
    checkRevision(doc, request);
    const Json* sectorsJson = request.find("sectors");
    if (!sectorsJson || sectorsJson->type != Json::Type::Array || sectorsJson->array.empty())
        fail(kExitUsage, "prefab-capture requires a non-empty 'sectors' array");
    std::vector<std::int32_t> sectors{};
    for (const Json& value : sectorsJson->array)
    {
        if (value.type != Json::Type::Number || value.number != std::floor(value.number))
            fail(kExitUsage, "prefab-capture 'sectors' entries must be integers");
        sectors.push_back(checkedInt32(value.number, "sector"));
    }
    std::sort(sectors.begin(), sectors.end());
    sectors.erase(std::unique(sectors.begin(), sectors.end()), sectors.end());
    std::int32_t primary = sectors.front();
    if (const Json* v = request.find("primary"))
    {
        if (v->type != Json::Type::Number) fail(kExitUsage, "prefab-capture 'primary' must be a number");
        primary = checkedInt32(v->number, "primary");
    }
    const auto prefab = sed::level::makePrefab(doc.level, sectors, primary);
    if (!prefab)
        failCoded(kExitRefusal, "prefabCaptureRefused",
            "prefabCaptureRefused: the sector selection cannot be captured");
    const auto text = sed::level::savePrefabText(*prefab);
    if (!text)
        failCoded(kExitRefusal, "prefabCaptureRefused",
            "prefabCaptureRefused: the prefab could not be serialized");
    const fs::path outputPath = args[2];
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath, *text);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("prefab-capture"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("contentSha256Hex", Json::makeString(prefab->contentSha256Hex));
    out.set("sectors", Json::makeNumber(static_cast<double>(prefab->payload.sectors.size())));
    out.set("attachments", Json::makeNumber(static_cast<double>(prefab->attachments.size())));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(*text)));
    return out;
}

Json cmdExportDxf(const std::vector<std::string>& args)
{
    if (args.size() < 2)
        fail(kExitUsage, "export-dxf requires <level> <new-out.dxf>");
    const fs::path inputPath = args[0];
    const auto doc = openDocument(inputPath);
    const std::string text = sed::assets::saveLevelToDxfText(doc.level);
    const fs::path outputPath = args[1];
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath, text);
    std::istringstream in(text);
    const auto faces = sed::assets::loadDxfFaces(in);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("export-dxf"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("outputSha256", Json::makeString(sha256Hex(text)));
    out.set("faces", Json::makeNumber(static_cast<double>(faces.size())));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

Json cmdStatistics(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "statistics requires <level>");
    const auto doc = openDocument(args[0]);
    // E-01: read-only Statter/Statman aggregation over the whole level.
    const auto stats = sed::level::levelComputeStatistics(doc.level);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("statistics"));
    out.set("sectors", Json::makeNumber(static_cast<double>(stats.sectors)));
    out.set("surfaces", Json::makeNumber(static_cast<double>(stats.surfaces)));
    out.set("vertices", Json::makeNumber(static_cast<double>(stats.vertices)));
    out.set("texVertices", Json::makeNumber(static_cast<double>(stats.texVertices)));
    out.set("things", Json::makeNumber(static_cast<double>(stats.things)));
    out.set("lights", Json::makeNumber(static_cast<double>(stats.lights)));
    out.set("cogs", Json::makeNumber(static_cast<double>(stats.cogs)));
    out.set("adjoinedSurfaces", Json::makeNumber(static_cast<double>(stats.adjoinedSurfaces)));
    out.set("skySurfaces", Json::makeNumber(static_cast<double>(stats.skySurfaces)));
    out.set("fullyLitSurfaces", Json::makeNumber(static_cast<double>(stats.fullyLitSurfaces)));
    out.set("surfaceArea", Json::makeNumber(stats.surfaceArea));
    if (stats.hasBounds)
    {
        Json bounds = Json::makeArray();
        for (const double v : { stats.bounds.x1, stats.bounds.y1, stats.bounds.z1,
                 stats.bounds.x2, stats.bounds.y2, stats.bounds.z2 })
            bounds.array.push_back(Json::makeNumber(v));
        out.set("bounds", bounds);
    }
    Json byLayer = Json::makeObject();
    for (const auto& [layer, count] : stats.sectorsByLayer)
        byLayer.set(std::to_string(layer), Json::makeNumber(static_cast<double>(count)));
    out.set("sectorsByLayer", byLayer);
    return out;
}

Json cmdTerrain3do(const std::vector<std::string>& args)
{
    if (args.size() < 2)
        fail(kExitUsage, "terrain-3do requires <request.json|-> <new-out.3do>");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "terrain-3do request must be a JSON object");
    checkSchemaVersion(request);
    // E-14: the sector heightmap's model (3DO) output. The request mirrors
    // the buildHeightmapTerrain operation fields, plus an optional material.
    sed::level::TerrainParameters parameters{};
    parameters.origin = pointField(request, "origin");
    parameters.cellSize = numberField(request, "cellSize", 0);
    parameters.headroom = numberField(request, "headroom", 0);
    parameters.columns = int32Field(request, "columns", 0);
    parameters.rows = int32Field(request, "rows", 0);
    const Json* heights = request.find("heights");
    if (!heights || heights->type != Json::Type::Array)
        fail(kExitUsage, "terrain-3do requires a 'heights' array of numbers");
    parameters.heights.reserve(heights->array.size());
    for (const Json& value : heights->array)
    {
        if (value.type != Json::Type::Number)
            fail(kExitUsage, "terrain-3do 'heights' entries must be numbers");
        parameters.heights.push_back(value.number);
    }
    std::string material{};
    if (const Json* v = request.find("material"))
    {
        if (v->type != Json::Type::String) fail(kExitUsage, "terrain-3do 'material' must be a string");
        material = v->str;
    }
    if (const auto refusal = sed::level::validateTerrainParameters(parameters))
        failCoded(kExitRefusal, refusal->code, refusal->code + ": " + refusal->message);
    const auto model = sed::level::levelBuildHeightmapModel(parameters, material);
    if (!model)
        failCoded(kExitRefusal, "invalidTerrainParameters",
            "invalidTerrainParameters: the terrain model could not be built (check the material name)");
    std::string text;
    try
    {
        text = sed::assets::saveTo3DO(*model);
    }
    catch (const std::runtime_error& error)
    {
        failCoded(kExitRefusal, "terrainModelRefused",
            std::string("terrainModelRefused: ") + error.what());
    }
    const fs::path outputPath = args[1];
    checkNewOutputTarget(outputPath, outputPath, false);
    writeNewFileAtomic(outputPath, text);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("terrain-3do"));
    out.set("outputSha256", Json::makeString(sha256Hex(text)));
    out.set("vertices", Json::makeNumber(static_cast<double>(model->meshes[0].vertices.size())));
    out.set("faces", Json::makeNumber(static_cast<double>(model->meshes[0].faces.size())));
    out.set("material", Json::makeString(model->materials[0]));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

sed::assets::MatFile loadMatFile(const fs::path& path, const sed::assets::CmpPalette* palette = nullptr)
{
    const std::string bytes = readFileBytes(path);
    std::vector<std::byte> data(bytes.size());
    std::memcpy(data.data(), bytes.data(), bytes.size());
    try
    {
        return sed::assets::MatFile::parse(std::move(data), palette);
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot parse MAT file: ") + e.what());
    }
}

Json cmdMatInspect(const std::vector<std::string>& args)
{
    if (args.empty())
        fail(kExitUsage, "mat-inspect requires <file.mat>");
    // E-18: MAT bridge inventory. No CMP palette is supplied, so indexed
    // MATs report their stored layout without palette-resolved colours.
    const sed::assets::MatFile mat = loadMatFile(fs::path(args[0]));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("mat-inspect"));
    out.set("isColor", Json::makeBool(mat.isColor()));
    out.set("isAnimated", Json::makeBool(mat.isAnimated()));
    out.set("isTrueColor", Json::makeBool(mat.isTrueColor()));
    out.set("bitsPerPixel", Json::makeNumber(static_cast<double>(mat.bitsPerPixel())));
    out.set("celCount", Json::makeNumber(static_cast<double>(mat.celCount())));
    Json cels = Json::makeArray();
    for (const sed::assets::MatFile::Cel& cel : mat.cels())
    {
        Json entry = Json::makeObject();
        entry.set("width", Json::makeNumber(static_cast<double>(cel.width)));
        entry.set("height", Json::makeNumber(static_cast<double>(cel.height)));
        entry.set("mipmaps", Json::makeNumber(static_cast<double>(cel.mipmaps.size())));
        cels.array.push_back(std::move(entry));
    }
    out.set("cels", std::move(cels));
    return out;
}

Json cmdMatExportPng(const std::vector<std::string>& args)
{
    if (args.size() < 2 || args.size() > 4)
        fail(kExitUsage, "mat-export-png requires <file.mat> <new-out.png> [cel] [palette.cmp]");
    std::int32_t celIndex = 0;
    // An indexed MAT stores palette indices, not colours. Without a colormap
    // the export is a grey index map; with one it shows the texture as the
    // engine draws it under that colormap.
    std::optional<sed::assets::CmpPalette> palette;
    if (args.size() > 3)
    {
        const std::string cmpBytes = readFileBytes(fs::path(args[3]));
        palette = sed::assets::parseCmpPalette(std::as_bytes(std::span(cmpBytes.data(), cmpBytes.size())));
        if (!palette)
            fail(kExitInput, "cannot parse CMP palette: " + args[3]);
    }
    if (args.size() > 2)
    {
        bool ok = true;
        try
        {
            std::size_t consumed = 0;
            celIndex = static_cast<std::int32_t>(std::stoi(args[2], &consumed));
            ok = consumed == args[2].size();
        }
        catch (const std::exception&)
        {
            ok = false;
        }
        if (!ok) fail(kExitUsage, "mat-export-png cel must be an integer");
    }
    const fs::path inputPath = args[0];
    const sed::assets::MatFile mat = loadMatFile(inputPath, palette ? &*palette : nullptr);
    if (celIndex < 0 || celIndex >= mat.celCount())
        failCoded(kExitRefusal, "matCelOutOfRange",
            "matCelOutOfRange: cel index is outside the MAT cel list");
    sed::Rgba8Image image{};
    try
    {
        image = mat.decodeCelRgba8(celIndex);
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot decode MAT cel: ") + e.what());
    }
    if (image.width <= 0 || image.height <= 0 || image.rgba.empty())
        failCoded(kExitRefusal, "matCelEmpty",
            "matCelEmpty: the MAT cel decodes to an empty image");
    const std::vector<std::uint8_t> png =
        sed::render::encodePngRgba8(image.width, image.height, image.rgba);
    const fs::path outputPath = args[1];
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath,
        std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("mat-export-png"));
    out.set("cel", Json::makeNumber(static_cast<double>(celIndex)));
    out.set("width", Json::makeNumber(static_cast<double>(image.width)));
    out.set("height", Json::makeNumber(static_cast<double>(image.height)));
    out.set("bitsPerPixel", Json::makeNumber(static_cast<double>(mat.bitsPerPixel())));
    out.set("outputSha256", Json::makeString(sha256Hex(
        std::string_view(reinterpret_cast<const char*>(png.data()), png.size()))));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

// A hash-bound input file named in a request: {path, sha256}.
struct BoundFile
{
    fs::path path;
    std::string bytes;
    std::string sha256;
};

BoundFile readBoundFile(const Json& item, const char* context, const std::size_t maxBytes)
{
    if (!item.isObject()) fail(kExitUsage, std::string(context) + " must be an object {path, sha256}");
    for (const auto& [key, value] : item.object)
    {
        (void)value;
        if (key != "path" && key != "sha256" && key != "name")
            fail(kExitUsage, std::string("Unknown field '") + key + "' in " + context);
    }
    BoundFile file;
    file.path = requiredString(item, "path", context);
    const std::string expected = lower(requiredString(item, "sha256", context));
    file.bytes = readFileBytes(file.path, maxBytes);
    file.sha256 = sha256Hex(file.bytes);
    if (file.sha256 != expected)
        fail(kExitConflict, std::string(context) + " sha256 does not match (stale file): " + file.path.string());
    return file;
}

// The pixel size an image file's header gives (PNG IHDR or JPEG frame),
// without decoding it; nullopt when the header cannot be read.
std::optional<std::pair<std::int32_t, std::int32_t>> imageHeaderSize(const std::span<const std::byte> bytes)
{
    static constexpr unsigned char kPng[8]{ 137, 80, 78, 71, 13, 10, 26, 10 };
    if (bytes.size() >= 24 && std::memcmp(bytes.data(), kPng, 8) == 0)
    {
        const auto be32 = [&bytes](const std::size_t at) {
            return (std::to_integer<std::uint32_t>(bytes[at]) << 24) | (std::to_integer<std::uint32_t>(bytes[at + 1]) << 16)
                | (std::to_integer<std::uint32_t>(bytes[at + 2]) << 8) | std::to_integer<std::uint32_t>(bytes[at + 3]);
        };
        const std::uint32_t width = be32(16);
        const std::uint32_t height = be32(20);
        if (width == 0 || height == 0 || width > 0x7FFFFFFFu || height > 0x7FFFFFFFu) return std::nullopt;
        return std::make_pair(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    }
    if (sed::assets::isJpeg(bytes))
    {
        try
        {
            const sed::assets::JpegInfo info = sed::assets::readJpegInfo(bytes);
            return std::make_pair(info.width, info.height);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// A texture side a MAT cel can have: a power of two from 8 to 1024.
bool isMatCelSide(const std::int32_t side)
{
    return side >= 8 && side <= 1024 && std::has_single_bit(static_cast<std::uint32_t>(side));
}

// mat-create: PNG or JPEG cels + a CMP palette -> an 8-bit indexed texture MAT with
// mipmaps, or explicit opaque RGB565 without a CMP. The mapping and dithering are RED's;
// the receipt reports the colour error so a reviewer can judge the result.
Json cmdMatCreate(const std::vector<std::string>& args)
{
    if (args.size() != 2)
        fail(kExitUsage, "mat-create requires <request.json|-> <new-output.mat>");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "mat-create request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "cels" && key != "palette" && key != "mipmaps" && key != "format"
            && key != "dither" && key != "reserveIndexZero" && key != "colorKey" && key != "guardEdges"
            && key != "resize" && key != "paint")
            fail(kExitUsage, "Unknown field '" + key + "' in mat-create request");
    }
    if (const Json* v = request.find("schemaVersion"))
        if (v->type != Json::Type::Number || v->number != 1) fail(kExitUsage, "unsupported schemaVersion (expected 1)");
    const Json* cels = request.find("cels");
    if ( !cels || cels->type != Json::Type::Array || cels->array.empty() || cels->array.size() > 16 )
    {
        fail(kExitUsage, "mat-create requires 'cels': 1..16 PNG or JPEG files {path, sha256}");
    }
    bool rgb565 = false;
    if ( const Json* format = request.find("format") )
    {
        if ( format->type != Json::Type::String || (format->str != "indexed8" && format->str != "rgb565") )
        {
            fail(kExitUsage, "'format' must be indexed8 or rgb565");
        }
        rgb565 = format->str == "rgb565";
    }
    std::optional<BoundFile> paletteFile;
    std::optional<sed::assets::CmpPalette> palette;
    if ( rgb565 )
    {
        for ( const auto* key : { "palette", "dither", "reserveIndexZero", "colorKey", "guardEdges" } )
        {
            if ( request.find(key) )
            {
                fail(kExitUsage, std::string("'") + key + "' is only supported for indexed8");
            }
        }
    }
    else
    {
        const Json* paletteJson = request.find("palette");
        if (!paletteJson) fail(kExitUsage, "mat-create requires 'palette' {path, sha256} (a CMP colormap)");
        paletteFile = readBoundFile(*paletteJson, "palette", 1u << 20);
        palette = sed::assets::parseCmpPalette(
            std::as_bytes(std::span(paletteFile->bytes.data(), paletteFile->bytes.size())));
        if (!palette) fail(kExitInput, "cannot parse CMP palette: " + paletteFile->path.string());
    }

    sed::assets::IndexedMatOptions options;
    if (const Json* v = request.find("mipmaps"))
    {
        if (v->type != Json::Type::Number || v->number != std::floor(v->number) || v->number < 1 || v->number > 4)
            fail(kExitUsage, "'mipmaps' must be an integer from 1 to 4");
        options.mipmaps = static_cast<std::int32_t>(v->number);
    }
    if (const Json* v = request.find("dither"))
    {
        if (v->type != Json::Type::Bool) fail(kExitUsage, "'dither' must be a boolean");
        options.dither = v->boolean;
    }
    if (const Json* v = request.find("reserveIndexZero"))
    {
        if (v->type != Json::Type::Bool) fail(kExitUsage, "'reserveIndexZero' must be a boolean");
        options.allowed[0] = !v->boolean;
    }
    if ( const Json* key = request.find("colorKey") )
    {
        if ( key->type != Json::Type::Bool ) fail(kExitUsage, "'colorKey' must be a boolean");
        options.colorKey = key->boolean;
    }
    const Json* guards = request.find("guardEdges");
    if ( guards )
    {
        if ( !options.colorKey || guards->type != Json::Type::Array || guards->array.size() > 4 )
            fail(kExitUsage, "'guardEdges' requires colorKey true and an array of up to four unique edges");
        for ( const auto& edge : guards->array )
        {
            bool* chosen = nullptr;
            if ( edge.type == Json::Type::String )
            {
                if ( edge.str == "left" ) chosen = &options.guardEdges.left;
                else if ( edge.str == "right" ) chosen = &options.guardEdges.right;
                else if ( edge.str == "top" ) chosen = &options.guardEdges.top;
                else if ( edge.str == "bottom" ) chosen = &options.guardEdges.bottom;
            }
            if ( !chosen || *chosen ) fail(kExitUsage, "'guardEdges' expects unique left, right, top or bottom edges");
            *chosen = true;
        }
    }
    // resize: scale every cel to a MAT size with an area filter, so a large
    // source (a 4096x4096 model texture) can become a 256x256 MAT.
    std::optional<std::pair<std::int32_t, std::int32_t>> resize;
    if (const Json* v = request.find("resize"))
    {
        const auto side = [v](const std::size_t i) {
            const Json* n = v->type == Json::Type::Array && v->array.size() == 2 ? &v->array[i] : nullptr;
            if (!n || n->type != Json::Type::Number || n->number != std::floor(n->number) || n->number < 0
                || n->number > 1024 || !isMatCelSide(static_cast<std::int32_t>(n->number)))
                fail(kExitUsage, "'resize' must be [width, height], each a power of two from 8 to 1024 (a MAT cel size)");
            return static_cast<std::int32_t>(n->number);
        };
        resize = std::make_pair(side(0), side(1));
    }

    // paint: lamp glows and lens fills painted into every cel (after resize),
    // in texels of the final cel, before the MAT is built.
    std::vector<sed::assets::GlowSpot> glows;
    std::vector<sed::assets::FillRect> fills;
    bool paintWrap = true;
    const Json* paintJson = request.find("paint");
    if (paintJson)
    {
        if (!paintJson->isObject()) fail(kExitUsage, "'paint' must be an object {glows?, fills?, wrap?}");
        for (const auto& [key, value] : paintJson->object)
        {
            (void)value;
            if (key != "glows" && key != "fills" && key != "wrap") fail(kExitUsage, "Unknown field '" + key + "' in paint");
        }
        const auto num = [](const Json& v, const char* what) {
            if (v.type != Json::Type::Number || !std::isfinite(v.number)) fail(kExitUsage, std::string("paint ") + what + " must be a finite number");
            return v.number;
        };
        const auto pair = [&](const Json& item, const char* key) {
            const Json* v = item.find(key);
            if (!v || v->type != Json::Type::Array || v->array.size() != 2) fail(kExitUsage, std::string("paint '") + key + "' must be [u, v]");
            return std::array<double, 2>{ num(v->array[0], key), num(v->array[1], key) };
        };
        const auto rgbOf = [&](const Json& item) {
            std::array<std::uint8_t, 3> rgb{ 255, 171, 0 };
            if (const Json* v = item.find("rgb"))
            {
                if (v->type != Json::Type::Array || v->array.size() != 3) fail(kExitUsage, "paint 'rgb' must be [r, g, b]");
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const double x = num(v->array[c], "rgb");
                    if (x < 0 || x > 255 || x != std::floor(x)) fail(kExitUsage, "paint 'rgb' channels must be integers from 0 to 255");
                    rgb[c] = static_cast<std::uint8_t>(x);
                }
            }
            return rgb;
        };
        if (const Json* list = paintJson->find("glows"))
        {
            if (list->type != Json::Type::Array || list->array.size() > 256) fail(kExitUsage, "paint 'glows' must be an array of at most 256");
            for (const Json& item : list->array)
            {
                if (!item.isObject()) fail(kExitUsage, "each glow must be an object {center, radius, rgb?, strength?}");
                for (const auto& [key, value] : item.object)
                {
                    (void)value;
                    if (key != "center" && key != "radius" && key != "rgb" && key != "strength") fail(kExitUsage, "Unknown field '" + key + "' in a glow");
                }
                sed::assets::GlowSpot g;
                const auto c = pair(item, "center"); const auto r = pair(item, "radius");
                g.centerU = c[0]; g.centerV = c[1]; g.radiusU = r[0]; g.radiusV = r[1]; g.rgb = rgbOf(item);
                if (const Json* s = item.find("strength")) g.strength = num(*s, "strength");
                if (!(g.radiusU > 0) || !(g.radiusV > 0) || g.strength < 0 || g.strength > 1)
                    fail(kExitUsage, "a glow needs positive radii and a strength from 0 to 1");
                glows.push_back(g);
            }
        }
        if (const Json* list = paintJson->find("fills"))
        {
            if (list->type != Json::Type::Array || list->array.size() > 256) fail(kExitUsage, "paint 'fills' must be an array of at most 256");
            for (const Json& item : list->array)
            {
                if (!item.isObject()) fail(kExitUsage, "each fill must be an object {min, max, rgb?}");
                for (const auto& [key, value] : item.object)
                {
                    (void)value;
                    if (key != "min" && key != "max" && key != "rgb") fail(kExitUsage, "Unknown field '" + key + "' in a fill");
                }
                sed::assets::FillRect f;
                const auto a = pair(item, "min"); const auto b = pair(item, "max");
                f.minU = a[0]; f.minV = a[1]; f.maxU = b[0]; f.maxV = b[1]; f.rgb = rgbOf(item);
                fills.push_back(f);
            }
        }
        if (const Json* v = paintJson->find("wrap"))
        {
            if (v->type != Json::Type::Bool) fail(kExitUsage, "paint 'wrap' must be a boolean");
            paintWrap = v->boolean;
        }
        if (glows.empty() && fills.empty()) fail(kExitUsage, "'paint' needs at least one glow or fill");
    }
    std::vector<sed::assets::RgbaImage> images;
    Json sources = Json::makeArray();
    for (const Json& item : cels->array)
    {
        const BoundFile cel = readBoundFile(item, "cels entry", 64u << 20);
        const auto bytes = std::as_bytes(std::span(cel.bytes.data(), cel.bytes.size()));
        const bool jpeg = sed::assets::isJpeg(bytes);
        // Without resize a cel must already be a MAT size: refuse a larger
        // image from its header, before it is decoded.
        if (const auto size = imageHeaderSize(bytes);
            size && !resize && (!isMatCelSide(size->first) || !isMatCelSide(size->second)))
            failCoded(kExitRefusal, "matCreateRefused", "matCreateRefused: " + cel.path.string() + " is "
                + std::to_string(size->first) + "x" + std::to_string(size->second)
                + "; a MAT cel must be a power of two from 8 to 1024 each way - add 'resize': [width, height] "
                  "(for example [256, 256]) to scale the cels with an area filter");
        sed::assets::RgbaImage image;
        try
        {
            image = jpeg ? sed::assets::decodeJpeg(bytes) : sed::assets::decodePng(bytes);
        }
        catch (const std::exception& e)
        {
            if (jpeg) failCoded(kExitInput, "invalidJpeg", "invalidJpeg: " + cel.path.string() + ": " + e.what());
            failCoded(kExitInput, "invalidPng", "invalidPng: " + cel.path.string() + ": " + e.what()
                + " (cels must be PNG or baseline JPEG files)");
        }
        Json row = Json::makeObject();
        row.set("path", Json::makeString(cel.path.string()));
        row.set("sha256", Json::makeString(cel.sha256));
        row.set("format", Json::makeString(jpeg ? "jpeg" : "png"));
        row.set("width", Json::makeNumber(image.width));
        row.set("height", Json::makeNumber(image.height));
        sources.array.push_back(std::move(row));
        if (resize)
        {
            try
            {
                image = sed::assets::resizeRgbaArea(image, resize->first, resize->second, options.colorKey);
            }
            catch (const std::invalid_argument& e)
            {
                failCoded(kExitRefusal, "matCreateRefused", std::string("matCreateRefused: ") + e.what());
            }
        }
        if (paintJson)
        {
            try { sed::assets::paintGlows(image, glows, fills, paintWrap); }
            catch (const std::invalid_argument& e) { fail(kExitUsage, std::string("mat-create paint: ") + e.what()); }
        }
        images.push_back(std::move(image));
    }
    sed::assets::IndexedMatResult built;
    try
    {
        built = rgb565 ? sed::assets::buildRgb565Mat(images, { options.mipmaps })
                       : sed::assets::buildIndexedMat(images, *palette, options);
    }
    catch (const std::invalid_argument& e)
    {
        failCoded(kExitRefusal, "matCreateRefused", std::string("matCreateRefused: ") + e.what());
    }
    // Read back through the MAT reader before publishing.
    try
    {
        std::vector<std::byte> raw(built.bytes.size());
        std::memcpy(raw.data(), built.bytes.data(), built.bytes.size());
        const auto check = sed::assets::MatFile::parse(std::move(raw), palette ? &*palette : nullptr);
        if (check.celCount() != built.report.cels)
            fail(kExitInput, "mat-create readback cel count differs");
        if ( check.hasColorKey() != options.colorKey ) fail(kExitInput, "mat-create readback color key differs");
        if ( rgb565 && (!check.isTrueColor() || check.bitsPerPixel() != 16) )
        {
            fail(kExitInput, "mat-create readback is not RGB565");
        }
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("mat-create output does not read back: ") + e.what());
    }
    const fs::path outputPath = args[1];
    if (lower(outputPath.extension().string()) != ".mat")
        fail(kExitUsage, "mat-create output must end in .mat");
    if ( paletteFile )
    {
        checkNewOutputTarget(paletteFile->path, outputPath, true);
    }
    else
    {
        for ( const Json& source : sources.array )
        {
            checkNewOutputTarget(source.find("path")->str, outputPath, true);
        }
    }
    writeNewFileAtomic(outputPath, built.bytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("mat-create"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(built.bytes)));
    out.set("bytes", Json::makeNumber(static_cast<double>(built.bytes.size())));
    out.set("width", Json::makeNumber(built.report.width));
    out.set("height", Json::makeNumber(built.report.height));
    out.set("mipmaps", Json::makeNumber(built.report.mipmaps));
    out.set("cels", Json::makeNumber(built.report.cels));
    out.set("coloursUsed", Json::makeNumber(built.report.coloursUsed));
    out.set("meanColourError", Json::makeNumber(std::round(built.report.meanError * 1000.0) / 1000.0));
    if ( rgb565 )
    {
        out.set("format", Json::makeString("rgb565"));
        out.set("bitsPerPixel", Json::makeNumber(16));
    }
    else
    {
        out.set("dither", Json::makeBool(options.dither));
        if ( options.colorKey )
        {
            out.set("colorKey", Json::makeBool(true));
            if ( guards ) out.set("guardEdges", *guards);
        }
    }
    if (paintJson)
    {
        Json painted = Json::makeObject();
        painted.set("glows", Json::makeNumber(static_cast<double>(glows.size())));
        painted.set("fills", Json::makeNumber(static_cast<double>(fills.size())));
        painted.set("wrap", Json::makeBool(paintWrap));
        out.set("paint", std::move(painted));
    }
    if (resize)
    {
        Json resized = Json::makeObject();
        resized.set("width", Json::makeNumber(resize->first));
        resized.set("height", Json::makeNumber(resize->second));
        resized.set("filter", Json::makeString("area"));
        out.set("resize", std::move(resized));
    }
    if ( paletteFile )
    {
        out.set("paletteSha256", Json::makeString(paletteFile->sha256));
    }
    out.set("sources", std::move(sources));
    out.set("note", Json::makeString("Texture density is the level's choice: at JK's usual 320 texels per JKU a "
        + std::to_string(built.report.width) + "-pixel-wide texture covers "
        + std::to_string(built.report.width / 320.0).substr(0, 5) + " JKU."));
    return out;
}

// cmp-create: a master CMP + an RGB tint -> a tinted colormap for a sector,
// the way the stock red maps are made. Its light tables give the colour in
// the 8-bit renderer; its header tint gives it in the hardware renderer.
Json cmdCmpCreate(const std::vector<std::string>& args)
{
    if (args.size() != 2)
        fail(kExitUsage, "cmp-create requires <request.json|-> <new-output.cmp>");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "cmp-create request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "master" && key != "tint")
            fail(kExitUsage, "Unknown field '" + key + "' in cmp-create request");
    }
    checkSchemaVersion(request);
    const Json* tintJson = request.find("tint");
    if ( !tintJson || tintJson->type != Json::Type::Array || tintJson->array.size() != 3 )
        fail(kExitUsage, "cmp-create requires 'tint': [red, green, blue], each from 0 to 4");
    std::array<float, 3> tint{};
    for ( std::size_t c = 0; c < 3; ++c )
    {
        const Json& channel = tintJson->array[c];
        if ( channel.type != Json::Type::Number || !std::isfinite(channel.number) || channel.number < 0.0
            || channel.number > 4.0 )
            fail(kExitUsage, "cmp-create 'tint' channels must be numbers from 0 to 4");
        tint[c] = static_cast<float>(channel.number);
    }
    const Json* masterJson = request.find("master");
    if (!masterJson) fail(kExitUsage, "cmp-create requires 'master' {path, sha256} (the level's master CMP)");
    const BoundFile master = readBoundFile(*masterJson, "master", 1u << 20);
    const fs::path outputPath = args[1];
    if (lower(outputPath.extension().string()) != ".cmp")
        fail(kExitUsage, "cmp-create output must end in .cmp");
    std::vector<std::byte> built;
    try
    {
        built = sed::assets::makeTintedCmp(std::as_bytes(std::span(master.bytes.data(), master.bytes.size())),
                                           sed::assets::CmpTint{ tint[0], tint[1], tint[2] });
    }
    catch (const std::invalid_argument& e)
    {
        fail(kExitInput, std::string("cmp-create: ") + e.what() + ": " + master.path.string());
    }
    // Read back before publishing.
    const auto readTint = sed::assets::parseCmpTint(built);
    if ( !sed::assets::parseCmp(built) || !readTint || readTint->r != tint[0] || readTint->g != tint[1]
        || readTint->b != tint[2] )
        fail(kExitInput, "cmp-create output does not read back");
    checkNewOutputTarget(master.path, outputPath, true);
    const std::string bytes(reinterpret_cast<const char*>(built.data()), built.size());
    writeNewFileAtomic(outputPath, bytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cmp-create"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(bytes)));
    out.set("bytes", Json::makeNumber(static_cast<double>(bytes.size())));
    out.set("masterSha256", Json::makeString(master.sha256));
    Json rgb = Json::makeArray();
    for ( const float channel : tint ) rgb.array.push_back(Json::makeNumber(channel));
    out.set("tint", std::move(rgb));
    out.set("lightLevels", Json::makeNumber(64));
    out.set("transparencyTable", Json::makeBool(bytes.size() > 832u + 64u * 256u));
    out.set("note", Json::makeString("Register the CMP with registerColormap and give a sector it with setSectorColormap; "
        "pack it in the level's GOB under misc\\cmp. The 8-bit renderer draws the sector through these light tables, "
        "the hardware renderer multiplies it by the header tint, and a snapshot with \"lighting\": \"baked\" "
        "previews the hardware tint. It carries the master's transparency table when the master has one, so a "
        "translucent face in the sector has a table to blend through in the 8-bit renderer."));
    return out;
}

// cmp-master-create: a level master colormap with new colours placed the way
// the stock level masters place theirs, in the per-level slots 192-254.
Json cmpMasterRgb(const Json& value, const char* context)
{
    if ( value.type != Json::Type::Array || value.array.size() != 3 )
        fail(kExitUsage, std::string(context) + " must be [red, green, blue], each an integer from 0 to 255");
    std::array<std::uint8_t, 3> rgb{};
    for ( std::size_t c = 0; c < 3; ++c )
    {
        const Json& channel = value.array[c];
        if ( channel.type != Json::Type::Number || !std::isfinite(channel.number) || channel.number < 0.0
            || channel.number > 255.0 || channel.number != std::floor(channel.number) )
            fail(kExitUsage, std::string(context) + " channels must be integers from 0 to 255");
        rgb[c] = static_cast<std::uint8_t>(channel.number);
    }
    return Json::makeNumber(rgb[0] * 65536.0 + rgb[1] * 256.0 + rgb[2]);
}

Json cmdCmpMasterCreate(const std::vector<std::string>& args)
{
    if (args.size() != 2)
        fail(kExitUsage, "cmp-master-create requires <request.json|-> <new-output.cmp>");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "cmp-master-create request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "master" && key != "colors" && key != "ramps" && key != "slots"
            && key != "protectMaterials" && key != "protectArchives")
            fail(kExitUsage, "Unknown field '" + key + "' in cmp-master-create request");
    }
    checkSchemaVersion(request);
    std::vector<sed::assets::CmpPaletteAddition> additions;
    const auto addition = [&](const std::uint32_t packed, const bool fullbright) {
        sed::assets::CmpPaletteAddition a;
        a.color = sed::assets::CmpColor{ static_cast<std::uint8_t>(packed >> 16), static_cast<std::uint8_t>((packed >> 8) & 0xFFu),
                                         static_cast<std::uint8_t>(packed & 0xFFu) };
        a.fullbright = fullbright;
        additions.push_back(a);
    };
    const auto fullbrightOf = [](const Json& item, const char* context) {
        const Json* f = item.find("fullbright");
        if ( !f ) return false;
        if ( f->type != Json::Type::Bool ) fail(kExitUsage, std::string(context) + " fullbright must be boolean");
        return f->boolean;
    };
    if ( const Json* colors = request.find("colors") )
    {
        if ( colors->type != Json::Type::Array || colors->array.size() > 255 )
            fail(kExitUsage, "cmp-master-create 'colors' must be an array of at most 255 colours");
        for ( const Json& item : colors->array )
        {
            if ( !item.isObject() ) fail(kExitUsage, "each colour must be an object {rgb, fullbright?}");
            for ( const auto& [key, value] : item.object )
            {
                (void)value;
                if ( key != "rgb" && key != "fullbright" ) fail(kExitUsage, "Unknown field '" + key + "' in a colour");
            }
            const Json* rgb = item.find("rgb");
            if ( !rgb ) fail(kExitUsage, "each colour requires 'rgb'");
            addition(static_cast<std::uint32_t>(cmpMasterRgb(*rgb, "colour rgb").number), fullbrightOf(item, "colour"));
        }
    }
    if ( const Json* ramps = request.find("ramps") )
    {
        if ( ramps->type != Json::Type::Array || ramps->array.size() > 63 )
            fail(kExitUsage, "cmp-master-create 'ramps' must be an array of at most 63 ramps");
        for ( const Json& item : ramps->array )
        {
            if ( !item.isObject() ) fail(kExitUsage, "each ramp must be an object {rgb, steps, fullbright?}");
            for ( const auto& [key, value] : item.object )
            {
                (void)value;
                if ( key != "rgb" && key != "steps" && key != "fullbright" ) fail(kExitUsage, "Unknown field '" + key + "' in a ramp");
            }
            const Json* rgb = item.find("rgb");
            const Json* steps = item.find("steps");
            if ( !rgb || !steps ) fail(kExitUsage, "each ramp requires 'rgb' and 'steps'");
            if ( steps->type != Json::Type::Number || steps->number != std::floor(steps->number) || steps->number < 2
                || steps->number > 63 )
                fail(kExitUsage, "ramp 'steps' must be an integer from 2 to 63");
            const auto top = static_cast<std::uint32_t>(cmpMasterRgb(*rgb, "ramp rgb").number);
            const int n = static_cast<int>(steps->number);
            const bool fb = fullbrightOf(item, "ramp");
            for ( int k = 1; k <= n; ++k )
            {
                const auto channel = [&](const std::uint32_t c) {
                    return static_cast<std::uint32_t>(std::lround(static_cast<double>(c) * k / n));
                };
                addition((channel(top >> 16) << 16) | (channel((top >> 8) & 0xFFu) << 8) | channel(top & 0xFFu), fb);
            }
        }
    }
    if ( additions.empty() ) fail(kExitUsage, "cmp-master-create requires at least one colour or ramp");
    if ( additions.size() > 255 ) fail(kExitUsage, "cmp-master-create: at most 255 colours in all");
    int first = 192, last = 254;
    if ( const Json* slots = request.find("slots") )
    {
        if ( slots->type != Json::Type::Array || slots->array.size() != 2 )
            fail(kExitUsage, "cmp-master-create 'slots' must be [first, last]");
        for ( const Json& v : slots->array )
            if ( v.type != Json::Type::Number || v.number != std::floor(v.number) || v.number < 0 || v.number > 255 )
                fail(kExitUsage, "cmp-master-create 'slots' entries must be integers from 0 to 255");
        first = static_cast<int>(slots->array[0].number);
        last = static_cast<int>(slots->array[1].number);
        if ( first > last ) fail(kExitUsage, "cmp-master-create 'slots' first must not exceed last");
    }
    const Json* masterJson = request.find("master");
    if (!masterJson) fail(kExitUsage, "cmp-master-create requires 'master' {path, sha256}");
    const BoundFile master = readBoundFile(*masterJson, "master", 1u << 20);
    const fs::path outputPath = args[1];
    if (lower(outputPath.extension().string()) != ".cmp")
        fail(kExitUsage, "cmp-master-create output must end in .cmp");

    std::array<bool, 256> protectedIdx{};
    std::array<std::string, 256> usedBy{};
    std::size_t protectedMaterials = 0;
    const auto protect = [&](const std::string& label, const std::span<const std::byte> bytes) {
        std::array<bool, 256> used{};
        try { used = sed::assets::matPaletteIndices(bytes); }
        catch (const std::exception& e) { fail(kExitInput, "cmp-master-create: protected MAT " + label + " is unreadable: " + e.what()); }
        for ( std::size_t i = 0; i < 256; ++i )
        {
            if ( used[i] && !protectedIdx[i] ) { protectedIdx[i] = true; usedBy[i] = label; }
        }
        ++protectedMaterials;
    };
    if ( const Json* mats = request.find("protectMaterials") )
    {
        if ( mats->type != Json::Type::Array || mats->array.size() > 64 )
            fail(kExitUsage, "cmp-master-create 'protectMaterials' must be an array of at most 64 captured MATs");
        for ( const Json& item : mats->array )
        {
            const BoundFile mat = readBoundFile(item, "protectMaterials", 16u << 20);
            protect(mat.path.filename().string(), std::as_bytes(std::span(mat.bytes.data(), mat.bytes.size())));
        }
    }
    if ( const Json* archives = request.find("protectArchives") )
    {
        Json sub = Json::makeObject();
        sub.set("resourceArchives", *archives);
        const CogArchiveSet set = loadCogArchives(sub);
        for ( const auto& archive : set.archives )
            for ( const auto& entry : archive )
                if ( lower(entry.entryName).ends_with(".mat") )
                    protect(entry.entryName, std::as_bytes(std::span(entry.text.data(), entry.text.size())));
    }
    std::vector<std::uint8_t> candidates;
    for ( int s = first; s <= last; ++s ) candidates.push_back(static_cast<std::uint8_t>(s));
    sed::assets::CmpMasterBuild built;
    try
    {
        built = sed::assets::makeMasterCmp(std::as_bytes(std::span(master.bytes.data(), master.bytes.size())), additions,
                                           candidates, protectedIdx);
    }
    catch (const std::invalid_argument& e)
    {
        fail(kExitInput, std::string("cmp-master-create: ") + e.what());
    }
    const auto readBack = sed::assets::parseCmp(built.bytes);
    if ( !readBack ) fail(kExitInput, "cmp-master-create output does not read back");
    for ( std::size_t a = 0; a < additions.size(); ++a )
    {
        const auto& c = readBack->palette[built.assignedSlots[a]];
        if ( c.r != additions[a].color.r || c.g != additions[a].color.g || c.b != additions[a].color.b )
            fail(kExitInput, "cmp-master-create output does not read back");
    }
    checkNewOutputTarget(master.path, outputPath, true);
    const std::string bytes(reinterpret_cast<const char*>(built.bytes.data()), built.bytes.size());
    writeNewFileAtomic(outputPath, bytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cmp-master-create"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(bytes)));
    out.set("bytes", Json::makeNumber(static_cast<double>(bytes.size())));
    out.set("masterSha256", Json::makeString(master.sha256));
    Json placed = Json::makeArray();
    for ( std::size_t a = 0; a < additions.size(); ++a )
    {
        Json row = Json::makeObject();
        row.set("slot", Json::makeNumber(built.assignedSlots[a]));
        Json rgb = Json::makeArray();
        for ( const std::uint8_t c : { additions[a].color.r, additions[a].color.g, additions[a].color.b } )
            rgb.array.push_back(Json::makeNumber(c));
        row.set("rgb", std::move(rgb));
        row.set("fullbright", Json::makeBool(additions[a].fullbright));
        placed.array.push_back(std::move(row));
    }
    out.set("colors", std::move(placed));
    Json skipped = Json::makeArray();
    for ( const std::uint8_t slot : built.skippedSlots )
    {
        Json row = Json::makeObject();
        row.set("slot", Json::makeNumber(slot));
        row.set("reason", Json::makeString(protectedIdx[slot] ? "protected" : "fullbright"));
        if ( protectedIdx[slot] ) row.set("usedBy", Json::makeString(usedBy[slot]));
        skipped.array.push_back(std::move(row));
    }
    out.set("skippedSlots", std::move(skipped));
    out.set("protectedMaterials", Json::makeNumber(static_cast<double>(protectedMaterials)));
    out.set("lightEntriesComputed", Json::makeNumber(static_cast<double>(built.lightEntriesComputed)));
    out.set("transparencyEntriesComputed", Json::makeNumber(static_cast<double>(built.transparencyEntriesComputed)));
    out.set("transparencyTable", Json::makeBool(bytes.size() > 832u + 64u * 256u));
    out.set("note", Json::makeString("Use it as the level's master colormap (World Colormap 0) and pack it in the level's GOB "
        "under misc\\cmp. Make the level's 8-bit MATs against it (mat-create with this file as palette) so they can use "
        "the new colours; tinted sector colormaps (cmp-create) should take it as their master too. Textures that do not "
        "use the replaced slots look exactly as they did. Compare both renderers in the game."));
    return out;
}

// model-import: a game-ready mesh (Wavefront OBJ or binary glTF) -> a JK 3DO
// 2.1 model. The core importer converts axes, texels, normals and polygons,
// splits meshes over JK's limits and refuses what JK cannot hold; the core
// writer's text is read back before it is published.
Json cmdModelImport(const std::vector<std::string>& args)
{
    if (args.size() != 2)
        fail(kExitUsage, "model-import requires <request.json|-> <new-output.3do>");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "model-import request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "source" && key != "materials" && key != "scale" && key != "upAxis"
            && key != "origin" && key != "meshName" && key != "allowUntextured" && key != "split"
            && key != "exportBaseColor" && key != "normalPolicy")
            fail(kExitUsage, "Unknown field '" + key + "' in model-import request");
    }
    checkSchemaVersion(request);
    const fs::path outputPath = args[1];
    if (lower(outputPath.extension().string()) != ".3do")
        fail(kExitUsage, "model-import output must end in .3do");
    const Json* sourceJson = request.find("source");
    if (!sourceJson)
        fail(kExitUsage, "model-import requires 'source' {path, sha256} (a Wavefront OBJ or binary glTF .glb file)");
    const Json* materialsJson = request.find("materials");
    if (!materialsJson || materialsJson->type != Json::Type::Array || materialsJson->array.size() > 256)
        fail(kExitUsage, "model-import requires 'materials': 0..256 {name, mat, size [width, height] | matFile {path, sha256}}");

    sed::assets::ObjImportOptions options;
    if ( const Json* value = request.find("normalPolicy") )
    {
        if ( value->type != Json::Type::String || (value->str != "preserve" && value->str != "recalculate") )
        {
            fail(kExitUsage, "'normalPolicy' must be 'preserve' or 'recalculate'");
        }
        options.normalPolicy = value->str == "recalculate" ? sed::assets::ObjNormalPolicy::Recalculate
            : sed::assets::ObjNormalPolicy::Preserve;
    }
    if (const Json* v = request.find("scale"))
    {
        if (v->type != Json::Type::Number || !std::isfinite(v->number) || v->number <= 0 || v->number > 1000000)
            fail(kExitUsage, "'scale' must be a number greater than 0 and at most 1000000");
        options.scale = v->number;
    }
    if (const Json* v = request.find("upAxis"))
    {
        if (v->type != Json::Type::String || (v->str != "y" && v->str != "z"))
            fail(kExitUsage, "'upAxis' must be \"y\" (the OBJ and glTF convention) or \"z\"");
        options.upAxis = v->str == "z" ? sed::assets::ObjUpAxis::Z : sed::assets::ObjUpAxis::Y;
    }
    if (const Json* v = request.find("origin"))
    {
        if (v->type != Json::Type::String || (v->str != "base" && v->str != "center" && v->str != "none"))
            fail(kExitUsage, "'origin' must be \"base\", \"center\" or \"none\"");
        options.origin = v->str == "center" ? sed::assets::ObjOrigin::Center
            : v->str == "none" ? sed::assets::ObjOrigin::None : sed::assets::ObjOrigin::Base;
    }
    if (const Json* v = request.find("meshName"))
    {
        if (v->type != Json::Type::String || v->str.empty())
            fail(kExitUsage, "'meshName' must be a non-empty string (the prefix of every mesh name)");
        options.meshNamePrefix = v->str;
    }
    if (const Json* v = request.find("allowUntextured"))
    {
        if (v->type != Json::Type::Bool) fail(kExitUsage, "'allowUntextured' must be a boolean");
        options.allowUntextured = v->boolean;
    }
    if (const Json* v = request.find("split"))
    {
        if (v->type != Json::Type::Bool) fail(kExitUsage, "'split' must be a boolean");
        options.split = v->boolean;
    }
    std::optional<fs::path> baseColorPath;
    if (const Json* v = request.find("exportBaseColor"))
    {
        if (v->type != Json::Type::String || v->str.empty())
            fail(kExitUsage, "'exportBaseColor' must be the path of a new .jpg or .png file");
        baseColorPath = fs::path(v->str);
    }
    const BoundFile source = readBoundFile(*sourceJson, "source", sed::assets::kGlbImportMaxSourceBytes);
    const auto sourceBytes = std::as_bytes(std::span(source.bytes.data(), source.bytes.size()));
    const bool glb = sed::assets::isGlbModel(sourceBytes);
    if (baseColorPath && !glb)
        fail(kExitUsage, "'exportBaseColor' needs a binary glTF source (an OBJ names its textures in its .mtl file)");
    std::map<std::string, Json> sizeSources; // material name -> where its texel size came from
    for (const Json& item : materialsJson->array)
    {
        if (!item.isObject()) fail(kExitUsage, "model-import materials must be objects {name, mat, size|matFile}");
        for (const auto& [key, value] : item.object)
        {
            (void)value;
            if (key != "name" && key != "mat" && key != "size" && key != "matFile")
                fail(kExitUsage, "Unknown field '" + key + "' in model-import material");
        }
        sed::assets::ObjImportMaterial material;
        material.name = requiredString(item, "name", "model-import material");
        material.mat = requiredString(item, "mat", "model-import material");
        const Json* size = item.find("size");
        const Json* matFile = item.find("matFile");
        if ((size == nullptr) == (matFile == nullptr))
            fail(kExitUsage, "model-import material '" + material.name
                + "' needs exactly one of 'size' [width, height] or 'matFile' {path, sha256}");
        Json from = Json::makeObject();
        if (size)
        {
            const auto texels = [&](const std::size_t i) {
                const Json* n = size->type == Json::Type::Array && size->array.size() == 2 ? &size->array[i] : nullptr;
                if (!n || n->type != Json::Type::Number || n->number != std::floor(n->number) || n->number < 1
                    || n->number > 4096)
                    fail(kExitUsage, "model-import material '" + material.name
                        + "' size must be [width, height], integers from 1 to 4096 (texels)");
                return static_cast<std::int32_t>(n->number);
            };
            material.width = texels(0);
            material.height = texels(1);
            from.set("from", Json::makeString("size"));
        }
        else
        {
            const BoundFile mat = readBoundFile(*matFile, "matFile", 64u << 20);
            std::optional<sed::assets::MatFile> parsed;
            try
            {
                std::vector<std::byte> raw(mat.bytes.size());
                std::memcpy(raw.data(), mat.bytes.data(), mat.bytes.size());
                parsed = sed::assets::MatFile::parse(std::move(raw));
            }
            catch (const std::exception& e)
            {
                failCoded(kExitInput, "invalidMat", "invalidMat: " + mat.path.string() + ": " + e.what());
            }
            if (parsed->isColor() || parsed->cels().empty())
                failCoded(kExitRefusal, "matFileHasNoTexture", "matFileHasNoTexture: " + mat.path.string()
                    + " is a colour MAT without a texture size; give 'size' for material '" + material.name + "'");
            material.width = parsed->cels().front().width;
            material.height = parsed->cels().front().height;
            from.set("from", Json::makeString("matFile"));
            from.set("path", Json::makeString(mat.path.string()));
            from.set("sha256", Json::makeString(mat.sha256));
        }
        sizeSources[material.name] = std::move(from);
        options.materials.push_back(std::move(material));
    }

    sed::assets::ObjImportResult imported;
    try
    {
        imported = sed::assets::importModel(source.bytes, options);
    }
    catch (const sed::assets::ObjImportError& e)
    {
        const int exitCode = e.kind() == sed::assets::ObjImportErrorKind::InvalidOptions ? kExitUsage
            : e.kind() == sed::assets::ObjImportErrorKind::Malformed ? kExitInput : kExitRefusal;
        failCoded(exitCode, e.code(), e.code() + ": " + e.what());
    }
    // exportBaseColor: the one base colour image the used materials show.
    const sed::assets::GltfBaseColorImage* baseColor = nullptr;
    if (baseColorPath)
    {
        const auto& images = imported.gltf.baseColorImages;
        if (images.empty())
            failCoded(kExitRefusal, "gltfNoBaseColorImage",
                "gltfNoBaseColorImage: no material the model uses has a base colour texture to export");
        if (images.size() > 1)
        {
            std::string list;
            for (const auto& image : images)
            {
                std::string materials;
                for (const std::string& name : image.materials) materials += (materials.empty() ? "'" : ", '") + name + "'";
                list += (list.empty() ? "" : "; ") + std::string("image ") + std::to_string(image.image) + " for " + materials;
            }
            failCoded(kExitRefusal, "gltfBaseColorAmbiguous", "gltfBaseColorAmbiguous: the used materials show "
                + std::to_string(images.size()) + " different base colour images (" + list
                + "); exportBaseColor writes one, so it needs a model whose materials share one image");
        }
        baseColor = &images.front();
        if (!baseColor->problem.empty())
            failCoded(kExitRefusal, "gltfBaseColorUnavailable", "gltfBaseColorUnavailable: the base colour image of material '"
                + baseColor->materials.front() + "' cannot be exported: " + baseColor->problem);
        const bool jpeg = baseColor->mimeType == "image/jpeg";
        const std::string extension = lower(baseColorPath->extension().string());
        if (jpeg ? (extension != ".jpg" && extension != ".jpeg") : extension != ".png")
            fail(kExitUsage, std::string("'exportBaseColor' must end in ") + (jpeg ? ".jpg or .jpeg" : ".png")
                + ": the base colour image is stored as " + (jpeg ? "JPEG" : "PNG") + " and is written unchanged");
    }
    std::string text;
    try
    {
        text = sed::assets::saveTo3DO(imported.model);
    }
    catch (const std::exception& e)
    {
        failCoded(kExitRefusal, "modelImportRefused", std::string("modelImportRefused: ") + e.what());
    }
    // Read the written text back through the 3DO reader before publishing.
    std::string readBackProblem;
    try
    {
        sed::assets::Model3doLoadOptions raw;
        raw.applyHierarchyTransforms = false;
        const auto back = sed::assets::loadFrom3DO(std::as_bytes(std::span(text.data(), text.size())), raw);
        const sed::assets::Model3do& model = imported.model;
        bool same = std::fabs(back.version - 2.1) < 1e-9 && back.materials == model.materials
            && back.meshes.size() == model.meshes.size() && back.nodes.size() == model.nodes.size();
        for (std::size_t n = 0; same && n < model.nodes.size(); ++n)
            same = back.nodes[n].parent == model.nodes[n].parent && back.nodes[n].mesh == model.nodes[n].mesh;
        for (std::size_t m = 0; same && m < model.meshes.size(); ++m)
        {
            const auto& a = model.meshes[m];
            const auto& b = back.meshes[m];
            same = a.name == b.name && a.vertices.size() == b.vertices.size()
                && a.texVertices.size() == b.texVertices.size() && a.faces.size() == b.faces.size()
                && b.vertexNormals.size() == a.vertices.size() && b.faceNormals.size() == a.faces.size();
            for (std::size_t f = 0; same && f < a.faces.size(); ++f)
                same = a.faces[f].material == b.faces[f].material && a.faces[f].vertices == b.faces[f].vertices
                    && a.faces[f].texVertices == b.faces[f].texVertices;
            for (std::size_t t = 0; same && t < a.texVertices.size(); ++t)
                same = a.texVertices[t].u == b.texVertices[t].u && a.texVertices[t].v == b.texVertices[t].v;
        }
        if (!same) readBackProblem = "the reloaded meshes, faces, texels or materials differ";
    }
    catch (const std::exception& e)
    {
        readBackProblem = e.what();
    }
    if (!readBackProblem.empty()) fail(kExitInput, "model-import output does not read back: " + readBackProblem);
    checkNewOutputTarget(source.path, outputPath, true);
    if (baseColorPath)
    {
        std::error_code ec;
        const auto absModel = fs::absolute(outputPath, ec);
        const auto absImage = fs::absolute(*baseColorPath, ec);
        if (!ec && absModel == absImage) fail(kExitUsage, "'exportBaseColor' must differ from the 3DO output");
        checkNewOutputTarget(source.path, *baseColorPath, true);
    }
    writeNewFileAtomic(outputPath, text);
    if (baseColor)
        writeNewFileAtomic(*baseColorPath, std::string_view(reinterpret_cast<const char*>(baseColor->bytes.data()),
            baseColor->bytes.size()));

    const auto vector3 = [](const sed::TVector& v) {
        Json a = Json::makeArray();
        for (const double c : { v.x, v.y, v.z }) a.array.push_back(Json::makeNumber(c));
        return a;
    };
    const auto count = [](const std::size_t n) { return Json::makeNumber(static_cast<double>(n)); };
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("model-import"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(text)));
    out.set("bytes", count(text.size()));
    out.set("version", Json::makeString("2.1"));
    out.set("format", Json::makeString(glb ? "glb" : "obj"));
    Json sourceRow = Json::makeObject();
    sourceRow.set("path", Json::makeString(source.path.string()));
    sourceRow.set("sha256", Json::makeString(source.sha256));
    sourceRow.set("polygons", count(imported.polygons));
    out.set("source", std::move(sourceRow));
    out.set("scale", Json::makeNumber(options.scale));
    out.set("upAxis", Json::makeString(options.upAxis == sed::assets::ObjUpAxis::Z ? "z" : "y"));
    out.set("origin", Json::makeString(options.origin == sed::assets::ObjOrigin::Center ? "center"
        : options.origin == sed::assets::ObjOrigin::None ? "none" : "base"));
    out.set("split", Json::makeBool(options.split));
    out.set("normalPolicy", Json::makeString(options.normalPolicy == sed::assets::ObjNormalPolicy::Recalculate
        ? "recalculate" : "preserve"));
    out.set("normalSplitVertices", count(imported.normalSplitVertices));
    Json meshes = Json::makeArray();
    for (const sed::assets::ObjImportMeshReport& mesh : imported.meshes)
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(mesh.name));
        row.set("source", Json::makeString(mesh.source));
        row.set("part", count(mesh.part));
        row.set("parts", count(mesh.parts));
        row.set("vertices", count(mesh.vertices));
        row.set("textureVertices", count(mesh.textureVertices));
        row.set("faces", count(mesh.faces));
        row.set("triangulatedPolygons", count(mesh.triangulatedPolygons));
        row.set("radius", Json::makeNumber(mesh.radius));
        meshes.array.push_back(std::move(row));
    }
    out.set("meshes", std::move(meshes));
    Json splits = Json::makeArray();
    for (const sed::assets::ObjImportSplitReport& split : imported.splits)
    {
        Json row = Json::makeObject();
        row.set("source", Json::makeString(split.source));
        row.set("faces", count(split.faces));
        row.set("vertices", count(split.vertices));
        row.set("textureVertices", count(split.textureVertices));
        Json names = Json::makeArray();
        for (const std::string& name : split.meshes) names.array.push_back(Json::makeString(name));
        row.set("meshes", std::move(names));
        splits.array.push_back(std::move(row));
    }
    out.set("splits", std::move(splits));
    out.set("faces", count(imported.faces));
    out.set("vertices", count(imported.vertices));
    out.set("textureVertices", count(imported.textureVertices));
    out.set("triangulatedPolygons", count(imported.triangulatedPolygons));
    out.set("untexturedFaces", count(imported.untexturedFaces));
    out.set("degenerateTriangles", count(imported.degenerateTriangles));
    out.set("retailTestedFaces", count(sed::assets::kJkRetailTestedModelFaces));
    out.set("overRetailTestedSize", Json::makeBool(imported.overRetailTestedSize));
    out.set("radius", Json::makeNumber(imported.model.radius));
    Json bounds = Json::makeObject();
    bounds.set("min", vector3(imported.boundsMin));
    bounds.set("max", vector3(imported.boundsMax));
    out.set("bounds", std::move(bounds));
    out.set("insertOffset", vector3(imported.model.insertOffset));
    Json materials = Json::makeArray();
    for (const sed::assets::ObjImportMaterialReport& material : imported.materials)
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(material.name));
        row.set("mat", Json::makeString(material.mat));
        row.set("index", Json::makeNumber(material.index));
        row.set("width", Json::makeNumber(material.width));
        row.set("height", Json::makeNumber(material.height));
        row.set("faces", count(material.faces));
        row.set("size", sizeSources[material.name]);
        materials.array.push_back(std::move(row));
    }
    out.set("materials", std::move(materials));
    Json unused = Json::makeArray();
    for (const std::string& name : imported.unusedMaterials) unused.array.push_back(Json::makeString(name));
    out.set("unusedMaterials", std::move(unused));
    Json ignored = Json::makeObject();
    for (const auto& [keyword, n] : imported.ignoredStatements) ignored.set(keyword, count(n));
    out.set("ignoredStatements", std::move(ignored));
    const auto imageRow = [&](const sed::assets::GltfBaseColorImage& image) {
        Json row = Json::makeObject();
        row.set("image", count(image.image));
        row.set("name", Json::makeString(image.name));
        Json names = Json::makeArray();
        for (const std::string& name : image.materials) names.array.push_back(Json::makeString(name));
        row.set("materials", std::move(names));
        if (image.problem.empty())
        {
            row.set("mimeType", Json::makeString(image.mimeType));
            row.set("bytes", count(image.bytes.size()));
            if (const auto size = imageHeaderSize(std::as_bytes(std::span(image.bytes.data(), image.bytes.size()))))
            {
                row.set("width", Json::makeNumber(size->first));
                row.set("height", Json::makeNumber(size->second));
            }
        }
        else
        {
            row.set("problem", Json::makeString(image.problem));
        }
        return row;
    };
    if (glb)
    {
        const sed::assets::GltfImportReport& gltf = imported.gltf;
        Json report = Json::makeObject();
        report.set("nodes", count(gltf.nodes));
        report.set("primitives", count(gltf.primitives));
        report.set("sourceVertices", count(gltf.sourceVertices));
        report.set("uniquePositions", count(gltf.uniquePositions));
        report.set("sourceTriangles", count(gltf.sourceTriangles));
        report.set("mirroredNodes", count(gltf.mirroredNodes));
        Json notImported = Json::makeObject();
        for (const auto& [what, n] : gltf.ignored) notImported.set(what, count(n));
        report.set("ignored", std::move(notImported));
        Json images = Json::makeArray();
        for (const auto& image : gltf.baseColorImages) images.array.push_back(imageRow(image));
        report.set("baseColorImages", std::move(images));
        out.set("gltf", std::move(report));
    }
    if (baseColor)
    {
        const std::string_view bytes(reinterpret_cast<const char*>(baseColor->bytes.data()), baseColor->bytes.size());
        Json row = imageRow(*baseColor);
        row.set("output", Json::makeString(baseColorPath->string()));
        row.set("sha256", Json::makeString(sha256Hex(bytes)));
        const Json* width = row.find("width");
        const Json* height = row.find("height");
        if (width && height
            && (!isMatCelSide(static_cast<std::int32_t>(width->number))
                || !isMatCelSide(static_cast<std::int32_t>(height->number))))
            row.set("note", Json::makeString("a MAT cel must be a power of two from 8 to 1024 each way: give mat-create "
                "'resize': [w, h] (for example [256, 256]) to scale this image with an area filter"));
        out.set("baseColor", std::move(row));
    }
    out.set("note", Json::makeString("JK loads a model's materials by name from mat\\ and 3do\\mat\\: pack each listed MAT "
        "there with archive-create beside 3do\\<name>.3do, and name the 3DO in a template's model3d."));
    return out;
}

// archive-create: loose resource files -> one GOB/GOO, so custom MATs, 3DOs,
// COGs and templates can be mounted as hash-bound resourceArchives and
// packaged. Entries are read back by type where RED has a reader.
Json cmdArchiveCreate(const std::vector<std::string>& args)
{
    if ( args.size() != 2 )
    {
        fail(kExitUsage, "archive-create requires <request.json|-> <new-output.gob|.goo>");
    }
    const Json request = readRequest(args[0]);
    if ( !request.isObject() )
    {
        fail(kExitUsage, "archive-create request must be a JSON object");
    }
    for ( const auto& [key, value] : request.object )
    {
        (void)value;
        if ( key != "schemaVersion" && key != "entries" )
        {
            fail(kExitUsage, "Unknown field '" + key + "' in archive-create request");
        }
    }
    if ( const Json* v = request.find("schemaVersion") )
    {
        if ( v->type != Json::Type::Number || v->number != 1 )
        {
            fail(kExitUsage, "unsupported schemaVersion (expected 1)");
        }
    }
    const Json* entries = request.find("entries");
    if ( !entries || entries->type != Json::Type::Array || entries->array.empty() || entries->array.size() > 4096 )
    {
        fail(kExitUsage, "archive-create requires 'entries': 1..4096 {name, path, sha256}");
    }
    const fs::path outputPath = args[1];
    const std::string ext = lower(outputPath.extension().string());
    if ( ext != ".gob" && ext != ".goo" )
    {
        fail(kExitUsage, "archive-create output must end in .gob or .goo");
    }
    checkNewOutputTarget(fs::path(), outputPath, false);
    sed::assets::RuntimeArchiveResult built;
    try
    {
        sed::assets::RuntimeArchiveBuilder builder(entries->array.size());
        std::vector<std::string> seen;
        for ( const Json& item : entries->array )
        {
            if ( !item.isObject() )
            {
                fail(kExitUsage, "archive-create entries must be objects");
            }
            std::string name = requiredString(item, "name", "archive-create entry");
            // Retain protocol precedence: each destination is admitted before
            // reading its source; payload validation precedes the next row.
            try
            {
                name = sed::assets::canonicalRuntimeGobEntryName(name);
            }
            catch ( const std::invalid_argument& error )
            {
                failCoded(kExitUsage, "invalidArchiveEntryName", error.what());
            }
            if ( std::find(seen.begin(), seen.end(), name) != seen.end() )
            {
                failCoded(kExitRefusal, "duplicateArchiveEntry", "duplicateArchiveEntry: '" + name
                    + "' is named twice (names are case-insensitive)");
            }
            seen.push_back(name);
            Json bound = Json::makeObject();
            if ( const Json* p = item.find("path") )
            {
                bound.set("path", *p);
            }
            if ( const Json* s = item.find("sha256") )
            {
                bound.set("sha256", *s);
            }
            if ( const Json* p = item.find("path"); p && p->type == Json::Type::String )
            {
                std::error_code ec;
                const auto absEntry = fs::absolute(fs::path(p->str), ec);
                const auto absOut = fs::absolute(outputPath, ec);
                if ( !ec && absEntry == absOut )
                {
                    fail(kExitRefusal, "output must differ from every entry's source file");
                }
            }
            BoundFile file = readBoundFile(bound, "archive-create entry", 256u << 20);
            builder.add({std::move(name), std::move(file.bytes)});
        }
        built = builder.finish();
    }
    catch ( const sed::assets::RuntimeArchiveError& error )
    {
        using Code = sed::assets::RuntimeArchiveErrorCode;
        switch ( error.code() )
        {
        case Code::InvalidEntryName:
            failCoded(kExitUsage, "invalidArchiveEntryName", error.what());
        case Code::DuplicateEntry:
            failCoded(kExitRefusal, "duplicateArchiveEntry", error.what());
        case Code::ArchiveTooLarge:
            failCoded(kExitRefusal, "archiveTooLarge", error.what());
        case Code::InvalidEntry:
            failCoded(kExitInput, "invalidArchiveEntry", error.what());
        case Code::InvalidRequest:
            fail(kExitUsage, error.what());
        case Code::SourceTooLarge:
        case Code::Readback:
            fail(kExitInput, error.what());
        }
        fail(kExitInput, error.what());
    }
    Json rows = Json::makeArray();
    for ( const auto& entry : built.entries )
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(entry.name));
        row.set("bytes", Json::makeNumber(static_cast<double>(entry.bytes)));
        row.set("sha256", Json::makeString(entry.sha256));
        row.set("readBack", Json::makeString(entry.readBack));
        rows.array.push_back(std::move(row));
    }
    Json warnings = Json::makeArray();
    for ( const auto& warning : built.warnings )
    {
        warnings.array.push_back(Json::makeString(warning));
    }
    const auto& bytes = built.outputData;
    checkNewOutputTarget(fs::path(), outputPath, false);
    writeNewFileAtomic(outputPath, bytes);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("archive-create"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(bytes)));
    out.set("archiveSha256", Json::makeString(sha256Hex(bytes)));
    out.set("bytes", Json::makeNumber(static_cast<double>(bytes.size())));
    out.set("entries", std::move(rows));
    out.set("warnings", std::move(warnings));
    return out;
}

Json cmdCogCheck(const std::vector<std::string>& args)
{
    if (args.size() < 2)
        fail(kExitUsage, "cog-check requires <file.cog> <jk|mots|ijim> [--strict]");
    bool strict = false;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--strict") strict = true;
        else fail(kExitUsage, "cog-check unknown option: " + args[i]);
    }
    const auto target = sed::assets::parseCogPreflightTarget(args[1]);
    if (!target)
        fail(kExitUsage, "cog-check target must be jk, mots or ijim");
    const std::string text = readFileBytes(fs::path(args[0]));
    // E-11: lexical COG preflight against the measured verb/message
    // registries. This is not a compiler: it reports availability and
    // inventory findings, never rewrites or executes the script.
    const sed::assets::CogPreflightResult result = sed::assets::runCogPreflight(
        fs::path(args[0]).filename().string(), text, *target,
        sed::assets::CogPreflightOptions{ strict });
    const auto stringArray = [](const std::vector<std::string>& values) {
        Json array = Json::makeArray();
        for (const std::string& value : values) array.array.push_back(Json::makeString(value));
        return array;
    };
    const auto callArray = [](const auto& calls) {
        Json array = Json::makeArray();
        for (const auto& call : calls)
        {
            Json entry = Json::makeObject();
            entry.set("name", Json::makeString(call.name));
            Json lines = Json::makeArray();
            for (const std::int32_t line : call.lines)
                lines.array.push_back(Json::makeNumber(static_cast<double>(line)));
            entry.set("lines", std::move(lines));
            array.array.push_back(std::move(entry));
        }
        return array;
    };
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cog-check"));
    out.set("target", Json::makeString(std::string(sed::assets::cogPreflightTargetName(*target))));
    out.set("measured", Json::makeBool(result.measured));
    out.set("method", Json::makeString(result.method));
    out.set("sourceSha256", Json::makeString(result.sourceSha256));
    out.set("calls", Json::makeNumber(static_cast<double>(result.calls.size())));
    out.set("absentCalls", callArray(result.absentCalls));
    out.set("absentMessages", stringArray(result.absentMessages));
    out.set("duplicateLabels", callArray(result.duplicateLabels));
    out.set("missingMessageHandlers", stringArray(result.missingMessageHandlers));
    out.set("undeclaredIdentifiers", stringArray(result.undeclaredIdentifiers));
    out.set("warnings", stringArray(result.warnings));
    out.set("availabilityFindings",
        Json::makeNumber(static_cast<double>(result.availabilityFindings)));
    out.set("strictFailure", Json::makeBool(result.strictFailure));
    if (result.strictFailure)
        failCoded(kExitRefusal, "cogPreflightFindings",
            "cogPreflightFindings: strict preflight found absent verbs/messages (see report)");
    return out;
}

Json cmdCogCompile(const std::vector<std::string>& args)
{
    if (args.size() < 2)
        fail(kExitUsage, "cog-compile requires <file.cog> <jk|mots> [--strict] [--listing]");
    bool strict = false;
    bool listing = false;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--strict") strict = true;
        else if (args[i] == "--listing") listing = true;
        else fail(kExitUsage, "cog-compile unknown option: " + args[i]);
    }
    const auto target = sed::assets::parseCogPreflightTarget(args[1]);
    if (!target || *target == sed::assets::CogPreflightTarget::ijim)
        fail(kExitUsage, "cog-compile target must be jk or mots");
    const std::string text = readFileBytes(fs::path(args[0]));
    // Compiles the script the way the retail engine loads it; nothing is run.
    const sed::assets::CogCompileResult result =
        sed::assets::compileCog(fs::path(args[0]).filename().string(), text, *target);
    Json diagnostics = Json::makeArray();
    for (const auto& d : result.diagnostics)
    {
        Json entry = Json::makeObject();
        entry.set("severity", Json::makeString(std::string(sed::assets::cogDiagnosticSeverityName(d.severity))));
        entry.set("code", Json::makeString(d.code));
        entry.set("line", Json::makeNumber(static_cast<double>(d.line)));
        entry.set("message", Json::makeString(d.message));
        diagnostics.array.push_back(std::move(entry));
    }
    Json handlers = Json::makeArray();
    for (const auto& h : result.handlers)
    {
        Json entry = Json::makeObject();
        entry.set("message", Json::makeString(h.message));
        entry.set("messageId", Json::makeNumber(static_cast<double>(h.messageId)));
        entry.set("address", Json::makeNumber(static_cast<double>(h.address)));
        handlers.array.push_back(std::move(entry));
    }
    const auto limit = [](const std::size_t used, const std::size_t max) {
        Json entry = Json::makeObject();
        entry.set("used", Json::makeNumber(static_cast<double>(used)));
        entry.set("limit", Json::makeNumber(static_cast<double>(max)));
        return entry;
    };
    Json limits = Json::makeObject();
    limits.set("symbols", limit(result.limits.symbols, result.limits.symbolLimit));
    limits.set("links", limit(result.limits.links, result.limits.linkLimit));
    limits.set("handlers", limit(result.limits.handlers, result.limits.handlerLimit));
    limits.set("labels", limit(result.limits.labels, result.limits.labelLimit));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cog-compile"));
    out.set("target", Json::makeString(std::string(sed::assets::cogPreflightTargetName(*target))));
    out.set("sourceSha256", Json::makeString(result.sourceSha256));
    out.set("loads", Json::makeBool(result.loads));
    out.set("flags", Json::makeNumber(static_cast<double>(result.flags)));
    out.set("errors", Json::makeNumber(static_cast<double>(result.count(sed::assets::CogDiagnosticSeverity::error))));
    out.set("warnings", Json::makeNumber(static_cast<double>(result.count(sed::assets::CogDiagnosticSeverity::warning))));
    out.set("diagnostics", std::move(diagnostics));
    out.set("handlers", std::move(handlers));
    out.set("limits", std::move(limits));
    out.set("codeWords", Json::makeNumber(static_cast<double>(result.code.size())));
    if (listing) out.set("listing", Json::makeString(result.disassembly));
    if (strict && (!result.loads || result.count(sed::assets::CogDiagnosticSeverity::error) > 0))
        failCoded(kExitRefusal, "cogCompileErrors",
            "cogCompileErrors: the script has compile errors (see report)");
    return out;
}

std::filesystem::path& agentExecutable()
{
    static std::filesystem::path path;
    return path;
}

std::filesystem::path cogTemplateLibraryOrFail(const std::vector<std::string>& args, const std::size_t libraryArg)
{
    std::vector<fs::path> candidates;
    if (args.size() > libraryArg) candidates.push_back(args[libraryArg]);
    if (const char* env = std::getenv("RED_DATA_DIR")) candidates.push_back(fs::path(env) / "cog_templates");
    std::error_code ec;
    if (!agentExecutable().empty())
    {
        const fs::path exe = fs::weakly_canonical(agentExecutable(), ec);
        if (!ec) candidates.push_back(exe.parent_path() / "data" / "cog_templates");
    }
    candidates.push_back(fs::current_path(ec) / "data" / "cog_templates");
#ifdef RED_SOURCE_DATA_DIR
    candidates.push_back(fs::path(RED_SOURCE_DATA_DIR) / "cog_templates");
#endif
    const auto found = sed::assets::findCogTemplateLibrary(candidates);
    if (!found)
        fail(kExitInput, "RED's COG template library (data/cog_templates) isn't beside red-agent; pass its folder or set RED_DATA_DIR");
    return *found;
}

// cog-templates: the template library, or one set in detail.
Json cmdCogTemplates(const std::vector<std::string>& args)
{
    const fs::path library = cogTemplateLibraryOrFail(args, 1);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cog-templates"));
    out.set("library", Json::makeString(library.string()));
    const auto describe = [&](const sed::assets::CogTemplateSet& set, const bool detail) {
        Json row = Json::makeObject();
        row.set("id", Json::makeString(set.id));
        row.set("title", Json::makeString(set.title));
        row.set("kind", Json::makeString(set.kind));
        row.set("modes", Json::makeString(set.modes));
        row.set("summary", Json::makeString(set.summary));
        Json scripts = Json::makeArray();
        for (const auto& script : set.scripts)
        {
            Json s = Json::makeObject();
            s.set("file", Json::makeString(script.file));
            s.set("role", Json::makeString(script.role));
            s.set("bindings", Json::makeString(sed::assets::cogTemplateBindings(library, script)));
            scripts.array.push_back(std::move(s));
        }
        row.set("scripts", std::move(scripts));
        Json templates = Json::makeArray();
        for (const auto& name : set.templates) templates.array.push_back(Json::makeString(name));
        row.set("templates", std::move(templates));
        if (detail)
        {
            Json files = Json::makeArray();
            for (const auto& file : sed::assets::cogTemplateUserFiles(library, set)) files.array.push_back(Json::makeString(file));
            row.set("userFiles", std::move(files));
            Json rows = Json::makeArray();
            for (const auto& r : sed::assets::cogTemplateRows(library, set)) rows.array.push_back(Json::makeString(r));
            row.set("templateRows", std::move(rows));
            Json named = Json::makeArray();
            for (const auto& n : sed::assets::cogTemplateNamedTemplates(library, set)) named.array.push_back(Json::makeString(n));
            row.set("namedTemplates", std::move(named));
            row.set("credits", Json::makeString(set.credits));
        }
        return row;
    };
    if (!args.empty() && !args[0].empty() && args[0] != "-")
    {
        const auto* set = sed::assets::findCogTemplateSet(args[0]);
        if (!set) fail(kExitUsage, "cog-templates: unknown set; run cog-templates for the list");
        out.set("set", describe(*set, true));
        return out;
    }
    Json sets = Json::makeArray();
    for (const auto& set : sed::assets::cogTemplateCatalog()) sets.array.push_back(describe(set, false));
    out.set("sets", std::move(sets));
    return out;
}

// cog-template-write: copies a set's scripts, template rows and notes into a project.
Json cmdCogTemplateWrite(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "cog-template-write requires <set> <project-folder> [library-folder]");
    const auto* set = sed::assets::findCogTemplateSet(args[0]);
    if (!set) fail(kExitUsage, "cog-template-write: unknown set; run cog-templates for the list");
    const fs::path library = cogTemplateLibraryOrFail(args, 2);
    const auto copy = sed::assets::copyCogTemplateSet(library, *set, args[1]);
    if (!copy.error.empty()) fail(kExitInput, copy.error);
    const auto list = [](const std::vector<fs::path>& paths) {
        Json out = Json::makeArray();
        for (const auto& p : paths) out.array.push_back(Json::makeString(p.string()));
        return out;
    };
    if (!copy.conflicts.empty())
        failCoded(kExitRefusal, "cogTemplateConflict", "cogTemplateConflict: these files already exist with different contents, so nothing was written: "
            + toJsonString(list(copy.conflicts)));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cog-template-write"));
    out.set("set", Json::makeString(set->id));
    out.set("written", list(copy.written));
    out.set("unchanged", list(copy.unchanged));
    return out;
}

Json hierarchyIssuesJson(const std::vector<sed::assets::HierarchyIssue>& issues)
{
    Json out = Json::makeArray();
    for (const auto& issue : issues)
    {
        Json row = Json::makeObject();
        row.set("severity", Json::makeString(issue.severity));
        row.set("code", Json::makeString(issue.code));
        row.set("message", Json::makeString(issue.message));
        if (issue.node >= 0) row.set("node", Json::makeNumber(static_cast<double>(issue.node)));
        out.array.push_back(std::move(row));
    }
    return out;
}

sed::assets::Model3do loadRawModel(const fs::path& path, std::string* bytesOut = nullptr)
{
    const std::string bytes = readFileBytes(path);
    if (bytesOut) *bytesOut = bytes;
    try
    {
        std::istringstream in(bytes);
        sed::assets::Model3doLoadOptions raw;
        raw.applyHierarchyTransforms = false;
        return sed::assets::loadFrom3DO(in, raw);
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot open 3DO file: ") + e.what());
    }
}

Json vectorJson(const sed::TVector& v)
{
    Json out = Json::makeArray();
    for (const double c : { v.x, v.y, v.z }) out.array.push_back(Json::makeNumber(c));
    return out;
}

// model-hierarchy: a 3DO's node tree and what's wrong with it.
Json cmdModelHierarchy(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "model-hierarchy requires <model.3do>");
    std::string bytes;
    const auto model = loadRawModel(args[0], &bytes);
    Json nodes = Json::makeArray();
    for (const auto& row : sed::assets::modelHierarchyRows(model))
    {
        const auto& node = model.nodes[static_cast<std::size_t>(row.node)];
        Json n = Json::makeObject();
        n.set("node", Json::makeNumber(static_cast<double>(row.node)));
        n.set("depth", Json::makeNumber(static_cast<double>(row.depth)));
        n.set("name", Json::makeString(node.name));
        n.set("parent", Json::makeNumber(static_cast<double>(node.parent)));
        n.set("mesh", Json::makeNumber(static_cast<double>(node.mesh)));
        if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < model.meshes.size())
            n.set("meshName", Json::makeString(model.meshes[static_cast<std::size_t>(node.mesh)].name));
        n.set("position", vectorJson(node.position));
        n.set("rotation", vectorJson(node.rotation));
        n.set("pivot", vectorJson(node.pivot));
        nodes.array.push_back(std::move(n));
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("model-hierarchy"));
    out.set("inputSha256", Json::makeString(sha256Hex(bytes)));
    out.set("meshes", Json::makeNumber(static_cast<double>(model.meshes.size())));
    out.set("nodes", std::move(nodes));
    out.set("issues", hierarchyIssuesJson(sed::assets::validateModelHierarchy(model)));
    return out;
}

// model-hierarchy-edit: rename, reparent, move, add and remove hierarchy nodes; writes a fresh 3DO.
Json cmdModelHierarchyEdit(const std::vector<std::string>& args)
{
    if (args.size() < 3) fail(kExitUsage, "model-hierarchy-edit requires <model.3do> <request.json|-> <new-output.3do>");
    std::string bytes;
    auto model = loadRawModel(args[0], &bytes);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "model-hierarchy-edit request must be a JSON object");
    checkSchemaVersion(request);
    if (lower(requiredString(request, "inputSha256", "model-hierarchy-edit request")) != sha256Hex(bytes))
        fail(kExitConflict, "model-hierarchy-edit inputSha256 does not match (stale file): " + args[0]);
    const Json* ops = request.find("operations");
    if (!ops || ops->type != Json::Type::Array || ops->array.empty())
        fail(kExitUsage, "model-hierarchy-edit requires 'operations'");
    const auto vec = [](const Json& op, const char* key) -> std::optional<sed::TVector> {
        const Json* j = op.find(key);
        if (!j) return std::nullopt;
        if (j->type != Json::Type::Array || j->array.size() != 3 || std::any_of(j->array.begin(), j->array.end(),
                [](const Json& c) { return c.type != Json::Type::Number; }))
            fail(kExitUsage, std::string("'") + key + "' must be [x, y, z]");
        return sed::TVector{ j->array[0].number, j->array[1].number, j->array[2].number };
    };
    for (const Json& op : ops->array)
    {
        if (!op.isObject()) fail(kExitUsage, "model-hierarchy-edit operations must be objects");
        const std::string name = requiredString(op, "op", "model-hierarchy-edit operation");
        const auto node = [&] { return static_cast<std::int32_t>(requiredInt(op, "node", "model-hierarchy-edit operation")); };
        std::optional<sed::assets::HierarchyIssue> problem;
        if (name == "rename") problem = sed::assets::renameModelNode(model, node(), requiredString(op, "name", "rename"));
        else if (name == "reparent")
            problem = sed::assets::reparentModelNode(model, node(), static_cast<std::int32_t>(requiredInt(op, "parent", "reparent")));
        else if (name == "setTransform")
            problem = sed::assets::setModelNodeTransform(model, node(), vec(op, "position"), vec(op, "rotation"), vec(op, "pivot"));
        else if (name == "addNode")
            problem = sed::assets::addModelNode(model, static_cast<std::int32_t>(requiredInt(op, "parent", "addNode")),
                op.find("mesh") ? static_cast<std::int32_t>(requiredInt(op, "mesh", "addNode")) : -1, requiredString(op, "name", "addNode"));
        else if (name == "removeNode") problem = sed::assets::removeModelNode(model, node());
        else fail(kExitUsage, "model-hierarchy-edit op must be rename, reparent, setTransform, addNode or removeNode");
        if (problem) failCoded(kExitRefusal, problem->code, problem->code + ": " + problem->message);
    }
    const auto issues = sed::assets::validateModelHierarchy(model);
    for (const auto& issue : issues)
    {
        if (issue.severity == "error") failCoded(kExitRefusal, issue.code, issue.code + ": " + issue.message);
    }
    std::string text;
    try
    {
        text = sed::assets::saveTo3DO(model);
    }
    catch (const std::exception& e)
    {
        failCoded(kExitRefusal, "modelNotWritable", std::string("modelNotWritable: ") + e.what());
    }
    const fs::path outputPath = args[2];
    checkNewOutputTarget(args[0], outputPath, true);
    writeNewFileAtomic(outputPath, text);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("model-hierarchy-edit"));
    out.set("nodes", Json::makeNumber(static_cast<double>(model.nodes.size())));
    out.set("issues", hierarchyIssuesJson(issues));
    out.set("outputSha256", Json::makeString(sha256Hex(text)));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

// key-from-model: a KEY with a rest-pose track for every hierarchy node.
Json cmdKeyFromModel(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "key-from-model requires <model.3do> <new-output.key> [frames] [fps]");
    const auto model = loadRawModel(args[0]);
    if (model.nodes.empty()) failCoded(kExitRefusal, "noHierarchy", "noHierarchy: the model has no hierarchy to animate");
    const auto number = [&](const std::size_t i, const double fallback, const double low, const double high) {
        if (args.size() <= i) return fallback;
        char* end = nullptr;
        const double v = std::strtod(args[i].c_str(), &end);
        if (!end || *end || !std::isfinite(v) || v < low || v > high)
            fail(kExitUsage, "key-from-model frames must be 1 to 100000 and fps 1 to 1000");
        return v;
    };
    const auto frames = static_cast<std::int32_t>(number(2, 30, 1, 100000));
    const double fps = number(3, 15, 1, 1000);
    const auto key = sed::assets::keyFromModel(model, frames, fps);
    const std::string text = sed::assets::saveToKEYText(key);
    const fs::path outputPath = args[1];
    checkNewOutputTarget(args[0], outputPath, true);
    writeNewFileAtomic(outputPath, text);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("key-from-model"));
    out.set("tracks", Json::makeNumber(static_cast<double>(key.nodes.size())));
    out.set("frames", Json::makeNumber(frames));
    out.set("fps", Json::makeNumber(fps));
    out.set("outputSha256", Json::makeString(sha256Hex(text)));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

// assistant-script: writes one of the scripts RED's assistants place (red_goals.cog, red_cutscene.cog).
Json cmdAssistantScript(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "assistant-script requires <red_goals.cog|red_cutscene.cog> <new-output.cog>");
    const std::string_view text = sed::level::assistantScriptText(args[0]);
    if (text.empty()) fail(kExitUsage, "assistant-script knows red_goals.cog and red_cutscene.cog");
    const fs::path outputPath = args[1];
    checkNewOutputTarget(fs::path{}, outputPath, false);
    writeNewFileAtomic(outputPath, text);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("assistant-script"));
    out.set("script", Json::makeString(lower(args[0])));
    out.set("outputSha256", Json::makeString(sha256Hex(std::string(text))));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

// assistant-check: runs a placed assistant COG in the COG runtime with its kind's scenario.
Json cmdAssistantCheck(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "assistant-check requires <request.json|->");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "assistant-check request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "level" && key != "kind" && key != "cog" && key != "script" && key != "target"
            && key != "strict")
            fail(kExitUsage, "Unknown field '" + key + "' in assistant-check request");
    }
    checkSchemaVersion(request);
    const auto kind = sed::level::parseAssistantKind(requiredString(request, "kind", "assistant-check request"));
    if (!kind) fail(kExitUsage, "assistant-check kind must be door, elevator, switch, objectives or cutscene");
    const auto target = sed::assets::parseCogPreflightTarget(
        request.find("target") ? requiredString(request, "target", "assistant-check request") : "jk");
    if (!target || *target == sed::assets::CogPreflightTarget::ijim) fail(kExitUsage, "assistant-check target must be jk or mots");
    const Json* levelJson = request.find("level");
    if (!levelJson) fail(kExitUsage, "assistant-check requires 'level' {path, sha256}");
    const std::string path = requiredString(*levelJson, "path", "assistant-check level");
    const OpenedDocument doc = openDocument(path);
    if (doc.inputSha256 != lower(requiredString(*levelJson, "sha256", "assistant-check level")))
        fail(kExitConflict, "assistant-check level sha256 does not match (stale file): " + path);
    const long long cog = requiredInt(request, "cog", "assistant-check request");
    if (cog < 0 || static_cast<std::size_t>(cog) >= doc.level.cogs.size()) fail(kExitInput, "assistant-check cog is out of range");
    std::string text;
    if (const Json* script = request.find("script")) text = readBoundFile(*script, "assistant-check script", 4u << 20).bytes;
    else text = std::string(sed::level::assistantScriptText(doc.level.cogs[static_cast<std::size_t>(cog)].name));
    if (text.empty()) fail(kExitUsage, "assistant-check needs 'script' {path, sha256} for a stock script");
    const auto check = sed::level::checkAssistant(doc.level, *kind, static_cast<std::int32_t>(cog), text, *target);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("assistant-check"));
    out.set("kind", Json::makeString(std::string(sed::level::assistantKindName(*kind))));
    out.set("passed", Json::makeBool(check.passed));
    Json expectations = Json::makeArray();
    for (const auto& line : check.expectations) expectations.array.push_back(Json::makeString(line));
    out.set("expectations", std::move(expectations));
    Json timeline = Json::makeArray();
    for (const auto& line : check.timeline) timeline.array.push_back(Json::makeString(line));
    out.set("timeline", std::move(timeline));
    Json unsimulated = Json::makeArray();
    for (const auto& line : check.unsimulated) unsimulated.array.push_back(Json::makeString(line));
    out.set("unsimulated", std::move(unsimulated));
    Json breakpoints = Json::makeArray();
    for (const auto& event : check.breakpoints)
    {
        Json item = Json::makeObject();
        item.set("time", Json::makeNumber(event.time));
        item.set("message", Json::makeString(event.message));
        item.set("cog", Json::makeNumber(event.cog));
        item.set("senderType", Json::makeNumber(event.senderType));
        item.set("senderRef", Json::makeNumber(event.senderRef));
        item.set("sourceType", Json::makeNumber(event.sourceType));
        item.set("sourceRef", Json::makeNumber(event.sourceRef));
        Json watches = Json::makeObject();
        for (const auto& [name, value] : event.watches) watches.set(name, Json::makeString(value));
        item.set("watches", std::move(watches));
        Json timers = Json::makeArray();
        for (const auto& [name, due] : event.due)
        {
            Json timer = Json::makeObject();
            timer.set("name", Json::makeString(name));
            timer.set("due", Json::makeNumber(due));
            timers.array.push_back(std::move(timer));
        }
        item.set("timers", std::move(timers));
        breakpoints.array.push_back(std::move(item));
    }
    out.set("breakpoints", std::move(breakpoints));
    if (!check.passed)
    {
        if (const Json* strict = request.find("strict"); strict && strict->type == Json::Type::Bool && strict->boolean)
            failCoded(kExitRefusal, "assistantCheckFailed", "assistantCheckFailed: " + toJsonString(out));
    }
    return out;
}

// cog-run: compiles scripts and runs them against RED's stand-in world.
Json cmdCogRun(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "cog-run requires <request.json|->");
    const Json request = readRequest(args[0]);
    if (!request.isObject()) fail(kExitUsage, "cog-run request must be a JSON object");
    for (const auto& [key, value] : request.object)
    {
        (void)value;
        if (key != "schemaVersion" && key != "target" && key != "level" && key != "cogs" && key != "events"
            && key != "duration" && key != "step" && key != "instructionBudget")
            fail(kExitUsage, "Unknown field '" + key + "' in cog-run request");
    }
    checkSchemaVersion(request);
    const std::string targetName = request.find("target") ? requiredString(request, "target", "cog-run request") : "jk";
    const auto target = sed::assets::parseCogPreflightTarget(targetName);
    if (!target || *target == sed::assets::CogPreflightTarget::ijim) fail(kExitUsage, "cog-run target must be jk or mots");
    const auto number = [&](const char* key, const double fallback, const double low, const double high) {
        const Json* j = request.find(key);
        if (!j) return fallback;
        if (j->type != Json::Type::Number || !std::isfinite(j->number) || j->number < low || j->number > high)
            fail(kExitUsage, std::string("cog-run ") + key + " must be a number from " + std::to_string(low) + " to "
                + std::to_string(high));
        return j->number;
    };
    const double duration = number("duration", 10.0, 0.0, 3600.0);
    const double step = number("step", 0.01, 0.001, 1.0);
    sed::assets::CogRuntimeOptions options;
    options.instructionBudget = static_cast<std::size_t>(number("instructionBudget", 200000.0, 100.0, 1.0e8));

    std::optional<OpenedDocument> doc;
    if (const Json* level = request.find("level"))
    {
        const std::string path = requiredString(*level, "path", "cog-run level");
        const std::string expected = lower(requiredString(*level, "sha256", "cog-run level"));
        doc = openDocument(path);
        if (doc->inputSha256 != expected) fail(kExitConflict, "cog-run level sha256 does not match (stale file): " + path);
    }
    const sed::assets::CogWorld initial = doc ? sed::assets::cogWorldFromLevel(doc->level) : sed::assets::CogWorld{};
    sed::assets::CogRuntime runtime(initial, *target, options);

    const Json* cogs = request.find("cogs");
    if (!cogs || cogs->type != Json::Type::Array || cogs->array.empty())
        fail(kExitUsage, "cog-run requires 'cogs': [{script {path, sha256}, placedCog?, bindings?}]");
    Json cogRows = Json::makeArray();
    for (const Json& entry : cogs->array)
    {
        if (!entry.isObject()) fail(kExitUsage, "cog-run cogs entries must be objects");
        for (const auto& [key, value] : entry.object)
        {
            (void)value;
            if (key != "script" && key != "placedCog" && key != "bindings")
                fail(kExitUsage, "Unknown field '" + key + "' in cog-run cogs entry");
        }
        const Json* scriptJson = entry.find("script");
        if (!scriptJson) fail(kExitUsage, "cog-run cogs entry requires 'script' {path, sha256}");
        const BoundFile script = readBoundFile(*scriptJson, "cog-run script", 4u << 20);
        const std::string name = script.path.filename().string();
        const sed::assets::CogCompileResult compiled = sed::assets::compileCog(name, script.bytes, *target);
        std::vector<sed::assets::CogRuntimeBinding> bindings;
        if (const Json* placed = entry.find("placedCog"))
        {
            if (!doc) fail(kExitUsage, "cog-run placedCog needs a 'level'");
            if (placed->type != Json::Type::Number || placed->number < 0
                || placed->number >= static_cast<double>(doc->level.cogs.size()))
                fail(kExitInput, "cog-run placedCog is out of range for this level");
            const sed::assets::CogSchema schema = sed::assets::parseCogSchema(name, script.bytes);
            const auto description = sed::level::describeBindings(doc->level, static_cast<std::int32_t>(placed->number), &schema);
            std::vector<std::int32_t> surfaceBase(doc->level.sectors.size() + 1, 0);
            for (std::size_t s = 0; s < doc->level.sectors.size(); ++s)
                surfaceBase[s + 1] = surfaceBase[s] + static_cast<std::int32_t>(doc->level.sectors[s].surfaces.size());
            for (const auto& row : description.rows)
            {
                if (!row.symbolName) continue;
                sed::assets::CogRuntimeBinding b;
                b.symbol = lower(*row.symbolName);
                if (row.target.sectorIndex && row.target.surfaceIndex && *row.target.sectorIndex >= 0
                    && static_cast<std::size_t>(*row.target.sectorIndex) < doc->level.sectors.size())
                    b.value = sed::assets::CogRuntimeValue::ofInt(surfaceBase[static_cast<std::size_t>(*row.target.sectorIndex)] + *row.target.surfaceIndex);
                else if (row.target.index)
                    b.value = sed::assets::CogRuntimeValue::ofInt(*row.target.index);
                else if (row.rawValue)
                {
                    b.value.type = sed::assets::CogRuntimeValue::Type::String;
                    b.value.text = *row.rawValue;
                }
                else
                    continue;
                bindings.push_back(std::move(b));
            }
        }
        if (const Json* explicitBindings = entry.find("bindings"))
        {
            if (!explicitBindings->isObject()) fail(kExitUsage, "cog-run bindings must be an object {symbol: value}");
            for (const auto& [symbol, value] : explicitBindings->object)
            {
                sed::assets::CogRuntimeBinding b;
                b.symbol = lower(symbol);
                if (value.type == Json::Type::Number) b.value = sed::assets::CogRuntimeValue::ofInt(static_cast<std::int32_t>(value.number));
                else if (value.type == Json::Type::String)
                {
                    b.value.type = sed::assets::CogRuntimeValue::Type::String;
                    b.value.text = lower(value.str);
                }
                else fail(kExitUsage, "cog-run binding '" + symbol + "' must be a number or a resource name");
                std::erase_if(bindings, [&](const auto& existing) { return existing.symbol == b.symbol; });
                bindings.push_back(std::move(b));
            }
        }
        std::string error;
        const std::int32_t index = runtime.addCog(compiled, name, bindings, error);
        Json row = Json::makeObject();
        row.set("script", Json::makeString(name));
        row.set("cog", Json::makeNumber(static_cast<double>(index)));
        row.set("loads", Json::makeBool(compiled.loads));
        row.set("errors", Json::makeNumber(static_cast<double>(compiled.count(sed::assets::CogDiagnosticSeverity::error))));
        if (index < 0) row.set("error", Json::makeString(error));
        cogRows.array.push_back(std::move(row));
    }

    struct Scheduled
    {
        double at;
        const Json* event;
    };
    std::vector<Scheduled> schedule;
    if (const Json* events = request.find("events"))
    {
        if (events->type != Json::Type::Array) fail(kExitUsage, "cog-run events must be an array");
        for (const Json& e : events->array)
        {
            if (!e.isObject()) fail(kExitUsage, "cog-run events entries must be objects");
            int actions = 0;
            for (const auto& [key, value] : e.object)
            {
                (void)value;
                if (key == "activate" || key == "enter" || key == "cross" || key == "damage" || key == "message") ++actions;
                else if (key != "at" && key != "cog" && key != "params")
                    fail(kExitUsage, "Unknown field '" + key + "' in cog-run event");
            }
            if (actions != 1) fail(kExitUsage, "cog-run event needs exactly one of activate, enter, cross, damage or message");
            const Json* at = e.find("at");
            if (!at || at->type != Json::Type::Number || at->number < 0) fail(kExitUsage, "cog-run event needs 'at' (seconds)");
            schedule.push_back(Scheduled{ at->number, &e });
        }
    }
    std::stable_sort(schedule.begin(), schedule.end(), [](const Scheduled& a, const Scheduled& b) { return a.at < b.at; });
    const auto objectRef = [](const Json& j, std::int32_t& kind) -> std::int32_t {
        for (const auto& [name, code] : { std::pair{ "thing", 3 }, std::pair{ "sector", 5 }, std::pair{ "surface", 6 } })
        {
            if (const Json* v = j.find(name); v && v->type == Json::Type::Number)
            {
                kind = code;
                return static_cast<std::int32_t>(v->number);
            }
        }
        fail(kExitUsage, "cog-run event object needs thing, sector or surface");
    };
    const std::int32_t player = runtime.world().player;
    runtime.startup();
    for (const Scheduled& s : schedule)
    {
        if (s.at > runtime.time()) runtime.advance(s.at - runtime.time(), step);
        const Json& e = *s.event;
        std::int32_t kind = 0;
        if (const Json* a = e.find("activate"))
        {
            const std::int32_t index = objectRef(*a, kind);
            runtime.sendLinked("activated", kind, index, 3, player);
        }
        else if (const Json* en = e.find("enter"))
        {
            const std::int32_t index = objectRef(*en, kind);
            runtime.sendLinked("entered", kind, index, 3, player);
        }
        else if (const Json* cr = e.find("cross"))
        {
            const std::int32_t index = objectRef(*cr, kind);
            if (kind != 6) fail(kExitUsage, "cog-run cross needs a surface");
            runtime.sendLinked("crossed", kind, index, 3, player);
        }
        else if (const Json* d = e.find("damage"))
        {
            const std::int32_t index = objectRef(*d, kind);
            if (kind != 3) fail(kExitUsage, "cog-run damage needs a thing");
            const Json* amount = d->find("amount");
            if (!amount || amount->type != Json::Type::Number) fail(kExitUsage, "cog-run damage needs 'amount'");
            const Json* type = d->find("type");
            runtime.damageThing(index, static_cast<float>(amount->number),
                type && type->type == Json::Type::Number ? static_cast<std::int32_t>(type->number) : 1, player);
        }
        else if (const Json* m = e.find("message"))
        {
            if (m->type != Json::Type::String) fail(kExitUsage, "cog-run event 'message' must be a message name");
            const long long cog = requiredInt(e, "cog", "cog-run message event");
            std::array<float, 4> params{};
            if (const Json* p = e.find("params"); p && p->type == Json::Type::Array)
            {
                for (std::size_t k = 0; k < p->array.size() && k < 4; ++k) params[k] = static_cast<float>(p->array[k].number);
            }
            runtime.sendMessage(static_cast<std::int32_t>(cog), lower(m->str), 0, -1, 3, player, params);
        }
        else fail(kExitUsage, "cog-run event needs activate, enter, cross, damage or message");
    }
    if (duration > runtime.time()) runtime.advance(duration - runtime.time(), step);

    Json events = Json::makeArray();
    for (const auto& ev : runtime.events())
    {
        Json row = Json::makeObject();
        row.set("time", Json::makeNumber(std::round(ev.time * 1000.0) / 1000.0));
        row.set("cog", Json::makeNumber(static_cast<double>(ev.cog)));
        row.set("kind", Json::makeString(ev.kind));
        row.set("text", Json::makeString(ev.text));
        events.array.push_back(std::move(row));
    }
    const auto& world = runtime.world();
    const auto rounded = [](const double v) { return Json::makeNumber(std::round(v * 10000.0) / 10000.0); };
    Json things = Json::makeArray();
    for (std::size_t i = 0; i < world.things.size(); ++i)
    {
        const auto& t = world.things[i];
        const bool changed = i >= initial.things.size() || t.position != initial.things[i].position
            || t.health != initial.things[i].health || t.destroyed != initial.things[i].destroyed
            || t.flags != initial.things[i].flags || t.light != initial.things[i].light;
        if (t.frames.empty() && !changed) continue;
        Json row = Json::makeObject();
        row.set("thing", Json::makeNumber(static_cast<double>(i)));
        row.set("name", Json::makeString(t.name));
        Json position = Json::makeArray();
        for (const double c : t.position) position.array.push_back(rounded(c));
        row.set("position", std::move(position));
        row.set("frame", Json::makeNumber(static_cast<double>(t.frame)));
        row.set("moving", Json::makeBool(t.moving));
        row.set("health", Json::makeNumber(t.health));
        row.set("destroyed", Json::makeBool(t.destroyed));
        row.set("flags", Json::makeNumber(static_cast<double>(t.flags)));
        things.array.push_back(std::move(row));
    }
    Json surfaces = Json::makeArray();
    for (std::size_t i = 0; i < world.surfaces.size() && i < initial.surfaces.size(); ++i)
    {
        const auto& s = world.surfaces[i];
        const auto& was = initial.surfaces[i];
        if (s.cel == was.cel && s.flags == was.flags && s.adjoinFlags == was.adjoinFlags && s.light == was.light) continue;
        Json row = Json::makeObject();
        row.set("surface", Json::makeNumber(static_cast<double>(i)));
        row.set("sector", Json::makeNumber(static_cast<double>(s.sector)));
        row.set("sectorSurface", Json::makeNumber(static_cast<double>(s.surface)));
        row.set("cel", Json::makeNumber(static_cast<double>(s.cel)));
        row.set("flags", Json::makeNumber(static_cast<double>(s.flags)));
        row.set("adjoinFlags", Json::makeNumber(static_cast<double>(s.adjoinFlags)));
        row.set("light", rounded(s.light));
        surfaces.array.push_back(std::move(row));
    }
    Json sectors = Json::makeArray();
    for (std::size_t i = 0; i < world.sectors.size() && i < initial.sectors.size(); ++i)
    {
        const auto& s = world.sectors[i];
        const auto& was = initial.sectors[i];
        if (s.light == was.light && s.flags == was.flags && s.adjoinsShown == was.adjoinsShown && s.colormap == was.colormap
            && s.tint == was.tint)
            continue;
        Json row = Json::makeObject();
        row.set("sector", Json::makeNumber(static_cast<double>(i)));
        row.set("light", rounded(s.light));
        row.set("flags", Json::makeNumber(static_cast<double>(s.flags)));
        row.set("adjoinsShown", Json::makeBool(s.adjoinsShown));
        Json tint = Json::makeArray();
        for (const double c : s.tint) tint.array.push_back(rounded(c));
        row.set("tint", std::move(tint));
        sectors.array.push_back(std::move(row));
    }
    Json inventory = Json::makeObject();
    for (const auto& [bin, amount] : world.inventory) inventory.set(std::to_string(bin), rounded(amount));
    Json goals = Json::makeObject();
    for (const auto& [goal, flags] : world.goals) goals.set(std::to_string(goal), Json::makeNumber(static_cast<double>(flags)));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("cog-run"));
    out.set("target", Json::makeString(std::string(sed::assets::cogPreflightTargetName(*target))));
    out.set("time", Json::makeNumber(std::round(runtime.time() * 1000.0) / 1000.0));
    out.set("cogs", std::move(cogRows));
    out.set("events", std::move(events));
    out.set("things", std::move(things));
    out.set("surfaces", std::move(surfaces));
    out.set("sectors", std::move(sectors));
    out.set("inventory", std::move(inventory));
    out.set("goals", std::move(goals));
    out.set("levelEnded", Json::makeBool(world.levelEnded));
    return out;
}

Json cmdDxfImport(const std::vector<std::string>& args)
{
    if (args.size() < 3)
        fail(kExitUsage, "dxf-import requires <input.dxf> <height> <new-out.jkl>");
    double height = 0.0;
    bool heightOk = true;
    try
    {
        std::size_t consumed = 0;
        height = std::stod(args[1], &consumed);
        heightOk = consumed == args[1].size();
    }
    catch (const std::exception&)
    {
        heightOk = false;
    }
    if (!heightOk) fail(kExitUsage, "dxf-import height must be a number");
    std::vector<sed::assets::DxfFace> faces{};
    try
    {
        faces = sed::assets::loadDxfFaces(fs::path(args[0]));
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot read DXF: ") + e.what());
    }
    // E-13: horizontal DXF footprints become extruded sectors in a new level.
    sed::Level level{};
    level.header.version = 1; // JKDF2 new-project header, as in the editor.
    // The prism builder authors pixel-space UVs (ppunit scaling), so the
    // fresh level carries the legacy pixel UV space JKL writes verbatim.
    level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
    const auto result = sed::level::levelImportDxfFaces(level, faces, height);
    if (!result)
        failCoded(kExitRefusal, "invalidDxfImportHeight",
            "invalidDxfImportHeight: height must be finite, positive and at most 1000");
    if (result->sectors.empty())
        failCoded(kExitRefusal, "dxfImportNoSectors",
            "dxfImportNoSectors: no horizontal DXF face could be compiled into a sector");
    std::string output;
    try
    {
        const auto built = sed::assets::buildJklExport(level);
        std::istringstream in(built.text);
        (void)sed::assets::loadFromJKL(in);
        output = built.text;
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot serialize imported level: ") + e.what());
    }
    const fs::path outputPath = args[2];
    checkNewOutputTarget(fs::path(args[0]), outputPath, true);
    writeNewFileAtomic(outputPath, output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("dxf-import"));
    out.set("sectors", Json::makeNumber(static_cast<double>(result->sectors.size())));
    out.set("skippedFaces", Json::makeNumber(static_cast<double>(result->skippedFaces)));
    out.set("outputSha256", Json::makeString(sha256Hex(output)));
    out.set("output", Json::makeString(outputPath.string()));
    return out;
}

Json cmdDxfInspect(const std::vector<std::string>& args)
{
    if (args.empty())
        fail(kExitUsage, "dxf-inspect requires a .dxf path");
    std::vector<sed::assets::DxfFace> faces{};
    try
    {
        faces = sed::assets::loadDxfFaces(fs::path(args[0]));
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot read DXF: ") + e.what());
    }
    std::size_t triangles = 0;
    std::size_t quads = 0;
    std::vector<std::string> layers{};
    for (const sed::assets::DxfFace& face : faces)
    {
        if (face.cornerCount == 4) ++quads; else ++triangles;
        if (std::find(layers.begin(), layers.end(), face.layer) == layers.end())
            layers.push_back(face.layer);
    }
    std::sort(layers.begin(), layers.end());
    Json layerJson = Json::makeArray();
    for (const std::string& layer : layers) layerJson.array.push_back(Json::makeString(layer));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("dxf-inspect"));
    out.set("faces", Json::makeNumber(static_cast<double>(faces.size())));
    out.set("triangles", Json::makeNumber(static_cast<double>(triangles)));
    out.set("quads", Json::makeNumber(static_cast<double>(quads)));
    out.set("layers", std::move(layerJson));
    return out;
}

sed::TVector keyVectorField(const Json& obj, const char* key, bool required)
{
    const Json* v = obj.find(key);
    if (!v)
    {
        if (required) fail(kExitUsage, std::string("KEY entry requires '") + key + "' as [x, y, z]");
        return sed::TVector{};
    }
    if (v->type != Json::Type::Array || v->array.size() != 3)
        fail(kExitUsage, std::string("KEY '") + key + "' must be an [x, y, z] array");
    for (const Json& component : v->array)
        if (component.type != Json::Type::Number)
            fail(kExitUsage, std::string("KEY '") + key + "' components must be numbers");
    return sed::TVector{ v->array[0].number, v->array[1].number, v->array[2].number };
}

void applyKeyOperations(sed::assets::KeyFile& key, const Json& request)
{
    const Json* ops = request.find("operations");
    if (!ops || ops->type != Json::Type::Array || ops->array.empty())
        fail(kExitUsage, "KEY request requires a non-empty 'operations' array");
    for (std::size_t i = 0; i < ops->array.size(); ++i)
    {
        const Json& op = ops->array[i];
        if (!op.isObject()) fail(kExitUsage, "KEY operation must be an object");
        const Json* name = op.find("op");
        if (!name || name->type != Json::Type::String)
            fail(kExitUsage, "KEY operation requires string 'op'");
        const int idx = static_cast<int>(i);
        if (name->str == "computeDeltas")
        {
            // Every entry's deltas and flags from the next entry (Blender-Sith's rule; retail angles turn the short way).
            (void)sed::assets::keyComputeDeltas(key);
            continue;
        }
        const long long nodeValue = intField(op, "node", idx);
        if (nodeValue < 0 || static_cast<std::size_t>(nodeValue) >= key.nodes.size())
            failCoded(kExitRefusal, "keyNodeOutOfRange",
                "keyNodeOutOfRange: KEY node index is outside the file");
        const std::size_t node = static_cast<std::size_t>(nodeValue);
        if (name->str == "insertEntry")
        {
            const Json* entryJson = op.find("entry");
            if (!entryJson || !entryJson->isObject())
                fail(kExitUsage, "insertEntry requires an 'entry' object");
            sed::assets::KeyEntry entry{};
            if (const Json* v = entryJson->find("label"))
            {
                if (v->type != Json::Type::String) fail(kExitUsage, "KEY entry 'label' must be a string");
                entry.label = v->str;
            }
            const Json* frame = entryJson->find("frame");
            if (!frame || frame->type != Json::Type::Number || !std::isfinite(frame->number))
                fail(kExitUsage, "KEY entry 'frame' must be a finite number");
            entry.frame = frame->number;
            if (const Json* v = entryJson->find("flags"))
            {
                if (v->type != Json::Type::Number || !std::isfinite(v->number)
                    || v->number != std::floor(v->number) || v->number < 0
                    || v->number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
                    fail(kExitUsage, "KEY entry 'flags' must be an unsigned 32-bit integer");
                entry.flags = static_cast<std::uint32_t>(v->number);
            }
            entry.position = keyVectorField(*entryJson, "position", true);
            entry.rotation = keyVectorField(*entryJson, "rotation", true);
            entry.dPosition = keyVectorField(*entryJson, "dPosition", false);
            entry.dRotation = keyVectorField(*entryJson, "dRotation", false);
            const auto finite = [](const sed::TVector& value) {
                return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
            };
            if (!finite(entry.position) || !finite(entry.rotation)
                || !finite(entry.dPosition) || !finite(entry.dRotation))
                failCoded(kExitRefusal, "invalidKeyEntry",
                    "invalidKeyEntry: KEY entry components must be finite");
            if (entry.frame < 0 || (key.frames > 0 && entry.frame > key.frames))
                failCoded(kExitRefusal, "keyFrameOutOfRange",
                    "keyFrameOutOfRange: KEY entry frame is outside the declared frame count");
            if (!sed::assets::keyInsertEntry(key, node, entry))
                failCoded(kExitRefusal, "keyFrameOutOfRange",
                    "keyFrameOutOfRange: KEY entry could not be inserted");
        }
        else if (name->str == "eraseEntry")
        {
            const long long entryValue = intField(op, "entry", idx);
            if (entryValue < 0
                || static_cast<std::size_t>(entryValue) >= key.nodes[node].entries.size())
                failCoded(kExitRefusal, "keyEntryOutOfRange",
                    "keyEntryOutOfRange: KEY entry index is outside the node");
            if (!sed::assets::keyEraseEntry(key, node, static_cast<std::size_t>(entryValue)))
                failCoded(kExitRefusal, "keyEntryOutOfRange",
                    "keyEntryOutOfRange: KEY entry could not be erased");
        }
        else if (name->str == "sortNode")
        {
            sed::assets::keySortNodeEntries(key, node);
        }
        else
        {
            fail(kExitUsage, "unsupported KEY operation '" + name->str + "'");
        }
    }
}

Json cmdKeyInspect(const std::vector<std::string>& args)
{
    if (args.empty())
        fail(kExitUsage, "key-inspect requires <file.key> [file.3do]");
    const fs::path inputPath = args[0];
    const std::string inputBytes = readFileBytes(inputPath);
    sed::assets::KeyFile key{};
    try
    {
        std::istringstream input(inputBytes);
        key = sed::assets::loadFromKEY(input);
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot open KEY file: ") + e.what());
    }
    // E-12: headless KEY timeline inventory; with a 3DO, also checks the
    // KEY node mesh names against the model hierarchy/mesh names. This
    // reports stored data only; it does not sample or interpolate.
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("key-inspect"));
    out.set("frames", Json::makeNumber(static_cast<double>(key.frames)));
    out.set("fps", Json::makeNumber(static_cast<double>(key.fps)));
    out.set("joints", Json::makeNumber(static_cast<double>(key.joints)));
    out.set("type", Json::makeNumber(static_cast<double>(key.type)));
    out.set("markers", Json::makeNumber(static_cast<double>(key.markers.size())));
    out.set("serializationSafe", Json::makeBool(key.serializationSafe));
    Json nodesJson = Json::makeArray();
    std::size_t entries = 0;
    for (const sed::assets::KeyNode& node : key.nodes)
    {
        entries += node.entries.size();
        Json entry = Json::makeObject();
        entry.set("meshName", Json::makeString(node.meshName));
        entry.set("nodeIndex", Json::makeNumber(static_cast<double>(node.nodeIndex)));
        entry.set("entries", Json::makeNumber(static_cast<double>(node.entries.size())));
        if (!node.entries.empty())
        {
            double first = node.entries.front().frame;
            double last = node.entries.front().frame;
            for (const sed::assets::KeyEntry& e : node.entries)
            {
                first = std::min(first, e.frame);
                last = std::max(last, e.frame);
            }
            entry.set("firstFrame", Json::makeNumber(static_cast<double>(first)));
            entry.set("lastFrame", Json::makeNumber(static_cast<double>(last)));
        }
        nodesJson.array.push_back(std::move(entry));
    }
    out.set("nodes", std::move(nodesJson));
    out.set("nodeCount", Json::makeNumber(static_cast<double>(key.nodes.size())));
    out.set("entries", Json::makeNumber(static_cast<double>(entries)));
    if (args.size() > 1)
    {
        sed::assets::Model3do model{};
        try
        {
            model = sed::assets::loadFrom3DO(fs::path(args[1]));
        }
        catch (const std::exception& e)
        {
            fail(kExitInput, std::string("cannot open 3DO file: ") + e.what());
        }
        std::vector<std::string> modelNames{};
        for (const sed::assets::Model3doNode& node : model.nodes)
            modelNames.push_back(lower(node.name));
        for (const sed::assets::Model3doMesh& mesh : model.meshes)
            modelNames.push_back(lower(mesh.name));
        Json unmatched = Json::makeArray();
        for (const sed::assets::KeyNode& node : key.nodes)
        {
            const std::string wanted = lower(node.meshName);
            if (std::find(modelNames.begin(), modelNames.end(), wanted) == modelNames.end())
                unmatched.array.push_back(Json::makeString(node.meshName));
        }
        out.set("modelNodes", Json::makeNumber(static_cast<double>(model.nodes.size())));
        out.set("modelMeshes", Json::makeNumber(static_cast<double>(model.meshes.size())));
        out.set("unmatchedKeyNodes", std::move(unmatched));
        sed::assets::Model3doLoadOptions raw;
        raw.applyHierarchyTransforms = false;
        out.set("modelIssues", hierarchyIssuesJson(sed::assets::validateKeyForModel(key, sed::assets::loadFrom3DO(fs::path(args[1]), raw))));
    }
    return out;
}

Json keyPlanOrApply(const std::vector<std::string>& args, bool isApply)
{
    const char* commandName = isApply ? "key-apply" : "key-plan";
    if (args.size() < (isApply ? 4u : 2u))
        fail(kExitUsage, std::string(commandName)
            + (isApply ? " requires <in.key> <request.json|-> <expected-output-sha256> <new-out.key>"
                       : " requires <in.key> <request.json|->"));
    const fs::path inputPath = args[0];
    const std::string inputBytes = readFileBytes(inputPath);
    const std::string inputSha256 = sha256Hex(inputBytes);
    sed::assets::KeyFile key{};
    try
    {
        std::istringstream input(inputBytes);
        key = sed::assets::loadFromKEY(input);
    }
    catch (const std::exception& e)
    {
        fail(kExitInput, std::string("cannot open KEY file: ") + e.what());
    }
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "KEY request must be a JSON object");
    checkSchemaVersion(request);
    if (!key.serializationSafe)
        failCoded(kExitRefusal, "keySerializationUnsupported",
            "KEY contains malformed or unsupported data; editing would discard it");
    if (const Json* expected = request.find("inputSha256"))
    {
        if (expected->type != Json::Type::String) fail(kExitUsage, "inputSha256 must be a string");
        if (lower(expected->str) != inputSha256)
            fail(kExitConflict, "inputSha256 does not match the current KEY input");
    }
    applyKeyOperations(key, request);
    std::string output;
    try { output = sed::assets::saveToKEYText(key); }
    catch (const std::exception& error)
    {
        failCoded(kExitRefusal, "keySerializationUnsupported", error.what());
    }
    const std::string outputSha256 = sha256Hex(output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(commandName));
    out.set("inputSha256", Json::makeString(inputSha256));
    out.set("outputSha256", Json::makeString(outputSha256));
    std::size_t entries = 0;
    for (const sed::assets::KeyNode& node : key.nodes) entries += node.entries.size();
    out.set("nodes", Json::makeNumber(static_cast<double>(key.nodes.size())));
    out.set("entries", Json::makeNumber(static_cast<double>(entries)));
    if (!isApply) return out; // key-plan writes NOTHING.
    if (lower(args[2]) != outputSha256)
        fail(kExitConflict, "expected output sha256 does not match the recomputed plan; re-run key-plan");
    const fs::path outputPath = args[3];
    checkNewOutputTarget(inputPath, outputPath, true);
    if (sha256Hex(readFileBytes(inputPath)) != inputSha256)
        fail(kExitConflict, "KEY source changed during preparation; re-run key-plan");
    writeNewFileAtomic(outputPath, output);
    out.set("output", Json::makeString(outputPath.string()));
    out.set("written", Json::makeBool(true));
    return out;
}

Json cmdSchema(const std::vector<std::string>& args)
{
    if ( args.size() > 1 )
    {
        failCoded(kExitUsage, "schemaTopicInvalid", "schema accepts at most one topic");
    }
    if ( !args.empty() )
    {
        return focusedSchema(args[0]);
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("protocol", Json::makeString("sed-agent"));
    out.set("schemaVersion", Json::makeNumber(1));
    out.set("note", Json::makeString(
        "This is a tool-neutral CLI and does not claim to implement MCP."));
    Json cmds = Json::makeArray();
    for ( const auto& command : agentCommandDescriptors() )
    {
        cmds.array.push_back(Json::makeString(std::string(command.name)));
    }
    out.set("commandCount", Json::makeNumber(static_cast<double>(cmds.array.size())));
    out.set("commands", std::move(cmds));
    out.set("joJaRequestSchema", joJaRequestSchema());
    out.set("modelNativeRequestSchema", modelNativeRequestSchema());
    out.set("modelNativeInspectQuerySchema", modelNativeInspectQuerySchema());
    out.set("modelLightingRequestSchema", modelLightingRequestSchema());
    out.set("dependencyRequestSchema", dependencyRequestSchema());
    out.set("semanticPreview", semanticPreviewCapabilities());
    Json importNormals = Json::makeObject();
    importNormals.set("normalPolicy", Json::makeString("preserve (default) | recalculate"));
    importNormals.set("normalSplitVertices", Json::makeString("extra geometric vertices required by normal/hard-edge boundaries before mesh partitioning"));
    out.set("modelImportNormals", std::move(importNormals));
    Json exits = Json::makeObject();
    exits.set("0", Json::makeString("ok"));
    exits.set("2", Json::makeString("usage / invalid request"));
    exits.set("3", Json::makeString("input error"));
    exits.set("4", Json::makeString("stale input or conflict"));
    exits.set("5", Json::makeString("refusal (existing output or unsafe target)"));
    out.set("exitCodes", std::move(exits));
    out.set("maxRequestBytes", Json::makeNumber(static_cast<double>(kMaxRequestBytes)));
    out.set("maxSourceBytes", Json::makeNumber(static_cast<double>(detail::kMaxSourceBytes)));
    out.set("maxArchiveBytes", Json::makeNumber(static_cast<double>(kMaxArchiveBytes)));
    Json ops = Json::makeArray();
    for ( const auto& operation : agentOperationDescriptors() )
    {
        ops.array.push_back(Json::makeString(std::string(operation.legacyDescriptor)));
    }
    out.set("operationCount", Json::makeNumber(static_cast<double>(ops.array.size())));
    out.set("focusedSchema", Json::makeString("schema commands | schema patch | schema dependencies | schema package-plan | schema package-apply | schema jo-ja-inspect | schema jo-ja-plan | schema jo-ja-apply | schema mat-create | schema cmp-create | schema model-import | schema model-native | schema model-native-inspect | schema archive-create | schema snapshot | schema overview | schema <operation>"));
    out.set("operations", std::move(ops));
    Json patchFields = Json::makeArray();
    for (const char* f : { "schemaVersion", "inputSha256", "sourceRevision", "operations",
                           "resourceArchives[{path,sha256}] (COG schemas, makeDoor script, export dependency scan)",
                           "templateSource{path,sha256} (importTemplates; master template file for the export scan)",
                           "resolveExportDependencies (default true when resourceArchives are given; false keeps document-only capacities)",
                           "outputFormat (\"jkl\" default, or \"jed\": the edited JK/MotS level written as JED; apply output then ends in .jed)" })
        patchFields.array.push_back(Json::makeString(f));
    out.set("patchFields", std::move(patchFields));
    out.set("snapshotNote", Json::makeString(
        "snapshot and overview default to world surfaces (software-v1); optional thingMode=static adds bounded LOD0/rest-pose models and classic cel0 sprites. Animation, dynamic lighting and runtime state remain out of scope"));
    return out;
}

std::string captureInputFile(const fs::path& path, const std::size_t maxBytes)
{
    return activeSourceReads ? activeSourceReads->capture(path, maxBytes) : detail::readBoundedFile(path, maxBytes);
}

} // namespace

std::string readFileBytes(const fs::path& path, const std::size_t maxBytes)
{
    try
    {
        return captureInputFile(path, maxBytes);
    }
    catch (const detail::SourceRevisionError& error) { fail(kExitConflict, error.what()); }
    catch (const detail::InputReadError& error)
    {
        failCoded(kExitInput, error.kind == detail::InputReadError::Kind::LimitExceeded
            ? "inputTooLarge" : "inputReadFailed", error.what());
    }
}

std::size_t nativeReceiptLimit()
{
    return activeCommandOptions ? std::min(kMaxRequestBytes, activeCommandOptions->nativeReceiptLimit) : kMaxRequestBytes;
}

void writeNewFileAtomic(const fs::path& path, std::string_view bytes)
{
    try
    {
        detail::createNewOutputFile(path, bytes, [] {
            if (activeResourceChecks)
                for (const auto& check : *activeResourceChecks) check();
            if (activeSourceReads) activeSourceReads->requireUnchanged();
        });
    }
    catch (const detail::ResourceRevisionError& error) { fail(kExitConflict, error.what()); }
    catch (const detail::SourceRevisionError& error) { fail(kExitConflict, error.what()); }
    catch (const std::exception& error)
    {
        fail(kExitRefusal, "cannot publish new output file: " + std::string(error.what()));
    }
}

Json readRequest(const std::string& path)
{
    return readRequestFile(fs::path(path));
}

Json readRequestFile(const fs::path& path)
{
    std::string bytes;
    try
    {
        bytes = path == "-" ? detail::readBoundedStream(std::cin, kMaxRequestBytes)
            : captureInputFile(path, kMaxRequestBytes);
    }
    catch ( const detail::SourceRevisionError& error )
    {
        fail(kExitConflict, error.what());
    }
    catch ( const detail::InputReadError& error )
    {
        if ( error.kind == detail::InputReadError::Kind::LimitExceeded )
        {
            fail(kExitUsage, "request exceeds the 1 MiB cap");
        }
        fail(kExitInput, error.what());
    }
    if ( bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 )
    {
        bytes.erase(0, 3);
    }
    try
    {
        return parseJson(bytes);
    }
    catch ( const std::exception& error )
    {
        fail(kExitUsage, std::string("malformed request JSON: ") + error.what());
    }
}

Json runCommand(const std::string& command, const std::vector<std::string>& args)
{
    return runCommand(command, args, CommandOptions{});
}

Json runCommand(const std::string& command, const std::vector<std::string>& args, const CommandOptions& options)
{
    // Preserve native diagnostic bytes for legacy callers such as dependencies.
    const bool nativeAuthoring = isModelNativeCommand(command);
    const bool levelCommand = isLevelPathCommand(command);
    CommandSourceScope sourceScope(options, nativeAuthoring || levelCommand ? pathForDiagnostic : detail::legacyPathBytes);
    if ( nativeAuthoring )
    {
        if ( command == "model-native-inspect" )
        {
            return modelNativeInspect(args);
        }
        return modelNativePlanOrApply(args, command == "model-native-apply");
    }
    if (command == "schema") return cmdSchema(args);
    if ( levelCommand )
    {
        try
        {
            if ( command == "inspect" ) return cmdInspect(args);
            if ( command == "new" ) return cmdNew(args);
            if ( command == "query" ) return cmdQuery(args);
            if ( command == "select" ) return cmdSelect(args);
            if ( command == "validate" ) return cmdValidate(args);
            if ( command == "plan" ) return planOrApply(args, false);
            if ( command == "apply" ) return planOrApply(args, true);
            fail(kExitUsage, "Unknown level command");
        }
        catch ( const AgentError& error )
        {
            if ( !isValidJsonUtf8(error.message) )
            {
                throw AgentError{ error.code, "Level command failed with an unrepresentable diagnostic", error.errorCode };
            }
            throw;
        }
        catch ( const fs::filesystem_error& )
        {
            fail(kExitInput, "Cannot access the native filesystem path");
        }
    }
    if (command == "convert") return cmdConvert(args);
    if (command == "archive") return cmdArchive(args);
    if (command == "extract") return cmdExtract(args);
    if (command == "launch-plan") return cmdLaunchPlan(args);
    if (command == "item-placement-check") return cmdItemPlacementCheck(args);
    if (command == "snapshot") return cmdSnapshot(args);
    if (command == "overview") return cmdOverview(args);
    if (command == "bindings") return cmdBindings(args);
    if (command == "workbench-query") return cmdWorkbenchQuery(args);
    if (command == "arch-validate") return cmdArchValidate(args);
    if (command == "package-plan") return cmdPackagePlan(args);
    if (command == "dependencies") return cmdDependencies(args);
    if (command == "native-inspect") return cmdNativeInspect(args);
    if (command == "package-apply") return cmdPackageApply(args);
    if (command == "addCog") return cogPlanOrApply(args, true);
    if (command == "rebindCogReference") return cogPlanOrApply(args, false);
    if (command == "prefab-capture") return cmdPrefabCapture(args);
    if (command == "export-dxf") return cmdExportDxf(args);
    if (command == "terrain-3do") return cmdTerrain3do(args);
    if (command == "statistics") return cmdStatistics(args);
    if (command == "dxf-inspect") return cmdDxfInspect(args);
    if (command == "dxf-import") return cmdDxfImport(args);
    if (command == "cog-check") return cmdCogCheck(args);
    if (command == "cog-compile") return cmdCogCompile(args);
    if (command == "cog-run") return cmdCogRun(args);
    if (command == "assistant-check") return cmdAssistantCheck(args);
    if (command == "model-hierarchy") return cmdModelHierarchy(args);
    if (command == "cog-templates") return cmdCogTemplates(args);
    if (command == "cog-template-write") return cmdCogTemplateWrite(args);
    if (command == "model-hierarchy-edit") return cmdModelHierarchyEdit(args);
    if (command == "key-from-model") return cmdKeyFromModel(args);
    if (command == "assistant-script") return cmdAssistantScript(args);
    if (command == "mat-inspect") return cmdMatInspect(args);
    if (command == "mat-export-png") return cmdMatExportPng(args);
    if (command == "mat-create") return cmdMatCreate(args);
    if (command == "cmp-create") return cmdCmpCreate(args);
    if (command == "cmp-master-create") return cmdCmpMasterCreate(args);
    if (command == "archive-create") return cmdArchiveCreate(args);
    if (command == "model-import") return cmdModelImport(args);
    if (command == "jo-ja-inspect") return joJaInspect(args);
    if (command == "jo-ja-plan") return joJaPlanOrApply(args, false);
    if (command == "jo-ja-apply") return joJaPlanOrApply(args, true);
    if (command == "model-lighting-inspect") return modelLightingInspect(args);
    if (command == "model-lighting-plan") return modelLightingPlanOrApply(args, false);
    if (command == "model-lighting-apply") return modelLightingPlanOrApply(args, true);
    if (command == "key-inspect") return cmdKeyInspect(args);
    if (command == "key-plan") return keyPlanOrApply(args, false);
    if (command == "key-apply") return keyPlanOrApply(args, true);
    fail(kExitUsage, "unknown command '" + command + "'");
}

void setAgentExecutable(const fs::path& path)
{
    agentExecutable() = path;
}

void setAgentExecutable(const char* argv0)
{
    if (argv0) agentExecutable() = argv0;
}

} // namespace sed_agent
