#include "sed/assets/obj_import.hpp"

#include "model_import_detail.hpp"

#include "sed/float_chars.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sed::assets
{

ObjImportError::ObjImportError(const ObjImportErrorKind kind, std::string code, const std::string& message)
    : std::runtime_error(message), kind_(kind), code_(std::move(code))
{
}

ObjImportErrorKind ObjImportError::kind() const noexcept
{
    return kind_;
}

const std::string& ObjImportError::code() const noexcept
{
    return code_;
}

namespace model_import
{

constexpr std::size_t kMaxReportedTokenLength = 32;

void refuse(const ObjImportErrorKind kind, const char* const code, const std::string& message)
{
    throw ObjImportError(kind, code, message);
}

std::string inQuotes(const std::string_view token)
{
    if ( token.size() <= kMaxReportedTokenLength )
    {
        return "'" + std::string(token) + "'";
    }
    return "'" + std::string(token.substr(0, kMaxReportedTokenLength)) + "...'";
}

} // namespace model_import

namespace
{

using model_import::cross;
using model_import::dot;
using model_import::inQuotes;
using model_import::isFinite;
using model_import::length;
using model_import::refuse;
using model_import::SourceCorner;
using model_import::SourceMeshName;
using model_import::SourceModel;
using model_import::SourcePolygon;
using model_import::Vec3;

constexpr double kMaxScale = 1000000.0;
constexpr double kMaxCoordinate = 1000000.0;
constexpr double kMaxTexel = 10000000.0;
constexpr std::int32_t kMaxTextureSize = 4096;
constexpr std::size_t kMaxMeshNamePrefixLength = 24;
constexpr std::size_t kMaxMaterialNameLength = 255;
constexpr std::size_t kMaxIgnoredKeywords = 32;
constexpr std::size_t kMaxReportedTokenLength = 32;
constexpr std::size_t kMaxListedMaterials = 16;
// Every OBJ polygon becomes at least one JK face, so more polygons than the
// largest model holds can be refused while reading.
constexpr std::size_t kMaxPolygons = kJkModelMaxMeshes * kJkMeshMaxFaces;

// Tolerances relative to a polygon's extent L, the diagonal of its bounds.
constexpr double kPlanarTolerance = 1e-5;  // largest distance from the polygon's plane, times L
constexpr double kConvexTolerance = 1e-6;  // most negative corner sine still treated as straight
constexpr double kAreaTolerance = 1e-12;   // twice an area that counts as none, times L squared
constexpr double kEdgeTolerance = 1e-9;    // an edge this short (times L) joins coincident corners
constexpr double kTwoPi = 6.283185307179586476925286766559;

const ColorF kNoLight{ 0.0f, 0.0f, 0.0f, 1.0f };

// Stock JK props draw textured faces lit (gouraud) with perspective-correct
// texturing, and mark every mesh the same way.
constexpr std::int32_t kGeometryTextured = 4;
constexpr std::int32_t kGeometryNotDrawn = 0;
constexpr std::int32_t kLightingGouraud = 3;
constexpr std::int32_t kTexturePerspective = 1;

std::string lineText(const std::size_t line)
{
    return "line " + std::to_string(line);
}

[[noreturn]] void malformed(const std::size_t line, const std::string& what)
{
    refuse(ObjImportErrorKind::Malformed, "objMalformed", lineText(line) + ": " + what);
}

// A material name quoted in full (up to the longest name a request can map).
std::string quotedName(const std::string_view name)
{
    return "'" + std::string(name.substr(0, kMaxMaterialNameLength)) + "'";
}

struct Vec2
{
    double x{};
    double y{};
};

Vec2 operator-(const Vec2& a, const Vec2& b) noexcept
{
    return { a.x - b.x, a.y - b.y };
}

double cross2(const Vec2& a, const Vec2& b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

double dot2(const Vec2& a, const Vec2& b) noexcept
{
    return a.x * b.x + a.y * b.y;
}

double length2(const Vec2& a) noexcept
{
    return std::sqrt(dot2(a, a));
}

// The (unnormalized) Newell normal: its direction follows the right-hand rule
// over the corner order and its length is twice the polygon's area.
Vec3 newellNormal(const std::vector<Vec3>& points) noexcept
{
    Vec3 normal{};
    for ( std::size_t i = 0; i < points.size(); ++i )
    {
        const Vec3& a = points[i];
        const Vec3& b = points[(i + 1) % points.size()];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

// The decimal text of the float the engine reads for `value`, as a double, so
// the 3DO carries clean numbers that load to exactly that float.
double shortestFloatValue(const float value)
{
    std::array<char, 64> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if ( written.ec != std::errc{} )
    {
        return static_cast<double>(value) + 0.0;
    }
    double parsed = 0.0;
    const auto read = sed::text::fromChars(buffer.data(), written.ptr, parsed);
    if ( read.ec != std::errc{} || read.ptr != written.ptr )
    {
        return static_cast<double>(value) + 0.0;
    }
    return parsed + 0.0; // + 0.0 turns -0 into 0
}

double engineValue(const double value)
{
    return shortestFloatValue(static_cast<float>(value));
}

// Like engineValue, but never below `value` (radii must cover every vertex).
double engineValueAtLeast(const double value)
{
    float rounded = static_cast<float>(value);
    for ( int attempt = 0; attempt < 4; ++attempt )
    {
        const double candidate = shortestFloatValue(rounded);
        if ( candidate >= value )
        {
            return candidate;
        }
        rounded = std::nextafter(rounded, std::numeric_limits<float>::infinity());
    }
    return static_cast<double>(rounded);
}

TVector engineUnit(const Vec3& unit)
{
    return TVector{ engineValue(unit.x), engineValue(unit.y), engineValue(unit.z) };
}

bool isNameCharacter(const char ch) noexcept
{
    const auto byte = static_cast<unsigned char>(ch);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')
        || ch == '_' || ch == '-' || ch == '.';
}

bool isMatFileName(const std::string_view name) noexcept
{
    return name.size() > 4 && name.size() <= kJkModelNameMaxLength
        && std::all_of(name.begin(), name.end(), isNameCharacter)
        && sed::text::iendsWithAscii(name, ".mat");
}

// Source axes to JK axes (positions and directions alike).
Vec3 toJkAxes(const Vec3& p, const ObjUpAxis upAxis) noexcept
{
    return upAxis == ObjUpAxis::Y ? Vec3{ p.x, -p.z, p.y } : p;
}

} // namespace

namespace model_import
{

void validateOptions(const ObjImportOptions& options)
{
    const auto invalid = [](const std::string& message)
    {
        refuse(ObjImportErrorKind::InvalidOptions, "objInvalidOptions", message);
    };
    if ( !std::isfinite(options.scale) || options.scale <= 0.0 || options.scale > kMaxScale )
    {
        invalid("scale must be greater than 0 and at most 1000000 (a negative scale would mirror the model)");
    }
    if ( options.upAxis != ObjUpAxis::Y && options.upAxis != ObjUpAxis::Z )
    {
        invalid("upAxis must be y or z");
    }
    if ( options.origin != ObjOrigin::Base && options.origin != ObjOrigin::Center
        && options.origin != ObjOrigin::None )
    {
        invalid("origin must be base, center or none");
    }
    if ( options.normalPolicy != ObjNormalPolicy::Preserve && options.normalPolicy != ObjNormalPolicy::Recalculate )
    {
        invalid("normalPolicy must be preserve or recalculate");
    }
    const std::string& prefix = options.meshNamePrefix;
    if ( !prefix.empty()
        && (prefix.size() > kMaxMeshNamePrefixLength || !std::all_of(prefix.begin(), prefix.end(), isNameCharacter)) )
    {
        invalid("meshName must be 1 to 24 letters, digits, '_', '-' or '.' (3DO names cannot hold other characters)");
    }
    for ( std::size_t i = 0; i < options.materials.size(); ++i )
    {
        const ObjImportMaterial& material = options.materials[i];
        const bool printable = std::all_of(material.name.begin(), material.name.end(),
            [](const char ch) { return static_cast<unsigned char>(ch) >= 0x20u && ch != 0x7f; });
        if ( material.name.empty() || material.name.size() > kMaxMaterialNameLength || !printable )
        {
            invalid("material " + std::to_string(i)
                + ": name must be the usemtl or glTF material name (1 to 255 printable characters)");
        }
        for ( std::size_t j = 0; j < i; ++j )
        {
            if ( options.materials[j].name == material.name )
            {
                invalid("material " + inQuotes(material.name) + " is mapped twice");
            }
        }
        if ( !isMatFileName(material.mat) )
        {
            invalid("material " + inQuotes(material.name) + ": mat " + inQuotes(material.mat)
                + " must be a .mat file name of at most 31 letters, digits, '_', '-' or '.' "
                  "(JK keeps material names in 32-byte fields)");
        }
        if ( material.width < 1 || material.width > kMaxTextureSize || material.height < 1
            || material.height > kMaxTextureSize )
        {
            invalid("material " + inQuotes(material.name) + ": the texture size must be 1 to 4096 texels each way");
        }
        for ( std::size_t j = 0; j < i; ++j )
        {
            const ObjImportMaterial& other = options.materials[j];
            if ( sed::text::iequalsAscii(other.mat, material.mat)
                && (other.width != material.width || other.height != material.height) )
            {
                invalid("mat " + inQuotes(material.mat) + " is given two different sizes");
            }
        }
    }
}

} // namespace model_import

namespace
{

// ---------------------------------------------------------------------------
// Reading

enum class NumberStatus : std::uint8_t
{
    Ok,
    Malformed,
    NonFinite,
};

NumberStatus parseNumber(std::string_view token, double& value) noexcept
{
    if ( !token.empty() && token.front() == '+' )
    {
        token.remove_prefix(1);
        if ( !token.empty() && (token.front() == '+' || token.front() == '-') )
        {
            return NumberStatus::Malformed;
        }
    }
    if ( token.empty() )
    {
        return NumberStatus::Malformed;
    }
    const char* const first = token.data();
    const char* const last = token.data() + token.size();
    const auto result = sed::text::fromChars(first, last, value, std::chars_format::general);
    if ( result.ptr != last || result.ec == std::errc::invalid_argument )
    {
        return NumberStatus::Malformed;
    }
    if ( result.ec != std::errc{} || !std::isfinite(value) )
    {
        return NumberStatus::NonFinite;
    }
    return NumberStatus::Ok;
}

const char* unsupportedStatement(const std::string_view keyword) noexcept
{
    if ( keyword == "vp" || keyword == "cstype" || keyword == "deg" || keyword == "bmat" || keyword == "step"
        || keyword == "curv" || keyword == "curv2" || keyword == "surf" || keyword == "parm" || keyword == "trim"
        || keyword == "hole" || keyword == "scrv" || keyword == "sp" || keyword == "end" || keyword == "con" )
    {
        return "free-form curves and surfaces";
    }
    if ( keyword == "l" )
    {
        return "line elements";
    }
    if ( keyword == "p" )
    {
        return "point elements";
    }
    if ( keyword == "call" )
    {
        return "including another file (call)";
    }
    if ( keyword == "csh" )
    {
        return "shell commands (csh)";
    }
    if ( keyword == "usemap" || keyword == "maplib" )
    {
        return "texture map libraries (usemap/maplib)";
    }
    return nullptr;
}

class ObjReader
{
public:
    explicit ObjReader(const ObjNormalPolicy normalPolicy) : normalPolicy_(normalPolicy) {}

    SourceModel read(std::string_view text)
    {
        if ( text.size() > kObjImportMaxSourceBytes )
        {
            refuse(ObjImportErrorKind::Malformed, "objInputTooLarge", "the OBJ is larger than 16 MiB");
        }
        if ( text.substr(0, 3) == "\xEF\xBB\xBF" )
        {
            text.remove_prefix(3);
        }
        std::string logical{};
        bool continuing = false;
        std::size_t logicalLine = 0;
        std::size_t lineNumber = 0;
        std::size_t begin = 0;
        while ( begin < text.size() )
        {
            const std::size_t newline = text.find('\n', begin);
            const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
            std::string_view raw = text.substr(begin, end - begin);
            begin = newline == std::string_view::npos ? text.size() : newline + 1;
            ++lineNumber;
            if ( lineNumber > kObjImportMaxLines )
            {
                refuse(ObjImportErrorKind::Malformed, "objInputTooLarge", "the OBJ has more than 1048576 lines");
            }
            if ( raw.size() > kObjImportMaxLineBytes )
            {
                refuse(ObjImportErrorKind::Malformed, "objInputTooLarge",
                    lineText(lineNumber) + " is longer than 64 KiB");
            }
            if ( raw.find('\0') != std::string_view::npos )
            {
                malformed(lineNumber, "the line holds a NUL byte; the source is not OBJ text");
            }
            if ( const std::size_t hash = raw.find('#'); hash != std::string_view::npos )
            {
                raw = raw.substr(0, hash);
            }
            raw = sed::text::trimRight(raw);
            const bool continues = !raw.empty() && raw.back() == '\\';
            if ( continues )
            {
                raw.remove_suffix(1);
            }
            if ( !continuing && !continues )
            {
                statement(raw, lineNumber);
                continue;
            }
            if ( !continuing )
            {
                logicalLine = lineNumber;
            }
            logical.append(raw);
            logical.push_back(' ');
            if ( logical.size() > kObjImportMaxLineBytes )
            {
                refuse(ObjImportErrorKind::Malformed, "objInputTooLarge",
                    lineText(logicalLine) + " continues past 64 KiB");
            }
            continuing = continues;
            if ( !continuing )
            {
                statement(logical, logicalLine);
                logical.clear();
            }
        }
        if ( continuing )
        {
            statement(logical, logicalLine);
        }
        validateIndices();
        return std::move(obj_);
    }

private:
    void statement(const std::string_view text, const std::size_t line)
    {
        tokens_.clear();
        std::size_t cursor = 0;
        while ( cursor < text.size() )
        {
            while ( cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) )
            {
                ++cursor;
            }
            const std::size_t start = cursor;
            while ( cursor < text.size() && !std::isspace(static_cast<unsigned char>(text[cursor])) )
            {
                ++cursor;
            }
            if ( cursor > start )
            {
                tokens_.push_back(text.substr(start, cursor - start));
            }
        }
        if ( tokens_.empty() )
        {
            return;
        }
        const std::string_view keyword = tokens_[0];
        const std::size_t restStart = static_cast<std::size_t>(keyword.data() + keyword.size() - text.data());
        const std::string_view rest = sed::text::trimView(text.substr(restStart));
        args_.assign(tokens_.begin() + 1, tokens_.end());
        if ( keyword == "v" )
        {
            vertex(line);
        }
        else if ( keyword == "vt" )
        {
            textureCoordinate(line);
        }
        else if ( keyword == "vn" )
        {
            normal(line);
        }
        else if ( keyword == "f" || keyword == "fo" )
        {
            face(line);
        }
        else if ( keyword == "o" )
        {
            object_ = std::string(rest);
            group_.clear();
        }
        else if ( keyword == "g" )
        {
            if ( args_.size() > 1 )
            {
                refuse(ObjImportErrorKind::Refused, "objUnsupportedFeature",
                    lineText(line) + ": 'g' with several names puts faces in several groups at once; "
                    "each OBJ group becomes one JK mesh, so give every face one group");
            }
            group_ = args_.empty() ? std::string{} : std::string(args_[0]);
        }
        else if ( keyword == "usemtl" )
        {
            if ( rest.empty() )
            {
                malformed(line, "'usemtl' needs a material name");
            }
            useMaterial(rest);
        }
        else if ( keyword == "s" )
        {
            smoothing(line);
            if ( normalPolicy_ == ObjNormalPolicy::Recalculate )
            {
                ignore(keyword);
            }
        }
        else if ( const char* const what = unsupportedStatement(keyword) )
        {
            refuse(ObjImportErrorKind::Refused, "objUnsupportedFeature",
                lineText(line) + ": " + inQuotes(keyword) + " (" + what
                + ") is not supported; export the model as polygons only");
        }
        else
        {
            ignore(keyword);
        }
    }

    double number(const std::string_view token, const std::size_t line) const
    {
        double value = 0.0;
        switch ( parseNumber(token, value) )
        {
        case NumberStatus::Ok:
            return value;
        case NumberStatus::NonFinite:
            refuse(ObjImportErrorKind::Malformed, "objNonFinite",
                lineText(line) + ": " + inQuotes(token) + " is not a finite number in range");
        case NumberStatus::Malformed:
            break;
        }
        malformed(line, inQuotes(token) + " is not a number");
    }

    void vertex(const std::size_t line)
    {
        const std::size_t count = args_.size();
        if ( count < 3 || count == 5 || count > 7 )
        {
            malformed(line, "'v' needs x y z (and optionally w)");
        }
        std::array<double, 7> values{};
        for ( std::size_t i = 0; i < count; ++i )
        {
            values[i] = number(args_[i], line);
        }
        if ( count >= 6 )
        {
            refuse(ObjImportErrorKind::Refused, "objUnsupportedFeature",
                lineText(line) + ": vertex colours (v x y z r g b) are not supported; "
                "export without vertex colours or bake them into the texture");
        }
        if ( count == 4 && values[3] != 1.0 )
        {
            refuse(ObjImportErrorKind::Refused, "objUnsupportedFeature",
                lineText(line) + ": a rational vertex weight other than 1 is not supported");
        }
        obj_.vertices.push_back({ values[0], values[1], values[2] });
    }

    void textureCoordinate(const std::size_t line)
    {
        const std::size_t count = args_.size();
        if ( count < 1 || count > 3 )
        {
            malformed(line, "'vt' needs u [v [w]]");
        }
        std::array<double, 3> values{};
        for ( std::size_t i = 0; i < count; ++i )
        {
            values[i] = number(args_[i], line);
        }
        if ( count == 3 && values[2] != 0.0 )
        {
            refuse(ObjImportErrorKind::Refused, "objUnsupportedFeature",
                lineText(line) + ": 3D texture coordinates (vt u v w with w other than 0) are not supported");
        }
        obj_.textureCoordinates.push_back({ values[0], values[1] });
    }

    void normal(const std::size_t line)
    {
        if ( args_.size() != 3 )
        {
            malformed(line, "'vn' needs x y z");
        }
        Vec3 value{ number(args_[0], line), number(args_[1], line), number(args_[2], line) };
        // Scale before normalizing so even finite near-DBL_MAX or subnormal
        // OBJ normal values keep their direction without overflow/underflow.
        const double scale = std::max({ std::fabs(value.x), std::fabs(value.y), std::fabs(value.z) });
        if ( scale > 0.0 )
        {
            value = { value.x / scale, value.y / scale, value.z / scale };
            value = value * (1.0 / length(value));
        }
        obj_.normals.push_back(value);
    }

    void smoothing(const std::size_t line)
    {
        if ( args_.size() != 1 )
        {
            malformed(line, "'s' needs off, 0, on or a positive integer group");
        }
        if ( args_[0] == "off" )
        {
            smoothingGroup_ = 0;
        }
        else if ( args_[0] == "on" )
        {
            smoothingGroup_ = 1;
        }
        else
        {
            const auto token = args_[0];
            const auto result = std::from_chars(token.data(), token.data() + token.size(), smoothingGroup_);
            if ( result.ec != std::errc{} || result.ptr != token.data() + token.size() || smoothingGroup_ < 0 )
            {
                malformed(line, "'s' needs off, 0, on or a positive integer group");
            }
        }
    }

    std::int64_t index(
        const std::string_view token,
        const std::size_t defined,
        const char* const what,
        const std::size_t line) const
    {
        std::int64_t value = 0;
        const char* const first = token.data();
        const char* const last = token.data() + token.size();
        const auto result = std::from_chars(first, last, value);
        if ( token.empty() || result.ec != std::errc{} || result.ptr != last )
        {
            malformed(line, inQuotes(token) + " is not a valid " + what + " index");
        }
        if ( value == 0 )
        {
            malformed(line, std::string(what) + " index 0 is not valid (OBJ indices start at 1)");
        }
        if ( value > 0 )
        {
            return value - 1; // checked against the final count once the file is read
        }
        if ( static_cast<std::uint64_t>(-(value + 1)) >= defined )
        {
            refuse(ObjImportErrorKind::Malformed, "objIndexOutOfRange",
                lineText(line) + ": relative " + what + " index " + std::to_string(value)
                + " reaches before the first " + what);
        }
        return static_cast<std::int64_t>(defined) + value;
    }

    void face(const std::size_t line)
    {
        const std::size_t count = args_.size();
        if ( count < 3 )
        {
            malformed(line, "a face needs at least 3 corners");
        }
        if ( count > kJkFaceMaxVertices )
        {
            refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
                lineText(line) + ": the polygon has " + std::to_string(count)
                + " corners; JK faces hold at most 24 - reduce the model first");
        }
        if ( obj_.polygons.size() >= kMaxPolygons )
        {
            refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
                lineText(line) + ": the OBJ has more than " + std::to_string(kMaxPolygons)
                + " polygons; a JK model holds at most 64 meshes of 512 faces - reduce the model first");
        }
        SourcePolygon polygon{};
        polygon.line = line;
        polygon.mesh = meshIndex();
        polygon.material = material_;
        polygon.firstCorner = obj_.corners.size();
        polygon.cornerCount = count;
        polygon.smoothingGroup = smoothingGroup_;
        for ( std::size_t i = 0; i < count; ++i )
        {
            const std::string_view token = args_[i];
            const std::size_t firstSlash = token.find('/');
            const std::string_view vertexPart = token.substr(0, firstSlash);
            std::string_view texturePart{};
            std::string_view normalPart{};
            bool hasNormal = false;
            if ( firstSlash != std::string_view::npos )
            {
                const std::size_t secondSlash = token.find('/', firstSlash + 1);
                if ( secondSlash == std::string_view::npos )
                {
                    texturePart = token.substr(firstSlash + 1);
                    if ( texturePart.empty() )
                    {
                        malformed(line, inQuotes(token) + " is not a valid face corner");
                    }
                }
                else
                {
                    texturePart = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                    normalPart = token.substr(secondSlash + 1);
                    hasNormal = true;
                    if ( normalPart.empty() || normalPart.find('/') != std::string_view::npos )
                    {
                        malformed(line, inQuotes(token) + " is not a valid face corner");
                    }
                }
            }
            const bool hasTexture = !texturePart.empty();
            if ( i == 0 )
            {
                polygon.textured = hasTexture;
            }
            else if ( hasTexture != polygon.textured )
            {
                malformed(line, "the face mixes corners with and without texture coordinates");
            }
            SourceCorner corner{};
            corner.vertex = index(vertexPart, obj_.vertices.size(), "vertex", line);
            if ( hasTexture )
            {
                corner.texture = index(texturePart, obj_.textureCoordinates.size(), "texture coordinate", line);
            }
            if ( hasNormal )
            {
                corner.normal = index(normalPart, obj_.normals.size(), "normal", line);
            }
            obj_.corners.push_back(corner);
        }
        obj_.polygons.push_back(polygon);
    }

    std::size_t meshIndex()
    {
        std::string key = object_;
        key.push_back('\n');
        key += group_;
        const auto found = meshes_.find(key);
        if ( found != meshes_.end() )
        {
            return found->second;
        }
        const std::size_t index = obj_.meshes.size();
        obj_.meshes.push_back({ object_, group_ });
        meshes_.emplace(std::move(key), index);
        return index;
    }

    void useMaterial(const std::string_view name)
    {
        const std::string key(name);
        const auto found = materials_.find(key);
        if ( found != materials_.end() )
        {
            material_ = static_cast<std::int64_t>(found->second);
            return;
        }
        material_ = static_cast<std::int64_t>(obj_.materialNames.size());
        obj_.materialNames.push_back(key);
        materials_.emplace(key, obj_.materialNames.size() - 1);
    }

    void ignore(const std::string_view keyword)
    {
        const std::string name = obj_.ignored.size() < kMaxIgnoredKeywords
            ? std::string(keyword.substr(0, kMaxReportedTokenLength))
            : std::string("(other)");
        for ( auto& [seen, count] : obj_.ignored )
        {
            if ( seen == name )
            {
                ++count;
                return;
            }
        }
        obj_.ignored.emplace_back(name, 1u);
    }

    void validateIndices() const
    {
        for ( const SourcePolygon& polygon : obj_.polygons )
        {
            for ( std::size_t i = 0; i < polygon.cornerCount; ++i )
            {
                const SourceCorner& corner = obj_.corners[polygon.firstCorner + i];
                const auto check = [&polygon](const std::int64_t value, const std::size_t defined, const char* what)
                {
                    if ( value >= static_cast<std::int64_t>(defined) )
                    {
                        refuse(ObjImportErrorKind::Malformed, "objIndexOutOfRange",
                            lineText(polygon.line) + ": the face uses " + what + " " + std::to_string(value + 1)
                            + " but the OBJ defines " + std::to_string(defined));
                    }
                };
                check(corner.vertex, obj_.vertices.size(), "vertex");
                if ( corner.texture >= 0 )
                {
                    check(corner.texture, obj_.textureCoordinates.size(), "texture coordinate");
                }
                if ( corner.normal >= 0 )
                {
                    check(corner.normal, obj_.normals.size(), "normal");
                }
            }
        }
    }

    SourceModel obj_{};
    ObjNormalPolicy normalPolicy_;
    std::int64_t smoothingGroup_{ -1 };
    std::vector<std::string_view> tokens_{};
    std::vector<std::string_view> args_{};
    std::string object_{};
    std::string group_{};
    std::int64_t material_{ -1 };
    std::unordered_map<std::string, std::size_t> meshes_{};
    std::unordered_map<std::string, std::size_t> materials_{};
};

// ---------------------------------------------------------------------------
// Polygons

// A right-handed basis (u, v, n) of the plane through the origin with normal n.
void planeBasis(const Vec3& n, Vec3& u, Vec3& v) noexcept
{
    const double ax = std::fabs(n.x);
    const double ay = std::fabs(n.y);
    const double az = std::fabs(n.z);
    const Vec3 axis = (ax <= ay && ax <= az) ? Vec3{ 1.0, 0.0, 0.0 }
        : (ay <= az ? Vec3{ 0.0, 1.0, 0.0 } : Vec3{ 0.0, 0.0, 1.0 });
    u = cross(axis, n);
    u = u * (1.0 / length(u));
    v = cross(n, u);
}

// A simple polygon whose every corner turns left (or runs straight on): its
// turning angles add up to one full turn.
bool isConvex(const std::vector<Vec2>& q)
{
    const std::size_t n = q.size();
    double turning = 0.0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        const Vec2 e1 = q[i] - q[(i + n - 1) % n];
        const Vec2 e2 = q[(i + 1) % n] - q[i];
        const double scale = length2(e1) * length2(e2);
        if ( !(scale > 0.0) )
        {
            return false;
        }
        const double sine = cross2(e1, e2);
        if ( sine < -kConvexTolerance * scale )
        {
            return false;
        }
        turning += std::atan2(sine, dot2(e1, e2));
    }
    return std::fabs(turning - kTwoPi) <= 1e-6;
}

bool insideOrOn(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, const double tolerance) noexcept
{
    return cross2(b - a, p - a) >= -tolerance && cross2(c - b, p - b) >= -tolerance
        && cross2(a - c, p - c) >= -tolerance;
}

[[noreturn]] void triangulationFailed(const std::size_t line)
{
    refuse(ObjImportErrorKind::Refused, "objTriangulationFailed",
        lineText(line) + ": the polygon cannot be triangulated (it crosses itself or folds back); "
        "fix or triangulate it in the modeller first");
}

// Ear clipping of a counter-clockwise polygon. Ears need a strictly convex tip
// and no other corner inside or on them, so no triangle is degenerate; the
// triangles must cover exactly the polygon's area.
std::vector<std::array<std::size_t, 3>> earClip(const std::vector<Vec2>& q, const double extent, const std::size_t line)
{
    const double tolerance = kAreaTolerance * extent * extent;
    std::vector<std::size_t> remaining(q.size());
    std::iota(remaining.begin(), remaining.end(), std::size_t{ 0 });
    std::vector<std::array<std::size_t, 3>> triangles{};
    triangles.reserve(q.size() - 2);
    while ( remaining.size() > 3 )
    {
        const std::size_t m = remaining.size();
        bool clipped = false;
        for ( std::size_t k = 0; k < m && !clipped; ++k )
        {
            const std::size_t a = remaining[(k + m - 1) % m];
            const std::size_t b = remaining[k];
            const std::size_t c = remaining[(k + 1) % m];
            if ( cross2(q[b] - q[a], q[c] - q[b]) <= tolerance )
            {
                continue;
            }
            bool empty = true;
            for ( const std::size_t j : remaining )
            {
                if ( j != a && j != b && j != c && insideOrOn(q[j], q[a], q[b], q[c], tolerance) )
                {
                    empty = false;
                    break;
                }
            }
            if ( !empty )
            {
                continue;
            }
            triangles.push_back({ a, b, c });
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(k));
            clipped = true;
        }
        if ( !clipped )
        {
            triangulationFailed(line);
        }
    }
    if ( cross2(q[remaining[1]] - q[remaining[0]], q[remaining[2]] - q[remaining[1]]) <= tolerance )
    {
        triangulationFailed(line);
    }
    triangles.push_back({ remaining[0], remaining[1], remaining[2] });

    double polygonArea = 0.0;
    for ( std::size_t i = 0; i < q.size(); ++i )
    {
        polygonArea += cross2(q[i], q[(i + 1) % q.size()]);
    }
    double triangleArea = 0.0;
    for ( const auto& triangle : triangles )
    {
        triangleArea += cross2(q[triangle[1]] - q[triangle[0]], q[triangle[2]] - q[triangle[0]]);
    }
    if ( std::fabs(triangleArea - polygonArea) > 1e-9 * extent * extent + 1e-7 * std::fabs(polygonArea) )
    {
        triangulationFailed(line);
    }
    return triangles;
}

