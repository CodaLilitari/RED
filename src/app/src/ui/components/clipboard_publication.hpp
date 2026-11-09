#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QMimeData;

namespace sed::ui
{

/** Final result of one system-clipboard publication. */
enum class ClipboardPublishOutcome : std::uint8_t
{
    published,
    unavailable,
    changedElsewhere,
};

/** One MIME format whose exact bytes must read back after publication. */
struct ClipboardFormatPayload
{
    QString mimeType{};
    QByteArray bytes{};
};

/**
 * Content published as one clipboard entry. Every format is verified by
 * reading it back; the plain-text summary is a convenience for other
 * applications and is not verified.
 */
struct ClipboardPublication
{
    std::vector<ClipboardFormatPayload> formats{};
    QString text{};
};

/** Clipboard primitives used by publication; tests inject deterministic fakes. */
struct ClipboardPublicationBackend
{
    /** Hands one freshly built entry to the clipboard, which owns it from then on. */
    std::function<void(std::unique_ptr<QMimeData>)> setMimeData{};
    /** Reads one format back as the clipboard currently presents it. */
    std::function<QByteArray(const QString&)> readFormat{};
    /** System clipboard change counter, or nullopt where the platform has none. */
    std::function<std::optional<std::uint32_t>()> changeCounter{};
    /** True while a window of another program has the clipboard open. */
    std::function<bool()> heldByAnotherWindow{};
    /**
     * Waits up to the given milliseconds while answering clipboard requests
     * that other programs send to this thread, such as rendering the entry
     * this application published last.
     */
    std::function<void(int)> serviceAndWait{};
};

/** Bounded wait and retry budget; the defaults keep a contended Copy under about a second. */
struct ClipboardPublicationPolicy
{
    int maxAttempts{ 3 };
    int retryWaitMs{ 50 };
    int maxHeldWaitMs{ 500 };
};

/** Returns the backend for the application's system clipboard. */
[[nodiscard]] ClipboardPublicationBackend systemClipboardPublicationBackend();

/** Returns the backend copy commands use: the system clipboard, or the test override. */
[[nodiscard]] ClipboardPublicationBackend activeClipboardPublicationBackend();

/** Replaces activeClipboardPublicationBackend() in tests; nullopt restores the system clipboard. */
void setClipboardPublicationBackendOverrideForTest(
    std::optional<ClipboardPublicationBackend> backend);

/**
 * Publishes one entry and reports success only after every verified format
 * reads back exactly.
 *
 * A program reading the previous entry can keep the clipboard open until
 * this thread renders that entry, and a write cannot succeed meanwhile. The
 * publication therefore answers such requests before writing, and between
 * attempts, within the policy budget. It never overwrites content that
 * another program published after a failed attempt.
 */
[[nodiscard]] ClipboardPublishOutcome publishClipboard(
    const ClipboardPublication& publication,
    const ClipboardPublicationBackend& backend,
    const ClipboardPublicationPolicy& policy = {});

/** Returns the user-facing explanation for an unsuccessful publication. */
[[nodiscard]] QString clipboardPublishFailureMessage(ClipboardPublishOutcome outcome);

} // namespace sed::ui
