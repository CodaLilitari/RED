#pragma once

#include <QDockWidget>
#include <QEvent>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include <vector>
#include <utility>

namespace sed::ui
{

// Qt 6.8 creates native dock tabs without copying each dock's window icon.
// Match complete public tabified groups, never Qt's private tabData identity.
class NativeDockTabIconSynchronizer final : public QObject
{
public:
    explicit NativeDockTabIconSynchronizer(QMainWindow& window)
        : QObject(&window), window_(&window)
    {
        window.installEventFilter(this);
        connect(&window, &QMainWindow::tabifiedDockWidgetActivated,
            this, [this](QDockWidget*) { schedule(); });
        schedule();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch ( event->type() )
        {
            case QEvent::ChildAdded:
            case QEvent::ChildRemoved:
            case QEvent::LayoutRequest:
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::ParentChange:
                schedule();
                break;
            default:
                break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    using IconsByTitle = QHash<QString, QIcon>;

    bool isWorkspaceBar(const QTabBar* bar) const
    {
        // File/document tab bars belong to a dock or QTabWidget. Floating
        // native dock groups may have an ordinary QWidget between bar/owner.
        for ( const QWidget* parent = bar->parentWidget(); parent; parent = parent->parentWidget() )
        {
            if ( parent == window_ ) return true;
            if ( qobject_cast<const QDockWidget*>(parent)
                || qobject_cast<const QTabWidget*>(parent)
                || qobject_cast<const QMainWindow*>(parent) ) return false;
        }
        return false;
    }

    void schedule()
    {
        if ( pending_ ) return;
        pending_ = true;
        QTimer::singleShot(0, this, [this]() { refresh(); });
    }

    void observe(QDockWidget* dock)
    {
        if ( observedDocks_.contains(dock) ) return;
        observedDocks_.insert(dock);
        connect(dock, &QWidget::windowIconChanged, this, [this](const QIcon&) { schedule(); });
        connect(dock, &QWidget::windowTitleChanged, this, [this](const QString&) { schedule(); });
        connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) { schedule(); });
        connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) { schedule(); });
        connect(dock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) { schedule(); });
        connect(dock, &QObject::destroyed, this, [this, dock]()
        {
            observedDocks_.remove(dock);
            schedule();
        });
    }

    void observe(QTabBar* bar)
    {
        if ( observedBars_.contains(bar) ) return;
        observedBars_.insert(bar);
        bar->installEventFilter(this);
        connect(bar, &QTabBar::tabMoved, this, [this](int, int) { schedule(); });
        connect(bar, &QTabBar::currentChanged, this, [this](int) { schedule(); });
        connect(bar, &QObject::destroyed, this, [this, bar]()
        {
            observedBars_.remove(bar);
            published_.remove(bar);
        });
    }

    static bool matches(const QTabBar& bar, const IconsByTitle& group)
    {
        if ( bar.count() != group.size() ) return false;
        QSet<QString> titles{};
        for ( int index = 0; index < bar.count(); ++index )
        {
            const QString title = bar.tabText(index);
            if ( !group.contains(title) || titles.contains(title) ) return false;
            titles.insert(title);
        }
        return true;
    }

    void refresh()
    {
        pending_ = false;
        if ( !window_ ) return;
        std::vector<IconsByTitle> groups{};
        QSet<QDockWidget*> grouped{};
        for ( QDockWidget* dock : window_->findChildren<QDockWidget*>() )
        {
            observe(dock);
            if ( grouped.contains(dock) || dock->isHidden() || dock->isFloating() ) continue;
            QList<QDockWidget*> peers = window_->tabifiedDockWidgets(dock);
            peers.prepend(dock);
            IconsByTitle group{};
            bool unique = true;
            for ( QDockWidget* peer : peers )
            {
                grouped.insert(peer);
                if ( peer->isHidden() || peer->isFloating() ) continue;
                const QString title = peer->windowTitle();
                unique = unique && !title.isEmpty() && !group.contains(title);
                group.insert(title, peer->windowIcon());
            }
            if ( unique && group.size() > 1 ) groups.push_back(std::move(group));
        }

        for ( QTabBar* bar : window_->findChildren<QTabBar*>() )
        {
            if ( isWorkspaceBar(bar) ) observe(bar);
        }
        std::vector<QPointer<QTabBar>> bars{};
        for ( QTabBar* bar : observedBars_ ) bars.emplace_back(bar);
        std::vector<int> groupForBar(bars.size(), -1);
        std::vector<int> barsForGroup(groups.size(), 0);
        for ( std::size_t index = 0; index < bars.size(); ++index )
        {
            if ( !bars[index] || bars[index]->isHidden() || !isWorkspaceBar(bars[index]) ) continue;
            int matched = -1;
            for ( std::size_t group = 0; group < groups.size(); ++group )
            {
                if ( !matches(*bars[index], groups[group]) ) continue;
                if ( matched >= 0 )
                {
                    matched = -2;
                    break;
                }
                matched = static_cast<int>(group);
            }
            groupForBar[index] = matched;
            if ( matched >= 0 ) ++barsForGroup[static_cast<std::size_t>(matched)];
        }

        for ( std::size_t index = 0; index < bars.size(); ++index )
        {
            const QPointer<QTabBar> bar = bars[index];
            if ( !bar ) continue;
            const int group = groupForBar[index];
            const bool resolved = group >= 0 && barsForGroup[static_cast<std::size_t>(group)] == 1;
            const QSet<qint64> previous = published_.value(bar.data());
            QSet<qint64> published{};
            for ( int tab = 0; bar && tab < bar->count(); ++tab )
            {
                const QIcon current = bar->tabIcon(tab);
                if ( resolved )
                {
                    const QIcon desired = groups[static_cast<std::size_t>(group)].value(bar->tabText(tab));
                    if ( current.cacheKey() != desired.cacheKey() ) bar->setTabIcon(tab, desired);
                    if ( !desired.isNull() ) published.insert(desired.cacheKey());
                }
                else if ( previous.contains(current.cacheKey()) )
                {
                    // Remove only icons this controller published. An ambiguous
                    // group or a recycled bar must not retain a guessed icon.
                    bar->setTabIcon(tab, QIcon{});
                }
            }
            if ( bar ) published_.insert(bar.data(), published);
        }
    }

    QPointer<QMainWindow> window_{};
    QSet<QDockWidget*> observedDocks_{};
    QSet<QTabBar*> observedBars_{};
    QHash<QTabBar*, QSet<qint64>> published_{};
    bool pending_{};
};

} // namespace sed::ui