// Where a polygon is, for messages: its OBJ line, or its glTF primitive and
// triangle.
std::string polygonPlace(const SourceModel& source, const SourcePolygon& polygon)
{
    if ( source.format == ModelSourceFormat::Glb && polygon.place < source.places.size() )
    {
        return source.places[polygon.place] + " triangle " + std::to_string(polygon.line);
    }
    return lineText(polygon.line);
}

// ---------------------------------------------------------------------------
// Materials

struct MaterialPlan
{
    // SourceModel material name -> request material (-1 when not mapped)
    std::vector<std::int64_t> requestOfName{};
    // request material -> 3DO material slot (-1 when unused)
    std::vector<std::int32_t> slotOfRequest{};
    std::vector<std::string> slots{};
    std::vector<std::size_t> facesOfRequest{};
};

std::string modelMaterialList(const SourceModel& source)
{
    std::string list{};
    for ( std::size_t n = 0; n < source.materialNames.size() && n < kMaxListedMaterials; ++n )
    {
        list += (n == 0 ? "" : ", ") + quotedName(source.materialNames[n]);
    }
    if ( source.materialNames.size() > kMaxListedMaterials )
    {
        list += ", ...";
    }
    return list;
}

MaterialPlan planMaterials(const SourceModel& source, const ObjImportOptions& options)
{
    const bool gltf = source.format == ModelSourceFormat::Glb;
    MaterialPlan plan{};
    plan.requestOfName.assign(source.materialNames.size(), -1);
    for ( std::size_t n = 0; n < source.materialNames.size(); ++n )
    {
        for ( std::size_t r = 0; r < options.materials.size(); ++r )
        {
            if ( options.materials[r].name == source.materialNames[n] )
            {
                plan.requestOfName[n] = static_cast<std::int64_t>(r);
            }
        }
    }
    std::vector<bool> used(options.materials.size(), false);
    for ( const SourcePolygon& polygon : source.polygons )
    {
        if ( polygon.material < 0 )
        {
            if ( !options.allowUntextured )
            {
                refuse(ObjImportErrorKind::Refused, "objUntexturedFace",
                    polygonPlace(source, polygon)
                        + (gltf ? ": the primitive has no material; "
                                : ": the face has no material (no usemtl before it); ")
                        + "assign one in the modeller or set allowUntextured");
            }
            continue;
        }
        const std::string& name = source.materialNames[static_cast<std::size_t>(polygon.material)];
        const std::int64_t request = plan.requestOfName[static_cast<std::size_t>(polygon.material)];
        if ( request < 0 )
        {
            refuse(ObjImportErrorKind::Refused, "objMaterialUnmapped",
                polygonPlace(source, polygon) + (gltf ? ": material " : ": usemtl ") + quotedName(name)
                    + " is not mapped; add {name, mat, size or matFile} for it to the request's materials "
                      "(the model's materials: "
                    + modelMaterialList(source) + ")");
        }
        if ( !polygon.textured )
        {
            refuse(ObjImportErrorKind::Refused, "objMissingTextureCoordinates",
                polygonPlace(source, polygon) + ": the face uses material " + inQuotes(name)
                    + (gltf ? " but its primitive has no TEXCOORD_0" : " but has no texture coordinates")
                    + "; UV-unwrap the model first");
        }
        used[static_cast<std::size_t>(request)] = true;
    }
    plan.slotOfRequest.assign(options.materials.size(), -1);
    plan.facesOfRequest.assign(options.materials.size(), 0);
    for ( std::size_t r = 0; r < options.materials.size(); ++r )
    {
        if ( !used[r] )
        {
            continue;
        }
        for ( std::size_t s = 0; s < plan.slots.size(); ++s )
        {
            if ( sed::text::iequalsAscii(plan.slots[s], options.materials[r].mat) )
            {
                plan.slotOfRequest[r] = static_cast<std::int32_t>(s);
            }
        }
        if ( plan.slotOfRequest[r] < 0 )
        {
            plan.slotOfRequest[r] = static_cast<std::int32_t>(plan.slots.size());
            plan.slots.push_back(options.materials[r].mat);
        }
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Faces

// A face corner before the face is placed in a JK mesh.
struct PendingCorner
{
    std::size_t vertex{};      // SourceModel vertex
    std::int64_t normal{ -1 }; // SourceModel authored normal, -1 when none
    std::uint64_t texel{};     // float bits of the texel (u << 32 | v); untextured corners use (0, 0)
    bool textured{};
};

struct PendingFace
{
    std::int64_t request{ -1 }; // request material, -1 when the face has none
    std::size_t firstCorner{};
    std::size_t cornerCount{};
    Vec3 area{};                // Newell normal in JK axes: twice the face's area
    bool startsTriangulation{}; // the first face of a polygon that was triangulated
    std::int64_t smoothingGroup{ -1 };
};

// One source mesh's JK faces, in source order.
struct SourceFaces
{
    std::vector<PendingFace> faces{};
    std::vector<PendingCorner> corners{};
};

std::uint64_t texelKey(const float u, const float v) noexcept
{
    return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(u)) << 32u) | std::bit_cast<std::uint32_t>(v);
}

