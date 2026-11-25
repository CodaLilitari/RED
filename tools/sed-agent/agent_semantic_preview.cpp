#include "agent_semantic_preview.hpp"

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace sed_agent
{
namespace
{
namespace preview = sed::semantic_preview;

template<typename T>
std::string bitHex(T bits)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(sizeof(T) * 2, '0');
    for ( std::size_t i = result.size(); i != 0; --i )
    {
        result[i - 1] = digits[static_cast<std::size_t>(bits & T{15})];
        bits >>= 4;
    }
    return result;
}

Json absentDescriptor()
{
    Json result = Json::makeObject();
    result.set("present", Json::makeBool(false));
    return result;
}

Json presentDescriptor(std::string_view type)
{
    Json result = Json::makeObject();
    result.set("present", Json::makeBool(true));
    result.set("type", Json::makeString(std::string(type)));
    return result;
}

Json limitsJson(const SemanticPreviewLimits& limits)
{
    Json result = Json::makeObject();
    result.set("maxVisitedSlots", Json::makeNumber(static_cast<double>(limits.maxVisitedSlots)));
    result.set("maxComparedStringBytes", Json::makeNumber(static_cast<double>(limits.maxComparedStringBytes)));
    result.set("maxChanges", Json::makeNumber(static_cast<double>(limits.maxChanges)));
    result.set("maxInlineStringBytes", Json::makeNumber(static_cast<double>(limits.maxInlineStringBytes)));
    result.set("maxTotalInlineStringBytes", Json::makeNumber(static_cast<double>(limits.maxTotalInlineStringBytes)));
    return result;
}

Json metadata(const SemanticPreviewLimits& limits)
{
    Json result = Json::makeObject();
    result.set("version", Json::makeNumber(preview::kVersion));
    result.set("coverage", Json::makeString(std::string(preview::kCoverage)));
    result.set("addressing", Json::makeString(std::string(preview::kAddressing)));
    Json excluded = Json::makeArray();
    for ( const auto path : preview::kExcludedFields )
    {
        excluded.array.push_back(Json::makeString(std::string(path)));
    }
    result.set("excludedFields", std::move(excluded));
    Json scopes = Json::makeArray();
    for ( const auto scope : preview::kExcludedScopes )
    {
        scopes.array.push_back(Json::makeString(std::string(scope)));
    }
    result.set("excludedScopes", std::move(scopes));
    result.set("limits", limitsJson(limits));
    Json commands = Json::makeArray();
    commands.array.push_back(Json::makeString("plan"));
    commands.array.push_back(Json::makeString("apply"));
    result.set("commands", std::move(commands));
    Json views = Json::makeObject();
    views.set("authored", Json::makeString(std::string(preview::kAuthoredDescription)));
    views.set("persisted", Json::makeString(std::string(preview::kPersistedDescription)));
    result.set("views", std::move(views));
    return result;
}

template<typename T>
Json floatDescriptor(const T& value)
{
    constexpr bool isFloat32 = std::is_same_v<T, preview::Float32Value>;
    using Number = std::conditional_t<isFloat32, float, double>;
    const auto classification = preview::classify(value);
    const bool finite = classification == preview::FloatClassification::Finite
        || classification == preview::FloatClassification::NegativeZero;
    Json result = presentDescriptor(isFloat32 ? "float32" : "float64");
    result.set("bits", Json::makeString(bitHex(value.bits)));
    result.set("classification", Json::makeString(std::string(preview::name(classification))));
    result.set("value", finite
        ? Json::makeNumber(static_cast<double>(std::bit_cast<Number>(value.bits))) : Json{});
    return result;
}

Json descriptorJson(const preview::Descriptor& descriptor)
{
    if ( !descriptor.value )
    {
        return absentDescriptor();
    }
    return std::visit([](const auto& value) -> Json
    {
        using T = std::decay_t<decltype(value)>;
        if constexpr ( std::is_same_v<T, preview::StringValue> )
        {
            Json result = presentDescriptor("string");
            result.set("byteCount", Json::makeNumber(static_cast<double>(value.byteCount)));
            if ( !value.encodedValue )
            {
                result.set("omitted", Json::makeBool(true));
            }
            else
            {
                result.set("encoding", Json::makeString(std::string(preview::name(value.encoding))));
                result.set("value", Json::makeString(*value.encodedValue));
            }
            return result;
        }
        else if constexpr ( std::is_same_v<T, preview::Float32Value> || std::is_same_v<T, preview::Float64Value> )
        {
            return floatDescriptor(value);
        }
        else if constexpr ( std::is_same_v<T, bool> )
        {
            Json result = presentDescriptor("bool");
            result.set("value", Json::makeBool(value));
            return result;
        }
        else if constexpr ( std::is_same_v<T, preview::EnumValue> || std::is_same_v<T, preview::SizeValue> )
        {
            Json result = presentDescriptor(std::is_same_v<T, preview::EnumValue> ? "enum" : "size");
            result.set("value", Json::makeNumber(static_cast<double>(value.value)));
            return result;
        }
        else
        {
            static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>);
            Json result = presentDescriptor(std::is_signed_v<T> ? "int32" : "uint32");
            result.set("value", Json::makeNumber(static_cast<double>(value)));
            return result;
        }
    }, *descriptor.value);
}

