#include "AssetsParser.h"
#include "DieselParser.h"
#include "ElectricParser.h"
#include "SteamParser.h"
#include "ControlParser.h"
#include "PassengerParser.h"
#include "FreightParser.h"
#include "TenderParser.h"
#include "DatabaseManager.h"
#include "AppLogging.h"
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>

std::vector<StockItem> g_StockCache;
CRITICAL_SECTION g_StockCacheCS;

volatile BOOL g_bCancelStockScan = FALSE;

std::string ReadConFileToAscii(const std::wstring& filePath)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return "";
    }

    std::vector<char> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL))
    {
        CloseHandle(hFile);
        return "";
    }
    CloseHandle(hFile);

    std::string asciiStr;
    asciiStr.reserve(bytesRead);

    for (DWORD i = 0; i < bytesRead; i++)
    {
        char c = buffer[i];
        if (c != 0 && (unsigned char)c != 0xFF && (unsigned char)c != 0xFE)
        {
            asciiStr.push_back(c);
        }
    }

    return asciiStr;
}

std::wstring ExtractTagValue(const std::string& ascii, const std::string& tag)
{
    std::string lower = ascii;
    for (char& c : lower) c = (char)tolower((unsigned char)c);

    size_t pos = lower.find(tag);
    while (pos != std::string::npos)
    {
        // Enforce word boundary before tag to prevent partial matches (e.g. matching "id" inside "uid")
        if (pos > 0 && isalnum((unsigned char)lower[pos - 1]))
        {
            pos = lower.find(tag, pos + 1);
            continue;
        }

        size_t idx = pos + tag.length();
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            if (idx < lower.length() && lower[idx] == '"')
            {
                idx++;
                size_t startVal = idx;
                size_t endVal = ascii.find('"', startVal);
                if (endVal != std::string::npos)
                {
                    std::string valStr = ascii.substr(startVal, endVal - startVal);
                    int len = MultiByteToWideChar(CP_UTF8, 0, valStr.data(), (int)valStr.size(), NULL, 0);
                    if (len <= 0) return std::wstring(valStr.begin(), valStr.end());
                    std::wstring ws(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, valStr.data(), (int)valStr.size(), &ws[0], len);
                    return ws;
                }
            }
            else
            {
                size_t startVal = idx;
                size_t endVal = lower.find_first_of(" \t\r\n)", startVal);
                if (endVal != std::string::npos)
                {
                    std::string valStr = ascii.substr(startVal, endVal - startVal);
                    int len = MultiByteToWideChar(CP_UTF8, 0, valStr.data(), (int)valStr.size(), NULL, 0);
                    if (len <= 0) return std::wstring(valStr.begin(), valStr.end());
                    std::wstring ws(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, valStr.data(), (int)valStr.size(), &ws[0], len);
                    return ws;
                }
            }
        }
        pos = lower.find(tag, pos + 1);
    }
    return L"";
}

