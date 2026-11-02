#include "sed/assets/cog_preflight.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include "sed/assets/cog_registry_tables.hpp"
#include "sed/text.hpp"

namespace sed::assets
{
namespace
{

// Small self-contained SHA-256 (FIPS 180-4) for source identity and for
// recomputing the generated registry tables digest.
class Sha256
{
public:
    Sha256()
    {
        state_ = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    }

    void update(const std::string_view data)
    {
        for (const char ch : data)
        {
            buffer_[bufferSize_++] = static_cast<std::uint8_t>(ch);
            totalSize_ += 1;
            if (bufferSize_ == buffer_.size())
            {
                transform(buffer_.data());
                bufferSize_ = 0;
            }
        }
    }

    std::string hexDigest()
    {
        const std::uint64_t bitLength = totalSize_ * 8u;
        const std::uint8_t one = 0x80u;
        update(std::string_view(reinterpret_cast<const char*>(&one), 1));
        const std::uint8_t zero = 0u;
        while (bufferSize_ != 56u)
        {
            update(std::string_view(reinterpret_cast<const char*>(&zero), 1));
        }
        std::array<std::uint8_t, 8> lengthBytes{};
        for (int i = 0; i < 8; ++i)
        {
            lengthBytes[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(bitLength >> (56 - 8 * i));
        }
        // Feed the length block directly so totalSize_ stays the message size.
        for (const std::uint8_t byte : lengthBytes)
        {
            buffer_[bufferSize_++] = byte;
            if (bufferSize_ == buffer_.size())
            {
                transform(buffer_.data());
                bufferSize_ = 0;
            }
        }

        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint32_t word : state_)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                out.push_back(kHex[(word >> shift) & 0xFu]);
            }
        }
        return out;
    }

private:
    static std::uint32_t rotateRight(const std::uint32_t value, const int bits)
    {
        return (value >> bits) | (value << (32 - bits));
    }

    void transform(const std::uint8_t* const block)
    {
        static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
                | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < schedule.size(); ++i)
        {
            const std::uint32_t s0 = rotateRight(schedule[i - 15], 7)
                ^ rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(schedule[i - 2], 17)
                ^ rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }
        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t i = 0; i < schedule.size(); ++i)
        {
            const std::uint32_t bigS1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + bigS1 + choose + kRoundConstants[i] + schedule[i];
            const std::uint32_t bigS0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = bigS0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_{ 0 };
    std::uint64_t totalSize_{ 0 };
};

std::string sha256Hex(const std::string_view data)
{
    Sha256 hash;
    hash.update(data);
    return hash.hexDigest();
}

bool isIdentifierStart(const char ch) noexcept
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

bool isIdentifierChar(const char ch) noexcept
{
    return isIdentifierStart(ch) || (ch >= '0' && ch <= '9');
}

// Replaces string/vector literals and `#` / `//` line comments with spaces,
// preserving every newline and the exact length, so source line numbers
// stay true. The captured COG lexer has no C block-comment rule, so `/*`
// sequences are deliberately left untouched here and warned about by the
// caller. With keepLiterals set, quoted literals survive (header lines).
std::string scrub(const std::string_view text, const bool keepLiterals)
{
    std::string out(text);
    std::size_t i = 0;
    while (i < text.size())
    {
        const char ch = text[i];
        if (ch == '"' || ch == '\'')
        {
            std::size_t end = i + 1;
            bool closed = false;
            while (end < text.size())
            {
                if (text[end] == '\\')
                {
                    end += 2;
                    continue;
                }
                if (text[end] == ch)
                {
                    closed = true;
                    break;
                }
                ++end;
            }
            if (!closed)
            {
                ++i;
                continue;
            }
            if (!keepLiterals)
            {
                for (std::size_t k = i; k <= end; ++k)
                {
                    if (out[k] != '\n')
                    {
                        out[k] = ' ';
                    }
                }
            }
            i = end + 1;
            continue;
        }
        if (ch == '#' || (ch == '/' && i + 1 < text.size() && text[i + 1] == '/'))
        {
            std::size_t end = i;
            while (end < text.size() && text[end] != '\n')
            {
                ++end;
            }
            for (std::size_t k = i; k < end; ++k)
            {
                out[k] = ' ';
            }
            i = end;
            continue;
        }
        ++i;
    }
    return out;
}

