#include "agent_dependencies.hpp"

#include "agent_schema.hpp"
#include "agent_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace sed_agent
{
namespace
{

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "dependencyRequestInvalid" };
}

bool validUtf8(const std::string_view text)
{
    for ( std::size_t index = 0; index < text.size(); )
    {
        const auto first = static_cast<unsigned char>(text[index++]);
        if ( first < 0x80u )
        {
            continue;
        }
        unsigned remaining{};
        std::uint32_t code{};
        std::uint32_t minimum{};
        if ( first >= 0xc2u && first <= 0xdfu )
        {
            remaining = 1;
            code = first & 0x1fu;
            minimum = 0x80u;
        }
        else if ( first >= 0xe0u && first <= 0xefu )
        {
            remaining = 2;
            code = first & 0x0fu;
            minimum = 0x800u;
        }
        else if ( first >= 0xf0u && first <= 0xf4u )
        {
            remaining = 3;
            code = first & 0x07u;
            minimum = 0x10000u;
        }
        else
        {
            return false;
        }
        if ( remaining > text.size() - index )
        {
            return false;
        }
        for ( unsigned part = 0; part < remaining; ++part )
        {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ( (next & 0xc0u) != 0x80u )
            {
                return false;
            }
            code = (code << 6u) | (next & 0x3fu);
        }
        if ( code < minimum || code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu) )
        {
            return false;
        }
    }
    return true;
}

void fields(const Json& value, const std::initializer_list<std::string_view> allowed,
    const char* context)
{
    if ( !value.isObject() )
    {
        invalid(std::string(context) + " must be an object");
    }
    for ( const auto& [key, unused] : value.object )
    {
        (void)unused;
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            // Unknown field names may themselves contain invalid UTF-8.
            invalid(std::string(context) + " contains an unknown field");
        }
    }
}

const Json& required(const Json& value, const char* name)
{
    const auto* field = value.find(name);
    if ( !field )
    {
        invalid(std::string("Missing required dependency field '") + name + "'");
    }
    return *field;
}

const std::string& stringValue(const Json& value, const char* name,
    const std::size_t minimum, const std::size_t maximum)
{
    if ( value.type != Json::Type::String || value.str.size() < minimum || value.str.size() > maximum
        || !validUtf8(value.str) )
    {
        invalid(std::string(name) + " must be valid UTF-8 within its byte limits");
    }
    return value.str;
}

std::string hashValue(const Json& value, const char* name)
{
    std::string result = stringValue(value, name, 64, 64);
    for ( char& character : result )
    {
        if ( character >= 'A' && character <= 'F' )
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if ( !(character >= '0' && character <= '9') && !(character >= 'a' && character <= 'f') )
        {
            invalid(std::string(name) + " must contain exactly 64 hexadecimal digits");
        }
    }
    return result;
}

std::size_t sizeValue(const Json& value, const std::size_t minimum, const std::size_t maximum)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || std::floor(value.number) != value.number || value.number < static_cast<double>(minimum)
        || value.number > static_cast<double>(maximum) )
    {
        invalid("Dependency numeric value must decode to an integer within its documented bounds");
    }
    return static_cast<std::size_t>(value.number);
}

std::string byteText(const std::string_view value)
{
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for ( const unsigned char byte : value )
    {
        if ( byte == '\\' )
        {
            encoded += "\\\\";
        }
        else if ( byte < 0x20u || byte > 0x7eu )
        {
            encoded += "\\x";
            encoded += kHex[byte >> 4u];
            encoded += kHex[byte & 0x0fu];
        }
        else
        {
            encoded += static_cast<char>(byte);
        }
    }
    return encoded;
}

Json countValue(const std::size_t value)
{
    return Json::makeNumber(static_cast<double>(value));
}

Json fixedStrings(const std::vector<std::string>& values)
{
    Json result = Json::makeArray();
    for ( const auto& value : values )
    {
        result.array.push_back(Json::makeString(value));
    }
    return result;
}

std::string resolutionName(const sed::level::DependencyResolution resolution)
{
    using Resolution = sed::level::DependencyResolution;
    switch ( resolution )
    {
    case Resolution::Source: return "source";
    case Resolution::Resolved: return "resolved";
    case Resolution::Missing: return "missing";
    case Resolution::Ambiguous: return "ambiguous";
    }
    throw std::logic_error("Unknown dependency resolution");
}

