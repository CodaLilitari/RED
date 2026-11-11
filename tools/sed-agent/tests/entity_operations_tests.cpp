#include "../agent_workspace.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/thing.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;
int failures = 0;
int requestNumber = 0;

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
    if ( !output ) throw std::runtime_error("cannot write synthetic entity fixture");
}

void writeNative(const fs::path& path, const sed::Level& level)
{
    const auto bytes = sed::assets::saveToSedBytes(level);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if ( !output ) throw std::runtime_error("cannot write synthetic native entity fixture");
}

sed::Level readNative(const fs::path& path)
{
    const auto bytes = sed_agent::readFileBytes(path);
    std::vector<std::byte> buffer(bytes.size());
    std::transform(bytes.begin(), bytes.end(), buffer.begin(), [](const char c) { return static_cast<std::byte>(c); });
    return sed::assets::loadSedFileBytes(buffer).level;
}

fs::path request(const fs::path& root, const std::string& operations)
{
    const auto path = root / ("entity-request-" + std::to_string(++requestNumber) + ".json");
    writeText(path, "{\"schemaVersion\":1,\"operations\":[" + operations + "]}");
    return path;
}

void apply(const fs::path& root, const fs::path& source, const fs::path& output, const std::string& operations)
{
    const auto patch = request(root, operations);
    const auto before = sed_agent::readFileBytes(source);
    const auto planned = sed_agent::runCommand("plan", { source.string(), patch.string() });
    const auto* hash = planned.find("outputSha256");
    check(hash && hash->type == sed_agent::Json::Type::String, "entity plan provides a reviewed output hash");
    if ( !hash ) return;
    (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string(), hash->str });
    check(sed_agent::sha256Hex(sed_agent::readFileBytes(output)) == hash->str, "entity apply matches reviewed hash");
    check(sed_agent::readFileBytes(source) == before, "entity edit preserves source bytes");
}

void expectRefusal(const fs::path& root, const fs::path& source, const std::string& operations,
    const std::string& code)
{
    const auto patch = request(root, operations);
    const auto output = root / ("entity-refused-" + std::to_string(requestNumber) + source.extension().string());
    const auto before = sed_agent::readFileBytes(source);
    try
    {
        (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string() });
        check(false, "entity operation must refuse with " + code);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == code, "expected " + code + ", received " + error.errorCode + ": " + error.message);
    }
    check(!fs::exists(output), "entity refusal publishes no output");
    check(sed_agent::readFileBytes(source) == before, "entity refusal preserves source bytes");
}

sed::Level fixture(const fs::path& starter)
{
    auto level = sed::assets::loadFromJKL(starter);
    for ( const std::string name : { "review_item", "review_other" } )
    {
        sed::ThingTemplate item{};
        item.name = name;
        item.parent = "none";
        item.values.push_back(sed::deserializeTplValue("type", "ghost"));
        level.templates.push_back(item);
        auto& thing = level.newThing();
        thing.templateName = name;
        thing.name = name + "_placed";
        thing.sector = 0;
        thing.values.push_back(sed::deserializeTplValue("health", "25"));
    }
    auto& light = level.newLight();
    light.position = { 0.25, 0.5, 0.75 };
    light.color = { 0.25f, 0.5f, 1.0f, 1.0f };
    light.intensity = 2.0;
    light.range = 3.0;
    light.rgbIntensity = 0.5;
    light.legacyRgbRange = -4.0;
    return level;
}