// Header lines reset quote handling per line: an apostrophe in an unquoted
// `desc=where's` value must not open a literal spanning the code section.
std::string scrubHeaderLines(const std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t newline = text.find('\n', cursor);
        const std::string_view line = newline == std::string_view::npos
            ? text.substr(cursor)
            : text.substr(cursor, newline - cursor + 1);
        out += scrub(line, true);
        if (newline == std::string_view::npos)
        {
            break;
        }
        cursor = newline + 1;
    }
    return out;
}

struct LineSpan
{
    std::size_t start{};
    std::string_view content{}; // Excludes the trailing '\n'; may keep '\r'.
};

std::vector<LineSpan> splitLines(const std::string_view text)
{
    std::vector<LineSpan> lines{};
    std::size_t cursor = 0;
    while (cursor < text.size())
    {
        const std::size_t newline = text.find('\n', cursor);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        lines.push_back(LineSpan{ cursor, text.substr(cursor, end - cursor) });
        if (newline == std::string_view::npos)
        {
            break;
        }
        cursor = newline + 1;
    }
    return lines;
}

// True when the line is exactly `word` (ASCII case-insensitive) aside from
// surrounding spaces, tabs, and a carriage return.
bool isSectionLine(const std::string_view content, const std::string_view word)
{
    const std::string_view trimmed = sed::text::trimView(content);
    return sed::text::iequalsAscii(trimmed, word);
}

std::vector<std::int32_t> lineIndex(const std::string_view text)
{
    std::vector<std::int32_t> lines(text.size() + 1, 1);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        lines[i + 1] = lines[i] + (text[i] == '\n' ? 1 : 0);
    }
    return lines;
}

template <std::size_t Size>
bool tableContains(
    const std::array<std::string_view, Size>& table,
    const std::string_view lowerName)
{
    return std::binary_search(table.begin(), table.end(), lowerName);
}

template <std::size_t Size>
bool tableSorted(const std::array<std::string_view, Size>& table)
{
    return std::is_sorted(table.begin(), table.end())
        && std::adjacent_find(table.begin(), table.end()) == table.end();
}

bool isSymbolType(const std::string_view lowerKind)
{
    static constexpr std::array<std::string_view, 15> kTypes = {
        "int", "flex", "float", "vector", "thing", "template", "surface",
        "sector", "sound", "keyframe", "model", "material", "ai", "cog",
        "message",
    };
    return std::find(kTypes.begin(), kTypes.end(), lowerKind) != kTypes.end();
}

bool isCallKeyword(const std::string_view lowerName)
{
    static constexpr std::array<std::string_view, 6> kExcluded = {
        "if", "while", "for", "switch", "return", "call",
    };
    return std::find(kExcluded.begin(), kExcluded.end(), lowerName) != kExcluded.end();
}

bool isLanguageWord(const std::string_view lowerName)
{
    static constexpr std::array<std::string_view, 23> kWords = {
        "if", "while", "for", "switch", "return", "call", "end", "code",
        "symbols", "local", "int", "flex", "float", "vector", "thing",
        "sector", "surface", "sound", "material", "model", "template",
        "keyframe", "ai",
    };
    // `cog` and `message` are declaration types too; handled by callers via
    // the declared-name and registry sets, so they are not listed here.
    return std::find(kWords.begin(), kWords.end(), lowerName) != kWords.end();
}

} // namespace

