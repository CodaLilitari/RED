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
    add("sed-agent validate <level> [validate.json|-] [--strict]");
    add("sed-agent plan <level> <patch.json|->");
    add("sed-agent apply <level> <patch.json|-> <new-output> [expected-output-sha256]");
    add("sed-agent new <new-output.jkl|new-output.sed> [mots|jk|droidworks|new.json|-]");
    add("sed-agent convert <input> <new-output> [expected-output-sha256]");
    add("sed-agent archive <archive.gob|archive.goo>");
    add("sed-agent extract <archive> <exact-entry-name> <new-output>");
    add("sed-agent launch-plan <launch-request.json|->");
    add("sed-agent snapshot <level> <snapshot.json|-> <new-output.png> (software-v1; world surfaces only)");
    add("sed-agent bindings <level> <bindings.json|->");
    add("sed-agent addCog <level> <addCog.json|-> [new-output [expected-output-sha256]]");
    add("sed-agent rebindCogReference <level> <rebind.json|-> [new-output [expected-output-sha256]]");
    add("sed-agent package-plan <level.jkl> <package.json|->");
    add("sed-agent package-apply <level.jkl> <package.json|-> <new-output.gob|.goo> <expected-output-sha256> <expected-manifest-sha256>");
    add("sed-agent native-inspect <input> <jk|mots|ijim|droidworks> <jkl|native-sed> <worker-config.json>");
    add("sed-agent overview <level> <overview.json|-> (software-v1 top/three-quarter view; world surfaces only)");
    add("sed-agent workbench-query <level> <query.json|->");
    add("sed-agent arch-validate <level> <request.json|->");
    add("sed-agent statistics <level>");
    add("sed-agent prefab-capture <level> <request.json|-> <new-output.prefab>");
    add("sed-agent export-dxf <level> <new-output.dxf>");
    add("sed-agent dxf-inspect <file.dxf>");
    add("sed-agent dxf-import <input.dxf> <height> <new-output.jkl>");
    add("sed-agent cog-check <file.cog> <jk|mots|ijim> [--strict]");
    add("sed-agent mat-inspect <file.mat>");
    add("sed-agent mat-export-png <file.mat> <new-output.png> [cel] [palette.cmp]");
    add("sed-agent mat-create <request.json|-> <new-output.mat> (PNG cels + CMP palette -> 8-bit MAT with mipmaps)");
    add("sed-agent archive-create <request.json|-> <new-output.gob|.goo> (loose resources -> GOB, read back by type)");
    add("sed-agent terrain-3do <request.json|-> <new-output.3do>");
    add("sed-agent key-inspect <file.key> [file.3do]");
    add("sed-agent key-plan <in.key> <request.json|->");
    add("sed-agent key-apply <in.key> <request.json|-> <expected-output-sha256> <new-output.key>");
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
