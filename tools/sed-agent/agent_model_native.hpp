#pragma once

#include "agent_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sed_agent
{

bool isModelNativeCommand(std::string_view name);
Json modelNativeInspect(const std::vector<std::string>& args);
Json modelNativePlanOrApply(const std::vector<std::string>& args, bool apply);
Json modelNativeRequestSchema();
Json modelNativeInspectQuerySchema();

} // namespace sed_agent
