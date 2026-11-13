#include "sed/assets/gob.hpp"

#include "sed/assets/sed.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <streambuf>

namespace sed::assets
{

namespace
{

// A seekable, non-owning input view lets captured and path-backed archives
// share the exact same index parser without copying the captured payload.
class CapturedInputBuffer final : public std::streambuf
{
public:
    explicit CapturedInputBuffer(std::vector<std::byte>& bytes)
    {
        if ( bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) )
        {
            throw std::runtime_error("Captured GOB is too large for a stream view.");
        }
        char* begin = bytes.empty() ? &empty_ : reinterpret_cast<char*>(bytes.data());
        setg(begin, begin, begin + bytes.size());
    }

protected:
    pos_type seekoff(const off_type offset, const std::ios_base::seekdir origin,
        const std::ios_base::openmode mode) override
    {
        if ( !(mode & std::ios_base::in) || (mode & std::ios_base::out) )
        {
            return pos_type(off_type(-1));
        }
        const off_type size = egptr() - eback();
        off_type base{};
        if ( origin == std::ios_base::cur )
        {
            base = gptr() - eback();
        }
        else if ( origin == std::ios_base::end )
        {
            base = size;
        }
        else if ( origin != std::ios_base::beg )
        {
            return pos_type(off_type(-1));
        }
        if ( offset < -base || offset > size - base )
        {
            return pos_type(off_type(-1));
        }
        const off_type position = base + offset;
        setg(eback(), eback() + position, egptr());
        return pos_type(position);
    }

    pos_type seekpos(const pos_type position, const std::ios_base::openmode mode) override
    {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }

private:
    char empty_{};
};

std::uint32_t readU32LE(std::istream& in)
{
    std::array<unsigned char, 4> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in)
    {
        throw std::runtime_error("Unexpected EOF while reading uint32");
    }
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void validateEntryBounds(
    const GobEntry& entry,
    const std::uint64_t archiveSize,
    const std::filesystem::path& path)
{
    const std::uint64_t offset = entry.offset;
    const std::uint64_t size = entry.size;
    if (offset > archiveSize || size > archiveSize - offset)
    {
        throw std::runtime_error(
            "GOB entry extends past end of file: " + entry.name + " in " + path.string());
    }
}

} // namespace

GobArchive GobArchive::open(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open GOB: " + path.string());
    }

    file.seekg(0, std::ios::end);
    const std::streampos endPosition = file.tellg();
    if (endPosition == std::streampos(-1))
    {
        throw std::runtime_error("Failed to query GOB size: " + path.string());
    }
    const std::uint64_t archiveSize = static_cast<std::uint64_t>(
        static_cast<std::streamoff>(endPosition));
    file.seekg(0, std::ios::beg);
    if (!file)
    {
        throw std::runtime_error("Failed to seek GOB header: " + path.string());
    }

    return parseIndex(file, archiveSize, path);
}

GobArchive GobArchive::fromBytes(std::vector<std::byte> bytes, const std::filesystem::path& sourceLabel)
{
    CapturedInputBuffer buffer(bytes);
    std::istream input(&buffer);
    GobArchive archive = parseIndex(input, bytes.size(), sourceLabel);
    archive.capturedBytes_ = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    return archive;
}

