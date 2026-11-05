#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <span>
#include <string>
#include <vector>

#include "sed/color.hpp"
#include "sed/geometry.hpp"

namespace sed::assets
{

struct Model3doVertex
{
    double x{};
    double y{};
    double z{};
    ColorF color{ ColorWhite };
};

struct Model3doTexVertex
{
    // Preserve the model's wire units: 3DO 2.1 uses texture pixels and 2.3
    // uses normalized coordinates. Consumers must inspect Model3do::version
    // before converting these values for a Level or renderer.
    float u{};
    float v{};
};

struct Model3doFace
{
    std::int32_t material{};
    std::uint32_t faceFlags{};
    std::int32_t geo{};
    std::int32_t light{};
    std::int32_t tex{};
    ColorF extraLight{ ColorWhite };

    std::vector<std::int32_t> vertices;
    std::vector<std::int32_t> texVertices;
};

struct Model3doMesh
{
    std::string name;
    // The 3DO MESH RADIUS value consumed by MotS tree collision. `radius`
    // remains the conservative renderer/bounds radius, which can expand after
    // hierarchy transforms are applied.
    double authoredRadius{};
    double radius{};
    std::int32_t geo{};
    std::int32_t light{};
    std::int32_t tex{};

    std::vector<Model3doVertex> vertices;
    std::vector<Model3doTexVertex> texVertices;
    std::vector<TVector> vertexNormals;
    std::vector<Model3doFace> faces;
    std::vector<TVector> faceNormals;
};

struct Model3doNode
{
    std::int32_t id{};
    // MotS uses hierarchy flag bit 0x2 when applying inherited mesh radii
    // during rendering; keep it distinct from the node type field.
    std::uint32_t type{};
    std::uint32_t flags{};
    std::int32_t mesh{-1};
    std::int32_t parent{-1};

    TVector position{};
    TVector rotation{};
    TVector pivot{};

    std::string name;
};

struct Model3do
{
    double version{};
    double radius{};
    // Declared GEOSETS count, retained even though one load parses only one geoset.
    std::size_t geosetCount{};
    TVector insertOffset{};

    std::vector<std::string> materials;
    std::vector<Model3doMesh> meshes;
    // Archive lighting records use authored/source mesh ordinals.  The normal
    // loader may append transformed instances when hierarchy nodes share one
    // source mesh, so this parallel table maps every published render mesh
    // back to the source ordinal consumed by ARCHLIGHTING records.
    std::vector<std::int32_t> meshSourceIndices;
    std::vector<Model3doNode> nodes;

    // Default loads publish hierarchy-transformed vertices only for meshes
    // referenced by at least one node, compacting unused source meshes while
    // preserving node and meshSourceIndices alignment. Importers that need
    // every authored source mesh, including orphans, request raw meshes and
    // apply each node transform to its own copy.
    bool hierarchyTransformsApplied{ true };
};

struct Model3doLoadOptions
{
    std::int32_t lod{ 0 };
    bool applyHierarchyTransforms{ true };
};

// Match the classic Sith renderer's geoset branches using Thing-origin depth
// along the camera forward axis. The caller applies the MotS inverse zoom scale
// to that depth. Equal thresholds choose the farther geoset; header slot 4 is
// unused. Invalid depth or a required invalid threshold safely selects geoset 0.
std::size_t selectModel3doLodIndex(
    std::size_t geosetCount,
    const std::array<double, 4>& lodDistances,
    double cameraForwardDepth) noexcept;

Model3do loadFrom3DO(std::istream& in, const Model3doLoadOptions& options = {});
Model3do loadFrom3DO(std::span<const std::byte> bytes, const Model3doLoadOptions& options = {});
Model3do loadFrom3DO(const std::filesystem::path& path, const Model3doLoadOptions& options = {});

/**
 * Serializes a model as 3DO text (one geoset). The only supported write
 * versions are 2.1 and 2.3. At 2.1 a grey vertex colour (r == g == b) is
 * written through the intensity column; any other vertex colour is refused
 * instead of being approximated. At 2.3 face extra light is written in the
 * `(r/g/b/a)` form, while at 2.1 it must be grey. Throws `std::runtime_error`
 * naming the refused case instead of dropping data: unsupported write version,
 * more than one declared geoset, hierarchy-transformed geometry (load with
 * `applyHierarchyTransforms = false` to save), unsupported hierarchy node
 * type (only 0 and 1 are written), invalid hierarchy mesh/parent links,
 * mesh references that only exist after hierarchy expansion, non-finite
 * values, and name tokens that the text format cannot represent.
 */
std::string saveTo3DO(const Model3do& model);
void saveTo3DO(const Model3do& model, std::ostream& out);
void saveTo3DO(const Model3do& model, const std::filesystem::path& path);

} // namespace sed::assets
