#include "ui/resources/native_model_mesh_preview.hpp"

#include "sed/assets/native_model_document.hpp"

#include <QApplication>
#include <QImage>
#include <QPalette>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
using sed::assets::NativeMeshTransform;
using sed::assets::NativeModelDocument;
using sed::ui::resources::NativeModelMeshPreview;

void expect(bool condition, const char* message)
{
    if ( !condition ) throw std::runtime_error(message);
}

std::string sourceFixture()
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry/relay_sentry.3do";
    std::ifstream input(path, std::ios::binary);
    expect(input.is_open(), "Open the owned relay-sentry fixture");
    return std::string(std::istreambuf_iterator<char>(input), {});
}

void replaceAfter(std::string& source, std::size_t offset, const std::string& before, const std::string& after)
{
    const auto position = source.find(before, offset);
    expect(position != std::string::npos, "Find the expected fixture record");
    source.replace(position, before.size(), after);
}

std::string sharedAndOrphanFixture()
{
    auto source = sourceFixture();
    // Both nodes now instance mesh 0. Mesh 1 shares its name but is orphaned.
    replaceAfter(source, 0, "0x00001 1 0 -1 -1 0 0.02", "0x00001 0 0 -1 -1 0 0.42");
    replaceAfter(source, 0, "NAME boom", "NAME base");
    const auto lod = source.find("GEOSET 1");
    const auto mesh = source.find("MESH 1", lod);
    replaceAfter(source, mesh, "NAME boom", "NAME base");
    // The selected LOD 1 orphan is a 2:1 right triangle. The other LOD 1
    // triangle and both LOD 0 squares have a 1:1 aspect ratio.
    replaceAfter(source, mesh, "1: 0.030000 0.000000", "1: 0.060000 0.000000");
    return source;
}

QImage render(NativeModelMeshPreview& view)
{
    QImage image(view.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    view.render(&image);
    return image;
}

bool ink(const QImage& image, int x, int y)
{
    if ( !image.rect().contains(x, y) ) return false;
    const auto pixel = image.pixel(x, y);
    return qRed(pixel) >= 96 && qGreen(pixel) >= 96 && qBlue(pixel) >= 96;
}

QRect inkBounds(const QImage& image)
{
    QRect result;
    for ( int y = 0; y < image.height(); ++y )
    {
        for ( int x = 0; x < image.width(); ++x )
        {
            if ( ink(image, x, y) ) result |= QRect(x, y, 1, 1);
        }
    }
    return result;
}

bool hasEdgeNear(const QImage& image, QPoint point)
{
    for ( int y = point.y() - 3; y <= point.y() + 3; ++y )
    {
        for ( int x = point.x() - 3; x <= point.x() + 3; ++x )
        {
            if ( ink(image, x, y) ) return true;
        }
    }
    return false;
}

void checkSelectedSource(NativeModelMeshPreview& view)
{
    const auto source = sharedAndOrphanFixture();
    {
        auto temporary = NativeModelDocument::open(source);
        view.setMesh(temporary, 1, 1);
    }
    const auto orphan = render(view);
    const auto bounds = inkBounds(orphan);
    expect(bounds.width() > 120 && bounds.height() > 60 && std::abs(bounds.width() - 2 * bounds.height()) < 5,
        "The owned LOD 1 orphan must draw its distinct 2:1 shape after the caller releases the document");
    expect(hasEdgeNear(orphan, bounds.bottomLeft()) && hasEdgeNear(orphan, bounds.bottomRight())
        && hasEdgeNear(orphan, bounds.topLeft()) && !hasEdgeNear(orphan, bounds.topRight()),
        "The selected source wireframe must show the three corners of the orphan triangle");

    const auto document = NativeModelDocument::open(source);
    view.setMesh(document, 0, 1);
    const auto square = render(view);
    const auto squareBounds = inkBounds(square);
    expect(squareBounds.width() > 100 && std::abs(squareBounds.width() - squareBounds.height()) < 4
        && hasEdgeNear(square, squareBounds.topRight()), "Switching source LOD must draw the LOD 0 square");

    view.setMesh(document, 0, 0);
    const auto shared = render(view);
    expect(shared == square, "The shared source square must draw once in mesh-local space, just like the orphan square");
    auto changedHierarchy = source;
    replaceAfter(changedHierarchy, 0, "0.42 0 0.04 0 0 0 0.01", "0.73 0.1 0.08 0 60 0 0.09");
    view.setMesh(NativeModelDocument::open(changedHierarchy), 0, 0, true);
    expect(render(view) == shared, "Joint origins, rotations and pivots must not move the static source wireframe");

    view.setMesh(document, 1, 9);
    expect(inkBounds(render(view)).isEmpty(), "An invalid source mesh must clear stale edges");
    view.setMesh(document, 9, 0);
    expect(inkBounds(render(view)).isEmpty(), "An invalid LOD must be safe");
    view.setMesh(document, 1, 1);
    expect(render(view) == orphan, "Valid selection must restore the owned source geometry");
    view.clear();
    expect(inkBounds(render(view)).isEmpty(), "Clearing the selection must remove its geometry");
}

void checkTransformFraming(NativeModelMeshPreview& view)
{
    const auto document = NativeModelDocument::open(sharedAndOrphanFixture());
    view.setMesh(document, 1, 1);
    const auto baseline = render(view);
    const auto before = inkBounds(baseline);
    const auto edited = document.applyMeshTransforms(std::array{NativeMeshTransform{1, 1, 0.5, {0.01, 0.005, 0}}});
    view.setMesh(edited, 1, 1);
    const auto after = inkBounds(render(view));
    const auto width = before.right() - before.left();
    const auto height = before.bottom() - before.top();
    expect(std::abs((after.right() - after.left()) - width * 0.5) < 3
        && std::abs((after.bottom() - after.top()) - height * 0.5) < 3,
        "Preview must show the half-scale proposal under the existing camera");
    expect(std::abs(after.left() - before.left() - width / 6.0) < 3
        && std::abs(after.bottom() - before.bottom() + height / 6.0) < 3,
        "Preview must move the orphan by the proposed mesh-local translation");
    view.setMesh(document, 1, 1);
    expect(render(view) == baseline, "Restoring the baseline snapshot must restore the exact wireframe");
    const auto raised = document.applyMeshTransforms(std::array{NativeMeshTransform{1, 1, 1.0, {0, 0, 0.012}}});
    view.setMesh(raised, 1, 1);
    const auto elevated = inkBounds(render(view));
    expect(elevated.left() > before.left() + 5 && elevated.top() < before.top() - 5
        && std::abs(elevated.width() - before.width()) < 3 && std::abs(elevated.height() - before.height()) < 3,
        "Translation along the third axis must move the oblique wireframe without resizing it");
    view.setMesh(edited, 1, 1);
    view.reframe();
    const auto fitted = inkBounds(render(view));
    expect(std::abs(fitted.width() - before.width()) < 3 && std::abs(fitted.height() - before.height()) < 3,
        "Explicit Reframe must fit the edited mesh again");
    view.setMesh(document, 1, 1, true);
    expect(render(view) == baseline, "A replacement source at the same ordinals must support an explicit refit");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        NativeModelMeshPreview view(nullptr);
        view.setFixedSize(480, 320);
        QPalette palette;
        palette.setColor(QPalette::Base, Qt::black);
        palette.setColor(QPalette::Text, Qt::white);
        view.setPalette(palette);
        checkSelectedSource(view);
        checkTransformFraming(view);
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
