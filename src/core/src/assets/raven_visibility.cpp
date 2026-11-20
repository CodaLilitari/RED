#include "sed/assets/raven_visibility.hpp"

#include <algorithm>

namespace sed::assets
{

RavenPvsView ravenPvsView(const RavenBsp& bsp, int viewCluster, std::optional<std::size_t> ownerOffset)
{
    RavenPvsView result;
    result.surfaces.resize(bsp.surfaces.size(), RavenPvsState::Unknown);
    const auto& world = bsp.models.at(0);
    const auto count = static_cast<std::size_t>(world.surfaceCount);
    if ( ownerOffset && *ownerOffset < count )
    {
        result.ownerPage.resize(std::min(kRavenVisibilityPage, count - *ownerOffset));
    }
    if ( bsp.nodes.empty() )
    {
        return result;
    }
    const auto& pvs = bsp.visibility;
    const auto marked = [&](int cluster)
    {
        const auto row = static_cast<std::size_t>(viewCluster) * static_cast<std::size_t>(pvs.rowBytes);
        return (pvs.bits[row + static_cast<unsigned>(cluster) / 8] & (1u << (cluster % 8))) != 0;
    };
    const bool validRow = viewCluster >= 0 && viewCluster < pvs.clusterCount && marked(viewCluster);
    bool foundViewer = false;
    // Distinguish missing, negative, marked and unmarked owners before validating the viewer.
    enum : std::uint8_t { Outside = 1, Unknown = 2, Visible = 4 };
    std::vector<std::uint8_t> hits(bsp.surfaces.size());
    std::vector<std::int32_t> stack{0};
    while ( !stack.empty() )
    {
        const auto node = stack.back();
        stack.pop_back();
        if ( node >= 0 )
        {
            const auto& children = bsp.nodes[static_cast<std::size_t>(node)].children;
            stack.insert(stack.end(), children.begin(), children.end());
            continue;
        }
        const auto& leaf = bsp.leaves[static_cast<std::size_t>(-1 - node)];
        foundViewer |= leaf.cluster == viewCluster;
        const auto visibility = !validRow || leaf.cluster < 0 ? Unknown :
                                marked(leaf.cluster) ? Visible : Outside;
        for ( const int surface : std::span(bsp.leafSurfaces).subspan(
                  static_cast<std::size_t>(leaf.firstLeafSurface), static_cast<std::size_t>(leaf.leafSurfaceCount)) )
        {
            hits[static_cast<std::size_t>(surface)] |= visibility;
            const auto index = static_cast<std::size_t>(surface - world.firstSurface);
            if ( !ownerOffset || index < *ownerOffset || index - *ownerOffset >= result.ownerPage.size() )
            {
                continue;
            }
            auto& owners = result.ownerPage[index - *ownerOffset];
            if ( leaf.cluster < 0 )
            {
                owners.unknownCluster = true;
            }
            else if ( std::find(owners.clusters.begin(), owners.clusters.end(), leaf.cluster) == owners.clusters.end() )
            {
                if ( owners.clusters.size() < 32 )
                {
                    owners.clusters.push_back(leaf.cluster);
                }
                else
                {
                    owners.truncated = true;
                }
            }
        }
    }
    for ( auto& owners : result.ownerPage )
    {
        std::sort(owners.clusters.begin(), owners.clusters.end());
    }
    result.rowUsable = validRow && foundViewer;
    if ( result.rowUsable )
    {
        for ( std::size_t i = 0; i < hits.size(); ++i )
        {
            if ( hits[i] & Visible )
            {
                result.surfaces[i] = RavenPvsState::PotentiallyVisible;
            }
            else if ( hits[i] == Outside )
            {
                result.surfaces[i] = RavenPvsState::Outside;
            }
        }
    }
    return result;
}

RavenPvsState ravenMeshVisibility(const RavenPvsView& view, std::span<const std::size_t> surfaces)
{
    bool unknown = surfaces.empty();
    for ( const auto source : surfaces )
    {
        const auto state = view.surfaces.at(source);
        if ( state == RavenPvsState::PotentiallyVisible )
        {
            return state;
        }
        unknown |= state == RavenPvsState::Unknown;
    }
    return unknown ? RavenPvsState::Unknown : RavenPvsState::Outside;
}

} // namespace sed::assets
