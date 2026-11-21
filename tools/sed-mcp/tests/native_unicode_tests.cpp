#include "agent_json.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;

void require(bool condition, const std::string& message)
{
    if ( !condition ) throw std::runtime_error(message);
}

std::string utf8(const fs::path& path)
{
    const auto bytes = path.u8string();
    return { bytes.begin(), bytes.end() };
}

std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(file.is_open(), "Cannot read " + utf8(path));
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

void writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream file;
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file.open(path, std::ios::binary);
    file << bytes;
    file.close();
}

struct Scratch
{
    fs::path root = fs::temp_directory_path() / (L"red-native-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64()));

    Scratch() { require(fs::create_directory(root), "Cannot reserve scratch directory"); }
    ~Scratch()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

// All arguments here are filenames or fixed tokens. Windows filenames cannot contain
// double quotes. Double trailing backslashes before the closing quote for the CRT.
std::wstring quote(const std::wstring& arg)
{
    require(arg.find(L'"') == std::wstring::npos, "Unexpected quote in argument");
    const auto last = arg.find_last_not_of(L'\\');
    const auto trailing = last == std::wstring::npos ? arg.size() : arg.size() - last - 1;
    return L"\"" + arg + std::wstring(trailing, L'\\') + L"\"";
}

void checkWindows(BOOL ok, const char* operation)
{
    if ( !ok ) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

Handle redirect(const fs::path& path, bool input)
{
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    const auto raw = CreateFileW(path.c_str(), input ? GENERIC_READ : GENERIC_WRITE,
        FILE_SHARE_READ, &security, input ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    checkWindows(raw != INVALID_HANDLE_VALUE, "CreateFileW");
    return Handle(raw, &CloseHandle);
}

Json run(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& scratch,
    const std::string& stdinBytes = {}, DWORD expectedExit = 0)
{
    const auto in = scratch / "stdin", out = scratch / "stdout", err = scratch / "stderr";
    writeFile(in, stdinBytes);
    std::wstring command = quote(exe.native());
    for ( const auto& arg : args ) command += L" " + quote(arg);
    DWORD exitCode{};
    {
        auto input = redirect(in, true), output = redirect(out, false), errors = redirect(err, false);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.get();
        startup.hStdOutput = output.get();
        startup.hStdError = errors.get();
        PROCESS_INFORMATION child{};
        checkWindows(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr,
            scratch.c_str(), &startup, &child), "CreateProcessW");
        Handle process(child.hProcess, &CloseHandle), thread(child.hThread, &CloseHandle);
        checkWindows(WaitForSingleObject(process.get(), INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject");
        checkWindows(GetExitCodeProcess(process.get(), &exitCode), "GetExitCodeProcess");
    }
    const auto stdoutBytes = readFile(out);
    require(exitCode == expectedExit, utf8(exe.filename()) + " exited " + std::to_string(exitCode)
        + ": " + stdoutBytes + readFile(err));
    require(sed_agent::isValidJsonUtf8(stdoutBytes), utf8(exe.filename()) + " returned invalid UTF-8 stdout");
    return sed_agent::parseJson(stdoutBytes);
}

Json request(const std::string& source)
{
    auto edit = sed_agent::parseJson(R"({"schemaVersion":1,"profile":"classic2.1","operations":[
        {"op":"setTextureVertex","geoset":0,"sourceMesh":0,"textureVertex":1,"u":-8.5,"v":32}]})");
    edit.set("inputSha256", Json::makeString(sed_agent::sha256Hex(source)));
    return edit;
}

void verifyPublication(const Json& receipt, const fs::path& output, const Json& plan)
{
    require(receipt.object.at("written").boolean, "Apply reported no write");
    const auto& reported = receipt.object.at("output").str;
    const fs::path reportedPath(std::u8string(reported.begin(), reported.end()));
    require(reportedPath == output, "Incorrect UTF-8 output in receipt: " + reported);
    // Enumerate native names so a successful receipt for a mis-encoded file names both sides of the failure.
    std::string actual;
    bool found = false;
    for ( const auto& entry : fs::directory_iterator(output.parent_path()) )
    {
        const auto name = entry.path().filename();
        actual += " [" + utf8(name) + "]";
        if ( name.native() == output.filename().native() ) found = true;
    }
    require(found, "Apply reported " + utf8(output) + " but actual directory entries were:" + actual);
    const auto hash = sed_agent::sha256Hex(readFile(output));
    require(hash == plan.object.at("outputSha256").str && hash == receipt.object.at("outputSha256").str,
        "Published bytes differ from reviewed output");
}

void cli(const fs::path& exe, const fs::path& scratch, const fs::path& source, const fs::path& output,
    const std::string& original)
{
    const auto query = scratch / L"caf\u00e9-\u0219\u6f22 query.json";
    const auto edit = scratch / L"caf\u00e9-\u0219\u6f22 request.json";
    writeFile(query, R"({"collection":"meshes","limit":1})");
    writeFile(edit, sed_agent::toJsonString(request(original)));
    const auto inspect = run(exe, { L"model-native-inspect", source.native(), query.native() }, scratch);
    require(inspect.object.at("inputSha256").str == sed_agent::sha256Hex(original)
        && inspect.object.at("items").array.size() == 1, "CLI inspected the wrong source or query");
    const auto plan = run(exe, { L"model-native-plan", source.native(), edit.native() }, scratch);
    const std::vector<std::wstring> apply{ L"model-native-apply", source.native(), edit.native(),
        fs::path(plan.object.at("requestSha256").str).native(), fs::path(plan.object.at("outputSha256").str).native(),
        output.native() };
    verifyPublication(run(exe, apply, scratch), output, plan);
    writeFile(output, "keep this destination");
    run(exe, apply, scratch, {}, 5);
    require(readFile(output) == "keep this destination", "CLI overwrote existing output");
}

Json callTool(const fs::path& exe, const std::vector<std::wstring>& flags, const fs::path& scratch,
    const char* name, const Json& args, bool expectError = false)
{
    const auto line = std::string(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")")
        + name + R"(","arguments":)" + sed_agent::toJsonString(args) + "}}\n";
    const auto reply = run(exe, flags, scratch, line);
    const auto& result = reply.object.at("result");
    const auto payload = result.object.at("content").array.at(0).object.at("text").str;
    require(result.object.at("isError").boolean == expectError, std::string(name) + ": " + payload);
    return sed_agent::parseJson(payload);
}

