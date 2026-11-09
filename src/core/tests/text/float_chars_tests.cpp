#include "test_common.hpp"

#include "sed/float_chars.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace sed::tests
{
namespace
{

template <typename T>
bool sameValue(const T a, const T b) noexcept
{
    if ( std::isnan(a) || std::isnan(b) )
    {
        return std::isnan(a) && std::isnan(b) && std::signbit(a) == std::signbit(b);
    }
    using Bits = std::conditional_t<sizeof(T) == sizeof(std::uint64_t), std::uint64_t, std::uint32_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b);
}

struct FixedVector
{
    std::string_view input{};
    std::chars_format format{ std::chars_format::general };
    std::errc ec{};
    std::size_t consumed{};
    double expected{};
};

// Platform-independent expectations for the public helper. They hold for the
// standard overloads and for the fallback, so they also run on toolchains
// that only have the fallback. For out-of-range entries, expected is the
// value MSVC stores; elsewhere the value must stay unmodified.
template <typename T>
void checkFixedVectors(const char* const typeName)
{
    constexpr T sentinel = static_cast<T>(42.0);
    const T inf = std::numeric_limits<T>::infinity();
    const std::string type(typeName);
    for ( const FixedVector& v : std::initializer_list<FixedVector>{
             { "1.5", std::chars_format::general, std::errc{}, 3, 1.5 },
             { "-0.25e1", std::chars_format::general, std::errc{}, 7, -2.5 },
             { "12abc", std::chars_format::general, std::errc{}, 2, 12.0 },
             { "1e", std::chars_format::general, std::errc{}, 1, 1.0 },
             { "1e+", std::chars_format::general, std::errc{}, 1, 1.0 },
             { "5.", std::chars_format::general, std::errc{}, 2, 5.0 },
             { ".5", std::chars_format::general, std::errc{}, 2, 0.5 },
             { "1e5", std::chars_format::fixed, std::errc{}, 1, 1.0 },
             { "1.5e2", std::chars_format::scientific, std::errc{}, 5, 150.0 },
             { "1.8p1", std::chars_format::hex, std::errc{}, 5, 3.0 },
             { "-a.8p-2", std::chars_format::hex, std::errc{}, 7, -2.625 },
             { "0x1p3", std::chars_format::hex, std::errc{}, 1, 0.0 },
             { "0x1p3", std::chars_format::general, std::errc{}, 1, 0.0 },
             { "infinite", std::chars_format::general, std::errc{}, 3, static_cast<double>(inf) },
             { "-INFINITY", std::chars_format::general, std::errc{}, 9, -static_cast<double>(inf) },
             { "+1", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { " 1", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { "", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { ".", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { "-", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { "e5", std::chars_format::general, std::errc::invalid_argument, 0, 0.0 },
             { "1.5", std::chars_format::scientific, std::errc::invalid_argument, 0, 0.0 },
             { "p3", std::chars_format::hex, std::errc::invalid_argument, 0, 0.0 },
             { "1e999", std::chars_format::general, std::errc::result_out_of_range, 5, static_cast<double>(inf) },
             { "-1e999", std::chars_format::general, std::errc::result_out_of_range, 6, -static_cast<double>(inf) },
             { "1e-999", std::chars_format::general, std::errc::result_out_of_range, 6, 0.0 } } )
    {
        T value = sentinel;
        const std::from_chars_result result =
            text::fromChars(v.input.data(), v.input.data() + v.input.size(), value, v.format);
        const std::size_t consumed = static_cast<std::size_t>(result.ptr - v.input.data());
        bool valueOk = v.ec == std::errc{}
            ? sameValue(value, static_cast<T>(v.expected))
            : sameValue(value, sentinel);
#if defined(_WIN32)
        // MSVC stores the infinity or zero for an out-of-range result (see
        // LWG 3081); the standard, and the fallback, leave the value unmodified.
        if ( v.ec == std::errc::result_out_of_range && !valueOk )
        {
            valueOk = sameValue(value, static_cast<T>(v.expected));
        }
#endif
        if ( result.ec != v.ec || consumed != v.consumed || !valueOk )
        {
            const std::string message = "fromChars<" + type + "> fixed vector \"" + std::string(v.input) + "\"";
            check(false, message.c_str());
        }
    }

    for ( const std::string_view input : { std::string_view("nan"), std::string_view("-nan(abc_1)x"),
              std::string_view("NaN(") } )
    {
        T value = sentinel;
        const std::from_chars_result result =
            text::fromChars(input.data(), input.data() + input.size(), value);
        const std::size_t expected = input == "-nan(abc_1)x" ? 11u : 3u;
        if ( result.ec != std::errc{} || static_cast<std::size_t>(result.ptr - input.data()) != expected
            || !std::isnan(value) || std::signbit(value) != (input.front() == '-') )
        {
            const std::string message = "fromChars<" + type + "> NaN vector \"" + std::string(input) + "\"";
            check(false, message.c_str());
        }
    }
}

#if SED_HAS_FLOAT_FROM_CHARS && !defined(_WIN32)
// Inputs for comparing the fallback with the standard library's own overloads.
constexpr std::string_view kDifferentialInputs[]{
    "0", "-0", "1", "-1", "1.5", ".5", "5.", "-.5e3", "1e", "1e+", "1e-", "1e+5", "1E-5", "1e5x", "  1", "+1",
    "-", "", ".", "..5", "1.5.3", "1,5", "e3", "E", "inf", "INF", "-Infinity", "infinit", "infinityx", "nan",
    "NaN(123)", "nan(", "nan()", "nan(_a1)", "nan(a-b)", "-nan", "1e309", "-1e309", "1e-400", "-1e-400",
    "4.9406564584124654e-324", "2.4703282292062327e-324", "2.2250738585072014e-308", "1e-310", "3.4028235e38",
    "3.4028236e38", "3.5e38", "1e-46", "1e-45", "1.4e-45", "1.17549435e-38", "1e-40", "1.7976931348623157e308",
    "1.7976931348623159e308", "9007199254740993", "123456789012345678901234567890",
    "0.000000000000000000000000000000000001", "0e999999", "0.0000e-99999", "1p3", "1.8p1", "-a.bp-2", "ff",
    "FF.8", "p3", "1p", "1p+", "0x1p3", "1P-1074", "1p-1075", "1p1024", "1p128", "1p-150", "1p-149",
    "00000000000000000000001.5", "1.000000000000000000000000000000000000001" };

template <typename T>
void checkDifferential(const char* const typeName)
{
    constexpr T sentinel = static_cast<T>(42.0);
    const std::string type(typeName);
    for ( const std::chars_format format : { std::chars_format::general, std::chars_format::fixed,
              std::chars_format::scientific, std::chars_format::hex } )
    {
        for ( const std::string_view input : kDifferentialInputs )
        {
            T standard = sentinel;
            T fallback = sentinel;
            const auto expected = std::from_chars(input.data(), input.data() + input.size(), standard, format);
            const auto actual = text::detail::fromCharsFallback(
                input.data(), input.data() + input.size(), fallback, format);
            // libstdc++'s hex path drops the sign of "-nan" while its decimal path
            // keeps it; the fallback keeps the sign in both, as strtod does.
            const bool hexNan = format == std::chars_format::hex && std::isnan(standard) && std::isnan(fallback);
            const bool valuesMatch = hexNan || sameValue(standard, fallback);
            if ( expected.ec != actual.ec || expected.ptr != actual.ptr || !valuesMatch )
            {
                const std::string message = "fromChars fallback<" + type + "> matches std::from_chars for \""
                    + std::string(input) + "\" format " + std::to_string(static_cast<int>(format));
                check(false, message.c_str());
            }
        }
    }
}
#endif

} // namespace

void runFloatCharsTests()
{
    checkFixedVectors<double>("double");
    checkFixedVectors<float>("float");
#if SED_HAS_FLOAT_FROM_CHARS && !defined(_WIN32)
    checkDifferential<double>("double");
    checkDifferential<float>("float");
#endif
}

} // namespace sed::tests
