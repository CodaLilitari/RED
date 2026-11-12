#pragma once

#include "agent_json.hpp"

#include <string>
#include <vector>

namespace sed_agent
{

Json modelLightingInspect(const std::vector<std::string>& args);
Json modelLightingPlanOrApply(const std::vector<std::string>& args, bool apply);
Json modelLightingRequestSchema();

} // namespace sed_agent
