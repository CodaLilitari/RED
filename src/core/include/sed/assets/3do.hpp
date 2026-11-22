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
    // First named extension section skipped by the reader. Generic export
    // cannot reconstruct its records; source-preserving operations use the input bytes.
    std::string unrepresentedSection;
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

// Validate the complete indexed 3DO source before loading transformed LOD0
// for geometric measurement. Throws on unsupported layouts and budget exhaustion.
Model3do loadCheckedModel3doBindPose(std::span<const std::byte> source);

/**
 * Jedi Knight's model loader (rdModel3_Load in JK.EXE, as OpenJKDF2 documents
 * it) refuses a whole 3DO when one mesh has more than 512 vertices, 768
 * texture vertices or 512 faces, or a face has more than 24 corners. A Thing
 * using such a model draws nothing in the game.
 */
inline constexpr std::size_t kModel3doMaxMeshVertices = 512;
inline constexpr std::size_t kModel3doMaxMeshTextureVertices = 768;
inline constexpr std::size_t kModel3doMaxMeshFaces = 512;
inline constexpr std::size_t kModel3doMaxFaceVertices = 24;

/** One way a mesh exceeds those limits. `what` is "vertices",
 * "textureVertices", "faces" or "faceVertices" (the most corners on one face). */
struct Model3doEngineLimitIssue
{
    std::size_t mesh{};
    std::string meshName{};
    std::string what{};
    std::size_t count{};
    std::size_t limit{};
};

/** Every limit each mesh of a model loaded with
 * `applyHierarchyTransforms = false` exceeds, in mesh order. */
std::vector<Model3doEngineLimitIssue> model3doEngineLimitIssues(const Model3do& model);

struct Model3doSaveOptions
{
    // Use the original single intensity column for 2.1 vertices. This
    // interoperates with strict 3DO readers and refuses non-opaque vertices,
    // since that syntax cannot preserve alpha. The default retains the
    // existing RGBA extension; 2.3 output is unaffected.
    bool scalarVersion21Vertices{ false };
};

/**
 * Serializes a model as 3DO text (one geoset). The only supported write
 * versions are 2.1 and 2.3. At 2.1 a grey vertex colour (r == g == b) is
 * written through the intensity column; any other vertex colour is refused
 * instead of being approximated. By default vertex rows also retain the RGBA
 * extension; scalarVersion21Vertices selects the original single-column 2.1
 * syntax and requires opaque vertices. At 2.3 face extra light is written in the
 * `(r/g/b/a)` form, while at 2.1 it must be grey. Throws `std::runtime_error`
 * naming the refused case instead of dropping data: unsupported write version,
 * a named source extension section, more than one declared geoset, hierarchy-transformed geometry (load with
 * `applyHierarchyTransforms = false` to save), unsupported hierarchy node
 * type (only 0 and 1 are written), invalid hierarchy mesh/parent links,
 * mesh references that only exist after hierarchy expansion, non-finite
 * values, and name tokens that the text format cannot represent.
 *
 * Every mesh gets the sections the engine's loader requires, even when they
 * are empty: "TEXTURE VERTICES 0" for a mesh without texture vertices, and a
 * VERTEX NORMALS and a FACE NORMALS section with one row per vertex and face.
 * Normals a mesh does not hold are derived from its faces (unit Newell face
 * normals; area-weighted vertex normals, zero for a vertex no face uses).
 */
std::string saveTo3DO(const Model3do& model, const Model3doSaveOptions& options = {});
void saveTo3DO(const Model3do& model, std::ostream& out, const Model3doSaveOptions& options = {});
void saveTo3DO(const Model3do& model, const std::filesystem::path& path, const Model3doSaveOptions& options = {});

} // namespace sed::assets
