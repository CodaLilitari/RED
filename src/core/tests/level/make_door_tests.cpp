#include "test_common.hpp"

#include "sed/geometry.hpp"
#include "sed/level/door.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using level::MakeDoorError;

Level doorLevel(const double pch, const double yaw, const double rol)
{
    Level level{};
    Thing& keep = level.newThing();
    keep.name = "keep";
    keep.templateName = "keep";
    Thing& door = level.newThing();
    door.name = "slab";
    door.templateName = "slab";
    door.x = 1.0;
    door.y = 2.0;
    door.z = 3.0;
    door.pch = pch;
    door.yaw = yaw;
    door.rol = rol;
    door.values.push_back(deserializeTplValue("health", "50"));
    door.values.push_back(deserializeTplValue("thingflags", "0x1"));
    return level;
}

std::vector<CogValue> doorSymbols()
{
    std::vector<CogValue> symbols(3);
    symbols[0].name = "door0";
    symbols[0].type = ValueType::ptr;
    symbols[0].cogType = CogType::thing;
    symbols[0].intValue = -1;
    symbols[1].name = "movespeed";
    symbols[1].type = ValueType::floating;
    symbols[1].cogType = CogType::float_;
    symbols[1].floatValue = 8.0;
    symbols[2].name = "sleeptime";
    symbols[2].type = ValueType::floating;
    symbols[2].cogType = CogType::float_;
    symbols[2].floatValue = 2.0;
    return symbols;
}

bool frameAt(const TplValue& value, TVector& position, TVector& pyr)
{
    return value.type == ValueType::frame && text::iequalsAscii(value.name, "frame")
        && value.tryGetFrame(position.x, position.y, position.z, pyr.pitch, pyr.yaw, pyr.roll);
}

void runMakeDoorConvertsThing()
{
    Level level = doorLevel(0.0, 45.0, 0.0);
    const std::int32_t curIdBefore = level.curId;
    const level::MakeDoorResult result = level::levelMakeDoor(level, 1, 0.5, "00_door.cog", doorSymbols());
    check(result.error == MakeDoorError::None && result.cog == 0, "make door: succeeds and returns the new COG");

    const Thing& door = level.things[1];
    check(door.values.size() == 4, "make door: replaces the Thing's own values with flags, count and two frames");
    if ( door.values.size() == 4 )
    {
        check(text::iequalsAscii(door.values[0].name, "thingflags") && door.values[0].type == ValueType::flag
                && door.values[0].intValue == 0x408,
            "make door: first value is thingflags 0x408");
        check(text::iequalsAscii(door.values[1].name, "numframes") && door.values[1].intValue == 2,
            "make door: numframes 2 precedes the frames");
        TVector p0{};
        TVector r0{};
        TVector p1{};
        TVector r1{};
        check(frameAt(door.values[2], p0, r0) && frameAt(door.values[3], p1, r1), "make door: two frame values");
        check(close(p0.x, 1.0) && close(p0.y, 2.0) && close(p0.z, 3.0) && close(r0.yaw, 45.0) && close(r1.yaw, 45.0),
            "make door: frame 0 is the Thing pose and both frames keep its orientation");
        check(close(p1.x, 1.0) && close(p1.y, 2.0) && close(p1.z, 3.5),
            "make door: frame 1 is raised by the bounding-box height along the yawed up axis");
    }
    check(level::thingCountFrames(door) == 2, "make door: the runtime frame count matches");
    check(level.things[0].values.empty() && level.things[0].name == "keep", "make door: other Things are untouched");

    check(level.cogs.size() == 1, "make door: one COG is added");
    if ( level.cogs.size() == 1 )
    {
        const Cog& cog = level.cogs[0];
        check(cog.name == "00_door.cog" && cog.values.size() == 3, "make door: the COG uses the door script symbols");
        check(cog.values.size() == 3 && cog.values[0].intValue == 1 && cog.values[0].cogType == CogType::thing,
            "make door: the first COG symbol references the door Thing");
        check(cog.values.size() == 3 && close(cog.values[1].floatValue, 8.0) && close(cog.values[2].floatValue, 2.0),
            "make door: the remaining COG symbols keep their defaults");
        check(cog.id == curIdBefore && level.curId == curIdBefore + 1, "make door: the COG receives a fresh entity ID");
    }
}

void runMakeDoorFollowsPitch()
{
    Level level = doorLevel(90.0, 0.0, 0.0);
    const level::MakeDoorResult result = level::levelMakeDoor(level, 1, 2.0, "00_door.cog", doorSymbols());
    check(result.error == MakeDoorError::None, "make door pitched: succeeds");
    const Thing& door = level.things[1];
    TVector p0{};
    TVector r0{};
    TVector p1{};
    TVector r1{};
    if ( door.values.size() == 4 && frameAt(door.values[2], p0, r0) && frameAt(door.values[3], p1, r1) )
    {
        const TVector offset{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        check(close(offset.Length(), 2.0, 1e-5) && std::abs(offset.z) < 1e-5,
            "make door pitched: the open offset follows the pitched up axis");
    }
    else
    {
        check(false, "make door pitched: two frame values");
    }
}

void runMakeDoorRefusals()
{
    const auto unchanged = [](Level& level, const std::size_t thing, const double height, const MakeDoorError expected,
                               const char* message)
        {
            const std::vector<TplValue> valuesBefore = level.things.size() > 1 ? level.things[1].values
                                                                                : std::vector<TplValue>{};
            const std::size_t cogsBefore = level.cogs.size();
            const std::int32_t curIdBefore = level.curId;
            const level::MakeDoorResult result = level::levelMakeDoor(level, thing, height, "00_door.cog", doorSymbols());
            bool same = result.error == expected && result.cog == -1 && level.cogs.size() == cogsBefore
                && level.curId == curIdBefore;
            if ( level.things.size() > 1 )
            {
                same = same && level.things[1].values.size() == valuesBefore.size();
                for ( std::size_t i = 0; same && i < valuesBefore.size(); ++i )
                {
                    same = level.things[1].values[i].name == valuesBefore[i].name
                        && level.things[1].values[i].asString() == valuesBefore[i].asString();
                }
            }
            check(same, message);
        };

    Level level = doorLevel(0.0, 0.0, 0.0);
    unchanged(level, 5, 0.5, MakeDoorError::InvalidThing, "make door: refuses an out-of-range Thing");
    unchanged(level, 1, 0.0, MakeDoorError::FlatBoundingBox, "make door: refuses a zero-height bounding box");
    unchanged(level, 1, 1e-9, MakeDoorError::FlatBoundingBox, "make door: refuses a near-zero bounding box");
    unchanged(level, 1, -0.5, MakeDoorError::FlatBoundingBox, "make door: refuses an inverted bounding box");
    unchanged(level, 1, std::numeric_limits<double>::quiet_NaN(), MakeDoorError::FlatBoundingBox,
        "make door: refuses a non-finite height");

    level.things[1].values.push_back(deserializeTplValue("NumFrames", "1"));
    unchanged(level, 1, 0.5, MakeDoorError::AlreadyDoor, "make door: refuses a Thing that already declares numframes");

    Level exhausted = doorLevel(0.0, 0.0, 0.0);
    exhausted.curId = std::numeric_limits<std::int32_t>::max();
    unchanged(exhausted, 1, 0.5, MakeDoorError::IdSpaceExhausted, "make door: refuses when no COG ID is available");
}

} // namespace

void runMakeDoorTests()
{
    runMakeDoorConvertsThing();
    runMakeDoorFollowsPitch();
    runMakeDoorRefusals();
}

} // namespace sed::tests
