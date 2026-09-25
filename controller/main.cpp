// warspear-controller/main.cpp
// Warspear Bot Controller v5 - Compact tree UI
// All complex globals heap-allocated to avoid CRT static init crash.

#include <Windows.h>
#include <CommCtrl.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <commdlg.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "comdlg32.lib")

#include "../include/IModule.h"
#include "../include/ModuleManager.h"
#include "../modules/Targeter.h"
#include "../modules/Attacker.h"
#include "../modules/Healer.h"
#include "../modules/Looter.h"
#include "../modules/Follower.h"
#include "../modules/Extra.h"
#include "../modules/Dungeon.h"

// ============================================================
// Game constants
// ============================================================
namespace Game {
    constexpr DWORD GM_PTR      = 0x00D8F98C;
    constexpr DWORD GM_OFFSET   = 0x14;
    constexpr DWORD LP_OFFSET   = 0x40;
    constexpr DWORD ENTITY_TREE = 0x3C;
    constexpr DWORD TN_LEFT   = 0x04;
    constexpr DWORD TN_RIGHT  = 0x08;
    constexpr DWORD TN_OBJ    = 0x14;
    constexpr DWORD TH_ROOT   = 0x00;
    constexpr DWORD TH_COUNT  = 0x04;
    constexpr DWORD ENT_VTABLE    = 0x00;
    constexpr DWORD ENT_RAW_X     = 0x10;
    constexpr DWORD ENT_RAW_Y     = 0x14;
    constexpr DWORD ENT_NAME_PTR  = 0x58;
    constexpr DWORD ENT_NAME_LEN  = 0x60;
    constexpr DWORD ENT_TYPE_IND  = 0x090;
    constexpr DWORD ENT_HP        = 0x110;
    constexpr DWORD ENT_MAX_HP    = 0x114;
    constexpr DWORD ENT_MANA      = 0x118;
    constexpr DWORD ENT_MAX_MANA  = 0x11C;
    constexpr DWORD ENT_LEVEL     = 0x2E4;
    constexpr DWORD ENT_CLASS_IND = 0x3F9;
    constexpr DWORD VT_PLAYER  = 0x00CD12D0;
    constexpr DWORD VT_BEAST   = 0x00CD17B4;
    constexpr DWORD VT_CORPSE  = 0x00000000;   // unknown after update (unused)
    constexpr DWORD OBJ_OBJECT_ID  = 0x120;
    constexpr DWORD OBJ_TYPE_ID    = 0x124;
    constexpr DWORD CURSOR_OFFSET  = 0x1244;
    constexpr DWORD CUR_X          = 0x08;
    constexpr DWORD CUR_Y          = 0x0A;
    constexpr DWORD CUR_RAW_X      = 0x10;
    constexpr DWORD CUR_RAW_Y      = 0x14;
    constexpr DWORD CUR_FLAG       = 0x7C;
}

// ============================================================
// Entity data structures
// ============================================================
struct EntityData {
    wchar_t name[64];
    float x, y;
    int hp, maxHp, mana, maxMana;
    int level, classId;
    float distance;
    int type;
    DWORD objAddr;
};

struct CorpseData {
    wchar_t name[64];
    float x, y;
    float distance;
    DWORD objAddr;
    WORD objectId, typeId;
};

struct ProcInfo { DWORD pid; std::wstring name; };

struct BotCmd {
    volatile long attackOn, followOn, healOn, healThreshold, targetAddr, followAddr;
};

// ============================================================
// Heap-allocated global state
// ============================================================
struct BotState {
    ModuleManager modMgr;
    TargeterModule  targeter;
    AttackerModule  attacker;
    HealerModule    healer;
    LooterModule    looter;
    FollowerModule  follower;
    ExtraModule     extra;
    DungeonModule   dungeon;

    std::vector<EntityData> cachedMobs, cachedPlayers, cachedNpcs;
    std::vector<CorpseData> cachedCorpses;
    std::vector<ProcInfo> procs;
    std::vector<int> listToProc;

    IModule* activeModule = nullptr;
    BotCmd*  pBotCmd = nullptr;
    HANDLE   hSharedMem = NULL;

    GameContext::PendingCorpse pendingCorpse;
};

static BotState* G = nullptr;

// ============================================================
// Simple globals
// ============================================================
static HINSTANCE g_hInst = NULL;
static HWND g_hWnd = NULL;
static HWND g_hStatus = NULL;
static HICON g_hAppIconSmall = NULL;  // icone do app (warspear.ico) p/ titulo - restaurado quando nao ha classe
static HFONT g_hFont = NULL;
static HFONT g_hTreeFont = NULL;

static HANDLE g_hProcess = NULL;
static DWORD  g_gamePid  = 0;
static bool   g_connected = false;
static bool   g_dllInjected = false;
static float  g_selfX = 0, g_selfY = 0;
static float  g_scale = 3.5f;
static DWORD  g_playerAddr = 0, g_gmAddr = 0;

// Stats tracking
static int    g_killCount = 0;
static int    g_lootCount = 0;
static DWORD  g_lastStatsTick = 0;
static std::wstring g_lastLootName;

// Stats carousel (status bar part 0: scroll da direita para a esquerda)
static int         g_statusPart0W = 0;      // largura da parte 0 (setada em LayoutStatusBar)
static std::wstring g_statsFull;            // texto completo com padding p/ looping
static DWORD       g_carouselStart = 0;     // inicio da animacao (0 = parado)

// Tab system
enum TabID { TAB_CONFIG = 0, TAB_QUICK = 1, TAB_CONN = 2 };
static int g_currentTab = TAB_CONFIG;
static HWND g_hTabBtn[3] = {};
static HWND g_hTabPanel[3] = {};

// Config tab - TreeView
static HWND g_hTree = NULL;
static const wchar_t* MOD_NAMES[] = { L"Targeter", L"Attacker", L"Healer", L"Follower", L"Looter", L"Extra", L"Dungeon" };
enum { MID_TARGETER=0, MID_ATTACKER, MID_HEALER, MID_FOLLOWER, MID_LOOTER, MID_EXTRA, MID_DUNGEON };
static const int MOD_COUNT = 7;

// Tree item data: which module + which sub-option
enum TreeItemKind { TREE_PARENT, TREE_TOGGLE, TREE_VALUE, TREE_SELECT };
struct TreeItemData { int module; TreeItemKind kind; int subId; };
static std::vector<TreeItemData> g_treeItems;

// Tree item handles per module [module][subItem]
static HTREEITEM g_hTreeParent[7] = {};
static HTREEITEM g_hTreeChild[7][10] = {};
static int g_treeChildCount[7] = {};

// Quick actions tab
static HWND g_hQuickBtn[8] = {};
static const wchar_t* QUICK_LABELS[] = {
    L"Toggle Attack [F1]", L"STOP ALL [F2]", L"Toggle Follow [F3]",
    L"Toggle Loot [F4]", L"Toggle All ON", L"STOP ALL (button)", L"Scale + [F5]", L"Scale - [F6]"
};
enum { QID_ATK=0, QID_HEAL, QID_FOLLOW, QID_LOOT, QID_ALL, QID_STOP, QID_SCP, QID_SCN };

// Connection tab
static HWND g_hProcList = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnConnect = NULL;
static HWND g_hDllPath = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hBtnInject = NULL;

// Debug console
static HWND g_hDebugConsole = NULL;
static HWND g_hDebugEdit = NULL;
static bool g_debugConsoleOpen = false;

// Menu IDs
enum MenuID {
    IDM_TOGGLE_ATTACK = 2001, IDM_TOGGLE_HEAL, IDM_TOGGLE_FOLLOW, IDM_TOGGLE_LOOT,
    IDM_TOGGLE_ALL, IDM_STOP_ALL, IDM_REFRESH, IDM_CONNECT, IDM_INJECT,
    IDM_BROWSE_DLL, IDM_DEBUG, IDM_SCALE_UP, IDM_SCALE_DOWN, IDM_EXIT,
};

// ============================================================
// Memory helpers
// ============================================================
template<typename T>
T Read(DWORD addr) {
    T val{};
    if (g_hProcess && addr > 0x1000)
        ReadProcessMemory(g_hProcess, (LPCVOID)addr, &val, sizeof(T), NULL);
    return val;
}

template<typename T>
bool Write(DWORD addr, T val) {
    if (!g_hProcess || addr <= 0x1000) return false;
    SIZE_T written = 0;
    return WriteProcessMemory(g_hProcess, (LPVOID)addr, &val, sizeof(T), &written) && written == sizeof(T);
}

float CalcDist(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    return sqrtf(dx*dx + dy*dy);
}

// ============================================================
// NPC / Class helpers
// ============================================================
static const wchar_t* NPC_NAMES[] = {
    L"Miliciano", L"Guarda", L"Balisteiro", L"Almoxarife", L"Vicente",
    L"Leiloeira Ilse", L"Vilma", L"Rokus", L"Mestre Hedwig",
    L"Citadino", L"Citadina", L"Gregrio", L"Moraes", L"Kpqbqtqk",
    L"Norberto, o aougueiro",
    // English NPC names
    L"Guard", L"Militia", L"Ballista", L"Quartermaster", L"Vicente",
    L"Auctioneer Ilse", L"Vilma", L"Rokus", L"Master Hedwig",
    L"Citizen", L"Gregory", L"Moraes", L"Butcher Norberto"
};
static const int NPC_COUNT = sizeof(NPC_NAMES) / sizeof(NPC_NAMES[0]);

// NPC names: built-in defaults below, replaced at runtime by config\npc_names.json
static std::vector<std::wstring>& NpcNames() {
    static std::vector<std::wstring> names;
    static bool seeded = false;
    if (!seeded) { seeded = true; for (int i = 0; i < NPC_COUNT; i++) names.push_back(NPC_NAMES[i]); }
    return names;
}

// "Nome Exato" -> exact match, "Prefixo*" -> prefix match
bool IsNPC(const std::wstring& n) {
    for (auto& s : NpcNames()) {
        if (s.empty()) continue;
        if (s.back() == L'*') {
            size_t pl = s.size() - 1;
            if (n.size() >= pl && n.compare(0, pl, s, 0, pl) == 0) return true;
        } else if (s == n) return true;
    }
    return false;
}

const wchar_t* GetClassName(int classId) {
    switch(classId) {
        case 0:  return L"Undefined";   case 1:  return L"Paladin";
        case 2:  return L"Priest";      case 3:  return L"Mage";
        case 4:  return L"Barbarian";   case 5:  return L"Rogue";
        case 6:  return L"Shaman";      case 7:  return L"Bladedancer";
        case 8:  return L"Ranger";      case 9:  return L"Druid";
        case 10: return L"Deathknight"; case 11: return L"Necromancer";
        case 12: return L"Warlock";     case 13: return L"Seeker";
        case 14: return L"Hunter";      case 15: return L"Warden";
        case 16: return L"Charmer";     case 17: return L"Templar";
        case 18: return L"Chieftain";   case 19: return L"Beastmaster";
        case 20: return L"Reaper";
        default: return L"Unknown";
    }
}

// ============================================================
// Debug console
// ============================================================
void DebugLog(const char* fmt, ...) {
    char buf[1024];
    va_list args; va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    if (g_hDebugEdit) {
        int len = GetWindowTextLengthA(g_hDebugEdit);
        SendMessageA(g_hDebugEdit, EM_SETSEL, len, len);
        SendMessageA(g_hDebugEdit, EM_REPLACESEL, FALSE, (LPARAM)buf);
        SendMessageA(g_hDebugEdit, EM_SETSEL, len + (int)strlen(buf), len + (int)strlen(buf));
        SendMessageA(g_hDebugEdit, EM_SCROLLCARET, 0, 0);
    }
}

