#include "SoundStudio.h"

#include "SmsEditorView.h"

#include "CustomListControl.h"

#include "ModernContextMenu.h"

#include "ModernMessageBox.h"

#include "UITheme.h"

#include "SmsPoolManagerDlg.h"

#include "SmsStreamMutatorDlg.h"

#include "../SRC/DatabaseManager.h"

#include "../SRC/AppLogging.h"

#include "../SRC/SoundScanner.h"

#include "../SRC/SmsParser.h"

#include "../SRC/TrainSimConsistBuilder.h"

#include <windowsx.h>

#include <commdlg.h>

#include <mmsystem.h>

#include <shellapi.h>

#include <algorithm>

#include <sstream>

#include <unordered_set>



#pragma comment(lib, "winmm.lib")



static RSSSoundClipboard s_smsClipboard;



RSSSoundClipboard& SoundStudio_GetSmsClipboard()

{

    return s_smsClipboard;

}



void SoundStudio_SetSmsClipboard(const RSSSoundEntry& entry, const std::wstring& unitName, const std::wstring& unitFolder, const std::wstring& unitType)

{

    s_smsClipboard.hasEntry = true;

    s_smsClipboard.entry = entry;

    s_smsClipboard.sourceUnitName = unitName;

    s_smsClipboard.sourceUnitFolder = unitFolder;

    s_smsClipboard.sourceUnitType = unitType;

}



void SoundStudio_ClearSmsClipboard()

{

    s_smsClipboard.hasEntry = false;

    s_smsClipboard.entry = RSSSoundEntry();

    s_smsClipboard.sourceUnitName.clear();

    s_smsClipboard.sourceUnitFolder.clear();

    s_smsClipboard.sourceUnitType.clear();



}





enum RSSRowType {

    RSS_ROW_UNIT,

    RSS_ROW_STREAM,

    RSS_ROW_ADD_BUTTON

};



struct RSSDisplayRow {

    RSSRowType rowType = RSS_ROW_UNIT;

    int unitIndex = -1;

    std::wstring category; // "Wagon", "Engine", "ORTS3DCab", "Inside"

    int streamIndex = -1;

};



struct RSSSlotHitInfo {

    enum SlotType {

        None,

        WagonExterior,

        WagonInside,

        WagonOrts3D,

        EngineCab

    };

    SlotType slotType = None;

    size_t distinctIndex = 0;

    int streamIndex = -1;

    bool isActionBtn = false;

    bool isPlayBtn = false;

    bool isDeleteBtn = false;

    bool isAddStreamBtn = false;

    RECT rc = { 0 };

};



struct RSSDistinctVehicle {

    std::wstring key; // folder + L"\\" + name + ext (lowercase)

    std::wstring name;

    std::wstring folder;

    std::wstring type;

    std::wstring ext;

    std::wstring fullPath;

    bool fileFound = true;

    bool isEngine = false;

    bool hasORTS3DCabBlock = false;

    bool hasInsideBlock = false;

    bool isModified = false;



    std::vector<RSSSoundEntry> wagonSounds;

    std::vector<RSSSoundEntry> engineSounds;

    std::vector<RSSSoundEntry> orts3dCabSounds;

    std::vector<RSSSoundEntry> insideSounds;



    std::vector<int> unitIndices;

    int firstUnitIndex = 0;



    // Collapsible states

    bool isCardExpanded = true;

    bool isWagonSectionExpanded = true;

    bool isEngineSectionExpanded = true;



    // Layout hit-test rects

    RECT rcCard = { 0 };

    RECT rcHeader = { 0 };

    RECT rcChevron = { 0 };

    RECT rcBtnOpenStock = { 0 };

    RECT rcBtnSave = { 0 };



    RECT rcWagonSectionHdr = { 0 };

    RECT rcWagonSectionChevron = { 0 };

    RECT rcWagonExteriorTile = { 0 };

    RECT rcWagonExteriorBtn = { 0 };

    RECT rcWagonInsideTile = { 0 };

    RECT rcWagonInsideBtn = { 0 };

    RECT rcWagonOrtsTile = { 0 };

    RECT rcWagonOrtsBtn = { 0 };



    RECT rcEngineSectionHdr = { 0 };

    RECT rcEngineSectionChevron = { 0 };

    RECT rcEngineCabTile = { 0 };

    RECT rcEngineCabBtn = { 0 };

};



struct SoundStudioHistoryStep {
    std::wstring actionDesc;
    std::vector<RSSUnitSoundInfo> unitsSoundData;
};

struct RollingStockStudioState {

    HWND hWnd = NULL;

    HWND hParent = NULL;

    HINSTANCE hInstance = NULL;



    CustomListControl soundMatrixList; // Left Consist Rake Navigator

    HWND hSoundMatrixListWnd = NULL;



    // Right Distinct Cards Studio Canvas

    HWND hCanvasWnd = NULL;

    CustomScrollBar canvasScrollBar;

    int canvasScrollY = 0;

    int canvasTotalHeight = 0;

    RECT rcCanvas = { 0 };

    std::vector<RSSSlotHitInfo> canvasHitRegions;

    POINT canvasMousePos = { -1, -1 };

    bool canvasMouseTracking = false;



    // Canvas Drag-Hover Target

    int dragHoverDistinctIndex = -1;

    RSSSlotHitInfo::SlotType dragHoverSlotType = RSSSlotHitInfo::None;

    int dragHoverStreamIndex = -1;

    bool dragHoverIsAddStream = false;



    std::vector<RSSDistinctVehicle> distinctVehicles;

    size_t selectedDistinctIndex = 0;



    const RSSSoundEntry* selectedInspectorEntry = nullptr;

    std::wstring selectedInspectorSlotCategory;

    std::wstring selectedInspectorUnitName;

    std::wstring selectedInspectorUnitFolder;

    std::wstring selectedInspectorUnitType;



    // Sub-Tab Navigation

    int activeSubTab = 0; // 0 = Consist Sound Matrix, 1 = SMS Editor

    HWND hSmsEditorWnd = NULL;

    RECT rcSubTabMatrix = { 0 };

    RECT rcSubTabEditor = { 0 };

    int hoverSubTab = -1; // 0 = Matrix, 1 = Editor

    SoundMatrixViewMode matrixViewMode = SoundMatrixViewMode::GridView;
    RECT rcBtnViewGrid = { 0 };
    RECT rcBtnViewCards = { 0 };

    std::vector<RSSUnitSoundInfo> unitsSoundData;
    std::vector<RSSUnitSoundInfo> savedUnitsSoundData;

    std::vector<RSSDisplayRow> displayRows;

    std::wstring consistName;

    std::wstring basePath;



    bool isDirectStockMode = false;

    StockItem directStockItem;

    bool isDirectSmsMode = false;

    std::wstring directSmsPath;



    int totalConsistUnits = 0;

    int totalMissingSounds = 0;

    int totalSoundStreams = 0;



    HFONT hFontTitle = NULL;

    HFONT hFontHeader = NULL;

    HFONT hFontBody = NULL;

    HFONT hFontBold = NULL;

    HFONT hFontMono = NULL;

    HFONT hFontIcons = NULL;

    HFONT hFontSmallIcons = NULL;



    RECT rcBtnExpandAll = { 0 };

    RECT rcBtnCollapseAll = { 0 };

    int hoverHeaderBtn = -1; // 0 = ExpandAll, 1 = CollapseAll

    RECT rcBtnUndo = { 0 };

    RECT rcBtnRedo = { 0 };

    int hoverUndoRedoBtn = -1; // 1 = Undo, 2 = Redo

    std::vector<SoundStudioHistoryStep> undoStack;

    std::vector<SoundStudioHistoryStep> redoStack;

    RECT rcConsistBanner = { 0 };



    RECT rcBtnSave = { 0 };

    bool hoverSaveBtn = false;

    bool isModified = false;



    int dragHoverRow = -1;

    int dragHoverCol = -1;



    // Inspector & Audio Preview Deck

    RECT rcTableCard = { 0 };

    RECT rcInspectorCard = { 0 };

    RECT rcInspectorHeader = { 0 };



    RECT rcBtnPlayAudio = { 0 };

    RECT rcBtnStopAudio = { 0 };

    RECT rcBtnOpenEditor = { 0 };

    RECT rcBtnRevealFolder = { 0 };

    RECT rcBtnOpenStock = { 0 };



    int hoverInspectorBtn = -1; // 0 = Play/Stop, 2 = OpenEditor, 3 = RevealFolder, 4 = OpenStock



    // Audio preview state

    bool isAudioPlaying = false;



    // Cached inspector info

    std::wstring cachedSmsPath;

    SmsDocument cachedSmsDoc;

    bool cachedSmsLoaded = false;

    std::wstring cachedPrimaryWav;

    bool cachedWavExists = false;



    BOOL bDarkMode = TRUE;

};

// Forward declarations for matrix and studio helper functions
static bool s_bSuppressMultiSmsWarning = false;
static RSSSoundEntry CreateSoundEntryFromText(const std::wstring& rawText, const std::wstring& category, const std::wstring& basePath);
static RSSSoundEntry CreateSoundEntryFromFullPath(const std::wstring& fullPath, const std::wstring& category, const std::wstring& basePath);
static void PopulateSoundMatrix(RollingStockStudioState* pState, int preferSelUnitIndex = -1);
static void RepositionStudioControls(RollingStockStudioState* pState);
static int GetDistinctCardHeight(const RSSDistinctVehicle& dv);
static void HandleAddDistinctButtonClick(RollingStockStudioState* pState, size_t distinctIdx, const std::wstring& slotCategory, int targetStreamIdx);
static void HandleDeleteDistinctStream(RollingStockStudioState* pState, size_t distinctIdx, const std::wstring& slotCategory, size_t streamIdx);
static void RSS_PushUndo(RollingStockStudioState* pState, const std::wstring& actionDesc);
static void RSS_PerformUndo(RollingStockStudioState* pState);
static void RSS_PerformRedo(RollingStockStudioState* pState);




static const wchar_t* RSS_WNDCLASS = L"RollingStockStudioPane";



static std::wstring ReadStockFileToWString(const std::wstring& filePath, StockFileEncoding* pOutEncoding = nullptr)

{

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) return L"";



    DWORD fileSize = GetFileSize(hFile, NULL);

    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)

    {

        CloseHandle(hFile);

        return L"";

    }



    std::vector<unsigned char> buffer(fileSize);

    DWORD bytesRead = 0;

    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL) || bytesRead == 0)

    {

        CloseHandle(hFile);

        return L"";

    }

    CloseHandle(hFile);

    buffer.resize(bytesRead);



    // 1. UTF-16 LE with BOM (0xFF, 0xFE)

    if (buffer.size() >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE)

    {

        if (pOutEncoding) *pOutEncoding = StockFileEncoding::Utf16LeBom;

        size_t numWChars = (buffer.size() - 2) / 2;

        const wchar_t* pWChars = (const wchar_t*)(buffer.data() + 2);

        return std::wstring(pWChars, numWChars);

    }



    // 2. UTF-16 LE without BOM

    if (buffer.size() >= 4 && buffer[1] == 0x00 && buffer[3] == 0x00)

    {

        if (pOutEncoding) *pOutEncoding = StockFileEncoding::Utf16LeBom;

        size_t numWChars = buffer.size() / 2;

        const wchar_t* pWChars = (const wchar_t*)buffer.data();

        return std::wstring(pWChars, numWChars);

    }



    // 3. UTF-8 with BOM (0xEF, 0xBB, 0xBF)

    const unsigned char* pData = buffer.data();

    size_t dataLen = buffer.size();

    bool hasUtf8Bom = false;

    if (dataLen >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF)

    {

        hasUtf8Bom = true;

        pData += 3;

        dataLen -= 3;

    }



    // Try UTF-8 conversion

    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)pData, (int)dataLen, NULL, 0);

    if (wlen > 0)

    {

        if (pOutEncoding) *pOutEncoding = hasUtf8Bom ? StockFileEncoding::Utf8Bom : StockFileEncoding::Utf8NoBom;

        std::wstring ws(wlen, L'\0');

        MultiByteToWideChar(CP_UTF8, 0, (const char*)pData, (int)dataLen, &ws[0], wlen);

        return ws;

    }



    // Fallback to ANSI (CP_ACP)

    wlen = MultiByteToWideChar(CP_ACP, 0, (const char*)pData, (int)dataLen, NULL, 0);

    if (wlen > 0)

    {

        if (pOutEncoding) *pOutEncoding = StockFileEncoding::Ansi;

        std::wstring ws(wlen, L'\0');

        MultiByteToWideChar(CP_ACP, 0, (const char*)pData, (int)dataLen, &ws[0], wlen);

        return ws;

    }



    return L"";

}



static void ResolveSoundFile(

    const std::wstring& rawToken,

    const std::wstring& stockFolder,

    const std::wstring& basePath,

    RSSSoundEntry& outEntry)

{

    std::wstring clean = rawToken;

    while (!clean.empty() && (clean.front() == L' ' || clean.front() == L'\t' || clean.front() == L'"' || clean.front() == L'\''))

        clean.erase(clean.begin());

    while (!clean.empty() && (clean.back() == L' ' || clean.back() == L'\t' || clean.back() == L'"' || clean.back() == L'\''))

        clean.pop_back();



    for (wchar_t& ch : clean)

    {

        if (ch == L'/') ch = L'\\';

    }



    outEntry.rawToken = clean;



    size_t lastSlash = clean.find_last_of(L"\\");

    std::wstring fileName = (lastSlash != std::wstring::npos) ? clean.substr(lastSlash + 1) : clean;

    outEntry.smsFileName = fileName;



    std::wstring normalizedBase = basePath;

    if (!normalizedBase.empty() && normalizedBase.back() != L'\\') normalizedBase += L'\\';



    std::wstring resolved;

    bool found = false;



    if (lastSlash != std::wstring::npos)

    {

        // 1. Open Rails standard: reference anchor is <StockFolder>\sound

        std::wstring anchor = stockFolder;

        if (!anchor.empty() && anchor.back() != L'\\') anchor += L'\\';

        anchor += L"sound";

        std::wstring c1 = ResolveRelativePath(anchor, clean);

        if (GetFileAttributesW(c1.c_str()) != INVALID_FILE_ATTRIBUTES)

        {

            resolved = c1;

            found = true;

        }



        // 2. Fallback directly from stockFolder

        if (!found)

        {

            std::wstring c2 = ResolveRelativePath(stockFolder, clean);

            if (GetFileAttributesW(c2.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c2;

                found = true;

            }

        }



        // 3. Fallback from TRAINSET folder

        if (!found)

        {

            std::wstring trainsetDir = normalizedBase + L"TRAINS\\TRAINSET";

            std::wstring c3 = ResolveRelativePath(trainsetDir, clean);

            if (GetFileAttributesW(c3.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c3;

                found = true;

            }

        }



        // 4. Fallback from MSTS basePath

        if (!found)

        {

            std::wstring c4 = ResolveRelativePath(normalizedBase, clean);

            if (GetFileAttributesW(c4.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c4;

                found = true;

            }

        }



        if (!found)

        {

            resolved = c1;

        }

    }

    else

    {

        // Simple filename without directory:

        // 1. <stockFolder>\sound\<file>

        std::wstring c1 = stockFolder;

        if (!c1.empty() && c1.back() != L'\\') c1 += L'\\';

        c1 += L"sound\\" + clean;

        if (GetFileAttributesW(c1.c_str()) != INVALID_FILE_ATTRIBUTES)

        {

            resolved = c1;

            found = true;

        }



        // 2. <stockFolder>\<file>

        if (!found)

        {

            std::wstring c2 = stockFolder;

            if (!c2.empty() && c2.back() != L'\\') c2 += L'\\';

            c2 += clean;

            if (GetFileAttributesW(c2.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c2;

                found = true;

            }

        }



        // 3. TRAINS\TRAINSET\common.sound\<file>

        if (!found)

        {

            std::wstring c3 = normalizedBase + L"TRAINS\\TRAINSET\\common.sound\\" + clean;

            if (GetFileAttributesW(c3.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c3;

                found = true;

            }

        }



        // 4. Global MSTS SOUND folder: SOUND\<file>

        if (!found)

        {

            std::wstring c4 = normalizedBase + L"SOUND\\" + clean;

            if (GetFileAttributesW(c4.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                resolved = c4;

                found = true;

            }

        }



        if (!found)

        {

            resolved = c1;

        }

    }



    outEntry.fullDiskPath = resolved;

    outEntry.existsOnDisk = found;



    // Friendly parent folder computation

    size_t rSlash = resolved.find_last_of(L"\\");

    std::wstring dirPath = (rSlash != std::wstring::npos) ? resolved.substr(0, rSlash) : L"";



    std::wstring trainsetPrefix = normalizedBase + L"TRAINS\\TRAINSET\\";

    std::wstring soundPrefix = normalizedBase + L"SOUND\\";



    auto startsWithNoCase = [](const std::wstring& str, const std::wstring& prefix) -> bool {

        if (str.length() < prefix.length()) return false;

        return _wcsnicmp(str.c_str(), prefix.c_str(), prefix.length()) == 0;

    };



    if (startsWithNoCase(dirPath, trainsetPrefix))

    {

        outEntry.parentFolder = dirPath.substr(trainsetPrefix.length());

    }

    else if (startsWithNoCase(dirPath, soundPrefix))

    {

        outEntry.parentFolder = L"SOUND";

    }

    else if (startsWithNoCase(dirPath, normalizedBase))

    {

        outEntry.parentFolder = dirPath.substr(normalizedBase.length());

    }

    else

    {

        size_t prevSlash = dirPath.find_last_of(L"\\");

        if (prevSlash != std::wstring::npos)

            outEntry.parentFolder = dirPath.substr(prevSlash + 1);

        else

            outEntry.parentFolder = dirPath;

    }

}



static void ParseStockSoundBlocksAndIncludes(

    const std::wstring& filePath,

    const std::wstring& stockFolder,

    const std::wstring& basePath,

    RSSUnitSoundInfo& outInfo,

    std::unordered_set<std::wstring>& visitedIncludes,

    int depth,

    const std::wstring& currentIncDirective = L"",

    bool currentEnclosedInParent = false)

{

    if (depth > 5) return;



    std::wstring normPath = filePath;

    std::transform(normPath.begin(), normPath.end(), normPath.begin(), ::towlower);

    if (visitedIncludes.count(normPath)) return;

    visitedIncludes.insert(normPath);



    std::wstring text = ReadStockFileToWString(filePath, (depth == 0) ? &outInfo.originalEncoding : nullptr);

    if (text.empty()) return;



    std::wstring currentFileDir = filePath;

    size_t lastSlash = currentFileDir.find_last_of(L"\\/");

    if (lastSlash != std::wstring::npos) currentFileDir = currentFileDir.substr(0, lastSlash);



    size_t i = 0;

    size_t len = text.length();

    std::vector<std::wstring> blockStack;



    auto skipWhitespaceAndComments = [&]() {

        while (i < len)

        {

            wchar_t c = text[i];

            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')

            {

                i++;

                continue;

            }

            if (c == L'#' || (c == L'/' && i + 1 < len && text[i + 1] == L'/'))

            {

                while (i < len && text[i] != L'\n') i++;

                continue;

            }

            break;

        }

    };



    while (i < len)

    {

        skipWhitespaceAndComments();

        if (i >= len) break;



        wchar_t c = text[i];

        if (c == L'(')

        {

            blockStack.push_back(L"");

            i++;

            continue;

        }

        else if (c == L')')

        {

            if (!blockStack.empty()) blockStack.pop_back();

            i++;

            continue;

        }



        if (c == L'"')

        {

            i++;

            while (i < len && text[i] != L'"') i++;

            if (i < len) i++;

            continue;

        }



        size_t tokenStart = i;

        while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L'(' && text[i] != L')' && text[i] != L'#' && text[i] != L'"')

        {

            i++;

        }

        std::wstring token = text.substr(tokenStart, i - tokenStart);

        if (token.empty()) continue;



        std::wstring tokenLower = token;

        std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), ::towlower);



        skipWhitespaceAndComments();



        if (i < len && text[i] == L'(')

        {

            i++; // skip '('



            if (tokenLower == L"comment" || tokenLower == L"skip")

            {

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    i++;

                }

                continue;

            }



            if (tokenLower == L"include")

            {

                skipWhitespaceAndComments();

                std::wstring incTarget;

                if (i < len && text[i] == L'"')

                {

                    i++;

                    size_t s = i;

                    while (i < len && text[i] != L'"') i++;

                    incTarget = text.substr(s, i - s);

                    if (i < len) i++;

                }

                else

                {

                    size_t s = i;

                    while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L')') i++;

                    incTarget = text.substr(s, i - s);

                }

                while (i < len && text[i] != L')') i++;

                if (i < len) i++;



                if (!incTarget.empty())

                {

                    std::wstring fullIncPath = ResolveRelativePath(currentFileDir, incTarget);

                    if (GetFileAttributesW(fullIncPath.c_str()) == INVALID_FILE_ATTRIBUTES)

                    {

                        std::wstring alt1 = ResolveRelativePath(stockFolder, incTarget);

                        if (GetFileAttributesW(alt1.c_str()) != INVALID_FILE_ATTRIBUTES) fullIncPath = alt1;

                        else {

                            std::wstring alt2 = ResolveRelativePath(basePath + L"TRAINS\\TRAINSET", incTarget);

                            if (GetFileAttributesW(alt2.c_str()) != INVALID_FILE_ATTRIBUTES) fullIncPath = alt2;

                        }

                    }

                    bool isEnclosed = false;

                    for (const auto& blk : blockStack)

                    {

                        if (blk == L"orts3dcab" || blk == L"orts3dcabfile" || blk == L"inside" || blk == L"engine" || blk == L"wagon")

                        {

                            isEnclosed = true;

                            break;

                        }

                    }

                    std::wstring passDirective = (depth == 0) ? incTarget : currentIncDirective;

                    ParseStockSoundBlocksAndIncludes(fullIncPath, stockFolder, basePath, outInfo, visitedIncludes, depth + 1, passDirective, isEnclosed);

                }

                continue;

            }



            if (tokenLower == L"sound")

            {

                size_t soundBlockStart = i;

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    if (pDepth > 0) i++;

                }

                size_t soundBlockEnd = i;

                if (i < len && text[i] == L')') i++;



                std::wstring soundContent = text.substr(soundBlockStart, soundBlockEnd - soundBlockStart);



                // Determine exact block category

                bool isORTS3D = false;

                bool isInside = false;

                bool isEngine = false;

                bool isWagon = false;



                for (const auto& blk : blockStack)

                {

                    if (blk == L"orts3dcab") isORTS3D = true;

                    else if (blk == L"inside") isInside = true;

                    else if (blk == L"engine") isEngine = true;

                    else if (blk == L"wagon") isWagon = true;

                }



                std::vector<std::wstring> smsTokens;

                size_t sIdx = 0;

                size_t sLen = soundContent.length();

                while (sIdx < sLen)

                {

                    while (sIdx < sLen && (soundContent[sIdx] == L' ' || soundContent[sIdx] == L'\t' || soundContent[sIdx] == L'\r' || soundContent[sIdx] == L'\n')) sIdx++;

                    if (sIdx >= sLen) break;



                    if (soundContent[sIdx] == L'"')

                    {

                        sIdx++;

                        size_t tokStart = sIdx;

                        while (sIdx < sLen && soundContent[sIdx] != L'"') sIdx++;

                        std::wstring tok = soundContent.substr(tokStart, sIdx - tokStart);

                        if (sIdx < sLen && soundContent[sIdx] == L'"') sIdx++;

                        if (!tok.empty()) smsTokens.push_back(tok);

                    }

                    else

                    {

                        size_t tokStart = sIdx;

                        while (sIdx < sLen && soundContent[sIdx] != L' ' && soundContent[sIdx] != L'\t' && soundContent[sIdx] != L'\r' && soundContent[sIdx] != L'\n') sIdx++;

                        std::wstring tok = soundContent.substr(tokStart, sIdx - tokStart);

                        if (!tok.empty()) smsTokens.push_back(tok);

                    }

                }



                for (size_t s = 0; s < smsTokens.size(); ++s)

                {

                    RSSSoundEntry entry;

                    entry.rawToken = smsTokens[s];

                    entry.sourceFilePath = filePath;

                    entry.isFromInclude = (depth > 0);

                    entry.incDirectiveString = currentIncDirective;

                    entry.isBlockEnclosedInParent = currentEnclosedInParent;



                    if (isORTS3D)

                    {

                        entry.soundType = L"ORTS3DCab";

                        ResolveSoundFile(entry.rawToken, stockFolder, basePath, entry);

                        outInfo.orts3dCabSounds.push_back(entry);

                        if (depth > 0)

                        {

                            outInfo.orts3dIncInfo.hasIncludeSound = true;

                            outInfo.orts3dIncInfo.sourceFilePath = filePath;

                            outInfo.orts3dIncInfo.incDirectiveString = currentIncDirective;

                            outInfo.orts3dIncInfo.isBlockEnclosedInParent = currentEnclosedInParent;

                        }

                    }

                    else if (isInside)

                    {

                        entry.soundType = L"Inside";

                        ResolveSoundFile(entry.rawToken, stockFolder, basePath, entry);

                        outInfo.insideSounds.push_back(entry);

                        if (depth > 0)

                        {

                            outInfo.insideIncInfo.hasIncludeSound = true;

                            outInfo.insideIncInfo.sourceFilePath = filePath;

                            outInfo.insideIncInfo.incDirectiveString = currentIncDirective;

                            outInfo.insideIncInfo.isBlockEnclosedInParent = currentEnclosedInParent;

                        }

                    }

                    else if (isEngine)

                    {

                        entry.soundType = L"Engine";

                        ResolveSoundFile(entry.rawToken, stockFolder, basePath, entry);

                        outInfo.engineSounds.push_back(entry);

                        if (depth > 0)

                        {

                            outInfo.engineIncInfo.hasIncludeSound = true;

                            outInfo.engineIncInfo.sourceFilePath = filePath;

                            outInfo.engineIncInfo.incDirectiveString = currentIncDirective;

                            outInfo.engineIncInfo.isBlockEnclosedInParent = currentEnclosedInParent;

                        }

                    }

                    else // Wagon or default

                    {

                        entry.soundType = L"Wagon";

                        ResolveSoundFile(entry.rawToken, stockFolder, basePath, entry);

                        outInfo.wagonSounds.push_back(entry);

                        if (depth > 0)

                        {

                            outInfo.wagonIncInfo.hasIncludeSound = true;

                            outInfo.wagonIncInfo.sourceFilePath = filePath;

                            outInfo.wagonIncInfo.incDirectiveString = currentIncDirective;

                            outInfo.wagonIncInfo.isBlockEnclosedInParent = currentEnclosedInParent;

                        }

                    }

                }

                continue;

            }



            if (tokenLower == L"orts3dcab" || tokenLower == L"orts3dcabfile")

            {

                outInfo.hasORTS3DCabBlock = true;

            }

            else if (tokenLower == L"inside")

            {

                outInfo.hasInsideBlock = true;

            }



            blockStack.push_back(tokenLower);

        }

    }



    if (!outInfo.orts3dCabSounds.empty()) outInfo.hasORTS3DCabBlock = true;

    if (!outInfo.insideSounds.empty()) outInfo.hasInsideBlock = true;

}



static void BuildRowSoundCells(RSSUnitSoundInfo& info)

{

    info.missingCount = 0;

    info.soundCount = 0;



    auto countMissing = [&](const std::vector<RSSSoundEntry>& list) {

        info.soundCount += (int)list.size();

        for (const auto& e : list) {

            if (!e.existsOnDisk) info.missingCount++;

        }

    };

    countMissing(info.wagonSounds);

    countMissing(info.engineSounds);

    countMissing(info.orts3dCabSounds);

    countMissing(info.insideSounds);



    auto formatSingle = [](const std::vector<RSSSoundEntry>& list) -> std::wstring {

        if (list.empty()) return L"-";

        if (list.size() == 1) return list[0].smsFileName;

        return list[0].smsFileName + L" (+" + std::to_wstring(list.size() - 1) + L")";

    };



    info.wagonCell  = formatSingle(info.wagonSounds);

    info.engineCell = formatSingle(info.engineSounds);

    info.orts3dCell = formatSingle(info.orts3dCabSounds);

    info.insideCell = formatSingle(info.insideSounds);



    if (!info.fileFound)

    {

        info.statusCell = L"Stock File Missing";

    }

    else if (info.missingCount > 0)

    {

        info.statusCell = std::to_wstring(info.missingCount) + L" Missing";

    }

    else if (info.soundCount == 0)

    {

        info.statusCell = L"No Sound";

    }

    else

    {

        info.statusCell = L"Ready";

    }



    if (info.isModified)

    {

        info.statusCell += L" ●";

    }

}



static void CopyStringToClipboard(HWND hWnd, const std::wstring& str)

{

    if (str.empty()) return;

    if (OpenClipboard(hWnd))

    {

        EmptyClipboard();

        size_t bytes = (str.length() + 1) * sizeof(wchar_t);

        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);

        if (hGlob)

        {

            memcpy(GlobalLock(hGlob), str.c_str(), bytes);

            GlobalUnlock(hGlob);

            SetClipboardData(CF_UNICODETEXT, hGlob);

        }

        CloseClipboard();

    }

}



static std::wstring GetClipboardText(HWND hWnd)

{

    std::wstring text = L"";

    if (OpenClipboard(hWnd))

    {

        HANDLE hData = GetClipboardData(CF_UNICODETEXT);

        if (hData)

        {

            wchar_t* pText = (wchar_t*)GlobalLock(hData);

            if (pText)

            {

                text = pText;

                GlobalUnlock(hData);

            }

        }

        CloseClipboard();

    }

    return text;

}



static void OpenFileInDefaultEditor(HWND hWnd, const std::wstring& filePath)

{

    if (filePath.empty()) return;

    if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES)

    {

        ShowModernMessageBox(hWnd, (L"File not found on disk:\r\n" + filePath).c_str(), L"Open File", MB_OK | MB_ICONWARNING);

        return;

    }



    HINSTANCE hInst = ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

    if ((INT_PTR)hInst <= 32)

    {

        // Fallback to notepad if no associated editor

        ShellExecuteW(NULL, L"open", L"notepad.exe", (L"\"" + filePath + L"\"").c_str(), NULL, SW_SHOWNORMAL);

    }

}



static bool s_rssPlayingAudio = false;

static std::wstring s_rssPlayingPath = L"";



static void RSS_StopAudio()

{

    if (s_rssPlayingAudio)

    {

        mciSendStringW(L"stop SoundStudioAudioAlias", NULL, 0, NULL);

        mciSendStringW(L"close SoundStudioAudioAlias", NULL, 0, NULL);

        PlaySoundW(NULL, NULL, 0);

        s_rssPlayingAudio = false;

        s_rssPlayingPath = L"";

    }

}



static bool RSS_PlayAudio(const std::wstring& path, bool repeat = false)

{

    RSS_StopAudio();

    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;



    wchar_t shortBuf[MAX_PATH] = { 0 };

    std::wstring targetPath = path;

    if (GetShortPathNameW(path.c_str(), shortBuf, MAX_PATH) > 0)

    {

        targetPath = shortBuf;

    }



    std::wstring openCmd = L"open \"" + targetPath + L"\" type waveaudio alias SoundStudioAudioAlias";

    if (mciSendStringW(openCmd.c_str(), NULL, 0, NULL) == 0)

    {

        std::wstring playCmd = repeat ? L"play SoundStudioAudioAlias repeat" : L"play SoundStudioAudioAlias";

        if (mciSendStringW(playCmd.c_str(), NULL, 0, NULL) == 0)

        {

            s_rssPlayingAudio = true;

            s_rssPlayingPath = path;

            return true;

        }

        mciSendStringW(L"close SoundStudioAudioAlias", NULL, 0, NULL);

    }



    DWORD flags = SND_FILENAME | SND_ASYNC;

    if (repeat) flags |= SND_LOOP;

    if (PlaySoundW(path.c_str(), NULL, flags))

    {

        s_rssPlayingAudio = true;

        s_rssPlayingPath = path;

        return true;

    }



    return false;

}



static void UpdateInspectorSelection(RollingStockStudioState* pState, int unitIndex)

{

    if (!pState) return;

    if (unitIndex < 0 || unitIndex >= (int)pState->unitsSoundData.size()) return;



    const auto& u = pState->unitsSoundData[unitIndex];

    const RSSSoundEntry* pEntry = nullptr;

    std::wstring category = L"Wagon";



    if (!u.wagonSounds.empty()) { pEntry = &u.wagonSounds[0]; category = L"Wagon"; }

    else if (!u.engineSounds.empty()) { pEntry = &u.engineSounds[0]; category = L"Engine"; }

    else if (!u.orts3dCabSounds.empty()) { pEntry = &u.orts3dCabSounds[0]; category = L"ORTS3DCab"; }

    else if (!u.insideSounds.empty()) { pEntry = &u.insideSounds[0]; category = L"Inside"; }



    pState->selectedInspectorEntry = pEntry;

    pState->selectedInspectorSlotCategory = category;

    pState->selectedInspectorUnitName = u.name;

    pState->selectedInspectorUnitFolder = u.folder;

    pState->selectedInspectorUnitType = u.type;



    std::wstring smsPath = pEntry ? pEntry->fullDiskPath : L"";

    if (smsPath != pState->cachedSmsPath)

    {

        if (s_rssPlayingAudio)

        {

            RSS_StopAudio();

            pState->isAudioPlaying = false;

        }



        pState->cachedSmsPath = smsPath;

        pState->cachedPrimaryWav = L"";

        pState->cachedWavExists = false;

        pState->cachedSmsLoaded = false;

        pState->cachedSmsDoc = SmsDocument();



        if (!smsPath.empty() && pEntry && pEntry->existsOnDisk)

        {

            pState->cachedSmsLoaded = SmsParser_LoadFromFile(smsPath, pState->basePath, pState->cachedSmsDoc);

            if (pState->cachedSmsLoaded)

            {

                for (const auto& grp : pState->cachedSmsDoc.groups)

                {

                    for (const auto& st : grp.streams)

                    {

                        for (const auto& tr : st.triggers)

                        {

                            for (const auto& f : tr.files)

                            {

                                if (!f.resolvedDiskPath.empty() && f.existsOnDisk)

                                {

                                    pState->cachedPrimaryWav = f.resolvedDiskPath;

                                    pState->cachedWavExists = true;

                                    break;

                                }

                                else if (pState->cachedPrimaryWav.empty() && !f.fileName.empty())

                                {

                                    pState->cachedPrimaryWav = f.fileName;

                                    pState->cachedWavExists = false;

                                }

                            }

                            if (pState->cachedWavExists) break;

                        }

                        if (pState->cachedWavExists) break;

                    }

                    if (pState->cachedWavExists) break;

                }

            }

        }

    }

}



