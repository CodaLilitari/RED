#include "sed/update/updater.hpp"
#include "sed/float_chars.hpp"

#include "sed/assets/sed.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <utility>

namespace sed::update
{
namespace fs = std::filesystem;
namespace
{

void fail(UpdateFailure* out, std::string code, std::string message)
{
    if (out) *out = UpdateFailure{ std::move(code), std::move(message) };
}

std::string lower(std::string_view text)
{
    std::string result(text);
    for (char& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

bool isSafeRelativePath(std::string_view path)
{
    if (path.empty() || path.size() > 512) return false;
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    std::string_view rest = path;
    while (!rest.empty())
    {
        const std::size_t slash = rest.find_first_of("/\\");
        const std::string_view part = rest.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    }
    return true;
}

// Minimal bounded JSON reader for the feed and manifest shapes.
struct Json
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{ Type::Null };
    bool boolean{};
    double number{};
    std::string str{};
    std::vector<Json> array{};
    std::map<std::string, Json> object{};
    [[nodiscard]] const Json* find(std::string_view key) const
    {
        if (type != Type::Object) return nullptr;
        const auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }
};

struct JsonParser
{
    std::string_view text{};
    std::size_t pos{};
    int depth{};
    bool parseValue(Json& out)
    {
        if (++depth > 32) return false;
        skipWs();
        bool ok = false;
        if (pos >= text.size()) ok = false;
        else if (text[pos] == '{') ok = parseObject(out);
        else if (text[pos] == '[') ok = parseArray(out);
        else if (text[pos] == '"') { out.type = Json::Type::String; ok = parseString(out.str); }
        else if (text.substr(pos, 4) == "true") { pos += 4; out.type = Json::Type::Bool; out.boolean = true; ok = true; }
        else if (text.substr(pos, 5) == "false") { pos += 5; out.type = Json::Type::Bool; ok = true; }
        else if (text.substr(pos, 4) == "null") { pos += 4; ok = true; }
        else ok = parseNumber(out);
        --depth;
        return ok;
    }
    void skipWs()
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) ++pos;
    }
    bool parseObject(Json& out)
    {
        out.type = Json::Type::Object; ++pos; skipWs();
        if (pos < text.size() && text[pos] == '}') { ++pos; return true; }
        while (true)
        {
            skipWs(); std::string key;
            if (pos >= text.size() || text[pos] != '"' || !parseString(key)) return false;
            skipWs();
            if (pos >= text.size() || text[pos] != ':') return false;
            ++pos; Json value;
            if (!parseValue(value)) return false;
            out.object[std::move(key)] = std::move(value);
            skipWs();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == '}') { ++pos; return true; }
            return false;
        }
    }
    bool parseArray(Json& out)
    {
        out.type = Json::Type::Array; ++pos; skipWs();
        if (pos < text.size() && text[pos] == ']') { ++pos; return true; }
        while (true)
        {
            Json value;
            if (!parseValue(value)) return false;
            out.array.push_back(std::move(value));
            if (out.array.size() > 100'000) return false;
            skipWs();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == ']') { ++pos; return true; }
            return false;
        }
    }
    bool parseString(std::string& out)
    {
        ++pos; out.clear();
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (pos >= text.size()) return false;
                const char esc = text[pos++];
                switch (esc)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 > text.size()) return false;
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const char h = text[pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code += static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code += static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code += static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    if (code < 0x80) out += static_cast<char>(code);
                    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    break;
                }
                default: return false;
                }
            }
            else if (static_cast<unsigned char>(c) < 0x20) return false;
            else out += c;
        }
        return false;
    }
    bool parseNumber(Json& out)
    {
        const std::size_t start = pos;
        if (pos < text.size() && text[pos] == '-') ++pos;
        while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-')) ++pos;
        if (pos == start) return false;
        out.type = Json::Type::Number;
        return sed::text::fromChars(text.data() + start, text.data() + pos, out.number).ec == std::errc{};
    }
};

