#include "sed/assets/jkl_structure.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

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

Words words(std::string_view line)
{
    Words result{};
    while ( !line.empty() )
    {
        line = trim(line);
        if ( line.empty() ) break;
        const auto end = line.find_first_of(" \t\r\n");
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

    enum class Block { none, materials, colormaps, vertices, texvertices, adjoins, surfaces };
    Block block{Block::none};
    std::array<bool, 7> seenBlocks{};
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
        if ( block == Block::materials )
        {
            if ( actual > expected ) emit("capacity-exceeded", headerLine, "Material rows exceed WORLD MATERIALS capacity.");
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
        if ( section == "MATERIALS" || section == "GEORESOURCE" )
        {
            if ( section == "MATERIALS" && !countSeen )
                emit("missing-count", sectionLine, "MATERIALS has no WORLD MATERIALS count header.");
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
            if ( section == "MATERIALS" && !terminated )
                emit("missing-terminator", sectionLine, "MATERIALS is missing END before its boundary.");
        }
        block = Block::none;
        countSeen = false;
        validCount = false;
        seenBlocks = {};
        geoHeaderPosition = 0;
    };

    std::size_t offset{};
    std::size_t lineNumber{};
    while ( offset < source.size() && report.complete )
    {
        ++lineNumber;
        const auto newline = source.find('\n', offset);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        auto line = source.substr(offset, end - offset);
        offset = newline == std::string_view::npos ? source.size() : newline + 1;
        if ( line.size() > options.maxLineBytes )
        {
            emit("line-limit", lineNumber, "Raw JKL line exceeds the configured byte budget.");
            report.complete = false;
            break;
        }
        line = trim(line.substr(0, line.find('#')));
        if ( line.empty() ) continue;
        if ( line.size() >= 8 && equalAscii(line.substr(0, 8), "SECTION:") )
        {
            finishSection(false);
            section.clear();
            sectionLine = lineNumber;
            const auto name = trim(line.substr(8));
            if ( equalAscii(name, "MATERIALS") )
            {
                section = "MATERIALS";
                if ( report.inspectedMaterials ) emit("duplicate-section", lineNumber, "MATERIALS section occurs more than once.");
                report.inspectedMaterials = true;
            }
            else if ( equalAscii(name, "GEORESOURCE") )
            {
                section = "GEORESOURCE";
                if ( report.inspectedGeoResource ) emit("duplicate-section", lineNumber, "GEORESOURCE section occurs more than once.");
                report.inspectedGeoResource = true;
            }
            continue;
        }
        if ( equalAscii(line, "END") )
        {
            finishSection(true);
            section.clear();
            continue;
        }
        if ( section.empty() ) continue;

        const Words tokens = words(line);
        Block next{Block::none};
        std::size_t countPosition{2};
        if ( tokens.size >= 2 && equalAscii(tokens.values[0], "WORLD") )
        {
            if ( section == "MATERIALS" && equalAscii(tokens.values[1], "MATERIALS") ) next = Block::materials;
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
                && countValue(tokens.values[countPosition], expected);
            if ( !validCount ) emit("invalid-count", headerLine, "WORLD count must be a nonnegative signed-32-bit integer.");
            continue;
        }
        if ( block == Block::none ) continue;
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
