#include "sed/level/gameplay_assistants.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/assets/cog_schema.hpp"
#include "sed/level/cog_bindings.hpp"
#include "sed/level/thing.hpp"
#include "sed/level/thing_frames.hpp"
#include "sed/text.hpp"
#include "sed/values.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace sed::level
{
namespace
{

constexpr std::string_view kGoalsScriptText = R"RED(# red_goals.cog
#
# Objectives, written by RED's Objectives assistant.
# Goal i is complete when the player enters reach<i>, uses use<i>, or kills
# kill<i>. The goal text is GOAL_<goalbase + i> in cogstrings.uni.
# With an exit surface, crossing it ends the level once every goal is done;
# without one, the level ends with the last goal when endwhendone is 1.

symbols
message startup
message entered
message activated
message killed
message crossed

int goalbase=1000
int goalcount=1
int endwhendone=1

sector reach0 linkid=0 mask=0x400
sector reach1 linkid=1 mask=0x400
sector reach2 linkid=2 mask=0x400
sector reach3 linkid=3 mask=0x400
sector reach4 linkid=4 mask=0x400
sector reach5 linkid=5 mask=0x400
sector reach6 linkid=6 mask=0x400
sector reach7 linkid=7 mask=0x400
thing use0 linkid=10 mask=0x400
thing use1 linkid=11 mask=0x400
thing use2 linkid=12 mask=0x400
thing use3 linkid=13 mask=0x400
thing use4 linkid=14 mask=0x400
thing use5 linkid=15 mask=0x400
thing use6 linkid=16 mask=0x400
thing use7 linkid=17 mask=0x400
thing kill0 linkid=20 mask=0xfff
thing kill1 linkid=21 mask=0xfff
thing kill2 linkid=22 mask=0xfff
thing kill3 linkid=23 mask=0xfff
thing kill4 linkid=24 mask=0xfff
thing kill5 linkid=25 mask=0xfff
thing kill6 linkid=26 mask=0xfff
thing kill7 linkid=27 mask=0xfff
surface exit linkid=100 mask=0x400

sound goalsound=accomplish1.wav local
int player local
int goal local
int i local
int completed=0 local
int done0=0 local
int done1=0 local
int done2=0 local
int done3=0 local
int done4=0 local
int done5=0 local
int done6=0 local
int done7=0 local
end

code
startup:
   player = GetLocalPlayerThing();
   SetInv(player, 99, goalbase);
   for (i = 0; i < goalcount; i = i + 1) SetGoalFlags(player, i, 1);
   return;

entered:
   goal = GetSenderId();
   call complete;
   return;

activated:
   goal = GetSenderId() - 10;
   call complete;
   return;

killed:
   goal = GetSenderId() - 20;
   call complete;
   return;

crossed:
   if (GetSenderId() != 100) return;
   if (completed < goalcount) {
      Print("Objectives incomplete");
      return;
   }
   jkEndLevel(1);
   return;

complete:
   if (goal < 0) return;
   if (goal >= goalcount) return;
   if (done0[goal]) return;
   done0[goal] = 1;
   completed = completed + 1;
   SetGoalFlags(player, goal, 2);
   PlaySoundThing(goalsound, player, 1.0, -1, -1, 0);
   if (completed < goalcount) return;
   if (endwhendone == 0) return;
   if (exit >= 0) return;
   jkEndLevel(1);
   return;
end
)RED";
constexpr std::string_view kCutsceneScriptText = R"RED(# red_cutscene.cog
#
# A camera move, written by RED's Cutscene assistant. When the player enters
# the trigger sector (or at startup when onstartup is 1), the player is held,
# the view follows the camera Thing along its path frames to lastframe, holds
# for 'hold' seconds, and control returns to the player.

symbols
message startup
message entered
message arrived
message timer

sector trigger linkid=1 mask=0x400
thing camera linkid=2
int lastframe=1
flex speed=2.0
flex hold=2.0
int onstartup=0
int once=1

int player local
int playing=0 local
int played=0 local
int previouscamera=0 local
end

code
startup:
   player = GetLocalPlayerThing();
   if (onstartup) call play;
   return;

entered:
   if (GetSenderId() != 1) return;
   if (GetSourceRef() != player) return;
   call play;
   return;

play:
   if (camera < 0) return;
   if (playing) return;
   if (played) if (once) return;
   playing = 1;
   played = 1;
   previouscamera = GetCurrentCamera();
   SetActorFlags(player, 0xa00000);
   StopThing(player);
   if (GetCurFrame(camera) != 0) JumpToFrame(camera, 0, GetThingSector(camera));
   SetCurrentCamera(0);
   SetCameraFocus(0, camera);
   MoveToFrame(camera, lastframe, speed);
   return;

arrived:
   if (GetSenderId() != 2) return;
   if (GetCurFrame(camera) != lastframe) return;
   SetTimer(hold);
   return;

timer:
   ClearActorFlags(player, 0xa00000);
   SetCameraFocus(0, player);
   SetCurrentCamera(previouscamera);
   playing = 0;
   return;
end
)RED";

std::string number(const double value)
{
    std::ostringstream out;
    out << std::setprecision(6) << value;
    return out.str();
}

WorkbenchFailure failure(std::string code, std::string message)
{
    return WorkbenchFailure{ std::move(code), std::move(message) };
}

AssistantResult refused(std::string code, std::string message)
{
    AssistantResult result;
    result.failure = failure(std::move(code), std::move(message));
    return result;
}

bool validThing(const Level& level, const std::int32_t index)
{
    return index >= 0 && static_cast<std::size_t>(index) < level.things.size();
}

bool validSector(const Level& level, const std::int32_t index)
{
    return index >= 0 && static_cast<std::size_t>(index) < level.sectors.size();
}