Json comparisonJson(const preview::Comparison& comparison, std::string_view afterSnapshot)
{
    Json result = Json::makeObject();
    result.set("beforeSnapshot", Json::makeString("source"));
    result.set("afterSnapshot", Json::makeString(std::string(afterSnapshot)));
    result.set("comparisonComplete", Json::makeBool(comparison.comparisonComplete));
    result.set("detailsComplete", Json::makeBool(comparison.detailsComplete));
    result.set("changed", comparison.changed ? Json::makeBool(*comparison.changed) : Json{});
    result.set("observedChanges", Json::makeNumber(static_cast<double>(comparison.observedChanges)));
    result.set("totalChanges", comparison.totalChanges
        ? Json::makeNumber(static_cast<double>(*comparison.totalChanges)) : Json{});
    Json changes = Json::makeArray();
    for ( const auto& change : comparison.changes )
    {
        Json row = Json::makeObject();
        row.set("path", Json::makeString(change.path));
        row.set("kind", Json::makeString(std::string(preview::name(change.kind))));
        row.set("before", descriptorJson(change.before));
        row.set("after", descriptorJson(change.after));
        changes.array.push_back(std::move(row));
    }
    result.set("changes", std::move(changes));
    Json reasons = Json::makeArray();
    for ( const auto reason : comparison.reasons )
    {
        reasons.array.push_back(Json::makeString(std::string(preview::name(reason))));
    }
    result.set("reasons", std::move(reasons));
    Json work = Json::makeObject();
    work.set("visitedSlots", Json::makeNumber(static_cast<double>(comparison.work.visitedSlots)));
    work.set("comparedStringBytes", Json::makeNumber(static_cast<double>(comparison.work.comparedStringBytes)));
    work.set("inlineStringBytes", Json::makeNumber(static_cast<double>(comparison.work.inlineStringBytes)));
    result.set("work", std::move(work));
    return result;
}
} // namespace

Json buildSemanticPreview(const sed::Level& source, const sed::Level& candidate,
    const sed::Level& persisted, std::string_view inputSha256,
    std::string_view outputSha256, const SemanticPreviewLimits& limits)
{
    Json result = metadata(limits);
    result.set("inputSha256", Json::makeString(std::string(inputSha256)));
    result.set("outputSha256", Json::makeString(std::string(outputSha256)));
    result.set("authored", comparisonJson(preview::compareLevels(source, candidate, limits), "candidate"));
    result.set("persisted", comparisonJson(preview::compareLevels(source, persisted, limits), "output"));
    return result;
}

Json semanticPreviewCapabilities()
{
    return metadata({});
}

} // namespace sed_agent
