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
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;
        if (targetAddr == 0 || targetAddr <= 0x1000) { attackState = 0; return; }

        DWORD now = ctx.tickCount;

        // Verify target alive - just skip, don't clear targetAddr (let Targeter/sync handle it)
        int hp = ReadInt(ctx.hProcess, targetAddr + ENT_HP_OFFSET);
        if (hp <= 0) {
            if (!killedLogged) {
                DebugLog("[ATTACK] Target killed!");
                killedLogged = true;

                // Save corpse position for looter (same as backup pending corpse system)
                if (ctx.pendingCorpse) {
                    for (auto& m : ctx.mobs) {
                        if (m.objAddr == targetAddr) {
                            ctx.pendingCorpse->objAddr = targetAddr;
                            ctx.pendingCorpse->name = m.name;
                            ctx.pendingCorpse->x = m.x;
                            ctx.pendingCorpse->y = m.y;
                            ctx.pendingCorpse->time = ctx.tickCount;
                            ctx.pendingCorpse->valid = true;
                            DebugLog("[ATTACK] Saved corpse: '%S' at (%.1f,%.1f)", m.name.c_str(), m.x, m.y);
                            break;
                        }
                    }
                }
            }
            attackState = 0;
            return;
        }
        killedLogged = false;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // Get cursor pointer
        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (!curPtr) return;

        // Find target in mob list to get coordinates
        float mobGX = 0, mobGY = 0;
        bool found = false;
        for (auto& m : ctx.mobs) {
            if (m.objAddr == targetAddr) {
                mobGX = m.x; mobGY = m.y;
                found = true;
                break;
            }
        }
        if (!found) { targetAddr = 0; attackState = 0; return; }

        // Convert mob game coords to tile coords
        WORD mobTileX = (WORD)((int)(mobGX / 24.0f));
        WORD mobTileY = (WORD)((int)(mobGY / 24.0f));
        if (mobTileX > 27) mobTileX = 27;
        if (mobTileY > 27) mobTileY = 27;

        switch (attackState) {
        case 0: // Position cursor on mob + call game to process + attack
            DebugLog("[ATTACK] Target: %S addr=0x%08X game(%.1f,%.1f) tile(%d,%d)",
                L"", targetAddr, mobGX, mobGY, mobTileX, mobTileY);

            // 1. Write cursor position on mob (tile + raw)
            WriteCursorOnMob(ctx.hProcess, curPtr, mobTileX, mobTileY);

            // 2. Call HandleMoveOrAction to force game to process cursor position
            //    This updates +0x7C to 8 (attack) if cursor is on hostile mob
            if (ctx.remoteHandleMoveOrAction && ctx.playerAddr > 0x1000) {
                ctx.remoteHandleMoveOrAction(ctx.playerAddr);
            }

            attackState = 2;
            attackStepTick = now;
            break;

        case 2: { // Verify attack flag and send Enter
            if (now - attackStepTick < 200) break; // wait for game to process

            int action = ReadInt(ctx.hProcess, curPtr + CUR_ACTION_OFFSET);

            if (action == CURSOR_ACTION_ATTACK) {
                DebugLog("[ATTACK] Sword detected! action=%d, sending Enter", action);

                // Sword detected! Set target + send Enter
                if (ctx.playerAddr > 0x1000) {
                    DWORD ta = targetAddr;
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(ctx.playerAddr + 0x290), &ta, 4, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(ctx.playerAddr + 0x478), &ta, 4, NULL);
                }
                SendAttackEnter(gw);
                lastAttackTick = now;
                attackState = 3;
                attackStepTick = now;
            } else {
                // No sword yet, retry from step 0
                attackState = 0;
            }
            break;
        }

        case 3: // Cooldown before next attack
            if (now - attackStepTick < 300) break;
            attackState = 0;
            break;
        }
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
            wchar_t name[64], key[32], cd[16], pri[16], en[16];
            swprintf_s(name, L"%sName", sec);
            swprintf_s(key, L"%sKey", sec);
            swprintf_s(cd, L"%sCooldown", sec);
            swprintf_s(pri, L"%sPriority", sec);
            swprintf_s(en, L"%sEnabled", sec);

            wchar_t vName[64]={}, vKey[32]={}, vCd[16]={}, vPri[16]={}, vEn[16]={};
            GetPrivateProfileStringW(L"Attacker", name, L"", vName, 64, path);
            GetPrivateProfileStringW(L"Attacker", key, L"", vKey, 32, path);
            GetPrivateProfileStringW(L"Attacker", cd, L"0", vCd, 16, path);
            GetPrivateProfileStringW(L"Attacker", pri, L"0", vPri, 16, path);
            GetPrivateProfileStringW(L"Attacker", en, L"1", vEn, 16, path);
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
    bool  killedLogged = false;

    static constexpr DWORD GM_PTR_OFFSET        = 0x00D387AC;
    static constexpr DWORD GM_OFFSET            = 0x14;
    static constexpr DWORD CURSOR_OFFSET        = 0x123C;
    static constexpr DWORD CUR_X_OFFSET         = 0x08;
    static constexpr DWORD CUR_Y_OFFSET         = 0x0A;
    static constexpr DWORD CUR_RAW_X_OFFSET     = 0x10;
    static constexpr DWORD CUR_RAW_Y_OFFSET     = 0x14;
    static constexpr DWORD CUR_ACTION_OFFSET    = 0x7C;
    static constexpr DWORD ENT_HP_OFFSET        = 0x10C;
    static constexpr int   CURSOR_ACTION_ATTACK = 8;

    static int ReadInt(HANDLE hProc, DWORD addr) {
        int val = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)addr, &val, sizeof(int), &r);
        return (r == sizeof(int)) ? val : 0;
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

    // Write cursor position on mob (tile coords + raw coords, NO walk flag)
    static void WriteCursorOnMob(HANDLE hProc, DWORD curPtr, WORD tileX, WORD tileY) {
        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(hProc, (LPVOID)(curPtr + CUR_X_OFFSET),     &tileX, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + CUR_Y_OFFSET),     &tileY, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + CUR_RAW_X_OFFSET), &rawX, 4, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + CUR_RAW_Y_OFFSET), &rawY, 4, NULL);
    }

    // Send Enter locally with foreground focus (same as follower/backup)
    static void SendAttackEnter(HWND gw) {
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
