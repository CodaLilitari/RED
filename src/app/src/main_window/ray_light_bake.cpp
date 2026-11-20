#include "main_window/common.hpp"
#include "app_version.hpp"
#include <QSysInfo>

#include "sed/level/ray_lighting_resources.hpp"
#include "ui/lighting/light_bake_utils.hpp"
#include "ui/lighting/ray_lighting_gpu.hpp"
#include "ui/lighting/ray_recipe.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QSpinBox>

#include <cmath>

namespace sed
{
namespace
{

struct RayBakeWorkerResult
{
    Level level{};
    level::RayLightingResult bake{};
    level::RayLightingContributions contributions{};
    level::RayLightingResources dependencies{};
    QString backend{};
    QString error{};
};

double settingDouble(const QSettings& settings, const char* key, const double fallback)
{
    bool valid{};
    const double value = settings.value(key, fallback).toDouble(&valid);
    return valid && std::isfinite(value) ? value : fallback;
}

} // namespace

void MainWindow::showRayLightingBakeDialog()
{
    if ( !documentEditingAllowed() )
    {
        return;
    }
    if ( auto* existing = findChild<QDialog*>(QStringLiteral("rayLightingBakeDialog")) )
    {
        existing->raise();
        return;
    }
    if ( lightBakeRunning_ || lightBakeTransformPreviewWorkerRunning_ || transformPreview_.active )
    {
        statusBar()->showMessage(tr("Finish or stop the current lighting bake or transform first."), 5000);
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("rayLightingBakeDialog"));
    dialog->setWindowTitle(tr("Ray-traced lighting"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Qt stylesheets do not propagate the dialog's foreground to its child labels.
    dialog->setStyleSheet(QStringLiteral("QLabel:enabled, QCheckBox:enabled { color: #eeeeee; }"));
    auto* layout = new QVBoxLayout(dialog);
    auto* introduction = new QLabel(tr(
        "Bake direct lights, sky from every visible direction, and diffuse reflections into world surface corners. "
        "The current Visible only setting limits which sectors are changed; the full scene supplies light and shadows. "
        "One Undo restores the previous bake."), dialog);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);
    auto* form = new QFormLayout();
    layout->addLayout(form);
    const QSettings settings = settingsStore();
    const auto saved = ui::lighting::decodeRayRecipe(property("red.acceptedRayRecipe").toByteArray());
    auto* backend = new QComboBox(dialog);
    backend->setObjectName(QStringLiteral("rayBake.backend"));
    backend->addItems({ tr("GPU compute, with CPU fallback"), tr("CPU reference") });
    backend->setCurrentIndex(settings.value("lighting/ray_prefer_gpu", true).toBool() ? 0 : 1);
    form->addRow(tr("Calculation"), backend);
    auto* samples = new QSpinBox(dialog);
    samples->setObjectName(QStringLiteral("rayBake.samples"));
    samples->setRange(8, static_cast<int>(level::kRayLightingMaxSamples));
    samples->setSingleStep(8);
    samples->setValue(settings.value("lighting/ray_samples", 32).toInt());
    samples->setToolTip(tr("Directions sampled at each corner. More samples reduce sampling noise and take longer."));
    form->addRow(tr("Rays per corner"), samples);
    auto* bounces = new QSpinBox(dialog);
    bounces->setObjectName(QStringLiteral("rayBake.bounces"));
    bounces->setRange(0, static_cast<int>(level::kRayLightingMaxBounces));
    bounces->setValue(settings.value("lighting/ray_bounces", 2).toInt());
    bounces->setToolTip(tr("Diffuse reflections after direct light and sky. Zero still gathers sky and emitted light."));
    form->addRow(tr("Diffuse bounces"), bounces);
    const auto number = [dialog](const QString& name, double minimum, double maximum, double value)
    {
        auto* result = new QDoubleSpinBox(dialog);
        result->setObjectName(name);
        result->setRange(minimum, maximum);
        result->setDecimals(3);
        result->setSingleStep(0.1);
        result->setValue(value);
        return result;
    };
    auto* reflectivity = number(QStringLiteral("rayBake.reflectivity"), 0, 1,
        settingDouble(settings, "lighting/ray_reflectivity", 0.65));
    reflectivity->setToolTip(tr("Diffuse fraction reflected by materials, multiplied by their representative color."));
    form->addRow(tr("Reflectivity"), reflectivity);
    auto* skyIntensity = number(QStringLiteral("rayBake.skyIntensity"), 0, 1000,
        settingDouble(settings, "lighting/ray_sky_intensity", 1));
    skyIntensity->setSpecialValueText(tr("Off"));
    form->addRow(tr("Sky intensity"), skyIntensity);
    auto* skyColor = new QPushButton(dialog);
    skyColor->setObjectName(QStringLiteral("rayBake.skyColor"));
    const QColor savedColor(settings.value("lighting/ray_sky_color", "#ffffff").toString());
    skyColor->setProperty("color", savedColor.isValid() ? savedColor : QColor(Qt::white));
    skyColor->setText(skyColor->property("color").value<QColor>().name());
    connect(skyColor, &QPushButton::clicked, dialog, [dialog, skyColor]()
    {
        const QColor selected = QColorDialog::getColor(skyColor->property("color").value<QColor>(), dialog,
            tr("Sky light color"));
        if ( selected.isValid() )
        {
            skyColor->setProperty("color", selected);
            skyColor->setText(selected.name());
        }
    });
    form->addRow(tr("Sky color"), skyColor);
    auto* angle = new QCheckBox(tr("Angle shading for direct lights"), dialog);
    angle->setObjectName(QStringLiteral("rayBake.angleShading"));
    angle->setChecked(settings.value("lighting/ray_angle_shading", false).toBool());
    form->addRow(angle);
    auto* water = new QCheckBox(tr("Water absorption for direct lights"), dialog);
    water->setObjectName(QStringLiteral("rayBake.waterAbsorption"));
    water->setChecked(settings.value("lighting/ray_water_absorption", false).toBool());
    form->addRow(water);
    quint64 sceneCorners = 0;
    for ( const auto& sector : document_.level().sectors )
        for ( const auto& face : sector.surfaces ) sceneCorners += face.vertices.size();
    auto* cost = new QLabel(dialog);
    cost->setObjectName(QStringLiteral("rayBake.cost"));
    const auto updateCost = [=]()
    {
        cost->setText(tr("%1 scene corners x %2 directions. Transport uses %3 gathering pass(es).")
            .arg(sceneCorners).arg(samples->value()).arg(bounces->value()+1));
    };
    connect(samples, &QSpinBox::valueChanged, dialog, updateCost);
    connect(bounces, &QSpinBox::valueChanged, dialog, updateCost);
    updateCost();
    layout->addWidget(cost);
    auto* limits = new QLabel(tr(
        "Sky uses surfaces explicitly marked as sky. Fully lit materials emit their representative color. "
        "Opaque static model architecture blocks sky and reflections; model vertex lighting and sector ambient stay as authored. "
        "Direct-light model shadows, transparent materials and animated architecture are outside this bake profile. "
        "Unsupported inputs are reported before any lighting is replaced."), dialog);
    limits->setWordWrap(true);
    layout->addWidget(limits);
    if ( saved )
    {
        backend->setCurrentIndex(saved->preferGpu ? 0 : 1);
        samples->setValue(int(saved->options.samples));
        bounces->setValue(int(saved->options.bounces));
        reflectivity->setValue(saved->options.reflectivity);
        skyIntensity->setValue(saved->options.skyIntensity);
        const auto c = saved->options.skyColor;
        const QColor recipeColor = QColor::fromRgbF(c.r, c.g, c.b);
        skyColor->setProperty("color", recipeColor);
        skyColor->setText(recipeColor.name());
        angle->setChecked(saved->options.direct.angleShading);
        water->setChecked(saved->options.direct.waterAbsorption);
        if ( !resourceLocatorSnapshot_ ) publishResourceLocatorSnapshot();
        const auto state = ui::lighting::checkRayRecipe(*saved, document_.level(), &templates_,
            level::makeRayLightingResourceReader(*resourceLocatorSnapshot_));
        auto* status = new QLabel(state.message, dialog);
        status->setToolTip(tr("Accepted build: %1").arg(saved->build));
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("rayBake.recipeStatus"));
        layout->addWidget(status);
        const auto& energy = saved->contributions;
        if ( energy.corners > 0 )
        {
            auto* measures = new QLabel(tr("Accepted bake, mean linear RGB per receiving corner\n"
                "Direct %1    Sky %2    Emission %3    Diffuse %4\n"
                "%5 corners across %6 surfaces. Longest interpolation edge: %7 JKU.")
                .arg(energy.direct / energy.corners, 0, 'g', 4).arg(energy.sky / energy.corners, 0, 'g', 4)
                .arg(energy.emission / energy.corners, 0, 'g', 4).arg(energy.diffuse / energy.corners, 0, 'g', 4)
                .arg(energy.corners).arg(energy.surfaces).arg(energy.longestEdge, 0, 'g', 4), dialog);
            measures->setObjectName(QStringLiteral("rayBake.contributions"));
            measures->setWordWrap(true);
            layout->addWidget(measures);
        }
        auto* replay = new QPushButton(tr("Replay accepted recipe"), dialog);
        replay->setObjectName(QStringLiteral("rayBake.replay"));
        replay->setEnabled(state.current);
        layout->addWidget(replay);
        const auto epoch = recoveryDocumentEpoch_;
        connect(replay, &QPushButton::clicked, dialog, [this, dialog, saved, epoch]()
        {
            dialog->reject();
            if ( epoch != recoveryDocumentEpoch_ || !resourceLocatorSnapshot_ ) return;
            const auto state = ui::lighting::checkRayRecipe(*saved, document_.level(), &templates_,
                level::makeRayLightingResourceReader(*resourceLocatorSnapshot_));
            if ( !state.current )
            {
                statusBar()->showMessage(state.message, 10000);
                return;
            }
            startRayLightingBake(saved->options, saved->preferGpu, saved->targets);
        });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->setObjectName(QStringLiteral("rayBake.buttons"));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Bake"));
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    connect(dialog, &QDialog::accepted, this,
        [this, backend, samples, bounces, reflectivity, skyIntensity, skyColor, angle, water]()
    {
        level::RayLightingOptions options{};
        options.samples = static_cast<std::uint32_t>(samples->value());
        options.bounces = static_cast<std::uint32_t>(bounces->value());
        options.reflectivity = reflectivity->value();
        options.skyIntensity = skyIntensity->value();
        const QColor color = skyColor->property("color").value<QColor>();
        options.skyColor = { color.redF(), color.greenF(), color.blueF(), 1 };
        options.direct.angleShading = angle->isChecked();
        options.direct.waterAbsorption = water->isChecked();
        const bool preferGpu = backend->currentIndex() == 0;
        QSettings settings = settingsStore();
        settings.setValue("lighting/ray_prefer_gpu", preferGpu);
        settings.setValue("lighting/ray_samples", options.samples);
        settings.setValue("lighting/ray_bounces", options.bounces);
        settings.setValue("lighting/ray_reflectivity", options.reflectivity);
        settings.setValue("lighting/ray_sky_intensity", options.skyIntensity);
        settings.setValue("lighting/ray_sky_color", color.name());
        settings.setValue("lighting/ray_angle_shading", options.direct.angleShading);
        settings.setValue("lighting/ray_water_absorption", options.direct.waterAbsorption);
        settings.sync();
        startRayLightingBake(options, preferGpu);
    });
    dialog->resize(520, dialog->sizeHint().height());
    dialog->open();
}

void MainWindow::startRayLightingBake(const level::RayLightingOptions& options, const bool preferGpu,
    std::optional<std::vector<std::int32_t>> replayTargets)
{
    if ( !documentEditingAllowed() || lightBakeRunning_ || lightBakeTransformPreviewWorkerRunning_
        || transformPreview_.active )
    {
        return;
    }
    const bool visibleOnly = lightBakeVisibleOnlyButton_ && lightBakeVisibleOnlyButton_->isChecked();
    const auto targets = replayTargets ? *replayTargets : collectLightBakeSectorTargets(visibleOnly);
    const auto targetsAllowed = [this, targets, visibleOnly, replay = replayTargets.has_value()]()
    {
        const auto allowed = collectLightBakeSectorTargets(replay ? false : visibleOnly);
        return replay ? std::ranges::includes(allowed, targets) : allowed == targets;
    };
    if ( !targetsAllowed() )
    {
        statusBar()->showMessage(tr("One or more recipe sectors are missing or locked."), 6000);
        return;
    }
    if ( targets.empty() )
    {
        statusBar()->showMessage(tr("No visible, unlocked sectors are available for ray lighting."), 5000);
        return;
    }
    cancelLightBake(true);
    pollLightBakeSourceChanges();
    cancelLightBake(true);
    if ( !resourceLocatorSnapshot_ )
    {
        publishResourceLocatorSnapshot();
    }
    const auto resources = resourceLocatorSnapshot_;
    const assets::Templates templates = templates_;
    const auto geometryFingerprint = ui::lighting::lightBakeSourceFingerprint(document_.level(), targets);
    const auto resourceInputs = level::rayLightingResourceInputsFingerprint(document_.level(), &templates_);
    const auto undoSignature = document_.undoSignature();
    const auto documentEpoch = recoveryDocumentEpoch_;
    const auto generation = lightBakeSourceGeneration_;
    const auto gpuInput = preferGpu ? ui::lighting::createRayLightingGpuInput() : nullptr;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    lightBakeCancelFlag_ = cancel;
    lightBakeRunning_ = true;
    lightBakeActiveTargets_ = targets;
    const auto serial = ++lightBakeJobSerial_;
    updateLightBakeProgress(0);
    if ( lightBakeStatusButton_ )
    {
        lightBakeStatusButton_->show();
    }
    if ( lightBakeStopButton_ )
    {
        lightBakeStopButton_->setEnabled(true);
        lightBakeStopButton_->setText(tr("Stop"));
    }
    if ( lightBakeProgressLabel_ )
    {
        lightBakeProgressLabel_->setText(tr("Resolving the full scene for ray lighting…"));
    }
    using Result = std::shared_ptr<RayBakeWorkerResult>;
    auto* watcher = new QFutureWatcher<Result>(this);
    watcher->setObjectName(QStringLiteral("rayLightingFutureWatcher"));
    lightBakeWatcher_ = watcher;
    lightBakeWaitForFinished_ = [watcher]() { watcher->waitForFinished(); };
    const QPointer<MainWindow> guarded(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
        [this, watcher, targets, targetsAllowed, options, preferGpu, resources, serial, generation, geometryFingerprint, resourceInputs,
         undoSignature, documentEpoch, cancel]()
    {
        Result result{};
        try
        {
            result = watcher->result();
        }
        catch ( ... )
        {
            result = std::make_shared<RayBakeWorkerResult>();
            result->error = tr("Ray lighting worker failed; no result was applied.");
        }
        watcher->deleteLater();
        if ( serial != lightBakeJobSerial_ )
        {
            return;
        }
        lightBakeWatcher_ = nullptr;
        lightBakeWaitForFinished_ = {};
        const bool stopped = cancel->load(std::memory_order_relaxed) || result->bake.cancelled;
        bool current = !stopped && result->bake.succeeded && documentEditingAllowed()
            && !transformPreview_.active && generation == lightBakeSourceGeneration_
            && documentEpoch == recoveryDocumentEpoch_ && undoSignature == document_.undoSignature()
            && resources == resourceLocatorSnapshot_
            && targetsAllowed()
            && geometryFingerprint == ui::lighting::lightBakeSourceFingerprint(document_.level(), targets)
            && resourceInputs == level::rayLightingResourceInputsFingerprint(document_.level(), &templates_);
        if ( current )
        {
            // A loose file can change after the worker posts its result and
            // before the GUI handles completion. Verify the captured winners
            // again at this manual transaction's publication boundary.
            try
            {
                current = level::rayLightingResourcesCurrent(result->dependencies,
                    level::makeRayLightingResourceReader(*resources));
                if ( !current )
                {
                    result->error = tr("Resource files changed before publication; no lighting was replaced.");
                }
            }
            catch ( const std::exception& error )
            {
                current = false;
                result->error = QString::fromUtf8(error.what());
            }
        }
        finishLightBake(std::move(result->level), targets, generation, geometryFingerprint, !current, false, false,
            "Baked Lighting: Ray-traced sky and diffuse reflections", 0, false, true, {}, {}, {});
        if ( stopped )
        {
            return;
        }
        if ( !result->error.isEmpty() )
        {
            statusBar()->showMessage(result->error, 15000);
        }
        else if ( !current )
        {
            statusBar()->showMessage(tr("The scene or resources changed; the ray lighting result was discarded."), 10000);
        }
        else if ( document_.undoSignature() != undoSignature )
        {
            QString build = QStringLiteral("%1 / Qt %2 / %3")
                .arg(QString::fromLatin1(app::kVersion), QString::fromLatin1(qVersion()), QSysInfo::buildAbi());
#ifdef RED_CONFIGURED_REVISION
            build += QStringLiteral(" / ") + QString::fromLatin1(RED_CONFIGURED_REVISION);
#endif
            ui::lighting::RayRecipe recipe{options, targets, ui::lighting::resourceDigests(result->dependencies),
                geometryFingerprint, resourceInputs, ui::lighting::cornerDigest(document_.level(), targets),
                result->backend, build, preferGpu, result->contributions};
            setProperty("red.acceptedRayRecipe", ui::lighting::encodeRayRecipe(recipe));
            scheduleRecoverySnapshot();
            statusBar()->showMessage(tr("Ray lighting baked using %1 (%2 ray queries).")
                .arg(result->backend).arg(static_cast<qulonglong>(result->bake.rays)), 15000);
        }
    });
    watcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(),
        [snapshot = document_.level(), templates, resources, targets, options, preferGpu, gpuInput, cancel, guarded, serial]() mutable
        -> Result
    {
        auto result = std::make_shared<RayBakeWorkerResult>();
        result->level = std::move(snapshot);
        try
        {
            const auto cancelled = [cancel]() { return cancel->load(std::memory_order_relaxed); };
            auto resolved = level::resolveRayLightingResources(result->level,
                level::makeRayLightingResourceReader(*resources), cancelled, &templates);
            std::unique_ptr<ui::lighting::RayLightingGpu> gpu{};
            level::RayLightIntersector intersector{};
            result->backend = QStringLiteral("CPU reference");
            if ( preferGpu )
            {
                gpu = std::make_unique<ui::lighting::RayLightingGpu>(gpuInput.get());
                if ( gpu->available() )
                {
                    result->backend = gpu->description();
                    intersector = [&gpu](auto triangles, auto nodes, auto rays, auto hits)
                    {
                        return gpu->intersect(triangles, nodes, rays, hits);
                    };
                }
                else
                {
                    result->backend += QStringLiteral(" (GPU unavailable)");
                }
            }
            int lastPercent = -1;
            const auto progress = [cancel, guarded, serial, &lastPercent](std::size_t done, std::size_t total)
            {
                if ( cancel->load(std::memory_order_relaxed) )
                {
                    return false;
                }
                const int percent = ui::lighting::lightBakeProgressPercent(done, total);
                if ( percent != lastPercent )
                {
                    lastPercent = percent;
                    if ( auto* app = QCoreApplication::instance() )
                    {
                        QMetaObject::invokeMethod(app, [guarded, serial, percent]()
                        {
                            if ( guarded && guarded->lightBakeJobSerial_ == serial )
                            {
                                guarded->updateLightBakeProgress(percent);
                            }
                        }, Qt::QueuedConnection);
                    }
                }
                return !cancel->load(std::memory_order_relaxed);
            };
            result->bake = level::bakeRayLightingWithReport(result->level, targets, options, resolved.materials,
                resolved.blockers, intersector, progress, result->contributions);
            if ( !result->bake.succeeded )
            {
                result->error = QString::fromStdString(result->bake.message);
                if ( gpu && !gpu->available() && intersector )
                {
                    result->error += QStringLiteral(" ") + gpu->description();
                }
            }
            else if ( !level::rayLightingResourcesCurrent(resolved,
                level::makeRayLightingResourceReader(*resources), cancelled) )
            {
                result->bake.succeeded = false;
                result->error = tr("Resource files changed while baking; no lighting was replaced.");
            }
            result->dependencies.dependencies = std::move(resolved.dependencies);
        }
        catch ( const std::exception& error )
        {
            result->bake.succeeded = false;
            result->error = QString::fromUtf8(error.what());
        }
        catch ( ... )
        {
            result->bake.succeeded = false;
            result->error = tr("Ray lighting failed; no lighting was replaced.");
        }
        return result;
    }));
}

} // namespace sed
