#pragma once
#include "../include/IModule.h"
#include <string>
#include <cmath>

class FollowerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Follower"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastFollowTick = 0; followPaused = false; }
    void Stop() override  { lastFollowTick = 0; followPaused = false; targetAddr = 0; }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;
        if (targetAddr <= 0x1000) return;

        DWORD now = ctx.tickCount;
        if (now - lastFollowTick < (DWORD)cooldownMs) return;

        // Foreground safety (same as backup)
        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // Find target in player list
        bool found = false;
        float tx = 0, ty = 0;
        for (auto& p : ctx.players) {
            if (p.objAddr == targetAddr) {
                tx = p.x; ty = p.y;
                found = true;
                break;
            }
        }

        if (!found) {
            for (auto& p : ctx.players) {
                if (p.name == targetName) {
                    targetAddr = p.objAddr;
                    tx = p.x; ty = p.y;
                    found = true;
                    followPaused = false;
                    break;
                }
            }
        }

        if (!found) {
            if (!followPaused) followPaused = true;
            return;
        }

        float dx = tx - ctx.selfX;
        float dy = ty - ctx.selfY;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist < desiredDistance) return;

        // Write cursor position to game memory (same as backup MoveToTile)
        WORD tileX = (WORD)((int)(tx / 24.0f));
        WORD tileY = (WORD)((int)(ty / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DebugLog("[FOLLOW] Following '%S' dist=%.1f -> tile(%d,%d)", targetName.c_str(), dist, tileX, tileY);

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
                    DWORD walkFlag = 0x10;
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(cur + 0x7C), &walkFlag, 4, NULL);
                }
            }
        }

        // Send Enter locally (backup approach - keybd_event from controller process)
        // The backup does AttachThreadInput + SetForegroundWindow + local keybd_event
        DWORD fgTid = GetWindowThreadProcessId(gw, NULL);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(gw);
        AttachThreadInput(myTid, fgTid, FALSE);
        keybd_event(VK_RETURN, 0, 0, 0);
        Sleep(30);
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);

        lastFollowTick = now;
    }

    // Config
    bool  enabled = false;
    DWORD targetAddr = 0;
    std::wstring targetName;
    float desiredDistance = 3.0f;
    float maxDistance = 30.0f;
    int   cooldownMs = 800;
    bool  autoReapproach = true;
    bool  followPaused = false;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Follower", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Follower", L"DesiredDistance", L"3", buf, 256, path);
        desiredDistance = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Follower", L"MaxDistance", L"30", buf, 256, path);
        maxDistance = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Follower", L"Cooldown", L"800", buf, 256, path);
        cooldownMs = _wtoi(buf);
        GetPrivateProfileStringW(L"Follower", L"AutoReapproach", L"1", buf, 256, path);
        autoReapproach = (buf[0] == L'1');
        wchar_t nameBuf[64] = {};
        GetPrivateProfileStringW(L"Follower", L"TargetName", L"", nameBuf, 64, path);
        targetName = nameBuf;
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Follower", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", desiredDistance);
        WritePrivateProfileStringW(L"Follower", L"DesiredDistance", buf, path);
        swprintf_s(buf, L"%.0f", maxDistance);
        WritePrivateProfileStringW(L"Follower", L"MaxDistance", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Follower", L"Cooldown", buf, path);
        WritePrivateProfileStringW(L"Follower", L"AutoReapproach", autoReapproach ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Follower", L"TargetName", targetName.c_str(), path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9401, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Target Name:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtTarget = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+85, y, 160, 22, parent, (HMENU)9402, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Desired Dist:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtDesiredDist = CreateWindowExW(0, L"edit", L"3", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9403, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Max Dist:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMaxDist = CreateWindowExW(0, L"edit", L"30", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9404, GetModuleHandle(NULL), NULL);

        y += 28;
        hChkReapproach = CreateWindowExW(0, L"button", L"Auto Reapproach",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9405, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtTarget) SetWindowTextW(hEdtTarget, targetName.c_str());
        if (hEdtDesiredDist) { wchar_t b[32]; swprintf_s(b, L"%.1f", desiredDistance); SetWindowTextW(hEdtDesiredDist, b); }
        if (hEdtMaxDist) { wchar_t b[32]; swprintf_s(b, L"%.0f", maxDistance); SetWindowTextW(hEdtMaxDist, b); }
        if (hChkReapproach) SendMessage(hChkReapproach, BM_SETCHECK, autoReapproach ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void OnCommand(int id, int code) override {
        if (id == 9401 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9402 && code == EN_CHANGE) {
            wchar_t b[64]; GetWindowTextW(hEdtTarget, b, 64); targetName = b;
        }
        if (id == 9403 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtDesiredDist, b, 32); desiredDistance = (float)_wtof(b);
        }
        if (id == 9404 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMaxDist, b, 32); maxDistance = (float)_wtof(b);
        }
        if (id == 9405 && code == BN_CLICKED) {
            autoReapproach = (SendMessage(hChkReapproach, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtTarget = NULL;
    HWND hEdtDesiredDist = NULL, hEdtMaxDist = NULL, hChkReapproach = NULL;
    DWORD lastFollowTick = 0;
};
