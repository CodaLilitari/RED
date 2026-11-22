#include "../agent_workspace.hpp"
#include "../atomic_output.hpp"
#include "../bounded_input.hpp"
#include "../source_read_set.hpp"
#include "model_lighting_fixture.hpp"

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

void testCommandLocalRequestInput(const fs::path& root)
{
    std::stringbuf protocol(R"({"message":"next"})");
    StdinScope protocolScope(protocol);
    const std::string outerRequest = R"({"kind":"mots"})";
    bool innerRefused{};
    GeneratedBuffer outerBuffer(outerRequest, outerRequest.size(), std::nullopt, [&]()
    {
        std::istringstream invalid("{");
        sed_agent::CommandOptions inner;
        inner.requestInput = &invalid;
        try
        {
            (void)sed_agent::runCommand("new", {(root / "inner.jkl").string(), "-"}, inner);
        }
        catch (const sed_agent::AgentError& error)
        {
            innerRefused = error.code == sed_agent::kExitUsage;
        }
    });
    std::istream input(&outerBuffer);
    sed_agent::CommandOptions outer;
    outer.requestInput = &input;
    const auto result = sed_agent::runCommand("new", {(root / "outer.jkl").string(), "-"}, outer);
    check(innerRefused && !fs::exists(root / "inner.jkl") && result.object.at("kind").str == "mots",
        "nested request refusal preserves the outer command's input");

    const auto file = root / "literal.json";
    writeText(file, R"({"kind":"jk"})");
    const auto fromFile = sed_agent::runCommand("new", {(root / "file.jkl").string(), file.string()}, outer);
    check(fromFile.object.at("kind").str == "jk", "explicit request file wins over a host stream");
    check(sed_agent::readRequest("-").object.at("message").str == "next",
        "standalone stdin retains the next document after nested hosted requests");
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

void testNativePublication(const fs::path& root)
{
    using Fault = sed::assets::detail::StagedAssetFileFaultPointForTests;
    const auto source = root / u8"native-caf\u00e9-\u6f22.3do";
    const auto output = root / u8"native-variant-\u6f22.3do";
    const auto requestFile = root / u8"native-request-\u6f22.json";
    const auto original = sed_agent::readFileBytes(NATIVE_RELAY_FIXTURE);
    writeText(source, original);
    const std::string request = "{\"schemaVersion\":1,\"profile\":\"classic2.1\",\"inputSha256\":\""
        + sed_agent::sha256Hex(original)
        + R"(","operations":[{"op":"setTextureVertex","geoset":0,"sourceMesh":0,"textureVertex":1,"u":1,"v":32}]})";
    writeText(requestFile, "\xef\xbb\xbf" + request);
    const auto asUtf8 = [](const fs::path& path)
    {
        const auto encoded = path.u8string();
        return std::string(encoded.begin(), encoded.end());
    };
    const auto checkMissing = [](const char* command, const std::string& spelling)
    {
        bool preserved{};
        try
        {
            (void)sed_agent::runCommand(command, { spelling });
        }
        catch ( const sed_agent::AgentError& error )
        {
            preserved = error.code == sed_agent::kExitInput && error.errorCode == "inputReadFailed"
                && error.message == "Cannot read input file: " + spelling;
        }
        check(preserved, std::string(command) + " preserves its input path encoding in read errors");
    };
    checkMissing("model-native-inspect", asUtf8(root / u8"missing-caf\u00e9-\u6f22.3do"));
    std::string legacySpelling;
    try
    {
        legacySpelling = (root / u8"missing-caf\u00e9.3do").string();
    }
    catch ( const fs::filesystem_error& )
    {
        // This code page cannot represent the accented filename.
        legacySpelling = (root / "missing-legacy.3do").string();
    }
    checkMissing("model-lighting-inspect", legacySpelling);
    const auto plan = sed_agent::runCommand("model-native-plan", { asUtf8(source), asUtf8(requestFile) });
    std::vector<std::string> args{ asUtf8(source), asUtf8(requestFile), plan.object.at("requestSha256").str,
        plan.object.at("outputSha256").str, asUtf8(output) };
    auto expectFailure = [&](int code, std::string_view detail = {})
    {
        try
        {
            (void)sed_agent::runCommand("model-native-apply", args);
        }
        catch ( const sed_agent::AgentError& error )
        {
            return error.code == code && error.message.find(detail) != std::string::npos;
        }
        return false;
    };
    auto exact = plan;
    exact.set("command", sed_agent::Json::makeString("model-native-apply"));
    exact.set("written", sed_agent::Json::makeBool(true));
    const auto boundaryFile = root / u8"at-limit-\u0219.3do";
    auto boundaryArgs = args;
    boundaryArgs[4] = asUtf8(boundaryFile);
    exact.set("output", sed_agent::Json::makeString(boundaryArgs[4]));
    sed_agent::CommandOptions budget;
    budget.nativeReceiptLimit = sed_agent::toJsonString(exact).size() - 1;
    bool limited{};
    try
    {
        (void)sed_agent::runCommand("model-native-apply", boundaryArgs, budget);
    }
    catch ( const sed_agent::AgentError& error )
    {
        limited = error.errorCode == "modelNativeResponseLimit";
    }
    check(limited && !fs::exists(boundaryFile), "one excess serialized receipt byte refuses before publication");
    ++budget.nativeReceiptLimit;
    const auto atLimit = sed_agent::runCommand("model-native-apply", boundaryArgs, budget);
    check(sed_agent::toJsonString(atLimit) == sed_agent::toJsonString(exact) && fs::is_regular_file(boundaryFile),
        "an exactly fitting receipt publishes");
    const std::vector<std::string> compareArgs{ asUtf8(source), asUtf8(source) };
    const auto comparison = sed_agent::runCommand("model-collision-compare", compareArgs);
    check(comparison.object.at("sameFaceMultiset").boolean
            && comparison.object.at("matchedFaces").number == 4
            && !comparison.object.at("runtimeVerified").boolean,
        "comparison routes through native capture without claiming runtime verification");
    budget.nativeReceiptLimit = sed_agent::toJsonString(comparison).size() - 1;
    limited = false;
    try
    {
        (void)sed_agent::runCommand("model-collision-compare", compareArgs, budget);
    }
    catch ( const sed_agent::AgentError& error )
    {
        limited = error.errorCode == "modelNativeResponseLimit";
    }
    check(limited, "collision comparison honors a caller's native receipt budget");
    ++budget.nativeReceiptLimit;
    check(sed_agent::toJsonString(sed_agent::runCommand("model-collision-compare", compareArgs, budget))
            == sed_agent::toJsonString(comparison),
        "an exactly fitting comparison receipt is admitted");
    const auto validPath = args[4];
    args[4] = asUtf8(root) + "/invalid-\xff.3do";
    check(expectFailure(sed_agent::kExitUsage, "UTF-8"), "unrepresentable output path refuses before publication");
    args[4] = validPath;
    for ( const auto index : { 0u, 1u } )
    {
        for ( const auto& suffix : { std::string("-\xff"), std::string("\0alias", 6) } )
        {
            std::vector<std::string> badArgs{ asUtf8(source), asUtf8(requestFile) };
            badArgs[index] += suffix;
            bool refused{};
            try
            {
                (void)sed_agent::runCommand("model-native-plan", badArgs);
            }
            catch ( const sed_agent::AgentError& error )
            {
                refused = error.code == sed_agent::kExitUsage && error.errorCode == "modelNativeInvalidRequest";
            }
            check(refused, "malformed UTF-8 or embedded NUL cannot alias a captured source or request");
        }
    }
    for ( const auto point : { Fault::write, Fault::flush, Fault::close, Fault::publish } )
    {
        sed::assets::detail::ScopedStagedAssetFileFaultForTests fault(point);
        check(expectFailure(sed_agent::kExitRefusal) && fault.triggered() && !fs::exists(output),
            "native apply propagates injected staging failure without partial output");
    }
    auto revisedRequest = request;
    revisedRequest.replace(revisedRequest.find("\"u\":1"), 5, "\"u\":2");
    writeText(requestFile, revisedRequest);
    check(expectFailure(sed_agent::kExitConflict) && !fs::exists(output), "revised file-backed request invalidates review");
    args[1] = "-";
    {
        GeneratedBuffer input(request, request.size(), std::nullopt, [&]() { writeText(source, original + "# revised\n"); });
        StdinScope scope(input);
        check(expectFailure(sed_agent::kExitConflict, "Source changed or became unavailable: " + asUtf8(source)) && !fs::exists(output),
            "source captured before interactive request is rechecked during publication");
    }
    writeText(source, original);
    {
        GeneratedBuffer input(request, request.size(), std::nullopt, [&]() { writeText(output, "competing writer"); });
        StdinScope scope(input);
        check(expectFailure(sed_agent::kExitRefusal) && sed_agent::readFileBytes(output) == "competing writer",
            "creator racing native apply keeps its complete output");
    }
    check(sed_agent::readFileBytes(source) == original, "publication faults leave native source intact");
}