// ============================================================
// config\npc_names.json loader (re-read on Connect)
// {"_ajuda":"...", "npcs":["Guarda", "Capitao*"]}
// ============================================================
static void AppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

static std::wstring Utf8OrAnsiToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    UINT cp = CP_UTF8; DWORD fl = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) { cp = CP_ACP; fl = 0; n = MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), NULL, 0); }
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, fl, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static bool LoadNameList(const wchar_t* path, std::vector<std::wstring>& out, const char* tag) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DebugLog("[%s] %S not found - keeping current list (%d names)", tag, path, (int)out.size());
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 1024 * 1024) {
        CloseHandle(h); DebugLog("[%s] %S empty or too big", tag, path); return false;
    }
    std::string raw((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(h, &raw[0], (DWORD)raw.size(), &got, NULL);
    CloseHandle(h);
    if (!ok || !got) return false;
    raw.resize(got);

    std::vector<std::wstring> names;
    const char* q = raw.c_str();
    const char* end = q + raw.size();

    while (q < end && *q != '[') {              // find the array (skips quoted keys)
        if (*q == '"') { q++; while (q < end && *q != '"') { if (*q == '\\') q++; q++; } }
        else q++;
    }
    if (q >= end) { DebugLog("[%s] no [...] array in %S - keeping current list", tag, path); return false; }
    q++;

    while (q < end) {
        unsigned char c = (unsigned char)*q;
        if (c <= ' ' || c == ',') { q++; continue; }
        if (c == ']') break;
        if (q + 1 < end && q[0] == '/' && q[1] == '/') { while (q < end && *q != '\n') q++; continue; }
        if (c != '"') { q++; continue; }
        q++;
        std::string item;
        while (q < end && *q != '"') {
            if (*q == '\\' && q + 1 < end) {
                q++;
                switch (*q) {
                    case 'n': item += '\n'; break;
                    case 't': item += '\t'; break;
                    case 'r': item += '\r'; break;
                    case 'u': {
                        unsigned cp = 0; bool hex = true;
                        for (int k = 1; k <= 4 && q + k < end; k++) {
                            char hc = q[k]; cp <<= 4;
                            if (hc >= '0' && hc <= '9') cp |= hc - '0';
                            else if (hc >= 'a' && hc <= 'f') cp |= hc - 'a' + 10;
                            else if (hc >= 'A' && hc <= 'F') cp |= hc - 'A' + 10;
                            else { hex = false; break; }
                        }
                        if (hex && cp >= 0x20 && !(cp >= 0xD800 && cp <= 0xDFFF)) AppendUtf8(item, cp);
                        q += 4;
                        break;
                    }
                    default: item += *q; break;
                }
                q++;
            } else item += *q++;
        }
        if (q < end) q++;
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        std::wstring w = Utf8OrAnsiToWide(item.substr(a, b - a + 1));
        if (w.empty()) continue;
        bool dup = false;
        for (auto& e : names) if (e == w) { dup = true; break; }
        if (!dup) names.push_back(w);
    }

    if (names.empty()) { DebugLog("[%s] no names parsed in %S - keeping current list", tag, path); return false; }
    out.swap(names);
    DebugLog("[%s] Loaded %d names from %S", tag, (int)out.size(), path);
    return true;
}

static bool LoadNpcNames(const wchar_t* path) {
    NpcNames(); // make sure built-in defaults exist even if the file fails
    return LoadNameList(path, NpcNames(), "NPC");
}

// Boss names for the Dungeon module (config\boss_names.json) - empty = any
// "all mobs dead" ends the wave, no boss recognition needed
std::vector<std::wstring> g_bossNames;

static void ReloadBossNames() {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* bs = wcsrchr(dir, L'\\'); if (bs) *bs = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\config\\boss_names.json", dir);
    LoadNameList(path, g_bossNames, "BOSS");
}

static void ReloadNpcNames() {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* bs = wcsrchr(dir, L'\\'); if (bs) *bs = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\config\\npc_names.json", dir);
    LoadNpcNames(path);
}

void OpenDebugConsole(HWND parent) {
    if (g_debugConsoleOpen) return;
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc);
    wc.lpfnWndProc=DefWindowProcW; wc.hInstance=g_hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"DebugConsoleWnd";
    RegisterClassExW(&wc);
    g_hDebugConsole = CreateWindowExW(WS_EX_TOOLWINDOW, L"DebugConsoleWnd",
        L"Bot Debug Console", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        parent ? 700 : CW_USEDEFAULT, parent ? 50 : CW_USEDEFAULT,
        550, 400, parent, NULL, g_hInst, NULL);
    g_hDebugEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
        5, 5, 535, 355, g_hDebugConsole, NULL, g_hInst, NULL);
    HFONT hMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH|FF_MODERN, L"Consolas");
    SendMessageW(g_hDebugEdit, WM_SETFONT, (WPARAM)hMono, TRUE);
    ShowWindow(g_hDebugConsole, SW_SHOW);
    UpdateWindow(g_hDebugConsole);
    g_debugConsoleOpen = true;
    DebugLog("=== Bot Debug Console Started ===");
    DebugLog("PID: %d", g_gamePid);
}

// ============================================================
// Font helpers
// ============================================================
void InitFont() {
    g_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
    g_hTreeFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
}
void SetFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE); }
void SetTreeFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)g_hTreeFont, TRUE); }

// ============================================================
// Find game window (PID-based)
// ============================================================
HWND FindGameWindow() {
    if (g_gamePid) {
        struct Ctx { DWORD pid; HWND h; } ctx = { g_gamePid, NULL };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto c = (Ctx*)lp; DWORD p = 0; GetWindowThreadProcessId(h, &p);
            if (p == c->pid && IsWindowVisible(h)) { c->h = h; return FALSE; }
            return TRUE;
        }, (LPARAM)&ctx);
        return ctx.h;
    }
    return NULL;
}

// ============================================================
// Game interaction helpers
// ============================================================
DWORD GetCursorAddr() {
    DWORD gmPtr = Read<DWORD>(Game::GM_PTR);
    if (gmPtr <= 0x1000) return 0;
    DWORD gm = Read<DWORD>(gmPtr + Game::GM_OFFSET);
    if (gm <= 0x1000) return 0;
    return Read<DWORD>(gm + Game::CURSOR_OFFSET);
}

bool WriteCursorPos(WORD tileX, WORD tileY) {
    DWORD cur = GetCursorAddr();
    if (cur <= 0x1000) return false;
    if (tileX > 27) tileX = 27;
    if (tileY > 27) tileY = 27;
    Write<WORD>(cur + Game::CUR_X, tileX);
    Write<WORD>(cur + Game::CUR_Y, tileY);
    int rawX = (int)tileX * 0x180000;
    int rawY = (int)tileY * 0x180000;
    Write<int>(cur + Game::CUR_RAW_X, rawX);
    Write<int>(cur + Game::CUR_RAW_Y, rawY);
    return true;
}

bool MoveToTile(float gameX, float gameY) {
    WORD tileX = (WORD)((int)(gameX / 24.0f));
    WORD tileY = (WORD)((int)(gameY / 24.0f));
    if (tileX > 27) tileX = 27;
    if (tileY > 27) tileY = 27;
    if (!WriteCursorPos(tileX, tileY)) return false;
    Sleep(30);
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(30);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

bool GameToClient(float gx, float gy, int& cx, int& cy) {
    HWND w = FindGameWindow();
    if (!w) return false;
    RECT rc; GetClientRect(w, &rc);
    int midX = (rc.right - rc.left) / 2;
    int midY = (rc.bottom - rc.top) / 2;
    float dx = gx - g_selfX;
    float dy = gy - g_selfY;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) { cx = midX; cy = midY; return true; }
    cx = midX + (int)(dx * g_scale);
    cy = midY + (int)(dy * g_scale);
    if (cx < 5) cx = 5; if (cx > rc.right - 5) cx = rc.right - 5;
    if (cy < 5) cy = 5; if (cy > rc.bottom - 5) cy = rc.bottom - 5;
    return true;
}

// ============================================================
// Entity tree traversal
// ============================================================
void TraverseTree(DWORD node, std::vector<EntityData>& entities, std::vector<CorpseData>& corpses, float selfX, float selfY) {
    if (node <= 0x1000) return;
    DWORD left  = Read<DWORD>(node + Game::TN_LEFT);
    DWORD right = Read<DWORD>(node + Game::TN_RIGHT);
    if (left > 0x1000)  TraverseTree(left, entities, corpses, selfX, selfY);
    if (right > 0x1000) TraverseTree(right, entities, corpses, selfX, selfY);
    DWORD objPtr = Read<DWORD>(node + Game::TN_OBJ);
    if (objPtr <= 0x1000) return;

    DWORD vtable = Read<DWORD>(objPtr + Game::ENT_VTABLE);
    int hp = Read<int>(objPtr + Game::ENT_HP);

    if (hp < 0) {
        CorpseData c{}; c.objAddr = objPtr;
        DWORD namePtr = Read<DWORD>(objPtr + Game::ENT_NAME_PTR);
        int nameLen = Read<int>(objPtr + Game::ENT_NAME_LEN);
        if (nameLen > 0 && nameLen < 64 && namePtr > 0x1000) {
            wchar_t w[64] = {};
            for (int i = 0; i < nameLen; i++) { wchar_t ch = Read<wchar_t>(namePtr + i * 2); if (ch == 0) break; w[i] = ch; }
            wcscpy(c.name, w);
        } else wcscpy(c.name, L"Corpse");
        c.x = Read<int>(objPtr + Game::ENT_RAW_X) / 65536.0f;
        c.y = Read<int>(objPtr + Game::ENT_RAW_Y) / 65536.0f;
        c.distance = CalcDist(selfX, selfY, c.x, c.y);
        c.objectId = Read<WORD>(objPtr + Game::OBJ_OBJECT_ID);
        c.typeId = Read<WORD>(objPtr + Game::OBJ_TYPE_ID);
        corpses.push_back(c);
        return;
    }

    EntityData e{}; e.objAddr = objPtr;
    DWORD namePtr = Read<DWORD>(objPtr + Game::ENT_NAME_PTR);
    int nameLen = Read<int>(objPtr + Game::ENT_NAME_LEN);
    if (nameLen > 0 && nameLen < 64 && namePtr > 0x1000) {
        wchar_t w[64] = {};
        for (int i=0;i<nameLen;i++) { wchar_t c=Read<wchar_t>(namePtr+i*2); if(c==0) break; w[i]=c; }
        wcscpy(e.name, w);
    } else wcscpy(e.name, L"(unknown)");
    e.x = Read<int>(objPtr+Game::ENT_RAW_X) / 65536.0f;
    e.y = Read<int>(objPtr+Game::ENT_RAW_Y) / 65536.0f;
    e.hp=Read<int>(objPtr+Game::ENT_HP); e.maxHp=Read<int>(objPtr+Game::ENT_MAX_HP);
    e.mana=Read<int>(objPtr+Game::ENT_MANA); e.maxMana=Read<int>(objPtr+Game::ENT_MAX_MANA);
    int ti=Read<int>(objPtr+Game::ENT_TYPE_IND);
    if (vtable==Game::VT_PLAYER||ti==1) e.type=1;
    else if (vtable==Game::VT_BEAST) e.type=4;
    else if (IsNPC(e.name)) e.type=3;
    else e.type=2;
    e.level=Read<BYTE>(objPtr+Game::ENT_LEVEL);
    e.classId=Read<BYTE>(objPtr+Game::ENT_CLASS_IND);
    e.distance=CalcDist(selfX,selfY,e.x,e.y);
    if (e.hp==0&&e.maxHp==0) return;
    entities.push_back(e);
}

