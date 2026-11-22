#pragma once

#include "agent_json.hpp"
#include "sed/level/workbench.hpp"

namespace sed_agent
{

inline constexpr std::size_t kMaxMapRequirements = 64;
inline constexpr std::size_t kMaxRequirementNameBytes = 128;
inline constexpr std::size_t kMaxRequirementWorkUnits = 4'000'000;

struct ParsedWorkbenchQuery
{
    sed::level::WorkbenchCollection collection{};
    std::optional<std::int32_t> sector{};
    std::optional<sed::level::ExactQueryFilter> filter{};
    std::string search{};
};

// The legacy standalone query and saved requirements use the same field decoding.
ParsedWorkbenchQuery parseWorkbenchQuery(const Json& request);

// Computes a complete read-only report, or throws AgentError with no partial results.
Json evaluateMapRequirements(const sed::Level& level, const Json& request);

} // namespace sed_agent
