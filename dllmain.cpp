// warspear-bot.dll - Arrow-key cursor navigation + attack
#include <windows.h>
#include <winuser.h>
#include <math.h>

struct BotCmd {
    volatile long attackOn;
    volatile long followOn;
    volatile long healOn;
    volatile long healThreshold;
    volatile long targetAddr;
    volatile long followAddr;
};

static BotCmd* g_cmd = NULL;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static int g_killCount = 0;

// ============================================================
// Cursor action flags (at cursor_ptr + 0x7C)
// ============================================================
constexpr int CURSOR_ACTION_ATTACK = 8;
constexpr int CURSOR_ACTION_MOVE   = 13;
constexpr int CURSOR_ACTION_NONE   = 15;

void Log(const char* msg) {
    OutputDebugStringA(msg);
    if (g_logFile == INVALID_HANDLE_VALUE) {
        g_logFile = CreateFileA(
            "C:\\Users\\Admin\\Documents\\GitHub\\WS-BOT\\bot_log.txt",
            GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (g_logFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_logFile, msg, (DWORD)lstrlenA(msg), &written, NULL);
        WriteFile(g_logFile, "\n", 1, &written, NULL);
    }
}

template<typename T> T GR(DWORD a) { T v{}; __try { v = *(T*)a; } __except(EXCEPTION_EXECUTE_HANDLER){} return v; }
template<typename T> void GW(DWORD a, T v) { __try { *(T*)a = v; } __except(EXCEPTION_EXECUTE_HANDLER){} }

DWORD GetSys() { return GR<DWORD>(0x00D387AC); }
DWORD GetGM(DWORD s) { return (s > 0x1000) ? GR<DWORD>(s + 0x14) : 0; }
DWORD GetLP(DWORD g) { return (g > 0x1000) ? GR<DWORD>(g + 0x40) : 0; }

// ============================================================
// Cursor helpers
// ============================================================
DWORD GetCursorPtr() {
    DWORD gm = GetGM(GetSys());
    if (gm == 0) return 0;
    return GR<DWORD>(gm + 0x123C);
}

short ReadCursorX() {
    DWORD cur = GetCursorPtr();
    return cur ? GR<short>(cur + 0x08) : 0;
}

short ReadCursorY() {
    DWORD cur = GetCursorPtr();
    return cur ? GR<short>(cur + 0x0A) : 0;
}

int ReadCursorAction() {
    DWORD cur = GetCursorPtr();
    return cur ? GR<int>(cur + 0x7C) : -1;
}

void WriteCursorTile(short tileX, short tileY) {
    DWORD cur = GetCursorPtr();
    if (!cur) return;
    int rawX = (int)tileX * 0x180000;
    int rawY = (int)tileY * 0x180000;
    GW<short>(cur + 0x08, tileX);
    GW<short>(cur + 0x0A, tileY);
    GW<int>(cur + 0x10, rawX);
    GW<int>(cur + 0x14, rawY);
}

// ============================================================
// PostMessage-based key press (does not affect global keyboard)
// ============================================================
void PressGameKey(WORD vk) {
    HWND hw = FindWindowA(NULL, "Warspear Online");
    if (!hw || !IsWindow(hw)) return;

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM keyDown = 1 | ((LPARAM)scan << 16);
    LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);

    PostMessageW(hw, WM_KEYDOWN, vk, keyDown);
    PostMessageW(hw, WM_KEYUP, vk, keyUp);
}

void SendEnter() {
    PressGameKey(VK_RETURN);
}

// ============================================================
// Follow: write cursor to target tile + Enter (walk)
// ============================================================
void DoFollow(DWORD targetAddr) {
    DWORD lp = GetLP(GetGM(GetSys()));
    if (lp <= 0x1000) return;

    int ftx = GR<int>(targetAddr + 0x10);
    int fty = GR<int>(targetAddr + 0x14);
    short tileX = (short)(ftx / 65536);
    short tileY = (short)(fty / 65536);
    if (tileX > 27) tileX = 27;
    if (tileY > 27) tileY = 27;

    // Check distance: only walk if target is far enough
    int plx = GR<int>(lp + 0x10) / 65536;
    int ply = GR<int>(lp + 0x14) / 65536;
    int dx = ftx / 65536 - plx;
    int dy = fty / 65536 - ply;
    int dist2 = dx * dx + dy * dy;
    if (dist2 <= 9) return; // within 3 tiles

    WriteCursorTile(tileX, tileY);
    Sleep(100);
    SendEnter();
}

