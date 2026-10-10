#include "PoolManager.h"
#include "TrainSimConsistBuilder.h"
#include "AppLogging.h"
#include "DatabaseManager.h"
#include <fstream>
#include <shlwapi.h>
#include <algorithm>
#include <map>

namespace PoolManager
{
    std::vector<PoolPreset> g_PoolPresetsCache;
    int g_ActivePresetIndex = 0;

    std::wstring GetPoolPresetCacheFilePath()
    {
        return DatabaseManager::GetDatabaseFilePath();
    }

    bool SavePoolPresetsToDisk(const std::vector<PoolPreset>& presets)
    {
        return DatabaseManager::SavePoolPresets(presets);
    }

    bool LoadPoolPresetsFromDisk(std::vector<PoolPreset>& outPresets)
    {
        return DatabaseManager::LoadPoolPresets(outPresets);
    }

    void InitializePoolPresets()
    {
        if (!g_PoolPresetsCache.empty())
            return;

        if (LoadPoolPresetsFromDisk(g_PoolPresetsCache) && !g_PoolPresetsCache.empty())
        {
            g_ActivePresetIndex = 0;
            return;
        }

        // Default initial presets
        PoolPreset defaultPreset;
        defaultPreset.presetName = L"Default Express Preset";

        ConsistPool locoPool;
        locoPool.name = L"Lead Locomotives";
        locoPool.pickMode = PoolPickMode::Random;
        locoPool.minCount = 1;
        locoPool.maxCount = 1;
        locoPool.flipPolicy = PoolFlipPolicy::ForwardOnly;
        defaultPreset.pools.push_back(locoPool);

        ConsistPool coachPool;
        coachPool.name = L"Passenger Coaches";
        coachPool.pickMode = PoolPickMode::Random;
        coachPool.minCount = 4;
        coachPool.maxCount = 12;
        coachPool.flipPolicy = PoolFlipPolicy::ForwardOnly;
        defaultPreset.pools.push_back(coachPool);

        ConsistPool guardPool;
        guardPool.name = L"Rear Brake / Guard Van";
        guardPool.pickMode = PoolPickMode::Random;
        guardPool.minCount = 1;
        guardPool.maxCount = 1;
        guardPool.flipPolicy = PoolFlipPolicy::ForwardOnly;
        defaultPreset.pools.push_back(guardPool);

        g_PoolPresetsCache.push_back(defaultPreset);
        g_ActivePresetIndex = 0;
        SavePoolPresetsToDisk(g_PoolPresetsCache);
    }

    void PersistPoolPresets()
    {
        SavePoolPresetsToDisk(g_PoolPresetsCache);
    }

    PoolPreset* GetActivePreset()
    {
        if (g_PoolPresetsCache.empty())
            InitializePoolPresets();

        if (g_ActivePresetIndex < 0 || g_ActivePresetIndex >= (int)g_PoolPresetsCache.size())
            g_ActivePresetIndex = 0;

        return &g_PoolPresetsCache[g_ActivePresetIndex];
    }

    PoolPreset* GetPresetByIndex(int index)
    {
        if (g_PoolPresetsCache.empty())
            InitializePoolPresets();

        if (index < 0 || index >= (int)g_PoolPresetsCache.size())
            return nullptr;

        return &g_PoolPresetsCache[index];
    }

    int GetActivePresetIndex()
    {
        return g_ActivePresetIndex;
    }

    bool SetActivePresetIndex(int index)
    {
        if (index >= 0 && index < (int)g_PoolPresetsCache.size())
        {
            g_ActivePresetIndex = index;
            return true;
        }
        return false;
    }

    int CreateNewPreset(const std::wstring& name)
    {
        PoolPreset newPreset;
        newPreset.presetName = name.empty() ? (L"Preset " + std::to_wstring(g_PoolPresetsCache.size() + 1)) : name;

        ConsistPool defaultPool;
        defaultPool.name = L"Locomotives";
        defaultPool.pickMode = PoolPickMode::Random;
        defaultPool.minCount = 1;
        defaultPool.maxCount = 1;
        newPreset.pools.push_back(defaultPool);

        g_PoolPresetsCache.push_back(newPreset);
        g_ActivePresetIndex = (int)g_PoolPresetsCache.size() - 1;
        PersistPoolPresets();
        return g_ActivePresetIndex;
    }

