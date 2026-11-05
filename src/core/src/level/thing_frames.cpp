#include "sed/level/thing_frames.hpp"

#include "sed/assets/tpl.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace sed::level
{
namespace
{

struct OrderedTemplateToken
{
    const TplValue* value{};
    std::size_t templateOrder{};
    std::size_t valueIndex{};
    bool frame{};
};

const ThingTemplate* embeddedTemplate(
    const Level& level,
    const std::string_view name) noexcept
{
    const auto it = std::find_if(
        level.templates.begin(),
        level.templates.end(),
        [name](const ThingTemplate& thingTemplate)
        {
            return text::iequalsAscii(thingTemplate.name, name);
        });
    return it == level.templates.end() ? nullptr : &*it;
}

template <typename FindTemplate>
void appendTemplateChainOccurrences(
    std::vector<OrderedTemplateToken>& tokens,
    const std::string_view rootName,
    const std::string_view parameterName,
    const bool frame,
    FindTemplate&& findTemplate)
{
    std::vector<const ThingTemplate*> derivedToBase{};
    std::string current(rootName);
    for (int depth = 0; depth < 1000 && !current.empty(); ++depth)
    {
        const ThingTemplate* thingTemplate = findTemplate(current);
        if (!thingTemplate
            || std::find(
                   derivedToBase.begin(), derivedToBase.end(), thingTemplate)
                != derivedToBase.end())
        {
            break;
        }
        derivedToBase.push_back(thingTemplate);
        if (thingTemplate->parent.empty()
            || text::iequalsAscii(thingTemplate->parent, "none"))
        {
            break;
        }
        current = thingTemplate->parent;
    }

    std::size_t templateOrder = 0;
    for (auto templateIt = derivedToBase.rbegin();
         templateIt != derivedToBase.rend();
         ++templateIt, ++templateOrder)
    {
        const ThingTemplate& thingTemplate = **templateIt;
        for (std::size_t valueIndex = 0;
             valueIndex < thingTemplate.values.size();
             ++valueIndex)
        {
            const TplValue& value = thingTemplate.values[valueIndex];
            if (text::iequalsAscii(value.name, parameterName))
            {
                tokens.push_back(OrderedTemplateToken{
                    &value,
                    templateOrder,
                    valueIndex,
                    frame,
                });
            }
        }
    }
}

std::vector<OrderedTemplateToken> inheritedParameterOccurrences(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing,
    const std::string_view parameterName,
    const bool frame)
{
    const std::string_view rootName = !thing.templateName.empty()
        ? std::string_view(thing.templateName)
        : std::string_view(thing.name);

    std::vector<OrderedTemplateToken> tokens{};
    appendTemplateChainOccurrences(
        tokens,
        rootName,
        parameterName,
        frame,
        [&level](const std::string_view name)
        {
            return embeddedTemplate(level, name);
        });
    if (!tokens.empty() || !templates)
    {
        return tokens;
    }

    appendTemplateChainOccurrences(
        tokens,
        rootName,
        parameterName,
        frame,
        [templates](const std::string_view name)
        {
            return templates->find(name);
        });
    return tokens;
}

bool typedFrame(const TplValue& value) noexcept
{
    return value.type == ValueType::frame
        && text::iequalsAscii(value.name, "frame");
}

bool validFrameComponent(const double value) noexcept
{
    return std::isfinite(value)
        && std::abs(value) <= kMaxWorkbenchCoordinate;
}

bool validFrame(const FrameValue& frame) noexcept
{
    return validFrameComponent(frame.x)
        && validFrameComponent(frame.y)
        && validFrameComponent(frame.z)
        && validFrameComponent(frame.pch)
        && validFrameComponent(frame.yaw)
        && validFrameComponent(frame.rol);
}

bool sameValue(const TplValue& left, const TplValue& right) noexcept
{
    return left.name == right.name
        && left.type == right.type
        && left.adjType == right.adjType
        && left.intValue == right.intValue
        && std::bit_cast<std::uint64_t>(left.floatValue)
            == std::bit_cast<std::uint64_t>(right.floatValue)
        && left.stringValue == right.stringValue;
}

bool sameValues(
    const std::vector<TplValue>& left,
    const std::vector<TplValue>& right) noexcept
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), sameValue);
}

} // namespace

