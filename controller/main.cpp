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
    PartyHealerModule partyHealer;
    LooterModule    looter;
    FollowerModule  follower;
    ExtraModule     extra;

    std::vector<EntityData> cachedMobs, cachedPlayers, cachedNpcs;
    std::vector<CorpseData> cachedCorpses;
    std::vector<ProcInfo> procs;
    std::vector<int> listToProc;

    IModule* activeModule = nullptr;
    BotCmd*  pBotCmd = nullptr;
    HANDLE   hSharedMem = NULL;
};

static BotState* G = nullptr;

// ============================================================
// Simple globals
// ============================================================
static HINSTANCE g_hInst = NULL;
static HWND g_hWnd = NULL;
static HWND g_hStatus = NULL;
static HFONT g_hFont = NULL;
static HFONT g_hTreeFont = NULL;

static HANDLE g_hProcess = NULL;
static DWORD  g_gamePid  = 0;
static bool   g_connected = false;
static bool   g_dllInjected = false;
static float  g_selfX = 0, g_selfY = 0;
static float  g_scale = 3.5f;
static DWORD  g_playerAddr = 0, g_gmAddr = 0;

// Tab system
enum TabID { TAB_CONFIG = 0, TAB_QUICK = 1, TAB_CONN = 2 };
static int g_currentTab = TAB_CONFIG;
static HWND g_hTabBtn[3] = {};
static HWND g_hTabPanel[3] = {};

// Config tab - Accordion
static HWND g_hModHeader[7] = {};
static HWND g_hModPanel[7] = {};
static HWND g_hModLabel[7][4] = {};
static bool g_modExpanded[7] = {};
static const wchar_t* MOD_NAMES[] = { L"Targeter", L"Attacker", L"Healer", L"PartyHealer", L"Looter", L"Follower", L"Extra" };
enum { MID_TARGETER=0, MID_ATTACKER, MID_HEALER, MID_PHEALER, MID_LOOTER, MID_FOLLOWER, MID_EXTRA };

