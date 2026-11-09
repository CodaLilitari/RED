#include "test_common.hpp"

#include "sed/assets/key.hpp"
#include "../../src/assets/atomic_file_write.hpp"
#include "../../src/assets/key_input.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <streambuf>

namespace sed::tests
{

namespace
{

class KeyGroupedNumbers final : public std::numpunct<char>
{
protected:
    char do_thousands_sep() const override
    {
        return ',';
    }
    std::string do_grouping() const override
    {
        return "\3";
    }
};

class GeneratedKeyInput final : public std::streambuf
{
public:
    explicit GeneratedKeyInput(const std::size_t length) : length_(length)
    {
    }

    std::size_t consumed() const noexcept
    {
        return consumed_;
    }

protected:
    std::streamsize xsgetn(char* output, const std::streamsize requested) override
    {
        const auto count = std::min(length_ - consumed_, static_cast<std::size_t>(requested));
        std::memset(output, '#', count);
        consumed_ += count;
        return static_cast<std::streamsize>(count);
    }

    int_type underflow() override
    {
        return consumed_ == length_ ? traits_type::eof() : traits_type::to_int_type('#');
    }

private:
    std::size_t length_;
    std::size_t consumed_{};
};

} // namespace

void runKeyTests()
{
    {
        const assets::detail::KeyInputLimits small{ 8u, 4u, 2u };
        for ( const std::string source : { std::string{}, std::string("abcd"), std::string("abc\ndef\n"),
                 std::string("abc\ndef"), std::string("a\r\nb\r\n") } )
        {
            std::istringstream input(source);
            check(assets::detail::readKeyInput(input, small) == source,
                "KEY bounded input preserves exact admissible bytes including CRLF and final lines");
        }
        std::istringstream tooLong("abcde");
        checkThrowsWithMessage(
            [&]() { (void)assets::detail::readKeyInput(tooLong, small); },
            "KEY source exceeds its line length limit.",
            "KEY bounded input refuses an overlong unterminated line");
        std::istringstream tooMany("a\nb\nc");
        checkThrowsWithMessage(
            [&]() { (void)assets::detail::readKeyInput(tooMany, small); },
            "KEY source exceeds its line count limit.",
            "KEY bounded input counts the final unterminated record");
        std::istringstream tooManyBlank("\n\n\n");
        checkThrowsWithMessage(
            [&]() { (void)assets::detail::readKeyInput(tooManyBlank, small); },
            "KEY source exceeds its line count limit.",
            "KEY bounded input includes blank records in work admission");
        GeneratedKeyInput oversized(100u);
        std::istream oversizedStream(&oversized);
        checkThrowsWithMessage(
            [&]() { (void)assets::detail::readKeyInput(oversizedStream, small); },
            "KEY source exceeds its byte limit.",
            "KEY byte admission rejects an unbounded source before grammar parsing");
        check(oversized.consumed() == small.maxBytes + 1u,
            "KEY byte admission consumes only its limit and one excess probe byte");
        std::istringstream zeroInput;
        check(assets::detail::readKeyInput(zeroInput, { 0u, 0u, 0u }).empty(),
            "KEY zero budget admits a truly empty source");
        std::istringstream nonemptyZero("x");
        checkThrowsWithMessage(
            [&]() { (void)assets::detail::readKeyInput(nonemptyZero, { 0u, 0u, 0u }); },
            "KEY source exceeds its byte limit.",
            "KEY zero budget refuses any actual source byte");
        std::istringstream overlongPublic(std::string(64u * 1024u + 1u, '#'));
        checkThrowsWithMessage(
            [&]() { (void)assets::loadFromKEY(overlongPublic); },
            "KEY source exceeds its line length limit.",
            "KEY public stream entrypoint applies bounded admission before stripping comments");
    }

    const char* key =
        R"KEY(
            SECTION: HEADER
            FLAGS 0x10
            TYPE 0x0
            FRAMES 100
            FPS 30
            JOINTS 1

            SECTION: KEYFRAME
            NODES 1
            NODE 0
            MESH NAME mesh0
            ENTRIES 1
            0: 0 0x1 1 2 3 10 20 30
            0.1 0.2 0.3 1 2 3
        )KEY";

    std::istringstream iss(key);
    const sed::assets::KeyFile kf = sed::assets::loadFromKEY(iss);

