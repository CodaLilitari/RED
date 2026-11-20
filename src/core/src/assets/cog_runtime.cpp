// The COG runtime. The virtual machine follows the retail engine as
// reconstructed by OpenJKDF2 (src/Cog/sithCogExec.c; 0BSD, see
// THIRD_PARTY_NOTICES.md); the world it runs against is RED's stand-in.

#include "sed/assets/cog_runtime.hpp"

#include "sed/level.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace sed::assets
{

CogRuntimeValue CogRuntimeValue::ofInt(const std::int32_t value)
{
    CogRuntimeValue v;
    v.type = Type::Int;
    v.i = value;
    return v;
}

CogRuntimeValue CogRuntimeValue::ofFlex(const float value)
{
    CogRuntimeValue v;
    v.type = Type::Flex;
    v.f = value;
    return v;
}

CogRuntimeValue CogRuntimeValue::ofVector(const std::array<float, 3>& value)
{
    CogRuntimeValue v;
    v.type = Type::Vector;
    v.v = value;
    return v;
}

namespace
{

std::string number(const double value)
{
    std::ostringstream out;
    out << std::setprecision(6) << value;
    return out.str();
}

std::string vectorText(const std::array<float, 3>& v)
{
    return "(" + number(v[0]) + "/" + number(v[1]) + "/" + number(v[2]) + ")";
}

} // namespace

std::string CogRuntimeValue::describe() const
{
    switch (type)
    {
        case Type::Int: return std::to_string(i);
        case Type::Flex: return number(f);
        case Type::Vector: return vectorText(v);
        case Type::Symbol: return "symbol " + std::to_string(i);
        case Type::String: return "\"" + text + "\"";
        case Type::Verb: return text + "()";
    }
    return "?";
}

namespace
{

enum Op : std::int32_t
{
    kNop = 0, kPushInt = 1, kPushFloat = 2, kPushSymbol = 3, kArrayIndex = 4, kCallFunc = 5, kAssign = 6,
    kPushVector = 7, kAdd = 8, kSub = 9, kMul = 10, kDiv = 11, kMod = 12, kCmpFalse = 13, kNeg = 14, kCmpGt = 15,
    kCmpLs = 16, kCmpEq = 17, kCmpLe = 18, kCmpGe = 19, kCmpAnd = 20, kCmpOr = 21, kCmpNe = 22, kAndI = 23,
    kOrI = 24, kXorI = 25, kGoFalse = 26, kGoTrue = 27, kGo = 28, kRet = 29, kCall = 31,
};

constexpr std::size_t kStackSize = 64;      // SITHCOGVM_MAX_STACKSIZE
constexpr std::int32_t kCallDepth = 4;      // sithCog::callstack

enum class Running
{
    Stopped = 0,
    Running = 1,
    Sleeping = 2,
    Waiting = 3,
};

struct Frame
{
    std::int32_t pc{};
    Running running{ Running::Stopped };
    double wake{};
};

struct Timer
{
    double at{};
    std::int32_t id{};
    float param0{};
    float param1{};
};

struct Link
{
    std::int32_t kind{};   // 3 thing, 5 sector, 6 surface
    std::int32_t index{};
    std::int32_t linkId{};
};

struct Cog
{
    std::string name;
    CogCompileResult compiled;
    std::vector<CogRuntimeValue> symbols;
    std::vector<std::string> symbolKinds;
    std::vector<Link> links;
    std::int32_t pc{};
    Running running{ Running::Stopped };
    double wake{};
    std::int32_t waitThing{ -1 };
    std::vector<Frame> callstack;
    std::vector<CogRuntimeValue> stack;
    std::int32_t senderId{ -1 };
    std::int32_t senderRef{ -1 };
    std::int32_t senderType{};
    std::int32_t sourceRef{ -1 };
    std::int32_t sourceType{};
    std::array<float, 4> params{};
    float returnEx{};
    bool timerSet{ false };
    double timerAt{};
    bool pulseSet{ false };
    double pulsePeriod{};
    double pulseNext{};
    std::vector<Timer> timers;
};

} // namespace

struct CogRuntime::Impl
{
    CogWorld world;
    CogPreflightTarget target;
    CogRuntimeOptions options;
    std::vector<CogGlobalSymbol> globalSymbols;
    std::vector<CogRuntimeValue> globals;
    std::unordered_map<std::string, std::int32_t> globalIndex;
    std::vector<std::unique_ptr<Cog>> cogs;
    std::vector<std::string> resources;   // names behind resource handles
    std::vector<CogMessageBreakpoint> breakpoints;
    std::vector<CogRuntimeEvent> events;
    double now{};
    std::int32_t nextChannel{ 1 };
    std::int32_t masterCog{ -1 };
    std::uint32_t randState{ 0x12345678u };
    bool eventsFull{ false };
    std::int32_t pushedByVerb{};

    using Verb = std::function<void(Impl&, Cog&, std::int32_t self)>;

    // ---- the log ----
    void log(const std::int32_t cog, std::string kind, std::string text)
    {
        if (events.size() >= options.eventLimit)
        {
            if (!eventsFull)
            {
                eventsFull = true;
                events.push_back(CogRuntimeEvent{ now, -1, "note", "The event log is full; later events aren't recorded." });
            }
            return;
        }
        events.push_back(CogRuntimeEvent{ now, cog, std::move(kind), std::move(text) });
    }

    // ---- values (sithCogExec_Pop*) ----
    CogRuntimeValue* symbolSlot(Cog& cog, const std::int32_t id)
    {
        if (id >= 0x100)
        {
            const auto g = static_cast<std::size_t>(id - 0x100);
            return g < globals.size() ? &globals[g] : nullptr;
        }
        if (id < 0 || static_cast<std::size_t>(id) >= cog.symbols.size()) return nullptr;
        return &cog.symbols[static_cast<std::size_t>(id)];
    }

    void push(Cog& cog, CogRuntimeValue value)
    {
        if (cog.stack.size() == kStackSize) cog.stack.erase(cog.stack.begin());   // the engine drops the oldest
        cog.stack.push_back(std::move(value));
        ++pushedByVerb;
    }

    std::optional<CogRuntimeValue> popRaw(Cog& cog)
    {
        if (cog.stack.empty()) return std::nullopt;
        CogRuntimeValue v = std::move(cog.stack.back());
        cog.stack.pop_back();
        return v;
    }

    // sithCogExec_PopSymbol: a symbol id reads as the symbol's value.
    std::optional<CogRuntimeValue> pop(Cog& cog)
    {
        auto v = popRaw(cog);
        if (!v) return std::nullopt;
        if (v->type == CogRuntimeValue::Type::Symbol)
        {
            const CogRuntimeValue* slot = symbolSlot(cog, v->i);
            return slot ? *slot : CogRuntimeValue::ofInt(0);
        }
        return v;
    }

    std::int32_t popInt(Cog& cog)
    {
        const auto v = pop(cog);
        if (!v) return -1;
        if (v->type == CogRuntimeValue::Type::Int) return v->i;
        if (v->type == CogRuntimeValue::Type::Flex) return static_cast<std::int32_t>(v->f);
        return -1;
    }

    float popFlex(Cog& cog)
    {
        const auto v = pop(cog);
        if (!v) return 0.0f;
        if (v->type == CogRuntimeValue::Type::Int) return static_cast<float>(v->i);
        if (v->type == CogRuntimeValue::Type::Flex) return v->f;
        return 0.0f;
    }

    bool popVector(Cog& cog, std::array<float, 3>& out)
    {
        const auto v = pop(cog);
        if (v && v->type == CogRuntimeValue::Type::Vector)
        {
            out = v->v;
            return true;
        }
        out = {};
        return false;
    }

    // Things, sectors, surfaces and resources: an index, -1 for none.
    std::int32_t popIndex(Cog& cog)
    {
        const auto v = pop(cog);
        if (!v) return -1;
        switch (v->type)
        {
            case CogRuntimeValue::Type::Int: return v->i;
            case CogRuntimeValue::Type::Flex: return static_cast<std::int32_t>(v->f);
            case CogRuntimeValue::Type::Vector: return static_cast<std::int32_t>(v->v[0]);
            default: return -1;
        }
    }

    std::string popString(Cog& cog)
    {
        const auto v = pop(cog);
        if (v && v->type == CogRuntimeValue::Type::String) return v->text;
        return {};
    }

    CogWorldThing* thing(const std::int32_t index)
    {
        return index >= 0 && static_cast<std::size_t>(index) < world.things.size() ? &world.things[static_cast<std::size_t>(index)] : nullptr;
    }
    CogWorldSurface* surface(const std::int32_t index)
    {
        return index >= 0 && static_cast<std::size_t>(index) < world.surfaces.size() ? &world.surfaces[static_cast<std::size_t>(index)] : nullptr;
    }
    CogWorldSector* sector(const std::int32_t index)
    {
        return index >= 0 && static_cast<std::size_t>(index) < world.sectors.size() ? &world.sectors[static_cast<std::size_t>(index)] : nullptr;
    }

    std::string thingName(const std::int32_t index)
    {
        const CogWorldThing* t = thing(index);
        if (!t) return "thing " + std::to_string(index) + " (none)";
        return "thing " + std::to_string(index) + (t->name.empty() ? "" : " (" + t->name + ")");
    }
    std::string resourceName(const std::int32_t handle)
    {
        return handle >= 0 && static_cast<std::size_t>(handle) < resources.size() ? resources[static_cast<std::size_t>(handle)]
                                                                                  : "resource " + std::to_string(handle);
    }
    std::int32_t resourceHandle(const std::string& name)
    {
        for (std::size_t i = 0; i < resources.size(); ++i)
        {
            if (resources[i] == name) return static_cast<std::int32_t>(i);
        }
        resources.push_back(name);
        return static_cast<std::int32_t>(resources.size() - 1);
    }

    // ---- messages (sithCog_SendMessage / sithCogExec_ExecCog) ----
    void pushCallstack(Cog& cog)
    {
        if (static_cast<std::int32_t>(cog.callstack.size()) == kCallDepth) return;
        cog.callstack.push_back(Frame{ cog.pc, cog.running, cog.wake });
    }

    void popCallstack(Cog& cog)
    {
        if (cog.callstack.empty())
        {
            cog.running = Running::Stopped;
            return;
        }
        const Frame f = cog.callstack.back();
        cog.callstack.pop_back();
        cog.running = f.running;
        cog.pc = f.pc;
        cog.wake = f.wake;
    }

    float deliver(const std::int32_t index, const std::string& message, const std::int32_t senderType,
        const std::int32_t senderRef, const std::int32_t senderId, const std::int32_t sourceType,
        const std::int32_t sourceRef, const std::array<float, 4>& params)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= cogs.size()) return 0.0f;
        Cog& cog = *cogs[static_cast<std::size_t>(index)];
        // The engine matches a handler by message id, so "activate" and
        // "activated" are the same message; the first handler with the id wins.
        std::int32_t id = -1;
        if (const auto it = globalIndex.find(message); it != globalIndex.end()
            && globalSymbols[static_cast<std::size_t>(it->second)].kind == "message")
            id = globalSymbols[static_cast<std::size_t>(it->second)].messageId;
        const CogCompiledHandler* handler = nullptr;
        for (const auto& h : cog.compiled.handlers)
        {
            if (id >= 0 ? h.messageId == id : h.message == message)
            {
                handler = &h;
                break;
            }
        }
        if (!handler || handler->address < 0) return 0.0f;
        if (breakpoints.size() < std::min<std::size_t>(options.breakpointLimit, 256))
        {
            CogMessageBreakpoint record{};
            record.time = now;
            record.cog = index;
            record.message = handler->message;
            record.senderType = senderType;
            record.senderRef = senderRef;
            record.sourceType = sourceType;
            record.sourceRef = sourceRef;
            for (std::size_t i = 0; i < cog.compiled.symbols.size() && record.watches.size() < 64; ++i)
            {
                const auto& symbol = cog.compiled.symbols[i];
                if (symbol.kind == "message" || symbol.kind == "string" || symbol.kind == "implicit") continue;
                record.watches.emplace(symbol.name, cog.symbols[i].describe().substr(0, 128));
            }
            if (cog.timerSet) record.due.emplace_back("timer", cog.timerAt);
            if (cog.pulseSet) record.due.emplace_back("pulse", cog.pulseNext);
            for (const auto& timer : cog.timers)
            {
                if (record.due.size() >= 32) break;
                record.due.emplace_back("timer " + std::to_string(timer.id), timer.at);
            }
            std::ranges::sort(record.due, {}, &std::pair<std::string,double>::second);
            breakpoints.push_back(std::move(record));
        }
        log(index, "message", message + " from " + senderDescription(senderType, senderRef));
        cog.senderType = senderType;
        cog.senderRef = senderRef;
        cog.senderId = senderId;
        cog.sourceType = sourceType;
        cog.sourceRef = sourceRef;
        cog.params = params;
        cog.returnEx = 0.0f;
        pushCallstack(cog);
        cog.pc = handler->address;
        cog.running = Running::Running;
        execute(index);
        return cog.returnEx;
    }

    std::string senderDescription(const std::int32_t type, const std::int32_t ref)
    {
        switch (type)
        {
            case 3: return thingName(ref);
            case 5: return "sector " + std::to_string(ref);
            case 6: return "surface " + std::to_string(ref);
            case 1: return "cog " + std::to_string(ref);
            case 0: return ref < 0 ? "the runtime" : "object " + std::to_string(ref);
            default: return "type " + std::to_string(type) + " ref " + std::to_string(ref);
        }
    }

    void sendLinked(const std::string& message, const std::int32_t kind, const std::int32_t index,
        const std::int32_t sourceType, const std::int32_t sourceRef)
    {
        sendLinkedWithParams(message, kind, index, sourceType, sourceRef, {});
    }

    // ---- the virtual machine (sithCogExec_Execute) ----
    void execute(const std::int32_t index)
    {
        Cog& cog = *cogs[static_cast<std::size_t>(index)];
        const std::vector<std::int32_t>& code = cog.compiled.code;
        std::size_t budget = options.instructionBudget;
        const auto operand = [&]() -> std::int32_t {
            if (cog.pc < 0 || static_cast<std::size_t>(cog.pc) >= code.size()) return 0;
            return code[static_cast<std::size_t>(cog.pc++)];
        };
        while (cog.running == Running::Running)
        {
            if (budget-- == 0)
            {
                log(index, "error", "The script ran " + std::to_string(options.instructionBudget)
                        + " instructions without sleeping or stopping; the game would freeze here. Stopped it.");
                cog.running = Running::Stopped;
                cog.callstack.clear();
                return;
            }
            if (cog.pc < 0 || static_cast<std::size_t>(cog.pc) >= code.size())
            {
                log(index, "error", "The script jumped to address " + std::to_string(cog.pc)
                        + ", outside its code; in the game this reads whatever memory is there. Stopped it.");
                cog.running = Running::Stopped;
                cog.callstack.clear();
                return;
            }
            const std::int32_t op = code[static_cast<std::size_t>(cog.pc++)];
            switch (op)
            {
                case kNop:
                    break;
                case kPushInt:
                    push(cog, CogRuntimeValue::ofInt(operand()));
                    break;
                case kPushFloat:
                {
                    const std::int32_t bits = operand();
                    float f = 0;
                    std::memcpy(&f, &bits, sizeof f);
                    push(cog, CogRuntimeValue::ofFlex(f));
                    break;
                }
                case kPushSymbol:
                {
                    CogRuntimeValue v;
                    v.type = CogRuntimeValue::Type::Symbol;
                    v.i = operand();
                    push(cog, v);
                    break;
                }
                case kPushVector:
                {
                    std::array<float, 3> v{};
                    for (float& f : v)
                    {
                        const std::int32_t bits = operand();
                        std::memcpy(&f, &bits, sizeof f);
                    }
                    push(cog, CogRuntimeValue::ofVector(v));
                    break;
                }
                case kArrayIndex:
                {
                    const std::int32_t offset = popInt(cog);
                    const auto base = popRaw(cog);
                    CogRuntimeValue v;
                    v.type = CogRuntimeValue::Type::Symbol;
                    v.i = offset + (base && base->type == CogRuntimeValue::Type::Symbol ? base->i : 0);
                    push(cog, v);
                    break;
                }
                case kCallFunc:
                {
                    const auto callee = popRaw(cog);
                    if (!callee || callee->type != CogRuntimeValue::Type::Symbol) break;
                    const CogRuntimeValue* slot = symbolSlot(cog, callee->i);
                    if (!slot || slot->type != CogRuntimeValue::Type::Verb)
                    {
                        log(index, "error", "Called something that isn't a verb; the engine does nothing.");
                        break;
                    }
                    callVerb(cog, index, slot->text);
                    break;
                }
                case kAssign:
                {
                    const auto value = pop(cog);
                    if (!value) break;
                    const auto targetSymbol = popRaw(cog);
                    if (!targetSymbol || targetSymbol->type != CogRuntimeValue::Type::Symbol) break;
                    CogRuntimeValue* slot = symbolSlot(cog, targetSymbol->i);
                    if (slot)
                    {
                        if (slot->type == CogRuntimeValue::Type::Verb)
                            log(index, "error", "Overwrote the verb " + slot->text + "() for every script.");
                        *slot = *value;
                    }
                    break;
                }
                case kCmpFalse:
                    push(cog, CogRuntimeValue::ofInt(popInt(cog) == 0 ? 1 : 0));
                    break;
                case kNeg:
                    push(cog, CogRuntimeValue::ofFlex(-popFlex(cog)));
                    break;
                case kCmpAnd:
                case kCmpOr:
                case kCmpNe:
                case kAndI:
                case kOrI:
                case kXorI:
                {
                    const std::int32_t a = popInt(cog);
                    const std::int32_t b = popInt(cog);
                    std::int32_t r = 0;
                    if (op == kCmpAnd) r = (a && b) ? 1 : 0;
                    else if (op == kCmpOr) r = (a || b) ? 1 : 0;
                    else if (op == kCmpNe) r = a != b ? 1 : 0;
                    else if (op == kAndI) r = a & b;
                    else if (op == kOrI) r = a | b;
                    else r = a ^ b;
                    push(cog, CogRuntimeValue::ofInt(r));
                    break;
                }
                case kAdd:
                case kSub:
                case kMul:
                case kDiv:
                case kMod:
                case kCmpGt:
                case kCmpLs:
                case kCmpEq:
                case kCmpLe:
                case kCmpGe:
                {
                    const float a = popFlex(cog);
                    const float b = popFlex(cog);
                    switch (op)
                    {
                        case kAdd: push(cog, CogRuntimeValue::ofFlex(a + b)); break;
                        case kSub: push(cog, CogRuntimeValue::ofFlex(b - a)); break;
                        case kMul: push(cog, CogRuntimeValue::ofFlex(a * b)); break;
                        case kDiv: push(cog, CogRuntimeValue::ofFlex(a == 0.0f ? 0.0f : b / a)); break;
                        case kMod: push(cog, CogRuntimeValue::ofFlex(std::fmod(b, a))); break;
                        case kCmpGt: push(cog, CogRuntimeValue::ofInt(b > a ? 1 : 0)); break;
                        case kCmpLs: push(cog, CogRuntimeValue::ofInt(b < a ? 1 : 0)); break;
                        case kCmpEq: push(cog, CogRuntimeValue::ofInt(b == a ? 1 : 0)); break;
                        case kCmpLe: push(cog, CogRuntimeValue::ofInt(b <= a ? 1 : 0)); break;
                        default: push(cog, CogRuntimeValue::ofInt(b >= a ? 1 : 0)); break;
                    }
                    break;
                }
                case kGoFalse:
                {
                    const std::int32_t to = operand();
                    if (!popInt(cog)) cog.pc = to;
                    break;
                }
                case kGoTrue:
                {
                    const std::int32_t to = operand();
                    if (popInt(cog)) cog.pc = to;
                    break;
                }
                case kGo:
                    cog.pc = operand();
                    break;
                case kRet:
                    popCallstack(cog);
                    break;
                case kCall:
                {
                    if (static_cast<std::int32_t>(cog.callstack.size()) >= kCallDepth)
                    {
                        // The engine skips a fifth nested call without reading its address, so the
                        // address is then run as if it were an instruction.
                        log(index, "error", "A fifth nested call: the engine skips it and then runs the call's "
                                            "address as an instruction.");
                        break;
                    }
                    const std::int32_t to = operand();
                    if (to < static_cast<std::int32_t>(code.size()))
                    {
                        pushCallstack(cog);
                        cog.pc = to;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    void callVerb(Cog& cog, std::int32_t self, const std::string& name);
    [[nodiscard]] std::string messageName(std::int32_t id) const;
    float random()
    {
        randState ^= randState << 13;
        randState ^= randState >> 17;
        randState ^= randState << 5;
        return static_cast<float>(randState % 1000000u) / 1000000.0f;
    }
    void startMove(std::int32_t self, std::int32_t thingIndex, std::int32_t frame, float speed, bool skip);
    void damage(std::int32_t thingIndex, float amount, std::int32_t type, std::int32_t source);
    void sendLinkedWithParams(const std::string& message, std::int32_t kind, std::int32_t index, std::int32_t sourceType,
        std::int32_t sourceRef, const std::array<float, 4>& params);
    void tick(double dt);
    std::map<std::int32_t, bool> skipping;
};

namespace
{

using Impl = CogRuntime::Impl;
using Verb = std::function<void(Impl&, Cog&, std::int32_t)>;

std::array<float, 3> toFloat(const std::array<double, 3>& v)
{
    return { static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]) };
}

// Arguments are popped last first, so a verb reads them in reverse.
const std::unordered_map<std::string, Verb>& verbs()
{
    static const std::unordered_map<std::string, Verb> table = [] {
        std::unordered_map<std::string, Verb> t;
        const auto alias = [&t](const char* name, const char* existing) { t[name] = t.at(existing); };

        // --- the script's own state ---
        t["sleep"] = [](Impl& r, Cog& c, std::int32_t self) {
            float secs = r.popFlex(c);
            if (secs <= 0.0f) secs = 0.1f;
            c.running = Running::Sleeping;
            c.wake = r.now + secs;
            r.log(self, "verb", "Sleep(" + number(secs) + ")");
        };
        t["settimer"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float secs = r.popFlex(c);
            c.timerSet = secs > 0.0f;
            c.timerAt = r.now + secs;
            r.log(self, "verb", "SetTimer(" + number(secs) + ")");
        };
        t["settimerex"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float p1 = r.popFlex(c);
            const float p0 = r.popFlex(c);
            const std::int32_t id = r.popInt(c);
            const float delay = r.popFlex(c);
            c.timers.push_back(Timer{ r.now + delay, id, p0, p1 });
            r.log(self, "verb", "SetTimerEx(" + number(delay) + ", " + std::to_string(id) + ")");
        };
        t["killtimerex"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t id = r.popInt(c);
            std::erase_if(c.timers, [id](const Timer& timer) { return timer.id == id; });
            r.log(self, "verb", "KillTimerEx(" + std::to_string(id) + ")");
        };
        t["setpulse"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float secs = r.popFlex(c);
            c.pulseSet = secs > 0.0f;
            c.pulsePeriod = secs;
            c.pulseNext = r.now + secs;
            r.log(self, "verb", "SetPulse(" + number(secs) + ")");
        };
        t["getleveltime"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofFlex(static_cast<float>(r.now))); };
        t["getsenderref"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(c.senderRef)); };
        t["getsenderid"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(c.senderId)); };
        t["getsendertype"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(c.senderType)); };
        t["getsourceref"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(c.sourceRef)); };
        t["getsourcetype"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(c.sourceType)); };
        t["getparam"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t i = r.popInt(c);
            r.push(c, CogRuntimeValue::ofFlex(i >= 0 && i < 4 ? c.params[static_cast<std::size_t>(i)] : 0.0f));
        };
        t["setparam"] = [](Impl& r, Cog& c, std::int32_t) {
            const float value = r.popFlex(c);
            const std::int32_t i = r.popInt(c);
            if (i >= 0 && i < 4) c.params[static_cast<std::size_t>(i)] = value;
        };
        t["returnex"] = [](Impl& r, Cog& c, std::int32_t) { c.returnEx = r.popFlex(c); };
        t["getselfcog"] = [](Impl& r, Cog& c, std::int32_t self) { r.push(c, CogRuntimeValue::ofInt(self)); };
        t["sendmessage"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t message = r.popInt(c);
            const std::int32_t target = r.popIndex(c);
            r.log(self, "verb", "SendMessage(cog " + std::to_string(target) + ", " + r.messageName(message) + ")");
            r.deliver(target, r.messageName(message), 1, self, -1, 0, -1, {});
        };
        t["sendmessageex"] = [](Impl& r, Cog& c, std::int32_t self) {
            std::array<float, 4> p{};
            for (int k = 3; k >= 0; --k) p[static_cast<std::size_t>(k)] = r.popFlex(c);
            const std::int32_t message = r.popInt(c);
            const std::int32_t target = r.popIndex(c);
            r.log(self, "verb", "SendMessageEx(cog " + std::to_string(target) + ", " + r.messageName(message) + ")");
            const float result = r.deliver(target, r.messageName(message), 1, self, -1, 0, -1, p);
            r.push(c, CogRuntimeValue::ofFlex(result));
        };

        // --- printing ---
        t["print"] = [](Impl& r, Cog& c, std::int32_t self) { r.log(self, "print", r.popString(c)); };
        t["printint"] = [](Impl& r, Cog& c, std::int32_t self) { r.log(self, "print", std::to_string(r.popInt(c))); };
        t["printflex"] = [](Impl& r, Cog& c, std::int32_t self) { r.log(self, "print", number(r.popFlex(c))); };
        t["printvector"] = [](Impl& r, Cog& c, std::int32_t self) {
            std::array<float, 3> v{};
            r.popVector(c, v);
            r.log(self, "print", vectorText(v));
        };
        t["jkprintunistring"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t string = r.popInt(c);
            const std::int32_t to = r.popInt(c);
            r.log(self, "print", "cogstrings.uni #" + std::to_string(string) + " to " + std::to_string(to));
        };

        // --- numbers and vectors ---
        t["rand"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofFlex(r.random())); };
        t["randvec"] = [](Impl& r, Cog& c, std::int32_t) {
            r.push(c, CogRuntimeValue::ofVector({ r.random() - 0.5f, r.random() - 0.5f, r.random() - 0.5f }));
        };
        const auto vector2 = [](auto op) {
            return [op](Impl& r, Cog& c, std::int32_t) {
                std::array<float, 3> b{}, a{};
                r.popVector(c, b);
                r.popVector(c, a);
                op(r, c, a, b);
            };
        };
        t["vectoradd"] = vector2([](Impl& r, Cog& c, auto a, auto b) { r.push(c, CogRuntimeValue::ofVector({ a[0] + b[0], a[1] + b[1], a[2] + b[2] })); });
        t["vectorsub"] = vector2([](Impl& r, Cog& c, auto a, auto b) { r.push(c, CogRuntimeValue::ofVector({ a[0] - b[0], a[1] - b[1], a[2] - b[2] })); });
        t["vectordot"] = vector2([](Impl& r, Cog& c, auto a, auto b) { r.push(c, CogRuntimeValue::ofFlex(a[0] * b[0] + a[1] * b[1] + a[2] * b[2])); });
        t["vectorcross"] = vector2([](Impl& r, Cog& c, auto a, auto b) {
            r.push(c, CogRuntimeValue::ofVector({ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }));
        });
        t["vectordist"] = vector2([](Impl& r, Cog& c, auto a, auto b) {
            r.push(c, CogRuntimeValue::ofFlex(std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]))));
        });
        t["vectorlen"] = [](Impl& r, Cog& c, std::int32_t) {
            std::array<float, 3> a{};
            r.popVector(c, a);
            r.push(c, CogRuntimeValue::ofFlex(std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])));
        };
        t["vectornorm"] = [](Impl& r, Cog& c, std::int32_t) {
            std::array<float, 3> a{};
            r.popVector(c, a);
            const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
            r.push(c, CogRuntimeValue::ofVector(len > 0 ? std::array<float, 3>{ a[0] / len, a[1] / len, a[2] / len } : a));
        };
        t["vectorscale"] = [](Impl& r, Cog& c, std::int32_t) {
            const float s = r.popFlex(c);
            std::array<float, 3> a{};
            r.popVector(c, a);
            r.push(c, CogRuntimeValue::ofVector({ a[0] * s, a[1] * s, a[2] * s }));
        };
        t["vectorset"] = [](Impl& r, Cog& c, std::int32_t) {
            const float z = r.popFlex(c);
            const float y = r.popFlex(c);
            const float x = r.popFlex(c);
            r.push(c, CogRuntimeValue::ofVector({ x, y, z }));
        };
        for (int k = 0; k < 3; ++k)
        {
            t[std::string("vector") + "xyz"[k]] = [k](Impl& r, Cog& c, std::int32_t) {
                std::array<float, 3> a{};
                r.popVector(c, a);
                r.push(c, CogRuntimeValue::ofFlex(a[static_cast<std::size_t>(k)]));
            };
        }

        // --- things ---
        t["getlocalplayerthing"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(r.world.player)); };
        alias("jkgetlocalplayer", "getlocalplayerthing");
        t["getthingpos"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofVector(th ? toFloat(th->position) : std::array<float, 3>{}));
        };
        t["setthingpos"] = [](Impl& r, Cog& c, std::int32_t self) {
            std::array<float, 3> p{};
            r.popVector(c, p);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldThing* th = r.thing(i))
            {
                th->position = { p[0], p[1], p[2] };
                th->moving = false;
            }
            r.log(self, "verb", "SetThingPos(" + r.thingName(i) + ", " + vectorText(p) + ")");
            r.push(c, CogRuntimeValue::ofInt(1));
        };
        t["getthingsector"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th ? th->sector : -1));
        };
        t["movetoframe"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float speed = r.popFlex(c);
            const std::int32_t frame = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            r.log(self, "verb", "MoveToFrame(" + r.thingName(i) + ", " + std::to_string(frame) + ", " + number(speed) + ")");
            r.startMove(self, i, frame, speed, false);
        };
        t["skiptoframe"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float speed = r.popFlex(c);
            const std::int32_t frame = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            r.log(self, "verb", "SkipToFrame(" + r.thingName(i) + ", " + std::to_string(frame) + ", " + number(speed) + ")");
            r.startMove(self, i, frame, speed, true);
        };
        t["jumptoframe"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t sec = r.popIndex(c);
            const std::int32_t frame = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            r.log(self, "verb", "JumpToFrame(" + r.thingName(i) + ", " + std::to_string(frame) + ")");
            if (CogWorldThing* th = r.thing(i); th && frame >= 0 && static_cast<std::size_t>(frame) < th->frames.size())
            {
                th->position = th->frames[static_cast<std::size_t>(frame)];
                th->frame = th->goalFrame = frame;
                th->moving = false;
                if (sec >= 0) th->sector = sec;
            }
        };
        t["getcurframe"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th ? th->frame : 0));
        };
        t["getgoalframe"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th ? th->goalFrame : 0));
        };
        t["ismoving"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th && th->moving ? 1 : 0));
        };
        alias("isthingmoving", "ismoving");
        t["stopthing"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t i = r.popIndex(c);
            if (CogWorldThing* th = r.thing(i)) th->moving = false;
            r.log(self, "verb", "StopThing(" + r.thingName(i) + ")");
        };
        t["waitforstop"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t i = r.popIndex(c);
            const CogWorldThing* th = r.thing(i);
            r.log(self, "verb", "WaitForStop(" + r.thingName(i) + ")");
            if (th && th->moving)
            {
                c.running = Running::Waiting;
                c.waitThing = i;
            }
        };
        t["getthinghealth"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofFlex(th ? static_cast<float>(th->health) : 0.0f));
        };
        alias("gethealth", "getthinghealth");
        t["setthinghealth"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float h = r.popFlex(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldThing* th = r.thing(i)) th->health = h;
            r.log(self, "verb", "SetThingHealth(" + r.thingName(i) + ", " + number(h) + ")");
        };
        alias("sethealth", "setthinghealth");
        t["damagething"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t source = r.popIndex(c);
            const std::int32_t type = r.popInt(c);
            const float amount = r.popFlex(c);
            const std::int32_t i = r.popIndex(c);
            r.log(self, "verb", "DamageThing(" + r.thingName(i) + ", " + number(amount) + ", type " + std::to_string(type) + ")");
            r.push(c, CogRuntimeValue::ofFlex(amount));
            r.damage(i, amount, type, source);
        };
        t["destroything"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t i = r.popIndex(c);
            if (CogWorldThing* th = r.thing(i))
            {
                th->destroyed = true;
                th->moving = false;
            }
            r.log(self, "verb", "DestroyThing(" + r.thingName(i) + ")");
        };
        t["getthingflags"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th ? static_cast<std::int32_t>(th->flags) : 0));
        };
        const auto flagsVerb = [](const char* label, bool set) {
            return [label, set](Impl& r, Cog& c, std::int32_t self) {
                const auto flags = static_cast<std::uint32_t>(r.popInt(c));
                const std::int32_t i = r.popIndex(c);
                if (CogWorldThing* th = r.thing(i)) th->flags = set ? (th->flags | flags) : (th->flags & ~flags);
                std::ostringstream hex;
                hex << "0x" << std::hex << flags;
                r.log(self, "verb", std::string(label) + "(" + r.thingName(i) + ", " + hex.str() + ")");
            };
        };
        t["setthingflags"] = flagsVerb("SetThingFlags", true);
        t["clearthingflags"] = flagsVerb("ClearThingFlags", false);
        t["creatething"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t at = r.popIndex(c);
            const std::int32_t templ = r.popIndex(c);
            CogWorldThing made;
            made.templateName = r.resourceName(templ);
            if (const CogWorldThing* where = r.thing(at))
            {
                made.position = where->position;
                made.sector = where->sector;
            }
            r.world.things.push_back(made);
            const auto index = static_cast<std::int32_t>(r.world.things.size() - 1);
            r.log(self, "verb", "CreateThing(" + made.templateName + ", " + r.thingName(at) + ") made thing " + std::to_string(index));
            r.push(c, CogRuntimeValue::ofInt(index));
        };
        alias("createthingnr", "creatething");
        t["createthingatpos"] = [](Impl& r, Cog& c, std::int32_t self) {
            std::array<float, 3> rot{}, pos{};
            r.popVector(c, rot);
            r.popVector(c, pos);
            const std::int32_t sec = r.popIndex(c);
            const std::int32_t templ = r.popIndex(c);
            CogWorldThing made;
            made.templateName = r.resourceName(templ);
            made.position = { pos[0], pos[1], pos[2] };
            made.sector = sec;
            r.world.things.push_back(made);
            const auto index = static_cast<std::int32_t>(r.world.things.size() - 1);
            r.log(self, "verb", "CreateThingAtPos(" + made.templateName + ", sector " + std::to_string(sec) + ", "
                    + vectorText(pos) + ") made thing " + std::to_string(index));
            r.push(c, CogRuntimeValue::ofInt(index));
        };
        alias("createthingatposnr", "createthingatpos");

        // --- surfaces ---
        const auto surfaceRead = [](auto read) {
            return [read](Impl& r, Cog& c, std::int32_t) {
                const CogWorldSurface* s = r.surface(r.popIndex(c));
                read(r, c, s);
            };
        };
        t["getwallcel"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) { r.push(c, CogRuntimeValue::ofInt(s ? s->cel : -1)); });
        alias("getsurfacecel", "getwallcel");
        t["setwallcel"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t cel = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            CogWorldSurface* s = r.surface(i);
            const std::int32_t old = s ? s->cel : -1;
            if (s) s->cel = cel;
            r.log(self, "verb", "SetWallCel(surface " + std::to_string(i) + ", " + std::to_string(cel) + ")");
            r.push(c, CogRuntimeValue::ofInt(old));
        };
        alias("setsurfacecel", "setwallcel");
        t["getsurfaceflags"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) {
            r.push(c, CogRuntimeValue::ofInt(s ? static_cast<std::int32_t>(s->flags) : 0));
        });
        t["getadjoinflags"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) {
            r.push(c, CogRuntimeValue::ofInt(s ? static_cast<std::int32_t>(s->adjoinFlags) : 0));
        });
        t["getsurfacesector"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) { r.push(c, CogRuntimeValue::ofInt(s ? s->sector : -1)); });
        t["getsurfacecenter"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) {
            r.push(c, CogRuntimeValue::ofVector(s ? toFloat(s->center) : std::array<float, 3>{}));
        });
        alias("surfacecenter", "getsurfacecenter");
        t["getsurfacelight"] = surfaceRead([](Impl& r, Cog& c, const CogWorldSurface* s) {
            r.push(c, CogRuntimeValue::ofFlex(s ? static_cast<float>(s->light) : 0.0f));
        });
        const auto surfaceFlags = [](const char* label, bool adjoin, bool set) {
            return [label, adjoin, set](Impl& r, Cog& c, std::int32_t self) {
                const auto flags = static_cast<std::uint32_t>(r.popInt(c));
                const std::int32_t i = r.popIndex(c);
                if (CogWorldSurface* s = r.surface(i))
                {
                    std::uint32_t& f = adjoin ? s->adjoinFlags : s->flags;
                    f = set ? (f | flags) : (f & ~flags);
                }
                std::ostringstream hex;
                hex << "0x" << std::hex << flags;
                r.log(self, "verb", std::string(label) + "(surface " + std::to_string(i) + ", " + hex.str() + ")");
            };
        };
        t["setsurfaceflags"] = surfaceFlags("SetSurfaceFlags", false, true);
        t["clearsurfaceflags"] = surfaceFlags("ClearSurfaceFlags", false, false);
        t["setadjoinflags"] = surfaceFlags("SetAdjoinFlags", true, true);
        t["clearadjoinflags"] = surfaceFlags("ClearAdjoinFlags", true, false);
        t["setsurfacelight"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float delay = r.popFlex(c);
            const float light = r.popFlex(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldSurface* s = r.surface(i)) s->light = light;
            r.log(self, "verb", "SetSurfaceLight(surface " + std::to_string(i) + ", " + number(light) + ", " + number(delay) + ")");
        };
        alias("surfacelight", "setsurfacelight");

        // --- sectors ---
        t["setsectoradjoins"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t show = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldSector* s = r.sector(i)) s->adjoinsShown = show != 0;
            r.log(self, "verb", "SetSectorAdjoins(sector " + std::to_string(i) + ", " + std::to_string(show) + ")");
        };
        alias("sectoradjoins", "setsectoradjoins");
        t["setsectorlight"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float delay = r.popFlex(c);
            const float light = r.popFlex(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldSector* s = r.sector(i)) s->light = light;
            r.log(self, "verb", "SetSectorLight(sector " + std::to_string(i) + ", " + number(light) + ", " + number(delay) + ")");
        };
        alias("sectorlight", "setsectorlight");
        t["getsectorlight"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldSector* s = r.sector(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofFlex(s ? static_cast<float>(s->light) : 0.0f));
        };
        t["setsectortint"] = [](Impl& r, Cog& c, std::int32_t self) {
            std::array<float, 3> tint{};
            r.popVector(c, tint);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldSector* s = r.sector(i)) s->tint = { tint[0], tint[1], tint[2] };
            r.log(self, "verb", "SetSectorTint(sector " + std::to_string(i) + ", " + vectorText(tint) + ")");
        };
        t["setsectorcolormap"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t map = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldSector* s = r.sector(i)) s->colormap = map;
            r.log(self, "verb", "SetSectorColormap(sector " + std::to_string(i) + ", " + std::to_string(map) + ")");
        };
        alias("setcolormap", "setsectorcolormap");
        t["getsectorflags"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldSector* s = r.sector(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(s ? static_cast<std::int32_t>(s->flags) : 0));
        };
        const auto sectorFlags = [](const char* label, bool set) {
            return [label, set](Impl& r, Cog& c, std::int32_t self) {
                const auto flags = static_cast<std::uint32_t>(r.popInt(c));
                const std::int32_t i = r.popIndex(c);
                if (CogWorldSector* s = r.sector(i)) s->flags = set ? (s->flags | flags) : (s->flags & ~flags);
                std::ostringstream hex;
                hex << "0x" << std::hex << flags;
                r.log(self, "verb", std::string(label) + "(sector " + std::to_string(i) + ", " + hex.str() + ")");
            };
        };
        t["setsectorflags"] = sectorFlags("SetSectorFlags", true);
        t["clearsectorflags"] = sectorFlags("ClearSectorFlags", false);

        // --- sound ---
        const auto playSound = [](const char* label, int extra, bool atThing, bool atPos) {
            return [label, extra, atThing, atPos](Impl& r, Cog& c, std::int32_t self) {
                for (int k = 0; k < extra; ++k) (void)r.popFlex(c);
                std::string where;
                if (atThing) where = ", " + r.thingName(r.popIndex(c));
                if (atPos)
                {
                    std::array<float, 3> p{};
                    r.popVector(c, p);
                    where = ", " + vectorText(p);
                }
                const std::int32_t sound = r.popIndex(c);
                const std::int32_t channel = r.nextChannel++;
                r.log(self, "verb", std::string(label) + "(" + r.resourceName(sound) + where + ") on channel " + std::to_string(channel));
                r.push(c, CogRuntimeValue::ofInt(channel));
            };
        };
        t["playsoundthing"] = playSound("PlaySoundThing", 4, true, false);
        t["playsoundpos"] = playSound("PlaySoundPos", 4, false, true);
        t["playsoundlocal"] = playSound("PlaySoundLocal", 3, false, false);
        t["playsoundglobal"] = playSound("PlaySoundGlobal", 3, false, false);
        t["stopsound"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float delay = r.popFlex(c);
            const std::int32_t channel = r.popInt(c);
            r.log(self, "verb", "StopSound(channel " + std::to_string(channel) + ", " + number(delay) + ")");
        };

        // --- keyframes and surface animation: logged, with a handle back ---
        t["playkey"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t flags = r.popInt(c);
            const std::int32_t priority = r.popInt(c);
            const std::int32_t key = r.popIndex(c);
            const std::int32_t i = r.popIndex(c);
            const std::int32_t channel = r.nextChannel++;
            r.log(self, "verb", "PlayKey(" + r.thingName(i) + ", " + r.resourceName(key) + ", priority " + std::to_string(priority)
                + ", flags " + std::to_string(flags) + ") on track " + std::to_string(channel));
            r.push(c, CogRuntimeValue::ofInt(channel));
        };
        t["stopkey"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float delay = r.popFlex(c);
            const std::int32_t track = r.popInt(c);
            const std::int32_t i = r.popIndex(c);
            r.log(self, "verb", "StopKey(" + r.thingName(i) + ", track " + std::to_string(track) + ", " + number(delay) + ")");
        };
        t["surfaceanim"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t flags = r.popInt(c);
            const float fps = r.popFlex(c);
            const std::int32_t s = r.popIndex(c);
            const std::int32_t handle = r.nextChannel++;
            r.log(self, "verb", "SurfaceAnim(surface " + std::to_string(s) + ", " + number(fps) + " fps, flags " + std::to_string(flags)
                + ") as animation " + std::to_string(handle));
            r.push(c, CogRuntimeValue::ofInt(handle));
        };
        t["stopsurfaceanim"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t s = r.popIndex(c);
            r.log(self, "verb", "StopSurfaceAnim(surface " + std::to_string(s) + ")");
        };

        // --- the view and the player's controls ---
        t["getcurrentcamera"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(r.world.currentCamera)); };
        t["setcurrentcamera"] = [](Impl& r, Cog& c, std::int32_t self) {
            r.world.currentCamera = r.popInt(c);
            r.log(self, "verb", "SetCurrentCamera(" + std::to_string(r.world.currentCamera) + ")");
        };
        t["setcamerafocus"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t i = r.popIndex(c);
            const std::int32_t camera = r.popInt(c);
            if (camera == 0) r.world.cameraFocus = i;
            r.log(self, "verb", "SetCameraFocus(" + std::to_string(camera) + ", " + r.thingName(i) + ")");
        };
        const auto actorFlags = [](const bool set) {
            return [set](Impl& r, Cog& c, std::int32_t self) {
                const std::int32_t flags = r.popInt(c);
                const std::int32_t i = r.popIndex(c);
                if (CogWorldThing* th = r.thing(i))
                {
                    if (set) th->actorFlags |= static_cast<std::uint32_t>(flags);
                    else th->actorFlags &= ~static_cast<std::uint32_t>(flags);
                }
                std::ostringstream hex;
                hex << "0x" << std::hex << static_cast<std::uint32_t>(flags);
                r.log(self, "verb", std::string(set ? "SetActorFlags(" : "ClearActorFlags(") + r.thingName(i) + ", " + hex.str() + ")");
            };
        };
        t["setactorflags"] = actorFlags(true);
        t["clearactorflags"] = actorFlags(false);
        t["getactorflags"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofInt(th ? static_cast<std::int32_t>(th->actorFlags) : 0));
        };

        // --- small state ---
        t["bittest"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t b = r.popInt(c);
            const std::int32_t a = r.popInt(c);
            r.push(c, CogRuntimeValue::ofInt(a & b));
        };
        t["getdifficulty"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(r.world.difficulty)); };
        t["getmastercog"] = [](Impl& r, Cog& c, std::int32_t) { r.push(c, CogRuntimeValue::ofInt(r.masterCog)); };
        t["setmastercog"] = [](Impl& r, Cog& c, std::int32_t self) {
            r.masterCog = r.popIndex(c);
            r.log(self, "verb", "SetMasterCog(cog " + std::to_string(r.masterCog) + ")");
        };
        t["getthinglight"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofFlex(th ? static_cast<float>(th->light) : 0.0f));
        };
        t["setthinglight"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float delay = r.popFlex(c);
            const float light = r.popFlex(c);
            const std::int32_t i = r.popIndex(c);
            if (CogWorldThing* th = r.thing(i)) th->light = light;
            r.log(self, "verb", "SetThingLight(" + r.thingName(i) + ", " + number(light) + ", " + number(delay) + ")");
        };
        alias("thinglight", "setthinglight");
        t["getthinguserdata"] = [](Impl& r, Cog& c, std::int32_t) {
            const CogWorldThing* th = r.thing(r.popIndex(c));
            r.push(c, CogRuntimeValue::ofFlex(th ? static_cast<float>(th->userData) : 0.0f));
        };
        t["setthinguserdata"] = [](Impl& r, Cog& c, std::int32_t) {
            const float value = r.popFlex(c);
            if (CogWorldThing* th = r.thing(r.popIndex(c))) th->userData = value;
        };
        t["getsectorthingcount"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t s = r.popIndex(c);
            std::int32_t n = 0;
            for (const CogWorldThing& th : r.world.things) n += (!th.destroyed && th.sector == s) ? 1 : 0;
            r.push(c, CogRuntimeValue::ofInt(n));
        };
        t["getsectorplayercount"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t s = r.popIndex(c);
            const CogWorldThing* player = r.thing(r.world.player);
            r.push(c, CogRuntimeValue::ofInt(player && player->sector == s ? 1 : 0));
        };
        t["firstthinginsector"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t s = r.popIndex(c);
            std::int32_t found = -1;
            for (std::size_t i = 0; i < r.world.things.size() && found < 0; ++i)
            {
                if (!r.world.things[i].destroyed && r.world.things[i].sector == s) found = static_cast<std::int32_t>(i);
            }
            r.push(c, CogRuntimeValue::ofInt(found));
        };
        t["nextthinginsector"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t after = r.popIndex(c);
            const CogWorldThing* from = r.thing(after);
            std::int32_t found = -1;
            if (from)
            {
                for (std::size_t i = static_cast<std::size_t>(after) + 1; i < r.world.things.size() && found < 0; ++i)
                {
                    if (!r.world.things[i].destroyed && r.world.things[i].sector == from->sector) found = static_cast<std::int32_t>(i);
                }
            }
            r.push(c, CogRuntimeValue::ofInt(found));
        };

        // --- the player ---
        t["getinv"] = [](Impl& r, Cog& c, std::int32_t) {
            const std::int32_t bin = r.popInt(c);
            (void)r.popIndex(c);
            const auto it = r.world.inventory.find(bin);
            r.push(c, CogRuntimeValue::ofFlex(it == r.world.inventory.end() ? 0.0f : static_cast<float>(it->second)));
        };
        t["setinv"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float amount = r.popFlex(c);
            const std::int32_t bin = r.popInt(c);
            (void)r.popIndex(c);
            r.world.inventory[bin] = amount;
            r.log(self, "verb", "SetInv(bin " + std::to_string(bin) + ", " + number(amount) + ")");
        };
        t["changeinv"] = [](Impl& r, Cog& c, std::int32_t self) {
            const float amount = r.popFlex(c);
            const std::int32_t bin = r.popInt(c);
            (void)r.popIndex(c);
            r.world.inventory[bin] += amount;
            r.log(self, "verb", "ChangeInv(bin " + std::to_string(bin) + ", " + number(amount) + ")");
            r.push(c, CogRuntimeValue::ofFlex(static_cast<float>(r.world.inventory[bin])));
        };
        t["setgoalflags"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t flags = r.popInt(c);
            const std::int32_t goal = r.popInt(c);
            (void)r.popIndex(c);
            r.world.goals[goal] |= flags;
            r.log(self, "verb", "SetGoalFlags(goal " + std::to_string(goal) + ", " + std::to_string(flags) + ")");
        };
        t["cleargoalflags"] = [](Impl& r, Cog& c, std::int32_t self) {
            const std::int32_t flags = r.popInt(c);
            const std::int32_t goal = r.popInt(c);
            (void)r.popIndex(c);
            r.world.goals[goal] &= ~flags;
            r.log(self, "verb", "ClearGoalFlags(goal " + std::to_string(goal) + ", " + std::to_string(flags) + ")");
        };
        t["jkendlevel"] = [](Impl& r, Cog& c, std::int32_t self) {
            r.world.levelEndValue = r.popInt(c);
            r.world.levelEnded = true;
            r.log(self, "verb", "jkEndLevel(" + std::to_string(r.world.levelEndValue) + ")");
        };
        return t;
    }();
    return table;
}

} // namespace

