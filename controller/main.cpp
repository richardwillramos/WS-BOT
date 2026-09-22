// warspear-controller/main.cpp
// Warspear Bot Controller v4 - Modular Architecture

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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")

#include "../include/IModule.h"
#include "../include/ModuleManager.h"
#include "../modules/Targeter.h"
#include "../modules/Attacker.h"
#include "../modules/Healer.h"
#include "../modules/PartyHealer.h"
#include "../modules/Looter.h"
#include "../modules/Follower.h"
#include "../modules/Extra.h"

// ============================================================
// Game constants
// ============================================================
namespace Game {
    constexpr DWORD GM_PTR      = 0x00D387AC;
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
    constexpr DWORD ENT_HP        = 0x10C;
    constexpr DWORD ENT_MAX_HP    = 0x110;
    constexpr DWORD ENT_MANA      = 0x114;
    constexpr DWORD ENT_MAX_MANA  = 0x118;
    constexpr DWORD ENT_LEVEL     = 0x2E0;
    constexpr DWORD ENT_CLASS_IND = 0x3ED;
    constexpr DWORD VT_PLAYER  = 0x00C80F9C;
    constexpr DWORD VT_BEAST   = 0x00C81490;
    constexpr DWORD VT_CORPSE  = 0x00C4FC5C;
    constexpr DWORD OBJ_OBJECT_ID  = 0x120;
    constexpr DWORD OBJ_TYPE_ID    = 0x124;
    constexpr DWORD CURSOR_OFFSET  = 0x123C;
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

// ============================================================
// Globals
// ============================================================
static HINSTANCE g_hInst = NULL;
static HWND g_hWnd = NULL;
static HMENU g_hMenu = NULL;
static HWND g_hStatus = NULL;
static HWND g_hTreeView = NULL;
static HWND g_hDetailPanel = NULL;

static HANDLE g_hProcess = NULL;
static DWORD  g_gamePid  = 0;
static bool   g_connected = false;
static bool   g_dllInjected = false;
static float  g_selfX = 0, g_selfY = 0;
static float  g_scale = 3.5f;

static std::vector<EntityData> g_cachedMobs, g_cachedPlayers, g_cachedNpcs;
static std::vector<CorpseData> g_cachedCorpses;
static DWORD g_playerAddr = 0, g_gmAddr = 0;

// Module manager
static ModuleManager g_modMgr;
static TargeterModule  g_targeter;
static AttackerModule  g_attacker;
static HealerModule    g_healer;
static PartyHealerModule g_partyHealer;
static LooterModule    g_looter;
static FollowerModule  g_follower;
static ExtraModule     g_extra;

// Active module UI panel
static IModule* g_activeModule = nullptr;

// Process listing
struct ProcInfo { DWORD pid; std::wstring name; };
static std::vector<ProcInfo> g_procs;
static std::vector<int> g_listToProc;

// DLL shared memory
struct BotCmd {
    volatile long attackOn, followOn, healOn, healThreshold, targetAddr, followAddr;
};
static HANDLE g_hSharedMem = NULL;
static BotCmd* g_pBotCmd = NULL;

// Menu IDs
enum MenuID {
    IDM_CONFIG = 1001,
    IDM_QUICK,
    IDM_CONNECTION,
    IDM_REFRESH,
    IDM_CONNECT,
    IDM_INJECT,
    IDM_BROWSE_DLL,
    IDM_TOGGLE_ATTACK,
    IDM_TOGGLE_FOLLOW,
    IDM_TOGGLE_LOOT,
    IDM_TOGGLE_HEAL,
    IDM_TOGGLE_ALL,
    IDM_STOP_ALL,
    IDM_SCALE_UP,
    IDM_SCALE_DOWN,
    IDM_DEBUG,
    IDM_EXIT,
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
    L"Norberto, o aougueiro"
};
static const int NPC_COUNT = sizeof(NPC_NAMES) / sizeof(NPC_NAMES[0]);
bool IsNPC(const std::wstring& n) { for (int i=0;i<NPC_COUNT;i++) if(n==NPC_NAMES[i]) return true; return false; }

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
        CorpseData c{};
        c.objAddr = objPtr;
        DWORD namePtr = Read<DWORD>(objPtr + Game::ENT_NAME_PTR);
        int nameLen = Read<int>(objPtr + Game::ENT_NAME_LEN);
        if (nameLen > 0 && nameLen < 64 && namePtr > 0x1000) {
            wchar_t w[64] = {};
            for (int i = 0; i < nameLen; i++) { wchar_t ch = Read<wchar_t>(namePtr + i * 2); if (ch == 0) break; w[i] = ch; }
            wcscpy_s(c.name, w);
        } else wcscpy_s(c.name, L"Corpse");
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
        wcscpy_s(e.name, w);
    } else wcscpy_s(e.name, L"(unknown)");
    e.x = Read<int>(objPtr+Game::ENT_RAW_X) / 65536.0f;
    e.y = Read<int>(objPtr+Game::ENT_RAW_Y) / 65536.0f;
    e.hp=Read<int>(objPtr+Game::ENT_HP); e.maxHp=Read<int>(objPtr+Game::ENT_MAX_HP);
    e.mana=Read<int>(objPtr+Game::ENT_MANA); e.maxMana=Read<int>(objPtr+Game::ENT_MAX_MANA);
    int ti=Read<int>(objPtr+Game::ENT_TYPE_IND);
    if (vtable==Game::VT_PLAYER||ti==1) e.type=1;
    else if (vtable==Game::VT_BEAST) e.type=4;
    else if (IsNPC(e.name)) e.type=3;
    else e.type=2;
    e.level=Read<int>(objPtr+Game::ENT_LEVEL);
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
    level=Read<int>(lp+Game::ENT_LEVEL);
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
// Debug console
// ============================================================
static HWND g_hDebugConsole = NULL;
static HWND g_hDebugEdit = NULL;
static bool g_debugConsoleOpen = false;

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
// Find game window (PID-based)
// ============================================================
HWND FindGameWindow() {
    if(g_gamePid) {
        struct Ctx { DWORD pid; HWND h; } ctx={g_gamePid,NULL};
        EnumWindows([](HWND h,LPARAM lp)->BOOL{
            auto c=(Ctx*)lp; DWORD p=0; GetWindowThreadProcessId(h,&p);
            if(p==c->pid&&IsWindowVisible(h)){c->h=h;return FALSE;} return TRUE;
        },(LPARAM)&ctx);
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
    HWND gw = FindGameWindow();
    if (!gw) return false;
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(30);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

void ClickAtClient(int cx, int cy) {
    HWND w=FindGameWindow(); if(!w) return;
    if(GetForegroundWindow()!=w || IsIconic(w)) return;
    POINT pt={cx,cy}; ClientToScreen(w,&pt);
    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    if(pt.x<0||pt.x>=sx||pt.y<0||pt.y>=sy) return;
    INPUT in[3]={};
    in[0].type=INPUT_MOUSE;
    in[0].mi.dx=(LONG)(pt.x*65536.0/sx);
    in[0].mi.dy=(LONG)(pt.y*65536.0/sy);
    in[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
    in[1].type=INPUT_MOUSE; in[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    in[2].type=INPUT_MOUSE; in[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    SendInput(3,in,sizeof(INPUT));
}

void MoveToClient(int cx, int cy) {
    HWND w = FindGameWindow();
    if (!w) return;
    POINT pt = { cx, cy };
    ClientToScreen(w, &pt);
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)(pt.x * 65536.0 / sx);
    in.mi.dy = (LONG)(pt.y * 65536.0 / sy);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &in, sizeof(INPUT));
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

void SendInputKey(WORD vk) {
    INPUT in[2]={};
    in[0].type=INPUT_KEYBOARD; in[0].ki.wVk=vk;
    in[1].type=INPUT_KEYBOARD; in[1].ki.wVk=vk; in[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(2,in,sizeof(INPUT));
}

// ============================================================
// Build GameContext from current state
// ============================================================
GameContext BuildContext() {
    GameContext ctx{};
    ctx.hProcess = g_hProcess;
    ctx.gamePid = g_gamePid;
    ctx.selfX = g_selfX;
    ctx.selfY = g_selfY;
    ctx.playerAddr = g_playerAddr;
    ctx.gmAddr = g_gmAddr;
    ctx.gameWindow = FindGameWindow();
    ctx.tickCount = GetTickCount();

    // Read current player stats
    if (g_playerAddr > 0x1000) {
        ctx.selfHp = Read<int>(g_playerAddr + Game::ENT_HP);
        ctx.selfMaxHp = Read<int>(g_playerAddr + Game::ENT_MAX_HP);
        ctx.selfMana = Read<int>(g_playerAddr + Game::ENT_MANA);
        ctx.selfMaxMana = Read<int>(g_playerAddr + Game::ENT_MAX_MANA);
        ctx.selfLevel = Read<int>(g_playerAddr + Game::ENT_LEVEL);
        ctx.selfClassId = Read<BYTE>(g_playerAddr + Game::ENT_CLASS_IND);
        DWORD np = Read<DWORD>(g_playerAddr + Game::ENT_NAME_PTR);
        int nl = Read<int>(g_playerAddr + Game::ENT_NAME_LEN);
        if (nl > 0 && nl < 64 && np > 0x1000) {
            wchar_t w[64] = {};
            for (int i = 0; i < nl; i++) { wchar_t c = Read<wchar_t>(np + i * 2); if (c == 0) break; w[i] = c; }
            ctx.selfName = w;
        }
    }

    // Convert cached entities
    for (auto& e : g_cachedPlayers) {
        GameContext::EntityInfo ei;
        ei.objAddr = e.objAddr; ei.name = e.name;
        ei.x = e.x; ei.y = e.y; ei.hp = e.hp; ei.maxHp = e.maxHp;
        ei.distance = e.distance; ei.type = e.type;
        ctx.players.push_back(ei);
    }
    for (auto& e : g_cachedMobs) {
        GameContext::EntityInfo ei;
        ei.objAddr = e.objAddr; ei.name = e.name;
        ei.x = e.x; ei.y = e.y; ei.hp = e.hp; ei.maxHp = e.maxHp;
        ei.distance = e.distance; ei.type = e.type;
        ctx.mobs.push_back(ei);
    }
    for (auto& e : g_cachedNpcs) {
        GameContext::EntityInfo ei;
        ei.objAddr = e.objAddr; ei.name = e.name;
        ei.x = e.x; ei.y = e.y; ei.hp = e.hp; ei.maxHp = e.maxHp;
        ei.distance = e.distance; ei.type = e.type;
        ctx.npcs.push_back(ei);
    }
    for (auto& c : g_cachedCorpses) {
        GameContext::CorpseInfo ci;
        ci.objAddr = c.objAddr; ci.name = c.name;
        ci.x = c.x; ci.y = c.y; ci.distance = c.distance;
        ctx.corpses.push_back(ci);
    }
    return ctx;
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
    g_procs.clear();
    g_listToProc.clear();
    SendMessageW(hList,LB_RESETCONTENT,0,0);
    HANDLE hs=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(hs==INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe);
    if(Process32FirstW(hs,&pe)){do{g_procs.push_back({pe.th32ProcessID,pe.szExeFile});}while(Process32NextW(hs,&pe));}
    CloseHandle(hs);
    std::sort(g_procs.begin(),g_procs.end(),[](const ProcInfo& a,const ProcInfo& b){return a.name<b.name;});

    std::vector<size_t> wsIdx, otherIdx;
    for(size_t i=0;i<g_procs.size();i++){
        if(_wcsicmp(g_procs[i].name.c_str(), L"warspear.exe")==0) wsIdx.push_back(i);
        else otherIdx.push_back(i);
    }

    for(size_t idx : wsIdx){
        g_listToProc.push_back((int)idx);
        auto& p = g_procs[idx];
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
            swprintf_s(buf, L"%s [Lv.%d %s]  (PID %d)", charName.c_str(), level, GetClassName(classId), p.pid);
        else
            swprintf_s(buf, L"%s  (PID %d)", charName.c_str(), p.pid);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }

    if(!wsIdx.empty() && !otherIdx.empty()){
        g_listToProc.push_back(-1);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"--- other processes ---");
    }
    for(size_t idx : otherIdx){
        g_listToProc.push_back((int)idx);
        auto& p = g_procs[idx];
        wchar_t buf[256]; swprintf_s(buf,L"%s  (PID %d)",p.name.c_str(),p.pid);
        SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)buf);
    }
}

// ============================================================
// UI Helpers
// ============================================================
HFONT g_hFont = NULL;
void InitFont() { g_hFont=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI"); }
void SetFont(HWND h){SendMessageW(h,WM_SETFONT,(WPARAM)g_hFont,TRUE);}

// ============================================================
// UI Layout constants
// ============================================================
static const int UI_MENU_H    = 24;
static const int UI_STATUS_H  = 22;
static const int UI_TREE_W    = 180;
static const int UI_DETAIL_X  = UI_TREE_W + 15;
static const int UI_DETAIL_W  = 390;
static const int UI_DETAIL_H  = 360;
static const int UI_TREE_Y    = UI_MENU_H + 5;
static const int UI_DETAIL_Y  = UI_MENU_H + 5;

// ============================================================
// Process list panel (for Connection tab)
// ============================================================
static HWND g_hProcList = NULL;
static HWND g_hBtnConnect = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hDllPath = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hBtnInject = NULL;
static HWND g_hProcPanel = NULL;

void CreateProcessPanel(HWND parent) {
    g_hProcPanel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD|WS_VISIBLE, UI_DETAIL_X, UI_DETAIL_Y, UI_DETAIL_W, UI_DETAIL_H,
        parent, NULL, g_hInst, NULL);

    int x = 10, y = 5;
    CreateWindowExW(0, L"static", L"Processes:", WS_CHILD|WS_VISIBLE,
        x, y, 200, 18, g_hProcPanel, NULL, g_hInst, NULL);
    y += 20;
    g_hProcList = CreateWindowExW(0, L"listbox", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
        x, y, 370, 200, g_hProcPanel, NULL, g_hInst, NULL);
    SetFont(g_hProcList);

    y += 208;
    g_hBtnRefresh = CreateWindowExW(0, L"button", L"Refresh",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x, y, 70, 24, g_hProcPanel, (HMENU)IDM_REFRESH, g_hInst, NULL);
    g_hBtnConnect = CreateWindowExW(0, L"button", L"Connect",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x+75, y, 70, 24, g_hProcPanel, (HMENU)IDM_CONNECT, g_hInst, NULL);

    y += 32;
    CreateWindowExW(0, L"static", L"DLL:", WS_CHILD|WS_VISIBLE,
        x, y+2, 30, 18, g_hProcPanel, NULL, g_hInst, NULL);
    g_hDllPath = CreateWindowExW(0, L"edit", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        x+32, y, 220, 22, g_hProcPanel, NULL, g_hInst, NULL);
    g_hBtnBrowse = CreateWindowExW(0, L"button", L"...",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x+255, y, 30, 22, g_hProcPanel, (HMENU)IDM_BROWSE_DLL, g_hInst, NULL);
    g_hBtnInject = CreateWindowExW(0, L"button", L"Inject DLL",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        x+290, y, 70, 22, g_hProcPanel, (HMENU)IDM_INJECT, g_hInst, NULL);

    SetFont(g_hBtnRefresh); SetFont(g_hBtnConnect);
    SetFont(g_hDllPath); SetFont(g_hBtnBrowse); SetFont(g_hBtnInject);
}

// ============================================================
// Detail panel for active module
// ============================================================
static HWND g_hDetailLabel = NULL;

void CreateDetailPanel(HWND parent) {
    g_hDetailPanel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD, UI_DETAIL_X, UI_DETAIL_Y, UI_DETAIL_W, UI_DETAIL_H,
        parent, NULL, g_hInst, NULL);
    g_hDetailLabel = CreateWindowExW(0, L"static", L"Select a module from the tree",
        WS_CHILD|WS_VISIBLE, 10, 10, 370, 20,
        g_hDetailPanel, NULL, g_hInst, NULL);
    SetFont(g_hDetailLabel);
}

void ShowModuleUI(IModule* mod) {
    // Hide process panel
    if (g_hProcPanel) ShowWindow(g_hProcPanel, SW_HIDE);

    // Destroy old module UI children
    if (g_hDetailPanel) {
        // Kill all child windows of detail panel
        HWND child = GetWindow(g_hDetailPanel, GW_CHILD);
        while (child) {
            HWND next = GetWindow(child, GW_HWNDNEXT);
            DestroyWindow(child);
            child = next;
        }
    }

    g_activeModule = mod;

    if (!mod || !mod->HasUI()) {
        if (g_hDetailPanel) {
            ShowWindow(g_hDetailPanel, SW_SHOW);
            const wchar_t* msg = mod ? mod->GetName() : L"Select a module from the tree";
            g_hDetailLabel = CreateWindowExW(0, L"static", msg,
                WS_CHILD|WS_VISIBLE, 10, 10, 370, 20,
                g_hDetailPanel, NULL, g_hInst, NULL);
            SetFont(g_hDetailLabel);
        }
        return;
    }

    ShowWindow(g_hDetailPanel, SW_SHOW);
    mod->CreateUI(g_hDetailPanel, 10, 30, 370);
}

void ShowProcessPanel() {
    if (g_hDetailPanel) ShowWindow(g_hDetailPanel, SW_HIDE);
    if (g_hProcPanel) {
        ShowWindow(g_hProcPanel, SW_SHOW);
        RefreshProcesses(g_hProcList);
    }
    g_activeModule = nullptr;
}

// ============================================================
// Menu bar
// ============================================================
void CreateMenuBar(HWND hWnd) {
    HMENU hMenuBar = CreateMenu();
    HMENU hConfigMenu = CreatePopupMenu();
    HMENU hQuickMenu = CreatePopupMenu();
    HMENU hConnMenu = CreatePopupMenu();

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hConfigMenu, L"Config");
    AppendMenuW(hConfigMenu, MF_STRING, IDM_REFRESH, L"Refresh Processes");
    AppendMenuW(hConfigMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hConfigMenu, MF_STRING, IDM_EXIT, L"Exit");

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hQuickMenu, L"Quick Actions");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_TOGGLE_ATTACK, L"Toggle Attack [F1]");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_TOGGLE_HEAL, L"Toggle Heal [F2]");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_TOGGLE_FOLLOW, L"Toggle Follow [F3]");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_TOGGLE_LOOT, L"Toggle Loot [F4]");
    AppendMenuW(hQuickMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hQuickMenu, MF_STRING, IDM_TOGGLE_ALL, L"Toggle All ON");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_STOP_ALL, L"STOP ALL");
    AppendMenuW(hQuickMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hQuickMenu, MF_STRING, IDM_SCALE_UP, L"Scale Up [F5]");
    AppendMenuW(hQuickMenu, MF_STRING, IDM_SCALE_DOWN, L"Scale Down [F6]");

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hConnMenu, L"Connection");
    AppendMenuW(hConnMenu, MF_STRING, IDM_CONNECT, L"Connect to Process");
    AppendMenuW(hConnMenu, MF_STRING, IDM_INJECT, L"Inject DLL");
    AppendMenuW(hConnMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hConnMenu, MF_STRING, IDM_DEBUG, L"Debug Console");

    SetMenu(hWnd, hMenuBar);
    g_hMenu = hMenuBar;
}

