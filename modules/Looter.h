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

    void Start() override { lastLootTick = 0; lootingState = 0; }
    void Stop() override  { lastLootTick = 0; lootingState = 0; }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastLootTick < (DWORD)cooldownMs) return;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // Find corpse to loot: first try tree corpses, then pending corpse from attacker
        std::wstring corpseName;
        float corpseX = 0, corpseY = 0;
        bool hasCorpse = false;

        if (!ctx.corpses.empty()) {
            // Tree-based corpse (some servers keep dead mobs in tree)
            auto& c = ctx.corpses[0];
            corpseName = c.name;
            corpseX = c.x;
            corpseY = c.y;
            hasCorpse = true;
        } else if (ctx.pendingCorpse && ctx.pendingCorpse->valid) {
            // Pending corpse (saved by attacker when mob died)
            corpseName = ctx.pendingCorpse->name;
            corpseX = ctx.pendingCorpse->x;
            corpseY = ctx.pendingCorpse->y;
            hasCorpse = true;

            // Invalidate if too old (15 seconds)
            if (now - ctx.pendingCorpse->time > 15000) {
                ctx.pendingCorpse->valid = false;
                hasCorpse = false;
                DebugLog("[LOOT] Pending corpse expired");
            }
        }

        if (!hasCorpse) { lootingState = 0; return; }

        float dx = corpseX - ctx.selfX;
        float dy = corpseY - ctx.selfY;
        float dist = sqrtf(dx*dx + dy*dy);

        // Get cursor pointer for writing
        DWORD curPtr = GetCursorPtr(ctx.hProcess);

        if (dist > radius) {
            // Walk toward corpse — call every tick, character walks incrementally
            WalkToPosition(ctx, gw, corpseX, corpseY);
        } else {
            // Close enough, loot directly
            LootCorpse(ctx, gw, curPtr, corpseX, corpseY);
            lastLootTick = now;
            lootCount++;
            DebugLog("[LOOT] Looted '%S' #%d", corpseName.c_str(), lootCount);
            // Invalidate pending corpse after looting
            if (ctx.pendingCorpse) ctx.pendingCorpse->valid = false;
        }
    }

    // Config
    bool  enabled = false;
    float radius = 9999.0f;
    int   cooldownMs = 500;
    int   lootCount = 0;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Looter", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Looter", L"Radius", L"20", buf, 256, path);
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
        CreateWindowExW(0, L"static", L"Loot Radius:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtRadius = CreateWindowExW(0, L"edit", L"10", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9302, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"1200", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9303, GetModuleHandle(NULL), NULL);

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
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtRadius = NULL, hEdtCooldown = NULL;
    DWORD lastLootTick = 0;
    DWORD lootingState = 0;
    DWORD lootingStepTick = 0;

    static DWORD GetCursorPtr(HANDLE hProc) {
        DWORD gmPtr = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)0x00D387AC, &gmPtr, 4, &r);
        if (r != 4 || gmPtr <= 0x1000) return 0;
        DWORD gm = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gmPtr + 0x14), &gm, 4, &r);
        if (r != 4 || gm <= 0x1000) return 0;
        DWORD cur = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gm + 0x123C), &cur, 4, &r);
        return (r == 4) ? cur : 0;
    }

    // Walk toward position (same as follower: cursor memory + walk flag + local Enter)
    void WalkToPosition(const GameContext& ctx, HWND gw, float gameX, float gameY) {
        extern void DebugLog(const char* fmt, ...);

        WORD tileX = (WORD)((int)(gameX / 24.0f));
        WORD tileY = (WORD)((int)(gameY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (!curPtr) return;

        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
        DWORD walkFlag = 0x10;
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x7C), &walkFlag, 4, NULL);

        DebugLog("[LOOT] Walk -> tile(%d,%d) raw(%d,%d)", tileX, tileY, rawX, rawY);

        // Local Enter (same as follower)
        SendLocalEnter(gw);
    }

    // Loot corpse when in range (cursor on corpse + HandleMoveOrAction + Enter)
    void LootCorpse(const GameContext& ctx, HWND gw, DWORD curPtr, float corpseX, float corpseY) {
        extern void DebugLog(const char* fmt, ...);
        if (!curPtr) return;

        WORD tileX = (WORD)((int)(corpseX / 24.0f));
        WORD tileY = (WORD)((int)(corpseY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);

        DebugLog("[LOOT] LootCorpse -> tile(%d,%d)", tileX, tileY);

        // Call HandleMoveOrAction to make game process cursor position (detect corpse)
        if (ctx.remoteHandleMoveOrAction && ctx.playerAddr > 0x1000) {
            ctx.remoteHandleMoveOrAction(ctx.playerAddr);
        }

        Sleep(200);
        SendLocalEnter(gw);
    }

    // Send Enter locally with foreground focus
    static void SendLocalEnter(HWND gw) {
        if (!gw) return;
        DWORD fgTid = GetWindowThreadProcessId(gw, NULL);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(gw);
        AttachThreadInput(myTid, fgTid, FALSE);
        keybd_event(VK_RETURN, 0, 0, 0);
        Sleep(30);
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    }
};
