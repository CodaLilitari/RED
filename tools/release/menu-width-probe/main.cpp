#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QStyle>
#include <QStyleFactory>
#include <QDebug>
#include <QFontMetrics>
#include <QStyleOptionMenuItem>
#include <iostream>
#define private public
#include "ui/shortcuts/key_sequence_display.hpp"
#undef private
using sed::ui::shortcuts::KeySequenceDisplayMenu;
void trace(const char* label, const KeySequenceDisplayMenu& m) {
    auto c=m.shortcutColumnWidths();
    std::cerr<<label<<" min="<<m.minimumWidth()<<" floor="<<m.callerMinimumWidth_<<" applied="<<m.appliedMinimumWidth_
             <<" cachedNative="<<m.nativeSizeHintWidth_<<" cache="<<m.nativeSizeHintWidthCached_
             <<" reset="<<m.minimumWidthTemporarilyReset_<<" cols="<<c.native<<"/"<<c.formatted<<" nativeBase="<<m.QMenu::sizeHint().width()<<"\n";
}
void scan(bool traceSteps) {
  QKeySequence key(int(Qt::KeypadModifier)|Qt::Key_F12);
  auto report=[&](const char* label, const KeySequenceDisplayMenu& m){if(traceSteps)trace(label,m);};
  KeySequenceDisplayMenu plain;auto a=plain.addAction("Visible");a->setShortcut(key);a->setShortcutVisibleInContextMenu(true);
  int w0=plain.sizeHint().width();report("visibleOnly",plain);
  KeySequenceDisplayMenu both; auto b=both.addAction("Visible");b->setShortcut(key);b->setShortcutVisibleInContextMenu(true);report("before extra",both);
  auto h=both.addAction("Hidden");report("after extra",both);h->setShortcut(QKeySequence(Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier|Qt::Key_F24));report("extra key",both);
  h->setShortcutVisibleInContextMenu(true);report("extra visible shortcut",both);h->setVisible(false);report("after hide",both);
  int w1=both.sizeHint().width();report("withInvisible",both);
  KeySequenceDisplayMenu m;m.setMinimumWidth(350);report("caller new",m);auto t=m.addAction("Initial minimum");report("caller add",m);t->setShortcut(key);report("caller shortcut",m);t->setShortcutVisibleInContextMenu(true);report("caller visible",m);
  int cm=m.sizeHint().width();report("caller final",m);
  std::cerr<<"RESULT traced="<<traceSteps<<" base="<<w0<<" hidden="<<w1<<" floorMin="<<m.minimumWidth()<<" floorHint="<<cm<<" style="<<qApp->style()->name().toStdString()<<" font="<<m.font().family().toStdString()<<"\n";
  QFontMetrics metrics(m.font());for(int i=0;i<10;i++){QKeySequence k(int(Qt::KeypadModifier)|int(Qt::Key_0+i));std::cerr<<"digit="<<i<<" native="<<metrics.horizontalAdvance(k.toString(QKeySequence::NativeText))<<" formatted="<<metrics.horizontalAdvance(sed::ui::shortcuts::keySequenceDisplayText(k))<<"\n";}
}
int main(int argc,char** argv){QApplication a(argc,argv);scan(false);scan(true);a.setStyle(QStyleFactory::create("Fusion"));scan(false);scan(true);}
