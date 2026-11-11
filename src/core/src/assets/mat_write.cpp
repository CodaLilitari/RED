#include "sed/assets/mat_write.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sed::assets
{
namespace
{

void putI32(std::string& out, const std::int32_t value)
{
    const auto u = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) out += static_cast<char>((u >> shift) & 0xFFu);
}

struct Rgbf
{
    float r{};
    float g{};
    float b{};
};

// Nearest allowed palette entry by weighted RGB distance (a cheap
// approximation of perceived difference).
class PaletteMap
{
public:
    PaletteMap(const CmpPalette& palette, const std::array<bool, 256>& allowed) : palette_(palette)
    {
        for (int i = 0; i < 256; ++i)
            if (allowed[static_cast<std::size_t>(i)]) indices_.push_back(i);
        if (indices_.empty()) throw std::invalid_argument("MAT: no palette index is allowed");
    }

    int nearest(const Rgbf& c) const noexcept
    {
        int best = indices_.front();
        float bestDistance = 1e30f;
        for (const int i : indices_)
        {
            const CmpColor& p = palette_[static_cast<std::size_t>(i)];
            const float dr = c.r - p.r;
            const float dg = c.g - p.g;
            const float db = c.b - p.b;
            const float rm = (c.r + p.r) * 0.5f;
            const float d = (2.0f + rm / 256.0f) * dr * dr + 4.0f * dg * dg + (2.0f + (255.0f - rm) / 256.0f) * db * db;
            if (d < bestDistance)
            {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }

    const CmpColor& colour(const int index) const noexcept { return palette_[static_cast<std::size_t>(index)]; }

private:
    const CmpPalette& palette_;
    std::vector<int> indices_;
};

std::vector<Rgbf> toFloat(const RgbaImage& image)
{
    std::vector<Rgbf> out(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = { static_cast<float>(image.rgba[i * 4]), static_cast<float>(image.rgba[i * 4 + 1]),
                   static_cast<float>(image.rgba[i * 4 + 2]) };
    return out;
}

std::vector<Rgbf> halve(const std::vector<Rgbf>& src, const int w, const int h)
{
    const int nw = w / 2;
    const int nh = h / 2;
    std::vector<Rgbf> out(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh));
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x)
        {
            Rgbf s{};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const Rgbf& p = src[static_cast<std::size_t>((y * 2 + dy) * w + x * 2 + dx)];
                    s.r += p.r;
                    s.g += p.g;
                    s.b += p.b;
                }
            out[static_cast<std::size_t>(y * nw + x)] = { s.r / 4.0f, s.g / 4.0f, s.b / 4.0f };
        }
    return out;
}

std::vector<std::uint8_t> mapToPalette(std::vector<Rgbf> pixels, const int w, const int h, const PaletteMap& map,
    const bool dither, double* errorSum)
{
    std::vector<std::uint8_t> out(pixels.size());
    const std::vector<Rgbf> original = pixels;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * w + x);
            Rgbf c = pixels[i];
            c.r = std::clamp(c.r, 0.0f, 255.0f);
            c.g = std::clamp(c.g, 0.0f, 255.0f);
            c.b = std::clamp(c.b, 0.0f, 255.0f);
            const int index = map.nearest(c);
            out[i] = static_cast<std::uint8_t>(index);
            const CmpColor& p = map.colour(index);
            if (errorSum)
                *errorSum += (std::abs(original[i].r - p.r) + std::abs(original[i].g - p.g)
                    + std::abs(original[i].b - p.b)) / 3.0;
            if (!dither) continue;
            const Rgbf e{ c.r - p.r, c.g - p.g, c.b - p.b };
            const auto spread = [&](const int dx, const int dy, const float f)
            {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || nx >= w || ny >= h) return;
                Rgbf& q = pixels[static_cast<std::size_t>(ny * w + nx)];
                q.r += e.r * f;
                q.g += e.g * f;
                q.b += e.b * f;
            };
            spread(1, 0, 7.0f / 16.0f);
            spread(-1, 1, 3.0f / 16.0f);
            spread(0, 1, 5.0f / 16.0f);
            spread(1, 1, 1.0f / 16.0f);
        }
    return out;
}

} // namespace