// ============================================================
// Attack: arrow-key cursor nav + action check + Enter
// ============================================================
DWORD WINAPI BotThread(LPVOID) {
    for (int i = 0; i < 50; i++) {
        HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\WarspearBotShared");
        if (h) { g_cmd = (BotCmd*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd)); if (g_cmd) break; }
        Sleep(100);
    }
    if (!g_cmd) return 1;
    Sleep(3000);

    char buf[512];
    wsprintfA(buf, "[INIT] DLL loaded, attack=%d follow=%d", g_cmd->attackOn, g_cmd->followOn);
    Log(buf);

    HWND hw = FindWindowA(NULL, "Warspear Online");

    // Attack state machine: 0=idle, 1=moving cursor, 2=ready to attack
    int atkState = 0;
    DWORD lastAtkTick = 0;
    DWORD lastStepTick = 0;
    DWORD atkCooldown = 1500;
    DWORD lastLogTick = 0;

    while (true) {
        DWORD now = GetTickCount();

        // ===== FOLLOW MODE =====
        if (g_cmd->followOn && g_cmd->followAddr > 0x1000) {
            DWORD followTarget = (DWORD)g_cmd->followAddr;
            int fhp = GR<int>(followTarget + 0x10C);
            if (fhp > 0) {
                DoFollow(followTarget);
            }
            Sleep(200);
            continue;
        }

        // ===== ATTACK MODE =====
        if (g_cmd->attackOn && g_cmd->targetAddr > 0x1000) {
            DWORD targetAddr = (DWORD)g_cmd->targetAddr;
            int hp = GR<int>(targetAddr + 0x10C);

            if (hp <= 0) {
                // Target dead
                atkState = 0;
                g_killCount++;
                wsprintfA(buf, "[ATK] Target dead, kills=%d", g_killCount);
                Log(buf);
                Sleep(1000);
                continue;
            }

            // Read cursor position
            short cx = ReadCursorX();
            short cy = ReadCursorY();

            // Read mob tile position
            int mobRawX = GR<int>(targetAddr + 0x10);
            int mobRawY = GR<int>(targetAddr + 0x14);
            short mobTX = (short)(mobRawX / 65536);
            short mobTY = (short)(mobRawY / 65536);

            switch (atkState) {
            case 0: // IDLE -> start
                atkState = 1;
                lastStepTick = now;
                break;

            case 1: { // MOVING cursor toward mob
                if (now - lastStepTick < 60) break;

                if (cx == mobTX && cy == mobTY) {
                    // Cursor on mob tile, check attack flag
                    int action = ReadCursorAction();
                    if (action == CURSOR_ACTION_ATTACK) {
                        atkState = 2;
                    }
                    break;
                }

                // Move cursor one tile using arrow key
                WORD vk = 0;
                if (cx < mobTX) vk = VK_RIGHT;
                else if (cx > mobTX) vk = VK_LEFT;
                else if (cy < mobTY) vk = VK_DOWN;
                else if (cy > mobTY) vk = VK_UP;

                if (vk) PressGameKey(vk);
                lastStepTick = now;
                break;
            }

            case 2: { // ATTACK: cursor on mob, action==ATTACK, press Enter
                int action = ReadCursorAction();
                if (action != CURSOR_ACTION_ATTACK) {
                    atkState = 1; // lost position, re-move
                    break;
                }

                if (now - lastAtkTick < atkCooldown) break;

                SendEnter();
                lastAtkTick = now;

                if (now - lastLogTick > 3000) {
                    wsprintfA(buf, "[ATK] Attacking hp=%d cursor=(%d,%d) action=%d kills=%d",
                        hp, cx, cy, action, g_killCount);
                    Log(buf);
                    lastLogTick = now;
                }
                break;
            }
            } // switch

            Sleep(20);
        } else {
            atkState = 0;
            g_killCount = 0;
            Sleep(200);
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, BotThread, NULL, 0, NULL);
    }
    return TRUE;
}
