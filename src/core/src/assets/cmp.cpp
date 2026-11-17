#include "sed/assets/cmp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

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

std::uint8_t nearestCmpColor(const CmpPalette& palette, const double r, const double g, const double b) noexcept
{
    std::uint8_t best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for ( std::size_t i = 0; i < palette.size(); ++i )
    {
        const double dr = palette[i].r - r;
        const double dg = palette[i].g - g;
        const double db = palette[i].b - b;
        const double distance = 0.30 * dr * dr + 0.59 * dg * dg + 0.11 * db * db;
        if ( distance < bestDistance )
        {
            bestDistance = distance;
            best = static_cast<std::uint8_t>(i);
        }
    }
    return best;
}

CmpMasterBuild makeMasterCmp(const std::span<const std::byte> master,
                             const std::span<const CmpPaletteAddition> additions,
                             const std::span<const std::uint8_t> candidateSlots,
                             const std::array<bool, 256>& protectedIndices)
{
    const auto parsed = parseCmp(master);
    if ( !parsed )
    {
        throw std::invalid_argument("the master is not a complete CMP (header, palette and 64 light tables)");
    }
    std::array<bool, 256> masterFullbright{};
    for ( std::size_t i = 0; i < 256; ++i )
    {
        bool self = true;
        for ( const auto& row : parsed->lightLevels )
        {
            self = self && row[i] == i;
        }
        masterFullbright[i] = self;
    }
    std::array<bool, 256> seen{};
    for ( const std::uint8_t slot : candidateSlots )
    {
        if ( seen[slot] )
        {
            throw std::invalid_argument("a candidate slot is listed twice");
        }
        seen[slot] = true;
    }
    CmpMasterBuild build;
    std::array<int, 256> addition{};
    addition.fill(-1);
    for ( const std::uint8_t slot : candidateSlots )
    {
        if ( build.assignedSlots.size() == additions.size() )
        {
            break;
        }
        if ( protectedIndices[slot] || masterFullbright[slot] )
        {
            build.skippedSlots.push_back(slot);
            continue;
        }
        addition[slot] = static_cast<int>(build.assignedSlots.size());
        build.assignedSlots.push_back(slot);
    }
    if ( build.assignedSlots.size() < additions.size() )
    {
        throw std::invalid_argument("only " + std::to_string(build.assignedSlots.size()) + " usable slots for "
                                    + std::to_string(additions.size()) + " colours");
    }
    CmpPalette palette = parsed->palette;
    for ( std::size_t a = 0; a < build.assignedSlots.size(); ++a )
    {
        palette[build.assignedSlots[a]] = additions[a].color;
    }
    const auto changed = [&](const std::uint8_t index) { return addition[index] >= 0; };

    build.bytes.assign(master.begin(), master.end());
    for ( std::size_t i = 0; i < palette.size(); ++i )
    {
        build.bytes[kCmpHeaderSize + i * 3 + 0] = static_cast<std::byte>(palette[i].r);
        build.bytes[kCmpHeaderSize + i * 3 + 1] = static_cast<std::byte>(palette[i].g);
        build.bytes[kCmpHeaderSize + i * 3 + 2] = static_cast<std::byte>(palette[i].b);
    }
    for ( std::size_t level = 0; level < kCmpTranslationTableCount; ++level )
    {
        const double scale = static_cast<double>(level) / 63.0;
        for ( std::size_t i = 0; i < 256; ++i )
        {
            const auto index = static_cast<std::uint8_t>(i);
            const std::uint8_t stock = parsed->lightLevels[level][i];
            if ( !changed(index) && !changed(stock) )
            {
                continue;
            }
            std::uint8_t value{};
            if ( changed(index) && (additions[static_cast<std::size_t>(addition[i])].fullbright || level == 63) )
            {
                value = index;
            }
            else
            {
                value = nearestCmpColor(palette, palette[i].r * scale, palette[i].g * scale, palette[i].b * scale);
            }
            build.bytes[kCmpTranslationOffset + level * kCmpTranslationSize + i] = static_cast<std::byte>(value);
            ++build.lightEntriesComputed;
        }
    }
    constexpr std::size_t kTransparencyOffset = kCmpTranslationOffset + kCmpTranslationTableCount * kCmpTranslationSize;
    const bool hasTransparency = (std::to_integer<std::uint8_t>(master[8]) & 1u) != 0
        && master.size() >= kTransparencyOffset + 256 * 256;
    if ( hasTransparency )
    {
        for ( std::size_t row = 0; row < 256; ++row )
        {
            for ( std::size_t column = 0; column < 256; ++column )
            {
                const std::size_t at = kTransparencyOffset + row * 256 + column;
                const auto stock = std::to_integer<std::uint8_t>(master[at]);
                if ( !changed(static_cast<std::uint8_t>(row)) && !changed(static_cast<std::uint8_t>(column)) && !changed(stock) )
                {
                    continue;
                }
                const auto& front = palette[row];
                const auto& back = palette[column];
                build.bytes[at] = static_cast<std::byte>(nearestCmpColor(palette,
                    0.35 * front.r + 0.65 * back.r, 0.35 * front.g + 0.65 * back.g, 0.35 * front.b + 0.65 * back.b));
                ++build.transparencyEntriesComputed;
            }
        }
    }
    return build;
}

} // namespace sed::assets
