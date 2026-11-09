#pragma once

#include "agent_json.hpp"
#include "sed/level/cog_bindings.hpp"

#include <optional>

namespace sed
{
struct Level;
}

namespace sed_agent
{

// Refuses retained unmodeled records whose entity-index dependencies cannot
// be preserved safely when either single Things or arrays are appended.
void validateEntityAppendSource(const sed::Level& level);

// Call once before an isolated patch's first allocating operation. Existing
// IDs remain untouched; a nonpositive or reused next-ID candidate refuses.
// Subsequent allocations use the core's constant-time capacity checks.
void validateAppendIdentitySource(const sed::Level& level);

// Source-light and Thing operations. The caller owns the isolated
// transaction and must discard it on error before publishing any output.
// An optional resolver must use the caller's captured, revision-bound inputs.
bool tryApplyEntityOperation(sed::Level& level, const Json& operation,
    const std::optional<sed::level::CogSchemaResolver>& schemaResolver = std::nullopt);

} // namespace sed_agent