IndexedMatResult buildIndexedMat(const std::vector<RgbaImage>& cels, const CmpPalette& palette,
    const IndexedMatOptions& options)
{
    // The engine keeps 16 texinfos per material (retail overflows its
    // arrays beyond that; OpenJKDF2 drops the material).
    if (cels.empty() || cels.size() > 16)
        throw std::invalid_argument("MAT: give 1 to 16 cel images");
    const int w = cels.front().width;
    const int h = cels.front().height;
    const auto pow2 = [](const int v) { return v >= 8 && v <= 1024 && std::has_single_bit(static_cast<unsigned>(v)); };
    if (!pow2(w) || !pow2(h))
        throw std::invalid_argument("MAT: width and height must be powers of two from 8 to 1024 (got "
            + std::to_string(w) + "x" + std::to_string(h) + ")");
    for (const RgbaImage& cel : cels)
    {
        if (cel.width != w || cel.height != h)
            throw std::invalid_argument("MAT: every cel must have the same size");
        if (cel.rgba.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u)
            throw std::invalid_argument("MAT: cel pixel data does not match its size");
    }
    const int maxMips = std::bit_width(static_cast<unsigned>(std::min(w, h))) - 3; // smallest level 8 px
    const int mips = std::clamp(options.mipmaps, 1, std::max(1, std::min(maxMips, 4)));
    const PaletteMap map(palette, options.allowed);

    IndexedMatResult result;
    std::string& out = result.bytes;
    // Header: "MAT ", version 0x32, texture type 2, cel and texture counts,
    // then the 56-byte colour info of an 8-bit indexed MAT (mode 0, bpp 8).
    out += "MAT ";
    putI32(out, 0x32);
    putI32(out, 2);
    putI32(out, static_cast<std::int32_t>(cels.size()));
    putI32(out, static_cast<std::int32_t>(cels.size()));
    putI32(out, 0);
    putI32(out, 8);
    for (int i = 0; i < 12; ++i) putI32(out, 0);

    std::vector<std::vector<std::uint8_t>> celData;
    std::array<bool, 256> used{};
    double errorSum = 0.0;
    std::size_t errorCount = 0;
    std::vector<int> solid;
    for (const RgbaImage& cel : cels)
    {
        std::vector<Rgbf> level = toFloat(cel);
        Rgbf mean{};
        for (const Rgbf& p : level)
        {
            mean.r += p.r;
            mean.g += p.g;
            mean.b += p.b;
        }
        const float n = static_cast<float>(level.size());
        solid.push_back(map.nearest({ mean.r / n, mean.g / n, mean.b / n }));
        std::vector<std::uint8_t> bytes;
        int lw = w;
        int lh = h;
        for (int m = 0; m < mips; ++m)
        {
            double* sum = m == 0 ? &errorSum : nullptr;
            const auto mapped = mapToPalette(level, lw, lh, map, options.dither, sum);
            if (m == 0) errorCount += mapped.size();
            for (const std::uint8_t index : mapped) used[index] = true;
            bytes.insert(bytes.end(), mapped.begin(), mapped.end());
            if (m + 1 < mips)
            {
                level = halve(level, lw, lh);
                lw /= 2;
                lh /= 2;
            }
        }
        celData.push_back(std::move(bytes));
    }
    // Texture headers: type 8, the cel's mean colour index, the words the
    // stock JK MATs carry (1.0f x4, 0, 0, 0xBFF78482), and last the index of
    // the texture this cel draws. Both engines point cel i at textures[word 9],
    // and every stock MAT stores the cel's own index there.
    for (std::size_t c = 0; c < cels.size(); ++c)
    {
        putI32(out, 8);
        putI32(out, solid[c]);
        for (int i = 0; i < 4; ++i) putI32(out, 0x3F800000);
        putI32(out, 0);
        putI32(out, 0);
        putI32(out, static_cast<std::int32_t>(0xBFF78482u));
        putI32(out, static_cast<std::int32_t>(c));
    }
    for (std::size_t c = 0; c < cels.size(); ++c)
    {
        putI32(out, w);
        putI32(out, h);
        putI32(out, 0); // opaque
        putI32(out, 0);
        putI32(out, 0);
        putI32(out, mips);
        out.append(reinterpret_cast<const char*>(celData[c].data()), celData[c].size());
    }
    result.report.width = w;
    result.report.height = h;
    result.report.mipmaps = mips;
    result.report.cels = static_cast<std::int32_t>(cels.size());
    result.report.coloursUsed = static_cast<std::int32_t>(std::count(used.begin(), used.end(), true));
    result.report.meanError = errorCount ? errorSum / static_cast<double>(errorCount) : 0.0;
    return result;
}

