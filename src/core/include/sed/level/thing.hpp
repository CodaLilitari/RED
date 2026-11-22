#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/level.hpp"

namespace sed::assets
{
class Templates;
} // namespace sed::assets

namespace sed::level
{

// Points into an immutable document. A resolved chain is leaf first and ends
// at an explicit root. Missing, ambiguous, cyclic and over-depth paths fail.
class TemplateChainIndex
{
public:
    explicit TemplateChainIndex(const Level& level);
    std::optional<std::vector<const ThingTemplate*>> resolve(std::string_view leaf) const;

private:
    std::map<std::string, const ThingTemplate*> templates_;
};

const TplValue* findTemplateChainField(std::span<const ThingTemplate* const> chain,
    std::string_view field) noexcept;

const TplValue* thingFindValue(const Level& level, const Thing& thing, const std::string_view name) noexcept;
const TplValue* thingFindValue(const Level& level, const assets::Templates* templates, const Thing& thing, const std::string_view name) noexcept;

// Counts `frame` values on a thing (values with name "frame" and ValueType::frame).
std::size_t thingCountFrames(const Thing& thing) noexcept;

// Ensures `numframes` matches the authored frame count and precedes every
// `frame`, as required by the runtime's allocate-then-append parser.
void thingSyncNumFrames(Thing& thing) noexcept;
void thingSyncNumFrames(ThingTemplate& thingTemplate) noexcept;

std::int32_t thingFlags(const Level& level, const Thing& thing) noexcept;
std::int32_t thingFlags(const Level& level, const assets::Templates* templates, const Thing& thing) noexcept;

ColorF thingLight(const Level& level, const Thing& thing) noexcept;
ColorF thingLight(const Level& level, const assets::Templates* templates, const Thing& thing) noexcept;

} // namespace sed::level
