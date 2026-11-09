#pragma once

#include "agent_json.hpp"

namespace sed
{
struct Level;
}

namespace sed_agent
{

// Applies the same unknown-record and imported-COG-mapping admission to
// basic geometry operations and the larger headless geometry recipes.
void validateGeometryAppendSource(const sed::Level& level);

// Checks existing COG bindings against proposed sector/global-surface appends,
// including preferred native surface-pair text. Throws before publication if
// an invalid or unresolved initializer could acquire a new geometry target.
// Callers of larger recipes retain a before snapshot only when COGs exist;
// the bounded command's candidate must be discarded on refusal.
void validateGeometryAppendReferences(const sed::Level& before, const sed::Level& after);

// Applies one supported basic design operation to the caller's isolated Level.
// Returns false for another operation family; throws AgentError on refusal.
// The caller must discard its working Level on any error and publish only after
// the complete request and persistence-preservation checks succeed.
bool tryApplyBasicOperation(sed::Level& level, const Json& operation);

} // namespace sed_agent
