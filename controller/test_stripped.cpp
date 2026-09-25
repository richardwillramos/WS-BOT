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

namespace Game {
    constexpr DWORD GM_PTR=0x00D8F98C, GM_OFFSET=0x14, LP_OFFSET=0x40, ENTITY_TREE=0x3C;
    constexpr DWORD TN_LEFT=0x04, TN_RIGHT=0x08, TN_OBJ=0x14, TH_ROOT=0x00, TH_COUNT=0x04;
    constexpr DWORD ENT_VTABLE=0x00, ENT_RAW_X=0x10, ENT_RAW_Y=0x14;
    constexpr DWORD ENT_NAME_PTR=0x58, ENT_NAME_LEN=0x60, ENT_TYPE_IND=0x090;
    constexpr DWORD ENT_HP=0x110, ENT_MAX_HP=0x114, ENT_MANA=0x118, ENT_MAX_MANA=0x11C;
    constexpr DWORD ENT_LEVEL=0x2E4, ENT_CLASS_IND=0x3F9;
    constexpr DWORD VT_PLAYER=0x00CD12D0, VT_BEAST=0x00CD17B4, VT_CORPSE=0;
    constexpr DWORD OBJ_OBJECT_ID=0x120, OBJ_TYPE_ID=0x124;
    constexpr DWORD CURSOR_OFFSET=0x1244, CUR_X=0x08, CUR_Y=0x0A;
    constexpr DWORD CUR_RAW_X=0x10, CUR_RAW_Y=0x14, CUR_FLAG=0x7C;
}

struct EntityData { wchar_t name[64]; float x,y; int hp,maxHp,mana,maxMana; int level,classId; float distance; int type; DWORD objAddr; };
struct CorpseData { wchar_t name[64]; float x,y; float distance; DWORD objAddr; WORD objectId,typeId; };
struct ProcInfo { DWORD pid; std::wstring name; };
struct BotCmd { volatile long attackOn,followOn,healOn,healThreshold,targetAddr,followAddr; };

struct BotState {
    ModuleManager modMgr;
    TargeterModule targeter; AttackerModule attacker; HealerModule healer;
    PartyHealerModule partyHealer; LooterModule looter; FollowerModule follower; ExtraModule extra;
    std::vector<EntityData> cachedMobs, cachedPlayers, cachedNpcs;
    std::vector<CorpseData> cachedCorpses;
    std::vector<ProcInfo> procs; std::vector<int> listToProc;
    IModule* activeModule=nullptr; BotCmd* pBotCmd=nullptr; HANDLE hSharedMem=NULL;
};
static BotState* G=nullptr;

static HINSTANCE g_hInst=NULL; static HWND g_hWnd=NULL; static HMENU g_hMenu=NULL;
static HWND g_hStatus=NULL; static HWND g_hTreeView=NULL; static HWND g_hDetailPanel=NULL; static HWND g_hDetailLabel=NULL;
static HFONT g_hFont=NULL; static HANDLE g_hProcess=NULL; static DWORD g_gamePid=0;
static bool g_connected=false; static bool g_dllInjected=false;
static float g_selfX=0, g_selfY=0, g_scale=3.5f;
static DWORD g_playerAddr=0, g_gmAddr=0;
static HWND g_hProcList=NULL, g_hBtnConnect=NULL, g_hBtnRefresh=NULL;
static HWND g_hDllPath=NULL, g_hBtnBrowse=NULL, g_hBtnInject=NULL, g_hProcPanel=NULL;
static HWND g_hDebugConsole=NULL, g_hDebugEdit=NULL; static bool g_debugConsoleOpen=false;

enum MenuID { IDM_CONFIG=1001, IDM_QUICK, IDM_CONNECTION, IDM_REFRESH, IDM_CONNECT, IDM_INJECT, IDM_BROWSE_DLL,
    IDM_TOGGLE_ATTACK, IDM_TOGGLE_FOLLOW, IDM_TOGGLE_LOOT, IDM_TOGGLE_HEAL, IDM_TOGGLE_ALL, IDM_STOP_ALL,
    IDM_SCALE_UP, IDM_SCALE_DOWN, IDM_DEBUG, IDM_EXIT };

template<typename T> T Read(DWORD a){ T v{}; if(g_hProcess&&a>0x1000) ReadProcessMemory(g_hProcess,(LPCVOID)a,&v,sizeof(T),NULL); return v; }
template<typename T> bool Write(DWORD a,T v){ if(!g_hProcess||a<=0x1000)return false; SIZE_T w=0; return WriteProcessMemory(g_hProcess,(LPVOID)a,&v,sizeof(T),&w)&&w==sizeof(T); }
float CalcDist(float x1,float y1,float x2,float y2){ float dx=x2-x1,dy=y2-y1; return sqrtf(dx*dx+dy*dy); }

