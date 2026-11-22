#include "sed/assets/jkl_structure.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

#include "jkl_text_framing.hpp"
#include "sed/float_chars.hpp"

namespace sed::assets
{
namespace
{

bool whitespace(const char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string_view trim(std::string_view value)
{
    while ( !value.empty() && whitespace(value.front()) ) value.remove_prefix(1);
    while ( !value.empty() && whitespace(value.back()) ) value.remove_suffix(1);
    return value;
}

bool equalAscii(const std::string_view lhs, const std::string_view rhs)
{
    if ( lhs.size() != rhs.size() ) return false;
    for ( std::size_t i = 0; i < lhs.size(); ++i )
    {
        const char ch = lhs[i] >= 'a' && lhs[i] <= 'z'
            ? static_cast<char>(lhs[i] - 'a' + 'A') : lhs[i];
        if ( ch != rhs[i] ) return false;
    }
    return true;
}

struct Words
{
    std::array<std::string_view, 12> values{};
    std::size_t size{};
};

Words words(std::string_view line, const bool entityTable)
{
    const std::string_view separators = entityTable ? ", \t\r\n" : " \t\r\n";
    Words result{};
    while ( !line.empty() )
    {
        const auto start = line.find_first_not_of(separators);
        if ( start == std::string_view::npos ) break;
        line.remove_prefix(start);
        const auto end = line.find_first_of(separators);
        if ( result.size < result.values.size() )
            result.values[result.size] = line.substr(0, end);
        ++result.size;
        if ( end == std::string_view::npos ) break;
        line.remove_prefix(end);
    }
    return result;
}

bool countValue(const std::string_view value, std::uint32_t& result)
{
    if ( value.empty() ) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
        && result <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
}

// Keep diagnostics bounded to the names of sections covered by this scanner.
std::string_view checkedSection(std::string_view name)
{
    for ( const auto known : {"MATERIALS", "GEORESOURCE", "TEMPLATES", "MODELS", "THINGS"} )
    {
        if ( equalAscii(name, known) ) return known;
    }
    return {};
}

struct Record
{
    std::string_view text{};
    std::size_t line{};
    enum Status { ok, tooLong, unfinished, embeddedNul, ambiguousSection } status{ok};
    // The first physical row, as the compatibility importer sees it.
    std::string_view physical{};
    bool continued{};
};

// Section dispatch itself reads logical lines. Resolve continuations even
// outside a known section, retaining source views for single-line records.
Record nextRecord(const std::string_view source, std::size_t& offset, std::size_t& physicalLine,
    const std::size_t limit, std::string& joined)
{
    const auto first = physicalLine + 1;
    joined.clear();
    bool continuing{};
    std::string_view firstPhysical;
    while ( offset < source.size() )
    {
        ++physicalLine;
        const auto newline = source.find('\n', offset);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        auto line = source.substr(offset, end - offset);
        offset = newline == std::string_view::npos ? end : end + 1;
        if ( line.size() > limit ) return {{}, physicalLine, Record::tooLong};
        if ( line.find('\0') != std::string_view::npos ) return {{}, physicalLine, Record::embeddedNul};
        const auto physical = text::trimView(line.substr(0, line.find('#')));
        if ( !continuing ) firstPhysical = physical;
        else if ( text::istartsWithAscii(physical, detail::kJklSectionPrefix) )
            return {{}, physicalLine, Record::ambiguousSection};

        const auto piece = detail::splitJklPhysicalLine(line, newline != std::string_view::npos,
                                                        continuing && joined.ends_with('\\'));
        if ( piece.skipped )
        {
            if ( continuing ) continue;
            return {{}, first, Record::ok, firstPhysical};
        }
        if ( !piece.continued && !continuing )
            return {trim(piece.text), first, Record::ok, firstPhysical};
        if ( piece.trimPrevious ) joined.pop_back();
        if ( piece.text.size() > limit - joined.size() ) return {{}, first, Record::tooLong};
        joined.append(piece.text);
        if ( !piece.continued ) return {trim(joined), first, Record::ok, firstPhysical, true};
        continuing = true;
    }
    return {{}, first, Record::unfinished};
}

bool entityNumber(std::string_view value, std::uint32_t& result)
{
    if ( value.starts_with('+') ) value.remove_prefix(1);
    return countValue(value, result);
}

bool normalRow(const Words& row)
{
    if ( row.size != 4 || row.values[0].empty() || row.values[0].back() != ':' ) return false;
    std::uint32_t index{};
    if ( !countValue(row.values[0].substr(0, row.values[0].size() - 1), index) ) return false;
    for ( std::size_t i = 1; i < 4; ++i )
    {
        double value{};
        const auto token = row.values[i];
        const auto parsed = sed::text::fromChars(token.data(), token.data() + token.size(), value);
        if ( parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(value) )
            return false;
    }
    return true;
}

} // namespace

JklStructureReport inspectJklStructure(
    const std::string_view source, const JklStructureOptions& options)
{
    JklStructureReport report{};
    std::string section;
    std::size_t sectionLine{1};
    auto emit = [&](const std::string_view code, const std::size_t line, const std::string_view message)
    {
        if ( report.diagnostics.size() >= options.maxDiagnostics )
        {
            report.complete = false;
            return;
        }
        report.diagnostics.push_back({std::string(code), section, line, std::string(message)});
    };
    if ( options.maxDiagnostics == 0 )
    {
        report.complete = false;
        return report;
    }
    if ( source.size() > options.maxSourceBytes )
    {
        emit("source-limit", 1, "Raw JKL exceeds the configured source byte budget.");
        report.complete = false;
        return report;
    }

    enum class Block { none, capacity, colormaps, vertices, texvertices, adjoins, surfaces };
    Block block{Block::none};
    std::array<bool, 7> seenBlocks{};
    constexpr std::array<std::string_view, 3> entitySections{"TEMPLATES", "MODELS", "THINGS"};
    std::array<bool, entitySections.size()> seenEntitySections{};
    std::set<std::uint32_t> thingSlots;
    bool entityTable{};
    std::size_t geoHeaderPosition{};
    bool countSeen{};
    bool validCount{};
    std::uint32_t expected{};
    std::size_t actual{};
    std::size_t normals{};
    bool normalsStarted{};
    std::size_t headerLine{1};
    auto finishBlock = [&]()
    {
        if ( block == Block::none || !validCount ) return;
        if ( block == Block::capacity )
        {
            if ( actual > expected )
            {
                const auto message = std::to_string(actual) + " rows exceed WORLD " + section
                    + " capacity " + std::to_string(expected) + ".";
                emit("capacity-exceeded", headerLine, message);
            }
        }
        else if ( actual != expected )
            emit("count-mismatch", headerLine, "Geometry record count differs from its declared exact count.");
        if ( block == Block::surfaces && normals < expected )
            emit("missing-normal", headerLine, "The grouped surface-normal block is incomplete or absent.");
        if ( block == Block::surfaces && normals > expected )
            emit("count-mismatch", headerLine, "Surface-normal rows exceed the declared surface count.");
    };
    auto finishSection = [&](const bool terminated)
    {
        finishBlock();
        if ( section == "MATERIALS" || section == "GEORESOURCE" || entityTable )
        {
            if ( (section == "MATERIALS" || entityTable) && !countSeen )
                emit("missing-count", sectionLine, "Section is missing its WORLD capacity header.");
            if ( section == "GEORESOURCE" )
            {
                bool missing = false;
                for ( std::size_t i = static_cast<std::size_t>(Block::colormaps);
                      i <= static_cast<std::size_t>(Block::surfaces); ++i )
                {
                    missing = missing || !seenBlocks[i];
                }
                if ( missing ) emit("missing-count", sectionLine,
                    "Classic GEORESOURCE requires COLORMAPS, VERTICES, TEXTURE VERTICES, ADJOINS and SURFACES headers.");
            }
            if ( (section == "MATERIALS" || entityTable) && !terminated )
                emit("missing-terminator", sectionLine, "Resource section is missing END before its boundary.");
        }
        entityTable = false;
        thingSlots.clear();
        block = Block::none;
        countSeen = false;
        validCount = false;
        seenBlocks = {};
        geoHeaderPosition = 0;
    };

    std::size_t offset{};
    std::size_t physicalLine{};
    std::string joined;
    while ( offset < source.size() && report.complete )
    {
        const auto record = nextRecord(source, offset, physicalLine, options.maxLineBytes, joined);
        const auto lineNumber = record.line;
        if ( record.status != Record::ok )
        {
            if ( record.status == Record::tooLong )
                emit("line-limit", lineNumber, "JKL line or continued row exceeds the configured byte budget.");
            else if ( record.status == Record::embeddedNul )
                emit("embedded-nul", lineNumber, "Raw JKL text contains an embedded NUL.");
            else if ( record.status == Record::ambiguousSection )
                emit("ambiguous-section", lineNumber, "Physical section header occurs inside a continued record.");
            else
                emit("unfinished-continuation", lineNumber, "Continued JKL row has no following line.");
            report.complete = false;
            break;
        }
        const auto line = record.text;
        const auto name = detail::nativeJklSectionName(line);
        const auto physicalSection = text::istartsWithAscii(record.physical, detail::kJklSectionPrefix)
            ? checkedSection(detail::jklSectionName(record.physical, ProjectType::JKDF2)) : std::string_view{};
        const auto nativeSection = name ? checkedSection(*name) : std::string_view{};
        if ( name || !physicalSection.empty() )
        {
            finishSection(false);
            section = nativeSection.empty() ? physicalSection : nativeSection;
            sectionLine = lineNumber;
            if ( nativeSection != physicalSection || (!nativeSection.empty() && record.continued) )
                emit("ambiguous-section", lineNumber, "Use one physical SECTION: name header read identically by the game and RED.");
            if ( !name )
            {
                section.clear();
                continue;
            }
            if ( equalAscii(*name, "MATERIALS") )
            {
                section = "MATERIALS";
                if ( report.inspectedMaterials ) emit("duplicate-section", lineNumber, "MATERIALS section occurs more than once.");
                report.inspectedMaterials = true;
            }
            else if ( equalAscii(*name, "GEORESOURCE") )
            {
                section = "GEORESOURCE";
                if ( report.inspectedGeoResource ) emit("duplicate-section", lineNumber, "GEORESOURCE section occurs more than once.");
                report.inspectedGeoResource = true;
            }
            else
            {
                for ( std::size_t i = 0; i < entitySections.size(); ++i )
                {
                    if ( !equalAscii(*name, entitySections[i]) ) continue;
                    section = entitySections[i];
                    entityTable = true;
                    if ( seenEntitySections[i] )
                        emit("duplicate-section", lineNumber, "Resource section occurs more than once.");
                    seenEntitySections[i] = true;
                    break;
                }
            }
            continue;
        }
        if ( !section.empty() && (record.continued || (line.empty() != record.physical.empty())) )
            emit("ambiguous-record", lineNumber, "Use one physical record per logical row and hash comments in this table.");
        if ( line.empty() ) continue;
        if ( !section.empty() && text::iequalsAscii(record.physical, "END") != equalAscii(line, "END") )
            emit("ambiguous-terminator", lineNumber, "Use a standalone END for both native and compatibility readers.");
        if ( equalAscii(line, "END") )
        {
            finishSection(true);
            section.clear();
            continue;
        }
        if ( section.empty() ) continue;

        const Words tokens = words(line, entityTable);
        if ( entityTable )
        {
            // Compatibility import splits only on whitespace. A comma in a
            // leading field can hide a name, placement or whole record from it.
            auto physical = record.physical;
            const std::size_t prefix = section == "THINGS" ? 10 : 2;
            bool agree = tokens.size != 0;
            for ( std::size_t i = 0; agree && i < std::min(tokens.size, prefix); ++i )
                agree = detail::nextJklField(physical) == tokens.values[i];
            if ( !agree ) emit("ambiguous-record", lineNumber, "Separate leading row fields with whitespace for both readers.");
        }
        if ( tokens.size == 0 ) continue;
        if ( entityTable )
        {
            // stdConffile_ReadArgs uses the value after '=' and the first
            // argument alone can terminate a native table. RED compatibility
            // import requires a standalone END, so surface the disagreement.
            auto first = tokens.values[0];
            const auto equals = first.find('=');
            if ( equals != std::string_view::npos ) first.remove_prefix(equals + 1);
            if ( equalAscii(first, "END") )
            {
                emit("ambiguous-terminator", lineNumber, "Use a standalone END for both native and compatibility readers.");
                finishSection(true);
                section.clear();
                continue;
            }
        }
        Block next{Block::none};
        std::size_t countPosition{2};
        if ( tokens.size >= 2 && equalAscii(tokens.values[0], "WORLD") )
        {
            if ( (section == "MATERIALS" || entityTable) && equalAscii(tokens.values[1], section) )
                next = Block::capacity;
            else if ( section == "GEORESOURCE" )
            {
                if ( equalAscii(tokens.values[1], "COLORMAPS") ) next = Block::colormaps;
                else if ( equalAscii(tokens.values[1], "VERTICES") ) next = Block::vertices;
                else if ( equalAscii(tokens.values[1], "ADJOINS") ) next = Block::adjoins;
                else if ( equalAscii(tokens.values[1], "SURFACES") ) next = Block::surfaces;
                else if ( equalAscii(tokens.values[1], "TEXTURE") )
                {
                    next = Block::texvertices;
                    countPosition = 3;
                }
            }
        }
        if ( next != Block::none )
        {
            finishBlock();
            block = next;
            headerLine = lineNumber;
            countSeen = true;
            actual = normals = 0;
            normalsStarted = false;
            const auto index = static_cast<std::size_t>(block);
            if ( seenBlocks[index] ) emit("duplicate-count", headerLine, "WORLD count block occurs more than once.");
            if ( section == "GEORESOURCE" )
            {
                constexpr std::array<Block, 5> requiredOrder{
                    Block::colormaps, Block::vertices, Block::texvertices, Block::adjoins, Block::surfaces};
                if ( geoHeaderPosition >= requiredOrder.size() || block != requiredOrder[geoHeaderPosition] )
                    emit("count-order", headerLine,
                        "Classic GEORESOURCE headers must follow COLORMAPS, VERTICES, TEXTURE VERTICES, ADJOINS, SURFACES order.");
                // Saturation bounds even deliberately repeated headers.
                if ( geoHeaderPosition < requiredOrder.size() ) ++geoHeaderPosition;
            }
            seenBlocks[index] = true;
            validCount = tokens.size == countPosition + 1
                && (block != Block::texvertices || equalAscii(tokens.values[2], "VERTICES"))
                && countValue(tokens.values[countPosition], expected)
                && (!entityTable || line.find(',') == std::string_view::npos);
            if ( !validCount ) emit("invalid-count", headerLine, "WORLD count must be a nonnegative signed-32-bit integer.");
            continue;
        }
        if ( block == Block::none )
        {
            if ( entityTable ) emit("count-order", lineNumber, "Resource row precedes its WORLD capacity header.");
            continue;
        }
        if ( section == "THINGS" && validCount )
        {
            auto label = tokens.values[0];
            if ( label.ends_with(':') ) label.remove_suffix(1);
            std::uint32_t slot{};
            if ( !entityNumber(label, slot) )
                emit("invalid-thing-slot", lineNumber, "Thing slot must be a nonnegative signed-32-bit index.");
            else if ( slot >= expected )
                emit("thing-slot-out-of-range", lineNumber, "Thing slot must be below WORLD THINGS capacity.");
            else if ( thingSlots.contains(slot) )
                emit("duplicate-thing-slot", lineNumber, "Thing slot occurs more than once.");
            else if ( thingSlots.size() >= options.maxTrackedThingSlots )
            {
                emit("thing-slot-limit", lineNumber, "Distinct Thing slots exceed the inspection budget.");
                report.complete = false;
            }
            else thingSlots.insert(slot);
        }
        if ( block == Block::surfaces && validCount )
        {
            // Surface records have at least ten fields, unlike the four-field
            // normal rows. This also recognizes a premature normal block when
            // the declared number of surface records is too large.
            if ( !normalsStarted && actual < expected && tokens.size >= 10 ) ++actual;
            else
            {
                normalsStarted = true;
                ++normals;
                if ( !normalRow(tokens) ) emit("invalid-normal", lineNumber, "Expected an indexed finite XYZ surface normal.");
            }
        }
        else ++actual;
    }
    if ( report.complete ) finishSection(false);
    return report;
}

} // namespace sed::assets
