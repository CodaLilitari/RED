#pragma once

#include "agent_json.hpp"

#include <cstddef>

namespace sed
{
struct Level;
}

namespace sed_agent
{

// Paginates sector-local surfaces/vertices before constructing detailed rows.
// The workspace adds its authoritative inputSha256/sourceRevision envelope.
Json queryGeometryDetails(const sed::Level& level, const Json& request);
Json describeThingDetails(const sed::Level& level, std::size_t index);
Json describeLightDetails(const sed::Level& level, std::size_t index);

} // namespace sed_agent
