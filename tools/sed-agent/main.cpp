#include "agent_workspace.hpp"

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
    auto add = [&usage](const char* s) { usage.array.push_back(sed_agent::Json::makeString(s)); };
    add("sed-agent help");
    add("sed-agent schema");
    add("sed-agent inspect <level.jkl|level.sed>");
    add("sed-agent query <level> <query.json|->");
    add("sed-agent select <level> <select.json|->");
    add("sed-agent validate <level> [validate.json|-]");
    add("sed-agent plan <level> <patch.json|->");
    add("sed-agent apply <level> <patch.json|-> <new-output> [expected-output-sha256]");
    add("sed-agent new <new-output.jkl|new-output.sed> [mots|jk|new.json|-]");
    add("sed-agent convert <input> <new-output> [expected-output-sha256]");
    add("sed-agent archive <archive.gob|archive.goo>");
    add("sed-agent extract <archive> <exact-entry-name> <new-output>");
    add("sed-agent launch-plan <launch-request.json|->");
    add("sed-agent snapshot <level> <snapshot.json|-> <new-output.png> (refused: snapshotRequiresRenderer)");
    add("sed-agent bindings <level> <bindings.json|->");
    add("sed-agent addCog <level> <addCog.json|-> [new-output [expected-output-sha256]]");
    add("sed-agent rebindCogReference <level> <rebind.json|-> [new-output [expected-output-sha256]]");
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

void printError(int code, const std::string& message, const std::string& errorCode = {})
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
        printError(e.code, e.message, e.errorCode);
        return e.code;
    }
    catch (const std::exception& e)
    {
        printError(sed_agent::kExitInput, e.what());
        return sed_agent::kExitInput;
    }
}
