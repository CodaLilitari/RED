#pragma once

#include "sed/level/emissive_resolution.hpp"
#include "sed/level/ray_lighting.hpp"
#include "sed/level/thing.hpp"

#include <memory>

namespace sed::level
{

struct RayLightingDependency
{
    std::string name;
    std::string extension;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

struct RayLightingResources
{
    std::vector<RayLightingMaterial> materials;
    std::vector<RayLightingBlocker> blockers;
    std::vector<RayLightingDependency> dependencies;
    std::size_t modelInstances{};
};

// Reads one bounded winner, then tries the extension if absent. Use a fresh
// locator configuration/reader for revalidation against live dependencies.
EmissiveResourceReader makeRayLightingResourceReader(assets::ResourceLocator locator);

// Resolve before baking. Throws on unsupported or unavailable resources,
// cancellation or a budget violation. The supplied Level is never modified.
RayLightingResources resolveRayLightingResources(const Level& level, const EmissiveResourceReader& reader,
                                                 const std::function<bool()>& cancelled = {},
                                                 const assets::Templates* templates = nullptr);

bool rayLightingResourcesCurrent(const RayLightingResources& snapshot, const EmissiveResourceReader& reader,
                                 const std::function<bool()>& cancelled = {});

// No I/O. Includes effective placed architecture parameters and material
// references. Combine with the caller's geometry/light/document revision.
std::uint64_t rayLightingResourceInputsFingerprint(const Level& level, const assets::Templates* templates = nullptr);

} // namespace sed::level
