#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace sed::level
{

enum class VertexPositionStatus
{
    Changed,
    Unchanged,
    InvalidTarget,
    InvalidPosition,
    GeometryUnsupported,
    AdjoinUnsupported,
    ReadOnlySector,
    WorkLimitExceeded,
    AllocationFailed,
};

struct VertexPositionOptions
{
    /** Optional caller policy; the GUI admits finite model coordinates. */
    double coordinateLimit{ std::numeric_limits<double>::infinity() };
    std::size_t maxFaceCorners{ std::numeric_limits<std::size_t>::max() };
    /** Cumulative geometry, correspondence and read-only lookup work. */
    std::size_t workLimit{ 8000000 };
    std::span<const std::int32_t> readOnlySectors{};
};

struct VertexPositionResult
{
    VertexPositionStatus status{ VertexPositionStatus::Unchanged };
    /** Static diagnostic; no borrowed model or temporary string lifetime. */
    std::string_view message{};
};

/** Sets an absolute position and exact reverse-cyclic reciprocal portal copies.
 * Merely coincident unadjoined vertices remain independent. Validates all touched
 * closed convex sectors before publishing any change; every refusal is atomic.
 * Keeps IDs, corner order, UV/light data, marks and references. Recalculates face
 * normals. A valid no-op still performs admission and reports Unchanged only
 * when both positions and derived normals remain unchanged.
 */
[[nodiscard]] VertexPositionResult levelSetVertexPositionChecked(Level& level,
    VertexRef vertex, const TVector& position, const VertexPositionOptions& options = {});

} // namespace sed::level
