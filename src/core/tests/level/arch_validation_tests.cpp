#include "test_common.hpp"

#include "sed/assets/gob.hpp"
#include "sed/assets/source_lights.hpp"
#include "sed/level/arch_validation.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

sed::MotsArchLightObject storedObject()
{
    sed::MotsArchLightObject object{};
    object.wireIndex = 0;
    sed::MotsArchLightMesh first{};
    first.wireIndex = 0;
    first.vertices.assign(3, sed::MotsArchLightVertex{});
    sed::MotsArchLightMesh second{};
    second.wireIndex = 1;
    second.vertices.assign(2, sed::MotsArchLightVertex{});
    object.meshes.push_back(first);
    object.meshes.push_back(second);
    return object;
}

sed::Level makeValidationLevel()
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;
    level.motsArchLighting.push_back(storedObject());

    sed::Thing& thing = level.newThing();
    thing.templateName = "arch_probe";
    thing.name = "bound_arch";
    thing.archLightingIndex = 0;
    sed::TplValue flags{};
    flags.name = "thingflags";
    flags.type = sed::ValueType::integer;
    flags.intValue = 0x8;
    thing.values.push_back(flags);
    sed::TplValue model{};
    model.name = "model3d";
    model.type = sed::ValueType::str;
    model.stringValue = "probe.3do";
    thing.values.push_back(model);
    return level;
}

sed::level::MotsArchLightingTopology topology(
    const std::vector<std::vector<std::size_t>>& geosets)
{
    sed::level::MotsArchLightingTopology value{};
    value.geosets = geosets;
    return value;
}

bool hasIssue(
    const sed::level::ArchBindingResult& result,
    const std::string_view code,
    const std::int32_t geoset = -2)
{
    for (const sed::level::ArchBindingIssue& issue : result.issues)
    {
        if (issue.code == code
            && (geoset == -2 || issue.geosetIndex == geoset))
        {
            return true;
        }
    }
    return false;
}

void writeU32(std::ofstream& out, const std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value & 0xFFu),
        static_cast<unsigned char>((value >> 8) & 0xFFu),
        static_cast<unsigned char>((value >> 16) & 0xFFu),
        static_cast<unsigned char>((value >> 24) & 0xFFu) };
    out.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

} // namespace