    check(kf.flags == 0x10u, "KEY: flags");
    check(kf.frames == 100, "KEY: frames");
    check(kf.fps == 30, "KEY: fps");
    check(kf.nodes.size() == 1, "KEY: nodes");
    check(kf.nodes.size() == 1 && kf.nodes[0].meshName == "mesh0", "KEY: mesh name");
    check(kf.nodes.size() == 1 && kf.nodes[0].entries.size() == 1, "KEY: entries");

    if (!kf.nodes.empty() && !kf.nodes[0].entries.empty())
    {
        const auto& entry = kf.nodes[0].entries[0];
        check(entry.label == "0:", "KEY: entry label");
        check(entry.frame == 0, "KEY: entry frame");
        check(entry.flags == 1u, "KEY: entry flags");
        check(closeVec(entry.position, sed::TVector{ 1.0, 2.0, 3.0 }), "KEY: entry position");
        check(closeVec(entry.rotation, sed::TVector{ 10.0, 20.0, 30.0 }), "KEY: entry rotation");
        check(closeVec(entry.dPosition, sed::TVector{ 0.1, 0.2, 0.3 }, kRotationEpsilon), "KEY: entry dPosition");
        check(closeVec(entry.dRotation, sed::TVector{ 1.0, 2.0, 3.0 }), "KEY: entry dRotation");
    }