std::string CogRuntime::Impl::messageName(const std::int32_t id) const
{
    for (const CogGlobalSymbol& g : globalSymbols)
    {
        if (g.kind == "message" && g.messageId == id) return g.name;
    }
    return "message " + std::to_string(id);
}

void CogRuntime::Impl::sendLinkedWithParams(const std::string& message, const std::int32_t kind, const std::int32_t index,
    const std::int32_t sourceType, const std::int32_t sourceRef, const std::array<float, 4>& params)
{
    for (std::size_t c = 0; c < cogs.size(); ++c)
    {
        const std::vector<Link> links = cogs[c]->links;   // a copy: a handler can change them
        for (const Link& link : links)
        {
            if (link.kind == kind && link.index == index)
            {
                deliver(static_cast<std::int32_t>(c), message, kind, index, link.linkId, sourceType, sourceRef, params);
                break;
            }
        }
    }
}

void CogRuntime::Impl::callVerb(Cog& cog, const std::int32_t self, const std::string& name)
{
    pushedByVerb = 0;
    const std::optional<CogVerbInfo> sig = cogVerbSignature(name);
    const auto& table = verbs();
    if (const auto it = table.find(name); it != table.end())
    {
        it->second(*this, cog, self);
    }
    else
    {
        std::vector<std::string> args;
        if (sig && !(sig->args.size() == 1 && sig->args[0] == "?"))
        {
            for (std::size_t k = 0; k < sig->args.size(); ++k)
            {
                const auto v = pop(cog);
                args.insert(args.begin(), v ? v->describe() : "(empty)");
            }
        }
        std::string list;
        for (const std::string& a : args) list += (list.empty() ? "" : ", ") + a;
        log(self, "unsimulated", name + "(" + list + ") isn't simulated"
                + std::string(sig && !sig->result.empty() ? "; it returns 0 here." : "."));
    }
    if (sig && !sig->result.empty() && pushedByVerb == 0)
    {
        if (sig->result == "vector") push(cog, CogRuntimeValue::ofVector({}));
        else if (sig->result == "flex") push(cog, CogRuntimeValue::ofFlex(0.0f));
        else push(cog, CogRuntimeValue::ofInt(0));
    }
}

