#pragma once

#include "sed/assets/raven_bsp.hpp"

#include <optional>

namespace sed::assets
{

enum class RavenPvsState { Unknown, Outside, PotentiallyVisible };

struct RavenVisibilityQuery
{
    int viewCluster{-1};
    std::size_t offset{};
};

inline constexpr std::size_t kRavenVisibilityPage = 64;

struct RavenSurfaceOwners
{
    std::vector<int> clusters;
    bool unknownCluster{}, truncated{};
};

struct RavenPvsView
{
    bool rowUsable{};
    std::vector<RavenPvsState> surfaces;
    // Optional bounded membership page, indexed relative to the world model.
    std::vector<RavenSurfaceOwners> ownerPage;
};

// Consume a validated RBSP. Only leaves reachable from the world root own
// rendered world surfaces. The optional page reports up to 32 distinct owners
// per surface; classification always uses every reference.
RavenPvsView ravenPvsView(const RavenBsp& bsp, int viewCluster,
                          std::optional<std::size_t> ownerOffset = std::nullopt);

RavenPvsState ravenMeshVisibility(const RavenPvsView& view, std::span<const std::size_t> surfaces);

} // namespace sed::assets