void lightEdits(const fs::path& root, const fs::path& source)
{
    const auto baseline = sed::assets::loadFromJKL(source);
    const auto added = root / "entity-light-added.jkl";
    apply(root, source, added,
        R"({"op":"addLight","position":[0,0,0],"rgb":[1,0.5,0.25],"intensity":4,"range":5,"rgbIntensity":0,"legacyRgbRange":-7,"flags":4,"layer":0})");
    const auto reopened = sed::assets::loadFromJKL(added);
    check(reopened.lights.size() == 2, "source-light addition persists");
    check(reopened.lights[1].rgbIntensity == 0.0 && reopened.lights[1].legacyRgbRange == -7.0,
        "explicit zero RGB intensity and signed legacy range remain distinct");
    const auto edited = root / "entity-light-edited.jkl";
    apply(root, added, edited, R"({"op":"setLight","light":0,"intensity":6},{"op":"setLight","light":1,"rgbIntensity":null,"legacyRgbRange":null,"position":[0.1,0.2,0.3],"range":8})");
    const auto result = sed::assets::loadFromJKL(edited);
    check(result.lights[0].intensity == 6 && result.lights[0].rgbIntensity == 0.5,
        "editing white light does not rescale independent RGB");
    check(result.lights[0].position == baseline.lights[0].position
            && result.lights[0].color == baseline.lights[0].color
            && result.lights[0].legacyRgbRange == -4.0, "omitted source-light fields survive");
    check(!result.lights[1].rgbIntensity && !result.lights[1].legacyRgbRange
            && result.lights[1].effectiveRgbIntensity() == 4, "null restores inheritance and clears only requested metadata");
    check(result.lights[1].range == 8 && result.lights[1].flags == 4, "source-light fields persist together");
    for ( std::size_t f = 0; f < baseline.sectors[0].surfaces.size(); ++f )
    {
        for ( std::size_t c = 0; c < baseline.sectors[0].surfaces[f].texVertices.size(); ++c )
        {
            check(result.sectors[0].surfaces[f].texVertices[c].color
                    == baseline.sectors[0].surfaces[f].texVertices[c].color,
                "editing source lights never bakes world corners");
        }
    }
}

void thingEdits(const fs::path& root, const fs::path& source)
{
    const auto baseline = sed::assets::loadFromJKL(source);
    const auto output = root / "entity-things-edited.jkl";
    apply(root, source, output,
        R"({"op":"setThingPosition","thing":0,"position":[0.25,0.25,0.25],"sector":0},{"op":"setThingOrientation","thing":0,"orientation":[10,20,30]},{"op":"setThingName","thing":0,"name":"review_changed"},{"op":"setThingTemplate","thing":0,"template":"review_other"},{"op":"setThingValue","thing":0,"key":"health","value":"75"})");
    const auto result = sed::assets::loadFromJKL(output);
    const auto& thing = result.things[0];
    check(result.things.size() == baseline.things.size() && thing.sector == 0
            && thing.x == 0.25 && thing.y == 0.25 && thing.z == 0.25, "existing Thing placement persists without append/reindex");
    check(thing.pch == 10 && thing.yaw == 20 && thing.rol == 30, "Thing orientation persists");
    check(thing.name == "review_changed" && thing.templateName == "review_other", "Thing name/template edits persist");
    const auto* health = sed::level::thingFindValue(result, thing, "health");
    check(health && health->asString() == "75", "Thing scalar token edit persists");
    check(result.things[1].name == baseline.things[1].name
            && result.things[1].templateName == baseline.things[1].templateName,
        "other Thing identity is unchanged");
}

void clearArchitecture(const fs::path& root, sed::Level level)
{
    level.things[0].values.push_back(sed::deserializeTplValue("thingflags", "0x8"));
    level.things[0].archLightingIndex = 0;
    sed::MotsArchLightObject object{};
    sed::MotsArchLightMesh mesh{};
    mesh.vertices.push_back({ 0.125f, 0.25f, 0.5f, 0.75f });
    object.meshes.push_back(mesh);
    level.motsArchLighting.push_back(object);
    const auto source = root / "entity-arch.sed";
    writeNative(source, level);
    expectRefusal(root, source,
        R"({"op":"setThingTemplate","thing":0,"template":"review_other"})", "entityArchBound");
    const auto output = root / "entity-arch-cleared.sed";
    apply(root, source, output,
        R"({"op":"clearThingArchitecture","thing":0},{"op":"setThingTemplate","thing":0,"template":"review_other"})");
    const auto result = readNative(output);
    check(result.things[0].archLightingIndex == -1, "explicit ARCH clear removes binding");
    check(result.motsArchLighting == level.motsArchLighting, "ARCH clear retains original sample arrays");
    check(result.things[0].templateName == "review_other", "cleared Thing can change template");
}

