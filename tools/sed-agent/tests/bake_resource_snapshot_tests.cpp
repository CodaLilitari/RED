#include "../bake_resource_snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

void writeText(const fs::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    if ( !output )
    {
        throw std::runtime_error("Cannot write resource-capture fixture");
    }
}

std::string text(const std::optional<std::vector<std::byte>>& bytes)
{
    return bytes ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : "<missing>";
}

template <typename Operation>
void expectConflict(Operation&& operation, const std::string& message)
{
    bool matched{};
    try
    {
        operation();
    }
    catch ( const sed_agent::detail::ResourceRevisionError& )
    {
        matched = true;
    }
    check(matched, message);
}

void runTests(const fs::path& root)
{
    const auto lower = root / "lower";
    const auto higher = root / "higher";
    fs::create_directory(lower);
    fs::create_directory(higher);
    sed::assets::ResourceLocator locator;
    locator.addRoot(lower);
    locator.addRoot(higher);
    writeText(lower / "wall.mat", "old");
    sed_agent::detail::BakeResourceSnapshot captured(locator, 16u, 32u);
    check(text(captured.read("wall", ".mat")) == "old", "capture preserves extension fallback");
    captured.throwIfReadFailed();
    captured.requireUnchanged();
    writeText(lower / "wall.mat", "new");
    check(text(captured.read("wall", ".mat")) == "old", "repeat reads use the original captured bytes");
    expectConflict([&]() { captured.requireUnchanged(); }, "same-size changed loose resource refuses publication");
    writeText(lower / "wall.mat", "old");
    captured.requireUnchanged();
    writeText(higher / "wall.mat", "old");
    expectConflict([&]() { captured.requireUnchanged(); },
        "new higher-priority loose winner is detected even with identical bytes");

    sed_agent::detail::BakeResourceSnapshot missing(locator, 16u, 32u);
    check(!missing.read("late.mat", ".mat"), "missing resource capture is explicit");
    missing.requireUnchanged();
    writeText(lower / "late.mat", "arrived");
    check(!missing.read("late.mat", ".mat"), "missing capture remains stable during resolution");
    expectConflict([&]() { missing.requireUnchanged(); }, "missing-to-present lookup refuses publication");

    sed_agent::detail::BakeResourceSnapshot removed(locator, 16u, 32u);
    check(text(removed.read("late.mat", ".mat")) == "arrived", "resource exists before deletion");
    fs::remove(lower / "late.mat");
    expectConflict([&]() { removed.requireUnchanged(); }, "deleted loose resource refuses publication");

    writeText(lower / "exact", "too large");
    writeText(lower / "exact.mat", "ok");
    sed_agent::detail::BakeResourceSnapshot limited(locator, 4u, 8u);
    check(!limited.read("exact", ".mat"), "oversized winning name is refused without extension fallback");
    bool limitFailure{};
    try
    {
        limited.throwIfReadFailed();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        limitFailure = error.kind == sed_agent::detail::InputReadError::Kind::LimitExceeded;
    }
    check(limitFailure, "oversized resource failure survives recoverable emissive omission");

    writeText(lower / "first.mat", "abc");
    writeText(lower / "second.mat", "def");
    sed_agent::detail::BakeResourceSnapshot aggregate(locator, 4u, 5u);
    check(text(aggregate.read("first.mat", ".mat")) == "abc", "aggregate budget admits first capture");
    check(text(aggregate.read("first.mat", ".mat")) == "abc", "cache reuse does not consume aggregate budget again");
    check(!aggregate.read("second.mat", ".mat"), "aggregate budget bounds all retained captures");
    limitFailure = false;
    try
    {
        aggregate.throwIfReadFailed();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        limitFailure = error.kind == sed_agent::detail::InputReadError::Kind::LimitExceeded;
    }
    check(limitFailure, "aggregate limit remains an explicit deferred failure");

    sed_agent::detail::BakeResourceSnapshot lookupLimited(locator, 4u, 8u, 1u);
    check(!lookupLimited.read("absent-a.mat", ".mat"), "missing lookup counts toward metadata admission");
    (void)lookupLimited.read("absent-b.mat", ".mat");
    limitFailure = false;
    try
    {
        lookupLimited.throwIfReadFailed();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        limitFailure = error.kind == sed_agent::detail::InputReadError::Kind::LimitExceeded;
    }
    check(limitFailure, "missing lookups cannot grow retained metadata without a bound");

    sed::assets::ResourceLocator broken;
    broken.setPrimaryResourceOverride("broken.mat", []() -> std::optional<std::vector<std::byte>>
    {
        throw std::runtime_error("injected resource read error");
    });
    sed_agent::detail::BakeResourceSnapshot failed(std::move(broken));
    check(!failed.read("broken.mat", ".mat"), "reader failure yields a recoverable resolver input");
    bool readFailure{};
    try
    {
        failed.throwIfReadFailed();
    }
    catch ( const std::runtime_error& error )
    {
        readFailure = std::string(error.what()) == "injected resource read error";
    }
    check(readFailure, "deferred resource error retains its original diagnostic");

    auto commandBytes = std::make_shared<sed_agent::detail::BakeResourceBudget>();
    commandBytes->remainingBytes = 5u;
    commandBytes->remainingLookups = 8u;
    sed_agent::detail::BakeResourceSnapshot firstBake(locator, commandBytes);
    sed_agent::detail::BakeResourceSnapshot secondBake(locator, commandBytes);
    check(text(firstBake.read("first.mat", ".mat")) == "abc"
            && commandBytes->remainingBytes == 2u && commandBytes->remainingLookups == 7u,
        "first bake charges its capture to the shared command budget");
    check(text(firstBake.read("first.mat", ".mat")) == "abc"
            && commandBytes->remainingBytes == 2u && commandBytes->remainingLookups == 7u,
        "cached lookup does not consume the shared budget twice");
    check(!secondBake.read("second.mat", ".mat"),
        "another bake cannot exceed the remaining shared command bytes");
    limitFailure = false;
    try
    {
        secondBake.throwIfReadFailed();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        limitFailure = error.kind == sed_agent::detail::InputReadError::Kind::LimitExceeded;
    }
    check(limitFailure && commandBytes->remainingBytes == 2u && commandBytes->remainingLookups == 7u,
        "failed shared admission retains the prior counters and exposes its limit error");
    firstBake.requireUnchanged();
    check(commandBytes->remainingBytes == 2u && commandBytes->remainingLookups == 7u,
        "publication replay does not reserve retained bytes again");

    auto commandLookups = std::make_shared<sed_agent::detail::BakeResourceBudget>();
    commandLookups->remainingBytes = 16u;
    commandLookups->remainingLookups = 1u;
    sed_agent::detail::BakeResourceSnapshot missingOne(locator, commandLookups);
    sed_agent::detail::BakeResourceSnapshot missingTwo(locator, commandLookups);
    check(!missingOne.read("missing-one.mat", ".mat") && commandLookups->remainingLookups == 0u,
        "missing resource captures consume the shared metadata count");
    check(!missingTwo.read("missing-two.mat", ".mat"), "later bake cannot bypass the shared lookup count");
    limitFailure = false;
    try
    {
        missingTwo.throwIfReadFailed();
    }
    catch ( const sed_agent::detail::InputReadError& error )
    {
        limitFailure = error.kind == sed_agent::detail::InputReadError::Kind::LimitExceeded;
    }
    check(limitFailure, "shared lookup exhaustion is a deferred admission failure");
}

} // namespace

int main()
{
    const auto root = fs::temp_directory_path() / ("red-bake-resources-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try
    {
        runTests(root);
    }
    catch ( const std::exception& error )
    {
        check(false, std::string("unexpected error: ") + error.what());
    }
    fs::remove_all(root);
    std::cout << "bake resource failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
