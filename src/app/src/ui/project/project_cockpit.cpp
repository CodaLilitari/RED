#include "ui/project/project_cockpit.hpp"
#include "ui/project/project_playtest.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "project_profile.hpp"
#include "assets/atomic_file_write.hpp"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>
#include <span>

namespace sed::ui
{
namespace
{
namespace fs = std::filesystem;

fs::path localPath(const QString& text)
{
#ifdef Q_OS_WIN
    return fs::path(text.toStdWString());
#else
    return fs::path(text.toUtf8().toStdString());
#endif
}

struct Result
{
    QString error;
    project::Prepared project;
    std::unique_ptr<assets::detail::StagedAssetFile> staged;
};

class Cockpit final : public QDialog
{
public:
    Cockpit(QWidget* parent, std::function<fs::path()> campaignDirectory, PlaytestEditor editor, FamilyActions familyActions)
        : QDialog(parent), campaignDirectory_(std::move(campaignDirectory)), editor_(std::move(editor))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName("projectCockpit");
        setWindowTitle(tr("Project Cockpit"));
        resize(750, 460);
        auto* layout = new QVBoxLayout(this);
        auto* fields = new QFormLayout;
        const auto pathField = [&](const QString& label, bool directory, bool projectInput = true)
        {
            auto* row = new QHBoxLayout;
            auto* edit = new QLineEdit(this);
            auto* browse = new QPushButton(tr("Browse…"), this);
            row->addWidget(edit);
            row->addWidget(browse);
            fields->addRow(label, row);
            connect(browse, &QPushButton::clicked, this, [this, directory, edit]
            {
                const auto name = directory ? QFileDialog::getExistingDirectory(this, tr("Stock installation root"), edit->text())
                    : QFileDialog::getOpenFileName(this, tr("Open project profile"), edit->text(), tr("JSON profiles (*.json)"));
                if ( !name.isEmpty() ) edit->setText(name);
            });
            connect(edit, &QLineEdit::textChanged, this, [this, projectInput]
            {
                if ( !projectInput ) return;
                revision_.clear();
                if ( scenarios_ ) scenarios_->clear();
                if ( test_ ) test_->setEnabled(false);
                if ( create_ ) create_->setEnabled(false);
                if ( tree_ ) tree_->clear();
            });
            return edit;
        };
        profile_ = pathField(tr("Project profile"), false);
        stock_ = pathField(tr("Local stock root"), true);
        machine_ = pathField(tr("Local engine capture request"), false, false);
        machine_->setObjectName("projectMachineRequest");
        machine_->setText(QSettings().value("project/localEngineCaptureRequest").toString());
        scenarios_ = new QComboBox(this);
        scenarios_->setObjectName("projectTestScenario");
        fields->addRow(tr("Test scenario"), scenarios_);
        profile_->setObjectName("projectProfilePath");
        stock_->setObjectName("projectStockRoot");
        layout->addLayout(fields);
        const auto directory = campaignDirectory_();
        if ( !directory.empty() )
            profile_->setText(QString::fromStdWString((directory / "red-project.json").wstring()));
        tree_ = new QTreeWidget(this);
        tree_->setHeaderLabels({tr("Project item"), tr("Source or status")});
        layout->addWidget(tree_, 1);
        layout->addWidget(createResourceFamilyBrowser(this, [this] {
            return std::pair{localPath(profile_->text()), localPath(stock_->text())};
        }, std::move(familyActions)), 2);
        status_ = new QLabel(this);
        status_->setObjectName("projectStatus");
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        layout->addWidget(status_);
        auto* buttons = new QHBoxLayout;
        prepare_ = new QPushButton(tr("Prepare"), this);
        create_ = new QPushButton(tr("Create Package…"), this);
        test_ = new QPushButton(tr("Test"), this);
        test_->setObjectName("projectRunTest");
        test_->setEnabled(false);
        prepare_->setObjectName("prepareProject");
        create_->setObjectName("createProjectPackage");
        auto* episode = new QPushButton(tr("Campaign Editor (F8)"), this);
        auto* close = new QPushButton(tr("Close"), this);
        buttons->addWidget(prepare_); buttons->addWidget(create_); buttons->addWidget(episode);
        buttons->addWidget(test_); buttons->addStretch(); buttons->addWidget(close);
        layout->addLayout(buttons);
        create_->setEnabled(false);
        connect(test_, &QPushButton::clicked, this, [this]
        {
            if ( machine_->text().isEmpty() ) { status_->setText(tr("Choose a local Engine Capture request.")); return; }
            QSettings().setValue("project/localEngineCaptureRequest", machine_->text());
            beginProjectPlaytest(this, status_, localPath(profile_->text()), localPath(stock_->text()),
                scenarios_->currentText(), machine_->text(), editor_);
        });
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        connect(prepare_, &QPushButton::clicked, this, [this] { work({}); });
        connect(create_, &QPushButton::clicked, this, [this]
        {
            const auto name = QFileDialog::getSaveFileName(this, tr("Create a new episode archive"), {},
                game_ == "mots" ? tr("MotS episode (*.goo)") : tr("JK episode (*.gob)"));
            if ( !name.isEmpty() ) work(localPath(name));
        });
        connect(episode, &QPushButton::clicked, this, [this, parent]
        {
            const auto here = campaignDirectory_();
            const auto file = localPath(profile_->text());
            std::error_code ec;
            if ( here.empty() || !fs::equivalent(here, file.parent_path(), ec) )
            {
                status_->setText(tr("Open a level from this campaign before using its F8 editor."));
                return;
            }
            if ( auto* action = parent->findChild<QAction*>("edit.episode_campaign_editor") ) action->trigger();
        });
    }

private:
    void work(const fs::path& destination)
    {
        const auto profile = localPath(profile_->text());
        const auto root = localPath(stock_->text());
        const auto expected = revision_;
        if ( profile.empty() ) { status_->setText(tr("Choose a RED project profile.")); return; }
        prepare_->setEnabled(false); create_->setEnabled(false); test_->setEnabled(false);
        status_->setText(tr("Checking the campaign and its resources…"));
        auto* watcher = new QFutureWatcher<std::shared_ptr<Result>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, profile, root, destination]
        {
            const auto result = watcher->result();
            watcher->deleteLater();
            prepare_->setEnabled(true);
            if ( localPath(profile_->text()) != profile || localPath(stock_->text()) != root )
            {
                status_->setText(tr("Project inputs changed. Prepare again."));
                return;
            }
            revision_.clear();
            if ( !result->error.isEmpty() ) { tree_->clear(); status_->setText(result->error); return; }
            try
            {
                if ( result->staged ) result->staged->publishNew(destination);
            }
            catch ( const std::exception& error ) { status_->setText(QString::fromUtf8(error.what())); return; }
            const auto& plan = result->project;
            revision_ = plan.revision; game_ = plan.target.game;
            tree_->clear();
            const auto row = [this](const std::string& key, const std::string& value)
            { return new QTreeWidgetItem(tree_, {QString::fromStdString(key), QString::fromStdString(value)}); };
            row(plan.target.game + " / " + plan.target.engine, plan.target.renderer + " | runtime untested by package preparation");
            for ( const auto& level : plan.levels ) row(level, "Campaign level");
            for ( const auto& resource : plan.resources )
            {
                auto* item = row(resource.name, resource.winner + (resource.suppliedByGame ? " (required stock)" : " (included)"));
                for ( const auto& other : resource.shadowed )
                    new QTreeWidgetItem(item, {tr("Shadowed"), QString::fromStdString(other)});
            }
            scenarios_->clear();
            for ( const auto& scenario : plan.scenarios )
            {
                scenarios_->addItem(QString::fromStdString(scenario.name));
                auto* item = row(scenario.name, scenario.level + " | " + scenario.definition);
                for ( const auto& receipt : scenario.receipts )
                    new QTreeWidgetItem(item, {tr("Declared receipt"), QString::fromStdString(receipt)});
            }
            for ( const auto& warning : plan.warnings ) row("Package warning", warning);
            tree_->resizeColumnToContents(0);
            status_->setText(destination.empty() ? tr("Prepared. Review the resource winners before creating the package.")
                : tr("Created %1").arg(QString::fromStdWString(destination.wstring())));
            create_->setEnabled(true);
            test_->setEnabled(scenarios_->count() > 0 && static_cast<bool>(editor_.source));
        });
        watcher->setFuture(detail::runPreviewWorkerTask([profile, root, destination, expected]
        {
            auto result = std::make_shared<Result>();
            try
            {
                auto& plan = result->project;
                plan = project::prepare(profile, root);
                if ( !destination.empty() )
                {
                    result->staged = project::stage(plan, expected, destination);
                }
                std::string{}.swap(plan.package);
            }
            catch ( const std::exception& e ) { result->error = QString::fromUtf8(e.what()); }
            return result;
        }));
    }

    std::function<fs::path()> campaignDirectory_;
    PlaytestEditor editor_;
    QLineEdit* machine_{};
    QComboBox* scenarios_{};
    QPushButton* test_{};
    QLineEdit* profile_{};
    QLineEdit* stock_{};
    QTreeWidget* tree_{};
    QLabel* status_{};
    QPushButton* prepare_{};
    QPushButton* create_{};
    std::string revision_;
    std::string game_;
};
} // namespace

void addProjectCockpit(QMenu* menu, QWidget* parent, std::function<fs::path()> campaignDirectory, PlaytestEditor editor, FamilyActions familyActions)
{
    auto* action = menu->addAction(QObject::tr("Project Cockpit…"));
    action->setObjectName("tools.project_cockpit");
    QObject::connect(action, &QAction::triggered, parent, [parent, campaignDirectory, editor, familyActions]
    { (new Cockpit(parent, campaignDirectory, editor, familyActions))->show(); });
}
} // namespace sed::ui