void nativeTypedValue(const fs::path& root, sed::Level level)
{
    auto& value = level.things[0].values[0];
    value.type = sed::ValueType::floating;
    value.floatValue = 25;
    value.stringValue.clear();
    const auto source = root / "entity-typed-value.sed";
    writeNative(source, level);
    const auto output = root / "entity-typed-value-edited.sed";
    apply(root, source, output, R"({"op":"setThingValue","thing":0,"key":"health","value":"75"})");
    const auto result = readNative(output);
    check(result.things[0].values[0].type == sed::ValueType::floating
            && result.things[0].values[0].floatValue == 75,
        "native parameter edit preserves its declared numeric type");
    expectRefusal(root, source,
        R"({"op":"setThingValue","thing":0,"key":"health","value":"nonsense"})", "entityOperationInvalid");
}

void addThingEdits(const fs::path& root, sed::Level level, const bool native)
{
    // Exact-name creation must not collide with an unused array-style name.
    level.things[0].name = "fresh_000";
    level.templates[0].values.push_back(sed::deserializeTplValue("health", "100"));
    const std::string extension = native ? ".sed" : ".jkl";
    const auto source = root / ("entity-add-source" + extension);
    if ( native )
    {
        level.templates[0].values.push_back(sed::deserializeTplValue("thingflags", "0x8"));
        level.things[0].archLightingIndex = 0;
        sed::MotsArchLightObject object{};
        sed::MotsArchLightMesh mesh{};
        mesh.vertices.push_back({ 0.125f, 0.25f, 0.5f, 0.75f });
        object.meshes.push_back(mesh);
        level.motsArchLighting.push_back(object);
        writeNative(source, level);
    }
    else
    {
        writeText(source, sed::assets::buildJklExport(level).text);
    }
    const auto before = native ? readNative(source) : sed::assets::loadFromJKL(source);
    const auto output = root / ("entity-add-output" + extension);
    apply(root, source, output,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[0.25,0.5,0.75]})");
    const auto after = native ? readNative(output) : sed::assets::loadFromJKL(output);
    check(after.things.size() == before.things.size() + 1, "addThing appends exactly one Thing");
    const auto& added = after.things.back();
    check(added.name == "fresh" && added.templateName == "review_item" && added.sector == 0
            && added.x == 0.25 && added.y == 0.5 && added.z == 0.75,
        "addThing preserves the exact requested name, template and placement");
    check(added.values.empty() && added.archLightingIndex == -1
            && added.pch == 0 && added.yaw == 0 && added.rol == 0 && added.layer == 0,
        "fresh Thing has no copied local parameters, ARCH binding, pose or layer");
    check(added.num == static_cast<std::int32_t>(before.things.size())
            && std::none_of(before.things.begin(), before.things.end(),
                [&](const auto& old) { return old.id == added.id; }),
        "fresh Thing receives a new stable ID and appended index");
    check(after.motsArchLighting == before.motsArchLighting
            && after.motsArchAuthoring == before.motsArchAuthoring,
        "adding an ARCH-capable Thing does not copy or bake ARCH state");
    check(after.things[0].name == before.things[0].name
            && after.things[0].archLightingIndex == before.things[0].archLightingIndex
            && after.things[0].values[0].asString() == before.things[0].values[0].asString()
            && after.materials == before.materials && after.colormaps == before.colormaps,
        "addThing preserves existing instance and resource identities");
}

void addThingRefusals(const fs::path& root, const fs::path& source, sed::Level level)
{
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"missing","name":"fresh","position":[0,0,0]})",
        "placementTemplateNotFound");
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"REVIEW_OTHER_PLACED","position":[0,0,0]})",
        "placementNameCollision");
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"bad name","position":[0,0,0]})",
        "entityOperationInvalid");
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[0,0,0],"copyThing":0})",
        "entityOperationInvalid");
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[1,0,0]})",
        "entityPositionOutsideSector");
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[9,9,9]})",
        "entityPositionOutsideSector");
    const auto unknown = root / "entity-add-unknown.jkl";
    writeText(unknown, sed_agent::readFileBytes(source) + "\nSection: FUTURE_THING_LINKS\nthing 2\nend\n");
    expectRefusal(root, unknown,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[0,0,0]})",
        "entityAppendUnknownState");
    expectRefusal(root, unknown,
        R"({"op":"placeThingArray","template":"review_item","namePrefix":"array_fresh","sector":0,"layout":{"kind":"linear","origin":[0,0,0],"step":[0.25,0,0],"count":1}})",
        "entityAppendUnknownState");
    auto& cog = level.newCog();
    cog.name = "review_append_guard.cog";
    sed::CogValue value{};
    value.name = "target";
    value.cogType = sed::CogType::thing;
    value.type = sed::ValueType::ptr;
    value.intValue = 0;
    cog.values.push_back(value);
    const auto withCog = root / "entity-add-with-cog.sed";
    writeNative(withCog, level);
    expectRefusal(root, withCog,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh","position":[0,0,0]})",
        "cogAppendGuardRequired");
}

