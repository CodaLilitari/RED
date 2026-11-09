#include "../agent_workspace.hpp"
#include "../bounded_input.hpp"
#include "../source_read_set.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>

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

// Produces a finite non-seekable stream without allocating its full extent.
// Faults and source changes occur at explicit read boundaries, never on timers.
class GeneratedBuffer final : public std::streambuf
{
public:
    GeneratedBuffer(std::string prefix, const std::size_t extent,
        std::optional<std::size_t> failAfter = std::nullopt,
        std::function<void()> onFirstRead = {})
        : prefix_(std::move(prefix)), extent_(extent), failAfter_(failAfter),
          onFirstRead_(std::move(onFirstRead))
    {
    }

    std::size_t consumed() const noexcept
    {
        return consumed_;
    }

protected:
    int_type underflow() override
    {
        if ( onFirstRead_ )
        {
            auto callback = std::move(onFirstRead_);
            onFirstRead_ = {};
            callback();
        }
        if ( failAfter_ && consumed_ >= *failAfter_ )
        {
            if ( !failureDelivered_ )
            {
                failureDelivered_ = true;
                throw std::runtime_error("Injected input device failure");
            }
            return traits_type::eof();
        }
        if ( consumed_ == extent_ )
        {
            return traits_type::eof();
        }
        return traits_type::to_int_type(consumed_ < prefix_.size() ? prefix_[consumed_] : ' ');
    }

    int_type uflow() override
    {
        const int_type next = underflow();
        if ( !traits_type::eq_int_type(next, traits_type::eof()) )
        {
            ++consumed_;
        }
        return next;
    }

    std::streamsize xsgetn(char* output, const std::streamsize count) override
    {
        std::streamsize copied{};
        while ( copied < count )
        {
            const int_type next = uflow();
            if ( traits_type::eq_int_type(next, traits_type::eof()) )
            {
                break;
            }
            output[copied++] = traits_type::to_char_type(next);
        }
        return copied;
    }

private:
    std::string prefix_;
    std::size_t extent_{};
    std::optional<std::size_t> failAfter_;
    std::function<void()> onFirstRead_;
    std::size_t consumed_{};
    bool failureDelivered_{};
};

class StdinScope
{
public:
    explicit StdinScope(std::streambuf& buffer)
        : savedState_(std::cin.rdstate()), savedBuffer_(std::cin.rdbuf(&buffer))
    {
        std::cin.clear();
    }

    ~StdinScope()
    {
        std::cin.rdbuf(savedBuffer_);
        std::cin.clear(savedState_);
    }

private:
    std::ios::iostate savedState_;
    std::streambuf* savedBuffer_;
};

void writeText(const fs::path& path, const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write input-safety fixture");
    }
}

void testRequestReadBoundaries()
{
    {
        GeneratedBuffer buffer("{}", sed_agent::kMaxRequestBytes);
        StdinScope input(buffer);
        check(sed_agent::readRequest("-").isObject(),
            "non-seekable request at the exact byte cap is accepted");
    }
    {
        GeneratedBuffer buffer("{}", sed_agent::kMaxRequestBytes * 3u);
        StdinScope input(buffer);
        bool refused{};
        try
        {
            (void)sed_agent::readRequest("-");
        }
        catch ( const sed_agent::AgentError& error )
        {
            refused = error.code == sed_agent::kExitUsage;
        }
        check(refused, "oversized request reports the request-cap usage error");
        check(buffer.consumed() <= sed_agent::kMaxRequestBytes + 1u,
            "request cap bounds consumed input to the limit plus one probe byte");
    }
    {
        GeneratedBuffer buffer("{}", 3u, 2u);
        StdinScope input(buffer);
        bool failed{};
        try
        {
            (void)sed_agent::readRequest("-");
        }
        catch ( const sed_agent::AgentError& error )
        {
            failed = error.code == sed_agent::kExitInput;
        }
        check(failed, "read error after valid JSON is not accepted as a complete request");
    }
}

void testSourceChangedDuringRequest(const fs::path& root, const bool removeSource)
{
    const std::string name = removeSource ? "deleted" : "changed";
    const auto source = root / (name + ".jkl");
    const auto output = root / (name + "-output.jkl");
    (void)sed_agent::runCommand("new", { source.string(), "mots" });
    const std::string original = sed_agent::readFileBytes(source);
    const std::string request = "{\"inputSha256\":\"" + sed_agent::sha256Hex(original)
        + "\",\"operations\":[{\"op\":\"setGravity\",\"value\":2}]}";
    GeneratedBuffer buffer(request, request.size(), std::nullopt, [&]()
    {
        if ( removeSource )
        {
            fs::remove(source);
        }
        else
        {
            writeText(source, original + "\n# independently changed source\n");
        }
    });
    StdinScope input(buffer);
    bool conflict{};
    try
    {
        (void)sed_agent::runCommand("apply", { source.string(), "-", output.string() });
    }
    catch ( const sed_agent::AgentError& error )
    {
        conflict = error.code == sed_agent::kExitConflict;
    }
    check(conflict, "source " + name + " during request read reports a final revision conflict");
    check(!fs::exists(output), "source " + name + " conflict publishes no output");
    if ( !removeSource )
    {
        check(sed_agent::readFileBytes(source) == original + "\n# independently changed source\n",
            "refused stale apply preserves the competing writer's source bytes");
    }
}