std::wstring ResolveRelativePath(const std::wstring& parentDir, const std::wstring& relPath)
{
    std::wstring normalizedRel = relPath;
    for (wchar_t& c : normalizedRel) {
        if (c == L'/') c = L'\\';
    }

    std::vector<std::wstring> parts;
    std::wstringstream parentSS(parentDir);
    std::wstring part;
    while (std::getline(parentSS, part, L'\\')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    std::wstringstream relSS(normalizedRel);
    while (std::getline(relSS, part, L'\\')) {
        if (part == L"." || part.empty()) {
            continue;
        }
        else if (part == L"..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        }
        else {
            parts.push_back(part);
        }
    }

    std::wstring resolvedPath;
    if (!parentDir.empty() && parentDir[0] == L'\\') {
        resolvedPath = L"\\";
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        resolvedPath += parts[i];
        if (i < parts.size() - 1) {
            resolvedPath += L"\\";
        }
    }
    return resolvedPath;
}

bool ParseEnginePropulsionType(const std::wstring& filePath, std::wstring& outType)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }
    std::vector<char> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    std::vector<char> ascii;
    ascii.reserve(bytesRead);
    if (bytesRead >= 2 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE)
    {
        for (DWORD i = 2; i < bytesRead - 1; i += 2)
        {
            char c = buffer[i];
            if (c != 0) ascii.push_back(c);
        }
    }
    else
    {
        for (DWORD i = 0; i < bytesRead; i++)
        {
            char c = buffer[i];
            if (c != 0 && (unsigned char)c != 0xEF && (unsigned char)c != 0xBB && (unsigned char)c != 0xBF)
            {
                ascii.push_back(c);
            }
        }
    }

    auto IsSpace = [](char c) -> bool {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    auto MatchStr = [](const char* p, const char* end, const char* token) -> bool {
        while (*token)
        {
            if (p >= end) return false;
            char c1 = *p;
            char c2 = *token;
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) return false;
            p++;
            token++;
        }
        return true;
    };

    const char* pStart = ascii.data();
    const char* pEnd = pStart + ascii.size();
    const char* pEngineBlock = nullptr;

    const char* p = pStart;
    while (p < pEnd)
    {
        if (MatchStr(p, pEnd, "engine"))
        {
            const char* pCheck = p + 6;
            while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
            if (pCheck < pEnd && *pCheck == '(')
            {
                pEngineBlock = pCheck + 1;
                break;
            }
        }
        p++;
    }

    const char* pSearch = pEngineBlock ? pEngineBlock : pStart;
    p = pSearch;
    while (p < pEnd)
    {
        if (MatchStr(p, pEnd, "type"))
        {
            const char* pCheck = p + 4;
            while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
            if (pCheck < pEnd && *pCheck == '(')
            {
                pCheck++;
                while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
                
                const char* valStart = pCheck;
                while (pCheck < pEnd && !IsSpace(*pCheck) && *pCheck != ')') pCheck++;
                
                std::string val(valStart, pCheck - valStart);
                for (char& c : val) if (c >= 'A' && c <= 'Z') c += 32;
                
                if (val == "diesel" || val == "electric" || val == "steam" || val == "control")
                {
                    if (val == "diesel") outType = L"Diesel";
                    else if (val == "electric") outType = L"Electric";
                    else if (val == "steam") outType = L"Steam";
                    else if (val == "control") outType = L"Control";
                    return true;
                }
            }
        }
        p++;
    }

    return false;
}
bool ParseWagonType(const std::wstring& filePath, std::wstring& outType)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }
    std::vector<char> buffer(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    std::vector<char> ascii;
    ascii.reserve(bytesRead);
    if (bytesRead >= 2 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE)
    {
        for (DWORD i = 2; i < bytesRead - 1; i += 2)
        {
            char c = buffer[i];
            if (c != 0) ascii.push_back(c);
        }
    }
    else
    {
        for (DWORD i = 0; i < bytesRead; i++)
        {
            char c = buffer[i];
            if (c != 0 && (unsigned char)c != 0xEF && (unsigned char)c != 0xBB && (unsigned char)c != 0xBF)
            {
                ascii.push_back(c);
            }
        }
    }

    auto IsSpace = [](char c) -> bool {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    auto MatchStr = [](const char* p, const char* end, const char* token) -> bool {
        while (*token)
        {
            if (p >= end) return false;
            char c1 = *p;
            char c2 = *token;
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) return false;
            p++;
            token++;
        }
        return true;
    };

    const char* pStart = ascii.data();
    const char* pEnd = pStart + ascii.size();
    const char* pWagonBlock = nullptr;

    const char* p = pStart;
    while (p < pEnd)
    {
        if (MatchStr(p, pEnd, "wagon"))
        {
            const char* pCheck = p + 5;
            while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
            if (pCheck < pEnd && *pCheck == '(')
            {
                pWagonBlock = pCheck + 1;
                break;
            }
        }
        p++;
    }

    const char* pSearch = pWagonBlock ? pWagonBlock : pStart;
    p = pSearch;
    while (p < pEnd)
    {
        if (MatchStr(p, pEnd, "type"))
        {
            const char* pCheck = p + 4;
            while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
            if (pCheck < pEnd && *pCheck == '(')
            {
                pCheck++;
                while (pCheck < pEnd && IsSpace(*pCheck)) pCheck++;
                
                const char* valStart = pCheck;
                while (pCheck < pEnd && !IsSpace(*pCheck) && *pCheck != ')') pCheck++;
                
                std::string val(valStart, pCheck - valStart);
                for (char& c : val) if (c >= 'A' && c <= 'Z') c += 32;
                
                if (val == "passenger" || val == "carriage" || val == "freight" || val == "wagon" || val == "tender")
                {
                    if (val == "passenger" || val == "carriage") outType = L"Passenger";
                    else if (val == "tender") outType = L"Tender";
                    else outType = L"Freight";
                    return true;
                }
            }
        }
        p++;
    }

    return false;
}

