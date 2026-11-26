#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sed/geometry.hpp"

namespace sed
{
struct Level;
}

namespace sed::level
{
/** What the retail JK and MotS renderer can hold for one frame.
 *
 * `sithRender` collects the sectors a frame reaches through its portals in a
 * fixed array of 128. JK.EXE 1.01 checks the count before each write and
 * returns without walking the sector, so the sectors past 128 are not drawn
 * and their part of the screen stays black. Translucent portal faces go in a
 * second array of 32; the ones past it are not drawn. OpenJKDF2 raises both
 * limits, so a level over them can look fine there and still fail in the
 * retail game. The MotS values follow OpenJKDF2's reconstruction and have
 * not been read from JKM.EXE.
 */
inline constexpr std::size_t kRetailVisibleSectors = 128;
inline constexpr std::size_t kRetailAlphaSurfaces = 32;

/** The view the retail game uses: 90 degrees across a 640 by 480 canvas. */
struct RenderCamera
{
    double horizontalFovDegrees{ 90.0 };
    double width{ 640.0 };
    double height{ 480.0 };
    double nearPlane{ 1.0 / 64.0 };
};

struct RenderFrame
{
    /// False when the work limit ended the walk early; the counts are then a lower bound.
    bool complete{ true };
    /// The distinct sectors the frame reaches, in the order the engine lists them.
    std::vector<std::int32_t> sectors{};
    /// Translucent portal faces of those sectors that face the camera.
    std::size_t alphaSurfaces{};
    std::size_t work{};
    /// Entries through a visible portal, including repeat visits by different paths.
    std::size_t portalCrossings{};
};

/** Walks one frame's portals as `sithRender_BuildVisibleSectorList` does.
 *
 * From `cameraSector` the walk enters each neighbour whose portal face looks
 * at the eye, lies inside the current view rectangle and is see-through
 * (visible adjoin flag and no drawn geometry, or a translucent face). The
 * neighbour is then walked with the view narrowed to the screen rectangle
 * around the visible part of that portal. A portal that crosses the near
 * plane passes the view on unchanged, see-through or not. A sector is listed
 * once, but is walked again whenever another chain of portals reaches it;
 * only the sectors of the chain in progress are skipped.
 *
 * Yaw 0 looks along +Y and a positive yaw turns left; a positive pitch looks
 * up. A see-through MAT texture cannot be seen from the level alone, so a
 * drawn, opaque-flagged portal face counts as a wall.
 */
RenderFrame traceRenderFrame(const Level& level, std::int32_t cameraSector, const TVector& eye, double yawDegrees,
    double pitchDegrees, const RenderCamera& camera = {}, std::size_t workLimit = 4'000'000);

struct RenderHeading
{
    double yawDegrees{};
    double pitchDegrees{};
};

struct RenderViewpoint
{
    std::int32_t sector{ -1 };
    TVector position{};
    /// A fixed frame in place of the heading sweep.
    std::optional<RenderHeading> heading{};
};

/** One viewpoint in the middle of every closed sector (its corner average). */
std::vector<RenderViewpoint> sectorCentreViewpoints(const Level& level);

/** The first sector whose faces all look at `position`, if any. */
std::optional<std::int32_t> sectorContainingPoint(const Level& level, const TVector& position);

struct RenderBudgetOptions
{
    RenderCamera camera{};
    /// Headings tried at each viewpoint, evenly spaced from yaw 0.
    std::size_t yawSteps{ 16 };
    std::vector<double> pitchDegrees{ 0.0, -30.0, 30.0 };
    /// Total portal tests across all frames before the audit stops.
    std::size_t workLimit{ 400'000'000 };
    std::size_t frameWorkLimit{ 4'000'000 };
};

/** A viewpoint's heaviest frame. */
struct RenderBudgetView
{
    std::int32_t sector{ -1 };
    TVector position{};
    double yawDegrees{};
    double pitchDegrees{};
    std::size_t visibleSectors{};
    /// The most translucent faces any of its frames queued (not necessarily the same frame).
    std::size_t alphaSurfaces{};
    RenderHeading alphaHeading{};
    /// Portal tests used by the listed sector peak, including a budget-limited frame.
    std::size_t sectorFrameWork{};
    bool complete{ true };
};

struct RenderBudgetReport
{
    std::size_t viewpoints{};
    std::size_t frames{};
    std::size_t overSectorLimit{}; ///< Viewpoints with a frame above kRetailVisibleSectors.
    std::size_t overAlphaLimit{};  ///< Viewpoints with a frame above kRetailAlphaSurfaces.
    /// False when a frame or the audit ran out of work; later viewpoints were then not checked.
    bool complete{ true };
    /// One entry per checked viewpoint, heaviest first (ties by sector index).
    std::vector<RenderBudgetView> views{};
};

/** Traces `yawSteps` headings at each pitch from every viewpoint and keeps each one's heaviest frame. */
RenderBudgetReport auditRenderBudget(const Level& level, std::span<const RenderViewpoint> viewpoints,
    const RenderBudgetOptions& options = {});
}
