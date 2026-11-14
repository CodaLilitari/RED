#pragma once

#include "agent_json.hpp"
#include "sed/level/semantic_preview.hpp"

#include <string_view>

namespace sed
{
struct Level;
}

namespace sed_agent
{

using SemanticPreviewLimits = sed::semantic_preview::Limits;

Json buildSemanticPreview(const sed::Level& source, const sed::Level& candidate,
    const sed::Level& persisted, std::string_view inputSha256,
    std::string_view outputSha256, const SemanticPreviewLimits& limits = {});

Json semanticPreviewCapabilities();

} // namespace sed_agent
