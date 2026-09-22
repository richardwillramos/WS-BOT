#pragma once
#include "../include/IModule.h"
#include <string>

class ExtraModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Extra"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastTick = 0; }
    void Stop() override  { lastTick = 0; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastTick < 5000) return; // 5 second interval

        // Anti-AFK: press a key to prevent AFK kick
        if (antiAfk) {
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_SPACE;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_SPACE;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
        }

        lastTick = now;
    }

    // Config
    bool  enabled = false;
    bool  antiAfk = true;
    bool  autoRevive = false;
    bool  autoSell = false;
    bool  autoRepair = false;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Extra", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AntiAfk", L"1", buf, 256, path);
        antiAfk = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoRevive", L"0", buf, 256, path);
        autoRevive = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoSell", L"0", buf, 256, path);
        autoSell = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoRepair", L"0", buf, 256, path);
        autoRepair = (buf[0] == L'1');
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Extra", L"Enabled", enabled ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AntiAfk", antiAfk ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoRevive", autoRevive ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoSell", autoSell ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoRepair", autoRepair ? L"1" : L"0", path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9501, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAntiAfk = CreateWindowExW(0, L"button", L"Anti AFK",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9502, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoRevive = CreateWindowExW(0, L"button", L"Auto Revive",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9503, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoSell = CreateWindowExW(0, L"button", L"Auto Sell",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9504, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoRepair = CreateWindowExW(0, L"button", L"Auto Repair",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9505, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAntiAfk) SendMessage(hChkAntiAfk, BM_SETCHECK, antiAfk ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoRevive) SendMessage(hChkAutoRevive, BM_SETCHECK, autoRevive ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoSell) SendMessage(hChkAutoSell, BM_SETCHECK, autoSell ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoRepair) SendMessage(hChkAutoRepair, BM_SETCHECK, autoRepair ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void OnCommand(int id, int code) override {
        if (id == 9501 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9502 && code == BN_CLICKED)
            antiAfk = (SendMessage(hChkAntiAfk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9503 && code == BN_CLICKED)
            autoRevive = (SendMessage(hChkAutoRevive, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9504 && code == BN_CLICKED)
            autoSell = (SendMessage(hChkAutoSell, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9505 && code == BN_CLICKED)
            autoRepair = (SendMessage(hChkAutoRepair, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hChkAntiAfk = NULL;
    HWND hChkAutoRevive = NULL, hChkAutoSell = NULL, hChkAutoRepair = NULL;
    DWORD lastTick = 0;
};
