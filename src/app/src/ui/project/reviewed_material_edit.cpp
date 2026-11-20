#include "ui/project/reviewed_material_edit.hpp"

#include "agent_json.hpp"
#include "sed/assets/current_archive_snapshot.hpp"
#include "ui/components/preview_resource_revision_utils.hpp"

#include <QAction>
#include <QCryptographicHash>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

namespace sed::ui
{
namespace
{
constexpr int planByteLimit = 64 * 1024;
constexpr std::size_t sessionRecordLimit = 1024;

struct Proposal
{
    QString id;
    level::SurfaceMaterialEdit edit;
};

QString readId(const sed_agent::Json& root)
{
    const auto* value = root.find("operationId");
    if (!value || value->type != sed_agent::Json::Type::String)
        throw std::runtime_error("The plan needs an operationId.");
    const QString id = QString::fromStdString(value->str);
    static const QRegularExpression valid(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z"));
    if (!valid.match(id).hasMatch())
        throw std::runtime_error("Use 1 to 128 letters, digits, dots, underscores or hyphens for operationId.");
    return id;
}

sed_agent::Json readJson(const QString& text)
{
    if (text.size() > planByteLimit || text.toUtf8().size() > planByteLimit)
        throw std::runtime_error("The plan exceeds 64 KiB.");
    return sed_agent::parseJson(text.toStdString());
}

int readIndex(const sed_agent::Json& object, const char* name)
{
    const auto* value = object.find(name);
    if (!value || value->type != sed_agent::Json::Type::Number || !std::isfinite(value->number)
        || value->number < 0 || value->number > std::numeric_limits<int>::max()
        || std::floor(value->number) != value->number)
        throw std::runtime_error(std::string(name) + " must be a nonnegative integer.");
    return static_cast<int>(value->number);
}

Proposal readProposal(const QString& text)
{
    const auto root = readJson(text);
    const QString id = readId(root);
    const auto* ops = root.find("operations");
    if (root.object.size() != 2 || !ops || ops->type != sed_agent::Json::Type::Array || ops->array.size() != 1)
        throw std::runtime_error("Use only operationId and operations, with exactly one native edit.");
    const auto& op = ops->array.front();
    const auto* type = op.find("op");
    if (!op.isObject() || op.object.size() != 4 || !type || type->type != sed_agent::Json::Type::String
        || type->str != "setSurfaceMaterial")
        throw std::runtime_error("Use setSurfaceMaterial with sector, surface and materialIndex.");
    return {id, {readIndex(op, "sector"), readIndex(op, "surface"), readIndex(op, "materialIndex")}};
}

struct Entry
{
    level::SurfaceMaterialEdit edit;
    MaterialReviewSnapshot snapshot;
    QString state;
    QString message;
};

struct Session
{
    MaterialReviewHost host;
    std::map<QString, Entry> entries;
};

class ReviewDialog final : public QDialog
{
public:
    ReviewDialog(QWidget* owner, std::shared_ptr<Session> session)
        : QDialog(owner), session_(std::move(session))
    {
        setObjectName(QStringLiteral("agentReview.dialog"));
        setWindowTitle(tr("Review Agent Edit"));
        setAttribute(Qt::WA_DeleteOnClose);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr("Open a plan file or paste a surface-material proposal for the current level."), this));
        plan_ = new QPlainTextEdit(this);
        plan_->setObjectName(QStringLiteral("agentReview.plan"));
        plan_->setAccessibleName(tr("Agent plan JSON"));
        plan_->setPlaceholderText(QStringLiteral(R"({"operationId":"edit-1","operations":[{"op":"setSurfaceMaterial","sector":0,"surface":0,"materialIndex":1}]})"));
        layout->addWidget(plan_);
        auto* input = new QHBoxLayout;
        layout->addLayout(input);
        open_ = addButton(input, tr("Open Plan…"), "open", [this] { openPlan(); });
        prepare_ = addButton(input, tr("Review"), "prepare", [this] { prepare(); });
        addButton(input, tr("Query Outcome"), "query", [this] { query(); });
        input->addStretch();
        status_ = new QLabel(this);
        status_->setObjectName(QStringLiteral("agentReview.status"));
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(status_);
        auto* buttons = new QHBoxLayout;
        layout->addLayout(buttons);
        buttons->addStretch();
        accept_ = addButton(buttons, tr("Apply Edit"), "accept", [this] { publish(); });
        cancel_ = addButton(buttons, tr("Cancel Proposal"), "cancel", [this] { cancelProposal(); });
        addButton(buttons, tr("Close"), "close", [this] { close(); });
        display(QStringLiteral("idle"), tr("Review checks the open document. Outcomes are kept for this editor window’s session."));
        resize(640, 340);
    }

protected:
    void done(int result) override
    {
        cancelProposal();
        QDialog::done(result);
    }

private:
    template<class Fn>
    QPushButton* addButton(QHBoxLayout* row, const QString& text, const char* id, Fn callback)
    {
        auto* button = new QPushButton(text, this);
        button->setObjectName(QStringLiteral("agentReview.") + QString::fromLatin1(id));
        button->setAutoDefault(false);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, this, std::move(callback));
        return button;
    }

