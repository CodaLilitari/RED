#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "sed/color.hpp"
#include "sed/geometry.hpp"
#include "sed/values.hpp"

namespace sed
{

/** Raised when the Level-wide identity allocator has no representable ID left. */
class IdExhaustionError : public std::overflow_error
{
public:
    using std::overflow_error::overflow_error;
};

// Sector flags (from src/J_level.pas)
inline constexpr std::int32_t SECF_Underwater = 0x2;
inline constexpr std::int32_t SECF_CollideBox = 0x1000;
inline constexpr std::int32_t SECF_IJIM_UseThrust = 0x8;
inline constexpr std::int32_t SECF_IJIM_Aetherim = 0x100;
inline constexpr std::int32_t SECF_NORGBAMBINETLIGHT = 1 << 29;
inline constexpr std::int32_t SECF_NOAMBINETLIGHT = 1 << 30;
inline constexpr std::int32_t SECF_3DO =
    static_cast<std::int32_t>(0x80000000u);

// Surface flags
inline constexpr std::int32_t SF_Floor = 0x1;
inline constexpr std::int32_t SF_CogLinked = 0x2;
inline constexpr std::int32_t SF_Collision = 0x4;
inline constexpr std::int32_t SF_DoubleRes = 0x10;
inline constexpr std::int32_t SF_HalfRes = 0x20;
inline constexpr std::int32_t SF_EighthRes = 0x40;
inline constexpr std::int32_t SF_SkyHorizon = 0x200;
inline constexpr std::int32_t SF_SkyCeiling = 0x400;
inline constexpr std::int32_t SF_Water = 0x20000;
inline constexpr std::int32_t SF_QuaterRes = 0x4000000;
inline constexpr std::int32_t SF_QuadrupleRes = 0x8000000;

// IJIM Surface flags
inline constexpr std::int32_t SF_IJIM_Aetherim = 0x80;
inline constexpr std::int32_t SF_IJIM_KillFloor = 0x1000;
inline constexpr std::int32_t SF_IJIM_Climable = 0x2000;
inline constexpr std::int32_t SF_IJIM_Track = 0x4000;
inline constexpr std::int32_t SF_IJIM_Ledge = 0x1000000;
inline constexpr std::int32_t SF_IJIM_WaterLedge = 0x2000000;
inline constexpr std::int32_t SF_IJIM_WhipAim = 0x10000000;

// Surface adjoin flags
inline constexpr std::int32_t SAF_Visible = 0x01;
inline constexpr std::int32_t SAF_Move = 0x02;
inline constexpr std::int32_t SAF_AllowSoundPass = 0x04;
inline constexpr std::int32_t SAF_NoAiMove = 0x08;
inline constexpr std::int32_t SAF_NoPlayerMove = 0x10;
inline constexpr std::int32_t SAF_BlockLight = static_cast<std::int32_t>(0x80000000u);

// Light flags
inline constexpr std::int32_t LF_NoBlock = 0x01;
// SED's MotS-only baked-light extension for applying ARCHLIGHTING output.
inline constexpr std::int32_t LF_AffectArchThings = 0x20;

// Thing flags
inline constexpr std::int32_t TF_INVISIBLE = 0x10;
inline constexpr std::int32_t TF_NOEASY    = 0x1000;
inline constexpr std::int32_t TF_NOMEDIUM  = 0x2000;
inline constexpr std::int32_t TF_NOHARD    = 0x4000;
inline constexpr std::int32_t TF_NOMULTI   = 0x8000;
inline constexpr std::int32_t TF_DISABLED  = 0x80000;

enum class ProjectType : std::int32_t
{
    JKDF2 = 0,
    MOTS = 1,
    IJIM = 2,
    DroidWorks = 3
};

struct ThingBox : TBox
{
    using TBox::TBox;
};

struct SedLight
{
    std::int32_t id{};
    std::int32_t flags{};
    std::int32_t mark{};
    std::int32_t layer{};

