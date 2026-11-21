#include "ui/components/ui_decimal_format.hpp"
#include "ui/resources/model_pose_preview.hpp"
#include "ui/resources/model_studio_dialog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QTest>
#include <QThread>
#include <QWindow>

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace
{
using sed::ui::resources::ModelStudioDialog;

void require(bool condition, const char* message)
{
    if ( !condition ) throw std::runtime_error(message);
}

template <typename Widget>
Widget& widget(QWidget& parent, const char* name)
{
    auto* found = parent.findChild<Widget*>(QString::fromLatin1(name));
    require(found != nullptr, name);
    return *found;
}

template <typename Predicate>
void waitFor(Predicate ready, const char* diagnostic)
{
    QElapsedTimer timer;
    timer.start();
    while ( !ready() && timer.elapsed() < 10000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(ready(), diagnostic);
}

void settle(ModelStudioDialog& dialog)
{
    waitFor([&] { return !dialog.state().busy; }, "Studio interaction did not become idle");
}

void press(ModelStudioDialog& dialog, const char* id)
{
    auto& button = widget<QPushButton>(dialog, id);
    require(button.isEnabled(), id);
    button.click();
    settle(dialog);
}

void open(ModelStudioDialog& dialog, const QString& source)
{
    dialog.show();
    widget<QLineEdit>(dialog, "modelStudioSource").setText(source);
    press(dialog, "modelStudioOpen");
    require(dialog.state().committed.has_value(), "interaction test source was not admitted");
}

void focus(ModelStudioDialog& dialog, QWidget& target)
{
    dialog.activateWindow();
    target.setFocus(Qt::OtherFocusReason);
    waitFor([&] { return QApplication::focusWidget() == &target && QApplication::activeWindow() == &dialog; },
        "Studio did not obtain keyboard focus on its own widget");
}

void shortcut(ModelStudioDialog& dialog, QKeySequence::StandardKey command)
{
    const auto sequence = QKeySequence(command);
    require(!sequence.isEmpty(), "platform has no standard model Undo/Redo key sequence");
    QTest::keySequence(dialog.windowHandle(), sequence);
    settle(dialog);
}

void keyboardHistory(const QString& source)
{
    ModelStudioDialog dialog({});
    open(dialog, source);
    const auto initial = dialog.state();
    auto& x = widget<QLineEdit>(dialog, "modelStudioX");
    x.setText(QStringLiteral("0.125"));
    press(dialog, "modelStudioPreview");
    press(dialog, "modelStudioApply");
    const auto applied = dialog.state();
    require(applied.canUndo && applied.dirty && applied.revision != initial.revision, "model Undo has no committed edit");
    const auto originalText = x.text();

    focus(dialog, x);
    require(!x.isUndoAvailable(), "reset numeric field unexpectedly contains text history");
    shortcut(dialog, QKeySequence::Undo);
    require(x.text() == originalText && dialog.state().revision == applied.revision,
        "empty native text Undo fell through to model Undo");

    x.selectAll();
    QTest::keyClicks(&x, QStringLiteral("27"));
    require(x.text() == QStringLiteral("27") && x.isUndoAvailable(), "actual keystrokes did not create native text Undo");
    shortcut(dialog, QKeySequence::Undo);
    require(x.text() == originalText && dialog.state().revision == applied.revision,
        "Undo in the numeric field must restore its text and preserve the model");
    shortcut(dialog, QKeySequence::Redo);
    require(x.text() == QStringLiteral("27") && dialog.state().revision == applied.revision,
        "Redo in the numeric field must restore its text and preserve the model");
    press(dialog, "modelStudioCancel");

    auto& action = widget<QPushButton>(dialog, "modelStudioOpen");
    focus(dialog, action);
    shortcut(dialog, QKeySequence::Undo);
    const auto undone = dialog.state();
    require(undone.revision == initial.revision && !undone.dirty && undone.canRedo
        && undone.committed->source() == initial.committed->source(), "keyboard model Undo did not restore exact clean state");
    focus(dialog, x);
    require(!x.isRedoAvailable(), "reset numeric field unexpectedly contains text Redo");
    shortcut(dialog, QKeySequence::Redo);
    require(dialog.state().revision == initial.revision, "empty native text Redo fell through to model Redo");
    focus(dialog, action);
    shortcut(dialog, QKeySequence::Redo);
    require(dialog.state().revision == applied.revision && dialog.state().committed->source() == applied.committed->source(),
        "keyboard model Redo did not restore authored bytes and revision");
}

struct DecimalPolicyScope
{
    sed::UiDecimalSeparator previous = sed::ui::uiDecimalSeparator();
    QLocale previousSystemLocale = sed::ui::uiSystemNumericLocale();
    ~DecimalPolicyScope()
    {
        sed::ui::installUiDecimalPolicy(*qApp, previous, previousSystemLocale);
    }
};

void numericBoundaries(const QString& source)
{
    DecimalPolicyScope scope;
    sed::ui::installUiDecimalPolicy(*qApp, sed::UiDecimalSeparator::comma, QLocale::c());
    ModelStudioDialog dialog({});
    open(dialog, source);
    auto& x = widget<QLineEdit>(dialog, "modelStudioX");
    focus(dialog, x);
    x.selectAll();
    QTest::keyClicks(&x, QStringLiteral("1,2345678901234567e-2"));
    // Once typing starts, a preference change must preserve raw text and
    // the interpretation of the draft's original comma decimal separator.
    sed::ui::applyUiDecimalSeparator(sed::UiDecimalSeparator::dot);
    require(x.text() == QStringLiteral("1,2345678901234567e-2"), "changing locale rewrote the active numeric draft");
    press(dialog, "modelStudioPreview");
    const auto preview = dialog.state();
    require(preview.displayed->geosets()[0].meshes[0].vertices[0].x == 1.2345678901234567e-2,
        "scientific decimal-comma translation lost precision or changed its draft locale");
    press(dialog, "modelStudioCancel");
    widget<QComboBox>(dialog, "modelStudioUvRow").setCurrentIndex(1);
    settle(dialog);
    widget<QComboBox>(dialog, "modelStudioOperation").setCurrentIndex(1);
    const auto baseline = dialog.state();
    auto& u = widget<QLineEdit>(dialog, "modelStudioU");
    for ( const auto invalid : {QStringLiteral("3.5e38"), QStringLiteral("-1e-60")} )
    {
        u.setText(QStringLiteral("1.25e-30"));
        press(dialog, "modelStudioPreview");
        require(widget<QPushButton>(dialog, "modelStudioApply").isEnabled()
            && dialog.state().displayed->geosets()[0].meshes[0].texVertices[0].u == 1.25e-30f,
            "valid scientific UV lost stored float precision before boundary test");
        u.setText(invalid);
        press(dialog, "modelStudioPreview");
        const auto refused = dialog.state();
        require(!widget<QPushButton>(dialog, "modelStudioApply").isEnabled() && refused.pendingInput
            && refused.revision == baseline.revision && refused.displayed->source() == baseline.committed->source(),
            "out-of-range UV survived as a stale accepted proposal or silently rounded to a usable boundary");
    }
    press(dialog, "modelStudioCancel");
}

// The existing pose tests measure only controlled wireframe ink below the LOD
// captions. Compare the same right-pane pixels for the host and reference dialogs.
QImage comparisonInk(QDialog& pose)
{
    widget<QComboBox>(pose, "poseJoint").setCurrentIndex(-1);
    for ( auto* combo : pose.findChildren<QComboBox*>() )
    {
        const auto top = combo->findText(QStringLiteral("Top (X/Y)"));
        if ( top >= 0 ) combo->setCurrentIndex(top);
    }
    widget<QComboBox>(pose, "poseMarkers").setCurrentIndex(1);
    require(widget<QDoubleSpinBox>(pose, "poseFrame").value() == 3, "named KEY's first marker did not sample frame 3");
    widget<QPushButton>(pose, "poseReframe").click();
    auto& view = widget<QWidget>(pose, "poseView");
    QPalette palette;
    palette.setColor(QPalette::Base, Qt::black);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Highlight, Qt::green);
    palette.setColor(QPalette::Mid, Qt::black);
    view.setPalette(palette);
    view.setFixedSize(800, 300);
    QImage image(view.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    view.render(&image);
    return image.copy(402, 24, 396, 274);
}

int inkDifference(const QImage& first, const QImage& second)
{
    require(first.size() == second.size(), "incompatible controlled wireframe dimensions");
    int changed = 0;
    for ( int y = 0; y < first.height(); ++y )
    {
        for ( int x = 0; x < first.width(); ++x )
        {
            const bool before = qGray(first.pixel(x, y)) >= 96;
            const bool after = qGray(second.pixel(x, y)) >= 96;
            if ( before != after ) ++changed;
        }
    }
    return changed;
}

void keyUsesDisplayedSnapshot(const QString& source, const QDir& directory)
{
    using namespace sed;
    const auto modelPath = directory.filePath(QStringLiteral("pose-owned-model.3do"));
    const auto keyPath = directory.filePath(QStringLiteral("pose-owned-relay.key"));
    const auto fixtureKey = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry/relay_point.key";
    require(QFile::copy(source, modelPath) && QFile::copy(QFileInfo(fixtureKey).absoluteFilePath(), keyPath),
        "could not create isolated Studio KEY input");
    ModelStudioDialog studio({});
    open(studio, modelPath);
    const auto original = *studio.state().committed;
    widget<QComboBox>(studio, "modelStudioLod").setCurrentIndex(1);
    settle(studio);
    widget<QLineEdit>(studio, "modelStudioScale").setText(QStringLiteral("4"));
    widget<QLineEdit>(studio, "modelStudioX").setText(QStringLiteral("0.24"));
    press(studio, "modelStudioPreview");
    const std::array expectedEdit{assets::NativeMeshTransform{1, 0, 4.0, {0.24, 0, 0}}};
    const auto expected = original.applyMeshTransforms(expectedEdit);
    require(studio.state().displayed->source() == expected.source() && studio.state().committed->source() == original.source(),
        "fixture has no uncommitted LOD 1 geometry for KEY preview");
    const auto key = assets::loadFromKEY(fixtureKey);
    const auto nativeKeyPath = QFileInfo(keyPath).filesystemAbsoluteFilePath();
    std::unique_ptr<QDialog> baseline(ui::resources::createModelPosePreview(nullptr, original, key, nativeKeyPath));
    std::unique_ptr<QDialog> reference(ui::resources::createModelPosePreview(nullptr, expected, key, nativeKeyPath));
    const auto baselineInk = comparisonInk(*baseline);
    const auto expectedInk = comparisonInk(*reference);
    require(inkDifference(baselineInk, expectedInk) > 100, "wireframe fixture failed to distinguish baseline and edited LOD");

    require(QFile::remove(modelPath), "temporary model could not be removed before launching KEY playback");
    widget<QLineEdit>(studio, "modelStudioKey").setText(keyPath);
    press(studio, "modelStudioKeyPreview");
    auto* actual = studio.findChild<QDialog*>(QString{}, Qt::FindDirectChildrenOnly);
    require(actual && actual->windowTitle().endsWith(QStringLiteral("pose-owned-relay.key")),
        "Studio KEY action did not open the captured named preview after model removal");
    auto& lod = widget<QComboBox>(*actual, "poseCompareLod");
    require(lod.count() == 2 && lod.currentIndex() == 1, "Studio KEY action lost the selected LOD");
    const auto actualInk = comparisonInk(*actual);
    require(inkDifference(actualInk, expectedInk) == 0 && inkDifference(actualInk, baselineInk) > 100,
        "Studio KEY action drew committed/file geometry instead of the exact unsaved displayed snapshot");
    require(QFile::remove(keyPath), "temporary KEY could not be removed after capture");
    press(studio, "modelStudioCancel");
    require(studio.state().displayed->source() == original.source(), "Studio Cancel did not restore the exact baseline");
    lod.setCurrentIndex(0);
    lod.setCurrentIndex(1);
    require(inkDifference(comparisonInk(*actual), actualInk) == 0 && !QFile::exists(modelPath) && !QFile::exists(keyPath),
        "captured KEY display lost the edited geometry after Studio Cancel and an independent LOD switch");
}
} // namespace

void runModelStudioInteractionTests(const QString& source, const QString& directory)
{
    keyboardHistory(source);
    numericBoundaries(source);
    keyUsesDisplayedSnapshot(source, QDir(directory));
}