    void display(const QString& state, const QString& message)
    {
        status_->setProperty("outcome", state);
        status_->setText(message);
        const bool actionable = !pending_.isEmpty() && state == QStringLiteral("reviewed");
        accept_->setEnabled(actionable);
        cancel_->setEnabled(actionable);
        plan_->setReadOnly(actionable);
        open_->setEnabled(!actionable);
        prepare_->setEnabled(!actionable);
    }

    void showEntry(const QString& id, const Entry& entry)
    {
        if (entry.state != QStringLiteral("reviewed")) pending_.clear();
        display(entry.state, id + QStringLiteral(" — ") + entry.message);
    }

    void openPlan()
    {
        const QString filename = QFileDialog::getOpenFileName(this, tr("Open Agent Plan"), {}, tr("JSON plans (*.json);;All files (*)"));
        if (filename.isEmpty()) return;
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly))
            return display(QStringLiteral("invalid"), file.errorString());
        const auto bytes = file.read(planByteLimit + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > planByteLimit)
            return display(QStringLiteral("invalid"), tr("Could not read a plan within 64 KiB."));
        plan_->setPlainText(QString::fromUtf8(bytes));
        prepare();
    }

    void prepare()
    {
        try
        {
            const Proposal p = readProposal(plan_->toPlainText());
            auto prior = session_->entries.find(p.id);
            if (prior != session_->entries.end())
            {
                if (prior->second.edit != p.edit)
                    return display(QStringLiteral("conflict"), tr("This operation ID belongs to a different edit."));
                if (prior->second.state == QStringLiteral("reviewed")) pending_ = p.id;
                return showEntry(p.id, prior->second);
            }
            if (session_->entries.size() >= sessionRecordLimit)
                return display(QStringLiteral("invalid"), tr("This editor session has reached its operation record limit."));
            QString error;
            auto observed = session_->host.observe(p.edit, error);
            if (!observed) return display(QStringLiteral("invalid"), error);
            const QString preview = tr("Sector %1, surface %2\nCurrent material [%3] %4\nProposed material [%5] %6")
                .arg(QString::number(p.edit.sector), QString::number(p.edit.surface),
                    QString::number(observed->oldSlot), observed->oldName,
                    QString::number(p.edit.materialIndex), observed->newName);
            auto row = session_->entries.emplace(p.id, Entry{p.edit, std::move(*observed), QStringLiteral("reviewed"), preview}).first;
            pending_ = p.id;
            showEntry(p.id, row->second);
        }
        catch (const std::exception& e) { display(QStringLiteral("invalid"), QString::fromUtf8(e.what())); }
    }

    void query()
    {
        try
        {
            const auto input = readJson(plan_->toPlainText());
            const QString id = readId(input);
            std::optional<Proposal> request;
            if (input.object.size() != 1) request = readProposal(plan_->toPlainText());
            const auto found = session_->entries.find(id);
            if (found == session_->entries.end())
                return display(QStringLiteral("unknown"), tr("No operation with that ID is recorded in this editor session."));
            if (request && request->edit != found->second.edit)
                return display(QStringLiteral("conflict"), tr("This operation ID belongs to a different edit."));
            showEntry(id, found->second);
        }
        catch (const std::exception& e) { display(QStringLiteral("invalid"), QString::fromUtf8(e.what())); }
    }

    void cancelProposal()
    {
        if (pending_.isEmpty()) return;
        const QString id = pending_;
        Entry& entry = session_->entries.at(id);
        if (entry.state == QStringLiteral("reviewed"))
        {
            entry.state = QStringLiteral("cancelled");
            entry.message = tr("Cancelled. The document was not changed.");
        }
        showEntry(id, entry);
    }

    void publish()
    {
        if (pending_.isEmpty()) return;
        const QString id = pending_;
        Entry& entry = session_->entries.at(id);
        if (entry.state != QStringLiteral("reviewed")) return showEntry(id, entry);
        try
        {
            QString error;
            const auto now = session_->host.observe(entry.edit, error);
            if (!now || *now != entry.snapshot)
            {
                entry.state = QStringLiteral("stale");
                entry.message = error.isEmpty() ? tr("The document, draft or material changed. Review a new operation ID.") : error;
            }
            else if (session_->host.apply(entry.edit))
            {
                entry.state = QStringLiteral("applied");
                entry.message = tr("Applied. Use Edit > Undo to restore the previous material.");
            }
            else
            {
                entry.state = QStringLiteral("unchanged");
                entry.message = tr("The surface already has the proposed material. No history entry was added.");
            }
        }
        catch (const std::exception& e)
        {
            entry.state = QStringLiteral("invalid");
            entry.message = QString::fromUtf8(e.what());
        }
        const bool applied = entry.state == QStringLiteral("applied");
        showEntry(id, entry);
        if (applied) session_->host.refresh();
    }

    std::shared_ptr<Session> session_;
    QString pending_;
    QPlainTextEdit* plan_{};
    QLabel* status_{};
    QPushButton* open_{};
    QPushButton* prepare_{};
    QPushButton* accept_{};
    QPushButton* cancel_{};
};
} // namespace