constexpr std::uint64_t kOriginTexel = 0; // texelKey(0.0f, 0.0f)

class FaceCollector
{
public:
    FaceCollector(
        const SourceModel& source,
        const ObjImportOptions& options,
        MaterialPlan& materials,
        const std::vector<Vec3>& positions)
        : source_(source), options_(options), materials_(materials), positions_(positions)
    {
    }

    // Appends the JK faces of one polygon. Returns false when a glTF face
    // without area was dropped (OBJ refuses it).
    bool addPolygon(const SourcePolygon& polygon, SourceFaces& out)
    {
        const std::size_t count = polygon.cornerCount;
        std::vector<Vec3> points(count);
        for ( std::size_t i = 0; i < count; ++i )
        {
            const SourceCorner& corner = source_.corners[polygon.firstCorner + i];
            for ( std::size_t j = 0; j < i; ++j )
            {
                if ( source_.corners[polygon.firstCorner + j].vertex == corner.vertex )
                {
                    if ( source_.dropDegenerate )
                    {
                        return false;
                    }
                    refuse(ObjImportErrorKind::Refused, "objDegeneratePolygon",
                        polygonPlace(source_, polygon) + ": the polygon uses vertex "
                            + std::to_string(corner.vertex + 1) + " twice; remove degenerate faces first");
                }
            }
            points[i] = positions_[static_cast<std::size_t>(corner.vertex)];
        }
        constexpr double kInfinity = std::numeric_limits<double>::infinity();
        Vec3 low{ kInfinity, kInfinity, kInfinity };
        Vec3 high{ -kInfinity, -kInfinity, -kInfinity };
        Vec3 centroid{};
        for ( const Vec3& p : points )
        {
            low = { std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z) };
            high = { std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z) };
            centroid = centroid + p;
        }
        centroid = centroid * (1.0 / static_cast<double>(count));
        const double extent = length(high - low);
        std::vector<Vec3> relative(count);
        for ( std::size_t i = 0; i < count; ++i )
        {
            relative[i] = points[i] - centroid;
        }
        const Vec3 areaNormal = newellNormal(relative);
        const double area2 = length(areaNormal);
        if ( !(extent > 0.0) || !(area2 > kAreaTolerance * extent * extent) )
        {
            if ( source_.dropDegenerate )
            {
                return false;
            }
            refuse(ObjImportErrorKind::Refused, "objDegeneratePolygon",
                polygonPlace(source_, polygon) + ": the polygon has no area (its corners coincide or lie on a line); "
                "remove degenerate faces first");
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( length(points[(i + 1) % count] - points[i]) <= kEdgeTolerance * extent )
            {
                if ( source_.dropDegenerate )
                {
                    return false;
                }
                refuse(ObjImportErrorKind::Refused, "objDegeneratePolygon",
                    polygonPlace(source_, polygon) + ": two corners of the polygon are at the same position; "
                    "remove degenerate faces first");
            }
        }
        const Vec3 unit = areaNormal * (1.0 / area2);

        std::vector<std::vector<std::size_t>> faces{};
        bool triangulated = false;
        if ( count == 3 )
        {
            faces.push_back({ 0, 1, 2 });
        }
        else
        {
            double deviation = 0.0;
            for ( const Vec3& p : relative )
            {
                deviation = std::max(deviation, std::fabs(dot(p, unit)));
            }
            Vec3 u{};
            Vec3 v{};
            planeBasis(unit, u, v);
            std::vector<Vec2> projected(count);
            for ( std::size_t i = 0; i < count; ++i )
            {
                projected[i] = { dot(relative[i], u), dot(relative[i], v) };
            }
            if ( deviation <= kPlanarTolerance * extent && isConvex(projected) )
            {
                std::vector<std::size_t> all(count);
                std::iota(all.begin(), all.end(), std::size_t{ 0 });
                faces.push_back(std::move(all));
            }
            else
            {
                for ( const auto& triangle : earClip(projected, extent, polygon.line) )
                {
                    faces.push_back({ triangle[0], triangle[1], triangle[2] });
                }
                triangulated = true;
            }
        }
        for ( std::size_t f = 0; f < faces.size(); ++f )
        {
            const std::vector<std::size_t>& face = faces[f];
            std::vector<Vec3> facePoints{};
            facePoints.reserve(face.size());
            for ( const std::size_t corner : face )
            {
                facePoints.push_back(relative[corner]);
            }
            const Vec3 faceArea = newellNormal(facePoints);
            const double faceArea2 = length(faceArea);
            if ( !(faceArea2 > kAreaTolerance * extent * extent) || dot(faceArea, areaNormal) <= 0.0 )
            {
                triangulationFailed(polygon.line);
            }
            emit(polygon, face, faceArea, triangulated && f == 0, out);
        }
        return true;
    }

