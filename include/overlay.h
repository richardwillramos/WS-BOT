#pragma once
#include <Windows.h>
#include <cstdio>

// Minimal ImGui for overlay (no external dependency)
// We'll use GDI for a simple text overlay as fallback

namespace Overlay {

    HWND    g_GameWindow = nullptr;
    HDC     g_Hdc = nullptr;
    HFONT   g_Font = nullptr;
    bool    g_Initialized = false;

    // Colours
    constexpr COLORREF COL_WHITE   = RGB(255, 255, 255);
    constexpr COLORREF COL_GREEN   = RGB(0, 255, 0);
    constexpr COLORREF COL_RED     = RGB(255, 0, 0);
    constexpr COLORREF COL_YELLOW  = RGB(255, 255, 0);
    constexpr COLORREF COL_CYAN    = RGB(0, 255, 255);
    constexpr COLORREF COL_BG      = RGB(10, 10, 10);
    constexpr COLORREF COL_DIM     = RGB(150, 150, 150);

    inline void Init(HWND hwnd) {
        if (g_Initialized) return;
        g_GameWindow = hwnd;
        g_Hdc = GetDC(hwnd);
        g_Font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        g_Initialized = true;
    }

    inline void Shutdown() {
        if (!g_Initialized) return;
        if (g_Font) DeleteObject(g_Font);
        if (g_Hdc && g_GameWindow) ReleaseDC(g_GameWindow, g_Hdc);
        g_Initialized = false;
    }

    struct TextLine {
        const char* text;
        COLORREF color;
        int x, y;
    };

    inline void DrawOverlay(const TextLine* lines, int count) {
        if (!g_Initialized || !g_Hdc) return;

        HDC hdc = g_Hdc;
        HGDIOBJ oldFont = SelectObject(hdc, g_Font);
        SetBkMode(hdc, TRANSPARENT);

        // Draw semi-transparent background
        HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
        RECT bgRect = { 5, 5, 400, (LONG)(5 + count * 16 + 10) };
        FillRect(hdc, &bgRect, bgBrush);
        DeleteObject(bgBrush);

        for (int i = 0; i < count; i++) {
            SetTextColor(hdc, lines[i].color);
            TextOutA(hdc, lines[i].x, lines[i].y, lines[i].text, (int)strlen(lines[i].text));
        }

        SelectObject(hdc, oldFont);
    }

    inline void Clear() {
        if (!g_Initialized || !g_Hdc) return;
        // We don't clear - the game renders over our DC each frame
    }

} // namespace Overlay
