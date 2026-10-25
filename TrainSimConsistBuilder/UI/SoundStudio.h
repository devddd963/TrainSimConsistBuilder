#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "../SRC/ConsistReader.h"
#include "../SRC/AssetsParser.h"

#define IDC_RSS_UNITS_LIST   8201
#define IDC_RSS_COLLAPSE_BTN 8202
#define IDC_RSS_CANVAS       8203

// Notification sent when switching between Consist Sound Matrix (0) and SMS Editor (1)
#define WM_SOUNDSTUDIO_SUBTABCHANGED (WM_USER + 315)

struct ScannedSoundItem;

enum class StockFileEncoding {
    Utf16LeBom,
    Utf8Bom,
    Utf8NoBom,
    Ansi
};

enum class IncludeSoundAction {
    PromptUser,      // Pop modern dialog to ask
    ModifyInInclude, // Update directly in the .inc file (Pattern 1 & Pattern 2)
    OverrideInParent,// Insert Sound above include in .eng / .wag
    Cancel           // Abort save
};

enum class SoundMatrixViewMode {
    GridView = 0,
    DistinctCards = 1
};

struct RSSSoundEntry {
    std::wstring soundType;     // "Wagon", "Engine", "ORTS3DCab", "Inside"
    std::wstring smsFileName;   // e.g. "3PH_External_5.sms"
    std::wstring parentFolder;  // e.g. "common.bgpro\Sounds\3Phaser"
    std::wstring fullDiskPath;  // Full absolute path on disk
    std::wstring rawToken;      // Raw text from .eng / .wag / .inc file
    bool existsOnDisk = false;

    // Include source tracking
    std::wstring sourceFilePath;          // File where Sound ( ) physically lives
    bool isFromInclude = false;            // True if sourceFilePath != stock root file
    std::wstring incDirectiveString;      // e.g. "./INCS/cab_sounds.inc"
    bool isBlockEnclosedInParent = false; // true = Pattern 1 (block opened in parent), false = Pattern 2

    bool operator==(const RSSSoundEntry& o) const {
        return _wcsicmp(smsFileName.c_str(), o.smsFileName.c_str()) == 0 &&
               _wcsicmp(parentFolder.c_str(), o.parentFolder.c_str()) == 0 &&
               _wcsicmp(fullDiskPath.c_str(), o.fullDiskPath.c_str()) == 0 &&
               _wcsicmp(sourceFilePath.c_str(), o.sourceFilePath.c_str()) == 0;
    }
    bool operator!=(const RSSSoundEntry& o) const { return !(*this == o); }
};

struct RSSSlotIncludeInfo {
    bool hasIncludeSound = false;
    std::wstring sourceFilePath;
    std::wstring incDirectiveString;
    bool isBlockEnclosedInParent = false;
};

struct RSSUnitSoundInfo {
    int index = 0;
    std::wstring name;
    std::wstring type;
    std::wstring folder;
    std::wstring ext;
    std::wstring fullPath;
    bool fileFound = true;
    bool isExpanded = false;

    std::vector<RSSSoundEntry> wagonSounds;
    std::vector<RSSSoundEntry> engineSounds;
    std::vector<RSSSoundEntry> orts3dCabSounds;
    std::vector<RSSSoundEntry> insideSounds;

    RSSSlotIncludeInfo wagonIncInfo;
    RSSSlotIncludeInfo engineIncInfo;
    RSSSlotIncludeInfo orts3dIncInfo;
    RSSSlotIncludeInfo insideIncInfo;

    std::wstring wagonCell;
    std::wstring engineCell;
    std::wstring orts3dCell;
    std::wstring insideCell;
    std::wstring statusCell;

    int missingCount = 0;
    int soundCount = 0;

    bool hasORTS3DCabBlock = false;
    bool hasInsideBlock = false;
    bool isModified = false;
    StockFileEncoding originalEncoding = StockFileEncoding::Utf16LeBom;

    bool operator==(const RSSUnitSoundInfo& o) const {
        return wagonSounds == o.wagonSounds &&
               engineSounds == o.engineSounds &&
               orts3dCabSounds == o.orts3dCabSounds &&
               insideSounds == o.insideSounds;
    }
    bool operator!=(const RSSUnitSoundInfo& o) const { return !(*this == o); }
};

struct RSSSoundClipboard {
    bool hasEntry = false;
    RSSSoundEntry entry;
    std::wstring sourceUnitName;
    std::wstring sourceUnitFolder;
    std::wstring sourceUnitType;
};

RSSSoundClipboard& SoundStudio_GetSmsClipboard();
void SoundStudio_SetSmsClipboard(const RSSSoundEntry& entry, const std::wstring& unitName = L"", const std::wstring& unitFolder = L"", const std::wstring& unitType = L"");
void SoundStudio_ClearSmsClipboard();