static const wchar_t* NPC_NAMES[]={L"Miliciano",L"Guarda",L"Balisteiro",L"Almoxarife",L"Vicente",L"Leiloeira Ilse",L"Vilma",L"Rokus",L"Mestre Hedwig",L"Citadino",L"Citadina",L"Gregrio",L"Moraes",L"Kpqbqtqk",L"Norberto, o aougueiro"};
static const int NPC_COUNT=sizeof(NPC_NAMES)/sizeof(NPC_NAMES[0]);
bool IsNPC(const std::wstring& n){for(int i=0;i<NPC_COUNT;i++)if(n==NPC_NAMES[i])return true;return false;}
const wchar_t* GetClassName(int c){switch(c){case 0:return L"Undefined";case 1:return L"Paladin";case 2:return L"Priest";case 3:return L"Mage";case 4:return L"Barbarian";case 5:return L"Rogue";case 6:return L"Shaman";case 7:return L"Bladedancer";case 8:return L"Ranger";case 9:return L"Druid";case 10:return L"Deathknight";case 11:return L"Necromancer";case 12:return L"Warlock";case 13:return L"Seeker";case 14:return L"Hunter";case 15:return L"Warden";case 16:return L"Charmer";case 17:return L"Templar";case 18:return L"Chieftain";case 19:return L"Beastmaster";case 20:return L"Reaper";default:return L"Unknown";}}

void DebugLog(const char* fmt,...){char buf[1024];va_list a;va_start(a,fmt);_vsnprintf(buf,sizeof(buf),fmt,a);va_end(a);OutputDebugStringA(buf);OutputDebugStringA("\n");if(g_hDebugEdit){int l=GetWindowTextLengthA(g_hDebugEdit);SendMessageA(g_hDebugEdit,EM_SETSEL,l,l);SendMessageA(g_hDebugEdit,EM_REPLACESEL,FALSE,(LPARAM)buf);}}
void InitFont(){g_hFont=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI");}
void SetFont(HWND h){SendMessageW(h,WM_SETFONT,(WPARAM)g_hFont,TRUE);}

HWND FindGameWindow(){if(!g_gamePid)return NULL;struct Ctx{DWORD pid;HWND h;}ctx={g_gamePid,NULL};EnumWindows([](HWND h,LPARAM lp)->BOOL{auto c=(Ctx*)lp;DWORD p=0;GetWindowThreadProcessId(h,&p);if(p==c->pid&&IsWindowVisible(h)){c->h=h;return FALSE;}return TRUE;},(LPARAM)&ctx);return ctx.h;}

void OpenDebugConsole(HWND parent){
    if(g_debugConsoleOpen)return;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=DefWindowProcW;wc.hInstance=g_hInst;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=L"DebugConsoleWnd2";RegisterClassExW(&wc);
    g_hDebugConsole=CreateWindowExW(WS_EX_TOOLWINDOW,L"DebugConsoleWnd2",L"Bot Debug Console",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,parent?700:CW_USEDEFAULT,parent?50:CW_USEDEFAULT,550,400,parent,NULL,g_hInst,NULL);
    g_hDebugEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,5,5,535,355,g_hDebugConsole,NULL,g_hInst,NULL);
    HFONT hMono=CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
    SendMessageW(g_hDebugEdit,WM_SETFONT,(WPARAM)hMono,TRUE);
    ShowWindow(g_hDebugConsole,SW_SHOW);UpdateWindow(g_hDebugConsole);g_debugConsoleOpen=true;
}

void TraverseTree(DWORD node,std::vector<EntityData>& entities,std::vector<CorpseData>& corpses,float selfX,float selfY){
    if(node<=0x1000)return;
    DWORD left=Read<DWORD>(node+Game::TN_LEFT),right=Read<DWORD>(node+Game::TN_RIGHT);
    if(left>0x1000)TraverseTree(left,entities,corpses,selfX,selfY);
    if(right>0x1000)TraverseTree(right,entities,corpses,selfX,selfY);
    DWORD objPtr=Read<DWORD>(node+Game::TN_OBJ);
    if(objPtr<=0x1000)return;
    DWORD vtable=Read<DWORD>(objPtr+Game::ENT_VTABLE);int hp=Read<int>(objPtr+Game::ENT_HP);
    if(hp<0){CorpseData c{};c.objAddr=objPtr;DWORD np=Read<DWORD>(objPtr+Game::ENT_NAME_PTR);int nl=Read<int>(objPtr+Game::ENT_NAME_LEN);if(nl>0&&nl<64&&np>0x1000){wchar_t w[64]={};for(int i=0;i<nl;i++){wchar_t ch=Read<wchar_t>(np+i*2);if(ch==0)break;w[i]=ch;}wcscpy_s(c.name,w);}else wcscpy_s(c.name,L"Corpse");c.x=Read<int>(objPtr+Game::ENT_RAW_X)/65536.0f;c.y=Read<int>(objPtr+Game::ENT_RAW_Y)/65536.0f;c.distance=CalcDist(selfX,selfY,c.x,c.y);c.objectId=Read<WORD>(objPtr+Game::OBJ_OBJECT_ID);c.typeId=Read<WORD>(objPtr+Game::OBJ_TYPE_ID);corpses.push_back(c);return;}
    EntityData e{};e.objAddr=objPtr;DWORD np=Read<DWORD>(objPtr+Game::ENT_NAME_PTR);int nl=Read<int>(objPtr+Game::ENT_NAME_LEN);if(nl>0&&nl<64&&np>0x1000){wchar_t w[64]={};for(int i=0;i<nl;i++){wchar_t c=Read<wchar_t>(np+i*2);if(c==0)break;w[i]=c;}wcscpy_s(e.name,w);}else wcscpy_s(e.name,L"(unknown)");
    e.x=Read<int>(objPtr+Game::ENT_RAW_X)/65536.0f;e.y=Read<int>(objPtr+Game::ENT_RAW_Y)/65536.0f;e.hp=Read<int>(objPtr+Game::ENT_HP);e.maxHp=Read<int>(objPtr+Game::ENT_MAX_HP);e.mana=Read<int>(objPtr+Game::ENT_MANA);e.maxMana=Read<int>(objPtr+Game::ENT_MAX_MANA);int ti=Read<int>(objPtr+Game::ENT_TYPE_IND);
    if(vtable==Game::VT_PLAYER||ti==1)e.type=1;else if(vtable==Game::VT_BEAST)e.type=4;else if(IsNPC(e.name))e.type=3;else e.type=2;
    e.level=Read<int>(objPtr+Game::ENT_LEVEL);e.classId=Read<BYTE>(objPtr+Game::ENT_CLASS_IND);e.distance=CalcDist(selfX,selfY,e.x,e.y);if(e.hp==0&&e.maxHp==0)return;entities.push_back(e);
}

