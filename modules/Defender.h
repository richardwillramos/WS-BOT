#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <cmath>

// ============================================================
// Defender - contra-ataca o mob que esta batendo em alguem da party.
// A party = os mesmos alvos do Healer (ctx.partyAddrs, preenchido no
// main.cpp) + o proprio jogador (opcional).
// O campo ENT_ATTACK_TARGET (+0x294) de CADA entidade guarda o endereco
// da entidade que ela esta atacando — verificado ao vivo: a Jaguatirica
// (0x0E591F88, vt 0x00CD17B4) tinha +0x294 = jogador local enquanto o
// mordia.
// Prioridade: main.cpp escreve attacker.targetAddr = defender.threatAddr
// ANTES do alvo do Targeter, entao o ataque normal so roda quando nao
// tem ameaca.
// ============================================================
class DefenderModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Defender"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { threatAddr = 0; threatName.clear(); }
    void Stop() override  { threatAddr = 0; threatName.clear(); }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) { threatAddr = 0; threatName.clear(); return; }

        // Cura (holdCombat) e dungeon (dungeonBusy) mandam no combate
        if (ctx.holdCombat || ctx.dungeonBusy) return;

        // Sem ninguem pra proteger nao faz nada (o Targeter segue normal)
        if (ctx.partyAddrs.empty() && !includeSelf) { threatAddr = 0; threatName.clear(); return; }

        float maxDistGame = (float)maxDistTiles * 24.0f;
        float maxDist2 = maxDistGame * maxDistGame;

        // Mob mais perto que esta batendo em alguem da party
        DWORD best = 0;
        std::wstring bestName;
        float bestD2 = 0;
        for (const auto& m : ctx.mobs) {
            if (m.hp <= 0 || m.maxHp <= 0) continue;
            DWORD victim = ReadInt(ctx.hProcess, m.objAddr + ENT_ATTACK_TARGET);
            if (!IsProtected(ctx, victim)) continue;
            float dx = m.x - ctx.selfX, dy = m.y - ctx.selfY;
            float d2 = dx * dx + dy * dy;
            if (maxDistTiles > 0 && d2 > maxDist2) continue;   // longe demais de mim
            if (best == 0 || d2 < bestD2) { bestD2 = d2; best = m.objAddr; bestName = m.name; }
        }

        if (best != threatAddr) {
            if (best) {
                DebugLog("[DEFEND] '%S' (0x%08X) batendo em 0x%08X - contra-atacando",
                         bestName.c_str(), best, ReadInt(ctx.hProcess, best + ENT_ATTACK_TARGET));
            } else if (threatAddr) {
                DebugLog("[DEFEND] ameaca acabou (era '%S')", threatName.c_str());
            }
            threatAddr = best;
            threatName = bestName;
        }
    }

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"Defender", L"Enabled", L"0", buf, 64, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Defender", L"MaxDistTiles", L"10", buf, 64, path);
        maxDistTiles = _wtoi(buf);
        GetPrivateProfileStringW(L"Defender", L"IncludeSelf", L"1", buf, 64, path);
        includeSelf = (buf[0] == L'1');
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Defender", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%d", maxDistTiles);
        WritePrivateProfileStringW(L"Defender", L"MaxDistTiles", buf, path);
        WritePrivateProfileStringW(L"Defender", L"IncludeSelf", includeSelf ? L"1" : L"0", path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 220, 20,
            parent, (HMENU)9201, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Max dist (tiles, 0=all):", WS_CHILD|WS_VISIBLE,
            x, y+2, 140, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtDist = CreateWindowExW(0, L"edit", L"10", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+145, y, 50, 22, parent, (HMENU)9202, GetModuleHandle(NULL), NULL);

        y += 28;
        hChkSelf = CreateWindowExW(0, L"button", L"Also defend myself",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 220, 20,
            parent, (HMENU)9203, GetModuleHandle(NULL), NULL);

        y += 30;
        CreateWindowExW(0, L"static",
            L"Party = the same targets configured in the Healer.\n"
            L"Attacks the mob that is attacking them (priority over Targeter).",
            WS_CHILD|WS_VISIBLE, x, y, 360, 40, parent, NULL, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkSelf)    SendMessage(hChkSelf,    BM_SETCHECK, includeSelf ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtDist)    { wchar_t b[32]; swprintf_s(b, L"%d", maxDistTiles); SetWindowTextW(hEdtDist, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9201 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9202 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtDist, b, 32); maxDistTiles = _wtoi(b);
        }
        if (id == 9203 && code == BN_CLICKED)
            includeSelf = (SendMessage(hChkSelf, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    // ---- exposto pro main.cpp (prioridade no Attacker) ----
    DWORD threatAddr = 0;
    std::wstring threatName;

    // Config
    bool enabled = false;
    int  maxDistTiles = 10;   // tiles ate mim; 0 = sem limite
    bool includeSelf = true;

private:
    // Endereco da entidade que esta entidade esta atacando (verificado ao vivo
    // na build 10432112 B: Jaguatirica+0x294 = jogador local enquanto mordia)
    static constexpr DWORD ENT_ATTACK_TARGET = 0x294;

    static int ReadInt(HANDLE hProc, DWORD addr) {
        int v = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)addr, &v, 4, &r);
        return (r == 4) ? v : 0;
    }

    bool IsProtected(const GameContext& ctx, DWORD victim) const {
        if (victim <= 0x1000) return false;
        if (includeSelf && victim == ctx.playerAddr) return true;
        for (DWORD a : ctx.partyAddrs) if (a == victim) return true;
        return false;
    }

    HWND hChkEnabled = NULL, hChkSelf = NULL, hEdtDist = NULL;
};
