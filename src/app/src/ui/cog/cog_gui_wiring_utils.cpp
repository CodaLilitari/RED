#include "ui/cog/cog_gui_wiring_utils.hpp"

#include <algorithm>
#include <cctype>

namespace sed::ui::cog
{
namespace
{

std::string lowerAscii(const std::string_view text)
{
    std::string out{};
    out.reserve(text.size());
    for (const char ch : text)
    {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

std::string_view symbolKindName(
    const assets::CogSchemaSymbolKind kind) noexcept
{
    using Kind = assets::CogSchemaSymbolKind;
    switch (kind)
    {
        case Kind::message: return "message";
        case Kind::int_: return "int";
        case Kind::flex: return "flex";
        case Kind::float_: return "float";
        case Kind::vector: return "vector";
        case Kind::thing: return "thing";
        case Kind::sector: return "sector";
        case Kind::surface: return "surface";
        case Kind::sound: return "sound";
        case Kind::material: return "material";
        case Kind::model: return "model";
        case Kind::template_: return "template";
        case Kind::keyframe: return "keyframe";
        case Kind::ai: return "ai";
        case Kind::cog: return "cog";
        case Kind::unknown: return "unknown";
    }
    return "unknown";
}

std::string_view targetStatusName(
    const level::CogBindingTargetStatus status) noexcept
{
    using Status = level::CogBindingTargetStatus;
    switch (status)
    {
        case Status::unknown: return "Unknown";
        case Status::notReference: return "NotReference";
        case Status::missing: return "Missing";
        case Status::invalid: return "Invalid";
        case Status::null: return "Null";
        case Status::level: return "Level";
        case Status::unplacedUnchecked: return "UnplacedUnchecked";
        case Status::staticUnchecked: return "StaticUnchecked";
    }
    return "Unknown";
}

std::string joinLines(const std::vector<std::int32_t>& lines)
{
    std::string out{};
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (i != 0)
        {
            out += ", ";
        }
        out += std::to_string(lines[i]);
    }
    return out;
}

} // namespace

PlacedCogRowMode placedCogRowMode(
    const level::CogBindingDescription& description) noexcept
{
    const bool typed = description.schemaComplete
        && description.parameterCountMatches.has_value()
        && *description.parameterCountMatches;
    return typed ? PlacedCogRowMode::typed
                 : PlacedCogRowMode::positionalFallback;
}

std::vector<PlacedCogRowView> buildPlacedCogRows(
    const level::CogBindingDescription& description)
{
    const bool typed =
        placedCogRowMode(description) == PlacedCogRowMode::typed;
    std::vector<PlacedCogRowView> rows{};
    rows.reserve(description.rows.size());
    for (const level::CogBindingRow& row : description.rows)
    {
        PlacedCogRowView view{};
        view.parameterIndex = row.parameterIndex;
        if (typed && row.symbolName.has_value())
        {
            view.label = *row.symbolName;
            view.typeName = std::string(symbolKindName(row.symbolKind));
        }
        else
        {
            view.label = "value " + std::to_string(row.parameterIndex);
        }
        view.rawValue = row.rawValue.value_or(std::string{});
        view.targetText = std::string(targetStatusName(row.target.status));
        if (!row.target.detail.empty())
        {
            view.targetText += ": ";
            view.targetText += row.target.detail;
        }
        if (row.symbol != nullptr)
        {
            view.linkText = "linkid "
                + std::to_string(row.symbol->effectiveLinkId)
                + ", mask 0x"
                + [&row]()
                {
                    std::string hex{};
                    std::uint32_t mask = row.symbol->effectiveMask;
                    if (mask == 0)
                    {
                        return std::string("0");
                    }
                    constexpr char digits[] = "0123456789abcdef";
                    while (mask != 0)
                    {
                        hex.push_back(digits[mask & 0xfu]);
                        mask >>= 4u;
                    }
                    std::reverse(hex.begin(), hex.end());
                    return hex;
                }();
        }
        view.eventLinkCandidate = row.eventLinkCandidate.value_or(false);
        rows.push_back(std::move(view));
    }
    return rows;
}

std::string placedCogFallbackNote(
    const level::CogBindingDescription& description)
{
    if (placedCogRowMode(description) == PlacedCogRowMode::typed)
    {
        return {};
    }
    std::string note(kCogPositionalFallbackLabel);
    note += " (";
    switch (description.scriptStatus)
    {
        case level::CogScriptStatus::missing:
            note += "script missing; no symbol layout is assumed";
            break;
        case level::CogScriptStatus::nameMismatch:
            note += "script name mismatch; no symbol layout is assumed";
            break;
        case level::CogScriptStatus::incomplete:
            note += "schema incomplete; no symbol layout is assumed";
            break;
        case level::CogScriptStatus::complete:
            note += "parameter count mismatch; no symbol layout is assumed";
            break;
    }
    note += ")";
    return note;
}

assets::CogPreflightTarget cogPreflightTargetForProject(
    const ProjectType kind) noexcept
{
    switch (kind)
    {
        case ProjectType::JKDF2: return assets::CogPreflightTarget::jk;
        case ProjectType::MOTS: return assets::CogPreflightTarget::mots;
        case ProjectType::IJIM: return assets::CogPreflightTarget::ijim;
    }
    return assets::CogPreflightTarget::ijim;
}

std::vector<std::string> formatCogPreflightLines(
    const assets::CogPreflightResult& result)
{
    std::vector<std::string> lines{};
    lines.push_back("COG preflight: " + result.method);
    lines.push_back("Target: " + result.targetName);
    if (result.status == assets::CogPreflightStatus::targetUnmeasured)
    {
        lines.push_back(
            "target-unmeasured: no measured registry exists for this "
            "target; no availability is claimed.");
        return lines;
    }
    if (result.retailBinarySha256.has_value())
    {
        lines.push_back(
            "Retail binary SHA-256: " + *result.retailBinarySha256);
    }
    for (const assets::CogPreflightAbsentCall& absent : result.absentCalls)
    {
        std::string line = "Call absent from target registry: "
            + absent.name + " (lines " + joinLines(absent.lines) + ")";
        if (absent.presentInOtherRetailTable)
        {
            line += " [present in other retail table]";
        }
        lines.push_back(std::move(line));
    }
    for (const std::string& absent : result.absentMessages)
    {
        lines.push_back("Message absent from target registry: " + absent);
    }
    for (const auto& binding : result.nonstandardMessageBindings)
    {
        lines.push_back(
            "Nonstandard message binding: " + binding.name
            + " (normal event delivery is not established)");
    }
    for (const std::string& missing : result.missingMessageHandlers)
    {
        lines.push_back("Declared message without handler: " + missing);
    }
    for (const auto& label : result.duplicateLabels)
    {
        lines.push_back(
            "Duplicate label: " + label.name + " (lines "
            + joinLines(label.lines) + ")");
    }
    for (const std::string& warning : result.warnings)
    {
        lines.push_back("Warning: " + warning);
    }
    for (const std::string& note : result.notes)
    {
        lines.push_back(note);
    }
    if (result.absentCalls.empty() && result.absentMessages.empty())
    {
        lines.push_back(
            "No availability findings for the measured target registry.");
    }
    return lines;
}

std::string formatCogMutationRefusal(
    const level::CogMutationFailure& failure)
{
    return failure.code + ": " + failure.message;
}

bool cogCompletionWordIsNonWorkingStatement(
    const std::string_view word) noexcept
{
    const std::string lower = lowerAscii(word);
    return lower == "break" || lower == "continue" || lower == "switch";
}

} // namespace sed::ui::cog
