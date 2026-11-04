#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QMimeData;

namespace sed::ui
{

/** Final result of one clipboard publication request. */
enum class ClipboardPublishOutcome : std::uint8_t
{
    Published,
    Unavailable,
    ChangedElsewhere,
    Superseded,
};

/** One MIME format whose exact bytes must be readable after publication. */
struct ClipboardFormatPayload
{
    QString mimeType{};
    QByteArray bytes{};
};

/**
 * Content published as one clipboard entry. Every format is verified by
 * reading it back; the plain-text summary is a convenience for other
 * applications and is not part of the verification.
 */
struct ClipboardPublication
{
    std::vector<ClipboardFormatPayload> formats{};
    QString text{};
};

/** Clipboard primitives used by the publisher; tests inject deterministic fakes. */
struct ClipboardPublisherBackend
{
    /** Hands one freshly built entry to the clipboard, which owns it from then on. */
    std::function<void(std::unique_ptr<QMimeData>)> setMimeData{};
    /** Reads one format back as the clipboard currently presents it. */
    std::function<QByteArray(const QString&)> readFormat{};
    /** System clipboard change counter, or nullopt when contention cannot be detected. */
    std::function<std::optional<std::uint32_t>()> changeCounter{};
};

/** Bounded retry budget used only while another program holds the clipboard. */
struct ClipboardRetryPolicy
{
    int maxAttempts{ 5 };
    int retryIntervalMs{ 200 };
};

/**
 * Returns the backend for the application clipboard. The change counter is
 * only available with the native Windows clipboard.
 */
[[nodiscard]] ClipboardPublisherBackend systemClipboardPublisherBackend();

/** Returns the user-facing explanation for an unsuccessful publication. */
[[nodiscard]] QString clipboardPublishFailureMessage(ClipboardPublishOutcome outcome);

/**
 * Publishes clipboard entries and reports success only after the published
 * bytes are read back. While another program holds the clipboard, attempts are
 * retried from the event loop within the retry policy; a newer request
 * supersedes pending retries and newer content from another program is never
 * overwritten. Destroying the publisher cancels a pending request without
 * invoking its completion.
 */
class VerifiedClipboardPublisher final : public QObject
{
public:
    using Completion = std::function<void(ClipboardPublishOutcome)>;

    explicit VerifiedClipboardPublisher(QObject* parent = nullptr);
    VerifiedClipboardPublisher(
        ClipboardPublisherBackend backend,
        ClipboardRetryPolicy policy,
        QObject* parent = nullptr);
    ~VerifiedClipboardPublisher() override;

    VerifiedClipboardPublisher(const VerifiedClipboardPublisher&) = delete;
    VerifiedClipboardPublisher& operator=(const VerifiedClipboardPublisher&) = delete;

    /** Returns the system-clipboard publisher shared by all copy commands of owner. */
    [[nodiscard]] static VerifiedClipboardPublisher* forOwner(QObject* owner);
    /** Returns owner's shared publisher without creating one. */
    [[nodiscard]] static VerifiedClipboardPublisher* findForOwner(const QObject* owner);

    /**
     * Starts publishing and invokes completion exactly once with the final
     * outcome; a pending earlier request completes as Superseded first. When
     * the outcome is known before returning, completion has already run and
     * the outcome is returned; otherwise nullopt is returned and completion
     * runs from the event loop.
     */
    std::optional<ClipboardPublishOutcome> publish(
        ClipboardPublication publication,
        Completion completion);

    [[nodiscard]] bool pending() const noexcept;
    /** Number of clipboard writes made for the current or most recent request. */
    [[nodiscard]] int attemptCount() const noexcept;
    [[nodiscard]] std::optional<ClipboardPublishOutcome> lastOutcome() const noexcept;
    [[nodiscard]] const ClipboardRetryPolicy& retryPolicy() const noexcept;

private:
    struct Request
    {
        ClipboardPublication publication{};
        Completion completion{};
        std::optional<std::uint32_t> counterAfterFailure{};
    };

    std::optional<ClipboardPublishOutcome> attempt();
    void retry();
    ClipboardPublishOutcome finish(ClipboardPublishOutcome outcome);
    [[nodiscard]] std::unique_ptr<QMimeData> buildMimeData() const;
    [[nodiscard]] bool readbackMatches() const;
    [[nodiscard]] std::optional<std::uint32_t> changeCounter() const;

    ClipboardPublisherBackend backend_{};
    ClipboardRetryPolicy policy_{};
    QTimer retryTimer_{};
    std::optional<Request> request_{};
    std::uint64_t generation_{};
    int attempts_{};
    std::optional<ClipboardPublishOutcome> lastOutcome_{};
};

} // namespace sed::ui
