#include "ui/semantic/semantic_review_io.hpp"

#include "sed/assets/sed.hpp"
#include "sed/assets/sed_extensions.hpp"
#include "sed/level.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
namespace review = sed::ui::semantic_review;
namespace semantic = sed::semantic_preview;
int failures{};

bool check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if ( !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || file.write(bytes) != bytes.size() || !file.flush() )
    {
        throw std::runtime_error("Cannot create isolated review fixture");
    }
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error("Cannot read isolated review fixture");
    }
    return file.readAll();
}

QString hash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray nativeBytes(const sed::Level& level,
    const std::vector<sed::assets::SedOptionalSection>& sections = {})
{
    const auto bytes = sed::assets::saveToSedBytes(level, sections);
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
        static_cast<qsizetype>(bytes.size()));
}

const semantic::Change* row(const semantic::Comparison& comparison, const std::string_view path)
{
    const auto found = std::find_if(comparison.changes.begin(), comparison.changes.end(),
        [&](const semantic::Change& change)
        {
            return change.path == path;
        });
    return found == comparison.changes.end() ? nullptr : &*found;
}

std::optional<std::uint64_t> doubleBits(const semantic::Descriptor& descriptor)
{
    if ( !descriptor.value )
    {
        return std::nullopt;
    }
    const auto* value = std::get_if<semantic::Float64Value>(&*descriptor.value);
    return value ? std::optional<std::uint64_t>(value->bits) : std::nullopt;
}

// Independently authored v1 receipt. Its values and metadata do not come from
// the service or comparator under test; mutations below name concrete refusals.
QByteArray receiptBytes()
{
    return R"JSON({
  "ok": true,
  "command": "plan",
  "inputSha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "sourceRevision": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "outputSha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "semanticPreview": {
    "version": 1,
    "coverage": "level-model-v1",
    "addressing": "snapshot-storage-slots",
    "excludedFields": ["/sectors/*/surfaces/*/d3dId", "/importedJklSurfaceRefs", "/importedJklCogRowsPresent", "/retainedJklSourceText", "/retainedJklSourceHash", "/retainedJklUnknownSections", "/retainedJklHeaderUnknownLines", "/retainedJklSectorUnknownLines", "/importedGeoWorldVertices", "/importedGeoWorldTexVertices", "/importedGeoAdjoinFlags", "/importedGeoAdjoinMirrors", "/importedGeoVertexIndices", "/importedGeoSurfaceIndices", "/importedGeoTexVertexIndices", "/importedGeoAdjoinIndices", "/importedGeoMaterialIndices", "/importedGeoSurfaceCount", "/importedGeoAdjoinCount"],
    "excludedScopes": ["nativeOptionalSectionBytes", "dataOutsideLevel"],
    "limits": {"maxVisitedSlots": 1000000, "maxComparedStringBytes": 4194304, "maxChanges": 128, "maxInlineStringBytes": 512, "maxTotalInlineStringBytes": 32768},
    "commands": ["plan", "apply"],
    "views": {"authored": "Captured source model compared with the candidate after all patch operations.", "persisted": "Captured source model compared with the model reloaded from the exact proposed output bytes."},
    "inputSha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "outputSha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    "authored": {
      "beforeSnapshot": "source", "afterSnapshot": "candidate",
      "comparisonComplete": true, "detailsComplete": true, "changed": true,
      "observedChanges": 1, "totalChanges": 1,
      "changes": [{"path": "/header/gravity", "kind": "value", "before": {"present": true, "type": "float64", "bits": "4010000000000000", "classification": "finite", "value": 4.0000}, "after": {"present": true, "type": "float64", "bits": "4018000000000000", "classification": "finite", "value": 6e0}}],
      "reasons": [], "work": {"visitedSlots": 68, "comparedStringBytes": 0, "inlineStringBytes": 0}
    },
    "persisted": {
      "beforeSnapshot": "source", "afterSnapshot": "output",
      "comparisonComplete": true, "detailsComplete": true, "changed": false,
      "observedChanges": 0, "totalChanges": 0, "changes": [], "reasons": [],
      "work": {"visitedSlots": 68, "comparedStringBytes": 0, "inlineStringBytes": 0}
    }
  }
}
)JSON";
}

