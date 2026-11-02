#include "ui/cog/cog_preflight_dialog.hpp"

#include "sed/assets/cog_preflight.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace sed::ui::cog
{

CogPreflightDialog::CogPreflightDialog(
    const QString& scriptName,
    const QString& scriptText,
    const ProjectType project,
    QWidget* parent)
    : QDialog(parent)
    , scriptName_(scriptName)
    , scriptText_(scriptText)
{
    setWindowTitle(tr("COG preflight — %1").arg(scriptName_));
    resize(720, 520);

    auto* root = new QVBoxLayout(this);
    targetCombo_ = new QComboBox(this);
    targetCombo_->addItem(
        QStringLiteral("JK 1.01 (measured)"),
        static_cast<int>(assets::CogPreflightTarget::jk));
    targetCombo_->addItem(
        QStringLiteral("MotS (measured)"),
        static_cast<int>(assets::CogPreflightTarget::mots));
    targetCombo_->addItem(
        QStringLiteral("IJIM (target-unmeasured)"),
        static_cast<int>(assets::CogPreflightTarget::ijim));
    const int initial = targetCombo_->findData(
        static_cast<int>(cogPreflightTargetForProject(project)));
    targetCombo_->setCurrentIndex(initial >= 0 ? initial : 0);
    root->addWidget(targetCombo_);

    findingsView_ = new QPlainTextEdit(this);
    findingsView_->setReadOnly(true);
    root->addWidget(findingsView_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(targetCombo_, &QComboBox::currentIndexChanged,
        this, [this]() { refreshFindings(); });
    refreshFindings();
}

void CogPreflightDialog::refreshFindings()
{
    if (!targetCombo_ || !findingsView_)
    {
        return;
    }
    const auto target = static_cast<assets::CogPreflightTarget>(
        targetCombo_->currentData().toInt());
    const std::string text = scriptText_.toStdString();
    const assets::CogPreflightResult result = assets::runCogPreflight(
        scriptName_.toStdString(), text, target, {});
    QString body{};
    for (const std::string& line : formatCogPreflightLines(result))
    {
        body += QString::fromStdString(line);
        body += QLatin1Char('\n');
    }
    findingsView_->setPlainText(body);
}

} // namespace sed::ui::cog
