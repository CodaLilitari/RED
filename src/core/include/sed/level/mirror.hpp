#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sed/level.hpp"

namespace sed::level
{

/**
 * Mirror floors: a retail-compatible reflection.
 *
 * Jedi Knight cannot render a reflection, but it can draw a translucent floor
 * face that is also a portal. Behind that portal sits an upside-down copy of
 * the room, so the floor shows the room reflected through it at the
 * renderer's fixed translucent blend (about 35% floor, 65% what lies behind).
 * The copy is a closed sector nobody can enter: its only adjoin is the floor
 * portal, and the portal does not allow movement, so the floor still holds
 * the player up.
 *
 * The renderer draws a sector once per frame and clips it by the last portal
 * it came through, so one copy serves one portal. Each mirror floor face gets
 * its own copy.
 */

inline constexpr double kDefaultMirrorReflectivity = 0.5;

enum class MirrorFloorError : std::uint8_t
{
    None,
    InvalidSurface,      ///< the sector or surface does not exist, or has under 3 corners
    Adjoined,            ///< the floor face is already a portal
    NotFloor,            ///< the face is not a flat, horizontal face looking up into its room
    RoomBelowFloor,      ///< part of the room lies below the floor plane (not convex)
    InvalidReflectivity, ///< reflectivity outside [0, 1]
    InvalidMaterial,     ///< the opening material index is out of range
    NoOpeningMaterial,   ///< a doorway needs a wall material and none can be found
    NonFinite,           ///< reflected geometry is not finite
};

struct MirrorFloorOptions
{
    /** Scales the light copied into the reflection, 0..1. */
    double reflectivity{ kDefaultMirrorReflectivity };
    /**
     * Material for the copy's doorways, which become walls (the copy has
     * nothing beyond them). -1 takes the material of a wall in the same plane
     * as the doorway, else the room's most used wall material.
     */
    std::int32_t openingMaterialIndex{ -1 };
};

struct MirrorFloorReport
{
    MirrorFloorError error{ MirrorFloorError::None };
    std::int32_t mirrorSector{ -1 };
    std::int32_t mirrorSurface{ -1 };
    /** Surfaces of the copy that were doorways in the room. */
    std::vector<std::int32_t> sealedOpenings{};
    /** Of those, the ones textured as a continuation of a wall in their plane. */
    std::size_t openingsContinued{};
};

/**
 * Builds the reflected copy of `floor`'s room across the floor plane and makes
 * the floor a translucent, walk-on portal into it. The copy's vertices are
 * mirrored, each face's corner order is reversed (a reflection flips winding)
 * and every face keeps its texture coordinates, which is exactly what makes
 * the texture appear mirrored. The room's baked light is copied, scaled by the
 * reflectivity. The copy is appended as the last sector, so existing sector
 * and surface numbers do not change. Returns false and leaves `level`
 * unchanged on any error.
 */
bool levelAddMirrorFloor(Level& level, SurfaceRef floor, const MirrorFloorOptions& options,
    MirrorFloorReport* report = nullptr);

struct MirrorFloorPortal
{
    SurfaceRef floor{};
    SurfaceRef mirror{};
};

/**
 * Finds the mirror floors in a level by geometry: a translucent portal that
 * does not allow movement, whose far sector is exactly the reflection of the
 * near one across the portal's plane, face for face and corner for corner,
 * and adjoins nothing else. A room edited after its mirror was built no
 * longer matches and is not listed; rebuild the mirror.
 */
std::vector<MirrorFloorPortal> levelFindMirrorFloors(const Level& level);

/**
 * Copies each mirror floor's room light into its reflection: every corner's
 * stored light and the sector's ambient and extra light, scaled by
 * `reflectivity`. Returns the number of mirrors refreshed; an invalid
 * reflectivity refreshes none.
 */
std::size_t levelRefreshMirrorFloors(Level& level, double reflectivity);

} // namespace sed::level