void mcp(const fs::path& exe, const fs::path& scratch, const fs::path& workspace, const fs::path& source,
    const fs::path& output, const std::string& original, bool resourceRoot)
{
    std::vector<std::wstring> flags{ L"--workspace", workspace.native() };
    if ( resourceRoot ) flags.insert(flags.end(), { L"--resource-root", source.parent_path().native() });

    auto args = Json::makeObject();
    args.set("path", Json::makeString(utf8(resourceRoot ? source : source.filename())));
    auto inspectArgs = args;
    inspectArgs.set("query", sed_agent::parseJson(R"({"collection":"meshes","limit":1})"));
    const auto inspect = callTool(exe, flags, scratch, "sed_model_native_inspect", inspectArgs);
    require(inspect.object.at("inputSha256").str == sed_agent::sha256Hex(original)
        && inspect.object.at("items").array.size() == 1, "MCP inspected the wrong source or query");
    args.set("request", request(original));
    const auto plan = callTool(exe, flags, scratch, "sed_model_native_plan", args);
    args.set("expectedRequestSha256", plan.object.at("requestSha256"));
    args.set("expectedOutputSha256", plan.object.at("outputSha256"));
    args.set("output", Json::makeString(utf8(output.filename())));
    verifyPublication(callTool(exe, flags, scratch, "sed_model_native_apply", args), output, plan);
    writeFile(output, "keep this destination");
    const auto refused = callTool(exe, flags, scratch, "sed_model_native_apply", args, true);
    require(refused.object.at("exitCode").number == 5 && readFile(output) == "keep this destination",
        "MCP failed to preserve existing output");
}

