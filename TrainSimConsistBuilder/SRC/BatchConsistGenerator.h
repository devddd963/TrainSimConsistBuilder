#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "PoolManager.h"
#include "ConsistReader.h"

namespace BatchConsistGenerator
{
    struct GeneratedConsistSpec {
        std::wstring fileName;
        std::wstring displayName;
        int targetTotalUnits = 0; // 0 = Dynamic Pool-Sum, >0 = Fixed exact total count
    };

    std::wstring ToAlphaIndex(int index);
    std::wstring ToRomanNumeral(int number);
    std::wstring FormatSerializedName(const std::wstring& baseName, int index, int totalCount, int styleIndex, const std::wstring& separator);
    
    bool BatchGenerateConsists(const PoolManager::PoolPreset& preset,
                               const std::vector<GeneratedConsistSpec>& specs,
                               const std::wstring& targetDir,
                               bool overwrite,
                               std::vector<std::wstring>& outCreatedFiles,
                               std::wstring& outError);
}

// Backward compatibility namespace
namespace BatchConsistWizard
{
    using GeneratedConsistSpec = BatchConsistGenerator::GeneratedConsistSpec;
    using BatchConsistGenerator::ToAlphaIndex;
    using BatchConsistGenerator::ToRomanNumeral;
    using BatchConsistGenerator::FormatSerializedName;
    using BatchConsistGenerator::BatchGenerateConsists;
}