bool ReadGameState(float& sx,float& sy,int& hp,int& mhp,int& mn,int& mmn,std::wstring& name,int& level,int& classId,std::vector<EntityData>& pl,std::vector<EntityData>& mb,std::vector<EntityData>& np,std::vector<CorpseData>& corpses,DWORD* outPA=nullptr,DWORD* outGM=nullptr){
    pl.clear();mb.clear();np.clear();corpses.clear();
    DWORD gmPtr=Read<DWORD>(Game::GM_PTR);if(gmPtr<=0x1000)return false;
    DWORD gm=Read<DWORD>(gmPtr+Game::GM_OFFSET);if(gm<=0x1000)return false;
    if(outGM)*outGM=gm;
    DWORD lp=Read<DWORD>(gm+Game::LP_OFFSET);if(lp<=0x1000)return false;
    if(outPA)*outPA=lp;
    sx=Read<int>(lp+Game::ENT_RAW_X)/65536.0f;sy=Read<int>(lp+Game::ENT_RAW_Y)/65536.0f;
    hp=Read<int>(lp+Game::ENT_HP);mhp=Read<int>(lp+Game::ENT_MAX_HP);mn=Read<int>(lp+Game::ENT_MANA);mmn=Read<int>(lp+Game::ENT_MAX_MANA);
    level=Read<int>(lp+Game::ENT_LEVEL);classId=Read<BYTE>(lp+Game::ENT_CLASS_IND);
    DWORD np2=Read<DWORD>(lp+Game::ENT_NAME_PTR);int nl=Read<int>(lp+Game::ENT_NAME_LEN);
    if(nl>0&&nl<64&&np2>0x1000){wchar_t w[64]={};for(int i=0;i<nl;i++){wchar_t c=Read<wchar_t>(np2+i*2);if(c==0)break;w[i]=c;}name=w;}else name=L"(unknown)";
    DWORD th=Read<DWORD>(gm+Game::ENTITY_TREE);if(th<=0x1000)return false;
    DWORD root=Read<DWORD>(th+Game::TH_ROOT);
    std::vector<EntityData> all;TraverseTree(root,all,corpses,sx,sy);
    for(auto&e:all){switch(e.type){case 1:pl.push_back(e);break;case 3:np.push_back(e);break;default:mb.push_back(e);break;}}
    auto bd=[](const EntityData&a,const EntityData&b){return a.distance<b.distance;};std::sort(pl.begin(),pl.end(),bd);std::sort(mb.begin(),mb.end(),bd);std::sort(np.begin(),np.end(),bd);
    auto cd=[](const CorpseData&a,const CorpseData&b){return a.distance<b.distance;};std::sort(corpses.begin(),corpses.end(),cd);
    return true;
}

