#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <limits>
#include <vector>
#include <string>

namespace host_ui
{
    // Global pause state for host in-game overlay menus (e.g. F1 Debug Menu)
    inline std::atomic<bool> g_gamePaused{false};
    inline std::mutex g_pauseMutex;
    inline std::condition_variable g_pauseCv;

    inline bool IsGamePaused()
    {
        return g_gamePaused.load(std::memory_order_relaxed);
    }

    inline void SetGamePaused(bool paused)
    {
        {
            std::lock_guard<std::mutex> lock(g_pauseMutex);
            g_gamePaused.store(paused, std::memory_order_release);
        }
        if (!paused)
        {
            g_pauseCv.notify_all();
        }
    }

    // Called by guest threads or wait routines to block while paused
    inline void WaitIfPaused()
    {
        if (!g_gamePaused.load(std::memory_order_relaxed))
            return;

        std::unique_lock<std::mutex> lock(g_pauseMutex);
        g_pauseCv.wait(lock, [] {
            return !g_gamePaused.load(std::memory_order_relaxed);
        });
    }

    // 1280x720 32-bit software frame buffer for overlays
    constexpr uint32_t kOverlayWidth = 1280;
    constexpr uint32_t kOverlayHeight = 720;

    struct PixelBuffer
    {
        uint32_t width = kOverlayWidth;
        uint32_t height = kOverlayHeight;
        // Packed so little-endian memory is R, G, B, A for R8G8B8A8_UNORM uploads.
        std::vector<uint32_t> pixels;

        bool Resize(uint32_t w = kOverlayWidth, uint32_t h = kOverlayHeight)
        {
            if (!w || !h || size_t(w) > std::numeric_limits<size_t>::max() / size_t(h))
            {
                width = height = 0;
                pixels.clear();
                return false;
            }
            width = w;
            height = h;
            pixels.assign(size_t(w) * h, 0);
            return true;
        }

        void Clear(uint32_t color = 0)
        {
            std::fill(pixels.begin(), pixels.end(), color);
        }
    };
}
