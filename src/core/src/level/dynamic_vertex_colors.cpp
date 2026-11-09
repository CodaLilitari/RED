#include "sed/level/dynamic_vertex_colors.hpp"
#include "sed/float_chars.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

#include "sed/text.hpp"

namespace sed::level
{
namespace
{

using sed::text::iequalsAscii;
using sed::text::istartsWithAscii;
using sed::text::parseInt;
using sed::text::splitWords;
using sed::text::trimView;
using sed::text::upperAscii;

DynamicVertexColorEvaluation invalidEvaluation(const char* reason)
{
    return DynamicVertexColorEvaluation{
        VertexRgbStorage::Invalid, VertexRgbStorage::Invalid,
        VertexColorReadiness::Unknown, std::nullopt, reason };
}

struct LightingToken
{
    float direct{};
    float viaDouble{};
    bool parsed{};
    bool finite{};
};

// Parses one serialized token both ways the loaders can reach float32:
// directly, and through a double intermediate. The two results are kept
// apart because their rare disagreement is exactly the unverified edge.
LightingToken parseLightingToken(const std::string_view token)
{
    LightingToken out{};
    std::string_view body = token;
    if (!body.empty() && body.front() == '+')
    {
        body.remove_prefix(1);
    }
    if (body.empty())
    {
        return out;
    }

    double asDouble{};
    const auto doubleResult = sed::text::fromChars(
        body.data(), body.data() + body.size(), asDouble,
        std::chars_format::general);
    if (doubleResult.ec != std::errc{} || doubleResult.ptr != body.data() + body.size())
    {
        return out;
    }

    // NaN marks "not written": an out-of-range parse never stores a NaN.
    float asFloat = std::numeric_limits<float>::quiet_NaN();
    const auto floatResult = sed::text::fromChars(
        body.data(), body.data() + body.size(), asFloat,
        std::chars_format::general);
    if (floatResult.ptr != body.data() + body.size())
    {
        return out;
    }
    if (floatResult.ec == std::errc::result_out_of_range)
    {
        // MSVC stores the infinity or zero for an out-of-range parse, while
        // libstdc++, libc++ and the portable fallback leave the value
        // unmodified, as the standard specifies. Recover MSVC's result from
        // the parsed double where nothing was stored. Underflow to zero is a
        // value, not a failure; overflow yields an infinite sample, which is
        // never usable.
        if (std::isnan(asFloat))
        {
            const bool overflow =
                std::fabs(asDouble) > static_cast<double>(std::numeric_limits<float>::max());
            const float magnitude = overflow ? std::numeric_limits<float>::infinity() : 0.0f;
            asFloat = std::signbit(asDouble) ? -magnitude : magnitude;
        }
    }
    else if (floatResult.ec != std::errc{})
    {
        return out;
    }

    out.direct = asFloat;
    out.viaDouble = static_cast<float>(asDouble);
    out.parsed = true;
    out.finite = std::isfinite(asFloat) && std::isfinite(asDouble);
    return out;
}

std::vector<std::string> splitLines(const std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string line(text.substr(start, end - start));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (newline == std::string_view::npos)
        {
            break;
        }
        start = newline + 1;
    }
    if (!lines.empty() && lines.front().starts_with("\xEF\xBB\xBF"))
    {
        lines.front().erase(0, 3);
    }
    return lines;
}

} // namespace

std::string_view vertexRgbStorageName(const VertexRgbStorage storage) noexcept
{
    switch (storage)
    {
    case VertexRgbStorage::Retained: return "Retained";
    case VertexRgbStorage::Compacted: return "Compacted";
    case VertexRgbStorage::Scalar: return "Scalar";
    case VertexRgbStorage::Unknown: return "Unknown";
    case VertexRgbStorage::Invalid: return "Invalid";
    }
    return "Invalid";
}

std::string_view vertexColorReadinessName(const VertexColorReadiness readiness) noexcept
{
    switch (readiness)
    {
    case VertexColorReadiness::Ready: return "Ready";
    case VertexColorReadiness::NotReady: return "NotReady";
    case VertexColorReadiness::Unknown: return "Unknown";
    }
    return "Unknown";
}