GameContext BuildContext(){
    GameContext ctx{};ctx.hProcess=g_hProcess;ctx.gamePid=g_gamePid;ctx.selfX=g_selfX;ctx.selfY=g_selfY;ctx.playerAddr=g_playerAddr;ctx.gmAddr=g_gmAddr;ctx.gameWindow=FindGameWindow();ctx.tickCount=GetTickCount();
    if(g_playerAddr>0x1000){ctx.selfHp=Read<int>(g_playerAddr+Game::ENT_HP);ctx.selfMaxHp=Read<int>(g_playerAddr+Game::ENT_MAX_HP);ctx.selfMana=Read<int>(g_playerAddr+Game::ENT_MANA);ctx.selfMaxMana=Read<int>(g_playerAddr+Game::ENT_MAX_MANA);ctx.selfLevel=Read<int>(g_playerAddr+Game::ENT_LEVEL);ctx.selfClassId=Read<BYTE>(g_playerAddr+Game::ENT_CLASS_IND);DWORD np=Read<DWORD>(g_playerAddr+Game::ENT_NAME_PTR);int nl=Read<int>(g_playerAddr+Game::ENT_NAME_LEN);if(nl>0&&nl<64&&np>0x1000){wchar_t w[64]={};for(int i=0;i<nl;i++){wchar_t c=Read<wchar_t>(np+i*2);if(c==0)break;w[i]=c;}ctx.selfName=w;}}
    for(auto&e:G->cachedPlayers){GameContext::EntityInfo ei;ei.objAddr=e.objAddr;ei.name=e.name;ei.x=e.x;ei.y=e.y;ei.hp=e.hp;ei.maxHp=e.maxHp;ei.distance=e.distance;ei.type=e.type;ctx.players.push_back(ei);}
    for(auto&e:G->cachedMobs){GameContext::EntityInfo ei;ei.objAddr=e.objAddr;ei.name=e.name;ei.x=e.x;ei.y=e.y;ei.hp=e.hp;ei.maxHp=e.maxHp;ei.distance=e.distance;ei.type=e.type;ctx.mobs.push_back(ei);}
    for(auto&e:G->cachedNpcs){GameContext::EntityInfo ei;ei.objAddr=e.objAddr;ei.name=e.name;ei.x=e.x;ei.y=e.y;ei.hp=e.hp;ei.maxHp=e.maxHp;ei.distance=e.distance;ei.type=e.type;ctx.npcs.push_back(ei);}
    for(auto&c:G->cachedCorpses){GameContext::CorpseInfo ci;ci.objAddr=c.objAddr;ci.name=c.name;ci.x=c.x;ci.y=c.y;ci.distance=c.distance;ctx.corpses.push_back(ci);}
    return ctx;
}

void RefreshProcesses(HWND hList){
    G->procs.clear();G->listToProc.clear();SendMessageW(hList,LB_RESETCONTENT,0,0);
    HANDLE hs=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(hs==INVALID_HANDLE_VALUE)return;
    PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);if(Process32FirstW(hs,&pe)){do{G->procs.push_back({pe.th32ProcessID,pe.szExeFile});}while(Process32NextW(hs,&pe));}CloseHandle(hs);
    std::sort(G->procs.begin(),G->procs.end(),[](const ProcInfo&a,const ProcInfo&b){return a.name<b.name;});
    std::vector<size_t> wsIdx,otherIdx;for(size_t i=0;i<G->procs.size();i++){if(_wcsicmp(G->procs[i].name.c_str(),L"warspear.exe")==0)wsIdx.push_back(i);else otherIdx.push_back(i);}
    for(size_t idx:wsIdx){G->listToProc.push_back((int)idx);auto&p=G->procs[idx];HANDLE hp=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,p.pid);std::wstring cn=L"(loading...)";int lv=0,cl=0;if(hp){float x,y;int hpV,mhp,mn,mmn;std::vector<EntityData>ents,mobs,npcs;std::vector<CorpseData>corpses;DWORD pa=0,ga=0;DWORD sp=g_gamePid;HANDLE sh=g_hProcess;g_gamePid=p.pid;g_hProcess=hp;if(!ReadGameState(x,y,hpV,mhp,mn,mmn,cn,lv,cl,ents,mobs,npcs,corpses,&pa,&ga))cn=L"(not loaded)";g_gamePid=sp;g_hProcess=sh;CloseHandle(hp);}wchar_t buf[256];if(cn!=L"(loading...)"&&cn!=L"(not loaded)")swprintf_s(buf,L"%s [Lv.%d %s]  (PID %d)",cn.c_str(),lv,GetClassName(cl),p.pid);else swprintf_s(buf,L"%s  (PID %d)",cn.c_str(),p.pid);SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)buf);}
    if(!wsIdx.empty()&&!otherIdx.empty()){G->listToProc.push_back(-1);SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)L"--- other processes ---");}
    for(size_t idx:otherIdx){G->listToProc.push_back((int)idx);auto&p=G->procs[idx];wchar_t buf[256];swprintf_s(buf,L"%s  (PID %d)",p.name.c_str(),p.pid);SendMessageW(hList,LB_ADDSTRING,0,(LPARAM)buf);}
}

bool InjectDLL(DWORD pid,const wchar_t*path){HANDLE hp=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pid);if(!hp)return false;size_t len=(wcslen(path)+1)*sizeof(wchar_t);LPVOID buf=VirtualAllocEx(hp,NULL,len,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!buf){CloseHandle(hp);return false;}WriteProcessMemory(hp,buf,path,len,NULL);FARPROC pLoadLib=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HANDLE ht=CreateRemoteThread(hp,NULL,0,(LPTHREAD_START_ROUTINE)pLoadLib,buf,0,NULL);bool ok=false;if(ht){WaitForSingleObject(ht,5000);DWORD ec=0;GetExitCodeThread(ht,&ec);ok=(ec!=0);CloseHandle(ht);}VirtualFreeEx(hp,buf,0,MEM_RELEASE);CloseHandle(hp);return ok;}

