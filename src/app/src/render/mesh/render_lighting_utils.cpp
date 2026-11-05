#include "render/mesh/render_lighting_utils.hpp"

#include "sed/assets/3do.hpp"
#include "sed/geometry.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace sed::render
{
namespace
{

constexpr float kThingLightMulMin = 0.25f;
constexpr float kThingLightMulMax = 4.0f;
constexpr float kThingLightMulDenEpsilon = 0.05f;
constexpr std::int32_t kThingArchFlag = 0x00000008;

float safeLightRatio(const float num, const float den) noexcept
{
    return num / std::max(den, kThingLightMulDenEpsilon);
}

} // namespace

std::optional<std::size_t> motsArchLightingSourceMeshIndex(
    const assets::Model3do& model,
    const std::size_t renderMeshIndex) noexcept
{
    return level::motsArchLightingSourceMeshIndex(model, renderMeshIndex);
}

const MotsArchLightObject* resolveMotsArchLightingPreview(
    const Level& level,
    const Thing& thing,
    const assets::Model3do& model) noexcept
{
    return resolveMotsArchLightingPreview(level, nullptr, thing, model);
}

const MotsArchLightObject* resolveMotsArchLightingPreview(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing,
    const assets::Model3do& model) noexcept
{
    if (level.kind != ProjectType::MOTS
        || (sed::level::thingFlags(level, templates, thing)
            & kThingArchFlag) == 0
        || thing.archLightingIndex < 0
        || static_cast<std::size_t>(thing.archLightingIndex)
            >= level.motsArchLighting.size())
    {
        return nullptr;
    }

    const MotsArchLightObject& object = level.motsArchLighting[
        static_cast<std::size_t>(thing.archLightingIndex)];
    // sithRender reuses the authored archive mesh/vertex ordinal across
    // transformed hierarchy instances. A lower LOD may expose a shorter
    // vertex prefix, so each consumed source mesh only needs to cover the
    // selected render mesh's vertices; surplus archive meshes remain unused.
    for (std::size_t renderMeshIndex = 0;
         renderMeshIndex < model.meshes.size();
         ++renderMeshIndex)
    {
        const std::optional<std::size_t> sourceMeshIndex =
            motsArchLightingSourceMeshIndex(model, renderMeshIndex);
        if (!sourceMeshIndex || *sourceMeshIndex >= object.meshes.size())
        {
            return nullptr;
        }

        const auto& archiveMesh = object.meshes[*sourceMeshIndex];
        const auto& modelMesh = model.meshes[renderMeshIndex];
        if (archiveMesh.vertices.size() < modelMesh.vertices.size())
        {
            return nullptr;
        }

        for (const MotsArchLightVertex& vertex : archiveMesh.vertices)
        {
            if (!std::isfinite(vertex.mono)
                || !std::isfinite(vertex.red)
                || !std::isfinite(vertex.green)
                || !std::isfinite(vertex.blue))
            {
                return nullptr;
            }
        }
    }

    return &object;
}

std::optional<MotsArchLightObject> makeDefaultMotsArchLightingPreview(
    const assets::Model3do& model)
{
    return level::makeDefaultMotsArchLightingPreview(model);
}

std::uint64_t motsArchLightingRevision(
    const assets::Model3do& model,
    const MotsArchLightObject& archLighting) noexcept
{
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t revision = kOffset;

    const auto appendInteger = [&revision](const std::size_t value) noexcept
    {
        std::uint64_t bits = static_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < sizeof(bits); ++i)
        {
            revision ^= static_cast<std::uint8_t>(bits & 0xffu);
            revision *= kPrime;
            bits >>= 8u;
        }
    };
    const auto appendFloat = [&revision](const float value) noexcept
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t i = 0; i < sizeof(bits); ++i)
        {
            revision ^= static_cast<std::uint8_t>(bits >> (i * 8u));
            revision *= kPrime;
        }
    };

    appendInteger(model.meshes.size());
    appendInteger(model.meshSourceIndices.size());
    appendInteger(archLighting.meshes.size());
    for (std::size_t renderMeshIndex = 0;
         renderMeshIndex < model.meshes.size();
         ++renderMeshIndex)
    {
        const std::optional<std::size_t> sourceMeshIndex =
            motsArchLightingSourceMeshIndex(model, renderMeshIndex);
        appendInteger(
            sourceMeshIndex.value_or(std::numeric_limits<std::size_t>::max()));

        const auto& modelMesh = model.meshes[renderMeshIndex];
        appendInteger(modelMesh.vertices.size());

        if (!sourceMeshIndex || *sourceMeshIndex >= archLighting.meshes.size())
        {
            appendInteger(0);
            continue;
        }

        const auto& archiveMesh = archLighting.meshes[*sourceMeshIndex];
        appendInteger(archiveMesh.vertices.size());
        for (const MotsArchLightVertex& vertex : archiveMesh.vertices)
        {
            appendFloat(vertex.mono);
            appendFloat(vertex.red);
            appendFloat(vertex.green);
            appendFloat(vertex.blue);
        }
    }
    return revision;
}

