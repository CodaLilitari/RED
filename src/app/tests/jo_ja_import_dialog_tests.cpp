#include "ui/resources/jo_ja_import_dialog.hpp"
#include "assets/raven_bsp_fixture.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/jo_ja_import.hpp"
#include "sed/assets/raven_bsp.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <stdexcept>

namespace
{
using Dialog = sed::ui::resources::JoJaImportDialog;

void require(bool value, const char* message)
{
    if ( !value )
    {
        throw std::runtime_error(message);
    }
}

template <class T> T& field(Dialog& dialog, const char* name)
{
    auto* item = dialog.findChild<T*>(QString::fromLatin1(name));
    require(item != nullptr, name);
    return *item;
}

void settle(Dialog& dialog)
{
    QElapsedTimer timer;
    timer.start();
    while ( field<QPushButton>(dialog, "joJaCancel").isEnabled() && timer.elapsed() < 15000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(!field<QPushButton>(dialog, "joJaCancel").isEnabled(), "worker timed out");
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
            "write fixture");
}

void run()
{
    QTemporaryDir dir;
    require(dir.isValid(), "private temporary directory");
    const auto fixture = sed::tests::raven_fixture::Fixture{}.bytes();
    const QByteArray bytes(reinterpret_cast<const char*>(fixture.data()), static_cast<int>(fixture.size()));
    const auto source = dir.filePath("sample.bsp");
    const auto out = dir.filePath("scene.gob");
    writeFile(source, bytes);
    bool current = true;
    int published = 0;
    std::function<void()> duringOwnerCheck;
    Dialog dialog({dir.path(),
                   [&]
                   {
                       if ( duringOwnerCheck )
                       {
                           duringOwnerCheck();
                       }
                       return current;
                   },
                   [&](const QString&) { ++published; }});
    auto& status = field<QLabel>(dialog, "joJaStatus");
    auto& prepare = field<QPushButton>(dialog, "joJaPrepare");
    auto& create = field<QPushButton>(dialog, "joJaCreate");
    auto& cancel = field<QPushButton>(dialog, "joJaCancel");
    auto& report = field<QPlainTextEdit>(dialog, "joJaReport");
    auto& destination = field<QLineEdit>(dialog, "joJaOutput");
    auto& name = field<QLineEdit>(dialog, "joJaLevelName");
    field<QLineEdit>(dialog, "joJaSource").setText(source);
    destination.setText(out);
    field<QPushButton>(dialog, "joJaInspect").click();
    settle(dialog);
    auto& shaders = field<QTableWidget>(dialog, "joJaShaders");
    require(shaders.rowCount() == 1 && shaders.item(0, 0)->text() == "a", "inspect exposes original shader");
    auto& rank = field<QCheckBox>(dialog, "joJaRankLightmaps");
    rank.setChecked(true);
    field<QPushButton>(dialog, "joJaInspect").click();
    settle(dialog);
    require(report.toPlainText().contains("0 grid probes") && report.toPlainText().contains("without a first atlas"),
            "World lightmap inspection explains a map with no source samples");
    rank.setChecked(false);
    prepare.click();
    settle(dialog);
    require(!create.isEnabled(), "missing MAT refuses preparation by default");
    field<QCheckBox>(dialog, "joJaPlaceholders").setChecked(true);
    name.setText("../escape");
    prepare.click();
    settle(dialog);
    require(!create.isEnabled(), "invalid level basename cannot publish");
    name.setText("scene");
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && !QFile::exists(out) && report.toPlainText().contains("SHA-256"),
            "prepare produces review only");
    auto& profile = field<QComboBox>(dialog, "joJaSourceProfile");
    require(report.toPlainText().contains("Source footstep profile: jo-mp"),
            "new GUI imports use the visible Outcast MP source profile");
    profile.setCurrentIndex(1);
    require(!create.isEnabled(), "changing source game revokes the reviewed archive");
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && report.toPlainText().contains("Source footstep profile: ja-mp"),
            "the reviewed archive uses the selected Academy MP source profile");
    auto& target = field<QComboBox>(dialog, "joJaTarget");
    auto& ordinary = field<QCheckBox>(dialog, "joJaOrdinaryWorldModels");
    require(ordinary.isEnabled() && !ordinary.isChecked(), "JK uses the level-geometry pass by default");
    auto& clips = field<QComboBox>(dialog, "joJaSourcePlayerClip");
    require(clips.isEnabled() && clips.currentIndex() == 0, "Player clips default to omission");
    auto& optimization = field<QComboBox>(dialog, "joJaMeshOptimization");
    require(optimization.count() == 3 && optimization.currentIndex() == 0 &&
                !report.toPlainText().contains("Mesh optimization"),
            "mesh optimization is off by default");
    optimization.setCurrentIndex(2);
    require(!create.isEnabled() && report.toPlainText().isEmpty(), "choosing a mesh optimization revokes the review");
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && report.toPlainText().contains("Mesh optimization: loose") &&
                report.toPlainText().contains("straight corners removed"),
            "the reviewed import welds, merges and prunes as chosen, and says so");
    optimization.setCurrentIndex(0);
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && !report.toPlainText().contains("Mesh optimization"), "off prepares the original recipe");
    clips.setCurrentIndex(1);
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && report.toPlainText().contains("Source player clips: standable, 0 hidden hulls"),
            "Editor prepares the selected policy on the real source");
    ordinary.click();
    require(!create.isEnabled() && report.toPlainText().isEmpty(), "changing the world pass revokes the review");
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && report.toPlainText().contains("World model render pass: ordinaryThing"),
            "the reviewed JK import uses the selected ordinary-Thing pass");
    target.setCurrentIndex(1);
    require(clips.currentIndex() == 0 && !clips.isEnabled(), "Switching to MotS clears the JK clip policy");
    require(!ordinary.isEnabled() && !ordinary.isChecked() && !create.isEnabled(),
            "switching to MotS resets the JK-only pass and revokes the prepared archive");
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && !report.toPlainText().contains("World model render pass: ordinaryThing"),
            "MotS prepares with its standard architecture lighting");
    target.setCurrentIndex(0);
    require(ordinary.isEnabled() && !ordinary.isChecked() && !create.isEnabled(),
            "returning to JK requires a fresh opt-in");
    prepare.click();
    settle(dialog);
    auto& geometry = field<QComboBox>(dialog, "joJaGeometryMode");
    auto& chunks = field<QSpinBox>(dialog, "joJaChunk");
    auto& triangles = field<QSpinBox>(dialog, "joJaMaxTriangles");
    auto& sectors = field<QSpinBox>(dialog, "joJaMaxSectors");
    auto& fragments = field<QSpinBox>(dialog, "joJaMaxFragments");
    require(geometry.currentIndex() == 0 && chunks.maximum() == 170 && triangles.maximum() == 100000,
            "model scene retains its original geometry limits");
    geometry.setCurrentIndex(1);
    require(!create.isEnabled() && chunks.value() == 400 && chunks.maximum() == 479 && triangles.value() == 200000
                && triangles.maximum() == 250000 && sectors.value() == 64 && fragments.value() == 500000,
            "hybrid mode revokes the review and suggests editable sector and mesh budgets");
    const auto rooms = sed::tests::makeRavenBspRoomsFixture();
    const auto roomsPath = dir.filePath("rooms.bsp");
    writeFile(roomsPath, QByteArray(reinterpret_cast<const char*>(rooms.data()), static_cast<int>(rooms.size())));
    field<QLineEdit>(dialog, "joJaSource").setText(roomsPath);
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && !QFile::exists(out) && report.toPlainText().contains("Native sectors 2")
                && report.toPlainText().contains("Residual model faces") && report.toPlainText().contains("Equivalent triangles")
                && report.toPlainText().contains("Native boundaries collide"),
            "hybrid rooms prepare with native boundary collision disclosed");
    sectors.setValue(2);
    require(!create.isEnabled(), "editing the native sector limit revokes the review");
    prepare.click();
    settle(dialog);
    require(create.isEnabled(), "edited sector limit can be prepared");
    fragments.setValue(450000);
    require(!create.isEnabled(), "editing the fragment budget revokes the review");
    geometry.setCurrentIndex(0);
    field<QLineEdit>(dialog, "joJaSource").setText(source);
    field<QPushButton>(dialog, "joJaInspect").click();
    settle(dialog);
    prepare.click();
    settle(dialog);
    name.setText("scene2");
    require(!create.isEnabled(), "option edit revokes prepared output");
    prepare.click();
    settle(dialog);
    writeFile(source, bytes + 'x');
    create.click();
    settle(dialog);
    require(!QFile::exists(out) && published == 0, "changed source prevents output");
    writeFile(source, bytes);
    prepare.click();
    settle(dialog);
    create.click();
    cancel.click();
    settle(dialog);
    require(!QFile::exists(out) && published == 0, "cancellation revokes staging");
    prepare.click();
    settle(dialog);
    create.click();
    current = false;
    settle(dialog);
    require(!QFile::exists(out) && published == 0, "stale owner revokes staging");
    current = true;
    prepare.click();
    settle(dialog);
    writeFile(out, "sentinel");
    create.click();
    settle(dialog);
    QFile existing(out);
    require(existing.open(QIODevice::ReadOnly) && existing.readAll() == "sentinel" && published == 0,
            "existing output is preserved");
    existing.close();
    require(QFile::remove(out), "clear owned test sentinel");
    prepare.click();
    settle(dialog);
    create.click();
    settle(dialog);
    QFile result(out);
    require(result.open(QIODevice::ReadOnly) && result.read(3) == "GOB" && published == 1,
            qPrintable(QStringLiteral("successful create-only publication: ") + status.text()));
    result.close();
    const auto mappedOut = dir.filePath("mapped.goo");
    const auto matPath = dir.filePath("material.mat");
    const sed::assets::RgbaImage image{8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 255)};
    writeFile(matPath, QByteArray::fromStdString(sed::assets::buildRgb565Mat({image}).bytes));
    field<QCheckBox>(dialog, "joJaPlaceholders").setChecked(false);
    field<QComboBox>(dialog, "joJaTarget").setCurrentIndex(1);
    auto& audio = field<QComboBox>(dialog, "joJaAudio");
    require(!(audio.model()->flags(audio.model()->index(1, 0)) & Qt::ItemIsEnabled),
            "unavailable source-audio import is disabled");
    require(audio.currentIndex() == 0, "stock JK/MotS footsteps are the default");
    shaders.item(0, 1)->setText(matPath);
    shaders.item(0, 2)->setText("material.mat");
    destination.setText(mappedOut);
    prepare.click();
    settle(dialog);
    require(create.isEnabled() && report.toPlainText().contains(matPath),
            "explicit MAT is captured and named in the review");
    audio.setCurrentIndex(1);
    prepare.click();
    settle(dialog);
    require(!create.isEnabled() && !QFile::exists(mappedOut),
            "unsupported source audio cannot produce a review or an archive");
    audio.setCurrentIndex(0);
    prepare.click();
    settle(dialog);
    int ownerChecks = 0;
    const auto originalMat = QByteArray::fromStdString(sed::assets::buildRgb565Mat({image}).bytes);
    auto sameSizeMat = originalMat;
    sameSizeMat[sameSizeMat.size() - 1] = static_cast<char>(sameSizeMat.back() ^ 1);
    duringOwnerCheck = [&]
    {
        if ( ++ownerChecks == 2 )
        {
            writeFile(matPath, sameSizeMat);
        }
    };
    create.click();
    settle(dialog);
    duringOwnerCheck = {};
    require(ownerChecks >= 2 && !QFile::exists(mappedOut) && published == 1,
            "a same-size MAT change in the completion-side owner check revokes publication");
    writeFile(matPath, originalMat);
    prepare.click();
    settle(dialog);
    writeFile(matPath, "changed MAT");
    create.click();
    settle(dialog);
    require(!QFile::exists(mappedOut) && published == 1, "changed mapped MAT prevents publication");
    writeFile(matPath, QByteArray::fromStdString(sed::assets::buildRgb565Mat({image}).bytes));
    prepare.click();
    settle(dialog);
    create.click();
    settle(dialog);
    require(QFile::exists(mappedOut) && published == 2, "explicitly mapped MotS archive is created");

    sed::tests::raven_fixture::Fixture twoShaders;
    twoShaders.lumps[1].resize(144);
    twoShaders.lumps[1][72] = std::byte{'b'};
    sed::tests::raven_fixture::write32(twoShaders.lumps[13], 148, 1);
    const auto secondBytes = twoShaders.bytes();
    const auto secondSource = dir.filePath("two-shaders.bsp");
    writeFile(secondSource,
              QByteArray(reinterpret_cast<const char*>(secondBytes.data()), static_cast<int>(secondBytes.size())));
    field<QLineEdit>(dialog, "joJaSource").setText(secondSource);
    field<QPushButton>(dialog, "joJaInspect").click();
    settle(dialog);
    require(shaders.rowCount() == 2 && shaders.columnCount() >= 4 && shaders.item(1, 3),
            "each inspected shader has its own skip control");
    shaders.item(0, 1)->setText(matPath);
    shaders.item(0, 2)->setText("material.mat");
    shaders.item(1, 3)->setCheckState(Qt::Checked);
    prepare.click();
    settle(dialog);
    require(create.isEnabled(), "an explicitly skipped shader needs no MAT");
    shaders.item(1, 1)->setText(matPath);
    shaders.item(1, 2)->setText("unused.mat");
    prepare.click();
    settle(dialog);
    require(!create.isEnabled(), "the same shader cannot be both mapped and skipped");
    shaders.item(1, 1)->setText({});
    destination.setText(dir.filePath("closed.goo"));
    prepare.click();
    settle(dialog);
    require(create.isEnabled(), "close test has a publishable review");
    create.click();
    dialog.reject();
    settle(dialog);
    require(!QFile::exists(destination.text()) && published == 2, "closing during staging revokes publication");
}
void checkCombinedCaptureBudget()
{
    QTemporaryDir dir;
    require(dir.isValid(), "budget fixture directory");
    const auto source = dir.filePath("budget.bsp");
    const auto mat = dir.filePath("budget.mat");
    const auto palette = dir.filePath("budget.cmp");
    sed::tests::raven_fixture::Fixture fixture;
    fixture.lumps[1].resize(8 * 72);
    for ( int i = 0; i < 8; ++i )
    {
        fixture.lumps[1][72 * i] = static_cast<std::byte>('a' + i);
    }
    const auto bytes = fixture.bytes();
    writeFile(source, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size())));
    Dialog dialog({});
    field<QLineEdit>(dialog, "joJaSource").setText(source);
    field<QPushButton>(dialog, "joJaInspect").click();
    settle(dialog);
    auto& shaders = field<QTableWidget>(dialog, "joJaShaders");
    require(shaders.rowCount() == 8, "budget fixture shader inventory");
    writeFile(mat, {});
    writeFile(palette, "x");
    require(QFile::resize(mat, sed::assets::kJoJaMaxMaterialBytes) &&
                QFile::resize(source, sed::assets::kRavenBspMaxBytes),
            "sparse input sizes");
    for ( int i = 0; i < shaders.rowCount(); ++i )
    {
        shaders.item(i, 1)->setText(mat);
        shaders.item(i, 2)->setText(QStringLiteral("m%1.mat").arg(i));
    }
    field<QLineEdit>(dialog, "joJaPalette").setText(palette);
    field<QPushButton>(dialog, "joJaPrepare").click();
    settle(dialog);
    require(field<QLabel>(dialog, "joJaStatus").text().contains("combined input capture limit"),
            "the optional palette counts against BSP plus mapped MATs before any bulk capture");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        run();
        checkCombinedCaptureBudget();
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
