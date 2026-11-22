#pragma once

#include <cstddef>
#include <functional>

namespace sed
{
struct Sector;
}

namespace sed::level
{

// Checks closed boundary incidence and distinct geometric normal directions.
// Uses the existing indexed proof, then its coordinate-alias/T-junction fallback.
// The caller separately checks face planarity, convexity and inward orientation.
// chargeWork is called before scans, allocation and ordered-container work;
// false stops without changing the sector. The caller owns the budget and can
// retain an exhaustion flag to distinguish refusal from invalid geometry.
bool sectorHasClosedVolumeBoundary(const Sector& sector,
    const std::function<bool(std::size_t)>& chargeWork);

} // namespace sed::level
