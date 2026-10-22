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

static HINSTANCE g_hInst = NULL;
static HWND g_hWnd = NULL;
static HMENU g_hMenu = NULL;
static HWND g_hStatus = NULL;
static HWND g_hTreeView = NULL;
static HWND g_hDetailPanel = NULL;
static HFONT g_hFont = NULL;

static ModuleManager g_modMgr;
static TargeterModule  g_targeter;
static AttackerModule  g_attacker;
static HealerModule    g_healer;
static PartyHealerModule g_partyHealer;
static LooterModule    g_looter;
static FollowerModule  g_follower;
static ExtraModule     g_extra;

void InitFont() { g_hFont=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Segoe UI"); }
void SetFont(HWND h){SendMessageW(h,WM_SETFONT,(WPARAM)g_hFont,TRUE);}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitFont();

        g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD|WS_VISIBLE|TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,
            5, 30, 180, 360, hWnd, (HMENU)2000, g_hInst, NULL);
        SetFont(g_hTreeView);

        g_hDetailPanel = CreateWindowExW(0, L"STATIC", L"Select a module",
            WS_CHILD, 200, 30, 390, 360, hWnd, NULL, g_hInst, NULL);

        g_modMgr.Add(&g_targeter);
        g_modMgr.Add(&g_attacker);
        g_modMgr.Add(&g_healer);
        g_modMgr.Add(&g_partyHealer);
        g_modMgr.Add(&g_looter);
        g_modMgr.Add(&g_follower);
        g_modMgr.Add(&g_extra);
        g_modMgr.BuildTreeView(g_hTreeView);

        break;
    }
    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g_hInst = hInst;
    INITCOMMONCONTROLSEX icex; icex.dwSize=sizeof(icex); icex.dwICC=ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc; ZeroMemory(&wc,sizeof(wc));
    wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName=L"WarspearBotCtrl"; RegisterClassExW(&wc);

    g_hWnd=CreateWindowExW(0,L"WarspearBotCtrl",L"Warspear Bot v4",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,610,480,NULL,NULL,hInst,NULL);
    if(!g_hWnd) return 0;
    ShowWindow(g_hWnd,nShow); UpdateWindow(g_hWnd);
    SetTimer(g_hWnd,1,500,NULL);

    MSG msg; ZeroMemory(&msg,sizeof(msg));
    while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return (int)msg.wParam;
}