static void UpdateInspectorStreamSelection(RollingStockStudioState* pState, int unitIndex, const std::wstring& category, int streamIndex)
{
    if (!pState || unitIndex < 0 || unitIndex >= (int)pState->unitsSoundData.size()) return;
    UpdateInspectorSelection(pState, unitIndex);

    const auto& u = pState->unitsSoundData[unitIndex];
    const RSSSoundEntry* pEntry = nullptr;

    if (category == L"Wagon" && streamIndex >= 0 && streamIndex < (int)u.wagonSounds.size()) pEntry = &u.wagonSounds[streamIndex];
    else if (category == L"Engine" && streamIndex >= 0 && streamIndex < (int)u.engineSounds.size()) pEntry = &u.engineSounds[streamIndex];
    else if (category == L"ORTS3DCab" && streamIndex >= 0 && streamIndex < (int)u.orts3dCabSounds.size()) pEntry = &u.orts3dCabSounds[streamIndex];
    else if (category == L"Inside" && streamIndex >= 0 && streamIndex < (int)u.insideSounds.size()) pEntry = &u.insideSounds[streamIndex];

    if (!pEntry) return;

    pState->selectedInspectorEntry = pEntry;
    pState->selectedInspectorSlotCategory = category;

    std::wstring smsPath = pEntry->fullDiskPath;
    if (smsPath != pState->cachedSmsPath)
    {
        if (s_rssPlayingAudio)
        {
            RSS_StopAudio();
            pState->isAudioPlaying = false;
        }

        pState->cachedSmsPath = smsPath;
        pState->cachedPrimaryWav = L"";
        pState->cachedWavExists = false;
        pState->cachedSmsLoaded = false;
        pState->cachedSmsDoc = SmsDocument();

        if (!smsPath.empty() && pEntry->existsOnDisk)
        {
            pState->cachedSmsLoaded = SmsParser_LoadFromFile(smsPath, pState->basePath, pState->cachedSmsDoc);
            if (pState->cachedSmsLoaded)
            {
                for (const auto& grp : pState->cachedSmsDoc.groups)
                {
                    for (const auto& st : grp.streams)
                    {
                        for (const auto& tr : st.triggers)
                        {
                            for (const auto& f : tr.files)
                            {
                                if (!f.fileName.empty())
                                {
                                    std::wstring wavPath = SmsParser_ResolveWavPath(f.fileName, smsPath, pState->basePath);
                                    if (GetFileAttributesW(wavPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                                    {
                                        pState->cachedPrimaryWav = wavPath;
                                        pState->cachedWavExists = true;
                                        break;
                                    }
                                }
                            }
                            if (pState->cachedWavExists) break;
                        }
                        if (pState->cachedWavExists) break;
                    }
                    if (pState->cachedWavExists) break;
                }
            }
        }
    }
}

static void HandleAddButtonClick(RollingStockStudioState* pState, int clickedRow, int clickedCol)
{
    if (!pState || clickedRow < 0 || clickedRow >= (int)pState->displayRows.size()) return;
    const auto& dRow = pState->displayRows[clickedRow];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->unitsSoundData.size()) return;
    auto& u = pState->unitsSoundData[dRow.unitIndex];

    std::wstring category = L"Wagon";
    if (clickedCol == 5) category = L"Engine";
    else if (clickedCol == 6) category = L"ORTS3DCab";
    else if (clickedCol == 7) category = L"Inside";

    if ((category == L"Engine" || category == L"ORTS3DCab") && u.type != L"Engine") return;

    size_t distinctIdx = 0;
    std::wstring key = u.folder + L"\\" + u.name + u.ext;
    for (size_t d = 0; d < pState->distinctVehicles.size(); ++d)
    {
        if (_wcsicmp(pState->distinctVehicles[d].key.c_str(), key.c_str()) == 0)
        {
            distinctIdx = d;
            break;
        }
    }
    HandleAddDistinctButtonClick(pState, distinctIdx, category, -1);
}
#if 0
        return;

    std::vector<RSSSoundEntry>* pTargetList = nullptr;
    if (category == L"Engine") pTargetList = &u.engineSounds;
    else if (category == L"ORTS3DCab") pTargetList = &u.orts3dCabSounds;
    else if (category == L"Inside") pTargetList = &u.insideSounds;
    else pTargetList = &u.wagonSounds;

    std::wstring clipText = GetClipboardText(pState->hWnd);
    bool hasClipSms = (!clipText.empty() && clipText.length() >= 4 && _wcsicmp(clipText.c_str() + clipText.length() - 4, L".sms") == 0);

    POINT ptCursor;
    GetCursorPos(&ptCursor);

    std::vector<ContextMenuItem> items;
    if (hasClipSms)
    {
        items.push_back(ContextMenuItem::Action(1, L"\xE77F", L"Paste SMS from Clipboard (" + clipText + L")", L"Ctrl+V", true));
    }
    items.push_back(ContextMenuItem::Action(2, L"\xE8DA", L"Browse for .sms file...", L"", true));

    int cmd = ModernContextMenu::Show(pState->hWnd, ptCursor.x, ptCursor.y, items, TRUE);
    if (cmd == 1 && hasClipSms)
    {
        if (pTargetList && !pTargetList->empty() && !s_bSuppressMultiSmsWarning)
        {
            bool bDontShowAgain = false;
            ShowModernMessageBoxWithCheckbox(
                pState->hWnd,
                L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",
                L"Open Rails Compatibility Notice",
                L"Do not show this warning again",
                &bDontShowAgain,
                MB_OK | MB_ICONINFORMATION
            );
            if (bDontShowAgain) s_bSuppressMultiSmsWarning = true;
        }

        RSSSoundEntry newEntry = CreateSoundEntryFromText(clipText, category, pState->basePath);
        if (pTargetList) pTargetList->push_back(newEntry);
        u.isExpanded = true;
        u.isModified = true;
        pState->isModified = true;
        PopulateSoundMatrix(pState, dRow.unitIndex);
    }
    else if (cmd == 2)
    {
        wchar_t szFile[MAX_PATH] = { 0 };
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = pState->hWnd;
        ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        std::wstring bp = pState->basePath;
        if (!bp.empty() && bp.back() != L'\\') bp += L'\\';
        std::wstring initDir = bp + L"TRAINS\\TRAINSET\\";
        ofn.lpstrInitialDir = initDir.c_str();
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn))
        {
            std::wstring selectedPath = szFile;
            if (_wcsnicmp(selectedPath.c_str(), initDir.c_str(), initDir.length()) != 0)
            {
                ShowModernMessageBox(
                    pState->hWnd,
                    L"Invalid Sound File Location:\r\n\r\nIn Open Rails and MSTS, all sound (.sms) files must reside inside the TRAINS\\TRAINSET directory.\r\n\r\nSound files outside TRAINS\\TRAINSET cannot be referenced or loaded by the simulator. Please copy or move this soundset into your TRAINS\\TRAINSET folder before adding it.",
                    L"Invalid Sound Location",
                    MB_OK | MB_ICONERROR
                );
                return;
            }

            if (pTargetList && !pTargetList->empty() && !s_bSuppressMultiSmsWarning)
            {
                bool bDontShowAgain = false;
                ShowModernMessageBoxWithCheckbox(
                    pState->hWnd,
                    L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",
                    L"Open Rails Compatibility Notice",
                    L"Do not show this warning again",
                    &bDontShowAgain,
                    MB_OK | MB_ICONINFORMATION
                );
                if (bDontShowAgain) s_bSuppressMultiSmsWarning = true;
            }

            RSSSoundEntry newEntry = CreateSoundEntryFromFullPath(szFile, category, pState->basePath);
            if (pTargetList) pTargetList->push_back(newEntry);
            u.isExpanded = true;
            u.isModified = true;
            pState->isModified = true;
            PopulateSoundMatrix(pState, dRow.unitIndex);
        }
    }
}

#endif

static void ShowSoundMatrixContextMenu(RollingStockStudioState* pState, int clickedRow, int clickedCol, int screenX, int screenY)
{
    if (!pState || clickedRow < 0 || clickedRow >= (int)pState->displayRows.size()) return;
    const auto& dRow = pState->displayRows[clickedRow];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->unitsSoundData.size()) return;
    auto& u = pState->unitsSoundData[dRow.unitIndex];

    std::wstring clipText = GetClipboardText(pState->hWnd);
    bool hasClipSms = (!clipText.empty() && clipText.length() >= 4 && _wcsicmp(clipText.c_str() + clipText.length() - 4, L".sms") == 0);

    if (dRow.rowType == RSS_ROW_STREAM)
    {
        RSSSoundEntry* pEntry = nullptr;
        if (dRow.category == L"Wagon" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.wagonSounds.size())
            pEntry = &u.wagonSounds[dRow.streamIndex];
        else if (dRow.category == L"Engine" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.engineSounds.size())
            pEntry = &u.engineSounds[dRow.streamIndex];
        else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.orts3dCabSounds.size())
            pEntry = &u.orts3dCabSounds[dRow.streamIndex];
        else if (dRow.category == L"Inside" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.insideSounds.size())
            pEntry = &u.insideSounds[dRow.streamIndex];

        if (!pEntry) return;

        std::vector<ContextMenuItem> items;
        if (hasClipSms)
        {
            items.push_back(ContextMenuItem::Action(1, L"\xE77F", L"Replace with Clipboard: " + clipText, L"Ctrl+V", true));
        }
        items.push_back(ContextMenuItem::Action(2, L"\xE8DA", L"Browse to Replace SMS...", L"", true));
        items.push_back(ContextMenuItem::Action(8, L"\xE710", L"Add Additional SMS (Open Rails Multi-SMS)...", L"", true));
        items.push_back(ContextMenuItem::Separator());
        items.push_back(ContextMenuItem::Action(3, L"\xE8C8", L"Copy SMS File Name (" + pEntry->smsFileName + L")", L"Ctrl+C", true));
        items.push_back(ContextMenuItem::Action(4, L"\xE8C8", L"Copy Relative Path", L"Ctrl+Shift+C", true));
        items.push_back(ContextMenuItem::Action(5, L"\xE8DA", L"Open Containing Folder in Explorer", L"", pEntry->existsOnDisk));
        items.push_back(ContextMenuItem::Separator());
        items.push_back(ContextMenuItem::Action(6, L"\xE74D", L"Delete / Remove this Sound Stream", L"Del", true));

        int cmd = ModernContextMenu::Show(pState->hWnd, screenX, screenY, items, TRUE);
        if (cmd == 1 && hasClipSms)
        {
            *pEntry = CreateSoundEntryFromText(clipText, dRow.category, pState->basePath);
            u.isModified = true;
            pState->isModified = true;
            PopulateSoundMatrix(pState, dRow.unitIndex);
        }
        else if (cmd == 2)
        {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner = pState->hWnd;
            ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            std::wstring bp = pState->basePath;
            if (!bp.empty() && bp.back() != L'\\') bp += L'\\';
            std::wstring initDir = bp + L"TRAINS\\TRAINSET\\";
            ofn.lpstrInitialDir = initDir.c_str();
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn))
            {
                *pEntry = CreateSoundEntryFromFullPath(szFile, dRow.category, pState->basePath);
                u.isModified = true;
                pState->isModified = true;
                PopulateSoundMatrix(pState, dRow.unitIndex);
            }
        }
        else if (cmd == 3)
        {
            CopyStringToClipboard(pState->hWnd, pEntry->smsFileName);
        }
        else if (cmd == 4)
        {
            std::wstring rel = pEntry->parentFolder.empty() ? pEntry->smsFileName : (pEntry->parentFolder + L"\\" + pEntry->smsFileName);
            CopyStringToClipboard(pState->hWnd, rel);
        }
        else if (cmd == 5)
        {
            if (pEntry->existsOnDisk)
            {
                std::wstring exploreCmd = L"/select,\"" + pEntry->fullDiskPath + L"\"";
                ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
            }
        }
        else if (cmd == 6)
        {
            RSS_PushUndo(pState, L"Remove Sound Stream");
            if (dRow.category == L"Wagon") u.wagonSounds.erase(u.wagonSounds.begin() + dRow.streamIndex);
            else if (dRow.category == L"Engine") u.engineSounds.erase(u.engineSounds.begin() + dRow.streamIndex);
            else if (dRow.category == L"ORTS3DCab") u.orts3dCabSounds.erase(u.orts3dCabSounds.begin() + dRow.streamIndex);
            else if (dRow.category == L"Inside") u.insideSounds.erase(u.insideSounds.begin() + dRow.streamIndex);
            u.isModified = true;
            pState->isModified = true;
            PopulateSoundMatrix(pState, dRow.unitIndex);
        }
    }
    else
    {
        if (dRow.rowType == RSS_ROW_UNIT && clickedCol >= 4 && clickedCol <= 7)
        {
            std::wstring slotCat = L"Wagon";
            std::wstring slotLabel = L"Wagon Exterior Sound";
            std::vector<RSSSoundEntry>* pTargetList = &u.wagonSounds;

            if (clickedCol == 5)
            {
                slotCat = L"Engine";
                slotLabel = L"Engine Cab Sound";
                pTargetList = (u.type == L"Engine") ? &u.engineSounds : nullptr;
            }
            else if (clickedCol == 6)
            {
                slotCat = L"ORTS3DCab";
                slotLabel = L"ORTS 3D Cab Sound";
                pTargetList = (u.type == L"Engine") ? &u.orts3dCabSounds : nullptr;
            }
            else if (clickedCol == 7)
            {
                slotCat = L"Inside";
                slotLabel = L"Inside Sound";
                pTargetList = &u.insideSounds;
            }

            if (pTargetList)
            {
                std::vector<ContextMenuItem> items;
                bool hasSounds = !pTargetList->empty();

                if (hasSounds)
                {
                    const auto& firstEntry = (*pTargetList)[0];
                    if (firstEntry.existsOnDisk)
                    {
                        items.push_back(ContextMenuItem::Action(7, L"\xE70F", L"Open in SMS Editor", L"DblClick", true));
                        items.push_back(ContextMenuItem::Separator());
                    }
                    if (hasClipSms)
                    {
                        items.push_back(ContextMenuItem::Action(1, L"\xE77F", L"Replace with Clipboard: " + clipText, L"Ctrl+V", true));
                    }
                    items.push_back(ContextMenuItem::Action(2, L"\xE8DA", L"Browse to Replace SMS...", L"", true));
                    items.push_back(ContextMenuItem::Action(8, L"\xE710", L"Add Additional SMS (Open Rails Multi-SMS)...", L"", true));
                    items.push_back(ContextMenuItem::Separator());
                    items.push_back(ContextMenuItem::Action(6, L"\xE74D", L"Remove / Clear " + slotLabel, L"", true));
                    items.push_back(ContextMenuItem::Separator());
                    items.push_back(ContextMenuItem::Action(3, L"\xE8C8", L"Copy SMS File Name (" + firstEntry.smsFileName + L")", L"Ctrl+C", true));
                    items.push_back(ContextMenuItem::Action(4, L"\xE8C8", L"Copy Relative Path", L"Ctrl+Shift+C", true));
                    items.push_back(ContextMenuItem::Action(5, L"\xE8DA", L"Open Containing Folder in Explorer", L"", firstEntry.existsOnDisk));
                }
                else
                {
                    if (hasClipSms)
                    {
                        items.push_back(ContextMenuItem::Action(1, L"\xE77F", L"Assign from Clipboard: " + clipText, L"Ctrl+V", true));
                    }
                    items.push_back(ContextMenuItem::Action(2, L"\xE8DA", L"Browse to Assign SMS...", L"", true));
                }
                items.push_back(ContextMenuItem::Separator());
                items.push_back(ContextMenuItem::Action(10, L"\xE8DA", L"Reveal Stock File in Explorer", L"", u.fileFound));
                items.push_back(ContextMenuItem::Action(11, L"\xE70F", L"Open Stock File in Default Editor", L"", u.fileFound));

                int cmd = ModernContextMenu::Show(pState->hWnd, screenX, screenY, items, TRUE);
                if (cmd == 7 && hasSounds && (*pTargetList)[0].existsOnDisk)
                {
                    SoundStudio_OpenSmsInEditor(pState->hWnd, (*pTargetList)[0].fullDiskPath);
                }
                else if (cmd == 1 && hasClipSms)
                {
                    RSS_PushUndo(pState, L"Replace SMS from Clipboard");
                    RSSSoundEntry newEntry = CreateSoundEntryFromText(clipText, slotCat, pState->basePath);
                    pTargetList->clear();
                    pTargetList->push_back(newEntry);
                    u.isModified = true;
                    pState->isModified = true;
                    PopulateSoundMatrix(pState, dRow.unitIndex);
                }
                else if (cmd == 2)
                {
                    wchar_t szFile[MAX_PATH] = { 0 };
                    OPENFILENAMEW ofn = { sizeof(ofn) };
                    ofn.hwndOwner = pState->hWnd;
                    ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    std::wstring bp = pState->basePath;
                    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';
                    std::wstring initDir = bp + L"TRAINS\\TRAINSET\\";
                    ofn.lpstrInitialDir = initDir.c_str();
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                    if (GetOpenFileNameW(&ofn))
                    {
                        RSS_PushUndo(pState, L"Assign SMS File");
                        RSSSoundEntry newEntry = CreateSoundEntryFromFullPath(szFile, slotCat, pState->basePath);
                        pTargetList->clear();
                        pTargetList->push_back(newEntry);
                        u.isModified = true;
                        pState->isModified = true;
                        PopulateSoundMatrix(pState, dRow.unitIndex);
                    }
                }
                else if (cmd == 8)
                {
                    if (!s_bSuppressMultiSmsWarning)
                    {
                        bool bDontShowAgain = false;
                        ShowModernMessageBoxWithCheckbox(
                            pState->hWnd,
                            L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",
                            L"Open Rails Compatibility Notice",
                            L"Do not show this warning again",
                            &bDontShowAgain,
                            MB_OK | MB_ICONINFORMATION
                        );
                        if (bDontShowAgain) s_bSuppressMultiSmsWarning = true;
                    }
                    wchar_t szFile[MAX_PATH] = { 0 };
                    OPENFILENAMEW ofn = { sizeof(ofn) };
                    ofn.hwndOwner = pState->hWnd;
                    ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    std::wstring bp = pState->basePath;
                    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';
                    std::wstring initDir = bp + L"TRAINS\\TRAINSET\\";
                    ofn.lpstrInitialDir = initDir.c_str();
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                    if (GetOpenFileNameW(&ofn))
                    {
                        RSS_PushUndo(pState, L"Assign SMS File");
                        RSSSoundEntry newEntry = CreateSoundEntryFromFullPath(szFile, slotCat, pState->basePath);
                        pTargetList->push_back(newEntry);
                        u.isExpanded = true;
                        u.isModified = true;
                        pState->isModified = true;
                        PopulateSoundMatrix(pState, dRow.unitIndex);
                    }
                }
                else if (cmd == 6)
                {
                    RSS_PushUndo(pState, L"Clear " + slotLabel);
                    pTargetList->clear();
                    u.isModified = true;
                    pState->isModified = true;
                    PopulateSoundMatrix(pState, dRow.unitIndex);
                }
                else if (cmd == 3 && hasSounds)
                {
                    CopyStringToClipboard(pState->hWnd, (*pTargetList)[0].smsFileName);
                }
                else if (cmd == 4 && hasSounds)
                {
                    const auto& firstEntry = (*pTargetList)[0];
                    std::wstring rel = firstEntry.parentFolder.empty() ? firstEntry.smsFileName : (firstEntry.parentFolder + L"\\" + firstEntry.smsFileName);
                    CopyStringToClipboard(pState->hWnd, rel);
                }
                else if (cmd == 5 && hasSounds)
                {
                    if ((*pTargetList)[0].existsOnDisk)
                    {
                        std::wstring exploreCmd = L"/select,\"" + (*pTargetList)[0].fullDiskPath + L"\"";
                        ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
                    }
                }
                else if (cmd == 10 && u.fileFound && !u.fullPath.empty())
                {
                    std::wstring exploreCmd = L"/select,\"" + u.fullPath + L"\"";
                    ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
                }
                else if (cmd == 11 && u.fileFound && !u.fullPath.empty())
                {
                    OpenFileInDefaultEditor(pState->hWnd, u.fullPath);
                }
            }
        }
        else
        {
            // General unit row context menu
        std::vector<ContextMenuItem> items;
        items.push_back(ContextMenuItem::Action(10, L"\xE8DA", L"Reveal Stock File in Explorer", L"", u.fileFound));
        items.push_back(ContextMenuItem::Action(11, L"\xE70F", L"Open Stock File in Default Editor", L"", u.fileFound));
        int cmd = ModernContextMenu::Show(pState->hWnd, screenX, screenY, items, TRUE);
        if (cmd == 10 && u.fileFound && !u.fullPath.empty())
        {
            std::wstring exploreCmd = L"/select,\"" + u.fullPath + L"\"";
            ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
        }
        else if (cmd == 11 && u.fileFound && !u.fullPath.empty())
        {
            OpenFileInDefaultEditor(pState->hWnd, u.fullPath);
        }
    }
}

}

static RSSSoundEntry CreateSoundEntryFromScanned(const ScannedSoundItem& item, const std::wstring& soundType, const std::wstring& basePath)

{

    RSSSoundEntry entry;

    entry.soundType = soundType;

    entry.smsFileName = item.szFileName;

    entry.parentFolder = item.szFolder;

    entry.fullDiskPath = item.szFullPath;

    entry.rawToken = item.szFileName;

    entry.existsOnDisk = (GetFileAttributesW(item.szFullPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    return entry;

}



static RSSSoundEntry CreateSoundEntryFromText(const std::wstring& rawText, const std::wstring& category, const std::wstring& basePath)

{

    RSSSoundEntry entry;

    entry.soundType = category;



    std::wstring clean = rawText;

    while (!clean.empty() && (clean.front() == L' ' || clean.front() == L'\t' || clean.front() == L'"' || clean.front() == L'\'')) clean.erase(clean.begin());

    while (!clean.empty() && (clean.back() == L' ' || clean.back() == L'\t' || clean.back() == L'"' || clean.back() == L'\'')) clean.pop_back();



    entry.rawToken = clean;



    size_t lastSlash = clean.find_last_of(L"\\/");

    if (lastSlash != std::wstring::npos)

    {

        entry.smsFileName = clean.substr(lastSlash + 1);

        entry.parentFolder = clean.substr(0, lastSlash);

    }

    else

    {

        entry.smsFileName = clean;

        entry.parentFolder = L"";

    }



    std::wstring bp = basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';



    if (GetFileAttributesW(clean.c_str()) != INVALID_FILE_ATTRIBUTES)

    {

        entry.fullDiskPath = clean;

        entry.existsOnDisk = true;

    }

    else

    {

        std::wstring candidate = bp + L"TRAINS\\TRAINSET\\" + clean;

        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)

        {

            entry.fullDiskPath = candidate;

            entry.existsOnDisk = true;

        }

        else

        {

            candidate = bp + L"TRAINS\\TRAINSET\\common.sound\\" + entry.smsFileName;

            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                entry.fullDiskPath = candidate;

                entry.existsOnDisk = true;

            }

            else

            {

                entry.fullDiskPath = clean;

                entry.existsOnDisk = false;

            }

        }

    }

    return entry;

}



static RSSSoundEntry CreateSoundEntryFromFullPath(const std::wstring& fullPath, const std::wstring& category, const std::wstring& basePath)

{

    RSSSoundEntry entry;

    entry.soundType = category;

    entry.fullDiskPath = fullPath;

    entry.existsOnDisk = (GetFileAttributesW(fullPath.c_str()) != INVALID_FILE_ATTRIBUTES);



    size_t lastSlash = fullPath.find_last_of(L"\\/");

    if (lastSlash != std::wstring::npos)

        entry.smsFileName = fullPath.substr(lastSlash + 1);

    else

        entry.smsFileName = fullPath;

    entry.rawToken = entry.smsFileName;



    std::wstring bp = basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';

    std::wstring trainsetPrefix = bp + L"TRAINS\\TRAINSET\\";



    if (fullPath.length() > trainsetPrefix.length() && _wcsnicmp(fullPath.c_str(), trainsetPrefix.c_str(), trainsetPrefix.length()) == 0)

    {

        std::wstring rel = fullPath.substr(trainsetPrefix.length());

        size_t rSlash = rel.find_last_of(L"\\/");

        if (rSlash != std::wstring::npos)

            entry.parentFolder = rel.substr(0, rSlash);

        else

            entry.parentFolder = L"";

    }

    else

    {

        size_t rSlash = fullPath.find_last_of(L"\\/");

        if (rSlash != std::wstring::npos)

        {

            std::wstring dir = fullPath.substr(0, rSlash);

            size_t pSlash = dir.find_last_of(L"\\/");

            if (pSlash != std::wstring::npos) entry.parentFolder = dir.substr(pSlash + 1);

            else entry.parentFolder = dir;

        }

    }

    return entry;

}



static std::wstring BuildRelativeSoundToken(const RSSSoundEntry& entry, const std::wstring& stockFolder, const std::wstring& basePath)

{

    if (entry.smsFileName.empty()) return L"";



    std::wstring smsName = entry.smsFileName;

    for (wchar_t& c : smsName) if (c == L'\\') c = L'/';

    size_t lastS = smsName.find_last_of(L'/');

    if (lastS != std::wstring::npos) smsName = smsName.substr(lastS + 1);



    std::wstring sFolderNorm = stockFolder;

    for (wchar_t& c : sFolderNorm) if (c == L'\\') c = L'/';

    while (!sFolderNorm.empty() && sFolderNorm.back() == L'/') sFolderNorm.pop_back();



    std::wstring soundDiskPath = entry.fullDiskPath;

    for (wchar_t& c : soundDiskPath) if (c == L'\\') c = L'/';



    // 1. If inside this stock's own folder or its sound/ subfolder:

    if (!sFolderNorm.empty() && !soundDiskPath.empty())

    {

        std::wstring localPrefix = sFolderNorm + L"/";

        if (_wcsnicmp(soundDiskPath.c_str(), localPrefix.c_str(), localPrefix.length()) == 0)

        {

            return smsName;

        }

    }



    // 2. If parentFolder is known and outside:

    if (!entry.parentFolder.empty())

    {

        std::wstring pf = entry.parentFolder;

        for (wchar_t& c : pf) if (c == L'\\') c = L'/';

        while (!pf.empty() && pf.front() == L'/') pf.erase(pf.begin());

        while (!pf.empty() && pf.back() == L'/') pf.pop_back();



        if (pf.rfind(L"..", 0) == 0)

        {

            return pf + L"/" + smsName;

        }

        return L"../../" + pf + L"/" + smsName;

    }



    if (!entry.rawToken.empty())

    {

        std::wstring rt = entry.rawToken;

        for (wchar_t& c : rt) if (c == L'\\') c = L'/';

        return rt;

    }



    return smsName;

}



static std::wstring FormatSoundBlock(const std::vector<RSSSoundEntry>& list, const std::wstring& stockFolder, const std::wstring& basePath)

{

    if (list.empty())

    {

        return L"Sound ( \"\" )";

    }



    std::wstring result = L"Sound ( ";

    for (size_t i = 0; i < list.size(); ++i)

    {

        if (i > 0) result += L" ";

        result += L"\"" + BuildRelativeSoundToken(list[i], stockFolder, basePath) + L"\"";

    }

    result += L" )";

    return result;

}



static bool WriteWStringToStockFile(const std::wstring& filePath, const std::wstring& content, StockFileEncoding encoding)

{

    std::wstring tmpPath = filePath + L".tmp";

    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) return false;



    DWORD bytesWritten = 0;

    bool success = true;



    if (encoding == StockFileEncoding::Utf16LeBom)

    {

        unsigned char bom[2] = { 0xFF, 0xFE };

        WriteFile(hFile, bom, 2, &bytesWritten, NULL);

        DWORD dataBytes = (DWORD)(content.length() * sizeof(wchar_t));

        if (!WriteFile(hFile, content.data(), dataBytes, &bytesWritten, NULL) || bytesWritten != dataBytes)

            success = false;

    }

    else if (encoding == StockFileEncoding::Utf8Bom || encoding == StockFileEncoding::Utf8NoBom)

    {

        if (encoding == StockFileEncoding::Utf8Bom)

        {

            unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };

            WriteFile(hFile, bom, 3, &bytesWritten, NULL);

        }

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.length(), NULL, 0, NULL, NULL);

        if (utf8Len > 0)

        {

            std::vector<char> utf8Buf(utf8Len);

            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.length(), utf8Buf.data(), utf8Len, NULL, NULL);

            if (!WriteFile(hFile, utf8Buf.data(), (DWORD)utf8Len, &bytesWritten, NULL) || (int)bytesWritten != utf8Len)

                success = false;

        }

    }

    else // ANSI

    {

        int ansiLen = WideCharToMultiByte(CP_ACP, 0, content.c_str(), (int)content.length(), NULL, 0, NULL, NULL);

        if (ansiLen > 0)

        {

            std::vector<char> ansiBuf(ansiLen);

            WideCharToMultiByte(CP_ACP, 0, content.c_str(), (int)content.length(), ansiBuf.data(), ansiLen, NULL, NULL);

            if (!WriteFile(hFile, ansiBuf.data(), (DWORD)ansiLen, &bytesWritten, NULL) || (int)bytesWritten != ansiLen)

                success = false;

        }

    }



    CloseHandle(hFile);



    if (!success)

    {

        DeleteFileW(tmpPath.c_str());

        return false;

    }



    std::wstring bakPath = filePath + L".TSCB_BACKUP";

    if (GetFileAttributesW(bakPath.c_str()) == INVALID_FILE_ATTRIBUTES)

    {

        CopyFileW(filePath.c_str(), bakPath.c_str(), TRUE);

    }



    if (!MoveFileExW(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))

    {

        DeleteFileW(tmpPath.c_str());

        return false;

    }



    return true;

}



static IncludeSoundAction PromptIncludeSoundChoice(

    HWND hWndParent,

    const std::wstring& stockName,

    const std::vector<std::wstring>& incFiles,

    bool isPattern2CabOrInside,

    bool* pOutApplyToAll)

{

    if (!hWndParent || !IsWindow(hWndParent))

    {

        return IncludeSoundAction::ModifyInInclude;

    }



    std::wstring incListText;

    for (const auto& f : incFiles)

    {

        size_t ls = f.find_last_of(L"\\/");

        std::wstring fname = (ls != std::wstring::npos) ? f.substr(ls + 1) : f;

        incListText += L"  • " + fname + L"\r\n";

    }



    std::wstring msg = L"Sound definitions for rolling stock unit '" + stockName + L"' originate from external include (.inc) file(s):\r\n\r\n" +

        incListText + L"\r\n";



    if (isPattern2CabOrInside)

    {

        msg += L"Note: 3D Cab / Interior sounds are defined inside the include file.\r\n"

               L"Saving directly into the .inc file preserves 3D cab model binding and head positions.\r\n\r\n";

    }



    msg += L"How would you like to save these sound changes?\r\n\r\n"

           L"• Modify in .inc (Recommended):\r\n"

           L"  Updates the sound definitions inside the .inc file. A backup (.inc.TSCB_BACKUP) will be created.\r\n\r\n"

           L"• Override in .eng / .wag:\r\n"

           L"  Inserts the sound statement above the Include directive in the stock file. Open Rails will parse the override first without touching the .inc file.\r\n";



    std::vector<ModernMsgBoxCustomButton> buttons;

    buttons.push_back({ 101, L"Modify in .inc (Recommended)", true });

    buttons.push_back({ 102, L"Override in .eng / .wag", false });

    buttons.push_back({ IDCANCEL, L"Cancel", false });



    bool bChecked = false;

    int res = ShowModernMessageBoxExWithCheckbox(

        hWndParent,

        msg.c_str(),

        L"Include (.inc) Sound Block Detected",

        buttons,

        L"Remember my choice for all remaining files in this batch",

        &bChecked,

        MB_ICONQUESTION);



    if (pOutApplyToAll)

    {

        *pOutApplyToAll = bChecked;

    }



    if (res == 101) return IncludeSoundAction::ModifyInInclude;

    if (res == 102) return IncludeSoundAction::OverrideInParent;

    return IncludeSoundAction::Cancel;

}



static bool UpdateSoundInIncludeFile(

    const std::wstring& incFilePath,

    const std::vector<RSSSoundEntry>& newSounds,

    const std::wstring& targetSlot,

    const std::wstring& stockFolder,

    const std::wstring& basePath)

{

    if (incFilePath.empty() || GetFileAttributesW(incFilePath.c_str()) == INVALID_FILE_ATTRIBUTES)

    {

        return false;

    }



    StockFileEncoding incEncoding = StockFileEncoding::Utf16LeBom;

    std::wstring text = ReadStockFileToWString(incFilePath, &incEncoding);

    if (text.empty()) return false;



    std::wstring incFolder = incFilePath;

    size_t lastSlash = incFolder.find_last_of(L"\\/");

    if (lastSlash != std::wstring::npos) incFolder = incFolder.substr(0, lastSlash);



    std::wstring newSoundStatement = FormatSoundBlock(newSounds, incFolder, basePath);



    size_t targetSoundStart = (size_t)-1, targetSoundEnd = (size_t)-1;

    size_t targetBlockCloseParen = (size_t)-1;

    size_t anySoundStart = (size_t)-1, anySoundEnd = (size_t)-1;



    std::wstring targetSlotLower = targetSlot;

    std::transform(targetSlotLower.begin(), targetSlotLower.end(), targetSlotLower.begin(), ::towlower);



    size_t i = 0;

    size_t len = text.length();

    std::vector<std::wstring> blockStack;



    auto skipWhitespaceAndComments = [&]() {

        while (i < len)

        {

            wchar_t c = text[i];

            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')

            {

                i++;

                continue;

            }

            if (c == L'#' || (c == L'/' && i + 1 < len && text[i + 1] == L'/'))

            {

                while (i < len && text[i] != L'\n') i++;

                continue;

            }

            break;

        }

    };



    while (i < len)

    {

        skipWhitespaceAndComments();

        if (i >= len) break;



        wchar_t c = text[i];

        if (c == L'(')

        {

            blockStack.push_back(L"");

            i++;

            continue;

        }

        else if (c == L')')

        {

            if (!blockStack.empty())

            {

                const std::wstring& closingBlock = blockStack.back();

                if (closingBlock == targetSlotLower ||

                    (targetSlotLower == L"orts3dcab" && (closingBlock == L"orts3dcab" || closingBlock == L"orts3dcabfile")))

                {

                    targetBlockCloseParen = i;

                }

                blockStack.pop_back();

            }

            i++;

            continue;

        }



        if (c == L'"')

        {

            i++;

            while (i < len && text[i] != L'"') i++;

            if (i < len) i++;

            continue;

        }



        size_t tokenStart = i;

        while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L'(' && text[i] != L')' && text[i] != L'#' && text[i] != L'"')

        {

            i++;

        }

        std::wstring token = text.substr(tokenStart, i - tokenStart);

        if (token.empty()) continue;



        std::wstring tokenLower = token;

        std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), ::towlower);



        skipWhitespaceAndComments();



        if (i < len && text[i] == L'(')

        {

            i++; // skip '('



            if (tokenLower == L"comment" || tokenLower == L"skip")

            {

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    i++;

                }

                continue;

            }



            if (tokenLower == L"sound")

            {

                size_t soundStatementStart = tokenStart;

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    if (pDepth > 0) i++;

                }

                if (i < len && text[i] == L')') i++;

                size_t soundStatementEnd = i;



                anySoundStart = soundStatementStart;

                anySoundEnd = soundStatementEnd;



                bool matchesTarget = false;

                for (const auto& blk : blockStack)

                {

                    if (blk == targetSlotLower ||

                        (targetSlotLower == L"orts3dcab" && (blk == L"orts3dcab" || blk == L"orts3dcabfile")))

                    {

                        matchesTarget = true;

                        break;

                    }

                }

                if (matchesTarget || blockStack.empty())

                {

                    targetSoundStart = soundStatementStart;

                    targetSoundEnd = soundStatementEnd;

                    break;

                }

                continue;

            }



            blockStack.push_back(tokenLower);

        }

    }



    if (targetSoundStart != (size_t)-1)

    {

        text.replace(targetSoundStart, targetSoundEnd - targetSoundStart, newSoundStatement);

    }

    else if (anySoundStart != (size_t)-1)

    {

        text.replace(anySoundStart, anySoundEnd - anySoundStart, newSoundStatement);

    }

    else if (targetBlockCloseParen != (size_t)-1)

    {

        text.insert(targetBlockCloseParen, L"\t" + newSoundStatement + L"\r\n");

    }

    else

    {

        // Append at end

        if (!text.empty() && text.back() != L'\n') text += L"\r\n";

        text += L"\t" + newSoundStatement + L"\r\n";

    }



    return WriteWStringToStockFile(incFilePath, text, incEncoding);

}



static bool InsertSoundAboveIncludeInParent(

    std::wstring& text,

    const std::wstring& incDirective,

    const std::wstring& soundStatement)

{

    if (text.empty() || incDirective.empty() || soundStatement.empty()) return false;



    // Extract bare filename and clean path for matching

    std::wstring targetIncClean = incDirective;

    for (wchar_t& c : targetIncClean) if (c == L'\\') c = L'/';

    while (!targetIncClean.empty() && (targetIncClean.front() == L'"' || targetIncClean.front() == L' ' || targetIncClean.front() == L'\t')) targetIncClean.erase(targetIncClean.begin());

    while (!targetIncClean.empty() && (targetIncClean.back() == L'"' || targetIncClean.back() == L' ' || targetIncClean.back() == L'\t')) targetIncClean.pop_back();



    size_t slashPos = targetIncClean.find_last_of(L'/');

    std::wstring targetIncFilename = (slashPos != std::wstring::npos) ? targetIncClean.substr(slashPos + 1) : targetIncClean;

    std::wstring targetIncFilenameLower = targetIncFilename;

    std::transform(targetIncFilenameLower.begin(), targetIncFilenameLower.end(), targetIncFilenameLower.begin(), ::towlower);



    size_t i = 0;

    size_t len = text.length();



    while (i < len)

    {

        wchar_t c = text[i];

        if (c == L'"')

        {

            i++;

            while (i < len && text[i] != L'"') i++;

            if (i < len) i++;

            continue;

        }

        if (c == L'#' || (c == L'/' && i + 1 < len && text[i + 1] == L'/'))

        {

            while (i < len && text[i] != L'\n') i++;

            continue;

        }



        if (c == L'i' || c == L'I')

        {

            size_t tokenStart = i;

            while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L'(' && text[i] != L'"')

            {

                i++;

            }

            std::wstring tok = text.substr(tokenStart, i - tokenStart);

            std::transform(tok.begin(), tok.end(), tok.begin(), ::towlower);



            if (tok == L"include")

            {

                while (i < len && (text[i] == L' ' || text[i] == L'\t' || text[i] == L'\r' || text[i] == L'\n')) i++;

                if (i < len && text[i] == L'(')

                {

                    i++;

                    while (i < len && (text[i] == L' ' || text[i] == L'\t' || text[i] == L'\r' || text[i] == L'\n')) i++;

                    std::wstring currentIncArg;

                    if (i < len && text[i] == L'"')

                    {

                        i++;

                        size_t s = i;

                        while (i < len && text[i] != L'"') i++;

                        currentIncArg = text.substr(s, i - s);

                        if (i < len) i++;

                    }

                    else

                    {

                        size_t s = i;

                        while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L')') i++;

                        currentIncArg = text.substr(s, i - s);

                    }



                    std::wstring currentIncClean = currentIncArg;

                    for (wchar_t& ch : currentIncClean) if (ch == L'\\') ch = L'/';

                    size_t curSlash = currentIncClean.find_last_of(L'/');

                    std::wstring currentIncFilename = (curSlash != std::wstring::npos) ? currentIncClean.substr(curSlash + 1) : currentIncClean;

                    std::transform(currentIncFilename.begin(), currentIncFilename.end(), currentIncFilename.begin(), ::towlower);



                    if (currentIncFilename == targetIncFilenameLower || currentIncClean == targetIncClean)

                    {

                        // Found matching include statement!

                        // Find start of line

                        size_t lineStart = tokenStart;

                        while (lineStart > 0 && text[lineStart - 1] != L'\n' && text[lineStart - 1] != L'\r')

                        {

                            lineStart--;

                        }



                        // Determine indentation

                        std::wstring indent;

                        for (size_t k = lineStart; k < tokenStart; ++k)

                        {

                            if (text[k] == L' ' || text[k] == L'\t') indent += text[k];

                            else break;

                        }

                        if (indent.empty()) indent = L"\t";



                        std::wstring toInsert = indent + soundStatement + L"\r\n";

                        text.insert(lineStart, toInsert);

                        return true;

                    }

                }

            }

            continue;

        }



        i++;

    }



    return false;

}



static bool UpdateUnitSoundInStockFile(

    RSSUnitSoundInfo& u,

    const std::wstring& basePath,

    HWND hWndParent = NULL,

    IncludeSoundAction defaultIncAction = IncludeSoundAction::PromptUser,

    IncludeSoundAction* pOutChosenAction = nullptr,

    bool* pOutApplyToAll = nullptr)

