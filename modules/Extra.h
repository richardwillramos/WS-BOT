#pragma once
#include "../include/IModule.h"
#include "../include/postkey.h"
#include <string>
#include <vector>

class ExtraModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Extra"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastTick = 0; }
    void Stop() override  { lastTick = 0; }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;

        // AUTO-BUFF: cursor no proprio char + tecla + Enter (mesmo padrao do
        // self-heal do healer). Rotacao LRU entre as teclas configuradas
        // (BuffKey="5,6,7" -> gira todas, uma por buffCooldown, como o Attacker).
        // Pula durante interacoes da dungeon para nao baguncar dialogo.
        if (buffEnabled && !ctx.dungeonBusy && !buffSkills.empty()) {
            int best = -1; DWORD bestAge = 0;
            for (size_t i = 0; i < buffSkills.size(); i++) {
                DWORD age = now - buffSkills[i].lastUsed;
                if (age < (DWORD)buffCooldownMs) continue;
                if (best < 0 || age > bestAge) { best = (int)i; bestAge = age; }
            }
            if (best >= 0) {
                HWND gw = ctx.gameWindow;
                if (gw && !IsIconic(gw)) {
                    DWORD curPtr = GetCursorPtr(ctx.hProcess);
                    if (curPtr > 0x1000) {
                        WriteCursorOnSelf(ctx.hProcess, curPtr, ctx.selfX, ctx.selfY);
                        SendBuffKey(gw, buffSkills[best].vk);
                        buffSkills[best].lastUsed = now;
                        DebugLog("[EXTRA] Auto buff (key %c)", (char)buffSkills[best].vk);
                    }
                }
            }
        }

        if (now - lastTick < 5000) return; // 5 second interval

        // Anti-AFK: tecla postada (nao afeta o teclado global)
        if (antiAfk) {
            PostGameKey(ctx.gameWindow, VK_SPACE, 50);
        }

        lastTick = now;
    }

    // Config
    bool  enabled = false;
    bool  antiAfk = true;
    bool  autoRevive = false;
    bool  autoSell = false;
    bool  autoRepair = false;
    bool  buffEnabled = false;
    int   buffCooldownMs = 10000;   // cooldown POR tecla de buff

    struct BuffSkill { int vk = 0; DWORD lastUsed = 0; };
    std::vector<BuffSkill> buffSkills;   // "5,6,7" -> rotacao LRU

    // "5, 6, 7" -> lista (qualquer contagem; ordem = ordem da rotacao)
    void SetBuffKeys(const wchar_t* csv) {
        buffSkills.clear();
        if (!csv) return;
        for (const wchar_t* p = csv; *p; p++) {
            wchar_t c = *p;
            if (c == L' ' || c == L'\t' || c == L',') continue;
            if (buffSkills.size() >= 12) break;
            bool dup = false;
            for (auto& b : buffSkills) if (b.vk == (int)c) { dup = true; break; }
            if (dup) continue;
            BuffSkill b; b.vk = (int)c;
            buffSkills.push_back(b);
        }
    }

    std::wstring BuffKeysCsv() const {
        std::wstring s;
        for (size_t i = 0; i < buffSkills.size(); i++) {
            if (i) s += L",";
            s += (wchar_t)buffSkills[i].vk;
        }
        return s;
    }

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Extra", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AntiAfk", L"1", buf, 256, path);
        antiAfk = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoRevive", L"0", buf, 256, path);
        autoRevive = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoSell", L"0", buf, 256, path);
        autoSell = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"AutoRepair", L"0", buf, 256, path);
        autoRepair = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"BuffEnabled", L"0", buf, 256, path);
        buffEnabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Extra", L"BuffKey", L"5", buf, 256, path);
        SetBuffKeys(buf);
        GetPrivateProfileStringW(L"Extra", L"BuffCooldown", L"10000", buf, 256, path);
        buffCooldownMs = _wtoi(buf);
        if (buffCooldownMs <= 0) buffCooldownMs = 10000;
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Extra", L"Enabled", enabled ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AntiAfk", antiAfk ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoRevive", autoRevive ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoSell", autoSell ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"AutoRepair", autoRepair ? L"1" : L"0", path);
        WritePrivateProfileStringW(L"Extra", L"BuffEnabled", buffEnabled ? L"1" : L"0", path);
        std::wstring bk = BuffKeysCsv();
        WritePrivateProfileStringW(L"Extra", L"BuffKey", bk.c_str(), path);
        wchar_t buf[16];
        swprintf_s(buf, L"%d", buffCooldownMs);
        WritePrivateProfileStringW(L"Extra", L"BuffCooldown", buf, path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9501, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAntiAfk = CreateWindowExW(0, L"button", L"Anti AFK",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9502, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoRevive = CreateWindowExW(0, L"button", L"Auto Revive",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9503, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoSell = CreateWindowExW(0, L"button", L"Auto Sell",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9504, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkAutoRepair = CreateWindowExW(0, L"button", L"Auto Repair",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9505, GetModuleHandle(NULL), NULL);

        y += 24;
        hChkBuff = CreateWindowExW(0, L"button", L"Auto Buff",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9506, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Buff key:", WS_CHILD|WS_VISIBLE, x, y+2, 70, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtBuffKey = CreateWindowExW(0, L"edit", L"5", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_UPPERCASE,
            x+75, y, 40, 22, parent, (HMENU)9507, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Buff cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 110, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtBuffCd = CreateWindowExW(0, L"edit", L"10000", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+115, y, 70, 22, parent, (HMENU)9508, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAntiAfk) SendMessage(hChkAntiAfk, BM_SETCHECK, antiAfk ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoRevive) SendMessage(hChkAutoRevive, BM_SETCHECK, autoRevive ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoSell) SendMessage(hChkAutoSell, BM_SETCHECK, autoSell ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkAutoRepair) SendMessage(hChkAutoRepair, BM_SETCHECK, autoRepair ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hChkBuff) SendMessage(hChkBuff, BM_SETCHECK, buffEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtBuffKey) SetWindowTextW(hEdtBuffKey, BuffKeysCsv().c_str());
        if (hEdtBuffCd) { wchar_t b[16]; swprintf_s(b, L"%d", buffCooldownMs); SetWindowTextW(hEdtBuffCd, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9501 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9502 && code == BN_CLICKED)
            antiAfk = (SendMessage(hChkAntiAfk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9503 && code == BN_CLICKED)
            autoRevive = (SendMessage(hChkAutoRevive, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9504 && code == BN_CLICKED)
            autoSell = (SendMessage(hChkAutoSell, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9505 && code == BN_CLICKED)
            autoRepair = (SendMessage(hChkAutoRepair, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9506 && code == BN_CLICKED)
            buffEnabled = (SendMessage(hChkBuff, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9507 && code == EN_CHANGE) {
            wchar_t b[64] = {}; GetWindowTextW(hEdtBuffKey, b, 64);
            if (b[0]) SetBuffKeys(b);
        }
        if (id == 9508 && code == EN_CHANGE) {
            wchar_t b[16]; GetWindowTextW(hEdtBuffCd, b, 16);
            int v = _wtoi(b);
            if (v > 0) buffCooldownMs = v;
        }
    }

private:
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hChkAntiAfk = NULL;
    HWND hChkAutoRevive = NULL, hChkAutoSell = NULL, hChkAutoRepair = NULL;
    HWND hChkBuff = NULL, hEdtBuffKey = NULL, hEdtBuffCd = NULL;
    DWORD lastTick = 0;

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

    // Cursor on own character (tile + raw, NO walk flag)
    static void WriteCursorOnSelf(HANDLE hProc, DWORD curPtr, float selfX, float selfY) {
        WORD tileX = (WORD)((int)(selfX / 24.0f));
        WORD tileY = (WORD)((int)(selfY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;
        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
        WriteProcessMemory(hProc, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
    }

    // key + Enter via PostMessage (mesmo padrao do healer; sem foreground)
    static void SendBuffKey(HWND gw, int vk) {
        PostGameKey(gw, (UINT)vk, 50);
        Sleep(30);
        PostGameEnter(gw, 80);
    }
};
