#include "ui/lighting/ray_recipe.hpp"
#include "ui/lighting/light_bake_utils.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <set>

namespace sed::ui::lighting
{
namespace
{
QByteArray digest(std::span<const std::byte> bytes)
{
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        QCryptographicHash::Sha256).toHex();
}

bool isDigest(const QByteArray& value)
{
    return value.size() == 64 && std::ranges::all_of(value, [](char c)
    { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace

QByteArray cornerDigest(const Level& level, std::span<const std::int32_t> targets)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&hash](quint32 value)
    {
        value = qToLittleEndian(value);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value), sizeof(value)));
    };
    for ( const auto index : targets )
    {
        if ( index < 0 || static_cast<std::size_t>(index) >= level.sectors.size() ) return {};
        add(static_cast<quint32>(index));
        const auto& sector = level.sectors[index];
        add(static_cast<quint32>(sector.surfaces.size()));
        for ( const auto& face : sector.surfaces )
        {
            add(static_cast<quint32>(face.texVertices.size()));
            for ( const auto& v : face.texVertices )
                for ( const float channel : {v.color.r, v.color.g, v.color.b, v.color.a} )
                    add(std::bit_cast<quint32>(channel));
        }
    }
    return hash.result().toHex();
}

std::vector<ResourceDigest> resourceDigests(const level::RayLightingResources& resources)
{
    std::vector<ResourceDigest> result;
    for ( const auto& d : resources.dependencies )
        result.push_back({d.name, d.extension, d.bytes ? digest(*d.bytes) : QByteArray{}});
    return result;
}

QByteArray encodeRayRecipe(const RayRecipe& r)
{
    QJsonArray sectors, resources;
    for ( const auto id : r.targets ) sectors.append(id);
    for ( const auto& d : r.resources )
        resources.append(QJsonObject{{"name", QString::fromStdString(d.name)}, {"ext", QString::fromStdString(d.extension)},
            {"sha256", QString::fromLatin1(d.sha256)}});
    const auto& o = r.options;
    const auto& c = r.contributions;
    return QJsonDocument(QJsonObject{
        {"contributions", QJsonArray{double(c.corners), double(c.surfaces), c.direct, c.sky, c.emission,
            c.diffuse, c.longestEdge}},
        {"algorithm", "red-world-ray-v1"}, {"samples", int(o.samples)}, {"bounces", int(o.bounces)},
        {"sky", QJsonArray{o.skyColor.r, o.skyColor.g, o.skyColor.b}}, {"intensity", o.skyIntensity},
        {"reflectivity", o.reflectivity}, {"angle", o.direct.angleShading}, {"water", o.direct.waterAbsorption},
        {"preferGpu", r.preferGpu}, {"backend", r.backend}, {"build", r.build}, {"targets", sectors},
        {"geometry", QString::number(r.geometry, 16)}, {"materials", QString::number(r.materials, 16)},
        {"resources", resources}, {"corners", QString::fromLatin1(r.corners)}
    }).toJson(QJsonDocument::Compact);
}

std::optional<RayRecipe> decodeRayRecipe(const QByteArray& data)
{
    if ( data.isEmpty() || data.size() > (2 << 20) ) return {};
    const auto v = QJsonDocument::fromJson(data).object();
    if ( v["algorithm"] != "red-world-ray-v1" || !v["targets"].isArray() || !v["resources"].isArray() ) return {};
    RayRecipe r{};
    bool g = false, m = false;
    r.geometry = v["geometry"].toString().toULongLong(&g, 16);
    r.materials = v["materials"].toString().toULongLong(&m, 16);
    r.corners = v["corners"].toString().toLatin1();
    r.backend = v["backend"].toString(); r.build = v["build"].toString();
    if ( !g || !m || !isDigest(r.corners) || r.backend.isEmpty() ) return {};
    auto& o = r.options;
    const auto channels = v["sky"].toArray();
    if ( channels.size() != 3 || !v["angle"].isBool() || !v["water"].isBool() || !v["preferGpu"].isBool() ) return {};
    for ( const auto& c : channels ) if ( !c.isDouble() || c.toDouble() < 0 || c.toDouble() > 1 ) return {};
    o.samples = static_cast<std::uint32_t>(v["samples"].toInt(-1));
    o.bounces = static_cast<std::uint32_t>(v["bounces"].toInt(-1));
    o.skyIntensity = v["intensity"].toDouble(-1); o.reflectivity = v["reflectivity"].toDouble(-1);
    if ( o.samples < 1 || o.samples > level::kRayLightingMaxSamples || o.bounces > level::kRayLightingMaxBounces
        || o.skyIntensity < 0 || o.skyIntensity > 1000 || o.reflectivity < 0 || o.reflectivity > 1 ) return {};
    o.skyColor = {float(channels[0].toDouble()), float(channels[1].toDouble()), float(channels[2].toDouble()), 1};
    o.direct.angleShading = v["angle"].toBool(); o.direct.waterAbsorption = v["water"].toBool();
    r.preferGpu = v["preferGpu"].toBool();
    std::set<int> unique;
    for ( const auto& t : v["targets"].toArray() )
    {
        const int target = t.toInt(-1);
        if ( target < 0 || !unique.insert(target).second ) return {};
        r.targets.push_back(target);
    }
    if ( r.targets.empty() || !std::ranges::is_sorted(r.targets) ) return {};
    for ( const auto& item : v["resources"].toArray() )
    {
        const auto d = item.toObject();
        ResourceDigest entry{d["name"].toString().toStdString(), d["ext"].toString().toStdString(), d["sha256"].toString().toLatin1()};
        if ( entry.name.empty() || (!entry.sha256.isEmpty() && !isDigest(entry.sha256)) ) return {};
        r.resources.push_back(std::move(entry));
    }
    const auto sums = v["contributions"].toArray();
    if ( sums.size() == 7 )
    {
        for ( const auto& n : sums ) if ( !n.isDouble() || n.toDouble() < 0 ) return {};
        const auto corners = sums[0].toInteger(-1), surfaces = sums[1].toInteger(-1);
        if ( corners <= 0 || corners > level::kRayLightingMaxCorners || surfaces < 1 || surfaces > corners ) return {};
        r.contributions = {std::uint64_t(corners), std::uint64_t(surfaces), sums[2].toDouble(), sums[3].toDouble(),
            sums[4].toDouble(), sums[5].toDouble(), sums[6].toDouble()};
    }
    return r;
}

RayRecipeState checkRayRecipe(const RayRecipe& r, const Level& level, const assets::Templates* templates,
    const level::EmissiveResourceReader& reader)
{
    if ( r.geometry != lightBakeSourceFingerprint(level, r.targets)
        || r.materials != sed::level::rayLightingResourceInputsFingerprint(level, templates) )
        return {false, QStringLiteral("Stale: authored lighting, geometry or a material binding changed.")};
    if ( r.corners != cornerDigest(level, r.targets) )
        return {false, QStringLiteral("Stale: stored corner lighting differs from the accepted result.")};
    try
    {
        const auto current = sed::level::resolveRayLightingResources(level, reader, {}, templates);
        if ( resourceDigests(current) != r.resources )
            return {false, QStringLiteral("Stale: a consumed resource or its winning source changed.")};
    }
    catch ( const std::exception& error )
    {
        return {false, QStringLiteral("Stale: %1").arg(QString::fromUtf8(error.what()))};
    }
    return {true, QStringLiteral("Current: %1, %2 samples, %3 bounces.").arg(r.backend).arg(r.options.samples).arg(r.options.bounces)};
}
} // namespace sed::ui::lighting
