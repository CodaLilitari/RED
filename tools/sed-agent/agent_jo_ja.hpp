#pragma once

#include "agent_json.hpp"
#include <optional>

namespace sed_agent
{
// The saved collision field is a complete decimal mode in [0, 3].
std::optional<int> parseThingCollisionMode(std::string_view spelling);
Json inspectTargetCollision(const std::vector<std::string>& args);
Json joJaInspect(const std::vector<std::string>& args);
Json joJaPlanOrApply(const std::vector<std::string>& args, bool apply);
} // namespace sed_agent
