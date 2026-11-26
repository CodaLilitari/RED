#include "ui/cog/placed_cog_bindings_dialog.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    using namespace sed;
    Level level;
    level.newThing();
    level.newSector().surfaces.resize(1);
    auto& cog = level.newCog();
    cog.name = "door.cog";
    for ( const char* text : {"0", "0", "0", "-1"} )
    {
        CogValue binding;
        binding.type = ValueType::str;
        binding.stringValue = text;
        cog.values.push_back(binding);
    }
    const auto schema = assets::parseCogSchema("door.cog",
        "symbols\nsurface switchFace\nthing door nolink\nint literal\nthing absent\nend");
    auto description = level::describeBindings(level, 0, &schema);
    const auto mapSlots = ui::cog::placedCogMapSlots(description);
    const auto sectorSchema = assets::parseCogSchema("door.cog",
        "symbols\nsector triggerRoom\nthing door nolink\nint literal\nthing absent\nend");
    const auto roomSlots = ui::cog::placedCogMapSlots(level::describeBindings(level, 0, &sectorSchema));
    if (roomSlots.empty() || roomSlots[0].name != "triggerRoom" || roomSlots[0].target.index != 0
        || roomSlots[0].target.kind != level::CogReferenceKind::sector) return 15;
    if ( mapSlots.size() != 3 || mapSlots[0].name != "switchFace"
        || mapSlots[0].target.sectorIndex != 0 || mapSlots[0].target.surfaceIndex != 0
        || mapSlots[1].name != "door" || mapSlots[1].target.index != 0
        || mapSlots[2].name != "absent" || mapSlots[2].target.status != level::CogBindingTargetStatus::null )
    {
        std::cerr << "Expected named map bindings from this placed instance including its empty slot\n";
        return 7;
    }
    level.newThing();
    Cog second = level.cogs[0];
    second.values[1].stringValue = "1";
    level.cogs.push_back(second);
    const auto other = ui::cog::placedCogMapSlots(level::describeBindings(level, 1, &schema));
    if ( other.size() != 3 || other[1].target.index != 1 || mapSlots[1].target.index != 0 ) return 8;
    ui::cog::PlacedCogBindingsDialog dialog(description, {});
    auto* showMap = dialog.findChild<QPushButton*>("placedCogShowMap");
    if ( !showMap || !showMap->isEnabled() ) return 9;
    showMap->click();
    if ( dialog.result() != ui::cog::PlacedCogBindingsDialog::ShowMap ) return 10;
    int highlighted = -1;
    auto samePosition = mapSlots;
    samePosition.push_back({"otherDoor", mapSlots[1].target});
    ui::cog::PlacedCogMapDialog map(0, "door.cog", samePosition,
        [&](const int index) { highlighted = index; }, [](int) {}, nullptr);
    auto* choices = map.findChild<QComboBox*>("placedCogMapSlots");
    auto* next = map.findChild<QPushButton*>("placedCogMapNext");
    if ( !choices || !next || highlighted != 0 || choices->count() != 4
        || !choices->itemText(2).contains("absent") || !choices->itemText(2).contains("Unbound") ) return 12;
    choices->setCurrentIndex(2);
    next->click();
    if ( highlighted != 3 || !choices->currentText().contains("otherDoor") ) return 13;
    next->click();
    if ( highlighted != 0 ) return 14;
    auto* tree = dialog.findChild<QTreeWidget*>("placedCogWiring");
    if ( !tree )
    {
        std::cerr << "Missing wiring tree\n";
        return 1;
    }
    const auto* inputs = tree->topLevelItem(0);
    const auto* targets = tree->topLevelItem(1);
    if ( !inputs || !targets || inputs->childCount() != 1 || targets->childCount() != 2
        || !inputs->child(0)->text(0).contains("switchFace")
        || !targets->child(1)->text(0).contains("door") )
    {
        return 2;
    }

    int writes = 0;
    ui::cog::PlacedCogBindingsDialog editable(description, [&](int, int) {
        ++writes;
        return std::optional<level::CogMutationFailure>{};
    });
    auto* tabs = editable.findChild<QTabWidget*>();
    auto* rebind = editable.findChild<QPushButton*>("placedCogRebind");
    if ( !tabs || !rebind ) return 5;
    tabs->setCurrentIndex(1);
    if ( rebind->isEnabled() || writes != 0 ) return 6;
    tabs->setCurrentIndex(0);
    if ( !rebind->isEnabled() ) return 7;
    description.parameterCountMatches = false;
    ui::cog::PlacedCogBindingsDialog fallback(description, {});
    if ( !ui::cog::placedCogMapSlots(description).empty()
        || fallback.findChild<QPushButton*>("placedCogShowMap")->isEnabled() ) return 11;
    auto* unknown = fallback.findChild<QTreeWidget*>("placedCogWiring");
    if ( !unknown || unknown->topLevelItemCount() != 0 ) return 3;

    description.parameterCountMatches = true;
    description.rows.resize(1000, description.rows.front());
    ui::cog::PlacedCogBindingsDialog bounded(description, {});
    auto* limited = bounded.findChild<QTreeWidget*>("placedCogWiring");
    if ( !limited || limited->topLevelItemCount() != 3
        || limited->topLevelItem(0)->childCount() > 64
        || limited->topLevelItem(1)->childCount() > 64
        || !limited->topLevelItem(2)->text(0).contains("64") ) return 4;
    return 0;
}