void levelInspect(bool useMcp, bool policy, const fs::path& agent, const fs::path& server, const fs::path& scratch)
{
    // Distinct native entries ensure that reading a plausible but mis-decoded path cannot pass.
    const fs::path workspace = scratch / L"work";
    const fs::path resources = scratch / L"resources \u6f22\U0001f319";
    fs::create_directory(workspace);
    fs::create_directory(resources);
    run(agent, { L"new", L"seed-jk.jkl", L"jk" }, scratch);
    run(agent, { L"new", L"seed-mots.jkl", L"mots" }, scratch);
    const auto intended = readFile(scratch / L"seed-jk.jkl");
    const auto other = readFile(scratch / L"seed-mots.jkl");
    require(intended != other, "Scaffold controls must have different bytes");
    const auto root = policy ? resources : workspace;
    writeFile(root / L"caf\u00c3\u00a9.jkl", other);
    const std::vector<std::wstring> flags{ L"--workspace", workspace.native(), L"--resource-root", resources.native() };
    for (const auto* name : { L"ascii.jkl", L"caf\u00e9.jkl", L"room \u6f22\U0001f319.jkl" })
    {
        const auto path = root / name;
        writeFile(path, intended);
        auto args = Json::makeObject();
        args.set("path", Json::makeString(utf8(policy ? path : path.filename())));
        auto verify = [&](const Json& receipt)
        {
            require(receipt.object.at("inputSha256").str == sed_agent::sha256Hex(intended)
                && receipt.object.at("kind").number == 0, "Inspected wrong native entry for " + utf8(path));
        };
        if (useMcp)
        {
            verify(callTool(server, flags, scratch, "sed_inspect", args));
            verify(callTool(server, flags, scratch, "sed_discover", args));
        }
        else
        {
            verify(run(agent, { L"inspect", path.native() }, scratch));
        }
    }
    if (policy)
    {
        auto args = Json::makeObject();
        args.set("path", Json::makeString(utf8(scratch / L"seed-jk.jkl")));
        require(callTool(server, flags, scratch, "sed_inspect", args, true).object.at("code").str == "outsideWorkspace",
            "Unexpected outside-root result");
    }
    const auto missing = root / L"missing \u6f22\U0001f319.jkl";
    auto args = Json::makeObject();
    args.set("path", Json::makeString(utf8(policy ? missing : missing.filename())));
    const auto error = useMcp ? callTool(server, flags, scratch, "sed_inspect", args, true)
        : run(agent, { L"inspect", missing.native() }, scratch, {}, 3);
    require(error.object.at("code").str == "inputReadFailed"
        && error.object.at("error").str.find(utf8(missing.filename())) != std::string::npos,
        "Missing source diagnostic lost filename or category");
}

void levelNew(bool useMcp, bool staged, const fs::path& agent, const fs::path& server, const fs::path& scratch)
{
    const auto workspace = scratch / (staged ? L"workspace \u6f22\U0001f319" : L"work");
    fs::create_directory(workspace);
    if (staged)
    {
        const auto temp = scratch / L"staging \u6f22\U0001f319";
        fs::create_directory(temp);
        checkWindows(SetEnvironmentVariableW(L"TMP", temp.c_str()), "SetEnvironmentVariableW(TMP)");
        checkWindows(SetEnvironmentVariableW(L"TEMP", temp.c_str()), "SetEnvironmentVariableW(TEMP)");
    }
    const auto requestFile = scratch / L"request \u6f22\U0001f319.json";
    const auto request = sed_agent::parseJson(R"({"schemaVersion":1,"kind":"jk"})");
    writeFile(requestFile, sed_agent::toJsonString(request));
    const std::vector<std::wstring> flags{ L"--workspace", workspace.native() };
    for (const auto* name : { L"ascii.jkl", L"caf\u00e9.jkl", L"room \u6f22\U0001f319.jkl" })
    {
        const auto path = workspace / name;
        auto args = Json::makeObject();
        args.set("output", Json::makeString(utf8(path.filename())));
        if (staged) args.set("request", request);
        else args.set("kind", Json::makeString("jk"));
        args.set("operationId", Json::makeString("new-" + utf8(path.filename())));
        const std::vector<std::wstring> cliArgs{ L"new", path.native(), staged ? requestFile.native() : L"jk" };
        const auto receipt = useMcp ? callTool(server, flags, scratch, "sed_new", args) : run(agent, cliArgs, scratch);
        require(receipt.object.at("output").str == utf8(path) && receipt.object.at("kind").str == "jk",
            "Incorrect output filename or request kind in receipt for " + utf8(path));
        bool found = false;
        for (const auto& entry : fs::directory_iterator(workspace))
            if (entry.path().filename().native() == path.filename().native()) found = true;
        require(found, "Successful receipt but native directory has no " + utf8(path.filename()));
        require(sed_agent::sha256Hex(readFile(path)) == receipt.object.at("outputSha256").str,
            "Output bytes differ from receipt");
        writeFile(path, "keep");
        if (useMcp)
        {
            const auto replay = callTool(server, flags, scratch, "sed_new", args);
            require(replay.object.at("journaled").boolean && replay.object.at("output").str == utf8(path)
                && replay.object.at("outputSha256").str == receipt.object.at("outputSha256").str, "Incorrect replay receipt");
            args.object.erase("operationId");
            require(callTool(server, flags, scratch, "sed_new", args, true).object.at("exitCode").number == 5,
                "Occupied output was not refused");
        }
        else run(agent, cliArgs, scratch, {}, 5);
        require(readFile(path) == "keep", "Existing output was overwritten");
    }
    if (useMcp)
    {
        const auto outside = scratch / L"outside \u6f22\U0001f319.jkl";
        auto args = Json::makeObject();
        args.set("output", Json::makeString(utf8(outside)));
        require(callTool(server, flags, scratch, "sed_new", args, true).object.at("code").str == "outsideWorkspace"
            && !fs::exists(outside), "Created output outside workspace");
    }
}

