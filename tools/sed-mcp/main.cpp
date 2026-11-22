// sed-mcp: a thin stdio JSON-RPC (MCP) transport over the F-01
// sed-agent domain layer (sed_agent::runCommand over sed_core).
//
// This file contains ONLY transport, file policy, and the operation
// journal. It contains no level parsing, geometry, or edit logic:
// every tool call is translated into the same sed-agent command the
// CLI runs, so a reviewer can confirm there is no second edit path
// by checking that domain behaviour lives in tools/sed-agent and
// src/core, never here.
#include "agent_workspace.hpp"
#include "agent_campaign.hpp"
#include "agent_model_lighting.hpp"
#include "agent_model_native.hpp"
#include "utf8_path.hpp"
#include "windows_arguments.hpp"
#include "agent_schema.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using sed_agent::Json;

namespace
{

constexpr std::size_t kMaxLineBytes = 1024 * 1024; // mirrors the agent cap

fs::path g_workspace;
std::vector<fs::path> g_resourceRoots;
int g_tempCounter = 0;

Json textResult(Json payload, bool isError)
{
    Json content = Json::makeArray();
    Json item = Json::makeObject();
    item.set("type", Json::makeString("text"));
    item.set("text", Json::makeString(sed_agent::toJsonString(payload)));
    content.array.push_back(std::move(item));
    Json result = Json::makeObject();
    result.set("content", std::move(content));
    result.set("isError", Json::makeBool(isError));
    return result;
}

Json agentErrorPayload(const sed_agent::AgentError& e)
{
    Json out = e.hasPayload && e.payload.isObject() ? e.payload : Json::makeObject();
    if ( e.hasPayload && !e.payload.isObject() )
    {
        out.set("payload", e.payload);
    }
    out.set("ok", Json::makeBool(false));
    out.set("exitCode", Json::makeNumber(static_cast<double>(e.code)));
    if ( !out.find("error") ) out.set("error", Json::makeString(e.message));
    if (!e.errorCode.empty())
    {
        out.set("code", Json::makeString(e.errorCode));
        out.set("errorCode", Json::makeString(e.errorCode));
    }
    return out;
}

Json policyErrorPayload(const std::string& code, const std::string& message)
{
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(false));
    out.set("code", Json::makeString(code));
    out.set("errorCode", Json::makeString(code));
    out.set("error", Json::makeString(message));
    return out;
}

// --- file policy ----------------------------------------------------------
// Writes are confined to the workspace; reads may also come from the
// workspace or an explicitly configured read-only resource root.

fs::path normalizedAbsolute(const fs::path& p)
{
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (ec) abs = p;
    return abs.lexically_normal();
}

bool hasPolicyRoot(const fs::path& resolvedPath, const fs::path& resolvedRoot)
{
    auto candidate = resolvedPath.begin();
    bool exact = true;
    for ( const auto& component : resolvedRoot )
    {
        if ( candidate == resolvedPath.end() )
        {
            return false;
        }
        exact = exact && candidate->native() == component.native();
        ++candidate;
    }
    if ( exact )
    {
        return true;
    }
#ifdef _WIN32
    // A case difference can denote either the same directory or a sibling on
    // a case-sensitive volume. Let the filesystem establish directory identity.
    fs::path prefix;
    for ( auto item = resolvedPath.begin(); item != candidate; ++item )
    {
        prefix /= *item;
    }
    std::error_code error;
    return fs::equivalent(prefix, resolvedRoot, error) && !error;
#else
    return false;
#endif
}

bool isWithin(const fs::path& path, const fs::path& root)
{
    // Resolve existing parents before admitting a path. Missing tails remain
    // valid for create-only output paths.
    std::error_code error;
    const auto resolvedPath = fs::weakly_canonical(normalizedAbsolute(path), error);
    if ( error )
    {
        return false;
    }
    const auto resolvedRoot = fs::weakly_canonical(normalizedAbsolute(root), error);
    return !error && hasPolicyRoot(resolvedPath, resolvedRoot);
}

fs::path resolveAgainstWorkspace(const fs::path& p)
{
    if ( p.is_absolute() )
    {
        return normalizedAbsolute(p);
    }
    return normalizedAbsolute(g_workspace / p);
}

bool checkReadablePath(const fs::path& resolved, const std::string& raw, Json& errorOut)
{
    if ( isWithin(resolved, g_workspace) )
    {
        return true;
    }
    for ( const auto& root : g_resourceRoots )
    {
        if ( isWithin(resolved, root) )
        {
            return true;
        }
    }
    errorOut = policyErrorPayload("outsideWorkspace",
        "path is outside the workspace and resource roots: " + raw);
    return false;
}

bool checkReadable(const std::string& raw, Json& errorOut)
{
    return checkReadablePath(resolveAgainstWorkspace(raw), raw, errorOut);
}

bool checkWritablePath(const fs::path& resolved, const std::string& raw, Json& errorOut)
{
    if ( isWithin(resolved, g_workspace) )
    {
        return true;
    }
    errorOut = policyErrorPayload("outsideWorkspace",
        "write path is outside the workspace: " + raw);
    return false;
}

bool checkWritable(const std::string& raw, Json& errorOut)
{
    return checkWritablePath(resolveAgainstWorkspace(raw), raw, errorOut);
}

std::string workspaceRelativeArg(const std::string& raw)
{
    // The agent layer resolves paths as given; hand it the absolute
    // in-workspace path so behaviour does not depend on our cwd.
    return resolveAgainstWorkspace(raw).string();
}

// Admit named loose resource roots, then let the domain enforce policy on the
// actual resolved winning file before reading it. Recursively scanning every
// descendant here duplicates locator work and can repeat per root/operation.
// Normalize only documented filesystem fields, never arbitrary strings named
// "path" inside level/template data. Keep the original arguments for journal
// fingerprints: staging absolute paths must not change replay identity.
bool normalizeRequestPaths(const std::string& tool, Json& arguments, Json& errorOut)
{
    auto requestIt = arguments.object.find("request");
    if (requestIt == arguments.object.end() || !requestIt->second.isObject()) return true;
    Json& request = requestIt->second;
    auto path = [&](Json& object, const char* key, bool writable = false)
    {
        if (!object.isObject()) return true; // The domain validates structure.
        auto it = object.object.find(key);
        if (it == object.object.end()) return true;
        if (it->second.type != Json::Type::String || it->second.str.empty())
        {
            errorOut = policyErrorPayload("invalidArguments", tool + " request field '" + key + "' must be a non-empty path string");
            return false;
        }
        const auto raw = it->second.str;
        if ( tool == "sed_campaign_check" )
        {
            if ( !sed_agent::isUtf8PathText(raw) )
            {
                errorOut = policyErrorPayload("invalidArguments", "Campaign input paths must be valid UTF-8");
                return false;
            }
            const auto resolved = resolveAgainstWorkspace(sed_agent::pathFromUtf8(raw));
            if ( !checkReadablePath(resolved, raw, errorOut) )
            {
                return false;
            }
            it->second = Json::makeString(sed_agent::pathToUtf8(resolved));
            return true;
        }
        if (!(writable ? checkWritable(raw, errorOut) : checkReadable(raw, errorOut))) return false;
        it->second = Json::makeString(workspaceRelativeArg(raw));
        return true;
    };
    auto boundFile = [&](Json& object, const char* key)
    {
        auto it = object.object.find(key);
        return it == object.object.end() || path(it->second, "path");
    };
    auto fileArray = [&](Json& object, const char* key, bool aliases = false)
    {
        auto it = object.object.find(key);
        if (it == object.object.end() || it->second.type != Json::Type::Array) return true;
        for (auto& entry : it->second.array)
            if (!path(entry, "path") || (aliases && !path(entry, "fileName"))) return false;
        return true;
    };
    auto stringPaths = [&](Json& object, const char* key)
    {
        auto it = object.object.find(key);
        if (it == object.object.end() || it->second.type != Json::Type::Array) return true;
        for (auto& entry : it->second.array)
        {
            Json holder = Json::makeObject(); holder.set("path", entry);
            if (!path(holder, "path")) return false;
            entry = holder.object.at("path");
        }
        return true;
    };
    const bool patch = tool == "sed_plan" || tool == "sed_apply";
    if (patch || tool == "sed_snapshot" || tool == "sed_overview" || tool == "sed_bindings" || tool == "sed_diagnose")
        if (!fileArray(request, "resourceArchives") || !boundFile(request, "templateSource")) return false;
    if (patch)
    {
        auto operations = request.object.find("operations");
        if (operations != request.object.end() && operations->second.type == Json::Type::Array)
            for (auto& operation : operations->second.array)
            {
                if (!operation.isObject()) continue;
                if (!fileArray(operation, "resourceArchives") || !boundFile(operation, "templateSource")) return false;
                const auto* op = operation.find("op");
                if (!op || op->type != Json::Type::String) continue;
                if (op->str == "insertPrefab" && !path(operation, "prefabFile")) return false;
                if (op->str == "bakeLighting"
                    && (!stringPaths(operation, "resourceRoots") || !stringPaths(operation, "gobs"))) return false;
            }
    }
    if ( tool == "sed_campaign_check" )
    {
        return boundFile(request, "episode") && fileArray(request, "inputs") && boundFile(request, "baseline");
    }
    if (tool == "sed_arch_validate" && !fileArray(request, "models")) return false;
    if (tool == "sed_dependencies" && !fileArray(request, "archives")) return false;
    if (tool == "sed_package_plan" || tool == "sed_package_apply")
        if (!fileArray(request, "archives", true) || !stringPaths(request, "stockArchives")) return false;
    if (tool == "sed_mat_create")
        if (!fileArray(request, "cels") || !boundFile(request, "palette")) return false;
    if (tool == "sed_cmp_create" && !boundFile(request, "master")) return false;
    if (tool == "sed_assistant_check" && (!boundFile(request, "level") || !boundFile(request, "script"))) return false;
    if (tool == "sed_cog_run")
    {
        if (!boundFile(request, "level")) return false;
        auto cogs = request.object.find("cogs");
        if (cogs != request.object.end() && cogs->second.type == Json::Type::Array)
            for (auto& cog : cogs->second.array)
                if (cog.isObject() && !boundFile(cog, "script")) return false;
    }
    if (tool == "sed_cmp_master_create"
        && (!boundFile(request, "master") || !fileArray(request, "protectMaterials") || !fileArray(request, "protectArchives")))
        return false;
    if (tool == "sed_archive_create" && !fileArray(request, "entries")) return false;
    if (tool == "sed_jo_ja_plan" || tool == "sed_jo_ja_apply")
    {
        const auto normalizeRecipe = [&](Json& recipe) {
            if (!boundFile(recipe, "source") || !boundFile(recipe, "palette")) return false;
            auto materials = recipe.object.find("materials");
            if (materials != recipe.object.end() && materials->second.type == Json::Type::Array)
                for (auto& material : materials->second.array)
                    if (material.isObject() && !boundFile(material, "source")) return false;
            return true;
        };
        if (!normalizeRecipe(request)) return false;
        auto reimport = request.object.find("reimport");
        if (reimport != request.object.end() && reimport->second.isObject())
        {
            if (!boundFile(reimport->second, "authoredArchive")) return false;
            auto baseline = reimport->second.object.find("baselineRecipe");
            if (baseline != reimport->second.object.end() && baseline->second.isObject()
                && !normalizeRecipe(baseline->second)) return false;
        }
    }
    if (tool == "sed_model_import")
    {
        if (!boundFile(request, "source") || !path(request, "exportBaseColor", true)) return false;
        auto materials = request.object.find("materials");
        if (materials != request.object.end() && materials->second.type == Json::Type::Array)
            for (auto& material : materials->second.array)
                if (material.isObject() && !boundFile(material, "matFile")) return false;
    }
    if (tool == "sed_overview" && !path(request, "output", true)) return false;
    if (tool == "sed_launch_plan")
    {
        if (!path(request, "gameDirectory") || !path(request, "projectDirectory")) return false;
        auto executable = request.object.find("executable");
        const auto* gameDirectory = request.find("gameDirectory");
        if (executable != request.object.end() && executable->second.type == Json::Type::String
            && gameDirectory && gameDirectory->type == Json::Type::String
            && !executable->second.str.empty() && fs::path(executable->second.str).is_relative())
        {
            executable->second = Json::makeString((fs::path(gameDirectory->str) / executable->second.str).string());
        }
        if (!path(request, "executable")) return false;
    }
    return true;
}

