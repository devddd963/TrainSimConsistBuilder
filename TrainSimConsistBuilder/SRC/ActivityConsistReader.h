#pragma once

#include <string>
#include <vector>
#include "ConsistReader.h"

namespace ActivityConsistReader {

    struct ActivityConsist {
        std::wstring id;            // Object ID (e.g. "32768" or "Loose_1")
        std::wstring name;          // Display Name / Description
        std::wstring serviceName;   // Object info
        std::wstring sourceTypeStr = L"Loose Consist";
        int objectIndex = 0;        // 0-based sequential index of the ActivityObject
        ConsistReader::TrainConfig trainCfg;
        std::vector<ConsistReader::UnitInfo> units;
        int totalUnits = 0;
        bool isBroken = false;
        bool isDirty = false;
    };

    struct ActivityData {
        std::wstring filePath;
        std::wstring routeFolder;
        std::wstring fileName;
        std::vector<ActivityConsist> consists;
    };

    /**
     * Parse an MSTS / Open Rails .act file and return all embedded loose consists
     * from the ActivityObjects block.
     */
    ActivityData LoadActivityConsists(const std::wstring& actFilePath, const std::wstring& basePath);
}
