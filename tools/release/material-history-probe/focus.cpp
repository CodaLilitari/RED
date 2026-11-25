#include <QApplication>
#include <QAction>
#include <QWidget>
#include <QEventLoop>
#include <QTimer>
#include <QDialog>
#include <cstdio>
extern "C" void materialProbe(void* value)
{
    QWidget *main = reinterpret_cast<QWidget*>(value);
    auto info=[](QWidget *w) {
        if (!w) return QStringLiteral("null");
        return QString("%1/%2 [%3]").arg(w->metaObject()->className(), w->objectName(), w->window()->objectName());
    };
    auto output=[&](const char* stage) {
        auto *undo=main->findChild<QAction*>("edit.undo");
        auto *redo=main->findChild<QAction*>("edit.redo");
        std::fprintf(stderr,"MATERIAL %s mainVisible=%d mainActive=%d focus=%s active=%s mainLast=%s undo=%d redo=%d\n",stage,main->isVisible(),main->isActiveWindow(),qPrintable(info(QApplication::focusWidget())),qPrintable(info(QApplication::activeWindow())),qPrintable(info(main->focusWidget())),undo?undo->isEnabled():-1,redo?redo->isEnabled():-1);
    };
    output("before");
    if (auto* f=QApplication::focusWidget(); f && f->window()->objectName()=="agentReview.dialog") f->clearFocus();
    if (qEnvironmentVariableIsSet("MATERIAL_ACTIVATE_VIEW")) {
        main->setAttribute(Qt::WA_DontShowOnScreen,false);main->show();
        for(auto* d:main->findChildren<QDialog*>("agentReview.dialog")) d->hide();
        QApplication::setActiveWindow(main);
        for(auto* candidate:main->findChildren<QWidget*>()) if(QString::fromLatin1(candidate->metaObject()->className()).endsWith("GlView")){candidate->setFocus(Qt::OtherFocusReason);break;}
    }
    output("after");
}