// --- operation journal ----------------------------------------------------
// Append-only JSONL under <workspace>/.sed-mcp/journal.jsonl. The last
// record for an operationId wins. A write first records "started";
// only after the agent call returns is it "completed" or "failed".
// A "started" record left behind means the outcome is uncertain: the
// write is never silently retried under that operationId.

fs::path journalPath() { return g_workspace / ".sed-mcp" / "journal.jsonl"; }

class JournalError final : public std::runtime_error
{
public:
    explicit JournalError(const std::string& message) : std::runtime_error(message) {}
};

fs::file_status journalStatus(const fs::path& path)
{
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if ( error && error != std::errc::no_such_file_or_directory )
    {
        throw JournalError("Cannot inspect the operation journal: " + error.message());
    }
    return status;
}

// Internal journal paths must be ordinary files/directories, not aliases.
// In particular, weakly_canonical alone does not resolve a dangling symlink:
// opening that leaf for append could create its target outside the workspace.
bool journalExists()
{
    const auto parent = journalStatus(journalPath().parent_path());
    if ( !fs::exists(parent) )
    {
        return false;
    }
    if ( fs::is_symlink(parent) || !fs::is_directory(parent) )
    {
        throw JournalError("The operation journal parent must be a real directory");
    }
    const auto file = journalStatus(journalPath());
    if ( fs::is_symlink(file) || (fs::exists(file) && !fs::is_regular_file(file)) )
    {
        throw JournalError("The operation journal must be a regular file, not a symlink or special file");
    }
    if ( !isWithin(journalPath(), g_workspace) )
    {
        throw JournalError("The operation journal is outside the workspace");
    }
    return fs::exists(file);
}

struct JournalEntry
{
    std::string operationId;
    std::string tool;
    std::string state; // started | completed | failed
    std::string fingerprint;
    Json result; // journaled result payload for completed writes
    bool found = false;
};

std::string fingerprintOf(const std::string& tool, const Json& arguments)
{
    // The fingerprint binds the intent (tool + arguments) but not the
    // operationId itself, so an interrupted caller re-issuing the same
    // call under the same operationId matches its journal record.
    Json intent = arguments;
    intent.object.erase("operationId");
    return sed_agent::sha256Hex(tool + "\n" + sed_agent::toJsonString(intent));
}

JournalEntry lookupOperation(const std::string& operationId)
{
    JournalEntry latest;
    if ( !journalExists() )
    {
        return latest;
    }
    std::ifstream in(journalPath(), std::ios::binary);
    if ( !in )
    {
        throw JournalError("Cannot open the operation journal for reading");
    }
    std::string line;
    while ( std::getline(in, line) )
    {
        if ( in.eof() )
        {
            throw JournalError("The operation journal has an unterminated record; its history must be recovered before another write");
        }
        if ( line.empty() )
        {
            continue;
        }
        Json row;
        try
        {
            row = sed_agent::parseJson(line);
        }
        catch ( const std::runtime_error& )
        {
            throw JournalError("The operation journal contains malformed JSON; its history must be recovered before another write");
        }
        const auto field = [&](const char* name) -> const std::string&
        {
            const Json* value = row.find(name);
            if ( !value || value->type != Json::Type::String || value->str.empty() )
            {
                throw JournalError("The operation journal contains an incomplete record");
            }
            return value->str;
        };
        const std::string& id = field("operationId");
        const std::string& tool = field("tool");
        const std::string& state = field("state");
        const std::string& fingerprint = field("fingerprint");
        const Json* result = row.find("result");
        if ( state != "started" && state != "completed" && state != "failed" )
        {
            throw JournalError("The operation journal contains an unknown state");
        }
        if ( state != "started" )
        {
            const Json* ok = result ? result->find("ok") : nullptr;
            if ( !result || !result->isObject() || !ok || ok->type != Json::Type::Bool
                || ok->boolean != (state == "completed") )
            {
                throw JournalError("The operation journal is missing a consistent command result");
            }
        }
        if ( id != operationId )
        {
            continue;
        }
        latest.found = true;
        latest.operationId = operationId;
        latest.tool = tool;
        latest.state = state;
        latest.fingerprint = fingerprint;
        latest.result = result ? *result : Json{};
    }
    if ( in.bad() || (in.fail() && !in.eof()) )
    {
        throw JournalError("Cannot finish reading the operation journal");
    }
    return latest;
}

void appendJournal(const std::string& operationId, const std::string& tool,
                   const std::string& state, const std::string& fingerprint,
                   const Json* result)
{
    (void)journalExists();
    std::error_code ec;
    fs::create_directories(journalPath().parent_path(), ec);
    if ( ec )
    {
        throw JournalError("Cannot create the operation journal directory: " + ec.message());
    }
    (void)journalExists();
    Json row = Json::makeObject();
    row.set("operationId", Json::makeString(operationId));
    row.set("tool", Json::makeString(tool));
    row.set("state", Json::makeString(state));
    row.set("fingerprint", Json::makeString(fingerprint));
    if (result) row.set("result", *result);
    std::ofstream out(journalPath(), std::ios::binary | std::ios::app);
    if ( !out )
    {
        throw JournalError("Cannot open the operation journal for append");
    }
    out << sed_agent::toJsonString(row) << '\n';
    out.flush();
    if ( !out )
    {
        throw JournalError("Cannot write and flush the operation journal");
    }
    out.close();
    if ( !out )
    {
        throw JournalError("Cannot close the operation journal after append");
    }
}

Json journalFailure(const std::string& operationId, const JournalError& error, const Json* commandResult = nullptr)
{
    Json output = policyErrorPayload(commandResult ? "journalOutcomeUncertain" : "journalUnavailable",
        commandResult ? "The command returned, but its journal completion could not be recorded: " + std::string(error.what())
            : "The operation journal is unavailable; this request did not execute the command: " + std::string(error.what()));
    output.set("operationId", Json::makeString(operationId));
    output.set("commandExecuted", Json::makeBool(commandResult != nullptr));
    output.set("journaled", Json::makeBool(false));
    output.set("state", Json::makeString(commandResult ? "uncertain" : "unavailable"));
    output.set("outcome", Json::makeString("uncertain"));
    if ( commandResult )
    {
        output.set("commandResult", *commandResult);
    }
    return output;
}

// --- request scratch files -------------------------------------------------
// Agent commands read request documents from a path (or stdin, which the
// MCP transport owns). Tool `request` objects are therefore staged in a
// scratch file under the system temp directory and removed after the call.
// This is transport plumbing only; the staged bytes are the caller's JSON.

struct ScratchFile
{
    fs::path path;
    ScratchFile() = default;
    ~ScratchFile() { clear(); }
    ScratchFile(const ScratchFile&) = delete;
    ScratchFile& operator=(const ScratchFile&) = delete;
    ScratchFile(ScratchFile&& other) noexcept : path(std::move(other.path)) { other.path.clear(); }
    ScratchFile& operator=(ScratchFile&& other) noexcept
    {
        if (this != &other) { clear(); path = std::move(other.path); other.path.clear(); }
        return *this;
    }
    void clear() { if (!path.empty()) { std::error_code ec; fs::remove(path, ec); path.clear(); } }
};

ScratchFile stageRequest(const Json& request)
{
    ScratchFile scratch;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    scratch.path = fs::temp_directory_path()
        / ("sed-mcp-req-" + std::to_string(static_cast<long long>(stamp))
           + "-" + std::to_string(++g_tempCounter) + ".json");
    std::ofstream out(scratch.path, std::ios::binary | std::ios::trunc);
    out << sed_agent::toJsonString(request);
    return scratch;
}