    {
        const char* const oversized = R"KEY(
            SECTION: KEYFRAME
            NODES 2147483647
        )KEY";
        std::istringstream oversizedInput(oversized);
        const sed::assets::KeyFile oversizedFile =
            sed::assets::loadFromKEY(oversizedInput);
        check(oversizedFile.nodes.empty(),
            "KEY: declared node count is bounded by available input");
    }

    {
        const char* const truncated = R"KEY(
            SECTION: HEADER
            FLAGS not_hex
            FRAMES not_an_integer
            FPS 60
            SECTION: KEYFRAME
            NODES 1
            NODE 0
            MESH NAME mesh0
            ENTRIES 2147483647
        )KEY";
        std::istringstream truncatedInput(truncated);
        const sed::assets::KeyFile truncatedFile =
            sed::assets::loadFromKEY(truncatedInput);
        check(truncatedFile.flags == 0
                && truncatedFile.frames == 0
                && truncatedFile.fps == 60,
            "KEY: malformed optional header values use documented defaults");
        check(truncatedFile.nodes.size() == 1
                && truncatedFile.nodes[0].entries.empty(),
            "KEY: declared entry count is bounded by available input");
    }

    checkThrowsWithMessage(
        []()
        {
            (void)sed::assets::loadFromKEY(
                std::filesystem::path("build/test-temp/core/missing.key"));
        },
        "Failed to open KEY file.",
        "KEY: missing path reports an actionable open failure");

    {
        std::istringstream source(key);
        const sed::assets::KeyFile original = sed::assets::loadFromKEY(source);
        const std::string written = sed::assets::saveToKEYText(original);
        std::istringstream reread(written);
        const sed::assets::KeyFile roundTrip = sed::assets::loadFromKEY(reread);
        check(roundTrip.flags == original.flags && roundTrip.frames == original.frames
                && roundTrip.fps == original.fps && roundTrip.nodes.size() == 1,
            "KEY: writer output loads back with the same header");
        check(roundTrip.nodes.size() == 1 && roundTrip.nodes[0].entries.size() == 1
                && roundTrip.nodes[0].meshName == "mesh0"
                && closeVec(roundTrip.nodes[0].entries[0].position, sed::TVector{ 1.0, 2.0, 3.0 })
                && closeVec(roundTrip.nodes[0].entries[0].dRotation, sed::TVector{ 1.0, 2.0, 3.0 }),
            "KEY: writer output preserves entries exactly");
    }

    {
        using Fault = sed::assets::detail::StagedAssetFileFaultPointForTests;
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::temp_directory_path() / ("sed-key-atomic-" + stamp);
        std::filesystem::create_directory(directory);
        const auto destination = directory / "animation.key";
        const auto otherWriter = directory / ".sed-save-other-owner.tmp";
        const auto write = [](const std::filesystem::path& path, const std::string& bytes)
        {
            std::ofstream output(path, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.close();
            check(static_cast<bool>(output), "KEY atomic save creates its synthetic destination fixture");
        };
        const auto read = [](const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        };
        write(otherWriter, "another writer's staging bytes");
        std::istringstream original(key);
        auto changed = sed::assets::loadFromKEY(original);
        changed.flags = 7u;
        const auto noOwnedStaging = [&]()
        {
            for ( const auto& entry : std::filesystem::directory_iterator(directory) )
            {
                check(entry.path() == destination || entry.path() == otherWriter,
                    "KEY save leaves no owned staging file after success or failure");
            }
            check(read(otherWriter) == "another writer's staging bytes",
                "KEY staging cleanup preserves another writer's file");
        };
        for ( const auto point : { Fault::write, Fault::flush, Fault::close, Fault::publish } )
        {
            write(destination, key);
            sed::assets::detail::ScopedStagedAssetFileFaultForTests fault(point);
            bool failed{};
            try
            {
                sed::assets::saveToKEY(changed, destination);
            }
            catch ( const std::exception& )
            {
                failed = true;
            }
            check(fault.triggered(), "KEY save exercises the requested staged IO failure");
            check(failed, "KEY save reports write/flush/close/publication failure");
            check(read(destination) == key, "KEY failed save retains the previous complete destination bytes");
            noOwnedStaging();
        }
        sed::assets::saveToKEY(changed, destination);
        check(read(destination) == sed::assets::saveToKEYText(changed),
            "KEY successful atomic save publishes all canonical output bytes");
        check(sed::assets::loadFromKEY(destination).flags == changed.flags,
            "KEY successful atomic replacement reopens as the authored animation");
        noOwnedStaging();
        std::filesystem::remove(destination);
        std::filesystem::remove(otherWriter);
        std::filesystem::remove(directory);
    }

    {
        sed::assets::KeyFile editable{};
        editable.frames = 100;
        editable.fps = 30;
        editable.nodes.push_back(sed::assets::KeyNode{ "mesh0", {} });

        sed::assets::KeyEntry late{};
        late.label = "FRAME";
        late.frame = 50;
        late.position = sed::TVector{ 5.0, 0.0, 0.0 };
        sed::assets::KeyEntry early{};
        early.label = "FRAME";
        early.frame = 10;
        early.position = sed::TVector{ 1.0, 0.0, 0.0 };

        check(sed::assets::keyInsertEntry(editable, 0, late)
                && sed::assets::keyInsertEntry(editable, 0, early)
                && editable.nodes[0].entries.size() == 2
                && editable.nodes[0].entries[0].frame == 10
                && editable.nodes[0].entries[1].frame == 50,
            "KEY timeline: inserts stay ordered by frame");

        sed::assets::KeyEntry beyond = early;
        beyond.frame = 101;
        check(!sed::assets::keyInsertEntry(editable, 0, beyond)
                && editable.nodes[0].entries.size() == 2,
            "KEY timeline: an insert past the frame count is refused");
        check(!sed::assets::keyInsertEntry(editable, 9, early)
                && editable.nodes[0].entries.size() == 2,
            "KEY timeline: an insert into an unknown node is refused");

        check(sed::assets::keyEraseEntry(editable, 0, 0)
                && editable.nodes[0].entries.size() == 1
                && editable.nodes[0].entries[0].frame == 50,
            "KEY timeline: erase removes the addressed entry");
        check(!sed::assets::keyEraseEntry(editable, 0, 4)
                && editable.nodes[0].entries.size() == 1,
            "KEY timeline: an out-of-range erase is refused");

        sed::assets::KeyEntry equal = early;
        equal.position.x = 2.0;
        editable.nodes[0].entries = { late, early, equal };
        sed::assets::KeyEntry inserted = early;
        inserted.position.x = 3.0;
        check(sed::assets::keyInsertEntry(editable, 0, inserted)
                && editable.nodes[0].entries.size() == 4
                && editable.nodes[0].entries[0].frame == 10
                && editable.nodes[0].entries[0].position.x == 1.0
                && editable.nodes[0].entries[1].frame == 10
                && editable.nodes[0].entries[1].position.x == 2.0
                && editable.nodes[0].entries[2].frame == 10
                && editable.nodes[0].entries[2].position.x == 3.0
                && editable.nodes[0].entries[3].frame == 50,
            "KEY timeline: insertion orders an unsorted track and preserves equal-frame stability");
    }

    {
        // Original synthetic fixture shaped after the engine's KEY grammar.
        // Sparse NODE indices refer to the declared JOINTS table, not vector order.
        const char* const engineKey = R"KEY(SECTION: HEADER
FLAGS 10
TYPE 0xA
FRAMES 100
FPS 29.5
JOINTS 12
SECTION: MARKERS
MARKERS 2
2.5 1
25.75 6
SECTION: KEYFRAME NODES
NODES 2
NODE 3
MESH NAME torso
ENTRIES 1
0: 2.5 0x1 1 2 3 10 20 30
0.1 0.2 0.3 1 2 3
NODE 9
MESH NAME hand
ENTRIES 1
0: 25.75 0x2 4 5 6 40 50 60
0.4 0.5 0.6 4 5 6
)KEY";
        std::istringstream source(engineKey);
        const sed::assets::KeyFile parsed = sed::assets::loadFromKEY(source);
        check(parsed.serializationSafe && parsed.type == 0xAu && parsed.joints == 12
                && parsed.nodes.size() == 2 && parsed.nodes[0].nodeIndex == 3
                && parsed.nodes[1].nodeIndex == 9 && parsed.markers.size() == 2,
            "Engine KEY exposes retained metadata and sparse node identities");
        check(parsed.flags == 10u && close(parsed.fps, 29.5),
            "Engine KEY retains decimal flags and fractional FPS");
        check(parsed.nodes.size() == 2 && parsed.nodes[0].entries.size() == 1
                && parsed.nodes[1].entries.size() == 1
                && close(parsed.nodes[0].entries[0].frame, 2.5)
                && close(parsed.nodes[1].entries[0].frame, 25.75),
            "Engine KEY retains fractional frame positions");
        const std::string written = sed::assets::saveToKEYText(parsed);
        {
            const std::locale savedLocale = std::locale();
            struct RestoreLocale
            {
                std::locale saved;
                ~RestoreLocale()
                {
                    std::locale::global(saved);
                }
            } restore{ savedLocale };
            std::locale::global(std::locale(savedLocale, new KeyGroupedNumbers));
            sed::assets::KeyFile longAnimation = parsed;
            longAnimation.frames = 10000;
            check(sed::assets::saveToKEYText(longAnimation).find("FRAMES 10000\n") != std::string::npos,
                "KEY numeric output ignores the process locale's digit grouping");
        }
        {
            sed::assets::KeyFile invalidName = parsed;
            invalidName.nodes[0].meshName = "torso\vextra";
            checkThrowsWithMessage(
                [&invalidName]() { (void)sed::assets::saveToKEYText(invalidName); },
                "KEY contains unsupported or malformed data; refusing lossy serialization.",
                "KEY authored mesh names reject all engine whitespace delimiters");
        }
        check(written.find("TYPE 0xA\n") != std::string::npos
                && written.find("JOINTS 12\n") != std::string::npos,
            "Engine KEY writer preserves animation type and joint count");
        check(written.find("NODE 3\n") != std::string::npos
                && written.find("NODE 9\n") != std::string::npos,
            "Engine KEY writer preserves sparse hierarchy node identities");
        const std::size_t markers = written.find("SECTION: MARKERS\n");
        const std::size_t nodes = written.find("SECTION: KEYFRAME NODES\n");
        check(markers != std::string::npos && nodes != std::string::npos && markers < nodes
                && written.find("MARKERS 2\n2.5 1\n25.75 6\n") != std::string::npos,
            "Engine KEY writer preserves marker events before keyframe nodes");
        check(written.find("\nEND\n") == std::string::npos
                && written.find("0: 2.5 0x1 ") != std::string::npos,
            "Engine KEY writer uses numeric entry labels without synthetic END separators");
        std::istringstream roundTripInput(written);
        const std::string secondWrite = sed::assets::saveToKEYText(sed::assets::loadFromKEY(roundTripInput));
        check(secondWrite == written, "Engine KEY semantic round trip is stable");

        std::istringstream extended(std::string(engineKey) + "SECTION: EXTENSION\nPRIVATE_MODE 7\n");
        const sed::assets::KeyFile unsupported = sed::assets::loadFromKEY(extended);
        checkThrowsWithMessage(
            [&unsupported]() { (void)sed::assets::saveToKEYText(unsupported); },
            "KEY contains unsupported or malformed data; refusing lossy serialization.",
            "KEY writer refuses to silently discard an unknown section");

        std::istringstream incomplete("SECTION: HEADER\nFRAMES 100\nFPS 30\nSECTION: KEYFRAME NODES\nNODES 0\n");
        const sed::assets::KeyFile missingFields = sed::assets::loadFromKEY(incomplete);
        checkThrowsWithMessage(
            [&missingFields]() { (void)sed::assets::saveToKEYText(missingFields); },
            "KEY contains unsupported or malformed data; refusing lossy serialization.",
            "KEY writer refuses imported data missing required engine header fields");

        const std::array<std::pair<std::string_view, std::string_view>, 9> invalidReplacements{{
            { "JOINTS 12", "JOINTS 9" },
            { "NODE 9", "NODE 3" },
            { "FPS 29.5", "FPS nan" },
            { "0: 2.5", "0: nan" },
            { "MARKERS 2", "MARKERS 9" },
            { "2.5 1", "2.5 invalid" },
            { "TYPE 0xA", "TYPE 0xA\nUNKNOWN 42" },
            { "0.1 0.2 0.3 1 2 3", "0.1 0.2 0.3 1 2" },
            { "ENTRIES 1", "ENTRIES 1 ignored" }
        }};
        for ( const auto& [before, after] : invalidReplacements )
        {
            std::string malformed(engineKey);
            malformed.replace(malformed.find(before), before.size(), after);
            std::istringstream malformedInput(malformed);
            sed::assets::KeyFile unsafe = sed::assets::loadFromKEY(malformedInput);
            check(!unsafe.serializationSafe,
                "KEY malformed fields, references and unknown data disable serialization");
            checkThrowsWithMessage(
                [&unsafe]() { (void)sed::assets::saveToKEYText(unsafe); },
                "KEY contains unsupported or malformed data; refusing lossy serialization.",
                "KEY refuses invalid imports instead of saving repaired defaults");
            const std::size_t entryCount = unsafe.nodes[0].entries.size();
            check(!sed::assets::keyInsertEntry(unsafe, 0, parsed.nodes[0].entries[0])
                    && !sed::assets::keyEraseEntry(unsafe, 0, 0)
                    && unsafe.nodes[0].entries.size() == entryCount,
                "KEY timeline refuses mutation of incomplete or unsupported imports");
        }

        sed::assets::KeyFile editable = parsed;
        sed::assets::KeyEntry fractional = editable.nodes[0].entries[0];
        fractional.frame = 1.25;
        check(sed::assets::keyInsertEntry(editable, 0, fractional)
                && editable.nodes[0].entries[0].frame == 1.25
                && editable.nodes[0].entries[1].frame == 2.5
                && editable.nodes[0].nodeIndex == 3 && editable.markers.size() == 2,
            "KEY timeline orders fractional frames and retains unrelated metadata");
        fractional.frame = std::numeric_limits<double>::quiet_NaN();
        check(!sed::assets::keyInsertEntry(editable, 0, fractional)
                && editable.nodes[0].entries.size() == 2,
            "KEY timeline refuses non-finite frame positions without mutation");
        editable.flags = 0x80000000u;
        const std::string highFlags = sed::assets::saveToKEYText(editable);
        std::istringstream highFlagsInput(highFlags);
        check(highFlags.find("FLAGS -2147483648\n") != std::string::npos
                && sed::assets::loadFromKEY(highFlagsInput).flags == 0x80000000u,
            "KEY flags use the engine's signed decimal spelling without losing bits");

        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto destination = std::filesystem::temp_directory_path() / ("sed_key_refusal_" + suffix + ".key");
        {
            std::ofstream existing(destination, std::ios::binary);
            existing << engineKey;
        }
        checkThrowsWithMessage(
            [&unsupported, &destination]() { sed::assets::saveToKEY(unsupported, destination); },
            "KEY contains unsupported or malformed data; refusing lossy serialization.",
            "KEY path save validates unsupported input before opening the destination");
        {
            std::ifstream existing(destination, std::ios::binary);
            const std::string retained((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
            check(retained == engineKey, "KEY refused save leaves existing destination bytes unchanged");
        }
        std::error_code cleanupError;
        std::filesystem::remove(destination, cleanupError);
        check(!cleanupError, "KEY refusal test removes its isolated temporary file");
    }
}

} // namespace sed::tests