void ParseStockMetadata(const std::wstring& filePath, std::wstring& outName, std::wstring& outType, std::wstring& outPower, std::wstring& outMass, std::wstring& outCabView, int depth)
{
    if (depth > 5) return;

    std::string ascii = ReadConFileToAscii(filePath);
    if (ascii.empty()) return;

    if (outName.empty())
    {
        outName = ExtractTagValue(ascii, "name");
    }

    if (outType.empty())
    {
        size_t dotPos = filePath.find_last_of(L'.');
        std::wstring ext = (dotPos != std::wstring::npos) ? filePath.substr(dotPos) : L"";
        for (wchar_t& c : ext) c = towlower(c);

        if (ext == L".eng" || ext == L".inc")
        {
            ParseEnginePropulsionType(filePath, outType);
        }
        else if (ext == L".wag")
        {
            ParseWagonType(filePath, outType);
        }
        else
        {
            outType = ExtractTagValue(ascii, "type");
        }
    }

    if (outPower.empty())
    {
        outPower = ExtractTagValue(ascii, "maxpower");
    }

    if (outMass.empty())
    {
        outMass = ExtractTagValue(ascii, "mass");
    }

    if (outCabView.empty())
    {
        outCabView = ExtractTagValue(ascii, "cabview");
    }

    std::string lower = ascii;
    for (char& c : lower) c = tolower(c);

    size_t parentEndSlash = filePath.find_last_of(L"\\/");
    std::wstring parentDir = (parentEndSlash != std::wstring::npos) ? filePath.substr(0, parentEndSlash + 1) : L"";

    size_t posInclude = lower.find("include");
    while (posInclude != std::string::npos)
    {
        size_t idx = posInclude + 7;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            std::wstring incRelPath;
            if (idx < lower.length() && lower[idx] == '"')
            {
                idx++;
                size_t startInc = idx;
                size_t endInc = ascii.find('"', startInc);
                if (endInc != std::string::npos)
                {
                    std::string incStr = ascii.substr(startInc, endInc - startInc);
                    incRelPath = std::wstring(incStr.begin(), incStr.end());
                }
            }
            else
            {
                size_t startInc = idx;
                size_t endInc = lower.find_first_of(" \t\r\n)", startInc);
                if (endInc != std::string::npos)
                {
                    std::string incStr = ascii.substr(startInc, endInc - startInc);
                    incRelPath = std::wstring(incStr.begin(), incStr.end());
                }
            }

            if (!incRelPath.empty())
            {
                std::wstring incFullPath = ResolveRelativePath(parentDir, incRelPath);
                ParseStockMetadata(incFullPath, outName, outType, outPower, outMass, outCabView, depth + 1);
            }
        }
        posInclude = lower.find("include", posInclude + 1);
    }
}

