#include "ui/resources/model_pose_preview.hpp"
#include "ui/resources/model_hierarchy_dialog.hpp"
#include "sed/assets/native_model_document.hpp"
#include "sed/assets/pup.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPushButton>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

namespace
{
std::string sourceBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

QImage wireframeImage(QDialog &dialog)
{
    // No text or selected-joint markers enter the measured wireframe region.
    dialog.findChild<QComboBox *>("poseJoint")->setCurrentIndex(-1);
    for ( auto *combo : dialog.findChildren<QComboBox *>() )
    {
        const auto top = combo->findText(QStringLiteral("Top (X/Y)"));
        if ( top >= 0 ) combo->setCurrentIndex(top);
    }
    auto *view = dialog.layout()->itemAt(0)->widget();
    QPalette palette;
    palette.setColor(QPalette::Base, Qt::black);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Highlight, Qt::green);
    palette.setColor(QPalette::Mid, Qt::black);
    view->setPalette(palette);
    view->setFixedSize(800, 300);
    QImage image(view->size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    view->render(&image);
    return image;
}

QRect paneInk(const QImage &image, int pane)
{
    const int half = image.width() / 2;
    int left = half, top = image.height(), right = -1, bottom = -1;
    for ( int y = 24; y < image.height(); ++y )
    {
        for ( int x = 0; x < half; ++x )
        {
            const auto pixel = image.pixel(x + pane * half, y);
            if ( qRed(pixel) < 96 || qGreen(pixel) < 96 || qBlue(pixel) < 96 ) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}

bool comparisonLodFits(const std::filesystem::path &fixture, const std::filesystem::path &temporary)
{
    using namespace sed;
    const auto source = sourceBytes(fixture / "relay_sentry.3do");
    const auto document = assets::NativeModelDocument::open(source);
    const std::array transforms{assets::NativeMeshTransform{1, 0, 10.0, {}}};
    const auto enlarged = document.applyMeshTransforms(transforms);
    const auto path = temporary / "large-comparison.3do";
    {
        std::ofstream output(path, std::ios::binary);
        output << enlarged.source();
        if ( !output ) return false;
    }
    std::unique_ptr<QDialog> dialog(ui::resources::createModelPosePreview(
        nullptr, path, document.geosets()[0], fixture / "relay_point.key"));
    const auto image = wireframeImage(*dialog);
    const auto first = paneInk(image, 0);
    const auto comparison = paneInk(image, 1);
    const auto inside = [&](const QRect &bounds) {
        return !bounds.isEmpty() && bounds.left() > 3 && bounds.right() < image.width() / 2 - 4 &&
               bounds.top() > 28 && bounds.bottom() < image.height() - 4;
    };
    if ( !inside(first) || !inside(comparison) )
    {
        std::cerr << "Comparison LOD wireframe must fit both panes; LOD 1 bounds: "
                  << comparison.left() << ',' << comparison.top() << ' ' << comparison.width() << 'x'
                  << comparison.height() << '\n';
        return false;
    }
    // The source triangle is ten times wider than the LOD 0 square. Their
    // measured widths must still express that ratio under a shared camera.
    const double widthRatio = static_cast<double>(comparison.width()) / first.width();
    if ( widthRatio < 8.0 || widthRatio > 12.0 )
    {
        std::cerr << "Comparison panes must share scale; measured width ratio: " << widthRatio << '\n';
        return false;
    }
    return sourceBytes(path) == enlarged.source() && sourceBytes(fixture / "relay_sentry.3do") == source;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using namespace sed;
    const auto fixture =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "tests/fixtures/relay-sentry";
    const auto path = fixture / "relay_sentry.3do";
    ui::resources::ModelHierarchyDialog hierarchy;
    if (!hierarchy.loadModel(path) || !hierarchy.reportText().contains("other 1 geoset")) return 6;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 7;
    const std::filesystem::path temp = temporary.path().toStdString();
    const auto extended = temp / "extended.3do";
    std::filesystem::copy_file(fixture / "relay_single.3do", extended);
    std::ofstream(extended, std::ios::app) << "\nSECTION: RIG_NOTES\nleft_foot 0.25\nEND\n";
    if (!hierarchy.loadModel(extended) || !hierarchy.reportText().contains("RIG_NOTES") ||
        !hierarchy.saveAs(temp / "export.3do").contains("RIG_NOTES") || std::filesystem::exists(temp / "export.3do")) return 8;
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
    if ( !comparisonLodFits(fixture, temp) ) return 9;
    return 0;
}
