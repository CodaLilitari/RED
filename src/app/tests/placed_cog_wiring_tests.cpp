#include "ui/cog/placed_cog_bindings_dialog.hpp"
#include <QApplication>
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
    ui::cog::PlacedCogBindingsDialog dialog(description, {});
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