std::string inspectionName(const sed::level::DependencyScan inspection)
{
    using Inspection = sed::level::DependencyScan;
    switch ( inspection )
    {
    case Inspection::Pending: return "pending";
    case Inspection::Scanned: return "scanned";
    case Inspection::Leaf: return "leaf";
    case Inspection::Unsupported: return "unsupported";
    case Inspection::Incomplete: return "incomplete";
    case Inspection::BudgetStopped: return "budgetStopped";
    }
    throw std::logic_error("Unknown dependency inspection state");
}

Json reportJson(const sed::level::DependencyInspection& report)
{
    Json result = Json::makeObject();
    result.set("ok", Json::makeBool(true));
    result.set("command", Json::makeString("dependencies"));
    result.set("schemaVersion", countValue(1));
    result.set("textEncoding", Json::makeString("ascii-byte-escape-v1"));
    result.set("inputSha256", Json::makeString(report.inputSha256));
    result.set("scanComplete", Json::makeBool(report.scanComplete));
    result.set("detailsComplete", Json::makeBool(report.detailsComplete));
    result.set("allReferencesResolved", report.allReferencesResolved
        ? Json::makeBool(*report.allReferencesResolved) : Json{});
    result.set("runtimeClosureVerified", Json::makeBool(false));
    const auto count = [&](const char* name, const std::size_t value)
    {
        result.set(std::string(name) + "Observed", countValue(value));
        result.set(std::string(name) + "Total", report.scanComplete ? countValue(value) : Json{});
    };
    count("nodes", report.counts.nodes);
    count("edges", report.counts.edges);
    count("issues", report.counts.issues);
    count("missing", report.counts.missing);
    count("ambiguous", report.counts.ambiguous);
    count("unsupported", report.counts.unsupported);
    Json limits = Json::makeObject();
    for ( const auto& limit : sed::level::dependencyLimitSpecs() )
    {
        limits.set(std::string(limit.name), countValue(report.limits.*(limit.member)));
    }
    result.set("limits", std::move(limits));
    Json usage = Json::makeObject();
    usage.set("workUnits", countValue(report.usage.workUnits));
    usage.set("indexEntries", countValue(report.usage.indexEntries));
    usage.set("scanBytes", countValue(report.usage.scanBytes));
    usage.set("resourceBytes", countValue(report.usage.resourceBytes));
    usage.set("stringBytes", countValue(report.usage.stringBytes));
    usage.set("peakPending", countValue(report.usage.peakPending));
    usage.set("detailRecords", countValue(report.usage.detailRecords));
    usage.set("detailBytes", countValue(report.usage.detailBytes));
    result.set("usage", std::move(usage));
    Json archives = Json::makeArray();
    for ( const auto& source : report.sourceArchives )
    {
        Json row = Json::makeObject();
        row.set("ordinal", countValue(source.ordinal));
        row.set("label", Json::makeString(byteText(source.label)));
        row.set("sha256", Json::makeString(source.sha256));
        row.set("bytes", countValue(source.bytes));
        row.set("entries", countValue(source.entries));
        row.set("stock", Json::makeBool(source.stock));
        archives.array.push_back(std::move(row));
    }
    result.set("sourceArchives", std::move(archives));
    Json nodes = Json::makeArray();
    for ( const auto& node : report.nodes )
    {
        Json row = Json::makeObject();
        row.set("id", countValue(node.id));
        row.set("depth", countValue(node.depth));
        row.set("path", Json::makeString(byteText(node.path)));
        row.set("resolution", Json::makeString(resolutionName(node.resolution)));
        row.set("inspection", Json::makeString(inspectionName(node.inspection)));
        row.set("sha256", node.sha256.empty() ? Json{} : Json::makeString(node.sha256));
        row.set("stock", Json::makeBool(node.stock));
        Json provenance;
        if ( node.provenance )
        {
            const auto& source = *node.provenance;
            provenance = Json::makeObject();
            provenance.set("archiveOrdinal", countValue(source.archiveOrdinal));
            provenance.set("entryOrdinal", countValue(source.entryOrdinal));
            provenance.set("entryName", Json::makeString(byteText(source.entryName)));
            provenance.set("offset", countValue(source.offset));
            provenance.set("bytes", countValue(source.bytes));
            provenance.set("lookupTier", Json::makeString(std::string(source.lookupTier)));
        }
        row.set("provenance", std::move(provenance));
        nodes.array.push_back(std::move(row));
    }
    result.set("nodes", std::move(nodes));
    Json edges = Json::makeArray();
    for ( const auto& edge : report.edges )
    {
        Json row = Json::makeObject();
        row.set("parent", countValue(edge.parent));
        row.set("target", countValue(edge.target));
        row.set("authoredName", Json::makeString(byteText(edge.authoredName)));
        row.set("folder", Json::makeString(byteText(edge.folder)));
        row.set("relation", Json::makeString(std::string(edge.relation)));
        row.set("reused", Json::makeBool(edge.reused));
        edges.array.push_back(std::move(row));
    }
    result.set("edges", std::move(edges));
    Json issues = Json::makeArray();
    for ( const auto& issue : report.issues )
    {
        Json row = Json::makeObject();
        row.set("node", countValue(issue.node));
        row.set("code", Json::makeString(std::string(issue.code)));
        row.set("message", Json::makeString(std::string(issue.message)));
        issues.array.push_back(std::move(row));
    }
    result.set("issues", std::move(issues));
    result.set("unverifiedScopes", fixedStrings(report.unverifiedScopes));
    result.set("scanReasons", fixedStrings(report.scanReasons));
    result.set("detailReasons", fixedStrings(report.detailReasons));
    return result;
}

