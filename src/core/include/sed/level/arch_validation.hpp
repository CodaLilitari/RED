#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/level/arch_lighting.hpp"

namespace sed::assets
{
class Templates;
} // namespace sed::assets

namespace sed::level
{

/** Resolved model facts for one ARCH thing, from explicit resources. */
struct ArchBindingExpectation
{
    std::int32_t thingIndex{ -1 };
    std::string modelName{};
    MotsArchLightingTopology topology{};
    std::optional<std::string> resolvedFingerprintHex{};
};

/** Stored fingerprint recorded for one ARCH thing, when known. */
struct ArchStoredFingerprint
{
    std::int32_t thingIndex{ -1 };
    std::string fingerprintHex{};
};

/** One validation finding. Blocking findings refuse the binding. */
struct ArchBindingIssue
{
    std::string code{};
    std::string message{};
    bool blocking{ true };
    std::int32_t thingIndex{ -1 };
    std::int32_t geosetIndex{ -1 };
};

/** Validation outcome for one ARCH thing. */
struct ArchBindingResult
{
    std::int32_t thingIndex{ -1 };
    bool usable{ false };
    bool fingerprintChecked{ false };
    std::vector<ArchBindingIssue> issues{};
};

/** Whole-level ARCH validation over explicitly supplied models. */
struct ArchValidationReport
{
    std::vector<ArchBindingResult> bindings{};

    bool usable() const noexcept;
};

/**
 * Normalized model fingerprint for the port: SHA-256 of the
 * canonical all-LOD mesh/vertex counts. This is distinct from
 * any raw resource-entry hash.
 */
std::string archModelFingerprintHex(const MotsArchLightingTopology& topology);

/**
 * Reads every declared geoset's per-mesh vertex counts from
 * 3DO text. Row labels are ignored; physical row order defines
 * each slot. Returns no topology (with `error` set) when the
 * text is malformed, truncated, or over budget.
 */
std::optional<MotsArchLightingTopology> readArchModelTopology(
    std::string_view text, std::string* error = nullptr);

/** True when stored arrays satisfy geoset-zero exact + prefix rules. */
bool archTopologyAllowsBinding(
    const MotsArchLightObject& stored,
    const MotsArchLightingTopology& topology,
    std::string* code = nullptr) noexcept;

/**
 * Validates every ARCH thing (flagged, or carrying a binding)
 * against explicitly resolved models. Without an expectation
 * no binding is authorized. A missing stored fingerprint is
 * reported unknown, never as a match.
 */
ArchValidationReport validateArchBindings(
    const Level& level,
    std::span<const ArchBindingExpectation> expectations,
    std::span<const ArchStoredFingerprint> storedFingerprints = {},
    const assets::Templates* templates = nullptr);

} // namespace sed::level