review::Result importBytes(const QString& root, const QByteArray& bytes, const QString& name)
{
    const QString path = root + '/' + name + ".json";
    writeBytes(path, bytes);
    return review::loadReview(review::ReceiptRequest{path});
}

QJsonObject fixtureObject()
{
    return QJsonDocument::fromJson(receiptBytes()).object();
}

QByteArray changedPreview(const char* name, const QJsonValue& value)
{
    auto root = fixtureObject();
    auto preview = root.value("semanticPreview").toObject();
    preview.insert(QString::fromLatin1(name), value);
    root.insert("semanticPreview", preview);
    return QJsonDocument(root).toJson();
}

QByteArray changedAuthored(const char* name, const QJsonValue& value)
{
    auto root = fixtureObject();
    auto preview = root.value("semanticPreview").toObject();
    auto view = preview.value("authored").toObject();
    view.insert(QString::fromLatin1(name), value);
    preview.insert("authored", view);
    root.insert("semanticPreview", preview);
    return QJsonDocument(root).toJson();
}

QJsonObject firstChange()
{
    return fixtureObject().value("semanticPreview").toObject().value("authored")
        .toObject().value("changes").toArray().first().toObject();
}

QByteArray changedDescriptor(const char* side, const QJsonObject& descriptor)
{
    auto change = firstChange();
    change.insert(QString::fromLatin1(side), descriptor);
    return changedAuthored("changes", QJsonArray{change});
}

void expectRefused(const QString& root, const QByteArray& bytes, const QString& name,
    const review::ErrorCode code, const char* message)
{
    const auto result = importBytes(root, bytes, name);
    check(!result.review && result.code == code && !result.error.isEmpty(), message);
}