std::optional<Json> parseJson(std::string_view text)
{
    JsonParser parser{ text };
    Json out;
    if (!parser.parseValue(out)) return std::nullopt;
    parser.skipWs();
    if (parser.pos != text.size()) return std::nullopt;
    return out;
}

std::optional<std::string> readBoundedFile(const fs::path& path, std::uint64_t maxBytes, UpdateFailure* failure, const char* code)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) { fail(failure, "io-error", "cannot stat " + path.string()); return std::nullopt; }
    if (size > maxBytes) { fail(failure, code, "file exceeds the size limit: " + path.string()); return std::nullopt; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { fail(failure, "io-error", "cannot open " + path.string()); return std::nullopt; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) { fail(failure, "io-error", "cannot read " + path.string()); return std::nullopt; }
    return buffer.str();
}

bool checkManifestFiles(const Json& root, std::vector<ManifestFile>& files, UpdateFailure* failure)
{
    const Json* list = root.find("files");
    if (!list || list->type != Json::Type::Array) { fail(failure, "malformed-manifest", "manifest has no files array"); return false; }
    if (list->array.size() > kMaxManifestFiles) { fail(failure, "malformed-manifest", "manifest exceeds the file-count limit"); return false; }
    std::set<std::string> seen;
    for (const Json& item : list->array)
    {
        const Json* path = item.find("path");
        const Json* bytes = item.find("bytes");
        const Json* hash = item.find("sha256");
        if (!path || path->type != Json::Type::String || !bytes || bytes->type != Json::Type::Number
            || !hash || hash->type != Json::Type::String)
        { fail(failure, "malformed-manifest", "manifest file entry is incomplete"); return false; }
        if (!isSafeRelativePath(path->str)) { fail(failure, "unsafe-path", "manifest path is not a safe relative path: " + path->str); return false; }
        if (!seen.insert(lower(path->str)).second) { fail(failure, "malformed-manifest", "duplicate manifest path: " + path->str); return false; }
        const std::string digest = lower(hash->str);
        if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
        { fail(failure, "malformed-manifest", "manifest sha256 is malformed for " + path->str); return false; }
        if (bytes->number < 0 || bytes->number > static_cast<double>(kMaxPackageFileBytes))
        { fail(failure, "malformed-manifest", "manifest size is out of range for " + path->str); return false; }
        files.push_back(ManifestFile{ path->str, static_cast<std::uint64_t>(bytes->number), digest });
    }
    return true;
}

std::string jsonEscape(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string receiptsJson(std::string_view version, const std::vector<ManifestFile>& files)
{
    std::string out = "{\n  \"format\": 1,\n  \"version\": \"" + jsonEscape(version) + "\",\n  \"files\": [\n";
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        out += "    {\"path\":\"" + jsonEscape(files[i].path) + "\",\"bytes\":" + std::to_string(files[i].bytes)
            + ",\"sha256\":\"" + files[i].sha256 + "\"}" + (i + 1 < files.size() ? "," : "") + "\n";
    }
    out += "  ]\n}\n";
    return out;
}

struct ParsedVersion
{
    std::vector<std::uint64_t> numbers{};
    std::vector<std::string> prerelease{};
};

std::optional<ParsedVersion> parseVersion(std::string_view text)
{
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    ParsedVersion out;
    const std::size_t dash = text.find('-');
    const std::string_view core = text.substr(0, dash);
    if (core.empty()) return std::nullopt;
    std::size_t start = 0;
    while (start <= core.size())
    {
        const std::size_t dot = core.find('.', start);
        const std::string_view part = core.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (part.empty() || part.size() > 10) return std::nullopt;
        std::uint64_t value = 0;
        for (const char c : part)
        {
            if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
            value = value * 10 + static_cast<std::uint64_t>(c - '0');
        }
        out.numbers.push_back(value);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (dash != std::string_view::npos)
    {
        const std::string_view pre = text.substr(dash + 1);
        if (pre.empty()) return std::nullopt;
        std::size_t at = 0;
        while (at <= pre.size())
        {
            const std::size_t dot = pre.find('.', at);
            const std::string_view part = pre.substr(at, dot == std::string_view::npos ? dot : dot - at);
            if (part.empty()) return std::nullopt;
            for (const char c : part)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return std::nullopt;
            out.prerelease.emplace_back(part);
            if (dot == std::string_view::npos) break;
            at = dot + 1;
        }
    }
    return out;
}

bool sameReceipts(const std::vector<ManifestFile>& a, const std::vector<ManifestFile>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].path != b[i].path || a[i].bytes != b[i].bytes || lower(a[i].sha256) != lower(b[i].sha256)) return false;
    return true;
}

} // namespace

