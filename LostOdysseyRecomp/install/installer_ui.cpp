#include "installer_ui.h"
#include "installer_font.h"
#include "file_browser.h"
#include "import_game.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <SDL.h>

namespace install
{
namespace
{
// Visual Design Palette: Lost Odyssey Game Menu Aesthetic
// Warm slate / brushed steel panels, bevel lines, high-contrast gold/amber highlights,
// dark active item text on metallic bright selection bar.
struct Color
{
    uint8_t r, g, b, a = 255;
};

constexpr Color COLOR_STEEL{ 55, 58, 60, 255 };          // Dark steel/slate background
constexpr Color COLOR_STEEL_PANEL{ 45, 48, 50, 255 };    // Panel interior
constexpr Color COLOR_RAIL{ 38, 40, 42, 255 };           // Dark rail / well
constexpr Color COLOR_BORDER_LIGHT{ 120, 124, 125, 255 };// Top/Left bevel highlight
constexpr Color COLOR_BORDER_DARK{ 25, 26, 27, 255 };    // Bottom/Right bevel shadow
constexpr Color COLOR_BORDER_LINE{ 70, 73, 75, 255 };    // Clean separator line

constexpr Color COLOR_ACCENT_GOLD{ 235, 205, 130, 255 }; // Lost Odyssey Title Gold
constexpr Color COLOR_CYAN{ 110, 210, 230, 255 };        // Directory cyan
constexpr Color COLOR_GREEN{ 140, 205, 150, 255 };       // Success / verified green
constexpr Color COLOR_RED{ 225, 85, 75, 255 };           // Alert red
constexpr Color COLOR_INK{ 245, 245, 242, 255 };         // Clean crisp white/cream text
constexpr Color COLOR_MUTED{ 165, 170, 172, 255 };       // Subdued gray text

// Game-style selected item bar: brushed silver/light gray surface with dark text
constexpr Color COLOR_SEL_SURFACE{ 205, 210, 212, 255 }; // Light metallic highlight
constexpr Color COLOR_SEL_INK{ 25, 28, 30, 255 };        // Crisp dark text on highlight
constexpr Color COLOR_SEL_TOP{ 245, 248, 250, 255 };     // Top edge of selection bar
constexpr Color COLOR_SEL_BOTTOM{ 135, 140, 142, 255 };  // Bottom edge of selection bar

void SetDrawColor(SDL_Renderer* renderer, Color c)
{
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

void DrawRect(SDL_Renderer* renderer, int x, int y, int w, int h, Color c)
{
    SetDrawColor(renderer, c);
    SDL_Rect r{ x, y, w, h };
    SDL_RenderDrawRect(renderer, &r);
}

void FillRect(SDL_Renderer* renderer, int x, int y, int w, int h, Color c)
{
    SetDrawColor(renderer, c);
    SDL_Rect r{ x, y, w, h };
    SDL_RenderFillRect(renderer, &r);
}

// Lost Odyssey signature beveled metallic frame
void DrawBevelPanel(SDL_Renderer* renderer, int x, int y, int w, int h, Color fillCol)
{
    FillRect(renderer, x, y, w, h, fillCol);
    // Bevel highlights
    SetDrawColor(renderer, COLOR_BORDER_LIGHT);
    SDL_RenderDrawLine(renderer, x, y, x + w - 1, y);
    SDL_RenderDrawLine(renderer, x, y, x, y + h - 1);
    // Bevel shadows
    SetDrawColor(renderer, COLOR_BORDER_DARK);
    SDL_RenderDrawLine(renderer, x, y + h - 1, x + w - 1, y + h - 1);
    SDL_RenderDrawLine(renderer, x + w - 1, y, x + w - 1, y + h - 1);
}

// Lost Odyssey signature selection row: metallic pill/bar with crisp bevel
void DrawSelectionBar(SDL_Renderer* renderer, int x, int y, int w, int h)
{
    FillRect(renderer, x, y, w, h, COLOR_SEL_SURFACE);
    SetDrawColor(renderer, COLOR_SEL_TOP);
    SDL_RenderDrawLine(renderer, x, y, x + w - 1, y);
    SDL_RenderDrawLine(renderer, x, y + 1, x + w - 1, y + 1);
    SetDrawColor(renderer, COLOR_SEL_BOTTOM);
    SDL_RenderDrawLine(renderer, x, y + h - 1, x + w - 1, y + h - 1);
    SDL_RenderDrawLine(renderer, x, y + h - 2, x + w - 1, y + h - 2);
}

// Controller button prompt chip (e.g. [A], [B], [X])
void DrawButtonPrompt(SDL_Renderer* renderer, int x, int y, std::string_view btn, std::string_view label, Color btnColor)
{
    int btnW = ui::MeasureTextWidth(btn, 1.0f) + 8;
    int btnH = 20;
    FillRect(renderer, x, y, btnW, btnH, btnColor);
    DrawRect(renderer, x, y, btnW, btnH, COLOR_BORDER_DARK);
    ui::DrawString(renderer, x + 4, y + 2, btn, 20, 20, 20, 255, 1.0f);
    ui::DrawString(renderer, x + btnW + 6, y + 2, label, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
}

std::string FormatBytes(uint64_t bytes)
{
    double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (gib >= 1.0)
    {
        ss << gib << " GiB";
    }
    else
    {
        ss << mib << " MiB";
    }
    return ss.str();
}

std::string EllipsizePath(const std::string& path, size_t maxLen)
{
    if (path.length() <= maxLen) return path;
    if (maxLen <= 5) return path.substr(0, maxLen);
    return "..." + path.substr(path.length() - (maxLen - 3));
}

enum class ScreenState
{
    BrowseSource,
    ReviewDiscs,
    BrowseDest,
    Importing,
    Complete,
    Error
};

struct UIState
{
    ScreenState screen = ScreenState::BrowseSource;

    // Source browsing
    std::filesystem::path currentSourceBrowse;
    std::vector<ui::DirectoryItem> sourceItems;
    int sourceSelectedIndex = 0;
    int sourceScrollOffset = 0;
    std::vector<ui::DirectoryItem> roots;
    int rootSelectedIndex = 0;
    bool focusOnRoots = false;

    // Scan result
    std::filesystem::path selectedSource;
    Scan scanResult;
    std::string scanError;
    std::atomic<bool> isScanning{ false };
    int reviewSelectedIndex = 0;

    // Destination browsing
    std::filesystem::path selectedDest;
    std::filesystem::path currentDestBrowse;
    std::vector<ui::DirectoryItem> destItems;
    int destSelectedIndex = 0;
    int destScrollOffset = 0;
    bool destFocusOnRoots = false;
    int destRootSelectedIndex = 0;

    // Import progress
    std::atomic<bool> isImporting{ false };
    std::atomic<bool> cancelRequested{ false };
    std::atomic<uint64_t> progressDone{ 0 };
    std::atomic<uint64_t> progressTotal{ 1 };
    std::mutex progressMutex;
    std::string progressLabel;
    std::string importError;
    std::vector<int> installedDiscs;

    // UI flags
    bool quit = false;
    bool userCancelled = false;
    bool installSuccess = false;
};

} // namespace

InstallerResult ShowInstallerUI(const std::filesystem::path& executableDirectory,
                                const std::filesystem::path& initialSource,
                                const std::filesystem::path& initialDest)
{
    InstallerResult result;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0)
    {
        result.error = std::string("SDL_Init failed: ") + SDL_GetError();
        return result;
    }

    std::vector<SDL_GameController*> controllers;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            if (auto* pad = SDL_GameControllerOpen(i))
            {
                controllers.push_back(pad);
            }
        }
    }

