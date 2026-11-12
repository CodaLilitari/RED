#include "../agent_workspace.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures{};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class TestDirectory
{
public:
    TestDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("red-agent-semantic-preview-" + std::to_string(stamp));
        if ( !fs::create_directory(path) )
        {
            throw std::runtime_error("Cannot create isolated semantic-preview test directory");
        }
    }

    ~TestDirectory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    fs::path path;
};

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write synthetic semantic-preview request");
    }
}

std::set<fs::path> directoryEntries(const fs::path& root)
{
    std::set<fs::path> result;
    for ( const auto& entry : fs::directory_iterator(root) )
    {
        result.insert(entry.path().filename());
    }
    return result;
}

const Json& property(const Json& value, const char* name)
{
    const auto* found = value.find(name);
    if ( !found )
    {
        throw std::runtime_error(std::string("missing semantic-preview property: ") + name);
    }
    return *found;
}

void checkString(const Json& value, const char* name, const std::string& expected,
    const std::string& message)
{
    const auto& actual = property(value, name);
    check(actual.type == Json::Type::String && actual.str == expected, message);
}

void checkNumber(const Json& value, const char* name, const double expected,
    const std::string& message)
{
    const auto& actual = property(value, name);
    check(actual.type == Json::Type::Number && actual.number == expected, message);
}

void checkTrue(const Json& value, const char* name, const std::string& message)
{
    const auto& actual = property(value, name);
    check(actual.type == Json::Type::Bool && actual.boolean, message);
}

void checkGravityView(const Json& preview, const char* viewName, const char* afterSnapshot)
{
    const auto& view = property(preview, viewName);
    const std::string context = std::string(viewName) + " gravity preview ";
    check(view.isObject(), context + "is an object");
    checkString(view, "beforeSnapshot", "source", context + "starts at the captured source");
    checkString(view, "afterSnapshot", afterSnapshot, context + "identifies its compared snapshot");
    checkTrue(view, "comparisonComplete", context + "finishes comparing the small fixture");
    checkTrue(view, "detailsComplete", context + "includes every changed value");
    checkTrue(view, "changed", context + "reports the observed edit");
    checkNumber(view, "observedChanges", 1, context + "observes exactly one change");
    checkNumber(view, "totalChanges", 1, context + "reports exactly one total change");
    const auto& reasons = property(view, "reasons");
    check(reasons.type == Json::Type::Array && reasons.array.empty(),
        context + "has no incompleteness reasons");

    const auto& changes = property(view, "changes");
    check(changes.type == Json::Type::Array && changes.array.size() == 1,
        context + "emits exactly one change record");
    if ( changes.type != Json::Type::Array || changes.array.size() != 1 )
    {
        return;
    }
    const auto& change = changes.array.front();
    checkString(change, "path", "/header/gravity", context + "addresses the exact model field");
    checkString(change, "kind", "value", context + "reports a scalar value change");

    const auto& before = property(change, "before");
    checkTrue(before, "present", context + "has a present source value");
    checkString(before, "type", "float64", context + "retains the source storage type");
    checkString(before, "bits", "4010000000000000", context + "retains the exact source bits");
    checkString(before, "classification", "finite", context + "classifies the source value");
    checkNumber(before, "value", 4, context + "shows source gravity four");

    const auto& after = property(change, "after");
    checkTrue(after, "present", context + "has a present destination value");
    checkString(after, "type", "float64", context + "retains the destination storage type");
    checkString(after, "bits", "4018000000000000", context + "retains the exact destination bits");
    checkString(after, "classification", "finite", context + "classifies the destination value");
    checkNumber(after, "value", 6, context + "shows requested gravity six");
}

