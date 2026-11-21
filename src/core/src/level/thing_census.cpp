#include "sed/level/thing_census.hpp"

#include "sed/level.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace sed::level
{
namespace
{
constexpr std::size_t kMaxRows = 65'536;
constexpr std::size_t kMaxFields = 1'000'000;
constexpr std::size_t kMaxName = 255;
constexpr std::array kDifficultyMasks{ TF_NOEASY, TF_NOMEDIUM, TF_NOHARD };
// Reconstructed sithThing loader selects this mask when the multiplayer submode is clear.
constexpr int kNoSinglePlayer = 0x10000;

struct Fields
{
    bool completeTemplate{};
    std::optional<std::int32_t> flags;
    std::string_view type;
};

std::string_view thingType(const TplValue& value)
{
    constexpr std::string_view types[]{ "free", "camera", "actor", "weapon", "debris", "item",
        "explosion", "cog", "ghost", "corpse", "player", "particle" };
    if ( value.type == ValueType::str || value.type == ValueType::unknown )
    {
        for ( const auto type : types )
        {
            if ( text::iequalsAscii(value.stringValue, type) )
            {
                return type;
            }
        }
    }
    return {};
}

std::optional<std::int32_t> flagBits(const TplValue& value)
{
    if ( value.type == ValueType::flag || value.type == ValueType::integer || value.type == ValueType::ptr )
    {
        return value.intValue;
    }
    if ( (value.type == ValueType::str || value.type == ValueType::unknown)
        && value.stringValue.size() <= kMaxName )
    {
        Value decoded;
        decoded.type = ValueType::flag;
        if ( decoded.parse(value.stringValue) )
        {
            return decoded.intValue;
        }
    }
    return std::nullopt;
}

void applyFields(Fields& fields, const std::vector<TplValue>& values)
{
    // Scalar Thing arguments take effect in source order. An opaque last
    // declaration cannot silently restore the inherited or default value.
    for ( const auto& value : values )
    {
        if ( text::iequalsAscii(value.name, "thingflags") )
        {
            fields.flags = flagBits(value);
        }
        else if ( text::iequalsAscii(value.name, "type") )
        {
            fields.type = thingType(value);
        }
    }
}

bool admitted(const Level& level)
{
    if ( level.things.size() > kMaxRows || level.templates.size() > kMaxRows )
    {
        return false;
    }
    std::size_t remaining = kMaxFields;
    for ( const auto& tpl : level.templates )
    {
        if ( tpl.name.size() > kMaxName || tpl.parent.size() > kMaxName || tpl.values.size() > remaining )
        {
            return false;
        }
        remaining -= tpl.values.size();
    }
    for ( const auto& thing : level.things )
    {
        const auto& name = thing.templateName.empty() ? thing.name : thing.templateName;
        if ( name.size() > kMaxName || thing.values.size() > remaining )
        {
            return false;
        }
        remaining -= thing.values.size();
    }
    return true;
}

class EmbeddedFields
{
public:
    explicit EmbeddedFields(const std::vector<ThingTemplate>& templates)
        : templates_(templates), states_(templates.size()), fields_(templates.size())
    {
        for ( std::size_t i = 0; i < templates.size(); ++i )
        {
            // RED's Level-local lookup takes the first case-insensitive match.
            indices_.try_emplace(text::lowerAscii(templates[i].name), i);
        }
    }

    Fields resolve(const std::string& name)
    {
        auto entry = indices_.find(name);
        Fields base;
        std::vector<std::size_t> path;
        while ( entry != indices_.end() )
        {
            const auto i = entry->second;
            if ( states_[i] == State::done )
            {
                base = fields_[i];
                break;
            }
            if ( states_[i] == State::visiting )
            {
                // A cycle has no base. Cache unknown fields for all its
                // members, independent of which placement led us here.
                const auto cycle = std::find(path.begin(), path.end(), i);
                for ( auto member = cycle; member != path.end(); ++member )
                {
                    states_[*member] = State::done;
                }
                path.erase(cycle, path.end());
                break;
            }
            states_[i] = State::visiting;
            path.push_back(i);
            const auto& parent = templates_[i].parent;
            if ( parent.empty() || text::iequalsAscii(parent, "none") )
            {
                base = { true, 0, "free" };
                break;
            }
            entry = indices_.find(text::lowerAscii(parent));
        }
        // Even when an ancestor is missing, each closer noncyclic declaration
        // can establish its own field without inferring the other field.
        for ( auto it = path.rbegin(); it != path.rend(); ++it )
        {
            applyFields(base, templates_[*it].values);
            fields_[*it] = base;
            states_[*it] = State::done;
        }
        return base;
    }

private:
    enum class State { unseen, visiting, done };
    const std::vector<ThingTemplate>& templates_;
    std::map<std::string, std::size_t> indices_;
    std::vector<State> states_;
    std::vector<Fields> fields_;
};
} // namespace

std::variant<ThingCensus, ThingCensusError> countThingPlacements(const Level& level)
{
    if ( level.kind != ProjectType::JKDF2 && level.kind != ProjectType::MOTS )
    {
        return ThingCensusError::unsupportedTarget;
    }
    if ( !admitted(level) )
    {
        return ThingCensusError::tooLarge;
    }
    ThingCensus result;
    EmbeddedFields templates(level.templates);
    for ( const auto& thing : level.things )
    {
        const auto name = text::lowerAscii(thing.templateName.empty() ? thing.name : thing.templateName);
        auto fields = templates.resolve(name);
        applyFields(fields, thing.values);
        const bool validSector = thing.sector >= 0 && static_cast<std::size_t>(thing.sector) < level.sectors.size();
        auto& group = result.groups[{ thing.sector, thing.layer, name, std::string(fields.type) }];
        for ( auto* counts : { &group, &result.summary } )
        {
            ++counts->placements;
            counts->invalidSector += !validSector;
            counts->unresolvedTemplate += !fields.completeTemplate;
            counts->unknownEligibility += validSector && !fields.flags;
        }
        if ( !validSector )
        {
            continue;
        }
        for ( std::size_t i = 0; i < kDifficultyMasks.size(); ++i )
        {
            if ( fields.flags && (*fields.flags & (kDifficultyMasks[i] | kNoSinglePlayer)) == 0 )
            {
                ++group.singlePlayer[i];
                ++result.summary.singlePlayer[i];
            }
            if ( fields.flags && (*fields.flags & (kDifficultyMasks[i] | TF_NOMULTI)) != 0 )
            {
                continue;
            }
            if ( fields.flags )
            {
                ++group.multiplayer[i];
                ++result.summary.multiplayer[i];
            }
            if ( fields.flags && fields.type == "player" )
            {
                ++result.confirmedPlayerStarts[i];
            }
            else if ( fields.type.empty() || fields.type == "player" )
            {
                ++result.possibleAdditionalPlayerStarts[i];
            }
        }
    }
    return result;
}
} // namespace sed::level
