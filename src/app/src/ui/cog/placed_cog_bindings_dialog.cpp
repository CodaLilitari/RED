#include "ui/cog/placed_cog_bindings_dialog.hpp"

#include "ui/cog/cog_gui_wiring_utils.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace sed::ui::cog
{

PlacedCogBindingsDialog::PlacedCogBindingsDialog(
    const level::CogBindingDescription& description,
    RebindHandler rebindHandler,
    QWidget* parent)
    : QDialog(parent)
    , description_(description)
    , rebindHandler_(std::move(rebindHandler))
{
    setWindowTitle(tr("COG bindings — %1")
        .arg(QString::fromStdString(description_.scriptName)));
    resize(760, 480);

    auto* root = new QVBoxLayout(this);
    noteLabel_ = new QLabel(this);
    root->addWidget(noteLabel_);

    auto* semantics = new QLabel(
        QString::fromStdString(
            std::string(level::kCogEventLinkSemantics)), this);
    semantics->setWordWrap(true);
    root->addWidget(semantics);

    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("Parameter"), QStringLiteral("Type"),
        QStringLiteral("Raw value"), QStringLiteral("Target"),
        QStringLiteral("Link"), QStringLiteral("Event link") });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(table_);

    auto* rebindRow = new QHBoxLayout();
    rebindRow->addWidget(new QLabel(tr("New target index (-1 = null):"), this));
    targetSpin_ = new QSpinBox(this);
    targetSpin_->setRange(-1, 1000000);
    targetSpin_->setValue(-1);
    rebindRow->addWidget(targetSpin_);
    rebindButton_ = new QPushButton(tr("Rebind selected reference"), this);
    rebindRow->addWidget(rebindButton_);
    rebindRow->addStretch(1);
    root->addLayout(rebindRow);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(rebindButton_, &QPushButton::clicked, this, [this]()
        {
            if (!rebindHandler_ || !table_)
            {
                return;
            }
            const int row = table_->currentRow();
            if (row < 0)
            {
                statusLabel_->setText(
                    tr("Select a typed reference row first."));
                return;
            }
            const auto refusal = rebindHandler_(
                static_cast<std::int32_t>(row),
                static_cast<std::int32_t>(targetSpin_->value()));
            if (refusal.has_value())
            {
                // Prepare-step refusal: show the stable code, change nothing.
                statusLabel_->setText(QString::fromStdString(
                    formatCogMutationRefusal(*refusal)));
                return;
            }
            statusLabel_->setText(tr("Reference rebound."));
        });

    refreshRows();
}

void PlacedCogBindingsDialog::refreshRows()
{
    const PlacedCogRowMode mode = placedCogRowMode(description_);
    const std::string note = placedCogFallbackNote(description_);
    noteLabel_->setText(QString::fromStdString(note));
    noteLabel_->setVisible(!note.empty());

    const std::vector<PlacedCogRowView> rows =
        buildPlacedCogRows(description_);
    table_->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const PlacedCogRowView& row = rows[i];
        const auto setCell = [this](const int r, const int c,
                                 const std::string& text)
        {
            table_->setItem(r, c,
                new QTableWidgetItem(QString::fromStdString(text)));
        };
        const int r = static_cast<int>(i);
        setCell(r, 0, row.label);
        setCell(r, 1, row.typeName);
        setCell(r, 2, row.rawValue);
        setCell(r, 3, row.targetText);
        setCell(r, 4, row.linkText);
        setCell(r, 5, row.eventLinkCandidate
            ? "candidate (runtime unverified)" : "");
    }
    // Positional rows never offer typed rebinding.
    const bool typed = mode == PlacedCogRowMode::typed;
    rebindButton_->setEnabled(typed && static_cast<bool>(rebindHandler_));
    targetSpin_->setEnabled(typed);
}

} // namespace sed::ui::cog