bool ReadGameState(float& sx, float& sy, int& hp, int& mhp, int& mn, int& mmn,
                   std::wstring& name, int& level, int& classId,
                   std::vector<EntityData>& pl, std::vector<EntityData>& mb,
                   std::vector<EntityData>& np, std::vector<CorpseData>& corpses,
                   DWORD* outPlayerAddr=nullptr, DWORD* outGM=nullptr) {
    pl.clear(); mb.clear(); np.clear(); corpses.clear();
    DWORD gmPtr=Read<DWORD>(Game::GM_PTR); if(gmPtr<=0x1000) return false;
    DWORD gm=Read<DWORD>(gmPtr+Game::GM_OFFSET); if(gm<=0x1000) return false;
    if(outGM) *outGM = gm;
    DWORD lp=Read<DWORD>(gm+Game::LP_OFFSET); if(lp<=0x1000) return false;
    if(outPlayerAddr) *outPlayerAddr = lp;
    sx=Read<int>(lp+Game::ENT_RAW_X)/65536.0f; sy=Read<int>(lp+Game::ENT_RAW_Y)/65536.0f;
    hp=Read<int>(lp+Game::ENT_HP); mhp=Read<int>(lp+Game::ENT_MAX_HP);
    mn=Read<int>(lp+Game::ENT_MANA); mmn=Read<int>(lp+Game::ENT_MAX_MANA);
    level=Read<BYTE>(lp+Game::ENT_LEVEL);
    classId=Read<BYTE>(lp+Game::ENT_CLASS_IND);
    DWORD np2=Read<DWORD>(lp+Game::ENT_NAME_PTR); int nl=Read<int>(lp+Game::ENT_NAME_LEN);
    if(nl>0&&nl<64&&np2>0x1000){wchar_t w[64]={};for(int i=0;i<nl;i++){wchar_t c=Read<wchar_t>(np2+i*2);if(c==0)break;w[i]=c;}name=w;}
    else name=L"(unknown)";
    DWORD th=Read<DWORD>(gm+Game::ENTITY_TREE); if(th<=0x1000) return false;
    DWORD root=Read<DWORD>(th+Game::TH_ROOT);
    std::vector<EntityData> all;
    TraverseTree(root, all, corpses, sx, sy);
    for (auto& e : all) {
        switch(e.type){ case 1:pl.push_back(e);break; case 3:np.push_back(e);break; default:mb.push_back(e);break; }
    }
    auto bd=[](const EntityData& a,const EntityData& b){return a.distance<b.distance;};
    std::sort(pl.begin(),pl.end(),bd); std::sort(mb.begin(),mb.end(),bd); std::sort(np.begin(),np.end(),bd);
    auto cd=[](const CorpseData& a,const CorpseData& b){return a.distance<b.distance;};
    std::sort(corpses.begin(),corpses.end(),cd);
    return true;
}

// ============================================================
// DLL injection
// ============================================================
bool InjectDLL(DWORD pid, const wchar_t* path) {
    HANDLE hp=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pid);
    if(!hp) return false;
    size_t len=(wcslen(path)+1)*sizeof(wchar_t);
    LPVOID buf=VirtualAllocEx(hp,NULL,len,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!buf){CloseHandle(hp);return false;}
    WriteProcessMemory(hp,buf,path,len,NULL);
    FARPROC pLoadLib=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HANDLE ht=CreateRemoteThread(hp,NULL,0,(LPTHREAD_START_ROUTINE)pLoadLib,buf,0,NULL);
    bool ok=false;
    if(ht){WaitForSingleObject(ht,5000);DWORD ec=0;GetExitCodeThread(ht,&ec);ok=(ec!=0);CloseHandle(ht);}
    VirtualFreeEx(hp,buf,0,MEM_RELEASE);
    CloseHandle(hp);
    return ok;
}

// ============================================================
// Process listing
// ============================================================
void RefreshProcesses(HWND hList) {
    G->procs.clear();
    G->listToProc.clear();
    SendMessageW(hList,LB_RESETCONTENT,0,0);
    HANDLE hs=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(hs==INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe);
    if(Process32FirstW(hs,&pe)){do{G->procs.push_back({pe.th32ProcessID,pe.szExeFile});}while(Process32NextW(hs,&pe));}
    CloseHandle(hs);
    std::sort(G->procs.begin(),G->procs.end(),[](const ProcInfo& a,const ProcInfo& b){return a.name<b.name;});

    std::vector<size_t> wsIdx, otherIdx;
    for(size_t i=0;i<G->procs.size();i++){
        if(_wcsicmp(G->procs[i].name.c_str(), L"warspear.exe")==0) wsIdx.push_back(i);
        else otherIdx.push_back(i);
    }

    for(size_t idx : wsIdx){
        G->listToProc.push_back((int)idx);
        auto& p = G->procs[idx];
        HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, p.pid);
        std::wstring charName = L"(loading...)";
        int level = 0, classId = 0;
        if(hp){
            float x,y; int hpVal,mhp,mn,mmn;
            std::vector<EntityData> ents, mobs, npcs;
            std::vector<CorpseData> corpses;
            DWORD pAddr=0, gmAddr=0;
            DWORD savedPid = g_gamePid; HANDLE savedH = g_hProcess;
            g_gamePid = p.pid; g_hProcess = hp;
            if(!ReadGameState(x,y,hpVal,mhp,mn,mmn,charName,level,classId,ents,mobs,npcs,corpses,&pAddr,&gmAddr))
                charName = L"(not loaded)";
            g_gamePid = savedPid; g_hProcess = savedH;
            CloseHandle(hp);
        }
        wchar_t buf[256];
        if(charName != L"(loading...)" && charName != L"(not loaded)")
            swprintf(buf, 256, L"%s [Lv.%d %s]  (PID %d)", charName.c_str(), level, GetClassName(classId), p.pid);
        else
            swprintf(buf, 256, L"%s  (PID %d)", charName.c_str(), p.pid);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }

    if(!wsIdx.empty() && !otherIdx.empty()){
        G->listToProc.push_back(-1);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"--- other processes ---");
    }
    for(size_t idx : otherIdx){
        G->listToProc.push_back((int)idx);
        auto& p = G->procs[idx];
        wchar_t buf[256];
        swprintf(buf, 256, L"%s  (PID %d)", p.name.c_str(), p.pid);
        SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)buf);
    }
}

// ============================================================
// Remote keybd_event via CreateRemoteThread
// Generates real OS-level input inside the target process,
// which DirectInput/raw input games actually process.
// ============================================================
static FARPROC g_keybdEventAddr = nullptr;

void RemoteSendEnter() {
    if (!g_hProcess || !g_keybdEventAddr) return;

    // Resolve keybd_event address (same across processes due to shared user-mode pages)
    if (!g_keybdEventAddr) {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) g_keybdEventAddr = GetProcAddress(hUser32, "keybd_event");
    }
    if (!g_keybdEventAddr) return;

    // Shellcode:
    //   mov esi, <keybd_event>
    //   push 0; push 0; push 0; push 0x0D; call esi   (keybd_event down)
    //   push 0; push 2; push 0; push 0x0D; call esi   (keybd_event up)
    //   ret 4
    BYTE sc[40];
    int i = 0;
    sc[i++] = 0xBE;                                    // mov esi, imm32
    *(DWORD*)(sc + i) = (DWORD)g_keybdEventAddr; i += 4;
    // keybd_event(VK_RETURN, 0, 0, 0)
    sc[i++] = 0x6A; sc[i++] = 0x00;                    // push 0 (dwExtraInfo)
    sc[i++] = 0x6A; sc[i++] = 0x00;                    // push 0 (dwFlags)
    sc[i++] = 0x6A; sc[i++] = 0x00;                    // push 0 (bScan)
    sc[i++] = 0x6A; sc[i++] = 0x0D;                    // push 0x0D (VK_RETURN)
    sc[i++] = 0xFF; sc[i++] = 0xD6;                    // call esi
    // keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0)
    sc[i++] = 0x6A; sc[i++] = 0x00;                    // push 0
    sc[i++] = 0x6A; sc[i++] = 0x02;                    // push 2 (KEYEVENTF_KEYUP)
    sc[i++] = 0x6A; sc[i++] = 0x00;                    // push 0
    sc[i++] = 0x6A; sc[i++] = 0x0D;                    // push 0x0D
    sc[i++] = 0xFF; sc[i++] = 0xD6;                    // call esi
    sc[i++] = 0xC2; sc[i++] = 0x04; sc[i++] = 0x00;   // ret 4

    LPVOID remote = VirtualAllocEx(g_hProcess, NULL, i, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote) return;
    WriteProcessMemory(g_hProcess, remote, sc, i, NULL);
    HANDLE ht = CreateRemoteThread(g_hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)remote, NULL, 0, NULL);
    if (ht) { WaitForSingleObject(ht, 1000); CloseHandle(ht); }
    VirtualFreeEx(g_hProcess, remote, 0, MEM_RELEASE);
}

// ============================================================
// Remote game function calls (for Attacker)
// ============================================================

// HandleMoveOrAction is DISABLED: the old address (0x00A3F480) is dead after the
// game update and calling it would crash the client. Verified: the game now
// recomputes cursor+0x7C by itself from the cursor struct (write cursor tile/raw,
// wait ~1 frame, read +0x7C). Kept so GameContext still has a valid function pointer.
void RemoteHandleMoveOrAction(DWORD localPlayerAddr) {
    (void)localPlayerAddr;
}

// ============================================================
// Build GameContext
// ============================================================
GameContext BuildContext() {
    GameContext ctx{};
    ctx.hProcess = g_hProcess;
    ctx.gamePid = g_gamePid;
    ctx.selfX = g_selfX; ctx.selfY = g_selfY;
    ctx.playerAddr = g_playerAddr; ctx.gmAddr = g_gmAddr;
    ctx.gameWindow = FindGameWindow();
    ctx.tickCount = GetTickCount();
    ctx.remoteSendEnter = RemoteSendEnter;
    ctx.remoteHandleMoveOrAction = RemoteHandleMoveOrAction;
    ctx.pendingCorpse = &G->pendingCorpse;

    if (g_playerAddr > 0x1000) {
        ctx.selfHp = Read<int>(g_playerAddr + Game::ENT_HP);
        ctx.selfMaxHp = Read<int>(g_playerAddr + Game::ENT_MAX_HP);
        ctx.selfMana = Read<int>(g_playerAddr + Game::ENT_MANA);
        ctx.selfMaxMana = Read<int>(g_playerAddr + Game::ENT_MAX_MANA);
        ctx.selfLevel = Read<BYTE>(g_playerAddr + Game::ENT_LEVEL);
        ctx.selfClassId = Read<BYTE>(g_playerAddr + Game::ENT_CLASS_IND);
        DWORD np = Read<DWORD>(g_playerAddr + Game::ENT_NAME_PTR);
        int nl = Read<int>(g_playerAddr + Game::ENT_NAME_LEN);
        if (nl > 0 && nl < 64 && np > 0x1000) {
            wchar_t w[64] = {};
            for (int i = 0; i < nl; i++) { wchar_t c = Read<wchar_t>(np + i * 2); if (c == 0) break; w[i] = c; }
            ctx.selfName = w;
        }
    }

    for (auto& e : G->cachedPlayers) {
        GameContext::EntityInfo ei;
        ei.objAddr=e.objAddr; ei.name=e.name; ei.x=e.x; ei.y=e.y;
        ei.hp=e.hp; ei.maxHp=e.maxHp; ei.distance=e.distance; ei.type=e.type;
        ctx.players.push_back(ei);
    }
    for (auto& e : G->cachedMobs) {
        GameContext::EntityInfo ei;
        ei.objAddr=e.objAddr; ei.name=e.name; ei.x=e.x; ei.y=e.y;
        ei.hp=e.hp; ei.maxHp=e.maxHp; ei.distance=e.distance; ei.type=e.type;
        ctx.mobs.push_back(ei);
    }
    for (auto& e : G->cachedNpcs) {
        GameContext::EntityInfo ei;
        ei.objAddr=e.objAddr; ei.name=e.name; ei.x=e.x; ei.y=e.y;
        ei.hp=e.hp; ei.maxHp=e.maxHp; ei.distance=e.distance; ei.type=e.type;
        ctx.npcs.push_back(ei);
    }
    for (auto& c : G->cachedCorpses) {
        GameContext::CorpseInfo ci;
        ci.objAddr=c.objAddr; ci.name=c.name; ci.x=c.x; ci.y=c.y; ci.distance=c.distance;
        ctx.corpses.push_back(ci);
    }
    return ctx;
}