std::string_view cogPreflightTargetName(const CogPreflightTarget target) noexcept
{
    switch (target)
    {
        case CogPreflightTarget::jk: return "jk";
        case CogPreflightTarget::mots: return "mots";
        case CogPreflightTarget::ijim: return "ijim";
    }
    return "jk";
}

std::optional<CogPreflightTarget> parseCogPreflightTarget(
    const std::string_view name) noexcept
{
    if (sed::text::iequalsAscii(name, "jk")) return CogPreflightTarget::jk;
    if (sed::text::iequalsAscii(name, "mots")) return CogPreflightTarget::mots;
    if (sed::text::iequalsAscii(name, "ijim")) return CogPreflightTarget::ijim;
    return std::nullopt;
}

CogRegistryInfo cogRegistryInfo(const CogPreflightTarget target) noexcept
{
    CogRegistryInfo info{};
    info.target = target;
    switch (target)
    {
        case CogPreflightTarget::jk:
            info.measured = true;
            info.retailBinarySha256 = cog_registry::kJkRetailBinarySha256;
            info.registrySourceJsonSha256 = cog_registry::kJkRegistrySourceJsonSha256;
            info.verbCount = cog_registry::kJkVerbs.size();
            info.messageCount = cog_registry::kJkMessages.size();
            info.globalFloatCount = cog_registry::kJkGlobalFloats.size();
            break;
        case CogPreflightTarget::mots:
            info.measured = true;
            info.retailBinarySha256 = cog_registry::kMotsRetailBinarySha256;
            info.registrySourceJsonSha256 = cog_registry::kMotsRegistrySourceJsonSha256;
            info.verbCount = cog_registry::kMotsVerbs.size();
            info.messageCount = cog_registry::kMotsMessages.size();
            info.globalFloatCount = cog_registry::kMotsGlobalFloats.size();
            break;
        case CogPreflightTarget::ijim:
            break;
    }
    return info;
}

bool cogVerbAvailable(
    const CogPreflightTarget target,
    const std::string_view spelling) noexcept
{
    const std::string lower = sed::text::lowerAscii(spelling);
    switch (target)
    {
        case CogPreflightTarget::jk:
            return tableContains(cog_registry::kJkVerbs, lower);
        case CogPreflightTarget::mots:
            return tableContains(cog_registry::kMotsVerbs, lower);
        case CogPreflightTarget::ijim:
            return false;
    }
    return false;
}

bool cogMessageAvailable(
    const CogPreflightTarget target,
    const std::string_view spelling) noexcept
{
    const std::string lower = sed::text::lowerAscii(spelling);
    switch (target)
    {
        case CogPreflightTarget::jk:
            return tableContains(cog_registry::kJkMessages, lower);
        case CogPreflightTarget::mots:
            return tableContains(cog_registry::kMotsMessages, lower);
        case CogPreflightTarget::ijim:
            return false;
    }
    return false;
}

bool cogRegistryTablesIntact()
{
    if (!tableSorted(cog_registry::kJkVerbs)
        || !tableSorted(cog_registry::kJkMessages)
        || !tableSorted(cog_registry::kMotsVerbs)
        || !tableSorted(cog_registry::kMotsMessages)
        || !tableSorted(cog_registry::kJkGlobalFloats)
        || !tableSorted(cog_registry::kMotsGlobalFloats))
    {
        return false;
    }
    // Canonical serialization shared with the generator: one section label
    // line, then each lower-case spelling on its own line.
    std::string canonical;
    const auto appendTable = [&canonical](const char* const label, const auto& table)
    {
        canonical += label;
        canonical += '\n';
        for (const std::string_view name : table)
        {
            canonical += name;
            canonical += '\n';
        }
    };
    appendTable("jk-verbs", cog_registry::kJkVerbs);
    appendTable("jk-messages", cog_registry::kJkMessages);
    appendTable("mots-verbs", cog_registry::kMotsVerbs);
    appendTable("mots-messages", cog_registry::kMotsMessages);
    appendTable("jk-global-floats", cog_registry::kJkGlobalFloats);
    appendTable("mots-global-floats", cog_registry::kMotsGlobalFloats);
    return sha256Hex(canonical) == cog_registry::kCogRegistryTablesSha256;
}

