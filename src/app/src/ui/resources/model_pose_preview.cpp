#include "ui/resources/model_pose_preview.hpp"
#include "sed/assets/model_hierarchy.hpp"
#include "sed/assets/pup.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>

namespace sed::ui::resources
{
namespace
{
QString coordinates(const TVector &v)
{
    return QStringLiteral("(%1, %2, %3)").arg(v.x, 0, 'g', 5).arg(v.y, 0, 'g', 5).arg(v.z, 0, 'g', 5);
}

QString identityName(const std::filesystem::path &path)
{
    const auto name = path.filename();
#ifdef _WIN32
    return QString::fromStdWString(name.native());
#else
    return QString::fromUtf8(name.native().data(), static_cast<qsizetype>(name.native().size()));
#endif
}

bool drawableFace(const assets::Model3doFace &face, std::size_t vertices)
{
    return face.vertices.size() >= 2 && std::all_of(face.vertices.begin(), face.vertices.end(), [vertices](auto index) {
        return index >= 0 && static_cast<std::size_t>(index) < vertices;
    });
}

class PoseView final : public QWidget
{
  public:
    PoseView(const std::filesystem::path &modelPath, const assets::Model3do &hierarchy, const assets::KeyFile &key)
        : path_(modelPath), legacyFirst_(hierarchy), playback_(legacyFirst_, key)
    {
        initialize();
    }
    PoseView(assets::NativeModelDocument snapshot, const assets::KeyFile &key)
        : snapshot_(std::move(snapshot)), playback_(snapshot_->geosets().front(), key)
    {
        initialize();
    }
    const assets::Model3do &first() const
    {
        return snapshot_ ? snapshot_->geosets().front() : legacyFirst_;
    }
    const assets::Model3do &paneModel(int pane) const
    {
        if ( !pane || !compareLod ) return first();
        return snapshot_ ? snapshot_->geosets()[static_cast<std::size_t>(compareLod)] : other;
    }
    void selectLod(int index)
    {
        if ( index < 0 || (snapshot_ && static_cast<std::size_t>(index) >= snapshot_->geosets().size()) )
            throw std::runtime_error("The comparison LOD is outside the captured model.");
        compareLod = index;
        if ( index && !snapshot_ )
            other = assets::loadFrom3DO(path_, {.lod = index, .applyHierarchyTransforms = false});
        else
            other = {};
        reframe();
    }
    void setFrame(double frame)
    {
        pose = playback_.sample(frame, false);
        update();
    }
    void reframe()
    {
        bool found = false;
        TVector lower{}, upper{};
        for ( int pane = 0; pane < 2; ++pane )
        {
            forEachJointMesh(paneModel(pane), [&](std::size_t joint, const assets::Model3doMesh &mesh) {
                for ( const auto &face : mesh.faces )
                {
                    if ( !drawableFace(face, mesh.vertices.size()) ) continue;
                    for ( const auto index : face.vertices )
                    {
                        const auto &vertex = mesh.vertices[static_cast<std::size_t>(index)];
                        const auto point = pose[joint].meshToModel.TransformPoint({vertex.x, vertex.y, vertex.z});
                        if ( !found )
                        {
                            lower = upper = point;
                            found = true;
                        }
                        else
                        {
                            lower.x = std::min(lower.x, point.x);
                            lower.y = std::min(lower.y, point.y);
                            lower.z = std::min(lower.z, point.z);
                            upper.x = std::max(upper.x, point.x);
                            upper.y = std::max(upper.y, point.y);
                            upper.z = std::max(upper.z, point.z);
                        }
                    }
                }
            });
        }
        frameCenter_ = found ? (lower + upper) * 0.5 : TVector{};
        const auto extent = (upper - lower) * 0.5;
        frameRadius_ = std::max(0.001, found ? std::hypot(extent.x, extent.y, extent.z) : first().radius);
        update();
    }
    assets::Model3do other;
    std::vector<assets::ModelPoseJoint> pose;
    int compareLod{}, selected{}, direction{};

