#pragma once

#include <filesystem>
#include <string>

#include "import_game.h"

namespace install
{
inline bool ShouldPersistGamePath(const InstallResult& result)
{
    return !result.discs.empty();
}

inline bool ShouldReportImportSuccess(const InstallResult& result, bool pathSaved)
{
    return !result.cancelled && result.error.empty() && pathSaved;
}

inline int ReviewActionStart(const ContentScan& scan)
{
    return static_cast<int>(scan.discs.size() + scan.packages.size());
}

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