// Catches generic plan/apply omitting the preview, comparing the wrong snapshots,
// losing exact field values, publishing during plan, or returning evidence that
// does not describe the source and the reviewed output actually written.
void testNativeGravityPreview(const fs::path& root)
{
    const auto source = root / "source.sed";
    const auto patch = root / "gravity.json";
    const auto output = root / "edited.sed";

    // The real starter command creates project-owned synthetic room geometry,
    // without game assets or opaque native workspace/history sections.
    (void)sed_agent::runCommand("new", { source.string(), "mots" });
    const auto sourceBytes = sed_agent::readFileBytes(source);
    const auto sourceHash = sed_agent::sha256Hex(sourceBytes);
    writeText(patch, "{\"schemaVersion\":1,\"inputSha256\":\"" + sourceHash
        + "\",\"sourceRevision\":\"" + sourceHash
        + "\",\"operations\":[{\"op\":\"setLevelHeader\",\"gravity\":6}]}");
    const auto entriesBeforePlan = directoryEntries(root);

    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    check(directoryEntries(root) == entriesBeforePlan && !fs::exists(output),
        "native plan publishes no output or staging files");
    check(sed_agent::readFileBytes(source) == sourceBytes, "native plan leaves source bytes unchanged");
    const auto* preview = planned.find("semanticPreview");
    check(preview && preview->isObject(), "native plan returns semanticPreview");
    if ( !preview || !preview->isObject() )
    {
        // Keep the baseline RED an assertion about the missing receipt field,
        // rather than dereferencing it or depending on a future adapter API.
        return;
    }

    checkTrue(planned, "ok", "native gravity plan succeeds");
    checkString(planned, "inputSha256", sourceHash, "plan binds the captured input bytes");
    checkString(planned, "sourceRevision", sourceHash, "plan retains its captured source revision");
    const auto& plannedHash = property(planned, "outputSha256");
    check(plannedHash.type == Json::Type::String && plannedHash.str.size() == 64,
        "plan supplies a reviewed output hash");
    if ( plannedHash.type != Json::Type::String || plannedHash.str.size() != 64 )
    {
        return;
    }
    checkNumber(*preview, "version", 1, "preview declares the versioned contract");
    checkString(*preview, "addressing", "snapshot-storage-slots", "preview declares positional addresses");
    checkString(*preview, "inputSha256", sourceHash, "preview binds the exact source bytes");
    checkString(*preview, "outputSha256", plannedHash.str, "preview binds the reviewed output bytes");
    checkGravityView(*preview, "authored", "candidate");
    checkGravityView(*preview, "persisted", "output");
    const auto plannedAgain = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto& repeatedPreview = property(plannedAgain, "semanticPreview");
    check(sed_agent::toJsonString(repeatedPreview) == sed_agent::toJsonString(*preview),
        "repeated native plans return the same complete semantic preview");
    checkString(plannedAgain, "outputSha256", plannedHash.str,
        "repeated native plans retain the same reviewed output hash");
    check(directoryEntries(root) == entriesBeforePlan && !fs::exists(output),
        "repeated native planning publishes no files");
    check(sed_agent::readFileBytes(source) == sourceBytes,
        "repeated native planning leaves source bytes unchanged");

    const auto applied = sed_agent::runCommand("apply",
        { source.string(), patch.string(), output.string(), plannedHash.str });
    checkTrue(applied, "ok", "native gravity apply accepts the reviewed output hash");
    const auto& appliedPreview = property(applied, "semanticPreview");
    check(sed_agent::toJsonString(appliedPreview) == sed_agent::toJsonString(*preview),
        "native apply returns the exact semantic preview reviewed during plan");
    const auto outputBytes = sed_agent::readFileBytes(output);
    const auto outputHash = sed_agent::sha256Hex(outputBytes);
    check(outputHash == plannedHash.str, "published native bytes match the reviewed output hash");
    checkString(applied, "inputSha256", sourceHash, "apply binds the unchanged source bytes");
    checkString(applied, "outputSha256", outputHash, "apply receipt binds the actual output bytes");
    checkString(appliedPreview, "inputSha256", sourceHash, "applied preview binds the unchanged source");
    checkString(appliedPreview, "outputSha256", outputHash, "applied preview binds the actual output");
    check(outputBytes != sourceBytes, "accepted gravity edit changes the published native bytes");
    const auto reopened = sed_agent::runCommand("inspect", { output.string() });
    checkNumber(reopened, "gravity", 6, "reopening the published level confirms the previewed gravity");
    check(sed_agent::readFileBytes(source) == sourceBytes,
        "native plan/apply and reopen leave the original source bytes unchanged");
}

