#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "sed/level.hpp"

namespace sed::assets
{

/**
 * One ordinary LIGHTS row reduced to the values the
 * SED_SOURCE_LIGHTS checksum binds. Flags are the unsigned
 * 32-bit wire value widened to a signed 64-bit decimal;
 * RGB components stay binary32 until formatting promotes
 * them to binary64.
 */
struct SourceLightBaseRow
{
    std::int64_t flags{};
    std::int32_t layer{};
    double x{};
    double y{};
    double z{};
    double range{};
    double intensity{};
    float r{ 1.0f };
    float g{ 1.0f };
    float b{ 1.0f };

    constexpr bool operator==(const SourceLightBaseRow&) const noexcept = default;
};

/** Invariant round-trip spelling of a finite binary64 value. */
std::string formatSourceLightDouble(double value);

/** Strict finite binary64 parse; the whole token must parse. */
bool tryParseSourceLightDouble(std::string_view token, double& value) noexcept;

/** Lowercase hex SHA-256 of UTF-8 text bytes. */
std::string sourceLightSha256Hex(std::string_view text);

/** Canonical checksum text for ordinary rows, in list order. */
std::string canonicalSourceLightsText(std::span<const SourceLightBaseRow> rows);

/** LIGHTS_SHA256 value for ordinary rows. */
std::string sourceLightsChecksumHex(std::span<const SourceLightBaseRow> rows);

/** Base row for a model light (flags zero-extended from wire bits). */
SourceLightBaseRow sourceLightBaseRow(const SedLight& light) noexcept;

} // namespace sed::assets
