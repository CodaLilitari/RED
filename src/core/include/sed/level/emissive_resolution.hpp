#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sed/assets/cmp.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/level/lighting.hpp"

namespace sed::level
{

/** E-17 bake-profile defaults, mirroring the pinned core lighting tests. */
inline constexpr double kDefaultEmissiveIntensity = 1.0;
inline constexpr double kDefaultEmissiveRange = 10.0;

[[nodiscard]] inline bool emissiveProfileValid(
    const double intensity, const double range) noexcept
{
    return std::isfinite(intensity) && std::isfinite(range)
        && intensity > 0.0 && range > 0.0;
}

struct EmissiveResolution
{
    std::vector<LightingEmissiveSurface> surfaces{};
    /** FULLYLIT surfaces omitted because their MAT was missing/unparseable. */
    std::size_t omittedCount{};
};

using EmissiveResourceReader = std::function<std::optional<std::vector<std::byte>>(
    std::string_view name, std::string_view extension)>;

/**
 * Resolves FULLYLIT (light mode 0) surfaces to emissive inputs from actual
 * MAT data supplied by `readResource`, allowing the caller to enforce byte
 * budgets and bind the calculation to captured resource revisions.
 *
 * Color MATs emit their cel solid color; texture MATs emit the mip-0 mean
 * over texels that are not discarded (decoded alpha 0), falling back to the
 * solid color when every texel is discarded. The palette is the sector CMP
 * (or the level master CMP) parsed via `parseCmpPalette`; without one the
 * MAT parser's grayscale fallback applies. Missing or unparseable MATs omit
 * only that surface. An invalid profile resolves nothing and reads nothing.
 * Reader exceptions preserve the same recoverable omission/fallback policy;
 * transactional callers must retain admission/read failures separately and
 * refuse publication after this function returns rather than lose the error.
 */
[[nodiscard]] inline EmissiveResolution resolveEmissiveSurfaces(
    const Level& level,
    const EmissiveResourceReader& readResource,
    const double intensity = kDefaultEmissiveIntensity,
    const double range = kDefaultEmissiveRange)
{
    EmissiveResolution out{};
    if (!emissiveProfileValid(intensity, range))
    {
        return out;
    }

    std::unordered_map<std::string, std::optional<assets::CmpPalette>> palettes{};
    std::unordered_map<std::string, std::optional<ColorF>> emissions{};

    const auto paletteFor = [&](const std::string& cmpName) -> const assets::CmpPalette*
    {
        auto it = palettes.find(cmpName);
        if (it == palettes.end())
        {
            std::optional<assets::CmpPalette> parsed{};
            try
            {
                if (auto cmpBytes = readResource(cmpName, ".cmp"))
                {
                    parsed = assets::parseCmpPalette(*cmpBytes);
                }
            }
            catch (...)
            {
                parsed.reset();
            }
            it = palettes.emplace(cmpName, std::move(parsed)).first;
        }
        return it->second ? &*it->second : nullptr;
    };

    const auto emissionFor = [&](const Sector& sector,
                                 const std::string& material) -> std::optional<ColorF>
    {
        const std::string cmpName = !sector.colormap.empty() && sector.colormap != "-"
            ? sector.colormap
            : level.getMasterCMP();
        const std::string key = cmpName + '\n' + material;
        if (const auto cached = emissions.find(key); cached != emissions.end())
        {
            return cached->second;
        }
        std::optional<ColorF> emission{};
        try
        {
            if (auto matBytes = readResource(material, ".mat"))
            {
                const assets::CmpPalette* palette =
                    cmpName.empty() ? nullptr : paletteFor(cmpName);
                const assets::MatFile mat =
                    assets::MatFile::parse(std::move(*matBytes), palette);
                if (mat.celCount() > 0)
                {
                    const assets::MatFile::Cel& cel = mat.cel(0);
                    emission = ColorF{
                        static_cast<float>(cel.solidColor.r) / 255.0f,
                        static_cast<float>(cel.solidColor.g) / 255.0f,
                        static_cast<float>(cel.solidColor.b) / 255.0f, 1.0f };
                    if (!mat.isColor())
                    {
                        const sed::Rgba8Image image = mat.decodeCelRgba8(0);
                        if (!image.empty())
                        {
                            double r = 0.0, g = 0.0, b = 0.0;
                            std::size_t count = 0;
                            const std::size_t pixels = image.rgba.size() / 4u;
                            for (std::size_t i = 0; i < pixels; ++i)
                            {
                                if (image.rgba[i * 4 + 3] == 0)
                                {
                                    continue; // discarded texel (index 0 / transparent)
                                }
                                r += image.rgba[i * 4 + 0];
                                g += image.rgba[i * 4 + 1];
                                b += image.rgba[i * 4 + 2];
                                ++count;
                            }
                            if (count > 0)
                            {
                                emission = ColorF{
                                    static_cast<float>(r / static_cast<double>(count) / 255.0),
                                    static_cast<float>(g / static_cast<double>(count) / 255.0),
                                    static_cast<float>(b / static_cast<double>(count) / 255.0),
                                    1.0f };
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
            emission.reset();
        }
        emissions.emplace(key, emission);
        return emission;
    };

    for (std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex)
    {
        const Sector& sector = level.sectors[sectorIndex];
        for (std::size_t surfaceIndex = 0; surfaceIndex < sector.surfaces.size(); ++surfaceIndex)
        {
            const Surface& surface = sector.surfaces[surfaceIndex];
            if (surface.light != 0 || surface.material.empty())
            {
                continue;
            }
            const std::optional<ColorF> emission = emissionFor(sector, surface.material);
            if (!emission)
            {
                ++out.omittedCount;
                continue;
            }
            out.surfaces.push_back(LightingEmissiveSurface{
                static_cast<std::int32_t>(sectorIndex),
                static_cast<std::int32_t>(surfaceIndex),
                *emission, intensity, range });
        }
    }
    return out;
}

/** Resolves emissive inputs through normal loose-root and mounted-GOB
 * precedence. Captured or bounded callers use the resource-reader overload. */
[[nodiscard]] inline EmissiveResolution resolveEmissiveSurfaces(
    const Level& level,
    const assets::ResourceLocator& resources,
    const double intensity = kDefaultEmissiveIntensity,
    const double range = kDefaultEmissiveRange)
{
    return resolveEmissiveSurfaces(level,
        [&resources](const std::string_view name, const std::string_view extension)
        {
            return resources.tryReadBinaryWithExt(name, extension);
        }, intensity, range);
}

/**
 * Resolves the E-17 sun/sky profile: FULLYLIT surfaces flagged
 * SF_SkyCeiling or SF_SkyHorizon emit the uniform `skyColor` at the
 * profile intensity/range, with no MAT lookup. Non-FULLYLIT sky
 * surfaces are skipped (the bake only honours light mode 0 emitters).
 * An invalid profile or a black/non-finite sky colour resolves
 * nothing. Directional sun shading is not modelled: sky emission is
 * uniform, like a bright sky texture in the original baker. */
[[nodiscard]] inline EmissiveResolution resolveSkyEmissiveSurfaces(
    const Level& level,
    const ColorF skyColor,
    const double intensity = kDefaultEmissiveIntensity,
    const double range = kDefaultEmissiveRange)
{
    EmissiveResolution out{};
    if (!emissiveProfileValid(intensity, range)
        || !std::isfinite(skyColor.r) || !std::isfinite(skyColor.g) || !std::isfinite(skyColor.b)
        || (skyColor.r <= 0.0f && skyColor.g <= 0.0f && skyColor.b <= 0.0f))
    {
        return out;
    }
    for (std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex)
    {
        const Sector& sector = level.sectors[sectorIndex];
        for (std::size_t surfaceIndex = 0; surfaceIndex < sector.surfaces.size(); ++surfaceIndex)
        {
            const Surface& surface = sector.surfaces[surfaceIndex];
            if (surface.light != 0
                || (surface.surfaceFlags & (SF_SkyCeiling | SF_SkyHorizon)) == 0)
            {
                continue;
            }
            out.surfaces.push_back(LightingEmissiveSurface{
                static_cast<std::int32_t>(sectorIndex),
                static_cast<std::int32_t>(surfaceIndex),
                skyColor, intensity, range });
        }
    }
    return out;
}

} // namespace sed::level
