#include "ui/resources/native_model_uv_preview.hpp"

#include <QApplication>
#include <QImage>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using sed::assets::NativeModelDocument;
using sed::assets::NativeTextureVertexEdit;
using sed::ui::resources::NativeModelUvPreview;

void expect(bool condition, const char* message)
{
    if ( !condition ) throw std::runtime_error(message);
}

std::string sourceFixture(bool overBudget = false)
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry/relay_sentry.3do";
    std::ifstream input(path, std::ios::binary);
    expect(input.is_open(), "open owned two-LOD model");
    std::string source(std::istreambuf_iterator<char>{input}, {});
    const auto replace = [&](const std::string& from, const std::string& to)
    {
        const auto pos = source.find(from);
        expect(pos != source.npos, "locate owned fixture row");
        source.replace(pos, from.size(), to);
    };
    replace("MATERIALS 1\n0: relay_skin.mat", "MATERIALS 2\n0: relay_skin.mat\n1: seam.mat");
    replace("TEXTURE VERTICES 4\n0: 0 0\n1: 1 0\n2: 0 1\n3: 1 1",
            "TEXTURE VERTICES 6\n0: -1 0\n1: 1 0\n2: 0 2\n3: 1 0\n4: 7 -3\n5: 3 2");
    const auto start = source.find("FACES 2");
    const auto end = source.find("MESH 1", start);
    std::string faces;
    if ( overBudget )
    {
        constexpr int faceCount = 1025;
        faces = "FACES " + std::to_string(faceCount) + "\n";
        for ( int i = 0; i < faceCount; ++i )
        {
            // Only the last face uses slot zero. The budget covers all slots.
            faces += std::to_string(i) + (i == faceCount - 1 ? ": 0" : ": 1") + " 0x0 4 3 3 0.0 256";
            for ( int j = 0; j < 256; ++j ) faces += " 1,1";
            faces += '\n';
        }
        faces += "FACE NORMALS\n";
        for ( int i = 0; i < faceCount; ++i ) faces += std::to_string(i) + ": 0 0 1\n";
    }
    else
    {
        faces = "FACES 3\n0: 0 0x0 4 3 3 0.0 3 1,1 2,2 3,3\n"
                "1: 0 0x0 4 3 3 0.0 3 0,0 3,1 2,2\n"
                "2: 1 0x0 4 3 3 0.0 3 3,5 1,1 0,0\nFACE NORMALS\n"
                "0: 0 0 1\n1: 0 0 1\n2: 0 0 1\n";
    }
    source.replace(start, end - start, faces);
    return source;
}

