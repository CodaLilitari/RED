#include "sed/level/campaign_contracts.hpp"

#include <algorithm>
#include <stdexcept>

namespace sed::level
{
namespace
{
void validateState(const CampaignState& state, std::size_t& remaining)
{
    if ( state.size() > 64 || state.size() > remaining )
    {
        throw std::length_error("campaign contracts exceed the state-fact budget");
    }
    remaining -= state.size();
    for ( const auto& [key, value] : state )
    {
        if ( key.empty() || key.size() > 64 || std::any_of(key.begin(), key.end(), [](unsigned char c)
            {
                return c <= 0x20 || c == 0x7f;
            }) )
        {
            throw std::invalid_argument("campaign state keys require 1..64 bytes without ASCII whitespace or controls");
        }
        if ( const auto* text = std::get_if<std::string>(&value); text && text->size() > 128 )
        {
            throw std::length_error("campaign state strings exceed 128 bytes");
        }
        if ( const auto* number = std::get_if<std::int64_t>(&value);
            number && (*number < -9007199254740991LL || *number > 9007199254740991LL) )
        {
            throw std::invalid_argument("campaign state integers must be exactly representable in JSON");
        }
    }
}

CampaignTransition compare(int from, int to, bool branchB,
    const CampaignSequenceContract* source, const CampaignSequenceContract* target)
{
    CampaignTransition row;
    row.from = from;
    row.to = to;
    row.branchB = branchB;
    if ( !target )
    {
        row.reason = !source ? "sourceContractMissing" : "targetContractMissing";
        return row;
    }
    row.requirements = target->entry.size();
    const auto* exit = source ? &(branchB ? source->exitB : source->exitA) : nullptr;
    if ( !exit || !*exit )
    {
        row.reason = source ? "exitContractMissing" : "sourceContractMissing";
        row.unknown = row.requirements;
        for ( const auto& [key, required] : target->entry )
        {
            row.unmet.push_back({key, required, {}});
        }
        return row;
    }
    for ( const auto& [key, required] : target->entry )
    {
        const auto actual = (*exit)->find(key);
        if ( actual == (*exit)->end() )
        {
            ++row.unknown;
            row.unmet.push_back({key, required, {}});
        }
        else if ( actual->second != required )
        {
            ++row.mismatched;
            row.unmet.push_back({key, required, actual->second});
        }
        else
        {
            ++row.matched;
        }
    }
    if ( row.mismatched )
    {
        row.status = CampaignTransitionStatus::mismatch;
        row.reason = "incompatibleGuarantee";
    }
    else if ( row.unknown )
    {
        row.reason = "guaranteeMissing";
    }
    else
    {
        row.status = CampaignTransitionStatus::matches;
    }
    return row;
}
}

CampaignContractReport auditCampaignContracts(const assets::EpisodeFile& episode,
    const std::vector<CampaignSequenceContract>& contracts)
{
    if ( episode.sequences.size() > 128 || contracts.size() > 128 )
    {
        throw std::length_error("campaign contracts admit at most 128 sequences");
    }
    if ( const auto error = assets::episodeGraphValidationError(episode) )
    {
        throw std::invalid_argument(*error);
    }
    std::map<int, const CampaignSequenceContract*> byLine;
    for ( const auto& row : episode.sequences )
    {
        byLine.emplace(row.lineNumber, nullptr);
    }
    std::size_t remaining = 4096;
    for ( const auto& contract : contracts )
    {
        const auto found = byLine.find(contract.line);
        if ( found == byLine.end() || found->second )
        {
            throw std::invalid_argument("campaign contracts must name distinct existing episode lines");
        }
        validateState(contract.entry, remaining);
        if ( contract.exitA )
        {
            validateState(*contract.exitA, remaining);
        }
        if ( contract.exitB )
        {
            validateState(*contract.exitB, remaining);
        }
        found->second = &contract;
    }
    CampaignContractReport report;
    bool mismatch = false;
    bool incomplete = false;
    for ( const auto& row : episode.sequences )
    {
        if ( !byLine.at(row.lineNumber) )
        {
            report.unmodeledSequences.push_back(row.lineNumber);
            incomplete = true;
        }
        for ( const bool branchB : {false, true} )
        {
            const auto target = branchB ? row.gotoB : row.gotoA;
            if ( target == -1 )
            {
                continue;
            }
            auto result = compare(row.lineNumber, target, branchB, byLine.at(row.lineNumber), byLine.at(target));
            mismatch |= result.status == CampaignTransitionStatus::mismatch;
            incomplete |= result.status == CampaignTransitionStatus::incomplete;
            report.transitions.push_back(std::move(result));
        }
    }
    if ( mismatch || !incomplete )
    {
        report.compatible = !mismatch;
    }
    return report;
}
}
