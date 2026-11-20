#include "ui/shortcuts/command_search.hpp"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QWidget window;
    QMenuBar bar(&window);
    auto* edit = bar.addMenu("Edit");
    auto* sector = edit->addMenu("Sector");
    auto* cut = sector->addAction("Cut");
    auto* blocked = sector->addAction("Repair");
    blocked->setEnabled(false);
    int calls = 0, failures = 0;
    QObject::connect(cut, &QAction::triggered, &window, [&]{ ++calls; });
    sed::ui::addCommandSearch(&bar, edit, &window, [](auto*) { return QStringLiteral("Select a sector."); });
    auto* search = edit->findChild<QAction*>("edit.find_command");
    if ( !search ) return 1;
    QTimer::singleShot(0, [&]{
        auto* dialog = window.findChild<QDialog*>("commandSearch");
        if ( !dialog ) { ++failures; return; }
        auto* query = dialog->findChild<QLineEdit*>("commandSearch.query");
        auto* rows = dialog->findChild<QListWidget*>("commandSearch.results");
        auto* reason = dialog->findChild<QLabel*>("commandSearch.reason");
        query->setText("sector repair");
        if ( rows->count()!=1 || !reason->text().contains("Select a sector") ) ++failures;
        QMetaObject::invokeMethod(query, "returnPressed");
        if ( !dialog->isVisible() ) ++failures;
        query->setText("CUT sector");
        if ( rows->count()!=1 || !rows->currentItem()->text().contains("Edit > Sector > Cut") ) ++failures;
        QMetaObject::invokeMethod(query, "returnPressed");
    });
    search->trigger();
    if ( calls!=1 || failures ) { std::cerr << "Command search failed\n"; return 1; }
    return 0;
}
