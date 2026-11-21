#pragma once

#include "sed/level.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <utility>
#include <string_view>
#include <vector>

namespace sed::ui::viewport
{

inline constexpr std::size_t kBrushStrokePoseLimit = 256;
inline constexpr qsizetype kBrushStrokeByteLimit = 96 * 1024;

inline bool acceptBrushSector(const bool restrictSector, const int center, const int sample) noexcept
{
    return sample >= 0 && (!restrictSector || center == sample);
}

inline QByteArray captureBrushStroke(const Level& level, const std::size_t first)
{
    if ( first >= level.things.size() || level.things.size() - first > kBrushStrokePoseLimit )
    {
        return {};
    }
    const auto& name = level.things[first].templateName;
    if ( name.empty() || name.size() > 256 ) return {};
    QJsonArray rows;
    for ( std::size_t i = first; i < level.things.size(); ++i )
    {
        const auto& t = level.things[i];
        if ( t.templateName != name || t.sector < 0 || std::size_t(t.sector) >= level.sectors.size() ) return {};
        for ( double n : {t.x, t.y, t.z, t.pch, t.yaw, t.rol} )
        {
            if ( !std::isfinite(n) ) return {};
        }
        rows.append(QJsonArray{t.sector, level.sectors[t.sector].id, t.x, t.y, t.z, t.pch, t.yaw, t.rol});
    }
    const auto result = QJsonDocument(QJsonObject{
        {"format", "red-thing-stroke-1"}, {"kind", static_cast<int>(level.kind)},
        {"template", QString::fromStdString(name)}, {"poses", rows}}).toJson(QJsonDocument::Compact);
    return result.size() <= kBrushStrokeByteLimit ? result : QByteArray{};
}

// All-or-nothing planning; caller alone allocates new IDs and publishes Undo.
// Preserve the selected template's current display bounds and active layer at commit.
template<typename Contains>
QString planBrushStroke(const Level& level, const QByteArray& bytes, const std::string_view selected,
    Contains contains, std::vector<Thing>& result)
{
    result.clear();
    if ( bytes.isEmpty() || bytes.size() > kBrushStrokeByteLimit ) return "Missing or oversized brush stroke.";
    const auto doc = QJsonDocument::fromJson(bytes).object();
    if ( doc.value("format") != QStringLiteral("red-thing-stroke-1")
        || doc.value("kind").toInt(-1) != static_cast<int>(level.kind) ) return "Incompatible brush stroke.";
    const auto name = doc.value("template").toString().toStdString();
    if ( name.empty() || name.size() > 256 || name != selected ) return "Select the saved Thing template first.";
    const auto rows = doc.value("poses").toArray();
    if ( rows.isEmpty() || rows.size() > qsizetype(kBrushStrokePoseLimit) ) return "Invalid stroke count.";
    std::vector<Thing> staged;
    std::size_t budget = 1'000'000;
    const auto charge = [&budget](std::size_t cost)
    {
        if ( cost > budget ) return false;
        budget -= cost;
        return true;
    };
    for ( const auto& entry : rows )
    {
        const auto row = entry.toArray();
        if ( row.size() != 8 ) return "Invalid stroke pose.";
        for ( const auto& n : row )
        {
            if ( !n.isDouble() || !std::isfinite(n.toDouble()) ) return "Invalid stroke coordinate.";
        }
        Thing t{};
        t.name = t.templateName = name;
        t.sector = row[0].toInt(-1);
        if ( t.sector < 0 || std::size_t(t.sector) >= level.sectors.size() ) return "Original sector is missing.";
        const auto& sector = level.sectors[t.sector];
        if ( row[1].toDouble() != sector.id ) return "Original sector changed.";
        t.x = row[2].toDouble(); t.y = row[3].toDouble(); t.z = row[4].toDouble();
        t.pch = row[5].toDouble(); t.yaw = row[6].toDouble(); t.rol = row[7].toDouble();
        if ( !charge(sector.vertices.size()) || !charge(sector.surfaces.size()) || !charge(level.things.size())
            || !charge(staged.size()) ) return "Stroke validation budget exceeded.";
        for ( const auto& face : sector.surfaces )
        {
            if ( !charge(face.vertices.size()) ) return "Stroke validation budget exceeded.";
        }
        if ( !contains(sector, t) ) return "A saved position is outside its original sector.";
        const auto overlaps = [&t](const Thing& other)
        {
            return std::hypot(t.x - other.x, t.y - other.y, t.z - other.z) < 0.001;
        };
        if ( std::any_of(level.things.begin(), level.things.end(), overlaps)
            || std::any_of(staged.begin(), staged.end(), overlaps) ) return "A saved position is already occupied.";
        staged.push_back(std::move(t));
    }
    result = std::move(staged);
    return {};
}

}
