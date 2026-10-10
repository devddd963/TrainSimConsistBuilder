#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "PoolManager.h"
#include "ConsistReader.h"

namespace PoolMutator
{
    enum class MutatorMode {
        MutateConsists = 0,    // Regenerate / Reskin entire consist(s)
        InsertUnits = 1,       // Insert unit(s) at specific position across consist(s)
        ReplaceBroken = 2,     // Replace broken/missing units across selected consist(s)
        ReplaceSelected = 2    // Legacy/Selection mode alias
    };

    enum class CountMode {
        KeepOriginalCount = 0,   // Keep exact same number of units as original
        DynamicPoolRules = 1,    // Dynamic based on pool min/max rules
        CustomUnitCount = 2      // User-specified exact number of units
    };

    enum class PositionMode {
        HeadPosition = 0,        // Position 0 (Before current lead engines)
        BehindEngines = 1,       // After lead locomotives (first wagon index)
        SpecificIndex = 2,       // At explicit 0-based position index
        TailPosition = 3         // At the very end of the consist
    };

    enum class InsertSource {
        FavouriteGroup = 0,   // Insert units from a Replacement Group (Favourite Batch)
        PoolPreset = 1,       // Insert units generated from a Pool Preset
        TrainBlueprint = 2,   // Insert units generated from a Train Blueprint (.train)
        IntelligentMatch = 3  // Intelligent Category Matching (All library stock)
    };

    struct SourceNodeConfig {
        int sourceId = 0;                          // 0 = Source A, 1 = Source B, 2 = Source C...
        std::wstring name = L"Source A";
        int sourceType = 0;                         // 0 = Favourite Group, 1 = Consist Pool Preset, 2 = Train Blueprint
        int groupIndex = 0;                         // Selected replacement group index
        int presetIndex = 0;                        // Selected pool preset index
        int blueprintIndex = 0;                     // Selected train blueprint index
        std::vector<int> selectedGroupIndices;
        std::vector<int> selectedPresetIndices;
        std::vector<int> selectedBlueprintIndices;
        std::vector<std::pair<int, int>> selectedPresetPools;    // (presetIdx, poolIdx)
        std::vector<std::pair<int, int>> selectedBlueprintPools; // (blueprintIdx, poolIdx)
        COLORREF color = RGB(56, 189, 248);
    };

    struct BrokenUnitInfo {
        int unitIndex = 0;
        std::wstring uid;
        std::wstring parentDir;
        bool isEngine = false;
        bool isFlipped = false;
        bool isBroken = false;
        std::wstring category;
        bool isSelected = false;
        int assignedSourceId = 0; // 0 = Source A, 1 = Source B, etc.
    };

    struct BrokenConsistInfo {
        std::wstring filePath;
        std::wstring fileName;
        std::wstring consistName;
        std::vector<BrokenUnitInfo> brokenUnits; // all units in consist
        bool isExpanded = true;
        bool isSelected = false;
        int assignedSourceId = 0; // default for consist-level assignment
        int actionExecutionMode = 0; // 0 = In-Place Unit Replacement ("Replace Units"), 1 = Rebuild Consist from scratch ("Rebuild Consist")
    };

    struct MutatorOptions {
        MutatorMode mode = MutatorMode::MutateConsists;
        int presetIndex = 0;
        int poolIndex = -1; // -1 = Entire Preset (All Pools), >= 0 = Specific Pool (legacy)
        std::vector<int> selectedPoolIndices; // For Tab 0: subset of pools in preset. Empty = All pools
        std::vector<int> selectedPresetIndices; // For Tab 1/2: subset of presets in library. Empty = All presets
        std::vector<int> selectedBlueprintIndices; // For Tab 1/2: subset of train blueprints. Empty = All blueprints
        std::vector<std::pair<int, int>> selectedPresetPools;    // For Tab 1/2: (presetIdx, poolIdx)
        std::vector<std::pair<int, int>> selectedBlueprintPools; // For Tab 1/2: (blueprintIdx, poolIdx)
        CountMode countMode = CountMode::KeepOriginalCount;
        int customCount = 20;
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        PositionMode posMode = PositionMode::TailPosition;
        int positionIndex = 0;
        std::vector<int> specificIndices; // 0-based indices parsed from semicolon/comma input
        int insertCount = 2; // Mandatory for groups; optional (0 = dynamic pool rules) for presets
        InsertSource insertSource = InsertSource::FavouriteGroup;
        int replacementGroupIndex = 0;
        int blueprintIndex = 0;
        std::vector<int> selectedReplacementGroupIndices; // For Tab 1/2: subset of favourite groups. Empty = All groups
        bool preserveOrientation = true;
        bool randomizeReplacements = true;
        bool hasCustomPresetOverride = false;
        PoolManager::PoolPreset customPresetOverride;
        std::vector<SourceNodeConfig> sourceNodes; // Multi-source routing configurations for Tab 2