    ColorF color{ ColorWhite };
    double intensity{ 10.0 };
    double range{ 2.0 };
    TVector position{};
    // Independent MotS RGB intensity. Absent inherits `intensity`;
    // an explicit value (including zero, or a value equal to
    // `intensity`) stays explicit. Only the JKL
    // SED_SOURCE_LIGHTS extension preserves this distinction.
    std::optional<double> rgbIntensity{};
    // Retained legacy RGB range metadata. Present or absent, it
    // never changes baking; the primary `range` drives both
    // channels. Preserved only by the same JKL extension.
    std::optional<double> legacyRgbRange{};

    /** RGB intensity with the inherit fallback applied. */
    double effectiveRgbIntensity() const noexcept
    {
        return rgbIntensity.value_or(intensity);
    }
};

struct HNode
{
    std::int32_t id{};
    std::int32_t num{};
    std::uint32_t type{ 1 };
    std::int32_t mesh{ -1 };
    std::int32_t parent{ -1 };

    TVector position{};
    TVector rotation{};
    TVector pivot{};

    std::string name{};
};

struct PointLight
{
    // A missing IJIM AVERAGE LIGHT block means no sector point light. The
    // runtime world loader zero-initializes this record before parsing it.
    ColorF color{ ColorBlack };
    TVector position{};
    double minRange{};
    double maxRange{};
};

/** Whether an IJIM sector owns any authored AVERAGE LIGHT parameter.
 *
 * The color alpha member is not part of the NDY fields and must not make the
 * default black PointLight active. Position is deliberately included: a
 * source at the world origin remains active when its RGB or falloff is
 * non-zero, while a position-only record is still preserved losslessly.
 */
[[nodiscard]] inline bool pointLightHasAuthoredParameters(const PointLight& light) noexcept
{
    return !light.color.isRGBZero()
        || !light.position.IsZero()
        || !sed::IsZero(light.minRange)
        || !sed::IsZero(light.maxRange);
}

struct Fog
{
    bool enabled{ false };
    ColorF color{ ColorWhite };
    double fogStart{};
    double fogEnd{};
};

struct CeilingSky
{
    double height{};
    TPoint2 offset{};
};

struct HorizonSky
{
    double distance{};
    double pixelsPerRev{};
    TPoint2 offset{};
};

struct LevelHeader
{
    std::int32_t version{};
    double gravity{};
    HorizonSky horizonSky{};
    CeilingSky ceilingSky{};
    std::array<double, 4> mipmapDistances{};
    std::array<double, 4> lodDistances{};
    double perspectiveDistance{};
    double gouraudDistance{};
    Fog fog{};
};

/**
 * Editable capacity beyond the required JKL/NDY world resource records.
 * Export writes `required + extra` exactly; zero therefore requests no spare
 * slots. New projects initialize these fields from project-specific,
 * user-configurable defaults, while imported engine levels infer them from
 * declared minus parsed counts.
 * Geometry/sector counts are intentionally absent because those section
 * headers describe exact record counts rather than growable resource pools.
 */
struct LevelExportBufferExtras
{
    std::uint32_t sounds{};
    std::uint32_t materials{};
    std::uint32_t aiClasses{};
    std::uint32_t models{};
    std::uint32_t sprites{};
    std::uint32_t keyframes{};
    std::uint32_t puppets{};
    std::uint32_t soundClasses{};
    std::uint32_t cogScripts{};
    std::uint32_t cogs{};
    std::uint32_t templates{};
    std::uint32_t things{};

    constexpr bool operator==(const LevelExportBufferExtras&) const noexcept = default;
};

struct TexVertex
{
    // The owning Level declares the unit used by u/v. New editable Levels use
    // normalized texture coordinates; a Level loaded from legacy SED V1 may
    // remain explicitly tagged as texture pixels until its MAT dimensions are
    // available for an exact migration.
    float u{};
    float v{};
    ColorF color{ ColorWhite };
};

enum class UvCoordinateSpace : std::uint8_t
{
    TexturePixels = 1,
    Normalized = 2,
};

struct Vertex
{
    double x{};
    double y{};
    double z{};
    std::int32_t mark{};
};

struct VertexRef
{
    std::int32_t sector{ -1 };
    std::int32_t vertex{ -1 };
};

struct SurfaceRef
{
    std::int32_t sector{ -1 };
    std::int32_t surface{ -1 };