Json type(const char* value)
{
    Json result = Json::makeObject();
    result.set("type", Json::makeString(value));
    return result;
}

Json textSchema(const std::size_t minimum, const std::size_t maximum)
{
    Json result = type("string");
    result.set("minLength", countValue(minimum));
    result.set("maxLength", countValue(maximum));
    result.set("x-maxUtf8Bytes", countValue(maximum));
    return result;
}

Json hashSchema()
{
    Json result = textSchema(64, 64);
    result.set("pattern", Json::makeString("^[0-9a-fA-F]{64}$"));
    return result;
}

Json arraySchema(Json items, const std::size_t maximum)
{
    Json result = type("array");
    result.set("items", std::move(items));
    result.set("maxItems", countValue(maximum));
    return result;
}

Json objectSchema(const std::initializer_list<std::pair<const char*, Json>> properties,
    const std::initializer_list<const char*> requiredNames)
{
    Json result = type("object");
    Json fields = Json::makeObject();
    for ( const auto& [name, property] : properties )
    {
        fields.set(name, property);
    }
    result.set("properties", std::move(fields));
    Json requiredFields = Json::makeArray();
    for ( const char* name : requiredNames )
    {
        requiredFields.array.push_back(Json::makeString(name));
    }
    result.set("required", std::move(requiredFields));
    result.set("additionalProperties", Json::makeBool(false));
    return result;
}

} // namespace

std::string encodeDependencyText(const std::string_view value)
{
    return byteText(value);
}

void validateDependencyPath(const std::string_view path)
{
    if ( path.empty() || path.size() > 4096 || path.find('\0') != std::string_view::npos || !validUtf8(path) )
    {
        invalid("Dependency filesystem paths must contain 1..4096 UTF-8 bytes and no NUL");
    }
}

ParsedDependencyRequest parseDependencyRequest(const Json& request)
{
    fields(request, { "schemaVersion", "inputSha256", "archives", "extraResources", "limits" },
        "Dependency request");
    (void)sizeValue(required(request, "schemaVersion"), 1, 1);
    ParsedDependencyRequest parsed;
    parsed.inputSha256 = hashValue(required(request, "inputSha256"), "inputSha256");
    const auto& archives = required(request, "archives");
    if ( archives.type != Json::Type::Array || archives.array.size() > 16 )
    {
        invalid("Dependency archives must be an ordered array of at most 16 entries");
    }
    parsed.archives.reserve(archives.array.size());
    for ( const auto& archive : archives.array )
    {
        fields(archive, { "path", "sha256", "stock" }, "Dependency archive");
        const auto& path = stringValue(required(archive, "path"), "archive path", 1, 4096);
        validateDependencyPath(path);
        bool stock = false;
        if ( const auto* value = archive.find("stock") )
        {
            if ( value->type != Json::Type::Bool )
            {
                invalid("Dependency archive stock must be a Boolean");
            }
            stock = value->boolean;
        }
        parsed.archives.push_back({ path, hashValue(required(archive, "sha256"), "archive sha256"), stock });
    }
    if ( const auto* extras = request.find("extraResources") )
    {
        if ( extras->type != Json::Type::Array || extras->array.size() > 1024 )
        {
            invalid("Dependency extraResources must be an array of at most 1024 logical references");
        }
        parsed.extraResources.reserve(extras->array.size());
        for ( const auto& extra : extras->array )
        {
            fields(extra, { "name", "folder" }, "Dependency extra resource");
            parsed.extraResources.push_back({ stringValue(required(extra, "name"), "extra resource name", 1, 1024),
                stringValue(required(extra, "folder"), "extra resource folder", 0, 128) });
        }
    }
    if ( const auto* limits = request.find("limits") )
    {
        if ( !limits->isObject() )
        {
            invalid("Dependency limits must be an object");
        }
        const auto specs = sed::level::dependencyLimitSpecs();
        for ( const auto& [name, value] : limits->object )
        {
            const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto& item)
            {
                return item.name == name;
            });
            if ( spec == specs.end() )
            {
                invalid("Dependency limits contain an unknown field");
            }
            parsed.limits.*(spec->member) = sizeValue(value, spec->minimum, spec->maximum);
        }
    }
    return parsed;
}

