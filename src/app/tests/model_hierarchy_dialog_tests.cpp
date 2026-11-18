#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/assets/model_hierarchy.hpp"
#include "ui/resources/model_hierarchy_dialog.hpp"

#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QTemporaryDir>

#include <fstream>

namespace sed::app_tests
{
namespace
{

const char* const kArm =
    "SECTION: HEADER\n3DO 2.1\nSECTION: MODELRESOURCE\nMATERIALS 0\nSECTION: GEOMETRYDEF\n"
    "RADIUS 0\nINSERT OFFSET 0 0 0\nGEOSETS 1\nGEOSET 0\nMESHES 2\n"
    "MESH 0\nNAME base\nRADIUS 0\nGEOMETRYMODE 0\nLIGHTINGMODE 0\nTEXTUREMODE 0\nVERTICES 0\nTEXTURE VERTICES 0\nVERTEX NORMALS\nFACES 0\nFACE NORMALS\n"
    "MESH 1\nNAME upper\nRADIUS 0\nGEOMETRYMODE 0\nLIGHTINGMODE 0\nTEXTUREMODE 0\nVERTICES 0\nTEXTURE VERTICES 0\nVERTEX NORMALS\nFACES 0\nFACE NORMALS\n"
    "SECTION: HIERARCHYDEF\nHIERARCHY NODES 2\n"
    "0: 0x0 0x0 0 -1 1 -1 1 0 0 0 0 0 0 0 0 0 base\n1: 0x0 0x0 1 0 -1 -1 0 0 0 0.1 0 0 0 0 0 0 upper\n";

void editAndSave()
{
    QTemporaryDir dir;
    const std::filesystem::path root = dir.path().toStdString();
    { std::ofstream(root / "arm.3do", std::ios::binary) << kArm; }
    ui::resources::ModelHierarchyDialog dialog;
    check(dialog.loadModel(root / "arm.3do") && dialog.model().nodes.size() == 2 && dialog.reportText().contains(QStringLiteral("no problems")),
        "3DO Hierarchy dialog: opens a model and reports it clean");
    dialog.selectNode(1);
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("modelHierarchy.name"));
    check(name && name->text() == QStringLiteral("upper"), "3DO Hierarchy dialog: selecting a node shows it");
    if (name) name->setText(QStringLiteral("forearm"));
    check(dialog.applyNodeEdits().isEmpty() && dialog.model().nodes[1].name == "forearm", "3DO Hierarchy dialog: rename applies");
    check(dialog.addChild(QStringLiteral("tip")).isEmpty() && dialog.model().nodes.size() == 3 && dialog.model().nodes[2].parent == 1,
        "3DO Hierarchy dialog: add a child");
    if (name) name->setText(QStringLiteral("two words"));
    check(!dialog.applyNodeEdits().isEmpty() && dialog.model().nodes[2].name == "tip", "3DO Hierarchy dialog: a bad name is refused");
    check(dialog.saveAs(root / "arm2.3do").startsWith(QStringLiteral("Saved")), "3DO Hierarchy dialog: saves a new 3DO");
    check(dialog.saveAs(root / "arm2.3do").contains(QStringLiteral("already exists")), "3DO Hierarchy dialog: won't overwrite a file");
    check(dialog.writeKeyFromModel(root / "arm.key", 20, 15.0).startsWith(QStringLiteral("Wrote")), "3DO Hierarchy dialog: starts a KEY");

    assets::KeyFile key = assets::loadFromKEY(root / "arm.key");
    assets::KeyEntry turn;
    turn.frame = 10;
    turn.rotation = TVector{ 45, 0, 0 };
    (void)assets::keyInsertEntry(key, 1, turn);
    assets::saveToKEY(key, root / "arm-edited.key");
    const QString checked = dialog.checkKey(root / "arm-edited.key", root / "arm-fixed.key");
    check(checked.contains(QStringLiteral("staleDeltas")) && checked.contains(QStringLiteral("recomputed deltas")),
        "3DO Hierarchy dialog: Check KEY finds stale deltas and writes a fixed KEY");
    check(dialog.checkKey(root / "arm-fixed.key").contains(QStringLiteral("matches the model")),
        "3DO Hierarchy dialog: the fixed KEY matches the model");
}

void toolsMenuHasIt()
{
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    check(window.findChild<QAction*>(QStringLiteral("tools.model_hierarchy")) != nullptr, "Tools has 3DO Hierarchy");
}

} // namespace

void runModelHierarchyDialogTests()
{
    editAndSave();
    toolsMenuHasIt();
}

} // namespace sed::app_tests
