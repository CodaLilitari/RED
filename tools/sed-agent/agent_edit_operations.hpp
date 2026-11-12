#pragma once

#include "agent_json.hpp"

namespace sed
{
struct Level;
}

namespace sed_agent
{

// Applies one bounded vertex/texture edit to the caller's isolated Level.
// Returns false only for another operation family; throws AgentError for an
// invalid or refused request. Discard the candidate on any error and publish
// only after the entire patch and persistence-preservation checks succeed.
bool tryApplyEditOperation(sed::Level& level, const Json& operation);

} // namespace sed_agent
