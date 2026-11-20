#include "ui/shortcuts/command_search.hpp"
#include <algorithm>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace sed::ui
{
namespace
{
struct Command
{
    QList<QPointer<QAction>> parents;
    QPointer<QAction> action;
    QString path, keys;
    bool enabled() const
    {
        return action && action->isEnabled() && action->isVisible() &&
            std::all_of(parents.begin(), parents.end(), [](const auto& p) { return p && p->isEnabled() && p->isVisible(); });
    }
};

QString label(const QAction* a)
{
    return a->text().section('\t', 0, 0).remove('&').trimmed();
}

void collect(const QList<QAction*>& actions, const QString& prefix, QList<QPointer<QAction>> parents,
             QAction* search, QList<Command>& results)
{
    for ( QAction* a : actions )
    {
        if ( a == search || !a->isVisible() || a->isSeparator() || label(a).isEmpty() ) continue;
        const QString path = prefix.isEmpty() ? label(a) : prefix + QStringLiteral(" > ") + label(a);
        if ( auto* menu = a->menu() )
        {
            auto ancestors = parents;
            ancestors.append(a);
            collect(menu->actions(), path, std::move(ancestors), search, results);
        }
        else results.append(Command{parents, a, path, a->shortcut().toString(QKeySequence::NativeText)});
    }
}
} // namespace

void addCommandSearch(QMenuBar* bar, QMenu* edit, QWidget* owner, CommandDisabledReason reason)
{
    auto* search = edit->addAction(QObject::tr("Find Command..."));
    search->setObjectName("edit.find_command");
    search->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    search->setToolTip(QObject::tr("Search editor menus and see the scope and availability of an action."));
    QObject::connect(search, &QAction::triggered, owner, [bar, owner, search, reason]()
    {
        QList<Command> commands;
        collect(bar->actions(), {}, {}, search, commands);
        QDialog dialog(owner);
        dialog.setObjectName("commandSearch");
        dialog.setWindowTitle(QObject::tr("Find Command"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* query = new QLineEdit(&dialog);
        query->setObjectName("commandSearch.query");
        query->setPlaceholderText(QObject::tr("Search by command, menu or shortcut"));
        query->setAccessibleName(QObject::tr("Command search"));
        auto* list = new QListWidget(&dialog);
        list->setAccessibleName(QObject::tr("Matching commands"));
        list->setObjectName("commandSearch.results");
        auto* detail = new QLabel(&dialog);
        detail->setWordWrap(true);
        detail->setObjectName("commandSearch.reason");
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
        auto* open = buttons->button(QDialogButtonBox::Open);
        open->setText(QObject::tr("Run"));
        layout->addWidget(query); layout->addWidget(list); layout->addWidget(detail); layout->addWidget(buttons);
        const auto selected = [&]() -> const Command*
        {
            auto* item = list->currentItem();
            return item ? &commands.at(item->data(Qt::UserRole).toInt()) : nullptr;
        };
        const auto refreshDetails = [&]()
        {
            const auto* c = selected();
            open->setEnabled(c && c->enabled());
            if ( !c || !c->action ) detail->setText(QObject::tr("No matching command."));
            else if ( !c->enabled() ) detail->setText(reason(c->action));
            else detail->setText(c->action->statusTip().isEmpty() ? c->action->toolTip() : c->action->statusTip());
        };
        const auto filter = [&]()
        {
            const auto words = query->text().simplified().split(' ', Qt::SkipEmptyParts);
            list->clear();
            for ( qsizetype i = 0; i < commands.size(); ++i )
            {
                const auto& c = commands.at(i);
                const QString text = c.keys.isEmpty() ? c.path : c.path + QStringLiteral("    ") + c.keys;
                if ( !std::all_of(words.begin(), words.end(), [&](const auto& word)
                    { return text.contains(word, Qt::CaseInsensitive); }) ) continue;
                auto* item = new QListWidgetItem(text, list);
                item->setData(Qt::UserRole, int(i));
                if ( !c.enabled() ) item->setForeground(list->palette().brush(QPalette::Disabled, QPalette::Text));
            }
            list->setCurrentRow(0); refreshDetails();
        };
        QPointer<QAction> chosen;
        const auto accept = [&]()
        {
            const auto* c = selected();
            if ( !c || !c->enabled() ) { refreshDetails(); return; }
            chosen = c->action; dialog.accept();
        };
        QObject::connect(query, &QLineEdit::textChanged, &dialog, filter);
        QObject::connect(query, &QLineEdit::returnPressed, &dialog, accept);
        QObject::connect(list, &QListWidget::currentRowChanged, &dialog, refreshDetails);
        QObject::connect(list, &QListWidget::itemActivated, &dialog, accept);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        for ( int step : {-1, 1} )
        {
            auto* key = new QShortcut(QKeySequence(step < 0 ? Qt::Key_Up : Qt::Key_Down), query);
            key->setContext(Qt::WidgetShortcut);
            QObject::connect(key, &QShortcut::activated, &dialog, [=]()
            {
                if ( list->count() ) list->setCurrentRow(std::clamp(list->currentRow() + step, 0, list->count()-1));
            });
        }
        filter(); query->setFocus();
        dialog.resize(620, 460);
        if ( dialog.exec() == QDialog::Accepted && chosen && chosen->isEnabled() ) chosen->trigger();
    });
}
} // namespace sed::ui
