#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <CommCtrl.h>

struct GameContext {
    HANDLE hProcess;
    DWORD  gamePid;
    float  selfX, selfY;
    int    selfHp, selfMaxHp, selfMana, selfMaxMana;
    int    selfLevel, selfClassId;
    std::wstring selfName;
    struct EntityInfo { DWORD objAddr; std::wstring name; float x, y; int hp, maxHp; float distance; int type; };
    std::vector<EntityInfo> players, mobs, npcs;
    struct CorpseInfo { DWORD objAddr; std::wstring name; float x, y; float distance; };
    std::vector<CorpseInfo> corpses;
    DWORD playerAddr, gmAddr;
    HWND  gameWindow;
    DWORD tickCount;
    // Heal priority: true while self/target HP is below the configured % —
    // attacker and looter must yield so healing always wins (main.cpp sets it)
    bool  holdCombat = false;
    // Dungeon module: busy = walking/interacting with portal/chest/exit
    // (targeter, attacker, follower, looter must yield); noLoot = during
    // dungeon waves where mobs don't drop loot (looter must yield)
    bool  dungeonBusy = false;
    bool  dungeonNoLoot = false;
    float scale = 3.5f;
    void  (*remoteSendEnter)() = nullptr;
    void  (*remoteHandleMoveOrAction)(DWORD localPlayerAddr) = nullptr;

    // Pointer to shared pending corpse (owned by BotState, visible to all modules)
    struct PendingCorpse {
        DWORD objAddr = 0;
        std::wstring name;
        float x = 0, y = 0;
        DWORD time = 0;
        bool valid = false;
    };
    PendingCorpse* pendingCorpse = nullptr;
};

class IModule {
public:
    virtual ~IModule() = default;

    virtual const wchar_t* GetName() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual void Start() {}
    virtual void Stop() {}
    virtual void Tick(const GameContext& ctx) = 0;

    virtual void LoadConfig(const wchar_t* path) {}
    virtual void SaveConfig(const wchar_t* path) const {}

    virtual bool HasUI() const { return false; }
    virtual void CreateUI(HWND parent, int x, int y, int w) {}
    virtual void OnCommand(int id, int code) {}
    virtual void UpdateUI() {}

    HTREEITEM hTreeItem = nullptr;
    HWND hTreeView = nullptr;
};
