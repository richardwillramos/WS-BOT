#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <cmath>

// Lista de bosses (config\boss_names.json) - definida em main.cpp
extern std::vector<std::wstring> g_bossNames;
inline const std::vector<std::wstring>& BossNames() { return g_bossNames; }

// Supervisor de dungeon: orquestra as fases de uma run.
//   WAVE1 (matar) -> PORTAL1 ("Passagem") -> WAVE2 (matar, boss por ultimo)
//   -> BOSS_LOOT (looter coleta o drop do boss) -> CHEST (bau) -> EXIT ("Saida")
// Em fases de interacao seta ctx.dungeonBusy (targeter/attacker/follower/looter
// cedem o tick); nas waves seta ctx.dungeonNoLoot se LootInWaves estiver off.
class DungeonModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Dungeon"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    enum Phase { PH_WAVE1 = 0, PH_PORTAL1, PH_WAVE2, PH_BOSS_LOOT, PH_CHEST, PH_EXIT, PH_DONE };

    const wchar_t* PhaseName() const {
        switch (phase) {
            case PH_WAVE1:     return L"WAVE1";
            case PH_PORTAL1:   return L"PORTAL1";
            case PH_WAVE2:     return L"WAVE2";
            case PH_BOSS_LOOT: return L"BOSS_LOOT";
            case PH_CHEST:     return L"CHEST";
            case PH_EXIT:      return L"EXIT";
            default:           return L"DONE";
        }
    }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled) return;

        // Edge: ativado agora (a UI liga .enabled direto) — reinicia a run
        if (!wasEnabled) {
            wasEnabled = true;
            EnterPhase(ctx, PH_WAVE1, "module enabled");
        }

        if (ctx.hProcess == NULL) return;
        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        DWORD now = ctx.tickCount;
        int hostiles = CountHostiles(ctx);

        // Salto de posicao = teleport (entrada de portal)
        bool posJump = false;
        if (prevX != 0 || prevY != 0) {
            float jdx = ctx.selfX - prevX, jdy = ctx.selfY - prevY;
            posJump = (jdx*jdx + jdy*jdy) > (200.0f * 200.0f); // >~8 tiles num tick
        }
        prevX = ctx.selfX; prevY = ctx.selfY;

        switch (phase) {
        case PH_WAVE1:
            SetFlags(ctx, false, !lootInWaves);
            if (hostiles > 0) { waveSawMobs = true; clearSince = 0; }
            if (hostiles == 0) {
                if (!waveSawMobs && now - phaseStart < 3000) break; // grace: ativou no meio
                if (clearSince == 0) { clearSince = now; break; }
                if (now - clearSince >= 3000) EnterPhase(ctx, PH_PORTAL1, "wave1 cleared");
            }
            break;

        case PH_PORTAL1:
            SetFlags(ctx, true, true);
            // Sucesso: teleport ou mobs novos apareceram (fase 2 carregou)
            if (iactTries > 0 && (posJump || hostiles > 0)) {
                EnterPhase(ctx, PH_WAVE2, posJump ? "teleported" : "new mobs appeared");
                break;
            }
            StepInteract(ctx, gw, now, portal1Name);
            break;

        case PH_WAVE2:
            SetFlags(ctx, false, !lootInWaves);
            if (hostiles > 0) {
                waveSawMobs = true; clearSince = 0;
                if (!sawBoss && IsBoss(ctx)) sawBoss = true;
            }
            if (hostiles == 0 && waveSawMobs && clearSince == 0) clearSince = now;
            if (hostiles == 0 && clearSince && now - clearSince >= 3000) {
                bool bossListEmpty = BossNames().empty();
                bool bossDone = bossListEmpty || sawBoss || (now - phaseStart > 120000);
                if (bossDone) EnterPhase(ctx, PH_BOSS_LOOT, "wave2 cleared");
            }
            // Nunca viu mob nenhum: provavelmente o portal nao funcionou
            if (!waveSawMobs && now - phaseStart > 60000)
                DebugLog("[DUNGEON] No mobs seen in WAVE2 for 60s - portal Enter may have failed");
            break;

        case PH_BOSS_LOOT:
            // Deixa o looter coletar o drop do boss antes de ir pro bau
            SetFlags(ctx, false, false);
            if (now - phaseStart >= 6000) {
                bool noLootLeft = ctx.corpses.empty()
                    && !(ctx.pendingCorpse && ctx.pendingCorpse->valid)
                    && !HasDeadMob(ctx);
                if (noLootLeft || now - phaseStart >= 25000) EnterPhase(ctx, PH_CHEST, "boss loot done");
            }
            break;

        case PH_CHEST:
            SetFlags(ctx, true, true);
            if (collectLeft > 0) {
                if (now - iactTick >= 400) {
                    iactTick = now;
                    SendLocalEnter(gw);
                    collectLeft--;
                    DebugLog("[DUNGEON] Chest collect (%d left)", collectLeft);
                    if (collectLeft == 0) EnterPhase(ctx, PH_EXIT, "chest collected");
                }
                break;
            }
            if (iactTries >= 1 && iactState == 2) {
                // Bau abriu (Enter enviado) - comeca a coleta
                collectLeft = collectTries;
                iactTick = now;
                DebugLog("[DUNGEON] Chest opened - collecting %d times", collectTries);
                break;
            }
            StepInteract(ctx, gw, now, chestName);
            break;

        case PH_EXIT:
            SetFlags(ctx, true, true);
            if (iactTries >= 1 && iactState == 2) {
                // Portal ativado - confirmar dialogo "Tem certeza?" (VALIDAR AO VIVO)
                if (confirmLeft == 0) confirmLeft = 2;
                if (now - iactTick >= 600) {
                    iactTick = now;
                    SendLocalEnter(gw);
                    confirmLeft--;
                    DebugLog("[DUNGEON] Confirm dialog (%d left)", confirmLeft);
                    if (confirmLeft <= 0) EnterPhase(ctx, PH_DONE, "exit confirmed");
                }
                break;
            }
            StepInteract(ctx, gw, now, exitName);
            break;

        case PH_DONE:
            SetFlags(ctx, false, false);
            if (autoDisable && enabled) {
                enabled = false;
                wasEnabled = false;
                DebugLog("[DUNGEON] Run complete - module disabled");
            }
            break;
        }
    }

    // Config
    bool  enabled = false;
    std::wstring portal1Name = L"Passagem";
    std::wstring chestName    = L"Ba\u00FA";     // Baú
    std::wstring exitName     = L"Sa\u00EDda";    // Saída
    float walkRadius = 10.0f;
    float maxDist    = 25.0f;
    int   collectTries = 6;
    bool  lootInWaves  = false; // mobs das waves nao dropam loot
    bool  autoDisable  = true;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Dungeon", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Dungeon", L"Portal1Name", portal1Name.c_str(), buf, 256, path);
        if (buf[0]) portal1Name = buf;
        GetPrivateProfileStringW(L"Dungeon", L"ChestName", chestName.c_str(), buf, 256, path);
        if (buf[0]) chestName = buf;
        GetPrivateProfileStringW(L"Dungeon", L"ExitName", exitName.c_str(), buf, 256, path);
        if (buf[0]) exitName = buf;
        GetPrivateProfileStringW(L"Dungeon", L"WalkRadius", L"10", buf, 256, path);
        walkRadius = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Dungeon", L"MaxDist", L"25", buf, 256, path);
        maxDist = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Dungeon", L"CollectTries", L"6", buf, 256, path);
        collectTries = _wtoi(buf);
        GetPrivateProfileStringW(L"Dungeon", L"LootInWaves", L"0", buf, 256, path);
        lootInWaves = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Dungeon", L"AutoDisable", L"1", buf, 256, path);
        autoDisable = (buf[0] != L'0');
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Dungeon", L"Enabled", enabled ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Dungeon", L"Portal1Name", portal1Name.c_str(), path);
        WritePrivateProfileStringW(L"Dungeon", L"ChestName", chestName.c_str(), path);
        WritePrivateProfileStringW(L"Dungeon", L"ExitName", exitName.c_str(), path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", walkRadius);
        WritePrivateProfileStringW(L"Dungeon", L"WalkRadius", buf, path);
        swprintf_s(buf, L"%.0f", maxDist);
        WritePrivateProfileStringW(L"Dungeon", L"MaxDist", buf, path);
        swprintf_s(buf, L"%d", collectTries);
        WritePrivateProfileStringW(L"Dungeon", L"CollectTries", buf, path);
        WritePrivateProfileStringW(L"Dungeon", L"LootInWaves", lootInWaves ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Dungeon", L"AutoDisable", autoDisable ? L"1" : L"0", path);
    }

private:
    Phase phase = PH_WAVE1;
    bool  wasEnabled = false;
    DWORD phaseStart = 0, clearSince = 0;
    bool  waveSawMobs = false, sawBoss = false;
    float prevX = 0, prevY = 0;
    // Sub-maquina de interacao (andar + cursor + Enter)
    int   iactState = 0;      // 0=find/pos, 1=cursor escrito, 2=Enter enviado
    DWORD iactTick = 0, lastWalkTick = 0, lastFindLog = 0;
    int   iactTries = 0;
    int   collectLeft = 0, confirmLeft = 0;

    void SetFlags(const GameContext& ctx, bool busy, bool noLoot) {
        const_cast<GameContext&>(ctx).dungeonBusy = busy;
        const_cast<GameContext&>(ctx).dungeonNoLoot = noLoot;
    }

    void EnterPhase(const GameContext& ctx, Phase p, const char* why) {
        extern void DebugLog(const char* fmt, ...);
        phase = p;
        phaseStart = ctx.tickCount;
        clearSince = 0; waveSawMobs = false; sawBoss = false;
        iactState = 0; iactTries = 0; lastWalkTick = 0;
        collectLeft = 0; confirmLeft = 0;
        prevX = 0; prevY = 0;
        DebugLog("[DUNGEON] Phase %s (%s)", PhaseName(), why);
    }

    int CountHostiles(const GameContext& ctx) const {
        int n = 0;
        for (auto& m : ctx.mobs) if (m.hp > 0) n++;
        return n;
    }

    bool HasDeadMob(const GameContext& ctx) const {
        for (auto& m : ctx.mobs) if (m.hp <= 0 && m.maxHp > 0) return true;
        return false;
    }

    static bool MatchName(const std::wstring& ent, const std::wstring& pat) {
        if (pat.empty()) return false;
        if (!pat.empty() && pat.back() == L'*')
            return ent.compare(0, pat.size() - 1, pat, 0, pat.size() - 1) == 0;
        return ent == pat;
    }

    bool IsBoss(const GameContext& ctx) const {
        for (auto& m : ctx.mobs) {
            if (m.hp <= 0) continue;
            for (auto& b : BossNames())
                if (MatchName(m.name, b)) return true;
        }
        return false;
    }

    // Procura entidade por nome em npcs + mobs + corpses
    bool FindEntity(const GameContext& ctx, const std::wstring& name, float& ex, float& ey) const {
        float best = 1e9f; bool found = false;
        auto consider = [&](const std::wstring& n, float x, float y) {
            if (!MatchName(n, name)) return;
            float dx = x - ctx.selfX, dy = y - ctx.selfY;
            float d = sqrtf(dx*dx + dy*dy);
            if (d > maxDist) return;          // nao atravessa o mapa
            if (d < best) { best = d; ex = x; ey = y; found = true; }
        };
        for (auto& e : ctx.npcs)  consider(e.name, e.x, e.y);
        for (auto& e : ctx.mobs)  consider(e.name, e.x, e.y);
        for (auto& c : ctx.corpses) consider(c.name, c.x, c.y);
        return found;
    }

    // Anda + interage. Retorna 0=em progresso, 1=Enter enviado neste tick.
    int StepInteract(const GameContext& ctx, HWND gw, DWORD now, const std::wstring& name) {
        extern void DebugLog(const char* fmt, ...);
        float ex = 0, ey = 0;
        if (!FindEntity(ctx, name, ex, ey)) {
            if (now - lastFindLog > 10000) {
                lastFindLog = now;
                DebugLog("[DUNGEON] Waiting for '%S' (not found within %.0f)", name.c_str(), maxDist);
            }
            return 0;
        }
        float dx = ex - ctx.selfX, dy = ey - ctx.selfY;
        float d = sqrtf(dx*dx + dy*dy);
        if (d > walkRadius) {
            if (now - lastWalkTick >= 700) {
                lastWalkTick = now;
                DebugLog("[DUNGEON] Walk to '%S' dist=%.1f", name.c_str(), d);
                WalkTo(ctx, gw, ex, ey);
            }
            return 0;
        }
        if (iactState == 0) {
            WriteCursorOn(ctx.hProcess, ex, ey);
            iactState = 1;
            iactTick = now;
            return 0;
        }
        if (iactState == 1) {
            if (now - iactTick < 250) return 0;
            SendLocalEnter(gw);
            iactTries++;
            iactState = 2;
            iactTick = now;
            DebugLog("[DUNGEON] Enter on '%S' (try %d)", name.c_str(), iactTries);
            return 1;
        }
        // iactState == 2: espera e tenta de novo se a fase nao avancou
        if (now - iactTick >= 1500) iactState = 0;
        return 2;
    }

    void WalkTo(const GameContext& ctx, HWND gw, float gameX, float gameY) {
        WORD tileX = (WORD)((int)(gameX / 24.0f));
        WORD tileY = (WORD)((int)(gameY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;
        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (curPtr > 0x1000) {
            int rawX = (int)tileX * 0x180000;
            int rawY = (int)tileY * 0x180000;
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
            DWORD walkFlag = 0x10;
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x7C), &walkFlag, 4, NULL);
        }
        SendLocalEnter(gw);
    }

    static void WriteCursorOn(HANDLE hProc, float gameX, float gameY) {
        DWORD curPtr = GetCursorPtr(hProc);
        if (curPtr <= 0x1000) return;
        WORD tileX = (WORD)((int)(gameX / 24.0f));
        WORD tileY = (WORD)((int)(gameY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;
        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
    }

    static DWORD GetCursorPtr(HANDLE hProc) {
        DWORD gmPtr = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)0x00D8F98C, &gmPtr, 4, &r);
        if (r != 4 || gmPtr <= 0x1000) return 0;
        DWORD gm = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gmPtr + 0x14), &gm, 4, &r);
        if (r != 4 || gm <= 0x1000) return 0;
        DWORD cur = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gm + 0x1244), &cur, 4, &r);
        return (r == 4) ? cur : 0;
    }

    static void SendLocalEnter(HWND gw) {
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
