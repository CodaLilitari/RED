#include "test_common.hpp"

#include "sed/level/build.hpp"
#include "sed/level/cleave.hpp"
#include "sed/level/prefab.hpp"

#include <algorithm>
#include <cmath>

#include <array>
#include <cstdint>
#include <vector>

namespace sed::tests
{
namespace
{

std::int32_t addBox(Level& level, const TVector& minCorner, const TVector& maxCorner)
{
    return level::levelCreateSectorPrimitiveBox(level, level::SectorPrimitive::Cube, minCorner, maxCorner);
}

void testPrefabRoundTrip()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(level, 0, 1), "prefab setup adjoins the pair");
    (void)addBox(level, TVector::Create(5, 5, 5), TVector::Create(6, 6, 6));

    const std::array<std::int32_t, 2> selection{ 0, 1 };
    const auto prefab = level::makePrefab(level, selection, 0);
    check(prefab.has_value(), "Prefab captures the selected sectors");
    if (!prefab)
    {
        return;
    }
    check(prefab->version == level::kPrefabVersion
            && prefab->sourceProject == level.kind
            && prefab->payload.sectors.size() == 2u,
        "Prefab records its version, project, and sectors");
    check(level::prefabHasValidHash(*prefab), "Prefab hash verifies its payload");
    check(prefab->attachments.size() == 10u,
        "Prefab of an adjoined pair records the ten unadjoined faces");

    Level target{};
    const auto inserted = level::insertPrefab(target, *prefab);
    check(inserted && inserted->indices.size() == 2u && target.sectors.size() == 2u,
        "Prefab inserts into an empty level");
    if (inserted && inserted->indices.size() == 2u)
    {
        std::size_t adjoins = 0;
        for (const Surface& surface : target.sectors[0].surfaces)
        {
            if (surface.adjoin)
            {
                ++adjoins;
            }
        }
        check(adjoins >= 1u, "Prefab keeps the internal adjoin");
    }

    const std::array<std::int32_t, 1> single{ 2 };
    const auto lone = level::makePrefab(level, single, 2);
    check(lone && lone->attachments.size() == lone->payload.sectors[0].surfaces.size(),
        "Prefab of an isolated sector records every face as a boundary");
}

void testPrefabRefusals()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::array<std::int32_t, 1> selection{ 0 };
    const auto prefab = level::makePrefab(level, selection, 0);
    check(prefab.has_value(), "prefab refusal setup captures one sector");
    if (!prefab)
    {
        return;
    }

    level::Prefab tampered = *prefab;
    tampered.payload.sectors[0].vertices[0].x += 1.0;
    check(!level::prefabHasValidHash(tampered), "Prefab hash detects a moved vertex");
    Level target{};
    check(!level::insertPrefab(target, tampered) && target.sectors.empty(),
        "Prefab insertion refuses a tampered payload");

    level::Prefab future = *prefab;
    future.version = level::kPrefabVersion + 1;
    check(!level::insertPrefab(target, future) && target.sectors.empty(),
        "Prefab insertion refuses an unsupported version");

    Level other{};
    other.kind = ProjectType::MOTS;
    check(!level::insertPrefab(other, *prefab) && other.sectors.empty(),
        "Prefab insertion refuses a different project");

    const std::array<std::int32_t, 1> missing{ 7 };
    check(!level::makePrefab(level, missing), "Prefab refuses an empty selection");
}

