#include "agent_semantic_preview.hpp"

#include "sed/level.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sed_agent
{
namespace
{
// This private exception is only a control-flow exit from an admitted walk.
// Allocation failures and other errors must not masquerade as partial previews.
struct ComparisonBudgetExceeded
{
};

enum class Reason : std::size_t
{
    ComparisonSlots,
    ComparisonStringBytes,
    ChangeLimit,
    InlineStringLength,
    InlineStringBytes,
    Count
};

constexpr std::array<std::string_view, static_cast<std::size_t>(Reason::Count)> kReasons{
    "comparisonSlots", "comparisonStringBytes", "changeLimit", "inlineStringLength", "inlineStringBytes"
};

template<typename T>
constexpr bool kScalar = std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>;

template<typename T>
using FloatBits = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, std::uint64_t>;

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

bool validUtf8(std::string_view text)
{
    std::size_t offset = 0;
    while ( offset < text.size() )
    {
        const auto first = static_cast<unsigned char>(text[offset++]);
        if ( first < 0x80 )
        {
            continue;
        }
        std::uint32_t codepoint{};
        std::uint32_t minimum{};
        std::size_t trailing{};
        if ( first >= 0xc2 && first <= 0xdf )
        {
            codepoint = first & 0x1fu;
            minimum = 0x80;
            trailing = 1;
        }
        else if ( first >= 0xe0 && first <= 0xef )
        {
            codepoint = first & 0x0fu;
            minimum = 0x800;
            trailing = 2;
        }
        else if ( first >= 0xf0 && first <= 0xf4 )
        {
            codepoint = first & 0x07u;
            minimum = 0x10000;
            trailing = 3;
        }
        else
        {
            return false;
        }
        if ( trailing > text.size() - offset )
        {
            return false;
        }
        for ( std::size_t i = 0; i < trailing; ++i )
        {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if ( (next & 0xc0u) != 0x80u )
            {
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if ( codepoint < minimum || codepoint > 0x10ffff
            || (codepoint >= 0xd800 && codepoint <= 0xdfff) )
        {
            return false;
        }
    }
    return true;
}

std::string byteHex(std::string_view bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    if ( bytes.size() > result.max_size() / 2 )
    {
        throw std::length_error("semantic preview hexadecimal string is too large");
    }
    result.resize(bytes.size() * 2);
    std::size_t offset = 0;
    for ( const unsigned char byte : bytes )
    {
        result[offset++] = digits[byte >> 4];
        result[offset++] = digits[byte & 15];
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

Json sizeDescriptor(bool present, std::size_t length)
{
    if ( !present )
    {
        return absentDescriptor();
    }
    Json result = presentDescriptor("size");
    result.set("value", Json::makeNumber(static_cast<double>(length)));
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
    result.set("version", Json::makeNumber(1));
    result.set("coverage", Json::makeString("level-model-v1"));
    result.set("addressing", Json::makeString("snapshot-storage-slots"));
    Json excluded = Json::makeArray();
    for ( const auto path : {
        "/sectors/*/surfaces/*/d3dId",
        "/importedJklSurfaceRefs", "/importedJklCogRowsPresent",
        "/retainedJklSourceText", "/retainedJklSourceHash", "/retainedJklUnknownSections",
        "/retainedJklHeaderUnknownLines", "/retainedJklSectorUnknownLines",
        "/importedGeoWorldVertices", "/importedGeoWorldTexVertices", "/importedGeoAdjoinFlags",
        "/importedGeoAdjoinMirrors", "/importedGeoVertexIndices", "/importedGeoSurfaceIndices",
        "/importedGeoTexVertexIndices", "/importedGeoAdjoinIndices", "/importedGeoMaterialIndices",
        "/importedGeoSurfaceCount", "/importedGeoAdjoinCount" } )
    {
        excluded.array.push_back(Json::makeString(path));
    }
    result.set("excludedFields", std::move(excluded));
    Json scopes = Json::makeArray();
    scopes.array.push_back(Json::makeString("nativeOptionalSectionBytes"));
    scopes.array.push_back(Json::makeString("dataOutsideLevel"));
    result.set("excludedScopes", std::move(scopes));
    result.set("limits", limitsJson(limits));
    Json commands = Json::makeArray();
    commands.array.push_back(Json::makeString("plan"));
    commands.array.push_back(Json::makeString("apply"));
    result.set("commands", std::move(commands));
    Json views = Json::makeObject();
    views.set("authored", Json::makeString("Captured source model compared with the candidate after all patch operations."));
    views.set("persisted", Json::makeString("Captured source model compared with the model reloaded from the exact proposed output bytes."));
    result.set("views", std::move(views));
    return result;
}

class PreviewWalker
{
public:
    explicit PreviewWalker(const SemanticPreviewLimits& limits) : limits_(limits)
    {
    }

    Json compare(const sed::Level& source, const sed::Level& after, std::string_view afterSnapshot)
    {
        bool comparisonComplete = true;
        try
        {
            walk({}, &source, &after);
        }
        catch ( const ComparisonBudgetExceeded& )
        {
            comparisonComplete = false;
        }

        Json result = Json::makeObject();
        result.set("beforeSnapshot", Json::makeString("source"));
        result.set("afterSnapshot", Json::makeString(std::string(afterSnapshot)));
        result.set("comparisonComplete", Json::makeBool(comparisonComplete));
        result.set("detailsComplete", Json::makeBool(comparisonComplete && detailsComplete_));
        result.set("changed", observedChanges_ != 0 ? Json::makeBool(true)
            : comparisonComplete ? Json::makeBool(false) : Json{});
        result.set("observedChanges", Json::makeNumber(static_cast<double>(observedChanges_)));
        result.set("totalChanges", comparisonComplete
            ? Json::makeNumber(static_cast<double>(observedChanges_)) : Json{});
        result.set("changes", std::move(changes_));
        result.set("reasons", std::move(reasons_));
        Json work = Json::makeObject();
        work.set("visitedSlots", Json::makeNumber(static_cast<double>(visitedSlots_)));
        work.set("comparedStringBytes", Json::makeNumber(static_cast<double>(comparedStringBytes_)));
        work.set("inlineStringBytes", Json::makeNumber(static_cast<double>(inlineStringBytes_)));
        result.set("work", std::move(work));
        return result;
    }

private:
    const SemanticPreviewLimits& limits_;
    std::size_t visitedSlots_{};
    std::size_t comparedStringBytes_{};
    std::size_t inlineStringBytes_{};
    std::size_t observedChanges_{};
    bool detailsComplete_{true};
    std::array<bool, static_cast<std::size_t>(Reason::Count)> reasonSeen_{};
    Json changes_{Json::makeArray()};
    Json reasons_{Json::makeArray()};

    void reason(Reason value)
    {
        const auto index = static_cast<std::size_t>(value);
        if ( !reasonSeen_[index] )
        {
            reasonSeen_[index] = true;
            reasons_.array.push_back(Json::makeString(std::string(kReasons[index])));
        }
    }

    void admitSlot()
    {
        if ( visitedSlots_ == limits_.maxVisitedSlots )
        {
            reason(Reason::ComparisonSlots);
            throw ComparisonBudgetExceeded{};
        }
        ++visitedSlots_;
    }

    void admitText(std::size_t beforeBytes, std::size_t afterBytes)
    {
        const auto remaining = limits_.maxComparedStringBytes - comparedStringBytes_;
        if ( beforeBytes > remaining || afterBytes > remaining - beforeBytes )
        {
            reason(Reason::ComparisonStringBytes);
            throw ComparisonBudgetExceeded{};
        }
        // The subtraction checks admit the pair atomically, without overflow.
        comparedStringBytes_ += beforeBytes + afterBytes;
    }

    template<typename BeforeDescriptor, typename AfterDescriptor>
    void change(std::string_view path, std::string_view kind,
        BeforeDescriptor&& before, AfterDescriptor&& after)
    {
        ++observedChanges_;
        if ( changes_.array.size() == limits_.maxChanges )
        {
            detailsComplete_ = false;
            reason(Reason::ChangeLimit);
            return;
        }
        Json result = Json::makeObject();
        result.set("path", Json::makeString(std::string(path)));
        result.set("kind", Json::makeString(std::string(kind)));
        // Evaluate in a fixed order: inline admission belongs to this view.
        result.set("before", before());
        result.set("after", after());
        changes_.array.push_back(std::move(result));
    }

    Json stringDescriptor(const std::string& text)
    {
        Json result = presentDescriptor("string");
        result.set("byteCount", Json::makeNumber(static_cast<double>(text.size())));
        if ( text.size() > limits_.maxInlineStringBytes )
        {
            detailsComplete_ = false;
            reason(Reason::InlineStringLength);
            result.set("omitted", Json::makeBool(true));
            return result;
        }
        if ( text.size() > limits_.maxTotalInlineStringBytes - inlineStringBytes_ )
        {
            detailsComplete_ = false;
            reason(Reason::InlineStringBytes);
            result.set("omitted", Json::makeBool(true));
            return result;
        }
        inlineStringBytes_ += text.size();
        // Comparison and inline admission both precede validation or copying.
        const bool utf8 = validUtf8(text);
        result.set("encoding", Json::makeString(utf8 ? "utf8" : "hex"));
        result.set("value", Json::makeString(utf8 ? text : byteHex(text)));
        return result;
    }

    template<typename T>
    Json floatDescriptor(const T& value)
    {
        using Bits = FloatBits<T>;
        constexpr Bits sign = Bits{1} << (sizeof(Bits) * 8 - 1);
        constexpr Bits infinity = []
        {
            if constexpr ( std::is_same_v<T, float> )
            {
                return Bits{0x7f800000u};
            }
            else
            {
                return Bits{0x7ff0000000000000ull};
            }
        }();
        const Bits bits = std::bit_cast<Bits>(value);
        const Bits magnitude = bits & ~sign;
        const bool negative = (bits & sign) != 0;
        const bool finite = magnitude < infinity;
        std::string_view classification = "finite";
        if ( magnitude > infinity )
        {
            classification = "nan";
        }
        else if ( magnitude == infinity )
        {
            classification = negative ? "negativeInfinity" : "positiveInfinity";
        }
        else if ( magnitude == 0 && negative )
        {
            classification = "negativeZero";
        }
        Json result = presentDescriptor(std::is_same_v<T, float> ? "float32" : "float64");
        result.set("bits", Json::makeString(bitHex(bits)));
        result.set("classification", Json::makeString(std::string(classification)));
        result.set("value", finite ? Json::makeNumber(static_cast<double>(value)) : Json{});
        return result;
    }

    template<typename T>
    Json descriptor(const T* value)
    {
        if ( !value )
        {
            return absentDescriptor();
        }
        if constexpr ( std::is_same_v<T, std::string> )
        {
            return stringDescriptor(*value);
        }
        else if constexpr ( std::is_floating_point_v<T> )
        {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
            return floatDescriptor(*value);
        }
        else if constexpr ( std::is_same_v<T, bool> )
        {
            Json result = presentDescriptor("bool");
            result.set("value", Json::makeBool(*value));
            return result;
        }
        else if constexpr ( std::is_enum_v<T> )
        {
            Json result = presentDescriptor("enum");
            result.set("value", Json::makeNumber(static_cast<double>(static_cast<std::underlying_type_t<T>>(*value))));
            return result;
        }
        else
        {
            static_assert(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>);
            Json result = presentDescriptor(std::is_signed_v<T> ? "int32" : "uint32");
            result.set("value", Json::makeNumber(static_cast<double>(*value)));
            return result;
        }
    }

    template<typename T>
    void scalar(std::string_view path, const T* before, const T* after, std::string_view kind)
    {
        if constexpr ( std::is_same_v<T, std::string> )
        {
            admitText(before ? before->size() : 0, after ? after->size() : 0);
        }
        if ( !before && !after )
        {
            return;
        }
        if ( before && after )
        {
            if constexpr ( std::is_floating_point_v<T> )
            {
                if ( std::bit_cast<FloatBits<T>>(*before) == std::bit_cast<FloatBits<T>>(*after) )
                {
                    return;
                }
            }
            else if ( *before == *after )
            {
                return;
            }
        }
        change(path, kind, [&] { return descriptor(before); }, [&] { return descriptor(after); });
    }

    template<typename T>
    void walk(std::string_view path, const T* before, const T* after)
    {
        admitSlot();
        if constexpr ( kScalar<T> )
        {
            scalar(path, before, after, "value");
        }
        else if ( before || after )
        {
            fields(path, before, after);
        }
    }

    template<typename T>
    void sequence(std::string_view path, const T* before, const T* after)
    {
        const std::size_t beforeSize = before ? before->size() : 0;
        const std::size_t afterSize = after ? after->size() : 0;
        if ( (before != nullptr) != (after != nullptr) || beforeSize != afterSize )
        {
            change(path, "arrayLength", [&] { return sizeDescriptor(before != nullptr, beforeSize); },
                [&] { return sizeDescriptor(after != nullptr, afterSize); });
        }
        const auto count = std::max(beforeSize, afterSize);
        for ( std::size_t i = 0; i < count; ++i )
        {
            std::string child(path);
            child.push_back('/');
            child += std::to_string(i);
            // The next walker admits the element before touching its fields.
            // An exhausted admission unwinds this loop immediately.
            walk(child, i < beforeSize ? &(*before)[i] : nullptr,
                i < afterSize ? &(*after)[i] : nullptr);
        }
    }

    template<typename T>
    void walk(std::string_view path, const std::vector<T>* before, const std::vector<T>* after)
    {
        admitSlot();
        sequence(path, before, after);
    }

    template<typename T, std::size_t N>
    void walk(std::string_view path, const std::array<T, N>* before, const std::array<T, N>* after)
    {
        admitSlot();
        sequence(path, before, after);
    }

    template<typename T>
    void walk(std::string_view path, const std::optional<T>* before, const std::optional<T>* after)
    {
        admitSlot();
        const T* beforeValue = before && before->has_value() ? &before->value() : nullptr;
        const T* afterValue = after && after->has_value() ? &after->value() : nullptr;
        if constexpr ( kScalar<T> )
        {
            // The optional container and its scalar slot each consume work,
            // including when both scalars are absent.
            admitSlot();
            scalar(path, beforeValue, afterValue,
                (beforeValue != nullptr) != (afterValue != nullptr) ? "presence" : "value");
        }
        else
        {
            const bool beforePresent = beforeValue != nullptr;
            const bool afterPresent = afterValue != nullptr;
            scalar(path, before ? &beforePresent : nullptr, after ? &afterPresent : nullptr, "presence");
            // Charge the nullable record once. If both are absent, walk stops
            // there instead of inventing nonexistent stored descendants.
            walk(path, beforeValue, afterValue);
        }
    }

    template<typename T, typename Owner, typename Member>
    void field(std::string_view path, const T* before, const T* after,
        std::string_view name, Member Owner::*member)
    {
        std::string child(path);
        child.push_back('/');
        child.append(name);
        walk(child, before ? &(before->*member) : nullptr, after ? &(after->*member) : nullptr);
    }

    void fields(std::string_view path, const sed::ColorF* before, const sed::ColorF* after)
    {
        field(path, before, after, "r", &sed::ColorF::r);
        field(path, before, after, "g", &sed::ColorF::g);
        field(path, before, after, "b", &sed::ColorF::b);
        field(path, before, after, "a", &sed::ColorF::a);
    }

    void fields(std::string_view path, const sed::TPoint2* before, const sed::TPoint2* after)
    {
        field(path, before, after, "x", &sed::TPoint2::x);
        field(path, before, after, "y", &sed::TPoint2::y);
    }

    void fields(std::string_view path, const sed::TVector* before, const sed::TVector* after)
    {
        field(path, before, after, "x", &sed::TVector::x);
        field(path, before, after, "y", &sed::TVector::y);
        field(path, before, after, "z", &sed::TVector::z);
    }

    void fields(std::string_view path, const sed::TBox* before, const sed::TBox* after)
    {
        field(path, before, after, "x1", &sed::TBox::x1);
        field(path, before, after, "y1", &sed::TBox::y1);
        field(path, before, after, "z1", &sed::TBox::z1);
        field(path, before, after, "x2", &sed::TBox::x2);
        field(path, before, after, "y2", &sed::TBox::y2);
        field(path, before, after, "z2", &sed::TBox::z2);
    }

    void fields(std::string_view path, const sed::SurfaceRef* before, const sed::SurfaceRef* after)
    {
        field(path, before, after, "sector", &sed::SurfaceRef::sector);
        field(path, before, after, "surface", &sed::SurfaceRef::surface);
    }

    void fields(std::string_view path, const sed::HorizonSky* before, const sed::HorizonSky* after)
    {
        field(path, before, after, "distance", &sed::HorizonSky::distance);
        field(path, before, after, "pixelsPerRev", &sed::HorizonSky::pixelsPerRev);
        field(path, before, after, "offset", &sed::HorizonSky::offset);
    }

    void fields(std::string_view path, const sed::CeilingSky* before, const sed::CeilingSky* after)
    {
        field(path, before, after, "height", &sed::CeilingSky::height);
        field(path, before, after, "offset", &sed::CeilingSky::offset);
    }

    void fields(std::string_view path, const sed::Fog* before, const sed::Fog* after)
    {
        field(path, before, after, "enabled", &sed::Fog::enabled);
        field(path, before, after, "color", &sed::Fog::color);
        field(path, before, after, "fogStart", &sed::Fog::fogStart);
        field(path, before, after, "fogEnd", &sed::Fog::fogEnd);
    }

    void fields(std::string_view path, const sed::LevelHeader* before, const sed::LevelHeader* after)
    {
        field(path, before, after, "version", &sed::LevelHeader::version);
        field(path, before, after, "gravity", &sed::LevelHeader::gravity);
        field(path, before, after, "horizonSky", &sed::LevelHeader::horizonSky);
        field(path, before, after, "ceilingSky", &sed::LevelHeader::ceilingSky);
        field(path, before, after, "mipmapDistances", &sed::LevelHeader::mipmapDistances);
        field(path, before, after, "lodDistances", &sed::LevelHeader::lodDistances);
        field(path, before, after, "perspectiveDistance", &sed::LevelHeader::perspectiveDistance);
        field(path, before, after, "gouraudDistance", &sed::LevelHeader::gouraudDistance);
        field(path, before, after, "fog", &sed::LevelHeader::fog);
    }

    void fields(std::string_view path, const sed::LevelExportBufferExtras* before, const sed::LevelExportBufferExtras* after)
    {
        field(path, before, after, "sounds", &sed::LevelExportBufferExtras::sounds);
        field(path, before, after, "materials", &sed::LevelExportBufferExtras::materials);
        field(path, before, after, "aiClasses", &sed::LevelExportBufferExtras::aiClasses);
        field(path, before, after, "models", &sed::LevelExportBufferExtras::models);
        field(path, before, after, "sprites", &sed::LevelExportBufferExtras::sprites);
        field(path, before, after, "keyframes", &sed::LevelExportBufferExtras::keyframes);
        field(path, before, after, "puppets", &sed::LevelExportBufferExtras::puppets);
        field(path, before, after, "soundClasses", &sed::LevelExportBufferExtras::soundClasses);
        field(path, before, after, "cogScripts", &sed::LevelExportBufferExtras::cogScripts);
        field(path, before, after, "cogs", &sed::LevelExportBufferExtras::cogs);
        field(path, before, after, "templates", &sed::LevelExportBufferExtras::templates);
        field(path, before, after, "things", &sed::LevelExportBufferExtras::things);
    }

    void fields(std::string_view path, const sed::PointLight* before, const sed::PointLight* after)
    {
        field(path, before, after, "color", &sed::PointLight::color);
        field(path, before, after, "position", &sed::PointLight::position);
        field(path, before, after, "minRange", &sed::PointLight::minRange);
        field(path, before, after, "maxRange", &sed::PointLight::maxRange);
    }

    void fields(std::string_view path, const sed::Vertex* before, const sed::Vertex* after)
    {
        field(path, before, after, "x", &sed::Vertex::x);
        field(path, before, after, "y", &sed::Vertex::y);
        field(path, before, after, "z", &sed::Vertex::z);
        field(path, before, after, "mark", &sed::Vertex::mark);
    }

    void fields(std::string_view path, const sed::TexVertex* before, const sed::TexVertex* after)
    {
        field(path, before, after, "u", &sed::TexVertex::u);
        field(path, before, after, "v", &sed::TexVertex::v);
        field(path, before, after, "color", &sed::TexVertex::color);
    }

    void fields(std::string_view path, const sed::Surface* before, const sed::Surface* after)
    {
        field(path, before, after, "num", &sed::Surface::num);
        field(path, before, after, "mark", &sed::Surface::mark);
        // d3dId is a renderer handle, explicitly outside level-model-v1.
        field(path, before, after, "surfaceFlags", &sed::Surface::surfaceFlags);
        field(path, before, after, "faceFlags", &sed::Surface::faceFlags);
        field(path, before, after, "geo", &sed::Surface::geo);
        field(path, before, after, "light", &sed::Surface::light);
        field(path, before, after, "tex", &sed::Surface::tex);
        field(path, before, after, "extraLight", &sed::Surface::extraLight);
        field(path, before, after, "adjoinFlags", &sed::Surface::adjoinFlags);
        field(path, before, after, "adjoin", &sed::Surface::adjoin);
        field(path, before, after, "material", &sed::Surface::material);
        field(path, before, after, "nAdj", &sed::Surface::nAdj);
        field(path, before, after, "nMat", &sed::Surface::nMat);
        field(path, before, after, "uScale", &sed::Surface::uScale);
        field(path, before, after, "vScale", &sed::Surface::vScale);
        field(path, before, after, "normal", &sed::Surface::normal);
        field(path, before, after, "vertices", &sed::Surface::vertices);
        field(path, before, after, "texVertices", &sed::Surface::texVertices);
    }

    void fields(std::string_view path, const sed::Sector* before, const sed::Sector* after)
    {
        field(path, before, after, "id", &sed::Sector::id);
        field(path, before, after, "num", &sed::Sector::num);
        field(path, before, after, "flags", &sed::Sector::flags);
        field(path, before, after, "ambient", &sed::Sector::ambient);
        field(path, before, after, "extraLight", &sed::Sector::extraLight);
        field(path, before, after, "pointLight", &sed::Sector::pointLight);
        field(path, before, after, "colormap", &sed::Sector::colormap);
        field(path, before, after, "colormapIndex", &sed::Sector::colormapIndex);
        field(path, before, after, "tint", &sed::Sector::tint);
        field(path, before, after, "sound", &sed::Sector::sound);
        field(path, before, after, "soundVolume", &sed::Sector::soundVolume);
        field(path, before, after, "thrust", &sed::Sector::thrust);
        field(path, before, after, "collideBox", &sed::Sector::collideBox);
        field(path, before, after, "layer", &sed::Sector::layer);
        field(path, before, after, "mark", &sed::Sector::mark);
        field(path, before, after, "vertices", &sed::Sector::vertices);
        field(path, before, after, "surfaces", &sed::Sector::surfaces);
    }

    void valueFields(std::string_view path, const sed::Value* before, const sed::Value* after)
    {
        // Inherited storage belongs to the enclosing parameter record. No
        // display conversion or active-type selection may drop these fields.
        field(path, before, after, "name", &sed::Value::name);
        field(path, before, after, "type", &sed::Value::type);
        field(path, before, after, "intValue", &sed::Value::intValue);
        field(path, before, after, "floatValue", &sed::Value::floatValue);
        field(path, before, after, "stringValue", &sed::Value::stringValue);
    }

    void fields(std::string_view path, const sed::TplValue* before, const sed::TplValue* after)
    {
        valueFields(path, before, after);
        field(path, before, after, "adjType", &sed::TplValue::adjType);
    }

    void fields(std::string_view path, const sed::CogValue* before, const sed::CogValue* after)
    {
        valueFields(path, before, after);
        field(path, before, after, "cogType", &sed::CogValue::cogType);
        field(path, before, after, "local", &sed::CogValue::local);
        field(path, before, after, "description", &sed::CogValue::description);
        field(path, before, after, "mask", &sed::CogValue::mask);
        field(path, before, after, "explicitLinkId", &sed::CogValue::explicitLinkId);
        field(path, before, after, "explicitMask", &sed::CogValue::explicitMask);
        field(path, before, after, "noLink", &sed::CogValue::noLink);
    }

    void fields(std::string_view path, const sed::Thing* before, const sed::Thing* after)
    {
        field(path, before, after, "id", &sed::Thing::id);
        field(path, before, after, "num", &sed::Thing::num);
        field(path, before, after, "sector", &sed::Thing::sector);
        field(path, before, after, "archLightingIndex", &sed::Thing::archLightingIndex);
        field(path, before, after, "templateName", &sed::Thing::templateName);
        field(path, before, after, "name", &sed::Thing::name);
        field(path, before, after, "x", &sed::Thing::x);
        field(path, before, after, "y", &sed::Thing::y);
        field(path, before, after, "z", &sed::Thing::z);
        field(path, before, after, "pch", &sed::Thing::pch);
        field(path, before, after, "yaw", &sed::Thing::yaw);
        field(path, before, after, "rol", &sed::Thing::rol);
        field(path, before, after, "values", &sed::Thing::values);
        field(path, before, after, "layer", &sed::Thing::layer);
        field(path, before, after, "bbox", &sed::Thing::bbox);
    }

    void fields(std::string_view path, const sed::ThingTemplate* before, const sed::ThingTemplate* after)
    {
        field(path, before, after, "name", &sed::ThingTemplate::name);
        field(path, before, after, "parent", &sed::ThingTemplate::parent);
        field(path, before, after, "values", &sed::ThingTemplate::values);
        field(path, before, after, "desc", &sed::ThingTemplate::desc);
        field(path, before, after, "bbox", &sed::ThingTemplate::bbox);
    }

    void fields(std::string_view path, const sed::Cog* before, const sed::Cog* after)
    {
        field(path, before, after, "id", &sed::Cog::id);
        field(path, before, after, "num", &sed::Cog::num);
        field(path, before, after, "name", &sed::Cog::name);
        field(path, before, after, "values", &sed::Cog::values);
    }

    void fields(std::string_view path, const sed::MotsArchLightVertex* before, const sed::MotsArchLightVertex* after)
    {
        field(path, before, after, "mono", &sed::MotsArchLightVertex::mono);
        field(path, before, after, "red", &sed::MotsArchLightVertex::red);
        field(path, before, after, "green", &sed::MotsArchLightVertex::green);
        field(path, before, after, "blue", &sed::MotsArchLightVertex::blue);
    }

    void fields(std::string_view path, const sed::MotsArchLightMesh* before, const sed::MotsArchLightMesh* after)
    {
        field(path, before, after, "wireIndex", &sed::MotsArchLightMesh::wireIndex);
        field(path, before, after, "vertices", &sed::MotsArchLightMesh::vertices);
    }

    void fields(std::string_view path, const sed::MotsArchLightObject* before, const sed::MotsArchLightObject* after)
    {
        field(path, before, after, "wireIndex", &sed::MotsArchLightObject::wireIndex);
        field(path, before, after, "meshes", &sed::MotsArchLightObject::meshes);
    }

    void fields(std::string_view path, const sed::MotsArchLightingBakeBaseline* before,
        const sed::MotsArchLightingBakeBaseline* after)
    {
        field(path, before, after, "thingId", &sed::MotsArchLightingBakeBaseline::thingId);
        field(path, before, after, "modelName", &sed::MotsArchLightingBakeBaseline::modelName);
        field(path, before, after, "originalArchLightingIndex", &sed::MotsArchLightingBakeBaseline::originalArchLightingIndex);
        field(path, before, after, "originalArchLighting", &sed::MotsArchLightingBakeBaseline::originalArchLighting);
    }

    void fields(std::string_view path, const sed::MotsArchAuthoringEntry* before, const sed::MotsArchAuthoringEntry* after)
    {
        field(path, before, after, "thingIndex", &sed::MotsArchAuthoringEntry::thingIndex);
        field(path, before, after, "origin", &sed::MotsArchAuthoringEntry::origin);
        field(path, before, after, "modelName", &sed::MotsArchAuthoringEntry::modelName);
        field(path, before, after, "modelEntrySha256Hex", &sed::MotsArchAuthoringEntry::modelEntrySha256Hex);
        field(path, before, after, "bakeInputSha256Hex", &sed::MotsArchAuthoringEntry::bakeInputSha256Hex);
        field(path, before, after, "sampleSha256Hex", &sed::MotsArchAuthoringEntry::sampleSha256Hex);
        field(path, before, after, "bakeProfile", &sed::MotsArchAuthoringEntry::bakeProfile);
    }

    void fields(std::string_view path, const sed::SedLight* before, const sed::SedLight* after)
    {
        field(path, before, after, "id", &sed::SedLight::id);
        field(path, before, after, "flags", &sed::SedLight::flags);
        field(path, before, after, "mark", &sed::SedLight::mark);
        field(path, before, after, "layer", &sed::SedLight::layer);
        field(path, before, after, "color", &sed::SedLight::color);
        field(path, before, after, "intensity", &sed::SedLight::intensity);
        field(path, before, after, "range", &sed::SedLight::range);
        field(path, before, after, "position", &sed::SedLight::position);
        field(path, before, after, "rgbIntensity", &sed::SedLight::rgbIntensity);
        field(path, before, after, "legacyRgbRange", &sed::SedLight::legacyRgbRange);
    }

    void fields(std::string_view path, const sed::HNode* before, const sed::HNode* after)
    {
        field(path, before, after, "id", &sed::HNode::id);
        field(path, before, after, "num", &sed::HNode::num);
        field(path, before, after, "type", &sed::HNode::type);
        field(path, before, after, "mesh", &sed::HNode::mesh);
        field(path, before, after, "parent", &sed::HNode::parent);
        field(path, before, after, "position", &sed::HNode::position);
        field(path, before, after, "rotation", &sed::HNode::rotation);
        field(path, before, after, "pivot", &sed::HNode::pivot);
        field(path, before, after, "name", &sed::HNode::name);
    }

    void fields(std::string_view path, const sed::Level* before, const sed::Level* after)
    {
        field(path, before, after, "header", &sed::Level::header);
        field(path, before, after, "exportBufferExtras", &sed::Level::exportBufferExtras);
        field(path, before, after, "kind", &sed::Level::kind);
        field(path, before, after, "uvCoordinateSpace", &sed::Level::uvCoordinateSpace);
        // Imported/retained bookkeeping is disclosed by metadata(), not walked.
        field(path, before, after, "sectors", &sed::Level::sectors);
        field(path, before, after, "cogs", &sed::Level::cogs);
        field(path, before, after, "templates", &sed::Level::templates);
        field(path, before, after, "things", &sed::Level::things);
        field(path, before, after, "motsArchLighting", &sed::Level::motsArchLighting);
        field(path, before, after, "motsArchLightingBakeBaselines", &sed::Level::motsArchLightingBakeBaselines);
        field(path, before, after, "motsArchAuthoring", &sed::Level::motsArchAuthoring);
        field(path, before, after, "hasSourceLightMetadata", &sed::Level::hasSourceLightMetadata);
        field(path, before, after, "lights", &sed::Level::lights);
        field(path, before, after, "h3doNodes", &sed::Level::h3doNodes);
        field(path, before, after, "colormaps", &sed::Level::colormaps);
        field(path, before, after, "materials", &sed::Level::materials);
        field(path, before, after, "layers", &sed::Level::layers);
        field(path, before, after, "masterCMP", &sed::Level::masterCMP);
        field(path, before, after, "ppunit", &sed::Level::ppunit);
        field(path, before, after, "lvisstring", &sed::Level::lvisstring);
        field(path, before, after, "curId", &sed::Level::curId);
    }
};
} // namespace

Json buildSemanticPreview(const sed::Level& source, const sed::Level& candidate,
    const sed::Level& persisted, std::string_view inputSha256,
    std::string_view outputSha256, const SemanticPreviewLimits& limits)
{
    Json result = metadata(limits);
    result.set("inputSha256", Json::makeString(std::string(inputSha256)));
    result.set("outputSha256", Json::makeString(std::string(outputSha256)));
    result.set("authored", PreviewWalker(limits).compare(source, candidate, "candidate"));
    result.set("persisted", PreviewWalker(limits).compare(source, persisted, "output"));
    return result;
}

Json semanticPreviewCapabilities()
{
    return metadata({});
}

} // namespace sed_agent