SourceLightChannels sourceLightChannels(const SedLight& light) noexcept
{
    SourceLightChannels channels{};
    channels.white = static_cast<float>(light.intensity);
    const float rgbIntensity =
        static_cast<float>(light.effectiveRgbIntensity());
    channels.rgb = ColorF{
        light.color.r * rgbIntensity,
        light.color.g * rgbIntensity,
        light.color.b * rgbIntensity,
        light.color.a };
    channels.rgbExplicit = light.rgbIntensity.has_value();
    return channels;
}
ColorF motsArchLightingVertexColor(
    const ColorF& modelColor,
    const MotsArchLightVertex& archiveVertex,
    const level::MotsArchLightingVertexMode mode) noexcept
{
    ColorF color = modelColor;
    if (mode == level::MotsArchLightingVertexMode::Mono)
    {
        color.r = archiveVertex.mono;
        color.g = archiveVertex.mono;
        color.b = archiveVertex.mono;
    }
    else if (mode == level::MotsArchLightingVertexMode::Rgb)
    {
        color.r = archiveVertex.red;
        color.g = archiveVertex.green;
        color.b = archiveVertex.blue;
    }
    return color;
}

QVector3D classicSectorTintScale(const ColorF& tint) noexcept
{
    // sithPlayer_SetScreenTint clamps the camera sector's authored tint before
    // rdCache derives its per-channel color-effect scalars.
    const float tr = std::clamp(tint.r, 0.0f, 1.0f);
    const float tg = std::clamp(tint.g, 0.0f, 1.0f);
    const float tb = std::clamp(tint.b, 0.0f, 1.0f);

    return QVector3D(
        1.0f + (tr - 0.5f * (tg + tb)),
        1.0f + (tg - 0.5f * (tr + tb)),
        1.0f + (tb - 0.5f * (tg + tr)));
}

QVector3D classicCameraSectorTintScale(
    const Level* level,
    const std::int32_t cameraSector) noexcept
{
    if (!level || level->kind == ProjectType::IJIM
        || cameraSector < 0
        || static_cast<std::size_t>(cameraSector) >= level->sectors.size())
    {
        return QVector3D(1.0f, 1.0f, 1.0f);
    }
    return classicSectorTintScale(level->sectors[static_cast<std::size_t>(cameraSector)].tint);
}

QVector3D classicEditorSectorTintScale(
    const Level* level,
    const std::int32_t sector) noexcept
{
    if (!level || level->kind == ProjectType::IJIM
        || sector < 0
        || static_cast<std::size_t>(sector) >= level->sectors.size())
    {
        return QVector3D(1.0f, 1.0f, 1.0f);
    }
    return classicSectorTintScale(
        level->sectors[static_cast<std::size_t>(sector)].tint);
}