    constexpr int WIN_WIDTH = 980;
    constexpr int WIN_HEIGHT = 680;

    SDL_Window* window = SDL_CreateWindow(
        "Lost Odyssey Recomp - Game Content Installer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_WIDTH, WIN_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window)
    {
        result.error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        SDL_Quit();
        return result;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer)
    {
        result.error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    UIState state;
    state.roots = ui::GetSystemRoots();

    std::filesystem::path startSource = initialSource;
    if (startSource.empty() || !std::filesystem::exists(startSource))
    {
        std::error_code ec;
        auto cjkCandidate = std::filesystem::current_path() / "中文测试_LostOdyssey光盘";
        if (std::filesystem::is_directory(cjkCandidate, ec))
        {
            startSource = cjkCandidate;
        }
        else
        {
            startSource = std::filesystem::current_path();
        }
    }
    state.currentSourceBrowse = startSource;
    state.sourceItems = ui::ListDirectory(state.currentSourceBrowse);
    // If the list has at least two items, select index 1 (the first actual item below '..')
    if (state.sourceItems.size() > 1)
    {
        state.sourceSelectedIndex = 1;
    }

    std::filesystem::path defaultDest = initialDest;
    if (defaultDest.empty())
    {
        defaultDest = DefaultGameDirectory(executableDirectory);
    }
    state.selectedDest = defaultDest;
    state.currentDestBrowse = defaultDest.parent_path();
    if (!std::filesystem::exists(state.currentDestBrowse))
    {
        state.currentDestBrowse = std::filesystem::current_path();
    }
    state.destItems = ui::ListDirectory(state.currentDestBrowse);

    std::thread workerThread;

    auto startScan = [&](const std::filesystem::path& srcPath) {
        if (state.isScanning.load()) return;
        state.isScanning = true;
        state.selectedSource = srcPath;
        state.scanError.clear();

        if (workerThread.joinable()) workerThread.join();

        workerThread = std::thread([&state, srcPath]() {
            try
            {
                state.scanResult = ScanSource(srcPath);
            }
            catch (const std::exception& e)
            {
                state.scanError = e.what();
            }
            state.isScanning = false;
        });
    };

    auto startImport = [&]() {
        if (state.isImporting.load()) return;
        state.isImporting = true;
        state.cancelRequested = false;
        state.progressDone = 0;
        state.progressTotal = 1;
        state.importError.clear();
        state.screen = ScreenState::Importing;

        if (workerThread.joinable()) workerThread.join();

        workerThread = std::thread([&state, executableDirectory]() {
            try
            {
                auto progressCb = [&](uint64_t done, uint64_t total, std::string_view label) {
                    state.progressDone = done;
                    state.progressTotal = std::max<uint64_t>(1, total);
                    std::lock_guard<std::mutex> lock(state.progressMutex);
                    state.progressLabel = std::string(label);
                };
                auto cancelCb = [&]() -> bool {
                    return state.cancelRequested.load();
                };

                state.installedDiscs = InstallDiscs(state.selectedSource, state.selectedDest, progressCb, cancelCb);

                std::string err;
                if (!WriteGamePath(executableDirectory, state.selectedDest, err))
                {
                    state.importError = "Failed to update game-path.txt: " + err;
                    state.screen = ScreenState::Error;
                }
                else
                {
                    state.installSuccess = true;
                    state.screen = ScreenState::Complete;
                }
            }
            catch (const Error& err)
            {
                if (err.cancelled())
                {
                    state.userCancelled = true;
                    state.screen = ScreenState::ReviewDiscs;
                }
                else
                {
                    state.importError = err.what();
                    state.screen = ScreenState::Error;
                }
            }
            catch (const std::exception& ex)
            {
                state.importError = ex.what();
                state.screen = ScreenState::Error;
            }
            state.isImporting = false;
        });
    };

    auto refreshSourceList = [&]() {
        state.sourceItems = ui::ListDirectory(state.currentSourceBrowse);
        state.sourceSelectedIndex = 0;
        state.sourceScrollOffset = 0;
    };

    auto refreshDestList = [&]() {
        state.destItems = ui::ListDirectory(state.currentDestBrowse);
        state.destSelectedIndex = 0;
        state.destScrollOffset = 0;
    };

    constexpr int VISIBLE_ITEMS = 14;

    auto handleNavUp = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            if (state.focusOnRoots)
            {
                if (state.rootSelectedIndex > 0) state.rootSelectedIndex--;
            }
            else
            {
                if (state.sourceSelectedIndex > 0)
                {
                    state.sourceSelectedIndex--;
                    if (state.sourceSelectedIndex < state.sourceScrollOffset)
                        state.sourceScrollOffset = state.sourceSelectedIndex;
                }
            }
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            if (state.destFocusOnRoots)
            {
                if (state.destRootSelectedIndex > 0) state.destRootSelectedIndex--;
            }
            else
            {
                if (state.destSelectedIndex > 0)
                {
                    state.destSelectedIndex--;
                    if (state.destSelectedIndex < state.destScrollOffset)
                        state.destScrollOffset = state.destSelectedIndex;
                }
            }
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            if (state.reviewSelectedIndex > 0) state.reviewSelectedIndex--;
        }
    };

