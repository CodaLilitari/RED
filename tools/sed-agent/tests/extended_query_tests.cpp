#include "../agent_workspace.hpp"
#include "../agent_query_details.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/values.hpp"

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
int failures{};
int requestNumber{};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const sed_agent::Json& property(const sed_agent::Json& value, const char* name)
{
    const auto* found = value.find(name);
    if ( !found )
    {
        throw std::runtime_error(std::string("missing detail property: ") + name);
    }
    return *found;
}

sed_agent::Json query(const fs::path& root, const fs::path& source, const std::string& body)
{
    const auto path = root / ("query-" + std::to_string(++requestNumber) + ".json");
    std::ofstream output(path, std::ios::binary);
    output << body;
    output.close();
    return sed_agent::runCommand("query", { source.string(), path.string() });
}

void save(const sed::Level& level, const fs::path& path)
{
    const auto bytes = sed::assets::saveToSedBytes(level);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

template <typename Function>
void scenario(const char* name, Function function)
{
    try
    {
        function();
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, std::string(name) + ": " + error.errorCode + ": " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string(name) + ": " + error.what());
    }
}

void runTests(const fs::path& root)
{
    const auto starter = root / "starter.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    auto level = sed::assets::loadFromJKL(starter);
    level.templates.clear();
    level.things.clear();
    level.cogs.clear();
    auto& sector = level.sectors[0];
    sector.flags = -2147483647;
    sector.sound = "room.wav";
    sector.soundVolume = 0.25;
    sector.thrust = { 1, 2, 3 };
    sector.ambient = { 0.125f, 0.25f, 0.5f, 1.0f };
    for ( const auto* name : { "DoorBase", "other", "doorChild" } )
    {
        sed::ThingTemplate item{};
        item.name = name;
        item.parent = std::string(name) == "doorChild" ? "DoorBase" : "none";
        item.values.push_back(sed::deserializeTplValue("health", "25"));
        item.values.push_back(sed::deserializeTplValue("health", "75"));
        level.templates.push_back(std::move(item));
    }
    auto& cog = level.newCog();
    cog.name = "door.cog";
    sed::CogValue first{};
    first.name = "target";
    first.type = sed::ValueType::integer;
    first.intValue = 17;
    cog.values.push_back(first);
    first.intValue = 23;
    cog.values.push_back(first);
    auto& inside = level.newThing();
    inside.name = "DoorInside";
    inside.templateName = "doorChild";
    inside.sector = 0;
    inside.archLightingIndex = 0;
    auto& boundary = level.newThing();
    boundary.name = "boundary";
    boundary.templateName = "other";
    boundary.sector = 0;
    boundary.x = 1;
    auto& outside = level.newThing();
    outside.name = "outside";
    outside.templateName = "other";
    outside.sector = 0;
    outside.x = 5;
    sed::MotsArchLightObject object{};
    object.meshes.resize(1);
    object.meshes[0].vertices = { { 0.125f, 0.25f, 0.5f, 0.75f }, { 0.5f, 0.75f, 0.25f, 0.125f } };
    level.motsArchLighting.push_back(object);
    const auto source = root / "records.sed";
    save(level, source);
    const auto before = sed_agent::readFileBytes(source);

    scenario("sector details", [&]
    {
        const auto result = query(root, source, R"({"collection":"sectors"})");
        const auto& row = property(result, "items").array.at(0);
        check(property(row, "flags").number == -2147483647, "sector flags retain signed legacy representation");
        check(property(row, "sound").str == "room.wav" && property(row, "soundVolume").number == 0.25,
            "sector authored sound is readable");
        check(property(row, "thrust").array.at(2).number == 3
                && property(row, "ambientRgb").array.at(1).number == 0.25,
            "sector vector and color fields remain independent");
        check(property(property(row, "geometryBounds"), "min").array.at(0).number == -1
                && property(property(row, "geometryBounds"), "max").array.at(2).number == 1,
            "sector bounds describe saved geometry");
        check(property(row, "neighbors").array.empty() && property(row, "neighborReferencesValid").boolean,
            "unadjoined room exposes an empty valid neighbor list");
        check(property(result, "inputSha256").str == sed_agent::sha256Hex(before), "rich query binds exact source bytes");
    });
    scenario("template details and filtered pagination", [&]
    {
        const auto page = query(root, source, R"({"collection":"templates","search":"DOOR","limit":1})");
        check(property(page, "total").number == 2 && property(page, "nextOffset").number == 1,
            "case-insensitive filter precedes pagination");
        const auto next = query(root, source, R"({"collection":"templates","search":"DOOR","offset":1,"limit":1})");
        const auto& row = property(next, "items").array.at(0);
        check(property(row, "index").number == 2 && property(row, "parent").str == "DoorBase",
            "filtered row retains document index and declared parent");
        const auto& records = property(row, "sourceParameters").array;
        check(records.size() == 2 && property(records[0], "name").str == "health"
                && property(records[1], "name").str == "health" && property(records[1], "value").str == "75",
            "template declarations preserve duplicate keys and order");
        check(property(row, "parameterScope").str == "declared" && property(next, "nextOffset").type == sed_agent::Json::Type::Null,
            "template values are declarations and final pagination is explicit");
    });
    scenario("COG details", [&]
    {
        const auto result = query(root, source, R"({"collection":"cogs","search":"DOOR"})");
        const auto& row = property(result, "items").array.at(0);
        const auto& parameters = property(row, "sourceParameters").array;
        check(parameters.size() == 2 && property(parameters[0], "name").str == "target"
                && property(parameters[1], "value").str == "23", "COG parameters retain duplicate symbol records");
        check(property(row, "values").array.at(0).str == "17" && property(row, "values").array.at(1).str == "23",
            "compatible COG positional values remain available");
        check(property(row, "bindingStatus").str == "unverified-use-bindings-with-explicit-resources",
            "stored COG records do not imply validated bindings");
    });
    scenario("ARCH summaries and sample page", [&]
    {
        const auto result = query(root, source, R"({"collection":"architecture"})");
        const auto& row = property(result, "items").array.at(0);
        check(property(row, "meshVertexCounts").array.at(0).number == 2
                && property(row, "boundThings").array.at(0).number == 0, "ARCH summary exposes stored topology and owners");
        check(property(row, "samplesSha256").str.size() == 64
                && property(row, "modelCompatibility").str == "unverified-use-arch-validate-with-explicit-resources",
            "ARCH samples have a digest without claiming model compatibility");
        const auto samples = query(root, source,
            R"({"collection":"architecturalVertices","lightingIndex":0,"mesh":0,"offset":1,"limit":1})");
        const auto& sample = property(samples, "items").array.at(0);
        check(property(samples, "total").number == 2 && property(sample, "index").number == 1
                && property(sample, "mono").number == 0.5 && property(sample, "rgb").array.at(2).number == 0.125,
            "sample pagination preserves independent mono and RGB channels");
    });
    scenario("point containment", [&]
    {
        const auto result = query(root, source, R"({"collection":"thingContainment"})");
        const auto& rows = property(result, "items").array;
        check(rows.size() == 3 && property(rows[0], "status").str == "inside"
                && property(rows[1], "status").str == "boundary" && property(rows[2], "status").str == "outside",
            "inside, boundary and outside have distinct semantics");
        check(property(rows[0], "assignedSectorMatches").boolean && property(rows[1], "onBoundary").boolean
                && !property(rows[2], "assignedSectorMatches").boolean, "containment reports assignment independently");
        const auto filtered = query(root, source, R"({"collection":"thingContainment","search":"doorchild"})");
        check(property(filtered, "total").number == 1, "containment search includes template names");
        auto overlapping = level;
        overlapping.sectors.push_back(overlapping.sectors[0]);
        const auto overlapPath = root / "overlap.sed";
        save(overlapping, overlapPath);
        const auto ambiguous = query(root, overlapPath, R"({"collection":"thingContainment","limit":1})");
        const auto& row = property(ambiguous, "items").array.at(0);
        check(property(row, "status").str == "ambiguous"
                && property(row, "assignedSectorMatches").type == sed_agent::Json::Type::Null
                && property(row, "onBoundary").type == sed_agent::Json::Type::Null,
            "overlapping rooms do not invent a unique or matching assignment");
    });
    for ( const auto* body : { R"({"collection":"templates","offset":0.5})",
        R"({"collection":"cogs","search":7})", R"({"collection":"sectors","limit":0})",
        R"({"collection":"architecturalVertices","lightingIndex":0})",
        R"({"collection":"architecturalVertices","lightingIndex":99,"mesh":0})",
        R"({"collection":"templates","mesh":0})" } )
    {
        try
        {
            (void)query(root, source, body);
            check(false, std::string("malformed rich query must refuse: ") + body);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "queryDetailsInvalid", "rich query refusal has a stable usage code");
        }
    }
    check(sed_agent::readFileBytes(source) == before, "rich queries never modify the source file");
}