// Quick actions tab
static HWND g_hQuickBtn[8] = {};
static const wchar_t* QUICK_LABELS[] = {
    L"Toggle Attack [F1]", L"Toggle Heal [F2]", L"Toggle Follow [F3]",
    L"Toggle Loot [F4]", L"Toggle All ON", L"STOP ALL", L"Scale + [F5]", L"Scale - [F6]"
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
enum {
    IDM_MOD_HEADER = 4000,
    IDM_MOD_TOGGLE = 4100,
    IDM_MOD_VALUE  = 4200,
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
    g_hFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH|FF_SWISS, L"Segoe UI");
    g_hTreeFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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

// ============================================================
// UI: Accordion config - helpers
// ============================================================
void AccordionUpdateLabels(int mod);

void AccordionToggleModule(int mod) {
    g_modExpanded[mod] = !g_modExpanded[mod];
    ShowWindow(g_hModPanel[mod], g_modExpanded[mod] ? SW_SHOW : SW_HIDE);
    AccordionUpdateLabels(mod);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

void AccordionUpdateLabels(int mod) {
    wchar_t b[128];
    auto setLabel = [](HWND h, const wchar_t* t) { if(h) SetWindowTextW(h, t); };
    switch (mod) {
    case MID_TARGETER:
        setLabel(g_hModLabel[0][0], G->targeter.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        setLabel(g_hModLabel[0][1], G->targeter.retargetOnNearby ? L"Retarget: ON" : L"Retarget: OFF");
        swprintf(b,128,L"Max dist: %d", (int)G->targeter.maxDistance);
        setLabel(g_hModLabel[0][2], b);
        break;
    case MID_ATTACKER:
        setLabel(g_hModLabel[1][0], G->attacker.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        swprintf(b,128,L"Cooldown: %d ms", G->attacker.globalCooldownMs);
        setLabel(g_hModLabel[1][1], b);
        break;
    case MID_HEALER:
        setLabel(g_hModLabel[2][0], G->healer.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        swprintf(b,128,L"Min HP%%: %d", (int)G->healer.minHpPct);
        setLabel(g_hModLabel[2][1], b);
        swprintf(b,128,L"Heal key: %d", G->healer.healKeyBind - 0x30);
        setLabel(g_hModLabel[2][2], b);
        break;
    case MID_PHEALER:
        setLabel(g_hModLabel[3][0], G->partyHealer.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        swprintf(b,128,L"Cooldown: %d ms", G->partyHealer.cooldownMs);
        setLabel(g_hModLabel[3][1], b);
        break;
    case MID_LOOTER:
        setLabel(g_hModLabel[4][0], G->looter.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        swprintf(b,128,L"Radius: %d", (int)G->looter.radius);
        setLabel(g_hModLabel[4][1], b);
        break;
    case MID_FOLLOWER:
        setLabel(g_hModLabel[5][0], G->follower.enabled ? L"[ON] Click to toggle" : L"[OFF] Click to toggle");
        break;
    case MID_EXTRA:
        setLabel(g_hModLabel[6][0], G->extra.antiAfk ? L"Anti AFK: ON" : L"Anti AFK: OFF");
        setLabel(g_hModLabel[6][1], G->extra.autoRevive ? L"Auto Revive: ON" : L"Auto Revive: OFF");
        setLabel(g_hModLabel[6][2], G->extra.autoSell ? L"Auto Sell: ON" : L"Auto Sell: OFF");
        setLabel(g_hModLabel[6][3], G->extra.autoRepair ? L"Auto Repair: ON" : L"Auto Repair: OFF");
        break;
    }
    wchar_t hdr[64];
    swprintf(hdr,64,L"%s %s", MOD_NAMES[mod], g_modExpanded[mod] ? L"-" : L"+");
    SetWindowTextW(g_hModHeader[mod], hdr);
}

// ============================================================
// UI: Accordion config - handle clicks
// ============================================================
void AccordionHandleClick(int id) {
    if (id >= IDM_MOD_HEADER && id < IDM_MOD_HEADER + 7) {
        AccordionToggleModule(id - IDM_MOD_HEADER);
        return;
    }
    int mod = (id - IDM_MOD_TOGGLE) / 10;
    int sub = (id - IDM_MOD_TOGGLE) % 10;
    if (id >= IDM_MOD_TOGGLE && id < IDM_MOD_TOGGLE + 70) {
        switch (mod) {
        case MID_TARGETER:
            if (sub == 0) { G->targeter.enabled = !G->targeter.enabled; AccordionUpdateLabels(mod); }
            else if (sub == 1) { G->targeter.retargetOnNearby = !G->targeter.retargetOnNearby; AccordionUpdateLabels(mod); }
            else if (sub == 2) { int v = ShowInputInt(g_hWnd, L"Max Distance", (int)G->targeter.maxDistance); G->targeter.maxDistance = (float)v; AccordionUpdateLabels(mod); }
            break;
        case MID_ATTACKER:
            if (sub == 0) { G->attacker.enabled = !G->attacker.enabled; AccordionUpdateLabels(mod); }
            else if (sub == 1) { int v = ShowInputInt(g_hWnd, L"Cooldown (ms)", G->attacker.globalCooldownMs); G->attacker.globalCooldownMs = v; AccordionUpdateLabels(mod); }
            break;
        case MID_HEALER:
            if (sub == 0) { G->healer.enabled = !G->healer.enabled; AccordionUpdateLabels(mod); }
            else if (sub == 1) { int v = ShowInputInt(g_hWnd, L"Min HP%", (int)G->healer.minHpPct); G->healer.minHpPct = (float)v; AccordionUpdateLabels(mod); }
            else if (sub == 2) { int v = ShowInputInt(g_hWnd, L"Heal Key (1-9)", G->healer.healKeyBind - 0x30); if(v>=1&&v<=9) G->healer.healKeyBind = 0x30+v; AccordionUpdateLabels(mod); }
            break;
        case MID_PHEALER:
            if (sub == 0) { G->partyHealer.enabled = !G->partyHealer.enabled; AccordionUpdateLabels(mod); }
            else if (sub == 1) { int v = ShowInputInt(g_hWnd, L"Cooldown (ms)", G->partyHealer.cooldownMs); G->partyHealer.cooldownMs = v; AccordionUpdateLabels(mod); }
            break;
        case MID_LOOTER:
            if (sub == 0) { G->looter.enabled = !G->looter.enabled; AccordionUpdateLabels(mod); }
            else if (sub == 1) { int v = ShowInputInt(g_hWnd, L"Loot Radius", (int)G->looter.radius); G->looter.radius = (float)v; AccordionUpdateLabels(mod); }
            break;
        case MID_FOLLOWER:
            if (sub == 0) { G->follower.enabled = !G->follower.enabled; AccordionUpdateLabels(mod); }
            break;
        case MID_EXTRA:
            if (sub == 0) { G->extra.antiAfk = !G->extra.antiAfk; AccordionUpdateLabels(mod); }
            else if (sub == 1) { G->extra.autoRevive = !G->extra.autoRevive; AccordionUpdateLabels(mod); }
            else if (sub == 2) { G->extra.autoSell = !G->extra.autoSell; AccordionUpdateLabels(mod); }
            else if (sub == 3) { G->extra.autoRepair = !G->extra.autoRepair; AccordionUpdateLabels(mod); }
            break;
        }
        return;
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

    int bw = 330, bh = 22, y = 2;
    for (int m = 0; m < 7; m++) {
        g_hModHeader[m] = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD|WS_VISIBLE|BS_LEFT|BS_FLAT,
            4, y, bw, bh, g_hTabPanel[TAB_CONFIG], (HMENU)(IDM_MOD_HEADER + m), g_hInst, NULL);
        SetFont(g_hModHeader[m]);
        y += bh + 2;

        g_hModPanel[m] = CreateWindowExW(0, PANEL_CLASS, L"",
            WS_CHILD, 4, y, bw, 90, g_hTabPanel[TAB_CONFIG], NULL, g_hInst, NULL);

        int sy = 2;
        for (int s = 0; s < 4; s++) {
            g_hModLabel[m][s] = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOTIFY,
                4, sy, bw - 8, 18, g_hModPanel[m], (HMENU)(IDM_MOD_TOGGLE + m * 10 + s), g_hInst, NULL);
            SetFont(g_hModLabel[m][s]);
            sy += 20;
        }
        y += 88;
    }
}

void RefreshAccordion() {
    for (int m = 0; m < 7; m++) {
        AccordionUpdateLabels(m);
        ShowWindow(g_hModPanel[m], g_modExpanded[m] ? SW_SHOW : SW_HIDE);
    }
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
void UpdateUI() {
    if (!g_connected || !g_hProcess) {
        SetWindowTextW(g_hStatus, g_hProcess ? L"Connected" : L"Select Warspear and connect");
        return;
    }
    float sx,sy; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
    std::vector<EntityData> pl,mb,np;
    std::vector<CorpseData> corpses;
    DWORD playerAddr=0, gmAddr=0;
    if (!ReadGameState(sx,sy,hp,mhp,mn,mmn,name,level,classId,pl,mb,np,corpses,&playerAddr,&gmAddr)) {
        SetWindowTextW(g_hStatus, L"Cannot read game memory");
        g_connected = false;
        return;
    }
    g_selfX = sx; g_selfY = sy;
    G->cachedCorpses = corpses;
    G->cachedPlayers = pl; G->cachedMobs = mb; G->cachedNpcs = np;
    g_playerAddr = playerAddr; g_gmAddr = gmAddr;

    wchar_t buf[512];
    swprintf(buf, 512, L"%s | Lv.%d %s | HP: %d/%d | P:%d M:%d N:%d",
        name.c_str(), level, GetClassName(classId), hp, mhp, (int)pl.size(), (int)mb.size(), (int)np.size());
    SetWindowTextW(g_hStatus, buf);

    static std::wstring lastCharName;
    if (name != lastCharName) {
        wchar_t wtitle[128];
        swprintf(wtitle, 128, L"%s [Lv.%d %s]", name.c_str(), level, GetClassName(classId));
        SetWindowTextW(g_hWnd, wtitle);
        lastCharName = name;
    }

    GameContext ctx = BuildContext();
    if (G->targeter.enabled && G->targeter.selectedAddr > 0x1000)
        G->attacker.targetAddr = G->targeter.selectedAddr;
    G->modMgr.TickAll(ctx);
}

// ============================================================
// WndProc
// ============================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitFont();
        CreateTabBar(hWnd);
        CreateConfigPanel(hWnd);
        CreateQuickPanel(hWnd);
        CreateConnPanel(hWnd);

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        G->modMgr.Add(&G->targeter);
        G->modMgr.Add(&G->attacker);
        G->modMgr.Add(&G->healer);
        G->modMgr.Add(&G->partyHealer);
        G->modMgr.Add(&G->looter);
        G->modMgr.Add(&G->follower);
        G->modMgr.Add(&G->extra);

        wchar_t cfgDir[MAX_PATH];
        GetModuleFileNameW(NULL, cfgDir, MAX_PATH);
        wchar_t* bs = wcsrchr(cfgDir, L'\\'); if (bs) *bs = 0;
        wcscat(cfgDir, L"\\config");
        G->modMgr.LoadAll(cfgDir);

        RefreshAccordion();
        SwitchTab(TAB_CONFIG);
        RefreshProcesses(g_hProcList);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        // Accordion module headers and settings
        if ((id >= IDM_MOD_HEADER && id < IDM_MOD_HEADER + 7) ||
            (id >= IDM_MOD_TOGGLE && id < IDM_MOD_TOGGLE + 70)) {
            AccordionHandleClick(id);
            break;
        }

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
            G->healer.enabled = false; G->partyHealer.enabled = false;
            G->looter.enabled = false; G->follower.enabled = false;
            G->extra.enabled = false;
            G->attacker.targetAddr = 0; G->follower.targetAddr = 0;
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