const Surface* surfaceAt(const Level& level, const SurfaceRef ref)
{
    if (!validSector(level, ref.sector) || ref.surface < 0) return nullptr;
    const auto& surfaces = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces;
    return static_cast<std::size_t>(ref.surface) < surfaces.size() ? &surfaces[static_cast<std::size_t>(ref.surface)] : nullptr;
}

std::string thingLabel(const Level& level, const std::int32_t index)
{
    const Thing& thing = level.things[static_cast<std::size_t>(index)];
    return "Thing " + std::to_string(index) + " (" + (thing.name.empty() ? thing.templateName : thing.name) + ")";
}

/** The symbols an assistant fills, checked before anything changes. */
std::optional<WorkbenchFailure> requireSymbols(
    const std::vector<CogValue>& symbols, const std::string_view script, const std::vector<std::string>& names)
{
    for (const std::string& name : names)
    {
        const bool found = std::any_of(symbols.begin(), symbols.end(),
            [&](const CogValue& value) { return sed::text::iequalsAscii(value.name, name); });
        if (!found)
            return failure("assistantScriptMismatch",
                std::string(script) + " has no symbol '" + name + "', so it isn't the script this assistant fills in.");
    }
    return std::nullopt;
}

void setSymbol(std::vector<CogValue>& values, const std::string_view name, const std::string& text)
{
    for (CogValue& value : values)
    {
        if (sed::text::iequalsAscii(value.name, name))
        {
            (void)value.parse(text);
            return;
        }
    }
}

std::optional<WorkbenchFailure> checkSetting(const char* what, const double value, const double low, const double high)
{
    if (!std::isfinite(value) || value < low || value > high)
        return failure("assistantInvalidSetting",
            std::string(what) + " must be from " + number(low) + " to " + number(high) + ".");
    return std::nullopt;
}

std::optional<WorkbenchFailure> checkOffset(const char* what, const std::array<double, 3>& offset)
{
    const double length = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
    if (!std::isfinite(length) || length < 0.001 || length > 200.0)
        return failure("assistantInvalidSetting", std::string(what) + " must move between 0.001 and 200 JKU.");
    return std::nullopt;
}

/** A Thing that will move on two frames: its pose, and its pose moved by `offset`. */
std::optional<WorkbenchFailure> checkMover(
    const Level& level, const assets::Templates* templates, const std::int32_t index, const char* role)
{
    if (!validThing(level, index))
        return failure("assistantTargetOutOfRange", std::string(role) + " " + std::to_string(index) + " isn't a Thing in this level.");
    const Thing& thing = level.things[static_cast<std::size_t>(index)];
    if (!thingMovesOnPath(level, templates, thing))
        return failure("assistantNotPathMover",
            thingLabel(level, index) + " doesn't move on a path: its template needs move=path.");
    if (thingCountFrames(thing) > 0)
        return failure("assistantAlreadyMoves",
            thingLabel(level, index) + " already has path frames. Clear them first, or edit them in the frame editor.");
    return std::nullopt;
}

struct PreparedMover
{
    PreparedThingFrames frames;
    std::int32_t index{};
    std::int32_t flags{};
};

std::optional<WorkbenchFailure> prepareMover(const Level& level, const assets::Templates* templates, const std::int32_t index,
    const std::array<double, 3>& offset, std::vector<PreparedMover>& out)
{
    const Thing& thing = level.things[static_cast<std::size_t>(index)];
    const FrameValue closed{ thing.x, thing.y, thing.z, thing.pch, thing.yaw, thing.rol };
    FrameValue opened = closed;
    opened.x += offset[0];
    opened.y += offset[1];
    opened.z += offset[2];
    const std::vector<FrameValue> frames{ closed, opened };
    PreparedMover mover{ prepareSetThingFrames(level, templates, static_cast<std::size_t>(index), frames), index,
        thingFlags(level, templates, thing) };
    if (!mover.frames.ok()) return mover.frames.failure;
    out.push_back(std::move(mover));
    return std::nullopt;
}

/** Writes the frames, and makes each mover part of the world (0x8) and COG-linked (0x400), as stock doors are. */
void applyMovers(Level& level, const std::vector<PreparedMover>& movers)
{
    for (const PreparedMover& mover : movers)
    {
        (void)mover.frames.apply(level);
        if ((mover.flags & 0x408) == 0x408) continue;
        Thing& thing = level.things[static_cast<std::size_t>(mover.index)];
        std::ostringstream hex;
        hex << "0x" << std::hex << static_cast<std::uint32_t>(mover.flags | 0x408);
        TplValue flags = deserializeTplValue("thingflags", hex.str());
        auto it = std::find_if(thing.values.begin(), thing.values.end(),
            [](const TplValue& value) { return sed::text::iequalsAscii(value.name, "thingflags"); });
        if (it != thing.values.end()) *it = flags;
        else thing.values.insert(thing.values.begin(), flags);
    }
}

std::optional<WorkbenchFailure> checkPieces(const Level& level, const assets::Templates* templates,
    const std::vector<std::int32_t>& pieces)
{
    if (pieces.empty()) return failure("assistantNothingSelected", "Select the door's pieces: one to four Things.");
    if (pieces.size() > kMaxAssistantDoorPieces)
        return failure("assistantTooMany", "A door has at most four pieces.");
    if (std::set<std::int32_t>(pieces.begin(), pieces.end()).size() != pieces.size())
        return failure("assistantDuplicateTarget", "A piece is listed twice.");
    for (const std::int32_t piece : pieces)
    {
        if (auto problem = checkMover(level, templates, piece, "Door piece")) return problem;
    }
    return std::nullopt;
}

