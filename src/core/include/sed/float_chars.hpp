#pragma once

#include <charconv>
#include <version>

// libc++ before LLVM 20, which Apple toolchains ship, declares no
// floating-point std::from_chars overloads. Standard libraries that provide
// them define __cpp_lib_to_chars; Windows builds always use the standard
// overloads, so the fallback is not compiled there.
#if (defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L) || defined(_WIN32)
#define SED_HAS_FLOAT_FROM_CHARS 1
#else
#define SED_HAS_FLOAT_FROM_CHARS 0
#endif

namespace sed::text
{

/**
 * Floating-point std::from_chars: the same accepted pattern, locale
 * independence and result contract. Where the standard library lacks the
 * floating-point overloads, a C-locale strtod_l/strtof_l conversion of the
 * exactly matched pattern is used instead.
 *
 * On result_out_of_range the value differs by platform: MSVC stores the
 * infinity or zero, while libstdc++, libc++ and the fallback leave it
 * unmodified, as the standard specifies.
 */
[[nodiscard]] std::from_chars_result fromChars(
    const char* first,
    const char* last,
    double& value,
    std::chars_format format = std::chars_format::general) noexcept;

[[nodiscard]] std::from_chars_result fromChars(
    const char* first,
    const char* last,
    float& value,
    std::chars_format format = std::chars_format::general) noexcept;

#if !defined(_WIN32)
namespace detail
{

/**
 * The portable implementation behind fromChars where the standard overloads
 * are missing. Exposed so platforms that have them can test it against them.
 */
[[nodiscard]] std::from_chars_result fromCharsFallback(
    const char* first,
    const char* last,
    double& value,
    std::chars_format format) noexcept;

[[nodiscard]] std::from_chars_result fromCharsFallback(
    const char* first,
    const char* last,
    float& value,
    std::chars_format format) noexcept;

} // namespace detail
#endif

} // namespace sed::text
