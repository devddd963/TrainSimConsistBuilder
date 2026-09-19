#include "PoolMutator.h"
#include "PoolManager.h"
#include "ConsistReader.h"
#include "ConsistWriter.h"
#include <random>
#include <algorithm>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace PoolMutator
{
    static std::wstring GetMutatedOutputPath(const std::wstring& originalPath, bool createClone, const std::wstring& cloneSuffix)
    {
        if (!createClone) return originalPath;

        wchar_t dir[MAX_PATH] = { 0 };
        wchar_t fname[MAX_PATH] = { 0 };
        wchar_t ext[MAX_PATH] = { 0 };
        wchar_t drive[MAX_PATH] = { 0 };
        _wsplitpath_s(originalPath.c_str(), drive, MAX_PATH, dir, MAX_PATH, fname, MAX_PATH, ext, MAX_PATH);

        std::wstring baseDir = std::wstring(drive) + std::wstring(dir);
        std::wstring baseFileName = fname;
        std::wstring suffix = cloneSuffix.empty() ? L"_PoolVar" : cloneSuffix;

        std::wstring candidate = baseDir + baseFileName + suffix + L".con";
        int counter = 1;
        while (PathFileExistsW(candidate.c_str()))
        {
            candidate = baseDir + baseFileName + suffix + L"_" + std::to_wstring(counter++) + L".con";
        }
        return candidate;
    }

    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromSinglePool(
        const PoolManager::ConsistPool& pool,
        int countToPick)
    {
        std::vector<ConsistReader::UnitInfo> result;
        if (pool.units.empty() || countToPick <= 0) return result;

        std::random_device rd;
        std::mt19937 rng(rd());

        for (int c = 0; c < countToPick; ++c)
        {
            const PoolManager::PoolUnit* pUnit = nullptr;
            if (pool.pickMode == PoolManager::PoolPickMode::Sequential)
            {
                pUnit = &pool.units[c % pool.units.size()];
            }
            else
            {
                std::uniform_int_distribution<size_t> udist(0, pool.units.size() - 1);
                pUnit = &pool.units[udist(rng)];
            }
            if (!pUnit) continue;

            bool isFlipped = false;
            switch (pUnit->flipMode)
            {
            case PoolManager::UnitFlipMode::Forward:
                isFlipped = false;
                break;
            case PoolManager::UnitFlipMode::Flipped:
                isFlipped = true;
                break;
            case PoolManager::UnitFlipMode::Random:
            {
                std::uniform_int_distribution<int> fdist(0, 1);
                isFlipped = (fdist(rng) == 1);
                break;
            }
            case PoolManager::UnitFlipMode::Auto:
            default:
            {
                if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                    isFlipped = true;
                else if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                {
                    std::uniform_int_distribution<int> fdist(0, 1);
                    isFlipped = (fdist(rng) == 1);
                }
                else
                    isFlipped = false;
                break;
            }
            }

            ConsistReader::UnitInfo u;
            u.uid = pUnit->szFileName;
            u.parentDir = pUnit->szFolder;
            u.isEngine = pUnit->isEngine;
            u.isFlipped = isFlipped;
            result.push_back(u);
        }
        return result;
    }

    static std::vector<PoolManager::ConsistPool> GetActivePoolsFromPreset(
        const PoolManager::PoolPreset& preset,
        const MutatorOptions& options)
    {
        std::vector<PoolManager::ConsistPool> result;
        if (preset.pools.empty()) return result;

        // If specific poolIndex >= 0 was given (legacy single pool) and selectedPoolIndices is empty
        if (options.poolIndex >= 0 && options.selectedPoolIndices.empty())
        {
            if (options.poolIndex < (int)preset.pools.size())
            {
                result.push_back(preset.pools[options.poolIndex]);
            }
            return result;
        }

        // If selectedPoolIndices is non-empty, sort and pick matching pools
        if (!options.selectedPoolIndices.empty())
        {
            std::vector<int> sortedIndices = options.selectedPoolIndices;
            std::sort(sortedIndices.begin(), sortedIndices.end());
            sortedIndices.erase(std::unique(sortedIndices.begin(), sortedIndices.end()), sortedIndices.end());

            for (int idx : sortedIndices)
            {
                if (idx >= 0 && idx < (int)preset.pools.size())
                {
                    result.push_back(preset.pools[idx]);
                }
            }
            return result;
        }

        // Otherwise (All Pools / Entire Preset): return all pools in preset
        return preset.pools;
    }

    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromPools(
        const std::vector<PoolManager::ConsistPool>& activePools,
        int targetUnitCount,
        bool isFixedTarget)
    {
        std::vector<ConsistReader::UnitInfo> result;
        if (activePools.empty()) return result;

        if (activePools.size() == 1)
        {
            const auto& singlePool = activePools[0];
            int countToPick = targetUnitCount;
            if (!isFixedTarget || countToPick <= 0)
            {
                std::random_device rd;
                std::mt19937 rng(rd());
                int minC = (std::max)(0, singlePool.minCount);
                int maxC = (std::max)(minC, singlePool.maxCount);
                std::uniform_int_distribution<int> dist(minC, maxC);
                countToPick = dist(rng);
            }
            return GenerateUnitsFromSinglePool(singlePool, countToPick);
        }

        std::random_device rd;
        std::mt19937 rng(rd());

        std::vector<int> poolPicks(activePools.size(), 0);
        for (size_t p = 0; p < activePools.size(); ++p)
        {
            const auto& pool = activePools[p];
            if (pool.units.empty()) continue;

            int minC = (std::max)(0, pool.minCount);
            int maxC = (std::max)(minC, pool.maxCount);
            if (minC == maxC)
                poolPicks[p] = minC;
            else
            {
                std::uniform_int_distribution<int> dist(minC, maxC);
                poolPicks[p] = dist(rng);
            }
        }

        if (isFixedTarget && targetUnitCount > 0)
        {
            int currentTotal = 0;
            for (int c : poolPicks) currentTotal += c;

            if (currentTotal > targetUnitCount)
            {
                int excess = currentTotal - targetUnitCount;
                for (int iter = 0; iter < excess; ++iter)
                {
                    int bestPoolIdx = -1;
                    int bestCount = 0;
                    for (size_t p = 0; p < activePools.size(); ++p)
                    {
                        if (poolPicks[p] > activePools[p].minCount && poolPicks[p] > bestCount)
                        {
                            bestCount = poolPicks[p];
                            bestPoolIdx = (int)p;
                        }
                    }
                    if (bestPoolIdx == -1)
                    {
                        for (size_t p = 0; p < activePools.size(); ++p)
                        {
                            if (poolPicks[p] > 0 && poolPicks[p] > bestCount)
                            {
                                bestCount = poolPicks[p];
                                bestPoolIdx = (int)p;
                            }
                        }
                    }
                    if (bestPoolIdx != -1)
                    {
                        poolPicks[bestPoolIdx]--;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else if (currentTotal < targetUnitCount)
            {
                int needed = targetUnitCount - currentTotal;
                int largestPoolIdx = 0;
                int largestCap = 0;
                for (size_t p = 0; p < activePools.size(); ++p)
                {
                    if (!activePools[p].units.empty() && activePools[p].maxCount >= largestCap)
                    {
                        largestCap = activePools[p].maxCount;
                        largestPoolIdx = (int)p;
                    }
                }
                if (!activePools.empty() && !activePools[largestPoolIdx].units.empty())
                {
                    poolPicks[largestPoolIdx] += needed;
                }
            }
        }

        for (size_t p = 0; p < activePools.size(); ++p)
        {
            const auto& pool = activePools[p];
            if (pool.units.empty() || poolPicks[p] <= 0) continue;

            auto unitsFromPool = GenerateUnitsFromSinglePool(pool, poolPicks[p]);
            result.insert(result.end(), unitsFromPool.begin(), unitsFromPool.end());
        }

        return result;
    }

    std::vector<ConsistReader::UnitInfo> GenerateUnitsFromPreset(
        const PoolManager::PoolPreset& preset,
        int targetUnitCount,
        bool isFixedTarget)
    {
        return GenerateUnitsFromPools(preset.pools, targetUnitCount, isFixedTarget);
    }

    bool MutateUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const MutatorOptions& options,
        std::wstring& outError)
    {
        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(options.presetIndex);
        if (!pPreset)
        {
            outError = L"Invalid Pool Preset selected.";
            return false;
        }

        std::vector<PoolManager::ConsistPool> activePools = GetActivePoolsFromPreset(*pPreset, options);
        if (activePools.empty())
        {
            outError = L"No valid source pools selected in preset.";
            return false;
        }

        int targetCount = (int)units.size();
        bool isFixed = true;
        if (options.countMode == CountMode::KeepOriginalCount)
        {
            targetCount = (int)units.size();
            if (targetCount <= 0) targetCount = 10;
            isFixed = true;
        }
        else if (options.countMode == CountMode::CustomUnitCount)
        {
            targetCount = (std::max)(1, options.customCount);
            isFixed = true;
        }
        else if (options.countMode == CountMode::DynamicPoolRules)
        {
            targetCount = -1;
            isFixed = false;
        }

        std::vector<ConsistReader::UnitInfo> generatedUnits = GenerateUnitsFromPools(activePools, targetCount, isFixed);

        if (generatedUnits.empty())
        {
            outError = L"No units could be generated from the selected pool/preset.";
            return false;
        }

        units = generatedUnits;
        return true;
    }

    bool ReplaceUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const std::vector<int>& selectedUnitIndices,
        const MutatorOptions& options,
        std::wstring& outError)
    {
        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(options.presetIndex);
        if (!pPreset)
        {
            outError = L"Invalid Pool Preset selected.";
            return false;
        }

        std::vector<PoolManager::ConsistPool> activePools = GetActivePoolsFromPreset(*pPreset, options);
        if (activePools.empty())
        {
            outError = L"No valid source pools selected in preset.";
            return false;
        }

        std::vector<int> targetIndices = selectedUnitIndices;
        if (targetIndices.empty())
        {
            for (size_t i = 0; i < units.size(); ++i) targetIndices.push_back((int)i);
        }

        if (targetIndices.empty())
        {
            outError = L"Consist contains no units to replace.";
            return false;
        }

        int countNeeded = (int)targetIndices.size();
        std::vector<ConsistReader::UnitInfo> replacementUnits = GenerateUnitsFromPools(activePools, countNeeded, true);

        if (replacementUnits.empty())
        {
            outError = L"No replacement units could be generated from the selected pool/preset.";
            return false;
        }

        for (size_t i = 0; i < targetIndices.size(); ++i)
        {
            int unitIdx = targetIndices[i];
            if (unitIdx >= 0 && unitIdx < (int)units.size())
            {
                bool preserveFlip = units[unitIdx].isFlipped;
                units[unitIdx] = replacementUnits[i % replacementUnits.size()];
                units[unitIdx].isFlipped = preserveFlip;
            }
        }
        return true;
    }

    bool InsertUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const MutatorOptions& options,
        std::wstring& outError)
    {
        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(options.presetIndex);
        if (!pPreset)
        {
            outError = L"Invalid Pool Preset selected.";
            return false;
        }

        std::vector<PoolManager::ConsistPool> activePools = GetActivePoolsFromPreset(*pPreset, options);
        if (activePools.empty())
        {
            outError = L"No valid source pools selected in preset.";
            return false;
        }

        int countToInsert = (options.insertCount > 0) ? options.insertCount : 1;
        std::vector<ConsistReader::UnitInfo> unitsToInsert = GenerateUnitsFromPools(activePools, countToInsert, true);

        if (unitsToInsert.empty())
        {
            outError = L"No units to insert generated from selected pool/preset.";
            return false;
        }

        int insertPos = (int)units.size();
        switch (options.posMode)
        {
        case PositionMode::HeadPosition:
            insertPos = 0;
            break;
        case PositionMode::BehindEngines:
        {
            insertPos = (int)units.size();
            for (size_t i = 0; i < units.size(); ++i)
            {
                if (!units[i].isEngine)
                {
                    insertPos = (int)i;
                    break;
                }
            }
            break;
        }
        case PositionMode::SpecificIndex:
            insertPos = (std::max)(0, (std::min)((int)units.size(), options.positionIndex));
            break;
        case PositionMode::TailPosition:
        default:
            insertPos = (int)units.size();
            break;
        }

        units.insert(units.begin() + insertPos, unitsToInsert.begin(), unitsToInsert.end());
        return true;
    }

    MutatorResult MutateConsistFiles(
        const std::vector<std::wstring>& consistFilePaths,
        const MutatorOptions& options,
        const std::wstring& basePath)
    {
        MutatorResult result;
        for (const auto& path : consistFilePaths)
        {
            if (!PathFileExistsW(path.c_str())) continue;

            ConsistReader::ConsistData consist;
            try
            {
                consist = ConsistReader::LoadConsist(path);
            }
            catch (...)
            {
                continue;
            }

            std::wstring err;
            if (!MutateUnitsVector(consist.units, options, err))
            {
                result.errorMessage = err;
                continue;
            }

            std::wstring outPath = GetMutatedOutputPath(path, options.createClones, options.cloneSuffix);
            wchar_t fname[MAX_PATH] = { 0 };
            _wsplitpath_s(outPath.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);

            std::wstring trainCfgId = fname;
            std::wstring displayName = consist.trainCfg.name;
            if (options.createClones)
            {
                displayName += L" " + (options.cloneSuffix.empty() ? L"_PoolVar" : options.cloneSuffix);
            }

            double maxVel = (consist.trainCfg.maxVelocity > 0.0) ? consist.trainCfg.maxVelocity : 120.0;
            double perfFactor = (consist.trainCfg.perfFactor > 0.0) ? consist.trainCfg.perfFactor : 1.0;

            if (ConsistWriter::SaveConsist(outPath, trainCfgId, displayName, maxVel, perfFactor, consist.units))
            {
                result.affectedFiles.push_back(outPath);
                result.processedCount++;
            }
        }

        result.success = (result.processedCount > 0);
        if (!result.success && result.errorMessage.empty())
        {
            result.errorMessage = L"No consists were mutated (check file write permissions or empty pools).";
        }
        return result;
    }

    MutatorResult ReplaceUnitsInConsist(
        const std::wstring& consistFilePath,
        const std::vector<int>& selectedUnitIndices,
        const MutatorOptions& options,
        const std::wstring& basePath)
    {
        MutatorResult result;
        if (!PathFileExistsW(consistFilePath.c_str()))
        {
            result.errorMessage = L"Consist file not found:\n" + consistFilePath;
            return result;
        }

        ConsistReader::ConsistData consist;
        try
        {
            consist = ConsistReader::LoadConsist(consistFilePath);
        }
        catch (...)
        {
            result.errorMessage = L"Failed to load and parse consist file.";
            return result;
        }

        std::wstring err;
        if (!ReplaceUnitsVector(consist.units, selectedUnitIndices, options, err))
        {
            result.errorMessage = err;
            return result;
        }

        std::wstring outPath = GetMutatedOutputPath(consistFilePath, options.createClones, options.cloneSuffix);
        wchar_t fname[MAX_PATH] = { 0 };
        _wsplitpath_s(outPath.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);

        std::wstring trainCfgId = fname;
        std::wstring displayName = consist.trainCfg.name;
        if (options.createClones)
        {
            displayName += L" " + (options.cloneSuffix.empty() ? L"_PoolVar" : options.cloneSuffix);
        }

        double maxVel = (consist.trainCfg.maxVelocity > 0.0) ? consist.trainCfg.maxVelocity : 120.0;
        double perfFactor = (consist.trainCfg.perfFactor > 0.0) ? consist.trainCfg.perfFactor : 1.0;

        if (ConsistWriter::SaveConsist(outPath, trainCfgId, displayName, maxVel, perfFactor, consist.units))
        {
            result.affectedFiles.push_back(outPath);
            result.processedCount = 1;
            result.success = true;
        }
        else
        {
            result.errorMessage = L"Failed to write modified consist to disk.";
        }

        return result;
    }

    MutatorResult InsertUnitsIntoConsists(
        const std::vector<std::wstring>& consistFilePaths,
        const MutatorOptions& options,
        const std::wstring& basePath)
    {
        MutatorResult result;
        for (const auto& path : consistFilePaths)
        {
            if (!PathFileExistsW(path.c_str())) continue;

            ConsistReader::ConsistData consist;
            try
            {
                consist = ConsistReader::LoadConsist(path);
            }
            catch (...)
            {
                continue;
            }

            std::wstring err;
            if (!InsertUnitsVector(consist.units, options, err))
            {
                result.errorMessage = err;
                continue;
            }

            std::wstring outPath = GetMutatedOutputPath(path, options.createClones, options.cloneSuffix);
            wchar_t fname[MAX_PATH] = { 0 };
            _wsplitpath_s(outPath.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);

            std::wstring trainCfgId = fname;
            std::wstring displayName = consist.trainCfg.name;
            if (options.createClones)
            {
                displayName += L" " + (options.cloneSuffix.empty() ? L"_PoolVar" : options.cloneSuffix);
            }

            double maxVel = (consist.trainCfg.maxVelocity > 0.0) ? consist.trainCfg.maxVelocity : 120.0;
            double perfFactor = (consist.trainCfg.perfFactor > 0.0) ? consist.trainCfg.perfFactor : 1.0;

            if (ConsistWriter::SaveConsist(outPath, trainCfgId, displayName, maxVel, perfFactor, consist.units))
            {
                result.affectedFiles.push_back(outPath);
                result.processedCount++;
            }
        }

        result.success = (result.processedCount > 0);
        if (!result.success && result.errorMessage.empty())
        {
            result.errorMessage = L"No consists were updated.";
        }
        return result;
    }
}