QVector3D classicEditorSurfaceTintScale(
    const Level* level,
    const std::int32_t ownerSector,
    const Surface&) noexcept
{
    return classicEditorSectorTintScale(level, ownerSector);
}

float classicFaceOpacity(
    const ProjectType project,
    const std::uint32_t faceFlags,
    const float authoredOpacity) noexcept
{
    if (project != ProjectType::IJIM
        && (faceFlags & FF_Transluent) != 0u)
    {
        // rdCache assigns 90 to every hardware vertex of a classic
        // RD_FF_TEX_TRANSLUCENT face before material alpha modulation.
        return 90.0f / 255.0f;
    }

    return authoredOpacity;
}

bool classicEditorShowsTranslucentAdjoinBackface(
    const ProjectType project,
    const bool gamePreview,
    const bool levelSurface,
    const std::uint32_t faceFlags,
    const bool textureHasAlpha) noexcept
{
    return project != ProjectType::IJIM
        && !gamePreview
        && levelSurface
        && (faceFlags & FF_RD_Adjoin) != 0u
        && (((faceFlags & FF_Transluent) != 0u) || textureHasAlpha);
}

bool shouldDiscardAdjoinBackface(
    const bool gamePreview,
    const bool levelSurface,
    const std::uint32_t faceFlags) noexcept
{
    return gamePreview
        && levelSurface
        && (faceFlags & FF_RD_Adjoin) != 0u;
}

namespace
{

constexpr long long kLegacyGammaMinTenths = 1;
constexpr long long kLegacyGammaMaxTenths = 2550;

long long legacyGammaTenths(const double gamma) noexcept
{
    if ( !std::isfinite(gamma) )
    {
        return 10;
    }
    const double clamped = std::clamp(gamma, kLegacyPreviewTextureGammaMin, kLegacyPreviewTextureGammaMax);
    return std::clamp(std::llround(clamped * 10.0), kLegacyGammaMinTenths, kLegacyGammaMaxTenths);
}

} // namespace

double sanitizeLegacyPreviewTextureGamma(const double gamma) noexcept
{
    return static_cast<double>(legacyGammaTenths(gamma)) / 10.0;
}

double stepLegacyPreviewTextureGamma(const double gamma, const int steps) noexcept
{
    const long long tenths = std::clamp(legacyGammaTenths(gamma) + static_cast<long long>(steps),
        kLegacyGammaMinTenths, kLegacyGammaMaxTenths);
    return static_cast<double>(tenths) / 10.0;
}

float effectivePreviewTextureGamma(const bool gamePreview, const double gamma) noexcept
{
    return gamePreview ? static_cast<float>(sanitizeLegacyPreviewTextureGamma(gamma)) : 1.0f;
}

float applyLegacyTextureGamma(const float channel, const float gamma) noexcept
{
    return std::min(channel * gamma, 1.0f);
}

bool classicBatchUsesCameraSectorTint(
    const ProjectType project,
    const bool gamePreview,
    const bool,
    const std::uint32_t) noexcept
{
    return project != ProjectType::IJIM && gamePreview;
}

bool classicBatchUsesLocalSectorTint(
    const ProjectType project,
    const bool gamePreview,
    const bool,
    const std::uint32_t) noexcept
{
    return project != ProjectType::IJIM
        && !gamePreview;
}

QVector3D applyClassicSectorTint(
    const QVector3D& completedColor,
    const QVector3D& tintScale) noexcept
{
    return QVector3D(
        std::clamp(completedColor.x() * tintScale.x(), 0.0f, 1.0f),
        std::clamp(completedColor.y() * tintScale.y(), 0.0f, 1.0f),
        std::clamp(completedColor.z() * tintScale.z(), 0.0f, 1.0f));
}

