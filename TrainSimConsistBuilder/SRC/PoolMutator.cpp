#include "PoolMutator.h"
#include "PoolManager.h"
#include "TrainConfig.h"
#include "ConsistReader.h"
#include "ConsistWriter.h"
#include "DatabaseManager.h"
#include "AssetsParser.h"
#include <random>
#include <algorithm>
#include <map>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace PoolMutator
{
    std::wstring GetPoolMutationCacheFilePath()
    {
        return DatabaseManager::GetDatabaseFilePath();
    }

    bool SaveMutationSettings(const MutatorSavedSettings& settings)
    {
        return DatabaseManager::SaveMutationSettings(settings);
    }

    bool LoadMutationSettings(MutatorSavedSettings& settings)
    {
        return DatabaseManager::LoadMutationSettings(settings);
    }

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
        PoolManager::PoolPreset* pPreset = options.hasCustomPresetOverride ? const_cast<PoolManager::PoolPreset*>(&options.customPresetOverride) : PoolManager::GetPresetByIndex(options.presetIndex);
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
        PoolManager::PoolPreset* pPreset = options.hasCustomPresetOverride ? const_cast<PoolManager::PoolPreset*>(&options.customPresetOverride) : PoolManager::GetPresetByIndex(options.presetIndex);
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
        std::random_device rd;
        std::mt19937 rng(rd());

        auto generateBatch = [&]() -> std::vector<ConsistReader::UnitInfo> {
            std::vector<ConsistReader::UnitInfo> batch;
            if (options.insertSource == InsertSource::FavouriteGroup)
            {
                PoolManager::InitializeReplacementGroups();
                std::vector<PoolManager::PoolUnit> availableUnits;

                std::vector<int> groupIndices = options.selectedReplacementGroupIndices;
                if (groupIndices.empty())
                {
                    if (options.replacementGroupIndex >= 0 && options.replacementGroupIndex < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        groupIndices.push_back(options.replacementGroupIndex);
                    }
                    else
                    {
                        for (size_t i = 0; i < PoolManager::g_ReplacementGroupsCache.size(); ++i)
                        {
                            groupIndices.push_back((int)i);
                        }
                    }
                }

                for (int gIdx : groupIndices)
                {
                    if (gIdx >= 0 && gIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        const auto& grp = PoolManager::g_ReplacementGroupsCache[gIdx];
                        availableUnits.insert(availableUnits.end(), grp.units.begin(), grp.units.end());
                    }
                }

                if (availableUnits.empty())
                {
                    outError = L"Selected Favourite Unit Group(s) contain no units.";
                    return batch;
                }

                int countToInsert = (options.insertCount > 0) ? options.insertCount : 1;
                std::uniform_int_distribution<size_t> udist(0, availableUnits.size() - 1);
                std::uniform_int_distribution<int> fdist(0, 1);

                for (int k = 0; k < countToInsert; ++k)
                {
                    const auto& u = availableUnits[udist(rng)];
                    ConsistReader::UnitInfo ui;
                    ui.uid = u.szFileName;
                    ui.parentDir = u.szFolder;
                    ui.isEngine = u.isEngine;
                    if (u.flipMode == PoolManager::UnitFlipMode::Forward) ui.isFlipped = false;
                    else if (u.flipMode == PoolManager::UnitFlipMode::Flipped) ui.isFlipped = true;
                    else if (u.flipMode == PoolManager::UnitFlipMode::Random) ui.isFlipped = (fdist(rng) == 1);
                    else ui.isFlipped = false;
                    batch.push_back(ui);
                }
            }
            else if (options.insertSource == InsertSource::TrainBlueprint)
            {
                TrainConfigManager::ScanTrainConfigs();
                std::vector<PoolManager::ConsistPool> activePools;
                if (!options.selectedBlueprintPools.empty())
                {
                    for (const auto& bpPair : options.selectedBlueprintPools)
                    {
                        int bIdx = bpPair.first;
                        int plIdx = bpPair.second;
                        if (bIdx >= 0 && bIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                        {
                            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[bIdx];
                            TrainConfigManager::TrainBinding binding;
                            TrainConfigManager::LoadTrainBinding(cfg, binding);

                            PoolManager::PoolPreset tempPreset;
                            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, tempPreset);
                            if (plIdx >= 0 && plIdx < (int)tempPreset.pools.size())
                            {
                                if (!tempPreset.pools[plIdx].units.empty())
                                {
                                    activePools.push_back(tempPreset.pools[plIdx]);
                                }
                            }
                        }
                    }
                }
                else
                {
                    std::vector<int> blueprintIndices = options.selectedBlueprintIndices;
                    if (blueprintIndices.empty())
                    {
                        if (options.blueprintIndex >= 0 && options.blueprintIndex < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                        {
                            blueprintIndices.push_back(options.blueprintIndex);
                        }
                        else
                        {
                            for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                            {
                                blueprintIndices.push_back((int)i);
                            }
                        }
                    }

                    for (int bIdx : blueprintIndices)
                    {
                        if (bIdx >= 0 && bIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                        {
                            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[bIdx];
                            TrainConfigManager::TrainBinding binding;
                            TrainConfigManager::LoadTrainBinding(cfg, binding);

                            PoolManager::PoolPreset tempPreset;
                            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, tempPreset);
                            for (const auto& pool : tempPreset.pools)
                            {
                                if (!pool.units.empty())
                                {
                                    activePools.push_back(pool);
                                }
                            }
                        }
                    }
                }

                if (activePools.empty())
                {
                    outError = L"No valid source pools found in selected Train Blueprint(s).";
                    return batch;
                }

                if (options.insertCount > 0)
                {
                    batch = GenerateUnitsFromPools(activePools, options.insertCount, true);
                }
                else
                {
                    batch = GenerateUnitsFromPools(activePools, -1, false);
                }

                if (batch.empty())
                {
                    outError = L"No units generated from selected Train Blueprint rules.";
                    return batch;
                }
            }
            else // PoolPreset
            {
                PoolManager::InitializePoolPresets();
                std::vector<PoolManager::ConsistPool> activePools;

                if (!options.selectedPresetPools.empty())
                {
                    for (const auto& prPair : options.selectedPresetPools)
                    {
                        int pIdx = prPair.first;
                        int plIdx = prPair.second;
                        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(pIdx);
                        if (pPreset && plIdx >= 0 && plIdx < (int)pPreset->pools.size())
                        {
                            if (!pPreset->pools[plIdx].units.empty())
                            {
                                activePools.push_back(pPreset->pools[plIdx]);
                            }
                        }
                    }
                }
                else
                {
                    std::vector<int> presetIndices = options.selectedPresetIndices;
                    if (presetIndices.empty())
                    {
                        if (options.presetIndex >= 0 && options.presetIndex < (int)PoolManager::g_PoolPresetsCache.size())
                        {
                            presetIndices.push_back(options.presetIndex);
                        }
                        else
                        {
                            for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
                            {
                                presetIndices.push_back((int)i);
                            }
                        }
                    }

                    for (int pIdx : presetIndices)
                    {
                        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(pIdx);
                        if (pPreset)
                        {
                            for (const auto& pool : pPreset->pools)
                            {
                                if (!pool.units.empty())
                                {
                                    activePools.push_back(pool);
                                }
                            }
                        }
                    }
                }

                if (activePools.empty())
                {
                    outError = L"No valid source pools found in selected preset(s).";
                    return batch;
                }

                if (options.insertCount > 0)
                {
                    batch = GenerateUnitsFromPools(activePools, options.insertCount, true);
                }
                else
                {
                    batch = GenerateUnitsFromPools(activePools, -1, false);
                }

                if (batch.empty())
                {
                    outError = L"No units generated from selected pool preset rules.";
                    return batch;
                }
            }
            return batch;
        };

        // Determine target insertion positions
        if (options.posMode == PositionMode::SpecificIndex)
        {
            std::vector<int> targetPositions;
            if (!options.specificIndices.empty())
            {
                std::vector<int> sorted = options.specificIndices;
                std::sort(sorted.begin(), sorted.end(), std::less<int>());
                sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
                for (int idx : sorted)
                {
                    targetPositions.push_back(idx);
                }
            }
            else
            {
                targetPositions.push_back(options.positionIndex);
            }

            // Perform insertions in ascending slot order so target indices 2;4;6 sit at table rows 2, 4, 6
            for (int pos : targetPositions)
            {
                std::vector<ConsistReader::UnitInfo> batch = generateBatch();
                if (batch.empty())
                {
                    if (outError.empty()) outError = L"Failed to generate units to insert.";
                    return false;
                }
                int insertPos = (std::max)(0, (std::min)((int)units.size(), pos));
                units.insert(units.begin() + insertPos, batch.begin(), batch.end());
            }
        }
        else
        {
            int insertPos = (int)units.size();
            if (options.posMode == PositionMode::HeadPosition)
            {
                insertPos = 0;
            }
            else if (options.posMode == PositionMode::BehindEngines)
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
            }
            else // TailPosition
            {
                insertPos = (int)units.size();
            }

            std::vector<ConsistReader::UnitInfo> batch = generateBatch();
            if (batch.empty())
            {
                if (outError.empty()) outError = L"Failed to generate units to insert.";
                return false;
            }
            insertPos = (std::max)(0, (std::min)((int)units.size(), insertPos));
            units.insert(units.begin() + insertPos, batch.begin(), batch.end());
        }

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

    std::vector<BrokenConsistInfo> ScanConsistsForBrokenUnits(
        const std::vector<std::wstring>& consistPaths,
        const std::vector<int>& preselectedUnitIndices,
        const std::wstring& basePath)
    {
        std::vector<BrokenConsistInfo> result;
        std::wstring bp = basePath;
        if (!bp.empty() && bp.back() != L'\\' && bp.back() != L'/') bp += L'\\';

        for (const auto& path : consistPaths)
        {
            if (!PathFileExistsW(path.c_str())) continue;

            ConsistReader::ConsistData conData;
            try
            {
                conData = ConsistReader::LoadConsist(path);
            }
            catch (...)
            {
                continue;
            }

            BrokenConsistInfo bcon;
            bcon.filePath = path;

            wchar_t fname[MAX_PATH] = { 0 };
            _wsplitpath_s(path.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);
            bcon.fileName = fname;
            bcon.consistName = conData.trainCfg.name.empty() ? fname : conData.trainCfg.name;

            bool anySelected = false;
            for (size_t i = 0; i < conData.units.size(); ++i)
            {
                const auto& u = conData.units[i];
                bool isBroken = false;
                if (u.uid.empty() || u.parentDir.empty())
                {
                    isBroken = true;
                }
                else
                {
                    std::wstring ext = u.isEngine ? L".eng" : L".wag";
                    std::wstring unitPath = bp + L"TRAINS\\TRAINSET\\" + u.parentDir + L"\\" + u.uid + ext;
                    DWORD attr = GetFileAttributesW(unitPath.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        isBroken = true;
                    }
                }

                BrokenUnitInfo bu;
                bu.unitIndex = (int)i;
                bu.uid = u.uid;
                bu.parentDir = u.parentDir;
                bu.isEngine = u.isEngine;
                bu.isFlipped = u.isFlipped;
                bu.isBroken = isBroken;

                if (!preselectedUnitIndices.empty())
                {
                    bu.isSelected = (std::find(preselectedUnitIndices.begin(), preselectedUnitIndices.end(), (int)i) != preselectedUnitIndices.end());
                }
                else
                {
                    bu.isSelected = isBroken;
                }

                if (bu.isSelected) anySelected = true;
                bcon.brokenUnits.push_back(bu);
            }

            if (conData.units.empty())
            {
                bcon.isSelected = true;
                bcon.isExpanded = true;
                bcon.actionExecutionMode = 1; // Default to Rebuild Consist for empty consists
                result.push_back(std::move(bcon));
            }
            else if (!bcon.brokenUnits.empty())
            {
                bcon.isSelected = anySelected;
                bcon.isExpanded = true;
                bcon.actionExecutionMode = 0; // Replace Units default
                result.push_back(std::move(bcon));
            }
        }
        return result;
    }

    bool RepairUnitsVector(
        std::vector<ConsistReader::UnitInfo>& units,
        const std::vector<BrokenUnitInfo>& brokenUnits,
        const MutatorOptions& options,
        std::wstring& outError,
        int& outRepairedCount)
    {
        outRepairedCount = 0;
        std::random_device rd;
        std::mt19937 rng(rd());

        struct RepSourcePool {
            std::wstring name;
            PoolManager::PoolPickMode pickMode = PoolManager::PoolPickMode::Random;
            PoolManager::PoolFlipPolicy flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;
            std::vector<PoolManager::PoolUnit> engineUnits;
            std::vector<PoolManager::PoolUnit> wagonUnits;
            std::vector<PoolManager::PoolUnit> allUnits;
            size_t engineSeq = 0;
            size_t wagonSeq = 0;
        };

        struct RepSourcePalette {
            int sourceId = 0;
            std::vector<RepSourcePool> pools;
            size_t nextPoolIdx = 0;
        };

        std::vector<RepSourcePalette> palettes;

        if (!options.sourceNodes.empty())
        {
            for (const auto& node : options.sourceNodes)
            {
                RepSourcePalette pal;
                pal.sourceId = node.sourceId;

                if (node.sourceType == 0) // Favourite Group
                {
                    PoolManager::InitializeReplacementGroups();
                    std::vector<int> groupIndices = node.selectedGroupIndices;
                    if (groupIndices.empty())
                    {
                        if (node.groupIndex >= 0 && node.groupIndex < (int)PoolManager::g_ReplacementGroupsCache.size())
                        {
                            groupIndices.push_back(node.groupIndex);
                        }
                        else
                        {
                            for (size_t i = 0; i < PoolManager::g_ReplacementGroupsCache.size(); ++i)
                                groupIndices.push_back((int)i);
                        }
                    }

                    for (int gIdx : groupIndices)
                    {
                        if (gIdx >= 0 && gIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                        {
                            const auto& grp = PoolManager::g_ReplacementGroupsCache[gIdx];
                            if (grp.units.empty()) continue;

                            RepSourcePool sp;
                            sp.name = grp.name;
                            sp.pickMode = grp.pickMode;
                            sp.flipPolicy = grp.flipPolicy;
                            for (const auto& u : grp.units)
                            {
                                sp.allUnits.push_back(u);
                                if (u.isEngine) sp.engineUnits.push_back(u);
                                else sp.wagonUnits.push_back(u);
                            }
                            pal.pools.push_back(std::move(sp));
                        }
                    }
                }
                else if (node.sourceType == 2) // Train Config / Blueprint
                {
                    TrainConfigManager::ScanTrainConfigs();
                    if (!node.selectedBlueprintPools.empty())
                    {
                        for (const auto& bpPair : node.selectedBlueprintPools)
                        {
                            int bIdx = bpPair.first;
                            int plIdx = bpPair.second;
                            if (bIdx >= 0 && bIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                            {
                                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[bIdx];
                                TrainConfigManager::TrainBinding binding;
                                TrainConfigManager::LoadTrainBinding(cfg, binding);

                                PoolManager::PoolPreset tempPreset;
                                TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, tempPreset);
                                if (plIdx >= 0 && plIdx < (int)tempPreset.pools.size())
                                {
                                    const auto& cp = tempPreset.pools[plIdx];
                                    if (cp.units.empty()) continue;

                                    RepSourcePool sp;
                                    sp.name = cp.name;
                                    sp.pickMode = cp.pickMode;
                                    sp.flipPolicy = cp.flipPolicy;
                                    for (const auto& u : cp.units)
                                    {
                                        sp.allUnits.push_back(u);
                                        if (u.isEngine) sp.engineUnits.push_back(u);
                                        else sp.wagonUnits.push_back(u);
                                    }
                                    pal.pools.push_back(std::move(sp));
                                }
                            }
                        }
                    }
                    else
                    {
                        std::vector<int> blueprintIndices = node.selectedBlueprintIndices;
                        if (blueprintIndices.empty())
                        {
                            if (node.blueprintIndex >= 0 && node.blueprintIndex < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                            {
                                blueprintIndices.push_back(node.blueprintIndex);
                            }
                            else
                            {
                                for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                                    blueprintIndices.push_back((int)i);
                            }
                        }

                        for (int bIdx : blueprintIndices)
                        {
                            if (bIdx >= 0 && bIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                            {
                                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[bIdx];
                                TrainConfigManager::TrainBinding binding;
                                TrainConfigManager::LoadTrainBinding(cfg, binding);

                                PoolManager::PoolPreset tempPreset;
                                TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, tempPreset);
                                for (const auto& cp : tempPreset.pools)
                                {
                                    if (cp.units.empty()) continue;

                                    RepSourcePool sp;
                                    sp.name = cp.name;
                                    sp.pickMode = cp.pickMode;
                                    sp.flipPolicy = cp.flipPolicy;
                                    for (const auto& u : cp.units)
                                    {
                                        sp.allUnits.push_back(u);
                                        if (u.isEngine) sp.engineUnits.push_back(u);
                                        else sp.wagonUnits.push_back(u);
                                    }
                                    pal.pools.push_back(std::move(sp));
                                }
                            }
                        }
                    }
                }
                else // Consist Pool Preset
                {
                    PoolManager::InitializePoolPresets();
                    if (!node.selectedPresetPools.empty())
                    {
                        for (const auto& prPair : node.selectedPresetPools)
                        {
                            int pIdx = prPair.first;
                            int plIdx = prPair.second;
                            if (pIdx >= 0 && pIdx < (int)PoolManager::g_PoolPresetsCache.size())
                            {
                                const auto& preset = PoolManager::g_PoolPresetsCache[pIdx];
                                if (plIdx >= 0 && plIdx < (int)preset.pools.size())
                                {
                                    const auto& cp = preset.pools[plIdx];
                                    if (cp.units.empty()) continue;

                                    RepSourcePool sp;
                                    sp.name = cp.name;
                                    sp.pickMode = cp.pickMode;
                                    sp.flipPolicy = cp.flipPolicy;
                                    for (const auto& u : cp.units)
                                    {
                                        sp.allUnits.push_back(u);
                                        if (u.isEngine) sp.engineUnits.push_back(u);
                                        else sp.wagonUnits.push_back(u);
                                    }
                                    pal.pools.push_back(std::move(sp));
                                }
                            }
                        }
                    }
                    else
                    {
                        std::vector<int> presetIndices = node.selectedPresetIndices;
                        if (presetIndices.empty())
                        {
                            if (node.presetIndex >= 0 && node.presetIndex < (int)PoolManager::g_PoolPresetsCache.size())
                            {
                                presetIndices.push_back(node.presetIndex);
                            }
                            else
                            {
                                for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
                                    presetIndices.push_back((int)i);
                            }
                        }

                        for (int pIdx : presetIndices)
                        {
                            if (pIdx >= 0 && pIdx < (int)PoolManager::g_PoolPresetsCache.size())
                            {
                                const auto& preset = PoolManager::g_PoolPresetsCache[pIdx];
                                for (const auto& cp : preset.pools)
                                {
                                    if (cp.units.empty()) continue;

                                    RepSourcePool sp;
                                    sp.name = cp.name;
                                    sp.pickMode = cp.pickMode;
                                    sp.flipPolicy = cp.flipPolicy;
                                    for (const auto& u : cp.units)
                                    {
                                        sp.allUnits.push_back(u);
                                        if (u.isEngine) sp.engineUnits.push_back(u);
                                        else sp.wagonUnits.push_back(u);
                                    }
                                    pal.pools.push_back(std::move(sp));
                                }
                            }
                        }
                    }
                }

                palettes.push_back(std::move(pal));
            }
        }

        // Check if any palette has available units
        bool hasAnyUnits = false;
        for (const auto& pal : palettes)
        {
            if (!pal.pools.empty())
            {
                for (const auto& pl : pal.pools)
                {
                    if (!pl.allUnits.empty())
                    {
                        hasAnyUnits = true;
                        break;
                    }
                }
            }
            if (hasAnyUnits) break;
        }

        if (!hasAnyUnits)
        {
            outError = L"No available replacement rolling stock found in selected Source Node(s). Please ensure valid rolling stock exists in the selected groups, presets, or train configs.";
            return false;
        }

        bool modified = false;

        for (const auto& bu : brokenUnits)
        {
            if (!bu.isSelected) continue;
            if (bu.unitIndex < 0 || bu.unitIndex >= (int)units.size()) continue;

            // Find palette matching bu.assignedSourceId
            RepSourcePalette* pTargetPal = nullptr;
            for (auto& pal : palettes)
            {
                if (pal.sourceId == bu.assignedSourceId && !pal.pools.empty())
                {
                    pTargetPal = &pal;
                    break;
                }
            }

            // Fallback to first non-empty palette
            if (!pTargetPal)
            {
                for (auto& pal : palettes)
                {
                    if (!pal.pools.empty())
                    {
                        pTargetPal = &pal;
                        break;
                    }
                }
            }

            if (!pTargetPal || pTargetPal->pools.empty()) continue;

            // Find a pool in this palette matching the vehicle type (engine vs wagon)
            RepSourcePool* pTargetPool = nullptr;
            size_t numPools = pTargetPal->pools.size();

            for (size_t k = 0; k < numPools; ++k)
            {
                size_t pIdx = (pTargetPal->nextPoolIdx + k) % numPools;
                auto& pl = pTargetPal->pools[pIdx];
                if (bu.isEngine && !pl.engineUnits.empty())
                {
                    pTargetPool = &pl;
                    pTargetPal->nextPoolIdx = (pIdx + 1) % numPools;
                    break;
                }
                else if (!bu.isEngine && !pl.wagonUnits.empty())
                {
                    pTargetPool = &pl;
                    pTargetPal->nextPoolIdx = (pIdx + 1) % numPools;
                    break;
                }
            }

            // Fallback to any pool in palette with units
            if (!pTargetPool)
            {
                for (size_t k = 0; k < numPools; ++k)
                {
                    size_t pIdx = (pTargetPal->nextPoolIdx + k) % numPools;
                    auto& pl = pTargetPal->pools[pIdx];
                    if (!pl.allUnits.empty())
                    {
                        pTargetPool = &pl;
                        pTargetPal->nextPoolIdx = (pIdx + 1) % numPools;
                        break;
                    }
                }
            }

            if (!pTargetPool) continue;

            // Select candidate units for this pool
            const std::vector<PoolManager::PoolUnit>* pCandidateUnits = nullptr;
            if (bu.isEngine && !pTargetPool->engineUnits.empty())
            {
                pCandidateUnits = &pTargetPool->engineUnits;
            }
            else if (!bu.isEngine && !pTargetPool->wagonUnits.empty())
            {
                pCandidateUnits = &pTargetPool->wagonUnits;
            }
            else
            {
                pCandidateUnits = &pTargetPool->allUnits;
            }

            if (!pCandidateUnits || pCandidateUnits->empty()) continue;

            PoolManager::PoolUnit rep;
            if (pTargetPool->pickMode == PoolManager::PoolPickMode::Sequential)
            {
                size_t& seq = bu.isEngine ? pTargetPool->engineSeq : pTargetPool->wagonSeq;
                rep = (*pCandidateUnits)[seq % pCandidateUnits->size()];
                seq++;
            }
            else // Random
            {
                std::uniform_int_distribution<size_t> udist(0, pCandidateUnits->size() - 1);
                rep = (*pCandidateUnits)[udist(rng)];
            }

            // Determine flip orientation strictly from unit flipMode and pool flipPolicy
            bool isFlipped = false;
            if (rep.flipMode == PoolManager::UnitFlipMode::Forward)
            {
                isFlipped = false;
            }
            else if (rep.flipMode == PoolManager::UnitFlipMode::Flipped)
            {
                isFlipped = true;
            }
            else if (rep.flipMode == PoolManager::UnitFlipMode::Random)
            {
                std::uniform_int_distribution<int> fdist(0, 1);
                isFlipped = (fdist(rng) == 1);
            }
            else // Auto -> follow pool / group flipPolicy
            {
                if (pTargetPool->flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                {
                    isFlipped = true;
                }
                else if (pTargetPool->flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                {
                    std::uniform_int_distribution<int> fdist(0, 1);
                    isFlipped = (fdist(rng) == 1);
                }
                else
                {
                    isFlipped = false;
                }
            }

            units[bu.unitIndex].uid = rep.szFileName;
            units[bu.unitIndex].parentDir = rep.szFolder;
            units[bu.unitIndex].isEngine = rep.isEngine;
            units[bu.unitIndex].isFlipped = isFlipped;

            modified = true;
            outRepairedCount++;
        }

        return modified;
    }

    bool GenerateFullConsistFromSourceNode(
        int sourceId,
        const MutatorOptions& options,
        std::vector<ConsistReader::UnitInfo>& outUnits,
        std::wstring& outError,
        double* pOutMaxSpeed,
        double* pOutPerfFactor)
    {
        outUnits.clear();
        outError.clear();

        const SourceNodeConfig* pTargetNode = nullptr;
        for (const auto& node : options.sourceNodes)
        {
            if (node.sourceId == sourceId)
            {
                pTargetNode = &node;
                break;
            }
        }
        if (!pTargetNode && !options.sourceNodes.empty())
        {
            pTargetNode = &options.sourceNodes[0];
        }

        if (!pTargetNode)
        {
            outError = L"No valid source node found for consist generation.";
            return false;
        }

        std::random_device rd;
        std::mt19937 rng(rd());

        if (pTargetNode->sourceType == 0) // Favourite Group
        {
            outError = L"Favourite Groups do not define train formation sequences or pool composition rules. Please select a Train Config or Pool Preset to rebuild consists from scratch.";
            return false;
        }
        else if (pTargetNode->sourceType == 2) // Train Config / Blueprint
        {
            TrainConfigManager::ScanTrainConfigs();
            if (TrainConfigManager::g_LoadedConfigsCache.empty())
            {
                outError = L"No Train Configs (.train) available in library.";
                return false;
            }

            int bIdx = 0;
            if (!pTargetNode->selectedBlueprintIndices.empty())
            {
                bIdx = pTargetNode->selectedBlueprintIndices[0];
            }
            else if (pTargetNode->blueprintIndex >= 0 && pTargetNode->blueprintIndex < (int)TrainConfigManager::g_LoadedConfigsCache.size())
            {
                bIdx = pTargetNode->blueprintIndex;
            }

            if (bIdx < 0 || bIdx >= (int)TrainConfigManager::g_LoadedConfigsCache.size())
            {
                bIdx = 0;
            }

            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[bIdx];
            if (pOutMaxSpeed && cfg.maxSpeedKmph > 0.0) *pOutMaxSpeed = cfg.maxSpeedKmph;
            if (pOutPerfFactor && cfg.perfFactor > 0.0) *pOutPerfFactor = cfg.perfFactor;

            TrainConfigManager::TrainBinding binding;
            TrainConfigManager::LoadTrainBinding(cfg, binding);

            struct MatchedPoolEntry {
                TrainConfigManager::TrainBlueprintPool blueprintPool;
                TrainConfigManager::TrainBindingPool bindingPool;
                size_t poolIndex = 0;
                size_t seqPickIdx = 0;
            };

            std::vector<MatchedPoolEntry> matchedPools;
            for (size_t p = 0; p < cfg.pools.size(); ++p)
            {
                MatchedPoolEntry mpe;
                mpe.blueprintPool = cfg.pools[p];
                mpe.poolIndex = p;
                mpe.seqPickIdx = 0;

                if (p < binding.pools.size() && _wcsicmp(binding.pools[p].poolName.c_str(), cfg.pools[p].poolName.c_str()) == 0)
                {
                    mpe.bindingPool = binding.pools[p];
                }
                else
                {
                    // Fallback search
                    for (const auto& bp : binding.pools)
                    {
                        if (_wcsicmp(bp.poolName.c_str(), cfg.pools[p].poolName.c_str()) == 0)
                        {
                            mpe.bindingPool = bp;
                            break;
                        }
                    }
                }
                matchedPools.push_back(mpe);
            }

            auto PickFromBoundPool = [&](MatchedPoolEntry& entry, int countToPick)
            {
                const auto& bpPool = entry.bindingPool;
                if (bpPool.units.empty() || countToPick <= 0) return;
                for (int c = 0; c < countToPick; ++c)
                {
                    const PoolManager::PoolUnit* pUnit = nullptr;
                    if (bpPool.pickMode == PoolManager::PoolPickMode::Sequential)
                    {
                        pUnit = &bpPool.units[entry.seqPickIdx % bpPool.units.size()];
                        entry.seqPickIdx++;
                    }
                    else
                    {
                        std::uniform_int_distribution<size_t> udist(0, bpPool.units.size() - 1);
                        pUnit = &bpPool.units[udist(rng)];
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
                        if (bpPool.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                            isFlipped = true;
                        else if (bpPool.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
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
                    outUnits.push_back(u);
                }
            };

            if (!cfg.sequence.empty())
            {
                // Generate units strictly following the ordered composition sequence
                std::map<std::wstring, size_t> poolOccurrences;

                for (const auto& poolName : cfg.sequence)
                {
                    std::wstring lName = poolName;
                    std::transform(lName.begin(), lName.end(), lName.begin(), ::towlower);
                    size_t targetOcc = poolOccurrences[lName]++;

                    int matchedIdx = -1;
                    size_t currentOcc = 0;
                    for (size_t p = 0; p < matchedPools.size(); ++p)
                    {
                        if (_wcsicmp(matchedPools[p].blueprintPool.poolName.c_str(), poolName.c_str()) == 0)
                        {
                            if (currentOcc == targetOcc)
                            {
                                matchedIdx = (int)p;
                                break;
                            }
                            currentOcc++;
                        }
                    }

                    // If sequence repeats pool name more times than distinct pool definitions, wrap around
                    if (matchedIdx == -1)
                    {
                        for (size_t p = 0; p < matchedPools.size(); ++p)
                        {
                            if (_wcsicmp(matchedPools[p].blueprintPool.poolName.c_str(), poolName.c_str()) == 0)
                            {
                                matchedIdx = (int)p;
                                break;
                            }
                        }
                    }

                    if (matchedIdx >= 0 && !matchedPools[matchedIdx].bindingPool.units.empty())
                    {
                        auto& entry = matchedPools[matchedIdx];
                        int minC = (std::max)(1, entry.blueprintPool.minCount);
                        int maxC = (std::max)(minC, entry.blueprintPool.maxCount);
                        int count = minC;
                        if (minC < maxC)
                        {
                            std::uniform_int_distribution<int> dist(minC, maxC);
                            count = dist(rng);
                        }
                        PickFromBoundPool(entry, count);
                    }
                }
            }
            else
            {
                // Sequence is empty: generate each defined pool in order
                for (size_t p = 0; p < matchedPools.size(); ++p)
                {
                    auto& entry = matchedPools[p];
                    if (!entry.bindingPool.units.empty())
                    {
                        int minC = (std::max)(0, entry.blueprintPool.minCount);
                        int maxC = (std::max)(minC, entry.blueprintPool.maxCount);
                        int count = minC;
                        if (minC < maxC)
                        {
                            std::uniform_int_distribution<int> dist(minC, maxC);
                            count = dist(rng);
                        }
                        PickFromBoundPool(entry, count);
                    }
                }
            }

            if (outUnits.empty())
            {
                outError = L"No rolling stock units could be generated from Train Config '" + cfg.name + L"'. Please verify rolling stock bindings in Train Config Studio.";
                return false;
            }
            return true;
        }
        else // Pool Preset
        {
            PoolManager::InitializePoolPresets();
            if (PoolManager::g_PoolPresetsCache.empty())
            {
                outError = L"No Pool Presets available in library.";
                return false;
            }

            int pIdx = 0;
            if (!pTargetNode->selectedPresetIndices.empty())
            {
                pIdx = pTargetNode->selectedPresetIndices[0];
            }
            else if (pTargetNode->presetIndex >= 0 && pTargetNode->presetIndex < (int)PoolManager::g_PoolPresetsCache.size())
            {
                pIdx = pTargetNode->presetIndex;
            }

            if (pIdx < 0 || pIdx >= (int)PoolManager::g_PoolPresetsCache.size())
            {
                pIdx = 0;
            }

            const auto& preset = PoolManager::g_PoolPresetsCache[pIdx];
            for (const auto& pool : preset.pools)
            {
                if (pool.units.empty()) continue;
                int minC = (std::max)(0, pool.minCount);
                int maxC = (std::max)(minC, pool.maxCount);
                int count = minC;
                if (minC < maxC)
                {
                    std::uniform_int_distribution<int> dist(minC, maxC);
                    count = dist(rng);
                }
                if (count <= 0) continue;

                auto poolBatch = GenerateUnitsFromSinglePool(pool, count);
                outUnits.insert(outUnits.end(), poolBatch.begin(), poolBatch.end());
            }

            if (outUnits.empty())
            {
                outError = L"No rolling stock units could be generated from Pool Preset '" + preset.presetName + L"'. Please verify pool stock units in Pool Manager.";
                return false;
            }
            return true;
        }
    }

    MutatorResult RepairBrokenConsists(
        const std::vector<BrokenConsistInfo>& brokenConsists,
        const MutatorOptions& options,
        const std::wstring& basePath)
    {
        MutatorResult result;
        int totalUnitsRepaired = 0;

        for (const auto& bcon : brokenConsists)
        {
            if (!bcon.isSelected) continue;
            if (bcon.actionExecutionMode == 0 && bcon.brokenUnits.empty()) continue;
            if (!PathFileExistsW(bcon.filePath.c_str())) continue;

            ConsistReader::ConsistData conData;
            try
            {
                conData = ConsistReader::LoadConsist(bcon.filePath);
            }
            catch (...)
            {
                continue;
            }

            std::wstring err;
            int repCount = 0;

            if (bcon.actionExecutionMode == 1) // Rebuild Consist from scratch
            {
                std::vector<ConsistReader::UnitInfo> rebuiltUnits;
                double maxVel = (conData.trainCfg.maxVelocity > 0.0) ? conData.trainCfg.maxVelocity : 120.0;
                double perfFactor = (conData.trainCfg.perfFactor > 0.0) ? conData.trainCfg.perfFactor : 1.0;

                if (GenerateFullConsistFromSourceNode(bcon.assignedSourceId, options, rebuiltUnits, err, &maxVel, &perfFactor))
                {
                    conData.units = rebuiltUnits;
                    conData.trainCfg.maxVelocity = maxVel;
                    conData.trainCfg.perfFactor = perfFactor;
                    repCount = (int)rebuiltUnits.size();
                    totalUnitsRepaired += repCount;

                    if (options.createClones)
                    {
                        std::wstring bakPath = bcon.filePath + L".bak";
                        CopyFileW(bcon.filePath.c_str(), bakPath.c_str(), FALSE);
                    }

                    if (ConsistWriter::SaveConsist(bcon.filePath, conData.trainCfg.trainCfgId, conData.trainCfg.name, maxVel, perfFactor, conData.units))
                    {
                        result.affectedFiles.push_back(bcon.filePath);
                        result.processedCount++;
                    }
                }
                else
                {
                    result.errorMessage = err;
                }
            }
            else // In-Place Unit Replacement
            {
                if (RepairUnitsVector(conData.units, bcon.brokenUnits, options, err, repCount))
                {
                    totalUnitsRepaired += repCount;
                    if (options.createClones)
                    {
                        std::wstring bakPath = bcon.filePath + L".bak";
                        CopyFileW(bcon.filePath.c_str(), bakPath.c_str(), FALSE);
                    }

                    double maxVel = (conData.trainCfg.maxVelocity > 0.0) ? conData.trainCfg.maxVelocity : 120.0;
                    double perfFactor = (conData.trainCfg.perfFactor > 0.0) ? conData.trainCfg.perfFactor : 1.0;

                    if (ConsistWriter::SaveConsist(bcon.filePath, conData.trainCfg.trainCfgId, conData.trainCfg.name, maxVel, perfFactor, conData.units))
                    {
                        result.affectedFiles.push_back(bcon.filePath);
                        result.processedCount++;
                    }
                }
            }
        }

        result.repairedUnitsCount = totalUnitsRepaired;
        result.success = (result.processedCount > 0);
        if (!result.success && result.errorMessage.empty())
        {
            result.errorMessage = L"No broken units or consists were processed.";
        }
        return result;
    }
}