// ============================================================
// TreeView for module selection
// ============================================================
void CreateModuleTree(HWND parent) {
    g_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TABCONTROLW, L"",
        WS_CHILD|WS_VISIBLE|TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,
        5, UI_TREE_Y, UI_TREE_W, UI_DETAIL_H,
        parent, NULL, g_hInst, NULL);

    // Actually use a TreeView control
    g_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD|WS_VISIBLE|TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,
        5, UI_TREE_Y, UI_TREE_W, UI_DETAIL_H,
        parent, (HMENU)2000, g_hInst, NULL);

    SetFont(g_hTreeView);

    // Register modules
    g_modMgr.Add(&g_targeter);
    g_modMgr.Add(&g_attacker);
    g_modMgr.Add(&g_healer);
    g_modMgr.Add(&g_partyHealer);
    g_modMgr.Add(&g_looter);
    g_modMgr.Add(&g_follower);
    g_modMgr.Add(&g_extra);

    // Load configs
    wchar_t cfgDir[MAX_PATH];
    GetModuleFileNameW(NULL, cfgDir, MAX_PATH);
    wchar_t* bs = wcsrchr(cfgDir, L'\\'); if (bs) *bs = 0;
    wcscat_s(cfgDir, L"\\config");
    g_modMgr.LoadAll(cfgDir);

    // Build tree
    g_modMgr.BuildTreeView(g_hTreeView);

    // Add connection item
    TVINSERTSTRUCTW tis{};
    tis.hParent = TVI_ROOT;
    tis.hInsertAfter = TVI_FIRST;
    tis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tis.item.pszText = L"Connection";
    tis.item.lParam = -1; // sentinel for connection
    TreeView_InsertItem(g_hTreeView, &tis);
}

