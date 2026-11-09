#include "sed/float_chars.hpp"

#if !defined(_WIN32)
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <locale.h>
#include <string>
#include <string_view>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace sed::text
{

#if !defined(_WIN32)
namespace
{

bool isDecimalDigit(const char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool isHexDigit(const char c) noexcept
{
    return isDecimalDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char lowerAscii(const char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool startsWithNoCase(const char* const position, const char* const last, const std::string_view word) noexcept
{
    if ( static_cast<std::size_t>(last - position) < word.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < word.size(); ++i )
    {
        if ( lowerAscii(position[i]) != word[i] )
        {
            return false;
        }
    }
    return true;
}

struct PatternMatch
{
    std::size_t length{};
    bool special{};
};

// Matches the longest prefix of the std::from_chars pattern for format: the
// strtod subject sequence without leading whitespace, a leading '+', or (for
// hex) the "0x" prefix; fixed forbids and scientific requires an exponent.
PatternMatch matchPattern(const char* const first, const char* const last, const std::chars_format format) noexcept
{
    const char* position = first;
    if ( position != last && *position == '-' )
    {
        ++position;
    }
    if ( startsWithNoCase(position, last, "infinity") )
    {
        return { static_cast<std::size_t>(position + 8 - first), true };
    }
    if ( startsWithNoCase(position, last, "inf") )
    {
        return { static_cast<std::size_t>(position + 3 - first), true };
    }
    if ( startsWithNoCase(position, last, "nan") )
    {
        const char* const afterNan = position + 3;
        if ( afterNan != last && *afterNan == '(' )
        {
            const char* sequence = afterNan + 1;
            while ( sequence != last
                && (isDecimalDigit(*sequence) || (lowerAscii(*sequence) >= 'a' && lowerAscii(*sequence) <= 'z')
                    || *sequence == '_') )
            {
                ++sequence;
            }
            if ( sequence != last && *sequence == ')' )
            {
                return { static_cast<std::size_t>(sequence + 1 - first), true };
            }
        }
        return { static_cast<std::size_t>(afterNan - first), true };
    }

    const bool hex = format == std::chars_format::hex;
    const auto isDigit = [hex](const char c)
    {
        return hex ? isHexDigit(c) : isDecimalDigit(c);
    };
    std::size_t digits = 0;
    while ( position != last && isDigit(*position) )
    {
        ++position;
        ++digits;
    }
    if ( position != last && *position == '.' )
    {
        ++position;
        while ( position != last && isDigit(*position) )
        {
            ++position;
            ++digits;
        }
    }
    if ( digits == 0 )
    {
        return {};
    }

    const bool scientific = (format & std::chars_format::scientific) == std::chars_format::scientific;
    const bool fixed = (format & std::chars_format::fixed) == std::chars_format::fixed;
    const bool exponentAllowed = hex || scientific;
    const bool exponentRequired = !hex && scientific && !fixed;
    bool hasExponent = false;
    if ( exponentAllowed && position != last && lowerAscii(*position) == (hex ? 'p' : 'e') )
    {
        const char* exponent = position + 1;
        if ( exponent != last && (*exponent == '+' || *exponent == '-') )
        {
            ++exponent;
        }
        if ( exponent != last && isDecimalDigit(*exponent) )
        {
            while ( exponent != last && isDecimalDigit(*exponent) )
            {
                ++exponent;
            }
            position = exponent;
            hasExponent = true;
        }
    }
    if ( exponentRequired && !hasExponent )
    {
        return {};
    }
    return { static_cast<std::size_t>(position - first), false };
}

locale_t cLocale() noexcept
{
    static const locale_t locale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    return locale;
}

double convert(const char* const text, char** const end, double) noexcept
{
    return strtod_l(text, end, cLocale());
}

float convert(const char* const text, char** const end, float) noexcept
{
    return strtof_l(text, end, cLocale());
}

template <typename T>
std::from_chars_result fallback(
    const char* const first,
    const char* const last,
    T& value,
    const std::chars_format format) noexcept
{
    const PatternMatch match = matchPattern(first, last, format);
    if ( match.length == 0 || cLocale() == static_cast<locale_t>(nullptr) )
    {
        return { first, std::errc::invalid_argument };
    }
    const char* const matchEnd = first + match.length;
    try
    {
        // strtod needs a terminated copy, and from_chars' hex form omits the
        // "0x" prefix that strtod requires.
        std::string text;
        text.reserve(match.length + 2);
        const char* body = first;
        if ( *body == '-' )
        {
            text.push_back('-');
            ++body;
        }
        if ( format == std::chars_format::hex && !match.special )
        {
            text += "0x";
        }
        text.append(body, matchEnd);

        errno = 0;
        char* end = nullptr;
        const T parsed = convert(text.c_str(), &end, T{});
        const int error = errno;
        if ( end != text.c_str() + text.size() )
        {
            return { first, std::errc::invalid_argument };
        }
        // strtod also reports ERANGE for representable subnormal results.
        // Only overflow and a nonzero value rounded to zero are out of range.
        if ( error == ERANGE && (std::isinf(parsed) || parsed == T{}) )
        {
            return { matchEnd, std::errc::result_out_of_range };
        }
        value = parsed;
        return { matchEnd, std::errc{} };
    }
    catch ( ... )
    {
        return { first, std::errc::not_enough_memory };
    }
}

} // namespace

namespace detail
{

std::from_chars_result fromCharsFallback(
    const char* const first,
    const char* const last,
    double& value,
    const std::chars_format format) noexcept
{
    return fallback(first, last, value, format);
}

std::from_chars_result fromCharsFallback(
    const char* const first,
    const char* const last,
    float& value,
    const std::chars_format format) noexcept
{
    return fallback(first, last, value, format);
}

} // namespace detail
#endif

std::from_chars_result fromChars(
    const char* const first,
    const char* const last,
    double& value,
    const std::chars_format format) noexcept
{
#if SED_HAS_FLOAT_FROM_CHARS
    return std::from_chars(first, last, value, format);
#else
    return detail::fromCharsFallback(first, last, value, format);
#endif
}

std::from_chars_result fromChars(
    const char* const first,
    const char* const last,
    float& value,
    const std::chars_format format) noexcept
{
#if SED_HAS_FLOAT_FROM_CHARS
    return std::from_chars(first, last, value, format);
#else
    return detail::fromCharsFallback(first, last, value, format);
#endif
}

} // namespace sed::text
