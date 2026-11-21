#pragma once

#include "sed/assets/3do.hpp"

#include <string_view>

namespace sed::assets::detail
{
struct NativeUvToken
{
    std::size_t begin{}, size{};
    float value{};
};

using NativeMeshUvSlots = std::vector<std::array<NativeUvToken, 2>>;

struct NativeModelSource
{
    std::vector<Model3do> models;
    std::vector<std::vector<NativeMeshUvSlots>> slots;
};

NativeModelSource readNativeModelSource(std::string_view text);
} // namespace sed::assets::detail
