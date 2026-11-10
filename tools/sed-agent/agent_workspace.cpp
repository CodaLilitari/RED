#include "agent_workspace.hpp"
#include "atomic_output.hpp"
#include "agent_basic_operations.hpp"
#include "agent_entity_operations.hpp"
#include "agent_query_details.hpp"
#include "bounded_input.hpp"
#include "source_read_set.hpp"
#include "bake_resource_snapshot.hpp"
#include "archive_admission.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jed.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/jkl_structure.hpp"
#include "sed/assets/mat.hpp"
#include "sed/level/overview.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/render/software_snapshot.hpp"

#include <cstring>  // RED: std::memcpy; MSVC pulled it in transitively
#include <map>
#include <limits>
#include <memory>
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level.hpp"
#include "sed/level/build.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/dynamic_vertex_colors.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/sealed_seams.hpp"
#include "sed/level/thing_frames.hpp"
#include "sed/level/workbench.hpp"
#include "sed/level/recipe_requests.hpp"
#include "sed/level/recipes.hpp"
#include "sed/level/arch_validation.hpp"
#include "sed/level/package.hpp"
#include "sed/level/game_launch.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/emissive_resolution.hpp"
#include "sed/level/lighting.hpp"
#include "sed/level/light_limits.hpp"
#include "sed/level/prefab.hpp"
#include "sed/level/statistics.hpp"
#include "sed/assets/dxf.hpp"
#include "sed/level/dxf_import.hpp"
#include "sed/assets/key.hpp"
#include "sed/assets/cog_preflight.hpp"
#include "sed/assets/3do.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
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
constexpr std::size_t kMaxArchiveBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxExpandedArchiveBytes = 512u * 1024u * 1024u;