Json workflowPatch(const Json& inspected)
{
    auto patch = sed_agent::parseJson(R"({"schemaVersion":1,"resolveExportDependencies":false,
        "operations":[{"op":"setGravity","value":2}]})");
    patch.set("inputSha256", inspected.object.at("inputSha256"));
    patch.set("sourceRevision", inspected.object.at("sourceRevision"));
    return patch;
}

void workflowRead(const std::string& command, bool useMcp, const fs::path& agent, const fs::path& server,
    const fs::path& scratch)
{
    const auto seed = scratch / L"seed.jkl", other = scratch / L"other.jkl";
    run(agent, { L"new", seed.native(), L"jk" }, scratch);
    run(agent, { L"new", other.native(), L"mots" }, scratch);
    const auto inspected = run(agent, { L"inspect", seed.native() }, scratch);
    const auto original = readFile(seed), twin = readFile(other);
    require(original != twin, "Source controls must differ");
    Json request;
    if ( command == "query" ) request = sed_agent::parseJson(R"({"collection":"summary"})");
    else if ( command == "select" ) request = sed_agent::parseJson(R"({"target":"sector","point":[0,0,0]})");
    else if ( command == "validate" ) request = sed_agent::parseJson(R"({"strict":true})");
    else request = workflowPatch(inspected);
    const std::string tool = "sed_" + (command == "validate" ? "diagnose" : command);
    const auto hash = sed_agent::sha256Hex(original);

    for ( const auto* name : { L"ascii.jkl", L"caf\u00e9.jkl", L"room \u6f22\U0001f319.jkl" } )
    {
        const bool wideRoots = fs::path(name) == L"room \u6f22\U0001f319.jkl";
        const fs::path workspace = scratch / (wideRoots ? L"workspace \u6f22\U0001f319" : L"workspace");
        fs::create_directories(workspace);
        const auto source = workspace / name;
        writeFile(source, original);
        writeFile(workspace / L"caf\u00c3\u00a9.jkl", twin);
        if ( wideRoots )
        {
            const auto temp = scratch / L"temp \u6f22\U0001f319";
            fs::create_directory(temp);
            checkWindows(SetEnvironmentVariableW(L"TMP", temp.c_str()), "SetEnvironmentVariableW(TMP)");
            checkWindows(SetEnvironmentVariableW(L"TEMP", temp.c_str()), "SetEnvironmentVariableW(TEMP)");
        }
        auto args = Json::makeObject();
        args.set("path", Json::makeString(utf8(source.filename())));
        args.set("request", request);
        const auto req = scratch / (wideRoots ? L"request \u6f22\U0001f319.json" : L"request.json");
        writeFile(req, sed_agent::toJsonString(request));
        const std::vector<std::wstring> flags{ L"--workspace", workspace.native() };
        const auto receipt = useMcp ? callTool(server, flags, scratch, tool.c_str(), args)
            : run(agent, { fs::path(command).native(), source.native(), req.native() }, scratch);
        require(receipt.object.at("inputSha256").str == hash, command + " read a different native file: " + utf8(source));
        if ( command == "validate" )
        {
            args.object.erase("request");
            const auto ordinary = useMcp ? callTool(server, flags, scratch, tool.c_str(), args)
                : run(agent, { L"validate", source.native() }, scratch);
            require(ordinary.object.at("inputSha256").str == hash, "Default validate read a different native file");
        }
        require(readFile(source) == original && readFile(workspace / L"caf\u00c3\u00a9.jkl") == twin,
            "Read operation changed source entries");

        if ( wideRoots )
        {
            const auto missing = workspace / L"missing \u6f22\U0001f319.jkl";
            args.set("path", Json::makeString(utf8(missing.filename())));
            const auto error = useMcp ? callTool(server, flags, scratch, tool.c_str(), args, true)
                : run(agent, { fs::path(command).native(), missing.native(), req.native() }, scratch, {}, 3);
            require(error.object.at("code").str == "inputReadFailed"
                && error.object.at("error").str.find(utf8(missing.filename())) != std::string::npos,
                "Missing source diagnostic lost filename or category");
        }
    }
}

