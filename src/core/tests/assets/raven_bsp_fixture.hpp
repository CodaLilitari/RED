#pragma once

#include <algorithm>
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

// A convex synthetic PLAYERCLIP hull with one oblique side and two eligible owners.
inline Fixture playerClipFixture()
{
    Fixture f;
    f.lumps[1].resize(144);
    f.lumps[1][72] = std::byte{'c'};
    write32(f.lumps[1], 140, 0x80000030);
    const std::array<std::array<float, 4>, 7> planes{{
        {-1,0,0,0}, {1,0,0,8}, {0,-1,0,0}, {0,1,0,8},
        {0,0,-1,0}, {0,0,1,8}, {1,1,0,12},
    }};
    f.lumps[2].resize(7 * 16);
    f.lumps[9].resize(7 * 12);
    for ( std::size_t i = 0; i < planes.size(); ++i )
    {
        for ( std::size_t j = 0; j < 4; ++j )
        {
            writeFloat(f.lumps[2], i * 16 + j * 4, planes[i][j]);
        }
        write32(f.lumps[9], i * 12, i);
        write32(f.lumps[9], i * 12 + 8, UINT32_MAX);
    }
    f.lumps[8].resize(3 * 12);
    for ( int i = 0; i < 3; ++i )
    {
        write32(f.lumps[8], i * 12 + 4, 7);
        write32(f.lumps[8], i * 12 + 8, i == 2 ? 0 : 1);
    }
    write32(f.lumps[7], 36, 3);
    return f;
}

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

// Two adjacent visible triangles with identical source flags and contents, plus one unused shader.
inline raven_fixture::Fixture makeRavenMixedCollisionFixture()
{
    using namespace raven_fixture;
    Fixture fixture;
    auto& shaders = fixture.lumps[1];
    shaders.assign(3 * 72, std::byte{});
    const std::array<std::string_view, 3> names{"structure", "decoration", "unused"};
    for ( std::size_t i = 0; i < names.size(); ++i )
    {
        for ( std::size_t c = 0; c < names[i].size(); ++c )
        {
            shaders[i * 72 + c] = static_cast<std::byte>(names[i][c]);
        }
        write32(shaders, i * 72 + 64, 3);
        write32(shaders, i * 72 + 68, 1);
    }
    auto& surfaces = fixture.lumps[13];
    write32(surfaces, 24, 3);
    write32(surfaces, 148, 1);
    write32(surfaces, 148 + 8, 1);
    write32(surfaces, 148 + 20, 3);
    write32(surfaces, 148 + 24, 3);
    write32(fixture.lumps[11], 12, 1);
    write32(fixture.lumps[11], 16, 3);
    write32(fixture.lumps[11], 20, 4);
    return fixture;
}

// The same original synthetic artwork in a finite two-room BSP partition.
// Model bounds are deliberately wider than the six structural wall planes.
inline std::vector<std::byte> makeRavenBspRoomsFixture(bool separateVisibility = false)
{
    using namespace raven_fixture;
    Fixture fixture;
    for ( std::size_t axis = 0; axis < 3; ++axis )
    {
        writeFloat(fixture.lumps[7], axis * 4, -8);
        writeFloat(fixture.lumps[7], 12 + axis * 4, 80);
    }
    const std::array<std::array<float, 4>, 7> planes{{
        {{-1, 0, 0, 2}}, {{1, 0, 0, 4}}, {{0, -1, 0, 2}}, {{0, 1, 0, 4}},
        {{0, 0, -1, 0}}, {{0, 0, 1, 64}}, {{1, 0, 0, 1}}
    }};
    fixture.lumps[2].resize(planes.size() * 16);
    fixture.lumps[3].resize(planes.size() * 36);
    fixture.lumps[4].resize(9 * 48);
    for ( std::size_t leaf = 0; leaf < 9; ++leaf )
    {
        const auto at = leaf * 48;
        write32(fixture.lumps[4], at, leaf >= 7 ? static_cast<std::uint32_t>(leaf - 7) : UINT32_MAX);
        for ( std::size_t axis = 0; axis < 3; ++axis )
        {
            write32(fixture.lumps[4], at + 8 + axis * 4, static_cast<std::uint32_t>(-8));
            write32(fixture.lumps[4], at + 20 + axis * 4, 80);
        }
        if ( leaf >= 7 )
        {
            write32(fixture.lumps[4], at + 32, static_cast<std::uint32_t>((leaf - 7) * 2));
            write32(fixture.lumps[4], at + 36, 2);
        }
    }
    fixture.lumps[5].resize(16);
    write32(fixture.lumps[5], 4, 1);
    write32(fixture.lumps[5], 12, 1);
    for ( std::size_t i = 0; i < planes.size(); ++i )
    {
        for ( std::size_t value = 0; value < 4; ++value )
        {
            writeFloat(fixture.lumps[2], i * 16 + value * 4, planes[i][value]);
        }
        write32(fixture.lumps[3], i * 36, static_cast<std::uint32_t>(i));
        write32(fixture.lumps[3], i * 36 + 4, static_cast<std::uint32_t>(i == 6 ? -8 : -2 - static_cast<int>(i)));
        write32(fixture.lumps[3], i * 36 + 8, static_cast<std::uint32_t>(i == 6 ? -9 : static_cast<int>(i + 1)));
        for ( std::size_t axis = 0; axis < 3; ++axis )
        {
            write32(fixture.lumps[3], i * 36 + 12 + axis * 4, static_cast<std::uint32_t>(-8));
            write32(fixture.lumps[3], i * 36 + 24 + axis * 4, 80);
        }
    }
    if ( separateVisibility )
    {
        // The first room owns the flat surface, the second owns the curved patch.
        write32(fixture.lumps[4], 7 * 48 + 36, 1);
        write32(fixture.lumps[4], 8 * 48 + 32, 3);
        write32(fixture.lumps[4], 8 * 48 + 36, 1);
        fixture.lumps[16].resize(8 + 2 * 4);
        write32(fixture.lumps[16], 0, 2);
        write32(fixture.lumps[16], 4, 4);
        fixture.lumps[16][8] = std::byte{1};
        fixture.lumps[16][12] = std::byte{2};
    }
    return fixture.bytes();
}