struct CommandResourceBudget
{
    std::size_t archiveBytes{ kMaxArchiveBytes };
    std::size_t archiveEntries{ 100000 };
    std::shared_ptr<detail::BakeResourceBudget> bake{ std::make_shared<detail::BakeResourceBudget>() };
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
    CommandSourceScope() : previous_(activeSourceReads), previousResources_(activeResourceChecks),
        previousBudget_(activeResourceBudget)
    {
        activeSourceReads = &sources_;
        activeResourceChecks = &resourceChecks_;
        activeResourceBudget = &budget_;
    }
    ~CommandSourceScope()
    {
        activeSourceReads = previous_;
        activeResourceChecks = previousResources_;
        activeResourceBudget = previousBudget_;
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
};

std::vector<std::byte> toBytes(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// Opens a JKL/NDY (text) or native SED (binary) document. Any unreadable
// or unparseable input is an input error (exit 3), never a guess.
OpenedDocument openDocumentBytes(const fs::path& path, const std::string& bytes)
{
    OpenedDocument doc;
    doc.inputSha256 = sha256Hex(bytes);
    doc.sourceRevision = doc.inputSha256; // JKL contract: revision == input hash.
    const std::string ext = extensionOf(path);
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

OpenedDocument openDocument(const fs::path& path)
{
    return openDocumentBytes(path, readFileBytes(path));
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
std::string buildNativeOutput(const OpenedDocument& doc, const sed::Level& level)
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
    std::string output(bytes.size(), '\0');
    std::memcpy(output.data(), bytes.data(), bytes.size());
    return output;
}

void checkApplyOutputFormat(const OpenedDocument& doc, const fs::path& path)
{
    const std::string expected = doc.isSed ? ".sed"
        : doc.level.kind == sed::ProjectType::IJIM ? ".ndy" : ".jkl";
    if ( extensionOf(path) != expected )
    {
        failCoded(kExitRefusal, "outputFormatMismatch",
            "Apply output must end in " + expected
            + " to match the reviewed serialization; use convert for a format change.");
    }
}

// Serializes an isolated document copy and reopens it, so a plan can
// never report bytes the reader would reject. Returns the output bytes.
std::string buildOutput(const OpenedDocument& doc, const sed::Level& level)
{
    try
    {
        if (doc.isSed)
        {
            return buildNativeOutput(doc, level);
        }
        const auto built = sed::assets::buildJklExport(level);
        std::istringstream in(built.text);
        (void)sed::assets::loadFromJKL(in);
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
        const std::string bytes = readArchiveBytes(path);
        if (sha256Hex(bytes) != expected)
            fail(kExitConflict, "resource archive sha256 does not match (stale archive): " + path);
        admitCapturedArchive(bytes, path);
        sed::assets::GobArchive gob;
        try { gob = sed::assets::GobArchive::fromBytes(toBytes(bytes), path); }
        catch (const std::exception& e) {
            failCoded(kExitUsage, "invalid_request",
                      "Resource archive is not a readable GOB/GOO archive: " + path + ". " + e.what());
        }
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

sed::level::CogSchemaResolver cogResolver(CogArchiveSet set)
{
    const auto captured = std::make_shared<const CogArchiveSet>(std::move(set));
    return [captured](std::string_view name) -> std::optional<sed::assets::CogSchema> {
        return sed::assets::resolveCogSchema(name, captured->archives);
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
    auto schema = sed::assets::resolveCogSchema(script, set.archives);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    const Json* bindingsJson = op.find("bindings");
    if (!bindingsJson || bindingsJson->type != Json::Type::Array)
        fail(kExitUsage, "addCog operation requires a 'bindings' array");
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
        bindings.push_back(sed::level::CogNamedBinding{ symbol, parseReferenceKind(kind),
            static_cast<std::int32_t>(requiredInt(b, "targetIndex", "addCog binding")) });
    }
    auto prepared = sed::level::prepareAddCog(level, script, schema ? &*schema : nullptr,
        bindings, cogResolver(set), expected);
    if (!prepared.ok()) failCogRefusal(*prepared.failure);
    if (!prepared.apply(level)) fail(kExitConflict, "addCog prepared step is stale for this document");
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
    const long long target = requiredInt(op, "targetIndex", "rebindCogReference operation");
    if (cog < 0 || static_cast<std::size_t>(cog) >= level.cogs.size())
        failCoded(kExitRefusal, "cogParameterOutOfRange", "cogParameterOutOfRange: COG index is outside the placed COG list");
    const CogArchiveSet set = [&] {
        if (op.find("resourceArchives")) { Json tmp = Json::makeObject(); tmp.set("resourceArchives", *op.find("resourceArchives")); return loadCogArchives(tmp); }
        return loadCogArchives(request);
    }();
    const std::string& scriptName = level.cogs[static_cast<std::size_t>(cog)].name;
    auto schema = sed::assets::resolveCogSchema(scriptName, set.archives);
    const auto expected = expectedScriptHash(op).has_value() ? expectedScriptHash(op) : expectedScriptHash(request);
    checkScriptHash(schema ? &*schema : nullptr, expected);
    auto prepared = sed::level::prepareRebindCogReference(level,
        static_cast<std::int32_t>(cog), static_cast<std::int32_t>(parameter),
        symbol, parseSymbolKind(kindText), static_cast<std::int32_t>(target),
        schema ? &*schema : nullptr, expected);
    if (!prepared.ok()) failCogRefusal(*prepared.failure);
    if (!prepared.apply(level)) fail(kExitConflict, "rebindCogReference prepared step is stale for this document");
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

// Applies the supported edit operations to an isolated copy. Numeric
// selectors are indices into the document they were read from; callers
// bind them with inputSha256/sourceRevision (checked before this runs).
sed::Level applyOperations(const OpenedDocument& doc, const Json& request, Json& bakeReports)
{
    const Json* ops = request.find("operations");
    if (!ops || ops->type != Json::Type::Array || ops->array.empty())
        fail(kExitUsage, "request requires a non-empty 'operations' array");
    if (ops->array.size() > 1000) fail(kExitUsage, "too many operations (max 1000)");
    sed::Level level = doc.level;
    std::size_t patchOutputs = 0;
    std::size_t remainingLightDiagnostics = 256;
    bool appendIdentityChecked = false;
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
            "buildSectorArray", "buildSpiralStairs", "buildHeightmapTerrain", "insertPrefab" };
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
            "buildSectorArray", "buildSpiralStairs", "buildHeightmapTerrain", "insertPrefab", "addCog" };
        if (!appendIdentityChecked && std::find(std::begin(allocatingOperations),
                std::end(allocatingOperations), name->str) != std::end(allocatingOperations))
        {
            validateAppendIdentitySource(level);
            appendIdentityChecked = true;
        }
        if (tryApplyBasicOperation(level, op))
        {
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
                auto prepared = sed::level::prepareLightArray(level, positions.positions,
                    color, intensity, range, flags, layer, patchOutputs);
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
                activeResourceBudget ? activeResourceBudget->bake : std::make_shared<detail::BakeResourceBudget>());
            const sed::level::EmissiveResourceReader reader = [captured](std::string_view resource, std::string_view extension) {
                return captured->read(resource, extension);
            };
            const auto emissive = sed::level::resolveEmissiveSurfaces(level, reader, intensity, range);
            try { captured->throwIfReadFailed(); }
            catch (const detail::ResourceRevisionError& error) { fail(kExitConflict, error.what()); }
            catch (const std::exception& error) { fail(kExitInput, error.what()); }
            if (activeResourceChecks)
                activeResourceChecks->push_back([captured] { captured->requireUnchanged(); });
            std::vector<sed::level::LightingEmissiveSurface> emissiveSurfaces = emissive.surfaces;
            // E-17 sun/sky profile: FULLYLIT sky-flagged surfaces emit the
            // uniform sky colour alongside MAT-resolved emissives.
            double skyIntensity = 0.0;
            double skyRange = sed::level::kDefaultEmissiveRange;
            if (op.find("skyIntensity")) skyIntensity = numberField(op, "skyIntensity", idx);
            if (op.find("skyRange")) skyRange = numberField(op, "skyRange", idx);
            if (skyIntensity > 0.0)
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
            // E-17 bounce profile: optional one-bounce diffuse pass.
            double bounceStrength = 0.0;
            double bounceRange = sed::level::kDefaultEmissiveRange;
            if (op.find("bounceStrength")) bounceStrength = numberField(op, "bounceStrength", idx);
            if (op.find("bounceRange")) bounceRange = numberField(op, "bounceRange", idx);
            if (bounceStrength > 0.0)
            {
                if (!sed::level::emissiveProfileValid(bounceStrength, bounceRange))
                    fail(kExitUsage, "bakeLighting bounceStrength/bounceRange must be finite and positive");
                (void)sed::level::levelCalcLightingNewWithBounce(
                    level, allSectors, emissiveSurfaces, bounceStrength, bounceRange);
            }
            else
            {
                sed::level::levelCalcLightingNew(level, allSectors, emissiveSurfaces);
            }
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
            summary.set("scope", Json::makeString("stored-corner-rgb"));
            summary.set("policyApplied", Json::makeBool(applyLimits));
            summary.set("lightFloor", Json::makeNumber(limits.floor));
            summary.set("lightCap", Json::makeNumber(limits.cap));
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

Json cmdInspect(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "inspect requires a level path");
    const auto doc = openDocument(args[0]);
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
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "query request must be a JSON object");
    checkRevision(doc, request);
    const Json* coll = request.find("collection");
    if (!coll || coll->type != Json::Type::String) fail(kExitUsage, "query requires 'collection'");
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
    else if (coll->str == "sectors")
    {
        for (std::size_t i = 0; i < level.sectors.size(); ++i)
        {
            const auto& s = level.sectors[i];
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "num", Json::makeNumber(static_cast<double>(s.num)) },
                { "flags", Json::makeNumber(static_cast<double>(s.flags)) },
                { "ambient", Json::makeNumber(static_cast<double>(s.ambient.r)) },
                { "surfaces", Json::makeNumber(static_cast<double>(s.surfaces.size())) },
                { "colormap", Json::makeString(s.colormap) },
                { "colormapIndex", Json::makeNumber(static_cast<double>(s.colormapIndex)) },
            }));
        }
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
    else if (coll->str == "cogs")
    {
        for (std::size_t i = 0; i < level.cogs.size(); ++i)
        {
            const auto& c = level.cogs[i];
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "num", Json::makeNumber(static_cast<double>(c.num)) },
                { "name", Json::makeString(c.name) },
            }));
        }
    }
    else if (coll->str == "templates")
    {
        for (std::size_t i = 0; i < level.templates.size(); ++i)
        {
            if (!matchesSearch(level.templates[i].name, search)) continue;
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "name", Json::makeString(level.templates[i].name) },
            }));
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
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
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

