#include "DatabaseManager.h"
#include "sqlite3/sqlite3.h"
#include "AppLogging.h"
#include <shlwapi.h>
#include <mutex>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

namespace DatabaseManager
{
    static sqlite3* g_db = nullptr;
    static std::recursive_mutex g_dbMutex;
    static bool g_isInitialized = false;

    static void SeedDefaultBlueprintRolesInternal();

    // Helper functions for string conversion
    static std::string WideToUtf8(const std::wstring& wstr)
    {
        if (wstr.empty()) return std::string();
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], sizeNeeded, NULL, NULL);
        return str;
    }

    static std::wstring Utf8ToWide(const char* utf8Str)
    {
        if (!utf8Str || utf8Str[0] == '\0') return std::wstring();
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
        std::wstring wstr(sizeNeeded > 0 ? sizeNeeded - 1 : 0, 0);
        if (sizeNeeded > 1)
        {
            MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, &wstr[0], sizeNeeded);
        }
        return wstr;
    }

    std::wstring GetAppDirectory()
    {
        HMODULE hMod = GetModuleHandleW(L"TSCBCore64.dll");
        if (!hMod) hMod = GetModuleHandleW(L"TSCBCore32.dll");
        if (!hMod) hMod = GetModuleHandleW(NULL);

        wchar_t szPath[MAX_PATH] = { 0 };
        GetModuleFileNameW(hMod, szPath, MAX_PATH);
        PathRemoveFileSpecW(szPath);

        std::wstring dir = szPath;
        if (!dir.empty() && dir.back() != L'\\') dir += L'\\';

        std::wstring appData = dir + L"AppData";
        CreateDirectoryW(appData.c_str(), NULL);

        return dir;
    }

    std::wstring GetDatabaseFilePath()
    {
        return GetAppDirectory() + L"AppData\\TSCB_DATA.db";
    }

    bool Initialize()
    {
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);
        if (g_isInitialized && g_db) return true;

        std::wstring dbPath = GetDatabaseFilePath();

        int rc = sqlite3_open16(
            dbPath.c_str(),
            &g_db
        );

        if (rc != SQLITE_OK)
        {
            LOG_ERROR("DatabaseManager: Failed to open database '%ls', error: %s", dbPath.c_str(), sqlite3_errmsg(g_db));
            if (g_db)
            {
                sqlite3_close(g_db);
                g_db = nullptr;
            }
            return false;
        }

        // Optimize SQLite performance & concurrency with WAL mode
        char* err = nullptr;
        sqlite3_exec(g_db, "PRAGMA journal_mode = WAL;", NULL, NULL, &err);
        sqlite3_exec(g_db, "PRAGMA synchronous = NORMAL;", NULL, NULL, &err);
        sqlite3_exec(g_db, "PRAGMA temp_store = MEMORY;", NULL, NULL, &err);

        // 1. AppSettings table (replaces Windows Registry)
        const char* sqlSettings = 
            "CREATE TABLE IF NOT EXISTS AppSettings ("
            "    key TEXT PRIMARY KEY NOT NULL,"
            "    val TEXT NOT NULL"
            ");";
        sqlite3_exec(g_db, sqlSettings, NULL, NULL, &err);

        // 2. RollingStock table (stores scanned engines and wagons)
        const char* sqlStock = 
            "CREATE TABLE IF NOT EXISTS RollingStock ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    folder TEXT NOT NULL,"
            "    fileName TEXT NOT NULL,"
            "    extension TEXT NOT NULL,"
            "    category TEXT NOT NULL,"
            "    details TEXT NOT NULL,"
            "    lastWriteTime INTEGER DEFAULT 0,"
            "    UNIQUE(folder, fileName)"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_stock_cat ON RollingStock(category);"
            "CREATE INDEX IF NOT EXISTS idx_stock_folder ON RollingStock(folder);";
        sqlite3_exec(g_db, sqlStock, NULL, NULL, &err);
        sqlite3_exec(g_db, "ALTER TABLE RollingStock ADD COLUMN lastWriteTime INTEGER DEFAULT 0;", NULL, NULL, NULL);

        // 3. PoolPresets table
        const char* sqlPoolPresets =
            "CREATE TABLE IF NOT EXISTS PoolPresets ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    name TEXT UNIQUE NOT NULL,"
            "    payload TEXT NOT NULL"
            ");";
        sqlite3_exec(g_db, sqlPoolPresets, NULL, NULL, &err);

        // 4. ReplacementGroups table
        const char* sqlReplGroups =
            "CREATE TABLE IF NOT EXISTS ReplacementGroups ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    name TEXT UNIQUE NOT NULL,"
            "    payload TEXT NOT NULL"
            ");";
        sqlite3_exec(g_db, sqlReplGroups, NULL, NULL, &err);

        // 5. PoolMutationSettings table
        const char* sqlMutation =
            "CREATE TABLE IF NOT EXISTS PoolMutationSettings ("
            "    id INTEGER PRIMARY KEY DEFAULT 1,"
            "    payload TEXT NOT NULL"
            ");";
        sqlite3_exec(g_db, sqlMutation, NULL, NULL, &err);

        // 6. BlueprintRoles table (stores built-in and user-custom role descriptors)
        const char* sqlRoles =
            "CREATE TABLE IF NOT EXISTS BlueprintRoles ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    role_name TEXT UNIQUE NOT NULL,"
            "    category TEXT NOT NULL,"
            "    is_default INTEGER DEFAULT 0"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_roles_cat ON BlueprintRoles(category);";
        sqlite3_exec(g_db, sqlRoles, NULL, NULL, &err);

        g_isInitialized = true;
        LOG_INFO("DatabaseManager: Successfully opened master database '%ls'", dbPath.c_str());

        // Ensure default seeded roles exist
        SeedDefaultBlueprintRolesInternal();

        // Remove old legacy .dat cache files
        CleanupLegacyDatFiles();

        return true;
    }

    void Shutdown()
    {
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);
        if (g_db)
        {
            // Flush WAL log and switch back to DELETE mode so SQLite automatically removes WAL & SHM companion files
            sqlite3_exec(g_db, "PRAGMA journal_mode = DELETE;", NULL, NULL, NULL);
            sqlite3_close(g_db);
            g_db = nullptr;
        }
        g_isInitialized = false;

        // Clean up any remaining WAL or SHM temporary files
        std::wstring dbPath = GetDatabaseFilePath();
        DeleteFileW((dbPath + L"-wal").c_str());
        DeleteFileW((dbPath + L"-shm").c_str());
    }

    void CleanupLegacyDatFiles()
    {
        std::wstring appData = GetAppDirectory() + L"AppData\\";
        DeleteFileW((appData + L"stock_cache.dat").c_str());
        DeleteFileW((appData + L"PoolPreset.dat").c_str());
        DeleteFileW((appData + L"PoolMutation.dat").c_str());
        DeleteFileW((appData + L"ReplacementGroups.dat").c_str());
    }

    // -----------------------------------------------------------------------
    // AppSettings (Key-Value Store)
    // -----------------------------------------------------------------------
    std::wstring GetSetting(const std::wstring& key, const std::wstring& defaultVal)
    {
        if (!g_isInitialized && !Initialize()) return defaultVal;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT val FROM AppSettings WHERE key = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return defaultVal;

        std::string utfKey = WideToUtf8(key);
        sqlite3_bind_text(stmt, 1, utfKey.c_str(), -1, SQLITE_TRANSIENT);

        std::wstring result = defaultVal;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* valText = (const char*)sqlite3_column_text(stmt, 0);
            if (valText)
            {
                result = Utf8ToWide(valText);
            }
        }

        sqlite3_finalize(stmt);
        return result;
    }

    void SetSetting(const std::wstring& key, const std::wstring& val)
    {
        if (!g_isInitialized && !Initialize()) return;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "INSERT INTO AppSettings (key, val) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET val = excluded.val;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

        std::string utfKey = WideToUtf8(key);
        std::string utfVal = WideToUtf8(val);

        sqlite3_bind_text(stmt, 1, utfKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, utfVal.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    int GetSettingInt(const std::wstring& key, int defaultVal)
    {
        std::wstring strVal = GetSetting(key, L"");
        if (strVal.empty()) return defaultVal;
        try {
            return std::stoi(strVal);
        } catch (...) {
            return defaultVal;
        }
    }

    void SetSettingInt(const std::wstring& key, int val)
    {
        SetSetting(key, std::to_wstring(val));
    }

    // -----------------------------------------------------------------------
    // Rolling Stock Cache Persistence & Fast Kernel32 Incremental Sync
    // -----------------------------------------------------------------------
    static std::wstring MakeStockKey(const std::wstring& folder, const std::wstring& fileName, const std::wstring& ext)
    {
        std::wstring k = folder + L"|" + fileName + ext;
        for (auto& c : k) c = towlower(c);
        return k;
    }

    bool LoadStockCache(uint64_t currentSig, std::vector<StockItem>& outCache)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        // Verify Trainset signature stored in DB
        std::wstring sigStr = GetSetting(L"TrainsetSignature", L"0");
        uint64_t dbSig = 0;
        try {
            dbSig = std::stoull(sigStr);
        } catch (...) {
            dbSig = 0;
        }

        if (dbSig != currentSig || currentSig == 0)
        {
            return false; // Cache is stale or non-existent
        }

        const char* sql = "SELECT fileName, category, folder, extension, details, lastWriteTime FROM RollingStock ORDER BY id ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

        std::vector<StockItem> temp;
        temp.reserve(10000);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            StockItem item;
            const char* fName = (const char*)sqlite3_column_text(stmt, 0);
            const char* cat   = (const char*)sqlite3_column_text(stmt, 1);
            const char* fol   = (const char*)sqlite3_column_text(stmt, 2);
            const char* ext   = (const char*)sqlite3_column_text(stmt, 3);
            const char* det   = (const char*)sqlite3_column_text(stmt, 4);
            int64_t     lwt   = sqlite3_column_int64(stmt, 5);

            if (fName) item.szFileName  = Utf8ToWide(fName);
            if (cat)   item.szCategory  = Utf8ToWide(cat);
            if (fol)   item.szFolder    = Utf8ToWide(fol);
            if (ext)   item.szExtension = Utf8ToWide(ext);
            if (det)   item.szDetails   = Utf8ToWide(det);
            item.lastWriteTime = (uint64_t)lwt;

            temp.push_back(std::move(item));
        }

        sqlite3_finalize(stmt);

        if (temp.empty()) return false;

        outCache = std::move(temp);
        LOG_INFO("DatabaseManager: Loaded %zu rolling stock items from SQLite master database.", outCache.size());
        return true;
    }

    void SaveStockCache(uint64_t currentSig, const std::vector<StockItem>& cache)
    {
        if (!g_isInitialized && !Initialize()) return;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "DELETE FROM RollingStock;", NULL, NULL, NULL);

        const char* sql = "INSERT INTO RollingStock (folder, fileName, extension, category, details, lastWriteTime) VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            for (const auto& item : cache)
            {
                std::string uFol = WideToUtf8(item.szFolder);
                std::string uName = WideToUtf8(item.szFileName);
                std::string uExt = WideToUtf8(item.szExtension);
                std::string uCat = WideToUtf8(item.szCategory);
                std::string uDet = WideToUtf8(item.szDetails);

                sqlite3_bind_text(stmt, 1, uFol.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, uName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, uExt.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, uCat.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, uDet.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 6, (sqlite3_int64)item.lastWriteTime);

                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);

        // Update signature in AppSettings
        SetSetting(L"TrainsetSignature", std::to_wstring(currentSig));
        LOG_INFO("DatabaseManager: Saved %zu rolling stock items to SQLite master database.", cache.size());
    }

    bool LoadStockMap(std::unordered_map<std::wstring, StockItem>& outMap)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT fileName, category, folder, extension, details, lastWriteTime FROM RollingStock;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

        outMap.clear();
        outMap.reserve(10000);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            StockItem item;
            const char* fName = (const char*)sqlite3_column_text(stmt, 0);
            const char* cat   = (const char*)sqlite3_column_text(stmt, 1);
            const char* fol   = (const char*)sqlite3_column_text(stmt, 2);
            const char* ext   = (const char*)sqlite3_column_text(stmt, 3);
            const char* det   = (const char*)sqlite3_column_text(stmt, 4);
            int64_t     lwt   = sqlite3_column_int64(stmt, 5);

            if (fName) item.szFileName  = Utf8ToWide(fName);
            if (cat)   item.szCategory  = Utf8ToWide(cat);
            if (fol)   item.szFolder    = Utf8ToWide(fol);
            if (ext)   item.szExtension = Utf8ToWide(ext);
            if (det)   item.szDetails   = Utf8ToWide(det);
            item.lastWriteTime = (uint64_t)lwt;

            std::wstring key = MakeStockKey(item.szFolder, item.szFileName, item.szExtension);
            outMap[key] = std::move(item);
        }

        sqlite3_finalize(stmt);
        return true;
    }

    bool SyncStockDiff(
        const std::vector<StockItem>& toInsert,
        const std::vector<StockItem>& toUpdate,
        const std::vector<std::pair<std::wstring, std::wstring>>& toDelete
    )
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

        // 1. Process Deletions
        if (!toDelete.empty())
        {
            const char* sqlDel = "DELETE FROM RollingStock WHERE folder = ? AND fileName = ?;";
            sqlite3_stmt* stmtDel = nullptr;
            if (sqlite3_prepare_v2(g_db, sqlDel, -1, &stmtDel, NULL) == SQLITE_OK)
            {
                for (const auto& d : toDelete)
                {
                    std::string uFol = WideToUtf8(d.first);
                    std::string uName = WideToUtf8(d.second);
                    sqlite3_bind_text(stmtDel, 1, uFol.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtDel, 2, uName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmtDel);
                    sqlite3_reset(stmtDel);
                }
                sqlite3_finalize(stmtDel);
            }
        }

        // 2. Process Updates
        if (!toUpdate.empty())
        {
            const char* sqlUpd = "UPDATE RollingStock SET category = ?, details = ?, lastWriteTime = ? WHERE folder = ? AND fileName = ?;";
            sqlite3_stmt* stmtUpd = nullptr;
            if (sqlite3_prepare_v2(g_db, sqlUpd, -1, &stmtUpd, NULL) == SQLITE_OK)
            {
                for (const auto& item : toUpdate)
                {
                    std::string uCat = WideToUtf8(item.szCategory);
                    std::string uDet = WideToUtf8(item.szDetails);
                    std::string uFol = WideToUtf8(item.szFolder);
                    std::string uName = WideToUtf8(item.szFileName);

                    sqlite3_bind_text(stmtUpd, 1, uCat.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtUpd, 2, uDet.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmtUpd, 3, (sqlite3_int64)item.lastWriteTime);
                    sqlite3_bind_text(stmtUpd, 4, uFol.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtUpd, 5, uName.c_str(), -1, SQLITE_TRANSIENT);

                    sqlite3_step(stmtUpd);
                    sqlite3_reset(stmtUpd);
                }
                sqlite3_finalize(stmtUpd);
            }
        }

        // 3. Process Inserts
        if (!toInsert.empty())
        {
            const char* sqlIns = "INSERT INTO RollingStock (folder, fileName, extension, category, details, lastWriteTime) VALUES (?, ?, ?, ?, ?, ?);";
            sqlite3_stmt* stmtIns = nullptr;
            if (sqlite3_prepare_v2(g_db, sqlIns, -1, &stmtIns, NULL) == SQLITE_OK)
            {
                for (const auto& item : toInsert)
                {
                    std::string uFol = WideToUtf8(item.szFolder);
                    std::string uName = WideToUtf8(item.szFileName);
                    std::string uExt = WideToUtf8(item.szExtension);
                    std::string uCat = WideToUtf8(item.szCategory);
                    std::string uDet = WideToUtf8(item.szDetails);

                    sqlite3_bind_text(stmtIns, 1, uFol.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtIns, 2, uName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtIns, 3, uExt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtIns, 4, uCat.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmtIns, 5, uDet.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmtIns, 6, (sqlite3_int64)item.lastWriteTime);

                    sqlite3_step(stmtIns);
                    sqlite3_reset(stmtIns);
                }
                sqlite3_finalize(stmtIns);
            }
        }

        sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
        LOG_INFO("DatabaseManager: Incremental Sync Complete: +%zu inserted, ~%zu updated, -%zu deleted.",
            toInsert.size(), toUpdate.size(), toDelete.size());
        return true;
    }

    // -----------------------------------------------------------------------
    // Helper Serializers for Complex Pool & Mutation Objects
    // -----------------------------------------------------------------------
    static std::wstring SerializePoolPreset(const PoolManager::PoolPreset& preset)
    {
        std::wstringstream ss;
        ss << preset.presetName << L"\t" << preset.pools.size() << L"\n";
        for (const auto& pool : preset.pools)
        {
            ss << pool.name << L"\t" 
               << (int)pool.pickMode << L"\t"
               << pool.minCount << L"\t"
               << pool.maxCount << L"\t"
               << (pool.flipAllowed ? 1 : 0) << L"\t"
               << (int)pool.flipPolicy << L"\t"
               << (pool.isCollapsed ? 1 : 0) << L"\t"
               << pool.units.size() << L"\n";

            for (const auto& unit : pool.units)
            {
                ss << unit.szFileName << L"\t"
                   << unit.szFolder << L"\t"
                   << (unit.isEngine ? 1 : 0) << L"\t"
                   << (int)unit.flipMode << L"\n";
            }
        }
        return ss.str();
    }

    static bool DeserializePoolPreset(const std::wstring& data, PoolManager::PoolPreset& outPreset)
    {
        if (data.empty()) return false;
        std::wstringstream ss(data);
        std::wstring line;

        if (!std::getline(ss, line)) return false;
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) return false;
        outPreset.presetName = line.substr(0, tab);
        int poolCount = std::stoi(line.substr(tab + 1));

        outPreset.pools.clear();
        for (int p = 0; p < poolCount; ++p)
        {
            if (!std::getline(ss, line)) break;
            std::wstringstream pss(line);
            PoolManager::ConsistPool pool;
            int pickMode = 0, flipAllowed = 0, flipPolicy = 0, isCollapsed = 0, unitCount = 0;

            std::getline(pss, pool.name, L'\t');
            pss >> pickMode >> pool.minCount >> pool.maxCount >> flipAllowed >> flipPolicy >> isCollapsed >> unitCount;
            pool.pickMode = (PoolManager::PoolPickMode)pickMode;
            pool.flipAllowed = (flipAllowed != 0);
            pool.flipPolicy = (PoolManager::PoolFlipPolicy)flipPolicy;
            pool.isCollapsed = (isCollapsed != 0);

            for (int u = 0; u < unitCount; ++u)
            {
                if (!std::getline(ss, line)) break;
                std::wstringstream uss(line);
                PoolManager::PoolUnit unit;
                int isEng = 1, flipMode = 0;
                std::getline(uss, unit.szFileName, L'\t');
                std::getline(uss, unit.szFolder, L'\t');
                uss >> isEng >> flipMode;
                unit.isEngine = (isEng != 0);
                unit.flipMode = (PoolManager::UnitFlipMode)flipMode;
                pool.units.push_back(std::move(unit));
            }
            outPreset.pools.push_back(std::move(pool));
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Pool Presets Persistence
    // -----------------------------------------------------------------------
    bool LoadPoolPresets(std::vector<PoolManager::PoolPreset>& outPresets)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT name, payload FROM PoolPresets ORDER BY id ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

        std::vector<PoolManager::PoolPreset> temp;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* payload = (const char*)sqlite3_column_text(stmt, 1);
            if (payload)
            {
                std::wstring wPayload = Utf8ToWide(payload);
                PoolManager::PoolPreset preset;
                if (DeserializePoolPreset(wPayload, preset))
                {
                    temp.push_back(std::move(preset));
                }
            }
        }
        sqlite3_finalize(stmt);

        if (temp.empty()) return false;
        outPresets = std::move(temp);
        return true;
    }

    bool SavePoolPresets(const std::vector<PoolManager::PoolPreset>& presets)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "DELETE FROM PoolPresets;", NULL, NULL, NULL);

        const char* sql = "INSERT INTO PoolPresets (name, payload) VALUES (?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            for (const auto& preset : presets)
            {
                std::string uName = WideToUtf8(preset.presetName);
                std::wstring wPayload = SerializePoolPreset(preset);
                std::string uPayload = WideToUtf8(wPayload);

                sqlite3_bind_text(stmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, uPayload.c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
        return true;
    }

    // -----------------------------------------------------------------------
    // Replacement Groups Persistence
    // -----------------------------------------------------------------------
    static std::wstring SerializeReplacementGroup(const PoolManager::ReplacementGroup& group)
    {
        std::wstringstream ss;
        std::wstring cat = group.category.empty() ? L"General" : group.category;
        ss << group.name << L"\t" << (group.isCollapsed ? 1 : 0) << L"\t" << group.units.size() << L"\t" << cat << L"\t"
           << (int)group.pickMode << L"\t" << (int)group.flipPolicy << L"\n";
        for (const auto& unit : group.units)
        {
            ss << unit.szFileName << L"\t"
               << unit.szFolder << L"\t"
               << (unit.isEngine ? 1 : 0) << L"\t"
               << (int)unit.flipMode << L"\n";
        }
        return ss.str();
    }

    static bool DeserializeReplacementGroup(const std::wstring& data, PoolManager::ReplacementGroup& outGroup)
    {
        if (data.empty()) return false;
        std::wstringstream ss(data);
        std::wstring line;

        if (!std::getline(ss, line)) return false;
        std::wstringstream lss(line);
        int isCollapsed = 0, unitCount = 0;
        std::getline(lss, outGroup.name, L'\t');
        std::wstring sCol, sCnt;
        std::getline(lss, sCol, L'\t');
        std::getline(lss, sCnt, L'\t');
        isCollapsed = _wtoi(sCol.c_str());
        unitCount = _wtoi(sCnt.c_str());
        outGroup.isCollapsed = (isCollapsed != 0);

        if (!std::getline(lss, outGroup.category, L'\t'))
        {
            outGroup.category = L"";
        }

        std::wstring sPickMode, sFlipPolicy;
        if (std::getline(lss, sPickMode, L'\t') && !sPickMode.empty())
        {
            outGroup.pickMode = (PoolManager::PoolPickMode)_wtoi(sPickMode.c_str());
        }
        else
        {
            outGroup.pickMode = PoolManager::PoolPickMode::Random;
        }

        if (std::getline(lss, sFlipPolicy, L'\t') && !sFlipPolicy.empty())
        {
            outGroup.flipPolicy = (PoolManager::PoolFlipPolicy)_wtoi(sFlipPolicy.c_str());
        }
        else
        {
            outGroup.flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;
        }

        outGroup.units.clear();
        for (int u = 0; u < unitCount; ++u)
        {
            if (!std::getline(ss, line)) break;
            std::wstringstream uss(line);
            PoolManager::PoolUnit unit;
            int isEng = 1, flipMode = 0;
            std::getline(uss, unit.szFileName, L'\t');
            std::getline(uss, unit.szFolder, L'\t');
            uss >> isEng >> flipMode;
            unit.isEngine = (isEng != 0);
            unit.flipMode = (PoolManager::UnitFlipMode)flipMode;
            outGroup.units.push_back(std::move(unit));
        }
        return true;
    }

    bool LoadReplacementGroups(std::vector<PoolManager::ReplacementGroup>& outGroups)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT name, payload FROM ReplacementGroups ORDER BY id ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

        std::vector<PoolManager::ReplacementGroup> temp;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* payload = (const char*)sqlite3_column_text(stmt, 1);
            if (payload)
            {
                std::wstring wPayload = Utf8ToWide(payload);
                PoolManager::ReplacementGroup group;
                if (DeserializeReplacementGroup(wPayload, group))
                {
                    temp.push_back(std::move(group));
                }
            }
        }
        sqlite3_finalize(stmt);

        if (temp.empty()) return false;
        outGroups = std::move(temp);
        return true;
    }

    bool SaveReplacementGroups(const std::vector<PoolManager::ReplacementGroup>& groups)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        sqlite3_exec(g_db, "DELETE FROM ReplacementGroups;", NULL, NULL, NULL);

        const char* sql = "INSERT INTO ReplacementGroups (name, payload) VALUES (?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            for (const auto& group : groups)
            {
                std::string uName = WideToUtf8(group.name);
                std::wstring wPayload = SerializeReplacementGroup(group);
                std::string uPayload = WideToUtf8(wPayload);

                sqlite3_bind_text(stmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, uPayload.c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
        return true;
    }

    // -----------------------------------------------------------------------
    // Pool Mutation Settings Persistence
    // -----------------------------------------------------------------------
    bool LoadMutationSettings(PoolMutator::MutatorSavedSettings& outSettings)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT payload FROM PoolMutationSettings WHERE id = 1 LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* payload = (const char*)sqlite3_column_text(stmt, 0);
            if (payload)
            {
                std::wstring wData = Utf8ToWide(payload);
                std::wstringstream ss(wData);
                int createClones = 0;

                ss >> outSettings.activeTab
                   >> outSettings.selectedPresetIdx
                   >> outSettings.countMode
                   >> outSettings.customCount
                   >> createClones
                   >> outSettings.insertSource
                   >> outSettings.selectedGroupIdx
                   >> outSettings.insertCount
                   >> outSettings.posMode;

                outSettings.createClones = (createClones != 0);

                ss >> outSettings.cloneSuffix;
                if (outSettings.cloneSuffix == L"-") outSettings.cloneSuffix.clear();

                ss >> outSettings.positionIndexText;
                if (outSettings.positionIndexText == L"-") outSettings.positionIndexText.clear();

                int pCount = 0;
                if (ss >> pCount)
                {
                    outSettings.selectedPoolIndices.clear();
                    for (int i = 0; i < pCount; ++i) {
                        int val = 0;
                        ss >> val;
                        outSettings.selectedPoolIndices.push_back(val);
                    }
                }

                int prCount = 0;
                if (ss >> prCount)
                {
                    outSettings.selectedPresetIndices.clear();
                    for (int i = 0; i < prCount; ++i) {
                        int val = 0;
                        ss >> val;
                        outSettings.selectedPresetIndices.push_back(val);
                    }
                }

                int grpCount = 0;
                if (ss >> grpCount)
                {
                    outSettings.selectedGroupIndices.clear();
                    for (int i = 0; i < grpCount; ++i) {
                        int val = 0;
                        ss >> val;
                        outSettings.selectedGroupIndices.push_back(val);
                    }
                }

                found = true;
            }
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool SaveMutationSettings(const PoolMutator::MutatorSavedSettings& settings)
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        std::wstringstream ss;
        ss << settings.activeTab << L" "
           << settings.selectedPresetIdx << L" "
           << settings.countMode << L" "
           << settings.customCount << L" "
           << (settings.createClones ? 1 : 0) << L" "
           << settings.insertSource << L" "
           << settings.selectedGroupIdx << L" "
           << settings.insertCount << L" "
           << settings.posMode << L" "
           << (settings.cloneSuffix.empty() ? L"-" : settings.cloneSuffix) << L" "
           << (settings.positionIndexText.empty() ? L"-" : settings.positionIndexText) << L" ";

        ss << settings.selectedPoolIndices.size() << L" ";
        for (int v : settings.selectedPoolIndices) ss << v << L" ";

        ss << settings.selectedPresetIndices.size() << L" ";
        for (int v : settings.selectedPresetIndices) ss << v << L" ";

        ss << settings.selectedGroupIndices.size() << L" ";
        for (int v : settings.selectedGroupIndices) ss << v << L" ";

        std::wstring wPayload = ss.str();
        std::string uPayload = WideToUtf8(wPayload);

        const char* sql = "INSERT INTO PoolMutationSettings (id, payload) VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET payload = excluded.payload;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, uPayload.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Blueprint Roles Management (Train Config Studio)
    // -----------------------------------------------------------------------

    static void SeedDefaultBlueprintRolesInternal()
    {
        if (!g_db) return;

        // Check if any roles exist
        sqlite3_stmt* stmt = nullptr;
        int count = 0;
        if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM BlueprintRoles;", -1, &stmt, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }

        if (count == 0)
        {
            struct SeedRole {
                const wchar_t* name;
                const wchar_t* category;
            };

            const SeedRole defaults[] = {
                // Locomotives & Cabs
                { L"LeadLoco", L"Locomotives & Cabs" },
                { L"EndLoco", L"Locomotives & Cabs" },
                { L"DrivingCab", L"Locomotives & Cabs" },
                { L"Banker", L"Locomotives & Cabs" },

                // Passenger & Coaches
                { L"Passenger", L"Coaches" },
                { L"ChairCar", L"Coaches" },
                { L"ExecutiveChairCar", L"Coaches" },
                { L"Sleeper", L"Coaches" },
                { L"FirstAC", L"Coaches" },
                { L"ThirdAC", L"Coaches" },
                { L"PantryCar", L"Coaches" },
                { L"LuggageRake", L"Coaches" },
                { L"BrakeVan", L"Coaches" },

                // Freight
                { L"Freight", L"Freight" },
                { L"BoxN", L"Freight" },
                { L"BCN", L"Freight" },
                { L"Tanker", L"Freight" },
                { L"FlatCar", L"Freight" },
                { L"Container", L"Freight" },
            };

            const char* insSql = "INSERT OR IGNORE INTO BlueprintRoles (role_name, category, is_default) VALUES (?, ?, 1);";
            for (const auto& sr : defaults)
            {
                sqlite3_stmt* insStmt = nullptr;
                if (sqlite3_prepare_v2(g_db, insSql, -1, &insStmt, NULL) == SQLITE_OK)
                {
                    std::string uName = WideToUtf8(sr.name);
                    std::string uCat = WideToUtf8(sr.category);
                    sqlite3_bind_text(insStmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insStmt, 2, uCat.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(insStmt);
                    sqlite3_finalize(insStmt);
                }
            }
        }
    }

    bool LoadBlueprintRoles(std::vector<BlueprintRoleItem>& outRoles)
    {
        outRoles.clear();
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "SELECT id, role_name, category, is_default FROM BlueprintRoles ORDER BY category ASC, role_name ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                BlueprintRoleItem item;
                item.id = sqlite3_column_int(stmt, 0);
                const char* uName = (const char*)sqlite3_column_text(stmt, 1);
                const char* uCat = (const char*)sqlite3_column_text(stmt, 2);
                item.roleName = uName ? Utf8ToWide(uName) : L"";
                item.category = uCat ? Utf8ToWide(uCat) : L"Custom";
                item.isDefault = (sqlite3_column_int(stmt, 3) != 0);

                if (!item.roleName.empty())
                {
                    outRoles.push_back(item);
                }
            }
            sqlite3_finalize(stmt);
            return true;
        }

        return false;
    }

    bool AddBlueprintRole(const std::wstring& roleName, const std::wstring& category, bool isDefault)
    {
        if (roleName.empty()) return false;
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "INSERT OR IGNORE INTO BlueprintRoles (role_name, category, is_default) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            std::string uName = WideToUtf8(roleName);
            std::string uCat = WideToUtf8(!category.empty() ? category : L"Custom");
            sqlite3_bind_text(stmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, uCat.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, isDefault ? 1 : 0);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            return (rc == SQLITE_DONE || rc == SQLITE_OK);
        }

        return false;
    }

    bool DeleteBlueprintRole(const std::wstring& roleName)
    {
        if (roleName.empty()) return false;
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        const char* sql = "DELETE FROM BlueprintRoles WHERE role_name = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            std::string uName = WideToUtf8(roleName);
            sqlite3_bind_text(stmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            return (rc == SQLITE_DONE || rc == SQLITE_OK);
        }

        return false;
    }

    bool RestoreDefaultBlueprintRoles()
    {
        if (!g_isInitialized && !Initialize()) return false;
        std::lock_guard<std::recursive_mutex> lock(g_dbMutex);

        struct SeedRole {
            const wchar_t* name;
            const wchar_t* category;
        };

        const SeedRole defaults[] = {
            // Locomotives & Cabs
            { L"LeadLoco", L"Locomotives & Cabs" },
            { L"EndLoco", L"Locomotives & Cabs" },
            { L"DrivingCab", L"Locomotives & Cabs" },
            { L"Banker", L"Locomotives & Cabs" },

            // Passenger & Coaches
            { L"Passenger", L"Coaches" },
            { L"ChairCar", L"Coaches" },
            { L"ExecutiveChairCar", L"Coaches" },
            { L"Sleeper", L"Coaches" },
            { L"FirstAC", L"Coaches" },
            { L"ThirdAC", L"Coaches" },
            { L"PantryCar", L"Coaches" },
            { L"LuggageRake", L"Coaches" },
            { L"BrakeVan", L"Coaches" },

            // Freight
            { L"Freight", L"Freight" },
            { L"BoxN", L"Freight" },
            { L"BCN", L"Freight" },
            { L"Tanker", L"Freight" },
            { L"FlatCar", L"Freight" },
            { L"Container", L"Freight" },
        };

        const char* insSql = "INSERT OR IGNORE INTO BlueprintRoles (role_name, category, is_default) VALUES (?, ?, 1);";
        for (const auto& sr : defaults)
        {
            sqlite3_stmt* insStmt = nullptr;
            if (sqlite3_prepare_v2(g_db, insSql, -1, &insStmt, NULL) == SQLITE_OK)
            {
                std::string uName = WideToUtf8(sr.name);
                std::string uCat = WideToUtf8(sr.category);
                sqlite3_bind_text(insStmt, 1, uName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insStmt, 2, uCat.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(insStmt);
                sqlite3_finalize(insStmt);
            }
        }
        return true;
    }
}
