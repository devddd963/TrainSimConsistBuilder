#include "AssetsParser.h"
#include "DieselParser.h"
#include "ElectricParser.h"
#include "SteamParser.h"
#include "ControlParser.h"
#include "PassengerParser.h"
#include "FreightParser.h"
#include "TenderParser.h"
#include <sstream>
#include <algorithm>

std::vector<StockItem> g_StockCache;
CRITICAL_SECTION g_StockCacheCS;

extern volatile BOOL g_bCancelScan;

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

void ScanStockLibraryRecursive(const std::wstring& folderPath, HWND hWndParent, uint64_t& lastUpdateTime)
{
    if (g_bCancelScan) return;

    std::wstring searchPattern = folderPath + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (g_bCancelScan) break;

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0)
                {
                    std::wstring subFolder = folderPath + L"\\" + ffd.cFileName;
                    ScanStockLibraryRecursive(subFolder, hWndParent, lastUpdateTime);
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
                        std::wstring fileFullPath = folderPath + L"\\" + ffd.cFileName;
                        
                        std::wstring outName;
                        std::wstring outType;
                        std::wstring outPower;
                        std::wstring outMass;
                        std::wstring outCabView;

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

                        std::wstring dispName = fileName.substr(0, dotPos);
                        size_t lastSlash = folderPath.find_last_of(L"\\/");
                        std::wstring parentFolder = (lastSlash != std::wstring::npos) ? folderPath.substr(lastSlash + 1) : L"";

                        StockItem item;
                        item.szFileName = dispName;
                        item.szCategory = category;
                        item.szFolder = parentFolder;
                        item.szExtension = ext;
                        item.szDetails = details;

                        EnterCriticalSection(&g_StockCacheCS);
                        g_StockCache.push_back(item);
                        LeaveCriticalSection(&g_StockCacheCS);

                        uint64_t currentTime = GetTickCount64();
                        if (currentTime - lastUpdateTime >= 163)
                        {
                            lastUpdateTime = currentTime;
                            PostMessageW(hWndParent, WM_STOCK_SCAN_PROGRESS, 0, 0);
                        }
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
    wchar_t szExePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    std::wstring exePath = szExePath;
    size_t lastSlash = exePath.find_last_of(L"\\/");
    std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";
    
    std::wstring appDataDir = dir + L"AppData";
    CreateDirectoryW(appDataDir.c_str(), NULL);
    
    return appDataDir + L"\\stock_cache.dat";
}

bool LoadStockCache(const std::wstring& basePath, uint64_t currentSig)
{
    std::wstring cachePath = GetCacheFilePath();
    HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesRead = 0;
    uint32_t magic = 0;
    if (!ReadFile(hFile, &magic, sizeof(magic), &bytesRead, NULL) || magic != 0x53544348) {
        CloseHandle(hFile);
        return false;
    }

    uint64_t cachedSig = 0;
    if (!ReadFile(hFile, &cachedSig, sizeof(cachedSig), &bytesRead, NULL)) {
        CloseHandle(hFile);
        return false;
    }

    if (cachedSig != currentSig) {
        CloseHandle(hFile);
        return false; // Stale cache
    }

    uint32_t itemCount = 0;
    if (!ReadFile(hFile, &itemCount, sizeof(itemCount), &bytesRead, NULL)) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<StockItem> tempCache;
    tempCache.reserve(itemCount);

    auto ReadString = [&](std::wstring& outStr) -> bool {
        uint32_t len = 0;
        if (!ReadFile(hFile, &len, sizeof(len), &bytesRead, NULL)) return false;
        if (len == 0) {
            outStr.clear();
            return true;
        }
        std::vector<wchar_t> buf(len);
        if (!ReadFile(hFile, buf.data(), len * sizeof(wchar_t), &bytesRead, NULL)) return false;
        outStr.assign(buf.begin(), buf.end());
        return true;
    };

    for (uint32_t i = 0; i < itemCount; ++i) {
        StockItem item;
        if (!ReadString(item.szFileName) ||
            !ReadString(item.szCategory) ||
            !ReadString(item.szFolder) ||
            !ReadString(item.szExtension) ||
            !ReadString(item.szDetails)) {
            CloseHandle(hFile);
            return false;
        }
        tempCache.push_back(item);
    }

    CloseHandle(hFile);

    EnterCriticalSection(&g_StockCacheCS);
    g_StockCache = std::move(tempCache);
    LeaveCriticalSection(&g_StockCacheCS);

    return true;
}

