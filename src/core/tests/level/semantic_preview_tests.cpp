#include "sed/level/semantic_preview.hpp"

#include "sed/level.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

namespace
{
namespace preview = sed::semantic_preview;

int failures{};

void check(bool condition, std::string_view message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const preview::Change* row(const preview::Comparison& result, std::string_view path)
{
    const auto found = std::find_if(result.changes.begin(), result.changes.end(),
        [path](const auto& change) { return change.path == path; });
    check(found != result.changes.end(), std::string("expected change at ") + std::string(path));
    return found == result.changes.end() ? nullptr : &*found;
}

template<typename T>
const T* value(const preview::Descriptor& descriptor)
{
    const auto* result = descriptor.value ? std::get_if<T>(&*descriptor.value) : nullptr;
    check(result != nullptr, "descriptor preserves its typed stored value");
    return result;
}

void metadataAndNoOp()
{
    check(preview::kVersion == 1 && preview::kCoverage == "level-model-v1"
            && preview::kAddressing == "snapshot-storage-slots", "version and positional coverage remain explicit");
    check(preview::kExcludedFields.size() == 19
            && preview::kExcludedFields.front() == "/sectors/*/surfaces/*/d3dId"
            && preview::kExcludedFields.back() == "/importedGeoAdjoinCount"
            && preview::kExcludedScopes[0] == "nativeOptionalSectionBytes"
            && preview::kExcludedScopes[1] == "dataOutsideLevel", "exclusions are available without agent or Qt types");
    const auto limits = preview::kDefaultLimits;
    check(limits.maxVisitedSlots == 1000000 && limits.maxComparedStringBytes == 4194304
            && limits.maxChanges == 128 && limits.maxInlineStringBytes == 512
            && limits.maxTotalInlineStringBytes == 32768, "shared defaults retain the legacy bounds");
    const sed::Level source;
    const auto result = preview::compareLevels(source, source);
    check(result.comparisonComplete && result.detailsComplete && result.changed == false
            && result.observedChanges == 0 && result.totalChanges == 0
            && result.changes.empty() && result.reasons.empty(), "complete no-op is exact");
    check(result.work.visitedSlots == 71 && result.work.comparedStringBytes == 0
            && result.work.inlineStringBytes == 0, "empty model still charges all 71 stored slots");
}

void soundPreloadState()
{
    sed::Level source;
    source.jklSoundPreloads.rows = {"old.wav"};
    auto after = source;
    after.jklSoundPreloads.capacityBase = 12;
    after.jklSoundPreloads.rows[0] = "new.wav";
    const auto result = preview::compareLevels(source, after);
    check(result.comparisonComplete && result.totalChanges == 2,
        "sound preload capacity and ordered WAV entries both participate in the saved comparison");
    (void)row(result, "/jklSoundPreloads/capacityBase");
    (void)row(result, "/jklSoundPreloads/rows/0");
}

void exactTypedValues()
{
    sed::Level source;
    source.sectors.resize(1);
    source.sectors[0].surfaces.resize(1);
    source.header.gravity = std::bit_cast<double>(std::uint64_t{0x8000000000000000});
    source.sectors[0].surfaces[0].uScale = std::bit_cast<float>(std::uint32_t{0x7fc00041});
    source.masterCMP = "before";
    auto after = source;
    after.header.gravity = 0.0;
    after.header.fog.enabled = true;
    after.exportBufferExtras.things = 0xffffffffu;
    after.kind = static_cast<sed::ProjectType>(-17);
    after.uvCoordinateSpace = static_cast<sed::UvCoordinateSpace>(255);
    after.sectors[0].flags = std::numeric_limits<std::int32_t>::min();
    after.sectors[0].surfaces[0].uScale = std::bit_cast<float>(std::uint32_t{0x7f800001});
    after.masterCMP = std::string("\xff\0", 2);
    const auto result = preview::compareLevels(source, after);
    check(result.comparisonComplete && result.detailsComplete && result.changed == true
            && result.totalChanges == 8, "typed result counts stored differences exactly");
    if ( const auto* change = row(result, "/header/gravity") )
    {
        const auto* before = value<preview::Float64Value>(change->before);
        const auto* current = value<preview::Float64Value>(change->after);
        check(before && current && before->bits == 0x8000000000000000ull && current->bits == 0,
            "float64 signed zero is preserved as exact bits");
    }
    if ( const auto* change = row(result, "/sectors/0/surfaces/0/uScale") )
    {
        const auto* before = value<preview::Float32Value>(change->before);
        const auto* current = value<preview::Float32Value>(change->after);
        check(before && current && before->bits == 0x7fc00041u && current->bits == 0x7f800001u,
            "quiet and signaling NaN payloads remain exact");
    }
    if ( const auto* change = row(result, "/exportBufferExtras/things") )
    {
        const auto* current = value<std::uint32_t>(change->after);
        check(current && *current == 0xffffffffu, "unsigned storage remains distinct from signed flags");
    }
    if ( const auto* change = row(result, "/sectors/0/flags") )
    {
        const auto* current = value<std::int32_t>(change->after);
        check(current && *current == std::numeric_limits<std::int32_t>::min(), "signed flags are not normalized");
    }
    if ( const auto* change = row(result, "/kind") )
    {
        const auto* current = value<preview::EnumValue>(change->after);
        check(current && current->value == -17, "raw signed enum code survives");
    }
    if ( const auto* change = row(result, "/uvCoordinateSpace") )
    {
        const auto* current = value<preview::EnumValue>(change->after);
        check(current && current->value == 255, "raw unsigned enum code is not sign extended");
    }
    if ( const auto* change = row(result, "/header/fog/enabled") )
    {
        const auto* current = value<bool>(change->after);
        check(current && *current, "boolean storage remains boolean");
    }
    if ( const auto* change = row(result, "/masterCMP") )
    {
        const auto* current = value<preview::StringValue>(change->after);
        check(current && current->byteCount == 2 && current->encoding == preview::StringEncoding::Hex
                && current->encodedValue == "ff00", "invalid UTF-8 retains raw byte count and exact hex encoding");
    }
    check(std::bit_cast<std::uint64_t>(source.header.gravity) == 0x8000000000000000ull
            && std::bit_cast<std::uint32_t>(after.sectors[0].surfaces[0].uScale) == 0x7f800001u,
        "both snapshots remain immutable");
}

void optionalPresenceAndArrayOrder()
{
    sed::Level source;
    source.sectors.resize(1);
    source.sectors[0].surfaces.resize(1);
    auto after = source;
    after.sectors[0].surfaces[0].adjoin = sed::SurfaceRef{2, 3};
    const auto result = preview::compareLevels(source, after);
    check(result.totalChanges == 3 && result.changes.size() == 3,
        "optional record occupancy and children remain separate changes");
    if ( const auto* change = row(result, "/sectors/0/surfaces/0/adjoin") )
    {
        const auto* before = value<bool>(change->before);
        const auto* current = value<bool>(change->after);
        check(change->kind == preview::ChangeKind::Presence && before && current && !*before && *current,
            "present owner with disengaged optional has a present false occupancy value");
    }
    if ( const auto* change = row(result, "/sectors/0/surfaces/0/adjoin/sector") )
    {
        check(!change->before.value && change->after.value.has_value(), "missing scalar has no fabricated payload");
    }
    source = {};
    after = {};
    after.materials.push_back("new.mat");
    const auto added = preview::compareLevels(source, after);
    check(added.totalChanges == 2 && added.changes.size() == 2
            && added.changes[0].path == "/materials" && added.changes[0].kind == preview::ChangeKind::ArrayLength
            && added.changes[1].path == "/materials/0", "array length precedes storage-slot changes");
    if ( const auto* change = row(added, "/materials") )
    {
        const auto* before = value<preview::SizeValue>(change->before);
        const auto* current = value<preview::SizeValue>(change->after);
        check(before && current && before->value == 0 && current->value == 1, "array sizes preserve integral storage");
    }
    after = {};
    after.motsArchLightingBakeBaselines.resize(1);
    const auto owner = preview::compareLevels(source, after);
    if ( const auto* change = row(owner, "/motsArchLightingBakeBaselines/0/originalArchLighting") )
    {
        const auto* current = value<bool>(change->after);
        check(!change->before.value && current && !*current,
            "missing owner is distinct from an owner with an absent composite");
    }
    source.lights.resize(1);
    after = source;
    after.lights[0].rgbIntensity = 0.0;
    const auto optional = preview::compareLevels(source, after);
    if ( const auto* change = row(optional, "/lights/0/rgbIntensity") )
    {
        const auto* current = value<preview::Float64Value>(change->after);
        check(optional.totalChanges == 1 && change->kind == preview::ChangeKind::Presence
                && !change->before.value && current && current->bits == 0, "optional zero differs from absence");
    }
}

void budgetBoundaries()
{
    const sed::Level empty;
    auto limits = preview::kDefaultLimits;
    limits.maxVisitedSlots = 70;
    const auto shortScan = preview::compareLevels(empty, empty, limits);
    check(!shortScan.comparisonComplete && !shortScan.detailsComplete && !shortScan.changed
            && !shortScan.totalChanges && shortScan.work.visitedSlots == 70
            && shortScan.reasons == std::vector{preview::Reason::ComparisonSlots},
        "slot exhaustion reports unknown change state and an exact admitted prefix");
    limits.maxVisitedSlots = 71;
    check(preview::compareLevels(empty, empty, limits).comparisonComplete, "exact slot boundary completes");
    sed::Level source;
    source.masterCMP = "ab";
    auto after = source;
    after.masterCMP = "cd";
    limits = {};
    limits.maxComparedStringBytes = 3;
    const auto textShort = preview::compareLevels(source, after, limits);
    check(!textShort.comparisonComplete && textShort.work.comparedStringBytes == 0
            && textShort.changes.empty() && textShort.reasons == std::vector{preview::Reason::ComparisonStringBytes},
        "text comparison admission is atomic and precedes equality or descriptors");
    limits.maxComparedStringBytes = 4;
    const auto textExact = preview::compareLevels(source, after, limits);
    check(textExact.comparisonComplete && textExact.totalChanges == 1 && textExact.work.comparedStringBytes == 4,
        "exact paired string boundary completes");
    limits = {};
    limits.maxChanges = 0;
    limits.maxInlineStringBytes = 0;
    const auto noDetails = preview::compareLevels(source, after, limits);
    check(noDetails.comparisonComplete && !noDetails.detailsComplete && noDetails.totalChanges == 1
            && noDetails.changes.empty() && noDetails.work.inlineStringBytes == 0
            && noDetails.reasons == std::vector{preview::Reason::ChangeLimit},
        "discarded rows still count without constructing or charging omitted descriptors");
    source.masterCMP = "abc";
    after.masterCMP = "defg";
    limits = {};
    limits.maxInlineStringBytes = 3;
    const auto omitted = preview::compareLevels(source, after, limits);
    if ( const auto* change = row(omitted, "/masterCMP") )
    {
        const auto* before = value<preview::StringValue>(change->before);
        const auto* current = value<preview::StringValue>(change->after);
        check(before && current && before->encodedValue == "abc" && !current->encodedValue
                && current->byteCount == 4 && omitted.work.inlineStringBytes == 3
                && omitted.reasons == std::vector{preview::Reason::InlineStringLength},
            "omitted present string is distinct from absent value and never clipped");
    }
    limits.maxInlineStringBytes = 4;
    limits.maxTotalInlineStringBytes = 6;
    const auto aggregate = preview::compareLevels(source, after, limits);
    check(aggregate.comparisonComplete && !aggregate.detailsComplete && aggregate.work.inlineStringBytes == 3
            && aggregate.reasons == std::vector{preview::Reason::InlineStringBytes},
        "aggregate inline allowance admits before then after in fixed order");
}

void classificationAndNames()
{
    check(preview::classify(preview::Float32Value{0x80000000u}) == preview::FloatClassification::NegativeZero
            && preview::classify(preview::Float32Value{0x7f800000u}) == preview::FloatClassification::PositiveInfinity
            && preview::classify(preview::Float32Value{0xff800000u}) == preview::FloatClassification::NegativeInfinity
            && preview::classify(preview::Float32Value{0x7f800001u}) == preview::FloatClassification::Nan
            && preview::classify(preview::Float32Value{0x7f7fffffu}) == preview::FloatClassification::Finite,
        "float32 classifications are derived from storage bits");
    check(preview::classify(preview::Float64Value{0x8000000000000000ull}) == preview::FloatClassification::NegativeZero
            && preview::classify(preview::Float64Value{0x7ff0000000000000ull}) == preview::FloatClassification::PositiveInfinity
            && preview::classify(preview::Float64Value{0xfff0000000000000ull}) == preview::FloatClassification::NegativeInfinity
            && preview::classify(preview::Float64Value{0x7ff0000000000001ull}) == preview::FloatClassification::Nan
            && preview::classify(preview::Float64Value{0x7fefffffffffffffull}) == preview::FloatClassification::Finite,
        "float64 classifications do not evaluate exceptional numbers");
    check(preview::name(preview::Reason::ComparisonSlots) == "comparisonSlots"
            && preview::name(preview::Reason::InlineStringBytes) == "inlineStringBytes"
            && preview::name(preview::ChangeKind::ArrayLength) == "arrayLength"
            && preview::name(preview::ChangeKind::Presence) == "presence"
            && preview::name(preview::StringEncoding::Utf8) == "utf8"
            && preview::name(preview::StringEncoding::Hex) == "hex"
            && preview::name(preview::FloatClassification::Nan) == "nan",
        "presentation names are shared and retain their legacy spelling");
}

void cancellationIsDistinct()
{
    const sed::Level source;
    std::stop_source stop;
    check(preview::compareLevels(source, source, {}, stop.get_token()) == preview::compareLevels(source, source),
        "unrequested stop token does not change legacy traversal");
    stop.request_stop();
    for ( const auto slotLimit : {std::size_t{0}, std::size_t{1000000}} )
    {
        auto limits = preview::kDefaultLimits;
        limits.maxVisitedSlots = slotLimit;
        bool cancelled = false;
        try
        {
            (void)preview::compareLevels(source, source, limits, stop.get_token());
        }
        catch ( const preview::ComparisonCancelled& error )
        {
            cancelled = true;
            check(std::string_view(error.what()) == "semantic comparison cancelled", "cancellation has an explicit diagnostic");
        }
        check(cancelled, "requested cancellation is raised before traversal or budget admission");
    }
}
} // namespace

int main()
{
    try
    {
        metadataAndNoOp();
        soundPreloadState();
    exactTypedValues();
        optionalPresenceAndArrayOrder();
        budgetBoundaries();
        classificationAndNames();
        cancellationIsDistinct();
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected exception: ") + error.what());
    }
    std::cout << "portable semantic preview failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
