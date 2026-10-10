#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include "PoolManager.h"

namespace TrainConfigManager
{
    // Blueprint Pool Definition in .train file
    struct TrainBlueprintPool {
        std::wstring poolName;        // e.g. "DrivingCab_Front", "CC_Block1", "LeadLoco"
        std::wstring role;            // e.g. "LeadCab", "ChairCar", "ExecutiveChairCar", "LeadLoco"
        std::wstring unitType;        // e.g. "Electric_EMU", "Passenger", "Electric / Diesel"
        int minCount = 1;             // Min count to pick
        int maxCount = 1;             // Max count to pick
        std::wstring filterTag;       // Comma-separated tags (e.g. "VB_DTC, VB_DrivingCab")
        double probability = 1.0;     // 1.0 = 100%
    };

    // Train Blueprint Configuration (.train)
    struct TrainConfig {
        std::wstring filePath;        // Full path on disk
        std::wstring relativePath;    // Relative to TrainConfigs/ (e.g. "Vande_Bharat\20901_Vande_Bharat_20Car.train")
        std::wstring category;        // Folder category (e.g. "Vande_Bharat", "Express_LHB", "Freight")
        std::wstring id;              // Unique blueprint identifier (e.g. "IR_VB_20901_20CAR")
        std::wstring name;            // Display Name
        std::wstring trainType;       // "EMU", "LocomotiveHauled", "Freight", etc.
        std::wstring description;     // Description
        double maxSpeedKmph = 0.0;    // Max velocity in km/h (e.g. 160.0, 130.0)
        double perfFactor = 1.0;      // Performance factor % (e.g. 1.0, 0.1, 0.001)
        int minLength = 0;            // Min total units
        int maxLength = 0;            // Max total units
        std::vector<std::wstring> sequence;           // Ordered sequence of pool names
        std::vector<TrainBlueprintPool> pools;        // Blueprint pool rules
    };

    // Local Machine Binding Pool (.bindings)
    struct TrainBindingPool {
        std::wstring poolName;
        PoolManager::PoolPickMode pickMode = PoolManager::PoolPickMode::Random;
        PoolManager::PoolFlipPolicy flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;
        std::vector<PoolManager::PoolUnit> units;
    };

    // Local Machine Binding File (.bindings)
    struct TrainBinding {
        std::wstring trainID;
        std::wstring configName;
        std::wstring lastModified;
        std::vector<TrainBindingPool> pools;
    };

    // Core Directory Paths
    std::wstring GetTrainConfigsRootDir();
    std::wstring GetTrainBindingsRootDir();
    std::wstring SanitizeFileName(const std::wstring& name);
    std::wstring SanitizeID(const std::wstring& id);

    // Default Seed Blueprints
    void EnsureDefaultTrainConfigs();

    // Scanning & Discovery
    std::vector<TrainConfig> ScanTrainConfigs();
    std::vector<std::wstring> GetCategories();

    // File IO (.train)
    bool LoadTrainConfig(const std::wstring& filePath, TrainConfig& outConfig);
    bool SaveTrainConfig(const std::wstring& filePath, const TrainConfig& config);

    // File IO (.bindings)
    std::wstring GetBindingFilePathForConfig(const TrainConfig& config);
    bool LoadTrainBinding(const TrainConfig& config, TrainBinding& outBinding);
    bool SaveTrainBinding(const TrainConfig& config, const TrainBinding& binding);

    // Bridge between TrainConfig + TrainBinding <---> PoolManager::PoolPreset
    bool BuildPresetFromConfigAndBinding(const TrainConfig& config, const TrainBinding& binding, PoolManager::PoolPreset& outPreset);
    bool ExtractBindingFromPreset(const TrainConfig& config, const PoolManager::PoolPreset& preset, TrainBinding& outBinding);

    // Active Config State
    extern std::vector<TrainConfig> g_LoadedConfigsCache;
    extern int g_ActiveTrainConfigIndex;

    void InitializeTrainConfigs();
    TrainConfig* GetActiveTrainConfig();
    bool SetActiveTrainConfigByIndex(int index);
}
