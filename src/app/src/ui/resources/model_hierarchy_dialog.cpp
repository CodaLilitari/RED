#include "ui/resources/model_hierarchy_dialog.hpp"
#include "ui/resources/model_pose_preview.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <fstream>
#include <sstream>

namespace sed::ui::resources
{
namespace
{

QString issuesText(const std::vector<assets::HierarchyIssue>& issues, const QString& clean)
{
    if (issues.empty()) return clean;
    QStringList lines;
    for (const auto& issue : issues)
        lines << QStringLiteral("%1 (%2): %3").arg(QString::fromStdString(issue.severity), QString::fromStdString(issue.code),
                     QString::fromStdString(issue.message));
    return lines.join('\n');
}

QString writeFresh(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return QStringLiteral("%1 already exists; choose a new file.").arg(QString::fromStdString(path.string()));
    std::ofstream out(path, std::ios::binary);
    out << text;
    return out ? QString() : QStringLiteral("Couldn't write %1.").arg(QString::fromStdString(path.string()));
}

QString vectorText(const TVector& v)
{
    return QStringLiteral("%1 / %2 / %3").arg(v.x).arg(v.y).arg(v.z);
}

} // namespace

ModelHierarchyDialog::ModelHierarchyDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("modelHierarchyDialog"));
    setWindowTitle(QStringLiteral("3DO Hierarchy"));
    auto* const layout = new QVBoxLayout(this);
    auto* const top = new QHBoxLayout();
    auto* const open = new QPushButton(QStringLiteral("Open 3DO…"), this);
    open->setObjectName(QStringLiteral("modelHierarchy.open"));
    top->addWidget(open);
    top->addStretch();
    layout->addLayout(top);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("modelHierarchy.tree"));
    tree_->setHeaderLabels({ QStringLiteral("Node"), QStringLiteral("Mesh"), QStringLiteral("Position"), QStringLiteral("Rotation"),
        QStringLiteral("Pivot") });
    tree_->header()->setStretchLastSection(false);
    layout->addWidget(tree_, 1);

    auto* const form = new QFormLayout();
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("modelHierarchy.name"));
    parent_ = new QComboBox(this);
    parent_->setObjectName(QStringLiteral("modelHierarchy.parent"));
    form->addRow(QStringLiteral("Name:"), name_);
    form->addRow(QStringLiteral("Parent:"), parent_);
    const char* rows[] = { "Position:", "Rotation (pitch/yaw/roll):", "Pivot:" };
    for (int r = 0; r < 3; ++r)
    {
        auto* const row = new QHBoxLayout();
        for (int c = 0; c < 3; ++c)
        {
            auto* const box = new QDoubleSpinBox(this);
            box->setObjectName(QStringLiteral("modelHierarchy.t%1").arg(r * 3 + c));
            box->setDecimals(r == 1 ? 3 : 6);
            box->setRange(-100000.0, 100000.0);
            transform_[static_cast<std::size_t>(r * 3 + c)] = box;
            row->addWidget(box);
        }
        form->addRow(QString::fromLatin1(rows[r]), row);
    }
    layout->addLayout(form);

    auto* const actions = new QHBoxLayout();
    const auto button = [&](const QString& text, const char* objectName) {
        auto* const b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(objectName));
        actions->addWidget(b);
        return b;
    };
    QPushButton* const apply = button(QStringLiteral("Apply to node"), "modelHierarchy.apply");
    QPushButton* const add = button(QStringLiteral("Add child…"), "modelHierarchy.add");
    QPushButton* const remove = button(QStringLiteral("Remove node"), "modelHierarchy.remove");
    QPushButton* const save = button(QStringLiteral("Save as…"), "modelHierarchy.save");
    QPushButton* const newKey = button(QStringLiteral("New KEY…"), "modelHierarchy.newKey");
    QPushButton* const checkKeyButton = button(QStringLiteral("Check KEY…"), "modelHierarchy.checkKey");
    QPushButton* const preview = button(QStringLiteral("Preview KEY…"), "modelHierarchy.previewKey");
    layout->addLayout(actions);

    report_ = new QPlainTextEdit(this);
    report_->setObjectName(QStringLiteral("modelHierarchy.report"));
    report_->setReadOnly(true);
    report_->setMaximumHeight(140);
    layout->addWidget(report_);
    auto* const close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(open, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open 3DO"), QString(), QStringLiteral("3DO models (*.3do)"));
        if (!file.isEmpty()) (void)loadModel(file.toStdString());
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item) showNode(item->data(0, Qt::UserRole).toInt());
    });
    connect(apply, &QPushButton::clicked, this, [this]() {
        const QString problem = applyNodeEdits();
        if (!problem.isEmpty()) report(problem);
    });
    connect(add, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Add child"), QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok);
        if (ok)
        {
            const QString problem = addChild(name);
            if (!problem.isEmpty()) report(problem);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const QString problem = removeSelected();
        if (!problem.isEmpty()) report(problem);
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save 3DO as"), QString(), QStringLiteral("3DO models (*.3do)"));
        if (!file.isEmpty()) report(saveAs(file.toStdString()));
    });
    connect(newKey, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const int frames = QInputDialog::getInt(this, QStringLiteral("New KEY"), QStringLiteral("Frames:"), 30, 1, 100000, 1, &ok);
        if (!ok) return;
        const double fps = QInputDialog::getDouble(this, QStringLiteral("New KEY"), QStringLiteral("Frames per second:"), 15.0, 1.0, 1000.0, 3, &ok);
        if (!ok) return;
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save KEY as"), QString(), QStringLiteral("KEY files (*.key)"));
        if (!file.isEmpty()) report(writeKeyFromModel(file.toStdString(), frames, fps));
    });
    connect(checkKeyButton, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Check KEY"), QString(), QStringLiteral("KEY files (*.key)"));
        if (!file.isEmpty()) report(checkKey(file.toStdString()));
    });
    connect(preview, &QPushButton::clicked, this, [this]() {
        if (path_.empty()) { report(QStringLiteral("Open the 3DO hierarchy to preview first.")); return; }
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("Preview KEY on this hierarchy"), {}, QStringLiteral("KEY files (*.key)"));
        if (file.isEmpty()) return;
        try
        {
            auto* dialog = createModelPosePreview(this, path_, model_, file.toStdString());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->open();
        }
        catch (const std::exception& e) { report(QString::fromUtf8(e.what())); }
    });
    resize(820, 640);
}

