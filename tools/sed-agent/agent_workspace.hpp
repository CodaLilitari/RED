#pragma once

#include "agent_json.hpp"

#include <filesystem>
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

// Reads a bounded request document (`path`, or stdin when path is "-").
// Throws AgentError{kExitUsage} when the document exceeds the cap or is
// malformed JSON, AgentError{kExitInput} when the file cannot be read.
Json readRequest(const std::string& path);

// Executes one command. `args` are the arguments after the command name.
// Results are written as one JSON document to stdout by the caller.
Json runCommand(const std::string& command, const std::vector<std::string>& args);

std::string readFileBytes(const std::filesystem::path& path,
    std::size_t maxBytes = 128u * 1024u * 1024u);
void writeNewFileAtomic(const std::filesystem::path& path, std::string_view bytes);

} // namespace sed_agent
