#pragma once

#include "sed/assets/cog_preflight.hpp"
#include "sed/assets/cog_runtime.hpp"
#include "sed/level.hpp"
#include "sed/level/workbench.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{
class Templates;
struct CogSchema;
} // namespace sed::assets

namespace sed::level
{

/**
 * Gameplay assistants: doors, elevators, switches, objectives and cutscenes.
 *
 * Each assistant validates the selection and settings, then writes path frames
 * and one placed COG in one step, or refuses and leaves the level unchanged.
 * Doors, elevators and switch doors use stock scripts every JK install has;
 * objectives and cutscenes use scripts RED writes itself
 * (`assistantScriptText`). `checkAssistant` then runs the placed COG in the
 * COG runtime with a scenario for its kind and reports what happened.
 */

inline constexpr std::string_view kDoorAssistantScript = "00_door.cog";
inline constexpr std::string_view kElevatorAssistantScript = "00_elev_switch.cog";
inline constexpr std::string_view kSwitchAssistantScript = "00_doorswitch.cog";
inline constexpr std::string_view kObjectivesAssistantScript = "red_goals.cog";
inline constexpr std::string_view kCutsceneAssistantScript = "red_cutscene.cog";
/** Lily's enemy swoop controller, from RED's COG template library (swoop-enemy-pursuit). */
inline constexpr std::string_view kPursuitAssistantScript = "kcswoop.cog";
inline constexpr std::size_t kMaxAssistantRoute = 4;
inline constexpr std::size_t kMaxAssistantDoorPieces = 4;
inline constexpr std::size_t kMaxAssistantCallAdjoins = 3;
inline constexpr std::size_t kMaxAssistantSwitches = 2;
inline constexpr std::size_t kMaxAssistantGoals = 8;

enum class AssistantKind
{
    door,
    elevator,
    switchDoor,
    objectives,
    cutscene,
    pursuit,
};

/** "door", "elevator", "switch", "objectives", "cutscene" or "pursuit". */
std::string_view assistantKindName(AssistantKind kind) noexcept;
std::optional<AssistantKind> parseAssistantKind(std::string_view name) noexcept;
/** The script an assistant places. */
std::string_view assistantScriptName(AssistantKind kind) noexcept;

/** The text of a script RED writes itself (red_goals.cog, red_cutscene.cog); empty for any other name. */
std::string_view assistantScriptText(std::string_view scriptName) noexcept;

struct AssistantResult
{
    std::optional<WorkbenchFailure> failure{};
    std::int32_t cog{ -1 };
    /** Things that will work but may not be what was meant. */
    std::vector<std::string> warnings{};