void SaveStockCache(const std::wstring& basePath, uint64_t signature)
{
    std::wstring cachePath = GetCacheFilePath();
    HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD bytesWritten = 0;
    uint32_t magic = 0x53544348; // "STCH"
    WriteFile(hFile, &magic, sizeof(magic), &bytesWritten, NULL);
    WriteFile(hFile, &signature, sizeof(signature), &bytesWritten, NULL);

    EnterCriticalSection(&g_StockCacheCS);
    uint32_t itemCount = (uint32_t)g_StockCache.size();
    WriteFile(hFile, &itemCount, sizeof(itemCount), &bytesWritten, NULL);

    auto WriteString = [&](const std::wstring& str) {
        uint32_t len = (uint32_t)str.length();
        WriteFile(hFile, &len, sizeof(len), &bytesWritten, NULL);
        if (len > 0) {
            WriteFile(hFile, str.c_str(), len * sizeof(wchar_t), &bytesWritten, NULL);
        }
    };

    for (const auto& item : g_StockCache) {
        WriteString(item.szFileName);
        WriteString(item.szCategory);
        WriteString(item.szFolder);
        WriteString(item.szExtension);
        WriteString(item.szDetails);
    }
    LeaveCriticalSection(&g_StockCacheCS);

    CloseHandle(hFile);
}

DWORD WINAPI StockScannerThreadProc(LPVOID lpParam)
{
    StockScanThreadParams* params = (StockScanThreadParams*)lpParam;
    HWND hWndParent = params->hWndParent;
    std::wstring basePath = params->basePath;
    delete params;

    if (!basePath.empty() && basePath.back() != L'\\')
    {
        basePath += L'\\';
    }

    // Try loading from cache first
    uint64_t currentSig = GetTrainsetSignature(basePath);
    if (LoadStockCache(basePath, currentSig))
    {
        PostMessageW(hWndParent, WM_STOCK_SCAN_COMPLETE, 0, 0);
        return 0;
    }

    std::wstring trainsetPath = basePath + L"TRAINS\\TRAINSET";

    EnterCriticalSection(&g_StockCacheCS);
    g_StockCache.clear();
    LeaveCriticalSection(&g_StockCacheCS);

    uint64_t lastUpdateTime = GetTickCount64();
    ScanStockLibraryRecursive(trainsetPath, hWndParent, lastUpdateTime);

    if (!g_bCancelScan)
    {
        EnterCriticalSection(&g_StockCacheCS);
        std::sort(g_StockCache.begin(), g_StockCache.end(), [](const StockItem& a, const StockItem& b) {
            return _wcsicmp(a.szFileName.c_str(), b.szFileName.c_str()) < 0;
        });
        LeaveCriticalSection(&g_StockCacheCS);

        // Save to cache file for next run
        SaveStockCache(basePath, currentSig);

        PostMessageW(hWndParent, WM_STOCK_SCAN_COMPLETE, 0, 0);
    }

    return 0;
}

HANDLE StartStockScan(HWND hWndParent, const std::wstring& basePath)
{
    StockScanThreadParams* params = new StockScanThreadParams();
    params->hWndParent = hWndParent;
    params->basePath = basePath;
    return CreateThread(NULL, 0, StockScannerThreadProc, params, 0, NULL);
}

void CancelStockScan(HANDLE& hThread)
{
    if (hThread != NULL)
    {
        g_bCancelScan = TRUE;
        WaitForSingleObject(hThread, 200);
        CloseHandle(hThread);
        hThread = NULL;
    }
}