void workflowApply(bool useMcp, bool occupied, const fs::path& agent, const fs::path& server, const fs::path& scratch)
{
    const auto workspace = scratch / L"workspace";
    fs::create_directory(workspace);
    const auto source = workspace / L"source.jkl";
    run(agent, { L"new", source.native(), L"jk" }, scratch);
    const auto original = readFile(source);
    const auto patch = workflowPatch(run(agent, { L"inspect", source.native() }, scratch));
    const auto req = scratch / L"patch.json";
    writeFile(req, sed_agent::toJsonString(patch));
    const auto plan = run(agent, { L"plan", source.native(), req.native() }, scratch);
    const auto hash = plan.object.at("outputSha256").str;
    require(hash != sed_agent::sha256Hex(original), "Planned edit must change the source bytes");
    const std::vector<std::wstring> flags{ L"--workspace", workspace.native() };
    for ( const auto* name : { L"ascii.jkl", L"caf\u00e9.jkl", L"room \u6f22\U0001f319.jkl" } )
    {
        const auto output = workspace / name;
        auto args = Json::makeObject();
        args.set("path", Json::makeString("source.jkl"));
        args.set("request", patch);
        args.set("output", Json::makeString(utf8(output.filename())));
        args.set("expectedOutputSha256", Json::makeString(hash));
        const auto apply = [&](DWORD expectedExit)
        {
            return useMcp ? callTool(server, flags, scratch, "sed_apply", args, expectedExit != 0)
                : run(agent, { L"apply", source.native(), req.native(), output.native(),
                    fs::path(args.object.at("expectedOutputSha256").str).native() }, scratch, {}, expectedExit);
        };
        args.set("expectedOutputSha256", Json::makeString(std::string(64, '0')));
        require(apply(4).object.at("exitCode").number == 4 && !fs::exists(output), "A stale plan published output");
        args.set("expectedOutputSha256", Json::makeString(hash));
        if ( occupied )
        {
            writeFile(output, "keep this destination");
            const auto count = std::distance(fs::directory_iterator(workspace), fs::directory_iterator());
            const auto refused = apply(5);
            require(refused.object.at("exitCode").number == 5 && refused.object.at("error").str.find("already exists") != std::string::npos
                && readFile(output) == "keep this destination"
                && std::distance(fs::directory_iterator(workspace), fs::directory_iterator()) == count,
                "Occupied destination did not refuse without creating another filename");
        }
        else
        {
            args.set("operationId", Json::makeString("apply-" + utf8(output.filename())));
            const auto receipt = apply(0);
            require(receipt.object.at("output").str == utf8(output)
                && fs::is_regular_file(output), "Receipt and native output disagree: " + utf8(output));
            require(receipt.object.at("outputSha256").str == hash && sed_agent::sha256Hex(readFile(output)) == hash,
                "Published file differs from the reviewed bytes");
            if ( useMcp )
            {
                writeFile(output, "retain after replay");
                const auto replay = apply(0);
                require(replay.object.at("journaled").boolean && replay.object.at("output").str == utf8(output)
                    && replay.object.at("outputSha256").str == hash && readFile(output) == "retain after replay",
                    "Journal replay lost the original receipt or repeated a write");
                args.set("output", Json::makeString("./" + utf8(output.filename())));
                require(callTool(server, flags, scratch, "sed_apply", args, true).object.at("code").str
                    == "operationIdConflict", "Path normalization changed the original argument fingerprint");
            }
        }
    }
    if ( !occupied )
    {
        const auto folder = workspace / L"project \u6f22\U0001f319";
        fs::create_directory(folder);
        const auto nativeSource = folder / L"source \u6f22\U0001f319.jkl";
        const auto nativeRequest = folder / L"patch \u6f22\U0001f319.json";
        const auto output = folder / L"variant \u6f22\U0001f319.jkl";
        writeFile(nativeSource, original);
        writeFile(nativeRequest, sed_agent::toJsonString(patch));
        auto args = Json::makeObject();
        args.set("request", patch);
        args.set("path", Json::makeString(utf8(nativeSource.filename())));
        args.set("output", Json::makeString(utf8(output.filename())));
        args.set("expectedOutputSha256", Json::makeString(hash));
        const std::vector<std::wstring> nativeFlags{ L"--workspace", folder.native() };
        checkWindows(SetEnvironmentVariableW(L"TMP", folder.c_str()), "SetEnvironmentVariableW(TMP)");
        checkWindows(SetEnvironmentVariableW(L"TEMP", folder.c_str()), "SetEnvironmentVariableW(TEMP)");
        const auto result = useMcp ? callTool(server, nativeFlags, scratch, "sed_apply", args)
            : run(agent, { L"apply", nativeSource.native(), nativeRequest.native(), output.native(), fs::path(hash).native() }, scratch);
        require(result.object.at("output").str == utf8(output) && sed_agent::sha256Hex(readFile(output)) == hash
            && readFile(nativeSource) == original, "Unicode apply lost a source, request or destination path");
        if ( useMcp )
        {
            const auto outside = scratch / L"outside \u6f22\U0001f319.jkl";
            args.set("output", Json::makeString(utf8(outside)));
            require(callTool(server, nativeFlags, scratch, "sed_apply", args, true).object.at("code").str
                == "outsideWorkspace" && !fs::exists(outside), "Output policy admitted an outside destination");
            args.set("output", Json::makeString("clean.jkl"));
            for ( const auto& bad : { std::string("bad\0path", 8), std::string("bad\xff") } )
            {
                for ( const auto* key : { "path", "output", "operationId" } )
                {
                    auto invalid = args;
                    invalid.set(key, Json::makeString(bad));
                    // Embedded NUL is valid JSON in an operation ID, but not in a filesystem path.
                    if ( std::string_view(key) == "operationId" && bad.find('\0') != std::string::npos ) continue;
                    const Json error = callTool(server, nativeFlags, scratch, "sed_apply", invalid, true);
                    require(error.object.at("code").str == "invalidArguments", "Malformed UTF-8 or NUL accepted");
                }
            }
            require(!fs::exists(folder / L"clean.jkl"), "Malformed apply created output");
        }
    }
    require(readFile(source) == original, "Apply changed its source");
}

