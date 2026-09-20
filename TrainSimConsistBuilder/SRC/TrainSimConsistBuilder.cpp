#include <unordered_map>
enum ActivePane {
    PANE_CONSIST,
    PANE_STOCK
};
ActivePane g_ActivePane = PANE_CONSIST;

#include "framework.h"
#include "TrainSimConsistBuilder.h"
#include "../UI/AddressBar.h"
#include "../UI/UITheme.h"
#include "../UI/CustomTreeView.h"
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <windowsx.h>
#include <shlwapi.h>
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
HFONT hUIFont = NULL;
std::vector<size_t> g_FilteredStockIndices;

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
#include "../UI/BatchConsistGeneratorDlg.h"
#include "../UI/PoolMutatorDlg.h"
#include "PoolMutator.h"
#include "../UI/CustomTitleBar.h"

LRESULT CALLBACK TabSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
#include "../UI/CustomListControl.h"
#include "ConsistReader.h"
#include "ConsistWriter.h"
#include "ActivityConsistReader.h"
#include "ActivityConsistWriter.h"
#include "Updater.h"
CustomListControl g_ConsistList;
CustomListControl g_AssetList;
CustomListControl g_EditorUnitList;
CustomTreeView g_RouteTreeView;
CustomTreeView g_CategoryTreeView;

static ActivityConsistReader::ActivityData g_CurrentActivityData;
static std::wstring g_CurrentActivityFilePath;
static int g_CurrentActivityConsistIndex = -1;

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


std::wstring AssetListGetCellText(int itemIndex, int subItemIndex, void* pParam);
static void RefreshEditorUnitList(bool preserveSelection = true);
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

BOOL g_bAutoSave = FALSE;
std::wstring g_szConsistSearchQuery = L"";
std::wstring g_szStockSearchQuery = L"";
std::wstring g_szBasePath = L"";

static bool IsUnitBrokenOnDisk(const ConsistReader::UnitInfo& unit, const std::wstring& basePath)
{
    if (unit.uid.empty() || unit.parentDir.empty()) return true;
    std::wstring ext = unit.isEngine ? L".eng" : L".wag";
    std::wstring unitPath = basePath;
    if (!unitPath.empty() && unitPath.back() != L'\\') unitPath += L'\\';
    unitPath += L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + ext;
    DWORD attr = GetFileAttributesW(unitPath.c_str());
    return (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static std::wstring EvaluateAndUpdateConsistStatus(
    const std::wstring& consistKey,
    const std::vector<ConsistReader::UnitInfo>& units,
    bool isSavedToDisk = false)
{
    bool isBroken = false;
    for (const auto& u : units)
    {
        if (IsUnitBrokenOnDisk(u, g_szBasePath))
        {
            isBroken = true;
            break;
        }
    }

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
    if (isBroken || (!consistKey.empty() && g_InitiallyBrokenConsists.count(consistKey) > 0 && !isSavedToDisk && g_SessionFixedConsists.count(consistKey) == 0))
        return L"Broken";

    return L"Healthy";
}

HWND g_hSplitter1 = NULL;
HWND g_hSplitter2 = NULL;
HWND g_hSplitter3 = NULL;
HWND g_hSplitter3Top = NULL;
int g_wConsist = 600;       // Left Pane Total Width
int g_hConsistSplit = 380;  // Top Deck (Consists List) Height
int g_wCategorySplit = 130; // Default width of Category Tree panel (smaller default)
HWND g_hRouteTree = NULL;    // Routes & Activities TreeView (Top Deck on Activity Tab)
int g_ActiveTab = 0;

enum SplitterType {
    SPLITTER_VERTICAL_MAIN = 1, // Splitter 1: Left deck vs Right Workspace (IDC_SIZEWE)
    SPLITTER_HORIZONTAL = 2,    // Splitter 2: Consist deck vs Stock deck (IDC_SIZENS)
    SPLITTER_VERTICAL_SUB = 3   // Splitter 3: TreeView vs ListView (IDC_SIZEWE)
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
        if (type == SPLITTER_HORIZONTAL)
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

            // Trigger parent WM_SIZE recalculation and window refresh
            SendMessage(hParent, WM_SIZE, 0, MAKELPARAM(width, height));
            InvalidateRect(hParent, NULL, TRUE);
            if (g_hCategoryTree != NULL) InvalidateRect(g_hCategoryTree, NULL, TRUE);
            if (g_hConsistList != NULL) g_ConsistList.Invalidate();
            if (g_hAssetList != NULL) g_AssetList.Invalidate();
            UpdateWindow(hParent);
        }
        else
        {
            if (type == SPLITTER_HORIZONTAL)
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

        if (type == SPLITTER_HORIZONTAL)
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
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = SplitterWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_SIZEWE);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"TSCBSplitter";
    RegisterClassExW(&wcex);
}

enum DragState {
    DRAG_NONE,
    DRAG_SPLIT1, // Main vertical split between Left Pane and Right Workspace
    DRAG_SPLIT2, // Horizontal split between Consists (top) and Stocks (bottom)
    DRAG_SPLIT3  // Vertical split between Category Tree and Asset List in bottom deck
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
            g_ConsistList.AddColumn(L"Name", 280, 0);
            g_ConsistList.AddColumn(L"Units", 80, 0);
            g_ConsistList.AddColumn(L"Status", 120, 0);
            SetWindowTextW(g_hConsistHeader, L"  Activity Consists");
            CommandBar_SetButtonText(g_hCommandBar, CMD_ACTION_SAVE_CONSISTS, L"Save Activity Consist(s)", 195);

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

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_bDarkMode = TRUE;
    g_hbrDarkBackground = CreateSolidBrush(UITheme::DarkBackground);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    hInst = hInstance;
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
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

    return (int)msg.wParam;
}

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

    ShowWindow(hWnd, nCmdShow);
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

    // Auto-expand root category nodes
    g_CategoryTreeView.ExpandNode(g_pNodeEngines, true);
    g_CategoryTreeView.ExpandNode(g_pNodeWagons, true);
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
}

#define WM_ADD_CONSIST_ITEM (WM_USER + 301)
#define WM_CONSIST_SCAN_COMPLETE (WM_USER + 304)
#define WM_REQUEST_DEBOUNCE_RESCAN (WM_USER + 305)
#define TIMER_DEBOUNCE_RESCAN 401

struct ScannedConsist {
    std::wstring szFileName;
    std::wstring szName;
    int nUnits = 0;
    std::wstring szLastModified;
    bool isBroken = false;
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
        bool isFixedInSession = (!item.isBroken && g_SessionFixedConsists.count(item.szFileName) > 0);
        std::wstring statusStr = item.isBroken ? L"Broken" : (isFixedInSession ? L"Fixed" : L"Healthy");
        
        std::wstring displayName = item.szName;
        auto itSess = g_ConsistSessions.find(item.szFileName);
        if (itSess != g_ConsistSessions.end() && itSess->second.isDirty)
        {
            displayName = L"● " + displayName;
        }

