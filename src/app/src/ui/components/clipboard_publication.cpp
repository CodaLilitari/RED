#include "ui/components/clipboard_publication.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QThread>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace sed::ui
{
namespace
{

std::optional<ClipboardPublicationBackend>& backendOverride()
{
    static std::optional<ClipboardPublicationBackend> override{};
    return override;
}

std::unique_ptr<QMimeData> buildMimeData(const ClipboardPublication& publication)
{
    auto mime = std::make_unique<QMimeData>();
    for ( const ClipboardFormatPayload& format : publication.formats )
    {
        mime->setData(format.mimeType, format.bytes);
    }
    if ( !publication.text.isEmpty() )
    {
        mime->setText(publication.text);
    }
    return mime;
}

#ifdef Q_OS_WIN
void serviceSentMessagesFor(const int milliseconds)
{
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(milliseconds, 0));
    for ( ;; )
    {
        MSG message{};
        // Peeking dispatches messages other threads sent to this thread's
        // windows, including WM_RENDERFORMAT for the entry published last.
        // Posted messages and user input stay queued.
        (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        const ULONGLONG now = GetTickCount64();
        if ( now >= deadline )
        {
            return;
        }
        (void)MsgWaitForMultipleObjectsEx(
            0, nullptr, static_cast<DWORD>(deadline - now), QS_SENDMESSAGE, 0);
    }
}
#endif

} // namespace

ClipboardPublicationBackend systemClipboardPublicationBackend()
{
    ClipboardPublicationBackend backend{};
    backend.setMimeData = [](std::unique_ptr<QMimeData> mime)
    {
        if ( QClipboard* const clipboard = QGuiApplication::clipboard() )
        {
            clipboard->setMimeData(mime.release());
        }
    };
    backend.readFormat = [](const QString& mimeType) -> QByteArray
    {
        const QClipboard* const clipboard = QGuiApplication::clipboard();
        const QMimeData* const mime = clipboard ? clipboard->mimeData() : nullptr;
        return mime ? mime->data(mimeType) : QByteArray{};
    };
#ifdef Q_OS_WIN
    backend.changeCounter = []() -> std::optional<std::uint32_t>
    {
        return static_cast<std::uint32_t>(GetClipboardSequenceNumber());
    };
    backend.heldByAnotherWindow = []()
    {
        const HWND open = GetOpenClipboardWindow();
        if ( !open )
        {
            return false;
        }
        DWORD processId = 0;
        (void)GetWindowThreadProcessId(open, &processId);
        return processId != GetCurrentProcessId();
    };
    backend.serviceAndWait = [](const int milliseconds)
    {
        serviceSentMessagesFor(milliseconds);
    };
#else
    backend.changeCounter = []() -> std::optional<std::uint32_t>
    {
        return std::nullopt;
    };
    backend.heldByAnotherWindow = []()
    {
        return false;
    };
    backend.serviceAndWait = [](const int milliseconds)
    {
        QThread::msleep(static_cast<unsigned long>(std::max(milliseconds, 0)));
    };
#endif
    return backend;
}

ClipboardPublicationBackend activeClipboardPublicationBackend()
{
    const std::optional<ClipboardPublicationBackend>& override = backendOverride();
    return override ? *override : systemClipboardPublicationBackend();
}

void setClipboardPublicationBackendOverrideForTest(
    std::optional<ClipboardPublicationBackend> backend)
{
    backendOverride() = std::move(backend);
}

ClipboardPublishOutcome publishClipboard(
    const ClipboardPublication& publication,
    const ClipboardPublicationBackend& backend,
    const ClipboardPublicationPolicy& policy)
{
    if ( !backend.setMimeData || !backend.readFormat || publication.formats.empty() )
    {
        return ClipboardPublishOutcome::unavailable;
    }

    const int retryWaitMs = std::max(policy.retryWaitMs, 0);
    const auto serviceAndWait = [&backend](const int milliseconds)
    {
        if ( backend.serviceAndWait )
        {
            backend.serviceAndWait(milliseconds);
        }
    };
    const auto changeCounter = [&backend]() -> std::optional<std::uint32_t>
    {
        return backend.changeCounter ? backend.changeCounter() : std::nullopt;
    };

    // A program reading the previous entry keeps the clipboard open until
    // this thread renders that entry. Answer it while that entry still
    // exists: the write below replaces it, and a write cannot succeed while
    // the clipboard is open elsewhere.
    int heldWaitMs = 0;
    while ( backend.heldByAnotherWindow && backend.heldByAnotherWindow()
        && heldWaitMs < policy.maxHeldWaitMs )
    {
        serviceAndWait(retryWaitMs);
        heldWaitMs += std::max(retryWaitMs, 1);
    }

    std::optional<std::uint32_t> counterAfterFailure{};
    const int attempts = std::max(policy.maxAttempts, 1);
    for ( int attempt = 1; attempt <= attempts; ++attempt )
    {
        if ( counterAfterFailure )
        {
            // A failed write leaves the counter unchanged, so a change means
            // another program published newer content. Keep it.
            const std::optional<std::uint32_t> counter = changeCounter();
            if ( counter && *counter != *counterAfterFailure )
            {
                return ClipboardPublishOutcome::changedElsewhere;
            }
        }

        backend.setMimeData(buildMimeData(publication));
        const bool readsBack = std::all_of(
            publication.formats.begin(),
            publication.formats.end(),
            [&backend](const ClipboardFormatPayload& format)
            {
                return backend.readFormat(format.mimeType) == format.bytes;
            });
        if ( readsBack )
        {
            return ClipboardPublishOutcome::published;
        }

        counterAfterFailure = changeCounter();
        if ( attempt < attempts )
        {
            serviceAndWait(retryWaitMs);
        }
    }
    return ClipboardPublishOutcome::unavailable;
}

QString clipboardPublishFailureMessage(const ClipboardPublishOutcome outcome)
{
    switch ( outcome )
    {
        case ClipboardPublishOutcome::published:
            return {};
        case ClipboardPublishOutcome::unavailable:
            return QStringLiteral("another program is holding the clipboard. Try again.");
        case ClipboardPublishOutcome::changedElsewhere:
            return QStringLiteral("another program changed the clipboard first. Try again.");
    }
    return {};
}

} // namespace sed::ui