sed_agent::Json directQuery(const sed::Level& level, const std::string& body)
{
    auto result = sed_agent::queryRecordDetails(level, sed_agent::parseJson(body));
    if ( !result )
    {
        throw std::runtime_error("rich collection was not dispatched");
    }
    return std::move(*result);
}

void expectTooLarge(const sed::Level& level, const std::string& body, const std::string& description)
{
    try
    {
        (void)directQuery(level, body);
        check(false, description + " must refuse before expensive row construction");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitRefusal && error.errorCode == "queryDetailsTooLarge",
            description + " must have the stable detail-budget refusal");
    }
}

void thingCensus(const fs::path& root)
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.sectors.resize(1);
    auto addTemplate = [&](const char* name, const char* parent, const char* type, const char* flags)
    {
        sed::ThingTemplate tpl{};
        tpl.name = name;
        tpl.parent = parent;
        if ( type ) tpl.values.push_back(sed::deserializeTplValue("type", type));
        if ( flags ) tpl.values.push_back(sed::deserializeTplValue("thingflags", flags));
        level.templates.push_back(std::move(tpl));
    };
    addTemplate("agent", "none", "PLAYER", "0");
    addTemplate("start", "AGENT", nullptr, "1000");
    addTemplate("pickup", "none", "item", "18000"); // excluded in both modes
    addTemplate("cycle", "cycle", nullptr, nullptr);
    auto add = [&](const char* name, int sector = 0) -> sed::Thing&
    {
        auto& thing = level.newThing();
        thing.name = "irrelevant";
        thing.templateName = name;
        thing.sector = sector;
        return thing;
    };
    add("start");
    auto& sp = add("START");
    sp.values.push_back(sed::deserializeTplValue("thingflags", "10000"));
    sp.values.push_back(sed::deserializeTplValue("thingflags", "A000")); // last wins: SP only, no medium
    auto& mp = add("agent");
    mp.values.push_back(sed::deserializeTplValue("thingflags", "14000")); // MP only, no hard
    add("agent", -1);
    add("pickup");
    add("outside");
    add("cycle");
    auto& opaque = add("pickup");
    opaque.values.push_back(sed::deserializeTplValue("thingflags", "oops"));
    auto& item = add("start");
    item.layer = 7;
    item.values.push_back(sed::deserializeTplValue("type", "item"));
    item.values.push_back(sed::deserializeTplValue("thingflags", "0"));

    const auto file = root / "census.sed";
    save(level, file);
    const auto before = sed_agent::readFileBytes(file);
    const auto page = query(root, file, R"({"collection":"thingCensus","limit":1,"intendedPlayers":2})");
    const auto& summary = property(page, "summary");
    check(property(summary, "placements").number == 9 && property(summary, "invalidSector").number == 1
            && property(summary, "unknownEligibility").number == 3
            && property(summary, "unresolvedTemplate").number == 2,
        "census preserves unresolved, opaque, and invalid-sector placements");
    const auto& single = property(summary, "singlePlayer");
    const auto& multi = property(summary, "multiplayer");
    check(property(single, "easy").number == 2 && property(single, "medium").number == 2
            && property(single, "hard").number == 3 && property(multi, "easy").number == 2
            && property(multi, "medium").number == 3 && property(multi, "hard").number == 2,
        "all six flag masks count eligible placements after local overrides");
    const auto& starts = property(page, "multiplayerPlayerStarts");
    check(property(property(starts, "easy"), "confirmed").number == 1
            && property(property(starts, "easy"), "possibleAdditional").number == 2
            && property(property(starts, "easy"), "comparison").str == "indeterminate"
            && property(property(starts, "medium"), "comparison").str == "meetsIntent",
        "compare only eligible player placements and separately bounded unknown candidates");
    check(property(page, "total").number == 7 && property(page, "items").array.size() == 1
            && property(page, "nextOffset").number == 1 && property(page, "inputSha256").str == sed_agent::sha256Hex(before),
        "page binds the exact input and exposes all grouped totals");
    const auto last = query(root, file, R"({"collection":"thingCensus","offset":6,"intendedPlayers":4})");
    const auto& lastGroup = property(last, "items").array.at(0);
    check(property(lastGroup, "layer").number == 7 && property(lastGroup, "sector").number == 0
            && property(lastGroup, "template").str == "start" && property(lastGroup, "type").str == "item"
            && property(last, "nextOffset").type == sed_agent::Json::Type::Null,
        "effective item-type placements remain attributable to area and template");
    check(property(property(property(last, "multiplayerPlayerStarts"), "easy"), "comparison").str == "belowIntent",
        "definite shortfall requires even the possible additional candidates to fall short");
    const auto noIntent = query(root, file, R"({"collection":"thingCensus","offset":99})");
    check(property(noIntent, "items").array.empty() && !noIntent.find("intendedPlayers")
            && !property(property(noIntent, "multiplayerPlayerStarts"), "easy").find("comparison")
            && property(property(noIntent, "summary"), "placements").number == 9,
        "empty pages still carry complete counts without an inferred player intent");
    check(property(property(query(root, file, R"({"schemaVersion":1,"collection":"thingCensus"})"), "summary"),
            "placements").number == 9, "the census accepts the request envelope's schemaVersion like other collections");
    for ( const auto* request : { R"({"collection":"thingCensus","intendedPlayers":0})",
        R"({"collection":"thingCensus","intendedPlayers":1.5})", R"({"collection":"thingCensus","search":"ammo"})",
        R"({"schemaVersion":2,"collection":"thingCensus"})" } )
    {
        try
        {
            (void)query(root, file, request);
            check(false, "malformed census controls must refuse");
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "queryDetailsInvalid", "invalid census controls have a stable usage code");
        }
    }
    try
    {
        (void)query(root, file, R"({"collection":"thingCensus","sourceRevision":"stale","offset":1})");
        check(false, "changed census input must refuse");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.code == sed_agent::kExitConflict, "census pages enforce source binding");
    }
    check(sed_agent::readFileBytes(file) == before, "census preserves source bytes");
    level.kind = sed::ProjectType::IJIM;
    try
    {
        (void)directQuery(level, R"({"collection":"thingCensus"})");
        check(false, "IJIM cannot use JK/MotS census masks");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == "thingCensusUnsupported", "unsupported census profile is explicit");
    }
    level = {};
    level.sectors.resize(1);
    addTemplate("incomplete", "missing-base", "item", nullptr);
    auto& explicitFields = add("incomplete");
    sed::TplValue rawFlags{};
    rawFlags.name = "thingflags";
    rawFlags.type = sed::ValueType::str;
    rawFlags.stringValue = "1000";
    explicitFields.values.push_back(rawFlags);
    explicitFields.values.push_back(sed::deserializeTplValue("type", "Player"));
    add("incomplete");
    const auto partial = directQuery(level, R"({"collection":"thingCensus"})");
    const auto& partialSummary = property(partial, "summary");
    check(property(partialSummary, "unresolvedTemplate").number == 2
            && property(partialSummary, "unknownEligibility").number == 1
            && property(property(partialSummary, "singlePlayer"), "easy").number == 0
            && property(property(partialSummary, "singlePlayer"), "medium").number == 1
            && property(property(property(partial, "multiplayerPlayerStarts"), "medium"), "confirmed").number == 1
            && property(property(property(partial, "multiplayerPlayerStarts"), "easy"), "possibleAdditional").number == 0,
        "missing ancestors leave absent fields unknown while closer template and instance declarations remain effective");
    level = {};
    const auto empty = directQuery(level, R"({"collection":"thingCensus","intendedPlayers":1})");
    check(property(empty, "total").number == 0 && property(empty, "items").array.empty()
            && property(property(property(empty, "multiplayerPlayerStarts"), "hard"), "comparison").str == "belowIntent",
        "empty levels report a definite authored shortfall");
    level.templates.resize(1);
    level.templates[0].name.assign(256, 't');
    expectTooLarge(level, R"({"collection":"thingCensus"})", "census template-name admission");
    level.templates.clear();
    level.things.resize(65'537);
    expectTooLarge(level, R"({"collection":"thingCensus","limit":1})", "global census work cap");
}

