#pragma once

#include "sed/level/surface_material_edit.hpp"

#include <QByteArray>
#include <QString>
#include <functional>
#include <optional>

class QMenu;
class QWidget;
namespace sed::assets { class ResourceLocator; }

namespace sed::ui
{

// A proposal retains this bounded observation, never an editable copy of the workspace.
struct MaterialReviewSnapshot
{
    quint64 documentLifetime{};
    quint64 mutationSerial{};
    quint64 undoSignature{};
    quint64 redoSignature{};
    quint64 resourceConfiguration{};
    QByteArray levelHash;
    QByteArray cogDraftHash;
    QByteArray resourceDraftHash;
    QByteArray cleaveDraftHash;
    QByteArray materialHash;
    QString materialRevision;
    QString oldName;
    QString newName;
    int oldSlot{};

    bool operator==(const MaterialReviewSnapshot&) const = default;
};

struct MaterialReviewHost
{
    std::function<std::optional<MaterialReviewSnapshot>(const level::SurfaceMaterialEdit&, QString&)> observe;
    std::function<bool(const level::SurfaceMaterialEdit&)> apply;
    std::function<void()> refresh;
};

bool captureReviewedMaterial(const assets::ResourceLocator& locator, const QString& name,
    MaterialReviewSnapshot& snapshot, QString& error);
void addReviewedMaterialEdit(QMenu* menu, QWidget* owner, MaterialReviewHost host);

} // namespace sed::ui
