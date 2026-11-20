#include "ui/lighting/shadow_ownership.hpp"
#include <QCborArray>
#include <QCborMap>
#include <QCryptographicHash>
#include <QtEndian>
#include <bit>
#include <limits>
#include <set>

namespace sed::ui::lighting
{
QByteArray cutTopology(const Level& level)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&hash](quint64 v)
    {
        v = qToLittleEndian(v);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&v), sizeof(v)));
    };
    add(level.sectors.size());
    for ( const auto& sec : level.sectors )
    {
        add(sec.id); add(sec.vertices.size());
        for ( const auto& v : sec.vertices )
            for ( double value : {v.x, v.y, v.z} ) add(std::bit_cast<quint64>(value));
        add(sec.surfaces.size());
        for ( const auto& f : sec.surfaces )
        {
            add(f.vertices.size());
            for ( const auto i : f.vertices ) add(i);
            add(f.adjoin ? f.adjoin->sector : -1); add(f.adjoin ? f.adjoin->surface : -1);
        }
    }
    return hash.result();
}

QByteArray encodeOwnedCuts(const OwnedCuts& cuts)
{
    QCborArray groups;
    for (const auto& family : cuts.families)
    {
        QCborArray row;
        row.append(family.front().sector);
        for ( const auto ref : family ) row.append(ref.surface);
        groups.append(row);
    }
    return QCborValue(QCborMap{{QStringLiteral("version"),1}, {QStringLiteral("base"),cuts.base},
        {QStringLiteral("generated"),cuts.generated}, {QStringLiteral("families"),groups}}).toCbor();
}

std::optional<OwnedCuts> decodeOwnedCuts(const QByteArray& bytes)
{
    if ( bytes.isEmpty() || bytes.size() > 4*1024*1024 ) return {};
    QCborParserError error;
    const auto map = QCborValue::fromCbor(bytes, &error).toMap();
    if ( error.error != QCborError::NoError || map.value(QStringLiteral("version")).toInteger() != 1 ) return {};
    OwnedCuts cuts{map.value(QStringLiteral("base")).toByteArray(), map.value(QStringLiteral("generated")).toByteArray(), {}};
    if ( cuts.base.size() != 32 || cuts.generated.size() != 32 ) return {};
    const auto rows = map.value(QStringLiteral("families")).toArray();
    std::set<std::pair<int,int>> seen;
    for ( const auto row : rows )
    {
        auto values = row.toArray();
        if ( values.size() < 3 || values.size() > 200001 ) return {};
        const auto valid = [](const QCborValue& v)
        { return v.isInteger() && v.toInteger() >= 0 && v.toInteger() <= std::numeric_limits<int>::max(); };
        if ( !valid(values[0]) ) return {};
        const int sector = int(values[0].toInteger());
        auto& group = cuts.families.emplace_back();
        for ( qsizetype i = 1; i < values.size(); ++i )
        {
            if ( !valid(values[i]) ) return {};
            const int face = int(values[i].toInteger());
            if ( seen.size() >= 200000 || !seen.emplace(sector, face).second ) return {};
            group.push_back({sector,face});
        }
    }
    if ( cuts.families.empty() ) return {};
    return cuts;
}
} // namespace sed::ui::lighting