void testUnchangedSourceControl(const fs::path& root)
{
    const auto source = root / "unchanged.jkl";
    const auto output = root / "unchanged-output.jkl";
    (void)sed_agent::runCommand("new", { source.string(), "mots" });
    const std::string original = sed_agent::readFileBytes(source);
    const std::string request = "{\"inputSha256\":\"" + sed_agent::sha256Hex(original)
        + "\",\"operations\":[{\"op\":\"setGravity\",\"value\":2}]}";
    GeneratedBuffer buffer(request, request.size());
    StdinScope input(buffer);
    const auto result = sed_agent::runCommand("apply", { source.string(), "-", output.string() });
    check(result.find("outputSha256") && fs::is_regular_file(output),
        "unchanged input still publishes the reviewed edit");
    check(sed_agent::readFileBytes(source) == original,
        "successful create-only apply preserves input bytes");
}

template <typename Operation>
void expectReadError(Operation&& operation, const sed_agent::detail::InputReadError::Kind kind,
    const std::string& message)
{
    bool matched{};
    try
    {
        operation();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        matched = error.kind == kind;
    }
    check(matched, message);
}

class StalledBuffer final : public std::streambuf
{
protected:
    std::streamsize xsgetn(char*, std::streamsize) override
    {
        return 0;
    }
    int_type underflow() override
    {
        return traits_type::to_int_type('x');
    }
};

void testBoundedHelpers(const fs::path& root)
{
    using sed_agent::detail::readBoundedFile;
    using sed_agent::detail::readBoundedStream;
    using Kind = sed_agent::detail::InputReadError::Kind;
    {
        GeneratedBuffer buffer("abc", 32u);
        std::istream input(&buffer);
        expectReadError([&]() { (void)readBoundedStream(input, 16u); }, Kind::LimitExceeded,
            "bounded stream distinguishes over-cap input from a device error");
        check(buffer.consumed() == 17u, "bounded stream reads at most one byte past a small cap");
    }
    {
        GeneratedBuffer buffer("abc", 32u);
        std::istream input(&buffer);
        expectReadError([&]() { (void)readBoundedStream(input, 16u, 17u); }, Kind::LimitExceeded,
            "known oversized extent is refused before reading");
        check(buffer.consumed() == 0u, "known oversized extent consumes no source data");
    }
    {
        GeneratedBuffer buffer("abc", 3u);
        std::istream input(&buffer);
        expectReadError([&]() { (void)readBoundedStream(input, 16u, 4u); }, Kind::ReadFailure,
            "short source differs from a complete admitted extent");
    }
    {
        GeneratedBuffer buffer("abc", 3u, 2u);
        std::istream input(&buffer);
        expectReadError([&]() { (void)readBoundedStream(input, 16u); }, Kind::ReadFailure,
            "stream failure never returns partially captured bytes");
    }
    {
        StalledBuffer buffer;
        std::istream input(&buffer);
        expectReadError([&]() { (void)readBoundedStream(input, 16u); }, Kind::ReadFailure,
            "zero progress without EOF is a read error rather than an unbounded retry");
    }
    {
        std::istringstream empty;
        check(readBoundedStream(empty, 0u).empty(), "zero cap permits a genuinely empty stream");
        std::istringstream nonempty("x");
        expectReadError([&]() { (void)readBoundedStream(nonempty, 0u); }, Kind::LimitExceeded,
            "zero cap refuses the first source byte");
    }
    const auto source = root / "bounded.bin";
    const std::string binary("a\0b\r\n", 5u);
    writeText(source, binary);
    check(readBoundedFile(source, binary.size()) == binary, "file reader preserves exact binary bytes at its cap");
    expectReadError([&]() { (void)readBoundedFile(source, binary.size() - 1u); }, Kind::LimitExceeded,
        "file reader refuses an oversized file at metadata admission");
    expectReadError([&]() { (void)readBoundedFile(root); }, Kind::NotRegularFile,
        "file reader refuses a directory without attempting stream input");
    expectReadError([&]() { (void)readBoundedFile(root / "missing.bin"); }, Kind::ReadFailure,
        "file reader distinguishes missing input from an oversized input");
}

template <typename Operation>
void expectSourceConflict(Operation&& operation, const std::string& message)
{
    bool conflicted{};
    try
    {
        operation();
    }
    catch ( const sed_agent::detail::SourceRevisionError& )
    {
        conflicted = true;
    }
    check(conflicted, message);
}

void testReadSetHelpers(const fs::path& root)
{
    using sed_agent::detail::SourceReadSet;
    const auto path = root / "tracked.bin";
    writeText(path, "abcd");
    SourceReadSet set;
    check(set.capture(path, 4u) == "abcd", "read set returns the exact bytes it tracks");
    set.track(path, "abcd", 4u);
    set.requireUnchanged();
    writeText(path, "abce");
    expectSourceConflict([&]() { set.requireUnchanged(); },
        "same-size source replacement is detected by content digest");
    expectSourceConflict([&]() { set.track(path, "abce", 4u); },
        "two different captures of one path cannot coexist in a transaction");
    writeText(path, "abcd");
    set.requireUnchanged();
    fs::remove(path);
    expectSourceConflict([&]() { set.requireUnchanged(); },
        "deleted tracked source reports a revision conflict");
    writeText(path, "longer than capture limit");
    expectSourceConflict([&]() { set.requireUnchanged(); },
        "tracked source growth past its admission budget is a revision conflict");
}

} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-input-safety-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        testRequestReadBoundaries();
        testSourceChangedDuringRequest(root, false);
        testSourceChangedDuringRequest(root, true);
        testUnchangedSourceControl(root);
        testBoundedHelpers(root);
        testReadSetHelpers(root);
    }
    catch ( const sed_agent::AgentError& error )
    {
        check(false, "unexpected agent error: " + error.message);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "input safety failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
