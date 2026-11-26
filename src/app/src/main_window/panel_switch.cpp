#include "main_window/common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/cog_compiler.hpp"
#include "sed/level/panel_switch.hpp"

#include <QBrush>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <map>
#include <cstring>
#include <tuple>

namespace sed
{
void MainWindow::openPanelSwitch()
{
    const QString title = tr("3DO Panel Switch");
    const auto& selected = document_.selection().things;
    if ( selected.size() != 1 )
    {
        QMessageBox::information(this, title, tr("Select one stationary architecture Thing with face collision."));
        return;
    }
    const auto owner = *selected.begin();
    const auto history = document_.undoSignature();
    const auto epoch = resourceEditorDocumentContextGeneration_;
    const auto path = currentPath_;
    const Level before = document_.level();
    if ( owner < 0 || static_cast<std::size_t>(owner) >= before.things.size() ) return;
    const auto layer = before.things[owner].layer;
    const auto editable = [&] {
        return !isLayerLocked(layer) && (layer < 0 || static_cast<std::size_t>(layer) >= layerVisible_.size() || layerVisible_[layer]);
    };
    if ( !editable() ) { QMessageBox::information(this, title, tr("The architecture is on a hidden or locked layer.")); return; }
    try
    {
        const auto name = level::panelOwnerModel(before, &templates_, owner);
        assets::ResourceReadOrigin origin;
        const auto bytes = resources_.tryReadCurrentBinary(name, 16 * 1024 * 1024, origin);
        if ( !bytes ) throw std::runtime_error("The architecture model is missing or exceeds 16 MiB.");
        const auto model = assets::loadCheckedModel3doCollision(*bytes);
        if ( model.meshes.empty() ) throw std::runtime_error("The model has no collision mesh.");
        std::map<std::string, assets::CogSchema> captured;
        std::size_t capturedBytes = 0;
        const level::CogSchemaResolver resolve = [&](std::string_view script) -> std::optional<assets::CogSchema> {
            const std::string key(script);
            if ( auto found = captured.find(key); found != captured.end() ) return found->second;
            assets::ResourceReadOrigin scriptOrigin;
            const auto src = resources_.tryReadCurrentBinary(script, assets::kMaxCogValidationSourceBytes, scriptOrigin);
            if ( !src ) return std::nullopt;
            capturedBytes += src->size();
            if ( capturedBytes > 32 * 1024 * 1024 ) throw std::runtime_error("Placed COG source exceeds the 32 MiB review limit.");
            std::string text(src->size(), '\0');
            if ( !src->empty() ) std::memcpy(text.data(), src->data(), src->size());
            return captured.emplace(key, assets::parseCogSchema(key, text)).first->second;
        };

        QDialog dialog(this);
        dialog.setObjectName("panelSwitch");
        dialog.setWindowTitle(title);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        auto* mesh = new QSpinBox(&dialog);
        mesh->setObjectName("panelMesh");
        mesh->setRange(0, static_cast<int>(model.meshes.size()) - 1);
        form->addRow(tr("Collision mesh"), mesh);
        auto* face = new QSpinBox(&dialog);
        face->setObjectName("panelFace");
        form->addRow(tr("Face"), face);
        const auto distance = [&](const char* id, const QString& label, double value, double low, double high) {
            auto* box = new QDoubleSpinBox(&dialog);
            box->setObjectName(id); box->setDecimals(4); box->setRange(low,high); box->setSingleStep(0.005); box->setValue(value);
            form->addRow(label,box);
            return box;
        };
        auto* u = distance("panelU",tr("Across face (JKU)"),0,-1000,1000);
        auto* v = distance("panelV",tr("Up face (JKU)"),0,-1000,1000);
        auto* radius = distance("panelRadius",tr("Button radius (JKU)"),0.02,0.005,0.1);
        auto* targets = new QComboBox(&dialog);
        targets->setObjectName("panelTarget");
        std::vector<std::pair<std::int32_t,std::string>> bindings;
        for ( std::size_t i = 0; i < before.cogs.size(); ++i )
        {
            const auto& cog = before.cogs[i];
            const auto schema = resolve(cog.name);
            if ( !schema || !schema->isComplete() ) continue;
            const auto symbols = schema->levelValues();
            if ( symbols.size() != cog.values.size() ) continue;
            for ( std::size_t j = 0; j < symbols.size(); ++j )
            {
                const auto* symbol = symbols[j];
                if ( symbol->kind != assets::CogSchemaSymbolKind::thing || symbol->effectiveLinkId < 0
                        || !(symbol->effectiveMask & 0x400u) || cog.values[j].asJedString() != "-1" ) continue;
                bindings.emplace_back(static_cast<std::int32_t>(i),symbol->name);
                targets->addItem(tr("COG %1 · %2 · %3").arg(static_cast<qulonglong>(i)).arg(QString::fromStdString(cog.name),
                    QString::fromStdString(symbol->name)));
            }
        }
        form->addRow(tr("Unbound activation parameter"), targets);
        QGraphicsScene scene;
        auto* view = new QGraphicsView(&scene,&dialog);
        view->setObjectName("panelPreview");
        view->setMinimumSize(300,195);
        layout->addWidget(view);
        auto* status = new QLabel(&dialog);
        status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
        layout->addWidget(status);
        auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(actions);
        auto request = [&] {
            level::PanelSwitchRequest r;
            r.owner = owner; r.mesh = mesh->value(); r.face = face->value(); r.u = u->value(); r.v = v->value();
            r.radius = radius->value();
            if ( targets->currentIndex() >= 0 )
                std::tie(r.cog,r.symbol) = bindings[static_cast<std::size_t>(targets->currentIndex())];
            return r;
        };
        const auto refresh = [&] {
            const auto r = request();
            const auto mount = level::locatePanelSwitch(before, &templates_, model, r);
            scene.clear();
            if ( mount.ok() )
            {
                QPolygonF polygon;
                for ( const auto& pt : mount.boundary )
                {
                    const TVector delta = pt - mount.anchor;
                    polygon << QPointF(delta.Dot(mount.uAxis), -delta.Dot(mount.vAxis));
                }
                QPen boundary(view->palette().text().color()); boundary.setCosmetic(true);
                scene.addPolygon(polygon,boundary);
                scene.addEllipse(-r.radius,-r.radius,2*r.radius,2*r.radius,boundary,view->palette().highlight());
                scene.setSceneRect(polygon.boundingRect());
                view->fitInView(scene.sceneRect(),Qt::KeepAspectRatio);
                status->setText(tr("Circle is the button's collision footprint. Sphere center in world JKU: %1, %2, %3.")
                    .arg(mount.center.x,0,'g',8).arg(mount.center.y,0,'g',8).arg(mount.center.z,0,'g',8));
            }
            else status->setText(QString::fromStdString(mount.failure->message));
            if ( bindings.empty() ) status->setText(tr("Place a COG with an activated handler and a free player-linked Thing parameter first."));
            actions->button(QDialogButtonBox::Ok)->setEnabled(mount.ok() && !bindings.empty());
        };
        const auto changeMesh = [&] {
            face->setRange(0,std::max(0,static_cast<int>(model.meshes[mesh->value()].faces.size())-1));
            refresh();
        };
        connect(mesh, &QSpinBox::valueChanged, &dialog, changeMesh);
        connect(face, &QSpinBox::valueChanged, &dialog, refresh);
        for ( auto* box : { u,v,radius } ) connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        changeMesh();
        if ( dialog.exec() != QDialog::Accepted ) return;
        if ( history != document_.undoSignature() || epoch != resourceEditorDocumentContextGeneration_
                || currentPath_ != path || !editable() ) throw std::runtime_error("The level changed. Review the button again.");
        if ( resources_.tryReadCurrentBinary(name, 16*1024*1024, origin) != bytes )
            throw std::runtime_error("The 3DO changed. Review the button again.");
        for ( const auto& [script,schema] : captured )
        {
            const auto data = resources_.tryReadCurrentBinary(script,assets::kMaxCogValidationSourceBytes,origin);
            if ( !data || data->size() != schema.sourceText.size()
                    || (!data->empty() && std::memcmp(data->data(),schema.sourceText.data(),data->size()) != 0) )
                throw std::runtime_error("A placed script changed. Review the button again.");
        }
        const auto r = request();
        level::PanelSwitchPlacement result;
        const bool applied = document_.applyUndoable("Place 3DO panel switch",
            editor::DocumentDirty::Things | editor::DocumentDirty::Cogs | editor::DocumentDirty::Structure,
            [&](editor::DocumentState& state) {
                result = level::placePanelSwitch(state.level, &templates_, model, r, resolve);
                if ( !result.ok() ) return false;
                state.selection.clear(); state.selection.things.insert(result.button);
                return true;
            });
        if ( !applied ) throw std::runtime_error(result.failure ? result.failure->message : "The placement did not change the level.");
        if ( glView_ ) { glView_->notifyThingsChanged(); glView_->setHighlightedSelection(document_.selection()); }
        syncCogScriptWatches(); rebuildOutliner(); rebuildProperties(); updateEditActions();
    }
    catch ( const std::exception& e )
    {
        QMessageBox::information(this, title, QString::fromUtf8(e.what()));
    }
}
} // namespace sed
