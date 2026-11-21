#pragma once

#include "agent_json.hpp"

#include <string>
#include <vector>

namespace sed_agent
{

Json modelNativeInspect(const std::vector<std::string>& args);
Json modelNativePlanOrApply(const std::vector<std::string>& args, bool apply);
Json modelNativeRequestSchema();
Json modelNativeInspectQuerySchema();

} // namespace sed_agent