private:
    void emit(
        const SourcePolygon& polygon,
        const std::vector<std::size_t>& face,
        const Vec3& area,
        const bool startsTriangulation,
        SourceFaces& out)
    {
        PendingFace pending{};
        pending.firstCorner = out.corners.size();
        pending.cornerCount = face.size();
        pending.area = area;
        pending.startsTriangulation = startsTriangulation;
        pending.smoothingGroup = polygon.smoothingGroup;
        const ObjImportMaterial* material = nullptr;
        if ( polygon.material >= 0 )
        {
            const auto request = static_cast<std::size_t>(
                materials_.requestOfName[static_cast<std::size_t>(polygon.material)]);
            material = &options_.materials[request];
            pending.request = static_cast<std::int64_t>(request);
            ++materials_.facesOfRequest[request];
        }
        for ( const std::size_t corner : face )
        {
            const SourceCorner& source = source_.corners[polygon.firstCorner + corner];
            PendingCorner out_corner{};
            out_corner.vertex = static_cast<std::size_t>(source.vertex);
            if ( options_.normalPolicy == ObjNormalPolicy::Preserve && source.normal >= 0
                && length(source_.normals[static_cast<std::size_t>(source.normal)]) > 0.0 )
            {
                out_corner.normal = source.normal;
            }
            if ( material )
            {
                const auto& uv = source_.textureCoordinates[static_cast<std::size_t>(source.texture)];
                const double u = uv[0] * static_cast<double>(material->width);
                const double v = (source_.textureOriginTop ? uv[1] : 1.0 - uv[1])
                    * static_cast<double>(material->height);
                if ( !std::isfinite(u) || !std::isfinite(v) || std::fabs(u) > kMaxTexel || std::fabs(v) > kMaxTexel )
                {
                    refuse(ObjImportErrorKind::Refused, "objTextureCoordinateOutOfRange",
                        polygonPlace(source_, polygon) + ": a texture coordinate lies beyond +/-10000000 texels");
                }
                out_corner.texel = texelKey(static_cast<float>(u) + 0.0f, static_cast<float>(v) + 0.0f);
                out_corner.textured = true;
            }
            else
            {
                out_corner.texel = kOriginTexel;
            }
            out.corners.push_back(out_corner);
        }
        out.faces.push_back(pending);
    }

    const SourceModel& source_;
    const ObjImportOptions& options_;
    MaterialPlan& materials_;
    const std::vector<Vec3>& positions_;
};

