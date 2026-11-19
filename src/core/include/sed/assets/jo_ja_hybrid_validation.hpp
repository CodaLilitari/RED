#pragma once

#include "sed/assets/jo_ja_hybrid_geometry.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace sed::assets
{
// A native placement cannot be represented at target precision. Rebuild the
// affected drawn surfaces as residual geometry, preserving all genuine portal
// masks. Other topology or work failures remain ordinary runtime errors.
struct JoJaNativePromotionError : std::runtime_error
{
    JoJaNativePromotionError(std::vector<std::size_t> sources, const std::string& reason)
        : std::runtime_error(reason), sourceSurfaces(std::move(sources))
    {
    }
    std::vector<std::size_t> sourceSurfaces;
};

// Completes generated target-space edge chains, bounds native face sizes, and
// certifies closed inward shells and reciprocal apertures at engine precision.
// Throws before modifying geometry if the partition cannot be certified.
void finalizeJoJaHybridGeometry(JoJaHybridGeometry& geometry, std::size_t maxFragments = 500000);
} // namespace sed::assets
