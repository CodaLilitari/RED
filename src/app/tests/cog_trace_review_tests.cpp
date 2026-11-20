#include "ui/assistants/cog_trace_review.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <memory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using sed::assets::CogMessageBreakpoint;
    const std::vector<CogMessageBreakpoint> history{
        {.message = "startup", .watches = {{"door", "4"}}},
        {.time = 0.2, .message = "activated", .senderType = 3, .senderRef = 4, .watches = {{"door", "4"}}, .due = {{"timer", 1.2}}},
        {.time = 1.2, .message = "timer", .watches = {{"door", "4"}}}};
    int kind{}, reference{-1};
    auto dialog = std::unique_ptr<QDialog>(sed::ui::assistants::createCogTraceReview(
        nullptr, history, "startup:\n return;\nactivated:\n return;\ntimer:\n return;\n", {}, [&](int k, int r) {
            kind = k;
            reference = r;
        }));
    const auto step = dialog->findChild<QPushButton *>("stepMessage");
    const auto go = dialog->findChild<QPushButton *>("continueMessage");
    const auto pauseOn = dialog->findChild<QComboBox *>("breakOnMessage");
    const auto view = dialog->findChild<QTreeWidget *>("messageTrace");
    const auto details = dialog->findChild<QPlainTextEdit *>("breakpointState");
    const auto show = dialog->findChild<QPushButton *>("locateCogSender");
    const auto source = dialog->findChild<QPlainTextEdit *>("cogTraceSource");
    if (!step || !go || !pauseOn || !view || !details || !show || !source || view->currentItem()->text(1) != "startup") return 1;
    step->click();
    if (view->currentItem()->text(1) != "activated" || !details->toPlainText().contains("door = 4") ||
        !details->toPlainText().contains("1.2") || source->textCursor().blockNumber() != 2)
        return 2;
    show->click();
    if (kind != 3 || reference != 4) return 3;
    pauseOn->setCurrentText("timer");
    go->click();
    return view->currentItem()->text(1) == "timer" && !step->isEnabled() ? 0 : 4;
}
