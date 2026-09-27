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
    std::wstring GetPoolMutationCacheFilePath()
    {
        wchar_t szExePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring exePath = szExePath;
        size_t lastSlash = exePath.find_last_of(L"\\/");
        std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";

        std::wstring appDataDir = dir + L"AppData";
        CreateDirectoryW(appDataDir.c_str(), NULL);

        return appDataDir + L"\\PoolMutation.dat";
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

    bool SaveMutationSettings(const MutatorSavedSettings& settings)
    {
        std::wstring cachePath = GetPoolMutationCacheFilePath();
        HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        uint32_t magic = 0x504D5554; // 'PMUT'
        uint32_t version = 1;
        WriteFile(hFile, &magic, sizeof(magic), &written, NULL);
        WriteFile(hFile, &version, sizeof(version), &written, NULL);

        WriteFile(hFile, &settings.activeTab, sizeof(settings.activeTab), &written, NULL);
        WriteFile(hFile, &settings.selectedPresetIdx, sizeof(settings.selectedPresetIdx), &written, NULL);

        uint32_t poolIdxCount = (uint32_t)settings.selectedPoolIndices.size();
        WriteFile(hFile, &poolIdxCount, sizeof(poolIdxCount), &written, NULL);
        for (int idx : settings.selectedPoolIndices)
            WriteFile(hFile, &idx, sizeof(idx), &written, NULL);

        uint32_t presIdxCount = (uint32_t)settings.selectedPresetIndices.size();
        WriteFile(hFile, &presIdxCount, sizeof(presIdxCount), &written, NULL);
        for (int idx : settings.selectedPresetIndices)
            WriteFile(hFile, &idx, sizeof(idx), &written, NULL);

        WriteFile(hFile, &settings.countMode, sizeof(settings.countMode), &written, NULL);
        WriteFile(hFile, &settings.customCount, sizeof(settings.customCount), &written, NULL);

        uint32_t clones = settings.createClones ? 1 : 0;
        WriteFile(hFile, &clones, sizeof(clones), &written, NULL);
        WriteWString(hFile, settings.cloneSuffix);

        WriteFile(hFile, &settings.insertSource, sizeof(settings.insertSource), &written, NULL);
        WriteFile(hFile, &settings.selectedGroupIdx, sizeof(settings.selectedGroupIdx), &written, NULL);

        uint32_t grpIdxCount = (uint32_t)settings.selectedGroupIndices.size();
        WriteFile(hFile, &grpIdxCount, sizeof(grpIdxCount), &written, NULL);
        for (int idx : settings.selectedGroupIndices)
            WriteFile(hFile, &idx, sizeof(idx), &written, NULL);

        WriteFile(hFile, &settings.insertCount, sizeof(settings.insertCount), &written, NULL);
        WriteFile(hFile, &settings.posMode, sizeof(settings.posMode), &written, NULL);
        WriteWString(hFile, settings.positionIndexText);

        CloseHandle(hFile);
        return true;
    }

    bool LoadMutationSettings(MutatorSavedSettings& settings)
    {
        std::wstring cachePath = GetPoolMutationCacheFilePath();
        HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return false;

        DWORD read = 0;
        uint32_t magic = 0;
        uint32_t version = 0;
        if (!ReadFile(hFile, &magic, sizeof(magic), &read, NULL) || magic != 0x504D5554)
        {
            CloseHandle(hFile);
            return false;
        }

        if (!ReadFile(hFile, &version, sizeof(version), &read, NULL) || version != 1)
        {
            CloseHandle(hFile);
            return false;
        }

        ReadFile(hFile, &settings.activeTab, sizeof(settings.activeTab), &read, NULL);
        ReadFile(hFile, &settings.selectedPresetIdx, sizeof(settings.selectedPresetIdx), &read, NULL);

        uint32_t poolIdxCount = 0;
        if (ReadFile(hFile, &poolIdxCount, sizeof(poolIdxCount), &read, NULL))
        {
            settings.selectedPoolIndices.clear();
            for (uint32_t i = 0; i < poolIdxCount; ++i)
            {
                int idx = 0;
                ReadFile(hFile, &idx, sizeof(idx), &read, NULL);
                settings.selectedPoolIndices.push_back(idx);
            }
        }

        uint32_t presIdxCount = 0;
        if (ReadFile(hFile, &presIdxCount, sizeof(presIdxCount), &read, NULL))
        {
            settings.selectedPresetIndices.clear();
            for (uint32_t i = 0; i < presIdxCount; ++i)
            {
                int idx = 0;
                ReadFile(hFile, &idx, sizeof(idx), &read, NULL);
                settings.selectedPresetIndices.push_back(idx);
            }
        }

        ReadFile(hFile, &settings.countMode, sizeof(settings.countMode), &read, NULL);
        ReadFile(hFile, &settings.customCount, sizeof(settings.customCount), &read, NULL);

        uint32_t clones = 0;
        ReadFile(hFile, &clones, sizeof(clones), &read, NULL);
        settings.createClones = (clones != 0);
        ReadWString(hFile, settings.cloneSuffix);

        ReadFile(hFile, &settings.insertSource, sizeof(settings.insertSource), &read, NULL);
        ReadFile(hFile, &settings.selectedGroupIdx, sizeof(settings.selectedGroupIdx), &read, NULL);

        uint32_t grpIdxCount = 0;
        if (ReadFile(hFile, &grpIdxCount, sizeof(grpIdxCount), &read, NULL))
        {
            settings.selectedGroupIndices.clear();
            for (uint32_t i = 0; i < grpIdxCount; ++i)
            {
                int idx = 0;
                ReadFile(hFile, &idx, sizeof(idx), &read, NULL);
                settings.selectedGroupIndices.push_back(idx);
            }
        }

        ReadFile(hFile, &settings.insertCount, sizeof(settings.insertCount), &read, NULL);
        ReadFile(hFile, &settings.posMode, sizeof(settings.posMode), &read, NULL);
        ReadWString(hFile, settings.positionIndexText);

        CloseHandle(hFile);
        return true;
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
            else
            {
                PoolManager::InitializePoolPresets();
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

                std::vector<PoolManager::ConsistPool> activePools;
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
}