        g_ConsistList.AddItem({ displayName, std::to_wstring(item.nUnits), statusStr, item.szLastModified, item.szFileName });
        totalConsists++;
        if (item.isBroken) brokenConsists++;
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

bool StringContainsIgnoreCase(const std::wstring& str, const std::wstring& search)
{
    if (search.empty()) return true;
    std::wstring strLower = str;
    std::wstring searchLower = search;
    for (wchar_t& c : strLower) c = towlower(c);
    for (wchar_t& c : searchLower) c = towlower(c);
    return strLower.find(searchLower) != std::wstring::npos;
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
        if (con.isBroken) brokenConsists++;
        totalConsists++;

        // Filter 0: Name (Letter filter)
        if (!MatchLetterFilter(con.name, nameFilters))
            continue;

        // Filter 1: Units count
        if (!MatchUnitsFilter(con.totalUnits, unitsFilters))
            continue;

        // Filter 2: Status (Healthy, Broken, Fixed)
        std::wstring consistKey = g_CurrentActivityFilePath + L"#" + con.id;
        bool isFixed = (!con.isBroken && g_SessionFixedConsists.count(consistKey) > 0);
        std::wstring statusStr = con.isBroken ? L"Broken" : (isFixed ? L"Fixed" : L"Healthy");

        if (!statusFilters.empty())
        {
            if (std::find(statusFilters.begin(), statusFilters.end(), statusStr) == statusFilters.end())
                continue;
        }

        std::wstring displayName = con.name;
        auto itSess = g_ConsistSessions.find(consistKey);
        if (con.isDirty || (itSess != g_ConsistSessions.end() && itSess->second.isDirty))
        {
            displayName = L"● " + displayName;
        }

        std::wstring idxStr = std::to_wstring(i);
        g_ConsistList.AddItem({ displayName, std::to_wstring(con.totalUnits), statusStr, idxStr });
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

                // Health check: verify all engines/wagons exist on disk
                bool isBroken = false;
                try
                {
                    ConsistReader::ConsistData conData = ConsistReader::LoadConsist(fileFullPath);
                    for (const auto& unit : conData.units)
                    {
                        if (unit.uid.empty() || unit.parentDir.empty())
                        {
                            isBroken = true;
                            break;
                        }
                        std::wstring ext = unit.isEngine ? L".eng" : L".wag";
                        std::wstring unitPath = basePath + L"TRAINS\\TRAINSET\\" + unit.parentDir + L"\\" + unit.uid + ext;
                        
                        DWORD attr = GetFileAttributesW(unitPath.c_str());
                        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            isBroken = true;
                            break;
                        }
                    }
                }
                catch (...)
                {
                    isBroken = true;
                }

                // Check Column Header Filters
                if (!MatchLetterFilter(szConsistName, g_ConsistList.GetActiveFilters(0)))
                {
                    continue;
                }
                if (!MatchUnitsFilter(nUnits, g_ConsistList.GetActiveFilters(1)))
                {
                    continue;
                }
                // Check Status filter (column 2)
                bool isFixedInSession = (!isBroken && g_SessionFixedConsists.count(ffd.cFileName) > 0);
                std::wstring statusStr = isBroken ? L"Broken" : (isFixedInSession ? L"Fixed" : L"Healthy");
                const auto& statusFilters = g_ConsistList.GetActiveFilters(2);
                if (!statusFilters.empty())
                {
                    bool match = false;
                    for (const auto& f : statusFilters)
                    {
                        if (f == statusStr) match = true;
                    }
                    if (!match) continue;
                }
                if (!MatchModifiedFilter(ffd.ftLastWriteTime, g_ConsistList.GetActiveFilters(3)))
                {
                    continue;
                }

                ScannedConsist* pConsist = new ScannedConsist();
                pConsist->szFileName = ffd.cFileName;
                pConsist->szName = szConsistName;
                pConsist->nUnits = nUnits;
                pConsist->szLastModified = FormatFileTimeFriendly(ffd.ftLastWriteTime);
                pConsist->isBroken = isBroken;
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
    if (g_hVisualConsistView) ShowWindow(g_hVisualConsistView, SW_HIDE);

    // Clear active loaded consist state
    g_LoadedConsistUnits.clear();
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