// Catches swapped snapshots, silently omitted native RGB/ARCH extensions,
// invented zero for an absent optional, and hashes taken from different bytes.
void testSavedSnapshots(const QString& root)
{
    sed::Level source;
    source.kind = sed::ProjectType::MOTS;
    source.header.gravity = 4.0;
    source.lights.resize(1);
    sed::Level candidate = source;
    candidate.header.gravity = 6.0;
    candidate.lights[0].rgbIntensity = -0.0;
    candidate.motsArchAuthoring.push_back({});
    candidate.motsArchAuthoring[0].thingIndex = 7;
    candidate.motsArchAuthoring[0].modelName = "synthetic.3do";
    candidate.motsArchAuthoring[0].origin = sed::MotsArchSampleOrigin::ImportedUnknown;
    sed::Level output = source;
    output.header.gravity = 7.0;
    output.lights[0].rgbIntensity = 0.0;
    std::vector<sed::assets::SedOptionalSection> candidateSections{
        *sed::assets::makeSedLightRgbSection(candidate),
        *sed::assets::makeSedArchAuthoringSection(candidate)};
    std::vector<sed::assets::SedOptionalSection> outputSections{
        *sed::assets::makeSedLightRgbSection(output)};
    const std::array<QByteArray, 3> bytes{
        nativeBytes(source), nativeBytes(candidate, candidateSections), nativeBytes(output, outputSections)};
    const review::SnapshotRequest request{root + "/baseline.sed", root + "/candidate.sed", root + "/output.sed"};
    const std::array<QString, 3> paths{request.baselinePath, request.candidatePath, request.outputPath};
    for ( std::size_t i = 0; i < paths.size(); ++i )
    {
        writeBytes(paths[i], bytes[i]);
    }
    const auto result = review::loadReview(request);
    if ( !check(result.review && result.code == review::ErrorCode::None && result.error.isEmpty(),
        "three saved native snapshots produce a read-only typed review") )
    {
        return;
    }
    const auto& value = *result.review;
    check(value.sourceKind == review::SourceKind::SavedSnapshots && value.originalReceipt.isEmpty(),
        "saved snapshot provenance is distinct from an imported receipt");
    check(value.provenance.contains("saved", Qt::CaseInsensitive)
        && value.provenance.contains("serializer", Qt::CaseInsensitive),
        "saved snapshot provenance explains the serializer-attribution boundary");
    check(value.files.size() == 3, "every captured native snapshot has an identity");
    for ( std::size_t i = 0; i < value.files.size() && i < paths.size(); ++i )
    {
        check(value.files[i].sha256 == hash(bytes[i]) && value.files[i].byteCount == static_cast<std::uint64_t>(bytes[i].size()),
            "identities bind exactly the captured file bytes");
        check(readBytes(paths[i]) == bytes[i], "comparison never changes a snapshot file");
    }
    const auto* gravity = row(value.authored, "/header/gravity");
    check(gravity && doubleBits(gravity->before) == 0x4010000000000000ull
        && doubleBits(gravity->after) == 0x4018000000000000ull,
        "candidate view compares saved gravity four with six");
    const auto* persisted = row(value.persisted, "/header/gravity");
    check(persisted && doubleBits(persisted->after) == 0x401c000000000000ull,
        "output view independently compares baseline with saved gravity seven");
    const auto* rgb = row(value.authored, "/lights/0/rgbIntensity");
    check(rgb && rgb->kind == semantic::ChangeKind::Presence && !rgb->before.value
        && doubleBits(rgb->after) == 0x8000000000000000ull,
        "native RGB metadata preserves optional absence and negative-zero bits");
    const auto* outputRgb = row(value.persisted, "/lights/0/rgbIntensity");
    check(outputRgb && doubleBits(outputRgb->after) == 0,
        "native output positive zero remains distinct from candidate negative zero");
    check(row(value.authored, "/motsArchAuthoring/0/modelName") != nullptr,
        "native ARCH authoring metadata participates in the model review");
    const QString report = root + "/snapshots-report.json";
    check(review::exportReviewCreateOnly(value, report).success,
        "saved snapshot review exports a new report");
    if ( QFile::exists(report) )
    {
        const auto exported = QJsonDocument::fromJson(readBytes(report)).object();
        check(exported.value("reportKind").toString() == "saved-snapshot-comparison"
            && exported.value("provenance").isObject(),
            "snapshot report has explicit provenance outside semantic data");
        const auto semanticData = exported.value("semanticPreview").toObject();
        const auto changes = semanticData.value("authored").toObject().value("changes").toArray();
        bool foundBits{};
        for ( const auto& entry : changes )
        {
            const auto change = entry.toObject();
            if ( change.value("path").toString() == "/lights/0/rgbIntensity" )
            {
                foundBits = change.value("before").toObject().size() == 1
                    && change.value("after").toObject().value("bits").toString() == "8000000000000000";
            }
        }
        check(foundBits, "export preserves exact descriptor shape and float bits");
    }
}