// ============================================================
// Update UI
// ============================================================
void UpdateUI() {
    if (!g_connected || !g_hProcess) {
        SetWindowTextW(g_hStatus, g_hProcess ? L"  Connected" : L"  Select Warspear and connect");
        return;
    }
    float sx,sy; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
    std::vector<EntityData> pl,mb,np;
    std::vector<CorpseData> corpses;
    DWORD playerAddr=0, gmAddr=0;
    if (!ReadGameState(sx,sy,hp,mhp,mn,mmn,name,level,classId,pl,mb,np,corpses,&playerAddr,&gmAddr)) {
        SetWindowTextW(g_hStatus, L"  Cannot read game memory");
        g_connected = false;
        return;
    }
    g_selfX = sx; g_selfY = sy;
    g_cachedCorpses = corpses;
    g_cachedPlayers = pl;
    g_cachedMobs = mb;
    g_cachedNpcs = np;
    g_playerAddr = playerAddr;
    g_gmAddr = gmAddr;

    wchar_t buf[512];
    swprintf_s(buf, L"  %s | Lv.%d %s | HP: %d/%d | Players: %d Mobs: %d NPCs: %d",
        name.c_str(), level, GetClassName(classId), hp, mhp, (int)pl.size(), (int)mb.size(), (int)np.size());
    SetWindowTextW(g_hStatus, buf);

    // Update window title
    static std::wstring lastCharName;
    if (name != lastCharName) {
        wchar_t wtitle[128];
        swprintf_s(wtitle, L"WS-Bot - %s [Lv.%d %s]", name.c_str(), level, GetClassName(classId));
        SetWindowTextW(g_hWnd, wtitle);
        lastCharName = name;
    }

    // Tick all modules
    GameContext ctx = BuildContext();
    // Sync Targeter selection to Attacker target
    if (g_targeter.enabled && g_targeter.selectedAddr > 0x1000) {
        g_attacker.targetAddr = g_targeter.selectedAddr;
    }
    g_modMgr.TickAll(ctx);

    // Refresh tree labels
    g_modMgr.RefreshTreeViewLabels(g_hTreeView);

    // Update active module UI
    if (g_activeModule) g_activeModule->UpdateUI();
}

