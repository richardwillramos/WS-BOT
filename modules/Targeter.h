#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Filter modes for target selection
enum TargetFilterMode {
    FILTER_ALL      = 0,  // Attack any mob (whitelist/blacklist)
    FILTER_BY_NAME  = 1,  // Attack only mobs matching targetMobName
    FILTER_BY_DIST  = 2,  // Attack any mob within maxDistance
};

class TargeterModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Targeter"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { selectedAddr = 0; selectedName.clear(); }
    void Stop() override  { selectedAddr = 0; selectedName.clear(); }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        // Verify current target is still valid (exists in mob list with HP > 0)
        if (selectedAddr > 0x1000) {
            bool stillAlive = false;
            for (auto& m : ctx.mobs) {
                if (m.objAddr == selectedAddr && m.hp > 0) {
                    stillAlive = true;
                    break;
                }
            }
            if (!stillAlive) {
                DebugLog("[TARGETER] Target lost (dead or removed), retargeting...");
                selectedAddr = 0;
                selectedName.clear();
            }
        }

        // Auto-target if no current target
        // WAIT if pending corpse is valid — let looter finish first
        if (selectedAddr <= 0x1000) {
            if (ctx.pendingCorpse && ctx.pendingCorpse->valid) return;

            float bestDist = 9999.0f;
            for (auto& m : ctx.mobs) {
                if (m.hp <= 0) continue;

                // Hard distance limit (always applied)
                if (m.distance > maxDistance) continue;

                // Apply filter mode
                switch (filterMode) {
                case FILTER_BY_NAME:
                    // Exact name match only (no substring - avoid targeting stronger variants)
                    if (targetMobName.empty()) continue;
                    if (m.name != targetMobName) continue;
                    break;

                case FILTER_BY_DIST:
                    // Already passed maxDistance check above, accept all
                    break;

                case FILTER_ALL:
                default:
                    // Apply whitelist/blacklist
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
                    break;
                }

                // For distance mode: pick closest mob
                // For name mode: pick closest matching mob
                // For all mode: pick first match (original behavior)
                if (filterMode == FILTER_BY_DIST || filterMode == FILTER_BY_NAME) {
                    if (m.distance < bestDist) {
                        bestDist = m.distance;
                        selectedAddr = m.objAddr;
                        selectedName = m.name;
                    }
                } else {
                    selectedAddr = m.objAddr;
                    selectedName = m.name;
                    break;
                }
            }
            if (selectedAddr > 0x1000) {
                DebugLog("[TARGETER] Selected: %S (addr=0x%08X dist=%.1f)", selectedName.c_str(), selectedAddr,
                    filterMode == FILTER_BY_DIST || filterMode == FILTER_BY_NAME ? bestDist : 0.0f);
            }
        }
    }

    // Public state for other modules
    bool  enabled = false;
    DWORD selectedAddr = 0;
    std::wstring selectedName;
    float maxDistance = 20.0f;
    int   filterMode = FILTER_ALL;
    std::wstring targetMobName;  // e.g. "Fada", "Goblin", etc.
    std::vector<std::wstring> whitelist;
    std::vector<std::wstring> blacklist;
    bool  retargetOnNearby = true;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Targeter", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Targeter", L"MaxDistance", L"20", buf, 256, path);
        maxDistance = (float)_wtoi(buf);
        GetPrivateProfileStringW(L"Targeter", L"FilterMode", L"0", buf, 256, path);
        filterMode = _wtoi(buf);
        GetPrivateProfileStringW(L"Targeter", L"TargetMobName", L"", buf, 256, path);
        targetMobName = buf;
        GetPrivateProfileStringW(L"Targeter", L"RetargetOnNearby", L"1", buf, 256, path);
        retargetOnNearby = (buf[0] == L'1');
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Targeter", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", maxDistance);
        WritePrivateProfileStringW(L"Targeter", L"MaxDistance", buf, path);
        swprintf_s(buf, L"%d", filterMode);
        WritePrivateProfileStringW(L"Targeter", L"FilterMode", buf, path);
        WritePrivateProfileStringW(L"Targeter", L"TargetMobName", targetMobName.c_str(), path);
        WritePrivateProfileStringW(L"Targeter", L"RetargetOnNearby", retargetOnNearby ? L"1" : L"0", path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;

        // Enabled checkbox
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9001, GetModuleHandle(NULL), NULL);
        y += 24;

        // Filter mode label
        CreateWindowExW(0, L"static", L"Filter Mode:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        // Filter mode combo
        hCboFilter = CreateWindowExW(0, L"combobox", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, x+85, y, 120, 120,
            parent, (HMENU)9006, GetModuleHandle(NULL), NULL);
        SendMessageW(hCboFilter, CB_ADDSTRING, 0, (LPARAM)L"All (whitelist/blacklist)");
        SendMessageW(hCboFilter, CB_ADDSTRING, 0, (LPARAM)L"By Name");
        SendMessageW(hCboFilter, CB_ADDSTRING, 0, (LPARAM)L"By Distance");
        SendMessageW(hCboFilter, CB_SETCURSEL, filterMode, 0);
        y += 28;

        // Target mob name (shown when filterMode == FILTER_BY_NAME)
        hLblMobName = CreateWindowExW(0, L"static", L"Mob Name:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMobName = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+85, y, 160, 22, parent, (HMENU)9007, GetModuleHandle(NULL), NULL);
        y += 28;

        // Max distance
        CreateWindowExW(0, L"static", L"Max Distance:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMaxDist = CreateWindowExW(0, L"edit", L"20", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 60, 22, parent, (HMENU)9002, GetModuleHandle(NULL), NULL);
        y += 28;

        // Retarget on nearby
        hChkRetarget = CreateWindowExW(0, L"button", L"Retarget on Nearby",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9003, GetModuleHandle(NULL), NULL);
        y += 28;

        // Whitelist (only for FILTER_ALL mode)
        hLblWhitelist = CreateWindowExW(0, L"static", L"Whitelist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtWhitelist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9004, GetModuleHandle(NULL), NULL);
        y += 28;

        // Blacklist (only for FILTER_ALL mode)
        hLblBlacklist = CreateWindowExW(0, L"static", L"Blacklist (comma-sep):", WS_CHILD|WS_VISIBLE, x, y, 200, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        y += 18;
        hEdtBlacklist = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x, y, w, 22, parent, (HMENU)9005, GetModuleHandle(NULL), NULL);

        UpdateUI();
        UpdateFilterVisibility();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkRetarget) SendMessage(hChkRetarget, BM_SETCHECK, retargetOnNearby ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtMaxDist) { wchar_t b[32]; swprintf_s(b, L"%.0f", maxDistance); SetWindowTextW(hEdtMaxDist, b); }
        if (hCboFilter) SendMessageW(hCboFilter, CB_SETCURSEL, filterMode, 0);
        if (hEdtMobName) SetWindowTextW(hEdtMobName, targetMobName.c_str());
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
        if (id == 9006 && code == CBN_SELCHANGE) {
            filterMode = (int)SendMessageW(hCboFilter, CB_GETCURSEL, 0, 0);
            UpdateFilterVisibility();
        }
        if (id == 9007 && code == EN_CHANGE) {
            wchar_t b[256]; GetWindowTextW(hEdtMobName, b, 256); targetMobName = b;
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
    HWND hCboFilter = NULL, hEdtMobName = NULL, hLblMobName = NULL;
    HWND hEdtWhitelist = NULL, hEdtBlacklist = NULL;
    HWND hLblWhitelist = NULL, hLblBlacklist = NULL;

    void UpdateFilterVisibility() {
        bool byName = (filterMode == FILTER_BY_NAME);
        bool byAll  = (filterMode == FILTER_ALL);

        if (hLblMobName)  ShowWindow(hLblMobName,  byName ? SW_SHOW : SW_HIDE);
        if (hEdtMobName)  ShowWindow(hEdtMobName,  byName ? SW_SHOW : SW_HIDE);
        if (hLblWhitelist) ShowWindow(hLblWhitelist, byAll ? SW_SHOW : SW_HIDE);
        if (hEdtWhitelist) ShowWindow(hEdtWhitelist, byAll ? SW_SHOW : SW_HIDE);
        if (hLblBlacklist) ShowWindow(hLblBlacklist, byAll ? SW_SHOW : SW_HIDE);
        if (hEdtBlacklist) ShowWindow(hEdtBlacklist, byAll ? SW_SHOW : SW_HIDE);
    }
};