void doorSectorWarnings(const Level& level, const std::vector<std::int32_t>& pieces, std::vector<std::string>& warnings)
{
    const std::int32_t sector = level.things[static_cast<std::size_t>(pieces.front())].sector;
    if (!validSector(level, sector))
    {
        warnings.push_back("The first piece isn't in a sector, so the script has no door sector to open and close.");
        return;
    }
    for (const std::int32_t piece : pieces)
    {
        if (level.things[static_cast<std::size_t>(piece)].sector != sector)
            warnings.push_back(thingLabel(level, piece) + " is in another sector. The script opens and closes only the adjoins of sector "
                + std::to_string(sector) + ", where the first piece is.");
    }
    std::size_t others = 0;
    for (std::size_t i = 0; i < level.things.size(); ++i)
    {
        if (level.things[i].sector == sector
            && std::find(pieces.begin(), pieces.end(), static_cast<std::int32_t>(i)) == pieces.end())
            ++others;
    }
    if (others > 0)
        warnings.push_back("Sector " + std::to_string(sector) + " also holds " + std::to_string(others)
            + " other Thing(s). The script hides that sector's adjoins while the door is shut, so it should be a small sector that holds only the door.");
}

std::optional<WorkbenchFailure> checkSurfaces(const Level& level, const std::vector<SurfaceRef>& surfaces, const char* role,
    const bool mustAdjoin)
{
    for (const SurfaceRef ref : surfaces)
    {
        const Surface* surface = surfaceAt(level, ref);
        if (!surface)
            return failure("assistantTargetOutOfRange", std::string(role) + " (sector " + std::to_string(ref.sector) + ", surface "
                + std::to_string(ref.surface) + ") isn't a surface in this level.");
        if (mustAdjoin && !surface->adjoin)
            return failure("assistantNotAdjoin", std::string(role) + " (sector " + std::to_string(ref.sector) + ", surface "
                + std::to_string(ref.surface) + ") isn't an adjoin, so nobody can cross it.");
    }
    for (std::size_t i = 0; i < surfaces.size(); ++i)
    {
        for (std::size_t j = i + 1; j < surfaces.size(); ++j)
        {
            if (surfaces[i] == surfaces[j]) return failure("assistantDuplicateTarget", std::string(role) + " is listed twice.");
        }
    }
    return std::nullopt;
}

std::string surfaceNumber(const Level& level, const SurfaceRef ref)
{
    return std::to_string(level.getGlobalSurfaceNumber(ref.sector, ref.surface));
}

AssistantResult placeCog(Level& level, const std::string_view script, std::vector<CogValue> values, AssistantResult result)
{
    Cog& cog = level.newCog();
    cog.name = std::string(script);
    cog.values = std::move(values);
    result.cog = cog.num;
    return result;
}

std::vector<CogValue> generatedSymbols(const std::string_view script)
{
    return assets::loadCogSymbolsFromTextResult(assistantScriptText(script), false).symbols;
}

} // namespace

std::string_view assistantKindName(const AssistantKind kind) noexcept
{
    switch (kind)
    {
    case AssistantKind::door: return "door";
    case AssistantKind::elevator: return "elevator";
    case AssistantKind::switchDoor: return "switch";
    case AssistantKind::objectives: return "objectives";
    case AssistantKind::cutscene: return "cutscene";
    case AssistantKind::pursuit: return "pursuit";
    }
    return "door";
}

std::optional<AssistantKind> parseAssistantKind(const std::string_view name) noexcept
{
    for (const AssistantKind kind : { AssistantKind::door, AssistantKind::elevator, AssistantKind::switchDoor,
             AssistantKind::objectives, AssistantKind::cutscene, AssistantKind::pursuit })
    {
        if (sed::text::iequalsAscii(name, assistantKindName(kind))) return kind;
    }
    return std::nullopt;
}

std::string_view assistantScriptName(const AssistantKind kind) noexcept
{
    switch (kind)
    {
    case AssistantKind::door: return kDoorAssistantScript;
    case AssistantKind::elevator: return kElevatorAssistantScript;
    case AssistantKind::switchDoor: return kSwitchAssistantScript;
    case AssistantKind::objectives: return kObjectivesAssistantScript;
    case AssistantKind::cutscene: return kCutsceneAssistantScript;
    case AssistantKind::pursuit: return kPursuitAssistantScript;
    }
    return kDoorAssistantScript;
}

std::string_view assistantScriptText(const std::string_view scriptName) noexcept
{
    if (sed::text::iequalsAscii(scriptName, kObjectivesAssistantScript)) return kGoalsScriptText;
    if (sed::text::iequalsAscii(scriptName, kCutsceneAssistantScript)) return kCutsceneScriptText;
    return {};
}

std::string_view goalTriggerName(const GoalTrigger trigger) noexcept
{
    switch (trigger)
    {
    case GoalTrigger::reach: return "reach";
    case GoalTrigger::use: return "use";
    case GoalTrigger::kill: return "kill";
    }
    return "reach";
}

std::optional<GoalTrigger> parseGoalTrigger(const std::string_view name) noexcept
{
    for (const GoalTrigger trigger : { GoalTrigger::reach, GoalTrigger::use, GoalTrigger::kill })
    {
        if (sed::text::iequalsAscii(name, goalTriggerName(trigger))) return trigger;
    }
    return std::nullopt;
}

