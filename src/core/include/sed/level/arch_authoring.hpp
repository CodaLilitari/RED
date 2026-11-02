#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sed/level/arch_lighting.hpp"
#include "sed/level/arch_validation.hpp"

namespace sed::assets
{
class Templates;
} // namespace sed::assets

namespace sed::level
{

/** Editor bake recipes. Neither is a recovered retail baker. */
enum class ArchBakeProfile : std::uint8_t
{
    SourcePointsV1,
    SourcePointsWorldOccludedV1,
};

/** Stable token for a bake profile (extension + digests). */
std::string_view archBakeProfileToken(ArchBakeProfile profile) noexcept;

/** Origin token for the SED_ARCH_AUTHORING extension. */
std::string_view archSampleOriginToken(MotsArchSampleOrigin origin) noexcept;

/** Parses an origin token; unknown tokens are refused. */
std::optional<MotsArchSampleOrigin> parseArchSampleOrigin(
    std::string_view token) noexcept;

/** SHA-256 of one object's sample bits, in stored order. */
std::string archSampleDigestHex(const MotsArchLightObject& object);

/** SHA-256 binding ARCH objects and thing bindings (extension). */
std::string archAuthoringChecksumHex(
    std::span<const MotsArchLightObject> objects,
    std::span<const std::pair<std::int32_t, std::int32_t>> bindings);

/** Computed binding status; never stored, never a bake claim by itself. */
enum class ArchBindingStatus : std::uint8_t
{
    Unbound,
    UnknownImported,
    BoundExistingSamples,
    ResetZeroed,
    BakedSamplesIntact,
    BakedCurrentInputs,
    BakedStaleInputs,
    BakedOverridden,
};

/**
 * Status for one thing. `currentBakeInputHex`, when supplied,
 * distinguishes BakedCurrentInputs from BakedStaleInputs;
 * without it a digest-intact bake reports BakedSamplesIntact.
 */
ArchBindingStatus archBindingStatus(
    const Level& level, std::int32_t thingIndex,
    std::optional<std::string_view> currentBakeInputHex = std::nullopt) noexcept;

/** One world triangle that blocks the occluded bake profile. */
struct ArchOccluderTriangle
{
    TVector a{};
    TVector b{};
    TVector c{};
};

/** Input digest for a bake: profile, lights, and sample points. */
std::string archBakeInputDigestHex(
    ArchBakeProfile profile,
    std::span<const SedLight> lights,
    const std::vector<std::vector<TVector>>& meshVertices);

/** Stable refusal for a prepared authoring step. */
struct ArchAuthoringFailure
{
    std::string code{};
    std::string message{};
};

/**
 * A validated ARCH authoring step. Prepare never mutates;
 * `apply` appends its captured object (bind/reset/bake) or
 * clears the binding (unbind), touches only the selected
 * thing, and returns false without mutation when stale.
 * Apply through Document::applyUndoable with
 * DocumentDirty::All so undo restores the exact prior
 * binding, arrays, and provenance.
 */
struct PreparedArchAuthoring
{
    std::optional<ArchAuthoringFailure> failure{};
    std::int32_t thingIndex{ -1 };
    std::int32_t expectedArchIndex{ -2 };
    std::size_t expectedObjectCount{};
    bool appendObject{ false };
    MotsArchLightObject newObject{};
    std::optional<MotsArchAuthoringEntry> newEntry{};
    bool clearEntry{ false };

    bool ok() const noexcept
    {
        return !failure.has_value();
    }

    bool apply(Level& level) const;
};

/** Binds a private copy of an existing address-compatible object. */
PreparedArchAuthoring prepareBindArchSamples(
    const Level& level, std::int32_t thingIndex,
    std::int32_t sourceLightingIndex,
    const MotsArchLightingTopology& topology,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates = nullptr);

/** Replaces the binding with a zeroed allocation for the model. */
PreparedArchAuthoring prepareResetArchBinding(
    const Level& level, std::int32_t thingIndex,
    const MotsArchLightingTopology& topology,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates = nullptr);

/**
 * Bakes geoset-zero samples from editor source lights.
 * `meshVertices` holds world-space points per mesh, aligned
 * to geoset zero. The occluded profile additionally tests
 * each light/sample segment against `occluders`; a light
 * with LF_NoBlock bypasses those tests.
 */
PreparedArchAuthoring prepareBakeArchSamples(
    const Level& level, std::int32_t thingIndex,
    const MotsArchLightingTopology& topology,
    const std::vector<std::vector<TVector>>& meshVertices,
    std::span<const SedLight> lights,
    ArchBakeProfile profile,
    std::span<const ArchOccluderTriangle> occluders,
    std::string modelName, std::string modelEntrySha256Hex,
    const assets::Templates* templates = nullptr);

/** Clears the binding, retaining the old arrays. */
PreparedArchAuthoring prepareUnbindArch(
    const Level& level, std::int32_t thingIndex,
    const assets::Templates* templates = nullptr);

} // namespace sed::level
