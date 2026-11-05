#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <QVector3D>

#include "sed/level.hpp"
#include "sed/level/arch_lighting.hpp"

namespace sed::assets
{
struct Model3do;
class Templates;
} // namespace sed::assets

namespace sed::render
{

inline constexpr std::string_view kThingIconMatPrefix = "__thing_icon__:";

/**
 * Returns the authored/source mesh ordinal consumed by one published 3DO
 * render mesh.  Expanded hierarchy clones retain their source ordinal in
 * Model3do::meshSourceIndices; raw and simple synthetic models use ordinal
 * identity.  An expanded hierarchy without that metadata is rejected because
 * its clone/source relationship cannot be recovered safely after loading.
 */
std::optional<std::size_t> motsArchLightingSourceMeshIndex(
    const assets::Model3do& model,
    std::size_t renderMeshIndex) noexcept;

/**
 * Sector lighting data used to recolor cached Thing vertices when a Thing moves
 * between sectors or a sector's ambient values change. Local classic Sector
 * Tint is carried independently by the editor vertex payload.
 */
struct ThingSectorLighting
{
    ColorF ambientLight{ ColorWhite };
};

/**
 * Resolves one eligible MotS Thing's ARCHLIGHTING data for a cached 3DO.
 * Returns no preview when the project/index is ineligible or the archive and
 * model topology do not match exactly.
 */
const MotsArchLightObject* resolveMotsArchLightingPreview(
    const Level& level,
    const Thing& thing,
    const assets::Model3do& model) noexcept;

/** Resolves the same preview while including mounted template inheritance. */
const MotsArchLightObject* resolveMotsArchLightingPreview(
    const Level& level,
    const assets::Templates* templates,
    const Thing& thing,
    const assets::Model3do& model) noexcept;

/** Builds a transient zero-valued ARCHLIGHTING payload in authored source-mesh
 * order. Compacted models retain empty slots for unused authored sources, and
 * hierarchy clones share one source record. The caller must first verify that
 * the MotS Thing has an effective Arch flag and no explicit record. This is
 * used by the editor preview and vertex editor for the virtual all-zero default
 * without mutating the document.
 */
std::optional<MotsArchLightObject> makeDefaultMotsArchLightingPreview(
    const assets::Model3do& model);

/**
 * Returns a stable revision for the model/ARCH mesh prefix consumed by the
 * renderer.  It includes topology and authored channel bits so an immutable
 * selected-vertex span can reject a changed table before it is handed to the
 * editor.  The function performs no resource lookup.
 */
std::uint64_t motsArchLightingRevision(
    const assets::Model3do& model,
    const MotsArchLightObject& archLighting) noexcept;

/**
 * Independent white/RGB channels of one editor source light (F-03(a)).
 * `white` is the authored intensity; `rgb` is the light colour scaled by
 * the effective RGB intensity, where an absent RGB intensity inherits the
 * white intensity and an explicit zero stays zero. This mirrors the core
 * bake split in `arch_authoring.cpp`; it invents no new falloff.
 */
struct SourceLightChannels
{
    float white{};
    ColorF rgb{ ColorBlack };
    bool rgbExplicit{ false };
};

/** Splits one source light into its independent white/RGB channels. */
SourceLightChannels sourceLightChannels(const SedLight& light) noexcept;
/** Maps one validated archive vertex through the engine's mono/RGB path. */
ColorF motsArchLightingVertexColor(
    const ColorF& modelColor,
    const MotsArchLightVertex& archiveVertex,
    level::MotsArchLightingVertexMode mode) noexcept;

/**
 * Computes the lit vertex color for one rendered polygon.
 *
 * This mirrors the viewport's legacy JK/MOTS/IJIM lighting approximation while
 * keeping the calculation outside GlView so mesh rebuild and fast-update paths
 * share exactly the same behavior. Lit, Diffuse and Gouraud all receive face
 * extra light, while only Gouraud receives authored vertex RGB. Dynamic model
 * and world-surface callers can defer project-specific RGB completion until
 * after GPU light accumulation, matching each renderer's ordering; alpha is
 * always normalized here.
 */
ColorF polyCalcLitColor(
    const ColorF& color,
    std::int32_t polyLightMode,
    const ColorF& ambientLight,
    const ColorF& extraLight,
    ProjectType project,
    TLightMode maxLightMode,
    bool deferRgbClamp = false) noexcept;

/** Calculates an RGB multiplier for recoloring one thing lighting cache into another. */
QVector3D calcThingLightMul(
    const ColorF& fromAmbient,
    const ColorF& toAmbient) noexcept;

/** Returns the normalized Thing ambient for one sector, or neutral lighting for invalid input. */
ThingSectorLighting calcThingSectorLighting(const Level* level, std::int32_t sectorIndex) noexcept;

/** Returns the color multiplier needed when a thing moves between two sectors. */
QVector3D calcThingSectorLightingMul(const Level* level, std::int32_t fromSector, std::int32_t toSector) noexcept;

/** JK/MotS screen-tint multiplier for a clamped JKL Sector Tint color. */
QVector3D classicSectorTintScale(const ColorF& tint) noexcept;

/**
 * Returns the one frame-wide tint selected by the camera sector.
 * IJIM, an invalid sector, and a missing level are always neutral.
 */
QVector3D classicCameraSectorTintScale(const Level* level, std::int32_t cameraSector) noexcept;

/** Returns the authored tint for one classic Sector in the normal editor viewport. */
QVector3D classicEditorSectorTintScale(const Level* level, std::int32_t sector) noexcept;

/**
 * Returns the local tint for one classic surface from its owning Sector.
 * An adjoin never borrows its mirror Sector's Tint because that would recolor
 * the surface MAT before the camera enters the mirror Sector. IJIM and invalid
 * owners remain neutral.
 */
QVector3D classicEditorSurfaceTintScale(
    const Level* level,
    std::int32_t ownerSector,
    const Surface& surface) noexcept;

/**
 * Resolves polygon opacity for the classic Face translucent flag. JK and MotS
 * use the renderer's fixed 90/255 alpha; other faces and IJIM retain the
 * caller-provided opacity.
 */
float classicFaceOpacity(
    ProjectType project,
    std::uint32_t faceFlags,
    float authoredOpacity) noexcept;

/**
 * Returns whether Editor should show a classic translucent adjoin from its
 * back side for convenient portal inspection from either side. Game Preview
 * retains the original engine's normal face culling.
 */
bool classicEditorShowsTranslucentAdjoinBackface(
    ProjectType project,
    bool gamePreview,
    bool levelSurface,
    std::uint32_t faceFlags,
    bool textureHasAlpha) noexcept;

/** D-06: only Game Preview discards back-facing fragments of adjoined world surfaces. */
bool shouldDiscardAdjoinBackface(
    bool gamePreview,
    bool levelSurface,
    std::uint32_t faceFlags) noexcept;

/**
 * P-16: Pascal SED's 3D Preview "Gamma" option. It is a texel multiplier,
 * min(c * gamma, 255), stepped by 0.1 between 0.1 and 255 (U_PREVIEW.PAS
 * VK_ADD/VK_SUBTRACT, GRAPH_FILES.PAS addGamma). It is presentation-only.
 */
inline constexpr double kLegacyPreviewTextureGammaDefault = 1.0;
inline constexpr double kLegacyPreviewTextureGammaMin = 0.1;
inline constexpr double kLegacyPreviewTextureGammaMax = 255.0;

/** Returns a finite gamma clamped to the legacy range and rounded to its 0.1 step. */
double sanitizeLegacyPreviewTextureGamma(double gamma) noexcept;

/** Moves gamma by `steps` legacy 0.1 increments, staying within the legacy range. */
double stepLegacyPreviewTextureGamma(double gamma, int steps) noexcept;

/** Game Preview uses the sanitized gamma; Editor views always use 1. */
float effectivePreviewTextureGamma(bool gamePreview, double gamma) noexcept;

/** CPU mirror of the shader rule: one texel channel scaled by gamma and saturated. */
float applyLegacyTextureGamma(float channel, float gamma) noexcept;

/**
 * Chooses the camera-wide classic Tint path for one draw batch. Editor batches
 * always retain their local vertex Tint. Game Preview uses the camera
 * Sector for every scene batch.
 */
bool classicBatchUsesCameraSectorTint(
    ProjectType project,
    bool gamePreview,
    bool levelSurface,
    std::uint32_t faceFlags) noexcept;

/** Returns whether one scene batch uses its local per-vertex classic Tint. */
bool classicBatchUsesLocalSectorTint(
    ProjectType project,
    bool gamePreview,
    bool levelSurface,
    std::uint32_t faceFlags) noexcept;

/** Applies the classic post-light, pre-texture vertex tint and output clamp. */
QVector3D applyClassicSectorTint(const QVector3D& completedColor, const QVector3D& tintScale) noexcept;

/**
 * Direct3D table-fog scale used by the original IJIM preview renderer.
 *
 * The old Pascal code configured D3D/GL linear fog with only the fog start/end
 * values multiplied by this value. The modern shader mirrors that setup by
 * comparing raw camera range against these scaled table limits.
 */
inline constexpr float kIjimFogTableScale = 0.0311f;

/** Returns whether an IJIM surface batch should use level-header fog. */
bool shouldRenderIjimFog(ProjectType project, const Fog& fog, bool viewportFogEnabled, std::uint32_t faceFlags) noexcept;

/**
 * Adds renderer-only IJIM fog eligibility to ordinary level surfaces.
 *
 * The Pascal renderer marked every non-sky IJIM level polygon as foggable while
 * leaving horizon/ceiling sky polygons unfogged.
 */
std::uint32_t levelSurfaceRenderFaceFlags(ProjectType project, std::int32_t surfaceFlags, std::uint32_t faceFlags) noexcept;

/**
 * Adds renderer-only IJIM fog eligibility to 3DO/model faces.
 *
 * IJIM preview rendering marked model polys as foggable too, so thing models can
 * fade into the same table fog as level geometry.
 */
std::uint32_t modelFaceRenderFaceFlags(ProjectType project, std::uint32_t faceFlags) noexcept;

/** Converts level fog start into the old Direct3D table-fog coordinate space. */
float ijimFogTableStart(const Fog& fog) noexcept;

/** Converts level fog end/density into the old Direct3D table-fog coordinate space. */
float ijimFogTableEnd(const Fog& fog, float density = 1.0f) noexcept;

/** Computes the shader fog blend amount for a raw camera-space range distance. */
float ijimLinearTableFogAmount(float cameraDistance, const Fog& fog, float density = 1.0f) noexcept;

/**
 * Returns the CMP name used to decode level surface MATs for one sector:
 * always the level master palette (D-05); the sector argument is retained
 * for call-site clarity and never selects a sector palette for textures.
 */
std::string sectorRenderColormap(const Level& level, const Sector& sector);

/** Stable material/colormap key used for render batches. */
std::string makeBatchKey(std::string_view material, std::string_view colormap);

} // namespace sed::render
