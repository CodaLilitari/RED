#include "ui/project/resource_family_browser.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "resource_families.hpp"

#include <QComboBox>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sed::ui
{
namespace
{
using Paths = std::pair<std::filesystem::path, std::filesystem::path>;
QString display(const std::string &s)
{
    return QString::fromStdString(s);
}
struct ScanResult
{
    FamilyIndex index;
    QString modelReport;
    QString error;
};
enum class Request
{
    Inspect,
    Model,
    Preview,
    Insert
};

class FamilyBrowser final : public QGroupBox
{
  public:
    FamilyBrowser(QWidget *parent, std::function<Paths()> paths, FamilyActions actions)
        : QGroupBox(tr("Project resources"), parent), paths_(std::move(paths)), actions_(std::move(actions))
    {
        auto *layout = new QVBoxLayout(this);
        auto *controls = new QHBoxLayout;
        layout->addLayout(controls);
        find_ = new QLineEdit(this);
        find_->setObjectName("projectAssetSearch");
        find_->setPlaceholderText(tr("Find a model, material, animation, script, sound or template"));
        kind_ = new QComboBox(this);
        kind_->addItems({tr("All types"), "Template", "Model", "Material", "Animation", "Puppet", "Script", "Sound",
                         "Sound class", "AI"});
        auto *inspect = button(tr("Inspect resources"), "inspectProjectAssets");
        controls->addWidget(find_, 1);
        controls->addWidget(kind_);
        controls->addWidget(inspect);
        auto *body = new QHBoxLayout;
        layout->addLayout(body, 1);
        rows_ = new QTreeWidget(this);
        rows_->setObjectName("projectAssetResults");
        rows_->setHeaderLabels({tr("Resource"), tr("Type")});
        body->addWidget(rows_);
        details_ = new QPlainTextEdit(this);
        details_->setObjectName("projectAssetExplanation");
        details_->setReadOnly(true);
        body->addWidget(details_, 2);
        auto *footer = new QHBoxLayout;
        layout->addLayout(footer);
        status_ = new QLabel(tr("Inspect the declared project and stock mounts to search their contents."), this);
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        footer->addWidget(status_, 1);
        model_ = button(tr("Inspect LODs�"), "inspectProjectModel");
        footer->addWidget(model_);
        preview_ = button(tr("Preview…"), "previewProjectAsset");
        insert_ = button(tr("Insert family…"), "insertProjectFamily");
        footer->addWidget(preview_);
        footer->addWidget(insert_);
        connect(inspect, &QPushButton::clicked, this, [this] { scan(Request::Inspect); });
        connect(find_, &QLineEdit::returnPressed, this, [this] { scan(Request::Inspect); });
        connect(find_, &QLineEdit::textChanged, this, [this] { filter(); });
        connect(kind_, &QComboBox::currentIndexChanged, this, [this] { filter(); });
        connect(rows_, &QTreeWidget::currentItemChanged, this, [this] { explain(); });
        connect(model_, &QPushButton::clicked, this, [this] { scan(Request::Model); });
        connect(preview_, &QPushButton::clicked, this, [this] { scan(Request::Preview); });
        connect(insert_, &QPushButton::clicked, this, [this] { scan(Request::Insert); });
        explain();
    }

  private:
    QPushButton *button(const QString &label, const char *name)
    {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        return b;
    }
    std::string selected() const
    {
        auto *item = rows_->currentItem();
        return item ? item->data(0, Qt::UserRole).toString().toStdString() : std::string{};
    }
    void filter()
    {
        const auto previous = selected();
        rows_->clear();
        if ( !index_ )
        {
            return;
        }
        const auto matches = ::sed::project::searchAssets(*index_, find_->text().toStdString());
        std::size_t count{};
        QTreeWidgetItem *select{};
        for ( const auto &key : matches )
        {
            const auto &row = index_->rows.at(key);
            if ( kind_->currentIndex() && kind_->currentText() != display(row.kind) )
            {
                continue;
            }
            if ( ++count > 500 )
            {
                continue;
            }
            auto *item = new QTreeWidgetItem(rows_, {display(row.name), display(row.kind)});
            item->setData(0, Qt::UserRole, display(key));
            if ( key == previous )
            {
                select = item;
            }
        }
        rows_->setCurrentItem(select ? select : rows_->topLevelItem(0));
        rows_->resizeColumnToContents(0);
        status_->setText(count > 500 ? tr("Showing the first 500 of %1 matches. Refine the search.").arg(count)
                                     : tr("%1 matching resources.").arg(count));
        explain();
    }
    void explain()
    {
        const std::string key = selected();
        const bool exists = index_ && index_->rows.contains(key);
        model_->setEnabled(false);
        preview_->setEnabled(false);
        insert_->setEnabled(false);
        details_->clear();
        if ( !exists )
        {
            return;
        }
        const auto &row = index_->rows.at(key);
        QStringList text;
        text << tr("Target: %1 / %2 / %3")
                    .arg(display(index_->sources.target.game), display(index_->sources.target.engine),
                         display(index_->sources.target.renderer));
        text << (row.missing ? tr("Missing resource") : tr("Winner: %1").arg(display(row.location)));
        for ( const auto &source : row.shadows )
        {
            text << tr("Shadowed: %1").arg(display(source));
        }
        text << tr("Dependencies");
        bool complete = !row.missing;
        for ( const auto &child : ::sed::project::resourceFamily(*index_, key) )
        {
            const auto &dep = index_->rows.at(child);
            complete &= !dep.missing;
            if ( child != key )
            {
                text << tr("%1 — %2")
                            .arg(display(dep.name), dep.missing ? tr("required, missing")
                                                    : dep.stock ? tr("required from game")
                                                                : tr("supplied by project"));
            }
        }
        text << tr("What changes if replaced");
        for ( const auto &parent : ::sed::project::affectedResources(*index_, key) )
        {
            if ( parent != key )
            {
                text << display(index_->rows.at(parent).name);
            }
        }
        if ( !row.dependencies.empty() )
        {
            text << tr("Direct references");
            for ( const auto &[child, relation] : row.dependencies )
            {
                text << display(relation) + QStringLiteral(" → ") + display(index_->rows.at(child).name);
            }
        }
        details_->setPlainText(text.join('\n'));
        model_->setEnabled(!row.missing && row.kind == "Model");
        preview_->setEnabled(actions_.preview && !row.missing && row.kind != "Template library" &&
                             (row.winner || row.authoringTemplate));
        insert_->setEnabled(actions_.insert && row.authoringTemplate && complete);
    }
    void scan(Request request)
    {
        const auto paths = paths_();
        if ( paths.first.empty() )
        {
            status_->setText(tr("Choose a project profile."));
            return;
        }
        const auto key = selected();
        const auto expected = index_ ? index_->sources.revision : std::string{};
        const auto scanId = ++serial_;
        status_->setText(tr("Reading the project resource revisions…"));
        auto *watcher = new QFutureWatcher<ScanResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, paths, request, key, expected, scanId]
                {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if ( scanId != serial_ )
                    {
                        return;
                    }
                    if ( paths_() != paths || !result.index || !result.error.isEmpty() )
                    {
                        index_.reset();
                        rows_->clear();
                        explain();
                        status_->setText(result.error.isEmpty()
                                             ? tr("The project selection changed. Inspect again.")
                                             : result.error);
                        return;
                    }
                    index_ = result.index;
                    filter();
                    if ( request == Request::Inspect )
                    {
                        return;
                    }
                    if ( index_->sources.revision != expected || key != selected() )
                    {
                        status_->setText(tr("Project resources changed. Review the current winner and try again."));
                        return;
                    }
                    try
                    {
                        if ( request == Request::Model )
                        {
                            details_->appendPlainText(result.modelReport);
                        }
                        else if ( request == Request::Preview && actions_.preview )
                        {
                            actions_.preview(this, index_, key);
                        }
                        else if ( request == Request::Insert && actions_.insert )
                        {
                            actions_.insert(this, index_, key, paths.first, paths.second);
                        }
                    }
                    catch ( const std::exception &error )
                    {
                        status_->setText(QString::fromUtf8(error.what()));
                    }
                });
        watcher->setFuture(detail::runPreviewWorkerTask(
            [paths, request, key]
            {
                ScanResult result;
                try
                {
                    result.index =
                        std::make_shared<::sed::project::ResourceFamilyIndex>(::sed::project::indexProjectAssets(
                            ::sed::project::captureAssetSources(paths.first, paths.second)));
                    if ( request == Request::Model )
                    {
                        result.modelReport = display(::sed::project::inspectFamilyModel(*result.index, key));
                    }
                }
                catch ( const std::exception &error )
                {
                    result.error = QString::fromUtf8(error.what());
                }
                return result;
            }));
    }
    std::function<Paths()> paths_;
    FamilyActions actions_;
    FamilyIndex index_;
    unsigned long long serial_{};
    QLineEdit *find_{};
    QComboBox *kind_{};
    QTreeWidget *rows_{};
    QPlainTextEdit *details_{};
    QLabel *status_{};
    QPushButton *model_{};
    QPushButton *preview_{};
    QPushButton *insert_{};
};
} // namespace
QWidget *createResourceFamilyBrowser(QWidget *parent, std::function<Paths()> selectedPaths, FamilyActions actions)
{
    return new FamilyBrowser(parent, std::move(selectedPaths), std::move(actions));
}
} // namespace sed::ui