// Catches opaque optional bytes being treated as model changes or erased.
void testOpaqueSectionsAndRepairs(const QString& root)
{
    sed::Level level;
    const sed::assets::SedOptionalSection first{{'t', 'e', 's', 't'}, {std::byte{1}}};
    const sed::assets::SedOptionalSection second{{'t', 'e', 's', 't'}, {std::byte{2}}};
    const QString before = root + "/opaque-before.sed";
    const QString after = root + "/opaque-after.sed";
    const auto beforeBytes = nativeBytes(level, {first});
    writeBytes(before, beforeBytes);
    writeBytes(after, nativeBytes(level, {second}));
    const auto result = review::loadReview(review::SnapshotRequest{before, after, after});
    if ( check(static_cast<bool>(result.review), "opaque optional sections admit a native model review") )
    {
        check(result.review->authored.comparisonComplete && result.review->authored.changed == false
            && result.review->files[0].sha256 != result.review->files[1].sha256,
            "zero covered-model changes do not assert identical native bytes");
    }
    // The first protected HEAD slot begins at byte 4096. Damaging one checked
    // byte is within its four-slot parity budget and must remain visible.
    auto damaged = beforeBytes;
    if ( damaged.size() > 4096 )
    {
        damaged[4096] = static_cast<char>(damaged[4096] ^ 0x40);
        const QString repairedPath = root + "/repairable.sed";
        writeBytes(repairedPath, damaged);
        const auto repaired = review::loadReview(review::SnapshotRequest{repairedPath, after, after});
        if ( check(static_cast<bool>(repaired.review), "native strict loader admits transparently repaired complete input") )
        {
            check(repaired.review->files[0].nativeHadCorruption
                && repaired.review->files[0].nativeRepairedChunks > 0
                && !repaired.review->warnings.isEmpty(),
                "native repair is explicit in identity and review warning");
            check(repaired.review->files[0].sha256 == hash(damaged)
                && readBytes(repairedPath) == damaged,
                "repair provenance retains hash of damaged captured bytes without rewriting");
        }
    }
}

