#pragma once
#include "../include/IModule.h"
#include <string>
#include <vector>
#include <cmath>

class LooterModule : public IModule {
public:
    const wchar_t* GetName() const override { return L"Looter"; }
    bool IsEnabled() const override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }

    void Start() override { lastLootTick = 0; lootingState = 0; clickAttempts = 0; lastCX = lastCY = 0; seenMobs.clear(); vanished.clear(); }
    void Stop() override  { lastLootTick = 0; lootingState = 0; clickAttempts = 0; lastCX = lastCY = 0; seenMobs.clear(); vanished.clear(); }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;
        if (now - lastLootTick < (DWORD)cooldownMs) return;

        HWND gw = ctx.gameWindow;
        if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;

        // PAUSA por prioridade: cura > loot > ataque; dungeon manda no loot
        // (dungeonNoLoot = waves sem drop; dungeonBusy = interagindo com portal/bau)
        if (ctx.holdCombat || ctx.dungeonBusy || ctx.dungeonNoLoot) return;

        // Find corpse to loot (STANDALONE — não depende do Attacker):
        // 1) corpses da tree (hp<0)
        // 2) mobs com hp<=0 (kills de terceiros — ex: você só curando no suporte)
        // 3) pending corpse do attacker (quando você matou)
        // Pega o mais próximo dentro de lootMaxDistance.
        std::wstring corpseName;
        float corpseX = 0, corpseY = 0;
        bool hasCorpse = false;
        float bestDist = 1e9f;

        for (auto& c : ctx.corpses) {
            float dx = c.x - ctx.selfX;
            float dy = c.y - ctx.selfY;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < bestDist) { bestDist = d; corpseName = c.name; corpseX = c.x; corpseY = c.y; hasCorpse = true; }
        }
        for (auto& m : ctx.mobs) {
            if (m.hp > 0) continue;
            if (m.maxHp <= 0) continue;
            float dx = m.x - ctx.selfX;
            float dy = m.y - ctx.selfY;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < bestDist) { bestDist = d; corpseName = m.name; corpseX = m.x; corpseY = m.y; hasCorpse = true; }
        }
        if (ctx.pendingCorpse && ctx.pendingCorpse->valid) {
            if (now - ctx.pendingCorpse->time > 30000) {
                ctx.pendingCorpse->valid = false;
                DebugLog("[LOOT] Pending corpse expired");
            } else {
                float dx = ctx.pendingCorpse->x - ctx.selfX;
                float dy = ctx.pendingCorpse->y - ctx.selfY;
                float d = sqrtf(dx*dx + dy*dy);
                if (!hasCorpse || d < bestDist) {
                    bestDist = d;
                    corpseName = ctx.pendingCorpse->name;
                    corpseX = ctx.pendingCorpse->x;
                    corpseY = ctx.pendingCorpse->y;
                    hasCorpse = true;
                }
            }
        }

        // ---- 4) mobs que SUMIRAM da lista (servidor remove morto na hora) ----
        // Heurística de kill de terceiros: visto em >=2 ticks seguidos,
        // estava perto (<= lootMaxDistance) e estava danificado (hp < maxHp).
        // Mob que só passou andando some com HP cheio e é ignorado.
        for (auto it = seenMobs.begin(); it != seenMobs.end(); ) {
            bool present = false;
            for (auto& m : ctx.mobs) if (m.objAddr == it->addr) { present = true; break; }
            if (present) { ++it; continue; }
            // Sumiu neste tick
            if (it->seenCount >= 2 && it->dist <= lootMaxDistance &&
                it->hp >= 0 && it->maxHp > 0 && it->hp < it->maxHp) {
                bool dup = false;
                for (auto& v : vanished) {
                    float ddx = v.x - it->x, ddy = v.y - it->y;
                    if (sqrtf(ddx*ddx + ddy*ddy) < 3.0f && now - v.time < 30000) { dup = true; break; }
                }
                if (!dup) {
                    VanishedCorpse v; v.name = it->name; v.x = it->x; v.y = it->y; v.time = now;
                    vanished.push_back(v);
                    DebugLog("[LOOT] Mob vanished, possible kill '%S' at (%.1f,%.1f) — will check", it->name.c_str(), it->x, it->y);
                }
            }
            it = seenMobs.erase(it);
        }
        // Atualiza vistos com a lista atual
        for (auto& m : ctx.mobs) {
            bool known = false;
            for (auto& s : seenMobs) {
                if (s.addr == m.objAddr) {
                    s.x = m.x; s.y = m.y; s.hp = m.hp; s.maxHp = m.maxHp;
                    s.dist = sqrtf((m.x - ctx.selfX)*(m.x - ctx.selfX) + (m.y - ctx.selfY)*(m.y - ctx.selfY));
                    s.seenCount++;
                    known = true;
                    break;
                }
            }
            if (!known) {
                SeenMob s; s.addr = m.objAddr; s.name = m.name; s.x = m.x; s.y = m.y;
                s.hp = m.hp; s.maxHp = m.maxHp;
                s.dist = sqrtf((m.x - ctx.selfX)*(m.x - ctx.selfX) + (m.y - ctx.selfY)*(m.y - ctx.selfY));
                s.seenCount = 1;
                seenMobs.push_back(s);
            }
        }
        // Expira vanished com +30s
        for (auto it = vanished.begin(); it != vanished.end(); ) {
            if (now - it->time > 30000) it = vanished.erase(it);
            else ++it;
        }
        for (auto& v : vanished) {
            float dx = v.x - ctx.selfX;
            float dy = v.y - ctx.selfY;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < bestDist) { bestDist = d; corpseName = v.name; corpseX = v.x; corpseY = v.y; hasCorpse = true; }
        }

        if (!hasCorpse) { lootingState = 0; return; }

        // Ignora corpse longe demais (evita atravessar o mapa)
        if (bestDist > lootMaxDistance) {
            if (now - lastFarLogTick > 10000) {
                lastFarLogTick = now;
                DebugLog("[LOOT] Nearest corpse '%S' too far (%.1f > %.0f), waiting", corpseName.c_str(), bestDist, lootMaxDistance);
            }
            return;
        }

        float dx = corpseX - ctx.selfX;
        float dy = corpseY - ctx.selfY;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist > walkRadius) {
            // Too far — walk toward corpse first
            DebugLog("[LOOT] Walking toward '%S' dist=%.1f", corpseName.c_str(), dist);
            WalkToPosition(ctx, gw, corpseX, corpseY);
        } else {
            // Close enough — click the corpse. One blind click often misses
            // (corpse shifted, scale off), so retry up to 3 clicks while the
            // corpse is valid instead of invalidating after the first one.
            if (corpseX != lastCX || corpseY != lastCY) { clickAttempts = 0; lastCX = corpseX; lastCY = corpseY; }
            if (clickAttempts >= 3) {
                // Already tried 3 times — give up on this corpse so we never
                // stall the bot, but only now.
                lootCount++;
                DebugLog("[LOOT] Gave up on '%S' after 3 clicks | Total=%d", corpseName.c_str(), lootCount);
                if (ctx.pendingCorpse) ctx.pendingCorpse->valid = false;
                for (auto it = vanished.begin(); it != vanished.end(); ) {
                    float ddx = it->x - corpseX, ddy = it->y - corpseY;
                    if (sqrtf(ddx*ddx + ddy*ddy) < 3.0f) it = vanished.erase(it);
                    else ++it;
                }
                clickAttempts = 0;
                lastLootTick = now;
                return;
            }
            int cx, cy;
            if (GameToClient(ctx, corpseX, corpseY, cx, cy)) {
                DebugLog("[LOOT] Click corpse '%S' (%d/3) -> client(%d,%d) dist=%.1f", corpseName.c_str(), clickAttempts + 1, cx, cy, dist);
                ClickAtClient(gw, cx, cy);
                Sleep(400);
                SendLocalEnter(gw);
                Sleep(300);
                clickAttempts++;
                lastLootTick = now;
            }
        }
    }

    // Config
    bool  enabled = false;
    float walkRadius = 10.0f;   // walk until within this distance, then click
    float lootMaxDistance = 25.0f; // ignora corpse além disso (standalone)
    int   cooldownMs = 1200;
    int   lootCount = 0;

    void LoadConfig(const wchar_t* path) override {
        wchar_t buf[256];
        GetPrivateProfileStringW(L"Looter", L"Enabled", L"0", buf, 256, path);
        enabled = (buf[0] == L'1');
        GetPrivateProfileStringW(L"Looter", L"WalkRadius", L"10", buf, 256, path);
        walkRadius = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Looter", L"MaxDistance", L"25", buf, 256, path);
        lootMaxDistance = (float)_wtof(buf);
        GetPrivateProfileStringW(L"Looter", L"Cooldown", L"1200", buf, 256, path);
        cooldownMs = _wtoi(buf);
    }

    void SaveConfig(const wchar_t* path) const override {
        WritePrivateProfileStringW(L"Looter", L"Enabled", enabled ? L"1" : L"0", path);
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", walkRadius);
        WritePrivateProfileStringW(L"Looter", L"WalkRadius", buf, path);
        swprintf_s(buf, L"%.0f", lootMaxDistance);
        WritePrivateProfileStringW(L"Looter", L"MaxDistance", buf, path);
        swprintf_s(buf, L"%d", cooldownMs);
        WritePrivateProfileStringW(L"Looter", L"Cooldown", buf, path);
    }

    bool HasUI() const override { return true; }

    void CreateUI(HWND parent, int x, int y, int w) override {
        hParent = parent;
        hChkEnabled = CreateWindowExW(0, L"button", L"Enabled",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, x, y, 200, 20,
            parent, (HMENU)9301, GetModuleHandle(NULL), NULL);

        y += 24;
        CreateWindowExW(0, L"static", L"Walk Radius:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtRadius = CreateWindowExW(0, L"edit", L"10", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9302, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Cooldown (ms):", WS_CHILD|WS_VISIBLE, x, y+2, 90, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtCooldown = CreateWindowExW(0, L"edit", L"1200", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+95, y, 60, 22, parent, (HMENU)9303, GetModuleHandle(NULL), NULL);

        y += 28;
        CreateWindowExW(0, L"static", L"Max Dist:", WS_CHILD|WS_VISIBLE, x, y+2, 80, 18,
            parent, NULL, GetModuleHandle(NULL), NULL);
        hEdtMaxDist = CreateWindowExW(0, L"edit", L"25", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            x+85, y, 50, 22, parent, (HMENU)9304, GetModuleHandle(NULL), NULL);

        UpdateUI();
    }

    void UpdateUI() override {
        if (hChkEnabled) SendMessage(hChkEnabled, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (hEdtRadius) { wchar_t b[32]; swprintf_s(b, L"%.0f", walkRadius); SetWindowTextW(hEdtRadius, b); }
        if (hEdtCooldown) { wchar_t b[32]; swprintf_s(b, L"%d", cooldownMs); SetWindowTextW(hEdtCooldown, b); }
        if (hEdtMaxDist) { wchar_t b[32]; swprintf_s(b, L"%.0f", lootMaxDistance); SetWindowTextW(hEdtMaxDist, b); }
    }

    void OnCommand(int id, int code) override {
        if (id == 9301 && code == BN_CLICKED)
            enabled = (SendMessage(hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (id == 9302 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtRadius, b, 32); walkRadius = (float)_wtof(b);
        }
        if (id == 9303 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtCooldown, b, 32); cooldownMs = _wtoi(b);
        }
        if (id == 9304 && code == EN_CHANGE) {
            wchar_t b[32]; GetWindowTextW(hEdtMaxDist, b, 32); lootMaxDistance = (float)_wtof(b);
        }
    }

private:
    struct SeenMob { DWORD addr = 0; std::wstring name; float x = 0, y = 0; int hp = 0, maxHp = 0; float dist = 0; int seenCount = 0; };
    struct VanishedCorpse { std::wstring name; float x = 0, y = 0; DWORD time = 0; };
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hEdtRadius = NULL, hEdtCooldown = NULL, hEdtMaxDist = NULL;
    DWORD lastLootTick = 0;
    DWORD lastFarLogTick = 0;
    DWORD lootingState = 0;
    int clickAttempts = 0;
    float lastCX = 0, lastCY = 0;
    std::vector<SeenMob> seenMobs;
    std::vector<VanishedCorpse> vanished;

    // Convert game coordinates to client-area screen coordinates
    // Warspear 2D top-down: player always centered, scale = pixels per game unit
    static bool GameToClient(const GameContext& ctx, float gx, float gy, int& cx, int& cy) {
        HWND w = ctx.gameWindow;
        if (!w) return false;
        RECT rc; GetClientRect(w, &rc);
        int midX = (rc.right - rc.left) / 2;
        int midY = (rc.bottom - rc.top) / 2;

        float dx = gx - ctx.selfX;
        float dy = gy - ctx.selfY;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.5f) { cx = midX; cy = midY; return true; }

        cx = midX + (int)(dx * ctx.scale);
        cy = midY + (int)(dy * ctx.scale);

        if (cx < 5) cx = 5; if (cx > rc.right - 5) cx = rc.right - 5;
        if (cy < 5) cy = 5; if (cy > rc.bottom - 5) cy = rc.bottom - 5;
        return true;
    }

    // Physical mouse click at client coordinates
    static void ClickAtClient(HWND gw, int cx, int cy) {
        if (!gw) return;
        if (GetForegroundWindow() != gw || IsIconic(gw)) return;

        // Force foreground
        DWORD fgTid = GetWindowThreadProcessId(gw, NULL);
        DWORD myTid = GetCurrentThreadId();
        AttachThreadInput(myTid, fgTid, TRUE);
        SetForegroundWindow(gw);
        AttachThreadInput(myTid, fgTid, FALSE);
        if (GetForegroundWindow() != gw) return;

        // Convert client to screen coords and click
        POINT pt = {cx, cy};
        ClientToScreen(gw, &pt);
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        if (pt.x < 0 || pt.x >= sx || pt.y < 0 || pt.y >= sy) return;

        INPUT in[3] = {};
        in[0].type = INPUT_MOUSE;
        in[0].mi.dx = (LONG)(pt.x * 65536.0 / sx);
        in[0].mi.dy = (LONG)(pt.y * 65536.0 / sy);
        in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[2].type = INPUT_MOUSE; in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(3, in, sizeof(INPUT));
    }

    // Walk toward position using cursor memory + walk flag + Enter
    void WalkToPosition(const GameContext& ctx, HWND gw, float gameX, float gameY) {
        extern void DebugLog(const char* fmt, ...);

        WORD tileX = (WORD)((int)(gameX / 24.0f));
        WORD tileY = (WORD)((int)(gameY / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

        DWORD curPtr = GetCursorPtr(ctx.hProcess);
        if (!curPtr) return;

        int rawX = (int)tileX * 0x180000;
        int rawY = (int)tileY * 0x180000;
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x08), &tileX, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x0A), &tileY, 2, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x10), &rawX, 4, NULL);
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x14), &rawY, 4, NULL);
        DWORD walkFlag = 0x10;
        WriteProcessMemory(ctx.hProcess, (LPVOID)(curPtr + 0x7C), &walkFlag, 4, NULL);

        DebugLog("[LOOT] Walk -> tile(%d,%d)", tileX, tileY);
        SendLocalEnter(gw);
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
};