const Json* findArg(const Json& args, const char* key)
{
    return args.isObject() ? args.find(key) : nullptr;
}

bool requiredPathArg(const Json& args, const char* key, std::string& out)
{
    const Json* v = findArg(args, key);
    if (!v || v->type != Json::Type::String || v->str.empty()) return false;
    out = v->str;
    return true;
}

Json missingArg(const char* tool, const char* key)
{
    return policyErrorPayload("invalidArguments",
        std::string(tool) + " requires string argument '" + key + "'");
}

Json runAgent(const std::string& command, const std::vector<std::string>& args, bool& isError,
    std::size_t nativeReceiptLimit = sed_agent::kMaxRequestBytes)
{
    try
    {
        sed_agent::CommandOptions options;
        options.nativeReceiptLimit = nativeReceiptLimit;
        options.resourceReadPolicy = [](const fs::path& resolved)
        {
            Json error;
            if (!checkReadable(resolved.string(), error))
                throw sed_agent::AgentError{ sed_agent::kExitRefusal,
                    error.object.at("error").str, "outsideWorkspace" };
        };
        Json result = sed_agent::runCommand(command, args, options);
        isError = false;
        return result;
    }
    catch (const sed_agent::AgentError& e)
    {
        isError = true;
        return agentErrorPayload(e);
    }
}

// Runs a mutating agent command under the operation journal. With an
// operationId, a completed journal record is replayed from the journal
// (the write is NOT executed again); a leftover "started" record is
// reported as uncertain; a fingerprint mismatch is a conflict.
Json runWrite(const std::string& tool, const Json& arguments,
              const std::string& command, const std::vector<std::string>& args,
              bool& isError, std::size_t nativeReceiptLimit = sed_agent::kMaxRequestBytes)
{
    std::string operationId;
    if (const Json* v = findArg(arguments, "operationId"))
    {
        if (v->type != Json::Type::String || v->str.empty())
        {
            isError = true;
            return policyErrorPayload("invalidArguments", tool + " operationId must be a non-empty string");
        }
        operationId = v->str;
    }
    if (operationId.empty())
    {
        Json result = runAgent(command, args, isError, nativeReceiptLimit);
        result.set("operationId", Json{});
        return result;
    }
    const std::string fingerprint = fingerprintOf(tool, arguments);
    JournalEntry prior;
    try
    {
        prior = lookupOperation(operationId);
    }
    catch ( const JournalError& error )
    {
        isError = true;
        return journalFailure(operationId, error);
    }
    if (prior.found)
    {
        if (prior.fingerprint != fingerprint)
        {
            isError = true;
            Json out = policyErrorPayload("operationIdConflict",
                "operationId '" + operationId + "' was already used for a different request");
            out.set("operationId", Json::makeString(operationId));
            return out;
        }
        if (prior.state == "completed")
        {
            isError = false;
            Json out = prior.result;
            out.set("operationId", Json::makeString(operationId));
            out.set("journaled", Json::makeBool(true));
            return out;
        }
        // started (uncertain) or failed: never silently retry the write.
        isError = true;
        Json out = Json::makeObject();
        out.set("ok", Json::makeBool(false));
        out.set("operationId", Json::makeString(operationId));
        out.set("state", Json::makeString(prior.state));
        out.set("outcome", Json::makeString("uncertain"));
        out.set("error", Json::makeString(
            "operation '" + operationId + "' has no completed journal record; "
            "query sed_outcome instead of replaying the write"));
        return out;
    }
    try
    {
        appendJournal(operationId, tool, "started", fingerprint, nullptr);
    }
    catch ( const JournalError& error )
    {
        isError = true;
        return journalFailure(operationId, error);
    }
    Json result = runAgent(command, args, isError, nativeReceiptLimit);
    result.set("operationId", Json::makeString(operationId));
    try
    {
        appendJournal(operationId, tool, isError ? "failed" : "completed", fingerprint, &result);
    }
    catch ( const JournalError& error )
    {
        isError = true;
        return journalFailure(operationId, error, &result);
    }
    return result;
}

Json outcomeTool(const Json& arguments, bool& isError)
{
    std::string operationId;
    if (!requiredPathArg(arguments, "operationId", operationId))
    {
        isError = true;
        return missingArg("sed_outcome", "operationId");
    }
    isError = false;
    JournalEntry entry;
    try
    {
        entry = lookupOperation(operationId);
    }
    catch ( const JournalError& error )
    {
        isError = true;
        return journalFailure(operationId, error);
    }
    Json out = Json::makeObject();
    out.set("operationId", Json::makeString(operationId));
    if (!entry.found)
    {
        out.set("ok", Json::makeBool(true));
        out.set("state", Json::makeString("unknown"));
        out.set("outcome", Json::makeString("unknown"));
        return out;
    }
    out.set("ok", Json::makeBool(true));
    out.set("tool", Json::makeString(entry.tool));
    out.set("state", Json::makeString(entry.state));
    out.set("outcome", Json::makeString(
        entry.state == "completed" ? "completed"
        : entry.state == "failed" ? "failed" : "uncertain"));
    if (entry.state == "completed") out.set("result", entry.result);
    return out;
}

// --- tool dispatch ----------------------------------------------------------
// Each branch only checks file policy, stages the caller's request JSON,
// and forwards to the identical sed-agent command. Read tools never touch
// the journal; write tools go through runWrite (journal + no-overwrite is
// enforced by the agent layer's create-new apply).

bool stageRequestArg(const Json& arguments, ScratchFile& scratch, bool& staged)
{
    staged = false;
    const Json* request = findArg(arguments, "request");
    if (!request) return true;
    if (!request->isObject()) return false;
    scratch = stageRequest(*request);
    staged = true;
    return true;
}

std::optional<std::string> levelToolPath(const std::string& name, const Json& arguments,
    const char* key, bool writable, Json& error)
{
    std::string raw;
    if ( !requiredPathArg(arguments, key, raw) || !sed_agent::isUtf8PathText(raw) )
    {
        error = policyErrorPayload("invalidArguments",
            name + " requires a non-empty UTF-8 filesystem path without NUL bytes in '" + key + "'");
        return std::nullopt;
    }
    const auto resolved = resolveAgainstWorkspace(sed_agent::pathFromUtf8(raw));
    const bool admitted = writable ? checkWritablePath(resolved, raw, error) : checkReadablePath(resolved, raw, error);
    if ( !admitted ) return std::nullopt;
    return sed_agent::pathToUtf8(resolved);
}

Json createLevelTool(const Json& arguments, std::vector<std::string> args, bool& isError)
{
    ScratchFile scratch;
    std::string kind;
    if ( requiredPathArg(arguments, "kind", kind) )
    {
        if ( !sed_agent::isNewLevelKind(kind) )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", "sed_new kind must be jk, jkdf2, mots or droidworks");
        }
        args.push_back(kind);
    }
    else
    {
        bool staged{};
        if ( !stageRequestArg(arguments, scratch, staged) )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", "sed_new request must be an object");
        }
        if ( staged )
        {
            args.push_back(sed_agent::pathToUtf8(scratch.path));
        }
    }
    return runWrite("sed_new", arguments, "new", args, isError);
}

Json callLevelTool(const std::string& name, const Json& arguments, bool& isError)
{
    if ( name == "sed_map_requirements"
        && (!arguments.isObject() || arguments.object.size() != 2 || !arguments.find("path") || !arguments.find("request")) )
    {
        isError = true;
        return policyErrorPayload("invalidArguments", "sed_map_requirements requires only path and request");
    }
    const bool create = name == "sed_new";
    const bool apply = name == "sed_apply";
    Json policyError;
    const auto path = levelToolPath(name, arguments, create ? "output" : "path", create, policyError);
    if ( !path )
    {
        isError = true;
        return policyError;
    }
    std::vector<std::string> args{ *path };
    if ( name == "sed_inspect" || name == "sed_discover" ) return runAgent("inspect", args, isError);
    const auto* id = findArg(arguments, "operationId");
    if ( (create || apply) && id && id->type == Json::Type::String && !sed_agent::isValidJsonUtf8(id->str) )
    {
        isError = true;
        return policyErrorPayload("invalidArguments", name + " operationId must be a UTF-8 string");
    }
    if ( create ) return createLevelTool(arguments, std::move(args), isError);

    std::optional<std::string> output;
    if ( apply )
    {
        output = levelToolPath(name, arguments, "output", true, policyError);
        if ( !output )
        {
            isError = true;
            return policyError;
        }
    }
    Json stagedArguments = arguments;
    if ( !normalizeRequestPaths(name, stagedArguments, policyError) )
    {
        isError = true;
        return policyError;
    }
    ScratchFile scratch;
    bool staged{};
    if ( !stageRequestArg(stagedArguments, scratch, staged) || (!staged && name != "sed_diagnose") )
    {
        isError = true;
        return policyErrorPayload("invalidArguments", name + " requires a 'request' object");
    }
    if ( staged ) args.push_back(sed_agent::pathToUtf8(scratch.path));
    if ( name == "sed_map_requirements" )
    {
        return runAgent("map-requirements", args, isError);
    }
    if ( !output ) return runAgent(name == "sed_diagnose" ? "validate" : name.substr(4), args, isError);

    args.push_back(*output);
    std::string expected;
    if ( requiredPathArg(arguments, "expectedOutputSha256", expected) ) args.push_back(expected);
    return runWrite(name, arguments, "apply", args, isError);
}

