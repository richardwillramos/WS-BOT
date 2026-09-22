#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cmath>

// ============================================================
// Warspear Online - Memory Offsets & Structures
// Private server client (32-bit, base 0x00400000)
// ============================================================

namespace Game {

// ---- Key addresses ----
constexpr DWORD DAT_GetGlobalSystemInstance = 0x00D387AC;  // read pointer from here → system instance
constexpr DWORD OFFSET_GameManager         = 0x14;        // system + 0x14 = gameManager
constexpr DWORD OFFSET_EntityTreeHeader    = 0x3C;        // gameManager + 0x3C = entity tree header
constexpr DWORD OFFSET_LocalPlayer         = 0x40;        // gameManager + 0x40 = localPlayer ptr

// ---- VTable IDs ----
constexpr DWORD VTABLE_LOCAL_PLAYER = 0x00C80F9C;
constexpr DWORD VTABLE_HUMANOID     = 0x00C8137C;  // players, NPCs, humanoid mobs
constexpr DWORD VTABLE_BEAST        = 0x00C81490;  // animal mobs (Javali, Boneca, etc.)

// ---- Function addresses ----
constexpr DWORD FN_HandleMoveOrAction = 0x00A3F480;  // __thiscall(localPlayer, param1)
constexpr DWORD FN_HandleSkillOrUse   = 0x00A3E0F0;  // __thiscall(localPlayer, entityPtr)

// ---- Entity object offsets ----
constexpr int ENT_VTABLE       = 0x000;
constexpr int ENT_RAW_X        = 0x010;  // raw X coord, divide by 65536.0
constexpr int ENT_RAW_Y        = 0x014;  // raw Y coord, divide by 65536.0
constexpr int ENT_SELF_PTR     = 0x038;  // self-referencing pointer
constexpr int ENT_SUB_VTABLE   = 0x054;  // secondary vtable
constexpr int ENT_NAME_PTR     = 0x058;  // pointer to name string
constexpr int ENT_NAME_LEN     = 0x060;  // name length (in chars)
constexpr int ENT_NAME         = 0x064;  // wide string name (UTF-16LE)
constexpr int ENT_HP           = 0x10C;  // current HP
constexpr int ENT_MAX_HP       = 0x110;  // max HP
constexpr int ENT_MANA         = 0x114;  // current mana
constexpr int ENT_MAX_MANA     = 0x118;  // max mana

// ---- Entity tree node offsets ----
constexpr int NODE_PARENT  = 0x00;
constexpr int NODE_LEFT    = 0x04;
constexpr int NODE_RIGHT   = 0x08;
constexpr int NODE_COLOR   = 0x0C;
constexpr int NODE_ID      = 0x10;
constexpr int NODE_OBJECT  = 0x14;

// ---- Entity type classification ----
enum class EntityType {
    Unknown,
    Player,      // local player (VTABLE_LOCAL_PLAYER)
    RemotePlayer,// another player (VTABLE_HUMANOID + is_player_name)
    Mob,         // hostile entity
    NPC,         // friendly NPC
    BeastMob,    // VTABLE_BEAST
};

// ---- Entity struct ----
struct Entity {
    DWORD  objPtr;
    DWORD  nodeId;
    DWORD  vtable;
    float  x;
    float  y;
    int    hp;
    int    maxHp;
    int    mana;
    int    maxMana;
    std::wstring name;
    EntityType type;
};

// ---- Helpers ----

inline DWORD ReadDword(DWORD address) {
    return *(DWORD*)address;
}

inline DWORD GetSystemInstance() {
    DWORD ptr = ReadDword(DAT_GetGlobalSystemInstance);
    return ptr;
}

inline DWORD GetGameManager() {
    DWORD sys = GetSystemInstance();
    if (sys == 0) return 0;
    return ReadDword(sys + OFFSET_GameManager);
}

inline DWORD GetLocalPlayer() {
    DWORD gm = GetGameManager();
    if (gm == 0) return 0;
    return ReadDword(gm + OFFSET_LocalPlayer);
}

inline DWORD GetEntityTreeRoot() {
    DWORD gm = GetGameManager();
    if (gm == 0) return 0;
    DWORD header = ReadDword(gm + OFFSET_EntityTreeHeader);
    if (header == 0) return 0;
    return ReadDword(header);
}

inline int GetEntityCount() {
    DWORD gm = GetGameManager();
    if (gm == 0) return 0;
    DWORD header = ReadDword(gm + OFFSET_EntityTreeHeader);
    if (header == 0) return 0;
    return ReadDword(header + 0x04);
}

inline float GetWorldX(DWORD objPtr) {
    return (float)(int)ReadDword(objPtr + ENT_RAW_X) / 65536.0f;
}

inline float GetWorldY(DWORD objPtr) {
    return (float)(int)ReadDword(objPtr + ENT_RAW_Y) / 65536.0f;
}

inline std::wstring GetEntityName(DWORD objPtr) {
    DWORD nameAddr = objPtr + ENT_NAME;
    std::wstring result;
    for (int i = 0; i < 32; i++) {
        wchar_t ch = (wchar_t)*(WORD*)(nameAddr + i * 2);
        if (ch == 0) break;
        result += ch;
    }
    return result;
}

inline int GetEntityHP(DWORD objPtr) {
    return (int)ReadDword(objPtr + ENT_HP);
}

inline int GetEntityMaxHP(DWORD objPtr) {
    return (int)ReadDword(objPtr + ENT_MAX_HP);
}

inline int GetEntityMana(DWORD objPtr) {
    return (int)ReadDword(objPtr + ENT_MANA);
}

inline int GetEntityMaxMana(DWORD objPtr) {
    return (int)ReadDword(objPtr + ENT_MAX_MANA);
}

// ---- Cursor action flags (at cursor_ptr + CUR_FLAG) ----
constexpr int CURSOR_ACTION_ATTACK = 8;   // sword icon, cursor over attackable entity
constexpr int CURSOR_ACTION_MOVE   = 13;  // boot icon, cursor over walkable tile
constexpr int CURSOR_ACTION_NONE   = 15;  // no action available

// ---- Cursor reading helpers ----
inline DWORD GetCursorPtr(DWORD processHandle) {
    DWORD gm = GetGameManager();
    if (gm == 0) return 0;
    DWORD cur = 0; SIZE_T r = 0;
    ReadProcessMemory((HANDLE)processHandle, (LPCVOID)(gm + 0x123C), &cur, 4, &r);
    return (r == 4) ? cur : 0;
}

inline int ReadCursorAction(DWORD processHandle, DWORD cursorPtr) {
    if (cursorPtr == 0) return -1;
    int action = 0; SIZE_T r = 0;
    ReadProcessMemory((HANDLE)processHandle, (LPCVOID)(cursorPtr + CUR_FLAG), &action, 4, &r);
    return (r == 4) ? action : -1;
}

inline void ReadCursorPos(DWORD processHandle, DWORD cursorPtr, short& cx, short& cy) {
    cx = 0; cy = 0;
    if (cursorPtr == 0) return;
    SIZE_T r = 0;
    ReadProcessMemory((HANDLE)processHandle, (LPCVOID)(cursorPtr + CUR_X), &cx, 2, &r);
    ReadProcessMemory((HANDLE)processHandle, (LPCVOID)(cursorPtr + CUR_Y), &cy, 2, &r);
}

inline void WriteCursorPos(DWORD processHandle, DWORD cursorPtr, short tileX, short tileY) {
    if (cursorPtr == 0) return;
    int rawX = (int)tileX * 0x180000;
    int rawY = (int)tileY * 0x180000;
    SIZE_T r = 0;
    ReadProcessMemory((HANDLE)processHandle, (LPCVOID)(cursorPtr + CUR_X), NULL, 0, &r); // validate
    WriteProcessMemory((HANDLE)processHandle, (LPVOID)(cursorPtr + CUR_X), &tileX, 2, NULL);
    WriteProcessMemory((HANDLE)processHandle, (LPVOID)(cursorPtr + CUR_Y), &tileY, 2, NULL);
    WriteProcessMemory((HANDLE)processHandle, (LPVOID)(cursorPtr + CUR_RAW_X), &rawX, 4, NULL);
    WriteProcessMemory((HANDLE)processHandle, (LPVOID)(cursorPtr + CUR_RAW_Y), &rawY, 4, NULL);
}

inline EntityType ClassifyEntity(DWORD vtable) {
    if (vtable == VTABLE_LOCAL_PLAYER) return EntityType::Player;
    if (vtable == VTABLE_BEAST)        return EntityType::BeastMob;
    if (vtable == VTABLE_HUMANOID)     return EntityType::Unknown; // need more context
    return EntityType::Unknown;
}

// Known player names (add your friends/alt accounts here)
inline bool IsKnownPlayerName(const std::wstring& name) {
    static const std::wstring players[] = {
        L"Ligerinhu", L"Magutopp", L"Victorw"
    };
    for (auto& p : players) {
        if (name == p) return true;
    }
    return false;
}

// Known NPC names (non-hostile)
inline bool IsKnownNPC(const std::wstring& name) {
    static const std::wstring npcs[] = {
        L"Rokus", L"Valentim", L"Mestre Hedwig", L"Leiloeira Ilse",
        L"Vilma", L"Almoxarife", L"Vicente"
    };
    for (auto& n : npcs) {
        if (name == n) return true;
    }
    return false;
}

// Known mob names (hostile)
inline bool IsKnownMob(const std::wstring& name) {
    static const std::wstring mobs[] = {
        L"Miliciano", L"Guarda", L"Balisteiro", L"Citadino",
        L"Citadina", L"Javali", L"Boneca", L"Manequim",
        L"Gregrio", L"Moraes", L"Kpqbqtqk"
    };
    for (auto& m : mobs) {
        if (name == m) return true;
    }
    return false;
}

// Traverses entity tree in-order and returns all entities
inline std::vector<Entity> GetAllEntities() {
    std::vector<Entity> result;

    DWORD root = GetEntityTreeRoot();
    if (root == 0) return result;

    // Recursive in-order traversal using stack
    struct StackFrame { DWORD node; };
    std::vector<StackFrame> stack;
    DWORD current = root;

    // Track visited nodes to avoid infinite loops
    const int MAX_ITERATIONS = 200;
    int iterations = 0;

    while ((current != 0 || !stack.empty()) && iterations < MAX_ITERATIONS) {
        iterations++;
        if (current != 0) {
            stack.push_back({ current });
            DWORD left = ReadDword(current + NODE_LEFT);
            current = left;
        } else {
            StackFrame frame = stack.back();
            stack.pop_back();
            DWORD nodePtr = frame.node;

            DWORD objPtr = ReadDword(nodePtr + NODE_OBJECT);
            if (objPtr != 0) {
                Entity ent{};
                ent.objPtr  = objPtr;
                ent.nodeId  = ReadDword(nodePtr + NODE_ID);
                ent.vtable  = ReadDword(objPtr + ENT_VTABLE);
                ent.x       = GetWorldX(objPtr);
                ent.y       = GetWorldY(objPtr);
                ent.hp      = GetEntityHP(objPtr);
                ent.maxHp   = GetEntityMaxHP(objPtr);
                ent.mana    = GetEntityMana(objPtr);
                ent.maxMana = GetEntityMaxMana(objPtr);
                ent.name    = GetEntityName(objPtr);

                // Classify
                if (ent.vtable == VTABLE_LOCAL_PLAYER) {
                    ent.type = EntityType::Player;
                } else if (ent.vtable == VTABLE_BEAST) {
                    ent.type = EntityType::BeastMob;
                } else if (IsKnownPlayerName(ent.name)) {
                    ent.type = EntityType::RemotePlayer;
                } else if (IsKnownNPC(ent.name)) {
                    ent.type = EntityType::NPC;
                } else if (IsKnownMob(ent.name)) {
                    ent.type = EntityType::Mob;
                } else {
                    // Default: humanoid with no known name = probably NPC
                    ent.type = EntityType::NPC;
                }

                result.push_back(ent);
            }

            DWORD right = ReadDword(nodePtr + NODE_RIGHT);
            current = right;
        }
    }

    return result;
}

// Get local player as Entity
inline Entity GetLocalPlayerEntity() {
    Entity ent{};
    DWORD lp = GetLocalPlayer();
    if (lp == 0) return ent;

    ent.objPtr  = lp;
    ent.vtable  = ReadDword(lp + ENT_VTABLE);
    ent.x       = GetWorldX(lp);
    ent.y       = GetWorldY(lp);
    ent.hp      = GetEntityHP(lp);
    ent.maxHp   = GetEntityMaxHP(lp);
    ent.mana    = GetEntityMana(lp);
    ent.maxMana = GetEntityMaxMana(lp);
    ent.name    = GetEntityName(lp);
    ent.type    = EntityType::Player;

    return ent;
}

// Distance between two entities
inline float Distance(const Entity& a, const Entity& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace Game