void CogRuntime::Impl::startMove(const std::int32_t self, const std::int32_t thingIndex, std::int32_t frame,
    const float speed, const bool skip)
{
    CogWorldThing* th = thing(thingIndex);
    if (!th) return;
    if (th->frames.empty())
    {
        log(self, "error", thingName(thingIndex) + " has no path frames, so it can't move.");
        return;
    }
    frame = std::clamp(frame, 0, static_cast<std::int32_t>(th->frames.size()) - 1);
    if (speed <= 0.0f)
    {
        log(self, "note", thingName(thingIndex) + " was asked to move at speed " + number(speed) + "; it stays put.");
        return;
    }
    th->goalFrame = frame;
    th->speed = speed;
    th->moving = !(frame == th->frame && th->position == th->frames[static_cast<std::size_t>(frame)]);
    skipping[thingIndex] = skip;
}

void CogRuntime::Impl::damage(const std::int32_t thingIndex, const float amount, const std::int32_t type, const std::int32_t source)
{
    CogWorldThing* th = thing(thingIndex);
    if (!th || th->destroyed) return;
    th->health -= amount;
    sendLinkedWithParams("damaged", 3, thingIndex, 3, source, { amount, static_cast<float>(type), 0.0f, 0.0f });
    if (th->health <= 0.0 && !th->destroyed)
    {
        th->destroyed = true;
        th->moving = false;
        sendLinked("killed", 3, thingIndex, 3, source);
    }
}

