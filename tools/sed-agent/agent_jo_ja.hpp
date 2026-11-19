#pragma once

#include "agent_json.hpp"

namespace sed_agent
{
Json joJaInspect(const std::vector<std::string>& args);
Json joJaPlanOrApply(const std::vector<std::string>& args, bool apply);
} // namespace sed_agent