bool ModelHierarchyDialog::loadModel(const std::filesystem::path& path)
{
    try
    {
        assets::Model3doLoadOptions raw;
        raw.applyHierarchyTransforms = false;
        model_ = assets::loadFrom3DO(path, raw);
    }
    catch (const std::exception& e)
    {
        report(QStringLiteral("Couldn't open %1: %2").arg(QString::fromStdString(path.string()), QString::fromUtf8(e.what())));
        return false;
    }
    path_ = path;
    selected_ = -1;
    rebuild();
    report(issuesText(assets::validateModelHierarchy(model_), QStringLiteral("%1: %2 nodes, no problems.")
                                                                   .arg(QString::fromStdString(path.filename().string()))
                                                                   .arg(model_.nodes.size())));
    return true;
}

void ModelHierarchyDialog::rebuild()
{
    tree_->clear();
    std::vector<QTreeWidgetItem*> items(model_.nodes.size(), nullptr);
    for (const auto& row : assets::modelHierarchyRows(model_))
    {
        const auto& node = model_.nodes[static_cast<std::size_t>(row.node)];
        const QStringList columns{ QStringLiteral("%1: %2").arg(row.node).arg(QString::fromStdString(node.name)),
            node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < model_.meshes.size()
                ? QString::fromStdString(model_.meshes[static_cast<std::size_t>(node.mesh)].name)
                : QStringLiteral("—"),
            vectorText(node.position), vectorText(node.rotation), vectorText(node.pivot) };
        QTreeWidgetItem* const parent = node.parent >= 0 && static_cast<std::size_t>(node.parent) < items.size()
            ? items[static_cast<std::size_t>(node.parent)] : nullptr;
        auto* const item = parent ? new QTreeWidgetItem(parent, columns) : new QTreeWidgetItem(tree_, columns);
        item->setData(0, Qt::UserRole, row.node);
        items[static_cast<std::size_t>(row.node)] = item;
    }
    tree_->expandAll();
    parent_->clear();
    for (std::size_t i = 0; i < model_.nodes.size(); ++i)
        parent_->addItem(QStringLiteral("%1: %2").arg(i).arg(QString::fromStdString(model_.nodes[i].name)), static_cast<int>(i));
    if (selected_ >= 0 && static_cast<std::size_t>(selected_) < items.size() && items[static_cast<std::size_t>(selected_)])
        tree_->setCurrentItem(items[static_cast<std::size_t>(selected_)]);
}

void ModelHierarchyDialog::selectNode(const std::int32_t node)
{
    selected_ = node;
    rebuild();
    showNode(node);
}

void ModelHierarchyDialog::showNode(const std::int32_t node)
{
    if (node < 0 || static_cast<std::size_t>(node) >= model_.nodes.size()) return;
    selected_ = node;
    const auto& n = model_.nodes[static_cast<std::size_t>(node)];
    name_->setText(QString::fromStdString(n.name));
    parent_->setEnabled(node != 0);
    parent_->setCurrentIndex(n.parent >= 0 ? n.parent : 0);
    const TVector* vectors[] = { &n.position, &n.rotation, &n.pivot };
    for (int r = 0; r < 3; ++r)
    {
        transform_[static_cast<std::size_t>(r * 3)]->setValue(vectors[r]->x);
        transform_[static_cast<std::size_t>(r * 3 + 1)]->setValue(vectors[r]->y);
        transform_[static_cast<std::size_t>(r * 3 + 2)]->setValue(vectors[r]->z);
    }
}

