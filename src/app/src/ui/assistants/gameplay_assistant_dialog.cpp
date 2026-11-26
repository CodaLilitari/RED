#include "ui/assistants/gameplay_assistant_dialog.hpp"

#include "sed/level/recipe_requests.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace sed::ui::assistants
{
namespace
{

QString thingText(const Level& level, const std::int32_t index)
{
    const Thing& thing = level.things[static_cast<std::size_t>(index)];
    return QStringLiteral("Thing %1 (%2)").arg(index).arg(QString::fromStdString(thing.name.empty() ? thing.templateName : thing.name));
}

QString surfaceText(const SurfaceRef ref)
{
    return QStringLiteral("sector %1 surface %2").arg(ref.sector).arg(ref.surface);
}

QString titleFor(const level::AssistantKind kind)
{
    switch (kind)
    {
    case level::AssistantKind::door: return QStringLiteral("Door Assistant");
    case level::AssistantKind::elevator: return QStringLiteral("Elevator Assistant");
    case level::AssistantKind::switchDoor: return QStringLiteral("Switch Assistant");
    case level::AssistantKind::objectives: return QStringLiteral("Objectives Assistant");
    case level::AssistantKind::cutscene: return QStringLiteral("Cutscene Assistant");
    case level::AssistantKind::pursuit: return QStringLiteral("Pursuit Assistant");
    }
    return QString();
}

} // namespace

GameplayAssistantDialog::GameplayAssistantDialog(const level::AssistantKind kind, const Level& level,
    const editor::Selection& selection, const assets::Templates* templates, std::vector<CogValue> scriptSymbols,
    std::string scriptText, QWidget* parent)
    : QDialog(parent), kind_(kind), level_(level), templates_(templates), symbols_(std::move(scriptSymbols)),
      scriptText_(std::move(scriptText))
{
    setObjectName(QStringLiteral("gameplayAssistant.%1").arg(QString::fromLatin1(level::assistantKindName(kind).data())));
    setWindowTitle(titleFor(kind));
    things_.assign(selection.things.begin(), selection.things.end());
    std::sort(things_.begin(), things_.end());
    std::erase_if(things_, [&](const std::int32_t i) { return i < 0 || static_cast<std::size_t>(i) >= level.things.size(); });
    std::vector<SurfaceRef> surfaces(selection.surfaces.begin(), selection.surfaces.end());
    std::sort(surfaces.begin(), surfaces.end(), [](const SurfaceRef a, const SurfaceRef b) {
        return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
    });
    for (const SurfaceRef ref : surfaces)
    {
        if (ref.sector < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size()) continue;
        const auto& list = level.sectors[static_cast<std::size_t>(ref.sector)].surfaces;
        if (ref.surface < 0 || static_cast<std::size_t>(ref.surface) >= list.size()) continue;
        (list[static_cast<std::size_t>(ref.surface)].adjoin ? adjoins_ : buttons_).push_back(ref);
    }
    sectors_.assign(selection.sectors.begin(), selection.sectors.end());
    std::sort(sectors_.begin(), sectors_.end());

    auto* const layout = new QVBoxLayout(this);
    auto* const targets = new QLabel(this);
    targets->setObjectName(QStringLiteral("targets"));
    targets->setWordWrap(true);
    auto* const form = new QFormLayout();
    layout->addWidget(targets);
    layout->addLayout(form);
    QStringList lines;
    const auto addOffset = [&](const QString& label, const double up) {
        auto* const row = new QHBoxLayout();
        const char* axes[] = { "x", "y", "z" };
        for (int i = 0; i < 3; ++i)
        {
            offset_[static_cast<std::size_t>(i)] = spin(QStringLiteral("offset.%1").arg(QLatin1String(axes[i])), i == 2 ? up : 0.0, -200.0, 200.0);
            row->addWidget(offset_[static_cast<std::size_t>(i)]);
        }
        form->addRow(label, row);
    };
    const double doorHeight = things_.empty() ? 1.0 : level::makeDoorHeight(level, templates, static_cast<std::size_t>(things_.front()));
    const double up = std::isfinite(doorHeight) && doorHeight > 0.001 ? doorHeight : 1.0;

    switch (kind)
    {
    case level::AssistantKind::door:
    case level::AssistantKind::switchDoor:
    {
        if (things_.size() > level::kMaxAssistantDoorPieces) things_.resize(level::kMaxAssistantDoorPieces);
        QStringList pieces;
        for (const std::int32_t i : things_) pieces << thingText(level, i);
        lines << QStringLiteral("Pieces (the selected Things, up to four): %1").arg(pieces.isEmpty() ? QStringLiteral("none") : pieces.join(", "));
        if (kind == level::AssistantKind::switchDoor)
        {
            QStringList switches;
            for (std::size_t i = 0; i < buttons_.size() && i < level::kMaxAssistantSwitches; ++i) switches << surfaceText(buttons_[i]);
            lines << QStringLiteral("Switches (selected surfaces that aren't adjoins, up to two): %1")
                         .arg(switches.isEmpty() ? QStringLiteral("none") : switches.join(", "));
        }
        addOffset(QStringLiteral("Opens by (JKU):"), up);
        if ( kind == level::AssistantKind::door )
        {
            opposingLeaves_ = new QCheckBox(tr("Open two leaves in opposite directions"), this);
            opposingLeaves_->setObjectName("opposingLeaves");
            opposingLeaves_->setEnabled(things_.size() == 2);
            opposingLeaves_->setToolTip(tr("Select exactly two pieces. The first listed Thing uses the opening offset; the second uses its negative."));
            form->addRow(opposingLeaves_);
            if ( things_.size() == 2 )
            {
                const Thing& first = level.things[static_cast<std::size_t>(things_[0])];
                const Thing& second = level.things[static_cast<std::size_t>(things_[1])];
                std::array<double, 3> otherOffset{ first.x - second.x, first.y - second.y, first.z - second.z };
                const double spread = std::hypot(otherOffset[0], otherOffset[1], otherOffset[2]);
                if ( !std::isfinite(spread) || spread < 0.001 || spread > 200.0 )
                    otherOffset = { -1.0, 0.0, 0.0 };

                auto* poses = new QLabel(this);
                poses->setObjectName("doorOpenPoses");
                poses->setTextFormat(Qt::PlainText);
                poses->setWordWrap(true);
                poses->hide();
                form->addRow(poses);
                const auto refresh = [this, poses]() {
                    if ( !opposingLeaves_->isChecked() ) return;
                    QStringList rows;
                    for ( std::size_t i = 0; i < things_.size(); ++i )
                    {
                        const auto index = things_[i];
                        const auto& leaf = level_.things[static_cast<std::size_t>(index)];
                        const auto pose = level::assistantMoverOpenPose(leaf, offset(), i == 1);
                        rows << tr("%1 opens at %2").arg(thingText(level_, index), QString::fromStdString(format::frameValue(pose)));
                    }
                    poses->setText(rows.join('\n'));
                };
                for ( auto* box : offset_ )
                    connect(box, &QDoubleSpinBox::valueChanged, this, refresh);
                connect(opposingLeaves_, &QCheckBox::toggled, this, [this, poses, refresh, otherOffset](bool checked) mutable {
                    const auto previous = offset();
                    for ( std::size_t i = 0; i < offset_.size(); ++i ) offset_[i]->setValue(otherOffset[i]);
                    otherOffset = previous;
                    refresh();
                    poses->setVisible(checked);
                });
            }
        }
        speed_ = spin(QStringLiteral("speed"), 8.0, 0.01, 100.0);
        wait_ = spin(QStringLiteral("wait"), 2.0, 0.0, 600.0);
        light_ = spin(QStringLiteral("light"), 0.5, 0.0, 2.0);
        form->addRow(QStringLiteral("Speed (JKU/s):"), speed_);
        form->addRow(QStringLiteral("Stays open (s):"), wait_);
        form->addRow(QStringLiteral("Door sector light:"), light_);
        break;
    }
    case level::AssistantKind::elevator:
    {
        lines << QStringLiteral("Lift: %1").arg(things_.empty() ? QStringLiteral("none") : thingText(level, things_.front()));
        lines << QStringLiteral("Button: %1").arg(buttons_.empty() ? QStringLiteral("none") : surfaceText(buttons_.front()));
        QStringList calls;
        for (std::size_t i = 0; i < adjoins_.size() && i < level::kMaxAssistantCallAdjoins; ++i) calls << surfaceText(adjoins_[i]);
        lines << QStringLiteral("Call adjoins (crossing one calls the lift, up to three): %1")
                     .arg(calls.isEmpty() ? QStringLiteral("none") : calls.join(", "));
        addOffset(QStringLiteral("Rises by (JKU):"), up);
        speed_ = spin(QStringLiteral("speed"), 4.0, 0.01, 100.0);
        wait_ = spin(QStringLiteral("wait"), 2.0, 0.0, 600.0);
        startWait_ = spin(QStringLiteral("startWait"), 0.25, 0.0, 60.0);
        form->addRow(QStringLiteral("Speed (JKU/s):"), speed_);
        form->addRow(QStringLiteral("Waits at the top (s):"), wait_);
        form->addRow(QStringLiteral("Waits before rising (s):"), startWait_);
        break;
    }
    case level::AssistantKind::objectives:
    {
        lines << QStringLiteral("Goals: reach each selected sector, then use or kill each selected Thing (up to eight).");
        lines << QStringLiteral("Exit: %1").arg(adjoins_.empty() ? QStringLiteral("none (the last goal ends the level)") : surfaceText(adjoins_.front()));
        goalBase_ = new QSpinBox(this);
        goalBase_->setObjectName(QStringLiteral("goalBase"));
        goalBase_->setRange(0, 99000);
        goalBase_->setSingleStep(1000);
        goalBase_->setValue(1000);
        form->addRow(QStringLiteral("Goal text starts at GOAL_:"), goalBase_);
        std::size_t goals = 0;
        for (const std::int32_t sector : sectors_)
        {
            if (++goals > level::kMaxAssistantGoals) break;
            form->addRow(QStringLiteral("Goal %1:").arg(goals - 1), new QLabel(QStringLiteral("reach sector %1").arg(sector), this));
        }
        for (const std::int32_t thing : things_)
        {
            if (++goals > level::kMaxAssistantGoals) break;
            auto* const trigger = new QComboBox(this);
            trigger->setObjectName(QStringLiteral("goal%1").arg(goals - 1));
            trigger->addItem(QStringLiteral("use %1").arg(thingText(level, thing)), QStringLiteral("use"));
            trigger->addItem(QStringLiteral("kill %1").arg(thingText(level, thing)), QStringLiteral("kill"));
            goalTriggers_.push_back(trigger);
            form->addRow(QStringLiteral("Goal %1:").arg(goals - 1), trigger);
        }
        endWhenDone_ = new QCheckBox(QStringLiteral("End the level with the last goal (when there's no exit)"), this);
        endWhenDone_->setObjectName(QStringLiteral("endWhenDone"));
        endWhenDone_->setChecked(true);
        form->addRow(endWhenDone_);
        break;
    }
    case level::AssistantKind::cutscene:
    {
        lines << QStringLiteral("Camera (a Thing with path frames): %1").arg(things_.empty() ? QStringLiteral("none") : thingText(level, things_.front()));
        lines << QStringLiteral("Plays when the player enters: %1")
                     .arg(sectors_.empty() ? QStringLiteral("no sector selected, so it plays at startup") : QStringLiteral("sector %1").arg(sectors_.front()));
        speed_ = spin(QStringLiteral("speed"), 2.0, 0.01, 100.0);
        wait_ = spin(QStringLiteral("hold"), 2.0, 0.0, 600.0);
        form->addRow(QStringLiteral("Camera speed (JKU/s):"), speed_);
        form->addRow(QStringLiteral("Holds at the end (s):"), wait_);
        once_ = new QCheckBox(QStringLiteral("Play once"), this);
        once_->setObjectName(QStringLiteral("once"));
        once_->setChecked(true);
        form->addRow(once_);
        break;
    }
    case level::AssistantKind::pursuit:
        break;
    }
    if (kind == level::AssistantKind::pursuit)
    {
        // The enemy is the selected kcswoop Thing (or the lowest numbered); the rest are its route, in number order.
        const auto enemy = std::find_if(things_.begin(), things_.end(), [&](const std::int32_t i) {
            return QString::fromStdString(level.things[static_cast<std::size_t>(i)].templateName).compare(QStringLiteral("kcswoop"), Qt::CaseInsensitive) == 0;
        });
        if (enemy != things_.end()) std::rotate(things_.begin(), enemy, enemy + 1);
        QStringList route;
        for (std::size_t i = 1; i < things_.size() && i <= level::kMaxAssistantRoute; ++i) route << thingText(level, things_[i]);
        lines << QStringLiteral("Enemy: %1").arg(things_.empty() ? QStringLiteral("none") : thingText(level, things_.front()));
        lines << QStringLiteral("Route markers, in number order (up to four): %1").arg(route.isEmpty() ? QStringLiteral("none, direct pursuit") : route.join(", "));
        lines << QStringLiteral("Wake sector: %1").arg(sectors_.empty() ? QStringLiteral("none") : QStringLiteral("sector %1").arg(sectors_.front()));
        wait_ = spin(QStringLiteral("wakeDistance"), 5.5, 0.1, 100.0);
        speed_ = spin(QStringLiteral("moveSpeed"), 1.5, 0.01, 20.0);
        ramDamage_ = spin(QStringLiteral("ramDamage"), 18.0, 0.0, 1000.0);
        fireInterval_ = spin(QStringLiteral("fireInterval"), 0.65, 0.05, 60.0);
        survival_ = spin(QStringLiteral("survivalChance"), 0.5, 0.0, 1.0);
        form->addRow(QStringLiteral("Wakes within (JKU):"), wait_);
        form->addRow(QStringLiteral("Speed:"), speed_);
        form->addRow(QStringLiteral("Ram damage:"), ramDamage_);
        form->addRow(QStringLiteral("Fires every (s):"), fireInterval_);
        form->addRow(QStringLiteral("Rider survives (chance):"), survival_);
    }
    lines << QStringLiteral("Script: %1").arg(QString::fromLatin1(level::assistantScriptName(kind).data()));
    targets->setText(lines.join(QStringLiteral("\n")));

    result_ = new QPlainTextEdit(this);
    result_->setObjectName(QStringLiteral("result"));
    result_->setReadOnly(true);
    result_->setPlaceholderText(QStringLiteral("Try it runs the result in the COG runtime and shows what happens."));
    result_->setMinimumHeight(180);
    layout->addWidget(result_);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* const tryButton = buttons->addButton(QStringLiteral("Try it"), QDialogButtonBox::ActionRole);
    tryButton->setObjectName(QStringLiteral("tryIt"));
    auto* debug = buttons->addButton(QStringLiteral("Inspect message trace…"), QDialogButtonBox::ActionRole);
    debug->setObjectName("inspectCogMessageTrace");
    connect(debug, &QPushButton::clicked, this, [this]{
        auto checked = tryIt();
        if (checked.breakpoints.empty()) return;
        auto* review = createCogTraceReview(this, std::move(checked.breakpoints), scriptText_, checked.unsimulated, traceHighlight_);
        review->setAttribute(Qt::WA_DeleteOnClose);
        review->open();
    });
    connect(tryButton, &QPushButton::clicked, this, [this]() { (void)tryIt(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, 520);
}

QDoubleSpinBox* GameplayAssistantDialog::spin(const QString& objectName, const double value, const double low, const double high,
    const int decimals)
{
    auto* const box = new QDoubleSpinBox(this);
    box->setObjectName(objectName);
    box->setDecimals(decimals);
    box->setRange(low, high);
    box->setValue(value);
    return box;
}

std::array<double, 3> GameplayAssistantDialog::offset() const
{
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) out[i] = offset_[i] ? offset_[i]->value() : 0.0;
    return out;
}

QString GameplayAssistantDialog::selectionProblem() const
{
    switch (kind_)
    {
    case level::AssistantKind::door:
        return things_.empty() ? QStringLiteral("Select the door's pieces: one to four Things.") : QString();
    case level::AssistantKind::switchDoor:
        if (things_.empty()) return QStringLiteral("Select the door's pieces (Things) and its switch (a surface).");
        return buttons_.empty() ? QStringLiteral("Select the switch too: a surface that isn't an adjoin.") : QString();
    case level::AssistantKind::elevator:
        if (things_.empty()) return QStringLiteral("Select the lift (a Thing) and its button or call adjoins (surfaces).");
        return buttons_.empty() && adjoins_.empty() ? QStringLiteral("Select the lift's button or the adjoins that call it.") : QString();
    case level::AssistantKind::objectives:
        return sectors_.empty() && things_.empty() ? QStringLiteral("Select the goals: sectors to reach, Things to use or kill.") : QString();
    case level::AssistantKind::cutscene:
        return things_.empty() ? QStringLiteral("Select the camera: a Thing with path frames.") : QString();
    case level::AssistantKind::pursuit:
        return things_.empty() ? QStringLiteral("Select the enemy (a kcswoop Thing) and, optionally, its route markers and a wake sector.") : QString();
    }
    return QString();
}

level::AssistantResult GameplayAssistantDialog::applyTo(Level& level) const
{
    switch (kind_)
    {
    case level::AssistantKind::door:
    {
        level::DoorAssistantRequest r;
        r.pieces = things_;
        r.openOffset = offset();
        r.opposingLeaves = opposingLeaves_->isChecked();
        r.speed = speed_->value();
        r.wait = wait_->value();
        r.light = light_->value();
        return level::levelDoorAssistant(level, templates_, r, symbols_);
    }
    case level::AssistantKind::switchDoor:
    {
        level::SwitchAssistantRequest r;
        r.pieces = things_;
        r.openOffset = offset();
        r.switches.assign(buttons_.begin(), buttons_.begin() + static_cast<std::ptrdiff_t>(std::min(buttons_.size(), level::kMaxAssistantSwitches)));
        r.speed = speed_->value();
        r.wait = wait_->value();
        r.light = light_->value();
        return level::levelSwitchAssistant(level, templates_, r, symbols_);
    }
    case level::AssistantKind::elevator:
    {
        level::ElevatorAssistantRequest r;
        r.elevator = things_.empty() ? -1 : things_.front();
        r.travel = offset();
        if (!buttons_.empty()) r.button = buttons_.front();
        r.callAdjoins.assign(adjoins_.begin(), adjoins_.begin() + static_cast<std::ptrdiff_t>(std::min(adjoins_.size(), level::kMaxAssistantCallAdjoins)));
        r.speed = speed_->value();
        r.wait = wait_->value();
        r.startWait = startWait_->value();
        return level::levelElevatorAssistant(level, templates_, r, symbols_);
    }
    case level::AssistantKind::objectives:
    {
        level::ObjectivesAssistantRequest r;
        r.goalBase = goalBase_->value();
        for (const std::int32_t sector : sectors_) r.goals.push_back({ level::GoalTrigger::reach, sector });
        for (std::size_t i = 0; i < things_.size(); ++i)
        {
            const bool kill = i < goalTriggers_.size() && goalTriggers_[i]->currentData().toString() == QStringLiteral("kill");
            r.goals.push_back({ kill ? level::GoalTrigger::kill : level::GoalTrigger::use, things_[i] });
        }
        if (!adjoins_.empty()) r.exit = adjoins_.front();
        r.endWhenDone = endWhenDone_->isChecked();
        return level::levelObjectivesAssistant(level, r);
    }
    case level::AssistantKind::cutscene:
    {
        level::CutsceneAssistantRequest r;
        r.camera = things_.empty() ? -1 : things_.front();
        if (!sectors_.empty()) r.trigger = sectors_.front();
        r.speed = speed_->value();
        r.hold = wait_->value();
        r.once = once_->isChecked();
        return level::levelCutsceneAssistant(level, templates_, r);
    }
    case level::AssistantKind::pursuit:
    {
        level::PursuitAssistantRequest r;
        r.enemy = things_.empty() ? -1 : things_.front();
        for (std::size_t i = 1; i < things_.size() && i <= level::kMaxAssistantRoute; ++i) r.route.push_back(things_[i]);
        if (!sectors_.empty()) r.wakeSector = sectors_.front();
        r.wakeDistance = wait_->value();
        r.moveSpeed = speed_->value();
        r.ramDamage = ramDamage_->value();
        r.fireInterval = fireInterval_->value();
        r.survivalChance = survival_->value();
        return level::levelPursuitAssistant(level, r, symbols_);
    }
    }
    return {};
}

level::AssistantCheck GameplayAssistantDialog::tryIt()
{
    Level copy = level_;
    const level::AssistantResult result = applyTo(copy);
    QStringList text;
    level::AssistantCheck check;
    if (!result.ok())
    {
        text << QStringLiteral("Can't build it: %1").arg(QString::fromStdString(result.failure->message));
    }
    else
    {
        for (const std::string& warning : result.warnings) text << QStringLiteral("Note: %1").arg(QString::fromStdString(warning));
        if (scriptText_.empty())
        {
            text << QStringLiteral("The script isn't in the project resources, so it can't be run here.");
        }
        else
        {
            const std::size_t index = std::find_if(copy.cogs.begin(), copy.cogs.end(),
                                          [&](const Cog& cog) { return cog.num == result.cog; })
                - copy.cogs.begin();
            check = level::checkAssistant(copy, kind_, static_cast<std::int32_t>(index), scriptText_,
                copy.kind == ProjectType::MOTS ? assets::CogPreflightTarget::mots : assets::CogPreflightTarget::jk);
            text << (!check.unsimulated.empty() ? QStringLiteral("This scenario requires a target check for the operations below.")
                : check.passed ? QStringLiteral("It works in the COG runtime for the supported checks.") : QStringLiteral("A simulator expectation failed."));
            for (const std::string& line : check.expectations) text << QStringLiteral("  ") + QString::fromStdString(line);
            text << QString() << QStringLiteral("Timeline:");
            for (const std::string& line : check.timeline) text << QStringLiteral("  ") + QString::fromStdString(line);
            text << QString() << QStringLiteral("The runtime is a stand-in: test the level in the game, in both renderers.");
        }
    }
    result_->setPlainText(text.join(QStringLiteral("\n")));
    return check;
}

QString GameplayAssistantDialog::resultText() const
{
    return result_->toPlainText();
}

} // namespace sed::ui::assistants