void CogRuntime::Impl::tick(const double dt)
{
    // Movement along path frames, at the speed the script asked (JKU a second).
    std::vector<std::int32_t> arrivals;
    for (std::size_t i = 0; i < world.things.size(); ++i)
    {
        CogWorldThing& th = world.things[i];
        if (!th.moving) continue;
        double travel = th.speed * dt;
        while (th.moving && travel > 0.0)
        {
            const bool skip = skipping[static_cast<std::int32_t>(i)];
            const std::int32_t next = skip || th.goalFrame == th.frame ? th.goalFrame
                                                                      : th.frame + (th.goalFrame > th.frame ? 1 : -1);
            const auto& to = th.frames[static_cast<std::size_t>(next)];
            const double dx = to[0] - th.position[0], dy = to[1] - th.position[1], dz = to[2] - th.position[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance <= travel)
            {
                th.position = to;
                th.frame = next;
                travel -= distance;
                if (th.frame == th.goalFrame)
                {
                    th.moving = false;
                    arrivals.push_back(static_cast<std::int32_t>(i));
                }
            }
            else
            {
                const double k = travel / distance;
                th.position = { th.position[0] + dx * k, th.position[1] + dy * k, th.position[2] + dz * k };
                travel = 0.0;
            }
        }
    }
    now += dt;
    for (const std::int32_t i : arrivals)
    {
        log(-1, "arrived", thingName(i) + " reached frame " + std::to_string(world.things[static_cast<std::size_t>(i)].frame));
        sendLinked("arrived", 3, i, 3, i);
    }
    for (std::size_t c = 0; c < cogs.size(); ++c)
    {
        Cog& cog = *cogs[c];
        const auto index = static_cast<std::int32_t>(c);
        if (cog.running == Running::Sleeping && now + 1e-9 >= cog.wake)
        {
            cog.running = Running::Running;
            execute(index);
        }
        else if (cog.running == Running::Waiting)
        {
            const CogWorldThing* th = thing(cog.waitThing);
            if (!th || !th->moving)
            {
                cog.running = Running::Running;
                execute(index);
            }
        }
        if (cog.timerSet && now + 1e-9 >= cog.timerAt)
        {
            cog.timerSet = false;
            deliver(index, "timer", 0, -1, 0, 0, -1, {});
        }
        for (std::size_t k = 0; k < cog.timers.size();)
        {
            if (now + 1e-9 >= cog.timers[k].at)
            {
                const Timer timer = cog.timers[k];
                cog.timers.erase(cog.timers.begin() + static_cast<std::ptrdiff_t>(k));
                deliver(index, "timer", 0, -1, timer.id, 0, -1, { timer.param0, timer.param1, 0.0f, 0.0f });
            }
            else
            {
                ++k;
            }
        }
        if (cog.pulseSet && now + 1e-9 >= cog.pulseNext)
        {
            cog.pulseNext += cog.pulsePeriod;
            deliver(index, "pulse", 0, -1, 0, 0, -1, {});
        }
    }
}

