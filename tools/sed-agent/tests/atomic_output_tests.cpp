#include "../agent_workspace.hpp"
#include "../atomic_output.hpp"
#include "../../../src/core/src/assets/atomic_file_write.hpp"

#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <set>

namespace
{
namespace fs = std::filesystem;
int failures{};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class TestDirectory
{
public:
    TestDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("red-agent-atomic-output-" + std::to_string(stamp));
        if ( !fs::create_directory(path) )
        {
            throw std::runtime_error("Cannot create isolated atomic-output test directory");
        }
    }

    ~TestDirectory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path path;
};

std::string readBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if ( !input )
    {
        throw std::runtime_error("Cannot read atomic-output fixture");
    }
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void writeCompetitor(const fs::path& path, const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot create competing atomic-output fixture");
    }
}

void expectRefusal(const fs::path& path, const std::string& bytes)
{
    bool refused{};
    try
    {
        sed_agent::writeNewFileAtomic(path, bytes);
    }
    catch ( const sed_agent::AgentError& error )
    {
        refused = error.code == sed_agent::kExitRefusal;
    }
    check(refused, "create-only output refuses an entry created after caller preflight");
}

void testExistingOutputIsNeverReplaced(const fs::path& root)
{
    const auto destination = root / "competing-output.bin";
    check(!fs::exists(destination), "caller initially sees no output");
    // A different writer wins after the caller's preflight. The final writer,
    // not an earlier exists() check, must enforce no-replace publication.
    writeCompetitor(destination, "complete competing output");
    expectRefusal(destination, "replacement must never appear");
    check(readBytes(destination) == "complete competing output",
        "competing destination bytes survive the refused publication");
}

void testCompleteBinaryAndEmptyOutput(const fs::path& root)
{
    std::string bytes(2 * 1024 * 1024 + 31, '\0');
    for ( std::size_t index = 0; index < bytes.size(); ++index )
    {
        bytes[index] = static_cast<char>(index % 251);
    }
    const auto destination = root / "binary-output.bin";
    sed_agent::writeNewFileAtomic(destination, bytes);
    check(readBytes(destination) == bytes, "all binary bytes are published without text conversion");
    const auto empty = root / "empty-output.bin";
    sed_agent::writeNewFileAtomic(empty, {});
    check(fs::is_regular_file(empty) && fs::file_size(empty) == 0,
        "an empty output is a complete regular file");
}

void testDirectoryCollisionAndCleanup(const fs::path& root)
{
    const auto destination = root / "existing-directory";
    fs::create_directory(destination);
    writeCompetitor(destination / "owner.txt", "unrelated owner bytes");
    expectRefusal(destination, "cannot replace a directory");
    check(readBytes(destination / "owner.txt") == "unrelated owner bytes",
        "destination directory and its contents survive refusal");
    for ( const auto& entry : fs::directory_iterator(root) )
    {
        check(!entry.path().filename().string().ends_with(".tmp"),
            "owned staging files are cleaned after success and refusal");
    }
}
void testCompetitorAppearsAfterStaging(const fs::path& root)
{
    const auto destination = root / "late-competitor.bin";
    bool refused{};
    {
        sed::assets::detail::StagedAssetFile staged(destination);
        const std::string bytes = "already staged replacement";
        staged.writeAndClose(std::as_bytes(std::span(bytes.data(), bytes.size())));
        check(!fs::exists(destination), "staging does not expose the destination");
        writeCompetitor(destination, "late competing complete output");
        try
        {
            staged.publishNew(destination);
        }
        catch ( const std::system_error& )
        {
            refused = true;
        }
    }
    check(refused, "publication itself rejects a destination created after staging closed");
    check(readBytes(destination) == "late competing complete output",
        "late competitor is never replaced by create-only publication");
}

void testPublicationRequiresClosedBytes(const fs::path& root)
{
    const auto destination = root / "not-written.bin";
    bool refused{};
    {
        sed::assets::detail::StagedAssetFile staged(destination);
        try
        {
            staged.publishNew(destination);
        }
        catch ( const std::logic_error& )
        {
            refused = true;
        }
    }
    check(refused && !fs::exists(destination), "an unclosed staging file cannot be published");
}