// renderBudget walks frames the way the retail renderer does, so a level can
// be held to its 128 sectors a frame before it is run.
void pillaredRouteMerge()
{
    sed::Level level{};
    level.kind = sed::ProjectType::JKDF2;
    // Crossing aisles around four solid piers. Some routes inspect an extra portal after a merge.
    int slots[5][5]{};
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
        {
            slots[y][x] = -1;
            if (y % 2 && x % 2) continue;
            slots[y][x] = static_cast<int>(level.sectors.size());
            sed::level::levelCreateSectorPrimitiveBox(level, sed::level::SectorPrimitive::Cube,
                {double(x), double(y), 0}, {double(x + 1), double(y + 1), 1});
            for (auto& face : level.sectors.back().surfaces)
                face.texVertices.resize(face.vertices.size());
        }
    const auto join = [&](int one, int two, bool alongX)
    {
        const auto face = [&](int sec, int sign) {
            const auto& surfaces = level.sectors[sec].surfaces;
            for (std::size_t i = 0; i < surfaces.size(); ++i)
                if (sign * (alongX ? surfaces[i].normal.x : surfaces[i].normal.y) > 0.99)
                    return static_cast<int>(i);
            return -1;
        };
        const sed::SurfaceRef a{one, face(one, -1)}, b{two, face(two, 1)};
        check(sed::level::levelSysAdjoinSurfaces(level, a, b, true), "pillar hallway joins");
        for (auto r : {a,b})
        {
            auto& surf = level.sectors[r.sector].surfaces[r.surface];
            surf.geo = 0;
            surf.adjoinFlags = 7;
        }
    };
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
        {
            const int a = slots[y][x];
            if (a < 0) continue;
            if (x + 1 < 5 && slots[y][x + 1] >= 0) join(a, slots[y][x + 1], true);
            if (y + 1 < 5 && slots[y + 1][x] >= 0) join(a, slots[y + 1][x], false);
        }
    const std::string query = R"({"collection":"renderBudget","proposedMerge":[1,2],)"
        R"("viewpoints":[{"position":[0.5,0.5,0.5],"yawDegrees":-180},{"position":[2.5,2.5,0.5],"yawDegrees":180}]})";
    auto before = sed::assets::saveToSedBytes(level);
    const auto result = directQuery(level, query);
    const auto& rows = property(result, "items").array;
    check(rows.size() == 2 && property(result, "adjoinPairsBefore").number == 24 &&
          property(result, "adjoinPairsAfter").number == 23 && property(rows[0], "portalTestDelta").number == 1 &&
          !result.find("fps") &&
          sed::assets::saveToSedBytes(level) == before, "route review uses a private copy of the proposed merge");
    const auto worst = static_cast<std::size_t>(property(result, "worstViewpoint").number);
    check(worst < rows.size() && property(rows[worst], "crossingDelta").number >=
          property(rows[1-worst], "crossingDelta").number, "find worst matched route station");
    const auto protectedBoundary = [&] {
        try { (void)directQuery(level, query); return false; }
        catch (const sed_agent::AgentError& e) { return e.errorCode == "mergeBoundaryProtected"; }
    };
    level.sectors[1].flags |= sed::SECF_Underwater;
    check(protectedBoundary(), "water boundary remains intact");
    level.sectors[1].flags &= ~sed::SECF_Underwater;
    level.sectors[1].flags |= 0x4; // Native sector COG link.
    check(protectedBoundary(), "a native COG-linked sector preserves its entry/exit boundary");
    level.sectors[1].flags &= ~0x4;
    auto outside = query;
    outside.replace(outside.find("[0.5,0.5,0.5]"), 13, "[90,90,90],\"sector\":0");
    try { (void)directQuery(level, outside); check(false, "reject impossible route positions"); }
    catch (const sed_agent::AgentError& e)
    {
        check(e.errorCode == "renderBudgetViewpointOutside", "reject a wrong explicit sector for a player viewpoint");
    }
    for (auto& face : level.sectors[1].surfaces)
        if (face.adjoin && face.adjoin->sector == 2) face.surfaceFlags |= sed::SF_CogLinked;
    check(protectedBoundary(), "scripted surface remains intact");
}

