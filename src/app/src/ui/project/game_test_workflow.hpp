#pragma once

#include "sed/level/game_launch.hpp"

#include <filesystem>
#include <functional>
#include <memory>

class QWidget;
namespace sed::assets { class ResourceLocator; }

namespace sed::ui::project
{
struct GameTestContext
{
    ProjectType game{};
    std::filesystem::path sourcePath;
    std::filesystem::path defaultGameDirectory;
    std::shared_ptr<const assets::ResourceLocator> resources;
    /** Exports a fresh immutable snapshot through the ordinary export pipeline. */
    std::function<bool(const std::filesystem::path&)> exportMap;
    /** Rechecked after staging, immediately before process creation. */
    std::function<bool()> snapshotStillCurrent;
};

struct GameTestResult
{
    bool started{};
    bool directMapStart{};
    std::filesystem::path stageDirectory;
};

/** Interactive export/stage/start transaction. Cancellation or owner loss at UI
 * and callback boundaries returns an empty result without invoking more callbacks.
 * Failures throw with a user-facing diagnostic. All generated files belong to a
 * newly allocated external stage; source and game installation are read-only.
 */
GameTestResult saveJklAndTest(QWidget* parent, const GameTestContext& context);
} // namespace sed::ui::project