void testModelSourceChangedDuringRequest(const fs::path& root)
{
    const fs::path source = root / "model.3do";
    const fs::path output = root / "model-lit.3do";
    const std::string original(kModelLightingFixture);
    writeText(source, original);
    const std::string request = "{\"schemaVersion\":1,\"inputSha256\":\""
        + sed_agent::sha256Hex(original) + "\",\"profile\":\"jk\",\"lights\":[]}";
    std::string expected;
    {
        GeneratedBuffer buffer(request, request.size());
        StdinScope input(buffer);
        const auto plan = sed_agent::runCommand("model-lighting-plan", { source.string(), "-" });
        const auto* hash = plan.find("outputSha256");
        check(hash && hash->str.size() == 64, "model source-race control produces a reviewed hash");
        if ( hash )
        {
            expected = hash->str;
        }
    }
    const std::string competing = original + "\n# competing writer\n";
    GeneratedBuffer buffer(request, request.size(), std::nullopt, [&]() { writeText(source, competing); });
    StdinScope input(buffer);
    bool conflict{};
    try
    {
        (void)sed_agent::runCommand("model-lighting-apply", { source.string(), "-", expected, output.string() });
    }
    catch ( const sed_agent::AgentError& error )
    {
        conflict = error.code == sed_agent::kExitConflict;
    }
    check(conflict && !fs::exists(output), "model changed during request preparation refuses publication");
    check(sed_agent::readFileBytes(source) == competing, "model source race preserves the competing writer");
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
        testCommandLocalRequestInput(root);
        testSourceChangedDuringRequest(root, false);
        testSourceChangedDuringRequest(root, true);
        testUnchangedSourceControl(root);
        testModelSourceChangedDuringRequest(root);
        testNativePublication(root);
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