    [[nodiscard]] bool ok() const noexcept { return !failure.has_value(); }
};

/** 00_door.cog: one to four pieces open together by `openOffset` and close after `wait`. */
struct DoorAssistantRequest
{
    std::vector<std::int32_t> pieces{};
    std::array<double, 3> openOffset{ 0.0, 0.0, 1.0 };
    double speed{ 8.0 };
    double wait{ 2.0 };
    /** Light added to the door's sector. */
    double light{ 0.5 };
};

/** 00_elev_switch.cog: a lift that rises by `travel` when its button is pressed or a call adjoin is crossed. */
struct ElevatorAssistantRequest
{
    std::int32_t elevator{ -1 };
    std::array<double, 3> travel{ 0.0, 0.0, 1.0 };
    std::optional<SurfaceRef> button{};
    std::vector<SurfaceRef> callAdjoins{};
    double speed{ 4.0 };
    double wait{ 2.0 };
    double startWait{ 0.25 };
};

/** 00_doorswitch.cog: a locked door that only its switches open; the switches show cel 1 while it is open. */
struct SwitchAssistantRequest
{
    std::vector<std::int32_t> pieces{};
    std::array<double, 3> openOffset{ 0.0, 0.0, 1.0 };
    std::vector<SurfaceRef> switches{};
    double speed{ 8.0 };
    double wait{ 2.0 };
    double light{ 0.5 };
};

enum class GoalTrigger
{
    reach,   // the player enters a sector
    use,     // the player activates a Thing
    kill,    // a Thing is killed
};
std::string_view goalTriggerName(GoalTrigger trigger) noexcept;
std::optional<GoalTrigger> parseGoalTrigger(std::string_view name) noexcept;

struct AssistantGoal
{
    GoalTrigger trigger{ GoalTrigger::reach };
    /** A sector for reach, a Thing for use and kill. */
    std::int32_t target{ -1 };
};

/** red_goals.cog: goals shown at startup and completed by their triggers; the level ends at the exit or with the last goal. */
struct ObjectivesAssistantRequest
{
    /** GOAL_<goalBase + i> in cogstrings.uni holds goal i's text; retail uses 1000 for level 1, 2000 for level 2, and so on. */
    std::int32_t goalBase{ 1000 };
    std::vector<AssistantGoal> goals{};
    std::optional<SurfaceRef> exit{};
    bool endWhenDone{ true };
};

/** red_cutscene.cog: the view follows a camera Thing along its existing path frames, holds, and returns. */
struct CutsceneAssistantRequest
{
    std::int32_t camera{ -1 };
    /** Entering this sector plays it; without one it plays at startup. */
    std::optional<std::int32_t> trigger{};
    double speed{ 2.0 };
    double hold{ 2.0 };
    bool once{ true };
};

/**
 * kcswoop.cog: an enemy that wakes on sight, on the player entering
 * `wakeSector`, or on damage; plays its startup; follows up to four route
 * markers with the game's AI; then pursues the player.
 */
struct PursuitAssistantRequest
{
    std::int32_t enemy{ -1 };
    /** Marker Things (+kcsway ghosts) in travel order. */
    std::vector<std::int32_t> route{};
    std::optional<std::int32_t> wakeSector{};
    double wakeDistance{ 5.5 };
    double moveSpeed{ 1.5 };
    double ramDamage{ 18.0 };
    double fireInterval{ 0.65 };
    double survivalChance{ 0.5 };
};

/**
 * The assistants. `scriptSymbols` are the stock script's symbols (as
 * `loadCogSymbols…` returns them, without locals); the refusal
 * `assistantScriptMismatch` names a symbol the script lacks. Other refusals:
 * `assistantTargetOutOfRange`, `assistantDuplicateTarget`, `assistantTooMany`,
 * `assistantNothingSelected`, `assistantNotPathMover` (the Thing's template
 * doesn't move on a path), `assistantAlreadyMoves` (it already has frames),
 * `assistantNotAdjoin`, `assistantNoCameraPath`, `assistantAlreadyPlaced`,
 * `assistantInvalidSetting` and `assistantIdSpaceExhausted`.
 */
AssistantResult levelDoorAssistant(Level& level, const assets::Templates* templates, const DoorAssistantRequest& request,
    const std::vector<CogValue>& scriptSymbols);
AssistantResult levelElevatorAssistant(Level& level, const assets::Templates* templates,
    const ElevatorAssistantRequest& request, const std::vector<CogValue>& scriptSymbols);
AssistantResult levelSwitchAssistant(Level& level, const assets::Templates* templates, const SwitchAssistantRequest& request,
    const std::vector<CogValue>& scriptSymbols);
AssistantResult levelObjectivesAssistant(Level& level, const ObjectivesAssistantRequest& request);
AssistantResult levelCutsceneAssistant(Level& level, const assets::Templates* templates, const CutsceneAssistantRequest& request);
AssistantResult levelPursuitAssistant(Level& level, const PursuitAssistantRequest& request, const std::vector<CogValue>& scriptSymbols);

/**
 * The runtime bindings of a placed COG: things and sectors by index, surfaces
 * numbered sector by sector, other values as text.
 */
std::vector<assets::CogRuntimeBinding> placedCogRuntimeBindings(
    const Level& level, std::int32_t cogIndex, const assets::CogSchema& schema);

struct AssistantCheck
{
    bool passed{ false };
    /** One line per expectation, starting "ok: " or "failed: ". */
    std::vector<std::string> expectations{};
    /** The runtime's log, one line per event: time, kind, text. */
    std::vector<std::string> timeline{};
};

/**
 * Runs a placed assistant COG in the COG runtime with the scenario for its
 * kind (press the door, call the lift, try the locked door then its switch,
 * complete each goal and take the exit, play the cutscene) and checks the
 * outcome. `scriptText` is the script's source.
 */
AssistantCheck checkAssistant(const Level& level, AssistantKind kind, std::int32_t cog, std::string_view scriptText,
    assets::CogPreflightTarget target = assets::CogPreflightTarget::jk);

} // namespace sed::level
