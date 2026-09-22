#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <cmath>

struct SkillEntry {
    std::wstring name;
    int keyBind = 0;
    int cooldownMs = 0;
    int priority = 0;
    bool enabled = true;
    DWORD lastUsedTick = 0;
};

class AttackerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Attacker"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastAttackTick = 0; attackState = 0; }
    void Stop() override  { lastAttackTick = 0; attackState = 0; }

    void Tick(const GameContext& ctx) override {
        if (!enabled || ctx.hProcess == NULL) return;
        if (targetAddr == 0 || targetAddr <= 0x1000) { attackState = 0; return; }

        DWORD now = ctx.tickCount;

        // Verify target alive
        int hp = ReadInt(ctx.hProcess, targetAddr + ENT_HP_OFFSET);
        if (hp <= 0) { targetAddr = 0; attackState = 0; return; }

        // Get cursor pointer via: warspear.exe+D387AC -> +14 -> +123C
        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (!curPtr) return;

        // Read cursor tile position
        short cx = ReadShort(ctx.hProcess, curPtr + CUR_X_OFFSET);
        short cy = ReadShort(ctx.hProcess, curPtr + CUR_Y_OFFSET);

        // Read mob tile position (raw/65536)
        int mobRawX = ReadInt(ctx.hProcess, targetAddr + ENT_RAW_X_OFFSET);
        int mobRawY = ReadInt(ctx.hProcess, targetAddr + ENT_RAW_Y_OFFSET);
        short mobTX = (short)(mobRawX / 65536);
        short mobTY = (short)(mobRawY / 65536);

        switch (attackState) {
        case 0:
            attackState = 1;
            attackStepTick = now;
            break;

        case 1: { // MOVING cursor toward mob using arrow keys
            if (now - attackStepTick < 60) break;

            if (cx == mobTX && cy == mobTY) {
                int action = ReadInt(ctx.hProcess, curPtr + CUR_ACTION_OFFSET);
                if (action == CURSOR_ACTION_ATTACK_VALUE) {
                    attackState = 2;
                }
                break;
            }

            // Send arrow key via PostMessage to game window
            HWND gw = ctx.gameWindow;
            if (gw && IsWindow(gw)) {
                WORD vk = 0;
                if (cx < mobTX) vk = VK_RIGHT;
                else if (cx > mobTX) vk = VK_LEFT;
                else if (cy < mobTY) vk = VK_DOWN;
                else if (cy > mobTY) vk = VK_UP;

                if (vk) {
                    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                    LPARAM keyDown = 1 | ((LPARAM)scan << 16);
                    LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);
                    PostMessageW(gw, WM_KEYDOWN, vk, keyDown);
                    PostMessageW(gw, WM_KEYUP, vk, keyUp);
                }
            }

            attackStepTick = now;
            break;
        }

        case 2: { // ATTACK: cursor on mob, action==ATTACK
            int action = ReadInt(ctx.hProcess, curPtr + CUR_ACTION_OFFSET);
            if (action != CURSOR_ACTION_ATTACK_VALUE) {
                attackState = 1;
                break;
            }

            if (now - lastAttackTick < (DWORD)globalCooldownMs) break;

            // Press Enter to attack
            HWND gw = ctx.gameWindow;
            if (gw && IsWindow(gw)) {
                UINT scan = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
                LPARAM keyDown = 1 | ((LPARAM)scan << 16);
                LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);
                PostMessageW(gw, WM_KEYDOWN, VK_RETURN, keyDown);
                PostMessageW(gw, WM_KEYUP, VK_RETURN, keyUp);
            }

            lastAttackTick = now;

            // Use skills if configured
            for (auto& skill : skills) {
                if (!skill.enabled || skill.keyBind == 0) continue;
                if (now - skill.lastUsedTick < (DWORD)skill.cooldownMs) continue;

                INPUT keyInputs[2] = {};
                keyInputs[0].type = INPUT_KEYBOARD;
                keyInputs[0].ki.wVk = (WORD)skill.keyBind;
                keyInputs[1].type = INPUT_KEYBOARD;
                keyInputs[1].ki.wVk = (WORD)skill.keyBind;
                keyInputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, keyInputs, sizeof(INPUT));
                skill.lastUsedTick = now;
                break;
            }

            // Check if mob died
            int hpAfter = ReadInt(ctx.hProcess, targetAddr + ENT_HP_OFFSET);
            if (hpAfter <= 0) {
                targetAddr = 0;
                attackState = 0;
            }
            break;
        }
        } // switch
    }

    // Config
    bool  enabled = false;
    DWORD targetAddr = 0;
    int   globalCooldownMs = 1500;
    std::vector<SkillEntry> skills;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Attacker", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Attacker", L"GlobalCooldown", L"1500", buf, 256, path);
        globalCooldownMs = _wtoi(buf);

        skills.clear();
        for (int i = 1; i <= 6; i++) {
            wchar_t sec[16]; swprintf_s(sec, L"Skill%d", i);
            wchar_t name[64], key[8], cd[8], pri[8], en[4];
            swprintf_s(name, L"%sName", sec);
            swprintf_s(key, L"%sKey", sec);
            swprintf_s(cd, L"%sCooldown", sec);
            swprintf_s(pri, L"%sPriority", sec);
            swprintf_s(en, L"%sEnabled", sec);

            wchar_t vName[64]={}, vKey[8]={}, vCd[8]={}, vPri[8]={}, vEn[4]={};
            GetPrivateProfileStringW(L"Attacker", name, L"", vName, 64, path);
            GetPrivateProfileStringW(L"Attacker", key, L"", vKey, 8, path);
            GetPrivateProfileStringW(L"Attacker", cd, L"0", vCd, 8, path);
            GetPrivateProfileStringW(L"Attacker", pri, L"0", vPri, 8, path);
            GetPrivateProfileStringW(L"Attacker", en, L"1", vEn, 4, path);
            if (vName[0] == 0 && vKey[0] == 0) break;

            SkillEntry s;
            s.name = vName;
            s.keyBind = vKey[0] ? vKey[0] : 0;
            s.cooldownMs = _wtoi(vCd);
            s.priority = _wtoi(vPri);
            s.enabled = (vEn[0] == L'1');
            skills.push_back(s);
        }
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Attacker", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%d", globalCooldownMs);
        WritePrivateProfileStringW(L"Attacker", L"GlobalCooldown", buf, path);

        for (size_t i = 0; i < skills.size(); i++) {
            wchar_t sec[16]; swprintf_s(sec, L"Skill%d", (int)(i+1));
            wchar_t nameK[32], keyK[32], cdK[32], priK[32], enK[32];
            swprintf_s(nameK, L"%sName", sec);
            swprintf_s(keyK, L"%sKey", sec);
            swprintf_s(cdK, L"%sCooldown", sec);
            swprintf_s(priK, L"%sPriority", sec);
            swprintf_s(enK, L"%sEnabled", sec);
            WritePrivateProfileStringW(L"Attacker", nameK, skills[i].name.c_str(), path);
            wchar_t v[8]; swprintf_s(v, L"%c", skills[i].keyBind ? skills[i].keyBind : L' ');
            WritePrivateProfileStringW(L"Attacker", keyK, v, path);
            swprintf_s(buf, L"%d", skills[i].cooldownMs);
            WritePrivateProfileStringW(L"Attacker", cdK, buf, path);
            swprintf_s(buf, L"%d", skills[i].priority);
            WritePrivateProfileStringW(L"Attacker", priK, buf, path);
            WritePrivateProfileStringW(L"Attacker", enK, skills[i].enabled ? L"1" : L"0", path);
        }
    }