// ============================================================
// WndProc
// ============================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitFont();
        CreateMenuBar(hWnd);
        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        CreateModuleTree(hWnd);
        CreateDetailPanel(hWnd);
        CreateProcessPanel(hWnd);
        ShowProcessPanel();
        break;
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->hwndFrom == g_hTreeView && nm->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW* nmtv = (NMTREEVIEWW*)lParam;
            HTREEITEM sel = TreeView_GetSelection(g_hTreeView);
            if (!sel) break;
            TVITEMW tvi{};
            tvi.mask = TVIF_PARAM;
            tvi.hItem = sel;
            TreeView_GetItem(g_hTreeView, &tvi);

            if (tvi.lParam == -1) {
                ShowProcessPanel();
            } else {
                IModule* mod = (IModule*)tvi.lParam;
                ShowModuleUI(mod);
            }
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // Forward to active module
        if (g_activeModule && id >= 9000) {
            g_activeModule->OnCommand(id, code);
            break;
        }

        switch (id) {
        case IDM_REFRESH:
            if (g_hProcList) RefreshProcesses(g_hProcList);
            break;

        case IDM_CONNECT: {
            if (!g_hProcList) break;
            int sel = (int)SendMessageW(g_hProcList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR || sel >= (int)g_listToProc.size()) {
                MessageBoxW(hWnd, L"Select a process first!", L"", MB_OK|MB_ICONWARNING);
                break;
            }
            int procIdx = g_listToProc[sel];
            if (procIdx < 0 || procIdx >= (int)g_procs.size()) break;
            DWORD pid = g_procs[procIdx].pid;
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
                swprintf_s(wtitle, L"WS-Bot - %s [Lv.%d %s]", name.c_str(), level, GetClassName(classId));
                SetWindowTextW(hWnd, wtitle);
                DebugLog("[CONNECT] SUCCESS - %S Lv.%d", name.c_str(), level);
                // Start enabled modules
                g_modMgr.StartAll();
                // Auto-select first mob for attacker
                if (!m.empty()) {
                    g_attacker.targetAddr = m[0].objAddr;
                }
                // Auto-select first player for follower
                if (!p.empty()) {
                    g_follower.targetAddr = p[0].objAddr;
                    g_follower.targetName = p[0].name;
                }
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
            if (!g_hSharedMem) {
                g_hSharedMem = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BotCmd), L"Local\\WarspearBotShared");
                if (g_hSharedMem) g_pBotCmd = (BotCmd*)MapViewOfFile(g_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd));
            }
            if (!g_pBotCmd) {
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
                wchar_t* bs = wcsrchr(dir, L'\\'); if (bs) *bs = 0;
                wchar_t full[MAX_PATH]; swprintf_s(full, L"%s\\%s", dir, dll);
                wcscpy_s(dll, full);
            }
            if (InjectDLL(g_gamePid, dll)) {
                g_dllInjected = true;
                DebugLog("[INJECT] SUCCESS");
            } else {
                MessageBoxW(hWnd, L"DLL injection failed.", L"Error", MB_OK|MB_ICONERROR);
            }
            break;
        }

        case IDM_TOGGLE_ATTACK: {
            g_targeter.enabled = !g_targeter.enabled;
            g_attacker.enabled = g_targeter.enabled;
            wchar_t s[64]; swprintf_s(s, L"  Attack: %s", g_attacker.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s);
            break;
        }

        case IDM_TOGGLE_HEAL:
            g_healer.enabled = !g_healer.enabled;
            { wchar_t s[64]; swprintf_s(s, L"  Heal: %s", g_healer.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_FOLLOW:
            g_follower.enabled = !g_follower.enabled;
            { wchar_t s[64]; swprintf_s(s, L"  Follow: %s", g_follower.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_LOOT:
            g_looter.enabled = !g_looter.enabled;
            { wchar_t s[64]; swprintf_s(s, L"  Loot: %s", g_looter.enabled ? L"ON" : L"OFF");
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_TOGGLE_ALL: {
            bool on = !(g_targeter.enabled && g_attacker.enabled && g_healer.enabled && g_looter.enabled);
            g_targeter.enabled = on;
            g_attacker.enabled = on;
            g_healer.enabled = on;
            g_looter.enabled = on;
            g_follower.enabled = on;
            g_modMgr.RefreshTreeViewLabels(g_hTreeView);
            SetWindowTextW(g_hStatus, on ? L"  ALL ON" : L"  ALL OFF");
            break;
        }

        case IDM_STOP_ALL: {
            g_targeter.enabled = false;
            g_attacker.enabled = false;
            g_healer.enabled = false;
            g_partyHealer.enabled = false;
            g_looter.enabled = false;
            g_follower.enabled = false;
            g_extra.enabled = false;
            g_attacker.targetAddr = 0;
            g_follower.targetAddr = 0;
            g_modMgr.RefreshTreeViewLabels(g_hTreeView);
            SetWindowTextW(g_hStatus, L"  ALL STOPPED");
            DebugLog("[STOP] All modules stopped");
            break;
        }

        case IDM_SCALE_UP:
            g_scale += 0.5f;
            { wchar_t s[64]; swprintf_s(s, L"  Scale: %.2f", g_scale);
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_SCALE_DOWN:
            g_scale -= 0.5f;
            if (g_scale < 0.5f) g_scale = 0.5f;
            { wchar_t s[64]; swprintf_s(s, L"  Scale: %.2f", g_scale);
            SetWindowTextW(g_hStatus, s); }
            break;

        case IDM_DEBUG:
            OpenDebugConsole(hWnd);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
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

    case WM_SIZE:
        if (g_hStatus) SendMessage(g_hStatus, WM_SIZE, 0, 0);
        break;

    case WM_DESTROY:
        DebugLog("[EXIT] Shutting down...");
        // Save all configs
        { wchar_t cfgDir[MAX_PATH];
        GetModuleFileNameW(NULL, cfgDir, MAX_PATH);
        wchar_t* bs = wcsrchr(cfgDir, L'\\'); if (bs) *bs = 0;
        wcscat_s(cfgDir, L"\\config");
        g_modMgr.SaveAll(cfgDir); }
        g_modMgr.StopAll();
        if (g_hFont) DeleteObject(g_hFont);
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
    g_hInst = hInst;
    INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"WarspearBotCtrl"; RegisterClassExW(&wc);

    g_hWnd=CreateWindowExW(0,L"WarspearBotCtrl",L"Warspear Bot v4",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,610,480,NULL,NULL,hInst,NULL);
    if(!g_hWnd) return 0;
    ShowWindow(g_hWnd,nShow); UpdateWindow(g_hWnd);
    SetTimer(g_hWnd,1,500,NULL);

    MSG msg{};
    while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return (int)msg.wParam;
}
