#include "agent_workspace.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jed.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/mat.hpp"
#include "sed/level/overview.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/render/software_snapshot.hpp"

#include <cstring>  // RED: std::memcpy; MSVC pulled it in transitively
#include <map>
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/dynamic_vertex_colors.hpp"
#include "sed/level/spatial_selection.hpp"
#include "sed/level/workbench.hpp"
#include "sed/level/arch_validation.hpp"
#include "sed/level/package.hpp"

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
OpenedDocument openDocument(const fs::path& path)
{
    const std::string bytes = readFileBytes(path);
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

// Serializes an isolated document copy and reopens it, so a plan can
// never report bytes the reader would reject. Returns the output bytes.
std::string buildOutput(const OpenedDocument& doc, const sed::Level& level)
{
    try
    {
        if (doc.isSed)
        {
            std::vector<sed::assets::SedOptionalSection> sections{};
            if (const auto rgb = sed::assets::makeSedLightRgbSection(level))
                sections.push_back(*rgb);
            if (const auto arch = sed::assets::makeSedArchAuthoringSection(level))
                sections.push_back(*arch);
            const auto bytes = sections.empty()
                ? sed::assets::saveToSedBytes(level)
                : sed::assets::saveToSedBytes(level, sections);
            std::string out(bytes.size(), '\0');
            std::memcpy(out.data(), bytes.data(), bytes.size());
            (void)sed::assets::loadFromSedBytes(bytes);
            return out;
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
    if (!v || v->type != Json::Type::Number || v->number != std::floor(v->number))
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
    for (const Json& item : list->array)
    {
        if (!item.isObject()) fail(kExitUsage, "resourceArchives entries must be objects");
        const std::string path = requiredString(item, "path", "resourceArchives entry");
        const std::string expected = lower(requiredString(item, "sha256", "resourceArchives entry"));
        std::string bytes;
        try { bytes = readFileBytes(path); }
        catch (const AgentError&) { fail(kExitInput, "cannot read resource archive: " + path); }
        if (sha256Hex(bytes) != expected)
            fail(kExitConflict, "resource archive sha256 does not match (stale archive): " + path);
        sed::assets::GobArchive gob;
        try { gob = sed::assets::GobArchive::open(path); }
        catch (const std::exception& e) {
            failCoded(kExitUsage, "invalid_request",
                      "Resource archive is not a readable GOB/GOO archive: " + path + ". " + e.what());
        }
        sed::assets::CogScriptArchive archive;
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

sed::level::CogSchemaResolver cogResolver(const CogArchiveSet& set)
{
    return [set](std::string_view name) -> std::optional<sed::assets::CogSchema> {
        return sed::assets::resolveCogSchema(name, set.archives);
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

long long intField(const Json& obj, const char* key, int opIndex)
{
    const double v = numberField(obj, key, opIndex);
    if (v != std::floor(v)) fail(kExitUsage, "operation requires integer '" + std::string(key) + "'");
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
        out.push_back(sed::SurfaceRef{ static_cast<std::int32_t>(row.array[0].number),
                                       static_cast<std::int32_t>(row.array[1].number) });
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
        if (obj.find(key)) target = static_cast<std::int32_t>(intField(obj, key, opIndex));
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
            target = static_cast<std::uint32_t>(p->number);
        }
    };
    part("any", mask.any); part("all", mask.all); part("none", mask.none);
    return mask;
}

// Applies the supported edit operations to an isolated copy. Numeric
// selectors are indices into the document they were read from; callers
// bind them with inputSha256/sourceRevision (checked before this runs).
sed::Level applyOperations(const OpenedDocument& doc, const Json& request)
{
    const Json* ops = request.find("operations");
    if (!ops || ops->type != Json::Type::Array || ops->array.empty())
        fail(kExitUsage, "request requires a non-empty 'operations' array");
    if (ops->array.size() > 1000) fail(kExitUsage, "too many operations (max 1000)");
    sed::Level level = doc.level;
    std::size_t patchOutputs = 0;
    for (std::size_t i = 0; i < ops->array.size(); ++i)
    {
        const Json& op = ops->array[i];
        if (!op.isObject()) fail(kExitUsage, "operation " + std::to_string(i) + " must be an object");
        const Json* name = op.find("op");
        if (!name || name->type != Json::Type::String)
            fail(kExitUsage, "operation " + std::to_string(i) + " requires string 'op'");
        const int idx = static_cast<int>(i);
        if (name->str == "setGravity")
        {
            level.header.gravity = numberField(op, "value", idx);
        }
        else if (name->str == "setSectorAmbient")
        {
            const long long sector = intField(op, "sector", idx);
            if (sector < 0 || static_cast<std::size_t>(sector) >= level.sectors.size())
                fail(kExitInput, "sector selector out of range for this input");
            const double v = numberField(op, "value", idx);
            auto& s = level.sectors[static_cast<std::size_t>(sector)];
            s.ambient = sed::ColorF{ static_cast<float>(v), static_cast<float>(v),
                                     static_cast<float>(v), 1.0f };
        }
        else if (name->str == "setSectorFlags")
        {
            const long long sector = intField(op, "sector", idx);
            if (sector < 0 || static_cast<std::size_t>(sector) >= level.sectors.size())
                fail(kExitInput, "sector selector out of range for this input");
            level.sectors[static_cast<std::size_t>(sector)].flags =
                static_cast<std::int32_t>(intField(op, "value", idx));
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
                const std::string templateName = stringField(op, "template", idx);
                const std::string namePrefix = stringField(op, "namePrefix", idx);
                sed::TVector orientation{};
                if (op.find("orientation")) orientation = pointField(op, "orientation");
                std::int32_t layer = 0;
                if (op.find("layer")) layer = static_cast<std::int32_t>(intField(op, "layer", idx));
                std::optional<std::int32_t> sector{};
                if (op.find("sector")) sector = static_cast<std::int32_t>(intField(op, "sector", idx));
                auto prepared = sed::level::prepareThingArray(level, positions.positions,
                    templateName, namePrefix, orientation, layer, sector, patchOutputs);
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
                    color = sed::ColorF{ static_cast<float>(v->array[0].number),
                        static_cast<float>(v->array[1].number), static_cast<float>(v->array[2].number),
                        v->array.size() == 4 ? static_cast<float>(v->array[3].number) : 1.0f };
                }
                double intensity = 10.0, range = 2.0;
                std::int32_t flags = 0, layer = 0;
                if (op.find("intensity")) intensity = numberField(op, "intensity", idx);
                if (op.find("range")) range = numberField(op, "range", idx);
                if (op.find("flags")) flags = static_cast<std::int32_t>(intField(op, "flags", idx));
                if (op.find("layer")) layer = static_cast<std::int32_t>(intField(op, "layer", idx));
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
    }
    return level;
}

Json paginate(Json items, const Json& request, std::size_t total)
{
    long long offset = 0, limit = 50;
    if (const Json* v = request.find("offset"))
    {
        if (v->type != Json::Type::Number) fail(kExitUsage, "offset must be a number");
        offset = static_cast<long long>(v->number);
    }
    if (const Json* v = request.find("limit"))
    {
        if (v->type != Json::Type::Number) fail(kExitUsage, "limit must be a number");
        limit = static_cast<long long>(v->number);
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
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "num", Json::makeNumber(static_cast<double>(t.num)) },
                { "name", Json::makeString(t.name) },
                { "template", Json::makeString(t.templateName) },
                { "sector", Json::makeNumber(static_cast<double>(t.sector)) },
            }));
        }
    }
    else if (coll->str == "lights")
    {
        for (std::size_t i = 0; i < level.lights.size(); ++i)
        {
            const auto& l = level.lights[i];
            items.array.push_back(row({
                { "index", Json::makeNumber(static_cast<double>(i)) },
                { "intensity", Json::makeNumber(l.intensity) },
                { "range", Json::makeNumber(l.range) },
            }));
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
    const sed::Level edited = applyOperations(doc, request);
    const std::string output = buildOutput(doc, edited);
    const std::string outputSha = sha256Hex(output);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString(isApply ? "apply" : "plan"));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
    out.set("outputSha256", Json::makeString(outputSha));
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
    if (args.empty()) fail(kExitUsage, "validate requires a level path");
    const auto doc = openDocument(args[0]);
    Json issues = Json::makeArray();
    bool hasErrors = false;
    std::string cogStatus = "unverified-no-resources";
    Json assetNotices = Json::makeArray();
    if (args.size() > 1)
    {
        const Json request = readRequest(args[1]);
        if (!request.isObject()) fail(kExitUsage, "validate request must be a JSON object");
        checkSchemaVersion(request);
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
    // Serializability is part of structural validity: exact bytes checked.
    (void)buildOutput(doc, doc.level);
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("validate"));
    out.set("valid", Json::makeBool(!hasErrors));
    out.set("inputSha256", Json::makeString(doc.inputSha256));
    out.set("sourceRevision", Json::makeString(doc.sourceRevision));
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
    if (args.empty()) fail(kExitUsage, "new requires <new-output.jkl|new-output.sed> [mots|jk|new.json|-]");
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
    if (kindText != "mots" && kindText != "jk" && kindText != "jkdf2")
        fail(kExitUsage, "new kind must be 'mots' or 'jk'");
    checkNewOutputTarget(outputPath, outputPath, false);
    // Original one-room scaffold (single triangle sector; no retail assets).
    static const char* kScaffold = R"JKL(SECTION: HEADER
VERSION 1
WORLD GRAVITY 4
END

SECTION: MATERIALS
WORLD MATERIALS 1
0: MAT: scaffold.mat
END

SECTION: GEORESOURCE
WORLD COLORMAPS 1
0: scaffold.cmp
WORLD VERTICES 3
0: 0 0 0
1: 1 0 0
2: 0 1 0
WORLD TEXTURE VERTICES 3
0: 0 0
1: 1 0
2: 0 1
WORLD ADJOINS 0
WORLD SURFACES 1
0: 0 0x1 0x0 3 0 1 -1 0 3 0,0 1,1 2,2 0 0 1
0: 0 0 1
END

SECTION: SECTORS
WORLD SECTORS 1
SECTOR 0
FLAGS 0
AMBIENT LIGHT 0.25
EXTRA LIGHT 0
COLORMAP 0
SURFACES 0 1
END

SECTION: TEMPLATES
WORLD TEMPLATES 0
END

SECTION: THINGS
WORLD THINGS 0
END
)JKL";
    sed::Level level;
    try
    {
        std::istringstream in(kScaffold);
        level = sed::assets::loadFromJKL(in);
    }
    catch (const std::exception& e) { fail(kExitInput, std::string("cannot build scaffold: ") + e.what()); }
    level.kind = kindText == "mots" ? sed::ProjectType::MOTS : sed::ProjectType::JKDF2;
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
            (void)sed::assets::loadFromJKL(in);
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
    out.set("kind", Json::makeString(kindText == "mots" ? "mots" : "jk"));
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
    try
    {
        if (outExt == ".sed")
        {
            const auto bytes = sed::assets::saveToSedBytes(doc.level);
            output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            (void)sed::assets::loadFromSedBytes(bytes);
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

sed::assets::GobArchive openGobOrFail(const fs::path& path)
{
    if (!fs::exists(path)) fail(kExitInput, "cannot read archive: " + path.string());
    try { return sed::assets::GobArchive::open(path); }
    catch (const std::exception& e) {
        failCoded(kExitUsage, "invalid_request",
                  "Resource archive is not a readable GOB/GOO archive: " + path.string() + ". " + e.what());
    }
}

Json cmdArchive(const std::vector<std::string>& args)
{
    if (args.empty()) fail(kExitUsage, "archive requires <archive.gob|archive.goo>");
    const fs::path path = args[0];
    const auto gob = openGobOrFail(path);
    const std::string bytes = readFileBytes(path);
    Json entries = Json::makeArray();
    for (const auto& entry : gob.entries())
    {
        Json row = Json::makeObject();
        row.set("name", Json::makeString(entry.name));
        row.set("size", Json::makeNumber(static_cast<double>(entry.size)));
        row.set("offset", Json::makeNumber(static_cast<double>(entry.offset)));
        std::string sha;
        try { sha = gob.entrySha256Hex(entry.name); } catch (...) { sha = sha256Hex(""); }
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
    const fs::path exePath = fs::path(gameDir) / executable;
    const fs::path levelPath = fs::path(projectDir) / levelName;
    Json requiredFiles = Json::makeArray();
    requiredFiles.array.push_back(Json::makeString(exePath.string()));
    requiredFiles.array.push_back(Json::makeString(levelPath.string()));
    Json checks = Json::makeObject();
    checks.set("gameDirectoryExists", Json::makeBool(fs::is_directory(gameDir)));
    checks.set("executableExists", Json::makeBool(fs::exists(exePath)));
    checks.set("levelExists", Json::makeBool(fs::exists(levelPath)));
    Json arguments = Json::makeArray();
    arguments.array.push_back(Json::makeString(levelPath.string()));
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("launch-plan"));
    out.set("game", Json::makeString(game));
    out.set("engine", Json::makeString(engine));
    out.set("executable", Json::makeString(exePath.string()));
    out.set("workingDirectory", Json::makeString(gameDir));
    out.set("arguments", std::move(arguments));
    out.set("requiredFiles", std::move(requiredFiles));
    out.set("checks", std::move(checks));
    out.set("launches", Json::makeBool(false));
    out.set("note", Json::makeString("Read-only plan; no process was launched and nothing was written."));
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

    Json snapCam = Json::makeObject();
    snapCam.set("position", vec3(camera.position));
    snapCam.set("target", vec3(camera.target));
    snapCam.set("projection", Json::makeString(camera.orthographic ? "orthographic" : "perspective"));
    snapCam.set("fieldOfViewDegrees", Json::makeNumber(camera.fieldOfViewDegrees));
    snapCam.set("orthographicHeight", Json::makeNumber(camera.orthographicHeight));
    snapCam.set("near", Json::makeNumber(camera.nearPlane));
    snapCam.set("far", Json::makeNumber(camera.farPlane));
    Json snapshotRequest = Json::makeObject();
    snapshotRequest.set("schemaVersion", Json::makeNumber(1));
    snapshotRequest.set("width", Json::makeNumber(static_cast<double>(width)));
    snapshotRequest.set("height", Json::makeNumber(static_cast<double>(height)));
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
    if (isAdd) applyAddCogOp(edited, syntheticOp, request, 0);
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
    if (request.find("sector")) sector = static_cast<std::int32_t>(intField(request, "sector", 0));
    std::optional<sed::level::ExactQueryFilter> filter{};
    if (const Json* f = request.find("filter"))
    {
        if (!f->isObject()) fail(kExitUsage, "'filter' must be an object");
        sed::level::ExactQueryFilter parsed{};
        if (f->find("materialIndex")) parsed.materialIndex = static_cast<std::int32_t>(intField(*f, "materialIndex", 0));
        if (f->find("layer")) parsed.layer = static_cast<std::int32_t>(intField(*f, "layer", 0));
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
    if (request.find("offset")) page.offset = static_cast<std::size_t>(intField(request, "offset", 0));
    if (request.find("limit")) page.limit = static_cast<std::size_t>(intField(request, "limit", 0));
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
        exp.thingIndex = static_cast<std::int32_t>(intField(m, "thing", 0));
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
                static_cast<std::int32_t>(intField(row, "thing", 0)), stringField(row, "fingerprint", 0) });
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
        fail(kExitUsage, "native-inspect requires <input> <jk|mots|ijim> <jkl|native-sed> <worker-config.json>");
    const fs::path inputPath = args[0];
    const std::string kind = lower(args[1]);
    const std::string format = lower(args[2]);
    if (kind != "jk" && kind != "mots" && kind != "ijim")
        fail(kExitUsage, "native inspection project kind must be jk, mots or ijim");
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
        timeoutSeconds = static_cast<int>(timeout->number);
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
    if (v->type != Json::Type::Number || v->number < 0)
        fail(kExitUsage, std::string(key) + " must be a non-negative number");
    return static_cast<std::size_t>(v->number);
}

sed::level::PackageArchiveInput loadPackageArchive(
    const std::string& pathText, const bool stock)
{
    const fs::path path = pathText;
    sed::assets::GobArchive archive;
    try
    {
        archive = sed::assets::GobArchive::open(path);
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
            const std::string bytes = readFileBytes(fs::path(pathText));
            if (sha256Hex(bytes) != expected)
                fail(kExitConflict, "archive sha256 mismatch: " + pathText);
            core.archives.push_back(loadPackageArchive(pathText, false));
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
            (void)readFileBytes(fs::path(item.str));
            core.archives.push_back(loadPackageArchive(item.str, true));
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
        core.episodeType = static_cast<std::int32_t>(type->number);
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
                           "package-plan", "package-apply", "native-inspect" })
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
    Json ops = Json::makeArray();
    for (const char* o : { "setGravity{value}", "setSectorAmbient{sector,value}",
                           "setSectorFlags{sector,value}", "setLightRgb{light,rgbIntensity,legacyRgbRange}",
                           "addCog{script,scriptSha256,bindings[]}", "batchUvProject{surfaces,direction,origin,texelsPerUnit}",
                           "batchUvTransform{surfaces,pivot,scale,rotationDegrees,offset}",
                           "placeThingArray{layout,template,namePrefix}", "placeLightArray{layout}",
                           "rebindCogReference{cog,parameter,symbol,kind,targetIndex,scriptSha256}" })
        ops.array.push_back(Json::makeString(o));
    out.set("operations", std::move(ops));
    out.set("snapshotNote", Json::makeString(
        "snapshot and overview render world surfaces to PNG with the built-in software renderer (software-v1); things, models and dynamic lighting are out of scope"));
    return out;
}

} // namespace

