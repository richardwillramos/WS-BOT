// warspear-bot22.dll - SetTarget + SendEnter attack (like v1.2)
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

void SetTarget(DWORD addr) {
    DWORD gm = GetGM(GetSys());
    DWORD lp = GetLP(gm);
    if (lp > 0x1000) {
        GW<DWORD>(lp + 0x290, addr);
        GW<DWORD>(lp + 0x478, addr);
    }
}

void SendEnter() {
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(30);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
}

void ClickAt(HWND hw, int x, int y) {
    POINT pt = { x, y };
    ClientToScreen(hw, &pt);
    INPUT inputs[3] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = (long)(pt.x * 65536.0 / GetSystemMetrics(SM_CXSCREEN));
    inputs[0].mi.dy = (long)(pt.y * 65536.0 / GetSystemMetrics(SM_CYSCREEN));
    inputs[0].mi.mouseData = 0;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(3, inputs, sizeof(INPUT));
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
    char buf[512];
    wsprintfA(buf, "[INIT] sys=%08X gm=%08X lp=%08X", sys, gm, lp);
    Log(buf);

    HWND hw = FindWindowA(NULL, "Warspear Online");
    RECT winRect;
    GetWindowRect(hw, &winRect);
    int windowCenterX = (winRect.left + winRect.right) / 2;
    int windowCenterY = (winRect.top + winRect.bottom) / 2;

    int count = 0;

    while (true) {
        // ===== FOLLOW MODE =====
        if (g_cmd->followOn && g_cmd->followAddr > 0x1000) {
            DWORD followTarget = (DWORD)g_cmd->followAddr;
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

                const float followDistance = 4.0f;
                const float followDistanceSq = followDistance * followDistance;

                if (dist2 > followDistanceSq) {
                    float dist = sqrtf((float)dist2);
                    float dxNorm = dx / dist;
                    float dyNorm = dy / dist;
                    int sx = windowCenterX + (int)(dxNorm * followDistance * 20.0f);
                    int sy = windowCenterY + (int)(dyNorm * followDistance * 20.0f);
                    int winWidth = winRect.right - winRect.left;
                    int winHeight = winRect.bottom - winRect.top;
                    if (sx < 20) sx = 20; if (sx > winWidth - 20) sx = winWidth - 20;
                    if (sy < 20) sy = 20; if (sy > winHeight - 20) sy = winHeight - 20;
                    if (!IsIconic(hw)) {
                        ClickAt(hw, sx, sy);
                    }
                }
            }
        }

        // ===== ATTACK MODE (v1.2 style: SetTarget + SendEnter) =====
        if (g_cmd->attackOn && g_cmd->targetAddr > 0x1000) {
            DWORD hp = GR<DWORD>((DWORD)g_cmd->targetAddr + 0x10C);
            if (hp > 0) {
                SetTarget((DWORD)g_cmd->targetAddr);
                Sleep(100);
                SendEnter();

                if (count % 10 == 0) {
                    wsprintfA(buf, "[ATK] target=0x%08X hp=%d kills=%d", (DWORD)g_cmd->targetAddr, hp, g_killCount);
                    Log(buf);
                }
            } else {
                // Target dead - clear selection
                GW<DWORD>(lp + 0x290, 0);
                GW<DWORD>(lp + 0x478, 0);
                g_killCount++;
                Sleep(1000);
            }
            Sleep(1500);
        } else {
            g_killCount = 0;
            Sleep(200);
        }

        count++;
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
