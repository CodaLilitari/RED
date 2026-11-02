#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/source_lights.hpp"

#include <bit>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>

namespace sed::tests
{
namespace
{

float floatFromBits(const std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

sed::Level makeLightLevel()
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;

    sed::SedLight inheritLight{};
    inheritLight.flags = 0x1;
    inheritLight.layer = 2;
    inheritLight.position = sed::TVector{ 1.0, 2.0, 3.0 };
    inheritLight.range = 2.0;
    inheritLight.intensity = 4.0;
    inheritLight.color = sed::ColorF{ 0.25f, 0.5f, 0.75f, 1.0f };
    level.lights.push_back(inheritLight);

    sed::SedLight zeroLight = inheritLight;
    zeroLight.rgbIntensity = 0.0;
    zeroLight.legacyRgbRange = 0.0;
    level.lights.push_back(zeroLight);

    sed::SedLight equalLight = inheritLight;
    equalLight.rgbIntensity = 4.0;
    level.lights.push_back(equalLight);

    sed::SedLight signedZeroLight = inheritLight;
    signedZeroLight.intensity = 2.0;
    signedZeroLight.rgbIntensity = -0.0;
    signedZeroLight.color = sed::ColorF{ 0.25f, 0.5f, -0.0f, 1.0f };
    level.lights.push_back(signedZeroLight);
    return level;
}

sed::Level reload(const std::string& text)
{
    std::istringstream input(text);
    sed::assets::JklLoadOptions options{};
    options.projectType = sed::ProjectType::MOTS;
    return sed::assets::loadFromJKL(input, options);
}

std::string save(const sed::Level& level)
{
    std::ostringstream output;
    sed::assets::saveToJKL(level, output);
    return output.str();
}

} // namespace

void runSourceLightMetadataTests()
{
    // Fixed checksum vector, re-derived from the canonical rules:
    // adjacent binary64 values, a negative-zero position, and
    // binary32 RGB bit patterns promoted to binary64.
    {
        sed::assets::SourceLightBaseRow row{};
        row.flags = 2147483649LL;
        row.layer = 2;
        row.x = 1.0;
        row.y = std::nextafter(1.0, 2.0);
        row.z = -0.0;
        row.range = std::nextafter(5.0, 6.0);
        row.intensity = 0.1;
        row.r = floatFromBits(0x3DCCCCCDu);
        row.g = floatFromBits(0x3DCCCCCEu);
        row.b = floatFromBits(0x80000000u);

        const std::string canonical =
            sed::assets::canonicalSourceLightsText({ &row, 1 });
        const std::string expected =
            "sed-source-light-base-v1\n"
            "1\n"
            "2147483649 2 1 1.0000000000000002 -0 5.000000000000001 "
            "0.1 0.10000000149011612 0.10000000894069672 -0\n";
        check(canonical == expected,
            "sourceLights.fixedVector.canonicalText");
        check(canonical.size() == 129,
            "sourceLights.fixedVector.canonicalLength");
        check(sed::assets::sourceLightsChecksumHex({ &row, 1 })
                == "63d045cfe6c98062d8cbd2daf5a37676af67ddab6698f2d5186f76bbad9bc1d0",
            "sourceLights.fixedVector.checksum");
    }

    // Null inherits, explicit zero and explicit equal stay explicit
    // across a JKL save/reopen, including signed zero.
    {
        const sed::Level level = makeLightLevel();
        const std::string text = save(level);
        check(text.find("Section: SED_SOURCE_LIGHTS\n") != std::string::npos,
            "sourceLights.roundTrip.sectionEmitted");
        check(text.find("0: inherit none 0.25 0.5 0.75\n") != std::string::npos,
            "sourceLights.roundTrip.inheritRow");
        check(text.find("1: 0 0 0.25 0.5 0.75\n") != std::string::npos,
            "sourceLights.roundTrip.explicitZeroRow");
        check(text.find("2: 4 none 0.25 0.5 0.75\n") != std::string::npos,
            "sourceLights.roundTrip.explicitEqualRow");

        const sed::Level loaded = reload(text);
        check(loaded.lights.size() == 4,
            "sourceLights.roundTrip.lightCount");
        if (loaded.lights.size() == 4)
        {
            check(!loaded.lights[0].rgbIntensity.has_value()
                    && !loaded.lights[0].legacyRgbRange.has_value()
                    && loaded.lights[0].effectiveRgbIntensity() == 4.0,
                "sourceLights.roundTrip.nullInherits");
            check(loaded.lights[1].rgbIntensity.has_value()
                    && *loaded.lights[1].rgbIntensity == 0.0
                    && loaded.lights[1].legacyRgbRange.has_value()
                    && *loaded.lights[1].legacyRgbRange == 0.0
                    && loaded.lights[1].effectiveRgbIntensity() == 0.0,
                "sourceLights.roundTrip.explicitZeroSurvives");
            check(loaded.lights[2].rgbIntensity.has_value()
                    && *loaded.lights[2].rgbIntensity == 4.0,
                "sourceLights.roundTrip.explicitEqualSurvives");
            check(loaded.lights[3].rgbIntensity.has_value()
                    && *loaded.lights[3].rgbIntensity == 0.0
                    && std::signbit(*loaded.lights[3].rgbIntensity),
                "sourceLights.roundTrip.explicitSignedZeroSurvives");
            check(std::signbit(loaded.lights[3].color.b)
                    && loaded.lights[3].color.r == 0.25f,
                "sourceLights.roundTrip.signedZeroColorSurvives");
        }
        check(loaded.hasSourceLightMetadata,
            "sourceLights.roundTrip.metadataFlag");
    }

    // The compact dialect preserves the same distinctions.
    {
        sed::assets::JklSaveOptions compactOptions{};
        compactOptions.compact = true;
        std::ostringstream compactOutput;
        sed::assets::saveToJKL(makeLightLevel(), compactOutput, compactOptions);
        const sed::Level loaded = reload(compactOutput.str());
        check(loaded.lights.size() == 4
                && !loaded.lights[0].rgbIntensity.has_value()
                && loaded.lights[1].rgbIntensity.has_value()
                && *loaded.lights[1].rgbIntensity == 0.0,
            "sourceLights.roundTrip.compactDialect");
    }
    // A legacy file without the extension loads inherited and a
    // rewrite does not invent the section.
    {
        sed::Level level{};
        level.kind = sed::ProjectType::MOTS;
        level.header.version = 1;
        sed::SedLight light{};
        light.intensity = 3.0;
        level.lights.push_back(light);
        const std::string text = save(level);
        check(text.find("SED_SOURCE_LIGHTS") == std::string::npos,
            "sourceLights.legacy.noSectionInvented");
        const sed::Level loaded = reload(text);
        check(loaded.lights.size() == 1
                && !loaded.lights[0].rgbIntensity.has_value()
                && !loaded.hasSourceLightMetadata,
            "sourceLights.legacy.inheritedDefault");
    }

    // Checksum-mismatched metadata is rejected, not applied: both
    // a replaced hash and an ordinary row edited behind its back.
    {
        const std::string text = save(makeLightLevel());
        std::string badHash = text;
        const std::size_t hashPos = badHash.find("LIGHTS_SHA256 ");
        check(hashPos != std::string::npos,
            "sourceLights.reject.hashAnchor");
        if (hashPos != std::string::npos)
        {
            badHash.replace(hashPos + 14, 64, 64, '0');
            checkThrowsWithMessage(
                [&badHash] { (void)reload(badHash); },
                "Cannot load JKL/NDY SED_SOURCE_LIGHTS: LIGHTS_SHA256 does not match the ordinary LIGHTS rows; metadata rejected.",
                "sourceLights.reject.replacedHash");
        }

        std::string editedRow = text;
        const std::size_t rowPos = editedRow.find("0: 0x1 ");
        check(rowPos != std::string::npos,
            "sourceLights.reject.rowAnchor");
        if (rowPos != std::string::npos)
        {
            editedRow[rowPos + 7] = '9';
            checkThrowsWithMessage(
                [&editedRow] { (void)reload(editedRow); },
                "Cannot load JKL/NDY SED_SOURCE_LIGHTS: LIGHTS_SHA256 does not match the ordinary LIGHTS rows; metadata rejected.",
                "sourceLights.reject.editedOrdinaryRow");
        }

        std::string badColor = text;
        const std::size_t sectionPos = badColor.find("Section: SED_SOURCE_LIGHTS");
        check(sectionPos != std::string::npos,
            "sourceLights.reject.colorAnchor");
        if (sectionPos != std::string::npos)
        {
            const std::size_t valuePos = badColor.find("0.75", sectionPos);
            badColor.replace(valuePos, 4, "0.5");
            checkThrowsWithMessage(
                [&badColor] { (void)reload(badColor); },
                "Cannot load JKL/NDY SED_SOURCE_LIGHTS: metadata color does not match the ordinary LIGHTS color.",
                "sourceLights.reject.redundantColorMismatch");
        }

        std::string badVersion = text;
        const std::size_t versionPos = badVersion.find(
            "Section: SED_SOURCE_LIGHTS\nVERSION 1");
        check(versionPos != std::string::npos,
            "sourceLights.reject.versionAnchor");
        if (versionPos != std::string::npos)
        {
            badVersion.replace(versionPos + 27, 9, "VERSION 2");
            checkThrowsWithMessage(
                [&badVersion] { (void)reload(badVersion); },
                "Cannot load JKL/NDY SED_SOURCE_LIGHTS: unsupported version.",
                "sourceLights.reject.unsupportedVersion");
        }
    }

    // Metadata carries inactive color for an ordinary row with no
    // RGB tokens (legacy seven-token form in this rewrite).
    {
        sed::assets::SourceLightBaseRow base{};
        base.flags = 0;
        base.layer = 0;
        base.x = 1.0;
        base.y = 2.0;
        base.z = 3.0;
        base.range = 2.0;
        base.intensity = 10.0;
        base.r = 1.0f;
        base.g = 1.0f;
        base.b = 1.0f;
        std::ostringstream fixture;
        fixture << "SECTION: HEADER\nVERSION 1\nEND\n"
                << "SECTION: LIGHTS\nEDITOR LIGHTS 1\n"
                << "0: 0 1 2 3 10 2\nEND\n"
                << "Section: SED_SOURCE_LIGHTS\nVERSION 1\n"
                << "LIGHTS_SHA256 "
                << sed::assets::sourceLightsChecksumHex({ &base, 1 }) << "\n"
                << "SOURCE_LIGHTS 1\n"
                << "0: inherit none 0.25 0.5 0.75\nEND\n";
        std::istringstream input(fixture.str());
        const sed::Level loaded = sed::assets::loadFromJKL(input);
        check(loaded.lights.size() == 1
                && loaded.lights[0].color.r == 0.25f
                && loaded.lights[0].color.g == 0.5f
                && loaded.lights[0].color.b == 0.75f
                && !loaded.lights[0].rgbIntensity.has_value(),
            "sourceLights.inactiveColor.carriedByMetadata");
    }
}

} // namespace sed::tests
