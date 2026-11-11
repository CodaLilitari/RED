#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sed/level.hpp"

namespace sed::level
{
struct LightLimits
{
    double floor{};
    double cap{ 1.0 };
};

enum class LightLimitsStatus { Ok, InvalidProfile, NonFiniteInput };

/** Explicit post-bake policy: min(cap, floor + baked).
 * JKDF2 uses the double-precision mean of fresh RGB as baked scalar and assigns
 * the result to RGB and a (the VERSION 1 exported mono field). Stale a is ignored.
 * MotS/IJIM apply the policy per RGB component and preserve alpha. DroidWorks
 * and unknown project kinds return InvalidProfile without mutation.
 * Call only on fresh baked output, never cumulatively on prior policy output.
 * Omitted CLI policy must bypass this helper entirely. Preflights all RGB before
 * mutation; preserves all other state, including sector ambient.
 * This is not a rendered-brightness guarantee and does not rederive ambient.
 * Floor is an additive baseline, not a lower clamp: finite negative baked
 * channels may remain below it. Results are stored as float, so rounding can
 * put the stored cap slightly above or below its requested double value.
 */
LightLimitsStatus applyLightLimits(Level& level, const LightLimits& limits) noexcept;

inline constexpr std::size_t kMaxLightDiagnostics = 4096;
struct LightDiagnosticOptions
{
    double nearBlack{ 0.01 };
    double cap{ 1.0 };
    std::size_t maxDiagnostics{ 256 };
};
struct LightCornerDiagnostic
{
    std::size_t sectorIndex{};
    std::int32_t sectorId{};
    std::size_t surfaceIndex{};
    std::int32_t surfaceNum{};
    std::size_t cornerIndex{};
    bool nearBlack{};
    bool atOrAboveCap{};
    bool overUnit{};
};
struct LightDiagnosticReport
{
    LightLimitsStatus status{ LightLimitsStatus::Ok };
    std::size_t corners{};
    std::size_t nearBlackCorners{};
    std::size_t cappedCorners{};
    std::size_t overUnitCorners{};
    double minimum{};
    double maximum{};
    bool truncated{};
    std::vector<LightCornerDiagnostic> diagnostics;
};
/** Read-only stored-RGB statistics, not pixel/occlusion/runtime diagnostics.
 * Near-black means all channels <= threshold; capped means any channel >= the
 * float representation of cap (the same rounding used for stored policy output);
 * over-unit means any channel > 1. Counts cover all corners despite truncation.
 * Invalid options or nonfinite RGB return no partial report. Empty min/max = 0.
 * Storage is bounded by maxDiagnostics <= kMaxLightDiagnostics. Allocation
 * failures may throw but never mutate the input level.
 */
LightDiagnosticReport inspectLightLimits(
    const Level& level, const LightDiagnosticOptions& options = {});
}
