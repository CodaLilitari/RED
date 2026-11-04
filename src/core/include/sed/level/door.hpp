#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sed::level
{

enum class MakeDoorError
{
    None,
    InvalidThing,
    AlreadyDoor,
    FlatBoundingBox,
    IdSpaceExhausted,
};

struct MakeDoorResult
{
    MakeDoorError error{ MakeDoorError::None };
    std::int32_t cog{ -1 };
};

/** Converts a Thing into a two-frame sliding door, matching Pascal SED's Make Door.
 *
 * The Thing's own values are replaced by `thingflags 0x408`, `numframes 2`, a frame at the Thing pose, and a second
 * frame offset by `height` along the Thing's rotated up axis. A COG named `cogScriptName` is appended with
 * `cogSymbols`, and its first symbol is set to the Thing number. Refuses, leaving the level unchanged, when the Thing
 * already declares `numframes`, when `height` is not a usable positive extent, or when no entity ID is available.
 */
MakeDoorResult levelMakeDoor(
    Level& level,
    std::size_t thingIndex,
    double height,
    std::string_view cogScriptName,
    const std::vector<CogValue>& cogSymbols);

} // namespace sed::level
