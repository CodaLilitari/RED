#pragma once

#include "app_logging.hpp"

#include <QDialog>
#include <QEvent>
#include <QMessageBox>
#include <QObject>
#include <QSize>
#include <QShowEvent>
#include <QString>

namespace sed::ui::components
{

class SharedUnsavedChangesMessageBox final : public QMessageBox
{
public:
    SharedUnsavedChangesMessageBox(
        const QString& title,
        const QString& text,
        const QMessageBox::StandardButtons buttons)
        : QMessageBox(QMessageBox::Question, title, text, buttons, nullptr)
    {
    }

protected:
    void showEvent(QShowEvent* const event) override
    {
        QMessageBox::showEvent(event);
        enforceMinimumPromptSize();
    }

    bool event(QEvent* const event) override
    {
        const bool handled = QMessageBox::event(event);
        if ( event->type() == QEvent::LayoutRequest && isVisible() )
        {
            // QMessageBox recalculates a fixed size on layout requests, even
            // after showEvent. Preserve its fit for long messages as well.
            enforceMinimumPromptSize();
        }
        return handled;
    }

private:
    void enforceMinimumPromptSize()
    {
        const QSize bounded = size().expandedTo(QSize(560, 220));
        if ( minimumSize().width() < 560 || minimumSize().height() < 220 || size() != bounded )
        {
            setFixedSize(bounded);
        }
    }
};

/** Shows the common application-modal question used before closing an edited document. */
inline QMessageBox::StandardButton showSharedUnsavedChangesPrompt(
    QObject* const owner,
    const QString& title,
    const QString& documentName,
    const QString& loggingContext = {})
{
    SharedUnsavedChangesMessageBox prompt(
        title,
        QStringLiteral("Save changes to %1 before closing?").arg(documentName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    prompt.setWindowModality(Qt::ApplicationModal);
    prompt.setObjectName(QStringLiteral("resource_authoring.unsaved_prompt"));
    prompt.setWindowFlag(Qt::WindowMinMaxButtonsHint, false);
    prompt.setMinimumSize(560, 220);
    prompt.setDefaultButton(QMessageBox::Save);
    prompt.setEscapeButton(QMessageBox::Cancel);
    if ( !loggingContext.trimmed().isEmpty() )
    {
        prompt.setProperty(kMessageBoxLoggingContextProperty, loggingContext);
    }
    if ( owner )
    {
        QObject::connect(owner, &QObject::destroyed, &prompt, &QDialog::reject);
    }
    return static_cast<QMessageBox::StandardButton>(prompt.exec());
}

} // namespace sed::ui::components