DynamicVertexColorEvaluation evaluateDynamicVertexColors(
    const std::span<const DynamicVertexColorSample> samples,
    const int componentsPerCorner)
{
    if (samples.size() < 3 || samples.size() > 24
        || (componentsPerCorner != 1 && componentsPerCorner != 4))
    {
        return invalidEvaluation(
            "Corner count or lighting row layout is outside the assessed range.");
    }
    for (const DynamicVertexColorSample& sample : samples)
    {
        if (!std::isfinite(sample.white) || !std::isfinite(sample.red)
            || !std::isfinite(sample.green) || !std::isfinite(sample.blue))
        {
            return invalidEvaluation(
                "Non-finite float32 lighting cannot support a storage verdict.");
        }
    }
    if (componentsPerCorner == 1)
    {
        return DynamicVertexColorEvaluation{
            VertexRgbStorage::Unknown, VertexRgbStorage::Scalar,
            VertexColorReadiness::Unknown, std::nullopt,
            "Scalar serialized row: retail handling is unverified, and "
            "current OpenMotS clears the RGB flag so its RGB setter is "
            "unavailable." };
    }
    for (std::size_t corner = 0; corner < samples.size(); ++corner)
    {
        const DynamicVertexColorSample& sample = samples[corner];
        // Exact float32 equality by design: no epsilon, no clamping.
        // Blue is deliberately absent from this predicate.
        if (sample.white != sample.red || sample.white != sample.green)
        {
            return DynamicVertexColorEvaluation{
                VertexRgbStorage::Retained, VertexRgbStorage::Retained,
                VertexColorReadiness::Ready,
                static_cast<std::int32_t>(corner),
                "One final float32 corner separates white from red or "
                "green, which keeps RGB storage for the whole surface "
                "in both inspected loaders." };
        }
    }
    return DynamicVertexColorEvaluation{
        VertexRgbStorage::Compacted, VertexRgbStorage::Retained,
        VertexColorReadiness::NotReady, std::nullopt,
        "Every final float32 corner has W == R == G, so the inspected "
        "retail loader compacts RGB storage away; blue cannot change "
        "that outcome, while current OpenMotS retains the 4N row." };
}

std::size_t countVertexColorReadiness(
    const DynamicVertexColorReport& report,
    const VertexColorReadiness readiness) noexcept
{
    std::size_t count = 0;
    for (const SurfaceVertexColorReadiness& surface : report.surfaces)
    {
        if (surface.readiness == readiness)
        {
            ++count;
        }
    }
    return count;
}

std::size_t countVertexRgbStorage(
    const DynamicVertexColorReport& report,
    const VertexRgbStorage storage,
    const bool retail) noexcept
{
    std::size_t count = 0;
    for (const SurfaceVertexColorReadiness& surface : report.surfaces)
    {
        if ((retail ? surface.retail : surface.openMots) == storage)
        {
            ++count;
        }
    }
    return count;
}

DynamicVertexColorReport evaluateDynamicVertexColorsSerialized(
    const std::string_view outputText,
    const bool isMots)
{
    DynamicVertexColorReport report{};
    report.proposedOutputSha256 = assets::jklSourceSha256Hex(outputText);
    report.detail = std::string(kDynamicVertexColorScope);
    const auto unavailable = [&report](const std::string& message) {
        report.status = "unavailable";
        report.surfaces.clear();
        report.detail = "Serialized output could not be assessed: " + message;
        return report;
    };
    if (!isMots)
    {
        report.status = "unsupported-game";
        report.detail = "This diagnostic is scoped to MotS. " + report.detail;
        return report;
    }

    const std::vector<std::string> lines = splitLines(outputText);
    std::string section{};
    std::int32_t version = 0;
    std::int32_t total = -1;
    std::size_t cursor = 0;
    for (std::size_t at = 0; at < lines.size(); ++at)
    {
        const std::string_view trimmed = trimView(lines[at]);
        if (istartsWithAscii(trimmed, "section:"))
        {
            section = upperAscii(trimView(trimmed.substr(8)));
            continue;
        }
        const auto words = splitWords(trimmed);
        if (section == "HEADER" && words.size() >= 2 && iequalsAscii(words[0], "VERSION"))
        {
            if (const auto parsed = parseInt<std::int32_t>(words[1]))
            {
                version = *parsed;
            }
            continue;
        }
        if (section == "GEORESOURCE" && words.size() >= 3
            && iequalsAscii(words[0], "WORLD") && iequalsAscii(words[1], "SURFACES"))
        {
            const auto parsed = parseInt<std::int32_t>(words[2]);
            if (!parsed || *parsed < 0)
            {
                return unavailable("Negative or unreadable surface count.");
            }
            total = *parsed;
            cursor = at + 1;
            break;
        }
    }
    if (total < 0)
    {
        return unavailable("No GEORESOURCE surface table found.");
    }

    // Header version 1 stores one extra-light token on each surface row;
    // every other version stores four. Vertex pairs count as two words.
    const std::size_t extraWidth = version == 1 ? 1u : 4u;
    const std::size_t nvertWord = 7 + extraWidth;
    std::vector<SurfaceVertexColorReadiness> surfaces;
    surfaces.reserve(static_cast<std::size_t>(total));
    for (std::int32_t expected = 0; expected < total; ++expected)
    {
        while (cursor < lines.size() && trimView(lines[cursor]).empty())
        {
            ++cursor;
        }
        if (cursor >= lines.size())
        {
            return unavailable("Missing surface row.");
        }
        std::string normalized = lines[cursor++];
        for (char& ch : normalized)
        {
            if (ch == ',')
            {
                ch = ' ';
            }
        }
        const auto tokens = splitWords(normalized);
        if (tokens.empty())
        {
            return unavailable("Missing surface row.");
        }
        std::string_view indexToken = tokens.front();
        if (!indexToken.empty() && indexToken.back() == ':')
        {
            indexToken.remove_suffix(1);
        }
        const auto rowIndex = parseInt<std::int32_t>(indexToken);
        if (!rowIndex || *rowIndex != expected)
        {
            return unavailable("Surface row identity is not the expected global index.");
        }
        const auto word = [&tokens](const std::size_t at) -> std::string_view {
            return tokens[1 + at];
        };
        if (tokens.size() < 1 + nvertWord + 1)
        {
            return unavailable("Truncated surface row.");
        }
        const auto geo = parseInt<std::int32_t>(word(3));
        const auto light = parseInt<std::int32_t>(word(4));
        const auto nAdj = parseInt<std::int32_t>(word(6));
        const auto count = parseInt<std::int32_t>(word(nvertWord));
        if (!geo || !light || !nAdj || !count)
        {
            return unavailable("Truncated surface row.");
        }
        if (*count < 3 || *count > 24)
        {
            return unavailable("Surface corner count is outside the inspected engine range 3..24.");
        }
        const std::size_t corners = static_cast<std::size_t>(*count);
        const std::size_t firstLight = nvertWord + 1 + 2 * corners;
        if (tokens.size() - 1 < firstLight)
        {
            return unavailable("Truncated surface row.");
        }
        const std::size_t remaining = tokens.size() - 1 - firstLight;
        const int components = remaining == corners ? 1 : remaining == 4 * corners ? 4 : 0;
        if (components == 0)
        {
            return unavailable("Surface lighting row is neither N nor 4N.");
        }

        bool ambiguousConversion = false;
        std::vector<DynamicVertexColorSample> samples(corners);
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
            const std::size_t base = firstLight + corner * static_cast<std::size_t>(components);
            float values[4]{};
            for (int channel = 0; channel < components; ++channel)
            {
                const LightingToken token = parseLightingToken(
                    word(base + static_cast<std::size_t>(channel)));
                if (!token.parsed || !token.finite)
                {
                    return unavailable("Lighting token is outside finite float32 range.");
                }
                if (channel < 3 && token.direct != token.viaDouble)
                {
                    ambiguousConversion = true;
                }
                values[channel] = token.direct;
            }
            samples[corner] = components == 1
                ? DynamicVertexColorSample{ values[0], values[0], values[0], values[0] }
                : DynamicVertexColorSample{ values[0], values[1], values[2], values[3] };
        }

        DynamicVertexColorEvaluation result = evaluateDynamicVertexColors(samples, components);
        if (components == 4 && ambiguousConversion)
        {
            result = DynamicVertexColorEvaluation{
                VertexRgbStorage::Unknown, VertexRgbStorage::Retained,
                VertexColorReadiness::Unknown, std::nullopt,
                "A serialized W/R/G token rounds differently through "
                "direct float32 parsing than through double-to-float32 "
                "conversion, so the retail outcome stays unverified; "
                "current OpenMotS still retains the 4N row." };
        }
        surfaces.push_back(SurfaceVertexColorReadiness{
            expected, -1, -1, *count, components,
            components == 1 ? "N" : "4N", result.retail, result.openMots,
            result.readiness, result.distinguishingCorner, *geo, *light,
            *nAdj >= 0, result.reason });
    }

    report.status = "assessed";
    report.surfaces = std::move(surfaces);
    return report;
}