void testPrefabTextRoundTrip()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(level, 0, 1), "prefab text setup adjoins the pair");
    const std::array<std::int32_t, 2> selection{ 0, 1 };
    const auto prefab = level::makePrefab(level, selection, 0);
    check(prefab.has_value(), "prefab text setup captures sectors");
    if (!prefab)
    {
        return;
    }
    const auto text = level::savePrefabText(*prefab);
    check(text.has_value() && text->find("RED-PREFAB 1") == 0,
        "Prefab serializes to the versioned text format");
    if (!text)
    {
        return;
    }
    const auto loaded = level::loadPrefabFromText(*text);
    check(loaded.has_value(), "Prefab text loads back");
    if (loaded)
    {
        check(loaded->version == prefab->version
                && loaded->sourceProject == prefab->sourceProject
                && loaded->contentSha256Hex == prefab->contentSha256Hex
                && loaded->attachments == prefab->attachments
                && loaded->payload.sectors.size() == prefab->payload.sectors.size()
                && level::prefabHasValidHash(*loaded),
            "Prefab text round-trip preserves identity, hash, and attachments");
        Level target{};
        check(level::insertPrefab(target, *loaded) && target.sectors.size() == 2u,
            "Loaded prefab inserts with its internal adjoin");
    }
    std::string tampered = *text;
    const std::size_t dataAt = tampered.find("data ");
    check(dataAt != std::string::npos, "prefab text carries a data line");
    if (dataAt != std::string::npos)
    {
        tampered.resize(dataAt + 5 + 64);
        check(!level::loadPrefabFromText(tampered), "Prefab text refuses a truncated payload");
    }
    std::string hashTampered = *text;
    const std::size_t hashAt = hashTampered.find("sha256 ");
    if (hashAt != std::string::npos)
    {
        char& digit = hashTampered[hashAt + 7];
        digit = digit == '0' ? '1' : '0';
        check(!level::loadPrefabFromText(hashTampered),
            "Prefab text refuses a tampered content hash");
    }
    check(!level::loadPrefabFromText("not a prefab"),
        "Prefab text refuses a wrong header");
    check(!level::savePrefabText(level::Prefab{}),
        "Prefab text refuses an invalid prefab");
}

} // namespace

void testSectorArray()
{
    Level level{};
    (void)addBox(level, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    (void)addBox(level, TVector::Create(1, 0, 0), TVector::Create(2, 1, 1));
    check(level::levelConnectSectors(level, 0, 1), "sector array setup adjoins the pair");

    const std::array<std::int32_t, 2> selection{ 0, 1 };
    level::PlacementLayout layout{};
    layout.kind = level::PlacementLayoutKind::linear;
    layout.origin = TVector::Create(10, 0, 0);
    layout.step = TVector::Create(3, 0, 0);
    layout.count = 3;
    const auto built = level::levelBuildSectorArray(level, selection, layout);
    check(built && built->size() == 6u && level.sectors.size() == 8u,
        "Sector Array inserts one prefab copy per layout position");
    if (!built || built->size() != 6u)
    {
        return;
    }
    // The anchor is the primary sector's centre (0.5, 0.5, 0.5): the first
    // copy's first sector sits with its centre at the layout origin.
    const Sector& first = level.sectors[static_cast<std::size_t>((*built)[0])];
    double minX = first.vertices[0].x;
    for (const Vertex& vertex : first.vertices)
    {
        minX = std::min(minX, vertex.x);
    }
    check(std::abs(minX - 9.5) < 1e-9, "Sector Array anchors the first copy at the layout origin");
    std::size_t adjoins = 0;
    for (const Surface& surface : first.surfaces)
    {
        if (surface.adjoin)
        {
            ++adjoins;
        }
    }
    check(adjoins >= 1u, "Sector Array copies keep their internal adjoins");

    Level refused{};
    (void)addBox(refused, TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));
    const std::size_t before = refused.sectors.size();
    const std::array<std::int32_t, 1> one{ 0 };
    layout.step = TVector::Create(0, 0, 0);
    check(!level::levelBuildSectorArray(refused, one, layout) && refused.sectors.size() == before,
        "Sector Array refuses duplicate layout positions without adding sectors");
    layout.step = TVector::Create(3, 0, 0);
    const std::span<const std::int32_t> empty{};
    check(!level::levelBuildSectorArray(refused, empty, layout) && refused.sectors.size() == before,
        "Sector Array refuses an empty selection without adding sectors");
}

void runPrefabTests()
{
    testPrefabRoundTrip();
    testSectorArray();
    testPrefabRefusals();
    testPrefabTextRoundTrip();
}

} // namespace sed::tests
