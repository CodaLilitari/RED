#include "ui/cog/cog_preflight_dialog.hpp"

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_preflight.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
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
    setObjectName(QStringLiteral("cogCheckDialog"));
    setWindowTitle(tr("COG check — %1").arg(scriptName_));
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
    targetCombo_->setObjectName(QStringLiteral("cogCheck.target"));
    root->addWidget(targetCombo_);

    listingCheck_ = new QCheckBox(tr("Show compiled code"), this);
    listingCheck_->setObjectName(QStringLiteral("cogCheck.listing"));
    root->addWidget(listingCheck_);

    findingsView_ = new QPlainTextEdit(this);
    findingsView_->setObjectName(QStringLiteral("cogCheck.findings"));
    findingsView_->setReadOnly(true);
    findingsView_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    findingsView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(findingsView_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(targetCombo_, &QComboBox::currentIndexChanged,
        this, [this]() { refreshFindings(); });
    connect(listingCheck_, &QCheckBox::toggled, this, [this]() { refreshFindings(); });
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
    QString body{};
    const assets::CogCompileResult compiled = assets::compileCog(scriptName_.toStdString(), text, target);
    for (const std::string& line : formatCogCompileLines(compiled, listingCheck_ && listingCheck_->isChecked()))
    {
        body += QString::fromStdString(line);
        body += QLatin1Char('\n');
    }
    body += QLatin1Char('\n');
    const assets::CogPreflightResult result = assets::runCogPreflight(
        scriptName_.toStdString(), text, target, {});
    for (const std::string& line : formatCogPreflightLines(result))
    {
        body += QString::fromStdString(line);
        body += QLatin1Char('\n');
    }
    findingsView_->setPlainText(body);
}

} // namespace sed::ui::cog
