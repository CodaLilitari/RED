#pragma once

#include <optional>
#include <string_view>

#include "sed/geometry.hpp"
#include "sed/level.hpp"

namespace sed::level
{

/** One computed overview camera (D-09 Wren framing).
 * fieldOfViewDegrees is set for perspective views and
 * orthographicHeight for orthographic ones; the other stays 0. */
struct OverviewCamera
{
    TVector position{};
    TVector target{};
    bool orthographic{};
    double fieldOfViewDegrees{};
    double orthographicHeight{};
    double nearPlane{};
    double farPlane{};
};

inline constexpr std::string_view kOverviewOverhead = "overhead";
inline constexpr std::string_view kOverviewThreeQuarter = "threequarter";
inline constexpr std::string_view kOverviewWren = "wren";
inline constexpr std::string_view kOverviewContain = "contain";
inline constexpr double kOverviewDefaultMargin = 0.025;
inline constexpr double kOverviewMaximumMargin = 0.25;

/** The drawn-world-surface predicate shared with scene assembly:
 * geo != 0, at least three corners, and a named material. */
bool isDrawnWorldSurface(const Surface& surface) noexcept;

/** Axis-aligned box of every corner of every drawn world surface,
 * or nullopt when the level draws nothing. Throws std::runtime_error
 * on non-finite vertex positions. */
std::optional<TBox> worldSurfaceBounds(const Level& level);

/** Frames bounds for a width x height image. view is "overhead"
 * (orthographic plan, north +Y up) or "threequarter" (perspective
 * from the south-east, above); framing is "wren" (fixed distance,
 * may crop near corners) or "contain" (back off until the box fits
 * inside the margin). Throws std::invalid_argument for unknown
 * view/framing, non-positive dimensions, or margin outside
 * 0..kOverviewMaximumMargin; std::runtime_error for an empty or
 * X/Y-degenerate box. */
OverviewCamera computeOverviewCamera(const TBox& bounds, std::string_view view,
                                     std::string_view framing, int width, int height,
                                     double margin);

} // namespace sed::level