Json planOrApply(const std::vector<std::string>& args, bool isApply)
{
    if (args.size() < (isApply ? 3u : 2u))
        fail(kExitUsage, isApply ? "apply requires <level> <patch.json|-> <new-output>"
                                 : "plan requires <level> <patch.json|->");
    const fs::path inputPath = args[0];
    const auto doc = openDocument(inputPath);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "patch request must be a JSON object");
    if (const Json* v = request.find("schemaVersion"))
    {
        if (v->type != Json::Type::Number || v->number != 1)
            fail(kExitUsage, "unsupported schemaVersion (expected 1)");
    }
    checkRevision(doc, request);
    Json bakeReports = Json::makeArray();
    const sed::Level edited = applyOperations(doc, request, bakeReports);
    const std::string output = buildOutput(doc, edited);
    const std::string outputSha = sha256Hex(output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(isApply ? "apply" : "plan"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("outputSha256", Json::makeString(outputSha));
    if ( !bakeReports.array.empty() ) out.set("bakeLightingReports", std::move(bakeReports));
    if (doc.isJkl)
    {
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

    const fs::path outputPath = args[2];
    checkApplyOutputFormat(doc, outputPath);
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
    writeNewFileAtomic(outputPath, output);
    out.set("output", Json::makeString(outputPath.string()));
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
    Json request = Json::makeObject();
    if ( positional.size() == 2 )
    {
        request = readRequest(positional[1]);
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
        if ( !limits->isObject() ) fail(kExitUsage, "validate seamLimits must be an object");
        for ( const auto& [name, value] : limits->object )
        {
            const double maximum = name == "maxSurfacePairs" ? 32000000.0
                : name == "maxEdgePairs" ? 256000000.0 : -1.0;
            if ( maximum < 0.0 ) fail(kExitUsage, "unknown seamLimits field: " + name);
            if ( value.type != Json::Type::Number || !std::isfinite(value.number)
                || value.number < 0.0 || value.number > maximum || value.number != std::floor(value.number) )
                fail(kExitUsage, "seamLimits field must be an integer within its documented work bound: " + name);
            auto& target = name == "maxSurfacePairs" ? seamLimits.maxSurfacePairs : seamLimits.maxEdgePairs;
            target = static_cast<std::size_t>(value.number);
        }
    }
    if ( strict )
    {
        const auto extension = extensionOf(positional[0]);
        if ( !extension.empty() && extension != ".jkl" )
            fail(kExitUsage, "validate --strict supports raw JKL text only");
        strictBytes = readFileBytes(positional[0]);
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
    const auto doc = strict ? openDocumentBytes(positional[0], strictBytes)
                            : openDocument(positional[0]);
    Json issues = Json::makeArray();
    bool hasErrors = false;
    std::string cogStatus = "unverified-no-resources";
    Json assetNotices = Json::makeArray();
    if (positional.size() > 1)
    {
        checkRevision(doc, request);
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
    const auto seamStatus = [&]() -> const char*
    {
        using Status = sed::level::SealedSeamStatus;
        switch ( seams.status )
        {
        case Status::Complete: return "complete";
        case Status::BudgetExceeded: return "budget-exceeded";
        case Status::InvalidGeometry: return "invalid-geometry";
        case Status::ArithmeticUnresolved: return "arithmetic-unresolved";
        case Status::AllocationFailure: return "allocation-failure";
        case Status::NotImplemented: return "not-implemented";
        }
        return "unknown";
    }();
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
    seamWorkLimits.set("maxSurfacePairs", Json::makeNumber(static_cast<double>(seamLimits.maxSurfacePairs)));
    seamWorkLimits.set("maxEdgePairs", Json::makeNumber(static_cast<double>(seamLimits.maxEdgePairs)));
    out.set("seamLimits", std::move(seamWorkLimits));
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
    if (args.empty()) fail(kExitUsage, "new requires <new-output.jkl|new-output.sed> [mots|jk|droidworks|new.json|-]");
    const fs::path outputPath = args[0];
    const std::string outExt = extensionOf(outputPath);
    if (outExt != ".jkl" && outExt != ".ndy" && outExt != ".sed")
        fail(kExitUsage, "new output must end in .jkl, .ndy or .sed");
    std::string kindText = "mots";
    if (args.size() > 1)
    {
        if (args[1] == "-" || extensionOf(fs::path(args[1])) == ".json")
        {
            const Json request = readRequest(args[1]);
            if (!request.isObject()) fail(kExitUsage, "new request must be a JSON object");
            checkSchemaVersion(request);
            if (auto k = optionalString(request, "kind")) kindText = lower(*k);
            else if (auto k = optionalString(request, "project")) kindText = lower(*k);
            else if (auto k = optionalString(request, "game")) kindText = lower(*k);
        }
        else kindText = lower(args[1]);
    }
    if (kindText != "mots" && kindText != "jk" && kindText != "jkdf2"
        && kindText != "droidworks")
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
    level.header.version = 1;
    level.header.gravity = 4.0;
    // No real MAT is mounted for an asset-free scaffold. Keep explicit pixel
    // UVs, as JKL imports do, rather than requiring invented MAT dimensions
    // during its first engine-format export.
    level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
    level.masterCMP = "scaffold.cmp";
    level.colormaps = { level.masterCMP };
    level.materials = { "scaffold.mat" };
    try
    {
        auto& room = sed::level::levelCreateCube(
            level, -1.0, -1.0, -1.0, { 0.0, 0.0, 1.0 }, { 2.0, 0.0, 0.0 });
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
    writeNewFileAtomic(outputPath, output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("new"));
    out.set("output", Json::makeString(outputPath.string()));
    out.set("outputSha256", Json::makeString(sha256Hex(output)));
    out.set("kind", Json::makeString(
        isDroidWorks ? "droidworks" : kindText == "mots" ? "mots" : "jk"));
    return out;
}

Json cmdConvert(const std::vector<std::string>& args)
{
    if (args.size() < 2) fail(kExitUsage, "convert requires <input> <new-output> [expected-output-sha256]");
    const fs::path inputPath = args[0];
    const fs::path outputPath = args[1];
    const std::string outExt = extensionOf(outputPath);
    if (outExt != ".jkl" && outExt != ".ndy" && outExt != ".sed")
        fail(kExitUsage, "convert output must end in .jkl, .ndy or .sed");
    const auto doc = openDocument(inputPath);
    std::string output;
    if (outExt != ".sed")
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
    checkNewOutputTarget(inputPath, outputPath, true);
    writeNewFileAtomic(outputPath, output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("convert"));
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
};

std::string entryBasename(const std::string& name)
{
    const std::size_t at = name.find_last_of("\\/");
    return lower(at == std::string::npos ? name : name.substr(at + 1));
}

// Renders one snapshot PNG in memory. Materials resolve from the
// supplied archives by entry basename; the master palette (level
// World Colormap 0 else dflt.cmp, per getMasterCMP) decodes indexed
// MATs. Unresolvable materials flat-shade and are counted.
SnapshotRenderOutcome renderSnapshotPng(const OpenedDocument& doc, const CogArchiveSet& set,
                                        const sed::render::SnapshotCamera& camera,
                                        const int width, const int height,
                                        const bool fullbright, const bool cullBackFaces)
{
    std::optional<sed::assets::CmpPalette> palette{};
    const std::string master = entryBasename(doc.level.getMasterCMP());
    for (const auto& archive : set.archives)
    {
        for (const auto& e : archive)
        {
            if (entryBasename(e.entryName) == master)
            {
                std::vector<std::byte> raw(e.text.size());
                for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::byte>(e.text[i]);
                palette = sed::assets::parseCmpPalette(raw);
            }
        }
    }
    std::map<std::string, sed::render::SnapshotMaterial> cache{};
    std::map<std::string, bool> tried{};
    const sed::render::SnapshotMaterialResolver resolver =
        [&](const std::string_view name) -> const sed::render::SnapshotMaterial* {
        const std::string key = entryBasename(std::string(name));
        if (auto it = cache.find(key); it != cache.end()) return &it->second;
        if (tried[key]) return nullptr;
        tried[key] = true;
        for (const auto& archive : set.archives)
        {
            for (const auto& e : archive)
            {
                if (entryBasename(e.entryName) != key) continue;
                try
                {
                    std::vector<std::byte> raw(e.text.size());
                    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::byte>(e.text[i]);
                    const sed::assets::MatFile mat = sed::assets::MatFile::parse(
                        std::move(raw), palette ? &*palette : nullptr);
                    sed::render::SnapshotMaterial material{};
                    material.image = mat.decodeCelRgba8(0);
                    material.trueColor = mat.isTrueColor();
                    material.alphaBpp = static_cast<std::int32_t>(mat.colorInfo().alphaBPP);
                    auto [it, inserted] = cache.emplace(key, std::move(material));
                    return &it->second;
                }
                catch (const std::exception&)
                {
                    return nullptr;
                }
            }
        }
        return nullptr;
    };
    sed::render::SoftwareSnapshotOptions options{};
    options.width = width;
    options.height = height;
    options.camera = camera;
    options.fullbright = fullbright;
    options.cullBackFaces = cullBackFaces;
    SnapshotRenderOutcome outcome{};
    outcome.result = sed::render::renderSoftwareSnapshot(doc.level, resolver, options);
    outcome.materialsResolved = outcome.result.materialsUsed;
    outcome.materialsMissing = outcome.result.materialsMissing;
    return outcome;
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
                                     static_cast<int>(height), fullbright, cullBackFaces);
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
    out.set("renderer", Json::makeString("software-v1"));
    out.set("renderScope", Json::makeString("world-surfaces"));
    out.set("fullbright", Json::makeBool(fullbright));
    out.set("cullBackFaces", Json::makeBool(cullBackFaces));
    return out;
}

Json cmdOverview(const std::vector<std::string>& args)
{
    // D-09 framing half: computes the Wren overview camera and the
    // equivalent snapshot request. Rendering itself still requires a
    // renderer the Qt-free agent does not have, so the outcome says
    // so (renderStatus) and no image is created.
    if (args.size() < 2) fail(kExitUsage, "overview requires <level> <overview.json|->");
    const auto doc = openDocument(args[0]);
    const Json request = readRequest(args[1]);
    if (!request.isObject()) fail(kExitUsage, "overview request must be a JSON object");
    checkSchemaVersion(request);
    const std::string view = requiredString(request, "view", "overview request");
    std::string framing{ sed::level::kOverviewWren };
    if (auto f = optionalString(request, "framing")) framing = *f;
    const long long width = requiredInt(request, "width", "overview request");
    const long long height = requiredInt(request, "height", "overview request");
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
        // with the software renderer (world surfaces only).
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
                fullbright, cullBackFaces);
            const std::string pngBytes(
                reinterpret_cast<const char*>(rendered.result.png.data()),
                rendered.result.png.size());
            writeNewFileAtomic(fs::path(*outputPath), pngBytes);
            out.set("renderStatus", Json::makeString("rendered"));
            imageCreated = true;
            out.set("output", Json::makeString(*outputPath));
            out.set("imageSha256", Json::makeString(sha256Hex(pngBytes)));
            out.set("imageBytes", Json::makeNumber(static_cast<double>(pngBytes.size())));
            out.set("renderer", Json::makeString("software-v1"));
            out.set("renderScope", Json::makeString("world-surfaces"));
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
    auto schema = sed::assets::resolveCogSchema(scriptName, set.archives);
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
    std::string cogScriptSha;
    if (const auto expected = expectedScriptHash(request))
    {
        // The hash was already verified against the resolved schema above.
        cogScriptSha = lower(*expected);
    }
    const std::string output = buildOutput(doc, edited);
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

sed::assets::MatFile loadMatFile(const fs::path& path)
{
    const std::string bytes = readFileBytes(path);
    std::vector<std::byte> data(bytes.size());
    std::memcpy(data.data(), bytes.data(), bytes.size());
    try
    {
        return sed::assets::MatFile::parse(std::move(data));
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
    if (args.size() < 2)
        fail(kExitUsage, "mat-export-png requires <file.mat> <new-out.png> [cel]");
    std::int32_t celIndex = 0;
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
    const sed::assets::MatFile mat = loadMatFile(inputPath);
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

Json cmdSchema()
{
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("protocol", Json::makeString("sed-agent"));
    out.set("schemaVersion", Json::makeNumber(1));
    out.set("note", Json::makeString(
        "This is a tool-neutral CLI and does not claim to implement MCP."));
    Json cmds = Json::makeArray();
    for (const char* c : { "help", "schema", "inspect", "query", "select", "validate", "workbench-query", "arch-validate", "plan", "apply",
                           "new", "convert", "archive", "extract", "launch-plan", "snapshot",
                           "bindings", "addCog", "rebindCogReference", "overview",
                           "package-plan", "package-apply", "native-inspect", "prefab-capture",
                           "export-dxf", "dxf-inspect", "dxf-import", "cog-check", "mat-inspect", "mat-export-png", "terrain-3do", "statistics", "key-inspect", "key-plan", "key-apply" })
        cmds.array.push_back(Json::makeString(c));
    out.set("commands", std::move(cmds));
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
    for (const char* o : { "addLight{position,rgb,intensity,range,rgbIntensity?,legacyRgbRange?,flags?,layer?}",
                           "setLight{light,position?,rgb?,intensity?,range?,rgbIntensity?,legacyRgbRange?,flags?,layer?}",
                           "setThingPosition{thing,position,sector}", "setThingOrientation{thing,orientation}",
                           "setThingName{thing,name}", "setThingTemplate{thing,template}",
                           "setThingValue{thing,key,value}", "clearThingArchitecture{thing}",
                           "addBox{center,halfSize}", "extrudeSurface{sector,surface,distance}",
                           "adjoinSurfaces{sector,surface,otherSector,otherSurface}",
                           "registerMaterial{name}", "registerColormap{name}",
                           "setSurfaceMaterial{sector,surface,materialIndex}",
                           "setSectorColormap{sector,colormapIndex}",
                           "setSurfaceRenderModes{sector,surface,geometryMode,lightMode,textureMode}",
                           "setSurfaceUv{sector,surface,uv}", "setVertexLight{sector,vertex,channel,value}",
                           "setGravity{value}", "addThing{sector,template,name,position}", "setThingFrames{thing,frames}", "setSectorAmbient{sector,value}",
                           "setSectorFlags{sector,flags|value}", "setLightRgb{light,rgbIntensity,legacyRgbRange}",
                           "addCog{script,scriptSha256,bindings[]}", "batchUvProject{surfaces,direction,origin,texelsPerUnit}",
                           "batchUvCylindrical{surfaces,axisOrigin,axisDirection,texelsPerUnit}",
            "batchUvSpherical{surfaces,center,poleDirection,texelsPerUnit}",
                           "batchUvTransform{surfaces,pivot,scale,rotationDegrees,offset}",
                           "placeThingArray{layout,template,namePrefix}", "placeLightArray{layout}",
                           "applyMaskedSurfaceRecipe{surfaces,surfaceFlags,faceFlags,adjoinFlags,minSlopeDegrees,maxSlopeDegrees,copyFrom,copyFields,setSurfaceFlags,clearSurfaceFlags,setFaceFlags,clearFaceFlags,setAdjoinFlags,clearAdjoinFlags,material}",
                           "bakeLighting{resourceRoots,gobs,intensity,range,bounceStrength,bounceRange,skyIntensity,skyRange,skyColor}",
                           "buildHeightmapTerrain{origin,cellSize,headroom,columns,rows,heights}",
                           "buildSpiralStairs{center,innerRadius,outerRadius,startAngleDegrees,stepAngleDegrees,rise,headroom,steps}",
                           "buildSectorArray{sectors,layout}",
                           "buildDome{center,radius,headroom,segments,rings}",
                           "buildSweptCorridor{center,innerRadius,outerRadius,startAngleDegrees,sweepDegrees,segments,risePerSegment,headroom}",
                           "buildThingPath{thing,waypoints}", "insertPrefab{prefabFile,target}",
                           "rebindCogReference{cog,parameter,symbol,kind,targetIndex,scriptSha256}" })
        ops.array.push_back(Json::makeString(o));
    out.set("operations", std::move(ops));
    out.set("snapshotNote", Json::makeString(
        "snapshot and overview render world surfaces to PNG with the built-in software renderer (software-v1); things, models and dynamic lighting are out of scope"));
    return out;
}

} // namespace

std::string readFileBytes(const fs::path& path, const std::size_t maxBytes)
{
    try
    {
        std::string bytes = detail::readBoundedFile(path, maxBytes);
        if (activeSourceReads) activeSourceReads->track(path, bytes, maxBytes);
        return bytes;
    }
    catch (const detail::SourceRevisionError& error) { fail(kExitConflict, error.what()); }
    catch (const detail::InputReadError& error)
    {
        failCoded(kExitInput, error.kind == detail::InputReadError::Kind::LimitExceeded
            ? "inputTooLarge" : "inputReadFailed", error.what());
    }
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
    std::string bytes;
    try
    {
        bytes = path == "-" ? detail::readBoundedStream(std::cin, kMaxRequestBytes)
            : detail::readBoundedFile(fs::path(path), kMaxRequestBytes);
        if (path != "-" && activeSourceReads)
            activeSourceReads->track(fs::path(path), bytes, kMaxRequestBytes);
    }
    catch (const detail::SourceRevisionError& error) { fail(kExitConflict, error.what()); }
    catch (const detail::InputReadError& error)
    {
        if (error.kind == detail::InputReadError::Kind::LimitExceeded)
            fail(kExitUsage, "request exceeds the 1 MiB cap");
        fail(kExitInput, error.what());
    }
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);
    try { return parseJson(bytes); }
    catch (const std::exception& e) { fail(kExitUsage, std::string("malformed request JSON: ") + e.what()); }
}

Json runCommand(const std::string& command, const std::vector<std::string>& args)
{
    CommandSourceScope sourceScope;
    if (command == "schema") return cmdSchema();
    if (command == "inspect") return cmdInspect(args);
    if (command == "query") return cmdQuery(args);
    if (command == "select") return cmdSelect(args);
    if (command == "validate") return cmdValidate(args);
    if (command == "plan") return planOrApply(args, false);
    if (command == "apply") return planOrApply(args, true);
    if (command == "new") return cmdNew(args);
    if (command == "convert") return cmdConvert(args);
    if (command == "archive") return cmdArchive(args);
    if (command == "extract") return cmdExtract(args);
    if (command == "launch-plan") return cmdLaunchPlan(args);
    if (command == "snapshot") return cmdSnapshot(args);
    if (command == "overview") return cmdOverview(args);
    if (command == "bindings") return cmdBindings(args);
    if (command == "workbench-query") return cmdWorkbenchQuery(args);
    if (command == "arch-validate") return cmdArchValidate(args);
    if (command == "package-plan") return cmdPackagePlan(args);
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
    if (command == "mat-inspect") return cmdMatInspect(args);
    if (command == "mat-export-png") return cmdMatExportPng(args);
    if (command == "key-inspect") return cmdKeyInspect(args);
    if (command == "key-plan") return keyPlanOrApply(args, false);
    if (command == "key-apply") return keyPlanOrApply(args, true);
    fail(kExitUsage, "unknown command '" + command + "'");
}

} // namespace sed_agent
