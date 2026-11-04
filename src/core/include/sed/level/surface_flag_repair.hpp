#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <span>

namespace sed::level
{

/** Legacy SED Recovery > Fix solid surface(s).
 *
 * Each referenced unadjoined surface is drawn textured, gains collision, and
 * has its floor flag recomputed from its normal (legacy CheckIfFloor).
 * Adjoined surfaces and invalid references are skipped. Returns the number of
 * surfaces whose stored values changed.
 */
std::size_t levelFixSolidSurfaces(Level& level, std::span<const SurfaceRef> surfaces) noexcept;

/** Legacy SED Recovery > Fix passable adjoin(s).
 *
 * Each referenced adjoined surface is made not drawn, visible, and passable,
 * and loses its AI/player movement blocks. Other adjoin flags are preserved
 * and only the referenced side changes. Unadjoined surfaces and invalid
 * references are skipped. Returns the number of surfaces changed.
 */
std::size_t levelFixPassableAdjoins(Level& level, std::span<const SurfaceRef> surfaces) noexcept;

} // namespace sed::level
