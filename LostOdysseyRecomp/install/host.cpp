#include "host.h"

#include "../settings/first_run.h"
#include "../settings/game_path.h"
#include "installer_ui.h"

namespace install
{
HostResult RunHost(const std::filesystem::path& executableDirectory, std::filesystem::path* gameRoot, bool force)
{
    if (!gameRoot)
        return HostResult::Failed;

    if (!force)
    {
        if (const auto recognized = settings::game_path::Recognize(*gameRoot))
        {
            *gameRoot = *recognized;
            return HostResult::AlreadyPresent;
        }
    }

    // Launch the game-like self-drawn SDL2 installer UI
    auto res = ShowInstallerUI(executableDirectory, {}, *gameRoot);
    if (res.cancelled)
        return HostResult::Cancelled;
    if (!res.success)
        return HostResult::Failed;

    if (const auto recognized = settings::game_path::Recognize(res.destination))
    {
        *gameRoot = *recognized;
        return HostResult::Installed;
    }
    if (const auto recognized = settings::game_path::Recognize(*gameRoot))
    {
        *gameRoot = *recognized;
        return HostResult::Installed;
    }

    return HostResult::Failed;
}
}
