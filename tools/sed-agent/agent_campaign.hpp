#pragma once

#include "agent_json.hpp"

namespace sed_agent
{
Json campaignCheck(const std::vector<std::string>& args);
Json campaignCheckSchema();
// All numeric fields in a campaign contract describe integers. Check before
// serializing an MCP request, while the parsed decimal precision is available.
bool campaignNumbersAreIntegers(const Json& value);
}