static StockItem ParseStockFileToItem(
    const std::wstring& folderPath,
    const std::wstring& fileName,
    const std::wstring& ext,
    uint64_t lastWriteTime
)
{
    std::wstring fileFullPath = folderPath + L"\\" + fileName;
    std::wstring outName, outType, outPower, outMass, outCabView;
    ParseStockMetadata(fileFullPath, outName, outType, outPower, outMass, outCabView, 0);

    std::wstring category = L"";
    std::wstring typeLower = outType;
    for (wchar_t& c : typeLower) c = towlower(c);
    std::wstring details = L"";

    if (ext == L".eng")
    {
        if (typeLower.find(L"control") != std::wstring::npos || typeLower.find(L"cab") != std::wstring::npos)
        {
            category = L"Control";
            ParseControlDetails(outCabView, outMass, details);
        }
        else if (typeLower.find(L"diesel") != std::wstring::npos)
        {
            category = L"Diesel";
            ParseDieselDetails(outPower, outMass, details);
        }
        else if (typeLower.find(L"electric") != std::wstring::npos)
        {
            category = L"Electric";
            ParseElectricDetails(outPower, outMass, details);
        }
        else if (typeLower.find(L"steam") != std::wstring::npos)
        {
            category = L"Steam";
            ParseSteamDetails(outPower, outMass, details);
        }
        else
        {
            category = L"Diesel";
            ParseDieselDetails(outPower, outMass, details);
        }
    }
    else // .wag
    {
        if (typeLower.find(L"tender") != std::wstring::npos || fileName.find(L"tender") != std::wstring::npos || fileName.find(L"Tender") != std::wstring::npos)
        {
            category = L"Tender";
            ParseTenderDetails(outMass, details);
        }
        else if (typeLower.find(L"carriage") != std::wstring::npos || typeLower.find(L"passenger") != std::wstring::npos)
        {
            category = L"Passenger";
            ParsePassengerDetails(outMass, details);
        }
        else
        {
            category = L"Freight";
            ParseFreightDetails(outMass, details);
        }
    }

    size_t dotPos = fileName.find_last_of(L'.');
    std::wstring dispName = (dotPos != std::wstring::npos) ? fileName.substr(0, dotPos) : fileName;
    size_t lastSlash = folderPath.find_last_of(L"\\/");
    std::wstring parentFolder = (lastSlash != std::wstring::npos) ? folderPath.substr(lastSlash + 1) : L"";

    StockItem item;
    item.szFileName = dispName;
    item.szCategory = category;
    item.szFolder = parentFolder;
    item.szExtension = ext;
    item.szDetails = details;
    item.lastWriteTime = lastWriteTime;
    return item;
}

struct DiskStockEntry {
    std::wstring folderPath;
    std::wstring fileName;
    std::wstring ext;
    std::wstring parentFolder;
    std::wstring key;
    uint64_t diskTime = 0;
    bool isUpdate = false;
};

static void CollectStockFilesFast(
    const std::wstring& folderPath,
    std::vector<DiskStockEntry>& outFiles
)
{
    if (g_bCancelStockScan) return;

    std::wstring searchPattern = folderPath + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileExW(
        searchPattern.c_str(),
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_LARGE_FETCH
    );

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (g_bCancelStockScan) break;

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0)
                {
                    std::wstring subFolder = folderPath + L"\\" + ffd.cFileName;
                    CollectStockFilesFast(subFolder, outFiles);
                }
            }
            else
            {
                std::wstring fileName = ffd.cFileName;
                size_t dotPos = fileName.find_last_of(L'.');
                if (dotPos != std::wstring::npos)
                {
                    std::wstring ext = fileName.substr(dotPos);
                    for (wchar_t& c : ext) c = towlower(c);

                    if (ext == L".eng" || ext == L".wag")
                    {
                        std::wstring dispName = fileName.substr(0, dotPos);
                        size_t lastSlash = folderPath.find_last_of(L"\\/");
                        std::wstring parentFolder = (lastSlash != std::wstring::npos) ? folderPath.substr(lastSlash + 1) : L"";

                        uint64_t diskTime = ((uint64_t)ffd.ftLastWriteTime.dwHighDateTime << 32) | ffd.ftLastWriteTime.dwLowDateTime;

                        // Create lookup key: folder|name.ext (lowercase)
                        std::wstring key = parentFolder + L"|" + dispName + ext;
                        for (auto& c : key) c = towlower(c);

                        DiskStockEntry entry;
                        entry.folderPath = folderPath;
                        entry.fileName = ffd.cFileName;
                        entry.ext = std::move(ext);
                        entry.parentFolder = std::move(parentFolder);
                        entry.key = std::move(key);
                        entry.diskTime = diskTime;

                        outFiles.push_back(std::move(entry));
                    }
                }
            }
        } while (FindNextFileW(hFind, &ffd) != 0);

        FindClose(hFind);
    }
}

