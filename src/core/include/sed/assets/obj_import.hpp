#pragma once

#include "sed/assets/3do.hpp"
#include "sed/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed::assets
{

// Model import: a game-ready mesh (Wavefront OBJ text or binary glTF 2.0)
// becomes a JK 3DO 2.1 model. The Obj* names predate glTF input and serve
// both formats.

/** The source axis that points up. JK is z-up and right-handed. */
enum class ObjUpAxis : std::uint8_t
{
    Y, // the OBJ and glTF convention: JK (x, y, z) = source (x, -z, y), a rotation, so handedness is kept
    Z, // already z-up: coordinates are kept
};

/** Where the imported model's origin is placed. */
enum class ObjOrigin : std::uint8_t
{
    Base,   // bottom centre of the bounding box at (0, 0, 0)
    Center, // centre of the bounding box at (0, 0, 0)
    None,   // coordinates kept after the axis change and scale
};

/** How corner normals and smoothing boundaries become 3DO vertex normals. */
enum class ObjNormalPolicy : std::uint8_t
{
    Preserve,    // keep authored normals and OBJ smoothing boundaries, splitting vertices when needed
    Recalculate, // ignore authored normals/groups and average geometric face areas at shared positions
};

/** The source formats model import reads. */
enum class ModelSourceFormat : std::uint8_t
{
    Obj, // Wavefront OBJ text
    Glb, // binary glTF 2.0 (.glb), recognised by its "glTF" magic
};

/** One source material and the MAT the 3DO lists for it. */
struct ObjImportMaterial
{
    // OBJ: the `usemtl` name. glTF: the material's name, or "#<index>" when
    // it has none. Matched exactly (case-sensitive).
    std::string name;
    // The MAT file name the 3DO lists, e.g. "relay.mat".
    std::string mat;
    // Texture size in texels: OBJ (u, v) becomes (u * width, (1 - v) * height);
    // glTF, whose v runs down the image, (u * width, v * height).
    std::int32_t width{};
    std::int32_t height{};
};

struct ObjImportOptions
{
    std::vector<ObjImportMaterial> materials{};
    // Uniform scale applied after the axis change; finite, > 0 and <= 1e6.
    double scale{ 1.0 };
    ObjUpAxis upAxis{ ObjUpAxis::Y };
    ObjOrigin origin{ ObjOrigin::Base };
    ObjNormalPolicy normalPolicy{ ObjNormalPolicy::Preserve };
    // Optional prefix for mesh (and hierarchy node) names.
    std::string meshNamePrefix{};
    // Faces without a material are refused unless this is set. They are then
    // written with no material (-1) and geometry mode 0: JK draws no face that
    // has no material, so such faces only collide.
    bool allowUntextured{ false };
    // A source mesh over a JK mesh limit is split into several meshes that
    // each fit (connected faces kept together where possible). When false it
    // is refused instead.
    bool split{ true };
};

// OBJ input bounds. A larger or longer source is refused before it is parsed.
inline constexpr std::size_t kObjImportMaxSourceBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kObjImportMaxLines = 1024u * 1024u;
inline constexpr std::size_t kObjImportMaxLineBytes = 64u * 1024u;

// Binary glTF input bounds: the whole file, its JSON chunk, and each buffer
// or image embedded as a base64 data: URI (decoded size).
inline constexpr std::size_t kGlbImportMaxSourceBytes = 64u * 1024u * 1024u;
inline constexpr std::size_t kGlbImportMaxJsonBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kGlbImportMaxDataUriBytes = 4u * 1024u * 1024u;

// JK limits enforced per model, mesh and face (the engine's 3DO loader
// refuses more faces or vertices per mesh and more corners per face).
inline constexpr std::size_t kJkModelMaxMeshes = 64;
inline constexpr std::size_t kJkMeshMaxFaces = 512;
inline constexpr std::size_t kJkMeshMaxVertices = 512;
inline constexpr std::size_t kJkMeshMaxTextureVertices = 768;
inline constexpr std::size_t kJkFaceMaxVertices = 24;
// Mesh, node and material names are kept in 32-byte engine fields.
inline constexpr std::size_t kJkModelNameMaxLength = 31;

enum class ObjImportErrorKind : std::uint8_t
{
    InvalidOptions, // the request itself is wrong
    Malformed,      // the source is not valid OBJ text or glTF, or is too large
    Refused,        // a valid source that JK cannot carry, or that would need a guess
};

class ObjImportError : public std::runtime_error
{
public:
    ObjImportError(ObjImportErrorKind kind, std::string code, const std::string& message);

    ObjImportErrorKind kind() const noexcept;
    // A stable machine-readable code such as "objMaterialUnmapped" or
    // "gltfUnsupportedFeature". Checks shared by both formats keep their
    // "obj" codes.
    const std::string& code() const noexcept;

private:
    ObjImportErrorKind kind_;
    std::string code_;
};

struct ObjImportMeshReport
{
    std::string name;
    // The OBJ object/group or glTF node name the mesh came from (empty when unnamed).
    std::string source;
    std::size_t vertices{};
    std::size_t textureVertices{};
    std::size_t faces{};
    std::size_t triangulatedPolygons{};
    double radius{};
    // The mesh is piece `part` of `parts` its source mesh was split into
    // (1 of 1 when it was not split).
    std::size_t part{ 1 };
    std::size_t parts{ 1 };
};

/** A source mesh over a JK mesh limit, and the meshes it was split into. */
struct ObjImportSplitReport
{
    std::string source;
    // What the source mesh would need as one JK mesh.
    std::size_t faces{};
    std::size_t vertices{};
    std::size_t textureVertices{};
    std::vector<std::string> meshes{};
};

struct ObjImportMaterialReport
{
    std::string name;
    std::string mat;
    // The 3DO material index; materials that share a MAT share an index.
    std::int32_t index{};
    std::int32_t width{};
    std::int32_t height{};
    std::size_t faces{};
};

/** A glTF image that a used material takes its base colour from. */
struct GltfBaseColorImage
{
    std::size_t image{};                // the glTF image index
    std::string name;                   // the image's name, if any
    std::vector<std::string> materials; // the used materials (names or "#<index>") that show it
    std::string mimeType;               // "image/jpeg" or "image/png" when the bytes are available
    std::vector<std::uint8_t> bytes;    // the image file exactly as stored in the glTF
    // Why the bytes are not available (an external file, another image
    // format); empty when they are.
    std::string problem;
};

struct GltfImportReport
{
    // Scene nodes with a mesh; each becomes one source mesh.
    std::size_t nodes{};
    std::size_t primitives{};
    // Vertices the primitives hold, and distinct positions before normal
    // discontinuities and mesh partitions introduce additional 3DO vertices.
    std::size_t sourceVertices{};
    std::size_t uniquePositions{};
    std::size_t sourceTriangles{};
    // Nodes whose transform mirrors; their triangles' winding is reversed.
    std::size_t mirroredNodes{};
    // Extensions, attributes and material maps read but not imported.
    std::vector<std::pair<std::string, std::size_t>> ignored{};
    std::vector<GltfBaseColorImage> baseColorImages{};
};

struct ObjImportResult
{
    ModelSourceFormat format{ ModelSourceFormat::Obj };
    // A 3DO 2.1 model ready for saveTo3DO: one mesh per OBJ object/group or
    // glTF node (or per piece of one that was split) and one hierarchy node
    // (type 0, identity transform) per mesh. The first mesh's node is the
    // only root and the others are its children, because the engine draws a
    // model from node 0 down.
    Model3do model{};
    std::vector<ObjImportMeshReport> meshes{};
    std::vector<ObjImportSplitReport> splits{};
    // Used materials, in request order.
    std::vector<ObjImportMaterialReport> materials{};
    // Request materials that no face uses (they are not listed by the 3DO).
    std::vector<std::string> unusedMaterials{};
    std::size_t polygons{};
    std::size_t faces{};
    std::size_t vertices{};
    std::size_t textureVertices{};
    std::size_t triangulatedPolygons{};
    std::size_t untexturedFaces{};
    // Extra geometric vertices required by preserved normals/smoothing
    // boundaries, counted before mesh partitioning duplicates seam vertices.
    std::size_t normalSplitVertices{};
    // glTF triangles without area, dropped (an OBJ face without area is refused).
    std::size_t degenerateTriangles{};
    TVector boundsMin{};
    TVector boundsMax{};
    // OBJ statements read but not used (mtllib, unknown
    // keywords...), by keyword in first-seen order.
    std::vector<std::pair<std::string, std::size_t>> ignoredStatements{};
    GltfImportReport gltf{};
};

/**
 * Converts Wavefront OBJ text into a JK 3DO 2.1 model.
 *
 * Supported: v (x y z, optional w = 1), vt (u [v [w = 0]]), vn, f with
 * v, v/vt, v//vn and v/vt/vn corners (negative indices count back from the
 * last element), o, g (one name), usemtl, s (off/0/on/positive integer), and
 * line continuations. Preserve uses normalized authored normals where present;
 * otherwise it averages within each smoothing group, keeps s off faces flat,
 * and retains smooth area-weighted fallback where no smoothing hint exists.
 * Geometric vertices split at normal discontinuities; a UV seam alone does
 * not split them. Recalculate ignores normals/groups and computes geometric
 * area-weighted normals. mtllib and unknown statements are ignored and reported
 * (s is also reported when Recalculate ignores it). Free-form geometry, lines,
 * points, vertex colours, several groups on one face, call/csh and
 * usemap/maplib are refused rather than guessed.
 *
 * Each object/group becomes one mesh, split into several when it exceeds a
 * JK mesh limit (unless options.split is false). Texture coordinates become
 * texels of the face's material size. Planar convex polygons are kept;
 * non-planar or non-convex polygons are triangulated by ear clipping. Face
 * normals are Newell normals; preserved authored normals take precedence over
 * smoothing groups. Zero authored normals use the geometric fallback. Splitting
 * for normal discontinuities happens before JK mesh-limit checks. Positions
 * and normals are rounded to the float values the engine reads.
 * Throws ObjImportError naming the line, material or
 * mesh and, for JK limits, telling the caller to reduce the model first.
 */
ObjImportResult importObjModel(std::string_view objText, const ObjImportOptions& options);

/** True when `bytes` start with the binary glTF magic "glTF". */
bool isGlbModel(std::span<const std::byte> bytes) noexcept;

/**
 * Converts a binary glTF 2.0 file into a JK 3DO 2.1 model.
 *
 * The JSON and BIN chunks are read with every size, count, offset and stride
 * checked against the file. The default scene (or scene 0, or every root
 * node) is walked from its roots; each node's TRS or matrix transform is
 * applied to positions and (by its inverse transpose) to normals, and a node
 * that mirrors has its triangles' winding reversed. Every node with a mesh
 * becomes one source mesh. Triangles, triangle strips and fans are read with
 * float POSITION, NORMAL and TEXCOORD_0 and 8/16/32-bit or no indices.
 * Preserve shares a position only when its authored normal agrees (the 3DO
 * keeps texture vertices apart); normal discontinuities split geometric
 * vertices. Missing/zero normals use area-weighted geometric fallback.
 * Recalculate discards authored normals and smooths shared positions.
 * Primitives map to the request's materials by material name, or "#<index>"
 * for a material without one. Triangles without
 * area are dropped and counted. The base colour image of each used material
 * is returned as stored.
 *
 * Refused: sparse accessors, Draco or meshopt compression, any extension the
 * file lists as required, points and lines, a missing POSITION, non-float
 * POSITION, NORMAL or TEXCOORD_0, base colour mapped through another texture
 * coordinate set or KHR_texture_transform, external buffers, data: URIs over
 * kGlbImportMaxDataUriBytes and files over the bounds above. JK limits and
 * placement are those of importObjModel.
 */
ObjImportResult importGlbModel(std::span<const std::byte> glb, const ObjImportOptions& options);

/** Imports `bytes` as binary glTF when isGlbModel says so, otherwise as OBJ text. */
ObjImportResult importModel(std::string_view bytes, const ObjImportOptions& options);

} // namespace sed::assets