Json callNativeModelTool(const std::string& name, const Json& arguments, bool& isError)
{
    std::string path, output, expected;
    Json policyError;
    const bool inspect = name == "sed_model_native_inspect";
    const bool apply = name == "sed_model_native_apply";
    for ( const auto& [key, value] : arguments.object )
    {
        if ( key == "path" || (inspect && key == "query") || (!inspect && key == "request")
            || (apply && (key == "output" || key == "expectedRequestSha256"
                || key == "expectedOutputSha256" || key == "operationId")) )
        {
            continue;
        }
        isError = true;
        return policyErrorPayload("invalidArguments", name + " contains an unknown argument");
    }
    if ( !requiredPathArg(arguments, "path", path) || !sed_agent::isUtf8PathText(path) )
    {
        isError = true;
        return missingArg(name.c_str(), "path");
    }
    const auto source = resolveAgainstWorkspace(sed_agent::pathFromUtf8(path));
    if ( !checkReadablePath(source, path, policyError) )
    {
        isError = true;
        return policyError;
    }
    std::vector<std::string> args{ sed_agent::pathToUtf8(source) };
    ScratchFile scratch;
    if ( inspect )
    {
        if ( const auto* query = findArg(arguments, "query") )
        {
            if ( !query->isObject() )
            {
                isError = true;
                return policyErrorPayload("invalidArguments", "query must be an object");
            }
            scratch = stageRequest(*query);
            args.push_back(sed_agent::pathToUtf8(scratch.path));
        }
        return runAgent("model-native-inspect", args, isError);
    }
    bool staged{};
    if ( !stageRequestArg(arguments, scratch, staged) || !staged )
    {
        isError = true;
        return policyErrorPayload("invalidArguments", "request must be an object");
    }
    args.push_back(sed_agent::pathToUtf8(scratch.path));
    if ( !apply )
    {
        return runAgent("model-native-plan", args, isError);
    }
    std::string intent;
    if ( !requiredPathArg(arguments, "expectedRequestSha256", intent)
        || !requiredPathArg(arguments, "expectedOutputSha256", expected)
        || !requiredPathArg(arguments, "output", output) || !sed_agent::isUtf8PathText(output) )
    {
        isError = true;
        return missingArg(name.c_str(), "expectedRequestSha256/expectedOutputSha256/output");
    }
    const auto* id = arguments.find("operationId");
    if ( id && (id->type != Json::Type::String || id->str.empty() || !sed_agent::isValidJsonUtf8(id->str)) )
    {
        isError = true;
        return policyErrorPayload("invalidArguments", "operationId must be a non-empty UTF-8 string");
    }
    const auto destination = resolveAgainstWorkspace(sed_agent::pathFromUtf8(output));
    if ( !checkWritablePath(destination, output, policyError) )
    {
        isError = true;
        return policyError;
    }
    args.insert(args.end(), { intent, expected, sed_agent::pathToUtf8(destination) });
    // Reserve the exact additional keys that a completed receipt or its replay will carry.
    Json additions = Json::makeObject();
    additions.set("operationId", id ? *id : Json{});
    if ( id )
    {
        additions.set("journaled", Json::makeBool(true));
    }
    const auto extra = sed_agent::toJsonString(additions).size() - 1; // Replace braces with a leading comma.
    const auto cap = sed_agent::kMaxRequestBytes;
    return runWrite(name, arguments, "model-native-apply", args, isError, cap - std::min(cap, extra));
}

