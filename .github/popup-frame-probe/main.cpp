#include <QApplication>
#include <QDialog>
#include <QWindow>
#include <QScreen>
#include <QTimer>
#include <QLayout>
#include <QLabel>
#include <QShowEvent>
#include <cstdio>
class Popup : public QDialog {
public:
    using QDialog::QDialog;
    int mode{};
    QPoint target;
    void showEvent(QShowEvent* e) override {
        QDialog::showEvent(e);
        if(mode==5 && windowHandle()) windowHandle()->setFramePosition(target);
    }
};
void inspect(const char* stage, const Popup& w) {
    const auto f=w.frameGeometry(), g=w.geometry();
    const QMargins m=w.windowHandle()?w.windowHandle()->frameMargins():QMargins();
    const auto pos=w.pos(), n=w.windowHandle()?w.windowHandle()->framePosition():QPoint();
    const auto c=w.mapToGlobal(QPoint());
    std::printf("%s method=%d goal=%d,%d frame=%d,%d/%d,%d geometry=%d,%d/%d,%d pos=%d,%d content=%d,%d nativeframe=%d,%d margins=%d,%d,%d,%d\n",
        stage,w.mode,w.target.x(),w.target.y(),f.x(),f.y(),f.width(),f.height(),g.x(),g.y(),g.width(),g.height(),pos.x(),pos.y(),c.x(),c.y(),n.x(),n.y(),m.left(),m.top(),m.right(),m.bottom()); fflush(stdout);
}
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    QWidget owner; owner.setGeometry(110,90,820,640); owner.show(); app.processEvents();
    const QRect available=owner.screen()->availableGeometry().adjusted(6,6,-6,-6);
    for (int mode=0; mode<8; ++mode) for(bool edge : {false,true}) {
        Popup dialog(&owner, Qt::Popup | Qt::FramelessWindowHint); dialog.mode=mode;
        dialog.setWindowTitle("Select mask"); dialog.setModal(true);
        auto* layout=new QVBoxLayout(&dialog); layout->addWidget(new QLabel("Test", &dialog));
        dialog.ensurePolished(); layout->activate(); dialog.adjustSize(); dialog.setFixedSize(404,267);
        dialog.target=edge?QPoint(available.right()-dialog.width()+1,available.bottom()-dialog.height()+1):QPoint(638,216);
        if(mode==1 || mode==2 || mode==3 || mode==5) (void)dialog.winId();
        dialog.move(dialog.target);
        if(mode==2) dialog.windowHandle()->setFramePosition(dialog.target);
        if(mode==3) QObject::connect(dialog.windowHandle(),&QWindow::visibleChanged,&dialog,[&](bool v){if(v)dialog.windowHandle()->setFramePosition(dialog.target);});
        if(mode==4) QTimer::singleShot(0,&dialog,[&]{dialog.windowHandle()->setFramePosition(dialog.target);});
        if(mode==6 || mode==7) { dialog.show(); if(mode==6) dialog.windowHandle()->setFramePosition(dialog.target); else dialog.move(dialog.target); }
        inspect("before",dialog);
        QTimer::singleShot(25,&dialog,[&]{inspect("open",dialog); dialog.accept();});
        dialog.exec();
    }
}
