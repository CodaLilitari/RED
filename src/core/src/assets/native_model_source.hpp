#pragma once

#include "sed/assets/3do.hpp"

#include <string_view>

namespace sed::assets::detail
{
template <class Number>
struct NativeToken
{
    std::size_t begin{}, size{};
    Number value{};
};

struct NativeMeshSlots
{
    std::vector<std::array<NativeToken<double>, 3>> positions;
    std::vector<std::array<NativeToken<float>, 2>> textureVertices;
};

struct NativeModelSource
{
    std::vector<Model3do> models;
    std::vector<std::vector<NativeMeshSlots>> slots;
};

NativeModelSource readNativeModelSource(std::string_view text);
// The caller precharges both quadratic polygon checks before validation.
void validateNativeTransformedFace(const Model3doMesh& mesh, const Model3doFace& face,
    std::span<const TVector> candidates, double scale);
} // namespace sed::assets::detail