private:
    DWORD lastAttackTick = 0;
    DWORD attackState = 0;
    DWORD attackStepTick = 0;

    // Memory offsets (must match main.cpp Game:: namespace)
    static constexpr DWORD GM_PTR_OFFSET        = 0x00D387AC;
    static constexpr DWORD GM_OFFSET            = 0x14;
    static constexpr DWORD CURSOR_OFFSET        = 0x123C;
    static constexpr DWORD CUR_X_OFFSET         = 0x08;
    static constexpr DWORD CUR_Y_OFFSET         = 0x0A;
    static constexpr DWORD CUR_ACTION_OFFSET    = 0x7C;
    static constexpr DWORD ENT_RAW_X_OFFSET     = 0x10;
    static constexpr DWORD ENT_RAW_Y_OFFSET     = 0x14;
    static constexpr DWORD ENT_HP_OFFSET        = 0x10C;
    static constexpr int   CURSOR_ACTION_ATTACK_VALUE = 8;

    static int ReadInt(HANDLE hProc, DWORD addr) {
        int val = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)addr, &val, sizeof(int), &r);
        return (r == sizeof(int)) ? val : 0;
    }

    static short ReadShort(HANDLE hProc, DWORD addr) {
        short val = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)addr, &val, sizeof(short), &r);
        return (r == sizeof(short)) ? val : 0;
    }

    static DWORD GetCursorPtr(HANDLE hProc) {
        DWORD gmPtr = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)GM_PTR_OFFSET, &gmPtr, 4, &r);
        if (r != 4 || gmPtr <= 0x1000) return 0;
        DWORD gm = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gmPtr + GM_OFFSET), &gm, 4, &r);
        if (r != 4 || gm <= 0x1000) return 0;
        DWORD cur = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gm + CURSOR_OFFSET), &cur, 4, &r);
        return (r == 4) ? cur : 0;
    }
};
