#include "sed/assets/source_lights.hpp"
#include "sed/float_chars.hpp"

#include "sed/assets/sed.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace sed::assets
{

std::string formatSourceLightDouble(const double value)
{
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general);
    if (result.ec != std::errc{})
    {
        throw std::runtime_error(
            "Cannot format a source-light value for its checksum.");
    }
    return std::string(buffer.data(), result.ptr);
}

bool tryParseSourceLightDouble(
    const std::string_view token, double& value) noexcept
{
    if (token.empty())
    {
        return false;
    }
    double parsed{};
    const char* const first = token.data();
    const char* const last = first + token.size();
    const auto result = sed::text::fromChars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last
        || !std::isfinite(parsed))
    {
        return false;
    }
    value = parsed;
    return true;
}

std::string sourceLightSha256Hex(const std::string_view text)
{
    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    const std::array<std::byte, 32> digest = calculateSedSha256(bytes);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex{};
    hex.reserve(digest.size() * 2);
    for (const std::byte byte : digest)
    {
        const auto value = static_cast<unsigned char>(byte);
        hex.push_back(kHex[value >> 4]);
        hex.push_back(kHex[value & 0x0f]);
    }
    return hex;
}

std::string canonicalSourceLightsText(
    const std::span<const SourceLightBaseRow> rows)
{
    std::string text = "sed-source-light-base-v1\n";
    text += std::to_string(rows.size());
    text.push_back('\n');
    for (const SourceLightBaseRow& row : rows)
    {
        text += std::to_string(row.flags);
        text.push_back(' ');
        text += std::to_string(row.layer);
        text.push_back(' ');
        text += formatSourceLightDouble(row.x);
        text.push_back(' ');
        text += formatSourceLightDouble(row.y);
        text.push_back(' ');
        text += formatSourceLightDouble(row.z);
        text.push_back(' ');
        text += formatSourceLightDouble(row.range);
        text.push_back(' ');
        text += formatSourceLightDouble(row.intensity);
        text.push_back(' ');
        text += formatSourceLightDouble(static_cast<double>(row.r));
        text.push_back(' ');
        text += formatSourceLightDouble(static_cast<double>(row.g));
        text.push_back(' ');
        text += formatSourceLightDouble(static_cast<double>(row.b));
        text.push_back('\n');
    }
    return text;
}

std::string sourceLightsChecksumHex(
    const std::span<const SourceLightBaseRow> rows)
{
    return sourceLightSha256Hex(canonicalSourceLightsText(rows));
}

SourceLightBaseRow sourceLightBaseRow(const SedLight& light) noexcept
{
    SourceLightBaseRow row{};
    row.flags = static_cast<std::int64_t>(
        static_cast<std::uint32_t>(light.flags));
    row.layer = light.layer;
    row.x = light.position.x;
    row.y = light.position.y;
    row.z = light.position.z;
    row.range = light.range;
    row.intensity = light.intensity;
    row.r = light.color.r;
    row.g = light.color.g;
    row.b = light.color.b;
    return row;
}

} // namespace sed::assets
