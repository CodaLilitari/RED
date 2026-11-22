#pragma once

#include "sed/level.hpp"

namespace sed::assets
{
struct RavenBsp;
struct JoJaImportOptions;
struct JoJaImportResult;
struct JoJaHybridGeometry;
void placeJoJaStockPickups(const RavenBsp& bsp, const JoJaImportOptions& options,
                           const JoJaHybridGeometry* hybrid, const TVector& low, const TVector& high,
                           Level& level, JoJaImportResult& result);
}