void renderBudget(const fs::path& root)
{
    // 140 rooms in a row along +Y, each open to the next: from either end one frame lists them all.
    sed::Level level{};
    level.kind = sed::ProjectType::JKDF2;
    constexpr int rooms = 140;
    for ( int i = 0; i < rooms; ++i )
    {
        (void)sed::level::levelCreateSectorPrimitiveBox(level, sed::level::SectorPrimitive::Cube,
            sed::TVector::Create(0, i, 0), sed::TVector::Create(1, i + 1, 1));
    }
    const auto face = [&level](const int room, const double inwardY)
    {
        const auto& surfaces = level.sectors[static_cast<std::size_t>(room)].surfaces;
        for ( std::size_t i = 0; i < surfaces.size(); ++i )
        {
            if ( surfaces[i].normal.y * inwardY > 0.999 ) return static_cast<std::int32_t>(i);
        }
        return std::int32_t{ -1 };
    };
    for ( int i = 0; i + 1 < rooms; ++i )
    {
        const sed::SurfaceRef out{ i, face(i, -1.0) };
        const sed::SurfaceRef in{ i + 1, face(i + 1, 1.0) };
        check(sed::level::levelSysAdjoinSurfaces(level, out, in, true), "render budget fixture joins its rooms");
        for ( const auto ref : { out, in } )
        {
            auto& surface = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces[static_cast<std::size_t>(ref.surface)];
            surface.geo = 0;
            surface.adjoinFlags = 7;
        }
    }
    const auto swept = directQuery(level, R"({"schemaVersion":1,"collection":"renderBudget","yawSteps":4,"pitchDegrees":[0],"limit":2})");
    check(property(swept, "scope").str == "retail-render-frame-capacity"
            && property(property(swept, "limits"), "visibleSectors").number == 128
            && property(property(swept, "limits"), "alphaSurfaces").number == 32
            && property(swept, "viewpoints").number == rooms && property(swept, "frames").number == rooms * 4
            && property(swept, "complete").boolean && property(swept, "maxVisibleSectors").number == rooms
            && property(swept, "overSectorLimit").number == 24 && property(swept, "overAlphaLimit").number == 0
            && property(swept, "total").number == rooms && property(swept, "nextOffset").number == 2,
        "renderBudget sweeps every room's centre and counts the viewpoints over the retail limit");
    const auto& worst = property(swept, "items").array;
    check(worst.size() == 2 && property(worst[0], "sector").number == 0 && property(worst[0], "visibleSectors").number == rooms
            && property(worst[0], "yawDegrees").number == 0 && property(worst[0], "overLimit").boolean
            && property(worst[1], "sector").number == rooms - 1 && property(worst[1], "yawDegrees").number == 180
            && !worst[0].find("sectors"),
        "the heaviest viewpoints come first, each with the heading of its heaviest frame");

    const auto one = directQuery(level, R"({"collection":"renderBudget","listSectors":true,)"
        R"("viewpoints":[{"position":[0.5,136.5,0.5],"yawDegrees":0},{"position":[0.5,3.5,0.5],"sector":3}],"yawSteps":2})");
    const auto& rows = property(one, "items").array;
    check(property(one, "viewpoints").number == 2 && property(one, "frames").number == 7 && rows.size() == 2
            && property(rows[0], "sector").number == 3 && property(rows[0], "visibleSectors").number == rooms - 3
            && property(rows[0], "overLimit").boolean
            && property(rows[1], "sector").number == 136 && property(rows[1], "visibleSectors").number == 4
            && property(rows[1], "alphaYawDegrees").number == 0 && property(rows[1], "alphaPitchDegrees").number == 0
            && property(rows[1], "sectors").array.size() == 4 && property(rows[1], "sectors").array[1].number == 137
            && !property(rows[1], "overLimit").boolean,
        "a viewpoint finds its own sector, and one given a heading is a single frame with its sectors in the engine's order");

    const auto chosen = directQuery(level, R"({"collection":"renderBudget","sectors":[70],"yawSteps":2,"pitchDegrees":[0]})");
    check(property(chosen, "viewpoints").number == 1 && property(chosen, "maxVisibleSectors").number == 71
            && property(chosen, "overSectorLimit").number == 0,
        "sectors limits the sweep to the named rooms");

    for ( const auto* request : { R"({"collection":"renderBudget","yawSteps":0})", R"({"collection":"renderBudget","yawSteps":73})",
        R"({"collection":"renderBudget","pitchDegrees":[]})", R"({"collection":"renderBudget","pitchDegrees":[95]})",
        R"({"collection":"renderBudget","fieldOfViewDegrees":2})", R"({"collection":"renderBudget","sectors":[140]})",
        R"({"collection":"renderBudget","sectors":[1],"viewpoints":[{"position":[0.5,0.5,0.5]}]})",
        R"({"collection":"renderBudget","viewpoints":[{"position":[0.5,0.5]}]})",
        R"({"collection":"renderBudget","viewpoints":[{"position":[0.5,0.5,0.5],"pitchDegrees":10}]})",
        R"({"collection":"renderBudget","viewpoints":[{"position":[0.5,0.5,0.5],"roll":1}]})",
        R"({"collection":"renderBudget","listSectors":1})", R"({"collection":"renderBudget","search":"x"})",
        R"({"schemaVersion":2,"collection":"renderBudget"})" } )
    {
        try
        {
            (void)directQuery(level, request);
            check(false, std::string("renderBudget must refuse ") + request);
        }
        catch ( const sed_agent::AgentError& error )
        {
            check(error.errorCode == "queryDetailsInvalid", std::string("renderBudget refuses ") + request);
        }
    }
    try
    {
        (void)directQuery(level, R"({"collection":"renderBudget","viewpoints":[{"position":[9,9,9]}]})");
        check(false, "a viewpoint outside every sector must be refused");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == "renderBudgetViewpointOutside", "a viewpoint outside every sector is refused by name");
    }
    level.kind = sed::ProjectType::IJIM;
    try
    {
        (void)directQuery(level, R"({"collection":"renderBudget"})");
        check(false, "renderBudget must refuse a game it does not describe");
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(error.errorCode == "renderBudgetUnsupported", "renderBudget describes JK and MotS only");
    }
    level.kind = sed::ProjectType::JKDF2;

    // Through the command: the receipt carries the document identity like every query.
    const auto file = root / "budget.sed";
    save(level, file);
    const auto receipt = query(root, file, R"({"collection":"renderBudget","sectors":[0],"yawSteps":4,"pitchDegrees":[0]})");
    check(property(receipt, "maxVisibleSectors").number == rooms && receipt.find("inputSha256") && receipt.find("sourceRevision"),
        "the query command returns the same report for a saved level");
}

