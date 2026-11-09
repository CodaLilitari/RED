#pragma once

#include "sed/assets/bounded_stream_reader.hpp"

#include <cstddef>
#include <istream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sed::assets::detail
{

// Operational limits protect both source capture and the section-record
// containers. They are editor admission bounds, not game format limits.
struct KeyInputLimits
{
    std::size_t maxBytes{ 128u * 1024u * 1024u };
    std::size_t maxLineBytes{ 64u * 1024u };
    std::size_t maxLines{ 1024u * 1024u };
};

inline std::string readKeyInput(std::istream& input, const KeyInputLimits limits = {})
{
    bool exceededBudget{};
    auto source = readBinaryStreamWithinBudget<std::string>(input, limits.maxBytes, 0u,
        exceededBudget, "Failed to read KEY.");
    if ( exceededBudget || !source )
    {
        throw std::runtime_error("KEY source exceeds its byte limit.");
    }

    std::size_t lines{};
    std::size_t lineBytes{};
    bool atLineStart = true;
    for ( const char value : *source )
    {
        if ( atLineStart )
        {
            if ( lines == limits.maxLines )
            {
                throw std::runtime_error("KEY source exceeds its line count limit.");
            }
            ++lines;
            atLineStart = false;
        }
        if ( value == '\n' )
        {
            lineBytes = 0u;
            atLineStart = true;
        }
        else
        {
            if ( lineBytes == limits.maxLineBytes )
            {
                throw std::runtime_error("KEY source exceeds its line length limit.");
            }
            ++lineBytes;
        }
    }
    return std::move(*source);
}

} // namespace sed::assets::detail