// What a set of faces needs as one JK mesh.
struct MeshNeeds
{
    std::size_t faces{};
    std::size_t vertices{};
    std::size_t texels{};

    bool fits() const noexcept
    {
        return faces <= kJkMeshMaxFaces && vertices <= kJkMeshMaxVertices && texels <= kJkMeshMaxTextureVertices;
    }
};

// A group of faces being filled up to the JK mesh limits. Untextured corners
// count as the shared (0, 0) texel, as they are written.
class FaceGroup
{
public:
    explicit FaceGroup(const SourceFaces& source) : source_(&source) {}

    bool empty() const noexcept
    {
        return faces_.empty();
    }

    MeshNeeds needsWith(const std::size_t face) const
    {
        MeshNeeds needs{ faces_.size() + 1, vertices_.size(), texels_.size() };
        const PendingFace& f = source_->faces[face];
        for ( std::size_t c = 0; c < f.cornerCount; ++c )
        {
            const PendingCorner& corner = source_->corners[f.firstCorner + c];
            bool newVertex = !vertices_.contains(corner.vertex);
            bool newTexel = !texels_.contains(corner.texel);
            for ( std::size_t d = 0; d < c && (newVertex || newTexel); ++d )
            {
                const PendingCorner& earlier = source_->corners[f.firstCorner + d];
                newVertex = newVertex && earlier.vertex != corner.vertex;
                newTexel = newTexel && earlier.texel != corner.texel;
            }
            needs.vertices += newVertex ? 1u : 0u;
            needs.texels += newTexel ? 1u : 0u;
        }
        return needs;
    }

    MeshNeeds needsWithAll(const std::vector<std::size_t>& faces) const
    {
        std::unordered_set<std::size_t> vertices{};
        std::unordered_set<std::uint64_t> texels{};
        for ( const std::size_t face : faces )
        {
            const PendingFace& f = source_->faces[face];
            for ( std::size_t c = 0; c < f.cornerCount; ++c )
            {
                const PendingCorner& corner = source_->corners[f.firstCorner + c];
                if ( !vertices_.contains(corner.vertex) )
                {
                    vertices.insert(corner.vertex);
                }
                if ( !texels_.contains(corner.texel) )
                {
                    texels.insert(corner.texel);
                }
            }
        }
        return { faces_.size() + faces.size(), vertices_.size() + vertices.size(), texels_.size() + texels.size() };
    }