// --- the public interface ---

CogRuntime::CogRuntime(CogWorld world, const CogPreflightTarget target, const CogRuntimeOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->world = std::move(world);
    impl_->target = target;
    impl_->options = options;
    impl_->globalSymbols = cogGlobalSymbols(target);
    for (std::size_t i = 0; i < impl_->globalSymbols.size(); ++i)
    {
        const CogGlobalSymbol& g = impl_->globalSymbols[i];
        CogRuntimeValue v;
        if (g.kind == "verb")
        {
            v.type = CogRuntimeValue::Type::Verb;
            v.text = g.name;
        }
        else if (g.kind == "message")
            v = CogRuntimeValue::ofInt(g.messageId);
        else
            v = CogRuntimeValue::ofFlex(0.0f);
        impl_->globals.push_back(v);
        impl_->globalIndex.emplace(g.name, static_cast<std::int32_t>(i));
    }
}

CogRuntime::~CogRuntime() = default;

std::int32_t CogRuntime::addCog(const CogCompileResult& compiled, const std::string& name,
    const std::vector<CogRuntimeBinding>& bindings, std::string& error)
{
    if (!compiled.loads)
    {
        error = name + " doesn't load in the game, so it can't run.";
        return -1;
    }
    auto cog = std::make_unique<Cog>();
    cog->name = name;
    cog->compiled = compiled;
    static const std::map<std::string, std::int32_t> linkKinds{ { "thing", 3 }, { "sector", 5 }, { "surface", 6 } };
    for (const CogCompiledSymbol& s : compiled.symbols)
    {
        CogRuntimeValue v;
        if (s.kind == "int") v = CogRuntimeValue::ofInt(static_cast<std::int32_t>(std::strtol(s.value.c_str(), nullptr, 10)));
        else if (s.kind == "flex") v = CogRuntimeValue::ofFlex(std::strtof(s.value.c_str(), nullptr));
        else if (s.kind == "vector") v = CogRuntimeValue::ofVector({});
        else if (s.kind == "string")
        {
            v.type = CogRuntimeValue::Type::String;
            v.text = s.value;
        }
        else if (s.kind == "message")
        {
            const auto it = impl_->globalIndex.find(s.value);
            v = CogRuntimeValue::ofInt(it == impl_->globalIndex.end() ? 0 : impl_->globalSymbols[static_cast<std::size_t>(it->second)].messageId);
        }
        else if (linkKinds.count(s.kind))
            v = CogRuntimeValue::ofInt(-1);   // unbound; the engine would read 0
        else if (s.kind != "implicit")
            v = CogRuntimeValue::ofInt(s.value.empty() ? -1 : impl_->resourceHandle(s.value));
        cog->symbols.push_back(v);
        cog->symbolKinds.push_back(s.kind);
    }
    for (const CogRuntimeBinding& b : bindings)
    {
        bool found = false;
        for (std::size_t i = 0; i < compiled.symbols.size(); ++i)
        {
            if (compiled.symbols[i].name != b.symbol) continue;
            found = true;
            CogRuntimeValue value = b.value;
            const std::string& kind = compiled.symbols[i].kind;
            // A placed COG's values are text: numbers for int, flex and
            // object symbols, "(x/y/z)" for vectors, names for resources.
            const bool text = value.type == CogRuntimeValue::Type::String;
            if (kind == "flex")
                value = CogRuntimeValue::ofFlex(text ? std::strtof(value.text.c_str(), nullptr)
                                                     : value.type == CogRuntimeValue::Type::Flex ? value.f : static_cast<float>(value.i));
            else if (kind == "int" || linkKinds.count(kind))
            {
                if (text) value = CogRuntimeValue::ofInt(static_cast<std::int32_t>(std::strtol(value.text.c_str(), nullptr, 10)));
                else if (value.type == CogRuntimeValue::Type::Flex) value = CogRuntimeValue::ofInt(static_cast<std::int32_t>(value.f));
            }
            else if (kind == "vector" && text)
            {
                std::array<float, 3> v{};
                std::string digits = value.text;
                for (char& ch : digits)
                {
                    if (ch == '(' || ch == ')' || ch == '/') ch = ' ';
                }
                std::istringstream in(digits);
                in >> v[0] >> v[1] >> v[2];
                value = CogRuntimeValue::ofVector(v);
            }
            else if (text && kind != "string")
                value = CogRuntimeValue::ofInt(impl_->resourceHandle(value.text));
            cog->symbols[i] = value;
            break;
        }
        if (!found)
        {
            error = name + " has no symbol '" + b.symbol + "' to bind.";
            return -1;
        }
    }
    for (std::size_t i = 0; i < compiled.symbols.size(); ++i)
    {
        const CogCompiledSymbol& s = compiled.symbols[i];
        const auto kind = linkKinds.find(s.kind);
        if (kind == linkKinds.end() || s.local || s.noLink) continue;
        const CogRuntimeValue& v = cog->symbols[i];
        if (v.type == CogRuntimeValue::Type::Int && v.i >= 0) cog->links.push_back(Link{ kind->second, v.i, s.linkId });
    }
    impl_->cogs.push_back(std::move(cog));
    return static_cast<std::int32_t>(impl_->cogs.size() - 1);
}

