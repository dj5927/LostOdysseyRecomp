#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "install/import_game.h"
#include "install/import_crypto.h"

namespace
{
std::vector<uint8_t> MakeXex(uint32_t disc = 1, uint32_t media = 0x39F7D748, uint32_t version = 4, uint32_t base = 4)
{
    std::vector<uint8_t> data(128, 0);
    std::memcpy(data.data(), "XEX2", 4);

    auto writeBeU32 = [&](size_t offset, uint32_t val) {
        data[offset] = static_cast<uint8_t>(val >> 24);
        data[offset + 1] = static_cast<uint8_t>(val >> 16);
        data[offset + 2] = static_cast<uint8_t>(val >> 8);
        data[offset + 3] = static_cast<uint8_t>(val);
    };

    writeBeU32(20, 1); // 1 optional header
    writeBeU32(24, 0x40006); // key
    writeBeU32(28, 32); // offset

    writeBeU32(32, media);
    writeBeU32(36, version);
    writeBeU32(40, base);
    writeBeU32(44, 0x4D5307FA); // title ID
    data[48] = 2;
    data[49] = 0;
    data[50] = static_cast<uint8_t>(disc);
    data[51] = 4; // 4 discs
    return data;
}

void WriteDiscFiles(const std::filesystem::path& dir, uint32_t disc, bool includeXex = true)
{
    std::filesystem::create_directories(dir);
    if (includeXex)
    {
        static const uint32_t mediaMap[5] = {0, 0x39F7D748, 0x0EF8CEA8, 0x309E3386, 0x7B21A91D};
        auto xex = MakeXex(disc, mediaMap[disc], 4, 4);
        std::ofstream xexOut(dir / "default.xex", std::ios::binary);
        xexOut.write(reinterpret_cast<const char*>(xex.data()), xex.size());
    }

    static const char* requiredNames[] = {
        "lo.fpd", "lo.fpi", "xenon_battle.fpd", "xenon_chr.fpd", "xenon_event.fpd",
        "xenon_field.fpd", "xenon_loc.fpd", "xenon_mov.fpd", "xenon_obj.fpd",
        "xenon_scr.fpd", "xenon_snd.fpd", "xenon_sys.fpd", "xenon_vfx.fpd", "xenon_world.fpd"
    };

    for (const char* name : requiredNames)
    {
        std::ofstream out(dir / name, std::ios::binary);
        std::string dummy = std::string(name) + " payload";
        out.write(dummy.data(), dummy.size());
    }
}
} // namespace