    constexpr bool operator==(const SurfaceRef&) const noexcept = default;
};

struct Surface
{
    std::int32_t num{};
    std::int32_t mark{};
    std::int32_t d3dId{};

    std::int32_t surfaceFlags{};
    std::uint32_t faceFlags{};
    std::int32_t geo{};
    std::int32_t light{};
    std::int32_t tex{};
    ColorF extraLight{ ColorBlack };
    std::int32_t adjoinFlags{};
    std::optional<SurfaceRef> adjoin{};
    std::string material{};

    std::int32_t nAdj{};
    std::int32_t nMat{};
    float uScale{ 1.0f };
    float vScale{ 1.0f };

    TVector normal{};

    // Indices into owning sector vertex list.
    std::vector<std::int32_t> vertices;
    std::vector<TexVertex> texVertices;
};

struct Sector
{
    std::int32_t id{};
    std::int32_t num{};
    std::int32_t flags{};
    ColorF ambient{ ColorWhite };
    ColorF extraLight{ ColorBlack };
    PointLight pointLight{};
    std::string colormap{};
    // Exact slot of `colormap` in the owning Level::colormaps table.
    // -1 is the name-only authoring state: the writer resolves the name
    // to a unique slot (or refuses when the name is ambiguous) instead
    // of silently picking a match. A non-negative slot whose table
    // entry no longer names the same colormap is a mismatch and also
    // refuses export before any file is written.
    std::int32_t colormapIndex{ -1 };
    ColorF tint{ ColorWhite };
    std::string sound{};
    double soundVolume{};
    TVector thrust{};

    TBox collideBox{};
    std::int32_t layer{};
    std::int32_t mark{};

    std::vector<Vertex> vertices;
    std::vector<Surface> surfaces;

    std::int32_t findVertex(double x, double y, double z) const noexcept;
    std::int32_t newVertex(double x, double y, double z);
    std::int32_t addVertex(double x, double y, double z);
    std::int32_t addSurface(Surface surface);
};

/** One MotS ARCHLIGHTING vertex's authored monochrome and RGB channels. */
struct MotsArchLightVertex
{
    float mono{};
    float red{};
    float green{};
    float blue{};

    constexpr bool operator==(const MotsArchLightVertex&) const noexcept = default;
};

/** One ordered mesh record in a MotS ARCHLIGHTING object. */
struct MotsArchLightMesh
{
    std::int32_t wireIndex{};
    std::vector<MotsArchLightVertex> vertices{};

    constexpr bool operator==(const MotsArchLightMesh&) const noexcept = default;
};

/** One ordered MotS ARCHLIGHTING object referenced by a Thing. */
struct MotsArchLightObject
{
    std::int32_t wireIndex{};
    std::vector<MotsArchLightMesh> meshes{};

    constexpr bool operator==(const MotsArchLightObject&) const noexcept = default;
};

/** Original authored MotS ARCHLIGHTING retained while baked-light output is applied. */
struct MotsArchLightingBakeBaseline
{
    std::int32_t thingId{};
    std::string modelName{};
    std::int32_t originalArchLightingIndex{ -1 };
    std::optional<MotsArchLightObject> originalArchLighting{};

    constexpr bool operator==(const MotsArchLightingBakeBaseline&) const noexcept = default;
};

/** How the samples bound to one ARCH thing originated.
 *
 * Imported samples keep the ImportedUnknown state forever:
 * binding them produces BoundExistingSamples, never a bake
 * state. Only the authoring bake produces a Baked* state.
 * The names are deliberately distinct so no code path can
 * present imported data as an editor bake. */
enum class MotsArchSampleOrigin : std::uint8_t
{
    ImportedUnknown,
    BoundExistingSamples,
    ResetZeroed,
    BakedSourcePointsV1,
    BakedWorldOccludedV1,
};

/** Authoring provenance for one bound ARCH thing.
 *
 * `thingIndex` is the editor Thing position. Digests are
 * lowercase hex SHA-256 values; empty means not recorded
 * (no bake input for non-bake origins). */
struct MotsArchAuthoringEntry
{
    std::int32_t thingIndex{ -1 };
    MotsArchSampleOrigin origin{ MotsArchSampleOrigin::ImportedUnknown };
    std::string modelName{};
    std::string modelEntrySha256Hex{};
    std::string bakeInputSha256Hex{};
    std::string sampleSha256Hex{};
    std::string bakeProfile{};

