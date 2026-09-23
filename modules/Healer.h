#pragma once
#include "../include/IModule.h"
#include <string>
#include <cmath>

class HealerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Healer"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastHealTick = 0; }
    void Stop() override  { lastHealTick = 0; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;
        if (targetAddr <= 0x1000) return;

        DWORD now = ctx.tickCount;
        if (now - lastHealTick < (DWORD)cooldownMs) return;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // If minHpFilter is ON, check target HP
        if (minHpFilter) {
            bool needsHeal = false;
            for (auto& p : ctx.players) {
                if (p.objAddr == targetAddr) {
                    float hpPct = p.maxHp > 0 ? (float)p.hp / p.maxHp * 100.0f : 100.0f;
                    if (hpPct <= minHpPct) needsHeal = true;
                    break;
                }
            }
            if (!needsHeal) return;
        }

        if (healKeyBind == 0) return;

        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = (WORD)healKeyBind;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = (WORD)healKeyBind;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        lastHealTick = now;
    }

    // Config
    bool  enabled = false;
    DWORD targetAddr = 0;
    std::wstring targetName;
    bool  minHpFilter = false;
    float minHpPct = 60.0f;
    int   cooldownMs = 2000;
    int   healKeyBind = 0x31;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Healer", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Healer", L"MinHpFilter", L"0", buf, 256, path);
        minHpFilter = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Healer", L"MinHpPct", L"60", buf, 256, path);
        minHpPct = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Healer", L"Cooldown", L"2000", buf, 256, path);
        cooldownMs = _wtoi(buf);
        GetPrivateProfileStringW(L"Healer", L"HealKey", L"1", buf, 256, path);
        healKeyBind = buf[0] ? (buf[0] >= L'0' && buf[0] <= L'9' ? buf[0] : 0x31) : 0x31;
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Healer", L"Enabled", enabled ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Healer", L"MinHpFilter", minHpFilter ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", minHpPct);
        WritePrivateProfileStringW(L"Healer", L"MinHpPct", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Healer", L"Cooldown", buf, path);
        swprintf_s(buf, L"%c", healKeyBind);
        WritePrivateProfileStringW(L"Healer", L"HealKey", buf, path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9101, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Min HP %:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMinHp = CreateWindowExW(0, L"edit", L"60", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+75, y, 50, 22, parent, (HMENU)9102, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"2000", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9103, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Heal Key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtHealKey = CreateWindowExW(0, L"edit", L"1", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+75, y, 40, 22, parent, (HMENU)9104, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtMinHp) { wchar_t b[32]; swprintf_s(b, L"%.0f", minHpPct); SetWindowTextW(hEdtMinHp, b); }
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
        if (hEdtHealKey) { wchar_t b[4] = {(wchar_t)healKeyBind, 0}; SetWindowTextW(hEdtHealKey, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9101 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9102 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMinHp, b, 32); minHpPct = (float)_wtof(b);
        }
        if (id == 9103 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
        if (id == 9104 && code == EN_CHANGE) {
            wchar_t b[4]; GetWindowTextW(hEdtHealKey, b, 4); healKeyBind = b[0] ? b[0] : 0x31;
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtMinHp = NULL, hEdtCooldown = NULL, hEdtHealKey = NULL;
    DWORD lastHealTick = 0;
};