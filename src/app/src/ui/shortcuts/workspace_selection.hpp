#pragma once
#include "sed/editor/selection.hpp"
#include <QByteArray>
#include <QVector3D>
#include <unordered_set>

namespace sed::ui
{
struct SelectionScope
{
    QByteArray identity;
    std::unordered_set<int> sectorIds;
    QVector3D minimum, maximum;
    bool hasBounds{};
};
std::optional<SurfaceRef> missingAdjoinBacklink(const Level& level, SurfaceRef source);
SelectionScope inspectSelection(const Level& level, const editor::Selection& selection);
}
