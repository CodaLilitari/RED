#include "test_common.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"

#include <bit>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sed::tests
{
namespace
{

sed::Level makeLevel()
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;

    sed::SedLight plain{};
    plain.flags = 0x1;
    plain.position = sed::TVector{ 1.0, 2.0, 3.0 };
    plain.range = 2.0;
    plain.intensity = 4.0;
    plain.color = sed::ColorF{ 0.25f, 0.5f, 0.75f, 1.0f };
    level.lights.push_back(plain);

    sed::SedLight zero = plain;
    zero.rgbIntensity = 0.0;
    zero.legacyRgbRange = 0.0;
    level.lights.push_back(zero);

    sed::SedLight signedZero = plain;
    signedZero.rgbIntensity = -0.0;
    level.lights.push_back(signedZero);

    sed::MotsArchAuthoringEntry imported{};
    imported.thingIndex = 3;
    imported.origin = sed::MotsArchSampleOrigin::ImportedUnknown;
    imported.modelName = "arch01.3do";
    imported.sampleSha256Hex = "aa";
    level.motsArchAuthoring.push_back(imported);

    sed::MotsArchAuthoringEntry baked = imported;
    baked.thingIndex = 5;
    baked.origin = sed::MotsArchSampleOrigin::BakedSourcePointsV1;
    baked.bakeInputSha256Hex = "bb";
    baked.bakeProfile = "source-points-v1";
    level.motsArchAuthoring.push_back(baked);
    return level;
}

std::vector<sed::assets::SedOptionalSection> sectionsFor(const sed::Level& level)
{
    std::vector<sed::assets::SedOptionalSection> sections{};
    if (const auto rgb = sed::assets::makeSedLightRgbSection(level))
    {
        sections.push_back(*rgb);
    }
    if (const auto arch = sed::assets::makeSedArchAuthoringSection(level))
    {
        sections.push_back(*arch);
    }
    return sections;
}

bool sameBits(const double a, const double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

} // namespace

void runSedExtensionTests()
{
    const sed::Level level = makeLevel();

    // Round trip: sections ride the real SED container.
    const auto sections = sectionsFor(level);
    check(sections.size() == 2, "sed-ext: both sections built when data present");
    const std::vector<std::byte> bytes =
        sed::assets::saveToSedBytes(level, sections);
    sed::assets::SedFile file = sed::assets::loadSedFileBytes(bytes);
    check(sed::assets::applySedLightRgbSection(file.level, file.optionalSections),
        "sed-ext: light section applied");
    check(sed::assets::applySedArchAuthoringSection(file.level, file.optionalSections),
        "sed-ext: arch section applied");

    check(file.level.lights.size() == 3, "sed-ext: light count preserved");
    check(!file.level.lights[0].rgbIntensity.has_value()
            && !file.level.lights[0].legacyRgbRange.has_value(),
        "sed-ext: absent stays absent");
    check(file.level.lights[1].rgbIntensity.has_value()
            && sameBits(*file.level.lights[1].rgbIntensity, 0.0)
            && !std::signbit(*file.level.lights[1].rgbIntensity),
        "sed-ext: explicit zero stays explicit zero");
    check(file.level.lights[2].rgbIntensity.has_value()
            && sameBits(*file.level.lights[2].rgbIntensity, -0.0)
            && std::signbit(*file.level.lights[2].rgbIntensity),
        "sed-ext: signed zero survives");
    check(file.level.motsArchAuthoring == level.motsArchAuthoring,
        "sed-ext: provenance round-trips distinctly (imported stays imported)");

    // Byte-exact re-save.
    const std::vector<std::byte> bytes2 =
        sed::assets::saveToSedBytes(file.level, file.optionalSections);
    check(bytes2 == bytes, "sed-ext: save/load/save byte-identical");

    // Old-file behaviour: no sections -> nothing applied, nothing invented.
    const std::vector<std::byte> oldBytes = sed::assets::saveToSedBytes(level);
    sed::assets::SedFile oldFile = sed::assets::loadSedFileBytes(oldBytes);
    check(!sed::assets::applySedLightRgbSection(oldFile.level, oldFile.optionalSections)
            && !sed::assets::applySedArchAuthoringSection(
                oldFile.level, oldFile.optionalSections),
        "sed-ext: missing sections apply nothing");
    check(!oldFile.level.lights[1].rgbIntensity.has_value()
            && oldFile.level.motsArchAuthoring.empty(),
        "sed-ext: missing data is absent, never invented");

    // No RGB data -> no section (old bytes unchanged).
    sed::Level plainLevel = makeLevel();
    for (sed::SedLight& light : plainLevel.lights)
    {
        light.rgbIntensity.reset();
        light.legacyRgbRange.reset();
    }
    plainLevel.motsArchAuthoring.clear();
    check(!sed::assets::makeSedLightRgbSection(plainLevel).has_value()
            && !sed::assets::makeSedArchAuthoringSection(plainLevel).has_value(),
        "sed-ext: no data produces no section");

    // Count mismatch is rejected, not partially applied.
    sed::Level fewer = makeLevel();
    fewer.lights.pop_back();
    bool rejected{};
    try
    {
        (void)sed::assets::applySedLightRgbSection(fewer, sections);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    check(rejected, "sed-ext: count mismatch rejected");
    check(fewer.lights[1].rgbIntensity.has_value()
            && sameBits(*fewer.lights[1].rgbIntensity, 0.0),
        "sed-ext: rejected section leaves level untouched");

    // Duplicate and malformed payloads are rejected.
    std::vector<sed::assets::SedOptionalSection> duplicate = sections;
    duplicate.push_back(sections.front());
    bool duplicateRejected{};
    try
    {
        sed::Level copy = makeLevel();
        (void)sed::assets::applySedLightRgbSection(copy, duplicate);
    }
    catch (const std::invalid_argument&)
    {
        duplicateRejected = true;
    }
    check(duplicateRejected, "sed-ext: duplicate section rejected");

    std::vector<sed::assets::SedOptionalSection> truncated = sections;
    truncated.front().payload.pop_back();
    bool malformedRejected{};
    try
    {
        sed::Level copy = makeLevel();
        (void)sed::assets::applySedLightRgbSection(copy, truncated);
    }
    catch (const std::invalid_argument&)
    {
        malformedRejected = true;
    }
    check(malformedRejected, "sed-ext: truncated payload rejected");
}

} // namespace sed::tests
