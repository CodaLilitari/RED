#pragma once

#include "sed/stop_token.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sed
{
struct Level;
}

namespace sed::semantic_preview
{

// These are comparison/detail bounds, independent from file or parser admission.
// Each compare call owns a fresh budget. Public workflows use these defaults;
// the parameters also support focused contract tests.
struct Limits
{
    std::size_t maxVisitedSlots{1000000};
    std::size_t maxComparedStringBytes{4 * 1024 * 1024};
    std::size_t maxChanges{128};
    std::size_t maxInlineStringBytes{512};
    std::size_t maxTotalInlineStringBytes{32 * 1024};

    bool operator==(const Limits&) const = default;
};

inline constexpr Limits kDefaultLimits{};
inline constexpr int kVersion = 1;
inline constexpr std::string_view kCoverage = "level-model-v1";
inline constexpr std::string_view kAddressing = "snapshot-storage-slots";
inline constexpr std::array<std::string_view, 19> kExcludedFields{
    "/sectors/*/surfaces/*/d3dId",
    "/importedJklSurfaceRefs", "/importedJklCogRowsPresent",
    "/retainedJklSourceText", "/retainedJklSourceHash", "/retainedJklUnknownSections",
    "/retainedJklHeaderUnknownLines", "/retainedJklSectorUnknownLines",
    "/importedGeoWorldVertices", "/importedGeoWorldTexVertices", "/importedGeoAdjoinFlags",
    "/importedGeoAdjoinMirrors", "/importedGeoVertexIndices", "/importedGeoSurfaceIndices",
    "/importedGeoTexVertexIndices", "/importedGeoAdjoinIndices", "/importedGeoMaterialIndices",
    "/importedGeoSurfaceCount", "/importedGeoAdjoinCount"
};
inline constexpr std::array<std::string_view, 2> kExcludedScopes{
    "nativeOptionalSectionBytes", "dataOutsideLevel"
};
inline constexpr std::string_view kAuthoredDescription =
    "Captured source model compared with the candidate after all patch operations.";
inline constexpr std::string_view kPersistedDescription =
    "Captured source model compared with the model reloaded from the exact proposed output bytes.";

enum class ChangeKind { Value, ArrayLength, Presence };
enum class Reason { ComparisonSlots, ComparisonStringBytes, ChangeLimit, InlineStringLength, InlineStringBytes };
enum class StringEncoding { Utf8, Hex };
enum class FloatClassification { Finite, NegativeZero, PositiveInfinity, NegativeInfinity, Nan };

struct EnumValue
{
    // All covered enum storage fits losslessly, including uint8_t values > 127.
    std::int64_t value{};
    bool operator==(const EnumValue&) const = default;
};

struct Float32Value
{
    std::uint32_t bits{};
    bool operator==(const Float32Value&) const = default;
};

struct Float64Value
{
    std::uint64_t bits{};
    bool operator==(const Float64Value&) const = default;
};

struct SizeValue
{
    std::size_t value{};
    bool operator==(const SizeValue&) const = default;
};

struct StringValue
{
    std::size_t byteCount{};
    // Disengagement means an explicitly omitted value, not scalar absence.
    // Engaged text is exact UTF-8 or lowercase hex of the original raw bytes.
    std::optional<std::string> encodedValue;
    StringEncoding encoding{StringEncoding::Utf8};
    bool operator==(const StringValue&) const = default;
};

using Value = std::variant<bool, std::int32_t, std::uint32_t, EnumValue,
    Float32Value, Float64Value, SizeValue, StringValue>;

struct Descriptor
{
    // Disengagement is absence, with no invented type or value. Optional-record
    // occupancy is a present boolean, distinct from a missing owner.
    std::optional<Value> value;
    bool operator==(const Descriptor&) const = default;
};

struct Change
{
    std::string path;
    ChangeKind kind{ChangeKind::Value};
    Descriptor before;
    Descriptor after;
    bool operator==(const Change&) const = default;
};

struct Work
{
    std::size_t visitedSlots{};
    std::size_t comparedStringBytes{};
    std::size_t inlineStringBytes{};
    bool operator==(const Work&) const = default;
};

struct Comparison
{
    bool comparisonComplete{true};
    bool detailsComplete{true};
    std::optional<bool> changed{false};
    std::size_t observedChanges{};
    std::optional<std::size_t> totalChanges{0};
    std::vector<Change> changes;
    std::vector<Reason> reasons;
    Work work;
    bool operator==(const Comparison&) const = default;
};

// The stable names are shared by presentation adapters. Float classification
// inspects bits only, including signaling NaNs, without evaluating the number.
[[nodiscard]] std::string_view name(ChangeKind value) noexcept;
[[nodiscard]] std::string_view name(Reason value) noexcept;
[[nodiscard]] std::string_view name(StringEncoding value) noexcept;
[[nodiscard]] std::string_view name(FloatClassification value) noexcept;
[[nodiscard]] FloatClassification classify(Float32Value value) noexcept;
[[nodiscard]] FloatClassification classify(Float64Value value) noexcept;

class ComparisonCancelled : public std::exception
{
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "semantic comparison cancelled";
    }
};

// Compare immutable snapshots in model-field then array-slot order, without a
// Level clone, resource lookup, serialization, or Qt/application dependency.
// Text is admitted before equality/encoding/copying. Budget exhaustion returns
// an explicitly incomplete Comparison; cancellation throws its distinct type
// at the next traversal/text admission. Allocation/programming errors propagate.
[[nodiscard]] Comparison compareLevels(const Level& source, const Level& after,
    const Limits& limits = {}, sed::StopToken cancellation = {});

} // namespace sed::semantic_preview