// ============================================================
// UI: Input dialog for int/float settings
// ============================================================
static int g_inputResult = 0;
static HWND g_inputParent = NULL;

LRESULT CALLBACK InputDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HWND hEd = CreateWindowExW(0, L"edit", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
            10, 10, 180, 24, h, (HMENU)1001, g_hInst, NULL);
        SendMessageW(hEd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        wchar_t buf[32]; swprintf(buf, 32, L"%d", g_inputResult);
        SetWindowTextW(hEd, buf);
        SetFocus(hEd);
        CreateWindowExW(0, L"button", L"OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            200, 10, 50, 24, h, (HMENU)1002, g_hInst, NULL);
        CreateWindowExW(0, L"button", L"Cancel", WS_CHILD|WS_VISIBLE,
            260, 10, 50, 24, h, (HMENU)1003, g_hInst, NULL);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 1002) {
            wchar_t buf[32] = {};
            GetWindowTextW(GetDlgItem(h, 1001), buf, 32);
            g_inputResult = _wtoi(buf);
            DestroyWindow(h);
        }
        if (LOWORD(w) == 1003) DestroyWindow(h);
        break;
    case WM_DESTROY:
        if (g_inputParent) { EnableWindow(g_inputParent, TRUE); SetForegroundWindow(g_inputParent); }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

int ShowInputInt(HWND parent, const wchar_t* title, int current) {
    g_inputResult = current;
    g_inputParent = parent;

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = InputDlgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"InputDlg";
    RegisterClassExW(&wc);

    EnableWindow(parent, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"InputDlg", title,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 330, 70,
        parent ? parent : g_hWnd, NULL, g_hInst, NULL);
    ShowWindow(hDlg, SW_SHOW); UpdateWindow(hDlg);
    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return g_inputResult;
}

// String input dialog
static wchar_t g_inputStrBuf[256] = {};
static HWND g_inputStrParent = NULL;
static LRESULT CALLBACK InputStrDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        CreateWindowExW(0, L"static", L"", WS_CHILD|WS_VISIBLE,
            10, 13, 300, 18, h, NULL, g_hInst, NULL);
        HWND hEd = CreateWindowExW(0, L"edit", L"",
            WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            10, 10, 240, 24, h, (HMENU)1001, g_hInst, NULL);
        SendMessageW(hEd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SetWindowTextW(hEd, g_inputStrBuf);
        SetFocus(hEd);
        CreateWindowExW(0, L"button", L"OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            260, 10, 50, 24, h, (HMENU)1002, g_hInst, NULL);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 1002) {
            GetWindowTextW(GetDlgItem(h, 1001), g_inputStrBuf, 256);
            DestroyWindow(h);
        }
        break;
    case WM_DESTROY:
        if (g_inputStrParent) { EnableWindow(g_inputStrParent, TRUE); SetForegroundWindow(g_inputStrParent); }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowInputString(HWND parent, const wchar_t* title, const wchar_t* current, wchar_t* out, int maxLen) {
    wcscpy_s(g_inputStrBuf, current ? current : L"");
    g_inputStrParent = parent;

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = InputStrDlgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"InputStrDlg";
    RegisterClassExW(&wc);

    EnableWindow(parent, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"InputStrDlg", title,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 330, 70,
        parent ? parent : g_hWnd, NULL, g_hInst, NULL);
    ShowWindow(hDlg, SW_SHOW); UpdateWindow(hDlg);
    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    wcscpy_s(out, maxLen, g_inputStrBuf);
}

// ============================================================
// UI: Selection list dialog (for picking players, etc.)
// ============================================================
static int g_selResult = -1;
static HWND g_selParent = NULL;

LRESULT CALLBACK SelListDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HWND hList = CreateWindowExW(0, L"listbox", L"",
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
            10, 10, 300, 250, h, (HMENU)1101, g_hInst, NULL);
        SendMessageW(hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        // Populate list from stored data
        extern std::vector<std::wstring> g_selListItems;
        for (auto& item : g_selListItems)
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
        if (!g_selListItems.empty())
            SendMessageW(hList, LB_SETCURSEL, 0, 0);
        SetFocus(hList);
        CreateWindowExW(0, L"button", L"OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            320, 10, 60, 24, h, (HMENU)1102, g_hInst, NULL);
        CreateWindowExW(0, L"button", L"Cancel", WS_CHILD|WS_VISIBLE,
            320, 40, 60, 24, h, (HMENU)1103, g_hInst, NULL);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 1102 || (LOWORD(w) == 1101 && HIWORD(w) == LBN_DBLCLK)) {
            HWND hList = GetDlgItem(h, 1101);
            g_selResult = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            DestroyWindow(h);
        }
        if (LOWORD(w) == 1103) { g_selResult = -1; DestroyWindow(h); }
        break;
    case WM_DESTROY:
        if (g_selParent) { EnableWindow(g_selParent, TRUE); SetForegroundWindow(g_selParent); }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

std::vector<std::wstring> g_selListItems;

int ShowSelectionList(HWND parent, const wchar_t* title, const wchar_t* prompt,
                      const std::vector<std::wstring>& items) {
    g_selResult = -1;
    g_selParent = parent;
    g_selListItems = items;

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SelListDlgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SelListDlg";
    RegisterClassExW(&wc);

    EnableWindow(parent, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SelListDlg", title,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 320,
        parent ? parent : g_hWnd, NULL, g_hInst, NULL);
    ShowWindow(hDlg, SW_SHOW); UpdateWindow(hDlg);
    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return g_selResult;
}

// ============================================================
// UI: TreeView config - helpers
// ============================================================
// Persistent string storage for TreeView items
static std::vector<std::wstring> g_treeStrings;

HTREEITEM TreeAddItem(HWND hTree, HTREEITEM hParent, const wchar_t* text, int dataIdx) {
    g_treeStrings.push_back(std::wstring(text));
    TVINSERTSTRUCTW tvi{};
    tvi.hParent = hParent;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.item.pszText = const_cast<LPWSTR>(g_treeStrings.back().c_str());
    tvi.item.cchTextMax = (int)g_treeStrings.back().size() + 1;
    tvi.item.lParam = dataIdx;
    return (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvi);
}

void TreeSetItemText(int mod, int childIdx, const wchar_t* text) {
    if (!g_hTree || childIdx < 0 || childIdx >= g_treeChildCount[mod]) return;
    // Find the string slot for this item and update it
    TVITEMW ti{};
    ti.mask = TVIF_HANDLE | TVIF_PARAM;
    ti.hItem = g_hTreeChild[mod][childIdx];
    if (SendMessageW(g_hTree, TVM_GETITEM, 0, (LPARAM)&ti)) {
        int idx = (int)ti.lParam;
        if (idx >= 0 && idx < (int)g_treeStrings.size()) {
            g_treeStrings[idx] = std::wstring(text);
            ti.mask = TVIF_TEXT;
            ti.pszText = const_cast<LPWSTR>(g_treeStrings[idx].c_str());
            ti.cchTextMax = (int)g_treeStrings[idx].size() + 1;
            SendMessageW(g_hTree, TVM_SETITEMW, 0, (LPARAM)&ti);
        }
    }
}

void TreeExpandAll() {
    for (int m = 0; m < MOD_COUNT; m++) {
        if (g_hTreeParent[m])
            SendMessageW(g_hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)g_hTreeParent[m]);
    }
}

void RefreshTree() {
    if (!G || !g_hTree) return;
    wchar_t b[256];

    TreeSetItemText(MID_TARGETER, 0, G->targeter.enabled ? L"Status: true" : L"Status: false");
    TreeSetItemText(MID_TARGETER, 1, G->targeter.retargetOnNearby ? L"Retarget on nearby: ON" : L"Retarget on nearby: OFF");
    swprintf(b,256,L"Max distance: %d", (int)G->targeter.maxDistance);
    TreeSetItemText(MID_TARGETER, 2, b);
    { const wchar_t* fm[] = { L"All", L"By Name", L"By Distance", L"Damaged" };
    swprintf(b,256,L"Filter: %s", fm[G->targeter.filterMode % 4]);
    TreeSetItemText(MID_TARGETER, 3, b); }
    if (G->targeter.filterMode == 1 && !G->targeter.targetMobName.empty())
        swprintf(b,256,L"Mob Name: %s", G->targeter.targetMobName.c_str());
    else
        swprintf(b,256,L"Mob Name: -");
    TreeSetItemText(MID_TARGETER, 4, b);

    TreeSetItemText(MID_ATTACKER, 0, G->attacker.enabled ? L"Status: true" : L"Status: false");
    swprintf(b,256,L"Cooldown: %d ms", G->attacker.globalCooldownMs);
    TreeSetItemText(MID_ATTACKER, 1, b);
    { std::wstring keys;
    for (auto& s : G->attacker.skills) {
        if (!s.enabled || !s.keyBind) continue;
        if (!keys.empty()) keys += L", ";
        keys += (wchar_t)s.keyBind;
    }
    if (keys.empty()) swprintf(b,256,L"Skills: (click to set)");
    else swprintf(b,256,L"Skills: %s", keys.c_str());
    TreeSetItemText(MID_ATTACKER, 2, b); }

    TreeSetItemText(MID_HEALER, 0, G->healer.enabled ? L"Status: true" : L"Status: false");
    if (G->healer.targetName.empty())
        TreeSetItemText(MID_HEALER, 1, L"Target: (none)");
    else {
        swprintf(b,256,L"Target: %s", G->healer.targetName.c_str());
        TreeSetItemText(MID_HEALER, 1, b);
    }
    swprintf(b,256,L"Cooldown: %d ms", G->healer.cooldownMs);
    TreeSetItemText(MID_HEALER, 2, b);
    swprintf(b,256,L"Heal key: %c", G->healer.healKeyBind);
    TreeSetItemText(MID_HEALER, 3, b);
    { const wchar_t* hm[] = { L"Every cooldown", L"HP% below" };
    swprintf(b,256,L"Mode: %s", hm[G->healer.healMode & 1]);
    TreeSetItemText(MID_HEALER, 4, b); }
    swprintf(b,256,L"Min HP%%: %d", (int)G->healer.minHpPct);
    TreeSetItemText(MID_HEALER, 5, b);
    TreeSetItemText(MID_HEALER, 6, G->healer.selfHealEnabled ? L"Self Heal: ON" : L"Self Heal: OFF");
    swprintf(b,256,L"Self HP%%: %d", (int)G->healer.selfHpPct);
    TreeSetItemText(MID_HEALER, 7, b);
    swprintf(b,256,L"Self key: %c", G->healer.selfHealKey);
    TreeSetItemText(MID_HEALER, 8, b);

    TreeSetItemText(MID_FOLLOWER, 0, G->follower.enabled ? L"Status: true" : L"Status: false");
    if (G->follower.targetName.empty())
        TreeSetItemText(MID_FOLLOWER, 1, L"Target: (none)");
    else {
        swprintf(b,256,L"Target: %s", G->follower.targetName.c_str());
        TreeSetItemText(MID_FOLLOWER, 1, b);
    }
    swprintf(b,256,L"Distance: %d", (int)G->follower.desiredDistance);
    TreeSetItemText(MID_FOLLOWER, 2, b);
    swprintf(b,256,L"Max distance: %d", (int)G->follower.maxDistance);
    TreeSetItemText(MID_FOLLOWER, 3, b);

    TreeSetItemText(MID_LOOTER, 0, G->looter.enabled ? L"Status: true" : L"Status: false");
    swprintf(b,256,L"Radius: %d", (int)G->looter.walkRadius);
    TreeSetItemText(MID_LOOTER, 1, b);
    swprintf(b,256,L"Cooldown: %d ms", G->looter.cooldownMs);
    TreeSetItemText(MID_LOOTER, 2, b);
    swprintf(b,256,L"Max distance: %d", (int)G->looter.lootMaxDistance);
    TreeSetItemText(MID_LOOTER, 3, b);

    TreeSetItemText(MID_EXTRA, 0, G->extra.antiAfk ? L"Anti AFK: ON" : L"Anti AFK: OFF");
    TreeSetItemText(MID_EXTRA, 1, G->extra.autoRevive ? L"Auto Revive: ON" : L"Auto Revive: OFF");
    TreeSetItemText(MID_EXTRA, 2, G->extra.autoSell ? L"Auto Sell: ON" : L"Auto Sell: OFF");
    TreeSetItemText(MID_EXTRA, 3, G->extra.autoRepair ? L"Auto Repair: ON" : L"Auto Repair: OFF");

    TreeSetItemText(MID_DUNGEON, 0, G->dungeon.enabled ? L"Status: true" : L"Status: false");
    swprintf(b,256,L"Phase: %s", G->dungeon.PhaseName());
    TreeSetItemText(MID_DUNGEON, 1, b);
    swprintf(b,256,L"Portal: %s", G->dungeon.portal1Name.c_str());
    TreeSetItemText(MID_DUNGEON, 2, b);
    swprintf(b,256,L"Chest: %s", G->dungeon.chestName.c_str());
    TreeSetItemText(MID_DUNGEON, 3, b);
    swprintf(b,256,L"Exit: %s", G->dungeon.exitName.c_str());
    TreeSetItemText(MID_DUNGEON, 4, b);
    swprintf(b,256,L"Walk radius: %d", (int)G->dungeon.walkRadius);
    TreeSetItemText(MID_DUNGEON, 5, b);
    swprintf(b,256,L"Max distance: %d", (int)G->dungeon.maxDist);
    TreeSetItemText(MID_DUNGEON, 6, b);
    TreeSetItemText(MID_DUNGEON, 7, G->dungeon.lootInWaves ? L"Loot in waves: ON" : L"Loot in waves: OFF");
    swprintf(b,256,L"Loot tries: %d", G->dungeon.collectTries);
    TreeSetItemText(MID_DUNGEON, 8, b);
}

// ============================================================
// UI: TreeView config - handle item click
// ============================================================
void TreeHandleClick(NMTREEVIEWW* ntv) {
    if (!ntv) return;
    int idx = (int)ntv->itemNew.lParam;
    if (idx < 0 || idx >= (int)g_treeItems.size()) return;
    auto& td = g_treeItems[idx];

    switch (td.kind) {
    case TREE_TOGGLE:
        switch (td.module) {
        case MID_TARGETER:
            if (td.subId == 0) G->targeter.enabled = !G->targeter.enabled;
            else if (td.subId == 1) G->targeter.retargetOnNearby = !G->targeter.retargetOnNearby;
            else if (td.subId == 3) G->targeter.filterMode = (G->targeter.filterMode + 1) % 4;
            break;
        case MID_ATTACKER:
            if (td.subId == 0) G->attacker.enabled = !G->attacker.enabled;
            break;
        case MID_HEALER:
            if (td.subId == 0) G->healer.enabled = !G->healer.enabled;
            else if (td.subId == 4) G->healer.healMode = (G->healer.healMode + 1) % 2;
            else if (td.subId == 6) G->healer.selfHealEnabled = !G->healer.selfHealEnabled;
            break;
        case MID_FOLLOWER:
            if (td.subId == 0) G->follower.enabled = !G->follower.enabled;
            break;
        case MID_LOOTER:
            if (td.subId == 0) G->looter.enabled = !G->looter.enabled;
            break;
        case MID_EXTRA:
            if (td.subId == 0) G->extra.antiAfk = !G->extra.antiAfk;
            else if (td.subId == 1) G->extra.autoRevive = !G->extra.autoRevive;
            else if (td.subId == 2) G->extra.autoSell = !G->extra.autoSell;
            else if (td.subId == 3) G->extra.autoRepair = !G->extra.autoRepair;
            break;
        case MID_DUNGEON:
            if (td.subId == 0) G->dungeon.enabled = !G->dungeon.enabled;
            else if (td.subId == 7) G->dungeon.lootInWaves = !G->dungeon.lootInWaves;
            break;
        }
        RefreshTree();
        break;

    case TREE_VALUE: {
        int v = 0;
        switch (td.module) {
        case MID_TARGETER:
            if (td.subId == 2) { v = ShowInputInt(g_hWnd, L"Max Distance", (int)G->targeter.maxDistance); G->targeter.maxDistance = (float)v; }
            else if (td.subId == 4) {
                // Show input dialog for mob name
                wchar_t buf[256] = {};
                ShowInputString(g_hWnd, L"Mob Name (partial match)", G->targeter.targetMobName.c_str(), buf, 256);
                G->targeter.targetMobName = buf;
            }
            break;
        case MID_ATTACKER:
            if (td.subId == 1) { v = ShowInputInt(g_hWnd, L"Cooldown (ms)", G->attacker.globalCooldownMs); G->attacker.globalCooldownMs = v; }
            else if (td.subId == 2) {
                // Skill keys separated by comma (ex: 1, 3)
                wchar_t cur[128] = {}, buf[128] = {};
                bool first = true;
                for (auto& s : G->attacker.skills) {
                    if (!s.enabled || !s.keyBind) continue;
                    if (!first) wcscat_s(cur, L",");
                    wchar_t k[2] = { (wchar_t)s.keyBind, 0 };
                    wcscat_s(cur, k);
                    first = false;
                }
                ShowInputString(g_hWnd, L"Skill keys (ex: 1, 3)", cur, buf, 128);
                G->attacker.SetSkillKeys(buf);
            }
            break;
        case MID_HEALER:
            if (td.subId == 2) { v = ShowInputInt(g_hWnd, L"Cooldown (ms)", G->healer.cooldownMs); if(v>0) G->healer.cooldownMs = v; }
            else if (td.subId == 3) { v = ShowInputInt(g_hWnd, L"Heal Key (1-9)", G->healer.healKeyBind - 0x30); if(v>=1&&v<=9) G->healer.healKeyBind = 0x30+v; }
            else if (td.subId == 5) { v = ShowInputInt(g_hWnd, L"Min HP%", (int)G->healer.minHpPct); G->healer.minHpPct = (float)v; }
            else if (td.subId == 7) { v = ShowInputInt(g_hWnd, L"Self HP%", (int)G->healer.selfHpPct); G->healer.selfHpPct = (float)v; }
            else if (td.subId == 8) { v = ShowInputInt(g_hWnd, L"Self Heal Key (1-9)", G->healer.selfHealKey - 0x30); if(v>=1&&v<=9) G->healer.selfHealKey = 0x30+v; }
            break;
        case MID_FOLLOWER:
            if (td.subId == 2) { v = ShowInputInt(g_hWnd, L"Desired Distance", (int)G->follower.desiredDistance); G->follower.desiredDistance = (float)v; }
            else if (td.subId == 3) { v = ShowInputInt(g_hWnd, L"Max Distance", (int)G->follower.maxDistance); G->follower.maxDistance = (float)v; }
            break;
        case MID_LOOTER:
            if (td.subId == 1) { v = ShowInputInt(g_hWnd, L"Loot Radius", (int)G->looter.walkRadius); G->looter.walkRadius = (float)v; }
            else if (td.subId == 2) { v = ShowInputInt(g_hWnd, L"Cooldown (ms)", G->looter.cooldownMs); G->looter.cooldownMs = v; }
            else if (td.subId == 3) { v = ShowInputInt(g_hWnd, L"Max Distance", (int)G->looter.lootMaxDistance); G->looter.lootMaxDistance = (float)v; }
            break;
        case MID_DUNGEON: {
            wchar_t buf[128] = {};
            if (td.subId == 2) { ShowInputString(g_hWnd, L"Portal name (walkable)", G->dungeon.portal1Name.c_str(), buf, 128); if (buf[0]) G->dungeon.portal1Name = buf; }
            else if (td.subId == 3) { ShowInputString(g_hWnd, L"Chest name", G->dungeon.chestName.c_str(), buf, 128); if (buf[0]) G->dungeon.chestName = buf; }
            else if (td.subId == 4) { ShowInputString(g_hWnd, L"Exit name", G->dungeon.exitName.c_str(), buf, 128); if (buf[0]) G->dungeon.exitName = buf; }
            else if (td.subId == 5) { v = ShowInputInt(g_hWnd, L"Walk radius", (int)G->dungeon.walkRadius); if (v > 0) G->dungeon.walkRadius = (float)v; }
            else if (td.subId == 6) { v = ShowInputInt(g_hWnd, L"Max distance", (int)G->dungeon.maxDist); if (v > 0) G->dungeon.maxDist = (float)v; }
            else if (td.subId == 8) { v = ShowInputInt(g_hWnd, L"Loot tries (chest)", G->dungeon.collectTries); if (v > 0) G->dungeon.collectTries = v; }
            break; }
        }
        RefreshTree();
        break;
    }

    case TREE_SELECT:
        if (td.module == MID_FOLLOWER && td.subId == 1) {
            std::vector<std::wstring> options;
            options.push_back(L"(none) - clear target");
            for (auto& p : G->cachedPlayers) {
                if (p.hp <= 0) continue;
                wchar_t entry[128];
                swprintf(entry, 128, L"%s (Lv.%d, HP:%d/%d, %.0fm)", p.name, p.level, p.hp, p.maxHp, p.distance);
                options.push_back(entry);
            }
            if (options.size() == 1) {
                MessageBoxW(g_hWnd, L"No players nearby to follow.", L"Follower", MB_OK|MB_ICONINFORMATION);
                break;
            }
            int sel = ShowSelectionList(g_hWnd, L"Select player to follow", L"Pick a target:", options);
            if (sel == 0) {
                G->follower.targetName.clear();
                G->follower.targetAddr = 0;
            } else if (sel > 0 && sel < (int)G->cachedPlayers.size() + 1) {
                G->follower.targetName = G->cachedPlayers[sel - 1].name;
                G->follower.targetAddr = G->cachedPlayers[sel - 1].objAddr;
            }
            RefreshTree();
        }
        else if (td.module == MID_HEALER && td.subId == 1) {
            std::vector<std::wstring> options;
            options.push_back(L"(none) - clear target");
            for (auto& p : G->cachedPlayers) {
                if (p.hp <= 0) continue;
                wchar_t entry[128];
                swprintf(entry, 128, L"%s (Lv.%d, HP:%d/%d, %.0fm)", p.name, p.level, p.hp, p.maxHp, p.distance);
                options.push_back(entry);
            }
            if (options.size() == 1) {
                MessageBoxW(g_hWnd, L"No players nearby.", L"Healer", MB_OK|MB_ICONINFORMATION);
                break;
            }
            int sel = ShowSelectionList(g_hWnd, L"Select player to heal", L"Pick a target:", options);
            if (sel == 0) {
                G->healer.targetName.clear();
                G->healer.targetAddr = 0;
            } else if (sel > 0 && sel < (int)G->cachedPlayers.size() + 1) {
                G->healer.targetName = G->cachedPlayers[sel - 1].name;
                G->healer.targetAddr = G->cachedPlayers[sel - 1].objAddr;
            }
            RefreshTree();
        }
        break;
    }
}

// ============================================================
// UI: Panel subclass to forward WM_COMMAND to main window
// ============================================================
LRESULT CALLBACK PanelWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_hWnd) {
        if (m == WM_COMMAND) return SendMessageW(g_hWnd, WM_COMMAND, w, l);
        if (m == WM_NOTIFY)  return SendMessageW(g_hWnd, WM_NOTIFY, w, l);
        if (m == WM_CTLCOLORSTATIC) return SendMessageW(g_hWnd, WM_CTLCOLORSTATIC, w, l);
        if (m == WM_CTLCOLORLISTBOX) return SendMessageW(g_hWnd, WM_CTLCOLORLISTBOX, w, l);
        if (m == WM_HSCROLL || m == WM_VSCROLL) return SendMessageW(g_hWnd, m, w, l);
        if (m == WM_ERASEBKGND) {
            RECT rc; GetClientRect(h, &rc);
            FillRect((HDC)w, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
            return 1;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static const wchar_t* PANEL_CLASS = L"WsBotPanel";

void RegisterPanelClass() {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = PANEL_CLASS;
    RegisterClassExW(&wc);
}

// ============================================================
// UI: Tab switching
// ============================================================
void SwitchTab(TabID tab) {
    g_currentTab = tab;
    for (int i = 0; i < 3; i++)
        ShowWindow(g_hTabPanel[i], i == (int)tab ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

// ============================================================
// UI: Create tab bar (Config | Quick actions | Connection)
// ============================================================
void CreateTabBar(HWND parent) {
    int bw = 100, bh = 22, y = 2;
    const wchar_t* labels[] = { L"Config", L"Quick actions", L"Connection" };
    for (int i = 0; i < 3; i++) {
        g_hTabBtn[i] = CreateWindowExW(0, L"STATIC", labels[i],
            WS_CHILD|WS_VISIBLE|SS_CENTER|SS_NOTIFY,
            4 + i * bw, y, bw, bh, parent, (HMENU)(2100 + i), g_hInst, NULL);
        SetFont(g_hTabBtn[i]);
    }
}

// ============================================================
// UI: Create Config tab (TreeView)
// ============================================================
void CreateConfigPanel(HWND parent) {
    g_hTabPanel[TAB_CONFIG] = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD|WS_VSCROLL, 0, 26, 350, 544, parent, NULL, g_hInst, NULL);

    g_hTree = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_CHILD|WS_VISIBLE|TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_NOHSCROLL,
        2, 2, 346, 540, g_hTabPanel[TAB_CONFIG], (HMENU)1200, g_hInst, NULL);
    SendMessageW(g_hTree, WM_SETFONT, (WPARAM)g_hTreeFont, TRUE);
    TreeView_SetIndent(g_hTree, 20);
    SendMessageW(g_hTree, TVM_SETEXTENDEDSTYLE, 0x0004, 0x0004); // TVS_EX_DOUBLEBUFFER: menos flicker

    g_treeItems.clear();
    for (int m = 0; m < MOD_COUNT; m++) {
        g_hTreeParent[m] = NULL;
        g_treeChildCount[m] = 0;
    }

    // Targeter
    g_hTreeParent[MID_TARGETER] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_TARGETER], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_TARGETER, TREE_TOGGLE, 0});
    g_hTreeChild[MID_TARGETER][g_treeChildCount[MID_TARGETER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_TARGETER], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_TARGETER, TREE_TOGGLE, 1});
    g_hTreeChild[MID_TARGETER][g_treeChildCount[MID_TARGETER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_TARGETER], L"Retarget on nearby: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_TARGETER, TREE_VALUE, 2});
    g_hTreeChild[MID_TARGETER][g_treeChildCount[MID_TARGETER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_TARGETER], L"Max distance: 30", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_TARGETER, TREE_TOGGLE, 3});
    g_hTreeChild[MID_TARGETER][g_treeChildCount[MID_TARGETER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_TARGETER], L"Filter: All", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_TARGETER, TREE_VALUE, 4});
    g_hTreeChild[MID_TARGETER][g_treeChildCount[MID_TARGETER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_TARGETER], L"Mob Name: -", idx); }

    // Attacker
    g_hTreeParent[MID_ATTACKER] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_ATTACKER], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_ATTACKER, TREE_TOGGLE, 0});
    g_hTreeChild[MID_ATTACKER][g_treeChildCount[MID_ATTACKER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_ATTACKER], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_ATTACKER, TREE_VALUE, 1});
    g_hTreeChild[MID_ATTACKER][g_treeChildCount[MID_ATTACKER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_ATTACKER], L"Cooldown: 1500 ms", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_ATTACKER, TREE_VALUE, 2});
    g_hTreeChild[MID_ATTACKER][g_treeChildCount[MID_ATTACKER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_ATTACKER], L"Skills: (click to set)", idx); }

    // Healer
    g_hTreeParent[MID_HEALER] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_HEALER], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_TOGGLE, 0});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_SELECT, 1});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Target: (click to select)", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_VALUE, 2});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Cooldown: 2000 ms", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_VALUE, 3});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Heal key: 1", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_TOGGLE, 4});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Mode: HP% below", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_VALUE, 5});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Min HP%: 60", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_TOGGLE, 6});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Self Heal: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_VALUE, 7});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Self HP%: 50", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_HEALER, TREE_VALUE, 8});
    g_hTreeChild[MID_HEALER][g_treeChildCount[MID_HEALER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_HEALER], L"Self key: 1", idx); }

    // Follower
    g_hTreeParent[MID_FOLLOWER] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_FOLLOWER], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_FOLLOWER, TREE_TOGGLE, 0});
    g_hTreeChild[MID_FOLLOWER][g_treeChildCount[MID_FOLLOWER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_FOLLOWER], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_FOLLOWER, TREE_SELECT, 1});
    g_hTreeChild[MID_FOLLOWER][g_treeChildCount[MID_FOLLOWER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_FOLLOWER], L"Target: (click to select)", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_FOLLOWER, TREE_VALUE, 2});
    g_hTreeChild[MID_FOLLOWER][g_treeChildCount[MID_FOLLOWER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_FOLLOWER], L"Distance: 3", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_FOLLOWER, TREE_VALUE, 3});
    g_hTreeChild[MID_FOLLOWER][g_treeChildCount[MID_FOLLOWER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_FOLLOWER], L"Max distance: 30", idx); }

    // Looter
    g_hTreeParent[MID_LOOTER] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_LOOTER], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_LOOTER, TREE_TOGGLE, 0});
    g_hTreeChild[MID_LOOTER][g_treeChildCount[MID_LOOTER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_LOOTER], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_LOOTER, TREE_VALUE, 1});
    g_hTreeChild[MID_LOOTER][g_treeChildCount[MID_LOOTER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_LOOTER], L"Radius: 10", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_LOOTER, TREE_VALUE, 2});
    g_hTreeChild[MID_LOOTER][g_treeChildCount[MID_LOOTER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_LOOTER], L"Cooldown: 1200 ms", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_LOOTER, TREE_VALUE, 3});
    g_hTreeChild[MID_LOOTER][g_treeChildCount[MID_LOOTER]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_LOOTER], L"Max distance: 25", idx); }

    // Extra
    g_hTreeParent[MID_EXTRA] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_EXTRA], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_EXTRA, TREE_TOGGLE, 0});
    g_hTreeChild[MID_EXTRA][g_treeChildCount[MID_EXTRA]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_EXTRA], L"Anti AFK: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_EXTRA, TREE_TOGGLE, 1});
    g_hTreeChild[MID_EXTRA][g_treeChildCount[MID_EXTRA]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_EXTRA], L"Auto Revive: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_EXTRA, TREE_TOGGLE, 2});
    g_hTreeChild[MID_EXTRA][g_treeChildCount[MID_EXTRA]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_EXTRA], L"Auto Sell: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_EXTRA, TREE_TOGGLE, 3});
    g_hTreeChild[MID_EXTRA][g_treeChildCount[MID_EXTRA]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_EXTRA], L"Auto Repair: OFF", idx); }

    // Dungeon
    g_hTreeParent[MID_DUNGEON] = TreeAddItem(g_hTree, TVI_ROOT, MOD_NAMES[MID_DUNGEON], -1);
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_TOGGLE, 0});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Status: false", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_TOGGLE, 1});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Phase: WAVE1", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 2});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Portal: Passagem", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 3});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Chest: Ba\u00FA", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 4});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Exit: Sa\u00EDda", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 5});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Walk radius: 10", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 6});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Max distance: 25", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_TOGGLE, 7});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Loot in waves: OFF", idx); }
    { int idx = (int)g_treeItems.size(); g_treeItems.push_back({MID_DUNGEON, TREE_VALUE, 8});
    g_hTreeChild[MID_DUNGEON][g_treeChildCount[MID_DUNGEON]++] = TreeAddItem(g_hTree, g_hTreeParent[MID_DUNGEON], L"Loot tries: 6", idx); }

    TreeExpandAll();
}

