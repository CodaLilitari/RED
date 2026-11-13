#include "installation_check_dialog.hpp"
#include "installation_check_controller.hpp"
#include "../../../../core/src/assets/atomic_file_write.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPointer>
#include <QUuid>
#include <QVBoxLayout>

namespace sed::ui::diagnostics
{
class InstallationCheckDialog::Impl
{
public:
    Impl(InstallationCheckDialog* owner, Context context)
        : owner_(owner), context_(std::move(context)), controller_(owner)
    {
        if ( context_.executable.isEmpty() )
        {
            context_.executable = QCoreApplication::applicationFilePath();
        }
        if ( context_.reportParentDirectory.isEmpty() )
        {
            context_.reportParentDirectory = QDir::tempPath();
        }
        owner_->setWindowTitle(QStringLiteral("Check Installation"));
        owner_->resize(640, 480);
        auto* layout = new QVBoxLayout(owner_);
        auto* explanation = new QLabel(QStringLiteral(
            "Check this installation in its own native editor window using synthetic geometry. "
            "Your current level and preferences are not used. This does not test a game engine."), owner_);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* executable = new QLineEdit(context_.executable, owner_);
        executable->setObjectName("installationExecutable");
        executable->setReadOnly(true);
        executable->setAccessibleName(QStringLiteral("Editor executable being checked"));
        layout->addWidget(executable);
        status_ = new QPlainTextEdit(owner_);
        status_->setObjectName("installationStatus");
        status_->setReadOnly(true);
        status_->setMinimumSize(360, 90);
        status_->setMaximumBlockCount(80);
        status_->setPlainText(QStringLiteral("Ready. The check has a 30-second deadline."));
        layout->addWidget(status_, 1);
        preview_ = new QLabel(QStringLiteral("The inspected synthetic frame appears here after a successful check."), owner_);
        preview_->setObjectName("installationPreview");
        preview_->setWordWrap(true);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setMinimumHeight(40);
        preview_->setMaximumHeight(160);
        preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(preview_);
        auto* buttons = new QHBoxLayout;
        run_ = new QPushButton(QStringLiteral("Run Check"), owner_);
        run_->setObjectName("installationRun");
        cancel_ = new QPushButton(QStringLiteral("Cancel Check"), owner_);
        cancel_->setObjectName("installationCancel");
        cancel_->setEnabled(false);
        save_ = new QPushButton(QStringLiteral("Save JSON Report…"), owner_);
        save_->setObjectName("installationSave");
        save_->setEnabled(false);
        auto* close = new QPushButton(QStringLiteral("Close"), owner_);
        buttons->addWidget(run_);
        buttons->addWidget(cancel_);
        buttons->addStretch();
        buttons->addWidget(save_);
        buttons->addWidget(close);
        layout->addLayout(buttons);
        QObject::connect(close, &QPushButton::clicked, owner_, &QDialog::reject);
        QObject::connect(cancel_, &QPushButton::clicked, &controller_, &InstallationCheckController::cancel);
        QObject::connect(run_, &QPushButton::clicked, owner_, [this]() { run(); });
        QObject::connect(save_, &QPushButton::clicked, owner_, [this]() { save(); });
        QObject::connect(&controller_, &InstallationCheckController::progress, owner_, [this](const QString& phase)
        {
            status_->appendPlainText(phase);
        });
        QObject::connect(&controller_, &InstallationCheckController::finished, owner_, [this](const QJsonObject& receipt)
        {
            run_->setEnabled(true);
            cancel_->setEnabled(false);
            save_->setEnabled(true);
            status_->appendPlainText(QStringLiteral("Result: %1\n%2\nReport directory: %3")
                .arg(receipt.value("status").toString(), receipt.value("error").toString(),
                    receipt.value("reportDirectory").toString()));
            const QJsonObject child = receipt.value("child").toObject();
            if ( receipt.value("nativeFramesAccepted") == true )
            {
                status_->appendPlainText(QStringLiteral("Native platform: %1\nOpenGL: %2")
                    .arg(child.value("nativePlatform").toString(), child.value("gl").toObject().value("renderer").toString()));
                showFrame(receipt);
            }
        });
    }

