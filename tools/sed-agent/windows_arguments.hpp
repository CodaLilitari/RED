#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sed_agent
{

// A narrow CRT main need not initialize __wargv. Decode the original command
// line with the same system parser used by the GUI installation checks.
inline std::optional<std::vector<std::wstring>> windowsArguments()
{
    int count{};
    const std::unique_ptr<wchar_t*, decltype(&::LocalFree)> words(
        ::CommandLineToArgvW(::GetCommandLineW(), &count), &::LocalFree);
    if ( !words || count < 1 )
    {
        return std::nullopt;
    }
    return std::vector<std::wstring>(words.get(), words.get() + count);
}

} // namespace sed_agent
#endif
