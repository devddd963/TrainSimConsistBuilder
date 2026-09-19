#pragma once
#include <windows.h>
#include <string>
#include <vector>

#define WM_STOCK_SCAN_COMPLETE (WM_USER + 302)
#define WM_STOCK_SCAN_PROGRESS (WM_USER + 303)

struct StockItem {
    std::wstring szFileName;  // File name without extension (e.g. "YellowWDG-4")
    std::wstring szCategory;  // Diesel, Electric, Steam, Control, Freight, Passenger, Tender
    std::wstring szFolder;    // Parent folder name under TRAINSET (e.g. "WDG4")
    std::wstring szExtension; // ".eng" or ".wag"
    std::wstring szDetails;   // Category specific details
};

extern std::vector<StockItem> g_StockCache;
extern CRITICAL_SECTION g_StockCacheCS;
extern volatile BOOL g_bCancelScan;

// Low-level helper functions exported for category parsers
std::string ReadConFileToAscii(const std::wstring& filePath);
std::wstring ExtractTagValue(const std::string& ascii, const std::string& tag);
std::wstring ResolveRelativePath(const std::wstring& parentDir, const std::wstring& relPath);
bool ParseEnginePropulsionType(const std::wstring& filePath, std::wstring& outType);
bool ParseWagonType(const std::wstring& filePath, std::wstring& outType);

// Interface for starting/stopping background scans
uint64_t GetTrainsetSignature(const std::wstring& basePath);
bool LoadStockCache(const std::wstring& basePath, uint64_t currentSig);
void SaveStockCache(const std::wstring& basePath, uint64_t signature);
std::wstring GetCacheFilePath();

HANDLE StartStockScan(HWND hWndParent, const std::wstring& basePath);
void CancelStockScan(HANDLE& hThread);