void nativeNextIdAdmission(const fs::path& root, const sed::Level& baseline)
{
    for ( const std::int32_t nextId : { -1, 0, baseline.things[0].id } )
    {
        auto level = baseline;
        level.curId = nextId;
        const auto source = root / ("entity-next-id-" + std::to_string(nextId) + ".sed");
        const auto output = root / ("entity-next-id-output-" + std::to_string(nextId) + ".sed");
        writeNative(source, level);
        const auto before = sed_agent::readFileBytes(source);
        const auto patch = request(root,
            R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh_id","position":[0,0,0]})");
        try
        {
            (void)sed_agent::runCommand("apply", { source.string(), patch.string(), output.string() });
            check(false, "native invalid/reused next ID must refuse before Thing append");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "entityAppendInvalidIdentity",
                "invalid next-ID admission requires entityAppendInvalidIdentity, received " + error.errorCode);
        }
        if ( fs::exists(output) )
        {
            const auto result = readNative(output);
            std::cerr << "OBSERVED: source curId=" << nextId << ", appended Thing id="
                << result.things.back().id << ", original Thing id=" << baseline.things[0].id << '\n';
        }
        check(!fs::exists(output), "invalid/reused next ID publishes no native output");
        check(sed_agent::readFileBytes(source) == before, "invalid next-ID refusal preserves source bytes");
    }
    auto valid = baseline;
    std::int32_t maximumId = 0;
    const auto inspectIds = [&](const auto& entities)
    {
        for ( const auto& entity : entities )
        {
            maximumId = std::max(maximumId, entity.id);
        }
    };
    inspectIds(valid.sectors);
    inspectIds(valid.things);
    inspectIds(valid.lights);
    inspectIds(valid.cogs);
    inspectIds(valid.h3doNodes);
    valid.curId = maximumId + 1;
    const auto validSource = root / "entity-next-id-valid.sed";
    const auto validOutput = root / "entity-next-id-valid-output.sed";
    writeNative(validSource, valid);
    apply(root, validSource, validOutput,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh_id","position":[0,0,0]})");
    const auto validResult = readNative(validOutput);
    check(validResult.things.back().id == maximumId + 1 && validResult.curId == maximumId + 2,
        "next ID exactly above the maximum live ID remains valid and advances once");
    auto exhausted = baseline;
    exhausted.curId = std::numeric_limits<std::int32_t>::max();
    const auto source = root / "entity-next-id-exhausted.sed";
    writeNative(source, exhausted);
    expectRefusal(root, source,
        R"({"op":"addThing","sector":0,"template":"review_item","name":"fresh_id","position":[0,0,0]})",
        "placementIdExhausted");
}