void RefreshAccordion() {
    RefreshTree();
}

// ============================================================
// UI: Create Quick Actions tab
// ============================================================
void CreateQuickPanel(HWND parent) {
    g_hTabPanel[TAB_QUICK] = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD, 0, 26, 340, 370, parent, NULL, g_hInst, NULL);

    int bw = 150, bh = 26, x = 10, y = 10;
    int quickCmds[] = { IDM_TOGGLE_ATTACK, IDM_TOGGLE_HEAL, IDM_TOGGLE_FOLLOW,
        IDM_TOGGLE_LOOT, IDM_TOGGLE_ALL, IDM_STOP_ALL, IDM_SCALE_UP, IDM_SCALE_DOWN };
    for (int i = 0; i < 8; i++) {
        g_hQuickBtn[i] = CreateWindowExW(0, L"button", QUICK_LABELS[i],
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, y, bw, bh, g_hTabPanel[TAB_QUICK], (HMENU)quickCmds[i], g_hInst, NULL);
        SetFont(g_hQuickBtn[i]);
        y += 32;
    }
}

// ============================================================
// UI: Create Connection tab
// ============================================================
void CreateConnPanel(HWND parent) {
    g_hTabPanel[TAB_CONN] = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD, 0, 26, 340, 370, parent, NULL, g_hInst, NULL);

    int x = 8, y = 5;
    HWND hLbl = CreateWindowExW(0, L"static", L"Processes:", WS_CHILD|WS_VISIBLE,
        x, y, 200, 18, g_hTabPanel[TAB_CONN], NULL, g_hInst, NULL);
    SetFont(hLbl); y += 20;

    g_hProcList = CreateWindowExW(0, L"listbox", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
        x, y, 320, 160, g_hTabPanel[TAB_CONN], NULL, g_hInst, NULL);
    SetFont(g_hProcList); y += 166;

    g_hBtnRefresh = CreateWindowExW(0, L"button", L"Refresh",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x, y, 70, 24, g_hTabPanel[TAB_CONN], (HMENU)IDM_REFRESH, g_hInst, NULL);
    g_hBtnConnect = CreateWindowExW(0, L"button", L"Connect",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x + 75, y, 70, 24, g_hTabPanel[TAB_CONN], (HMENU)IDM_CONNECT, g_hInst, NULL);
    SetFont(g_hBtnRefresh); SetFont(g_hBtnConnect); y += 30;

    HWND hDllLbl = CreateWindowExW(0, L"static", L"DLL:", WS_CHILD|WS_VISIBLE,
        x, y + 2, 30, 18, g_hTabPanel[TAB_CONN], NULL, g_hInst, NULL);
    SetFont(hDllLbl);
    g_hDllPath = CreateWindowExW(0, L"edit", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        x + 32, y, 210, 22, g_hTabPanel[TAB_CONN], NULL, g_hInst, NULL);
    SetFont(g_hDllPath);
    g_hBtnBrowse = CreateWindowExW(0, L"button", L"...",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x + 245, y, 30, 22, g_hTabPanel[TAB_CONN], (HMENU)IDM_BROWSE_DLL, g_hInst, NULL);
    g_hBtnInject = CreateWindowExW(0, L"button", L"Inject",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x + 280, y, 48, 22, g_hTabPanel[TAB_CONN], (HMENU)IDM_INJECT, g_hInst, NULL);
    SetFont(g_hBtnBrowse); SetFont(g_hBtnInject);
}

