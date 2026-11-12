#include "sed/assets/model_lighting.hpp"

#include "sed/assets/3do.hpp"
#include "sed/float_chars.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sed::assets
{
namespace
{

[[noreturn]] void refuse(const std::string& detail)
{
    throw std::runtime_error("Model lighting: " + detail);
}

void require(const bool condition, const char* detail)
{
    if ( !condition )
    {
        refuse(detail);
    }
}

void checkCancelled(const ModelLightingOptions* options)
{
    if ( options && options->cancelled && options->cancelled() )
    {
        refuse("cancelled; no output was published.");
    }
}

void validateLimits(const ModelLightingLimits& limits)
{
    const ModelLightingLimits hard;
    const std::array values{
        std::pair{ limits.sourceBytes, hard.sourceBytes },
        std::pair{ limits.lines, hard.lines },
        std::pair{ limits.tokensPerLine, hard.tokensPerLine },
        std::pair{ limits.geosets, hard.geosets },
        std::pair{ limits.meshes, hard.meshes },
        std::pair{ limits.vertices, hard.vertices },
        std::pair{ limits.triangles, hard.triangles },
        std::pair{ limits.hierarchyNodes, hard.hierarchyNodes },
        std::pair{ limits.lights, hard.lights },
        std::pair{ limits.workUnits, hard.workUnits }
    };
    for ( const auto& [value, ceiling] : values )
    {
        require(value <= ceiling, "operational limit exceeds its hard maximum.");
    }
}

struct WorkBudget
{
    std::size_t remaining;
    const ModelLightingOptions* options{};

    void charge(const std::size_t count = 1)
    {
        require(count <= remaining, "work budget exceeded.");
        remaining -= count;
        checkCancelled(options);
    }

    void product(const std::size_t left, const std::size_t right)
    {
        require(right == 0 || left <= remaining / right, "work budget exceeded.");
        charge(left * right);
    }
};

void addBounded(std::size_t& total, const std::size_t amount, const std::size_t ceiling)
{
    require(total <= ceiling && amount <= ceiling - total, "geometry budget exceeded.");
    total += amount;
}

bool finite(const TVector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const ColorF& value)
{
    return std::isfinite(value.r) && std::isfinite(value.g)
        && std::isfinite(value.b) && std::isfinite(value.a);
}

double length(const TVector& value)
{
    return std::hypot(value.x, value.y, value.z);
}

TVector unit(const TVector& value)
{
    const double scale = std::max({ std::abs(value.x), std::abs(value.y), std::abs(value.z) });
    if ( scale == 0 )
    {
        return {};
    }
    require(std::isfinite(scale), "non-finite normal or direction.");
    const TVector scaled{ value.x / scale, value.y / scale, value.z / scale };
    const double magnitude = length(scaled);
    return { scaled.x / magnitude, scaled.y / magnitude, scaled.z / magnitude };
}

double number(std::string_view token)
{
    if ( !token.empty() && token.front() == '+' )
    {
        token.remove_prefix(1);
    }
    double value{};
    const auto parsed = sed::text::fromChars(
        token.data(), token.data() + token.size(), value, std::chars_format::general);
    require(!token.empty() && parsed.ec == std::errc{}
            && parsed.ptr == token.data() + token.size() && std::isfinite(value),
        "invalid or non-finite numeric field.");
    return value;
}

float colorNumber(const std::string_view token)
{
    const double value = number(token);
    require(std::abs(value) <= std::numeric_limits<float>::max(), "light value is outside float range.");
    return static_cast<float>(value);
}

std::int32_t integer(const std::string_view token)
{
    const auto value = sed::text::parseInt<std::int32_t>(token);
    require(value.has_value(), "invalid integer field.");
    return *value;
}

std::size_t count(const std::string_view token)
{
    const std::int32_t value = integer(token);
    require(value >= 0, "negative record count.");
    return static_cast<std::size_t>(value);
}

void rowIndex(std::string_view token, const std::size_t expected)
{
    require(!token.empty() && token.back() == ':', "record index must end in a colon.");
    token.remove_suffix(1);
    require(count(token) == expected, "missing, duplicate, or out-of-order record index.");
}

using Words = std::vector<std::string_view>;

Words words(std::string_view line, const ModelLightingLimits& limits)
{
    Words result;
    line = sed::text::trimView(line);
    while ( !line.empty() )
    {
        require(result.size() < limits.tokensPerLine, "line-token budget exceeded.");
        // Use the loader's whitespace predicate so a process locale cannot
        // make the geometry parser see control words this scanner overlooked.
        std::size_t size = 0;
        while ( size < line.size() && !std::isspace(static_cast<unsigned char>(line[size])) )
        {
            ++size;
        }
        result.push_back(line.substr(0, size));
        line.remove_prefix(size);
        line = sed::text::trimLeft(line);
    }
    return result;
}

bool is(const std::string_view value, const std::string_view expected)
{
    return sed::text::iequalsAscii(value, expected);
}

struct Sections
{
    std::vector<std::string_view> header;
    std::vector<std::string_view> resources;
    std::vector<std::string_view> geometry;
    std::vector<std::string_view> hierarchy;
};

Sections scan(const std::string_view text, const ModelLightingLimits& limits, WorkBudget& work)
{
    require(text.size() <= limits.sourceBytes, "source byte budget exceeded.");
    require(text.find('\0') == std::string_view::npos, "NUL byte in text input.");
    work.charge(text.size());
    Sections result;
    std::array<bool, 4> seen{};
    std::vector<std::string_view>* section = nullptr;
    std::size_t lineCount = 0;
    for ( std::size_t begin = 0; begin < text.size(); )
    {
        require(lineCount++ < limits.lines, "line budget exceeded.");
        const auto newline = text.find('\n', begin);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        auto line = text.substr(begin, end - begin);
        if ( const auto comment = line.find('#'); comment != std::string_view::npos )
        {
            line = line.substr(0, comment);
        }
        line = sed::text::trimView(line);
        if ( !line.empty() )
        {
            const auto tokens = words(line, limits);
            if ( is(tokens[0], "END") )
            {
                require(tokens.size() == 1, "ambiguous END record.");
                section = nullptr;
            }
            else if ( sed::text::istartsWithAscii(line, "SECTION:") )
            {
                // loadFrom3DO recognizes every SECTION: prefix. Reject its
                // noncanonical aliases even inside otherwise opaque sections,
                // where accepting one could bind light spans to another mesh.
                require(is(tokens[0], "SECTION:") && tokens.size() == 2, "ambiguous section header.");
                section = nullptr;
                const std::array names{ "HEADER", "MODELRESOURCE", "GEOMETRYDEF", "HIERARCHYDEF" };
                const std::array targets{ &result.header, &result.resources, &result.geometry, &result.hierarchy };
                for ( std::size_t i = 0; i < names.size(); ++i )
                {
                    if ( is(tokens[1], names[i]) )
                    {
                        require(!seen[i], "duplicate known section.");
                        seen[i] = true;
                        section = targets[i];
                        break;
                    }
                }
            }
            else if ( section )
            {
                section->push_back(line);
            }
        }
        begin = newline == std::string_view::npos ? text.size() : newline + 1;
    }
    require(seen[0] && seen[1] && seen[2] && seen[3], "required 3DO section is missing.");
    return result;
}

struct Cursor
{
    const std::vector<std::string_view>& lines;
    const ModelLightingLimits& limits;
    std::size_t index{};

    Words peek() const
    {
        require(index < lines.size(), "truncated record block.");
        return words(lines[index], limits);
    }

    Words next()
    {
        auto result = peek();
        ++index;
        return result;
    }

    bool at(const std::string_view first) const
    {
        return index < lines.size() && is(peek()[0], first);
    }

    Words expect(const std::string_view first, const std::size_t size)
    {
        auto result = next();
        require(result.size() == size && is(result[0], first), "missing or malformed required record.");
        return result;
    }
};

struct TokenSpan
{
    std::size_t begin{};
    std::size_t size{};
};

struct VertexSlot
{
    std::array<TokenSpan, 3> channels{};
    std::size_t channelCount{};
    ColorF original{};
};

struct MeshSlots
{
    std::vector<VertexSlot> vertices;
    std::size_t faces{};
    std::size_t instances{};
    ColorF minimumFaceExtra{};
    ColorF maximumFaceExtra{};
};

struct SourceDocument
{
    double version{};
    std::vector<std::vector<MeshSlots>> geosets;
    std::vector<std::size_t> transformVisits;
};

void validateNormalRows(Cursor& cursor, const std::size_t total)
{
    for ( std::size_t i = 0; i < total; ++i )
    {
        const auto row = cursor.next();
        require(row.size() == 4, "malformed normal row.");
        rowIndex(row[0], i);
        (void)number(row[1]);
        (void)number(row[2]);
        (void)number(row[3]);
    }
}

void updateRange(ColorF& minimum, ColorF& maximum, const ColorF& color, bool first);

ColorF parseFaceExtra(std::string_view token, const bool rgb)
{
    if ( !rgb )
    {
        const float value = colorNumber(token);
        return { value, value, value, 1.0f };
    }
    require(token.size() >= 2 && token.front() == '(' && token.back() == ')', "invalid RGB face-extra tuple.");
    token.remove_prefix(1);
    token.remove_suffix(1);
    std::size_t fields = 0;
    std::array<float, 4> channels{ 0, 0, 0, 1 };
    while ( !token.empty() )
    {
        const auto slash = token.find('/');
        const auto end = slash == std::string_view::npos ? token.size() : slash;
        require(fields < channels.size(), "invalid RGB face-extra tuple.");
        channels[fields++] = colorNumber(token.substr(0, end));
        if ( slash == std::string_view::npos )
        {
            token = {};
        }
        else
        {
            token.remove_prefix(slash + 1);
            require(!token.empty(), "invalid RGB face-extra tuple.");
        }
    }
    require(fields == 3 || fields == 4, "invalid RGB face-extra tuple.");
    return { channels[0], channels[1], channels[2], channels[3] };
}

ColorF validateFace(const Words& row, const std::size_t face, const std::size_t vertices,
    const std::size_t texVertices, const std::size_t materials, const bool rgb,
    std::size_t& triangles, const ModelLightingLimits& limits)
{
    require(row.size() >= 8, "truncated face row.");
    rowIndex(row[0], face);
    const auto material = integer(row[1]);
    require(material == -1 || (material >= 0 && static_cast<std::size_t>(material) < materials),
        "face material index is out of range.");
    require(sed::text::parseHex(row[2]).has_value(), "invalid face flags.");
    (void)integer(row[3]);
    (void)integer(row[4]);
    (void)integer(row[5]);
    const ColorF extra = parseFaceExtra(row[6], rgb);
    const std::size_t corners = count(row[7]);
    require(corners >= 3, "face needs at least three vertices.");
    addBounded(triangles, corners - 2, limits.triangles);
    std::size_t cursor = 8;
    for ( std::size_t i = 0; i < corners; ++i )
    {
        require(cursor < row.size(), "truncated face corner list.");
        const auto pair = row[cursor++];
        const auto comma = pair.find(',');
        require(comma != std::string_view::npos && comma > 0, "invalid face corner pair.");
        const auto vertex = count(pair.substr(0, comma));
        auto texture = pair.substr(comma + 1);
        if ( texture.empty() )
        {
            require(cursor < row.size(), "truncated face texture index.");
            texture = row[cursor++];
        }
        const auto textureIndex = integer(texture);
        require(vertex < vertices, "face vertex index is out of range.");
        require(textureIndex == -1 || (textureIndex >= 0
                && (static_cast<std::size_t>(textureIndex) < texVertices
                    || (texVertices == 0 && textureIndex == 0))),
            "face texture index is out of range.");
    }
    require(cursor == row.size(), "unexpected fields after face corners.");
    return extra;
}

MeshSlots parseMesh(Cursor& cursor, const std::size_t index, const std::string_view text,
    const bool rgb, const std::size_t materials, std::size_t& vertices, std::size_t& triangles)
{
    const auto header = cursor.expect("MESH", 2);
    require(count(header[1]) == index, "duplicate or out-of-order mesh index.");
    (void)cursor.expect("NAME", 2);
    bool radius = false;
    bool geometry = false;
    bool lighting = false;
    bool texture = false;
    while ( !cursor.at("VERTICES") )
    {
        const auto row = cursor.next();
        if ( is(row[0], "RADIUS") )
        {
            require(!radius && row.size() == 2, "duplicate or malformed mesh radius.");
            (void)number(row[1]);
            radius = true;
        }
        else if ( is(row[0], "GEOMETRYMODE") || is(row[0], "LIGHTINGMODE") || is(row[0], "TEXTUREMODE") )
        {
            bool& found = is(row[0], "GEOMETRYMODE") ? geometry : (is(row[0], "LIGHTINGMODE") ? lighting : texture);
            require(!found && row.size() == 2, "duplicate or malformed mesh mode.");
            (void)integer(row[1]);
            found = true;
        }
        else
        {
            // Only clearly named extension records are opaque. Structural
            // records cannot be swallowed while searching for VERTICES.
            require(!is(row[0], "MESH") && !is(row[0], "GEOSET") && !is(row[0], "MESHES")
                    && !is(row[0], "GEOSETS") && !is(row[0], "INSERT")
                    && !is(row[0], "HIERARCHY") && !is(row[0], "MATERIALS")
                    && !is(row[0], "3DO") && !is(row[0], "NAME")
                    && !is(row[0], "VERTEX") && !is(row[0], "TEXTURE")
                    && !is(row[0], "FACE") && !is(row[0], "FACES")
                    && row[0].find(':') == std::string_view::npos,
                "reserved control record in mesh properties; vertex block may be missing.");
        }
    }
    require(radius && geometry && lighting && texture, "required mesh property is missing.");
    const auto vertexHeader = cursor.expect("VERTICES", 2);
    const std::size_t vertexCount = count(vertexHeader[1]);
    addBounded(vertices, vertexCount, cursor.limits.vertices);
    require(vertexCount <= cursor.lines.size() - cursor.index, "truncated vertex block.");
    MeshSlots result;
    result.vertices.reserve(vertexCount);
    for ( std::size_t v = 0; v < vertexCount; ++v )
    {
        const auto row = cursor.next();
        require(row.size() == 8 || (!rgb && row.size() == 5), "unsupported vertex row layout.");
        rowIndex(row[0], v);
        (void)number(row[1]);
        (void)number(row[2]);
        (void)number(row[3]);
        VertexSlot slot;
        slot.channelCount = row.size() == 8 ? 3 : 1;
        slot.original.r = colorNumber(row[4]);
        slot.original.g = slot.channelCount == 3 ? colorNumber(row[5]) : slot.original.r;
        slot.original.b = slot.channelCount == 3 ? colorNumber(row[6]) : slot.original.r;
        slot.original.a = row.size() == 8 ? colorNumber(row[7]) : 1.0f;
        require(rgb || (slot.original.r == slot.original.g && slot.original.r == slot.original.b),
            "classic vertex colors must be grey.");
        for ( std::size_t channel = 0; channel < slot.channelCount; ++channel )
        {
            const auto token = row[4 + channel];
            slot.channels[channel] = { static_cast<std::size_t>(token.data() - text.data()), token.size() };
        }
        result.vertices.push_back(slot);
    }
    const auto textureHeader = cursor.expect("TEXTURE", 3);
    require(is(textureHeader[1], "VERTICES"), "texture-vertex header is missing.");
    const auto textureCount = count(textureHeader[2]);
    require(textureCount <= cursor.limits.vertices && textureCount <= cursor.lines.size() - cursor.index,
        "texture-vertex budget or row count exceeded.");
    for ( std::size_t t = 0; t < textureCount; ++t )
    {
        const auto row = cursor.next();
        require(row.size() == 3, "malformed texture-vertex row.");
        rowIndex(row[0], t);
        (void)colorNumber(row[1]);
        (void)colorNumber(row[2]);
    }
    if ( cursor.at("VERTEX") )
    {
        require(is(cursor.expect("VERTEX", 2)[1], "NORMALS"), "invalid vertex-normal header.");
        validateNormalRows(cursor, vertexCount);
    }
    const auto faceHeader = cursor.expect("FACES", 2);
    result.faces = count(faceHeader[1]);
    require(result.faces <= cursor.lines.size() - cursor.index, "truncated face block.");
    for ( std::size_t f = 0; f < result.faces; ++f )
    {
        const auto extra = validateFace(cursor.next(), f, vertexCount, textureCount, materials, rgb, triangles, cursor.limits);
        updateRange(result.minimumFaceExtra, result.maximumFaceExtra, extra, f == 0);
    }
    if ( cursor.at("FACE") )
    {
        require(is(cursor.expect("FACE", 2)[1], "NORMALS"), "invalid face-normal header.");
        validateNormalRows(cursor, result.faces);
    }
    return result;
}

SourceDocument parse(const std::string_view text, const ModelLightingLimits& limits, WorkBudget& work)
{
    const auto sections = scan(text, limits, work);
    SourceDocument result;
    bool foundVersion = false;
    for ( const auto line : sections.header )
    {
        const auto row = words(line, limits);
        if ( is(row[0], "3DO") )
        {
            require(!foundVersion && row.size() == 2, "duplicate or malformed version.");
            result.version = number(row[1]);
            foundVersion = true;
        }
    }
    require(foundVersion && (result.version == 2.1 || result.version == 2.3), "unsupported 3DO version; only 2.1 and 2.3 are admitted.");
    Cursor resources{ sections.resources, limits };
    const std::size_t materials = count(resources.expect("MATERIALS", 2)[1]);
    require(materials <= limits.vertices && materials == sections.resources.size() - resources.index,
        "material count is truncated or ambiguous.");
    for ( std::size_t i = 0; i < materials; ++i )
    {
        const auto row = resources.next();
        require(row.size() == 2, "malformed material row.");
        rowIndex(row[0], i);
    }
    Cursor geometry{ sections.geometry, limits };
    bool radius = false;
    bool offset = false;
    while ( !geometry.at("GEOSETS") )
    {
        const auto row = geometry.next();
        if ( is(row[0], "RADIUS") )
        {
            require(!radius && row.size() == 2, "duplicate or malformed model radius.");
            (void)number(row[1]);
            radius = true;
        }
        else if ( is(row[0], "INSERT") )
        {
            require(!offset && row.size() == 5 && is(row[1], "OFFSET"), "duplicate or malformed insert offset.");
            (void)number(row[2]);
            (void)number(row[3]);
            (void)number(row[4]);
            offset = true;
        }
        else
        {
            refuse("unexpected geometry header record.");
        }
    }
    require(radius && offset, "required geometry header is missing.");
    const std::size_t lodCount = count(geometry.expect("GEOSETS", 2)[1]);
    require(lodCount > 0 && lodCount <= limits.geosets, "geoset budget exceeded or empty.");
    result.geosets.reserve(lodCount);
    std::size_t meshes = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    for ( std::size_t lod = 0; lod < lodCount; ++lod )
    {
        require(count(geometry.expect("GEOSET", 2)[1]) == lod, "missing, duplicate, or out-of-order geoset.");
        const std::size_t meshCount = count(geometry.expect("MESHES", 2)[1]);
        addBounded(meshes, meshCount, limits.meshes);
        std::vector<MeshSlots> sourceMeshes;
        sourceMeshes.reserve(meshCount);
        for ( std::size_t mesh = 0; mesh < meshCount; ++mesh )
        {
            work.charge();
            sourceMeshes.push_back(parseMesh(geometry, mesh, text, result.version == 2.3,
                materials, vertices, triangles));
        }
        result.geosets.push_back(std::move(sourceMeshes));
    }
    require(geometry.index == geometry.lines.size(), "unconsumed or duplicate geometry record.");

    Cursor hierarchy{ sections.hierarchy, limits };
    const auto hierarchyHeader = hierarchy.expect("HIERARCHY", 3);
    require(is(hierarchyHeader[1], "NODES"), "missing hierarchy-node header.");
    const auto nodes = count(hierarchyHeader[2]);
    require(nodes <= limits.hierarchyNodes && nodes == sections.hierarchy.size() - hierarchy.index,
        "hierarchy budget or row count exceeded.");
    std::vector<std::int32_t> parents;
    std::vector<std::int32_t> nodeMeshes;
    parents.reserve(nodes);
    nodeMeshes.reserve(nodes);
    for ( std::size_t n = 0; n < nodes; ++n )
    {
        const auto row = hierarchy.next();
        require(row.size() == 18, "unsupported hierarchy row layout.");
        rowIndex(row[0], n);
        require(sed::text::parseHex(row[1]).has_value() && sed::text::parseHex(row[2]).has_value(),
            "invalid hierarchy flags or type.");
        const auto mesh = integer(row[3]);
        const auto parent = integer(row[4]);
        for ( std::size_t link = 4; link <= 6; ++link )
        {
            const auto value = integer(row[link]);
            require(value == -1 || (value >= 0 && static_cast<std::size_t>(value) < nodes),
                "hierarchy link is out of range.");
        }
        require(count(row[7]) <= nodes, "invalid hierarchy child count.");
        for ( std::size_t value = 8; value <= 16; ++value )
        {
            (void)number(row[value]);
        }
        parents.push_back(parent);
        nodeMeshes.push_back(mesh);
        for ( auto& geoset : result.geosets )
        {
            require(mesh == -1 || (mesh >= 0 && static_cast<std::size_t>(mesh) < geoset.size()),
                "hierarchy mesh index is out of range for a LOD.");
            if ( mesh >= 0 )
            {
                ++geoset[static_cast<std::size_t>(mesh)].instances;
            }
        }
    }
    std::vector<unsigned char> marks(nodes, 0);
    for ( std::size_t n = 0; n < nodes; ++n )
    {
        if ( marks[n] != 0 )
        {
            continue;
        }
        std::int32_t current = static_cast<std::int32_t>(n);
        while ( current != -1 && marks[static_cast<std::size_t>(current)] == 0 )
        {
            work.charge();
            marks[static_cast<std::size_t>(current)] = 1;
            current = parents[static_cast<std::size_t>(current)];
        }
        require(current == -1 || marks[static_cast<std::size_t>(current)] == 2, "cyclic hierarchy parent links.");
        current = static_cast<std::int32_t>(n);
        while ( current != -1 && marks[static_cast<std::size_t>(current)] == 1 )
        {
            marks[static_cast<std::size_t>(current)] = 2;
            current = parents[static_cast<std::size_t>(current)];
        }
    }
    if ( nodes == 0 )
    {
        for ( auto& geoset : result.geosets )
        {
            for ( auto& mesh : geoset )
            {
                mesh.instances = 1;
            }
        }
    }
    // Memoize parent depths once. The existing loader has its own hard
    // transform cap, but a caller-lowered bake budget must also charge actual
    // hierarchy evaluation across every LOD, not just cycle validation.
    std::vector<std::size_t> depths(nodes, 0);
    std::vector<std::size_t> path;
    path.reserve(nodes);
    for ( std::size_t n = 0; n < nodes; ++n )
    {
        path.clear();
        auto current = static_cast<std::int32_t>(n);
        while ( current != -1 && depths[static_cast<std::size_t>(current)] == 0 )
        {
            path.push_back(static_cast<std::size_t>(current));
            current = parents[static_cast<std::size_t>(current)];
        }
        std::size_t depth = current == -1 ? 0 : depths[static_cast<std::size_t>(current)];
        while ( !path.empty() )
        {
            depths[path.back()] = ++depth;
            path.pop_back();
        }
    }
    for ( const auto& geoset : result.geosets )
    {
        std::size_t visits = 0;
        for ( std::size_t n = 0; n < nodes; ++n )
        {
            if ( nodeMeshes[n] < 0 )
            {
                continue;
            }
            const auto& mesh = geoset[static_cast<std::size_t>(nodeMeshes[n])];
            // Both normal arrays are conservatively counted when absent.
            const std::size_t vectors = 2 * mesh.vertices.size() + mesh.faces;
            if ( visits > limits.workUnits || vectors > (limits.workUnits - visits) / depths[n] )
            {
                visits = limits.workUnits + 1;
                break;
            }
            visits += vectors * depths[n];
        }
        result.transformVisits.push_back(visits);
    }
    return result;
}

Model3do load(const std::span<const std::byte> source, const std::size_t lod,
    const bool transform, WorkBudget& work)
{
    work.charge(source.size());
    try
    {
        auto model = loadFrom3DO(source, { static_cast<std::int32_t>(lod), transform });
        require(std::isfinite(model.radius), "model bounds overflow.");
        for ( const auto& mesh : model.meshes )
        {
            require(std::isfinite(mesh.radius), "mesh bounds overflow.");
            for ( const auto& vertex : mesh.vertices )
            {
                require(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z),
                    "transformed vertex is non-finite.");
            }
            for ( const auto& normal : mesh.vertexNormals )
            {
                require(finite(normal), "transformed normal is non-finite.");
            }
        }
        return model;
    }
    catch ( const std::runtime_error& error )
    {
        if ( std::string_view(error.what()).starts_with("Model lighting: ") )
        {
            throw;
        }
        refuse(std::string("3DO parser refused input: ") + error.what());
    }
}

void updateRange(ColorF& minimum, ColorF& maximum, const ColorF& color, const bool first)
{
    if ( first )
    {
        minimum = maximum = color;
        return;
    }
    minimum.r = std::min(minimum.r, color.r);
    minimum.g = std::min(minimum.g, color.g);
    minimum.b = std::min(minimum.b, color.b);
    minimum.a = std::min(minimum.a, color.a);
    maximum.r = std::max(maximum.r, color.r);
    maximum.g = std::max(maximum.g, color.g);
    maximum.b = std::max(maximum.b, color.b);
    maximum.a = std::max(maximum.a, color.a);
}

ModelLightingInspection inspect(const std::span<const std::byte> source,
    const SourceDocument& document, WorkBudget& work)
{
    ModelLightingInspection result;
    result.version = document.version;
    result.geosets = document.geosets.size();
    for ( std::size_t lod = 0; lod < document.geosets.size(); ++lod )
    {
        const auto raw = load(source, lod, false, work);
        require(raw.meshes.size() == document.geosets[lod].size(), "source/parsed mesh identity mismatch.");
        for ( std::size_t m = 0; m < raw.meshes.size(); ++m )
        {
            work.charge();
            const auto& mesh = raw.meshes[m];
            const auto& slots = document.geosets[lod][m];
            require(mesh.vertices.size() == slots.vertices.size() && mesh.faces.size() == slots.faces,
                "source/parsed geometry identity mismatch.");
            ModelLightingMeshInspection item;
            item.geoset = lod;
            item.sourceMesh = m;
            item.name = mesh.name;
            item.vertices = mesh.vertices.size();
            item.faces = mesh.faces.size();
            item.renderInstances = slots.instances;
            item.meshLightMode = mesh.light;
            item.minimumFaceExtra = slots.minimumFaceExtra;
            item.maximumFaceExtra = slots.maximumFaceExtra;
            item.effectiveLightModes.reserve(mesh.faces.size());
            if ( slots.instances == 0 )
            {
                ++result.orphanMeshes;
            }
            if ( slots.instances > 1 )
            {
                ++result.sharedMeshes;
            }
            ++result.sourceMeshes;
            result.renderMeshes += slots.instances;
            result.vertices += mesh.vertices.size();
            for ( std::size_t v = 0; v < mesh.vertices.size(); ++v )
            {
                const auto& slot = slots.vertices[v];
                const auto color = slot.original;
                // The existing preview loader clamps scalar rows, but stored
                // diagnostics must retain signed/HDR source values. Also bind
                // every inspected/read-back color to the scanner's row, so a
                // future parser differential cannot silently select another
                // mesh with matching counts.
                const ColorF expected = slot.channelCount == 1 ? intensityToColor(color.r) : color;
                const ColorF actual = mesh.vertices[v].color;
                require(actual.r == expected.r && actual.g == expected.g
                        && actual.b == expected.b && actual.a == expected.a,
                    "source/parsed lighting identity mismatch.");
                updateRange(item.minimumVertexLight, item.maximumVertexLight, color, v == 0);
                if ( color.r >= 1 || color.g >= 1 || color.b >= 1 )
                {
                    ++item.saturatedVertices;
                }
                if ( v >= mesh.vertexNormals.size() )
                {
                    ++item.missingNormals;
                }
                else
                {
                    const double normalLength = length(mesh.vertexNormals[v]);
                    if ( normalLength == 0 )
                    {
                        ++item.zeroNormals;
                    }
                    else if ( !std::isfinite(normalLength) || std::abs(normalLength - 1.0) > 1e-5 )
                    {
                        ++item.nonUnitNormals;
                    }
                }
            }
            for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
            {
                const auto& face = mesh.faces[f];
                const int mode = std::min(mesh.light, face.light);
                item.effectiveLightModes.push_back({ mode, 1 });
            }
            std::sort(item.effectiveLightModes.begin(), item.effectiveLightModes.end(),
                [](const auto& left, const auto& right)
                {
                    return left.mode < right.mode;
                });
            std::size_t modeCount = 0;
            for ( const auto entry : item.effectiveLightModes )
            {
                if ( modeCount == 0 || item.effectiveLightModes[modeCount - 1].mode != entry.mode )
                {
                    item.effectiveLightModes[modeCount++] = entry;
                }
                else
                {
                    ++item.effectiveLightModes[modeCount - 1].faces;
                }
            }
            item.effectiveLightModes.resize(modeCount);
            result.meshes.push_back(std::move(item));
        }
    }
    return result;
}

void validateOptions(const ModelLightingOptions& options)
{
    require(options.profile == ModelLightingProfile::ClassicScalar || options.profile == ModelLightingProfile::IjimRgb,
        "unknown lighting profile.");
    require(options.blend == ModelLightingBlend::Replace || options.blend == ModelLightingBlend::Add,
        "unknown blending mode.");
    require(finite(options.ambient) && options.ambient.r >= 0 && options.ambient.g >= 0 && options.ambient.b >= 0,
        "ambient must contain finite nonnegative RGB.");
    require(std::isfinite(options.shadowBias) && options.shadowBias > 0, "shadow bias must be positive finite.");
    require(options.lights.size() <= options.limits.lights, "light-count budget exceeded.");
    const auto requireGrey = [&](const ColorF& color)
    {
        require(options.profile != ModelLightingProfile::ClassicScalar || (color.r == color.g && color.r == color.b),
            "classic profile requires grey ambient and light colors.");
    };
    requireGrey(options.ambient);
    for ( const auto& light : options.lights )
    {
        require(light.kind == ModelLightKind::Point || light.kind == ModelLightKind::Directional, "unknown light kind.");
        require(finite(light.color) && light.color.r >= 0 && light.color.g >= 0 && light.color.b >= 0
                && std::isfinite(light.intensity) && light.intensity >= 0,
            "light color/intensity must be finite and nonnegative.");
        requireGrey(light.color);
        if ( light.kind == ModelLightKind::Point )
        {
            require(finite(light.position) && std::isfinite(light.range) && light.range > 0,
                "point position/range must be finite with positive range.");
        }
        else
        {
            require(finite(light.directionToLight)
                    && (light.directionToLight.x != 0 || light.directionToLight.y != 0 || light.directionToLight.z != 0),
                "directional light needs a finite nonzero direction.");
        }
    }
}

struct Triangle
{
    TVector a;
    TVector b;
    TVector c;
};

TVector position(const Model3doVertex& vertex)
{
    return { vertex.x, vertex.y, vertex.z };
}

// Fan coverage is only a truthful opaque proxy for convex, planar, simple
// polygons. Reject concavity, crossing edges, collinearity and zero-area fans.
void appendFaceTriangles(const Model3doMesh& mesh, const Model3doFace& face,
    std::vector<Triangle>& triangles, const ModelLightingLimits& limits, WorkBudget& work)
{
    const auto n = face.vertices.size();
    const TVector anchor = position(mesh.vertices[static_cast<std::size_t>(face.vertices[0])]);
    std::vector<TVector> points;
    points.reserve(n);
    double scale = 0;
    for ( const auto index : face.vertices )
    {
        const TVector relative = position(mesh.vertices[static_cast<std::size_t>(index)]) - anchor;
        require(finite(relative), "occluder coordinates overflow.");
        scale = std::max(scale, length(relative));
        points.push_back(relative);
    }
    require(std::isfinite(scale) && scale > 0, "degenerate occluder face.");
    for ( auto& point : points )
    {
        point = { point.x / scale, point.y / scale, point.z / scale };
    }
    const TVector normal = unit(points[1].Cross(points[2]));
    require(length(normal) > 0, "degenerate occluder fan.");
    constexpr double epsilon = 1e-10;
    for ( const auto& point : points )
    {
        require(std::abs(point.Dot(normal)) <= epsilon, "non-planar occluder face.");
    }
    work.product(n, n);
    for ( std::size_t i = 0; i < n; ++i )
    {
        const TVector edge = points[(i + 1) % n] - points[i];
        require(length(edge) > epsilon, "duplicate occluder corner.");
        for ( std::size_t j = 0; j < n; ++j )
        {
            if ( j == i || j == (i + 1) % n )
            {
                continue;
            }
            const double side = edge.Cross(points[j] - points[i]).Dot(normal);
            require(side > epsilon, "occlusion requires strictly convex simple polygon fans.");
        }
    }
    require(n - 2 <= limits.triangles - triangles.size(), "triangle budget exceeded.");
    for ( std::size_t i = 1; i + 1 < n; ++i )
    {
        require(points[i].Cross(points[i + 1]).Dot(normal) > epsilon, "degenerate occluder fan.");
        triangles.push_back({ anchor,
            position(mesh.vertices[static_cast<std::size_t>(face.vertices[i])]),
            position(mesh.vertices[static_cast<std::size_t>(face.vertices[i + 1])]) });
    }
}

std::vector<Triangle> occluders(const Model3do& model, const ModelLightingLimits& limits, WorkBudget& work)
{
    std::vector<Triangle> result;
    for ( const auto& mesh : model.meshes )
    {
        for ( const auto& face : mesh.faces )
        {
            work.charge();
            if ( std::min(mesh.geo, face.geo) >= 3 )
            {
                appendFaceTriangles(mesh, face, result, limits, work);
            }
        }
    }
    return result;
}

bool blocked(const TVector& origin, const TVector& direction, const double maximumDistance,
    const double bias, const Triangle& triangle)
{
    // Normalize triangle edges and relative position by the same scale to
    // avoid world-size-dependent determinant thresholds and intermediate
    // overflow. t is recovered in source units before endpoint rejection.
    TVector edge1 = triangle.b - triangle.a;
    TVector edge2 = triangle.c - triangle.a;
    TVector relative = origin - triangle.a;
    require(finite(edge1) && finite(edge2) && finite(relative), "shadow coordinate overflow.");
    const double scale = std::max({ length(edge1), length(edge2), length(relative) });
    require(std::isfinite(scale), "shadow coordinate overflow.");
    if ( scale == 0 )
    {
        return false;
    }
    edge1 = { edge1.x / scale, edge1.y / scale, edge1.z / scale };
    edge2 = { edge2.x / scale, edge2.y / scale, edge2.z / scale };
    relative = { relative.x / scale, relative.y / scale, relative.z / scale };
    const TVector p = direction.Cross(edge2);
    const double determinant = edge1.Dot(p);
    const double edgeMagnitude = length(edge1) * length(edge2);
    if ( std::abs(determinant) <= 1e-12 * edgeMagnitude )
    {
        return false;
    }
    const double inverse = 1.0 / determinant;
    const double u = relative.Dot(p) * inverse;
    if ( u < -1e-10 || u > 1.0 + 1e-10 )
    {
        return false;
    }
    const TVector q = relative.Cross(edge1);
    const double v = direction.Dot(q) * inverse;
    if ( v < -1e-10 || u + v > 1.0 + 1e-10 )
    {
        return false;
    }
    const double distance = edge2.Dot(q) * inverse * scale;
    require(std::isfinite(distance), "shadow distance overflow.");
    return distance > bias && distance < maximumDistance - bias;
}

struct Replacement
{
    TokenSpan token;
    float value;
};

std::string floatText(const float value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general, std::numeric_limits<float>::max_digits10);
    require(result.ec == std::errc{}, "unable to format light value.");
    return { buffer.data(), result.ptr };
}