{

    if (u.fullPath.empty() || GetFileAttributesW(u.fullPath.c_str()) == INVALID_FILE_ATTRIBUTES)

    {

        return false;

    }



    StockFileEncoding encoding = u.originalEncoding;

    std::wstring text = ReadStockFileToWString(u.fullPath, &encoding);

    if (text.empty()) return false;



    std::wstring stockFolder = u.folder;

    size_t lastSlash = u.fullPath.find_last_of(L"\\/");

    if (lastSlash != std::wstring::npos) stockFolder = u.fullPath.substr(0, lastSlash);



    // 1. Detect any sound blocks originating from external .inc files

    std::vector<std::wstring> incFiles;

    bool hasPattern2CabOrInside = false;



    auto checkIncSlot = [&](const RSSSlotIncludeInfo& incInfo, const std::wstring& slotName) {

        if (incInfo.hasIncludeSound && !incInfo.sourceFilePath.empty())

        {

            if (std::find(incFiles.begin(), incFiles.end(), incInfo.sourceFilePath) == incFiles.end())

            {

                incFiles.push_back(incInfo.sourceFilePath);

            }

            if ((slotName == L"ORTS3DCab" || slotName == L"Inside") && !incInfo.isBlockEnclosedInParent)

            {

                hasPattern2CabOrInside = true;

            }

        }

    };



    checkIncSlot(u.wagonIncInfo, L"Wagon");

    if (u.type == L"Engine") checkIncSlot(u.engineIncInfo, L"Engine");

    if (u.hasORTS3DCabBlock) checkIncSlot(u.orts3dIncInfo, L"ORTS3DCab");

    if (u.hasInsideBlock) checkIncSlot(u.insideIncInfo, L"Inside");



    // Also check entry-level isFromInclude

    for (const auto& e : u.wagonSounds) {

        if (e.isFromInclude && !e.sourceFilePath.empty()) {

            if (std::find(incFiles.begin(), incFiles.end(), e.sourceFilePath) == incFiles.end()) incFiles.push_back(e.sourceFilePath);

        }

    }

    for (const auto& e : u.engineSounds) {

        if (e.isFromInclude && !e.sourceFilePath.empty()) {

            if (std::find(incFiles.begin(), incFiles.end(), e.sourceFilePath) == incFiles.end()) incFiles.push_back(e.sourceFilePath);

        }

    }

    for (const auto& e : u.orts3dCabSounds) {

        if (e.isFromInclude && !e.sourceFilePath.empty()) {

            if (std::find(incFiles.begin(), incFiles.end(), e.sourceFilePath) == incFiles.end()) incFiles.push_back(e.sourceFilePath);

            if (!e.isBlockEnclosedInParent) hasPattern2CabOrInside = true;

        }

    }

    for (const auto& e : u.insideSounds) {

        if (e.isFromInclude && !e.sourceFilePath.empty()) {

            if (std::find(incFiles.begin(), incFiles.end(), e.sourceFilePath) == incFiles.end()) incFiles.push_back(e.sourceFilePath);

            if (!e.isBlockEnclosedInParent) hasPattern2CabOrInside = true;

        }

    }



    // 2. Resolve Include Sound Action

    IncludeSoundAction actionToTake = defaultIncAction;

    if (!incFiles.empty())

    {

        if (pOutApplyToAll && *pOutApplyToAll && pOutChosenAction && *pOutChosenAction != IncludeSoundAction::PromptUser)

        {

            actionToTake = *pOutChosenAction;

        }

        else if (actionToTake == IncludeSoundAction::PromptUser)

        {

            if (hWndParent && IsWindow(hWndParent))

            {

                actionToTake = PromptIncludeSoundChoice(hWndParent, u.name, incFiles, hasPattern2CabOrInside, pOutApplyToAll);

            }

            else

            {

                actionToTake = IncludeSoundAction::ModifyInInclude;

            }

        }

    }



    if (pOutChosenAction)

    {

        *pOutChosenAction = actionToTake;

    }



    if (actionToTake == IncludeSoundAction::Cancel)

    {

        return false;

    }



    // 3. Format sound statements

    std::wstring wagonSoundStatement = FormatSoundBlock(u.wagonSounds, stockFolder, basePath);

    std::wstring engineSoundStatement = FormatSoundBlock(u.engineSounds, stockFolder, basePath);

    std::wstring ortsSoundStatement = FormatSoundBlock(u.orts3dCabSounds, stockFolder, basePath);

    std::wstring insideSoundStatement = FormatSoundBlock(u.insideSounds, stockFolder, basePath);



    // Track which slots are handled externally in .inc files

    bool wagonHandledInInc = false;

    bool engineHandledInInc = false;

    bool ortsHandledInInc = false;

    bool insideHandledInInc = false;



    if (actionToTake == IncludeSoundAction::ModifyInInclude)

    {

        if (u.wagonIncInfo.hasIncludeSound && !u.wagonIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.wagonIncInfo.sourceFilePath, u.wagonSounds, L"Wagon", stockFolder, basePath);

            wagonHandledInInc = true;

        }

        if (u.type == L"Engine" && u.engineIncInfo.hasIncludeSound && !u.engineIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.engineIncInfo.sourceFilePath, u.engineSounds, L"Engine", stockFolder, basePath);

            engineHandledInInc = true;

        }

        if (u.hasORTS3DCabBlock && u.orts3dIncInfo.hasIncludeSound && !u.orts3dIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.orts3dIncInfo.sourceFilePath, u.orts3dCabSounds, L"ORTS3DCab", stockFolder, basePath);

            ortsHandledInInc = true;

        }

        if (u.hasInsideBlock && u.insideIncInfo.hasIncludeSound && !u.insideIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.insideIncInfo.sourceFilePath, u.insideSounds, L"Inside", stockFolder, basePath);

            insideHandledInInc = true;

        }

    }

    else if (actionToTake == IncludeSoundAction::OverrideInParent)

    {

        // For Pattern 2 ORTS3DCab and Inside, the container block only exists inside the .inc.

        // Therefore, safely update in the .inc to preserve 3D cab shape binding.

        if (u.hasORTS3DCabBlock && u.orts3dIncInfo.hasIncludeSound && !u.orts3dIncInfo.isBlockEnclosedInParent && !u.orts3dIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.orts3dIncInfo.sourceFilePath, u.orts3dCabSounds, L"ORTS3DCab", stockFolder, basePath);

            ortsHandledInInc = true;

        }

        if (u.hasInsideBlock && u.insideIncInfo.hasIncludeSound && !u.insideIncInfo.isBlockEnclosedInParent && !u.insideIncInfo.sourceFilePath.empty())

        {

            UpdateSoundInIncludeFile(u.insideIncInfo.sourceFilePath, u.insideSounds, L"Inside", stockFolder, basePath);

            insideHandledInInc = true;

        }



        // For Wagon, Engine, and Pattern 1 ORTS3DCab/Inside:

        // Insert Sound statement immediately above the Include statement in parent file!

        if (u.wagonIncInfo.hasIncludeSound && !u.wagonIncInfo.incDirectiveString.empty())

        {

            if (InsertSoundAboveIncludeInParent(text, u.wagonIncInfo.incDirectiveString, wagonSoundStatement))

            {

                wagonHandledInInc = true;

            }

        }

        if (u.type == L"Engine" && u.engineIncInfo.hasIncludeSound && !u.engineIncInfo.incDirectiveString.empty())

        {

            if (InsertSoundAboveIncludeInParent(text, u.engineIncInfo.incDirectiveString, engineSoundStatement))

            {

                engineHandledInInc = true;

            }

        }

        if (u.hasORTS3DCabBlock && !ortsHandledInInc && u.orts3dIncInfo.hasIncludeSound && !u.orts3dIncInfo.incDirectiveString.empty())

        {

            if (InsertSoundAboveIncludeInParent(text, u.orts3dIncInfo.incDirectiveString, ortsSoundStatement))

            {

                ortsHandledInInc = true;

            }

        }

        if (u.hasInsideBlock && !insideHandledInInc && u.insideIncInfo.hasIncludeSound && !u.insideIncInfo.incDirectiveString.empty())

        {

            if (InsertSoundAboveIncludeInParent(text, u.insideIncInfo.incDirectiveString, insideSoundStatement))

            {

                insideHandledInInc = true;

            }

        }

    }



    // 4. Update slots that are defined directly in the root stock file

    size_t wagonSoundStart = (size_t)-1, wagonSoundEnd = (size_t)-1;

    size_t wagonBlockCloseParen = (size_t)-1;



    size_t engineSoundStart = (size_t)-1, engineSoundEnd = (size_t)-1;

    size_t engineBlockCloseParen = (size_t)-1;



    size_t ortsSoundStart = (size_t)-1, ortsSoundEnd = (size_t)-1;

    size_t ortsBlockCloseParen = (size_t)-1;



    size_t insideSoundStart = (size_t)-1, insideSoundEnd = (size_t)-1;

    size_t insideBlockCloseParen = (size_t)-1;



    size_t i = 0;

    size_t len = text.length();

    std::vector<std::wstring> blockStack;



    auto skipWhitespaceAndComments = [&]() {

        while (i < len)

        {

            wchar_t c = text[i];

            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')

            {

                i++;

                continue;

            }

            if (c == L'#' || (c == L'/' && i + 1 < len && text[i + 1] == L'/'))

            {

                while (i < len && text[i] != L'\n') i++;

                continue;

            }

            break;

        }

    };



    while (i < len)

    {

        skipWhitespaceAndComments();

        if (i >= len) break;



        wchar_t c = text[i];

        if (c == L'(')

        {

            blockStack.push_back(L"");

            i++;

            continue;

        }

        else if (c == L')')

        {

            if (!blockStack.empty())

            {

                const std::wstring& closingBlock = blockStack.back();

                if (closingBlock == L"wagon") wagonBlockCloseParen = i;

                else if (closingBlock == L"engine") engineBlockCloseParen = i;

                else if (closingBlock == L"orts3dcab" || closingBlock == L"orts3dcabfile") ortsBlockCloseParen = i;

                else if (closingBlock == L"inside") insideBlockCloseParen = i;

                blockStack.pop_back();

            }

            i++;

            continue;

        }



        if (c == L'"')

        {

            i++;

            while (i < len && text[i] != L'"') i++;

            if (i < len) i++;

            continue;

        }



        size_t tokenStart = i;

        while (i < len && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n' && text[i] != L'(' && text[i] != L')' && text[i] != L'#' && text[i] != L'"')

        {

            i++;

        }

        std::wstring token = text.substr(tokenStart, i - tokenStart);

        if (token.empty()) continue;



        std::wstring tokenLower = token;

        std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), ::towlower);



        skipWhitespaceAndComments();



        if (i < len && text[i] == L'(')

        {

            i++; // skip '('



            if (tokenLower == L"comment" || tokenLower == L"skip")

            {

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    i++;

                }

                continue;

            }



            if (tokenLower == L"sound")

            {

                size_t soundStatementStart = tokenStart;

                int pDepth = 1;

                while (i < len && pDepth > 0)

                {

                    if (text[i] == L'"')

                    {

                        i++;

                        while (i < len && text[i] != L'"') i++;

                        if (i < len) i++;

                        continue;

                    }

                    if (text[i] == L'(') pDepth++;

                    else if (text[i] == L')') pDepth--;

                    if (pDepth > 0) i++;

                }

                if (i < len && text[i] == L')') i++;

                size_t soundStatementEnd = i;



                bool isORTS3D = false;

                bool isInside = false;

                bool isEngine = false;

                bool isWagon = false;



                for (const auto& blk : blockStack)

                {

                    if (blk == L"orts3dcab" || blk == L"orts3dcabfile") isORTS3D = true;

                    else if (blk == L"inside") isInside = true;

                    else if (blk == L"engine") isEngine = true;

                    else if (blk == L"wagon") isWagon = true;

                }



                if (isORTS3D)

                {

                    ortsSoundStart = soundStatementStart;

                    ortsSoundEnd = soundStatementEnd;

                }

                else if (isInside)

                {

                    insideSoundStart = soundStatementStart;

                    insideSoundEnd = soundStatementEnd;

                }

                else if (isEngine)

                {

                    engineSoundStart = soundStatementStart;

                    engineSoundEnd = soundStatementEnd;

                }

                else if (isWagon)

                {

                    wagonSoundStart = soundStatementStart;

                    wagonSoundEnd = soundStatementEnd;

                }

                continue;

            }



            blockStack.push_back(tokenLower);

        }

    }



    struct TextReplacement {

        size_t startPos;

        size_t endPos;

        std::wstring newText;

    };

    std::vector<TextReplacement> replacements;



    if (!wagonHandledInInc)

    {

        if (wagonSoundStart != (size_t)-1)

        {

            replacements.push_back({ wagonSoundStart, wagonSoundEnd, wagonSoundStatement });

        }

        else if (!u.wagonSounds.empty() && wagonBlockCloseParen != (size_t)-1)

        {

            replacements.push_back({ wagonBlockCloseParen, wagonBlockCloseParen, L"\t" + wagonSoundStatement + L"\r\n" });

        }

    }



    if (u.type == L"Engine" && !engineHandledInInc)

    {

        if (engineSoundStart != (size_t)-1)

        {

            replacements.push_back({ engineSoundStart, engineSoundEnd, engineSoundStatement });

        }

        else if (!u.engineSounds.empty() && engineBlockCloseParen != (size_t)-1)

        {

            replacements.push_back({ engineBlockCloseParen, engineBlockCloseParen, L"\t" + engineSoundStatement + L"\r\n" });

        }

    }



    if (u.hasORTS3DCabBlock && !ortsHandledInInc)

    {

        if (ortsSoundStart != (size_t)-1)

        {

            replacements.push_back({ ortsSoundStart, ortsSoundEnd, ortsSoundStatement });

        }

        else if (!u.orts3dCabSounds.empty() && ortsBlockCloseParen != (size_t)-1)

        {

            replacements.push_back({ ortsBlockCloseParen, ortsBlockCloseParen, L"\t\t" + ortsSoundStatement + L"\r\n\t" });

        }

    }



    if (u.hasInsideBlock && !insideHandledInInc)

    {

        if (insideSoundStart != (size_t)-1)

        {

            replacements.push_back({ insideSoundStart, insideSoundEnd, insideSoundStatement });

        }

        else if (!u.insideSounds.empty() && insideBlockCloseParen != (size_t)-1)

        {

            replacements.push_back({ insideBlockCloseParen, insideBlockCloseParen, L"\t\t" + insideSoundStatement + L"\r\n\t" });

        }

    }



    std::sort(replacements.begin(), replacements.end(), [](const TextReplacement& a, const TextReplacement& b) {

        return a.startPos > b.startPos;

    });



    for (const auto& rep : replacements)

    {

        if (rep.startPos <= text.length() && rep.endPos <= text.length() && rep.startPos <= rep.endPos)

        {

            text.replace(rep.startPos, rep.endPos - rep.startPos, rep.newText);

        }

    }



    bool writeOk = WriteWStringToStockFile(u.fullPath, text, encoding);

    if (writeOk)

    {

        u.isModified = false;

    }

    return writeOk;

}





static void BuildDistinctVehicles(RollingStockStudioState* pState)

{

    if (!pState) return;



    std::unordered_map<std::wstring, bool> prevCardExp;

    std::unordered_map<std::wstring, bool> prevWagonSecExp;

    std::unordered_map<std::wstring, bool> prevEngineSecExp;

    for (const auto& dv : pState->distinctVehicles)

    {

        prevCardExp[dv.key] = dv.isCardExpanded;

        prevWagonSecExp[dv.key] = dv.isWagonSectionExpanded;

        prevEngineSecExp[dv.key] = dv.isEngineSectionExpanded;

    }



    pState->distinctVehicles.clear();

    std::unordered_map<std::wstring, size_t> keyMap;



    for (size_t i = 0; i < pState->unitsSoundData.size(); ++i)

    {

        const auto& u = pState->unitsSoundData[i];

        std::wstring key = u.folder + L"\\" + u.name + u.ext;

        std::transform(key.begin(), key.end(), key.begin(), ::towlower);



        auto it = keyMap.find(key);

        if (it != keyMap.end())

        {

            pState->distinctVehicles[it->second].unitIndices.push_back(u.index);

        }

        else

        {

            RSSDistinctVehicle dv;

            dv.key = key;

            dv.name = u.name;

            dv.folder = u.folder;

            dv.type = u.type;

            dv.ext = u.ext;

            dv.fullPath = u.fullPath;

            dv.fileFound = u.fileFound;

            dv.isEngine = (u.type == L"Engine");

            dv.hasORTS3DCabBlock = u.hasORTS3DCabBlock;

            dv.hasInsideBlock = u.hasInsideBlock;

            dv.isModified = u.isModified;



            dv.wagonSounds = u.wagonSounds;

            dv.engineSounds = u.engineSounds;

            dv.orts3dCabSounds = u.orts3dCabSounds;

            dv.insideSounds = u.insideSounds;



            dv.unitIndices.push_back(u.index);

            dv.firstUnitIndex = u.index;



            dv.isCardExpanded = (prevCardExp.find(key) != prevCardExp.end()) ? prevCardExp[key] : true;

            dv.isWagonSectionExpanded = (prevWagonSecExp.find(key) != prevWagonSecExp.end()) ? prevWagonSecExp[key] : true;

            dv.isEngineSectionExpanded = (prevEngineSecExp.find(key) != prevEngineSecExp.end()) ? prevEngineSecExp[key] : true;



            keyMap[key] = pState->distinctVehicles.size();

            pState->distinctVehicles.push_back(dv);

        }

    }
}

static void RSS_PushUndo(RollingStockStudioState* pState, const std::wstring& actionDesc)
{
    if (!pState) return;
    SoundStudioHistoryStep step;
    step.actionDesc = actionDesc;
    step.unitsSoundData = pState->unitsSoundData;
    pState->undoStack.push_back(step);
    pState->redoStack.clear();
    if (pState->undoStack.size() > 50)
    {
        pState->undoStack.erase(pState->undoStack.begin());
    }
    if (pState->hWnd) InvalidateRect(pState->hWnd, NULL, FALSE);
}

static void RSS_PerformUndo(RollingStockStudioState* pState)
{
    if (!pState || pState->undoStack.empty()) return;
    SoundStudioHistoryStep step = pState->undoStack.back();
    pState->undoStack.pop_back();

    SoundStudioHistoryStep redoStep;
    redoStep.actionDesc = step.actionDesc;
    redoStep.unitsSoundData = pState->unitsSoundData;
    pState->redoStack.push_back(redoStep);

    pState->unitsSoundData = step.unitsSoundData;

    BuildDistinctVehicles(pState);
    PopulateSoundMatrix(pState);
    if (pState->hWnd) InvalidateRect(pState->hWnd, NULL, FALSE);
    if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);
}

static void RSS_PerformRedo(RollingStockStudioState* pState)
{
    if (!pState || pState->redoStack.empty()) return;
    SoundStudioHistoryStep step = pState->redoStack.back();
    pState->redoStack.pop_back();

    SoundStudioHistoryStep undoStep;
    undoStep.actionDesc = step.actionDesc;
    undoStep.unitsSoundData = pState->unitsSoundData;
    pState->undoStack.push_back(undoStep);

    pState->unitsSoundData = step.unitsSoundData;

    BuildDistinctVehicles(pState);
    PopulateSoundMatrix(pState);
    if (pState->hWnd) InvalidateRect(pState->hWnd, NULL, FALSE);
    if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);
}

static void SyncDistinctVehicleToUnits(RollingStockStudioState* pState, size_t distinctIdx)

{

    if (!pState || distinctIdx >= pState->distinctVehicles.size()) return;

    const auto& dv = pState->distinctVehicles[distinctIdx];



    for (auto& u : pState->unitsSoundData)

    {

        std::wstring key = u.folder + L"\\" + u.name + u.ext;

        std::transform(key.begin(), key.end(), key.begin(), ::towlower);

        if (key == dv.key)

        {

            u.wagonSounds = dv.wagonSounds;

            u.engineSounds = dv.engineSounds;

            u.orts3dCabSounds = dv.orts3dCabSounds;

            u.insideSounds = dv.insideSounds;

            u.isModified = true;

            BuildRowSoundCells(u);

        }

    }

    pState->isModified = true;

}



static void SelectSoundInInspector(RollingStockStudioState* pState, const RSSSoundEntry* pEntry, const RSSDistinctVehicle* pVehicle, const std::wstring& slotCategory)

{

    if (!pState) return;

    std::wstring smsPath = pEntry ? pEntry->fullDiskPath : L"";

    if (smsPath != pState->cachedSmsPath)

    {

        if (s_rssPlayingAudio)

        {

            RSS_StopAudio();

            pState->isAudioPlaying = false;

        }



        pState->cachedSmsPath = smsPath;

        pState->cachedPrimaryWav = L"";

        pState->cachedWavExists = false;

        pState->cachedSmsLoaded = false;

        pState->cachedSmsDoc = SmsDocument();



        if (!smsPath.empty() && pEntry && pEntry->existsOnDisk)

        {

            pState->cachedSmsLoaded = SmsParser_LoadFromFile(smsPath, pState->basePath, pState->cachedSmsDoc);

            if (pState->cachedSmsLoaded)

            {

                for (const auto& grp : pState->cachedSmsDoc.groups)

                {

                    for (const auto& st : grp.streams)

                    {

                        for (const auto& tr : st.triggers)

                        {

                            for (const auto& f : tr.files)

                            {

                                if (!f.resolvedDiskPath.empty() && f.existsOnDisk)

                                {

                                    pState->cachedPrimaryWav = f.resolvedDiskPath;

                                    pState->cachedWavExists = true;

                                    break;

                                }

                                else if (pState->cachedPrimaryWav.empty() && !f.fileName.empty())

                                {

                                    pState->cachedPrimaryWav = f.fileName;

                                    pState->cachedWavExists = false;

                                }

                            }

                            if (pState->cachedWavExists) break;

                        }

                        if (pState->cachedWavExists) break;

                    }

                    if (pState->cachedWavExists) break;

                }

            }

        }

    }



    InvalidateRect(pState->hWnd, &pState->rcInspectorCard, FALSE);

}



static void PopulateSoundMatrix(RollingStockStudioState* pState, int preferSelUnitIndex /*= -1*/)
{
    if (!pState || !pState->hSoundMatrixListWnd) return;

    for (size_t i = 0; i < pState->unitsSoundData.size(); ++i)
    {
        bool mod = false;
        if (i < pState->savedUnitsSoundData.size())
        {
            mod = (pState->unitsSoundData[i] != pState->savedUnitsSoundData[i]);
        }
        else
        {
            mod = true;
        }
        pState->unitsSoundData[i].isModified = mod;
    }

    BuildDistinctVehicles(pState);

    pState->displayRows.clear();
    pState->soundMatrixList.Clear();
    pState->totalMissingSounds = 0;
    pState->totalSoundStreams = 0;

    bool anyMod = false;
    for (size_t uIdx = 0; uIdx < pState->unitsSoundData.size(); ++uIdx)
    {
        auto& u = pState->unitsSoundData[uIdx];
        BuildRowSoundCells(u);
        pState->totalMissingSounds += u.missingCount;
        pState->totalSoundStreams += u.soundCount;
        if (u.isModified) anyMod = true;

        if (pState->matrixViewMode == SoundMatrixViewMode::GridView)
        {
            // 1. Parent Unit Row
            RSSDisplayRow parentRow;
            parentRow.rowType = RSS_ROW_UNIT;
            parentRow.unitIndex = (int)uIdx;
            parentRow.category = L"";
            parentRow.streamIndex = -1;
            pState->displayRows.push_back(parentRow);

            int totalStreams = (int)(u.wagonSounds.size() + u.engineSounds.size() + u.orts3dCabSounds.size() + u.insideSounds.size());
            std::wstring expandIcon = (totalStreams > 0) ? (u.isExpanded ? L"\x25BE " : L"\x25B8 ") : L"  ";
            std::wstring col0 = expandIcon + std::to_wstring(u.index);

            std::wstring wagonSummary = u.wagonCell;
            if (u.wagonSounds.size() > 1 && !u.isExpanded)
            {
                wagonSummary = u.wagonSounds[0].smsFileName + L" (+" + std::to_wstring(u.wagonSounds.size() - 1) + L" more)";
            }

            std::wstring unitDisplayName = u.isModified ? (L"● " + u.name) : u.name;
            pState->soundMatrixList.AddItem({
                col0,
                unitDisplayName,
                u.type,
                u.folder,
                wagonSummary,
                u.engineCell,
                u.orts3dCell,
                u.insideCell,
                u.statusCell
            });

            // 2. If Expanded: Child stream rows + Drop/Add action row
            if (u.isExpanded)
            {
                // Wagon Exterior Sounds
                for (size_t sIdx = 0; sIdx < u.wagonSounds.size(); ++sIdx)
                {
                    const auto& entry = u.wagonSounds[sIdx];
                    RSSDisplayRow streamRow;
                    streamRow.rowType = RSS_ROW_STREAM;
                    streamRow.unitIndex = (int)uIdx;
                    streamRow.category = L"Wagon";
                    streamRow.streamIndex = (int)sIdx;
                    pState->displayRows.push_back(streamRow);

                    std::wstring slotName = L"   \x21B3 Wagon Slot " + std::to_wstring(sIdx + 1);
                    std::wstring st = entry.existsOnDisk ? L"\x2713 OK" : L"\x2715 Missing";

                    pState->soundMatrixList.AddItem({
                        L"   \x21B3",
                        slotName,
                        L"Stream",
                        entry.parentFolder,
                        entry.smsFileName,
                        L"\x2014",
                        L"\x2014",
                        L"\x2014",
                        st
                    });
                }

                // Engine Cab Sounds
                for (size_t sIdx = 0; sIdx < u.engineSounds.size(); ++sIdx)
                {
                    const auto& entry = u.engineSounds[sIdx];
                    RSSDisplayRow streamRow;
                    streamRow.rowType = RSS_ROW_STREAM;
                    streamRow.unitIndex = (int)uIdx;
                    streamRow.category = L"Engine";
                    streamRow.streamIndex = (int)sIdx;
                    pState->displayRows.push_back(streamRow);

                    std::wstring slotName = L"   \x21B3 Cab Slot " + std::to_wstring(sIdx + 1);
                    std::wstring st = entry.existsOnDisk ? L"\x2713 OK" : L"\x2715 Missing";

                    pState->soundMatrixList.AddItem({
                        L"   \x21B3",
                        slotName,
                        L"Stream",
                        entry.parentFolder,
                        L"\x2014",
                        entry.smsFileName,
                        L"\x2014",
                        L"\x2014",
                        st
                    });
                }

                // ORTS3DCab Sounds
                for (size_t sIdx = 0; sIdx < u.orts3dCabSounds.size(); ++sIdx)
                {
                    const auto& entry = u.orts3dCabSounds[sIdx];
                    RSSDisplayRow streamRow;
                    streamRow.rowType = RSS_ROW_STREAM;
                    streamRow.unitIndex = (int)uIdx;
                    streamRow.category = L"ORTS3DCab";
                    streamRow.streamIndex = (int)sIdx;
                    pState->displayRows.push_back(streamRow);

                    std::wstring slotName = L"   \x21B3 ORTS3D Slot " + std::to_wstring(sIdx + 1);
                    std::wstring st = entry.existsOnDisk ? L"\x2713 OK" : L"\x2715 Missing";

                    pState->soundMatrixList.AddItem({
                        L"   \x21B3",
                        slotName,
                        L"Stream",
                        entry.parentFolder,
                        L"\x2014",
                        L"\x2014",
                        entry.smsFileName,
                        L"\x2014",
                        st
                    });
                }

                // Inside Sounds
                for (size_t sIdx = 0; sIdx < u.insideSounds.size(); ++sIdx)
                {
                    const auto& entry = u.insideSounds[sIdx];
                    RSSDisplayRow streamRow;
                    streamRow.rowType = RSS_ROW_STREAM;
                    streamRow.unitIndex = (int)uIdx;
                    streamRow.category = L"Inside";
                    streamRow.streamIndex = (int)sIdx;
                    pState->displayRows.push_back(streamRow);

                    std::wstring slotName = L"   \x21B3 Inside Slot " + std::to_wstring(sIdx + 1);
                    std::wstring st = entry.existsOnDisk ? L"\x2713 OK" : L"\x2715 Missing";

                    pState->soundMatrixList.AddItem({
                        L"   \x21B3",
                        slotName,
                        L"Stream",
                        entry.parentFolder,
                        L"\x2014",
                        L"\x2014",
                        L"\x2014",
                        entry.smsFileName,
                        st
                    });
                }

                // Drop Target & Add Stream row
                RSSDisplayRow addRow;
                addRow.rowType = RSS_ROW_ADD_BUTTON;
                addRow.unitIndex = (int)uIdx;
                addRow.category = L"";
                addRow.streamIndex = -1;
                pState->displayRows.push_back(addRow);

                pState->soundMatrixList.AddItem({
                    L"   +",
                    L"   [ + Add Stream / Drop .sms ]", L"", L"Drag .sms here or click ...", /*
                    L"",
                    L"",
                    */ L"+ Add Wagon SMS",
                    (u.type == L"Engine" ? L"+ Add Cab SMS" : L"\x2014"),
                    (u.type == L"Engine" ? L"+ Add 3D Cab SMS" : L"\x2014"),
                    L"+ Add Inside SMS",
                    L""
                });
            }
        }
        else
        {
            // DistinctCards mode: left 3-column consist rake navigator (#, Vehicle, Status)
            RSSDisplayRow navRow;
            navRow.rowType = RSS_ROW_UNIT;
            navRow.unitIndex = (int)uIdx;
            navRow.category = L"";
            navRow.streamIndex = -1;
            pState->displayRows.push_back(navRow);

            std::wstring st = (u.missingCount == 0) ? L"\x2713 Ready" : (L"\x26A0 " + std::to_wstring(u.missingCount));
            std::wstring unitDisplayName = u.isModified ? (L"● " + u.name) : u.name;
            pState->soundMatrixList.AddItem({
                std::to_wstring(u.index),
                unitDisplayName,
                st
            });
        }
    }
    pState->isModified = anyMod;

    int targetUnit = (preferSelUnitIndex >= 0 && preferSelUnitIndex < (int)pState->unitsSoundData.size()) ? preferSelUnitIndex : 0;
    int targetRow = 0;
    for (size_t r = 0; r < pState->displayRows.size(); ++r)
    {
        if (pState->displayRows[r].unitIndex == targetUnit && pState->displayRows[r].rowType == RSS_ROW_UNIT)
        {
            targetRow = (int)r;
            break;
        }
    }

    if (targetRow >= 0 && targetRow < (int)pState->displayRows.size())
    {
        pState->soundMatrixList.SetSelectedIndex(targetRow, false);
        UpdateInspectorSelection(pState, targetUnit);
    }

    if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))
    {
        pState->canvasScrollBar.SetRange(0, pState->canvasTotalHeight, pState->rcCanvas.bottom - pState->rcCanvas.top);
        InvalidateRect(pState->hCanvasWnd, NULL, FALSE);
    }

    InvalidateRect(pState->hWnd, NULL, TRUE);
}


// s_bSuppressMultiSmsWarning declared above



static void HandleAddDistinctButtonClick(RollingStockStudioState* pState, size_t distinctIdx, const std::wstring& slotCategory, int targetStreamIdx = -1)

{

    if (!pState || distinctIdx >= pState->distinctVehicles.size()) return;
    RSS_PushUndo(pState, L"Remove Distinct Sound Stream");
    auto& dv = pState->distinctVehicles[distinctIdx];



    std::vector<RSSSoundEntry>* pTargetList = nullptr;

    if (slotCategory == L"Engine") pTargetList = &dv.engineSounds;

    else if (slotCategory == L"ORTS3DCab") pTargetList = &dv.orts3dCabSounds;

    else if (slotCategory == L"Inside") pTargetList = &dv.insideSounds;

    else pTargetList = &dv.wagonSounds;



    if (!pTargetList) return;



    // Check Open Rails multi-SMS compatibility notice if appending an additional stream to an existing slot

    if (targetStreamIdx < 0 && !pTargetList->empty())

    {

        if (!s_bSuppressMultiSmsWarning)

        {

            bool bDontShowAgain = false;

            ShowModernMessageBoxWithCheckbox(

                pState->hWnd,

                L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",

                L"Open Rails Compatibility Notice",

                L"Do not show this warning again",

                &bDontShowAgain,

                MB_OK | MB_ICONINFORMATION

            );

            if (bDontShowAgain)

            {

                s_bSuppressMultiSmsWarning = true;

            }

        }

    }



    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn = { sizeof(ofn) };

    ofn.hwndOwner = pState->hWnd;

    ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";

    ofn.lpstrFile = szFile;

    ofn.nMaxFile = MAX_PATH;



    std::wstring bp = pState->basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';

    std::wstring trainsetDir = bp + L"TRAINS\\TRAINSET\\";



    ofn.lpstrInitialDir = trainsetDir.c_str();

    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;



    if (GetOpenFileNameW(&ofn))

    {

        std::wstring selectedPath = szFile;

        if (_wcsnicmp(selectedPath.c_str(), trainsetDir.c_str(), trainsetDir.length()) != 0)

        {

            ShowModernMessageBox(

                pState->hWnd,

                L"Invalid Sound File Location:\r\n\r\nIn Open Rails and MSTS, all sound (.sms) files must reside inside the TRAINS\\TRAINSET directory.\r\n\r\nPlease copy or move this soundset into your TRAINS\\TRAINSET folder before assigning it.",

                L"Invalid Sound Location",

                MB_OK | MB_ICONERROR

            );

            return;

        }



        RSSSoundEntry newEntry = CreateSoundEntryFromFullPath(selectedPath, slotCategory, pState->basePath);

        if (targetStreamIdx >= 0 && (size_t)targetStreamIdx < pTargetList->size())

        {

            (*pTargetList)[targetStreamIdx] = newEntry;

        }

        else

        {

            pTargetList->push_back(newEntry);

        }



        dv.isModified = true;

        pState->isModified = true;

        SyncDistinctVehicleToUnits(pState, distinctIdx);

        PopulateSoundMatrix(pState, -1);



        pState->selectedDistinctIndex = distinctIdx;

        SelectSoundInInspector(pState, &newEntry, &dv, slotCategory);



        if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

        InvalidateRect(pState->hWnd, &pState->rcInspectorCard, FALSE);

    }

}



static void HandleDeleteDistinctStream(RollingStockStudioState* pState, size_t distinctIdx, const std::wstring& slotCategory, size_t streamIdx)

{

    if (!pState || distinctIdx >= pState->distinctVehicles.size()) return;
    RSS_PushUndo(pState, L"Remove Distinct Sound Stream");
    auto& dv = pState->distinctVehicles[distinctIdx];



    std::vector<RSSSoundEntry>* pTargetList = nullptr;

    if (slotCategory == L"Engine") pTargetList = &dv.engineSounds;

    else if (slotCategory == L"ORTS3DCab") pTargetList = &dv.orts3dCabSounds;

    else if (slotCategory == L"Inside") pTargetList = &dv.insideSounds;

    else pTargetList = &dv.wagonSounds;



    if (pTargetList && streamIdx < pTargetList->size())

    {

        pTargetList->erase(pTargetList->begin() + streamIdx);

        dv.isModified = true;

        pState->isModified = true;

        SyncDistinctVehicleToUnits(pState, distinctIdx);

        PopulateSoundMatrix(pState, -1);



        if (pState->selectedDistinctIndex == distinctIdx)

        {

            if (!pTargetList->empty())

            {

                SelectSoundInInspector(pState, &(*pTargetList)[0], &dv, slotCategory);

            }

            else

            {

                SelectSoundInInspector(pState, nullptr, &dv, slotCategory);

            }

        }



        if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

        InvalidateRect(pState->hWnd, &pState->rcInspectorCard, FALSE);

    }

}



// Left Rake Navigator list callbacks

static bool RSS_NavRowBackgroundCallback(HDC hdc, int itemIndex, const RECT& rcRow, bool isSelected, bool isHovered, void* pParam)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)pParam;

    if (!pState || itemIndex < 0 || itemIndex >= (int)pState->unitsSoundData.size()) return false;

    const auto& u = pState->unitsSoundData[itemIndex];



    COLORREF bg = isSelected ? RGB(32, 60, 95) : (isHovered ? RGB(30, 36, 48) : RGB(22, 25, 33));

    HBRUSH hbr = CreateSolidBrush(bg);

    FillRect(hdc, &rcRow, hbr);

    DeleteObject(hbr);



    // Left accent pip

    COLORREF pipCol = (u.type == L"Engine") ? RGB(0, 145, 255) : RGB(0, 195, 150);

    RECT rcPip = { rcRow.left + 2, rcRow.top + 4, rcRow.left + 5, rcRow.bottom - 4 };

    HBRUSH hbrPip = CreateSolidBrush(pipCol);

    FillRect(hdc, &rcPip, hbrPip);

    DeleteObject(hbrPip);



    return true;

}



