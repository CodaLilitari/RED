#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/assets/jkl.hpp"
#include "sed/level.hpp"

namespace sed::level
{

/** Scope of the F-13 inference: static loader storage, never a runtime test. */
inline constexpr std::string_view kDynamicVertexColorScope =
    "Static loader-storage inference for proposed serialized output. "
    "Visibility, COG execution, networking and retail runtime parity are "
    "not tested. Flags and fullbright/hidden/adjoin modes do not promote "
    "RGB storage. No values are changed.";

/** One corner's final serialized lighting values, in row order W/R/G/B. */
struct DynamicVertexColorSample
{
    float white{};
    float red{};
    float green{};
    float blue{};

    constexpr bool operator==(const DynamicVertexColorSample&) const noexcept = default;
};

/** Per-loader RGB storage inferred for one serialized surface row. */
enum class VertexRgbStorage : std::uint8_t
{
    Retained,
    Compacted,
    Scalar,
    Unknown,
    Invalid,
};

std::string_view vertexRgbStorageName(VertexRgbStorage storage) noexcept;

/**
 * Aggregate authoring verdict for one surface. `Ready` means RGB storage
 * survives in both inspected loaders; `NotReady` means the inspected
 * retail loader compacts the row away (current OpenMotS still retains a
 * `4N` row, which the per-loader fields keep visible); `Unknown` means
 * the bytes do not support a verdict (scalar retail handling, a decimal
 * conversion disagreement, or invalid/non-finite data). The verdict is
 * never repaired by offsetting a value or by an epsilon comparison.
 */
enum class VertexColorReadiness : std::uint8_t
{
    Ready,
    NotReady,
    Unknown,
};

std::string_view vertexColorReadinessName(VertexColorReadiness readiness) noexcept;

/** Readiness of one row, with the first corner that keeps RGB storage. */
struct DynamicVertexColorEvaluation
{
    VertexRgbStorage retail{ VertexRgbStorage::Invalid };
    VertexRgbStorage openMots{ VertexRgbStorage::Invalid };
    VertexColorReadiness readiness{ VertexColorReadiness::Unknown };
    std::optional<std::int32_t> distinguishingCorner{};
    std::string reason{};
};

/**
 * Evaluates final `float32` corner samples with exact equality and no
 * clamping. `componentsPerCorner` is the serialized row width: `1` for a
 * scalar `N` row, `4` for a `4N` row. Blue never affects the retail
 * compaction predicate, but every channel (blue included) must be finite.
 * Corner counts outside 3..24 and any other row width are `Invalid`.
 */
DynamicVertexColorEvaluation evaluateDynamicVertexColors(
    std::span<const DynamicVertexColorSample> samples,
    int componentsPerCorner);

/** One assessed surface row from proposed serialized output. */
struct SurfaceVertexColorReadiness
{
    std::int32_t globalSurfaceIndex{ -1 };
    std::int32_t sectorIndex{ -1 };
    std::int32_t surfaceIndex{ -1 };
    std::int32_t cornerCount{};
    int componentsPerCorner{};
    std::string layout{};
    VertexRgbStorage retail{ VertexRgbStorage::Invalid };
    VertexRgbStorage openMots{ VertexRgbStorage::Invalid };
    VertexColorReadiness readiness{ VertexColorReadiness::Unknown };
    std::optional<std::int32_t> distinguishingCorner{};
    std::int32_t geometryMode{};
    std::int32_t lightingMode{};
    bool adjoined{};
    std::string reason{};
};

/**
 * Whole-output assessment. `status` is `assessed`, `unavailable`
 * (serialization, geometry, identities, non-finite values or row layout
 * prevented assessment), or `unsupported-game` (outside the MotS scope).
 * A successful call can still carry `unavailable`; always inspect
 * `status` and the per-surface outcomes before deciding anything.
 */
struct DynamicVertexColorReport
{
    std::string status{};
    std::string proposedOutputSha256{};
    std::optional<bool> geometryValid{};
    std::vector<SurfaceVertexColorReadiness> surfaces{};
    std::string detail{};
};

/** Counts surfaces in `report` whose aggregate verdict is `readiness`. */
std::size_t countVertexColorReadiness(
    const DynamicVertexColorReport& report,
    VertexColorReadiness readiness) noexcept;

/** Counts surfaces in `report` with one per-loader storage outcome. */
std::size_t countVertexRgbStorage(
    const DynamicVertexColorReport& report,
    VertexRgbStorage storage,
    bool retail) noexcept;

/**
 * Assesses the exact serialized rows in `outputText` (the proposed plan
 * or output bytes), never an in-memory color state. `isMots` states the
 * document scope explicitly because scalar rows alone cannot prove it;
 * non-MotS bytes report `unsupported-game` instead of a guessed verdict.
 * Malformed, truncated or non-finite rows report `unavailable` with the
 * assessed bytes' SHA-256 and no surfaces. This function changes nothing.
 */
DynamicVertexColorReport evaluateDynamicVertexColorsSerialized(
    std::string_view outputText,
    bool isMots);

/**
 * Builds the proposed export of `level` and assesses those exact bytes,
 * so `proposedOutputSha256` binds to the bytes a plan would write.
 * Sector/surface addresses and render modes come from `level` in writer
 * order. A failed build reports `unavailable` before any byte is written.
 */
DynamicVertexColorReport evaluateDynamicVertexColors(
    const Level& level,
    const assets::JklSaveOptions& options = {});

} // namespace sed::level
