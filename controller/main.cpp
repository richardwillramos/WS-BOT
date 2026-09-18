// warspear-controller/main.cpp
// Warspear Bot Controller v3 - Connect, Browse DLL, Hotkeys, Attack, Follow

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
    constexpr DWORD ENT_CLASS_IND = 0x2D0;
    constexpr DWORD VT_PLAYER  = 0x00C80F9C;
    constexpr DWORD VT_BEAST   = 0x00C81490;
    constexpr DWORD VT_CORPSE  = 0x00C4FC5C;
    constexpr DWORD OBJ_OBJECT_ID  = 0x120;
    constexpr DWORD OBJ_TYPE_ID    = 0x124;
    constexpr DWORD OBJ_LIFETIME   = 0x128;
}

struct EntityData {
    wchar_t name[64];
    float x, y;
    int hp, maxHp, mana, maxMana;
    int level;
    int classId;
    float distance;
    int type;
    DWORD objAddr;
};

struct CorpseData {
    wchar_t name[64];
    float x, y;
    float distance;
    DWORD objAddr;
    WORD objectId;
    WORD typeId;
};

static HANDLE g_hProcess = NULL;
static DWORD  g_gamePid  = 0;

template<typename T>
T Read(DWORD addr) {
    T val{};
    if (g_hProcess && addr > 0x1000)
        ReadProcessMemory(g_hProcess, (LPCVOID)addr, &val, sizeof(T), NULL);
    return val;
}

float CalcDist(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    return sqrtf(dx*dx + dy*dy);
}

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
        case 1: return L"Seeker";
        case 2: return L"Shadow";
        case 3: return L"Druid";
        case 4: return L"Paladin";
        case 5: return L"Mage";
        case 6: return L"Necromancer";
        case 7: return L"Technician";
        case 8: return L"Assassin";
        default: return L"Unknown";
    }
}

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

    // Check if this is a corpse: HP < 0 means dead (e.g. -24 = 0xFFFFFFE8)
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
    e.classId=Read<int>(objPtr+Game::ENT_CLASS_IND);
    e.distance=CalcDist(selfX,selfY,e.x,e.y);
    if (e.hp==0&&e.maxHp==0) return;
    entities.push_back(e);
}

bool ReadGameState(float& sx, float& sy, int& hp, int& mhp, int& mn, int& mmn,
                   std::wstring& name, int& level, int& classId,
                   std::vector<EntityData>& pl,
                   std::vector<EntityData>& mb, std::vector<EntityData>& np,
                   std::vector<CorpseData>& corpses) {
    pl.clear(); mb.clear(); np.clear(); corpses.clear();
    DWORD gmPtr=Read<DWORD>(Game::GM_PTR); if(gmPtr<=0x1000) return false;
    DWORD gm=Read<DWORD>(gmPtr+Game::GM_OFFSET); if(gm<=0x1000) return false;
    DWORD lp=Read<DWORD>(gm+Game::LP_OFFSET); if(lp<=0x1000) return false;
    sx=Read<int>(lp+Game::ENT_RAW_X)/65536.0f; sy=Read<int>(lp+Game::ENT_RAW_Y)/65536.0f;
    hp=Read<int>(lp+Game::ENT_HP); mhp=Read<int>(lp+Game::ENT_MAX_HP);
    mn=Read<int>(lp+Game::ENT_MANA); mmn=Read<int>(lp+Game::ENT_MAX_MANA);
    level=Read<int>(lp+Game::ENT_LEVEL);
    classId=Read<int>(lp+Game::ENT_CLASS_IND);
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
// Globals
// ============================================================
static HINSTANCE g_hInst=NULL; static HWND g_hWnd=NULL; static HFONT g_hFont=NULL;
static HWND g_hTab=NULL, g_hStatus=NULL;

// Tab 0: Connection
static HWND g_hDllLabel, g_hDllPath, g_hBtnBrowse, g_hBtnRefresh, g_hBtnConnect, g_hBtnInjectDll, g_hProcList;

// Tab 1: Bot
static HWND g_hChkAttack, g_hChkHeal, g_hChkFollow, g_hChkLoot;
static HWND g_hHealLabel, g_hHealThreshold, g_hHealPct;
static HWND g_hAtkNameLabel, g_hAtkName;

// Tab 2: Players
static HWND g_hFollowTitle, g_hFollowName, g_hFollowDist, g_hBtnFollowClear, g_hPlayerList;

// Tab 3: Mobs
static HWND g_hTargetTitle, g_hTargetName, g_hTargetHP, g_hBtnTargetClear, g_hMobList;

// Tab 4: NPCs
static HWND g_hNpcList;

// State
static DWORD g_selectedTargetAddr=0; static wchar_t g_selTargetName[64]={};
static std::vector<EntityData> g_cachedMobs;
static std::vector<EntityData> g_prevMobs;  // Previous frame mobs for death detection
static DWORD g_followTargetAddr=0; static wchar_t g_followName[64]={};
static std::vector<EntityData> g_cachedPlayers;
static std::vector<CorpseData> g_cachedCorpses;
static float g_selfX=0, g_selfY=0;
static bool g_connected=false;
static bool g_dllInjected=false;
static wchar_t g_attackMobFilter[64]={};
static bool g_autoLoot=false;

// Pending corpse (position saved when mob dies, used by loot timer)
static bool g_hasPendingCorpse=false;
static float g_pendingCorpseX=0, g_pendingCorpseY=0;
static wchar_t g_pendingCorpseName[64]={};
static DWORD g_pendingCorpseTime=0;

// Stats
static int g_killCount=0;
static int g_lootCount=0;
static int g_lootClickCount=0;
static wchar_t g_lastLootName[64]={};
static DWORD g_lastLootTime=0;

// Shared memory with DLL
struct BotCmd {
    volatile long attackOn;
    volatile long followOn;
    volatile long healOn;
    volatile long healThreshold;
    volatile long targetAddr;
    volatile long followAddr;
};
static HANDLE g_hSharedMem=NULL;
static BotCmd* g_pBotCmd=NULL;

bool OpenBotSharedMem() {
    g_hSharedMem = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\WarspearBotShared");
    if (!g_hSharedMem) return false;
    g_pBotCmd = (BotCmd*)MapViewOfFile(g_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd));
    return g_pBotCmd != NULL;
}

void UpdateBotCmd() {
    if (!g_pBotCmd) return;
    g_pBotCmd->attackOn = (g_selectedTargetAddr > 0x1000) ? 1 : 0;
    g_pBotCmd->targetAddr = g_selectedTargetAddr;
    g_pBotCmd->followOn = (g_followTargetAddr > 0x1000) ? 1 : 0;
    g_pBotCmd->followAddr = g_followTargetAddr;
}

struct ProcInfo { DWORD pid; std::wstring name; };
static std::vector<ProcInfo> g_procs;

// ============================================================
// Debug Console
// ============================================================
static HWND g_hDebugConsole = NULL;
static HWND g_hDebugEdit = NULL;
static bool g_debugConsoleOpen = false;

void DebugLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

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

void CloseDebugConsole() {
    if (g_hDebugConsole) {
        DestroyWindow(g_hDebugConsole);
        g_hDebugConsole = NULL;
        g_hDebugEdit = NULL;
        g_debugConsoleOpen = false;
    }
}

