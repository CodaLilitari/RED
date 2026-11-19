#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sed::tests::raven_fixture
{

using Bytes = std::vector<std::byte>;

inline void write32(Bytes& bytes, std::size_t at, std::uint32_t value)
{
    for ( unsigned i = 0; i < 4; ++i )
    {
        bytes.at(at + i) = static_cast<std::byte>((value >> (i * 8)) & 255);
    }
}

inline void writeFloat(Bytes& bytes, std::size_t at, float value)
{
    write32(bytes, at, std::bit_cast<std::uint32_t>(value));
}

struct Fixture
{
    std::array<Bytes, 18> lumps{};

    Fixture()
    {
        for ( char c : std::string_view("{\n\"classname\" \"worldspawn\" \"x\" \"one\" \"x\" \"two\"\n}\n{\"classname\" \"info_player_deathmatch\" \"origin\" \"0 0 32\" \"angle\" \"90\"}") )
        {
            lumps[0].push_back(static_cast<std::byte>(c));
        }
        lumps[0].push_back(std::byte{});
        lumps[1].resize(72);
        lumps[1][0] = std::byte{'a'};
        write32(lumps[1], 68, 1);
        lumps[7].resize(40);
        writeFloat(lumps[7], 12, 2);
        writeFloat(lumps[7], 16, 2);
        writeFloat(lumps[7], 20, 4);
        write32(lumps[7], 28, 2);
        // First vertex is intentionally unused. Surface-local indices start at zero.
        lumps[10].resize(10 * 80);
        for ( int i = 0; i < 9; ++i )
        {
            const auto base = static_cast<std::size_t>(i + 1) * 80;
            writeFloat(lumps[10], base, static_cast<float>(i % 3));
            writeFloat(lumps[10], base + 4, static_cast<float>(i / 3));
            writeFloat(lumps[10], base + 8, i == 4 ? 4.f : 0.f);
            writeFloat(lumps[10], base + 12, static_cast<float>(i % 3) / 2);
            writeFloat(lumps[10], base + 24, static_cast<float>(i / 3) / 2);
            writeFloat(lumps[10], base + 60, 1);
            lumps[10][base + 64] = i == 4 ? std::byte{255} : std::byte{0};
            lumps[10][base + 67] = std::byte{255};
        }
        lumps[11].resize(6 * 4);
        write32(lumps[11], 4, 3);
        write32(lumps[11], 8, 1);
        // The second triangle repeats a point and should disappear.
        lumps[13].resize(2 * 148);
        for ( std::size_t base : {0u, 148u} )
        {
            write32(lumps[13], base + 4, UINT32_MAX);
            write32(lumps[13], base + 8, base == 0 ? 1 : 2);
            write32(lumps[13], base + 12, 1);
            write32(lumps[13], base + 16, 9);
            for ( std::size_t i = 0; i < 4; ++i )
            {
                write32(lumps[13], base + 36 + 4 * i, UINT32_MAX);
            }
        }
        write32(lumps[13], 24, 6);
        write32(lumps[13], 148 + 140, 3);
        write32(lumps[13], 148 + 144, 3);
    }

    Bytes bytes() const
    {
        Bytes out(152);
        out[0] = std::byte{'R'};
        out[1] = std::byte{'B'};
        out[2] = std::byte{'S'};
        out[3] = std::byte{'P'};
        write32(out, 4, 1);
        for ( std::size_t i = 0; i < lumps.size(); ++i )
        {
            write32(out, 8 + 8 * i, static_cast<std::uint32_t>(out.size()));
            write32(out, 12 + 8 * i, static_cast<std::uint32_t>(lumps[i].size()));
            out.insert(out.end(), lumps[i].begin(), lumps[i].end());
            while ( out.size() % 4 != 0 )
            {
                out.push_back(std::byte{});
            }
        }
        return out;
    }
};

} // namespace sed::tests::raven_fixture

namespace sed::tests
{

// Entirely synthetic. Shader a, one world triangle, an FFA spawn, and an optional curved patch.
inline std::vector<std::byte> makeRavenBspFixture(bool includePatch = true)
{
    raven_fixture::Fixture fixture;
    if ( !includePatch )
    {
        fixture.lumps[13].resize(148);
        raven_fixture::write32(fixture.lumps[7], 28, 1);
    }
    return fixture.bytes();
}

} // namespace sed::tests