    void add(const std::size_t face)
    {
        faces_.push_back(face);
        const PendingFace& f = source_->faces[face];
        for ( std::size_t c = 0; c < f.cornerCount; ++c )
        {
            vertices_.insert(source_->corners[f.firstCorner + c].vertex);
            texels_.insert(source_->corners[f.firstCorner + c].texel);
        }
    }

    // The group's faces in source order; the group is left empty.
    std::vector<std::size_t> take()
    {
        std::vector<std::size_t> faces = std::move(faces_);
        std::sort(faces.begin(), faces.end());
        faces_.clear();
        vertices_.clear();
        texels_.clear();
        return faces;
    }

private:
    const SourceFaces* source_;
    std::vector<std::size_t> faces_{};
    std::unordered_set<std::size_t> vertices_{};
    std::unordered_set<std::uint64_t> texels_{};
};

MeshNeeds meshNeeds(const SourceFaces& source)
{
    std::vector<std::size_t> all(source.faces.size());
    std::iota(all.begin(), all.end(), std::size_t{ 0 });
    return FaceGroup(source).needsWithAll(all);
}

// Splits one source mesh's faces into groups that each fit a JK mesh. Faces
// are taken breadth-first over shared vertices from the lowest unplaced face,
// so a group is a connected patch and the next group continues from its
// edge; a connected part that would fit a fresh group is not started in a
// group it would overflow. The order is fixed by face indices alone, and
// every face lands in exactly one group, in source order within it.
std::vector<std::vector<std::size_t>> splitFaces(const SourceFaces& source)
{
    const std::size_t count = source.faces.size();
    std::unordered_map<std::size_t, std::vector<std::size_t>> facesOfVertex{};
    for ( std::size_t f = 0; f < count; ++f )
    {
        const PendingFace& face = source.faces[f];
        for ( std::size_t c = 0; c < face.cornerCount; ++c )
        {
            std::vector<std::size_t>& list = facesOfVertex[source.corners[face.firstCorner + c].vertex];
            if ( list.empty() || list.back() != f )
            {
                list.push_back(f);
            }
        }
    }

    // Connected parts: faces joined through shared vertices, numbered by
    // their lowest face.
    std::vector<std::size_t> parent(count);
    std::iota(parent.begin(), parent.end(), std::size_t{ 0 });
    const auto find = [&parent](std::size_t x)
    {
        while ( parent[x] != x )
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for ( const auto& [vertex, faces] : facesOfVertex )
    {
        (void)vertex;
        for ( std::size_t i = 1; i < faces.size(); ++i )
        {
            const std::size_t a = find(faces[0]);
            const std::size_t b = find(faces[i]);
            if ( a != b )
            {
                parent[std::max(a, b)] = std::min(a, b);
            }
        }
    }
    std::vector<std::size_t> partOf(count);
    std::vector<std::vector<std::size_t>> parts{};
    std::unordered_map<std::size_t, std::size_t> partOfRoot{};
    for ( std::size_t f = 0; f < count; ++f )
    {
        const auto [found, inserted] = partOfRoot.emplace(find(f), parts.size());
        if ( inserted )
        {
            parts.emplace_back();
        }
        partOf[f] = found->second;
        parts[found->second].push_back(f);
    }
    std::vector<bool> partFitsAlone(parts.size());
    for ( std::size_t p = 0; p < parts.size(); ++p )
    {
        partFitsAlone[p] = FaceGroup(source).needsWithAll(parts[p]).fits();
    }

    std::vector<std::vector<std::size_t>> groups{};
    FaceGroup current(source);
    std::vector<bool> queued(count, false);
    std::unordered_set<std::size_t> expanded{};
    std::deque<std::size_t> queue{};
    for ( std::size_t seed = 0; seed < count; ++seed )
    {
        if ( queued[seed] )
        {
            continue;
        }
        const std::size_t part = partOf[seed];
        if ( !current.empty() && partFitsAlone[part] && !current.needsWithAll(parts[part]).fits() )
        {
            groups.push_back(current.take());
        }
        queue.push_back(seed);
        queued[seed] = true;
        while ( !queue.empty() )
        {
            const std::size_t f = queue.front();
            queue.pop_front();
            if ( !current.needsWith(f).fits() )
            {
                groups.push_back(current.take());
            }
            current.add(f);
            const PendingFace& face = source.faces[f];
            for ( std::size_t c = 0; c < face.cornerCount; ++c )
            {
                const std::size_t vertex = source.corners[face.firstCorner + c].vertex;
                if ( !expanded.insert(vertex).second )
                {
                    continue;
                }
                for ( const std::size_t next : facesOfVertex[vertex] )
                {
                    if ( !queued[next] )
                    {
                        queued[next] = true;
                        queue.push_back(next);
                    }
                }
            }
        }
    }
    if ( !current.empty() )
    {
        groups.push_back(current.take());
    }
    return groups;
}

// A 3DO vertex has one normal, while OBJ/glTF corners can have distinct
// normals at the same position. Allocate those vertex identities before
// partitioning so JK limits count the actual output and partitions retain
// matching normals. Texture coordinates deliberately do not enter the key.
std::size_t splitNormalVertices(
    SourceFaces& faces,
    const SourceModel& source,
    const ObjImportOptions& options,
    std::vector<Vec3>& positions)
{
    if ( options.normalPolicy == ObjNormalPolicy::Recalculate )
    {
        return 0;
    }
    std::map<std::array<std::uint64_t, 5>, std::size_t> vertexOfKey{};
    std::unordered_set<std::size_t> usedPositions{};
    const std::size_t initialPositions = positions.size();
    for ( std::size_t f = 0; f < faces.faces.size(); ++f )
    {
        const PendingFace& face = faces.faces[f];
        for ( std::size_t c = 0; c < face.cornerCount; ++c )
        {
            PendingCorner& corner = faces.corners[face.firstCorner + c];
            const std::size_t original = corner.vertex;
            std::array<std::uint64_t, 5> key{ original, 0, 0, 0, 0 };
            if ( corner.normal >= 0 )
            {
                const Vec3 authored = toJkAxes(source.normals[static_cast<std::size_t>(corner.normal)], options.upAxis);
                const TVector direction = engineUnit(authored * (1.0 / length(authored)));
                key[1] = 3;
                key[2] = std::bit_cast<std::uint32_t>(static_cast<float>(direction.x) + 0.0f);
                key[3] = std::bit_cast<std::uint32_t>(static_cast<float>(direction.y) + 0.0f);
                key[4] = std::bit_cast<std::uint32_t>(static_cast<float>(direction.z) + 0.0f);
            }
            else if ( face.smoothingGroup == 0 )
            {
                key[1] = 2;
                key[2] = f;
            }
            else if ( face.smoothingGroup > 0 )
            {
                key[1] = 1;
                key[2] = static_cast<std::uint64_t>(face.smoothingGroup);
            }
            const auto [found, inserted] = vertexOfKey.try_emplace(key, original);
            if ( inserted && !usedPositions.insert(original).second )
            {
                found->second = positions.size();
                const Vec3 position = positions[original];
                positions.push_back(position);
            }
            corner.vertex = found->second;
        }
    }
    return positions.size() - initialPositions;
}

// Engine-rounded unit vertex normals for every vertex of one source mesh:
// the authored normal for its already-partitioned identity, otherwise the
// area-weighted average of the faces using it, otherwise (faces that cancel, e.g. two
// sides of a sheet) the first such face's normal. Worked out before
// splitting, so meshes split from one source agree where they meet.
std::unordered_map<std::size_t, TVector> vertexNormals(
    const SourceFaces& faces,
    const SourceModel& source,
    const ObjUpAxis upAxis)
{
    struct Sums
    {
        Vec3 authored{};
        Vec3 area{};
        std::size_t firstFace{};
        bool hasAuthored{};
    };
    std::unordered_map<std::size_t, Sums> sums{};
    for ( std::size_t f = 0; f < faces.faces.size(); ++f )
    {
        const PendingFace& face = faces.faces[f];
        for ( std::size_t c = 0; c < face.cornerCount; ++c )
        {
            const PendingCorner& corner = faces.corners[face.firstCorner + c];
            const auto [found, inserted] = sums.try_emplace(corner.vertex);
            Sums& sum = found->second;
            if ( inserted )
            {
                sum.firstFace = f;
            }
            sum.area = sum.area + face.area;
            if ( corner.normal < 0 )
            {
                continue;
            }
            if ( !sum.hasAuthored )
            {
                sum.authored = toJkAxes(source.normals[static_cast<std::size_t>(corner.normal)], upAxis);
                sum.hasAuthored = true;
            }
        }
    }
    std::unordered_map<std::size_t, TVector> normals{};
    for ( const auto& [vertex, sum] : sums )
    {
        const double authored = length(sum.authored);
        const double area = length(sum.area);
        Vec3 unit{};
        if ( authored > 1e-6 && std::isfinite(authored) )
        {
            unit = sum.authored * (1.0 / authored);
        }
        else if ( area > 0.0 && std::isfinite(area) )
        {
            unit = sum.area * (1.0 / area);
        }
        else
        {
            const Vec3& first = faces.faces[sum.firstFace].area;
            unit = first * (1.0 / length(first));
        }
        normals.emplace(vertex, engineUnit(unit));
    }
    return normals;
}

// One JK mesh from a group of a source mesh's faces: vertices and texture
// vertices are numbered in first-use order, untextured faces share a (0, 0)
// texture vertex, normals come from the whole source mesh.
Model3doMesh buildMesh(
    const SourceFaces& source,
    const std::vector<std::size_t>& group,
    const MaterialPlan& materials,
    const std::vector<Vec3>& positions,
    const std::unordered_map<std::size_t, TVector>& normals,
    std::string name,
    std::size_t& triangulated,
    std::size_t& untextured)
{
    Model3doMesh mesh{};
    mesh.name = std::move(name);
    mesh.geo = kGeometryTextured;
    mesh.light = kLightingGouraud;
    mesh.tex = kTexturePerspective;
    std::unordered_map<std::size_t, std::int32_t> localOfVertex{};
    std::vector<std::size_t> vertexOfLocal{};
    std::unordered_map<std::uint64_t, std::int32_t> texelIndex{};
    const auto texel = [&mesh, &texelIndex](const std::uint64_t key)
    {
        const auto [found, inserted] = texelIndex.try_emplace(key, static_cast<std::int32_t>(mesh.texVertices.size()));
        if ( inserted )
        {
            mesh.texVertices.push_back({ std::bit_cast<float>(static_cast<std::uint32_t>(key >> 32u)),
                std::bit_cast<float>(static_cast<std::uint32_t>(key & 0xFFFFFFFFu)) });
        }
        return found->second;
    };
    std::vector<std::size_t> untexturedFaces{};
    for ( const std::size_t f : group )
    {
        const PendingFace& face = source.faces[f];
        Model3doFace out{};
        out.faceFlags = 0;
        out.light = kLightingGouraud;
        out.tex = kTexturePerspective;
        out.extraLight = kNoLight;
        if ( face.request >= 0 )
        {
            out.material = materials.slotOfRequest[static_cast<std::size_t>(face.request)];
            out.geo = kGeometryTextured;
        }
        else
        {
            out.material = -1;
            out.geo = kGeometryNotDrawn;
        }
        for ( std::size_t c = 0; c < face.cornerCount; ++c )
        {
            const PendingCorner& corner = source.corners[face.firstCorner + c];
            const auto [found, inserted] = localOfVertex.try_emplace(
                corner.vertex, static_cast<std::int32_t>(vertexOfLocal.size()));
            if ( inserted )
            {
                vertexOfLocal.push_back(corner.vertex);
                const Vec3& p = positions[corner.vertex];
                Model3doVertex vertex{};
                vertex.x = p.x;
                vertex.y = p.y;
                vertex.z = p.z;
                vertex.color = kNoLight;
                mesh.vertices.push_back(vertex);
            }
            out.vertices.push_back(found->second);
            if ( corner.textured )
            {
                out.texVertices.push_back(texel(corner.texel));
            }
        }
        if ( face.request < 0 )
        {
            untexturedFaces.push_back(mesh.faces.size());
        }
        if ( face.startsTriangulation )
        {
            ++triangulated;
        }
        mesh.faceNormals.push_back(engineUnit(face.area * (1.0 / length(face.area))));
        mesh.faces.push_back(std::move(out));
    }
    if ( !untexturedFaces.empty() )
    {
        const std::int32_t origin = texel(kOriginTexel);
        for ( const std::size_t face : untexturedFaces )
        {
            mesh.faces[face].texVertices.assign(mesh.faces[face].vertices.size(), origin);
        }
    }
    untextured += untexturedFaces.size();
    mesh.vertexNormals.reserve(vertexOfLocal.size());
    for ( const std::size_t vertex : vertexOfLocal )
    {
        mesh.vertexNormals.push_back(normals.at(vertex));
    }
    double radius = 0.0;
    for ( const Model3doVertex& vertex : mesh.vertices )
    {
        radius = std::max(radius, length(Vec3{ vertex.x, vertex.y, vertex.z }));
    }
    mesh.radius = engineValueAtLeast(radius);
    mesh.authoredRadius = mesh.radius;
    return mesh;
}

// ---------------------------------------------------------------------------
// Names

std::string sanitizedName(const std::string_view source)
{
    std::string name(source);
    for ( char& ch : name )
    {
        if ( !isNameCharacter(ch) )
        {
            ch = '_';
        }
    }
    return name;
}

std::string sourceName(const SourceMeshName& mesh)
{
    return mesh.group.empty() ? mesh.object : mesh.group;
}

// A source mesh's name before it is shortened or made unique.
std::string baseMeshName(const SourceMeshName& mesh, const std::size_t index, const std::string& prefix)
{
    const std::string base = sanitizedName(sourceName(mesh));
    return base.empty() ? (prefix.empty() ? std::string("mesh") : prefix) + std::to_string(index) : prefix + base;
}

// Engine-sized names, unique without regard to case: a repeated name gets
// "_2", "_3"... in place of its last characters.
std::vector<std::string> uniqueNames(const std::vector<std::string>& wanted)
{
    std::vector<std::string> names{};
    std::unordered_set<std::string> taken{};
    for ( std::string name : wanted )
    {
        name.resize(std::min(name.size(), kJkModelNameMaxLength));
        std::string candidate = name;
        for ( std::size_t n = 2; taken.contains(sed::text::lowerAscii(candidate)); ++n )
        {
            const std::string suffix = "_" + std::to_string(n);
            candidate = name.substr(0, kJkModelNameMaxLength - suffix.size()) + suffix;
        }
        taken.insert(sed::text::lowerAscii(candidate));
        names.push_back(std::move(candidate));
    }
    return names;
}

// Positions in JK axes, scaled and moved to the requested origin, then
// rounded to the floats the engine reads. Only vertices that faces use count.
std::vector<Vec3> placeVertices(const SourceModel& source, const ObjImportOptions& options, ObjImportResult& result)
{
    std::vector<char> used(source.vertices.size(), 0);
    for ( const SourceCorner& corner : source.corners )
    {
        used[static_cast<std::size_t>(corner.vertex)] = 1;
    }
    std::vector<Vec3> positions(source.vertices.size());
    const auto outOfRange = [&source](const std::size_t vertex)
    {
        refuse(ObjImportErrorKind::Refused, "objCoordinateOutOfRange",
            (source.format == ModelSourceFormat::Glb ? std::string("a glTF vertex")
                                                     : "OBJ vertex " + std::to_string(vertex + 1))
                + " lies beyond +/-1000000 JKU after scaling and placement; scale the model down");
    };
    bool first = true;
    Vec3 low{};
    Vec3 high{};
    for ( std::size_t i = 0; i < source.vertices.size(); ++i )
    {
        if ( !used[i] )
        {
            continue;
        }
        const Vec3 scaled = toJkAxes(source.vertices[i], options.upAxis) * options.scale;
        if ( !isFinite(scaled) )
        {
            outOfRange(i);
        }
        positions[i] = scaled;
        if ( first )
        {
            low = scaled;
            high = scaled;
            first = false;
        }
        low = { std::min(low.x, scaled.x), std::min(low.y, scaled.y), std::min(low.z, scaled.z) };
        high = { std::max(high.x, scaled.x), std::max(high.y, scaled.y), std::max(high.z, scaled.z) };
    }
    Vec3 offset{};
    if ( options.origin == ObjOrigin::Base )
    {
        offset = { -(low.x + high.x) / 2.0, -(low.y + high.y) / 2.0, -low.z };
    }
    else if ( options.origin == ObjOrigin::Center )
    {
        offset = (low + high) * -0.5;
    }
    first = true;
    for ( std::size_t i = 0; i < source.vertices.size(); ++i )
    {
        if ( !used[i] )
        {
            continue;
        }
        const Vec3 placed = positions[i] + offset;
        if ( !isFinite(placed) || std::fabs(placed.x) > kMaxCoordinate || std::fabs(placed.y) > kMaxCoordinate
            || std::fabs(placed.z) > kMaxCoordinate )
        {
            outOfRange(i);
        }
        const Vec3 rounded{ engineValue(placed.x), engineValue(placed.y), engineValue(placed.z) };
        positions[i] = rounded;
        if ( first )
        {
            result.boundsMin = TVector{ rounded.x, rounded.y, rounded.z };
            result.boundsMax = result.boundsMin;
            first = false;
        }
        result.boundsMin = TVector{ std::min(result.boundsMin.x, rounded.x), std::min(result.boundsMin.y, rounded.y),
            std::min(result.boundsMin.z, rounded.z) };
        result.boundsMax = TVector{ std::max(result.boundsMax.x, rounded.x), std::max(result.boundsMax.y, rounded.y),
            std::max(result.boundsMax.z, rounded.z) };
    }
    return positions;
}

} // namespace