    void run()
    {
        const QString directory = QDir(context_.reportParentDirectory).filePath(
            QStringLiteral("red-installation-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        status_->clear();
        preview_->clear();
        run_->setEnabled(false);
        cancel_->setEnabled(true);
        save_->setEnabled(false);
        if ( !controller_.start(context_.executable, directory) )
        {
            status_->setPlainText(controller_.receipt().value("error").toString());
            run_->setEnabled(true);
            cancel_->setEnabled(false);
        }
    }

    void showFrame(const QJsonObject& receipt)
    {
        const auto frames = receipt.value("child").toObject().value("frames").toArray();
        if ( frames.size() != 2 )
        {
            return;
        }
        const auto record = frames[1].toObject();
        const QString path = receipt.value("reportDirectory").toString() + "/frame-after.png";
        const QFileInfo info(path);
        QFile file(path);
        if ( !info.isFile() || info.isSymLink() || info.size() > 8 * 1024 * 1024
            || !file.open(QIODevice::ReadOnly) )
        {
            status_->appendPlainText(QStringLiteral("The frame is unavailable for display; the recorded result is retained."));
            return;
        }
        const QByteArray bytes = file.read(8 * 1024 * 1024 + 1);
        if ( bytes.size() > 8 * 1024 * 1024
            || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                != record.value("sha256").toString() )
        {
            status_->appendPlainText(QStringLiteral("The frame changed after verification and is not displayed."));
            return;
        }
        QBuffer captured;
        captured.setData(bytes);
        captured.open(QIODevice::ReadOnly);
        QImageReader reader(&captured, "png");
        const QSize size = reader.size();
        if ( size.width() < 64 || size.height() < 64 || size.width() > 4096 || size.height() > 4096 )
        {
            return;
        }
        const QImage image = reader.read();
        if ( !image.isNull() )
        {
            preview_->setPixmap(QPixmap::fromImage(image).scaled(320, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    void save()
    {
        const QByteArray bytes = QJsonDocument(controller_.receipt()).toJson();
        const auto chooser = context_.selectJsonReportDestination;
        const QPointer<InstallationCheckDialog> guard(owner_);
        const QString destination = chooser
            ? chooser(owner_)
            : QFileDialog::getSaveFileName(owner_, QStringLiteral("Save Installation JSON Report"),
                QStringLiteral("red-installation-report.json"), QStringLiteral("JSON report (*.json)"),
                nullptr, QFileDialog::DontConfirmOverwrite);
        if ( !guard || closed_ || destination.isEmpty() )
        {
            return;
        }
        try
        {
            const auto path = std::filesystem::path(destination.toStdWString());
            sed::assets::detail::StagedAssetFile output(path);
            output.writeAndClose(std::as_bytes(std::span(bytes.constData(),
                static_cast<std::size_t>(bytes.size()))));
            output.publishNew(path);
        }
        catch ( const std::exception& error )
        {
            status_->appendPlainText(QStringLiteral("Report not saved: %1").arg(QString::fromUtf8(error.what())));
            return;
        }
        status_->appendPlainText(QStringLiteral("JSON report saved. Its original frame files remain in the report directory."));
    }

    InstallationCheckDialog* owner_{};
    Context context_{};
    InstallationCheckController controller_;
    bool closed_{};
    QPlainTextEdit* status_{};
    QLabel* preview_{};
    QPushButton* run_{};
    QPushButton* cancel_{};
    QPushButton* save_{};
};

InstallationCheckDialog::InstallationCheckDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(this, std::move(context)))
{
}
InstallationCheckDialog::~InstallationCheckDialog() = default;
void InstallationCheckDialog::done(const int result)
{
    impl_->closed_ = true;
    impl_->controller_.cancel();
    QDialog::done(result);
}
} // namespace sed::ui::diagnostics
