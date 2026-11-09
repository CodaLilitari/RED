#pragma once

#include "../../src/core/src/assets/atomic_file_write.hpp"

#include <filesystem>
#include <functional>
#include <span>
#include <string_view>

namespace sed_agent::detail
{

// Throws on staging, write, flush, close, or publication failure. The caller
// translates that failure into the agent's refusal envelope. Existing outputs
// are never removed, truncated, or replaced, including after a caller preflight.
inline void createNewOutputFile(const std::filesystem::path& path, const std::string_view bytes,
    const std::function<void()>& beforePublish = {})
{
    const auto destination = std::filesystem::absolute(path);
    sed::assets::detail::StagedAssetFile staged(destination);
    staged.writeAndClose(std::as_bytes(std::span(bytes.data(), bytes.size())));
    if (beforePublish) beforePublish();
    staged.publishNew(destination);
}

} // namespace sed_agent::detail
