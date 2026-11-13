#include "installation_runtime_data.hpp"
#include "sed/assets/df2jk.hpp"
#include "sed/assets/jlh.hpp"
#include "sed/assets/tpl.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
#include <sstream>
#include <stdexcept>

namespace sed::ui::diagnostics
{
QStringList installationRuntimeDataNames()
{
    return {"df2jk.lst", "ijim.tpl", "master.tpl", "mats.jlh", "mats.jll", "mots.tpl",
        "mtpls.jlh", "mtpls.jll", "shapes.tpl", "tpls.jlh", "tpls.jll"};
}
std::optional<QJsonArray> inspectInstallationRuntimeData(const QString& dataDirectory, QString* error)
{
    if ( error )
    {
        error->clear();
    }
    QString currentName;
    try
    {
        QMap<QString, QByteArray> captured;
        QJsonArray records;
        const QFileInfo root(dataDirectory);
        if ( !root.isDir() || root.isSymLink() )
        {
            throw std::runtime_error("The installed data directory is missing or linked.");
        }
        for ( const auto& name : installationRuntimeDataNames() )
        {
            currentName = name;
            const QFileInfo info(QDir(dataDirectory).filePath(name));
            const qint64 limit = name.endsWith(".jlh") || name.endsWith(".jll") ? 64 * 1024 : 4 * 1024 * 1024;
            QFile file(info.filePath());
            if ( !info.isFile() || info.isSymLink() || info.size() > limit || !file.open(QIODevice::ReadOnly) )
            {
                throw std::runtime_error("Required installed data is missing, linked, oversized, or unreadable.");
            }
            const QByteArray bytes = file.read(limit + 1);
            if ( bytes.isEmpty() || bytes.size() > limit || file.error() != QFileDevice::NoError
                || bytes.contains('\0') || (name.endsWith(".jlh") && bytes.count('\n') > 256) )
            {
                throw std::runtime_error("Required installed data exceeds its bounded text budget.");
            }
            captured.insert(name, bytes);
            records.append(QJsonObject{{"file", name}, {"bytes", bytes.size()},
                {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
        }
        for ( auto item = captured.cbegin(); item != captured.cend(); ++item )
        {
            currentName = item.key();
            std::istringstream stream(item.value().toStdString());
            if ( currentName.endsWith(".tpl") && assets::loadTemplates(stream).size() == 0 )
            {
                throw std::runtime_error("The authoritative template reader found no templates.");
            }
            if ( currentName.endsWith(".lst") && assets::loadDf2JkConversionTable(stream).empty() )
            {
                throw std::runtime_error("The authoritative conversion reader found no entries.");
            }
            if ( currentName.endsWith(".jlh") )
            {
                const QString listName = currentName.left(currentName.size() - 4) + ".jll";
                std::istringstream lists(captured.value(listName).toStdString());
                const auto tree = assets::loadFromJLH(stream, lists);
                std::vector<const assets::JlhNode*> nodes;
                for ( const auto& node : tree.roots )
                {
                    nodes.push_back(&node);
                }
                bool hasEntries = false;
                while ( !nodes.empty() )
                {
                    const auto* node = nodes.back();
                    nodes.pop_back();
                    hasEntries = hasEntries || (node->list && !node->list->entries.empty());
                    for ( const auto& child : node->children )
                    {
                        nodes.push_back(&child);
                    }
                }
                if ( !hasEntries )
                {
                    throw std::runtime_error("The authoritative category reader found no resolved entries.");
                }
            }
        }
        return records;
    }
    catch ( const std::exception& exception )
    {
        if ( error )
        {
            *error = currentName + ": " + QString::fromUtf8(exception.what());
        }
        return std::nullopt;
    }
}
}