int main()
{
    std::cout << "Starting LoImportGameTest..." << std::endl;

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / ("lo-import-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    struct TempCleanup {
        std::filesystem::path p;
        ~TempCleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{tempDir};

    // 1. Set up test hashes for Asia edition
    static const uint32_t mediaMap[5] = {0, 0x39F7D748, 0x0EF8CEA8, 0x309E3386, 0x7B21A91D};
    for (uint32_t d = 1; d <= 4; ++d)
    {
        auto xex = MakeXex(d, mediaMap[d], 4, 4);
        std::string hash = install::crypto::Sha256Hex(xex.data(), xex.size());
        install::SetTestSha256(d, hash, false);
    }

    // 2. Test standard folder import with authentic per-disc files
    std::filesystem::path sourceStandard = tempDir / "standard";
    for (uint32_t d = 1; d <= 4; ++d)
    {
        WriteDiscFiles(sourceStandard / ("disc" + std::to_string(d)), d, true);
    }

    auto disc1Scan = install::ScanSource(sourceStandard / "disc1");
    assert(disc1Scan.discs.size() == 1);
    assert(disc1Scan.discs[0].disc == 1);
    assert(disc1Scan.discs[0].edition == "asia");
    std::cout << "[PASS] Standard single-disc scan" << std::endl;

    auto allScan = install::ScanSource(sourceStandard);
    assert(allScan.discs.size() == 4);
    for (size_t i = 0; i < 4; ++i)
    {
        assert(allScan.discs[i].disc == static_cast<uint32_t>(i + 1));
        assert(allScan.discs[i].edition == "asia");
    }
    std::cout << "[PASS] Standard 4-disc set scan" << std::endl;

    // 3. Test authentic per-disc identity enforcement:
    // Reject fabrication where discs share a single top-level XEX or fabricate disc numbers without authentic per-disc identity
    std::filesystem::path fakeLayoutDir = tempDir / "fakeLayout";
    std::filesystem::create_directories(fakeLayoutDir);
    auto topXex = MakeXex(1, mediaMap[1], 4, 4);
    {
        std::ofstream topXexOut(fakeLayoutDir / "default.xex", std::ios::binary);
        topXexOut.write(reinterpret_cast<const char*>(topXex.data()), topXex.size());
    }
    for (uint32_t d = 1; d <= 4; ++d)
    {
        // Missing per-disc default.xex
        WriteDiscFiles(fakeLayoutDir / ("disc" + std::to_string(d)), d, false);
    }

    bool fakeRejected = false;
    try
    {
        // When scanning fake layout, the top folder only has Disc 1 XEX without required disc files at top level
        install::ScanSource(fakeLayoutDir);
    }
    catch (const install::Error& err)
    {
        fakeRejected = true;
    }
    assert(fakeRejected && "Should not fabricate disc identities from missing per-disc XEX");
    std::cout << "[PASS] Rejection of fabricated disc layout" << std::endl;

    // 4. Test transactional installation of standard 4-disc set
    std::filesystem::path destGame = tempDir / "destGame";
    uint64_t progressReported = 0;
    std::vector<int> installed;

    try
    {
        installed = install::InstallDiscs(sourceStandard, destGame,
            [&](uint64_t done, uint64_t total, std::string_view label) {
                progressReported = done;
                (void)total;
                (void)label;
            });
    }
    catch (const std::exception& ex)
    {
        std::cerr << "InstallDiscs exception: " << ex.what() << std::endl;
        assert(false);
    }
    std::cout << "InstallDiscs returned " << installed.size() << " discs" << std::endl;

    assert(installed.size() == 4);
    assert(installed[0] == 1 && installed[1] == 2 && installed[2] == 3 && installed[3] == 4);
    assert(progressReported > 0);

    // Verify destination files exist
    for (uint32_t d = 1; d <= 4; ++d)
    {
        assert(std::filesystem::exists(destGame / ("disc" + std::to_string(d)) / "default.xex"));
        assert(std::filesystem::exists(destGame / ("disc" + std::to_string(d)) / "lo.fpd"));
        assert(std::filesystem::exists(destGame / ("disc" + std::to_string(d)) / "import-info.json"));
    }
    assert(!std::filesystem::exists(destGame / ".import.lock"));
    std::cout << "[PASS] Transactional installation of 4 discs" << std::endl;

    // 5. Test DefaultGameDirectory and WriteGamePath
    auto defaultDir = install::DefaultGameDirectory(tempDir / "bin");
    assert(defaultDir == (tempDir / "game").lexically_normal());

    std::string writeErr;
    bool writeOk = install::WriteGamePath(tempDir, destGame, writeErr);
    assert(writeOk);
    assert(std::filesystem::exists(tempDir / "game-path.txt"));
    std::cout << "[PASS] Game path persistence" << std::endl;

    // 6. Test cancellation during scan/install
    bool cancelled = false;
    try
    {
        install::ScanSource(sourceStandard, []() { return true; });
        assert(false && "Should have thrown Cancelled Error");
    }
    catch (const install::Error& err)
    {
        assert(err.cancelled());
        cancelled = true;
    }
    assert(cancelled);
    std::cout << "[PASS] Cancellation handling" << std::endl;

    // 7. Test cancellation rollback during install
    std::filesystem::path cancelDest = tempDir / "cancelDest";
    bool installCancelled = false;
    try
    {
        install::InstallDiscs(sourceStandard, cancelDest, {}, []() { return true; });
    }
    catch (const install::Error& err)
    {
        assert(err.cancelled());
        installCancelled = true;
    }
    assert(installCancelled);
    // Verify rollback: no published discs or locks remain
    assert(!std::filesystem::exists(cancelDest / "disc1"));
    assert(!std::filesystem::exists(cancelDest / ".import.lock"));
    std::cout << "[PASS] Installation rollback on cancellation" << std::endl;

    // 7b. Test mixed edition rejection in scan and install
    std::filesystem::path mixedSource = tempDir / "mixedSource";
    std::filesystem::create_directories(mixedSource / "disc1");
    std::filesystem::create_directories(mixedSource / "disc2");
    WriteDiscFiles(mixedSource / "disc1", 1, true); // Asia Disc 1
    // Write USA/Europe Disc 2
    static const uint32_t euMediaMap[5] = {0, 0x368DE6DD, 0x1888BE4E, 0x6DD59D08, 0x0C0E80B5};
    auto euXex2 = MakeXex(2, euMediaMap[2], 3, 3);
    install::SetTestSha256(2, install::crypto::Sha256Hex(euXex2.data(), euXex2.size()), true);
    {
        std::ofstream out(mixedSource / "disc2" / "default.xex", std::ios::binary);
        out.write(reinterpret_cast<const char*>(euXex2.data()), euXex2.size());
    }
    static const char* reqFiles[] = {
        "lo.fpd", "lo.fpi", "xenon_battle.fpd", "xenon_chr.fpd", "xenon_event.fpd",
        "xenon_field.fpd", "xenon_loc.fpd", "xenon_mov.fpd", "xenon_obj.fpd",
        "xenon_scr.fpd", "xenon_snd.fpd", "xenon_sys.fpd", "xenon_vfx.fpd", "xenon_world.fpd"
    };
    for (const char* name : reqFiles)
    {
        std::ofstream out(mixedSource / "disc2" / name, std::ios::binary);
        out.write("payload", 7);
    }

    bool mixedRejected = false;
    try
    {
        install::ScanContent(mixedSource);
    }
    catch (const install::Error& err)
    {
        mixedRejected = (std::string(err.what()).find("Cannot mix") != std::string::npos);
    }
    assert(mixedRejected && "Mixed editions must be rejected");
    std::cout << "[PASS] Rejection of mixed editions" << std::endl;

    // 7c. Test isolated DLC installation and cancellation
    std::filesystem::path asiaDlcSrc = (std::filesystem::path("G:/ROMS/X360CH176/DLC") / (const char8_t*)u8"2号DLC：「獎勵物品二合一組」");
    if (std::filesystem::exists(asiaDlcSrc))
    {
        std::filesystem::path dlcTestDest = tempDir / "dlcTestGame";
        // Pre-create fake installed disc1 so dlc installer recognizes game root
        std::filesystem::create_directories(dlcTestDest / "disc1");
        {
            auto d1xex = MakeXex(1, mediaMap[1], 4, 4);
            std::ofstream out(dlcTestDest / "disc1" / "default.xex", std::ios::binary);
            out.write(reinterpret_cast<const char*>(d1xex.data()), d1xex.size());
        }

        auto dlcScan = install::ScanContent(asiaDlcSrc);
        assert(dlcScan.packages.size() == 1);
        auto pkg = dlcScan.packages[0];

        // First test cancellation rollback
        bool dlcCancelled = false;
        try
        {
            install::InstallContent(dlcScan, dlcTestDest, {}, []() { return true; });
        }
        catch (const install::Error& err)
        {
            assert(err.cancelled());
            dlcCancelled = true;
        }
        assert(dlcCancelled);
        assert(!std::filesystem::exists(dlcTestDest / "dlc" / pkg.contentId));
        assert(!std::filesystem::exists(dlcTestDest / ".import.lock"));

        // Now install cleanly
        auto res = install::InstallContent(dlcScan, dlcTestDest);
        assert(res.dlcImported.size() == 1);
        assert(res.dlcImported[0] == pkg.contentId);
        assert(std::filesystem::exists(dlcTestDest / "dlc" / pkg.contentId / ".lo-content"));
        assert(std::filesystem::exists(dlcTestDest / "dlc" / pkg.contentId / ".lo-dlc-header"));
        assert(std::filesystem::exists(dlcTestDest / "dlc" / pkg.contentId / ".lo-dlc.json"));

        // Verify re-importing unchanged DLC is recognized as unchanged
        auto res2 = install::InstallContent(dlcScan, dlcTestDest);
        assert(res2.dlcUnchanged.size() == 1);
        assert(res2.dlcUnchanged[0] == pkg.contentId);
        std::cout << "[PASS] Isolated DLC installation, rollback, and duplicate detection" << std::endl;
    }

    install::ClearTestOverrides();

    // 8. Test Real Sources Read-Only Verification (if present)
    std::cout << "\nVerifying real authoritative read-only sources:" << std::endl;

    // A. G:/ROMS/US (USA/Europe ISOs)
    std::filesystem::path usIsoPath("G:/ROMS/US");
    if (std::filesystem::exists(usIsoPath))
    {
        auto scan = install::ScanContent(usIsoPath);
        std::cout << "[REAL SOURCE] G:/ROMS/US - Discs found: " << scan.discs.size() << std::endl;
        assert(scan.discs.size() == 4);
        for (size_t i = 0; i < scan.discs.size(); ++i)
        {
            const auto& d = scan.discs[i];
            std::cout << "  Disc " << d.disc << " [" << (d.kind == install::Kind::Iso ? "ISO" : "Other") << "]: "
                      << d.path.filename().string() << "\n    Edition: " << d.edition
                      << ", Media: " << d.media << ", SHA256: " << d.sha256.substr(0, 16) << "..." << std::endl;
            assert(d.disc == static_cast<uint32_t>(i + 1));
            assert(d.edition == "usa-europe");
            assert(d.kind == install::Kind::Iso);
        }
        std::cout << "[PASS] Real USA/Europe ISOs verified successfully" << std::endl;
    }

    // B. G:/ROMS/X360CH176 (Asia GOD + DLC)
    std::filesystem::path asiaPath("G:/ROMS/X360CH176");
    if (std::filesystem::exists(asiaPath))
    {
        auto scan = install::ScanContent(asiaPath);
        std::cout << "[REAL SOURCE] G:/ROMS/X360CH176 - Discs found: " << scan.discs.size()
                  << ", Packages found: " << scan.packages.size()
                  << ", Rejected: " << scan.rejected.size() << std::endl;
        for (const auto& r : scan.rejected)
        {
            std::cout << "  REJECTED: " << r.first.string() << " -> " << r.second << std::endl;
        }
        assert(scan.discs.size() == 4);
        for (size_t i = 0; i < scan.discs.size(); ++i)
        {
            const auto& d = scan.discs[i];
            std::cout << "  Disc " << d.disc << " [" << (d.kind == install::Kind::God ? "GOD" : "Other") << "]: "
                      << d.path.filename().string() << "\n    Edition: " << d.edition
                      << ", Media: " << d.media << ", SHA256: " << d.sha256.substr(0, 16) << "..." << std::endl;
            assert(d.disc == static_cast<uint32_t>(i + 1));
            assert(d.edition == "asia");
            assert(d.kind == install::Kind::God);
        }

        assert(scan.packages.size() == 3);
        for (const auto& pkg : scan.packages)
        {
            std::cout << "  DLC Package: " << pkg.displayName << "\n    ID: " << pkg.contentId
                      << ", Files: " << pkg.files << ", Bytes: " << pkg.bytes
                      << ", SHA256: " << pkg.sourceSha256.substr(0, 16) << "..." << std::endl;
        }
        std::cout << "[PASS] Real Asia GOD + DLC verified successfully" << std::endl;
    }

    // C. D:/Mihoyo/LostOdysseyRecomp-windows-x64/game (Extracted dump)
    std::filesystem::path dumpPath("D:/Mihoyo/LostOdysseyRecomp-windows-x64/game");
    if (std::filesystem::exists(dumpPath))
    {
        auto scan = install::ScanContent(dumpPath);
        std::cout << "[REAL SOURCE] D:/Mihoyo/LostOdysseyRecomp-windows-x64/game - Discs found: "
                  << scan.discs.size() << std::endl;
        assert(scan.discs.size() == 4);
        for (size_t i = 0; i < scan.discs.size(); ++i)
        {
            const auto& d = scan.discs[i];
            std::cout << "  Disc " << d.disc << " [Folder]: " << d.path.filename().string()
                      << "\n    Edition: " << d.edition << ", Media: " << d.media
                      << ", SHA256: " << d.sha256.substr(0, 16) << "..." << std::endl;
            assert(d.disc == static_cast<uint32_t>(i + 1));
            assert(d.edition == "asia");
            assert(d.kind == install::Kind::Folder);
        }
        std::cout << "[PASS] Real Extracted Folder dump verified successfully" << std::endl;
    }

    std::cout << "\nALL LO_IMPORT_GAME_TEST CHECKS PASSED!" << std::endl;
    return 0;
}
