#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sed/level.hpp"
#include "sed/level/workbench.hpp"
#include "sed/parse_frame.hpp"

namespace sed::assets
{
class Templates;
} // namespace sed::assets

namespace sed::level
{

/** Most local `frame` values one frame-list replacement may author. */
inline constexpr std::size_t kMaxThingFrames = 256;

/**
 * Inherited Path frame buffer of one placed Thing.
 *
 * The first positive `numframes` met in base-to-derived template token order
 * sets `capacity`; a template `frame` before that allocation or beyond the
 * capacity is ignored by the runtime. Embedded Level templates win; the
 * project template database is consulted for a parameter only when the
 * embedded chain contributes no occurrence of it. AI waypoint frames are
 * instance-local and are never counted here.
 */
struct ThingPathFrameState
{
    std::size_t capacity{};
    std::size_t activeFrames{};
    std::size_t ignoredFrames{};
};

ThingPathFrameState thingInheritedPathFrameState(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing);

/** Whether the Thing's effective `move` parameter is `path`. */
bool thingMovesOnPath(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing);

/**
 * A validated replacement of one Thing's local `frame` list.
 *
 * Applying it is the GUI frame editor's Delete (every local frame) followed
 * by Add (each new frame in order): other values keep their order, the new
 * frames are appended, and `numframes` is synchronized to precede them (or
 * removed for an empty list). Apply through `Document::applyUndoable`.
 */
struct PreparedThingFrames
{
    std::optional<WorkbenchFailure> failure{};
    std::size_t thingIndex{};
    std::vector<FrameValue> frames{};
    std::size_t previousFrameCount{};
    bool pathMove{};
    ThingPathFrameState pathState{};
    std::string originalTemplateName{};
    std::vector<TplValue> originalValues{};
    std::vector<TplValue> preparedValues{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }

    /** Writes the prepared list; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * Prepares a replacement of one Thing's local frames.
 *
 * Refuses an unknown Thing (`thingFrameTargetOutOfRange`), more than
 * kMaxThingFrames frames (`thingFrameLimit`), a component that is not finite
 * or exceeds kMaxWorkbenchCoordinate in magnitude (`invalidThingFrame`), a
 * local `frame` value the editor cannot parse (`untypedThingFrame`), and,
 * for a Thing whose effective `move` is `path` with an inherited buffer, a
 * list that would exceed the remaining capacity, the same limit the GUI
 * frame editor enforces (`pathFrameCapacityExceeded`).
 */
PreparedThingFrames prepareSetThingFrames(
    const Level& level,
    const assets::Templates* templates,
    std::size_t thingIndex,
    std::span<const FrameValue> frames);

} // namespace sed::level
