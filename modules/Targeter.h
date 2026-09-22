#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

class TargeterModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Targeter"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { selectedAddr = 0; selectedName.clear(); }
    void Stop() override  { selectedAddr = 0; selectedName.clear(); }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;

        // If we have a target, verify it's still alive
        if (selectedAddr > 0x1000) {
            DWORD hp = 0; SIZE_T r = 0;
            ReadProcessMemory(ctx.hProcess, (LPCVOID)(selectedAddr + 0x10C), &hp, 4, &r);
            if (r != 4 || hp <= 0) {
                // Target dead or gone
                selectedAddr = 0;
                selectedName.clear();
            }
        }

        // Auto-target if no current target
        if (selectedAddr <= 0x1000) {
            for (auto& m : ctx.mobs) {
                if (m.hp <= 0) continue;
                if (m.distance > maxDistance) continue;
                if (!blacklist.empty()) {
                    bool blocked = false;
                    for (auto& b : blacklist)
                        if (m.name.find(b) != std::wstring::npos) { blocked = true; break; }
                    if (blocked) continue;
                }
                if (!whitelist.empty()) {
                    bool allowed = false;
                    for (auto& w : whitelist)
                        if (m.name.find(w) != std::wstring::npos) { allowed = true; break; }
                    if (!allowed) continue;
                }
                selectedAddr = m.objAddr;
                selectedName = m.name;
                break;
            }
        }
    }

    // Public state for other modules
    bool  enabled = false;
    DWORD selectedAddr = 0;
    std::wstring selectedName;
    float maxDistance = 20.0f;
    std::vector<std::wstring> whitelist;
    std::vector<std::wstring> blacklist;
    bool  retargetOnNearby = true;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Targeter", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Targeter", L"MaxDistance", L"20", buf, 256, path);
        maxDistance = (float)_wtoi(buf);
        GetPrivateProfileStringW(L"Targeter", L"RetargetOnNearby", L"1", buf, 256, path);
        retargetOnNearby = (buf[0] == L'1');
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Targeter", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32]; swprintf_s(buf, L"%.0f", maxDistance);
        WritePrivateProfileStringW(L"Targeter", L"MaxDistance", buf, path);
        WritePrivateProfileStringW(L"Targeter", L"RetargetOnNearby", retargetOnNearby ? L"1" : L"0", path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9001, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Max Distance:", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMaxDist = CreateWindowExW(0, L"edit", L"20", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9002, GetModuleHandle(NULL), NULL);

        y += 28;
        hChkRetarget = CreateWindowExW(0, L"button", L"Retarget on Nearby",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9003, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Whitelist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtWhitelist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9004, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Blacklist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtBlacklist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9005, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkRetarget) SendMessage(hChkRetarget, BM_SETCHECK, retargetOnNearby ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtMaxDist) { wchar_t b[32]; swprintf_s(b, L"%.0f", maxDistance); SetWindowTextW(hEdtMaxDist, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9001 && code == BN_CLICKED) {
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        if (id == 9003 && code == BN_CLICKED) {
            retargetOnNearby = (SendMessage(hChkRetarget, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        if (id == 9002 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMaxDist, b, 32); maxDistance = (float)_wtoi(b);
        }
        if (id == 9004 && code == EN_CHANGE) {
            wchar_t b[256]; GetWindowTextW(hEdtWhitelist, b, 256);
            whitelist.clear();
            wchar_t* ctx; wchar_t* tok = wcstok_s(b, L",", &ctx);
            while (tok) { while (*tok == L' ') tok++; whitelist.push_back(tok); tok = wcstok_s(NULL, L",", &ctx); }
        }
        if (id == 9005 && code == EN_CHANGE) {
            wchar_t b[256]; GetWindowTextW(hEdtBlacklist, b, 256);
            blacklist.clear();
            wchar_t* ctx; wchar_t* tok = wcstok_s(b, L",", &ctx);
            while (tok) { while (*tok == L' ') tok++; blacklist.push_back(tok); tok = wcstok_s(NULL, L",", &ctx); }
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtMaxDist = NULL, hChkRetarget = NULL;
    HWND hEdtWhitelist = NULL, hEdtBlacklist = NULL;
};
