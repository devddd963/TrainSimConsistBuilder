#include "BatchConsistGenerator.h"
#include "ConsistWriter.h"
#include <random>
#include <algorithm>
#include <shlwapi.h>

namespace BatchConsistGenerator
{
    std::wstring ToAlphaIndex(int index)
    {
        if (index < 1) return L"A";
        std::wstring res;
        int n = index;
        while (n > 0)
        {
            int rem = (n - 1) % 26;
            res = (wchar_t)(L'A' + rem) + res;
            n = (n - 1) / 26;
        }
        return res;
    }

    std::wstring ToRomanNumeral(int number)
    {
        if (number <= 0) return std::to_wstring(number);
        struct RomanDigit { int val; const wchar_t* numeral; };
        static const RomanDigit table[] = {
            { 1000, L"M" }, { 900, L"CM" }, { 500, L"D" }, { 400, L"CD" },
            { 100, L"C" },  { 90, L"XC" },  { 50, L"L" },   { 40, L"XL" },
            { 10, L"X" },   { 9, L"IX" },   { 5, L"V" },    { 4, L"IV" },
            { 1, L"I" }
        };

        std::wstring result;
        int rem = number;
        for (const auto& entry : table)
        {
            while (rem >= entry.val)
            {
                result += entry.numeral;
                rem -= entry.val;
            }
        }
        return result;
    }

    std::wstring FormatSerializedName(const std::wstring& baseName, int index, int totalCount, int styleIndex, const std::wstring& separator)
    {
        std::wstring suffix;
        switch (styleIndex)
        {
        case 0: // Simple numeric: 1, 2, 3...
            suffix = std::to_wstring(index);
            break;

        case 1: // Zero-padded numeric: 01, 02...
        {
            int padWidth = (totalCount >= 100) ? 3 : 2;
            std::wstring raw = std::to_wstring(index);
            while ((int)raw.length() < padWidth)
            {
                raw = L"0" + raw;
            }
            suffix = raw;
            break;
        }

        case 2: // Alphabetical: A, B, C...
            suffix = ToAlphaIndex(index);
            break;

        case 3: // Roman numerals: I, II, III...
            suffix = ToRomanNumeral(index);
            break;

        case 4: // Timestamp: YYYYMMDD_HHMMSS
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t tbuf[64] = { 0 };
            swprintf_s(tbuf, L"%04d%02d%02d_%02d%02d%02d_%02d",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, index);
            suffix = tbuf;
            break;
        }

        default:
            suffix = std::to_wstring(index);
            break;
        }

        if (baseName.empty())
            return suffix;

