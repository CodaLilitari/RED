#include "agent_workspace.hpp"
#include "agent_schema.hpp"

#include <iostream>

namespace
{

sed_agent::Json helpJson()
{
    sed_agent::Json out = sed_agent::Json::makeObject();
    out.set("ok", sed_agent::Json::makeBool(true));
    out.set("protocol", sed_agent::Json::makeString("sed-agent"));
    out.set("schemaVersion", sed_agent::Json::makeNumber(1));
    out.set("note", sed_agent::Json::makeString(
        "This is a tool-neutral CLI and does not claim to implement MCP."));
    sed_agent::Json usage = sed_agent::Json::makeArray();
    for ( const auto& command : sed_agent::agentCommandDescriptors() )
    {
        usage.array.push_back(sed_agent::Json::makeString(std::string(command.usage)));
    }
    out.set("usage", std::move(usage));
    sed_agent::Json exits = sed_agent::Json::makeObject();
    exits.set("0", sed_agent::Json::makeString("ok"));
    exits.set("2", sed_agent::Json::makeString("usage / invalid request"));
    exits.set("3", sed_agent::Json::makeString("input error"));
    exits.set("4", sed_agent::Json::makeString("stale input or conflict"));
    exits.set("5", sed_agent::Json::makeString("refusal (existing output or unsafe target)"));
    out.set("exitCodes", std::move(exits));
    return out;
}

void printErrorEnvelope(int code, const std::string& message, const std::string& errorCode);

void printError(int code, const std::string& message, const std::string& errorCode,
    const sed_agent::Json* payload)
{
    if (payload)
    {
        std::cout << sed_agent::toJsonString(*payload) << "\n";
        return;
    }
    printErrorEnvelope(code, message, errorCode);
}

void printErrorEnvelope(int code, const std::string& message, const std::string& errorCode = {})
{
    sed_agent::Json err = sed_agent::Json::makeObject();
    err.set("ok", sed_agent::Json::makeBool(false));
    err.set("exitCode", sed_agent::Json::makeNumber(static_cast<double>(code)));
    err.set("error", sed_agent::Json::makeString(message));
    if (!errorCode.empty())
    {
        err.set("code", sed_agent::Json::makeString(errorCode));
        err.set("errorCode", sed_agent::Json::makeString(errorCode));
    }
    std::cout << sed_agent::toJsonString(err) << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
        {
            std::cout << sed_agent::toJsonString(helpJson()) << "\n";
            return sed_agent::kExitOk;
        }
        const std::string command = argv[1];
        if (command == "help" || command == "--help" || command == "-h")
        {
            std::cout << sed_agent::toJsonString(helpJson()) << "\n";
            return sed_agent::kExitOk;
        }
        std::vector<std::string> args(argv + 2, argv + argc);
        const auto result = sed_agent::runCommand(command, args);
        std::cout << sed_agent::toJsonString(result) << "\n";
        return sed_agent::kExitOk;
    }
    catch (const sed_agent::AgentError& e)
    {
        printError(e.code, e.message, e.errorCode,
            e.hasPayload ? &e.payload : nullptr);
        return e.code;
    }
    catch (const std::exception& e)
    {
        printErrorEnvelope(sed_agent::kExitInput, e.what());
        return sed_agent::kExitInput;
    }
}
