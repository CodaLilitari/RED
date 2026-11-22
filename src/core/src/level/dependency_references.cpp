#include "dependency_references.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <utility>

namespace sed::level::detail
{

namespace
{

enum class VisitResult { Continue, Done, Stopped };
enum class WordSpace { Package, General };

bool takeWork(const ReferenceCallbacks& callbacks)
{
    return !callbacks.work || callbacks.work();
}

bool emitReference(const ReferenceCallbacks& callbacks, const ReferenceView reference)
{
    return !callbacks.reference || callbacks.reference(reference);
}

void emitScope(const ReferenceCallbacks& callbacks, const std::string_view scope)
{
    if ( callbacks.scope )
    {
        callbacks.scope(scope);
    }
}

void emitCoverage(const ReferenceCallbacks& callbacks, const std::string_view coverage)
{
    if ( callbacks.coverage )
    {
        callbacks.coverage(coverage);
    }
}

bool equalsCi(const std::string_view left, const std::string_view right)
{
    if ( left.size() != right.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < left.size(); ++i )
    {
        if ( std::tolower(static_cast<unsigned char>(left[i]))
            != std::tolower(static_cast<unsigned char>(right[i])) )
        {
            return false;
        }
    }
    return true;
}

bool startsWithCi(const std::string_view value, const std::string_view prefix)
{
    return value.size() >= prefix.size() && equalsCi(value.substr(0, prefix.size()), prefix);
}

bool isWordSpace(const char value, const WordSpace mode)
{
    return mode == WordSpace::General
        ? std::isspace(static_cast<unsigned char>(value)) != 0
        : value == ' ' || value == '\t' || value == '\r';
}

bool takeKeyword(std::string_view& line, const std::string_view keyword, const WordSpace mode = WordSpace::Package)
{
    if ( !startsWithCi(line, keyword)
        || (line.size() > keyword.size() && !isWordSpace(line[keyword.size()], mode)) )
    {
        return false;
    }
    line = text::trimLeft(line.substr(keyword.size()));
    return true;
}

template <typename Visitor>
bool visitWords(const std::string_view line, const WordSpace mode,
    const ReferenceCallbacks& callbacks, Visitor&& visitor)
{
    std::size_t cursor = 0;
    std::size_t ordinal = 0;
    while ( cursor < line.size() )
    {
        while ( cursor < line.size() && isWordSpace(line[cursor], mode) )
        {
            ++cursor;
        }
        if ( cursor == line.size() )
        {
            break;
        }
        if ( !takeWork(callbacks) )
        {
            return false;
        }
        const std::size_t start = cursor;
        while ( cursor < line.size() && !isWordSpace(line[cursor], mode) )
        {
            ++cursor;
        }
        const VisitResult result = visitor(line.substr(start, cursor - start), ordinal++);
        if ( result != VisitResult::Continue )
        {
            return result == VisitResult::Done;
        }
    }
    return true;
}

template <typename Visitor>
bool visitLines(const std::string_view bytes, const ReferenceCallbacks& callbacks,
    Visitor&& visitor)
{
    std::size_t cursor = 0;
    while ( cursor < bytes.size() )
    {
        if ( !takeWork(callbacks) )
        {
            return false;
        }
        const std::size_t newline = bytes.find('\n', cursor);
        const std::size_t end = newline == std::string_view::npos ? bytes.size() : newline;
        const std::string_view line = bytes.substr(cursor, end - cursor);
        const VisitResult result = visitor(line);
        if ( result != VisitResult::Continue )
        {
            return result == VisitResult::Done;
        }
        cursor = newline == std::string_view::npos ? bytes.size() : newline + 1;
    }
    return true;
}

std::string_view withoutLineComments(const std::string_view line)
{
    const std::size_t cut = std::min({line.find('#'), line.find(';'), line.find("//")});
    return line.substr(0, cut);
}

struct ReferenceKind
{
    std::string_view key;
    std::string_view folder;
    std::string_view relation;
};

constexpr ReferenceKind kTables[]{
    {"sounds", "sound", "jklSound"},
    {"models", "3do", "jklModel"},
    {"sprites", "misc/spr", "jklSprite"},
    {"keyframes", "3do/key", "jklKeyframe"},
    {"animclass", "misc/pup", "jklPuppet"},
    {"soundclass", "misc/snd", "jklSoundclass"},
    {"aiclass", "misc/ai", "jklAiClass"},
    {"cogscripts", "cog", "jklCogScript"},
};

struct ResourceParameter
{
    std::string_view key;
    std::string_view folder;
    std::string_view templateRelation;
    std::string_view thingRelation;
};

constexpr ResourceParameter kResourceParameters[]{
    {"model3d", "3do", "templateModel", "thingModel"},
    {"keyframe", "3do/key", "templateKeyframe", "thingKeyframe"},
    {"puppet", "misc/pup", "templatePuppet", "thingPuppet"},
    {"soundclass", "misc/snd", "templateSoundClass", "thingSoundClass"},
    {"sound", "sound", "templateSound", "thingSound"},
    {"snd", "sound", "templateSound", "thingSound"},
    {"material", "mat", "templateMaterial", "thingMaterial"},
    {"sprite", "misc/spr", "templateSprite", "thingSprite"},
    {"aiclass", "misc/ai", "templateAiClass", "thingAiClass"},
    {"cog", "cog", "templateCog", "thingCog"},
};

bool emitRoot(const ReferenceCallbacks& callbacks, const std::string_view folder,
    const std::string_view name, const std::string_view relation)
{
    const std::string_view trimmed = text::trimView(name);
    return trimmed.empty() || text::iequalsAscii(trimmed, "none")
        || emitReference(callbacks, ReferenceView{folder, trimmed, relation});
}

VisitResult visitResourceParameter(const std::string_view word, const ReferenceCallbacks& callbacks,
    const bool onThing)
{
    const std::size_t equal = word.find('=');
    if ( equal != std::string_view::npos )
    {
        for ( const ResourceParameter& parameter : kResourceParameters )
        {
            if ( equalsCi(word.substr(0, equal), parameter.key) )
            {
                const auto relation = onThing ? parameter.thingRelation : parameter.templateRelation;
                return emitRoot(callbacks, parameter.folder, word.substr(equal + 1), relation)
                    ? VisitResult::Continue : VisitResult::Stopped;
            }
        }
    }
    return VisitResult::Continue;
}

// A decimal pose token is only a lexical guard. No coordinates are materialized.
bool isPoseToken(std::string_view word)
{
    const auto takeSign = [&word]
    {
        if ( word.starts_with('+') || word.starts_with('-') )
        {
            word.remove_prefix(1);
        }
    };
    const auto takeDigits = [&word]
    {
        const auto count = std::min(word.find_first_not_of("0123456789"), word.size());
        word.remove_prefix(count);
        return count != 0;
    };
    takeSign();
    bool hasDigits = takeDigits();
    if ( word.starts_with('.') )
    {
        word.remove_prefix(1);
        hasDigits = takeDigits() || hasDigits;
    }
    if ( !hasDigits )
    {
        return false;
    }
    if ( word.starts_with('e') || word.starts_with('E') )
    {
        word.remove_prefix(1);
        takeSign();
        return takeDigits() && word.empty();
    }
    return word.empty();
}

bool visitThingRow(const std::string_view line, const ReferenceCallbacks& callbacks)
{
    return visitWords(line, WordSpace::Package, callbacks,
        [&](std::string_view word, const std::size_t ordinal)
        {
            bool valid;
            if ( ordinal == 0 )
            {
                valid = word.ends_with(':')
                    && text::parseInt<std::uint32_t>(word.substr(0, word.size() - 1)).has_value();
            }
            else if ( ordinal < 3 )
            {
                valid = word.find('=') == word.npos;
            }
            else if ( ordinal < 9 )
            {
                valid = isPoseToken(word);
            }
            else if ( ordinal == 9 )
            {
                valid = text::parseInt<std::int32_t>(word).has_value();
            }
            else if ( ordinal == 10 && word.find('=') == word.npos )
            {
                // MotS writes an ARCH index here. Its reader also accepts a trailing colon.
                if ( word.ends_with(':') )
                {
                    word.remove_suffix(1);
                }
                valid = text::parseInt<std::int32_t>(word).has_value();
            }
            else
            {
                return visitResourceParameter(word, callbacks, true);
            }
            return valid ? VisitResult::Continue : VisitResult::Done;
        });
}

VisitResult visitFirstReference(const std::string_view line,
    const ReferenceCallbacks& callbacks, const std::string_view suffix,
    const std::string_view folder, const std::string_view relation)
{
    const bool complete = visitWords(line, WordSpace::Package, callbacks,
        [&](const std::string_view word, std::size_t)
        {
            const bool matches = suffix.empty()
                ? word.find('.') != std::string_view::npos : referenceEndsWithCi(word, suffix);
            if ( !matches )
            {
                return VisitResult::Continue;
            }
            return emitRoot(callbacks, folder, word, relation)
                ? VisitResult::Done : VisitResult::Stopped;
        });
    return complete ? VisitResult::Continue : VisitResult::Stopped;
}

// JKL sector properties run from a SECTOR record to its closing SURFACES span.
struct SectorSoundReader
{
    std::int32_t remainingSectors{};
    std::int32_t remainingVertices{};
    bool inSector{};

