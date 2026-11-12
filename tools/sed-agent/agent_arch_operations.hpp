#pragma once

#include "agent_json.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sed
{
struct Level;
namespace assets
{
class Templates;
}
}

namespace sed_agent
{

// The caller resolves the effective model name exclusively from its admitted,
// immutable archive capture. The digest must describe these exact entry bytes.
struct ArchModelResource
{
    std::string bytes;
    std::string sha256Hex;
};

using ArchModelResolver =
    std::function<std::optional<ArchModelResource>(std::string_view modelName)>;

// One instance belongs to one patch, shared by every ARCH operation. These
// limits supplement the caller's archive byte/entry budget and source read set.
struct ArchOperationBudget
{
    std::size_t remainingWorkUnits{ 50000000u };
    std::size_t remainingModelBytes{ 256u * 1024u * 1024u };
};

// Returns false only for another operation family. Invalid/refused requests
// throw AgentError before publishing an ARCH change. The caller owns the
// isolated Level candidate and discards it if any later patch step fails.
// No resource name or path is read from the filesystem by this adapter.
// All three operations require the shared validator's exact geoset-zero shape
// with fitting lower LODs. Successful authoring clears only the selected
// Thing's retained automatic-bake baseline, so its next automatic bake starts
// from the newly authored samples. Other owners and baseline records remain.
bool tryApplyArchOperation(sed::Level& level, const Json& operation,
    const ArchModelResolver& resolver, const sed::assets::Templates* templates = nullptr,
    ArchOperationBudget* budget = nullptr);

} // namespace sed_agent