namespace model_import
{

ObjImportResult buildModel(const SourceModel& source, const ObjImportOptions& options)
{
    const bool gltf = source.format == ModelSourceFormat::Glb;
    ObjImportResult result{};
    result.format = source.format;
    result.polygons = source.polygons.size();
    if ( !gltf )
    {
        result.ignoredStatements = source.ignored;
    }
    result.degenerateTriangles = source.droppedDegenerate;
    if ( source.polygons.empty() )
    {
        refuse(ObjImportErrorKind::Refused, "objNoFaces",
            gltf ? (source.droppedDegenerate > 0 ? "every triangle of the glTF is degenerate (has no area)"
                                                 : "the glTF has no triangles")
                 : "the OBJ has no faces");
    }
    MaterialPlan materials = planMaterials(source, options);
    if ( source.meshes.size() > kJkModelMaxMeshes )
    {
        refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
            gltf ? "the glTF has " + std::to_string(source.meshes.size())
                    + " meshes (nodes with meshes); a JK model holds at most 64 - merge meshes or reduce the model first"
                 : "the OBJ has " + std::to_string(source.meshes.size())
                    + " meshes (objects/groups with faces); a JK model holds at most 64 - merge groups or reduce the "
                      "model first");
    }
    std::vector<Vec3> positions = placeVertices(source, options, result);