// ============================================================
// Find game window
// ============================================================
HWND FindGameWindow() {
    HWND h = FindWindowW(NULL, L"Warspear Online");
    if (h) return h;
    h = FindWindowW(NULL, L"Warspear");
    if (h) return h;
    if (g_gamePid) {
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
// Game interaction
// ============================================================
bool IsGameForeground() {
    HWND fg = GetForegroundWindow();
    return fg == FindGameWindow();
}

void SendEnterAttack() {
    HWND gw = FindGameWindow();
    if (!gw || GetForegroundWindow() != gw || IsIconic(gw)) return;
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(30);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
}

bool WriteGameTarget(DWORD addr) {
    if(!g_hProcess||addr<=0x1000) return false;
    DWORD gmPtr=Read<DWORD>(Game::GM_PTR); if(gmPtr<=0x1000) return false;
    DWORD gm=Read<DWORD>(gmPtr+Game::GM_OFFSET); if(gm<=0x1000) return false;
    DWORD lp=Read<DWORD>(gm+Game::LP_OFFSET); if(lp<=0x1000) return false;
    DWORD dw=0;
    WriteProcessMemory(g_hProcess,(LPVOID)(lp+0x290),&addr,4,&dw);
    WriteProcessMemory(g_hProcess,(LPVOID)(lp+0x478),&addr,4,&dw);
    return dw==4;
}

void ClickAtClient(int cx, int cy) {
    HWND w=FindGameWindow(); if(!w) return;
    // Only click if game window is foreground and not minimized
    if(GetForegroundWindow()!=w || IsIconic(w)) return;
    // Force foreground
    DWORD fgTid = GetWindowThreadProcessId(w, NULL);
    DWORD myTid = GetCurrentThreadId();
    AttachThreadInput(myTid, fgTid, TRUE);
    SetForegroundWindow(w);
    AttachThreadInput(myTid, fgTid, FALSE);
    // Re-check after attach
    if(GetForegroundWindow()!=w) return;
    // Convert client coords and click
    POINT pt={cx,cy}; ClientToScreen(w,&pt);
    // Bounds check: click must be within screen
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

void SendInputKey(WORD vk) {
    INPUT in[2]={};
    in[0].type=INPUT_KEYBOARD;
    in[0].ki.wVk=vk;
    in[1].type=INPUT_KEYBOARD;
    in[1].ki.wVk=vk;
    in[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(2,in,sizeof(INPUT));
}

void FollowTarget() {
    if(g_followTargetAddr<=0x1000||!g_hProcess) return;
    SIZE_T r=0; int rx=0,ry=0;
    ReadProcessMemory(g_hProcess,(LPCVOID)(g_followTargetAddr+Game::ENT_RAW_X),&rx,4,&r);
    ReadProcessMemory(g_hProcess,(LPCVOID)(g_followTargetAddr+Game::ENT_RAW_Y),&ry,4,&r);
    float tx=rx/65536.0f, ty=ry/65536.0f;
    float dx=tx-g_selfX, dy=ty-g_selfY;
    float dist=sqrtf(dx*dx+dy*dy);

    DebugLog("[FOLLOW] target=(%.1f,%.1f) self=(%.1f,%.1f) dist=%.1f", tx, ty, g_selfX, g_selfY, dist);

    if(dist<1.0f) return;

    // Select target in game (same as mob attack)
    WriteGameTarget(g_followTargetAddr);

    HWND w=FindGameWindow(); if(!w) return;
    RECT rc; GetClientRect(w,&rc);
    int cx=(rc.right-rc.left)/2, cy=(rc.bottom-rc.top)/2;

    float cd=dist*0.6f; if(cd>15.0f) cd=15.0f;
    int px=cx+(int)(dx/dist*cd*6.0f), py=cy+(int)(dy/dist*cd*6.0f);
    if(px<10)px=10; if(px>rc.right-10)px=rc.right-10;
    if(py<10)py=10; if(py>rc.bottom-10)py=rc.bottom-10;

    DebugLog("[FOLLOW] click client=(%d,%d) cd=%.1f dir=(%.2f,%.2f)", px, py, cd, dx/dist, dy/dist);

    ClickAtClient(px,py);
}

// ============================================================
// Process listing
// ============================================================
void RefreshProcesses() {
    g_procs.clear();
    SendMessageW(g_hProcList,LB_RESETCONTENT,0,0);
    HANDLE hs=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(hs==INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize=sizeof(pe);
    if(Process32FirstW(hs,&pe)){do{g_procs.push_back({pe.th32ProcessID,pe.szExeFile});}while(Process32NextW(hs,&pe));}
    CloseHandle(hs);
    std::sort(g_procs.begin(),g_procs.end(),[](const ProcInfo& a,const ProcInfo& b){return a.name<b.name;});
    for(size_t i=0;i<g_procs.size();i++){
        wchar_t b[256]; swprintf_s(b,L"%s  (PID %d)",g_procs[i].name.c_str(),g_procs[i].pid);
        SendMessageW(g_hProcList,LB_ADDSTRING,0,(LPARAM)b);
    }
}

// ============================================================
// UI Helpers
// ============================================================
void InitFont() { g_hFont=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI"); }
void SetFont(HWND h){SendMessageW(h,WM_SETFONT,(WPARAM)g_hFont,TRUE);}
HWND MkL(HWND p,const wchar_t*t,int x,int y,int w,int h){HWND hw=CreateWindowW(L"static",t,WS_CHILD|WS_VISIBLE,x,y,w,h,p,0,g_hInst,0);SetFont(hw);return hw;}
HWND MkE(HWND p,const wchar_t*d,int x,int y,int w,int h,DWORD ext=0){HWND hw=CreateWindowW(L"edit",d,WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|ext,x,y,w,h,p,0,g_hInst,0);SetFont(hw);return hw;}
HWND MkB(HWND p,const wchar_t*t,int x,int y,int w,int h,int id){HWND hw=CreateWindowW(L"button",t,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,w,h,p,(HMENU)(intptr_t)id,g_hInst,0);SetFont(hw);return hw;}
HWND MkC(HWND p,const wchar_t*t,int x,int y,int w,int id){HWND hw=CreateWindowW(L"button",t,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,w,22,p,(HMENU)(intptr_t)id,g_hInst,0);SetFont(hw);return hw;}
HWND MkLst(HWND p,int x,int y,int w,int h){HWND hw=CreateWindowW(L"listbox",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS,x,y,w,h,p,0,g_hInst,0);SetFont(hw);return hw;}

// ============================================================
// Create controls once
// ============================================================
void HideTabContent() {
    HWND all[]={g_hDllLabel,g_hDllPath,g_hBtnBrowse,g_hBtnRefresh,g_hBtnConnect,g_hBtnInjectDll,g_hProcList,
        g_hChkAttack,g_hChkHeal,g_hChkFollow,g_hChkLoot,g_hHealLabel,g_hHealThreshold,g_hHealPct,
        g_hAtkNameLabel,g_hAtkName,
        g_hFollowTitle,g_hFollowName,g_hFollowDist,g_hBtnFollowClear,g_hPlayerList,
        g_hTargetTitle,g_hTargetName,g_hTargetHP,g_hBtnTargetClear,g_hMobList,g_hNpcList};
    for(HWND h:all) if(h) ShowWindow(h,SW_HIDE);
}

void CreateAllControls(HWND p) {
    int x=20,y=48,w=555;
    // Tab 0: Connection
    g_hDllLabel=MkL(p,L"DLL:",x,y+2,30,18);
    g_hDllPath=MkE(p,L"",x+32,y,230,22);
    g_hBtnBrowse=MkB(p,L"...",x+265,y,30,22,1003);
    g_hBtnConnect=MkB(p,L"CONNECT",x+298,y,70,22,1002);
    g_hBtnRefresh=MkB(p,L"Refresh",x+371,y,58,22,1001);
    g_hBtnInjectDll=MkB(p,L"Inject DLL",x+432,y,70,22,1004);
    g_hProcList=MkLst(p,x,y+28,w,334);
    // Tab 1: Bot
    g_hChkAttack=MkC(p,L"[F1] Auto Attack - attack nearest mob",x,y,400,2001);
    g_hAtkNameLabel=MkL(p,L"Mob name:",x+25,y+28,65,20);
    g_hAtkName=MkE(p,L"",x+95,y+25,160,22);
    g_hChkLoot=MkC(p,L"[F4] Auto Loot - pick up corpses",x,y+52,400,2005);
    g_hChkHeal=MkC(p,L"[F2] Auto Heal - heal when HP low",x,y+78,400,2002);
    g_hHealLabel=MkL(p,L"Heal below:",x+25,y+106,80,20);
    g_hHealThreshold=MkE(p,L"60",x+110,y+103,45,24,ES_NUMBER);
    g_hHealPct=MkL(p,L"%",x+158,y+106,15,20);
    g_hChkFollow=MkC(p,L"[F3] Follow Player",x,y+134,400,2004);
    // Tab 2: Players
    g_hFollowTitle=MkL(p,L"FOLLOW TARGET:",x,y,200,20);
    g_hFollowName=MkL(p,L"(double-click a player to follow)",x,y+20,400,20);
    g_hFollowDist=MkL(p,L"",x,y+40,400,20);
    g_hBtnFollowClear=MkB(p,L"Stop Follow",x+420,y+18,90,22,3002);
    g_hPlayerList=MkLst(p,x,y+68,w,272);
    // Tab 3: Mobs
    g_hTargetTitle=MkL(p,L"SELECTED TARGET:",x,y,200,20);
    g_hTargetName=MkL(p,L"(double-click a mob to select)",x,y+20,400,20);
    g_hTargetHP=MkL(p,L"",x,y+40,400,20);
    g_hBtnTargetClear=MkB(p,L"Clear Target",x+420,y+18,90,22,3001);
    g_hMobList=MkLst(p,x,y+68,w,272);
    // Tab 4: NPCs
    g_hNpcList=MkLst(p,x,y,w,340);
    HideTabContent();
}

void ShowTab(int idx) {
    HideTabContent();
    HWND t0[]={g_hDllLabel,g_hDllPath,g_hBtnBrowse,g_hBtnRefresh,g_hBtnConnect,g_hBtnInjectDll,g_hProcList};
    HWND t1[]={g_hChkAttack,g_hAtkNameLabel,g_hAtkName,g_hChkLoot,g_hChkHeal,g_hHealLabel,g_hHealThreshold,g_hHealPct,g_hChkFollow};
    HWND t2[]={g_hFollowTitle,g_hFollowName,g_hFollowDist,g_hBtnFollowClear,g_hPlayerList};
    HWND t3[]={g_hTargetTitle,g_hTargetName,g_hTargetHP,g_hBtnTargetClear,g_hMobList};
    HWND t4[]={g_hNpcList};
    HWND* sets[]={t0,t1,t2,t3,t4}; int cnt[]={7,9,5,5,1};
    for(int i=0;i<cnt[idx];i++) ShowWindow(sets[idx][i],SW_SHOW);
    if(idx==0) RefreshProcesses();
}

// ============================================================
// Inject DLL (optional)
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
// Update UI
// ============================================================
void UpdateUI() {
    if(!g_connected||!g_hProcess) {
        SetWindowTextW(g_hStatus,g_hProcess?L"  Connected (memory read OK)":L"  Select Warspear and click CONNECT");
        return;
    }
    float sx,sy; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
    std::vector<EntityData> pl,mb,np;
    std::vector<CorpseData> corpses;
    if(!ReadGameState(sx,sy,hp,mhp,mn,mmn,name,level,classId,pl,mb,np,corpses)){
        SetWindowTextW(g_hStatus,L"  Cannot read game memory"); g_connected=false; return;
    }
    g_selfX=sx; g_selfY=sy;
    g_cachedCorpses=corpses;
    wchar_t buf[512];
    if(g_killCount>0 || g_lootCount>0) {
        swprintf_s(buf,L"  %s | Lv.%d %s | HP: %d/%d | Kills: %d | Loots: %d | Corpses: %d",
            name.c_str(),level,GetClassName(classId),hp,mhp,g_killCount,g_lootCount,(int)corpses.size());
    } else {
        swprintf_s(buf,L"  %s  |  Lv.%d %s  |  HP: %d/%d  |  Mana: %d/%d  |  Players: %d  Mobs: %d  NPCs: %d  Corpses: %d",
            name.c_str(),level,GetClassName(classId),hp,mhp,mn,mmn,(int)pl.size(),(int)mb.size(),(int)np.size(),(int)corpses.size());
    }
    SetWindowTextW(g_hStatus,buf);

    g_cachedPlayers=pl;
    static std::vector<EntityData> prevPlayers;
    bool playersChanged = (pl.size() != prevPlayers.size());
    if (!playersChanged) {
        for (size_t i = 0; i < pl.size(); i++) {
            if (pl[i].objAddr != prevPlayers[i].objAddr || pl[i].hp != prevPlayers[i].hp) {
                playersChanged = true; break;
            }
        }
    }
    if (playersChanged) {
        SendMessageW(g_hPlayerList,LB_RESETCONTENT,0,0);
        if(pl.empty()) SendMessageW(g_hPlayerList,LB_ADDSTRING,0,(LPARAM)L"(no players nearby)");
        for(auto& p:pl){
            wchar_t pfx[8]={}; if(g_followTargetAddr>0x1000&&p.objAddr==g_followTargetAddr) wcscpy_s(pfx,L"[>] ");
            swprintf_s(buf,L"%s%-20s  HP: %d/%d  Dist: %.1f",pfx,p.name,p.hp,p.maxHp,p.distance);
            SendMessageW(g_hPlayerList,LB_ADDSTRING,0,(LPARAM)buf);
        }
        prevPlayers = pl;
    }

    if(g_followTargetAddr>0x1000){
        bool f=false;
        for(auto&p:pl){if(p.objAddr==g_followTargetAddr){
            swprintf_s(buf,L"%s  (0x%X)",p.name,p.objAddr); SetWindowTextW(g_hFollowName,buf);
            swprintf_s(buf,L"Dist: %.1f  |  HP: %d/%d",p.distance,p.hp,p.maxHp); SetWindowTextW(g_hFollowDist,buf);
            f=true;break;}}
        if(!f){SetWindowTextW(g_hFollowName,L"(player left area)");SetWindowTextW(g_hFollowDist,L"");}
    }

    g_cachedMobs=mb;

    // Periodic stats summary every 30 seconds
    static int statsTimer=0;
    statsTimer++;
    if(statsTimer >= 60 && (g_killCount > 0 || g_lootCount > 0)) {
        statsTimer = 0;
        DWORD elapsed = (GetTickCount() - g_lastLootTime) / 1000;
        DebugLog("[STATS] ====================================");
        DebugLog("[STATS] Kills: %d  |  Loots: %d  |  Ratio: %s",
            g_killCount, g_lootCount,
            g_killCount > 0 ? "" : "N/A");
        DebugLog("[STATS] Last loot: '%S' (%d sec ago)", g_lastLootName, elapsed);
        DebugLog("[STATS] Corpses nearby: %d", (int)corpses.size());
        DebugLog("[STATS] ====================================");
    }
    // Only rebuild mob list if data changed
    static std::vector<EntityData> prevMobs;
    bool mobsChanged = (mb.size() != prevMobs.size());
    if (!mobsChanged) {
        for (size_t i = 0; i < mb.size(); i++) {
            if (mb[i].objAddr != prevMobs[i].objAddr || mb[i].hp != prevMobs[i].hp || mb[i].maxHp != prevMobs[i].maxHp) {
                mobsChanged = true; break;
            }
        }
    }
    if (mobsChanged) {
        int prevSel = (int)SendMessageW(g_hMobList, LB_GETCURSEL, 0, 0);
        DWORD prevSelAddr = 0;
        if (prevSel != LB_ERR && prevSel < (int)prevMobs.size()) prevSelAddr = prevMobs[prevSel].objAddr;
        SendMessageW(g_hMobList,LB_RESETCONTENT,0,0);
        if(mb.empty()) SendMessageW(g_hMobList,LB_ADDSTRING,0,(LPARAM)L"(no mobs nearby)");
        int newSel = 0;
        for(size_t i=0;i<mb.size();i++){
            auto&m=mb[i];
            wchar_t pfx[8]={}; if(g_selectedTargetAddr>0x1000&&m.objAddr==g_selectedTargetAddr) wcscpy_s(pfx,L"[>] ");
            swprintf_s(buf,L"%s%-20s  HP: %d/%d  Dist: %.1f%s",pfx,m.name,m.hp,m.maxHp,m.distance,m.hp<=0?L" [DEAD]":L"");
            SendMessageW(g_hMobList,LB_ADDSTRING,0,(LPARAM)buf);
            if (prevSelAddr > 0x1000 && m.objAddr == prevSelAddr) newSel = (int)i;
        }
        if (prevSel != LB_ERR) SendMessageW(g_hMobList, LB_SETCURSEL, newSel, 0);
        prevMobs = mb;

        // Death detection: mobs that disappeared from tree = likely killed manually
        if(g_autoLoot && !g_hasPendingCorpse && !g_prevMobs.empty()) {
            for(auto& prev : g_prevMobs) {
                if(prev.hp <= 0) continue;
                bool stillAlive = false;
                for(auto& cur : mb) {
                    if(cur.objAddr == prev.objAddr) { stillAlive = true; break; }
                }
                if(!stillAlive) {
                    g_pendingCorpseX = prev.x;
                    g_pendingCorpseY = prev.y;
                    wcscpy_s(g_pendingCorpseName, prev.name);
                    g_pendingCorpseTime = GetTickCount();
                    g_hasPendingCorpse = true;
                    DebugLog("[DEATH] Mob disappeared: '%S' (%.1f,%.1f) - saving corpse pos for loot", prev.name, prev.x, prev.y);
                }
            }
        }
        g_prevMobs = mb;
    }

    if(g_selectedTargetAddr>0x1000){
        bool f=false;
        for(auto&m:mb){if(m.objAddr==g_selectedTargetAddr){
            swprintf_s(buf,L"%s  (0x%X)",m.name,m.objAddr); SetWindowTextW(g_hTargetName,buf);
            int pct=m.maxHp>0?(m.hp*100)/m.maxHp:0;
            swprintf_s(buf,L"HP: %d / %d  (%d%%)",m.hp,m.maxHp,pct); SetWindowTextW(g_hTargetHP,buf);
            f=true;break;}}
        if(!f){SetWindowTextW(g_hTargetName,L"(target lost)");SetWindowTextW(g_hTargetHP,L"");}
    }

    static std::vector<EntityData> prevNpcs;
    bool npcsChanged = (np.size() != prevNpcs.size());
    if (!npcsChanged) {
        for (size_t i = 0; i < np.size(); i++) {
            if (np[i].objAddr != prevNpcs[i].objAddr || np[i].hp != prevNpcs[i].hp) {
                npcsChanged = true; break;
            }
        }
    }
    if (npcsChanged) {
        SendMessageW(g_hNpcList,LB_RESETCONTENT,0,0);
        if(np.empty()) SendMessageW(g_hNpcList,LB_ADDSTRING,0,(LPARAM)L"(no NPCs nearby)");
        for(auto&n:np){swprintf_s(buf,L"%-20s  HP: %d/%d  Dist: %.1f",n.name,n.hp,n.maxHp,n.distance);SendMessageW(g_hNpcList,LB_ADDSTRING,0,(LPARAM)buf);}
        prevNpcs = np;
    }
}

// ============================================================
// WndProc
// ============================================================
LRESULT CALLBACK WndProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam) {
    switch(msg) {
    case WM_CREATE: {
        InitFont();
        g_hTab=CreateWindowW(WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,10,10,575,420,hWnd,0,g_hInst,0);
        SetFont(g_hTab);
        TCITEMW tie{}; tie.mask=TCIF_TEXT;
        const wchar_t* tabs[]={L"Connection",L"Bot",L"Players",L"Mobs",L"NPCs"};
        for(int i=0;i<5;i++){tie.pszText=(LPWSTR)tabs[i];TabCtrl_InsertItem(g_hTab,i,&tie);}
        g_hStatus=CreateWindowW(STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,hWnd,0,g_hInst,0);
        SendMessageW(g_hStatus,WM_SETFONT,(WPARAM)g_hFont,TRUE);
        CreateAllControls(hWnd);
        ShowTab(0);
        break;
    }
    case WM_NOTIFY:
        if(((NMHDR*)lParam)->hwndFrom==g_hTab&&((NMHDR*)lParam)->code==TCN_SELCHANGE)
            ShowTab(TabCtrl_GetCurSel(g_hTab));
        break;

    case WM_COMMAND: {
        int id=LOWORD(wParam), code=HIWORD(wParam);

        // Connect (read memory only, no DLL)
        if(id==1002) {
            int sel=(int)SendMessageW(g_hProcList,LB_GETCURSEL,0,0);
            if(sel==LB_ERR||sel>=(int)g_procs.size()){
                DebugLog("[CONNECT] No process selected");
                MessageBoxW(hWnd,L"Select a process first!",L"",MB_OK|MB_ICONWARNING); break;
            }
            DWORD pid=g_procs[sel].pid;
            DebugLog("[CONNECT] Attempting to connect to PID %d (%S)", pid, g_procs[sel].name.c_str());
            if(g_hProcess) CloseHandle(g_hProcess);
            g_hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION,FALSE,pid);
            g_gamePid=pid;
            if(!g_hProcess){
                DebugLog("[CONNECT] FAILED to open process - error %d", GetLastError());
                MessageBoxW(hWnd,L"Cannot open process.\nTry running as Administrator.",L"Error",MB_OK|MB_ICONERROR);
                break;
            }
            DebugLog("[CONNECT] Process opened, reading game state...");
            float x,y; int hp,mhp,mn,mmn; std::wstring name; int level=0, classId=0;
            std::vector<EntityData> p,m,n;
            std::vector<CorpseData> corpses;
            if(ReadGameState(x,y,hp,mhp,mn,mmn,name,level,classId,p,m,n,corpses)){
                g_connected=true;
                DebugLog("[CONNECT] SUCCESS - Character: %S", name.c_str());
                DebugLog("[CONNECT] Position: (%.1f, %.1f) HP: %d/%d Mana: %d/%d Level: %d Class: %d", x, y, hp, mhp, mn, mmn, level, classId);
                DebugLog("[CONNECT] Entities: %d players, %d mobs, %d NPCs, %d corpses", (int)p.size(), (int)m.size(), (int)n.size(), (int)corpses.size());
                OpenDebugConsole(hWnd);
                wchar_t m2[256];
                swprintf_s(m2,L"Connected to PID %d!\n\nCharacter: %s\nLevel: %d\nClass: %s\nHP: %d/%d\nMana: %d/%d\nPlayers: %d  Mobs: %d  NPCs: %d\nCorpses: %d",
                    pid,name.c_str(),level,GetClassName(classId),hp,mhp,mn,mmn,(int)p.size(),(int)m.size(),(int)n.size(),(int)corpses.size());
                MessageBoxW(hWnd,m2,L"Connected!",MB_OK|MB_ICONINFORMATION);
            } else {
                DebugLog("[CONNECT] FAILED to read game memory");
                MessageBoxW(hWnd,L"Process opened but cannot read game memory.\nMake sure game is loaded.",L"Warning",MB_OK|MB_ICONWARNING);
            }
            break;
        }

        // Refresh
        if(id==1001){RefreshProcesses();break;}

        // Browse DLL
        if(id==1003) {
            OPENFILENAMEW ofn{}; wchar_t file[MAX_PATH]={};
            ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hWnd;
            ofn.lpstrFilter=L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
            ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
            if(GetOpenFileNameW(&ofn)) SetWindowTextW(g_hDllPath,file);
            break;
        }

        // Inject DLL
        if(id==1004) {
            if(!g_hProcess||!g_connected){
                DebugLog("[INJECT] Cannot inject - not connected");
                MessageBoxW(hWnd,L"Connect to the process first!",L"",MB_OK|MB_ICONWARNING); break;
            }
            DebugLog("[INJECT] Creating shared memory...");
            // Create shared memory for DLL communication
            if(!g_hSharedMem){
                g_hSharedMem = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BotCmd), L"Local\\WarspearBotShared");
                if(g_hSharedMem) g_pBotCmd = (BotCmd*)MapViewOfFile(g_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BotCmd));
            }
            if(!g_pBotCmd){
                DebugLog("[INJECT] FAILED to create shared memory");
                MessageBoxW(hWnd,L"Cannot create shared memory.",L"Error",MB_OK|MB_ICONERROR); break;
            }
            ZeroMemory(g_pBotCmd, sizeof(BotCmd));
            wchar_t dll[MAX_PATH]; GetWindowTextW(g_hDllPath,dll,MAX_PATH);
            if(dll[0]==0){
                DebugLog("[INJECT] No DLL path specified");
                MessageBoxW(hWnd,L"Enter or browse for a DLL path.",L"",MB_OK|MB_ICONWARNING);break;
            }
            if(!wcschr(dll,L':')){
                wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL,dir,MAX_PATH);
                wchar_t* bs=wcsrchr(dir,L'\\'); if(bs)*bs=0;
                wchar_t full[MAX_PATH]; swprintf_s(full,L"%s\\%s",dir,dll); wcscpy_s(dll,full);
            }
            DebugLog("[INJECT] DLL path: %S", dll);
            DebugLog("[INJECT] Target PID: %d", g_gamePid);
            if(InjectDLL(g_gamePid,dll)){
                g_dllInjected=true;
                OpenDebugConsole(hWnd);
                DebugLog("[INJECT] SUCCESS - DLL injected!");
                DebugLog("[INJECT] Shared memory: attackOn=%d followOn=%d", g_pBotCmd->attackOn, g_pBotCmd->followOn);
                SetWindowTextW(g_hStatus,L"  DLL injected! Bot active via DLL.");
            } else {
                DebugLog("[INJECT] FAILED - injection error %d", GetLastError());
                MessageBoxW(hWnd,L"DLL injection failed.",L"Error",MB_OK|MB_ICONERROR);
            }
            break;
        }

        // Mob double-click -> select target
        if((HWND)lParam==g_hMobList&&code==LBN_DBLCLK) {
            int sel=(int)SendMessageW(g_hMobList,LB_GETCURSEL,0,0);
            if(sel!=LB_ERR&&sel>=0&&sel<(int)g_cachedMobs.size()){
                auto&m=g_cachedMobs[sel];
                if(m.hp>0){
                    g_selectedTargetAddr=m.objAddr; wcscpy_s(g_selTargetName,m.name);
                    DebugLog("[TARGET] Selected: %S (0x%08X) HP=%d/%d Dist=%.1f", m.name, m.objAddr, m.hp, m.maxHp, m.distance);
                    if(g_dllInjected && g_pBotCmd) {
                        g_pBotCmd->targetAddr = m.objAddr;
                        g_pBotCmd->attackOn = 1;
                        DebugLog("[TARGET] DLL attack ON via shared memory");
                        wchar_t s[128]; swprintf_s(s,L"  Target: %s (DLL attack ON)",m.name);
                        SetWindowTextW(g_hStatus,s);
                    } else {
                        bool ok=WriteGameTarget(m.objAddr);
                        DebugLog("[TARGET] Direct write: %s", ok ? "OK" : "FAILED");
                        wchar_t s[128]; swprintf_s(s,L"  Target: %s %s",m.name,ok?L"(set)":L"(write failed)");
                        SetWindowTextW(g_hStatus,s);
                    }
                }
            }
            break;
        }

        // Player double-click -> follow
        if((HWND)lParam==g_hPlayerList&&code==LBN_DBLCLK) {
            int sel=(int)SendMessageW(g_hPlayerList,LB_GETCURSEL,0,0);
            if(sel!=LB_ERR&&sel>=0&&sel<(int)g_cachedPlayers.size()){
                auto&p=g_cachedPlayers[sel];
                g_followTargetAddr=p.objAddr; wcscpy_s(g_followName,p.name);
                DebugLog("[FOLLOW] Following: %S (0x%08X) Dist=%.1f HP=%d/%d", p.name, p.objAddr, p.distance, p.hp, p.maxHp);
                if(g_dllInjected && g_pBotCmd) {
                    g_pBotCmd->followAddr = p.objAddr;
                    g_pBotCmd->followOn = 1;
                    KillTimer(hWnd,3);
                    DebugLog("[FOLLOW] DLL follow ON via shared memory");
                    wchar_t s[128]; swprintf_s(s,L"  Following: %s (DLL follow ON)",p.name);
                    SetWindowTextW(g_hStatus,s);
                } else {
                    SendMessageW(g_hChkFollow,BM_SETCHECK,BST_CHECKED,0);
                    SetTimer(hWnd,3,800,NULL);
                    DebugLog("[FOLLOW] Direct follow started (800ms timer)");
                    wchar_t s[128]; swprintf_s(s,L"  Following: %s (dist: %.1f)",p.name,p.distance);
                    SetWindowTextW(g_hStatus,s);
                }
            }
            break;
        }

        if(id==3001){
            g_selectedTargetAddr=0;g_selTargetName[0]=0;
            if(g_pBotCmd){g_pBotCmd->attackOn=0;g_pBotCmd->targetAddr=0;}
            SetWindowTextW(g_hTargetName,L"(double-click a mob to select)");SetWindowTextW(g_hTargetHP,L"");
            break;
        }
        if(id==3002){
            g_followTargetAddr=0;g_followName[0]=0;
            if(g_pBotCmd){g_pBotCmd->followOn=0;g_pBotCmd->followAddr=0;}
            SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);KillTimer(hWnd,3);
            SetWindowTextW(g_hFollowName,L"(double-click a player to follow)");SetWindowTextW(g_hFollowDist,L"");
            SetWindowTextW(g_hStatus,L"  Follow: OFF");
            break;
        }

        if(id==2001){
            BOOL on=SendMessage((HWND)lParam,BM_GETCHECK,0,0)==BST_CHECKED;
            if(on){
                // Read mob name filter
                wchar_t filter[64]={}; GetWindowTextW(g_hAtkName, filter, 64);
                bool hasFilter = (filter[0] != 0);
                if(g_dllInjected && g_pBotCmd) {
                    KillTimer(hWnd,3); SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);
                    if(g_selectedTargetAddr<=0x1000&&!g_cachedMobs.empty()){
                        for(auto&m:g_cachedMobs){
                            if(m.hp<=0||IsNPC(m.name)) continue;
                            if(hasFilter && wcsstr(m.name, filter)==NULL) continue;
                            g_selectedTargetAddr=m.objAddr;wcscpy_s(g_selTargetName,m.name);g_pBotCmd->targetAddr=m.objAddr;break;
                        }
                    }
                    g_pBotCmd->attackOn=1;
                    SetWindowTextW(g_hStatus,L"  [DLL] Auto Attack: ON");
                } else {
                    KillTimer(hWnd,3); SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);
                    if(g_selectedTargetAddr<=0x1000&&!g_cachedMobs.empty()){
                        for(auto&m:g_cachedMobs){
                            if(m.hp<=0||IsNPC(m.name)) continue;
                            if(hasFilter && wcsstr(m.name, filter)==NULL) continue;
                            g_selectedTargetAddr=m.objAddr;wcscpy_s(g_selTargetName,m.name);WriteGameTarget(m.objAddr);break;
                        }
                    }
                    SetTimer(hWnd,2,1500,NULL); SetWindowTextW(g_hStatus,L"  Auto Attack: ON  (F1 to toggle)");
                }
            } else {
                if(g_dllInjected && g_pBotCmd) g_pBotCmd->attackOn=0;
                else KillTimer(hWnd,2);
                SetWindowTextW(g_hStatus,L"  Auto Attack: OFF");
            }
            break;
        }
        if(id==2004){
            BOOL on=SendMessage((HWND)lParam,BM_GETCHECK,0,0)==BST_CHECKED;
            if(on){
                if(g_dllInjected && g_pBotCmd) {
                    KillTimer(hWnd,2); SendMessageW(g_hChkAttack,BM_SETCHECK,BST_UNCHECKED,0);
                    if(g_followTargetAddr<=0x1000&&!g_cachedPlayers.empty()){
                        auto&p=g_cachedPlayers[0]; g_followTargetAddr=p.objAddr; wcscpy_s(g_followName,p.name);
                    }
                    if(g_followTargetAddr>0x1000){g_pBotCmd->followAddr=g_followTargetAddr;g_pBotCmd->followOn=1;wchar_t s[128];swprintf_s(s,L"  [DLL] Following: %s",g_followName);SetWindowTextW(g_hStatus,s);}
                    else{SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);SetWindowTextW(g_hStatus,L"  No players to follow");}
                } else {
                    KillTimer(hWnd,2); SendMessageW(g_hChkAttack,BM_SETCHECK,BST_UNCHECKED,0);
                    if(g_followTargetAddr<=0x1000&&!g_cachedPlayers.empty()){
                        auto&p=g_cachedPlayers[0]; g_followTargetAddr=p.objAddr; wcscpy_s(g_followName,p.name);
                    }
                    if(g_followTargetAddr>0x1000){SetTimer(hWnd,3,800,NULL);wchar_t s[128];swprintf_s(s,L"  Following: %s  (F3 to toggle)",g_followName);SetWindowTextW(g_hStatus,s);}
                    else{SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);SetWindowTextW(g_hStatus,L"  No players to follow");}
                }
            } else { KillTimer(hWnd,3); SetWindowTextW(g_hStatus,L"  Follow: OFF"); }
            break;
        }
        if(id==2002){
            BOOL on=SendMessage((HWND)lParam,BM_GETCHECK,0,0)==BST_CHECKED;
            wchar_t s[64];swprintf_s(s,L"Auto Heal %s",on?L"ON":L"OFF");SetWindowTextW(g_hStatus,s);
            break;
        }
        if(id==2005){
            BOOL on=SendMessage((HWND)lParam,BM_GETCHECK,0,0)==BST_CHECKED;
            g_autoLoot = (on != FALSE);
            if(on){
                SetTimer(hWnd,4,1200,NULL);
                DebugLog("[LOOT] Auto Loot: ON (1200ms timer)");
                SetWindowTextW(g_hStatus,L"  Auto Loot: ON  (F4 to toggle)");
            } else {
                KillTimer(hWnd,4);
                DebugLog("[LOOT] Auto Loot: OFF");
                SetWindowTextW(g_hStatus,L"  Auto Loot: OFF");
            }
            break;
        }
        break;
    }

    case WM_TIMER: {
        // Global hotkeys (F1/F2/F3/F4) - check on key press transition
        if(wParam==1) {
            // Check hotkeys first - only toggle on key press transition (not held down)
            static bool f1Prev = false, f2Prev = false, f3Prev = false, f4Prev = false;
            bool f1Curr = (GetAsyncKeyState(VK_F1) & 1) != 0;
            bool f2Curr = (GetAsyncKeyState(VK_F2) & 1) != 0;
            bool f3Curr = (GetAsyncKeyState(VK_F3) & 1) != 0;
            bool f4Curr = (GetAsyncKeyState(VK_F4) & 1) != 0;

            // F2: STOP ALL (panic button)
            if (f2Curr && !f2Prev) {
                KillTimer(hWnd,2); KillTimer(hWnd,3); KillTimer(hWnd,4);
                SendMessage(g_hChkAttack,BM_SETCHECK,BST_UNCHECKED,0);
                SendMessage(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);
                SendMessage(g_hChkLoot,BM_SETCHECK,BST_UNCHECKED,0);
                g_selectedTargetAddr=0; g_followTargetAddr=0; g_autoLoot=false;
                wcscpy_s(g_selTargetName,L""); wcscpy_s(g_followName,L"");
                if(g_pBotCmd){
                    g_pBotCmd->attackOn=0;g_pBotCmd->followOn=0;
                    g_pBotCmd->targetAddr=0;g_pBotCmd->followAddr=0;
                    g_pBotCmd->healOn=0;
                }
                SetWindowTextW(g_hStatus,L"  [F2] ALL STOPPED");
                DebugLog("[F2] ====================================");
                DebugLog("[F2] PANIC STOP - all timers killed");
                DebugLog("[F2] Stats: Kills=%d Loots=%d", g_killCount, g_lootCount);
                DebugLog("[F2] ====================================");
            }

            // F1 toggle: only act on press transition (release->press)
            if (f1Curr && !f1Prev) {
                if(g_connected) {
                    BOOL cur=SendMessage(g_hChkAttack,BM_GETCHECK,0,0)==BST_CHECKED;
                    SendMessage(g_hChkAttack,BM_SETCHECK,cur?BST_UNCHECKED:BST_CHECKED,0);
                    if(!cur) {
                        KillTimer(hWnd,3); SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);
                        // Read mob name filter
                        wchar_t filter[64]={}; GetWindowTextW(g_hAtkName, filter, 64);
                        bool hasFilter = (filter[0] != 0);
                        if(g_selectedTargetAddr<=0x1000&&!g_cachedMobs.empty()){
                            for(auto&m:g_cachedMobs){
                                if(m.hp<=0||IsNPC(m.name)) continue;
                                if(hasFilter && wcsstr(m.name, filter)==NULL) continue;
                                g_selectedTargetAddr=m.objAddr;wcscpy_s(g_selTargetName,m.name);
                                if(g_dllInjected && g_pBotCmd) g_pBotCmd->targetAddr=m.objAddr;
                                else WriteGameTarget(m.objAddr);
                                break;
                            }
                        }
                        if(g_dllInjected && g_pBotCmd) {
                            g_pBotCmd->attackOn=1;
                            SetWindowTextW(g_hStatus,L"  [DLL][F1] Auto Attack: ON");
                        } else {
                            SetTimer(hWnd,2,1500,NULL); SetWindowTextW(g_hStatus,L"  [F1] Auto Attack: ON");
                        }
                    } else {
                        if(g_dllInjected && g_pBotCmd) g_pBotCmd->attackOn=0;
                        else KillTimer(hWnd,2);
                        SetWindowTextW(g_hStatus,L"  [F1] Auto Attack: OFF");
                    }
                }
            }
            // F3 toggle: only act on press transition (release->press)
            if (f3Curr && !f3Prev) {
                if(g_connected) {
                    BOOL cur=SendMessage(g_hChkFollow,BM_GETCHECK,0,0)==BST_CHECKED;
                    SendMessage(g_hChkFollow,BM_SETCHECK,cur?BST_UNCHECKED:BST_CHECKED,0);
                    if(!cur) {
                        KillTimer(hWnd,2); SendMessageW(g_hChkAttack,BM_SETCHECK,BST_UNCHECKED,0);
                        if(g_followTargetAddr<=0x1000&&!g_cachedPlayers.empty()){
                            auto&p=g_cachedPlayers[0]; g_followTargetAddr=p.objAddr; wcscpy_s(g_followName,p.name);
                        }
                        if(g_dllInjected && g_pBotCmd) {
                            if(g_followTargetAddr>0x1000){g_pBotCmd->followAddr=g_followTargetAddr;g_pBotCmd->followOn=1;wchar_t s[128];swprintf_s(s,L"  [DLL][F3] Following: %s",g_followName);SetWindowTextW(g_hStatus,s);}
                            else{SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);SetWindowTextW(g_hStatus,L"  No players to follow");}
                        } else {
                            if(g_followTargetAddr>0x1000){SetTimer(hWnd,3,800,NULL);wchar_t s[128];swprintf_s(s,L"  [F3] Following: %s",g_followName);SetWindowTextW(g_hStatus,s);}
                            else{SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);SetWindowTextW(g_hStatus,L"  No players to follow");}
                        }
                    } else {
                        if(g_dllInjected && g_pBotCmd) g_pBotCmd->followOn=0;
                        else KillTimer(hWnd,3);
                        SetWindowTextW(g_hStatus,L"  [F3] Follow: OFF");
                    }
                }
            }
            // F4 toggle: auto loot
            if (f4Curr && !f4Prev) {
                if(g_connected) {
                    BOOL cur=SendMessage(g_hChkLoot,BM_GETCHECK,0,0)==BST_CHECKED;
                    SendMessage(g_hChkLoot,BM_SETCHECK,cur?BST_UNCHECKED:BST_CHECKED,0);
                    g_autoLoot = !cur;
                    if(!cur) {
                        SetTimer(hWnd,4,1200,NULL);
                        DebugLog("[LOOT][F4] Auto Loot: ON");
                        SetWindowTextW(g_hStatus,L"  [F4] Auto Loot: ON");
                    } else {
                        KillTimer(hWnd,4);
                        DebugLog("[LOOT][F4] Auto Loot: OFF");
                        SetWindowTextW(g_hStatus,L"  [F4] Auto Loot: OFF");
                    }
                }
            }
            f1Prev = f1Curr;
            f2Prev = f2Curr;
            f3Prev = f3Curr;
            f4Prev = f4Curr;
            UpdateUI();
        }

        if(wParam==2) { // Attack
            HWND gw = FindGameWindow();
            if(!gw || GetForegroundWindow()!=gw || IsIconic(gw)) break;

            wchar_t filter[64]={};
            GetWindowTextW(g_hAtkName, filter, 64);
            bool hasFilter = (filter[0] != 0);

            if(g_selectedTargetAddr>0x1000&&g_hProcess) {
                DWORD hp=0; SIZE_T r=0;
                ReadProcessMemory(g_hProcess,(LPCVOID)(g_selectedTargetAddr+Game::ENT_HP),&hp,4,&r);
                DebugLog("[ATK] target=0x%08X hp=%d read=%d name=%S", g_selectedTargetAddr, hp, (int)r, g_selTargetName);
                if(hp>0) {
                    DebugLog("[ATK] Writing target + sending enter attack...");
                    WriteGameTarget(g_selectedTargetAddr);
                    SendEnterAttack();
                    DebugLog("[ATK] Attack sent!");
                } else {
                    g_killCount++;
                    DebugLog("[ATK] Target dead! Kills=%d name=%S", g_killCount, g_selTargetName);

                    // Save corpse position for auto-loot
                    if(g_autoLoot) {
                        SIZE_T r2=0; int rawX=0, rawY=0;
                        ReadProcessMemory(g_hProcess,(LPCVOID)(g_selectedTargetAddr+Game::ENT_RAW_X),&rawX,4,&r2);
                        ReadProcessMemory(g_hProcess,(LPCVOID)(g_selectedTargetAddr+Game::ENT_RAW_Y),&rawY,4,&r2);
                        if(r2==4) {
                            g_pendingCorpseX = rawX / 65536.0f;
                            g_pendingCorpseY = rawY / 65536.0f;
                            wcscpy_s(g_pendingCorpseName, g_selTargetName);
                            g_pendingCorpseTime = GetTickCount();
                            g_hasPendingCorpse = true;
                            DebugLog("[LOOT] Saved corpse position: '%S' (%.1f, %.1f)", g_pendingCorpseName, g_pendingCorpseX, g_pendingCorpseY);
                        }
                    }

                    bool found=false;
                    for(auto&m:g_cachedMobs){
                        if(m.hp<=0||IsNPC(m.name)||m.objAddr==g_selectedTargetAddr) continue;
                        if(hasFilter && wcsstr(m.name, filter)==NULL) continue;
                        DebugLog("[ATK] New target: %S (0x%08X) HP=%d/%d", m.name, m.objAddr, m.hp, m.maxHp);
                        g_selectedTargetAddr=m.objAddr;wcscpy_s(g_selTargetName,m.name);
                        WriteGameTarget(m.objAddr);SendEnterAttack();found=true;break;
                    }
                    if(!found) {
                        DebugLog("[ATK] No more mobs alive%s", hasFilter ? " matching filter" : "");
                        SetWindowTextW(g_hStatus, hasFilter ? L"  No matching mobs" : L"  No more mobs");
                    }
                }
            }
        }

        if(wParam==3) { // Follow
            HWND gw = FindGameWindow();
            if(!gw || GetForegroundWindow()!=gw || IsIconic(gw)) break;

            if(g_followTargetAddr>0x1000&&g_hProcess) {
                SIZE_T r=0; int rx=0,ry=0;
                ReadProcessMemory(g_hProcess,(LPCVOID)(g_followTargetAddr+Game::ENT_RAW_X),&rx,4,&r);
                ReadProcessMemory(g_hProcess,(LPCVOID)(g_followTargetAddr+Game::ENT_RAW_Y),&ry,4,&r);
                DebugLog("[FOLLOW TIMER] addr=0x%08X r=%d rx=%d ry=%d name=%S", g_followTargetAddr, (int)r, rx, ry, g_followName);
                if(r==4&&(rx!=0||ry!=0)) {
                    FollowTarget();
                } else {
                    DebugLog("[FOLLOW] Target lost - raw read failed");
                    g_followTargetAddr=0; KillTimer(hWnd,3);
                    SendMessageW(g_hChkFollow,BM_SETCHECK,BST_UNCHECKED,0);
                    SetWindowTextW(g_hStatus,L"  Follow target lost");
                }
            } else {
                DebugLog("[FOLLOW TIMER] invalid addr=0x%08X process=%d", g_followTargetAddr, g_hProcess!=NULL);
            }
        }

        if(wParam==4) { // Auto Loot
            if(!g_autoLoot || !g_hProcess) break;
            HWND w=FindGameWindow();
            if(!w || GetForegroundWindow()!=w || IsIconic(w)) { break; }

            // Priority 1: Use pending corpse (position saved by attack timer)
            if(g_hasPendingCorpse) {
                float dx = g_pendingCorpseX - g_selfX;
                float dy = g_pendingCorpseY - g_selfY;
                float dist = sqrtf(dx*dx + dy*dy);
                DWORD elapsed = GetTickCount() - g_pendingCorpseTime;

                DebugLog("[LOOT] Pending corpse '%S' dist=%.1f age=%dms", g_pendingCorpseName, dist, elapsed);

                // Timeout: corpse too old (10 seconds), give up
                if(elapsed > 10000) {
                    DebugLog("[LOOT] Corpse expired (10s timeout)");
                    g_hasPendingCorpse = false;
                } else if(dist > 2.0f) {
                    // FAR: Walk toward corpse
                    RECT rc; GetClientRect(w,&rc);
                    int cx = (rc.right-rc.left)/2;
                    int cy = (rc.bottom-rc.top)/2;
                    float cd = dist * 0.6f; if(cd > 15.0f) cd = 15.0f;
                    int px = cx + (int)(dx/dist * cd * 6.0f);
                    int py = cy + (int)(dy/dist * cd * 6.0f);
                    if(px<10)px=10; if(px>rc.right-10)px=rc.right-10;
                    if(py<10)py=10; if(py>rc.bottom-10)py=rc.bottom-10;
                    DebugLog("[LOOT] Walking to corpse: click (%d,%d)", px, py);
                    ClickAtClient(px, py);
                } else {
                    // CLOSE: Click on corpse + Enter to Take All
                    RECT rc; GetClientRect(w,&rc);
                    int cx = (rc.right-rc.left)/2;
                    int cy = (rc.bottom-rc.top)/2;
                    int px = cx + (int)(dx * 20.0f);
                    int py = cy + (int)(dy * 20.0f);
                    if(px<10)px=10; if(px>rc.right-10)px=rc.right-10;
                    if(py<10)py=10; if(py>rc.bottom-10)py=rc.bottom-10;
                    DebugLog("[LOOT] Clicking corpse at client=(%d,%d)", px, py);
                    ClickAtClient(px, py);
                    Sleep(300);
                    SendInputKey(VK_RETURN);
                    Sleep(200);
                    g_lootCount++;
                    g_lootClickCount++;
                    wcscpy_s(g_lastLootName, g_pendingCorpseName);
                    g_lastLootTime = GetTickCount();
                    DebugLog("[LOOT] Looted! #%d '%S' | Total loots: %d | Kills: %d",
                        g_lootClickCount, g_pendingCorpseName, g_lootCount, g_killCount);
                    g_hasPendingCorpse = false;
                }
                break;
            }

            // Priority 2: Use cached corpses from entity tree (legacy, may not find any)
            if(!g_cachedCorpses.empty()) {
                auto& c = g_cachedCorpses[0];
                float dx = c.x - g_selfX;
                float dy = c.y - g_selfY;
                float dist = sqrtf(dx*dx + dy*dy);

                DebugLog("[LOOT] Tree corpse '%S' dist=%.1f", c.name, dist);

                if(dist > 2.0f) {
                    RECT rc; GetClientRect(w,&rc);
                    int cx = (rc.right-rc.left)/2;
                    int cy = (rc.bottom-rc.top)/2;
                    float cd = dist * 0.6f; if(cd > 15.0f) cd = 15.0f;
                    int px = cx + (int)(dx/dist * cd * 6.0f);
                    int py = cy + (int)(dy/dist * cd * 6.0f);
                    if(px<10)px=10; if(px>rc.right-10)px=rc.right-10;
                    if(py<10)py=10; if(py>rc.bottom-10)py=rc.bottom-10;
                    DebugLog("[LOOT] Walking to corpse: click (%d,%d)", px, py);
                    ClickAtClient(px, py);
                } else {
                    RECT rc; GetClientRect(w,&rc);
                    int cx = (rc.right-rc.left)/2;
                    int cy = (rc.bottom-rc.top)/2;
                    int px = cx + (int)(dx * 20.0f);
                    int py = cy + (int)(dy * 20.0f);
                    if(px<10)px=10; if(px>rc.right-10)px=rc.right-10;
                    if(py<10)py=10; if(py>rc.bottom-10)py=rc.bottom-10;
                    DebugLog("[LOOT] Clicking corpse at client=(%d,%d)", px, py);
                    ClickAtClient(px, py);
                    Sleep(300);
                    SendInputKey(VK_RETURN);
                    Sleep(200);
                    g_lootCount++;
                    g_lootClickCount++;
                    wcscpy_s(g_lastLootName, c.name);
                    g_lastLootTime = GetTickCount();
                    DebugLog("[LOOT] Looted! #%d '%S' | Total loots: %d | Kills: %d",
                        g_lootClickCount, c.name, g_lootCount, g_killCount);
                }
            }
        }
        break;
    }

    case WM_SIZE: if(g_hStatus) SendMessage(g_hStatus,WM_SIZE,0,0); break;
    case WM_DESTROY:
        DebugLog("[EXIT] Shutting down...");
        CloseDebugConsole();
        KillTimer(hWnd,1);KillTimer(hWnd,2);KillTimer(hWnd,3);KillTimer(hWnd,4);
        if(g_hFont)DeleteObject(g_hFont);
        if(g_hProcess)CloseHandle(g_hProcess);
        PostQuitMessage(0); break;
    default: return DefWindowProcW(hWnd,msg,wParam,lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int nShow) {
    g_hInst=hInst;
    INITCOMMONCONTROLSEX icex{sizeof(icex),ICC_TAB_CLASSES|ICC_BAR_CLASSES};
    InitCommonControlsEx(&icex);
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"WarspearBotCtrl"; RegisterClassExW(&wc);
    g_hWnd=CreateWindowExW(0,L"WarspearBotCtrl",L"Warspear Bot Controller v3",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,610,500,NULL,NULL,hInst,NULL);
    if(!g_hWnd) return 0;
    ShowWindow(g_hWnd,nShow); UpdateWindow(g_hWnd);
    SetTimer(g_hWnd,1,500,NULL);
    MSG msg{};
    while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return (int)msg.wParam;
}
