#include "sed/assets/cmp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace sed::assets
{

namespace
{

constexpr std::size_t kCmpHeaderSize = 64;
constexpr std::size_t kCmpPaletteSize = 256 * 3;
constexpr std::size_t kCmpTranslationTableCount = 64;
constexpr std::size_t kCmpTranslationSize = 256;
constexpr std::size_t kCmpTranslationOffset = kCmpHeaderSize + kCmpPaletteSize;
constexpr std::size_t kCmpFinalTranslationOffset = kCmpTranslationOffset
    + (kCmpTranslationTableCount - 1) * kCmpTranslationSize;

} // namespace

std::optional<CmpFile> parseCmp(const std::span<const std::byte> data) noexcept
{
    if (data.size() < kCmpFinalTranslationOffset + kCmpTranslationSize)
    {
        return std::nullopt;
    }

    const char* sig = reinterpret_cast<const char*>(data.data());
    if (!(sig[0] == 'C' && sig[1] == 'M' && sig[2] == 'P' && sig[3] == ' '))
    {
        return std::nullopt;
    }

    CmpFile file{};
    const std::byte* p = data.data() + kCmpHeaderSize;

    for (std::size_t i = 0; i < file.palette.size(); ++i)
    {
        file.palette[i].r = std::to_integer<std::uint8_t>(p[i * 3 + 0]);
        file.palette[i].g = std::to_integer<std::uint8_t>(p[i * 3 + 1]);
        file.palette[i].b = std::to_integer<std::uint8_t>(p[i * 3 + 2]);
    }

    for (std::size_t level = 0; level < file.lightLevels.size(); ++level)
    {
        for (std::size_t index = 0;
            index < file.lightLevels[level].size();
            ++index)
        {
            file.lightLevels[level][index] = std::to_integer<std::uint8_t>(
                data[kCmpTranslationOffset + level * kCmpTranslationSize + index]);
        }
    }
    file.translation = file.lightLevels.back();

    return file;
}

std::optional<CmpTint> parseCmpTint(const std::span<const std::byte> data) noexcept
{
    constexpr std::size_t kTintOffset = 12;
    if ( data.size() < kTintOffset + 3 * sizeof(float) )
    {
        return std::nullopt;
    }
    const char* sig = reinterpret_cast<const char*>(data.data());
    if ( !(sig[0] == 'C' && sig[1] == 'M' && sig[2] == 'P' && sig[3] == ' ') )
    {
        return std::nullopt;
    }
    std::array<float, 3> channels{};
    for ( std::size_t c = 0; c < channels.size(); ++c )
    {
        std::uint32_t bits{};
        for ( std::size_t i = 0; i < 4; ++i )
        {
            bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[kTintOffset + c * 4 + i])) << (8 * i);
        }
        std::memcpy(&channels[c], &bits, sizeof bits);
        if ( !std::isfinite(channels[c]) || channels[c] < 0.0f )
        {
            return std::nullopt;
        }
    }
    return CmpTint{ channels[0], channels[1], channels[2] };
}

