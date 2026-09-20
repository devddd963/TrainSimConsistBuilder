#pragma once
#include <string>
#include <vector>
#include "ConsistReader.h"

namespace ConsistWriter
{
    bool SaveConsist(const std::wstring& fullPath,
                     const std::wstring& trainCfgId,
                     const std::wstring& name,
                     double maxVelocityKmh,
                     double perfFactorPct,
                     const std::vector<ConsistReader::UnitInfo>& units);
}
