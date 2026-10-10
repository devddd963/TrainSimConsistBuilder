#include "TrainConfig.h"
#include "AppLogging.h"
#include "DatabaseManager.h"
#include "AssetsParser.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace TrainConfigManager
{
    std::vector<TrainConfig> g_LoadedConfigsCache;
    int g_ActiveTrainConfigIndex = -1;

    // String Utilities
    static std::wstring Trim(const std::wstring& s)
    {
        size_t start = s.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) return L"";
        size_t end = s.find_last_not_of(L" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string TrimA(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::wstring ToLowerW(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    static std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim)
    {
        std::vector<std::wstring> result;
        std::wstringstream ss(s);
        std::wstring item;
        while (std::getline(ss, item, delim))
        {
            std::wstring trimmed = Trim(item);
            if (!trimmed.empty())
            {
                result.push_back(trimmed);
            }
        }
        return result;
    }

    static std::wstring Utf8ToWide(const std::string& str)
    {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    static std::string WideToUtf8(const std::wstring& wstr)
    {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    std::wstring GetTrainConfigsRootDir()
    {
        return DatabaseManager::GetAppDirectory() + L"TrainConfigs";
    }

    std::wstring GetTrainBindingsRootDir()
    {
        return DatabaseManager::GetAppDirectory() + L"AppData\\TrainBindings";
    }

    std::wstring SanitizeFileName(const std::wstring& name)
    {
        std::wstring result = name;
        for (auto& ch : result)
        {
            if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|')
            {
                ch = L'_';
            }
        }
        return result;
    }

    std::wstring SanitizeID(const std::wstring& id)
    {
        std::wstring result = id;
        for (auto& ch : result)
        {
            if (ch == L' ' || ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'\t' || ch == L'\r' || ch == L'\n')
            {
                ch = L'_';
            }
        }
        return result;
    }

    std::wstring GetBindingFilePathForConfig(const TrainConfig& config)
    {
        if (!config.filePath.empty())
        {
            try
            {
                fs::path p(config.filePath);
                return (p.parent_path() / (p.stem().wstring() + L".bindings")).wstring();
            }
            catch (...) {}
        }

        std::wstring root = GetTrainConfigsRootDir();
        std::wstring cat = !config.category.empty() ? SanitizeFileName(config.category) : L"General";
        std::wstring name = !config.name.empty() ? SanitizeFileName(config.name) : (!config.id.empty() ? SanitizeID(config.id) : L"UnnamedTrain");
        return (fs::path(root) / cat / name / (name + L".bindings")).wstring();
    }

    void EnsureDefaultTrainConfigs()
    {
        try
        {
            std::wstring root = GetTrainConfigsRootDir();
            if (!fs::exists(root))
            {
                fs::create_directories(root);
            }
        }
        catch (...)
        {
        }
    }

    std::vector<TrainConfig> ScanTrainConfigs()
    {
        std::vector<TrainConfig> configs;
        std::wstring root = GetTrainConfigsRootDir();
        EnsureDefaultTrainConfigs();

        try
        {
            if (fs::exists(root))
            {
                for (const auto& entry : fs::recursive_directory_iterator(root))
                {
                    if (entry.is_regular_file())
                    {
                        std::wstring ext = entry.path().extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                        if (ext == L".train" || ext == L".config")
                        {
                            TrainConfig cfg;
                            if (LoadTrainConfig(entry.path().wstring(), cfg))
                            {
                                cfg.filePath = entry.path().wstring();
                                try
                                {
                                    cfg.relativePath = fs::relative(entry.path(), root).wstring();
                                    fs::path relP(cfg.relativePath);
                                    std::vector<std::wstring> parts;
                                    for (const auto& p : relP)
                                    {
                                        parts.push_back(p.wstring());
                                    }

                                    if (parts.size() >= 3)
                                    {
                                        // Category / TrainFolder / File.train
                                        cfg.category = parts[0];
                                    }
                                    else if (parts.size() == 2)
                                    {
                                        // Category / File.train (legacy)
                                        cfg.category = parts[0];
                                    }
                                    else
                                    {
                                        cfg.category = L"General";
                                    }
                                }
                                catch (...)
                                {
                                    cfg.relativePath = entry.path().filename().wstring();
                                    cfg.category = L"General";
                                }

                                configs.push_back(cfg);
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }

        std::sort(configs.begin(), configs.end(), [](const TrainConfig& a, const TrainConfig& b) {
            if (a.category != b.category) return a.category < b.category;
            return a.name < b.name;
        });

        g_LoadedConfigsCache = configs;
        return configs;
    }

    std::vector<std::wstring> GetCategories()
    {
        std::vector<std::wstring> cats;
        std::wstring root = GetTrainConfigsRootDir();
        try
        {
            if (fs::exists(root))
            {
                for (const auto& entry : fs::directory_iterator(root))
                {
                    if (entry.is_directory())
                    {
                        std::wstring dirName = entry.path().filename().wstring();
                        if (!dirName.empty() && std::find(cats.begin(), cats.end(), dirName) == cats.end())
                        {
                            cats.push_back(dirName);
                        }
                    }
                }
            }
        }
        catch (...) {}

        for (const auto& cfg : g_LoadedConfigsCache)
        {
            if (!cfg.category.empty() && std::find(cats.begin(), cats.end(), cfg.category) == cats.end())
            {
                cats.push_back(cfg.category);
            }
        }

        std::sort(cats.begin(), cats.end());
        return cats;
    }

    bool LoadTrainConfig(const std::wstring& filePath, TrainConfig& outConfig)
    {
        outConfig = TrainConfig();
        outConfig.filePath = filePath;

        std::ifstream ifs(filePath);
        if (!ifs.is_open()) return false;

        std::string line;
        std::string currentSection = "";
        std::vector<TrainBlueprintPool> parsedPools;
        int currentPoolIdx = -1;

        while (std::getline(ifs, line))
        {
            std::string trimmed = TrimA(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

            if (trimmed.front() == '[' && trimmed.back() == ']')
            {
                currentSection = trimmed.substr(1, trimmed.length() - 2);
                currentSection = TrimA(currentSection);
                std::string lSec = currentSection;
                std::transform(lSec.begin(), lSec.end(), lSec.begin(), ::tolower);

                if (lSec.rfind("pool:", 0) == 0)
                {
                    std::wstring poolName = Utf8ToWide(currentSection.substr(5));
                    poolName = Trim(poolName);
                    TrainBlueprintPool bp;
                    bp.poolName = poolName.empty() ? L"Pool" : poolName;
                    parsedPools.push_back(bp);
                    currentPoolIdx = (int)parsedPools.size() - 1;
                }
                else
                {
                    currentPoolIdx = -1;
                }
                continue;
            }

            size_t eqPos = trimmed.find('=');
            if (eqPos == std::string::npos) continue;

            std::wstring key = Trim(Utf8ToWide(trimmed.substr(0, eqPos)));
            std::wstring val = Trim(Utf8ToWide(trimmed.substr(eqPos + 1)));

            std::wstring lKey = ToLowerW(key);
            std::string lSec = currentSection;
            std::transform(lSec.begin(), lSec.end(), lSec.begin(), ::tolower);

            if (lSec == "train" || lSec == "trainconfig")
            {
                if (lKey == L"id") outConfig.id = val;
                else if (lKey == L"name") outConfig.name = val;
                else if (lKey == L"type" || lKey == L"traintype") outConfig.trainType = val;
                else if (lKey == L"category") outConfig.category = val;
                else if (lKey == L"description") outConfig.description = val;
                else if (lKey == L"maxspeed" || lKey == L"maxvelocity" || lKey == L"speed")
                {
                    // Parse "160 km/h" or "160"
                    try {
                        std::wstringstream wss(val);
                        double spd = 0.0;
                        wss >> spd;
                        outConfig.maxSpeedKmph = spd;
                    } catch (...) {}
                }
                else if (lKey == L"perffactor" || lKey == L"performancefactor" || lKey == L"perf")
                {
                    // Parse "1.0 %" or "0.100"
                    try {
                        std::wstringstream wss(val);
                        double pf = 1.0;
                        wss >> pf;
                        outConfig.perfFactor = pf;
                    } catch (...) {}
                }
                else if (lKey == L"minlength") outConfig.minLength = _wtoi(val.c_str());
                else if (lKey == L"maxlength" || lKey == L"totalunits" || lKey == L"lengthlimit") outConfig.maxLength = _wtoi(val.c_str());
            }
            else if (lSec == "composition")
            {
                if (lKey == L"sequence")
                {
                    outConfig.sequence = Split(val, L',');
                }
            }
            else if (currentPoolIdx >= 0 && currentPoolIdx < (int)parsedPools.size())
            {
                TrainBlueprintPool& bp = parsedPools[currentPoolIdx];

                if (lKey == L"role")
                {
                    bp.role = val;
                    if (!val.empty()) DatabaseManager::AddBlueprintRole(val, L"Custom");
                }
                else if (lKey == L"type") bp.unitType = val;
                else if (lKey == L"filtertag" || lKey == L"tags") bp.filterTag = val;
                else if (lKey == L"count")
                {
                    // Parse "6-9" or "10"
                    size_t dash = val.find(L'-');
                    if (dash == std::wstring::npos) dash = val.find(L"..");
                    if (dash != std::wstring::npos)
                    {
                        bp.minCount = _wtoi(val.substr(0, dash).c_str());
                        bp.maxCount = _wtoi(val.substr(dash + 1).c_str());
                    }
                    else
                    {
                        int cnt = _wtoi(val.c_str());
                        bp.minCount = cnt;
                        bp.maxCount = cnt;
                    }
                }
                else if (lKey == L"mincount") bp.minCount = _wtoi(val.c_str());
                else if (lKey == L"maxcount") bp.maxCount = _wtoi(val.c_str());
                else if (lKey == L"probability")
                {
                    double p = _wtof(val.c_str());
                    if (val.find(L'%') != std::wstring::npos) p /= 100.0;
                    bp.probability = p;
                }
            }
        }

        if (outConfig.name.empty())
        {
            outConfig.name = fs::path(filePath).stem().wstring();
        }

        outConfig.pools = parsedPools;
        if (outConfig.sequence.empty())
        {
            for (const auto& p : outConfig.pools)
            {
                outConfig.sequence.push_back(p.poolName);
            }
        }

        return true;
    }

    bool SaveTrainConfig(const std::wstring& filePath, const TrainConfig& config)
    {
        std::ofstream ofs(filePath);
        if (!ofs.is_open()) return false;

        ofs << "[Train]\n";
        std::wstring cleanID = SanitizeID(config.id);
        if (!cleanID.empty()) ofs << "ID          = " << WideToUtf8(cleanID) << "\n";
        ofs << "Name        = " << WideToUtf8(config.name) << "\n";
        if (!config.trainType.empty()) ofs << "Type        = " << WideToUtf8(config.trainType) << "\n";
        if (!config.category.empty()) ofs << "Category    = " << WideToUtf8(config.category) << "\n";
        if (config.maxSpeedKmph > 0.0) ofs << "MaxSpeed    = " << config.maxSpeedKmph << " km/h\n";
        if (config.perfFactor > 0.0) ofs << "PerfFactor  = " << config.perfFactor << " %\n";
        if (config.minLength > 0) ofs << "MinLength   = " << config.minLength << "\n";
        if (config.maxLength > 0) ofs << "MaxLength   = " << config.maxLength << "\n";
        if (!config.description.empty()) ofs << "Description = " << WideToUtf8(config.description) << "\n";

        ofs << "\n[Composition]\nSequence = ";
        for (size_t i = 0; i < config.sequence.size(); ++i)
        {
            ofs << WideToUtf8(config.sequence[i]);
            if (i + 1 < config.sequence.size()) ofs << ", ";
        }
        ofs << "\n\n";

        for (const auto& pool : config.pools)
        {
            ofs << "[Pool:" << WideToUtf8(pool.poolName) << "]\n";
            if (!pool.role.empty()) ofs << "Role        = " << WideToUtf8(pool.role) << "\n";
            if (!pool.unitType.empty()) ofs << "Type        = " << WideToUtf8(pool.unitType) << "\n";
            if (pool.minCount == pool.maxCount)
            {
                ofs << "Count       = " << pool.minCount << "\n";
            }
            else
            {
                ofs << "Count       = " << pool.minCount << "-" << pool.maxCount << "\n";
            }
            if (!pool.filterTag.empty()) ofs << "FilterTag   = " << WideToUtf8(pool.filterTag) << "\n";

            if (pool.probability < 0.999) ofs << "Probability = " << (int)(pool.probability * 100.0) << "%\n";
            ofs << "\n";
        }

        ofs.close();
        return true;
    }

    bool LoadTrainBinding(const TrainConfig& config, TrainBinding& outBinding)
    {
        outBinding = TrainBinding();
        outBinding.trainID = config.id;
        outBinding.configName = config.name;

        std::wstring bindingPath = GetBindingFilePathForConfig(config);
        std::ifstream ifs(bindingPath);
        if (!ifs.is_open()) return false;

        std::string line;
        std::string currentSection = "";
        std::vector<TrainBindingPool> parsedBindings;
        int currentBndIdx = -1;

        while (std::getline(ifs, line))
        {
            std::string trimmed = TrimA(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

            if (trimmed.front() == '[' && trimmed.back() == ']')
            {
                currentSection = trimmed.substr(1, trimmed.length() - 2);
                currentSection = TrimA(currentSection);
                std::string lSec = currentSection;
                std::transform(lSec.begin(), lSec.end(), lSec.begin(), ::tolower);

                if (lSec.rfind("binding:", 0) == 0)
                {
                    std::wstring poolName = Utf8ToWide(currentSection.substr(8));
                    poolName = Trim(poolName);
                    TrainBindingPool bp;
                    bp.poolName = poolName.empty() ? L"Pool" : poolName;
                    parsedBindings.push_back(bp);
                    currentBndIdx = (int)parsedBindings.size() - 1;
                }
                else
                {
                    currentBndIdx = -1;
                }
                continue;
            }

            if (currentBndIdx >= 0 && currentBndIdx < (int)parsedBindings.size())
            {
                TrainBindingPool& bp = parsedBindings[currentBndIdx];
                size_t eqPos = trimmed.find('=');
                std::wstring key = L"";
                std::wstring val = L"";
                if (eqPos != std::string::npos)
                {
                    key = Trim(Utf8ToWide(trimmed.substr(0, eqPos)));
                    val = Trim(Utf8ToWide(trimmed.substr(eqPos + 1)));
                }
                else
                {
                    val = Trim(Utf8ToWide(trimmed));
                }

                std::wstring lKey = ToLowerW(key);
                if (lKey == L"pickmode" || lKey == L"mode")
                {
                    std::wstring lVal = ToLowerW(val);
                    if (lVal == L"sequential" || lVal == L"sequence" || lVal == L"order")
                        bp.pickMode = PoolManager::PoolPickMode::Sequential;
                    else
                        bp.pickMode = PoolManager::PoolPickMode::Random;
                }
                else if (lKey == L"flippolicy")
                {
                    std::wstring lVal = ToLowerW(val);
                    if (lVal == L"alwaysflipped" || lVal == L"reversed")
                        bp.flipPolicy = PoolManager::PoolFlipPolicy::AlwaysFlipped;
                    else if (lVal == L"allowrandom" || lVal == L"random")
                        bp.flipPolicy = PoolManager::PoolFlipPolicy::AllowRandom;
                    else
                        bp.flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;
                }
                else if (lKey == L"units" || eqPos == std::string::npos)
                {
                    auto unitTokens = Split(val, L',');
                    for (const auto& uTok : unitTokens)
                    {
                        if (uTok.empty()) continue;
                        PoolManager::PoolUnit pu;
                        std::wstring uName = uTok;
                        PoolManager::UnitFlipMode uFlip = PoolManager::UnitFlipMode::Auto;

                        // Support format "folder/unit.wag:fwd", "unit.wag:flip", "unit:rnd"
                        size_t colonPos = uName.find(L':');
                        if (colonPos != std::wstring::npos)
                        {
                            std::wstring flag = ToLowerW(uName.substr(colonPos + 1));
                            uName = uName.substr(0, colonPos);
                            if (flag == L"fwd" || flag == L"forward") uFlip = PoolManager::UnitFlipMode::Forward;
                            else if (flag == L"flip" || flag == L"flipped" || flag == L"rev") uFlip = PoolManager::UnitFlipMode::Flipped;
                            else if (flag == L"rnd" || flag == L"random") uFlip = PoolManager::UnitFlipMode::Random;
                        }

                        // Check if a folder was specified (e.g. "IR_WAP7/WAP7_30201" or "IR_WAP7\WAP7_30201")
                        std::wstring parsedFolder = L"";
                        size_t slashPos = uName.find_last_of(L"/\\");
                        if (slashPos != std::wstring::npos)
                        {
                            parsedFolder = uName.substr(0, slashPos);
                            uName = uName.substr(slashPos + 1);
                        }

                        pu.szFileName = uName;
                        pu.szFolder = parsedFolder;
                        pu.flipMode = uFlip;

                        // Resolve exact folder and isEngine from stock cache / database
                        bool foundInCache = false;
                        EnterCriticalSection(&g_StockCacheCS);
                        for (const auto& item : g_StockCache)
                        {
                            bool nameMatch = (_wcsicmp(item.szFileName.c_str(), uName.c_str()) == 0 ||
                                              _wcsicmp((item.szFileName + item.szExtension).c_str(), uName.c_str()) == 0);
                            if (nameMatch)
                            {
                                if (parsedFolder.empty() || _wcsicmp(item.szFolder.c_str(), parsedFolder.c_str()) == 0)
                                {
                                    pu.szFileName = item.szFileName;
                                    pu.szFolder = item.szFolder;
                                    pu.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                                    foundInCache = true;
                                    break;
                                }
                            }
                        }
                        LeaveCriticalSection(&g_StockCacheCS);

                        if (!foundInCache)
                        {
                            // Robust edge case fallback for external or uninstalled stock
                            std::wstring lName = ToLowerW(uName);
                            if (lName.ends_with(L".eng"))
                            {
                                pu.isEngine = true;
                            }
                            else if (lName.ends_with(L".wag"))
                            {
                                pu.isEngine = false;
                            }
                            else if (lName.find(L"wap") != std::wstring::npos || lName.find(L"wdp") != std::wstring::npos ||
                                     lName.find(L"wag") != std::wstring::npos || lName.find(L"wdg") != std::wstring::npos ||
                                     lName.find(L"loco") != std::wstring::npos || lName.find(L"engine") != std::wstring::npos)
                            {
                                pu.isEngine = true;
                            }
                            else
                            {
                                pu.isEngine = false;
                            }
                        }
                        bp.units.push_back(pu);
                    }
                }
            }
        }

        outBinding.pools = parsedBindings;
        return true;
    }

    bool SaveTrainBinding(const TrainConfig& config, const TrainBinding& binding)
    {
        std::wstring bindingPath = GetBindingFilePathForConfig(config);
        try
        {
            fs::path parentDir = fs::path(bindingPath).parent_path();
            if (!fs::exists(parentDir))
            {
                fs::create_directories(parentDir);
            }
        }
        catch (...) {}

        std::ofstream ofs(bindingPath);
        if (!ofs.is_open()) return false;

        ofs << "[TrainBinding]\n";
        ofs << "TrainID    = " << WideToUtf8(!binding.trainID.empty() ? binding.trainID : config.id) << "\n";
        ofs << "ConfigName = " << WideToUtf8(!binding.configName.empty() ? binding.configName : config.name) << "\n\n";

        for (const auto& p : binding.pools)
        {
            ofs << "[Binding:" << WideToUtf8(p.poolName) << "]\n";
            if (p.pickMode == PoolManager::PoolPickMode::Sequential)
                ofs << "PickMode   = Sequential\n";
            else
                ofs << "PickMode   = Random\n";

            if (p.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                ofs << "FlipPolicy = AllowRandom\n";
            else if (p.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                ofs << "FlipPolicy = AlwaysFlipped\n";
            else
                ofs << "FlipPolicy = ForwardOnly\n";

            ofs << "Units      = ";
            for (size_t i = 0; i < p.units.size(); ++i)
            {
                ofs << WideToUtf8(p.units[i].szFileName);
                if (p.units[i].flipMode == PoolManager::UnitFlipMode::Forward) ofs << ":fwd";
                else if (p.units[i].flipMode == PoolManager::UnitFlipMode::Flipped) ofs << ":flip";
                else if (p.units[i].flipMode == PoolManager::UnitFlipMode::Random) ofs << ":rnd";

                if (i + 1 < p.units.size()) ofs << ", ";
            }
            ofs << "\n\n";
        }

        ofs.close();
        return true;
    }

    bool BuildPresetFromConfigAndBinding(const TrainConfig& config, const TrainBinding& binding, PoolManager::PoolPreset& outPreset)
    {
        outPreset.presetName = config.name;
        outPreset.pools.clear();

        for (size_t p = 0; p < config.pools.size(); ++p)
        {
            const auto& bp = config.pools[p];
            PoolManager::ConsistPool cp;
            cp.name = bp.poolName;
            cp.minCount = bp.minCount;
            cp.maxCount = bp.maxCount;

            if (p < binding.pools.size())
            {
                cp.pickMode = binding.pools[p].pickMode;
                cp.flipPolicy = binding.pools[p].flipPolicy;
                cp.units = binding.pools[p].units;
            }
            else
            {
                cp.pickMode = PoolManager::PoolPickMode::Random;
                cp.flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;
            }

            outPreset.pools.push_back(cp);
        }

        return true;
    }

    bool ExtractBindingFromPreset(const TrainConfig& config, const PoolManager::PoolPreset& preset, TrainBinding& outBinding)
    {
        outBinding.trainID = config.id;
        outBinding.configName = config.name;
        outBinding.pools.clear();

        for (size_t p = 0; p < preset.pools.size(); ++p)
        {
            const auto& cp = preset.pools[p];
            TrainBindingPool tbp;
            tbp.poolName = cp.name;
            tbp.pickMode = cp.pickMode;
            tbp.flipPolicy = cp.flipPolicy;
            tbp.units = cp.units;
            outBinding.pools.push_back(tbp);
        }

        return true;
    }

    void InitializeTrainConfigs()
    {
        ScanTrainConfigs();
        if (!g_LoadedConfigsCache.empty())
        {
            g_ActiveTrainConfigIndex = 0;
        }
    }

    TrainConfig* GetActiveTrainConfig()
    {
        if (g_ActiveTrainConfigIndex >= 0 && g_ActiveTrainConfigIndex < (int)g_LoadedConfigsCache.size())
        {
            return &g_LoadedConfigsCache[g_ActiveTrainConfigIndex];
        }
        return nullptr;
    }

    bool SetActiveTrainConfigByIndex(int index)
    {
        if (index >= 0 && index < (int)g_LoadedConfigsCache.size())
        {
            g_ActiveTrainConfigIndex = index;
            return true;
        }
        return false;
    }
}
