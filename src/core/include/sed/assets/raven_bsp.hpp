#pragma once

#include "sed/color.hpp"
#include "sed/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sed::assets
{

inline constexpr std::size_t kRavenBspMaxBytes = 128u * 1024u * 1024u;

struct RavenBspShader
{
    std::string name;
    std::uint32_t surfaceFlags{}, contents{};
};

struct RavenBspVertex
{
    TVector position{}, normal{};
    double u{}, v{}, lightmapU{}, lightmapV{};
    ColorF color{};
};

struct RavenBspSurface
{
    std::int32_t shader{}, fog{}, type{}, firstVertex{}, vertexCount{}, firstIndex{}, indexCount{};
    std::int32_t lightmapIndex{}, patchWidth{}, patchHeight{};
};

struct RavenBspModel
{
    TVector minimum{}, maximum{};
    std::int32_t firstSurface{}, surfaceCount{}, firstBrush{}, brushCount{};
};

struct RavenBspEntity
{
    // Preserve source order and repeated keys for downstream diagnostics.
    std::vector<std::pair<std::string, std::string>> fields;
};

struct RavenBsp
{
    std::vector<RavenBspShader> shaders;
    std::vector<RavenBspVertex> vertices;
    std::vector<std::int32_t> indices;
    std::vector<RavenBspSurface> surfaces;
    std::vector<RavenBspModel> models;
    std::vector<RavenBspEntity> entities;
    std::vector<std::uint8_t> lightmaps;
    std::size_t brushCount{};
    std::string entityText;
};

struct RavenBspTriangle
{
    std::array<RavenBspVertex, 3> corners;
    std::size_t surface{};
};

// RBSP v1 only. Source coordinates remain Z-up, in original game units.
// Throws std::runtime_error for malformed or unsupported sources.
RavenBsp parseRavenBsp(std::span<const std::byte> bytes);

// Tessellate one draw surface without combining independent model transforms.
// patchSubdivisions must be in [1, 16]. Flares produce no triangles.
std::vector<RavenBspTriangle> ravenBspSurfaceTriangles(const RavenBsp& bsp, std::size_t surface,
                                                       std::uint32_t patchSubdivisions);

} // namespace sed::assets