ColorF polyCalcLitColor(
    const ColorF& color,
    const std::int32_t polyLightMode,
    const ColorF& ambientLight,
    const ColorF& extraLight,
    const ProjectType project,
    const TLightMode maxLightMode,
    const bool deferRgbClamp) noexcept
{
    ColorF result = ColorWhite;
    result.a = color.a; // IJIM preserves authored alpha even when the light mode bypasses vertex light.

    TLightMode lm = static_cast<TLightMode>(polyLightMode);
    if (static_cast<std::int32_t>(lm) > static_cast<std::int32_t>(maxLightMode))
    {
        lm = maxLightMode;
    }

    if (lm == TLightMode::VertexDiffuseOnly)
    {
        return color;
    }

    if (static_cast<std::int32_t>(lm) >= static_cast<std::int32_t>(TLightMode::Lit))
    {
        // rdCache starts every Lit/Diffuse/Gouraud polygon with face extra
        // light (plus IJIM camera ambient for 3DOs). Only Gouraud adds the
        // authored per-vertex intensity. In JK/MotS the sector value remains
        // the lower clamp, while signed face values can dim authored Gouraud
        // light without changing that sector floor.
        if (project == ProjectType::IJIM)
        {
            result = ambientLight + extraLight;
        }
        else
        {
            // Classic dynamic world/model paths must clamp authored+dynamic
            // before adding signed face extra light, so both extra and the
            // ambient floor are carried separately to the shader.
            result = deferRgbClamp ? ColorBlack : extraLight;
        }

        if (lm == TLightMode::Gouraud)
        {
            const float prevA = result.a;
            result = result + color;
            if (prevA >= 1.0f)
            {
                result.a = color.a;
            }
        }

        if (project != ProjectType::IJIM && !deferRgbClamp)
        {
            // Static JK/MotS paths complete on the CPU. Sector Tint is not
            // local lighting; the camera sector applies it once after drawing.
            result.clamp(ambientLight, ColorWhite);
        }

    }

    if (deferRgbClamp)
    {
        // Original 3DO lighting adds dynamic terms before rdCache clamps the
        // completed vertex. Preserve signed/over-range RGB for that GPU step,
        // but alpha has no dynamic contribution and is still finalized here.
        result.a = std::clamp(result.a, 0.0f, 1.0f);
    }
    else
    {
        result.normalize();
    }
    return result;
}

QVector3D calcThingLightMul(
    const ColorF& fromAmbient,
    const ColorF& toAmbient) noexcept
{
    QVector3D mul(
        safeLightRatio(toAmbient.r, fromAmbient.r),
        safeLightRatio(toAmbient.g, fromAmbient.g),
        safeLightRatio(toAmbient.b, fromAmbient.b));

    mul.setX(std::clamp(mul.x(), kThingLightMulMin, kThingLightMulMax));
    mul.setY(std::clamp(mul.y(), kThingLightMulMin, kThingLightMulMax));
    mul.setZ(std::clamp(mul.z(), kThingLightMulMin, kThingLightMulMax));
    return mul;
}

ThingSectorLighting calcThingSectorLighting(const Level* level, const std::int32_t sectorIndex) noexcept
{
    ThingSectorLighting out{};
    out.ambientLight = ColorWhite;
    out.ambientLight.a = 0.0f;

    if (!level || sectorIndex < 0 || static_cast<std::size_t>(sectorIndex) >= level->sectors.size())
    {
        return out;
    }

    const Sector& sec = level->sectors[static_cast<std::size_t>(sectorIndex)];

    const ColorF ambientAndExtra = sec.ambient + sec.extraLight;
    if (level->kind == ProjectType::JKDF2 || level->kind == ProjectType::MOTS)
    {
        // The original JKDF2/MotS Thing renderer accepts one scalar ambient
        // value per sector. Sector ambient can be derived from RGB baked
        // vertices in SED, so reduce it back to scalar before rendering Things
        // and sprites; otherwise a colored surface bake incorrectly tints them.
        const float scalar = std::clamp(ambientAndExtra.intensity(), 0.0f, 1.0f);
        out.ambientLight = ColorF{ scalar, scalar, scalar, 0.0f };
    }
    else
    {
        // IJIM has RGB sector lighting and uses the authored channels directly.
        out.ambientLight = ambientAndExtra;
        out.ambientLight.a = 0.0f;
        out.ambientLight.normalize();
    }

    return out;
}

