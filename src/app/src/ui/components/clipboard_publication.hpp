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

/** One private MIME format whose nonempty bytes must read back after publication. */
struct ClipboardFormatPayload
{
    QString mimeType{};
    QByteArray bytes{};
};

/**
 * Content published as one clipboard entry. Empty private payloads are
 * rejected because readFormat cannot distinguish absence from empty data.
 * Every format is verified by reading it back; multiple formats additionally
 * require an available generation counter that stays unchanged across the
 * reads. The plain-text summary is a convenience and is not verified.
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
     * Services sent messages while waiting for the given nominal interval,
     * including requests to render the entry this application published last.
     * On Windows all sent messages may dispatch, so callers must account for
     * reentrancy. A dispatched handler can outlast the requested interval.
     */
    std::function<void(int)> serviceAndWait{};
};

/** Finite retry count and nominal wait budget; backend calls can take additional time. */
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
 * reads back exactly, with no observed generation change during verification.
 *
 * A program reading the previous entry can keep the clipboard open until
 * this thread renders that entry, and a write cannot succeed meanwhile. The
 * publication therefore answers such requests before writing, and between
 * attempts, within the nominal policy budget. Where a change counter exists,
 * a failed verification followed by an observed change during that attempt or
 * its retry wait stops publication. A change cannot be attributed to a writer,
 * so an uncertain write of our own also stops retries. Counter checks and
 * writes are not atomic; changes racing after a check can still be overwritten.
 */
[[nodiscard]] ClipboardPublishOutcome publishClipboard(
    const ClipboardPublication& publication,
    const ClipboardPublicationBackend& backend,
    const ClipboardPublicationPolicy& policy = {});

/** Returns the user-facing explanation for an unsuccessful publication. */
[[nodiscard]] QString clipboardPublishFailureMessage(ClipboardPublishOutcome outcome);

} // namespace sed::ui
