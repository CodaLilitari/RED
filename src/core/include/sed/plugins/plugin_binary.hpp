#pragma once

#include <cstddef>
#include <span>

namespace sed::plugins
{

enum class PluginBinaryArchitecture
{
    unknown,
    x86,
    x64,
    arm64,
};

/**
 * Reads the machine type of a Windows PE image (DLL or EXE) from its leading
 * bytes. Returns `unknown` for non-PE data, truncated headers, and machines
 * RED does not ship for. Only the first few kilobytes are needed.
 */
PluginBinaryArchitecture detectPeArchitecture(std::span<const std::byte> image) noexcept;

/** Designer-facing name, e.g. "32-bit x86". */
const char* pluginArchitectureName(PluginBinaryArchitecture architecture) noexcept;

} // namespace sed::plugins