    constexpr bool operator==(const MotsArchAuthoringEntry&) const noexcept = default;
};
struct Thing
{
    std::int32_t id{};
    std::int32_t num{};
    std::int32_t sector{ -1 };
    // MotS ARCHLIGHTING object index; -1 means that the Thing has no
    // architectural-light record. JKDF2 and IJIM leave this at -1.
    std::int32_t archLightingIndex{ -1 };
    // Name of the template/type for this thing (e.g. "+ripples", "shirtplayer").
    std::string templateName{};

    // Instance name (often matches templateName, but not always).
    std::string name{ "walkplayer" };
    double x{};
    double y{};
    double z{};
    double pch{};
    double yaw{};
    double rol{};
    std::vector<TplValue> values;
    std::int32_t layer{};
    ThingBox bbox{};
};

struct ThingTemplate
{
    std::string name{};
    std::string parent{};
    std::vector<TplValue> values{};
    std::string desc{};
    ThingBox bbox{};
};

struct Cog
{
    std::int32_t id{};
    std::int32_t num{};
    std::string name{};
    std::vector<CogValue> values;
};

struct Level
{
    LevelHeader header{};
    LevelExportBufferExtras exportBufferExtras{};
    ProjectType kind{ ProjectType::JKDF2 };
    UvCoordinateSpace uvCoordinateSpace{ UvCoordinateSpace::Normalized };
    // JKL/NDY surface arguments use GEORESOURCE numbering, which can differ
    // from the editor's flattened sector order.  Resource-independent imports
    // retain this transient map until COG schemas are hydrated.  It is not
    // part of the native SED payload; once values are resolved, their regular
    // typed indices are authoritative.
    std::vector<SurfaceRef> importedJklSurfaceRefs{};
    // A JKL COG row can legally omit every or only trailing initializers.
    // Keep this import-only marker so a resource-backed finalizer can append
    // script defaults even when the row produced no raw paramN values.
    bool importedJklCogRowsPresent{ false };
    // JKL/NDY source retained at import for the transactional writer.
    // `retainedJklSourceText` is the exact imported text and
    // `retainedJklSourceHash` is its lowercase hex SHA-256, both
    // captured by the loader. Export re-checks the text against the
    // hash before replacing any section and refuses (writing zero
    // bytes) when they disagree, so retained text must be refreshed
    // by reloading rather than edited in place. Both stay empty for
    // model-created levels, which export without a source baseline.
    // `retainedJklUnknownSections` holds the verbatim text of each
    // section the importer does not model, in source order, so an
    // open -> edit -> save round-trip does not silently drop it.
    std::string retainedJklSourceText{};
    std::string retainedJklSourceHash{};
    std::vector<std::string> retainedJklUnknownSections{};
    // Verbatim lines the JKL importer does not model but can delimit
    // safely inside modeled sections: unknown HEADER lines, and
    // unknown per-sector lines before the SURFACES record. Export
    // re-emits them in place; count-framed or record-internal extras
    // are not retained (see PORT_LEDGER F-00).
    std::vector<std::string> retainedJklHeaderUnknownLines{};
    std::vector<std::vector<std::string>> retainedJklSectorUnknownLines{};
    // Original GEORESOURCE wire-slot identity captured at JKL import
    // (F-06). Empty/-1 for model-created levels. The writer reuses
    // these slots when the editor topology still matches them and
    // otherwise regenerates, reporting the renumber on the result.
    std::vector<Vertex> importedGeoWorldVertices{};
    std::vector<TexVertex> importedGeoWorldTexVertices{};
    std::vector<std::uint32_t> importedGeoAdjoinFlags{};
    std::vector<std::int32_t> importedGeoAdjoinMirrors{};
    std::vector<std::vector<std::int32_t>> importedGeoVertexIndices{};
    std::vector<std::vector<std::int32_t>> importedGeoSurfaceIndices{};
    std::vector<std::vector<std::vector<std::int32_t>>> importedGeoTexVertexIndices{};
    std::vector<std::vector<std::int32_t>> importedGeoAdjoinIndices{};
    std::vector<std::vector<std::int32_t>> importedGeoMaterialIndices{};
    std::int32_t importedGeoSurfaceCount{ -1 };
    std::int32_t importedGeoAdjoinCount{ -1 };
    std::vector<Sector> sectors;
    std::vector<Cog> cogs;
    std::vector<ThingTemplate> templates;
    std::vector<Thing> things;
    // Ordered MotS ARCHLIGHTING objects. Thing::archLightingIndex addresses
    // this vector by editor order; SED preserves nonzero dormant records and
    // ambiguous external-template references, while JKL export writes only
    // active Arch Thing bindings and may synthesize the model-sized all-zero
    // default for an unbound Arch Thing.
    std::vector<MotsArchLightObject> motsArchLighting;
    // Original authored values retained while baked-light output is generated.
    // Full objects are copied because the active archive table may be compacted.
    std::vector<MotsArchLightingBakeBaseline> motsArchLightingBakeBaselines;
    // Per-thing ARCH authoring provenance (F-08 core). Empty for
    // imported bindings, whose origin stays unknown. Persisted
    // only by the JKL SED_ARCH_AUTHORING extension.
    std::vector<MotsArchAuthoringEntry> motsArchAuthoring;
    // True when the imported JKL carried a valid
    // SED_SOURCE_LIGHTS section, so a rewrite keeps the
    // section even after its overrides are cleared.
    bool hasSourceLightMetadata{ false };
    std::vector<SedLight> lights;
    std::vector<HNode> h3doNodes;
    std::vector<std::string> colormaps;
    std::vector<std::string> materials;
    std::vector<std::string> layers;

