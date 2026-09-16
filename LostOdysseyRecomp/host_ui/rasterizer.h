#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "host_ui.h"
#include "unifont.h"

namespace host_ui
{
    // Color helper: ARGB uint32 representation (0xAARRGGBB)
    inline constexpr uint32_t MakeColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }

    inline constexpr uint32_t ColorBlend(uint32_t bg, uint32_t fg)
    {
        uint32_t fa = (fg >> 24) & 0xFF;
        if (fa == 0) return bg;
        if (fa == 255) return fg;

        uint32_t ba = (bg >> 24) & 0xFF;
        uint32_t inv_fa = 255 - fa;

        uint32_t r = (((fg >> 16) & 0xFF) * fa + ((bg >> 16) & 0xFF) * inv_fa) / 255;
        uint32_t g = (((fg >> 8) & 0xFF) * fa + ((bg >> 8) & 0xFF) * inv_fa) / 255;
        uint32_t b = ((fg & 0xFF) * fa + (bg & 0xFF) * inv_fa) / 255;
        uint32_t a = fa + (ba * inv_fa) / 255;

        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    struct Rasterizer
    {
        uint32_t width = kOverlayWidth;
        uint32_t height = kOverlayHeight;
        uint32_t* pixels = nullptr;

        Rasterizer(PixelBuffer& buffer)
            : width(buffer.width), height(buffer.height), pixels(buffer.pixels.data()) {}

        Rasterizer(uint32_t* p, uint32_t w, uint32_t h)
            : width(w), height(h), pixels(p) {}

        void PutPixel(int x, int y, uint32_t color)
        {
            if (x < 0 || x >= int(width) || y < 0 || y >= int(height)) return;
            size_t idx = size_t(y) * width + x;
            pixels[idx] = ColorBlend(pixels[idx], color);
        }

        void FillRect(int x, int y, int w, int h, uint32_t color)
        {
            int x0 = std::max(0, x);
            int y0 = std::max(0, y);
            int x1 = std::min(int(width), x + w);
            int y1 = std::min(int(height), y + h);
            if (x0 >= x1 || y0 >= y1) return;

            uint32_t alpha = (color >> 24) & 0xFF;
            if (alpha == 255)
            {
                for (int py = y0; py < y1; ++py)
                {
                    uint32_t* row = &pixels[size_t(py) * width + x0];
                    std::fill_n(row, x1 - x0, color);
                }
            }
            else
            {
                for (int py = y0; py < y1; ++py)
                {
                    for (int px = x0; px < x1; ++px)
                    {
                        size_t idx = size_t(py) * width + px;
                        pixels[idx] = ColorBlend(pixels[idx], color);
                    }
                }
            }
        }

        void DrawHLine(int x, int y, int w, uint32_t color, int thickness = 1)
        {
            FillRect(x, y - (thickness / 2), w, thickness, color);
        }

        void DrawVLine(int x, int y, int h, uint32_t color, int thickness = 1)
        {
            FillRect(x - (thickness / 2), y, thickness, h, color);
        }

        void DrawRect(int x, int y, int w, int h, uint32_t color, int thickness = 1)
        {
            DrawHLine(x, y, w, color, thickness);
            DrawHLine(x, y + h - thickness, w, color, thickness);
            DrawVLine(x, y, h, color, thickness);
            DrawVLine(x + w - thickness, y, h, color, thickness);
        }

        // Draw unifont bitmap glyph
        void DrawGlyphBitmap(int x, int y, const host_ui::font::Glyph& glyph, uint32_t color, float scale = 1.0f)
        {
            if (!glyph.bitmap || glyph.width <= 0) return;

            for (int r = 0; r < 16; ++r)
            {
                uint16_t rowBits = 0;
                if (glyph.width == 8)
                {
                    rowBits = uint16_t(glyph.bitmap[r]) << 8;
                }
                else
                {
                    rowBits = (uint16_t(glyph.bitmap[r * 2]) << 8) | glyph.bitmap[r * 2 + 1];
                }

                for (int c = 0; c < glyph.width; ++c)
                {
                    if (rowBits & (0x8000 >> c))
                    {
                        int px0 = x + int(std::floor(c * scale));
                        int py0 = y + int(std::floor(r * scale));
                        int pw = std::max(1, int(std::ceil(scale)));
                        int ph = std::max(1, int(std::ceil(scale)));
                        FillRect(px0, py0, pw, ph, color);
                    }
                }
            }
        }

        int DrawChar(int x, int y, uint32_t codepoint, uint32_t color, float scale = 1.0f)
        {
            auto glyph = host_ui::font::GetGlyph(codepoint);
            if (!glyph.bitmap) return int(8 * scale);
            DrawGlyphBitmap(x, y, glyph, color, scale);
            return int((glyph.width + 1) * scale);
        }

        int DrawString(int x, int y, const std::string& utf8Text, uint32_t color, float scale = 1.0f)
        {
            int cx = x;
            size_t offset = 0;
            while (offset < utf8Text.size())
            {
                uint32_t cp = host_ui::font::DecodeUtf8(utf8Text, offset);
                if (cp == 0) break;
                if (cp == '\n')
                {
                    cx = x;
                    y += int(18 * scale);
                    continue;
                }
                cx += DrawChar(cx, y, cp, color, scale);
            }
            return cx - x;
        }

        // Wide-char UTF-16/32 conversion helper
        int DrawWString(int x, int y, const std::wstring& wtext, uint32_t color, float scale = 1.0f)
        {
            int cx = x;
            for (wchar_t wc : wtext)
            {
                if (wc == L'\n')
                {
                    cx = x;
                    y += int(18 * scale);
                    continue;
                }
                cx += DrawChar(cx, y, uint32_t(wc), color, scale);
            }
            return cx - x;
        }
    };
}
