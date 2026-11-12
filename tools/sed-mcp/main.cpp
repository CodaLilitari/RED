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
#include "agent_model_lighting.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sed_agent::Json;

namespace
{

constexpr std::size_t kMaxLineBytes = 1024 * 1024; // mirrors the agent cap

fs::path g_workspace;
std::vector<fs::path> g_resourceRoots;
int g_tempCounter = 0;

#ifdef _WIN32
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
#endif

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
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(false));
    out.set("exitCode", Json::makeNumber(static_cast<double>(e.code)));
    out.set("error", Json::makeString(e.message));
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

bool isWithin(const fs::path& path, const fs::path& root)
{
    // Resolve existing parents before admitting a path. A lexical child can
    // otherwise follow a symlink outside the configured workspace. Missing
    // tails remain valid for create-only output paths.
    std::error_code error;
    const fs::path resolvedPath = fs::weakly_canonical(normalizedAbsolute(path), error);
    if ( error )
    {
        return false;
    }
    const fs::path resolvedRoot = fs::weakly_canonical(normalizedAbsolute(root), error);
    if ( error )
    {
        return false;
    }
    auto candidate = resolvedPath.begin();
    for ( const auto& component : resolvedRoot )
    {
        if ( candidate == resolvedPath.end() )
        {
            return false;
        }
#ifdef _WIN32
        if ( lower(candidate->string()) != lower(component.string()) )
#else
        if ( *candidate != component )
#endif
        {
            return false;
        }
        ++candidate;
    }
    return true;
}

fs::path resolveAgainstWorkspace(const std::string& raw)
{
    const fs::path p(raw);
    if (p.is_absolute()) return normalizedAbsolute(p);
    return normalizedAbsolute(g_workspace / p);
}

bool checkReadable(const std::string& raw, Json& errorOut)
{
    const fs::path resolved = resolveAgainstWorkspace(raw);
    if (isWithin(resolved, g_workspace)) return true;
    for (const auto& root : g_resourceRoots)
        if (isWithin(resolved, root)) return true;
    errorOut = policyErrorPayload("outsideWorkspace",
        "path is outside the workspace and resource roots: " + raw);
    return false;
}

bool checkWritable(const std::string& raw, Json& errorOut)
{
    const fs::path resolved = resolveAgainstWorkspace(raw);
    if (isWithin(resolved, g_workspace)) return true;
    errorOut = policyErrorPayload("outsideWorkspace",
        "write path is outside the workspace: " + raw);
    return false;
}

