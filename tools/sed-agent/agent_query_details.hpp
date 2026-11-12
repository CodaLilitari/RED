#pragma once

#include "agent_json.hpp"

#include <cstddef>
#include <optional>

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

// Rich record collections paginate before constructing parameter/sample rows.
// Returns nullopt for another collection. The caller supplies source hashes.
// Each page admits at most 4096 nested records, one MiB of authored text
// (including both COG value copies, with bounded scalar formatting allowance),
// one million ARCH samples and one million ARCH-to-Thing comparisons. Point
// containment additionally bounds geometry work and reports unbounded
// nonfinite geometry as unknown rather than claiming an outside result.
// Nonempty name filters use linear ASCII matching with an eight-MiB cumulative
// pattern/candidate-byte allowance plus one unit per examined record. Empty
// filters select a page without scanning unselected names.
std::optional<Json> queryRecordDetails(const sed::Level& level, const Json& request);

} // namespace sed_agent
