#include <Windows.h>

static BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        OutputDebugStringA("[BOT] DLL loaded OK\n");
    }
    return TRUE;
}
