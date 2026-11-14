#include "semantic_review_io.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sed::ui::semantic_review
{
namespace
{
namespace semantic = semantic_preview;
constexpr qsizetype kReadChunkBytes = 1024 * 1024;
constexpr std::size_t kMaxReceiptKeyBytes = 1024;
constexpr std::uint64_t kMaxExactJsonInteger = 9007199254740991ull;

struct Failure
{
    ErrorCode code;
    QString message;
};

[[noreturn]] void fail(const ErrorCode code, const QString& message)
{
    throw Failure{code, message};
}

void checkStop(const std::stop_token stop)
{
    if ( stop.stop_requested() )
    {
        throw semantic::ComparisonCancelled{};
    }
}

QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string exactString(const QString& value)
{
    const auto bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

void require(const bool condition, const QString& message)
{
    if ( !condition )
    {
        fail(ErrorCode::InvalidReceipt, message);
    }
}

bool validPath(const QString& path)
{
    return !path.isEmpty() && static_cast<std::size_t>(path.size()) <= kMaxPathCharacters
        && !path.contains(QChar::Null);
}

struct CapturedFile
{
    QByteArray bytes;
    FileIdentity identity;
};

CapturedFile capture(const QString& path, const QString& role,
    const std::uint64_t maxBytes, const std::stop_token stop)
{
    checkStop(stop);
    if ( !validPath(path) )
    {
        fail(ErrorCode::InvalidRequest, QStringLiteral("Choose a nonempty file path within the path limit."));
    }
    if ( !QFileInfo(path).isFile() )
    {
        fail(ErrorCode::ReadFailed, QStringLiteral("The selected %1 path is not a regular file.").arg(role));
    }
    QFile input(path);
    if ( !input.open(QIODevice::ReadOnly) || input.isSequential() )
    {
        fail(ErrorCode::ReadFailed, QStringLiteral("Cannot open the selected %1 file for bounded reading.").arg(role));
    }
    const qint64 initialSize = input.size();
    if ( initialSize < 0 )
    {
        fail(ErrorCode::ReadFailed, QStringLiteral("Cannot determine the selected %1 file size.").arg(role));
    }
    if ( static_cast<std::uint64_t>(initialSize) > maxBytes )
    {
        fail(ErrorCode::InputTooLarge, QStringLiteral("The selected %1 file exceeds the %2-byte review limit.")
            .arg(role).arg(static_cast<qulonglong>(maxBytes)));
    }
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(initialSize));
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while ( true )
    {
        checkStop(stop);
        const auto remaining = maxBytes - static_cast<std::uint64_t>(bytes.size());
        const auto amount = static_cast<qint64>(std::min<std::uint64_t>(remaining + 1,
            static_cast<std::uint64_t>(kReadChunkBytes)));
        const QByteArray chunk = input.read(amount);
        if ( input.error() != QFileDevice::NoError )
        {
            fail(ErrorCode::ReadFailed, QStringLiteral("Reading the selected %1 file failed.").arg(role));
        }
        if ( chunk.isEmpty() )
        {
            if ( !input.atEnd() )
            {
                fail(ErrorCode::ReadFailed, QStringLiteral("Reading the selected %1 file made no progress.").arg(role));
            }
            break;
        }
        if ( static_cast<std::uint64_t>(chunk.size()) > remaining )
        {
            fail(ErrorCode::InputTooLarge, QStringLiteral("The selected %1 file grew beyond its review byte limit.").arg(role));
        }
        digest.addData(chunk);
        bytes.append(chunk);
    }
    checkStop(stop);
    FileIdentity identity;
    identity.role = role;
    identity.path = QFileInfo(path).absoluteFilePath();
    identity.sha256 = QString::fromLatin1(digest.result().toHex());
    identity.byteCount = static_cast<std::uint64_t>(bytes.size());
    return {std::move(bytes), std::move(identity)};
}

std::uint32_t littleU32(const std::span<const std::byte> bytes)
{
    return std::to_integer<std::uint32_t>(bytes[0])
        | (std::to_integer<std::uint32_t>(bytes[1]) << 8)
        | (std::to_integer<std::uint32_t>(bytes[2]) << 16)
        | (std::to_integer<std::uint32_t>(bytes[3]) << 24);
}

// Native Level decoding already charges its 128-MiB/two-million-item graph
// before allocations. The optional ARCH helper reserves its declared count,
// so admit its conservative footprint before calling the unchanged decoder.
// This is allocation admission only; field validity remains the codec's job.
void admitArchExtension(const Level& level,
    const std::span<const assets::SedOptionalSection> sections)
{
    const auto base = assets::estimateSedLevelDecodedFootprint(level);
    bool seen{};
    for ( const auto& section : sections )
    {
        if ( section.type != assets::SedArchAuthoringSectionType )
        {
            continue;
        }
        const auto payload = std::span<const std::byte>(section.payload);
        if ( seen || payload.size() < 5 )
        {
            fail(ErrorCode::InvalidNative, QStringLiteral("Malformed or duplicate native ARCH authoring section."));
        }
        seen = true;
        const std::uint64_t count = littleU32(payload.subspan(1, 4));
        // One record has an int32, an origin byte, and five uint32 string lengths.
        if ( count > (payload.size() - 5) / 25
            || count > assets::SedMaximumDecodedItems - base.itemCount )
        {
            fail(ErrorCode::InvalidNative, QStringLiteral("Native ARCH authoring count exceeds available payload or decoded-item admission."));
        }
        const std::uint64_t recordBytes = count * sizeof(MotsArchAuthoringEntry);
        const std::uint64_t stringBound = static_cast<std::uint64_t>(payload.size()) + count * 5;
        const std::uint64_t remaining = assets::SedMaximumDecodedObjectBytes - base.ownedBytes;
        if ( recordBytes > remaining || stringBound > remaining - recordBytes )
        {
            fail(ErrorCode::InvalidNative, QStringLiteral("Native ARCH authoring exceeds decoded-memory admission."));
        }
    }
}

struct LoadedSnapshot
{
    Level level;
    FileIdentity identity;
};

LoadedSnapshot loadSnapshot(const QString& path, const QString& role, const std::stop_token stop)
{
    if ( !validPath(path) )
    {
        fail(ErrorCode::InvalidRequest, QStringLiteral("Choose all three saved snapshot files."));
    }
    if ( QFileInfo(path).suffix().compare(QStringLiteral("sed"), Qt::CaseInsensitive) != 0 )
    {
        fail(ErrorCode::UnsupportedFormat, QStringLiteral("Saved snapshot review supports native .sed files only. "
            "Import a complete plan/apply receipt to inspect a JKL, NDY, JED or NED workflow."));
    }
    auto captured = capture(path, role, kMaxSnapshotBytes, stop);
    checkStop(stop);
    try
    {
        auto file = assets::loadSedFileBytes(std::as_bytes(std::span(
            captured.bytes.constData(), static_cast<std::size_t>(captured.bytes.size()))));
        checkStop(stop);
        admitArchExtension(file.level, file.optionalSections);
        (void)assets::applySedLightRgbSection(file.level, file.optionalSections);
        checkStop(stop);
        (void)assets::applySedArchAuthoringSection(file.level, file.optionalSections);
        checkStop(stop);
        captured.identity.nativeHadCorruption = file.hadCorruption;
        captured.identity.nativeRepairedChunks = file.repairedChunks;
        for ( const auto& section : file.repairedSections )
        {
            captured.identity.nativeRepairedSections.push_back(text(section));
        }
        return {std::move(file.level), std::move(captured.identity)};
    }
    catch ( const std::bad_alloc& )
    {
        throw;
    }
    catch ( const semantic::ComparisonCancelled& )
    {
        throw;
    }
    catch ( const std::exception& error )
    {
        fail(ErrorCode::InvalidNative, QStringLiteral("Cannot decode the selected %1 native snapshot: %2")
            .arg(role, QString::fromUtf8(error.what())));
    }
}

void addIdentity(Review& review, FileIdentity identity)
{
    if ( identity.nativeHadCorruption || identity.nativeRepairedChunks != 0
        || !identity.nativeRepairedSections.isEmpty() )
    {
        review.warnings.push_back(QStringLiteral("%1: native loading reported corruption or repair (%2 checked chunks; sections: %3). "
            "The model reflects that load; SHA-256 identifies the original captured file bytes.")
            .arg(identity.role).arg(identity.nativeRepairedChunks)
            .arg(identity.nativeRepairedSections.join(QStringLiteral(", "))));
    }
    review.files.push_back(std::move(identity));
}

std::shared_ptr<const Review> loadSnapshots(const SnapshotRequest& request, const std::stop_token stop)
{
    // Validate every format before opening any one of the files.
    for ( const auto* path : {&request.baselinePath, &request.candidatePath, &request.outputPath} )
    {
        if ( !validPath(*path) )
        {
            fail(ErrorCode::InvalidRequest, QStringLiteral("Choose all three saved snapshot files."));
        }
        if ( QFileInfo(*path).suffix().compare(QStringLiteral("sed"), Qt::CaseInsensitive) != 0 )
        {
            fail(ErrorCode::UnsupportedFormat, QStringLiteral("Saved snapshot review supports native .sed files only. "
                "JKL, NDY, JED and NED workflows can be inspected through a complete plan/apply receipt."));
        }
    }
    auto result = std::make_shared<Review>();
    result->sourceKind = SourceKind::SavedSnapshots;
    result->provenance = QStringLiteral("Comparison of loaded saved .sed snapshots, captured separately from the selected files. "
        "Candidate and output are saved snapshots. This does not establish an in-memory patched model or infer serializer effects.");
    const auto baseline = loadSnapshot(request.baselinePath, QStringLiteral("baseline"), stop);
    result->inputSha256 = baseline.identity.sha256;
    addIdentity(*result, baseline.identity);
    // Retain only the baseline and one after-model at a time. Each decoded
    // model is independently capped, including understood authoring extensions.
    {
        auto candidate = loadSnapshot(request.candidatePath, QStringLiteral("candidate"), stop);
        result->authored = semantic::compareLevels(baseline.level, candidate.level, result->limits, stop);
        addIdentity(*result, std::move(candidate.identity));
    }
    {
        auto output = loadSnapshot(request.outputPath, QStringLiteral("output"), stop);
        result->persisted = semantic::compareLevels(baseline.level, output.level, result->limits, stop);
        result->outputSha256 = output.identity.sha256;
        addIdentity(*result, std::move(output.identity));
    }
    checkStop(stop);
    return result;
}

bool hexDigit(const char value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

// Bounded grammar preflight precedes QJsonDocument. It admits the complete
// receipt (including auxiliary reports), rejects duplicate decoded keys, and
// validates Unicode so the Qt tree cannot silently repair malformed strings.
class JsonAdmission
{
public:
    JsonAdmission(const QByteArray& bytes, const std::stop_token stop)
        : bytes_(bytes.constData(), static_cast<std::size_t>(bytes.size())), stop_(stop)
    {
    }

    void run()
    {
        whitespace();
        value(1);
        whitespace();
        require(offset_ == bytes_.size(), QStringLiteral("Trailing data after the receipt JSON value."));
    }

private:
    std::string_view bytes_;
    std::size_t offset_{};
    std::size_t nodes_{};
    std::stop_token stop_;

    [[nodiscard]] char current() const
    {
        return offset_ < bytes_.size() ? bytes_[offset_] : '\0';
    }

    void whitespace()
    {
        while ( offset_ < bytes_.size() )
        {
            const char next = current();
            if ( next != ' ' && next != '\t' && next != '\r' && next != '\n' )
            {
                break;
            }
            ++offset_;
            if ( offset_ % 4096 == 0 )
            {
                checkStop(stop_);
            }
        }
    }

    void admitNode()
    {
        checkStop(stop_);
        if ( nodes_ == kMaxReceiptNodes )
        {
            fail(ErrorCode::ReceiptTooComplex, QStringLiteral("The receipt exceeds its JSON node budget."));
        }
        ++nodes_;
    }

    void expect(const char expected)
    {
        require(current() == expected && offset_ < bytes_.size(), QStringLiteral("Malformed receipt JSON framing."));
        ++offset_;
    }

    std::uint16_t unicodeEscape()
    {
        require(bytes_.size() - offset_ >= 4, QStringLiteral("Truncated JSON Unicode escape."));
        std::uint16_t result{};
        for ( int index = 0; index < 4; ++index )
        {
            const char ch = bytes_[offset_++];
            require(hexDigit(ch), QStringLiteral("Invalid JSON Unicode escape."));
            const unsigned digit = ch <= '9' ? static_cast<unsigned>(ch - '0')
                : ch <= 'F' ? static_cast<unsigned>(ch - 'A' + 10) : static_cast<unsigned>(ch - 'a' + 10);
            result = static_cast<std::uint16_t>((result << 4) | digit);
        }
        return result;
    }

    void utf8Continuation(const unsigned char first)
    {
        std::uint32_t codepoint{};
        std::uint32_t minimum{};
        std::size_t trailing{};
        if ( first >= 0xc2 && first <= 0xdf )
        {
            codepoint = first & 0x1fu;
            minimum = 0x80;
            trailing = 1;
        }
        else if ( first >= 0xe0 && first <= 0xef )
        {
            codepoint = first & 0x0fu;
            minimum = 0x800;
            trailing = 2;
        }
        else if ( first >= 0xf0 && first <= 0xf4 )
        {
            codepoint = first & 0x07u;
            minimum = 0x10000;
            trailing = 3;
        }
        else
        {
            fail(ErrorCode::InvalidReceipt, QStringLiteral("Receipt strings must contain valid UTF-8."));
        }
        require(trailing <= bytes_.size() - offset_, QStringLiteral("Truncated UTF-8 receipt string."));
        for ( std::size_t index = 0; index < trailing; ++index )
        {
            const auto next = static_cast<unsigned char>(bytes_[offset_++]);
            require((next & 0xc0u) == 0x80u, QStringLiteral("Invalid UTF-8 receipt string."));
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        require(codepoint >= minimum && codepoint <= 0x10ffff
            && !(codepoint >= 0xd800 && codepoint <= 0xdfff), QStringLiteral("Invalid Unicode receipt string."));
    }

    std::string_view stringToken()
    {
        const auto start = offset_;
        expect('"');
        while ( offset_ < bytes_.size() )
        {
            if ( offset_ % 4096 == 0 )
            {
                checkStop(stop_);
            }
            const auto ch = static_cast<unsigned char>(bytes_[offset_++]);
            if ( ch == '"' )
            {
                return bytes_.substr(start, offset_ - start);
            }
            require(ch >= 0x20, QStringLiteral("Unescaped control character in receipt string."));
            if ( ch == '\\' )
            {
                require(offset_ < bytes_.size(), QStringLiteral("Truncated receipt string escape."));
                const char escaped = bytes_[offset_++];
                if ( escaped == 'u' )
                {
                    const auto code = unicodeEscape();
                    if ( code >= 0xd800 && code <= 0xdbff )
                    {
                        expect('\\');
                        expect('u');
                        const auto low = unicodeEscape();
                        require(low >= 0xdc00 && low <= 0xdfff, QStringLiteral("Unpaired JSON Unicode surrogate."));
                    }
                    else
                    {
                        require(code < 0xdc00 || code > 0xdfff, QStringLiteral("Unpaired JSON Unicode surrogate."));
                    }
                }
                else
                {
                    require(escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b'
                        || escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't',
                        QStringLiteral("Invalid receipt string escape."));
                }
            }
            else if ( ch >= 0x80 )
            {
                utf8Continuation(ch);
            }
        }
        fail(ErrorCode::InvalidReceipt, QStringLiteral("Unterminated receipt string."));
    }

    QString key()
    {
        admitNode();
        const auto token = stringToken();
        if ( token.size() > kMaxReceiptKeyBytes )
        {
            fail(ErrorCode::ReceiptTooComplex, QStringLiteral("A receipt object key exceeds its byte limit."));
        }
        // Only this bounded scalar is parsed before full-tree admission. It
        // decodes escaped spellings so \"ok\" and \"\\u006fk\" collide.
        QByteArray wrapped("[");
        wrapped.append(token.data(), static_cast<qsizetype>(token.size()));
        wrapped.append(']');
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(wrapped, &error);
        require(error.error == QJsonParseError::NoError && parsed.isArray()
            && parsed.array().size() == 1 && parsed.array().first().isString(),
            QStringLiteral("Invalid receipt object key."));
        return parsed.array().first().toString();
    }

    void number()
    {
        if ( current() == '-' )
        {
            ++offset_;
        }
        if ( current() == '0' )
        {
            ++offset_;
        }
        else
        {
            require(current() >= '1' && current() <= '9', QStringLiteral("Invalid receipt JSON number."));
            while ( current() >= '0' && current() <= '9' )
            {
                ++offset_;
            }
        }
        if ( current() == '.' )
        {
            ++offset_;
            require(current() >= '0' && current() <= '9', QStringLiteral("Invalid JSON fraction."));
            while ( current() >= '0' && current() <= '9' )
            {
                ++offset_;
            }
        }
        if ( current() == 'e' || current() == 'E' )
        {
            ++offset_;
            if ( current() == '+' || current() == '-' )
            {
                ++offset_;
            }
            require(current() >= '0' && current() <= '9', QStringLiteral("Invalid JSON exponent."));
            while ( current() >= '0' && current() <= '9' )
            {
                ++offset_;
            }
        }
    }

    void value(const std::size_t depth)
    {
        admitNode();
        if ( depth > kMaxReceiptDepth )
        {
            fail(ErrorCode::ReceiptTooComplex, QStringLiteral("The receipt exceeds its JSON nesting limit."));
        }
        whitespace();
        if ( current() == '{' )
        {
            ++offset_;
            whitespace();
            QSet<QString> keys;
            if ( current() != '}' )
            {
                while ( true )
                {
                    const QString next = key();
                    require(!keys.contains(next), QStringLiteral("Duplicate object key in receipt JSON."));
                    keys.insert(next);
                    whitespace();
                    expect(':');
                    value(depth + 1);
                    whitespace();
                    if ( current() != ',' )
                    {
                        break;
                    }
                    ++offset_;
                    whitespace();
                }
            }
            expect('}');
        }
        else if ( current() == '[' )
        {
            ++offset_;
            whitespace();
            if ( current() != ']' )
            {
                while ( true )
                {
                    value(depth + 1);
                    whitespace();
                    if ( current() != ',' )
                    {
                        break;
                    }
                    ++offset_;
                }
            }
            expect(']');
        }
        else if ( current() == '"' )
        {
            (void)stringToken();
        }
        else if ( current() == 't' || current() == 'f' || current() == 'n' )
        {
            const std::string_view literal = current() == 't' ? "true" : current() == 'f' ? "false" : "null";
            require(bytes_.substr(offset_, literal.size()) == literal, QStringLiteral("Invalid receipt JSON literal."));
            offset_ += literal.size();
        }
        else
        {
            number();
        }
    }
};

QJsonObject object(const QJsonValue& value, const char* label)
{
    require(value.isObject(), QStringLiteral("Expected a JSON object for %1.").arg(QString::fromLatin1(label)));
    return value.toObject();
}

void shape(const QJsonObject& value, const std::initializer_list<const char*> fields)
{
    require(value.size() == static_cast<qsizetype>(fields.size()), QStringLiteral("Unexpected or missing semantic receipt fields."));
    for ( const char* field : fields )
    {
        require(value.contains(QString::fromLatin1(field)), QStringLiteral("Missing semantic receipt field: %1.").arg(QString::fromLatin1(field)));
    }
}

bool boolean(const QJsonValue& value)
{
    require(value.isBool(), QStringLiteral("Expected a boolean semantic receipt field."));
    return value.toBool();
}

std::size_t sizeValue(const QJsonValue& value,
    const std::uint64_t maximum = kMaxExactJsonInteger)
{
    require(value.isDouble(), QStringLiteral("Expected an integer semantic receipt field."));
    const double number = value.toDouble();
    const auto supported = std::min<std::uint64_t>(maximum,
        std::min<std::uint64_t>(kMaxExactJsonInteger, std::numeric_limits<std::size_t>::max()));
    require(std::isfinite(number) && number >= 0 && std::floor(number) == number
        && number <= static_cast<double>(supported), QStringLiteral("Semantic receipt integer is outside its admitted range."));
    return static_cast<std::size_t>(number);
}

bool lowerHex(const QString& value, const qsizetype length)
{
    if ( value.size() != length )
    {
        return false;
    }
    for ( const QChar ch : value )
    {
        if ( !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) )
        {
            return false;
        }
    }
    return true;
}

QString sha256(const QJsonValue& value)
{
    require(value.isString() && lowerHex(value.toString(), 64), QStringLiteral("Receipt hashes must be 64 lowercase hexadecimal characters."));
    return value.toString();
}

template<std::size_t N>
void exactStringArray(const QJsonValue& value, const std::array<std::string_view, N>& expected)
{
    require(value.isArray(), QStringLiteral("Expected a semantic receipt metadata array."));
    const auto array = value.toArray();
    require(array.size() == static_cast<qsizetype>(N), QStringLiteral("Semantic receipt metadata array does not match this version."));
    for ( std::size_t index = 0; index < N; ++index )
    {
        require(array[static_cast<qsizetype>(index)].isString()
            && array[static_cast<qsizetype>(index)].toString() == text(expected[index]),
            QStringLiteral("Semantic receipt metadata does not match this version."));
    }
}

semantic::Limits limits(const QJsonValue& value)
{
    const auto fields = object(value, "limits");
    shape(fields, {"maxVisitedSlots", "maxComparedStringBytes", "maxChanges", "maxInlineStringBytes", "maxTotalInlineStringBytes"});
    semantic::Limits result;
    result.maxVisitedSlots = sizeValue(fields.value("maxVisitedSlots"), semantic::kDefaultLimits.maxVisitedSlots);
    result.maxComparedStringBytes = sizeValue(fields.value("maxComparedStringBytes"), semantic::kDefaultLimits.maxComparedStringBytes);
    result.maxChanges = sizeValue(fields.value("maxChanges"), semantic::kDefaultLimits.maxChanges);
    result.maxInlineStringBytes = sizeValue(fields.value("maxInlineStringBytes"), semantic::kDefaultLimits.maxInlineStringBytes);
    result.maxTotalInlineStringBytes = sizeValue(fields.value("maxTotalInlineStringBytes"), semantic::kDefaultLimits.maxTotalInlineStringBytes);
    return result;
}

void addReason(std::vector<semantic::Reason>& reasons, const semantic::Reason reason)
{
    if ( std::find(reasons.begin(), reasons.end(), reason) == reasons.end() )
    {
        reasons.push_back(reason);
    }
}

semantic::Reason reasonValue(const QJsonValue& value)
{
    require(value.isString(), QStringLiteral("Invalid semantic incompleteness reason."));
    for ( const auto reason : {semantic::Reason::ComparisonSlots, semantic::Reason::ComparisonStringBytes,
        semantic::Reason::ChangeLimit, semantic::Reason::InlineStringLength, semantic::Reason::InlineStringBytes} )
    {
        if ( value.toString() == text(semantic::name(reason)) )
        {
            return reason;
        }
    }
    fail(ErrorCode::InvalidReceipt, QStringLiteral("Unknown semantic incompleteness reason."));
}

struct DescriptorBudget
{
    const semantic::Limits& limits;
    std::size_t inlineBytes{};
    std::size_t describedStringBytes{};
    std::vector<semantic::Reason> reasons;
};

template<typename Bits, typename FloatValue>
semantic::Descriptor floatDescriptor(const QJsonObject& fields)
{
    shape(fields, {"present", "type", "bits", "classification", "value"});
    const auto bitText = fields.value("bits");
    require(bitText.isString() && lowerHex(bitText.toString(), sizeof(Bits) * 2),
        QStringLiteral("Float descriptors require exact-width lowercase IEEE hexadecimal bits."));
    const QByteArray ascii = bitText.toString().toLatin1();
    Bits bits{};
    const auto parsed = std::from_chars(ascii.constData(), ascii.constData() + ascii.size(), bits, 16);
    require(parsed.ec == std::errc{} && parsed.ptr == ascii.constData() + ascii.size(),
        QStringLiteral("Invalid float descriptor bits."));
    const FloatValue value{bits};
    const auto classification = semantic::classify(value);
    require(fields.value("classification").isString()
        && fields.value("classification").toString() == text(semantic::name(classification)),
        QStringLiteral("Float descriptor classification disagrees with its IEEE bits."));
    if ( classification == semantic::FloatClassification::Finite
        || classification == semantic::FloatClassification::NegativeZero )
    {
        double number{};
        if constexpr ( std::is_same_v<Bits, std::uint32_t> )
        {
            number = static_cast<double>(std::bit_cast<float>(bits));
        }
        else
        {
            number = std::bit_cast<double>(bits);
        }
        require(fields.value("value").isDouble() && fields.value("value").toDouble() == number,
            QStringLiteral("Float descriptor value disagrees with its IEEE bits."));
    }
    else
    {
        require(fields.value("value").isNull(), QStringLiteral("Nonfinite float descriptors must use JSON null."));
    }
    return {semantic::Value{value}};
}

semantic::Descriptor descriptor(const QJsonValue& source, DescriptorBudget& budget)
{
    const auto fields = object(source, "value descriptor");
    if ( !boolean(fields.value("present")) )
    {
        shape(fields, {"present"});
        return {};
    }
    require(fields.value("type").isString(), QStringLiteral("Present descriptor requires a storage type."));
    const auto type = fields.value("type").toString();
    if ( type == "float32" )
    {
        return floatDescriptor<std::uint32_t, semantic::Float32Value>(fields);
    }
    if ( type == "float64" )
    {
        return floatDescriptor<std::uint64_t, semantic::Float64Value>(fields);
    }
    if ( type == "string" )
    {
        const auto count = sizeValue(fields.value("byteCount"), budget.limits.maxComparedStringBytes);
        require(count <= budget.limits.maxComparedStringBytes - budget.describedStringBytes,
            QStringLiteral("Descriptor strings exceed compared-string admission."));
        budget.describedStringBytes += count;
        const bool tooLong = count > budget.limits.maxInlineStringBytes;
        const bool tooMany = count > budget.limits.maxTotalInlineStringBytes - budget.inlineBytes;
        if ( fields.contains("omitted") )
        {
            shape(fields, {"present", "type", "byteCount", "omitted"});
            require(boolean(fields.value("omitted")) && (tooLong || tooMany),
                QStringLiteral("String omission is not explained by the declared inline limits."));
            addReason(budget.reasons, tooLong ? semantic::Reason::InlineStringLength : semantic::Reason::InlineStringBytes);
            return {semantic::Value{semantic::StringValue{count, std::nullopt, semantic::StringEncoding::Utf8}}};
        }
        shape(fields, {"present", "type", "byteCount", "encoding", "value"});
        require(!tooLong && !tooMany && fields.value("encoding").isString() && fields.value("value").isString(),
            QStringLiteral("Inline string descriptor exceeds its limits or has invalid fields."));
        const auto encoding = fields.value("encoding").toString();
        const auto value = fields.value("value").toString();
        semantic::StringEncoding kind;
        if ( encoding == "utf8" )
        {
            kind = semantic::StringEncoding::Utf8;
            require(static_cast<std::size_t>(value.toUtf8().size()) == count,
                QStringLiteral("UTF-8 string descriptor byteCount is incorrect."));
        }
        else
        {
            require(encoding == "hex" && lowerHex(value, static_cast<qsizetype>(count * 2)),
                QStringLiteral("Hexadecimal string descriptor does not match byteCount."));
            const auto raw = QByteArray::fromHex(value.toLatin1());
            require(QString::fromUtf8(raw).toUtf8() != raw,
                QStringLiteral("Hexadecimal string descriptors are reserved for invalid UTF-8 bytes."));
            kind = semantic::StringEncoding::Hex;
        }
        budget.inlineBytes += count;
        return {semantic::Value{semantic::StringValue{count, exactString(value), kind}}};
    }
    shape(fields, {"present", "type", "value"});
    if ( type == "bool" )
    {
        return {semantic::Value{boolean(fields.value("value"))}};
    }
    if ( type == "size" )
    {
        return {semantic::Value{semantic::SizeValue{sizeValue(fields.value("value"))}}};
    }
    if ( type == "uint32" )
    {
        return {semantic::Value{static_cast<std::uint32_t>(sizeValue(fields.value("value"), std::numeric_limits<std::uint32_t>::max()))}};
    }
    require((type == "int32" || type == "enum") && fields.value("value").isDouble(),
        QStringLiteral("Unsupported descriptor storage type or value."));
    const double number = fields.value("value").toDouble();
    const double minimum = type == "int32" ? static_cast<double>(std::numeric_limits<std::int32_t>::min())
        : -static_cast<double>(kMaxExactJsonInteger);
    const double maximum = type == "int32" ? static_cast<double>(std::numeric_limits<std::int32_t>::max())
        : static_cast<double>(kMaxExactJsonInteger);
    require(std::isfinite(number) && std::floor(number) == number && number >= minimum && number <= maximum,
        QStringLiteral("Signed or enum descriptor is outside its exact integer range."));
    if ( type == "int32" )
    {
        return {semantic::Value{static_cast<std::int32_t>(number)}};
    }
    return {semantic::Value{semantic::EnumValue{static_cast<std::int64_t>(number)}}};
}

bool omitted(const semantic::Descriptor& value)
{
    if ( !value.value )
    {
        return false;
    }
    const auto* string = std::get_if<semantic::StringValue>(&*value.value);
    return string && !string->encodedValue;
}

bool validModelPath(const QString& path)
{
    if ( path.isEmpty() || path.size() > 4096 || path.front() != '/' || path.back() == '/' )
    {
        return false;
    }
    constexpr std::array<std::string_view, 21> roots{
        "header", "exportBufferExtras", "kind", "uvCoordinateSpace", "sectors",
        "cogs", "templates", "things", "motsArchLighting", "motsArchLightingBakeBaselines",
        "motsArchAuthoring", "hasSourceLightMetadata", "lights", "h3doNodes", "colormaps",
        "materials", "layers", "masterCMP", "ppunit", "lvisstring", "curId"};
    const QString root = path.section('/', 1, 1);
    const auto coveredRoot = [&](const auto name)
    {
        return root == text(name);
    };
    if ( std::none_of(roots.begin(), roots.end(), coveredRoot) )
    {
        return false;
    }
    bool separator{};
    for ( const QChar ch : path )
    {
        if ( ch == '/' )
        {
            if ( separator )
            {
                return false;
            }
            separator = true;
        }
        else
        {
            if ( !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) )
            {
                return false;
            }
            separator = false;
        }
    }
    for ( const auto exclusion : semantic::kExcludedFields )
    {
        if ( exclusion.find('*') == std::string_view::npos && path == text(exclusion) )
        {
            return false;
        }
    }
    return !(path.startsWith("/sectors/") && path.endsWith("/d3dId"));
}

semantic::Comparison comparison(const QJsonValue& source, const char* after,
    const semantic::Limits& limits, const std::stop_token stop)
{
    const auto fields = object(source, "comparison");
    shape(fields, {"beforeSnapshot", "afterSnapshot", "comparisonComplete", "detailsComplete", "changed",
        "observedChanges", "totalChanges", "changes", "reasons", "work"});
    require(fields.value("beforeSnapshot").toString() == "source"
        && fields.value("afterSnapshot").toString() == QString::fromLatin1(after),
        QStringLiteral("Comparison snapshot identities are invalid."));
    semantic::Comparison result;
    result.comparisonComplete = boolean(fields.value("comparisonComplete"));
    result.detailsComplete = boolean(fields.value("detailsComplete"));
    result.observedChanges = sizeValue(fields.value("observedChanges"), limits.maxVisitedSlots);
    if ( fields.value("changed").isNull() )
    {
        result.changed.reset();
    }
    else
    {
        result.changed = boolean(fields.value("changed"));
    }
    if ( fields.value("totalChanges").isNull() )
    {
        result.totalChanges.reset();
    }
    else
    {
        result.totalChanges = sizeValue(fields.value("totalChanges"), limits.maxVisitedSlots);
    }
    const auto expectedChanged = result.observedChanges != 0 ? std::optional<bool>(true)
        : result.comparisonComplete ? std::optional<bool>(false) : std::nullopt;
    require(result.changed == expectedChanged && (result.comparisonComplete
        ? result.totalChanges == result.observedChanges : !result.totalChanges),
        QStringLiteral("Comparison completeness, changed flag or total count is inconsistent."));
    const auto work = object(fields.value("work"), "work");
    shape(work, {"visitedSlots", "comparedStringBytes", "inlineStringBytes"});
    result.work.visitedSlots = sizeValue(work.value("visitedSlots"), limits.maxVisitedSlots);
    result.work.comparedStringBytes = sizeValue(work.value("comparedStringBytes"), limits.maxComparedStringBytes);
    result.work.inlineStringBytes = sizeValue(work.value("inlineStringBytes"), limits.maxTotalInlineStringBytes);
    require(result.observedChanges <= result.work.visitedSlots, QStringLiteral("Change count exceeds visited slots."));
    require(!result.comparisonComplete || result.work.visitedSlots != 0,
        QStringLiteral("A complete comparison must visit its Level root."));
    require(fields.value("changes").isArray(), QStringLiteral("Comparison changes must be an array."));
    const auto rows = fields.value("changes").toArray();
    require(static_cast<std::size_t>(rows.size()) == std::min(result.observedChanges, limits.maxChanges),
        QStringLiteral("Emitted change count disagrees with the declared detail limit."));
    DescriptorBudget budget{limits, 0, 0, {}};
    QSet<QString> paths;
    for ( const auto& item : rows )
    {
        checkStop(stop);
        const auto fields = object(item, "change");
        shape(fields, {"path", "kind", "before", "after"});
        require(fields.value("path").isString() && validModelPath(fields.value("path").toString()),
            QStringLiteral("Change path has invalid snapshot-address syntax or an excluded or unknown model root."));
        const QString path = fields.value("path").toString();
        require(!paths.contains(path), QStringLiteral("Duplicate semantic change path."));
        paths.insert(path);
        semantic::Change change;
        change.path = exactString(path);
        const auto kind = fields.value("kind").toString();
        if ( kind == "value" )
        {
            change.kind = semantic::ChangeKind::Value;
        }
        else if ( kind == "presence" )
        {
            change.kind = semantic::ChangeKind::Presence;
        }
        else
        {
            require(kind == "arrayLength", QStringLiteral("Unknown semantic change kind."));
            change.kind = semantic::ChangeKind::ArrayLength;
        }
        change.before = descriptor(fields.value("before"), budget);
        change.after = descriptor(fields.value("after"), budget);
        require(change.before.value || change.after.value, QStringLiteral("A change cannot have two absent descriptors."));
        if ( change.before.value && change.after.value )
        {
            require(change.before.value->index() == change.after.value->index(),
                QStringLiteral("A scalar change cannot change its storage type."));
            require(change.before != change.after || omitted(change.before) || omitted(change.after),
                QStringLiteral("A change row contains identical complete descriptors."));
        }
        const auto isSize = [](const semantic::Descriptor& value)
        {
            return !value.value || std::holds_alternative<semantic::SizeValue>(*value.value);
        };
        if ( change.kind == semantic::ChangeKind::ArrayLength )
        {
            require(isSize(change.before) && isSize(change.after), QStringLiteral("Array lengths require size descriptors."));
        }
        else
        {
            require(!(change.before.value && isSize(change.before)) && !(change.after.value && isSize(change.after)),
                QStringLiteral("Size descriptors are reserved for array-length changes."));
        }
        if ( change.kind == semantic::ChangeKind::Presence && change.before.value && change.after.value )
        {
            require(std::holds_alternative<bool>(*change.before.value),
                QStringLiteral("Present optional-record occupancy requires boolean descriptors."));
        }
        result.changes.push_back(std::move(change));
    }
    require(result.work.inlineStringBytes == budget.inlineBytes
        && result.work.comparedStringBytes >= budget.describedStringBytes,
        QStringLiteral("String work counters disagree with the emitted descriptors."));
    require(fields.value("reasons").isArray(), QStringLiteral("Comparison reasons must be an array."));
    const auto reasons = fields.value("reasons").toArray();
    require(reasons.size() <= 5, QStringLiteral("Too many comparison incompleteness reasons."));
    std::optional<semantic::Reason> exhaustion;
    for ( const auto& item : reasons )
    {
        const auto reason = reasonValue(item);
        require(std::find(result.reasons.begin(), result.reasons.end(), reason) == result.reasons.end(),
            QStringLiteral("Comparison incompleteness reasons must be unique."));
        result.reasons.push_back(reason);
        if ( reason == semantic::Reason::ComparisonSlots || reason == semantic::Reason::ComparisonStringBytes )
        {
            require(!exhaustion, QStringLiteral("Only one exhausted comparison budget can terminate a view."));
            exhaustion = reason;
        }
    }
    if ( result.observedChanges > result.changes.size() )
    {
        addReason(budget.reasons, semantic::Reason::ChangeLimit);
    }
    require(result.comparisonComplete == !exhaustion, QStringLiteral("Comparison exhaustion reason disagrees with completeness."));
    if ( exhaustion )
    {
        if ( *exhaustion == semantic::Reason::ComparisonSlots )
        {
            require(result.work.visitedSlots == limits.maxVisitedSlots,
                QStringLiteral("Slot exhaustion requires consuming the declared slot limit."));
        }
        addReason(budget.reasons, *exhaustion);
    }
    require(result.reasons == budget.reasons, QStringLiteral("Comparison reasons do not explain the emitted result in encounter order."));
    require(result.detailsComplete == (result.comparisonComplete && budget.reasons.empty()),
        QStringLiteral("Detail completeness disagrees with omitted rows, values or comparison work."));
    return result;
}

std::shared_ptr<const Review> loadReceipt(const ReceiptRequest& request, const std::stop_token stop)
{
    auto captured = capture(request.receiptPath, QStringLiteral("receipt"), kMaxReceiptBytes, stop);
    JsonAdmission(captured.bytes, stop).run();
    checkStop(stop);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(captured.bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
        QStringLiteral("The receipt must be one complete JSON object."));
    const auto receipt = document.object();
    require(boolean(receipt.value("ok")), QStringLiteral("Only successful plan/apply receipts contain a reviewable result."));
    const auto command = receipt.value("command").toString();
    require(command == "plan" || command == "apply", QStringLiteral("Expected a complete plan or apply command receipt."));
    if ( receipt.contains("schemaVersion") )
    {
        require(sizeValue(receipt.value("schemaVersion"), 1) == 1, QStringLiteral("Unsupported receipt schemaVersion."));
    }
    const QString inputHash = sha256(receipt.value("inputSha256"));
    const QString outputHash = sha256(receipt.value("outputSha256"));
    require(sha256(receipt.value("sourceRevision")) == inputHash, QStringLiteral("Receipt sourceRevision disagrees with its input hash."));
    if ( command == "apply" )
    {
        require(receipt.value("output").isString() && validPath(receipt.value("output").toString()),
            QStringLiteral("An apply receipt requires its declared output path."));
    }
    const auto preview = object(receipt.value("semanticPreview"), "semanticPreview");
    shape(preview, {"version", "coverage", "addressing", "excludedFields", "excludedScopes", "limits",
        "commands", "views", "inputSha256", "outputSha256", "authored", "persisted"});
    require(sizeValue(preview.value("version"), semantic::kVersion) == semantic::kVersion
        && preview.value("coverage").toString() == text(semantic::kCoverage)
        && preview.value("addressing").toString() == text(semantic::kAddressing),
        QStringLiteral("Unsupported semantic preview version, coverage or addressing."));
    exactStringArray(preview.value("excludedFields"), semantic::kExcludedFields);
    exactStringArray(preview.value("excludedScopes"), semantic::kExcludedScopes);
    exactStringArray(preview.value("commands"), std::array<std::string_view, 2>{"plan", "apply"});
    const auto views = object(preview.value("views"), "views");
    shape(views, {"authored", "persisted"});
    require(views.value("authored").toString() == text(semantic::kAuthoredDescription)
        && views.value("persisted").toString() == text(semantic::kPersistedDescription),
        QStringLiteral("Semantic view scope does not match this receipt version."));
    require(sha256(preview.value("inputSha256")) == inputHash
        && sha256(preview.value("outputSha256")) == outputHash,
        QStringLiteral("Receipt and semantic preview byte identities disagree."));
    auto result = std::make_shared<Review>();
    result->sourceKind = SourceKind::ImportedReceipt;
    result->provenance = QStringLiteral("Imported plan/apply receipt; its semantic comparisons and input/output hashes are asserted by the receipt, not recomputed. "
        "Only the identity of the captured receipt file is calculated here.");
    result->receiptCommand = command;
    result->inputSha256 = inputHash;
    result->outputSha256 = outputHash;
    result->limits = limits(preview.value("limits"));
    result->authored = comparison(preview.value("authored"), "candidate", result->limits, stop);
    result->persisted = comparison(preview.value("persisted"), "output", result->limits, stop);
    result->files.push_back(std::move(captured.identity));
    result->originalReceipt = std::move(captured.bytes);
    checkStop(stop);
    return result;
}

QJsonObject limitsJson(const semantic::Limits& limits)
{
    return {{"maxVisitedSlots", static_cast<double>(limits.maxVisitedSlots)},
        {"maxComparedStringBytes", static_cast<double>(limits.maxComparedStringBytes)},
        {"maxChanges", static_cast<double>(limits.maxChanges)},
        {"maxInlineStringBytes", static_cast<double>(limits.maxInlineStringBytes)},
        {"maxTotalInlineStringBytes", static_cast<double>(limits.maxTotalInlineStringBytes)}};
}

template<typename Bits>
QString bitHex(Bits bits)
{
    constexpr char digits[] = "0123456789abcdef";
    QByteArray output(static_cast<qsizetype>(sizeof(Bits) * 2), '0');
    for ( qsizetype index = output.size(); index > 0; --index )
    {
        output.data()[index - 1] = digits[bits & Bits{15}];
        bits >>= 4;
    }
    return QString::fromLatin1(output);
}

QJsonObject descriptorJson(const semantic::Descriptor& descriptor)
{
    if ( !descriptor.value )
    {
        return {{"present", false}};
    }
    QJsonObject result{{"present", true}};
    std::visit([&](const auto& value)
    {
        using T = std::decay_t<decltype(value)>;
        if constexpr ( std::is_same_v<T, bool> )
        {
            result.insert("type", "bool");
            result.insert("value", value);
        }
        else if constexpr ( std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> )
        {
            result.insert("type", std::is_same_v<T, std::int32_t> ? "int32" : "uint32");
            result.insert("value", static_cast<double>(value));
        }
        else if constexpr ( std::is_same_v<T, semantic::EnumValue> || std::is_same_v<T, semantic::SizeValue> )
        {
            result.insert("type", std::is_same_v<T, semantic::EnumValue> ? "enum" : "size");
            result.insert("value", static_cast<double>(value.value));
        }
        else if constexpr ( std::is_same_v<T, semantic::StringValue> )
        {
            result.insert("type", "string");
            result.insert("byteCount", static_cast<double>(value.byteCount));
            if ( value.encodedValue )
            {
                result.insert("encoding", text(semantic::name(value.encoding)));
                result.insert("value", text(*value.encodedValue));
            }
            else
            {
                result.insert("omitted", true);
            }
        }
        else
        {
            result.insert("type", std::is_same_v<T, semantic::Float32Value> ? "float32" : "float64");
            result.insert("bits", bitHex(value.bits));
            const auto classification = semantic::classify(value);
            result.insert("classification", text(semantic::name(classification)));
            if ( classification == semantic::FloatClassification::Finite
                || classification == semantic::FloatClassification::NegativeZero )
            {
                if constexpr ( std::is_same_v<T, semantic::Float32Value> )
                {
                    result.insert("value", static_cast<double>(std::bit_cast<float>(value.bits)));
                }
                else
                {
                    result.insert("value", std::bit_cast<double>(value.bits));
                }
            }
            else
            {
                result.insert("value", QJsonValue::Null);
            }
        }
    }, *descriptor.value);
    return result;
}

QJsonObject comparisonJson(const semantic::Comparison& comparison, const char* after)
{
    QJsonObject result{{"beforeSnapshot", "source"}, {"afterSnapshot", QString::fromLatin1(after)},
        {"comparisonComplete", comparison.comparisonComplete}, {"detailsComplete", comparison.detailsComplete},
        {"changed", comparison.changed ? QJsonValue(*comparison.changed) : QJsonValue(QJsonValue::Null)},
        {"observedChanges", static_cast<double>(comparison.observedChanges)},
        {"totalChanges", comparison.totalChanges ? QJsonValue(static_cast<double>(*comparison.totalChanges)) : QJsonValue(QJsonValue::Null)}};
    QJsonArray changes;
    for ( const auto& change : comparison.changes )
    {
        changes.push_back(QJsonObject{{"path", text(change.path)}, {"kind", text(semantic::name(change.kind))},
            {"before", descriptorJson(change.before)}, {"after", descriptorJson(change.after)}});
    }
    result.insert("changes", changes);
    QJsonArray reasons;
    for ( const auto reason : comparison.reasons )
    {
        reasons.push_back(text(semantic::name(reason)));
    }
    result.insert("reasons", reasons);
    result.insert("work", QJsonObject{{"visitedSlots", static_cast<double>(comparison.work.visitedSlots)},
        {"comparedStringBytes", static_cast<double>(comparison.work.comparedStringBytes)},
        {"inlineStringBytes", static_cast<double>(comparison.work.inlineStringBytes)}});
    return result;
}

QByteArray exportBytes(const Review& review)
{
    if ( review.sourceKind == SourceKind::ImportedReceipt )
    {
        return review.originalReceipt;
    }
    QJsonArray excludedFields;
    for ( const auto field : semantic::kExcludedFields )
    {
        excludedFields.push_back(text(field));
    }
    QJsonArray excludedScopes;
    for ( const auto scope : semantic::kExcludedScopes )
    {
        excludedScopes.push_back(text(scope));
    }
    const QJsonObject preview{{"version", semantic::kVersion}, {"coverage", text(semantic::kCoverage)},
        {"addressing", text(semantic::kAddressing)}, {"excludedFields", excludedFields}, {"excludedScopes", excludedScopes},
        {"limits", limitsJson(review.limits)}, {"inputSha256", review.inputSha256}, {"outputSha256", review.outputSha256},
        {"views", QJsonObject{{"authored", "Loaded baseline saved snapshot compared with loaded candidate saved snapshot."},
            {"persisted", "Loaded baseline saved snapshot compared with loaded output saved snapshot."}}},
        {"authored", comparisonJson(review.authored, "candidate")}, {"persisted", comparisonJson(review.persisted, "output")}};
    QJsonArray files;
    for ( const auto& file : review.files )
    {
        QJsonArray sections;
        for ( const auto& section : file.nativeRepairedSections )
        {
            sections.push_back(section);
        }
        files.push_back(QJsonObject{{"role", file.role}, {"path", file.path}, {"sha256", file.sha256},
            {"byteCount", static_cast<double>(file.byteCount)}, {"nativeHadCorruption", file.nativeHadCorruption},
            {"nativeRepairedChunks", static_cast<double>(file.nativeRepairedChunks)}, {"nativeRepairedSections", sections}});
    }
    QJsonArray warnings;
    for ( const auto& warning : review.warnings )
    {
        warnings.push_back(warning);
    }
    const QJsonObject provenance{{"sourceKind", "saved-snapshots"}, {"description", review.provenance},
        {"files", files}, {"warnings", warnings}};
    return QJsonDocument(QJsonObject{{"reportVersion", 1}, {"reportKind", "saved-snapshot-comparison"},
        {"provenance", provenance}, {"semanticPreview", preview}}).toJson(QJsonDocument::Indented);
}
} // namespace

Result loadReview(const Request& request, const std::stop_token stop)
{
    try
    {
        checkStop(stop);
        auto result = std::visit([&](const auto& value) -> std::shared_ptr<const Review>
        {
            if constexpr ( std::is_same_v<std::decay_t<decltype(value)>, SnapshotRequest> )
            {
                return loadSnapshots(value, stop);
            }
            else
            {
                return loadReceipt(value, stop);
            }
        }, request);
        return {std::move(result), {}, ErrorCode::None};
    }
    catch ( const Failure& error )
    {
        return {nullptr, error.message, error.code};
    }
    catch ( const semantic::ComparisonCancelled& )
    {
        return {nullptr, {}, ErrorCode::Cancelled};
    }
}

ExportResult exportReviewCreateOnly(const Review& review, const QString& destination, const std::stop_token stop)
{
    if ( stop.stop_requested() )
    {
        return {false, {}, ErrorCode::Cancelled};
    }
    if ( !validPath(destination) )
    {
        return {false, QStringLiteral("Choose a nonempty report file path within the path limit."), ErrorCode::InvalidRequest};
    }
    const QByteArray bytes = exportBytes(review);
    if ( stop.stop_requested() )
    {
        return {false, {}, ErrorCode::Cancelled};
    }
    QFile output(destination);
    if ( !output.open(QIODevice::WriteOnly | QIODevice::NewOnly) )
    {
        const QFileInfo info(destination);
        const auto code = info.exists() || info.isSymLink() ? ErrorCode::OutputExists : ErrorCode::WriteFailed;
        return {false, QStringLiteral("Cannot create the report. Choose a new filename in an existing writable directory."), code};
    }
    const auto abandon = [&](const ErrorCode code, const QString& message)
    {
        output.close();
        if ( !output.remove() )
        {
            return ExportResult{false, message + QStringLiteral(" The incomplete new report could not be removed."), code};
        }
        return ExportResult{false, message, code};
    };
    qsizetype offset{};
    while ( offset < bytes.size() )
    {
        if ( stop.stop_requested() )
        {
            return abandon(ErrorCode::Cancelled, QStringLiteral("Report export cancelled."));
        }
        const auto amount = std::min(kReadChunkBytes, bytes.size() - offset);
        const auto written = output.write(bytes.constData() + offset, amount);
        if ( written <= 0 )
        {
            return abandon(ErrorCode::WriteFailed, QStringLiteral("Writing the new report failed."));
        }
        offset += written;
    }
    if ( stop.stop_requested() )
    {
        return abandon(ErrorCode::Cancelled, QStringLiteral("Report export cancelled."));
    }
    if ( !output.flush() )
    {
        return abandon(ErrorCode::WriteFailed, QStringLiteral("Flushing the new report failed."));
    }
    output.close();
    if ( output.error() != QFileDevice::NoError )
    {
        return abandon(ErrorCode::WriteFailed, QStringLiteral("Closing the new report failed."));
    }
    return {true, {}, ErrorCode::None};
}

} // namespace sed::ui::semantic_review