    ResetConsistEditorWorkspace(hWnd);


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
    if (fileName.empty() || basePath.empty()) return L"";
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
//   • Hover  : slightly brighter border
//   • Focused: 2-px accent-colour bottom line (blue / teal)
// ---------------------------------------------------------------------------
static LRESULT CALLBACK ModernEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
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
        if (isReadOnly)
        {
            HideCaret(hWnd);
        }
        break;
    }

    case WM_KILLFOCUS:
        if (g_hFocusedEdit == hWnd) g_hFocusedEdit = NULL;
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;

    case WM_SETCURSOR:
    {
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
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (isReadOnly)
        {
            LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HideCaret(hWnd);
            return lr;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
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

    case WM_PAINT:
    {
        HDC hdc = GetDC(hWnd);
        if (hdc)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            bool focused = (g_hFocusedEdit == hWnd);
            bool hovered = (g_hHoveredEdit == hWnd);
            
            // 1. Paint the parent workspace background color to clear the sharp corner regions
            HBRUSH hbrParent = CreateSolidBrush(UITheme::DarkBackground);
            FillRect(hdc, &rc, hbrParent);
            DeleteObject(hbrParent);
            
            // 2. Select edit box background color based on active state (Win11 Explorer spec)
            COLORREF clrBg = RGB(38, 38, 38);
            if (focused)      clrBg = RGB(30, 30, 30);
            else if (hovered) clrBg = RGB(45, 45, 45);
            
            HBRUSH hbrBg = CreateSolidBrush(clrBg);
            
            // 3. Select border color (unfocused matches background -> borderless look)
            COLORREF clrBorder = RGB(38, 38, 38);
            if (focused)      clrBorder = RGB(70, 70, 70);
            else if (hovered) clrBorder = RGB(55, 55, 55);
            
            HPEN hPen = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOld = (HPEN)SelectObject(hdc, hPen);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hbrBg);
            
            // Draw custom filled rounded rectangle background
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            
            SelectObject(hdc, hOld);
            SelectObject(hdc, hOldBr);
            DeleteObject(hPen);
            DeleteObject(hbrBg);
            
            ReleaseDC(hWnd, hdc);
        }
        
        // 4. Let the default EDIT control paint its centered text transparently on top of our rounded shape
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        
        // 5. Draw focused accent blue bottom line (inset slightly for rounded corners, only when editable)
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (g_hFocusedEdit == hWnd && !isReadOnly)
        {
            HDC hdcText = GetDC(hWnd);
            if (hdcText)
            {
                RECT rc;
                GetClientRect(hWnd, &rc);
                HPEN hAccent = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
                HPEN hOld = (HPEN)SelectObject(hdcText, hAccent);
                MoveToEx(hdcText, rc.left + 4, rc.bottom - 2, NULL);
                LineTo(hdcText, rc.right - 4, rc.bottom - 2);
                SelectObject(hdcText, hOld);
                DeleteObject(hAccent);
                ReleaseDC(hWnd, hdcText);
            }
        }
        
        return lr;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, ModernEditSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// SectionUnitsHeaderSubclassProc – Fluent Card Header Bar for Consist Units
static LRESULT CALLBACK SectionUnitsHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
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

        HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
        if (!hFont) hFont = hUIFont;
        HFONT hOldFont = NULL;
        if (hFont) hOldFont = (HFONT)SelectObject(hMemDC, hFont);

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(240, 240, 240));

        RECT rcText = { 12, 0, rc.right - 12, rc.bottom };
        DrawTextW(hMemDC, szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK StockHeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_NCHITTEST)
    {
        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Top 6 pixels for horizontal Splitter 2, rightmost 6 pixels for vertical Splitter 1
        if (pt.y <= 6 || pt.x >= rcClient.right - 6)
        {
            return HTTRANSPARENT;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SortAssetGrid()
{
    int sortCol = g_AssetList.GetSortColumn();
    if (sortCol < 0) return;
    bool ascending = g_AssetList.IsSortAscending();

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
    std::stable_sort(g_FilteredStockIndices.begin(), g_FilteredStockIndices.end(), [&](size_t idxA, size_t idxB) {
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
            // If categories are same, sub-sort by Name
            int cmp = StrCmpLogicalW(itemA.szFileName.c_str(), itemB.szFileName.c_str());
            if (cmp == 0) return false;
            return ascending ? (cmp < 0) : (cmp > 0);
        }
        else if (sortCol == 2) // Folder
        {
            int cmp = StrCmpLogicalW(itemA.szFolder.c_str(), itemB.szFolder.c_str());
            if (cmp == 0)
            {
                // Sub-sort by Name
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

void PopulateAssetGrid(CustomTreeNode* hSelected = nullptr)
{
    if (!g_hAssetList) return;
    
    if (!hSelected && g_hCategoryTree)
    {
        hSelected = g_CategoryTreeView.GetSelectedNode();
    }

    g_FilteredStockIndices.clear();
    if (!hSelected)
    {
        g_AssetList.SetItemCount(0);
        return;
    }

    EnterCriticalSection(&g_StockCacheCS);
    for (size_t i = 0; i < g_StockCache.size(); ++i)
    {
        const auto& item = g_StockCache[i];
        // 1. Category Matching
        BOOL isMatch = FALSE;
        if (hSelected == g_pNodeEnginesAll && item.szExtension == L".eng")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeWagonsAll && item.szExtension == L".wag")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeDiesel && item.szCategory == L"Diesel")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeElectric && item.szCategory == L"Electric")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeSteam && item.szCategory == L"Steam")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeControl && item.szCategory == L"Control")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodePassenger && item.szCategory == L"Passenger")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeFreight && item.szCategory == L"Freight")
        {
            isMatch = TRUE;
        }
        else if (hSelected == g_pNodeTender && item.szCategory == L"Tender")
        {
            isMatch = TRUE;
        }

        if (!isMatch) continue;

        // 2. Search Query Filtering (checks filename and folder)
        if (!g_szStockSearchQuery.empty())
        {
            if (!StringContainsIgnoreCase(item.szFileName, g_szStockSearchQuery) &&
                !StringContainsIgnoreCase(item.szFolder, g_szStockSearchQuery))
            {
                continue;
            }
        }

        // 3. Column Header Filters
        if (!MatchLetterFilter(item.szFileName, g_AssetList.GetActiveFilters(0)))
        {
            continue;
        }
        const auto& typeFilters = g_AssetList.GetActiveFilters(1);
        if (!typeFilters.empty())
        {
            if (std::find(typeFilters.begin(), typeFilters.end(), item.szCategory) == typeFilters.end())
            {
                continue;
            }
        }
        if (!MatchLetterFilter(item.szFolder, g_AssetList.GetActiveFilters(2)))
        {
            continue;
        }

        g_FilteredStockIndices.push_back(i);
    }
    LeaveCriticalSection(&g_StockCacheCS);

    SortAssetGrid();

    g_AssetList.SetItemCount((int)g_FilteredStockIndices.size());
    g_AssetList.Invalidate();

    // Auto-stretch last column to fit
    if (g_hAssetList != NULL)
    {
        int wList = g_AssetList.GetUsableWidth();
        int wCol0 = g_AssetList.GetColumnWidth(0);
        int wCol1 = g_AssetList.GetColumnWidth(1);
        int wLast = wList - (wCol0 + wCol1);
        if (wLast < 50) wLast = 50;
        g_AssetList.SetColumnWidth(2, wLast);
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
            TriggerConsistsRescan(GetAncestor(hWndList, GA_ROOT));
        }
    }
    else if (hWndList == g_hAssetList)
    {
        g_AssetList.SetActiveFilters(colIndex, checkedOptions);
        CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
        PopulateAssetGrid(hSelected);
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
            else if (colIndex == 2) allOptions = { L"Healthy", L"Broken", L"Fixed" };
        }
        else
        {
            if (colIndex == 0) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
            else if (colIndex == 1) allOptions = { L"1-16", L"17-26", L"27-36", L"37-46", L"47-56", L"57-60+" };
            else if (colIndex == 2) allOptions = { L"Healthy", L"Broken", L"Fixed" };
            else if (colIndex == 3) allOptions = { L"Today", L"Yesterday", L"Last week", L"Earlier this month", L"Last month", L"A long time ago" };
        }
    }
    else if (hWndList == g_hAssetList)
    {
        if (colIndex == 0) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
        else if (colIndex == 1)
        {
            if (g_hCategoryTree)
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
        else if (colIndex == 3) allOptions = { L"Healthy", L"Broken", L"Fixed" };
        else if (colIndex == 4) allOptions = { L"Normal", L"Flipped" };
        else if (colIndex == 5) allOptions = { L"0-9", L"A-H", L"I-P", L"Q-Z", L"Other" };
    }

    if (allOptions.empty()) return;

    POINT pt;
    GetCursorPos(&pt);
    int x = pt.x - 180;
    int y = pt.y + 10;

    CustomListControl& listCtrl = (hWndList == g_hConsistList) ? g_ConsistList : 
                                  ((hWndList == g_hAssetList) ? g_AssetList : g_EditorUnitList);
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
    std::vector<int> selRows = g_EditorUnitList.GetSelectedIndices();
    std::vector<int> unitIndices;
    unitIndices.reserve(selRows.size());
    for (int row : selRows)
    {
        if (row >= 0 && row < g_EditorUnitList.GetItemCount())
        {
            std::wstring noStr = g_EditorUnitList.GetCellText(row, 0);
            int originalNo = _wtoi(noStr.c_str());
            int unitIdx = originalNo - 1;
            if (unitIdx >= 0 && unitIdx < (int)g_LoadedConsistUnits.size())
            {
                unitIndices.push_back(unitIdx);
            }
        }
    }
    return unitIndices;
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

static void RefreshEditorUnitList(bool preserveSelection)
{
    int savedScrollY = preserveSelection ? g_EditorUnitList.GetScrollY() : 0;
    std::vector<int> savedSelectedUnits = preserveSelection ? GetSelectedConsistUnitIndices() : std::vector<int>();
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
        bool isUnitBroken = IsUnitBrokenOnDisk(unit, g_szBasePath);
        bool isFixedInSession = false;
        std::wstring activeConsistKey = GetCurrentActiveConsistKey();
        if (!activeConsistKey.empty())
        {
            auto it = g_SessionFixedUnitsPerConsist.find(activeConsistKey);
            if (it != g_SessionFixedUnitsPerConsist.end())
            {
                isFixedInSession = (!isUnitBroken && it->second.count((int)i) > 0);
            }
        }
        std::wstring unitStatus = isUnitBroken ? L"Broken" : (isFixedInSession ? L"Fixed" : L"Healthy");
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

bool ApplyPoolMutationToSessions(
    HWND hWnd,
    const std::vector<std::wstring>& targetConsistPaths,
    const std::vector<int>& targetUnitIndices,
    const PoolMutator::MutatorOptions& options,
    PoolMutator::MutatorResult& outResult)
{
    outResult.success = false;
    outResult.processedCount = 0;
    outResult.affectedFiles.clear();
    outResult.errorMessage.clear();

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
        g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    else if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
    {
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].isDirty = true;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].units = g_LoadedConsistUnits;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].totalUnits = (int)g_LoadedConsistUnits.size();
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            std::wstring curName = g_ConsistList.GetCellText(sel, 0);
            if (curName.rfind(L"● ", 0) != 0)
            {
                g_ConsistList.SetCellText(sel, 0, L"● " + curName);
                g_ConsistList.SetCellText(sel, 1, std::to_wstring(g_LoadedConsistUnits.size()));
                g_ConsistList.Invalidate();
            }
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
        g_ConsistSessions[g_szCurrentConsistFile].isDirty = true;
        UpdateConsistManagerRow(g_szCurrentConsistFile);
    }
    else if (g_ActiveTab == 1 && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
    {
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].isDirty = true;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].units = g_LoadedConsistUnits;
        g_CurrentActivityData.consists[g_CurrentActivityConsistIndex].totalUnits = (int)g_LoadedConsistUnits.size();
        int sel = g_ConsistList.GetSelectedIndex();
        if (sel >= 0)
        {
            std::wstring curName = g_ConsistList.GetCellText(sel, 0);
            if (curName.rfind(L"● ", 0) != 0)
            {
                g_ConsistList.SetCellText(sel, 0, L"● " + curName);
                g_ConsistList.SetCellText(sel, 1, std::to_wstring(g_LoadedConsistUnits.size()));
                g_ConsistList.Invalidate();
            }
        }
    }
}

