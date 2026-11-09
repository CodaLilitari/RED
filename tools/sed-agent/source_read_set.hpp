#pragma once

#include "agent_json.hpp"
#include "bounded_input.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed_agent::detail
{

class SourceRevisionError final : public std::runtime_error
{
public:
    explicit SourceRevisionError(const std::filesystem::path& path)
        : std::runtime_error("Source changed or became unavailable: " + path.string())
    {
    }
};

// Tracks only files actually captured by the caller. Directory providers,
// discovery precedence, and files read through another API need their own
// explicit capture/admission; this is not a filesystem snapshot or lock.
class SourceReadSet
{
public:
    void track(const std::filesystem::path& path, const std::string_view bytes,
        const std::size_t limit = kMaxSourceBytes)
    {
        if ( bytes.size() > limit )
        {
            throw InputReadError(InputReadError::Kind::LimitExceeded, "Captured source exceeds its byte limit.");
        }
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        const std::string digest = sha256Hex(bytes);
        const auto existing = std::find_if(sources_.begin(), sources_.end(),
            [&absolute](const Source& source) { return source.path == absolute; });
        if ( existing != sources_.end() )
        {
            if ( existing->sha256 != digest )
            {
                throw SourceRevisionError(absolute);
            }
            existing->limit = std::min(existing->limit, limit);
            return;
        }
        sources_.push_back(Source{ absolute, digest, limit });
    }

    std::string capture(const std::filesystem::path& path, const std::size_t limit = kMaxSourceBytes)
    {
        std::string bytes = readBoundedFile(path, limit);
        track(path, bytes, limit);
        return bytes;
    }

    void requireUnchanged() const
    {
        for ( const Source& source : sources_ )
        {
            std::string bytes;
            try
            {
                bytes = readBoundedFile(source.path, source.limit);
            }
            catch ( const InputReadError& )
            {
                throw SourceRevisionError(source.path);
            }
            if ( sha256Hex(bytes) != source.sha256 )
            {
                throw SourceRevisionError(source.path);
            }
        }
    }

private:
    struct Source
    {
        std::filesystem::path path;
        std::string sha256;
        std::size_t limit;
    };
    std::vector<Source> sources_;
};

} // namespace sed_agent::detail
