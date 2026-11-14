// Standalone test child: no Qt, game data, runtime installation, or GUI.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
bool argumentEquals(const std::filesystem::path::value_type* argument, const char* expected)
{
    return std::filesystem::path(argument).native() == std::filesystem::path(expected).native();
}
std::string encodedValue(const std::string& json, const std::string& key)
{
    const auto field = json.find('"' + key + '"');
    const auto colon = json.find(':', field);
    const auto start = json.find('"', colon);
    auto end = start + 1;
    while ( end < json.size() )
    {
        if ( json[end] == '\\' )
        {
            end += 2;
        }
        else if ( json[end] == '"' )
        {
            return json.substr(start, end - start + 1);
        }
        else
        {
            ++end;
        }
    }
    return "null";
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if ( argc == 3 && argumentEquals(argv[1], "--descendant") )
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::ofstream(std::filesystem::path(argv[2])) << "escaped";
        return 0;
    }
    if ( argc != 4 || !argumentEquals(argv[1], "--installation-check-child")
        || !argumentEquals(argv[2], "--report-directory") )
    {
        return 2;
    }
    std::string admission;
    std::getline(std::cin, admission);
    if ( admission != "start" )
    {
        return 3;
    }
    const std::filesystem::path directory(argv[3]);
    const auto mode = directory.filename();
    if ( mode == "partial-progress" )
    {
        std::cout << "incomplete" << std::flush;
    }
    if ( mode == "crash" )
    {
        return 31;
    }
    if ( mode == "excess" )
    {
        for ( int index = 0; index < 400000; ++index )
        {
            std::fputc('x', stderr);
        }
        std::fflush(stderr);
    }
    if ( mode == "descendants" )
    {
        const auto marker = directory / "escaped-child";
#ifdef _WIN32
        std::wstring command = L'"' + std::wstring(argv[0]) + L"\" --descendant \"" + marker.native() + L'"';
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if ( !CreateProcessW(argv[0], command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &process) )
        {
            return 8;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        const pid_t child = fork();
        if ( child == 0 )
        {
            execl(argv[0], argv[0], "--descendant", marker.c_str(), nullptr);
            _exit(9);
        }
        if ( child < 0 )
        {
            return 8;
        }
#endif
    }
    if ( mode == "stall" || mode == "cancel" || mode == "descendants" )
    {
        std::ofstream(directory / "admitted") << "owned";
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    std::ifstream requestFile(directory / "request.json");
    const std::string request((std::istreambuf_iterator<char>(requestFile)), {});
#ifdef _WIN32
    const std::string nativePlatform = "windows";
#elif defined(__APPLE__)
    const std::string nativePlatform = "cocoa";
#else
    const std::string nativePlatform = "xcb";
#endif
    std::filesystem::copy_file(directory.parent_path() / "before.png", directory / "frame-before.png");
    std::filesystem::copy_file(directory.parent_path() / (mode == "blank" ? "blank.png" : "after.png"),
        directory / "frame-after.png");
    std::ofstream output(directory / "child-result.json");
    output << "{\"schemaVersion\":1,\"runId\":" << (mode == "wrong-run" ? "\"stale\"" : encodedValue(request, "runId"))
           << ",\"status\":\"passed\",\"executable\":" << encodedValue(request, "executable")
           << ",\"nativePlatform\":\"" << (mode == "offscreen" ? "offscreen" : nativePlatform)
           << "\",\"appVersion\":\"synthetic\",\"qtVersion\":\"synthetic\","
              "\"gl\":{\"vendor\":\"fixture\",\"renderer\":\"fixture\",\"version\":\"3.3\"},"
              "\"milestones\":{\"shellExposed\":true,\"contextValid\":true,\"sceneShaderLinked\":"
           << (mode == "missing-shader" ? "false" : "true")
           << ",\"runtimeDataParsed\":true},"
              "\"probes\":{\"firstFace\":[0.25,0.5],\"secondFace\":[0.75,0.5],\"background\":[0.5,0.2]}}";
    if ( mode == "progress-owner-destroy" )
    {
        std::cout << "{\"phase\":\"fixture-final-progress\"}\n" << std::flush;
    }
    return 0;
}
