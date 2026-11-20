#pragma once

#include "sed/assets/runtime_archive.hpp"
#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sed::assets
{

inline constexpr std::size_t kJoJaMaxMaterials = 256;
inline constexpr std::size_t kJoJaMaxCapturedInputBytes = 256u * 1024u * 1024u;
inline constexpr std::size_t kJoJaMaxMaterialBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kJoJaMaxCapturedMaterialBytes = 128u * 1024u * 1024u;
inline constexpr std::size_t kJoJaMaxPaletteBytes = 1024u * 1024u;

enum class JoJaAudioPolicy
{
    StockEquivalents,
    SourceImport,
};

enum class JoJaSourceProfile
{
    Legacy,
    JediOutcastMultiplayer,
    JediAcademyMultiplayer,
};

const char *joJaSourceProfileName(JoJaSourceProfile profile);

enum class JoJaGeometryMode
{
    ModelScene,
    HybridSectors,
};

/** One explicitly captured target material. Shader names are exact source names. */
struct JoJaMaterialInput
{
    std::string shader;
    std::string name;
    std::string bytes;
};

struct JoJaImportOptions
{
    ProjectType target{ProjectType::JKDF2};
    std::string levelName{"joja_scene"};
    double scale{1.0 / 320.0};
    std::uint32_t patchSubdivisions{4};
    std::uint32_t trianglesPerModel{140};
    std::size_t maxTriangles{100000};
    std::size_t maxModels{512};
    bool allowPlaceholders{false};
    bool renderMeshCollision{false};
    JoJaAudioPolicy audioPolicy{JoJaAudioPolicy::StockEquivalents};
    JoJaGeometryMode geometryMode{JoJaGeometryMode::ModelScene};
    std::size_t maxSectors{64};
    std::size_t maxFragments{500000};
    std::vector<JoJaMaterialInput> materials;
    std::vector<std::string> skipShaders;
    // Required whenever an input MAT is indexed. Captured CMP bytes, not a path.
    std::string palette;
    JoJaSourceProfile sourceProfile{JoJaSourceProfile::Legacy};
};

struct JoJaModelReport
{
    std::string name;
    std::size_t triangles{};
    TVector position;
    double radius{};
    std::vector<std::size_t> sourceSurfaces;
    std::string footstep;
    std::size_t sector{};
};

struct JoJaMaterialReport
{
    std::string shader;
    std::string name;
    std::string sha256;
    std::int32_t width{};
    std::int32_t height{};
    bool placeholder{};
    std::uint32_t sourceMaterial{};
    std::string footstep;
    bool footstepNeedsReview{};
    std::string sourceFootstep;
    bool noStepsFlag{};
};

struct JoJaImportResult
{
    RuntimeArchiveResult archive;
    std::string sourceSha256;
    std::string outputSha256;
    std::string manifestSha256;
    std::string report;
    std::vector<JoJaModelReport> models;
    std::vector<JoJaMaterialReport> materials;
    std::vector<std::string> warnings;
    std::size_t triangles{};
    std::size_t patches{};
    std::size_t skippedSurfaces{};
    std::size_t omittedSubmodels{};
    std::size_t sourceSurfaces{};
    std::size_t sourceShaders{};
    std::size_t sourceBrushes{};
    std::size_t sourceEntities{};
    std::size_t playerStarts{};
    std::size_t lightmapCorners{};
    std::size_t vertexLitCorners{};
    std::size_t sectors{1};
    std::size_t portalPairs{};
    std::size_t nativeFaces{};
    std::size_t residualTriangles{};
    std::size_t residualFaces{};
    std::size_t splitSourceTriangles{};
    std::size_t untrimmedBoundaryPlanes{};
    std::size_t nativePromotionFallbacks{};
    std::size_t targetCollapsedFragments{};
    double targetCollapsedArea{};
    double sourceArea{};
    double outputArea{};
    double maximumRelativeCoverageError{};
};

/** Portable scene generation from immutable captures. No I/O or game execution.
 * Throws before returning any output when admission or target readback fails.
 * The resulting archive is an authoring scaffold, not runtime acceptance. */
JoJaImportResult prepareJoJaImport(std::span<const std::byte> source, const JoJaImportOptions &options);

} // namespace sed::assets