void UpdateUI(){
    if(!g_connected||!g_hProcess){SetWindowTextW(g_hStatus,g_hProcess?L"  Connected":L"  Select Warspear and connect");return;}
    float sx,sy;int hp,mhp,mn,mmn;std::wstring name;int level=0,classId=0;
    std::vector<EntityData>pl,mb,np;std::vector<CorpseData>corpses;DWORD pa=0,ga=0;
    if(!ReadGameState(sx,sy,hp,mhp,mn,mmn,name,level,classId,pl,mb,np,corpses,&pa,&ga)){SetWindowTextW(g_hStatus,L"  Cannot read game memory");g_connected=false;return;}
    g_selfX=sx;g_selfY=sy;G->cachedCorpses=corpses;G->cachedPlayers=pl;G->cachedMobs=mb;G->cachedNpcs=np;g_playerAddr=pa;g_gmAddr=ga;
    wchar_t buf[512];swprintf_s(buf,L"  %s | Lv.%d %s | HP: %d/%d | Players: %d Mobs: %d NPCs: %d",name.c_str(),level,GetClassName(classId),hp,mhp,(int)pl.size(),(int)mb.size(),(int)np.size());SetWindowTextW(g_hStatus,buf);
    static std::wstring lastCharName;if(name!=lastCharName){wchar_t wtitle[128];swprintf_s(wtitle,L"WS-Bot - %s [Lv.%d %s]",name.c_str(),level,GetClassName(classId));SetWindowTextW(g_hWnd,wtitle);lastCharName=name;}
    GameContext ctx=BuildContext();if(G->targeter.enabled&&G->targeter.selectedAddr>0x1000)G->attacker.targetAddr=G->targeter.selectedAddr;
    G->modMgr.TickAll(ctx);G->modMgr.RefreshTreeViewLabels(g_hTreeView);if(G->activeModule)G->activeModule->UpdateUI();
}

void CreateProcessPanel(HWND parent){
    g_hProcPanel=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,UI_DETAIL_X,UI_DETAIL_Y,UI_DETAIL_W,UI_DETAIL_H,parent,NULL,g_hInst,NULL);
    int x=10,y=5;CreateWindowExW(0,L"static",L"Processes:",WS_CHILD|WS_VISIBLE,x,y,200,18,g_hProcPanel,NULL,g_hInst,NULL);y+=20;
    g_hProcList=CreateWindowExW(0,L"listbox",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,x,y,370,200,g_hProcPanel,NULL,g_hInst,NULL);SetFont(g_hProcList);y+=208;
    g_hBtnRefresh=CreateWindowExW(0,L"button",L"Refresh",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,70,24,g_hProcPanel,(HMENU)IDM_REFRESH,g_hInst,NULL);
    g_hBtnConnect=CreateWindowExW(0,L"button",L"Connect",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x+75,y,70,24,g_hProcPanel,(HMENU)IDM_CONNECT,g_hInst,NULL);y+=32;
    CreateWindowExW(0,L"static",L"DLL:",WS_CHILD|WS_VISIBLE,x,y+2,30,18,g_hProcPanel,NULL,g_hInst,NULL);
    g_hDllPath=CreateWindowExW(0,L"edit",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,x+32,y,220,22,g_hProcPanel,NULL,g_hInst,NULL);
    g_hBtnBrowse=CreateWindowExW(0,L"button",L"...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x+255,y,30,22,g_hProcPanel,(HMENU)IDM_BROWSE_DLL,g_hInst,NULL);
    g_hBtnInject=CreateWindowExW(0,L"button",L"Inject DLL",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x+290,y,70,22,g_hProcPanel,(HMENU)IDM_INJECT,g_hInst,NULL);
    SetFont(g_hBtnRefresh);SetFont(g_hBtnConnect);SetFont(g_hDllPath);SetFont(g_hBtnBrowse);SetFont(g_hBtnInject);
}

void CreateDetailPanel(HWND parent){g_hDetailPanel=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,UI_DETAIL_X,UI_DETAIL_Y,UI_DETAIL_W,UI_DETAIL_H,parent,NULL,g_hInst,NULL);g_hDetailLabel=CreateWindowExW(0,L"static",L"Select a module from the tree",WS_CHILD|WS_VISIBLE,10,10,370,20,g_hDetailPanel,NULL,g_hInst,NULL);SetFont(g_hDetailLabel);}

