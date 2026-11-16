#pragma once

#include "agent_json.hpp"

#include <string_view>

namespace sed
{
struct Level;
}

namespace sed::level
{
enum class SealedSeamStatus;
struct SealedSeamLimits;
}

namespace sed_agent
{

// The receipt name of a sealed-seam inspection status, shared by validate and
// conformSeams so both report an unfinished inspection in the same words.
const char* sealedSeamStatusName(sed::level::SealedSeamStatus status) noexcept;

// Applies a request's optional `seamLimits` object to the inspection limits.
// Only maxSectorPairs, maxSurfacePairs and maxEdgePairs may be set, each a
// non-negative integer within its documented work bound; anything else throws
// AgentError{kExitUsage} whose message starts with `owner`.
void applySeamLimits(const Json& limits, sed::level::SealedSeamLimits& target, std::string_view owner);

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

// Applies one conformSeams operation: every sealed-seam T-junction that has an
// admitted insertion candidate receives its neighbour's corner on the edge
// that lacks it. No surface is split and nothing is renumbered, so the
// operation is admitted after COGs are placed. Returns the operation's report
// for the receipt; throws AgentError on refusal with the Level unchanged.
Json applyConformSeams(sed::Level& level, const Json& operation);

// Applies one connectSurfaces operation: two unadjoined opposite coplanar
// faces of different closed convex rooms are cut along each other's outline
// and their shared area is adjoined, as the editor's Connect does. Faces can
// be split, which renumbers surfaces, so like the cleaves it is refused once
// COGs are placed. Returns the operation's report (the portal faces it made);
// throws AgentError on refusal with the Level unchanged.
Json applyConnectSurfaces(sed::Level& level, const Json& operation);

// Applies one deleteSectors operation: the named rooms are removed and each
// face that was a portal into one becomes a wall with the given material
// (materialIndex, required when there is such a face). Refused once COGs are
// placed and when a Thing stands in a named room. Returns the operation's
// report (deleted sectors and the faces sealed, by their new indices).
Json applyDeleteSectors(sed::Level& level, const Json& operation);

} // namespace sed_agent