// ============================================================
// UI: Update UI (called on timer)
// ============================================================
// ============================================================
// Icone da classe: barra de titulo (WM_SETICON SMALL) + status bar (SB_SETICON)
// Muda so quando o classId muda; arquivos em config\class-icons\NN_*.ico
// ============================================================
static HICON g_hClassIcon = NULL;
static int   g_classIconId = -1;

static const wchar_t* kClassIconFile(int classId) {
    static const wchar_t* files[] = {
        L"", L"01_paladino.ico", L"02_sacerdote.ico", L"03_mago.ico", L"04_barbaro.ico",
        L"05_ladino.ico", L"06_xama.ico", L"07_dancarino_da_lamina.ico", L"08_patrulheiro.ico",
        L"09_druida.ico", L"10_cavaleiro_da_morte.ico", L"11_necromante.ico", L"12_bruxo.ico",
        L"13_explorador.ico", L"14_cacador.ico", L"15_guarda.ico", L"16_encantador.ico",
        L"17_templario.ico", L"18_cacique.ico", L"19_invocador_de_feras.ico", L"20_ceifeiro.ico"
    };
    if (classId < 1 || classId > 20) return L"";
    return files[classId];
}

static void SetClassIcon(int classId) {
    if (classId == g_classIconId) return;
    g_classIconId = classId;

    if (g_hClassIcon) {
        if (g_hStatus) SendMessageW(g_hStatus, SB_SETICON, 0, 0);
        DestroyIcon(g_hClassIcon);
        g_hClassIcon = NULL;
    }

    HICON hTitleIco = g_hAppIconSmall;  // titulo volta pro icone do app
    const wchar_t* f = kClassIconFile(classId);
    if (f[0]) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* bs = wcsrchr(path, L'\\'); if (bs) *bs = 0;
        wcscat_s(path, L"\\config\\class-icons\\");
        wcscat_s(path, f);
        g_hClassIcon = (HICON)LoadImageW(NULL, path, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        if (g_hClassIcon) {
            hTitleIco = g_hClassIcon;
            if (g_hStatus) SendMessageW(g_hStatus, SB_SETICON, 0, (LPARAM)g_hClassIcon);
        }
    }
    if (g_hWnd && hTitleIco)
        SendMessageW(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hTitleIco);
}