void ShowModuleUI(IModule*mod){
    if(g_hProcPanel)ShowWindow(g_hProcPanel,SW_HIDE);
    if(g_hDetailPanel){HWND c=GetWindow(g_hDetailPanel,GW_CHILD);while(c){HWND n=GetWindow(c,GW_HWNDNEXT);DestroyWindow(c);c=n;}}
    G->activeModule=mod;if(!mod||!mod->HasUI()){if(g_hDetailPanel){ShowWindow(g_hDetailPanel,SW_SHOW);const wchar_t*msg=mod?mod->GetName():L"Select a module from the tree";g_hDetailLabel=CreateWindowExW(0,L"static",msg,WS_CHILD|WS_VISIBLE,10,10,370,20,g_hDetailPanel,NULL,g_hInst,NULL);SetFont(g_hDetailLabel);}return;}
    ShowWindow(g_hDetailPanel,SW_SHOW);mod->CreateUI(g_hDetailPanel,10,30,370);
}

void ShowProcessPanel(){if(g_hDetailPanel)ShowWindow(g_hDetailPanel,SW_HIDE);if(g_hProcPanel){ShowWindow(g_hProcPanel,SW_SHOW);RefreshProcesses(g_hProcList);}G->activeModule=nullptr;}

void CreateMenuBar(HWND hWnd){
    HMENU hb=CreateMenu(),hc=CreatePopupMenu(),hq=CreatePopupMenu(),hn=CreatePopupMenu();
    AppendMenuW(hb,MF_POPUP,(UINT_PTR)hc,L"Config");AppendMenuW(hc,MF_STRING,IDM_REFRESH,L"Refresh Processes");AppendMenuW(hc,MF_SEPARATOR,0,NULL);AppendMenuW(hc,MF_STRING,IDM_EXIT,L"Exit");
    AppendMenuW(hb,MF_POPUP,(UINT_PTR)hq,L"Quick Actions");AppendMenuW(hq,MF_STRING,IDM_TOGGLE_ATTACK,L"Toggle Attack [F1]");AppendMenuW(hq,MF_STRING,IDM_TOGGLE_HEAL,L"Toggle Heal [F2]");AppendMenuW(hq,MF_STRING,IDM_TOGGLE_FOLLOW,L"Toggle Follow [F3]");AppendMenuW(hq,MF_STRING,IDM_TOGGLE_LOOT,L"Toggle Loot [F4]");AppendMenuW(hq,MF_SEPARATOR,0,NULL);AppendMenuW(hq,MF_STRING,IDM_TOGGLE_ALL,L"Toggle All ON");AppendMenuW(hq,MF_STRING,IDM_STOP_ALL,L"STOP ALL");AppendMenuW(hq,MF_SEPARATOR,0,NULL);AppendMenuW(hq,MF_STRING,IDM_SCALE_UP,L"Scale Up [F5]");AppendMenuW(hq,MF_STRING,IDM_SCALE_DOWN,L"Scale Down [F6]");
    AppendMenuW(hb,MF_POPUP,(UINT_PTR)hn,L"Connection");AppendMenuW(hn,MF_STRING,IDM_CONNECT,L"Connect to Process");AppendMenuW(hn,MF_STRING,IDM_INJECT,L"Inject DLL");AppendMenuW(hn,MF_SEPARATOR,0,NULL);AppendMenuW(hn,MF_STRING,IDM_DEBUG,L"Debug Console");
    SetMenu(hWnd,hb);g_hMenu=hb;
}

void CreateModuleTree(HWND parent){
    g_hTreeView=CreateWindowExW(WS_EX_CLIENTEDGE,WC_TREEVIEWW,L"",WS_CHILD|WS_VISIBLE|TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,5,UI_TREE_Y,UI_TREE_W,UI_DETAIL_H,parent,(HMENU)2000,g_hInst,NULL);SetFont(g_hTreeView);
    G->modMgr.Add(&G->targeter);G->modMgr.Add(&G->attacker);G->modMgr.Add(&G->healer);G->modMgr.Add(&G->partyHealer);G->modMgr.Add(&G->looter);G->modMgr.Add(&G->follower);G->modMgr.Add(&G->extra);
    wchar_t cfgDir[MAX_PATH];GetModuleFileNameW(NULL,cfgDir,MAX_PATH);wchar_t*bs=wcsrchr(cfgDir,L'\\');if(bs)*bs=0;wcscat_s(cfgDir,L"\\config");G->modMgr.LoadAll(cfgDir);G->modMgr.BuildTreeView(g_hTreeView);
    TVINSERTSTRUCTW tis{};tis.hParent=TVI_ROOT;tis.hInsertAfter=TVI_FIRST;tis.item.mask=TVIF_TEXT|TVIF_PARAM;tis.item.pszText=L"Connection";tis.item.lParam=-1;TreeView_InsertItem(g_hTreeView,&tis);
}

static const int UI_MENU_H=24, UI_TREE_W=180, UI_DETAIL_X=UI_TREE_W+15, UI_DETAIL_W=390, UI_DETAIL_H=360, UI_TREE_Y=UI_MENU_H+5, UI_DETAIL_Y=UI_MENU_H+5;

