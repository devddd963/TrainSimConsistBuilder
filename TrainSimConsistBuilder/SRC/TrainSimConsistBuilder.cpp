#include <unordered_map>
enum ActivePane {
    PANE_CONSIST,
    PANE_STOCK,
    PANE_WORKSPACE
};
ActivePane g_ActivePane = PANE_CONSIST;

#include "framework.h"
#include "TrainSimConsistBuilder.h"
#include "../UI/UITheme.h"
#include "../UI/CustomTreeView.h"
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <windowsx.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include "TrainConfig.h"
#include "../../TSCBResources/TSCBResources.h"
#pragma comment(lib, "shlwapi.lib")

// Global Variables
HINSTANCE hInst;
HWND hTabControl = NULL;
HWND g_hCustomTitleBar = NULL;
HWND g_hNavToolbar = NULL;
HWND g_hCommandBar = NULL;
HWND g_hConsistHeader = NULL;
HWND g_hConsistList = NULL;
HWND g_hStockHeader = NULL;
HWND g_hCategoryTree = NULL;
HWND g_hAssetList = NULL;
HWND g_hEditorPane = NULL;
HWND g_hWorkspaceHeader = NULL;
HFONT hUIFont = NULL;
static HFONT g_hSectionFont = NULL;

enum class StockViewMode {
    Single = 0,
    Dual = 1,
    Triple = 2
};
static StockViewMode g_StockViewMode = StockViewMode::Single;

enum class StockCategoryFilter {
    AllStock = 0,
    AllEngines = 1,
    Diesel = 2,
    Electric = 3,
    Steam = 4,
    Control = 5,
    AllWagons = 6,
    Passenger = 7,
    Freight = 8,
    Tender = 9
};

static const wchar_t* GetCategoryFilterLabel(StockCategoryFilter cat)
{
    switch (cat)
    {
    case StockCategoryFilter::AllEngines: return L"All Engines";
    case StockCategoryFilter::Diesel:     return L"Diesel Engines";
    case StockCategoryFilter::Electric:   return L"Electric Engines";
    case StockCategoryFilter::Steam:      return L"Steam Engines";
    case StockCategoryFilter::Control:    return L"Control / Cab";
    case StockCategoryFilter::AllWagons:  return L"All Wagons";
    case StockCategoryFilter::Passenger:  return L"Passenger Wagons";
    case StockCategoryFilter::Freight:    return L"Freight Wagons";
    case StockCategoryFilter::Tender:     return L"Tenders";
    case StockCategoryFilter::AllStock:   return L"All Stock";
    default:                              return L"All Stock";
    }
}

static StockCategoryFilter g_PaneCategory[3] = {
    StockCategoryFilter::AllEngines,
    StockCategoryFilter::Passenger,
    StockCategoryFilter::Freight
};

static std::wstring g_szPaneSearchQuery[3] = { L"", L"", L"" };

std::vector<size_t> g_FilteredStockIndicesPane[3];
#define g_FilteredStockIndices (g_FilteredStockIndicesPane[0])

// Tree Node Handles
CustomTreeNode* g_pNodeEngines = nullptr;
CustomTreeNode* g_pNodeWagons = nullptr;
CustomTreeNode* g_pNodeEnginesAll = nullptr;
CustomTreeNode* g_pNodeWagonsAll = nullptr;
CustomTreeNode* g_pNodeDiesel = nullptr;
CustomTreeNode* g_pNodeElectric = nullptr;
CustomTreeNode* g_pNodeSteam = nullptr;
CustomTreeNode* g_pNodeControl = nullptr;

CustomTreeNode* g_pNodePassenger = nullptr;
CustomTreeNode* g_pNodeFreight = nullptr;
CustomTreeNode* g_pNodeTender = nullptr;

#include "AssetsParser.h"
#include "../UI/FilterPopup.h"
#include "../UI/ModernMessageBox.h"
#include "../UI/VisualConsistView.h"
#include "../UI/FluentDragGhost.h"
#include "../UI/ModernContextMenu.h"
#include "../UI/BatchConsistGenerationWizardDlg.h"
#include "../UI/PoolManagerDlg.h"
#include "PoolManager.h"
#include "../UI/BatchConsistGeneratorDlg.h"
#include "../UI/PoolMutatorDlg.h"
#include "PoolMutator.h"
#include "../UI/CustomTitleBar.h"
#include "../UI/StockInfoDlg.h"
#include "../UI/3D-VisualStudio.h"
#include "../UI/UnitPreviewCard.h"
#include "../UI/TrainConfigStudioDlg.h"
#include "StockSpecReader.h"

LRESULT CALLBACK TabSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
#include "../UI/CustomListControl.h"
#include "ConsistReader.h"
#include "ConsistWriter.h"
#include "ActivityConsistReader.h"
#include "ActivityConsistWriter.h"
#include "Updater.h"
#include "AppLogging.h"
#include "DatabaseManager.h"
CustomListControl g_ConsistList;
CustomListControl g_AssetList;
CustomListControl g_AssetList2;
CustomListControl g_AssetList3;
CustomListControl g_EditorUnitList;
CustomTreeView g_RouteTreeView;
CustomTreeView g_CategoryTreeView;

HWND g_hAssetList2 = NULL;
HWND g_hAssetList3 = NULL;
HWND g_hPaneCatBtn[3] = { NULL, NULL, NULL };
HWND g_hPaneSearchEdit[3] = { NULL, NULL, NULL };
void PopulateAssetGridPane(int paneIdx, CustomTreeNode* hSelected = nullptr);

static ActivityConsistReader::ActivityData g_CurrentActivityData;
static std::wstring g_CurrentActivityFilePath;
static int g_CurrentActivityConsistIndex = -1;

HWND g_hLabelFileName   = NULL;  // caption: "File Name (.con)"
HWND g_hEditFileName    = NULL;  // edit field for physical on-disk .con filename
HWND g_hLabelTrainName   = NULL;
HWND g_hEditTrainName    = NULL;
HWND g_hLabelMaxVelocity = NULL;
HWND g_hEditMaxVelocity  = NULL;
HWND g_hLabelPerfFactor  = NULL;
HWND g_hEditPerfFactor   = NULL;
HWND g_hLabelTrainCfgId  = NULL;  // caption: "Consist Identifier"
HWND g_hEditTrainCfgId   = NULL;  // edit field for trainCfgId token
HWND g_hEditorUnitList   = NULL;
HWND g_hVisualConsistView = NULL; // 2D Visual Consist Track Preview (Docked / Floating)
HWND g_hUnitPreviewCard   = NULL; // Embedded Live 3D Rolling Stock Preview Card
HWND g_hSectionTrainCfg  = NULL;  // "TRAIN DETAILS" centered section header
HWND g_hSectionUnits     = NULL;  // "Consist Units" section header

HWND g_hSectionMetrics    = NULL; // "TRAIN SUMMARY & METRICS" centered section header
HWND g_hLabelMetricMass   = NULL;
HWND g_hEditMetricMass    = NULL;
HWND g_hLabelMetricLength = NULL;
HWND g_hEditMetricLength  = NULL;
HWND g_hLabelMetricPower  = NULL;
HWND g_hEditMetricPower   = NULL;
HWND g_hLabelMetricRatio  = NULL;
HWND g_hEditMetricRatio   = NULL;


// Track which edit box is focused for accent-line rendering
HWND g_hFocusedEdit = NULL;
HWND g_hHoveredEdit = NULL; // Track which edit box is hovered


static void SetActivePane(ActivePane newPane, HWND hWnd = NULL);
static void UpdateUnitPreviewFromSelected();
std::wstring AssetListGetCellText(int itemIndex, int subItemIndex, void* pParam);
static void RefreshEditorUnitList(bool preserveSelection = true);
static void DeleteSelectedConsistUnits(HWND hWnd);
static void ClearClipboard(HWND hWnd);
static void SaveCurrentConsist(HWND hWnd);
static void SaveCurrentConsistSessionState();
static bool SaveConsistSessionToDisk(HWND hWnd, const std::wstring& filename);

static bool SaveCurrentConsistDiskOnly(HWND hWnd);

static std::vector<ConsistReader::UnitInfo> g_ClipboardUnits;
const std::vector<ConsistReader::UnitInfo>& GetAppClipboardUnits()
{
    return g_ClipboardUnits;
}

void SetAppClipboardUnits(const std::vector<ConsistReader::UnitInfo>& units)
{
    g_ClipboardUnits = units;
}


static std::unordered_set<std::wstring> g_InitiallyBrokenConsists;
static std::unordered_set<std::wstring> g_SessionFixedConsists;
static std::unordered_map<std::wstring, std::unordered_set<int>> g_SessionFixedUnitsPerConsist;
static std::unordered_set<int> g_CheckedConsistUnits;

BOOL g_bAutoSave = FALSE;
std::wstring g_szConsistSearchQuery = L"";
std::wstring g_szStockSearchQuery = L"";
std::wstring g_szBasePath = L"";

enum class UnitHealthStatus
{
    Healthy,
    MissingStock,
    MissingShape
};

static UnitHealthStatus GetUnitHealthOnDisk(const ConsistReader::UnitInfo& unit, const std::wstring& basePath)
{
    if (unit.uid.empty() || unit.parentDir.empty()) return UnitHealthStatus::MissingStock;
    std::wstring ext = unit.isEngine ? L".eng" : L".wag";
    std::wstring unitPath = basePath;
    if (!unitPath.empty() && unitPath.back() != L'\\') unitPath += L'\\';
    unitPath += L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + ext;
    DWORD attr = GetFileAttributesW(unitPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        // Try alternate extension fallback (.wag / .eng)
        std::wstring altExt = unit.isEngine ? L".wag" : L".eng";
        std::wstring altPath = basePath;
        if (!altPath.empty() && altPath.back() != L'\\') altPath += L'\\';
        altPath += L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + altExt;
        DWORD altAttr = GetFileAttributesW(altPath.c_str());
        if (altAttr == INVALID_FILE_ATTRIBUTES || (altAttr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return UnitHealthStatus::MissingStock;
        }
        unitPath = altPath;
    }

    StockSpecReader::StockSpec spec = StockSpecReader::ReadFullSpec(unitPath, basePath);
    if (!spec.mainShapeFile.empty() && !spec.shapeExistsOnDisk)
    {
        return UnitHealthStatus::MissingShape;
    }

    return UnitHealthStatus::Healthy;
}

static bool IsUnitBrokenOnDisk(const ConsistReader::UnitInfo& unit, const std::wstring& basePath, bool checkShape = true)
{
    UnitHealthStatus h = GetUnitHealthOnDisk(unit, basePath);
    if (h == UnitHealthStatus::MissingStock) return true;
    if (checkShape && h == UnitHealthStatus::MissingShape) return true;
    return false;
}

static std::wstring BuildFullStockPath(const std::wstring& basePath, const std::wstring& folder, const std::wstring& fileName, const std::wstring& ext = L"")
{
    if (folder.empty() || fileName.empty()) return L"";
    std::wstring p = basePath;
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"TRAINS\\TRAINSET\\" + folder + L"\\" + fileName;

    // Check if filename already ends with .eng or .wag
    std::wstring lowerP = p;
    for (wchar_t& c : lowerP) c = towlower(c);
    if (lowerP.size() >= 4 && (lowerP.substr(lowerP.size() - 4) == L".eng" || lowerP.substr(lowerP.size() - 4) == L".wag"))
    {
        return p;
    }

    std::wstring extension = ext;
    if (!extension.empty())
    {
        if (extension[0] != L'.') extension = L"." + extension;
        std::wstring testP = p + extension;
        if (GetFileAttributesW(testP.c_str()) != INVALID_FILE_ATTRIBUTES) return testP;
    }

    std::wstring pEng = p + L".eng";
    if (GetFileAttributesW(pEng.c_str()) != INVALID_FILE_ATTRIBUTES) return pEng;

    std::wstring pWag = p + L".wag";
    if (GetFileAttributesW(pWag.c_str()) != INVALID_FILE_ATTRIBUTES) return pWag;

    // If file does not exist on disk, respect caller's requested extension
    if (!extension.empty())
    {
        return p + extension;
    }

    return pEng;
}

static std::wstring EvaluateAndUpdateConsistStatus(
    const std::wstring& consistKey,
    const std::vector<ConsistReader::UnitInfo>& units,
    bool isSavedToDisk = false)
{
    bool hasMissingStock = false;
    bool hasMissingShape = false;
    for (const auto& u : units)
    {
        UnitHealthStatus h = GetUnitHealthOnDisk(u, g_szBasePath);
        if (h == UnitHealthStatus::MissingStock)
        {
            hasMissingStock = true;
            break;
        }
        else if (h == UnitHealthStatus::MissingShape)
        {
            hasMissingShape = true;
        }
    }

    bool isBroken = (hasMissingStock || hasMissingShape);

    if (isSavedToDisk)
    {
        if (!isBroken)
        {
            if (!consistKey.empty() && g_InitiallyBrokenConsists.count(consistKey) > 0)
            {
                g_SessionFixedConsists.insert(consistKey);
            }
        }
        else
        {
            if (!consistKey.empty())
            {
                g_SessionFixedConsists.erase(consistKey);
                g_InitiallyBrokenConsists.insert(consistKey);
            }
        }
    }

    bool isFixed = (!isBroken && !consistKey.empty() && g_SessionFixedConsists.count(consistKey) > 0);
    if (isFixed) return L"Fixed";
    if (hasMissingStock || (!consistKey.empty() && g_InitiallyBrokenConsists.count(consistKey) > 0 && !isSavedToDisk && g_SessionFixedConsists.count(consistKey) == 0))
        return L"Missing Stock";
    if (hasMissingShape)
        return L"Missing Shape";

    return L"Healthy";
}

HWND g_hSplitter1 = NULL;
HWND g_hSplitter2 = NULL;
HWND g_hSplitter3 = NULL;
HWND g_hSplitter3Top = NULL;
HWND g_hSplitterSection3 = NULL; // Horizontal Splitter between Section 2 and Section 3
HWND g_hSplitterUnitPreview = NULL; // Left Vertical Splitter for 3D Live Unit Preview
HWND g_hSplitterUnitPreviewRight = NULL; // Right Vertical Splitter for 3D Live Unit Preview
HWND g_hSplitterTopDeck = NULL; // Horizontal Splitter directly under 3D Live Unit Preview
int g_wConsist = 600;            // Left Pane Total Width
int g_hConsistSplit = 380;       // Top Deck (Consists List) Height
int g_wCategorySplit = 130;      // Default width of Category Tree panel (smaller default)
int g_hSection3Height = 220;     // Default height of Section 3 (3D Visual Consist View)
int g_wUnitPreviewWidth = 480;   // Default width of 3D Live Unit Preview Card (matching screenshot)
int g_hTopCardsHeight = 220;     // Default height of Top Cards (matching Train Details full height)
HWND g_hRouteTree = NULL;         // Routes & Activities TreeView (Top Deck on Activity Tab)
int g_ActiveTab = 0;

enum SplitterType {
    SPLITTER_VERTICAL_MAIN = 1,        // Splitter 1: Left deck vs Right Workspace (IDC_SIZEWE)
    SPLITTER_HORIZONTAL = 2,           // Splitter 2: Consist deck vs Stock deck (IDC_SIZENS)
    SPLITTER_VERTICAL_SUB = 3,         // Splitter 3: TreeView vs ListView (IDC_SIZEWE)
    SPLITTER_HORIZONTAL_SECTION3 = 4,  // Splitter 4: Section 2 Workspace vs Section 3 Visual Consist View (IDC_SIZENS)
    SPLITTER_VERTICAL_UNIT_PREVIEW = 5, // Splitter 5: Left border of 3D Live Unit Preview (IDC_SIZEWE)
    SPLITTER_HORIZONTAL_TOPDECK = 6,   // Splitter 6: Bottom border of 3D Live Unit Preview (IDC_SIZENS)
    SPLITTER_VERTICAL_UNIT_PREVIEW_RIGHT = 7 // Splitter 7: Right border of 3D Live Unit Preview (IDC_SIZEWE)
};

static LRESULT CALLBACK SplitterWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SplitterType type = (SplitterType)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    static bool s_isDragging = false;
    static HWND s_hDragWnd = NULL;
    static POINT s_ptDragStart;
    static int s_initialVal = 0;

    switch (uMsg)
    {
    case WM_SETCURSOR:
    {
        if (type == SPLITTER_HORIZONTAL || type == SPLITTER_HORIZONTAL_SECTION3 || type == SPLITTER_HORIZONTAL_TOPDECK)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
        }
        else
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }
        return TRUE;
    }

    case WM_LBUTTONDOWN:
    {
        SetCapture(hWnd);
        s_isDragging = true;
        s_hDragWnd = hWnd;
        GetCursorPos(&s_ptDragStart);
        if (type == SPLITTER_VERTICAL_MAIN)
        {
            s_initialVal = g_wConsist;
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }
        else if (type == SPLITTER_HORIZONTAL)
        {
            s_initialVal = g_hConsistSplit;
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
        }
        else if (type == SPLITTER_VERTICAL_SUB)
        {
            s_initialVal = g_wCategorySplit;
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }
        else if (type == SPLITTER_HORIZONTAL_SECTION3)
        {
            s_initialVal = g_hSection3Height;
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
        }
        else if (type == SPLITTER_VERTICAL_UNIT_PREVIEW || type == SPLITTER_VERTICAL_UNIT_PREVIEW_RIGHT)
        {
            s_initialVal = g_wUnitPreviewWidth;
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }
        else if (type == SPLITTER_HORIZONTAL_TOPDECK)
        {
            s_initialVal = g_hTopCardsHeight;
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (s_isDragging && s_hDragWnd == hWnd && (wParam & MK_LBUTTON))
        {
            POINT ptNow;
            GetCursorPos(&ptNow);
            HWND hParent = GetParent(hWnd);
            if (!hParent) break;

            RECT rcParent;
            GetClientRect(hParent, &rcParent);
            int width = rcParent.right;
            int height = rcParent.bottom;
            int paneY = 150;
            int paneHeight = height - paneY;

            if (type == SPLITTER_VERTICAL_MAIN)
            {
                int dx = ptNow.x - s_ptDragStart.x;
                g_wConsist = s_initialVal + dx;
                if (g_wConsist < 200) g_wConsist = 200;
                if (g_wConsist > width - 200) g_wConsist = width - 200;
                if (g_wCategorySplit > g_wConsist - 80) g_wCategorySplit = g_wConsist - 80;
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (type == SPLITTER_HORIZONTAL)
            {
                int dy = ptNow.y - s_ptDragStart.y;
                g_hConsistSplit = s_initialVal + dy;
                if (g_hConsistSplit < 100) g_hConsistSplit = 100;
                if (g_hConsistSplit > paneHeight - 100) g_hConsistSplit = paneHeight - 100;
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }
            else if (type == SPLITTER_VERTICAL_SUB)
            {
                int dx = ptNow.x - s_ptDragStart.x;
                g_wCategorySplit = s_initialVal + dx;
                if (g_wCategorySplit < 60) g_wCategorySplit = 60;
                if (g_wCategorySplit > g_wConsist - 80) g_wCategorySplit = g_wConsist - 80;
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (type == SPLITTER_HORIZONTAL_SECTION3)
            {
                int dy = ptNow.y - s_ptDragStart.y;
                g_hSection3Height = s_initialVal - dy;
                if (g_hSection3Height < 100) g_hSection3Height = 100;
                if (g_hSection3Height > paneHeight - 250) g_hSection3Height = paneHeight - 250;
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }
            else if (type == SPLITTER_VERTICAL_UNIT_PREVIEW)
            {
                int dx = ptNow.x - s_ptDragStart.x;
                int wLeftPane = g_wConsist;
                int wEditor = width - wLeftPane - 9;
                g_wUnitPreviewWidth = s_initialVal - dx;
                if (g_wUnitPreviewWidth < 200) g_wUnitPreviewWidth = 200;
                if (g_wUnitPreviewWidth > wEditor - 240) g_wUnitPreviewWidth = wEditor - 240;
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (type == SPLITTER_VERTICAL_UNIT_PREVIEW_RIGHT)
            {
                int dx = ptNow.x - s_ptDragStart.x;
                int wLeftPane = g_wConsist;
                int wEditor = width - wLeftPane - 9;
                g_wUnitPreviewWidth = s_initialVal + dx;
                if (g_wUnitPreviewWidth < 200) g_wUnitPreviewWidth = 200;
                if (g_wUnitPreviewWidth > wEditor - 240) g_wUnitPreviewWidth = wEditor - 240;
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (type == SPLITTER_HORIZONTAL_TOPDECK)
            {
                int dy = ptNow.y - s_ptDragStart.y;
                g_hTopCardsHeight = s_initialVal + dy;
                if (g_hTopCardsHeight < 140) g_hTopCardsHeight = 140;
                if (g_hTopCardsHeight > paneHeight - 200) g_hTopCardsHeight = paneHeight - 200;
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }

            // Trigger parent WM_SIZE recalculation and window refresh
            SendMessage(hParent, WM_SIZE, 0, MAKELPARAM(width, height));
            InvalidateRect(hParent, NULL, TRUE);
            if (g_hCategoryTree != NULL) InvalidateRect(g_hCategoryTree, NULL, TRUE);
            if (g_hConsistList != NULL) g_ConsistList.Invalidate();
            if (g_hAssetList != NULL) g_AssetList.Invalidate();
            if (g_hAssetList2 != NULL) g_AssetList2.Invalidate();
            if (g_hAssetList3 != NULL) g_AssetList3.Invalidate();
            if (g_hConsistHeader != NULL) InvalidateRect(g_hConsistHeader, NULL, TRUE);
            if (g_hStockHeader != NULL) InvalidateRect(g_hStockHeader, NULL, TRUE);
            if (g_hWorkspaceHeader != NULL) InvalidateRect(g_hWorkspaceHeader, NULL, TRUE);
            for (int k = 0; k < 3; ++k)
            {
                if (g_hPaneCatBtn[k] != NULL) InvalidateRect(g_hPaneCatBtn[k], NULL, TRUE);
                if (g_hPaneSearchEdit[k] != NULL) InvalidateRect(g_hPaneSearchEdit[k], NULL, TRUE);
            }
            UpdateWindow(hParent);
        }
        else
        {
            if (type == SPLITTER_HORIZONTAL || type == SPLITTER_HORIZONTAL_SECTION3 || type == SPLITTER_HORIZONTAL_TOPDECK)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }
            else
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    {
        if (s_isDragging && s_hDragWnd == hWnd)
        {
            s_isDragging = false;
            s_hDragWnd = NULL;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // Dark background matching fluent theme
        HBRUSH hbrBg = CreateSolidBrush(RGB(32, 32, 32));
        FillRect(hdc, &rc, hbrBg);
        DeleteObject(hbrBg);

        // 1px border lines
        COLORREF clrLine = RGB(65, 65, 65);
        HPEN hPen = CreatePen(PS_SOLID, 1, clrLine);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        if (type == SPLITTER_HORIZONTAL || type == SPLITTER_HORIZONTAL_SECTION3 || type == SPLITTER_HORIZONTAL_TOPDECK)
        {
            MoveToEx(hdc, 0, 0, NULL);
            LineTo(hdc, rc.right, 0);
            MoveToEx(hdc, 0, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);
        }
        else
        {
            MoveToEx(hdc, 0, 0, NULL);
            LineTo(hdc, 0, rc.bottom);
            MoveToEx(hdc, rc.right - 1, 0, NULL);
            LineTo(hdc, rc.right - 1, rc.bottom);
        }

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void RegisterSplitterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_GLOBALCLASS;
    wcex.lpfnWndProc = SplitterWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_SIZEWE);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"TSCBSplitter";
    RegisterClassExW(&wcex);
}

enum DragState {
    DRAG_NONE,
    DRAG_SPLIT1,     // Main vertical split between Left Pane and Right Workspace
    DRAG_SPLIT2,     // Horizontal split between Consists (top) and Stocks (bottom)
    DRAG_SPLIT3,     // Vertical split between Category Tree and Asset List in bottom deck
    DRAG_SPLIT_SEC3  // Horizontal split between Section 2 Workspace and Section 3 Visual Consist View
};

DragState g_DragState = DRAG_NONE;

void ResetConsistEditorWorkspace(HWND hWnd);
void PopulateRouteActivityTree();
void PopulateConsistListFromCache();

void SwitchActiveConsistTab(HWND hWnd, int newSel)
{
    if (newSel != g_ActiveTab)
    {
        g_ActiveTab = newSel;
        if (g_hCustomTitleBar && IsWindow(g_hCustomTitleBar))
        {
            CustomTitleBar_SetActiveTab(g_hCustomTitleBar, g_ActiveTab);
        }

        if (g_ActiveTab == 1) // Activity Consists Tab
        {
            ResetConsistEditorWorkspace(hWnd);
            PopulateRouteActivityTree();
            g_ConsistList.Clear();
            g_ConsistList.ClearColumns();
            g_ConsistList.AddColumn(L"Name", 200, 0);
            g_ConsistList.AddColumn(L"Units", 70, 0);
            g_ConsistList.AddColumn(L"Status", 100, 0);
            SetWindowTextW(g_hConsistHeader, L"  Activity Consists");
            CommandBar_SetButtonText(g_hCommandBar, CMD_ACTION_SAVE_CONSISTS, L"Save Activity Consist(s)", 195);

            if (g_hEditTrainCfgId)  SendMessage(g_hEditTrainCfgId,  EM_SETREADONLY, TRUE, 0);
            if (g_hEditFileName)    SendMessage(g_hEditFileName,    EM_SETREADONLY, TRUE, 0);
        }
        else // Main Consists Tab (Tab 0)
        {
            ResetConsistEditorWorkspace(hWnd);
            g_ConsistList.Clear();
            g_ConsistList.ClearColumns();
            g_ConsistList.AddColumn(L"Name", 240, 0);
            g_ConsistList.AddColumn(L"Units", 80, 0);
            g_ConsistList.AddColumn(L"Status", 100, 0);
            g_ConsistList.AddColumn(L"Modified", 140, 0);
            PopulateConsistListFromCache();
            CommandBar_SetButtonText(g_hCommandBar, CMD_ACTION_SAVE_CONSISTS, L"Save Consist(s)", 145);

            if (g_hEditFileName)    SendMessage(g_hEditFileName,    EM_SETREADONLY, FALSE, 0);
            if (g_hEditTrainCfgId)  SendMessage(g_hEditTrainCfgId,  EM_SETREADONLY, FALSE, 0);
            if (g_hEditTrainName)   SendMessage(g_hEditTrainName,   EM_SETREADONLY, FALSE, 0);
            if (g_hEditMaxVelocity) SendMessage(g_hEditMaxVelocity, EM_SETREADONLY, FALSE, 0);
            if (g_hEditPerfFactor)  SendMessage(g_hEditPerfFactor,  EM_SETREADONLY, FALSE, 0);
        }

        if (g_hEditFileName)    InvalidateRect(g_hEditFileName,    NULL, TRUE);
        if (g_hEditTrainCfgId)  InvalidateRect(g_hEditTrainCfgId,  NULL, TRUE);
        if (g_hEditTrainName)   InvalidateRect(g_hEditTrainName,   NULL, TRUE);
        if (g_hEditMaxVelocity) InvalidateRect(g_hEditMaxVelocity, NULL, TRUE);
        if (g_hEditPerfFactor)  InvalidateRect(g_hEditPerfFactor,  NULL, TRUE);

        RECT rc;
        GetClientRect(hWnd, &rc);
        SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    }
}
        // 0 = MAIN CONSISTS, 1 = ACTIVITY CONSISTS
std::vector<ConsistReader::UnitInfo> g_LoadedConsistUnits;
std::wstring g_szCurrentConsistFile;
static bool g_bIsLoadingConsist = false;

struct ConsistHistoryStep {
    std::vector<ConsistReader::UnitInfo> units;
    std::wstring actionDesc;
};
static std::vector<ConsistHistoryStep> g_UndoStack;
static std::vector<ConsistHistoryStep> g_RedoStack;

struct ConsistSessionState {
    std::wstring fileName;
    ConsistReader::TrainConfig trainCfg;
    std::vector<ConsistReader::UnitInfo> units;
    std::vector<int> selectedIndices;
    int scrollY = 0;
    bool isDirty = false;
    std::vector<ConsistHistoryStep> undoStack;
    std::vector<ConsistHistoryStep> redoStack;
};
static std::unordered_map<std::wstring, ConsistSessionState> g_ConsistSessions;



BOOL g_bDarkMode = TRUE;
HBRUSH g_hbrDarkBackground = NULL;

BOOL IsSystemDarkMode()
{
    return TRUE; // Universal Dark Mode
}

void ApplyTitleBarTheme(HWND hWnd, BOOL bDarkMode)
{
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL BOOL_DARK = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &BOOL_DARK, sizeof(BOOL_DARK));

    // Enable Windows 11 22H2+ Backdrop (Mica Alt / Tabbed Window)
    DWORD backdropType = DWMSBT_TABBEDWINDOW;
    DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    // Enable Windows 11 21H2 Mica (Build 22000 fallback)
    BOOL micaTrue = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_MICA_EFFECT, &micaTrue, sizeof(micaTrue));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = RGB(55, 55, 62);
    DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 80, 0 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);

    SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

HFONT GetAdaptiveSystemFont()
{
    NONCLIENTMETRICS ncm = { 0 };
    ncm.cbSize = sizeof(NONCLIENTMETRICS);

    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
    {
        return CreateFontIndirect(&ncm.lfMessageFont);
    }

    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

// Custom Tab Control Subclass rendering WinUI 3 / Windows 11 File Explorer borderless tabs is no longer needed


#ifdef _WINDLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
#endif

extern "C" __declspec(dllexport) int TSCB_Run(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (!hInstance || hInstance == GetModuleHandleW(NULL))
    {
        HMODULE hSelf = GetModuleHandleW(L"TSCBCore64.dll");
        if (!hSelf) hSelf = GetModuleHandleW(L"TSCBCore32.dll");
        if (hSelf) hInstance = hSelf;
    }

    DatabaseManager::Initialize();
    AppLogging::AppLogger::Initialize();

    // Register embedded font resources in process memory
    TSCB_LoadEmbeddedFonts();

    g_bDarkMode = TRUE;
    g_hbrDarkBackground = CreateSolidBrush(UITheme::DarkBackground);

    LOG_INFO("===============================================================================");
    LOG_INFO(" Train Sim Consist Builder v9.3.0 (Native Core Engine) - Session Initialized");
    LOG_INFO("===============================================================================");

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    hInst = hInstance;
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        TSCB_UnloadEmbeddedFonts();
        AppLogging::AppLogger::Shutdown();
        DatabaseManager::Shutdown();
        return FALSE;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hUIFont) DeleteObject(hUIFont);
    if (g_hbrDarkBackground) DeleteObject(g_hbrDarkBackground);

    TSCB_UnloadEmbeddedFonts();
    AppLogging::AppLogger::Shutdown();
    DatabaseManager::Shutdown();

    return (int)msg.wParam;
}

extern "C" __declspec(dllexport) void CALLBACK TSCB_RunW(HWND hwnd, HINSTANCE hinst, LPCWSTR lpszCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hwnd);
    if (nCmdShow == 0) nCmdShow = SW_SHOWNORMAL;
    TSCB_Run(hinst, const_cast<LPWSTR>(lpszCmdLine ? lpszCmdLine : L""), nCmdShow);
}

#ifndef _WINDLL
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    return TSCB_Run(hInstance, lpCmdLine, nCmdShow);
}
#endif

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAINSIMCONSISTBUILDER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = g_hbrDarkBackground;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"TrainSimConsistBuilderClass";
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    CustomListControl::Register(hInstance);
    CustomTreeView::Register(hInstance);
    FilterPopup::Register(hInstance);
    RegisterSplitterClass(hInstance);
    // Retrieve the desktop work area (excludes the taskbar)
    RECT rcWorkArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWorkArea, 0);
    int workAreaWidth = rcWorkArea.right - rcWorkArea.left;
    int workAreaHeight = rcWorkArea.bottom - rcWorkArea.top;

    int width = 1660;
    int height = 900;

    // Constrain size if the screen is smaller than the default window size
    if (width > workAreaWidth) width = workAreaWidth;
    if (height > workAreaHeight) height = workAreaHeight;

    // Calculate centered position
    int x = rcWorkArea.left + (workAreaWidth - width) / 2;
    int y = rcWorkArea.top + (workAreaHeight - height) / 2;

    HWND hWnd = CreateWindowExW(0,
        L"TrainSimConsistBuilderClass",
        L"Train Sim Consist Builder for Open Rails",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    ShowWindow(hWnd, SW_MAXIMIZE);
    UpdateWindow(hWnd);

    return TRUE;
}

static DragState GetSplitterUnderMouse(int x, int y, int width, int height)
{
    int paneY = 150;
    int paneHeight = height - paneY;
    if (paneHeight < 50) return DRAG_NONE;

    // Check Splitter 1 (vertical separator bar: g_wConsist to g_wConsist + 9)
    // Comfortable hit zone: [g_wConsist - 6, g_wConsist + 15]
    if (y >= paneY && x >= (g_wConsist - 6) && x <= (g_wConsist + 15))
    {
        return DRAG_SPLIT1;
    }

    // Check Splitter 2 (horizontal separator bar: g_hConsistSplit to g_hConsistSplit + 9)
    // Comfortable hit zone: [paneY + g_hConsistSplit - 6, paneY + g_hConsistSplit + 15]
    if (x >= 0 && x < g_wConsist && y >= (paneY + g_hConsistSplit - 6) && y <= (paneY + g_hConsistSplit + 15))
    {
        return DRAG_SPLIT2;
    }

    // Check Splitter 3 (vertical splitter between Tree and List in bottom deck, and in top deck on Activity tab)
    // Comfortable hit zone: [g_wCategorySplit - 6, g_wCategorySplit + 15]
    int bottomY = paneY + g_hConsistSplit + 9;
    if (x >= 0 && x < g_wConsist && x >= (g_wCategorySplit - 6) && x <= (g_wCategorySplit + 15))
    {
        if (y >= bottomY + 28)
        {
            return DRAG_SPLIT3;
        }
        if (g_ActiveTab == 1 && y >= (paneY + 28) && y < (paneY + g_hConsistSplit))
        {
            return DRAG_SPLIT3;
        }
    }

    // Check Splitter Section 3 (horizontal separator bar between Section 2 Workspace and Section 3 Visual View)
    bool isFloating = (g_hVisualConsistView != NULL && VisualConsistView_IsFloating(g_hVisualConsistView));
    bool isCollapsed = (g_hVisualConsistView != NULL && VisualConsistView_IsCollapsed(g_hVisualConsistView));
    if (!isFloating && !isCollapsed && x >= (g_wConsist + 9))
    {
        int sec3SplitY = paneY + paneHeight - g_hSection3Height - 9;
        if (y >= (sec3SplitY - 6) && y <= (sec3SplitY + 15))
        {
            return DRAG_SPLIT_SEC3;
        }
    }

    return DRAG_NONE;
}

static void PopulateCategoryTree()
{
    g_CategoryTreeView.Clear();

    // 1. Engines Node
    g_pNodeEngines = g_CategoryTreeView.AddRoot(L"Engines", L"Engines", 0, true);

    // Engines Sub-nodes (Alphabetical: All, Control, Diesel, Electric, Steam)
    g_pNodeEnginesAll = g_CategoryTreeView.AddChild(g_pNodeEngines, L"All", L"EnginesAll", 0, false);
    g_pNodeControl = g_CategoryTreeView.AddChild(g_pNodeEngines, L"Control", L"Control", 0, false);
    g_pNodeDiesel = g_CategoryTreeView.AddChild(g_pNodeEngines, L"Diesel", L"Diesel", 0, false);
    g_pNodeElectric = g_CategoryTreeView.AddChild(g_pNodeEngines, L"Electric", L"Electric", 0, false);
    g_pNodeSteam = g_CategoryTreeView.AddChild(g_pNodeEngines, L"Steam", L"Steam", 0, false);

    // 2. Wagons Node
    g_pNodeWagons = g_CategoryTreeView.AddRoot(L"Wagons", L"Wagons", 0, true);

    // Wagons Sub-nodes (Alphabetical: All, Freight, Passenger, Tender)
    g_pNodeWagonsAll = g_CategoryTreeView.AddChild(g_pNodeWagons, L"All", L"WagonsAll", 0, false);
    g_pNodeFreight = g_CategoryTreeView.AddChild(g_pNodeWagons, L"Freight", L"Freight", 0, false);
    g_pNodePassenger = g_CategoryTreeView.AddChild(g_pNodeWagons, L"Passenger", L"Passenger", 0, false);
    g_pNodeTender = g_CategoryTreeView.AddChild(g_pNodeWagons, L"Tender", L"Tender", 0, false);

    // Auto-expand root category nodes and select "All" engines by default
    g_CategoryTreeView.ExpandNode(g_pNodeEngines, true);
    g_CategoryTreeView.ExpandNode(g_pNodeWagons, true);
    g_CategoryTreeView.SelectNode(g_pNodeEnginesAll);
}

static void PopulateRouteActivityTree()
{
    g_RouteTreeView.Clear();
    if (g_szBasePath.empty()) return;

    std::wstring routesDir = g_szBasePath;
    if (routesDir.back() != L'\\') routesDir += L'\\';
    routesDir += L"ROUTES\\";

    std::wstring searchPattern = routesDir + L"*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
                continue;

            std::wstring routeFolder = ffd.cFileName;
            std::wstring actDir = routesDir + routeFolder + L"\\ACTIVITIES";
            DWORD actAttr = GetFileAttributesW(actDir.c_str());
            if (actAttr != INVALID_FILE_ATTRIBUTES && (actAttr & FILE_ATTRIBUTE_DIRECTORY))
            {
                // Add Route folder parent node
                CustomTreeNode* pRouteNode = g_RouteTreeView.AddRoot(routeFolder, routeFolder, 0, true);

                // Scan for *.act files inside the ACTIVITIES folder
                std::wstring actSearch = actDir + L"\\*.act";
                WIN32_FIND_DATAW actFfd;
                HANDLE hActFind = FindFirstFileW(actSearch.c_str(), &actFfd);
                if (hActFind != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (!(actFfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            g_RouteTreeView.AddChild(pRouteNode, actFfd.cFileName, actFfd.cFileName, 0, false);
                        }
                    } while (FindNextFileW(hActFind, &actFfd));
                    FindClose(hActFind);
                }
            }
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
}

static void PopulateActivityConsistList();
static void LoadAndDisplayActivityConsist(HWND hWnd, int consistIndex);

static void OnRouteActivitySelected(HWND hWnd, CustomTreeNode* pItem)
{
    if (!pItem) return;

    if (pItem->parent != nullptr) // Selected an .act file child node!
    {
        // Save current consist state before switching activities
        SaveCurrentConsistSessionState();

        // Reset activity consist index pointer and clear workspace before loading new file
        g_CurrentActivityConsistIndex = -1;
        g_LoadedConsistUnits.clear();

        std::wstring szRouteFolder = pItem->parent->text;
        std::wstring szActFile = pItem->text;

        std::wstring fullActPath = g_szBasePath;
        if (!fullActPath.empty() && fullActPath.back() != L'\\') fullActPath += L'\\';
        fullActPath += L"ROUTES\\" + szRouteFolder + L"\\ACTIVITIES\\" + szActFile;

        g_CurrentActivityFilePath = fullActPath;
        g_CurrentActivityData = ActivityConsistReader::LoadActivityConsists(fullActPath, g_szBasePath);

        for (const auto& con : g_CurrentActivityData.consists)
        {
            if (con.isBroken)
            {
                g_InitiallyBrokenConsists.insert(g_CurrentActivityFilePath + L"#" + con.id);
            }
        }

        PopulateActivityConsistList();

        // Automatically select and load first consist into workspace if available
        if (!g_CurrentActivityData.consists.empty())
        {
            g_ConsistList.SetSelectedIndex(0);
            LoadAndDisplayActivityConsist(hWnd, 0);
        }
        else
        {
            ResetConsistEditorWorkspace(hWnd);
        }
    }
    else
    {
        // Root route node selected: clear activity consist list and reset workspace
        g_CurrentActivityData = ActivityConsistReader::ActivityData();
        g_CurrentActivityFilePath = L"";
        g_ConsistList.Clear();
        SetWindowTextW(g_hConsistHeader, L"  Activity Consists");
        ResetConsistEditorWorkspace(hWnd);
    }
}

static void UpdateLibraryTheme(BOOL bDarkMode)
{
    if (g_hCategoryTree)
    {
        g_CategoryTreeView.SetDarkMode(bDarkMode ? true : false);
        g_CategoryTreeView.Invalidate();
    }

    if (g_hRouteTree)
    {
        g_RouteTreeView.SetDarkMode(bDarkMode ? true : false);
        g_RouteTreeView.Invalidate();
    }

    if (g_hConsistList)
    {
        g_ConsistList.Invalidate();
    }

    if (g_hAssetList)
    {
        g_AssetList.Invalidate();
    }
    if (g_hAssetList2)
    {
        g_AssetList2.Invalidate();
    }
    if (g_hAssetList3)
    {
        g_AssetList3.Invalidate();
    }
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetDarkMode(g_hVisualConsistView, bDarkMode);
    }
    if (g_hUnitPreviewCard)
    {
        UnitPreviewCard_SetDarkMode(g_hUnitPreviewCard, bDarkMode);
    }
}

#define WM_ADD_CONSIST_ITEM (WM_USER + 301)
#define WM_CONSIST_SCAN_COMPLETE (WM_USER + 304)
#define WM_REQUEST_DEBOUNCE_RESCAN (WM_USER + 305)
#define TIMER_DEBOUNCE_RESCAN 401

bool StringContainsIgnoreCase(const std::wstring& str, const std::wstring& search);
static bool MatchLetterFilter(const std::wstring& str, const std::vector<std::wstring>& activeFilters);
static bool MatchUnitsFilter(int units, const std::vector<std::wstring>& activeFilters);
static bool MatchModifiedFilter(const FILETIME& ft, const std::vector<std::wstring>& activeFilters);

struct ScannedConsist {
    std::wstring szFileName;
    std::wstring szName;
    int nUnits = 0;
    std::wstring szLastModified;
    FILETIME ftLastWriteTime = { 0 };
    bool isBroken = false;
    bool hasMissingStock = false;
    bool hasMissingShape = false;
};

static std::vector<ScannedConsist> g_ScannedConsistsCache;

static void PopulateConsistListFromCache()
{
    if (!g_hConsistList) return;

    g_ConsistList.Clear();

    int totalConsists = 0;
    int brokenConsists = 0;

    for (const auto& item : g_ScannedConsistsCache)
    {
        // 1. Search Query Filter
        if (!g_szConsistSearchQuery.empty() &&
            !StringContainsIgnoreCase(item.szFileName, g_szConsistSearchQuery) &&
            !StringContainsIgnoreCase(item.szName, g_szConsistSearchQuery))
        {
            continue;
        }

        // 2. Column Header Filters
        if (!MatchLetterFilter(item.szName, g_ConsistList.GetActiveFilters(0)))
            continue;
        if (!MatchUnitsFilter(item.nUnits, g_ConsistList.GetActiveFilters(1)))
            continue;

        auto itSess = g_ConsistSessions.find(item.szFileName);
        std::wstring statusStr;
        if (itSess != g_ConsistSessions.end())
        {
            statusStr = EvaluateAndUpdateConsistStatus(item.szFileName, itSess->second.units, !itSess->second.isDirty);
        }
        else
        {
            bool isFixedInSession = (!item.isBroken && g_SessionFixedConsists.count(item.szFileName) > 0);
            if (isFixedInSession)
                statusStr = L"Fixed";
            else if (item.hasMissingStock || (item.isBroken && !item.hasMissingShape))
                statusStr = L"Missing Stock";
            else if (item.hasMissingShape)
                statusStr = L"Missing Shape";
            else
                statusStr = L"Healthy";
        }

        const auto& statusFilters = g_ConsistList.GetActiveFilters(2);
        if (!statusFilters.empty())
        {
            bool match = false;
            for (const auto& f : statusFilters)
            {
                if (f == statusStr) { match = true; break; }
            }
            if (!match) continue;
        }

        if (!MatchModifiedFilter(item.ftLastWriteTime, g_ConsistList.GetActiveFilters(3)))
            continue;

        std::wstring displayName = item.szName;
        if (itSess != g_ConsistSessions.end() && itSess->second.isDirty)
        {
            displayName = L"● " + displayName;
        }

        g_ConsistList.AddItem({ displayName, std::to_wstring(item.nUnits), statusStr, item.szLastModified, item.szFileName });
        totalConsists++;
        if (statusStr == L"Missing Stock" || statusStr == L"Missing Shape" || statusStr == L"Broken")
            brokenConsists++;
    }

    wchar_t szHeader[128];
    swprintf_s(szHeader, 128, L"  Consists Manager [ Total: %d • Broken: %d ]", totalConsists, brokenConsists);
    SetWindowTextW(g_hConsistHeader, szHeader);

    int curSortCol = g_ConsistList.GetSortColumn();
    if (curSortCol < 0) curSortCol = 0;
    g_ConsistList.SortByColumn(curSortCol, false);
}

// Global Directory Watcher state
HANDLE g_hWatcherThread = NULL;
HANDLE g_hDirHandle = INVALID_HANDLE_VALUE;
volatile BOOL g_bCancelWatcher = FALSE;
volatile BOOL g_bIgnoreWatcher = FALSE;
#define TIMER_IGNORE_WATCHER_RESET 402

static std::wstring FormatFileTimeFriendly(const FILETIME& ft)
{
    FILETIME ftLocal;
    if (!FileTimeToLocalFileTime(&ft, &ftLocal))
        ftLocal = ft;

    SYSTEMTIME stFile;
    if (!FileTimeToSystemTime(&ftLocal, &stFile))
        return L"";

    SYSTEMTIME stCurrentSystem;
    GetSystemTime(&stCurrentSystem);
    FILETIME ftCurrentUTC;
    SystemTimeToFileTime(&stCurrentSystem, &ftCurrentUTC);
    FILETIME ftCurrentLocal;
    FileTimeToLocalFileTime(&ftCurrentUTC, &ftCurrentLocal);

    ULARGE_INTEGER uFile, uCurrent;
    uFile.LowPart = ftLocal.dwLowDateTime;
    uFile.HighPart = ftLocal.dwHighDateTime;
    uCurrent.LowPart = ftCurrentLocal.dwLowDateTime;
    uCurrent.HighPart = ftCurrentLocal.dwHighDateTime;

    if (uCurrent.QuadPart >= uFile.QuadPart)
    {
        ULONGLONG diffTicks = uCurrent.QuadPart - uFile.QuadPart;
        ULONGLONG diffSeconds = diffTicks / 10000000ULL;

        if (diffSeconds < 60)
        {
            if (diffSeconds == 1)
                return L"1 second ago";
            else
                return std::to_wstring(diffSeconds) + L" seconds ago";
        }
        else if (diffSeconds < 3600)
        {
            ULONGLONG minutes = diffSeconds / 60;
            ULONGLONG seconds = diffSeconds % 60;
            std::wstring minStr = (minutes == 1) ? L"1 minute" : (std::to_wstring(minutes) + L" minutes");
            std::wstring secStr = (seconds == 1) ? L"1 second" : (std::to_wstring(seconds) + L" seconds");
            return minStr + L" " + secStr + L" ago";
        }
    }

    SYSTEMTIME stCurrentLocal;
    FileTimeToSystemTime(&ftCurrentLocal, &stCurrentLocal);

    ULARGE_INTEGER uYesterday;
    uYesterday.QuadPart = uCurrent.QuadPart - (24ULL * 3600ULL * 10000000ULL);
    FILETIME ftYesterday;
    ftYesterday.dwLowDateTime = uYesterday.LowPart;
    ftYesterday.dwHighDateTime = uYesterday.HighPart;
    SYSTEMTIME stYesterday;
    FileTimeToSystemTime(&ftYesterday, &stYesterday);

    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"%02d:%02d:%02d", stFile.wHour, stFile.wMinute, stFile.wSecond);

    if (stFile.wYear == stCurrentLocal.wYear && stFile.wMonth == stCurrentLocal.wMonth && stFile.wDay == stCurrentLocal.wDay)
    {
        return std::wstring(L"Today, ") + timeBuf;
    }
    else if (stFile.wYear == stYesterday.wYear && stFile.wMonth == stYesterday.wMonth && stFile.wDay == stYesterday.wDay)
    {
        return std::wstring(L"Yesterday, ") + timeBuf;
    }

    const wchar_t* MONTHS[] = { L"", L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December" };
    int monthIdx = stFile.wMonth;
    if (monthIdx < 1 || monthIdx > 12) monthIdx = 1;

    wchar_t dateBuf[128];
    swprintf_s(dateBuf, L"%s %d, %04d, %s", MONTHS[monthIdx], stFile.wDay, stFile.wYear, timeBuf);
    return dateBuf;
}

struct ScanThreadParams {
    HWND hWndParent = NULL;
    std::wstring szPath;
    std::wstring szSearch;
};

HANDLE g_hScanThread = NULL;
HANDLE g_hStockScanThread = NULL;
volatile BOOL g_bCancelScan = FALSE;

static inline bool FastContainsIgnoreCase(const std::wstring& str, const std::wstring& searchLower)
{
    if (searchLower.empty()) return true;
    if (str.size() < searchLower.size()) return false;

    size_t sLen = searchLower.size();
    size_t maxStart = str.size() - sLen;
    const wchar_t* pStr = str.c_str();
    const wchar_t* pSearch = searchLower.c_str();
    wchar_t firstChar = pSearch[0];

    for (size_t i = 0; i <= maxStart; ++i)
    {
        if (towlower(pStr[i]) == firstChar)
        {
            bool match = true;
            for (size_t j = 1; j < sLen; ++j)
            {
                if (towlower(pStr[i + j]) != pSearch[j])
                {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

bool StringContainsIgnoreCase(const std::wstring& str, const std::wstring& search)
{
    if (search.empty()) return true;
    std::wstring searchLower = search;
    for (wchar_t& c : searchLower) c = towlower(c);
    return FastContainsIgnoreCase(str, searchLower);
}

static bool MatchLetterFilter(const std::wstring& str, const std::vector<std::wstring>& activeFilters)
{
    if (activeFilters.empty()) return true;

    if (str.empty())
    {
        return std::find(activeFilters.begin(), activeFilters.end(), L"Other") != activeFilters.end();
    }

    wchar_t c = towupper(str[0]);
    bool isDigit = (c >= L'0' && c <= L'9');
    bool isAlpha = (c >= L'A' && c <= L'Z');

    for (const auto& filter : activeFilters)
    {
        if (filter == L"0-9" && isDigit) return true;
        if (filter == L"A-H" && c >= L'A' && c <= L'H') return true;
        if (filter == L"I-P" && c >= L'I' && c <= L'P') return true;
        if (filter == L"Q-Z" && c >= L'Q' && c <= L'Z') return true;
        if (filter == L"Other" && !isDigit && !isAlpha) return true;
    }
    return false;
}

static bool MatchUnitsFilter(int units, const std::vector<std::wstring>& activeFilters)
{
    if (activeFilters.empty()) return true;

    for (const auto& filter : activeFilters)
    {
        if (filter == L"1-16" && units >= 1 && units <= 16) return true;
        if (filter == L"17-26" && units >= 17 && units <= 26) return true;
        if (filter == L"27-36" && units >= 27 && units <= 36) return true;
        if (filter == L"37-46" && units >= 37 && units <= 46) return true;
        if (filter == L"47-56" && units >= 47 && units <= 56) return true;
        if (filter == L"57-60+" && units >= 57) return true;
    }
    return false;
}

static bool MatchModifiedFilter(const FILETIME& ft, const std::vector<std::wstring>& activeFilters)
{
    if (activeFilters.empty()) return true;

    FILETIME ftLocal;
    if (!FileTimeToLocalFileTime(&ft, &ftLocal))
        ftLocal = ft;

    SYSTEMTIME stCurrentSystem;
    GetSystemTime(&stCurrentSystem);
    FILETIME ftCurrentUTC;
    SystemTimeToFileTime(&stCurrentSystem, &ftCurrentUTC);
    FILETIME ftCurrentLocal;
    FileTimeToLocalFileTime(&ftCurrentUTC, &ftCurrentLocal);

    ULARGE_INTEGER uFile, uCurrent;
    uFile.LowPart = ftLocal.dwLowDateTime;
    uFile.HighPart = ftLocal.dwHighDateTime;
    uCurrent.LowPart = ftCurrentLocal.dwLowDateTime;
    uCurrent.HighPart = ftCurrentLocal.dwHighDateTime;

    SYSTEMTIME stCurrentLocal;
    FileTimeToSystemTime(&ftCurrentLocal, &stCurrentLocal);

    SYSTEMTIME stMidnight = stCurrentLocal;
    stMidnight.wHour = 0;
    stMidnight.wMinute = 0;
    stMidnight.wSecond = 0;
    stMidnight.wMilliseconds = 0;
    
    FILETIME ftMidnightUTC;
    SystemTimeToFileTime(&stMidnight, &ftMidnightUTC);
    FILETIME ftMidnightLocal;
    FileTimeToLocalFileTime(&ftMidnightUTC, &ftMidnightLocal);
    ULARGE_INTEGER uMidnight;
    uMidnight.LowPart = ftMidnightLocal.dwLowDateTime;
    uMidnight.HighPart = ftMidnightLocal.dwHighDateTime;

    ULARGE_INTEGER uYesterdayMidnight;
    uYesterdayMidnight.QuadPart = uMidnight.QuadPart - (24ULL * 3600ULL * 10000000ULL);

    ULARGE_INTEGER uLastWeekMidnight;
    uLastWeekMidnight.QuadPart = uMidnight.QuadPart - (7ULL * 24ULL * 3600ULL * 10000000ULL);

    SYSTEMTIME stStartOfMonth = stCurrentLocal;
    stStartOfMonth.wDay = 1;
    stStartOfMonth.wHour = 0;
    stStartOfMonth.wMinute = 0;
    stStartOfMonth.wSecond = 0;
    stStartOfMonth.wMilliseconds = 0;
    
    FILETIME ftStartOfMonthUTC;
    SystemTimeToFileTime(&stStartOfMonth, &ftStartOfMonthUTC);
    FILETIME ftStartOfMonthLocal;
    FileTimeToLocalFileTime(&ftStartOfMonthUTC, &ftStartOfMonthLocal);
    ULARGE_INTEGER uStartOfMonth;
    uStartOfMonth.LowPart = ftStartOfMonthLocal.dwLowDateTime;
    uStartOfMonth.HighPart = ftStartOfMonthLocal.dwHighDateTime;

    SYSTEMTIME stLastMonth = stCurrentLocal;
    if (stLastMonth.wMonth == 1)
    {
        stLastMonth.wMonth = 12;
        stLastMonth.wYear--;
    }
    else
    {
        stLastMonth.wMonth--;
    }
    stLastMonth.wDay = 1;
    stLastMonth.wHour = 0;
    stLastMonth.wMinute = 0;
    stLastMonth.wSecond = 0;
    stLastMonth.wMilliseconds = 0;
    
    FILETIME ftLastMonthUTC;
    SystemTimeToFileTime(&stLastMonth, &ftLastMonthUTC);
    FILETIME ftLastMonthLocal;
    FileTimeToLocalFileTime(&ftLastMonthUTC, &ftLastMonthLocal);
    ULARGE_INTEGER uLastMonth;
    uLastMonth.LowPart = ftLastMonthLocal.dwLowDateTime;
    uLastMonth.HighPart = ftLastMonthLocal.dwHighDateTime;

    for (const auto& filter : activeFilters)
    {
        if (filter == L"Today" && uFile.QuadPart >= uMidnight.QuadPart) return true;
        if (filter == L"Yesterday" && uFile.QuadPart >= uYesterdayMidnight.QuadPart && uFile.QuadPart < uMidnight.QuadPart) return true;
        if (filter == L"Last week" && uFile.QuadPart >= uLastWeekMidnight.QuadPart && uFile.QuadPart < uYesterdayMidnight.QuadPart) return true;
        if (filter == L"Earlier this month" && uFile.QuadPart >= uStartOfMonth.QuadPart && uFile.QuadPart < uLastWeekMidnight.QuadPart) return true;
        if (filter == L"Last month" && uFile.QuadPart >= uLastMonth.QuadPart && uFile.QuadPart < uStartOfMonth.QuadPart) return true;
        if (filter == L"A long time ago" && uFile.QuadPart < uLastMonth.QuadPart) return true;
    }
    return false;
}

static void PopulateActivityConsistList()
{
    if (!g_hConsistList) return;
    g_ConsistList.Clear();

    int totalConsists = 0;
    int brokenConsists = 0;

    const auto& nameFilters   = g_ConsistList.GetActiveFilters(0);
    const auto& unitsFilters  = g_ConsistList.GetActiveFilters(1);
    const auto& statusFilters = g_ConsistList.GetActiveFilters(2);

    for (size_t i = 0; i < g_CurrentActivityData.consists.size(); ++i)
    {
        const auto& con = g_CurrentActivityData.consists[i];

        // Filter 0: Name (Letter filter)
        if (!MatchLetterFilter(con.name, nameFilters))
            continue;

        // Filter 1: Units count
        if (!MatchUnitsFilter(con.totalUnits, unitsFilters))
            continue;

        // Filter 2: Status (Healthy, Missing Stock, Missing Shape, Fixed)
        std::wstring consistKey = g_CurrentActivityFilePath + L"#" + con.id;
        auto itSess = g_ConsistSessions.find(consistKey);
        std::wstring statusStr;
        if (itSess != g_ConsistSessions.end())
        {
            statusStr = EvaluateAndUpdateConsistStatus(consistKey, itSess->second.units, !itSess->second.isDirty);
        }
        else
        {
            bool isFixed = (!con.isBroken && g_SessionFixedConsists.count(consistKey) > 0);
            if (isFixed)
                statusStr = L"Fixed";
            else if (con.hasMissingStock || (con.isBroken && !con.hasMissingShape))
                statusStr = L"Missing Stock";
            else if (con.hasMissingShape)
                statusStr = L"Missing Shape";
            else
                statusStr = L"Healthy";
        }

        if (!statusFilters.empty())
        {
            if (std::find(statusFilters.begin(), statusFilters.end(), statusStr) == statusFilters.end())
                continue;
        }

        std::wstring displayName = con.name;
        if (con.isDirty || (itSess != g_ConsistSessions.end() && itSess->second.isDirty))
        {
            displayName = L"● " + displayName;
        }

        std::wstring idxStr = std::to_wstring(i);
        g_ConsistList.AddItem({ displayName, std::to_wstring(con.totalUnits), statusStr, idxStr });

        totalConsists++;
        if (statusStr == L"Missing Stock" || statusStr == L"Missing Shape" || statusStr == L"Broken")
            brokenConsists++;
    }

    std::wstring actDispName = g_CurrentActivityData.fileName.empty() ? L"Activity" : g_CurrentActivityData.fileName;
    std::wstring headerText = L"  Activity Consists [ " + actDispName + L" \x2022 Consists: " +
        std::to_wstring(totalConsists) + L" \x2022 Broken: " +
        std::to_wstring(brokenConsists) + L" ]";
    SetWindowTextW(g_hConsistHeader, headerText.c_str());

    int curSortCol = g_ConsistList.GetSortColumn();
    if (curSortCol >= 0 && curSortCol < 3)
    {
        g_ConsistList.SortByColumn(curSortCol, false);
    }
}



DWORD WINAPI ConsistScannerThreadProc(LPVOID lpParam)
{
    ScanThreadParams* params = (ScanThreadParams*)lpParam;
    HWND hWndParent = params->hWndParent;
    std::wstring basePath = params->szPath;
    std::wstring searchFilter = params->szSearch;
    delete params;

    if (!basePath.empty() && basePath.back() != L'\\')
    {
        basePath += L'\\';
    }

    std::wstring searchPath = basePath + L"TRAINS\\CONSISTS\\*.con";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (g_bCancelScan) break;

            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wstring fileFullPath = basePath + L"TRAINS\\CONSISTS\\" + ffd.cFileName;
                
                std::string ascii = ReadConFileToAscii(fileFullPath);
                int nUnits = CountCarsInAscii(ascii);
                std::wstring szConsistName = ExtractConsistName(ascii);

                if (szConsistName.empty())
                {
                    std::wstring fName = ffd.cFileName;
                    size_t lastDot = fName.find_last_of(L'.');
                    if (lastDot != std::wstring::npos)
                    {
                        fName = fName.substr(0, lastDot);
                    }
                    szConsistName = fName;
                }

                // Check search filter match (against filename or internal name)
                if (!searchFilter.empty() && 
                    !StringContainsIgnoreCase(ffd.cFileName, searchFilter) &&
                    !StringContainsIgnoreCase(szConsistName, searchFilter))
                {
                    continue;
                }

                // Health check: verify all engines/wagons and shapes exist on disk
                bool hasMissingStock = false;
                bool hasMissingShape = false;
                try
                {
                    ConsistReader::ConsistData conData = ConsistReader::LoadConsist(fileFullPath);
                    for (const auto& unit : conData.units)
                    {
                        UnitHealthStatus h = GetUnitHealthOnDisk(unit, basePath);
                        if (h == UnitHealthStatus::MissingStock)
                        {
                            hasMissingStock = true;
                            break;
                        }
                        else if (h == UnitHealthStatus::MissingShape)
                        {
                            hasMissingShape = true;
                        }
                    }
                }
                catch (...)
                {
                    hasMissingStock = true;
                }
                bool isBroken = (hasMissingStock || hasMissingShape);

                ScannedConsist* pConsist = new ScannedConsist();
                pConsist->szFileName = ffd.cFileName;
                pConsist->szName = szConsistName;
                pConsist->nUnits = nUnits;
                pConsist->szLastModified = FormatFileTimeFriendly(ffd.ftLastWriteTime);
                pConsist->ftLastWriteTime = ffd.ftLastWriteTime;
                pConsist->isBroken = isBroken;
                pConsist->hasMissingStock = hasMissingStock;
                pConsist->hasMissingShape = hasMissingShape;
                if (isBroken)
                {
                    g_InitiallyBrokenConsists.insert(ffd.cFileName);
                }

                PostMessageW(hWndParent, WM_ADD_CONSIST_ITEM, 0, (LPARAM)pConsist);
            }
        } while (FindNextFileW(hFind, &ffd) != 0);

        FindClose(hFind);
    }

    PostMessageW(hWndParent, WM_CONSIST_SCAN_COMPLETE, 0, 0);
    return 0;
}

static void ResetConsistEditorWorkspace(HWND hWnd)
{
    // Reset editor pane to placeholder state
    if (g_hEditorPane)       ShowWindow(g_hEditorPane,       SW_SHOW);
    if (g_hSectionTrainCfg)  ShowWindow(g_hSectionTrainCfg,  SW_HIDE);
    if (g_hSectionUnits)     ShowWindow(g_hSectionUnits,     SW_HIDE);
    if (g_hLabelFileName)    ShowWindow(g_hLabelFileName,    SW_HIDE);
    if (g_hEditFileName)     ShowWindow(g_hEditFileName,     SW_HIDE);
    if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId,  SW_HIDE);
    if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,   SW_HIDE);
    if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_HIDE);
    if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_HIDE);
    if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_HIDE);
    if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_HIDE);
    if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_HIDE);
    if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_HIDE);
    if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_HIDE);
    if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_HIDE);
    if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_HIDE);
    if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_HIDE);
    if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_HIDE);
    if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_HIDE);
    if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_HIDE);
    if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_HIDE);
    if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_HIDE);
    if (g_hUnitPreviewCard)
    {
        ShowWindow(g_hUnitPreviewCard, SW_HIDE);
        UnitPreviewCard_Clear(g_hUnitPreviewCard);
    }
    if (g_hEditorUnitList)   ShowWindow(g_hEditorUnitList,   SW_HIDE);
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, {}, g_szBasePath, L"", L"");
        ShowWindow(g_hVisualConsistView, SW_SHOW);
    }

    // Clear active loaded consist state
    g_LoadedConsistUnits.clear();
    g_CheckedConsistUnits.clear();
    g_CurrentActivityConsistIndex = -1;
    g_szCurrentConsistFile.clear();
    g_UndoStack.clear();
    g_RedoStack.clear();

    if (hWnd)
    {
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

static void TriggerConsistsRescan(HWND hWnd)
{
    if (g_szBasePath.empty()) return;

    // Clear existing list and cache
    g_ScannedConsistsCache.clear();
    if (g_hConsistList)
    {
        g_ConsistList.Clear();
    }

    // Cancel existing scan
    g_bCancelScan = TRUE;
    if (g_hScanThread != NULL)
    {
        WaitForSingleObject(g_hScanThread, 200);
        CloseHandle(g_hScanThread);
        g_hScanThread = NULL;
    }
    g_bCancelScan = FALSE;

    ScanThreadParams* params = new ScanThreadParams();
    params->hWndParent = hWnd;
    params->szPath = g_szBasePath;
    params->szSearch = g_szConsistSearchQuery;

    g_hScanThread = CreateThread(NULL, 0, ConsistScannerThreadProc, params, 0, NULL);
}

std::wstring GetAppConsistsDirectory()
{
    if (g_szBasePath.empty()) return L"";
    std::wstring p = g_szBasePath;
    if (p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"TRAINS\\CONSISTS\\";
    return p;
}

std::wstring EnsureConsistFilePath(const std::wstring& basePath, const std::wstring& fileName)
{
    if (fileName.empty()) return L"";
    if (fileName.find(L':') != std::wstring::npos || (fileName.length() >= 2 && fileName[0] == L'\\' && fileName[1] == L'\\'))
    {
        std::wstring p = fileName;
        if (p.length() < 4 || _wcsicmp(p.c_str() + p.length() - 4, L".con") != 0)
        {
            p += L".con";
        }
        return p;
    }
    if (basePath.empty()) return fileName;
    std::wstring p = basePath;
    if (p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"TRAINS\\CONSISTS\\";
    p += fileName;
    if (p.length() < 4 || _wcsicmp(p.c_str() + p.length() - 4, L".con") != 0)
    {
        p += L".con";
    }
    return p;
}

void TriggerAppConsistsRescan(HWND hWnd)
{
    if (!hWnd && g_hConsistList) hWnd = GetAncestor(g_hConsistList, GA_ROOT);
    TriggerConsistsRescan(hWnd);
}

DWORD WINAPI ConsistWatcherThreadProc(LPVOID lpParam)

{
    HWND hWndParent = (HWND)lpParam;

    while (!g_bCancelWatcher)
    {
        if (g_szBasePath.empty())
        {
            Sleep(100);
            continue;
        }

        std::wstring watchDir = g_szBasePath;
        if (watchDir.back() != L'\\')
            watchDir += L'\\';
        watchDir += L"TRAINS\\CONSISTS";

        g_hDirHandle = CreateFileW(
            watchDir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL
        );

        if (g_hDirHandle == INVALID_HANDLE_VALUE)
        {
            Sleep(1000); // Retry later if directory is not created yet
            continue;
        }

        BYTE buffer[1024];
        DWORD bytesReturned;

        while (!g_bCancelWatcher)
        {
            BOOL success = ReadDirectoryChangesW(
                g_hDirHandle,
                buffer,
                sizeof(buffer),
                FALSE, // Do not watch subdirectories
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytesReturned,
                NULL,
                NULL
            );

            if (!success)
            {
                // If CancelIoEx was called or handle closed, success will be FALSE
                break;
            }

            if (g_bCancelWatcher)
                break;

            if (g_bIgnoreWatcher)
                continue;

            // Debounce rescan
            PostMessageW(hWndParent, WM_REQUEST_DEBOUNCE_RESCAN, 0, 0);
        }

        CloseHandle(g_hDirHandle);
        g_hDirHandle = INVALID_HANDLE_VALUE;
    }

    return 0;
}



// ---------------------------------------------------------------------------
// ModernEditSubclassProc – Win11-style flat edit field renderer
//   • Normal : thin 1-px border  (subtle, not raised/sunken)
static HFONT CreateMdl2IconFont(float pointSize, int weight = FW_NORMAL)
{
    return TSCB_CreateIconFont(pointSize, weight, GetDpiForSystem());
}

static HFONT g_hStockMdl2Font = NULL;

// ModernEditSubclassProc: Windows 11 rounded-corner flat border & accent focus for Edit controls
static LRESULT CALLBACK ModernEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_SETFOCUS:
    {
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (!isReadOnly)
        {
            g_hFocusedEdit = hWnd;
        }
        else
        {
            g_hFocusedEdit = NULL;
            HideCaret(hWnd);
        }
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_KILLFOCUS:
        if (g_hFocusedEdit == hWnd) g_hFocusedEdit = NULL;
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;

    case WM_SETCURSOR:
    {
        if (uIdSubclass >= 40 && uIdSubclass <= 42)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            RECT rc;
            GetClientRect(hWnd, &rc);
            RECT rcIcon = { rc.right - 26, rc.top, rc.right - 4, rc.bottom };
            if (GetWindowTextLengthW(hWnd) > 0 && PtInRect(&rcIcon, pt))
            {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (isReadOnly)
        {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        if (uIdSubclass >= 40 && uIdSubclass <= 42)
        {
            POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
            RECT rc;
            GetClientRect(hWnd, &rc);
            RECT rcIcon = { rc.right - 26, rc.top, rc.right - 4, rc.bottom };
            if (GetWindowTextLengthW(hWnd) > 0 && PtInRect(&rcIcon, pt))
            {
                SetWindowTextW(hWnd, L"");
                InvalidateRect(hWnd, NULL, TRUE);
                UpdateWindow(hWnd);
                int paneIdx = (int)(uIdSubclass - 40);
                if (paneIdx >= 0 && paneIdx < 3)
                {
                    g_szPaneSearchQuery[paneIdx] = L"";
                    PopulateAssetGridPane(paneIdx);
                }
                SetFocus(hWnd);
                return 0;
            }
        }
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (isReadOnly)
        {
            LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HideCaret(hWnd);
            return lr;
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_SIZE:
        InvalidateRect(hWnd, NULL, TRUE);
        break;

    case WM_GETDLGCODE:
        if (uIdSubclass >= 40 && uIdSubclass <= 42)
        {
            if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN && ((MSG*)lParam)->wParam == VK_RETURN)
                return DLGC_WANTALLKEYS;
        }
        break;

    case WM_CHAR:
    {
        if (uIdSubclass >= 40 && uIdSubclass <= 42)
        {
            if (wParam == VK_RETURN)
                return 0;
        }

        // Strict numeric-only validation for Max Velocity (11) and Performance Factor (12)
        if (uIdSubclass == 11 || uIdSubclass == 12)
        {
            wchar_t ch = (wchar_t)wParam;
            // Allow control keys (backspace, enter, escape, Ctrl combinations)
            if (ch < 32)
            {
                // allow control character
            }
            else if (ch >= L'0' && ch <= L'9')
            {
                // allow digit
            }
            else if (ch == L'.')
            {
                // Allow only one decimal point
                wchar_t szBuf[128] = { 0 };
                GetWindowTextW(hWnd, szBuf, 128);
                DWORD dwSel = (DWORD)SendMessageW(hWnd, EM_GETSEL, 0, 0);
                int selStart = LOWORD(dwSel);
                int selEnd = HIWORD(dwSel);

                wchar_t* pDot = wcschr(szBuf, L'.');
                if (pDot)
                {
                    int dotPos = (int)(pDot - szBuf);
                    // If selection covers the existing dot, allow replacement; otherwise block
                    if (!(selStart <= dotPos && selEnd > dotPos))
                    {
                        return 0; // block extra decimal points
                    }
                }
            }
            else
            {
                // Block letters, spaces, symbols
                return 0;
            }
        }

        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_PASTE:
    {
        if (uIdSubclass == 11 || uIdSubclass == 12)
        {
            if (OpenClipboard(hWnd))
            {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData)
                {
                    wchar_t* pClip = (wchar_t*)GlobalLock(hData);
                    if (pClip)
                    {
                        std::wstring filtered;
                        bool hasDot = false;
                        for (size_t i = 0; pClip[i] != L'\0'; ++i)
                        {
                            if (pClip[i] >= L'0' && pClip[i] <= L'9') filtered += pClip[i];
                            else if (pClip[i] == L'.' && !hasDot) { filtered += L'.'; hasDot = true; }
                        }
                        GlobalUnlock(hData);
                        CloseClipboard();
                        if (!filtered.empty())
                        {
                            SendMessageW(hWnd, EM_REPLACESEL, TRUE, (LPARAM)filtered.c_str());
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }
                }
                CloseClipboard();
            }
        }
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_KEYUP:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_KEYDOWN:
        if (uIdSubclass >= 40 && uIdSubclass <= 42)
        {
            if (wParam == VK_RETURN)
                return 0;
            if (wParam == VK_ESCAPE)
            {
                SetWindowTextW(hWnd, L"");
                InvalidateRect(hWnd, NULL, TRUE);
                UpdateWindow(hWnd);
                int paneIdx = (int)(uIdSubclass - 40);
                if (paneIdx >= 0 && paneIdx < 3)
                {
                    g_szPaneSearchQuery[paneIdx] = L"";
                    PopulateAssetGridPane(paneIdx);
                }
                return 0;
            }
        }
        break;

    case WM_MOUSEMOVE:
    {
        if (uIdSubclass >= 40 && uIdSubclass <= 42 && GetWindowTextLengthW(hWnd) > 0)
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        if (g_hHoveredEdit != hWnd)
        {
            g_hHoveredEdit = hWnd;
            
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }
    
    case WM_MOUSELEAVE:
    {
        if (g_hHoveredEdit == hWnd)
        {
            g_hHoveredEdit = NULL;
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right;
            int height = rc.bottom;

            if (width > 0 && height > 0)
            {
                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

                bool focused = (g_hFocusedEdit == hWnd);
                bool hovered = (g_hHoveredEdit == hWnd);
                bool hasText = (GetWindowTextLengthW(hWnd) > 0);

                // 1. Fill parent workspace background to anti-alias corners
                HBRUSH hbrParent = CreateSolidBrush(UITheme::DarkBackground);
                FillRect(hMemDC, &rc, hbrParent);
                DeleteObject(hbrParent);

                // 2. Select edit box background color based on active state (Win11 Explorer spec)
                COLORREF clrBg = focused ? RGB(30, 30, 30) : (hovered ? RGB(45, 45, 45) : RGB(38, 38, 38));
                COLORREF clrBorder = focused ? RGB(70, 70, 70) : (hovered ? RGB(65, 65, 65) : RGB(50, 50, 50));

                HBRUSH hbrBg = CreateSolidBrush(clrBg);
                HPEN hPen = CreatePen(PS_SOLID, 1, clrBorder);
                HGDIOBJ hOldBr = SelectObject(hMemDC, hbrBg);
                HGDIOBJ hOldP = SelectObject(hMemDC, hPen);

                RoundRect(hMemDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

                SelectObject(hMemDC, hOldP);
                SelectObject(hMemDC, hOldBr);
                DeleteObject(hPen);
                DeleteObject(hbrBg);

                // 3. Render edit control text onto memory DC (clipped cleanly within rounded boundaries)
                HRGN hRgnClip = CreateRoundRectRgn(rc.left + 1, rc.top + 1, rc.right, rc.bottom, 6, 6);
                SelectClipRgn(hMemDC, hRgnClip);
                DefSubclassProc(hWnd, WM_PRINTCLIENT, (WPARAM)hMemDC, PRF_CLIENT);
                SelectClipRgn(hMemDC, NULL);
                DeleteObject(hRgnClip);

                // 4. For search controls: draw placeholder when empty and unfocused, plus right icon (clear '✕' \xE711 or search glass \xE721)
                if (uIdSubclass >= 40 && uIdSubclass <= 42)
                {
                    if (!focused && !hasText)
                    {
                        RECT rcText = rc;
                        rcText.left += 8;
                        rcText.right -= 26;
                        SetTextColor(hMemDC, RGB(140, 140, 148));
                        SetBkMode(hMemDC, TRANSPARENT);
                        HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
                        if (!hFont) hFont = hUIFont;
                        HGDIOBJ hOldF = SelectObject(hMemDC, hFont ? hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        DrawTextW(hMemDC, L"Search...", -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        SelectObject(hMemDC, hOldF);
                    }

                    // Right icon: Native Segoe MDL2 Assets / Fluent icons from NavToolbar
                    if (!g_hStockMdl2Font)
                    {
                        g_hStockMdl2Font = CreateMdl2IconFont(9.5f, FW_NORMAL);
                    }

                    RECT rcIcon = { rc.right - 26, rc.top, rc.right - 4, rc.bottom };
                    SetBkMode(hMemDC, TRANSPARENT);
                    HGDIOBJ hOldIconFont = SelectObject(hMemDC, g_hStockMdl2Font ? g_hStockMdl2Font : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

                    if (hasText)
                    {
                        POINT ptCursor;
                        GetCursorPos(&ptCursor);
                        ScreenToClient(hWnd, &ptCursor);
                        bool hoverClear = PtInRect(&rcIcon, ptCursor);

                        // Draw Clear '✕' (\xE711) button
                        SetTextColor(hMemDC, hoverClear ? RGB(255, 255, 255) : RGB(200, 200, 200));
                        DrawTextW(hMemDC, L"\xE711", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    else
                    {
                        // Draw Search Glass facing leftwards (horizontally flipped around icon center)
                        SetTextColor(hMemDC, RGB(180, 180, 185));
                        int prevMode = SetGraphicsMode(hMemDC, GM_ADVANCED);
                        float cx = (rcIcon.left + rcIcon.right) / 2.0f;
                        XFORM xf = { -1.0f, 0.0f, 0.0f, 1.0f, 2.0f * cx, 0.0f };
                        SetWorldTransform(hMemDC, &xf);

                        DrawTextW(hMemDC, L"\xE721", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                        ModifyWorldTransform(hMemDC, NULL, MWT_IDENTITY);
                        SetGraphicsMode(hMemDC, prevMode);
                    }

                    SelectObject(hMemDC, hOldIconFont);
                }

                // 5. Draw focused accent blue bottom line
                bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
                if (focused && !isReadOnly)
                {
                    HPEN hAccent = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
                    HGDIOBJ hOldA = SelectObject(hMemDC, hAccent);
                    MoveToEx(hMemDC, rc.left + 4, rc.bottom - 2, NULL);
                    LineTo(hMemDC, rc.right - 4, rc.bottom - 2);
                    SelectObject(hMemDC, hOldA);
                    DeleteObject(hAccent);
                }

                // 6. Blit entire frame to screen in one single operation
                BitBlt(hdc, 0, 0, width, height, hMemDC, 0, 0, SRCCOPY);

                SelectObject(hMemDC, hOldBmp);
                DeleteObject(hMemBmp);
                DeleteDC(hMemDC);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, ModernEditSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// SectionUnitsHeaderSubclassProc – Fluent Card Header Bar for Consist Units
static bool s_hoverDelUnitsBtn = false;
static bool s_pressedDelUnitsBtn = false;
static RECT s_rcDelUnitsBtn = { 0, 0, 0, 0 };

static RECT GetSectionUnitsDelBtnRect(HWND hWnd)
{
    if (s_rcDelUnitsBtn.right > s_rcDelUnitsBtn.left)
    {
        return s_rcDelUnitsBtn;
    }

    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rc);
    wchar_t szText[128] = { 0 };
    GetWindowTextW(hWnd, szText, 128);

    int textWidth = 260;
    HDC hdc = GetDC(hWnd);
    if (hdc)
    {
        HFONT hFont = g_hSectionFont ? g_hSectionFont : (hUIFont ? hUIFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        SIZE sz = { 0, 0 };
        if (szText[0] != L'\0' && GetTextExtentPoint32W(hdc, szText, (int)wcslen(szText), &sz))
        {
            textWidth = sz.cx;
        }
        SelectObject(hdc, hOld);
        ReleaseDC(hWnd, hdc);
    }

    int btnW = 82;
    int btnH = 22;
    int btnX = 14 + textWidth + 18;
    if (btnX + btnW > rc.right - 8) btnX = rc.right - btnW - 8;
    int btnY = (rc.bottom - btnH) / 2;
    RECT rcDelBtn = { btnX, btnY, btnX + btnW, btnY + btnH };
    return rcDelBtn;
}

static LRESULT CALLBACK SectionUnitsHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETTEXT:
    {
        s_rcDelUnitsBtn = { 0, 0, 0, 0 };
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_SETCURSOR:
    {
        RECT rcDelBtn = GetSectionUnitsDelBtnRect(hWnd);
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        if (PtInRect(&rcDelBtn, pt))
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        RECT rcDelBtn = GetSectionUnitsDelBtnRect(hWnd);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool hover = PtInRect(&rcDelBtn, pt);
        if (hover != s_hoverDelUnitsBtn)
        {
            s_hoverDelUnitsBtn = hover;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (s_hoverDelUnitsBtn || s_pressedDelUnitsBtn)
        {
            s_hoverDelUnitsBtn = false;
            s_pressedDelUnitsBtn = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        HWND hParent = GetParent(hWnd);
        SetActivePane(PANE_WORKSPACE, hParent);
        if (g_hEditorUnitList) SetFocus(g_hEditorUnitList);

        RECT rcDelBtn = GetSectionUnitsDelBtnRect(hWnd);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&rcDelBtn, pt))
        {
            s_pressedDelUnitsBtn = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (s_pressedDelUnitsBtn)
        {
            ReleaseCapture();
            s_pressedDelUnitsBtn = false;
            InvalidateRect(hWnd, NULL, FALSE);
            RECT rcDelBtn = GetSectionUnitsDelBtnRect(hWnd);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&rcDelBtn, pt))
            {
                HWND hRoot = GetAncestor(hWnd, GA_ROOT);
                DeleteSelectedConsistUnits(hRoot);
            }
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        COLORREF bgHeader = RGB(32, 32, 32);
        COLORREF borderCol = RGB(55, 55, 55);

        HBRUSH hbr = CreateSolidBrush(bgHeader);
        FillRect(hMemDC, &rc, hbr);
        DeleteObject(hbr);

        // Draw top, left, right borders and bottom divider
        HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenBorder);

        // Top border
        MoveToEx(hMemDC, 0, 0, NULL);
        LineTo(hMemDC, rc.right, 0);

        // Left border
        MoveToEx(hMemDC, 0, 0, NULL);
        LineTo(hMemDC, 0, rc.bottom);

        // Right border
        MoveToEx(hMemDC, rc.right - 1, 0, NULL);
        LineTo(hMemDC, rc.right - 1, rc.bottom);

        // Bottom divider separating header from unit list
        MoveToEx(hMemDC, 0, rc.bottom - 1, NULL);
        LineTo(hMemDC, rc.right, rc.bottom - 1);

        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPenBorder);

        // Text
        wchar_t szText[128] = { 0 };
        GetWindowTextW(hWnd, szText, 128);

        HFONT hFont = g_hSectionFont ? g_hSectionFont : (hUIFont ? hUIFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        HFONT hOldFont = NULL;
        if (hFont) hOldFont = (HFONT)SelectObject(hMemDC, hFont);

        SIZE sz = { 0, 0 };
        int textWidth = 260;
        if (szText[0] != L'\0' && GetTextExtentPoint32W(hMemDC, szText, (int)wcslen(szText), &sz))
        {
            textWidth = sz.cx;
        }

        int btnW = 82;
        int btnH = 22;
        int btnX = 14 + textWidth + 18;
        if (btnX + btnW > rc.right - 8) btnX = rc.right - btnW - 8;
        int btnY = (rc.bottom - btnH) / 2;
        s_rcDelUnitsBtn = { btnX, btnY, btnX + btnW, btnY + btnH };
        RECT rcDelBtn = s_rcDelUnitsBtn;

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(240, 240, 240));

        RECT rcText = { 12, 0, rcDelBtn.left - 8, rc.bottom };
        DrawTextW(hMemDC, szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Render [  Delete ] Button adjacent to title text
        COLORREF btnBg = s_pressedDelUnitsBtn ? RGB(110, 25, 35) : (s_hoverDelUnitsBtn ? RGB(80, 22, 28) : RGB(42, 42, 44));
        COLORREF btnBorder = s_pressedDelUnitsBtn ? RGB(160, 40, 50) : (s_hoverDelUnitsBtn ? RGB(160, 50, 60) : RGB(65, 65, 68));
        COLORREF btnTextCol = s_hoverDelUnitsBtn ? RGB(255, 255, 255) : RGB(220, 180, 185);

        HBRUSH hbrBtn = CreateSolidBrush(btnBg);
        HPEN hpenBtn = CreatePen(PS_SOLID, 1, btnBorder);
        HGDIOBJ oldB = SelectObject(hMemDC, hbrBtn);
        HGDIOBJ oldP = SelectObject(hMemDC, hpenBtn);

        RoundRect(hMemDC, rcDelBtn.left, rcDelBtn.top, rcDelBtn.right, rcDelBtn.bottom, 4, 4);

        SelectObject(hMemDC, oldB);
        SelectObject(hMemDC, oldP);
        DeleteObject(hbrBtn);
        DeleteObject(hpenBtn);

        // Draw Trash Icon (\xE74D) and "Delete" Text
        if (!g_hStockMdl2Font)
        {
            g_hStockMdl2Font = CreateMdl2IconFont(9.5f, FW_NORMAL);
        }

        RECT rcIcon = { rcDelBtn.left + 6, rcDelBtn.top, rcDelBtn.left + 22, rcDelBtn.bottom };
        SelectObject(hMemDC, g_hStockMdl2Font ? g_hStockMdl2Font : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hMemDC, btnTextCol);
        DrawTextW(hMemDC, L"\xE74D", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT rcBtnLabel = { rcDelBtn.left + 24, rcDelBtn.top, rcDelBtn.right - 6, rcDelBtn.bottom };
        SelectObject(hMemDC, hFont ? hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(hMemDC, L"Delete", -1, &rcBtnLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, SectionUnitsHeaderSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SetActivePane(ActivePane newPane, HWND hWnd)
{
    if (g_ActivePane != newPane)
    {
        g_ActivePane = newPane;
        if (g_hConsistHeader != NULL) InvalidateRect(g_hConsistHeader, NULL, TRUE);
        if (g_hStockHeader != NULL) InvalidateRect(g_hStockHeader, NULL, TRUE);
        if (g_hWorkspaceHeader != NULL) InvalidateRect(g_hWorkspaceHeader, NULL, TRUE);
        if (hWnd != NULL) InvalidateRect(hWnd, NULL, TRUE);

        if (newPane == PANE_CONSIST)
        {
            NavToolbar_SetSearchQuery(g_hNavToolbar, g_szConsistSearchQuery.c_str());
        }
        else if (newPane == PANE_STOCK)
        {
            NavToolbar_SetSearchQuery(g_hNavToolbar, g_szStockSearchQuery.c_str());
        }
        UpdateUnitPreviewFromSelected();
    }
}

LRESULT CALLBACK ConsistHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NCHITTEST)
    {
        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Rightmost 6 pixels are transparent for Splitter 1
        if (pt.x >= rcClient.right - 6)
        {
            return HTTRANSPARENT;
        }
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        HWND hParent = GetParent(hWnd);
        SetActivePane(PANE_CONSIST, hParent);
        if (g_hConsistList) SetFocus(g_hConsistList);
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, ConsistHeaderSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WorkspaceHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NCHITTEST)
    {
        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);

        // Leftmost 6 pixels are transparent to let parent splitter hit-test work
        if (pt.x <= 6)
        {
            return HTTRANSPARENT;
        }
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        HWND hParent = GetParent(hWnd);
        SetActivePane(PANE_WORKSPACE, hParent);
        if (g_hEditorUnitList) SetFocus(g_hEditorUnitList);
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, WorkspaceHeaderSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK EditorPaneSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NCHITTEST)
    {
        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);

        // Leftmost 6 pixels are transparent to let parent splitter hit-test work
        if (pt.x <= 6)
        {
            return HTTRANSPARENT;
        }
    }
    else if (uMsg == WM_LBUTTONDOWN)
    {
        HWND hParent = GetParent(hWnd);
        SetActivePane(PANE_WORKSPACE, hParent);
        if (g_hEditorUnitList) SetFocus(g_hEditorUnitList);
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, EditorPaneSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static int s_hoverPaneCatBtn = -1;
static int s_pressedPaneCatBtn = -1;

static CustomListControl* GetAssetListCtrl(int paneIdx)
{
    if (paneIdx == 0) return &g_AssetList;
    if (paneIdx == 1) return &g_AssetList2;
    if (paneIdx == 2) return &g_AssetList3;
    return &g_AssetList;
}

static HWND GetAssetListHwnd(int paneIdx)
{
    if (paneIdx == 0) return g_hAssetList;
    if (paneIdx == 1) return g_hAssetList2;
    if (paneIdx == 2) return g_hAssetList3;
    return g_hAssetList;
}

static LRESULT CALLBACK PaneCatBtnSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    int paneIdx = (int)dwRefData;

    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_SETTEXT:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_SETCURSOR:
    {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    }

    case WM_MOUSEMOVE:
    {
        if (s_hoverPaneCatBtn != paneIdx)
        {
            s_hoverPaneCatBtn = paneIdx;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (s_hoverPaneCatBtn == paneIdx)
        {
            s_hoverPaneCatBtn = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        s_pressedPaneCatBtn = paneIdx;
        InvalidateRect(hWnd, NULL, FALSE);

        // Show Category Selection Context Menu directly below this button
        RECT rcBtn;
        GetWindowRect(hWnd, &rcBtn);

        std::vector<ContextMenuItem> items = {
            ContextMenuItem::Action((int)StockCategoryFilter::AllEngines, L"\xE7C3", L"All Engines", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Diesel,     L"\xE7C3", L"Diesel", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Electric,   L"\xE7C3", L"Electric", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Steam,      L"\xE7C3", L"Steam", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Control,    L"\xE7C3", L"Control / Cab", L"", true),
            ContextMenuItem::Separator(),
            ContextMenuItem::Action((int)StockCategoryFilter::AllWagons,  L"\xE7C3", L"All Wagons", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Passenger,  L"\xE7C3", L"Passenger Wagons", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Freight,    L"\xE7C3", L"Freight Wagons", L"", true),
            ContextMenuItem::Action((int)StockCategoryFilter::Tender,     L"\xE7C3", L"Tenders", L"", true),
            ContextMenuItem::Separator(),
            ContextMenuItem::Action((int)StockCategoryFilter::AllStock,   L"\xE8B7", L"All Stock", L"", true)
        };

        s_pressedPaneCatBtn = -1;
        InvalidateRect(hWnd, NULL, FALSE);

        HWND hRoot = GetAncestor(hWnd, GA_ROOT);
        int cmd = ModernContextMenu::Show(hRoot, rcBtn.left, rcBtn.bottom + 2, items, TRUE, 220);
        if (cmd >= 0 && cmd <= 9)
        {
            g_PaneCategory[paneIdx] = (StockCategoryFilter)cmd;
            SetWindowTextW(hWnd, GetCategoryFilterLabel(g_PaneCategory[paneIdx]));
            PopulateAssetGridPane(paneIdx);
        }
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        bool isHovered = (s_hoverPaneCatBtn == paneIdx);
        bool isPressed = (s_pressedPaneCatBtn == paneIdx);

        COLORREF bgParent = UITheme::DarkBackground;
        HBRUSH hbrParent = CreateSolidBrush(bgParent);
        FillRect(hMemDC, &rc, hbrParent);
        DeleteObject(hbrParent);

        COLORREF bgBtn = isPressed ? RGB(32, 32, 36) : (isHovered ? RGB(52, 52, 58) : RGB(42, 42, 46));
        COLORREF borderBtn = isHovered ? RGB(90, 90, 98) : RGB(65, 65, 72);

        HBRUSH hbrBtn = CreateSolidBrush(bgBtn);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderBtn);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hbrBtn);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPen);

        RoundRect(hMemDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        SelectObject(hMemDC, hOldPen);
        SelectObject(hMemDC, hOldBr);
        DeleteObject(hPen);
        DeleteObject(hbrBtn);

        // Get text
        wchar_t szText[128] = { 0 };
        GetWindowTextW(hWnd, szText, 128);
        if (szText[0] == L'\0')
        {
            wcscpy_s(szText, GetCategoryFilterLabel(g_PaneCategory[paneIdx]));
        }

        HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
        if (!hFont) hFont = hUIFont;
        HFONT hOldFont = NULL;
        if (hFont) hOldFont = (HFONT)SelectObject(hMemDC, hFont);

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, isHovered ? RGB(255, 255, 255) : RGB(225, 225, 225));

        // Draw text
        RECT rcText = { rc.left + 8, rc.top, rc.right - 18, rc.bottom };
        DrawTextW(hMemDC, szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Draw crisp geometric dropdown arrow on the right
        int arrowX = rc.right - 11;
        int arrowY = (rc.top + rc.bottom) / 2 - 1;
        POINT arrowPts[3] = {
            { arrowX - 4, arrowY },
            { arrowX + 4, arrowY },
            { arrowX,     arrowY + 4 }
        };
        COLORREF arrowClr = isHovered ? RGB(255, 255, 255) : RGB(175, 175, 180);
        HBRUSH hbrArrow = CreateSolidBrush(arrowClr);
        HPEN hPenArrow = CreatePen(PS_SOLID, 1, arrowClr);
        HGDIOBJ hOldArrBr = SelectObject(hMemDC, hbrArrow);
        HGDIOBJ hOldArrPen = SelectObject(hMemDC, hPenArrow);
        Polygon(hMemDC, arrowPts, 3);
        SelectObject(hMemDC, hOldArrPen);
        SelectObject(hMemDC, hOldArrBr);
        DeleteObject(hPenArrow);
        DeleteObject(hbrArrow);

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, PaneCatBtnSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static int s_hoverStockHeaderBtn = -1; // 0 = Single, 1 = Dual, 2 = Triple

static RECT GetStockModeBtnRect(const RECT& rcClient, int modeIndex)
{
    // Mode buttons aligned to the right: Single, Dual, Triple
    const int BTN_H = 22;
    const int BTN_Y = (rcClient.bottom - BTN_H) / 2;
    const int BTN_W = 68;
    const int GAP = 4;
    const int RIGHT_MARGIN = 8;

    int rightX = rcClient.right - RIGHT_MARGIN;
    int btnX = rightX - (3 - modeIndex) * BTN_W - (2 - modeIndex) * GAP;

    RECT rcBtn = { btnX, BTN_Y, btnX + BTN_W, BTN_Y + BTN_H };
    return rcBtn;
}

static int HitTestStockModeBtn(HWND hWnd, POINT pt)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    for (int m = 0; m < 3; ++m)
    {
        RECT rcBtn = GetStockModeBtnRect(rcClient, m);
        if (PtInRect(&rcBtn, pt))
        {
            return m;
        }
    }
    return -1;
}

LRESULT CALLBACK StockHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_NCHITTEST:
    {
        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        POINT ptClient = pt;
        ScreenToClient(hWnd, &ptClient);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Check if cursor is over one of the mode pill buttons
        if (HitTestStockModeBtn(hWnd, ptClient) >= 0)
        {
            return HTCLIENT;
        }

        // Top 6 pixels for horizontal Splitter 2, rightmost 6 pixels for vertical Splitter 1
        if (ptClient.y <= 6 || ptClient.x >= rcClient.right - 6)
        {
            return HTTRANSPARENT;
        }
        return HTCLIENT;
    }

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        if (HitTestStockModeBtn(hWnd, pt) >= 0)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        int hit = HitTestStockModeBtn(hWnd, pt);
        if (hit != s_hoverStockHeaderBtn)
        {
            s_hoverStockHeaderBtn = hit;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (s_hoverStockHeaderBtn != -1)
        {
            s_hoverStockHeaderBtn = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        HWND hParent = GetParent(hWnd);
        SetActivePane(PANE_STOCK, hParent);
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        int hit = HitTestStockModeBtn(hWnd, pt);
        if (hit >= 0)
        {
            StockViewMode newMode = (StockViewMode)hit;
            if (newMode != g_StockViewMode)
            {
                g_StockViewMode = newMode;
                if (hParent)
                {
                    // Populate active pane(s)
                    if (g_StockViewMode == StockViewMode::Single)
                    {
                        CustomTreeNode* hSel = g_CategoryTreeView.GetSelectedNode();
                        PopulateAssetGridPane(0, hSel);
                    }
                    else if (g_StockViewMode == StockViewMode::Dual)
                    {
                        PopulateAssetGridPane(0);
                        PopulateAssetGridPane(1);
                    }
                    else if (g_StockViewMode == StockViewMode::Triple)
                    {
                        PopulateAssetGridPane(0);
                        PopulateAssetGridPane(1);
                        PopulateAssetGridPane(2);
                    }

                    RECT rcParent;
                    GetClientRect(hParent, &rcParent);
                    SendMessage(hParent, WM_SIZE, 0, MAKELPARAM(rcParent.right, rcParent.bottom));
                    InvalidateRect(hParent, NULL, TRUE);
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            return 0;
        }
        else
        {
            if (g_hAssetList) SetFocus(g_hAssetList);
        }
        break;
    }

    case WM_SIZE:
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SETTEXT:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        BOOL isActive = (g_ActivePane == PANE_STOCK);
        COLORREF bgHeader = isActive ? RGB(24, 60, 100) : UITheme::DarkHeaderBackground;
        HBRUSH hbr = CreateSolidBrush(bgHeader);
        FillRect(hMemDC, &rc, hbr);
        DeleteObject(hbr);

        // Header Title text
        wchar_t szText[256] = { 0 };
        GetWindowTextW(hWnd, szText, 256);

        HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
        if (!hFont) hFont = hUIFont;
        HFONT hOldFont = NULL;
        if (hFont) hOldFont = (HFONT)SelectObject(hMemDC, hFont);

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, isActive ? RGB(255, 255, 255) : UITheme::TextPrimary);

        // Compute title text rect ending before the mode buttons
        RECT rcBtnSingle = GetStockModeBtnRect(rc, 0);
        RECT rcText = { 10, 0, rcBtnSingle.left - 10, rc.bottom };
        DrawTextW(hMemDC, szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Draw 3 Mode Switch Pill Buttons: [ Single ] [ Dual ] [ Triple ]
        const wchar_t* btnLabels[3] = { L"Single", L"Dual", L"Triple" };

        for (int m = 0; m < 3; ++m)
        {
            RECT rcBtn = GetStockModeBtnRect(rc, m);
            bool isCurrent = (g_StockViewMode == (StockViewMode)m);
            bool isHovered = (s_hoverStockHeaderBtn == m);

            COLORREF bgPill = isCurrent ? RGB(0, 120, 215) : (isHovered ? RGB(60, 60, 68) : RGB(40, 40, 46));
            COLORREF borderPill = isCurrent ? RGB(60, 160, 255) : (isHovered ? RGB(85, 85, 95) : RGB(58, 58, 65));
            COLORREF textPill = isCurrent ? RGB(255, 255, 255) : (isHovered ? RGB(240, 240, 240) : RGB(180, 180, 185));

            HBRUSH hbrPill = CreateSolidBrush(bgPill);
            HPEN hPenPill = CreatePen(PS_SOLID, 1, borderPill);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hbrPill);
            HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenPill);

            RoundRect(hMemDC, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 6, 6);

            SelectObject(hMemDC, hOldP);
            SelectObject(hMemDC, hOldBr);
            DeleteObject(hPenPill);
            DeleteObject(hbrPill);

            SetTextColor(hMemDC, textPill);
            DrawTextW(hMemDC, btnLabels[m], -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, StockHeaderSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static bool MatchesCategoryFilter(const StockItem& item, StockCategoryFilter cat)
{
    switch (cat)
    {
    case StockCategoryFilter::AllEngines:
        return item.szExtension == L".eng" || item.szCategory == L"Diesel" || item.szCategory == L"Electric" || item.szCategory == L"Steam" || item.szCategory == L"Control";
    case StockCategoryFilter::Diesel:
        return item.szCategory == L"Diesel";
    case StockCategoryFilter::Electric:
        return item.szCategory == L"Electric";
    case StockCategoryFilter::Steam:
        return item.szCategory == L"Steam";
    case StockCategoryFilter::Control:
        return item.szCategory == L"Control";
    case StockCategoryFilter::AllWagons:
        return item.szExtension == L".wag" || item.szCategory == L"Freight" || item.szCategory == L"Passenger" || item.szCategory == L"Tender";
    case StockCategoryFilter::Passenger:
        return item.szCategory == L"Passenger";
    case StockCategoryFilter::Freight:
        return item.szCategory == L"Freight";
    case StockCategoryFilter::Tender:
        return item.szCategory == L"Tender";
    case StockCategoryFilter::AllStock:
    default:
        return true;
    }
}

static void SortAssetGridPane(int paneIdx)
{
    if (paneIdx < 0 || paneIdx >= 3) return;
    CustomListControl* pList = GetAssetListCtrl(paneIdx);
    if (!pList) return;

    int sortCol = pList->GetSortColumn();
    if (sortCol < 0) return;
    bool ascending = pList->IsSortAscending();

    auto getCategoryPriority = [](const std::wstring& cat) -> int {
        if (cat == L"Control") return 1;
        if (cat == L"Diesel") return 2;
        if (cat == L"Electric") return 3;
        if (cat == L"Steam") return 4;
        if (cat == L"Freight") return 5;
        if (cat == L"Passenger") return 6;
        if (cat == L"Tender") return 7;
        return 8;
    };

    EnterCriticalSection(&g_StockCacheCS);
    auto& indices = g_FilteredStockIndicesPane[paneIdx];
    std::stable_sort(indices.begin(), indices.end(), [&](size_t idxA, size_t idxB) {
        if (idxA >= g_StockCache.size() || idxB >= g_StockCache.size())
            return false;

        const StockItem& itemA = g_StockCache[idxA];
        const StockItem& itemB = g_StockCache[idxB];

        if (sortCol == 0) // Name
        {
            int cmp = StrCmpLogicalW(itemA.szFileName.c_str(), itemB.szFileName.c_str());
            if (cmp == 0) return false;
            return ascending ? (cmp < 0) : (cmp > 0);
        }
        else if (sortCol == 1) // Type
        {
            int pA = getCategoryPriority(itemA.szCategory);
            int pB = getCategoryPriority(itemB.szCategory);
            if (pA != pB)
            {
                return ascending ? (pA < pB) : (pA > pB);
            }
            int cmp = StrCmpLogicalW(itemA.szFileName.c_str(), itemB.szFileName.c_str());
            if (cmp == 0) return false;
            return ascending ? (cmp < 0) : (cmp > 0);
        }
        else if (sortCol == 2) // Folder
        {
            int cmp = StrCmpLogicalW(itemA.szFolder.c_str(), itemB.szFolder.c_str());
            if (cmp == 0)
            {
                cmp = StrCmpLogicalW(itemA.szFileName.c_str(), itemB.szFileName.c_str());
                if (cmp == 0) return false;
                return ascending ? (cmp < 0) : (cmp > 0);
            }
            return ascending ? (cmp < 0) : (cmp > 0);
        }
        return false;
    });
    LeaveCriticalSection(&g_StockCacheCS);
}

static void SortAssetGrid()
{
    SortAssetGridPane(0);
}

void PopulateAssetGridPane(int paneIdx, CustomTreeNode* hSelected)
{
    if (paneIdx < 0 || paneIdx >= 3) return;
    HWND hList = GetAssetListHwnd(paneIdx);
    CustomListControl* pList = GetAssetListCtrl(paneIdx);
    if (!hList || !pList) return;

    auto& indices = g_FilteredStockIndicesPane[paneIdx];

    EnterCriticalSection(&g_StockCacheCS);

    indices.clear();

    for (size_t i = 0; i < g_StockCache.size(); ++i)
    {
        const auto& item = g_StockCache[i];

        // In Single mode with pane 0, filter by category tree selection
        if (g_StockViewMode == StockViewMode::Single && paneIdx == 0)
        {
            if (!hSelected && g_hCategoryTree)
            {
                hSelected = g_CategoryTreeView.GetSelectedNode();
            }
            if (!hSelected)
            {
                continue;
            }

            BOOL isMatch = FALSE;
            if (hSelected == g_pNodeEnginesAll && item.szExtension == L".eng") isMatch = TRUE;
            else if (hSelected == g_pNodeWagonsAll && item.szExtension == L".wag") isMatch = TRUE;
            else if (hSelected == g_pNodeDiesel && item.szCategory == L"Diesel") isMatch = TRUE;
            else if (hSelected == g_pNodeElectric && item.szCategory == L"Electric") isMatch = TRUE;
            else if (hSelected == g_pNodeSteam && item.szCategory == L"Steam") isMatch = TRUE;
            else if (hSelected == g_pNodeControl && item.szCategory == L"Control") isMatch = TRUE;
            else if (hSelected == g_pNodePassenger && item.szCategory == L"Passenger") isMatch = TRUE;
            else if (hSelected == g_pNodeFreight && item.szCategory == L"Freight") isMatch = TRUE;
            else if (hSelected == g_pNodeTender && item.szCategory == L"Tender") isMatch = TRUE;

            if (!isMatch) continue;

            // Search query filter in Single mode (check pane search edit query or global query)
            const std::wstring& qSingle = !g_szPaneSearchQuery[0].empty() ? g_szPaneSearchQuery[0] : g_szStockSearchQuery;
            if (!qSingle.empty())
            {
                if (!StringContainsIgnoreCase(item.szFileName, qSingle) &&
                    !StringContainsIgnoreCase(item.szFolder, qSingle))
                {
                    continue;
                }
            }
        }
        else
        {
            // In Dual / Triple mode (or pane 1/2), filter by pane's assigned category
            if (!MatchesCategoryFilter(item, g_PaneCategory[paneIdx]))
            {
                continue;
            }

            // Filter by pane's dedicated search box query
            if (!g_szPaneSearchQuery[paneIdx].empty())
            {
                if (!StringContainsIgnoreCase(item.szFileName, g_szPaneSearchQuery[paneIdx]) &&
                    !StringContainsIgnoreCase(item.szFolder, g_szPaneSearchQuery[paneIdx]))
                {
                    continue;
                }
            }
        }

        // Column Header Filters
        if (!MatchLetterFilter(item.szFileName, pList->GetActiveFilters(0)))
        {
            continue;
        }
        const auto& typeFilters = pList->GetActiveFilters(1);
        if (!typeFilters.empty())
        {
            if (std::find(typeFilters.begin(), typeFilters.end(), item.szCategory) == typeFilters.end())
            {
                continue;
            }
        }
        if (!MatchLetterFilter(item.szFolder, pList->GetActiveFilters(2)))
        {
            continue;
        }

        indices.push_back(i);
    }
    LeaveCriticalSection(&g_StockCacheCS);

    SortAssetGridPane(paneIdx);

    pList->SetItemCount((int)indices.size());
    pList->ClearSelection();
    pList->Invalidate();
}

void PopulateAssetGrid(CustomTreeNode* hSelected = nullptr)
{
    if (g_StockViewMode == StockViewMode::Single)
    {
        PopulateAssetGridPane(0, hSelected);
    }
    else if (g_StockViewMode == StockViewMode::Dual)
    {
        PopulateAssetGridPane(0);
        PopulateAssetGridPane(1);
    }
    else if (g_StockViewMode == StockViewMode::Triple)
    {
        PopulateAssetGridPane(0);
        PopulateAssetGridPane(1);
        PopulateAssetGridPane(2);
    }
}

FilterPopup g_FilterPopup;

void OnFilterPopupCallback(int colIndex, const std::vector<std::wstring>& checkedOptions, void* pParam)
{
    if (colIndex == 999)
    {
        bool newAutoSaveState = false;
        for (const auto& opt : checkedOptions)
        {
            if (opt == L"Auto-Save")
            {
                newAutoSaveState = true;
            }
        }
        g_bAutoSave = newAutoSaveState ? TRUE : FALSE;

        HWND hWndParent = GetAncestor(g_hCommandBar, GA_ROOT);
        if (g_bAutoSave)
        {
            SaveCurrentConsistDiskOnly(hWndParent);
            ShowModernMessageBox(hWndParent, L"Auto-Save enabled. Current changes saved to disk.", L"Auto-Save", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            ShowModernMessageBox(hWndParent, L"Auto-Save disabled. Use 'Save Consist(s)' button to save your changes manually.", L"Auto-Save", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    HWND hWndList = (HWND)pParam;
    if (hWndList == g_hConsistList)
    {
        g_ConsistList.SetActiveFilters(colIndex, checkedOptions);
        if (g_ActiveTab == 1)
        {
            PopulateActivityConsistList();
        }
        else
        {
            if (!g_ScannedConsistsCache.empty())
            {
                PopulateConsistListFromCache();
            }
            else
            {
                TriggerConsistsRescan(GetAncestor(hWndList, GA_ROOT));
            }
        }
    }
    else if (hWndList == g_hAssetList || hWndList == g_hAssetList2 || hWndList == g_hAssetList3)
    {
        int paneIdx = (hWndList == g_hAssetList) ? 0 : ((hWndList == g_hAssetList2) ? 1 : 2);
        CustomListControl* pList = GetAssetListCtrl(paneIdx);
        pList->SetActiveFilters(colIndex, checkedOptions);
        if (g_StockViewMode == StockViewMode::Single && paneIdx == 0)
        {
            CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
            PopulateAssetGridPane(0, hSelected);
        }
        else
        {
            PopulateAssetGridPane(paneIdx);
        }
    }
    else if (hWndList == g_hEditorUnitList)
    {
        g_EditorUnitList.SetActiveFilters(colIndex, checkedOptions);
        RefreshEditorUnitList();
    }
}

void ShowFilterPopup(HWND hWndList, int colIndex)
{
    std::vector<std::wstring> allOptions;
    if (hWndList == g_hConsistList)
    {
        if (g_ActiveTab == 1)
        {
            if (colIndex == 0) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
            else if (colIndex == 1) allOptions = { L"1-16", L"17-26", L"27-36", L"37-46", L"47-56", L"57-60+" };
            else if (colIndex == 2) allOptions = { L"Healthy", L"Missing Shape", L"Missing Stock", L"Fixed" };
        }
        else
        {
            if (colIndex == 0) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
            else if (colIndex == 1) allOptions = { L"1-16", L"17-26", L"27-36", L"37-46", L"47-56", L"57-60+" };
            else if (colIndex == 2) allOptions = { L"Healthy", L"Missing Shape", L"Missing Stock", L"Fixed" };
            else if (colIndex == 3) allOptions = { L"Today", L"Yesterday", L"Last week", L"Earlier this month", L"Last month", L"A long time ago" };
        }
    }
    else if (hWndList == g_hAssetList || hWndList == g_hAssetList2 || hWndList == g_hAssetList3)
    {
        if (colIndex == 0) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
        else if (colIndex == 1)
        {
            if (hWndList == g_hAssetList && g_StockViewMode == StockViewMode::Single && g_hCategoryTree)
            {
                CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
                if (hSelected == g_pNodeEnginesAll || hSelected == g_pNodeEngines)
                {
                    allOptions = { L"Control", L"Diesel", L"Electric", L"Steam" };
                }
                else if (hSelected == g_pNodeWagonsAll || hSelected == g_pNodeWagons)
                {
                    allOptions = { L"Freight", L"Passenger", L"Tender" };
                }
                else if (hSelected == g_pNodeDiesel) allOptions = { L"Diesel" };
                else if (hSelected == g_pNodeElectric) allOptions = { L"Electric" };
                else if (hSelected == g_pNodeSteam) allOptions = { L"Steam" };
                else if (hSelected == g_pNodeControl) allOptions = { L"Control" };
                else if (hSelected == g_pNodePassenger) allOptions = { L"Passenger" };
                else if (hSelected == g_pNodeFreight) allOptions = { L"Freight" };
                else if (hSelected == g_pNodeTender) allOptions = { L"Tender" };
                else
                {
                    allOptions = { L"Control", L"Diesel", L"Electric", L"Steam", L"Freight", L"Passenger", L"Tender" };
                }
            }
            else
            {
                allOptions = { L"Control", L"Diesel", L"Electric", L"Steam", L"Freight", L"Passenger", L"Tender" };
            }
        }
        else if (colIndex == 2) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
    }
    else if (hWndList == g_hEditorUnitList)
    {
        if (colIndex == 0)      allOptions = { L"1-16", L"17-26", L"27-36", L"37-46", L"47-56", L"57-60+" };
        else if (colIndex == 1) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
        else if (colIndex == 2) allOptions = { L"Engine", L"Wagon" };
        else if (colIndex == 3) allOptions = { L"Healthy", L"Missing Shape", L"Missing Stock", L"Fixed" };
        else if (colIndex == 4) allOptions = { L"Normal", L"Flipped" };
        else if (colIndex == 5) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
    }

    if (allOptions.empty()) return;

    POINT pt;
    GetCursorPos(&pt);
    int x = pt.x - 180;
    int y = pt.y + 10;

    CustomListControl& listCtrl = (hWndList == g_hConsistList) ? g_ConsistList : 
                                  ((hWndList == g_hAssetList) ? g_AssetList : 
                                  ((hWndList == g_hAssetList2) ? g_AssetList2 : 
                                  ((hWndList == g_hAssetList3) ? g_AssetList3 : g_EditorUnitList)));
    const std::vector<std::wstring>& checkedOptions = listCtrl.GetActiveFilters(colIndex);
    
    g_FilterPopup.Show(GetAncestor(hWndList, GA_ROOT), colIndex, x, y, allOptions, checkedOptions, OnFilterPopupCallback, (void*)hWndList);
}

// ---------------------------------------------------------------------------
// Train Physical Metrics Parser & Live Calculation (Total Mass, Length, Power, Ratio)
// ---------------------------------------------------------------------------
struct UnitPhysicalMetrics {
    double massTonnes = 0.0;
    double lengthMeters = 0.0;
    double powerHP = 0.0;
    double powerKW = 0.0;
    bool isValid = false;
};

static std::unordered_map<std::wstring, UnitPhysicalMetrics> g_UnitMetricsCache;

static UnitPhysicalMetrics ParseUnitPhysicalMetrics(const std::wstring& filePath, int depth = 0)
{
    UnitPhysicalMetrics m;
    if (depth > 5) return m;

    std::string ascii = ReadConFileToAscii(filePath);
    if (ascii.empty()) return m;

    std::string lower = ascii;
    for (char& c : lower) c = tolower(c);

    // 1. Parse Mass ( <val> [unit] )
    size_t posMass = lower.find("mass");
    while (posMass != std::string::npos)
    {
        size_t idx = posMass + 4;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            size_t valStart = idx;
            size_t valEnd = lower.find_first_of(" \t\r\n)", valStart);
            if (valEnd != std::string::npos)
            {
                std::string massValStr = lower.substr(valStart, valEnd - valStart);
                double numVal = 0.0;
                char unitStr[32] = { 0 };
                if (sscanf_s(massValStr.c_str(), "%lf%31s", &numVal, unitStr, (unsigned)sizeof(unitStr)) >= 1)
                {
                    std::string u(unitStr);
                    for (char& cu : u) cu = tolower(cu);
                    if (u.find("kg") != std::string::npos)
                        m.massTonnes = numVal * 0.001;
                    else if (u.find("lb") != std::string::npos)
                        m.massTonnes = numVal * 0.00045359237;
                    else if (u.find("t-us") != std::string::npos || u.find("t_us") != std::string::npos)
                        m.massTonnes = numVal * 0.90718474;
                    else if (u.find("t-uk") != std::string::npos || u.find("t_uk") != std::string::npos)
                        m.massTonnes = numVal * 1.0160469;
                    else
                        m.massTonnes = numVal; // default is metric tonnes
                    m.isValid = true;
                    break;
                }
            }
        }
        posMass = lower.find("mass", posMass + 1);
    }

    // 2. Parse Size ( <width> <height> <length> )
    size_t posSize = lower.find("size");
    while (posSize != std::string::npos)
    {
        size_t idx = posSize + 4;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            size_t closeParen = lower.find(')', idx);
            if (closeParen != std::string::npos)
            {
                std::string sizeContent = lower.substr(idx, closeParen - idx);
                std::stringstream ss(sizeContent);
                std::string tokW, tokH, tokL;
                if (ss >> tokW >> tokH >> tokL)
                {
                    double numL = 0.0;
                    char unitL[32] = { 0 };
                    if (sscanf_s(tokL.c_str(), "%lf%31s", &numL, unitL, (unsigned)sizeof(unitL)) >= 1)
                    {
                        std::string u(unitL);
                        for (char& cu : u) cu = tolower(cu);
                        if (u.find("ft") != std::string::npos || u.find("feet") != std::string::npos || u.find("'") != std::string::npos)
                            m.lengthMeters = numL * 0.3048;
                        else if (u.find("in") != std::string::npos || u.find("\"") != std::string::npos)
                            m.lengthMeters = numL * 0.0254;
                        else if (u.find("mm") != std::string::npos)
                            m.lengthMeters = numL * 0.001;
                        else if (u.find("cm") != std::string::npos)
                            m.lengthMeters = numL * 0.01;
                        else
                            m.lengthMeters = numL; // default is meters
                        m.isValid = true;
                        break;
                    }
                }
            }
        }
        posSize = lower.find("size", posSize + 1);
    }

    // 3. Parse MaxPower
    size_t posPower = lower.find("maxpower");
    while (posPower != std::string::npos)
    {
        size_t idx = posPower + 8;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            size_t valStart = idx;
            size_t valEnd = lower.find_first_of(" \t\r\n)", valStart);
            if (valEnd != std::string::npos)
            {
                std::string pwrValStr = lower.substr(valStart, valEnd - valStart);
                double numVal = 0.0;
                char unitStr[32] = { 0 };
                if (sscanf_s(pwrValStr.c_str(), "%lf%31s", &numVal, unitStr, (unsigned)sizeof(unitStr)) >= 1)
                {
                    std::string u(unitStr);
                    for (char& cu : u) cu = tolower(cu);
                    if (u.find("hp") != std::string::npos || u.find("bhp") != std::string::npos)
                    {
                        m.powerHP = numVal;
                        m.powerKW = numVal * 0.745699872;
                    }
                    else if (u.find("mw") != std::string::npos)
                    {
                        m.powerKW = numVal * 1000.0;
                        m.powerHP = m.powerKW * 1.34102209;
                    }
                    else // kW or default
                    {
                        m.powerKW = numVal;
                        m.powerHP = numVal * 1.34102209;
                    }
                    m.isValid = true;
                    break;
                }
            }
        }
        posPower = lower.find("maxpower", posPower + 1);
    }

    // 4. Follow Include files if anything is missing
    if (m.massTonnes == 0.0 || m.lengthMeters == 0.0 || m.powerHP == 0.0)
    {
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
                    UnitPhysicalMetrics incM = ParseUnitPhysicalMetrics(incFullPath, depth + 1);
                    if (m.massTonnes == 0.0 && incM.massTonnes > 0.0) m.massTonnes = incM.massTonnes;
                    if (m.lengthMeters == 0.0 && incM.lengthMeters > 0.0) m.lengthMeters = incM.lengthMeters;
                    if (m.powerHP == 0.0 && incM.powerHP > 0.0)
                    {
                        m.powerHP = incM.powerHP;
                        m.powerKW = incM.powerKW;
                    }
                }
            }
            posInclude = lower.find("include", posInclude + 1);
        }
    }

    // Default length fallback if Size wasn't specified
    if (m.lengthMeters <= 0.0)
    {
        m.lengthMeters = 15.0; // standard default car length
    }

    return m;
}

static void UpdateConsistMetricsUI()
{
    if (!g_hEditMetricMass || !g_hEditMetricLength || !g_hEditMetricPower || !g_hEditMetricRatio)
        return;

    double totalMass = 0.0;
    double totalLength = 0.0;
    double totalPowerHP = 0.0;
    double totalPowerKW = 0.0;
    int engineCount = 0;
    int wagonCount = 0;

    for (const auto& unit : g_LoadedConsistUnits)
    {
        if (unit.isEngine) engineCount++;
        else wagonCount++;

        if (unit.uid.empty() || unit.parentDir.empty()) continue;

        std::wstring ext = unit.isEngine ? L".eng" : L".wag";
        std::wstring unitPath = g_szBasePath;
        if (!unitPath.empty() && unitPath.back() != L'\\') unitPath += L'\\';
        unitPath += L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + ext;

        std::wstring cacheKey = unit.parentDir + L"\\" + unit.uid + ext;
        for (wchar_t& c : cacheKey) c = towlower(c);

        auto it = g_UnitMetricsCache.find(cacheKey);
        if (it == g_UnitMetricsCache.end())
        {
            UnitPhysicalMetrics m = ParseUnitPhysicalMetrics(unitPath);
            g_UnitMetricsCache[cacheKey] = m;
            totalMass += m.massTonnes;
            totalLength += m.lengthMeters;
            totalPowerHP += m.powerHP;
            totalPowerKW += m.powerKW;
        }
        else
        {
            totalMass += it->second.massTonnes;
            totalLength += it->second.lengthMeters;
            totalPowerHP += it->second.powerHP;
            totalPowerKW += it->second.powerKW;
        }
    }

    wchar_t szMass[128], szLength[128], szPower[128], szRatio[128];
    swprintf_s(szMass, 128, L"%.2f t", totalMass);
    swprintf_s(szLength, 128, L"%.2f m", totalLength);
    if (totalPowerHP > 0.0)
    {
        swprintf_s(szPower, 128, L"%.0f HP (%.0f kW)", totalPowerHP, totalPowerKW);
    }
    else
    {
        wcscpy_s(szPower, 128, L"0 HP (0 kW)");
    }
    swprintf_s(szRatio, 128, L"%d Units (%d Eng, %d Wag)", (int)g_LoadedConsistUnits.size(), engineCount, wagonCount);

    SetWindowTextW(g_hEditMetricMass, szMass);
    SetWindowTextW(g_hEditMetricLength, szLength);
    SetWindowTextW(g_hEditMetricPower, szPower);
    SetWindowTextW(g_hEditMetricRatio, szRatio);
}

static std::wstring GetCurrentActiveConsistKey()
{
    if (g_ActiveTab == 1)
    {
        if (g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
        {
            return g_CurrentActivityFilePath + L"#" + g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].id;
        }
        return L"";
    }
    return g_szCurrentConsistFile;
}

static std::vector<int> GetSelectedConsistUnitIndices()
{
    std::unordered_set<int> uniqueIndices;

    // 1. Add all persistent checked units from dedicated Column 0 checkboxes
    for (int unitIdx : g_CheckedConsistUnits)
    {
        if (unitIdx >= 0 && unitIdx < (int)g_LoadedConsistUnits.size())
        {
            uniqueIndices.insert(unitIdx);
        }
    }

    // 2. Gather selected/highlighted rows
    std::vector<int> selRows = g_EditorUnitList.GetSelectedIndices();
    if (selRows.empty())
    {
        int singleSel = g_EditorUnitList.GetSelectedIndex();
        if (singleSel >= 0) selRows.push_back(singleSel);
    }
    for (int row : selRows)
    {
        if (row >= 0 && row < g_EditorUnitList.GetItemCount())
        {
            std::wstring noStr = g_EditorUnitList.GetCellText(row, 0);
            int originalNo = _wtoi(noStr.c_str());
            int unitIdx = originalNo - 1;
            if (unitIdx >= 0 && unitIdx < (int)g_LoadedConsistUnits.size())
            {
                uniqueIndices.insert(unitIdx);
            }
        }
    }

    std::vector<int> unitIndices(uniqueIndices.begin(), uniqueIndices.end());
    std::sort(unitIndices.begin(), unitIndices.end());
    return unitIndices;
}

static void GetActiveTargetConsistsAndUnits(std::vector<std::wstring>& outConsists, std::vector<int>& outUnits)
{
    outConsists.clear();
    outUnits.clear();

    std::vector<int> selRows = g_ConsistList.GetCheckedIndices();
    if (selRows.empty())
    {
        selRows = g_ConsistList.GetSelectedIndices();
        if (selRows.empty())
        {
            int singleSel = g_ConsistList.GetSelectedIndex();
            if (singleSel >= 0) selRows.push_back(singleSel);
        }
    }
    if (g_ActiveTab == 1)
    {
        for (int row : selRows)
        {
            std::wstring idxStr = g_ConsistList.GetCellText(row, 3);
            if (!idxStr.empty())
            {
                outConsists.push_back(L"ACTIVITY:" + idxStr);
            }
        }
        if (outConsists.empty() && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
        {
            outConsists.push_back(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex));
        }
    }
    else
    {
        for (int row : selRows)
        {
            std::wstring fname = g_ConsistList.GetCellText(row, 4);
            if (!fname.empty())
            {
                std::wstring fullPath = EnsureConsistFilePath(g_szBasePath, fname);
                if (!fullPath.empty()) outConsists.push_back(fullPath);
            }
        }
        if (outConsists.empty() && !g_szCurrentConsistFile.empty())
        {
            std::wstring fullPath = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
            if (!fullPath.empty()) outConsists.push_back(fullPath);
        }
    }
    outUnits = GetSelectedConsistUnitIndices();
}

static void SyncPoolMutatorSelectionIfOpen()
{
    if (PoolMutatorDlg_IsOpen())
    {
        std::vector<std::wstring> selConsists;
        std::vector<int> selUnits;
        GetActiveTargetConsistsAndUnits(selConsists, selUnits);
        PoolMutatorDlg_UpdateSelection(selConsists, selUnits);
    }
}

static int GetSelectedConsistUnitIndex()
{
    int sel = g_EditorUnitList.GetSelectedIndex();
    if (sel >= 0 && sel < g_EditorUnitList.GetItemCount())
    {
        std::wstring noStr = g_EditorUnitList.GetCellText(sel, 0);
        int originalNo = _wtoi(noStr.c_str());
        int unitIdx = originalNo - 1;
        if (unitIdx >= 0 && unitIdx < (int)g_LoadedConsistUnits.size())
        {
            return unitIdx;
        }
    }
    return -1;
}

static void SetSelectedConsistUnitIndices(const std::vector<int>& unitIndices)
{
    std::unordered_set<int> unitSet(unitIndices.begin(), unitIndices.end());
    std::vector<int> matchingRows;
    int count = g_EditorUnitList.GetItemCount();
    for (int r = 0; r < count; ++r)
    {
        std::wstring noStr = g_EditorUnitList.GetCellText(r, 0);
        int originalNo = _wtoi(noStr.c_str());
        int unitIdx = originalNo - 1;
        if (unitSet.count(unitIdx) > 0)
        {
            matchingRows.push_back(r);
        }
    }
    g_EditorUnitList.SetSelectedIndices(matchingRows);
}

static void UpdateUnitPreviewFromSelected()
{
    if (!g_hUnitPreviewCard) return;

    if (g_ActivePane == PANE_STOCK)
    {
        // 1. Stock Manager active: Inspect first selected item (anchor) in active Asset List (Pane 1, 2, or 3)
        CustomListControl* pActiveAssetList = &g_AssetList;
        if (g_StockViewMode == StockViewMode::Dual || g_StockViewMode == StockViewMode::Triple)
        {
            HWND hFocus = GetFocus();
            if (hFocus == g_hAssetList2) { pActiveAssetList = &g_AssetList2; }
            else if (hFocus == g_hAssetList3) { pActiveAssetList = &g_AssetList3; }
            else if (g_AssetList.GetFirstSelectedIndex() < 0)
            {
                if (g_AssetList2.GetFirstSelectedIndex() >= 0) pActiveAssetList = &g_AssetList2;
                else if (g_AssetList3.GetFirstSelectedIndex() >= 0) pActiveAssetList = &g_AssetList3;
            }
        }

        int stockSel = pActiveAssetList->GetFirstSelectedIndex();
        if (stockSel >= 0 && stockSel < pActiveAssetList->GetItemCount())
        {
            std::wstring stockName   = pActiveAssetList->GetCellText(stockSel, 0);
            std::wstring stockType   = pActiveAssetList->GetCellText(stockSel, 1);
            std::wstring stockFolder = pActiveAssetList->GetCellText(stockSel, 2);
            if (!stockName.empty() && !stockFolder.empty())
            {
                std::wstring fullPath = BuildFullStockPath(g_szBasePath, stockFolder, stockName);
                UnitPreviewCard_SetUnit(g_hUnitPreviewCard, fullPath, g_szBasePath, false, stockName, stockType);
                return;
            }
        }

        UnitPreviewCard_Clear(g_hUnitPreviewCard);
        return;
    }
    else if (g_ActivePane == PANE_WORKSPACE)
    {
        // 2. Consist Editor Workspace active: Inspect first selected unit (anchor) in workspace
        int sel = g_EditorUnitList.GetFirstSelectedIndex();
        if (sel >= 0 && sel < g_EditorUnitList.GetItemCount())
        {
            std::wstring strNo = g_EditorUnitList.GetCellText(sel, 0);
            int originalNo = _wtoi(strNo.c_str());
            int idx = originalNo - 1;
            if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
            {
                const auto& unit = g_LoadedConsistUnits[idx];
                std::wstring fullPath = BuildFullStockPath(g_szBasePath, unit.parentDir, unit.uid, unit.isEngine ? L".eng" : L".wag");
                UnitPreviewCard_SetUnit(g_hUnitPreviewCard, fullPath, g_szBasePath, unit.isFlipped, unit.uid, unit.isEngine ? L"Engine" : L"Wagon");
                return;
            }
        }

        UnitPreviewCard_Clear(g_hUnitPreviewCard);
        return;
    }
    else
    {
        // 3. Consist Manager or unselected state: show clean empty preview
        UnitPreviewCard_Clear(g_hUnitPreviewCard);
        return;
    }
}

static void RefreshEditorUnitList(bool preserveSelection)
{
    int savedScrollY = preserveSelection ? g_EditorUnitList.GetScrollY() : 0;
    std::vector<int> savedSelectedUnits;
    if (preserveSelection)
    {
        // Save highlighted selection rows (unit indices)
        int curItemCount = g_EditorUnitList.GetItemCount();
        std::vector<int> selRows = g_EditorUnitList.GetSelectedIndices();
        if (selRows.empty())
        {
            int singleSel = g_EditorUnitList.GetSelectedIndex();
            if (singleSel >= 0) selRows.push_back(singleSel);
        }
        for (int r : selRows)
        {
            if (r >= 0 && r < curItemCount)
            {
                std::wstring noStr = g_EditorUnitList.GetCellText(r, 0);
                int originalNo = _wtoi(noStr.c_str());
                int unitIdx = originalNo - 1;
                if (unitIdx >= 0 && unitIdx < (int)g_LoadedConsistUnits.size())
                {
                    savedSelectedUnits.push_back(unitIdx);
                }
            }
        }
    }
    else
    {
        g_CheckedConsistUnits.clear();
    }

    g_EditorUnitList.Clear();

    const auto& filtersNo     = g_EditorUnitList.GetActiveFilters(0);
    const auto& filtersName   = g_EditorUnitList.GetActiveFilters(1);
    const auto& filtersType   = g_EditorUnitList.GetActiveFilters(2);
    const auto& filtersStatus = g_EditorUnitList.GetActiveFilters(3);
    const auto& filtersOrient = g_EditorUnitList.GetActiveFilters(4);
    const auto& filtersParent = g_EditorUnitList.GetActiveFilters(5);

    for (size_t i = 0; i < g_LoadedConsistUnits.size(); ++i)
    {
        const auto& unit = g_LoadedConsistUnits[i];
        int originalNo = (int)(i + 1);
        std::wstring unitNo = std::to_wstring(originalNo);
        std::wstring unitType = unit.isEngine ? L"Engine" : L"Wagon";

        // Perform dynamic disk check for this unit
        UnitHealthStatus health = GetUnitHealthOnDisk(unit, g_szBasePath);
        bool isFixedInSession = false;
        std::wstring activeConsistKey = GetCurrentActiveConsistKey();
        if (!activeConsistKey.empty())
        {
            auto it = g_SessionFixedUnitsPerConsist.find(activeConsistKey);
            if (it != g_SessionFixedUnitsPerConsist.end())
            {
                isFixedInSession = (health == UnitHealthStatus::Healthy && it->second.count((int)i) > 0);
            }
        }
        std::wstring unitStatus = L"Healthy";
        if (health == UnitHealthStatus::MissingStock) unitStatus = L"Missing Stock";
        else if (health == UnitHealthStatus::MissingShape) unitStatus = L"Missing Shape";
        else if (isFixedInSession) unitStatus = L"Fixed";
        std::wstring unitOrient = unit.isFlipped ? L"Flipped" : L"Normal";

        // 1. Filter by No.
        if (!filtersNo.empty())
        {
            bool match = false;
            for (const auto& f : filtersNo)
            {
                if (f == L"1-16" && originalNo >= 1 && originalNo <= 16) match = true;
                else if (f == L"17-26" && originalNo >= 17 && originalNo <= 26) match = true;
                else if (f == L"27-36" && originalNo >= 27 && originalNo <= 36) match = true;
                else if (f == L"37-46" && originalNo >= 37 && originalNo <= 46) match = true;
                else if (f == L"47-56" && originalNo >= 47 && originalNo <= 56) match = true;
                else if (f == L"57-60+" && originalNo >= 57) match = true;
            }
            if (!match) continue;
        }

        // 2. Filter by Name
        if (!filtersName.empty())
        {
            if (!MatchLetterFilter(unit.uid, filtersName)) continue;
        }

        // 3. Filter by Type
        if (!filtersType.empty())
        {
            bool match = false;
            for (const auto& f : filtersType)
            {
                if (f == unitType) match = true;
            }
            if (!match) continue;
        }

        // 4. Filter by Status
        if (!filtersStatus.empty())
        {
            bool match = false;
            for (const auto& f : filtersStatus)
            {
                if (f == unitStatus) match = true;
            }
            if (!match) continue;
        }

        // 5. Filter by Orientation
        if (!filtersOrient.empty())
        {
            bool match = false;
            for (const auto& f : filtersOrient)
            {
                if (f == unitOrient) match = true;
            }
            if (!match) continue;
        }

        // 6. Filter by Parent Directory
        if (!filtersParent.empty())
        {
            if (!MatchLetterFilter(unit.parentDir, filtersParent)) continue;
        }

        g_EditorUnitList.AddItem({ unitNo, unit.uid, unitType, unitStatus, unitOrient, unit.parentDir });
        int rowIndex = g_EditorUnitList.GetItemCount() - 1;
        if (g_CheckedConsistUnits.count((int)i) > 0)
        {
            g_EditorUnitList.SetItemChecked(rowIndex, true);
        }
    }

    // Update Consist Units section header with total and broken counts
    if (g_hSectionUnits)
    {
        int total = (int)g_LoadedConsistUnits.size();
        int broken = 0;
        for (const auto& unit : g_LoadedConsistUnits)
        {
            bool isUnitBroken = false;
            if (unit.uid.empty() || unit.parentDir.empty())
            {
                isUnitBroken = true;
            }
            else
            {
                std::wstring ext = unit.isEngine ? L".eng" : L".wag";
                std::wstring unitPath = g_szBasePath;
                if (!unitPath.empty() && unitPath.back() != L'\\')
                    unitPath += L'\\';
                unitPath += L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + ext;

                DWORD attr = GetFileAttributesW(unitPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
                {
                    isUnitBroken = true;
                }
            }
            if (isUnitBroken) broken++;
        }

        wchar_t szUnitsText[128];
        swprintf_s(szUnitsText, 128, L"Consist Units [ Total Units: %d • Broken: %d ]", total, broken);
        SetWindowTextW(g_hSectionUnits, szUnitsText);

        if (g_hVisualConsistView)
        {
            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
        }
    }

    UpdateConsistMetricsUI();

    if (preserveSelection && !savedSelectedUnits.empty())
    {
        SetSelectedConsistUnitIndices(savedSelectedUnits);
    }
    else if (!preserveSelection)
    {
        g_EditorUnitList.ClearSelection();
    }
    g_EditorUnitList.SetScrollY(savedScrollY);
    UpdateUnitPreviewFromSelected();
}


// ---------------------------------------------------------------------------
// Consist Editing Clipboard & Undo / Redo System
// ---------------------------------------------------------------------------

static void UpdateConsistManagerRow(const std::wstring& filename)
{
    if (filename.empty()) return;

    for (int i = 0; i < g_ConsistList.GetItemCount(); ++i)
    {
        std::wstring fName = g_ConsistList.GetCellText(i, 4);
        if (_wcsicmp(fName.c_str(), filename.c_str()) == 0)
        {
            auto it = g_ConsistSessions.find(filename);
            bool isDirty = (it != g_ConsistSessions.end() && it->second.isDirty);

            std::wstring curName = g_ConsistList.GetCellText((int)i, 0);
            if (curName.rfind(L"● ", 0) == 0)
            {
                curName = curName.substr(2);
            }

            if (isDirty)
            {
                g_ConsistList.SetCellText((int)i, 0, L"● " + curName);
            }
            else
            {
                g_ConsistList.SetCellText((int)i, 0, curName);
            }

            if (it != g_ConsistSessions.end())
            {
                g_ConsistList.SetCellText((int)i, 1, std::to_wstring(it->second.units.size()));
                std::wstring statusStr = EvaluateAndUpdateConsistStatus(filename, it->second.units, !it->second.isDirty);
                g_ConsistList.SetCellText((int)i, 2, statusStr);
            }

            g_ConsistList.Invalidate();
            break;
        }
    }
}

static void SaveCurrentConsistSessionState()
{
    if (g_ActiveTab == 1)
    {
        if (g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
        {
            g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].units = g_LoadedConsistUnits;
            g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].totalUnits = (int)g_LoadedConsistUnits.size();
        }
        return;
    }

    if (g_szCurrentConsistFile.empty()) return;

    auto& session = g_ConsistSessions[g_szCurrentConsistFile];
    session.fileName = g_szCurrentConsistFile;
    session.units = g_LoadedConsistUnits;
    session.undoStack = g_UndoStack;
    session.redoStack = g_RedoStack;
    session.selectedIndices = g_EditorUnitList.GetSelectedIndices();
    session.scrollY = g_EditorUnitList.GetScrollY();

    wchar_t szCfgId[256] = { 0 };
    wchar_t szName[256] = { 0 };
    wchar_t szVelocity[256] = { 0 };
    wchar_t szPerf[256] = { 0 };
    if (g_hEditTrainCfgId)  GetWindowTextW(g_hEditTrainCfgId,  szCfgId,    256);
    if (g_hEditTrainName)   GetWindowTextW(g_hEditTrainName,   szName,     256);
    if (g_hEditMaxVelocity) GetWindowTextW(g_hEditMaxVelocity, szVelocity, 256);
    if (g_hEditPerfFactor)  GetWindowTextW(g_hEditPerfFactor,  szPerf,     256);

    session.trainCfg.trainCfgId = szCfgId;
    session.trainCfg.name = szName;
    session.trainCfg.maxVelocity = _wtof(szVelocity);
    session.trainCfg.perfFactor = _wtof(szPerf);
}

static bool SaveConsistSessionToDisk(HWND hWnd, const std::wstring& filename)
{
    auto it = g_ConsistSessions.find(filename);
    if (it == g_ConsistSessions.end()) return false;

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\')
        consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";
    std::wstring fullPath = consistFolder + filename;

    g_bIgnoreWatcher = TRUE;
    SetTimer(hWnd, TIMER_IGNORE_WATCHER_RESET, 500, NULL);

    bool success = ConsistWriter::SaveConsist(
        fullPath,
        it->second.trainCfg.trainCfgId,
        it->second.trainCfg.name,
        it->second.trainCfg.maxVelocity,
        it->second.trainCfg.perfFactor,
        it->second.units
    );
    if (success)
    {
        it->second.isDirty = false;
        UpdateConsistManagerRow(filename);
        LOG_INFO("Saved consist '%ls' to disk (%zu units).", filename.c_str(), it->second.units.size());

        for (int i = 0; i < g_ConsistList.GetItemCount(); ++i)
        {
            std::wstring fName = g_ConsistList.GetCellText(i, 4);
            if (_wcsicmp(fName.c_str(), filename.c_str()) == 0)
            {
                SYSTEMTIME stLocal;
                GetLocalTime(&stLocal);
                wchar_t dateBuf[64] = { 0 };
                wchar_t timeBuf[64] = { 0 };
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, dateBuf, 64);
                GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &stLocal, NULL, timeBuf, 64);
                std::wstring friendlyTime = std::wstring(dateBuf) + L" " + timeBuf;
                g_ConsistList.SetCellText((int)i, 3, friendlyTime);
                g_ConsistList.Invalidate();
                break;
            }
        }
    }
    return success;
}

static void PushUndoState(const std::wstring& actionDesc);

std::vector<PoolMutator::BrokenConsistInfo> ScanActiveTargetConsistsForBrokenUnits(
    const std::vector<std::wstring>& targetConsistPaths,
    const std::vector<int>& targetUnitIndices,
    bool forceScanAll,
    const std::wstring& basePath)
{
    std::vector<PoolMutator::BrokenConsistInfo> result;
    std::wstring bp = basePath;
    if (!bp.empty() && bp.back() != L'\\' && bp.back() != L'/') bp += L'\\';

    std::vector<std::wstring> pathsToScan = targetConsistPaths;
    if (forceScanAll)
    {
        pathsToScan.clear();
        if (g_ActiveTab == 1)
        {
            for (size_t i = 0; i < g_CurrentActivityData.consists.size(); ++i)
            {
                pathsToScan.push_back(L"ACTIVITY:" + std::to_wstring(i));
            }
        }

        if (!bp.empty())
        {
            std::wstring searchPattern = bp + L"TRAINS\\CONSISTS\\*.con";
            WIN32_FIND_DATAW ffd;
            HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        pathsToScan.push_back(bp + L"TRAINS\\CONSISTS\\" + std::wstring(ffd.cFileName));
                    }
                } while (FindNextFileW(hFind, &ffd) != 0);
                FindClose(hFind);
            }
        }
    }

    if (pathsToScan.empty())
    {
        if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
        {
            pathsToScan.push_back(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex));
        }
        else if (!g_szCurrentConsistFile.empty())
        {
            std::wstring fullPath = EnsureConsistFilePath(bp, g_szCurrentConsistFile);
            if (!fullPath.empty()) pathsToScan.push_back(fullPath);
        }
    }

    for (const auto& path : pathsToScan)
    {
        PoolMutator::BrokenConsistInfo bcon;
        bcon.filePath = path;

        std::vector<ConsistReader::UnitInfo> units;

        if (path.rfind(L"ACTIVITY:", 0) == 0)
        {
            int cIdx = _wtoi(path.substr(9).c_str());
            if (cIdx < 0 || cIdx >= (int)g_CurrentActivityData.consists.size()) continue;

            const auto& con = g_CurrentActivityData.consists[cIdx];
            bcon.fileName = con.id.empty() ? (L"Activity Consist #" + std::to_wstring(cIdx + 1)) : con.id;
            bcon.consistName = con.name.empty() ? bcon.fileName : con.name;

            if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
            {
                units = g_LoadedConsistUnits;
            }
            else
            {
                units = con.units;
            }
        }
        else
        {
            wchar_t fname[MAX_PATH] = { 0 };
            _wsplitpath_s(path.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);
            std::wstring filename = std::wstring(fname) + L".con";
            bcon.fileName = fname;

            auto it = g_ConsistSessions.find(filename);
            if (it != g_ConsistSessions.end())
            {
                bcon.consistName = it->second.trainCfg.name.empty() ? fname : it->second.trainCfg.name;
                if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
                {
                    units = g_LoadedConsistUnits;
                }
                else
                {
                    units = it->second.units;
                }
            }
            else if (PathFileExistsW(path.c_str()))
            {
                try
                {
                    auto conData = ConsistReader::LoadConsist(path);
                    bcon.consistName = conData.trainCfg.name.empty() ? fname : conData.trainCfg.name;
                    units = conData.units;
                }
                catch (...)
                {
                    continue;
                }
            }
            else
            {
                continue;
            }
        }

        bool anySelected = false;
        for (size_t i = 0; i < units.size(); ++i)
        {
            const auto& u = units[i];
            bool isBroken = IsUnitBrokenOnDisk(u, bp);

            PoolMutator::BrokenUnitInfo bu;
            bu.unitIndex = (int)i;
            bu.uid = u.uid;
            bu.parentDir = u.parentDir;
            bu.isEngine = u.isEngine;
            bu.isFlipped = u.isFlipped;
            bu.isBroken = isBroken;

            if (!targetUnitIndices.empty())
            {
                bu.isSelected = (std::find(targetUnitIndices.begin(), targetUnitIndices.end(), (int)i) != targetUnitIndices.end());
            }
            else
            {
                bu.isSelected = isBroken;
            }

            if (bu.isSelected) anySelected = true;
            bcon.brokenUnits.push_back(bu);
        }

        if (units.empty())
        {
            bcon.isSelected = true;
            bcon.isExpanded = true;
            bcon.actionExecutionMode = 1; // Default to Rebuild Consist for empty consists
            result.push_back(std::move(bcon));
        }
        else if (!bcon.brokenUnits.empty())
        {
            bcon.isSelected = anySelected;
            bcon.isExpanded = true;
            bcon.actionExecutionMode = 0; // Replace Units default
            result.push_back(std::move(bcon));
        }
    }

    return result;
}

bool ApplyPoolMutationToSessions(
    HWND hWnd,
    const std::vector<std::wstring>& targetConsistPaths,
    const std::vector<int>& targetUnitIndices,
    const PoolMutator::MutatorOptions& options,
    PoolMutator::MutatorResult& outResult,
    const std::vector<PoolMutator::BrokenConsistInfo>* pBrokenConsists)
{
    outResult.success = false;
    outResult.processedCount = 0;
    outResult.repairedUnitsCount = 0;
    outResult.affectedFiles.clear();
    outResult.errorMessage.clear();

    // Mode 2: Replace Broken Units
    if (options.mode == PoolMutator::MutatorMode::ReplaceBroken && pBrokenConsists)
    {
        bool anyRepaired = false;
        for (const auto& bcon : *pBrokenConsists)
        {
            if (!bcon.isSelected) continue;
            if (bcon.actionExecutionMode == 0 && bcon.brokenUnits.empty()) continue;

            const std::wstring& path = bcon.filePath;
            if (path.rfind(L"ACTIVITY:", 0) == 0)
            {
                int cIdx = _wtoi(path.substr(9).c_str());
                if (cIdx < 0 || cIdx >= (int)g_CurrentActivityData.consists.size()) continue;

                auto& con = g_CurrentActivityData.consists[cIdx];
                std::wstring consistKey = g_CurrentActivityFilePath + L"#" + con.id;

                if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
                {
                    PushUndoState(L"Replace Broken Units");
                    con.units = g_LoadedConsistUnits;
                }

                std::vector<bool> wasBrokenBefore(con.units.size(), false);
                for (size_t i = 0; i < con.units.size(); ++i)
                {
                    wasBrokenBefore[i] = IsUnitBrokenOnDisk(con.units[i], g_szBasePath);
                }

                std::wstring err;
                int repCount = 0;
                bool okApply = false;

                if (bcon.actionExecutionMode == 1) // Rebuild Consist from scratch
                {
                    std::vector<ConsistReader::UnitInfo> rebuiltUnits;
                    if (PoolMutator::GenerateFullConsistFromSourceNode(bcon.assignedSourceId, options, rebuiltUnits, err))
                    {
                        con.units = rebuiltUnits;
                        repCount = (int)rebuiltUnits.size();
                        okApply = true;
                    }
                    else
                    {
                        outResult.errorMessage = err;
                    }
                }
                else // In-Place Unit Replacement
                {
                    okApply = PoolMutator::RepairUnitsVector(con.units, bcon.brokenUnits, options, err, repCount);
                }

                if (okApply)
                {
                    con.isDirty = true;
                    con.totalUnits = (int)con.units.size();
                    outResult.processedCount++;
                    outResult.repairedUnitsCount += repCount;
                    std::wstring dispName = con.name.empty() ? (L"Activity Consist #" + std::to_wstring(cIdx + 1)) : con.name;
                    outResult.affectedFiles.push_back(dispName);
                    anyRepaired = true;

                    for (size_t i = 0; i < con.units.size(); ++i)
                    {
                        bool isNowBroken = IsUnitBrokenOnDisk(con.units[i], g_szBasePath);
                        if (i < wasBrokenBefore.size() && wasBrokenBefore[i] && !isNowBroken)
                        {
                            g_SessionFixedUnitsPerConsist[consistKey].insert((int)i);
                        }
                        else if (isNowBroken)
                        {
                            g_SessionFixedUnitsPerConsist[consistKey].erase((int)i);
                        }
                    }

                    if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
                    {
                        g_LoadedConsistUnits = con.units;
                        RefreshEditorUnitList(false);
                        UpdateConsistMetricsUI();
                        if (g_hVisualConsistView)
                        {
                            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                        }
                    }

                    for (int r = 0; r < g_ConsistList.GetItemCount(); ++r)
                    {
                        std::wstring rIdxStr = g_ConsistList.GetCellText(r, 3);
                        if (_wtoi(rIdxStr.c_str()) == cIdx)
                        {
                            std::wstring curName = g_ConsistList.GetCellText(r, 0);
                            if (curName.rfind(L"● ", 0) != 0)
                            {
                                g_ConsistList.SetCellText(r, 0, L"● " + curName);
                            }
                            g_ConsistList.SetCellText(r, 1, std::to_wstring(con.totalUnits));
                            std::wstring statusStr = EvaluateAndUpdateConsistStatus(consistKey, con.units, !con.isDirty);
                            g_ConsistList.SetCellText(r, 2, statusStr);
                            g_ConsistList.Invalidate();
                            break;
                        }
                    }
                }
            }
            else
            {
                wchar_t fname[MAX_PATH] = { 0 };
                _wsplitpath_s(path.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);
                std::wstring filename = std::wstring(fname) + L".con";

                if (g_ConsistSessions.find(filename) == g_ConsistSessions.end())
                {
                    if (!PathFileExistsW(path.c_str())) continue;
                    try {
                        auto data = ConsistReader::LoadConsist(path);
                        auto& sess = g_ConsistSessions[filename];
                        sess.fileName = filename;
                        sess.trainCfg = data.trainCfg;
                        sess.units = data.units;
                        sess.isDirty = false;
                    } catch (...) {
                        continue;
                    }
                }

                auto& sess = g_ConsistSessions[filename];
                if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
                {
                    PushUndoState(bcon.actionExecutionMode == 1 ? L"Rebuild Consist" : L"Replace Broken Units");
                    sess.units = g_LoadedConsistUnits;
                }

                std::vector<bool> wasBrokenBefore(sess.units.size(), false);
                for (size_t i = 0; i < sess.units.size(); ++i)
                {
                    wasBrokenBefore[i] = IsUnitBrokenOnDisk(sess.units[i], g_szBasePath);
                }

                std::wstring err;
                int repCount = 0;
                bool okApply = false;

                if (bcon.actionExecutionMode == 1) // Rebuild Consist from scratch
                {
                    std::vector<ConsistReader::UnitInfo> rebuiltUnits;
                    double maxVel = (sess.trainCfg.maxVelocity > 0.0) ? sess.trainCfg.maxVelocity : 120.0;
                    double perfFactor = (sess.trainCfg.perfFactor > 0.0) ? sess.trainCfg.perfFactor : 1.0;
                    if (PoolMutator::GenerateFullConsistFromSourceNode(bcon.assignedSourceId, options, rebuiltUnits, err, &maxVel, &perfFactor))
                    {
                        sess.units = rebuiltUnits;
                        sess.trainCfg.maxVelocity = maxVel;
                        sess.trainCfg.perfFactor = perfFactor;
                        repCount = (int)rebuiltUnits.size();
                        okApply = true;
                    }
                    else
                    {
                        outResult.errorMessage = err;
                    }
                }
                else // In-Place Unit Replacement
                {
                    okApply = PoolMutator::RepairUnitsVector(sess.units, bcon.brokenUnits, options, err, repCount);
                }

                if (okApply)
                {
                    sess.isDirty = true;
                    outResult.processedCount++;
                    outResult.repairedUnitsCount += repCount;
                    outResult.affectedFiles.push_back(path);
                    anyRepaired = true;

                    for (size_t i = 0; i < sess.units.size(); ++i)
                    {
                        bool isNowBroken = IsUnitBrokenOnDisk(sess.units[i], g_szBasePath);
                        if (i < wasBrokenBefore.size() && wasBrokenBefore[i] && !isNowBroken)
                        {
                            g_SessionFixedUnitsPerConsist[filename].insert((int)i);
                        }
                        else if (isNowBroken)
                        {
                            g_SessionFixedUnitsPerConsist[filename].erase((int)i);
                        }
                    }

                    if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
                    {
                        g_LoadedConsistUnits = sess.units;
                        RefreshEditorUnitList(false);
                        UpdateConsistMetricsUI();
                        if (g_hVisualConsistView)
                        {
                            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                        }
                    }

                    UpdateConsistManagerRow(filename);
                }
            }
        }

        outResult.success = anyRepaired;
        if (!outResult.success && outResult.errorMessage.empty())
        {
            outResult.errorMessage = L"No consists or broken units were selected for replacement / rebuild.";
        }
        return outResult.success;
    }

    std::vector<std::wstring> paths = targetConsistPaths;
    if (paths.empty())
    {
        if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
        {
            paths.push_back(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex));
        }
        else if (!g_szCurrentConsistFile.empty())
        {
            std::wstring fullPath = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
            if (!fullPath.empty()) paths.push_back(fullPath);
        }
    }

    if (paths.empty())
    {
        outResult.errorMessage = L"No consist selected for operation.";
        return false;
    }

    if (options.createClones)
    {
        bool hasActivity = false;
        for (const auto& p : paths)
        {
            if (p.rfind(L"ACTIVITY:", 0) == 0) { hasActivity = true; break; }
        }

        if (!hasActivity)
        {
            if (options.mode == PoolMutator::MutatorMode::MutateConsists)
                outResult = PoolMutator::MutateConsistFiles(paths, options, g_szBasePath);
            else if (options.mode == PoolMutator::MutatorMode::ReplaceSelected)
                outResult = PoolMutator::ReplaceUnitsInConsist(paths[0], targetUnitIndices, options, g_szBasePath);
            else if (options.mode == PoolMutator::MutatorMode::InsertUnits)
                outResult = PoolMutator::InsertUnitsIntoConsists(paths, options, g_szBasePath);

            if (outResult.success)
            {
                TriggerConsistsRescan(hWnd);
            }
            return outResult.success;
        }
    }

    bool anyModified = false;
    for (const auto& path : paths)
    {
        if (path.rfind(L"ACTIVITY:", 0) == 0)
        {
            int cIdx = _wtoi(path.substr(9).c_str());
            if (cIdx < 0 || cIdx >= (int)g_CurrentActivityData.consists.size()) continue;

            auto& con = g_CurrentActivityData.consists[cIdx];
            std::wstring consistKey = g_CurrentActivityFilePath + L"#" + con.id;

            if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
            {
                con.units = g_LoadedConsistUnits;
            }

            std::vector<bool> wasBrokenBefore(con.units.size(), false);
            for (size_t i = 0; i < con.units.size(); ++i)
            {
                wasBrokenBefore[i] = IsUnitBrokenOnDisk(con.units[i], g_szBasePath);
            }

            if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
            {
                PushUndoState(L"Pool Mutation");
            }

            std::wstring err;
            bool mutated = false;
            if (options.mode == PoolMutator::MutatorMode::MutateConsists)
            {
                mutated = PoolMutator::MutateUnitsVector(con.units, options, err);
            }
            else if (options.mode == PoolMutator::MutatorMode::ReplaceSelected)
            {
                mutated = PoolMutator::ReplaceUnitsVector(con.units, targetUnitIndices, options, err);
            }
            else if (options.mode == PoolMutator::MutatorMode::InsertUnits)
            {
                mutated = PoolMutator::InsertUnitsVector(con.units, options, err);
            }

            if (mutated)
            {
                con.isDirty = true;
                con.totalUnits = (int)con.units.size();
                outResult.processedCount++;
                std::wstring dispName = con.name.empty() ? (L"Activity Consist #" + std::to_wstring(cIdx + 1)) : con.name;
                outResult.affectedFiles.push_back(dispName);
                anyModified = true;

                // Track fixed broken units in session
                for (size_t i = 0; i < con.units.size(); ++i)
                {
                    bool isNowBroken = IsUnitBrokenOnDisk(con.units[i], g_szBasePath);
                    if (i < wasBrokenBefore.size() && wasBrokenBefore[i] && !isNowBroken)
                    {
                        g_SessionFixedUnitsPerConsist[consistKey].insert((int)i);
                    }
                    else if (isNowBroken)
                    {
                        g_SessionFixedUnitsPerConsist[consistKey].erase((int)i);
                    }
                }

                if (g_ActiveTab == 1 && cIdx == g_CurrentActivityConsistIndex)
                {
                    g_LoadedConsistUnits = con.units;
                    RefreshEditorUnitList(false);
                    UpdateConsistMetricsUI();
                    if (g_hVisualConsistView)
                    {
                        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                    }
                }

                // Update row in g_ConsistList
                for (int r = 0; r < g_ConsistList.GetItemCount(); ++r)
                {
                    std::wstring rIdxStr = g_ConsistList.GetCellText(r, 3);
                    if (_wtoi(rIdxStr.c_str()) == cIdx)
                    {
                        std::wstring curName = g_ConsistList.GetCellText((int)r, 0);
                        if (curName.rfind(L"● ", 0) != 0)
                        {
                            g_ConsistList.SetCellText((int)r, 0, L"● " + curName);
                        }
                        g_ConsistList.SetCellText((int)r, 1, std::to_wstring(con.totalUnits));
                        std::wstring statusStr = EvaluateAndUpdateConsistStatus(consistKey, con.units, !con.isDirty);
                        g_ConsistList.SetCellText((int)r, 2, statusStr);
                        g_ConsistList.Invalidate();
                        break;
                    }
                }
            }
            else if (outResult.errorMessage.empty() && !err.empty())
            {
                outResult.errorMessage = err;
            }
            continue;
        }

        wchar_t fname[MAX_PATH] = { 0 };
        _wsplitpath_s(path.c_str(), nullptr, 0, nullptr, 0, fname, MAX_PATH, nullptr, 0);
        std::wstring filename = std::wstring(fname) + L".con";

        if (g_ConsistSessions.find(filename) == g_ConsistSessions.end())
        {
            if (!PathFileExistsW(path.c_str())) continue;
            try {
                auto data = ConsistReader::LoadConsist(path);
                auto& sess = g_ConsistSessions[filename];
                sess.fileName = filename;
                sess.trainCfg = data.trainCfg;
                sess.units = data.units;
                sess.isDirty = false;
            } catch (...) {
                continue;
            }
        }

        auto& sess = g_ConsistSessions[filename];
        if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
        {
            sess.units = g_LoadedConsistUnits;
        }

        std::vector<bool> wasBrokenBefore(sess.units.size(), false);
        for (size_t i = 0; i < sess.units.size(); ++i)
        {
            wasBrokenBefore[i] = IsUnitBrokenOnDisk(sess.units[i], g_szBasePath);
        }

        if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
        {
            PushUndoState(L"Pool Mutation");
        }

        std::wstring err;
        bool mutated = false;
        if (options.mode == PoolMutator::MutatorMode::MutateConsists)
        {
            mutated = PoolMutator::MutateUnitsVector(sess.units, options, err);
        }
        else if (options.mode == PoolMutator::MutatorMode::ReplaceSelected)
        {
            mutated = PoolMutator::ReplaceUnitsVector(sess.units, targetUnitIndices, options, err);
        }
        else if (options.mode == PoolMutator::MutatorMode::InsertUnits)
        {
            mutated = PoolMutator::InsertUnitsVector(sess.units, options, err);
        }

        if (mutated)
        {
            sess.isDirty = true;
            outResult.processedCount++;
            outResult.affectedFiles.push_back(path);
            anyModified = true;

            // Track fixed broken units in session
            for (size_t i = 0; i < sess.units.size(); ++i)
            {
                bool isNowBroken = IsUnitBrokenOnDisk(sess.units[i], g_szBasePath);
                if (i < wasBrokenBefore.size() && wasBrokenBefore[i] && !isNowBroken)
                {
                    g_SessionFixedUnitsPerConsist[filename].insert((int)i);
                }
                else if (isNowBroken)
                {
                    g_SessionFixedUnitsPerConsist[filename].erase((int)i);
                }
            }

            if (g_ActiveTab == 0 && !g_szCurrentConsistFile.empty() && _wcsicmp(g_szCurrentConsistFile.c_str(), filename.c_str()) == 0)
            {
                g_LoadedConsistUnits = sess.units;
                RefreshEditorUnitList(false);
                UpdateConsistMetricsUI();
                if (g_hVisualConsistView)
                {
                    VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                }
            }

            UpdateConsistManagerRow(filename);
        }
        else if (outResult.errorMessage.empty() && !err.empty())
        {
            outResult.errorMessage = err;
        }
    }

    outResult.success = anyModified;
    if (!outResult.success && outResult.errorMessage.empty())
    {
        outResult.errorMessage = L"No consists could be mutated.";
    }

    return outResult.success;
}

static void PushUndoState(const std::wstring& actionDesc)
{
    ConsistHistoryStep step;
    step.units = g_LoadedConsistUnits;
    step.actionDesc = actionDesc;
    g_UndoStack.push_back(step);
    g_RedoStack.clear();
    if (g_UndoStack.size() > 100)
    {
        g_UndoStack.erase(g_UndoStack.begin());
    }

    if (!g_szCurrentConsistFile.empty())
    {
        g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    else if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
    {
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].isDirty = true;
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            std::wstring curName = g_ConsistList.GetCellText(sel, 0);
            if (curName.rfind(L"● ", 0) != 0)
            {
                g_ConsistList.SetCellText(sel, 0, L"● " + curName);
                g_ConsistList.Invalidate();
            }
        }
    }
}

static void PerformUndo(HWND hWnd)
{
    if (g_UndoStack.empty()) return;
    ConsistHistoryStep step = g_UndoStack.back();
    g_UndoStack.pop_back();

    ConsistHistoryStep redoStep;
    redoStep.units = g_LoadedConsistUnits;
    redoStep.actionDesc = step.actionDesc;
    g_RedoStack.push_back(redoStep);

    g_LoadedConsistUnits = step.units;
    RefreshEditorUnitList();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);

    if (!g_szCurrentConsistFile.empty())
    {
        if (!g_bAutoSave)
        {
            g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
        }
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    else if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
    {
        if (!g_bAutoSave)
        {
            g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].isDirty = true;
        }
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].units = g_LoadedConsistUnits;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].totalUnits = (int)g_LoadedConsistUnits.size();
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            std::wstring curName = g_ConsistList.GetCellText(sel, 0);
            if (!g_bAutoSave && curName.rfind(L"● ", 0) != 0)
            {
                g_ConsistList.SetCellText(sel, 0, L"● " + curName);
            }
            g_ConsistList.SetCellText(sel, 1, std::to_wstring(g_LoadedConsistUnits.size()));
            std::wstring statusStr = EvaluateAndUpdateConsistStatus(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex), g_LoadedConsistUnits, g_bAutoSave);
            g_ConsistList.SetCellText(sel, 2, statusStr);
            g_ConsistList.Invalidate();
        }
    }
}

static void PerformRedo(HWND hWnd)
{
    if (g_RedoStack.empty()) return;
    ConsistHistoryStep step = g_RedoStack.back();
    g_RedoStack.pop_back();

    ConsistHistoryStep undoStep;
    undoStep.units = g_LoadedConsistUnits;
    undoStep.actionDesc = step.actionDesc;
    g_UndoStack.push_back(undoStep);

    g_LoadedConsistUnits = step.units;
    RefreshEditorUnitList();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);

    if (!g_szCurrentConsistFile.empty())
    {
        if (!g_bAutoSave)
        {
            g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
        }
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    else if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
    {
        if (!g_bAutoSave)
        {
            g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].isDirty = true;
        }
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].units = g_LoadedConsistUnits;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].totalUnits = (int)g_LoadedConsistUnits.size();
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            std::wstring curName = g_ConsistList.GetCellText(sel, 0);
            if (!g_bAutoSave && curName.rfind(L"● ", 0) != 0)
            {
                g_ConsistList.SetCellText(sel, 0, L"● " + curName);
            }
            g_ConsistList.SetCellText(sel, 1, std::to_wstring(g_LoadedConsistUnits.size()));
            std::wstring statusStr = EvaluateAndUpdateConsistStatus(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex), g_LoadedConsistUnits, g_bAutoSave);
            g_ConsistList.SetCellText(sel, 2, statusStr);
            g_ConsistList.Invalidate();
        }
    }
}

static std::vector<ConsistReader::UnitInfo> GetSelectedStockUnitsFromLibrary(HWND hSpecificList = NULL)
{
    std::vector<ConsistReader::UnitInfo> units;
    std::unordered_set<std::wstring> seenKeys;

    HWND targetLists[3] = { g_hAssetList, g_hAssetList2, g_hAssetList3 };
    int listCount = (g_StockViewMode == StockViewMode::Single) ? 1 : ((g_StockViewMode == StockViewMode::Dual) ? 2 : 3);

    EnterCriticalSection(&g_StockCacheCS);

    for (int k = 0; k < listCount; ++k)
    {
        if (targetLists[k])
        {
            CustomListControl* pList = GetAssetListCtrl(k);
            std::vector<int> selIndices = pList->GetSelectedIndices();
            if (selIndices.empty())
            {
                int singleSel = pList->GetSelectedIndex();
                if (singleSel >= 0) selIndices.push_back(singleSel);
            }

            for (int selIdx : selIndices)
            {
                if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndicesPane[k].size())
                {
                    size_t cacheIdx = g_FilteredStockIndicesPane[k][selIdx];
                    if (cacheIdx < g_StockCache.size())
                    {
                        const auto& item = g_StockCache[cacheIdx];
                        std::wstring key = item.szFolder + L"\\" + item.szFileName;
                        if (seenKeys.insert(key).second)
                        {
                            ConsistReader::UnitInfo u;
                            u.uid = item.szFileName;
                            u.parentDir = item.szFolder;
                            u.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                            u.isFlipped = false;
                            units.push_back(u);
                        }
                    }
                }
            }
        }
    }

    LeaveCriticalSection(&g_StockCacheCS);
    return units;
}

static void DeleteSelectedConsistUnits(HWND hWnd)
{
    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
    if (selIndices.empty()) return;

    PushUndoState(L"Delete Units");
    std::sort(selIndices.rbegin(), selIndices.rend());
    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
        {
            g_LoadedConsistUnits.erase(g_LoadedConsistUnits.begin() + idx);
        }
    }
    g_CheckedConsistUnits.clear();
    g_EditorUnitList.ClearSelection();
    RefreshEditorUnitList();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
}

static std::wstring NormalizeStockUid(const std::wstring& s)
{
    std::wstring str = s;
    const wchar_t* ws = L" \t\r\n\"'";
    size_t start = str.find_first_not_of(ws);
    size_t end = str.find_last_not_of(ws);
    if (start == std::wstring::npos) return L"";
    str = str.substr(start, end - start + 1);

    if (str.size() > 4)
    {
        std::wstring ext = str.substr(str.size() - 4);
        if (_wcsicmp(ext.c_str(), L".eng") == 0 || _wcsicmp(ext.c_str(), L".wag") == 0)
        {
            str = str.substr(0, str.size() - 4);
        }
    }
    return str;
}

enum ReplacementScope {
    SCOPE_SELECTED_ROWS,
    SCOPE_ALL_MATCHING
};

static void TransferStockUnitsToConsist(HWND hWnd, const std::vector<int>& stockIndices, int targetInsertPos, int paneIdx = 0)
{
    if (stockIndices.empty()) return;
    if (paneIdx < 0 || paneIdx >= 3) paneIdx = 0;

    PushUndoState(L"Insert Transferred Stock Units");

    std::vector<ConsistReader::UnitInfo> toInsert;
    EnterCriticalSection(&g_StockCacheCS);
    for (int selIdx : stockIndices)
    {
        if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndicesPane[paneIdx].size())
        {
            size_t cacheIdx = g_FilteredStockIndicesPane[paneIdx][selIdx];
            if (cacheIdx < g_StockCache.size())
            {
                const auto& item = g_StockCache[cacheIdx];
                ConsistReader::UnitInfo u;
                u.uid = item.szFileName;
                u.parentDir = item.szFolder;
                u.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                u.isFlipped = false;
                toInsert.push_back(u);
            }
        }
    }
    LeaveCriticalSection(&g_StockCacheCS);

    if (toInsert.empty()) return;

    int total = (int)g_LoadedConsistUnits.size();
    int dropPos = targetInsertPos;
    if (dropPos < 0) dropPos = total;
    if (dropPos > total) dropPos = total;

    g_LoadedConsistUnits.insert(g_LoadedConsistUnits.begin() + dropPos, toInsert.begin(), toInsert.end());

    RefreshEditorUnitList();

    std::vector<int> newSel;
    for (size_t k = 0; k < toInsert.size(); ++k)
    {
        newSel.push_back(dropPos + (int)k);
    }
    SetSelectedConsistUnitIndices(newSel);

    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }

    SaveCurrentConsist(hWnd);
}

static void ReorderConsistUnits(HWND hWnd, const std::vector<int>& sourceIndices, int targetInsertPos)
{
    if (sourceIndices.empty() || g_LoadedConsistUnits.empty()) return;

    PushUndoState(L"Reorder Units");

    std::vector<int> sortedIndices = sourceIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end());

    std::vector<ConsistReader::UnitInfo> movedUnits;
    for (int idx : sortedIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
        {
            movedUnits.push_back(g_LoadedConsistUnits[idx]);
        }
    }

    // Calculate how many removed items were BEFORE targetInsertPos to adjust insertion index
    int adjustedTarget = targetInsertPos;
    for (int idx : sortedIndices)
    {
        if (idx < targetInsertPos)
        {
            adjustedTarget--;
        }
    }
    if (adjustedTarget < 0) adjustedTarget = 0;

    // Erase moved units from backwards
    for (auto it = sortedIndices.rbegin(); it != sortedIndices.rend(); ++it)
    {
        if (*it >= 0 && *it < (int)g_LoadedConsistUnits.size())
        {
            g_LoadedConsistUnits.erase(g_LoadedConsistUnits.begin() + *it);
        }
    }

    if (adjustedTarget > (int)g_LoadedConsistUnits.size())
        adjustedTarget = (int)g_LoadedConsistUnits.size();

    // Insert at adjusted target
    g_LoadedConsistUnits.insert(g_LoadedConsistUnits.begin() + adjustedTarget, movedUnits.begin(), movedUnits.end());

    RefreshEditorUnitList();
    std::vector<int> newSel;
    for (size_t i = 0; i < movedUnits.size(); ++i)
    {
        newSel.push_back(adjustedTarget + (int)i);
    }
    SetSelectedConsistUnitIndices(newSel);

    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
}

static bool ExecuteConsistReplacement(HWND hWnd, ReplacementScope scope, const std::vector<ConsistReader::UnitInfo>* pCustomSource = nullptr)
{
    const std::vector<ConsistReader::UnitInfo>& sourceUnits = (pCustomSource && !pCustomSource->empty()) ? *pCustomSource : g_ClipboardUnits;

    if (sourceUnits.empty())
    {
        ShowModernMessageBox(hWnd, L"Clipboard is currently empty.\r\n\r\nPlease copy unit(s) using Ctrl+C from the Consist table or Stock Library first.", L"Replace Units", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
    if (selIndices.empty())
    {
        ShowModernMessageBox(hWnd, L"Please select at least one unit in the Consist Units table to replace.", L"Replace Units", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::sort(selIndices.begin(), selIndices.end());

    std::vector<int> newSelection;

    if (scope == SCOPE_SELECTED_ROWS)
    {
        PushUndoState(L"Replace Selected Units");

        if (sourceUnits.size() == 1)
        {
            // Case A: 1 unit in clipboard / stock -> 1-to-Many stamp across all selected rows
            const auto& src = sourceUnits[0];
            for (int idx : selIndices)
            {
                if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
                {
                    bool wasBroken = IsUnitBrokenOnDisk(g_LoadedConsistUnits[idx], g_szBasePath);
                    g_LoadedConsistUnits[idx].uid = src.uid;
                    g_LoadedConsistUnits[idx].parentDir = src.parentDir;
                    g_LoadedConsistUnits[idx].isEngine = src.isEngine;
                    std::wstring activeConsistKey = GetCurrentActiveConsistKey();
                    if (!activeConsistKey.empty())
                    {
                        if (wasBroken)
                            g_SessionFixedUnitsPerConsist[activeConsistKey].insert(idx);
                        else
                            g_SessionFixedUnitsPerConsist[activeConsistKey].erase(idx);
                    }
                }
            }
            newSelection = selIndices;
        }
        else
        {
            // Case B & C: Multiple units in clipboard -> Sequential 1-to-1 replacement + Overflow insertion
            size_t M = selIndices.size();
            size_t N = sourceUnits.size();
            size_t replaceCount = (std::min)(M, N);

            // 1. Replace the first min(M, N) selected rows in sequence
            for (size_t i = 0; i < replaceCount; ++i)
            {
                int idx = selIndices[i];
                if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
                {
                    bool wasBroken = IsUnitBrokenOnDisk(g_LoadedConsistUnits[idx], g_szBasePath);
                    g_LoadedConsistUnits[idx].uid = sourceUnits[i].uid;
                    g_LoadedConsistUnits[idx].parentDir = sourceUnits[i].parentDir;
                    g_LoadedConsistUnits[idx].isEngine = sourceUnits[i].isEngine;
                    g_LoadedConsistUnits[idx].isFlipped = sourceUnits[i].isFlipped;
                    std::wstring activeConsistKey = GetCurrentActiveConsistKey();
                    if (!activeConsistKey.empty())
                    {
                        if (wasBroken)
                            g_SessionFixedUnitsPerConsist[activeConsistKey].insert(idx);
                        else
                            g_SessionFixedUnitsPerConsist[activeConsistKey].erase(idx);
                    }
                    newSelection.push_back(idx);
                }
            }

            // 2. If N > M (Overflow), insert remaining (N - M) units right after the last selected row
            if (N > M)
            {
                int lastSelIdx = selIndices.back();
                int insertPos = lastSelIdx + 1;
                if (insertPos > (int)g_LoadedConsistUnits.size())
                    insertPos = (int)g_LoadedConsistUnits.size();

                std::vector<ConsistReader::UnitInfo> overflowUnits(sourceUnits.begin() + M, sourceUnits.end());
                g_LoadedConsistUnits.insert(g_LoadedConsistUnits.begin() + insertPos, overflowUnits.begin(), overflowUnits.end());

                for (size_t k = 0; k < (N - M); ++k)
                {
                    newSelection.push_back(lastSelIdx + 1 + (int)k);
                }
            }
        }
        RefreshEditorUnitList();
    }
    else if (scope == SCOPE_ALL_MATCHING)
    {
        std::unordered_set<std::wstring> targetNormalizedNames;
        for (int idx : selIndices)
        {
            if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
            {
                std::wstring n = NormalizeStockUid(g_LoadedConsistUnits[idx].uid);
                if (!n.empty())
                {
                    targetNormalizedNames.insert(n);
                }
            }
        }

        if (targetNormalizedNames.empty()) return false;

        const auto& src = sourceUnits[0];
        PushUndoState(L"Replace All with Same Name");
        for (size_t i = 0; i < g_LoadedConsistUnits.size(); ++i)
        {
            std::wstring curUidNorm = NormalizeStockUid(g_LoadedConsistUnits[i].uid);
            if (targetNormalizedNames.count(curUidNorm) > 0)
            {
                bool wasBroken = IsUnitBrokenOnDisk(g_LoadedConsistUnits[i], g_szBasePath);
                g_LoadedConsistUnits[i].uid = src.uid;
                g_LoadedConsistUnits[i].parentDir = src.parentDir;
                g_LoadedConsistUnits[i].isEngine = src.isEngine;
                std::wstring activeConsistKey = GetCurrentActiveConsistKey();
                if (!activeConsistKey.empty())
                {
                    if (wasBroken)
                        g_SessionFixedUnitsPerConsist[activeConsistKey].insert((int)i);
                    else
                        g_SessionFixedUnitsPerConsist[activeConsistKey].erase((int)i);
                }
                newSelection.push_back((int)i);
            }
        }
        RefreshEditorUnitList();
    }

    if (!newSelection.empty())
    {
        SetSelectedConsistUnitIndices(newSelection);
    }
    g_EditorUnitList.Invalidate();

    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
    return true;
}

static void ReplaceSelectedConsistUnits(HWND hWnd)
{
    ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS);
}

static void ReplaceAllConsistUnitsWithName(HWND hWnd, const std::wstring& targetUid)
{
    ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING);
}

static void ExecuteReplacementFromGroup(HWND hWnd, int groupIdx, const std::vector<int>& selIndices)
{
    if (groupIdx < 0 || groupIdx >= (int)PoolManager::g_ReplacementGroupsCache.size() || selIndices.empty())
        return;

    const auto& grp = PoolManager::g_ReplacementGroupsCache[groupIdx];
    if (grp.units.empty())
    {
        ShowModernMessageBox(hWnd, L"The selected replacement group contains no stock units.\n\nAdd units to this group in the Pool Manager before replacing.", L"Empty Replacement Group", MB_OK | MB_ICONWARNING);
        return;
    }

    PushUndoState(L"Replace with " + grp.name);

    for (size_t s = 0; s < selIndices.size(); ++s)
    {
        int selIdx = selIndices[s];
        if (selIdx >= 0 && selIdx < (int)g_LoadedConsistUnits.size())
        {
            PoolManager::PoolUnit pickedUnit;
            if (PoolManager::PickUnitFromGroup(groupIdx, pickedUnit, (int)s))
            {
                g_LoadedConsistUnits[selIdx].uid = pickedUnit.szFileName;
                g_LoadedConsistUnits[selIdx].parentDir = pickedUnit.szFolder;
                g_LoadedConsistUnits[selIdx].isEngine = pickedUnit.isEngine;
                if (pickedUnit.flipMode == PoolManager::UnitFlipMode::Flipped)
                    g_LoadedConsistUnits[selIdx].isFlipped = true;
                else if (pickedUnit.flipMode == PoolManager::UnitFlipMode::Forward)
                    g_LoadedConsistUnits[selIdx].isFlipped = false;
                else if (pickedUnit.flipMode == PoolManager::UnitFlipMode::Random)
                    g_LoadedConsistUnits[selIdx].isFlipped = (rand() % 2 == 1);
                else // Auto -> follow group flipPolicy
                {
                    if (grp.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                        g_LoadedConsistUnits[selIdx].isFlipped = true;
                    else if (grp.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                        g_LoadedConsistUnits[selIdx].isFlipped = (rand() % 2 == 1);
                    else
                        g_LoadedConsistUnits[selIdx].isFlipped = false;
                }
            }
        }
    }

    RefreshEditorUnitList();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
}

static void FlipSelectedConsistUnits(HWND hWnd)
{
    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
    if (selIndices.empty()) return;

    PushUndoState(L"Flip Units");
    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
        {
            g_LoadedConsistUnits[idx].isFlipped = !g_LoadedConsistUnits[idx].isFlipped;
        }
    }
    RefreshEditorUnitList();
    g_EditorUnitList.Invalidate();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
}

static bool CutSelectedConsistUnits(HWND hWnd)
{
    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
    if (selIndices.empty()) return false;

    // 1. Copy selected units to clipboard in order
    g_ClipboardUnits.clear();
    std::vector<int> sortedIndices = selIndices;
    std::sort(sortedIndices.begin(), sortedIndices.end());
    for (int idx : sortedIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
            g_ClipboardUnits.push_back(g_LoadedConsistUnits[idx]);
    }

    if (g_ClipboardUnits.empty()) return false;

    // 2. Delete selected units from workspace with undo tracking
    PushUndoState(L"Cut Units");
    std::sort(selIndices.rbegin(), selIndices.rend());
    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
        {
            g_LoadedConsistUnits.erase(g_LoadedConsistUnits.begin() + idx);
        }
    }
    g_EditorUnitList.ClearSelection();
    RefreshEditorUnitList();
    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
    return true;
}

static bool CopySelectedConsistUnits(HWND hWnd)
{
    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
    if (selIndices.empty()) return false;
    g_ClipboardUnits.clear();
    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
            g_ClipboardUnits.push_back(g_LoadedConsistUnits[idx]);
    }
    return !g_ClipboardUnits.empty();
}

static bool CopySelectedStockUnits(HWND hWnd)
{
    std::vector<ConsistReader::UnitInfo> stock = GetSelectedStockUnitsFromLibrary();
    if (stock.empty()) return false;
    g_ClipboardUnits = stock;
    return !g_ClipboardUnits.empty();
}

static void CopySelectedUnitOrStock(HWND hWnd)
{
    if (g_ActivePane == PANE_STOCK)
    {
        if (CopySelectedStockUnits(hWnd)) return;
    }
    else if (g_ActivePane == PANE_WORKSPACE)
    {
        if (CopySelectedConsistUnits(hWnd)) return;
    }

    HWND hFocus = GetFocus();
    bool isConsistFocus = (hFocus == g_hEditorUnitList || (g_hEditorUnitList && IsChild(g_hEditorUnitList, hFocus)));
    bool isStockFocus = (hFocus == g_hAssetList || hFocus == g_hAssetList2 || hFocus == g_hAssetList3 ||
                         hFocus == g_hCategoryTree ||
                         (g_hAssetList && IsChild(g_hAssetList, hFocus)) ||
                         (g_hAssetList2 && IsChild(g_hAssetList2, hFocus)) ||
                         (g_hAssetList3 && IsChild(g_hAssetList3, hFocus)) ||
                         (g_hCategoryTree && IsChild(g_hCategoryTree, hFocus)));

    if (isConsistFocus)
    {
        if (CopySelectedConsistUnits(hWnd)) return;
    }
    else if (isStockFocus)
    {
        if (CopySelectedStockUnits(hWnd)) return;
    }

    if (!g_CheckedConsistUnits.empty())
    {
        if (CopySelectedConsistUnits(hWnd)) return;
    }

    if (g_ActivePane == PANE_STOCK)
    {
        if (CopySelectedStockUnits(hWnd)) return;
    }
    CopySelectedConsistUnits(hWnd);
}

static void CopySelectedUnitNamesToClipboard(HWND hWnd)
{
    std::wstring textToCopy = L"";

    if (g_ActivePane == PANE_STOCK)
    {
        std::vector<ConsistReader::UnitInfo> stock = GetSelectedStockUnitsFromLibrary();
        for (const auto& u : stock)
        {
            if (!textToCopy.empty()) textToCopy += L"\r\n";
            textToCopy += u.uid;
        }
    }
    else // PANE_WORKSPACE or PANE_CONSIST
    {
        std::vector<int> selIndices = GetSelectedConsistUnitIndices();
        for (int uIdx : selIndices)
        {
            if (uIdx >= 0 && uIdx < (int)g_LoadedConsistUnits.size())
            {
                if (!textToCopy.empty()) textToCopy += L"\r\n";
                textToCopy += g_LoadedConsistUnits[uIdx].uid;
            }
        }
    }

    if (textToCopy.empty())
    {
        if (g_ActivePane != PANE_STOCK)
        {
            std::vector<ConsistReader::UnitInfo> stock = GetSelectedStockUnitsFromLibrary();
            for (const auto& u : stock)
            {
                if (!textToCopy.empty()) textToCopy += L"\r\n";
                textToCopy += u.uid;
            }
        }
        else
        {
            std::vector<int> selIndices = GetSelectedConsistUnitIndices();
            for (int uIdx : selIndices)
            {
                if (uIdx >= 0 && uIdx < (int)g_LoadedConsistUnits.size())
                {
                    if (!textToCopy.empty()) textToCopy += L"\r\n";
                    textToCopy += g_LoadedConsistUnits[uIdx].uid;
                }
            }
        }
    }

    if (!textToCopy.empty() && OpenClipboard(hWnd))
    {
        EmptyClipboard();
        size_t cch = textToCopy.size() + 1;
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, cch * sizeof(wchar_t));
        if (hGlob)
        {
            wchar_t* pBuf = (wchar_t*)GlobalLock(hGlob);
            if (pBuf)
            {
                wcscpy_s(pBuf, cch, textToCopy.c_str());
                GlobalUnlock(hGlob);
                SetClipboardData(CF_UNICODETEXT, hGlob);
            }
            else
            {
                GlobalFree(hGlob);
            }
        }
        CloseClipboard();
    }
}

enum PasteTargetMode {
    PASTE_START,
    PASTE_AFTER_SELECTED,
    PASTE_END
};

static void PasteConsistUnits(HWND hWnd, PasteTargetMode mode = PASTE_AFTER_SELECTED, const std::vector<ConsistReader::UnitInfo>* pCustomUnits = nullptr)
{
    const std::vector<ConsistReader::UnitInfo>& toPaste = (pCustomUnits && !pCustomUnits->empty()) ? *pCustomUnits : g_ClipboardUnits;

    if (toPaste.empty())
    {
        ShowModernMessageBox(hWnd, L"Clipboard is currently empty.\r\n\r\nPlease copy unit(s) using Ctrl+C from the Consist table or Stock Library first.", L"Paste Units", MB_OK | MB_ICONINFORMATION);
        return;
    }

    PushUndoState(L"Paste Units");

    int insertPos = 0;
    if (g_LoadedConsistUnits.empty() || mode == PASTE_START)
    {
        insertPos = 0;
    }
    else if (mode == PASTE_END)
    {
        insertPos = (int)g_LoadedConsistUnits.size();
    }
    else // PASTE_AFTER_SELECTED
    {
        int sel = GetSelectedConsistUnitIndex();
        if (sel >= 0 && sel < (int)g_LoadedConsistUnits.size())
        {
            insertPos = sel + 1;
        }
        else
        {
            insertPos = (int)g_LoadedConsistUnits.size();
        }
    }

    g_LoadedConsistUnits.insert(g_LoadedConsistUnits.begin() + insertPos, toPaste.begin(), toPaste.end());

    RefreshEditorUnitList();
    std::vector<int> newSel;
    for (size_t i = 0; i < toPaste.size(); ++i)
    {
        newSel.push_back(insertPos + (int)i);
    }
    SetSelectedConsistUnitIndices(newSel);

    if (g_hVisualConsistView)
    {
        VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
    }
    SaveCurrentConsist(hWnd);
}

static void ShowClipboardContents(HWND hWnd)
{
    if (g_ClipboardUnits.empty())
    {
        ShowModernMessageBox(hWnd, L"Clipboard is currently empty.\r\n\r\nYou can copy units using Ctrl+C from the Consist table or Stock Library.", L"Clipboard Contents", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring msg = L"Clipboard Contents (" + std::to_wstring(g_ClipboardUnits.size()) + L" Units):\r\n";
    msg += L"────────────────────────────────────────────────────────\r\n";

    for (size_t i = 0; i < g_ClipboardUnits.size(); ++i)
    {
        const auto& u = g_ClipboardUnits[i];

        bool isBroken = false;
        if (u.uid.empty() || u.parentDir.empty())
        {
            isBroken = true;
        }
        else
        {
            std::wstring ext = u.isEngine ? L".eng" : L".wag";
            std::wstring unitPath = g_szBasePath;
            if (!unitPath.empty() && unitPath.back() != L'\\') unitPath += L'\\';
            unitPath += L"TRAINS\\TRAINSET\\" + u.parentDir + L"\\" + u.uid + ext;

            DWORD attr = GetFileAttributesW(unitPath.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                isBroken = true;
            }
        }

        wchar_t lineBuf[256];
        swprintf_s(lineBuf, 256, L"#%d  %s (%s)\r\n    • Folder: %s\r\n    • Status: %s\r\n    • Orientation: %s\r\n\r\n",
            (int)(i + 1),
            u.uid.c_str(),
            u.isEngine ? L"Engine" : L"Wagon",
            u.parentDir.c_str(),
            isBroken ? L"✖ Broken (Missing)" : L"✔ Healthy",
            u.isFlipped ? L"🠔 Flipped (Reversed)" : L"➔ Normal");
        msg += lineBuf;
    }

    std::vector<ModernMsgBoxCustomButton> buttons = {
        { 101, L"Clear Contents", false },
        { IDOK, L"Close", true }
    };

    int result = ShowModernMessageBoxEx(hWnd, msg.c_str(), L"Clipboard Contents", buttons, MB_ICONINFORMATION);
    if (result == 101)
    {
        ClearClipboard(hWnd);
    }
}

static void ClearClipboard(HWND hWnd)
{
    g_ClipboardUnits.clear();
    ShowModernMessageBox(hWnd, L"Clipboard has been cleared successfully.", L"Clear Clipboard", MB_OK | MB_ICONINFORMATION);
}


static bool SaveCurrentConsistDiskOnly(HWND hWnd)
{
    if (g_szCurrentConsistFile.empty() || g_szBasePath.empty())
        return false;

    SaveCurrentConsistSessionState();

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\')
        consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";

    wchar_t szRawFileName[256] = { 0 };
    if (g_hEditFileName) GetWindowTextW(g_hEditFileName, szRawFileName, 256);
    std::wstring targetFileName = szRawFileName;
    while (!targetFileName.empty() && iswspace(targetFileName.front())) targetFileName.erase(0, 1);
    while (!targetFileName.empty() && iswspace(targetFileName.back())) targetFileName.pop_back();

    for (wchar_t& c : targetFileName)
    {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'\"' || c == L'<' || c == L'>' || c == L'|')
        {
            c = L'_';
        }
    }

    if (targetFileName.empty())
    {
        targetFileName = g_szCurrentConsistFile;
    }
    else if (targetFileName.length() < 4 || _wcsicmp(targetFileName.c_str() + targetFileName.length() - 4, L".con") != 0)
    {
        targetFileName += L".con";
    }

    if (g_hEditFileName) SetWindowTextW(g_hEditFileName, targetFileName.c_str());

    std::wstring oldFileName = g_szCurrentConsistFile;
    std::wstring oldFullPath = consistFolder + oldFileName;
    std::wstring newFullPath = consistFolder + targetFileName;

    bool isRenaming = (_wcsicmp(targetFileName.c_str(), oldFileName.c_str()) != 0);

    if (isRenaming)
    {
        DWORD attr = GetFileAttributesW(newFullPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES)
        {
            std::wstring prompt = L"A consist file named '" + targetFileName + L"' already exists.\r\n\r\nDo you want to overwrite it?";
            if (ShowModernMessageBox(hWnd, prompt.c_str(), L"Overwrite Consist File", MB_YESNO | MB_ICONWARNING) != IDYES)
            {
                if (g_hEditFileName) SetWindowTextW(g_hEditFileName, oldFileName.c_str());
                return false;
            }
        }
    }

    wchar_t szCfgId[256] = { 0 };
    wchar_t szName[256] = { 0 };
    wchar_t szVelocity[256] = { 0 };
    wchar_t szPerf[256] = { 0 };

    if (g_hEditTrainCfgId)  GetWindowTextW(g_hEditTrainCfgId,  szCfgId,    256);
    if (g_hEditTrainName)   GetWindowTextW(g_hEditTrainName,   szName,     256);
    if (g_hEditMaxVelocity) GetWindowTextW(g_hEditMaxVelocity, szVelocity, 256);
    if (g_hEditPerfFactor)  GetWindowTextW(g_hEditPerfFactor,  szPerf,     256);

    double maxVelocityKmh = _wtof(szVelocity);
    double perfFactorPct  = _wtof(szPerf);

    // 1. Pause watcher
    g_bIgnoreWatcher = TRUE;
    SetTimer(hWnd, TIMER_IGNORE_WATCHER_RESET, 500, NULL);

    // 2. Save to destination
    bool success = ConsistWriter::SaveConsist(newFullPath, szCfgId, szName, maxVelocityKmh, perfFactorPct, g_LoadedConsistUnits);

    if (success)
    {
        if (isRenaming)
        {
            if (PathFileExistsW(oldFullPath.c_str()) && _wcsicmp(oldFullPath.c_str(), newFullPath.c_str()) != 0)
            {
                DeleteFileW(oldFullPath.c_str());
            }

            auto itOld = g_ConsistSessions.find(oldFileName);
            if (itOld != g_ConsistSessions.end())
            {
                auto sess = itOld->second;
                sess.fileName = targetFileName;
                sess.isDirty = false;
                g_ConsistSessions.erase(itOld);
                g_ConsistSessions[targetFileName] = sess;
            }

            g_szCurrentConsistFile = targetFileName;

            // Update row in consist list
            int sel = g_ConsistList.GetSelectedIndex();
            if (sel >= 0)
            {
                g_ConsistList.SetCellText(sel, 4, targetFileName);
            }
            else
            {
                for (int i = 0; i < g_ConsistList.GetItemCount(); ++i)
                {
                    if (_wcsicmp(g_ConsistList.GetCellText(i, 4).c_str(), oldFileName.c_str()) == 0)
                    {
                        g_ConsistList.SetCellText(i, 4, targetFileName);
                        break;
                    }
                }
            }

            if (g_hVisualConsistView)
            {
                VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath, szName, targetFileName);
            }
        }

        // 3. Update the left consist list row if it is selected
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            // Name
            std::wstring strName = szName;
            if (strName.empty()) strName = szCfgId;
            g_ConsistList.SetCellText(sel, 0, strName);

            // Units
            g_ConsistList.SetCellText(sel, 1, std::to_wstring(g_LoadedConsistUnits.size()));

            // Status
            std::wstring statusStr = EvaluateAndUpdateConsistStatus(g_szCurrentConsistFile, g_LoadedConsistUnits, true);
            g_ConsistList.SetCellText(sel, 2, statusStr);

            // Modified date
            SYSTEMTIME stLocal;
            GetLocalTime(&stLocal);
            wchar_t dateBuf[64] = { 0 };
            wchar_t timeBuf[64] = { 0 };
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, dateBuf, 64);
            GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &stLocal, NULL, timeBuf, 64);
            std::wstring friendlyTime = std::wstring(dateBuf) + L" " + timeBuf;
            g_ConsistList.SetCellText(sel, 3, friendlyTime);
            g_ConsistList.SetCellText(sel, 4, g_szCurrentConsistFile);

            for (auto& item : g_ScannedConsistsCache)
            {
                if (item.szFileName == g_szCurrentConsistFile)
                {
                    item.szName = strName;
                    item.nUnits = (int)g_LoadedConsistUnits.size();
                    item.szLastModified = friendlyTime;
                    item.isBroken = (statusStr == L"Missing Stock" || statusStr == L"Missing Shape" || statusStr == L"Broken");
                    item.hasMissingStock = (statusStr == L"Missing Stock");
                    item.hasMissingShape = (statusStr == L"Missing Shape");
                    break;
                }
            }
        }

        if (!g_szCurrentConsistFile.empty())
        {
            g_ConsistSessions[g_szCurrentConsistFile].isDirty = false;
            UpdateConsistManagerRow(g_szCurrentConsistFile);
        }
    }
    return success;
}

static bool SaveActivityConsistByIndex(HWND hWnd, int consistIndex, int listRowIndex = -1)
{
    if (g_CurrentActivityFilePath.empty() || consistIndex < 0 ||
        consistIndex >= (int)g_CurrentActivityData.consists.size())
    {
        return false;
    }

    auto& con = g_CurrentActivityData.consists[consistIndex];
    std::wstring consistKey = g_CurrentActivityFilePath + L"#" + con.id;

    if (consistIndex == g_CurrentActivityConsistIndex)
    {
        con.units = g_LoadedConsistUnits;
    }
    con.totalUnits = (int)con.units.size();

    auto res = ActivityConsistWriter::SaveActivityConsist(
        g_CurrentActivityFilePath,
        con.objectIndex,
        con.id,
        con.units,
        con.name
    );

    if (res.success)
    {
        con.isDirty = false;

        std::wstring statusStr = EvaluateAndUpdateConsistStatus(consistKey, con.units, true);
        con.isBroken = (statusStr == L"Missing Stock" || statusStr == L"Missing Shape" || statusStr == L"Broken");
        con.hasMissingStock = (statusStr == L"Missing Stock");
        con.hasMissingShape = (statusStr == L"Missing Shape");

        int targetRow = -1;
        if (listRowIndex >= 0 && listRowIndex < g_ConsistList.GetItemCount())
        {
            std::wstring rIdxStr = g_ConsistList.GetCellText(listRowIndex, 3);
            if (_wtoi(rIdxStr.c_str()) == consistIndex)
            {
                targetRow = listRowIndex;
            }
        }
        if (targetRow == -1)
        {
            for (int r = 0; r < g_ConsistList.GetItemCount(); ++r)
            {
                std::wstring rIdxStr = g_ConsistList.GetCellText(r, 3);
                if (_wtoi(rIdxStr.c_str()) == consistIndex)
                {
                    targetRow = r;
                    break;
                }
            }
        }

        if (targetRow >= 0 && targetRow < g_ConsistList.GetItemCount())
        {
            std::wstring curName = g_ConsistList.GetCellText(targetRow, 0);
            if (curName.rfind(L"● ", 0) == 0)
            {
                curName = curName.substr(2);
            }
            g_ConsistList.SetCellText(targetRow, 0, curName);
            g_ConsistList.SetCellText(targetRow, 1, std::to_wstring(con.totalUnits));
            g_ConsistList.SetCellText(targetRow, 2, statusStr);
            g_ConsistList.Invalidate();
        }

        // Update header broken count
        int brokenCount = 0;
        for (const auto& c : g_CurrentActivityData.consists)
        {
            if (c.isBroken) brokenCount++;
        }
        std::wstring actDispName = g_CurrentActivityData.fileName.empty() ? L"Activity" : g_CurrentActivityData.fileName;
        std::wstring headerText = L"  Activity Consists [ " + actDispName + L" \x2022 Consists: " +
            std::to_wstring(g_CurrentActivityData.consists.size()) + L" \x2022 Broken: " +
            std::to_wstring(brokenCount) + L" ]";
        SetWindowTextW(g_hConsistHeader, headerText.c_str());

        return true;
    }
    else
    {
        ShowModernMessageBox(hWnd, res.errorMessage.c_str(), L"Error Saving Activity Consist", MB_OK | MB_ICONERROR);
        return false;
    }
}

static bool SaveCurrentActivityConsistDiskOnly(HWND hWnd)
{
    int sel = g_ConsistList.GetSelectedIndex();
    return SaveActivityConsistByIndex(hWnd, g_CurrentActivityConsistIndex, sel);
}

static void SaveCurrentConsist(HWND hWnd)
{
    SaveCurrentConsistSessionState();

    if (g_bAutoSave)
    {
        if (g_ActiveTab == 1)
        {
            SaveCurrentActivityConsistDiskOnly(hWnd);
        }
        else
        {
            SaveCurrentConsistDiskOnly(hWnd);
        }
    }
}

static void LoadAndDisplayConsist(HWND hWnd, const std::wstring& filename)
{
    // 1. Save session state of current consist before switching
    SaveCurrentConsistSessionState();

    bool isExistingSession = (g_ConsistSessions.find(filename) != g_ConsistSessions.end());

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\')
        consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";
    std::wstring fullPath = consistFolder + filename;

    g_bIsLoadingConsist = true;
    try
    {
        ConsistReader::TrainConfig trainCfg;
        std::vector<ConsistReader::UnitInfo> units;
        std::vector<int> savedSel;
        int savedScroll = 0;

        if (isExistingSession)
        {
            const auto& sess = g_ConsistSessions[filename];
            trainCfg = sess.trainCfg;
            units = sess.units;
            savedSel = sess.selectedIndices;
            savedScroll = sess.scrollY;
            g_UndoStack = sess.undoStack;
            g_RedoStack = sess.redoStack;
        }
        else
        {
            ConsistReader::ConsistData data = ConsistReader::LoadConsist(fullPath);
            trainCfg = data.trainCfg;
            units = data.units;
            g_UndoStack.clear();
            g_RedoStack.clear();

            auto& sess = g_ConsistSessions[filename];
            sess.fileName = filename;
            sess.trainCfg = trainCfg;
            sess.units = units;
            sess.isDirty = false;
        }

        // Hide placeholder and show editor controls
        if (g_hEditorPane)       ShowWindow(g_hEditorPane,      SW_HIDE);
        if (g_hSectionTrainCfg)  ShowWindow(g_hSectionTrainCfg, SW_SHOW);
        if (g_hSectionUnits)     ShowWindow(g_hSectionUnits,     SW_SHOW);
        if (g_hLabelFileName)    ShowWindow(g_hLabelFileName,   SW_SHOW);
        if (g_hEditFileName)     ShowWindow(g_hEditFileName,    SW_SHOW);
        if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId, SW_SHOW);
        if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,  SW_SHOW);
        if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_HIDE);
        if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_HIDE);
        if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_HIDE);
        if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_HIDE);
        if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_HIDE);
        if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_HIDE);
        if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_HIDE);
        if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_HIDE);
        if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_HIDE);
        if (g_hUnitPreviewCard)  ShowWindow(g_hUnitPreviewCard,  SW_SHOW);

        if (g_ActiveTab == 1) // Activity Consists Tab: only Identifier is shown
        {
            if (g_hLabelFileName)    ShowWindow(g_hLabelFileName,    SW_HIDE);
            if (g_hEditFileName)     ShowWindow(g_hEditFileName,     SW_HIDE);
            if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_HIDE);
            if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_HIDE);
            if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_HIDE);
            if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_HIDE);
            if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_HIDE);
            if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_HIDE);
        }
        else // Main Consists Tab: show all fields
        {
            if (g_hLabelFileName)    ShowWindow(g_hLabelFileName,    SW_SHOW);
            if (g_hEditFileName)     ShowWindow(g_hEditFileName,     SW_SHOW);
            if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_SHOW);
            if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_SHOW);
            if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_SHOW);
            if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_SHOW);
            if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_SHOW);
            if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_SHOW);
        }

        if (g_hEditorUnitList)   ShowWindow(g_hEditorUnitList,  SW_SHOW);

        // Populate edit controls
        if (g_hEditFileName)   SetWindowTextW(g_hEditFileName,   filename.c_str());
        if (g_hEditTrainCfgId) SetWindowTextW(g_hEditTrainCfgId, trainCfg.trainCfgId.c_str());
        if (g_hEditTrainName)  SetWindowTextW(g_hEditTrainName,  trainCfg.name.c_str());

        auto FormatNumber = [](double v, wchar_t* buf, size_t bufCch) {
            double whole;
            if (std::modf(v, &whole) == 0.0)
                swprintf_s(buf, bufCch, L"%.0f", v);
            else
                swprintf_s(buf, bufCch, L"%.3f", v);
        };

        wchar_t szVelocity[128], szPerf[128];
        FormatNumber(trainCfg.maxVelocity, szVelocity, 128);
        FormatNumber(trainCfg.perfFactor,  szPerf,     128);
        SetWindowTextW(g_hEditMaxVelocity, szVelocity);
        SetWindowTextW(g_hEditPerfFactor,  szPerf);

        // Re-apply EM_SETRECT
        RECT rc;
        const int VPAD = 8;
        const int FIELD_H = 32;

        if (g_hEditFileName && GetClientRect(g_hEditFileName, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditFileName, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }
        if (g_hEditTrainCfgId && GetClientRect(g_hEditTrainCfgId, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditTrainCfgId, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }
        if (g_hEditTrainName && GetClientRect(g_hEditTrainName, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditTrainName, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }
        if (g_hEditMaxVelocity && GetClientRect(g_hEditMaxVelocity, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditMaxVelocity, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }
        if (g_hEditPerfFactor && GetClientRect(g_hEditPerfFactor, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditPerfFactor, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }

        g_LoadedConsistUnits = units;
        g_szCurrentConsistFile = filename;

        g_EditorUnitList.ClearAllFilters();
        RefreshEditorUnitList(false);

        if (isExistingSession && !savedSel.empty())
        {
            g_EditorUnitList.SetSelectedIndices(savedSel);
            g_EditorUnitList.SetScrollY(savedScroll);
        }
        else
        {
            g_EditorUnitList.ClearSelection();
            g_EditorUnitList.SetScrollY(0);
        }

        if (g_hVisualConsistView)
        {
            if (!VisualConsistView_IsFloating(g_hVisualConsistView))
            {
                ShowWindow(g_hVisualConsistView, SW_SHOW);
            }
            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath, trainCfg.name, filename);
        }
        UpdateConsistManagerRow(filename);
        LOG_INFO("Loaded consist '%ls' with %zu units.", filename.c_str(), g_LoadedConsistUnits.size());

        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        g_bIsLoadingConsist = false;
        SyncPoolMutatorSelectionIfOpen();
    }
    catch (const std::exception& e)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, NULL, 0);
        if (len > 0)
        {
            std::wstring wmsg(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wmsg[0], len);
            ShowModernMessageBox(hWnd, wmsg.c_str(), L"Error Reading Consist", MB_OK | MB_ICONERROR);
        }
    }
}

static void LoadAndDisplayActivityConsist(HWND hWnd, int consistIndex)
{
    SaveCurrentConsistSessionState();

    if (consistIndex < 0 || consistIndex >= (int)g_CurrentActivityData.consists.size())
        return;

    g_CurrentActivityConsistIndex = consistIndex;
    const auto& actCon = g_CurrentActivityData.consists[consistIndex];

    g_bIsLoadingConsist = true;
    try
    {
        // Hide placeholder and show editor controls
        if (g_hEditorPane)       ShowWindow(g_hEditorPane,      SW_HIDE);
        if (g_hSectionTrainCfg)  ShowWindow(g_hSectionTrainCfg, SW_SHOW);
        if (g_hSectionUnits)     ShowWindow(g_hSectionUnits,     SW_SHOW);
        if (g_hLabelFileName)    ShowWindow(g_hLabelFileName,   SW_HIDE);
        if (g_hEditFileName)     ShowWindow(g_hEditFileName,    SW_HIDE);
        if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId, SW_SHOW);
        if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,  SW_SHOW);
        if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_HIDE);
        if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_HIDE);
        if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_HIDE);
        if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_HIDE);
        if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_HIDE);
        if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_HIDE);
        if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_HIDE);
        if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_HIDE);
        if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_HIDE);
        if (g_hUnitPreviewCard)  ShowWindow(g_hUnitPreviewCard,  SW_SHOW);

        // Hide Train Name / Speed / Perf Factor in Activity mode
        if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_HIDE);
        if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_HIDE);
        if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_HIDE);
        if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_HIDE);
        if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_HIDE);
        if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_HIDE);

        if (g_hEditorUnitList)   ShowWindow(g_hEditorUnitList,  SW_SHOW);

        // Populate Consist Identifier (read-only)
        std::wstring idDisplay = actCon.id;
        if (idDisplay.empty()) idDisplay = actCon.name;
        SetWindowTextW(g_hEditTrainCfgId, idDisplay.c_str());
        SendMessage(g_hEditTrainCfgId, EM_SETREADONLY, TRUE, 0);

        // Re-apply EM_SETRECT
        RECT rc;
        const int VPAD = 8;
        const int FIELD_H = 32;
        if (g_hEditTrainCfgId && GetClientRect(g_hEditTrainCfgId, &rc)) {
            RECT rcFmt = { 6, VPAD, rc.right - 6, FIELD_H - VPAD };
            SendMessage(g_hEditTrainCfgId, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }

        g_LoadedConsistUnits = actCon.units;
        g_szCurrentConsistFile = L"";

        g_UndoStack.clear();
        g_RedoStack.clear();

        g_EditorUnitList.ClearAllFilters();
        RefreshEditorUnitList(false);
        g_EditorUnitList.ClearSelection();
        g_EditorUnitList.SetScrollY(0);

        if (g_hVisualConsistView)
        {
            if (!VisualConsistView_IsFloating(g_hVisualConsistView))
            {
                ShowWindow(g_hVisualConsistView, SW_SHOW);
            }
            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath, actCon.name, actCon.id);
        }

        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        g_bIsLoadingConsist = false;
        SyncPoolMutatorSelectionIfOpen();
    }
    catch (const std::exception& e)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, NULL, 0);
        if (len > 0)
        {
            std::wstring wmsg(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wmsg[0], len);
            ShowModernMessageBox(hWnd, wmsg.c_str(), L"Error Reading Activity Consist", MB_OK | MB_ICONERROR);
        }
    }
}


static void ActionCreateNewConsist(HWND hWnd)
{
    if (g_szBasePath.empty())
    {
        ShowModernMessageBox(hWnd, L"Please select a Train Simulator root directory first.", L"Create New Consist", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\') consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";

    // Find unique filename
    std::wstring baseFileName = L"New_Consist";
    std::wstring candidateFile = baseFileName + L".con";
    int counter = 1;
    while (GetFileAttributesW((consistFolder + candidateFile).c_str()) != INVALID_FILE_ATTRIBUTES ||
           g_ConsistSessions.find(candidateFile) != g_ConsistSessions.end())
    {
        candidateFile = baseFileName + L"_" + std::to_wstring(counter++) + L".con";
    }

    std::wstring cfgId = candidateFile.substr(0, candidateFile.length() - 4);
    std::wstring dispName = L"New Consist";
    if (counter > 1) dispName += L" " + std::to_wstring(counter - 1);

    // Create session in memory
    auto& session = g_ConsistSessions[candidateFile];
    session.fileName = candidateFile;
    session.trainCfg.trainCfgId = cfgId;
    session.trainCfg.name = dispName;
    session.trainCfg.maxVelocity = 120.0;
    session.trainCfg.perfFactor = 1.0;
    session.units.clear();
    session.isDirty = true;
    session.undoStack.clear();
    session.redoStack.clear();
    session.selectedIndices.clear();
    session.scrollY = 0;

    // Create physical empty consist file on disk
    g_bIgnoreWatcher = TRUE;
    SetTimer(hWnd, TIMER_IGNORE_WATCHER_RESET, 500, NULL);
    ConsistWriter::SaveConsist(consistFolder + candidateFile, cfgId, dispName, 120.0, 1.0, {});

    // Add to Consists list
    SYSTEMTIME stLocal;
    GetLocalTime(&stLocal);
    wchar_t dateBuf[64] = { 0 }, timeBuf[64] = { 0 };
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, dateBuf, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &stLocal, NULL, timeBuf, 64);
    std::wstring friendlyTime = std::wstring(dateBuf) + L" " + timeBuf;

    g_ConsistList.AddItem({ L"● " + dispName, L"0", L"Healthy", friendlyTime, candidateFile });

    // Select and load the new consist
    for (int i = 0; i < g_ConsistList.GetItemCount(); ++i)
    {
        if (_wcsicmp(g_ConsistList.GetCellText(i, 4).c_str(), candidateFile.c_str()) == 0)
        {
            g_ConsistList.SetSelectedIndex(i);
            break;
        }
    }

    LoadAndDisplayConsist(hWnd, candidateFile);
    LOG_INFO("Created new consist '%ls'", candidateFile.c_str());

    if (g_hEditFileName)
    {
        SetFocus(g_hEditFileName);
        SendMessage(g_hEditFileName, EM_SETSEL, 0, -1);
    }
    else if (g_hEditTrainName)
    {
        SetFocus(g_hEditTrainName);
        SendMessage(g_hEditTrainName, EM_SETSEL, 0, -1);
    }
}

static void ActionCloneConsist(HWND hWnd)
{
    std::wstring srcFile = g_szCurrentConsistFile;
    if (srcFile.empty())
    {
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            srcFile = g_ConsistList.GetCellText(sel, 4);
        }
    }

    if (srcFile.empty())
    {
        ShowModernMessageBox(hWnd, L"Please select or open a consist first to clone it.", L"Clone Consist", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\') consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";

    // Save current active state if cloning active consist
    if (srcFile == g_szCurrentConsistFile)
    {
        SaveCurrentConsistSessionState();
    }

    // Retrieve source data
    ConsistReader::TrainConfig srcCfg;
    std::vector<ConsistReader::UnitInfo> srcUnits;

    auto it = g_ConsistSessions.find(srcFile);
    if (it != g_ConsistSessions.end())
    {
        srcCfg = it->second.trainCfg;
        srcUnits = it->second.units;
    }
    else
    {
        try {
            auto data = ConsistReader::LoadConsist(consistFolder + srcFile);
            srcCfg = data.trainCfg;
            srcUnits = data.units;
        } catch (...) {
            ShowModernMessageBox(hWnd, L"Unable to read source consist for cloning.", L"Clone Error", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // Determine clone filename following Windows Explorer naming: "<Base> - Copy.con", "<Base> - Copy (2).con"
    std::wstring baseName = srcFile;
    if (baseName.size() > 4 && _wcsicmp(baseName.substr(baseName.size() - 4).c_str(), L".con") == 0)
    {
        baseName = baseName.substr(0, baseName.size() - 4);
    }

    std::wstring cloneFile = baseName + L" - Copy.con";
    std::wstring cloneDispName = srcCfg.name.empty() ? (baseName + L" - Copy") : (srcCfg.name + L" - Copy");
    int copyIndex = 2;

    while (GetFileAttributesW((consistFolder + cloneFile).c_str()) != INVALID_FILE_ATTRIBUTES ||
           g_ConsistSessions.find(cloneFile) != g_ConsistSessions.end())
    {
        cloneFile = baseName + L" - Copy (" + std::to_wstring(copyIndex) + L").con";
        cloneDispName = (srcCfg.name.empty() ? baseName : srcCfg.name) + L" - Copy (" + std::to_wstring(copyIndex) + L")";
        copyIndex++;
    }

    std::wstring cloneCfgId = cloneFile.substr(0, cloneFile.length() - 4);

    // Save physically to disk
    g_bIgnoreWatcher = TRUE;
    SetTimer(hWnd, TIMER_IGNORE_WATCHER_RESET, 500, NULL);
    bool saved = ConsistWriter::SaveConsist(consistFolder + cloneFile, cloneCfgId, cloneDispName, srcCfg.maxVelocity, srcCfg.perfFactor, srcUnits);
    if (!saved)
    {
        ShowModernMessageBox(hWnd, L"Failed to write cloned consist to disk.", L"Clone Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Add to session
    auto& cloneSess = g_ConsistSessions[cloneFile];
    cloneSess.fileName = cloneFile;
    cloneSess.trainCfg.trainCfgId = cloneCfgId;
    cloneSess.trainCfg.name = cloneDispName;
    cloneSess.trainCfg.maxVelocity = srcCfg.maxVelocity;
    cloneSess.trainCfg.perfFactor = srcCfg.perfFactor;
    cloneSess.units = srcUnits;
    cloneSess.isDirty = false;

    // Add to list table
    SYSTEMTIME stLocal;
    GetLocalTime(&stLocal);
    wchar_t dateBuf[64] = { 0 }, timeBuf[64] = { 0 };
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &stLocal, NULL, dateBuf, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &stLocal, NULL, timeBuf, 64);
    std::wstring friendlyTime = std::wstring(dateBuf) + L" " + timeBuf;

    g_ConsistList.AddItem({ cloneDispName, std::to_wstring(srcUnits.size()), L"Healthy", friendlyTime, cloneFile });

    // Select and load
    for (int i = 0; i < g_ConsistList.GetItemCount(); ++i)
    {
        if (_wcsicmp(g_ConsistList.GetCellText(i, 4).c_str(), cloneFile.c_str()) == 0)
        {
            g_ConsistList.SetSelectedIndex(i);
            break;
        }
    }

    LoadAndDisplayConsist(hWnd, cloneFile);
    LOG_INFO("Cloned consist '%ls' -> '%ls'", srcFile.c_str(), cloneFile.c_str());
}

static void ActionDeleteSelectedConsists(HWND hWnd)
{
    std::vector<int> selIndices = g_ConsistList.GetSelectedIndices();
    if (selIndices.empty())
    {
        int single = g_ConsistList.GetSelectedIndex();
        if (single >= 0) selIndices.push_back(single);
    }

    std::vector<std::wstring> filesToDelete;
    std::vector<std::wstring> namesToDelete;

    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < g_ConsistList.GetItemCount())
        {
            std::wstring fName = g_ConsistList.GetCellText(idx, 4);
            std::wstring dName = g_ConsistList.GetCellText(idx, 0);
            if (!fName.empty())
            {
                filesToDelete.push_back(fName);
                namesToDelete.push_back(dName);
            }
        }
    }

    if (filesToDelete.empty() && !g_szCurrentConsistFile.empty())
    {
        filesToDelete.push_back(g_szCurrentConsistFile);
        namesToDelete.push_back(g_szCurrentConsistFile);
    }

    if (filesToDelete.empty())
    {
        ShowModernMessageBox(hWnd, L"Please select one or more consist files to delete.", L"Delete Consist(s)", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Build confirmation message
    std::wstring prompt = L"Are you sure you want to permanently delete ";
    if (filesToDelete.size() == 1)
    {
        prompt += L"this consist file:\r\n\r\n  • " + filesToDelete[0];
    }
    else
    {
        prompt += L"these " + std::to_wstring(filesToDelete.size()) + L" consist files from disk?\r\n────────────────────────────────────────────────────────\r\n";
        for (size_t i = 0; i < filesToDelete.size() && i < 8; ++i)
        {
            prompt += L"  • " + filesToDelete[i] + L"\r\n";
        }
        if (filesToDelete.size() > 8)
        {
            prompt += L"  • ... and " + std::to_wstring(filesToDelete.size() - 8) + L" more files\r\n";
        }
    }
    prompt += L"\r\n\r\nThis physical file deletion cannot be undone.";

    int res = ShowModernMessageBox(hWnd, prompt.c_str(), L"Confirm Permanent Deletion", MB_YESNO | MB_ICONWARNING);
    if (res != IDYES) return;

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\') consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";

    g_bIgnoreWatcher = TRUE;
    SetTimer(hWnd, TIMER_IGNORE_WATCHER_RESET, 1000, NULL);

    bool activeDeleted = false;
    for (const auto& fname : filesToDelete)
    {
        std::wstring fullPath = consistFolder + fname;
        DeleteFileW(fullPath.c_str());
        g_ConsistSessions.erase(fname);
        if (_wcsicmp(fname.c_str(), g_szCurrentConsistFile.c_str()) == 0)
        {
            activeDeleted = true;
        }
    }
    LOG_INFO("Deleted %zu consist file(s) from disk.", filesToDelete.size());

    if (activeDeleted)
    {
        g_szCurrentConsistFile = L"";
        g_LoadedConsistUnits.clear();
        if (g_hEditorPane)       ShowWindow(g_hEditorPane,       SW_SHOW);
        if (g_hSectionTrainCfg)  ShowWindow(g_hSectionTrainCfg,  SW_HIDE);
        if (g_hSectionUnits)     ShowWindow(g_hSectionUnits,     SW_HIDE);
        if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId,  SW_HIDE);
        if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,   SW_HIDE);
        if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_HIDE);
        if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_HIDE);
        if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_HIDE);
        if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_HIDE);
        if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_HIDE);
        if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_HIDE);
        if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_HIDE);
        if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_HIDE);
        if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_HIDE);
        if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_HIDE);
        if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_HIDE);
        if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_HIDE);
        if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_HIDE);
        if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_HIDE);
        if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_HIDE);
        if (g_hEditorUnitList)   ShowWindow(g_hEditorUnitList,   SW_HIDE);
        if (g_hUnitPreviewCard)
        {
            ShowWindow(g_hUnitPreviewCard, SW_HIDE);
            UnitPreviewCard_Clear(g_hUnitPreviewCard);
        }
        if (g_hVisualConsistView) VisualConsistView_SetUnits(g_hVisualConsistView, {}, g_szBasePath);
        InvalidateRect(hWnd, NULL, TRUE);
    }

    TriggerConsistsRescan(hWnd);
}

static void ActionReverseConsist(HWND hWnd)
{
    if (g_LoadedConsistUnits.empty() || g_szCurrentConsistFile.empty())
    {
        ShowModernMessageBox(hWnd, L"No consist is currently loaded in the editor to reverse.", L"Reverse Consist", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (g_LoadedConsistUnits.size() < 2)
    {
        ShowModernMessageBox(hWnd, L"Consist must contain at least 2 units to reverse sequence.", L"Reverse Consist", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 1. Push undo step (also marks session dirty & updates consist table row)
    PushUndoState(L"Reverse Consist");

    // 2. Pure sequence inversion (A - B - C -> C - B - A) without touching individual unit flip orientations
    std::reverse(g_LoadedConsistUnits.begin(), g_LoadedConsistUnits.end());

    // 3. Update active consist session units
    if (!g_szCurrentConsistFile.empty())
    {
        auto it = g_ConsistSessions.find(g_szCurrentConsistFile);
        if (it != g_ConsistSessions.end())
        {
            it->second.units = g_LoadedConsistUnits;
            it->second.isDirty = true;
        }
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    LOG_INFO("Reversed sequence of consist '%ls' (%zu units).", g_szCurrentConsistFile.c_str(), g_LoadedConsistUnits.size());

    // 4. Refresh Editor Unit table and Visual Consist Track View
    RefreshEditorUnitList(false);
}

static bool PromptUserForTrainSimDirectory(HWND hWndParent, std::wstring& outSelectedPath)
{
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr))
    {
        DWORD dwOptions = 0;
        pFileOpen->GetOptions(&dwOptions);
        pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        pFileOpen->SetTitle(L"Select Train Simulator Installation Directory");

        hr = pFileOpen->Show(hWndParent);
        if (SUCCEEDED(hr))
        {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr))
            {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr))
                {
                    outSelectedPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                    pItem->Release();
                    pFileOpen->Release();
                    return true;
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return false;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (hMonitor)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(hMonitor, &mi))
            {
                pMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                pMMI->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                pMMI->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                pMMI->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
        }
        return 0;
    }

    case WM_NCCALCSIZE:
    {
        if (wParam)
        {
            NCCALCSIZE_PARAMS* pParams = (NCCALCSIZE_PARAMS*)lParam;
            if (IsZoomed(hWnd))
            {
                HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                if (hMonitor)
                {
                    MONITORINFO mi = { sizeof(mi) };
                    if (GetMonitorInfoW(hMonitor, &mi))
                    {
                        pParams->rgrc[0] = mi.rcWork;
                    }
                }
            }
            return 0;
        }
        break;
    }

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Border resizing when not maximized (6px border margin)
        if (!IsZoomed(hWnd))
        {
            int b = 6;
            if (pt.y < b && pt.x < b) return HTTOPLEFT;
            if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
            if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
            if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
            if (pt.y < b) return HTTOP;
            if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
            if (pt.x < b) return HTLEFT;
            if (pt.x >= rcClient.right - b) return HTRIGHT;
        }

        // Top custom title bar area (0 to 40px)
        if (pt.y >= 0 && pt.y < 66)
        {
            if (g_hCustomTitleBar && IsWindow(g_hCustomTitleBar))
            {
                POINT ptBar = pt;
                LRESULT hit = SendMessageW(g_hCustomTitleBar, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                if (hit == HTTRANSPARENT)
                {
                    return HTCAPTION;
                }
            }
        }
        break;
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        SwitchActiveConsistTab(hWnd, (int)wParam);
        return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    {
        return 0;
    }
    case WM_CREATE:
    {
        InitializeCriticalSection(&g_StockCacheCS);
        PoolManager::InitializePoolPresets();
        PoolManager::InitializeReplacementGroups();
        TrainConfigManager::EnsureDefaultTrainConfigs();
        Updater::CleanupOldUpdateFiles();
        Updater::CheckForUpdates(hWnd, true);
        BOOL bLoaded = FALSE;
        hUIFont = GetAdaptiveSystemFont();
        // 1. Create Universal Custom Title Bar (0 to 40px)
        g_hCustomTitleBar = CreateCustomTitleBar(hWnd, hInst, 0, 0, 1100, 66, 10001);
        if (g_hCustomTitleBar)
        {
            CustomTitleBar_SetDarkMode(g_hCustomTitleBar, g_bDarkMode);
            CustomTitleBar_SetActiveTab(g_hCustomTitleBar, g_ActiveTab);
        }

        // 2. Create Navigation Toolbar seamlessly below Custom Title Bar (40 to 84px)
        g_hNavToolbar = CreateNavToolbar(hWnd, hInst, 0, 66, 1100, 44, IDC_NAVTOOLBAR);
        if (g_hNavToolbar)
        {
            NavToolbar_SetDarkMode(g_hNavToolbar, g_bDarkMode);
            
            wchar_t szSavedPath[MAX_PATH] = { 0 };
            std::wstring savedPath = DatabaseManager::GetSetting(L"LastDirectory", L"");
            if (!savedPath.empty())
            {
                wcsncpy_s(szSavedPath, savedPath.c_str(), _TRUNCATE);
                bLoaded = TRUE;
            }

            if (bLoaded)
            {
                NavToolbar_SetPath(g_hNavToolbar, szSavedPath);
            }
            else
            {
                NavToolbar_SetPath(g_hNavToolbar, L"");
            }
        }

        // 3. Create Windows 11 Explorer Command Bar seamlessly below Navigation Toolbar (80 to 120px)
        g_hCommandBar = CreateCommandBar(hWnd, hInst, 0, 110, 1100, 40, IDC_COMMANDBAR);
        if (g_hCommandBar)
        {
            CommandBar_SetDarkMode(g_hCommandBar, g_bDarkMode);
        }

        // Ensure treeview and listview classes are registered
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icex);

        // 4. Create Top Deck: Consist Header (static control)
        g_hConsistHeader = CreateWindowEx(
            0, L"STATIC", L"  Consists Manager",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFT | SS_NOTIFY,
            0, 150, 600, 28,
            hWnd, (HMENU)IDC_CONSISTHEADER, hInst, NULL
        );
        if (g_hConsistHeader)
        {
            SetWindowSubclass(g_hConsistHeader, ConsistHeaderSubclassProc, 4, 0);
        }

        // 5. Create Top Deck: Consist ListView (Custom Control)
        g_hConsistList = g_ConsistList.Create(hWnd, 0, 148, 600, 200, IDC_CONSISTLIST);
        if (g_hConsistList)
        {
            g_ConsistList.SetMultiSelect(true);
            g_ConsistList.SetAllowMarquee(true);
            g_ConsistList.SetShowSelectionGutter(true);
            g_ConsistList.AddColumn(L"Name", 240, 0);
            g_ConsistList.AddColumn(L"Units", 80, 0);
            g_ConsistList.AddColumn(L"Status", 100, 0);
            g_ConsistList.AddColumn(L"Modified", 140, 0);
        }

        // 5b. Create Top Deck: Route/Activities TreeView (for Activity Consists Tab)
        g_hRouteTree = g_RouteTreeView.Create(hWnd, 0, 148, g_wCategorySplit, 200, IDC_ROUTETREE);
        if (g_hRouteTree)
        {
            g_RouteTreeView.SetDarkMode(g_bDarkMode);
            g_RouteTreeView.SetSelectionCallback([hWnd](CustomTreeNode* pNode) {
                OnRouteActivitySelected(hWnd, pNode);
            });
        }

        // 6. Create Bottom Deck: Stock Header (static control)
        g_hStockHeader = CreateWindowEx(
            0, L"STATIC", L"  Stock Library",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFT | SS_NOTIFY,
            0, 360, 600, 28,
            hWnd, (HMENU)IDC_STOCKHEADER, hInst, NULL
        );
        if (g_hStockHeader)
        {
            SetWindowSubclass(g_hStockHeader, StockHeaderSubclassProc, 5, 0);
        }

        // 7. Create Bottom Deck: Stock Category TreeView
        g_hCategoryTree = g_CategoryTreeView.Create(hWnd, 0, 388, g_wCategorySplit, 300, IDC_STOCKTREE);
        if (g_hCategoryTree)
        {
            g_CategoryTreeView.SetDarkMode(g_bDarkMode);
            g_CategoryTreeView.SetSelectionCallback([](CustomTreeNode* pNode) {
                PopulateAssetGrid(pNode);
            });
        }

        // 8. Create Bottom Deck: Asset ListView (Pane 0)
        g_hAssetList = g_AssetList.Create(hWnd, g_wCategorySplit + 8, 388, 350, 300, IDC_ASSETLIST);
        if (g_hAssetList)
        {
            g_AssetList.SetMultiSelect(true);
            g_AssetList.SetAllowRearrange(false);
            g_AssetList.SetAllowTransferSource(true);
            g_AssetList.SetAllowMarquee(true);
            g_AssetList.SetShowSelectionGutter(true);
            g_AssetList.SetVirtualMode(AssetListGetCellText, (void*)(intptr_t)0);
            g_AssetList.AddColumn(L"Name", 200, 0);
            g_AssetList.AddColumn(L"Type", 80, 0);
            g_AssetList.AddColumn(L"Folder", 140, 0);
        }

        // 8b. Create Bottom Deck: Asset ListView (Pane 1 - Dual/Triple)
        g_hAssetList2 = g_AssetList2.Create(hWnd, 0, 0, 0, 0, IDC_ASSETLIST2);
        if (g_hAssetList2)
        {
            g_AssetList2.SetMultiSelect(true);
            g_AssetList2.SetAllowRearrange(false);
            g_AssetList2.SetAllowTransferSource(true);
            g_AssetList2.SetAllowMarquee(true);
            g_AssetList2.SetShowSelectionGutter(true);
            g_AssetList2.SetVirtualMode(AssetListGetCellText, (void*)(intptr_t)1);
            g_AssetList2.AddColumn(L"Name", 200, 0);
            g_AssetList2.AddColumn(L"Type", 80, 0);
            g_AssetList2.AddColumn(L"Folder", 140, 0);
            ShowWindow(g_hAssetList2, SW_HIDE);
        }

        // 8c. Create Bottom Deck: Asset ListView (Pane 2 - Triple)
        g_hAssetList3 = g_AssetList3.Create(hWnd, 0, 0, 0, 0, IDC_ASSETLIST3);
        if (g_hAssetList3)
        {
            g_AssetList3.SetMultiSelect(true);
            g_AssetList3.SetAllowRearrange(false);
            g_AssetList3.SetAllowTransferSource(true);
            g_AssetList3.SetAllowMarquee(true);
            g_AssetList3.SetShowSelectionGutter(true);
            g_AssetList3.SetVirtualMode(AssetListGetCellText, (void*)(intptr_t)2);
            g_AssetList3.AddColumn(L"Name", 200, 0);
            g_AssetList3.AddColumn(L"Type", 80, 0);
            g_AssetList3.AddColumn(L"Folder", 140, 0);
            ShowWindow(g_hAssetList3, SW_HIDE);
        }

        // Create Pane Category Buttons and Dedicated Search Edit Controls for Panes 0, 1, 2
        for (int k = 0; k < 3; ++k)
        {
            g_hPaneCatBtn[k] = CreateWindowExW(
                0, L"STATIC", GetCategoryFilterLabel(g_PaneCategory[k]),
                WS_CHILD | SS_NOTIFY,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)(IDC_PANE_CAT_BTN_0 + k), hInst, NULL
            );
            if (g_hPaneCatBtn[k])
            {
                SetWindowSubclass(g_hPaneCatBtn[k], PaneCatBtnSubclassProc, 30 + k, (DWORD_PTR)k);
                ShowWindow(g_hPaneCatBtn[k], SW_HIDE);
            }

            g_hPaneSearchEdit[k] = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)(IDC_PANE_SEARCH_0 + k), hInst, NULL
            );
            if (g_hPaneSearchEdit[k])
            {
                SetWindowSubclass(g_hPaneSearchEdit[k], ModernEditSubclassProc, 40 + k, 0);
                SendMessage(g_hPaneSearchEdit[k], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 26));
                SendMessageW(g_hPaneSearchEdit[k], EM_SETCUEBANNER, TRUE, (LPARAM)L"Search...");
                ShowWindow(g_hPaneSearchEdit[k], SW_HIDE);
            }
        }

        // Populate tree
        PopulateCategoryTree();
        UpdateLibraryTheme(g_bDarkMode);

        // 9. Create Right Pane: Consist Editor Workspace Header (static control)
        g_hWorkspaceHeader = CreateWindowEx(
            0, L"STATIC", L"  Consist Editor Workspace",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFT | SS_NOTIFY,
            600, 150, 500, 28,
            hWnd, (HMENU)IDC_WORKSPACEHEADER, hInst, NULL
        );
        if (g_hWorkspaceHeader)
        {
            SetWindowSubclass(g_hWorkspaceHeader, WorkspaceHeaderSubclassProc, 7, 0);
        }

        // 9b. Create Right Pane: Consist Editor Workspace Background / Placeholder
        g_hEditorPane = CreateWindowEx(
            0, L"STATIC", L"Consist Editor Workspace",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY,
            600, 178, 500, 600,
            hWnd, (HMENU)IDC_EDITORPANE, hInst, NULL
        );
        if (g_hEditorPane)
        {
            SetWindowSubclass(g_hEditorPane, EditorPaneSubclassProc, 6, 0);
        }

        // ---------------------------------------------------------------
        // Create Editor Pane Controls – modern Windows 11 style
        // All created hidden (no WS_VISIBLE); shown on consist load.
        // ---------------------------------------------------------------

        // "TRAIN DETAILS" — centered section header
        g_hSectionTrainCfg = CreateWindowEx(
            0, L"STATIC", L"TRAIN DETAILS",
            WS_CHILD | SS_CENTER | SS_NOPREFIX | SS_NOTIFY,            // center-aligned
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_SECTION_CFG, hInst, NULL);

        // Caption labels (small, muted text – vertically centered and left-aligned)
        g_hLabelFileName    = CreateWindowEx(0, L"STATIC", L"File Name (.con)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_FILENAME, hInst, NULL);
        g_hLabelTrainCfgId  = CreateWindowEx(0, L"STATIC", L"Consist Identifier",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelTrainName   = CreateWindowEx(0, L"STATIC", L"Train Name",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelMaxVelocity = CreateWindowEx(0, L"STATIC", L"Speed Limit (km/h)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelPerfFactor  = CreateWindowEx(0, L"STATIC", L"Performance Factor (%)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);

        // Flat edit boxes — ES_MULTILINE allows EM_SETRECT for vertical centering.
        // ES_CENTER centers text horizontally.
        // ES_AUTOHSCROLL keeps single-line scroll behaviour.
        // No WS_EX_CLIENTEDGE — border is drawn by ModernEditSubclassProc.
        const DWORD dwEditStyle = WS_CHILD | ES_CENTER | ES_MULTILINE | ES_AUTOHSCROLL;

        g_hEditFileName = CreateWindowEx(
            0, L"EDIT", L"", dwEditStyle,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_FILENAME, hInst, NULL);
        g_hEditTrainCfgId = CreateWindowEx(
            0, L"EDIT", L"", dwEditStyle,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_TRAINCFGID, hInst, NULL);
        g_hEditTrainName = CreateWindowEx(
            0, L"EDIT", L"", dwEditStyle,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_TRAINNAME, hInst, NULL);
        g_hEditMaxVelocity = CreateWindowEx(
            0, L"EDIT", L"", dwEditStyle,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_MAXVELOCITY, hInst, NULL);
        g_hEditPerfFactor = CreateWindowEx(
            0, L"EDIT", L"", dwEditStyle,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_PERFFACTOR, hInst, NULL);

        // Right Card: "TRAIN SUMMARY & METRICS" section header
        g_hSectionMetrics = CreateWindowEx(
            0, L"STATIC", L"TRAIN SUMMARY & METRICS",
            WS_CHILD | SS_CENTER | SS_NOPREFIX,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_SECTION_METRICS, hInst, NULL);

        // Metrics caption labels
        g_hLabelMetricMass   = CreateWindowEx(0, L"STATIC", L"Total Train Mass", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelMetricLength = CreateWindowEx(0, L"STATIC", L"Total Train Length", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelMetricPower  = CreateWindowEx(0, L"STATIC", L"Total Power", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelMetricRatio  = CreateWindowEx(0, L"STATIC", L"Composition", WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);

        // Metrics read-only edit boxes
        g_hEditMetricMass   = CreateWindowEx(0, L"EDIT", L"", dwEditStyle | ES_READONLY, 0, 0, 0, 0, hWnd, (HMENU)IDC_ED_METRIC_MASS, hInst, NULL);
        g_hEditMetricLength = CreateWindowEx(0, L"EDIT", L"", dwEditStyle | ES_READONLY, 0, 0, 0, 0, hWnd, (HMENU)IDC_ED_METRIC_LENGTH, hInst, NULL);
        g_hEditMetricPower  = CreateWindowEx(0, L"EDIT", L"", dwEditStyle | ES_READONLY, 0, 0, 0, 0, hWnd, (HMENU)IDC_ED_METRIC_POWER, hInst, NULL);
        g_hEditMetricRatio  = CreateWindowEx(0, L"EDIT", L"", dwEditStyle | ES_READONLY, 0, 0, 0, 0, hWnd, (HMENU)IDC_ED_METRIC_RATIO, hInst, NULL);

        // Apply modern-style subclass to each edit box
        if (g_hEditFileName)    SetWindowSubclass(g_hEditFileName,    ModernEditSubclassProc, 18, 0);
        if (g_hEditTrainCfgId)  SetWindowSubclass(g_hEditTrainCfgId,  ModernEditSubclassProc, 9,  0);
        if (g_hEditTrainName)   SetWindowSubclass(g_hEditTrainName,   ModernEditSubclassProc, 10, 0);
        if (g_hEditMaxVelocity) SetWindowSubclass(g_hEditMaxVelocity, ModernEditSubclassProc, 11, 0);
        if (g_hEditPerfFactor)  SetWindowSubclass(g_hEditPerfFactor,  ModernEditSubclassProc, 12, 0);
        if (g_hEditMetricMass)   SetWindowSubclass(g_hEditMetricMass,   ModernEditSubclassProc, 13, 0);
        if (g_hEditMetricLength) SetWindowSubclass(g_hEditMetricLength, ModernEditSubclassProc, 14, 0);
        if (g_hEditMetricPower)  SetWindowSubclass(g_hEditMetricPower,  ModernEditSubclassProc, 15, 0);
        if (g_hEditMetricRatio)  SetWindowSubclass(g_hEditMetricRatio,  ModernEditSubclassProc, 16, 0);

        // "Consist Units" section header
        g_hSectionUnits = CreateWindowEx(
            0, L"STATIC", L"Consist Units",
            WS_CHILD | SS_LEFT | SS_NOTIFY,
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_SECTION_UNITS, hInst, NULL);
        if (g_hSectionUnits)
            SetWindowSubclass(g_hSectionUnits, SectionUnitsHeaderSubclassProc, 17, 0);

        // Unit list table
        g_hEditorUnitList = g_EditorUnitList.Create(hWnd, 0, 0, 0, 0, IDC_ED_UNITLIST);
        if (g_hEditorUnitList)
        {
            g_EditorUnitList.SetMultiSelect(true);
            g_EditorUnitList.SetAllowRearrange(true);
            g_EditorUnitList.SetAllowMarquee(true);
            g_EditorUnitList.SetDrawCardBorder(true);
            g_EditorUnitList.SetShowSelectionGutter(true);
            g_EditorUnitList.AddColumn(L"No.",              48,  1);
            g_EditorUnitList.AddColumn(L"Name",             200, 0);
            g_EditorUnitList.AddColumn(L"Type",             80,  0);
            g_EditorUnitList.AddColumn(L"Status",           90,  0);
            g_EditorUnitList.AddColumn(L"Orientation",      90,  0);
            g_EditorUnitList.AddColumn(L"Parent Directory", 200, 0);
            g_EditorUnitList.SetInlineCopyColumn(1);
            ShowWindow(g_hEditorUnitList, SW_HIDE);
        }

        // Visual Consist Track Preview (docked by default in dedicated bottom workspace partition)
        g_hVisualConsistView = CreateVisualConsistView(hWnd, hInst, 0, 0, 0, 0, 8888);
        if (g_hVisualConsistView)
        {
            ShowWindow(g_hVisualConsistView, SW_SHOW);
        }

        // Live 3D Rolling Stock Preview Card (docked in Consist Editor alongside Unit List)
        g_hUnitPreviewCard = CreateUnitPreviewCard(hWnd, hInst, 0, 0, 0, 0, 8999);
        if (g_hUnitPreviewCard)
        {
            ShowWindow(g_hUnitPreviewCard, SW_SHOW);
        }

        // Create Dedicated Splitter Windows
        g_hSplitter1 = CreateWindowExW(
            0, L"TSCBSplitter", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            g_wConsist, 150, 9, 700,
            hWnd, NULL, hInst, NULL
        );
        if (g_hSplitter1) SetWindowLongPtrW(g_hSplitter1, GWLP_USERDATA, (LONG_PTR)SPLITTER_VERTICAL_MAIN);

        g_hSplitter2 = CreateWindowExW(
            0, L"TSCBSplitter", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 150 + g_hConsistSplit, g_wConsist, 9,
            hWnd, NULL, hInst, NULL
        );
        if (g_hSplitter2) SetWindowLongPtrW(g_hSplitter2, GWLP_USERDATA, (LONG_PTR)SPLITTER_HORIZONTAL);

        g_hSplitter3 = CreateWindowExW(
            0, L"TSCBSplitter", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            g_wCategorySplit, 150 + g_hConsistSplit + 9 + 29, 9, 300,
            hWnd, NULL, hInst, NULL
        );
        if (g_hSplitter3) SetWindowLongPtrW(g_hSplitter3, GWLP_USERDATA, (LONG_PTR)SPLITTER_VERTICAL_SUB);

        g_hSplitter3Top = CreateWindowExW(
            0, L"TSCBSplitter", L"",
            WS_CHILD | WS_CLIPSIBLINGS,
            g_wCategorySplit, 150 + 29, 9, 200,
            hWnd, NULL, hInst, NULL
        );
        if (g_hSplitter3Top) SetWindowLongPtrW(g_hSplitter3Top, GWLP_USERDATA, (LONG_PTR)SPLITTER_VERTICAL_SUB);

        // Splitter between Section 2 (Editor Workspace) and Section 3 (3D Visual Consist View)
        g_hSplitterSection3 = CreateWindowExW(
            0, L"TSCBSplitter", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            g_wConsist + 9, 600, 500, 9,
            hWnd, NULL, hInst, NULL
        );
        if (g_hSplitterSection3) SetWindowLongPtrW(g_hSplitterSection3, GWLP_USERDATA, (LONG_PTR)SPLITTER_HORIZONTAL_SECTION3);

        // Bold/slightly larger font for section headers
        HFONT hSectionFont = NULL;
        if (!g_hStockMdl2Font)
        {
            g_hStockMdl2Font = CreateMdl2IconFont(9.5f, FW_NORMAL);
        }
        if (hUIFont)
        {
            LOGFONT lf = {};
            GetObject(hUIFont, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight - 2) : (lf.lfHeight + 2);
            g_hSectionFont = CreateFontIndirect(&lf);
            hSectionFont = g_hSectionFont;
        }

        if (hUIFont)
        {
            if (g_hConsistHeader)    SendMessage(g_hConsistHeader,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hConsistList)      SendMessage(g_hConsistList,     WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hRouteTree)        g_RouteTreeView.SetFont(hUIFont);
            if (g_hStockHeader)      SendMessage(g_hStockHeader,     WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hWorkspaceHeader)  SendMessage(g_hWorkspaceHeader, WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hCategoryTree)     g_CategoryTreeView.SetFont(hUIFont);
            if (g_hAssetList)        SendMessage(g_hAssetList,       WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hAssetList2)       SendMessage(g_hAssetList2,      WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hAssetList3)       SendMessage(g_hAssetList3,      WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            for (int k = 0; k < 3; ++k)
            {
                if (g_hPaneCatBtn[k])     SendMessage(g_hPaneCatBtn[k],     WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
                if (g_hPaneSearchEdit[k]) SendMessage(g_hPaneSearchEdit[k], WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            }
            if (g_hEditorPane)       SendMessage(g_hEditorPane,      WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelFileName)    SendMessage(g_hLabelFileName,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditFileName)     SendMessage(g_hEditFileName,    WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelTrainCfgId)  SendMessage(g_hLabelTrainCfgId, WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditTrainCfgId)   SendMessage(g_hEditTrainCfgId,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelTrainName)   SendMessage(g_hLabelTrainName,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditTrainName)    SendMessage(g_hEditTrainName,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelMaxVelocity) SendMessage(g_hLabelMaxVelocity,WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditMaxVelocity)  SendMessage(g_hEditMaxVelocity, WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelPerfFactor)  SendMessage(g_hLabelPerfFactor, WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditPerfFactor)   SendMessage(g_hEditPerfFactor,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelMetricMass)   SendMessage(g_hLabelMetricMass,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditMetricMass)    SendMessage(g_hEditMetricMass,    WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelMetricLength) SendMessage(g_hLabelMetricLength, WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditMetricLength)  SendMessage(g_hEditMetricLength,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelMetricPower)  SendMessage(g_hLabelMetricPower,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditMetricPower)   SendMessage(g_hEditMetricPower,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hLabelMetricRatio)  SendMessage(g_hLabelMetricRatio,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditMetricRatio)   SendMessage(g_hEditMetricRatio,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditorUnitList)   SendMessage(g_hEditorUnitList,  WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hSectionTrainCfg && hSectionFont)
                SendMessage(g_hSectionTrainCfg, WM_SETFONT, (WPARAM)hSectionFont, MAKELPARAM(TRUE, 0));
            if (g_hSectionMetrics && hSectionFont)
                SendMessage(g_hSectionMetrics,  WM_SETFONT, (WPARAM)hSectionFont, MAKELPARAM(TRUE, 0));
            if (g_hSectionUnits && hSectionFont)
                SendMessage(g_hSectionUnits,    WM_SETFONT, (WPARAM)hSectionFont, MAKELPARAM(TRUE, 0));
        }

        if (bLoaded)
        {
            SendMessage(hWnd, WM_NAVTOOLBAR_NAVIGATE, 0, 0);
        }

        // Spawn Consists Directory Watcher Thread
        g_bCancelWatcher = FALSE;
        g_hWatcherThread = CreateThread(NULL, 0, ConsistWatcherThreadProc, hWnd, 0, NULL);
    }
    break;

    case WM_REQUEST_DEBOUNCE_RESCAN:
    {
        KillTimer(hWnd, TIMER_DEBOUNCE_RESCAN);
        SetTimer(hWnd, TIMER_DEBOUNCE_RESCAN, 250, NULL);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_DEBOUNCE_RESCAN)
        {
            KillTimer(hWnd, TIMER_DEBOUNCE_RESCAN);
            TriggerConsistsRescan(hWnd);
        }
        else if (wParam == TIMER_IGNORE_WATCHER_RESET)
        {
            KillTimer(hWnd, TIMER_IGNORE_WATCHER_RESET);
            g_bIgnoreWatcher = FALSE;
        }
        return 0;
    }

    
    case WM_SYSKEYDOWN:
    {
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (bAlt && (wParam == 'V' || wParam == 'v'))
        {
            ShowClipboardContents(hWnd);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (bAlt && (wParam == 'V' || wParam == 'v'))
        {
            ShowClipboardContents(hWnd);
            return 0;
        }

        if (g_ActivePane == PANE_STOCK)
        {
            if (bCtrl && bShift && (wParam == 'C' || wParam == 'c'))
            {
                CopySelectedUnitNamesToClipboard(hWnd);
                return 0;
            }
            else if (bCtrl && (wParam == 'C' || wParam == 'c'))
            {
                CopySelectedStockUnits(hWnd);
                return 0;
            }
            else if (wParam == VK_RETURN)
            {
                std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
                if (!selectedStock.empty())
                {
                    std::vector<int> selConsist = GetSelectedConsistUnitIndices();
                    PasteConsistUnits(hWnd, selConsist.empty() ? PASTE_END : PASTE_AFTER_SELECTED, &selectedStock);
                }
                return 0;
            }
            else if (bCtrl && bShift && (wParam == 'V' || wParam == 'v'))
            {
                std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
                if (!selectedStock.empty())
                {
                    PasteConsistUnits(hWnd, PASTE_START, &selectedStock);
                }
                return 0;
            }
            else if (bCtrl && (wParam == 'V' || wParam == 'v'))
            {
                std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
                if (!selectedStock.empty())
                {
                    std::vector<int> selConsist = GetSelectedConsistUnitIndices();
                    PasteConsistUnits(hWnd, selConsist.empty() ? PASTE_END : PASTE_AFTER_SELECTED, &selectedStock);
                }
                return 0;
            }
            else if (bCtrl && bShift && (wParam == 'R' || wParam == 'r'))
            {
                std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
                if (!selectedStock.empty())
                {
                    ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING, &selectedStock);
                }
                return 0;
            }
            else if (bCtrl && (wParam == 'R' || wParam == 'r'))
            {
                std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
                if (!selectedStock.empty())
                {
                    ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS, &selectedStock);
                }
                return 0;
            }
        }
        else // PANE_WORKSPACE or PANE_CONSIST
        {
            if (wParam == VK_DELETE)
            {
                DeleteSelectedConsistUnits(hWnd);
                return 0;
            }
            else if (bCtrl && (wParam == 'Z' || wParam == 'z'))
            {
                PerformUndo(hWnd);
                return 0;
            }
            else if (bCtrl && (wParam == 'Y' || wParam == 'y'))
            {
                PerformRedo(hWnd);
                return 0;
            }
            else if (bCtrl && bShift && (wParam == 'C' || wParam == 'c'))
            {
                CopySelectedUnitNamesToClipboard(hWnd);
                return 0;
            }
            else if (bCtrl && (wParam == 'C' || wParam == 'c'))
            {
                CopySelectedUnitOrStock(hWnd);
                return 0;
            }
            else if (bCtrl && (wParam == 'X' || wParam == 'x'))
            {
                CutSelectedConsistUnits(hWnd);
                return 0;
            }
            else if (bCtrl && bShift && (wParam == 'V' || wParam == 'v'))
            {
                PasteConsistUnits(hWnd, PASTE_START);
                return 0;
            }
            else if (bCtrl && (wParam == 'V' || wParam == 'v'))
            {
                PasteConsistUnits(hWnd, PASTE_AFTER_SELECTED);
                return 0;
            }
            else if (bCtrl && bShift && (wParam == 'R' || wParam == 'r'))
            {
                std::vector<int> selIndices = GetSelectedConsistUnitIndices();
                if (!selIndices.empty() && selIndices[0] < (int)g_LoadedConsistUnits.size())
                {
                    ReplaceAllConsistUnitsWithName(hWnd, g_LoadedConsistUnits[selIndices[0]].uid);
                }
                return 0;
            }
            else if (bCtrl && (wParam == 'R' || wParam == 'r'))
            {
                ReplaceSelectedConsistUnits(hWnd);
                return 0;
            }
            else if (!bCtrl && !bShift && (wParam == 'F' || wParam == 'f'))
            {
                FlipSelectedConsistUnits(hWnd);
                return 0;
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == 8888 && wmEvent == LBN_SELCHANGE)
        {
            SetActivePane(PANE_WORKSPACE, hWnd);
            int clickedIndex = (int)(INT_PTR)lParam;
            for (int r = 0; r < g_EditorUnitList.GetItemCount(); ++r)
            {
                int no = _wtoi(g_EditorUnitList.GetCellText(r, 0).c_str()) - 1;
                if (no == clickedIndex)
                {
                    g_EditorUnitList.SetSelectedIndex(r);
                    g_EditorUnitList.EnsureVisible(r);
                    SyncPoolMutatorSelectionIfOpen();
                    break;
                }
            }
            return 0;
        }

        if (wmEvent == EN_SETFOCUS)
        {
            if (lParam == (LPARAM)g_hEditFileName || lParam == (LPARAM)g_hEditTrainCfgId || lParam == (LPARAM)g_hEditTrainName ||
                lParam == (LPARAM)g_hEditMaxVelocity || lParam == (LPARAM)g_hEditPerfFactor)
            {
                SetActivePane(PANE_WORKSPACE, hWnd);
            }
            else if (lParam == (LPARAM)g_hPaneSearchEdit[0] || lParam == (LPARAM)g_hPaneSearchEdit[1] ||
                     lParam == (LPARAM)g_hPaneSearchEdit[2])
            {
                SetActivePane(PANE_STOCK, hWnd);
            }
        }

        if (wmEvent == EN_CHANGE)
        {
            if (wmId >= IDC_PANE_SEARCH_0 && wmId <= IDC_PANE_SEARCH_2)
            {
                int paneIdx = wmId - IDC_PANE_SEARCH_0;
                if (paneIdx >= 0 && paneIdx < 3 && g_hPaneSearchEdit[paneIdx])
                {
                    wchar_t buf[256] = { 0 };
                    GetWindowTextW(g_hPaneSearchEdit[paneIdx], buf, 256);
                    g_szPaneSearchQuery[paneIdx] = buf;

                    // Immediately force instantaneous redraw of search edit box (0ms delay for '✕' / magnifying glass)
                    InvalidateRect(g_hPaneSearchEdit[paneIdx], NULL, FALSE);
                    UpdateWindow(g_hPaneSearchEdit[paneIdx]);

                    // Real-time grid filter
                    PopulateAssetGridPane(paneIdx);
                }
                return 0;
            }

            if (!g_bIsLoadingConsist && (wmId == IDC_ED_FILENAME || wmId == IDC_ED_TRAINCFGID || wmId == IDC_ED_TRAINNAME ||
                wmId == IDC_ED_MAXVELOCITY || wmId == IDC_ED_PERFFACTOR))
            {
                if (!g_szCurrentConsistFile.empty())
                {
                    g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
                    UpdateConsistManagerRow(g_szCurrentConsistFile);
                }
            }
        }
        else if (wmEvent == EN_KILLFOCUS)
        {
            if (wmId == IDC_ED_FILENAME || wmId == IDC_ED_TRAINCFGID || wmId == IDC_ED_TRAINNAME ||
                wmId == IDC_ED_MAXVELOCITY || wmId == IDC_ED_PERFFACTOR)
            {
                SaveCurrentConsist(hWnd);
            }
        }

        if ((wmId == IDC_ASSETLIST || wmId == IDC_ASSETLIST2 || wmId == IDC_ASSETLIST3) && wmEvent >= 1000 && wmEvent < 2000)
        {
            int paneIdx = (wmId == IDC_ASSETLIST) ? 0 : ((wmId == IDC_ASSETLIST2) ? 1 : 2);
            if (g_StockViewMode == StockViewMode::Single && paneIdx == 0 && g_hCategoryTree)
            {
                CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
                PopulateAssetGridPane(0, hSelected);
            }
            else
            {
                SortAssetGridPane(paneIdx);
                CustomListControl* pList = GetAssetListCtrl(paneIdx);
                if (pList) pList->Invalidate();
            }
            return 0;
        }

        if (wmEvent >= 2000)
        {
            int colIndex = wmEvent - 2000;
            if (wmId == IDC_CONSISTLIST)
            {
                ShowFilterPopup(g_hConsistList, colIndex);
                return 0;
            }
            else if (wmId == IDC_ASSETLIST)
            {
                ShowFilterPopup(g_hAssetList, colIndex);
                return 0;
            }
            else if (wmId == IDC_ASSETLIST2)
            {
                ShowFilterPopup(g_hAssetList2, colIndex);
                return 0;
            }
            else if (wmId == IDC_ASSETLIST3)
            {
                ShowFilterPopup(g_hAssetList3, colIndex);
                return 0;
            }
            else if (wmId == IDC_ED_UNITLIST)
            {
                ShowFilterPopup(g_hEditorUnitList, colIndex);
                return 0;
            }
        }

        if (wmEvent == STN_CLICKED)
        {
            if (wmId == IDC_CONSISTHEADER)
            {
                SetActivePane(PANE_CONSIST, hWnd);
                if (g_hConsistList) SetFocus(g_hConsistList);
            }
            else if (wmId == IDC_STOCKHEADER)
            {
                SetActivePane(PANE_STOCK, hWnd);
                if (g_hAssetList) SetFocus(g_hAssetList);
            }
            else if (wmId == IDC_WORKSPACEHEADER || wmId == IDC_EDITORPANE ||
                     wmId == IDC_ED_SECTION_CFG || wmId == IDC_ED_SECTION_UNITS)
            {
                SetActivePane(PANE_WORKSPACE, hWnd);
                if (g_hEditorUnitList) SetFocus(g_hEditorUnitList);
            }
        }
    }
    break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        LONG_PTR id = GetWindowLongPtr(hwndStatic, GWLP_ID);

        // Section header labels – bold, accent-coloured
        if (hwndStatic == g_hSectionTrainCfg || hwndStatic == g_hSectionUnits || hwndStatic == g_hSectionMetrics)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            static HBRUSH hbrSecDark = CreateSolidBrush(UITheme::DarkBackground);
            return (LRESULT)hbrSecDark;
        }
        // Caption labels – small, muted text above each field
        else if (hwndStatic == g_hLabelFileName || hwndStatic == g_hLabelTrainCfgId || hwndStatic == g_hLabelTrainName || hwndStatic == g_hLabelMaxVelocity || hwndStatic == g_hLabelPerfFactor ||
                 hwndStatic == g_hLabelMetricMass || hwndStatic == g_hLabelMetricLength || hwndStatic == g_hLabelMetricPower || hwndStatic == g_hLabelMetricRatio)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, UITheme::TextMuted);
            static HBRUSH hbrCapDark = CreateSolidBrush(UITheme::DarkBackground);
            return (LRESULT)hbrCapDark;
        }
        // Train Details read-only edit controls
        else if (hwndStatic == g_hEditFileName || hwndStatic == g_hEditTrainCfgId || hwndStatic == g_hEditTrainName || hwndStatic == g_hEditMaxVelocity || hwndStatic == g_hEditPerfFactor ||
                 hwndStatic == g_hEditMetricMass || hwndStatic == g_hEditMetricLength || hwndStatic == g_hEditMetricPower || hwndStatic == g_hEditMetricRatio)
        {
            COLORREF clrBg = (hwndStatic == g_hHoveredEdit) ? RGB(45, 45, 45) : RGB(38, 38, 38);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, clrBg);
            SetTextColor(hdc, RGB(220, 220, 220));
            static HBRUSH hbrEditRoNormal  = CreateSolidBrush(RGB(38, 38, 38));
            static HBRUSH hbrEditRoHovered = CreateSolidBrush(RGB(45, 45, 45));
            return (LRESULT)((hwndStatic == g_hHoveredEdit) ? hbrEditRoHovered : hbrEditRoNormal);
        }
        // Editor pane placeholder
        else if (id == IDC_EDITORPANE)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, UITheme::TextMuted);
            static HBRUSH hbrPaneDark = CreateSolidBrush(UITheme::DarkBackground);
            return (LRESULT)hbrPaneDark;
        }
        else if (id == IDC_CONSISTHEADER || id == IDC_STOCKHEADER || id == IDC_WORKSPACEHEADER)
        {
            SetBkMode(hdc, TRANSPARENT);
            BOOL isActive = FALSE;
            if (id == IDC_CONSISTHEADER && g_ActivePane == PANE_CONSIST) isActive = TRUE;
            if (id == IDC_STOCKHEADER && g_ActivePane == PANE_STOCK) isActive = TRUE;
            if (id == IDC_WORKSPACEHEADER && g_ActivePane == PANE_WORKSPACE) isActive = TRUE;

            if (isActive)
            {
                SetTextColor(hdc, RGB(255, 255, 255));
                static HBRUSH hbrActiveDark = CreateSolidBrush(RGB(24, 60, 100)); // Steel Blue Accent
                return (LRESULT)hbrActiveDark;
            }
            else
            {
                SetTextColor(hdc, UITheme::TextPrimary);
                static HBRUSH hbrInactiveDark = CreateSolidBrush(UITheme::DarkHeaderBackground);
                return (LRESULT)hbrInactiveDark;
            }
        }
    }
    break;

    case WM_CTLCOLOREDIT:
    {
        HDC  hdc      = (HDC)wParam;
        HWND hwndEdit = (HWND)lParam;
        // Restyle consist-editor fields and multi-pane search edit fields
        if (hwndEdit == g_hEditFileName || hwndEdit == g_hEditTrainCfgId || hwndEdit == g_hEditTrainName || hwndEdit == g_hEditMaxVelocity || hwndEdit == g_hEditPerfFactor ||
            hwndEdit == g_hEditMetricMass || hwndEdit == g_hEditMetricLength || hwndEdit == g_hEditMetricPower || hwndEdit == g_hEditMetricRatio ||
            hwndEdit == g_hPaneSearchEdit[0] || hwndEdit == g_hPaneSearchEdit[1] || hwndEdit == g_hPaneSearchEdit[2])
        {
            COLORREF clrBg = (hwndEdit == g_hFocusedEdit) ? RGB(30, 30, 30) : ((hwndEdit == g_hHoveredEdit) ? RGB(45, 45, 45) : RGB(38, 38, 38));
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, clrBg);
            SetTextColor(hdc, UITheme::TextPrimary);

            static HBRUSH hbrEditFocused = CreateSolidBrush(RGB(30, 30, 30));
            static HBRUSH hbrEditHovered = CreateSolidBrush(RGB(45, 45, 45));
            static HBRUSH hbrEditNormal  = CreateSolidBrush(RGB(38, 38, 38));

            if (hwndEdit == g_hFocusedEdit) return (LRESULT)hbrEditFocused;
            if (hwndEdit == g_hHoveredEdit) return (LRESULT)hbrEditHovered;
            return (LRESULT)hbrEditNormal;
        }
        // Generic dark-mode colouring for any other edit controls
        SetTextColor(hdc, UITheme::TextPrimary);
        SetBkColor(hdc, RGB(45, 45, 45));
        SetBkMode(hdc, OPAQUE);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(45, 45, 45));
        return (LRESULT)hbrEditDark;
    }

    case WM_NAVTOOLBAR_NAVIGATE:
    {
        wchar_t szPath[MAX_PATH] = { 0 };
        NavToolbar_GetPath(g_hNavToolbar, szPath, MAX_PATH);

        if (szPath[0] == L'\0')
        {
            std::wstring chosenPath;
            if (PromptUserForTrainSimDirectory(hWnd, chosenPath) && !chosenPath.empty())
            {
                NavToolbar_SetPath(g_hNavToolbar, chosenPath.c_str());
                SendMessage(hWnd, WM_NAVTOOLBAR_NAVIGATE, 0, 0);
            }
            break;
        }

        // 1. Deliberately check if the path physically exists on the PC system
        DWORD dwAttr = GetFileAttributesW(szPath);
        bool bPhysicallyExists = (dwAttr != INVALID_FILE_ATTRIBUTES && (dwAttr & FILE_ATTRIBUTE_DIRECTORY));

        if (!bPhysicallyExists)
        {
            LOG_WARN_W(L"Configured Train Simulator directory does not physically exist on this system: '%ls'", szPath);
            std::wstring msg = L"The configured Train Simulator directory was not found or does not physically exist on this system:\n\n" +
                std::wstring(szPath) + L"\n\nWould you like to browse and select the true physical Train Simulator installation directory now?";
            int res = ShowModernMessageBox(hWnd, msg.c_str(), L"Train Simulator Directory Not Found", MB_YESNO | MB_ICONWARNING);
            if (res == IDYES)
            {
                std::wstring chosenPath;
                if (PromptUserForTrainSimDirectory(hWnd, chosenPath) && !chosenPath.empty())
                {
                    NavToolbar_SetPath(g_hNavToolbar, chosenPath.c_str());
                    SendMessage(hWnd, WM_NAVTOOLBAR_NAVIGATE, 0, 0);
                    break;
                }
            }
            // User declined or cancelled folder picker: clear invalid path to keep DB and system in sync
            NavToolbar_SetPath(g_hNavToolbar, L"");
            g_szBasePath = L"";
            DatabaseManager::SetSetting(L"LastDirectory", L"");
            if (g_hAssetList) g_AssetList.Clear();
            EnterCriticalSection(&g_StockCacheCS);
            g_StockCache.clear();
            LeaveCriticalSection(&g_StockCacheCS);
            break;
        }

        // If the user navigated to the exact same already-validated path, do nothing
        if (!g_szBasePath.empty() && _wcsicmp(szPath, g_szBasePath.c_str()) == 0)
        {
            break;
        }

        // 2. Validation Guard: Check if the folder contains Train Simulator folders or files
        std::wstring testPath = szPath;
        if (testPath.back() != L'\\' && testPath.back() != L'/') testPath += L"\\";

        bool bIsTrainSimFolder = false;
        DWORD dwTrains   = GetFileAttributesW((testPath + L"TRAINS").c_str());
        DWORD dwRoutes   = GetFileAttributesW((testPath + L"ROUTES").c_str());
        DWORD dwConsists = GetFileAttributesW((testPath + L"TRAINS\\CONSISTS").c_str());
        DWORD dwTrainset = GetFileAttributesW((testPath + L"TRAINS\\TRAINSET").c_str());

        if ((dwTrains != INVALID_FILE_ATTRIBUTES && (dwTrains & FILE_ATTRIBUTE_DIRECTORY)) ||
            (dwRoutes != INVALID_FILE_ATTRIBUTES && (dwRoutes & FILE_ATTRIBUTE_DIRECTORY)) ||
            (dwConsists != INVALID_FILE_ATTRIBUTES && (dwConsists & FILE_ATTRIBUTE_DIRECTORY)) ||
            (dwTrainset != INVALID_FILE_ATTRIBUTES && (dwTrainset & FILE_ATTRIBUTE_DIRECTORY)))
        {
            bIsTrainSimFolder = true;
        }
        else
        {
            // Check if directory contains .con or .eng or .wag files directly
            WIN32_FIND_DATAW fd;
            HANDLE hF = FindFirstFileW((testPath + L"*.con").c_str(), &fd);
            if (hF != INVALID_HANDLE_VALUE) { bIsTrainSimFolder = true; FindClose(hF); }
            else
            {
                hF = FindFirstFileW((testPath + L"*.eng").c_str(), &fd);
                if (hF != INVALID_HANDLE_VALUE) { bIsTrainSimFolder = true; FindClose(hF); }
            }
        }

        if (!bIsTrainSimFolder)
        {
            std::wstring msg = L"The selected directory does not appear to contain Train Simulator data (e.g. TRAINS, ROUTES, or Consists):\n\n" +
                std::wstring(szPath) + L"\n\nAre you sure you want to switch to this directory and scan it?";
            int res = ShowModernMessageBox(hWnd, msg.c_str(), L"Confirm Directory Switch", MB_YESNO | MB_ICONWARNING);
            if (res != IDYES)
            {
                // Revert address bar to current active path
                if (!g_szBasePath.empty())
                {
                    NavToolbar_SetPath(g_hNavToolbar, g_szBasePath.c_str());
                }
                break;
            }
        }

        g_szBasePath = szPath;
        LOG_INFO("Active Train Simulator base directory changed to: %ls", g_szBasePath.c_str());
        DatabaseManager::SetSetting(L"LastDirectory", szPath);

        if (g_hAssetList)
        {
            g_AssetList.Clear();
        }
        EnterCriticalSection(&g_StockCacheCS);
        g_StockCache.clear();
        LeaveCriticalSection(&g_StockCacheCS);

        CancelStockScan(g_hStockScanThread);

        TriggerConsistsRescan(hWnd);

        g_hStockScanThread = StartStockScan(hWnd, szPath);
    }
    break;

    case WM_ADD_CONSIST_ITEM:
    {
        ScannedConsist* pConsist = (ScannedConsist*)lParam;
        if (pConsist)
        {
            g_ScannedConsistsCache.push_back(*pConsist);

            if (g_hConsistList && g_ActiveTab == 0)
            {
                auto itSess = g_ConsistSessions.find(pConsist->szFileName);
                std::wstring statusStr;
                if (itSess != g_ConsistSessions.end())
                {
                    statusStr = EvaluateAndUpdateConsistStatus(pConsist->szFileName, itSess->second.units, !itSess->second.isDirty);
                }
                else
                {
                    bool isFixedInSession = (!pConsist->isBroken && g_SessionFixedConsists.count(pConsist->szFileName) > 0);
                    if (isFixedInSession) statusStr = L"Fixed";
                    else if (pConsist->hasMissingStock || (pConsist->isBroken && !pConsist->hasMissingShape)) statusStr = L"Missing Stock";
                    else if (pConsist->hasMissingShape) statusStr = L"Missing Shape";
                    else statusStr = L"Healthy";
                }

                bool match = true;
                if (!g_szConsistSearchQuery.empty() &&
                    !StringContainsIgnoreCase(pConsist->szFileName, g_szConsistSearchQuery) &&
                    !StringContainsIgnoreCase(pConsist->szName, g_szConsistSearchQuery))
                {
                    match = false;
                }
                if (match && !MatchLetterFilter(pConsist->szName, g_ConsistList.GetActiveFilters(0)))
                    match = false;
                if (match && !MatchUnitsFilter(pConsist->nUnits, g_ConsistList.GetActiveFilters(1)))
                    match = false;
                if (match)
                {
                    const auto& statusFilters = g_ConsistList.GetActiveFilters(2);
                    if (!statusFilters.empty())
                    {
                        if (std::find(statusFilters.begin(), statusFilters.end(), statusStr) == statusFilters.end())
                            match = false;
                    }
                }
                if (match && !MatchModifiedFilter(pConsist->ftLastWriteTime, g_ConsistList.GetActiveFilters(3)))
                    match = false;

                if (match)
                {
                    std::wstring displayName = pConsist->szName;
                    if (itSess != g_ConsistSessions.end() && itSess->second.isDirty)
                    {
                        displayName = L"● " + displayName;
                    }
                    g_ConsistList.AddItem({ displayName, std::to_wstring(pConsist->nUnits), statusStr, pConsist->szLastModified, pConsist->szFileName });
                }
            }
            delete pConsist;
        }
        return 0;
    }

    case WM_CONSIST_SCAN_COMPLETE:
    {
        PopulateConsistListFromCache();
        return 0;
    }

    case WM_STOCK_SCAN_COMPLETE:
    {
        EnterCriticalSection(&g_StockCacheCS);
        int totalItems = (int)g_StockCache.size();
        int engineCount = 0;
        int wagonCount = 0;
        for (const auto& it : g_StockCache)
        {
            if (_wcsicmp(it.szExtension.c_str(), L".eng") == 0) engineCount++;
            else wagonCount++;
        }
        LeaveCriticalSection(&g_StockCacheCS);

        if (g_hCategoryTree)
        {
            g_AssetList.SetSortState(0, true);
            CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
            PopulateAssetGrid(hSelected);
        }

        wchar_t szStockHeader[128];
        swprintf_s(szStockHeader, 128, L"  Stock Library [ Total: %d \x00B7 Engines: %d \x00B7 Wagons: %d ]", totalItems, engineCount, wagonCount);
        SetWindowTextW(g_hStockHeader, szStockHeader);

        wchar_t szMsg[256];
        swprintf_s(szMsg, 256, L"Stock Library scan complete!\n\nFound %d total rolling stock items (%d Engines, %d Wagons).",
            totalItems, engineCount, wagonCount);
        ShowModernMessageBox(hWnd, szMsg, L"Scan Completed", MB_OK | MB_ICONINFORMATION);

        return 0;
    }

    case WM_STOCK_SCAN_PROGRESS:
    {
        size_t current = (size_t)wParam;
        size_t total = (size_t)lParam;
        if (g_hStockHeader)
        {
            wchar_t szStockHeader[128];
            if (total > 0)
            {
                swprintf_s(szStockHeader, 128, L"  Stock Library [ Scanning... %zu / %zu items ]", current, total);
            }
            else
            {
                swprintf_s(szStockHeader, 128, L"  Stock Library [ Scanning... %zu items ]", current);
            }
            SetWindowTextW(g_hStockHeader, szStockHeader);
        }

        if (g_hCategoryTree)
        {
            CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
            PopulateAssetGrid(hSelected);
        }
        return 0;
    }

    case WM_NAVTOOLBAR_SEARCH:
    {
        wchar_t szSearch[256] = { 0 };
        NavToolbar_GetSearchQuery(g_hNavToolbar, szSearch, 256);

        if (g_ActivePane == PANE_CONSIST)
        {
            g_szConsistSearchQuery = szSearch;
            if (!g_ScannedConsistsCache.empty())
            {
                PopulateConsistListFromCache();
            }
            else
            {
                TriggerConsistsRescan(hWnd);
            }
        }
        else if (g_ActivePane == PANE_STOCK)
        {
            g_szStockSearchQuery = szSearch;
            if (g_hCategoryTree)
            {
                CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
                PopulateAssetGrid(hSelected);
            }
        }
    }
    break;

    case WM_NAVTOOLBAR_ACTION:
    {
        int actionId = (int)wParam;
        switch (actionId)
        {
        case NAV_ACTION_BACK:
            break;
        case NAV_ACTION_FORWARD:
            break;
        case NAV_ACTION_UP:
            break;
        case NAV_ACTION_REFRESH:
            if (!g_szBasePath.empty())
            {
                if (g_ActivePane == PANE_STOCK)
                {
                    CancelStockScan(g_hStockScanThread);
                    g_hStockScanThread = StartStockScan(hWnd, g_szBasePath, true /* bForceRescan */);
                }
                else
                {
                    TriggerConsistsRescan(hWnd);
                }
            }
            break;
        }
    }
    break;

    case WM_COMMANDBAR_ACTION:
    {
        int actionId = (int)wParam;
        switch (actionId)
        {
        case CMD_ACTION_NEW_CONSIST:
            ActionCreateNewConsist(hWnd);
            break;
        case CMD_ACTION_CLONE_CONSIST:
            ActionCloneConsist(hWnd);
            break;
        case CMD_ACTION_SAVE_CONSISTS:
            if (lParam == 1) // Chevron/Dropdown clicked
            {
                POINT pt;
                GetCursorPos(&pt);
                std::vector<std::wstring> allOpts = { L"Auto-Save" };
                std::vector<std::wstring> checkedOpts;
                if (g_bAutoSave) checkedOpts.push_back(L"Auto-Save");
                g_FilterPopup.Show(hWnd, 999, pt.x, pt.y, allOpts, checkedOpts, OnFilterPopupCallback, NULL);
            }
            else
            {
                SaveCurrentConsistSessionState();
                std::vector<int> selRows = g_ConsistList.GetSelectedIndices();

                if (g_ActiveTab == 1)
                {
                    if (selRows.size() > 1)
                    {
                        int savedCount = 0;
                        for (int row : selRows)
                        {
                            std::wstring strIdx = g_ConsistList.GetCellText(row, 3);
                            if (!strIdx.empty())
                            {
                                int cIdx = _wtoi(strIdx.c_str());
                                if (cIdx >= 0 && cIdx < (int)g_CurrentActivityData.consists.size())
                                {
                                    if (SaveActivityConsistByIndex(hWnd, cIdx, row))
                                    {
                                        savedCount++;
                                    }
                                }
                            }
                        }
                        wchar_t msg[128];
                        swprintf_s(msg, 128, L"%d activity consist(s) saved successfully to .act file.", savedCount);
                        ShowModernMessageBox(hWnd, msg, L"Save Activity Consist(s)", MB_OK | MB_ICONINFORMATION);
                    }
                    else
                    {
                        if (SaveCurrentActivityConsistDiskOnly(hWnd))
                        {
                            ShowModernMessageBox(hWnd, L"Activity consist saved successfully to .act file.", L"Save Activity Consist", MB_OK | MB_ICONINFORMATION);
                        }
                    }
                }
                else
                {
                    if (selRows.size() > 1)
                    {
                        int savedCount = 0;
                        for (int row : selRows)
                        {
                            std::wstring fname = g_ConsistList.GetCellText(row, 4);
                            if (!fname.empty())
                            {
                                if (SaveConsistSessionToDisk(hWnd, fname))
                                {
                                    savedCount++;
                                }
                            }
                        }
                        wchar_t msg[128];
                        swprintf_s(msg, 128, L"%d consist(s) saved successfully to disk.", savedCount);
                        ShowModernMessageBox(hWnd, msg, L"Save Consist(s)", MB_OK | MB_ICONINFORMATION);
                    }
                    else
                    {
                        if (SaveCurrentConsistDiskOnly(hWnd))
                        {
                            ShowModernMessageBox(hWnd, L"Consist saved successfully to disk.", L"Save Consist(s)", MB_OK | MB_ICONINFORMATION);
                        }
                    }
                }
            }
            break;
        case CMD_ACTION_DELETE_CONSISTS:
            ActionDeleteSelectedConsists(hWnd);
            break;
        case CMD_ACTION_REVERSE_CONSIST:
            ActionReverseConsist(hWnd);
            break;
        case CMD_ACTION_STOCK_INFO:
        {
            std::wstring targetUnitPath = L"";

            // 1. Check if user has selected a unit in Consist Editor Unit Table
            int selConsistUnit = GetSelectedConsistUnitIndex();
            if (selConsistUnit >= 0 && selConsistUnit < (int)g_LoadedConsistUnits.size())
            {
                const auto& u = g_LoadedConsistUnits[selConsistUnit];
                targetUnitPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
            }

            // 2. Check if user has selected a unit in Stock Library
            if (targetUnitPath.empty())
            {
                std::vector<ConsistReader::UnitInfo> stockUnits = GetSelectedStockUnitsFromLibrary();
                if (!stockUnits.empty())
                {
                    const auto& u = stockUnits[0];
                    targetUnitPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
                }
            }

            // 3. Check single selection in Stock Library direct cache lookup
            if (targetUnitPath.empty())
            {
                int selStock = g_AssetList.GetSelectedIndex();
                EnterCriticalSection(&g_StockCacheCS);
                if (selStock >= 0 && selStock < (int)g_FilteredStockIndices.size())
                {
                    size_t origIdx = g_FilteredStockIndices[selStock];
                    if (origIdx < g_StockCache.size())
                    {
                        const auto& item = g_StockCache[origIdx];
                        targetUnitPath = BuildFullStockPath(g_szBasePath, item.szFolder, item.szFileName, item.szExtension);
                    }
                }
                LeaveCriticalSection(&g_StockCacheCS);
            }

            // 4. Fallback: Take the first unit from the currently loaded consist if any
            if (targetUnitPath.empty() && !g_LoadedConsistUnits.empty())
            {
                const auto& u = g_LoadedConsistUnits[0];
                targetUnitPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
            }

            // 5. Fallback: Take the first stock item in library if any
            if (targetUnitPath.empty())
            {
                EnterCriticalSection(&g_StockCacheCS);
                if (!g_FilteredStockIndices.empty())
                {
                    size_t origIdx = g_FilteredStockIndices[0];
                    if (origIdx < g_StockCache.size())
                    {
                        const auto& item = g_StockCache[origIdx];
                        targetUnitPath = BuildFullStockPath(g_szBasePath, item.szFolder, item.szFileName, item.szExtension);
                    }
                }
                LeaveCriticalSection(&g_StockCacheCS);
            }

            if (targetUnitPath.empty())
            {
                ShowModernMessageBox(hWnd, L"Please select a rolling stock unit from the Stock Library or Consist Editor to inspect its specifications.", L"Stock Specification Inspector", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                ShowStockInfoDialog(hWnd, targetUnitPath, g_szBasePath);
            }
            break;
        }
        case CMD_ACTION_SHAPE_VIEWER:
        {
            std::wstring targetUnitPath = L"";

            int selConsistUnit = GetSelectedConsistUnitIndex();
            if (selConsistUnit >= 0 && selConsistUnit < (int)g_LoadedConsistUnits.size())
            {
                const auto& u = g_LoadedConsistUnits[selConsistUnit];
                targetUnitPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
            }

            if (targetUnitPath.empty())
            {
                std::vector<ConsistReader::UnitInfo> stockUnits = GetSelectedStockUnitsFromLibrary();
                if (!stockUnits.empty())
                {
                    const auto& u = stockUnits[0];
                    targetUnitPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
                }
            }

            if (targetUnitPath.empty())
            {
                int selStock = g_AssetList.GetSelectedIndex();
                EnterCriticalSection(&g_StockCacheCS);
                if (selStock >= 0 && selStock < (int)g_FilteredStockIndices.size())
                {
                    size_t origIdx = g_FilteredStockIndices[selStock];
                    if (origIdx < g_StockCache.size())
                    {
                        const auto& item = g_StockCache[origIdx];
                        targetUnitPath = BuildFullStockPath(g_szBasePath, item.szFolder, item.szFileName, item.szExtension);
                    }
                }
                LeaveCriticalSection(&g_StockCacheCS);
            }

            Show3DVisualStudioDialog(hWnd, targetUnitPath, g_szBasePath);
            break;
        }
        case CMD_ACTION_TRAIN_CONFIGS:
            ShowTrainConfigStudioDialog(hWnd);
            break;
        case CMD_ACTION_POOL_MANAGER:
            ShowPoolManagerDialog(hWnd);
            break;
        case CMD_ACTION_POOL_MUTATOR:
        {
            std::vector<std::wstring> selConsists;
            std::vector<int> selUnits;
            GetActiveTargetConsistsAndUnits(selConsists, selUnits);
            ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::MutateConsists, selConsists, selUnits);
            break;
        }
        case CMD_ACTION_BATCH_WIZARD:
            ShowBatchConsistGeneratorDialog(hWnd);
            break;
        case CMD_ACTION_REFRESH_CONSISTS:
            if (!g_szBasePath.empty())
            {
                bool hasDirty = false;
                for (const auto& kv : g_ConsistSessions)
                {
                    if (kv.second.isDirty) { hasDirty = true; break; }
                }
                if (hasDirty)
                {
                    int res = ShowModernMessageBox(hWnd, L"You have unsaved changes in one or more consists.\nRefreshing will discard all unsaved edits from memory.\n\nDo you want to continue?", L"Refresh Consists", MB_YESNO | MB_ICONWARNING);
                    if (res != IDYES) break;
                }
                g_ConsistSessions.clear();
                TriggerConsistsRescan(hWnd);
            }
            break;
        case CMD_ACTION_REFRESH_STOCKS:
            if (!g_szBasePath.empty())
            {
                CancelStockScan(g_hStockScanThread);
                g_hStockScanThread = StartStockScan(hWnd, g_szBasePath, true /* bForceRescan */);
            }
            break;
        case CMD_ACTION_ABOUT:
        {
            RECT rcBtn = CommandBar_GetButtonRect(g_hCommandBar, CMD_ACTION_ABOUT);
            POINT pt = { rcBtn.left, rcBtn.bottom + 2 };
            ClientToScreen(g_hCommandBar, &pt);

            bool bLogging = AppLogging::AppLogger::IsEnabled();

            std::vector<ContextMenuItem> aboutItems = {
                ContextMenuItem::Action(1, bLogging ? L"\xE73E" : L"\xE739", bLogging ? L"Diagnostic Logging (Enabled)" : L"Diagnostic Logging (Disabled)", bLogging ? L"Active" : L"Off"),
                ContextMenuItem::Action(2, L"\xE8A5", L"Open Logs Folder (AppData\\Logs)"),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(3, L"\xE895", L"Check for Online Updates..."),
                ContextMenuItem::Action(4, L"\xE946", L"About Train Sim Consist Builder", L"v9.3.0")
            };

            int cmd = ModernContextMenu::Show(hWnd, pt.x, pt.y, aboutItems, TRUE, 260);
            if (cmd == 1)
            {
                bool newState = !bLogging;
                AppLogging::AppLogger::SetEnabled(newState);
                if (newState)
                {
                    LOG_INFO("Diagnostic logging enabled by user from CommandBar menu.");
                }
            }
            else if (cmd == 2)
            {
                AppLogging::AppLogger::OpenLogsFolderInExplorer();
            }
            else if (cmd == 3)
            {
                Updater::CheckForUpdates(hWnd, false);
            }
            else if (cmd == 4)
            {
                Updater::ShowAboutDialog(hWnd);
            }
            break;
        }
        }
    }
    break;

    case WM_VISUAL_UNIT_SELECTED:
    {
        int sel = (int)wParam;
        if (sel >= 0 && sel < g_EditorUnitList.GetItemCount())
        {
            g_EditorUnitList.SetSelectedIndices({ sel });
            g_EditorUnitList.EnsureVisible(sel);
        }
        UpdateUnitPreviewFromSelected();
        return 0;
    }

    case WM_PREVIEW_UNIT_FLIPPED:
    {
        int sel = g_EditorUnitList.GetSelectedIndex();
        if (sel >= 0 && sel < g_EditorUnitList.GetItemCount())
        {
            std::wstring strNo = g_EditorUnitList.GetCellText(sel, 0);
            int originalNo = _wtoi(strNo.c_str());
            int originalIndex = originalNo - 1;
            if (originalIndex >= 0 && originalIndex < (int)g_LoadedConsistUnits.size())
            {
                PushUndoState(L"Toggle Orientation");
                g_LoadedConsistUnits[originalIndex].isFlipped = !g_LoadedConsistUnits[originalIndex].isFlipped;
                RefreshEditorUnitList();
                if (g_hVisualConsistView)
                {
                    VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                    VisualConsistView_SetSelected(g_hVisualConsistView, originalIndex);
                }
                SaveCurrentConsist(hWnd);
                UpdateUnitPreviewFromSelected();
            }
        }
        return 0;
    }

    case WM_VISUAL_UNIT_FLIPPED:
    {
        int idx = (int)wParam;
        if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
        {
            g_LoadedConsistUnits[idx].isFlipped = !g_LoadedConsistUnits[idx].isFlipped;
            RefreshEditorUnitList();
            if (g_hVisualConsistView)
            {
                VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
            }
            SaveCurrentConsist(hWnd);
        }
        return 0;
    }

    case WM_VISUAL_DOCK_CHANGED:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = (int)(short)LOWORD(lParam);
        int y = (int)(short)HIWORD(lParam);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right;
        int height = rc.bottom;
        int paneY = 150;

        if (g_DragState != DRAG_NONE)
        {
            if (g_DragState == DRAG_SPLIT1)
            {
                g_wConsist = x;
                if (g_wConsist < 200) g_wConsist = 200;
                if (g_wConsist > width - 200) g_wConsist = width - 200;
                if (g_wCategorySplit > g_wConsist - 80) g_wCategorySplit = g_wConsist - 80;
                if (g_wCategorySplit < 80) g_wCategorySplit = 80;
            }
            else if (g_DragState == DRAG_SPLIT2)
            {
                g_hConsistSplit = y - paneY;
                if (g_hConsistSplit < 80) g_hConsistSplit = 80;
                if (g_hConsistSplit > height - paneY - 120) g_hConsistSplit = height - paneY - 120;
            }
            else if (g_DragState == DRAG_SPLIT3)
            {
                g_wCategorySplit = x;
                if (g_wCategorySplit < 80) g_wCategorySplit = 80;
                if (g_wCategorySplit > g_wConsist - 80) g_wCategorySplit = g_wConsist - 80;
            }
            else if (g_DragState == DRAG_SPLIT_SEC3)
            {
                int paneHeight = height - paneY;
                g_hSection3Height = (paneY + paneHeight) - y;
                if (g_hSection3Height < 100) g_hSection3Height = 100;
                if (g_hSection3Height > paneHeight - 250) g_hSection3Height = paneHeight - 250;
            }

            // Force recalculate child positions
            SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(width, height));
            InvalidateRect(hWnd, NULL, TRUE);
            if (g_hCategoryTree != NULL) InvalidateRect(g_hCategoryTree, NULL, TRUE);
            if (g_hConsistList != NULL) g_ConsistList.Invalidate();
            if (g_hAssetList != NULL) g_AssetList.Invalidate();
            UpdateWindow(hWnd);
        }
        else
        {
            DragState hover = GetSplitterUnderMouse(x, y, width, height);
            if (hover == DRAG_SPLIT1 || hover == DRAG_SPLIT3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (hover == DRAG_SPLIT2 || hover == DRAG_SPLIT_SEC3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        int x = (int)(short)LOWORD(lParam);
        int y = (int)(short)HIWORD(lParam);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right;
        int height = rc.bottom;

        DragState hover = GetSplitterUnderMouse(x, y, width, height);
        if (hover != DRAG_NONE)
        {
            g_DragState = hover;
            SetCapture(hWnd);
            if (hover == DRAG_SPLIT1 || hover == DRAG_SPLIT3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else if (hover == DRAG_SPLIT2 || hover == DRAG_SPLIT_SEC3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
            }
        }
        else
        {
            int wLeftPane = g_wConsist;
            int paneY = 150;
            if (x > wLeftPane + 9)
            {
                SetActivePane(PANE_WORKSPACE, hWnd);
            }
            else if (x <= wLeftPane && y >= paneY)
            {
                if (y < paneY + g_hConsistSplit)
                {
                    SetActivePane(PANE_CONSIST, hWnd);
                }
                else
                {
                    SetActivePane(PANE_STOCK, hWnd);
                }
            }
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        if (g_DragState != DRAG_NONE)
        {
            ReleaseCapture();
            g_DragState = DRAG_NONE;
        }
    }
    break;

    case WM_CAPTURECHANGED:
    {
        if ((HWND)lParam != hWnd)
        {
            g_DragState = DRAG_NONE;
        }
    }
    break;

    case WM_SETCURSOR:
    {
        if (g_DragState == DRAG_SPLIT1 || g_DragState == DRAG_SPLIT3)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        else if (g_DragState == DRAG_SPLIT2 || g_DragState == DRAG_SPLIT_SEC3)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
            return TRUE;
        }

        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);

            RECT rc;
            GetClientRect(hWnd, &rc);
            DragState hover = GetSplitterUnderMouse(pt.x, pt.y, rc.right, rc.bottom);
            if (hover == DRAG_SPLIT1 || hover == DRAG_SPLIT3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
            else if (hover == DRAG_SPLIT2 || hover == DRAG_SPLIT_SEC3)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        if (g_hCustomTitleBar != NULL)
        {
            SetWindowPos(g_hCustomTitleBar, NULL, 0, 0, width, 66, SWP_NOZORDER);
            CustomTitleBar_UpdateWindowState(g_hCustomTitleBar);
        }

        if (g_hNavToolbar != NULL)
        {
            SetWindowPos(g_hNavToolbar, NULL, 0, 66, width, 44, SWP_NOZORDER);
        }

        if (g_hCommandBar != NULL)
        {
            SetWindowPos(g_hCommandBar, NULL, 0, 110, width, 40, SWP_NOZORDER);
        }

        int paneY = 150;
        int paneHeight = height - paneY;
        if (paneHeight < 50) paneHeight = 50;

        int wLeftPane = g_wConsist;
        int wEditor = width - wLeftPane - 9; // 9px vertical gap for Splitter 1
        if (wEditor < 100) wEditor = 100;

        int wTreeSplitWidth = wLeftPane; // Extend all the way to the splitter line

        // Top Deck: Consists
        if (g_hConsistHeader != NULL)
        {
            SetWindowPos(g_hConsistHeader, NULL, 0, paneY, wTreeSplitWidth, 28, SWP_NOZORDER | SWP_NOCOPYBITS);
            InvalidateRect(g_hConsistHeader, NULL, TRUE);
        }

        if (g_ActiveTab == 1) // Activity Consists Tab
        {
            if (g_hRouteTree != NULL)
            {
                g_RouteTreeView.SetBounds(0, paneY + 29, g_wCategorySplit, g_hConsistSplit - 29);
                g_RouteTreeView.Show(true);
            }
            if (g_hConsistList != NULL)
            {
                int xConsist = g_wCategorySplit + 9;
                int wConsistList = wTreeSplitWidth - xConsist;
                if (wConsistList < 50) wConsistList = 50;
                SetWindowPos(g_hConsistList, NULL, xConsist, paneY + 29, wConsistList, g_hConsistSplit - 29, SWP_NOZORDER | SWP_NOCOPYBITS);
                ShowWindow(g_hConsistList, SW_SHOW);
            }
        }
        else // Main Consists Tab (Tab 0)
        {
            if (g_hRouteTree != NULL)
            {
                g_RouteTreeView.Show(false);
            }
            if (g_hConsistList != NULL)
            {
                SetWindowPos(g_hConsistList, NULL, 0, paneY + 29, wTreeSplitWidth, g_hConsistSplit - 29, SWP_NOZORDER | SWP_NOCOPYBITS);
                ShowWindow(g_hConsistList, SW_SHOW);
            }
        }

        // Bottom Deck: Stocks (Engines & Wagons)
        int bottomY = paneY + g_hConsistSplit + 9; // 9px gap for horizontal Splitter 2
        int bottomHeight = paneHeight - g_hConsistSplit - 9;
        if (bottomHeight < 50) bottomHeight = 50;

        if (g_hStockHeader != NULL)
        {
            SetWindowPos(g_hStockHeader, NULL, 0, bottomY, wTreeSplitWidth, 28, SWP_NOZORDER | SWP_NOCOPYBITS);
            InvalidateRect(g_hStockHeader, NULL, TRUE);
        }

        int contentY = bottomY + 29;
        int contentHeight = bottomHeight - 29;
        if (contentHeight < 40) contentHeight = 40;

        if (g_StockViewMode == StockViewMode::Single)
        {
            // Show Category Tree and Splitter 3
            if (g_hCategoryTree != NULL)
            {
                g_CategoryTreeView.SetBounds(0, contentY, g_wCategorySplit, contentHeight);
                g_CategoryTreeView.Show(true);
            }
            if (g_hSplitter3 != NULL)
            {
                SetWindowPos(g_hSplitter3, NULL, g_wCategorySplit, contentY, 9, contentHeight, SWP_NOZORDER | SWP_SHOWWINDOW);
            }

            // Hide unused multi-pane controls
            for (int k = 0; k < 3; ++k)
            {
                if (g_hPaneCatBtn[k]) ShowWindow(g_hPaneCatBtn[k], SW_HIDE);
            }
            if (g_hPaneSearchEdit[1]) ShowWindow(g_hPaneSearchEdit[1], SW_HIDE);
            if (g_hPaneSearchEdit[2]) ShowWindow(g_hPaneSearchEdit[2], SW_HIDE);
            if (g_hAssetList2) ShowWindow(g_hAssetList2, SW_HIDE);
            if (g_hAssetList3) ShowWindow(g_hAssetList3, SW_HIDE);

            const int HEADER_BAR_H = 26;
            const int RIGHT_MARGIN = 4;
            int xAsset = g_wCategorySplit + 9;
            int wAsset = wTreeSplitWidth - xAsset;
            if (wAsset < 50) wAsset = 50;

            // Show and position Search Edit for Single mode (Pane 0)
            int searchW = wAsset - RIGHT_MARGIN;
            if (searchW < 50) searchW = 50;
            if (g_hPaneSearchEdit[0])
            {
                SetWindowPos(g_hPaneSearchEdit[0], NULL, xAsset, contentY + 2, searchW, HEADER_BAR_H - 4, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                RECT rcFmt = { 8, 3, searchW - 26, HEADER_BAR_H - 4 - 3 };
                SendMessage(g_hPaneSearchEdit[0], EM_SETRECT, 0, (LPARAM)&rcFmt);
                SendMessage(g_hPaneSearchEdit[0], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 26));
                InvalidateRect(g_hPaneSearchEdit[0], NULL, TRUE);
            }

            int listH = contentHeight - HEADER_BAR_H - 4;
            if (listH < 30) listH = 30;

            if (g_hAssetList != NULL)
            {
                SetWindowPos(g_hAssetList, NULL, xAsset, contentY + HEADER_BAR_H, wAsset, listH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
            }
        }
        else if (g_StockViewMode == StockViewMode::Dual)
        {
            // Hide Category Tree, Splitter 3, Pane 2 controls
            if (g_hCategoryTree != NULL) g_CategoryTreeView.Show(false);
            if (g_hSplitter3 != NULL) ShowWindow(g_hSplitter3, SW_HIDE);
            if (g_hAssetList3) ShowWindow(g_hAssetList3, SW_HIDE);
            if (g_hPaneCatBtn[2]) ShowWindow(g_hPaneCatBtn[2], SW_HIDE);
            if (g_hPaneSearchEdit[2]) ShowWindow(g_hPaneSearchEdit[2], SW_HIDE);

            const int GAP = 6;
            const int HEADER_BAR_H = 26;
            const int RIGHT_MARGIN = 4;
            int availW = wTreeSplitWidth - RIGHT_MARGIN;
            if (availW < 160) availW = 160;

            int colW = (availW - GAP) / 2;
            if (colW < 80) colW = 80;

            int listH = contentHeight - HEADER_BAR_H - 4;
            if (listH < 30) listH = 30;

            for (int k = 0; k < 2; ++k)
            {
                int colX = k * (colW + GAP);
                int actualW = (k == 1) ? (availW - colX) : colW;
                if (actualW < 80) actualW = 80;

                int catW = (actualW * 45) / 100;
                if (catW < 105) catW = 105;
                if (catW > actualW - 70) catW = actualW - 70;
                int searchW = actualW - catW - 4;
                if (searchW < 50) searchW = 50;

                if (g_hPaneCatBtn[k])
                {
                    SetWindowPos(g_hPaneCatBtn[k], NULL, colX, contentY + 2, catW, HEADER_BAR_H - 4, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                    InvalidateRect(g_hPaneCatBtn[k], NULL, TRUE);
                }
                if (g_hPaneSearchEdit[k])
                {
                    SetWindowPos(g_hPaneSearchEdit[k], NULL, colX + catW + 4, contentY + 2, searchW, HEADER_BAR_H - 4, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                    RECT rcFmt = { 8, 3, searchW - 26, HEADER_BAR_H - 4 - 3 };
                    SendMessage(g_hPaneSearchEdit[k], EM_SETRECT, 0, (LPARAM)&rcFmt);
                    SendMessage(g_hPaneSearchEdit[k], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 26));
                    InvalidateRect(g_hPaneSearchEdit[k], NULL, TRUE);
                }

                HWND hList = (k == 0) ? g_hAssetList : g_hAssetList2;
                if (hList)
                {
                    SetWindowPos(hList, NULL, colX, contentY + HEADER_BAR_H, actualW, listH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                }
            }
        }
        else if (g_StockViewMode == StockViewMode::Triple)
        {
            // Hide Category Tree and Splitter 3
            if (g_hCategoryTree != NULL) g_CategoryTreeView.Show(false);
            if (g_hSplitter3 != NULL) ShowWindow(g_hSplitter3, SW_HIDE);

            const int GAP = 6;
            const int HEADER_BAR_H = 26;
            const int RIGHT_MARGIN = 4;
            int availW = wTreeSplitWidth - RIGHT_MARGIN;
            if (availW < 180) availW = 180;

            int colW = (availW - GAP * 2) / 3;
            if (colW < 60) colW = 60;

            int listH = contentHeight - HEADER_BAR_H - 4;
            if (listH < 30) listH = 30;

            for (int k = 0; k < 3; ++k)
            {
                int colX = k * (colW + GAP);
                int actualW = (k == 2) ? (availW - colX) : colW;
                if (actualW < 60) actualW = 60;

                int catW = (actualW * 45) / 100;
                if (catW < 85) catW = 85;
                if (catW > actualW - 55) catW = actualW - 55;
                int searchW = actualW - catW - 4;
                if (searchW < 45) searchW = 45;

                if (g_hPaneCatBtn[k])
                {
                    SetWindowPos(g_hPaneCatBtn[k], NULL, colX, contentY + 2, catW, HEADER_BAR_H - 4, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                    InvalidateRect(g_hPaneCatBtn[k], NULL, TRUE);
                }
                if (g_hPaneSearchEdit[k])
                {
                    SetWindowPos(g_hPaneSearchEdit[k], NULL, colX + catW + 4, contentY + 2, searchW, HEADER_BAR_H - 4, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                    RECT rcFmt = { 8, 3, searchW - 26, HEADER_BAR_H - 4 - 3 };
                    SendMessage(g_hPaneSearchEdit[k], EM_SETRECT, 0, (LPARAM)&rcFmt);
                    SendMessage(g_hPaneSearchEdit[k], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 26));
                    InvalidateRect(g_hPaneSearchEdit[k], NULL, TRUE);
                }

                HWND hList = (k == 0) ? g_hAssetList : ((k == 1) ? g_hAssetList2 : g_hAssetList3);
                if (hList)
                {
                    SetWindowPos(hList, NULL, colX, contentY + HEADER_BAR_H, actualW, listH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
                }
            }
        }

        // Consists ListView
        if (g_hConsistList != NULL)
        {
            // Column widths remain fixed/natural
        }

        // Position Dedicated Splitters
        if (g_hSplitter1 != NULL)
        {
            SetWindowPos(g_hSplitter1, NULL, wLeftPane, paneY, 9, paneHeight, SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        if (g_hSplitter2 != NULL)
        {
            SetWindowPos(g_hSplitter2, NULL, 0, paneY + g_hConsistSplit, wLeftPane, 9, SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        if (g_hSplitter3Top != NULL)
        {
            if (g_ActiveTab == 1)
            {
                SetWindowPos(g_hSplitter3Top, NULL, g_wCategorySplit, paneY + 29, 9, g_hConsistSplit - 29, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_hSplitter3Top, SW_HIDE);
            }
        }

        // Right Pane: Editor Workspace & Dedicated Bottom Visual Preview (Section 3)
        bool isFloating = (g_hVisualConsistView != NULL && VisualConsistView_IsFloating(g_hVisualConsistView));
        bool isCollapsed = (g_hVisualConsistView != NULL && VisualConsistView_IsCollapsed(g_hVisualConsistView));

        int visH = 0;
        if (!isFloating)
        {
            if (isCollapsed)
            {
                visH = 28; // HEADER_HEIGHT
            }
            else
            {
                visH = g_hSection3Height;
                if (visH < 100) visH = 100;
                if (visH > paneHeight - 250) visH = paneHeight - 250;
                g_hSection3Height = visH;
            }
        }

        const int SPLIT_H  = 9;   // Splitter thickness
        const int GUTTER   = 24;  // left/right margin for Section 2 inner cards
        const int FIELD_H  = 32;  // edit box height (and label height)
        const int ROW_GAP  = 12;  // vertical gap between rows
        const int SEC_H    = 26;  // section header height
        const int SEC_GAP  = 14;  // gap after section header
        const int VPAD     = 8;   // 8 px top + bottom margin

        int edX = wLeftPane + 9 + GUTTER;
        int edW = wEditor - GUTTER * 2;
        if (edW < 100) edW = 100;

        int splitterSpace = (!isFloating && !isCollapsed) ? SPLIT_H : 0;
        int topAreaHeight = paneHeight - visH - splitterSpace;
        if (topAreaHeight < 100) topAreaHeight = 100;

        // Position Workspace Header (static control)
        if (g_hWorkspaceHeader != NULL)
        {
            SetWindowPos(g_hWorkspaceHeader, NULL, wLeftPane + 9, paneY, wEditor, 28, SWP_NOZORDER | SWP_NOCOPYBITS);
            InvalidateRect(g_hWorkspaceHeader, NULL, TRUE);
        }

        // Position placeholder in the top region (Section 2)
        if (g_hEditorPane != NULL)
        {
            SetWindowPos(g_hEditorPane, NULL, wLeftPane + 9, paneY + 29, wEditor, topAreaHeight - 29, SWP_NOZORDER);
        }

        // Section 3 Horizontal Splitter & Docked Visual Consist View
        if (g_hSplitterSection3 != NULL)
        {
            if (!isFloating && !isCollapsed)
            {
                int splitY = paneY + paneHeight - visH - SPLIT_H;
                SetWindowPos(g_hSplitterSection3, NULL, wLeftPane + 9, splitY, wEditor, SPLIT_H, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_hSplitterSection3, SW_HIDE);
            }
        }

        if (g_hVisualConsistView)
        {
            if (isFloating)
            {
                ShowWindow(g_hVisualConsistView, SW_HIDE);
            }
            else
            {
                int visY = paneY + paneHeight - visH;
                SetWindowPos(g_hVisualConsistView, NULL, wLeftPane + 9, visY, wEditor, visH, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
        }

            // Dual card layout: Train Details on Left, 3D Unit Preview Card on Right (Directly edge-resizable)
            int previewW = g_wUnitPreviewWidth;
            if (previewW < 200) previewW = 200;
            if (previewW > edW - 240) previewW = edW - 240;
            if (edW < 450) previewW = edW / 2;

            int previewX = edX + edW - previewW;
            int leftCardW = previewX - edX - 12; // 12px clean margin b/w fields and unit preview card
            if (leftCardW < 180) leftCardW = 180;

            int leftCardX = edX;

            int labelW = 160;
            if (labelW > leftCardW - 80) labelW = leftCardW - 80;
            if (labelW < 80) labelW = 80;
            int valueW = leftCardW - labelW - 10;
            if (valueW < 50) valueW = 50;

            int leftValX = leftCardX + labelW + 10;

            int topY = paneY + 28 + GUTTER;
            int leftY = topY;

            // -------------------------------------------------------
            // Left Card: "TRAIN DETAILS" — centered directly over fields
            // -------------------------------------------------------
            if (g_hSectionTrainCfg)
                SetWindowPos(g_hSectionTrainCfg, NULL, leftValX, leftY, valueW, SEC_H, SWP_NOZORDER);
            leftY += SEC_H + SEC_GAP;

            if (g_ActiveTab == 0) // Main Consists Tab: Row 1 is File Name (.con)
            {
                if (g_hLabelFileName)
                    SetWindowPos(g_hLabelFileName, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditFileName) {
                    SetWindowPos(g_hEditFileName, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditFileName, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;
            }

            // Consist Identifier (Row 2 in Main Consists tab, Row 1 in Activity tab)
            if (g_hLabelTrainCfgId)
                SetWindowPos(g_hLabelTrainCfgId, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditTrainCfgId) {
                SetWindowPos(g_hEditTrainCfgId, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditTrainCfgId, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            leftY += FIELD_H + ROW_GAP;

            if (g_ActiveTab == 0) // Main Consists Tab: show and position Rows 3, 4, 5
            {
                // Row 3: Train Name
                if (g_hLabelTrainName)
                    SetWindowPos(g_hLabelTrainName, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditTrainName) {
                    SetWindowPos(g_hEditTrainName, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditTrainName, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;

                // Row 4: Speed Limit
                if (g_hLabelMaxVelocity)
                    SetWindowPos(g_hLabelMaxVelocity, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditMaxVelocity) {
                    SetWindowPos(g_hEditMaxVelocity, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditMaxVelocity, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;

                // Row 5: Performance Factor
                if (g_hLabelPerfFactor)
                    SetWindowPos(g_hLabelPerfFactor, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditPerfFactor) {
                    SetWindowPos(g_hEditPerfFactor, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditPerfFactor, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;
            }

            // -------------------------------------------------------
            // Right Card: 3D Live Rolling Stock Preview Card (Directly edge/corner resizable)
            // -------------------------------------------------------
            if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_HIDE);
            if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_HIDE);
            if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_HIDE);
            if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_HIDE);
            if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_HIDE);
            if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_HIDE);
            if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_HIDE);
            if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_HIDE);
            if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_HIDE);

            int previewH = g_hTopCardsHeight;
            int minTopH = (g_ActiveTab == 0) ? (leftY - topY) : 146;
            if (minTopH < 146) minTopH = 146;
            if (previewH < minTopH) previewH = minTopH;
            if (previewH > topAreaHeight - 160) previewH = topAreaHeight - 160;

            if (g_hSplitterUnitPreview) ShowWindow(g_hSplitterUnitPreview, SW_HIDE);
            if (g_hSplitterUnitPreviewRight) ShowWindow(g_hSplitterUnitPreviewRight, SW_HIDE);
            if (g_hSplitterTopDeck) ShowWindow(g_hSplitterTopDeck, SW_HIDE);

            if (g_hUnitPreviewCard)
            {
                if (previewW > 100)
                {
                    SetWindowPos(g_hUnitPreviewCard, NULL, previewX, topY, previewW, previewH, SWP_NOZORDER | SWP_SHOWWINDOW);
                }
                else
                {
                    ShowWindow(g_hUnitPreviewCard, SW_HIDE);
                }
            }

            // Bottom of top deck area and 9px clean margin before Consist Units table
            int topDeckBottom = (std::max)(leftY, topY + previewH);
            int curY = topDeckBottom + 9;

            // "Consist Units" section header (integrated 28px card header bar)
            const int SEC_H_UNITS = 28;
            int bottomLimit = paneY + topAreaHeight - 12;
            int tableHeight = bottomLimit - (curY + SEC_H_UNITS);
            if (tableHeight < 50) tableHeight = 50;

            // Full dynamic width for Consist Unit Table
            int tableW = edW;

            if (g_hSectionUnits)
                SetWindowPos(g_hSectionUnits, NULL, edX, curY, tableW, SEC_H_UNITS, SWP_NOZORDER);

            if (g_hEditorUnitList)
                SetWindowPos(g_hEditorUnitList, NULL, edX, curY + SEC_H_UNITS, tableW, tableHeight, SWP_NOZORDER);
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // Fill background with main DarkBackground
        HBRUSH hbrBg = CreateSolidBrush(UITheme::DarkBackground);
        FillRect(hdc, &rc, hbrBg);
        DeleteObject(hbrBg);

        int paneY = 150;
        int wLeftPane = g_wConsist;
        int wTreeSplitWidth = wLeftPane;
        int bottomY = paneY + g_hConsistSplit + 9;

        // Draw Splitter 1 Gutter (vertical separator bar: wLeftPane to wLeftPane + 9)
        RECT rcSplit1 = { wLeftPane, paneY, wLeftPane + 9, rc.bottom };
        HBRUSH hbrSplit = CreateSolidBrush(RGB(32, 32, 32));
        FillRect(hdc, &rcSplit1, hbrSplit);

        // Draw Splitter 2 Gutter (horizontal separator bar: y = g_hConsistSplit to g_hConsistSplit + 9)
        RECT rcSplit2 = { 0, paneY + g_hConsistSplit, wLeftPane, paneY + g_hConsistSplit + 9 };
        FillRect(hdc, &rcSplit2, hbrSplit);

        // Draw Splitter 3 Gutter (vertical Category Tree vs Asset List separator bar in bottom deck when Single mode, and top deck on Activity tab)
        if (g_StockViewMode == StockViewMode::Single)
        {
            RECT rcSplit3 = { g_wCategorySplit, bottomY + 28, g_wCategorySplit + 9, rc.bottom };
            FillRect(hdc, &rcSplit3, hbrSplit);
        }
        if (g_ActiveTab == 1)
        {
            RECT rcSplitTop = { g_wCategorySplit, paneY + 28, g_wCategorySplit + 9, paneY + g_hConsistSplit };
            FillRect(hdc, &rcSplitTop, hbrSplit);
        }

        // Draw Section 3 Horizontal Splitter Gutter when docked & expanded
        bool isFloating = (g_hVisualConsistView != NULL && VisualConsistView_IsFloating(g_hVisualConsistView));
        bool isCollapsed = (g_hVisualConsistView != NULL && VisualConsistView_IsCollapsed(g_hVisualConsistView));
        if (g_hSplitterSection3 && !isFloating && !isCollapsed)
        {
            int splitY = paneY + (rc.bottom - paneY) - g_hSection3Height - 9;
            RECT rcSplitSec3 = { wLeftPane + 9, splitY, rc.right, splitY + 9 };
            FillRect(hdc, &rcSplitSec3, hbrSplit);
        }

        DeleteObject(hbrSplit);

        // Draw outline borders for Splitters and Headers
        COLORREF clrLine = RGB(65, 65, 65);
        HPEN hPen = CreatePen(PS_SOLID, 1, clrLine);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        // Vertical Splitter Left and Right borders
        MoveToEx(hdc, wLeftPane, paneY, NULL);
        LineTo(hdc, wLeftPane, rc.bottom);
        MoveToEx(hdc, wLeftPane + 8, paneY, NULL);
        LineTo(hdc, wLeftPane + 8, rc.bottom);

        // Horizontal Splitter Top and Bottom borders
        MoveToEx(hdc, 0, paneY + g_hConsistSplit, NULL);
        LineTo(hdc, wLeftPane, paneY + g_hConsistSplit);
        MoveToEx(hdc, 0, paneY + g_hConsistSplit + 8, NULL);
        LineTo(hdc, wLeftPane, paneY + g_hConsistSplit + 8);

        // Draw vertical separator inside Stocks (TreeView on left, ListView on right)
        if (g_StockViewMode == StockViewMode::Single)
        {
            MoveToEx(hdc, g_wCategorySplit, bottomY + 28, NULL);
            LineTo(hdc, g_wCategorySplit, rc.bottom);
            MoveToEx(hdc, g_wCategorySplit + 8, bottomY + 28, NULL);
            LineTo(hdc, g_wCategorySplit + 8, rc.bottom);
        }

        if (g_ActiveTab == 1)
        {
            MoveToEx(hdc, g_wCategorySplit, paneY + 28, NULL);
            LineTo(hdc, g_wCategorySplit, paneY + g_hConsistSplit);
            MoveToEx(hdc, g_wCategorySplit + 8, paneY + 28, NULL);
            LineTo(hdc, g_wCategorySplit + 8, paneY + g_hConsistSplit);
        }

        // Line below Consist Header
        COLORREF clrConsistLine = (g_ActivePane == PANE_CONSIST) ? RGB(0, 120, 215) : clrLine;
        HPEN hPenConsist = CreatePen(PS_SOLID, (g_ActivePane == PANE_CONSIST) ? 2 : 1, clrConsistLine);
        HPEN hOldPenTmp = (HPEN)SelectObject(hdc, hPenConsist);
        MoveToEx(hdc, 0, paneY + 28, NULL);
        LineTo(hdc, wTreeSplitWidth, paneY + 28);
        SelectObject(hdc, hOldPenTmp);
        DeleteObject(hPenConsist);

        // Line below Stock Header
        COLORREF clrStockLine = (g_ActivePane == PANE_STOCK) ? RGB(0, 120, 215) : clrLine;
        HPEN hPenStock = CreatePen(PS_SOLID, (g_ActivePane == PANE_STOCK) ? 2 : 1, clrStockLine);
        hOldPenTmp = (HPEN)SelectObject(hdc, hPenStock);
        MoveToEx(hdc, 0, bottomY + 28, NULL);
        LineTo(hdc, wTreeSplitWidth, bottomY + 28);
        SelectObject(hdc, hOldPenTmp);
        DeleteObject(hPenStock);

        // Line below Workspace Header
        COLORREF clrWorkspaceLine = (g_ActivePane == PANE_WORKSPACE) ? RGB(0, 120, 215) : clrLine;
        HPEN hPenWorkspace = CreatePen(PS_SOLID, (g_ActivePane == PANE_WORKSPACE) ? 2 : 1, clrWorkspaceLine);
        hOldPenTmp = (HPEN)SelectObject(hdc, hPenWorkspace);
        MoveToEx(hdc, wLeftPane + 9, paneY + 28, NULL);
        LineTo(hdc, rc.right, paneY + 28);
        SelectObject(hdc, hOldPenTmp);
        DeleteObject(hPenWorkspace);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        // 1px App-Drawn Perimeter Border (Left, Right, Bottom) when windowed
        if (!IsZoomed(hWnd))
        {
            COLORREF clrOuterBorder = RGB(78, 32, 38);
            HPEN hPenOuterBorder = CreatePen(PS_SOLID, 1, clrOuterBorder);
            HPEN hOldOuterPen = (HPEN)SelectObject(hdc, hPenOuterBorder);

            // Left border
            MoveToEx(hdc, 0, paneY, NULL);
            LineTo(hdc, 0, rc.bottom);

            // Right border
            MoveToEx(hdc, rc.right - 1, paneY, NULL);
            LineTo(hdc, rc.right - 1, rc.bottom);

            // Bottom border
            MoveToEx(hdc, 0, rc.bottom - 1, NULL);
            LineTo(hdc, rc.right, rc.bottom - 1);

            SelectObject(hdc, hOldOuterPen);
            DeleteObject(hPenOuterBorder);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NOTIFY:
    {
        UINT_PTR idCtrl = (UINT_PTR)wParam;
        NMHDR* pnmh = (NMHDR*)lParam;
        // Internal Drag Hover / Drop Notifications (3001 = Hover, 3002 = Drop)
        if (pnmh->code == 3001) // Drag Hover
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            POINT ptScreen = { pCell->itemIndex, pCell->subItemIndex };

            // 1. Check if hovering over Batch Consist Wizard pool cards
            if (BatchWizard_IsActive())
            {
                if (BatchWizard_HandleDragHover(ptScreen))
                {
                    if (g_hEditorUnitList && IsWindow(g_hEditorUnitList))
                    {
                        g_EditorUnitList.SetDropTargetIndex(-1);
                    }
                    return TRUE;
                }
            }

            // 2. Check if hovering over Train Config Studio binding cards
            if (TrainConfigStudio_IsActive())
            {
                if (TrainConfigStudio_HandleDragHover(ptScreen))
                {
                    if (g_hEditorUnitList && IsWindow(g_hEditorUnitList))
                    {
                        g_EditorUnitList.SetDropTargetIndex(-1);
                    }
                    return TRUE;
                }

                RECT rcStudio = { 0 };
                HWND hStudio = TrainConfigStudio_GetHWND();
                if (hStudio && IsWindow(hStudio))
                {
                    GetWindowRect(hStudio, &rcStudio);
                }
                if (PtInRect(&rcStudio, ptScreen))
                {
                    if (g_hEditorUnitList && IsWindow(g_hEditorUnitList))
                    {
                        g_EditorUnitList.SetDropTargetIndex(-1);
                    }
                    return FALSE;
                }
            }

            RECT rcEditor = { 0 };
            if (g_hEditorUnitList && IsWindow(g_hEditorUnitList))
            {
                GetWindowRect(g_hEditorUnitList, &rcEditor);
            }

            bool isOverEditor = PtInRect(&rcEditor, ptScreen);
            if (isOverEditor)
            {
                POINT ptClient = ptScreen;
                ScreenToClient(g_hEditorUnitList, &ptClient);
                int dropIdx = g_EditorUnitList.GetDropIndexFromPoint(ptClient);
                g_EditorUnitList.SetDropTargetIndex(dropIdx);
                g_EditorUnitList.CheckDragAutoScroll(ptClient);
                return TRUE;
            }
            else
            {
                g_EditorUnitList.SetDropTargetIndex(-1);
                g_EditorUnitList.CheckDragAutoScroll({ -1, -1 });
                return FALSE;
            }
        }
        else if (pnmh->code == 3002) // Drag Drop Finalized
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            POINT ptScreen = { pCell->itemIndex, pCell->subItemIndex };

            // 1. Check if dropped into Batch Consist Wizard pool cards
            if (BatchWizard_IsActive())
            {
                std::vector<ConsistReader::UnitInfo> droppedUnits;
                if (pnmh->hwndFrom == g_hAssetList || pnmh->hwndFrom == g_hAssetList2 || pnmh->hwndFrom == g_hAssetList3)
                {
                    int paneIdx = (pnmh->hwndFrom == g_hAssetList) ? 0 : ((pnmh->hwndFrom == g_hAssetList2) ? 1 : 2);
                    CustomListControl* pList = GetAssetListCtrl(paneIdx);
                    std::vector<int> selStock = pList->GetSelectedIndices();
                    if (selStock.empty())
                    {
                        int singleSel = pList->GetSelectedIndex();
                        if (singleSel >= 0) selStock.push_back(singleSel);
                    }
                    EnterCriticalSection(&g_StockCacheCS);
                    for (int selIdx : selStock)
                    {
                        if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndicesPane[paneIdx].size())
                        {
                            size_t cacheIdx = g_FilteredStockIndicesPane[paneIdx][selIdx];
                            if (cacheIdx < g_StockCache.size())
                            {
                                const auto& item = g_StockCache[cacheIdx];
                                ConsistReader::UnitInfo u;
                                u.uid = item.szFileName;
                                u.parentDir = item.szFolder;
                                u.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                                u.isFlipped = false;
                                droppedUnits.push_back(u);
                            }
                        }
                    }
                    LeaveCriticalSection(&g_StockCacheCS);
                }
                else if (pnmh->hwndFrom == g_hEditorUnitList)
                {
                    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
                    for (int selIdx : selIndices)
                    {
                        if (selIdx >= 0 && selIdx < (int)g_LoadedConsistUnits.size())
                        {
                            droppedUnits.push_back(g_LoadedConsistUnits[selIdx]);
                        }
                    }
                }

                if (!droppedUnits.empty())
                {
                    if (BatchWizard_HandleDragDrop(ptScreen, droppedUnits))
                    {
                        g_EditorUnitList.SetDropTargetIndex(-1);
                        g_EditorUnitList.CheckDragAutoScroll({ -1, -1 });
                        return 0;
                    }
                }
            }

            // 2. Check if dropped into Train Config Studio dialog
            if (TrainConfigStudio_IsActive())
            {
                RECT rcStudio = { 0 };
                HWND hStudio = TrainConfigStudio_GetHWND();
                if (hStudio && IsWindow(hStudio))
                {
                    GetWindowRect(hStudio, &rcStudio);
                }
                if (PtInRect(&rcStudio, ptScreen))
                {
                    std::vector<ConsistReader::UnitInfo> droppedUnits;
                    if (pnmh->hwndFrom == g_hAssetList || pnmh->hwndFrom == g_hAssetList2 || pnmh->hwndFrom == g_hAssetList3)
                    {
                        int paneIdx = (pnmh->hwndFrom == g_hAssetList) ? 0 : ((pnmh->hwndFrom == g_hAssetList2) ? 1 : 2);
                        CustomListControl* pList = GetAssetListCtrl(paneIdx);
                        std::vector<int> selStock = pList->GetSelectedIndices();
                        if (selStock.empty())
                        {
                            int singleSel = pList->GetSelectedIndex();
                            if (singleSel >= 0) selStock.push_back(singleSel);
                        }
                        EnterCriticalSection(&g_StockCacheCS);
                        for (int selIdx : selStock)
                        {
                            if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndicesPane[paneIdx].size())
                            {
                                size_t cacheIdx = g_FilteredStockIndicesPane[paneIdx][selIdx];
                                if (cacheIdx < g_StockCache.size())
                                {
                                    const auto& item = g_StockCache[cacheIdx];
                                    ConsistReader::UnitInfo u;
                                    u.uid = item.szFileName;
                                    u.parentDir = item.szFolder;
                                    u.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                                    u.isFlipped = false;
                                    droppedUnits.push_back(u);
                                }
                            }
                        }
                        LeaveCriticalSection(&g_StockCacheCS);
                    }
                    else if (pnmh->hwndFrom == g_hEditorUnitList)
                    {
                        std::vector<int> selIndices = GetSelectedConsistUnitIndices();
                        for (int selIdx : selIndices)
                        {
                            if (selIdx >= 0 && selIdx < (int)g_LoadedConsistUnits.size())
                            {
                                droppedUnits.push_back(g_LoadedConsistUnits[selIdx]);
                            }
                        }
                    }

                    if (!droppedUnits.empty())
                    {
                        TrainConfigStudio_HandleDragDrop(ptScreen, droppedUnits);
                    }

                    g_EditorUnitList.SetDropTargetIndex(-1);
                    g_EditorUnitList.CheckDragAutoScroll({ -1, -1 });
                    return 0;
                }
            }

            RECT rcEditor = { 0 };
            if (g_hEditorUnitList && IsWindow(g_hEditorUnitList))
            {
                GetWindowRect(g_hEditorUnitList, &rcEditor);
            }
            bool isOverEditor = PtInRect(&rcEditor, ptScreen);
            int dropIdx = g_EditorUnitList.GetDropTargetIndex();
            if (dropIdx < 0 && isOverEditor)
            {
                dropIdx = g_EditorUnitList.GetDropIndexFromScreenPoint(ptScreen);
            }
            g_EditorUnitList.SetDropTargetIndex(-1);
            g_EditorUnitList.CheckDragAutoScroll({ -1, -1 });

            if (pnmh->hwndFrom == g_hEditorUnitList)
            {
                // Reorder within Consist Units Table
                if (isOverEditor && dropIdx >= 0)
                {
                    std::vector<int> selIndices = GetSelectedConsistUnitIndices();
                    int targetInsertPos = dropIdx;
                    if (dropIdx >= 0 && dropIdx < g_EditorUnitList.GetItemCount())
                    {
                        std::wstring noStr = g_EditorUnitList.GetCellText(dropIdx, 0);
                        targetInsertPos = _wtoi(noStr.c_str()) - 1;
                    }
                    else
                    {
                        targetInsertPos = (int)g_LoadedConsistUnits.size();
                    }
                    ReorderConsistUnits(hWnd, selIndices, targetInsertPos);
                }
            }
            else if (pnmh->hwndFrom == g_hAssetList || pnmh->hwndFrom == g_hAssetList2 || pnmh->hwndFrom == g_hAssetList3)
            {
                // Dedicated Stock Transfer from Stock Library to Consist Units Table
                if (isOverEditor && dropIdx >= 0)
                {
                    int paneIdx = (pnmh->hwndFrom == g_hAssetList) ? 0 : ((pnmh->hwndFrom == g_hAssetList2) ? 1 : 2);
                    CustomListControl* pList = GetAssetListCtrl(paneIdx);
                    std::vector<int> selStock = pList->GetSelectedIndices();
                    if (selStock.empty())
                    {
                        int singleSel = pList->GetSelectedIndex();
                        if (singleSel >= 0) selStock.push_back(singleSel);
                    }

                    if (!selStock.empty())
                    {
                        int targetInsertPos = dropIdx;
                        if (dropIdx >= 0 && dropIdx < g_EditorUnitList.GetItemCount())
                        {
                            std::wstring noStr = g_EditorUnitList.GetCellText(dropIdx, 0);
                            targetInsertPos = _wtoi(noStr.c_str()) - 1;
                        }
                        else
                        {
                            targetInsertPos = (int)g_LoadedConsistUnits.size();
                        }
                        TransferStockUnitsToConsist(hWnd, selStock, targetInsertPos, paneIdx);
                    }
                }
            }
            return 0;
        }
        if (idCtrl == IDC_ED_UNITLIST && pnmh->code == NM_RCELLCLICK)
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            int screenX = pCell->itemIndex;
            int screenY = pCell->subItemIndex;

            std::vector<int> selIndices = GetSelectedConsistUnitIndices();
            bool hasSelection = !selIndices.empty();
            bool canUndo = !g_UndoStack.empty();
            bool canRedo = !g_RedoStack.empty();
            bool canInsertUnits = !g_ClipboardUnits.empty();
            size_t clipCount = g_ClipboardUnits.size();
            std::wstring viewClipText = clipCount > 0 ? (L"View Clipboard (" + std::to_wstring(clipCount) + L" Units)...") : L"View Clipboard Contents...";

            std::vector<ContextMenuItem> menuItems;

            if (g_LoadedConsistUnits.empty())
            {
                // Streamlined menu for empty consist
                menuItems.push_back(ContextMenuItem::Action(6, L"\xE77F", L"Insert Unit(s)", L"Ctrl+V", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(11, L"\xE7B8", viewClipText, L"Alt+V", true));
                menuItems.push_back(ContextMenuItem::Action(12, L"\xE75C", L"Clear Clipboard", L"", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(7, L"\xE7A7", L"Undo", L"Ctrl+Z", canUndo));
                menuItems.push_back(ContextMenuItem::Action(8, L"\xE7A6", L"Redo", L"Ctrl+Y", canRedo));
            }
            else
            {
                // Full contextual menu for populated consist
                std::wstring primaryName = L"";
                if (hasSelection && selIndices[0] < (int)g_LoadedConsistUnits.size())
                {
                    primaryName = g_LoadedConsistUnits[selIndices[0]].uid;
                }

                std::wstring replaceAllText = L"Replace All with Stock";
                if (!primaryName.empty())
                {
                    replaceAllText = L"Replace All '" + primaryName + L"' with Stock";
                }

                bool canReplace = hasSelection && !g_ClipboardUnits.empty();

                menuItems.push_back(ContextMenuItem::Action(1, L"\xE74D", L"Delete Selected Units", L"Del", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(2, L"\xE777", L"Replace Selected Unit(s)", L"Ctrl+R", canReplace));
                menuItems.push_back(ContextMenuItem::Action(3, L"\xE8D7", replaceAllText, L"Ctrl+Shift+R", canReplace));
                menuItems.push_back(ContextMenuItem::Action(14, L"\xE790", L"Replace Selected from Pool Preset...", L"", hasSelection));

                // Cascading Submenu: "Replace with Favourite Group ▶"
                std::vector<ContextMenuItem> replaceSubItems;
                if (!PoolManager::g_ReplacementGroupsCache.empty())
                {
                    std::vector<std::wstring> cats = PoolManager::GetReplacementGroupCategories();
                    if (cats.size() > 1)
                    {
                        for (const auto& cat : cats)
                        {
                            std::vector<ContextMenuItem> catItems;
                            for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
                            {
                                const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
                                std::wstring grpCat = grp.category.empty() ? L"General" : grp.category;
                                if (_wcsicmp(grpCat.c_str(), cat.c_str()) == 0)
                                {
                                    std::wstring label = grp.name + L" (" + std::to_wstring(grp.units.size()) + L" units)";
                                    catItems.push_back(ContextMenuItem::Action(1000 + (int)g, L"\xE8D7", label, L"", hasSelection && !grp.units.empty()));
                                }
                            }
                            if (!catItems.empty())
                            {
                                replaceSubItems.push_back(ContextMenuItem::SubMenu(L"\xE8D7", cat, catItems, true));
                            }
                        }
                    }
                    else
                    {
                        for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
                        {
                            const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
                            std::wstring label = grp.name + L" (" + std::to_wstring(grp.units.size()) + L" units)";
                            replaceSubItems.push_back(ContextMenuItem::Action(1000 + (int)g, L"\xE8D7", label, L"", hasSelection && !grp.units.empty()));
                        }
                    }
                    replaceSubItems.push_back(ContextMenuItem::Separator());
                }
                replaceSubItems.push_back(ContextMenuItem::Action(1999, L"\xE713", L"Manage Favourite Groups...", L"", true));

                menuItems.push_back(ContextMenuItem::SubMenu(L"\xE8D7", L"Replace with Favourite Group", replaceSubItems, true));

                menuItems.push_back(ContextMenuItem::Action(4, L"\xE745", L"Flip Selected Unit(s)", L"F", hasSelection));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(16, L"\xE8C6", L"Cut Selected Unit(s)", L"Ctrl+X", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(5, L"\xE8C8", L"Copy Selected Unit(s)", L"Ctrl+C", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(20, L"\xE8C8", L"Copy Unit Name(s)", L"Ctrl+Shift+C", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(9, L"\xE77F", L"Insert at Beginning", L"Ctrl+Shift+V", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Action(6, L"\xE77F", L"Insert After Selected", L"Ctrl+V", canInsertUnits && hasSelection));
                menuItems.push_back(ContextMenuItem::Action(10, L"\xE77F", L"Insert at End", L"Ctrl+Alt+V", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Action(15, L"\xE77F", L"Insert Units from Pool Preset...", L"", true));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(11, L"\xE7B8", viewClipText, L"Alt+V", true));
                menuItems.push_back(ContextMenuItem::Action(12, L"\xE75C", L"Clear Clipboard", L"", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(17, L"\xE946", L"Inspect Unit Specifications (Stock Info)...", L"", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(19, L"\xE7F4", L"Inspect in 3D Visual Studio (Stock Inspector)...", L"", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(18, L"\xE7B7", L"View 3D Model in Shape Viewer...", L"", hasSelection));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(7, L"\xE7A7", L"Undo", L"Ctrl+Z", canUndo));
                menuItems.push_back(ContextMenuItem::Action(8, L"\xE7A6", L"Redo", L"Ctrl+Y", canRedo));
            }

            int cmd = ModernContextMenu::Show(hWnd, screenX, screenY, menuItems, TRUE);
            if (cmd >= 1000 && cmd < 1000 + (int)PoolManager::g_ReplacementGroupsCache.size())
            {
                ExecuteReplacementFromGroup(hWnd, cmd - 1000, selIndices);
                return 0;
            }
            if (cmd == 1999)
            {
                ShowPoolManagerDialog(hWnd, 1);
                return 0;
            }
            switch (cmd)
            {
            case 1: DeleteSelectedConsistUnits(hWnd); break;
            case 2: ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS); break;
            case 3: ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING); break;
            case 17: SendMessage(hWnd, WM_COMMANDBAR_ACTION, CMD_ACTION_STOCK_INFO, 0); break;
            case 18: SendMessage(hWnd, WM_COMMANDBAR_ACTION, CMD_ACTION_SHAPE_VIEWER, 0); break;
            case 19:
            {
                std::wstring targetPath = L"";
                int selConsistUnit = GetSelectedConsistUnitIndex();
                if (selConsistUnit >= 0 && selConsistUnit < (int)g_LoadedConsistUnits.size())
                {
                    const auto& u = g_LoadedConsistUnits[selConsistUnit];
                    targetPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
                }
                Show3DVisualStudioDialog(hWnd, targetPath, g_szBasePath);
                break;
            }
            case 20:
            {
                CopySelectedUnitNamesToClipboard(hWnd);
                break;
            }
            case 14:
            {
                if (g_ActiveTab == 1)
                {
                    if (g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
                    {
                        std::wstring targetKey = L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex);
                        ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::ReplaceBroken, { targetKey }, selIndices);
                    }
                }
                else if (!g_szCurrentConsistFile.empty())
                {
                    std::wstring currentFile = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
                    ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::ReplaceBroken, { currentFile }, selIndices);
                }
                break;
            }
            case 4: FlipSelectedConsistUnits(hWnd); break;
            case 16: CutSelectedConsistUnits(hWnd); break;
            case 5: CopySelectedConsistUnits(hWnd); break;
            case 6: PasteConsistUnits(hWnd, PASTE_AFTER_SELECTED); break;
            case 9: PasteConsistUnits(hWnd, PASTE_START); break;
            case 10: PasteConsistUnits(hWnd, PASTE_END); break;
            case 15:
            {
                if (g_ActiveTab == 1)
                {
                    if (g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
                    {
                        std::wstring targetKey = L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex);
                        ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::InsertUnits, { targetKey }, selIndices);
                    }
                }
                else if (!g_szCurrentConsistFile.empty())
                {
                    std::wstring currentFile = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
                    ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::InsertUnits, { currentFile }, selIndices);
                }
                break;
            }
            case 11: ShowClipboardContents(hWnd); break;
            case 12: ClearClipboard(hWnd); break;
            case 7: PerformUndo(hWnd); break;
            case 8: PerformRedo(hWnd); break;
            }
            return 0;
        }

        if (idCtrl == IDC_CONSISTLIST && pnmh->code == NM_RCELLCLICK)
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            int screenX = pCell->itemIndex;
            int screenY = pCell->subItemIndex;

            std::vector<int> selRows = g_ConsistList.GetCheckedIndices();
            if (selRows.empty())
            {
                selRows = g_ConsistList.GetSelectedIndices();
                if (selRows.empty())
                {
                    int single = g_ConsistList.GetSelectedIndex();
                    if (single >= 0) selRows.push_back(single);
                }
            }
            bool hasSelection = !selRows.empty();

            std::vector<ContextMenuItem> menuItems = {
                ContextMenuItem::Action(1, L"\xE790", L"Mutate Consist(s) from Pool Preset...", L"", hasSelection),
                ContextMenuItem::Action(2, L"\xE77F", L"Insert Units from Pool Preset...", L"", hasSelection),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(3, L"\xE8C8", L"Clone Consist", L"", hasSelection),
                ContextMenuItem::Action(4, L"\xE74D", L"Delete Consist(s)", L"Del", hasSelection),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(5, L"\xE74E", L"Save Consist(s)", L"Ctrl+S", hasSelection)
            };

            int cmd = ModernContextMenu::Show(hWnd, screenX, screenY, menuItems, TRUE);
            switch (cmd)
            {
            case 1:
            {
                std::vector<std::wstring> selFiles;
                if (g_ActiveTab == 1)
                {
                    for (int r : selRows)
                    {
                        std::wstring idxStr = g_ConsistList.GetCellText(r, 3);
                        if (!idxStr.empty())
                        {
                            selFiles.push_back(L"ACTIVITY:" + idxStr);
                        }
                    }
                }
                else
                {
                    for (int r : selRows)
                    {
                        std::wstring fn = g_ConsistList.GetCellText(r, 4);
                        if (!fn.empty())
                        {
                            std::wstring fp = EnsureConsistFilePath(g_szBasePath, fn);
                            if (!fp.empty()) selFiles.push_back(fp);
                        }
                    }
                }
                ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::MutateConsists, selFiles, {});
                break;
            }
            case 2:
            {
                std::vector<std::wstring> selFiles;
                if (g_ActiveTab == 1)
                {
                    for (int r : selRows)
                    {
                        std::wstring idxStr = g_ConsistList.GetCellText(r, 3);
                        if (!idxStr.empty())
                        {
                            selFiles.push_back(L"ACTIVITY:" + idxStr);
                        }
                    }
                }
                else
                {
                    for (int r : selRows)
                    {
                        std::wstring fn = g_ConsistList.GetCellText(r, 4);
                        if (!fn.empty())
                        {
                            std::wstring fp = EnsureConsistFilePath(g_szBasePath, fn);
                            if (!fp.empty()) selFiles.push_back(fp);
                        }
                    }
                }
                ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::InsertUnits, selFiles, {});
                break;
            }
            case 3: ActionCloneConsist(hWnd); break;
            case 4: ActionDeleteSelectedConsists(hWnd); break;
            case 5: SendMessage(hWnd, WM_COMMANDBAR_ACTION, CMD_ACTION_SAVE_CONSISTS, 0); break;
            }
            return 0;
        }

        if ((idCtrl == IDC_ASSETLIST || idCtrl == IDC_ASSETLIST2 || idCtrl == IDC_ASSETLIST3) && pnmh->code == NM_RCELLCLICK)
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            int screenX = pCell->itemIndex;
            int screenY = pCell->subItemIndex;

            std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary(pnmh->hwndFrom);
            bool hasStockSel = !selectedStock.empty();
            std::vector<int> selConsist = GetSelectedConsistUnitIndices();
            bool hasConsistSel = !selConsist.empty();

            std::wstring primaryTargetName = L"";
            for (int idx : selConsist)
            {
                if (idx >= 0 && idx < (int)g_LoadedConsistUnits.size())
                {
                    std::wstring name = g_LoadedConsistUnits[idx].uid;
                    if (primaryTargetName.empty()) primaryTargetName = name;
                }
            }

            std::wstring replaceAllText = L"Replace All Matching in Consist";
            if (!primaryTargetName.empty())
            {
                replaceAllText = L"Replace All '" + primaryTargetName + L"' with Stock";
            }

            bool canInsertUnits = !g_ClipboardUnits.empty();
            size_t clipCount = g_ClipboardUnits.size();
            std::wstring viewClipText = clipCount > 0 ? (L"View Clipboard (" + std::to_wstring(clipCount) + L" Units)...") : L"View Clipboard Contents...";

            std::vector<ContextMenuItem> menuItems = {
                ContextMenuItem::Action(5, L"\xE8C8", L"Copy Stock to Clipboard", L"Ctrl+C", hasStockSel),
                ContextMenuItem::Action(20, L"\xE8C8", L"Copy Unit Name(s)", L"Ctrl+Shift+C", hasStockSel),
                ContextMenuItem::Action(6, L"\xE77F", L"Insert Stock at End", L"Enter", hasStockSel),
                ContextMenuItem::Action(9, L"\xE77F", L"Insert Stock at Beginning", L"Ctrl+Shift+V", hasStockSel),
                ContextMenuItem::Action(13, L"\xE77F", L"Insert Stock After Selected", L"Ctrl+V", hasStockSel && hasConsistSel),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(2, L"\xE777", L"Replace Selected in Consist", L"Ctrl+R", hasStockSel && hasConsistSel),
                ContextMenuItem::Action(3, L"\xE8D7", replaceAllText, L"Ctrl+Shift+R", hasStockSel && hasConsistSel),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(11, L"\xE7B8", viewClipText, L"Alt+V", true),
                ContextMenuItem::Action(12, L"\xE75C", L"Clear Clipboard", L"", canInsertUnits),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(17, L"\xE946", L"Inspect Stock Specifications (Stock Info)...", L"", hasStockSel),
                ContextMenuItem::Action(19, L"\xE7F4", L"Inspect in 3D Visual Studio (Stock Inspector)...", L"", hasStockSel),
                ContextMenuItem::Action(18, L"\xE7B7", L"View 3D Model in Shape Viewer...", L"", hasStockSel)
            };

            int cmd = ModernContextMenu::Show(hWnd, screenX, screenY, menuItems, TRUE);
            switch (cmd)
            {
            case 5: CopySelectedStockUnits(hWnd); break;
            case 20: CopySelectedUnitNamesToClipboard(hWnd); break;
            case 6: PasteConsistUnits(hWnd, PASTE_END, &selectedStock); break;
            case 9: PasteConsistUnits(hWnd, PASTE_START, &selectedStock); break;
            case 13: PasteConsistUnits(hWnd, PASTE_AFTER_SELECTED, &selectedStock); break;
            case 2: ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS, &selectedStock); break;
            case 3: ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING, &selectedStock); break;
            case 11: ShowClipboardContents(hWnd); break;
            case 12: ClearClipboard(hWnd); break;
            case 17: SendMessage(hWnd, WM_COMMANDBAR_ACTION, CMD_ACTION_STOCK_INFO, 0); break;
            case 18: SendMessage(hWnd, WM_COMMANDBAR_ACTION, CMD_ACTION_SHAPE_VIEWER, 0); break;
            case 19:
            {
                std::wstring targetPath = L"";
                if (hasStockSel)
                {
                    const auto& u = selectedStock[0];
                    targetPath = BuildFullStockPath(g_szBasePath, u.parentDir, u.uid, u.isEngine ? L".eng" : L".wag");
                }
                Show3DVisualStudioDialog(hWnd, targetPath, g_szBasePath);
                break;
            }
            }
            return 0;
        }

        if ((idCtrl == IDC_ASSETLIST || idCtrl == IDC_ASSETLIST2 || idCtrl == IDC_ASSETLIST3) && (pnmh->code == NM_DBLCLK || pnmh->code == NM_RETURN))
        {
            std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary(pnmh->hwndFrom);
            if (!selectedStock.empty())
            {
                std::vector<int> selConsist = GetSelectedConsistUnitIndices();
                if (!selConsist.empty())
                {
                    PasteConsistUnits(hWnd, PASTE_AFTER_SELECTED, &selectedStock);
                }
                else
                {
                    PasteConsistUnits(hWnd, PASTE_END, &selectedStock);
                }
            }
            return 0;
        }

        LPNMHDR pnmhdr = (LPNMHDR)lParam;
        if (pnmhdr->hwndFrom == hTabControl && pnmhdr->code == TCN_SELCHANGE)
        {
            int newSel = TabCtrl_GetCurSel(hTabControl);
            if (newSel != g_ActiveTab)
            {
                g_ActiveTab = newSel;
                if (g_ActiveTab == 1) // Activity Consists Tab
                {
                    ResetConsistEditorWorkspace(hWnd);
                    PopulateRouteActivityTree();
                    g_ConsistList.Clear();
                    g_ConsistList.ClearColumns();
                    g_ConsistList.AddColumn(L"Name", 280, 0);
                    g_ConsistList.AddColumn(L"Units", 80, 0);
                    g_ConsistList.AddColumn(L"Status", 120, 0);
                    SetWindowTextW(g_hConsistHeader, L"  Activity Consists");
                    CommandBar_SetButtonText(g_hCommandBar, CMD_ACTION_SAVE_CONSISTS, L"Save Activity Consist(s)", 195);

                    // Keep Identifier read-only in Activity mode
                    if (g_hEditTrainCfgId)  SendMessage(g_hEditTrainCfgId,  EM_SETREADONLY, TRUE, 0);
                }
                else // Main Consists Tab (Tab 0)
                {
                    ResetConsistEditorWorkspace(hWnd);
                    g_ConsistList.Clear();
                    g_ConsistList.ClearColumns();
                    g_ConsistList.AddColumn(L"Name", 240, 0);
                    g_ConsistList.AddColumn(L"Units", 80, 0);
                    g_ConsistList.AddColumn(L"Status", 100, 0);
                    g_ConsistList.AddColumn(L"Modified", 160, 0);
                    PopulateConsistListFromCache();
                    CommandBar_SetButtonText(g_hCommandBar, CMD_ACTION_SAVE_CONSISTS, L"Save Consist(s)", 145);

                    // Restore editable state for Main Consists mode
                    if (g_hEditTrainCfgId)  SendMessage(g_hEditTrainCfgId,  EM_SETREADONLY, FALSE, 0);
                    if (g_hEditTrainName)   SendMessage(g_hEditTrainName,   EM_SETREADONLY, FALSE, 0);
                    if (g_hEditMaxVelocity) SendMessage(g_hEditMaxVelocity, EM_SETREADONLY, FALSE, 0);
                    if (g_hEditPerfFactor)  SendMessage(g_hEditPerfFactor,  EM_SETREADONLY, FALSE, 0);
                }

                if (g_hEditTrainCfgId)  InvalidateRect(g_hEditTrainCfgId,  NULL, TRUE);
                if (g_hEditTrainName)   InvalidateRect(g_hEditTrainName,   NULL, TRUE);
                if (g_hEditMaxVelocity) InvalidateRect(g_hEditMaxVelocity, NULL, TRUE);
                if (g_hEditPerfFactor)  InvalidateRect(g_hEditPerfFactor,  NULL, TRUE);

                RECT rc;
                GetClientRect(hWnd, &rc);
                SendMessageW(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
                InvalidateRect(hWnd, NULL, TRUE);
            }
            return 0;
        }
        else if (pnmhdr->code == NM_SETFOCUS)
        {
            if (pnmhdr->hwndFrom == g_hConsistList || pnmhdr->hwndFrom == g_hRouteTree)
            {
                SetActivePane(PANE_CONSIST, hWnd);
            }
            else if (pnmhdr->hwndFrom == g_hCategoryTree || pnmhdr->hwndFrom == g_hAssetList ||
                     pnmhdr->hwndFrom == g_hAssetList2 || pnmhdr->hwndFrom == g_hAssetList3)
            {
                SetActivePane(PANE_STOCK, hWnd);
            }
            else if (pnmhdr->hwndFrom == g_hEditorUnitList || pnmhdr->hwndFrom == g_hVisualConsistView ||
                     pnmhdr->hwndFrom == g_hUnitPreviewCard || pnmhdr->hwndFrom == g_hEditorPane ||
                     pnmhdr->hwndFrom == g_hWorkspaceHeader)
            {
                SetActivePane(PANE_WORKSPACE, hWnd);
            }
            return 0;
        }
        else if (pnmhdr->hwndFrom == g_hConsistList && (pnmhdr->code == NM_CLICK || pnmhdr->code == NM_CELLCLICK || pnmhdr->code == NM_RETURN || pnmhdr->code == NM_DBLCLK))
        {
            SetActivePane(PANE_CONSIST, hWnd);
            int sel = g_ConsistList.GetSelectedIndex();
            if (sel >= 0)
            {
                if (g_ActiveTab == 1) // Activity Consists Tab
                {
                    std::wstring strIdx = g_ConsistList.GetCellText(sel, 3);
                    if (!strIdx.empty())
                    {
                        int cIdx = _wtoi(strIdx.c_str());
                        LoadAndDisplayActivityConsist(hWnd, cIdx);
                    }
                }
                else // Main Consists Tab
                {
                    std::wstring filename = g_ConsistList.GetCellText(sel, 4);
                    if (!filename.empty())
                    {
                        LoadAndDisplayConsist(hWnd, filename);
                    }
                }
            }
            SyncPoolMutatorSelectionIfOpen();
            return 0;
        }
        else if (pnmhdr->hwndFrom == g_hEditorUnitList && (pnmhdr->code == NM_CELLCLICK || pnmhdr->code == NM_CLICK))
        {
            SetActivePane(PANE_WORKSPACE, hWnd);
            if (pnmhdr->code == NM_CELLCLICK)
            {
                NMCELLCLICK* pCellClick = (NMCELLCLICK*)lParam;
                if (pCellClick->subItemIndex == -1)
                {
                    if (pCellClick->itemIndex == -1)
                    {
                        // Header master checkbox clicked
                        int totalItems = g_EditorUnitList.GetItemCount();
                        int checkedCount = g_EditorUnitList.GetCheckedCount();
                        g_CheckedConsistUnits.clear();
                        if (checkedCount > 0)
                        {
                            for (int r = 0; r < totalItems; ++r)
                            {
                                if (g_EditorUnitList.IsItemChecked(r))
                                {
                                    std::wstring strNo = g_EditorUnitList.GetCellText(r, 0);
                                    int originalNo = _wtoi(strNo.c_str());
                                    int originalIndex = originalNo - 1;
                                    if (originalIndex >= 0 && originalIndex < (int)g_LoadedConsistUnits.size())
                                    {
                                        g_CheckedConsistUnits.insert(originalIndex);
                                    }
                                }
                            }
                        }
                        RefreshEditorUnitList(true);
                        return 0;
                    }
                    else // Row checkbox clicked
                    {
                        int clickedRow = pCellClick->itemIndex;
                        if (clickedRow >= 0 && clickedRow < g_EditorUnitList.GetItemCount())
                        {
                            std::wstring strNo = g_EditorUnitList.GetCellText(clickedRow, 0);
                            int originalNo = _wtoi(strNo.c_str());
                            int originalIndex = originalNo - 1;
                            if (originalIndex >= 0 && originalIndex < (int)g_LoadedConsistUnits.size())
                            {
                                if (g_EditorUnitList.IsItemChecked(clickedRow))
                                {
                                    g_CheckedConsistUnits.insert(originalIndex);
                                }
                                else
                                {
                                    g_CheckedConsistUnits.erase(originalIndex);
                                }
                                g_EditorUnitList.SetSelectedIndices({ clickedRow });
                                RefreshEditorUnitList(true);
                            }
                        }
                        return 0;
                    }
                }
                else if (pCellClick->subItemIndex == 4) // Orientation column (index 4)
                {
                    int clickedRow = pCellClick->itemIndex;
                    if (clickedRow >= 0)
                    {
                        g_EditorUnitList.SetSelectedIndices({ clickedRow });
                        std::wstring strNo = g_EditorUnitList.GetCellText(clickedRow, 0);
                        int originalNo = _wtoi(strNo.c_str());
                        int originalIndex = originalNo - 1;
                        if (originalIndex >= 0 && originalIndex < (int)g_LoadedConsistUnits.size())
                        {
                            PushUndoState(L"Toggle Orientation");
                            g_LoadedConsistUnits[originalIndex].isFlipped = !g_LoadedConsistUnits[originalIndex].isFlipped;
                            RefreshEditorUnitList(true);
                            if (g_hVisualConsistView)
                            {
                                VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
                            }
                            SaveCurrentConsist(hWnd);
                        }
                    }
                }
            }

            int sel = g_EditorUnitList.GetSelectedIndex();
            if (sel >= 0)
            {
                std::wstring strNo = g_EditorUnitList.GetCellText(sel, 0);
                int originalNo = _wtoi(strNo.c_str());
                int originalIndex = originalNo - 1;
                if (g_hVisualConsistView)
                {
                    VisualConsistView_SetSelected(g_hVisualConsistView, originalIndex);
                }
            }
            UpdateUnitPreviewFromSelected();
            SyncPoolMutatorSelectionIfOpen();
            return 0;
        }
        else if ((pnmhdr->hwndFrom == g_hAssetList || pnmhdr->hwndFrom == g_hAssetList2 || pnmhdr->hwndFrom == g_hAssetList3) &&
                 (pnmhdr->code == NM_CLICK || pnmhdr->code == NM_CELLCLICK || pnmhdr->code == NM_RETURN))
        {
            SetActivePane(PANE_STOCK, hWnd);
            UpdateUnitPreviewFromSelected();
            return 0;
        }
    }
    break;

        case WM_CLOSE:
    {
        SaveCurrentConsistSessionState();

        std::vector<std::wstring> dirtyMainFiles;
        for (const auto& kv : g_ConsistSessions)
        {
            if (kv.second.isDirty)
            {
                dirtyMainFiles.push_back(kv.first);
            }
        }

        std::vector<int> dirtyActivityIndices;
        for (size_t i = 0; i < g_CurrentActivityData.consists.size(); ++i)
        {
            if (g_CurrentActivityData.consists[i].isDirty)
            {
                dirtyActivityIndices.push_back((int)i);
            }
        }

        size_t totalDirty = dirtyMainFiles.size() + dirtyActivityIndices.size();

        if (totalDirty > 0)
        {
            std::wstring prompt = L"You have " + std::to_wstring(totalDirty) + L" consist" + (totalDirty > 1 ? L"s" : L"") + L" with unsaved changes:\r\n";
            prompt += L"────────────────────────────────────────────────────────\r\n";

            size_t listed = 0;
            for (const auto& fname : dirtyMainFiles)
            {
                if (listed < 8)
                {
                    prompt += L"  • " + fname + L"\r\n";
                    listed++;
                }
            }
            std::wstring actFileNameOnly = L"Activity";
            if (!g_CurrentActivityFilePath.empty())
            {
                size_t slash = g_CurrentActivityFilePath.find_last_of(L"\\/");
                std::wstring fname = (slash != std::wstring::npos) ? g_CurrentActivityFilePath.substr(slash + 1) : g_CurrentActivityFilePath;
                size_t dot = fname.find_last_of(L'.');
                if (dot != std::wstring::npos)
                {
                    actFileNameOnly = fname.substr(0, dot);
                }
                else
                {
                    actFileNameOnly = fname;
                }
            }

            for (int actIdx : dirtyActivityIndices)
            {
                if (listed < 8)
                {
                    prompt += L"  • " + actFileNameOnly + L" - " + g_CurrentActivityData.consists[actIdx].name + L"\r\n";
                    listed++;
                }
            }
            if (totalDirty > 8)
            {
                prompt += L"  • ... and " + std::to_wstring(totalDirty - 8) + L" more items\r\n";
            }
            prompt += L"\r\nDo you want to save all changes before exiting?";

            std::vector<ModernMsgBoxCustomButton> buttons = {
                { 101, L"Save All & Exit", true },
                { 102, L"Discard & Exit", false },
                { IDCANCEL, L"Cancel", false }
            };

            int res = ShowModernMessageBoxEx(hWnd, prompt.c_str(), L"Unsaved Changes", buttons, MB_ICONWARNING);
            if (res == 101) // Save All & Exit
            {
                for (const auto& fname : dirtyMainFiles)
                {
                    SaveConsistSessionToDisk(hWnd, fname);
                }
                for (int actIdx : dirtyActivityIndices)
                {
                    SaveActivityConsistByIndex(hWnd, actIdx, -1);
                }
                DestroyWindow(hWnd);
                return 0;
            }
            else if (res == 102) // Discard & Exit
            {
                DestroyWindow(hWnd);
                return 0;
            }
            else // Cancel (return back to editor)
            {
                return 0;
            }
        }

        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY:
        g_bCancelScan = TRUE;
        CancelStockScan(g_hStockScanThread);
        if (g_hScanThread != NULL)
        {
            WaitForSingleObject(g_hScanThread, 200);
            CloseHandle(g_hScanThread);
            g_hScanThread = NULL;
        }
        DeleteCriticalSection(&g_StockCacheCS);

        // Cancel Consists Directory Watcher Thread
        g_bCancelWatcher = TRUE;
        if (g_hDirHandle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(g_hDirHandle, NULL);
        }
        if (g_hWatcherThread != NULL)
        {
            WaitForSingleObject(g_hWatcherThread, 200);
            CloseHandle(g_hWatcherThread);
            g_hWatcherThread = NULL;
        }

        LOG_INFO("Train Sim Consist Builder session terminated gracefully.");
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK TabSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return TRUE;

    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        TCHITTESTINFO hti = { 0 };
        hti.pt = pt;
        int clickedIdx = TabCtrl_HitTest(hWnd, &hti);
        if (clickedIdx >= 0)
        {
            TabCtrl_SetCurSel(hWnd, clickedIdx);
            InvalidateRect(hWnd, NULL, FALSE);
            NMHDR nmhdr = { 0 };
            nmhdr.hwndFrom = hWnd;
            nmhdr.idFrom = (UINT_PTR)GetWindowLongPtrW(hWnd, GWLP_ID);
            nmhdr.code = TCN_SELCHANGE;
            SendMessageW(GetParent(hWnd), WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int oldHover = (int)(INT_PTR)GetPropW(hWnd, L"HoverIndex");
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        TCHITTESTINFO hti = { 0 };
        hti.pt = pt;
        int newHover = TabCtrl_HitTest(hWnd, &hti);

        if (newHover != oldHover)
        {
            SetPropW(hWnd, L"HoverIndex", (HANDLE)(INT_PTR)newHover);
            InvalidateRect(hWnd, NULL, FALSE);

            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        SetPropW(hWnd, L"HoverIndex", (HANDLE)(INT_PTR)-1);
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Double buffer DC
        HDC hmemDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP holdBm = (HBITMAP)SelectObject(hmemDC, hbm);

        // In Dark Mode fill with black to reveal DWM Mica
        COLORREF clrTabBg = RGB(0, 0, 0);
        HBRUSH hbrTabBg = CreateSolidBrush(clrTabBg);
        FillRect(hmemDC, &rcClient, hbrTabBg);
        DeleteObject(hbrTabBg);

        int itemCount = TabCtrl_GetItemCount(hWnd);
        int curSel = TabCtrl_GetCurSel(hWnd);
        int hoverIdx = (int)(INT_PTR)GetPropW(hWnd, L"HoverIndex");

        SelectObject(hmemDC, hUIFont ? hUIFont : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(hmemDC, TRANSPARENT);

        const int r_b = 6;  // Radius for concave bottom fillets
        const int r_t = 8;  // Radius for convex top corners
        const int baselineY = rcClient.bottom - 1;

        // Colors
        COLORREF clrActiveSurface = UITheme::DarkHeaderBackground;
        COLORREF clrHoverSurface = RGB(38, 38, 38);
        COLORREF clrBorder = RGB(55, 55, 55);

        // 1. Draw Inactive Tabs (Floating text, with hover background/border outline if hovered)
        for (int i = 0; i < itemCount; ++i)
        {
            if (i == curSel) continue;

            RECT rcItem;
            TabCtrl_GetItemRect(hWnd, i, &rcItem);

            WCHAR szText[64] = { 0 };
            TCITEM tie = { 0 };
            tie.mask = TCIF_TEXT;
            tie.pszText = szText;
            tie.cchTextMax = 64;
            TabCtrl_GetItem(hWnd, i, &tie);

            if (i == hoverIdx)
            {
                // Draw a subtle curved background for hover
                int tabLeft = rcItem.left + r_b;
                int tabRight = rcItem.right - r_b;
                int tabTop = rcItem.top + 3;
                int tabBottom = baselineY;

                HBRUSH hbrHover = CreateSolidBrush(clrHoverSurface);
                HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                HBRUSH hOldBr = (HBRUSH)SelectObject(hmemDC, hbrHover);
                HPEN hOldPen = (HPEN)SelectObject(hmemDC, hNullPen);

                BeginPath(hmemDC);
                MoveToEx(hmemDC, tabLeft - r_b, baselineY, NULL);
                AngleArc(hmemDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
                AngleArc(hmemDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
                LineTo(hmemDC, tabRight - r_t, tabTop);
                AngleArc(hmemDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
                AngleArc(hmemDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);
                LineTo(hmemDC, tabRight + r_b, baselineY);
                CloseFigure(hmemDC);
                EndPath(hmemDC);
                FillPath(hmemDC);

                SelectObject(hmemDC, hOldBr);
                SelectObject(hmemDC, hOldPen);
                DeleteObject(hbrHover);
                DeleteObject(hNullPen);

                // Draw a subtle outline for hover
                HPEN hHoverPen = CreatePen(PS_SOLID, 1, clrBorder);
                HPEN hOldPenForHover = (HPEN)SelectObject(hmemDC, hHoverPen);

                MoveToEx(hmemDC, tabLeft - r_b, baselineY, NULL);
                AngleArc(hmemDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
                AngleArc(hmemDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
                LineTo(hmemDC, tabRight - r_t, tabTop);
                AngleArc(hmemDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
                AngleArc(hmemDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);

                SelectObject(hmemDC, hOldPenForHover);
                DeleteObject(hHoverPen);
            }

            COLORREF clrInactiveText = RGB(180, 180, 180);
            SetTextColor(hmemDC, clrInactiveText);

            RECT rcText = rcItem;
            rcText.top += 2;
            DrawTextW(hmemDC, szText, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // 2. Render Active Tab (Seamless surface merging into client panel, with thin border outline)
        if (curSel >= 0 && curSel < itemCount)
        {
            RECT rcItem;
            TabCtrl_GetItemRect(hWnd, curSel, &rcItem);

            int tabLeft = rcItem.left + r_b;
            int tabRight = rcItem.right - r_b;
            int tabTop = rcItem.top + 3;
            int tabBottom = baselineY;

            // A. Fill active surface (seamlessly extending down to baseline)
            HBRUSH hbrSurface = CreateSolidBrush(clrActiveSurface);
            HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hmemDC, hbrSurface);
            HPEN hOldPen = (HPEN)SelectObject(hmemDC, hNullPen);

            BeginPath(hmemDC);
            MoveToEx(hmemDC, 0, baselineY, NULL);
            LineTo(hmemDC, tabLeft - r_b, baselineY);

            // Left fillet
            AngleArc(hmemDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
            // Top-left corner
            AngleArc(hmemDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
            // Top line
            LineTo(hmemDC, tabRight - r_t, tabTop);
            // Top-right corner
            AngleArc(hmemDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
            // Right fillet
            AngleArc(hmemDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);

            // Close down to baseline
            LineTo(hmemDC, rcClient.right, baselineY);
            LineTo(hmemDC, rcClient.right, rcClient.bottom);
            LineTo(hmemDC, 0, rcClient.bottom);
            CloseFigure(hmemDC);
            EndPath(hmemDC);
            FillPath(hmemDC);

            SelectObject(hmemDC, hOldBr);
            SelectObject(hmemDC, hOldPen);
            DeleteObject(hbrSurface);
            DeleteObject(hNullPen);

            // B. Draw active tab border outline & baseline (continuous across whole tab width)
            HPEN hBorderPen = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOldPenForBorder = (HPEN)SelectObject(hmemDC, hBorderPen);

            // Line from left of screen to active tab left fillet
            MoveToEx(hmemDC, 0, baselineY, NULL);
            LineTo(hmemDC, tabLeft - r_b, baselineY);

            // Left fillet arc
            AngleArc(hmemDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
            // Top-left corner arc
            AngleArc(hmemDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
            // Top edge line
            LineTo(hmemDC, tabRight - r_t, tabTop);
            // Top-right corner arc
            AngleArc(hmemDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
            // Right fillet arc
            AngleArc(hmemDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);

            // Line from active tab right fillet to right end of screen
            LineTo(hmemDC, rcClient.right, baselineY);

            SelectObject(hmemDC, hOldPenForBorder);
            DeleteObject(hBorderPen);

            // C. Draw text label
            WCHAR szText[64] = { 0 };
            TCITEM tie = { 0 };
            tie.mask = TCIF_TEXT;
            tie.pszText = szText;
            tie.cchTextMax = 64;
            TabCtrl_GetItem(hWnd, curSel, &tie);

            COLORREF clrActiveText = RGB(255, 255, 255);
            SetTextColor(hmemDC, clrActiveText);

            RECT rcText = rcItem;
            rcText.top += 2;
            DrawTextW(hmemDC, szText, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hmemDC, 0, 0, SRCCOPY);

        SelectObject(hmemDC, holdBm);
        DeleteObject(hbm);
        DeleteDC(hmemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
        RemovePropW(hWnd, L"HoverIndex");
        RemoveWindowSubclass(hWnd, TabSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

std::wstring AssetListGetCellText(int itemIndex, int subItemIndex, void* pParam)
{
    int paneIdx = (int)(intptr_t)pParam;
    if (paneIdx < 0 || paneIdx >= 3) paneIdx = 0;

    std::wstring text = L"";
    EnterCriticalSection(&g_StockCacheCS);
    if (itemIndex >= 0 && itemIndex < (int)g_FilteredStockIndicesPane[paneIdx].size())
    {
        size_t globalIdx = g_FilteredStockIndicesPane[paneIdx][itemIndex];
        if (globalIdx < g_StockCache.size())
        {
            const StockItem& item = g_StockCache[globalIdx];
            if (subItemIndex == 0) text = item.szFileName;
            else if (subItemIndex == 1) text = item.szCategory;
            else if (subItemIndex == 2) text = item.szFolder;
        }
    }
    LeaveCriticalSection(&g_StockCacheCS);
    return text;
}