#pragma once
#include <windows.h>

// Teclas do jogo via PostMessage (WM_KEYDOWN/WM_KEYUP) em vez de
// keybd_event/SendInput. Comprovado ao vivo em 06/10/2026:
//   - cursor WPM no tile do mob -> jogo recalcula +0x7C
//   - flag 13 (bota) + Enter postado -> personagem andou
//   - flag 8 (espada) + tecla '1' + Enter postado -> dano (Fada 203->3->0)
// Nao exige janela em primeiro plano nem keyboard real: serve multi-janela.
inline void PostGameKey(HWND gw, UINT vk, DWORD holdMs = 50) {
    if (!gw || !IsWindow(gw)) return;
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM keyDown = 1 | ((LPARAM)scan << 16);
    LPARAM keyUp = keyDown | ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    PostMessageW(gw, WM_KEYDOWN, (WPARAM)vk, keyDown);
    if (holdMs) Sleep(holdMs);
    PostMessageW(gw, WM_KEYUP, (WPARAM)vk, keyUp);
}

inline void PostGameEnter(HWND gw, DWORD holdMs = 80) {
    PostGameKey(gw, VK_RETURN, holdMs);
}
