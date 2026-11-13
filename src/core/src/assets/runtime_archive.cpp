#include "sed/assets/runtime_archive.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/cog_script.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/tpl.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace sed::assets
{
void validateRuntimeArchiveSizes(const std::span<const std::size_t> sizes)
{
    if ( sizes.empty() || sizes.size() > kRuntimeArchiveMaxEntries )
    {
        throw RuntimeArchiveError(RuntimeArchiveErrorCode::InvalidRequest,
            "archive-create requires 'entries': 1..4096 {name, path, sha256}");
    }
    std::size_t remaining = kRuntimeArchiveMaxBytes - 16u - 136u * sizes.size();
    for ( const std::size_t bytes : sizes )
    {
        if ( bytes > kRuntimeArchiveMaxSourceBytes )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::SourceTooLarge,
                "Archive entry source exceeds 256 MiB.");
        }
        if ( bytes > remaining )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::ArchiveTooLarge,
                "archiveTooLarge: the archive would exceed 512 MiB, the most the agent reads as a resource archive");
        }
        remaining -= bytes;
    }
}

class RuntimeArchiveBuilder::Impl
{
public:
    explicit Impl(const std::size_t count) : expected_(count)
    {
        if ( count == 0 || count > kRuntimeArchiveMaxEntries )
        {
            validateRuntimeArchiveSizes({});
        }
        remaining_ = kRuntimeArchiveMaxBytes - 16u - 136u * count;
        result_.entries.reserve(count);
        files_.reserve(count);
    }

    void add(RuntimeArchiveInput input)
    {
        if ( failed_ || finished_ || files_.size() == expected_ )
        {
            failed_ = true;
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::InvalidRequest,
                "Archive builder does not accept another entry.");
        }
        // No partially validated builder may be published after an exception.
        failed_ = true;
        if ( input.bytes.size() > kRuntimeArchiveMaxSourceBytes )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::SourceTooLarge,
                "Archive entry source exceeds 256 MiB.");
        }
        if ( input.bytes.size() > remaining_ )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::ArchiveTooLarge,
                "archiveTooLarge: the archive would exceed 512 MiB, the most the agent reads as a resource archive");
        }
        remaining_ -= input.bytes.size();
        constexpr std::array<std::string_view, 10> kKnownFolders{
            "3do", "cog", "jkl", "mat", "misc", "sound", "voice", "ui", "player", "cutscene"};
        std::string name;
        try
        {
            name = canonicalRuntimeGobEntryName(input.name);
        }
        catch ( const std::invalid_argument& error )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::InvalidEntryName, error.what());
        }
        if ( !seen_.insert(name).second )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::DuplicateEntry,
                "duplicateArchiveEntry: '" + name + "' is named twice (names are case-insensitive)");
        }
        const auto slash = name.find('\\');
        const std::string top = name.substr(0, slash);
        if ( slash == std::string::npos )
        {
            if ( name != "episode.jk" )
            {
                result_.warnings.push_back("'" + name
                    + "' is at the archive root; JK looks resources up in folders such as mat\\ and 3do\\");
            }
        }
        else if ( std::find(kKnownFolders.begin(), kKnownFolders.end(), top) == kKnownFolders.end() )
        {
            result_.warnings.push_back("'" + name + "' is in an unusual top folder '" + top + "'");
        }
        // Preserve the archive-create adapter's lexical extension contract,
        // including dotfiles (which have no extension). No path is opened here.
        const std::string extension = std::filesystem::path(name).extension().string();
        std::string checked = "none";
        try
        {
            if ( extension == ".mat" )
            {
                std::vector<std::byte> raw(input.bytes.size());
                if ( !input.bytes.empty() )
                {
                    std::memcpy(raw.data(), input.bytes.data(), input.bytes.size());
                }
                (void)MatFile::parse(std::move(raw));
                checked = "mat";
            }
            else if ( extension == ".3do" )
            {
                (void)loadFrom3DO(std::as_bytes(std::span(input.bytes.data(), input.bytes.size())));
                checked = "3do";
            }
            else if ( extension == ".cog" )
            {
                const auto symbols = loadCogSymbolsFromTextResult(input.bytes, true);
                if ( !symbols.loaded() )
                {
                    throw std::runtime_error(symbols.diagnostic);
                }
                checked = "cog-symbols";
            }
            else if ( extension == ".tpl" )
            {
                std::istringstream stream(input.bytes);
                (void)loadTemplates(stream);
                checked = "tpl";
            }
        }
        catch ( const std::exception& error )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::InvalidEntry,
                "invalidArchiveEntry: '" + name + "' does not read as " + extension.substr(1) + ": " + error.what());
        }
        result_.entries.push_back({name, input.bytes.size(), jklSourceSha256Hex(input.bytes), checked});
        files_.emplace_back(std::move(name), std::move(input.bytes));
        failed_ = false;
    }

    RuntimeArchiveResult finish()
    {
        if ( failed_ || finished_ || files_.size() != expected_ )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::InvalidRequest,
                "Archive builder is incomplete, failed or already finished.");
        }
        finished_ = true;
        result_.outputData = buildGobBytes(files_);
        files_.clear();
        files_.shrink_to_fit();
        try
        {
            std::vector<std::byte> raw(result_.outputData.size());
            if ( !raw.empty() )
            {
                std::memcpy(raw.data(), result_.outputData.data(), raw.size());
            }
            const auto archive = GobArchive::fromBytes(std::move(raw));
            if ( archive.entries().size() != result_.entries.size() )
            {
                throw RuntimeArchiveError(RuntimeArchiveErrorCode::Readback, "archive-create readback entry count differs");
            }
        }
        catch ( const RuntimeArchiveError& )
        {
            throw;
        }
        catch ( const std::exception& error )
        {
            throw RuntimeArchiveError(RuntimeArchiveErrorCode::Readback,
                std::string("archive-create output does not read back: ") + error.what());
        }
        return std::move(result_);
    }
private:
    std::size_t expected_{};
    std::size_t remaining_{};
    bool failed_{};
    bool finished_{};
    RuntimeArchiveResult result_;
    std::unordered_set<std::string> seen_;
    std::vector<std::pair<std::string, std::string>> files_;
};

RuntimeArchiveBuilder::RuntimeArchiveBuilder(const std::size_t entryCount)
    : impl_(std::make_unique<Impl>(entryCount))
{
}
RuntimeArchiveBuilder::~RuntimeArchiveBuilder() = default;
void RuntimeArchiveBuilder::add(RuntimeArchiveInput input)
{
    impl_->add(std::move(input));
}
RuntimeArchiveResult RuntimeArchiveBuilder::finish()
{
    return impl_->finish();
}

RuntimeArchiveResult buildRuntimeArchive(std::vector<RuntimeArchiveInput> inputs)
{
    RuntimeArchiveBuilder builder(inputs.size());
    for ( auto& input : inputs )
    {
        builder.add(std::move(input));
    }
    return builder.finish();
}
} // namespace sed::assets