void CogRuntime::startup()
{
    for (std::size_t c = 0; c < impl_->cogs.size(); ++c)
    {
        impl_->deliver(static_cast<std::int32_t>(c), "startup", 0, -1, -1, 0, -1, {});
    }
}

float CogRuntime::sendMessage(const std::int32_t cog, const std::string& message, const std::int32_t senderType,
    const std::int32_t senderRef, const std::int32_t sourceType, const std::int32_t sourceRef, const std::array<float, 4>& params)
{
    return impl_->deliver(cog, message, senderType, senderRef, -1, sourceType, sourceRef, params);
}

void CogRuntime::sendLinked(const std::string& message, const std::int32_t kind, const std::int32_t index,
    const std::int32_t sourceType, const std::int32_t sourceRef)
{
    impl_->sendLinked(message, kind, index, sourceType, sourceRef);
}

void CogRuntime::damageThing(const std::int32_t thing, const float amount, const std::int32_t type, const std::int32_t sourceRef)
{
    impl_->damage(thing, amount, type, sourceRef);
}

void CogRuntime::advance(const double seconds, const double step)
{
    const double dt = step > 0.0 ? step : 0.01;
    const auto steps = static_cast<std::int64_t>(std::llround(seconds / dt));
    for (std::int64_t i = 0; i < steps; ++i) impl_->tick(dt);
}

