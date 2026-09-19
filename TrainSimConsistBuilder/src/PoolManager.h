#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "ConsistReader.h"

namespace PoolManager
{
    enum class UnitFlipMode {
        Auto = 0,      // Follow pool setting
        Forward = 1,   // Force Forward (never flip)
        Flipped = 2,   // Force Reversed (always flip)
        Random = 3     // 50% Random Flip
    };

    struct PoolUnit {
        std::wstring szFileName;  // Unit filename without extension (e.g. "WAP7_30201")
        std::wstring szFolder;    // Parent folder under TRAINSET (e.g. "IR_WAP7_Pack")
        bool isEngine = true;     // Engine (.eng) vs Wagon (.wag)
        UnitFlipMode flipMode = UnitFlipMode::Auto; // Unit orientation override
    };

    enum class PoolPickMode {
        Random = 0,        // Randomly pick from pool units
        Sequential = 1     // Pick in order / round-robin
    };

    enum class PoolFlipPolicy {
        ForwardOnly = 0,   // Forward orientation only
        AllowRandom = 1,   // Randomly flip units that are set to Auto
        AlwaysFlipped = 2  // Always flip units that are set to Auto
    };

    struct ConsistPool {
        std::wstring name;                         // e.g. "Locomotives", "Front EOG", "Coaches"
        PoolPickMode pickMode = PoolPickMode::Random;
        int minCount = 1;                          // Min units to pick
        int maxCount = 1;                          // Max units to pick
        bool flipAllowed = false;                  // Backward compatibility flag
        PoolFlipPolicy flipPolicy = PoolFlipPolicy::ForwardOnly; // Pool orientation policy
        bool isCollapsed = false;                  // Card collapsed/expanded UI state
        std::vector<PoolUnit> units;               // Loaded units in this pool
    };

    struct PoolPreset {
        std::wstring presetName;                   // e.g. "LHB 22-Coach Rajdhani", "BOXN Freight"
        std::vector<ConsistPool> pools;            // Ordered list of pools
    };

    // Cache File Management (AppData\PoolPreset.dat)
    std::wstring GetPoolPresetCacheFilePath();
    bool LoadPoolPresetsFromDisk(std::vector<PoolPreset>& outPresets);
    bool SavePoolPresetsToDisk(const std::vector<PoolPreset>& presets);

    // Global in-memory presets cache
    extern std::vector<PoolPreset> g_PoolPresetsCache;
    extern int g_ActivePresetIndex;

    // Initialization & persistence
    void InitializePoolPresets();
    void PersistPoolPresets();

    // Preset CRUD operations
    PoolPreset* GetActivePreset();
    PoolPreset* GetPresetByIndex(int index);
    int GetActivePresetIndex();
    bool SetActivePresetIndex(int index);
    int CreateNewPreset(const std::wstring& name = L"");
    bool RenameActivePreset(const std::wstring& newName);
    int CloneActivePreset();
    bool DeleteActivePreset();

    // Pool operations within active preset
    int AddPoolToActivePreset(const std::wstring& poolName = L"");
    int ClonePoolInActivePreset(int poolIndex);
    bool RenamePool(int poolIndex, const std::wstring& newName);
    bool RemovePoolFromActivePreset(int poolIndex);
    bool MovePool(int fromIndex, int toIndex);
    bool AddUnitToPool(int poolIndex, const PoolUnit& unit);
    bool RemoveUnitFromPool(int poolIndex, int unitIndex);
    bool ClearPoolUnits(int poolIndex);
    bool CycleUnitFlipMode(int poolIndex, int unitIndex);
    bool SetUnitFlipMode(int poolIndex, int unitIndex, UnitFlipMode mode);
    bool CyclePoolFlipPolicy(int poolIndex);

    // Clipboard transfer helpers
    int PasteUnitsToPool(int poolIndex, const std::vector<ConsistReader::UnitInfo>& clipboardUnits);
    bool CopyPoolUnitsToClipboard(int poolIndex);
    bool CopySingleUnitToClipboard(int poolIndex, int unitIndex);
    bool CopyMultipleUnitsToClipboard(int poolIndex, const std::vector<int>& unitIndices);
    bool RemoveMultipleUnitsFromPool(int poolIndex, const std::vector<int>& unitIndices);
    bool SetMultipleUnitsFlipMode(int poolIndex, const std::vector<int>& unitIndices, UnitFlipMode mode);
}

// Backward compatibility namespace
namespace BatchConsistWizard
{
    using UnitFlipMode = PoolManager::UnitFlipMode;
    using PoolUnit = PoolManager::PoolUnit;
    using PoolPickMode = PoolManager::PoolPickMode;
    using PoolFlipPolicy = PoolManager::PoolFlipPolicy;
    using ConsistPool = PoolManager::ConsistPool;
    using PoolPreset = PoolManager::PoolPreset;

    using PoolManager::InitializePoolPresets;
    using PoolManager::PersistPoolPresets;
    using PoolManager::GetActivePreset;
    using PoolManager::GetPresetByIndex;
    using PoolManager::GetActivePresetIndex;
    using PoolManager::SetActivePresetIndex;
    using PoolManager::CreateNewPreset;
    using PoolManager::RenameActivePreset;
    using PoolManager::CloneActivePreset;
    using PoolManager::DeleteActivePreset;
    using PoolManager::AddPoolToActivePreset;
    using PoolManager::RenamePool;
    using PoolManager::RemovePoolFromActivePreset;
    using PoolManager::MovePool;
    using PoolManager::AddUnitToPool;
    using PoolManager::RemoveUnitFromPool;
    using PoolManager::ClearPoolUnits;
    using PoolManager::CycleUnitFlipMode;
    using PoolManager::SetUnitFlipMode;
    using PoolManager::CyclePoolFlipPolicy;
    using PoolManager::PasteUnitsToPool;
}