struct StockScanThreadParams {
    HWND hWndParent;
    std::wstring basePath;
    bool bForceRescan = false;
};

uint64_t GetTrainsetSignature(const std::wstring& basePath)
{
    std::wstring p = basePath;
    if (!p.empty() && p.back() != L'\\') {
        p += L'\\';
    }
    std::wstring trainsetPath = p + L"TRAINS\\TRAINSET";

    uint64_t signature = 0;

    // 1. Get root directory write time
    HANDLE hDir = CreateFileW(trainsetPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hDir != INVALID_HANDLE_VALUE) {
        FILETIME ftWrite;
        if (GetFileTime(hDir, NULL, NULL, &ftWrite)) {
            signature += ((uint64_t)ftWrite.dwHighDateTime << 32) | ftWrite.dwLowDateTime;
        }
        CloseHandle(hDir);
    }

    // 2. Sum up write times of all first-level subdirectories
    std::wstring searchPattern = trainsetPath + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0) {
                    uint64_t writeTime = ((uint64_t)ffd.ftLastWriteTime.dwHighDateTime << 32) | ffd.ftLastWriteTime.dwLowDateTime;
                    signature += writeTime;
                }
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    return signature;
}

std::wstring GetCacheFilePath()
{
    return DatabaseManager::GetDatabaseFilePath();
}

bool LoadStockCache(const std::wstring& basePath, uint64_t currentSig)
{
    UNREFERENCED_PARAMETER(basePath);
    EnterCriticalSection(&g_StockCacheCS);
    bool loaded = DatabaseManager::LoadStockCache(currentSig, g_StockCache);
    LeaveCriticalSection(&g_StockCacheCS);
    return loaded;
}

void SaveStockCache(const std::wstring& basePath, uint64_t signature)
{
    UNREFERENCED_PARAMETER(basePath);
    EnterCriticalSection(&g_StockCacheCS);
    DatabaseManager::SaveStockCache(signature, g_StockCache);
    LeaveCriticalSection(&g_StockCacheCS);
}

