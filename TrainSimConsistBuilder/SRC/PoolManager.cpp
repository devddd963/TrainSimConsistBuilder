#include "PoolManager.h"
#include "TrainSimConsistBuilder.h"
#include <fstream>
#include <shlwapi.h>
#include <algorithm>

namespace PoolManager
{
    std::vector<PoolPreset> g_PoolPresetsCache;
    int g_ActivePresetIndex = 0;

    std::wstring GetPoolPresetCacheFilePath()
    {
        wchar_t szExePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring exePath = szExePath;
        size_t lastSlash = exePath.find_last_of(L"\\/");
        std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";

        std::wstring appDataDir = dir + L"AppData";
        CreateDirectoryW(appDataDir.c_str(), NULL);

        return appDataDir + L"\\PoolPreset.dat";
    }

    static void WriteWString(HANDLE hFile, const std::wstring& str)
    {
        uint32_t len = (uint32_t)str.length();
        DWORD written = 0;
        WriteFile(hFile, &len, sizeof(len), &written, NULL);
        if (len > 0)
        {
            WriteFile(hFile, str.data(), (DWORD)(len * sizeof(wchar_t)), &written, NULL);
        }
    }

    static bool ReadWString(HANDLE hFile, std::wstring& outStr)
    {
        uint32_t len = 0;
        DWORD read = 0;
        if (!ReadFile(hFile, &len, sizeof(len), &read, NULL) || read != sizeof(len))
            return false;

        if (len == 0)
        {
            outStr.clear();
            return true;
        }

        outStr.resize(len);
        if (!ReadFile(hFile, &outStr[0], (DWORD)(len * sizeof(wchar_t)), &read, NULL) || read != len * sizeof(wchar_t))
            return false;

        return true;
    }

    bool SavePoolPresetsToDisk(const std::vector<PoolPreset>& presets)
    {
        std::wstring cachePath = GetPoolPresetCacheFilePath();
        HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        uint32_t magic = 0x504F4F4C;
        uint32_t version = 3;
        WriteFile(hFile, &magic, sizeof(magic), &written, NULL);
        WriteFile(hFile, &version, sizeof(version), &written, NULL);

        uint32_t presetCount = (uint32_t)presets.size();
        WriteFile(hFile, &presetCount, sizeof(presetCount), &written, NULL);

        for (const auto& preset : presets)
        {
            WriteWString(hFile, preset.presetName);
            uint32_t poolCount = (uint32_t)preset.pools.size();
            WriteFile(hFile, &poolCount, sizeof(poolCount), &written, NULL);

            for (const auto& pool : preset.pools)
            {
                WriteWString(hFile, pool.name);
                uint32_t pMode = (uint32_t)pool.pickMode;
                WriteFile(hFile, &pMode, sizeof(pMode), &written, NULL);
                WriteFile(hFile, &pool.minCount, sizeof(pool.minCount), &written, NULL);
                WriteFile(hFile, &pool.maxCount, sizeof(pool.maxCount), &written, NULL);
                
                uint32_t flipPol = (uint32_t)pool.flipPolicy;
                WriteFile(hFile, &flipPol, sizeof(flipPol), &written, NULL);

                uint32_t collapsed = pool.isCollapsed ? 1 : 0;
                WriteFile(hFile, &collapsed, sizeof(collapsed), &written, NULL);

                uint32_t unitCount = (uint32_t)pool.units.size();
                WriteFile(hFile, &unitCount, sizeof(unitCount), &written, NULL);

                for (const auto& u : pool.units)
                {
                    WriteWString(hFile, u.szFileName);
                    WriteWString(hFile, u.szFolder);
                    uint32_t isEng = u.isEngine ? 1 : 0;
                    WriteFile(hFile, &isEng, sizeof(isEng), &written, NULL);

                    uint32_t fMode = (uint32_t)u.flipMode;
                    WriteFile(hFile, &fMode, sizeof(fMode), &written, NULL);
                }
            }
        }

        CloseHandle(hFile);
        return true;
    }

    bool LoadPoolPresetsFromDisk(std::vector<PoolPreset>& outPresets)
    {
        std::wstring cachePath = GetPoolPresetCacheFilePath();
        HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        DWORD read = 0;
        uint32_t magic = 0;
        uint32_t version = 0;
        if (!ReadFile(hFile, &magic, sizeof(magic), &read, NULL) || magic != 0x504F4F4C)
        {
            CloseHandle(hFile);
            return false;
        }

        ReadFile(hFile, &version, sizeof(version), &read, NULL);

        uint32_t presetCount = 0;
        if (!ReadFile(hFile, &presetCount, sizeof(presetCount), &read, NULL))
        {
            CloseHandle(hFile);
            return false;
        }

        outPresets.clear();
        outPresets.reserve(presetCount);

        for (uint32_t i = 0; i < presetCount; ++i)
        {
            PoolPreset preset;
            if (!ReadWString(hFile, preset.presetName)) break;

            uint32_t poolCount = 0;
            if (!ReadFile(hFile, &poolCount, sizeof(poolCount), &read, NULL)) break;

            preset.pools.reserve(poolCount);
            for (uint32_t p = 0; p < poolCount; ++p)
            {
                ConsistPool pool;
                if (!ReadWString(hFile, pool.name)) break;

                uint32_t pMode = 0;
                ReadFile(hFile, &pMode, sizeof(pMode), &read, NULL);
                pool.pickMode = (PoolPickMode)pMode;

                ReadFile(hFile, &pool.minCount, sizeof(pool.minCount), &read, NULL);
                ReadFile(hFile, &pool.maxCount, sizeof(pool.maxCount), &read, NULL);

                if (version >= 2)
                {
                    uint32_t flipPol = 0;
                    ReadFile(hFile, &flipPol, sizeof(flipPol), &read, NULL);
                    pool.flipPolicy = (PoolFlipPolicy)flipPol;
                }

                if (version >= 3)
                {
                    uint32_t collapsed = 0;
                    ReadFile(hFile, &collapsed, sizeof(collapsed), &read, NULL);
                    pool.isCollapsed = (collapsed != 0);
                }

                uint32_t unitCount = 0;
                ReadFile(hFile, &unitCount, sizeof(unitCount), &read, NULL);

                pool.units.reserve(unitCount);
                for (uint32_t u = 0; u < unitCount; ++u)
                {
                    PoolUnit unit;
                    if (!ReadWString(hFile, unit.szFileName)) break;
                    if (!ReadWString(hFile, unit.szFolder)) break;

                    uint32_t isEng = 1;
                    ReadFile(hFile, &isEng, sizeof(isEng), &read, NULL);
                    unit.isEngine = (isEng != 0);

                    if (version >= 2)
                    {
                        uint32_t fMode = 0;
                        ReadFile(hFile, &fMode, sizeof(fMode), &read, NULL);
                        unit.flipMode = (UnitFlipMode)fMode;
                    }

                    pool.units.push_back(unit);
                }
                preset.pools.push_back(pool);
            }
            outPresets.push_back(preset);
        }

        CloseHandle(hFile);
        return true;
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

        std::swap(pPreset->pools[fromIndex], pPreset->pools[toIndex]);
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
}