static bool RSS_RowBackgroundCallback(HDC hdc, int itemIndex, const RECT& rcRow, bool isSelected, bool isHovered, void* pParam)
{
    RollingStockStudioState* pState = (RollingStockStudioState*)pParam;
    if (!pState || pState->matrixViewMode != SoundMatrixViewMode::GridView) return false;
    if (itemIndex < 0 || itemIndex >= (int)pState->displayRows.size()) return false;

    const auto& dRow = pState->displayRows[itemIndex];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->unitsSoundData.size()) return false;
    const auto& u = pState->unitsSoundData[dRow.unitIndex];

    bool isEngine = (u.type == L"Engine");
    COLORREF accentColor = isEngine ? RGB(0, 145, 255) : RGB(0, 195, 150); // Electric Blue or Mint Green

    bool isFirstInCard = (dRow.rowType == RSS_ROW_UNIT);
    bool isLastInCard = (!u.isExpanded || (itemIndex + 1 >= (int)pState->displayRows.size()) || (pState->displayRows[itemIndex + 1].unitIndex != dRow.unitIndex));

    RECT rc = rcRow;

    // For multi-row expanded cards, close the 2px gap between rows inside the card
    // so the card body connects seamlessly from top to bottom
    if (!isFirstInCard)
    {
        rc.top = rcRow.top - 1; // Seamless connection to row above
    }
    else if (dRow.unitIndex > 0)
    {
        rc.top += 6; // 6px card gap at top of unit card (except unit 0)
    }

    if (!isLastInCard)
    {
        rc.bottom = rcRow.bottom + 1; // Seamless connection to row below
    }
    else
    {
        rc.bottom -= 1; // 1px bottom margin
    }

    COLORREF bgCol;
    COLORREF borderCol = RGB(46, 54, 68);

    if (dRow.rowType == RSS_ROW_UNIT)
    {
        bgCol = isEngine ? RGB(26, 34, 48) : RGB(23, 38, 42);
        if (isSelected) bgCol = RGB(36, 68, 110);
        else if (isHovered) bgCol = isEngine ? RGB(32, 44, 62) : RGB(29, 48, 54);
    }
    else if (dRow.rowType == RSS_ROW_STREAM)
    {
        bgCol = (dRow.streamIndex % 2 == 0) ? RGB(20, 24, 32) : RGB(23, 27, 36);
        if (isSelected) bgCol = RGB(28, 52, 85);
        else if (isHovered) bgCol = RGB(30, 36, 48);
    }
    else // RSS_ROW_ADD_BUTTON
    {
        bgCol = RGB(19, 23, 30);
        if (isSelected) bgCol = RGB(28, 52, 85);
        else if (isHovered) bgCol = RGB(26, 32, 44);
    }

    HBRUSH hbrBg = CreateSolidBrush(bgCol);
    HPEN hpenBorder = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ oldBr = SelectObject(hdc, hbrBg);
    HGDIOBJ oldPn = SelectObject(hdc, hpenBorder);

    if (isFirstInCard && isLastInCard)
    {
        // Standalone collapsed unit card (rounded 6px on all sides)
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    }
    else if (isFirstInCard)
    {
        // Top row of expanded card: rounded top corners, flat bottom (strictly clipped to rc)
        int savedDC = SaveDC(hdc);
        IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom + 6, 6, 6);
        RestoreDC(hdc, savedDC);

        // Crisp horizontal divider separating unit header from child stream rows
        HPEN hInnerPen = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ oldP = SelectObject(hdc, hInnerPen);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, oldP);
        DeleteObject(hInnerPen);
    }
    else if (isLastInCard)
    {
        // Bottom row of expanded card: flat top, rounded bottom corners (strictly clipped to rc)
        int savedDC = SaveDC(hdc);
        IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        RoundRect(hdc, rc.left, rc.top - 6, rc.right, rc.bottom, 6, 6);
        RestoreDC(hdc, savedDC);
    }
    else
    {
        // Middle rows of card: fill + left & right borders + bottom divider
        FillRect(hdc, &rc, hbrBg);
        HPEN hSidePen = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ oldP = SelectObject(hdc, hSidePen);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.left, rc.bottom);
        MoveToEx(hdc, rc.right - 1, rc.top, NULL); LineTo(hdc, rc.right - 1, rc.bottom);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, oldP);
        DeleteObject(hSidePen);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(hbrBg);
    DeleteObject(hpenBorder);

    // Left Accent Color Stripe
    int stripeLeft = rc.left + 1;
    int stripeW = 4;
    RECT rcStripe = { stripeLeft, rc.top + (isFirstInCard ? 2 : 0), stripeLeft + stripeW, rc.bottom - (isLastInCard ? 2 : 0) };
    if (dRow.rowType == RSS_ROW_UNIT)
    {
        HBRUSH hbrStripe = CreateSolidBrush(accentColor);
        FillRect(hdc, &rcStripe, hbrStripe);
        DeleteObject(hbrStripe);
    }
    else
    {
        COLORREF mutedAccent = isEngine ? RGB(32, 60, 95) : RGB(25, 72, 62);
        HBRUSH hbrStripe = CreateSolidBrush(mutedAccent);
        FillRect(hdc, &rcStripe, hbrStripe);
        DeleteObject(hbrStripe);
    }

    // Inter-card separator gap line
    if (isFirstInCard && dRow.unitIndex > 0)
    {
        HPEN hSepPen = CreatePen(PS_SOLID, 1, RGB(30, 35, 45));
        HGDIOBJ oldSep = SelectObject(hdc, hSepPen);
        MoveToEx(hdc, rc.left - 2, rc.top - 3, NULL);
        LineTo(hdc, rc.right + 2, rc.top - 3);
        SelectObject(hdc, oldSep);
        DeleteObject(hSepPen);
    }

    return true;
}

static bool RSS_IsCellInteractive(int itemIndex, int subItemIndex, void* pParam)
{
    RollingStockStudioState* pState = (RollingStockStudioState*)pParam;
    if (!pState || pState->matrixViewMode != SoundMatrixViewMode::GridView) return false;
    if (itemIndex < 0 || itemIndex >= (int)pState->displayRows.size()) return false;
    const auto& dRow = pState->displayRows[itemIndex];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->unitsSoundData.size()) return false;
    const auto& u = pState->unitsSoundData[dRow.unitIndex];

    if (dRow.rowType == RSS_ROW_UNIT)
    {
        if (subItemIndex == 0) return true; // Chevron
    }
    else if (dRow.rowType == RSS_ROW_ADD_BUTTON)
    {
        if (subItemIndex == 4) return true; // Add Wagon SMS
        if (subItemIndex == 5) return (u.type == L"Engine"); // Add Engine Cab SMS
        if (subItemIndex == 6) return (u.type == L"Engine"); // Add ORTS 3D Cab SMS
        if (subItemIndex == 7) return true; // Add Inside SMS
    }
    return false;
}

static void ConfigureSoundMatrixColumns(RollingStockStudioState* pState, int totalWidth)
{
    if (!pState || !pState->hSoundMatrixListWnd) return;

    pState->soundMatrixList.ClearColumns();
    if (pState->matrixViewMode == SoundMatrixViewMode::GridView)
    {
        int usableW = (totalWidth > 18) ? (totalWidth - 18) : 1000;
        int col0 = 42;  // #
        int col2 = 68;  // Type
        int col3 = 130; /*
        int col8 = 115; // Status
        */ int col1 = (usableW * 18) / 100; if (col1 < 220) col1 = 220; // Name / Stream
        int col8 = 115; // Status

        int fixedW = col0 + col1 + col2 + col3 + col8;
        int remAudio = usableW - fixedW; if (remAudio < 580) remAudio = 580;
        int audioBase = remAudio / 4; if (audioBase < 145) audioBase = 145;
        int col4 = audioBase; // Wagon Sound (Exterior)
        int col5 = audioBase; // Engine Cab Sound
        int col6 = audioBase; int col7 = remAudio - (audioBase * 3); if (col7 < 145) col7 = 145;

        pState->soundMatrixList.AddColumn(L"#", col0, 1);
        pState->soundMatrixList.AddColumn(L"Name / Stream", col1, 0);
        pState->soundMatrixList.AddColumn(L"Type", col2, 1);
        pState->soundMatrixList.AddColumn(L"Folder", col3, 0);
        pState->soundMatrixList.AddColumn(L"Wagon Sound (Exterior)", col4, 0);
        pState->soundMatrixList.AddColumn(L"Engine Cab Sound", col5, 0);
        pState->soundMatrixList.AddColumn(L"ORTS3DCab Sound", col6, 0);
        pState->soundMatrixList.AddColumn(L"Inside Sound", col7, 0);
        pState->soundMatrixList.AddColumn(L"Status", col8, 1);
    }
    else
    {
        int navW = (totalWidth > 0) ? totalWidth : 295;
        pState->soundMatrixList.AddColumn(L"#", 42, 1);
        pState->soundMatrixList.AddColumn(L"Vehicle", navW - 42 - 74 - 20, 0);
        pState->soundMatrixList.AddColumn(L"Status", 74, 1);
    }
}