HWND CreateSoundStudio(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id);
void SoundStudio_SetConsist(HWND hWnd, const std::wstring& consistName, const std::wstring& basePath, const std::vector<ConsistReader::UnitInfo>& units);
void SoundStudio_SetDirectStock(HWND hWnd, const StockItem& item, const std::wstring& basePath);
void SoundStudio_SetDirectSms(HWND hWnd, const std::wstring& smsFilePath, const std::wstring& basePath);
void SoundStudio_SetDarkMode(HWND hWnd, BOOL bDark);
void SoundStudio_SetVisible(HWND hWnd, bool bVisible);
void SoundStudio_Resize(HWND hWnd, int x, int y, int w, int h);
bool SoundStudio_HandleDragHover(HWND hWnd, POINT ptScreen, const struct ScannedSoundItem* pItem, std::wstring& outActionText);
void SoundStudio_ClearDragHover(HWND hWnd);
bool SoundStudio_HandleDrop(HWND hWnd, POINT ptScreen, const struct ScannedSoundItem& item);
HWND SoundStudio_GetListHwnd(HWND hWnd);
bool SoundStudio_SaveAllChanges(HWND hWnd);
bool SoundStudio_SaveUnitChanges(HWND hWnd, int unitIndex);
bool SoundStudio_HasUnsavedChanges(HWND hWnd);
std::vector<std::wstring> SoundStudio_GetDirtyUnitNames(HWND hWnd);
std::wstring SoundStudio_GetDirtySmsFile(HWND hWnd);
bool SoundStudio_SaveCurrentSms(HWND hWnd);
void SoundStudio_OpenSmsInEditor(HWND hWnd, const std::wstring& smsFilePath);
void SoundStudio_SwitchSubTab(HWND hWnd, int subTab); // 0 = Consist Sound Matrix, 1 = SMS Editor
int SoundStudio_GetActiveSubTab(HWND hWnd);

// Sound parsing & file mutation engine
bool SoundStudio_ParseUnitSound(const std::wstring& filePath, const std::wstring& stockFolder, const std::wstring& basePath, RSSUnitSoundInfo& outInfo);
void SoundStudio_ClearStockSoundCache();
bool SoundStudio_UpdateUnitSoundInStockFile(
    RSSUnitSoundInfo& u,
    const std::wstring& basePath,
    HWND hWndParent = NULL,
    IncludeSoundAction defaultIncAction = IncludeSoundAction::PromptUser,
    IncludeSoundAction* pOutChosenAction = nullptr,
    bool* pOutApplyToAll = nullptr);

// Backward-compatibility aliases
inline HWND CreateRollingStockStudio(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id) { return CreateSoundStudio(hParent, hInstance, x, y, w, h, id); }
inline void RollingStockStudio_SetConsist(HWND hWnd, const std::wstring& consistName, const std::wstring& basePath, const std::vector<ConsistReader::UnitInfo>& units) { SoundStudio_SetConsist(hWnd, consistName, basePath, units); }
inline void RollingStockStudio_SetDirectStock(HWND hWnd, const StockItem& item, const std::wstring& basePath) { SoundStudio_SetDirectStock(hWnd, item, basePath); }
inline void RollingStockStudio_SetDirectSms(HWND hWnd, const std::wstring& smsFilePath, const std::wstring& basePath) { SoundStudio_SetDirectSms(hWnd, smsFilePath, basePath); }
inline void RollingStockStudio_SetDarkMode(HWND hWnd, BOOL bDark) { SoundStudio_SetDarkMode(hWnd, bDark); }
inline void RollingStockStudio_SetVisible(HWND hWnd, bool bVisible) { SoundStudio_SetVisible(hWnd, bVisible); }
inline void RollingStockStudio_Resize(HWND hWnd, int x, int y, int w, int h) { SoundStudio_Resize(hWnd, x, y, w, h); }
inline bool RollingStockStudio_HandleDragHover(HWND hWnd, POINT ptScreen, const struct ScannedSoundItem* pItem, std::wstring& outActionText) { return SoundStudio_HandleDragHover(hWnd, ptScreen, pItem, outActionText); }
inline void RollingStockStudio_ClearDragHover(HWND hWnd) { SoundStudio_ClearDragHover(hWnd); }
inline bool RollingStockStudio_HandleDrop(HWND hWnd, POINT ptScreen, const struct ScannedSoundItem& item) { return SoundStudio_HandleDrop(hWnd, ptScreen, item); }
inline HWND RollingStockStudio_GetListHwnd(HWND hWnd) { return SoundStudio_GetListHwnd(hWnd); }
inline int RollingStockStudio_GetActiveSubTab(HWND hWnd) { return SoundStudio_GetActiveSubTab(hWnd); }
inline void RollingStockStudio_OpenSmsInEditor(HWND hWnd, const std::wstring& smsFilePath) { SoundStudio_OpenSmsInEditor(hWnd, smsFilePath); }
