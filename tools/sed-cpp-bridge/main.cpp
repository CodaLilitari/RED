// sed-cpp-bridge: read-only native inspection worker, protocol 1.
//
// One request per process. Raw file bytes arrive on stdin (never a path);
// stdout carries exactly one JSON object plus LF. Contract summary:
// docs in DELTA_INVENTORY.md (D-01) and the response fields below mirror
// native inspection worker protocol 1. No file is ever written.

#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/diagnostic.hpp"
#include "sed/level.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{

constexpr std::size_t kMaxInputBytes = 16777216;
constexpr std::size_t kMaxOutputBytes = 65536;
constexpr const char* kUpstreamCommit = "b82255998fb733c8e8c87104eb7c17f27b48376f";
constexpr const char* kSourceManifestSha256 =
    "0cf83d6f89760fa57346209235f4bf3ee4985f7f3603e1213477607333cec5a8";

std::string escapeJson(const std::string_view text)
{
    std::string out;
    const char* hex = "0123456789abcdef";
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '"') { out += "\\\""; }
        else if (ch == '\\') { out += "\\\\"; }
        else if (c >= 0x20 && c < 0x7F) { out += ch; }
        else
        {
            out += "\\u00";
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string jsonQuoted(const std::string_view text)
{
    return "\"" + escapeJson(text) + "\"";
}

struct Response
{
    std::string operation{ "unknown" };
    std::string requestId{};
    bool hasRequestId{ false };
    std::string status{ "failed" };
    std::size_t inputBytes{ 0 };
    std::string errorCode{};
    std::string errorMessage{};
    std::string extra{}; // pre-rendered extra top-level fields

    [[nodiscard]] std::string render() const
    {
        std::ostringstream out;
        out << "{\"schemaVersion\":1,\"name\":\"sed-cpp-bridge\","
               "\"version\":\"0.1.0\",\"upstreamCommit\":"
            << jsonQuoted(kUpstreamCommit)
            << ",\"sourceManifestSha256\":" << jsonQuoted(kSourceManifestSha256)
            << ",\"operation\":" << jsonQuoted(operation) << ",\"requestId\":";
        if (hasRequestId) { out << jsonQuoted(requestId); }
        else { out << "null"; }
        out << ",\"status\":" << jsonQuoted(status)
            << ",\"readOnly\":true,\"inputBytes\":" << inputBytes;
        if (!extra.empty()) { out << ',' << extra; }
        if (!errorCode.empty())
        {
            out << ",\"error\":{\"code\":" << jsonQuoted(errorCode)
                << ",\"message\":" << jsonQuoted(errorMessage) << '}';
        }
        out << '}';
        return out.str();
    }
};

int emit(Response response)
{
    std::string text = response.render();
    if (text.size() + 1 > kMaxOutputBytes)
    {
        response.status = "failed";
        response.errorCode = "output_limit";
        response.errorMessage = "Response exceeds the 65536-byte limit.";
        response.extra.clear();
        text = response.render();
        std::cout << text << '\n';
        return 6;
    }
    std::cout << text << '\n';
    if (response.status == "ok") { return 0; }
    if (response.status == "rejected")
    {
        return response.errorCode == "parse_error"
                || response.errorCode == "project_kind_mismatch" ? 3 : 2;
    }
    return response.errorCode == "io_error" ? 5 : 6;
}

bool validRequestId(const std::string_view id)
{
    if (id.size() != 32) { return false; }
    return std::all_of(id.begin(), id.end(), [](const char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::string severityName(const sed::DiagnosticSeverity severity)
{
    using enum sed::DiagnosticSeverity;
    switch (severity)
    {
    case verbose: return "verbose";
    case trace: return "trace";
    case debug: return "debug";
    case info: return "info";
    case warning: return "warning";
    case error: return "error";
    case fatal: return "fatal";
    }
    return "info";
}

std::string summaryJson(const sed::Level& level)
{
    std::size_t surfaces = 0;
    std::size_t vertices = 0;
    for (const sed::Sector& sector : level.sectors)
    {
        surfaces += sector.surfaces.size();
        vertices += sector.vertices.size();
    }
    std::ostringstream out;
    out << "\"summary\":{\"sectors\":" << level.sectors.size()
        << ",\"surfaces\":" << surfaces
        << ",\"vertices\":" << vertices
        << ",\"things\":" << level.things.size()
        << ",\"templates\":" << level.templates.size()
        << ",\"cogs\":" << level.cogs.size()
        << ",\"lights\":" << level.lights.size()
        << ",\"materials\":" << level.materials.size()
        << ",\"colormaps\":" << level.colormaps.size()
        << ",\"architecturalLightingObjects\":" << level.motsArchLighting.size()
        << "},\"resourceValidation\":{\"status\":\"not-performed\","
           "\"complete\":false,\"cogHydration\":\"deferred\","
           "\"uvNormalization\":\"not-performed\"}";
    return out.str();
}

std::string diagnosticsJson(
    const std::vector<std::pair<std::string, std::string>>& diagnostics,
    const bool truncated)
{
    std::ostringstream out;
    out << ",\"diagnostics\":[";
    bool first = true;
    for (const auto& [severity, message] : diagnostics)
    {
        if (!first) { out << ','; }
        first = false;
        out << "{\"severity\":" << jsonQuoted(severity)
            << ",\"message\":" << jsonQuoted(message) << '}';
    }
    out << "],\"diagnosticsTruncated\":" << (truncated ? "true" : "false");
    return out.str();
}

const char* kNotApplicableIntegrity =
    ",\"integrity\":{\"status\":\"not-applicable\",\"hadCorruption\":false,"
    "\"repairedChunks\":0,\"repairedSections\":[],\"unreadableRanges\":[],"
    "\"truncated\":false},\"optionalSections\":[],"
    "\"optionalSectionsTruncated\":false";

int inspectJkl(Response& response, const std::string& bytes,
    const std::string& projectKind)
{
    const std::string_view text(bytes);
    if (text.find('\0') != std::string_view::npos)
    {
        response.status = "rejected";
        response.errorCode = "parse_error";
        response.errorMessage = "JKL input contains a NUL byte.";
        return emit(response);
    }

    // Admission budgets (protocol 1): physical lines, line bytes, tokens.
    std::size_t lines = 0;
    std::size_t tokens = 0;
    std::size_t start = 0;
    int version = 0;
    bool inHeader = false;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::string_view line = text.substr(start,
            newline == std::string_view::npos ? std::string_view::npos
                                              : newline - start);
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++lines;
        if (lines > 131072 || line.size() > 65536)
        {
            response.status = "rejected";
            response.errorCode = "input_limit";
            response.errorMessage = "JKL input exceeds a protocol line budget.";
            return emit(response);
        }
        std::size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) { ++i; }
            if (i >= line.size()) { break; }
            ++tokens;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') { ++i; }
        }
        if (tokens > 1048576)
        {
            response.status = "rejected";
            response.errorCode = "input_limit";
            response.errorMessage = "JKL input exceeds the protocol token budget.";
            return emit(response);
        }
        const std::string_view trimmed = sed::text::trimView(line);
        if (sed::text::iequalsAscii(trimmed, "SECTION: HEADER")) { inHeader = true; }
        else if (inHeader && sed::text::iequalsAscii(trimmed, "END")) { inHeader = false; }
        else if (inHeader)
        {
            std::istringstream lineStream{ std::string(trimmed) };
            std::string key;
            int value = 0;
            if ((lineStream >> key >> value) && sed::text::iequalsAscii(key, "VERSION"))
            {
                version = value;
            }
        }
        if (start > text.size()) { break; }
    }
    if (version != 1 && version != 3)
    {
        response.status = "rejected";
        response.errorCode = "parse_error";
        response.errorMessage = "JKL envelope requires a HEADER section with VERSION 1 or 3.";
        return emit(response);
    }
    const bool decodedIjim = version == 3;
    if (decodedIjim != (projectKind == "ijim"))
    {
        response.status = "rejected";
        response.errorCode = "project_kind_mismatch";
        response.errorMessage = "Decoded project kind differs from the expected kind.";
        return emit(response);
    }

    std::vector<std::pair<std::string, std::string>> diagnostics;
    bool diagnosticsTruncated = false;
    sed::assets::JklLoadOptions options{};
    options.onDiagnostic = [&](const sed::DiagnosticSeverity severity,
                               const std::string_view message)
    {
        if (diagnostics.size() >= 32)
        {
            diagnosticsTruncated = true;
            return;
        }
        std::string clipped(message.substr(0, 256));
        if (message.size() > 256) { diagnosticsTruncated = true; }
        diagnostics.emplace_back(severityName(severity), std::move(clipped));
    };

    sed::Level level;
    try
    {
        std::istringstream input{ std::string(text) };
        level = sed::assets::loadFromJKL(input, options);
    }
    catch (const std::exception& error)
    {
        response.status = "rejected";
        response.errorCode = "parse_error";
        response.errorMessage = std::string(error.what()).substr(0, 256);
        return emit(response);
    }

    response.status = "ok";
    response.extra = "\"projectKind\":" + jsonQuoted(projectKind)
        + ",\"uvCoordinateSpace\":"
        + jsonQuoted(decodedIjim ? "normalized" : "texture-pixels")
        + "," + summaryJson(level)
        + diagnosticsJson(diagnostics, diagnosticsTruncated)
        + kNotApplicableIntegrity;
    return emit(response);
}

int inspectNativeSed(Response& response, const std::string& bytes,
    const std::string& projectKind)
{
    std::vector<std::byte> raw(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        raw[i] = static_cast<std::byte>(bytes[i]);
    }
    sed::assets::SedFile file;
    try
    {
        file = sed::assets::loadSedFileBytes(raw);
    }
    catch (const std::exception& error)
    {
        response.status = "rejected";
        response.errorCode = "parse_error";
        response.errorMessage = std::string(error.what()).substr(0, 256);
        return emit(response);
    }
    const std::string decoded = file.level.kind == sed::ProjectType::IJIM
        ? "ijim"
        : (file.level.kind == sed::ProjectType::MOTS ? "mots" : "jk");
    if (decoded != projectKind)
    {
        response.status = "rejected";
        response.errorCode = "project_kind_mismatch";
        response.errorMessage = "Decoded project kind differs from the expected kind.";
        return emit(response);
    }

    std::ostringstream sections;
    sections << ",\"optionalSections\":[";
    const bool sectionsTruncated = file.optionalSections.size() > 32;
    const std::size_t sectionCount =
        std::min<std::size_t>(file.optionalSections.size(), 32);
    for (std::size_t i = 0; i < sectionCount; ++i)
    {
        const sed::assets::SedOptionalSection& section = file.optionalSections[i];
        if (i) { sections << ','; }
        sections << "{\"type\":" << jsonQuoted(std::string_view(
                        reinterpret_cast<const char*>(section.type.data()), 4))
                 << ",\"payloadBytes\":" << section.payload.size() << '}';
    }
    sections << "],\"optionalSectionsTruncated\":"
             << (sectionsTruncated ? "true" : "false");

    std::ostringstream ranges;
    ranges << '[';
    bool firstRange = true;
    for (const sed::assets::SedUnreadableRange& range : file.unreadableRanges)
    {
        if (!firstRange) { ranges << ','; }
        firstRange = false;
        ranges << "{\"offset\":" << range.offset
               << ",\"length\":" << range.length << '}';
    }
    ranges << ']';
    std::ostringstream repaired;
    repaired << '[';
    bool firstRepaired = true;
    for (const std::string& name : file.repairedSections)
    {
        if (!firstRepaired) { repaired << ','; }
        firstRepaired = false;
        repaired << jsonQuoted(name);
    }
    repaired << ']';

    response.status = "ok";
    response.extra = "\"projectKind\":" + jsonQuoted(decoded)
        + ",\"uvCoordinateSpace\":"
        + jsonQuoted(decoded == "ijim" ? "normalized" : "texture-pixels")
        + "," + summaryJson(file.level)
        + ",\"diagnostics\":[],\"diagnosticsTruncated\":false"
        + ",\"integrity\":{\"status\":"
        + jsonQuoted(file.hadCorruption ? "repaired" : "clean")
        + ",\"hadCorruption\":" + (file.hadCorruption ? "true" : "false")
        + ",\"repairedChunks\":" + std::to_string(file.repairedChunks)
        + ",\"repairedSections\":" + repaired.str()
        + ",\"unreadableRanges\":" + ranges.str()
        + ",\"truncated\":false}"
        + sections.str();
    return emit(response);
}

int failInvalidRequest(Response& response, const std::string& message)
{
    response.status = "rejected";
    response.errorCode = "invalid_request";
    response.errorMessage = message;
    return emit(response);
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Response response{};

    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 3 || args[0] != "--protocol" || args[1] != "1")
    {
        return failInvalidRequest(response,
            "Expected: sed-cpp-bridge --protocol 1 <operation> [options].");
    }
    const std::string& operation = args[2];
    if (operation == "capabilities" && args.size() == 3)
    {
        response.operation = "capabilities";
        response.status = "ok";
        response.extra =
            "\"capabilities\":[\"inspect-jkl\",\"inspect-native-sed\"],"
            "\"projectKinds\":[\"jk\",\"mots\",\"ijim\"],\"writesFiles\":false,"
            "\"limits\":{\"maxInputBytes\":16777216,\"maxOutputBytes\":65536,"
            "\"maxDiagnostics\":32,\"maxDiagnosticBytes\":256,"
            "\"maxOptionalSections\":32,\"maxIntegrityItems\":32,"
            "\"maxJklLines\":131072,\"maxJklLineBytes\":65536,"
            "\"maxJklTokens\":1048576,\"maxJklSurfaceReferences\":131072,"
            "\"maxJklSurfaceExpansionTokens\":1048576,"
            "\"maxJklExpansionBytes\":67108864}";
        return emit(response);
    }
    if ((operation != "inspect-jkl" && operation != "inspect-native-sed")
        || args.size() != 7 || args[3] != "--project-kind"
        || args[5] != "--request-id" || !validRequestId(args[6])
        || (args[4] != "jk" && args[4] != "mots" && args[4] != "ijim"))
    {
        return failInvalidRequest(response,
            "Unknown, reordered, duplicate or extra arguments.");
    }
    response.operation = operation;
    response.requestId = args[6];
    response.hasRequestId = true;
    const std::string projectKind = args[4];

    // Read stdin until EOF, observing (not storing beyond) the cap.
    std::string bytes;
    char buffer[65536];
    std::size_t observed = 0;
    while (std::cin)
    {
        std::cin.read(buffer, sizeof(buffer));
        const std::streamsize got = std::cin.gcount();
        if (got <= 0) { break; }
        observed += static_cast<std::size_t>(got);
        if (observed > kMaxInputBytes)
        {
            response.inputBytes = kMaxInputBytes + 1;
            response.status = "rejected";
            response.errorCode = "input_limit";
            response.errorMessage = "Input exceeds the 16777216-byte limit.";
            return emit(response);
        }
        bytes.append(buffer, static_cast<std::size_t>(got));
    }
    response.inputBytes = observed;
    if (bytes.empty())
    {
        return failInvalidRequest(response, "Empty inspection input is rejected.");
    }

    try
    {
        if (operation == "inspect-jkl")
        {
            return inspectJkl(response, bytes, projectKind);
        }
        return inspectNativeSed(response, bytes, projectKind);
    }
    catch (const std::exception& error)
    {
        response.status = "failed";
        response.errorCode = "internal_error";
        response.errorMessage = std::string(error.what()).substr(0, 256);
        response.extra.clear();
        return emit(response);
    }
    catch (...)
    {
        response.status = "failed";
        response.errorCode = "internal_error";
        response.errorMessage = "Unexpected worker failure.";
        response.extra.clear();
        return emit(response);
    }
}
