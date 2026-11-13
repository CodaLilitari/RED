#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>

namespace sed::ui::diagnostics
{

/** Owns one admitted diagnostic child; never invokes a shell or a game. */
class InstallationCheckController final : public QObject
{
    Q_OBJECT

public:
    explicit InstallationCheckController(QObject* parent = nullptr);
    ~InstallationCheckController() override;
    bool start(const QString& executable, const QString& reportDirectory, int timeoutMs = 30000);
    void cancel();
    bool running() const;
    QJsonObject receipt() const;

Q_SIGNALS:
    void progress(const QString& phase);
    void finished(const QJsonObject& receipt);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sed::ui::diagnostics
