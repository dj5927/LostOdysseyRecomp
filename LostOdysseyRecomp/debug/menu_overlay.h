#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "../host_ui/rasterizer.h"
#include "../host_ui/widgets.h"
#include "../debug/teleport.h"
#include "../debug/map_info.h"
#include "../debug/map_poi.h"
#include "../debug/save_anywhere.h"
#include "../debug/battle_menu.h"
#include "../debug/translations.h"
#include "../gpu/renderer.h"
#include "../settings/config.h"

namespace debug_menu
{
    struct OverlayState
    {
        bool visible = false;
        int activeTab = 0; // 0: Overview/常用, 1: Teleport/传送
        int selectedRow = 0;
        bool chinese = false;

        // Teleport edit state
        int selectedAxis = 0; // 0: X, 1: Y, 2: Z
        float editCoordinates[3] = {0.0f, 0.0f, 0.0f};
        float stepSize = 100.0f;

        // POI list
        int selectedPoi = 0;
        std::vector<debug_menu::MapPoi> pois;
        uint64_t poiRevision = ~uint64_t(0);

        std::wstring statusMessage;
    };

    inline OverlayState g_overlayState;

    inline void ToggleOverlay()
    {
        g_overlayState.visible = !g_overlayState.visible;
        g_overlayState.chinese = settings::GetConfig().debugLanguage == 1;

        if (g_overlayState.visible)
        {
            // Sync current position when opening
            auto snapshot = debug_menu::GetTeleportSnapshot();
            if (snapshot.available)
            {
                g_overlayState.editCoordinates[0] = snapshot.current.x;
                g_overlayState.editCoordinates[1] = snapshot.current.y;
                g_overlayState.editCoordinates[2] = snapshot.current.z;
            }
        }

        // Trigger pause/resume
        host_ui::SetGamePaused(g_overlayState.visible);
    }

    inline bool IsOverlayVisible()
    {
        return g_overlayState.visible;
    }

    inline void UpdateOverlaySnapshot()
    {
        if (!g_overlayState.visible) return;
        auto snapshot = debug_menu::GetTeleportSnapshot();
        if (snapshot.available && snapshot.poiRevision != g_overlayState.poiRevision)
        {
            g_overlayState.pois = snapshot.pois;
            g_overlayState.poiRevision = snapshot.poiRevision;
            if (g_overlayState.selectedPoi >= int(g_overlayState.pois.size()))
            {
                g_overlayState.selectedPoi = 0;
            }
        }
    }

    // Input actions
    enum class InputAction
    {
        None,
        Up,
        Down,
        Left,
        Right,
        Confirm,
        Cancel,
        PrevTab,
        NextTab
    };

