// warspear-bot22.dll - Tab target + click attack + SendInput mouse
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
static DWORD g_lockedTarget = 0;
static int g_killCount = 0;

void Log(const char* msg) {
    OutputDebugStringA(msg);
    if (g_logFile == INVALID_HANDLE_VALUE) {
        g_logFile = CreateFileA(
            "C:\\Users\\Admin\\Documents\\Default Project\\warspear-bot\\bot_log.txt",
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

void ClickAt(HWND hw, int x, int y) {
    POINT pt = { x, y };
    ClientToScreen(hw, &pt);
    INPUT inputs[3] = {};
    // Move
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = (long)(pt.x * 65536.0 / GetSystemMetrics(SM_CXSCREEN));
    inputs[0].mi.dy = (long)(pt.y * 65536.0 / GetSystemMetrics(SM_CYSCREEN));
    inputs[0].mi.mouseData = 0;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    // Down
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    // Up
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(3, inputs, sizeof(INPUT));
}

void PressKey(WORD vk) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

DWORD WINAPI BotThread(LPVOID) {
    for (int i = 0; i < 50; i++) {
        HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\WarspearBotShared");
        if (h) { g_cmd = (BotCmd*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd)); if (g_cmd) break; }
        Sleep(100);
    }
    if (!g_cmd) return 1;
    Sleep(3000);

    DWORD sys = GetSys();
    DWORD gm = GetGM(sys);
    DWORD lp = GetLP(gm);
    DWORD cObj = gm > 0x1000 ? GR<DWORD>(gm + 0x123c) : 0;
    char buf[512];
    wsprintfA(buf, "[INIT] sys=%08X gm=%08X lp=%08X cObj=%08X", sys, gm, lp, cObj);
    Log(buf);

    HWND hw = FindWindowA(NULL, "Warspear Online");
    wsprintfA(buf, "[INIT] hwnd=%08X", (DWORD)hw);
    Log(buf);

    int count = 0;
    int tabCount = 0;
    bool targetLocked = false;

    while (true) {
        // ===== FOLLOW MODE =====
        if (g_cmd->followOn) {
            DWORD followTarget = (DWORD)g_cmd->followAddr;

            if (followTarget > 0x1000) {
                int fhp = GR<int>(followTarget + 0x10C);
                int fvt = GR<int>(followTarget);
                if (fhp > 0 && fvt >= 0x00400000 && fvt <= 0x01000000) {
                    int ftx = GR<int>(followTarget + 0x10) / 65536;
                    int fty = GR<int>(followTarget + 0x14) / 65536;
                    int plx = GR<int>(lp + 0x10) / 65536;
                    int ply = GR<int>(lp + 0x14) / 65536;
                    int dx = ftx - plx;
                    int dy = fty - ply;
                    int dist2 = dx * dx + dy * dy;

                    if (count % 20 == 0) {
                        wsprintfA(buf, "[FOLLOW] target=(%d,%d) player=(%d,%d) dist2=%d hp=%d",
                            ftx, fty, plx, ply, dist2, fhp);
                        Log(buf);
                    }

                    // Follow at a fixed distance behind target to avoid oscillation
                    const float followDistance = 4.0f; // Follow at 4 tiles distance
                    const float followDistanceSq = followDistance * followDistance;

                    if (dist2 > followDistanceSq) {
                        // We're farther than our follow distance, move towards target
                        float dist = sqrtf((float)dist2);
                        float dxNorm = dx / dist;
                        float dyNorm = dy / dist;

                        int sx = 336 + (int)(dxNorm * followDistance * 20.0f);
                        int sy = 260 + (int)(dyNorm * followDistance * 20.0f);

                        // Clamp to screen bounds
                        if (sx < 20) sx = 20; if (sx > 652) sx = 652;
                        if (sy < 20) sy = 20; if (sy > 500) sy = 500;

                        // Only click if game window is not minimized and coordinates are valid
                        if (!IsIconic(hw)) {
                            RECT rect;
                            if (GetWindowRect(hw, &rect)) {
                                int width = rect.right - rect.left;
                                int height = rect.bottom - rect.top;
                                // Additional safety: ensure click is within reasonable game area
                                if (sx >= 0 && sx <= width && sy >= 0 && sy <= height) {
                                    ClickAt(hw, sx, sy);
                                }
                            }
                        }
                    }
                    // If we're closer than or equal to followDistance, do nothing (let character stop)
                }
            }
        }

        // ===== ATTACK MODE =====
        if (g_cmd->attackOn) {
            DWORD curTarget = GR<DWORD>(lp + 0x290);

            // Check if current target is alive
            if (curTarget > 0x1000) {
                int hp = GR<int>(curTarget + 0x10C);
                int vtable = GR<int>(curTarget);
                bool valid = (vtable >= 0x00400000 && vtable <= 0x01000000);

                if (hp == 0 || !valid) {
                    wsprintfA(buf, "[KILL] Target dead/invalid! hp=%d vt=%08X kills=%d", hp, vtable, g_killCount);
                    Log(buf);
                    g_killCount++;
                    targetLocked = false;
                    curTarget = 0;
                    GW<DWORD>(lp + 0x290, 0);
                    GW<DWORD>(lp + 0x478, 0);
                    Sleep(1000);
                    continue;
                }

                int tileX = GR<int>(curTarget + 0x10) / 65536;
                int tileY = GR<int>(curTarget + 0x14) / 65536;
                int playerX = GR<int>(lp + 0x10) / 65536;
                int playerY = GR<int>(lp + 0x14) / 65536;
                int dist2 = (tileX - playerX) * (tileX - playerX) + (tileY - playerY) * (tileY - playerY);

                if (count % 10 == 0) {
                    wsprintfA(buf, "[ATK] tar=%08X tar=(%d,%d) hp=%d player=(%d,%d) dist2=%d kills=%d",
                        curTarget, tileX, tileY, hp, playerX, playerY, dist2, g_killCount);
                    Log(buf);
                }

                if (dist2 > 9) {
                    // FAR: Walk to mob by clicking near it
                    int relX = tileX - playerX;
                    int relY = tileY - playerY;
                    int sx = 336 + relX * 20;
                    int sy = 260 + relY * 20;
                    if (sx < 20) sx = 20; if (sx > 652) sx = 652;
                    if (sy < 20) sy = 20; if (sy > 500) sy = 500;
                    // Only click if game window is not minimized
                    if (!IsIconic(hw)) {
                        ClickAt(hw, sx, sy);
                        if (count % 10 == 0) {
                            wsprintfA(buf, "[WALK] click=(%d,%d)", sx, sy);
                            Log(buf);
                        }
                    }
                } else {
                    // NEAR: Attack!
                    // Select target properly with Tab first
                    PressKey(VK_TAB);
                    Sleep(300);

                    // Now click on mob (should be near center)
                    int relX = tileX - playerX;
                    int relY = tileY - playerY;
                    int sx = 336 + relX * 20;
                    int sy = 260 + relY * 20;
                    // Only click if game window is not minimized
                    if (!IsIconic(hw)) {
                        ClickAt(hw, sx, sy);
                        Sleep(200);

                        // Also try Enter
                        PressKey(VK_RETURN);
                        Sleep(200);
                    }

                    if (count % 5 == 0) {
                        wsprintfA(buf, "[ATTACK] near! click=(%d,%d) hp=%d", sx, sy, hp);
                        Log(buf);
                    }
                }
            } else {
                // No target: press Tab to find one
                if (tabCount % 3 == 0) {
                    PressKey(VK_TAB);
                    if (count % 10 == 0) {
                        wsprintfA(buf, "[TAB] pressing Tab to find target");
                        Log(buf);
                    }
                }
                tabCount++;
            }

            count++;
            Sleep(1000);
        } else {
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
