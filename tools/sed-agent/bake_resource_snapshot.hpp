#pragma once

#include "bounded_input.hpp"

#include "sed/assets/resource_locator.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed_agent::detail
{

class ResourceRevisionError final : public std::runtime_error
{
public:
    explicit ResourceRevisionError(const std::string& name)
        : std::runtime_error("Resource lookup changed or became unavailable: " + name)
    {
    }
};

// Shared by the sequential bakes of one command. Captures consume this budget
// for the command's lifetime; cache hits and publication replay do not charge
// it again. Keep each snapshot's own limits as an additional bound.
struct BakeResourceBudget
{
    std::size_t remainingBytes{ 256u * 1024u * 1024u };
    std::size_t remainingLookups{ 4096u };
};

// Owns the provider configuration and exact lookup results used by one bake.
// Mount captured GOBs before construction and separately track their source
// files in SourceReadSet: replay deliberately uses the same captured archives.
// Loose-file changes, new higher-priority winners and missing-to-present
// changes are detected by replaying every requested lookup before publication.
class BakeResourceSnapshot
{
public:
    explicit BakeResourceSnapshot(sed::assets::ResourceLocator locator,
        const std::size_t perItemBytes = kMaxSourceBytes,
        const std::size_t aggregateBytes = 256u * 1024u * 1024u,
        const std::size_t maxLookups = 4096u,
        std::shared_ptr<BakeResourceBudget> commandBudget = {},
        std::function<void(const std::filesystem::path&)> readPolicy = {})
        : locator_(std::move(locator)), perItemBytes_(perItemBytes),
          aggregateBytes_(aggregateBytes), maxLookups_(maxLookups), commandBudget_(std::move(commandBudget)),
          readPolicy_(std::move(readPolicy))
    {
    }

    BakeResourceSnapshot(sed::assets::ResourceLocator locator, std::shared_ptr<BakeResourceBudget> commandBudget,
        std::function<void(const std::filesystem::path&)> readPolicy = {})
        : BakeResourceSnapshot(std::move(locator), kMaxSourceBytes, 256u * 1024u * 1024u,
            4096u, std::move(commandBudget), std::move(readPolicy))
    {
    }

    std::optional<std::vector<std::byte>> read(const std::string_view name, const std::string_view extension)
    {
        if ( failure_ )
        {
            return std::nullopt;
        }
        try
        {
            const Key key{ std::string(sed::text::trimView(name)), std::string(extension) };
            if ( const auto found = captured_.find(key); found != captured_.end() )
            {
                return found->second.bytes;
            }
            if ( captured_.size() >= maxLookups_ || (commandBudget_ && commandBudget_->remainingLookups == 0u) )
            {
                throw InputReadError(InputReadError::Kind::LimitExceeded, "Bake resource lookup count exceeds its limit.");
            }
            const std::size_t remaining = commandBudget_
                ? std::min(aggregateBytes_ - retainedBytes_, commandBudget_->remainingBytes)
                : aggregateBytes_ - retainedBytes_;
            Lookup capture = lookup(key, std::min(perItemBytes_, remaining));
            const std::size_t byteCount = capture.bytes ? capture.bytes->size() : 0u;
            // A trusted resource callback may itself perform another capture;
            // recheck the shared reservation before committing our own result.
            if ( commandBudget_ && (commandBudget_->remainingLookups == 0u
                || byteCount > commandBudget_->remainingBytes) )
            {
                throw InputReadError(InputReadError::Kind::LimitExceeded, "Bake resources exceed the command budget.");
            }
            const auto [stored, inserted] = captured_.emplace(key, std::move(capture));
            if ( inserted )
            {
                retainedBytes_ += byteCount;
                if ( commandBudget_ )
                {
                    commandBudget_->remainingBytes -= byteCount;
                    --commandBudget_->remainingLookups;
                }
            }
            return stored->second.bytes;
        }
        catch ( ... )
        {
            // Emissive resolution intentionally omits unreadable materials.
            // Preserve failures here so that omission cannot authorize output.
            failure_ = std::current_exception();
            return std::nullopt;
        }
    }

    void throwIfReadFailed() const
    {
        if ( failure_ )
        {
            std::rethrow_exception(failure_);
        }
    }

    void requireUnchanged() const
    {
        throwIfReadFailed();
        for ( const auto& [key, original] : captured_ )
        {
            Lookup current;
            try
            {
                current = lookup(key, perItemBytes_);
            }
            catch ( const std::bad_alloc& )
            {
                throw;
            }
            catch ( ... )
            {
                throw ResourceRevisionError(key.first);
            }
            if ( current.bytes != original.bytes || current.diskWinner != original.diskWinner
                || current.selectedName != original.selectedName )
            {
                throw ResourceRevisionError(key.first);
            }
        }
    }

private:
    using Key = std::pair<std::string, std::string>;
    struct Lookup
    {
        std::optional<std::vector<std::byte>> bytes;
        std::optional<std::filesystem::path> diskWinner;
        std::string selectedName;
    };

    Lookup lookupOne(const std::string& name, const std::size_t limit) const
    {
        Lookup result;
        result.selectedName = name;
        result.diskWinner = locator_.findSelectedOnDisk(name);
        if ( result.diskWinner )
        {
            *result.diskWinner = std::filesystem::absolute(*result.diskWinner).lexically_normal();
            if ( readPolicy_ ) readPolicy_(*result.diskWinner);
            std::error_code error;
            const auto size = std::filesystem::file_size(*result.diskWinner, error);
            if ( error )
            {
                throw InputReadError(InputReadError::Kind::ReadFailure, "Cannot determine resource size: " + name);
            }
            if ( size > limit )
            {
                throw InputReadError(InputReadError::Kind::LimitExceeded, "Bake resource exceeds its byte budget: " + name);
            }
        }
        bool exceededBudget{};
        if ( readPolicy_ && result.diskWinner )
        {
            // Read the admitted winner itself, not a second unconstrained
            // locator lookup that could select a different loose resource.
            const auto bytes = readBoundedFile(*result.diskWinner, limit);
            const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
            result.bytes = std::vector<std::byte>(begin, begin + bytes.size());
        }
        else if ( readPolicy_ )
        {
            // No admitted disk winner: restrict fallback to captured archives.
            // A newly appearing loose file is detected by the winner recheck.
            auto archives = locator_;
            archives.setArchivesOnly(true);
            result.bytes = archives.tryReadBinary(name, limit, exceededBudget);
        }
        else
        {
            result.bytes = locator_.tryReadBinary(name, limit, exceededBudget);
        }
        if ( exceededBudget )
        {
            throw InputReadError(InputReadError::Kind::LimitExceeded, "Bake resource exceeds its byte budget: " + name);
        }
        auto after = locator_.findSelectedOnDisk(name);
        if ( after )
        {
            *after = std::filesystem::absolute(*after).lexically_normal();
        }
        if ( after != result.diskWinner )
        {
            throw ResourceRevisionError(name);
        }
        return result;
    }

    Lookup lookup(const Key& key, const std::size_t limit) const
    {
        Lookup original = lookupOne(key.first, limit);
        if ( original.bytes || key.first.empty() || sed::text::iendsWithAscii(key.first, key.second) )
        {
            return original;
        }
        return lookupOne(key.first + key.second, limit);
    }

    sed::assets::ResourceLocator locator_;
    std::size_t perItemBytes_;
    std::size_t aggregateBytes_;
    std::size_t maxLookups_;
    std::shared_ptr<BakeResourceBudget> commandBudget_;
    std::function<void(const std::filesystem::path&)> readPolicy_;
    std::size_t retainedBytes_{};
    std::map<Key, Lookup> captured_;
    std::exception_ptr failure_;
};

} // namespace sed_agent::detail
