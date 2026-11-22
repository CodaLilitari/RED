#pragma once

#include "agent_json.hpp"
#include "sed/text.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace sed_agent
{

// Exit-code contract (PORT_PLAN Slice 3 / task spec):
//   0 ok, 2 usage, 3 input error, 4 stale-or-conflict, 5 refusal.
inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitInput = 3;
inline constexpr int kExitConflict = 4;
inline constexpr int kExitRefusal = 5;

// Requests (file or stdin) are capped at 1 MiB; exceeding refuses.
inline constexpr std::size_t kMaxRequestBytes = 1024 * 1024;

struct AgentError
{
    int code;
    std::string message;

    // Stable machine-readable refusal code (e.g. a core COG code or
    // `snapshotRequiresRenderer`); empty when the message is enough.
    std::string errorCode{};
    // Optional full JSON payload printed instead of the error envelope
    // (blocked package plans still report their whole plan, exit 3).
    Json payload{};
    bool hasPayload{ false };
};

inline bool isLevelPathCommand(std::string_view command)
{
    return command == "inspect" || command == "new" || command == "query" || command == "select"
        || command == "mots-bake-delta" || command == "validate" || command == "plan"
        || command == "apply" || command == "cog-links";
}

inline bool isNewLevelKind(std::string_view kind)
{
    return sed::text::iequalsAscii(kind, "jk") || sed::text::iequalsAscii(kind, "jkdf2")
        || sed::text::iequalsAscii(kind, "mots") || sed::text::iequalsAscii(kind, "droidworks");
}

// Reads a bounded request document (`path`, or stdin when path is "-").
// Throws AgentError{kExitUsage} when the document exceeds the cap or is
// malformed JSON, AgentError{kExitInput} when the file cannot be read.
Json readRequest(const std::string& path);
// Same bounded, revision-tracked request reader for an explicitly decoded native path.
Json readRequestFile(const std::filesystem::path& path);

// Executes one command. `args` are the arguments after the command name.
// Results are written as one JSON document to stdout by the caller.
Json runCommand(const std::string& command, const std::vector<std::string>& args);

// Optional host policy for loose resources resolved from names stored inside
// a document. Explicit command/request paths remain the host's admission
// responsibility. The callback runs before resource bytes are read and may
// throw AgentError; the CLI's two-argument entry point imposes no host policy.
struct CommandOptions
{
    std::function<void(const std::filesystem::path&)> resourceReadPolicy;
    // Native asset receipts can reserve space for journal metadata added by their host.
    std::size_t nativeReceiptLimit{ kMaxRequestBytes };
};
Json runCommand(const std::string& command, const std::vector<std::string>& args,
    const CommandOptions& options);

std::string readFileBytes(const std::filesystem::path& path,
    std::size_t maxBytes = 128u * 1024u * 1024u);
std::size_t nativeReceiptLimit();
void writeNewFileAtomic(const std::filesystem::path& path, std::string_view bytes);

/** Where red-agent was started from, for finding data/ beside it. */
void setAgentExecutable(const char* argv0);
void setAgentExecutable(const std::filesystem::path& path);

} // namespace sed_agent