  private:
    void initialize()
    {
        setMinimumSize(400, 190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setObjectName("poseView");
        setFrame(0);
    }
    template<class Visit>
    void forEachJointMesh(const assets::Model3do &model, Visit &&visit) const
    {
        for ( std::size_t joint = 0; joint < pose.size(); ++joint )
        {
            const auto mesh = first().nodes[joint].mesh;
            if ( mesh < 0 || static_cast<std::size_t>(mesh) >= model.meshes.size() ) continue;
            visit(joint, model.meshes[static_cast<std::size_t>(mesh)]);
        }
    }
    // The snapshot path retains the complete immutable source and all raw
    // geosets, including UVs. It never supplies a filesystem model source.
    const std::optional<assets::NativeModelDocument> snapshot_;
    const std::filesystem::path path_;
    const assets::Model3do legacyFirst_;
    assets::ModelKeyPlayback playback_;
    TVector frameCenter_{};
    double frameRadius_{0.001};

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setRenderHint(QPainter::Antialiasing);
        const double half = width() * 0.5;
        const double scale = 0.43 * std::min(half, static_cast<double>(height() - 24)) / frameRadius_;
        for (int view = 0; view < 2; ++view)
        {
            painter.save();
            painter.setClipRect(QRectF(half * view, 0, half, height()));
            const auto &model = paneModel(view);
            const auto project = [&](const TVector &point) {
                const auto p = point - frameCenter_;
                double x = p.x, y = p.z;
                if (direction == 1)
                {
                    x = p.x;
                    y = p.y;
                }
                if (direction == 2)
                {
                    x = p.y;
                    y = p.z;
                }
                return QPointF(half * (view + 0.5) + x * scale, (height() + 24) * 0.5 - y * scale);
            };
            painter.setPen(palette().text().color());
            painter.drawText(QRectF(half * view + 8, 2, half - 16, 20), Qt::AlignLeft, QStringLiteral("LOD %1").arg(view ? compareLod : 0));
            forEachJointMesh(model, [&](std::size_t joint, const assets::Model3doMesh &mesh) {
                QVector<QPointF> projected;
                projected.reserve(static_cast<qsizetype>(mesh.vertices.size()));
                for (const auto &vertex : mesh.vertices)
                    projected.push_back(project(pose[joint].meshToModel.TransformPoint({vertex.x, vertex.y, vertex.z})));
                painter.setPen(QPen(joint == static_cast<std::size_t>(selected) ? palette().highlight().color() : palette().text().color(), 1));
                for (const auto &face : mesh.faces)
                {
                    if ( !drawableFace(face, mesh.vertices.size()) ) continue;
                    QPolygonF polygon;
                    for (auto vertex : face.vertices)
                        polygon.push_back(projected[vertex]);
                    painter.drawPolygon(polygon);
                }
            });
            if (selected >= 0 && static_cast<std::size_t>(selected) < pose.size())
            {
                painter.setPen(QPen(palette().highlight().color(), 2));
                painter.drawEllipse(project(pose[selected].origin), 4, 4);
                const auto pivot = project(pose[selected].meshToModel.dvec);
                painter.drawLine(pivot + QPointF(-4, 0), pivot + QPointF(4, 0));
                painter.drawLine(pivot + QPointF(0, -4), pivot + QPointF(0, 4));
            }
            painter.restore();
        }
        painter.setPen(palette().mid().color());
        painter.drawLine(QPointF(half, 0), QPointF(half, height()));
    }
};

QDialog *createPosePreviewDialog(QWidget *parent, std::unique_ptr<PoseView> ownedView, const assets::KeyFile &key,
                                 const QString &keyName)
{
    auto dialog = std::make_unique<QDialog>(parent);
    dialog->setWindowTitle(keyName.isEmpty() ? QStringLiteral("KEY playback") : QStringLiteral("KEY playback — %1").arg(keyName));
    auto *layout = new QVBoxLayout(dialog.get());
    auto *view = ownedView.get();
    layout->addWidget(view, 1);
    ownedView.release();
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    auto *play = new QPushButton(QStringLiteral("Play"));
    play->setObjectName("posePlay");
    play->setCheckable(true);
    auto *loop = new QCheckBox(QStringLiteral("Loop"));
    loop->setChecked(true);
    auto *frame = new QDoubleSpinBox;
    frame->setObjectName("poseFrame");
    frame->setDecimals(3);
    frame->setRange(0, key.frames);
    frame->setPrefix(QStringLiteral("Frame "));
    frame->setSuffix(QStringLiteral(" / %1 · %2 fps").arg(key.frames).arg(key.fps));
    auto *markers = new QComboBox;
    markers->setObjectName("poseMarkers");
    markers->addItem(QStringLiteral("Jump to marker"));
    for (const auto &marker : key.markers)
        markers->addItem(QStringLiteral("%1 · frame %2").arg(marker.markerId).arg(marker.frame), marker.frame);
    for (QWidget *item : std::initializer_list<QWidget *>{play, loop, frame, markers})
        row->addWidget(item);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *reframe = new QPushButton(QStringLiteral("Reframe"));
    reframe->setObjectName("poseReframe");
    reframe->setToolTip(QStringLiteral("Fit both displayed LODs at the current frame. Playback keeps this framing."));
    row->addWidget(reframe);
    auto *lod = new QComboBox;
    lod->setObjectName("poseCompareLod");
    for (std::size_t i = 0; i < view->first().geosetCount; ++i)
        lod->addItem(QString::number(i));
    auto *direction = new QComboBox;
    direction->addItems({QStringLiteral("Front (X/Z)"), QStringLiteral("Top (X/Y)"), QStringLiteral("Side (Y/Z)")});
    auto *joints = new QComboBox;
    joints->setObjectName("poseJoint");
    for (const auto &node : view->first().nodes)
        joints->addItem(QStringLiteral("%1 · %2").arg(node.id).arg(QString::fromStdString(node.name)));
    form->addRow(QStringLiteral("Compare with LOD 0"), lod);
    form->addRow(QStringLiteral("View both from"), direction);
    form->addRow(QStringLiteral("Inspect joint"), joints);
    auto *detail = new QLabel;
    detail->setWordWrap(true);
    detail->setObjectName("poseJointReadout");
    layout->addWidget(detail);
    auto *pupButton = new QPushButton(QStringLiteral("Inspect PUP association…"));
    auto *association = new QLabel;
    association->setWordWrap(true);
    form->addRow(pupButton, association);
    const auto refresh = [view, frame, joints, detail] {
        view->selected = joints->currentIndex();
        view->setFrame(frame->value());
        if (view->selected < 0 || static_cast<std::size_t>(view->selected) >= view->pose.size()) return;
        const auto index = static_cast<std::size_t>(view->selected);
        const auto &pose = view->pose[index];
        const auto &node = view->first().nodes[index];
        const QString meshRadius = (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < view->first().meshes.size())
                                       ? QString::number(view->first().meshes[node.mesh].authoredRadius)
                                       : QStringLiteral("—");
        detail->setText(QStringLiteral("Joint origin %1 · rotation %2 · local pivot %3 · LOD 0 authored mesh radius %4 · derived model radius %5")
                            .arg(coordinates(pose.origin), coordinates(pose.rotation), coordinates(node.pivot), meshRadius)
                            .arg(view->first().radius));
    };
    QObject::connect(reframe, &QPushButton::clicked, view, [view] { view->reframe(); });
    QObject::connect(frame, &QDoubleSpinBox::valueChanged, view, refresh);
    QObject::connect(joints, &QComboBox::currentIndexChanged, view, refresh);
    QObject::connect(lod, &QComboBox::currentIndexChanged, view, [view, detail](int index) {
        try
        {
            view->selectLod(index);
        }
        catch (const std::exception &e)
        {
            view->other = {};
            view->update();
            detail->setText(QString::fromUtf8(e.what()));
        }
    });
    QObject::connect(direction, &QComboBox::currentIndexChanged, view, [view](int index) {
        view->direction = index;
        view->update();
    });
    QObject::connect(markers, &QComboBox::currentIndexChanged, frame, [frame, markers, play](int index) {
        if (index > 0)
        {
            play->setChecked(false);
            frame->setValue(markers->itemData(index).toDouble());
        }
    });
    QObject::connect(pupButton, &QPushButton::clicked, association, [association, keyName] {
        auto file = QFileDialog::getOpenFileName(association, QStringLiteral("Inspect puppet"), {}, QStringLiteral("PUP (*.pup)"));
        if (file.isEmpty()) return;
        try
        {
            const auto pup = assets::loadFromPUP(file.toStdString());
            QStringList bindings;
            bool found = false;
            for (const auto &binding : pup.bindings)
            {
                const auto name = QString::fromStdString(binding.key);
                found = found || keyName.compare(name, Qt::CaseInsensitive) == 0;
                bindings << QStringLiteral("%1 → %2").arg(QString::fromStdString(binding.action), name);
            }
            association->setText(QStringLiteral("%1 · %2").arg(
                keyName.isEmpty() ? QStringLiteral("No captured KEY filename") :
                    found ? QStringLiteral("References this KEY") : QStringLiteral("No reference to this KEY"), bindings.join(", ")));
        }
        catch (const std::exception &e)
        {
            association->setText(QString::fromUtf8(e.what()));
        }
    });
    auto *timer = new QTimer(view);
    timer->setInterval(32);
    auto elapsed = std::make_shared<QElapsedTimer>();
    QObject::connect(play, &QPushButton::toggled, view, [play, timer, elapsed, frame](bool playing) {
        play->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        if (!playing)
        {
            timer->stop();
            return;
        }
        if (frame->value() >= frame->maximum()) frame->setValue(0);
        elapsed->start();
        timer->start();
    });
    QObject::connect(timer, &QTimer::timeout, view, [elapsed, frame, loop, play, fps = key.fps] {
        double next = frame->value() + fps * (static_cast<double>(elapsed->restart()) / 1000.0);
        if (next >= frame->maximum())
        {
            if (loop->isChecked())
                next = std::fmod(next, frame->maximum());
            else
            {
                next = frame->maximum();
                play->setChecked(false);
            }
        }
        frame->setValue(next);
    });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(close, &QDialogButtonBox::rejected, dialog.get(), &QDialog::reject);
    layout->addWidget(close);
    lod->setCurrentIndex(lod->count() > 1 ? 1 : 0);
    refresh();
    view->reframe();
    dialog->resize(800, 510);
    return dialog.release();
}
} // namespace

QDialog *createModelPosePreview(QWidget *parent, const std::filesystem::path &modelPath, const assets::Model3do &hierarchy,
                                const std::filesystem::path &keyPath)
{
    const auto key = assets::loadFromKEY(keyPath);
    return createPosePreviewDialog(parent, std::make_unique<PoseView>(modelPath, hierarchy, key), key, identityName(keyPath));
}

QDialog *createModelPosePreview(QWidget *parent, assets::NativeModelDocument snapshot, assets::KeyFile key,
                                const std::filesystem::path &keyIdentity)
{
    return createPosePreviewDialog(parent, std::make_unique<PoseView>(std::move(snapshot), key), key, identityName(keyIdentity));
}
} // namespace sed::ui::resources
