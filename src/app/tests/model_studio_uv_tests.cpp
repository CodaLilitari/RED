#include "ui/resources/model_studio_dialog.hpp"
#include "ui/resources/model_uv_inspector.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QThread>

#include <filesystem>
#include <stdexcept>

namespace
{
using namespace sed::ui::resources;

void expect(bool condition, const char* diagnostic)
{
    if ( !condition ) throw std::runtime_error(diagnostic);
}

template<typename Widget> Widget& control(QWidget& owner, const char* name)
{
    auto* found = owner.findChild<Widget*>(QString::fromLatin1(name));
    expect(found != nullptr, name);
    return *found;
}

template<typename Predicate> void waitFor(Predicate ready)
{
    QDeadlineTimer deadline(10000);
    while ( !ready() && !deadline.hasExpired() )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    expect(ready(), "Studio UV interaction did not finish");
}

void press(ModelStudioDialog& studio, const char* name)
{
    auto& button = control<QPushButton>(studio, name);
    expect(button.isEnabled(), name);
    button.click();
    waitFor([&] { return !studio.state().busy; });
}

QByteArray read(const QString& path)
{
    QFile file(path);
    expect(file.open(QIODevice::ReadOnly), "open Studio UV fixture");
    return file.readAll();
}

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write Studio UV fixture");
}

QImage render(NativeModelUvPreview& canvas)
{
    QImage result(canvas.size(), QImage::Format_ARGB32);
    result.fill(Qt::black);
    canvas.render(&result);
    return result;
}

bool selectedAt(const QImage& image, NativeModelUvPreview& canvas, QPointF sourceUv)
{
    const auto point = canvas.uvToWidget(sourceUv).toPoint();
    expect(image.rect().contains(point), "expected UV marker is outside the retained frame");
    return image.pixelColor(point) == QColor(255, 181, 55);
}
}