void translateEdits(const fs::path& root, const fs::path& source)
{
    const auto movedThing = root / "entity-thing-moved.jkl";
    apply(root, source, movedThing,
        R"({"op":"translateThings","things":[0],"delta":[0.5,0.25,-0.5]})");
    const auto movedLevel = sed::assets::loadFromJKL(movedThing);
    check(movedLevel.things[0].x == 0.5 && movedLevel.things[0].y == 0.25
            && movedLevel.things[0].z == -0.5 && movedLevel.things[0].sector == 0,
        "translated Thing lands exactly on position + delta");
    check(movedLevel.things[1].x == 0.0 && movedLevel.things[1].y == 0.0
            && movedLevel.things[1].z == 0.0,
        "unlisted Thing keeps its position");
    const auto movedOnce = root / "entity-thing-moved-once.jkl";
    apply(root, source, movedOnce,
        R"({"op":"translateThings","things":[0,0],"delta":[0.5,0,0],"moveFrames":false})");
    check(sed::assets::loadFromJKL(movedOnce).things[0].x == 0.5,
        "duplicate Thing entries translate once");
    const auto movedLight = root / "entity-light-moved.jkl";
    apply(root, source, movedLight,
        R"({"op":"translateLights","lights":[0],"delta":[0.5,0.25,-0.5]})");
    const auto& light = sed::assets::loadFromJKL(movedLight).lights[0];
    check(light.position.x == 0.75 && light.position.y == 0.75 && light.position.z == 0.25,
        "translated Light lands exactly on position + delta");

    // Room and contents travel together in one atomic patch: the sector
    // move lands first, so the Things' containment check sees the room
    // already at its destination.
    const auto relocated = root / "entity-room-relocated.jkl";
    apply(root, source, relocated,
        R"({"op":"translateSectors","sectors":[0],"delta":[10,0,0]},)"
        R"({"op":"translateThings","things":[0,1],"delta":[10,0,0]},)"
        R"({"op":"translateLights","lights":[0],"delta":[10,0,0]})");
    const auto relocatedLevel = sed::assets::loadFromJKL(relocated);
    check(relocatedLevel.things[0].x == 10.0 && relocatedLevel.things[1].x == 10.0
            && relocatedLevel.lights[0].position.x == 10.25,
        "sector, Things and Light relocate together in one patch");

    expectRefusal(root, source, R"({"op":"translateThings","things":[0],"delta":[10,0,0]})",
        "entityPositionOutsideSector");
    expectRefusal(root, source, R"({"op":"translateThings","things":[],"delta":[1,0,0]})",
        "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"translateThings","things":[99],"delta":[1,0,0]})",
        "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"translateThings","things":[0],"delta":[1,0,0],"moveFrames":"yes"})",
        "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"translateLights","lights":[0],"delta":[1,0,0],"ignored":true})",
        "entityOperationInvalid");
}

void runTests(const fs::path& root)
{
    const auto starter = root / "entity-starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    const auto level = fixture(starter);
    const auto source = root / "entity-source.jkl";
    writeText(source, sed::assets::buildJklExport(level).text);
    for ( const auto scenario : { 0, 1, 2, 3, 4, 5, 6 } )
    {
        try
        {
            if ( scenario == 0 ) lightEdits(root, source);
            else if ( scenario == 1 ) thingEdits(root, source);
            else if ( scenario == 2 ) clearArchitecture(root, level);
            else if ( scenario == 3 ) nativeTypedValue(root, level);
            else if ( scenario == 6 ) translateEdits(root, source);
            else addThingEdits(root, level, scenario == 5);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(false, "entity scenario " + std::to_string(scenario) + ": " + error.message);
        }
    }
    addThingRefusals(root, source, level);
    nativeNextIdAdmission(root, level);
    expectRefusal(root, source, R"({"op":"setLight","light":0})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setLight","light":0,"range":0})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setLight","light":0,"rgb":[1,0,2]})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setLight","light":0,"layer":99})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setThingTemplate","thing":0,"template":"missing"})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setThingPosition","thing":0,"position":[9,9,9],"sector":0})", "entityPositionOutsideSector");
    expectRefusal(root, source, R"({"op":"setThingPosition","thing":0,"position":[1,0,0],"sector":0})", "entityPositionOutsideSector");
    expectRefusal(root, source, R"({"op":"setThingName","thing":0,"name":"bad name"})", "entityOperationInvalid");
    expectRefusal(root, source, R"json({"op":"setThingValue","thing":0,"key":"frame","value":"(0/0/0:0/0/0)"})json", "entityFramesRequireOperation");
    expectRefusal(root, source, R"({"op":"setThingValue","thing":0,"key":"thingflags","value":"nonsense"})", "entityOperationInvalid");
    expectRefusal(root, source, R"({"op":"setThingOrientation","thing":0,"orientation":[0,0,0],"ignored":true})", "entityOperationInvalid");
    const auto jk = root / "entity-jk.jkl";
    (void)sed_agent::runCommand("new", { jk.string(), "jk" });
    expectRefusal(root, jk, R"({"op":"addLight","position":[0,0,0],"rgb":[1,0,0],"intensity":1,"range":1})", "entityColorRequiresMots");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-entity-operations-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        runTests(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected entity agent error: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected entity test error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "entity operation failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
