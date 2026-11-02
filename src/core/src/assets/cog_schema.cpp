#include "sed/assets/cog_schema.hpp"

#include <cctype>
#include <utility>

#include "sed/text.hpp"

namespace sed::assets
{
namespace
{

// Schema inspection budget for captured script text (1 MiB). Larger inputs
// are refused with a diagnostic rather than an exception.
constexpr std::size_t kCogSchemaInspectionBudget = 1024u * 1024u;

std::string lowerAscii(const std::string_view text)
{
    return sed::text::lowerAscii(text);
}

std::string normalizeResourceName(const std::string_view name)
{
    std::string normalized(name);
    for (char& ch : normalized)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    return lowerAscii(normalized);
}

std::string resourceBasename(const std::string_view normalizedName)
{
    const std::size_t slash = normalizedName.rfind('/');
    return slash == std::string_view::npos
        ? std::string(normalizedName)
        : std::string(normalizedName.substr(slash + 1));
}

// Splits on whitespace (or commas) outside single/double quotes. Quote
// characters stay in the tokens, matching the measured declaration layout.
std::vector<std::string> splitOutsideQuotes(
    const std::string_view text,
    const bool commas,
    bool& balanced)
{
    std::vector<std::string> result{};
    char quote = '\0';
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i < text.size() && (text[i] == '\'' || text[i] == '"'))
        {
            if (quote == '\0')
            {
                quote = text[i];
            }
            else if (quote == text[i])
            {
                quote = '\0';
            }
        }
        const bool atEnd = i == text.size();
        const bool separator = !atEnd && quote == '\0'
            && (commas ? text[i] == ','
                       : std::isspace(static_cast<unsigned char>(text[i])) != 0);
        if (atEnd || separator)
        {
            if (i > start)
            {
                result.emplace_back(text.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    balanced = quote == '\0';
    return result;
}

// Removes a trailing `#` or `//` comment outside quotes and returns it.
std::string stripComment(const std::string_view line, std::optional<std::string>& comment)
{
    comment = std::nullopt;
    std::size_t cut = line.size();
    char quote = '\0';
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '\'' || line[i] == '"')
        {
            if (quote == '\0')
            {
                quote = line[i];
            }
            else if (quote == line[i])
            {
                quote = '\0';
            }
        }
        if (quote == '\0'
            && (line[i] == '#'
                || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')))
        {
            cut = i;
            break;
        }
    }

    if (cut < line.size())
    {
        std::string_view text = line.substr(cut);
        while (!text.empty()
            && (text.front() == '/' || text.front() == '#'
                || text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        {
            text.remove_prefix(1);
        }
        const std::string_view trimmed = sed::text::trimView(text);
        if (!trimmed.empty())
        {
            comment = std::string(trimmed);
        }
    }

    return std::string(line.substr(0, cut));
}

std::string trimDeclarationLine(const std::string_view text)
{
    std::string_view trimmed = sed::text::trimView(text);
    // A leading byte-order mark does not change the declaration.
    if (trimmed.size() >= 3
        && static_cast<unsigned char>(trimmed[0]) == 0xEFu
        && static_cast<unsigned char>(trimmed[1]) == 0xBBu
        && static_cast<unsigned char>(trimmed[2]) == 0xBFu)
    {
        trimmed.remove_prefix(3);
        trimmed = sed::text::trimLeft(trimmed);
    }
    return std::string(trimmed);
}

bool isValidSymbolName(const std::string_view name) noexcept
{
    if (name.empty())
    {
        return false;
    }
    const auto isAlpha = [](const char ch) noexcept
    {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
    };
    const auto isDigit = [](const char ch) noexcept
    {
        return ch >= '0' && ch <= '9';
    };
    if (!isAlpha(name.front()))
    {
        return false;
    }
    for (const char ch : name)
    {
        if (!isAlpha(ch) && !isDigit(ch))
        {
            return false;
        }
    }
    return true;
}

// Complete decimal 32-bit index with optional leading sign; nothing else.
bool tryParseIndex(const std::string_view text, std::int32_t& value) noexcept
{
    const std::string_view token = sed::text::trimView(text);
    if (token.empty())
    {
        return false;
    }
    const std::size_t firstDigit = (token.front() == '+' || token.front() == '-') ? 1u : 0u;
    if (firstDigit == token.size())
    {
        return false;
    }
    for (std::size_t i = firstDigit; i < token.size(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
    }
    // std::from_chars rejects a leading '+', which the measured grammar allows.
    const std::string_view digits = token.front() == '+' ? token.substr(1) : token;
    const std::optional<std::int32_t> parsed = sed::text::parseInt<std::int32_t>(digits, 10);
    if (!parsed)
    {
        return false;
    }
    value = *parsed;
    return true;
}

// Complete hexadecimal 32-bit value (bare `10` means 0x10); no sign allowed.
bool tryParseHexMask(const std::string_view text, std::uint32_t& value) noexcept
{
    std::string_view token = text;
    if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
    {
        token.remove_prefix(2);
    }
    if (token.empty())
    {
        return false;
    }
    for (const char ch : token)
    {
        const bool hexDigit = (ch >= '0' && ch <= '9')
            || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!hexDigit)
        {
            return false;
        }
    }
    const std::optional<std::uint32_t> parsed = sed::text::parseInt<std::uint32_t>(token, 16);
    if (!parsed)
    {
        return false;
    }
    value = *parsed;
    return true;
}

CogSchemaSymbolKind parseSymbolKind(const std::string_view token) noexcept
{
    const std::string lower = lowerAscii(token);
    if (lower == "message") return CogSchemaSymbolKind::message;
    if (lower == "int") return CogSchemaSymbolKind::int_;
    if (lower == "flex") return CogSchemaSymbolKind::flex;
    if (lower == "float") return CogSchemaSymbolKind::float_;
    if (lower == "vector") return CogSchemaSymbolKind::vector;
    if (lower == "thing") return CogSchemaSymbolKind::thing;
    if (lower == "sector") return CogSchemaSymbolKind::sector;
    if (lower == "surface") return CogSchemaSymbolKind::surface;
    if (lower == "sound") return CogSchemaSymbolKind::sound;
    if (lower == "material") return CogSchemaSymbolKind::material;
    if (lower == "model") return CogSchemaSymbolKind::model;
    if (lower == "template") return CogSchemaSymbolKind::template_;
    if (lower == "keyframe") return CogSchemaSymbolKind::keyframe;
    if (lower == "ai") return CogSchemaSymbolKind::ai;
    if (lower == "cog") return CogSchemaSymbolKind::cog;
    return CogSchemaSymbolKind::unknown;
}

std::optional<CogSymbol> parseSymbol(
    const std::string& line,
    const std::string& raw,
    const std::int32_t lineNumber,
    const std::optional<std::string>& comment,
    std::vector<CogSchemaDiagnostic>& diagnostics)
{
    const auto problem = [&](const char* const code, std::string message)
    {
        diagnostics.push_back(CogSchemaDiagnostic{
            std::string(code), lineNumber, std::move(message), raw });
    };

    bool balanced = true;
    const std::vector<std::string> tokens = splitOutsideQuotes(line, false, balanced);
    if (!balanced)
    {
        problem("unsupportedDeclaration", "Unterminated quoted token in symbol declaration.");
    }
    if (tokens.empty())
    {
        return std::nullopt;
    }

    const CogSchemaSymbolKind kind = parseSymbolKind(tokens[0]);
    if (kind == CogSchemaSymbolKind::unknown)
    {
        problem("unknownDeclaration", "Unknown symbol declaration type '" + tokens[0] + "'.");
        return std::nullopt;
    }
    if (tokens.size() < 2)
    {
        problem("unsupportedDeclaration", "Symbol declaration has no name.");
        return std::nullopt;
    }

    std::string name = tokens[1];
    std::optional<std::string> defaultText{};
    bool hasEquals = false;
    if (const std::size_t eq = name.find('='); eq != std::string::npos)
    {
        hasEquals = true;
        if (eq > 0)
        {
            defaultText = name.substr(eq + 1);
            name = name.substr(0, eq);
        }
    }
    if (!isValidSymbolName(name))
    {
        problem("unsupportedDeclaration", "Unsupported symbol name or declaration layout '" + name + "'.");
    }
    if (hasEquals && (!defaultText || defaultText->empty()))
    {
        problem("unsupportedDeclaration", "An explicit symbol default is empty.");
    }

    CogSymbol symbol{};
    symbol.kind = kind;
    symbol.name = name;
    symbol.defaultText = defaultText;
    symbol.rawDeclaration = raw;
    symbol.lineNumber = lineNumber;

    std::optional<std::string> description{};
    for (std::size_t i = 2; i < tokens.size(); ++i)
    {
        bool partBalanced = true;
        const std::vector<std::string> parts = splitOutsideQuotes(tokens[i], true, partBalanced);
        for (const std::string& part : parts)
        {
            symbol.rawModifiers.push_back(part);
            if (sed::text::iequalsAscii(part, "local"))
            {
                symbol.local = true;
            }
            else if (sed::text::istartsWithAscii(part, "desc="))
            {
                description = part.substr(5);
            }
            else if (sed::text::iequalsAscii(part, "nolink"))
            {
                symbol.noLink = true;
                symbol.effectiveLinkId = -1;
            }
            else if (sed::text::istartsWithAscii(part, "linkid="))
            {
                std::int32_t link = 0;
                if (tryParseIndex(part.substr(7), link))
                {
                    symbol.explicitLinkId = link;
                    symbol.effectiveLinkId = link;
                }
                else
                {
                    problem("invalidModifier", "linkid must be a complete decimal 32-bit integer.");
                }
            }
            else if (sed::text::istartsWithAscii(part, "mask="))
            {
                std::uint32_t mask = 0;
                if (tryParseHexMask(part.substr(5), mask))
                {
                    symbol.explicitMask = mask;
                    symbol.effectiveMask = mask;
                }
                else
                {
                    problem("invalidModifier", "mask must be a complete hexadecimal 32-bit value.");
                }
            }
            else
            {
                problem("unknownModifier", "Unsupported symbol modifier '" + part + "'.");
            }
        }
    }

    symbol.description = description ? description : comment;
    return symbol;
}

} // namespace

CogType CogSymbol::cogType() const noexcept
{
    switch (kind)
    {
        case CogSchemaSymbolKind::message: return CogType::msg;
        case CogSchemaSymbolKind::int_: return CogType::int_;
        case CogSchemaSymbolKind::flex: return CogType::float_;
        case CogSchemaSymbolKind::float_: return CogType::float_;
        case CogSchemaSymbolKind::vector: return CogType::vect;
        case CogSchemaSymbolKind::thing: return CogType::thing;
        case CogSchemaSymbolKind::sector: return CogType::sec;
        case CogSchemaSymbolKind::surface: return CogType::surface;
        case CogSchemaSymbolKind::sound: return CogType::wav;
        case CogSchemaSymbolKind::material: return CogType::mat;
        case CogSchemaSymbolKind::model: return CogType::model3do;
        case CogSchemaSymbolKind::template_: return CogType::template_;
        case CogSchemaSymbolKind::keyframe: return CogType::key;
        case CogSchemaSymbolKind::ai: return CogType::ai;
        case CogSchemaSymbolKind::cog: return CogType::cog;
        case CogSchemaSymbolKind::unknown: return CogType::unk;
    }
    return CogType::unk;
}

std::vector<const CogSymbol*> CogSchema::levelValues() const
{
    std::vector<const CogSymbol*> values{};
    for (const CogSymbol& symbol : symbols)
    {
        if (symbol.takesLevelValue())
        {
            values.push_back(&symbol);
        }
    }
    return values;
}

CogSchema parseCogSchema(const std::string_view name, const std::string_view text)
{
    CogSchema schema{};
    schema.name = std::string(name);
    schema.sourceText = std::string(text);

    if (text.size() > kCogSchemaInspectionBudget)
    {
        schema.diagnostics.push_back(CogSchemaDiagnostic{
            "schemaTooLarge", std::nullopt,
            "Script exceeds the 1 MiB schema-inspection budget.", std::nullopt });
        return schema;
    }

    std::vector<std::string> seenNames{};
    bool inSymbols = false;
    std::int32_t lineNumber = 0;
    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t newline = text.find('\n', cursor);
        const std::string_view rawLine = newline == std::string_view::npos
            ? text.substr(cursor)
            : text.substr(cursor, newline - cursor);
        cursor = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++lineNumber;

        const std::string raw(rawLine);
        std::optional<std::string> comment{};
        const std::string line = trimDeclarationLine(stripComment(rawLine, comment));

        if (sed::text::iequalsAscii(line, "symbols"))
        {
            if (schema.hasSymbolsBlock)
            {
                schema.diagnostics.push_back(CogSchemaDiagnostic{
                    "multipleSymbolBlocks", lineNumber,
                    "More than one symbols block is unsupported.", raw });
            }
            else
            {
                schema.hasSymbolsBlock = true;
                inSymbols = true;
            }
            continue;
        }

        if (!inSymbols)
        {
            continue;
        }

        if (sed::text::iequalsAscii(line, "end"))
        {
            inSymbols = false;
            schema.hasSymbolsEnd = true;
            continue;
        }
        if (line.empty())
        {
            continue;
        }

        schema.rawSymbolDeclarations.push_back(raw);
        if (std::optional<CogSymbol> symbol = parseSymbol(
                line, raw, lineNumber, comment, schema.diagnostics))
        {
            bool duplicate = false;
            for (const std::string& seen : seenNames)
            {
                if (sed::text::iequalsAscii(seen, symbol->name))
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                schema.diagnostics.push_back(CogSchemaDiagnostic{
                    "duplicateSymbol", lineNumber,
                    "Duplicate symbol name '" + symbol->name + "'.", raw });
            }
            else
            {
                seenNames.push_back(symbol->name);
            }
            schema.symbols.push_back(std::move(*symbol));
        }

        if (newline == std::string_view::npos)
        {
            break;
        }
    }

    if (!schema.hasSymbolsBlock)
    {
        schema.diagnostics.push_back(CogSchemaDiagnostic{
            "missingSymbolsBlock", std::nullopt, "No symbols block was found.", std::nullopt });
    }
    else if (!schema.hasSymbolsEnd)
    {
        schema.diagnostics.push_back(CogSchemaDiagnostic{
            "missingSymbolsEnd", std::nullopt, "The symbols block has no terminating end.", std::nullopt });
    }

    return schema;
}

bool cogSchemaNamesMatch(const std::string_view left, const std::string_view right) noexcept
{
    return normalizeResourceName(left) == normalizeResourceName(right);
}

std::optional<CogSchema> resolveCogSchema(
    const std::string_view requestedName,
    const std::vector<CogScriptArchive>& archives)
{
    if (sed::text::trimView(requestedName).empty())
    {
        return std::nullopt;
    }

    const std::string normalized = normalizeResourceName(requestedName);
    const std::string standard = "cog/" + normalized;

    for (const CogScriptArchive& archive : archives)
    {
        std::vector<const CogScriptCandidate*> matches{};
        for (const CogScriptCandidate& entry : archive)
        {
            if (normalizeResourceName(entry.entryName) == standard)
            {
                matches.push_back(&entry);
            }
        }
        if (matches.empty())
        {
            for (const CogScriptCandidate& entry : archive)
            {
                if (normalizeResourceName(entry.entryName) == normalized)
                {
                    matches.push_back(&entry);
                }
            }
        }
        if (matches.empty())
        {
            const std::string basename = resourceBasename(normalized);
            for (const CogScriptCandidate& entry : archive)
            {
                if (resourceBasename(normalizeResourceName(entry.entryName)) == basename)
                {
                    matches.push_back(&entry);
                }
            }
        }

        if (matches.empty())
        {
            continue;
        }
        // Ambiguity in the first matching archive is a refusal, never a
        // choice and never a fallthrough to a lower-priority archive.
        if (matches.size() != 1)
        {
            return std::nullopt;
        }
        return parseCogSchema(requestedName, matches.front()->text);
    }

    return std::nullopt;
}

} // namespace sed::assets
