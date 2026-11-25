#pragma once

#include "ui/semantic/semantic_review_io.hpp"

#include <QDialog>

#include <functional>
#include <memory>

class QCloseEvent;

namespace sed::ui::semantic_review
{

// Read-only file/receipt review, deliberately independent of MainWindow and the
// live document. The owner chooses how to expose it in its menus. The loader
// boundary allows isolated hosts to supply their captured-file review service;
// the ordinary constructor uses the bounded native/receipt service above.
class SemanticReviewDialog final : public QDialog
{
public:
    using Loader = std::function<Result(const Request&, sed::StopToken)>;

    explicit SemanticReviewDialog(QWidget* parent = nullptr);
    explicit SemanticReviewDialog(Loader loader, QWidget* parent = nullptr);
    ~SemanticReviewDialog() override;

    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::semantic_review
