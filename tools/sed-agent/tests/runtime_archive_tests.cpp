#include "../agent_workspace.hpp"
#include "sed/assets/runtime_archive.hpp"
#include "sed/assets/mat_write.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
namespace fs = std::filesystem;
using sed_agent::Json;
int failures{};
void check(const bool value, const char* message)
{
    if ( !value )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
void write(const fs::path& path, const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write owned archive fixture");
    }
}
std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if ( !input )
    {
        throw std::runtime_error("Cannot read owned archive fixture");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
struct Directory
{
    fs::path path = fs::temp_directory_path() / ("red-runtime-archive-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory()
    {
        if ( !fs::create_directory(path) )
        {
            throw std::runtime_error("Cannot create isolated archive test directory");
        }
    }
    ~Directory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
Json entry(const std::string& name, const fs::path& path, const std::string& bytes)
{
    auto result = Json::makeObject();
    result.set("name", Json::makeString(name));
    result.set("path", Json::makeString(path.string()));
    result.set("sha256", Json::makeString(sed_agent::sha256Hex(bytes)));
    return result;
}
void run()
{
    using namespace sed::assets;
    Directory directory;
    const auto requestPath = directory.path / "request.json";
    const RgbaImage image{8, 8, std::vector<std::uint8_t>(8u * 8u * 4u, 255)};
    const auto mat = buildIndexedMat({image}, makeGrayscalePalette()).bytes;
    const std::vector<RuntimeArchiveInput> inputs{
        {"Misc/RAW.BIN", std::string("abc\0\xff", 5)},
        {"cog/gate.COG", "symbols\nmessage startup\nend\ncode\nstartup:\nreturn;\nend\n"},
        {"mat/test.MAT", mat}, {"misc/example.TPL", "base none type=cog\n"},
        {".mat", "a dotfile is not an extension"}, {"odd/readme.bin", "note"}};
    const auto built = buildRuntimeArchive(inputs);
    auto request = Json::makeObject();
    auto entries = Json::makeArray();
    for ( std::size_t index = 0; index < inputs.size(); ++index )
    {
        const auto path = directory.path / (std::to_string(index) + ".bin");
        write(path, inputs[index].bytes);
        entries.array.push_back(entry(inputs[index].name, path, inputs[index].bytes));
    }
    request.set("entries", entries);
    write(requestPath, sed_agent::toJsonString(request));
    for ( const auto* extension : {".gob", ".goo"} )
    {
        const auto output = directory.path / (std::string("result") + extension);
        const auto result = sed_agent::runCommand("archive-create", {requestPath.string(), output.string()});
        check(read(output) == built.outputData, "agent and captured helper publish byte-identical GOB and GOO");
        auto expectedEntries = Json::makeArray();
        for ( const auto& value : built.entries )
        {
            auto row = Json::makeObject();
            row.set("name", Json::makeString(value.name));
            row.set("bytes", Json::makeNumber(static_cast<double>(value.bytes)));
            row.set("sha256", Json::makeString(value.sha256));
            row.set("readBack", Json::makeString(value.readBack));
            expectedEntries.array.push_back(std::move(row));
        }
        auto expectedWarnings = Json::makeArray();
        for ( const auto& warning : built.warnings )
        {
            expectedWarnings.array.push_back(Json::makeString(warning));
        }
        check(result.find("entries") && sed_agent::toJsonString(*result.find("entries"))
                == sed_agent::toJsonString(expectedEntries), "agent entry receipts match shared typed results exactly");
        check(result.find("warnings") && sed_agent::toJsonString(*result.find("warnings"))
                == sed_agent::toJsonString(expectedWarnings), "agent warning wording and order remain exact");
    }
    const auto refuse = [&](Json rows, const int code, const std::string& errorCode, const char* message)
    {
        auto invalid = Json::makeObject();
        invalid.set("entries", std::move(rows));
        write(requestPath, sed_agent::toJsonString(invalid));
        const auto output = directory.path / "refused.gob";
        bool matched{};
        try
        {
            (void)sed_agent::runCommand("archive-create", {requestPath.string(), output.string()});
        }
        catch ( const sed_agent::AgentError& error )
        {
            matched = error.code == code && error.errorCode == errorCode;
        }
        check(matched && !fs::exists(output), message);
    };
    auto invalidName = Json::makeArray();
    invalidName.array.push_back(entry("../bad.bin", directory.path / "missing", ""));
    refuse(invalidName, 2, "invalidArchiveEntryName", "name refusal precedes missing source capture");
    auto duplicate = Json::makeArray();
    duplicate.array.push_back(entries.array.front());
    duplicate.array.push_back(entry("misc/raw.bin", directory.path / "missing", ""));
    refuse(duplicate, 5, "duplicateArchiveEntry", "duplicate refusal precedes second source capture");
    auto badPayload = Json::makeArray();
    badPayload.array.push_back(entry("mat/bad.mat", directory.path / "0.bin", inputs[0].bytes));
    badPayload.array.push_back(entry("../bad.bin", directory.path / "missing", ""));
    refuse(badPayload, 3, "invalidArchiveEntry", "first payload validation precedes later entry name checks");
    auto stale = Json::makeArray();
    stale.array.push_back(entry("misc/a.bin", directory.path / "0.bin", "wrong"));
    refuse(stale, 4, "", "bound source hash conflict remains a distinct refusal");
}
}
int main()
{
    try
    {
        run();
    }
    catch ( const sed_agent::AgentError& error )
    {
        std::cerr << "Unexpected agent error: " << error.message << '\n';
        return 1;
    }
    catch ( const std::exception& error )
    {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