QString ModelHierarchyDialog::applyNodeEdits()
{
    if (selected_ < 0) return QStringLiteral("Select a node first.");
    assets::Model3do edited = model_;
    const auto value = [&](const int i) { return transform_[static_cast<std::size_t>(i)]->value(); };
    std::optional<assets::HierarchyIssue> problem = assets::renameModelNode(edited, selected_, name_->text().trimmed().toStdString());
    if (!problem && selected_ != 0 && parent_->currentData().toInt() != edited.nodes[static_cast<std::size_t>(selected_)].parent)
        problem = assets::reparentModelNode(edited, selected_, parent_->currentData().toInt());
    if (!problem)
        problem = assets::setModelNodeTransform(edited, selected_, TVector{ value(0), value(1), value(2) },
            TVector{ value(3), value(4), value(5) }, TVector{ value(6), value(7), value(8) });
    if (problem) return QString::fromStdString(problem->message);
    model_ = std::move(edited);
    rebuild();
    report(issuesText(assets::validateModelHierarchy(model_), QStringLiteral("Node %1 updated; no problems.").arg(selected_)));
    return QString();
}

QString ModelHierarchyDialog::addChild(const QString& name)
{
    if (selected_ < 0) return QStringLiteral("Select the parent node first.");
    std::int32_t added = -1;
    if (auto problem = assets::addModelNode(model_, selected_, -1, name.trimmed().toStdString(), &added))
        return QString::fromStdString(problem->message);
    selectNode(added);
    report(QStringLiteral("Added node %1 under node %2.").arg(added).arg(model_.nodes[static_cast<std::size_t>(added)].parent));
    return QString();
}

QString ModelHierarchyDialog::removeSelected()
{
    if (selected_ < 0) return QStringLiteral("Select a node first.");
    if (auto problem = assets::removeModelNode(model_, selected_)) return QString::fromStdString(problem->message);
    selected_ = -1;
    rebuild();
    report(QStringLiteral("Node removed; the nodes after it are renumbered."));
    return QString();
}

QString ModelHierarchyDialog::saveAs(const std::filesystem::path& path) const
{
    for (const auto& issue : assets::validateModelHierarchy(model_))
    {
        if (issue.severity == "error") return QStringLiteral("Not saved: %1").arg(QString::fromStdString(issue.message));
    }
    std::string text;
    try
    {
        text = assets::saveTo3DO(model_);
    }
    catch (const std::exception& e)
    {
        return QStringLiteral("Not saved: %1").arg(QString::fromUtf8(e.what()));
    }
    const QString problem = writeFresh(path, text);
    return problem.isEmpty() ? QStringLiteral("Saved %1.").arg(QString::fromStdString(path.string())) : problem;
}

QString ModelHierarchyDialog::writeKeyFromModel(const std::filesystem::path& path, const std::int32_t frames, const double fps) const
{
    if (model_.nodes.empty()) return QStringLiteral("Open a model with a hierarchy first.");
    const QString problem = writeFresh(path, assets::saveToKEYText(assets::keyFromModel(model_, frames, fps)));
    return problem.isEmpty()
        ? QStringLiteral("Wrote %1: %2 tracks at their rest pose. Add entries in the KEY editor, then Check KEY to fix the deltas.")
              .arg(QString::fromStdString(path.string()))
              .arg(model_.nodes.size())
        : problem;
}

QString ModelHierarchyDialog::checkKey(const std::filesystem::path& keyPath, const std::optional<std::filesystem::path>& fixedPath)
{
    assets::KeyFile key;
    try
    {
        key = assets::loadFromKEY(keyPath);
    }
    catch (const std::exception& e)
    {
        return QStringLiteral("Couldn't open %1: %2").arg(QString::fromStdString(keyPath.string()), QString::fromUtf8(e.what()));
    }
    const auto issues = assets::validateKeyForModel(key, model_);
    QString text = issuesText(issues, QStringLiteral("%1 matches the model.").arg(QString::fromStdString(keyPath.filename().string())));
    const bool stale = std::any_of(issues.begin(), issues.end(), [](const auto& i) { return i.code == "staleDeltas"; });
    std::optional<std::filesystem::path> target = fixedPath;
    if (stale && !target && isVisible())
    {
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save the KEY with recomputed deltas as"), QString(),
            QStringLiteral("KEY files (*.key)"));
        if (!file.isEmpty()) target = file.toStdString();
    }
    if (stale && target)
    {
        (void)assets::keyComputeDeltas(key);
        const QString problem = writeFresh(*target, assets::saveToKEYText(key));
        text += QStringLiteral("\n") + (problem.isEmpty() ? QStringLiteral("Wrote %1 with recomputed deltas.").arg(QString::fromStdString(target->string())) : problem);
    }
    return text;
}

void ModelHierarchyDialog::report(const QString& text)
{
    report_->setPlainText(text);
}

QString ModelHierarchyDialog::reportText() const
{
    return report_->toPlainText();
}

} // namespace sed::ui::resources
