#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>

struct PartyMember {
    std::wstring name;
    float minHpPct = 50.0f;
    int healKeyBind = 0x33; // '3'
    bool enabled = true;
};

class PartyHealerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Party Healer"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastHealTick = 0; }
    void Stop() override  { lastHealTick = 0; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastHealTick < (DWORD)cooldownMs) return;

        for (auto& member : members) {
            if (!member.enabled) continue;
            // Find player in entity list
            for (auto& p : ctx.players) {
                if (p.name != member.name) continue;
                if (p.maxHp <= 0) continue;
                float hpPct = (float)p.hp / p.maxHp * 100.0f;
                if (hpPct > member.minHpPct) continue;

                // Cast heal on party member
                // First, select the party member (write target)
                DWORD target = p.objAddr;
                DWORD written = 0;
                DWORD lp = ctx.playerAddr;
                if (lp > 0x1000) {
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(lp + 0x294), &target, 4, &written);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(lp + 0x484), &target, 4, &written);
                }
                Sleep(100);

                // Then cast heal key
                INPUT inputs[2] = {};
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = (WORD)member.healKeyBind;
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = (WORD)member.healKeyBind;
                inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, inputs, sizeof(INPUT));

                lastHealTick = now;
                return; // one heal per tick
            }
        }
    }

    // Config
    bool  enabled = false;
    int   cooldownMs = 3000;
    std::vector<PartyMember> members;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"PartyHealer", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"PartyHealer", L"Cooldown", L"3000", buf, 256, path);
        cooldownMs = _wtoi(buf);

        members.clear();
        for (int i = 1; i <= 5; i++) {
            wchar_t sec[16]; swprintf_s(sec, L"Member%d", i);
            wchar_t nameK[32], hpK[32], keyK[32], enK[32];
            swprintf_s(nameK, L"%sName", sec);
            swprintf_s(hpK, L"%sMinHp", sec);
            swprintf_s(keyK, L"%sHealKey", sec);
            swprintf_s(enK, L"%sEnabled", sec);

            wchar_t vName[64]={}, vHp[8]={}, vKey[8]={}, vEn[4]={};
            GetPrivateProfileStringW(L"PartyHealer", nameK, L"", vName, 64, path);
            GetPrivateProfileStringW(L"PartyHealer", hpK, L"50", vHp, 8, path);
            GetPrivateProfileStringW(L"PartyHealer", keyK, L"3", vKey, 8, path);
            GetPrivateProfileStringW(L"PartyHealer", enK, L"1", vEn, 4, path);
            if (vName[0] == 0) break;

            PartyMember m;
            m.name = vName;
            m.minHpPct = (float)_wtof(vHp);
            m.healKeyBind = vKey[0] ? vKey[0] : 0x33;
            m.enabled = (vEn[0] == L'1');
            members.push_back(m);
        }
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"PartyHealer", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"PartyHealer", L"Cooldown", buf, path);

        for (size_t i = 0; i < members.size(); i++) {
            wchar_t sec[16]; swprintf_s(sec, L"Member%d", (int)(i+1));
            wchar_t nameK[32], hpK[32], keyK[32], enK[32];
            swprintf_s(nameK, L"%sName", sec);
            swprintf_s(hpK, L"%sMinHp", sec);
            swprintf_s(keyK, L"%sHealKey", sec);
            swprintf_s(enK, L"%sEnabled", sec);
            WritePrivateProfileStringW(L"PartyHealer", nameK, members[i].name.c_str(), path);
            swprintf_s(buf, L"%.0f", members[i].minHpPct);
            WritePrivateProfileStringW(L"PartyHealer", hpK, buf, path);
            swprintf_s(buf, L"%c", members[i].healKeyBind);
            WritePrivateProfileStringW(L"PartyHealer", keyK, buf, path);
            WritePrivateProfileStringW(L"PartyHealer", enK, members[i].enabled ? L"1" : L"0", path);
        }
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9201, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"3000", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9202, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Party Members:", WS_CHILD|WS_VISIBLE, x, y, 200, 18, parent, NULL, GetModuleHandle(NULL), NULL);

        y += 20;
        hLstMembers = CreateWindowExW(0, L"listbox", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
            x, y, w, 120, parent, (HMENU)9203, GetModuleHandle(NULL), NULL);

        y += 125;
        hBtnAdd = CreateWindowExW(0, L"button", L"Add", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, y, 60, 22, parent, (HMENU)9204, GetModuleHandle(NULL), NULL);
        hBtnRemove = CreateWindowExW(0, L"button", L"Remove", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x+65, y, 60, 22, parent, (HMENU)9205, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
        if (hLstMembers) {
            SendMessageW(hLstMembers, LB_RESETCONTENT, 0, 0);
            for (auto& m : members) {
                wchar_t buf[128];
                swprintf_s(buf, L"%s (HP<%.0f%%, key=%c) %s", m.name.c_str(), m.minHpPct, m.healKeyBind, m.enabled ? L"" : L"[OFF]");
                SendMessageW(hLstMembers, LB_ADDSTRING, 0, (LPARAM)buf);
            }
        }
    }

    void OnCommand(int id, int code) override {
        if (id == 9201 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9202 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
        if (id == 9204 && code == BN_CLICKED) {
            PartyMember m; m.name = L"NewMember"; m.minHpPct = 50; m.healKeyBind = 0x33;
            members.push_back(m);
            UpdateUI();
        }
        if (id == 9205 && code == BN_CLICKED) {
            int sel = (int)SendMessageW(hLstMembers, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)members.size()) {
                members.erase(members.begin() + sel);
                UpdateUI();
            }
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtCooldown = NULL;
    HWND hLstMembers = NULL, hBtnAdd = NULL, hBtnRemove = NULL;
    DWORD lastHealTick = 0;
};