bool captureReviewedMaterial(const assets::ResourceLocator& locator, const QString& name,
    MaterialReviewSnapshot& snapshot, QString& error)
{
    const std::string raw = name.toStdString();
    const auto trimmed = sed::text::trimView(raw);
    const QString relative = QString::fromUtf8(trimmed.data(), static_cast<qsizetype>(trimmed.size())).replace('\\', '/');
    const QStringList components = relative.split('/');
    const QString leaf = components.last();
    if (relative == QStringLiteral("-") || components.contains(QString{}) || components.contains(QStringLiteral("."))
        || components.contains(QStringLiteral("..")) || relative.contains(':') || relative.contains(QChar::Null))
    {
        error = QObject::tr("The material must identify a project MAT resource.");
        return false;
    }
    const auto current = assets::currentArchiveSnapshot(locator);
    std::string lookup = relative.toStdString();
    constexpr std::size_t maxBytes = 16u << 20;
    bool over = false;
    auto bytes = current.tryReadBinary(lookup, maxBytes, over);
    if (!bytes && !over && !leaf.endsWith(QStringLiteral(".mat"), Qt::CaseInsensitive))
    {
        lookup += ".mat";
        bytes = current.tryReadBinary(lookup, maxBytes, over);
    }
    if (!bytes)
    {
        error = over ? QObject::tr("The proposed MAT exceeds the 16 MiB review limit.")
                     : QObject::tr("The proposed material is missing from the project resources.");
        return false;
    }
    snapshot.materialHash = QCryptographicHash::hash(QByteArrayView(
        reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size())), QCryptographicHash::Sha256);
    if (const auto overrideRevision = current.primaryResourceOverrideRevision(lookup))
        snapshot.materialRevision = QStringLiteral("override:%1").arg(*overrideRevision);
    else if (const auto disk = current.findSelectedOnDisk(lookup))
        snapshot.materialRevision = QString::fromStdString(detail::previewFileRevisionToken("file", *disk));
    else
    {
        // The bounded read found an archive entry. Identify the actual winner
        // on the refreshed index, including its mount and case-preserved name.
        for (const auto& candidate : current.candidates(lookup))
        {
            for (auto archive = current.gobs().rbegin(); archive != current.gobs().rend(); ++archive)
            {
                if (const auto entry = archive->find(candidate))
                {
                    snapshot.materialRevision = QString::fromStdString(
                        detail::previewFileRevisionToken("archive", archive->path()) + "|" + entry->name)
                        + QStringLiteral("|%1|%2").arg(entry->offset).arg(entry->size);
                    return true;
                }
            }
        }
        error = QObject::tr("The material source changed during review.");
        return false;
    }
    return true;
}

void addReviewedMaterialEdit(QMenu* menu, QWidget* owner, MaterialReviewHost host)
{
    auto session = std::make_shared<Session>(Session{std::move(host), {}});
    auto* action = menu->addAction(QObject::tr("Review Agent Edit…"));
    action->setObjectName(QStringLiteral("tools.review_agent_edit"));
    QObject::connect(action, &QAction::triggered, owner, [owner, session]
    {
        (new ReviewDialog(owner, session))->show();
    });
}

} // namespace sed::ui