void workflowInstalledAgent(const fs::path& agent, const fs::path& scratch)
{
    const auto installation = scratch / L"installed \u6f22\U0001f319";
    const auto library = installation / L"data" / L"cog_templates";
    fs::create_directories(library);
    const auto installedAgent = installation / agent.filename();
    fs::copy_file(agent, installedAgent);
    if ( GetEnvironmentVariableW(L"RED_DATA_DIR", nullptr, 0) )
        checkWindows(SetEnvironmentVariableW(L"RED_DATA_DIR", nullptr), "SetEnvironmentVariableW(RED_DATA_DIR)");
    writeFile(library / "knightcop_vehicles.tpl", "# packaged library marker");
    // Deliberately omit one required symbol. Only the executable-adjacent library
    // can produce this refusal; the compiled source library has a complete script.
    writeFile(library / "kcswoop.cog", "symbols\nthing bike\nend\ncode\nend\n");
    const auto level = scratch / L"source.jkl";
    run(installedAgent, { L"new", level.native(), L"jk" }, scratch);
    const std::string patch = R"({"schemaVersion":1,"operations":[{"op":"pursuitAssistant","enemy":0}]})";
    const auto error = run(installedAgent, { L"plan", level.native(), L"-" }, scratch, patch, 5);
    require(error.object.at("code").str == "assistantScriptMismatch"
        && error.object.at("error").str.find("wakedistance") != std::string::npos,
        "Native CLI lost executable-adjacent template discovery or stdin requests");
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try
    {
        require(argc == 4, "Expected scenario, red-agent and red-mcp paths");
        const std::wstring scenario = argv[1];
        std::cout << "GetACP() = " << GetACP() << std::endl;
        const Scratch scratch;
        if ( scenario == L"workflow_install" )
        {
            workflowInstalledAgent(argv[2], scratch.root);
            return 0;
        }
        if ( scenario.starts_with(L"workflow_") )
        {
            const bool useMcp = scenario.starts_with(L"workflow_mcp_");
            const auto command = utf8(fs::path(scenario.substr(13)));
            if ( command == "apply" || command == "occupied" )
                workflowApply(useMcp, command == "occupied", argv[2], argv[3], scratch.root);
            else
                workflowRead(command, useMcp, argv[2], argv[3], scratch.root);
            return 0;
        }
        if (scenario == L"level_cli_inspect" || scenario == L"level_mcp_inspect" || scenario == L"level_mcp_resource")
        {
            levelInspect(scenario != L"level_cli_inspect", scenario == L"level_mcp_resource", argv[2], argv[3], scratch.root);
            return 0;
        }
        if (scenario == L"level_cli_new" || scenario == L"level_mcp_new"
            || scenario == L"level_cli_request" || scenario == L"level_mcp_staging")
        {
            levelNew(scenario == L"level_mcp_new" || scenario == L"level_mcp_staging",
                scenario == L"level_cli_request" || scenario == L"level_mcp_staging", argv[2], argv[3], scratch.root);
            return 0;
        }
        const auto workspace = scratch.root / (scenario == L"mcp_workspace" ? L"caf\u00e9 \u0219\u6f22" : L"workspace");
        if ( scenario == L"mcp_temp" )
        {
            const auto temp = scratch.root / L"staging caf\u00e9 \u0219\u6f22";
            fs::create_directory(temp);
            checkWindows(SetEnvironmentVariableW(L"TMP", temp.c_str()), "SetEnvironmentVariableW(TMP)");
            checkWindows(SetEnvironmentVariableW(L"TEMP", temp.c_str()), "SetEnvironmentVariableW(TEMP)");
        }
        const bool resourceRoot = scenario == L"mcp_resource";
        const auto sourceDir = resourceRoot ? scratch.root / L"resource caf\u00e9 \u0219\u6f22" : workspace;
        fs::create_directories(workspace);
        fs::create_directories(sourceDir);
        const bool unicodeSource = scenario == L"cli" || scenario == L"mcp_source";
        const auto source = sourceDir / (unicodeSource ? L"caf\u00e9 \u0219\u6f22 source.3do" : L"source.3do");
        const auto output = workspace / (scenario == L"mcp_output" ? L"caf\u00e9.3do" : L"caf\u00e9 \u0219\u6f22 variant.3do");
        const auto original = readFile("tests/fixtures/relay-sentry/relay_sentry.3do");
        writeFile(source, original);
        if ( scenario == L"cli" ) cli(argv[2], scratch.root, source, output, original);
        else mcp(argv[3], scratch.root, workspace, source, output, original, resourceRoot);
        require(readFile(source) == original, "Original source changed");
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