AssistantResult levelDoorAssistant(Level& level, const assets::Templates* templates, const DoorAssistantRequest& request,
    const std::vector<CogValue>& scriptSymbols)
{
    if (auto problem = requireSymbols(scriptSymbols, kDoorAssistantScript,
            { "door0", "door1", "door2", "door3", "movespeed", "sleeptime", "lightvalue" }))
        return AssistantResult{ problem };
    if (auto problem = checkPieces(level, templates, request.pieces)) return AssistantResult{ problem };
    if (auto problem = checkOffset("The opening", request.openOffset)) return AssistantResult{ problem };
    if (auto problem = checkSetting("Speed", request.speed, 0.01, 100.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The wait", request.wait, 0.0, 600.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The light", request.light, 0.0, 2.0)) return AssistantResult{ problem };
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    std::vector<PreparedMover> movers;
    for (const std::int32_t piece : request.pieces)
    {
        if (auto problem = prepareMover(level, templates, piece, request.openOffset, movers)) return AssistantResult{ problem };
    }
    AssistantResult result;
    doorSectorWarnings(level, request.pieces, result.warnings);
    std::vector<CogValue> values = scriptSymbols;
    for (std::size_t i = 0; i < kMaxAssistantDoorPieces; ++i)
        setSymbol(values, "door" + std::to_string(i), i < request.pieces.size() ? std::to_string(request.pieces[i]) : "-1");
    setSymbol(values, "movespeed", number(request.speed));
    setSymbol(values, "sleeptime", number(request.wait));
    setSymbol(values, "lightvalue", number(request.light));
    applyMovers(level, movers);
    return placeCog(level, kDoorAssistantScript, std::move(values), std::move(result));
}

AssistantResult levelElevatorAssistant(Level& level, const assets::Templates* templates,
    const ElevatorAssistantRequest& request, const std::vector<CogValue>& scriptSymbols)
{
    if (auto problem = requireSymbols(scriptSymbols, kElevatorAssistantScript,
            { "lower_adjoin0", "lower_adjoin1", "lower_adjoin2", "button", "elevator", "start_wait", "sleeptime", "speed" }))
        return AssistantResult{ problem };
    if (auto problem = checkMover(level, templates, request.elevator, "The elevator")) return AssistantResult{ problem };
    if (!request.button && request.callAdjoins.empty())
        return refused("assistantNothingSelected", "Give the lift a button or at least one adjoin that calls it.");
    if (request.callAdjoins.size() > kMaxAssistantCallAdjoins)
        return refused("assistantTooMany", "The script takes at most three adjoins that call the lift.");
    if (request.button)
    {
        if (auto problem = checkSurfaces(level, { *request.button }, "The button", false)) return AssistantResult{ problem };
    }
    if (auto problem = checkSurfaces(level, request.callAdjoins, "A call adjoin", true)) return AssistantResult{ problem };
    if (auto problem = checkOffset("The lift's travel", request.travel)) return AssistantResult{ problem };
    if (auto problem = checkSetting("Speed", request.speed, 0.01, 100.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The wait at the top", request.wait, 0.0, 600.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The start wait", request.startWait, 0.0, 60.0)) return AssistantResult{ problem };
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    std::vector<PreparedMover> movers;
    if (auto problem = prepareMover(level, templates, request.elevator, request.travel, movers)) return AssistantResult{ problem };
    AssistantResult result;
    if (request.button)
        result.warnings.push_back("The button shows cel 1 while the lift is away, so its material needs two cels.");
    std::vector<CogValue> values = scriptSymbols;
    for (std::size_t i = 0; i < kMaxAssistantCallAdjoins; ++i)
        setSymbol(values, "lower_adjoin" + std::to_string(i),
            i < request.callAdjoins.size() ? surfaceNumber(level, request.callAdjoins[i]) : "-1");
    setSymbol(values, "button", request.button ? surfaceNumber(level, *request.button) : "-1");
    setSymbol(values, "elevator", std::to_string(request.elevator));
    setSymbol(values, "start_wait", number(request.startWait));
    setSymbol(values, "sleeptime", number(request.wait));
    setSymbol(values, "speed", number(request.speed));
    applyMovers(level, movers);
    return placeCog(level, kElevatorAssistantScript, std::move(values), std::move(result));
}

AssistantResult levelSwitchAssistant(Level& level, const assets::Templates* templates, const SwitchAssistantRequest& request,
    const std::vector<CogValue>& scriptSymbols)
{
    if (auto problem = requireSymbols(scriptSymbols, kSwitchAssistantScript,
            { "door0", "door1", "door2", "door3", "switch0", "switch1", "movespeed", "sleeptime", "lightvalue" }))
        return AssistantResult{ problem };
    if (auto problem = checkPieces(level, templates, request.pieces)) return AssistantResult{ problem };
    if (request.switches.empty()) return refused("assistantNothingSelected", "Select the switch: one or two surfaces.");
    if (request.switches.size() > kMaxAssistantSwitches) return refused("assistantTooMany", "The script takes at most two switches.");
    if (auto problem = checkSurfaces(level, request.switches, "A switch", false)) return AssistantResult{ problem };
    if (auto problem = checkOffset("The opening", request.openOffset)) return AssistantResult{ problem };
    if (auto problem = checkSetting("Speed", request.speed, 0.01, 100.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The wait", request.wait, 0.0, 600.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The light", request.light, 0.0, 2.0)) return AssistantResult{ problem };
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    std::vector<PreparedMover> movers;
    for (const std::int32_t piece : request.pieces)
    {
        if (auto problem = prepareMover(level, templates, piece, request.openOffset, movers)) return AssistantResult{ problem };
    }
    AssistantResult result;
    doorSectorWarnings(level, request.pieces, result.warnings);
    result.warnings.push_back("Each switch shows cel 1 while the door is open, so its material needs two cels.");
    std::vector<CogValue> values = scriptSymbols;
    for (std::size_t i = 0; i < kMaxAssistantDoorPieces; ++i)
        setSymbol(values, "door" + std::to_string(i), i < request.pieces.size() ? std::to_string(request.pieces[i]) : "-1");
    for (std::size_t i = 0; i < kMaxAssistantSwitches; ++i)
        setSymbol(values, "switch" + std::to_string(i), i < request.switches.size() ? surfaceNumber(level, request.switches[i]) : "-1");
    setSymbol(values, "movespeed", number(request.speed));
    setSymbol(values, "sleeptime", number(request.wait));
    setSymbol(values, "lightvalue", number(request.light));
    applyMovers(level, movers);
    return placeCog(level, kSwitchAssistantScript, std::move(values), std::move(result));
}

