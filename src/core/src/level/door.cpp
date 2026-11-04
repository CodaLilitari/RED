#include "sed/level/door.hpp"

#include "sed/geometry.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace sed::level
{

MakeDoorResult levelMakeDoor(
    Level& level,
    const std::size_t thingIndex,
    const double height,
    const std::string_view cogScriptName,
    const std::vector<CogValue>& cogSymbols)
{
    if ( thingIndex >= level.things.size() )
    {
        return MakeDoorResult{ MakeDoorError::InvalidThing, -1 };
    }
    Thing& thing = level.things[thingIndex];
    const bool hasFrameCount = std::any_of(thing.values.begin(), thing.values.end(), [](const TplValue& value)
        {
            return sed::text::iequalsAscii(value.name, "numframes");
        });
    if ( hasFrameCount )
    {
        return MakeDoorResult{ MakeDoorError::AlreadyDoor, -1 };
    }
    // Pascal SED rejects IsClose(height, 0); a downward or non-finite extent cannot describe an opening either.
    if ( !std::isfinite(height) || height <= CloseEnough )
    {
        return MakeDoorResult{ MakeDoorError::FlatBoundingBox, -1 };
    }
    if ( !level.canAllocateIds(1) )
    {
        return MakeDoorResult{ MakeDoorError::IdSpaceExhausted, -1 };
    }

    TVector open{ 0.0, 0.0, height };
    rotateVector(open, thing.pch, thing.yaw, thing.rol);

    std::vector<TplValue> values{};
    values.reserve(4);
    values.push_back(deserializeTplValue("thingflags", "0x408"));
    TplValue closed = deserializeTplValue("frame", "");
    closed.setFrame(thing.x, thing.y, thing.z, thing.pch, thing.yaw, thing.rol);
    TplValue opened = closed;
    opened.setFrame(thing.x + open.x, thing.y + open.y, thing.z + open.z, thing.pch, thing.yaw, thing.rol);
    values.push_back(std::move(closed));
    values.push_back(std::move(opened));

    std::vector<CogValue> cogValues = cogSymbols;
    if ( !cogValues.empty() )
    {
        (void)cogValues.front().parse(std::to_string(thingIndex));
    }

    Cog& cog = level.newCog();
    cog.name = std::string(cogScriptName);
    cog.values = std::move(cogValues);
    thing.values = std::move(values);
    thingSyncNumFrames(thing);
    return MakeDoorResult{ MakeDoorError::None, cog.num };
}

} // namespace sed::level