DWORD WINAPI StockScannerThreadProc(LPVOID lpParam)
{
    StockScanThreadParams* params = (StockScanThreadParams*)lpParam;
    HWND hWndParent = params->hWndParent;
    std::wstring basePath = params->basePath;
    bool bForceRescan = params->bForceRescan;
    delete params;

    if (!basePath.empty() && basePath.back() != L'\\')
    {
        basePath += L'\\';
    }

    uint64_t currentSig = GetTrainsetSignature(basePath);
    std::wstring trainsetPath = basePath + L"TRAINS\\TRAINSET";

    std::unordered_map<std::wstring, StockItem> dbMap;
    bool hasDbRecords = DatabaseManager::LoadStockMap(dbMap);

    // If signature matches, DB has records, and rescan is NOT forced -> Instant match (<30ms)
    if (!bForceRescan && hasDbRecords && !dbMap.empty())
    {
        std::wstring sigStr = DatabaseManager::GetSetting(L"TrainsetSignature", L"0");
        uint64_t dbSig = 0;
        try { dbSig = std::stoull(sigStr); } catch (...) { dbSig = 0; }

        if (dbSig == currentSig && currentSig != 0)
        {
            EnterCriticalSection(&g_StockCacheCS);
            g_StockCache.clear();
            g_StockCache.reserve(dbMap.size());
            for (auto& pair : dbMap)
            {
                g_StockCache.push_back(std::move(pair.second));
            }
            std::sort(g_StockCache.begin(), g_StockCache.end(), [](const StockItem& a, const StockItem& b) {
                return _wcsicmp(a.szFileName.c_str(), b.szFileName.c_str()) < 0;
            });
            size_t total = g_StockCache.size();
            LeaveCriticalSection(&g_StockCacheCS);

            LOG_INFO("Loaded %zu rolling stock items instantly from SQLite database.", total);
            PostMessageW(hWndParent, WM_STOCK_SCAN_COMPLETE, 0, 0);
            return 0;
        }
    }

    // Fast Kernel32 directory traverse across entire TRAINSET folder (~15-20ms)
    LOG_INFO("Performing fast Kernel32 Incremental Sync for '%ls'...", basePath.c_str());

    std::vector<DiskStockEntry> diskEntries;
    diskEntries.reserve(dbMap.empty() ? 10000 : dbMap.size() + 500);
    CollectStockFilesFast(trainsetPath, diskEntries);

    if (g_bCancelStockScan) return 0;

    std::unordered_set<std::wstring> visitedKeys;
    visitedKeys.reserve(diskEntries.size());

    std::vector<DiskStockEntry> toParseEntries;
    toParseEntries.reserve(diskEntries.size());

    std::vector<StockItem> currentCache;
    currentCache.reserve(diskEntries.size());

    // Instant differential categorization
    for (auto& entry : diskEntries)
    {
        visitedKeys.insert(entry.key);
        auto it = dbMap.find(entry.key);
        if (it != dbMap.end() && it->second.lastWriteTime == entry.diskTime && entry.diskTime != 0)
        {
            // 100% UNCHANGED: Instant match from SQLite map (0 file reads, 0 parsing)
            currentCache.push_back(it->second);
        }
        else
        {
            // NEW OR MODIFIED: Queue for multi-threaded parsing
            entry.isUpdate = (it != dbMap.end());
            toParseEntries.push_back(std::move(entry));
        }
    }

    std::vector<StockItem> toInsert;
    std::vector<StockItem> toUpdate;
    toInsert.reserve(toParseEntries.size());
    toUpdate.reserve(toParseEntries.size());

    // Initial progressive UI update with existing unchanged items
    if (!currentCache.empty())
    {
        EnterCriticalSection(&g_StockCacheCS);
        g_StockCache = currentCache;
        LeaveCriticalSection(&g_StockCacheCS);
        PostMessageW(hWndParent, WM_STOCK_SCAN_PROGRESS, (WPARAM)currentCache.size(), (LPARAM)diskEntries.size());
    }

    // Multi-threaded parallel parsing across all available CPU cores
    if (!toParseEntries.empty() && !g_bCancelStockScan)
    {
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
        if (numThreads > 16) numThreads = 16;
        if (numThreads > toParseEntries.size()) numThreads = (unsigned int)toParseEntries.size();

        std::atomic<size_t> nextIndex(0);
        std::atomic<size_t> parsedCount(0);
        std::mutex resultsMutex;
        uint64_t lastStreamTime = GetTickCount64();

        auto WorkerFunc = [&]() {
            std::vector<StockItem> localInserts;
            std::vector<StockItem> localUpdates;
            std::vector<StockItem> localCache;
            localInserts.reserve(128);
            localUpdates.reserve(128);
            localCache.reserve(128);

            while (!g_bCancelStockScan)
            {
                size_t idx = nextIndex.fetch_add(1);
                if (idx >= toParseEntries.size()) break;

                const auto& entry = toParseEntries[idx];
                StockItem item = ParseStockFileToItem(entry.folderPath, entry.fileName, entry.ext, entry.diskTime);
                if (entry.isUpdate)
                {
                    localUpdates.push_back(item);
                }
                else
                {
                    localInserts.push_back(item);
                }
                localCache.push_back(std::move(item));

                size_t done = parsedCount.fetch_add(1) + 1;

                if (localCache.size() >= 64 || done == toParseEntries.size())
                {
                    std::lock_guard<std::mutex> lk(resultsMutex);
                    toInsert.insert(toInsert.end(), std::make_move_iterator(localInserts.begin()), std::make_move_iterator(localInserts.end()));
                    toUpdate.insert(toUpdate.end(), std::make_move_iterator(localUpdates.begin()), std::make_move_iterator(localUpdates.end()));
                    currentCache.insert(currentCache.end(), std::make_move_iterator(localCache.begin()), std::make_move_iterator(localCache.end()));
                    localInserts.clear();
                    localUpdates.clear();
                    localCache.clear();

                    uint64_t now = GetTickCount64();
                    if (now - lastStreamTime >= 80)
                    {
                        lastStreamTime = now;
                        EnterCriticalSection(&g_StockCacheCS);
                        g_StockCache = currentCache;
                        LeaveCriticalSection(&g_StockCacheCS);
                        PostMessageW(hWndParent, WM_STOCK_SCAN_PROGRESS, (WPARAM)currentCache.size(), (LPARAM)diskEntries.size());
                    }
                }
            }

            if (!localCache.empty())
            {
                std::lock_guard<std::mutex> lk(resultsMutex);
                toInsert.insert(toInsert.end(), std::make_move_iterator(localInserts.begin()), std::make_move_iterator(localInserts.end()));
                toUpdate.insert(toUpdate.end(), std::make_move_iterator(localUpdates.begin()), std::make_move_iterator(localUpdates.end()));
                currentCache.insert(currentCache.end(), std::make_move_iterator(localCache.begin()), std::make_move_iterator(localCache.end()));
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(numThreads);
        for (unsigned int t = 0; t < numThreads; ++t)
        {
            workers.emplace_back(WorkerFunc);
        }

        for (auto& w : workers)
        {
            if (w.joinable()) w.join();
        }
    }

    if (!g_bCancelStockScan)
    {
        // Detect deletions (records in DB that no longer exist on physical disk)
        std::vector<std::pair<std::wstring, std::wstring>> toDelete;
        for (const auto& pair : dbMap)
        {
            if (visitedKeys.find(pair.first) == visitedKeys.end())
            {
                toDelete.push_back({ pair.second.szFolder, pair.second.szFileName });
            }
        }

        // Apply diff to SQLite database within a single fast atomic transaction
        if (!toInsert.empty() || !toUpdate.empty() || !toDelete.empty())
        {
            DatabaseManager::SyncStockDiff(toInsert, toUpdate, toDelete);
        }

        // Update signature in AppSettings
        DatabaseManager::SetSetting(L"TrainsetSignature", std::to_wstring(currentSig));

        // Sort and publish full cache to UI
        EnterCriticalSection(&g_StockCacheCS);
        std::sort(currentCache.begin(), currentCache.end(), [](const StockItem& a, const StockItem& b) {
            return _wcsicmp(a.szFileName.c_str(), b.szFileName.c_str()) < 0;
        });
        g_StockCache = std::move(currentCache);
        size_t totalScanned = g_StockCache.size();
        LeaveCriticalSection(&g_StockCacheCS);

        LOG_INFO("Stock Library Differential Sync complete: %zu items active (+%zu new, ~%zu updated, -%zu deleted).",
            totalScanned, toInsert.size(), toUpdate.size(), toDelete.size());

        PostMessageW(hWndParent, WM_STOCK_SCAN_COMPLETE, 0, 0);
    }

    return 0;
}

HANDLE StartStockScan(HWND hWndParent, const std::wstring& basePath, bool bForceRescan)
{
    g_bCancelStockScan = FALSE; // Reset cancel flag to start clean scan
    StockScanThreadParams* params = new StockScanThreadParams();
    params->hWndParent = hWndParent;
    params->basePath = basePath;
    params->bForceRescan = bForceRescan;
    return CreateThread(NULL, 0, StockScannerThreadProc, params, 0, NULL);
}

void CancelStockScan(HANDLE& hThread)
{
    if (hThread != NULL)
    {
        g_bCancelStockScan = TRUE;
        WaitForSingleObject(hThread, 500);
        CloseHandle(hThread);
        hThread = NULL;
    }
}