static bool RSS_NavDrawCellCallback(HDC hdc, int itemIndex, int subItemIndex, const RECT& rcCell, bool isSelected, bool isHovered, void* pParam)
{
    RollingStockStudioState* pState = (RollingStockStudioState*)pParam;
    if (!pState) return false;

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    if (pState->matrixViewMode == SoundMatrixViewMode::DistinctCards)
    {
        if (itemIndex < 0 || itemIndex >= (int)pState->unitsSoundData.size()) return false;
        const auto& u = pState->unitsSoundData[itemIndex];

        if (subItemIndex == 0) // Index #
        {
            SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
            SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(140, 160, 185));
            RECT rc = rcCell;
            rc.left += 6;
            rc.right -= 2;
            std::wstring s = std::to_wstring(u.index);
            DrawTextW(hdc, s.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }
        else if (subItemIndex == 1) // Vehicle Name
        {
            SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(225, 230, 240));
            RECT rc = rcCell;
            rc.left += 2;
            rc.right -= 4;
            DrawTextW(hdc, u.name.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }
        else if (subItemIndex == 2) // Status pill
        {
            RECT rc = rcCell;
            RECT rcP = rc;
            rcP.top += 4; rcP.bottom -= 4;
            rcP.left += 2; rcP.right -= 6;
            if (rcP.right > rcP.left)
            {
                bool ok = (u.missingCount == 0);
                COLORREF bg = ok ? RGB(18, 48, 28) : RGB(56, 24, 24);
                COLORREF border = ok ? RGB(35, 100, 52) : RGB(120, 40, 40);
                COLORREF textCol = ok ? RGB(90, 215, 130) : RGB(255, 120, 110);

                HBRUSH hbr = CreateSolidBrush(bg);
                HPEN hpen = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ ob = SelectObject(hdc, hbr);
                HGDIOBJ op = SelectObject(hdc, hpen);
                RoundRect(hdc, rcP.left, rcP.top, rcP.right, rcP.bottom, 4, 4);
                SelectObject(hdc, ob); SelectObject(hdc, op);
                DeleteObject(hbr); DeleteObject(hpen);

                SelectObject(hdc, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);
                SetTextColor(hdc, textCol);
                std::wstring label = ok ? L"\x2713 Ready" : (L"\x26A0 " + std::to_wstring(u.missingCount));
                DrawTextW(hdc, label.c_str(), -1, &rcP, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }
    }
    else
    {
        // GridView mode: Full Card Styling with child streams and Add SMS buttons!
        if (itemIndex < 0 || itemIndex >= (int)pState->displayRows.size()) return false;
        const auto& dRow = pState->displayRows[itemIndex];
        if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->unitsSoundData.size()) return false;
        const auto& u = pState->unitsSoundData[dRow.unitIndex];

        bool isFirstInCard = (dRow.rowType == RSS_ROW_UNIT);
        bool isLastInCard = (!u.isExpanded || (itemIndex + 1 >= (int)pState->displayRows.size()) || (pState->displayRows[itemIndex + 1].unitIndex != dRow.unitIndex));

        RECT rc = rcCell;
        // Shift content down by 6px on unit row (except unit 0) to align with card top gap
        if (isFirstInCard && dRow.unitIndex > 0)
        {
            rc.top += 6;
        }
        // Align bottom bounds with card visual bottom margin for the last row
        if (isLastInCard)
        {
            rc.bottom -= 2;
        }

        // SubItem 0: Solid Triangle Chevron / Branch / Add Icon
        if (subItemIndex == 0)
        {
            if (dRow.rowType == RSS_ROW_UNIT)
            {
                int totalStreams = (int)(u.wagonSounds.size() + u.engineSounds.size() +
                                         u.orts3dCabSounds.size() + u.insideSounds.size());
                if (totalStreams > 0)
                {
                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(140, 185, 240));
                    RECT rcTriangle = { rc.left + 8, rc.top, rc.left + 20, rc.bottom };
                    DrawTextW(hdc, u.isExpanded ? L"\x25BE" : L"\x25B8", -1, &rcTriangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }

                SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(200, 215, 235));
                RECT rcNum = { rc.left + 22, rc.top, rc.right - 2, rc.bottom };
                std::wstring numStr = std::to_wstring(u.index);
                DrawTextW(hdc, numStr.c_str(), -1, &rcNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else if (dRow.rowType == RSS_ROW_STREAM)
            {
                SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, RGB(90, 115, 145));
                RECT rcBranch = { rc.left + 12, rc.top, rc.right, rc.bottom };
                DrawTextW(hdc, L"\x21B3", -1, &rcBranch, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }

        // SubItem 1: Vehicle Name / Stream Description / Add Action Label
        if (subItemIndex == 1)
        {
            if (dRow.rowType == RSS_ROW_UNIT)
            {
                SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(235, 240, 250));
                RECT rcName = rc;
                rcName.left += 6; rcName.right -= 6;
                DrawTextW(hdc, u.name.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            else if (dRow.rowType == RSS_ROW_STREAM)
            {
                SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(175, 190, 210));
                RECT rcSlot = rc;
                rcSlot.left += 4; rcSlot.right -= 6;

                std::wstring streamDesc = L"Wagon Slot " + std::to_wstring(dRow.streamIndex + 1);
                if (dRow.category == L"Wagon") streamDesc = L"Wagon Slot " + std::to_wstring(dRow.streamIndex + 1);
                else if (dRow.category == L"Engine") streamDesc = L"Cab Slot " + std::to_wstring(dRow.streamIndex + 1);
                else if (dRow.category == L"ORTS3DCab") streamDesc = L"ORTS3D Slot " + std::to_wstring(dRow.streamIndex + 1);
                else if (dRow.category == L"Inside") streamDesc = L"Inside Slot " + std::to_wstring(dRow.streamIndex + 1);

                DrawTextW(hdc, streamDesc.c_str(), -1, &rcSlot, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            else // RSS_ROW_ADD_BUTTON
            {
                SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                SetTextColor(hdc, RGB(115, 140, 175));
                RECT rcAddDesc = rc;
                rcAddDesc.left += 6; rcAddDesc.right -= 6;
                DrawTextW(hdc, L"[ + Add Stream / Drop .sms ]", -1, &rcAddDesc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }

        // SubItem 2: Type Pill / Sub-Slot Badge
        if (subItemIndex == 2)
        {
            if (dRow.rowType == RSS_ROW_UNIT)
            {
                int cellW = rc.right - rc.left;
                int innerTop = rc.top + 2;
                int innerBottom = rc.bottom - 2;
                int innerH = innerBottom - innerTop;
                int pillH = (innerH >= 24) ? 20 : 18;
                int pillY = innerTop + (innerH - pillH) / 2;
                int pillW = (std::min)(cellW - 8, 56);
                int pillX = rc.left + (cellW - pillW) / 2;
                RECT rcPill = { pillX, pillY, pillX + pillW, pillY + pillH };
                if (rcPill.right > rcPill.left)
                {
                    bool isEng = (u.type == L"Engine");
                    COLORREF pillBg = isEng ? RGB(18, 48, 88) : RGB(48, 36, 18);
                    COLORREF pillBorder = isEng ? RGB(32, 90, 160) : RGB(120, 85, 35);
                    COLORREF pillText = isEng ? RGB(100, 185, 255) : RGB(255, 175, 80);

                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);
                    HGDIOBJ ob = SelectObject(hdc, hbr); HGDIOBJ op = SelectObject(hdc, hp);
                    RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                    SelectObject(hdc, ob); SelectObject(hdc, op);
                    DeleteObject(hbr); DeleteObject(hp);

                    SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                    SetTextColor(hdc, pillText);
                    DrawTextW(hdc, u.type.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            else if (dRow.rowType == RSS_ROW_STREAM)
            {
                std::wstring badgeText = L"Stream"; /*
                if (dRow.category == L"Engine") badgeText = L"Cab";
                else if (dRow.category == L"ORTS3DCab") badgeText = L"3D Cab";
                else if (dRow.category == L"Inside") badgeText = L"Inside"; */

                SelectObject(hdc, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);
                SetTextColor(hdc, RGB(120, 130, 145));
                RECT rcBadge = rc;
                DrawTextW(hdc, badgeText.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else // RSS_ROW_ADD_BUTTON
            {
                SelectObject(hdc, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);
                SetTextColor(hdc, RGB(90, 105, 125));
                RECT rcAction = rc;
                DrawTextW(hdc, L"", -1, &rcAction, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }

        // SubItem 3: Folder
        if (subItemIndex == 3)
        {
            if (dRow.rowType == RSS_ROW_UNIT)
            {
                SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(150, 165, 185));
                RECT rcFol = rc;
                rcFol.left += 6; rcFol.right -= 6;
                DrawTextW(hdc, u.folder.c_str(), -1, &rcFol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            else if (dRow.rowType == RSS_ROW_STREAM)
            {
                const RSSSoundEntry* pEntry = nullptr;
                if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.wagonSounds.size()) pEntry = &u.wagonSounds[dRow.streamIndex];
                else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.engineSounds.size()) pEntry = &u.engineSounds[dRow.streamIndex];
                else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.orts3dCabSounds.size()) pEntry = &u.orts3dCabSounds[dRow.streamIndex];
                else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.insideSounds.size()) pEntry = &u.insideSounds[dRow.streamIndex];

                if (pEntry)
                {
                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(130, 145, 165));
                    RECT rcFol = rc;
                    rcFol.left += 6; rcFol.right -= 6;
                    DrawTextW(hdc, pEntry->parentFolder.c_str(), -1, &rcFol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                }
            }
            else if (dRow.rowType == RSS_ROW_ADD_BUTTON)
            {
                SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, RGB(95, 110, 130));
                RECT rcH = rc;
                rcH.left += 6; rcH.right -= 6;
                DrawTextW(hdc, L"Drag .sms here or click ...", -1, &rcH, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }

        // SubItems 4..7: Audio Chips / Add SMS Buttons / Centered Dashes
        if (subItemIndex >= 4 && subItemIndex <= 7)
        {
            std::wstring colCat = L"Wagon";
            if (subItemIndex == 5) colCat = L"Engine";
            else if (subItemIndex == 6) colCat = L"ORTS3DCab";
            else if (subItemIndex == 7) colCat = L"Inside";

            int cellW = rc.right - rc.left;
            int innerTop = rc.top + 2;
            int innerBottom = rc.bottom - 2;
            int innerH = innerBottom - innerTop;
            if (innerH < 18) innerH = 18;
            int chipH = (innerH >= 24) ? 20 : 18;
            int chipY = innerTop + (innerH - chipH) / 2;

            if (dRow.rowType == RSS_ROW_STREAM)
            {
                if (dRow.category == colCat)
                {
                    const RSSSoundEntry* pEntry = nullptr;
                    if (colCat == L"Wagon" && dRow.streamIndex < (int)u.wagonSounds.size()) pEntry = &u.wagonSounds[dRow.streamIndex];
                    else if (colCat == L"Engine" && dRow.streamIndex < (int)u.engineSounds.size()) pEntry = &u.engineSounds[dRow.streamIndex];
                    else if (colCat == L"ORTS3DCab" && dRow.streamIndex < (int)u.orts3dCabSounds.size()) pEntry = &u.orts3dCabSounds[dRow.streamIndex];
                    else if (colCat == L"Inside" && dRow.streamIndex < (int)u.insideSounds.size()) pEntry = &u.insideSounds[dRow.streamIndex];

                    if (pEntry)
                    {
                        std::wstring chipTextStr = pEntry->smsFileName;
                        SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        RECT rcMeasure = { 0, 0, 0, 0 };
                        DrawTextW(hdc, chipTextStr.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                        int textW = rcMeasure.right - rcMeasure.left;
                        int chipW = 26 + textW + 14;
                        if (chipW > cellW - 8) chipW = cellW - 8;
                        if (chipW < 70) chipW = 70;
                        int chipX = rc.left + (cellW - chipW) / 2;
                        RECT rcChip = { chipX, chipY, chipX + chipW, chipY + chipH };

                        COLORREF chipBg = pEntry->existsOnDisk ? RGB(28, 36, 48) : RGB(46, 28, 28);
                        COLORREF chipBorder = pEntry->existsOnDisk ? RGB(50, 75, 110) : RGB(105, 50, 50);
                        COLORREF chipText = pEntry->existsOnDisk ? RGB(220, 230, 245) : RGB(255, 150, 140);

                        HBRUSH hbr = CreateSolidBrush(chipBg); HPEN hp = CreatePen(PS_SOLID, 1, chipBorder);
                        HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);
                        RoundRect(hdc, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                        SelectObject(hdc, oB); SelectObject(hdc, oP);
                        DeleteObject(hbr); DeleteObject(hp);

                        SelectObject(hdc, pState->hFontSmallIcons ? pState->hFontSmallIcons : pState->hFontBody);
                        SetTextColor(hdc, pEntry->existsOnDisk ? RGB(0, 160, 240) : RGB(255, 120, 100));
                        RECT rcIcon = { rcChip.left + 5, rcChip.top, rcChip.left + 21, rcChip.bottom };
                        DrawTextW(hdc, L"\xE767", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        SetTextColor(hdc, chipText);
                        RECT rcLabel = { rcChip.left + 23, rcChip.top, rcChip.right - 5, rcChip.bottom };
                        DrawTextW(hdc, pEntry->smsFileName.c_str(), -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    }
                }
                else
                {
                    // Non-matching category column in stream row: muted dash
                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, RGB(75, 85, 100));
                    RECT rcDash = rc;
                    DrawTextW(hdc, L"\x2014", -1, &rcDash, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            else if (dRow.rowType == RSS_ROW_ADD_BUTTON)
            {
                bool isSlotEnabled = true;
                if ((colCat == L"Engine" || colCat == L"ORTS3DCab") && u.type != L"Engine")
                    isSlotEnabled = false;

                if (isSlotEnabled)
                {
                    std::wstring btnLabel = L"+ Add Wagon SMS";
                    if (colCat == L"Engine") btnLabel = L"+ Add Cab SMS";
                    else if (colCat == L"ORTS3DCab") btnLabel = L"+ Add 3D Cab SMS";
                    else if (colCat == L"Inside") btnLabel = L"+ Add Inside SMS";

                    SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                    RECT rcMeasure = { 0, 0, 0, 0 };
                    DrawTextW(hdc, btnLabel.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    int textW = rcMeasure.right - rcMeasure.left;
                    int btnW = textW + 24;
                    if (btnW > cellW - 8) btnW = cellW - 8;
                    if (btnW < 80) btnW = 80;
                    int btnX = rc.left + (cellW - btnW) / 2;
                    RECT rcBtn = { btnX, chipY, btnX + btnW, chipY + chipH };

                    bool btnHov = isHovered; // Hover state on interactive add button cell
                    COLORREF btnBg = btnHov ? RGB(32, 48, 68) : RGB(22, 30, 40);
                    COLORREF btnBorder = btnHov ? RGB(0, 160, 245) : RGB(45, 65, 88);
                    COLORREF btnText = btnHov ? RGB(255, 255, 255) : RGB(130, 175, 220);

                    HBRUSH hbr = CreateSolidBrush(btnBg); HPEN hp = CreatePen(PS_SOLID, 1, btnBorder);
                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);
                    RoundRect(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 4, 4);
                    SelectObject(hdc, oB); SelectObject(hdc, oP);
                    DeleteObject(hbr); DeleteObject(hp);

                    SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                    SetTextColor(hdc, btnText);
                    DrawTextW(hdc, btnLabel.c_str(), -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else
                {
                    // Disabled slot for Wagon (Engine/ORTS3DCab)
                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, RGB(65, 72, 85));
                    RECT rcDash = rc;
                    DrawTextW(hdc, L"\x2014", -1, &rcDash, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            else // RSS_ROW_UNIT
            {
                const std::vector<RSSSoundEntry>* pVec = nullptr;
                if (colCat == L"Wagon") pVec = &u.wagonSounds;
                else if (colCat == L"Engine") pVec = &u.engineSounds;
                else if (colCat == L"ORTS3DCab") pVec = &u.orts3dCabSounds;
                else if (colCat == L"Inside") pVec = &u.insideSounds;

                if (pVec && !pVec->empty())
                {
                    const auto& entry = (*pVec)[0];
                    std::wstring chipTextStr = entry.smsFileName;
                    if (pVec->size() > 1 && !u.isExpanded)
                    {
                        chipTextStr += L" (+" + std::to_wstring(pVec->size() - 1) + L")";
                    }

                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    RECT rcMeasure = { 0, 0, 0, 0 };
                    DrawTextW(hdc, chipTextStr.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    int textW = rcMeasure.right - rcMeasure.left;
                    int chipW = 26 + textW + 14;
                    if (chipW > cellW - 8) chipW = cellW - 8;
                    if (chipW < 70) chipW = 70;
                    int chipX = rc.left + (cellW - chipW) / 2;
                    RECT rcChip = { chipX, chipY, chipX + chipW, chipY + chipH };

                    COLORREF chipBg = entry.existsOnDisk ? RGB(28, 36, 48) : RGB(46, 28, 28);
                    COLORREF chipBorder = entry.existsOnDisk ? RGB(50, 75, 110) : RGB(105, 50, 50);
                    COLORREF chipText = entry.existsOnDisk ? RGB(220, 230, 245) : RGB(255, 150, 140);

                    HBRUSH hbr = CreateSolidBrush(chipBg); HPEN hp = CreatePen(PS_SOLID, 1, chipBorder);
                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);
                    RoundRect(hdc, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                    SelectObject(hdc, oB); SelectObject(hdc, oP);
                    DeleteObject(hbr); DeleteObject(hp);

                    SelectObject(hdc, pState->hFontSmallIcons ? pState->hFontSmallIcons : pState->hFontBody);
                    SetTextColor(hdc, entry.existsOnDisk ? RGB(0, 160, 240) : RGB(255, 120, 100));
                    RECT rcIcon = { rcChip.left + 5, rcChip.top, rcChip.left + 21, rcChip.bottom };
                    DrawTextW(hdc, L"\xE767", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, chipText);
                    RECT rcLabel = { rcChip.left + 23, rcChip.top, rcChip.right - 5, rcChip.bottom };
                    DrawTextW(hdc, chipTextStr.c_str(), -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                }
                else
                {
                    SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                    SetTextColor(hdc, RGB(65, 72, 85));
                    RECT rcDash = rc;
                    DrawTextW(hdc, L"\x2014", -1, &rcDash, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }

        // SubItem 8: Status Pill (Ready / Missing, or OK / Missing)
        if (subItemIndex == 8)
        {
            int cellW = rc.right - rc.left;
            int innerTop = rc.top + 2;
            int innerBottom = rc.bottom - 2;
            int innerH = innerBottom - innerTop;
            int pillH = (innerH >= 24) ? 20 : 18;
            int pillY = innerTop + (innerH - pillH) / 2;

            if (dRow.rowType == RSS_ROW_UNIT)
            {
                bool isReady = (u.missingCount == 0);
                COLORREF pillBg = isReady ? RGB(16, 52, 28) : RGB(64, 20, 20);
                COLORREF pillBorder = isReady ? RGB(32, 110, 56) : RGB(140, 40, 40);
                COLORREF pillText = isReady ? RGB(90, 220, 125) : RGB(255, 100, 100);
                std::wstring label = isReady ? L"\x2713 Ready" : (L"\x26A0 Missing (" + std::to_wstring(u.missingCount) + L")");

                SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                RECT rcMeasure = { 0, 0, 0, 0 };
                DrawTextW(hdc, label.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                int textW = rcMeasure.right - rcMeasure.left;
                int pillW = textW + 20;
                if (pillW > cellW - 8) pillW = cellW - 8;
                if (pillW < 50) pillW = 50;
                int pillX = rc.left + (cellW - pillW) / 2;
                RECT rcPill = { pillX, pillY, pillX + pillW, pillY + pillH };
                if (rcPill.right > rcPill.left)
                {
                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);
                    HGDIOBJ ob = SelectObject(hdc, hbr); HGDIOBJ op = SelectObject(hdc, hp);
                    RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                    SelectObject(hdc, ob); SelectObject(hdc, op);
                    DeleteObject(hbr); DeleteObject(hp);

                    SetTextColor(hdc, pillText);
                    DrawTextW(hdc, label.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            else if (dRow.rowType == RSS_ROW_STREAM)
            {
                const RSSSoundEntry* pEntry = nullptr;
                if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.wagonSounds.size()) pEntry = &u.wagonSounds[dRow.streamIndex];
                else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.engineSounds.size()) pEntry = &u.engineSounds[dRow.streamIndex];
                else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.orts3dCabSounds.size()) pEntry = &u.orts3dCabSounds[dRow.streamIndex];
                else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.insideSounds.size()) pEntry = &u.insideSounds[dRow.streamIndex];

                bool exists = pEntry ? pEntry->existsOnDisk : false;
                COLORREF pillBg = exists ? RGB(16, 52, 28) : RGB(64, 20, 20);
                COLORREF pillBorder = exists ? RGB(32, 110, 56) : RGB(140, 40, 40);
                COLORREF pillText = exists ? RGB(90, 220, 125) : RGB(255, 100, 100);
                std::wstring label = exists ? L"\x2713 OK" : L"\x2715 Missing";

                SelectObject(hdc, pState->hFontBold ? pState->hFontBold : pState->hFontBody);
                RECT rcMeasure = { 0, 0, 0, 0 };
                DrawTextW(hdc, label.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                int textW = rcMeasure.right - rcMeasure.left;
                int pillW = textW + 20;
                if (pillW > cellW - 8) pillW = cellW - 8;
                if (pillW < 44) pillW = 44;
                int pillX = rc.left + (cellW - pillW) / 2;
                RECT rcPill = { pillX, pillY, pillX + pillW, pillY + pillH };
                if (rcPill.right > rcPill.left)
                {
                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);
                    HGDIOBJ ob = SelectObject(hdc, hbr); HGDIOBJ op = SelectObject(hdc, hp);
                    RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                    SelectObject(hdc, ob); SelectObject(hdc, op);
                    DeleteObject(hbr); DeleteObject(hp);

                    SetTextColor(hdc, pillText);
                    DrawTextW(hdc, label.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);
            return true;
        }
    }

    SelectObject(hdc, hOldFont);
    SetBkMode(hdc, oldBkMode);
    return false;
}

static void RepositionStudioControls(RollingStockStudioState* pState)
{
    if (!pState || !pState->hWnd || !IsWindow(pState->hWnd)) return;

    RECT rcClient;
    GetClientRect(pState->hWnd, &rcClient);
    int totalW = rcClient.right - rcClient.left;
    int totalH = rcClient.bottom - rcClient.top;
    if (totalW <= 100 || totalH <= 100) return;

    int marginX = 8;
    int marginY = 6;
    int tableX = marginX;
    int tableY = 44;
    int tableW = totalW - (marginX * 2);

    if (pState->activeSubTab == 0) // Consist Sound Matrix
    {
        int bannerH = 28;
        int bannerY = 44 + marginY;
        pState->rcConsistBanner = { tableX, bannerY, tableX + tableW, bannerY + bannerH };
        tableY = bannerY + bannerH + marginY;

        int availableH = totalH - tableY - marginY;
        int inspectorGap = 8;
        int inspectorH = (availableH * 28) / 100;
        if (inspectorH < 155) inspectorH = 155;
        if (inspectorH > 220) inspectorH = 220;

        int matrixH = availableH - inspectorH - inspectorGap;
        if (matrixH < 180) matrixH = 180;
        int inspectorY = tableY + matrixH + inspectorGap;

        pState->rcTableCard = { tableX, tableY, tableX + tableW, tableY + matrixH };
        pState->rcInspectorCard = { tableX, inspectorY, tableX + tableW, inspectorY + inspectorH };

        if (pState->matrixViewMode == SoundMatrixViewMode::GridView)
        {
            // GridView: Full width table with Card Styling
            if (pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))
            {
                SetWindowPos(pState->hSoundMatrixListWnd, NULL,
                    tableX, tableY, tableW, matrixH,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
                ConfigureSoundMatrixColumns(pState, tableW);
                pState->soundMatrixList.SetRowHeight(32);
                pState->soundMatrixList.SetShowSelectionGutter(false);
                pState->soundMatrixList.SetShowVerticalDividers(true);
                pState->soundMatrixList.SetRowBackgroundCallback(RSS_RowBackgroundCallback, pState);
                pState->soundMatrixList.SetCellInteractiveCallback(RSS_IsCellInteractive, pState);
            }
            if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))
            {
                ShowWindow(pState->hCanvasWnd, SW_HIDE);
            }
        }
        else
        {
            // DistinctCards: Left Rake Navigator + Right Cards Canvas
            int navW = 295;
            if (tableW < 750) navW = tableW / 3;

            if (pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))
            {
                SetWindowPos(pState->hSoundMatrixListWnd, NULL,
                    tableX, tableY, navW, matrixH,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
                ConfigureSoundMatrixColumns(pState, navW);
                pState->soundMatrixList.SetRowHeight(28);
                pState->soundMatrixList.SetShowSelectionGutter(false);
                pState->soundMatrixList.SetShowVerticalDividers(false);
                pState->soundMatrixList.SetRowBackgroundCallback(nullptr, nullptr);
                pState->soundMatrixList.SetCellInteractiveCallback(nullptr, nullptr);
            }

            int canvasX = tableX + navW + 6;
            int canvasW = tableW - navW - 6;
            pState->rcCanvas = { canvasX, tableY, canvasX + canvasW, tableY + matrixH };

            if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))
            {
                SetWindowPos(pState->hCanvasWnd, NULL,
                    canvasX, tableY, canvasW, matrixH,
                    SWP_NOZORDER | SWP_SHOWWINDOW);
                ShowWindow(pState->hCanvasWnd, SW_SHOW);
                pState->canvasScrollBar.SetBounds({ canvasW - 14, 0, canvasW, matrixH });
                pState->canvasScrollBar.SetRange(0, pState->canvasTotalHeight, matrixH);
                InvalidateRect(pState->hCanvasWnd, NULL, FALSE);
            }
        }

        if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))
        {
            ShowWindow(pState->hSmsEditorWnd, SW_HIDE);
        }
    }
    else // activeSubTab == 1 (SMS Editor)
    {
        if (pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))
        {
            ShowWindow(pState->hSoundMatrixListWnd, SW_HIDE);
        }
        if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))
        {
            ShowWindow(pState->hCanvasWnd, SW_HIDE);
        }

        int fullTableH = totalH - tableY - marginY;
        if (fullTableH < 100) fullTableH = 100;

        if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))
        {
            SetWindowPos(pState->hSmsEditorWnd, NULL,
                tableX, tableY, tableW, fullTableH,
                SWP_NOZORDER | SWP_SHOWWINDOW);
        }
    }

    InvalidateRect(pState->hWnd, NULL, TRUE);
}

// Painting the Distinct Cards Canvas

static int GetSlotTileHeight(bool isBlockDefined, size_t streamCount)

{

    if (!isBlockDefined) return 68 + 8;

    if (streamCount == 0) return 66 + 8;

    return 28 + (int)(streamCount * 32) + 40 + 8;

}



static int GetDistinctCardHeight(const RSSDistinctVehicle& dv)

{

    int cardHeaderH = 46;

    if (!dv.isCardExpanded) return cardHeaderH;



    int h = cardHeaderH + 12; // top pad inside card

    // Section 1: Wagon Block Sounds

    h += 36; // Section 1 header (36px)

    if (dv.isWagonSectionExpanded)

    {

        h += GetSlotTileHeight(true, dv.wagonSounds.size()); // Exterior slot

        h += 24; // Interior category divider banner

        h += GetSlotTileHeight(dv.hasInsideBlock, dv.insideSounds.size()); // Inside slot

        if (dv.isEngine)

        {

            h += GetSlotTileHeight(dv.hasORTS3DCabBlock, dv.orts3dCabSounds.size()); // ORTS 3D cab slot

        }

    }



    // Section 2: Engine Block Sounds (if engine)

    if (dv.isEngine)

    {

        h += 12; // gap

        h += 36; // Section 2 header (36px)

        if (dv.isEngineSectionExpanded)

        {

            h += GetSlotTileHeight(true, dv.engineSounds.size()); // Cab slot

        }

    }



    h += 14; // bottom pad inside card

    return h;

}



static void DrawDistinctCardsCanvas(RollingStockStudioState* pState, HDC hdc, int canvasW, int canvasH)

{

    if (!pState) return;



    pState->canvasHitRegions.clear();



    HDC memDC = CreateCompatibleDC(hdc);

    HBITMAP hbm = CreateCompatibleBitmap(hdc, canvasW, canvasH);

    HGDIOBJ oldBm = SelectObject(memDC, hbm);



    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 20, 26));

    RECT rcAll = { 0, 0, canvasW, canvasH };

    FillRect(memDC, &rcAll, bgBrush);

    DeleteObject(bgBrush);



    int cardMarginX = 14;

    int cardW = canvasW - (cardMarginX * 2) - 14; // Leave room for scrollbar

    if (cardW < 300) cardW = 300;



    int curY = 12 - pState->canvasScrollY;

    SetBkMode(memDC, TRANSPARENT);



    for (size_t di = 0; di < pState->distinctVehicles.size(); ++di)

    {

        auto& dv = pState->distinctVehicles[di];

        int cardStartY = curY;



        int totalCardH = GetDistinctCardHeight(dv);

        dv.rcCard = { cardMarginX, cardStartY, cardMarginX + cardW, cardStartY + totalCardH };



        // 1. Outer Card Container

        HBRUSH hbrCard = CreateSolidBrush(RGB(22, 26, 34));

        HPEN hpCardBdr = CreatePen(PS_SOLID, 1, (di == pState->selectedDistinctIndex) ? RGB(0, 145, 255) : RGB(46, 54, 68));

        HGDIOBJ ob1 = SelectObject(memDC, hbrCard);

        HGDIOBJ op1 = SelectObject(memDC, hpCardBdr);

        RoundRect(memDC, dv.rcCard.left, dv.rcCard.top, dv.rcCard.right, dv.rcCard.bottom, 6, 6);

        SelectObject(memDC, ob1);

        SelectObject(memDC, op1);

        DeleteObject(hbrCard);

        DeleteObject(hpCardBdr);



        // 2. Distinct Vehicle Card Header (46px tall)

        int cardHeaderH = 46;

        dv.rcHeader = { dv.rcCard.left, dv.rcCard.top, dv.rcCard.right, dv.rcCard.top + cardHeaderH };

        COLORREF hdrBg = dv.isEngine ? RGB(16, 32, 54) : RGB(12, 34, 28);

        HBRUSH hbrHdr = CreateSolidBrush(hdrBg);

        HGDIOBJ obH = SelectObject(memDC, hbrHdr);

        RoundRect(memDC, dv.rcHeader.left, dv.rcHeader.top, dv.rcHeader.right, dv.rcHeader.bottom + (dv.isCardExpanded ? 6 : 0), 6, 6);

        SelectObject(memDC, obH);

        DeleteObject(hbrHdr);



        // Accent strip on left of header (4px solid bright indicator)

        COLORREF accentColor = dv.isEngine ? RGB(0, 160, 255) : RGB(0, 215, 140);

        RECT rcAccent = { dv.rcHeader.left + 1, dv.rcHeader.top + 3, dv.rcHeader.left + 5, dv.rcHeader.bottom - 3 };

        HBRUSH hbrAcc = CreateSolidBrush(accentColor);

        FillRect(memDC, &rcAccent, hbrAcc);

        DeleteObject(hbrAcc);



        // Chevron

        dv.rcChevron = { dv.rcHeader.left + 10, dv.rcHeader.top, dv.rcHeader.left + 28, dv.rcHeader.bottom };

        SelectObject(memDC, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);

        SetTextColor(memDC, RGB(160, 205, 255));

        DrawTextW(memDC, dv.isCardExpanded ? L"▼" : L"▶", -1, &dv.rcChevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



        // Vehicle Icon & Name

        int textX = dv.rcHeader.left + 32;

        SelectObject(memDC, pState->hFontTitle ? pState->hFontTitle : pState->hFontBold);

        SetTextColor(memDC, RGB(255, 255, 255));

        std::wstring icon = dv.isEngine ? L"🚂 " : L"📦 ";

        std::wstring nameStr = icon + dv.name;

        SIZE szName;

        GetTextExtentPoint32W(memDC, nameStr.c_str(), (int)nameStr.length(), &szName);

        RECT rcName = { textX, dv.rcHeader.top, textX + (int)szName.cx + 8, dv.rcHeader.bottom };

        DrawTextW(memDC, nameStr.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        textX += (int)szName.cx + 12;



        // Type Pill

        COLORREF pillBg = dv.isEngine ? RGB(18, 48, 88) : RGB(16, 56, 50);

        COLORREF pillBdr = dv.isEngine ? RGB(32, 90, 160) : RGB(28, 120, 100);

        COLORREF pillFg = dv.isEngine ? RGB(100, 185, 255) : RGB(70, 225, 180);

        RECT rcTypePill = { textX, dv.rcHeader.top + 11, textX + 68, dv.rcHeader.bottom - 11 };

        HBRUSH hbrTP = CreateSolidBrush(pillBg);

        HPEN hpTP = CreatePen(PS_SOLID, 1, pillBdr);

        HGDIOBJ oB = SelectObject(memDC, hbrTP);

        HGDIOBJ oP = SelectObject(memDC, hpTP);

        RoundRect(memDC, rcTypePill.left, rcTypePill.top, rcTypePill.right, rcTypePill.bottom, 4, 4);

        SelectObject(memDC, oB);

        SelectObject(memDC, oP);

        DeleteObject(hbrTP);

        DeleteObject(hpTP);



        SelectObject(memDC, pState->hFontBold);

        SetTextColor(memDC, pillFg);

        std::wstring typeLabel = dv.isEngine ? L"Engine" : L"Wagon";

        DrawTextW(memDC, typeLabel.c_str(), -1, &rcTypePill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        textX += 76;



        // Applied Count Badge

        if (!dv.unitIndices.empty())

        {

            std::wstring appStr;

            if (dv.unitIndices.size() == 1)

                appStr = L"Applied to 1 Car in Rake (#" + std::to_wstring(dv.unitIndices[0]) + L")";

            else

                appStr = L"Applied to " + std::to_wstring(dv.unitIndices.size()) + L" Cars in Rake (#" +

                         std::to_wstring(dv.unitIndices.front()) + L"–#" + std::to_wstring(dv.unitIndices.back()) + L")";



            SIZE szApp;

            GetTextExtentPoint32W(memDC, appStr.c_str(), (int)appStr.length(), &szApp);

            RECT rcApp = { textX, dv.rcHeader.top + 11, textX + (int)szApp.cx + 18, dv.rcHeader.bottom - 11 };

            HBRUSH hbrApp = CreateSolidBrush(RGB(20, 25, 35));

            HPEN hpApp = CreatePen(PS_SOLID, 1, RGB(42, 52, 70));

            oB = SelectObject(memDC, hbrApp);

            oP = SelectObject(memDC, hpApp);

            RoundRect(memDC, rcApp.left, rcApp.top, rcApp.right, rcApp.bottom, 4, 4);

            SelectObject(memDC, oB);

            SelectObject(memDC, oP);

            DeleteObject(hbrApp);

            DeleteObject(hpApp);



            SelectObject(memDC, pState->hFontBody);

            SetTextColor(memDC, RGB(165, 185, 215));

            DrawTextW(memDC, appStr.c_str(), -1, &rcApp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            textX += (int)szApp.cx + 26;



            // Missing Sound Badge in Card Header

            bool dvHasMissing = false;

            for (const auto& s : dv.wagonSounds) if (!s.existsOnDisk) dvHasMissing = true;

            for (const auto& s : dv.engineSounds) if (!s.existsOnDisk) dvHasMissing = true;

            for (const auto& s : dv.orts3dCabSounds) if (!s.existsOnDisk) dvHasMissing = true;

            for (const auto& s : dv.insideSounds) if (!s.existsOnDisk) dvHasMissing = true;

            if (dv.wagonSounds.empty()) dvHasMissing = true;



            if (dvHasMissing)

            {

                std::wstring missHdrText = L"⚠ Sound Missing on Disk";

                SIZE szM;

                GetTextExtentPoint32W(memDC, missHdrText.c_str(), (int)missHdrText.length(), &szM);

                RECT rcMissHdr = { textX, dv.rcHeader.top + 11, textX + (int)szM.cx + 18, dv.rcHeader.bottom - 11 };

                HBRUSH hbrMh = CreateSolidBrush(RGB(58, 20, 24));

                HPEN hpMh = CreatePen(PS_SOLID, 1, RGB(150, 48, 55));

                oB = SelectObject(memDC, hbrMh);

                oP = SelectObject(memDC, hpMh);

                RoundRect(memDC, rcMissHdr.left, rcMissHdr.top, rcMissHdr.right, rcMissHdr.bottom, 4, 4);

                SelectObject(memDC, oB);

                SelectObject(memDC, oP);

                DeleteObject(hbrMh);

                DeleteObject(hpMh);



                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(255, 95, 95));

                DrawTextW(memDC, missHdrText.c_str(), -1, &rcMissHdr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

        }



        // Header Action Buttons on the right

        int btnRight = dv.rcHeader.right - 12;



        // [ 💾 Save Sound ]

        dv.rcBtnSave = { btnRight - 110, dv.rcHeader.top + 10, btnRight, dv.rcHeader.bottom - 10 };

        btnRight -= 118;

        COLORREF saveBg = dv.isModified ? RGB(24, 88, 48) : RGB(28, 32, 40);

        COLORREF saveBdr = dv.isModified ? RGB(45, 160, 85) : RGB(46, 52, 64);

        COLORREF saveFg = dv.isModified ? RGB(120, 255, 160) : RGB(140, 150, 165);

        HBRUSH hbrSv = CreateSolidBrush(saveBg);

        HPEN hpSv = CreatePen(PS_SOLID, 1, saveBdr);

        oB = SelectObject(memDC, hbrSv);

        oP = SelectObject(memDC, hpSv);

        RoundRect(memDC, dv.rcBtnSave.left, dv.rcBtnSave.top, dv.rcBtnSave.right, dv.rcBtnSave.bottom, 4, 4);

        SelectObject(memDC, oB);

        SelectObject(memDC, oP);

        DeleteObject(hbrSv);

        DeleteObject(hpSv);



        SelectObject(memDC, pState->hFontBold);

        SetTextColor(memDC, saveFg);

        DrawTextW(memDC, dv.isModified ? L"💾 Save Sound ●" : L"💾 Save Sound", -1, &dv.rcBtnSave, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



        // [ 📄 Open Stock File ]

        dv.rcBtnOpenStock = { btnRight - 130, dv.rcHeader.top + 10, btnRight, dv.rcHeader.bottom - 10 };

        btnRight -= 138;

        HBRUSH hbrOp = CreateSolidBrush(RGB(28, 33, 44));

        HPEN hpOp = CreatePen(PS_SOLID, 1, RGB(48, 56, 72));

        oB = SelectObject(memDC, hbrOp);

        oP = SelectObject(memDC, hpOp);

        RoundRect(memDC, dv.rcBtnOpenStock.left, dv.rcBtnOpenStock.top, dv.rcBtnOpenStock.right, dv.rcBtnOpenStock.bottom, 4, 4);

        SelectObject(memDC, oB);

        SelectObject(memDC, oP);

        DeleteObject(hbrOp);

        DeleteObject(hpOp);



        SetTextColor(memDC, RGB(215, 225, 240));

        DrawTextW(memDC, L"📄 Open Stock File", -1, &dv.rcBtnOpenStock, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



        // Multi-stream aware slot tile renderer lambda

        auto drawSlotTile = [&](const std::wstring& slotTitle, const std::wstring& slotSubtitle,

            const std::vector<RSSSoundEntry>& streams,

            bool isBlockDefined, const std::wstring& missingTitle, const std::wstring& missingDesc,

            RSSSlotHitInfo::SlotType slotType,

            int innerY) -> int

        {

            int slotW = cardW - 24;

            int slotX = dv.rcCard.left + 12;

            int tileH = !isBlockDefined ? 68 : (streams.empty() ? 66 : (28 + (int)streams.size() * 32 + 40));

            RECT rcTile = { slotX, innerY, slotX + slotW, innerY + tileH };



            bool isSlotHovered = (pState->dragHoverDistinctIndex == (int)di && pState->dragHoverSlotType == slotType);



            COLORREF tileBg = isSlotHovered ? (isBlockDefined ? RGB(16, 48, 76) : RGB(48, 20, 24)) : (isBlockDefined ? RGB(17, 20, 27) : RGB(24, 22, 18));

            COLORREF tileBdr = isSlotHovered ? (isBlockDefined ? RGB(0, 215, 255) : RGB(255, 75, 75)) : (isBlockDefined ? RGB(36, 42, 54) : RGB(100, 70, 24));



            HBRUSH hbrT = CreateSolidBrush(tileBg);

            HPEN hpT = CreatePen(isBlockDefined ? PS_SOLID : PS_DASH, isSlotHovered ? 2 : 1, tileBdr);

            HGDIOBJ obTile = SelectObject(memDC, hbrT);

            HGDIOBJ opTile = SelectObject(memDC, hpT);

            RoundRect(memDC, rcTile.left, rcTile.top, rcTile.right, rcTile.bottom, 4, 4);

            SelectObject(memDC, obTile);

            SelectObject(memDC, opTile);

            DeleteObject(hbrT);

            DeleteObject(hpT);



            // Subtitle header line with slot name & subtitle

            SelectObject(memDC, pState->hFontBold);

            SetTextColor(memDC, isBlockDefined ? RGB(150, 195, 255) : RGB(225, 175, 80));

            std::wstring fullHdr = slotTitle;

            if (!slotSubtitle.empty()) fullHdr += L" " + slotSubtitle;

            RECT rcSub = { slotX + 10, innerY + 6, slotX + slotW - 190, innerY + 22 };

            DrawTextW(memDC, fullHdr.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



            // Status Badge on the right side of the slot title line

            RECT rcSlotBadge = { slotX + slotW - 180, innerY + 5, slotX + slotW - 10, innerY + 23 };



            bool anyMissingOnDisk = false;

            for (const auto& s : streams) { if (!s.existsOnDisk) anyMissingOnDisk = true; }



            if (!isBlockDefined)

            {

                // Amber badge: [ ⚠ Block Missing in Stock ]

                HBRUSH hbrMb = CreateSolidBrush(RGB(44, 30, 16));

                HPEN hpMb = CreatePen(PS_SOLID, 1, RGB(115, 75, 25));

                HGDIOBJ obMb = SelectObject(memDC, hbrMb);

                HGDIOBJ opMb = SelectObject(memDC, hpMb);

                RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 4, 4);

                SelectObject(memDC, obMb);

                SelectObject(memDC, opMb);

                DeleteObject(hbrMb);

                DeleteObject(hpMb);



                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(235, 170, 60));

                DrawTextW(memDC, L"⚠ Block Missing in Stock", -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else if (anyMissingOnDisk)

            {

                // Crimson Alert Badge: [ ⚠ File Missing on Disk ]

                HBRUSH hbrMb = CreateSolidBrush(RGB(58, 20, 24));

                HPEN hpMb = CreatePen(PS_SOLID, 1, RGB(150, 48, 55));

                HGDIOBJ obMb = SelectObject(memDC, hbrMb);

                HGDIOBJ opMb = SelectObject(memDC, hpMb);

                RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 4, 4);

                SelectObject(memDC, obMb);

                SelectObject(memDC, opMb);

                DeleteObject(hbrMb);

                DeleteObject(hpMb);



                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(255, 95, 95));

                DrawTextW(memDC, L"⚠ File Missing on Disk", -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else if (!streams.empty())

            {

                // Emerald Badge: [ ✓ Sound Ready ] (with stream count if > 1)

                HBRUSH hbrMb = CreateSolidBrush(RGB(16, 44, 28));

                HPEN hpMb = CreatePen(PS_SOLID, 1, RGB(30, 95, 55));

                HGDIOBJ obMb = SelectObject(memDC, hbrMb);

                HGDIOBJ opMb = SelectObject(memDC, hpMb);

                RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 4, 4);

                SelectObject(memDC, obMb);

                SelectObject(memDC, opMb);

                DeleteObject(hbrMb);

                DeleteObject(hpMb);



                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(80, 220, 140));

                std::wstring rdyText = (streams.size() > 1) ?

                    (L"✓ " + std::to_wstring(streams.size()) + L" Streams Ready") : L"✓ Sound Ready";

                DrawTextW(memDC, rdyText.c_str(), -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else // No sound assigned yet

            {

                if (slotType == RSSSlotHitInfo::WagonExterior)

                {

                    // Amber badge: [ ⚠ No Sound Assigned ]

                    HBRUSH hbrMb = CreateSolidBrush(RGB(44, 30, 16));

                    HPEN hpMb = CreatePen(PS_SOLID, 1, RGB(115, 75, 25));

                    HGDIOBJ obMb = SelectObject(memDC, hbrMb);

                    HGDIOBJ opMb = SelectObject(memDC, hpMb);

                    RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 4, 4);

                    SelectObject(memDC, obMb);

                    SelectObject(memDC, opMb);

                    DeleteObject(hbrMb);

                    DeleteObject(hpMb);



                    SelectObject(memDC, pState->hFontBold);

                    SetTextColor(memDC, RGB(235, 170, 60));

                    DrawTextW(memDC, L"⚠ No Sound Assigned", -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

                else

                {

                    // Slate badge: [ ＋ Sound Optional ]

                    HBRUSH hbrMb = CreateSolidBrush(RGB(22, 28, 38));

                    HPEN hpMb = CreatePen(PS_SOLID, 1, RGB(42, 54, 72));

                    HGDIOBJ obMb = SelectObject(memDC, hbrMb);

                    HGDIOBJ opMb = SelectObject(memDC, hpMb);

                    RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 4, 4);

                    SelectObject(memDC, obMb);

                    SelectObject(memDC, opMb);

                    DeleteObject(hbrMb);

                    DeleteObject(hpMb);



                    SelectObject(memDC, pState->hFontBody);

                    SetTextColor(memDC, RGB(140, 170, 205));

                    DrawTextW(memDC, L"＋ Sound Optional", -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

            }



            if (!isBlockDefined)

            {

                // Prominent Notice Box inside slot

                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(245, 185, 90));

                RECT rcMsg1 = { slotX + 12, innerY + 26, slotX + slotW - 12, innerY + 44 };

                DrawTextW(memDC, missingTitle.c_str(), -1, &rcMsg1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                SelectObject(memDC, pState->hFontBody);

                SetTextColor(memDC, RGB(150, 160, 175));

                RECT rcMsg2 = { slotX + 12, innerY + 44, slotX + slotW - 12, innerY + 62 };

                DrawTextW(memDC, missingDesc.c_str(), -1, &rcMsg2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else

            {

                if (streams.empty())

                {

                    // Empty slot: Sleek dashed active drop box

                    RECT rcEmptyBtn = { slotX + 10, innerY + 28, slotX + slotW - 10, innerY + 56 };

                    HBRUSH hbrEmp = CreateSolidBrush(RGB(22, 27, 36));

                    HPEN hpEmp = CreatePen(PS_DASH, 1, RGB(55, 75, 105));

                    HGDIOBJ obE = SelectObject(memDC, hbrEmp);

                    HGDIOBJ opE = SelectObject(memDC, hpEmp);

                    RoundRect(memDC, rcEmptyBtn.left, rcEmptyBtn.top, rcEmptyBtn.right, rcEmptyBtn.bottom, 4, 4);

                    SelectObject(memDC, obE);

                    SelectObject(memDC, opE);

                    DeleteObject(hbrEmp);

                    DeleteObject(hpEmp);



                    SelectObject(memDC, pState->hFontBody);

                    SetTextColor(memDC, RGB(140, 180, 230));

                    std::wstring empText = (slotType == RSSSlotHitInfo::WagonInside) ? L"+ Drop .sms here or Click to Assign Inside View Sound File" :

                                           (slotType == RSSSlotHitInfo::WagonOrts3D) ? L"+ Drop .sms here or Click to Assign 3D Cab Sound File" :

                                           L"+ Drop .sms here or Click to Assign Sound File";

                    DrawTextW(memDC, empText.c_str(), -1, &rcEmptyBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    pState->canvasHitRegions.push_back({ slotType, di, -1, true, false, false, true, rcEmptyBtn });

                }

                else

                {

                    // Render each assigned sound stream

                    for (size_t sIdx = 0; sIdx < streams.size(); ++sIdx)

                    {

                        const auto& s = streams[sIdx];

                        int itemY = innerY + 28 + (int)sIdx * 32;



                        int chipStartX = slotX + 10;

                        if (streams.size() > 1)

                        {

                            // Multi-stream indicator tag (e.g. [S1], [S2])

                            RECT rcStTag = { chipStartX, itemY + 2, chipStartX + 26, itemY + 24 };

                            HBRUSH hbrSt = CreateSolidBrush((sIdx == 0) ? RGB(20, 42, 60) : RGB(32, 28, 48));

                            HPEN hpSt = CreatePen(PS_SOLID, 1, (sIdx == 0) ? RGB(45, 95, 135) : RGB(80, 65, 115));

                            HGDIOBJ obSt = SelectObject(memDC, hbrSt);

                            HGDIOBJ opSt = SelectObject(memDC, hpSt);

                            RoundRect(memDC, rcStTag.left, rcStTag.top, rcStTag.right, rcStTag.bottom, 3, 3);

                            SelectObject(memDC, obSt);

                            SelectObject(memDC, opSt);

                            DeleteObject(hbrSt);

                            DeleteObject(hpSt);



                            SelectObject(memDC, pState->hFontBold);

                            SetTextColor(memDC, (sIdx == 0) ? RGB(100, 200, 255) : RGB(190, 165, 255));

                            std::wstring stTxt = L"S" + std::to_wstring(sIdx + 1);

                            DrawTextW(memDC, stTxt.c_str(), -1, &rcStTag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            chipStartX += 32;

                        }



                        // Sound chip

                        int chipW = 240;

                        RECT rcChip = { chipStartX, itemY, chipStartX + chipW, itemY + 26 };



                        COLORREF cBg = s.existsOnDisk ? RGB(26, 36, 48) : RGB(48, 24, 26);

                        COLORREF cBdr = s.existsOnDisk ? RGB(45, 75, 110) : RGB(140, 48, 55);

                        COLORREF cFg = s.existsOnDisk ? RGB(225, 235, 250) : RGB(255, 130, 130);



                        HBRUSH hbrC = CreateSolidBrush(cBg);

                        HPEN hpC = CreatePen(PS_SOLID, 1, cBdr);

                        HGDIOBJ obC = SelectObject(memDC, hbrC);

                        HGDIOBJ opC = SelectObject(memDC, hpC);

                        RoundRect(memDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);

                        SelectObject(memDC, obC);

                        SelectObject(memDC, opC);

                        DeleteObject(hbrC);

                        DeleteObject(hpC);



                        // Audio icon

                        SelectObject(memDC, pState->hFontSmallIcons ? pState->hFontSmallIcons : pState->hFontIcons);

                        SetTextColor(memDC, s.existsOnDisk ? RGB(0, 180, 255) : RGB(255, 95, 95));

                        RECT rcIco = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };

                        DrawTextW(memDC, L"\xE767", -1, &rcIco, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                        // Name

                        SelectObject(memDC, pState->hFontBold);

                        SetTextColor(memDC, cFg);

                        int txtRight = s.existsOnDisk ? (rcChip.right - 6) : (rcChip.right - 72);

                        RECT rcTxt = { rcChip.left + 26, rcChip.top, txtRight, rcChip.bottom };

                        DrawTextW(memDC, s.smsFileName.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                        // Missing Pill Badge inside Chip if !existsOnDisk

                        if (!s.existsOnDisk)

                        {

                            RECT rcMissTag = { rcChip.right - 68, rcChip.top + 3, rcChip.right - 4, rcChip.bottom - 3 };

                            HBRUSH hbrMt = CreateSolidBrush(RGB(72, 22, 26));

                            HPEN hpMt = CreatePen(PS_SOLID, 1, RGB(160, 48, 55));

                            HGDIOBJ obMt = SelectObject(memDC, hbrMt);

                            HGDIOBJ opMt = SelectObject(memDC, hpMt);

                            RoundRect(memDC, rcMissTag.left, rcMissTag.top, rcMissTag.right, rcMissTag.bottom, 3, 3);

                            SelectObject(memDC, obMt);

                            SelectObject(memDC, opMt);

                            DeleteObject(hbrMt);

                            DeleteObject(hpMt);



                            SelectObject(memDC, pState->hFontBold);

                            SetTextColor(memDC, RGB(255, 110, 110));

                            DrawTextW(memDC, L"MISSING", -1, &rcMissTag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        }



                        // Register chip hit region

                        pState->canvasHitRegions.push_back({ slotType, di, (int)sIdx, false, false, false, false, rcChip });



                        // Right action buttons (Replace & Delete)

                        int rightOffset = 10;

                        if (sIdx >= 0)

                        {

                            // [ ✕ ] Delete Button for secondary streams

                            int delBtnW = 24;

                            RECT rcDel = { slotX + slotW - rightOffset - delBtnW, itemY, slotX + slotW - rightOffset, itemY + 26 };

                            rightOffset += delBtnW + 8;



                            bool isDelHov = PtInRect(&rcDel, pState->canvasMousePos);
                            COLORREF delBg = isDelHov ? RGB(75, 24, 30) : RGB(48, 20, 24);
                            COLORREF delBdr = isDelHov ? RGB(220, 60, 70) : RGB(120, 40, 48);
                            HBRUSH hbrD = CreateSolidBrush(delBg);

                            HPEN hpD = CreatePen(PS_SOLID, 1, delBdr);

                            HGDIOBJ obD = SelectObject(memDC, hbrD);

                            HGDIOBJ opD = SelectObject(memDC, hpD);

                            RoundRect(memDC, rcDel.left, rcDel.top, rcDel.right, rcDel.bottom, 4, 4);

                            SelectObject(memDC, obD);

                            SelectObject(memDC, opD);

                            DeleteObject(hbrD);

                            DeleteObject(hpD);



                            SelectObject(memDC, pState->hFontBold);

                            SetTextColor(memDC, isDelHov ? RGB(255, 175, 175) : RGB(255, 120, 120));

                            DrawTextW(memDC, L"✕", -1, &rcDel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            pState->canvasHitRegions.push_back({ slotType, di, (int)sIdx, false, false, true, false, rcDel });

                        }



                        // [ + Replace ] Button

                        int repBtnW = (sIdx == 0) ? 175 : 120;

                        RECT rcRep = { slotX + slotW - rightOffset - repBtnW, itemY, slotX + slotW - rightOffset, itemY + 26 };

                        rightOffset += repBtnW + 12;



                        HBRUSH hbrAct = CreateSolidBrush(RGB(28, 34, 46));

                        HPEN hpAct = CreatePen(PS_SOLID, 1, RGB(46, 56, 76));

                        HGDIOBJ obA = SelectObject(memDC, hbrAct);

                        HGDIOBJ opA = SelectObject(memDC, hpAct);

                        RoundRect(memDC, rcRep.left, rcRep.top, rcRep.right, rcRep.bottom, 4, 4);

                        SelectObject(memDC, obA);

                        SelectObject(memDC, opA);

                        DeleteObject(hbrAct);

                        DeleteObject(hpAct);



                        SelectObject(memDC, pState->hFontBody);

                        SetTextColor(memDC, RGB(190, 215, 245));

                        std::wstring repLabel = (sIdx == 0) ? L"+ Drop or Browse to Replace" : L"+ Replace";

                        DrawTextW(memDC, repLabel.c_str(), -1, &rcRep, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        pState->canvasHitRegions.push_back({ slotType, di, (int)sIdx, true, false, false, false, rcRep });



                        // Folder path text next to chip

                        SelectObject(memDC, pState->hFontBody);

                        SetTextColor(memDC, s.existsOnDisk ? RGB(130, 145, 165) : RGB(240, 120, 120));

                        std::wstring pathStr = s.parentFolder.empty() ? s.smsFileName : s.parentFolder;

                        if (!s.existsOnDisk) pathStr = L"⚠ File not found on disk: " + pathStr;

                        RECT rcPath = { rcChip.right + 12, itemY, slotX + slotW - rightOffset, itemY + 26 };

                        DrawTextW(memDC, pathStr.c_str(), -1, &rcPath, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    }



                    // Solid Centered [ ＋ Add Additional SMS ] button with SMS Editor Studio styling

                    int addBtnW = 190;

                    int addBtnH = 26;

                    int addBtnX = slotX + (slotW - addBtnW) / 2;

                    int addBtnY = innerY + 28 + (int)streams.size() * 32 + 4;

                    RECT rcAddBtn = { addBtnX, addBtnY, addBtnX + addBtnW, addBtnY + addBtnH };



                    bool isHoverAdd = (PtInRect(&rcAddBtn, pState->canvasMousePos) ||

                                      (pState->dragHoverDistinctIndex == (int)di && pState->dragHoverSlotType == slotType && pState->dragHoverIsAddStream));



                    COLORREF bgAdd = isHoverAdd ? RGB(26, 75, 120) : RGB(20, 50, 80);

                    COLORREF bdrAdd = isHoverAdd ? RGB(70, 175, 255) : RGB(45, 115, 185);

                    COLORREF fgAdd = isHoverAdd ? RGB(255, 255, 255) : RGB(210, 235, 255);



                    HBRUSH hbrAb = CreateSolidBrush(bgAdd);

                    HPEN hpAb = CreatePen(PS_SOLID, 1, bdrAdd);

                    HGDIOBJ obAb = SelectObject(memDC, hbrAb);

                    HGDIOBJ opAb = SelectObject(memDC, hpAb);

                    RoundRect(memDC, rcAddBtn.left, rcAddBtn.top, rcAddBtn.right, rcAddBtn.bottom, 4, 4);

                    SelectObject(memDC, obAb);

                    SelectObject(memDC, opAb);

                    DeleteObject(hbrAb);

                    DeleteObject(hpAb);



                    SelectObject(memDC, pState->hFontBold);

                    SetTextColor(memDC, fgAdd);

                    DrawTextW(memDC, L"＋ Add Additional SMS", -1, &rcAddBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    pState->canvasHitRegions.push_back({ slotType, di, -1, false, false, false, true, rcAddBtn });

                }

            }



            return tileH + 8;

        };



        if (dv.isCardExpanded)

        {

            int innerY = dv.rcHeader.bottom + 10;



            // ================= SECTION 1: WAGON BLOCK SOUNDS =================

            int secW = cardW - 16;

            int secX = dv.rcCard.left + 8;

            int sec1HdrH = 36;

            dv.rcWagonSectionHdr = { secX, innerY, secX + secW, innerY + sec1HdrH };



            HBRUSH hbrWagHdr = CreateSolidBrush(RGB(18, 34, 48));

            HPEN hpWagHdr = CreatePen(PS_SOLID, 1, RGB(36, 75, 110));

            HGDIOBJ obW = SelectObject(memDC, hbrWagHdr);

            HGDIOBJ opW = SelectObject(memDC, hpWagHdr);

            RoundRect(memDC, dv.rcWagonSectionHdr.left, dv.rcWagonSectionHdr.top, dv.rcWagonSectionHdr.right, dv.rcWagonSectionHdr.bottom, 4, 4);

            SelectObject(memDC, obW);

            SelectObject(memDC, opW);

            DeleteObject(hbrWagHdr);

            DeleteObject(hpWagHdr);



            dv.rcWagonSectionChevron = { dv.rcWagonSectionHdr.left + 8, dv.rcWagonSectionHdr.top, dv.rcWagonSectionHdr.left + 24, dv.rcWagonSectionHdr.bottom };

            SelectObject(memDC, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);

            SetTextColor(memDC, RGB(140, 215, 255));

            DrawTextW(memDC, dv.isWagonSectionExpanded ? L"▼" : L"▶", -1, &dv.rcWagonSectionChevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



            SelectObject(memDC, pState->hFontHeader ? pState->hFontHeader : pState->hFontBold);

            SetTextColor(memDC, RGB(160, 225, 255));

            std::wstring wagHdrTitle = dv.isEngine ?

                L"📦  WAGON BLOCK SOUNDS  (Exterior, Inside View & ORTS 3D Cab)" :

                L"📦  WAGON BLOCK SOUNDS  (Exterior & Inside Passenger View)";

            RECT rcWagTitle = { dv.rcWagonSectionHdr.left + 28, dv.rcWagonSectionHdr.top, dv.rcWagonSectionHdr.right - 130, dv.rcWagonSectionHdr.bottom };

            DrawTextW(memDC, wagHdrTitle.c_str(), -1, &rcWagTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



            // Right Tag Badge: [ WAGON BLOCK ]

            RECT rcWagBadge = { dv.rcWagonSectionHdr.right - 120, dv.rcWagonSectionHdr.top + 7, dv.rcWagonSectionHdr.right - 10, dv.rcWagonSectionHdr.bottom - 7 };

            HBRUSH hbrWb = CreateSolidBrush(RGB(16, 45, 68));

            HPEN hpWb = CreatePen(PS_SOLID, 1, RGB(30, 90, 130));

            oB = SelectObject(memDC, hbrWb);

            oP = SelectObject(memDC, hpWb);

            RoundRect(memDC, rcWagBadge.left, rcWagBadge.top, rcWagBadge.right, rcWagBadge.bottom, 3, 3);

            SelectObject(memDC, oB);

            SelectObject(memDC, oP);

            DeleteObject(hbrWb);

            DeleteObject(hpWb);



            SelectObject(memDC, pState->hFontBold);

            SetTextColor(memDC, RGB(80, 200, 255));

            DrawTextW(memDC, L"WAGON BLOCK", -1, &rcWagBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



            innerY += sec1HdrH + 6;



            if (dv.isWagonSectionExpanded)

            {

                // Slot 1: Wagon Exterior Sound

                innerY += drawSlotTile(

                    L"🔊 Wagon Exterior Sound",

                    L"(Sound parameter directly in Wagon block — Trackside views #2, #3, #4)",

                    dv.wagonSounds,

                    true,

                    L"",

                    L"",

                    RSSSlotHitInfo::WagonExterior,

                    innerY

                );



                // Category Sub-divider line

                RECT rcDiv = { secX + 6, innerY + 2, secX + secW - 6, innerY + 20 };

                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(105, 140, 175));

                DrawTextW(memDC, L"─── INTERIOR CAMERA & PASSENGER AUDIO (SUB-BLOCKS IN WAGON) ───", -1, &rcDiv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                innerY += 24;



                // Slot 2: Inside View Sound

                innerY += drawSlotTile(

                    L"💺 Inside View Sound",

                    L"(Inside block passenger / interior view camera #5)",

                    dv.insideSounds,

                    dv.hasInsideBlock,

                    L"⚠ Inside view block not defined in this rolling stock file.",

                    L"Passenger / interior audio requires an Inside ( ... ) camera position block in the stock file first.",

                    RSSSlotHitInfo::WagonInside,

                    innerY

                );



                // Slot 3: ORTS 3D Cab Sound (Locomotives only)

                if (dv.isEngine)

                {

                    innerY += drawSlotTile(

                        L"👓 ORTS 3D Cab Sound",

                        L"(ORTS3DCab block — supports Front & Reverse interactive 3D cabviews)",

                        dv.orts3dCabSounds,

                        dv.hasORTS3DCabBlock,

                        L"⚠ ORTS 3D Cab block not defined in this locomotive file.",

                        L"3D Cab audio requires an ORTS3DCab ( ... ) block with 3D shape & camera coordinates in the .eng file first.",

                        RSSSlotHitInfo::WagonOrts3D,

                        innerY

                    );

                }

            }



            // ================= SECTION 2: ENGINE BLOCK SOUNDS =================

            if (dv.isEngine)

            {

                innerY += 8;

                int sec2HdrH = 36;

                dv.rcEngineSectionHdr = { secX, innerY, secX + secW, innerY + sec2HdrH };



                HBRUSH hbrEngHdr = CreateSolidBrush(RGB(40, 28, 16));

                HPEN hpEngHdr = CreatePen(PS_SOLID, 1, RGB(130, 85, 25));

                HGDIOBJ obE = SelectObject(memDC, hbrEngHdr);

                HGDIOBJ opE = SelectObject(memDC, hpEngHdr);

                RoundRect(memDC, dv.rcEngineSectionHdr.left, dv.rcEngineSectionHdr.top, dv.rcEngineSectionHdr.right, dv.rcEngineSectionHdr.bottom, 4, 4);

                SelectObject(memDC, obE);

                SelectObject(memDC, opE);

                DeleteObject(hbrEngHdr);

                DeleteObject(hpEngHdr);



                dv.rcEngineSectionChevron = { dv.rcEngineSectionHdr.left + 8, dv.rcEngineSectionHdr.top, dv.rcEngineSectionHdr.left + 24, dv.rcEngineSectionHdr.bottom };

                SelectObject(memDC, pState->hFontHeader ? pState->hFontHeader : pState->hFontBody);

                SetTextColor(memDC, RGB(255, 195, 110));

                DrawTextW(memDC, dv.isEngineSectionExpanded ? L"▼" : L"▶", -1, &dv.rcEngineSectionChevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                SelectObject(memDC, pState->hFontHeader ? pState->hFontHeader : pState->hFontBold);

                SetTextColor(memDC, RGB(255, 215, 135));

                RECT rcEngTitle = { dv.rcEngineSectionHdr.left + 28, dv.rcEngineSectionHdr.top, dv.rcEngineSectionHdr.right - 130, dv.rcEngineSectionHdr.bottom };

                DrawTextW(memDC, L"🕹️  ENGINE BLOCK SOUNDS  (Locomotive Cab Interior)", -1, &rcEngTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Right Tag Badge: [ ENGINE BLOCK ]

                RECT rcEngBadge = { dv.rcEngineSectionHdr.right - 120, dv.rcEngineSectionHdr.top + 7, dv.rcEngineSectionHdr.right - 10, dv.rcEngineSectionHdr.bottom - 7 };

                HBRUSH hbrEb = CreateSolidBrush(RGB(52, 34, 16));

                HPEN hpEb = CreatePen(PS_SOLID, 1, RGB(130, 85, 25));

                oB = SelectObject(memDC, hbrEb);

                oP = SelectObject(memDC, hpEb);

                RoundRect(memDC, rcEngBadge.left, rcEngBadge.top, rcEngBadge.right, rcEngBadge.bottom, 3, 3);

                SelectObject(memDC, oB);

                SelectObject(memDC, oP);

                DeleteObject(hbrEb);

                DeleteObject(hpEb);



                SelectObject(memDC, pState->hFontBold);

                SetTextColor(memDC, RGB(255, 185, 70));

                DrawTextW(memDC, L"ENGINE BLOCK", -1, &rcEngBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                innerY += sec2HdrH + 6;



                if (dv.isEngineSectionExpanded)

                {

                    innerY += drawSlotTile(

                        L"🕹️ Locomotive Cab Sound",

                        L"(Sound parameter in Engine block — In-Cab controls and prime mover audio)",

                        dv.engineSounds,

                        true,

                        L"",

                        L"",

                        RSSSlotHitInfo::EngineCab,

                        innerY

                    );

                }

            }

        }



        curY += totalCardH + 16;

    }



    pState->canvasTotalHeight = curY + pState->canvasScrollY + 24;

    pState->canvasScrollBar.SetBounds({ canvasW - 14, 0, canvasW, canvasH });

    pState->canvasScrollBar.SetRange(0, pState->canvasTotalHeight, canvasH);

    pState->canvasScrollBar.Paint(memDC);



    BitBlt(hdc, 0, 0, canvasW, canvasH, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBm);

    DeleteObject(hbm);

    DeleteDC(memDC);

}



// Window procedure for RollingStockCanvas child window

static LRESULT CALLBACK RollingStockCanvasWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);



    switch (uMsg)

    {

    case WM_NCCREATE:

    {

        CREATESTRUCTW* pCS = (CREATESTRUCTW*)lParam;

        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pCS->lpCreateParams);

        return TRUE;

    }



    case WM_PAINT:

    {

        PAINTSTRUCT ps;

        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;

        GetClientRect(hWnd, &rc);

        DrawDistinctCardsCanvas(pState, hdc, rc.right, rc.bottom);

        EndPaint(hWnd, &ps);

        return 0;

    }



    case WM_SIZE:

    {

        if (pState)

        {

            RECT rc;

            GetClientRect(hWnd, &rc);

            pState->canvasScrollBar.SetBounds({ rc.right - 14, 0, rc.right, rc.bottom });

            pState->canvasScrollBar.SetRange(0, pState->canvasTotalHeight, rc.bottom);

        }

        return 0;

    }



    case WM_MOUSEWHEEL:

    {

        if (pState)

        {

            short delta = GET_WHEEL_DELTA_WPARAM(wParam);

            pState->canvasScrollBar.OnMouseWheel(delta, 28, hWnd);

            pState->canvasScrollY = pState->canvasScrollBar.GetPos();

            InvalidateRect(hWnd, NULL, FALSE);

        }

        return 0;

    }



    case WM_LBUTTONDOWN:

    {

        if (!pState) return 0;

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };



        if (pState->canvasScrollBar.OnLButtonDown(pt, hWnd))

        {

            pState->canvasScrollY = pState->canvasScrollBar.GetPos();

            InvalidateRect(hWnd, NULL, FALSE);

            return 0;

        }



        // 1. Check canvasHitRegions first (for chips, replace buttons, delete buttons, add stream buttons)

        for (const auto& hit : pState->canvasHitRegions)

        {

            if (PtInRect(&hit.rc, pt))

            {

                if (hit.distinctIndex < pState->distinctVehicles.size())

                {

                    auto& dv = pState->distinctVehicles[hit.distinctIndex];

                    std::wstring catStr = (hit.slotType == RSSSlotHitInfo::WagonExterior) ? L"Wagon" :

                                          (hit.slotType == RSSSlotHitInfo::WagonInside) ? L"Inside" :

                                          (hit.slotType == RSSSlotHitInfo::WagonOrts3D) ? L"ORTS3DCab" : L"Engine";



                    if (hit.isDeleteBtn)

                    {

                        HandleDeleteDistinctStream(pState, hit.distinctIndex, catStr, (size_t)hit.streamIndex);

                        return 0;

                    }

                    if (hit.isAddStreamBtn)

                    {

                        HandleAddDistinctButtonClick(pState, hit.distinctIndex, catStr, -1);

                        return 0;

                    }

                    if (hit.isActionBtn && hit.streamIndex >= 0)

                    {

                        HandleAddDistinctButtonClick(pState, hit.distinctIndex, catStr, hit.streamIndex);

                        return 0;

                    }

                    if (!hit.isActionBtn && !hit.isDeleteBtn && !hit.isAddStreamBtn && hit.streamIndex >= 0)

                    {

                        pState->selectedDistinctIndex = hit.distinctIndex;

                        const RSSSoundEntry* pEntry = nullptr;

                        const std::vector<RSSSoundEntry>* pList = (catStr == L"Wagon") ? &dv.wagonSounds :

                                                                 (catStr == L"Inside") ? &dv.insideSounds :

                                                                 (catStr == L"ORTS3DCab") ? &dv.orts3dCabSounds : &dv.engineSounds;

                        if (pList && (size_t)hit.streamIndex < pList->size())

                        {

                            pEntry = &(*pList)[hit.streamIndex];

                        }

                        SelectSoundInInspector(pState, pEntry, &dv, catStr);

                        InvalidateRect(hWnd, NULL, FALSE);

                        return 0;

                    }

                }

            }

        }



        // 2. Card Header and Section Chevron hit testing

        for (size_t di = 0; di < pState->distinctVehicles.size(); ++di)

        {

            auto& dv = pState->distinctVehicles[di];



            // Card Header Chevron click

            if (PtInRect(&dv.rcChevron, pt))

            {

                dv.isCardExpanded = !dv.isCardExpanded;

                pState->selectedDistinctIndex = di;

                InvalidateRect(hWnd, NULL, FALSE);

                return 0;

            }



            // [ 📄 Open Stock File ]

            if (PtInRect(&dv.rcBtnOpenStock, pt))

            {

                if (dv.fileFound && !dv.fullPath.empty())

                {

                    OpenFileInDefaultEditor(pState->hWnd, dv.fullPath);

                }

                return 0;

            }



            // [ 💾 Save Sound ]

            if (PtInRect(&dv.rcBtnSave, pt))

            {

                if (!dv.unitIndices.empty())

                {

                    SoundStudio_SaveUnitChanges(pState->hWnd, dv.unitIndices[0] - 1);

                }

                return 0;

            }



            // Wagon Section Chevron click

            if (PtInRect(&dv.rcWagonSectionChevron, pt))

            {

                dv.isWagonSectionExpanded = !dv.isWagonSectionExpanded;

                InvalidateRect(hWnd, NULL, FALSE);

                return 0;

            }



            // Engine Section Chevron click

            if (dv.isEngine && PtInRect(&dv.rcEngineSectionChevron, pt))

            {

                dv.isEngineSectionExpanded = !dv.isEngineSectionExpanded;

                InvalidateRect(hWnd, NULL, FALSE);

                return 0;

            }

        }



        return 0;

    }



    case WM_MOUSEMOVE:

    {

        if (pState)

        {

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            if (!pState->canvasMouseTracking)

            {

                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };

                TrackMouseEvent(&tme);

                pState->canvasMouseTracking = true;

            }

            pState->canvasMousePos = pt;

            if (pState->canvasScrollBar.OnMouseMove(pt, hWnd))

            {

                pState->canvasScrollY = pState->canvasScrollBar.GetPos();

            }

            InvalidateRect(hWnd, NULL, FALSE);

        }

        return 0;

    }



    case WM_MOUSELEAVE:

    {

        if (pState)

        {

            pState->canvasMouseTracking = false;

            pState->canvasMousePos = { -1, -1 };

            InvalidateRect(hWnd, NULL, FALSE);

        }

        return 0;

    }



    case WM_LBUTTONUP:

    {

        if (pState)

        {

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            pState->canvasScrollBar.OnLButtonUp(pt, hWnd);

        }

        return 0;

    }



    case WM_SETCURSOR:

    {

        if (pState)

        {

            POINT pt;

            GetCursorPos(&pt);

            ScreenToClient(hWnd, &pt);



            for (const auto& hit : pState->canvasHitRegions)

            {

                if (PtInRect(&hit.rc, pt))

                {

                    SetCursor(LoadCursor(NULL, IDC_HAND));

                    return TRUE;

                }

            }



            for (const auto& dv : pState->distinctVehicles)

            {

                if (PtInRect(&dv.rcChevron, pt) || PtInRect(&dv.rcBtnOpenStock, pt) || PtInRect(&dv.rcBtnSave, pt) ||

                    PtInRect(&dv.rcWagonSectionChevron, pt) || (dv.isEngine && PtInRect(&dv.rcEngineSectionChevron, pt)))

                {

                    SetCursor(LoadCursor(NULL, IDC_HAND));

                    return TRUE;

                }

            }

        }

        break;

    }



    case WM_RBUTTONUP:

    {

        if (!pState) return 0;

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };



        for (const auto& hit : pState->canvasHitRegions)

        {

            if (PtInRect(&hit.rc, pt) && hit.distinctIndex < pState->distinctVehicles.size())

            {

                auto& dv = pState->distinctVehicles[hit.distinctIndex];

                std::wstring cat = (hit.slotType == RSSSlotHitInfo::WagonExterior) ? L"Wagon" :

                                   (hit.slotType == RSSSlotHitInfo::WagonInside) ? L"Inside" :

                                   (hit.slotType == RSSSlotHitInfo::WagonOrts3D) ? L"ORTS3DCab" : L"Engine";



                const RSSSoundEntry* pEntry = nullptr;

                const std::vector<RSSSoundEntry>* pList = (cat == L"Wagon") ? &dv.wagonSounds :

                                                         (cat == L"Inside") ? &dv.insideSounds :

                                                         (cat == L"ORTS3DCab") ? &dv.orts3dCabSounds : &dv.engineSounds;

                if (pList && hit.streamIndex >= 0 && (size_t)hit.streamIndex < pList->size())

                {

                    pEntry = &(*pList)[hit.streamIndex];

                }



                if (pEntry)

                {

                    POINT ptScreen = pt;

                    ClientToScreen(hWnd, &ptScreen);

                    std::vector<ContextMenuItem> items;

                    items.push_back(ContextMenuItem::Action(7, L"\xE70F", L"Open in SMS Editor", L"DblClick", pEntry->existsOnDisk));

                    items.push_back(ContextMenuItem::Separator());

                    items.push_back(ContextMenuItem::Action(2, L"\xE8DA", L"Browse to Replace SMS...", L"", true));
        items.push_back(ContextMenuItem::Action(8, L"\xE710", L"Add Additional SMS (Open Rails Multi-SMS)...", L"", true));

                    // Add Additional SMS handled above

                    if (hit.streamIndex >= 0)

                    {

                        items.push_back(ContextMenuItem::Action(11, L"\xE74D", L"Remove This SMS", L"", true));

                    }

                    items.push_back(ContextMenuItem::Separator());

                    items.push_back(ContextMenuItem::Action(3, L"\xE8C8", L"Copy SMS File Name (" + pEntry->smsFileName + L")", L"Ctrl+C", true));

                    items.push_back(ContextMenuItem::Action(5, L"\xE8DA", L"Open Containing Folder in Explorer", L"", pEntry->existsOnDisk));



                    int cmd = ModernContextMenu::Show(pState->hWnd, ptScreen.x, ptScreen.y, items, TRUE);

                    if (cmd == 7 && pEntry->existsOnDisk)

                    {

                        SoundStudio_OpenSmsInEditor(pState->hWnd, pEntry->fullDiskPath);

                    }

                    else if (cmd == 2)

                    {

                        HandleAddDistinctButtonClick(pState, hit.distinctIndex, cat, hit.streamIndex);

                    }

                    else if (cmd == 10 || cmd == 8)

                    {

                        HandleAddDistinctButtonClick(pState, hit.distinctIndex, cat, -1);

                    }

                    else if (cmd == 11 && hit.streamIndex >= 0)

                    {

                        HandleDeleteDistinctStream(pState, hit.distinctIndex, cat, (size_t)hit.streamIndex);

                    }

                    else if (cmd == 3)

                    {

                        CopyStringToClipboard(pState->hWnd, pEntry->smsFileName);

                    }

                    else if (cmd == 5 && pEntry->existsOnDisk)

                    {

                        std::wstring exploreCmd = L"/select,\"" + pEntry->fullDiskPath + L"\"";

                        ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);

                    }

                    return 0;

                }

            }

        }

        return 0;

    }

    case WM_KEYDOWN:
    {
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (bCtrl && (wParam == 'Z' || wParam == 'z'))
        {
            if (pState && pState->activeSubTab == 0)
            {
                if (bShift) RSS_PerformRedo(pState);
                else RSS_PerformUndo(pState);
                return 0;
            }
        }
        if (bCtrl && (wParam == 'Y' || wParam == 'y'))
        {
            if (pState && pState->activeSubTab == 0)
            {
                RSS_PerformRedo(pState);
                return 0;
            }
        }
        break;
    }

    default:
        break;

    }



    return DefWindowProcW(hWnd, uMsg, wParam, lParam);

}



static void DrawConsistBanner(RollingStockStudioState* pState, HDC hdc)
{
    if (!pState || pState->rcConsistBanner.right <= pState->rcConsistBanner.left) return;

    RECT rc = pState->rcConsistBanner;

    // Background: Dark card with rounded corners
    HBRUSH hbr = CreateSolidBrush(RGB(22, 25, 33));
    HPEN hp = CreatePen(PS_SOLID, 1, RGB(46, 52, 66));
    HGDIOBJ ob = SelectObject(hdc, hbr);
    HGDIOBJ op = SelectObject(hdc, hp);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(hbr);
    DeleteObject(hp);

    // Left Accent Strip (3px vertical indicator)
    RECT rcAccent = { rc.left + 5, rc.top + 4, rc.left + 8, rc.bottom - 4 };
    HBRUSH hbrAccent = CreateSolidBrush(RGB(0, 140, 230));
    FillRect(hdc, &rcAccent, hbrAccent);
    DeleteObject(hbrAccent);

    // Prepare text details
    std::wstring titleType = L"Consist";
    std::wstring mainTitle = pState->consistName.empty() ? L"(No Consist Selected)" : pState->consistName;
    std::wstring rightDetail = pState->unitsSoundData.empty() ? L"" : (std::to_wstring(pState->unitsSoundData.size()) + L" Vehicles in Consist");

    if (pState->isDirectSmsMode)
    {
        titleType = L"SMS File";
        size_t ls = pState->directSmsPath.find_last_of(L"\\/");
        mainTitle = (ls != std::wstring::npos) ? pState->directSmsPath.substr(ls + 1) : pState->directSmsPath;
        rightDetail = pState->directSmsPath;
    }
    else if (pState->isDirectStockMode)
    {
        titleType = L"Stock File";
        mainTitle = pState->directStockItem.szFileName + pState->directStockItem.szExtension;
        rightDetail = pState->directStockItem.szFolder;
    }

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);

    // Draw Train Icon (\xE7C0 from MDL2 Assets or train symbol)
    int textLeft = rc.left + 15;
    if (pState->hFontIcons)
    {
        SelectObject(hdc, pState->hFontIcons);
        SetTextColor(hdc, RGB(0, 160, 255));
        RECT rcIcon = { textLeft, rc.top, textLeft + 20, rc.bottom };
        DrawTextW(hdc, L"\xE7C0", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        textLeft += 24;
    }

    // Type label (e.g. "Consist: ") in cyan
    SelectObject(hdc, pState->hFontBold);
    SetTextColor(hdc, RGB(100, 190, 255));
    std::wstring prefix = titleType + L": ";
    SIZE szPrefix;
    GetTextExtentPoint32W(hdc, prefix.c_str(), (int)prefix.length(), &szPrefix);
    RECT rcPrefix = { textLeft, rc.top, textLeft + szPrefix.cx, rc.bottom };
    DrawTextW(hdc, prefix.c_str(), -1, &rcPrefix, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    textLeft += szPrefix.cx;

    // Right-side detail text (relative file path or directory)
    int rightLimit = rc.right - 14;
    if (!rightDetail.empty())
    {
        SelectObject(hdc, pState->hFontBody);
        SetTextColor(hdc, RGB(130, 145, 170));
        SIZE szRight;
        GetTextExtentPoint32W(hdc, rightDetail.c_str(), (int)rightDetail.length(), &szRight);
        int rw = min(szRight.cx + 10, (rc.right - rc.left) / 3);
        RECT rcRight = { rc.right - rw - 12, rc.top, rc.right - 12, rc.bottom };
        DrawTextW(hdc, rightDetail.c_str(), -1, &rcRight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        rightLimit = rcRight.left - 12;
    }

    // Main title (in bold crisp white)
    if (rightLimit > textLeft + 20)
    {
        SelectObject(hdc, pState->hFontBold);
        SetTextColor(hdc, RGB(240, 244, 252));
        RECT rcTitle = { textLeft, rc.top, rightLimit, rc.bottom };
        DrawTextW(hdc, mainTitle.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SetBkMode(hdc, oldBkMode);
}

static void DrawInspectorCard(RollingStockStudioState* pState, HDC hDestDC)

{

    if (!pState || pState->activeSubTab != 0) return;

    int insW = pState->rcInspectorCard.right - pState->rcInspectorCard.left;

    int insH = pState->rcInspectorCard.bottom - pState->rcInspectorCard.top;

    if (insW <= 60 || insH <= 60) return;



    HDC hMemDC = CreateCompatibleDC(hDestDC);

    HBITMAP hBmp = CreateCompatibleBitmap(hDestDC, insW, insH);

    HGDIOBJ oldBmp = SelectObject(hMemDC, hBmp);



    int sel = pState->soundMatrixList.GetSelectedIndex();

    if (sel < 0 && !pState->unitsSoundData.empty())

    {

        sel = 0;

    }



    const RSSUnitSoundInfo* pSelectedUnit = nullptr;

    if (sel >= 0 && sel < (int)pState->unitsSoundData.size())

    {

        pSelectedUnit = &pState->unitsSoundData[sel];

    }

    else if (pState->selectedDistinctIndex < pState->distinctVehicles.size() && !pState->unitsSoundData.empty())

    {

        int uIdx = pState->distinctVehicles[pState->selectedDistinctIndex].firstUnitIndex;

        if (uIdx >= 0 && uIdx < (int)pState->unitsSoundData.size())

            pSelectedUnit = &pState->unitsSoundData[uIdx];

    }



    const RSSSoundEntry* pSelectedEntry = pState->selectedInspectorEntry;

    std::wstring slotCategory = pState->selectedInspectorSlotCategory.empty() ? L"Wagon" : pState->selectedInspectorSlotCategory;



    if (!pSelectedEntry && pSelectedUnit)

    {

        if (!pSelectedUnit->wagonSounds.empty()) { pSelectedEntry = &pSelectedUnit->wagonSounds[0]; slotCategory = L"Wagon"; }

        else if (!pSelectedUnit->engineSounds.empty()) { pSelectedEntry = &pSelectedUnit->engineSounds[0]; slotCategory = L"Engine"; }

        else if (!pSelectedUnit->orts3dCabSounds.empty()) { pSelectedEntry = &pSelectedUnit->orts3dCabSounds[0]; slotCategory = L"ORTS3DCab"; }

        else if (!pSelectedUnit->insideSounds.empty()) { pSelectedEntry = &pSelectedUnit->insideSounds[0]; slotCategory = L"Inside"; }

    }



    bool isEngine = (pSelectedUnit && pSelectedUnit->type == L"Engine");

    COLORREF accentColor = isEngine ? RGB(0, 145, 255) : (pSelectedUnit ? RGB(0, 195, 150) : RGB(80, 150, 230));



    // 1. Background outer card fill & border (Option A: Fluent Dark Surface)

    RECT rcLocal = { 0, 0, insW, insH };

    HBRUSH hbrCard = CreateSolidBrush(RGB(18, 20, 26));

    HPEN hpBorder = CreatePen(PS_SOLID, 1, RGB(46, 52, 66));

    HGDIOBJ oldBr = SelectObject(hMemDC, hbrCard);

    HGDIOBJ oldPen = SelectObject(hMemDC, hpBorder);

    RoundRect(hMemDC, rcLocal.left, rcLocal.top, rcLocal.right, rcLocal.bottom, 6, 6);

    SelectObject(hMemDC, oldBr);

    SelectObject(hMemDC, oldPen);

    DeleteObject(hbrCard);

    DeleteObject(hpBorder);



    // 2. Docked Accent Header Bar

    int hdrH = 30;

    RECT rcHdr = { 1, 1, insW - 1, hdrH };

    HBRUSH hbrHdr = CreateSolidBrush(RGB(24, 28, 38));

    FillRect(hMemDC, &rcHdr, hbrHdr);

    DeleteObject(hbrHdr);



    // Bottom 1px header divider line

    HPEN hpHdrLine = CreatePen(PS_SOLID, 1, RGB(38, 44, 56));

    HGDIOBJ opHdr = SelectObject(hMemDC, hpHdrLine);

    MoveToEx(hMemDC, 0, hdrH - 1, NULL);

    LineTo(hMemDC, insW, hdrH - 1);

    SelectObject(hMemDC, opHdr);

    DeleteObject(hpHdrLine);



    SetBkMode(hMemDC, TRANSPARENT);



    // Header Icon & Title

    int titleX = 14;

    if (pState->hFontIcons)

    {

        RECT rcIcon = { titleX, 2, titleX + 22, hdrH };

        SelectObject(hMemDC, pState->hFontIcons);

        SetTextColor(hMemDC, RGB(80, 185, 255));

        DrawTextW(hMemDC, L"\xE767", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        titleX += 26;

    }

    RECT rcTitle = { titleX, 2, insW / 2, hdrH };

    SelectObject(hMemDC, pState->hFontBold);

    SetTextColor(hMemDC, RGB(242, 246, 255));

    DrawTextW(hMemDC, L"Selected Sound Stream Inspector & Audio Preview Deck", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



    // Header Badges on the right (Option A: Fluent Rounded Pills with Glowing Status Dots)

    int badgeRight = insW - 12;

    auto drawHdrBadge = [&](const std::wstring& txt, COLORREF bg, COLORREF border, COLORREF fg, bool hasDot, COLORREF dotColor) {

        SelectObject(hMemDC, pState->hFontBold);

        SIZE sz;

        GetTextExtentPoint32W(hMemDC, txt.c_str(), (int)txt.length(), &sz);

        int extraDotW = hasDot ? 12 : 0;

        int bW = sz.cx + 18 + extraDotW;

        int bX = badgeRight - bW;

        RECT rcB = { bX, 4, badgeRight, 26 };



        HBRUSH hbr = CreateSolidBrush(bg);

        HPEN hp = CreatePen(PS_SOLID, 1, border);

        HGDIOBJ ob = SelectObject(hMemDC, hbr);

        HGDIOBJ op = SelectObject(hMemDC, hp);

        RoundRect(hMemDC, rcB.left, rcB.top, rcB.right, rcB.bottom, 10, 10);

        SelectObject(hMemDC, ob);

        SelectObject(hMemDC, op);

        DeleteObject(hbr);

        DeleteObject(hp);



        if (hasDot)

        {

            int dotSize = 6;

            int dotX = rcB.left + 9;

            int dotY = rcB.top + (22 - dotSize) / 2;

            HBRUSH hbrDot = CreateSolidBrush(dotColor);

            HPEN hpDot = CreatePen(PS_SOLID, 1, dotColor);

            HGDIOBJ obD = SelectObject(hMemDC, hbrDot);

            HGDIOBJ opD = SelectObject(hMemDC, hpDot);

            Ellipse(hMemDC, dotX, dotY, dotX + dotSize, dotY + dotSize);

            SelectObject(hMemDC, obD);

            SelectObject(hMemDC, opD);

            DeleteObject(hbrDot);

            DeleteObject(hpDot);



            RECT rcText = { dotX + dotSize + 6, rcB.top, rcB.right - 8, rcB.bottom };

            SetTextColor(hMemDC, fg);

            DrawTextW(hMemDC, txt.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        else

        {

            SetTextColor(hMemDC, fg);

            DrawTextW(hMemDC, txt.c_str(), -1, &rcB, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        badgeRight = bX - 8;

    };



    if (pSelectedEntry && pSelectedEntry->isFromInclude)

    {

        drawHdrBadge(L"🔗 Included from .inc", RGB(38, 24, 52), RGB(95, 50, 135), RGB(215, 160, 255), false, 0);

    }



    if (pSelectedEntry)

    {

        if (pSelectedEntry->existsOnDisk)

            drawHdrBadge(L"Sound Ready", RGB(16, 40, 24), RGB(38, 105, 55), RGB(80, 230, 130), true, RGB(80, 230, 130));

        else

            drawHdrBadge(L"Missing File", RGB(48, 18, 20), RGB(145, 42, 45), RGB(255, 95, 95), true, RGB(255, 95, 95));

    }



    if (pSelectedUnit)

    {

        std::wstring uTag = L"Unit #" + std::to_wstring(pSelectedUnit->index) + L" • " + pSelectedUnit->type;

        COLORREF uBg = isEngine ? RGB(18, 38, 62) : RGB(16, 42, 38);

        COLORREF uBdr = isEngine ? RGB(38, 90, 150) : RGB(30, 110, 85);

        COLORREF uFg = isEngine ? RGB(105, 190, 255) : RGB(80, 225, 175);

        drawHdrBadge(uTag, uBg, uBdr, uFg, false, 0);

    }



    if (!pSelectedUnit)

    {

        // Placeholder Inset Tile

        RECT rcPlaceholder = { 8, hdrH + 6, insW - 8, insH - 6 };

        HBRUSH hbrPl = CreateSolidBrush(RGB(14, 17, 23));

        HPEN hpPl = CreatePen(PS_SOLID, 1, RGB(36, 42, 54));

        HGDIOBJ obPl = SelectObject(hMemDC, hbrPl);

        HGDIOBJ opPl = SelectObject(hMemDC, hpPl);

        RoundRect(hMemDC, rcPlaceholder.left, rcPlaceholder.top, rcPlaceholder.right, rcPlaceholder.bottom, 6, 6);

        SelectObject(hMemDC, obPl);

        SelectObject(hMemDC, opPl);

        DeleteObject(hbrPl);

        DeleteObject(hpPl);



        SelectObject(hMemDC, pState->hFontBody);

        SetTextColor(hMemDC, UITheme::TextMuted);

        DrawTextW(hMemDC, L"Select any unit or sound stream from the matrix above to inspect audio channels and preview sounds.", -1, &rcPlaceholder, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    }

    else

    {

        // 3. Option A: Dual Inset Content Cards

        int padX = 8;

        int padTop = hdrH + 6;

        int padBottom = insH - 6;

        int gap = 8;

        int totalTilesW = insW - padX * 2 - gap;

        int leftTileW = (totalTilesW * 44) / 100;

        if (leftTileW < 220) leftTileW = 220;

        int rightTileW = totalTilesW - leftTileW;

        if (rightTileW < 260) rightTileW = 260;



        RECT rcLeftTile = { padX, padTop, padX + leftTileW, padBottom };

        RECT rcRightTile = { padX + leftTileW + gap, padTop, padX + leftTileW + gap + rightTileW, padBottom };



        auto drawInsetTile = [&](const RECT& rc) {

            HBRUSH hbr = CreateSolidBrush(RGB(14, 17, 23));

            HPEN hp = CreatePen(PS_SOLID, 1, RGB(36, 42, 54));

            HGDIOBJ ob = SelectObject(hMemDC, hbr);

            HGDIOBJ op = SelectObject(hMemDC, hp);

            RoundRect(hMemDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

            SelectObject(hMemDC, ob);

            SelectObject(hMemDC, op);

            DeleteObject(hbr);

            DeleteObject(hp);

        };



        drawInsetTile(rcLeftTile);

        drawInsetTile(rcRightTile);



        int btnH = 24;

        int btnY = rcLeftTile.bottom - btnH - 8;

        int curY = rcLeftTile.top + 8;

        int leftPadX = rcLeftTile.left + 12;

        int leftInnerW = (rcLeftTile.right - 12) - leftPadX;

        int lineStep = (btnY - curY > 105) ? 18 : 16;



        // ================= LEFT INSET TILE =================

        // Accent pip before subtitle

        RECT rcLeftAccent = { leftPadX, curY + 2, leftPadX + 3, curY + 13 };

        HBRUSH hbrLeftPip = CreateSolidBrush(accentColor);

        FillRect(hMemDC, &rcLeftAccent, hbrLeftPip);

        DeleteObject(hbrLeftPip);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, RGB(110, 145, 195));

        RECT rcSecLeft = { leftPadX + 7, curY, leftPadX + leftInnerW, curY + 15 };

        DrawTextW(hMemDC, L"ROLLING STOCK CONTEXT", -1, &rcSecLeft, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        curY += lineStep;



        // Unit Name

        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, RGB(255, 255, 255));

        RECT rcUName = { leftPadX, curY, leftPadX + leftInnerW, curY + 17 };

        DrawTextW(hMemDC, pSelectedUnit->name.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        curY += lineStep;



        // Vehicle Type & Folder

        SelectObject(hMemDC, pState->hFontBody);

        SetTextColor(hMemDC, UITheme::TextMuted);

        std::wstring tfStr = L"Type: " + pSelectedUnit->type + L"    Folder: " + pSelectedUnit->folder;

        RECT rcTF = { leftPadX, curY, leftPadX + leftInnerW, curY + 15 };

        DrawTextW(hMemDC, tfStr.c_str(), -1, &rcTF, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        curY += lineStep;



        // Stock File

        std::wstring stFile = L"Stock: " + pSelectedUnit->fullPath;

        RECT rcSF = { leftPadX, curY, leftPadX + leftInnerW, curY + 15 };

        DrawTextW(hMemDC, stFile.c_str(), -1, &rcSF, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        curY += lineStep;



        // Include Source

        RECT rcInc = { leftPadX, curY, leftPadX + leftInnerW, curY + 15 };

        if (pSelectedEntry && pSelectedEntry->isFromInclude)

        {

            SetTextColor(hMemDC, RGB(190, 150, 250));

            std::wstring incTxt = L"Source: Included via " + pSelectedEntry->incDirectiveString;

            DrawTextW(hMemDC, incTxt.c_str(), -1, &rcInc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        }

        else

        {

            SetTextColor(hMemDC, RGB(145, 160, 180));

            DrawTextW(hMemDC, L"Source: Defined directly in stock file", -1, &rcInc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        curY += lineStep;



        // Assigned Slots

        SetTextColor(hMemDC, RGB(175, 185, 200));

        std::wstring slotsTxt = L"Slots: Wagon (" + std::to_wstring(pSelectedUnit->wagonSounds.size()) + L") • Engine (" +

            std::to_wstring(pSelectedUnit->engineSounds.size()) + L") • 3DCab (" +

            std::to_wstring(pSelectedUnit->orts3dCabSounds.size()) + L") • Inside (" +

            std::to_wstring(pSelectedUnit->insideSounds.size()) + L")";

        RECT rcSlots = { leftPadX, curY, leftPadX + leftInnerW, curY + 15 };

        DrawTextW(hMemDC, slotsTxt.c_str(), -1, &rcSlots, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



        // Button: [ 📄 Open Stock File ]

        RECT rcStockLocal = { leftPadX, btnY, leftPadX + 145, btnY + btnH };

        pState->rcBtnOpenStock = {

            pState->rcInspectorCard.left + rcStockLocal.left,

            pState->rcInspectorCard.top + rcStockLocal.top,

            pState->rcInspectorCard.left + rcStockLocal.right,

            pState->rcInspectorCard.top + rcStockLocal.bottom

        };



        bool isStockHov = (pState->hoverInspectorBtn == 4);

        HBRUSH hbrStk = CreateSolidBrush(isStockHov ? RGB(45, 54, 70) : RGB(28, 33, 44));

        HPEN hpStk = CreatePen(PS_SOLID, 1, isStockHov ? RGB(80, 95, 125) : RGB(48, 56, 72));

        HGDIOBJ ob1 = SelectObject(hMemDC, hbrStk);

        HGDIOBJ op1 = SelectObject(hMemDC, hpStk);

        RoundRect(hMemDC, rcStockLocal.left, rcStockLocal.top, rcStockLocal.right, rcStockLocal.bottom, 4, 4);

        SelectObject(hMemDC, ob1);

        SelectObject(hMemDC, op1);

        DeleteObject(hbrStk);

        DeleteObject(hpStk);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, isStockHov ? RGB(255, 255, 255) : RGB(215, 222, 235));

        DrawTextW(hMemDC, L"📄 Open Stock File", -1, &rcStockLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);



        // ================= RIGHT INSET TILE =================

        curY = rcRightTile.top + 8;

        int rightPadX = rcRightTile.left + 12;

        int rightInnerW = (rcRightTile.right - 12) - rightPadX;



        // Accent pip before subtitle

        RECT rcRightAccent = { rightPadX, curY + 2, rightPadX + 3, curY + 13 };

        HBRUSH hbrRightPip = CreateSolidBrush(RGB(80, 185, 255));

        FillRect(hMemDC, &rcRightAccent, hbrRightPip);

        DeleteObject(hbrRightPip);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, RGB(110, 145, 195));

        RECT rcSecRight = { rightPadX + 7, curY, rightPadX + rightInnerW, curY + 15 };

        DrawTextW(hMemDC, L"SOUND MANAGEMENT SYSTEM (.SMS) & PREVIEW", -1, &rcSecRight, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        curY += lineStep;



        // Active Slot & SMS Name

        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, RGB(70, 205, 255));

        std::wstring smsTitle = slotCategory + L" Sound: " + (pSelectedEntry && !pSelectedEntry->smsFileName.empty() ? pSelectedEntry->smsFileName : L"(None Assigned)");

        RECT rcSmsTitle = { rightPadX, curY, rightPadX + rightInnerW, curY + 17 };

        DrawTextW(hMemDC, smsTitle.c_str(), -1, &rcSmsTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        curY += lineStep;



        // Disk Path

        SelectObject(hMemDC, pState->hFontBody);

        RECT rcDisk = { rightPadX, curY, rightPadX + rightInnerW, curY + 15 };

        if (pSelectedEntry && !pSelectedEntry->fullDiskPath.empty())

        {

            if (pSelectedEntry->existsOnDisk)

            {

                SetTextColor(hMemDC, RGB(95, 230, 130));

                std::wstring pathTxt = L"✓ Path: " + pSelectedEntry->fullDiskPath;

                DrawTextW(hMemDC, pathTxt.c_str(), -1, &rcDisk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

            else

            {

                SetTextColor(hMemDC, RGB(255, 95, 95));

                std::wstring pathTxt = L"⚠ Path: " + pSelectedEntry->fullDiskPath + L" (File Not Found)";

                DrawTextW(hMemDC, pathTxt.c_str(), -1, &rcDisk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

        }

        else

        {

            SetTextColor(hMemDC, UITheme::TextMuted);

            DrawTextW(hMemDC, L"No sound file path assigned", -1, &rcDisk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        curY += lineStep;



        // SMS Stream Statistics

        RECT rcStats = { rightPadX, curY, rightPadX + rightInnerW, curY + 15 };

        if (pState->cachedSmsLoaded)

        {

            size_t numGroups = pState->cachedSmsDoc.groups.size();

            size_t numStreams = pState->cachedSmsDoc.groups.empty() ? 0 : pState->cachedSmsDoc.groups[0].streams.size();

            SetTextColor(hMemDC, RGB(195, 208, 225));

            std::wstring statsTxt = L"Structure: " + std::to_wstring(numGroups) + L" Scalability Groups • " + std::to_wstring(numStreams) + L" Sound Streams in SG5";

            DrawTextW(hMemDC, statsTxt.c_str(), -1, &rcStats, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        else if (pSelectedEntry && pSelectedEntry->existsOnDisk)

        {

            SetTextColor(hMemDC, UITheme::TextMuted);

            DrawTextW(hMemDC, L"Structure: SMS file detected on disk", -1, &rcStats, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        else

        {

            SetTextColor(hMemDC, UITheme::TextMuted);

            DrawTextW(hMemDC, L"Structure: No valid SMS parsed", -1, &rcStats, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }

        curY += lineStep;



        // Audio WAV Sample

        RECT rcWav = { rightPadX, curY, rightPadX + rightInnerW, curY + 15 };

        if (pState->cachedWavExists)

        {

            SetTextColor(hMemDC, RGB(105, 235, 145));

            std::wstring wavTxt = L"🎵 Sample Audio: " + pState->cachedPrimaryWav + L" (Verified)";

            DrawTextW(hMemDC, wavTxt.c_str(), -1, &rcWav, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        }

        else if (!pState->cachedPrimaryWav.empty())

        {

            SetTextColor(hMemDC, RGB(255, 150, 100));

            std::wstring wavTxt = L"⚠ Sample Audio: " + pState->cachedPrimaryWav + L" (Not found on disk)";

            DrawTextW(hMemDC, wavTxt.c_str(), -1, &rcWav, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        }

        else

        {

            SetTextColor(hMemDC, UITheme::TextMuted);

            DrawTextW(hMemDC, L"🎵 Sample Audio: No .wav triggers found in SMS", -1, &rcWav, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        }



        // Action Buttons in Right Tile

        int bX = rightPadX;

        int bW1 = 135;

        RECT rcPlayLocal = { bX, btnY, bX + bW1, btnY + btnH };

        pState->rcBtnPlayAudio = {

            pState->rcInspectorCard.left + rcPlayLocal.left,

            pState->rcInspectorCard.top + rcPlayLocal.top,

            pState->rcInspectorCard.left + rcPlayLocal.right,

            pState->rcInspectorCard.top + rcPlayLocal.bottom

        };

        pState->rcBtnStopAudio = pState->rcBtnPlayAudio;



        bool isPlayHov = (pState->hoverInspectorBtn == 0);

        COLORREF playBg, playBorder, playFg;

        std::wstring playLabel;



        if (pState->isAudioPlaying)

        {

            playBg = isPlayHov ? RGB(185, 48, 48) : RGB(155, 36, 36);

            playBorder = isPlayHov ? RGB(245, 85, 85) : RGB(215, 60, 60);

            playFg = RGB(255, 255, 255);

            playLabel = L"⏹ Stop Audio";

        }

        else if (pState->cachedWavExists && !pState->cachedPrimaryWav.empty())

        {

            playBg = isPlayHov ? RGB(28, 90, 48) : RGB(20, 68, 36);

            playBorder = isPlayHov ? RGB(52, 155, 80) : RGB(38, 120, 62);

            playFg = RGB(105, 240, 145);

            playLabel = L"▶ Preview Audio";

        }

        else

        {

            playBg = RGB(24, 27, 34);

            playBorder = RGB(40, 44, 54);

            playFg = RGB(95, 102, 112);

            playLabel = L"▶ Preview Audio";

        }



        HBRUSH hbrPlay = CreateSolidBrush(playBg);

        HPEN hpPlay = CreatePen(PS_SOLID, 1, playBorder);

        HGDIOBJ ob2 = SelectObject(hMemDC, hbrPlay);

        HGDIOBJ op2 = SelectObject(hMemDC, hpPlay);

        RoundRect(hMemDC, rcPlayLocal.left, rcPlayLocal.top, rcPlayLocal.right, rcPlayLocal.bottom, 4, 4);

        SelectObject(hMemDC, ob2);

        SelectObject(hMemDC, op2);

        DeleteObject(hbrPlay);

        DeleteObject(hpPlay);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, playFg);

        DrawTextW(hMemDC, playLabel.c_str(), -1, &rcPlayLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);



        // Button 2: [ ✏ Open in SMS Editor ]

        bX += bW1 + 8;

        int bW2 = 150;

        RECT rcEditLocal = { bX, btnY, bX + bW2, btnY + btnH };

        pState->rcBtnOpenEditor = {

            pState->rcInspectorCard.left + rcEditLocal.left,

            pState->rcInspectorCard.top + rcEditLocal.top,

            pState->rcInspectorCard.left + rcEditLocal.right,

            pState->rcInspectorCard.top + rcEditLocal.bottom

        };



        bool isEditHov = (pState->hoverInspectorBtn == 2);

        bool hasSms = (pSelectedEntry && !pSelectedEntry->fullDiskPath.empty() && pSelectedEntry->existsOnDisk);

        COLORREF editBg = !hasSms ? RGB(24, 27, 34) : (isEditHov ? RGB(28, 75, 128) : RGB(20, 52, 90));

        COLORREF editBorder = !hasSms ? RGB(40, 44, 54) : (isEditHov ? RGB(60, 135, 215) : RGB(40, 100, 168));

        COLORREF editFg = !hasSms ? RGB(95, 102, 112) : (isEditHov ? RGB(255, 255, 255) : RGB(125, 200, 255));



        HBRUSH hbrEdit = CreateSolidBrush(editBg);

        HPEN hpEdit = CreatePen(PS_SOLID, 1, editBorder);

        HGDIOBJ ob3 = SelectObject(hMemDC, hbrEdit);

        HGDIOBJ op3 = SelectObject(hMemDC, hpEdit);

        RoundRect(hMemDC, rcEditLocal.left, rcEditLocal.top, rcEditLocal.right, rcEditLocal.bottom, 4, 4);

        SelectObject(hMemDC, ob3);

        SelectObject(hMemDC, op3);

        DeleteObject(hbrEdit);

        DeleteObject(hpEdit);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, editFg);

        DrawTextW(hMemDC, L"✏ Open in SMS Editor", -1, &rcEditLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);



        // Button 3: [ 📁 Reveal in Explorer ]

        bX += bW2 + 8;

        int bW3 = 145;

        RECT rcRevLocal = { bX, btnY, bX + bW3, btnY + btnH };

        pState->rcBtnRevealFolder = {

            pState->rcInspectorCard.left + rcRevLocal.left,

            pState->rcInspectorCard.top + rcRevLocal.top,

            pState->rcInspectorCard.left + rcRevLocal.right,

            pState->rcInspectorCard.top + rcRevLocal.bottom

        };



        bool isRevHov = (pState->hoverInspectorBtn == 3);

        HBRUSH hbrRev = CreateSolidBrush(isRevHov ? RGB(45, 54, 70) : RGB(28, 33, 44));

        HPEN hpRev = CreatePen(PS_SOLID, 1, isRevHov ? RGB(80, 95, 125) : RGB(48, 56, 72));

        HGDIOBJ ob4 = SelectObject(hMemDC, hbrRev);

        HGDIOBJ op4 = SelectObject(hMemDC, hpRev);

        RoundRect(hMemDC, rcRevLocal.left, rcRevLocal.top, rcRevLocal.right, rcRevLocal.bottom, 4, 4);

        SelectObject(hMemDC, ob4);

        SelectObject(hMemDC, op4);

        DeleteObject(hbrRev);

        DeleteObject(hpRev);



        SelectObject(hMemDC, pState->hFontBold);

        SetTextColor(hMemDC, isRevHov ? RGB(255, 255, 255) : RGB(215, 222, 235));

        DrawTextW(hMemDC, L"📁 Reveal in Explorer", -1, &rcRevLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    }



    BitBlt(hDestDC, pState->rcInspectorCard.left, pState->rcInspectorCard.top, insW, insH, hMemDC, 0, 0, SRCCOPY);



    SelectObject(hMemDC, oldBmp);

    DeleteObject(hBmp);

    DeleteDC(hMemDC);

}



static LRESULT CALLBACK RollingStockStudioProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);



    switch (uMsg)

    {

    case WM_NCCREATE:

    {

        CREATESTRUCTW* pCS = (CREATESTRUCTW*)lParam;

        pState = (RollingStockStudioState*)pCS->lpCreateParams;

        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);

        pState->hWnd = hWnd;

        return TRUE;

    }



    case WM_CREATE:

    {

        if (!pState) return 0;



        HDC hDC = GetDC(hWnd);

        int logPixelsY = GetDeviceCaps(hDC, LOGPIXELSY);

        ReleaseDC(hWnd, hDC);



        pState->hFontTitle  = CreateFontW(-MulDiv(13, logPixelsY, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontHeader = CreateFontW(-MulDiv(10, logPixelsY, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontBody   = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontBold   = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontMono   = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        pState->hFontIcons  = CreateFontW(-MulDiv(13, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");

        if (!pState->hFontIcons)

            pState->hFontIcons = CreateFontW(-MulDiv(13, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");



        pState->hFontSmallIcons = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");

        if (!pState->hFontSmallIcons)

            pState->hFontSmallIcons = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");



        // Register Rolling Stock Distinct Vehicle Canvas Window Class

        static bool s_canvasClassRegistered = false;

        if (!s_canvasClassRegistered)

        {

            WNDCLASSEXW wcx = { sizeof(wcx) };

            wcx.cbSize = sizeof(wcx);

            wcx.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

            wcx.lpfnWndProc = RollingStockCanvasWndProc;

            wcx.hInstance = pState->hInstance ? pState->hInstance : GetModuleHandleW(NULL);

            wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);

            wcx.hbrBackground = NULL;

            wcx.lpszClassName = L"RollingStockCanvas";

            RegisterClassExW(&wcx);

            s_canvasClassRegistered = true;

        }



        // Create Left Consist Rake Navigator (295px wide list)

        pState->hSoundMatrixListWnd = pState->soundMatrixList.Create(hWnd, 0, 44, 295, 400, IDC_RSS_UNITS_LIST);

        if (pState->hSoundMatrixListWnd)

        {

            pState->soundMatrixList.SetShowSelectionGutter(false);

            pState->soundMatrixList.SetMultiSelect(false);
            pState->soundMatrixList.SetShowVerticalDividers(true);

            pState->soundMatrixList.SetAutoFitLastColumn(false);

            pState->soundMatrixList.SetRowHeight(32);

            pState->soundMatrixList.SetDrawCardBorder(false);

            pState->soundMatrixList.SetRowBackgroundCallback(RSS_RowBackgroundCallback, pState);

            pState->soundMatrixList.SetDrawCellCallback(RSS_NavDrawCellCallback, pState);



            ConfigureSoundMatrixColumns(pState, 1000);

        }



        // Create Master Studio Cards Canvas Child Window

        pState->hCanvasWnd = CreateWindowExW(

            0,

            L"RollingStockCanvas",

            L"",

            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,

            248, 44, 600, 400,

            hWnd,

            (HMENU)IDC_RSS_CANVAS,

            pState->hInstance ? pState->hInstance : GetModuleHandleW(NULL),

            pState

        );

        if (pState->hCanvasWnd)

        {

            SetWindowLongPtrW(pState->hCanvasWnd, GWLP_USERDATA, (LONG_PTR)pState);

            // canvasScrollBar initialized by constructor

        }



        // Create Full-Featured SMS Editor View Child Window

        pState->hSmsEditorWnd = CreateSmsEditorView(hWnd, pState->hInstance, 0, 44, 800, 400, 8999);

        if (pState->hSmsEditorWnd)

        {

            ShowWindow(pState->hSmsEditorWnd, SW_HIDE);

        }



        return 0;

    }



    case WM_SIZE:

    {

        RepositionStudioControls(pState);

        return 0;

    }



    case WM_SETCURSOR:

    {

        if (pState)

        {

            POINT pt;

            GetCursorPos(&pt);

            ScreenToClient(hWnd, &pt);

            if (PtInRect(&pState->rcSubTabMatrix, pt) || PtInRect(&pState->rcSubTabEditor, pt) ||
                PtInRect(&pState->rcBtnViewGrid, pt) || PtInRect(&pState->rcBtnViewCards, pt) ||
                PtInRect(&pState->rcBtnExpandAll, pt) || PtInRect(&pState->rcBtnCollapseAll, pt) || PtInRect(&pState->rcBtnSave, pt) ||
                PtInRect(&pState->rcBtnUndo, pt) || PtInRect(&pState->rcBtnRedo, pt) ||
                PtInRect(&pState->rcBtnPlayAudio, pt) || PtInRect(&pState->rcBtnOpenEditor, pt) ||
                PtInRect(&pState->rcBtnRevealFolder, pt) || PtInRect(&pState->rcBtnOpenStock, pt))

            {

                SetCursor(LoadCursor(NULL, IDC_HAND));

                return TRUE;

            }

        }

        break;

    }



    case WM_MOUSEMOVE:

    {

        if (pState)

        {

            POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };

            int hovSub = -1;

            if (PtInRect(&pState->rcSubTabMatrix, pt)) hovSub = 0;

            else if (PtInRect(&pState->rcSubTabEditor, pt)) hovSub = 1;



            int hov = -1;
            int hovIns = -1;
            int hovUndoRedo = -1;

            if (pState->activeSubTab == 0)
            {
                if (PtInRect(&pState->rcBtnExpandAll, pt)) hov = 0;
                else if (PtInRect(&pState->rcBtnCollapseAll, pt)) hov = 1;

                if (PtInRect(&pState->rcBtnUndo, pt)) hovUndoRedo = 1;
                else if (PtInRect(&pState->rcBtnRedo, pt)) hovUndoRedo = 2;

                if (PtInRect(&pState->rcBtnPlayAudio, pt)) hovIns = 0;
                else if (PtInRect(&pState->rcBtnOpenEditor, pt)) hovIns = 2;
                else if (PtInRect(&pState->rcBtnRevealFolder, pt)) hovIns = 3;
                else if (PtInRect(&pState->rcBtnOpenStock, pt)) hovIns = 4;
            }

            bool saveHov = (pState->activeSubTab == 0) && PtInRect(&pState->rcBtnSave, pt);

            if (hovSub != pState->hoverSubTab || hov != pState->hoverHeaderBtn || saveHov != pState->hoverSaveBtn || hovIns != pState->hoverInspectorBtn || hovUndoRedo != pState->hoverUndoRedoBtn)
            {
                pState->hoverSubTab = hovSub;
                pState->hoverHeaderBtn = hov;
                pState->hoverSaveBtn = saveHov;
                pState->hoverInspectorBtn = hovIns;
                pState->hoverUndoRedoBtn = hovUndoRedo;

                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (pState && (pState->hoverSubTab != -1 || pState->hoverHeaderBtn != -1 || pState->hoverSaveBtn || pState->hoverInspectorBtn != -1 || pState->hoverUndoRedoBtn != -1))
        {
            pState->hoverSubTab = -1;
            pState->hoverHeaderBtn = -1;
            pState->hoverSaveBtn = false;
            pState->hoverInspectorBtn = -1;
            pState->hoverUndoRedoBtn = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }



    case WM_LBUTTONDOWN:

    {

        if (pState)

        {

            POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };

            if (PtInRect(&pState->rcSubTabMatrix, pt))

            {

                SoundStudio_SwitchSubTab(hWnd, 0);

                return 0;

            }

            if (PtInRect(&pState->rcSubTabEditor, pt))

            {

                SoundStudio_SwitchSubTab(hWnd, 1);

                return 0;

            }



            if (pState->activeSubTab == 0)

            {
                if (PtInRect(&pState->rcBtnViewGrid, pt))
                {
                    if (pState->matrixViewMode != SoundMatrixViewMode::GridView)
                    {
                        pState->matrixViewMode = SoundMatrixViewMode::GridView;
                        RepositionStudioControls(pState);
                        PopulateSoundMatrix(pState);
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    return 0;
                }
                if (PtInRect(&pState->rcBtnViewCards, pt))
                {
                    if (pState->matrixViewMode != SoundMatrixViewMode::DistinctCards)
                    {
                        pState->matrixViewMode = SoundMatrixViewMode::DistinctCards;
                        RepositionStudioControls(pState);
                        PopulateSoundMatrix(pState);
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    return 0;
                }

                if (PtInRect(&pState->rcBtnSave, pt))
                {
                    SoundStudio_SaveAllChanges(hWnd);
                    return 0;
                }

                if (PtInRect(&pState->rcBtnUndo, pt))
                {
                    RSS_PerformUndo(pState);
                    return 0;
                }

                if (PtInRect(&pState->rcBtnRedo, pt))
                {
                    RSS_PerformRedo(pState);
                    return 0;
                }

                if (PtInRect(&pState->rcBtnExpandAll, pt))

                {

                    for (auto& dv : pState->distinctVehicles)

                    {

                        dv.isCardExpanded = true;

                        dv.isWagonSectionExpanded = true;

                        dv.isEngineSectionExpanded = true;

                    }

                    for (auto& u : pState->unitsSoundData) u.isExpanded = true;

                    PopulateSoundMatrix(pState);

                    if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

                    return 0;

                }

                if (PtInRect(&pState->rcBtnCollapseAll, pt))

                {

                    for (auto& dv : pState->distinctVehicles)

                    {

                        dv.isCardExpanded = false;

                        dv.isWagonSectionExpanded = false;

                        dv.isEngineSectionExpanded = false;

                    }

                    for (auto& u : pState->unitsSoundData) u.isExpanded = false;

                    PopulateSoundMatrix(pState);

                    if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

                    return 0;

                }

                if (PtInRect(&pState->rcBtnPlayAudio, pt))

                {

                    if (pState->isAudioPlaying)

                    {

                        RSS_StopAudio();

                        pState->isAudioPlaying = false;

                    }

                    else if (pState->cachedWavExists && !pState->cachedPrimaryWav.empty())

                    {

                        pState->isAudioPlaying = RSS_PlayAudio(pState->cachedPrimaryWav, true);

                    }

                    InvalidateRect(hWnd, &pState->rcInspectorCard, FALSE);

                    return 0;

                }

                if (PtInRect(&pState->rcBtnOpenEditor, pt))

                {

                    if (!pState->cachedSmsPath.empty())

                    {

                        SoundStudio_OpenSmsInEditor(hWnd, pState->cachedSmsPath);

                    }

                    return 0;

                }

                if (PtInRect(&pState->rcBtnRevealFolder, pt))

                {

                    std::wstring target = !pState->cachedSmsPath.empty() ? pState->cachedSmsPath : L"";

                    if (target.empty())

                    {

                        int sel = pState->soundMatrixList.GetSelectedIndex();

                        if (sel >= 0 && sel < (int)pState->displayRows.size())

                        {

                            int uIdx = pState->displayRows[sel].unitIndex;

                            if (uIdx >= 0 && uIdx < (int)pState->unitsSoundData.size())

                                target = pState->unitsSoundData[uIdx].fullPath;

                        }

                    }

                    if (!target.empty())

                    {

                        std::wstring params = L"/select,\"" + target + L"\"";

                        ShellExecuteW(NULL, L"open", L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);

                    }

                    return 0;

                }

                if (PtInRect(&pState->rcBtnOpenStock, pt))

                {

                    int sel = pState->soundMatrixList.GetSelectedIndex();

                    if (sel >= 0 && sel < (int)pState->displayRows.size())

                    {

                        int uIdx = pState->displayRows[sel].unitIndex;

                        if (uIdx >= 0 && uIdx < (int)pState->unitsSoundData.size())

                        {

                            const auto& u = pState->unitsSoundData[uIdx];

                            if (!u.fullPath.empty())

                                OpenFileInDefaultEditor(hWnd, u.fullPath);

                        }

                    }

                    return 0;

                }

            }

        }

        break;

    }



    case WM_NOTIFY:
    {
        if (pState && pState->hSoundMatrixListWnd && ((NMHDR*)lParam)->hwndFrom == pState->hSoundMatrixListWnd)
        {
            NMHDR* pnmh = (NMHDR*)lParam;
            if (pnmh->code == NM_CLICK || pnmh->code == LVN_ITEMCHANGED || pnmh->code == NM_CELLCLICK)
            {
                POINT ptCursor;
                GetCursorPos(&ptCursor);
                POINT ptClient = ptCursor;
                ScreenToClient(pState->hSoundMatrixListWnd, &ptClient);
                int clickedCol = -1;
                int xAccum = pState->soundMatrixList.GetGutterWidth();
                for (int c = 0; c < 9; ++c)
                {
                    int cw = pState->soundMatrixList.GetColumnWidth(c);
                    if (cw <= 0) break;
                    if (ptClient.x >= xAccum && ptClient.x < xAccum + cw)
                    {
                        clickedCol = c;
                        break;
                    }
                    xAccum += cw;
                }
                NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
                int row = (pnmh->code == NM_CELLCLICK && pCell && pCell->itemIndex >= 0) ? pCell->itemIndex : pState->soundMatrixList.GetSelectedIndex();
                int col = pCell ? pCell->subItemIndex : 0;

                if (row >= 0 && row < (int)pState->displayRows.size())
                {
                    const auto& dRow = pState->displayRows[row];
                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())
                    {
                        auto& u = pState->unitsSoundData[dRow.unitIndex];

                        // In GridView mode:
                        if (pState->matrixViewMode == SoundMatrixViewMode::GridView)
                        {
                            // 1. If parent row and Col 0 clicked: toggle expansion
                            if (dRow.rowType == RSS_ROW_UNIT && col == 0)
                            {
                                u.isExpanded = !u.isExpanded;
                                PopulateSoundMatrix(pState, dRow.unitIndex);
                                return 0;
                            }

                            // 2. If ADD_BUTTON row and cols 4..7 clicked: handle add
                            if (dRow.rowType == RSS_ROW_ADD_BUTTON && col >= 4 && col <= 7)
                            {
                                HandleAddButtonClick(pState, row, col);
                                return 0;
                            }
                        }

                        if (dRow.rowType == RSS_ROW_STREAM)
                        {
                            UpdateInspectorStreamSelection(pState, dRow.unitIndex, dRow.category, dRow.streamIndex);
                        }
                        else
                        {
                            UpdateInspectorSelection(pState, dRow.unitIndex);
                        }
                        InvalidateRect(pState->hWnd, &pState->rcInspectorCard, FALSE);

                        // Auto-scroll canvas to distinct card (only in DistinctCards mode)
                        if (pState->matrixViewMode == SoundMatrixViewMode::DistinctCards)
                        {
                            std::wstring key = u.folder + L"\\" + u.name + u.ext;
                            for (size_t d = 0; d < pState->distinctVehicles.size(); ++d)
                            {
                                if (_wcsicmp(pState->distinctVehicles[d].key.c_str(), key.c_str()) == 0)
                                {
                                    pState->selectedDistinctIndex = d;
                                    int cardTop = 12;
                                    for (size_t cd = 0; cd < d; ++cd)
                                    {
                                        cardTop += GetDistinctCardHeight(pState->distinctVehicles[cd]) + 16;
                                    }
                                    pState->canvasScrollY = cardTop - 12;
                                    if (pState->canvasScrollY < 0) pState->canvasScrollY = 0;
                                    if (pState->canvasScrollY > pState->canvasScrollBar.GetMax())
                                        pState->canvasScrollY = pState->canvasScrollBar.GetMax();
                                    pState->canvasScrollBar.SetPos(pState->canvasScrollY);
                                    if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, FALSE);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            else if (pnmh->code == NM_DBLCLK)
            {
                int sel = pState->soundMatrixList.GetSelectedIndex();
                if (sel >= 0 && sel < (int)pState->displayRows.size())
                {
                    const auto& dRow = pState->displayRows[sel];
                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())
                    {
                        auto& u = pState->unitsSoundData[dRow.unitIndex];
                        if (pState->matrixViewMode == SoundMatrixViewMode::GridView && dRow.rowType == RSS_ROW_UNIT)
                        {
                            u.isExpanded = !u.isExpanded;
                            PopulateSoundMatrix(pState, dRow.unitIndex);
                            return 0;
                        }
                        else if (dRow.rowType == RSS_ROW_STREAM)
                        {
                            const RSSSoundEntry* pEntry = nullptr;
                            if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.wagonSounds.size()) pEntry = &u.wagonSounds[dRow.streamIndex];
                            else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.engineSounds.size()) pEntry = &u.engineSounds[dRow.streamIndex];
                            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.orts3dCabSounds.size()) pEntry = &u.orts3dCabSounds[dRow.streamIndex];
                            else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.insideSounds.size()) pEntry = &u.insideSounds[dRow.streamIndex];

                            if (pEntry && pEntry->existsOnDisk)
                            {
                                OpenFileInDefaultEditor(pState->hWnd, pEntry->fullDiskPath);
                                return 0;
                            }
                        }
                        else
                        {
                            if (u.fileFound && !u.fullPath.empty())
                            {
                                OpenFileInDefaultEditor(pState->hWnd, u.fullPath);
                                return 0;
                            }
                        }
                    }
                }
            }
            else if (pnmh->code == NM_RCELLCLICK)
            {
                POINT ptCursor;
                GetCursorPos(&ptCursor);
                POINT ptClient = ptCursor;
                ScreenToClient(pState->hSoundMatrixListWnd, &ptClient);
                int clickedCol = -1;
                int xAccum = pState->soundMatrixList.GetGutterWidth();
                for (int c = 0; c < 9; ++c)
                {
                    int cw = pState->soundMatrixList.GetColumnWidth(c);
                    if (cw <= 0) break;
                    if (ptClient.x >= xAccum && ptClient.x < xAccum + cw)
                    {
                        clickedCol = c;
                        break;
                    }
                    xAccum += cw;
                }
                NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
                int row = pState->soundMatrixList.GetSelectedIndex();
                POINT pt;
                GetCursorPos(&pt);
                if (pState->matrixViewMode == SoundMatrixViewMode::GridView)
                {
                    ShowSoundMatrixContextMenu(pState, row, clickedCol, pt.x, pt.y);
                    return 0;
                }
                else
                {
                    if (row >= 0 && row < (int)pState->unitsSoundData.size())
                    {
                        const auto& u = pState->unitsSoundData[row];
                        if (u.fileFound && !u.fullPath.empty())
                        {
                            std::wstring exploreCmd = L"/select,\"" + u.fullPath + L"\"";
                            ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
                            return 0;
                        }
                    }
                }
            }
        }
        break;
    }

    case WM_SYSKEYDOWN:

    {

        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (bAlt && (wParam == 'V' || wParam == 'v'))

        {

            App_ShowClipboardContents(hWnd);

            return 0;

        }

        break;

    }



    case WM_KEYDOWN:
    {
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (bCtrl && (wParam == 'Z' || wParam == 'z'))
        {
            if (pState && pState->activeSubTab == 0)
            {
                if (bShift) RSS_PerformRedo(pState);
                else RSS_PerformUndo(pState);
                return 0;
            }
        }
        if (bCtrl && (wParam == 'Y' || wParam == 'y'))
        {
            if (pState && pState->activeSubTab == 0)
            {
                RSS_PerformRedo(pState);
                return 0;
            }
        }

        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (bAlt && (wParam == 'V' || wParam == 'v'))

        {

            App_ShowClipboardContents(hWnd);

            return 0;

        }



        if (wParam == VK_DELETE)
        {
            if (pState && pState->activeSubTab == 0)
            {
                int sel = pState->soundMatrixList.GetSelectedIndex();
                if (sel >= 0 && sel < (int)pState->displayRows.size())
                {
                    const auto& dRow = pState->displayRows[sel];
                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())
                    {
                        auto& u = pState->unitsSoundData[dRow.unitIndex];
                        if (dRow.rowType == RSS_ROW_STREAM)
                        {
                            RSS_PushUndo(pState, L"Delete Sound Stream");
                            if (dRow.streamIndex > 0)
                            {
                                if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.wagonSounds.size())
                                    u.wagonSounds.erase(u.wagonSounds.begin() + dRow.streamIndex);
                                else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.engineSounds.size())
                                    u.engineSounds.erase(u.engineSounds.begin() + dRow.streamIndex);
                                else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.orts3dCabSounds.size())
                                    u.orts3dCabSounds.erase(u.orts3dCabSounds.begin() + dRow.streamIndex);
                                else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.insideSounds.size())
                                    u.insideSounds.erase(u.insideSounds.begin() + dRow.streamIndex);
                            }
                            else if (dRow.streamIndex == 0)
                            {
                                if (dRow.category == L"Wagon") u.wagonSounds.clear();
                                else if (dRow.category == L"Engine") u.engineSounds.clear();
                                else if (dRow.category == L"ORTS3DCab") u.orts3dCabSounds.clear();
                                else if (dRow.category == L"Inside") u.insideSounds.clear();
                            }
                            u.isModified = true;
                            pState->isModified = true;
                            PopulateSoundMatrix(pState, dRow.unitIndex);
                            return 0;
                        }
                    }
                }
            }
        }

        if (bCtrl && (wParam == 'C' || wParam == 'c'))

        {

            if (pState && pState->activeSubTab == 0)

            {

                int sel = pState->soundMatrixList.GetSelectedIndex();

                if (sel >= 0 && sel < (int)pState->displayRows.size())

                {

                    const auto& dRow = pState->displayRows[sel];

                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())

                    {

                        const auto& u = pState->unitsSoundData[dRow.unitIndex];

                        const RSSSoundEntry* pEntry = nullptr;

                        if (dRow.rowType == RSS_ROW_STREAM)

                        {

                            if (dRow.category == L"Wagon" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.wagonSounds.size())

                                pEntry = &u.wagonSounds[dRow.streamIndex];

                            else if (dRow.category == L"Engine" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.engineSounds.size())

                                pEntry = &u.engineSounds[dRow.streamIndex];

                            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.orts3dCabSounds.size())

                                pEntry = &u.orts3dCabSounds[dRow.streamIndex];

                            else if (dRow.category == L"Inside" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.insideSounds.size())

                                pEntry = &u.insideSounds[dRow.streamIndex];

                        }

                        else if (dRow.rowType == RSS_ROW_UNIT)

                        {

                            if (!u.engineSounds.empty()) pEntry = &u.engineSounds[0];

                            else if (!u.wagonSounds.empty()) pEntry = &u.wagonSounds[0];

                            else if (!u.orts3dCabSounds.empty()) pEntry = &u.orts3dCabSounds[0];

                            else if (!u.insideSounds.empty()) pEntry = &u.insideSounds[0];

                        }



                        if (pEntry && !pEntry->smsFileName.empty())

                        {

                            SoundStudio_SetSmsClipboard(*pEntry, u.name, u.folder, u.type);

                            std::wstring rel = pEntry->parentFolder.empty() ? pEntry->smsFileName : (pEntry->parentFolder + L"\\" + pEntry->smsFileName);

                            CopyStringToClipboard(pState->hWnd, rel);

                            return 0;

                        }

                    }

                }

            }

        }

        else if (bCtrl && (wParam == 'V' || wParam == 'v'))

        {

            if (pState && pState->activeSubTab == 0)

            {

                int sel = pState->soundMatrixList.GetSelectedIndex();

                if (sel >= 0 && sel < (int)pState->displayRows.size())

                {

                    const auto& dRow = pState->displayRows[sel];

                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())

                    {

                        auto& u = pState->unitsSoundData[dRow.unitIndex];

                        RSSSoundEntry* pEntry = nullptr;

                        if (dRow.rowType == RSS_ROW_STREAM)

                        {

                            if (dRow.category == L"Wagon" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.wagonSounds.size())

                                pEntry = &u.wagonSounds[dRow.streamIndex];

                            else if (dRow.category == L"Engine" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.engineSounds.size())

                                pEntry = &u.engineSounds[dRow.streamIndex];

                            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.orts3dCabSounds.size())

                                pEntry = &u.orts3dCabSounds[dRow.streamIndex];

                            else if (dRow.category == L"Inside" && dRow.streamIndex >= 0 && dRow.streamIndex < (int)u.insideSounds.size())

                                pEntry = &u.insideSounds[dRow.streamIndex];

                        }



                        if (pEntry)
                        {
                            RSS_PushUndo(pState, L"Paste SMS");
                            const auto& soundClip = SoundStudio_GetSmsClipboard();

                            if (soundClip.hasEntry && !soundClip.entry.smsFileName.empty())

                            {

                                *pEntry = soundClip.entry;

                                pEntry->soundType = dRow.category;

                                u.isModified = true;

                                pState->isModified = true;

                                PopulateSoundMatrix(pState);

                                return 0;

                            }

                            else

                            {

                                std::wstring clipText = GetClipboardText(pState->hWnd);

                                if (!clipText.empty())

                                {

                                    *pEntry = CreateSoundEntryFromText(clipText, dRow.category, pState->basePath);

                                    u.isModified = true;

                                    pState->isModified = true;

                                    PopulateSoundMatrix(pState);

                                    return 0;

                                }

                            }

                        }

                    }

                }

            }

        }

        break;

    }



    case WM_ERASEBKGND:

        return 1;



    case WM_PAINT:

    {

        PAINTSTRUCT ps;

        HDC hDC = BeginPaint(hWnd, &ps);



        RECT rcClient;

        GetClientRect(hWnd, &rcClient);

        int w = rcClient.right - rcClient.left;

        int h = rcClient.bottom - rcClient.top;



        // Fill background canvas behind table card margins

        if (h > 44)

        {

            RECT rcBody = { 0, 44, w, h };

            HBRUSH hbrBody = CreateSolidBrush(UITheme::DarkBackground);

            FillRect(hDC, &rcBody, hbrBody);

            DeleteObject(hbrBody);

        }



        HDC hMemDC = CreateCompatibleDC(hDC);

        HBITMAP hBmp = CreateCompatibleBitmap(hDC, w, 44);

        HGDIOBJ oldBmp = SelectObject(hMemDC, hBmp);



        // Header Background

        RECT rcHdr = { 0, 0, w, 44 };

        HBRUSH hbrHdr = CreateSolidBrush(UITheme::DarkHeaderBackground);

        FillRect(hMemDC, &rcHdr, hbrHdr);

        DeleteObject(hbrHdr);



        // Ensure transparent text rendering on dark header

        SetBkMode(hMemDC, TRANSPARENT);



        // Bottom border line

        HPEN hBorderPen = CreatePen(PS_SOLID, 1, UITheme::DarkHeaderBorder);

        HGDIOBJ oldPen = SelectObject(hMemDC, hBorderPen);

        MoveToEx(hMemDC, 0, 43, NULL);

        LineTo(hMemDC, w, 43);

        SelectObject(hMemDC, oldPen);

        DeleteObject(hBorderPen);



        // Sub-Tab Navigation Pills

        RECT rcTab0 = { 10, 7, 186, 37 };

        RECT rcTab1 = { 192, 7, 355, 37 };

        pState->rcSubTabMatrix = rcTab0;

        pState->rcSubTabEditor = rcTab1;



        auto drawSubTab = [&](const RECT& rcT, const wchar_t* iconGlyph, const std::wstring& txt, bool isCur, bool isHov) {

            COLORREF bg = isCur ? RGB(0, 120, 215) : (isHov ? RGB(45, 48, 58) : RGB(32, 34, 42));

            COLORREF border = isCur ? RGB(0, 160, 255) : RGB(52, 56, 68);

            COLORREF fg = isCur ? RGB(255, 255, 255) : (isHov ? RGB(230, 235, 245) : RGB(170, 180, 195));



            HBRUSH hbr = CreateSolidBrush(bg);

            HPEN hp = CreatePen(PS_SOLID, 1, border);

            HGDIOBJ ob = SelectObject(hMemDC, hbr);

            HGDIOBJ op = SelectObject(hMemDC, hp);

            RoundRect(hMemDC, rcT.left, rcT.top, rcT.right, rcT.bottom, 4, 4);

            SelectObject(hMemDC, ob);

            SelectObject(hMemDC, op);

            DeleteObject(hbr);

            DeleteObject(hp);



            int textLeft = rcT.left + 12;

            if (iconGlyph && pState->hFontIcons)

            {

                RECT rcIcon = { rcT.left + 8, rcT.top, rcT.left + 28, rcT.bottom };

                SelectObject(hMemDC, pState->hFontIcons);

                SetTextColor(hMemDC, fg);

                DrawTextW(hMemDC, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                textLeft = rcT.left + 30;

            }



            RECT rcTxt = { textLeft, rcT.top, rcT.right - 10, rcT.bottom };

            SelectObject(hMemDC, isCur ? pState->hFontBold : pState->hFontBody);

            SetTextColor(hMemDC, fg);

            DrawTextW(hMemDC, txt.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        };



        std::wstring tab0Title = L"Consist Sound Matrix";
        if (pState->isModified) tab0Title += L" \u25cf";

        bool smsDirty = pState->hSmsEditorWnd && SmsEditorView_HasUnsavedChanges(pState->hSmsEditorWnd);
        std::wstring tab1Title = L"SMS Editor / Studio";
        if (smsDirty) tab1Title += L" \u25cf";

        drawSubTab(rcTab0, L"\xE9E9", tab0Title.c_str(), pState->activeSubTab == 0, pState->hoverSubTab == 0);

        drawSubTab(rcTab1, L"\xE8D6", tab1Title.c_str(), pState->activeSubTab == 1, pState->hoverSubTab == 1);

        if (pState->activeSubTab == 0)
        {
            // View Switcher Pills: [ ▦ Grid View ] [ 🗂 Distinct Cards ]
            int toggleX = 364;
            pState->rcBtnViewGrid = { toggleX, 7, toggleX + 96, 37 };
            pState->rcBtnViewCards = { toggleX + 102, 7, toggleX + 224, 37 };

            POINT ptMouse;
            GetCursorPos(&ptMouse);
            ScreenToClient(hWnd, &ptMouse);

            auto drawViewPill = [&](const RECT& rcPill, const wchar_t* iconGlyph, const wchar_t* label, bool isCur) {
                bool isHov = PtInRect(&rcPill, ptMouse);
                COLORREF bg = isCur ? RGB(0, 120, 215) : (isHov ? RGB(45, 48, 58) : RGB(28, 30, 38));
                COLORREF border = isCur ? RGB(0, 160, 255) : RGB(52, 56, 68);
                COLORREF fg = isCur ? RGB(255, 255, 255) : (isHov ? RGB(230, 235, 245) : RGB(170, 180, 195));

                HBRUSH hbr = CreateSolidBrush(bg);
                HPEN hp = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ ob = SelectObject(hMemDC, hbr);
                HGDIOBJ op = SelectObject(hMemDC, hp);
                RoundRect(hMemDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                SelectObject(hMemDC, ob);
                SelectObject(hMemDC, op);
                DeleteObject(hbr);
                DeleteObject(hp);

                int textLeft = rcPill.left + 8;
                if (iconGlyph && pState->hFontIcons)
                {
                    RECT rcIcon = { rcPill.left + 6, rcPill.top, rcPill.left + 24, rcPill.bottom };
                    SelectObject(hMemDC, pState->hFontIcons);
                    SetTextColor(hMemDC, fg);
                    DrawTextW(hMemDC, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    textLeft = rcPill.left + 26;
                }

                SelectObject(hMemDC, pState->hFontBold);
                SetTextColor(hMemDC, fg);
                RECT rcTxt = { textLeft, rcPill.top, rcPill.right - 6, rcPill.bottom };
                DrawTextW(hMemDC, label, -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };

            drawViewPill(pState->rcBtnViewGrid, L"\xE71D", L"Grid View", pState->matrixViewMode == SoundMatrixViewMode::GridView);
            drawViewPill(pState->rcBtnViewCards, L"\xE8EC", L"Distinct Cards", pState->matrixViewMode == SoundMatrixViewMode::DistinctCards);





            // Right side badges & buttons

            int badgeRight = w - 16;

            int badgeY = 11;

            int badgeH = 22;



            auto drawPillBadge = [&](const std::wstring& txt, COLORREF bg, COLORREF border, COLORREF fg) {

                SelectObject(hMemDC, pState->hFontBold);

                SIZE sz;

                GetTextExtentPoint32W(hMemDC, txt.c_str(), (int)txt.length(), &sz);

                int bW = sz.cx + 16;

                int bX = badgeRight - bW;

                RECT rcB = { bX, badgeY, badgeRight, badgeY + badgeH };



                HBRUSH hbr = CreateSolidBrush(bg);

                HPEN hp = CreatePen(PS_SOLID, 1, border);

                HGDIOBJ ob = SelectObject(hMemDC, hbr);

                HGDIOBJ op = SelectObject(hMemDC, hp);

                RoundRect(hMemDC, rcB.left, rcB.top, rcB.right, rcB.bottom, 4, 4);

                SelectObject(hMemDC, ob);

                SelectObject(hMemDC, op);

                DeleteObject(hbr);

                DeleteObject(hp);



                SetTextColor(hMemDC, fg);

                DrawTextW(hMemDC, txt.c_str(), -1, &rcB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                badgeRight = bX - 8;

            };



            // Draw [ 💾 Save Changes ] button

            std::wstring saveLabel = pState->isModified ? L"💾 Save Changes ●" : L"💾 Save Changes";

            SelectObject(hMemDC, pState->hFontBold);

            SIZE szSave;

            GetTextExtentPoint32W(hMemDC, saveLabel.c_str(), (int)saveLabel.length(), &szSave);

            int saveW = szSave.cx + 20;

            int saveX = badgeRight - saveW;

            RECT rcSave = { saveX, badgeY, badgeRight, badgeY + badgeH };

            pState->rcBtnSave = rcSave;



            bool isSaveHov = pState->hoverSaveBtn;

            COLORREF saveBg, saveBorder, saveFg;

            if (pState->isModified)

            {

                saveBg = isSaveHov ? RGB(0, 140, 230) : RGB(0, 110, 195);

                saveBorder = isSaveHov ? RGB(60, 180, 255) : RGB(30, 150, 240);

                saveFg = RGB(255, 255, 255);

            }

            else

            {

                saveBg = isSaveHov ? RGB(55, 60, 70) : RGB(40, 44, 52);

                saveBorder = isSaveHov ? RGB(85, 95, 110) : RGB(65, 72, 85);

                saveFg = isSaveHov ? RGB(220, 225, 235) : RGB(160, 168, 180);

            }



            HBRUSH hbrSave = CreateSolidBrush(saveBg);

            HPEN hpSave = CreatePen(PS_SOLID, 1, saveBorder);

            HGDIOBJ obSave = SelectObject(hMemDC, hbrSave);

            HGDIOBJ opSave = SelectObject(hMemDC, hpSave);

            RoundRect(hMemDC, rcSave.left, rcSave.top, rcSave.right, rcSave.bottom, 4, 4);

            SelectObject(hMemDC, obSave);

            SelectObject(hMemDC, opSave);

            DeleteObject(hbrSave);

            DeleteObject(hpSave);



            SetTextColor(hMemDC, saveFg);

            DrawTextW(hMemDC, saveLabel.c_str(), -1, &rcSave, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            badgeRight = saveX - 8;



            if (pState->totalMissingSounds > 0)

            {

                drawPillBadge(std::to_wstring(pState->totalMissingSounds) + L" Missing Sounds", RGB(55, 20, 20), RGB(160, 45, 45), RGB(255, 95, 95));

            }

            else if (!pState->unitsSoundData.empty())

            {

                drawPillBadge(L"All Sounds Ready", RGB(20, 48, 25), RGB(45, 125, 55), RGB(90, 225, 120));

            }



            if (!pState->unitsSoundData.empty())

            {

                std::wstring uStr = std::to_wstring(pState->unitsSoundData.size()) + L" Units";

                drawPillBadge(uStr, RGB(22, 42, 58), RGB(45, 110, 160), RGB(100, 190, 255));

            }



            // Draw Expand All / Collapse All buttons (available in BOTH Grid View and Distinct Cards)
            const wchar_t* btnLabels[2] = { L"▾ Expand All", L"▸ Collapse All" };
            for (int b = 1; b >= 0; --b)
            {
                SelectObject(hMemDC, pState->hFontBold);
                SIZE sz;
                GetTextExtentPoint32W(hMemDC, btnLabels[b], (int)wcslen(btnLabels[b]), &sz);
                int bW = sz.cx + 14;
                int bX = badgeRight - bW;
                RECT rcB = { bX, badgeY, badgeRight, badgeY + badgeH };
                if (b == 0) pState->rcBtnExpandAll = rcB;
                else pState->rcBtnCollapseAll = rcB;

                bool isHov = (pState->hoverHeaderBtn == b);
                COLORREF bg = isHov ? RGB(60, 60, 70) : RGB(42, 42, 48);
                COLORREF border = isHov ? RGB(100, 100, 115) : RGB(70, 70, 78);
                COLORREF fg = isHov ? RGB(255, 255, 255) : RGB(210, 210, 215);

                HBRUSH hbr = CreateSolidBrush(bg);
                HPEN hp = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ ob = SelectObject(hMemDC, hbr);
                HGDIOBJ op = SelectObject(hMemDC, hp);
                RoundRect(hMemDC, rcB.left, rcB.top, rcB.right, rcB.bottom, 4, 4);
                SelectObject(hMemDC, ob);
                SelectObject(hMemDC, op);
                DeleteObject(hbr);
                DeleteObject(hp);

                SetTextColor(hMemDC, fg);
                DrawTextW(hMemDC, btnLabels[b], -1, &rcB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                badgeRight = bX - 8;
            }

            // Draw [ ↷ Redo ] and [ ↶ Undo ] buttons
            const wchar_t* historyLabels[2] = { L"↶ Undo", L"↷ Redo" };
            for (int h = 1; h >= 0; --h)
            {
                bool canPerform = (h == 0) ? !pState->undoStack.empty() : !pState->redoStack.empty();
                SelectObject(hMemDC, pState->hFontBold);
                SIZE szH;
                GetTextExtentPoint32W(hMemDC, historyLabels[h], (int)wcslen(historyLabels[h]), &szH);
                int hW = szH.cx + 16;
                int hX = badgeRight - hW;
                RECT rcH = { hX, badgeY, badgeRight, badgeY + badgeH };
                if (h == 0) pState->rcBtnUndo = rcH;
                else pState->rcBtnRedo = rcH;

                bool isHov = canPerform && (pState->hoverUndoRedoBtn == (h + 1));
                COLORREF bg = !canPerform ? RGB(30, 32, 38) : (isHov ? RGB(60, 60, 70) : RGB(42, 42, 48));
                COLORREF border = !canPerform ? RGB(45, 48, 56) : (isHov ? RGB(100, 100, 115) : RGB(70, 70, 78));
                COLORREF fg = !canPerform ? RGB(95, 100, 110) : (isHov ? RGB(255, 255, 255) : RGB(210, 210, 215));

                HBRUSH hbr = CreateSolidBrush(bg);
                HPEN hp = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ ob = SelectObject(hMemDC, hbr);
                HGDIOBJ op = SelectObject(hMemDC, hp);
                RoundRect(hMemDC, rcH.left, rcH.top, rcH.right, rcH.bottom, 4, 4);
                SelectObject(hMemDC, ob);
                SelectObject(hMemDC, op);
                DeleteObject(hbr);
                DeleteObject(hp);

                SetTextColor(hMemDC, fg);
                DrawTextW(hMemDC, historyLabels[h], -1, &rcH, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                badgeRight = hX - 8;
            }
        }

        else

        {

            // SMS Editor header label

            SelectObject(hMemDC, pState->hFontBody);

            SetTextColor(hMemDC, RGB(160, 175, 200));

            RECT rcSub = { 370, 4, w - 20, 40 };

            DrawTextW(hMemDC, L"Inspect, Test & Edit Sound Management System (.sms) Streams, Triggers & Curves", -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        }



        BitBlt(hDC, 0, 0, w, 44, hMemDC, 0, 0, SRCCOPY);



        SelectObject(hMemDC, oldBmp);

        DeleteObject(hBmp);

        DeleteDC(hMemDC);



        if (pState->activeSubTab == 0)
        {
            DrawConsistBanner(pState, hDC);
            DrawInspectorCard(pState, hDC);
        }



        EndPaint(hWnd, &ps);

        return 0;

    }



    case WM_DESTROY:

    {

        if (pState)

        {

            RSS_StopAudio();

            if (pState->hFontTitle)  DeleteObject(pState->hFontTitle);

            if (pState->hFontHeader) DeleteObject(pState->hFontHeader);

            if (pState->hFontBody)   DeleteObject(pState->hFontBody);

            if (pState->hFontBold)   DeleteObject(pState->hFontBold);

            if (pState->hFontMono)   DeleteObject(pState->hFontMono);

            if (pState->hFontIcons)  DeleteObject(pState->hFontIcons);

            if (pState->hFontSmallIcons) DeleteObject(pState->hFontSmallIcons);

            delete pState;

            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);

        }

        return 0;

    }

    default:
        break;

    }



    return DefWindowProcW(hWnd, uMsg, wParam, lParam);

}



HWND CreateSoundStudio(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id)

{

    static bool s_registered = false;

    if (!s_registered)

    {

        WNDCLASSEXW wc = { 0 };

        wc.cbSize = sizeof(wc);

        wc.style = CS_HREDRAW | CS_VREDRAW;

        wc.lpfnWndProc = RollingStockStudioProc;

        wc.hInstance = hInstance;

        wc.hCursor = LoadCursor(NULL, IDC_ARROW);

        wc.lpszClassName = RSS_WNDCLASS;

        RegisterClassExW(&wc);

        s_registered = true;

    }



    RollingStockStudioState* pState = new RollingStockStudioState();

    pState->hParent = hParent;

    pState->hInstance = hInstance;



    HWND hWnd = CreateWindowExW(

        0, RSS_WNDCLASS, L"",

        WS_CHILD | WS_CLIPCHILDREN,

        x, y, w, h,

        hParent, (HMENU)(UINT_PTR)id, hInstance, pState

    );



    return hWnd;

}



static std::unordered_map<std::wstring, RSSUnitSoundInfo> s_stockSoundCache;



void SoundStudio_ClearStockSoundCache()

{

    s_stockSoundCache.clear();

}



static void SyncSmsMutatorTargetsIfOpen(const std::vector<RSSUnitSoundInfo>& units)

{

    if (!SmsStreamMutatorDlg_IsOpen()) return;



    std::vector<std::wstring> engList;

    std::vector<std::wstring> wagList;

    std::unordered_set<std::wstring> seenEng;

    std::unordered_set<std::wstring> seenWag;



    for (const auto& u : units)

    {

        bool isEng = (u.type == L"Engine" || _wcsicmp(u.ext.c_str(), L".eng") == 0);

        auto addEntry = [&](const RSSSoundEntry& entry, bool toEng) {

            if (!entry.fullDiskPath.empty() && entry.existsOnDisk)

            {

                std::wstring norm = entry.fullDiskPath;

                std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);

                if (toEng)

                {

                    if (seenEng.find(norm) == seenEng.end())

                    {

                        seenEng.insert(norm);

                        engList.push_back(entry.fullDiskPath);

                    }

                }

                else

                {

                    if (seenWag.find(norm) == seenWag.end())

                    {

                        seenWag.insert(norm);

                        wagList.push_back(entry.fullDiskPath);

                    }

                }

            }

        };



        if (isEng)

        {

            for (const auto& s : u.engineSounds)    addEntry(s, true);

            for (const auto& s : u.orts3dCabSounds) addEntry(s, true);

            for (const auto& s : u.insideSounds)    addEntry(s, true);

            for (const auto& s : u.wagonSounds)     addEntry(s, true);

        }

        else

        {

            for (const auto& s : u.wagonSounds)     addEntry(s, false);

            for (const auto& s : u.insideSounds)    addEntry(s, false);

        }

    }



    SmsStreamMutatorDlg_SetCategorizedTargets(engList, wagList);

}



void SoundStudio_SetConsist(HWND hWnd, const std::wstring& consistName, const std::wstring& basePath, const std::vector<ConsistReader::UnitInfo>& units)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    pState->consistName = consistName;

    pState->basePath = basePath;

    pState->isDirectStockMode = false;

    pState->isDirectSmsMode = false;

    pState->directSmsPath.clear();

    pState->unitsSoundData.clear();



    std::wstring bp = basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';



    for (size_t i = 0; i < units.size(); ++i)

    {

        const auto& u = units[i];

        RSSUnitSoundInfo info;

        info.index = (int)i + 1;

        info.name = u.uid;

        info.folder = u.parentDir;

        info.type = u.isEngine ? L"Engine" : L"Wagon";

        info.ext = u.isEngine ? L".eng" : L".wag";



        std::wstring fullStockFolder = bp + L"TRAINS\\TRAINSET\\" + info.folder;

        info.fullPath = fullStockFolder + L"\\" + info.name + info.ext;



        if (GetFileAttributesW(info.fullPath.c_str()) == INVALID_FILE_ATTRIBUTES)

        {

            // Try alternate extension (.eng vs .wag)

            std::wstring altExt = u.isEngine ? L".wag" : L".eng";

            std::wstring altPath = fullStockFolder + L"\\" + info.name + altExt;

            if (GetFileAttributesW(altPath.c_str()) != INVALID_FILE_ATTRIBUTES)

            {

                info.fullPath = altPath;

                info.ext = altExt;

                info.fileFound = true;

            }

            else

            {

                info.fileFound = false;

            }

        }

        else

        {

            info.fileFound = true;

        }



        if (info.fileFound)

        {

            std::wstring normKey = info.fullPath;

            std::transform(normKey.begin(), normKey.end(), normKey.begin(), ::towlower);

            auto itCache = s_stockSoundCache.find(normKey);

            if (itCache != s_stockSoundCache.end())

            {

                int curIdx = info.index;

                info = itCache->second;

                info.index = curIdx;

            }

            else

            {

                std::unordered_set<std::wstring> visited;

                ParseStockSoundBlocksAndIncludes(info.fullPath, fullStockFolder, bp, info, visited, 0);

                s_stockSoundCache[normKey] = info;

            }

        }

        else

        {

            info.missingCount = 1;

        }



        info.isExpanded = (info.wagonSounds.size() > 1 || info.engineSounds.size() > 1 || info.orts3dCabSounds.size() > 1);

        pState->unitsSoundData.push_back(info);

    }



    // Guard: Do not process or populate matrix for broken consist with missing stock

    bool hasMissingStock = false;

    for (const auto& item : pState->unitsSoundData)

    {

        if (!item.fileFound) { hasMissingStock = true; break; }

    }

    if (hasMissingStock)

    {

        pState->unitsSoundData.clear();

        pState->distinctVehicles.clear();

        pState->soundMatrixList.Clear();

        pState->selectedDistinctIndex = (size_t)-1;

        SelectSoundInInspector(pState, nullptr, nullptr, L"");

        if (pState->hCanvasWnd) InvalidateRect(pState->hCanvasWnd, NULL, TRUE);

        InvalidateRect(pState->hWnd, &pState->rcInspectorCard, TRUE);

        return;

    }



    pState->savedUnitsSoundData = pState->unitsSoundData;
    pState->isModified = false;
    pState->undoStack.clear();
    pState->redoStack.clear();

    PopulateSoundMatrix(pState);



    if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        SmsEditorView_SetConsistContext(pState->hSmsEditorWnd, consistName, basePath, pState->unitsSoundData);

    }



    SyncSmsMutatorTargetsIfOpen(pState->unitsSoundData);

}



void SoundStudio_SetDirectStock(HWND hWnd, const StockItem& item, const std::wstring& basePath)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    pState->isDirectStockMode = true;

    pState->isDirectSmsMode = false;

    pState->directSmsPath.clear();

    pState->directStockItem = item;

    pState->basePath = basePath;

    pState->unitsSoundData.clear();



    std::wstring bp = basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';



    RSSUnitSoundInfo info;

    info.index = 1;

    info.name = item.szFileName;

    info.folder = item.szFolder;

    info.type = item.szCategory.empty() ? (_wcsicmp(item.szExtension.c_str(), L".eng") == 0 ? L"Engine" : L"Wagon") : item.szCategory;

    info.ext = item.szExtension;



    std::wstring fullStockFolder = bp + L"TRAINS\\TRAINSET\\" + info.folder;

    info.fullPath = fullStockFolder + L"\\" + info.name + info.ext;



    if (GetFileAttributesW(info.fullPath.c_str()) != INVALID_FILE_ATTRIBUTES)

    {

        info.fileFound = true;

        std::unordered_set<std::wstring> visited;

        ParseStockSoundBlocksAndIncludes(info.fullPath, fullStockFolder, bp, info, visited, 0);

    }

    else

    {

        info.fileFound = false;

    }



    info.isExpanded = (info.wagonSounds.size() > 1 || info.engineSounds.size() > 1 || info.orts3dCabSounds.size() > 1);

    pState->unitsSoundData.push_back(info);

    pState->savedUnitsSoundData = pState->unitsSoundData;
    pState->isModified = false;
    pState->undoStack.clear();
    pState->redoStack.clear();

    PopulateSoundMatrix(pState);



    if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        RSSUnitSoundInfo uInfo;

        if (!pState->unitsSoundData.empty()) uInfo = pState->unitsSoundData[0];

        SmsEditorView_SetDirectStockContext(pState->hSmsEditorWnd, item, basePath, uInfo);

    }



    SyncSmsMutatorTargetsIfOpen(pState->unitsSoundData);

}



void SoundStudio_SetDirectSms(HWND hWnd, const std::wstring& smsFilePath, const std::wstring& basePath)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    pState->isDirectSmsMode = true;

    pState->isDirectStockMode = false;

    pState->directSmsPath = smsFilePath;

    pState->basePath = basePath;

    pState->unitsSoundData.clear();



    size_t lastSlash = smsFilePath.find_last_of(L"\\/");

    std::wstring fileName = (lastSlash != std::wstring::npos) ? smsFilePath.substr(lastSlash + 1) : smsFilePath;

    std::wstring parentDir = (lastSlash != std::wstring::npos) ? smsFilePath.substr(0, lastSlash) : L"";

    size_t secondSlash = parentDir.find_last_of(L"\\/");

    std::wstring displayFolder = (secondSlash != std::wstring::npos) ? parentDir.substr(secondSlash + 1) : parentDir;



    RSSUnitSoundInfo info;

    info.index = 1;

    info.name = fileName;

    info.folder = displayFolder;

    info.type = L"Direct SMS";

    info.ext = L".sms";

    info.fullPath = smsFilePath;

    info.fileFound = (GetFileAttributesW(smsFilePath.c_str()) != INVALID_FILE_ATTRIBUTES);



    RSSSoundEntry entry;

    entry.soundType = L"Direct SMS";

    entry.smsFileName = fileName;

    entry.parentFolder = displayFolder;

    entry.fullDiskPath = smsFilePath;

    entry.rawToken = fileName;

    entry.existsOnDisk = info.fileFound;

    info.engineSounds.push_back(entry);



    pState->unitsSoundData.push_back(info);

    pState->savedUnitsSoundData = pState->unitsSoundData;
    pState->isModified = false;
    pState->undoStack.clear();
    pState->redoStack.clear();

    PopulateSoundMatrix(pState);



    // Switch to SubTab 1 (SMS Editor Studio)

    RSS_StopAudio();

    pState->isAudioPlaying = false;

    pState->activeSubTab = 1;

    RepositionStudioControls(pState);



    if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        SmsEditorView_OpenSmsFile(pState->hSmsEditorWnd, smsFilePath, basePath);

    }

    InvalidateRect(hWnd, NULL, TRUE);



    if (pState->hParent && IsWindow(pState->hParent))

    {

        SendMessage(pState->hParent, WM_SOUNDSTUDIO_SUBTABCHANGED, (WPARAM)1, (LPARAM)hWnd);

    }



    if (SmsStreamMutatorDlg_IsOpen() && !smsFilePath.empty())

    {

        SmsStreamMutatorDlg_SetTargets({ smsFilePath });

    }

}



void SoundStudio_SetDarkMode(HWND hWnd, BOOL bDark)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;

    pState->bDarkMode = bDark;

    if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        SmsEditorView_SetDarkMode(pState->hSmsEditorWnd, bDark);

    }

    InvalidateRect(hWnd, NULL, TRUE);

}



void SoundStudio_SwitchSubTab(HWND hWnd, int subTab)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    if (pState->activeSubTab != subTab)

    {

        RSS_StopAudio();

        pState->isAudioPlaying = false;

    }



    pState->activeSubTab = subTab;

    RepositionStudioControls(pState);



    if (subTab == 1 && pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        if (pState->isDirectSmsMode)

        {

            if (!pState->directSmsPath.empty())

            {

                SmsEditorView_OpenSmsFile(pState->hSmsEditorWnd, pState->directSmsPath, pState->basePath);

            }

        }

        else if (pState->isDirectStockMode)

        {

            RSSUnitSoundInfo uInfo;

            if (!pState->unitsSoundData.empty()) uInfo = pState->unitsSoundData[0];

            SmsEditorView_SetDirectStockContext(pState->hSmsEditorWnd, pState->directStockItem, pState->basePath, uInfo);

        }

        else

        {

            SmsEditorView_SetConsistContext(pState->hSmsEditorWnd, pState->consistName, pState->basePath, pState->unitsSoundData);

        }

    }

    InvalidateRect(hWnd, NULL, FALSE);



    if (pState->hParent && IsWindow(pState->hParent))

    {

        SendMessage(pState->hParent, WM_SOUNDSTUDIO_SUBTABCHANGED, (WPARAM)subTab, (LPARAM)hWnd);

    }

}



int SoundStudio_GetActiveSubTab(HWND hWnd)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    return pState ? pState->activeSubTab : 0;

}



void SoundStudio_OpenSmsInEditor(HWND hWnd, const std::wstring& smsFilePath)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    SoundStudio_SwitchSubTab(hWnd, 1);

    if (pState->hSmsEditorWnd && IsWindow(pState->hSmsEditorWnd))

    {

        SmsEditorView_OpenSmsFile(pState->hSmsEditorWnd, smsFilePath, pState->basePath);

    }

}



void SoundStudio_SetVisible(HWND hWnd, bool bVisible)

{

    if (!hWnd) return;

    ShowWindow(hWnd, bVisible ? SW_SHOW : SW_HIDE);

}



void SoundStudio_Resize(HWND hWnd, int x, int y, int w, int h)

{

    if (!hWnd) return;

    SetWindowPos(hWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (pState) RepositionStudioControls(pState);

}



bool SoundStudio_HandleDragHover(HWND hWnd, POINT ptScreen, const ScannedSoundItem* pItem, std::wstring& outActionText)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return false;



    if (pState->activeSubTab != 0)

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    RECT rcStudio;

    GetWindowRect(hWnd, &rcStudio);

    if (!PtInRect(&rcStudio, ptScreen))

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    std::wstring smsName = pItem ? pItem->szFileName : L"sound file";



    // 1. Check if hovering over the Right Master Cards Studio Canvas

    if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))

    {

        RECT rcCanvas;

        GetWindowRect(pState->hCanvasWnd, &rcCanvas);

        if (PtInRect(&rcCanvas, ptScreen))

        {

            POINT ptCanvas = ptScreen;

            ScreenToClient(pState->hCanvasWnd, &ptCanvas);



            for (const auto& hit : pState->canvasHitRegions)

            {

                if (PtInRect(&hit.rc, ptCanvas))

                {

                    if (hit.distinctIndex < pState->distinctVehicles.size())

                    {

                        const auto& dv = pState->distinctVehicles[hit.distinctIndex];

                        bool isValid = true;

                        std::wstring validationReason = L"";



                        if (hit.slotType == RSSSlotHitInfo::WagonInside && !dv.hasInsideBlock)

                        {

                            isValid = false;

                            validationReason = L"No Inside View block defined in " + dv.name;

                        }

                        else if (hit.slotType == RSSSlotHitInfo::WagonOrts3D && !dv.hasORTS3DCabBlock)

                        {

                            isValid = false;

                            validationReason = L"No ORTS 3D Cab block defined in " + dv.name;

                        }

                        else if (hit.slotType == RSSSlotHitInfo::EngineCab && !dv.isEngine)

                        {

                            isValid = false;

                            validationReason = L"Wagons do not support Cab sounds";

                        }



                        if (isValid)

                        {

                            std::wstring slotName = (hit.slotType == RSSSlotHitInfo::WagonExterior) ? L"Wagon Exterior" :

                                                    (hit.slotType == RSSSlotHitInfo::WagonInside) ? L"Inside View" :

                                                    (hit.slotType == RSSSlotHitInfo::WagonOrts3D) ? L"ORTS 3D Cab" : L"Cab Sound";



                            if (hit.isAddStreamBtn)

                            {

                                outActionText = L"+ Add Additional SMS to " + slotName + L": " + smsName;

                            }

                            else if (hit.streamIndex >= 0)

                            {

                                outActionText = L"Replace " + slotName + L" (Stream #" + std::to_wstring(hit.streamIndex + 1) + L"): " + smsName;

                            }

                            else

                            {

                                outActionText = L"+ Assign " + slotName + L" SMS: " + smsName;

                            }

                        }

                        else

                        {

                            outActionText = validationReason;

                        }



                        if (pState->dragHoverDistinctIndex != (int)hit.distinctIndex || pState->dragHoverSlotType != hit.slotType ||

                            pState->dragHoverStreamIndex != hit.streamIndex || pState->dragHoverIsAddStream != hit.isAddStreamBtn)

                        {

                            pState->dragHoverDistinctIndex = (int)hit.distinctIndex;

                            pState->dragHoverSlotType = hit.slotType;

                            pState->dragHoverStreamIndex = hit.streamIndex;

                            pState->dragHoverIsAddStream = hit.isAddStreamBtn;

                            InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

                        }

                        return isValid;

                    }

                }

            }



            SoundStudio_ClearDragHover(hWnd);

            outActionText = L"Drop onto a sound slot card to assign SMS";

            return false;

        }

    }



    // 2. Check if hovering over Left Consist Rake Navigator

    if (pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))

    {

        RECT rcList;

        GetWindowRect(pState->hSoundMatrixListWnd, &rcList);

        if (PtInRect(&rcList, ptScreen))

        {

            POINT ptList = ptScreen;

            ScreenToClient(pState->hSoundMatrixListWnd, &ptList);

            int hoverRow = pState->soundMatrixList.GetItemIndexFromPoint(ptList);
            int hoverUnit = -1;
            if (hoverRow >= 0 && hoverRow < (int)pState->displayRows.size())
                hoverUnit = pState->displayRows[hoverRow].unitIndex;

            if (hoverUnit >= 0 && hoverUnit < (int)pState->unitsSoundData.size())
            {
                const auto& u = pState->unitsSoundData[hoverUnit];

                outActionText = L"Assign SMS to " + u.name + L" (Wagon Exterior)";

                return true;

            }

        }

    }



    SoundStudio_ClearDragHover(hWnd);

    outActionText = L"";

    return false;

}



void SoundStudio_ClearDragHover(HWND hWnd)

{

    if (!hWnd || !IsWindow(hWnd)) return;

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return;



    if (pState->dragHoverDistinctIndex != -1 || pState->dragHoverSlotType != RSSSlotHitInfo::None ||

        pState->dragHoverStreamIndex != -1 || pState->dragHoverIsAddStream)

    {

        pState->dragHoverDistinctIndex = -1;

        pState->dragHoverSlotType = RSSSlotHitInfo::None;

        pState->dragHoverStreamIndex = -1;

        pState->dragHoverIsAddStream = false;

        if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))

        {

            InvalidateRect(pState->hCanvasWnd, NULL, FALSE);

        }

    }

}



bool SoundStudio_HandleDrop(HWND hWnd, POINT ptScreen, const ScannedSoundItem& item)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState || pState->activeSubTab != 0) return false;



    RECT rcStudio;

    GetWindowRect(hWnd, &rcStudio);

    if (!PtInRect(&rcStudio, ptScreen))

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    std::wstring bp = pState->basePath;

    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';

    std::wstring trainsetDir = bp + L"TRAINS\\TRAINSET\\";

    if (!item.szFullPath.empty() && _wcsnicmp(item.szFullPath.c_str(), trainsetDir.c_str(), trainsetDir.length()) != 0)

    {

        SoundStudio_ClearDragHover(hWnd);

        ShowModernMessageBox(

            pState->hWnd,

            L"Invalid Sound File Location:\r\n\r\nIn Open Rails and MSTS, all sound (.sms) files must reside inside the TRAINS\\TRAINSET directory.\r\n\r\nSound files outside TRAINS\\TRAINSET cannot be referenced or loaded by the simulator. Please copy or move this soundset into your TRAINS\\TRAINSET folder before adding it.",

            L"Invalid Sound Location",

            MB_OK | MB_ICONERROR

        );

        return false;

    }



    size_t targetDistinctIdx = (size_t)-1;

    RSSSlotHitInfo::SlotType targetSlot = RSSSlotHitInfo::None;

    int targetStreamIdx = pState->dragHoverStreamIndex;

    bool isAddStream = pState->dragHoverIsAddStream;



    if (pState->dragHoverDistinctIndex >= 0 && (size_t)pState->dragHoverDistinctIndex < pState->distinctVehicles.size() &&

        pState->dragHoverSlotType != RSSSlotHitInfo::None)

    {

        targetDistinctIdx = (size_t)pState->dragHoverDistinctIndex;

        targetSlot = pState->dragHoverSlotType;

    }

    else if (pState->hCanvasWnd && IsWindow(pState->hCanvasWnd))

    {

        POINT ptCanvas = ptScreen;

        ScreenToClient(pState->hCanvasWnd, &ptCanvas);

        for (const auto& hit : pState->canvasHitRegions)

        {

            if (PtInRect(&hit.rc, ptCanvas) && hit.distinctIndex < pState->distinctVehicles.size())

            {

                targetDistinctIdx = hit.distinctIndex;

                targetSlot = hit.slotType;

                targetStreamIdx = hit.streamIndex;

                isAddStream = hit.isAddStreamBtn;

                break;

            }

        }

    }



    // GridView drop handler: drop onto table cells or Add SMS buttons
    if (pState->matrixViewMode == SoundMatrixViewMode::GridView && pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))
    {
        POINT ptList = ptScreen;
        ScreenToClient(pState->hSoundMatrixListWnd, &ptList);
        int hitRow = pState->soundMatrixList.GetItemIndexFromPoint(ptList);
        if (hitRow >= 0 && hitRow < (int)pState->displayRows.size())
        {
            const auto& dRow = pState->displayRows[hitRow];
            if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->unitsSoundData.size())
            {
                auto& u = pState->unitsSoundData[dRow.unitIndex];

                std::wstring dropCat = L"Wagon";
                if (dRow.rowType == RSS_ROW_STREAM)
                {
                    dropCat = dRow.category;
                }
                else
                {
                    int curX = 0;
                    int hitCol = -1;
                    int colCount = 9;
                    for (int c = 0; c < colCount; ++c)
                    {
                        int w = pState->soundMatrixList.GetColumnWidth(c);
                        if (ptList.x >= curX && ptList.x < curX + w)
                        {
                            hitCol = c;
                            break;
                        }
                        curX += w;
                    }
                    if (hitCol == 5) dropCat = L"Engine";
                    else if (hitCol == 6) dropCat = L"ORTS3DCab";
                    else if (hitCol == 7) dropCat = L"Inside";
                    else dropCat = L"Wagon";
                }

                if ((dropCat == L"Engine" || dropCat == L"ORTS3DCab") && u.type != L"Engine")
                {
                    SoundStudio_ClearDragHover(hWnd);
                    return false;
                }

                std::vector<RSSSoundEntry>* pTargetList = nullptr;
                if (dropCat == L"Engine") pTargetList = &u.engineSounds;
                else if (dropCat == L"ORTS3DCab") pTargetList = &u.orts3dCabSounds;
                else if (dropCat == L"Inside") pTargetList = &u.insideSounds;
                else pTargetList = &u.wagonSounds;

                if (pTargetList)
                {
                    // Drag and drop onto existing sound file or Add button APPENDS the SMS (does not replace)
                    if (!pTargetList->empty() && !s_bSuppressMultiSmsWarning)
                    {
                        bool bDontShowAgain = false;
                        ShowModernMessageBoxWithCheckbox(
                            pState->hWnd,
                            L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",
                            L"Open Rails Compatibility Notice",
                            L"Do not show this warning again",
                            &bDontShowAgain,
                            MB_OK | MB_ICONINFORMATION
                        );
                        if (bDontShowAgain)
                        {
                            s_bSuppressMultiSmsWarning = true;
                        }
                    }

                    RSS_PushUndo(pState, L"Assign Sound to Unit");
                    RSSSoundEntry newEntry = CreateSoundEntryFromScanned(item, dropCat, pState->basePath);
                    pTargetList->push_back(newEntry);
                    u.isExpanded = true;
                    u.isModified = true;
                    pState->isModified = true;

                    PopulateSoundMatrix(pState, dRow.unitIndex);
                    SoundStudio_ClearDragHover(hWnd);
                    return true;
                }
            }
        }
    }

    // Fallback: dropped onto left consist rake navigator

    if (targetDistinctIdx == (size_t)-1 && pState->hSoundMatrixListWnd && IsWindow(pState->hSoundMatrixListWnd))

    {

        POINT ptList = ptScreen;

        ScreenToClient(pState->hSoundMatrixListWnd, &ptList);

        int hoverRow = pState->soundMatrixList.GetItemIndexFromPoint(ptList);
        int hoverUnit = -1;
        if (hoverRow >= 0 && hoverRow < (int)pState->displayRows.size())
            hoverUnit = pState->displayRows[hoverRow].unitIndex;

        if (hoverUnit >= 0 && hoverUnit < (int)pState->unitsSoundData.size())
        {
            const auto& u = pState->unitsSoundData[hoverUnit];

            std::wstring key = u.folder + L"\\" + u.name + u.ext;

            for (size_t d = 0; d < pState->distinctVehicles.size(); ++d)

            {

                if (_wcsicmp(pState->distinctVehicles[d].key.c_str(), key.c_str()) == 0)

                {

                    targetDistinctIdx = d;

                    targetSlot = RSSSlotHitInfo::WagonExterior;

                    targetStreamIdx = 0;

                    isAddStream = false;

                    break;

                }

            }

        }

    }



    if (targetDistinctIdx == (size_t)-1 || targetDistinctIdx >= pState->distinctVehicles.size())

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    auto& dv = pState->distinctVehicles[targetDistinctIdx];

    std::wstring category = L"Wagon";

    if (targetSlot == RSSSlotHitInfo::WagonInside) category = L"Inside";

    else if (targetSlot == RSSSlotHitInfo::WagonOrts3D) category = L"ORTS3DCab";

    else if (targetSlot == RSSSlotHitInfo::EngineCab) category = L"Engine";



    if (targetSlot == RSSSlotHitInfo::EngineCab && !dv.isEngine)

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    if (targetSlot == RSSSlotHitInfo::WagonOrts3D && !dv.hasORTS3DCabBlock)

    {

        SoundStudio_ClearDragHover(hWnd);

        ShowModernMessageBox(

            pState->hWnd,

            L"This rolling stock does not contain or define an ORTS 3D Cab View block (ORTS3DCab / ORTS3DCabFile).\r\n\r\nYou cannot add 3D cab sound without an ORTS 3D Cab definition.",

            L"ORTS 3D Cab View Not Defined",

            MB_OK | MB_ICONWARNING

        );

        return false;

    }



    if (targetSlot == RSSSlotHitInfo::WagonInside && !dv.hasInsideBlock)

    {

        SoundStudio_ClearDragHover(hWnd);

        ShowModernMessageBox(

            pState->hWnd,

            L"This rolling stock does not contain or define an Inside View block (passenger camera position).\r\n\r\nYou cannot add inside sound without an Inside camera definition.",

            L"Inside View Not Defined",

            MB_OK | MB_ICONWARNING

        );

        return false;

    }



    std::vector<RSSSoundEntry>* pTargetList = nullptr;

    if (targetSlot == RSSSlotHitInfo::WagonExterior) pTargetList = &dv.wagonSounds;

    else if (targetSlot == RSSSlotHitInfo::WagonInside) pTargetList = &dv.insideSounds;

    else if (targetSlot == RSSSlotHitInfo::WagonOrts3D) pTargetList = &dv.orts3dCabSounds;

    else if (targetSlot == RSSSlotHitInfo::EngineCab) pTargetList = &dv.engineSounds;



    if (!pTargetList)

    {

        SoundStudio_ClearDragHover(hWnd);

        return false;

    }



    RSS_PushUndo(pState, L"Assign Sound to Distinct Vehicle");
    RSSSoundEntry newEntry = CreateSoundEntryFromScanned(item, category, pState->basePath);



    if (isAddStream)

    {

        // Dropped on "+ Add Additional Stream"

        if (!pTargetList->empty())

        {

            if (!s_bSuppressMultiSmsWarning)

            {

                bool bDontShowAgain = false;

                ShowModernMessageBoxWithCheckbox(

                    pState->hWnd,

                    L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",

                    L"Open Rails Compatibility Notice",

                    L"Do not show this warning again",

                    &bDontShowAgain,

                    MB_OK | MB_ICONINFORMATION

                );

                if (bDontShowAgain)

                {

                    s_bSuppressMultiSmsWarning = true;

                }

            }

        }

        pTargetList->push_back(newEntry);

    }

    else if (targetStreamIdx >= 0 && (size_t)targetStreamIdx < pTargetList->size())

    {

        // Replace targeted stream directly

        (*pTargetList)[targetStreamIdx] = newEntry;

    }

    else

    {

        // Empty slot

        if (pTargetList->empty()) pTargetList->push_back(newEntry);

        else (*pTargetList)[0] = newEntry;

    }



    dv.isModified = true;

    pState->isModified = true;

    SyncDistinctVehicleToUnits(pState, targetDistinctIdx);

    SoundStudio_ClearDragHover(hWnd);

    PopulateSoundMatrix(pState);

    return true;

}



HWND SoundStudio_GetListHwnd(HWND hWnd)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    return pState ? pState->hSoundMatrixListWnd : NULL;

}



bool SoundStudio_SaveUnitChanges(HWND hWnd, int unitIndex)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState || unitIndex < 0 || unitIndex >= (int)pState->unitsSoundData.size()) return false;



    auto& u = pState->unitsSoundData[unitIndex];

    if (!u.fileFound || u.fullPath.empty())

    {

        ShowModernMessageBox(hWnd, L"Cannot save: target stock file does not exist on disk.", L"Save Error", MB_OK | MB_ICONERROR);

        return false;

    }



    IncludeSoundAction chosenAction = IncludeSoundAction::PromptUser;

    bool applyToAll = false;

    bool success = UpdateUnitSoundInStockFile(u, pState->basePath, hWnd, IncludeSoundAction::PromptUser, &chosenAction, &applyToAll);

    if (chosenAction == IncludeSoundAction::Cancel)

    {

        return false;

    }



    if (success)

    {

        if (unitIndex >= 0 && unitIndex < (int)pState->savedUnitsSoundData.size())
        {
            pState->savedUnitsSoundData[unitIndex] = pState->unitsSoundData[unitIndex];
        }
        PopulateSoundMatrix(pState);

        std::wstring msg = L"Successfully saved sound stream changes to:\r\n" + u.fullPath + L"\r\n\r\nBackup created at:\r\n" + u.fullPath + L".TSCB_BACKUP";

        ShowModernMessageBox(hWnd, msg.c_str(), L"Sound Configuration Saved", MB_OK | MB_ICONINFORMATION);

    }

    else

    {

        std::wstring msg = L"Failed to update sound definitions in:\r\n" + u.fullPath + L"\r\n\r\nPlease check file permissions or if the file is locked by another process.";

        ShowModernMessageBox(hWnd, msg.c_str(), L"Save Failed", MB_OK | MB_ICONERROR);

    }

    return success;

}



bool SoundStudio_SaveAllChanges(HWND hWnd)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return false;



    int modifiedCount = 0;

    int successCount = 0;

    std::wstring errorDetails;



    IncludeSoundAction batchIncAction = IncludeSoundAction::PromptUser;

    bool batchApplyToAll = false;



    for (size_t i = 0; i < pState->unitsSoundData.size(); ++i)

    {

        auto& u = pState->unitsSoundData[i];

        if (u.isModified)

        {

            modifiedCount++;

            if (UpdateUnitSoundInStockFile(u, pState->basePath, hWnd, batchIncAction, &batchIncAction, &batchApplyToAll))

            {

                successCount++;

            }

            else if (batchIncAction == IncludeSoundAction::Cancel)

            {

                break;

            }

            else

            {

                if (!errorDetails.empty()) errorDetails += L"\r\n";

                errorDetails += u.name + u.ext;

            }

        }

    }



    PopulateSoundMatrix(pState);



    if (modifiedCount == 0)

    {

        ShowModernMessageBox(hWnd, L"No unsaved sound changes to save.", L"Sound Studio", MB_OK | MB_ICONINFORMATION);

        return true;

    }



    if (batchIncAction == IncludeSoundAction::Cancel)

    {

        ShowModernMessageBox(hWnd, L"Save operation cancelled by user.", L"Sound Studio", MB_OK | MB_ICONINFORMATION);

        return false;

    }



    if (successCount == modifiedCount)

    {

        pState->savedUnitsSoundData = pState->unitsSoundData;
        pState->isModified = false;
        PopulateSoundMatrix(pState);

        std::wstring msg = L"Successfully saved sound configuration changes to all " + std::to_wstring(successCount) + L" modified rolling stock file(s).\r\nBackups (.TSCB_BACKUP) were created safely.";

        ShowModernMessageBox(hWnd, msg.c_str(), L"All Sound Changes Saved", MB_OK | MB_ICONINFORMATION);

        return true;

    }

    else

    {

        std::wstring msg = L"Saved " + std::to_wstring(successCount) + L" of " + std::to_wstring(modifiedCount) + L" files.\r\nFailed to save the following files:\r\n" + errorDetails;

        ShowModernMessageBox(hWnd, msg.c_str(), L"Save Incomplete", MB_OK | MB_ICONWARNING);

        return false;

    }

}



bool SoundStudio_HasUnsavedChanges(HWND hWnd)

{

    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    if (!pState) return false;

    if (pState->isModified) return true;

    if (pState->hSmsEditorWnd && SmsEditorView_HasUnsavedChanges(pState->hSmsEditorWnd)) return true;

    return false;

}



std::vector<std::wstring> SoundStudio_GetDirtyUnitNames(HWND hWnd)
{
    std::vector<std::wstring> dirtyUnits;
    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return dirtyUnits;
    for (const auto& u : pState->unitsSoundData)
    {
        if (u.isModified)
        {
            dirtyUnits.push_back(u.name + u.ext);
        }
    }
    return dirtyUnits;
}

std::wstring SoundStudio_GetDirtySmsFile(HWND hWnd)
{
    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState || !pState->hSmsEditorWnd) return L"";
    return SmsEditorView_GetCurrentFilePath(pState->hSmsEditorWnd);
}

bool SoundStudio_SaveCurrentSms(HWND hWnd)
{
    RollingStockStudioState* pState = (RollingStockStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState || !pState->hSmsEditorWnd) return false;
    return SmsEditorView_SaveCurrentFile(pState->hSmsEditorWnd);
}

bool SoundStudio_ParseUnitSound(const std::wstring& filePath, const std::wstring& stockFolder, const std::wstring& basePath, RSSUnitSoundInfo& outInfo)

{

    if (filePath.empty() || GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES)

    {

        outInfo.fileFound = false;

        return false;

    }

    std::wstring normKey = filePath;

    std::transform(normKey.begin(), normKey.end(), normKey.begin(), ::towlower);

    auto it = s_stockSoundCache.find(normKey);

    if (it != s_stockSoundCache.end())

    {

        outInfo = it->second;

        return true;

    }



    outInfo.fullPath = filePath;

    outInfo.fileFound = true;

    std::unordered_set<std::wstring> visited;

    ParseStockSoundBlocksAndIncludes(filePath, stockFolder, basePath, outInfo, visited, 0);

    s_stockSoundCache[normKey] = outInfo;

    return true;

}



bool SoundStudio_UpdateUnitSoundInStockFile(

    RSSUnitSoundInfo& u,

    const std::wstring& basePath,

    HWND hWndParent,

    IncludeSoundAction defaultIncAction,

    IncludeSoundAction* pOutChosenAction,

    bool* pOutApplyToAll)

{

    return UpdateUnitSoundInStockFile(u, basePath, hWndParent, defaultIncAction, pOutChosenAction, pOutApplyToAll);

}