GobArchive GobArchive::parseIndex(std::istream& file, const std::uint64_t archiveSize,
    const std::filesystem::path& path)
{

    std::array<char, 4> magic{};
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!file)
    {
        throw std::runtime_error("Failed to read GOB header: " + path.string());
    }

    GobArchive archive{};
    archive.path_ = path;

    if (magic[0] == 'G' && magic[1] == 'O' && magic[2] == 'B' && magic[3] == '\n')
    {
        const std::uint32_t indexOffset = readU32LE(file);
        constexpr std::uint64_t kEntrySize = 21u;
        if ( indexOffset > archiveSize || archiveSize - indexOffset < 4u )
        {
            throw std::runtime_error("Invalid GOB v1 index offset: " + path.string());
        }
        file.seekg(static_cast<std::streamoff>(indexOffset), std::ios::beg);
        if (!file)
        {
            throw std::runtime_error("Invalid GOB v1 index offset: " + path.string());
        }

        const std::uint32_t numEntries = readU32LE(file);
        if ( numEntries > (archiveSize - indexOffset - 4u) / kEntrySize )
        {
            throw std::runtime_error(
                "Unexpected EOF: GOB v1 index extends past end of file: "
                    + path.string());
        }
        archive.entries_.reserve(numEntries);

        for (std::uint32_t i = 0; i < numEntries; ++i)
        {
            GobEntry entry{};
            entry.offset = readU32LE(file);
            entry.size = readU32LE(file);

            std::array<char, 13> nameBytes{};
            file.read(nameBytes.data(), static_cast<std::streamsize>(nameBytes.size()));
            if (!file)
            {
                throw std::runtime_error("Unexpected EOF while reading GOB v1 entry name: " + path.string());
            }

            entry.name.assign(nameBytes.data(), nameBytes.size());
            const std::size_t nul = entry.name.find('\0');
            if (nul != std::string::npos)
            {
                entry.name.resize(nul);
            }

            validateEntryBounds(entry, archiveSize, path);

            const std::string normalized = normalizeEntryName(entry.name);
            if (!normalized.empty() && !archive.index_.contains(normalized))
            {
                archive.index_.emplace(normalized, archive.entries_.size());
            }
            archive.entries_.push_back(std::move(entry));
        }
    }
    else if (magic[0] == 'G' && magic[1] == 'O' && magic[2] == 'B' && magic[3] == ' ')
    {
        (void)readU32LE(file); // long1
        (void)readU32LE(file); // long2
        const std::uint32_t numEntries = readU32LE(file);
        constexpr std::uint64_t kHeaderSize = 16u;
        constexpr std::uint64_t kEntrySize = 136u;
        if ( archiveSize < kHeaderSize
            || numEntries > (archiveSize - kHeaderSize) / kEntrySize )
        {
            throw std::runtime_error(
                "Unexpected EOF: GOB v2 index extends past end of file: "
                    + path.string());
        }
        archive.entries_.reserve(numEntries);

        for (std::uint32_t i = 0; i < numEntries; ++i)
        {
            GobEntry entry{};
            entry.offset = readU32LE(file);
            entry.size = readU32LE(file);

            std::array<char, 128> nameBytes{};
            file.read(nameBytes.data(), static_cast<std::streamsize>(nameBytes.size()));
            if (!file)
            {
                throw std::runtime_error("Unexpected EOF while reading GOB v2 entry name: " + path.string());
            }

            entry.name.assign(nameBytes.data(), nameBytes.size());
            const std::size_t nul = entry.name.find('\0');
            if (nul != std::string::npos)
            {
                entry.name.resize(nul);
            }

            validateEntryBounds(entry, archiveSize, path);

            const std::string normalized = normalizeEntryName(entry.name);
            if (!normalized.empty() && !archive.index_.contains(normalized))
            {
                archive.index_.emplace(normalized, archive.entries_.size());
            }
            archive.entries_.push_back(std::move(entry));
        }
    }
    else
    {
        throw std::runtime_error("Not a GOB file: " + path.string());
    }

    return archive;
}

const std::filesystem::path& GobArchive::path() const noexcept
{
    return path_;
}

const std::vector<GobEntry>& GobArchive::entries() const noexcept
{
    return entries_;
}

std::optional<std::span<const std::byte>> GobArchive::capturedBytes() const noexcept
{
    if ( !capturedBytes_ )
    {
        return std::nullopt;
    }
    return std::span<const std::byte>(*capturedBytes_);
}

std::optional<GobEntry> GobArchive::find(const std::string_view name) const
{
    const std::string key = normalizeEntryName(name);
    if (key.empty())
    {
        return std::nullopt;
    }

    const auto it = index_.find(key);
    if (it == index_.end())
    {
        return std::nullopt;
    }

    return entries_[it->second];
}

bool GobArchive::contains(const std::string_view name) const
{
    return find(name).has_value();
}

