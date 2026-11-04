#include "sed/level/thing_frames.hpp"

namespace sed::level
{

ThingPathFrameState thingInheritedPathFrameState(
    const Level&,
    const assets::Templates*,
    const Thing&)
{
    return {};
}

bool thingMovesOnPath(const Level&, const assets::Templates*, const Thing&)
{
    return false;
}

bool PreparedThingFrames::apply(Level&) const
{
    return false;
}

PreparedThingFrames prepareSetThingFrames(
    const Level&,
    const assets::Templates*,
    const std::size_t thingIndex,
    std::span<const FrameValue>)
{
    PreparedThingFrames prepared{};
    prepared.thingIndex = thingIndex;
    prepared.failure = WorkbenchFailure{ "notImplemented", "setThingFrames is not implemented" };
    return prepared;
}

} // namespace sed::level
