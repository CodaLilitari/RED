#include "ui/assistants/cog_trace_review.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sed::ui::assistants
{
QDialog *createCogTraceReview(QWidget *parent, std::vector<assets::CogMessageBreakpoint> events, const std::string &script,
                              const std::vector<std::string> &unsimulated, TraceHighlight highlight)
{
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(QObject::tr("COG message workbench"));
    dialog->resize(800, 540);
    auto *layout = new QVBoxLayout(dialog);
    auto *controls = new QHBoxLayout;
    layout->addLayout(controls);
    auto *stop = new QComboBox(dialog);
    stop->setObjectName("breakOnMessage");
    controls->addWidget(new QLabel(QObject::tr("Break on message"), dialog));
    controls->addWidget(stop);
    const auto button = [&](const char *name, const QString &text) {
        auto *b = new QPushButton(text, dialog);
        b->setObjectName(name);
        controls->addWidget(b);
        return b;
    };
    auto *step = button("stepMessage", QObject::tr("Step"));
    auto *go = button("continueMessage", QObject::tr("Continue"));
    auto *sender = button("locateCogSender", QObject::tr("Highlight sender"));
    auto *source = button("locateCogSource", QObject::tr("Highlight source"));
    auto *watch = new QLineEdit(dialog);
    watch->setObjectName("cogWatchSearch");
    watch->setPlaceholderText(QObject::tr("Filter symbol watches"));
    layout->addWidget(watch);
    auto *panes = new QSplitter(Qt::Horizontal, dialog);
    layout->addWidget(panes);
    auto *list = new QTreeWidget(panes);
    list->setObjectName("messageTrace");
    list->setHeaderLabels({QObject::tr("Seconds"), QObject::tr("Message")});
    auto *state = new QPlainTextEdit(panes);
    state->setObjectName("breakpointState");
    state->setReadOnly(true);
    auto *code = new QPlainTextEdit(panes);
    code->setObjectName("cogTraceSource");
    code->setReadOnly(true);
    code->setPlainText(QString::fromStdString(script));
    QStringList limits;
    limits << QObject::tr("Recorded simulator states at handler entry. Timers use simulation seconds. Collision, audio, physics and AI "
                          "require target observation.");
    for (const auto &value : unsimulated)
        limits << QString::fromStdString(value);
    auto *coverage = new QLabel(limits.join('\n'), dialog);
    coverage->setTextFormat(Qt::PlainText);
    coverage->setWordWrap(true);
    layout->addWidget(coverage);
    auto data = std::make_shared<const std::vector<assets::CogMessageBreakpoint>>(std::move(events));
    for (const auto &event : *data)
    {
        const auto name = QString::fromStdString(event.message);
        new QTreeWidgetItem(list, {QString::number(event.time, 'f', 3), name});
        if (stop->findText(name) < 0) stop->addItem(name);
    }
    const auto cursor = [=] { return list->indexOfTopLevelItem(list->currentItem()); };
    const auto update = [=] {
        const int current = cursor();
        const bool selected = current >= 0;
        const bool more = selected && current + 1 < list->topLevelItemCount();
        go->setEnabled(more);
        step->setEnabled(more);
        sender->setEnabled(false);
        source->setEnabled(false);
        if (!selected) return;
        const auto &event = data->at(static_cast<std::size_t>(current));
        const auto valid = [](int kind, int ref) { return ref >= 0 && (kind == 3 || kind == 5 || kind == 6); };
        sender->setEnabled(highlight && valid(event.senderType, event.senderRef));
        source->setEnabled(highlight && valid(event.sourceType, event.sourceRef));
        QStringList lines;
        lines << QObject::tr("COG %1, sender %2/%3, source %4/%5")
                     .arg(event.cog)
                     .arg(event.senderType)
                     .arg(event.senderRef)
                     .arg(event.sourceType)
                     .arg(event.sourceRef);
        for (const auto &[name, due] : event.due)
            lines << QObject::tr("%1 due at %2 s").arg(QString::fromStdString(name)).arg(due);
        for (const auto &[name, value] : event.watches)
            if (QString::fromStdString(name).contains(watch->text(), Qt::CaseInsensitive))
                lines << QString::fromStdString(name + " = " + value);
        state->setPlainText(lines.join('\n'));
        const auto name = QRegularExpression::escape(QString::fromStdString(event.message));
        const QRegularExpression label("^\\s*" + name + "\\s*:",
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
        const auto found = code->document()->find(label);
        if (!found.isNull())
        {
            code->setTextCursor(found);
            code->centerCursor();
        }
    };
    QObject::connect(list, &QTreeWidget::currentItemChanged, dialog, update);
    QObject::connect(watch, &QLineEdit::textChanged, dialog, update);
    QObject::connect(step, &QPushButton::clicked, dialog, [=] { list->setCurrentItem(list->topLevelItem(cursor() + 1)); });
    QObject::connect(go, &QPushButton::clicked, dialog, [=] {
        const int last = list->topLevelItemCount() - 1;
        for (int i = cursor() + 1; i <= last; ++i)
            if (i == last || list->topLevelItem(i)->text(1) == stop->currentText())
            {
                list->setCurrentItem(list->topLevelItem(i));
                break;
            }
    });
    QObject::connect(sender, &QPushButton::clicked, dialog, [=] {
        const auto &e = data->at(cursor());
        highlight(e.senderType, e.senderRef);
    });
    QObject::connect(source, &QPushButton::clicked, dialog, [=] {
        const auto &e = data->at(cursor());
        highlight(e.sourceType, e.sourceRef);
    });
    if (highlight) QObject::connect(dialog, &QDialog::finished, dialog, [=] { highlight(-1, -1); });
    auto *done = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(done);
    QObject::connect(done, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    list->setCurrentItem(list->topLevelItem(0));
    update();
    return dialog;
}
} // namespace sed::ui::assistants