const Json& changeAt(const Json& view, const std::string& path)
{
    for ( const auto& change : property(view, "changes").array )
    {
        const auto* actual = change.find("path");
        if ( actual && actual->type == Json::Type::String && actual->str == path )
        {
            return change;
        }
    }
    throw std::runtime_error("missing semantic-preview change: " + path);
}

// Catches comparing only the candidate, or reparsing the wrong bytes for the
// persisted view. The JKL writer rounds this fixture to eight decimal places.
void testTextRoundingAndNoop(const fs::path& root)
{
    const auto source = root / "text-source.jkl";
    const auto patch = root / "rounding.json";
    const auto output = root / "rounded.jkl";
    (void)sed_agent::runCommand("new", { source.string(), "mots" });
    const auto original = sed_agent::readFileBytes(source);
    writeText(patch, R"({"operations":[{"op":"setLevelHeader","gravity":0.12345678901234566}]})");
    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto* preview = planned.find("semanticPreview");
    check(preview && preview->isObject(), "text plan returns semanticPreview");
    if ( !preview || !preview->isObject() )
    {
        return;
    }
    const auto& authored = property(*preview, "authored");
    const auto& persisted = property(*preview, "persisted");
    checkNumber(property(changeAt(authored, "/header/gravity"), "after"), "value",
        0.12345678901234566, "authored view preserves the requested unrounded double");
    checkNumber(property(changeAt(persisted, "/header/gravity"), "after"), "value",
        0.12345679, "persisted view shows the exact parsed output rounding");
    const auto& outputHash = property(planned, "outputSha256").str;
    const auto applied = sed_agent::runCommand("apply",
        { source.string(), patch.string(), output.string(), outputHash });
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(output)) == outputHash,
        "text preview is bound to actual reviewed output bytes");
    check(sed_agent::toJsonString(property(applied, "semanticPreview")) == sed_agent::toJsonString(*preview),
        "text apply retains both reviewed comparison views");
    checkNumber(sed_agent::runCommand("inspect", { output.string() }), "gravity", 0.12345679,
        "actual output reopens at the persisted preview value");

    const auto refused = root / "stale-output.jkl";
    bool conflict{};
    try
    {
        (void)sed_agent::runCommand("apply",
            { source.string(), patch.string(), refused.string(), std::string(64, '0') });
    }
    catch ( const sed_agent::AgentError& error )
    {
        conflict = error.code == sed_agent::kExitConflict;
    }
    check(conflict && !fs::exists(refused), "preview does not bypass the reviewed output hash guard");

    writeText(patch, R"({"operations":[{"op":"setLevelHeader","gravity":4}]})");
    const auto noop = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto& unchanged = property(noop, "semanticPreview");
    for ( const char* name : { "authored", "persisted" } )
    {
        const auto& view = property(unchanged, name);
        checkTrue(view, "comparisonComplete", std::string(name) + " no-op finishes comparison");
        checkTrue(view, "detailsComplete", std::string(name) + " no-op details are complete");
        const auto& changed = property(view, "changed");
        check(changed.type == Json::Type::Bool && !changed.boolean,
            std::string(name) + " complete covered no-op is explicitly unchanged");
        checkNumber(view, "totalChanges", 0, std::string(name) + " no-op has zero total changes");
    }
    checkString(noop, "outputSha256", sed_agent::sha256Hex(original), "starter no-op keeps identical text bytes");
    check(sed_agent::readFileBytes(source) == original, "text previews, apply and refusal preserve their source");
}

