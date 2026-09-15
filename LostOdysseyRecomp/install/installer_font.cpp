#include "installer_font.h"
#include <algorithm>
#include <cmath>

namespace install::ui
{
namespace
{
#include "unifont_packed.inl"

// Binary search in sorted g_unifontCodepoints table
int FindGlyphIndex(uint32_t codepoint)
{
    if (codepoint > 0xFFFF) return -1;
    uint16_t cp16 = static_cast<uint16_t>(codepoint);
    int low = 0;
    int high = static_cast<int>(UNIFONT_GLYPH_COUNT) - 1;

    while (low <= high)
    {
        int mid = low + (high - low) / 2;
        uint16_t val = g_unifontCodepoints[mid];
        if (val == cp16) return mid;
        if (val < cp16) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}
}

GlyphInfo GetGlyph(uint32_t codepoint)
{
    int idx = FindGlyphIndex(codepoint);
    if (idx < 0)
    {
        // Try fallback '?'
        idx = FindGlyphIndex('?');
        if (idx < 0) return {};
    }

    uint32_t entry = g_unifontOffsets[idx];
    bool isWide = (entry & 0x80000000u) != 0;
    uint32_t offset = entry & 0x7FFFFFFFu;

    GlyphInfo info;
    info.bitmap = &g_unifontData[offset];
    info.width = isWide ? 16 : 8;
    info.height = 16;
    return info;
}

uint32_t DecodeUtf8(std::string_view text, size_t& offset)
{
    if (offset >= text.size()) return 0;
    unsigned char b0 = static_cast<unsigned char>(text[offset]);

    if (b0 < 0x80)
    {
        offset += 1;
        return b0;
    }
    else if ((b0 & 0xE0) == 0xC0)
    {
        if (offset + 1 >= text.size()) { offset += 1; return '?'; }
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        offset += 2;
        return ((b0 & 0x1F) << 6) | (b1 & 0x3F);
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        if (offset + 2 >= text.size()) { offset += 1; return '?'; }
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        offset += 3;
        return ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        if (offset + 3 >= text.size()) { offset += 1; return '?'; }
        unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
        offset += 4;
        return ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    }
    offset += 1;
    return '?';
}

int DrawGlyph(SDL_Renderer* renderer, int x, int y, uint32_t codepoint,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale)
{
    GlyphInfo glyph = GetGlyph(codepoint);
    if (!glyph.bitmap) return static_cast<int>(8 * scale);

    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    int scaledPixel = static_cast<int>(std::ceil(scale));
    int advWidth = static_cast<int>(glyph.width * scale);

    if (glyph.width == 8)
    {
        // 8x16 glyph: 16 bytes, each byte is 1 row of 8 bits (0x80 >> col)
        for (int row = 0; row < 16; ++row)
        {
            uint8_t bits = glyph.bitmap[row];
            if (bits == 0) continue;
            int py = y + static_cast<int>(row * scale);
            for (int col = 0; col < 8; ++col)
            {
                if (bits & (0x80 >> col))
                {
                    int px = x + static_cast<int>(col * scale);
                    if (scaledPixel <= 1)
                    {
                        SDL_RenderDrawPoint(renderer, px, py);
                    }
                    else
                    {
                        SDL_Rect rc{ px, py, scaledPixel, scaledPixel };
                        SDL_RenderFillRect(renderer, &rc);
                    }
                }
            }
        }
    }
    else
    {
        // 16x16 glyph: 32 bytes, each row is 2 bytes (16 bits)
        for (int row = 0; row < 16; ++row)
        {
            uint16_t bits = (static_cast<uint16_t>(glyph.bitmap[row * 2]) << 8) |
                             static_cast<uint16_t>(glyph.bitmap[row * 2 + 1]);
            if (bits == 0) continue;
            int py = y + static_cast<int>(row * scale);
            for (int col = 0; col < 16; ++col)
            {
                if (bits & (0x8000 >> col))
                {
                    int px = x + static_cast<int>(col * scale);
                    if (scaledPixel <= 1)
                    {
                        SDL_RenderDrawPoint(renderer, px, py);
                    }
                    else
                    {
                        SDL_Rect rc{ px, py, scaledPixel, scaledPixel };
                        SDL_RenderFillRect(renderer, &rc);
                    }
                }
            }
        }
    }

    return advWidth;
}

void DrawString(SDL_Renderer* renderer, int x, int y, std::string_view text,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale)
{
    int curX = x;
    int curY = y;
    int lineHeight = TextLineHeight(scale);

    size_t offset = 0;
    while (offset < text.size())
    {
        uint32_t cp = DecodeUtf8(text, offset);
        if (cp == '\n')
        {
            curX = x;
            curY += lineHeight;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == '\t')
        {
            curX += static_cast<int>(32 * scale);
            continue;
        }

        int adv = DrawGlyph(renderer, curX, curY, cp, r, g, b, a, scale);
        curX += adv;
    }
}

int MeasureTextWidth(std::string_view text, float scale)
{
    int maxWidth = 0;
    int curWidth = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        uint32_t cp = DecodeUtf8(text, offset);
        if (cp == '\n')
        {
            maxWidth = std::max(maxWidth, curWidth);
            curWidth = 0;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == '\t')
        {
            curWidth += static_cast<int>(32 * scale);
            continue;
        }

        GlyphInfo g = GetGlyph(cp);
        curWidth += static_cast<int>(g.width * scale);
    }
    return std::max(maxWidth, curWidth);
}

int TextLineHeight(float scale)
{
    return static_cast<int>(std::round(18.0f * scale));
}

}