QImage render(NativeModelUvPreview& view, int scale = 1)
{
    QImage image(view.size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    view.render(&image);
    return image;
}

void checkCanvas()
{
    NativeModelUvPreview view;
    view.resize(600, 400);
    const auto source = sourceFixture();
    const auto doc = NativeModelDocument::open(source);
    view.setModel(doc, {0, 0, 1}, 0);
    using Address = NativeModelUvPreview::CornerAddress;
    expect(view.overlayAvailable() && view.selectedCorners() == std::vector<Address>{{0,0}, {1,1}}
        && view.totalSelectedUses() == 3, "shared row has exact material-specific corner addresses and cross-slot count");
    QImage tile(3, 2, QImage::Format_RGB32);
    tile.fill(Qt::red);
    tile.setPixelColor(2, 0, Qt::green);
    tile.setPixelColor(2, 1, Qt::blue);
    view.setMaterial(tile, tile.size());
    const auto origin = view.uvToWidget({0, 0});
    const auto opposite = view.uvToWidget({3, 2});
    expect(opposite.x() > origin.x() && opposite.y() > origin.y()
        && std::abs((opposite.x()-origin.x()) / (opposite.y()-origin.y()) - 1.5) < 0.001,
        "U right, V down and nonsquare origin tile in native texture pixels");
    for ( int scale : {1, 2} )
    {
        const auto pixels = render(view, scale);
        const auto sample = [&](QPointF uv) { return pixels.pixelColor((view.uvToWidget(uv) * scale).toPoint()); };
        expect(sample({2.5, 0.5}) == QColor(Qt::green) && sample({2.5, 1.5}) == QColor(Qt::blue)
            && sample({0.5, 1.5}) == QColor(Qt::red), "origin tile uses actual oriented cel pixels at both scales");
        expect(sample({-0.5, 1.5}) != QColor(Qt::blue), "negative repeat has no second material tile");
    }
    const auto frame = view.frameBounds();
    const auto edited = doc.applyTextureVertexEdits(std::array{NativeTextureVertexEdit{0,0,1,1e34f,-1e34f}});
    view.setModel(edited, {0,0,1}, 0);
    expect(view.frameBounds() == frame && render(view).size() == view.size(), "finite extreme proposals preserve frame and render safely");
    tile.fill(Qt::yellow);
    view.setMaterial(tile, tile.size());
    expect(view.frameBounds() == frame, "same-size palette replacement keeps the inspection frame");
    view.reframe();
    expect(view.frameBounds().contains(QPointF(1e34f, -1e34f)), "explicit reframe includes extreme UV");
    view.setModel(doc, {0,0,4}, 0, true);
    expect(view.totalSelectedUses() == 0 && view.selectedCorners().empty()
        && view.frameBounds().contains(QPointF(7,-3)), "unused row participates in fit");
    const auto isolated = render(view);
    view.setModel(doc, {0,0,std::nullopt}, 0);
    expect(render(view) != isolated, "isolated selected row has a visible marker");
    view.setModel(doc, {0,0,1}, 1);
    expect(view.selectedCorners() == std::vector<Address>{{2,1}}, "slot change follows material identity");
    view.setModel(doc, {0,0,std::nullopt}, 1);
    const auto diagonal = [&](float extent)
    {
        const std::array edits{NativeTextureVertexEdit{0,0,5,-extent,-extent},
            NativeTextureVertexEdit{0,0,1,extent,extent}, NativeTextureVertexEdit{0,0,0,-extent,-extent}};
        view.setModel(doc.applyTextureVertexEdits(edits), {0,0,std::nullopt}, 1);
        return render(view);
    };
    const auto moderate = diagonal(32);
    const auto extreme = diagonal(1e34f);
    const auto middle = view.uvToWidget({0.5, 0.5}).toPoint();
    const QRect crossing(middle.x() - 4, middle.y() - 4, 9, 9);
    expect(moderate.copy(crossing) == extreme.copy(crossing),
        "extreme opposite endpoints must preserve the visible diagonal through the source origin");
    const std::array collapsedEdits{NativeTextureVertexEdit{0,0,5,1e34f,1e34f},
        NativeTextureVertexEdit{0,0,1,1e34f,1e34f}, NativeTextureVertexEdit{0,0,0,1e34f,1e34f}};
    const auto collapsed = doc.applyTextureVertexEdits(collapsedEdits);
    view.setModel(collapsed, {0,0,1}, 1, true);
    const auto selectedExtreme = render(view);
    view.setModel(collapsed, {0,0,std::nullopt}, 1);
    expect(render(view) != selectedExtreme, "a selected row remains visible when an extreme point fills the frame");
    const std::array verticalEdits{NativeTextureVertexEdit{0,0,5,1e34f,0},
        NativeTextureVertexEdit{0,0,1,1e34f,2}, NativeTextureVertexEdit{0,0,0,1e34f,0}};
    view.setModel(doc.applyTextureVertexEdits(verticalEdits), {0,0,std::nullopt}, 1, true);
    const auto anchoredCenter = view.uvToWidget({1e34f, 1}).toPoint();
    const std::array wideEdits{NativeTextureVertexEdit{0,0,5,0,1},
        NativeTextureVertexEdit{0,0,1,2e34f,1}, NativeTextureVertexEdit{0,0,0,0,1}};
    view.setModel(doc.applyTextureVertexEdits(wideEdits), {0,0,std::nullopt}, 1);
    const auto wide = render(view);
    bool offCenterEdge = false;
    for ( int dy = -2; dy <= 2; ++dy )
    {
        const auto pixel = wide.pixelColor(anchoredCenter + QPoint(60, dy));
        if ( pixel.blue() - pixel.red() > 32 ) offCenterEdge = true;
    }
    expect(offCenterEdge, "frame centered at extreme U retains the visible width of a crossing edge");
    const float h = std::ldexp(1.0f, 100);
    const std::array pointEdits{NativeTextureVertexEdit{0,0,5,3*h,h},
        NativeTextureVertexEdit{0,0,1,3*h,h}, NativeTextureVertexEdit{0,0,0,3*h,h}};
    view.setModel(doc.applyTextureVertexEdits(pointEdits), {0,0,std::nullopt}, 1, true);
    const auto center = view.uvToWidget({3*h, h}).toPoint();
    const std::array slopeEdits{NativeTextureVertexEdit{0,0,5,0,0},
        NativeTextureVertexEdit{0,0,1,6*h,2*h}, NativeTextureVertexEdit{0,0,0,0,0}};
    view.setModel(doc.applyTextureVertexEdits(slopeEdits), {0,0,std::nullopt}, 1);
    const auto shallow = render(view);
    bool shallowEdge = false;
    for ( int dy = -2; dy <= 2; ++dy )
    {
        const auto pixel = shallow.pixelColor(center + QPoint(60, 20 + dy));
        if ( pixel.blue() - pixel.red() > 32 ) shallowEdge = true;
    }
    expect(shallowEdge, "one-third slope through a distant frame center retains its visible segment");
    view.setModel(doc, {1,0,1}, 0);
    expect(view.selectedCorners() == std::vector<Address>{{0,1}}, "LOD switch follows raw source mesh");
    view.setMaterial({}, {3,2});
    view.reframe();
    expect(view.frameBounds().contains(QPointF(3,2)), "missing colors retain known dimensions");
    view.setMaterial({}, {});
    view.reframe();
    expect(!view.frameBounds().contains(QPointF(3,2)), "unknown material contributes no tile size");
    expect(doc.source() == source, "canvas leaves source bytes unchanged");
    view.setModel(NativeModelDocument::open(sourceFixture(true)), {0,0,1}, 0);
    expect(!view.overlayAvailable() && !view.overlayError().isEmpty() && view.selectedCorners().empty() && view.totalSelectedUses() == 0,
        "over-budget mesh refuses the whole overlay and readout");
    view.clear();
    expect(!view.overlayAvailable() && view.selectedCorners().empty(), "clear removes stale addresses");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try { checkCanvas(); }
    catch ( const std::exception& error ) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
