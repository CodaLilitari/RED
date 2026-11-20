#pragma once

#include "agent_json.hpp"
#include "sed/assets/jo_ja_import.hpp"

#include <map>

namespace sed_agent
{
std::map<std::string, std::string> takeGeneratedLevels(sed::assets::JoJaImportResult& baseline);

/** Three-way comparison against the exact generated baseline. Mutates next only on success. */
Json applyJoJaOverlays(const sed::assets::JoJaImportResult& baseline, sed::assets::JoJaImportResult& next,
    std::string authoredBytes, const Json& overrides, const std::map<std::string, std::string>& baselineLevels);
}
