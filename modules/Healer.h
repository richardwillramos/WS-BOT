#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>

class HealerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Healer"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastHealTick = 0; }
    void Stop() override  { lastHealTick = 0; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastHealTick < (DWORD)cooldownMs) return;

        float hpPct = ctx.selfMaxHp > 0 ? (float)ctx.selfHp / ctx.selfMaxHp * 100.0f : 100.0f;
        float mpPct = ctx.selfMaxMana > 0 ? (float)ctx.selfMana / ctx.selfMaxMana * 100.0f : 100.0f;

        bool needHeal = (hpPct <= minHpPct);
        bool needMana = (mpPct <= minManaPct && mpPct > 0);

        if (!needHeal && !needMana) return;

        // Cast heal skill
        int bind = needHeal ? healKeyBind : manaKeyBind;
        if (bind == 0) return;

        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = (WORD)bind;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = (WORD)bind;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        lastHealTick = now;
    }

    // Config
    bool  enabled = false;
    float minHpPct = 60.0f;
    float minManaPct = 30.0f;
    int   cooldownMs = 2000;
    int   healKeyBind = 0x31; // '1' key
    int   manaKeyBind = 0x32; // '2' key

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Healer", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Healer", L"MinHpPct", L"60", buf, 256, path);
        minHpPct = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Healer", L"MinManaPct", L"30", buf, 256, path);
        minManaPct = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Healer", L"Cooldown", L"2000", buf, 256, path);
        cooldownMs = _wtoi(buf);
        GetPrivateProfileStringW(L"Healer", L"HealKey", L"1", buf, 256, path);
        healKeyBind = buf[0] ? (buf[0] >= L'0' && buf[0] <= L'9' ? buf[0] : 0x31) : 0x31;
        GetPrivateProfileStringW(L"Healer", L"ManaKey", L"2", buf, 256, path);
        manaKeyBind = buf[0] ? (buf[0] >= L'0' && buf[0] <= L'9' ? buf[0] : 0x32) : 0x32;
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Healer", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", minHpPct);
        WritePrivateProfileStringW(L"Healer", L"MinHpPct", buf, path);
        swprintf_s(buf, L"%.0f", minManaPct);
        WritePrivateProfileStringW(L"Healer", L"MinManaPct", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Healer", L"Cooldown", buf, path);
        swprintf_s(buf, L"%c", healKeyBind);
        WritePrivateProfileStringW(L"Healer", L"HealKey", buf, path);
        swprintf_s(buf, L"%c", manaKeyBind);
        WritePrivateProfileStringW(L"Healer", L"ManaKey", buf, path);
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
        CreateWindowExW(0, L"static", L"Min Mana %:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMinMana = CreateWindowExW(0, L"edit", L"30", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+75, y, 50, 22, parent, (HMENU)9103, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Heal Key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtHealKey = CreateWindowExW(0, L"edit", L"1", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+75, y, 40, 22, parent, (HMENU)9104, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Mana Key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtManaKey = CreateWindowExW(0, L"edit", L"2", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+75, y, 40, 22, parent, (HMENU)9105, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"2000", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9106, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtMinHp) { wchar_t b[32]; swprintf_s(b, L"%.0f", minHpPct); SetWindowTextW(hEdtMinHp, b); }
        if (hEdtMinMana) { wchar_t b[32]; swprintf_s(b, L"%.0f", minManaPct); SetWindowTextW(hEdtMinMana, b); }
        if (hEdtHealKey) { wchar_t b[4] = {(wchar_t)healKeyBind, 0}; SetWindowTextW(hEdtHealKey, b); }
        if (hEdtManaKey) { wchar_t b[4] = {(wchar_t)manaKeyBind, 0}; SetWindowTextW(hEdtManaKey, b); }
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9101 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9102 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMinHp, b, 32); minHpPct = (float)_wtof(b);
        }
        if (id == 9103 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMinMana, b, 32); minManaPct = (float)_wtof(b);
        }
        if (id == 9104 && code == EN_CHANGE) {
            wchar_t b[4]; GetWindowTextW(hEdtHealKey, b, 4); healKeyBind = b[0] ? b[0] : 0x31;
        }
        if (id == 9105 && code == EN_CHANGE) {
            wchar_t b[4]; GetWindowTextW(hEdtManaKey, b, 4); manaKeyBind = b[0] ? b[0] : 0x32;
        }
        if (id == 9106 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtMinHp = NULL, hEdtMinMana = NULL;
    HWND hEdtHealKey = NULL, hEdtManaKey = NULL, hEdtCooldown = NULL;
    DWORD lastHealTick = 0;
};
