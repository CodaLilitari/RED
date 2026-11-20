#pragma once

#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/raven_bsp.hpp"

namespace sed::assets
{
struct JoJaPlayerClipModel
{
    JoJaPlayerClipReport report;
    TVector origin, low, high;
    double radius{};
    std::string bytes;
};

// Operates on parseRavenBsp-validated ranges; selected brushes must close at target precision.
std::vector<JoJaPlayerClipModel> buildJoJaPlayerClips(const RavenBsp& bsp, const JoJaImportOptions& options,
                                                      std::size_t availableModels, const std::string& existingMaterial);
} // namespace sed::assets