static std::vector<ConsistReader::UnitInfo> GetSelectedStockUnitsFromLibrary()
{
    std::vector<ConsistReader::UnitInfo> units;
    if (g_hAssetList)
    {
        std::vector<int> selIndices = g_AssetList.GetSelectedIndices();
        if (selIndices.empty())
        {
            int singleSel = g_AssetList.GetSelectedIndex();
            if (singleSel >= 0) selIndices.push_back(singleSel);
        }

        EnterCriticalSection(&g_StockCacheCS);
        for (int selIdx : selIndices)
        {
            if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndices.size())
            {
                size_t cacheIdx = g_FilteredStockIndices[selIdx];
                if (cacheIdx < g_StockCache.size())
                {
                    const auto& item = g_StockCache[cacheIdx];
                    ConsistReader::UnitInfo u;
                    u.uid = item.szFileName;
                    u.parentDir = item.szFolder;
                    u.isEngine = (_wcsicmp(item.szExtension.c_str(), L".eng") == 0);
                    u.isFlipped = false;
                    units.push_back(u);
                }
            }
        }
        LeaveCriticalSection(&g_StockCacheCS);
    }
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

static void TransferStockUnitsToConsist(HWND hWnd, const std::vector<int>& stockIndices, int targetInsertPos)
{
    if (stockIndices.empty()) return;

    PushUndoState(L"Insert Transferred Stock Units");

    std::vector<ConsistReader::UnitInfo> toInsert;
    EnterCriticalSection(&g_StockCacheCS);
    for (int selIdx : stockIndices)
    {
        if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndices.size())
        {
            size_t cacheIdx = g_FilteredStockIndices[selIdx];
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
    HWND hFocus = GetFocus();
    if (hFocus == g_hEditorUnitList)
    {
        if (CopySelectedConsistUnits(hWnd)) return;
    }
    else if (hFocus == g_hAssetList || hFocus == g_hCategoryTree)
    {
        if (CopySelectedStockUnits(hWnd)) return;
    }

    if (!GetSelectedConsistUnitIndices().empty())
        CopySelectedConsistUnits(hWnd);
    else
        CopySelectedStockUnits(hWnd);
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

    std::wstring consistFolder = g_szBasePath;
    if (!consistFolder.empty() && consistFolder.back() != L'\\')
        consistFolder += L'\\';
    consistFolder += L"TRAINS\\CONSISTS\\";
    std::wstring fullPath = consistFolder + g_szCurrentConsistFile;

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

    // 2. Save
    bool success = ConsistWriter::SaveConsist(fullPath, szCfgId, szName, maxVelocityKmh, perfFactorPct, g_LoadedConsistUnits);

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
    }
    if (!g_szCurrentConsistFile.empty())
    {
        g_ConsistSessions[g_szCurrentConsistFile].isDirty = false;
        UpdateConsistManagerRow(g_szCurrentConsistFile);
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
        con.isBroken = (statusStr == L"Broken");

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
        if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId, SW_SHOW);
        if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,  SW_SHOW);
        if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_SHOW);
        if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_SHOW);
        if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_SHOW);
        if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_SHOW);
        if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_SHOW);
        if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_SHOW);
        if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_SHOW);
        if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_SHOW);
        if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_SHOW);

        if (g_ActiveTab == 1) // Activity Consists Tab: only Identifier is shown
        {
            if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_HIDE);
            if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_HIDE);
            if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_HIDE);
            if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_HIDE);
            if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_HIDE);
            if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_HIDE);
        }
        else // Main Consists Tab: show all 4 fields
        {
            if (g_hLabelTrainName)   ShowWindow(g_hLabelTrainName,   SW_SHOW);
            if (g_hEditTrainName)    ShowWindow(g_hEditTrainName,    SW_SHOW);
            if (g_hLabelMaxVelocity) ShowWindow(g_hLabelMaxVelocity, SW_SHOW);
            if (g_hEditMaxVelocity)  ShowWindow(g_hEditMaxVelocity,  SW_SHOW);
            if (g_hLabelPerfFactor)  ShowWindow(g_hLabelPerfFactor,  SW_SHOW);
            if (g_hEditPerfFactor)   ShowWindow(g_hEditPerfFactor,   SW_SHOW);
        }

        if (g_hEditorUnitList)   ShowWindow(g_hEditorUnitList,  SW_SHOW);

        // Populate edit controls
        SetWindowTextW(g_hEditTrainCfgId, trainCfg.trainCfgId.c_str());
        SetWindowTextW(g_hEditTrainName,  trainCfg.name.c_str());

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

        if (g_hVisualConsistView && !VisualConsistView_IsFloating(g_hVisualConsistView))
        {
            ShowWindow(g_hVisualConsistView, SW_SHOW);
            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
        }
        UpdateConsistManagerRow(filename);

        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        g_bIsLoadingConsist = false;
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
        if (g_hLabelTrainCfgId)  ShowWindow(g_hLabelTrainCfgId, SW_SHOW);
        if (g_hEditTrainCfgId)   ShowWindow(g_hEditTrainCfgId,  SW_SHOW);
        if (g_hSectionMetrics)   ShowWindow(g_hSectionMetrics,   SW_SHOW);
        if (g_hLabelMetricMass)   ShowWindow(g_hLabelMetricMass,   SW_SHOW);
        if (g_hEditMetricMass)    ShowWindow(g_hEditMetricMass,    SW_SHOW);
        if (g_hLabelMetricLength) ShowWindow(g_hLabelMetricLength, SW_SHOW);
        if (g_hEditMetricLength)  ShowWindow(g_hEditMetricLength,  SW_SHOW);
        if (g_hLabelMetricPower)  ShowWindow(g_hLabelMetricPower,  SW_SHOW);
        if (g_hEditMetricPower)   ShowWindow(g_hEditMetricPower,   SW_SHOW);
        if (g_hLabelMetricRatio)  ShowWindow(g_hLabelMetricRatio,  SW_SHOW);
        if (g_hEditMetricRatio)   ShowWindow(g_hEditMetricRatio,   SW_SHOW);

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

        if (g_hVisualConsistView && !VisualConsistView_IsFloating(g_hVisualConsistView))
        {
            ShowWindow(g_hVisualConsistView, SW_SHOW);
            VisualConsistView_SetUnits(g_hVisualConsistView, g_LoadedConsistUnits, g_szBasePath);
        }

        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        g_bIsLoadingConsist = false;
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

    if (g_hEditTrainName)
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

    // 4. Refresh Editor Unit table and Visual Consist Track View
    RefreshEditorUnitList(false);
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
            HKEY hKey = NULL;
            LSTATUS regStatus = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder\\Settings", 0, KEY_READ, &hKey);
            if (regStatus == ERROR_SUCCESS)
            {
                DWORD dwType = REG_SZ;
                DWORD dwBytes = sizeof(szSavedPath);
                regStatus = RegQueryValueExW(hKey, L"LastDirectory", NULL, &dwType, (LPBYTE)szSavedPath, &dwBytes);
                RegCloseKey(hKey);
                if (regStatus == ERROR_SUCCESS && wcslen(szSavedPath) > 0)
                {
                    bLoaded = TRUE;
                }
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

        // 5. Create Top Deck: Consist ListView
        // 5. Create Top Deck: Consist ListView (Custom Control)
        g_hConsistList = g_ConsistList.Create(hWnd, 0, 148, 600, 200, IDC_CONSISTLIST);
        if (g_hConsistList)
        {
            g_ConsistList.SetMultiSelect(true);
            g_ConsistList.SetAllowMarquee(true);
            g_ConsistList.AddColumn(L"Name", 240, 0);
            g_ConsistList.AddColumn(L"Units", 80, 0);
            g_ConsistList.AddColumn(L"Status", 100, 0);
            g_ConsistList.AddColumn(L"Modified", 160, 0);
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

        // 8. Create Bottom Deck: Asset ListView (Custom Control)
        g_hAssetList = g_AssetList.Create(hWnd, g_wCategorySplit + 8, 388, 350, 300, IDC_ASSETLIST);
        if (g_hAssetList)
        {
            g_AssetList.SetMultiSelect(true);
            g_AssetList.SetAllowRearrange(false);
            g_AssetList.SetAllowTransferSource(true);
            g_AssetList.SetAllowMarquee(true);
            g_AssetList.SetVirtualMode(AssetListGetCellText, NULL);
            g_AssetList.AddColumn(L"Name", 200, 0);
            g_AssetList.AddColumn(L"Type", 80, 0);
            g_AssetList.AddColumn(L"Folder", 150, 0);
        }

        // Populate tree
        PopulateCategoryTree();
        UpdateLibraryTheme(g_bDarkMode);

        // 9. Create Right Pane: Consist Editor Workspace (120px to bottom)
        g_hEditorPane = CreateWindowEx(
            0, L"STATIC", L"Consist Editor Workspace",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            600, 120, 500, 600,
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
            WS_CHILD | SS_CENTER | SS_NOPREFIX,            // center-aligned
            0, 0, 0, 0, hWnd, (HMENU)IDC_ED_SECTION_CFG, hInst, NULL);

        // Caption labels (small, muted text – vertically centered and left-aligned)
        g_hLabelTrainCfgId  = CreateWindowEx(0, L"STATIC", L"Consist Identifier",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelTrainName   = CreateWindowEx(0, L"STATIC", L"Train Name",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelMaxVelocity = CreateWindowEx(0, L"STATIC", L"Speed Limit (km/h)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);
        g_hLabelPerfFactor  = CreateWindowEx(0, L"STATIC", L"Performance Factor (%)",
            WS_CHILD | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, hWnd, NULL, hInst, NULL);

        // Flat edit boxes — ES_MULTILINE allows EM_SETRECT for vertical centering.
        // ES_CENTER centers text horizontally.
        // ES_AUTOHSCROLL keeps single-line scroll behaviour.
        // No WS_EX_CLIENTEDGE — border is drawn by ModernEditSubclassProc.
        const DWORD dwEditStyle = WS_CHILD | ES_CENTER | ES_MULTILINE | ES_AUTOHSCROLL;

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
            WS_CHILD | SS_LEFT,
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
        }
        if (g_hEditorUnitList)
        {
            g_EditorUnitList.AddColumn(L"No.",        50,  1);
            g_EditorUnitList.AddColumn(L"Name",       200, 0);
            g_EditorUnitList.AddColumn(L"Type",        80, 0);
            g_EditorUnitList.AddColumn(L"Status",      90, 0);
            g_EditorUnitList.AddColumn(L"Orientation", 90, 0);
            g_EditorUnitList.AddColumn(L"Parent Directory",  120, 0);
            ShowWindow(g_hEditorUnitList, SW_HIDE);
        }

        // Visual Consist Track Preview (docked by default at bottom)
        g_hVisualConsistView = CreateVisualConsistView(hWnd, hInst, 0, 0, 0, 0, 8888);
        if (g_hVisualConsistView)
        {
            ShowWindow(g_hVisualConsistView, SW_HIDE);
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

        // Bold/slightly larger font for section headers
        HFONT hSectionFont = NULL;
        if (hUIFont)
        {
            LOGFONT lf = {};
            GetObject(hUIFont, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight - 2) : (lf.lfHeight + 2);
            hSectionFont = CreateFontIndirect(&lf);
        }

        if (hUIFont)
        {
            if (g_hConsistHeader)    SendMessage(g_hConsistHeader,   WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hConsistList)      SendMessage(g_hConsistList,     WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hRouteTree)        g_RouteTreeView.SetFont(hUIFont);
            if (g_hStockHeader)      SendMessage(g_hStockHeader,     WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hCategoryTree)     g_CategoryTreeView.SetFont(hUIFont);
            if (g_hAssetList)        SendMessage(g_hAssetList,       WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
            if (g_hEditorPane)       SendMessage(g_hEditorPane,      WM_SETFONT, (WPARAM)hUIFont, MAKELPARAM(TRUE, 0));
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
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmEvent == EN_CHANGE)
        {
            if (!g_bIsLoadingConsist && (wmId == IDC_ED_TRAINCFGID || wmId == IDC_ED_TRAINNAME ||
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
            if (wmId == IDC_ED_TRAINCFGID || wmId == IDC_ED_TRAINNAME ||
                wmId == IDC_ED_MAXVELOCITY || wmId == IDC_ED_PERFFACTOR)
            {
                SaveCurrentConsist(hWnd);
            }
        }

        if (wmId == IDC_ASSETLIST && wmEvent >= 1000 && wmEvent < 2000)
        {
            if (g_hCategoryTree)
            {
                CustomTreeNode* hSelected = g_CategoryTreeView.GetSelectedNode();
                PopulateAssetGrid(hSelected);
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
            else if (wmId == IDC_ED_UNITLIST)
            {
                ShowFilterPopup(g_hEditorUnitList, colIndex);
                return 0;
            }
        }

        if (wmEvent == STN_CLICKED)
        {
            ActivePane newPane = g_ActivePane;
            if (wmId == IDC_CONSISTHEADER)
            {
                newPane = PANE_CONSIST;
                if (g_hConsistList) SetFocus(g_hConsistList);
            }
            else if (wmId == IDC_STOCKHEADER)
            {
                newPane = PANE_STOCK;
                if (g_hAssetList) SetFocus(g_hAssetList);
            }

            if (newPane != g_ActivePane)
            {
                g_ActivePane = newPane;
                InvalidateRect(g_hConsistHeader, NULL, TRUE);
                InvalidateRect(g_hStockHeader, NULL, TRUE);
                InvalidateRect(hWnd, NULL, TRUE);
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
        else if (hwndStatic == g_hLabelTrainCfgId || hwndStatic == g_hLabelTrainName || hwndStatic == g_hLabelMaxVelocity || hwndStatic == g_hLabelPerfFactor ||
                 hwndStatic == g_hLabelMetricMass || hwndStatic == g_hLabelMetricLength || hwndStatic == g_hLabelMetricPower || hwndStatic == g_hLabelMetricRatio)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, UITheme::TextMuted);
            static HBRUSH hbrCapDark = CreateSolidBrush(UITheme::DarkBackground);
            return (LRESULT)hbrCapDark;
        }
        // Train Details read-only edit controls
        else if (hwndStatic == g_hEditTrainCfgId || hwndStatic == g_hEditTrainName || hwndStatic == g_hEditMaxVelocity || hwndStatic == g_hEditPerfFactor ||
                 hwndStatic == g_hEditMetricMass || hwndStatic == g_hEditMetricLength || hwndStatic == g_hEditMetricPower || hwndStatic == g_hEditMetricRatio)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 220, 220));
            static HBRUSH hbrEditRo = CreateSolidBrush(RGB(38, 38, 38));
            return (LRESULT)hbrEditRo;
        }
        // Editor pane placeholder
        else if (id == IDC_EDITORPANE)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, UITheme::TextMuted);
            static HBRUSH hbrPaneDark = CreateSolidBrush(UITheme::DarkBackground);
            return (LRESULT)hbrPaneDark;
        }
        else if (id == IDC_CONSISTHEADER || id == IDC_STOCKHEADER)
        {
            SetBkMode(hdc, TRANSPARENT);
            BOOL isActive = FALSE;
            if (id == IDC_CONSISTHEADER && g_ActivePane == PANE_CONSIST) isActive = TRUE;
            if (id == IDC_STOCKHEADER && g_ActivePane == PANE_STOCK) isActive = TRUE;

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
        // Only restyle our consist-editor fields
        if (hwndEdit == g_hEditTrainCfgId || hwndEdit == g_hEditTrainName || hwndEdit == g_hEditMaxVelocity || hwndEdit == g_hEditPerfFactor ||
            hwndEdit == g_hEditMetricMass || hwndEdit == g_hEditMetricLength || hwndEdit == g_hEditMetricPower || hwndEdit == g_hEditMetricRatio)
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, UITheme::TextPrimary);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        // Generic dark-mode colouring for any other edit controls
        SetTextColor(hdc, UITheme::TextPrimary);
        SetBkColor(hdc, RGB(45, 45, 45));
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(45, 45, 45));
        return (LRESULT)hbrEditDark;
    }

    case WM_NAVTOOLBAR_NAVIGATE:
    {
        wchar_t szPath[MAX_PATH] = { 0 };
        NavToolbar_GetPath(g_hNavToolbar, szPath, MAX_PATH);
        if (szPath[0] != L'\0')
        {
            // If the user navigated to the exact same path, do nothing
            if (!g_szBasePath.empty() && _wcsicmp(szPath, g_szBasePath.c_str()) == 0)
            {
                break;
            }

            // Validation Guard: Check if the folder contains Train Simulator folders or files
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
            // Save to registry
            HKEY hKey = NULL;
            LSTATUS regStatus = RegCreateKeyExW(
                HKEY_CURRENT_USER,
                L"Software\\TrainSimConsistBuilder\\Settings",
                0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL
            );
            if (regStatus == ERROR_SUCCESS)
            {
                DWORD dwBytes = (DWORD)((wcslen(szPath) + 1) * sizeof(wchar_t));
                RegSetValueExW(hKey, L"LastDirectory", 0, REG_SZ, (const BYTE*)szPath, dwBytes);
                RegCloseKey(hKey);
            }

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
                bool isFixedInSession = (!pConsist->isBroken && g_SessionFixedConsists.count(pConsist->szFileName) > 0);
                std::wstring statusStr = pConsist->isBroken ? L"Broken" : (isFixedInSession ? L"Fixed" : L"Healthy");
                
                std::wstring displayName = pConsist->szName;
                auto itSess = g_ConsistSessions.find(pConsist->szFileName);
                if (itSess != g_ConsistSessions.end() && itSess->second.isDirty)
                {
                    displayName = L"● " + displayName;
                }
                g_ConsistList.AddItem({ displayName, std::to_wstring(pConsist->nUnits), statusStr, pConsist->szLastModified, pConsist->szFileName });
            }
            delete pConsist;
        }
        return 0;
    }

    case WM_CONSIST_SCAN_COMPLETE:
    {
        if (g_hConsistList != NULL)
        {
            int wList = g_ConsistList.GetUsableWidth();
            int wCol0 = g_ConsistList.GetColumnWidth(0);
            int wCol1 = g_ConsistList.GetColumnWidth(1);
            int wCol2 = g_ConsistList.GetColumnWidth(2);
            int wLast = wList - (wCol0 + wCol1 + wCol2);
            if (wLast < 50) wLast = 50;
            g_ConsistList.SetColumnWidth(3, wLast);
        }

        int totalConsists = g_ConsistList.GetItemCount();
        int brokenConsists = 0;
        for (int i = 0; i < totalConsists; ++i)
        {
            if (g_ConsistList.GetCellText(i, 2) == L"Broken")
            {
                brokenConsists++;
            }
        }
        wchar_t szHeader[128];
        swprintf_s(szHeader, 128, L"  Consists Manager [ Total: %d • Broken: %d ]", totalConsists, brokenConsists);
        SetWindowTextW(g_hConsistHeader, szHeader);

        g_ConsistList.SortByColumn(0, false);
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
        swprintf_s(szStockHeader, 128, L"  Stock Library [ Total: %d • Engines: %d • Wagons: %d ]", totalItems, engineCount, wagonCount);
        SetWindowTextW(g_hStockHeader, szStockHeader);

        wchar_t szMsg[256];
        swprintf_s(szMsg, 256, L"Stock Library scan complete!\n\nFound %d total rolling stock items (%d Engines, %d Wagons).",
            totalItems, engineCount, wagonCount);
        ShowModernMessageBox(hWnd, szMsg, L"Scan Completed", MB_OK | MB_ICONINFORMATION);

        return 0;
    }

    case WM_STOCK_SCAN_PROGRESS:
    {
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
            
            TriggerConsistsRescan(hWnd);
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
        case CMD_ACTION_POOL_MANAGER:
            ShowPoolManagerDialog(hWnd);
            break;
        case CMD_ACTION_POOL_MUTATOR:
        {
            std::vector<std::wstring> selConsists;
            std::vector<int> selRows = g_ConsistList.GetSelectedIndices();
            if (selRows.empty())
            {
                int singleSel = g_ConsistList.GetSelectedIndex();
                if (singleSel >= 0) selRows.push_back(singleSel);
            }
            if (g_ActiveTab == 1)
            {
                for (int row : selRows)
                {
                    std::wstring idxStr = g_ConsistList.GetCellText(row, 3);
                    if (!idxStr.empty())
                    {
                        selConsists.push_back(L"ACTIVITY:" + idxStr);
                    }
                }
                if (selConsists.empty() && g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
                {
                    selConsists.push_back(L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex));
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
                        if (!fullPath.empty()) selConsists.push_back(fullPath);
                    }
                }
                if (selConsists.empty() && !g_szCurrentConsistFile.empty())
                {
                    std::wstring fullPath = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
                    if (!fullPath.empty()) selConsists.push_back(fullPath);
                }
            }
            std::vector<int> selUnits = GetSelectedConsistUnitIndices();
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
                g_hStockScanThread = StartStockScan(hWnd, g_szBasePath);
            }
            break;
        case CMD_ACTION_ABOUT:
            Updater::ShowAboutDialog(hWnd);
            break;
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
            else if (hover == DRAG_SPLIT2)
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
            else if (hover == DRAG_SPLIT2)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
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
        else if (g_DragState == DRAG_SPLIT2)
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
            else if (hover == DRAG_SPLIT2)
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
            SetWindowPos(g_hConsistHeader, NULL, 0, paneY, wTreeSplitWidth, 28, SWP_NOZORDER);
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
            SetWindowPos(g_hStockHeader, NULL, 0, bottomY, wTreeSplitWidth, 28, SWP_NOZORDER);
        }
        if (g_hCategoryTree != NULL)
        {
            g_CategoryTreeView.SetBounds(0, bottomY + 29, g_wCategorySplit, bottomHeight - 29);
        }
        if (g_hAssetList != NULL)
        {
            int xAsset = g_wCategorySplit + 9;
            int wAsset = wTreeSplitWidth - xAsset;
            if (wAsset < 50) wAsset = 50;
            SetWindowPos(g_hAssetList, NULL, xAsset, bottomY + 29, wAsset, bottomHeight - 29, SWP_NOZORDER | SWP_NOCOPYBITS);
        }

        // Auto-stretch last column (index 3) of Consists ListView to fill remaining width
        if (g_hConsistList != NULL)
        {
            int wList = g_ConsistList.GetUsableWidth();
            int wCol0 = g_ConsistList.GetColumnWidth(0);
            int wCol1 = g_ConsistList.GetColumnWidth(1);
            int wCol2 = g_ConsistList.GetColumnWidth(2);
            int wLast = wList - (wCol0 + wCol1 + wCol2);
            if (wLast < 50) wLast = 50;
            g_ConsistList.SetColumnWidth(3, wLast);
        }

        // Auto-stretch last column (index 2) of Asset ListView to fill remaining width
        if (g_hAssetList != NULL)
        {
            int wList = g_AssetList.GetUsableWidth();
            int wCol0 = g_AssetList.GetColumnWidth(0);
            int wCol1 = g_AssetList.GetColumnWidth(1);
            int wLast = wList - (wCol0 + wCol1);
            if (wLast < 50) wLast = 50;
            g_AssetList.SetColumnWidth(2, wLast);
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
        if (g_hSplitter3 != NULL)
        {
            SetWindowPos(g_hSplitter3, NULL, g_wCategorySplit, bottomY + 29, 9, bottomHeight - 29, SWP_NOZORDER | SWP_SHOWWINDOW);
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

        // Right Pane: Editor Workspace
        if (g_hEditorPane != NULL)
        {
            SetWindowPos(g_hEditorPane, NULL, wLeftPane + 9, paneY, wEditor, paneHeight, SWP_NOZORDER);

            // -------------------------------------------------------
            // Modern inline layout (Label on left, Value on right)
            // Split the workspace into left/right halves by a vertical separator line.
            // All controls are constrained to the left half.
            // -------------------------------------------------------
            const int GUTTER   = 24;  // left/right margin
            const int FIELD_H  = 32;  // edit box height (and label height)
            const int ROW_GAP  = 12;  // vertical gap between rows
            const int SEC_H    = 26;  // section header height
            const int SEC_GAP  = 14;  // gap after section header

            // Inner vertical padding for EM_SETRECT centering
            const int VPAD = 8;   // 8 px top + bottom margin

            int wLeftPane = g_wConsist;
            int wEditor = width - wLeftPane - 9;
            if (wEditor < 100) wEditor = 100;

            int edX = wLeftPane + 9 + GUTTER;
            int edW = wEditor - GUTTER * 2;
            if (edW < 100) edW = 100;

            // Dual card layout: split into 2 equal-width cards with a 24px gap
            const int CARD_GAP = 24;
            int cardW = (edW - CARD_GAP) / 2;
            if (cardW < 200) cardW = (edW - CARD_GAP) / 2;
            if (cardW < 50) cardW = 50;

            int leftCardX = edX;
            int rightCardX = edX + cardW + CARD_GAP;

            int labelW = 160;
            if (labelW > cardW - 80) labelW = cardW - 80;
            if (labelW < 80) labelW = 80;
            int valueW = cardW - labelW - 10;
            if (valueW < 50) valueW = 50;

            int leftValX = leftCardX + labelW + 10;
            int rightValX = rightCardX + labelW + 10;

            int topY = paneY + GUTTER;
            int leftY = topY;
            int rightY = topY;

            // -------------------------------------------------------
            // Left Card: "TRAIN DETAILS" — centered directly over fields
            // -------------------------------------------------------
            if (g_hSectionTrainCfg)
                SetWindowPos(g_hSectionTrainCfg, NULL, leftValX, leftY, valueW, SEC_H, SWP_NOZORDER);
            leftY += SEC_H + SEC_GAP;

            // Row 1: Consist Identifier
            if (g_hLabelTrainCfgId)
                SetWindowPos(g_hLabelTrainCfgId, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditTrainCfgId) {
                SetWindowPos(g_hEditTrainCfgId, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditTrainCfgId, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            leftY += FIELD_H + ROW_GAP;

            if (g_ActiveTab == 0) // Main Consists Tab: show and position Rows 2, 3, 4
            {
                // Row 2: Train Name
                if (g_hLabelTrainName)
                    SetWindowPos(g_hLabelTrainName, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditTrainName) {
                    SetWindowPos(g_hEditTrainName, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditTrainName, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;

                // Row 3: Speed Limit
                if (g_hLabelMaxVelocity)
                    SetWindowPos(g_hLabelMaxVelocity, NULL, leftCardX, leftY, labelW, FIELD_H, SWP_NOZORDER);
                if (g_hEditMaxVelocity) {
                    SetWindowPos(g_hEditMaxVelocity, NULL, leftValX, leftY, valueW, FIELD_H, SWP_NOZORDER);
                    RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                    SendMessage(g_hEditMaxVelocity, EM_SETRECT, 0, (LPARAM)&rcFmt);
                }
                leftY += FIELD_H + ROW_GAP;

                // Row 4: Performance Factor
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
            // Right Card: "TRAIN SUMMARY & METRICS" — centered directly over fields
            // -------------------------------------------------------
            if (g_hSectionMetrics)
                SetWindowPos(g_hSectionMetrics, NULL, rightValX, rightY, valueW, SEC_H, SWP_NOZORDER);
            rightY += SEC_H + SEC_GAP;

            // Row 1: Total Mass
            if (g_hLabelMetricMass)
                SetWindowPos(g_hLabelMetricMass, NULL, rightCardX, rightY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditMetricMass) {
                SetWindowPos(g_hEditMetricMass, NULL, rightValX, rightY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditMetricMass, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            rightY += FIELD_H + ROW_GAP;

            // Row 2: Total Length
            if (g_hLabelMetricLength)
                SetWindowPos(g_hLabelMetricLength, NULL, rightCardX, rightY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditMetricLength) {
                SetWindowPos(g_hEditMetricLength, NULL, rightValX, rightY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditMetricLength, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            rightY += FIELD_H + ROW_GAP;

            // Row 3: Total Power
            if (g_hLabelMetricPower)
                SetWindowPos(g_hLabelMetricPower, NULL, rightCardX, rightY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditMetricPower) {
                SetWindowPos(g_hEditMetricPower, NULL, rightValX, rightY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditMetricPower, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            rightY += FIELD_H + ROW_GAP;

            // Row 4: Composition
            if (g_hLabelMetricRatio)
                SetWindowPos(g_hLabelMetricRatio, NULL, rightCardX, rightY, labelW, FIELD_H, SWP_NOZORDER);
            if (g_hEditMetricRatio) {
                SetWindowPos(g_hEditMetricRatio, NULL, rightValX, rightY, valueW, FIELD_H, SWP_NOZORDER);
                RECT rcFmt = { 6, VPAD, valueW - 6, FIELD_H - VPAD };
                SendMessage(g_hEditMetricRatio, EM_SETRECT, 0, (LPARAM)&rcFmt);
            }
            rightY += FIELD_H + ROW_GAP;

            // Advance curY below the taller of the two cards
            int curY = (std::max)(leftY, rightY) + 6;

            // "Consist Units" section header (integrated 28px card header bar)
            const int SEC_H_UNITS = 28;
            if (g_hSectionUnits)
                SetWindowPos(g_hSectionUnits, NULL, edX, curY, edW, SEC_H_UNITS, SWP_NOZORDER);

            // Unit list and Visual Consist Track View layout
            int visH = VisualConsistView_GetDesiredHeight(g_hVisualConsistView);
            int availableH = paneY + paneHeight - curY - GUTTER;
            int tableHeight = availableH - SEC_H_UNITS;
            if (visH > 0 && g_hVisualConsistView && !VisualConsistView_IsFloating(g_hVisualConsistView))
            {
                tableHeight -= (visH + 12); // 12px clean gap between the two framed cards
            }
            if (tableHeight < 50) tableHeight = 50;

            if (g_hEditorUnitList)
                SetWindowPos(g_hEditorUnitList, NULL, edX, curY + SEC_H_UNITS, edW, tableHeight, SWP_NOZORDER);

            int visY = curY + SEC_H_UNITS + tableHeight + 12;
            if (g_hVisualConsistView && !VisualConsistView_IsFloating(g_hVisualConsistView))
            {
                BOOL bShow = (g_hSectionUnits && IsWindowVisible(g_hSectionUnits));
                SetWindowPos(g_hVisualConsistView, NULL, edX, visY, edW, visH, SWP_NOZORDER | (bShow ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            }

            // Stretch the last column (Parent Directory, index 5) of Unit List to fill remaining width
            int wListUnit  = g_EditorUnitList.GetUsableWidth();
            int wCol0Unit  = g_EditorUnitList.GetColumnWidth(0); // No.
            int wCol1Unit  = g_EditorUnitList.GetColumnWidth(1); // Name
            int wCol2Unit  = g_EditorUnitList.GetColumnWidth(2); // Type
            int wCol3Unit  = g_EditorUnitList.GetColumnWidth(3); // Status
            int wCol4Unit  = g_EditorUnitList.GetColumnWidth(4); // Orientation
            int wLastUnit  = wListUnit - (wCol0Unit + wCol1Unit + wCol2Unit + wCol3Unit + wCol4Unit);
            if (wLastUnit < 60) wLastUnit = 60;
            g_EditorUnitList.SetColumnWidth(5, wLastUnit);
        }
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

        // Draw Splitter 3 Gutter (vertical Category Tree vs Asset List separator bar in bottom deck, and top deck on Activity tab)
        RECT rcSplit3 = { g_wCategorySplit, bottomY + 28, g_wCategorySplit + 9, rc.bottom };
        FillRect(hdc, &rcSplit3, hbrSplit);
        if (g_ActiveTab == 1)
        {
            RECT rcSplitTop = { g_wCategorySplit, paneY + 28, g_wCategorySplit + 9, paneY + g_hConsistSplit };
            FillRect(hdc, &rcSplitTop, hbrSplit);
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
        MoveToEx(hdc, g_wCategorySplit, bottomY + 28, NULL);
        LineTo(hdc, g_wCategorySplit, rc.bottom);
        MoveToEx(hdc, g_wCategorySplit + 8, bottomY + 28, NULL);
        LineTo(hdc, g_wCategorySplit + 8, rc.bottom);

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
                if (pnmh->hwndFrom == g_hAssetList)
                {
                    std::vector<int> selStock = g_AssetList.GetSelectedIndices();
                    if (selStock.empty())
                    {
                        int singleSel = g_AssetList.GetSelectedIndex();
                        if (singleSel >= 0) selStock.push_back(singleSel);
                    }
                    EnterCriticalSection(&g_StockCacheCS);
                    for (int selIdx : selStock)
                    {
                        if (selIdx >= 0 && selIdx < (int)g_FilteredStockIndices.size())
                        {
                            size_t cacheIdx = g_FilteredStockIndices[selIdx];
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
            else if (pnmh->hwndFrom == g_hAssetList)
            {
                // Dedicated Stock Transfer from Stock Library to Consist Units Table
                if (isOverEditor && dropIdx >= 0)
                {
                    std::vector<int> selStock = g_AssetList.GetSelectedIndices();
                    if (selStock.empty())
                    {
                        int singleSel = g_AssetList.GetSelectedIndex();
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
                        TransferStockUnitsToConsist(hWnd, selStock, targetInsertPos);
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
                menuItems.push_back(ContextMenuItem::Action(4, L"\xE745", L"Flip Selected Unit(s)", L"F", hasSelection));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(16, L"\xE8C6", L"Cut Selected Unit(s)", L"Ctrl+X", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(5, L"\xE8C8", L"Copy Selected Unit(s)", L"Ctrl+C", hasSelection));
                menuItems.push_back(ContextMenuItem::Action(9, L"\xE77F", L"Insert at Beginning", L"Ctrl+Shift+V", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Action(6, L"\xE77F", L"Insert After Selected", L"Ctrl+V", canInsertUnits && hasSelection));
                menuItems.push_back(ContextMenuItem::Action(10, L"\xE77F", L"Insert at End", L"Ctrl+Alt+V", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Action(15, L"\xE77F", L"Insert Units from Pool Preset...", L"", true));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(11, L"\xE7B8", viewClipText, L"Alt+V", true));
                menuItems.push_back(ContextMenuItem::Action(12, L"\xE75C", L"Clear Clipboard", L"", canInsertUnits));
                menuItems.push_back(ContextMenuItem::Separator());
                menuItems.push_back(ContextMenuItem::Action(7, L"\xE7A7", L"Undo", L"Ctrl+Z", canUndo));
                menuItems.push_back(ContextMenuItem::Action(8, L"\xE7A6", L"Redo", L"Ctrl+Y", canRedo));
            }

            int cmd = ModernContextMenu::Show(hWnd, screenX, screenY, menuItems, TRUE);
            switch (cmd)
            {
            case 1: DeleteSelectedConsistUnits(hWnd); break;
            case 2: ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS); break;
            case 3: ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING); break;
            case 14:
            {
                if (g_ActiveTab == 1)
                {
                    if (g_CurrentActivityConsistIndex >= 0 && g_CurrentActivityConsistIndex < (int)g_CurrentActivityData.consists.size())
                    {
                        std::wstring targetKey = L"ACTIVITY:" + std::to_wstring(g_CurrentActivityConsistIndex);
                        ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::ReplaceSelected, { targetKey }, selIndices);
                    }
                }
                else if (!g_szCurrentConsistFile.empty())
                {
                    std::wstring currentFile = EnsureConsistFilePath(g_szBasePath, g_szCurrentConsistFile);
                    ShowPoolMutatorDialog(hWnd, PoolMutator::MutatorMode::ReplaceSelected, { currentFile }, selIndices);
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

            std::vector<int> selRows = g_ConsistList.GetSelectedIndices();
            if (selRows.empty())
            {
                int single = g_ConsistList.GetSelectedIndex();
                if (single >= 0) selRows.push_back(single);
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

        if (idCtrl == IDC_ASSETLIST && pnmh->code == NM_RCELLCLICK)
        {
            NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
            int screenX = pCell->itemIndex;
            int screenY = pCell->subItemIndex;

            std::vector<ConsistReader::UnitInfo> selectedStock = GetSelectedStockUnitsFromLibrary();
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
                ContextMenuItem::Action(6, L"\xE77F", L"Insert Stock at End", L"Enter", hasStockSel),
                ContextMenuItem::Action(9, L"\xE77F", L"Insert Stock at Beginning", L"Ctrl+Shift+V", hasStockSel),
                ContextMenuItem::Action(13, L"\xE77F", L"Insert Stock After Selected", L"Ctrl+V", hasStockSel && hasConsistSel),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(2, L"\xE777", L"Replace Selected in Consist", L"Ctrl+R", hasStockSel && hasConsistSel),
                ContextMenuItem::Action(3, L"\xE8D7", replaceAllText, L"Ctrl+Shift+R", hasStockSel && hasConsistSel),
                ContextMenuItem::Separator(),
                ContextMenuItem::Action(11, L"\xE7B8", viewClipText, L"Alt+V", true),
                ContextMenuItem::Action(12, L"\xE75C", L"Clear Clipboard", L"", canInsertUnits)
            };

            int cmd = ModernContextMenu::Show(hWnd, screenX, screenY, menuItems, TRUE);
            switch (cmd)
            {
            case 5: CopySelectedStockUnits(hWnd); break;
            case 6: PasteConsistUnits(hWnd, PASTE_END, &selectedStock); break;
            case 9: PasteConsistUnits(hWnd, PASTE_START, &selectedStock); break;
            case 13: PasteConsistUnits(hWnd, PASTE_AFTER_SELECTED, &selectedStock); break;
            case 2: ExecuteConsistReplacement(hWnd, SCOPE_SELECTED_ROWS, &selectedStock); break;
            case 3: ExecuteConsistReplacement(hWnd, SCOPE_ALL_MATCHING, &selectedStock); break;
            case 11: ShowClipboardContents(hWnd); break;
            case 12: ClearClipboard(hWnd); break;
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
            ActivePane newPane = g_ActivePane;
            if (pnmhdr->hwndFrom == g_hConsistList || pnmhdr->hwndFrom == g_hRouteTree || pnmhdr->hwndFrom == g_hEditorUnitList)
            {
                newPane = PANE_CONSIST;
            }
            else if (pnmhdr->hwndFrom == g_hCategoryTree || pnmhdr->hwndFrom == g_hAssetList)
            {
                newPane = PANE_STOCK;
            }

            if (newPane != g_ActivePane)
            {
                g_ActivePane = newPane;
                InvalidateRect(g_hConsistHeader, NULL, TRUE);
                InvalidateRect(g_hStockHeader, NULL, TRUE);
                InvalidateRect(hWnd, NULL, TRUE);

                if (newPane == PANE_CONSIST)
                {
                    NavToolbar_SetSearchQuery(g_hNavToolbar, g_szConsistSearchQuery.c_str());
                }
                else if (newPane == PANE_STOCK)
                {
                    NavToolbar_SetSearchQuery(g_hNavToolbar, g_szStockSearchQuery.c_str());
                }
            }
        }
        else if (pnmhdr->hwndFrom == g_hConsistList && (pnmhdr->code == NM_CLICK || pnmhdr->code == NM_RETURN || pnmhdr->code == NM_DBLCLK))
        {
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
            return 0;
        }
        else if (pnmhdr->hwndFrom == g_hEditorUnitList && (pnmhdr->code == NM_CELLCLICK || pnmhdr->code == NM_CLICK))
        {
            if (pnmhdr->code == NM_CELLCLICK)
            {
                NMCELLCLICK* pCellClick = (NMCELLCLICK*)lParam;
                if (pCellClick->subItemIndex == 4) // Orientation column (index 4)
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
                            RefreshEditorUnitList();
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
        DeleteCriticalSection(&g_StockCacheCS);
        g_bCancelScan = TRUE;
        if (g_hScanThread != NULL)
        {
            WaitForSingleObject(g_hScanThread, 200);
            CloseHandle(g_hScanThread);
            g_hScanThread = NULL;
        }
        CancelStockScan(g_hStockScanThread);

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
    std::wstring text = L"";
    EnterCriticalSection(&g_StockCacheCS);
    if (itemIndex >= 0 && itemIndex < (int)g_FilteredStockIndices.size())
    {
        size_t globalIdx = g_FilteredStockIndices[itemIndex];
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