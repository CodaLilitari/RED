#pragma once

#include <cstddef>
#include <vector>

#include "sed/level.hpp"

namespace sed::level
{

/** Completion is separate from findings: incomplete scans never mean clean. */
enum class FanDegeneracyStatus
{
    Complete,
    BudgetExceeded,
    InvalidGeometry,
    AllocationFailure,
    NotImplemented,
};

/** Exact degeneracy class of one fan triangle; advisory only. */
enum class FanDegeneracyKind
{
    /** Two fan corners share one stored position. */
    CoincidentCorners,
    /** Distinct fan corners whose cross product is exactly zero. */
    CollinearCorners,
};

struct FanDegeneracyIssue
{
    SurfaceRef surface{};
    /** Fan triangle (corner 0, fanIndex, fanIndex + 1) of the loop. */
    std::size_t fanIndex{};
    FanDegeneracyKind kind{FanDegeneracyKind::CollinearCorners};
};

/** Aggregate per-call limits, counted before findings are interpreted. */
struct FanDegeneracyLimits
{
    std::size_t maxFanTriangles{4000000};
    std::size_t maxIssues{100000};
};

struct FanDegeneracyReport
{
    FanDegeneracyStatus status{FanDegeneracyStatus::NotImplemented};
    std::vector<FanDegeneracyIssue> issues{};
};

/**
 * Read-only exact fan-degeneracy inspection.
 *
 * Surfaces render as fans around loop corner 0. This reports only
 * triangles that are degenerate for the stored coordinates themselves:
 * coincident corners, or a cross product that is exactly zero. Near-zero
 * area is a heuristic other tooling may apply; it is not reported here,
 * so a clean report does not certify fan quality, convexity, planarity
 * or engine safety. Surfaces with fewer than three corners have no fan
 * triangles and are out of scope. Invalid vertex references and
 * non-finite coordinates refuse as InvalidGeometry rather than presenting
 * a complete empty report. Budget and refusal results may carry the
 * findings prefix gathered so far; callers MUST inspect status before
 * interpreting absence or completeness. No repair is offered: removing a
 * collinear corner can rewrite reciprocal adjoins, UVs and lighting, so
 * mutation needs its own proven design.
 */
[[nodiscard]] FanDegeneracyReport inspectFanDegeneracies(
    const Level& level, const FanDegeneracyLimits& limits = {}) noexcept;

} // namespace sed::level