std::string workspaceRelativeArg(const std::string& raw)
{
    // The agent layer resolves paths as given; hand it the absolute
    // in-workspace path so behaviour does not depend on our cwd.
    return resolveAgainstWorkspace(raw).string();
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

Json runAgent(const std::string& command, const std::vector<std::string>& args, bool& isError)
{
    try
    {
        Json result = sed_agent::runCommand(command, args);
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
              bool& isError)
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
        Json result = runAgent(command, args, isError);
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
    Json result = runAgent(command, args, isError);
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

Json callTool(const std::string& name, const Json& arguments, bool& isError, bool& known)
{
    known = true;
    isError = false;
    std::string path, output, expected;
    Json policyError;
    auto needPath = [&]() { return requiredPathArg(arguments, "path", path); };
    auto readOk = [&]() { return checkReadable(path, policyError); };
    auto writeOk = [&](const std::string& raw) { return checkWritable(raw, policyError); };

    if (name == "sed_outcome") return outcomeTool(arguments, isError);

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
        if ( !stageRequestArg(arguments, scratch, staged) || !staged )
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

    if (name == "sed_discover")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        return runAgent("inspect", { workspaceRelativeArg(path) }, isError);
    }
    if (name == "sed_query" || name == "sed_select" || name == "sed_diagnose")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(arguments, scratch, staged))
        { isError = true; return policyErrorPayload("invalidArguments", name + " request must be an object"); }
        const std::string command = name == "sed_query" ? "query" : name == "sed_select" ? "select" : "validate";
        std::vector<std::string> args{ workspaceRelativeArg(path) };
        if (staged) args.push_back(scratch.path.string());
        else if (name != "sed_diagnose")
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a 'request' object"); }
        return runAgent(command, args, isError);
    }
    if (name == "sed_workbench_query" || name == "sed_arch_validate")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(arguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", name + " requires a 'request' object"); }
        const std::string command = name == "sed_workbench_query" ? "workbench-query" : "arch-validate";
        return runAgent(command, { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }
    if (name == "sed_package_plan")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(arguments, scratch, staged) || !staged)
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
        if (!stageRequestArg(arguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", "sed_package_apply requires a 'request' object"); }
        return runWrite(name, arguments, "package-apply",
            { workspaceRelativeArg(path), scratch.path.string(),
              workspaceRelativeArg(output), expected, expectedManifest }, isError);
    }
    if (name == "sed_plan")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!readOk()) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(arguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", "sed_plan requires a 'request' object"); }
        return runAgent("plan", { workspaceRelativeArg(path), scratch.path.string() }, isError);
    }
    if (name == "sed_apply")
    {
        if (!needPath()) { isError = true; return missingArg(name.c_str(), "path"); }
        if (!requiredPathArg(arguments, "output", output))
        { isError = true; return missingArg(name.c_str(), "output"); }
        if (!readOk()) { isError = true; return policyError; }
        if (!writeOk(output)) { isError = true; return policyError; }
        ScratchFile scratch; bool staged = false;
        if (!stageRequestArg(arguments, scratch, staged) || !staged)
        { isError = true; return policyErrorPayload("invalidArguments", "sed_apply requires a 'request' object"); }
        std::vector<std::string> args{ workspaceRelativeArg(path), scratch.path.string(),
                                       workspaceRelativeArg(output) };
        if (requiredPathArg(arguments, "expectedOutputSha256", expected)) args.push_back(expected);
        return runWrite(name, arguments, "apply", args, isError);
    }
    if (name == "sed_new")
    {
        if (!requiredPathArg(arguments, "output", output))
        { isError = true; return missingArg(name.c_str(), "output"); }
        if (!writeOk(output)) { isError = true; return policyError; }
        std::vector<std::string> args{ workspaceRelativeArg(output) };
        std::string kind;
        if (requiredPathArg(arguments, "kind", kind)) args.push_back(kind);
        else
        {
            ScratchFile scratch; bool staged = false;
            if (!stageRequestArg(arguments, scratch, staged))
            { isError = true; return policyErrorPayload("invalidArguments", "sed_new request must be an object"); }
            if (staged)
            {
                // Keep the scratch alive only for this call.
                Json result = runWrite(name, arguments, "new",
                    { workspaceRelativeArg(output), scratch.path.string() }, isError);
                return result;
            }
        }
        return runWrite(name, arguments, "new", args, isError);
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
        if (!stageRequestArg(arguments, scratch, staged))
        { isError = true; return policyErrorPayload("invalidArguments", "sed_snapshot request must be an object"); }
        if (!staged) { scratch = stageRequest(Json::makeObject()); }
        return runAgent("snapshot", { workspaceRelativeArg(path), scratch.path.string(),
                                      workspaceRelativeArg(output) }, isError);
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
                std::string(p) == "request" ? "object" : "string"));
            props.set(p, std::move(prop));
        }
        s.set("properties", std::move(props));
        Json req = Json::makeArray();
        for (const char* r : required) req.array.push_back(Json::makeString(r));
        s.set("required", std::move(req));
        return s;
    };
    auto tool = [](const char* name, const char* description, Json inputSchema) {
        Json t = Json::makeObject();
        t.set("name", Json::makeString(name));
        t.set("description", Json::makeString(description));
        t.set("inputSchema", std::move(inputSchema));
        return t;
    };
    Json tools = Json::makeArray();
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
    tools.array.push_back(tool("sed_discover", "Inspect a level document (agent inspect).",
        schema({ "path" }, { "path" })));
    tools.array.push_back(tool("sed_query", "Query level collections (agent query).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_select", "Geometry-bound spatial selection (agent select).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_diagnose", "Validate a level document (agent validate). Optional request.strict checks raw JKL MATERIALS/GEORESOURCE framing before compatibility import.",
        schema({ "path" }, { "path", "request" })));
    tools.array.push_back(tool("sed_workbench_query", "Exact workbench query over level collections (agent workbench-query).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_arch_validate", "Validate ARCH bindings against explicitly supplied 3DO models (agent arch-validate).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_package_plan", "Plan a reviewed level package; writes nothing (agent package-plan). (D-03)",
                          schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_package_apply", "Build a reviewed level package to a new .gob/.goo after hash revalidation; never overwrites (agent package-apply). (D-03)",
                          schema({ "path", "request", "output" }, { "path", "request", "output", "expectedOutputSha256", "expectedManifestSha256", "operationId" })));
    tools.array.push_back(tool("sed_plan", "Plan a patch; writes nothing, returns outputSha256 (agent plan).",
        schema({ "path", "request" }, { "path", "request" })));
    tools.array.push_back(tool("sed_apply", "Apply a reviewed patch to a new file; never overwrites (agent apply).",
        schema({ "path", "request", "output" }, { "path", "request", "output", "expectedOutputSha256", "operationId" })));
    tools.array.push_back(tool("sed_new", "Create a new level scaffold at a new path (agent new).",
        schema({ "output" }, { "output", "kind", "request", "operationId" })));
    tools.array.push_back(tool("sed_convert", "Convert a level to a new .jkl/.sed file (agent convert).",
        schema({ "path", "output" }, { "path", "output", "expectedOutputSha256", "operationId" })));
    tools.array.push_back(tool("sed_archive", "List a GOB/GOO archive, or extract one exact entry to a new file (agent archive/extract).",
        schema({ "path" }, { "path", "entry", "output", "operationId" })));
    tools.array.push_back(tool("sed_snapshot", "Render a software-v1 PNG of world surfaces; Things, models, sprites, and dynamic lights are excluded.",
        schema({ "path", "output" }, { "path", "request", "output", "operationId" })));
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
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto takeValue = [&](const char* flag) -> const char* {
            if (i + 1 >= argc)
            {
                std::cerr << "sed-mcp: " << flag << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--workspace")
        {
            const char* v = takeValue("--workspace");
            if (!v) return 2;
            g_workspace = fs::path(v);
        }
        else if (arg == "--resource-root")
        {
            const char* v = takeValue("--resource-root");
            if (!v) return 2;
            g_resourceRoots.push_back(fs::path(v));
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
            std::cerr << "sed-mcp: unknown argument '" << arg << "'\n";
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