void validateDependencyRequest(const Json& request)
{
    (void)parseDependencyRequest(request);
}

Json inspectDependencyRequest(const std::string_view levelBytes, const std::string_view levelFileName,
    const ParsedDependencyRequest& request, const std::span<const sed::level::DependencyArchiveInput> archives)
{
    sed::level::DependencyRequest core;
    core.inputBytes = levelBytes;
    core.levelFileName = levelFileName;
    core.archives = archives;
    core.extraResources = request.extraResources;
    core.limits = request.limits;
    try
    {
        return reportJson(sed::level::inspectDependencies(core));
    }
    catch ( const std::invalid_argument& error )
    {
        invalid(error.what());
    }
}

Json dependencyPathSchema()
{
    Json path = textSchema(1, 4096);
    path.set("pattern", Json::makeString("^[^\\u0000]+$(?![\\s\\S])"));
    return path;
}

Json dependencyRequestSchema()
{
    Json version = type("integer");
    version.set("const", countValue(1));
    Json stock = type("boolean");
    stock.set("default", Json::makeBool(false));
    Json archive = objectSchema({ { "path", dependencyPathSchema() }, { "sha256", hashSchema() },
        { "stock", std::move(stock) } }, { "path", "sha256" });
    Json extra = objectSchema({ { "name", textSchema(1, 1024) }, { "folder", textSchema(0, 128) } },
        { "name", "folder" });
    Json limits = objectSchema({}, {});
    const sed::level::DependencyLimits defaults;
    for ( const auto& spec : sed::level::dependencyLimitSpecs() )
    {
        Json limit = type("integer");
        limit.set("minimum", countValue(spec.minimum));
        limit.set("maximum", countValue(spec.maximum));
        limit.set("default", countValue(defaults.*(spec.member)));
        limits.object.at("properties").set(std::string(spec.name), std::move(limit));
    }
    Json result = objectSchema({ { "schemaVersion", std::move(version) }, { "inputSha256", hashSchema() },
        { "archives", arraySchema(std::move(archive), 16) },
        { "extraResources", arraySchema(std::move(extra), 1024) }, { "limits", std::move(limits) } },
        { "schemaVersion", "inputSha256", "archives" });
    result.set("$schema", Json::makeString("https://json-schema.org/draft/2020-12/schema"));
    result.set("title", Json::makeString("RED dependency inspection request"));
    result.set("description", Json::makeString(
        "Inspect captured raw JKL and ordered hash-bound archives without file or journal publication. "
        "Missing references and work/detail limits return diagnostic reports; inspect completeness and resolution separately."));
    result.set("x-runtimeConstraints", Json::makeString(
        "All request strings must be valid UTF-8; maxLength counts characters while runtime also enforces x-maxUtf8Bytes. "
        "Filesystem paths contain no NUL. Limits validate finite decoded integer values. Source/archive digests must match "
        "captured bytes. The source basename is at most 255 bytes and ends in .jkl. Raw captures are not an atomic "
        "filesystem snapshot. Each COG scan is limited to 256 KiB and 1024 physical lines; its conservative "
        "diagnostic-evidence expansion estimate is capped at 16 MiB, not a peak-memory guarantee. "
        "Runtime closure is never certified."));
    return result;
}

} // namespace sed_agent