Json callTool(const std::string& name, const Json& arguments, bool& isError, bool& known)
{
    known = true;
    isError = false;
    std::string path, output, expected;
    Json policyError;
    const bool levelTool = name == "sed_new" || name == "sed_inspect" || name == "sed_query"
        || name == "sed_select" || name == "sed_map_requirements" || name == "sed_diagnose" || name == "sed_plan" || name == "sed_apply"
        || (name == "sed_discover" && findArg(arguments, "path") && !findArg(arguments, "topic"));
    if ( levelTool || name == "sed_model_native_inspect" || name == "sed_model_native_plan" || name == "sed_model_native_apply" )
    {
        try
        {
            return levelTool ? callLevelTool(name, arguments, isError) : callNativeModelTool(name, arguments, isError);
        }
        catch ( const fs::filesystem_error& )
        {
            isError = true;
            return policyErrorPayload("nativePathUnavailable", "Cannot prepare a native filesystem path");
        }
    }
    if ( name == "sed_campaign_check" )
    {
        const auto* request = arguments.find("request");
        if ( arguments.object.size() != 1 || !request || !request->isObject() )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", "sed_campaign_check requires exactly one request object");
        }
        if ( !sed_agent::campaignNumbersAreIntegers(*request) )
        {
            isError = true;
            return agentErrorPayload({sed_agent::kExitUsage, "Campaign numbers must be exact integers", "campaignContractInvalid"});
        }
    }
    if ( name == "sed_dependencies" || name == "sed_cog_links" )
    {
        if ( !arguments.isObject() )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", name + " requires an argument object");
        }
        for ( const auto& [key, value] : arguments.object )
        {
            (void)value;
            if ( key != "path" && key != "request" )
            {
                isError = true;
                return policyErrorPayload("invalidArguments", name + " accepts only path and request arguments");
            }
        }
        if ( !requiredPathArg(arguments, "path", path) )
        {
            isError = true;
            return missingArg(name.c_str(), "path");
        }
        const Json* request = findArg(arguments, "request");
        if ( !request || !request->isObject() )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", name + " requires a 'request' object");
        }
        // Validate original values before path normalization or any capture.
        // The same domain validator also governs direct CLI invocation.
        try
        {
            sed_agent::validateDependencyPath(path);
            if ( name == "sed_cog_links" )
            {
                sed_agent::validateCogLinksRequest(*request);
                const auto source = levelToolPath(name, arguments, "path", false, policyError);
                if ( !source )
                {
                    isError = true;
                    return policyError;
                }
                Json normalized = *request;
                for ( auto& archive : normalized.object.at("resourceArchives").array )
                {
                    const auto resolved = levelToolPath(name, archive, "path", false, policyError);
                    if ( !resolved )
                    {
                        isError = true;
                        return policyError;
                    }
                    archive.set("path", Json::makeString(*resolved));
                }
                ScratchFile scratch = stageRequest(normalized);
                return runAgent("cog-links", { *source, sed_agent::pathToUtf8(scratch.path) }, isError);
            }
            sed_agent::validateDependencyRequest(*request);
        }
        catch ( const sed_agent::AgentError& error )
        {
            isError = true;
            return agentErrorPayload(error);
        }
        catch ( const fs::filesystem_error& )
        {
            isError = true;
            return policyErrorPayload("nativePathUnavailable", "Cannot prepare a native filesystem path");
        }
        if ( !checkReadable(path, policyError) )
        {
            isError = true;
            return policyError;
        }
        Json normalized = arguments;
        if ( !normalizeRequestPaths(name, normalized, policyError) )
        {
            isError = true;
            return policyError;
        }
        ScratchFile scratch = stageRequest(normalized.object.at("request"));
        return runAgent("dependencies", { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }
    if (name == "sed_jo_ja_inspect" || name == "sed_jo_ja_plan" || name == "sed_jo_ja_apply")
    {
        for (const auto& field : arguments.object)
        {
            const auto& key = field.first;
            const bool allowed = name == "sed_jo_ja_inspect" ? (key == "path" || key == "brush" || key == "sideOffset" || key == "target" || key == "sourceVisibility" || key == "sourceLightmap") : key == "request"
                || (name == "sed_jo_ja_apply" && (key == "output" || key == "expectedOutputSha256"
                    || key == "expectedManifestSha256" || key == "operationId"));
            if (!allowed)
            {
                isError = true;
                return policyErrorPayload("invalidArguments", name + " has an unknown argument: " + key);
            }
        }
    }
    Json stagedArguments = arguments;
    if (!normalizeRequestPaths(name, stagedArguments, policyError))
    {
        isError = true;
        return policyError;
    }
    auto needPath = [&]() { return requiredPathArg(arguments, "path", path); };
    auto readOk = [&]() { return checkReadable(path, policyError); };
    auto writeOk = [&](const std::string& raw) { return checkWritable(raw, policyError); };

    if (name == "sed_outcome") return outcomeTool(arguments, isError);

    // Additional domain commands use explicit routes. This deliberately does
    // not expose an arbitrary command/argv tool that could bypass file policy.
    if (name == "sed_statistics" || name == "sed_dxf_inspect" || name == "sed_mat_inspect")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        const std::string command = name == "sed_statistics" ? "statistics"
            : name == "sed_dxf_inspect" ? "dxf-inspect" : "mat-inspect";
        return runAgent(command, { workspaceRelativeArg(path) }, isError);
    }
    if (name == "sed_bindings" || name == "sed_overview" || name == "sed_launch_plan"
        || name == "sed_prefab_capture" || name == "sed_mat_create" || name == "sed_cmp_create" || name == "sed_cmp_master_create" || name == "sed_archive_create"
        || name == "sed_campaign_check" || name == "sed_cog_run" || name == "sed_assistant_check" || name == "sed_model_import" || name == "sed_terrain_3do" || name == "sed_key_plan" || name == "sed_key_apply")
    {
        const bool input = name == "sed_bindings" || name == "sed_overview" || name == "sed_prefab_capture"
            || name == "sed_key_plan" || name == "sed_key_apply";
        const bool outputArg = name == "sed_prefab_capture" || name == "sed_mat_create" || name == "sed_cmp_create"
            || name == "sed_cmp_master_create"
            || name == "sed_archive_create"
            || name == "sed_model_import" || name == "sed_terrain_3do" || name == "sed_key_apply";
        if (input)
        {
            if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
            if (!readOk()) { isError = true; return policyError; }
        }
        if (outputArg)
        {
            if (!requiredPathArg(arguments, "output", output)) { isError = true; return missingArg(name.c_str(), "output"); }
            if (!writeOk(output)) { isError = true; return policyError; }
        }
        if (name == "sed_key_apply" && !requiredPathArg(arguments, "expectedOutputSha256", expected))
        { isError = true; return missingArg(name.c_str(), "expectedOutputSha256"); }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a 'request' object"); }
        std::string command = name.substr(4);
        std::replace(command.begin(), command.end(), '_', '-');
        std::vector<std::string> args;
        if (input) args.push_back(workspaceRelativeArg(path));
        args.push_back(name == "sed_campaign_check" ? sed_agent::pathToUtf8(scratch.path) : scratch.path.string());
        if (name == "sed_key_apply") args.push_back(expected);
        if (outputArg) args.push_back(workspaceRelativeArg(output));
        const Json* request = findArg(stagedArguments, "request");
        const bool writesOverview = name == "sed_overview" && request && request->find("output");
        return outputArg || writesOverview ? runWrite(name, arguments, command, args, isError) : runAgent(command, args, isError);
    }
    if (name == "sed_export_dxf" || name == "sed_dxf_import" || name == "sed_mat_export_png")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!requiredPathArg(arguments, "output", output)) { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk() || !writeOk(output)) { isError = true; return policyError; }
        std::vector<std::string> args{ workspaceRelativeArg(path) };
        if (name == "sed_dxf_import")
        {
            const auto* height = findArg(arguments, "height");
            if (!height || height->type != Json::Type::Number || !std::isfinite(height->number))
            { isError = true; return policyErrorPayload("invalidArguments", "sed_dxf_import requires finite numeric height"); }
            args.push_back(sed_agent::toJsonString(*height));
        }
        args.push_back(workspaceRelativeArg(output));
        if (name == "sed_mat_export_png")
        {
            const auto* cel = findArg(arguments, "cel");
            const auto* palette = findArg(arguments, "palette");
            if (cel && (cel->type != Json::Type::Number || !std::isfinite(cel->number)
                || std::floor(cel->number) != cel->number || cel->number < 0 || cel->number > std::numeric_limits<int>::max()))
            { isError = true; return policyErrorPayload("invalidArguments", "sed_mat_export_png cel must be a nonnegative 32-bit integer"); }
            if (cel || palette) args.push_back(cel ? sed_agent::toJsonString(*cel) : "0");
            if (palette)
            {
                std::string palettePath;
                if (!requiredPathArg(arguments, "palette", palettePath)) { isError = true; return missingArg(name.c_str(), "palette"); }
                if (!checkReadable(palettePath, policyError)) { isError = true; return policyError; }
                args.push_back(workspaceRelativeArg(palettePath));
            }
        }
        return runWrite(name, arguments, name == "sed_export_dxf" ? "export-dxf" : name == "sed_dxf_import" ? "dxf-import" : "mat-export-png", args, isError);
    }
    if (name == "sed_key_inspect" || name == "sed_cog_check" || name == "sed_cog_compile" || name == "sed_native_inspect")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        std::vector<std::string> args{ workspaceRelativeArg(path) };
        if (name == "sed_key_inspect")
        {
            if (findArg(arguments, "model"))
            {
                std::string model;
                if (!requiredPathArg(arguments, "model", model)) { isError = true; return missingArg(name.c_str(), "model"); }
                if (!checkReadable(model, policyError)) { isError = true; return policyError; }
                args.push_back(workspaceRelativeArg(model));
            }
            return runAgent("key-inspect", args, isError);
        }
        std::string game;
        if (!requiredPathArg(arguments, "game", game)) { isError = true; return missingArg(name.c_str(), "game"); }
        args.push_back(game);
        if (name == "sed_cog_check")
        {
            if (const auto* strict = findArg(arguments, "strict"))
            {
                if (strict->type != Json::Type::Bool)
                { isError = true; return policyErrorPayload("invalidArguments", "sed_cog_check strict must be boolean"); }
                if (strict->boolean) args.push_back("--strict");
            }
            return runAgent("cog-check", args, isError);
        }
        if (name == "sed_cog_compile")
        {
            for (const char* option : { "strict", "listing" })
            {
                if (const auto* flag = findArg(arguments, option))
                {
                    if (flag->type != Json::Type::Bool)
                    { isError = true; return policyErrorPayload("invalidArguments", std::string("sed_cog_compile ") + option + " must be boolean"); }
                    if (flag->boolean) args.push_back(std::string("--") + option);
                }
            }
            return runAgent("cog-compile", args, isError);
        }
        std::string format, workerConfig;
        if (!requiredPathArg(arguments, "format", format) || !requiredPathArg(arguments, "workerConfig", workerConfig))
        { isError = true; return missingArg(name.c_str(), "format/workerConfig"); }
        if (!checkReadable(workerConfig, policyError)
            || !checkReadable((resolveAgainstWorkspace(workerConfig).parent_path() / "sed-cpp-bridge.exe").string(), policyError))
        { isError = true; return policyError; }
        args.push_back(format); args.push_back(workspaceRelativeArg(workerConfig));
        return runAgent("native-inspect", args, isError);
    }
    if ( name == "sed_model_lighting_inspect" || name == "sed_model_lighting_plan"
        || name == "sed_model_lighting_apply" )
    {
        const bool inspect = name == "sed_model_lighting_inspect";
        const bool apply = name == "sed_model_lighting_apply";
        for ( const auto& [key, value] : arguments.object )
        {
            (void)value;
            if ( key != "path" && !(key == "request" && !inspect)
                && !(apply && (key == "output" || key == "expectedOutputSha256" || key == "operationId")) )
            {
                isError = true;
                return policyErrorPayload("invalidArguments", name + ": unknown argument '" + key + "'");
            }
        }
        if ( !needPath() )
        {
            isError = true;
            return missingArg(name.c_str(), "path");
        }
        if ( !readOk() )
        {
            isError = true;
            return policyError;
        }
        if ( inspect )
        {
            return runAgent("model-lighting-inspect", { workspaceRelativeArg(path) }, isError);
        }
        if ( apply )
        {
            if ( !requiredPathArg(arguments, "output", output)
                || !requiredPathArg(arguments, "expectedOutputSha256", expected) )
            {
                isError = true;
                return missingArg(name.c_str(), "output/expectedOutputSha256");
            }
            if ( !writeOk(output) )
            {
                isError = true;
                return policyError;
            }
        }
        ScratchFile scratch;
        bool staged = false;
        if ( !stageRequestArg(stagedArguments, scratch, staged) || !staged )
        {
            isError = true;
            return policyErrorPayload("invalidArguments", name + " requires a 'request' object");
        }
        if ( apply )
        {
            return runWrite(name, arguments, "model-lighting-apply",
                { workspaceRelativeArg(path), scratch.path.string(), expected, workspaceRelativeArg(output) }, isError);
        }
        return runAgent("model-lighting-plan", { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }

    if (name == "sed_discover" || name == "sed_schema")
    {
        const auto* pathValue = findArg(arguments, "path");
        const auto* topic = findArg(arguments, "topic");
        if (pathValue && (topic || name == "sed_schema"))
        { isError = true; return policyErrorPayload("invalidArguments", name + " accepts either a discovery topic or the legacy inspect path"); }
        std::vector<std::string> args;
        if (topic)
        {
            if (topic->type != Json::Type::String || topic->str.empty())
            { isError = true; return missingArg(name.c_str(), "topic"); }
            args.push_back(topic->str);
        }
        return runAgent("schema", args, isError);
    }
    if (name == "sed_workbench_query" || name == "sed_arch_validate")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a 'request' object"); }
        const std::string command = name == "sed_workbench_query" ? "workbench-query" : "arch-validate";
        return runAgent(command, { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }
    if (name == "sed_jo_ja_inspect")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        std::vector<std::string> args{workspaceRelativeArg(path)};
        const auto* brush = findArg(arguments, "brush");
        const auto* offset = findArg(arguments, "sideOffset");
        const auto* target = findArg(arguments,"target");
        const auto* visibility = findArg(arguments, "sourceVisibility");
        const auto validIndex = [](const Json* v) {
            return v && v->type == Json::Type::Number && std::isfinite(v->number)
                && std::floor(v->number) == v->number && v->number >= 0 && v->number <= std::numeric_limits<int>::max();
        };
        const auto indexArg = [](const Json* v) { return std::to_string(static_cast<int>(v->number)); };
        if ( const auto* lightmap = findArg(arguments, "sourceLightmap") )
        {
            const auto* surface = lightmap->find("surface");
            const auto* steps = lightmap->find("patchSubdivisions");
            if ( visibility || target || brush || offset || !validIndex(surface) ||
                 (steps && (!validIndex(steps) || steps->number < 1 || steps->number > 16)) ||
                 lightmap->object.size() != 1u + (steps ? 1u : 0u) )
            {
                isError = true;
                return policyErrorPayload("invalidArguments", "Choose one lightmap surface and optional patch subdivisions from 1 to 16");
            }
            args.insert(args.end(), {"--lightmap", indexArg(surface)});
            if ( steps )
            {
                args.push_back(indexArg(steps));
            }
        }
        else if ( visibility )
        {
            const auto* viewer = visibility->find("viewCluster");
            const auto* page = visibility->find("offset");
            if ( !visibility->isObject() || target || brush || offset || !viewer || viewer->type != Json::Type::Number ||
                 !std::isfinite(viewer->number) || std::floor(viewer->number) != viewer->number ||
                 viewer->number < -1 || viewer->number > 1048575 || (page && !validIndex(page)) ||
                 visibility->object.size() != 1u + (page ? 1u : 0u) )
            {
                isError = true;
                return policyErrorPayload("invalidArguments", "Choose a source visibility viewCluster and optional offset");
            }
            args.insert(args.end(), {"--visibility", indexArg(viewer)});
            if ( page ) args.push_back(indexArg(page));
        }
        else if (target)
        {
            const auto* level = target->find("level");
            const auto* sector = target->find("sector");
            const auto* subject = target->find("subject");
            const auto* surfaces = target->find("surfaceOffset");
            const auto* faces = target->find("faceOffset");
            if (!target->isObject() || brush || offset || !level || level->type != Json::Type::String
                || !subject || subject->type != Json::Type::String || !validIndex(sector)
                || (surfaces && !validIndex(surfaces)) || (faces && !validIndex(faces))
                || target->object.size() != 3u + (surfaces ? 1u : 0u) + (faces ? 1u : 0u))
            {
                isError=true;
                return policyErrorPayload("invalidArguments", "Choose a target level, sector, subject and optional page offsets");
            }
            args.insert(args.end(),{"--target",level->str,indexArg(sector),subject->str});
            if (surfaces || faces) args.push_back(surfaces ? indexArg(surfaces) : "0");
            if (faces) args.push_back(indexArg(faces));
        }
        else if (brush || offset)
        {
            if (!validIndex(brush) || (offset && !validIndex(offset)))
            {
                isError = true;
                return policyErrorPayload("invalidArguments", "Use a nonnegative brush index and optional sideOffset");
            }
            args.insert(args.end(),{"--brush",indexArg(brush)});
            if (offset) args.push_back(indexArg(offset));
        }
        return runAgent("jo-ja-inspect", args, isError);
    }
    if (name == "sed_jo_ja_plan" || name == "sed_jo_ja_apply")
    {
        const bool apply = name == "sed_jo_ja_apply";
        std::string manifest;
        if (apply)
        {
            if (!requiredPathArg(arguments, "output", output)
                || !requiredPathArg(arguments, "expectedOutputSha256", expected)
                || !requiredPathArg(arguments, "expectedManifestSha256", manifest))
            { isError = true; return missingArg(name.c_str(), "output/expectedOutputSha256/expectedManifestSha256"); }
            if (!writeOk(output)) { isError = true; return policyError; }
        }
        ScratchFile scratch;
        bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a request object"); }
        if (apply) return runWrite(name, arguments, "jo-ja-apply",
            { scratch.path.string(), workspaceRelativeArg(output), expected, manifest }, isError);
        return runAgent("jo-ja-plan", { scratch.path.string() }, isError);
    }
    if (name == "sed_package_plan")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", "sed_package_plan requires a 'request' object"); }
        return runAgent("package-plan", { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }
    if (name == "sed_package_apply")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!requiredPathArg(arguments, "output", output))
        { isError = true; return missingArg(name.c_str(), "output"); }
        std::string expectedManifest;
        if (!requiredPathArg(arguments, "expectedOutputSha256", expected)
            || !requiredPathArg(arguments, "expectedManifestSha256", expectedManifest))
        { isError = true; return missingArg(name.c_str(), "expectedOutputSha256/expectedManifestSha256"); }
        if (!readOk()) { isError = true; return policyError; }
        if (!writeOk(output)) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", "sed_package_apply requires a 'request' object"); }
        return runWrite(name, arguments, "package-apply",
            { workspaceRelativeArg(path), scratch.path.string(),
              workspaceRelativeArg(output), expected, expectedManifest }, isError);
    }
    if (name == "sed_convert")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!requiredPathArg(arguments, "output", output))
        { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk()) { isError = true; return policyError; }
        if (!writeOk(output)) { isError = true; return policyError; }
        std::vector<std::string> args{ workspaceRelativeArg(path), workspaceRelativeArg(output) };
        if (requiredPathArg(arguments, "expectedOutputSha256", expected)) args.push_back(expected);
        return runWrite(name, arguments, "convert", args, isError);
    }
    if (name == "sed_archive")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        std::string entry;
        if (requiredPathArg(arguments, "entry", entry))
        {
            if (!requiredPathArg(arguments, "output", output))
            { isError = true; return missingArg(name.c_str(), "output"); }
            if (!writeOk(output)) { isError = true; return policyError; }
            return runWrite(name, arguments, "extract",
                { workspaceRelativeArg(path), entry, workspaceRelativeArg(output) }, isError);
        }
        return runAgent("archive", { workspaceRelativeArg(path) }, isError);
    }
    if (name == "sed_snapshot")
    {
        // Delegate to the agent's bounded software renderer. Its snapshot
        // contract is deliberately world-surfaces-only, without Things,
        // models, sprites, or dynamic lights.
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!requiredPathArg(arguments, "output", output))
        { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk()) { isError = true; return policyError; }
        if (!writeOk(output)) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(stagedArguments, scratch, staged))
        { isError = true; return policyErrorPayload("invalidArguments", "sed_snapshot request must be an object"); }
        if (!staged) { scratch = stageRequest(Json::makeObject()); }
        return runWrite(name, arguments, "snapshot", { workspaceRelativeArg(path), scratch.path.string(),
                                      workspaceRelativeArg(output) }, isError);
    }
    if (name == "sed_model_hierarchy")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        return runAgent("model-hierarchy", { workspaceRelativeArg(path) }, isError);
    }
    if (name == "sed_key_from_model")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        std::string keyOutput;
        if (!requiredPathArg(arguments, "output", keyOutput)) { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk() || !writeOk(keyOutput)) { isError = true; return policyError; }
        std::vector<std::string> args{ workspaceRelativeArg(path), workspaceRelativeArg(keyOutput) };
        const Json* frames = findArg(arguments, "frames");
        const Json* fps = findArg(arguments, "fps");
        for (const Json* v : { frames, fps })
        {
            if (v && (v->type != Json::Type::Number || !std::isfinite(v->number)))
            { isError = true; return policyErrorPayload("invalidArguments", "sed_key_from_model frames and fps must be numbers"); }
        }
        if (frames || fps) args.push_back(frames ? sed_agent::toJsonString(*frames) : "30");
        if (fps) args.push_back(sed_agent::toJsonString(*fps));
        return runWrite(name, arguments, "key-from-model", args, isError);
    }
    if (name == "sed_model_hierarchy_edit")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        std::string modelOutput;
        if (!requiredPathArg(arguments, "output", modelOutput)) { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk() || !writeOk(modelOutput)) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        Json staging = arguments;
        if (!stageRequestArg(staging, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a 'request' object"); }
        return runWrite(name, arguments, "model-hierarchy-edit",
            { workspaceRelativeArg(path), scratch.path.string(), workspaceRelativeArg(modelOutput) }, isError);
    }
    if (name == "sed_assistant_script")
    {
        const Json* script = findArg(arguments, "script");
        if (!script || script->type != Json::Type::String) { isError = true; return missingArg(name.c_str(), "script"); }
        std::string scriptOutput;
        if (!requiredPathArg(arguments, "output", scriptOutput)) { isError = true; return missingArg(name.c_str(), "output"); }
        if (!writeOk(scriptOutput)) { isError = true; return policyError; }
        return runWrite(name, arguments, "assistant-script", { script->str, workspaceRelativeArg(scriptOutput) }, isError);
    }
    known = false;
    isError = true;
    return policyErrorPayload("unknownTool", "unknown tool '" + name + "'");
}

