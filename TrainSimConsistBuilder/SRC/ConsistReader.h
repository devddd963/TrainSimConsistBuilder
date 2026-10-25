#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <cwchar>

namespace ConsistReader {
    struct TrainConfig {
        std::wstring trainCfgId;    // consist identifier token e.g. "NewConsist"
        std::wstring name;          // display name e.g. "INTERCITY EXP"
        double      maxVelocity = 0.0;  // km/h (converted from m/s on load)
        double      perfFactor  = 0.0;  // percentage (raw fraction × 100)

        bool operator==(const TrainConfig& o) const {
            return trainCfgId == o.trainCfgId &&
                   name == o.name &&
                   std::fabs(maxVelocity - o.maxVelocity) < 1e-4 &&
                   std::fabs(perfFactor - o.perfFactor) < 1e-4;
        }
        bool operator!=(const TrainConfig& o) const { return !(*this == o); }
    };

    struct EngineInfo {
        std::wstring uid;        // engine file name without extension
        std::wstring parentDir; // directory (second quoted string)
        bool         isFlipped = false;
    };

    struct WagonInfo {
        std::wstring uid;        // wagon file name without extension
        std::wstring parentDir; // directory (second quoted string)
        bool         isFlipped = false;
    };

    struct UnitInfo {
        std::wstring uid;
        std::wstring parentDir;
        bool         isEngine = false;
        bool         isFlipped = false;

        bool operator==(const UnitInfo& o) const {
            return isEngine == o.isEngine &&
                   isFlipped == o.isFlipped &&
                   _wcsicmp(uid.c_str(), o.uid.c_str()) == 0 &&
                   _wcsicmp(parentDir.c_str(), o.parentDir.c_str()) == 0;
        }
        bool operator!=(const UnitInfo& o) const { return !(*this == o); }
    };

    struct ConsistData {
        std::wstring                 fileName;    // base name without extension
        TrainConfig                  trainCfg;
        std::vector<EngineInfo>      engines;    // order of appearance
        std::vector<WagonInfo>       wagons;     // order of appearance
        std::vector<UnitInfo>        units;      // combined order of appearance
        int                          totalUnits = 0; // same count as existing logic
    };

    /**
     * Parse a .con file and return a populated ConsistData structure.
     * Throws std::runtime_error on fatal parsing errors.
     */
    ConsistData LoadConsist(const std::wstring& fullPath);
}

// Helper functions that are used internally and may be unit‑tested.
std::string ReadConFileToAscii(const std::wstring& path);
int         CountCarsInAscii(const std::string& ascii);
std::wstring ExtractConsistName(const std::string& ascii);
