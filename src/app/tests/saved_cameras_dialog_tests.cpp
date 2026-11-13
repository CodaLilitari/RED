#include "ui/viewport/saved_cameras_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <iostream>

namespace
{
using Dialog = sed::ui::viewport::SavedCamerasDialog;
int failures{};
void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
template <class T> T* control(Dialog& dialog, const char* name)
{
    auto* widget = dialog.findChild<T*>(QString::fromLatin1(name));
    check(widget != nullptr, name);
    return widget;
}
sed::app::CameraPose pose()
{
    sed::app::CameraPose value;
    value.logicalSize = QSize(800, 600);
    value.orbit.yawDeg = 0.0f;
    value.orbit.pitchDeg = 0.0f;
    return value;
}
struct Fixture
{
    sed::app::CameraPose current{ pose() };
    std::vector<sed::app::SavedCamera> cameras;
    int applies{}, stores{};
    bool live{ true }, rejectStore{};
    Dialog::Context context()
    {
        return { cameras, [this] { return current; },
            [this](const auto& next, QString*)
            {
                current = next;
                ++applies;
                return true;
            },
            [this](const auto& next, QString* error)
            {
                if ( rejectStore )
                {
                    *error = "Synthetic workspace refusal";
                    return false;
                }
                cameras = next;
                ++stores;
                return true;
            },
            [this] { return live; } };
    }
};
void controlsAndNumericApply()
{
    Fixture fixture;
    Dialog dialog(fixture.context());
    auto* x = control<QDoubleSpinBox>(dialog, "savedCameraX");
    auto* pitch = control<QDoubleSpinBox>(dialog, "savedCameraPitch");
    auto* apply = control<QPushButton>(dialog, "savedCameraApply");
    control<QDoubleSpinBox>(dialog, "savedCameraYaw");
    control<QDoubleSpinBox>(dialog, "savedCameraRoll");
    control<QPushButton>(dialog, "savedCameraCapture");
    auto* warning = control<QLabel>(dialog, "savedCameraWarning");
    if ( warning )
    {
        check(warning->text().contains("workspace") && warning->text().contains("orbit"),
            "coordinate and ownership explanation is visible");
    }
    if ( !x || !pitch || !apply )
    {
        return;
    }
    x->setValue(17.25);
    pitch->setValue(30.0);
    check(fixture.applies == 0 && fixture.stores == 0, "typing does not navigate or store");
    apply->click();
    const auto eye =
        fixture.current.orbit.target() +
        sed::app::ViewportCamera::SphericalDirection(fixture.current.orbit.yawDeg, fixture.current.orbit.pitchDeg) *
            fixture.current.orbit.distance;
    check(fixture.applies == 1 && std::abs(eye.x() - 17.25f) < 1.0e-5f && fixture.current.orbit.pitchDeg == 30.0f,
        "explicit Apply uses numeric eye and orbit angles");
    dialog.reject();
    check(fixture.applies == 1 && fixture.stores == 0, "Close does not undo applied navigation or store a viewpoint");
}
void namedMutationAndStaleness()
{
    Fixture fixture;
    Dialog dialog(fixture.context());
    auto* name = control<QLineEdit>(dialog, "savedCameraName");
    auto* list = control<QListWidget>(dialog, "savedCameraList");
    auto* add = control<QPushButton>(dialog, "savedCameraAdd");
    auto* rename = control<QPushButton>(dialog, "savedCameraRename");
    auto* remove = control<QPushButton>(dialog, "savedCameraDelete");
    auto* recall = control<QPushButton>(dialog, "savedCameraRecall");
    auto* replace = control<QPushButton>(dialog, "savedCameraReplace");
    auto* status = control<QLabel>(dialog, "savedCameraStatus");
    if ( !name || !list || !add || !rename || !remove || !recall || !replace || !status )
    {
        return;
    }
    name->setText("  North   Room  ");
    add->click();
    check(fixture.stores == 1 && fixture.cameras.size() == 1 && fixture.cameras.front().name == "North Room",
        "named viewpoint is normalized and stored");
    check(fixture.applies == 0, "saving a viewpoint does not navigate");
    name->setText("north room");
    add->click();
    check(fixture.stores == 1 && status->text().contains("already exists"),
        "duplicate viewpoint refuses without mutation");
    name->setText("South");
    rename->click();
    check(fixture.stores == 2 && fixture.cameras.front().name == "South", "Rename updates selected viewpoint");
    fixture.rejectStore = true;
    remove->click();
    check(list->count() == 1 && fixture.stores == 2 && status->text().contains("Synthetic workspace refusal"),
        "store refusal preserves local and owner list");
    fixture.rejectStore = false;
    fixture.live = false;
    recall->click();
    replace->click();
    remove->click();
    check(fixture.applies == 0 && fixture.stores == 2 && status->text().contains("changed"),
        "stale document refuses all navigation and list mutations");
}

void limitsAndCompactLayout()
{
    Fixture fixture;
    Dialog dialog(fixture.context());
    auto* name = control<QLineEdit>(dialog, "savedCameraName");
    auto* add = control<QPushButton>(dialog, "savedCameraAdd");
    auto* status = control<QLabel>(dialog, "savedCameraStatus");
    name->setText(QString(129, 'a'));
    add->click();
    check(
        fixture.stores == 0 && status->text().contains("128"), "overlong name refuses rather than silently truncating");
    auto* distance = control<QDoubleSpinBox>(dialog, "savedCameraDistance");
    distance->setValue(0.0);
    name->setText("Invalid pose");
    add->click();
    check(fixture.stores == 0, "invalid numeric pose cannot become a saved viewpoint");
    dialog.ensurePolished();
    dialog.resize(1000, 536);
    dialog.layout()->activate();
    QPixmap rendered(dialog.size());
    dialog.render(&rendered);
    check(dialog.width() <= 1000 && dialog.height() <= 536, "saved camera form fits scaled 720p client area");
    for ( const char* action : { "savedCameraApply", "savedCameraCapture", "savedCameraRecall", "savedCameraAdd" } )
    {
        auto* button = control<QPushButton>(dialog, action);
        check(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint()), button->size())),
            "camera actions remain visible outside form scroll");
    }
    auto* scroll = control<QScrollArea>(dialog, "savedCameraScroll");
    auto* last = control<QCheckBox>(dialog, "savedCameraClip");
    scroll->ensureWidgetVisible(last);
    check(scroll->verticalScrollBar()->maximum() > 0 &&
              scroll->viewport()->rect().contains(QRect(last->mapTo(scroll->viewport(), QPoint()), last->size())),
        "last camera field remains reachable");
    Fixture full;
    for ( int i = 0; i < 256; ++i )
    {
        full.cameras.push_back({ QString::number(i), pose() });
    }
    Dialog fullDialog(full.context());
    control<QLineEdit>(fullDialog, "savedCameraName")->setText("Overflow");
    control<QPushButton>(fullDialog, "savedCameraAdd")->click();
    check(full.stores == 0 && full.cameras.size() == 256 &&
              control<QLabel>(fullDialog, "savedCameraStatus")->text().contains("256"),
        "list limit enforced before owner publication");
}
} // namespace
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const QString fontFile = qEnvironmentVariable("SED_TEST_FONT_FILE");
    if ( !fontFile.isEmpty() )
    {
        const int font = QFontDatabase::addApplicationFont(fontFile);
        check(font >= 0, "load explicit fixture font");
        if ( font >= 0 )
        {
            application.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 10));
        }
    }
    controlsAndNumericApply();
    namedMutationAndStaleness();
    limitsAndCompactLayout();
    if ( argc == 3 && QString::fromLocal8Bit(argv[1]) == "--snapshot" )
    {
        Fixture fixture;
        fixture.cameras.push_back({ QStringLiteral("North room"), pose() });
        Dialog dialog(fixture.context());
        dialog.ensurePolished();
        dialog.layout()->activate();
        QPixmap image(dialog.size());
        dialog.render(&image);
        check(image.save(QString::fromLocal8Bit(argv[2]), "PNG"), "render synthetic saved-camera controls");
    }
    std::cout << "Saved camera dialog failures: " << failures << '\n';
    return failures ? 1 : 0;
}
