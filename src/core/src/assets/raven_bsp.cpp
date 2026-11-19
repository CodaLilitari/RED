#include "sed/assets/raven_bsp.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sed::assets
{
namespace
{

// Raven's RBSP v1 on-disk records (OpenJK qfiles.h: dheader_t, drawVert_t,
// dsurface_t; NetRadiant-custom q3map2.h, bspDrawVert_t / bspDrawSurface_t).
// Read little-endian scalars separately from native C++ layout.
using Bytes = std::span<const std::byte>;
constexpr std::size_t kHeaderBytes = 8 + 18 * 8;
constexpr std::size_t kLightmapBytes = 128 * 128 * 3;
constexpr std::size_t kMaxTrianglesPerSurface = 65536;

[[noreturn]] void invalid(std::string_view message)
{
    throw std::runtime_error("Raven RBSP: " + std::string(message));
}

std::uint32_t readU32(Bytes bytes, std::size_t offset)
{
    std::uint32_t result{};
    for ( unsigned i = 0; i < 4; ++i )
    {
        result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return result;
}

std::int32_t readI32(Bytes bytes, std::size_t offset)
{
    return std::bit_cast<std::int32_t>(readU32(bytes, offset));
}

double readFloat(Bytes bytes, std::size_t offset)
{
    const auto value = std::bit_cast<float>(readU32(bytes, offset));
    if ( !std::isfinite(value) )
    {
        invalid("non-finite coordinate or attribute");
    }
    return value;
}

TVector readVector(Bytes bytes, std::size_t offset)
{
    return {readFloat(bytes, offset), readFloat(bytes, offset + 4), readFloat(bytes, offset + 8)};
}

bool inRange(std::int32_t first, std::int32_t count, std::size_t size)
{
    return first >= 0 && count >= 0 && static_cast<std::size_t>(first) <= size
        && static_cast<std::size_t>(count) <= size - static_cast<std::size_t>(first);
}

std::array<Bytes, 18> readDirectory(Bytes bytes)
{
    if ( bytes.size() < kHeaderBytes || bytes.size() > kRavenBspMaxBytes )
    {
        invalid("file outside supported size");
    }
    if ( readU32(bytes, 0) != 0x50534252u || readU32(bytes, 4) != 1 )
    {
        invalid("expected RBSP version 1");
    }
    std::array<Bytes, 18> lumps{};
    for ( std::size_t i = 0; i < lumps.size(); ++i )
    {
        const auto offset = readI32(bytes, 8 + i * 8);
        const auto length = readI32(bytes, 12 + i * 8);
        if ( !inRange(offset, length, bytes.size())
             || (length && static_cast<std::size_t>(offset) < kHeaderBytes) )
        {
            invalid("lump outside file or overlapping header");
        }
        const auto start = static_cast<std::size_t>(offset);
        const auto end = start + static_cast<std::size_t>(length);
        for ( std::size_t j = 0; length && j < i; ++j )
        {
            if ( !lumps[j].empty() && bytes.data() + start < lumps[j].data() + lumps[j].size()
                 && lumps[j].data() < bytes.data() + end )
            {
                invalid("overlapping non-empty lumps");
            }
        }
        lumps[i] = bytes.subspan(start, static_cast<std::size_t>(length));
    }
    return lumps;
}

std::size_t recordCount(Bytes lump, std::size_t stride, std::size_t maximum)
{
    if ( lump.size() % stride || lump.size() / stride > maximum )
    {
        invalid("partial record or excess lump count");
    }
    return lump.size() / stride;
}

class EntityReader
{
public:
    explicit EntityReader(std::string_view text) : text_(text) {}

    std::vector<RavenBspEntity> parse()
    {
        std::vector<RavenBspEntity> entities;
        std::size_t totalFields = 0;
        while ( skipSpace() )
        {
            if ( text_[position_++] != '{' || entities.size() >= 65536 )
            {
                invalid("expected entity opening brace or too many entities");
            }
            auto& entity = entities.emplace_back();
            while ( skipSpace() && text_[position_] != '}' )
            {
                if ( ++totalFields > 262144 || entity.fields.size() >= 4096 )
                {
                    invalid("too many entity fields");
                }
                auto key = token();
                if ( key.empty() || !skipSpace() )
                {
                    invalid("missing entity key or value");
                }
                entity.fields.emplace_back(std::move(key), token());
            }
            if ( position_ == text_.size() )
            {
                invalid("unterminated entity");
            }
            ++position_;
        }
        return entities;
    }

private:
    bool skipSpace()
    {
        while ( position_ < text_.size() )
        {
            const auto c = static_cast<unsigned char>(text_[position_]);
            if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' )
            {
                ++position_;
            }
            else if ( text_.substr(position_, 2) == "//" )
            {
                position_ = text_.find('\n', position_ + 2);
                if ( position_ == std::string_view::npos )
                {
                    position_ = text_.size();
                }
            }
            else if ( text_.substr(position_, 2) == "/*" )
            {
                const auto end = text_.find("*/", position_ + 2);
                if ( end == std::string_view::npos )
                {
                    invalid("unterminated entity comment");
                }
                position_ = end + 2;
            }
            else
            {
                break;
            }
        }
        return position_ < text_.size();
    }

    std::string token()
    {
        const bool quoted = text_[position_] == '"';
        if ( quoted )
        {
            ++position_;
        }
        const auto begin = position_;
        while ( position_ < text_.size() )
        {
            const auto c = static_cast<unsigned char>(text_[position_]);
            if ( c == '"' || (!quoted && (c <= ' ' || c == '{' || c == '}')) )
            {
                break;
            }
            ++position_;
        }
        const auto count = position_ - begin;
        if ( count > 65536 || (!quoted && !count) || (quoted && position_ == text_.size()) )
        {
            invalid("invalid or oversized entity token");
        }
        if ( quoted )
        {
            ++position_;
        }
        return std::string(text_.substr(begin, count));
    }

    std::string_view text_;
    std::size_t position_{};
};

void readEntities(RavenBsp& result, Bytes bytes)
{
    if ( bytes.size() > 8u * 1024u * 1024u )
    {
        invalid("entity text exceeds 8 MiB");
    }
    while ( !bytes.empty() && bytes.back() == std::byte{} )
    {
        bytes = bytes.first(bytes.size() - 1);
    }
    for ( const auto value : bytes )
    {
        const auto c = std::to_integer<unsigned char>(value);
        if ( (c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127 )
        {
            invalid("control character inside entity text");
        }
    }
    result.entityText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    result.entities = EntityReader(result.entityText).parse();
}

void validateSurface(const RavenBspSurface& surface, const RavenBsp& bsp)
{
    if ( surface.type < 1 || surface.type > 4 || !inRange(surface.shader, 1, bsp.shaders.size())
         || !inRange(surface.firstVertex, surface.vertexCount, bsp.vertices.size())
         || !inRange(surface.firstIndex, surface.indexCount, bsp.indices.size()) )
    {
        invalid("invalid surface type, shader, vertex range or index range");
    }
    if ( surface.type == 2 )
    {
        if ( surface.patchWidth < 3 || surface.patchHeight < 3 || !(surface.patchWidth % 2)
             || !(surface.patchHeight % 2)
             || static_cast<std::int64_t>(surface.patchWidth) * surface.patchHeight != surface.vertexCount )
        {
            invalid("invalid quadratic patch dimensions");
        }
    }
    if ( surface.type == 1 || surface.type == 3 )
    {
        if ( surface.indexCount % 3 || static_cast<std::size_t>(surface.indexCount / 3) > kMaxTrianglesPerSurface )
        {
            invalid("invalid indexed triangle count or surface exceeds 65536 triangles");
        }
    }
}

void validateModelOwnership(const std::vector<RavenBspModel>& models,
                            std::int32_t RavenBspModel::* first, std::int32_t RavenBspModel::* count)
{
    std::vector<std::pair<std::int32_t, std::int32_t>> spans;
    spans.reserve(models.size());
    for ( const auto& model : models )
    {
        if ( model.*count )
        {
            spans.emplace_back(model.*first, model.*first + model.*count);
        }
    }
    std::sort(spans.begin(), spans.end());
    for ( std::size_t i = 1; i < spans.size(); ++i )
    {
        if ( spans[i].first < spans[i - 1].second )
        {
            invalid("overlapping model ownership");
        }
    }
}

void validateBrushes(const std::array<Bytes, 18>& lumps, const RavenBsp& bsp)
{
    const auto planeCount = recordCount(lumps[2], 16, 4u * 1024u * 1024u);
    const auto sideCount = recordCount(lumps[9], 12, 4u * 1024u * 1024u);
    for ( std::size_t p = 0; p < lumps[8].size(); p += 12 )
    {
        if ( !inRange(readI32(lumps[8], p), readI32(lumps[8], p + 4), sideCount)
             || !inRange(readI32(lumps[8], p + 8), 1, bsp.shaders.size()) )
        {
            invalid("invalid brush side range or shader");
        }
    }
    for ( std::size_t p = 0; p < lumps[9].size(); p += 12 )
    {
        const auto surface = readI32(lumps[9], p + 8);
        if ( !inRange(readI32(lumps[9], p), 1, planeCount)
             || !inRange(readI32(lumps[9], p + 4), 1, bsp.shaders.size())
             || (surface != -1 && !inRange(surface, 1, bsp.surfaces.size())) )
        {
            invalid("invalid brush side reference");
        }
    }
}

} // namespace

RavenBsp parseRavenBsp(std::span<const std::byte> bytes)
{
    const auto lumps = readDirectory(bytes);
    RavenBsp bsp;
    const auto shaderCount = recordCount(lumps[1], 72, 65536);
    const auto modelCount = recordCount(lumps[7], 40, 65536);
    bsp.brushCount = recordCount(lumps[8], 12, 1048576);
    const auto vertexCount = recordCount(lumps[10], 80, 1048576);
    const auto indexCount = recordCount(lumps[11], 4, 8u * 1024u * 1024u);
    const auto fogCount = recordCount(lumps[12], 72, 65536);
    const auto surfaceCount = recordCount(lumps[13], 148, 524288);
    const auto lightmapCount = recordCount(lumps[14], kLightmapBytes, 2048);
    if ( !modelCount )
    {
        invalid("world model is missing");
    }
    readEntities(bsp, lumps[0]);
    bsp.shaders.reserve(shaderCount);
    for ( std::size_t i = 0; i < shaderCount; ++i )
    {
        const auto record = lumps[1].subspan(i * 72, 72);
        const auto name = record.first(64);
        const auto end = std::find(name.begin(), name.end(), std::byte{});
        if ( end == name.end() || end == name.begin()
             || std::any_of(name.begin(), end, [](auto ch) { return ch < std::byte{32} || ch == std::byte{127}; }) )
        {
            invalid("empty, unterminated or invalid shader name");
        }
        bsp.shaders.push_back({std::string(reinterpret_cast<const char*>(name.data()),
                                           static_cast<std::size_t>(end - name.begin())),
                               readU32(record, 64), readU32(record, 68)});
    }

    // Some shipped vertex-lit surfaces have uninitialized lightmap UV bytes.
    // Track invalid values by range so even shared control points are checked
    // against every surface that actually samples a lightmap.
    std::vector<std::uint32_t> invalidLightUvPrefix(vertexCount + 1);
    bsp.vertices.reserve(vertexCount);
    for ( std::size_t i = 0; i < vertexCount; ++i )
    {
        const auto v = lumps[10].subspan(i * 80, 80);
        auto channel = [&](std::size_t offset) { return std::to_integer<unsigned char>(v[offset]) / 255.0f; };
        const auto lightU = std::bit_cast<float>(readU32(v, 20));
        const auto lightV = std::bit_cast<float>(readU32(v, 24));
        const bool validLightUv = std::isfinite(lightU) && std::isfinite(lightV);
        invalidLightUvPrefix[i + 1] = invalidLightUvPrefix[i] + (validLightUv ? 0u : 1u);
        bsp.vertices.push_back({readVector(v, 0), readVector(v, 52), readFloat(v, 12), readFloat(v, 16),
                                validLightUv ? lightU : 0.f, validLightUv ? lightV : 0.f,
                                {channel(64), channel(65), channel(66), channel(67)}});
    }
    bsp.indices.reserve(indexCount);
    for ( std::size_t i = 0; i < indexCount; ++i )
    {
        bsp.indices.push_back(readI32(lumps[11], i * 4));
    }
    bsp.surfaces.reserve(surfaceCount);
    std::size_t indexVisits = 0;
    for ( std::size_t i = 0; i < surfaceCount; ++i )
    {
        const auto s = lumps[13].subspan(i * 148, 148);
        const RavenBspSurface surface{readI32(s, 0), readI32(s, 4), readI32(s, 8), readI32(s, 12),
                                      readI32(s, 16), readI32(s, 20), readI32(s, 24), readI32(s, 36),
                                      readI32(s, 140), readI32(s, 144)};
        validateSurface(surface, bsp);
        if ( surface.fog != -1 && !inRange(surface.fog, 1, fogCount) )
        {
            invalid("invalid surface fog index");
        }
        for ( std::size_t style = 0; style < 4; ++style )
        {
            const auto lm = readI32(s, 36 + 4 * style);
            if ( lm < -4 || (lm >= 0 && !inRange(lm, 1, lightmapCount)) )
            {
                invalid("invalid surface lightmap index");
            }
        }
        const auto firstVertex = static_cast<std::size_t>(surface.firstVertex);
        if ( surface.lightmapIndex >= 0
             && invalidLightUvPrefix[firstVertex] != invalidLightUvPrefix[firstVertex + static_cast<std::size_t>(surface.vertexCount)] )
        {
            invalid("non-finite coordinates on a lightmapped surface");
        }
        // Shared index spans are allowed. Bound total work even for repeated ranges.
        indexVisits += static_cast<std::size_t>(surface.indexCount);
        if ( indexVisits > 16u * 1024u * 1024u )
        {
            invalid("surface index traversal budget exceeded");
        }
        for ( int j = 0; j < surface.indexCount; ++j )
        {
            if ( !inRange(bsp.indices[static_cast<std::size_t>(surface.firstIndex + j)], 1,
                          static_cast<std::size_t>(surface.vertexCount)) )
            {
                invalid("draw index outside its local surface");
            }
        }
        bsp.surfaces.push_back(surface);
    }
    validateBrushes(lumps, bsp);
    bsp.models.reserve(modelCount);
    for ( std::size_t i = 0; i < modelCount; ++i )
    {
        const auto m = lumps[7].subspan(i * 40, 40);
        RavenBspModel model{readVector(m, 0), readVector(m, 12), readI32(m, 24), readI32(m, 28),
                            readI32(m, 32), readI32(m, 36)};
        if ( model.minimum.x > model.maximum.x || model.minimum.y > model.maximum.y || model.minimum.z > model.maximum.z
             || !inRange(model.firstSurface, model.surfaceCount, bsp.surfaces.size())
             || !inRange(model.firstBrush, model.brushCount, bsp.brushCount) )
        {
            invalid("invalid model bounds, surface range or brush range");
        }
        bsp.models.push_back(model);
    }
    validateModelOwnership(bsp.models, &RavenBspModel::firstSurface, &RavenBspModel::surfaceCount);
    validateModelOwnership(bsp.models, &RavenBspModel::firstBrush, &RavenBspModel::brushCount);
    bsp.lightmaps.resize(lumps[14].size());
    std::transform(lumps[14].begin(), lumps[14].end(), bsp.lightmaps.begin(),
                   [](auto v) { return std::to_integer<std::uint8_t>(v); });
    return bsp;
}

namespace
{

void appendTriangle(std::vector<RavenBspTriangle>& triangles, std::size_t source,
                    const RavenBspVertex& a, const RavenBspVertex& b, const RavenBspVertex& c)
{
    const auto area = b.position.sub(a.position).cross(c.position.sub(a.position));
    if ( area.x == 0 && area.y == 0 && area.z == 0 )
    {
        return;
    }
    const bool reverse = area.dot(a.normal.add(b.normal).add(c.normal)) < 0;
    triangles.push_back({{a, reverse ? c : b, reverse ? b : c}, source});
}

std::array<double, 3> basis(double t)
{
    return {(1 - t) * (1 - t), 2 * t * (1 - t), t * t};
}

RavenBspVertex evaluatePatch(const RavenBsp& bsp, std::size_t start, std::size_t stride, double u, double v)
{
    const auto bu = basis(u);
    const auto bv = basis(v);
    RavenBspVertex result;
    std::array<double, 4> color{};
    for ( std::size_t y = 0; y < 3; ++y )
    {
        for ( std::size_t x = 0; x < 3; ++x )
        {
            const auto& control = bsp.vertices[start + y * stride + x];
            const auto weight = bu[x] * bv[y];
            result.position = result.position.add(control.position.scaled(weight));
            result.normal = result.normal.add(control.normal.scaled(weight));
            result.u += weight * control.u;
            result.v += weight * control.v;
            result.lightmapU += weight * control.lightmapU;
            result.lightmapV += weight * control.lightmapV;
            color[0] += weight * control.color.r;
            color[1] += weight * control.color.g;
            color[2] += weight * control.color.b;
            color[3] += weight * control.color.a;
        }
    }
    result.color = {static_cast<float>(color[0]), static_cast<float>(color[1]),
                    static_cast<float>(color[2]), static_cast<float>(color[3])};
    return result;
}

} // namespace

std::vector<RavenBspTriangle> ravenBspSurfaceTriangles(const RavenBsp& bsp, std::size_t source,
                                                       std::uint32_t patchSubdivisions)
{
    if ( source >= bsp.surfaces.size() || patchSubdivisions < 1 || patchSubdivisions > 16 )
    {
        invalid("surface index or patch subdivisions out of range");
    }
    const auto& surface = bsp.surfaces[source];
    validateSurface(surface, bsp);
    std::vector<RavenBspTriangle> triangles;
    if ( surface.type == 4 )
    {
        return triangles;
    }
    if ( surface.type != 2 )
    {
        triangles.reserve(static_cast<std::size_t>(surface.indexCount) / 3);
        const auto corner = [&](int i) -> const RavenBspVertex&
        {
            const auto local = bsp.indices[static_cast<std::size_t>(surface.firstIndex + i)];
            if ( !inRange(local, 1, static_cast<std::size_t>(surface.vertexCount)) )
            {
                invalid("draw index outside its local surface");
            }
            return bsp.vertices[static_cast<std::size_t>(surface.firstVertex + local)];
        };
        for ( int i = 0; i < surface.indexCount; i += 3 )
        {
            appendTriangle(triangles, source, corner(i), corner(i + 1), corner(i + 2));
        }
        return triangles;
    }

    const auto width = static_cast<std::size_t>(surface.patchWidth);
    const auto height = static_cast<std::size_t>(surface.patchHeight);
    const auto steps = static_cast<std::size_t>(patchSubdivisions);
    const auto blocks = (width / 2) * (height / 2);
    const auto trianglesPerBlock = 2 * steps * steps;
    if ( blocks > kMaxTrianglesPerSurface / trianglesPerBlock )
    {
        invalid("patch subdivision exceeds 65536 triangles per surface");
    }
    triangles.reserve(blocks * trianglesPerBlock);
    const auto side = steps + 1;
    std::vector<RavenBspVertex> samples(side * side);
    for ( std::size_t by = 0; by + 2 < height; by += 2 )
    {
        for ( std::size_t bx = 0; bx + 2 < width; bx += 2 )
        {
            const auto start = static_cast<std::size_t>(surface.firstVertex) + by * width + bx;
            for ( std::size_t y = 0; y <= steps; ++y )
            {
                for ( std::size_t x = 0; x <= steps; ++x )
                {
                    samples[y * side + x] = evaluatePatch(bsp, start, width, static_cast<double>(x) / patchSubdivisions,
                                                          static_cast<double>(y) / patchSubdivisions);
                }
            }
            for ( std::size_t y = 0; y < steps; ++y )
            {
                for ( std::size_t x = 0; x < steps; ++x )
                {
                    const auto i = y * side + x;
                    appendTriangle(triangles, source, samples[i], samples[i + 1], samples[i + side + 1]);
                    appendTriangle(triangles, source, samples[i], samples[i + side + 1], samples[i + side]);
                }
            }
        }
    }
    return triangles;
}

} // namespace sed::assets
