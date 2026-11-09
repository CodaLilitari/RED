#include "../agent_workspace.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
int failures = 0;

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void writeBytes(const fs::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if ( !output )
    {
        throw std::runtime_error("Cannot write synthetic native fixture");
    }
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
    if ( !output )
    {
        throw std::runtime_error("Cannot write synthetic text fixture");
    }
}

sed::assets::SedFile readNative(const fs::path& path)
{
    const std::string bytes = sed_agent::readFileBytes(path);
    std::vector<std::byte> buffer(bytes.size());
    std::transform(bytes.begin(), bytes.end(), buffer.begin(),
        [](const char value) { return static_cast<std::byte>(value); });
    auto file = sed::assets::loadSedFileBytes(buffer);
    (void)sed::assets::applySedLightRgbSection(file.level, file.optionalSections);
    (void)sed::assets::applySedArchAuthoringSection(file.level, file.optionalSections);
    return file;
}

bool sameSections(const std::vector<sed::assets::SedOptionalSection>& left,
    const std::vector<sed::assets::SedOptionalSection>& right)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(),
            [](const auto& a, const auto& b)
            {
                return a.type == b.type && a.payload == b.payload;
            });
}

std::vector<sed::assets::SedOptionalSection> authoringSections(const sed::Level& level)
{
    std::vector<sed::assets::SedOptionalSection> sections{};
    if ( const auto rgb = sed::assets::makeSedLightRgbSection(level) )
    {
        sections.push_back(*rgb);
    }
    if ( const auto arch = sed::assets::makeSedArchAuthoringSection(level) )
    {
        sections.push_back(*arch);
    }
    return sections;
}

void expectRefusal(const std::string& command, const std::vector<std::string>& args,
    const std::string& description,
    const std::string& expectedCode = "nativeWorkspaceEditUnsupported")
{
    try
    {
        (void)sed_agent::runCommand(command, args);
        check(false, description + " must refuse");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitRefusal
                && error.errorCode == expectedCode,
            description + " reports the stable native preservation refusal");
    }
}

