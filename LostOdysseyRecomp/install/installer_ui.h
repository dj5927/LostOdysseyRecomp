#pragma once

#include <filesystem>
#include <string>
#include <utility>

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

// Shared by the SDL controller and its transition tests. A failed or pending
// scan must never leave an earlier source eligible for import.
struct InstallerSessionState
{
    ContentScan scanResult;
    std::string scanError;
    bool userCancelled = false;
    bool installSuccess = false;

    void BeginScan()
    {
        scanResult = {};
        scanError.clear();
        scanReady = false;
    }

    void FinishScan(ContentScan scan)
    {
        scanResult = std::move(scan);
        scanError.clear();
        scanReady = true;
    }

    void FailScan(std::string error)
    {
        BeginScan();
        scanError = std::move(error);
    }

    bool CanImport() const
    {
        return scanReady && scanError.empty() &&
               (!scanResult.discs.empty() || !scanResult.packages.empty());
    }

    bool BeginImport()
    {
        if (!CanImport()) return false;
        userCancelled = false;
        installSuccess = false;
        return true;
    }

private:
    bool scanReady = false;
};

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