// Catches unsupported parsers, input allocation before admission, and stopping
// after cancellation. Sparse oversize fixture needs no large in-memory buffer.
void testAdmissionAndCancellation(const QString& root)
{
    const QString unsupported = root + "/foreign.jkl";
    writeBytes(unsupported, "SECTION: HEADER\nVERSION 1\n");
    const auto unsupportedResult = review::loadReview(review::SnapshotRequest{unsupported, unsupported, unsupported});
    check(!unsupportedResult.review && unsupportedResult.code == review::ErrorCode::UnsupportedFormat,
        "text snapshot formats refuse before invoking an unbounded parser");
    const QString oversize = root + "/oversized.sed";
    QFile file(oversize);
    if ( !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || !file.resize(static_cast<qint64>(review::kMaxSnapshotBytes + 1)) )
    {
        throw std::runtime_error("Cannot create sparse over-budget fixture");
    }
    file.close();
    const auto tooLarge = review::loadReview(review::SnapshotRequest{oversize, oversize, oversize});
    check(!tooLarge.review && tooLarge.code == review::ErrorCode::InputTooLarge,
        "oversized native file is refused before decoding or byte allocation");
    const QString malformed = root + "/malformed.sed";
    writeBytes(malformed, "not a native level");
    const auto invalid = review::loadReview(review::SnapshotRequest{malformed, malformed, malformed});
    check(!invalid.review && invalid.code == review::ErrorCode::InvalidNative,
        "malformed native framing refuses instead of returning an empty model");
    sed::Level level;
    sed::assets::SedOptionalSection arca;
    arca.type = sed::assets::SedArchAuthoringSectionType;
    arca.payload = {std::byte{1}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const QString hostile = root + "/malformed-arca.sed";
    writeBytes(hostile, nativeBytes(level, {arca}));
    const auto rejectedExtension = review::loadReview(review::SnapshotRequest{hostile, hostile, hostile});
    check(!rejectedExtension.review && rejectedExtension.code == review::ErrorCode::InvalidNative,
        "native ARCH extension count is admitted before its count-driven allocation");
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = review::loadReview(review::ReceiptRequest{root + "/does-not-exist.json"}, stop.get_token());
    check(!cancelled.review && cancelled.code == review::ErrorCode::Cancelled,
        "already-cancelled request never opens its path");
    const auto empty = review::loadReview(review::ReceiptRequest{});
    check(!empty.review && empty.code == review::ErrorCode::InvalidRequest,
        "empty request path is explicitly rejected");
}

// Catches receipt reformatting, treating asserted hashes as recomputed, and
// accidental overwrite even if a destination appears after the save chooser.
void testImportedIdentityAndCreateOnly(const QString& root)
{
    const auto bytes = receiptBytes();
    const auto result = importBytes(root, bytes, "receipt");
    if ( !check(static_cast<bool>(result.review), "a complete hand-authored plan receipt is accepted") )
    {
        return;
    }
    const auto& value = *result.review;
    check(value.sourceKind == review::SourceKind::ImportedReceipt && value.receiptCommand == "plan"
        && value.originalReceipt == bytes, "receipt keeps its command, source kind, and exact original bytes");
    check(value.files.size() == 1 && value.files.front().sha256 == hash(bytes)
        && value.files.front().byteCount == static_cast<std::uint64_t>(bytes.size()),
        "receipt file identity binds the full imported bytes");
    check(value.provenance.contains("not recomputed", Qt::CaseInsensitive),
        "imported receipt is never labeled as recomputed evidence");
    check(value.authored.totalChanges == 1 && value.persisted.changed == false,
        "receipt authored and persisted results remain independent");
    const QString destination = root + "/receipt-export.json";
    check(review::exportReviewCreateOnly(value, destination).success
        && readBytes(destination) == bytes, "receipt export preserves whitespace and numeric spelling exactly");
    const auto denied = review::exportReviewCreateOnly(value, destination);
    check(!denied.success && denied.code == review::ErrorCode::OutputExists
        && readBytes(destination) == bytes, "report export cannot overwrite an existing path");
    const auto directory = review::exportReviewCreateOnly(value, root);
    check(!directory.success, "report export refuses a directory destination");
    const QString link = root + "/receipt-link.json";
    if ( QFile::link(destination, link) )
    {
        check(!review::exportReviewCreateOnly(value, link).success && readBytes(destination) == bytes,
            "report export refuses an existing link without changing its target");
    }
    std::stop_source stop;
    stop.request_stop();
    const QString cancelledPath = root + "/cancelled-export.json";
    const auto cancelled = review::exportReviewCreateOnly(value, cancelledPath, stop.get_token());
    check(!cancelled.success && cancelled.code == review::ErrorCode::Cancelled && !QFile::exists(cancelledPath),
        "cancelled export leaves no destination file");
}

// Each mutation violates a specific receipt invariant; acceptance would misstate
// the amount or exact kind of evidence provided by an untrusted external file.
void testReceiptRefusals(const QString& root)
{
    expectRefused(root, "{", "bad-json", review::ErrorCode::InvalidReceipt, "malformed receipt JSON refuses");
    auto duplicate = receiptBytes();
    duplicate.replace("\"ok\": true", "\"ok\": false, \"ok\": true");
    expectRefused(root, duplicate, "duplicate-key", review::ErrorCode::InvalidReceipt,
        "duplicate object keys refuse rather than adopting last-wins claims");
    expectRefused(root, changedPreview("version", 2), "new-version", review::ErrorCode::InvalidReceipt,
        "unknown semantic version refuses");
    expectRefused(root, changedPreview("excludedFields", QJsonArray{}), "missing-exclusions", review::ErrorCode::InvalidReceipt,
        "an incomplete exclusion declaration refuses");
    expectRefused(root, changedPreview("inputSha256", QString(64, 'c')), "hash-mismatch", review::ErrorCode::InvalidReceipt,
        "receipt and preview hash mismatch refuses");
    expectRefused(root, changedAuthored("changed", false), "false-change", review::ErrorCode::InvalidReceipt,
        "changed=false cannot conceal observed changes");
    expectRefused(root, changedAuthored("totalChanges", 2), "false-total", review::ErrorCode::InvalidReceipt,
        "complete total must equal observed changes");
    expectRefused(root, changedAuthored("detailsComplete", false), "false-incomplete", review::ErrorCode::InvalidReceipt,
        "detail incompleteness requires evidence of omitted details");
    auto descriptor = firstChange().value("after").toObject();
    descriptor.insert("value", 7);
    expectRefused(root, changedDescriptor("after", descriptor), "float-value-mismatch", review::ErrorCode::InvalidReceipt,
        "finite float JSON value must agree with its IEEE bits");
    descriptor = firstChange().value("after").toObject();
    descriptor.insert("classification", "nan");
    expectRefused(root, changedDescriptor("after", descriptor), "float-class-mismatch", review::ErrorCode::InvalidReceipt,
        "float classification must agree with its IEEE bits");
    descriptor = QJsonObject{{"present", false}, {"value", 0}};
    expectRefused(root, changedDescriptor("before", descriptor), "absence-value", review::ErrorCode::InvalidReceipt,
        "absence cannot carry an invented value");
    descriptor = QJsonObject{{"present", true}, {"type", "uint32"}, {"value", -1}};
    expectRefused(root, changedDescriptor("after", descriptor), "negative-unsigned", review::ErrorCode::InvalidReceipt,
        "unsigned descriptor rejects negative numbers");
    descriptor = QJsonObject{{"present", true}, {"type", "string"}, {"byteCount", 2}, {"encoding", "hex"}, {"value", "ff"}};
    expectRefused(root, changedDescriptor("after", descriptor), "hex-length", review::ErrorCode::InvalidReceipt,
        "hex string encoding must account for every declared byte");
    descriptor = QJsonObject{{"present", true}, {"type", "string"}, {"byteCount", 1}, {"omitted", true}};
    expectRefused(root, changedDescriptor("after", descriptor), "false-omission", review::ErrorCode::InvalidReceipt,
        "an inline-admissible string cannot claim unexplained omission");
    auto work = QJsonObject{{"visitedSlots", 1000001}, {"comparedStringBytes", 0}, {"inlineStringBytes", 0}};
    expectRefused(root, changedAuthored("work", work), "over-work", review::ErrorCode::InvalidReceipt,
        "reported work cannot exceed declared limits");
    QByteArray deep = "{\"extra\":";
    for ( std::size_t i = 0; i < review::kMaxReceiptDepth + 1; ++i )
    {
        deep += '[';
    }
    deep += '0';
    for ( std::size_t i = 0; i < review::kMaxReceiptDepth + 1; ++i )
    {
        deep += ']';
    }
    deep += '}';
    expectRefused(root, deep, "depth", review::ErrorCode::ReceiptTooComplex,
        "receipt nesting is bounded before Qt materializes the tree");
    QByteArray broad = "{\"extra\":[";
    for ( std::size_t i = 0; i < review::kMaxReceiptNodes; ++i )
    {
        broad += "0,";
    }
    broad += "0]}";
    expectRefused(root, broad, "nodes", review::ErrorCode::ReceiptTooComplex,
        "the entire receipt tree shares a preparse node budget");
    expectRefused(root, QByteArray(static_cast<qsizetype>(review::kMaxReceiptBytes + 1), ' '),
        "receipt-bytes", review::ErrorCode::InputTooLarge, "receipt bytes are bounded before JSON parsing");
}

// Catches evaluation of nonfinite floats, loss of signed zero, and replacing an
// absent optional with zero while converting imported descriptors to core types.
void testExactImportedValues(const QString& root)
{
    auto change = firstChange();
    change.insert("before", QJsonObject{{"present", true}, {"type", "float64"},
        {"bits", "8000000000000000"}, {"classification", "negativeZero"}, {"value", 0}});
    change.insert("after", QJsonObject{{"present", true}, {"type", "float64"},
        {"bits", "7ff8000000000042"}, {"classification", "nan"}, {"value", QJsonValue::Null}});
    const auto bitsResult = importBytes(root, changedAuthored("changes", QJsonArray{change}), "exact-bits");
    if ( check(static_cast<bool>(bitsResult.review), "nonfinite and signed-zero receipt descriptors are accepted exactly") )
    {
        const auto& actual = bitsResult.review->authored.changes.front();
        check(doubleBits(actual.before) == 0x8000000000000000ull
            && doubleBits(actual.after) == 0x7ff8000000000042ull,
            "import preserves negative-zero sign and NaN payload without evaluating it");
    }
    change.insert("kind", "presence");
    change.insert("before", QJsonObject{{"present", false}});
    const auto presence = importBytes(root, changedAuthored("changes", QJsonArray{change}), "exact-absence");
    check(presence.review && !presence.review->authored.changes.front().before.value,
        "absent imported optional remains a disengaged typed descriptor");
}

// Catches rejecting real partial-result contracts or accepting claimed complete
// work with no root visit, fabricated roots, or noncanonical string encoding.
void testPartialReceiptsAndCoverage(const QString& root)
{
    auto source = fixtureObject();
    auto preview = source.value("semanticPreview").toObject();
    auto view = preview.value("authored").toObject();
    view.insert("comparisonComplete", false);
    view.insert("detailsComplete", false);
    view.insert("changed", QJsonValue::Null);
    view.insert("observedChanges", 0);
    view.insert("totalChanges", QJsonValue::Null);
    view.insert("changes", QJsonArray{});
    view.insert("reasons", QJsonArray{"comparisonSlots"});
    view.insert("work", QJsonObject{{"visitedSlots", 1000000}, {"comparedStringBytes", 0}, {"inlineStringBytes", 0}});
    preview.insert("authored", view);
    source.insert("semanticPreview", preview);
    const auto partial = importBytes(root, QJsonDocument(source).toJson(), "partial-slots");
    check(partial.review && !partial.review->authored.comparisonComplete
        && !partial.review->authored.changed && !partial.review->authored.totalChanges,
        "partial comparison retains unknown changed and total instead of inventing zero");

    source = fixtureObject();
    preview = source.value("semanticPreview").toObject();
    auto limits = preview.value("limits").toObject();
    limits.insert("maxChanges", 1);
    preview.insert("limits", limits);
    view = preview.value("authored").toObject();
    view.insert("detailsComplete", false);
    view.insert("observedChanges", 2);
    view.insert("totalChanges", 2);
    view.insert("reasons", QJsonArray{"changeLimit"});
    preview.insert("authored", view);
    source.insert("semanticPreview", preview);
    const auto truncated = importBytes(root, QJsonDocument(source).toJson(), "partial-detail");
    check(truncated.review && truncated.review->authored.comparisonComplete
        && !truncated.review->authored.detailsComplete && truncated.review->authored.totalChanges == 2
        && truncated.review->authored.changes.size() == 1,
        "detail truncation preserves an exact complete count independently of emitted rows");

    auto change = firstChange();
    change.insert("path", "/lvisstring");
    change.insert("before", QJsonObject{{"present", true}, {"type", "string"},
        {"byteCount", 1}, {"encoding", "utf8"}, {"value", "a"}});
    change.insert("after", QJsonObject{{"present", true}, {"type", "string"},
        {"byteCount", 513}, {"omitted", true}});
    source = fixtureObject();
    preview = source.value("semanticPreview").toObject();
    view = preview.value("authored").toObject();
    view.insert("detailsComplete", false);
    view.insert("changes", QJsonArray{change});
    view.insert("reasons", QJsonArray{"inlineStringLength"});
    view.insert("work", QJsonObject{{"visitedSlots", 68}, {"comparedStringBytes", 514}, {"inlineStringBytes", 1}});
    preview.insert("authored", view);
    source.insert("semanticPreview", preview);
    const auto omitted = importBytes(root, QJsonDocument(source).toJson(), "omitted-value");
    if ( check(static_cast<bool>(omitted.review), "explicit string omission imports within recorded budgets") )
    {
        const auto& descriptor = omitted.review->authored.changes.front().after;
        const auto* string = descriptor.value ? std::get_if<semantic::StringValue>(&*descriptor.value) : nullptr;
        check(string && string->byteCount == 513 && !string->encodedValue,
            "omitted string retains byte count and explicit absence of inline bytes");
    }

    change.insert("after", QJsonObject{{"present", true}, {"type", "string"},
        {"byteCount", 1}, {"encoding", "hex"}, {"value", "ff"}});
    view.insert("detailsComplete", true);
    view.insert("changes", QJsonArray{change});
    view.insert("reasons", QJsonArray{});
    view.insert("work", QJsonObject{{"visitedSlots", 68}, {"comparedStringBytes", 2}, {"inlineStringBytes", 2}});
    preview.insert("authored", view);
    source.insert("semanticPreview", preview);
    check(static_cast<bool>(importBytes(root, QJsonDocument(source).toJson(), "invalid-utf8-hex").review),
        "hex encoding retains an invalid UTF-8 byte exactly");
    change.insert("after", QJsonObject{{"present", true}, {"type", "string"},
        {"byteCount", 1}, {"encoding", "hex"}, {"value", "62"}});
    view.insert("changes", QJsonArray{change});
    preview.insert("authored", view);
    source.insert("semanticPreview", preview);
    expectRefused(root, QJsonDocument(source).toJson(), "valid-utf8-as-hex", review::ErrorCode::InvalidReceipt,
        "hex encoding cannot contradict the v1 invalid-UTF-8-only descriptor contract");

    source = fixtureObject();
    preview = source.value("semanticPreview").toObject();
    view = preview.value("persisted").toObject();
    view.insert("work", QJsonObject{{"visitedSlots", 0}, {"comparedStringBytes", 0}, {"inlineStringBytes", 0}});
    preview.insert("persisted", view);
    source.insert("semanticPreview", preview);
    expectRefused(root, QJsonDocument(source).toJson(), "complete-without-root", review::ErrorCode::InvalidReceipt,
        "a complete comparison cannot omit the mandatory root visit");
    for ( const QString& path : {QStringLiteral("/bogus"), QStringLiteral("/retainedJklSourceText/x")} )
    {
        change = firstChange();
        change.insert("path", path);
        expectRefused(root, changedAuthored("changes", QJsonArray{change}),
            path == "/bogus" ? "unknown-root" : "excluded-root-child", review::ErrorCode::InvalidReceipt,
            "receipt paths must start in a covered model root and cannot enter an excluded field");
    }
}

// Optional integration mode consumes independently captured legacy CLI receipts
// without running a CLI from production code or rewriting their numeric tokens.
void testExternalReceipts(const QString& root, const int argc, char** argv)
{
    if ( argc < 2 )
    {
        return;
    }
    if ( QString::fromUtf8(argv[1]) != "--receipts" )
    {
        throw std::runtime_error("Unknown semantic review test argument");
    }
    for ( int index = 2; index < argc; ++index )
    {
        const QString path = QString::fromUtf8(argv[index]);
        const auto original = readBytes(path);
        const auto imported = review::loadReview(review::ReceiptRequest{path});
        if ( !check(static_cast<bool>(imported.review), "independently captured agent receipt imports") )
        {
            std::cerr << "Receipt: " << path.toStdString() << ": " << imported.error.toStdString() << '\n';
            continue;
        }
        check(imported.review->originalReceipt == original && imported.review->files.size() == 1
            && imported.review->files[0].sha256 == hash(original),
            "real receipt import retains full original bytes and identity");
        const QString output = root + "/external-receipt-" + QString::number(index) + ".json";
        check(review::exportReviewCreateOnly(*imported.review, output).success && readBytes(output) == original,
            "real receipt export preserves every original token byte");
    }
    std::cout << "External receipts checked: " << argc - 2 << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if ( !directory.isValid() )
    {
        std::cerr << "Cannot create isolated semantic review test directory\n";
        return 2;
    }
    try
    {
        testSavedSnapshots(directory.path());
        testOpaqueSectionsAndRepairs(directory.path());
        testAdmissionAndCancellation(directory.path());
        testImportedIdentityAndCreateOnly(directory.path());
        testReceiptRefusals(directory.path());
        testExactImportedValues(directory.path());
        testPartialReceiptsAndCoverage(directory.path());
        testExternalReceipts(directory.path(), argc, argv);
    }
    catch ( const std::exception& error )
    {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 2;
    }
    if ( failures != 0 )
    {
        std::cerr << failures << " semantic review I/O assertions failed\n";
        return 1;
    }
    std::cout << "semantic review I/O tests passed\n";
    return 0;
}