// Entirely synthetic. A 256-unit lightmapped floor triangle whose corners sit on black luxels around a
// bright pool, beside a vertex-lit triangle that shares its long edge. Luxels are 16 source units apart.
inline raven_fixture::Fixture makeRavenLightPoolFixture()
{
    using namespace raven_fixture;
    Fixture fixture;
    writeFloat(fixture.lumps[7], 12, 256);
    writeFloat(fixture.lumps[7], 16, 256);
    writeFloat(fixture.lumps[7], 20, 0);
    const std::array<std::array<float, 2>, 6> corners{{{0, 0}, {256, 0}, {0, 256}, {256, 0}, {256, 256}, {0, 256}}};
    fixture.lumps[10].assign((corners.size() + 1) * 80, std::byte{});
    for ( std::size_t i = 0; i < corners.size(); ++i )
    {
        const auto base = (i + 1) * 80;
        writeFloat(fixture.lumps[10], base, corners[i][0]);
        writeFloat(fixture.lumps[10], base + 4, corners[i][1]);
        writeFloat(fixture.lumps[10], base + 12, corners[i][0] / 64);
        writeFloat(fixture.lumps[10], base + 16, corners[i][1] / 64);
        writeFloat(fixture.lumps[10], base + 20, (corners[i][0] / 16 + 0.5f) / 128);
        writeFloat(fixture.lumps[10], base + 24, (corners[i][1] / 16 + 0.5f) / 128);
        writeFloat(fixture.lumps[10], base + 60, 1);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            fixture.lumps[10][base + 64 + channel] = std::byte{200};
        }
        fixture.lumps[10][base + 67] = std::byte{255};
    }
    fixture.lumps[11].assign(6 * 4, std::byte{});
    for ( std::size_t i = 0; i < 6; ++i )
    {
        write32(fixture.lumps[11], i * 4, static_cast<std::uint32_t>(i % 3));
    }
    for ( std::size_t surface = 0; surface < 2; ++surface )
    {
        const auto base = surface * 148;
        write32(fixture.lumps[13], base + 8, 1);
        write32(fixture.lumps[13], base + 12, static_cast<std::uint32_t>(1 + 3 * surface));
        write32(fixture.lumps[13], base + 16, 3);
        write32(fixture.lumps[13], base + 20, static_cast<std::uint32_t>(3 * surface));
        write32(fixture.lumps[13], base + 24, 3);
        write32(fixture.lumps[13], base + 36, surface == 0 ? 0 : UINT32_MAX);
        write32(fixture.lumps[13], base + 140, 0);
        write32(fixture.lumps[13], base + 144, 0);
    }
    // Luxels 4 to 8 on both axes are white; the triangle's corner luxels (0, 0), (16, 0) and (0, 16) are black.
    fixture.lumps[14].assign(128u * 128u * 3u, std::byte{});
    for ( std::size_t y = 4; y <= 8; ++y )
    {
        for ( std::size_t x = 4; x <= 8; ++x )
        {
            std::fill_n(fixture.lumps[14].begin() + static_cast<std::ptrdiff_t>((y * 128 + x) * 3), 3, std::byte{255});
        }
    }
    return fixture;
}

} // namespace sed::tests
