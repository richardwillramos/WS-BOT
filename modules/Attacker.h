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
        if (targetAddr == 0 || targetAddr <= 0x1000) { attackState = 0; attackCount = 0; noSwordTries = 0; return; }

        DWORD now = ctx.tickCount;

        // WAIT for looter: if pending corpse is valid, don't attack — let looter walk and click
        if (ctx.pendingCorpse && ctx.pendingCorpse->valid) {
            return;
        }

        // PAUSA por prioridade: cura (holdCombat) e dungeon (dungeonBusy)
        // mandam no ataque — heal > loot > attack
        if (ctx.holdCombat || ctx.dungeonBusy) {
            return;
        }

        // Verify target alive
        int hp = ReadInt(ctx.hProcess, targetAddr + ENT_HP_OFFSET);
        if (hp <= 0) {
            if (!killedLogged) {
                DebugLog("[ATTACK] Target killed!");
                killedLogged = true;

                // Save corpse position for looter — use cached mob coords (more reliable than re-searching)
                if (ctx.pendingCorpse) {
                    ctx.pendingCorpse->objAddr = targetAddr;
                    ctx.pendingCorpse->name = targetName;
                    ctx.pendingCorpse->x = targetGX;
                    ctx.pendingCorpse->y = targetGY;
                    ctx.pendingCorpse->time = ctx.tickCount;
                    ctx.pendingCorpse->valid = true;
                    DebugLog("[ATTACK] Saved corpse: '%S' at (%.1f,%.1f)", targetName.c_str(), targetGX, targetGY);
                }

                // Clear target so Targeter can select next mob
                targetAddr = 0;
                attackState = 0;
            }
            return;
        }
        killedLogged = false;

        // Detect unattackable target: if HP unchanged after many attacks, skip
        // Use higher threshold — damage may register in bursts, not every hit
        if (attackCount == 0) {
            lastCheckedHp = hp;
        } else if (attackCount >= 15 && hp >= lastCheckedHp) {
            DebugLog("[ATTACK] Target not taking damage after %d attacks (HP=%d), skipping...", attackCount, hp);
            targetAddr = 0;
            attackState = 0;
            attackCount = 0;
            return;
        }

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
                targetName = m.name;
                targetGX = m.x; targetGY = m.y;
                found = true;
                break;
            }
        }
        if (!found) {
            // Target vanished from mob list while engaged: server removes dead
            // mobs instantly, so the HP<=0 tick is often missed. Treat as kill
            // and save last known coords so the looter still visits the corpse.
            if (attackCount > 0 && ctx.pendingCorpse && !ctx.pendingCorpse->valid) {
                ctx.pendingCorpse->objAddr = targetAddr;
                ctx.pendingCorpse->name = targetName;
                ctx.pendingCorpse->x = targetGX;
                ctx.pendingCorpse->y = targetGY;
                ctx.pendingCorpse->time = ctx.tickCount;
                ctx.pendingCorpse->valid = true;
                DebugLog("[ATTACK] Target vanished after %d attacks, saved corpse: '%S' at (%.1f,%.1f)",
                    attackCount, targetName.c_str(), targetGX, targetGY);
            }
            targetAddr = 0; attackState = 0; attackCount = 0; return;
        }

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

            // 2. The game recomputes cursor+0x7C by itself from the cursor struct
            //    (verified after the update), so no game function call is needed.
            if (ctx.remoteHandleMoveOrAction && ctx.playerAddr > 0x1000) {
                ctx.remoteHandleMoveOrAction(ctx.playerAddr);  // no-op, kept for compatibility
            }

            attackState = 2;
            attackStepTick = now;
            break;

        case 2: { // Verify attack flag and send Enter
            if (now - attackStepTick < 200) break; // wait for game to process

            int action = ReadInt(ctx.hProcess, curPtr + CUR_ACTION_OFFSET);

            if (action == CURSOR_ACTION_ATTACK) {
                noSwordTries = 0;
                DebugLog("[ATTACK] Sword detected! action=%d, sending Enter", action);

                // Fire a ready skill (cursor is already on the mob) instead of
                // the plain hit; key + Enter, same pattern as the healer
                SkillEntry* sk = nullptr;
                for (auto& s : skills) {
                    if (!s.enabled || !s.keyBind) continue;
                    int cd = s.cooldownMs > 0 ? s.cooldownMs : 1500; // default per skill
                    if (now - s.lastUsedTick < (DWORD)cd) continue;
                    if (!sk || s.priority < sk->priority) sk = &s;
                }
                if (sk) {
                    DebugLog("[ATTACK] Skill '%S' key=%c", sk->name.c_str(), (char)sk->keyBind);
                    SendSkillKey(gw, sk->keyBind);
                    sk->lastUsedTick = now;
                }

                // NOTE: do NOT write lp+0x294/0x484 here — experiment (2026-09-24)
                // proved those writes BLOCK the attack: flag 8 + Enter worked
                // (278->159 in one hit) without them, but with them HP never drops.
                SendAttackEnter(gw);
                attackCount++;
                lastAttackTick = now;
                attackState = 3;
                attackStepTick = now;
            } else {
                // Cursor is on the target but the game offers no attack (NPC,
                // friendly, or not reachable). Retry a few times, then DROP the
                // target instead of looping here forever.
                noSwordTries++;
                if (noSwordTries >= MAX_NO_SWORD_TRIES) {
                    DebugLog("[ATTACK] No attack flag on '%S' after %d tries (not attackable?) - skipping",
                             targetName.c_str(), noSwordTries);
                    lastFailedAddr = targetAddr;
                    targetAddr = 0;
                    attackState = 0;
                    attackCount = 0;
                    noSwordTries = 0;
                    return;
                }
                attackState = 0;  // no sword yet, reposition cursor and retry
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
    DWORD lastFailedAddr = 0;   // set once when a target was dropped as unattackable (Targeter parks it)
    int   globalCooldownMs = 1500;
    std::vector<SkillEntry> skills;

    // UI dialog: "1, 3" -> skills list (key only; cooldown falls back to GlobalCooldown)
    void SetSkillKeys(const wchar_t* csv) {
        skills.clear();
        if (!csv) return;
        int prio = 1;
        for (const wchar_t* p = csv; *p; p++) {
            wchar_t c = *p;
            if (c == L' ' || c == L'\t' || c == L',') continue;
            SkillEntry s;
            wchar_t nm[16]; swprintf_s(nm, L"Skill %c", c);
            s.name = nm;
            s.keyBind = (int)c;
            s.cooldownMs = 0;
            s.priority = prio++;
            s.enabled = true;
            skills.push_back(s);
        }
    }

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
    int   attackCount = 0;
    int   lastCheckedHp = 0;
    int   noSwordTries = 0;
    std::wstring targetName;
    float targetGX = 0, targetGY = 0;

    static constexpr DWORD GM_PTR_OFFSET        = 0x00D8F98C;
    static constexpr DWORD GM_OFFSET            = 0x14;
    static constexpr DWORD CURSOR_OFFSET        = 0x1244;
    static constexpr DWORD CUR_X_OFFSET         = 0x08;
    static constexpr DWORD CUR_Y_OFFSET         = 0x0A;
    static constexpr DWORD CUR_RAW_X_OFFSET     = 0x10;
    static constexpr DWORD CUR_RAW_Y_OFFSET     = 0x14;
    static constexpr DWORD CUR_ACTION_OFFSET    = 0x7C;
    static constexpr DWORD ENT_HP_OFFSET        = 0x110;
    static constexpr int   CURSOR_ACTION_ATTACK = 8;
    static constexpr int   MAX_NO_SWORD_TRIES   = 5;   // ~1.5s before we drop an unattackable target

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
        Sleep(80);   // 30ms was too short in tests; 80ms proven to land hits
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    }

    // Press a skill key with foreground focus (then caller sends Enter)
    static void SendSkillKey(HWND gw, int vk) {
        if (!gw) return;
        DWORD fgTid = GetWindowThreadProcessId(gw, NULL);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(gw);
        AttachThreadInput(myTid, fgTid, FALSE);
        keybd_event((BYTE)vk, 0, 0, 0);
        Sleep(50);
        keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
        Sleep(30);
    }
};