// Mostra os stats no status bar; se nao couber na parte 0, rola em carrossel
// (da direita para a esquerda) com looping suave via padding de espacos.
static void SetStatusStats(const wchar_t* text) {
    if (!g_hStatus || !text || !text[0]) return;

    if (g_carouselStart == 0) g_carouselStart = GetTickCount();

    // Mede o texto com a fonte atual
    HDC hdc = GetDC(g_hStatus);
    if (g_hFont) SelectObject(hdc, g_hFont);
    SIZE sz = {0,0};
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &sz);
    int contentW = sz.cx;
    ReleaseDC(g_hStatus, hdc);

    // Cabe na parte 0? → texto estatico (sem necessidade de scroll)
    int avail = g_statusPart0W - (g_hClassIcon ? 18 : 0);  // SB_SETICON reserva espaco
    if (avail <= 0 || contentW <= avail - 16) {
        SetWindowTextW(g_hStatus, text);
        return;
    }

    // Texto + separador; rotaciona a partir do offset (1 char a cada 70ms)
    g_statsFull = L"    ";
    g_statsFull += text;
    g_statsFull += L"    ";
    int len = (int)g_statsFull.size();
    DWORD now = GetTickCount();
    int off = (int)(((now - g_carouselStart) / 70) % (DWORD)len);
    std::wstring disp = g_statsFull.substr(off) + g_statsFull.substr(0, off);
    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)disp.c_str());
}

void UpdateUI() {
    if (!g_connected || !g_hProcess) {
        SetClassIcon(0);
        SetWindowTextW(g_hStatus, g_hProcess ? L"Connected" : L"Select Warspear and connect");
        return;
    }
    float sx,sy; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
    std::vector<EntityData> pl,mb,np;
    std::vector<CorpseData> corpses;
    DWORD playerAddr=0, gmAddr=0;
    if (!ReadGameState(sx,sy,hp,mhp,mn,mmn,name,level,classId,pl,mb,np,corpses,&playerAddr,&gmAddr)) {
        SetClassIcon(0);
        SetWindowTextW(g_hStatus, L"Cannot read game memory");
        g_connected = false;
        return;
    }
    g_selfX = sx; g_selfY = sy;
    G->cachedCorpses = corpses;
    G->cachedPlayers = pl; G->cachedMobs = mb; G->cachedNpcs = np;
    g_playerAddr = playerAddr; g_gmAddr = gmAddr;

    wchar_t buf[512];
    swprintf(buf, 512, L"%s | Lv.%d %s | HP: %d/%d | MP: %d/%d | Pos: %.0f,%.0f | P:%d M:%d N:%d | Corpos: %d",
        name.c_str(), level, GetClassName(classId), hp, mhp, mn, mmn, sx, sy,
        (int)pl.size(), (int)mb.size(), (int)np.size(), (int)corpses.size());
    SetStatusStats(buf);
    SetClassIcon(classId);

    static std::wstring lastCharName;
    if (name != lastCharName) {
        wchar_t wtitle[128];
        swprintf(wtitle, 128, L"%s [Lv.%d %s]", name.c_str(), level, GetClassName(classId));
        SetWindowTextW(g_hWnd, wtitle);
        lastCharName = name;
    }

    GameContext ctx = BuildContext();

    // PRIORIDADE DE CURA (heal > loot > attack): enquanto o HP do proprio char
    // ou do alvo estiver abaixo do limite definido, attacker e looter cedem o
    // tick (o healer ja roda primeiro por causa da ordem dos modulos).
    // Teto de 15s: se a cura nao surtir efeito (alcance/mana/alvo fora),
    // nunca trava o bot — solta o combate ate o HP subir de novo.
    static DWORD holdStartTick = 0;
    static bool  holdReleased = false;
    bool healUrgent = G->healer.enabled && G->healer.NeedsHeal(ctx);
    if (healUrgent) {
        if (holdStartTick == 0) {
            holdStartTick = ctx.tickCount;
            DebugLog("[HEAL] HP below threshold - holding attack/loot");
        } else if (!holdReleased && ctx.tickCount - holdStartTick > 15000) {
            holdReleased = true;
            DebugLog("[HEAL] Hold timeout 15s (HP still low) - releasing attack/loot");
        }
    } else {
        holdStartTick = 0;
        holdReleased = false;
    }
    ctx.holdCombat = healUrgent && !holdReleased;

    G->modMgr.TickAll(ctx);

    // Tree labels are static otherwise - refresh so Dungeon phase/status stay live
    static DWORD lastTreeRefresh = 0;
    if (ctx.tickCount - lastTreeRefresh > 1000) {
        lastTreeRefresh = ctx.tickCount;
        RefreshTree();
    }

    // Attacker gave up on a target that never offered the attack flag (NPC/friendly)
    if (G->attacker.lastFailedAddr > 0x1000) {
        G->targeter.MarkSkipped(G->attacker.lastFailedAddr);
        DebugLog("[TARGETER] Parking unattackable target 0x%08X for 60s", G->attacker.lastFailedAddr);
        G->attacker.lastFailedAddr = 0;
    }

    // Sync AFTER TickAll so Targeter has already updated selectedAddr
    if (G->targeter.enabled && G->targeter.selectedAddr > 0x1000)
        G->attacker.targetAddr = G->targeter.selectedAddr;
    else if (G->targeter.enabled)
        G->attacker.targetAddr = 0;

    // Periodic stats (every 10 seconds)
    DWORD now = GetTickCount();
    if (now - g_lastStatsTick > 10000) {
        g_lastStatsTick = now;
        DebugLog("[STATS] ====================================");
        DebugLog("[STATS] Position: (%.1f, %.1f) | Mobs: %d | Players: %d", g_selfX, g_selfY, (int)G->cachedMobs.size(), (int)G->cachedPlayers.size());
        DebugLog("[STATS] Target: 0x%08X | Follower: 0x%08X", G->attacker.targetAddr, G->follower.targetAddr);
        DebugLog("[STATS] Corpses: %d | Loots: %d", (int)G->cachedCorpses.size(), G->looter.lootCount);
        DebugLog("[STATS] Modules: T=%d A=%d H=%d F=%d L=%d",
            G->targeter.enabled, G->attacker.enabled, G->healer.enabled, G->follower.enabled, G->looter.enabled);
        DebugLog("[STATS] ====================================");
    }
}