namespace
{

struct AreaTap
{
    std::size_t source{};
    double weight{};
};

// For each of `to` output pixels, the source pixels its span
// [i * from / to, (i + 1) * from / to) covers and their share of it.
std::vector<std::vector<AreaTap>> areaTaps(const std::int32_t from, const std::int32_t to)
{
    std::vector<std::vector<AreaTap>> taps(static_cast<std::size_t>(to));
    const double step = static_cast<double>(from) / static_cast<double>(to);
    for ( std::int32_t i = 0; i < to; ++i )
    {
        const double start = static_cast<double>(i) * step;
        const double end = i + 1 == to ? static_cast<double>(from) : static_cast<double>(i + 1) * step;
        const auto first = static_cast<std::int32_t>(std::floor(start));
        const auto last = std::min(from, static_cast<std::int32_t>(std::ceil(end)));
        for ( std::int32_t s = first; s < last; ++s )
        {
            const double covered = std::min(end, s + 1.0) - std::max(start, static_cast<double>(s));
            if ( covered > 0.0 )
            {
                taps[static_cast<std::size_t>(i)].push_back({ static_cast<std::size_t>(s), covered / (end - start) });
            }
        }
    }
    return taps;
}

} // namespace

RgbaImage resizeRgbaArea(const RgbaImage& image, const std::int32_t width, const std::int32_t height)
{
    if ( image.width < 1 || image.height < 1
        || image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u )
    {
        throw std::invalid_argument("resize: the source image is empty or its pixel data does not match its size");
    }
    if ( width < 1 || height < 1 || width > kMaxResizeDimension || height > kMaxResizeDimension )
    {
        throw std::invalid_argument("resize: the size must be 1 to " + std::to_string(kMaxResizeDimension)
            + " pixels each way");
    }
    const auto across = areaTaps(image.width, width);
    const auto down = areaTaps(image.height, height);
    const std::size_t sourceWidth = static_cast<std::size_t>(image.width);
    const std::size_t outWidth = static_cast<std::size_t>(width);
    // Columns first, for every source row; then rows.
    std::vector<double> narrow(static_cast<std::size_t>(image.height) * outWidth * 4u, 0.0);
    for ( std::size_t y = 0; y < static_cast<std::size_t>(image.height); ++y )
    {
        const std::uint8_t* row = image.rgba.data() + y * sourceWidth * 4u;
        double* out = narrow.data() + y * outWidth * 4u;
        for ( std::size_t x = 0; x < outWidth; ++x )
        {
            for ( const AreaTap& tap : across[x] )
            {
                for ( std::size_t c = 0; c < 4; ++c )
                {
                    out[x * 4u + c] += tap.weight * row[tap.source * 4u + c];
                }
            }
        }
    }
    RgbaImage result{};
    result.width = width;
    result.height = height;
    result.rgba.resize(outWidth * static_cast<std::size_t>(height) * 4u);
    std::vector<double> sum(outWidth * 4u);
    for ( std::size_t y = 0; y < static_cast<std::size_t>(height); ++y )
    {
        std::fill(sum.begin(), sum.end(), 0.0);
        for ( const AreaTap& tap : down[y] )
        {
            const double* row = narrow.data() + tap.source * outWidth * 4u;
            for ( std::size_t i = 0; i < sum.size(); ++i )
            {
                sum[i] += tap.weight * row[i];
            }
        }
        std::uint8_t* out = result.rgba.data() + y * outWidth * 4u;
        for ( std::size_t i = 0; i < sum.size(); ++i )
        {
            out[i] = static_cast<std::uint8_t>(std::clamp(std::lround(sum[i]), 0L, 255L));
        }
    }
    return result;
}

} // namespace sed::assets