std::vector<std::byte> GobArchive::read(const std::string_view name) const
{
    const auto entryOpt = find(name);
    if (!entryOpt)
    {
        throw std::runtime_error("GOB entry not found: " + std::string(name));
    }

    const GobEntry& entry = *entryOpt;
    if ( capturedBytes_ )
    {
        const auto data = std::span<const std::byte>(*capturedBytes_).subspan(entry.offset, entry.size);
        return { data.begin(), data.end() };
    }
    std::ifstream file(path_, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open GOB: " + path_.string());
    }

    file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    if (!file)
    {
        throw std::runtime_error("Invalid GOB entry offset for: " + entry.name);
    }

    std::vector<std::byte> data;
    data.resize(entry.size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file)
    {
        throw std::runtime_error("Unexpected EOF while reading GOB entry: " + entry.name);
    }
    return data;
}

std::string GobArchive::entrySha256Hex(const std::string_view name) const
{
    const std::vector<std::byte> data = read(name);
    const std::array<std::byte, 32> digest = calculateSedSha256(
        std::span<const std::byte>(data.data(), data.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex{};
    hex.reserve(digest.size() * 2);
    for (const std::byte byte : digest)
    {
        const auto value = static_cast<unsigned char>(byte);
        hex.push_back(kHex[value >> 4]);
        hex.push_back(kHex[value & 0x0f]);
    }
    return hex;
}
std::string GobArchive::normalizeEntryName(const std::string_view name)
{
    std::string out;
    out.reserve(name.size());

    for (const char ch : name)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (ch == '\\' || ch == '/')
        {
            out.push_back('/');
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(uch)));
    }

    while (!out.empty() && out.front() == '/')
    {
        out.erase(out.begin());
    }

    return out;
}


std::string canonicalRuntimeGobEntryName(const std::string_view name)
{
    if ( name.empty() || name.size() > 127 )
    {
        throw std::invalid_argument("Runtime archive names must contain 1 to 127 ASCII bytes.");
    }
    std::string result;
    result.reserve(name.size());
    for ( const unsigned char c : name )
    {
        if ( c <= 32 || c >= 127 || c == '"' || c == ':' || c == '*'
            || c == '?' || c == '<' || c == '>' || c == '|' )
        {
            throw std::invalid_argument(
                "Runtime archive names must be relative ASCII paths without spaces, drive letters or wildcards.");
        }
        result.push_back(c == '/' ? '\\'
            : static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    std::size_t start{};
    while ( start <= result.size() )
    {
        const auto separator = result.find('\\', start);
        const auto end = separator == std::string::npos ? result.size() : separator;
        const std::string_view part(result.data() + start, end - start);
        if ( part.empty() || part == "." || part == ".." )
        {
            throw std::invalid_argument(
                "Runtime archive names must be relative paths without empty, '.' or '..' components.");
        }
        if ( separator == std::string::npos )
        {
            break;
        }
        start = end + 1;
    }
    return result;
}

// GOB version 0x14 with the index right after the header: name, offset and
// size per entry (136 bytes), then the entry bytes in order. Forward
// slashes in names become backslashes, as the engine expects.
std::string buildGobBytes(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::string out;
    const auto putU32 = [&out](const std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out += static_cast<char>((value >> shift) & 0xFFu);
        }
    };
    out += "GOB ";
    putU32(0x14u);
    putU32(12u);
    putU32(static_cast<std::uint32_t>(files.size()));
    std::uint32_t offset = 16u + static_cast<std::uint32_t>(files.size()) * 136u;
    for (const auto& [name, bytes] : files)
    {
        putU32(offset);
        putU32(static_cast<std::uint32_t>(bytes.size()));
        std::string entryName = name;
        for (char& c : entryName)
        {
            if (c == '/')
            {
                c = '\\';
            }
        }
        entryName.resize(128, '\0');
        out += entryName;
        offset += static_cast<std::uint32_t>(bytes.size());
    }
    for (const auto& [name, bytes] : files)
    {
        (void)name;
        out += bytes;
    }
    return out;
}

} // namespace sed::assets
