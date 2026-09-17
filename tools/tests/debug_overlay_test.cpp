#include <debug/menu_overlay.h>
#include <debug/teleport.h>
#include <debug/map_info.h>
#include <host_ui/host_ui.h>
#include <host_ui/rasterizer.h>
#include <settings/config.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace
{
std::mutex g_fixtureMutex;
debug_menu::TeleportSnapshot g_teleport;
std::atomic<bool> g_saveAnywhere{false};

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
}

namespace settings
{
Config GetConfig()
{
    Config config;
    config.debugLanguage = 0;
    return config;
}
bool SaveDebugLanguage(uint32_t) { return true; }
}

namespace gpu::renderer
{
void RequestDebugCapture() {}
std::wstring DebugCaptureStatus() { return {}; }
}

namespace debug_menu
{
TeleportSnapshot GetTeleportSnapshot()
{
    std::lock_guard lock(g_fixtureMutex);
    return g_teleport;
}
bool RequestTeleport(Position) { return true; }
bool RequestTeleportOffset(Position) { return true; }
bool RequestSavePosition() { return true; }
bool RequestRestorePosition() { return true; }
bool RequestPoiTeleport(uint64_t) { return true; }
MapInfo GetMapInfo() { return {}; }
bool SaveAnywhereEnabled() { return g_saveAnywhere.load(); }
void SetSaveAnywhereEnabled(bool enabled) { g_saveAnywhere = enabled; }
bool RequestVictory() { return true; }
void CancelVictory() {}
const wchar_t* Status() { return L""; }
}

int main()
{
    {
        std::lock_guard lock(g_fixtureMutex);
        g_teleport.available = true;
        g_teleport.current = {1.0f, 2.0f, 3.0f};
    }
    debug_menu::ToggleOverlay();
    Require(debug_menu::IsOverlayVisible() && host_ui::IsGamePaused(), "overlay did not open and pause");

    host_ui::PixelBuffer frame;
    Require(frame.Resize(), "overlay frame allocation failed");
    std::thread updates([] {
        for (uint64_t revision = 1; revision <= 1000; ++revision)
        {
            {
                std::lock_guard lock(g_fixtureMutex);
                g_teleport.poiRevision = revision;
                g_teleport.pois = {{revision, L"POI", {float(revision), 2.0f, 3.0f}}};
            }
            debug_menu::UpdateOverlaySnapshot();
        }
    });
    std::thread input([] {
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            debug_menu::HandleInput(debug_menu::InputAction::NextTab);
            debug_menu::HandleInput(debug_menu::InputAction::Down);
            debug_menu::HandleInput(debug_menu::InputAction::Right);
            debug_menu::HandleInput(debug_menu::InputAction::Up);
            debug_menu::HandleInput(debug_menu::InputAction::PrevTab);
        }
    });
    std::thread render([&] {
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            frame.Clear();
            host_ui::Rasterizer rasterizer(frame);
            debug_menu::RenderOverlay(rasterizer);
        }
    });
    updates.join();
    input.join();
    render.join();

    Require(frame.pixels.size() == size_t(host_ui::kOverlayWidth) * host_ui::kOverlayHeight,
            "render changed overlay frame dimensions");
    Require(debug_menu::IsOverlayVisible(), "concurrent menu actions closed overlay");
    debug_menu::HandleInput(debug_menu::InputAction::Cancel);
    Require(!debug_menu::IsOverlayVisible() && !host_ui::IsGamePaused(), "cancel did not close and resume");
    std::puts("debug overlay concurrent update, input, render, and cancel passed");
    return 0;
}
