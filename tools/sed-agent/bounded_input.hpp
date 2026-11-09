#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>

namespace sed_agent::detail
{

inline constexpr std::size_t kMaxSourceBytes = 128u * 1024u * 1024u;

class InputReadError final : public std::runtime_error
{
public:
    enum class Kind { LimitExceeded, ReadFailure, NotRegularFile };

    InputReadError(const Kind reason, const std::string& message)
        : std::runtime_error(message), kind(reason)
    {
    }

    Kind kind;
};

// Works with finite non-seekable streams. Consumption is at most limit+1;
// the extra byte distinguishes an exact-limit input from a larger input.
// A byte budget cannot interrupt a device blocked inside its read operation;
// process/transport deadlines remain the caller's responsibility.
inline std::string readBoundedStream(std::istream& input, const std::size_t limit,
    const std::optional<std::uintmax_t> expectedSize = std::nullopt)
{
    if ( expectedSize && *expectedSize > limit )
    {
        throw InputReadError(InputReadError::Kind::LimitExceeded, "Input exceeds its byte limit.");
    }
    if ( !input.good() || !input.rdbuf() )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Input stream is not readable.");
    }
    std::array<char, 64u * 1024u> buffer{};
    std::string bytes;
    if ( expectedSize )
    {
        bytes.reserve(static_cast<std::size_t>(*expectedSize));
    }
    while ( true )
    {
        const std::size_t remaining = limit - bytes.size();
        const std::size_t requested = remaining < buffer.size() ? remaining + 1u : buffer.size();
        std::streamsize count{};
        bool atEnd{};
        try
        {
            count = input.rdbuf()->sgetn(buffer.data(), static_cast<std::streamsize>(requested));
            if ( count == 0 )
            {
                atEnd = std::char_traits<char>::eq_int_type(input.rdbuf()->sgetc(),
                    std::char_traits<char>::eof());
            }
        }
        catch ( const std::bad_alloc& )
        {
            throw;
        }
        catch ( ... )
        {
            throw InputReadError(InputReadError::Kind::ReadFailure, "Input stream failed while reading.");
        }
        if ( count < 0 || static_cast<std::uintmax_t>(count) > requested || (count == 0 && !atEnd) )
        {
            throw InputReadError(InputReadError::Kind::ReadFailure, "Input stream made no valid read progress.");
        }
        if ( static_cast<std::size_t>(count) > remaining )
        {
            throw InputReadError(InputReadError::Kind::LimitExceeded, "Input exceeds its byte limit.");
        }
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
        if ( atEnd )
        {
            break;
        }
    }
    if ( expectedSize && bytes.size() != *expectedSize )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Input size changed while reading.");
    }
    return bytes;
}

// Archive callers pass their separately documented archive budget explicitly.
// The initial metadata check avoids allocating or opening oversized files;
// the stream budget still protects against growth after that check.
inline std::string readBoundedFile(const std::filesystem::path& path,
    const std::size_t limit = kMaxSourceBytes)
{
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if ( error || !std::filesystem::exists(status) )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Cannot read input file: " + path.string());
    }
    if ( !std::filesystem::is_regular_file(status) )
    {
        throw InputReadError(InputReadError::Kind::NotRegularFile, "Input is not a regular file: " + path.string());
    }
    const auto size = std::filesystem::file_size(path, error);
    if ( error )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Cannot determine input size: " + path.string());
    }
    if ( size > limit )
    {
        throw InputReadError(InputReadError::Kind::LimitExceeded, "Input exceeds its byte limit: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if ( !input )
    {
        throw InputReadError(InputReadError::Kind::ReadFailure, "Cannot open input file: " + path.string());
    }
    return readBoundedStream(input, limit, size);
}

} // namespace sed_agent::detail
