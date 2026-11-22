#include "agent_campaign.hpp"

#include <algorithm>

namespace sed_agent
{
bool campaignNumbersAreIntegers(const Json& value)
{
    if ( value.type == Json::Type::Number )
    {
        return value.isInteger();
    }
    if ( value.type == Json::Type::Array )
    {
        return std::all_of(value.array.begin(), value.array.end(), campaignNumbersAreIntegers);
    }
    return std::all_of(value.object.begin(), value.object.end(), [](const auto& field)
    {
        return campaignNumbersAreIntegers(field.second);
    });
}

Json campaignCheckSchema()
{
    const Json path = parseJson(R"({"type":"string","minLength":1,"maxLength":4096,"pattern":"^[^\\u0000]*$"})");
    const Json digest = parseJson(R"({"type":"string","minLength":64,"maxLength":64,"pattern":"^[a-fA-F0-9]{64}$"})");
    const Json id = parseJson(R"({"type":"string","minLength":1,"maxLength":64,"pattern":"^[a-zA-Z0-9._:-]+$"})");
    Json file = parseJson(R"({"type":"object","additionalProperties":false,
        "required":["path"],"properties":{}})");
    auto& fileProperties = file.object.at("properties");
    fileProperties.set("path", path);
    fileProperties.set("sha256", digest);
    Json input = file;
    input.object.at("required").array.push_back(Json::makeString("id"));
    auto& inputProperties = input.object.at("properties");
    inputProperties.set("id", id);
    inputProperties.set("resource", parseJson(R"({"type":"string","minLength":1,"maxLength":255,"pattern":"^[^\\u0000]*$",
        "description":"Logical resource name. Defaults to the source basename; slash direction and ASCII case are normalized."})"));
    Json baseline = file;
    baseline.object.at("required").array.push_back(Json::makeString("sha256"));

    const Json state = parseJson(R"({"type":"object","maxProperties":64,
        "propertyNames":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[^\\u0000-\\u0020\\u007f]+$"},
        "additionalProperties":{"oneOf":[{"type":"boolean"},
            {"type":"integer","minimum":-9007199254740991,"maximum":9007199254740991},
            {"type":"string","maxLength":128}]}})");
    Json sequence = parseJson(R"({"type":"object","additionalProperties":false,
        "required":["line","entry","exits"],"properties":{
            "line":{"type":"integer","minimum":0,"maximum":2147483647},
            "dependsOn":{"type":"array","maxItems":32,"uniqueItems":true},
            "exits":{"type":"object","additionalProperties":false,"properties":{}}}})");
    auto& sequenceProperties = sequence.object.at("properties");
    sequenceProperties.set("input", id);
    sequenceProperties.object.at("dependsOn").set("items", id);
    sequenceProperties.set("entry", state);
    auto& exits = sequenceProperties.object.at("exits").object.at("properties");
    exits.set("a", state);
    exits.set("b", state);

    Json schema = parseJson(R"({"$schema":"https://json-schema.org/draft/2020-12/schema",
        "title":"RED campaign state contract check","type":"object","additionalProperties":false,
        "required":["schemaVersion","episode","inputs","sequences"],"properties":{
            "schemaVersion":{"type":"integer","const":1},
            "inputs":{"type":"array","maxItems":256},"sequences":{"type":"array","maxItems":128},
            "offset":{"type":"integer","minimum":0,"maximum":2147483647,"default":0},
            "limit":{"type":"integer","minimum":1,"maximum":32,"default":16}}})");
    auto& properties = schema.object.at("properties");
    properties.set("episode", file);
    properties.set("baseline", baseline);
    properties.object.at("inputs").set("items", input);
    properties.object.at("sequences").set("items", sequence);
    schema.set("description", Json::makeString("Compare explicitly declared exit guarantees and successor entry "
        "requirements on both episode branches. Capture declared file bytes and optionally compare a previous receipt."));
    schema.set("x-runtimeConstraints", Json::makeString("Text size limits apply to UTF-8 bytes. Input IDs and "
        "episode lines are unique; associations refer to declared inputs and existing lines. At most 4096 facts and "
        "1024 sequence/input associations. Episode, baseline and request are limited to 1 MiB each. Declared inputs "
        "are limited to 64 MiB each and 256 MiB total. Hashes bind captured bytes. MCP admits files only in its "
        "workspace or declared resource roots. No script execution or runtime state verification."));
    return schema;
}
}