    VisitResult visit(const std::string_view line, const bool firstRow, const ReferenceCallbacks& callbacks)
    {
        if ( remainingVertices > 0 )
        {
            --remainingVertices;
            return VisitResult::Continue;
        }
        // Three tokens identify any relevant record. Charge each before examining it.
        std::array<std::string_view, 3> fields{};
        if ( !visitWords(line, WordSpace::General, callbacks,
            [&](const std::string_view word, const std::size_t index)
            {
                fields[index] = word;
                return index + 1 == fields.size() ? VisitResult::Done : VisitResult::Continue;
            }) )
        {
            return VisitResult::Stopped;
        }
        if ( firstRow )
        {
            if ( equalsCi(fields[0], "world") && equalsCi(fields[1], "sectors") )
            {
                remainingSectors = text::parseInt<std::int32_t>(fields[2]).value_or(0);
            }
        }
        else if ( equalsCi(fields[0], "sector") )
        {
            inSector = remainingSectors > 0 && text::parseInt<std::int32_t>(fields[1]).has_value();
            if ( inSector )
            {
                --remainingSectors;
            }
        }
        else if ( equalsCi(fields[0], "surfaces") )
        {
            inSector = false;
        }
        else if ( inSector && equalsCi(fields[0], "vertices") )
        {
            remainingVertices = text::parseInt<std::int32_t>(fields[1]).value_or(0);
        }
        else if ( inSector && equalsCi(fields[0], "sound") && !fields[2].empty() )
        {
            return emitRoot(callbacks, "sound", fields[1], "sectorAmbientSound")
                ? VisitResult::Continue : VisitResult::Stopped;
        }
        return VisitResult::Continue;
    }
};

bool visitCogReferences(const std::string_view name, const std::string_view bytes,
    const ReferenceCallbacks& callbacks)
{
    emitScope(callbacks, "cog-literal-references");
    if ( callbacks.beforeCogParse && !callbacks.beforeCogParse(bytes) )
    {
        return false;
    }
    if ( callbacks.work && !visitLines(bytes, callbacks, [&](const std::string_view line)
        {
            const bool complete = visitWords(line, WordSpace::General, callbacks,
                [](std::string_view, std::size_t) { return VisitResult::Continue; });
            return complete ? VisitResult::Continue : VisitResult::Stopped;
        }) )
    {
        return false;
    }

    const sed::assets::CogSchema schema = sed::assets::parseCogSchema(name, bytes);
    if ( !schema.isComplete() )
    {
        emitScope(callbacks, "cog-resource-defaults");
        emitCoverage(callbacks, "incompleteCogSchema");
        return true;
    }
    for ( const sed::assets::CogSymbol& symbol : schema.symbols )
    {
        if ( !symbol.defaultText )
        {
            continue;
        }
        std::string_view folder;
        switch ( symbol.kind )
        {
        case sed::assets::CogSchemaSymbolKind::sound: folder = "sound"; break;
        case sed::assets::CogSchemaSymbolKind::material: folder = "mat"; break;
        case sed::assets::CogSchemaSymbolKind::model: folder = "3do"; break;
        case sed::assets::CogSchemaSymbolKind::keyframe: folder = "3do/key"; break;
        case sed::assets::CogSchemaSymbolKind::ai: folder = "misc/ai"; break;
        case sed::assets::CogSchemaSymbolKind::cog: folder = "cog"; break;
        default: break;
        }
        if ( folder.empty() )
        {
            continue;
        }
        std::string_view value = text::trimView(*symbol.defaultText);
        if ( value.size() >= 2 && (value.front() == '\'' || value.front() == '"')
            && value.back() == value.front() )
        {
            value = text::trimView(value.substr(1, value.size() - 2));
        }
        if ( !value.empty() && value.find_first_of(" \t") == std::string_view::npos
            && !emitReference(callbacks, ReferenceView{folder, value, "cogDefaultResource"}) )
        {
            return false;
        }
    }
    return true;
}

bool visitSecondTokenReferences(const std::string_view bytes,
    const ReferenceCallbacks& callbacks, const bool puppet)
{
    return visitLines(bytes, callbacks, [&](const std::string_view rawLine)
        {
            const std::string_view line = text::trimView(withoutLineComments(rawLine));
            const bool complete = visitWords(line, WordSpace::General, callbacks,
                [&](const std::string_view word, const std::size_t ordinal)
                {
                    if ( ordinal == 0 )
                    {
                        return VisitResult::Continue;
                    }
                    if ( puppet && (word.size() < 4
                        || !text::iequalsAscii(word.substr(word.size() - 4), ".KEY")) )
                    {
                        return VisitResult::Done;
                    }
                    return emitReference(callbacks, ReferenceView{
                        puppet ? "3do/key" : "sound", word,
                        puppet ? "puppetKeyframe" : "soundClassWav"})
                        ? VisitResult::Done : VisitResult::Stopped;
                });
            return complete ? VisitResult::Continue : VisitResult::Stopped;
        });
}

} // namespace

std::string normalizeReferenceName(const std::string_view name)
{
    std::string normalized(name);
    for ( char& character : normalized )
    {
        if ( character == '\\' )
        {
            character = '/';
        }
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return normalized;
}

std::string_view referenceBaseName(const std::string_view name)
{
    const std::size_t slash = name.find_last_of("/\\");
    return slash == std::string_view::npos ? name : name.substr(slash + 1);
}

bool referenceEndsWithCi(const std::string_view text, const std::string_view suffix)
{
    return text.size() >= suffix.size() && equalsCi(text.substr(text.size() - suffix.size()), suffix);
}

void ReferenceArchiveIndex::add(std::string normalizedName, std::string normalizedBasename,
    const std::size_t ordinal)
{
    byKey_.try_emplace(std::move(normalizedName), ordinal);
    const auto [position, inserted] = byBase_.try_emplace(
        std::move(normalizedBasename), BasenameRows{ordinal, false});
    if ( !inserted )
    {
        position->second.ambiguous = true;
    }
}

std::optional<ArchiveMatch> ReferenceArchiveIndex::exact(
    const std::string_view normalizedFolderKey, const std::string_view normalizedNameKey) const
{
    if ( const auto found = byKey_.find(normalizedFolderKey); found != byKey_.end() )
    {
        return ArchiveMatch{found->second, "folderExact"};
    }
    if ( const auto found = byKey_.find(normalizedNameKey); found != byKey_.end() )
    {
        return ArchiveMatch{found->second, "authoredExact"};
    }
    return std::nullopt;
}

std::optional<ArchiveMatch> ReferenceArchiveIndex::basename(
    const std::string_view normalizedBasename, bool& ambiguous) const
{
    ambiguous = false;
    if ( const auto found = byBase_.find(normalizedBasename); found != byBase_.end() )
    {
        ambiguous = found->second.ambiguous;
        if ( !ambiguous )
        {
            return ArchiveMatch{found->second.first, "basename"};
        }
    }
    return std::nullopt;
}

bool visitRootReferences(const std::string_view input,
    const std::span<const PackageExtraResource> extras, const ReferenceCallbacks& callbacks)
{
    std::string_view section;
    bool skipHeaderRow = false;
    bool inColormaps = false;
    std::int32_t remainingThings = 0;
    std::optional<SectorSoundReader> sectorSounds;
    const bool complete = visitLines(input, callbacks, [&](const std::string_view rawLine)
        {
            std::string_view line = text::trimView(rawLine);
            if ( line.empty() || line.front() == '#' )
            {
                return VisitResult::Continue;
            }
            if ( startsWithCi(line, "section:") )
            {
                section = text::trimView(line.substr(std::string_view("section:").size()));
                // The level importer takes the second word of the comment-stripped
                // header. A name attached directly to SECTION: is part of word one.
                const auto header = line.substr(0, line.find('#'));
                const auto separator = std::find_if(header.begin(), header.end(),
                    [](char value) { return isWordSpace(value, WordSpace::General); });
                auto name = text::trimLeft(header.substr(static_cast<std::size_t>(separator - header.begin())));
                sectorSounds.reset();
                if ( takeKeyword(name, "sectors", WordSpace::General) )
                {
                    sectorSounds.emplace();
                }
                // Keep the placed-Thing header and capacity rules for its own table.
                if ( equalsCi(text::trimView(section.substr(0, section.find('#'))), "things") )
                {
                    section = "things";
                    remainingThings = 0;
                }
                skipHeaderRow = true;
                inColormaps = false;
                return VisitResult::Continue;
            }
            if ( sectorSounds || section == "things" )
            {
                line = text::trimView(line.substr(0, line.find('#')));
            }
            if ( text::iequalsAscii(line, "END") )
            {
                section = {};
                inColormaps = false;
                sectorSounds.reset();
                return VisitResult::Continue;
            }
            if ( section.empty() )
            {
                return VisitResult::Continue;
            }
            if ( equalsCi(section, "georesource") )
            {
                skipHeaderRow = false;
                auto header = line;
                if ( takeKeyword(header, "world") )
                {
                    inColormaps = takeKeyword(header, "colormaps");
                    return VisitResult::Continue;
                }
                return inColormaps
                    ? visitFirstReference(line, callbacks, ".cmp", "misc/cmp", "levelColormap")
                    : VisitResult::Continue;
            }
            if ( sectorSounds )
            {
                return sectorSounds->visit(line, std::exchange(skipHeaderRow, false), callbacks);
            }
            if ( skipHeaderRow )
            {
                if ( section == "things" && takeKeyword(line, "world") && takeKeyword(line, "things") )
                {
                    remainingThings = text::parseInt<std::int32_t>(line).value_or(0);
                }
                skipHeaderRow = false;
                return VisitResult::Continue;
            }
            if ( section == "things" )
            {
                if ( remainingThings <= 0 )
                {
                    return VisitResult::Continue;
                }
                --remainingThings;
                return visitThingRow(line, callbacks) ? VisitResult::Continue : VisitResult::Stopped;
            }
            if ( equalsCi(section, "materials") )
            {
                return visitFirstReference(line, callbacks, ".mat", "mat", "jklMaterial");
            }
            if ( equalsCi(section, "templates") )
            {
                const bool tokensComplete = visitWords(line, WordSpace::Package, callbacks,
                    [&](const std::string_view word, const std::size_t ordinal)
                    {
                        if ( ordinal < 2 )
                        {
                            return VisitResult::Continue;
                        }
                        return visitResourceParameter(word, callbacks, false);
                    });
                return tokensComplete ? VisitResult::Continue : VisitResult::Stopped;
            }
            if ( equalsCi(section, "cogs") )
            {
                return visitFirstReference(line, callbacks, ".cog", "cog", "placedCogScript");
            }
            for ( const ReferenceKind& kind : kTables )
            {
                if ( equalsCi(section, kind.key) )
                {
                    return visitFirstReference(line, callbacks, {}, kind.folder, kind.relation);
                }
            }
            return VisitResult::Continue;
        });
    if ( !complete )
    {
        return false;
    }
    for ( const PackageExtraResource& extra : extras )
    {
        if ( !emitReference(callbacks, ReferenceView{extra.folder, extra.name, "extraResource"}) )
        {
            return false;
        }
    }
    return true;
}

bool visitChildReferences(const std::string_view name, const std::string_view bytes,
    const ReferenceCallbacks& callbacks)
{
    if ( referenceEndsWithCi(name, ".cog") )
    {
        return visitCogReferences(name, bytes, callbacks);
    }
    if ( referenceEndsWithCi(name, ".ai") )
    {
        emitScope(callbacks, "subresource-dependencies");
        return true;
    }
    if ( referenceEndsWithCi(name, ".pup") || referenceEndsWithCi(name, ".snd") )
    {
        return visitSecondTokenReferences(bytes, callbacks, referenceEndsWithCi(name, ".pup"));
    }
    if ( referenceEndsWithCi(name, ".spr") )
    {
        bool found = false;
        const bool complete = visitLines(bytes, callbacks, [&](const std::string_view rawLine)
            {
                const std::string_view line = text::trimView(withoutLineComments(rawLine));
                const bool tokensComplete = visitWords(line, WordSpace::General, callbacks,
                    [&](const std::string_view word, std::size_t)
                    {
                        found = true;
                        return emitReference(callbacks, ReferenceView{"mat", word, "spriteMaterial"})
                            ? VisitResult::Done : VisitResult::Stopped;
                    });
                if ( !tokensComplete )
                {
                    return VisitResult::Stopped;
                }
                return found ? VisitResult::Done : VisitResult::Continue;
            });
        if ( complete && !found )
        {
            emitScope(callbacks, "subresource-dependencies");
            emitCoverage(callbacks, "emptySprite");
        }
        return complete;
    }
    if ( referenceEndsWithCi(name, ".3do") )
    {
        return visitLines(bytes, callbacks, [&](const std::string_view rawLine)
            {
                const bool complete = visitWords(text::trimView(rawLine), WordSpace::Package, callbacks,
                    [&](std::string_view word, const std::size_t ordinal)
                    {
                        if ( ordinal == 0 )
                        {
                            // A 3DO material row can omit the space after its numeric index.
                            const auto colon = word.find(':');
                            if ( colon != word.npos && colon > 0
                                && word.substr(0, colon).find_first_not_of("0123456789") == word.npos )
                            {
                                word.remove_prefix(colon + 1);
                            }
                        }
                        if ( referenceEndsWithCi(word, ".mat")
                            && !emitReference(callbacks, ReferenceView{"mat", word, "modelMaterial"}) )
                        {
                            return VisitResult::Stopped;
                        }
                        return VisitResult::Continue;
                    });
                return complete ? VisitResult::Continue : VisitResult::Stopped;
            });
    }
    return true;
}

} // namespace sed::level::detail