std::vector<std::byte> makeTintedCmp(const std::span<const std::byte> master, const CmpTint& tint)
{
    const auto parsed = parseCmp(master);
    if ( !parsed )
    {
        throw std::invalid_argument("the master is not a complete CMP (header, palette and 64 light tables)");
    }
    const std::array<float, 3> channels{ tint.r, tint.g, tint.b };
    for ( const float channel : channels )
    {
        if ( !std::isfinite(channel) || channel < 0.0f )
        {
            throw std::invalid_argument("a tint channel must be a finite number of at least 0");
        }
    }
    constexpr std::size_t kTintedSize = kCmpTranslationOffset + kCmpTranslationTableCount * kCmpTranslationSize;
    constexpr std::size_t kTransparencySize = 256 * 256;
    // The transparency table blends two palette entries, and the palette is
    // the master's, so the master's table is right for the tinted map too.
    // Carrying it means a translucent face in a tinted sector finds a table
    // whichever colormap the 8-bit renderer reads it from.
    const bool masterHasTransparency = (std::to_integer<std::uint8_t>(master[8]) & 1u) != 0
        && master.size() >= kTintedSize + kTransparencySize;
    std::vector<std::byte> out(kTintedSize + (masterHasTransparency ? kTransparencySize : 0));
    std::memcpy(out.data(), master.data(), kCmpTranslationOffset);
    for ( std::size_t i = 8; i < 12; ++i ) out[i] = std::byte{ 0 };
    if ( masterHasTransparency )
    {
        out[8] = std::byte{ 1 };
        std::memcpy(out.data() + kTintedSize, master.data() + kTintedSize, kTransparencySize);
    }
    for ( std::size_t c = 0; c < channels.size(); ++c )
    {
        std::uint32_t bits{};
        std::memcpy(&bits, &channels[c], sizeof bits);
        for ( std::size_t i = 0; i < 4; ++i )
        {
            out[12 + c * 4 + i] = static_cast<std::byte>((bits >> (8 * i)) & 0xFFu);
        }
    }
    const CmpPalette& palette = parsed->palette;
    // Each lit master colour is an index, so 256 nearest searches cover all
    // 64 levels.
    std::array<std::uint8_t, 256> nearest{};
    for ( std::size_t source = 0; source < palette.size(); ++source )
    {
        const double target[3] = { palette[source].r * static_cast<double>(tint.r),
                                   palette[source].g * static_cast<double>(tint.g),
                                   palette[source].b * static_cast<double>(tint.b) };
        double best = std::numeric_limits<double>::infinity();
        for ( std::size_t candidate = 0; candidate < palette.size(); ++candidate )
        {
            const double dr = palette[candidate].r - target[0];
            const double dg = palette[candidate].g - target[1];
            const double db = palette[candidate].b - target[2];
            const double distance = dr * dr + dg * dg + db * db;
            if ( distance < best )
            {
                best = distance;
                nearest[source] = static_cast<std::uint8_t>(candidate);
            }
        }
    }
    for ( std::size_t level = 0; level < parsed->lightLevels.size(); ++level )
    {
        for ( std::size_t index = 0; index < 256; ++index )
        {
            out[kCmpTranslationOffset + level * kCmpTranslationSize + index]
                = static_cast<std::byte>(nearest[parsed->lightLevels[level][index]]);
        }
    }
    return out;
}

std::optional<CmpPalette> parseCmpPalette(const std::span<const std::byte> data) noexcept
{
    if (data.size() < kCmpHeaderSize + kCmpPaletteSize)
    {
        return std::nullopt;
    }
    const char* signature = reinterpret_cast<const char*>(data.data());
    if (!(signature[0] == 'C' && signature[1] == 'M'
            && signature[2] == 'P' && signature[3] == ' '))
    {
        return std::nullopt;
    }

    CmpPalette palette{};
    const std::byte* bytes = data.data() + kCmpHeaderSize;
    for (std::size_t index = 0; index < palette.size(); ++index)
    {
        palette[index].r = std::to_integer<std::uint8_t>(bytes[index * 3 + 0]);
        palette[index].g = std::to_integer<std::uint8_t>(bytes[index * 3 + 1]);
        palette[index].b = std::to_integer<std::uint8_t>(bytes[index * 3 + 2]);
    }
    return palette;
}

CmpPalette applyCmpTranslation(
    const CmpPalette& masterPalette,
    const CmpTranslation& translation) noexcept
{
    CmpPalette result{};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        result[index] = masterPalette[translation[index]];
    }
    return result;
}

CmpLightRamp makeCmpLightRamp(
    const CmpFile& cmp,
    const std::uint8_t paletteIndex) noexcept
{
    CmpLightRamp ramp{};
    for (std::size_t level = 0; level < ramp.size(); ++level)
    {
        ramp[level] = cmp.palette[cmp.lightLevels[level][paletteIndex]];
    }
    return ramp;
}

CmpPalette makeGrayscalePalette() noexcept
{
    CmpPalette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i)
    {
        const auto v = static_cast<std::uint8_t>(i);
        palette[i] = CmpColor{ v, v, v };
    }
    return palette;
}

} // namespace sed::assets