ThingPathFrameState thingInheritedPathFrameState(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing)
{
    std::vector<OrderedTemplateToken> tokens = inheritedParameterOccurrences(
        level, templates, thing, "numframes", false);
    std::vector<OrderedTemplateToken> frames = inheritedParameterOccurrences(
        level, templates, thing, "frame", true);
    tokens.insert(
        tokens.end(),
        std::make_move_iterator(frames.begin()),
        std::make_move_iterator(frames.end()));
    std::stable_sort(
        tokens.begin(),
        tokens.end(),
        [](const OrderedTemplateToken& left, const OrderedTemplateToken& right)
        {
            return std::tie(left.templateOrder, left.valueIndex)
                < std::tie(right.templateOrder, right.valueIndex);
        });

    ThingPathFrameState state{};
    for (const OrderedTemplateToken& token : tokens)
    {
        if (!token.frame)
        {
            if (state.capacity != 0u)
            {
                continue;
            }
            const TplValue& value = *token.value;
            const std::int32_t parsed = value.type == ValueType::integer
                    || value.type == ValueType::ptr
                    || value.type == ValueType::flag
                ? value.intValue
                : text::parseInt<std::int32_t>(value.asString()).value_or(0);
            if (parsed > 0)
            {
                state.capacity = static_cast<std::size_t>(parsed);
            }
            continue;
        }

        if (state.capacity != 0u && state.activeFrames < state.capacity)
        {
            ++state.activeFrames;
        }
        else
        {
            ++state.ignoredFrames;
        }
    }
    return state;
}

bool thingMovesOnPath(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing)
{
    const TplValue* move = thingFindValue(level, templates, thing, "move");
    return move
        && text::iequalsAscii(text::trimView(move->asString()), "path");
}

bool PreparedThingFrames::apply(Level& level) const
{
    if (failure || thingIndex >= level.things.size())
    {
        return false;
    }

    Thing& thing = level.things[thingIndex];
    if (thing.templateName != originalTemplateName
        || !sameValues(thing.values, originalValues))
    {
        return false;
    }

    std::vector<TplValue> candidate = preparedValues;
    thing.values.swap(candidate);
    return true;
}

PreparedThingFrames prepareSetThingFrames(
    const Level& level,
    const assets::Templates* templates,
    const std::size_t thingIndex,
    const std::span<const FrameValue> frames)
{
    PreparedThingFrames prepared{};
    prepared.thingIndex = thingIndex;
    if (thingIndex >= level.things.size())
    {
        prepared.failure = WorkbenchFailure{
            "thingFrameTargetOutOfRange",
            "The requested Thing does not exist.",
        };
        return prepared;
    }
    if (frames.size() > kMaxThingFrames)
    {
        prepared.failure = WorkbenchFailure{
            "thingFrameLimit",
            "A Thing frame replacement may contain at most 256 frames.",
        };
        return prepared;
    }
    if (std::any_of(
            frames.begin(),
            frames.end(),
            [](const FrameValue& frame)
            {
                return !validFrame(frame);
            }))
    {
        prepared.failure = WorkbenchFailure{
            "invalidThingFrame",
            "Every frame component must be finite and within the editor coordinate bound.",
        };
        return prepared;
    }

    const Thing& thing = level.things[thingIndex];
    for (const TplValue& value : thing.values)
    {
        if (text::iequalsAscii(value.name, "frame")
            && (value.type != ValueType::frame
                || !parse::frameValue(value.asString())))
        {
            prepared.failure = WorkbenchFailure{
                "untypedThingFrame",
                "An existing local frame cannot be parsed safely; the Thing was not changed.",
            };
            return prepared;
        }
    }

    prepared.frames.assign(frames.begin(), frames.end());
    prepared.previousFrameCount = thingCountFrames(thing);
    prepared.pathMove = thingMovesOnPath(level, templates, thing);
    if (prepared.pathMove)
    {
        prepared.pathState = thingInheritedPathFrameState(
            level, templates, thing);
        const std::size_t remaining =
            prepared.pathState.capacity > prepared.pathState.activeFrames
            ? prepared.pathState.capacity - prepared.pathState.activeFrames
            : 0u;
        if (prepared.pathState.capacity != 0u && frames.size() > remaining)
        {
            prepared.failure = WorkbenchFailure{
                "pathFrameCapacityExceeded",
                "The replacement exceeds the remaining inherited Path frame capacity.",
            };
            return prepared;
        }
    }

    prepared.originalTemplateName = thing.templateName;
    prepared.originalValues = thing.values;
    prepared.preparedValues = thing.values;
    prepared.preparedValues.erase(
        std::remove_if(
            prepared.preparedValues.begin(),
            prepared.preparedValues.end(),
            typedFrame),
        prepared.preparedValues.end());
    prepared.preparedValues.reserve(
        prepared.preparedValues.size() + frames.size() + 1u);
    for (const FrameValue& frame : frames)
    {
        TplValue next{};
        next.name = "frame";
        next.type = ValueType::frame;
        next.adjType = tplAdjTypeForName(next.name);
        if (!next.parse(format::frameValue(frame)))
        {
            prepared.failure = WorkbenchFailure{
                "invalidThingFrame",
                "The replacement frame could not be represented as a typed frame value.",
            };
            prepared.preparedValues.clear();
            return prepared;
        }
        prepared.preparedValues.push_back(std::move(next));
    }

    Thing synchronized{};
    synchronized.values = std::move(prepared.preparedValues);
    thingSyncNumFrames(synchronized);
    prepared.preparedValues = std::move(synchronized.values);
    return prepared;
}

} // namespace sed::level
