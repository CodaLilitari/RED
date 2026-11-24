#pragma once

#include "sed/assets/3do.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

// Source ordinals and classic 2.1 texture pixel coordinates.
struct NativeTextureVertexEdit
{
    std::size_t geoset{}, sourceMesh{}, textureVertex{};
    float u{}, v{};
};

// Positive uniform scale about the mesh-local origin, followed by translation.
struct NativeMeshTransform
{
    std::size_t geoset{}, sourceMesh{};
    double scale{ 1.0 };
    TVector translation{};
};

// A new name for one entry of the model's material table (MODELRESOURCE), by its ordinal. Every
// face that names that entry draws the new MAT; nothing else in the file changes.
struct NativeMaterialRename
{
    std::size_t material{};
    std::string name;
};

// Owns an immutable source snapshot and its independent raw geosets.
class NativeModelDocument
{
public:
    static NativeModelDocument open(std::string_view source);
    std::string_view source() const noexcept;
    std::span<const Model3do> geosets() const noexcept;
    NativeModelDocument applyTextureVertexEdits(std::span<const NativeTextureVertexEdit> edits) const;
    NativeModelDocument applyMeshTransforms(std::span<const NativeMeshTransform> transforms) const;
    // Replaces only the named table entries' bytes. A name is 1 to 31 printable ASCII bytes ending in
    // .mat, without spaces, ':' or '#', and may not repeat another entry's name.
    NativeModelDocument applyMaterialRenames(std::span<const NativeMaterialRename> renames) const;

private:
    struct Data;
    explicit NativeModelDocument(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};

// Every geoset of a 3DO as the ordinary reader reads it: raw meshes (no hierarchy transforms), with the source's
// tables and faces audited. It admits files native editing refuses, such as a 2.1 model whose vertex rows carry
// light in the RGBA form, which RED's own 3DO writer and the Blender Sith add-on write by default.
std::vector<Model3do> readModelGeosets(std::string_view source);

// The material rename for any 3DO readModelGeosets reads. Only the named table entries' bytes change, and every
// geoset must read back with the new table and the same meshes. Names follow applyMaterialRenames. Returns the
// source unchanged when every entry already has its name.
std::string renameModelMaterials(std::string_view source, std::span<const NativeMaterialRename> renames);

} // namespace sed::assets
