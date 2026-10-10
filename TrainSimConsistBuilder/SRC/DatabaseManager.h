#pragma once

#include <windows.h>
#include <string>
#include <unordered_map>
#include "AssetsParser.h"
#include "PoolManager.h"
#include "PoolMutator.h"

namespace DatabaseManager
{
    // Lifecycle
    bool Initialize();
    void Shutdown();

    // App Directory helper (ensures AppData folder exists and returns path)
    std::wstring GetAppDirectory();
    std::wstring GetDatabaseFilePath();

    // AppSettings (Key-Value configuration store, replaces Windows Registry)
    std::wstring GetSetting(const std::wstring& key, const std::wstring& defaultVal = L"");
    void SetSetting(const std::wstring& key, const std::wstring& val);
    int GetSettingInt(const std::wstring& key, int defaultVal = 0);
    void SetSettingInt(const std::wstring& key, int val);

    // Rolling Stock Cache persistence & Fast Kernel32 Incremental Sync
    bool LoadStockCache(uint64_t currentSig, std::vector<StockItem>& outCache);
    void SaveStockCache(uint64_t currentSig, const std::vector<StockItem>& cache);
    bool LoadStockMap(std::unordered_map<std::wstring, StockItem>& outMap);
    bool SyncStockDiff(const std::vector<StockItem>& toInsert, const std::vector<StockItem>& toUpdate, const std::vector<std::pair<std::wstring, std::wstring>>& toDelete);

    // Pool Presets persistence
    bool LoadPoolPresets(std::vector<PoolManager::PoolPreset>& outPresets);
    bool SavePoolPresets(const std::vector<PoolManager::PoolPreset>& presets);

    // Replacement Groups persistence
    bool LoadReplacementGroups(std::vector<PoolManager::ReplacementGroup>& outGroups);
    bool SaveReplacementGroups(const std::vector<PoolManager::ReplacementGroup>& groups);

    // Pool Mutation Settings persistence
    bool LoadMutationSettings(PoolMutator::MutatorSavedSettings& outSettings);
    bool SaveMutationSettings(const PoolMutator::MutatorSavedSettings& settings);

    // Blueprint Roles persistence (Train Config Studio)
    struct BlueprintRoleItem {
        int id = 0;
        std::wstring roleName;
        std::wstring category;
        bool isDefault = false;
    };

    bool LoadBlueprintRoles(std::vector<BlueprintRoleItem>& outRoles);
    bool AddBlueprintRole(const std::wstring& roleName, const std::wstring& category = L"Custom", bool isDefault = false);
    bool DeleteBlueprintRole(const std::wstring& roleName);
    bool RestoreDefaultBlueprintRoles();

    // Cleanup legacy .dat cache files if present
    void CleanupLegacyDatFiles();
}
