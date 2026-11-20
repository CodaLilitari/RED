#include "main_window/common.hpp"
#include "ui/shortcuts/key_sequence_display.hpp"
#include "ui/episode/episode_editor.hpp"
#include "ui/episode/gameplay_publication.hpp"
#include "ui/layers/layer_color_utils.hpp"
#include "ui/lighting/light_bake_utils.hpp"

#include "sed/level/seam_conform.hpp"
#include "sed/level/statistics.hpp"

#include <QApplication>
#include <QMessageBox>

#include <iterator>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace sed {

void MainWindow::setupMapMenu(QMenu *mapMenu, QMenu *editMenu) {
  if (!mapMenu || !editMenu) {
    return;
  }

  // Step 1: prepare the layer editor dialog workflow shared by the menu action.
  auto openLayerManager = [this]() {
    if (!documentEditingAllowed()) {
      return;
    }
    const Level &level = document_.level();
    const bool levelHadNoLayers = level.layers.empty();

    // Build a normalized draft without mutating the document. Imported
    // files may contain sparse or malformed authored layer indices; those
    // repairs are committed later as part of the same Undo transaction as
    // the user's layout changes.
    Level normalizedLevel = level;
    if (normalizedLevel.layers.empty()) {
      normalizedLevel.layers.push_back("Main");
    }
    normalizedLevel.addMissingLayers();

    const std::vector<std::string> currentNames = normalizedLevel.layers;
    const std::vector<std::size_t> currentUsage =
        normalizedLevel.layerUsageCounts();

    std::vector<bool> currentVisible(currentNames.size(), true);
    std::vector<bool> currentLocked(currentNames.size(), false);
    std::vector<QColor> currentColors(currentNames.size());
    for (std::size_t i = 0; i < currentNames.size(); ++i) {
      if (i < layerVisible_.size()) {
        currentVisible[i] = layerVisible_[i];
      }
      if (i < layerLocked_.size()) {
        currentLocked[i] = layerLocked_[i];
      }
      if (i < layerColors_.size() && layerColors_[i].isValid()) {
        currentColors[i] = layerColors_[i];
      } else {
        currentColors[i] = ui::layers::defaultLayerColor(i);
      }
      currentColors[i].setAlpha(255);
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Layers");
    dialog.setModal(true);
    dialog.resize(560, 420);
    dialog.setMinimumSize(460, 320);

    auto *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *table = new QTreeWidget(&dialog);
    table->setObjectName("layer_editor_table");
    table->setColumnCount(4);
    table->setHeaderLabels({"Layer", "Visible", "Locked", "Color"});
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setAlternatingRowColors(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked |
                           QAbstractItemView::EditKeyPressed);
    table->setDragDropMode(QAbstractItemView::InternalMove);
    table->setDefaultDropAction(Qt::MoveAction);
    table->setDragDropOverwriteMode(false);
    table->setDropIndicatorShown(true);
    table->setStyleSheet(
        "QTreeWidget#layer_editor_table {"
        " background-color: #2b2b2b;"
        " alternate-background-color: #303030;"
        " border: 1px solid #202020;"
        " border-radius: 6px;"
        "}"
        "QTreeWidget#layer_editor_table::item {"
        " border-bottom: 1px solid rgba(214, 218, 224, 36);"
        "}"
        "QTreeWidget#layer_editor_table::item:selected {"
        " background-color: rgba(150, 156, 164, 104);"
        " color: #ffffff;"
        "}"
        "QTreeWidget#layer_editor_table::item:selected:active {"
        " background-color: rgba(150, 156, 164, 104);"
        " color: #ffffff;"
        "}"
        "QTreeWidget#layer_editor_table::item:selected:!active {"
        " background-color: rgba(150, 156, 164, 72);"
        " color: #ffffff;"
        "}");
    class LayerTableDelegate final : public QStyledItemDelegate {
    public:
      using QStyledItemDelegate::QStyledItemDelegate;

      QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const override {
        if (index.column() != 0) {
          return nullptr;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
      }
    };
    table->setItemDelegate(new LayerTableDelegate(table));
    table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(table, 1);

    auto makeDefaultLayerName = [](const std::size_t index) -> QString {
      return QString("Layer%1").arg(static_cast<int>(index));
    };

    auto refreshColorCell = [](QTreeWidgetItem *item,
                               const QColor &color) -> void {
      if (!item) {
        return;
      }

      QColor c = color;
      if (!c.isValid()) {
        c = QColor(180, 180, 180);
      }
      c.setAlpha(255);

      constexpr int swatchW = 22;
      constexpr int swatchH = 14;
      const qreal dpr =
          std::max<qreal>(1.0, qApp ? qApp->devicePixelRatio() : 1.0);

      QPixmap swatch(static_cast<int>(std::ceil(swatchW * dpr)),
                     static_cast<int>(std::ceil(swatchH * dpr)));
      swatch.setDevicePixelRatio(dpr);
      swatch.fill(Qt::transparent);
      {
        QPainter painter(&swatch);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(6, 8, 10, 210), 1.0));
        painter.setBrush(c);
        painter.drawRoundedRect(QRectF(1.0, 1.0, swatchW - 2.0, swatchH - 2.0),
                                2.5, 2.5);
      }

      item->setData(3, Qt::UserRole, c);
      item->setText(3, QString{});
      item->setData(3, Qt::DecorationRole, swatch);
      item->setToolTip(3, c.name(QColor::HexRgb).toUpper());
      item->setTextAlignment(3, Qt::AlignCenter);
      item->setSizeHint(3, QSize(42, 20));
      item->setBackground(3, QBrush());
      item->setForeground(3, QBrush());
    };

    constexpr int kOriginalLayerRole = Qt::UserRole + 1;
    constexpr int kLayerUsageRole = Qt::UserRole + 2;

    auto addRow = [table,
                   refreshColorCell](const QString &name, const bool visible,
                                     const bool locked, const QColor &color,
                                     const std::int32_t originalLayer,
                                     const std::size_t usage) -> void {
      auto *item = new QTreeWidgetItem();
      item->setText(0, name);
      item->setFlags((item->flags() | Qt::ItemIsEditable |
                      Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) &
                     ~Qt::ItemIsDropEnabled);
      item->setData(0, kOriginalLayerRole, originalLayer);
      item->setData(0, kLayerUsageRole, static_cast<qulonglong>(usage));
      item->setCheckState(1, visible ? Qt::Checked : Qt::Unchecked);
      item->setCheckState(2, locked ? Qt::Checked : Qt::Unchecked);
      const QString ownerText =
          usage == 1
              ? QStringLiteral("1 Sector, Thing, or light uses this layer.")
              : QStringLiteral("%1 Sectors, Things, or lights use this layer.")
                    .arg(static_cast<qulonglong>(usage));
      item->setToolTip(0, ownerText);
      refreshColorCell(item, color);
      table->addTopLevelItem(item);
    };

    for (std::size_t i = 0; i < currentNames.size(); ++i) {
      const QString name = QString::fromStdString(
          currentNames[i].empty() ? ("Layer" + std::to_string(i))
                                  : currentNames[i]);
      addRow(name, currentVisible[i], currentLocked[i], currentColors[i],
             static_cast<std::int32_t>(i),
             i < currentUsage.size() ? currentUsage[i] : 0);
    }

    connect(table, &QTreeWidget::itemClicked, &dialog,
            [table, refreshColorCell](QTreeWidgetItem *item, const int column) {
              if (!item || column != 3) {
                return;
              }

              QColor current = item->data(3, Qt::UserRole).value<QColor>();
              if (!current.isValid()) {
                current =
                    ui::layers::defaultLayerColor(static_cast<std::size_t>(
                        std::max(0, table->indexOfTopLevelItem(item))));
              }

              QColor chosen =
                  QColorDialog::getColor(current, table, "Select layer color");
              if (!chosen.isValid()) {
                return;
              }

              chosen.setAlpha(255);
              refreshColorCell(item, chosen);
            });

    auto *buttonRow = new QWidget(&dialog);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(6);

    auto *addBtn = new QPushButton("Add Layer", buttonRow);
    addBtn->setObjectName("layer_editor_add");
    connect(addBtn, &QPushButton::clicked, &dialog,
            [table, addRow, makeDefaultLayerName]() {
              const std::size_t row = static_cast<std::size_t>(
                  std::max(0, table->topLevelItemCount()));
              addRow(makeDefaultLayerName(row), true, false,
                     ui::layers::defaultLayerColor(row), -1, 0);
              if (QTreeWidgetItem *item =
                      table->topLevelItem(static_cast<int>(row))) {
                table->setCurrentItem(item);
                table->editItem(item, 0);
              }
            });
    buttonLayout->addWidget(addBtn);

    auto *deleteBtn = new QPushButton("Delete Layer", buttonRow);
    deleteBtn->setObjectName("layer_editor_delete");
    deleteBtn->setToolTip("Delete the selected layer. Layers containing "
                          "Sectors, Things, or lights cannot be deleted.");
    connect(deleteBtn, &QPushButton::clicked, &dialog, [table, &dialog]() {
      QTreeWidgetItem *item = table->currentItem();
      if (!item) {
        return;
      }
      if (table->topLevelItemCount() <= 1) {
        QMessageBox::information(&dialog, "Keep one layer",
                                 "A level must retain at least one layer.");
        return;
      }

      const qulonglong usage = item->data(0, kLayerUsageRole).toULongLong();
      if (usage != 0) {
        QMessageBox::information(
            &dialog, "Layer is in use",
            QString("This layer contains %1 Sector(s), Thing(s), or light(s). "
                    "Move those objects to another layer before deleting it.")
                .arg(usage));
        return;
      }

      const int row = table->indexOfTopLevelItem(item);
      delete table->takeTopLevelItem(row);
      if (table->topLevelItemCount() > 0) {
        table->setCurrentItem(
            table->topLevelItem(std::min(row, table->topLevelItemCount() - 1)));
      }
    });
    buttonLayout->addWidget(deleteBtn);

    auto *compactBtn = new QPushButton("Compact Unused", buttonRow);
    compactBtn->setObjectName("layer_editor_compact");
    compactBtn->setToolTip(
        "Remove every unused layer and compact the remaining layer indices. At "
        "least one layer is always retained.");
    connect(compactBtn, &QPushButton::clicked, &dialog, [table]() {
      int retained = table->topLevelItemCount();
      for (int row = table->topLevelItemCount() - 1; row >= 0; --row) {
        QTreeWidgetItem *item = table->topLevelItem(row);
        if (!item || item->data(0, kLayerUsageRole).toULongLong() != 0 ||
            retained <= 1) {
          continue;
        }
        delete table->takeTopLevelItem(row);
        --retained;
      }
      if (table->topLevelItemCount() > 0 && !table->currentItem()) {
        table->setCurrentItem(table->topLevelItem(0));
      }
    });
    buttonLayout->addWidget(compactBtn);

    auto moveCurrentRow = [table](const int delta) {
      QTreeWidgetItem *item = table->currentItem();
      if (!item) {
        return;
      }
      const int source = table->indexOfTopLevelItem(item);
      const int destination = source + delta;
      if (source < 0 || destination < 0 ||
          destination >= table->topLevelItemCount()) {
        return;
      }
      item = table->takeTopLevelItem(source);
      table->insertTopLevelItem(destination, item);
      table->setCurrentItem(item);
    };

    auto *moveUpBtn = new QPushButton("Up", buttonRow);
    moveUpBtn->setObjectName("layer_editor_move_up");
    moveUpBtn->setToolTip("Move the selected layer earlier. You can also drag "
                          "rows to reorder layers.");
    connect(moveUpBtn, &QPushButton::clicked, &dialog,
            [moveCurrentRow]() { moveCurrentRow(-1); });
    buttonLayout->addWidget(moveUpBtn);

    auto *moveDownBtn = new QPushButton("Down", buttonRow);
    moveDownBtn->setObjectName("layer_editor_move_down");
    moveDownBtn->setToolTip("Move the selected layer later. You can also drag "
                            "rows to reorder layers.");
    connect(moveDownBtn, &QPushButton::clicked, &dialog,
            [moveCurrentRow]() { moveCurrentRow(1); });
    buttonLayout->addWidget(moveDownBtn);
    buttonLayout->addStretch(1);
    root->addWidget(buttonRow);

    auto *dialogButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog,
            &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog,
            &QDialog::reject);
    root->addWidget(dialogButtons);

    if (dialog.exec() != QDialog::Accepted) {
      return;
    }

    std::vector<std::string> nextNames{};
    std::vector<QTreeWidgetItem *> orderedItems{};
    std::vector<std::int32_t> oldToNew(currentNames.size(), -1);
    nextNames.reserve(static_cast<std::size_t>(table->topLevelItemCount()));
    orderedItems.reserve(static_cast<std::size_t>(table->topLevelItemCount()));

    for (int row = 0; row < table->topLevelItemCount(); ++row) {
      QTreeWidgetItem *item = table->topLevelItem(row);
      if (!item) {
        continue;
      }

      QString name = item->text(0).trimmed();
      if (name.isEmpty()) {
        name = makeDefaultLayerName(static_cast<std::size_t>(row));
      }

      nextNames.push_back(name.toStdString());
      orderedItems.push_back(item);

      const std::int32_t original = item->data(0, kOriginalLayerRole).toInt();
      if (original >= 0 &&
          static_cast<std::size_t>(original) < oldToNew.size()) {
        oldToNew[static_cast<std::size_t>(original)] =
            static_cast<std::int32_t>(nextNames.size() - 1);
      }
    }

    if (nextNames.empty()) {
      nextNames.push_back("Main");
    }

    ui::layers::LayerVisualState nextVisual = ui::layers::remapLayerVisualState(
        oldToNew, nextNames.size(), currentVisible, currentLocked,
        currentColors);
    for (std::size_t row = 0; row < orderedItems.size(); ++row) {
      QTreeWidgetItem *item = orderedItems[row];
      nextVisual.visible[row] = item->checkState(1) != Qt::Unchecked;
      nextVisual.locked[row] = item->checkState(2) == Qt::Checked;

      QColor color = item->data(3, Qt::UserRole).value<QColor>();
      if (!color.isValid()) {
        color = nextVisual.colors[row];
      }
      color.setAlpha(255);
      nextVisual.colors[row] = color;
    }

    const auto originalLayerMalformed =
        [&level](const std::int32_t layerIndex) noexcept {
          return layerIndex < 0 ||
                 static_cast<std::size_t>(layerIndex) >= level.layers.size();
        };
    const bool normalizationRequired =
        levelHadNoLayers ||
        std::ranges::any_of(level.sectors,
                            [&](const Sector &sector) {
                              return originalLayerMalformed(sector.layer);
                            }) ||
        std::ranges::any_of(level.things,
                            [&](const Thing &thing) {
                              return originalLayerMalformed(thing.layer);
                            }) ||
        std::ranges::any_of(level.lights, [&](const SedLight &light) {
          return originalLayerMalformed(light.layer);
        });
    const bool indexLayoutChanged = std::ranges::any_of(
        oldToNew,
        [index = std::size_t{0}](const std::int32_t destination) mutable {
          return destination != static_cast<std::int32_t>(index++);
        });
    const bool layoutChanged = normalizationRequired ||
                               nextNames != currentNames || indexLayoutChanged;

    const std::unordered_set<std::int32_t> oldHiddenLayers =
        ui::layers::hiddenLayersFromVisibility(currentVisible);
    const std::unordered_set<std::int32_t> nextHiddenLayers =
        ui::layers::hiddenLayersFromVisibility(nextVisual.visible);
    const std::uint64_t layerHistoryBeforeSignature = document_.undoSignature();
    LayerVisualHistoryState layerHistoryBefore{
        currentVisible,
        currentLocked,
        currentColors,
    };
    LayerVisualHistoryByAfter stagedLayerHistoryByAfter{};
    LayerVisualHistoryAfterByBefore stagedLayerHistoryAfterByBefore{};

    if (layoutChanged)
    {
      const std::uint64_t layerAttachmentBytes =
          layerVisualHistoryTransitionOwnedBytes(layerHistoryBefore, nextVisual);
      if (!canFitCleaveWorkspaceAttachmentBudget(layerAttachmentBytes, true))
      {
        QMessageBox::warning(
            this, "Layer history limit reached",
            "The Layer change was not applied because its undo presentation would exceed the bounded workspace-history limit.");
        return;
      }
      try
      {
        layerVisualHistoryByAfterSignature_.reserve(
            layerVisualHistoryByAfterSignature_.size() + 1u);
        layerVisualHistoryAfterByBeforeSignature_.reserve(
            layerVisualHistoryAfterByBeforeSignature_.size() + 1u);
        stagedLayerHistoryByAfter.reserve(1u);
        stagedLayerHistoryAfterByBefore.reserve(1u);
        LayerVisualHistoryTransition stagedTransition{};
        stagedTransition.beforeSignature = layerHistoryBeforeSignature;
        stagedTransition.before = std::move(layerHistoryBefore);
        stagedTransition.after = nextVisual;
        stagedLayerHistoryByAfter.emplace(0u, std::move(stagedTransition));
        stagedLayerHistoryAfterByBefore.emplace(0u, 0u);
      }
      catch (const std::exception&)
      {
        QMessageBox::warning(
            this, "Layer update failed",
            "The Layer change was not applied because its undo presentation could not be reserved.");
        return;
      }
    }

    if (layoutChanged) {
      const std::string nextVisibility = ui::layers::buildLayerVisibilityString(
          nextNames.size(), nextVisual.visible);
      const bool applied = document_.applyUndoable(
          "Layers: Changed", editor::DocumentDirty::All,
          [nextNames, oldToNew,
           nextVisibility](editor::DocumentState &state) -> bool {
            if (state.level.layers.empty()) {
              state.level.layers.push_back("Main");
            }
            state.level.addMissingLayers();
            if (!state.level.remapLayers(nextNames, oldToNew)) {
              return false;
            }
            state.level.lvisstring = nextVisibility;
            return true;
          });
      if (!applied) {
        QMessageBox::warning(
            this, "Layer update failed",
            "Could not apply the layer layout. A referenced layer may have "
            "changed while the dialog was open.");
        return;
      }

      scheduleRebuildChecks(editor::DocumentDirty::All);
      refreshWindowTitle();
    }

    layerVisible_ = std::move(nextVisual.visible);
    layerLocked_ = std::move(nextVisual.locked);
    layerColors_ = std::move(nextVisual.colors);

    if (layoutChanged) {
      if (thingPlacementLayerOverride_ >= 0) {
        const std::size_t oldPlacementLayer =
            static_cast<std::size_t>(thingPlacementLayerOverride_);
        thingPlacementLayerOverride_ = oldPlacementLayer < oldToNew.size()
                                           ? oldToNew[oldPlacementLayer]
                                           : -1;
      }
      if (const auto identity = document_.historyTransitionIdentityAtDepth(
              document_.undoDepth()))
      {
        publishPreallocatedLayerVisualHistoryTransition(
            layerHistoryBeforeSignature,
            document_.undoSignature(),
            identity->recordId,
            stagedLayerHistoryByAfter,
            stagedLayerHistoryAfterByBefore);
      }
    }
    persistLayerColors();

    syncLayerViewState();
    if (layoutChanged && glView_ && oldHiddenLayers == nextHiddenLayers) {
      // setHiddenLayers() rebuilds when the hidden index set changes.
      // An all-visible reorder still needs fresh per-entity layer IDs in
      // the render meshes and pick metadata.
      glView_->notifyAllMeshesChanged();
    }
    rebuildOutliner();
    rebuildProperties();
    updateEditActions();
  };

  // Step 2: translate the requested lighting scope into a unique unlocked
  // sector list.
  auto collectLightingSectorTargets =
      [this](const int mode) -> std::vector<std::int32_t> {
    // mode: 0 = selected sectors context, 1 = visible layers, 2 = all sectors
    const Level &level = document_.level();
    std::vector<std::int32_t> sectors{};
    sectors.reserve(level.sectors.size());

    auto addSector = [&](const std::int32_t sectorIndex) -> void {
      if (sectorIndex < 0 ||
          static_cast<std::size_t>(sectorIndex) >= level.sectors.size()) {
        return;
      }

      const Sector &sector =
          level.sectors[static_cast<std::size_t>(sectorIndex)];
      if (isLayerLocked(sector.layer)) {
        return;
      }
      sectors.push_back(sectorIndex);
    };

    if (mode == 0) {
      const editor::Selection &sel = document_.selection();
      for (const std::int32_t sectorIndex : sel.sectors) {
        addSector(sectorIndex);
      }
      for (const SurfaceRef &ref : sel.surfaces) {
        addSector(ref.sector);
      }
      for (const editor::SurfaceVertexRef &ref : sel.surfaceVertices) {
        addSector(ref.surface.sector);
      }
    } else if (mode == 1) {
      normalizeLayerVisualState();
      for (std::size_t i = 0; i < level.sectors.size(); ++i) {
        const Sector &sector = level.sectors[i];
        bool visible = true;
        if (sector.layer >= 0 &&
            static_cast<std::size_t>(sector.layer) < layerVisible_.size()) {
          visible = layerVisible_[static_cast<std::size_t>(sector.layer)];
        }
        if (!visible) {
          continue;
        }
        addSector(static_cast<std::int32_t>(i));
      }
    } else {
      for (std::size_t i = 0; i < level.sectors.size(); ++i) {
        addSector(static_cast<std::int32_t>(i));
      }
    }

    std::sort(sectors.begin(), sectors.end());
    sectors.erase(std::unique(sectors.begin(), sectors.end()), sectors.end());
    return sectors;
  };

  // Step 3: ask for the safest recalculation scope and apply it as one undoable
  // edit.
  auto recalcLightingWithScope = [this, collectLightingSectorTargets]() {
    if (!documentEditingAllowed()) {
      return;
    }
    if (editMode_ != EditMode::light) {
      QMessageBox::information(
          this, "Recalculate lighting",
          "Switch to Light edit mode to recalculate baked lighting.");
      return;
    }

    const Level &level = document_.level();
    if (level.sectors.empty()) {
      QMessageBox::information(this, "Recalculate lighting",
                               "No sectors available.");
      return;
    }

    const std::vector<std::int32_t> selectedTargets =
        collectLightingSectorTargets(0);
    const std::vector<std::int32_t> visibleTargets =
        collectLightingSectorTargets(1);
    const std::vector<std::int32_t> allTargets =
        collectLightingSectorTargets(2);

    if (allTargets.empty()) {
      QMessageBox::information(
          this, "Recalculate lighting",
          "No unlocked sectors available for recalculation.");
      return;
    }

    enum class Scope : std::uint8_t { Selected, Visible, All };

    Scope scope = !selectedTargets.empty() ? Scope::Selected : Scope::Visible;

    QMessageBox scopeDialog(this);
    scopeDialog.setWindowTitle("Recalculate lighting");
    scopeDialog.setIcon(QMessageBox::Question);
    scopeDialog.setText(
        "Choose sectors to recalculate with the new lighting algorithm.");
    scopeDialog.setInformativeText("Locked layers are skipped.");

    QPushButton *selectedButton = nullptr;
    if (!selectedTargets.empty()) {
      selectedButton =
          scopeDialog.addButton("Selected sectors", QMessageBox::AcceptRole);
    }
    QPushButton *visibleButton = nullptr;
    if (!visibleTargets.empty()) {
      visibleButton =
          scopeDialog.addButton("Visible layers", QMessageBox::AcceptRole);
    }
    QPushButton *allButton =
        scopeDialog.addButton("All unlocked sectors", QMessageBox::AcceptRole);
    scopeDialog.addButton(QMessageBox::Cancel);

    scopeDialog.exec();

    QAbstractButton *clicked = scopeDialog.clickedButton();
    if (!clicked) {
      return;
    }
    if (clicked == selectedButton) {
      scope = Scope::Selected;
    } else if (clicked == visibleButton) {
      scope = Scope::Visible;
    } else if (clicked == allButton) {
      scope = Scope::All;
    } else {
      return;
    }

    std::vector<std::int32_t> targets{};
    std::string undoLabel{};
    switch (scope) {
    case Scope::Selected:
      targets = selectedTargets;
      undoLabel = ui::lighting::lightBakeUndoLabel(
          ui::lighting::ManualLightBakeScope::selectedSectors);
      break;
    case Scope::Visible:
      targets = visibleTargets;
      undoLabel = ui::lighting::lightBakeUndoLabel(
          ui::lighting::ManualLightBakeScope::visibleSectors);
      break;
    case Scope::All:
      targets = allTargets;
      undoLabel = ui::lighting::lightBakeUndoLabel(
          ui::lighting::ManualLightBakeScope::allSectors);
      break;
    }

    if (targets.empty()) {
      QMessageBox::information(this, "Recalculate lighting",
                               "No unlocked sectors match the chosen scope.");
      return;
    }

    startLightBake(
        std::move(targets), false, {}, {}, std::move(undoLabel));
  };

  // Step 4: wire the visible Map menu actions and refreshable layer submenus.
  auto *layersAction = new QAction("Manage &Layers...", this);
  layersAction->setObjectName("edit.layers");
  connect(layersAction, &QAction::triggered, this,
          [openLayerManager]() { openLayerManager(); });
  mapMenu->addAction(layersAction);

  // E-01: read-only level/selection statistics (Statter/Statman).
  auto *statisticsAction = new QAction("Level &Statistics...", this);
  statisticsAction->setObjectName("map.level_statistics");
  statisticsAction->setToolTip("Show aggregated counts, surface area and bounds for the level and the selected sectors");
  connect(statisticsAction, &QAction::triggered, this, [this]() {
    const Level &level = document_.level();
    const auto summarize = [](const level::LevelStatistics &stats) {
      QString text = QStringLiteral(
          "Sectors: %1\nSurfaces: %2 (adjoined %3, sky %4, fullbright %5)\n"
          "Vertices: %6 (texture %7)\nThings: %8\nLights: %9\nCOGs: %10\n"
          "Surface area: %11")
          .arg(stats.sectors).arg(stats.surfaces).arg(stats.adjoinedSurfaces)
          .arg(stats.skySurfaces).arg(stats.fullyLitSurfaces)
          .arg(stats.vertices).arg(stats.texVertices)
          .arg(stats.things).arg(stats.lights).arg(stats.cogs)
          .arg(stats.surfaceArea, 0, 'f', 3);
      if (stats.hasBounds) {
        text += QStringLiteral("\nBounds: (%1, %2, %3) - (%4, %5, %6)")
            .arg(stats.bounds.x1, 0, 'f', 3).arg(stats.bounds.y1, 0, 'f', 3)
            .arg(stats.bounds.z1, 0, 'f', 3).arg(stats.bounds.x2, 0, 'f', 3)
            .arg(stats.bounds.y2, 0, 'f', 3).arg(stats.bounds.z2, 0, 'f', 3);
      } else {
        text += QStringLiteral("\nBounds: (none)");
      }
      return text;
    };
    QDialog dialog(this);
    dialog.setObjectName("map.level_statistics.dialog");
    dialog.setWindowTitle(QStringLiteral("Level Statistics"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *levelLabel = new QLabel(summarize(level::levelComputeStatistics(level)), &dialog);
    levelLabel->setObjectName("statistics_level");
    layout->addWidget(new QLabel(QStringLiteral("Whole level:"), &dialog));
    layout->addWidget(levelLabel);
    const editor::Selection &selection = document_.selection();
    std::vector<std::int32_t> selected(selection.sectors.begin(), selection.sectors.end());
    auto *selectionLabel = new QLabel(
        selected.empty() ? QStringLiteral("(no sectors selected)")
                         : summarize(level::levelComputeStatistics(level, selected)),
        &dialog);
    selectionLabel->setObjectName("statistics_selection");
    layout->addWidget(new QLabel(QStringLiteral("Selected sectors:"), &dialog));
    layout->addWidget(selectionLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
  });
  mapMenu->addAction(statisticsAction);

  // Sealed-seam T-junctions: the core pass behind the agent's conformSeams,
  // as one undoable edit. Exact corners are inserted without asking; corners
  // that can only be placed approximately, and seams with competing corners,
  // are explained first and left alone unless the designer agrees.
  auto *conformSeamsAction = new QAction("Con&form Sealed Seams", this);
  conformSeamsAction->setObjectName("map.conform_sealed_seams");
  conformSeamsAction->setToolTip(
      "Give every edge the corner its neighbouring room already has there, "
      "closing the T-junctions left where rooms of different sizes meet. "
      "Nothing is split or renumbered.");
  connect(conformSeamsAction, &QAction::triggered, this, [this]() {
    if (!documentEditingAllowed()) {
      return;
    }
    const auto plural = [](const std::size_t count, const char *noun) {
      return QStringLiteral("%1 %2%3")
          .arg(count)
          .arg(QString::fromLatin1(noun))
          .arg(count == 1 ? QString{} : QStringLiteral("s"));
    };
    // An outcome, not a failure: seams left over are part of the report.
    const auto toast = [this](const QString &text, const bool lingering) {
      if (glView_) {
        glView_->showToast(text, lingering ? 6000 : 4000,
                           GlView::ToastPlacement::BottomCenter,
                           GlView::ToastGroup::General);
      }
      statusBar()->showMessage(text, 7000);
    };
    const auto describeSurface = [](const SurfaceRef ref) {
      return ref.surface >= 0
                 ? QStringLiteral("sector %1, surface %2")
                       .arg(ref.sector)
                       .arg(ref.surface)
                 : QStringLiteral("sector %1").arg(ref.sector);
    };
    // Runs one pass inside an undoable edit. When it inserts corners and
    // leaves seams, the first one left becomes the selection.
    const auto conform = [this](const level::SeamConformOptions &options) {
      struct WaitCursor {
        WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
        ~WaitCursor() { QApplication::restoreOverrideCursor(); }
      } waitCursor;
      level::SeamConformResult result;
      const bool applied = document_.applyUndoable(
          "Map: Sealed seams conformed", editor::DocumentDirty::LevelGeometry,
          [&](editor::DocumentState &state) -> bool {
            result = level::levelConformSealedSeams(state.level, options);
            if (!result.succeeded() || result.insertions.empty()) {
              return false;
            }
            // Corner numbering changed on every surface that gained one.
            state.selection.surfaceVertices.clear();
            if (result.issuesRemaining > 0 && result.hasBlockingIssue) {
              state.selection.clear();
              for (const SurfaceRef ref :
                   {result.blockingFirst, result.blockingSecond}) {
                if (ref.surface >= 0) {
                  state.selection.surfaces.insert(ref);
                }
              }
            }
            return true;
          });
      return std::pair<bool, level::SeamConformResult>{applied, result};
    };

    level::SeamConformOptions options;
    options.maxInsertions = 100000;
    auto [applied, result] = conform(options);

    if (result.status == level::SeamConformStatus::Unconformable) {
      // Some issue has no corner that can be inserted exactly. Ask, then
      // insert what the designer allows and leave the rest.
      QMessageBox question(this);
      question.setIcon(QMessageBox::Question);
      question.setWindowTitle(QStringLiteral("Conform Sealed Seams"));
      question.setText(QStringLiteral(
          "Not every sealed seam can be closed with a corner exactly on its "
          "edge."));
      QStringList details;
      details << QStringLiteral("%1 found; the first is between %2 and %3.")
                     .arg(plural(result.issuesRemaining, "seam"))
                     .arg(describeSurface(result.blockingFirst))
                     .arg(describeSurface(result.blockingSecond));
      if (result.approximateCandidates > 0) {
        details << QStringLiteral(
                       "%1 lie on angled edges. Each can be placed at the "
                       "nearest point of its edge, within 0.002 units of the "
                       "neighbouring corner.")
                       .arg(plural(result.approximateCandidates, "corner"));
      }
      if (result.ambiguousCandidates > 0) {
        details << QStringLiteral(
                       "%1 compete with another corner close by on the same "
                       "edge; those are never inserted, so the seams they "
                       "belong to stay for you to fix.")
                       .arg(plural(result.ambiguousCandidates, "corner"));
      }
      question.setInformativeText(details.join(QStringLiteral("\n\n")));
      QMessageBox::StandardButtons buttons =
          QMessageBox::No | QMessageBox::Cancel;
      if (result.approximateCandidates > 0) {
        buttons |= QMessageBox::Yes;
      }
      question.setStandardButtons(buttons);
      if (QAbstractButton *approximate = question.button(QMessageBox::Yes)) {
        approximate->setText(QStringLiteral("Include Approximate Corners"));
      }
      question.button(QMessageBox::No)
          ->setText(result.approximateCandidates > 0
                        ? QStringLiteral("Exact Corners Only")
                        : QStringLiteral("Insert the Exact Corners"));
      question.setDefaultButton(QMessageBox::Cancel);
      const int answer = question.exec();
      if (answer != QMessageBox::Yes && answer != QMessageBox::No) {
        return;
      }
      options.allowRemaining = true;
      options.allowApproximate = answer == QMessageBox::Yes;
      std::tie(applied, result) = conform(options);
    }

    if (!result.succeeded()) {
      QString reason;
      switch (result.status) {
      case level::SeamConformStatus::InspectionIncomplete:
        reason = QStringLiteral(
            "The seam inspection ran out of its work budget on this level, "
            "so it cannot say which edges need corners.");
        break;
      case level::SeamConformStatus::BudgetExceeded:
        reason = QStringLiteral("The level needs more than %1 corners.")
                     .arg(options.maxInsertions);
        break;
      case level::SeamConformStatus::AllocationFailure:
        reason = QStringLiteral("There was not enough memory.");
        break;
      default:
        reason = QStringLiteral("A corner could not be inserted at %1.")
                     .arg(describeSurface(result.blockingFirst));
        break;
      }
      QMessageBox::warning(this, QStringLiteral("Conform Sealed Seams"),
                           reason + QStringLiteral(
                                        " The level was not changed."));
      return;
    }

    if (!applied) {
      if (result.issuesBefore == 0) {
        toast(QStringLiteral("No sealed-seam T-junctions found."), false);
        return;
      }
      // Nothing could be inserted under the chosen options: point at the
      // first seam left without recording an edit.
      editor::Selection &selection = document_.selection();
      if (result.hasBlockingIssue) {
        selection.clear();
        for (const SurfaceRef ref :
             {result.blockingFirst, result.blockingSecond}) {
          if (ref.surface >= 0) {
            selection.surfaces.insert(ref);
          }
        }
        if (glView_) {
          glView_->setHighlightedSelection(selection);
        }
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
      }
      toast(QStringLiteral("No corner could be inserted exactly; %1 remain%2. "
                           "The first is selected.")
                .arg(plural(result.issuesRemaining, "seam"))
                .arg(result.issuesRemaining == 1 ? QStringLiteral("s")
                                                 : QString{}),
            true);
      return;
    }

    std::set<std::pair<std::int32_t, std::int32_t>> surfaces;
    for (const level::SeamConformInsertion &insertion : result.insertions) {
      surfaces.emplace(insertion.target.surface.sector,
                       insertion.target.surface.surface);
    }
    QString message =
        QStringLiteral("Sealed seams: inserted %1 on %2; %3 closed")
            .arg(plural(result.insertions.size(), "corner"))
            .arg(plural(surfaces.size(), "surface"))
            .arg(plural(result.issuesBefore - result.issuesRemaining, "seam"));
    if (result.issuesRemaining > 0) {
      message += QStringLiteral(", %1 remain%2; the first is selected")
                     .arg(plural(result.issuesRemaining, "seam"))
                     .arg(result.issuesRemaining == 1 ? QStringLiteral("s")
                                                      : QString{});
    }
    message += QLatin1Char('.');

    invalidateSurfaceSelectionContextForTopologyEdit();
    if (glView_) {
      glView_->notifyLevelGeometryChanged();
      glView_->setHighlightedSelection(document_.selection());
    }
    rebuildOutliner();
    rebuildProperties();
    updateEditActions();
    scheduleRebuildChecks(editor::DocumentDirty::LevelGeometry);
    refreshWindowTitle();
    toast(message, result.issuesRemaining > 0);
  });
  mapMenu->addAction(conformSeamsAction);
  connect(mapMenu, &QMenu::aboutToShow, this, [this, conformSeamsAction]() {
    conformSeamsAction->setEnabled(documentEditingAllowed());
  });

  auto *layerVisibilityMenu = ui::shortcuts::addKeySequenceDisplayMenu(mapMenu, "Layer &Visibility");
  layerVisibilityMenu->setObjectName("map.layers.visibility");

  auto *layerLockMenu = ui::shortcuts::addKeySequenceDisplayMenu(mapMenu, "Layer &Locks");
  layerLockMenu->setObjectName("map.layers.locks");

  auto rebuildMapLayerMenus = [this, layerVisibilityMenu, layerLockMenu]() {
    if (!layerVisibilityMenu || !layerLockMenu) {
      return;
    }

    const Level &level = document_.level();
    const std::size_t layerCount =
        std::max<std::size_t>(1, level.layers.size());
    const bool editingAllowed = documentEditingAllowed();
    if (editingAllowed) {
      normalizeLayerVisualState();
    }
    layerVisibilityMenu->menuAction()->setEnabled(editingAllowed);
    layerLockMenu->menuAction()->setEnabled(editingAllowed);

    auto layerLabel = [&level](const std::size_t i) -> QString {
      QString layerName = QString("Layer%1").arg(static_cast<int>(i));
      if (i < level.layers.size() && !level.layers[i].empty()) {
        layerName = QString::fromStdString(level.layers[i]);
      }
      return QString("%1: %2").arg(static_cast<int>(i)).arg(layerName);
    };

    layerVisibilityMenu->clear();
    {
      auto *showAll = layerVisibilityMenu->addAction("Show All");
      showAll->setEnabled(editingAllowed);
      connect(showAll, &QAction::triggered, this, [this, layerCount]() {
        if (!documentEditingAllowed()) return;
        layerVisible_.assign(layerCount, true);
        syncLayerViewState();
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
      });

      auto *hideAll = layerVisibilityMenu->addAction("Hide All");
      hideAll->setEnabled(editingAllowed);
      connect(hideAll, &QAction::triggered, this, [this, layerCount]() {
        if (!documentEditingAllowed()) return;
        layerVisible_.assign(layerCount, false);
        syncLayerViewState();
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
      });
      layerVisibilityMenu->addSeparator();
    }

    layerLockMenu->clear();
    {
      auto *unlockAll = layerLockMenu->addAction("Unlock All");
      unlockAll->setEnabled(editingAllowed);
      connect(unlockAll, &QAction::triggered, this, [this, layerCount]() {
        if (!documentEditingAllowed()) return;
        layerLocked_.assign(layerCount, false);
        syncLayerViewState();
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
      });
      layerLockMenu->addSeparator();
    }

    for (std::size_t i = 0; i < layerCount; ++i) {
      const QString label = layerLabel(i);
      const QString keymapLayerName =
          i < level.layers.size() && !level.layers[i].empty()
              ? QString::fromStdString(level.layers[i])
              : QString("Layer%1").arg(static_cast<int>(i));

      auto *visAction = layerVisibilityMenu->addAction(label);
      // Keep any future user-visible layer bindings readable in Preferences;
      // dynamic action text also contains the numeric index and is not a
      // stable keymap label.
      visAction->setProperty("sed.layerName", keymapLayerName);
      visAction->setProperty("sed.layerAction", QStringLiteral("Visibility"));
      visAction->setProperty("sed_keymap_expose", true);
      visAction->setObjectName(
          QString("map.layer.%1.visibility").arg(static_cast<qulonglong>(i)));
      visAction->setCheckable(true);
      visAction->setChecked(i < layerVisible_.size() ? layerVisible_[i] : true);
      visAction->setEnabled(editingAllowed);
      connect(visAction, &QAction::toggled, this,
              [this, i](const bool checked) {
                if (!documentEditingAllowed()) return;
                if (i >= layerVisible_.size()) {
                  layerVisible_.resize(i + 1, true);
                }
                if (layerVisible_[i] == checked) {
                  return;
                }
                layerVisible_[i] = checked;
                syncLayerViewState();
                rebuildOutliner();
                rebuildProperties();
                updateEditActions();
              });

      auto *lockAction = layerLockMenu->addAction(label);
      lockAction->setProperty("sed.layerName", keymapLayerName);
      lockAction->setProperty("sed.layerAction", QStringLiteral("Lock"));
      lockAction->setProperty("sed_keymap_expose", true);
      lockAction->setObjectName(
          QString("map.layer.%1.lock").arg(static_cast<qulonglong>(i)));
      lockAction->setCheckable(true);
      lockAction->setChecked(i < layerLocked_.size() ? layerLocked_[i] : false);
      lockAction->setEnabled(editingAllowed);
      connect(lockAction, &QAction::toggled, this,
              [this, i](const bool checked) {
                if (!documentEditingAllowed()) return;
                if (i >= layerLocked_.size()) {
                  layerLocked_.resize(i + 1, false);
                }
                if (layerLocked_[i] == checked) {
                  return;
                }
                layerLocked_[i] = checked;
                syncLayerViewState();
                rebuildOutliner();
                rebuildProperties();
                updateEditActions();
              });
    }
  };

  connect(mapMenu, &QMenu::aboutToShow, this,
          [rebuildMapLayerMenus]() { rebuildMapLayerMenus(); });
  rebuildMapLayerMenus();

  auto *episodeEditorAction =
      new QAction("Episode / &Campaign Editor...", this);
  episodeEditorAction->setObjectName(
      QStringLiteral("edit.episode_campaign_editor"));
  episodeEditorAction->setShortcut(QKeySequence(Qt::Key_F8));
  episodeEditorAction->setShortcutContext(Qt::ApplicationShortcut);
  episodeEditorAction->setToolTip(
      QStringLiteral("Edit JK/MotS episode.jk sequences, cogstrings.uni text "
                     "and goals, and generate a startup COG."));
  connect(episodeEditorAction, &QAction::triggered, this, [this]() {
    if (!documentEditingAllowed()) {
      return;
    }
    if (document_.level().kind == ProjectType::IJIM) {
      QMessageBox::information(
          this, QStringLiteral("Episode / Campaign Editor"),
          QStringLiteral("IJIM projects do not use the JK/MotS episode.jk "
                         "campaign format."));
      return;
    }
    if (currentPath_.empty()) {
      QMessageBox::information(
          this, QStringLiteral("Episode / Campaign Editor"),
          QStringLiteral("Save the level first so SED can locate the campaign "
                         "directory."));
      return;
    }

    const std::filesystem::path projectDirectory =
        ui::episode::projectDirectoryForLevel(currentPath_);
    if (projectDirectory.empty()) {
      QMessageBox::warning(
          this, QStringLiteral("Episode / Campaign Editor"),
          QStringLiteral(
              "Could not resolve a campaign directory for the current level."));
      return;
    }

    const std::filesystem::path episodePath = projectDirectory / "episode.jk";
    const std::filesystem::path rootStringsPath =
        projectDirectory / "cogstrings.uni";
    const std::filesystem::path stringsPath =
        ui::episode::canonicalCogStringsPath(projectDirectory);
    const std::filesystem::path uiStringsPath =
        projectDirectory / "ui" / "cogstrings.uni";

    assets::EpisodeFile episode{};
    assets::CogStringsFile strings{};
    std::map<std::filesystem::path, std::optional<QByteArray>> reviewedFiles;
    const auto reviewedLifetime = documentLifetimeGeneration_;
    const auto reviewedPath = currentPath_;
    const auto reviewedHistory = document_.undoSignature();
    try {
      if (ui::episode::recoverGameplayFiles(projectDirectory, recoveryDirectory()))
        logMessage(MessageSeverity::info, tr("Recovered the approved campaign files."));
      for (const auto& path : {episodePath, rootStringsPath, stringsPath, uiStringsPath}) {
        const auto relative = path.lexically_relative(projectDirectory);
        reviewedFiles.emplace(relative, ui::episode::gameplayFileRevision(projectDirectory, relative));
      }
      const auto existsChecked = [](const std::filesystem::path &path) {
        std::error_code ec{};
        const bool exists = std::filesystem::exists(path, ec);
        if (ec) {
          throw std::runtime_error("Could not inspect " + path.string() + ": " +
                                   ec.message());
        }
        return exists;
      };

      if (existsChecked(episodePath)) {
        episode = assets::loadFromEpisodeJk(episodePath);
      } else {
        episode.name = currentPath_.stem().string();
        assets::EpisodeSequence sequence{};
        std::filesystem::path runtimeLevel = currentPath_.filename();
        runtimeLevel.replace_extension(".jkl");
        sequence.fileName = runtimeLevel.string();
        episode.sequences.push_back(std::move(sequence));
      }

      struct LocatedStrings {
        QString label{};
        assets::CogStringsFile strings{};
      };
      std::vector<LocatedStrings> sources{};
      const auto addStringsSource =
          [&sources, &existsChecked](const QString &label,
                                     const std::filesystem::path &path) {
            if (existsChecked(path)) {
              sources.push_back(
                  LocatedStrings{label, assets::loadFromCogStringsUni(path)});
            }
          };
      // Preserve the legacy search precedence while unioning unique
      // values instead of silently dropping lower-priority files.
      addStringsSource(QStringLiteral("root cogstrings.uni"), rootStringsPath);
      addStringsSource(QStringLiteral("misc/cogstrings.uni"), stringsPath);
      addStringsSource(QStringLiteral("ui/cogstrings.uni"), uiStringsPath);

      if (!sources.empty()) {
        strings = sources.front().strings;
        QString currentLabel = sources.front().label;
        for (std::size_t sourceIndex = 1; sourceIndex < sources.size();
             ++sourceIndex) {
          const LocatedStrings &source = sources[sourceIndex];
          const std::vector<std::string> conflicts =
              assets::conflictingCogStringKeys(strings, source.strings);

          bool preferCurrent = true;
          if (!conflicts.empty()) {
            QStringList examples{};
            const std::size_t exampleCount =
                std::min<std::size_t>(8, conflicts.size());
            for (std::size_t index = 0; index < exampleCount; ++index) {
              examples.push_back(
                  ui::episode::displayTextForGameBytes(conflicts[index]));
            }
            if (conflicts.size() > exampleCount) {
              examples.push_back(QStringLiteral("..."));
            }

            QMessageBox conflictBox(
                QMessageBox::Warning, QStringLiteral("Conflicting COG strings"),
                QStringLiteral(
                    "%1 and %2 contain %3 shared key(s) with different text. "
                    "Unique keys will still be merged.\n\n%4")
                    .arg(currentLabel)
                    .arg(source.label)
                    .arg(static_cast<qulonglong>(conflicts.size()))
                    .arg(examples.join(QStringLiteral(", "))),
                QMessageBox::NoButton, this);
            QPushButton *currentButton = conflictBox.addButton(
                QStringLiteral("Use current values"), QMessageBox::AcceptRole);
            QPushButton *sourceButton = conflictBox.addButton(
                QStringLiteral("Use %1 values").arg(source.label),
                QMessageBox::ActionRole);
            conflictBox.addButton(QMessageBox::Cancel);
            conflictBox.setDefaultButton(currentButton);
            conflictBox.exec();
            if (conflictBox.clickedButton() != currentButton &&
                conflictBox.clickedButton() != sourceButton) {
              return;
            }
            preferCurrent = conflictBox.clickedButton() == currentButton;
          }
          strings = preferCurrent
                        ? assets::mergeCogStrings(strings, source.strings)
                        : assets::mergeCogStrings(source.strings, strings);
          currentLabel = QStringLiteral("current merged values");
        }
      }
    } catch (const std::exception &error) {
      QMessageBox::critical(
          this, QStringLiteral("Episode / Campaign Editor"),
          QStringLiteral("The campaign files could not be parsed:\n%1")
              .arg(QString::fromUtf8(error.what())));
      return;
    }

    std::filesystem::path runtimeLevel = currentPath_.filename();
    runtimeLevel.replace_extension(".jkl");
    const std::optional<ui::episode::EpisodeEditorResult> edited =
        ui::episode::editEpisodeCampaign(
            this, std::move(episode), std::move(strings), runtimeLevel.string(),
            document_.level().kind);
    if (!edited) {
      return;
    }
    if (currentPath_ != reviewedPath || documentLifetimeGeneration_ != reviewedLifetime
        || document_.undoSignature() != reviewedHistory || !documentEditingAllowed()) {
      QMessageBox::warning(this, tr("Episode / Campaign Editor"), tr("The current document changed. Review the campaign again."));
      return;
    }

    std::string episodeText{};
    std::string stringsText{};
    try {
      episodeText = assets::saveToEpisodeJkText(edited->episode);
      stringsText = assets::saveToCogStringsUniText(edited->strings);
    } catch (const std::exception &error) {
      QMessageBox::critical(
          this, QStringLiteral("Episode / Campaign Editor"),
          QStringLiteral("The campaign could not be serialized:\n%1")
              .arg(QString::fromUtf8(error.what())));
      return;
    }

    QString completion = tr("Saved episode.jk and misc/cogstrings.uni.");
    try {
      std::vector<ui::episode::GameplayFileChange> files;
      std::string startupName;
      std::string startupText;
      bool targetsCurrentLevel = false;
      bool place = false;
      const auto checkStartupResource = [&](const bool mayBeMissing) {
        if (!targetsCurrentLevel) return;
        bool tooLarge = false;
        const auto effective = resources_.tryReadBinary(startupName, startupText.size(), tooLarge);
        if (!effective && mayBeMissing && !tooLarge) return;
        if (!effective || effective->size() != startupText.size()
            || std::memcmp(effective->data(), startupText.data(), startupText.size()) != 0)
          throw std::runtime_error("The project does not resolve to the reviewed startup COG: " + startupName);
      };
      if (edited->startupCog) {
        const auto& target = edited->episode.sequences.at(edited->startupCog->sequenceIndex);
        startupName = ui::episode::startupCogFileName(target);
        startupText = ui::episode::generateStartupCogText(target, *edited->startupCog);
        files.push_back({startupName, std::nullopt, startupText});
        targetsCurrentLevel = sed::text::iequalsAscii(
            std::filesystem::path(target.fileName).stem().string(), currentPath_.stem().string());
        place = targetsCurrentLevel && std::none_of(document_.level().cogs.begin(), document_.level().cogs.end(),
            [&](const Cog& cog) { return sed::text::iequalsAscii(cog.name, startupName); });
        if (place && !document_.level().canAllocateIds(1))
          throw std::runtime_error("The current level cannot allocate an ID for the startup COG.");
        checkStartupResource(true);
      }
      const auto addMetadata = [&](const std::filesystem::path& path, std::optional<std::string> bytes) {
        const auto relative = path.lexically_relative(projectDirectory);
        files.push_back({relative, reviewedFiles.at(relative), std::move(bytes)});
      };
      addMetadata(stringsPath, std::move(stringsText));
      addMetadata(episodePath, std::move(episodeText));
      addMetadata(rootStringsPath, std::nullopt);
      addMetadata(uiStringsPath, std::nullopt);

      const auto publishFiles = [&] {
        ui::episode::publishGameplayFiles(projectDirectory, recoveryDirectory(), files);
        checkStartupResource(false);
      };
      if (place) {
        document_.applyUndoable("COG: Startup placed", editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
            [&](editor::DocumentState& state) {
              Cog& cog = state.level.newCog();
              cog.name = startupName;
              state.selection.clear();
              state.selection.cogs.insert(cog.num);
              publishFiles();
              return true;
            });
        rebuildResourceLocator(currentPath_);
        if (glView_) glView_->setHighlightedSelection(document_.selection());
        rebuildOutliner();
        rebuildProperties();
        updateEditActions();
        scheduleRebuildChecks(editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure);
        refreshWindowTitle();
        completion += tr(" Placed %1 in the current level.").arg(QString::fromStdString(startupName));
      } else {
        publishFiles();
        if (!startupName.empty()) completion += tr(" Provisioned %1.").arg(QString::fromStdString(startupName));
      }
    } catch (const std::exception& error) {
      QMessageBox::warning(this, tr("Episode / Campaign Editor"),
          tr("The campaign operation could not finish. The level placement was not committed.\n%1")
              .arg(QString::fromUtf8(error.what())));
      return;
    }

    statusBar()->showMessage(completion, 7000);
  });
  const auto refreshEpisodeEditorAction = [this, episodeEditorAction]() {
    const bool supported = document_.level().kind != ProjectType::IJIM;
    episodeEditorAction->setVisible(supported);
    episodeEditorAction->setEnabled(supported && documentEditingAllowed());
  };
  connect(editMenu, &QMenu::aboutToShow, this, refreshEpisodeEditorAction);
  refreshEpisodeEditorAction();
  editMenu->addSeparator();
  editMenu->addAction(episodeEditorAction);

  recalcLightingAction_ = new QAction("Recalculate &Lighting", this);
  recalcLightingAction_->setObjectName("edit.recalculate_lighting");
  recalcLightingAction_->setToolTip(
      "Recalculate baked vertex lighting for selected/visible/all unlocked "
      "sectors in the background. "
      "The Light bake 'Ambient' option controls whether sector Ambient colors "
      "are derived too.");
  connect(recalcLightingAction_, &QAction::triggered, this,
          [recalcLightingWithScope]() { recalcLightingWithScope(); });
}

} // namespace sed
