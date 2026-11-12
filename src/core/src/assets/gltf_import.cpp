// Binary glTF 2.0 (.glb) input for model import: the container, its JSON and
// BIN chunks, accessors, the node hierarchy and materials are read here into
// the source model that the OBJ reader also produces; model_import::buildModel
// turns it into a 3DO.

#include "sed/assets/obj_import.hpp"

#include "model_import_detail.hpp"

#include "sed/float_chars.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sed::assets
{
namespace
{

using model_import::inQuotes;
using model_import::refuse;
using model_import::SourceCorner;
using model_import::SourceModel;
using model_import::SourcePolygon;
using model_import::Vec3;

constexpr std::uint32_t kGlbMagic = 0x46546C67u;     // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534Au;    // "JSON"
constexpr std::uint32_t kChunkBin = 0x004E4942u;     // "BIN\0"
constexpr std::size_t kMaxJsonDepth = 64;
constexpr std::size_t kMaxJsonValues = 1u << 21;
constexpr std::size_t kMaxNodeDepth = 128;
constexpr std::size_t kMaxAccessorCount = 1u << 22;
constexpr double kMaxJsonInteger = 9007199254740992.0; // 2^53
// Every triangle becomes one JK face, so more than the largest model holds
// is refused while reading.
constexpr std::size_t kMaxTriangles = kJkModelMaxMeshes * kJkMeshMaxFaces;

constexpr std::uint32_t kByte = 5120;
constexpr std::uint32_t kUnsignedByte = 5121;
constexpr std::uint32_t kShort = 5122;
constexpr std::uint32_t kUnsignedShort = 5123;
constexpr std::uint32_t kUnsignedInt = 5125;
constexpr std::uint32_t kFloat = 5126;

[[noreturn]] void malformed(const std::string& what)
{
    refuse(ObjImportErrorKind::Malformed, "gltfMalformed", "glTF: " + what);
}

[[noreturn]] void unsupported(const std::string& what)
{
    refuse(ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "glTF: " + what);
}

[[noreturn]] void tooLarge(const std::string& what)
{
    refuse(ObjImportErrorKind::Malformed, "gltfInputTooLarge", "glTF: " + what);
}

std::uint32_t readLe32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u)
        | (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

float readF32(const std::uint8_t* p) noexcept
{
    return std::bit_cast<float>(readLe32(p));
}

// ---------------------------------------------------------------------------
// JSON

struct Json
{
    enum class Kind : std::uint8_t
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind{ Kind::Null };
    bool boolean{};
    double number{};
    std::string text{};
    std::vector<Json> items{};
    std::vector<std::pair<std::string, Json>> members{};

    const Json* find(const std::string_view key) const noexcept
    {
        for ( const auto& [name, value] : members )
        {
            if ( name == key )
            {
                return &value;
            }
        }
        return nullptr;
    }
};

// A strict, bounded JSON reader (RFC 8259): nesting, value count and every
// string are limited by the chunk; duplicate object keys are refused.
class JsonParser
{
public:
    explicit JsonParser(const std::string_view text) : text_(text) {}

    Json parse()
    {
        Json root = value(0);
        skipSpace();
        if ( pos_ != text_.size() )
        {
            fail("unexpected text after the JSON value");
        }
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& what) const
    {
        malformed("the JSON chunk is not valid JSON (" + what + " at byte " + std::to_string(pos_) + ")");
    }

    void skipSpace() noexcept
    {
        while ( pos_ < text_.size()
            && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r') )
        {
            ++pos_;
        }
    }

    bool isDigit(const std::size_t at) const noexcept
    {
        return at < text_.size() && text_[at] >= '0' && text_[at] <= '9';
    }

    Json value(const std::size_t depth)
    {
        if ( depth > kMaxJsonDepth )
        {
            fail("nesting deeper than 64 levels");
        }
        if ( ++values_ > kMaxJsonValues )
        {
            tooLarge("the JSON chunk holds more than 2097152 values");
        }
        skipSpace();
        if ( pos_ >= text_.size() )
        {
            fail("unexpected end");
        }
        Json out{};
        const char ch = text_[pos_];
        if ( ch == '{' )
        {
            object(out, depth);
        }
        else if ( ch == '[' )
        {
            array(out, depth);
        }
        else if ( ch == '"' )
        {
            out.kind = Json::Kind::String;
            out.text = string();
        }
        else if ( literal("true") )
        {
            out.kind = Json::Kind::Bool;
            out.boolean = true;
        }
        else if ( literal("false") )
        {
            out.kind = Json::Kind::Bool;
        }
        else if ( literal("null") )
        {
            out.kind = Json::Kind::Null;
        }
        else
        {
            number(out);
        }
        return out;
    }

    bool literal(const std::string_view word) noexcept
    {
        if ( text_.substr(pos_, word.size()) == word )
        {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    void object(Json& out, const std::size_t depth)
    {
        out.kind = Json::Kind::Object;
        ++pos_;
        skipSpace();
        if ( pos_ < text_.size() && text_[pos_] == '}' )
        {
            ++pos_;
            return;
        }
        for ( ;; )
        {
            skipSpace();
            if ( pos_ >= text_.size() || text_[pos_] != '"' )
            {
                fail("expected a member name");
            }
            std::string key = string();
            if ( out.find(key) )
            {
                fail("duplicate member '" + key.substr(0, 32) + "'");
            }
            skipSpace();
            if ( pos_ >= text_.size() || text_[pos_] != ':' )
            {
                fail("expected ':'");
            }
            ++pos_;
            Json member = value(depth + 1);
            out.members.emplace_back(std::move(key), std::move(member));
            skipSpace();
            if ( pos_ < text_.size() && text_[pos_] == ',' )
            {
                ++pos_;
                continue;
            }
            if ( pos_ < text_.size() && text_[pos_] == '}' )
            {
                ++pos_;
                return;
            }
            fail("expected ',' or '}'");
        }
    }

    void array(Json& out, const std::size_t depth)
    {
        out.kind = Json::Kind::Array;
        ++pos_;
        skipSpace();
        if ( pos_ < text_.size() && text_[pos_] == ']' )
        {
            ++pos_;
            return;
        }
        for ( ;; )
        {
            out.items.push_back(value(depth + 1));
            skipSpace();
            if ( pos_ < text_.size() && text_[pos_] == ',' )
            {
                ++pos_;
                continue;
            }
            if ( pos_ < text_.size() && text_[pos_] == ']' )
            {
                ++pos_;
                return;
            }
            fail("expected ',' or ']'");
        }
    }

    std::uint32_t hex4()
    {
        if ( pos_ + 4 > text_.size() )
        {
            fail("truncated \\u escape");
        }
        std::uint32_t code = 0;
        for ( int i = 0; i < 4; ++i )
        {
            const char ch = text_[pos_++];
            code <<= 4u;
            if ( ch >= '0' && ch <= '9' )
            {
                code |= static_cast<std::uint32_t>(ch - '0');
            }
            else if ( ch >= 'a' && ch <= 'f' )
            {
                code |= static_cast<std::uint32_t>(ch - 'a' + 10);
            }
            else if ( ch >= 'A' && ch <= 'F' )
            {
                code |= static_cast<std::uint32_t>(ch - 'A' + 10);
            }
            else
            {
                fail("invalid \\u escape");
            }
        }
        return code;
    }

    static void appendUtf8(std::string& out, const std::uint32_t code)
    {
        if ( code < 0x80u )
        {
            out.push_back(static_cast<char>(code));
        }
        else if ( code < 0x800u )
        {
            out.push_back(static_cast<char>(0xC0u | (code >> 6u)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else if ( code < 0x10000u )
        {
            out.push_back(static_cast<char>(0xE0u | (code >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (code >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((code >> 12u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    std::string string()
    {
        ++pos_; // opening quote
        std::string out{};
        for ( ;; )
        {
            const std::size_t run = pos_;
            while ( pos_ < text_.size() && text_[pos_] != '"' && text_[pos_] != '\\'
                && static_cast<unsigned char>(text_[pos_]) >= 0x20u )
            {
                ++pos_;
            }
            out.append(text_.substr(run, pos_ - run));
            if ( pos_ >= text_.size() )
            {
                fail("unterminated string");
            }
            const char ch = text_[pos_++];
            if ( ch == '"' )
            {
                return out;
            }
            if ( ch != '\\' )
            {
                --pos_;
                fail("control character in a string");
            }
            if ( pos_ >= text_.size() )
            {
                fail("unterminated string");
            }
            const char escape = text_[pos_++];
            switch ( escape )
            {
            case '"':
            case '\\':
            case '/':
                out.push_back(escape);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
            {
                std::uint32_t code = hex4();
                if ( code >= 0xD800u && code <= 0xDBFFu )
                {
                    if ( text_.substr(pos_, 2) != "\\u" )
                    {
                        fail("unpaired surrogate");
                    }
                    pos_ += 2;
                    const std::uint32_t low = hex4();
                    if ( low < 0xDC00u || low > 0xDFFFu )
                    {
                        fail("unpaired surrogate");
                    }
                    code = 0x10000u + ((code - 0xD800u) << 10u) + (low - 0xDC00u);
                }
                else if ( code >= 0xDC00u && code <= 0xDFFFu )
                {
                    fail("unpaired surrogate");
                }
                appendUtf8(out, code);
                break;
            }
            default:
                fail("invalid escape");
            }
        }
    }

    void number(Json& out)
    {
        const std::size_t start = pos_;
        if ( pos_ < text_.size() && text_[pos_] == '-' )
        {
            ++pos_;
        }
        if ( !isDigit(pos_) )
        {
            fail("invalid value");
        }
        if ( text_[pos_] == '0' )
        {
            ++pos_;
        }
        else
        {
            while ( isDigit(pos_) )
            {
                ++pos_;
            }
        }
        if ( pos_ < text_.size() && text_[pos_] == '.' )
        {
            ++pos_;
            if ( !isDigit(pos_) )
            {
                fail("invalid number");
            }
            while ( isDigit(pos_) )
            {
                ++pos_;
            }
        }
        if ( pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E') )
        {
            ++pos_;
            if ( pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-') )
            {
                ++pos_;
            }
            if ( !isDigit(pos_) )
            {
                fail("invalid number");
            }
            while ( isDigit(pos_) )
            {
                ++pos_;
            }
        }
        double value = 0.0;
        const char* const first = text_.data() + start;
        const char* const last = text_.data() + pos_;
        const auto result = sed::text::fromChars(first, last, value, std::chars_format::general);
        if ( result.ec != std::errc{} || result.ptr != last || !std::isfinite(value) )
        {
            fail("number out of range");
        }
        out.kind = Json::Kind::Number;
        out.number = value;
    }

    std::string_view text_;
    std::size_t pos_{};
    std::size_t values_{};
};

// ---------------------------------------------------------------------------
// Typed access to glTF properties

const std::vector<Json>& arrayMember(const Json& object, const std::string_view key, const std::string& where)
{
    static const std::vector<Json> kNone{};
    const Json* value = object.find(key);
    if ( !value )
    {
        return kNone;
    }
    if ( value->kind != Json::Kind::Array )
    {
        malformed(where + " '" + std::string(key) + "' is not an array");
    }
    return value->items;
}

const Json* objectMember(const Json& object, const std::string_view key, const std::string& where)
{
    const Json* value = object.find(key);
    if ( value && value->kind != Json::Kind::Object )
    {
        malformed(where + " '" + std::string(key) + "' is not an object");
    }
    return value;
}

const Json& objectItem(const std::vector<Json>& items, const std::size_t index, const std::string& what)
{
    if ( items[index].kind != Json::Kind::Object )
    {
        malformed(what + " is not an object");
    }
    return items[index];
}

std::optional<std::size_t> optionalInteger(
    const Json& object,
    const std::string_view key,
    const std::string& where,
    const double max = kMaxJsonInteger)
{
    const Json* value = object.find(key);
    if ( !value )
    {
        return std::nullopt;
    }
    if ( value->kind != Json::Kind::Number || value->number < 0.0 || value->number != std::floor(value->number)
        || value->number > max )
    {
        malformed(where + " '" + std::string(key) + "' must be a whole number from 0 to "
            + std::to_string(static_cast<std::uint64_t>(max)));
    }
    return static_cast<std::size_t>(value->number);
}

std::size_t requiredInteger(
    const Json& object,
    const std::string_view key,
    const std::string& where,
    const double max = kMaxJsonInteger)
{
    const auto value = optionalInteger(object, key, where, max);
    if ( !value )
    {
        malformed(where + " has no '" + std::string(key) + "'");
    }
    return *value;
}

// An index into a top-level array of `count` items.
std::optional<std::size_t> optionalIndex(
    const Json& object,
    const std::string_view key,
    const std::size_t count,
    const std::string& where)
{
    const auto value = optionalInteger(object, key, where);
    if ( value && *value >= count )
    {
        malformed(where + " '" + std::string(key) + "' refers to item " + std::to_string(*value) + " of "
            + std::to_string(count));
    }
    return value;
}

std::size_t requiredIndex(
    const Json& object,
    const std::string_view key,
    const std::size_t count,
    const std::string& where)
{
    const auto value = optionalIndex(object, key, count, where);
    if ( !value )
    {
        malformed(where + " has no '" + std::string(key) + "'");
    }
    return *value;
}

std::string optionalString(const Json& object, const std::string_view key, const std::string& where)
{
    const Json* value = object.find(key);
    if ( !value )
    {
        return {};
    }
    if ( value->kind != Json::Kind::String )
    {
        malformed(where + " '" + std::string(key) + "' is not a string");
    }
    return value->text;
}

std::vector<double> numberArray(
    const Json& object,
    const std::string_view key,
    const std::size_t size,
    const std::string& where)
{
    const Json* value = object.find(key);
    if ( !value )
    {
        return {};
    }
    if ( value->kind != Json::Kind::Array || value->items.size() != size )
    {
        malformed(where + " '" + std::string(key) + "' must be an array of " + std::to_string(size) + " numbers");
    }
    std::vector<double> numbers{};
    for ( const Json& item : value->items )
    {
        if ( item.kind != Json::Kind::Number )
        {
            malformed(where + " '" + std::string(key) + "' must be an array of " + std::to_string(size) + " numbers");
        }
        numbers.push_back(item.number);
    }
    return numbers;
}

// ---------------------------------------------------------------------------
// Base64 data: URIs

bool isDataUri(const std::string_view uri) noexcept
{
    return uri.size() >= 5 && (uri.substr(0, 5) == "data:" || uri.substr(0, 5) == "DATA:");
}

int base64Value(const char ch) noexcept
{
    if ( ch >= 'A' && ch <= 'Z' )
    {
        return ch - 'A';
    }
    if ( ch >= 'a' && ch <= 'z' )
    {
        return ch - 'a' + 26;
    }
    if ( ch >= '0' && ch <= '9' )
    {
        return ch - '0' + 52;
    }
    if ( ch == '+' )
    {
        return 62;
    }
    if ( ch == '/' )
    {
        return 63;
    }
    return -1;
}

// Decodes "data:[<type>];base64,<payload>" into bytes, refusing anything
// larger than kGlbImportMaxDataUriBytes before it is decoded.
std::vector<std::uint8_t> decodeDataUri(const std::string_view uri, std::string& mediaType, const std::string& what)
{
    const std::size_t comma = uri.find(',');
    if ( comma == std::string_view::npos )
    {
        malformed(what + " has a data: URI without ','");
    }
    std::string_view header = uri.substr(5, comma - 5);
    constexpr std::string_view kBase64 = ";base64";
    if ( header.size() < kBase64.size() || header.substr(header.size() - kBase64.size()) != kBase64 )
    {
        unsupported(what + " uses a data: URI that is not base64");
    }
    header.remove_suffix(kBase64.size());
    mediaType = std::string(header);
    std::string_view payload = uri.substr(comma + 1);
    while ( !payload.empty() && payload.back() == '=' )
    {
        payload.remove_suffix(1);
    }
    const std::size_t decodedSize = payload.size() / 4u * 3u + (payload.size() % 4u == 0 ? 0u : payload.size() % 4u - 1u);
    if ( decodedSize > kGlbImportMaxDataUriBytes )
    {
        tooLarge(what + " is a data: URI of " + std::to_string(decodedSize)
            + " bytes; at most 4194304 are read that way - keep it in the GLB's binary chunk");
    }
    if ( payload.size() % 4u == 1u )
    {
        malformed(what + " has a data: URI with invalid base64");
    }
    std::vector<std::uint8_t> bytes{};
    bytes.reserve(decodedSize);
    std::uint32_t buffer = 0;
    int bits = 0;
    for ( const char ch : payload )
    {
        const int value = base64Value(ch);
        if ( value < 0 )
        {
            malformed(what + " has a data: URI with invalid base64");
        }
        buffer = (buffer << 6u) | static_cast<std::uint32_t>(value);
        bits += 6;
        if ( bits >= 8 )
        {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((buffer >> static_cast<unsigned>(bits)) & 0xFFu));
        }
    }
    return bytes;
}

bool isJpegBytes(const std::vector<std::uint8_t>& bytes) noexcept
{
    return bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

bool isPngBytes(const std::vector<std::uint8_t>& bytes) noexcept
{
    static constexpr std::array<std::uint8_t, 8> kSignature{ 137, 80, 78, 71, 13, 10, 26, 10 };
    return bytes.size() >= kSignature.size() && std::equal(kSignature.begin(), kSignature.end(), bytes.begin());
}

// ---------------------------------------------------------------------------
// Transforms

// An affine transform as a row-major 3x4 matrix (the last row is 0 0 0 1).
struct Affine
{
    std::array<std::array<double, 4>, 3> m{ { { 1.0, 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0, 0.0 } } };

    Affine operator*(const Affine& b) const noexcept
    {
        Affine out{};
        for ( std::size_t r = 0; r < 3; ++r )
        {
            for ( std::size_t c = 0; c < 4; ++c )
            {
                double sum = c == 3 ? m[r][3] : 0.0;
                for ( std::size_t k = 0; k < 3; ++k )
                {
                    sum += m[r][k] * b.m[k][c];
                }
                out.m[r][c] = sum;
            }
        }
        return out;
    }

    Vec3 point(const Vec3& p) const noexcept
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3] };
    }

    double determinant() const noexcept
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    double columnLength(const std::size_t c) const noexcept
    {
        return std::sqrt(m[0][c] * m[0][c] + m[1][c] * m[1][c] + m[2][c] * m[2][c]);
    }
};

// The inverse transpose of a transform's linear part, which carries normals.
struct NormalMatrix
{
    std::array<std::array<double, 3>, 3> m{};

    explicit NormalMatrix(const Affine& a) noexcept
    {
        const auto& t = a.m;
        const double det = a.determinant();
        // cofactor(t) / det == inverse(t) transposed
        m[0][0] = (t[1][1] * t[2][2] - t[1][2] * t[2][1]) / det;
        m[0][1] = -(t[1][0] * t[2][2] - t[1][2] * t[2][0]) / det;
        m[0][2] = (t[1][0] * t[2][1] - t[1][1] * t[2][0]) / det;
        m[1][0] = -(t[0][1] * t[2][2] - t[0][2] * t[2][1]) / det;
        m[1][1] = (t[0][0] * t[2][2] - t[0][2] * t[2][0]) / det;
        m[1][2] = -(t[0][0] * t[2][1] - t[0][1] * t[2][0]) / det;
        m[2][0] = (t[0][1] * t[1][2] - t[0][2] * t[1][1]) / det;
        m[2][1] = -(t[0][0] * t[1][2] - t[0][2] * t[1][0]) / det;
        m[2][2] = (t[0][0] * t[1][1] - t[0][1] * t[1][0]) / det;
    }

    Vec3 apply(const Vec3& n) const noexcept
    {
        return { m[0][0] * n.x + m[0][1] * n.y + m[0][2] * n.z, m[1][0] * n.x + m[1][1] * n.y + m[1][2] * n.z,
            m[2][0] * n.x + m[2][1] * n.y + m[2][2] * n.z };
    }
};

// ---------------------------------------------------------------------------
// The reader

struct BufferView
{
    const std::uint8_t* data{};
    std::size_t length{};
    std::size_t stride{}; // 0: tightly packed
};

struct Accessor
{
    const std::uint8_t* data{};
    std::size_t count{};
    std::size_t stride{};
    std::uint32_t componentType{};
};

struct PositionKeyHash
{
    std::size_t operator()(const std::array<std::uint32_t, 3>& key) const noexcept
    {
        std::uint64_t h = 1469598103934665603ull;
        for ( const std::uint32_t value : key )
        {
            h = (h ^ value) * 1099511628211ull;
        }
        return static_cast<std::size_t>(h);
    }
};

std::size_t componentSize(const std::uint32_t type) noexcept
{
    switch ( type )
    {
    case kByte:
    case kUnsignedByte:
        return 1;
    case kShort:
    case kUnsignedShort:
        return 2;
    case kUnsignedInt:
    case kFloat:
        return 4;
    default:
        return 0;
    }
}

std::size_t componentCount(const std::string_view type) noexcept
{
    if ( type == "SCALAR" )
    {
        return 1;
    }
    if ( type == "VEC2" )
    {
        return 2;
    }
    if ( type == "VEC3" )
    {
        return 3;
    }
    if ( type == "VEC4" || type == "MAT2" )
    {
        return 4;
    }
    if ( type == "MAT3" )
    {
        return 9;
    }
    if ( type == "MAT4" )
    {
        return 16;
    }
    return 0;
}

class GlbReader
{
public:
    GlbReader(const std::span<const std::byte> bytes, GltfImportReport& report)
        : bytes_(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), report_(report)
    {
    }

    SourceModel read()
    {
        readContainer();
        model_.format = ModelSourceFormat::Glb;
        model_.textureOriginTop = true;
        model_.dropDegenerate = true;

        if ( root_.kind != Json::Kind::Object )
        {
            malformed("the JSON chunk is not an object");
        }
        checkAsset();
        accessors_ = &arrayMember(root_, "accessors", "the glTF");
        bufferViews_ = &arrayMember(root_, "bufferViews", "the glTF");
        buffers_ = &arrayMember(root_, "buffers", "the glTF");
        meshes_ = &arrayMember(root_, "meshes", "the glTF");
        nodes_ = &arrayMember(root_, "nodes", "the glTF");
        materials_ = &arrayMember(root_, "materials", "the glTF");
        textures_ = &arrayMember(root_, "textures", "the glTF");
        images_ = &arrayMember(root_, "images", "the glTF");
        bufferData_.assign(buffers_->size(), {});
        bufferRead_.assign(buffers_->size(), false);
        materialName_.assign(materials_->size(), -1);
        materialUsed_.assign(materials_->size(), false);
        materialTextured_.assign(materials_->size(), false);
        if ( !arrayMember(root_, "animations", "the glTF").empty() )
        {
            ignore("animations");
        }
        if ( !arrayMember(root_, "skins", "the glTF").empty() )
        {
            ignore("skins");
        }

        walkScene();
        report_.uniquePositions = model_.vertices.size();
        checkMaterials();
        return std::move(model_);
    }

private:
    // -- container ----------------------------------------------------------

    void readContainer()
    {
        if ( bytes_.size() > kGlbImportMaxSourceBytes )
        {
            tooLarge("the GLB is larger than 64 MiB");
        }
        if ( bytes_.size() < 12 )
        {
            malformed("the file is shorter than a GLB header");
        }
        if ( readLe32(bytes_.data()) != kGlbMagic )
        {
            malformed("the file does not start with the binary glTF magic 'glTF'");
        }
        const std::uint32_t version = readLe32(bytes_.data() + 4);
        if ( version != 2 )
        {
            unsupported("GLB container version " + std::to_string(version) + " is not supported; export glTF 2.0");
        }
        const std::uint32_t declared = readLe32(bytes_.data() + 8);
        if ( declared != bytes_.size() )
        {
            malformed("the GLB header gives a length of " + std::to_string(declared) + " bytes but the file has "
                + std::to_string(bytes_.size()));
        }
        std::size_t offset = 12;
        bool haveJson = false;
        std::span<const std::uint8_t> json{};
        while ( offset < bytes_.size() )
        {
            if ( bytes_.size() - offset < 8 )
            {
                malformed("a chunk header is cut short at byte " + std::to_string(offset));
            }
            const std::size_t chunkLength = readLe32(bytes_.data() + offset);
            const std::uint32_t chunkType = readLe32(bytes_.data() + offset + 4);
            if ( chunkLength > bytes_.size() - offset - 8 )
            {
                malformed("the chunk at byte " + std::to_string(offset) + " runs past the end of the file");
            }
            const std::span<const std::uint8_t> data = bytes_.subspan(offset + 8, chunkLength);
            if ( !haveJson )
            {
                if ( chunkType != kChunkJson )
                {
                    malformed("the first chunk is not the JSON chunk");
                }
                json = data;
                haveJson = true;
            }
            else if ( chunkType == kChunkJson )
            {
                malformed("the GLB has a second JSON chunk");
            }
            else if ( chunkType == kChunkBin && !haveBin_ )
            {
                bin_ = data;
                haveBin_ = true;
            }
            offset += 8 + chunkLength;
        }
        if ( !haveJson )
        {
            malformed("the GLB has no JSON chunk");
        }
        if ( json.size() > kGlbImportMaxJsonBytes )
        {
            tooLarge("the JSON chunk is larger than 8 MiB");
        }
        std::string_view text(reinterpret_cast<const char*>(json.data()), json.size());
        if ( text.substr(0, 3) == "\xEF\xBB\xBF" )
        {
            text.remove_prefix(3);
        }
        while ( !text.empty() && (text.back() == ' ' || text.back() == '\0') )
        {
            text.remove_suffix(1); // chunk padding
        }
        root_ = JsonParser(text).parse();
    }

    void checkAsset()
    {
        const Json* asset = objectMember(root_, "asset", "the glTF");
        if ( !asset )
        {
            malformed("the glTF has no 'asset'");
        }
        const std::string version = optionalString(*asset, "version", "asset");
        if ( version.substr(0, 2) != "2." )
        {
            unsupported("glTF version '" + version.substr(0, 16) + "' is not supported; export glTF 2.0");
        }
        const std::string minVersion = optionalString(*asset, "minVersion", "asset");
        if ( !minVersion.empty() && minVersion != "2.0" )
        {
            unsupported("the glTF needs a reader of version " + minVersion.substr(0, 16) + "; RED reads glTF 2.0");
        }
        const std::vector<Json>& required = arrayMember(root_, "extensionsRequired", "the glTF");
        if ( !required.empty() )
        {
            std::string names{};
            for ( const Json& name : required )
            {
                names += (names.empty() ? "" : ", ") + (name.kind == Json::Kind::String ? name.text.substr(0, 64) : "?");
            }
            unsupported("the glTF requires the extension(s) " + names
                + ", which RED does not read; export without them (no Draco or meshopt compression, mesh "
                  "quantization or other required extensions)");
        }
        for ( const Json& name : arrayMember(root_, "extensionsUsed", "the glTF") )
        {
            if ( name.kind == Json::Kind::String )
            {
                ignore("extension " + name.text.substr(0, 64));
            }
        }
    }

    void ignore(const std::string& what)
    {
        for ( auto& [name, count] : report_.ignored )
        {
            if ( name == what )
            {
                ++count;
                return;
            }
        }
        if ( report_.ignored.size() < 64 )
        {
            report_.ignored.emplace_back(what, 1u);
        }
    }

    // -- buffers and accessors ------------------------------------------------

    std::span<const std::uint8_t> buffer(const std::size_t index)
    {
        const std::string where = "buffer " + std::to_string(index);
        const Json& buffer = objectItem(*buffers_, index, where);
        const std::size_t byteLength = requiredInteger(buffer, "byteLength", where);
        if ( byteLength == 0 )
        {
            malformed(where + " is empty");
        }
        const Json* uri = buffer.find("uri");
        if ( !uri )
        {
            if ( index != 0 || !haveBin_ )
            {
                malformed(where + " has no uri and is not the GLB's binary chunk");
            }
            if ( byteLength > bin_.size() )
            {
                malformed(where + " is " + std::to_string(byteLength) + " bytes but the binary chunk holds "
                    + std::to_string(bin_.size()));
            }
            return bin_.first(byteLength);
        }
        if ( uri->kind != Json::Kind::String )
        {
            malformed(where + " uri is not a string");
        }
        if ( !isDataUri(uri->text) )
        {
            unsupported(where + " is an external file " + inQuotes(uri->text)
                + "; export a self-contained .glb with its buffers embedded");
        }
        if ( !bufferRead_[index] )
        {
            std::string mediaType{};
            bufferData_[index] = decodeDataUri(uri->text, mediaType, where);
            bufferRead_[index] = true;
        }
        if ( byteLength > bufferData_[index].size() )
        {
            malformed(where + " is " + std::to_string(byteLength) + " bytes but its data: URI holds "
                + std::to_string(bufferData_[index].size()));
        }
        return std::span<const std::uint8_t>(bufferData_[index]).first(byteLength);
    }

    BufferView bufferView(const std::size_t index)
    {
        const std::string where = "buffer view " + std::to_string(index);
        const Json& view = objectItem(*bufferViews_, index, where);
        if ( const Json* extensions = objectMember(view, "extensions", where) )
        {
            if ( extensions->find("EXT_meshopt_compression") || extensions->find("KHR_meshopt_compression") )
            {
                unsupported(where + " is meshopt-compressed; export without mesh compression");
            }
        }
        const std::size_t bufferIndex = requiredIndex(view, "buffer", buffers_->size(), where);
        const std::size_t offset = optionalInteger(view, "byteOffset", where).value_or(0);
        const std::size_t length = requiredInteger(view, "byteLength", where);
        const std::size_t stride = optionalInteger(view, "byteStride", where).value_or(0);
        if ( length == 0 )
        {
            malformed(where + " is empty");
        }
        if ( view.find("byteStride") && (stride < 4 || stride > 252 || stride % 4 != 0) )
        {
            malformed(where + " byteStride must be a multiple of 4 from 4 to 252");
        }
        const std::span<const std::uint8_t> data = buffer(bufferIndex);
        if ( offset > data.size() || length > data.size() - offset )
        {
            malformed(where + " reaches past the end of buffer " + std::to_string(bufferIndex));
        }
        return { data.data() + offset, length, stride };
    }

    // An accessor of the given shape, bounds-checked against its view.
    Accessor accessor(
        const std::size_t index,
        const std::string& role,
        const std::string_view wantedType,
        const std::vector<std::uint32_t>& wantedComponents)
    {
        const std::string where = role + " accessor " + std::to_string(index);
        const Json& accessor = objectItem(*accessors_, index, where);
        if ( accessor.find("sparse") )
        {
            unsupported(where + " is sparse; sparse accessors are not supported - export without sparse storage");
        }
        const std::uint32_t componentType = static_cast<std::uint32_t>(requiredInteger(accessor, "componentType", where));
        const std::string type = optionalString(accessor, "type", where);
        const std::size_t count = requiredInteger(accessor, "count", where);
        if ( componentSize(componentType) == 0 || componentCount(type) == 0 )
        {
            malformed(where + " has an invalid componentType or type");
        }
        if ( type != wantedType )
        {
            malformed(where + " is " + type + " but " + role + " must be " + std::string(wantedType));
        }
        if ( std::find(wantedComponents.begin(), wantedComponents.end(), componentType) == wantedComponents.end() )
        {
            if ( role == "indices" )
            {
                malformed(where + " must hold unsigned 8-, 16- or 32-bit integers");
            }
            unsupported(where + " holds " + (componentType == kFloat ? std::string("floats") : "integers")
                + "; " + role + " must be 32-bit float (quantized or normalized integer data is not supported)");
        }
        if ( const Json* normalized = accessor.find("normalized");
            normalized && (normalized->kind != Json::Kind::Bool || (normalized->boolean && componentType == kFloat)) )
        {
            malformed(where + " 'normalized' is invalid");
        }
        if ( count == 0 || count > kMaxAccessorCount )
        {
            tooLarge(where + " count must be 1 to 4194304");
        }
        const std::optional<std::size_t> viewIndex = optionalIndex(accessor, "bufferView", bufferViews_->size(), where);
        if ( !viewIndex )
        {
            unsupported(where + " has no buffer view (all zeros); export the data itself");
        }
        const std::size_t offset = optionalInteger(accessor, "byteOffset", where).value_or(0);
        const BufferView view = bufferView(*viewIndex);
        const std::size_t elementSize = componentSize(componentType) * componentCount(type);
        const std::size_t stride = view.stride != 0 ? view.stride : elementSize;
        if ( stride < elementSize )
        {
            malformed(where + " elements are larger than the buffer view's byteStride");
        }
        if ( offset > view.length || (count - 1) * stride + elementSize > view.length - offset )
        {
            malformed(where + " reaches past the end of buffer view " + std::to_string(*viewIndex));
        }
        return { view.data + offset, count, stride, componentType };
    }

    std::size_t indexValue(const Accessor& indices, const std::size_t i) const noexcept
    {
        const std::uint8_t* p = indices.data + i * indices.stride;
        switch ( indices.componentType )
        {
        case kUnsignedByte:
            return p[0];
        case kUnsignedShort:
            return static_cast<std::size_t>(p[0]) | (static_cast<std::size_t>(p[1]) << 8u);
        default:
            return readLe32(p);
        }
    }

    // -- scene ----------------------------------------------------------------

    void walkScene()
    {
        const std::vector<Json>& scenes = arrayMember(root_, "scenes", "the glTF");
        std::vector<std::size_t> roots{};
        const auto sceneRoots = [&](const std::size_t index)
        {
            const std::string where = "scene " + std::to_string(index);
            const Json& scene = objectItem(scenes, index, where);
            for ( const Json& node : arrayMember(scene, "nodes", where) )
            {
                if ( node.kind != Json::Kind::Number || node.number < 0 || node.number != std::floor(node.number)
                    || node.number >= static_cast<double>(nodes_->size()) )
                {
                    malformed(where + " lists a node that does not exist");
                }
                roots.push_back(static_cast<std::size_t>(node.number));
            }
        };
        if ( const auto scene = optionalIndex(root_, "scene", scenes.size(), "the glTF") )
        {
            sceneRoots(*scene);
        }
        else if ( !scenes.empty() )
        {
            sceneRoots(0);
        }
        else
        {
            // No scene: every node that is nobody's child is a root.
            std::vector<bool> isChild(nodes_->size(), false);
            for ( std::size_t n = 0; n < nodes_->size(); ++n )
            {
                for ( const std::size_t child : children(n) )
                {
                    isChild[child] = true;
                }
            }
            for ( std::size_t n = 0; n < nodes_->size(); ++n )
            {
                if ( !isChild[n] )
                {
                    roots.push_back(n);
                }
            }
        }

        struct Visit
        {
            std::size_t node{};
            Affine parent{};
            std::size_t depth{};
        };
        std::vector<bool> visited(nodes_->size(), false);
        std::vector<Visit> stack{};
        for ( auto root = roots.rbegin(); root != roots.rend(); ++root )
        {
            stack.push_back({ *root, Affine{}, 0 });
        }
        while ( !stack.empty() )
        {
            const Visit visit = stack.back();
            stack.pop_back();
            if ( visited[visit.node] )
            {
                malformed("node " + std::to_string(visit.node)
                    + " is reached twice (a node has at most one parent and the hierarchy has no cycles)");
            }
            visited[visit.node] = true;
            if ( visit.depth > kMaxNodeDepth )
            {
                malformed("the node hierarchy is deeper than 128 levels");
            }
            const Affine world = visit.parent * localTransform(visit.node);
            const std::string where = "node " + std::to_string(visit.node);
            const Json& node = objectItem(*nodes_, visit.node, where);
            if ( node.find("skin") )
            {
                ignore("skins");
            }
            if ( const auto mesh = optionalIndex(node, "mesh", meshes_->size(), where) )
            {
                instance(visit.node, *mesh, world);
            }
            const std::vector<std::size_t> kids = children(visit.node);
            for ( auto child = kids.rbegin(); child != kids.rend(); ++child )
            {
                stack.push_back({ *child, world, visit.depth + 1 });
            }
        }
    }

    std::vector<std::size_t> children(const std::size_t index) const
    {
        const std::string where = "node " + std::to_string(index);
        const Json& node = objectItem(*nodes_, index, where);
        std::vector<std::size_t> out{};
        for ( const Json& child : arrayMember(node, "children", where) )
        {
            if ( child.kind != Json::Kind::Number || child.number < 0 || child.number != std::floor(child.number)
                || child.number >= static_cast<double>(nodes_->size()) )
            {
                malformed(where + " lists a child that does not exist");
            }
            out.push_back(static_cast<std::size_t>(child.number));
        }
        return out;
    }

    Affine localTransform(const std::size_t index) const
    {
        const std::string where = "node " + std::to_string(index);
        const Json& node = objectItem(*nodes_, index, where);
        const std::vector<double> matrix = numberArray(node, "matrix", 16, where);
        const std::vector<double> translation = numberArray(node, "translation", 3, where);
        const std::vector<double> rotation = numberArray(node, "rotation", 4, where);
        const std::vector<double> scale = numberArray(node, "scale", 3, where);
        Affine out{};
        if ( !matrix.empty() )
        {
            if ( !translation.empty() || !rotation.empty() || !scale.empty() )
            {
                malformed(where + " has both a matrix and translation/rotation/scale");
            }
            // glTF matrices are column-major.
            for ( std::size_t c = 0; c < 4; ++c )
            {
                for ( std::size_t r = 0; r < 3; ++r )
                {
                    out.m[r][c] = matrix[c * 4 + r];
                }
            }
            if ( matrix[3] != 0.0 || matrix[7] != 0.0 || matrix[11] != 0.0 || matrix[15] != 1.0 )
            {
                unsupported(where + " has a projective matrix; node matrices must be affine");
            }
            return out;
        }
        double tx = 0.0;
        double ty = 0.0;
        double tz = 0.0;
        if ( !translation.empty() )
        {
            tx = translation[0];
            ty = translation[1];
            tz = translation[2];
        }
        double qx = 0.0;
        double qy = 0.0;
        double qz = 0.0;
        double qw = 1.0;
        if ( !rotation.empty() )
        {
            const double size = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1]
                + rotation[2] * rotation[2] + rotation[3] * rotation[3]);
            if ( !(size > 0.0) )
            {
                malformed(where + " rotation is not a unit quaternion");
            }
            qx = rotation[0] / size;
            qy = rotation[1] / size;
            qz = rotation[2] / size;
            qw = rotation[3] / size;
        }
        double sx = 1.0;
        double sy = 1.0;
        double sz = 1.0;
        if ( !scale.empty() )
        {
            sx = scale[0];
            sy = scale[1];
            sz = scale[2];
        }
        const std::array<std::array<double, 3>, 3> r{ {
            { 1.0 - 2.0 * (qy * qy + qz * qz), 2.0 * (qx * qy - qw * qz), 2.0 * (qx * qz + qw * qy) },
            { 2.0 * (qx * qy + qw * qz), 1.0 - 2.0 * (qx * qx + qz * qz), 2.0 * (qy * qz - qw * qx) },
            { 2.0 * (qx * qz - qw * qy), 2.0 * (qy * qz + qw * qx), 1.0 - 2.0 * (qx * qx + qy * qy) },
        } };
        const std::array<double, 3> s{ sx, sy, sz };
        const std::array<double, 3> t{ tx, ty, tz };
        for ( std::size_t row = 0; row < 3; ++row )
        {
            for ( std::size_t c = 0; c < 3; ++c )
            {
                out.m[row][c] = r[row][c] * s[c];
            }
            out.m[row][3] = t[row];
        }
        return out;
    }

    // -- meshes ---------------------------------------------------------------

    std::string nodeLabel(const std::size_t index) const
    {
        const Json& node = (*nodes_)[index];
        const Json* name = node.find("name");
        if ( name && name->kind == Json::Kind::String && !name->text.empty() )
        {
            return "node " + inQuotes(name->text);
        }
        return "node " + std::to_string(index);
    }

    void instance(const std::size_t nodeIndex, const std::size_t meshIndex, const Affine& world)
    {
        const std::string where = "mesh " + std::to_string(meshIndex);
        const Json& mesh = objectItem(*meshes_, meshIndex, where);
        const Json& node = (*nodes_)[nodeIndex];
        const double det = world.determinant();
        const double scale = world.columnLength(0) * world.columnLength(1) * world.columnLength(2);
        bool finite = std::isfinite(det) && std::isfinite(scale);
        for ( const auto& row : world.m )
        {
            for ( const double value : row )
            {
                finite = finite && std::isfinite(value);
            }
        }
        if ( !finite || !(std::fabs(det) > 1e-12 * scale) )
        {
            unsupported(nodeLabel(nodeIndex) + " scales its mesh to nothing (its transform is singular or not finite)");
        }
        const bool mirrored = det < 0.0;
        if ( mirrored )
        {
            ++report_.mirroredNodes;
        }
        ++report_.nodes;
        const NormalMatrix normals(world);

        std::string name = optionalString(node, "name", "node " + std::to_string(nodeIndex));
        if ( name.empty() )
        {
            name = optionalString(mesh, "name", where);
        }
        const std::size_t sourceMesh = model_.meshes.size();
        model_.meshes.push_back({ name, {} });
        std::unordered_map<std::array<std::uint32_t, 3>, std::size_t, PositionKeyHash> positions{};

        const std::vector<Json>& primitives = arrayMember(mesh, "primitives", where);
        if ( primitives.empty() )
        {
            malformed(where + " has no primitives");
        }
        for ( std::size_t p = 0; p < primitives.size(); ++p )
        {
            primitive(nodeIndex, meshIndex, p, objectItem(primitives, p, where + " primitive " + std::to_string(p)),
                world, normals, mirrored, sourceMesh, positions);
        }
    }

    std::int64_t materialIndex(const std::size_t index, const bool textured)
    {
        if ( materialName_[index] < 0 )
        {
            const std::string where = "material " + std::to_string(index);
            const Json& material = objectItem(*materials_, index, where);
            std::string name = optionalString(material, "name", where);
            if ( name.empty() )
            {
                name = "#" + std::to_string(index);
            }
            const auto found = std::find(model_.materialNames.begin(), model_.materialNames.end(), name);
            materialName_[index] = static_cast<std::int64_t>(found - model_.materialNames.begin());
            if ( found == model_.materialNames.end() )
            {
                model_.materialNames.push_back(std::move(name));
            }
        }
        materialUsed_[index] = true;
        materialTextured_[index] = materialTextured_[index] || textured;
        return materialName_[index];
    }

    void primitive(
        const std::size_t nodeIndex,
        const std::size_t meshIndex,
        const std::size_t primitiveIndex,
        const Json& object,
        const Affine& world,
        const NormalMatrix& normalMatrix,
        const bool mirrored,
        const std::size_t sourceMesh,
        std::unordered_map<std::array<std::uint32_t, 3>, std::size_t, PositionKeyHash>& positions)
    {
        const std::string where = "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primitiveIndex);
        if ( const Json* extensions = objectMember(object, "extensions", where) )
        {
            if ( extensions->find("KHR_draco_mesh_compression") )
            {
                unsupported(where + " is Draco-compressed (KHR_draco_mesh_compression); export without mesh "
                    "compression");
            }
        }
        const std::size_t mode = optionalInteger(object, "mode", where).value_or(4);
        if ( mode <= 3 )
        {
            unsupported(where + " draws " + (mode == 0 ? "points" : "lines") + " (mode " + std::to_string(mode)
                + "); only triangles become JK faces");
        }
        if ( mode > 6 )
        {
            malformed(where + " has an invalid mode " + std::to_string(mode));
        }
        const Json* attributes = objectMember(object, "attributes", where);
        if ( !attributes )
        {
            malformed(where + " has no attributes");
        }
        std::optional<Accessor> position{};
        std::optional<Accessor> normal{};
        std::optional<Accessor> uv{};
        for ( const auto& [name, value] : attributes->members )
        {
            if ( value.kind != Json::Kind::Number || value.number < 0 || value.number != std::floor(value.number)
                || value.number >= static_cast<double>(accessors_->size()) )
            {
                malformed(where + " attribute " + inQuotes(name) + " does not name an accessor");
            }
            const auto index = static_cast<std::size_t>(value.number);
            if ( name == "POSITION" )
            {
                position = accessor(index, "POSITION", "VEC3", { kFloat });
            }
            else if ( name == "NORMAL" )
            {
                normal = accessor(index, "NORMAL", "VEC3", { kFloat });
            }
            else if ( name == "TEXCOORD_0" )
            {
                uv = accessor(index, "TEXCOORD_0", "VEC2", { kFloat });
            }
            else
            {
                ignore("attribute " + name.substr(0, 32));
            }
        }
        if ( !position )
        {
            malformed(where + " has no POSITION attribute");
        }
        if ( (normal && normal->count != position->count) || (uv && uv->count != position->count) )
        {
            malformed(where + " attributes have different counts");
        }
        if ( !arrayMember(object, "targets", where).empty() )
        {
            ignore("morph targets");
        }
        std::optional<Accessor> indices{};
        if ( const auto index = optionalIndex(object, "indices", accessors_->size(), where) )
        {
            indices = accessor(*index, "indices", "SCALAR", { kUnsignedByte, kUnsignedShort, kUnsignedInt });
        }
        std::int64_t material = -1;
        if ( const auto index = optionalIndex(object, "material", materials_->size(), where) )
        {
            material = materialIndex(*index, uv.has_value());
        }
        ++report_.primitives;
        report_.sourceVertices += position->count;

        const std::size_t place = model_.places.size();
        model_.places.push_back(nodeLabel(nodeIndex) + " primitive " + std::to_string(primitiveIndex));
        const std::size_t vertexCount = position->count;
        std::vector<std::int64_t> vertexOf(vertexCount, -1);
        std::vector<std::int64_t> normalOf(vertexCount, -1);
        std::vector<std::int64_t> textureOf(vertexCount, -1);

        const auto vertexAt = [&](const std::size_t v) -> std::size_t
        {
            if ( vertexOf[v] < 0 )
            {
                const std::uint8_t* p = position->data + v * position->stride;
                const float x = readF32(p) + 0.0f;
                const float y = readF32(p + 4) + 0.0f;
                const float z = readF32(p + 8) + 0.0f;
                if ( !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) )
                {
                    malformed(where + " POSITION holds a value that is not finite");
                }
                const std::array<std::uint32_t, 3> key{ std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y),
                    std::bit_cast<std::uint32_t>(z) };
                const auto [found, inserted] = positions.try_emplace(key, model_.vertices.size());
                if ( inserted )
                {
                    const Vec3 placed = world.point({ x, y, z });
                    if ( !model_import::isFinite(placed) )
                    {
                        unsupported(nodeLabel(nodeIndex) + " moves a vertex beyond the range of a number");
                    }
                    model_.vertices.push_back(placed);
                }
                vertexOf[v] = static_cast<std::int64_t>(found->second);
            }
            return static_cast<std::size_t>(vertexOf[v]);
        };
        const auto normalAt = [&](const std::size_t v) -> std::int64_t
        {
            if ( !normal )
            {
                return -1;
            }
            if ( normalOf[v] == -1 )
            {
                const std::uint8_t* p = normal->data + v * normal->stride;
                const Vec3 n{ readF32(p), readF32(p + 4), readF32(p + 8) };
                if ( !model_import::isFinite(n) )
                {
                    malformed(where + " NORMAL holds a value that is not finite");
                }
                const Vec3 turned = normalMatrix.apply(n);
                if ( !model_import::isFinite(turned) )
                {
                    unsupported(where + " transforms a NORMAL beyond the range of a number");
                }
                // An inverse transpose can make a valid direction arbitrarily
                // small or large. Scale first to avoid a world-unit cutoff,
                // squared-length underflow, or squared-length overflow.
                const double magnitude = std::max({ std::fabs(turned.x), std::fabs(turned.y), std::fabs(turned.z) });
                if ( magnitude > 0.0 )
                {
                    const Vec3 scaled{ turned.x / magnitude, turned.y / magnitude, turned.z / magnitude };
                    normalOf[v] = static_cast<std::int64_t>(model_.normals.size());
                    model_.normals.push_back(scaled * (1.0 / model_import::length(scaled)));
                }
                else
                {
                    normalOf[v] = -2; // a zero normal: the face normals decide
                }
            }
            return normalOf[v] >= 0 ? normalOf[v] : -1;
        };
        const auto textureAt = [&](const std::size_t v) -> std::int64_t
        {
            if ( textureOf[v] < 0 )
            {
                const std::uint8_t* p = uv->data + v * uv->stride;
                const double u = readF32(p);
                const double t = readF32(p + 4);
                if ( !std::isfinite(u) || !std::isfinite(t) )
                {
                    malformed(where + " TEXCOORD_0 holds a value that is not finite");
                }
                textureOf[v] = static_cast<std::int64_t>(model_.textureCoordinates.size());
                model_.textureCoordinates.push_back({ u, t });
            }
            return textureOf[v];
        };

        const std::size_t elements = indices ? indices->count : vertexCount;
        if ( mode == 4 && elements % 3 != 0 )
        {
            malformed(where + " lists " + std::to_string(elements) + " triangle corners, which is not a multiple of 3");
        }
        const std::size_t triangles = mode == 4 ? elements / 3 : (elements >= 3 ? elements - 2 : 0);
        const bool textured = uv.has_value() && material >= 0;
        for ( std::size_t t = 0; t < triangles; ++t )
        {
            std::array<std::size_t, 3> slot{};
            if ( mode == 4 )
            {
                slot = { 3 * t, 3 * t + 1, 3 * t + 2 };
            }
            else if ( mode == 5 )
            {
                slot = t % 2 == 0 ? std::array<std::size_t, 3>{ t, t + 1, t + 2 }
                                  : std::array<std::size_t, 3>{ t, t + 2, t + 1 };
            }
            else
            {
                slot = { t + 1, t + 2, 0 };
            }
            std::array<std::size_t, 3> corner{};
            for ( std::size_t k = 0; k < 3; ++k )
            {
                corner[k] = indices ? indexValue(*indices, slot[k]) : slot[k];
                if ( corner[k] >= vertexCount )
                {
                    malformed(where + " index " + std::to_string(corner[k]) + " is past its "
                        + std::to_string(vertexCount) + " vertices");
                }
            }
            if ( mirrored )
            {
                std::swap(corner[1], corner[2]);
            }
            ++report_.sourceTriangles;
            const std::size_t a = vertexAt(corner[0]);
            const std::size_t b = vertexAt(corner[1]);
            const std::size_t c = vertexAt(corner[2]);
            if ( a == b || b == c || a == c )
            {
                ++model_.droppedDegenerate;
                continue;
            }
            if ( model_.polygons.size() >= kMaxTriangles )
            {
                refuse(ObjImportErrorKind::Refused, "objLimitExceeded",
                    "glTF: the model has more than " + std::to_string(kMaxTriangles)
                        + " triangles; a JK model holds at most 64 meshes of 512 faces - reduce the model first");
            }
            SourcePolygon polygon{};
            polygon.line = t + 1;
            polygon.place = place;
            polygon.mesh = sourceMesh;
            polygon.material = material;
            polygon.firstCorner = model_.corners.size();
            polygon.cornerCount = 3;
            polygon.textured = uv.has_value();
            for ( const std::size_t v : corner )
            {
                SourceCorner out{};
                out.vertex = static_cast<std::int64_t>(vertexAt(v));
                out.normal = normalAt(v);
                out.texture = textured ? textureAt(v) : -1;
                model_.corners.push_back(out);
            }
            model_.polygons.push_back(polygon);
        }
    }

    // -- materials ------------------------------------------------------------

    std::vector<std::uint8_t> imageBytes(const std::size_t index, std::string& problem)
    {
        const std::string where = "image " + std::to_string(index);
        const Json& image = objectItem(*images_, index, where);
        std::vector<std::uint8_t> bytes{};
        if ( const auto view = optionalIndex(image, "bufferView", bufferViews_->size(), where) )
        {
            const BufferView data = bufferView(*view);
            bytes.assign(data.data, data.data + data.length);
        }
        else
        {
            const std::string uri = optionalString(image, "uri", where);
            if ( uri.empty() )
            {
                malformed(where + " has neither a bufferView nor a uri");
            }
            if ( !isDataUri(uri) )
            {
                problem = "it is an external file " + inQuotes(uri) + "; use that file directly";
                return {};
            }
            std::string mediaType{};
            bytes = decodeDataUri(uri, mediaType, where);
        }
        if ( !isJpegBytes(bytes) && !isPngBytes(bytes) )
        {
            problem = "it is neither a JPEG nor a PNG";
            return {};
        }
        return bytes;
    }

    // Base colour mapping, the material maps JK cannot use, and the base
    // colour image of every material a primitive used.
    void checkMaterials()
    {
        for ( std::size_t m = 0; m < materials_->size(); ++m )
        {
            if ( !materialUsed_[m] )
            {
                continue;
            }
            const std::string where = "material " + std::to_string(m);
            const Json& material = (*materials_)[m];
            const std::string& name = model_.materialNames[static_cast<std::size_t>(materialName_[m])];
            for ( const char* map : { "normalTexture", "occlusionTexture", "emissiveTexture" } )
            {
                if ( material.find(map) )
                {
                    ignore(std::string("material ") + map);
                }
            }
            if ( const Json* doubleSided = material.find("doubleSided");
                doubleSided && doubleSided->kind == Json::Kind::Bool && doubleSided->boolean )
            {
                ignore("material doubleSided (JK draws the front of each face only)");
            }
            const std::string alphaMode = optionalString(material, "alphaMode", where);
            if ( !alphaMode.empty() && alphaMode != "OPAQUE" )
            {
                ignore("material alphaMode " + alphaMode.substr(0, 16));
            }
            const Json* pbr = objectMember(material, "pbrMetallicRoughness", where);
            if ( !pbr )
            {
                continue;
            }
            if ( pbr->find("metallicRoughnessTexture") )
            {
                ignore("material metallicRoughnessTexture");
            }
            const std::vector<double> factor = numberArray(*pbr, "baseColorFactor", 4, where);
            if ( !factor.empty() && (factor[0] != 1.0 || factor[1] != 1.0 || factor[2] != 1.0) )
            {
                ignore("material baseColorFactor (a tint the texture would need baked in)");
            }
            const Json* baseColor = objectMember(*pbr, "baseColorTexture", where);
            if ( !baseColor )
            {
                continue;
            }
            if ( materialTextured_[m] )
            {
                if ( optionalInteger(*baseColor, "texCoord", where).value_or(0) != 0 )
                {
                    unsupported("material " + inQuotes(name) + " maps its base colour through TEXCOORD_"
                        + std::to_string(*optionalInteger(*baseColor, "texCoord", where))
                        + "; only TEXCOORD_0 is read - export the base colour on the first UV map");
                }
                if ( const Json* extensions = objectMember(*baseColor, "extensions", where);
                    extensions && extensions->find("KHR_texture_transform") )
                {
                    unsupported("material " + inQuotes(name)
                        + " offsets, scales or rotates its texture coordinates (KHR_texture_transform), which JK "
                          "cannot carry; apply the transform to the UVs before export");
                }
            }
            const std::size_t texture = requiredIndex(*baseColor, "index", textures_->size(), where + " baseColorTexture");
            const std::string textureWhere = "texture " + std::to_string(texture);
            const Json& textureObject = objectItem(*textures_, texture, textureWhere);
            const auto source = optionalIndex(textureObject, "source", images_->size(), textureWhere);
            const std::size_t imageKey = source ? *source : images_->size() + texture;
            auto found = std::find_if(report_.baseColorImages.begin(), report_.baseColorImages.end(),
                [imageKey](const GltfBaseColorImage& image) { return image.image == imageKey; });
            if ( found == report_.baseColorImages.end() )
            {
                GltfBaseColorImage image{};
                image.image = imageKey;
                if ( !source )
                {
                    image.problem = "texture " + std::to_string(texture)
                        + " has no JPEG or PNG source (its image is in an extension format)";
                }
                else
                {
                    image.name = optionalString((*images_)[*source], "name", "image " + std::to_string(*source));
                    image.bytes = imageBytes(*source, image.problem);
                    if ( image.problem.empty() )
                    {
                        image.mimeType = isJpegBytes(image.bytes) ? "image/jpeg" : "image/png";
                    }
                }
                report_.baseColorImages.push_back(std::move(image));
                found = report_.baseColorImages.end() - 1;
            }
            found->materials.push_back(name);
        }
    }

    std::span<const std::uint8_t> bytes_;
    GltfImportReport& report_;
    Json root_{};
    std::span<const std::uint8_t> bin_{};
    bool haveBin_{};
    const std::vector<Json>* accessors_{};
    const std::vector<Json>* bufferViews_{};
    const std::vector<Json>* buffers_{};
    const std::vector<Json>* meshes_{};
    const std::vector<Json>* nodes_{};
    const std::vector<Json>* materials_{};
    const std::vector<Json>* textures_{};
    const std::vector<Json>* images_{};
    std::vector<std::vector<std::uint8_t>> bufferData_{};
    std::vector<bool> bufferRead_{};
    std::vector<std::int64_t> materialName_{};
    std::vector<bool> materialUsed_{};
    std::vector<bool> materialTextured_{};
    SourceModel model_{};
};

} // namespace

ObjImportResult importGlbModel(const std::span<const std::byte> glb, const ObjImportOptions& options)
{
    model_import::validateOptions(options);
    GltfImportReport report{};
    const SourceModel source = GlbReader(glb, report).read();
    ObjImportResult result = model_import::buildModel(source, options);
    result.gltf = std::move(report);
    return result;
}

} // namespace sed::assets
