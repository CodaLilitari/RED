#pragma once

#include "agent_json.hpp"

namespace sed
{
struct Level;
}

namespace sed_agent
{

// Operations that give the agent the RED app's remaining Edit, Sector and
// Surface commands over the shared core: deleteThings, deleteLights,
// deleteCogs, setSectorProperties, setSurfaceProperties, unadjoinSurfaces,
// makeUnderwater, placeSectorLights and removeRedundantVertices.
// Returns false for another operation family. On success `report` holds the
// operation's receipt entry; throws AgentError for an invalid or refused
// request, with the caller's candidate Level to be discarded.
bool tryApplyParityOperation(sed::Level& level, const Json& operation, Json& report);

} // namespace sed_agent
