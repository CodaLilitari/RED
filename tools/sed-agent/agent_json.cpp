#include "agent_json.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sed_agent
{

namespace
{

struct Parser
{
    std::string_view text;
    std::size_t pos{};
    int depth{};

    void skipWs()
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'
               || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }
    char peek()
    {
        skipWs();
        if (pos >= text.size()) throw std::runtime_error("unexpected end of JSON");
        return text[pos];
    }
    void expect(char c)
    {
        if (peek() != c) throw std::runtime_error("unexpected character in JSON");
        ++pos;
    }
    Json value()
    {
        if (++depth > 64) throw std::runtime_error("JSON nesting too deep");
        Json result;
        const char c = peek();
        if (c == '{')
        {
            ++pos; result = Json::makeObject();
            if (peek() == '}') { ++pos; --depth; return result; }
            while (true)
            {
                Json key = valueString();
                expect(':');
                result.object[key.str] = value();
                if (peek() == ',') { ++pos; continue; }
                expect('}'); break;
            }
        }
        else if (c == '[')
        {
            ++pos; result = Json::makeArray();
            if (peek() == ']') { ++pos; --depth; return result; }
            while (true)
            {
                result.array.push_back(value());
                if (result.array.size() > 100000) throw std::runtime_error("JSON array too large");
                if (peek() == ',') { ++pos; continue; }
                expect(']'); break;
            }
        }
        else if (c == '"') result = valueString();
        else if (c == 't') { literal("true"); result = Json::makeBool(true); }
        else if (c == 'f') { literal("false"); result = Json::makeBool(false); }
        else if (c == 'n') { literal("null"); result = Json{}; }
        else result = valueNumber();
        --depth;
        return result;
    }
    void literal(std::string_view word)
    {
        if (text.substr(pos, word.size()) != word) throw std::runtime_error("invalid JSON literal");
        pos += word.size();
    }
    Json valueString()
    {
        expect('"');
        std::string out;
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"') return Json::makeString(std::move(out));
            if (c == '\\')
            {
                if (pos >= text.size()) break;
                const char e = text[pos++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 > text.size()) throw std::runtime_error("bad \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const char h = text[pos++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp += static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp += static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp += static_cast<unsigned>(h - 'A' + 10);
                        else throw std::runtime_error("bad \\u escape");
                    }
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800)
                    {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    else
                    {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: throw std::runtime_error("bad JSON escape");
                }
            }
            else out += c;
        }
        throw std::runtime_error("unterminated JSON string");
    }
    Json valueNumber()
    {
        const std::size_t begin = pos;
        if (pos < text.size() && text[pos] == '-') ++pos;
        while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos]))
               || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E'
               || text[pos] == '+' || text[pos] == '-'))
            ++pos;
        if (begin == pos) throw std::runtime_error("invalid JSON value");
        double v = 0;
        const auto sv = text.substr(begin, pos - begin);
        const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (res.ec != std::errc{} || !std::isfinite(v))
            throw std::runtime_error("invalid JSON number");
        return Json::makeNumber(v);
    }
};

void writeJson(const Json& v, std::string& out)
{
    switch (v.type)
    {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += v.boolean ? "true" : "false"; break;
    case Json::Type::Number: {
        if (v.number == std::floor(v.number) && std::abs(v.number) < 9.0e15)
            out += std::to_string(static_cast<long long>(v.number));
        else
        {
            char buf[40];
            const auto r = std::to_chars(buf, buf + sizeof(buf), v.number);
            out.append(buf, r.ptr);
        }
        break;
    }
    case Json::Type::String: out += '"'; out += jsonEscape(v.str); out += '"'; break;
    case Json::Type::Array:
        out += '[';
        for (std::size_t i = 0; i < v.array.size(); ++i)
        {
            if (i) out += ',';
            writeJson(v.array[i], out);
        }
        out += ']';
        break;
    case Json::Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& [k, val] : v.object)
        {
            if (!first) out += ',';
            first = false;
            out += '"'; out += jsonEscape(k); out += "\":";
            writeJson(val, out);
        }
        out += '}';
        break;
    }
    }
}

// SHA-256 (FIPS 180-4), implemented locally: sed_core exposes a JKL-text
// hash but no general byte hash for tool use.
struct Sha256
{
    std::uint32_t h[8]{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::uint64_t total{};
    std::array<std::byte, 64> block{};
    std::size_t blockLen{};

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress(const std::byte* p)
    {
        static constexpr std::uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::to_integer<std::uint32_t>(p[i * 4]) << 24)
                 | (std::to_integer<std::uint32_t>(p[i * 4 + 1]) << 16)
                 | (std::to_integer<std::uint32_t>(p[i * 4 + 2]) << 8)
                 | std::to_integer<std::uint32_t>(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i)
        {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3],
                      e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = hh + s1 + ch + K[i] + w[i];
            const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const std::byte* data, std::size_t size)
    {
        total += size;
        while (size > 0)
        {
            const auto take = std::min(size, block.size() - blockLen);
            std::memcpy(block.data() + blockLen, data, take);
            blockLen += take; data += take; size -= take;
            if (blockLen == block.size()) { compress(block.data()); blockLen = 0; }
        }
    }
    std::string finish()
    {
        const std::uint64_t bits = total * 8;
        const std::byte pad = std::byte{ 0x80 };
        update(&pad, 1);
        const std::byte zero = std::byte{ 0 };
        while (blockLen != 56) update(&zero, 1);
        std::byte len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::byte>((bits >> (56 - i * 8)) & 0xff);
        // Append length without counting it in `total` again.
        for (int i = 0; i < 8; ++i)
        {
            block[blockLen++] = len[i];
            if (blockLen == block.size()) { compress(block.data()); blockLen = 0; }
        }
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        for (const auto word : h)
            for (int s = 28; s >= 0; s -= 4) out += hex[(word >> s) & 0xf];
        return out;
    }
};

} // namespace

Json parseJson(std::string_view text)
{
    Parser p{ text };
    Json v = p.value();
    p.skipWs();
    if (p.pos != text.size()) throw std::runtime_error("trailing data after JSON document");
    return v;
}

std::string toJsonString(const Json& value)
{
    std::string out;
    writeJson(value, out);
    return out;
}

std::string jsonEscape(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else out += c;
        }
    }
    return out;
}

std::string sha256Hex(std::string_view bytes)
{
    Sha256 s;
    s.update(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
    return s.finish();
}

std::string sha256Hex(const std::vector<std::byte>& bytes)
{
    Sha256 s;
    s.update(bytes.data(), bytes.size());
    return s.finish();
}

} // namespace sed_agent