    inline void HandleInput(InputAction action)
    {
        if (!g_overlayState.visible) return;

        switch (action)
        {
        case InputAction::Cancel:
            ToggleOverlay();
            return;
        case InputAction::PrevTab:
            g_overlayState.activeTab = (g_overlayState.activeTab + 1) % 2;
            g_overlayState.selectedRow = 0;
            return;
        case InputAction::NextTab:
            g_overlayState.activeTab = (g_overlayState.activeTab + 1) % 2;
            g_overlayState.selectedRow = 0;
            return;
        case InputAction::Up:
            if (g_overlayState.selectedRow > 0)
                g_overlayState.selectedRow--;
            return;
        case InputAction::Down:
            if (g_overlayState.activeTab == 0)
            {
                if (g_overlayState.selectedRow < 4)
                    g_overlayState.selectedRow++;
            }
            else
            {
                if (g_overlayState.selectedRow < 7)
                    g_overlayState.selectedRow++;
            }
            return;
        default:
            break;
        }

        if (g_overlayState.activeTab == 0)
        {
            // Overview:
            // 0: Language (Toggle En / Zh)
            // 1: Render Capture
            // 2: Save Anywhere
            // 3: Win Battle
            // 4: Cancel Battle Request
            if (action == InputAction::Confirm || action == InputAction::Left || action == InputAction::Right)
            {
                switch (g_overlayState.selectedRow)
                {
                case 0:
                    g_overlayState.chinese = !g_overlayState.chinese;
                    settings::SaveDebugLanguage(g_overlayState.chinese ? 1 : 0);
                    break;
                case 1:
                    gpu::renderer::RequestDebugCapture();
                    g_overlayState.statusMessage = L"Capture requested";
                    break;
                case 2:
                    debug_menu::SetSaveAnywhereEnabled(!debug_menu::SaveAnywhereEnabled());
                    break;
                case 3:
                    debug_menu::RequestVictory();
                    g_overlayState.statusMessage = L"Victory requested";
                    break;
                case 4:
                    debug_menu::CancelVictory();
                    g_overlayState.statusMessage = L"Victory cancelled";
                    break;
                }
            }
        }
        else if (g_overlayState.activeTab == 1)
        {
            // Teleport Tab:
            // 0: Remember Current Pos
            // 1: Restore Pos
            // 2: Fill Current Pos
            // 3: Select Axis (X/Y/Z) & Adjust with Left/Right
            // 4: Teleport to XYZ
            // 5: Step delta (+-100/+-500)
            // 6: POI Selection (Left/Right)
            // 7: Teleport to POI
            switch (g_overlayState.selectedRow)
            {
            case 0:
                if (action == InputAction::Confirm)
                {
                    debug_menu::RequestSavePosition();
                    g_overlayState.statusMessage = L"Position saved";
                }
                break;
            case 1:
                if (action == InputAction::Confirm)
                {
                    debug_menu::RequestRestorePosition();
                    g_overlayState.statusMessage = L"Position restored";
                }
                break;
            case 2:
                if (action == InputAction::Confirm)
                {
                    auto s = debug_menu::GetTeleportSnapshot();
                    if (s.available)
                    {
                        g_overlayState.editCoordinates[0] = s.current.x;
                        g_overlayState.editCoordinates[1] = s.current.y;
                        g_overlayState.editCoordinates[2] = s.current.z;
                        g_overlayState.statusMessage = L"Position filled";
                    }
                }
                break;
            case 3: // Axis adjustment
                if (action == InputAction::Confirm)
                {
                    g_overlayState.selectedAxis = (g_overlayState.selectedAxis + 1) % 3;
                }
                else if (action == InputAction::Left)
                {
                    g_overlayState.editCoordinates[g_overlayState.selectedAxis] -= g_overlayState.stepSize;
                }
                else if (action == InputAction::Right)
                {
                    g_overlayState.editCoordinates[g_overlayState.selectedAxis] += g_overlayState.stepSize;
                }
                break;
            case 4:
                if (action == InputAction::Confirm)
                {
                    debug_menu::Position p{
                        g_overlayState.editCoordinates[0],
                        g_overlayState.editCoordinates[1],
                        g_overlayState.editCoordinates[2]
                    };
                    debug_menu::RequestTeleport(p);
                    g_overlayState.statusMessage = L"Teleported to XYZ";
                }
                break;
            case 5:
                if (action == InputAction::Confirm || action == InputAction::Right)
                {
                    g_overlayState.stepSize = (g_overlayState.stepSize == 100.0f) ? 500.0f : (g_overlayState.stepSize == 500.0f ? 10.0f : 100.0f);
                }
                else if (action == InputAction::Left)
                {
                    g_overlayState.stepSize = (g_overlayState.stepSize == 100.0f) ? 10.0f : (g_overlayState.stepSize == 10.0f ? 500.0f : 100.0f);
                }
                break;
            case 6: // POI selection
                if (!g_overlayState.pois.empty())
                {
                    if (action == InputAction::Left)
                    {
                        g_overlayState.selectedPoi = (g_overlayState.selectedPoi + int(g_overlayState.pois.size()) - 1) % int(g_overlayState.pois.size());
                    }
                    else if (action == InputAction::Right)
                    {
                        g_overlayState.selectedPoi = (g_overlayState.selectedPoi + 1) % int(g_overlayState.pois.size());
                    }
                }
                break;
            case 7:
                if (action == InputAction::Confirm && !g_overlayState.pois.empty())
                {
                    if (size_t(g_overlayState.selectedPoi) < g_overlayState.pois.size())
                    {
                        debug_menu::RequestPoiTeleport(g_overlayState.pois[g_overlayState.selectedPoi].id);
                        g_overlayState.statusMessage = L"Teleported to POI";
                    }
                }
                break;
            }
        }
    }