        return baseName + separator + suffix;
    }

    bool BatchGenerateConsists(const PoolManager::PoolPreset& preset,
                               const std::vector<GeneratedConsistSpec>& specs,
                               const std::wstring& targetDir,
                               bool overwrite,
                               std::vector<std::wstring>& outCreatedFiles,
                               std::wstring& outError)
    {
        outCreatedFiles.clear();
        outError.clear();

        if (preset.pools.empty())
        {
            outError = L"Preset contains no pools.";
            return false;
        }

        if (specs.empty())
        {
            outError = L"No consist specifications provided.";
            return false;
        }

        if (targetDir.empty() || !PathFileExistsW(targetDir.c_str()))
        {
            outError = L"Target directory does not exist: " + targetDir;
            return false;
        }

        std::random_device rd;
        std::mt19937 rng(rd());

        for (const auto& spec : specs)
        {
            if (spec.fileName.empty()) continue;

            std::wstring safeFileName = spec.fileName;
            for (auto& ch : safeFileName)
            {
                if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|')
                {
                    ch = L'_';
                }
            }

            std::wstring outPath = targetDir;
            if (!outPath.empty() && outPath.back() != L'\\') outPath += L'\\';
            outPath += safeFileName;
            if (outPath.length() < 4 || _wcsicmp(outPath.substr(outPath.length() - 4).c_str(), L".con") != 0)
            {
                outPath += L".con";
            }

            if (!overwrite && PathFileExistsW(outPath.c_str()))
            {
                continue; // Skip existing files if overwrite is false
            }

            // Consist generation logic
            std::vector<ConsistReader::UnitInfo> consistUnits;

            // Phase 1: Determine target pool counts
            std::vector<int> poolPicks(preset.pools.size(), 0);
            for (size_t p = 0; p < preset.pools.size(); ++p)
            {
                const auto& pool = preset.pools[p];
                if (pool.units.empty()) continue;

                int minC = (std::max)(0, pool.minCount);
                int maxC = (std::max)(minC, pool.maxCount);
                if (minC == maxC)
                {
                    poolPicks[p] = minC;
                }
                else
                {
                    std::uniform_int_distribution<int> dist(minC, maxC);
                    poolPicks[p] = dist(rng);
                }
            }

            // Phase 2: If fixed total units target is specified (> 0), adjust pool picks
            if (spec.targetTotalUnits > 0)
            {
                int currentTotal = 0;
                for (int c : poolPicks) currentTotal += c;

                int targetUnits = spec.targetTotalUnits;

                if (currentTotal > targetUnits)
                {
                    // Scale down non-empty pools proportionally
                    int excess = currentTotal - targetUnits;
                    for (int iter = 0; iter < excess; ++iter)
                    {
                        int bestPoolIdx = -1;
                        int bestCount = 0;
                        for (size_t p = 0; p < preset.pools.size(); ++p)
                        {
                            if (poolPicks[p] > preset.pools[p].minCount && poolPicks[p] > bestCount)
                            {
                                bestCount = poolPicks[p];
                                bestPoolIdx = (int)p;
                            }
                        }
                        if (bestPoolIdx == -1)
                        {
                            for (size_t p = 0; p < preset.pools.size(); ++p)
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
                else if (currentTotal < targetUnits)
                {
                    // Scale up pools (preferring largest capacity / coach pools)
                    int needed = targetUnits - currentTotal;
                    int largestPoolIdx = 0;
                    int largestCap = 0;
                    for (size_t p = 0; p < preset.pools.size(); ++p)
                    {
                        if (!preset.pools[p].units.empty() && preset.pools[p].maxCount >= largestCap)
                        {
                            largestCap = preset.pools[p].maxCount;
                            largestPoolIdx = (int)p;
                        }
                    }
                    if (!preset.pools.empty() && !preset.pools[largestPoolIdx].units.empty())
                    {
                        poolPicks[largestPoolIdx] += needed;
                    }
                }
            }

            // Phase 3: Pick units from each pool
            for (size_t p = 0; p < preset.pools.size(); ++p)
            {
                const auto& pool = preset.pools[p];
                if (pool.units.empty()) continue;

                int countToPick = poolPicks[p];
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

                    // Determine orientation / flip state
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
                        {
                            isFlipped = true;
                        }
                        else if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                        {
                            std::uniform_int_distribution<int> fdist(0, 1);
                            isFlipped = (fdist(rng) == 1);
                        }
                        else
                        {
                            isFlipped = false;
                        }
                        break;
                    }
                    }

                    ConsistReader::UnitInfo cu;
                    cu.uid = pUnit->szFileName;
                    cu.parentDir = pUnit->szFolder;
                    cu.isEngine = pUnit->isEngine;
                    cu.isFlipped = isFlipped;
                    consistUnits.push_back(cu);
                }
            }

            if (consistUnits.empty()) continue;

            // Write .con file
            std::wstring displayName = spec.displayName.empty() ? safeFileName : spec.displayName;
            if (ConsistWriter::SaveConsist(outPath, safeFileName, displayName, 120.0, 1.0, consistUnits))
            {
                outCreatedFiles.push_back(outPath);
            }
        }

        if (outCreatedFiles.empty() && !specs.empty())
        {
            if (outError.empty()) outError = L"No consist files could be created (check permissions or overwrite options).";
            return false;
        }

        return true;
    }
}