void runArchValidationTests()
{
    using namespace sed::level;

    // Preview (geoset zero) fits exactly, but a lower LOD needs
    // more slots than the stored arrays hold: refused.
    {
        const sed::Level level = makeValidationLevel();
        ArchBindingExpectation expectation{};
        expectation.thingIndex = 0;
        expectation.modelName = "probe.3do";
        expectation.topology = topology({ { 3, 2 }, { 3, 3 } });

        const ArchValidationReport report =
            validateArchBindings(level, { &expectation, 1 });
        check(report.bindings.size() == 1 && !report.usable(),
            "archValidation.lowerLodOverflow.refused");
        check(report.bindings.size() == 1
                && hasIssue(report.bindings[0], "archLowerLodOverflow", 1),
            "archValidation.lowerLodOverflow.located");

        ArchBindingExpectation previewOnly = expectation;
        previewOnly.topology = topology({ { 3, 2 } });
        const ArchValidationReport previewReport =
            validateArchBindings(level, { &previewOnly, 1 });
        check(previewReport.usable(),
            "archValidation.lowerLodOverflow.previewAloneFits");

        std::string code;
        check(!archTopologyAllowsBinding(
                level.motsArchLighting[0], expectation.topology, &code)
                && code == "archLowerLodOverflow",
            "archValidation.lowerLodOverflow.directCheck");
    }

    // Lower LODs as strict prefixes pass; geoset zero must match
    // exactly.
    {
        const sed::Level level = makeValidationLevel();
        ArchBindingExpectation prefix{};
        prefix.thingIndex = 0;
        prefix.modelName = "probe.3do";
        prefix.topology = topology({ { 3, 2 }, { 2, 1 }, { 1 } });
        check(validateArchBindings(level, { &prefix, 1 }).usable(),
            "archValidation.prefix.passes");

        ArchBindingExpectation wrongZero = prefix;
        wrongZero.topology = topology({ { 3, 3 }, { 1, 1 } });
        const ArchValidationReport report =
            validateArchBindings(level, { &wrongZero, 1 });
        check(!report.usable()
                && hasIssue(report.bindings[0], "archGeosetZeroMismatch"),
            "archValidation.geosetZero.exact");
    }

    // Fingerprints: match verifies, mismatch refuses, and a
    // missing baseline stays unknown without blocking topology.
    {
        const sed::Level level = makeValidationLevel();
        ArchBindingExpectation expectation{};
        expectation.thingIndex = 0;
        expectation.modelName = "probe.3do";
        expectation.topology = topology({ { 3, 2 }, { 2, 1 } });
        expectation.resolvedFingerprintHex =
            archModelFingerprintHex(expectation.topology);

        ArchStoredFingerprint matching{};
        matching.thingIndex = 0;
        matching.fingerprintHex = *expectation.resolvedFingerprintHex;
        const ArchValidationReport matched =
            validateArchBindings(level, { &expectation, 1 }, { &matching, 1 });
        check(matched.usable() && matched.bindings[0].fingerprintChecked,
            "archValidation.fingerprint.matchVerifies");

        ArchStoredFingerprint mismatched = matching;
        mismatched.fingerprintHex = std::string(64, '0');
        const ArchValidationReport mismatch =
            validateArchBindings(level, { &expectation, 1 }, { &mismatched, 1 });
        check(!mismatch.usable()
                && hasIssue(mismatch.bindings[0], "archFingerprintMismatch"),
            "archValidation.fingerprint.mismatchRefused");

        const ArchValidationReport unknown =
            validateArchBindings(level, { &expectation, 1 });
        check(unknown.usable() && !unknown.bindings[0].fingerprintChecked
                && hasIssue(unknown.bindings[0], "archFingerprintUnknown"),
            "archValidation.fingerprint.missingStaysUnknown");
    }

    // Missing model, missing flag, and unbound states stay distinct.
    {
        const sed::Level level = makeValidationLevel();
        const ArchValidationReport missing =
            validateArchBindings(level, {});
        check(!missing.usable()
                && hasIssue(missing.bindings[0], "archModelMissing"),
            "archValidation.model.noExpectationNoClosure");

        sed::Level unflagged = makeValidationLevel();
        unflagged.things[0].values[0].intValue = 0;
        ArchBindingExpectation expectation{};
        expectation.thingIndex = 0;
        expectation.modelName = "probe.3do";
        expectation.topology = topology({ { 3, 2 } });
        const ArchValidationReport flagReport =
            validateArchBindings(unflagged, { &expectation, 1 });
        check(!flagReport.usable()
                && hasIssue(flagReport.bindings[0], "archThingFlagMissing"),
            "archValidation.flags.missingRefused");
    }

    // All-LOD topology reading: physical order defines slots,
    // declared-but-missing meshes fail.
    {
        const std::string text =
            "SECTION: GEOMETRYDEF\nGEOSETS 2\n"
            "GEOSET 0\nMESHES 2\n"
            "MESH 0\nVERTICES 3\n0: 0 0 0\n1: 1 0 0\n2: 0 1 0\nFACES 0\n"
            "MESH 0\nVERTICES 2\n0: 0 0 0\n1: 1 0 0\nFACES 0\n"
            "GEOSET 1\nMESHES 1\n"
            "MESH 0\nVERTICES 2\n0: 0 0 0\n1: 1 0 0\nFACES 0\n";
        std::string error;
        const std::optional<MotsArchLightingTopology> read =
            readArchModelTopology(text, &error);
        check(read.has_value() && read->geosets.size() == 2
                && read->geosets[0] == std::vector<std::size_t>{ 3, 2 }
                && read->geosets[1] == std::vector<std::size_t>{ 2 },
            "archValidation.topologyReader.allLods");

        const std::string truncated =
            "GEOSETS 1\nGEOSET 0\nMESHES 2\nMESH 0\nVERTICES 1\n0: 0 0 0\n";
        check(!readArchModelTopology(truncated, &error).has_value()
                && !error.empty(),
            "archValidation.topologyReader.truncatedRefused");
    }

    // Explicit archive entries expose their byte hash.
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "sed_arch_validation_gob_test.gob";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            const std::string payload = "arch-model-bytes";
            const std::uint32_t indexOffset =
                8 + static_cast<std::uint32_t>(payload.size());
            out.write("GOB\n", 4);
            writeU32(out, indexOffset);
            out << payload;
            writeU32(out, 1);
            writeU32(out, 8);
            writeU32(out, static_cast<std::uint32_t>(payload.size()));
            const std::string name = "probe.3do";
            std::string padded = name;
            padded.resize(13, '\0');
            out << padded;
        }
        const sed::assets::GobArchive archive =
            sed::assets::GobArchive::open(path);
        check(archive.contains("probe.3do"),
            "archValidation.gob.entryFound");
        check(archive.entrySha256Hex("probe.3do")
                == sed::assets::sourceLightSha256Hex("arch-model-bytes"),
            "archValidation.gob.entryHash");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

} // namespace sed::tests