        bool IsAllPoolsSelected(size_t totalPoolsInPreset) const
        {
            return (selectedPoolIndices.empty() || selectedPoolIndices.size() >= totalPoolsInPreset);
        }
    };

    struct MutatorResult {
        bool success = false;
        int processedCount = 0;
        int repairedUnitsCount = 0;
        std::vector<std::wstring> affectedFiles;
        std::wstring errorMessage;
    };

    // Engine operations
    MutatorResult MutateConsistFiles(
        const std::vector<std::wstring>& consistFilePaths,
        const MutatorOptions& options,
        const std::wstring& basePath
    );

    MutatorResult ReplaceUnitsInConsist(
        const std::wstring& consistFilePath,
        const std::vector<int>& selectedUnitIndices,
        const MutatorOptions& options,
        const std::wstring& basePath
    );

    MutatorResult InsertUnitsIntoConsists(
        const std::vector<std::wstring>& consistFilePaths,
        const MutatorOptions& options,
        const std::wstring& basePath
    );

    std::vector<BrokenConsistInfo> ScanConsistsForBrokenUnits(
        const std::vector<std::wstring>& consistPaths,
        const std::vector<int>& preselectedUnitIndices,
        const std::wstring& basePath
    );

    MutatorResult RepairBrokenConsists(
        const std::vector<BrokenConsistInfo>& brokenConsists,
        const MutatorOptions& options,
        const std::wstring& basePath
    );

    bool GenerateFullConsistFromSourceNode(
        int sourceId,
        const MutatorOptions& options,
        std::vector<ConsistReader::UnitInfo>& outUnits,
        std::wstring& outError,
        double* pOutMaxSpeed = nullptr,
        double* pOutPerfFactor = nullptr
    );

    // Vector transformation operations (in-memory)
    bool MutateUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const MutatorOptions& options,
        std::wstring& outError
    );

    bool ReplaceUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const std::vector<int>& selectedUnitIndices,
        const MutatorOptions& options,
        std::wstring& outError
    );

    bool InsertUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const MutatorOptions& options,
        std::wstring& outError
    );

    bool RepairUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const std::vector<BrokenUnitInfo>& brokenUnits,
        const MutatorOptions& options,
        std::wstring& outError,
        int& outRepairedCount
    );

    // Persistence of user configuration
    struct MutatorSavedSettings {
        int activeTab = 0; // 0 = Mutate Consist, 1 = Insert Units
        int selectedPresetIdx = 0;
        std::vector<int> selectedPoolIndices;
        std::vector<int> selectedPresetIndices;
        std::vector<std::pair<int, int>> selectedPresetPools;
        std::vector<std::pair<int, int>> selectedBlueprintPools;
        int countMode = 0; // 0 = KeepOriginal, 1 = Dynamic, 2 = Custom
        int customCount = 20;
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        int insertSource = 0; // 0 = Favourite Group, 1 = Pool Preset, 2 = Train Blueprint
        int selectedGroupIdx = 0;
        std::vector<int> selectedGroupIndices;
        int selectedBlueprintIdx = 0;
        std::vector<int> selectedBlueprintIndices;
        int insertCount = 2;
        int posMode = 3; // 0 = Head, 1 = BehindEngines, 2 = Specific, 3 = Tail
        std::wstring positionIndexText = L"1";
    };

    std::wstring GetPoolMutationCacheFilePath();
    bool SaveMutationSettings(const MutatorSavedSettings& settings);
    bool LoadMutationSettings(MutatorSavedSettings& settings);

    // Helpers for generating units from a pool preset, subset of pools, or specific pool
    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromPreset(
        const PoolManager::PoolPreset& preset,
        int targetUnitCount,
        bool isFixedTarget
    );

    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromPools(
        const std::vector<PoolManager::ConsistPool>& activePools,
        int targetUnitCount,
        bool isFixedTarget
    );

    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromSinglePool(
        const PoolManager::ConsistPool& pool,
        int countToPick
    );
}