std::optional<int> compareVersions(std::string_view lhs, std::string_view rhs)
{
    const auto a = parseVersion(lhs);
    const auto b = parseVersion(rhs);
    if (!a || !b) return std::nullopt;
    const std::size_t count = std::max(a->numbers.size(), b->numbers.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint64_t x = i < a->numbers.size() ? a->numbers[i] : 0;
        const std::uint64_t y = i < b->numbers.size() ? b->numbers[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (a->prerelease.empty() != b->prerelease.empty())
        return a->prerelease.empty() ? 1 : -1;
    const std::size_t pre = std::max(a->prerelease.size(), b->prerelease.size());
    for (std::size_t i = 0; i < pre; ++i)
    {
        if (i >= a->prerelease.size()) return -1;
        if (i >= b->prerelease.size()) return 1;
        const std::string& x = a->prerelease[i];
        const std::string& y = b->prerelease[i];
        const bool xNum = std::all_of(x.begin(), x.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        const bool yNum = std::all_of(y.begin(), y.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        if (xNum && yNum)
        {
            const std::uint64_t xv = std::stoull(x);
            const std::uint64_t yv = std::stoull(y);
            if (xv != yv) return xv < yv ? -1 : 1;
        }
        else if (xNum != yNum) return xNum ? -1 : 1;
        else if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::string sha256Hex(std::string_view bytes)
{
    const auto* data = reinterpret_cast<const std::byte*>(bytes.data());
    const std::array<std::byte, 32> digest =
        sed::assets::calculateSedSha256(std::span<const std::byte>(data, bytes.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const std::byte byte : digest)
    {
        const auto value = static_cast<unsigned char>(byte);
        out.push_back(kHex[value >> 4]);
        out.push_back(kHex[value & 0x0f]);
    }
    return out;
}

std::optional<std::string> sha256FileHex(const fs::path& path, UpdateFailure* failure)
{
    const auto text = readBoundedFile(path, kMaxPackageFileBytes, failure, "file-too-large");
    if (!text) return std::nullopt;
    return sha256Hex(*text);
}

std::optional<Feed> parseFeed(std::string_view text, UpdateFailure* failure)
{
    if (text.size() > kMaxFeedBytes) { fail(failure, "feed-too-large", "update feed exceeds the size limit"); return std::nullopt; }
    const auto root = parseJson(text);
    if (!root || root->type != Json::Type::Object) { fail(failure, "malformed-feed", "update feed is not a JSON object"); return std::nullopt; }
    const Json* releases = root->find("releases");
    if (!releases || releases->type != Json::Type::Array || releases->array.empty())
    { fail(failure, "malformed-feed", "update feed has no releases"); return std::nullopt; }
    Feed feed;
    for (const Json& item : releases->array)
    {
        const Json* version = item.find("version");
        const Json* package = item.find("package");
        const Json* channel = item.find("channel");
        if (!version || version->type != Json::Type::String || !package || package->type != Json::Type::String)
        { fail(failure, "malformed-feed", "feed release is missing version or package"); return std::nullopt; }
        if (!compareVersions(version->str, version->str)) { fail(failure, "malformed-feed", "feed release version is malformed: " + version->str); return std::nullopt; }
        if (!isSafeRelativePath(package->str)) { fail(failure, "unsafe-path", "feed package path is unsafe: " + package->str); return std::nullopt; }
        FeedRelease release;
        release.version = version->str;
        release.package = package->str;
        release.channel = channel && channel->type == Json::Type::String ? channel->str : "stable";
        feed.releases.push_back(std::move(release));
    }
    return feed;
}

std::optional<ReleaseManifest> parseReleaseManifest(std::string_view text, UpdateFailure* failure)
{
    const auto root = parseJson(text);
    if (!root || root->type != Json::Type::Object) { fail(failure, "malformed-manifest", "release manifest is not a JSON object"); return std::nullopt; }
    if (const Json* format = root->find("format"))
    {
        if (format->type != Json::Type::Number || format->number != 1.0)
        { fail(failure, "malformed-manifest", "unsupported release manifest format"); return std::nullopt; }
    }
    const Json* version = root->find("version");
    if (!version || version->type != Json::Type::String || version->str.empty())
    { fail(failure, "malformed-manifest", "release manifest has no version"); return std::nullopt; }
    ReleaseManifest manifest;
    manifest.version = version->str;
    if (!checkManifestFiles(*root, manifest.files, failure)) return std::nullopt;
    return manifest;
}

std::optional<std::string> FileUpdateSource::fetchFeedText(UpdateFailure* failure)
{
    return readBoundedFile(feedPath_, kMaxFeedBytes, failure, "feed-too-large");
}

std::optional<fs::path> FileUpdateSource::packageDirectory(const FeedRelease& release, UpdateFailure* failure)
{
    if (!isSafeRelativePath(release.package)) { fail(failure, "unsafe-path", "feed package path is unsafe: " + release.package); return std::nullopt; }
    const fs::path dir = feedPath_.parent_path() / fs::u8path(release.package);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { fail(failure, "io-error", "package directory does not exist: " + dir.string()); return std::nullopt; }
    return dir;
}

std::optional<FeedRelease> selectUpdate(const Feed& feed, std::string_view currentVersion, std::string_view channel, UpdateFailure* failure)
{
    if (!compareVersions(currentVersion, currentVersion)) { fail(failure, "malformed-version", "current version is malformed"); return std::nullopt; }
    std::optional<FeedRelease> best;
    bool sawChannel = false;
    bool sawEqualOrOlder = false;
    for (const FeedRelease& release : feed.releases)
    {
        if (release.channel != channel) continue;
        sawChannel = true;
        const auto cmp = compareVersions(release.version, currentVersion);
        if (!cmp) { fail(failure, "malformed-feed", "feed release version is malformed: " + release.version); return std::nullopt; }
        if (*cmp <= 0) { sawEqualOrOlder = true; continue; }
        if (!best || *compareVersions(release.version, best->version) > 0) best = release;
    }
    if (best) return best;
    if (sawChannel && sawEqualOrOlder)
    {
        // Distinguish equal from downgrade-only for a precise refusal.
        bool anyEqual = false;
        for (const FeedRelease& release : feed.releases)
            if (release.channel == channel && *compareVersions(release.version, currentVersion) == 0) anyEqual = true;
        if (anyEqual) fail(failure, "equal-version", "the feed offers the running version; equal versions are refused");
        else fail(failure, "downgrade-refused", "the feed offers only older versions; downgrades are refused");
    }
    else fail(failure, "no-release", "the feed has no release on this channel");
    return std::nullopt;
}

fs::path stagingDirectoryFor(const fs::path& installDir, std::string_view version)
{
    return installDir.parent_path() / (installDir.filename().string() + ".staging-" + std::string(version));
}

fs::path activatedDirectoryFor(const fs::path& installDir, std::string_view version)
{
    return installDir.parent_path() / (installDir.filename().string() + "-" + std::string(version));
}

bool verifyDirectory(const fs::path& dir, bool requireStagedReceipt, UpdateFailure* failure)
{
    const auto manifestText = readBoundedFile(dir / std::string(kManifestFileName), kMaxFeedBytes * 16, failure, "manifest-too-large");
    if (!manifestText) { if (failure && failure->code.empty()) fail(failure, "missing-manifest", "RELEASE_MANIFEST.json is missing"); return false; }
    const auto manifest = parseReleaseManifest(*manifestText, failure);
    if (!manifest) return false;
    std::set<std::string> expected;
    for (const ManifestFile& file : manifest->files)
    {
        expected.insert(lower(file.path));
        const fs::path target = dir / fs::u8path(file.path);
        std::error_code ec;
        if (!fs::is_regular_file(target, ec)) { fail(failure, "missing-file", "packaged file is missing: " + file.path); return false; }
        if (fs::file_size(target, ec) != file.bytes || ec) { fail(failure, "size-mismatch", "packaged file size differs: " + file.path); return false; }
        const auto digest = sha256FileHex(target, failure);
        if (!digest) return false;
        if (*digest != file.sha256) { fail(failure, "hash-mismatch", "packaged file hash differs: " + file.path); return false; }
    }
    if (requireStagedReceipt)
    {
        const auto stagedText = readBoundedFile(dir / std::string(kStagedReceiptFileName), kMaxFeedBytes * 16, failure, "manifest-too-large");
        if (!stagedText) { fail(failure, "not-staged", "STAGED.json receipt is missing; the directory was not staged by the updater"); return false; }
        const auto staged = parseReleaseManifest(*stagedText, failure);
        if (!staged) return false;
        if (staged->version != manifest->version || !sameReceipts(staged->files, manifest->files))
        { fail(failure, "receipt-mismatch", "STAGED.json receipts do not match RELEASE_MANIFEST.json"); return false; }
    }
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec)) continue;
        const std::string rel = entry.path().lexically_relative(dir).generic_string();
        const std::string key = lower(rel);
        if (key == lower(std::string(kManifestFileName))) continue;
        if (requireStagedReceipt && key == lower(std::string(kStagedReceiptFileName))) continue;
        if (!requireStagedReceipt && key == lower(std::string(kStagedReceiptFileName))) continue;
        if (expected.find(key) == expected.end()) { fail(failure, "unexpected-file", "directory contains an unlisted file: " + rel); return false; }
    }
    if (ec) { fail(failure, "io-error", "cannot enumerate directory: " + dir.string()); return false; }
    return true;
}

std::optional<StagedUpdate> stageUpdate(
    IUpdateSource& source, const fs::path& installDir,
    std::string_view currentVersion, std::string_view channel,
    UpdateFailure* failure)
{
    const auto feedText = source.fetchFeedText(failure);
    if (!feedText) return std::nullopt;
    const auto feed = parseFeed(*feedText, failure);
    if (!feed) return std::nullopt;
    const auto release = selectUpdate(*feed, currentVersion, channel, failure);
    if (!release) return std::nullopt;
    const auto packageDir = source.packageDirectory(*release, failure);
    if (!packageDir) return std::nullopt;
    const auto manifestText = readBoundedFile(*packageDir / std::string(kManifestFileName), kMaxFeedBytes * 16, failure, "manifest-too-large");
    if (!manifestText) { fail(failure, "missing-manifest", "package has no RELEASE_MANIFEST.json"); return std::nullopt; }
    const auto manifest = parseReleaseManifest(*manifestText, failure);
    if (!manifest) return std::nullopt;
    if (manifest->version != release->version)
    { fail(failure, "version-mismatch", "package manifest version does not match the feed release"); return std::nullopt; }
    std::error_code ec;
    if (!fs::is_directory(installDir, ec))
    { fail(failure, "io-error", "install directory does not exist: " + installDir.string()); return std::nullopt; }
    const fs::path staging = stagingDirectoryFor(installDir, release->version);
    const fs::path activated = activatedDirectoryFor(installDir, release->version);
    if (fs::exists(staging, ec))
    { fail(failure, "interrupted-staging", "a staging directory already exists; inspect or remove it before retrying: " + staging.string()); return std::nullopt; }
    if (fs::exists(activated, ec))
    { fail(failure, "already-activated", "the activated directory already exists: " + activated.string()); return std::nullopt; }
    // Refuse unlisted package files before copying anything.
    {
        std::set<std::string> listed;
        for (const ManifestFile& file : manifest->files) listed.insert(lower(file.path));
        for (const auto& entry : fs::recursive_directory_iterator(*packageDir, ec))
        {
            if (!entry.is_regular_file(ec)) continue;
            const std::string rel = entry.path().lexically_relative(*packageDir).generic_string();
            if (lower(rel) == lower(std::string(kManifestFileName))) continue;
            if (listed.find(lower(rel)) == listed.end())
            { fail(failure, "unexpected-file", "package contains an unlisted file: " + rel); return std::nullopt; }
        }
        if (ec) { fail(failure, "io-error", "cannot enumerate the package directory"); return std::nullopt; }
    }
    if (!fs::create_directory(staging, ec) || ec)
    { fail(failure, "io-error", "cannot create the staging directory: " + staging.string()); return std::nullopt; }
    const auto abandon = [&]() { std::error_code ignored; fs::remove_all(staging, ignored); };
    const auto copyOne = [&](const fs::path& from, const std::string& rel) {
        const fs::path to = staging / fs::u8path(rel);
        fs::create_directories(to.parent_path(), ec);
        if (ec) return false;
        fs::copy_file(from, to, fs::copy_options::none, ec);
        return !ec;
    };
    if (!copyOne(*packageDir / std::string(kManifestFileName), std::string(kManifestFileName)))
    { abandon(); fail(failure, "io-error", "cannot copy RELEASE_MANIFEST.json into staging"); return std::nullopt; }
    for (const ManifestFile& file : manifest->files)
    {
        if (!copyOne(*packageDir / fs::u8path(file.path), file.path))
        { abandon(); fail(failure, "io-error", "cannot copy packaged file into staging: " + file.path); return std::nullopt; }
        const auto digest = sha256FileHex(staging / fs::u8path(file.path), failure);
        if (!digest) { abandon(); return std::nullopt; }
        std::error_code sizeEc;
        if (fs::file_size(staging / fs::u8path(file.path), sizeEc) != file.bytes || sizeEc || *digest != file.sha256)
        {
            abandon();
            fail(failure, "hash-mismatch", "staged file failed its receipt check: " + file.path);
            return std::nullopt;
        }
    }
    {
        std::ofstream out(staging / std::string(kStagedReceiptFileName), std::ios::binary | std::ios::trunc);
        out << receiptsJson(manifest->version, manifest->files);
        out.flush();
        if (!out) { abandon(); fail(failure, "io-error", "cannot write STAGED.json"); return std::nullopt; }
    }
    if (!verifyDirectory(staging, true, failure)) { abandon(); return std::nullopt; }
    return StagedUpdate{ staging, manifest->version, manifest->files };
}

std::optional<fs::path> activateUpdate(
    const fs::path& stagingDir,
    const std::optional<fs::path>& targetDir,
    UpdateFailure* failure)
{
    if (!verifyDirectory(stagingDir, true, failure)) return std::nullopt;
    const auto manifestText = readBoundedFile(stagingDir / std::string(kManifestFileName), kMaxFeedBytes * 16, failure, "manifest-too-large");
    if (!manifestText) return std::nullopt;
    const auto manifest = parseReleaseManifest(*manifestText, failure);
    if (!manifest) return std::nullopt;
    fs::path target;
    if (targetDir) target = *targetDir;
    else
    {
        const std::string name = stagingDir.filename().string();
        const std::string marker = ".staging-";
        const std::size_t at = name.rfind(marker);
        if (at == std::string::npos)
        { fail(failure, "not-staged", "staging directory name does not carry the .staging- marker"); return std::nullopt; }
        target = stagingDir.parent_path() / (name.substr(0, at) + "-" + manifest->version);
    }
    std::error_code ec;
    if (fs::exists(target, ec))
    { fail(failure, "already-activated", "the activation target already exists: " + target.string()); return std::nullopt; }
    fs::rename(stagingDir, target, ec);
    if (ec) { fail(failure, "io-error", "cannot activate the staged update: " + ec.message()); return std::nullopt; }
    if (!verifyDirectory(target, true, failure))
    {
        // Rename succeeded but receipts no longer verify: report, never
        // fall back to touching the old install.
        fail(failure, "hash-mismatch", "activated directory failed its receipt recheck");
        return std::nullopt;
    }
    return target;
}

} // namespace sed::update

