#include "ui/resources/model_pose_preview.hpp"
#include "sed/assets/pup.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <memory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using namespace sed;
    const auto fixture =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "tests/fixtures/relay-sentry";
    const auto path = fixture / "relay_sentry.3do";
    const auto pup = assets::loadFromPUP(fixture / "relay_motion.pup");
    if (pup.bindings.size() != 1 || pup.bindings[0].action != "stand" || pup.bindings[0].key != "relay_point.key") return 5;
    auto model = assets::loadFrom3DO(path, {.applyHierarchyTransforms = false});
    std::unique_ptr<QDialog> dialog(ui::resources::createModelPosePreview(nullptr, path, model, fixture / "relay_point.key"));
    auto *scrub = dialog->findChild<QDoubleSpinBox *>("poseFrame");
    auto *lod = dialog->findChild<QComboBox *>("poseCompareLod");
    auto *marker = dialog->findChild<QComboBox *>("poseMarkers");
    auto *node = dialog->findChild<QComboBox *>("poseJoint");
    auto *detail = dialog->findChild<QLabel *>("poseJointReadout");
    auto *play = dialog->findChild<QPushButton *>("posePlay");
    if (!scrub || !lod || !marker || !node || !play || !detail || lod->count() != 2 || lod->currentIndex() != 1) return 1;
    if (marker->count() != 3 || scrub->maximum() != 12.0 || node->count() != 2) return 2;
    node->setCurrentIndex(1);
    marker->setCurrentIndex(1);
    if (scrub->value() != 3 || !detail->text().contains("22.5") || !detail->text().contains("pivot", Qt::CaseInsensitive)) return 3;
    scrub->setValue(12);
    play->click();
    if (scrub->value() != 0 || !play->isChecked()) return 4;
    play->click();
    return 0;
}