Json toolCatalog()
{
    auto schema = [](std::initializer_list<const char*> required,
                     std::initializer_list<const char*> properties) {
        Json s = Json::makeObject();
        s.set("type", Json::makeString("object"));
        Json props = Json::makeObject();
        for (const char* p : properties)
        {
            Json prop = Json::makeObject();
            prop.set("type", Json::makeString(
                std::string(p) == "request" ? "object"
                : std::string(p) == "height" ? "number"
                : std::string(p) == "cel" ? "integer"
                : std::string(p) == "strict" || std::string(p) == "listing" ? "boolean" : "string"));
            props.set(p, std::move(prop));
        }
        s.set("properties", std::move(props));
        Json req = Json::makeArray();
        for (const char* r : required) req.array.push_back(Json::makeString(r));
        s.set("required", std::move(req));
        return s;
    };
    auto patchSchema = [&](std::initializer_list<const char*> required,
                           std::initializer_list<const char*> properties)
    {
        Json result = schema(required, properties);
        result.object.at("properties").set("request", sed_agent::patchRequestSchema());
        return result;
    };
    auto tool = [](const char* name, const char* description, Json inputSchema) {
        Json t = Json::makeObject();
        t.set("name", Json::makeString(name));
        t.set("description", Json::makeString(description));
        t.set("inputSchema", std::move(inputSchema));
        return t;
    };
    Json tools = Json::makeArray();
    auto nativeInspect = schema({ "path" }, { "path", "query" });
    auto nativePlan = schema({ "path", "request" }, { "path", "request" });
    auto nativeApply = schema({ "path", "request", "expectedRequestSha256", "expectedOutputSha256", "output" },
        { "path", "request", "expectedRequestSha256", "expectedOutputSha256", "output", "operationId" });
    for ( Json* spec : { &nativeInspect, &nativePlan, &nativeApply } )
    {
        spec->set("additionalProperties", Json::makeBool(false));
        auto& path = spec->object.at("properties").object.at("path");
        path.set("minLength", Json::makeNumber(1));
        path.set("pattern", Json::makeString(R"(^[^\u0000]*$)"));
    }
    nativeInspect.object.at("properties").set("query", sed_agent::modelNativeInspectQuerySchema());
    const auto nativeRequest = sed_agent::modelNativeRequestSchema();
    nativePlan.object.at("properties").set("request", nativeRequest);
    auto& applyFields = nativeApply.object.at("properties");
    applyFields.set("request", nativeRequest);
    const auto& hash = nativeRequest.object.at("properties").object.at("inputSha256");
    applyFields.set("expectedRequestSha256", hash);
    applyFields.set("expectedOutputSha256", hash);
    // A leading dot starts a basename, not an extension. On POSIX a backslash is a filename byte.
    const char* outputPattern = fs::path::preferred_separator == '\\'
        ? R"(^[^\u0000]*[^/\\:\u0000]\.3[dD][oO]$(?![\s\S]))"
        : R"(^[^\u0000]*[^/\u0000]\.3[dD][oO]$(?![\s\S]))";
    applyFields.object.at("output").set("pattern", Json::makeString(outputPattern));
    applyFields.object.at("operationId").set("minLength", Json::makeNumber(1));
    tools.array.push_back(tool("sed_model_native_inspect", "Page source meshes, mesh-local positions or texture-pixel UV rows.", std::move(nativeInspect)));
    tools.array.push_back(tool("sed_model_native_plan", "Review native UV edits or positive uniform mesh transforms and both digests.", std::move(nativePlan)));
    tools.array.push_back(tool("sed_model_native_apply", "Publish a fresh .3do bound to reviewed request and output digests.", std::move(nativeApply)));
    Json modelInspect = schema({ "path" }, { "path" });
    modelInspect.set("additionalProperties", Json::makeBool(false));
    tools.array.push_back(tool("sed_model_lighting_inspect",
        "Inspect every 3DO LOD's stored lighting, normals and modes; returns the exact source hash.", std::move(modelInspect)));
    Json modelPlan = schema({ "path", "request" }, { "path", "request" });
    modelPlan.set("additionalProperties", Json::makeBool(false));
    modelPlan.object.at("properties").set("request", sed_agent::modelLightingRequestSchema());
    tools.array.push_back(tool("sed_model_lighting_plan",
        "Plan a loss-preserving bind-pose model bake; writes nothing and returns outputSha256.", std::move(modelPlan)));
    Json modelApply = schema({ "path", "request", "output", "expectedOutputSha256" },
        { "path", "request", "output", "expectedOutputSha256", "operationId" });
    modelApply.set("additionalProperties", Json::makeBool(false));
    modelApply.object.at("properties").set("request", sed_agent::modelLightingRequestSchema());
    tools.array.push_back(tool("sed_model_lighting_apply",
        "Apply a reviewed model-lighting plan to a fresh .3do; source/output hashes are required. Supports operationId outcome lookup.",
        std::move(modelApply)));
    tools.array.push_back(tool("sed_discover", "Discover command, patch, dependency or package schemas, or inspect using the legacy path-only form.",
        schema({}, { "path", "topic" })));
    tools.array.push_back(tool("sed_query", "Query saved level collections, JK/MotS thingCensus groups, portalRoutes structural closures and progressionRoutes declared abilities, gates and softlocks.",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_select", "Geometry-bound spatial selection (agent select).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_diagnose", "Validate a level document (agent validate). request.strict checks raw JKL framing. request.resourceArchives supplies scripts for binding checks and placed JK/MotS compilation.",
        schema({ "path" }, { "path", "request" })));
    Json requirements = schema({ "path", "request" }, { "path", "request" });
    requirements.set("additionalProperties", Json::makeBool(false));
    requirements.object.at("properties").set("request", sed_agent::mapRequirementsSchema());
    tools.array.push_back(tool("sed_map_requirements", "Check named authored-record counts on a level. "
        "A completed report includes every check; inspect passed for the overall verdict.", std::move(requirements)));
    tools.array.push_back(tool("sed_workbench_query", "Exact workbench query over level collections (agent workbench-query).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_arch_validate", "Validate ARCH bindings against explicitly supplied 3DO models (agent arch-validate).",
        schema({ "path", "request" }, { "path", "request" })));
    Json cogLinks = schema({ "path", "request" }, { "path", "request" });
    cogLinks.set("additionalProperties", Json::makeBool(false));
    cogLinks.object.at("properties").set("path", sed_agent::dependencyPathSchema());
    cogLinks.object.at("properties").set("request", sed_agent::cogLinksRequestSchema());
    tools.array.push_back(tool("sed_cog_links", "Audit the initial references and message declarations of placed COGs across a saved JKL.", std::move(cogLinks)));
    Json dependencies = schema({ "path", "request" }, { "path", "request" });
    dependencies.set("additionalProperties", Json::makeBool(false));
    dependencies.object.at("properties").set("request", sed_agent::dependencyRequestSchema());
    dependencies.object.at("properties").set("path", sed_agent::dependencyPathSchema());
    tools.array.push_back(tool("sed_dependencies",
        "Inspect a bounded dependency graph from raw JKL and ordered hash-bound archives; writes no output or journal and does not prove runtime closure.",
        std::move(dependencies)));
    tools.array.push_back(tool("sed_jo_ja_inspect", "Inspect a Raven RBSP map, source lightmap fidelity, PVS ownership or clip brushes, or target sector and model geometry in a GOB/GOO.",
        sed_agent::joJaInspectSchema()));
    Json joJaPlan = schema({ "request" }, { "request" });
    joJaPlan.set("additionalProperties", Json::makeBool(false));
    joJaPlan.object.at("properties").set("request", sed_agent::joJaRequestSchema());
    tools.array.push_back(tool("sed_jo_ja_plan", "Prepare a JO/JA model scene or hybrid sector import and review the geometry and package hashes.", std::move(joJaPlan)));
    Json joJaApply = schema({ "request", "output", "expectedOutputSha256", "expectedManifestSha256" },
        { "request", "output", "expectedOutputSha256", "expectedManifestSha256", "operationId" });
    joJaApply.set("additionalProperties", Json::makeBool(false));
    joJaApply.object.at("properties").set("request", sed_agent::joJaRequestSchema());
    tools.array.push_back(tool("sed_jo_ja_apply", "Publish a new reviewed JK/MotS scene archive with hash revalidation.", std::move(joJaApply)));
    Json packagePlan = schema({ "path", "request" }, { "path", "request" });
    packagePlan.object.at("properties").set("request", sed_agent::packageRequestSchema());
    tools.array.push_back(tool("sed_package_plan", "Plan a reviewed level package; writes nothing (agent package-plan). (D-03)", std::move(packagePlan)));
    Json packageApply = schema({ "path", "request", "output", "expectedOutputSha256", "expectedManifestSha256" },
        { "path", "request", "output", "expectedOutputSha256", "expectedManifestSha256", "operationId" });
    packageApply.object.at("properties").set("request", sed_agent::packageRequestSchema());
    tools.array.push_back(tool("sed_package_apply", "Build a reviewed level package to a new .gob/.goo after hash revalidation; never overwrites (agent package-apply). (D-03)", std::move(packageApply)));
    tools.array.push_back(tool("sed_plan", "Plan a patch; writes nothing, returns outputSha256 (agent plan).",
        patchSchema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_apply", "Apply a reviewed patch to a new file; never overwrites (agent apply).",
        patchSchema({ "path", "request", "output" }, { "path", "request", "output", "expectedOutputSha256", "operationId" })));
    tools.array.push_back(tool("sed_new", "Create a new level scaffold at a new path (agent new).",
        schema({ "output" }, { "output", "kind", "request", "operationId" })));
    tools.array.push_back(tool("sed_convert", "Convert a level to a new .jkl/.ndy/.sed/.jed file (agent convert).",
        schema({ "path", "output" }, { "path", "output", "expectedOutputSha256", "operationId" })));
    tools.array.push_back(tool("sed_archive", "List a GOB/GOO archive, or extract one exact entry to a new file (agent archive/extract).",
        schema({ "path" }, { "path", "entry", "output", "operationId" })));
    auto snapshot = schema({ "path", "output" }, { "path", "request", "output", "operationId" });
    snapshot.object.at("properties").set("request", sed_agent::snapshotRequestSchema());
    tools.array.push_back(tool("sed_snapshot", "Render a PNG: world surfaces by default; request.thingMode=static adds bounded LOD0/rest-pose 3DO models and classic cel0 sprites. No runtime lighting or animation.",
        std::move(snapshot)));
    tools.array.push_back(tool("sed_schema", "Discover the inventory or a focused command, patch, dependency, package, MAT creation, model import, archive creation, snapshot, overview, or operation schema.",
        schema({}, { "topic" })));
    tools.array.push_back(tool("sed_inspect", "Inspect a level document (agent inspect).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_statistics", "Compute read-only whole-level statistics (agent statistics).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_bindings", "Inspect one placed COG's bindings using captured resources (agent bindings).",
        schema({ "path", "request" }, { "path", "request" })));
    auto overview = schema({ "path", "request" }, { "path", "request", "operationId" });
    overview.object.at("properties").set("request", sed_agent::overviewRequestSchema());
    tools.array.push_back(tool("sed_overview", "Compute world-surface overview framing; optional request.output publishes a PNG with journal support. request.thingMode=static includes bounded static Thing geometry.",
        std::move(overview)));
    tools.array.push_back(tool("sed_launch_plan", "Prepare a launch plan without starting a process (agent launch-plan).",
        schema({ "request" }, { "request" })));
    tools.array.push_back(tool("sed_native_inspect", "Inspect using the verified adjacent native worker on supported hosts (agent native-inspect).",
        schema({ "path", "game", "format", "workerConfig" }, { "path", "game", "format", "workerConfig" })));
    tools.array.push_back(tool("sed_prefab_capture", "Capture selected sectors into a fresh prefab file (agent prefab-capture).",
        schema({ "path", "request", "output" }, { "path", "request", "output", "operationId" })));
    tools.array.push_back(tool("sed_export_dxf", "Export world faces into a fresh DXF (agent export-dxf).",
        schema({ "path", "output" }, { "path", "output", "operationId" })));
    tools.array.push_back(tool("sed_dxf_inspect", "Inspect a DXF face inventory (agent dxf-inspect).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_dxf_import", "Import DXF outlines at an explicit numeric height into a fresh level (agent dxf-import).",
        schema({ "path", "height", "output" }, { "path", "height", "output", "operationId" })));
    tools.array.push_back(tool("sed_cog_check", "Check a COG against the selected game target (agent cog-check).",
        schema({ "path", "game" }, { "path", "game", "strict" })));
    tools.array.push_back(tool("sed_cog_compile", "Compile a COG the way retail JK or MotS loads it, with diagnostics and an optional listing (agent cog-compile).",
        schema({ "path", "game" }, { "path", "game", "strict", "listing" })));
    tools.array.push_back(tool("sed_model_hierarchy", "List a 3DO's hierarchy as a tree and what the engine would trip over (agent model-hierarchy).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_model_hierarchy_edit", "Rename, reparent, move, add and remove 3DO hierarchy nodes into a fresh 3DO; request {schemaVersion, inputSha256, operations} (agent model-hierarchy-edit).",
        schema({ "path", "request", "output" }, { "path", "request", "output", "operationId" })));
    auto keyFromModel = schema({ "path", "output" }, { "path", "output", "operationId" });
    for (const char* field : { "frames", "fps" })
    {
        Json number = Json::makeObject();
        number.set("type", Json::makeString("number"));
        keyFromModel.object.at("properties").set(field, std::move(number));
    }
    tools.array.push_back(tool("sed_key_from_model", "Start a KEY for a 3DO: a track per hierarchy node at its rest pose, into a fresh file (agent key-from-model).",
        std::move(keyFromModel)));
    tools.array.push_back(tool("sed_assistant_check", "Run a placed assistant COG (door, elevator, switch, objectives, cutscene) in the COG runtime with its kind's scenario and report each expectation and the timeline; request {schemaVersion, level {path, sha256}, kind, cog, script? {path, sha256}, target?, strict?} (agent assistant-check).",
        schema({ "request" }, { "request" })));
    tools.array.push_back(tool("sed_assistant_script", "Write red_goals.cog or red_cutscene.cog, the scripts the objectives and cutscene assistants place, to a fresh file (agent assistant-script).",
        schema({ "script", "output" }, { "script", "output", "operationId" })));
    auto campaign = schema({ "request" }, { "request" });
    campaign.set("additionalProperties", Json::makeBool(false));
    campaign.object.at("properties").set("request", sed_agent::campaignCheckSchema());
    tools.array.push_back(tool("sed_campaign_check", "Check declared state across episode branches and compare "
        "the captured input revisions with a previous audit (agent campaign-check).", std::move(campaign)));

    auto cogRun = schema({ "request" }, { "request" });
    cogRun.object.at("properties").set("request", sed_agent::cogRunRequestSchema());
    tools.array.push_back(tool("sed_cog_run", "Run COGs on a simulated clock against a stand-in world from a level, with timed activate/enter/cross/damage/message events; returns the event log and the things that moved (agent cog-run).",
        std::move(cogRun)));
    tools.array.push_back(tool("sed_mat_inspect", "Inspect stored MAT cel layout (agent mat-inspect).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_mat_export_png", "Export a MAT cel as a fresh PNG; cel and palette are optional (agent mat-export-png).",
        schema({ "path", "output" }, { "path", "output", "cel", "palette", "operationId" })));
    auto matCreate = schema({ "request", "output" }, { "request", "output", "operationId" });
    matCreate.object.at("properties").set("request", sed_agent::matCreateRequestSchema());
    tools.array.push_back(tool("sed_mat_create", "Create an indexed8 MAT with a CMP and optional color key, or an opaque RGB565 MAT, from captured image cels (agent mat-create).",
        std::move(matCreate)));
    auto cmpCreate = schema({ "request", "output" }, { "request", "output", "operationId" });
    cmpCreate.object.at("properties").set("request", sed_agent::cmpCreateRequestSchema());
    tools.array.push_back(tool("sed_cmp_create", "Create a fresh tinted colormap from a captured master CMP and an RGB tint (agent cmp-create).",
        std::move(cmpCreate)));
    auto cmpMasterCreate = schema({ "request", "output" }, { "request", "output", "operationId" });
    cmpMasterCreate.object.at("properties").set("request", sed_agent::cmpMasterCreateRequestSchema());
    tools.array.push_back(tool("sed_cmp_master_create", "Create a fresh level master colormap with new colours in the per-level palette slots (agent cmp-master-create).",
        std::move(cmpMasterCreate)));
    auto archiveCreate = schema({ "request", "output" }, { "request", "output", "operationId" });
    archiveCreate.object.at("properties").set("request", sed_agent::archiveCreateRequestSchema());
    tools.array.push_back(tool("sed_archive_create", "Create a fresh GOB from captured loose entries (agent archive-create).",
        std::move(archiveCreate)));
    auto modelImport = schema({ "request", "output" }, { "request", "output", "operationId" });
    modelImport.object.at("properties").set("request", sed_agent::modelImportRequestSchema());
    tools.array.push_back(tool("sed_model_import", "Import an explicitly captured source into a fresh 3DO (agent model-import).",
        std::move(modelImport)));
    tools.array.push_back(tool("sed_terrain_3do", "Build an explicit heightmap as a fresh 3DO (agent terrain-3do).",
        schema({ "request", "output" }, { "request", "output", "operationId" })));
    tools.array.push_back(tool("sed_key_inspect", "Inspect a KEY timeline, optionally against an explicit 3DO (agent key-inspect).",
        schema({ "path" }, { "path", "model" })));
    tools.array.push_back(tool("sed_key_plan", "Plan KEY edits without writing output (agent key-plan).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_key_apply", "Publish reviewed KEY edits to a fresh output (agent key-apply).",
        schema({ "path", "request", "expectedOutputSha256", "output" }, { "path", "request", "expectedOutputSha256", "output", "operationId" })));
    tools.array.push_back(tool("sed_outcome", "Look up the journaled outcome of an operationId after an interrupted or uncertain write. Never replays the write.",
        schema({ "operationId" }, { "operationId" })));
    return tools;
}

