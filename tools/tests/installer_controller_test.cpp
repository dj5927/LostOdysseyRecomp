#include <cassert>

#include "install/installer_ui.h"
#include "install/installer_navigation.h"

int main()
{
    using install::ui::Direction;
    install::ui::StickNavigation stick;
    // Explicit checks remain active in release builds, unlike assert.
    if (stick.Update(15000, 0, 0) != Direction::None) return 1;
    if (stick.Update(20000, 0, 10) != Direction::Right) return 2;
    if (stick.Update(20000, 0, 359) != Direction::None) return 3;
    if (stick.Update(20000, 0, 360) != Direction::Right) return 4;
    if (stick.Update(20000, 0, 479) != Direction::None) return 5;
    if (stick.Update(20000, 0, 480) != Direction::Right) return 6;
    if (stick.Update(0, 0, 490) != Direction::None) return 7;
    if (stick.Update(-32768, 0, 500) != Direction::Left) return 8;
    if (stick.Update(17000, -25000, 510) != Direction::Up) return 9;
    if (stick.Update(0, 25000, 520) != Direction::Down) return 10;
    if (stick.Update(0, 11000, 530) != Direction::None) return 11;
    if (stick.Update(0, 9000, 540) != Direction::None) return 12;
    if (stick.Update(0, 15000, 550) != Direction::None) return 13;
    if (stick.Update(0, 20000, 560) != Direction::Down) return 14;

    install::InstallResult discsOnly;
    discsOnly.discs.push_back(1);
    assert(install::ShouldPersistGamePath(discsOnly));

    install::InstallResult dlcOnly;
    dlcOnly.dlcImported.push_back("dlc");
    assert(!install::ShouldPersistGamePath(dlcOnly));

    install::InstallResult discsWithDlcFailure;
    discsWithDlcFailure.discs.push_back(2);
    discsWithDlcFailure.error = "DLC import failed";
    assert(install::ShouldPersistGamePath(discsWithDlcFailure));
    assert(!install::ShouldReportImportSuccess(discsWithDlcFailure, true));
    assert(!install::ShouldReportImportSuccess(discsOnly, false));
    assert(install::ShouldReportImportSuccess(discsOnly, true));
    assert(install::ShouldReportImportSuccess(dlcOnly, true));

    install::ContentScan mixed;
    mixed.discs.resize(4);
    mixed.packages.resize(2);
    assert(install::ReviewActionStart(mixed) == 6);

    // Scan A, switch to invalid B, then press Import: never reuse A.
    install::InstallerSessionState session;
    if (session.BeginImport()) return 15;
    session.BeginScan();
    session.FinishScan(mixed);
    if (!session.CanImport()) return 16;
    session.BeginScan();
    if (session.BeginImport() || !session.scanResult.discs.empty()) return 17;
    session.FailScan("No supported sources in B");
    if (session.BeginImport() || !session.scanResult.packages.empty()) return 18;

    // Retry after cancellation must clear the result consumed by RunHost.
    session.BeginScan();
    session.FinishScan(mixed);
    if (!session.BeginImport()) return 19;
    session.userCancelled = true;
    if (!session.BeginImport() || session.userCancelled) return 20;
    session.installSuccess = true;
    if (!session.installSuccess || session.userCancelled) return 21;

    // Empty scans stay disabled, but DLC-only imports remain supported.
    session.BeginScan();
    session.FinishScan({});
    if (session.BeginImport()) return 22;
    install::ContentScan dlcScan;
    dlcScan.packages.resize(1);
    session.FinishScan(std::move(dlcScan));
    if (!session.BeginImport() || session.installSuccess) return 23;

    return 0;
}