    auto handleNavDown = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            if (state.focusOnRoots)
            {
                if (state.rootSelectedIndex + 1 < static_cast<int>(state.roots.size())) state.rootSelectedIndex++;
            }
            else
            {
                if (state.sourceSelectedIndex + 1 < static_cast<int>(state.sourceItems.size()))
                {
                    state.sourceSelectedIndex++;
                    if (state.sourceSelectedIndex >= state.sourceScrollOffset + VISIBLE_ITEMS)
                        state.sourceScrollOffset = state.sourceSelectedIndex - VISIBLE_ITEMS + 1;
                }
            }
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            if (state.destFocusOnRoots)
            {
                if (state.destRootSelectedIndex + 1 < static_cast<int>(state.roots.size())) state.destRootSelectedIndex++;
            }
            else
            {
                if (state.destSelectedIndex + 1 < static_cast<int>(state.destItems.size()))
                {
                    state.destSelectedIndex++;
                    if (state.destSelectedIndex >= state.destScrollOffset + VISIBLE_ITEMS)
                        state.destScrollOffset = state.destSelectedIndex - VISIBLE_ITEMS + 1;
                }
            }
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            int maxIdx = static_cast<int>(state.scanResult.discs.size());
            if (state.reviewSelectedIndex < maxIdx + 2) state.reviewSelectedIndex++;
        }
    };

    auto handleNavLeft = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            state.focusOnRoots = true;
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            state.destFocusOnRoots = true;
        }
    };

    auto handleNavRight = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            state.focusOnRoots = false;
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            state.destFocusOnRoots = false;
        }
    };

    auto handleAction = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            if (state.focusOnRoots)
            {
                if (state.rootSelectedIndex >= 0 && state.rootSelectedIndex < static_cast<int>(state.roots.size()))
                {
                    state.currentSourceBrowse = state.roots[state.rootSelectedIndex].path;
                    refreshSourceList();
                    state.focusOnRoots = false;
                }
            }
            else
            {
                if (state.sourceSelectedIndex >= 0 && state.sourceSelectedIndex < static_cast<int>(state.sourceItems.size()))
                {
                    const auto& item = state.sourceItems[state.sourceSelectedIndex];
                    if (item.isDirectory)
                    {
                        std::error_code ec;
                        auto can = std::filesystem::canonical(item.path, ec);
                        state.currentSourceBrowse = ec ? item.path : can;
                        refreshSourceList();
                    }
                }
            }
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            if (state.destFocusOnRoots)
            {
                if (state.destRootSelectedIndex >= 0 && state.destRootSelectedIndex < static_cast<int>(state.roots.size()))
                {
                    state.currentDestBrowse = state.roots[state.destRootSelectedIndex].path;
                    refreshDestList();
                    state.destFocusOnRoots = false;
                }
            }
            else
            {
                if (state.destSelectedIndex >= 0 && state.destSelectedIndex < static_cast<int>(state.destItems.size()))
                {
                    const auto& item = state.destItems[state.destSelectedIndex];
                    if (item.isDirectory)
                    {
                        std::error_code ec;
                        auto can = std::filesystem::canonical(item.path, ec);
                        state.currentDestBrowse = ec ? item.path : can;
                        refreshDestList();
                    }
                }
            }
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            int discCount = static_cast<int>(state.scanResult.discs.size());
            if (state.reviewSelectedIndex == discCount)
            {
                startImport();
            }
            else if (state.reviewSelectedIndex == discCount + 1)
            {
                state.screen = ScreenState::BrowseDest;
            }
            else if (state.reviewSelectedIndex == discCount + 2)
            {
                state.screen = ScreenState::BrowseSource;
            }
        }
        else if (state.screen == ScreenState::Complete)
        {
            state.quit = true;
        }
        else if (state.screen == ScreenState::Error)
        {
            state.screen = ScreenState::ReviewDiscs;
        }
    };

    auto handleSelectCurrent = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            startScan(state.currentSourceBrowse);
            state.screen = ScreenState::ReviewDiscs;
            state.reviewSelectedIndex = static_cast<int>(state.scanResult.discs.size());
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            state.selectedDest = state.currentDestBrowse;
            state.screen = ScreenState::ReviewDiscs;
        }
    };

    auto handleCancel = [&]() {
        if (state.screen == ScreenState::BrowseSource)
        {
            state.quit = true;
            state.userCancelled = true;
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            state.screen = ScreenState::BrowseSource;
        }
        else if (state.screen == ScreenState::BrowseDest)
        {
            state.screen = ScreenState::ReviewDiscs;
        }
        else if (state.screen == ScreenState::Importing)
        {
            state.cancelRequested = true;
        }
        else if (state.screen == ScreenState::Complete || state.screen == ScreenState::Error)
        {
            state.quit = true;
        }
    };

    while (!state.quit)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                state.quit = true;
                state.userCancelled = true;
                if (state.isImporting.load())
                {
                    state.cancelRequested = true;
                }
                break;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    handleCancel();
                    break;
                case SDLK_UP:
                case SDLK_w:
                    handleNavUp();
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                    handleNavDown();
                    break;
                case SDLK_LEFT:
                case SDLK_a:
                    handleNavLeft();
                    break;
                case SDLK_RIGHT:
                case SDLK_d:
                    handleNavRight();
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    handleAction();
                    break;
                case SDLK_TAB:
                case SDLK_f:
                    handleSelectCurrent();
                    break;
                case SDLK_BACKSPACE:
                    if (state.screen == ScreenState::BrowseSource && !state.focusOnRoots)
                    {
                        state.currentSourceBrowse = state.currentSourceBrowse.parent_path();
                        refreshSourceList();
                    }
                    else if (state.screen == ScreenState::BrowseDest && !state.destFocusOnRoots)
                    {
                        state.currentDestBrowse = state.currentDestBrowse.parent_path();
                        refreshDestList();
                    }
                    else
                    {
                        handleCancel();
                    }
                    break;
                default:
                    break;
                }
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    handleNavUp();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    handleNavDown();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    handleNavLeft();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    handleNavRight();
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                    handleAction();
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    handleCancel();
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                case SDL_CONTROLLER_BUTTON_Y:
                    handleSelectCurrent();
                    break;
                default:
                    break;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    handleAction();
                }
                break;

            default:
                break;
            }
        }

        // Render pass
        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SetDrawColor(renderer, COLOR_STEEL);
        SDL_RenderClear(renderer);

        // Header: Lost Odyssey Steel Bar
        DrawBevelPanel(renderer, 0, 0, w, 70, COLOR_STEEL_PANEL);
        ui::DrawString(renderer, 28, 16, "LOST ODYSSEY RECOMP", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 1.25f);
        ui::DrawString(renderer, 28, 42, "Content Importer - Disc & Extracted Folder Setup", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.9f);

        // Footer / Controller Prompts Bar
        int footerY = h - 50;
        DrawBevelPanel(renderer, 0, footerY, w, 50, COLOR_STEEL_PANEL);

        // Button chips matching Lost Odyssey controller legend
        int chipX = 24;
        int chipY = footerY + 15;

        if (state.screen == ScreenState::BrowseSource || state.screen == ScreenState::BrowseDest)
        {
            DrawButtonPrompt(renderer, chipX, chipY, "A", "Enter / Open", COLOR_GREEN);
            chipX += 140;
            DrawButtonPrompt(renderer, chipX, chipY, "X", "Choose Folder", COLOR_CYAN);
            chipX += 150;
            DrawButtonPrompt(renderer, chipX, chipY, "B", "Cancel", COLOR_RED);
            chipX += 110;
            DrawButtonPrompt(renderer, chipX, chipY, "PAD", "Navigate", COLOR_MUTED);
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            DrawButtonPrompt(renderer, chipX, chipY, "A", "Confirm / Select", COLOR_GREEN);
            chipX += 160;
            DrawButtonPrompt(renderer, chipX, chipY, "B", "Back to Browser", COLOR_RED);
            chipX += 160;
            DrawButtonPrompt(renderer, chipX, chipY, "PAD", "Choose Option", COLOR_MUTED);
        }
        else if (state.screen == ScreenState::Importing)
        {
            DrawButtonPrompt(renderer, chipX, chipY, "B", "Cancel Import (Safe Rollback)", COLOR_RED);
        }
        else if (state.screen == ScreenState::Complete)
        {
            DrawButtonPrompt(renderer, chipX, chipY, "A", "Finish / Launch Game", COLOR_GREEN);
        }

        // Body Content
        int bodyY = 80;
        int bodyH = footerY - bodyY - 10;

        if (state.screen == ScreenState::BrowseSource || state.screen == ScreenState::BrowseDest)
        {
            bool isSource = (state.screen == ScreenState::BrowseSource);
            const auto& currentPath = isSource ? state.currentSourceBrowse : state.currentDestBrowse;
            const auto& items = isSource ? state.sourceItems : state.destItems;
            int selIdx = isSource ? state.sourceSelectedIndex : state.destSelectedIndex;
            int scrollOff = isSource ? state.sourceScrollOffset : state.destScrollOffset;
            bool onRoots = isSource ? state.focusOnRoots : state.destFocusOnRoots;
            int rootIdx = isSource ? state.rootSelectedIndex : state.destRootSelectedIndex;

            // Left panel: Drives / Roots
            int rootsW = 160;
            DrawBevelPanel(renderer, 20, bodyY, rootsW, bodyH, COLOR_STEEL_PANEL);
            ui::DrawString(renderer, 32, bodyY + 14, "SYSTEM DRIVES", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);
            SetDrawColor(renderer, COLOR_BORDER_LINE);
            SDL_RenderDrawLine(renderer, 24, bodyY + 34, 20 + rootsW - 4, bodyY + 34);

            int rootItemY = bodyY + 44;
            int rootItemH = 28;
            for (size_t i = 0; i < state.roots.size(); ++i)
            {
                bool isSel = (onRoots && static_cast<int>(i) == rootIdx);
                int rowY = rootItemY + static_cast<int>(i) * rootItemH;

                if (isSel)
                {
                    DrawSelectionBar(renderer, 24, rowY, rootsW - 8, rootItemH - 2);
                    // Perfectly centered text vertically inside the selection bar
                    int textY = rowY + (rootItemH - 2 - 16) / 2;
                    ui::DrawString(renderer, 36, textY, state.roots[i].name, COLOR_SEL_INK.r, COLOR_SEL_INK.g, COLOR_SEL_INK.b, 255, 1.0f);
                }
                else
                {
                    int textY = rowY + (rootItemH - 2 - 16) / 2;
                    ui::DrawString(renderer, 36, textY, state.roots[i].name, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
                }
            }

            // Right panel: Directory Browser
            int mainX = 20 + rootsW + 14;
            int mainW = w - mainX - 20;
            DrawBevelPanel(renderer, mainX, bodyY, mainW, bodyH, COLOR_STEEL_PANEL);

            std::string headerTitle = isSource ? "SOURCE SELECTION (Extracted Folder / Discs)" : "DESTINATION DIRECTORY SELECTION";
            ui::DrawString(renderer, mainX + 16, bodyY + 14, headerTitle, COLOR_CYAN.r, COLOR_CYAN.g, COLOR_CYAN.b, 255, 0.95f);

            // Path bar well
            std::u8string u8p = currentPath.u8string();
            std::string pathStr(reinterpret_cast<const char*>(u8p.data()), u8p.size());
            FillRect(renderer, mainX + 14, bodyY + 36, mainW - 28, 28, COLOR_RAIL);
            DrawRect(renderer, mainX + 14, bodyY + 36, mainW - 28, 28, COLOR_BORDER_LINE);

            // Truncate path cleanly if needed
            int availPathW = mainW - 44;
            std::string displayPath = pathStr;
            if (ui::MeasureTextWidth(displayPath, 0.95f) > availPathW)
            {
                displayPath = EllipsizePath(pathStr, 65);
            }
            ui::DrawString(renderer, mainX + 22, bodyY + 42, displayPath, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 0.95f);

            // Item list
            int listY = bodyY + 76;
            int rowH = 28;
            int maxVisible = (bodyH - 90) / rowH;

            for (int i = 0; i < maxVisible && (i + scrollOff) < static_cast<int>(items.size()); ++i)
            {
                int itemIdx = i + scrollOff;
                const auto& item = items[itemIdx];
                int itemY = listY + i * rowH;

                bool isSelected = (!onRoots && itemIdx == selIdx);
                int textY = itemY + (rowH - 2 - 16) / 2;

                if (isSelected)
                {
                    DrawSelectionBar(renderer, mainX + 14, itemY, mainW - 28, rowH - 2);

                    std::string icon = item.isDirectory ? "[DIR]  " : "[FILE] ";
                    std::string fullText = icon + item.name;
                    ui::DrawString(renderer, mainX + 24, textY, fullText, COLOR_SEL_INK.r, COLOR_SEL_INK.g, COLOR_SEL_INK.b, 255, 1.0f);
                }
                else
                {
                    std::string icon = item.isDirectory ? "[DIR]  " : "[FILE] ";
                    Color textCol = item.isDirectory ? COLOR_CYAN : COLOR_MUTED;
                    ui::DrawString(renderer, mainX + 24, textY, icon + item.name, textCol.r, textCol.g, textCol.b, 255, 1.0f);
                }
            }

            if (items.empty())
            {
                ui::DrawString(renderer, mainX + 24, listY + 12, "(Directory is empty)", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 1.0f);
            }
        }
        else if (state.screen == ScreenState::ReviewDiscs)
        {
            // Review Discs Screen
            int cardW = w - 40;
            DrawBevelPanel(renderer, 20, bodyY, cardW, bodyH, COLOR_STEEL_PANEL);

            ui::DrawString(renderer, 40, bodyY + 20, "SCANNED GAME DISCS REVIEW", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 1.25f);

            std::string sourceInfo = "Source: " + state.selectedSource.string();
            ui::DrawString(renderer, 40, bodyY + 54, sourceInfo, COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);

            std::string destInfo = "Destination: " + state.selectedDest.string();
            ui::DrawString(renderer, 40, bodyY + 76, destInfo, COLOR_CYAN.r, COLOR_CYAN.g, COLOR_CYAN.b, 255, 0.95f);

            if (state.isScanning.load())
            {
                ui::DrawString(renderer, 40, bodyY + 120, "Scanning files and calculating hashes... Please wait.", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 1.0f);
            }
            else if (!state.scanError.empty())
            {
                ui::DrawString(renderer, 40, bodyY + 120, "Scan Error: " + state.scanError, COLOR_RED.r, COLOR_RED.g, COLOR_RED.b, 255, 1.0f);
            }
            else
            {
                int tableY = bodyY + 110;
                FillRect(renderer, 40, tableY, cardW - 80, 26, COLOR_RAIL);
                DrawRect(renderer, 40, tableY, cardW - 80, 26, COLOR_BORDER_LINE);
                ui::DrawString(renderer, 50, tableY + 5, "DISC / ITEM", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);
                ui::DrawString(renderer, 220, tableY + 5, "EDITION", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);
                ui::DrawString(renderer, 400, tableY + 5, "FILES", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);
                ui::DrawString(renderer, 500, tableY + 5, "SIZE", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);
                ui::DrawString(renderer, 650, tableY + 5, "STATUS / HASH", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 0.9f);

                int rowY = tableY + 32;
                for (size_t i = 0; i < state.scanResult.discs.size(); ++i)
                {
                    const auto& d = state.scanResult.discs[i];
                    std::string discName = "Disc " + std::to_string(d.disc) + " of " + std::to_string(d.discs);
                    std::string filesStr = std::to_string(d.files);
                    std::string sizeStr = FormatBytes(d.bytes);
                    std::string hashStatus = d.identity.empty() ? "Verified" : d.identity;

                    ui::DrawString(renderer, 50, rowY, discName, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
                    ui::DrawString(renderer, 220, rowY, d.edition, COLOR_CYAN.r, COLOR_CYAN.g, COLOR_CYAN.b, 255, 1.0f);
                    ui::DrawString(renderer, 400, rowY, filesStr, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
                    ui::DrawString(renderer, 500, rowY, sizeStr, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
                    ui::DrawString(renderer, 650, rowY, hashStatus, COLOR_GREEN.r, COLOR_GREEN.g, COLOR_GREEN.b, 255, 1.0f);
                    rowY += 28;
                }

                if (state.scanResult.discs.empty())
                {
                    ui::DrawString(renderer, 50, rowY, "No valid Lost Odyssey discs or XEX files found in this folder.", COLOR_RED.r, COLOR_RED.g, COLOR_RED.b, 255, 1.0f);
                    ui::DrawString(renderer, 50, rowY + 22, "Make sure the folder contains disc1..disc4 or default.xex.", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 1.0f);
                }

                // Action Buttons
                int btnY = bodyY + bodyH - 65;
                int btnW = 230;
                int btnH = 38;
                int discCount = static_cast<int>(state.scanResult.discs.size());

                // Button 1: Start Import
                bool b1Sel = (state.reviewSelectedIndex == discCount);
                if (b1Sel)
                {
                    DrawSelectionBar(renderer, 40, btnY, btnW, btnH);
                    ui::DrawString(renderer, 75, btnY + 10, "START IMPORT", COLOR_SEL_INK.r, COLOR_SEL_INK.g, COLOR_SEL_INK.b, 255, 1.1f);
                }
                else
                {
                    DrawBevelPanel(renderer, 40, btnY, btnW, btnH, COLOR_RAIL);
                    ui::DrawString(renderer, 75, btnY + 10, "START IMPORT", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.1f);
                }

                // Button 2: Change Destination
                bool b2Sel = (state.reviewSelectedIndex == discCount + 1);
                if (b2Sel)
                {
                    DrawSelectionBar(renderer, 40 + btnW + 20, btnY, btnW, btnH);
                    ui::DrawString(renderer, 40 + btnW + 45, btnY + 10, "CHANGE DEST", COLOR_SEL_INK.r, COLOR_SEL_INK.g, COLOR_SEL_INK.b, 255, 1.1f);
                }
                else
                {
                    DrawBevelPanel(renderer, 40 + btnW + 20, btnY, btnW, btnH, COLOR_RAIL);
                    ui::DrawString(renderer, 40 + btnW + 45, btnY + 10, "CHANGE DEST", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.1f);
                }

                // Button 3: Rescan / Change Source
                bool b3Sel = (state.reviewSelectedIndex == discCount + 2);
                if (b3Sel)
                {
                    DrawSelectionBar(renderer, 40 + (btnW + 20) * 2, btnY, btnW, btnH);
                    ui::DrawString(renderer, 40 + (btnW + 20) * 2 + 40, btnY + 10, "CHANGE SOURCE", COLOR_SEL_INK.r, COLOR_SEL_INK.g, COLOR_SEL_INK.b, 255, 1.1f);
                }
                else
                {
                    DrawBevelPanel(renderer, 40 + (btnW + 20) * 2, btnY, btnW, btnH, COLOR_RAIL);
                    ui::DrawString(renderer, 40 + (btnW + 20) * 2 + 40, btnY + 10, "CHANGE SOURCE", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.1f);
                }
            }
        }
        else if (state.screen == ScreenState::Importing)
        {
            int cardW = w - 40;
            DrawBevelPanel(renderer, 20, bodyY, cardW, bodyH, COLOR_STEEL_PANEL);

            ui::DrawString(renderer, 40, bodyY + 30, "IMPORTING GAME CONTENT...", COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 1.25f);

            uint64_t done = state.progressDone.load();
            uint64_t total = state.progressTotal.load();
            double pct = total > 0 ? std::clamp(static_cast<double>(done) / total, 0.0, 1.0) : 0.0;

            std::string label;
            {
                std::lock_guard<std::mutex> lock(state.progressMutex);
                label = state.progressLabel;
            }

            ui::DrawString(renderer, 40, bodyY + 75, label, COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);

            int barY = bodyY + 105;
            int barW = cardW - 80;
            int barH = 30;
            FillRect(renderer, 40, barY, barW, barH, COLOR_RAIL);
            DrawRect(renderer, 40, barY, barW, barH, COLOR_BORDER_LINE);

            int fillW = static_cast<int>(barW * pct);
            if (fillW > 0)
            {
                FillRect(renderer, 42, barY + 2, fillW - 4, barH - 4, COLOR_CYAN);
            }

            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << (pct * 100.0) << "% (" << FormatBytes(done) << " / " << FormatBytes(total) << ")";
            std::string pctStr = ss.str();
            ui::DrawString(renderer, 40, barY + 40, pctStr, COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);

            ui::DrawString(renderer, 40, bodyY + 195, "Files are copied safely to staging and verified before final publish.", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);
            ui::DrawString(renderer, 40, bodyY + 220, "Original source disc files remain untouched.", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);
        }
        else if (state.screen == ScreenState::Complete)
        {
            int cardW = w - 40;
            DrawBevelPanel(renderer, 20, bodyY, cardW, bodyH, COLOR_STEEL_PANEL);

            ui::DrawString(renderer, 40, bodyY + 40, "IMPORT COMPLETED SUCCESSFULLY!", COLOR_GREEN.r, COLOR_GREEN.g, COLOR_GREEN.b, 255, 1.25f);
            ui::DrawString(renderer, 40, bodyY + 80, "Game files were installed to:", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
            ui::DrawString(renderer, 40, bodyY + 105, state.selectedDest.string(), COLOR_CYAN.r, COLOR_CYAN.g, COLOR_CYAN.b, 255, 1.0f);

            std::string summary = "Discs imported: ";
            for (int discNum : state.installedDiscs)
            {
                summary += "Disc " + std::to_string(discNum) + " ";
            }
            ui::DrawString(renderer, 40, bodyY + 140, summary, COLOR_ACCENT_GOLD.r, COLOR_ACCENT_GOLD.g, COLOR_ACCENT_GOLD.b, 255, 1.0f);
            ui::DrawString(renderer, 40, bodyY + 170, "Configuration file game-path.txt has been updated.", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);
            ui::DrawString(renderer, 40, bodyY + 220, "Press [A] or [Enter] to exit installer.", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
        }
        else if (state.screen == ScreenState::Error)
        {
            int cardW = w - 40;
            DrawBevelPanel(renderer, 20, bodyY, cardW, bodyH, COLOR_STEEL_PANEL);

            ui::DrawString(renderer, 40, bodyY + 40, "IMPORT ENCOUNTERED AN ERROR", COLOR_RED.r, COLOR_RED.g, COLOR_RED.b, 255, 1.25f);
            ui::DrawString(renderer, 40, bodyY + 90, "Details:", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
            ui::DrawString(renderer, 40, bodyY + 115, state.importError, COLOR_RED.r, COLOR_RED.g, COLOR_RED.b, 255, 1.0f);

            ui::DrawString(renderer, 40, bodyY + 160, "Any partial files were rolled back. Original game sources were kept safe.", COLOR_MUTED.r, COLOR_MUTED.g, COLOR_MUTED.b, 255, 0.95f);
            ui::DrawString(renderer, 40, bodyY + 200, "Press [B] or [Enter] to return and retry.", COLOR_INK.r, COLOR_INK.g, COLOR_INK.b, 255, 1.0f);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (workerThread.joinable())
    {
        state.cancelRequested = true;
        workerThread.join();
    }

    for (auto* pad : controllers)
    {
        SDL_GameControllerClose(pad);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    result.success = state.installSuccess;
    result.cancelled = state.userCancelled;
    result.destination = state.selectedDest;
    result.error = state.importError;

    return result;
}

} // namespace install