    std::string masterCMP{};
    double ppunit{ 320.0 };
    std::string lvisstring{};

    std::int32_t curId{ 1 };

    void clear();
    bool canAllocateIds(std::size_t count) const noexcept;
    std::int32_t newId();

    std::string getMasterCMP() const;
    std::string getLayerName(std::int32_t n) const;
    std::int32_t addLayer(const std::string& name);
    void addMissingLayers();

    /** Counts Sector, Thing, and editor-light owners for every declared
     * layer. References outside the declared layer array are intentionally
     * ignored; call addMissingLayers() first when normalizing imported data. */
    std::vector<std::size_t> layerUsageCounts() const;

    /** Atomically replaces and reindexes the layer table.
     *
     * oldToNew contains one destination index for every current layer. A -1
     * destination deletes an unused layer. Non-negative destinations must be
     * unique and in range of nextLayers. The operation fails without changing
     * the Level when an entity has a malformed layer reference, a referenced
     * layer would be deleted, or the mapping is otherwise invalid.
     */
    bool remapLayers(std::vector<std::string> nextLayers,
                     const std::vector<std::int32_t>& oldToNew);

    std::int32_t getGlobalSurfaceNumber(std::int32_t sectorIndex, std::int32_t surfaceIndex) const noexcept;
    std::optional<std::int32_t> tryGetSectorIndexByNum(std::int32_t sectorNumber) const noexcept;
    std::optional<std::int32_t> tryGetThingIndexByNum(std::int32_t thingNumber) const noexcept;
    std::optional<SurfaceRef> tryGetSurfaceRefBySectorAndSurfaceNum(std::int32_t sectorNumber, std::int32_t surfaceNumber) const noexcept;
    std::optional<SurfaceRef> tryGetSurfaceRefByGlobalSurfaceNumber(std::int32_t globalSurfaceNumber) const noexcept;

    /** Returns true while a resource-independent JKL import still contains
     * raw COG arguments whose schema has not been hydrated. */
    bool hasPendingJklCogValues() const noexcept;

    /** Returns true when pending raw COG values could be retargeted by the
     * noncanonical JKL GEORESOURCE-to-editor surface ordering. */
    bool hasAmbiguousPendingJklCogSurfaceReferences() const noexcept;

    Sector& newSector();
    Cog& newCog();
    Thing& newThing();
    SedLight& newLight();
    HNode& new3doNode();
};

} // namespace sed

namespace sed::level
{

using VertexRef = sed::VertexRef;

} // namespace sed::level