    bool RenameActivePreset(const std::wstring& newName)
    {
        if (newName.empty()) return false;
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset) return false;
        pPreset->presetName = newName;
        PersistPoolPresets();
        return true;
    }

    int CloneActivePreset()
    {
        PoolPreset* pActive = GetActivePreset();
        if (!pActive) return -1;

        PoolPreset cloned = *pActive;
        cloned.presetName += L" (Copy)";
        g_PoolPresetsCache.push_back(cloned);
        g_ActivePresetIndex = (int)g_PoolPresetsCache.size() - 1;
        PersistPoolPresets();
        return g_ActivePresetIndex;
    }

    bool DeleteActivePreset()
    {
        if (g_PoolPresetsCache.size() <= 1)
            return false;

        if (g_ActivePresetIndex >= 0 && g_ActivePresetIndex < (int)g_PoolPresetsCache.size())
        {
            g_PoolPresetsCache.erase(g_PoolPresetsCache.begin() + g_ActivePresetIndex);
            if (g_ActivePresetIndex >= (int)g_PoolPresetsCache.size())
                g_ActivePresetIndex = (int)g_PoolPresetsCache.size() - 1;
            PersistPoolPresets();
            return true;
        }
        return false;
    }

    int AddPoolToActivePreset(const std::wstring& poolName)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset) return -1;

        ConsistPool newPool;
        newPool.name = poolName.empty() ? (L"Pool #" + std::to_wstring(pPreset->pools.size() + 1)) : poolName;
        newPool.pickMode = PoolPickMode::Random;
        newPool.minCount = 1;
        newPool.maxCount = 1;
        pPreset->pools.push_back(newPool);
        PersistPoolPresets();
        return (int)pPreset->pools.size() - 1;
    }

    int ClonePoolInActivePreset(int poolIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return -1;

        ConsistPool newPool = pPreset->pools[poolIndex];
        newPool.name = newPool.name + L" (Copy)";

        int insertIdx = poolIndex + 1;
        if (insertIdx >= (int)pPreset->pools.size())
        {
            pPreset->pools.push_back(newPool);
        }
        else
        {
            pPreset->pools.insert(pPreset->pools.begin() + insertIdx, newPool);
        }
        PersistPoolPresets();
        return insertIdx;
    }

    bool RenamePool(int poolIndex, const std::wstring& newName)
    {
        if (newName.empty()) return false;
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        pPreset->pools[poolIndex].name = newName;
        PersistPoolPresets();
        return true;
    }

    bool RemovePoolFromActivePreset(int poolIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        pPreset->pools.erase(pPreset->pools.begin() + poolIndex);
        PersistPoolPresets();
        return true;
    }

    bool MovePool(int fromIndex, int toIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset) return false;
        int n = (int)pPreset->pools.size();
        if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n || fromIndex == toIndex)
            return false;

        ConsistPool moved = pPreset->pools[fromIndex];
        pPreset->pools.erase(pPreset->pools.begin() + fromIndex);
        pPreset->pools.insert(pPreset->pools.begin() + toIndex, moved);
        PersistPoolPresets();
        return true;
    }

    bool AddUnitToPool(int poolIndex, const PoolUnit& unit)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        pPreset->pools[poolIndex].units.push_back(unit);
        PersistPoolPresets();
        return true;
    }

    bool RemoveUnitFromPool(int poolIndex, int unitIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        if (unitIndex < 0 || unitIndex >= (int)pool.units.size())
            return false;

        pool.units.erase(pool.units.begin() + unitIndex);
        PersistPoolPresets();
        return true;
    }

    bool ClearPoolUnits(int poolIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        pPreset->pools[poolIndex].units.clear();
        PersistPoolPresets();
        return true;
    }

    bool CycleUnitFlipMode(int poolIndex, int unitIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        if (unitIndex < 0 || unitIndex >= (int)pool.units.size())
            return false;

        auto& u = pool.units[unitIndex];
        switch (u.flipMode)
        {
        case UnitFlipMode::Auto:    u.flipMode = UnitFlipMode::Forward; break;
        case UnitFlipMode::Forward: u.flipMode = UnitFlipMode::Flipped; break;
        case UnitFlipMode::Flipped: u.flipMode = UnitFlipMode::Random; break;
        case UnitFlipMode::Random:  u.flipMode = UnitFlipMode::Auto; break;
        }
        PersistPoolPresets();
        return true;
    }

    bool SetUnitFlipMode(int poolIndex, int unitIndex, UnitFlipMode mode)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        if (unitIndex < 0 || unitIndex >= (int)pool.units.size())
            return false;

        pool.units[unitIndex].flipMode = mode;
        PersistPoolPresets();
        return true;
    }

    bool CyclePoolFlipPolicy(int poolIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        switch (pool.flipPolicy)
        {
        case PoolFlipPolicy::ForwardOnly:   pool.flipPolicy = PoolFlipPolicy::AllowRandom; break;
        case PoolFlipPolicy::AllowRandom:   pool.flipPolicy = PoolFlipPolicy::AlwaysFlipped; break;
        case PoolFlipPolicy::AlwaysFlipped: pool.flipPolicy = PoolFlipPolicy::ForwardOnly; break;
        }
        PersistPoolPresets();
        return true;
    }

    int PasteUnitsToPool(int poolIndex, const std::vector<ConsistReader::UnitInfo>& clipboardUnits)
    {
        if (clipboardUnits.empty()) return 0;
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return 0;

        auto& pool = pPreset->pools[poolIndex];
        int added = 0;
        for (const auto& cu : clipboardUnits)
        {
            PoolUnit pu;
            pu.szFileName = cu.uid;
            pu.szFolder = cu.parentDir;
            pu.isEngine = cu.isEngine;
            pu.flipMode = cu.isFlipped ? UnitFlipMode::Flipped : UnitFlipMode::Auto;
            pool.units.push_back(pu);
            added++;
        }
        PersistPoolPresets();
        return added;
    }

    bool CopyPoolUnitsToClipboard(int poolIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        const auto& pool = pPreset->pools[poolIndex];
        if (pool.units.empty())
            return false;

        std::vector<ConsistReader::UnitInfo> clipUnits;
        for (const auto& pu : pool.units)
        {
            ConsistReader::UnitInfo ui;
            ui.uid = pu.szFileName;
            ui.parentDir = pu.szFolder;
            ui.isEngine = pu.isEngine;
            ui.isFlipped = (pu.flipMode == UnitFlipMode::Flipped);
            clipUnits.push_back(ui);
        }

        SetAppClipboardUnits(clipUnits);
        return true;
    }

    bool CopySingleUnitToClipboard(int poolIndex, int unitIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        const auto& pool = pPreset->pools[poolIndex];
        if (unitIndex < 0 || unitIndex >= (int)pool.units.size())
            return false;

        const auto& pu = pool.units[unitIndex];
        std::vector<ConsistReader::UnitInfo> clipUnits;
        ConsistReader::UnitInfo ui;
        ui.uid = pu.szFileName;
        ui.parentDir = pu.szFolder;
        ui.isEngine = pu.isEngine;
        ui.isFlipped = (pu.flipMode == UnitFlipMode::Flipped);
        clipUnits.push_back(ui);

        SetAppClipboardUnits(clipUnits);
        return true;
    }

    bool CopyMultipleUnitsToClipboard(int poolIndex, const std::vector<int>& unitIndices)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        const auto& pool = pPreset->pools[poolIndex];
        if (pool.units.empty() || unitIndices.empty())
            return false;

        std::vector<ConsistReader::UnitInfo> clipUnits;
        for (int idx : unitIndices)
        {
            if (idx >= 0 && idx < (int)pool.units.size())
            {
                const auto& pu = pool.units[idx];
                ConsistReader::UnitInfo ui;
                ui.uid = pu.szFileName;
                ui.parentDir = pu.szFolder;
                ui.isEngine = pu.isEngine;
                ui.isFlipped = (pu.flipMode == UnitFlipMode::Flipped);
                clipUnits.push_back(ui);
            }
        }

        if (clipUnits.empty()) return false;
        SetAppClipboardUnits(clipUnits);
        return true;
    }

    bool RemoveMultipleUnitsFromPool(int poolIndex, const std::vector<int>& unitIndices)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        if (unitIndices.empty()) return false;

        std::vector<int> sortedIndices = unitIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());

        for (int idx : sortedIndices)
        {
            if (idx >= 0 && idx < (int)pool.units.size())
            {
                pool.units.erase(pool.units.begin() + idx);
            }
        }

        PersistPoolPresets();
        return true;
    }

    bool SetMultipleUnitsFlipMode(int poolIndex, const std::vector<int>& unitIndices, UnitFlipMode mode)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;

        auto& pool = pPreset->pools[poolIndex];
        for (int idx : unitIndices)
        {
            if (idx >= 0 && idx < (int)pool.units.size())
            {
                pool.units[idx].flipMode = mode;
            }
        }

        PersistPoolPresets();
        return true;
    }

    bool MoveUnitInPool(int poolIndex, int fromUnitIndex, int toUnitIndex)
    {
        std::vector<int> indices = { fromUnitIndex };
        return MoveUnitsInPool(poolIndex, indices, toUnitIndex);
    }

    bool MoveUnitsInPool(int poolIndex, const std::vector<int>& fromIndices, int targetIndex)
    {
        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset || poolIndex < 0 || poolIndex >= (int)pPreset->pools.size())
            return false;
        if (fromIndices.empty()) return false;

        auto& pool = pPreset->pools[poolIndex];
        int totalUnits = (int)pool.units.size();
        if (totalUnits <= 1) return false;

        std::vector<int> sortedIndices = fromIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());

        for (int idx : sortedIndices)
        {
            if (idx < 0 || idx >= totalUnits) return false;
        }

        std::vector<PoolUnit> movingUnits;
        for (int idx : sortedIndices)
        {
            movingUnits.push_back(pool.units[idx]);
        }

        int adjustedTarget = targetIndex;
        for (int idx : sortedIndices)
        {
            if (idx < targetIndex)
                adjustedTarget--;
        }

        for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
        {
            pool.units.erase(pool.units.begin() + *it);
        }

        if (adjustedTarget < 0) adjustedTarget = 0;
        if (adjustedTarget > (int)pool.units.size()) adjustedTarget = (int)pool.units.size();

        pool.units.insert(pool.units.begin() + adjustedTarget, movingUnits.begin(), movingUnits.end());
        PersistPoolPresets();
        return true;
    }

    bool MoveUnitsAcrossPools(int fromPoolIndex, const std::vector<int>& fromIndices, int toPoolIndex, int targetIndex)
    {
        if (fromPoolIndex == toPoolIndex)
            return MoveUnitsInPool(fromPoolIndex, fromIndices, targetIndex);

        PoolPreset* pPreset = GetActivePreset();
        if (!pPreset) return false;
        if (fromPoolIndex < 0 || fromPoolIndex >= (int)pPreset->pools.size()) return false;
        if (toPoolIndex < 0 || toPoolIndex >= (int)pPreset->pools.size()) return false;
        if (fromIndices.empty()) return false;

        auto& fromPool = pPreset->pools[fromPoolIndex];
        auto& toPool = pPreset->pools[toPoolIndex];

        std::vector<int> sortedIndices = fromIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());

        for (int idx : sortedIndices)
        {
            if (idx < 0 || idx >= (int)fromPool.units.size()) return false;
        }

        std::vector<PoolUnit> movingUnits;
        for (int idx : sortedIndices)
        {
            movingUnits.push_back(fromPool.units[idx]);
        }

        for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
        {
            fromPool.units.erase(fromPool.units.begin() + *it);
        }

        if (targetIndex < 0) targetIndex = 0;
        if (targetIndex > (int)toPool.units.size()) targetIndex = (int)toPool.units.size();

        toPool.units.insert(toPool.units.begin() + targetIndex, movingUnits.begin(), movingUnits.end());
        PersistPoolPresets();
        return true;
    }

    // -----------------------------------------------------------------------
    // Replacement Group / Palette Management
    // -----------------------------------------------------------------------
    std::vector<ReplacementGroup> g_ReplacementGroupsCache;
    std::vector<std::wstring> g_ReplacementGroupCategoriesCache;

    std::wstring GetReplacementGroupsCacheFilePath()
    {
        return DatabaseManager::GetDatabaseFilePath();
    }

    bool SaveReplacementGroupsToDisk(const std::vector<ReplacementGroup>& groups)
    {
        return DatabaseManager::SaveReplacementGroups(groups);
    }

    bool LoadReplacementGroupsFromDisk(std::vector<ReplacementGroup>& outGroups)
    {
        return DatabaseManager::LoadReplacementGroups(outGroups);
    }

    void PersistReplacementGroupCategories()
    {
        std::wstring joined;
        for (size_t i = 0; i < g_ReplacementGroupCategoriesCache.size(); ++i)
        {
            if (i > 0) joined += L"\t";
            joined += g_ReplacementGroupCategoriesCache[i];
        }
        DatabaseManager::SetSetting(L"ReplacementGroupCategories", joined);
    }

    void InitializeReplacementGroups()
    {
        if (!LoadReplacementGroupsFromDisk(g_ReplacementGroupsCache))
        {
            g_ReplacementGroupsCache.clear();
        }

        g_ReplacementGroupCategoriesCache.clear();

        std::wstring savedCats = DatabaseManager::GetSetting(L"ReplacementGroupCategories", L"");
        if (!savedCats.empty())
        {
            std::wstringstream ss(savedCats);
            std::wstring cat;
            while (std::getline(ss, cat, L'\t'))
            {
                if (!cat.empty())
                {
                    bool exists = false;
                    for (const auto& c : g_ReplacementGroupCategoriesCache)
                    {
                        if (_wcsicmp(c.c_str(), cat.c_str()) == 0) { exists = true; break; }
                    }
                    if (!exists)
                    {
                        g_ReplacementGroupCategoriesCache.push_back(cat);
                    }
                }
            }
        }

        // Also register categories from existing groups if any
        for (const auto& grp : g_ReplacementGroupsCache)
        {
            if (!grp.category.empty())
            {
                bool exists = false;
                for (const auto& ex : g_ReplacementGroupCategoriesCache)
                {
                    if (_wcsicmp(ex.c_str(), grp.category.c_str()) == 0) { exists = true; break; }
                }
                if (!exists)
                {
                    g_ReplacementGroupCategoriesCache.push_back(grp.category);
                }
            }
        }
    }

    void PersistReplacementGroups()
    {
        SaveReplacementGroupsToDisk(g_ReplacementGroupsCache);
        PersistReplacementGroupCategories();
    }

    std::vector<std::wstring> GetReplacementGroupCategories()
    {
        return g_ReplacementGroupCategoriesCache;
    }

    bool AddReplacementGroupCategory(const std::wstring& categoryName)
    {
        std::wstring trimmed = categoryName;
        size_t first = trimmed.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return false;
        size_t last = trimmed.find_last_not_of(L" \t\r\n");
        trimmed = trimmed.substr(first, (last - first + 1));
        if (trimmed.empty()) return false;

        for (const auto& c : g_ReplacementGroupCategoriesCache)
        {
            if (_wcsicmp(c.c_str(), trimmed.c_str()) == 0)
                return false; // Already exists
        }

        g_ReplacementGroupCategoriesCache.push_back(trimmed);
        PersistReplacementGroupCategories();
        return true;
    }

    bool DeleteReplacementGroupCategory(const std::wstring& categoryName)
    {
        auto it = std::find_if(g_ReplacementGroupCategoriesCache.begin(), g_ReplacementGroupCategoriesCache.end(),
            [&](const std::wstring& c) { return _wcsicmp(c.c_str(), categoryName.c_str()) == 0; });

        if (it == g_ReplacementGroupCategoriesCache.end())
            return false;

        g_ReplacementGroupCategoriesCache.erase(it);

        // Delete all groups in this category along with their units
        g_ReplacementGroupsCache.erase(
            std::remove_if(g_ReplacementGroupsCache.begin(), g_ReplacementGroupsCache.end(),
                [&](const ReplacementGroup& grp) {
                    return _wcsicmp(grp.category.c_str(), categoryName.c_str()) == 0;
                }),
            g_ReplacementGroupsCache.end()
        );

        PersistReplacementGroups();
        return true;
    }

    bool EmptyReplacementGroupCategory(const std::wstring& categoryName)
    {
        // Remove all favourite groups belonging to this category
        g_ReplacementGroupsCache.erase(
            std::remove_if(g_ReplacementGroupsCache.begin(), g_ReplacementGroupsCache.end(),
                [&](const ReplacementGroup& grp) {
                    return _wcsicmp(grp.category.c_str(), categoryName.c_str()) == 0;
                }),
            g_ReplacementGroupsCache.end()
        );

        PersistReplacementGroups();
        return true;
    }

    bool RenameReplacementGroupCategory(const std::wstring& oldName, const std::wstring& newName)
    {
        std::wstring trimmed = newName;
        size_t first = trimmed.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return false;
        size_t last = trimmed.find_last_not_of(L" \t\r\n");
        trimmed = trimmed.substr(first, (last - first + 1));
        if (trimmed.empty()) return false;

        for (const auto& c : g_ReplacementGroupCategoriesCache)
        {
            if (_wcsicmp(c.c_str(), trimmed.c_str()) == 0)
                return false; // Duplicate
        }

        auto it = std::find_if(g_ReplacementGroupCategoriesCache.begin(), g_ReplacementGroupCategoriesCache.end(),
            [&](const std::wstring& c) { return _wcsicmp(c.c_str(), oldName.c_str()) == 0; });

        if (it == g_ReplacementGroupCategoriesCache.end())
            return false;

        *it = trimmed;

        for (auto& grp : g_ReplacementGroupsCache)
        {
            if (_wcsicmp(grp.category.c_str(), oldName.c_str()) == 0)
            {
                grp.category = trimmed;
            }
        }

        PersistReplacementGroups();
        return true;
    }

    int AddReplacementGroup(const std::wstring& name, const std::wstring& category)
    {
        ReplacementGroup grp;
        if (name.empty())
        {
            grp.name = L"Group #" + std::to_wstring(g_ReplacementGroupsCache.size() + 1);
        }
        else
        {
            grp.name = name;
        }

        std::wstring cat = category;
        if (cat.empty() && !g_ReplacementGroupCategoriesCache.empty())
        {
            cat = g_ReplacementGroupCategoriesCache[0];
        }
        grp.category = cat;
        g_ReplacementGroupsCache.push_back(grp);
        PersistReplacementGroups();
        return (int)g_ReplacementGroupsCache.size() - 1;
    }

    int CloneReplacementGroup(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return -1;

        ReplacementGroup newGrp = g_ReplacementGroupsCache[groupIndex];
        newGrp.name = newGrp.name + L" (Copy)";

        int insertIdx = groupIndex + 1;
        if (insertIdx >= (int)g_ReplacementGroupsCache.size())
        {
            g_ReplacementGroupsCache.push_back(newGrp);
        }
        else
        {
            g_ReplacementGroupsCache.insert(g_ReplacementGroupsCache.begin() + insertIdx, newGrp);
        }
        PersistReplacementGroups();
        return insertIdx;
    }

    bool MoveReplacementGroup(int fromIndex, int toIndex)
    {
        int n = (int)g_ReplacementGroupsCache.size();
        if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n || fromIndex == toIndex)
            return false;

        std::swap(g_ReplacementGroupsCache[fromIndex], g_ReplacementGroupsCache[toIndex]);
        PersistReplacementGroups();
        return true;
    }

    bool RemoveReplacementGroup(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache.erase(g_ReplacementGroupsCache.begin() + groupIndex);
        PersistReplacementGroups();
        return true;
    }

    bool RenameReplacementGroup(int groupIndex, const std::wstring& newName)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size() || newName.empty())
            return false;

        g_ReplacementGroupsCache[groupIndex].name = newName;
        PersistReplacementGroups();
        return true;
    }

    bool SetReplacementGroupCategory(int groupIndex, const std::wstring& newCategory)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache[groupIndex].category = newCategory.empty() ? L"General" : newCategory;
        PersistReplacementGroups();
        return true;
    }

    bool CycleReplacementGroupPickMode(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        grp.pickMode = (grp.pickMode == PoolPickMode::Random) ? PoolPickMode::Sequential : PoolPickMode::Random;
        PersistReplacementGroups();
        return true;
    }

    bool CycleReplacementGroupFlipPolicy(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        if (grp.flipPolicy == PoolFlipPolicy::ForwardOnly)
            grp.flipPolicy = PoolFlipPolicy::AllowRandom;
        else if (grp.flipPolicy == PoolFlipPolicy::AllowRandom)
            grp.flipPolicy = PoolFlipPolicy::AlwaysFlipped;
        else
            grp.flipPolicy = PoolFlipPolicy::ForwardOnly;

        PersistReplacementGroups();
        return true;
    }

    bool SetReplacementGroupPickMode(int groupIndex, PoolPickMode mode)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache[groupIndex].pickMode = mode;
        PersistReplacementGroups();
        return true;
    }

    bool SetReplacementGroupFlipPolicy(int groupIndex, PoolFlipPolicy policy)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache[groupIndex].flipPolicy = policy;
        PersistReplacementGroups();
        return true;
    }

    bool AddUnitToReplacementGroup(int groupIndex, const PoolUnit& unit)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache[groupIndex].units.push_back(unit);
        PersistReplacementGroups();
        return true;
    }

    bool RemoveUnitFromReplacementGroup(int groupIndex, int unitIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        if (unitIndex < 0 || unitIndex >= (int)grp.units.size())
            return false;

        grp.units.erase(grp.units.begin() + unitIndex);
        PersistReplacementGroups();
        return true;
    }

    bool ClearReplacementGroupUnits(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        g_ReplacementGroupsCache[groupIndex].units.clear();
        PersistReplacementGroups();
        return true;
    }

    bool CopyReplacementGroupUnitsToClipboard(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        const auto& grp = g_ReplacementGroupsCache[groupIndex];
        if (grp.units.empty())
            return false;

        std::vector<ConsistReader::UnitInfo> clipUnits;
        for (const auto& pu : grp.units)
        {
            ConsistReader::UnitInfo ui;
            ui.uid = pu.szFileName;
            ui.parentDir = pu.szFolder;
            ui.isEngine = pu.isEngine;
            ui.isFlipped = (pu.flipMode == UnitFlipMode::Flipped);
            clipUnits.push_back(ui);
        }

        SetAppClipboardUnits(clipUnits);
        return true;
    }

    bool CopyMultipleUnitsFromReplacementGroupToClipboard(int groupIndex, const std::vector<int>& unitIndices)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        const auto& grp = g_ReplacementGroupsCache[groupIndex];
        if (grp.units.empty() || unitIndices.empty())
            return false;

        std::vector<ConsistReader::UnitInfo> clipUnits;
        for (int idx : unitIndices)
        {
            if (idx >= 0 && idx < (int)grp.units.size())
            {
                const auto& pu = grp.units[idx];
                ConsistReader::UnitInfo ui;
                ui.uid = pu.szFileName;
                ui.parentDir = pu.szFolder;
                ui.isEngine = pu.isEngine;
                ui.isFlipped = (pu.flipMode == UnitFlipMode::Flipped);
                clipUnits.push_back(ui);
            }
        }

        if (clipUnits.empty()) return false;
        SetAppClipboardUnits(clipUnits);
        return true;
    }

    int PasteUnitsToReplacementGroup(int groupIndex, const std::vector<ConsistReader::UnitInfo>& clipboardUnits)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return 0;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        int added = 0;
        for (const auto& u : clipboardUnits)
        {
            PoolUnit pu;
            pu.szFileName = u.uid;
            pu.szFolder = u.parentDir;
            pu.isEngine = u.isEngine;
            pu.flipMode = u.isFlipped ? UnitFlipMode::Flipped : UnitFlipMode::Forward;
            grp.units.push_back(pu);
            added++;
        }

        if (added > 0)
        {
            PersistReplacementGroups();
        }
        return added;
    }

    bool RemoveMultipleUnitsFromReplacementGroup(int groupIndex, const std::vector<int>& unitIndices)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size() || unitIndices.empty())
            return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        std::vector<int> sortedIndices = unitIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());

        for (int idx : sortedIndices)
        {
            if (idx >= 0 && idx < (int)grp.units.size())
            {
                grp.units.erase(grp.units.begin() + idx);
            }
        }

        PersistReplacementGroups();
        return true;
    }

    bool SetMultipleUnitsFlipModeInReplacementGroup(int groupIndex, const std::vector<int>& unitIndices, UnitFlipMode mode)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        for (int idx : unitIndices)
        {
            if (idx >= 0 && idx < (int)grp.units.size())
            {
                grp.units[idx].flipMode = mode;
            }
        }

        PersistReplacementGroups();
        return true;
    }

    bool MoveUnitInReplacementGroup(int groupIndex, int fromUnitIndex, int toUnitIndex)
    {
        std::vector<int> indices = { fromUnitIndex };
        return MoveUnitsInReplacementGroup(groupIndex, indices, toUnitIndex);
    }

    bool MoveUnitsInReplacementGroup(int groupIndex, const std::vector<int>& fromIndices, int targetIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;
        if (fromIndices.empty()) return false;

        auto& grp = g_ReplacementGroupsCache[groupIndex];
        int totalUnits = (int)grp.units.size();
        if (totalUnits <= 1) return false;

        std::vector<int> sortedIndices = fromIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());

        for (int idx : sortedIndices)
        {
            if (idx < 0 || idx >= totalUnits) return false;
        }

        std::vector<PoolUnit> movingUnits;
        for (int idx : sortedIndices)
        {
            movingUnits.push_back(grp.units[idx]);
        }

        int adjustedTarget = targetIndex;
        for (int idx : sortedIndices)
        {
            if (idx < targetIndex)
                adjustedTarget--;
        }

        for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
        {
            grp.units.erase(grp.units.begin() + *it);
        }

        if (adjustedTarget < 0) adjustedTarget = 0;
        if (adjustedTarget > (int)grp.units.size()) adjustedTarget = (int)grp.units.size();

        grp.units.insert(grp.units.begin() + adjustedTarget, movingUnits.begin(), movingUnits.end());
        PersistReplacementGroups();
        return true;
    }

    bool MoveUnitsAcrossReplacementGroups(int fromGroupIndex, const std::vector<int>& fromIndices, int toGroupIndex, int targetIndex)
    {
        if (fromGroupIndex == toGroupIndex)
            return MoveUnitsInReplacementGroup(fromGroupIndex, fromIndices, targetIndex);

        if (fromGroupIndex < 0 || fromGroupIndex >= (int)g_ReplacementGroupsCache.size()) return false;
        if (toGroupIndex < 0 || toGroupIndex >= (int)g_ReplacementGroupsCache.size()) return false;
        if (fromIndices.empty()) return false;

        auto& fromGrp = g_ReplacementGroupsCache[fromGroupIndex];
        auto& toGrp = g_ReplacementGroupsCache[toGroupIndex];

        std::vector<int> sortedIndices = fromIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());

        for (int idx : sortedIndices)
        {
            if (idx < 0 || idx >= (int)fromGrp.units.size()) return false;
        }

        std::vector<PoolUnit> movingUnits;
        for (int idx : sortedIndices)
        {
            movingUnits.push_back(fromGrp.units[idx]);
        }

        for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
        {
            fromGrp.units.erase(fromGrp.units.begin() + *it);
        }

        if (targetIndex < 0) targetIndex = 0;
        if (targetIndex > (int)toGrp.units.size()) targetIndex = (int)toGrp.units.size();

        toGrp.units.insert(toGrp.units.begin() + targetIndex, movingUnits.begin(), movingUnits.end());
        PersistReplacementGroups();
        return true;
    }

    bool PickUnitFromGroup(int groupIndex, PoolUnit& outUnit, int seqIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)g_ReplacementGroupsCache.size())
            return false;

        const auto& grp = g_ReplacementGroupsCache[groupIndex];
        if (grp.units.empty())
            return false;

        int pick = 0;
        if (grp.pickMode == PoolPickMode::Sequential)
        {
            static std::map<int, int> s_groupSeq;
            int s = (seqIndex >= 0) ? seqIndex : s_groupSeq[groupIndex]++;
            pick = s % (int)grp.units.size();
        }
        else
        {
            pick = rand() % (int)grp.units.size();
        }
        outUnit = grp.units[pick];
        return true;
    }
}
