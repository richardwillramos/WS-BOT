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

// Clique direito sintetico em (cx,cy) client — self buff: seleciona a skill
// e clica com o BOTAO DIREITO (diferente do ataque = skill + clique esquerdo
// no alvo). PostMessage puro: nao move o mouse real nem rouba foreground.
inline void PostGameRightClick(HWND gw, int cx, int cy, DWORD holdMs = 30) {
    if (!gw || !IsWindow(gw)) return;
    LPARAM lp = MAKELPARAM(cx, cy);
    PostMessageW(gw, WM_MOUSEMOVE, 0, lp);
    PostMessageW(gw, WM_RBUTTONDOWN, MK_RBUTTON, lp);
    if (holdMs) Sleep(holdMs);
    PostMessageW(gw, WM_RBUTTONUP, 0, lp);
}
