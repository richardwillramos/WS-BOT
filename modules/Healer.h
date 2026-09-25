#pragma once
#include "../include/IModule.h"
#include <string>
#include <cmath>

class HealerModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Healer"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastHealTick = 0; }
    void Stop() override  { lastHealTick = 0; }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastHealTick < (DWORD)cooldownMs) return;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // ---- 1) AUTO SELF-HEAL (prioridade) ----
        // Se meu HP % estiver abaixo do limite, cura em si mesmo.
        if (selfHealEnabled && ctx.selfMaxHp > 0 && ctx.selfHp > 0) {
            float selfPct = (float)ctx.selfHp / (float)ctx.selfMaxHp * 100.0f;
            if (selfPct <= selfHpPct) {
                if (selfHealKey == 0) return;
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
                lastHealTick = now;
                return;
            }
        }

        // ---- 2) HEAL NO ALVO ----
        if (targetAddr <= 0x1000 && targetName.empty()) return;

        // Acha o alvo na lista de players (por addr, com fallback por nome igual ao follow)
        float tx = 0, ty = 0;
        bool found = false;
        for (auto& p : ctx.players) {
            if (p.objAddr == targetAddr) {
                tx = p.x; ty = p.y;
                found = true;
                break;
            }
        }
        if (!found && !targetName.empty()) {
            for (auto& p : ctx.players) {
                if (p.name == targetName) {
                    targetAddr = p.objAddr;
                    tx = p.x; ty = p.y;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return;

        // If minHpFilter is ON, check target HP
        if (minHpFilter) {
            bool needsHeal = false;
            for (auto& p : ctx.players) {
                if (p.objAddr == targetAddr) {
                    float hpPct = p.maxHp > 0 ? (float)p.hp / p.maxHp * 100.0f : 100.0f;
                    if (hpPct <= minHpPct) needsHeal = true;
                    break;
                }
            }
            if (!needsHeal) return;
        }

        if (healKeyBind == 0) return;

        // Converte pos do alvo p/ tile (mesmo calculo do follow)
        WORD tileX = (WORD)((int)(tx / 24.0f));
        WORD tileY = (WORD)((int)(ty / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DebugLog("[HEAL] Healing '%S' at (%.1f,%.1f) tile(%d,%d) key=%c",
            targetName.c_str(), tx, ty, tileX, tileY, (char)healKeyBind);

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

        lastHealTick = now;
    }

    // Config
    bool  enabled = false;
    DWORD targetAddr = 0;
    std::wstring targetName;
    bool  minHpFilter = false;
    float minHpPct = 60.0f;
    int   cooldownMs = 2000;
    int   healKeyBind = 0x32; // '2' — heal no alvo
    // Auto self-heal (HP próprio)
    bool  selfHealEnabled = true;
    float selfHpPct = 50.0f;
    int   selfHealKey = 0x31; // '1' — cura própria / pot

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Healer", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Healer", L"MinHpFilter", L"0", buf, 256, path);
        minHpFilter = (buf[0] == L'1');
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
        WritePrivateProfileStringW(L"Healer", L"MinHpFilter", minHpFilter ? L"1" : L"0", path);
        wchar_t buf[32];
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
        CreateWindowExW(0, L"static", L"Min HP %:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18, parent, NULL, GetModuleHandle(NULL), NULL);
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
    HWND hChkEnabled = NULL, hEdtMinHp = NULL, hEdtCooldown = NULL, hEdtHealKey = NULL;
    HWND hChkSelfHeal = NULL, hEdtSelfHp = NULL, hEdtSelfKey = NULL;
    DWORD lastHealTick = 0;
};