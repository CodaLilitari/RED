#include "ui/shortcuts/workspace_selection.hpp"
#include "sed/level/geometry.hpp"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cmath>

namespace sed::ui
{
std::optional<SurfaceRef> missingAdjoinBacklink(const Level& level, SurfaceRef source)
{
    const auto valid = [&](SurfaceRef ref)
    {
        return ref.sector >= 0 && std::size_t(ref.sector) < level.sectors.size() && ref.surface >= 0 &&
            std::size_t(ref.surface) < level.sectors[ref.sector].surfaces.size();
    };
    if (!valid(source)) return {};
    const auto& a = level.sectors[source.sector];
    const auto& face = a.surfaces[source.surface];
    if (!face.adjoin || !valid(*face.adjoin) || face.adjoin->sector == source.sector) return {};
    const auto target = *face.adjoin;
    const auto& b = level.sectors[target.sector];
    const auto& other = b.surfaces[target.surface];
    if (other.adjoin || !level::surfaceBoundariesMatchForDiagnostics(a,face,b,other)) return {};
    for (const auto& sector : level.sectors)
        for (const auto& candidate : sector.surfaces)
            if (&candidate != &face && (candidate.adjoin == target || candidate.adjoin == source)) return {};
    return target;
}

SelectionScope inspectSelection(const Level& level, const editor::Selection& selection)
{
    SelectionScope scope;
    if ( selection.empty() ) return scope;
    QList<QByteArray> witnesses;
    bool valid = true;
    const auto inside = [](const auto& values, int i)
    { return i >= 0 && static_cast<std::size_t>(i) < values.size(); };
    const auto point = [&](double x, double y, double z)
    {
        if ( !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ) return;
        const QVector3D p{float(x), float(y), float(z)};
        if ( !scope.hasBounds ) { scope.minimum = scope.maximum = p; scope.hasBounds = true; }
        else for (int k=0; k<3; ++k)
        {
            scope.minimum[k] = std::min(scope.minimum[k], p[k]);
            scope.maximum[k] = std::max(scope.maximum[k], p[k]);
        }
    };
    const auto record = [&](auto write)
    {
        QByteArray row;
        QDataStream out(&row, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        write(out); witnesses.append(row);
    };
    const auto sector = [&](int i) -> const Sector*
    {
        if ( !inside(level.sectors,i) ) { valid = false; return nullptr; }
        scope.sectorIds.insert(level.sectors[i].id);
        return &level.sectors[i];
    };
    for (int i : selection.sectors)
    {
        if ( const auto* s = sector(i) )
        {
            record([&](auto& out){ out << 's' << i << s->id; });
            for (const auto& v : s->vertices) point(v.x,v.y,v.z);
        }
    }
    const auto face = [&](SurfaceRef ref, int corner)
    {
        const auto* s = sector(ref.sector);
        if ( !s || !inside(s->surfaces,ref.surface) ) { valid = false; return; }
        const auto& f = s->surfaces[ref.surface];
        if ( corner >= 0 && !inside(f.vertices,corner) ) { valid=false; return; }
        record([&](auto& out)
        {
            out << 'f' << ref.sector << s->id << ref.surface << corner;
            for (std::size_t j=0; j<f.vertices.size(); ++j)
            {
                const auto i = f.vertices[j];
                if ( !inside(s->vertices,i) ) { valid = false; return; }
                const auto& v = s->vertices[i];
                out << i << v.x << v.y << v.z;
                if (corner < 0 || int(j)==corner) point(v.x,v.y,v.z);
            }
        });
    };
    for ( auto ref : selection.surfaces ) face(ref,-1);
    for ( auto ref : selection.surfaceVertices ) face(ref.surface,ref.localVertex);
    const auto thing = [&](int i, int frame)
    {
        if ( !inside(level.things,i) ) { valid = false; return; }
        const auto& t = level.things[i];
        if ( frame >= 0 && !inside(t.values,frame) ) { valid = false; return; }
        record([&](auto& out)
        {
            out << 't' << i << t.id << frame << QString::fromStdString(t.name) << QString::fromStdString(t.templateName);
            if ( frame >= 0 ) out << QString::fromStdString(t.values[frame].name);
        });
        if ( inside(level.sectors,t.sector) ) scope.sectorIds.insert(level.sectors[t.sector].id);
        double x,y,z,pitch,yaw,roll;
        if (frame >= 0 && t.values[frame].tryGetFrame(x,y,z,pitch,yaw,roll)) point(x,y,z);
        else point(t.x,t.y,t.z);
    };
    for ( int i : selection.things ) thing(i,-1);
    for ( auto f : selection.frames ) thing(f.thing,f.value);
    for ( int i : selection.lights )
    {
        if ( !inside(level.lights,i) ) return {};
        const auto& light = level.lights[i];
        record([&](auto& out){ out << 'l' << i << light.id; });
        point(light.position.x, light.position.y, light.position.z);
    }
    for ( int i : selection.cogs )
    {
        if ( !inside(level.cogs,i) ) return {};
        const auto& c = level.cogs[i];
        record([&](auto& out){ out << 'c' << i << c.id << QString::fromStdString(c.name); });
    }
    if ( !valid ) return {};
    std::sort(witnesses.begin(), witnesses.end());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for ( const auto& row : witnesses ) hash.addData(row);
    scope.identity = hash.result();
    return scope;
}
} // namespace sed::ui
