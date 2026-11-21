#pragma once

#include "sed/assets/episode.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sed::level
{
using CampaignStateValue = std::variant<bool, std::int64_t, std::string>;
using CampaignState = std::map<std::string, CampaignStateValue>;

struct CampaignSequenceContract
{
    int line{};
    CampaignState entry;
    std::optional<CampaignState> exitA, exitB;
};

enum class CampaignTransitionStatus { matches, mismatch, incomplete };

struct CampaignRequirementFinding
{
    std::string key;
    CampaignStateValue required;
    std::optional<CampaignStateValue> provided;
};

struct CampaignTransition
{
    int from{}, to{};
    bool branchB{};
    CampaignTransitionStatus status{CampaignTransitionStatus::incomplete};
    std::string reason;
    std::size_t requirements{}, matched{}, mismatched{}, unknown{};
    std::vector<CampaignRequirementFinding> unmet;
};

struct CampaignContractReport
{
    std::vector<int> unmodeledSequences;
    std::vector<CampaignTransition> transitions;
    // False means at least one declared contradiction. Null means no known
    // contradiction but missing declarations prevent a complete comparison.
    std::optional<bool> compatible;
};

// Compare explicit postconditions with preconditions on every nonterminal
// episode edge. No branch selection, script execution, initial-state check,
// implicit state preservation, file resolution or runtime claim.
// At most 128 sequences, 64 facts per state, 4096 facts in total; names 1..64
// bytes, strings <=128 bytes, integers exactly representable in binary64.
// Invalid or over-limit input throws invalid_argument/length_error.
[[nodiscard]] CampaignContractReport auditCampaignContracts(const assets::EpisodeFile& episode,
    const std::vector<CampaignSequenceContract>& contracts);
}