// --- JSON-RPC over stdio -----------------------------------------------------

void writeMessage(const Json& message)
{
    std::cout << sed_agent::toJsonString(message) << "\n";
    std::cout.flush();
}

Json rpcResult(const Json& id, Json result)
{
    Json out = Json::makeObject();
    out.set("jsonrpc", Json::makeString("2.0"));
    out.set("id", id);
    out.set("result", std::move(result));
    return out;
}

Json rpcError(const Json& id, double code, const std::string& message)
{
    Json error = Json::makeObject();
    error.set("code", Json::makeNumber(code));
    error.set("message", Json::makeString(message));
    Json out = Json::makeObject();
    out.set("jsonrpc", Json::makeString("2.0"));
    out.set("id", id);
    out.set("error", std::move(error));
    return out;
}

bool handleMessage(const Json& message)
{
    if (!message.isObject()) return true;
    const Json* methodJson = message.find("method");
    if (!methodJson || methodJson->type != Json::Type::String) return true;
    const std::string method = methodJson->str;
    const bool isNotification = message.find("id") == nullptr;
    if (method.rfind("notifications/", 0) == 0) return true; // never answered
    Json id;
    if (const Json* v = message.find("id")) id = *v;

    if (method == "initialize")
    {
        Json result = Json::makeObject();
        result.set("protocolVersion", Json::makeString("2024-11-05"));
        Json capabilities = Json::makeObject();
        capabilities.set("tools", Json::makeObject());
        result.set("capabilities", std::move(capabilities));
        Json serverInfo = Json::makeObject();
        serverInfo.set("name", Json::makeString("sed-mcp"));
        serverInfo.set("version", Json::makeString("0.1.0"));
        result.set("serverInfo", std::move(serverInfo));
        writeMessage(rpcResult(id, std::move(result)));
        return true;
    }
    if (method == "ping")
    {
        if (!isNotification) writeMessage(rpcResult(id, Json::makeObject()));
        return true;
    }
    if (method == "tools/list")
    {
        Json result = Json::makeObject();
        result.set("tools", toolCatalog());
        writeMessage(rpcResult(id, std::move(result)));
        return true;
    }
    if (method == "tools/call")
    {
        const Json* params = message.find("params");
        const Json* nameJson = params ? params->find("name") : nullptr;
        if (!nameJson || nameJson->type != Json::Type::String)
        {
            writeMessage(rpcError(id, -32602, "tools/call requires a string 'name'"));
            return true;
        }
        Json arguments = Json::makeObject();
        if (params)
            if (const Json* a = params->find("arguments")) arguments = *a;
        if (!arguments.isObject())
        {
            writeMessage(rpcError(id, -32602, "tools/call 'arguments' must be an object"));
            return true;
        }
        bool isError = false, known = true;
        Json payload = callTool(nameJson->str, arguments, isError, known);
        if (!known)
        {
            writeMessage(rpcError(id, -32602, "unknown tool '" + nameJson->str + "'"));
            return true;
        }
        writeMessage(rpcResult(id, textResult(std::move(payload), isError)));
        return true;
    }
    if (!isNotification)
        writeMessage(rpcError(id, -32601, "method not found: " + method));
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    g_workspace = fs::current_path();
#ifdef _WIN32
    (void)argc;
    (void)argv;
    const auto wide = sed_agent::windowsArguments();
    if ( !wide )
    {
        std::cerr << "sed-mcp: cannot decode Windows command line\n";
        return 2;
    }
    const std::vector<fs::path> args(wide->begin() + 1, wide->end());
#else
    const std::vector<fs::path> args(argv + 1, argv + argc);
#endif
    for ( std::size_t i = 0; i < args.size(); ++i )
    {
        const auto& arg = args[i];
        if ( arg == "--workspace" || arg == "--resource-root" )
        {
            if ( ++i == args.size() )
            {
                std::cerr << "sed-mcp: " << sed_agent::pathForDiagnostic(arg) << " requires a value\n";
                return 2;
            }
            if ( arg == "--workspace" )
            {
                g_workspace = args[i];
            }
            else
            {
                g_resourceRoots.push_back(args[i]);
            }
        }
        else if (arg == "help" || arg == "--help" || arg == "-h")
        {
            Json out = Json::makeObject();
            out.set("ok", Json::makeBool(true));
            out.set("protocol", Json::makeString("mcp"));
            out.set("transport", Json::makeString("stdio JSON-RPC, newline-delimited"));
            out.set("usage", Json::makeString(
                "sed-mcp [--workspace <dir>] [--resource-root <dir>]..."));
            out.set("note", Json::makeString(
                "Thin MCP adapter over sed-agent/sed_core; no domain logic in this binary."));
            std::cout << sed_agent::toJsonString(out) << "\n";
            return 0;
        }
        else
        {
            std::cerr << "sed-mcp: unknown argument '" << sed_agent::pathForDiagnostic(arg) << "'\n";
            return 2;
        }
    }
    std::error_code ec;
    fs::create_directories(g_workspace, ec);
    g_workspace = normalizedAbsolute(g_workspace);
    for (auto& root : g_resourceRoots) root = normalizedAbsolute(root);

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.size() > kMaxLineBytes)
        {
            writeMessage(rpcError(Json{}, -32600, "request exceeds the 1 MiB line cap"));
            continue;
        }
        if (line.empty()) continue;
        Json message;
        try { message = sed_agent::parseJson(line); }
        catch (const std::exception& e)
        {
            writeMessage(rpcError(Json{}, -32700, std::string("parse error: ") + e.what()));
            continue;
        }
        handleMessage(message);
    }
    return 0;
}