    std::vector<std::vector<std::size_t>> polygonsOfMesh(source.meshes.size());
    for ( std::size_t p = 0; p < source.polygons.size(); ++p )
    {
        polygonsOfMesh[source.polygons[p].mesh].push_back(p);
    }
    std::vector<std::string> baseNames{};
    for ( std::size_t m = 0; m < source.meshes.size(); ++m )
    {
        baseNames.push_back(baseMeshName(source.meshes[m], m, options.meshNamePrefix));
    }
    const std::vector<std::string> unsplitNames = uniqueNames(baseNames);

    struct Piece
    {
        std::size_t source{};
        std::size_t part{};
        std::size_t parts{};
        std::vector<std::size_t> faces{};
    };
    FaceCollector collector(source, options, materials, positions);
    std::vector<SourceFaces> faceSets(source.meshes.size());
    std::vector<std::unordered_map<std::size_t, TVector>> normalSets(source.meshes.size());
    std::vector<Piece> pieces{};
    std::vector<std::size_t> splitOfSource(source.meshes.size(), std::numeric_limits<std::size_t>::max());
    for ( std::size_t m = 0; m < source.meshes.size(); ++m )
    {
        SourceFaces& faces = faceSets[m];
        for ( const std::size_t p : polygonsOfMesh[m] )
        {
            if ( !collector.addPolygon(source.polygons[p], faces) )
            {
                ++result.degenerateTriangles;
            }
        }
        if ( faces.faces.empty() )
        {
            continue; // every triangle of this glTF mesh had no area
        }
        result.normalSplitVertices += splitNormalVertices(faces, source, options, positions);
        const MeshNeeds needs = meshNeeds(faces);
        std::vector<std::vector<std::size_t>> groups{};
        if ( needs.fits() )
        {
            groups.emplace_back(faces.faces.size());
            std::iota(groups.back().begin(), groups.back().end(), std::size_t{ 0 });
        }
        else if ( !options.split )
        {
            refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
                "mesh " + inQuotes(unsplitNames[m]) + " needs " + std::to_string(needs.faces) + " faces, "
                    + std::to_string(needs.vertices) + " vertices and " + std::to_string(needs.texels)
                    + " texture vertices; a JK mesh holds at most 512 faces, 512 vertices and 768 texture "
                      "vertices - reduce the model first, or leave split on to divide it into several meshes");
        }
        else
        {
            groups = splitFaces(faces);
            ObjImportSplitReport split{};
            split.source = sourceName(source.meshes[m]);
            split.faces = needs.faces;
            split.vertices = needs.vertices;
            split.textureVertices = needs.texels;
            splitOfSource[m] = result.splits.size();
            result.splits.push_back(std::move(split));
        }
        normalSets[m] = vertexNormals(faces, source, options.upAxis);
        for ( std::size_t g = 0; g < groups.size(); ++g )
        {
            pieces.push_back({ m, g + 1, groups.size(), std::move(groups[g]) });
        }
    }
    if ( pieces.empty() )
    {
        refuse(ObjImportErrorKind::Refused, "objNoFaces", "every triangle of the glTF is degenerate (has no area)");
    }
    if ( pieces.size() > kJkModelMaxMeshes )
    {
        refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
            "the model needs " + std::to_string(pieces.size())
                + " meshes once meshes over JK's limits are split; a JK model holds at most 64 - reduce the model "
                  "first");
    }
    std::vector<std::string> wanted{};
    for ( const Piece& piece : pieces )
    {
        std::string name = baseNames[piece.source];
        if ( piece.parts > 1 )
        {
            const std::string suffix = "_" + std::to_string(piece.part);
            name = name.substr(0, kJkModelNameMaxLength - suffix.size()) + suffix;
        }
        wanted.push_back(std::move(name));
    }
    const std::vector<std::string> names = uniqueNames(wanted);

    Model3do& model = result.model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    model.insertOffset = TVector{ 0.0, 0.0, 0.0 };
    for ( std::size_t i = 0; i < pieces.size(); ++i )
    {
        const Piece& piece = pieces[i];
        std::size_t triangulated = 0;
        std::size_t untextured = 0;
        Model3doMesh mesh = buildMesh(faceSets[piece.source], piece.faces, materials, positions,
            normalSets[piece.source], names[i], triangulated, untextured);

        ObjImportMeshReport report{};
        report.name = mesh.name;
        report.source = sourceName(source.meshes[piece.source]);
        report.vertices = mesh.vertices.size();
        report.textureVertices = mesh.texVertices.size();
        report.faces = mesh.faces.size();
        report.triangulatedPolygons = triangulated;
        report.radius = mesh.radius;
        report.part = piece.part;
        report.parts = piece.parts;
        result.faces += report.faces;
        result.vertices += report.vertices;
        result.textureVertices += report.textureVertices;
        result.triangulatedPolygons += report.triangulatedPolygons;
        result.untexturedFaces += untextured;
        model.radius = std::max(model.radius, mesh.radius);
        result.meshes.push_back(std::move(report));
        if ( splitOfSource[piece.source] < result.splits.size() )
        {
            result.splits[splitOfSource[piece.source]].meshes.push_back(mesh.name);
        }

        Model3doNode node{};
        node.id = static_cast<std::int32_t>(i);
        node.type = 0;
        node.flags = 0;
        node.mesh = static_cast<std::int32_t>(i);
        // The engine draws a model from node 0 down through child and sibling
        // links (rdModel3_DrawHNode), so a second root would never be drawn.
        // As in stock multi-mesh props, one node is the root and the others
        // (split pieces included) are its children; every transform is the
        // identity.
        node.parent = i == 0 ? -1 : 0;
        node.name = mesh.name;
        model.nodes.push_back(std::move(node));
        model.meshes.push_back(std::move(mesh));
    }
    model.materials = materials.slots;
    for ( std::size_t r = 0; r < options.materials.size(); ++r )
    {
        const ObjImportMaterial& material = options.materials[r];
        const std::int32_t slot = materials.slotOfRequest[r];
        if ( slot < 0 )
        {
            result.unusedMaterials.push_back(material.name);
            continue;
        }
        ObjImportMaterialReport report{};
        report.name = material.name;
        report.mat = materials.slots[static_cast<std::size_t>(slot)];
        report.index = slot;
        report.width = material.width;
        report.height = material.height;
        report.faces = materials.facesOfRequest[r];
        result.materials.push_back(std::move(report));
    }
    return result;
}

} // namespace model_import

ObjImportResult importObjModel(const std::string_view objText, const ObjImportOptions& options)
{
    model_import::validateOptions(options);
    const SourceModel obj = ObjReader{ options.normalPolicy }.read(objText);
    return model_import::buildModel(obj, options);
}

bool isGlbModel(const std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= 4 && bytes[0] == std::byte{ 'g' } && bytes[1] == std::byte{ 'l' }
        && bytes[2] == std::byte{ 'T' } && bytes[3] == std::byte{ 'F' };
}

ObjImportResult importModel(const std::string_view bytes, const ObjImportOptions& options)
{
    const auto raw = std::as_bytes(std::span<const char>(bytes.data(), bytes.size()));
    if ( isGlbModel(raw) )
    {
        return importGlbModel(raw, options);
    }
    return importObjModel(bytes, options);
}

} // namespace sed::assets