LRESULT CALLBACK WndProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
    case WM_CREATE:{InitFont();CreateMenuBar(hWnd);g_hStatus=CreateWindowExW(0,STATUSCLASSNAMEW,L"",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,hWnd,NULL,g_hInst,NULL);SendMessageW(g_hStatus,WM_SETFONT,(WPARAM)g_hFont,TRUE);CreateModuleTree(hWnd);CreateDetailPanel(hWnd);CreateProcessPanel(hWnd);ShowProcessPanel();break;}
    case WM_NOTIFY:{NMHDR*nm=(NMHDR*)lParam;if(nm->hwndFrom==g_hTreeView&&nm->code==TVN_SELCHANGEDW){HTREEITEM sel=TreeView_GetSelection(g_hTreeView);if(!sel)break;TVITEMW tvi{};tvi.mask=TVIF_PARAM;tvi.hItem=sel;TreeView_GetItem(g_hTreeView,&tvi);if(tvi.lParam==-1)ShowProcessPanel();else ShowModuleUI((IModule*)tvi.lParam);}break;}
    case WM_COMMAND:{int id=LOWORD(wParam),code=HIWORD(wParam);if(G->activeModule&&id>=9000){G->activeModule->OnCommand(id,code);break;}
        switch(id){
        case IDM_REFRESH:if(g_hProcList)RefreshProcesses(g_hProcList);break;
        case IDM_CONNECT:{if(!g_hProcList)break;int sel=(int)SendMessageW(g_hProcList,LB_GETCURSEL,0,0);if(sel==LB_ERR||sel>=G->listToProc.size()){MessageBoxW(hWnd,L"Select a process first!",L"",MB_OK|MB_ICONWARNING);break;}int pi=G->listToProc[sel];if(pi<0||pi>=(int)G->procs.size())break;DWORD pid=G->procs[pi].pid;if(g_hProcess)CloseHandle(g_hProcess);g_hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION,FALSE,pid);g_gamePid=pid;if(!g_hProcess){MessageBoxW(hWnd,L"Cannot open process.\nTry running as Administrator.",L"Error",MB_OK|MB_ICONERROR);break;}float x,y;int hp,mhp,mn,mmn;std::wstring name;int level=0,cl=0;std::vector<EntityData>p,m,n;std::vector<CorpseData>corpses;if(ReadGameState(x,y,hp,mhp,mn,mmn,name,level,cl,p,m,n,corpses)){g_connected=true;OpenDebugConsole(hWnd);wchar_t wt[128];swprintf_s(wt,L"WS-Bot - %s [Lv.%d %s]",name.c_str(),level,GetClassName(cl));SetWindowTextW(hWnd,wt);DebugLog("[CONNECT] SUCCESS");G->modMgr.StartAll();if(!m.empty())G->attacker.targetAddr=m[0].objAddr;if(!p.empty()){G->follower.targetAddr=p[0].objAddr;G->follower.targetName=p[0].name;}}else MessageBoxW(hWnd,L"Cannot read game memory.",L"Warning",MB_OK|MB_ICONWARNING);break;}
        case IDM_BROWSE_DLL:{OPENFILENAMEW ofn{};wchar_t file[MAX_PATH]={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hWnd;ofn.lpstrFilter=L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";ofn.lpstrFile=file;ofn.nMaxFile=MAX_PATH;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;if(GetOpenFileNameW(&ofn))SetWindowTextW(g_hDllPath,file);break;}
        case IDM_INJECT:{if(!g_hProcess||!g_connected){MessageBoxW(hWnd,L"Connect first!",L"",MB_OK|MB_ICONWARNING);break;}if(!G->hSharedMem){G->hSharedMem=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(BotCmd),L"Local\\WarspearBotShared");if(G->hSharedMem)G->pBotCmd=(BotCmd*)MapViewOfFile(G->hSharedMem,FILE_MAP_ALL_ACCESS,0,0,sizeof(BotCmd));}if(!G->pBotCmd){MessageBoxW(hWnd,L"Cannot create shared memory.",L"Error",MB_OK|MB_ICONERROR);break;}wchar_t dll[MAX_PATH];GetWindowTextW(g_hDllPath,dll,MAX_PATH);if(dll[0]==0){MessageBoxW(hWnd,L"Enter DLL path.",L"",MB_OK|MB_ICONWARNING);break;}if(!wcschr(dll,L':')){wchar_t dir[MAX_PATH];GetModuleFileNameW(NULL,dir,MAX_PATH);wchar_t*bs=wcsrchr(dir,L'\\');if(bs)*bs=0;wchar_t full[MAX_PATH];swprintf_s(full,L"%s\\%s",dir,dll);wcscpy_s(dll,full);}if(InjectDLL(g_gamePid,dll)){g_dllInjected=true;DebugLog("[INJECT] SUCCESS");}else MessageBoxW(hWnd,L"DLL injection failed.",L"Error",MB_OK|MB_ICONERROR);break;}
        case IDM_TOGGLE_ATTACK:{G->targeter.enabled=!G->targeter.enabled;G->attacker.enabled=G->targeter.enabled;wchar_t s[64];swprintf_s(s,L"  Attack: %s",G->attacker.enabled?L"ON":L"OFF");SetWindowTextW(g_hStatus,s);break;}
        case IDM_TOGGLE_HEAL:G->healer.enabled=!G->healer.enabled;{wchar_t s[64];swprintf_s(s,L"  Heal: %s",G->healer.enabled?L"ON":L"OFF");SetWindowTextW(g_hStatus,s);}break;
        case IDM_TOGGLE_FOLLOW:G->follower.enabled=!G->follower.enabled;{wchar_t s[64];swprintf_s(s,L"  Follow: %s",G->follower.enabled?L"ON":L"OFF");SetWindowTextW(g_hStatus,s);}break;
        case IDM_TOGGLE_LOOT:G->looter.enabled=!G->looter.enabled;{wchar_t s[64];swprintf_s(s,L"  Loot: %s",G->looter.enabled?L"ON":L"OFF");SetWindowTextW(g_hStatus,s);}break;
        case IDM_TOGGLE_ALL:{bool on=!(G->targeter.enabled&&G->attacker.enabled&&G->healer.enabled&&G->looter.enabled);G->targeter.enabled=on;G->attacker.enabled=on;G->healer.enabled=on;G->looter.enabled=on;G->follower.enabled=on;G->modMgr.RefreshTreeViewLabels(g_hTreeView);SetWindowTextW(g_hStatus,on?L"  ALL ON":L"  ALL OFF");break;}
        case IDM_STOP_ALL:{G->targeter.enabled=false;G->attacker.enabled=false;G->healer.enabled=false;G->partyHealer.enabled=false;G->looter.enabled=false;G->follower.enabled=false;G->extra.enabled=false;G->attacker.targetAddr=0;G->follower.targetAddr=0;G->modMgr.RefreshTreeViewLabels(g_hTreeView);SetWindowTextW(g_hStatus,L"  ALL STOPPED");DebugLog("[STOP] All modules stopped");break;}
        case IDM_SCALE_UP:g_scale+=0.5f;{wchar_t s[64];swprintf_s(s,L"  Scale: %.2f",g_scale);SetWindowTextW(g_hStatus,s);}break;
        case IDM_SCALE_DOWN:g_scale-=0.5f;if(g_scale<0.5f)g_scale=0.5f;{wchar_t s[64];swprintf_s(s,L"  Scale: %.2f",g_scale);SetWindowTextW(g_hStatus,s);}break;
        case IDM_DEBUG:OpenDebugConsole(hWnd);break;
        case IDM_EXIT:DestroyWindow(hWnd);break;}break;}
    case WM_KEYDOWN:switch(wParam){case VK_F1:SendMessage(hWnd,WM_COMMAND,IDM_TOGGLE_ATTACK,0);break;case VK_F2:SendMessage(hWnd,WM_COMMAND,IDM_STOP_ALL,0);break;case VK_F3:SendMessage(hWnd,WM_COMMAND,IDM_TOGGLE_FOLLOW,0);break;case VK_F4:SendMessage(hWnd,WM_COMMAND,IDM_TOGGLE_LOOT,0);break;case VK_F5:SendMessage(hWnd,WM_COMMAND,IDM_SCALE_UP,0);break;case VK_F6:SendMessage(hWnd,WM_COMMAND,IDM_SCALE_DOWN,0);break;}break;
    case WM_TIMER:if(wParam==1)UpdateUI();break;
    case WM_SIZE:if(g_hStatus)SendMessage(g_hStatus,WM_SIZE,0,0);break;
    case WM_DESTROY:{DebugLog("[EXIT] Shutting down...");wchar_t cfgDir[MAX_PATH];GetModuleFileNameW(NULL,cfgDir,MAX_PATH);wchar_t*bs=wcsrchr(cfgDir,L'\\');if(bs)*bs=0;wcscat_s(cfgDir,L"\\config");G->modMgr.SaveAll(cfgDir);G->modMgr.StopAll();if(g_hFont)DeleteObject(g_hFont);if(g_hProcess)CloseHandle(g_hProcess);if(g_hDebugConsole)DestroyWindow(g_hDebugConsole);PostQuitMessage(0);break;}
    default:return DefWindowProcW(hWnd,msg,wParam,lParam);}return 0;
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int nShow){
    G=new BotState();
    g_hInst=hInst;
    INITCOMMONCONTROLSEX icex{sizeof(icex),ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&icex);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WndProc;wc.hInstance=hInst;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=L"WarspearBotCtrl";RegisterClassExW(&wc);
    g_hWnd=CreateWindowExW(0,L"WarspearBotCtrl",L"Warspear Bot v4",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,610,480,NULL,NULL,hInst,NULL);if(!g_hWnd)return 0;
    ShowWindow(g_hWnd,nShow);UpdateWindow(g_hWnd);SetTimer(g_hWnd,1,500,NULL);
    MSG msg{};while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}return(int)msg.wParam;
}