// Catches retaining native readback before optional RGB and ARCH fields have been restored.
void testNativeOptionalMetadata(const fs::path& root)
{
    const auto source = root / "rgb-source.sed";
    (void)sed_agent::runCommand("new", { source.string(), "mots" });
    const auto bytes = sed_agent::readFileBytes(source);
    auto file = sed::assets::loadSedFileBytes(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
    auto& light = file.level.newLight();
    light.rgbIntensity = 0.0;
    light.legacyRgbRange = 2.5;
    sed::ThingTemplate archTemplate;
    archTemplate.name = "preview_arch";
    archTemplate.parent = "none";
    archTemplate.values = { sed::deserializeTplValue("thingflags", "0x8"),
        sed::deserializeTplValue("model3d", "preview_arch.3do") };
    file.level.templates.push_back(archTemplate);
    const auto ownerIndex = static_cast<std::int32_t>(file.level.things.size());
    auto& owner = file.level.newThing();
    owner.name = "preview_arch";
    owner.templateName = "preview_arch";
    owner.sector = 0;
    owner.archLightingIndex = 0;
    sed::MotsArchLightObject samples;
    samples.wireIndex = 7;
    sed::MotsArchLightMesh mesh;
    mesh.wireIndex = 11;
    mesh.vertices.push_back({ 0.25f, 0.5f, 0.75f, 1.0f });
    samples.meshes.push_back(mesh);
    file.level.motsArchLighting.push_back(samples);
    sed::MotsArchAuthoringEntry provenance;
    provenance.thingIndex = ownerIndex;
    provenance.modelName = "preview_arch.3do";
    provenance.origin = sed::MotsArchSampleOrigin::ImportedUnknown;
    file.level.motsArchAuthoring.push_back(provenance);
    const auto section = sed::assets::makeSedLightRgbSection(file.level);
    const auto archSection = sed::assets::makeSedArchAuthoringSection(file.level);
    check(section.has_value() && archSection.has_value(), "native optional fixture produces RGB and ARCH extensions");
    if ( !section || !archSection )
    {
        return;
    }
    const std::vector<sed::assets::SedOptionalSection> sections{ *section, *archSection };
    const auto encoded = sed::assets::saveToSedBytes(file.level, sections,
        sed::assets::SedSerializationProfile::ProtectedV1, file.timestamp);
    writeText(source, std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
    const auto patch = root / "rgb-gravity.json";
    writeText(patch, R"({"operations":[{"op":"setLevelHeader","gravity":6}]})");
    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto* preview = planned.find("semanticPreview");
    check(preview && preview->isObject(), "native RGB and ARCH plan returns semanticPreview");
    if ( !preview || !preview->isObject() )
    {
        return;
    }
    checkGravityView(*preview, "authored", "candidate");
    checkGravityView(*preview, "persisted", "output");
}

void testDiscovery()
{
    const auto schema = sed_agent::runCommand("schema", {});
    const auto* preview = schema.find("semanticPreview");
    check(preview && preview->isObject(), "discovery describes semantic previews");
    if ( !preview || !preview->isObject() )
    {
        return;
    }
    checkNumber(*preview, "version", 1, "discovery identifies preview contract version");
    checkString(*preview, "coverage", "level-model-v1", "discovery bounds model coverage");
    const auto& commands = property(*preview, "commands");
    check(commands.array.size() == 2 && commands.array[0].str == "plan" && commands.array[1].str == "apply",
        "discovery scopes previews to generic plan/apply");
    check(property(*preview, "excludedFields").type == Json::Type::Array,
        "discovery states model fields omitted from previews");
    checkNumber(schema, "operationCount", 63, "preview adds no patch operations");
    checkNumber(schema, "commandCount", 42, "inventory includes dependency inspection with previews still embedded");
}
} // namespace

int main()
{
    try
    {
        const TestDirectory directory;
        testNativeGravityPreview(directory.path);
        testTextRoundingAndNoop(directory.path);
        testNativeOptionalMetadata(directory.path);
        testDiscovery();
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected semantic-preview agent error: " + error.errorCode + ": " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected semantic-preview error: ") + error.what());
    }
    std::cout << "semantic preview failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