double CogRuntime::time() const noexcept { return impl_->now; }
const CogWorld& CogRuntime::world() const noexcept { return impl_->world; }
const std::vector<CogMessageBreakpoint>& CogRuntime::breakpoints() const noexcept { return impl_->breakpoints; }
const std::vector<CogRuntimeEvent>& CogRuntime::events() const noexcept { return impl_->events; }
std::size_t CogRuntime::cogCount() const noexcept { return impl_->cogs.size(); }

std::optional<CogRuntimeValue> CogRuntime::symbolValue(const std::int32_t cog, const std::string& symbol) const
{
    if (cog < 0 || static_cast<std::size_t>(cog) >= impl_->cogs.size()) return std::nullopt;
    const Cog& c = *impl_->cogs[static_cast<std::size_t>(cog)];
    for (std::size_t i = 0; i < c.compiled.symbols.size(); ++i)
    {
        if (c.compiled.symbols[i].name == symbol) return c.symbols[i];
    }
    return std::nullopt;
}

const std::vector<std::string>& cogSimulatedVerbs()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const auto& entry : verbs()) out.push_back(entry.first);
        std::sort(out.begin(), out.end());
        return out;
    }();
    return names;
}

CogWorld cogWorldFromLevel(const Level& level)
{
    CogWorld world;
    for (const Thing& t : level.things)
    {
        CogWorldThing th;
        th.name = t.name;
        th.templateName = t.templateName;
        th.position = { t.x, t.y, t.z };
        th.sector = t.sector;
        for (const TplValue& value : t.values)
        {
            std::string key = value.name;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (key != "frame") continue;
            // (x/y/z:pitch/yaw/roll)
            const std::string text = value.stringValue.empty() ? value.asString() : value.stringValue;
            std::array<double, 3> p{};
            int read = 0;
            const char* s = text.c_str();
            while (*s && read < 3)
            {
                char* end = nullptr;
                const double d = std::strtod(s, &end);
                if (end != s)
                {
                    p[static_cast<std::size_t>(read++)] = d;
                    s = end;
                }
                else
                {
                    ++s;
                }
            }
            if (read == 3) th.frames.push_back(p);
        }
        if (world.player < 0)
        {
            std::string templ = t.templateName;
            std::transform(templ.begin(), templ.end(), templ.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (templ.find("player") != std::string::npos) world.player = static_cast<std::int32_t>(world.things.size());
        }
        world.things.push_back(std::move(th));
    }
    for (std::size_t s = 0; s < level.sectors.size(); ++s)
    {
        const Sector& sector = level.sectors[s];
        CogWorldSector ws;
        ws.light = sector.extraLight.r;
        ws.flags = static_cast<std::uint32_t>(sector.flags);
        ws.colormap = sector.colormapIndex;
        ws.tint = { sector.tint.r, sector.tint.g, sector.tint.b };
        world.sectors.push_back(ws);
        for (std::size_t j = 0; j < sector.surfaces.size(); ++j)
        {
            const Surface& surface = sector.surfaces[j];
            CogWorldSurface w;
            w.sector = static_cast<std::int32_t>(s);
            w.surface = static_cast<std::int32_t>(j);
            w.flags = static_cast<std::uint32_t>(surface.surfaceFlags);
            w.adjoinFlags = static_cast<std::uint32_t>(surface.adjoinFlags);
            w.light = surface.extraLight.r;
            std::array<double, 3> sum{};
            std::size_t count = 0;
            for (const std::int32_t v : surface.vertices)
            {
                if (v < 0 || static_cast<std::size_t>(v) >= sector.vertices.size()) continue;
                const Vertex& vertex = sector.vertices[static_cast<std::size_t>(v)];
                sum[0] += vertex.x;
                sum[1] += vertex.y;
                sum[2] += vertex.z;
                ++count;
            }
            if (count) w.center = { sum[0] / count, sum[1] / count, sum[2] / count };
            world.surfaces.push_back(w);
        }
    }
    return world;
}

} // namespace sed::assets
