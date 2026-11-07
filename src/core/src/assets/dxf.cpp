#include "sed/assets/dxf.hpp"

#include "sed/format_number.hpp"
#include "sed/text.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sed::assets
{
namespace
{

void writeCorner(std::ostringstream& out, const int index, const TVector& corner)
{
    out << (10 + index) << "\n" << sed::format::authoredDouble(corner.x) << "\n"
        << (20 + index) << "\n" << sed::format::authoredDouble(corner.y) << "\n"
        << (30 + index) << "\n" << sed::format::authoredDouble(corner.z) << "\n";
}

} // namespace

std::string saveLevelToDxfText(const Level& level)
{
    std::ostringstream out{};
    out << "0\nSECTION\n2\nENTITIES\n";
    for (std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex)
    {
        const Sector& sector = level.sectors[sectorIndex];
        const std::string layer = "SECTOR_" + std::to_string(sectorIndex);
        for (const Surface& surface : sector.surfaces)
        {
            if (surface.vertices.size() < 3)
            {
                continue;
            }
            bool valid = true;
            for (const std::int32_t index : surface.vertices)
            {
                valid = valid && index >= 0
                    && static_cast<std::size_t>(index) < sector.vertices.size();
            }
            if (!valid)
            {
                continue;
            }
            const auto cornerOf = [&sector](const std::int32_t index)
            {
                const Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
                return TVector::Create(vertex.x, vertex.y, vertex.z);
            };
            for (std::size_t i = 1; i + 1 < surface.vertices.size(); ++i)
            {
                const TVector first = cornerOf(surface.vertices[0]);
                const TVector second = cornerOf(surface.vertices[i]);
                const TVector third = cornerOf(surface.vertices[i + 1]);
                out << "0\n3DFACE\n8\n" << layer << "\n";
                writeCorner(out, 0, first);
                writeCorner(out, 1, second);
                writeCorner(out, 2, third);
                writeCorner(out, 3, third);
            }
        }
    }
    out << "0\nENDSEC\n0\nEOF\n";
    return out.str();
}

std::vector<DxfFace> loadDxfFaces(std::istream& in)
{
    std::vector<DxfFace> faces{};
    std::string codeLine;
    std::string valueLine;
    bool inFace = false;
    DxfFace current{};
    bool has[4]{};
    const auto flush = [&]()
    {
        if (inFace && has[0] && has[1] && has[2])
        {
            current.cornerCount = (has[3]
                && (current.corners[3].x != current.corners[2].x
                    || current.corners[3].y != current.corners[2].y
                    || current.corners[3].z != current.corners[2].z))
                ? 4u
                : 3u;
            if (faces.size() < 1'000'000u)
            {
                faces.push_back(current);
            }
        }
        inFace = false;
        current = DxfFace{};
        has[0] = has[1] = has[2] = has[3] = false;
    };

    while (std::getline(in, codeLine))
    {
        if (!std::getline(in, valueLine))
        {
            break;
        }
        const std::string codeText = sed::text::trim(codeLine);
        const std::string value = sed::text::trim(valueLine);
        const auto code = sed::text::parseInt<int>(codeText);
        if (!code)
        {
            continue;
        }
        if (*code == 0)
        {
            flush();
            if (value == "3DFACE")
            {
                inFace = true;
            }
            continue;
        }
        if (!inFace)
        {
            continue;
        }
        if (*code == 8)
        {
            current.layer = value;
            continue;
        }
        // Corner coordinates: 10-13 are X, 20-23 are Y, 30-33 are Z,
        // with the units digit addressing the corner (0..3).
        if (*code >= 10 && *code <= 33)
        {
            const int corner = *code % 10;
            const int group = *code / 10;
            if (corner > 3)
            {
                continue;
            }
            const auto number = sed::text::parseDouble(value);
            if (!number)
            {
                continue;
            }
            TVector& target = current.corners[static_cast<std::size_t>(corner)];
            if (group == 1)
            {
                target.x = *number;
                has[corner] = true;
            }
            else if (group == 2)
            {
                target.y = *number;
            }
            else
            {
                target.z = *number;
            }
        }
    }
    flush();
    if (in.bad())
    {
        throw std::runtime_error("Failed to read DXF.");
    }
    return faces;
}

std::vector<DxfFace> loadDxfFaces(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Failed to open DXF file.");
    }
    return loadDxfFaces(file);
}

} // namespace sed::assets