std::string publishText(const std::string_view source, std::vector<Replacement>& replacements,
    const ModelLightingLimits& limits, WorkBudget& work)
{
    std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b)
    {
        return a.token.begin < b.token.begin;
    });
    std::string result;
    result.reserve(source.size());
    std::size_t copied = 0;
    const auto append = [&](const std::string_view value)
    {
        require(value.size() <= limits.sourceBytes - result.size(), "output byte budget exceeded.");
        result.append(value);
    };
    for ( const auto& replacement : replacements )
    {
        work.charge();
        require(replacement.token.begin >= copied && replacement.token.begin <= source.size()
                && replacement.token.size <= source.size() - replacement.token.begin,
            "invalid or overlapping light token span.");
        append(source.substr(copied, replacement.token.begin - copied));
        append(floatText(replacement.value));
        copied = replacement.token.begin + replacement.token.size;
    }
    append(source.substr(copied));
    return result;
}

std::string_view view(const std::span<const std::byte> source)
{
    return { source.empty() ? "" : reinterpret_cast<const char*>(source.data()), source.size() };
}

} // namespace

ModelLightingInspection inspectModelLighting(
    const std::span<const std::byte> source, const ModelLightingLimits& limits)
{
    validateLimits(limits);
    WorkBudget work{ limits.workUnits };
    const auto document = parse(view(source), limits, work);
    return inspect(source, document, work);
}