void testFailuresCleanOnlyOwnedStaging(const fs::path& root)
{
    using Fault = sed::assets::detail::StagedAssetFileFaultPointForTests;
    constexpr std::array faults{ Fault::write, Fault::flush, Fault::close, Fault::publish };
    const auto unowned = root / ".sed-save-another-writer.tmp";
    writeCompetitor(unowned, "another writer owns this temporary file");
    for ( const auto point : faults )
    {
        const auto destination = root / ("fault-" + std::to_string(static_cast<unsigned>(point)) + ".bin");
        sed::assets::detail::ScopedStagedAssetFileFaultForTests fault(point);
        bool refused{};
        try
        {
            sed_agent::writeNewFileAtomic(destination, "unpublished bytes");
        }
        catch ( const sed_agent::AgentError& error )
        {
            refused = error.code == sed_agent::kExitRefusal;
        }
        check(fault.triggered(), "the selected write/flush/close/publication failure was exercised");
        check(refused && !fs::exists(destination), "failed staging never publishes a destination");
        check(readBytes(unowned) == "another writer owns this temporary file",
            "failure cleanup retains another writer's temporary file");
        for ( const auto& entry : fs::directory_iterator(root) )
        {
            if ( entry.path() != unowned )
            {
                check(!entry.path().filename().string().ends_with(".tmp"),
                    "failure cleanup removes only this writer's staging file");
            }
        }
    }
    fs::remove(unowned);
}

void testReplacingAssetPublicationRemainsAvailable(const fs::path& root)
{
    const auto destination = root / "replace-intentionally.bin";
    writeCompetitor(destination, "previous complete asset");
    {
        sed::assets::detail::StagedAssetFile staged(destination);
        const std::string bytes = "new complete asset";
        staged.writeAndClose(std::as_bytes(std::span(bytes.data(), bytes.size())));
        staged.publish(destination);
    }
    check(readBytes(destination) == "new complete asset",
        "the existing replacing asset-save contract remains unchanged");
}

void testRevisionCallbackPublication(const fs::path& root)
{
    const auto names = [&]() {
        std::set<fs::path> result;
        for (const auto& entry : fs::directory_iterator(root)) result.insert(entry.path());
        return result;
    };
    const auto before = names();
    const auto conflict = root / "callback-conflict.bin";
    bool callbackRejected = false;
    try
    {
        sed_agent::detail::createNewOutputFile(conflict, "staged bytes", [] {
            throw std::runtime_error("source revision changed");
        });
    }
    catch (const std::runtime_error& error)
    {
        callbackRejected = std::string(error.what()) == "source revision changed";
    }
    check(callbackRejected && names() == before,
        "revision callback failure publishes nothing and removes its staging name");

    const auto competitor = root / "callback-competitor.bin";
    bool publicationRejected = false;
    try
    {
        sed_agent::detail::createNewOutputFile(competitor, "must not replace", [&] {
            writeCompetitor(competitor, "competing complete output");
        });
    }
    catch (const std::system_error&)
    {
        publicationRejected = true;
    }
    auto expected = before;
    expected.insert(competitor);
    check(publicationRejected && readBytes(competitor) == "competing complete output"
        && names() == expected,
        "callback-time competitor survives and rejected staging is fully cleaned");
}
}

int main()
{
    try
    {
        TestDirectory directory;
        testExistingOutputIsNeverReplaced(directory.path);
        testCompleteBinaryAndEmptyOutput(directory.path);
        testDirectoryCollisionAndCleanup(directory.path);
        testCompetitorAppearsAfterStaging(directory.path);
        testPublicationRequiresClosedBytes(directory.path);
        testFailuresCleanOnlyOwnedStaging(directory.path);
        testReplacingAssetPublicationRemainsAvailable(directory.path);
        testRevisionCallbackPublication(directory.path);
    }
    catch ( const sed_agent::AgentError& error )
    {
        std::cerr << "Unexpected agent error: " << error.message << '\n';
        return 1;
    }
    catch ( const std::exception& error )
    {
        std::cerr << "Unexpected test error: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
