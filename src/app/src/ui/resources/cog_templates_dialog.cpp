#include "ui/resources/cog_templates_dialog.hpp"

#include "sed/assets/cog_template_library.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace sed::ui::resources
{

CogTemplatesDialog::CogTemplatesDialog(std::optional<std::filesystem::path> library, std::filesystem::path projectDirectory, QWidget* parent)
    : QDialog(parent), library_(std::move(library)), project_(std::move(projectDirectory))
{
    setObjectName(QStringLiteral("cogTemplatesDialog"));
    setWindowTitle(QStringLiteral("COG Templates"));
    auto* const layout = new QVBoxLayout(this);
    auto* const row = new QHBoxLayout();
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("cogTemplates.sets"));
    list_->setMaximumWidth(240);
    for (const auto& set : assets::cogTemplateCatalog())
        list_->addItem(QString::fromStdString(set.title));
    notes_ = new QPlainTextEdit(this);
    notes_->setObjectName(QStringLiteral("cogTemplates.notes"));
    notes_->setReadOnly(true);
    row->addWidget(list_);
    row->addWidget(notes_, 1);
    layout->addLayout(row, 1);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* const copy = buttons->addButton(QStringLiteral("Copy into project"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("cogTemplates.copy"));
    copy->setEnabled(library_.has_value() && !project_.empty());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [this]() { notes_->setPlainText(copySelected() + QStringLiteral("\n\n") + notes_->toPlainText()); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](const int r) { selectSet(r); });
    layout->addWidget(buttons);
    if (!library_)
        notes_->setPlainText(QStringLiteral("RED's COG template library (data/cog_templates) isn't installed beside the editor."));
    else
        list_->setCurrentRow(0);
    resize(860, 560);
}

void CogTemplatesDialog::selectSet(const int row)
{
    const auto& catalog = assets::cogTemplateCatalog();
    if (!library_ || row < 0 || static_cast<std::size_t>(row) >= catalog.size()) return;
    if (list_->currentRow() != row) list_->setCurrentRow(row);
    notes_->setPlainText(QString::fromStdString(assets::cogTemplateNotes(*library_, catalog[static_cast<std::size_t>(row)])));
}

QString CogTemplatesDialog::copySelected()
{
    const auto& catalog = assets::cogTemplateCatalog();
    const int row = list_->currentRow();
    if (!library_ || row < 0 || static_cast<std::size_t>(row) >= catalog.size()) return QStringLiteral("Select a set first.");
    if (project_.empty()) return QStringLiteral("Save the level first, so RED knows its project folder.");
    const auto copy = assets::copyCogTemplateSet(*library_, catalog[static_cast<std::size_t>(row)], project_);
    if (!copy.error.empty()) return QString::fromStdString(copy.error);
    QStringList lines;
    if (!copy.conflicts.empty())
    {
        lines << QStringLiteral("Nothing was copied: these files are already in the project with different contents.");
        for (const auto& p : copy.conflicts) lines << QStringLiteral("  ") + QString::fromStdString(p.string());
        return lines.join('\n');
    }
    lines << QStringLiteral("Copied %1 file(s); %2 were already there.").arg(copy.written.size()).arg(copy.unchanged.size());
    for (const auto& p : copy.written) lines << QStringLiteral("  ") + QString::fromStdString(p.string());
    lines << QStringLiteral("Merge templates/%1.tpl into master.tpl, then place and bind as the notes say.")
                 .arg(QString::fromStdString(catalog[static_cast<std::size_t>(row)].id));
    return lines.join('\n');
}

QString CogTemplatesDialog::notesText() const
{
    return notes_->toPlainText();
}

} // namespace sed::ui::resources
