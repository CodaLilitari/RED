#pragma once

#include "agent_json.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace sed_agent
{

inline bool isUtf8PathText(std::string_view text)
{
    return text.find('\0') == std::string_view::npos && isValidJsonUtf8(text);
}

// Callers validate wire strings before conversion. char8_t selects the UTF-8
// path constructor on Windows instead of the current ANSI filesystem code page.
inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    return { bytes.begin(), bytes.end() };
}

inline std::string pathForDiagnostic(const std::filesystem::path& path)
{
    try
    {
        auto text = pathToUtf8(path);
        if ( isUtf8PathText(text) )
        {
            return text;
        }
    }
    catch ( const std::system_error& )
    {
        // A native filename may contain an unpaired UTF-16 surrogate.
    }
    return "<unrepresentable path>";
}

} // namespace sed_agent