    // Render debug overlay onto a 1280x720 pixel buffer (alpha composited over frozen frame)
    inline void RenderOverlay(host_ui::Rasterizer& r)
    {
        if (!g_overlayState.visible) return;

        bool zh = g_overlayState.chinese;
        auto tr = [zh](const wchar_t* key) { return debug_menu::translations::Text(key, zh); };

        // Dim background slightly to focus attention on debug overlay
        r.FillRect(0, 0, r.width, r.height, host_ui::MakeColor(140, 0, 0, 0));

        // Draw Main Dialog Panel (Centered: 760 x 540)
        int panelX = (1280 - 760) / 2;
        int panelY = (720 - 540) / 2;
        int panelW = 760;
        int panelH = 540;

        host_ui::DrawPanel(r, panelX, panelY, panelW, panelH, host_ui::MakeColor(235, 20, 22, 26), host_ui::MakeColor(255, 75, 85, 95));

        // Header Title
        std::wstring headerTitle = zh ? L"Lost Odyssey — 调试菜单 (F1 / LB+RB)" : L"Lost Odyssey — Debug Menu (F1 / LB+RB)";
        host_ui::DrawHeader(r, panelX, panelY, panelW, 36, headerTitle);

        // Tab buttons
        int tabY = panelY + 44;
        host_ui::DrawButton(r, panelX + 20, tabY, 140, 30, zh ? L"常用 / Overview" : L"Overview", g_overlayState.activeTab == 0, g_overlayState.activeTab == 0);
        host_ui::DrawButton(r, panelX + 170, tabY, 140, 30, zh ? L"传送 / Teleport" : L"Teleport", g_overlayState.activeTab == 1, g_overlayState.activeTab == 1);

        // Status message at bottom of panel
        int footerY = panelY + panelH - 32;
        r.DrawHLine(panelX, footerY - 6, panelW, host_ui::MakeColor(255, 60, 65, 75));
        std::wstring help = zh ? L"方向键/左摇杆: 导航   A/Enter: 确定   B/Esc: 关闭   LB/RB: 切页"
                               : L"D-Pad/Stick: Nav   A/Enter: Confirm   B/Esc: Close   LB/RB: Tab";
        r.DrawWString(panelX + 20, footerY, help, host_ui::MakeColor(255, 170, 175, 185));

        int contentX = panelX + 25;
        int contentY = tabY + 42;
        int rowH = 34;

        if (g_overlayState.activeTab == 0)
        {
            // Overview Content:
            // 0: Language
            std::wstring langText = std::wstring(zh ? L"界面语言: 简体中文" : L"Language: English");
            host_ui::DrawButton(r, contentX, contentY + 0 * rowH, 360, 28, langText, g_overlayState.selectedRow == 0);

            // 1: Render Capture
            std::wstring capText = zh ? L"截取渲染状态 (Capture)" : L"Capture render state";
            host_ui::DrawButton(r, contentX, contentY + 1 * rowH, 360, 28, capText, g_overlayState.selectedRow == 1);

            // Status of capture
            std::wstring capStat = gpu::renderer::DebugCaptureStatus();
            if (!capStat.empty())
                r.DrawWString(contentX + 375, contentY + 1 * rowH + 6, capStat, host_ui::MakeColor(255, 200, 200, 100));

            // 2: Save Anywhere
            bool saveOn = debug_menu::SaveAnywhereEnabled();
            std::wstring saveText = (zh ? L"随时存档: " : L"Save Anywhere: ") + std::wstring(saveOn ? (zh ? L"【已启用】" : L"[ON]") : (zh ? L"【已关闭】" : L"[OFF]"));
            host_ui::DrawButton(r, contentX, contentY + 2 * rowH, 360, 28, saveText, g_overlayState.selectedRow == 2);

            // Map info display
            auto mapInfo = debug_menu::GetMapInfo();
            std::wstring mapText = zh ? L"当前地图: " : L"Current Map: ";
            if (mapInfo.available)
            {
                mapText += mapInfo.name + L" (" + std::to_wstring(mapInfo.id) + L")";
            }
            else
            {
                mapText += zh ? L"未知" : L"Unknown";
            }
            r.DrawWString(contentX, contentY + 3 * rowH + 6, mapText, host_ui::MakeColor(255, 180, 210, 240));

            // 3: Win Battle
            std::wstring winText = zh ? L"当前战斗判胜 (Win Battle)" : L"Win Battle";
            host_ui::DrawButton(r, contentX, contentY + 4 * rowH, 240, 28, winText, g_overlayState.selectedRow == 3);

            // 4: Cancel Victory
            std::wstring cancelWinText = zh ? L"取消判胜请求" : L"Cancel Victory Request";
            host_ui::DrawButton(r, contentX + 255, contentY + 4 * rowH, 200, 28, cancelWinText, g_overlayState.selectedRow == 4);

            const wchar_t* bStat = debug_menu::Status();
            if (bStat && *bStat)
                r.DrawWString(contentX, contentY + 5 * rowH + 6, bStat, host_ui::MakeColor(255, 220, 180, 120));
        }
        else if (g_overlayState.activeTab == 1)
        {
            // Teleport Content
            // 0: Save Pos, 1: Restore Pos, 2: Fill Pos
            host_ui::DrawButton(r, contentX, contentY + 0 * rowH, 220, 28, zh ? L"记住当前位置" : L"Remember Position", g_overlayState.selectedRow == 0);
            host_ui::DrawButton(r, contentX + 235, contentY + 0 * rowH, 220, 28, zh ? L"返回记录位置" : L"Restore Position", g_overlayState.selectedRow == 1);
            host_ui::DrawButton(r, contentX + 470, contentY + 0 * rowH, 220, 28, zh ? L"填入当前坐标" : L"Fill Coordinates", g_overlayState.selectedRow == 2);

            // Current coordinates display
            auto posSnap = debug_menu::GetTeleportSnapshot();
            std::wstring curPosStr = zh ? L"角色实时坐标: " : L"Player Position: ";
            if (posSnap.available)
            {
                wchar_t buf[128];
                swprintf(buf, 128, L"X: %.2f  Y: %.2f  Z: %.2f", posSnap.current.x, posSnap.current.y, posSnap.current.z);
                curPosStr += buf;
            }
            else
            {
                curPosStr += zh ? L"不可用" : L"Unavailable";
            }
            r.DrawWString(contentX, contentY + 1 * rowH + 6, curPosStr, host_ui::MakeColor(255, 180, 220, 180));

            // 3: Editable XYZ
            wchar_t coordBuf[128];
            const wchar_t* axisNames[] = {L"X", L"Y", L"Z"};
            swprintf(coordBuf, 128, L"目标坐标 [%ls]: X: %.1f,  Y: %.1f,  Z: %.1f  (◄/► 微调)",
                     axisNames[g_overlayState.selectedAxis],
                     g_overlayState.editCoordinates[0],
                     g_overlayState.editCoordinates[1],
                     g_overlayState.editCoordinates[2]);
            host_ui::DrawButton(r, contentX, contentY + 2 * rowH, 500, 28, coordBuf, g_overlayState.selectedRow == 3);

            // 4: Teleport to XYZ
            host_ui::DrawButton(r, contentX + 515, contentY + 2 * rowH, 180, 28, zh ? L"传送到目标坐标" : L"Teleport to XYZ", g_overlayState.selectedRow == 4);

            // 5: Step Size
            wchar_t stepBuf[64];
            swprintf(stepBuf, 64, zh ? L"微调步长: ±%.0f (◄/► 切换)" : L"Step Size: ±%.0f (◄/► Switch)", g_overlayState.stepSize);
            host_ui::DrawButton(r, contentX, contentY + 3 * rowH, 300, 28, stepBuf, g_overlayState.selectedRow == 5);

            // 6 & 7: POI
            r.DrawWString(contentX, contentY + 4 * rowH + 4, zh ? L"地图兴趣点 (POI):" : L"Points of Interest (POI):", host_ui::MakeColor(255, 230, 230, 230));

            std::wstring poiName = zh ? L"无可用 POI" : L"No POIs available";
            if (!g_overlayState.pois.empty() && size_t(g_overlayState.selectedPoi) < g_overlayState.pois.size())
            {
                const auto& p = g_overlayState.pois[g_overlayState.selectedPoi];
                wchar_t pBuf[128];
                swprintf(pBuf, 128, L"[%d/%d] %ls (%.0f, %.0f, %.0f)",
                         g_overlayState.selectedPoi + 1, int(g_overlayState.pois.size()),
                         p.label.c_str(), p.position.x, p.position.y, p.position.z);
                poiName = pBuf;
            }
            host_ui::DrawButton(r, contentX, contentY + 5 * rowH, 500, 28, poiName, g_overlayState.selectedRow == 6);
            host_ui::DrawButton(r, contentX + 515, contentY + 5 * rowH, 180, 28, zh ? L"传送到此 POI" : L"Teleport to POI", g_overlayState.selectedRow == 7);
        }

        if (!g_overlayState.statusMessage.empty())
        {
            r.DrawWString(contentX, footerY - 24, g_overlayState.statusMessage, host_ui::MakeColor(255, 120, 220, 150));
        }
    }
}