void runModelStudioUvTests(const QString& source, const QString& directory, bool evidence)
{
    const QDir dir(directory);
    auto original = read(source);
    // Row 1 is shared by two source faces. Row 3 is a distinct seam whose
    // coordinates coincide with row 1; selecting/editing row 1 must not move it.
    expect(original.contains("3: 1 1\n"), "owned seam fixture changed");
    original.replace("3: 1 1\n", "3: 1 0\n");
    const QByteArray face("1: 0 0x0 4 3 3 0.000000 3 1,1 3,3 2,2");
    const auto faceOffset = original.indexOf(face);
    expect(faceOffset >= 0, "owned seam face changed");
    // Geometric vertex 1 now uses row 1 in face 0 and row 3 in face 1.
    // Row 1 remains shared by both faces, at different geometric corners.
    original.replace(faceOffset, face.size(), "1: 0 0x0 4 3 3 0.000000 3 1,3 3,1 2,2");
    const auto modelPath = dir.filePath(QStringLiteral("uv-shared-seam.3do"));
    const auto palettePath = dir.filePath(QStringLiteral("uv-inspection.cmp"));
    write(modelPath, original);
    QByteArray palette(832 + 64 * 256, '\0');
    palette.replace(0, 4, "CMP ");
    palette[64 + 25 * 3] = char(80);
    palette[64 + 25 * 3 + 1] = char(20);
    write(palettePath, palette);
    const auto materialPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry/relay_skin.mat";

    ModelStudioDialog studio({});
    studio.show();
    control<QLineEdit>(studio, "modelStudioSource").setText(modelPath);
    press(studio, "modelStudioOpen");
    expect(studio.state().committed.has_value(), "Studio UV source admitted");
    auto& panelWidget = control<QWidget>(studio, "modelStudioUvInspector");
    auto* panel = dynamic_cast<ModelUvInspector*>(&panelWidget);
    expect(panel != nullptr, "Studio embeds the material inspector");
    auto& tabs = control<QTabWidget>(studio, "modelStudioVisualTabs");
    tabs.setCurrentWidget(panel);
    auto& canvas = *panel->canvas();
    canvas.setFixedSize(480, 320);
    control<QComboBox>(studio, "modelStudioUvRow").setCurrentIndex(2);
    waitFor([&] { return !studio.state().busy; });
    control<QComboBox>(studio, "modelStudioOperation").setCurrentIndex(1);
    control<QLineEdit>(studio, "uvMaterialPath").setText(QString::fromStdString(materialPath.string()));
    control<QLineEdit>(studio, "uvPalettePath").setText(palettePath);
    control<QPushButton>(studio, "uvLoadMaterial").click();
    waitFor([&] { return !panel->busy(); });
    expect(panel->capture() && !panel->capture()->image.isNull(), "Studio captures the requested MAT and CMP");
    const auto hash = panel->capture()->material.sha256;
    const auto paletteHash = panel->capture()->palette.sha256;
    const auto frame = canvas.frameBounds();
    const auto initial = studio.state();
    const auto baseline = render(canvas);
    expect(canvas.selectedCorners() == std::vector<NativeModelUvPreview::CornerAddress>{{0, 1}, {1, 1}}
        && canvas.totalSelectedUses() == 2 && selectedAt(baseline, canvas, {1, 0}),
        "Studio selection maps the shared source row to both exact corners");

    const auto previewUv = [&]
    {
        control<QLineEdit>(studio, "modelStudioU").setText(QStringLiteral("0.25"));
        control<QLineEdit>(studio, "modelStudioV").setText(QStringLiteral("1.5"));
        press(studio, "modelStudioPreview");
    };
    previewUv();
    const auto proposal = studio.state();
    const auto after = render(canvas);
    expect(proposal.revision == initial.revision && proposal.committed->source() == initial.committed->source()
        && proposal.displayed->geosets()[0].meshes[0].texVertices[1].u == 0.25f
        && proposal.displayed->geosets()[0].meshes[0].texVertices[3].u == 1.0f,
        "UV proposal moves the shared row and preserves the coincident seam and committed revision");
    expect(canvas.frameBounds() == frame && selectedAt(after, canvas, {0.25, 1.5})
        && !selectedAt(after, canvas, {1, 0}) && panel->capture()->material.sha256 == hash,
        "Studio renders the unsaved row with stable framing and the same captured material");
    if ( evidence )
    {
        studio.resize(1220, 1020);
        QApplication::processEvents();
        expect(studio.grab().save(dir.filePath(QStringLiteral("model-studio-uv-preview.png"))),
            "capture actual material-backed Studio UV preview");
    }
    press(studio, "modelStudioCancel");
    expect(render(canvas) == baseline && studio.state().displayed->source() == initial.committed->source(),
        "Cancel restores exact UV display and authored bytes");
    previewUv();
    press(studio, "modelStudioApply");
    expect(render(canvas) == after && studio.state().canUndo, "Apply retains the accepted visual UV proposal");
    press(studio, "modelStudioUndo");
    expect(render(canvas) == baseline && !studio.state().dirty, "Undo restores source UV display and clean revision");
    press(studio, "modelStudioRedo");
    expect(render(canvas) == after && studio.state().dirty, "Redo restores the accepted source UV display");

    // A visual inspection must never constrain legal tiled coordinates. Publish
    // the same cumulative negative/tiled row to two create-only variants.
    control<QLineEdit>(studio, "modelStudioU").setText(QStringLiteral("-4"));
    control<QLineEdit>(studio, "modelStudioV").setText(QStringLiteral("7"));
    press(studio, "modelStudioPreview");
    press(studio, "modelStudioApply");
    const auto first = dir.filePath(QStringLiteral("uv-variant-first.3do"));
    const auto second = dir.filePath(QStringLiteral("uv-variant-second.3do"));
    const auto outputHash = QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromStdString(std::string(studio.state().committed->source())), QCryptographicHash::Sha256).toHex());
    for ( const auto& output : {first, second} )
    {
        control<QLineEdit>(studio, "modelStudioOutput").setText(output);
        press(studio, "modelStudioReview");
        expect(control<QPlainTextEdit>(studio, "modelStudioReviewText").toPlainText().contains(outputHash),
            "each UV variant review identifies the exact cumulative output bytes");
        press(studio, "modelStudioSave");
    }
    expect(read(first) == read(second) && read(first).toStdString() == studio.state().committed->source()
        && read(modelPath) == original && canvas.frameBounds() == frame,
        "UV inspection preserves cumulative deterministic publication and never overwrites the input");
    const auto reopened = sed::assets::NativeModelDocument::open(read(first).toStdString());
    expect(reopened.geosets()[0].meshes[0].texVertices[1].u == -4.0f
        && reopened.geosets()[0].meshes[0].texVertices[1].v == 7.0f
        && reopened.geosets()[0].meshes[0].texVertices[3].u == 1.0f,
        "both published variants preserve negative/tiled UVs and the separate seam");
    if ( evidence )
    {
        const QJsonArray operations{QJsonObject{{"op", "setTextureVertex"}, {"geoset", 0}, {"sourceMesh", 0},
            {"textureVertex", 1}, {"u", -4.0}, {"v", 7.0}}};
        const QJsonArray candidates{
            QJsonObject{{"candidateFile", "uv-variant-first.3do"}, {"reviewedOutputSha256", outputHash}},
            QJsonObject{{"candidateFile", "uv-variant-second.3do"}, {"reviewedOutputSha256", outputHash}}};
        const QJsonObject receipt{{"sourceFile", "uv-shared-seam.3do"},
            {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex())},
            {"materialSha256", hash}, {"paletteSha256", paletteHash}, {"candidates", candidates},
            {"operations", operations}, {"originalUnchanged", true}, {"runtimeVerified", false}};
        write(dir.filePath(QStringLiteral("uv-inspection-receipt.json")), QJsonDocument(receipt).toJson());
    }
}