std::string readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) fail(kExitInput, "cannot read file: " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) fail(kExitInput, "failed reading file: " + path.string());
    return ss.str();
}

void writeNewFileAtomic(const fs::path& path, std::string_view bytes)
{
    // Create-new semantics: stage beside the destination, then publish
    // with a rename that must not replace an existing file.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmp = path.parent_path()
        / (path.filename().string() + ".sed-agent-"
           + std::to_string(static_cast<long long>(stamp)) + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) fail(kExitRefusal, "cannot create staging file beside output");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) { std::error_code ec; fs::remove(tmp, ec); fail(kExitRefusal, "failed writing output"); }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
    {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        if (fs::exists(path)) fail(kExitRefusal, "output already exists; refusing to overwrite");
        fail(kExitRefusal, "cannot publish output file: " + ec.message());
    }
}

Json readRequest(const std::string& path)
{
    std::string bytes;
    if (path == "-")
    {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        bytes = ss.str();
    }
    else
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) fail(kExitInput, "cannot read request file: " + path);
        std::ostringstream ss;
        ss << in.rdbuf();
        bytes = ss.str();
    }
    if (bytes.size() > kMaxRequestBytes)
        fail(kExitUsage, "request exceeds the 1 MiB cap");
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);
    try { return parseJson(bytes); }
    catch (const std::exception& e) { fail(kExitUsage, std::string("malformed request JSON: ") + e.what()); }
}

Json runCommand(const std::string& command, const std::vector<std::string>& args)
{
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
    fail(kExitUsage, "unknown command '" + command + "'");
}

} // namespace sed_agent
