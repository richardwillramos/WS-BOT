#pragma once
#include "../include/IModule.h"
#include <string>
#include <cmath>

class HealerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Healer"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastSelfHealTick = 0; lastTargetHealTick = 0; }
    void Stop() override  { lastSelfHealTick = 0; lastTargetHealTick = 0; }

    static constexpr int HEAL_MODE_COOLDOWN = 0; // cura a cada N ms (cooldown)
    static constexpr int HEAL_MODE_HP_BELOW = 1; // cura quando HP% <= limite

    // True while someone needs a heal RIGHT NOW (HP below the configured %).
    // main.cpp uses this to pause attacker/looter (heal > loot > attack).
    bool NeedsHeal(const GameContext& ctx) const {
        if (!enabled) return false;
        // Self: always threshold-based (priority 1)
        if (selfHealEnabled && selfHealKey != 0 && ctx.selfMaxHp > 0 && ctx.selfHp > 0) {
            float selfPct = (float)ctx.selfHp / (float)ctx.selfMaxHp * 100.0f;
            if (selfPct <= selfHpPct) return true;
        }
        // Target: only in HP% mode — the periodic mode is not urgent, so it
        // must never hold combat
        if (healMode != HEAL_MODE_HP_BELOW) return false;
        if (healKeyBind == 0) return false;
        if (targetAddr <= 0x1000 && targetName.empty()) return false;
        for (auto& p : ctx.players) {
            bool match = (targetAddr > 0x1000 && p.objAddr == targetAddr) ||
                         (!targetName.empty() && p.name == targetName);
            if (!match) continue;
            if (p.hp <= 0 || p.maxHp <= 0) return false;  // dead ally never holds combat
            return (float)p.hp / (float)p.maxHp * 100.0f <= minHpPct;
        }
        return false;
    }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // ---- 1) AUTO SELF-HEAL (prioridade 1, sempre por %) ----
        // Cooldown proprio: uma cura em si mesmo NAO atrasa a cura do alvo.
        if (selfHealEnabled && selfHealKey != 0 && ctx.selfMaxHp > 0 && ctx.selfHp > 0) {
            float selfPct = (float)ctx.selfHp / (float)ctx.selfMaxHp * 100.0f;
            if (selfPct <= selfHpPct && now - lastSelfHealTick >= (DWORD)cooldownMs) {
                // Posiciona cursor em si mesmo (p/ skills de area funcionarem),
                // aperta a tecla e confirma com Enter — igual ao follow.
                WORD tileX = (WORD)((int)(ctx.selfX / 24.0f));
                WORD tileY = (WORD)((int)(ctx.selfY / 24.0f));
                if (tileX > 27) tileX = 27;
                if (tileY > 27) tileY = 27;

                DebugLog("[HEAL] Self heal HP %d/%d (%.0f%% <= %.0f%%) key=%c",
                    ctx.selfHp, ctx.selfMaxHp, selfPct, selfHpPct, (char)selfHealKey);

                DWORD curPtr = GetCursorPtr(ctx.hProcess);
                if (curPtr > 0x1000) {
                    int rawX = (int)tileX * 0x180000;
                    int rawY = (int)tileY * 0x180000;
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
                    WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
                }
                SendGameKey(gw, (WORD)selfHealKey);
                Sleep(80);
                SendLocalEnter(gw);
                lastSelfHealTick = now;
            }
        }

        // ---- 2) HEAL NO ALVO (prioridade 1 junto com o proprio) ----
        if (targetAddr <= 0x1000 && targetName.empty()) return;
        if (healKeyBind == 0) return;

        // Acha o alvo na lista de players (por addr, com fallback por nome igual ao follow)
        float tx = 0, ty = 0, hpPct = 100.0f;
        bool found = false, dead = false;
        for (auto& p : ctx.players) {
            if (p.objAddr == targetAddr) {
                tx = p.x; ty = p.y; found = true;
                dead = (p.hp <= 0 || p.maxHp <= 0);
                if (!dead) hpPct = (float)p.hp / (float)p.maxHp * 100.0f;
                break;
            }
        }
        if (!found && !targetName.empty()) {
            for (auto& p : ctx.players) {
                if (p.name == targetName) {
                    targetAddr = p.objAddr;
                    tx = p.x; ty = p.y; found = true;
                    dead = (p.hp <= 0 || p.maxHp <= 0);
                    if (!dead) hpPct = (float)p.hp / (float)p.maxHp * 100.0f;
                    break;
                }
            }
        }
        if (!found) return;
        if (dead) return;  // alvo morto: nao desperdicia cast

        // Filtro de cura (igual ao filtro do attacker/targeter):
        //   HP_BELOW  -> so cura quando HP% <= MinHpPct (mantem sempre acima)
        //   COOLDOWN  -> cura a cada N ms do Cooldown, ignorando HP%
        bool need = (healMode == HEAL_MODE_HP_BELOW) ? (hpPct <= minHpPct) : true;
        if (!need) return;
        if (now - lastTargetHealTick < (DWORD)cooldownMs) return;

        // Converte pos do alvo p/ tile (mesmo calculo do follow)
        WORD tileX = (WORD)((int)(tx / 24.0f));
        WORD tileY = (WORD)((int)(ty / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DebugLog("[HEAL] Healing '%S' HP=%.0f%% at (%.1f,%.1f) tile(%d,%d) key=%c",
            targetName.c_str(), hpPct, tx, ty, tileX, tileY, (char)healKeyBind);

        // 1. Posiciona o cursor na posicao do alvo (igual ao follow)
        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (curPtr > 0x1000) {
            int rawX = (int)tileX * 0x180000;
            int rawY = (int)tileY * 0x180000;
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
            WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
        }

        // 2. Aperta a tecla de heal (default '2')
        SendGameKey(gw, (WORD)healKeyBind);
        Sleep(80);

        // 3. Clica/confirma na posicao (Enter = click do jogo, igual follow/attacker)
        SendLocalEnter(gw);

        lastTargetHealTick = now;
    }

    // Config
    bool  enabled = false;
    DWORD targetAddr = 0;
    std::wstring targetName;
    // Heal mode filter (like the targeter filter): HEAL_MODE_* above
    int   healMode = HEAL_MODE_HP_BELOW;
    float minHpPct = 60.0f;
    int   cooldownMs = 2000;
    int   healKeyBind = 0x32; // '2' — heal no alvo
    // Auto self-heal (HP proprio)
    bool  selfHealEnabled = true;
    float selfHpPct = 50.0f;
    int   selfHealKey = 0x31; // '1' — cura própria / pot

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Healer", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        // HealMode novo; MinHpFilter antigo vira fallback (1 = HP%, 0 = cooldown)
        GetPrivateProfileStringW(L"Healer", L"HealMode", L"", buf, 256, path);
        if (buf[0]) healMode = _wtoi(buf) ? HEAL_MODE_HP_BELOW : HEAL_MODE_COOLDOWN;
        else {
            GetPrivateProfileStringW(L"Healer", L"MinHpFilter", L"1", buf, 256, path);
            healMode = (buf[0] == L'1') ? HEAL_MODE_HP_BELOW : HEAL_MODE_COOLDOWN;
        }
        GetPrivateProfileStringW(L"Healer", L"MinHpPct", L"60", buf, 256, path);
        minHpPct = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Healer", L"Cooldown", L"2000", buf, 256, path);
        cooldownMs = _wtoi(buf);
        GetPrivateProfileStringW(L"Healer", L"HealKey", L"2", buf, 256, path);
        healKeyBind = buf[0] ? (buf[0] >= L'0' && buf[0] <= L'9' ? buf[0] : 0x32) : 0x32;
        GetPrivateProfileStringW(L"Healer", L"SelfHeal", L"1", buf, 256, path);
        selfHealEnabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Healer", L"SelfHpPct", L"50", buf, 256, path);
        selfHpPct = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Healer", L"SelfHealKey", L"1", buf, 256, path);
        selfHealKey = buf[0] ? buf[0] : 0x31;
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Healer", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%d", healMode);
        WritePrivateProfileStringW(L"Healer", L"HealMode", buf, path);
        // Mantido por compatibilidade com configs antigos
        WritePrivateProfileStringW(L"Healer", L"MinHpFilter", healMode == HEAL_MODE_HP_BELOW ? L"1" : L"0", path);
        swprintf_s(buf, L"%.0f", minHpPct);
        WritePrivateProfileStringW(L"Healer", L"MinHpPct", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Healer", L"Cooldown", buf, path);
        swprintf_s(buf, L"%c", healKeyBind);
        WritePrivateProfileStringW(L"Healer", L"HealKey", buf, path);
        WritePrivateProfileStringW(L"Healer", L"SelfHeal", selfHealEnabled ? L"1" : L"0", path);
        swprintf_s(buf, L"%.0f", selfHpPct);
        WritePrivateProfileStringW(L"Healer", L"SelfHpPct", buf, path);
        swprintf_s(buf, L"%c", selfHealKey);
        WritePrivateProfileStringW(L"Healer", L"SelfHealKey", buf, path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9101, GetModuleHandle(NULL), NULL);

        y += 24;
        hLblMinHp = CreateWindowExW(0, L"static", L"Min HP %:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMinHp = CreateWindowExW(0, L"edit", L"60", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+75, y, 50, 22, parent, (HMENU)9102, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"2000", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9103, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Heal Key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtHealKey = CreateWindowExW(0, L"edit", L"2", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+75, y, 40, 22, parent, (HMENU)9104, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Heal Mode:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hCboMode = CreateWindowExW(0, L"combobox", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, x+75, y, 190, 100,
            parent, (HMENU)9108, GetModuleHandle(NULL), NULL);
        SendMessageW(hCboMode, CB_ADDSTRING, 0, (LPARAM)L"Every cooldown (N sec)");
        SendMessageW(hCboMode, CB_ADDSTRING, 0, (LPARAM)L"When HP% below");

        y += 28;
        hChkSelfHeal = CreateWindowExW(0, L"button", L"Self Heal (HP proprio)",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9105, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Self HP %:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtSelfHp = CreateWindowExW(0, L"edit", L"50", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+75, y, 50, 22, parent, (HMENU)9106, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Self Key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtSelfKey = CreateWindowExW(0, L"edit", L"1", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            x+75, y, 40, 22, parent, (HMENU)9107, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtMinHp) { wchar_t b[32]; swprintf_s(b, L"%.0f", minHpPct); SetWindowTextW(hEdtMinHp, b); }
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
        if (hEdtHealKey) { wchar_t b[4] = {(wchar_t)healKeyBind, 0}; SetWindowTextW(hEdtHealKey, b); }
        if (hCboMode) SendMessageW(hCboMode, CB_SETCURSEL, healMode, 0);
        // Min HP% so faz sentido no modo HP%
        bool hpMode = (healMode == HEAL_MODE_HP_BELOW);
        if (hLblMinHp) ShowWindow(hLblMinHp, hpMode ? SW_SHOW : SW_HIDE);
        if (hEdtMinHp) ShowWindow(hEdtMinHp, hpMode ? SW_SHOW : SW_HIDE);
        if (hChkSelfHeal) SendMessage(hChkSelfHeal, BM_SETCHECK, selfHealEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtSelfHp) { wchar_t b[32]; swprintf_s(b, L"%.0f", selfHpPct); SetWindowTextW(hEdtSelfHp, b); }
        if (hEdtSelfKey) { wchar_t b[4] = {(wchar_t)selfHealKey, 0}; SetWindowTextW(hEdtSelfKey, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9101 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9102 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMinHp, b, 32); minHpPct = (float)_wtof(b);
        }
        if (id == 9103 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
        if (id == 9104 && code == EN_CHANGE) {
            wchar_t b[4]; GetWindowTextW(hEdtHealKey, b, 4); healKeyBind = b[0] ? b[0] : 0x32;
        }
        if (id == 9108 && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(hCboMode, CB_GETCURSEL, 0, 0);
            healMode = (sel == HEAL_MODE_COOLDOWN) ? HEAL_MODE_COOLDOWN : HEAL_MODE_HP_BELOW;
            UpdateUI();
        }
        if (id == 9105 && code == BN_CLICKED)
            selfHealEnabled = (SendMessage(hChkSelfHeal, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9106 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtSelfHp, b, 32); selfHpPct = (float)_wtof(b);
        }
        if (id == 9107 && code == EN_CHANGE) {
            wchar_t b[4]; GetWindowTextW(hEdtSelfKey, b, 4); selfHealKey = b[0] ? b[0] : 0x31;
        }
    }

private:
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

    static void SendGameKey(HWND gw, WORD vk) {
        if (!gw) return;
        DWORD fgTid = GetWindowThreadProcessId(gw, NULL);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(gw);
        AttachThreadInput(myTid, fgTid, FALSE);
        keybd_event((BYTE)vk, 0, 0, 0);
        Sleep(30);
        keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
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
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hLblMinHp = NULL, hEdtMinHp = NULL, hEdtCooldown = NULL, hEdtHealKey = NULL;
    HWND hCboMode = NULL;
    HWND hChkSelfHeal = NULL, hEdtSelfHp = NULL, hEdtSelfKey = NULL;
    DWORD lastSelfHealTick = 0;
    DWORD lastTargetHealTick = 0;
};
