#pragma once
#include <Windows.h>

// ============================================================
// Shared Memory Protocol - Bot Controller <-> Injected DLL
// ============================================================

constexpr const wchar_t* SHARED_MEMORY_NAME = L"WarspearBotSharedMemory";

// Max entities to track in shared memory
constexpr int MAX_PLAYERS = 20;
constexpr int MAX_MOBS    = 50;
constexpr int MAX_NPCS    = 30;

#pragma pack(push, 1)

struct EntityInfo {
    wchar_t name[32];
    float x;
    float y;
    int   hp;
    int   maxHp;
    int   mana;
    int   maxMana;
    float distance;  // distance to local player
    int   type;      // 0=unknown, 1=player, 2=mob, 3=npc, 4=beast
};

struct BotSharedData {
    // === EXE -> DLL (commands) ===
    bool cmdAutoAttack;
    bool cmdAutoHeal;
    bool cmdAutoCollect;
    bool cmdFollowPlayer;
    bool cmdShowOverlay;
    float cmdHealThreshold;     // 0.0 - 1.0
    float cmdAttackRange;
    float cmdFollowRange;
    wchar_t cmdFollowTarget[32];

    // === DLL -> EXE (game state) ===
    bool dllActive;             // DLL is loaded and running

    // Local player info
    wchar_t localPlayerName[32];
    float localPlayerX;
    float localPlayerY;
    int   localPlayerHP;
    int   localPlayerMaxHP;
    int   localPlayerMana;
    int   localPlayerMaxMana;

    // Entity lists
    int playerCount;
    EntityInfo players[MAX_PLAYERS];

    int mobCount;
    EntityInfo mobs[MAX_MOBS];

    int npcCount;
    EntityInfo npcs[MAX_NPCS];

    // Bot status
    bool botAttacking;
    bool botHealing;
    bool botFollowing;
    wchar_t botStatus[128];

    // Timestamp (for detecting stale data)
    DWORD lastUpdateTick;
};

#pragma pack(pop)

constexpr DWORD SHARED_MEMORY_SIZE = sizeof(BotSharedData);

// Helper to create/open shared memory
inline HANDLE CreateBotSharedMemory() {
    return CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        SHARED_MEMORY_SIZE,
        SHARED_MEMORY_NAME
    );
}

inline BotSharedData* MapBotSharedData(HANDLE hMap) {
    return (BotSharedData*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEMORY_SIZE);
}

inline HANDLE OpenBotSharedMemory() {
    return OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEMORY_NAME);
}
