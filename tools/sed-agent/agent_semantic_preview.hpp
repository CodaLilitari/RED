#pragma once

#include "agent_json.hpp"

#include <cstddef>
#include <string_view>

namespace sed
{
struct Level;
}

namespace sed_agent
{

struct SemanticPreviewLimits
{
    std::size_t maxVisitedSlots{1000000};
    std::size_t maxComparedStringBytes{4 * 1024 * 1024};
    std::size_t maxChanges{128};
    std::size_t maxInlineStringBytes{512};
    std::size_t maxTotalInlineStringBytes{32 * 1024};
};

Json buildSemanticPreview(const sed::Level& source, const sed::Level& candidate,
    const sed::Level& persisted, std::string_view inputSha256,
    std::string_view outputSha256, const SemanticPreviewLimits& limits = {});

Json semanticPreviewCapabilities();

} // namespace sed_agent