// ============================================================
// WndProc
// ============================================================
// Status bar with 2 parts: left = status text, right = fixed credit footer
static void LayoutStatusBar(HWND parent) {
    if (!g_hStatus) return;
    RECT rc; GetClientRect(parent, &rc);
    const wchar_t* credit = L"  Dev By Richard W.";
    HDC hdc = GetDC(g_hStatus);
    if (g_hFont) SelectObject(hdc, g_hFont);
    SIZE sz = {0,0};
    GetTextExtentPoint32W(hdc, credit, lstrlenW(credit), &sz);
    ReleaseDC(g_hStatus, hdc);
    int creditW = sz.cx + 24;
    int left = rc.right - creditW;
    if (left < 0) left = 0;
    g_statusPart0W = left;   // largura disponivel p/ o carrossel de stats
    int parts[2] = { left, -1 };
    SendMessageW(g_hStatus, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageW(g_hStatus, SB_SETTEXTW, 1, (LPARAM)credit);
}

// ============================================================
// UI: TreeView custom draw — ON/true em verde, OFF/false em vermelho
// (NM_CUSTOMDRAW pinta na hora do desenho: sem timer, sem repaint extra)
// ============================================================
static bool ModuleEnabledByMid(int m) {
    if (!G) return false;
    switch (m) {
    case MID_TARGETER: return G->targeter.enabled;
    case MID_ATTACKER: return G->attacker.enabled;
    case MID_HEALER:   return G->healer.enabled;
    case MID_FOLLOWER: return G->follower.enabled;
    case MID_LOOTER:   return G->looter.enabled;
    case MID_EXTRA:    return G->extra.enabled;
    case MID_DUNGEON:  return G->dungeon.enabled;
    }
    return false;
}

static LRESULT TreeCustomDraw(NMTVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT: {
        HTREEITEM hItem = (HTREEITEM)cd->nmcd.dwItemSpec;
        if (!hItem || !g_hTree) return CDRF_DODEFAULT;

        // Item pai (nome do modulo): verde quando ligado, cinza quando desligado
        for (int m = 0; m < MOD_COUNT; m++) {
            if (hItem == g_hTreeParent[m]) {
                cd->clrText = ModuleEnabledByMid(m) ? RGB(0, 130, 0) : RGB(120, 120, 120);
                return CDRF_NEWFONT;
            }
        }

        // Itens com valor booleano: ": true"/": ON" verde, ": false"/": OFF" vermelho
        wchar_t txt[128] = {};
        TVITEMW ti{};
        ti.hItem = hItem;
        ti.mask = TVIF_TEXT;
        ti.pszText = txt;
        ti.cchTextMax = 128;
        SendMessageW(g_hTree, TVM_GETITEMW, 0, (LPARAM)&ti);

        if (wcsstr(txt, L": true") || wcsstr(txt, L": ON")) {
            cd->clrText = RGB(0, 130, 0);
            return CDRF_NEWFONT;
        }
        if (wcsstr(txt, L": false") || wcsstr(txt, L": OFF")) {
            cd->clrText = RGB(190, 30, 30);
            return CDRF_NEWFONT;
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitFont();

        // Icone da janela/barra de tarefas: recurso embutido (warspear.rc)
        // ou warspear.ico ao lado do exe (fallback p/ builds sem .rc)
        {
            HICON hIco = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
            if (!hIco) {
                wchar_t iconPath[MAX_PATH];
                GetModuleFileNameW(NULL, iconPath, MAX_PATH);
                wchar_t* ip = wcsrchr(iconPath, L'\\'); if (ip) ip[1] = 0;
                wcscat_s(iconPath, L"warspear.ico");
                hIco = (HICON)LoadImageW(NULL, iconPath, IMAGE_ICON, 0, 0,
                                         LR_LOADFROMFILE | LR_DEFAULTSIZE);
            }
            if (hIco) {
                SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIco);
                SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                g_hAppIconSmall = hIco;
            }
        }

        CreateTabBar(hWnd);
        CreateConfigPanel(hWnd);
        CreateQuickPanel(hWnd);
        CreateConnPanel(hWnd);

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        LayoutStatusBar(hWnd);

        G->modMgr.Add(&G->healer);
        G->modMgr.Add(&G->dungeon);
        G->modMgr.Add(&G->follower);
        G->modMgr.Add(&G->looter);
        G->modMgr.Add(&G->targeter);
        G->modMgr.Add(&G->attacker);
        G->modMgr.Add(&G->extra);

        wchar_t cfgDir[MAX_PATH];
        GetModuleFileNameW(NULL, cfgDir, MAX_PATH);
        wchar_t* bs = wcsrchr(cfgDir, L'\\'); if (bs) *bs = 0;
        wcscat(cfgDir, L"\\config");
        G->modMgr.LoadAll(cfgDir);
        ReloadNpcNames();
        ReloadBossNames();

        RefreshAccordion();
        SwitchTab(TAB_CONFIG);
        RefreshProcesses(g_hProcList);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        // Tab buttons
        if (id >= 2100 && id <= 2102) {
            SwitchTab((TabID)(id - 2100));
            break;
        }

        // Tab panel buttons (Forward to parent for tab buttons)
        switch (id) {
        case IDM_TOGGLE_ATTACK:
            G->targeter.enabled = !G->targeter.enabled;
            G->attacker.enabled = G->targeter.enabled;
            { wchar_t s[64]; swprintf(s,64,L"Attack: %s", G->attacker.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_HEAL:
            G->healer.enabled = !G->healer.enabled;
            { wchar_t s[64]; swprintf(s,64,L"Heal: %s", G->healer.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_FOLLOW:
            G->follower.enabled = !G->follower.enabled;
            { wchar_t s[64]; swprintf(s,64,L"Follow: %s", G->follower.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_LOOT:
            G->looter.enabled = !G->looter.enabled;
            { wchar_t s[64]; swprintf(s,64,L"Loot: %s", G->looter.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_ALL: {
            bool on = !(G->targeter.enabled && G->attacker.enabled && G->healer.enabled && G->looter.enabled);
            G->targeter.enabled = on; G->attacker.enabled = on;
            G->healer.enabled = on; G->looter.enabled = on; G->follower.enabled = on;
            SetWindowTextW(g_hStatus, on ? L"ALL ON" : L"ALL OFF");
            break;
        }

        case IDM_STOP_ALL:
            G->targeter.enabled = false; G->attacker.enabled = false;
            G->healer.enabled = false;
            G->looter.enabled = false; G->follower.enabled = false;
            G->extra.enabled = false;
            G->dungeon.enabled = false;
            G->attacker.targetAddr = 0; G->follower.targetAddr = 0; G->healer.targetAddr = 0;
            SetWindowTextW(g_hStatus, L"ALL STOPPED");
            DebugLog("[STOP] All modules stopped");
            break;

        case IDM_SCALE_UP:
            g_scale += 0.5f;
            { wchar_t s[64]; swprintf(s,64,L"Scale: %.2f", g_scale);
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_SCALE_DOWN:
            g_scale -= 0.5f;
            if (g_scale < 0.5f) g_scale = 0.5f;
            { wchar_t s[64]; swprintf(s,64,L"Scale: %.2f", g_scale);
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_REFRESH:
            if (g_hProcList) RefreshProcesses(g_hProcList);
            break;

        case IDM_CONNECT: {
            if (!g_hProcList) break;
            int sel = (int)SendMessageW(g_hProcList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR || sel >= (int)G->listToProc.size()) {
                MessageBoxW(hWnd, L"Select a process first!", L"", MB_OK|MB_ICONWARNING);
                break;
            }
            int procIdx = G->listToProc[sel];
            if (procIdx < 0 || procIdx >= (int)G->procs.size()) break;
            DWORD pid = G->procs[procIdx].pid;
            if (g_hProcess) CloseHandle(g_hProcess);
            g_hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION, FALSE, pid);
            g_gamePid = pid;
            if (!g_hProcess) {
                MessageBoxW(hWnd, L"Cannot open process.\nTry running as Administrator.", L"Error", MB_OK|MB_ICONERROR);
                break;
            }
            float x,y; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
            std::vector<EntityData> p,m,n; std::vector<CorpseData> corpses;
            if (ReadGameState(x,y,hp,mhp,mn,mmn,name,level,classId,p,m,n,corpses)) {
                g_connected = true;
                OpenDebugConsole(hWnd);
                wchar_t wtitle[128];
                swprintf(wtitle,128,L"%s [Lv.%d %s]", name.c_str(), level, GetClassName(classId));
                SetWindowTextW(hWnd, wtitle);
                DebugLog("[CONNECT] SUCCESS - %S Lv.%d", name.c_str(), level);
                ReloadNpcNames();   // re-read config\npc_names.json on every Connect
                ReloadBossNames();  // re-read config\boss_names.json on every Connect
                G->modMgr.StartAll();
                if (!m.empty()) G->attacker.targetAddr = m[0].objAddr;
                if (!p.empty()) { G->follower.targetAddr = p[0].objAddr; G->follower.targetName = p[0].name; }
            } else {
                MessageBoxW(hWnd, L"Cannot read game memory.", L"Warning", MB_OK|MB_ICONWARNING);
            }
            break;
        }

        case IDM_BROWSE_DLL: {
            OPENFILENAMEW ofn{}; wchar_t file[MAX_PATH]={};
            ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hWnd;
            ofn.lpstrFilter=L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
            ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_hDllPath, file);
            break;
        }

        case IDM_INJECT: {
            if (!g_hProcess || !g_connected) {
                MessageBoxW(hWnd, L"Connect first!", L"", MB_OK|MB_ICONWARNING);
                break;
            }
            if (!G->hSharedMem) {
                G->hSharedMem = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BotCmd), L"Local\\WarspearBotShared");
                if (G->hSharedMem) G->pBotCmd = (BotCmd*)MapViewOfFile(G->hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd));
            }
            if (!G->pBotCmd) {
                MessageBoxW(hWnd, L"Cannot create shared memory.", L"Error", MB_OK|MB_ICONERROR);
                break;
            }
            wchar_t dll[MAX_PATH]; GetWindowTextW(g_hDllPath, dll, MAX_PATH);
            if (dll[0] == 0) {
                MessageBoxW(hWnd, L"Enter DLL path.", L"", MB_OK|MB_ICONWARNING);
                break;
            }
            if (!wcschr(dll, L':')) {
                wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH);
                wchar_t* b = wcsrchr(dir, L'\\'); if (b) *b = 0;
                wchar_t full[MAX_PATH]; swprintf(full,MAX_PATH,L"%s\\%s", dir, dll);
                wcscpy(dll, full);
            }
            if (InjectDLL(g_gamePid, dll)) {
                g_dllInjected = true;
                DebugLog("[INJECT] SUCCESS");
            } else {
                MessageBoxW(hWnd, L"DLL injection failed.", L"Error", MB_OK|MB_ICONERROR);
            }
            break;
        }

        case IDM_DEBUG:
            OpenDebugConsole(hWnd);
            break;
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR* nmh = (NMHDR*)lParam;
        if (nmh->idFrom == 1200 && nmh->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW* ntv = (NMTREEVIEWW*)lParam;
            TreeHandleClick(ntv);
        } else if (nmh->hwndFrom == g_hTree && nmh->code == NM_CUSTOMDRAW) {
            return TreeCustomDraw((NMTVCUSTOMDRAW*)lParam);
        }
        break;
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_F1: SendMessage(hWnd, WM_COMMAND, IDM_TOGGLE_ATTACK, 0); break;
        case VK_F2: SendMessage(hWnd, WM_COMMAND, IDM_STOP_ALL, 0); break;
        case VK_F3: SendMessage(hWnd, WM_COMMAND, IDM_TOGGLE_FOLLOW, 0); break;
        case VK_F4: SendMessage(hWnd, WM_COMMAND, IDM_TOGGLE_LOOT, 0); break;
        case VK_F5: SendMessage(hWnd, WM_COMMAND, IDM_SCALE_UP, 0); break;
        case VK_F6: SendMessage(hWnd, WM_COMMAND, IDM_SCALE_DOWN, 0); break;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == 1) UpdateUI();
        break;

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_SIZE:
        if (g_hStatus) {
            SendMessage(g_hStatus, WM_SIZE, 0, 0);
            LayoutStatusBar(hWnd);
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        for (int i = 0; i < 3; i++) {
            if (hCtrl == g_hTabBtn[i]) {
                SetBkMode(hdc, TRANSPARENT);
                if (i == g_currentTab)
                    SetTextColor(hdc, RGB(0, 100, 200));
                else
                    SetTextColor(hdc, RGB(0, 0, 0));
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
        }
        break;
    }

    case WM_DESTROY:
        DebugLog("[EXIT] Shutting down...");
        { wchar_t cfgDir[MAX_PATH];
        GetModuleFileNameW(NULL, cfgDir, MAX_PATH);
        wchar_t* b = wcsrchr(cfgDir, L'\\'); if (b) *b = 0;
        wcscat(cfgDir, L"\\config");
        G->modMgr.SaveAll(cfgDir); }
        G->modMgr.StopAll();
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hTreeFont) DeleteObject(g_hTreeFont);
        if (g_hProcess) CloseHandle(g_hProcess);
        if (g_hDebugConsole) DestroyWindow(g_hDebugConsole);
        PostQuitMessage(0);
        break;

    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================
// Entry point
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    G = new BotState();
    g_hInst = hInst;
    INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&icex);
    RegisterPanelClass();

    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"WarspearBotCtrl"; RegisterClassExW(&wc);

    g_hWnd=CreateWindowExW(0,L"WarspearBotCtrl",L"Warspear Bot",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,360,600,NULL,NULL,hInst,NULL);
    if(!g_hWnd) return 0;
    ShowWindow(g_hWnd,nShow); UpdateWindow(g_hWnd);
    SetTimer(g_hWnd,1,200,NULL);

    MSG msg{};
    while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return (int)msg.wParam;
}