CogPreflightResult runCogPreflight(
    const std::string_view fileName,
    const std::string_view text,
    const CogPreflightTarget target,
    const CogPreflightOptions options)
{
    CogPreflightResult result{};
    result.fileName = std::string(fileName);
    result.sourceSha256 = sha256Hex(text);
    result.target = target;
    result.targetName = std::string(cogPreflightTargetName(target));
    const CogRegistryInfo info = cogRegistryInfo(target);
    result.measured = info.measured;
    result.status = info.measured
        ? CogPreflightStatus::measured
        : CogPreflightStatus::targetUnmeasured;
    if (info.measured)
    {
        result.retailBinarySha256 = std::string(info.retailBinarySha256);
        result.registrySourceJsonSha256 = std::string(info.registrySourceJsonSha256);
    }
    result.notes = {
        "Availability is measured for the pinned original executable; it is not an argument/type or runtime check.",
        "Message tables list integer message constants; accepted generic global-symbol aliases and actual event delivery are separate questions.",
        "Resource spellings are a discovery heuristic and may include comments; dynamic references are not resolved.",
        "Changing the source can affect bytecode layout; this tool does not predict multiplayer checksums.",
    };

    // Working copy for parsing only; the caller's text is never modified.
    std::string_view working = text;
    if (working.size() >= 3
        && static_cast<unsigned char>(working[0]) == 0xEFu
        && static_cast<unsigned char>(working[1]) == 0xBBu
        && static_cast<unsigned char>(working[2]) == 0xBFu)
    {
        working.remove_prefix(3);
    }
    while (!working.empty() && working.back() == '\x1A')
    {
        working.remove_suffix(1);
    }
    const std::string original(working);

    const std::string declarationsText = scrubHeaderLines(original);

    // Split header/code at the `code` line.
    const std::vector<LineSpan> declLines = splitLines(declarationsText);
    bool hasCode = false;
    std::size_t codeOffset = 0;
    for (const LineSpan& line : declLines)
    {
        if (isSectionLine(line.content, "code"))
        {
            hasCode = true;
            codeOffset = line.start + line.content.size();
            break;
        }
    }
    result.hasCode = hasCode;
    const std::string clean = hasCode
        ? declarationsText.substr(0, codeOffset)
            + scrub(std::string_view(original).substr(codeOffset), false)
        : declarationsText;
    std::string_view code =
        hasCode ? std::string_view(clean).substr(codeOffset) : std::string_view{};
    const std::vector<std::int32_t> lines = lineIndex(clean);

    // Truncate the code at its first standalone `end` word.
    std::size_t codeLength = code.size();
    for (std::size_t i = 0; i < code.size();)
    {
        if (!isIdentifierStart(code[i]))
        {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < code.size() && isIdentifierChar(code[i]))
        {
            ++i;
        }
        if (sed::text::iequalsAscii(code.substr(start, i - start), "end"))
        {
            codeLength = start;
            break;
        }
    }
    code = code.substr(0, codeLength);

    const auto lineAt = [&lines, codeOffset](const std::size_t codePos)
    {
        return lines[codeOffset + codePos];
    };

    // Call inventory: identifiers followed by `(` in the code section.
    std::map<std::string, std::vector<std::int32_t>> callMap{};
    // Label inventory: identifiers followed by `:` (but not `::`).
    std::map<std::string, std::vector<std::int32_t>> labelMap{};
    std::set<std::string> identifierSet{};
    for (std::size_t i = 0; i < code.size();)
    {
        if (!isIdentifierStart(code[i]))
        {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < code.size() && isIdentifierChar(code[i]))
        {
            ++i;
        }
        const std::string_view word = code.substr(start, i - start);
        const std::string lower = sed::text::lowerAscii(word);
        identifierSet.insert(lower);
        std::size_t next = i;
        while (next < code.size()
            && (code[next] == ' ' || code[next] == '\t' || code[next] == '\r'))
        {
            ++next;
        }
        if (next < code.size() && code[next] == '(' && !isCallKeyword(lower))
        {
            callMap[lower].push_back(lineAt(start));
        }
        if (next < code.size() && code[next] == ':'
            && (next + 1 >= code.size() || code[next + 1] != ':'))
        {
            labelMap[lower].push_back(lineAt(start));
        }
    }
    for (const auto& [name, callLines] : callMap)
    {
        result.calls.push_back(CogPreflightCall{ name, callLines });
    }
    for (const auto& [name, labelLines] : labelMap)
    {
        result.labels.push_back(CogPreflightLabel{ name, labelLines });
        if (labelLines.size() > 1)
        {
            result.duplicateLabels.push_back(CogPreflightLabel{ name, labelLines });
        }
    }

    // Declarations: the `symbols` block of the header-scrubbed text.
    bool inSymbols = false;
    bool symbolsClosed = false;
    for (const LineSpan& line : declLines)
    {
        if (!inSymbols)
        {
            if (isSectionLine(line.content, "symbols"))
            {
                inSymbols = true;
            }
            continue;
        }
        if (isSectionLine(line.content, "end"))
        {
            symbolsClosed = true;
            break;
        }
        const std::string_view content = line.content;
        std::size_t pos = 0;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'))
        {
            ++pos;
        }
        const std::size_t typeStart = pos;
        while (pos < content.size() && isIdentifierChar(content[pos]))
        {
            ++pos;
        }
        if (pos == typeStart)
        {
            continue;
        }
        const std::string_view typeToken = content.substr(typeStart, pos - typeStart);
        const std::size_t gapStart = pos;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'))
        {
            ++pos;
        }
        if (pos == gapStart || pos >= content.size() || !isIdentifierStart(content[pos]))
        {
            continue;
        }
        const std::size_t nameStart = pos;
        while (pos < content.size() && isIdentifierChar(content[pos]))
        {
            ++pos;
        }
        CogPreflightDeclaration declaration{};
        declaration.type = sed::text::lowerAscii(typeToken);
        declaration.semanticType =
            declaration.type == "float" ? "flex" : declaration.type;
        declaration.name = std::string(content.substr(nameStart, pos - nameStart));
        declaration.tail =
            std::string(sed::text::trimView(content.substr(pos)));
        declaration.line = lines[line.start];
        if (declaration.type == "message")
        {
            std::string registered = sed::text::lowerAscii(declaration.name);
            const std::string_view tail = declaration.tail;
            if (!tail.empty() && tail.front() == '=')
            {
                const std::string_view afterEquals =
                    sed::text::trimLeft(tail.substr(1));
                std::size_t tokenEnd = 0;
                while (tokenEnd < afterEquals.size()
                    && !std::isspace(static_cast<unsigned char>(afterEquals[tokenEnd])))
                {
                    ++tokenEnd;
                }
                if (tokenEnd > 0)
                {
                    registered =
                        sed::text::lowerAscii(afterEquals.substr(0, tokenEnd));
                }
            }
            declaration.registeredMessage = registered;
        }
        if (isSymbolType(declaration.type))
        {
            result.declarations.push_back(std::move(declaration));
        }
        else
        {
            result.unknownDeclarations.push_back(std::move(declaration));
        }
    }
    result.hasSymbols = inSymbols && symbolsClosed;

    for (const CogPreflightDeclaration& declaration : result.declarations)
    {
        if (declaration.type == "message" && declaration.registeredMessage)
        {
            result.declaredMessages.push_back(*declaration.registeredMessage);
        }
    }

    const bool jkTarget = target == CogPreflightTarget::jk;
    const auto inVerbs = [target, jkTarget](const std::string_view lowerName)
    {
        if (target == CogPreflightTarget::ijim) return false;
        return jkTarget
            ? tableContains(cog_registry::kJkVerbs, lowerName)
            : tableContains(cog_registry::kMotsVerbs, lowerName);
    };
    const auto inOtherVerbs = [jkTarget](const std::string_view lowerName)
    {
        return jkTarget
            ? tableContains(cog_registry::kMotsVerbs, lowerName)
            : tableContains(cog_registry::kJkVerbs, lowerName);
    };
    const auto inMessages = [target, jkTarget](const std::string_view lowerName)
    {
        if (target == CogPreflightTarget::ijim) return false;
        return jkTarget
            ? tableContains(cog_registry::kJkMessages, lowerName)
            : tableContains(cog_registry::kMotsMessages, lowerName);
    };
    const auto inFloatGlobals = [target, jkTarget](const std::string_view lowerName)
    {
        if (target == CogPreflightTarget::ijim) return false;
        return jkTarget
            ? tableContains(cog_registry::kJkGlobalFloats, lowerName)
            : tableContains(cog_registry::kMotsGlobalFloats, lowerName);
    };

    if (result.measured)
    {
        for (const CogPreflightCall& call : result.calls)
        {
            if (!inVerbs(call.name))
            {
                result.absentCalls.push_back(CogPreflightAbsentCall{
                    call.name, call.lines, inOtherVerbs(call.name) });
            }
        }
        std::set<std::string> absentMessageSet{};
        for (const std::string& declared : result.declaredMessages)
        {
            if (!inMessages(declared) && !inFloatGlobals(declared))
            {
                absentMessageSet.insert(declared);
            }
        }
        result.absentMessages.assign(absentMessageSet.begin(), absentMessageSet.end());
        for (const CogPreflightDeclaration& declaration : result.declarations)
        {
            if (declaration.type == "message" && declaration.registeredMessage
                && inFloatGlobals(*declaration.registeredMessage))
            {
                result.nonstandardMessageBindings.push_back(declaration);
            }
        }
    }

    // Declared-vs-handled message labels, checked separately from duplicate
    // labels: a missing handler and a duplicated handler are different
    // dispatch-contract findings.
    {
        std::set<std::string> seenHandlers{};
        std::set<std::string> declaredNames{};
        std::set<std::string> registeredNames{};
        for (const CogPreflightDeclaration& declaration : result.declarations)
        {
            declaredNames.insert(sed::text::lowerAscii(declaration.name));
            if (declaration.type == "message" && declaration.registeredMessage)
            {
                registeredNames.insert(*declaration.registeredMessage);
            }
        }
        for (const CogPreflightDeclaration& declaration : result.declarations)
        {
            if (declaration.type != "message")
            {
                continue;
            }
            // A measured global-float binding is not an event handler and
            // is reported as a nonstandard binding, not a missing handler.
            if (declaration.registeredMessage
                && inFloatGlobals(*declaration.registeredMessage))
            {
                continue;
            }
            const std::string handler = sed::text::lowerAscii(declaration.name);
            if (!seenHandlers.insert(handler).second)
            {
                continue;
            }
            if (labelMap.find(handler) == labelMap.end())
            {
                result.missingMessageHandlers.push_back(declaration.name);
            }
        }
        if (result.measured)
        {
            for (const CogPreflightLabel& label : result.labels)
            {
                if (inMessages(label.name)
                    && !declaredNames.count(label.name)
                    && !registeredNames.count(label.name))
                {
                    result.undeclaredMessageHandlers.push_back(label.name);
                }
            }
        }
    }

    // Undeclared identifiers are a typo/integration review inventory only:
    // the loader auto-creates them, so they are never lexical errors.
    if (result.measured)
    {
        std::set<std::string> declaredNames{};
        for (const CogPreflightDeclaration& declaration : result.declarations)
        {
            declaredNames.insert(sed::text::lowerAscii(declaration.name));
        }
        for (const CogPreflightDeclaration& declaration : result.unknownDeclarations)
        {
            declaredNames.insert(sed::text::lowerAscii(declaration.name));
        }
        std::set<std::string> labelNames{};
        for (const CogPreflightLabel& label : result.labels)
        {
            labelNames.insert(label.name);
        }
        for (const std::string& identifier : identifierSet)
        {
            if (isLanguageWord(identifier) || declaredNames.count(identifier)
                || labelNames.count(identifier) || inVerbs(identifier)
                || inMessages(identifier) || inFloatGlobals(identifier))
            {
                continue;
            }
            result.undeclaredIdentifiers.push_back(identifier);
        }
    }

    // The `flags=` header value, when present.
    for (const LineSpan& line : splitLines(clean))
    {
        const std::string_view content = line.content;
        std::size_t pos = 0;
        while (pos < content.size()
            && std::isspace(static_cast<unsigned char>(content[pos])))
        {
            ++pos;
        }
        if (!sed::text::istartsWithAscii(content.substr(pos), "flags"))
        {
            continue;
        }
        pos += 5;
        while (pos < content.size()
            && std::isspace(static_cast<unsigned char>(content[pos])))
        {
            ++pos;
        }
        if (pos >= content.size() || content[pos] != '=')
        {
            continue;
        }
        ++pos;
        while (pos < content.size()
            && std::isspace(static_cast<unsigned char>(content[pos])))
        {
            ++pos;
        }
        const std::size_t valueStart = pos;
        while (pos < content.size()
            && !std::isspace(static_cast<unsigned char>(content[pos])))
        {
            ++pos;
        }
        if (pos > valueStart)
        {
            result.flags = std::string(content.substr(valueStart, pos - valueStart));
        }
        break;
    }

    // Literal resource-reference heuristic over the original text.
    {
        static constexpr std::array<std::string_view, 8> kExtensions = {
            "cog", "3do", "key", "pup", "mat", "wav", "snd", "ai",
        };
        std::set<std::string> references{};
        for (std::size_t i = 0; i < original.size(); ++i)
        {
            if (original[i] != '.')
            {
                continue;
            }
            for (const std::string_view extension : kExtensions)
            {
                if (i + 1 + extension.size() > original.size()
                    || !sed::text::iequalsAscii(
                        std::string_view(original).substr(i + 1, extension.size()),
                        extension))
                {
                    continue;
                }
                const std::size_t after = i + 1 + extension.size();
                if (after < original.size() && isIdentifierChar(original[after]))
                {
                    continue;
                }
                std::size_t start = i;
                while (start > 0)
                {
                    const char prev = original[start - 1];
                    if (std::isspace(static_cast<unsigned char>(prev))
                        || prev == '=' || prev == '"' || prev == '<' || prev == '>')
                    {
                        break;
                    }
                    --start;
                }
                if (start < i)
                {
                    references.insert(original.substr(start, after - start));
                }
                break;
            }
        }
        result.literalResourceReferences.assign(references.begin(), references.end());
    }

    if (code.find("/*") != std::string_view::npos
        || code.find("*/") != std::string_view::npos)
    {
        result.warnings.push_back(
            "C-style block-comment markers are unsupported by the captured lexer; their contents were not silently removed.");
    }
    if (!result.unknownDeclarations.empty())
    {
        result.warnings.push_back("Unrecognized declaration types require manual review.");
    }
    if (!result.nonstandardMessageBindings.empty())
    {
        result.warnings.push_back(
            "A message binds a measured global float symbol rather than an integer message constant. Generic symbol lookup can accept it; normal event delivery is not established.");
    }
    if (!result.measured)
    {
        result.warnings.push_back(
            "Target has no measured registry in this slice; no other target's table was substituted.");
    }

    result.availabilityFindings =
        result.absentCalls.size() + result.absentMessages.size();
    result.strictFailure = options.strict && result.availabilityFindings > 0;
    return result;
}

} // namespace sed::assets

