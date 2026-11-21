#pragma once

#include "sed/assets/3do.hpp"

#include <memory>
#include <string_view>

namespace sed::assets
{

// Source ordinals and classic 2.1 texture pixel coordinates.
struct NativeTextureVertexEdit
{
    std::size_t geoset{}, sourceMesh{}, textureVertex{};
    float u{}, v{};
};

// Owns an immutable source snapshot and its independent raw geosets.
class NativeModelDocument
{
public:
    static NativeModelDocument open(std::string_view source);
    std::string_view source() const noexcept;
    std::span<const Model3do> geosets() const noexcept;
    NativeModelDocument applyTextureVertexEdits(std::span<const NativeTextureVertexEdit> edits) const;

private:
    struct Data;
    explicit NativeModelDocument(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};

} // namespace sed::assets