QVector3D calcThingSectorLightingMul(const Level* level, const std::int32_t fromSector, const std::int32_t toSector) noexcept
{
    if (!level || fromSector == toSector)
    {
        return QVector3D(1.0f, 1.0f, 1.0f);
    }

    const ThingSectorLighting a = calcThingSectorLighting(level, fromSector);
    const ThingSectorLighting b = calcThingSectorLighting(level, toSector);
    return calcThingLightMul(a.ambientLight, b.ambientLight);
}

bool shouldRenderIjimFog(
    const ProjectType project,
    const Fog& fog,
    const bool viewportFogEnabled,
    const std::uint32_t faceFlags) noexcept
{
    return viewportFogEnabled
        && project == ProjectType::IJIM
        && fog.enabled
        && ((faceFlags & FF_IJIM_FogEnabled) != 0u);
}

std::uint32_t levelSurfaceRenderFaceFlags(
    const ProjectType project,
    const std::int32_t surfaceFlags,
    const std::uint32_t faceFlags) noexcept
{
    if (project != ProjectType::IJIM)
    {
        return faceFlags;
    }

    if ((surfaceFlags & (SF_SkyHorizon | SF_SkyCeiling)) != 0)
    {
        return faceFlags;
    }

    return faceFlags | FF_IJIM_FogEnabled;
}

std::uint32_t modelFaceRenderFaceFlags(const ProjectType project, const std::uint32_t faceFlags) noexcept
{
    if (project != ProjectType::IJIM)
    {
        return faceFlags;
    }

    return faceFlags | FF_IJIM_FogEnabled;
}

float ijimFogTableStart(const Fog& fog) noexcept
{
    if (!std::isfinite(fog.fogStart))
    {
        return 0.0f;
    }
    return static_cast<float>(fog.fogStart) * kIjimFogTableScale;
}

float ijimFogTableEnd(const Fog& fog, const float density) noexcept
{
    if (!std::isfinite(fog.fogEnd) || !std::isfinite(density))
    {
        return 0.0f;
    }
    return ((2.0f - density) * static_cast<float>(fog.fogEnd)) * kIjimFogTableScale;
}

float ijimLinearTableFogAmount(const float cameraDistance, const Fog& fog, const float density) noexcept
{
    if (density == 0.0f || !std::isfinite(cameraDistance))
    {
        return 0.0f;
    }

    const float tableStart = ijimFogTableStart(fog);
    const float tableEnd = ijimFogTableEnd(fog, density);
    const float tableSpan = tableEnd - tableStart;
    if (!std::isfinite(tableSpan) || tableSpan <= 1.0e-6f)
    {
        return (cameraDistance >= tableEnd) ? 1.0f : 0.0f;
    }

    return std::clamp((cameraDistance - tableStart) / tableSpan, 0.0f, 1.0f);
}

std::string sectorRenderColormap(const Level& level, const Sector& sector)
{
    if (level.kind == ProjectType::IJIM)
    {
        return {};
    }

    // D-05: MAT texture decoding always uses the level's master palette
    // (World Colormap 0, else dflt.cmp). A sector colormap supplies only
    // that sector's light ramp, never the texture palette, and a later
    // sector's CMP is never substituted for the master.
    const std::string masterCmp = level.getMasterCMP();
    const std::string_view sectorCmp = text::trimView(masterCmp);
    if (!sectorCmp.empty() && sectorCmp != "-")
    {
        return std::string(sectorCmp);
    }

    return level.getMasterCMP();
}

std::string makeBatchKey(const std::string_view material, const std::string_view colormap)
{
    std::string key;
    key.reserve(material.size() + 1 + colormap.size());
    key.append(material);
    key.push_back('|');
    key.append(colormap);
    return key;
}

} // namespace sed::render
