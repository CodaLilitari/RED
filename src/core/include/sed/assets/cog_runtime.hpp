#pragma once

#include "sed/assets/cog_compiler.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sed
{
struct Level;
}

namespace sed::assets
{

/**
 * The COG runtime: runs compiled scripts against a stand-in world, so a
 * door, elevator or switch can be tried in RED before the game.
 *
 * The virtual machine follows the retail engine (OpenJKDF2's sithCogExec,
 * 0BSD): its stack of 64 values (the oldest is dropped when it overflows),
 * four levels of `call`, arithmetic that always yields a flex, `!=` that
 * compares integers, a popped empty stack reading -1 as an int and 0 as a
 * flex, and messages that interrupt a sleeping script and resume it.
 * Scripts sleep, set timers and pulses, and wait for things to stop on a
 * simulated clock.
 *
 * The world is a stand-in: things with positions and path frames that move
 * at the speed a script asks and send `arrived` when they get there;
 * surfaces with cels and flags; sectors with light and adjoins; one player's
 * inventory and goals. Over a hundred common verbs are simulated. Every
 * other verb is logged as not simulated: it takes its arguments off the
 * stack and leaves a zero result, so the script carries on.
 */

struct CogRuntimeValue
{
    enum class Type
    {
        Int,
        Flex,
        Vector,
        Symbol,
        String,
        Verb,
    };
    Type type{ Type::Int };
    std::int32_t i{};
    float f{};
    std::array<float, 3> v{};
    std::string text{};

    static CogRuntimeValue ofInt(std::int32_t value);
    static CogRuntimeValue ofFlex(float value);
    static CogRuntimeValue ofVector(const std::array<float, 3>& value);
    [[nodiscard]] std::string describe() const;
};

struct CogWorldThing
{
    std::string name{};
    std::string templateName{};
    std::array<double, 3> position{};
    std::int32_t sector{ -1 };
    /** Path frames (x, y, z), frame 0 first. A thing without frames can't move. */
    std::vector<std::array<double, 3>> frames{};
    std::int32_t frame{};
    std::int32_t goalFrame{};
    double speed{};
    bool moving{ false };
    double health{ 100.0 };
    bool destroyed{ false };
    std::uint32_t flags{};
    double light{};
    double userData{};
};

struct CogWorldSurface
{
    std::int32_t sector{ -1 };
    std::int32_t surface{ -1 };
    std::int32_t cel{};
    std::uint32_t flags{};
    std::uint32_t adjoinFlags{};
    double light{};
    std::array<double, 3> center{};
};

struct CogWorldSector
{
    double light{};
    std::uint32_t flags{};
    bool adjoinsShown{ true };
    std::int32_t colormap{};
    std::array<double, 3> tint{};
};

struct CogWorld
{
    std::vector<CogWorldThing> things{};
    /** In the engine's numbering: every surface of sector 0, then sector 1, and so on. */
    std::vector<CogWorldSurface> surfaces{};
    std::vector<CogWorldSector> sectors{};
    std::int32_t player{ -1 };
    std::map<std::int32_t, double> inventory{};
    std::map<std::int32_t, std::int32_t> goals{};
    bool levelEnded{ false };
    std::int32_t levelEndValue{};
    /** GetDifficulty's answer: 0 easy, 1 medium, 2 hard. */
    std::int32_t difficulty{ 1 };
};

/**
 * Builds the stand-in world from a level: things with their positions,
 * sectors and `frame=` path frames; sectors with their light; surfaces with
 * their cels, flags, light and centres. The first player start, if any, is
 * the player.
 */
CogWorld cogWorldFromLevel(const Level& level);

struct CogRuntimeEvent
{
    double time{};
    std::int32_t cog{ -1 };
    /** "message", "verb", "print", "unsimulated", "arrived", "error" or "note". */
    std::string kind{};
    std::string text{};
};

/** One symbol's starting value, for linking a script to the world. */
struct CogRuntimeBinding
{
    std::string symbol{};
    CogRuntimeValue value{};
};

struct CogRuntimeOptions
{
    /** Instructions one script may run without sleeping before it is stopped as a runaway. */
    std::size_t instructionBudget{ 200000 };
    /** Events kept before the log stops recording. */
    std::size_t eventLimit{ 20000 };
};

class CogRuntime
{
public:
    CogRuntime(CogWorld world, CogPreflightTarget target, CogRuntimeOptions options = {});
    ~CogRuntime();
    CogRuntime(const CogRuntime&) = delete;
    CogRuntime& operator=(const CogRuntime&) = delete;

    /**
     * Adds a compiled script. Bindings set symbol values (things, sectors
     * and surfaces as indices; resources by name). Returns the script's
     * index, or -1 with `error` set when the script doesn't load.
     */
    std::int32_t addCog(const CogCompileResult& compiled, const std::string& name,
        const std::vector<CogRuntimeBinding>& bindings, std::string& error);

    /** Sends `startup` to every script that handles it, in the order they were added. */
    void startup();

    /**
     * Sends a message to one script. Types are the engine's sender types
     * (3 thing, 5 sector, 6 surface; 0 none). Returns ReturnEx's value, or 0.
     */
    float sendMessage(std::int32_t cog, const std::string& message, std::int32_t senderType = 0,
        std::int32_t senderRef = -1, std::int32_t sourceType = 0, std::int32_t sourceRef = -1,
        const std::array<float, 4>& params = {});

    /**
     * Sends a world event to every script linked to the object: a script is
     * linked when one of its resource symbols that isn't `local` or `nolink`
     * holds the object. GetSenderId gives that symbol's linkid.
     * `kind` is 3 (thing), 5 (sector) or 6 (surface).
     */
    void sendLinked(const std::string& message, std::int32_t kind, std::int32_t index,
        std::int32_t sourceType = 3, std::int32_t sourceRef = -1);

    /**
     * Damages a thing as a weapon would: scripts linked to it get `damaged`
     * (amount and type as parameters), then `killed` if its health runs out.
     */
    void damageThing(std::int32_t thing, float amount, std::int32_t type = 1, std::int32_t sourceRef = -1);

    /** Runs the clock forward in steps: movement, sleeps, timers and pulses. */
    void advance(double seconds, double step = 0.01);

    [[nodiscard]] double time() const noexcept;
    [[nodiscard]] const CogWorld& world() const noexcept;
    [[nodiscard]] const std::vector<CogRuntimeEvent>& events() const noexcept;
    [[nodiscard]] std::size_t cogCount() const noexcept;
    /** A symbol's current value in one script, or nullopt. */
    [[nodiscard]] std::optional<CogRuntimeValue> symbolValue(std::int32_t cog, const std::string& symbol) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

/** The verbs the runtime simulates (lowercase), for documentation and tests. */
const std::vector<std::string>& cogSimulatedVerbs();

} // namespace sed::assets
