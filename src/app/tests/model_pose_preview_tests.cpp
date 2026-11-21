#include "ui/resources/model_pose_preview.hpp"
#include "ui/resources/model_hierarchy_dialog.hpp"
#include "sed/assets/model_hierarchy.hpp"
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
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
std::string sourceBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

QImage wireframeImage(QDialog &dialog, int selectedJoint = -1)
{
    // Tests normally exclude selected-joint markers from the measured region.
    dialog.findChild<QComboBox *>("poseJoint")->setCurrentIndex(selectedJoint);
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

bool whiteInk(const QImage &image, int x, int y)
{
    const auto pixel = image.pixel(x, y);
    return qRed(pixel) >= 96 && qGreen(pixel) >= 96 && qBlue(pixel) >= 96;
}

QRect paneInk(const QImage &image, int pane)
{
    const int half = image.width() / 2;
    int left = half, top = image.height(), right = -1, bottom = -1;
    for ( int y = 24; y < image.height(); ++y )
    {
        for ( int x = 0; x < half; ++x )
        {
            if ( !whiteInk(image, x + pane * half, y) ) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}

std::vector<QRect> separateMeshInk(const QImage &image, int pane)
{
    // These fixtures put two mesh instances in disjoint horizontal ranges.
    // Measure the visible shapes, independently of PoseView's projection code.
    std::vector<QRect> result;
    const int half = image.width() / 2;
    QRect run;
    for ( int x = 0; x <= half; ++x )
    {
        QRect column;
        if ( x < half )
        {
            for ( int y = 24; y < image.height(); ++y )
            {
                if ( whiteInk(image, x + pane * half, y) ) column |= QRect(x, y, 1, 1);
            }
        }
        if ( column.isNull() )
        {
            if ( !run.isNull() ) result.push_back(run);
            run = {};
        }
        else
            run |= column;
    }
    return result;
}

bool greenNear(const QImage &image, const QPointF &point)
{
    const int cx = qRound(point.x()), cy = qRound(point.y());
    for ( int y = cy - 2; y <= cy + 2; ++y )
    {
        for ( int x = cx - 2; x <= cx + 2; ++x )
        {
            if ( !image.rect().contains(x, y) ) continue;
            const auto pixel = image.pixel(x, y);
            if ( qGreen(pixel) >= 96 && qRed(pixel) < 64 && qBlue(pixel) < 64 ) return true;
        }
    }
    return false;
}

bool expect(bool condition, const char *message)
{
    if ( !condition ) std::cerr << message << '\n';
    return condition;
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

bool ownedEditsSurviveLodSwitch(const std::filesystem::path &fixture, const std::filesystem::path &temporary)
{
    using namespace sed;
    const auto source = sourceBytes(fixture / "relay_sentry.3do");
    const auto path = temporary / "captured-unsaved.3do";
    const auto identity = temporary / std::filesystem::path(u8"caf\u00e9 \u65e5\u672c \U0001F319.key");
    {
        std::ofstream output(path, std::ios::binary);
        output << source;
        if ( !output ) return false;
    }
    std::unique_ptr<QDialog> dialog;
    {
        const auto document = assets::NativeModelDocument::open(sourceBytes(path));
        const std::array uvEdits{assets::NativeTextureVertexEdit{1, 0, 0, 17.5f, -3.25f}};
        const std::array transforms{
            assets::NativeMeshTransform{1, 0, 4.0, {0.24, 0.12, 0}},
            assets::NativeMeshTransform{1, 1, 4.0, {0.24, 0.12, 0}}};
        const auto edited = document.applyTextureVertexEdits(uvEdits).applyMeshTransforms(transforms);
        const auto &uv = edited.geosets()[1].meshes[0].texVertices[0];
        if ( !expect(uv.u == 17.5f && uv.v == -3.25f, "Captured fixture must contain the unsaved UV edit") ) return false;
        auto key = assets::loadFromKEY(fixture / "relay_point.key");
        dialog.reset(ui::resources::createModelPosePreview(nullptr, edited, key, identity));
        key.nodes.clear();
        key.markers.clear();
        key.frames = 1;
    }
    // The caller's document and KEY no longer exist. The on-disk model still
    // has its original geometry; no preview may publish the edited bytes.
    if ( !expect(sourceBytes(path) == source && !std::filesystem::exists(identity),
                 "Snapshot preview must leave the source and display-only KEY identity untouched") ) return false;
    if ( !expect(dialog->windowTitle().endsWith(QStringLiteral("caf\u00e9 \u65e5\u672c \U0001F319.key")),
                 "Captured KEY title must retain its native Unicode identity") ) return false;
    auto *lod = dialog->findChild<QComboBox *>("poseCompareLod");
    auto *frame = dialog->findChild<QDoubleSpinBox *>("poseFrame");
    auto *markers = dialog->findChild<QComboBox *>("poseMarkers");
    if ( !expect(lod && lod->count() == 2 && frame && frame->maximum() == 12 && markers && markers->count() == 3,
                 "Snapshot preview must own both LODs and the captured KEY controls") ) return false;
    const auto before = wireframeImage(*dialog);
    const auto first = paneInk(before, 0), comparison = paneInk(before, 1);
    if ( !expect(!first.isEmpty() && !comparison.isEmpty(), "Both snapshot panes must render geometry") ) return false;
    const double width = first.width();
    if ( !expect(std::abs(comparison.width() / width - 4.0) < 0.3 &&
                 std::abs((comparison.left() - first.left()) / width - 4.0) < 0.3,
                 "Snapshot comparison must show the unsaved fourfold scale and 0.24 translation") ) return false;
    lod->setCurrentIndex(0);
    if ( !expect(std::filesystem::remove(path), "Temporary model must be removed before the comparison switch") ) return false;
    lod->setCurrentIndex(1);
    const auto after = wireframeImage(*dialog);
    if ( !expect(after == before, "Changing LOD must retain the owned edited geometry after source deletion") ) return false;
    markers->setCurrentIndex(1);
    if ( !expect(frame->value() == 3, "Captured KEY markers must survive caller destruction") ) return false;
    return expect(!std::filesystem::exists(path) && !std::filesystem::exists(identity) &&
                  sourceBytes(fixture / "relay_sentry.3do") == source,
                  "Snapshot preview must not recreate or modify any model or KEY file");
}

bool sharedInstancesAndFixedCamera(const std::filesystem::path &fixture)
{
    using namespace sed;
    auto source = sourceBytes(fixture / "relay_sentry.3do");
    const std::string oldNode = "1: 0x0000 0x00001 1 0 -1 -1 0 0.02 0 0.04 0 0 0 0.01 0 0 boom";
    const auto row = source.find(oldNode);
    if ( !expect(row != std::string::npos, "Relay fixture must contain its expected child node") ) return false;
    source.replace(row, oldNode.size(), "1: 0x0000 0x00001 0 0 -1 -1 0 0.2 0.1 0 0 0 0 0.01 0 0 boom");
    const auto document = assets::NativeModelDocument::open(source);
    const std::array transforms{
        assets::NativeMeshTransform{0, 0, 1.5, {0.03, 0, 0}},
        assets::NativeMeshTransform{1, 0, 1.5, {0.03, 0, 0}}};
    const auto edited = document.applyMeshTransforms(transforms);
    auto key = assets::keyFromModel(edited.geosets()[0], 12, 12);
    auto &track = key.nodes[1];
    track.entries[0].flags = 1;
    // Keep the motion inside the initial extents; a fixed camera need not
    // predict future poses or leave extra space outside the chosen frame.
    track.entries[0].dPosition = {-0.005, 0, 0};
    auto end = track.entries[0];
    end.frame = 12;
    end.flags = 0;
    end.position = {0.14, 0.1, 0};
    end.dPosition = {};
    track.entries.push_back(end);
    std::unique_ptr<QDialog> dialog(ui::resources::createModelPosePreview(nullptr, edited, key));
    const auto before = wireframeImage(*dialog);
    std::array<std::vector<QRect>, 2> initial;
    for ( int pane = 0; pane < 2; ++pane )
    {
        initial[pane] = separateMeshInk(before, pane);
        if ( !expect(initial[pane].size() == 2, "Each pane must render both joint instances of one source mesh") ) return false;
        const auto &root = initial[pane][0], &child = initial[pane][1];
        const double width = root.width();
        if ( !expect(std::abs(child.width() - root.width()) <= 2 &&
                     std::abs((child.left() - root.left()) / width - 7.0 / 3.0) < 0.06 &&
                     std::abs((root.bottom() - child.bottom()) / width - 10.0 / 9.0) < 0.06,
                     "Shared geometry must receive each joint transform and its pivot exactly once") ) return false;
    }
    const auto marked = wireframeImage(*dialog, 1);
    for ( int pane = 0; pane < 2; ++pane )
    {
        const auto &root = initial[pane][0];
        const double scale = (root.width() - 1) / 0.09;
        const QPointF origin(pane * before.width() / 2.0 + root.left() + 0.17 * scale,
                             root.bottom() - 0.1 * scale);
        if ( !expect(greenNear(marked, origin + QPointF(-4, 0)) &&
                     greenNear(marked, origin + QPointF(0.01 * scale + 4, 0)),
                     "Joint origin circle and pivot cross must retain their distinct unchanged positions") ) return false;
    }
    auto *frame = dialog->findChild<QDoubleSpinBox *>("poseFrame");
    frame->setValue(12);
    const auto after = wireframeImage(*dialog);
    for ( int pane = 0; pane < 2; ++pane )
    {
        const auto moved = separateMeshInk(after, pane);
        if ( !expect(moved.size() == 2 && moved[0] == initial[pane][0],
                     "Sampling an animated child must not reframe or resize the stationary root") ) return false;
        const double width = initial[pane][0].width();
        if ( !expect(std::abs((moved[1].left() - initial[pane][1].left()) / width + 2.0 / 3.0) < 0.06,
                     "Captured KEY translation must move the displayed child under the fixed camera") ) return false;
    }
    auto *reframe = dialog->findChild<QPushButton *>("poseReframe");
    if ( !expect(reframe != nullptr, "The mapper must be able to explicitly reframe the current pose") ) return false;
    reframe->click();
    const auto reframed = separateMeshInk(wireframeImage(*dialog), 0);
    return expect(reframed.size() == 2 && reframed[0] != initial[0][0],
                  "Explicit Reframe must fit the current displayed pose");
}

bool legacyHierarchyStillWins(const std::filesystem::path &fixture)
{
    using namespace sed;
    const auto path = fixture / "relay_sentry.3do";
    const auto source = sourceBytes(path);
    auto hierarchy = assets::loadFrom3DO(path, {.applyHierarchyTransforms = false});
    hierarchy.nodes[1].pivot = {0.2, 0.1, 0};
    std::unique_ptr<QDialog> dialog(ui::resources::createModelPosePreview(
        nullptr, path, hierarchy, fixture / "relay_point.key"));
    hierarchy.nodes[1].pivot = {};
    const auto image = wireframeImage(*dialog);
    for ( int pane = 0; pane < 2; ++pane )
    {
        const auto meshes = separateMeshInk(image, pane);
        if ( !expect(meshes.size() == 2, "Legacy preview must render the supplied unsaved hierarchy in both LODs") ) return false;
        const double width = meshes[0].width();
        if ( !expect(std::abs(meshes[1].width() / width - 0.5) < 0.06 &&
                     std::abs((meshes[1].left() - meshes[0].left()) / width - 10.0 / 3.0) < 0.1,
                     "Legacy preview must retain the caller's copied pivot instead of reloading the original hierarchy") ) return false;
    }
    return expect(sourceBytes(path) == source, "Legacy pose preview must not write the model");
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
    if ( !ownedEditsSurviveLodSwitch(fixture, temp) ) return 10;
    if ( !sharedInstancesAndFixedCamera(fixture) ) return 11;
    if ( !legacyHierarchyStillWins(fixture) ) return 12;
    return 0;
}
