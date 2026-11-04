#include "ui/components/verified_clipboard_publisher.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include <utility>

// RED-PHASE STUB: legacy behavior (one unverified write reported as success).
// Replaced by the verified implementation in the green phase.

namespace sed::ui
{
namespace
{

constexpr auto kPublisherObjectName = "sedVerifiedClipboardPublisher";

} // namespace

ClipboardPublisherBackend systemClipboardPublisherBackend()
{
    ClipboardPublisherBackend backend{};
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
    backend.changeCounter = []() -> std::optional<std::uint32_t>
    {
        return std::nullopt;
    };
    return backend;
}

QString clipboardPublishFailureMessage(const ClipboardPublishOutcome)
{
    return {};
}

VerifiedClipboardPublisher::VerifiedClipboardPublisher(QObject* const parent)
    : VerifiedClipboardPublisher(
        systemClipboardPublisherBackend(),
        ClipboardRetryPolicy{},
        parent)
{
}

VerifiedClipboardPublisher::VerifiedClipboardPublisher(
    ClipboardPublisherBackend backend,
    const ClipboardRetryPolicy policy,
    QObject* const parent)
    : QObject(parent)
    , backend_(std::move(backend))
    , policy_(policy)
{
    retryTimer_.setSingleShot(true);
}

VerifiedClipboardPublisher::~VerifiedClipboardPublisher() = default;

VerifiedClipboardPublisher* VerifiedClipboardPublisher::forOwner(QObject* const owner)
{
    if ( !owner )
    {
        return nullptr;
    }
    if ( VerifiedClipboardPublisher* const existing = findForOwner(owner) )
    {
        return existing;
    }
    auto* const publisher = new VerifiedClipboardPublisher(owner);
    publisher->setObjectName(QString::fromLatin1(kPublisherObjectName));
    return publisher;
}

VerifiedClipboardPublisher* VerifiedClipboardPublisher::findForOwner(const QObject* const owner)
{
    if ( !owner )
    {
        return nullptr;
    }
    return dynamic_cast<VerifiedClipboardPublisher*>(owner->findChild<QObject*>(
        QString::fromLatin1(kPublisherObjectName),
        Qt::FindDirectChildrenOnly));
}

std::optional<ClipboardPublishOutcome> VerifiedClipboardPublisher::publish(
    ClipboardPublication publication,
    Completion completion)
{
    ++generation_;
    request_ = Request{ std::move(publication), {}, std::nullopt };
    attempts_ = 1;
    if ( backend_.setMimeData )
    {
        backend_.setMimeData(buildMimeData());
    }
    request_.reset();
    (void)finish(ClipboardPublishOutcome::Published);
    if ( completion )
    {
        completion(ClipboardPublishOutcome::Published);
    }
    return ClipboardPublishOutcome::Published;
}

bool VerifiedClipboardPublisher::pending() const noexcept
{
    return false;
}

int VerifiedClipboardPublisher::attemptCount() const noexcept
{
    return attempts_;
}

std::optional<ClipboardPublishOutcome> VerifiedClipboardPublisher::lastOutcome() const noexcept
{
    return lastOutcome_;
}

const ClipboardRetryPolicy& VerifiedClipboardPublisher::retryPolicy() const noexcept
{
    return policy_;
}

std::optional<ClipboardPublishOutcome> VerifiedClipboardPublisher::attempt()
{
    return std::nullopt;
}

void VerifiedClipboardPublisher::retry()
{
}

ClipboardPublishOutcome VerifiedClipboardPublisher::finish(const ClipboardPublishOutcome outcome)
{
    lastOutcome_ = outcome;
    return outcome;
}

std::unique_ptr<QMimeData> VerifiedClipboardPublisher::buildMimeData() const
{
    auto mime = std::make_unique<QMimeData>();
    if ( request_ )
    {
        for ( const ClipboardFormatPayload& format : request_->publication.formats )
        {
            mime->setData(format.mimeType, format.bytes);
        }
        if ( !request_->publication.text.isEmpty() )
        {
            mime->setText(request_->publication.text);
        }
    }
    return mime;
}

bool VerifiedClipboardPublisher::readbackMatches() const
{
    return true;
}

std::optional<std::uint32_t> VerifiedClipboardPublisher::changeCounter() const
{
    return std::nullopt;
}

} // namespace sed::ui
