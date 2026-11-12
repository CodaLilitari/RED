#pragma once

#include "agent_json.hpp"
#include "sed/level/dependencies.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed_agent
{

struct DependencyArchiveRequest
{
    std::string path;
    std::string sha256;
    bool stock{false};
};

struct ParsedDependencyRequest
{
    std::string inputSha256;
    std::vector<DependencyArchiveRequest> archives;
    std::vector<sed::level::PackageExtraResource> extraResources;
    sed::level::DependencyLimits limits{};
};

// Uses the same reversible ASCII encoding as dependency receipt identifiers.
std::string encodeDependencyText(std::string_view value);

// Pure admission: these functions never open files or assemble output.
ParsedDependencyRequest parseDependencyRequest(const Json& request);
Json inspectDependencyRequest(std::string_view levelBytes, std::string_view levelFileName,
    const ParsedDependencyRequest& request,
    std::span<const sed::level::DependencyArchiveInput> archives);

} // namespace sed_agent
