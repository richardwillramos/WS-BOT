#pragma once
#include "IModule.h"
#include <vector>
#include <string>
#include <algorithm>

class ModuleManager {
public:
    void Add(IModule* mod) { modules.push_back(mod); }

    IModule* Get(const wchar_t* name) {
        for (auto* m : modules)
            if (wcscmp(m->GetName(), name) == 0) return m;
        return nullptr;
    }

    const std::vector<IModule*>& GetAll() const { return modules; }

    void StartAll() { for (auto* m : modules) if (m->IsEnabled()) m->Start(); }
    void StopAll()  { for (auto* m : modules) m->Stop(); }

    void TickAll(const GameContext& ctx) {
        for (auto* m : modules)
            if (m->IsEnabled()) m->Tick(ctx);
    }

    void LoadAll(const wchar_t* dir) {
        for (auto* m : modules) {
            wchar_t path[MAX_PATH];
            wchar_t safeName[64];
            wcscpy_s(safeName, m->GetName());
            for (wchar_t* p = safeName; *p; p++) if (*p == L' ') *p = L'_';
            swprintf_s(path, L"%s\\%s.ini", dir, safeName);
            m->LoadConfig(path);
        }
    }

    void SaveAll(const wchar_t* dir) {
        CreateDirectoryW(dir, NULL);
        for (auto* m : modules) {
            wchar_t path[MAX_PATH];
            wchar_t safeName[64];
            wcscpy_s(safeName, m->GetName());
            for (wchar_t* p = safeName; *p; p++) if (*p == L' ') *p = L'_';
            swprintf_s(path, L"%s\\%s.ini", dir, safeName);
            m->SaveConfig(path);
        }
    }

    // Build TreeView with module items
    void BuildTreeView(HWND hTree) {
        TVINSERTSTRUCTW tis{};
        tis.hParent = TVI_ROOT;
        tis.hInsertAfter = TVI_LAST;
        tis.item.mask = TVIF_TEXT | TVIF_PARAM;

        for (auto* m : modules) {
            wchar_t label[128];
            swprintf_s(label, L"%s [%s]", m->GetName(), m->IsEnabled() ? L"ON" : L"OFF");
            tis.item.pszText = label;
            tis.item.lParam = (LPARAM)m;
            m->hTreeItem = TreeView_InsertItem(hTree, &tis);
            m->hTreeView = hTree;
        }
    }

    void RefreshTreeViewLabels(HWND hTree) {
        for (auto* m : modules) {
            if (!m->hTreeItem) continue;
            TVITEMW tvi{};
            tvi.mask = TVIF_TEXT;
            tvi.hItem = m->hTreeItem;
            wchar_t label[128];
            swprintf_s(label, L"%s [%s]", m->GetName(), m->IsEnabled() ? L"ON" : L"OFF");
            wchar_t buf[128]; wcscpy_s(buf, label);
            tvi.pszText = buf;
            TreeView_SetItem(hTree, &tvi);
        }
    }

private:
    std::vector<IModule*> modules;
};
