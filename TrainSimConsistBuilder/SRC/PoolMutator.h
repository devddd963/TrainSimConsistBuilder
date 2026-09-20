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
        ReplaceSelected = 1,   // Replace selected unit(s) within a consist
        InsertUnits = 2        // Insert unit(s) at specific position across consist(s)
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

    struct MutatorOptions {
        MutatorMode mode = MutatorMode::MutateConsists;
        int presetIndex = 0;
        int poolIndex = -1; // -1 = Entire Preset (All Pools), >= 0 = Specific Pool (legacy)
        std::vector<int> selectedPoolIndices; // Empty = All pools in preset; otherwise sorted subset of pool indices
        CountMode countMode = CountMode::KeepOriginalCount;
        int customCount = 20;
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        PositionMode posMode = PositionMode::TailPosition;
        int positionIndex = 0;
        int insertCount = 2;

        bool IsAllPoolsSelected(size_t totalPoolsInPreset) const
        {
            return (selectedPoolIndices.empty() || selectedPoolIndices.size() >= totalPoolsInPreset);
        }
    };

    struct MutatorResult {
        bool success = false;
        int processedCount = 0;
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
