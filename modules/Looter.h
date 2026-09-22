#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <cmath>

class LooterModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Looter"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastLootTick = 0; hasPendingCorpse = false; }
    void Stop() override  { lastLootTick = 0; hasPendingCorpse = false; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastLootTick < (DWORD)cooldownMs) return;

        // Check for corpses to loot
        if (ctx.corpses.empty()) { hasPendingCorpse = false; return; }

        auto& corpse = ctx.corpses[0];
        float dx = corpse.x - ctx.selfX;
        float dy = corpse.y - ctx.selfY;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist > radius) {
            // Walk toward corpse (use cursor + Enter approach)
            // Write game coords to cursor, then press Enter
            HWND gw = ctx.gameWindow;
            if (!gw) return;

            // Convert to cursor tile coords
            WORD tileX = (WORD)((int)(corpse.x / 24.0f));
            WORD tileY = (WORD)((int)(corpse.y / 24.0f));
            if (tileX > 27) tileX = 27;
            if (tileY > 27) tileY = 27;

            // Get cursor address from game memory
            DWORD gmPtr = 0; SIZE_T r = 0;
            ReadProcessMemory(ctx.hProcess, (LPCVOID)0x00D387AC, &gmPtr, 4, &r);
            if (gmPtr > 0x1000) {
                DWORD gm = 0;
                ReadProcessMemory(ctx.hProcess, (LPCVOID)(gmPtr + 0x14), &gm, 4, &r);
                if (gm > 0x1000) {
                    DWORD cur = 0;
                    ReadProcessMemory(ctx.hProcess, (LPCVOID)(gm + 0x123C), &cur, 4, &r);
                    if (cur > 0x1000) {
                        WORD rawTileX = tileX;
                        WORD rawTileY = tileY;
                        WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x08), &rawTileX, 2, NULL);
                        WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x0A), &rawTileY, 2, NULL);
                        int rawX = (int)tileX * 0x180000;
                        int rawY = (int)tileY * 0x180000;
                        WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x10), &rawX, 4, NULL);
                        WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x14), &rawY, 4, NULL);
                    }
                }
            }

            // Send Enter to walk
            if (gw && IsWindow(gw)) {
                UINT scan = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
                LPARAM keyDown = 1 | ((LPARAM)scan << 16);
                LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);
                PostMessageW(gw, WM_KEYDOWN, VK_RETURN, keyDown);
                PostMessageW(gw, WM_KEYUP, VK_RETURN, keyUp);
            }
            lastLootTick = now;
            return;
        }

        // Close enough - write cursor to corpse tile and press Enter
        HWND gw = ctx.gameWindow;
        if (!gw) return;

        WORD tileX = (WORD)((int)(corpse.x / 24.0f));
        WORD tileY = (WORD)((int)(corpse.y / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DWORD gmPtr = 0; SIZE_T r = 0;
        ReadProcessMemory(ctx.hProcess, (LPCVOID)0x00D387AC, &gmPtr, 4, &r);
        if (gmPtr > 0x1000) {
            DWORD gm = 0;
            ReadProcessMemory(ctx.hProcess, (LPCVOID)(gmPtr + 0x14), &gm, 4, &r);
            if (gm > 0x1000) {
                DWORD cur = 0;
                ReadProcessMemory(ctx.hProcess, (LPCVOID)(gm + 0x123C), &cur, 4, &r);
                if (cur > 0x1000) {
                    int rawX = (int)tileX * 0x180000;
                    int rawY = (int)tileY * 0x180000;
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x08), &tileX, 2, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x0A), &tileY, 2, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x10), &rawX, 4, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x14), &rawY, 4, NULL);
                }
            }
        }

        Sleep(100);

        // Press Enter to interact/loot
        if (gw && IsWindow(gw)) {
            UINT scan = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
            LPARAM keyDown = 1 | ((LPARAM)scan << 16);
            LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);
            PostMessageW(gw, WM_KEYDOWN, VK_RETURN, keyDown);
            PostMessageW(gw, WM_KEYUP, VK_RETURN, keyUp);
        }

        lastLootTick = now;
        lootCount++;
    }

    // Config
    bool  enabled = false;
    float radius = 10.0f;
    int   cooldownMs = 1200;
    int   lootCount = 0;
    std::vector<std::wstring> itemWhitelist;
    std::vector<std::wstring> itemBlacklist;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Looter", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Looter", L"Radius", L"10", buf, 256, path);
        radius = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Looter", L"Cooldown", L"1200", buf, 256, path);
        cooldownMs = _wtoi(buf);
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Looter", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", radius);
        WritePrivateProfileStringW(L"Looter", L"Radius", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Looter", L"Cooldown", buf, path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9301, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Loot Radius:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtRadius = CreateWindowExW(0, L"edit", L"10", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9302, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"1200", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9303, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Item Whitelist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtWhitelist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9304, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Item Blacklist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtBlacklist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9305, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtRadius) { wchar_t b[32]; swprintf_s(b, L"%.0f", radius); SetWindowTextW(hEdtRadius, b); }
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9301 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9302 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtRadius, b, 32); radius = (float)_wtof(b);
        }
        if (id == 9303 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
        if (id == 9304 && code == EN_CHANGE) {
            wchar_t b[256]; GetWindowTextW(hEdtWhitelist, b, 256);
            itemWhitelist.clear();
            wchar_t* ctx; wchar_t* tok = wcstok_s(b, L",", &ctx);
            while (tok) { while (*tok == L' ') tok++; itemWhitelist.push_back(tok); tok = wcstok_s(NULL, L",", &ctx); }
        }
        if (id == 9305 && code == EN_CHANGE) {
            wchar_t b[256]; GetWindowTextW(hEdtBlacklist, b, 256);
            itemBlacklist.clear();
            wchar_t* ctx; wchar_t* tok = wcstok_s(b, L",", &ctx);
            while (tok) { while (*tok == L' ') tok++; itemBlacklist.push_back(tok); tok = wcstok_s(NULL, L",", &ctx); }
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtRadius = NULL, hEdtCooldown = NULL;
    HWND hEdtWhitelist = NULL, hEdtBlacklist = NULL;
    DWORD lastLootTick = 0;
    bool hasPendingCorpse = false;
};