AssistantResult levelObjectivesAssistant(Level& level, const ObjectivesAssistantRequest& request)
{
    if (request.goals.empty()) return refused("assistantNothingSelected", "Give the level at least one goal.");
    if (request.goals.size() > kMaxAssistantGoals) return refused("assistantTooMany", "The objectives script takes at most eight goals.");
    if (request.goalBase < 0 || request.goalBase > 99000)
        return refused("assistantInvalidSetting", "The goal base must be from 0 to 99000.");
    const bool placed = std::any_of(level.cogs.begin(), level.cogs.end(),
        [](const Cog& cog) { return sed::text::iequalsAscii(cog.name, kObjectivesAssistantScript); });
    if (placed)
        return refused("assistantAlreadyPlaced", "The level already has red_goals.cog. Edit that COG's values, or delete it first.");
    std::set<std::pair<int, std::int32_t>> seen;
    for (const AssistantGoal& goal : request.goals)
    {
        const bool valid = goal.trigger == GoalTrigger::reach ? validSector(level, goal.target) : validThing(level, goal.target);
        if (!valid)
            return refused("assistantTargetOutOfRange", std::string("A ") + std::string(goalTriggerName(goal.trigger)) + " goal's target "
                + std::to_string(goal.target) + (goal.trigger == GoalTrigger::reach ? " isn't a sector." : " isn't a Thing."));
        if (!seen.insert({ static_cast<int>(goal.trigger), goal.target }).second)
            return refused("assistantDuplicateTarget", "Two goals have the same trigger and target.");
    }
    if (request.exit)
    {
        if (auto problem = checkSurfaces(level, { *request.exit }, "The exit", true)) return AssistantResult{ problem };
    }
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    AssistantResult result;
    const bool hasPlayer = std::any_of(level.things.begin(), level.things.end(), [](const Thing& thing) {
        std::string templ = thing.templateName;
        std::transform(templ.begin(), templ.end(), templ.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return templ.find("player") != std::string::npos;
    });
    if (!hasPlayer) result.warnings.push_back("The level has no player start, and objectives are a single-player feature.");
    result.warnings.push_back("Write each goal's text as GOAL_" + std::to_string(request.goalBase) + " onward in cogstrings.uni (Episode > Goals).");
    std::vector<CogValue> values = generatedSymbols(kObjectivesAssistantScript);
    setSymbol(values, "goalbase", std::to_string(request.goalBase));
    setSymbol(values, "goalcount", std::to_string(request.goals.size()));
    setSymbol(values, "endwhendone", request.endWhenDone ? "1" : "0");
    for (std::size_t i = 0; i < kMaxAssistantGoals; ++i)
    {
        for (const GoalTrigger trigger : { GoalTrigger::reach, GoalTrigger::use, GoalTrigger::kill })
        {
            const bool here = i < request.goals.size() && request.goals[i].trigger == trigger;
            setSymbol(values, std::string(goalTriggerName(trigger)) + std::to_string(i), here ? std::to_string(request.goals[i].target) : "-1");
        }
    }
    setSymbol(values, "exit", request.exit ? surfaceNumber(level, *request.exit) : "-1");
    return placeCog(level, kObjectivesAssistantScript, std::move(values), std::move(result));
}

AssistantResult levelCutsceneAssistant(Level& level, const assets::Templates* templates, const CutsceneAssistantRequest& request)
{
    if (!validThing(level, request.camera))
        return refused("assistantTargetOutOfRange", "The camera " + std::to_string(request.camera) + " isn't a Thing in this level.");
    const Thing& camera = level.things[static_cast<std::size_t>(request.camera)];
    if (!thingMovesOnPath(level, templates, camera))
        return refused("assistantNotPathMover", thingLabel(level, request.camera) + " doesn't move on a path: its template needs move=path.");
    const std::size_t frames = thingCountFrames(camera);
    if (frames < 2)
        return refused("assistantNoCameraPath",
            thingLabel(level, request.camera) + " needs at least two path frames for the camera to travel; build them with Thing > Build Thing Path.");
    if (request.trigger && !validSector(level, *request.trigger))
        return refused("assistantTargetOutOfRange", "The trigger " + std::to_string(*request.trigger) + " isn't a sector.");
    if (auto problem = checkSetting("Speed", request.speed, 0.01, 100.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The hold", request.hold, 0.0, 600.0)) return AssistantResult{ problem };
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    AssistantResult result;
    std::vector<CogValue> values = generatedSymbols(kCutsceneAssistantScript);
    setSymbol(values, "trigger", request.trigger ? std::to_string(*request.trigger) : "-1");
    setSymbol(values, "camera", std::to_string(request.camera));
    setSymbol(values, "lastframe", std::to_string(frames - 1));
    setSymbol(values, "speed", number(request.speed));
    setSymbol(values, "hold", number(request.hold));
    setSymbol(values, "onstartup", request.trigger ? "0" : "1");
    setSymbol(values, "once", request.once ? "1" : "0");
    return placeCog(level, kCutsceneAssistantScript, std::move(values), std::move(result));
}

AssistantResult levelPursuitAssistant(Level& level, const PursuitAssistantRequest& request, const std::vector<CogValue>& scriptSymbols)
{
    if (auto problem = requireSymbols(scriptSymbols, kPursuitAssistantScript,
            { "bike", "wakedistance", "movespeed", "ramdamage", "fireinterval", "wakesector", "exit0", "exit1", "exit2", "exit3",
                "survivalchance" }))
        return AssistantResult{ problem };
    if (!validThing(level, request.enemy))
        return refused("assistantTargetOutOfRange", "The enemy " + std::to_string(request.enemy) + " isn't a Thing in this level.");
    if (request.route.size() > kMaxAssistantRoute) return refused("assistantTooMany", "The route has at most four markers.");
    std::set<std::int32_t> seen{ request.enemy };
    for (const std::int32_t marker : request.route)
    {
        if (!validThing(level, marker))
            return refused("assistantTargetOutOfRange", "Route marker " + std::to_string(marker) + " isn't a Thing in this level.");
        if (!seen.insert(marker).second)
            return refused("assistantDuplicateTarget", "A route marker is listed twice, or is the enemy itself.");
    }
    if (request.wakeSector && !validSector(level, *request.wakeSector))
        return refused("assistantTargetOutOfRange", "The wake sector " + std::to_string(*request.wakeSector) + " isn't a sector.");
    if (auto problem = checkSetting("The wake distance", request.wakeDistance, 0.1, 100.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("Speed", request.moveSpeed, 0.01, 20.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("Ram damage", request.ramDamage, 0.0, 1000.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The fire interval", request.fireInterval, 0.05, 60.0)) return AssistantResult{ problem };
    if (auto problem = checkSetting("The survival chance", request.survivalChance, 0.0, 1.0)) return AssistantResult{ problem };
    const std::string enemy = std::to_string(request.enemy);
    const bool placed = std::any_of(level.cogs.begin(), level.cogs.end(), [&](const Cog& cog) {
        return sed::text::iequalsAscii(cog.name, kPursuitAssistantScript) && !cog.values.empty()
            && cog.values.front().asJedString() == enemy;
    });
    if (placed) return refused("assistantAlreadyPlaced", thingLabel(level, request.enemy) + " already has a pursuit controller; each enemy needs its own.");
    if (!level.canAllocateIds(1)) return refused("assistantIdSpaceExhausted", "The level has no entity IDs left for a COG.");
    AssistantResult result;
    if (!sed::text::iequalsAscii(level.things[static_cast<std::size_t>(request.enemy)].templateName, "kcswoop"))
        result.warnings.push_back(thingLabel(level, request.enemy) + " isn't a kcswoop Thing; the controller plays the kcswoop rider's keys and spawns its parts, so use that template or one like it.");
    for (const std::int32_t marker : request.route)
    {
        if (!sed::text::iequalsAscii(level.things[static_cast<std::size_t>(marker)].templateName, "+kcsway"))
            result.warnings.push_back(thingLabel(level, marker) + " isn't a +kcsway marker; a marker should be an invisible ghost on walkable ground, clear of walls.");
    }
    result.warnings.push_back("The runtime doesn't simulate AI, so the chase itself needs a test in the game.");
    std::vector<CogValue> values = scriptSymbols;
    setSymbol(values, "bike", enemy);
    setSymbol(values, "wakedistance", number(request.wakeDistance));
    setSymbol(values, "movespeed", number(request.moveSpeed));
    setSymbol(values, "ramdamage", number(request.ramDamage));
    setSymbol(values, "fireinterval", number(request.fireInterval));
    setSymbol(values, "wakesector", request.wakeSector ? std::to_string(*request.wakeSector) : "-1");
    for (std::size_t i = 0; i < kMaxAssistantRoute; ++i)
        setSymbol(values, "exit" + std::to_string(i), i < request.route.size() ? std::to_string(request.route[i]) : "-1");
    setSymbol(values, "survivalchance", number(request.survivalChance));
    return placeCog(level, kPursuitAssistantScript, std::move(values), std::move(result));
}

std::vector<assets::CogRuntimeBinding> placedCogRuntimeBindings(
    const Level& level, const std::int32_t cogIndex, const assets::CogSchema& schema)
{
    std::vector<assets::CogRuntimeBinding> bindings;
    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size()) return bindings;
    const CogBindingDescription description = describeBindings(level, cogIndex, &schema);
    std::vector<std::int32_t> surfaceBase(level.sectors.size() + 1, 0);
    for (std::size_t s = 0; s < level.sectors.size(); ++s)
        surfaceBase[s + 1] = surfaceBase[s] + static_cast<std::int32_t>(level.sectors[s].surfaces.size());
    for (const CogBindingRow& row : description.rows)
    {
        if (!row.symbolName) continue;
        assets::CogRuntimeBinding binding;
        binding.symbol = *row.symbolName;
        std::transform(binding.symbol.begin(), binding.symbol.end(), binding.symbol.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (row.target.sectorIndex && row.target.surfaceIndex && validSector(level, *row.target.sectorIndex))
            binding.value = assets::CogRuntimeValue::ofInt(surfaceBase[static_cast<std::size_t>(*row.target.sectorIndex)] + *row.target.surfaceIndex);
        else if (row.target.index)
            binding.value = assets::CogRuntimeValue::ofInt(*row.target.index);
        else if (row.rawValue)
        {
            binding.value.type = assets::CogRuntimeValue::Type::String;
            binding.value.text = *row.rawValue;
        }
        else
            continue;
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

AssistantCheck checkAssistant(const Level& level, const AssistantKind kind, const std::int32_t cog, const std::string_view scriptText,
    const assets::CogPreflightTarget target)
{
    AssistantCheck out;
    out.passed = true;
    const auto expect = [&out](const bool condition, const std::string& text) {
        out.expectations.push_back((condition ? "ok: " : "failed: ") + text);
        out.passed = out.passed && condition;
    };
    if (cog < 0 || static_cast<std::size_t>(cog) >= level.cogs.size())
    {
        expect(false, "COG " + std::to_string(cog) + " is placed in the level");
        return out;
    }
    const std::string& name = level.cogs[static_cast<std::size_t>(cog)].name;
    const assets::CogCompileResult compiled = assets::compileCog(name, scriptText, target);
    if (!compiled.loads)
    {
        expect(false, name + " loads in the game");
        return out;
    }
    const assets::CogSchema schema = assets::parseCogSchema(name, scriptText);
    assets::CogRuntime runtime(assets::cogWorldFromLevel(level), target);
    std::string error;
    const std::int32_t index = runtime.addCog(compiled, name, placedCogRuntimeBindings(level, cog, schema), error);
    if (index < 0)
    {
        expect(false, error);
        return out;
    }
    const auto ref = [&](const std::string& symbol) -> std::int32_t {
        const auto value = runtime.symbolValue(index, symbol);
        if (!value) return -1;
        return value->type == assets::CogRuntimeValue::Type::Flex ? static_cast<std::int32_t>(value->f) : value->i;
    };
    const auto& world = runtime.world();
    const std::int32_t player = world.player;
    const auto thingAt = [&](const std::int32_t i) -> const assets::CogWorldThing* {
        return i >= 0 && static_cast<std::size_t>(i) < world.things.size() ? &world.things[static_cast<std::size_t>(i)] : nullptr;
    };
    const auto at = [&](const std::vector<std::int32_t>& things, const std::int32_t frame) {
        return std::all_of(things.begin(), things.end(), [&](const std::int32_t i) {
            const auto* t = thingAt(i);
            return t && t->frame == frame && !t->moving;
        });
    };
    // Runs the clock until `done` holds; returns the time, or -1 after `limit` seconds.
    const auto waitFor = [&](const std::function<bool()>& done, const double limit) -> double {
        const double stop = runtime.time() + limit;
        while (runtime.time() < stop)
        {
            runtime.advance(0.01);
            if (done()) return runtime.time();
        }
        return -1.0;
    };
    const auto when = [](const double t) { return " (" + number(std::round(t * 100.0) / 100.0) + " s)"; };
    runtime.startup();
    runtime.advance(0.5);

    if (kind == AssistantKind::door || kind == AssistantKind::switchDoor)
    {
        std::vector<std::int32_t> pieces;
        for (int i = 0; i < 4; ++i)
        {
            const std::int32_t piece = ref("door" + std::to_string(i));
            if (thingAt(piece)) pieces.push_back(piece);
        }
        if (pieces.empty())
        {
            expect(false, "the COG has a door piece");
        }
        else
        {
            const auto* first = thingAt(pieces.front());
            const std::int32_t sector = first->sector;
            const auto adjoinsShown = [&] {
                return sector >= 0 && static_cast<std::size_t>(sector) < world.sectors.size()
                    && world.sectors[static_cast<std::size_t>(sector)].adjoinsShown;
            };
            if (sector >= 0) expect(!adjoinsShown(), "at startup the door's sector " + std::to_string(sector) + " hides its adjoins");
            std::int32_t press = pieces.front();
            std::int32_t pressKind = 3;
            std::string pressed = "using the door";
            if (kind == AssistantKind::switchDoor)
            {
                runtime.sendLinked("activated", 3, pieces.front(), 3, player);
                runtime.advance(1.0);
                expect(at(pieces, 0), "the door is locked: using it does nothing");
                press = ref("switch0");
                pressKind = 6;
                pressed = "pressing the switch";
            }
            runtime.sendLinked("activated", pressKind, press, 3, player);
            const double opened = waitFor([&] { return at(pieces, 1); }, 60.0);
            expect(opened >= 0, pressed + " opens all " + std::to_string(pieces.size()) + " piece(s)" + (opened >= 0 ? when(opened) : ""));
            if (opened >= 0 && sector >= 0) expect(adjoinsShown(), "its sector shows its adjoins while it is open");
            if (opened >= 0 && kind == AssistantKind::switchDoor)
            {
                const std::int32_t sw = ref("switch0");
                expect(sw >= 0 && static_cast<std::size_t>(sw) < world.surfaces.size() && world.surfaces[static_cast<std::size_t>(sw)].cel == 1,
                    "the switch shows cel 1 while the door is open");
            }
            const double closed = waitFor([&] { return at(pieces, 0); }, 660.0);
            expect(closed >= 0, "it closes again by itself" + (closed >= 0 ? when(closed) : ""));
            if (closed >= 0 && sector >= 0)
            {
                runtime.advance(0.1);
                expect(!adjoinsShown(), "its sector hides its adjoins again once it is shut");
            }
        }
    }
    else if (kind == AssistantKind::elevator)
    {
        const std::int32_t lift = ref("elevator");
        const std::int32_t button = ref("button");
        const std::int32_t call = ref("lower_adjoin0");
        if (!thingAt(lift))
        {
            expect(false, "the COG has an elevator");
        }
        else
        {
            const auto celOf = [&](const std::int32_t s) {
                return s >= 0 && static_cast<std::size_t>(s) < world.surfaces.size() ? world.surfaces[static_cast<std::size_t>(s)].cel : -1;
            };
            const auto ride = [&](const std::string& how, const std::string& message, const std::int32_t surface) {
                runtime.sendLinked(message, 6, surface, 3, player);
                const double up = waitFor([&] { return at({ lift }, 1); }, 120.0);
                expect(up >= 0, how + " sends the lift up" + (up >= 0 ? when(up) : ""));
                if (up >= 0 && button >= 0) expect(celOf(button) == 1, "the button shows cel 1 while the lift is away");
                const double down = waitFor([&] { return at({ lift }, 0); }, 720.0);
                expect(down >= 0, "it comes back down by itself" + (down >= 0 ? when(down) : ""));
                if (down >= 0 && button >= 0)
                {
                    runtime.advance(0.1);
                    expect(celOf(button) == 0, "the button is back to cel 0");
                }
            };
            if (button >= 0) ride("pressing the button", "activated", button);
            if (call >= 0) ride("crossing the call adjoin", "crossed", call);
        }
    }
    else if (kind == AssistantKind::objectives)
    {
        const std::int32_t count = ref("goalcount");
        const std::int32_t base = ref("goalbase");
        const std::int32_t exit = ref("exit");
        const auto flags = [&](const std::int32_t goal) {
            const auto it = world.goals.find(goal);
            return it == world.goals.end() ? 0 : it->second;
        };
        const auto inv = world.inventory.find(99);
        expect(inv != world.inventory.end() && static_cast<std::int32_t>(inv->second) == base,
            "the objectives screen reads its text from GOAL_" + std::to_string(base));
        bool shown = count > 0;
        for (std::int32_t g = 0; g < count; ++g) shown = shown && (flags(g) & 1) != 0;
        expect(shown, "all " + std::to_string(count) + " goal(s) are shown at startup");
        if (exit >= 0)
        {
            runtime.sendLinked("crossed", 6, exit, 3, player);
            runtime.advance(0.2);
            expect(!world.levelEnded, "the exit doesn't end the level before the goals are done");
        }
        for (std::int32_t g = 0; g < count; ++g)
        {
            std::string how;
            if (const std::int32_t s = ref("reach" + std::to_string(g)); s >= 0)
            {
                runtime.sendLinked("entered", 5, s, 3, player);
                how = "entering sector " + std::to_string(s);
            }
            else if (const std::int32_t t = ref("use" + std::to_string(g)); t >= 0)
            {
                runtime.sendLinked("activated", 3, t, 3, player);
                how = "using Thing " + std::to_string(t);
            }
            else if (const std::int32_t k = ref("kill" + std::to_string(g)); k >= 0)
            {
                runtime.damageThing(k, 100000.0f, 1, player);
                how = "killing Thing " + std::to_string(k);
            }
            runtime.advance(0.2);
            expect(!how.empty() && (flags(g) & 2) != 0, "goal " + std::to_string(g) + " is completed by " + (how.empty() ? "nothing" : how));
        }
        if (exit >= 0)
        {
            runtime.sendLinked("crossed", 6, exit, 3, player);
            runtime.advance(0.2);
            expect(world.levelEnded, "crossing the exit then ends the level");
        }
        else if (ref("endwhendone") != 0)
            expect(world.levelEnded, "the last goal ends the level");
        else
            expect(!world.levelEnded, "the level doesn't end by itself");
    }
    else if (kind == AssistantKind::pursuit)
    {
        // AI isn't simulated: check that the controller starts parked and keeps running.
        const std::int32_t bike = ref("bike");
        expect(thingAt(bike) != nullptr, "the controller is bound to an enemy Thing");
        const bool parked = std::any_of(runtime.events().begin(), runtime.events().end(),
            [](const assets::CogRuntimeEvent& e) { return e.kind == "verb" && e.text.find("PlayKey(") == 0; });
        expect(parked, "at startup it plays the enemy's parked animation");
        runtime.damageThing(bike, 1.0f, 1, player);
        runtime.advance(2.0);
        expect(true, "the chase isn't checked: the runtime doesn't simulate AI, so test it in the game");
    }
    else if (kind == AssistantKind::cutscene)
    {
        const std::int32_t camera = ref("camera");
        const std::int32_t last = ref("lastframe");
        const std::int32_t trigger = ref("trigger");
        if (!thingAt(camera))
        {
            expect(false, "the COG has a camera Thing");
        }
        else
        {
            const auto play = [&] {
                if (trigger >= 0) runtime.sendLinked("entered", 5, trigger, 3, player);
            };
            if (trigger >= 0) play();
            const double arrived = waitFor([&] { return at({ camera }, last); }, 600.0);
            expect(arrived >= 0, std::string(trigger >= 0 ? "entering sector " + std::to_string(trigger) : "startup")
                    + " sends the camera to frame " + std::to_string(last) + (arrived >= 0 ? when(arrived) : ""));
            expect(world.cameraFocus == camera, "the view follows the camera");
            const auto* held = thingAt(player);
            if (held) expect((held->actorFlags & 0xa00000u) == 0xa00000u, "the player is held still");
            const double back = waitFor([&] { return world.cameraFocus == player; }, 660.0);
            expect(back >= 0, "the view returns to the player" + (back >= 0 ? when(back) : ""));
            if (held) expect((held->actorFlags & 0xa00000u) == 0, "the player can move again");
            if (trigger >= 0 && ref("once") != 0)
            {
                play();
                runtime.advance(0.5);
                expect(world.cameraFocus == player, "entering again doesn't play it twice");
            }
        }
    }
    for (const assets::CogRuntimeEvent& event : runtime.events())
    {
        if (event.kind == "message" && event.text.rfind("startup", 0) == 0) continue;
        out.timeline.push_back(number(std::round(event.time * 100.0) / 100.0) + " s  " + event.kind + "  " + event.text);
    }
    for (const assets::CogRuntimeEvent& event : runtime.events())
    {
        if (event.kind == "error") expect(false, "the script runs without errors: " + event.text);
    }
    return out;
}

} // namespace sed::level
