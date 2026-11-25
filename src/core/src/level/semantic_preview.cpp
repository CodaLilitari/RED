#include "sed/level/semantic_preview.hpp"

#include "sed/level.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sed::semantic_preview
{
namespace
{
// This private exception is only a control-flow exit from an admitted walk.
// Allocation failures and other errors must not masquerade as partial previews.
struct ComparisonBudgetExceeded
{
};

constexpr std::size_t kReasonCount = 5;

template<typename T>
constexpr bool kScalar = std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>;

template<typename T>
using FloatBits = std::conditional_t<std::is_same_v<T, float>, std::uint32_t, std::uint64_t>;

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

Descriptor sizeDescriptor(bool present, std::size_t length)
{
    return present ? Descriptor{Value{SizeValue{length}}} : Descriptor{};
}

class PreviewWalker
{
public:
    explicit PreviewWalker(const Limits& limits, sed::StopToken cancellation)
        : limits_(limits), cancellation_(std::move(cancellation))
    {
    }

    Comparison compare(const sed::Level& source, const sed::Level& after)
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

        Comparison result;
        result.comparisonComplete = comparisonComplete;
        result.detailsComplete = comparisonComplete && detailsComplete_;
        result.changed = observedChanges_ != 0 ? std::optional<bool>{true}
            : comparisonComplete ? std::optional<bool>{false} : std::nullopt;
        result.observedChanges = observedChanges_;
        result.totalChanges = comparisonComplete ? std::optional<std::size_t>{observedChanges_} : std::nullopt;
        result.changes = std::move(changes_);
        result.reasons = std::move(reasons_);
        result.work = {visitedSlots_, comparedStringBytes_, inlineStringBytes_};
        return result;
    }

private:
    const Limits& limits_;
    sed::StopToken cancellation_;
    std::size_t visitedSlots_{};
    std::size_t comparedStringBytes_{};
    std::size_t inlineStringBytes_{};
    std::size_t observedChanges_{};
    bool detailsComplete_{true};
    std::array<bool, kReasonCount> reasonSeen_{};
    std::vector<Change> changes_;
    std::vector<Reason> reasons_;

    void reason(Reason value)
    {
        const auto index = static_cast<std::size_t>(value);
        if ( !reasonSeen_[index] )
        {
            reasonSeen_[index] = true;
            reasons_.push_back(value);
        }
    }

    void admitSlot()
    {
        if ( cancellation_.stop_requested() )
        {
            throw ComparisonCancelled{};
        }
        if ( visitedSlots_ == limits_.maxVisitedSlots )
        {
            reason(Reason::ComparisonSlots);
            throw ComparisonBudgetExceeded{};
        }
        ++visitedSlots_;
    }

    void admitText(std::size_t beforeBytes, std::size_t afterBytes)
    {
        if ( cancellation_.stop_requested() )
        {
            throw ComparisonCancelled{};
        }
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
    void change(std::string_view path, ChangeKind kind,
        BeforeDescriptor&& before, AfterDescriptor&& after)
    {
        ++observedChanges_;
        if ( changes_.size() == limits_.maxChanges )
        {
            detailsComplete_ = false;
            reason(Reason::ChangeLimit);
            return;
        }
        Change result;
        result.path = std::string(path);
        result.kind = kind;
        // Evaluate in a fixed order: inline admission belongs to this view.
        result.before = before();
        result.after = after();
        changes_.push_back(std::move(result));
    }

    StringValue stringValue(const std::string& text)
    {
        StringValue result;
        result.byteCount = text.size();
        if ( text.size() > limits_.maxInlineStringBytes )
        {
            detailsComplete_ = false;
            reason(Reason::InlineStringLength);
            return result;
        }
        if ( text.size() > limits_.maxTotalInlineStringBytes - inlineStringBytes_ )
        {
            detailsComplete_ = false;
            reason(Reason::InlineStringBytes);
            return result;
        }
        inlineStringBytes_ += text.size();
        // Comparison and inline admission both precede validation or copying.
        const bool utf8 = validUtf8(text);
        result.encoding = utf8 ? StringEncoding::Utf8 : StringEncoding::Hex;
        result.encodedValue = utf8 ? text : byteHex(text);
        return result;
    }

    template<typename T>
    Descriptor descriptor(const T* value)
    {
        if ( !value )
        {
            return {};
        }
        if constexpr ( std::is_same_v<T, std::string> )
        {
            return {Value{stringValue(*value)}};
        }
        else if constexpr ( std::is_same_v<T, float> )
        {
            return {Value{Float32Value{std::bit_cast<std::uint32_t>(*value)}}};
        }
        else if constexpr ( std::is_same_v<T, double> )
        {
            return {Value{Float64Value{std::bit_cast<std::uint64_t>(*value)}}};
        }
        else if constexpr ( std::is_enum_v<T> )
        {
            using Underlying = std::underlying_type_t<T>;
            static_assert(std::numeric_limits<Underlying>::digits <= std::numeric_limits<std::int64_t>::digits);
            return {Value{EnumValue{static_cast<std::int64_t>(static_cast<Underlying>(*value))}}};
        }
        else
        {
            static_assert(std::is_same_v<T, bool> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>);
            return {Value{*value}};
        }
    }

    template<typename T>
    void scalar(std::string_view path, const T* before, const T* after, ChangeKind kind)
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
            scalar(path, before, after, ChangeKind::Value);
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
            change(path, ChangeKind::ArrayLength, [&] { return sizeDescriptor(before != nullptr, beforeSize); },
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
                (beforeValue != nullptr) != (afterValue != nullptr) ? ChangeKind::Presence : ChangeKind::Value);
        }
        else
        {
            const bool beforePresent = beforeValue != nullptr;
            const bool afterPresent = afterValue != nullptr;
            scalar(path, before ? &beforePresent : nullptr, after ? &afterPresent : nullptr, ChangeKind::Presence);
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

    void fields(std::string_view path, const sed::JklSoundPreloadState* before, const sed::JklSoundPreloadState* after)
    {
        field(path, before, after, "capacityBase", &sed::JklSoundPreloadState::capacityBase);
        field(path, before, after, "rows", &sed::JklSoundPreloadState::rows);
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
        field(path, before, after, "jklSoundPreloads", &sed::Level::jklSoundPreloads);
        field(path, before, after, "kind", &sed::Level::kind);
        field(path, before, after, "uvCoordinateSpace", &sed::Level::uvCoordinateSpace);
        // Imported/retained bookkeeping is disclosed by kExcludedFields, not walked.
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

template<typename Bits>
FloatClassification classifyBits(Bits bits, Bits infinity) noexcept
{
    constexpr Bits sign = Bits{1} << (sizeof(Bits) * 8 - 1);
    const Bits magnitude = bits & ~sign;
    const bool negative = (bits & sign) != 0;
    if ( magnitude > infinity )
    {
        return FloatClassification::Nan;
    }
    if ( magnitude == infinity )
    {
        return negative ? FloatClassification::NegativeInfinity : FloatClassification::PositiveInfinity;
    }
    if ( magnitude == 0 && negative )
    {
        return FloatClassification::NegativeZero;
    }
    return FloatClassification::Finite;
}
} // namespace

std::string_view name(ChangeKind value) noexcept
{
    switch ( value )
    {
    case ChangeKind::Value: return "value";
    case ChangeKind::ArrayLength: return "arrayLength";
    case ChangeKind::Presence: return "presence";
    }
    return {};
}

std::string_view name(Reason value) noexcept
{
    switch ( value )
    {
    case Reason::ComparisonSlots: return "comparisonSlots";
    case Reason::ComparisonStringBytes: return "comparisonStringBytes";
    case Reason::ChangeLimit: return "changeLimit";
    case Reason::InlineStringLength: return "inlineStringLength";
    case Reason::InlineStringBytes: return "inlineStringBytes";
    }
    return {};
}

std::string_view name(StringEncoding value) noexcept
{
    switch ( value )
    {
    case StringEncoding::Utf8: return "utf8";
    case StringEncoding::Hex: return "hex";
    }
    return {};
}

std::string_view name(FloatClassification value) noexcept
{
    switch ( value )
    {
    case FloatClassification::Finite: return "finite";
    case FloatClassification::NegativeZero: return "negativeZero";
    case FloatClassification::PositiveInfinity: return "positiveInfinity";
    case FloatClassification::NegativeInfinity: return "negativeInfinity";
    case FloatClassification::Nan: return "nan";
    }
    return {};
}

FloatClassification classify(Float32Value value) noexcept
{
    return classifyBits(value.bits, std::uint32_t{0x7f800000u});
}

FloatClassification classify(Float64Value value) noexcept
{
    return classifyBits(value.bits, std::uint64_t{0x7ff0000000000000ull});
}

Comparison compareLevels(const Level& source, const Level& after,
    const Limits& limits, sed::StopToken cancellation)
{
    return PreviewWalker(limits, std::move(cancellation)).compare(source, after);
}

} // namespace sed::semantic_preview
