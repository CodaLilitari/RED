#include "jo_ja_stock_pickups.hpp"

#include "sed/assets/jo_ja_hybrid_geometry.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/raven_bsp.hpp"
#include "sed/assets/tpl.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sed::assets
{
void placeJoJaStockPickups(const RavenBsp& bsp, const JoJaImportOptions& options,
                           const JoJaHybridGeometry* hybrid, const TVector& low, const TVector& high,
                           Level& level, JoJaImportResult& result)
{
    if ( options.sourceProfile == JoJaSourceProfile::Legacy )
        throw std::runtime_error("stockPickups requires an explicit jo-mp or ja-mp source profile");
    static constexpr const char* jkRows =
#include "master_pickup_rows.inc"
    ;
    static constexpr const char* motsRows =
#include "mots_pickup_rows.inc"
    ;
    std::istringstream rows(options.target == ProjectType::MOTS ? motsRows : jkRows);
    const auto catalog = loadTemplates(rows);
    // Conservative common MP counterparts. Never silently substitute a different weapon family.
    static const std::map<std::string, std::string> equivalents{
        {"weapon_blaster", "strifle"}, {"weapon_bowcaster", "crossbow"},
        {"weapon_thermal", "detonator"}, {"ammo_blaster", "energycell"},
        {"item_medpak_instant", "healthpack"}, {"item_medpac", "bactatank"},
        {"item_shield_sm_instant", "shieldrecharge"}
    };
    std::set<std::string> emitted;
    std::size_t candidates = 0;
    for ( std::size_t index = 0; index < bsp.entities.size(); ++index )
    {
        const auto& fields = bsp.entities[index].fields;
        const auto find = [&](const char* key) -> std::string {
            for ( const auto& [name, value] : fields )
                if ( name == key ) return value;
            return {};
        };
        const auto kind = find("classname");
        if ( !kind.starts_with("weapon_") && !kind.starts_with("item_") && !kind.starts_with("ammo_") &&
             !kind.starts_with("team_") )
            continue;
        if ( ++candidates > 512 )
            throw std::runtime_error("stockPickups exceeds the 512 pickup candidate budget");
        const auto note = [&](const std::string& message) {
            result.warnings.push_back("Pickup entity " + std::to_string(index) + " " + kind + ": " + message);
        };
        const auto mapping = equivalents.find(kind);
        if ( mapping == equivalents.end() )
        {
            note("review; no supported stock counterpart");
            continue;
        }
        // Additional keys can gate FFA spawn, make a trigger-only item or change its timing/count.
        if ( fields.size() != 2 )
        {
            note("review; non-default entity fields, not placed");
            continue;
        }
        std::istringstream origin(find("origin"));
        origin.imbue(std::locale::classic());
        TVector p;
        std::string tail;
        if ( !(origin >> p.x >> p.y >> p.z) || origin >> tail )
        {
            note("review; invalid origin");
            continue;
        }
        p.x *= options.scale;
        p.y *= options.scale;
        p.z *= options.scale;
        int sector = 0;
        if ( !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) )
        {
            note("review; non-finite origin");
            continue;
        }
        if ( hybrid )
        {
            const auto cell = std::find_if(hybrid->cells.begin(), hybrid->cells.end(),
                [&](const auto& candidate) { return joJaHybridContains(candidate, p, 1e-9); });
            if ( cell == hybrid->cells.end() )
            {
                note("review; origin outside generated cells");
                continue;
            }
            sector = static_cast<int>(cell - hybrid->cells.begin());
        }
        else if ( p.x < low.x - .5 || p.y < low.y - .5 || p.z < low.z - .5 ||
                  p.x > high.x + .5 || p.y > high.y + .5 || p.z > high.z + .5 )
        {
            note("review; origin outside generated room");
            continue;
        }
        std::vector<const ThingTemplate*> chain;
        std::string name = mapping->second;
        while ( name != "none" && !emitted.contains(name) )
        {
            const auto* row = catalog.find(name);
            if ( !row || chain.size() > catalog.size() )
                throw std::runtime_error("Unresolved stock pickup template: " + name);
            chain.push_back(row);
            name = row->parent;
        }
        for ( auto it = chain.rbegin(); it != chain.rend(); ++it )
        {
            level.templates.push_back(**it);
            emitted.insert((*it)->name);
        }
        auto& thing = level.newThing();
        thing.templateName = mapping->second;
        thing.name = "pickup" + std::to_string(index);
        thing.sector = sector;
        thing.x = p.x;
        thing.y = p.y;
        thing.z = p.z;
        note("placed stock counterpart " + thing.templateName +
             "; verify floor contact, quantity, respawn and gameplay in target");
    }
}
} // namespace sed::assets