void runTests(const fs::path& root)
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;
    level.header.gravity = 4.0;
    sed::SedLight light{};
    light.position = { 1.0, 2.0, 3.0 };
    light.range = 2.0;
    light.intensity = 4.0;
    light.color = { 0.25f, 0.5f, 0.75f, 1.0f };
    light.rgbIntensity = 0.5;
    light.legacyRgbRange = 3.0;
    level.lights.push_back(light);
    sed::MotsArchAuthoringEntry provenance{};
    provenance.thingIndex = 0;
    provenance.origin = sed::MotsArchSampleOrigin::ImportedUnknown;
    provenance.modelName = "fixture.3do";
    provenance.sampleSha256Hex = "original-samples";
    level.motsArchAuthoring.push_back(provenance);

    const auto knownSections = authoringSections(level);
    auto sections = knownSections;
    // The core owns the container and treats application/future payloads as
    // opaque. This marker tests preservation without pretending to validate
    // the Qt workspace or history schemas.
    for ( const auto type : { std::array<char, 4>{ 'e', 'd', 'i', 't' },
        std::array<char, 4>{ 'h', 'i', 's', 't' },
        std::array<char, 4>{ 'x', 't', 'r', 'a' } } )
    {
        sections.push_back({ type, { std::byte{ 0xa0 }, std::byte{ 0x19 } } });
    }
    const auto source = root / "workspace.sed";
    constexpr std::int64_t timestamp = 1700000000000;
    writeBytes(source, sed::assets::saveToSedBytes(level, sections,
        sed::assets::SedSerializationProfile::ProtectedV1, timestamp));
    const std::string sourceBytes = sed_agent::readFileBytes(source);

    const auto converted = root / "copy.sed";
    (void)sed_agent::runCommand("convert", { source.string(), converted.string() });
    const auto copy = readNative(converted);
    check(sameSections(copy.optionalSections, sections),
        "native conversion retains every optional section byte and ordering");
    check(copy.level.lights[0].rgbIntensity == light.rgbIntensity
            && copy.level.lights[0].legacyRgbRange == light.legacyRgbRange,
        "native conversion retains independent RGB controls");
    check(copy.level.motsArchAuthoring == level.motsArchAuthoring,
        "native conversion retains ARCH authoring provenance");
    check(copy.timestamp == timestamp, "native conversion retains timestamp");

    const auto noopPatch = root / "noop.json";
    writeText(noopPatch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":4}]})");
    const auto plan = sed_agent::runCommand("plan", { source.string(), noopPatch.string() });
    const auto planHash = plan.find("outputSha256");
    check(planHash && planHash->type == sed_agent::Json::Type::String,
        "native no-op plan returns a reviewed output hash");
    const auto applied = root / "noop.sed";
    if ( planHash )
    {
        (void)sed_agent::runCommand("apply",
            { source.string(), noopPatch.string(), applied.string(), planHash->str });
        const auto result = readNative(applied);
        check(sameSections(result.optionalSections, sections),
            "native no-op apply retains every optional section byte and ordering");
        check(result.timestamp == timestamp, "native no-op apply retains timestamp");
    }

    const auto changedPatch = root / "changed.json";
    writeText(changedPatch, R"({"schemaVersion":1,"operations":[{"op":"setGravity","value":6}]})");
    for ( std::size_t i = knownSections.size(); i < sections.size(); ++i )
    {
        const auto input = root / ("opaque-" + std::to_string(i) + ".sed");
        auto oneOpaque = knownSections;
        oneOpaque.push_back(sections[i]);
        writeBytes(input, sed::assets::saveToSedBytes(level, oneOpaque));
        const auto output = root / ("rejected-" + std::to_string(i) + ".sed");
        expectRefusal("plan", { input.string(), changedPatch.string() },
            "changed native plan with opaque state");
        expectRefusal("apply", { input.string(), changedPatch.string(), output.string() },
            "changed native apply with opaque state");
        check(!fs::exists(output), "native refusal publishes no output");
    }

    const auto plain = root / "authoring-only.sed";
    writeBytes(plain, sed::assets::saveToSedBytes(level, knownSections));
    const auto rgbPatch = root / "rgb.json";
    writeText(rgbPatch, R"({"schemaVersion":1,"operations":[{"op":"setLightRgb","light":0,"rgbIntensity":1.5}]})");
    const auto rgbOutput = root / "rgb.sed";
    (void)sed_agent::runCommand("apply", { plain.string(), rgbPatch.string(), rgbOutput.string() });
    const auto rgbResult = readNative(rgbOutput);
    check(rgbResult.level.lights[0].rgbIntensity == 1.5,
        "native files with understood extensions remain editable");
    check(rgbResult.level.motsArchAuthoring == level.motsArchAuthoring,
        "unrelated ARCH provenance survives native RGB edits");

    // Export/import changes the container, but must not erase the authoring
    // controls which RED explicitly represents in both formats.
    level.motsArchAuthoring.clear();
    const auto jkl = root / "source-lights.jkl";
    writeText(jkl, sed::assets::buildJklExport(level).text);
    const auto imported = root / "source-lights.sed";
    (void)sed_agent::runCommand("convert", { jkl.string(), imported.string() });
    const auto importedFile = readNative(imported);
    check(importedFile.level.lights.size() == 1
            && importedFile.level.lights[0].rgbIntensity == light.rgbIntensity
            && importedFile.level.lights[0].legacyRgbRange == light.legacyRgbRange,
        "JKL-to-native conversion retains source-light authoring controls");
    const auto disguisedJkl = root / "binary-disguised-as-text.jkl";
    expectRefusal("apply", { source.string(), noopPatch.string(), disguisedJkl.string() },
        "native apply with a text-format suffix", "outputFormatMismatch");
    check(!fs::exists(disguisedJkl), "suffix refusal publishes no disguised binary");
    const auto disguisedSed = root / "text-disguised-as-native.sed";
    expectRefusal("apply", { jkl.string(), noopPatch.string(), disguisedSed.string() },
        "text apply with a native-format suffix", "outputFormatMismatch");
    check(!fs::exists(disguisedSed), "suffix refusal publishes no disguised text");
    check(sed_agent::readFileBytes(source) == sourceBytes,
        "all plans, conversions, accepted edits and refusals leave input intact");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path()
        / ("red-native-preservation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        runTests(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected agent error: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "native preservation failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
