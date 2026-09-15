#pragma once

#include <filesystem>
#include <string>

namespace install
{
struct InstallerResult
{
    bool success = false;
    bool cancelled = false;
    std::filesystem::path destination;
    std::string error;
};

// Shows the self-drawn SDL2 installer window.
// Returns success status, whether user cancelled, and the selected/installed destination.
InstallerResult ShowInstallerUI(const std::filesystem::path& executableDirectory,
                                const std::filesystem::path& initialSource = {},
                                const std::filesystem::path& initialDest = {});
}