void readbackLimits(const fs::path& root)
{
    sed::Level level{};
    level.templates.resize(1);
    level.templates[0].name = "large";
    level.templates[0].values.assign(4097, sed::deserializeTplValue("health", "1"));
    expectTooLarge(level, R"({"collection":"templates"})", "nested parameter budget");
    auto empty = directQuery(level, R"({"collection":"templates","offset":1})");
    check(property(empty, "items").array.empty(), "unselected large parameter records are not materialized");
    level.templates[0].values.clear();
    sed::TplValue text{};
    text.name = "text";
    text.type = sed::ValueType::str;
    text.stringValue.assign(1024 * 1024, 'a');
    level.templates[0].values.push_back(std::move(text));
    expectTooLarge(level, R"({"collection":"templates"})", "authored text budget");

    level = {};
    level.cogs.resize(1);
    level.cogs[0].name = "large.cog";
    sed::CogValue value{};
    value.name = "text";
    value.type = sed::ValueType::str;
    value.stringValue.assign(600 * 1024, 'x');
    level.cogs[0].values.push_back(std::move(value));
    expectTooLarge(level, R"({"collection":"cogs"})", "two COG value representations share text budget");

    level = {};
    level.motsArchLighting.resize(1);
    level.motsArchLighting[0].meshes.resize(1);
    level.motsArchLighting[0].meshes[0].vertices.resize(1'000'001);
    expectTooLarge(level, R"({"collection":"architecture"})", "ARCH sample digest budget");
    const auto sample = directQuery(level,
        R"({"collection":"architecturalVertices","lightingIndex":0,"mesh":0,"offset":1000000,"limit":1})");
    check(property(sample, "items").array.size() == 1 && property(sample, "total").number == 1'000'001,
        "one sample remains readable without hashing the whole ARCH object");
    level = {};
    level.motsArchLighting.resize(101);
    level.things.resize(10'000);
    expectTooLarge(level, R"({"collection":"architecture","limit":200})", "cumulative ARCH owner scan budget");

    const auto starter = root / "nonfinite-source.jkl";
    (void)sed_agent::runCommand("new", { starter.string(), "mots" });
    level = sed::assets::loadFromJKL(starter);
    level.things.clear();
    level.newThing().sector = 0;
    level.sectors[0].vertices[0].x = std::numeric_limits<double>::quiet_NaN();
    const auto unknown = directQuery(level, R"({"collection":"thingContainment"})");
    const auto& row = property(unknown, "items").array.at(0);
    check(property(row, "status").str == "unknown"
            && property(row, "assignedSectorMatches").type == sed_agent::Json::Type::Null
            && property(row, "candidates").type == sed_agent::Json::Type::Null,
        "nonfinite geometry cannot certify outside or a sector assignment");
    const auto sectors = directQuery(level, R"({"collection":"sectors"})");
    check(property(property(sectors, "items").array.at(0), "geometryBounds").type == sed_agent::Json::Type::Null,
        "nonfinite sector bounds remain null");

    level = sed::assets::loadFromJKL(starter);
    const auto cube = level.sectors[0];
    level.sectors.assign(400, cube);
    level.things.clear();
    level.things.resize(200);
    // All rooms are valid but overlap, so the selector must classify them all.
    // Each precision plane comparison also walks the face's stored corners.
    expectTooLarge(level, R"({"collection":"thingContainment","limit":200})",
        "containment admission includes precision face-corner work across the page");
}

void filterAndContainmentWork()
{
    sed::Level level{};
    level.templates.resize(33);
    for ( auto& item : level.templates )
    {
        item.name.assign(256 * 1024, 'a');
    }
    // No rows match, so an output-page allowance alone cannot bound this scan.
    // The proposed cumulative eight-MiB search allowance must reject it.
    expectTooLarge(level, R"({"collection":"templates","search":"Z","limit":1})",
        "unmatched names share a cumulative search budget");
    level = {};
    level.templates.resize(2);
    level.templates[0].name = std::string(8192, 'A') + "b";
    level.templates[1].name = std::string(8192, 'A') + "c";
    const auto pattern = std::string(512, 'a') + "B";
    const auto result = directQuery(level,
        "{\"collection\":\"templates\",\"search\":\"" + pattern + "\"}");
    check(property(result, "total").number == 1
            && property(property(result, "items").array.at(0), "index").number == 0,
        "long repetitive prefixes preserve ASCII case-insensitive substring semantics");

    level = {};
    level.sectors.resize(100'001);
    level.things.resize(200);
    // Empty sectors still cost one classification attempt per queried Thing.
    expectTooLarge(level, R"({"collection":"thingContainment","limit":200})",
        "empty-sector scans count toward cumulative containment work");
}
} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-extended-query-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    scenario("extended query fixture", [&] { runTests(root); });
    scenario("authored Thing census", [&] { thingCensus(root); });
    scenario("retail render budget", [&] { renderBudget(root); });
    scenario("compare a proposed merge on a pillared hallway route", pillaredRouteMerge);
    scenario("readback limits", [&] { readbackLimits(root); });
    scenario("filter and containment work", filterAndContainmentWork);
    fs::remove_all(root);
    std::cout << "extended query failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
