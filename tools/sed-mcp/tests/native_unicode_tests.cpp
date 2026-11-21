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

void mcp(const fs::path& exe, const fs::path& scratch, const fs::path& workspace, const fs::path& source,
    const fs::path& output, const std::string& original, bool resourceRoot)
{
    std::vector<std::wstring> flags{ L"--workspace", workspace.native() };
    if ( resourceRoot ) flags.insert(flags.end(), { L"--resource-root", source.parent_path().native() });
    auto call = [&](const char* name, const Json& args, bool expectError = false)
    {
        const auto line = std::string(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")")
            + name + R"(","arguments":)" + sed_agent::toJsonString(args) + "}}\n";
        const auto reply = run(exe, flags, scratch, line);
        const auto& result = reply.object.at("result");
        const auto payload = result.object.at("content").array.at(0).object.at("text").str;
        require(result.object.at("isError").boolean == expectError, std::string(name) + ": " + payload);
        return sed_agent::parseJson(payload);
    };

    auto args = Json::makeObject();
    args.set("path", Json::makeString(utf8(resourceRoot ? source : source.filename())));
    auto inspectArgs = args;
    inspectArgs.set("query", sed_agent::parseJson(R"({"collection":"meshes","limit":1})"));
    const auto inspect = call("sed_model_native_inspect", inspectArgs);
    require(inspect.object.at("inputSha256").str == sed_agent::sha256Hex(original)
        && inspect.object.at("items").array.size() == 1, "MCP inspected the wrong source or query");
    args.set("request", request(original));
    const auto plan = call("sed_model_native_plan", args);
    args.set("expectedRequestSha256", plan.object.at("requestSha256"));
    args.set("expectedOutputSha256", plan.object.at("outputSha256"));
    args.set("output", Json::makeString(utf8(output.filename())));
    verifyPublication(call("sed_model_native_apply", args), output, plan);
    writeFile(output, "keep this destination");
    const auto refused = call("sed_model_native_apply", args, true);
    require(refused.object.at("exitCode").number == 5 && readFile(output) == "keep this destination",
        "MCP failed to preserve existing output");
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