ModelLightingBakeResult bakeModelLighting(
    const std::span<const std::byte> source, const ModelLightingOptions& options)
{
    validateLimits(options.limits);
    validateOptions(options);
    checkCancelled(&options);
    WorkBudget work{ options.limits.workUnits, &options };
    const auto document = parse(view(source), options.limits, work);
    require((document.version == 2.1 && options.profile == ModelLightingProfile::ClassicScalar)
            || (document.version == 2.3 && options.profile == ModelLightingProfile::IjimRgb),
        "profile does not match the existing 3DO version.");
    ModelLightingBakeResult result;
    result.before = inspect(source, document, work);
    require(result.before.sharedMeshes == 0, "shared source mesh instances require independent geometry before baking.");
    std::vector<Replacement> replacements;
    for ( std::size_t lod = 0; lod < document.geosets.size(); ++lod )
    {
        work.charge(document.transformVisits[lod]);
        auto model = load(source, lod, true, work);
        const auto triangles = options.selfOcclusion ? occluders(model, options.limits, work) : std::vector<Triangle>{};
        require(model.meshSourceIndices.size() == model.meshes.size(), "missing source-mesh identity.");
        for ( std::size_t m = 0; m < model.meshes.size(); ++m )
        {
            const auto sourceIndex = model.meshSourceIndices[m];
            require(sourceIndex >= 0 && static_cast<std::size_t>(sourceIndex) < document.geosets[lod].size(),
                "invalid source-mesh identity.");
            const auto& slots = document.geosets[lod][static_cast<std::size_t>(sourceIndex)];
            const auto& mesh = model.meshes[m];
            require(slots.vertices.size() == mesh.vertices.size(), "transformed vertex identity mismatch.");
            // Admit the full potential ray work, not only a lucky early hit.
            work.product(mesh.vertices.size(), options.lights.size());
            if ( options.selfOcclusion )
            {
                require(options.lights.empty() || mesh.vertices.size() <= work.remaining / options.lights.size(),
                    "shadow work budget exceeded.");
                work.product(mesh.vertices.size() * options.lights.size(), triangles.size());
            }
            for ( std::size_t v = 0; v < mesh.vertices.size(); ++v )
            {
                work.charge();
                const auto& slot = slots.vertices[v];
                const TVector point = position(mesh.vertices[v]);
                const TVector normal = v < mesh.vertexNormals.size() ? unit(mesh.vertexNormals[v]) : TVector{};
                if ( normal.x == 0 && normal.y == 0 && normal.z == 0 )
                {
                    ++result.zeroNormalVertices;
                }
                std::array<double, 3> values{ options.ambient.r, options.ambient.g, options.ambient.b };
                if ( options.blend == ModelLightingBlend::Add )
                {
                    values[0] += slot.original.r;
                    values[1] += slot.original.g;
                    values[2] += slot.original.b;
                }
                for ( const auto& light : options.lights )
                {
                    double falloff = 1;
                    double distance = std::numeric_limits<double>::infinity();
                    TVector toward;
                    if ( light.kind == ModelLightKind::Point )
                    {
                        toward = light.position - point;
                        require(finite(toward), "light/sample difference overflow.");
                        distance = length(toward);
                        require(std::isfinite(distance), "light/sample distance overflow.");
                        if ( distance == 0 || distance >= light.range )
                        {
                            continue;
                        }
                        toward = unit(toward);
                        falloff = 1.0 - distance / light.range;
                        falloff *= falloff;
                    }
                    else
                    {
                        toward = unit(light.directionToLight);
                    }
                    const double facing = std::max(0.0, normal.Dot(toward));
                    if ( facing == 0 || light.intensity == 0
                            || (light.color.r == 0 && light.color.g == 0 && light.color.b == 0) )
                    {
                        continue;
                    }
                    bool shadowed = false;
                    for ( std::size_t t = 0; t < triangles.size(); ++t )
                    {
                        if ( (t & 63u) == 0 )
                        {
                            checkCancelled(&options);
                        }
                        if ( blocked(point, toward, distance, options.shadowBias, triangles[t]) )
                        {
                            shadowed = true;
                            break;
                        }
                    }
                    if ( shadowed )
                    {
                        ++result.occludedSamples;
                        continue;
                    }
                    const double amount = facing * falloff * light.intensity;
                    values[0] += amount * light.color.r;
                    values[1] += amount * light.color.g;
                    values[2] += amount * light.color.b;
                }
                bool changed = false;
                bool clamped = false;
                const std::array original{ slot.original.r, slot.original.g, slot.original.b };
                for ( std::size_t c = 0; c < slot.channelCount; ++c )
                {
                    require(std::isfinite(values[c]), "light accumulation overflow.");
                    clamped = clamped || values[c] < 0 || values[c] > 1;
                    const float value = static_cast<float>(std::clamp(values[c], 0.0, 1.0));
                    if ( value != original[c] )
                    {
                        replacements.push_back({ slot.channels[c], value });
                        changed = true;
                    }
                }
                result.changedVertices += changed ? 1u : 0u;
                result.clampedVertices += clamped ? 1u : 0u;
            }
        }
    }
    result.text = publishText(view(source), replacements, options.limits, work);
    const auto output = std::as_bytes(std::span(result.text.data(), result.text.size()));
    const auto afterDocument = parse(view(output), options.limits, work);
    result.after = inspect(output, afterDocument, work);
    require(result.before.geosets == result.after.geosets && result.before.vertices == result.after.vertices
            && result.before.sourceMeshes == result.after.sourceMeshes, "output structural identity mismatch.");
    checkCancelled(&options);
    return result;
}

} // namespace sed::assets