DynamicVertexColorReport evaluateDynamicVertexColors(
    const Level& level,
    const assets::JklSaveOptions& options)
{
    DynamicVertexColorReport report{};
    report.detail = std::string(kDynamicVertexColorScope);
    if (level.kind != ProjectType::MOTS)
    {
        report.status = "unsupported-game";
        report.detail = "This diagnostic is scoped to MotS. " + report.detail;
        return report;
    }

    assets::JklBuiltExport built{};
    try
    {
        built = assets::buildJklExport(level, options);
    }
    catch (const std::exception& error)
    {
        report.status = "unavailable";
        report.detail = std::string("Proposed serialization could not be assessed: ") + error.what();
        return report;
    }

    report = evaluateDynamicVertexColorsSerialized(built.text, true);
    if (report.status != "assessed")
    {
        return report;
    }

    std::size_t totalSurfaces = 0;
    for (const Sector& sector : level.sectors)
    {
        totalSurfaces += sector.surfaces.size();
    }
    if (report.surfaces.size() != totalSurfaces)
    {
        report.status = "unavailable";
        report.surfaces.clear();
        report.detail = "Serialized output could not be assessed: "
            "Serialized surface rows do not match the parsed surface identities.";
        return report;
    }

    std::size_t at = 0;
    for (std::size_t sector = 0; sector < level.sectors.size(); ++sector)
    {
        const Sector& sec = level.sectors[sector];
        for (std::size_t surface = 0; surface < sec.surfaces.size(); ++surface)
        {
            SurfaceVertexColorReadiness& row = report.surfaces[at++];
            row.sectorIndex = static_cast<std::int32_t>(sector);
            row.surfaceIndex = static_cast<std::int32_t>(surface);
            row.geometryMode = sec.surfaces[surface].geo;
            row.lightingMode = sec.surfaces[surface].light;
            row.adjoined = sec.surfaces[surface].adjoin.has_value();
        }
    }
    report.geometryValid = true;
    return report;
}

} // namespace sed::level
