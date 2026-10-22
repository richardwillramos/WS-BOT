#pragma once
#include "../include/IModule.h"
#include "../include/postkey.h"
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

    // Alvos de cura (lista: da pra marcar varios jogadores)
    struct HealTarget { DWORD addr = 0; std::wstring name; };
    std::vector<HealTarget> targets;

    bool HasTarget(DWORD addr, const std::wstring& name) const {
        for (const auto& t : targets)
            if ((addr > 0x1000 && t.addr == addr) || (!name.empty() && t.name == name)) return true;
        return false;
    }
    void ToggleTarget(DWORD addr, const std::wstring& name) {
        for (size_t i = 0; i < targets.size(); i++) {
            bool same = (addr > 0x1000 && targets[i].addr == addr) ||
                        (!name.empty() && !targets[i].name.empty() && targets[i].name == name);
            if (same) { targets.erase(targets.begin() + i); return; }
        }
        targets.push_back({ addr, name });
    }
    void ClearTargets() { targets.clear(); }

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
        if (healKeyBind == 0 || targets.empty()) return false;
        for (const auto& t : targets) {
            const GameContext::EntityInfo* p = FindEntity(ctx, t);
            if (!p || p->hp <= 0 || p->maxHp <= 0) continue;  // dead ally never holds combat
            if ((float)p->hp / (float)p->maxHp * 100.0f <= minHpPct) return true;
        }
        return false;
    }

    void Tick(const GameContext& ctx) override {
        extern void DebugLog(const char* fmt, ...);
        if (!enabled || ctx.hProcess == NULL) return;

        DWORD now = ctx.tickCount;

        HWND gw = ctx.gameWindow;
        if (!gw || IsIconic(gw)) return;

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
                // Com 2+ seres no tile o jogo abre "Selecione o alvo" e o Enter
                // confirmaria a PRIMEIRA linha (pode nao ser voce).
                PickListEntry(ctx, tileX, tileY, ctx.playerAddr);
                SendLocalEnter(gw);
                lastSelfHealTick = now;
            }
        }

        // ---- 2) HEAL NOS ALVOS (vários, prioridade 1 junto com o proprio) ----
        if (targets.empty()) return;
        if (healKeyBind == 0) return;

        // Escolhe o alvo configurado de MENOR HP% (ignora mortos: nao gasta cast).
        //   HP_BELOW  -> so considera quem esta <= MinHpPct (mantem sempre acima)
        //   COOLDOWN  -> cura a cada N ms do Cooldown, ignorando HP%
        const GameContext::EntityInfo* best = nullptr;
        float bestPct = 100.0f;
        for (const auto& t : targets) {
            const GameContext::EntityInfo* p = FindEntity(ctx, t);
            if (!p || p->hp <= 0 || p->maxHp <= 0) continue;
            float pct = (float)p->hp / (float)p->maxHp * 100.0f;
            if (healMode == HEAL_MODE_HP_BELOW && pct > minHpPct) continue;
            if (!best || pct < bestPct) { best = p; bestPct = pct; }
        }
        if (!best) return;                       // ninguem precisa agora
        if (now - lastTargetHealTick < (DWORD)cooldownMs) return;

        float tx = best->x, ty = best->y;
        DebugLog("[HEAL] Healing '%S' HP=%.0f%% (%d targets) at (%.1f,%.1f) key=%c",
            best->name.c_str(), bestPct, (int)targets.size(), tx, ty, (char)healKeyBind);

        // Converte pos do alvo p/ tile (mesmo calculo do follow)
        WORD tileX = (WORD)((int)(tx / 24.0f));
        WORD tileY = (WORD)((int)(ty / 24.0f));
        if (tileX > 27) tileX = 27;
        if (tileY > 27) tileY = 27;

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

        // 2b. Com 2+ entidades no tile o jogo abre "Selecione o alvo": sem
        //     navegacao o Enter sempre confirma a primeira linha. Navega ate o
        //     jogador de menor HP% do tile (regra do usuario).
        PickListEntry(ctx, tileX, tileY, LowestHpPlayerOnTile(ctx, tileX, tileY));

        // 3. Clica/confirma na posicao (Enter = click do jogo, igual follow/attacker)
        SendLocalEnter(gw);

        lastTargetHealTick = now;
    }

    // Config
    bool  enabled = false;
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
    // Alvo configurado -> entidade viva agora (addr primeiro, nome como
    // fallback: o addr muda quando o jogador recarrega/reloga)
    static const GameContext::EntityInfo* FindEntity(const GameContext& ctx, const HealTarget& t) {
        if (t.addr > 0x1000)
            for (const auto& p : ctx.players)
                if (p.objAddr == t.addr) return &p;
        if (!t.name.empty())
            for (const auto& p : ctx.players)
                if (p.name == t.name) return &p;
        return nullptr;
    }

    // ============================================================
    // Lista "Selecione o alvo" (2+ entidades no mesmo tile)
    // O jogo mostra a lista e o Enter confirma a LINHA DESTACADA.
    // O campo fica dentro da imagem (base 0x00400000, sem ASLR), mas o
    // endereco muda a cada update do jogo — por isso procuramos por
    // assinatura em vez de hardcodar. O widget comeca em:
    //   01 00 78 00 | 2F 00 00 00 | <ptr entidade destacada> | FF FF FF FF |
    //   A8 99 CC 00  (vtable do widget)
    // e o campo e o <ptr> (+8). O jogo reescreve sozinho a cada tecla —
    // nao da pra setar; so lemos e navegamos com seta pra baixo/cima.
    // Verificado ao vivo: build 10432112 B -> campo 0x072CA9A4
    // (build anterior 11640832 B -> 0x072D602C); ↓ muda a selecao.
    // Existem 2 widgets identicos no processo; escolhemos o cujo valor
    // aponta para algo do tile (o outro e outra lista).
    // ============================================================
    // O widget da lista NAO e estatico: e alocado quando "Selecione o alvo"
    // abre e desalocado depois (mesmos endereços mudam de conteudo). Por isso
    // o cache so guarda ACERTO — vazio re-scana no proximo heal.
    static void ScanSelectionFields(HANDLE hProc, std::vector<DWORD>& fields) {
        extern void DebugLog(const char* fmt, ...);
        static HANDLE cachedProc = NULL;
        static std::vector<DWORD> cached;
        static DWORD cachedTick = 0;
        static bool wasEmpty = true;

        DWORD now = GetTickCount();
        if (hProc == cachedProc && !cached.empty() && now - cachedTick < 30000) {
            fields = cached;
            return;
        }

        const DWORD imgBase = 0x00400000;
        const DWORD imgEnd  = 0x00F00000;   // exe de 10-12 MB
        const DWORD chunk   = 0x10000;
        std::vector<BYTE> buf(chunk);

        auto scanOnce = [&](bool strict) {
            for (DWORD addr = imgBase; addr < imgEnd; ) {
                SIZE_T got = 0;
                if (!ReadProcessMemory(hProc, (LPCVOID)addr, buf.data(), chunk, &got) || got < 20) {
                    addr += 0x1000;   // regiao ilegivel: pula pagina
                    continue;
                }
                for (DWORD i = 0; i + 20 <= (DWORD)got; i++) {
                    const BYTE* p = &buf[i];
                    if (p[0]==0x01 && p[1]==0x00 && p[2]==0x78 && p[3]==0x00 &&
                        p[4]==0x2F && p[5]==0x00 && p[6]==0x00 && p[7]==0x00) {
                        // Strict: cabecalho + FFFFFFFF + vtable 0x00CC99A8.
                        // Relaxado: so o cabecalho (os 8 bytes finais podem ser
                        // estado transitorio); o probe da seta valida de verdade.
                        if (strict &&
                            !(p[12]==0xFF && p[13]==0xFF && p[14]==0xFF && p[15]==0xFF &&
                              p[16]==0xA8 && p[17]==0x99 && p[18]==0xCC && p[19]==0x00))
                            continue;
                        DWORD f = addr + i + 8;
                        bool dup = false;
                        for (DWORD h : cached) if (h == f) { dup = true; break; }
                        if (!dup) cached.push_back(f);
                    }
                }
                DWORD next = addr + (DWORD)got - 19;   // re-le os 19 bytes finais
                if (next <= addr) break;
                addr = next;
            }
        };

        cachedProc = hProc;
        cached.clear();
        scanOnce(true);
        if (cached.empty())
            scanOnce(false);            // relaxado: so o cabecalho 01 00 78 00 2F 00 00 00
        cachedTick = now;
        fields = cached;

        if (cached.empty() != wasEmpty) {   // loga so quando o estado muda
            if (!cached.empty())
                DebugLog("[HEAL] scan: %d widget(s), campo=0x%08X",
                         (int)cached.size(), cached[0]);
            else
                DebugLog("[HEAL] scan: nenhum widget (lista fechada)");
            wasEmpty = cached.empty();
        }
    }

    static DWORD ReadListSelection(HANDLE hProc, DWORD field) {
        DWORD v = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)field, &v, 4, &r);
        return (r == 4) ? v : 0;
    }

    static bool SameTile(float x, float y, WORD tileX, WORD tileY) {
        return (WORD)((int)(x / 24.0f)) == tileX && (WORD)((int)(y / 24.0f)) == tileY;
    }

    // Tudo que o jogo pode listar no tile (players + mobs + npcs + proprio)
    static void CollectTileEntities(const GameContext& ctx, WORD tileX, WORD tileY,
                                    std::vector<DWORD>& out) {
        auto add = [&](const std::vector<GameContext::EntityInfo>& list) {
            for (const auto& e : list)
                if (e.hp > 0 && e.maxHp > 0 && SameTile(e.x, e.y, tileX, tileY))
                    out.push_back(e.objAddr);
        };
        add(ctx.players);
        add(ctx.mobs);
        add(ctx.npcs);
        if (ctx.playerAddr > 0x1000 && ctx.selfHp > 0 && ctx.selfMaxHp > 0 &&
            SameTile(ctx.selfX, ctx.selfY, tileX, tileY)) {
            bool has = false;
            for (DWORD a : out) if (a == ctx.playerAddr) { has = true; break; }
            if (!has) out.push_back(ctx.playerAddr);
        }
    }

    // Regra: cura o jogador de MENOR HP% do tile onde o cursor foi posicionado
    static DWORD LowestHpPlayerOnTile(const GameContext& ctx, WORD tileX, WORD tileY) {
        DWORD best = 0;
        float bestPct = 1.0f;
        for (const auto& p : ctx.players) {
            if (p.hp <= 0 || p.maxHp <= 0) continue;
            if (!SameTile(p.x, p.y, tileX, tileY)) continue;
            float pct = (float)p.hp / (float)p.maxHp;
            if (best == 0 || pct < bestPct) { bestPct = pct; best = p.objAddr; }
        }
        if (best == 0 && ctx.playerAddr > 0x1000 && ctx.selfHp > 0 && ctx.selfMaxHp > 0 &&
            SameTile(ctx.selfX, ctx.selfY, tileX, tileY))
            best = ctx.playerAddr;
        return best;
    }

    static bool InSet(const std::vector<DWORD>& v, DWORD a) {
        for (DWORD c : v) if (c == a) return true;
        return false;
    }

    // Espera a selecao mudar (o PostMessage e async); devolve o novo valor
    static DWORD WaitForSelectionChange(HANDLE hProc, DWORD field, DWORD before) {
        for (int k = 0; k < 8; k++) {
            Sleep(20);
            DWORD v = ReadListSelection(hProc, field);
            if (v != before) return v;
        }
        return before;
    }

    // Navega a lista aberta ate a selecao == desired. false = nao navegou
    // (lista fechada / alvo fora da lista) — o chamador cai no Enter antigo.
    static bool PickListEntry(const GameContext& ctx, WORD tileX, WORD tileY, DWORD desired) {
        extern void DebugLog(const char* fmt, ...);
        if (ctx.hProcess == NULL || ctx.gameWindow == NULL || desired == 0) return false;

        std::vector<DWORD> cands;
        CollectTileEntities(ctx, tileX, tileY, cands);
        if (cands.size() < 2) {            // com 1 entidade nao ha lista
            DebugLog("[HEAL] Lista: so %d entidade no tile (%08X,%08X)",
                     (int)cands.size(), tileX, tileY);
            return false;
        }
        if (!InSet(cands, desired)) {
            DebugLog("[HEAL] Lista: alvo %08X fora do tile (%d entidades)",
                     desired, (int)cands.size());
            return false;
        }

        // Ha 2 widgets identicos no processo. O VALOR nao serve pra escolher:
        // o outro guarda o proprio jogador, que tambem esta no tile. A unica
        // prova e mexer: aperta uma seta e ve qual dos campos muda.
        std::vector<DWORD> fields;
        ScanSelectionFields(ctx.hProcess, fields);
        if (fields.empty()) {
            // O widget so existe com a lista ABERTA; se a lista demorou mais
            // que 80ms pra abrir, re-scan mais pra frente acha ela.
            Sleep(120);
            ScanSelectionFields(ctx.hProcess, fields);
        }
        if (fields.empty()) {
            DebugLog("[HEAL] Lista: campo de selecao nao encontrado (widget alocado so com lista aberta)");
            return false;
        }
        int nf = (int)fields.size();
        if (nf > 8) nf = 8;
        DWORD pre[8], post[8];
        for (int i = 0; i < nf; i++) pre[i] = ReadListSelection(ctx.hProcess, fields[i]);

        // Probe (o caller ja dormiu 80ms apos a tecla de skill, entao a lista,
        // se foi abrir, ja esta aberta)
        PostGameKey(ctx.gameWindow, VK_DOWN, 20);
        Sleep(60);
        int changed = -1;
        for (int i = 0; i < nf; i++) {
            post[i] = ReadListSelection(ctx.hProcess, fields[i]);
            if (post[i] != pre[i] && changed < 0) changed = i;
        }
        if (changed < 0) {
            DebugLog("[HEAL] Lista: seta nao mexeu nada (sem lista), %d widgets, sel=%08X",
                     nf, pre[0]);
            return false;   // nao ha lista (skill em cooldown etc.)
        }
        DWORD field = fields[changed];
        DWORD start = pre[changed];
        DWORD sel   = post[changed];
        DebugLog("[HEAL] Lista: campo=%08X start=%08X sel=%08X (%d widgets, %d entidades)",
                 field, start, sel, nf, (int)cands.size());

        int steps = (int)cands.size() + 2;
        if (steps > 12) steps = 12;

        // O probe ja provou que a lista responde; aqui so vai ate o desired
        // (o probe ja andou 1 passo, entao se desired for o inicio a volta
        // com seta cima la embaixo resolve)
        for (int i = 0; i < steps && sel != desired; i++) {
            DWORD before = sel;
            PostGameKey(ctx.gameWindow, VK_DOWN, 20);
            sel = WaitForSelectionChange(ctx.hProcess, field, before);
            if (sel == before) break;   // fim da lista (pode nao dar a volta)
        }
        if (sel != desired) {           // lista pode nao dar a volta: tenta cima
            for (int i = 0; i < steps && sel != desired; i++) {
                DWORD before = sel;
                PostGameKey(ctx.gameWindow, VK_UP, 20);
                sel = WaitForSelectionChange(ctx.hProcess, field, before);
                if (sel == before) break;
            }
        }

        if (sel != desired) {
            DebugLog("[HEAL] Lista: falhou (start=%08X sel=%08X want=%08X, %d entidades)",
                     start, sel, desired, (int)cands.size());
            return false;
        }
        DebugLog("[HEAL] Lista: selecionado %08X (campo=%08X, start=%08X, %d entradas)",
                 sel, field, start, (int)cands.size());
        return true;
    }

    static DWORD GetCursorPtr(HANDLE hProc) {
        DWORD gmPtr = 0; SIZE_T r = 0;
        ReadProcessMemory(hProc, (LPCVOID)0x00DA09AC, &gmPtr, 4, &r);
        if (r != 4 || gmPtr <= 0x1000) return 0;
        DWORD gm = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gmPtr + 0x14), &gm, 4, &r);
        if (r != 4 || gm <= 0x1000) return 0;
        DWORD cur = 0;
        ReadProcessMemory(hProc, (LPCVOID)(gm + 0x124C), &cur, 4, &r);
        return (r == 4) ? cur : 0;
    }

    static void SendGameKey(HWND gw, WORD vk) {
        PostGameKey(gw, vk, 30);
    }

    static void SendLocalEnter(HWND gw) {
        PostGameEnter(gw, 60);
    }
    HWND hParent = NULL;
    HWND hChkEnabled = NULL, hLblMinHp = NULL, hEdtMinHp = NULL, hEdtCooldown = NULL, hEdtHealKey = NULL;
    HWND hCboMode = NULL;
    HWND hChkSelfHeal = NULL, hEdtSelfHp = NULL, hEdtSelfKey = NULL;
    DWORD lastSelfHealTick = 0;
    DWORD lastTargetHealTick = 0;
};
