#include "sed/render/software_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sed::render
{

namespace
{

struct ViewVertex
{
    double x{}; // view-space (right)
    double y{}; // view-space (up)
    double z{}; // view-space depth (forward, positive in front)
    double u{};
    double v{};
};

double clamp01(const double v) noexcept
{
    return std::clamp(v, 0.0, 1.0);
}

std::uint32_t crc32(const std::uint8_t* data, const std::size_t size) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, const std::size_t size) noexcept
{
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void appendU32BE(std::vector<std::uint8_t>& out, const std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void appendChunk(std::vector<std::uint8_t>& out, const char type[4],
                 const std::vector<std::uint8_t>& data)
{
    appendU32BE(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    appendU32BE(out, crc32(out.data() + crcStart, 4 + data.size()));
}

} // namespace

std::vector<std::uint8_t> encodePngRgba8(const int width, const int height,
                                         const std::vector<std::uint8_t>& rgba)
{
    if (width < 1 || height < 1
        || rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
    {
        throw std::invalid_argument("encodePngRgba8 needs a matching RGBA8 buffer");
    }
    // Raw scanlines, one filter byte (0) per row.
    std::vector<std::uint8_t> raw{};
    raw.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
    for (int y = 0; y < height; ++y)
    {
        raw.push_back(0);
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 4u);
    }
    // zlib stream of stored blocks.
    std::vector<std::uint8_t> z{ 0x78, 0x01 };
    std::size_t offset = 0;
    while (offset < raw.size())
    {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = (offset + chunk == raw.size());
        z.push_back(last ? 0x01 : 0x00);
        z.push_back(static_cast<std::uint8_t>(chunk & 0xFFu));
        z.push_back(static_cast<std::uint8_t>((chunk >> 8) & 0xFFu));
        const auto nlen = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(chunk));
        z.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        z.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFu));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                 raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const std::uint32_t adler = adler32(raw.data(), raw.size());
    appendU32BE(z, adler);

    std::vector<std::uint8_t> png{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<std::uint8_t> ihdr{};
    appendU32BE(ihdr, static_cast<std::uint32_t>(width));
    appendU32BE(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", z);
    appendChunk(png, "IEND", {});
    return png;
}

SoftwareSnapshotResult renderSoftwareSnapshot(const Level& level,
                                              const SnapshotMaterialResolver& materials,
                                              const SoftwareSnapshotOptions& options)
{
    if (options.width < 1 || options.height < 1)
    {
        throw std::invalid_argument("snapshot dimensions must be positive");
    }
    const SnapshotCamera& cam = options.camera;
    const TVector forwardRaw = cam.target.Sub(cam.position);
    if (forwardRaw.Length() < 1e-12)
    {
        throw std::invalid_argument("snapshot camera position and target must differ");
    }
    const TVector forward = forwardRaw.Normalized();
    const TVector upHint = std::abs(forward.Dot(TVector::up)) > 0.999
        ? TVector{ 0, 1, 0 }
        : TVector::up;
    const TVector right = forward.Cross(upHint).Normalized();
    const TVector up = right.Cross(forward);
    if (!cam.orthographic && (cam.fieldOfViewDegrees <= 0.0 || cam.fieldOfViewDegrees >= 180.0))
    {
        throw std::invalid_argument("snapshot fieldOfViewDegrees must be within (0, 180)");
    }
    if (cam.orthographic && cam.orthographicHeight <= 0.0)
    {
        throw std::invalid_argument("snapshot orthographicHeight must be positive");
    }
    const double aspect = static_cast<double>(options.width) / static_cast<double>(options.height);
    const double tanV = cam.orthographic ? 0.0
                      : std::tan(cam.fieldOfViewDegrees * 3.14159265358979323846 / 360.0);
    const double tanH = tanV * aspect;
    const double halfH = cam.orthographicHeight / 2.0;
    const double halfW = halfH * aspect;
    const double nearPlane = std::max(1e-6, cam.nearPlane);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height) * 4u, 0);
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    std::vector<double> zbuffer(
        static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height),
        std::numeric_limits<double>::infinity());

    SoftwareSnapshotResult result{};

    for (const Sector& sector : level.sectors)
    {
        const double shadeR = options.fullbright ? 1.0 : clamp01(sector.ambient.r);
        const double shadeG = options.fullbright ? 1.0 : clamp01(sector.ambient.g);
        const double shadeB = options.fullbright ? 1.0 : clamp01(sector.ambient.b);
        for (const Surface& surface : sector.surfaces)
        {
            if (surface.vertices.size() < 3)
            {
                continue;
            }
            // An adjoin with no material is a portal into the next
            // sector: the engine draws nothing there, so neither does
            // the snapshot (it is not a missing material either).
            if (surface.adjoin && surface.material.empty())
            {
                ++result.portalsSkipped;
                continue;
            }
            // Facing uses the authored surface normal when present,
            // else the winding normal (same rule as D-06).
            TVector normal = surface.normal;
            auto vertexAt = [&](const std::size_t i) -> TVector {
                const std::int32_t index = surface.vertices[i];
                if (index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size())
                {
                    throw std::runtime_error("snapshot: surface vertex index out of range");
                }
                const Vertex& v = sector.vertices[static_cast<std::size_t>(index)];
                return TVector{ v.x, v.y, v.z };
            };
            if (normal.Length() < 1e-12)
            {
                normal = vertexAt(1).Sub(vertexAt(0)).Cross(vertexAt(2).Sub(vertexAt(0)));
            }
            if (options.cullBackFaces && normal.Length() > 1e-12
                && normal.Dot(cam.position.Sub(vertexAt(0))) < 0.0)
            {
                continue;
            }

            const SnapshotMaterial* material = nullptr;
            if (materials && !surface.material.empty())
            {
                material = materials(surface.material);
            }
            if (material) ++result.materialsUsed; else ++result.materialsMissing;

            std::vector<ViewVertex> poly{};
            poly.reserve(surface.vertices.size());
            for (std::size_t i = 0; i < surface.vertices.size(); ++i)
            {
                const TVector p = vertexAt(i);
                const TVector rel = p.Sub(cam.position);
                ViewVertex vv{};
                vv.x = rel.Dot(right);
                vv.y = rel.Dot(up);
                vv.z = rel.Dot(forward);
                if (i < surface.texVertices.size())
                {
                    vv.u = surface.texVertices[i].u;
                    vv.v = surface.texVertices[i].v;
                }
                poly.push_back(vv);
            }
            // Near-plane clip (Sutherland against z >= near).
            std::vector<ViewVertex> clipped{};
            for (std::size_t i = 0; i < poly.size(); ++i)
            {
                const ViewVertex& cur = poly[i];
                const ViewVertex& prv = poly[(i + poly.size() - 1) % poly.size()];
                const bool curIn = cur.z >= nearPlane;
                const bool prvIn = prv.z >= nearPlane;
                auto intersect = [&]() {
                    const double t = (nearPlane - prv.z) / (cur.z - prv.z);
                    ViewVertex out{};
                    out.x = prv.x + (cur.x - prv.x) * t;
                    out.y = prv.y + (cur.y - prv.y) * t;
                    out.z = nearPlane;
                    out.u = prv.u + (cur.u - prv.u) * t;
                    out.v = prv.v + (cur.v - prv.v) * t;
                    return out;
                };
                if (curIn && !prvIn) clipped.push_back(intersect());
                if (curIn) clipped.push_back(cur);
                if (!curIn && prvIn) clipped.push_back(intersect());
            }
            if (clipped.size() < 3)
            {
                continue;
            }
            ++result.surfacesDrawn;

            struct ScreenVertex
            {
                double x, y;     // pixels
                double depth;    // view z
                double u, v;     // texture coords (pre-divided by z when perspective)
                double invZ;
            };
            std::vector<ScreenVertex> screen{};
            screen.reserve(clipped.size());
            for (const ViewVertex& vv : clipped)
            {
                ScreenVertex sv{};
                sv.depth = vv.z;
                if (cam.orthographic)
                {
                    sv.x = (vv.x / halfW * 0.5 + 0.5) * options.width;
                    sv.y = (0.5 - vv.y / halfH * 0.5) * options.height;
                    sv.invZ = 1.0;
                    sv.u = vv.u;
                    sv.v = vv.v;
                }
                else
                {
                    sv.x = (vv.x / (vv.z * tanH) * 0.5 + 0.5) * options.width;
                    sv.y = (0.5 - vv.y / (vv.z * tanV) * 0.5) * options.height;
                    sv.invZ = 1.0 / vv.z;
                    sv.u = vv.u * sv.invZ;
                    sv.v = vv.v * sv.invZ;
                }
                screen.push_back(sv);
            }

            auto rasterTriangle = [&](const ScreenVertex& a, const ScreenVertex& b,
                                      const ScreenVertex& c) {
                const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::abs(area) < 1e-12) return;
                const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
                const int maxX = std::min(options.width - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
                const int maxY = std::min(options.height - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        const double px = x + 0.5, py = y + 0.5;
                        const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                        const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                        const double w2 = 1.0 - w0 - w1;
                        if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
                        const double invZ = w0 * a.invZ + w1 * b.invZ + w2 * c.invZ;
                        if (invZ <= 0.0) continue;
                        const double depth = 1.0 / invZ;
                        const std::size_t pi = static_cast<std::size_t>(y)
                            * static_cast<std::size_t>(options.width) + static_cast<std::size_t>(x);
                        if (depth >= zbuffer[pi]) continue;
                        double u = (w0 * a.u + w1 * b.u + w2 * c.u) / invZ;
                        double v = (w0 * a.v + w1 * b.v + w2 * c.v) / invZ;
                        std::array<double, 3> rgb{ 0.75, 0.75, 0.75 };
                        if (material && !material->image.empty())
                        {
                            const Rgba8Image& img = material->image;
                            if (level.uvCoordinateSpace == UvCoordinateSpace::TexturePixels)
                            {
                                u /= img.width;
                                v /= img.height;
                            }
                            const double uu = u - std::floor(u);
                            const double vv = v - std::floor(v);
                            const int tx = std::min(img.width - 1, static_cast<int>(uu * img.width));
                            const int ty = std::min(img.height - 1, static_cast<int>(vv * img.height));
                            const std::size_t ti = (static_cast<std::size_t>(ty)
                                * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(tx)) * 4u;
                            const std::uint8_t alpha = img.rgba[ti + 3];
                            if (alpha == 0 || (material->trueColor && alpha < 128)) continue;
                            rgb = { img.rgba[ti] / 255.0, img.rgba[ti + 1] / 255.0,
                                    img.rgba[ti + 2] / 255.0 };
                        }
                        zbuffer[pi] = depth;
                        pixels[pi * 4 + 0] = static_cast<std::uint8_t>(std::clamp(rgb[0] * shadeR, 0.0, 1.0) * 255.0);
                        pixels[pi * 4 + 1] = static_cast<std::uint8_t>(std::clamp(rgb[1] * shadeG, 0.0, 1.0) * 255.0);
                        pixels[pi * 4 + 2] = static_cast<std::uint8_t>(std::clamp(rgb[2] * shadeB, 0.0, 1.0) * 255.0);
                        pixels[pi * 4 + 3] = 255;
                    }
                }
                ++result.trianglesDrawn;
            };
            for (std::size_t t = 1; t + 1 < screen.size(); ++t)
            {
                rasterTriangle(screen[0], screen[t], screen[t + 1]);
            }
        }
    }

    result.png = encodePngRgba8(options.width, options.height, pixels);
    return result;
}

} // namespace sed::render
