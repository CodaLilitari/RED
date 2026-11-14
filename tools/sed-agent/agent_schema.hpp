#pragma once

#include "agent_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sed_agent
{
struct AgentCommandDescriptor
{
    std::string_view name;
    std::string_view usage;
    std::string_view description;
};
struct AgentOperationDescriptor
{
    std::string_view name;
    std::string_view legacyDescriptor;
};

const std::vector<AgentCommandDescriptor>& agentCommandDescriptors();
const std::vector<AgentOperationDescriptor>& agentOperationDescriptors();
Json patchRequestSchema();
Json packageRequestSchema();
Json matCreateRequestSchema();
Json dependencyPathSchema();
Json dependencyRequestSchema();
void validateDependencyRequest(const Json& request);
void validateDependencyPath(std::string_view path);
Json patchOperationSchema(const std::string& name);
Json focusedSchema(const std::string& topic);
} // namespace sed_agent
