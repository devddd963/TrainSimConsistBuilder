#include "ClassicConsistView.h"
#include "ModernEditControl.h"
#include "CustomScrollBar.h"
#include "FluentDragGhost.h"
#include "UITheme.h"
#include "../SRC/ConsistReader.h"
#include "../SRC/ConsistWriter.h"
#include "../SRC/StockSpecReader.h"
#include "../SRC/DatabaseManager.h"
#include <windowsx.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cwctype>

#define IDC_CLASSIC_SEARCH_EDIT    9101
#define IDC_CLASSIC_ED_FILENAME    9111
#define IDC_CLASSIC_ED_CFGID       9112
#define IDC_CLASSIC_ED_TRAINNAME   9113
#define IDC_CLASSIC_ED_MAXVELOCITY 9114
#define IDC_CLASSIC_ED_PERFFACTOR  9115

struct UnitMetricsSummary {
    double totalMass = 0.0;
    double engineMass = 0.0;
    double wagonMass = 0.0;
    double totalLength = 0.0;
    double totalPowerHP = 0.0;
    double totalPowerKW = 0.0;
    int engineCount = 0;
    int wagonCount = 0;
};

struct ClassicConsistViewState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HINSTANCE hInstance = NULL;

    // Consist Data
    std::vector<ConsistReader::UnitInfo> units;
    std::wstring basePath;
    std::wstring consistName;
    std::wstring fileName;
    std::wstring cfgId;
    std::wstring maxVelocity;
    std::wstring perfFactor;

    // Physical Metrics
    UnitMetricsSummary metrics;

    // Selection & Checkbox States
    std::unordered_set<int> selUnits;
    std::unordered_set<int> checkedUnits;
    int anchorIndex = -1;
    int focusedIndex = -1;

    // Scrolling & Layout
    CustomScrollBar vScroll{ ScrollBarOrientation::Vertical };
    int listWidth = 536; // resizable splitter width in sync with all columns (No:48 + Name:220 + Orient:100 + Status:100 + gutter + scrollbar)
    bool isDraggingSplitter = false;
    bool hasUserDraggedSplitter = false;
    int splitterDragStartX = 0;
    int splitterStartWidth = 536;

    // Hover states
    int hoverRow = -1;
    int hoverBtn = 0; // 1 = Flip, 2 = Delete

    // Resequencing Drag & Drop
    bool isDraggingItem = false;
    int dragSourceIdx = -1;
    int dropTargetIdx = -1;
    POINT ptDragStart{ 0, 0 };

    // Stock drop hover from external decks
    int stockDropIndex = -1;

    // Consist Details Edit Controls
    HWND hEditFileName = NULL;
    HWND hEditCfgId = NULL;
    HWND hEditTrainName = NULL;
    HWND hEditMaxVelocity = NULL;
    HWND hEditPerfFactor = NULL;
    bool isUpdatingMetadata = false;

    // Filtering / Search
    HWND hSearchEdit = NULL;
    std::wstring searchQuery;

    // Sizing Rectangles
    RECT rcListPanel{ 0, 0, 0, 0 };
    RECT rcListHeader{ 0, 0, 0, 0 };
    RECT rcListBody{ 0, 0, 0, 0 };
    RECT rcSplitter{ 0, 0, 0, 0 };
    RECT rcRightPanel{ 0, 0, 0, 0 };
    RECT rcPreviewContainer{ 0, 0, 0, 0 }; // Squared shape
    RECT rcDetailsCard{ 0, 0, 0, 0 };      // Beside squared preview
    RECT rcTelemetryPanel{ 0, 0, 0, 0 };   // Bottom metrics & unit specs

    // Header Delete Button
    RECT rcBtnDelete{ 0, 0, 0, 0 };
    bool isHoverDelete = false;
    bool isDirty = false;

    // GDI Fonts
    HFONT hFontHeader = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontNormal = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontBadges = NULL;
    HFONT hFontTelemetryVal = NULL;

    BOOL bDarkMode = TRUE;
};

static const wchar_t* CLASSIC_VIEW_PROP = L"ClassicConsistViewState";

// Helper: Calculate Physical Metrics from Consist Units
static void RecalculateMetrics(ClassicConsistViewState* pState)
{
    if (!pState) return;
    pState->metrics = UnitMetricsSummary();

    for (const auto& u : pState->units)
    {
        if (u.isEngine) pState->metrics.engineCount++;
        else pState->metrics.wagonCount++;

        if (u.uid.empty() || u.parentDir.empty() || pState->basePath.empty()) continue;

        std::wstring ext = u.isEngine ? L".eng" : L".wag";
        std::wstring fullPath = pState->basePath;
        if (!fullPath.empty() && fullPath.back() != L'\\') fullPath += L'\\';
        fullPath += L"TRAINS\\TRAINSET\\" + u.parentDir + L"\\" + u.uid + ext;

        std::string ascii = ReadConFileToAscii(fullPath);
        if (ascii.empty()) continue;

        std::string lower = ascii;
        for (char& c : lower) c = (char)tolower(c);

        // Mass (t / tonnes)
        size_t pMass = lower.find("mass");
        if (pMass != std::string::npos)
        {
            size_t pOpen = lower.find('(', pMass);
            if (pOpen != std::string::npos)
            {
                size_t pClose = lower.find(')', pOpen);
                if (pClose != std::string::npos)
                {
                    std::string token = lower.substr(pOpen + 1, pClose - pOpen - 1);
                    double m = atof(token.c_str());
                    if (token.find("lb") != std::string::npos) m *= 0.000453592;
                    if (u.isEngine) pState->metrics.engineMass += m;
                    else pState->metrics.wagonMass += m;
                    pState->metrics.totalMass += m;
                }
            }
        }

        // Length (Size: x y z)
        size_t pSize = lower.find("size");
        if (pSize != std::string::npos)
        {
            size_t pOpen = lower.find('(', pSize);
            if (pOpen != std::string::npos)
            {
                size_t pClose = lower.find(')', pOpen);
                if (pClose != std::string::npos)
                {
                    std::string token = lower.substr(pOpen + 1, pClose - pOpen - 1);
                    float sx = 0, sy = 0, sz = 0;
                    if (sscanf_s(token.c_str(), "%f %*[^0-9] %f %*[^0-9] %f", &sx, &sy, &sz) >= 3 ||
                        sscanf_s(token.c_str(), "%f %f %f", &sx, &sy, &sz) >= 3)
                    {
                        pState->metrics.totalLength += sz;
                    }
                }
            }
        }

        // Engine Power (hp / kw)
        if (u.isEngine)
        {
            size_t pMaxPower = lower.find("maxpower");
            if (pMaxPower != std::string::npos)
            {
                size_t pOpen = lower.find('(', pMaxPower);
                if (pOpen != std::string::npos)
                {
                    size_t pClose = lower.find(')', pOpen);
                    if (pClose != std::string::npos)
                    {
                        std::string token = lower.substr(pOpen + 1, pClose - pOpen - 1);
                        double hp = atof(token.c_str());
                        if (token.find("kw") != std::string::npos)
                        {
                            hp *= 1.34102;
                        }
                        pState->metrics.totalPowerHP += hp;
                        pState->metrics.totalPowerKW += hp * 0.7457;
                    }
                }
            }
        }
    }
}

// Filter matching helper
static bool UnitMatchesFilter(const ConsistReader::UnitInfo& u, const std::wstring& query)
{
    if (query.empty()) return true;

    auto containsCaseInsensitive = [](const std::wstring& haystack, const std::wstring& needle) {
        if (needle.empty()) return true;
        auto it = std::search(
            haystack.begin(), haystack.end(),
            needle.begin(), needle.end(),
            [](wchar_t ch1, wchar_t ch2) { return towlower(ch1) == towlower(ch2); }
        );
        return it != haystack.end();
    };

    if (containsCaseInsensitive(u.uid, query)) return true;
    if (containsCaseInsensitive(u.parentDir, query)) return true;
    if (u.isEngine && containsCaseInsensitive(L"Engine Locomotive ENG", query)) return true;
    if (!u.isEngine && containsCaseInsensitive(L"Wagon Carriage Freight WAG", query)) return true;
    return false;
}

// Get filtered unit list indices
static std::vector<int> GetFilteredUnitIndices(const ClassicConsistViewState* pState)
{
    std::vector<int> visible;
    if (!pState) return visible;

    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        if (UnitMatchesFilter(pState->units[i], pState->searchQuery))
        {
            visible.push_back((int)i);
        }
    }
    return visible;
}

// Edit control subclass to filter Enter key and prevent unwanted newlines in multiline edit controls
static LRESULT CALLBACK ClassicEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_CHAR:
        if (wParam == VK_RETURN)
        {
            // Drop focus to parent on Enter
            HWND hParent = GetParent(hWnd);
            if (hParent) SetFocus(hParent);
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        if (wParam == VK_RETURN)
        {
            return DLGC_WANTALLKEYS;
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, ClassicEditSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Window procedure
static LRESULT CALLBACK ClassicConsistViewProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
        pState = new ClassicConsistViewState();
        pState->hWnd = hWnd;
        pState->hParent = pcs->hwndParent;
        pState->hInstance = pcs->hInstance;
        SetPropW(hWnd, CLASSIC_VIEW_PROP, (HANDLE)pState);

        // Create standard fluent fonts
        pState->hFontHeader = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontNormal = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontBadges = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontTelemetryVal = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFontIcons = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

        // Embedded search filter edit (borderless, dark styled)
        pState->hSearchEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_SEARCH_EDIT, pcs->hInstance, NULL
        );
        if (pState->hSearchEdit)
        {
            SendMessageW(pState->hSearchEdit, WM_SETFONT, (WPARAM)pState->hFontNormal, MAKELPARAM(TRUE, 0));
            SendMessageW(pState->hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search units...");
        }

        // Consist Details Edit Controls
        // Text and numeric controls use ES_LEFT with ES_AUTOHSCROLL for proper long name viewing and scrolling without glitching.
        const DWORD dwTextEditStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOHSCROLL;
        const DWORD dwNumEditStyle  = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOHSCROLL;

        pState->hEditFileName = CreateWindowExW(
            0, L"EDIT", L"", dwTextEditStyle,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_ED_FILENAME, pcs->hInstance, NULL
        );
        pState->hEditCfgId = CreateWindowExW(
            0, L"EDIT", L"", dwTextEditStyle,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_ED_CFGID, pcs->hInstance, NULL
        );
        pState->hEditTrainName = CreateWindowExW(
            0, L"EDIT", L"", dwTextEditStyle,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_ED_TRAINNAME, pcs->hInstance, NULL
        );
        pState->hEditMaxVelocity = CreateWindowExW(
            0, L"EDIT", L"", dwNumEditStyle,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_ED_MAXVELOCITY, pcs->hInstance, NULL
        );
        pState->hEditPerfFactor = CreateWindowExW(
            0, L"EDIT", L"", dwNumEditStyle,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_CLASSIC_ED_PERFFACTOR, pcs->hInstance, NULL
        );

        HWND edControls[] = { pState->hEditFileName, pState->hEditCfgId, pState->hEditTrainName, pState->hEditMaxVelocity, pState->hEditPerfFactor };
        for (size_t i = 0; i < _countof(edControls); ++i)
        {
            HWND hEd = edControls[i];
            if (hEd)
            {
                SendMessageW(hEd, WM_SETFONT, (WPARAM)pState->hFontNormal, MAKELPARAM(TRUE, 0));
                bool isNumeric = (hEd == pState->hEditMaxVelocity || hEd == pState->hEditPerfFactor);
                // Set left/right margin padding for clean text presentation
                SendMessageW(hEd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(9, 9));
                ModernEdit_Attach(hEd, isNumeric, true, true, false);
                EnableWindow(hEd, FALSE);
            }
        }
        if (pState->hEditFileName) SendMessageW(pState->hEditFileName, EM_SETCUEBANNER, TRUE, (LPARAM)L"filename.con");
        if (pState->hEditCfgId) SendMessageW(pState->hEditCfgId, EM_SETCUEBANNER, TRUE, (LPARAM)L"TrainCfgId");
        if (pState->hEditTrainName) SendMessageW(pState->hEditTrainName, EM_SETCUEBANNER, TRUE, (LPARAM)L"Train display name");
        if (pState->hEditMaxVelocity) SendMessageW(pState->hEditMaxVelocity, EM_SETCUEBANNER, TRUE, (LPARAM)L"120");
        if (pState->hEditPerfFactor) SendMessageW(pState->hEditPerfFactor, EM_SETCUEBANNER, TRUE, (LPARAM)L"1.00");
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HWND hwndEdit = (HWND)lParam;
        if (pState && (hwndEdit == pState->hEditFileName ||
                       hwndEdit == pState->hEditCfgId ||
                       hwndEdit == pState->hEditTrainName ||
                       hwndEdit == pState->hEditMaxVelocity ||
                       hwndEdit == pState->hEditPerfFactor))
        {
            return ModernEdit_OnCtlColor((HDC)wParam, hwndEdit);
        }
        HDC hdcEdit = (HDC)wParam;
        if (pState && hwndEdit == pState->hSearchEdit)
        {
            SetBkColor(hdcEdit, RGB(30, 33, 42));
            SetBkMode(hdcEdit, OPAQUE);
            SetTextColor(hdcEdit, RGB(240, 240, 240));
            static HBRUSH hbrEditDark = CreateSolidBrush(RGB(30, 33, 42));
            return (LRESULT)hbrEditDark;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        if (pState && (hwndStatic == pState->hEditFileName ||
                       hwndStatic == pState->hEditCfgId ||
                       hwndStatic == pState->hEditTrainName ||
                       hwndStatic == pState->hEditMaxVelocity ||
                       hwndStatic == pState->hEditPerfFactor))
        {
            return ModernEdit_OnCtlColor(hdcStatic, hwndStatic);
        }
        break;
    }

    case WM_COMMAND:
    {
        if (pState)
        {
            WORD cmdId = LOWORD(wParam);
            WORD notifyCode = HIWORD(wParam);

            if (cmdId == IDC_CLASSIC_SEARCH_EDIT && notifyCode == EN_CHANGE)
            {
                wchar_t szBuf[128] = { 0 };
                GetWindowTextW(pState->hSearchEdit, szBuf, 128);
                pState->searchQuery = szBuf;
                pState->vScroll.SetPos(0);
                SendMessageW(pState->hParent, WM_CLASSIC_METADATA_CHANGE, 99, 0);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            if (!pState->isUpdatingMetadata && notifyCode == EN_CHANGE &&
                (cmdId == IDC_CLASSIC_ED_FILENAME ||
                 cmdId == IDC_CLASSIC_ED_CFGID ||
                 cmdId == IDC_CLASSIC_ED_TRAINNAME ||
                 cmdId == IDC_CLASSIC_ED_MAXVELOCITY ||
                 cmdId == IDC_CLASSIC_ED_PERFFACTOR))
            {
                wchar_t buf[256];
                if (pState->hEditFileName) { GetWindowTextW(pState->hEditFileName, buf, 256); pState->fileName = buf; }
                if (pState->hEditCfgId) { GetWindowTextW(pState->hEditCfgId, buf, 256); pState->cfgId = buf; }
                if (pState->hEditTrainName) { GetWindowTextW(pState->hEditTrainName, buf, 256); pState->consistName = buf; }
                if (pState->hEditMaxVelocity) { GetWindowTextW(pState->hEditMaxVelocity, buf, 256); pState->maxVelocity = buf; }
                if (pState->hEditPerfFactor) { GetWindowTextW(pState->hEditPerfFactor, buf, 256); pState->perfFactor = buf; }

                SendMessageW(pState->hParent, WM_CLASSIC_METADATA_CHANGE, 0, 0);
                return 0;
            }

            if (!pState->isUpdatingMetadata && notifyCode == EN_KILLFOCUS &&
                (cmdId == IDC_CLASSIC_ED_FILENAME ||
                 cmdId == IDC_CLASSIC_ED_CFGID ||
                 cmdId == IDC_CLASSIC_ED_TRAINNAME ||
                 cmdId == IDC_CLASSIC_ED_MAXVELOCITY ||
                 cmdId == IDC_CLASSIC_ED_PERFFACTOR))
            {
                SendMessageW(pState->hParent, WM_CLASSIC_METADATA_CHANGE, 1, 0);
                return 0;
            }
        }
        break;
    }

    case WM_SIZE:
    {
        if (!pState) return 0;
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (w <= 0 || h <= 0) return 0;

        // Splitter bounds clamp
        int minListW = 320;
        int minRightW = 250;
        int maxListW = (w > (minListW + minRightW)) ? (w - minRightW) : minListW;
        if (pState->listWidth < minListW) pState->listWidth = minListW;
        if (pState->listWidth > maxListW) pState->listWidth = maxListW;

        // Middle list panel (framed card on the left)
        pState->rcListPanel = { 0, 0, pState->listWidth, h };
        pState->rcListHeader = { 0, 0, pState->listWidth, 38 };
        pState->rcListBody = { 0, 38, pState->listWidth, h };

        // Header Delete Button next to unit count badge
        pState->rcBtnDelete = { 168, 7, 240, 31 };

        // Position search box at top of list header inside sleek dark container with guaranteed spacing
        int searchW = (std::min)(140, pState->listWidth - 264);
        if (searchW < 60) searchW = 60;
        int searchX = pState->listWidth - searchW - 12;
        if (searchX < 248) searchX = 248;
        if (pState->hSearchEdit)
        {
            SetWindowPos(pState->hSearchEdit, NULL, searchX, 10, searchW, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        // Splitter (8px width matching TSCBSplitter standard)
        pState->rcSplitter = { pState->listWidth, 0, pState->listWidth + 8, h };

        // Right column
        int rightX = pState->listWidth + 8;
        int rightW = w - rightX;
        pState->rcRightPanel = { rightX, 0, w, h };

        // ===================================================================
        // TSRE-STYLE HUGE 3D PREVIEW VIEWPORT + COMPACT BOTTOM DETAILS PANEL
        // ===================================================================
        int bottomPanelH = 124;
        int previewMargin = 6;
        int previewX = rightX + previewMargin;
        int previewY = 30;
        int previewW = w - previewX - previewMargin;
        if (previewW < 200) previewW = 200;
        int previewH = h - previewY - bottomPanelH - 8;
        if (previewH < 150) previewH = 150;

        // The 3D Preview Viewport covers the ENTIRE rightmost section (Huge TSRE-style viewport!)
        pState->rcPreviewContainer = { previewX, previewY, previewX + previewW, previewY + previewH };

        // Lower Details & Telemetry Panel (spans full width directly under 3D preview)
        int teleY = previewY + previewH + 6;
        pState->rcTelemetryPanel = { previewX, teleY, previewX + previewW, h - previewMargin };

        // Sizing & Positioning Consist Details Edit Controls
        int teleW = previewW;
        int detailsW = (previewW > 900) ? 560 : ((previewW > 750) ? 460 : 380);
        if (detailsW > teleW - 300) detailsW = teleW - 300;
        if (detailsW < 320) detailsW = 320;

        int ed1X = previewX + 12;
        int ed1W = (detailsW - 36) * 44 / 100;
        if (ed1W < 110) ed1W = 110;

        int ed2X = ed1X + ed1W + 8;
        int ed2W = (detailsW - 36) * 36 / 100;
        if (ed2W < 90) ed2W = 90;

        int ed3X = ed2X + ed2W + 8;
        int ed3W = (previewX + detailsW) - ed3X;
        if (ed3W < 55) ed3W = 55;

        int ed4X = previewX + 12;
        int ed4W = (detailsW - 36) * 80 / 100;
        if (ed4W < 200) ed4W = 200;

        int ed5X = ed4X + ed4W + 8;
        int ed5W = (previewX + detailsW) - ed5X;
        if (ed5W < 55) ed5W = 55;

        int r1Y = teleY + 38;
        int r2Y = teleY + 80;
        int edH = 24;

        auto applyEditPosAndRect = [&](HWND hEd, int x, int y, int w, int h) {
            if (!hEd) return;
            SetWindowPos(hEd, NULL, x, y, w, h, SWP_NOZORDER | SWP_SHOWWINDOW);

            // Dynamically calculate vertical center offset based on active font metrics
            HDC hdc = GetDC(hEd);
            HFONT hFont = (HFONT)SendMessage(hEd, WM_GETFONT, 0, 0);
            HGDIOBJ hOldF = SelectObject(hdc, hFont ? hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            TEXTMETRICW tm;
            GetTextMetricsW(hdc, &tm);
            SelectObject(hdc, hOldF);
            ReleaseDC(hEd, hdc);

            // Center capital glyphs / numbers vertically within control height
            int topPad = (h > tm.tmHeight) ? ((h - tm.tmHeight + 1) / 2) : 0;
            RECT rcFmt = { 9, topPad, w - 9, h };
            SendMessage(hEd, EM_SETRECT, 0, (LPARAM)&rcFmt);
        };

        applyEditPosAndRect(pState->hEditFileName,    ed1X, r1Y, ed1W, edH);
        applyEditPosAndRect(pState->hEditCfgId,       ed2X, r1Y, ed2W, edH);
        applyEditPosAndRect(pState->hEditMaxVelocity, ed3X, r1Y, ed3W, edH);
        applyEditPosAndRect(pState->hEditTrainName,   ed4X, r2Y, ed4W, edH);
        applyEditPosAndRect(pState->hEditPerfFactor,  ed5X, r2Y, ed5W, edH);

        // Configure scrollbar for middle list
        auto visible = GetFilteredUnitIndices(pState);
        int totalRows = (int)visible.size();
        int rowH = 48;
        int bodyH = pState->rcListBody.bottom - pState->rcListBody.top;
        int pageRows = bodyH / rowH;
        pState->vScroll.SetBounds({ pState->rcListBody.right - 14, pState->rcListBody.top, pState->rcListBody.right, pState->rcListBody.bottom });
        pState->vScroll.SetRange(0, (std::max)(0, totalRows - 1), (std::max)(1, pageRows));
        pState->vScroll.SetVisible(totalRows * rowH > bodyH);

        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_PAINT:
    {
        if (!pState) return 0;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        // Fill main background
        HBRUSH hbrMainBg = CreateSolidBrush(RGB(18, 20, 24));
        FillRect(memDC, &rc, hbrMainBg);
        DeleteObject(hbrMainBg);

        SetBkMode(memDC, TRANSPARENT);

        // ===================================================================
        // 1. LEFT / MIDDLE COLUMN: TSRE-STYLE VERTICAL CONSIST UNIT LIST
        // ===================================================================
        HBRUSH hbrListBg = CreateSolidBrush(RGB(22, 24, 30));
        FillRect(memDC, &pState->rcListPanel, hbrListBg);
        DeleteObject(hbrListBg);

        // 1a. Header bar of framed list panel
        HBRUSH hbrHeaderBg = CreateSolidBrush(RGB(28, 30, 38));
        FillRect(memDC, &pState->rcListHeader, hbrHeaderBg);
        DeleteObject(hbrHeaderBg);

        HPEN hpBorder = CreatePen(PS_SOLID, 1, RGB(45, 50, 64));
        HPEN oldPen = (HPEN)SelectObject(memDC, hpBorder);
        MoveToEx(memDC, 0, pState->rcListHeader.bottom - 1, NULL);
        LineTo(memDC, pState->rcListPanel.right, pState->rcListHeader.bottom - 1);

        // Title: "Consist Units"
        SelectObject(memDC, pState->hFontHeader);
        SetTextColor(memDC, RGB(245, 245, 245));
        RECT rcTitle = { 10, 0, 102, 38 };
        DrawTextW(memDC, L"Consist Units", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Badge: Unit count
        int totalUnits = (int)pState->units.size();
        wchar_t szBadge[64];
        swprintf_s(szBadge, 64, L"%d Units", totalUnits);

        SelectObject(memDC, pState->hFontBadges);
        RECT rcBadge = { 104, 9, 160, 29 };
        HBRUSH hbrBadge = CreateSolidBrush(RGB(16, 75, 120));
        HPEN hpBadge = CreatePen(PS_SOLID, 1, RGB(30, 130, 210));
        HGDIOBJ oldB = SelectObject(memDC, hbrBadge);
        HGDIOBJ oldP = SelectObject(memDC, hpBadge);
        RoundRect(memDC, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 6, 6);
        SelectObject(memDC, oldB);
        SelectObject(memDC, oldP);
        DeleteObject(hbrBadge);
        DeleteObject(hpBadge);
        SetTextColor(memDC, RGB(220, 240, 255));
        DrawTextW(memDC, szBadge, -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Delete Button next to unit count badge
        COLORREF delBg = pState->isHoverDelete ? RGB(160, 40, 40) : RGB(36, 38, 48);
        COLORREF delBorder = pState->isHoverDelete ? RGB(220, 60, 60) : RGB(58, 62, 76);
        COLORREF delFg = pState->isHoverDelete ? RGB(255, 255, 255) : RGB(210, 215, 225);

        HBRUSH hbrDel = CreateSolidBrush(delBg);
        HPEN hpDel = CreatePen(PS_SOLID, 1, delBorder);
        HGDIOBJ obDel = SelectObject(memDC, hbrDel);
        HGDIOBJ opDel = SelectObject(memDC, hpDel);
        RoundRect(memDC, pState->rcBtnDelete.left, pState->rcBtnDelete.top, pState->rcBtnDelete.right, pState->rcBtnDelete.bottom, 6, 6);
        SelectObject(memDC, obDel);
        SelectObject(memDC, opDel);
        DeleteObject(hbrDel);
        DeleteObject(hpDel);

        SelectObject(memDC, pState->hFontBadges);
        SetTextColor(memDC, delFg);
        RECT rcDelText = pState->rcBtnDelete;
        DrawTextW(memDC, L"🗑 Delete", -1, &rcDelText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Rounded container card behind search edit (dark, no white box, guaranteed non-overlapping)
        if (pState->hSearchEdit)
        {
            RECT rcEditWin;
            GetWindowRect(pState->hSearchEdit, &rcEditWin);
            POINT ptEdit = { rcEditWin.left, rcEditWin.top };
            ScreenToClient(hWnd, &ptEdit);
            int eW = rcEditWin.right - rcEditWin.left;
            int eH = rcEditWin.bottom - rcEditWin.top;

            RECT rcSearchBox = { ptEdit.x - 22, ptEdit.y - 3, ptEdit.x + eW + 4, ptEdit.y + eH + 3 };
            HBRUSH hbrSearchBox = CreateSolidBrush(RGB(30, 33, 42));
            HPEN hpSearchBorder = CreatePen(PS_SOLID, 1, RGB(55, 60, 75));
            HGDIOBJ osb = SelectObject(memDC, hbrSearchBox);
            HGDIOBJ osp = SelectObject(memDC, hpSearchBorder);
            RoundRect(memDC, rcSearchBox.left, rcSearchBox.top, rcSearchBox.right, rcSearchBox.bottom, 6, 6);
            SelectObject(memDC, osb);
            SelectObject(memDC, osp);
            DeleteObject(hbrSearchBox);
            DeleteObject(hpSearchBorder);

            // Subtle magnifying glass glyph
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(140, 145, 165));
            RECT rcSearchIcon = { rcSearchBox.left + 4, rcSearchBox.top, rcSearchBox.left + 18, rcSearchBox.bottom };
            DrawTextW(memDC, L"\U0001F50D", -1, &rcSearchIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // 1b. Clean List Body background (native CustomListControl g_EditorUnitList is hosted here)
        HBRUSH hbrBodyBg = CreateSolidBrush(RGB(22, 24, 30));
        FillRect(memDC, &pState->rcListBody, hbrBodyBg);
        DeleteObject(hbrBodyBg);

        // Frame border adjoining splitter on the right
        HPEN hpPanelAdjoining = CreatePen(PS_SOLID, 1, RGB(45, 50, 64));
        HGDIOBJ oldP2 = SelectObject(memDC, hpPanelAdjoining);
        MoveToEx(memDC, pState->rcListPanel.right - 1, 0, NULL);
        LineTo(memDC, pState->rcListPanel.right - 1, h);
        SelectObject(memDC, oldP2);
        DeleteObject(hpPanelAdjoining);

        // ===================================================================
        // 2. RESIZABLE VERTICAL SPLITTER (DOUBLE LINE TSCB STYLE)
        // ===================================================================
        HBRUSH hbrSplitter = CreateSolidBrush(RGB(32, 32, 32));
        FillRect(memDC, &pState->rcSplitter, hbrSplitter);
        DeleteObject(hbrSplitter);

        // Double 1px border lines matching TSCBSplitter
        COLORREF clrSplitLine = pState->isDraggingSplitter ? RGB(0, 160, 255) : RGB(65, 65, 65);
        HPEN hpSplit = CreatePen(PS_SOLID, 1, clrSplitLine);
        HGDIOBJ oldSp = SelectObject(memDC, hpSplit);
        // Left line
        MoveToEx(memDC, pState->rcSplitter.left, 0, NULL);
        LineTo(memDC, pState->rcSplitter.left, h);
        // Right line
        MoveToEx(memDC, pState->rcSplitter.right - 1, 0, NULL);
        LineTo(memDC, pState->rcSplitter.right - 1, h);
        SelectObject(memDC, oldSp);
        DeleteObject(hpSplit);

        // ===================================================================
        // 3. RIGHT COLUMN: HUGE 3D PREVIEW VIEWPORT + COMPACT BOTTOM DETAILS
        // ===================================================================

        // 3a. Huge Preview Header Bar
        RECT rcPreviewHeader = { pState->rcPreviewContainer.left, 4, pState->rcPreviewContainer.right, 26 };
        SelectObject(memDC, pState->hFontHeader);
        SetTextColor(memDC, RGB(220, 225, 235));
        DrawTextW(memDC, L"3D LIVE UNIT PREVIEW", -1, &rcPreviewHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Huge Preview Card Border & Background
        HPEN hpPreviewBorder = CreatePen(PS_SOLID, 1, RGB(42, 46, 56));
        HBRUSH hbrPreviewBg = CreateSolidBrush(RGB(12, 14, 18));
        HGDIOBJ oldPrB = SelectObject(memDC, hbrPreviewBg);
        HGDIOBJ oldPrP = SelectObject(memDC, hpPreviewBorder);
        RoundRect(memDC, pState->rcPreviewContainer.left, pState->rcPreviewContainer.top, pState->rcPreviewContainer.right, pState->rcPreviewContainer.bottom, 6, 6);
        SelectObject(memDC, oldPrB);
        SelectObject(memDC, oldPrP);
        DeleteObject(hbrPreviewBg);
        DeleteObject(hpPreviewBorder);

        // 3b. Bottom Details & Telemetry Panel (spans full width under 3D preview)
        HBRUSH hbrTeleBg = CreateSolidBrush(RGB(22, 24, 30));
        HPEN hpTeleBorder = CreatePen(PS_SOLID, 1, RGB(42, 46, 58));
        HGDIOBJ otb = SelectObject(memDC, hbrTeleBg);
        HGDIOBJ otp = SelectObject(memDC, hpTeleBorder);
        RoundRect(memDC, pState->rcTelemetryPanel.left, pState->rcTelemetryPanel.top, pState->rcTelemetryPanel.right, pState->rcTelemetryPanel.bottom, 6, 6);
        SelectObject(memDC, otb);
        SelectObject(memDC, otp);
        DeleteObject(hbrTeleBg);
        DeleteObject(hpTeleBorder);

        // -------------------------------------------------------------------
        // 3b. CONSIST DETAILS (EDITABLE CONTROLS) & PHYSICAL TOTALS PANEL
        // -------------------------------------------------------------------
        int teleX = pState->rcTelemetryPanel.left;
        int teleY = pState->rcTelemetryPanel.top;
        int teleW = pState->rcTelemetryPanel.right - teleX;
        int teleH = pState->rcTelemetryPanel.bottom - teleY;

        int detailsW = (teleW > 900) ? 560 : ((teleW > 750) ? 460 : 380);
        if (detailsW > teleW - 300) detailsW = teleW - 300;
        if (detailsW < 320) detailsW = 320;

        int totalsX = teleX + detailsW + 16;
        int totalsW = (teleX + teleW) - totalsX - 8;

        // --- Left: CONSIST DETAILS ---
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, RGB(100, 180, 255));
        RECT rcDetailsHeader = { teleX + 12, teleY + 4, teleX + detailsW, teleY + 20 };
        DrawTextW(memDC, L"CONSIST DETAILS", -1, &rcDetailsHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Calculate positions for row 1 & row 2 labels
        int ed1X = teleX + 12;
        int ed1W = (detailsW - 36) * 44 / 100;
        if (ed1W < 110) ed1W = 110;

        int ed2X = ed1X + ed1W + 8;
        int ed2W = (detailsW - 36) * 36 / 100;
        if (ed2W < 90) ed2W = 90;

        int ed3X = ed2X + ed2W + 8;
        int ed3W = (teleX + detailsW) - ed3X;
        if (ed3W < 55) ed3W = 55;

        int ed4X = teleX + 12;
        int ed4W = (detailsW - 36) * 80 / 100;
        if (ed4W < 200) ed4W = 200;

        int ed5X = ed4X + ed4W + 8;
        int ed5W = (teleX + detailsW) - ed5X;
        if (ed5W < 55) ed5W = 55;

        // Labels for Edit Controls (Horizontally Centered Over Each Field)
        bool bHasConsist = (!pState->fileName.empty() || !pState->consistName.empty() || !pState->units.empty());
        COLORREF labelColor = bHasConsist ? RGB(140, 145, 160) : RGB(100, 100, 110);
        SelectObject(memDC, pState->hFontSmall);
        SetTextColor(memDC, labelColor);

        RECT rcLblFile = { ed1X, teleY + 20, ed1X + ed1W, teleY + 36 };
        DrawTextW(memDC, L"File Name (.con)", -1, &rcLblFile, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcLblCfg = { ed2X, teleY + 20, ed2X + ed2W, teleY + 36 };
        DrawTextW(memDC, L"Consist Identifier", -1, &rcLblCfg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcLblVel = { ed3X, teleY + 20, ed3X + ed3W, teleY + 36 };
        DrawTextW(memDC, L"Speed (km/h)", -1, &rcLblVel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcLblName = { ed4X, teleY + 62, ed4X + ed4W, teleY + 78 };
        std::wstring nameLbl = L"Train Display Name";
        if (pState->isDirty) nameLbl += L"  ● Modified";
        DrawTextW(memDC, nameLbl.c_str(), -1, &rcLblName, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcLblPerf = { ed5X, teleY + 62, ed5X + ed5W, teleY + 78 };
        DrawTextW(memDC, L"Perf (%)", -1, &rcLblPerf, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Edit control 1px border outlines are handled automatically by ModernEditControl
        // (no manual border overdrawing needed)

        // Divider between Details and Totals
        HPEN hpDivMid = CreatePen(PS_SOLID, 1, RGB(42, 46, 58));
        HGDIOBJ oldDivMid = SelectObject(memDC, hpDivMid);
        MoveToEx(memDC, totalsX - 10, teleY + 8, NULL);
        LineTo(memDC, totalsX - 10, teleY + teleH - 8);
        SelectObject(memDC, oldDivMid);
        DeleteObject(hpDivMid);

        // --- Right: CONSIST TOTALS ---
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, RGB(100, 180, 255));
        RECT rcTotalsHeader = { totalsX, teleY + 4, teleX + teleW - 12, teleY + 20 };
        DrawTextW(memDC, L"CONSIST TOTALS", -1, &rcTotalsHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3 Columns x 2 Rows of Physical Totals (Centered in Each Column)
        int statColW = totalsW / 3;
        int stat1X = totalsX;
        int stat2X = stat1X + statColW;
        int stat3X = stat2X + statColW;

        auto drawStatCell = [&](int x, int y, int colW, const wchar_t* label, const std::wstring& value, COLORREF valColor) {
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(130, 135, 150));
            RECT rcL = { x, y, x + colW, y + 16 };
            DrawTextW(memDC, label, -1, &rcL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, pState->hFontNormal);
            SetTextColor(memDC, valColor);
            RECT rcV = { x, y + 16, x + colW, y + 38 };
            DrawTextW(memDC, value.c_str(), -1, &rcV, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };

        int statRow1Y = teleY + 22;
        int statRow2Y = teleY + 64;

        wchar_t szTotalMass[64], szEngMass[64], szWagMass[64], szLen[64], szUnits[64], szPower[64];
        double massT = pState->metrics.totalMass;
        double engT = pState->metrics.engineMass;
        double wagT = pState->metrics.wagonMass;
        double lenM = pState->metrics.totalLength;
        double lenFt = lenM * 3.28084;
        double hp = pState->metrics.totalPowerHP;

        swprintf_s(szTotalMass, 64, L"%.2f T", massT);
        swprintf_s(szEngMass,   64, L"%.2f T", engT);
        swprintf_s(szWagMass,   64, L"%.2f T", wagT);
        swprintf_s(szLen,       64, L"%.1f m (%.0f ft)", lenM, lenFt);
        swprintf_s(szUnits,     64, L"%d", (int)pState->units.size());
        swprintf_s(szPower,     64, L"%.0f HP", hp);

        // Row 1
        drawStatCell(stat1X, statRow1Y, statColW - 6, L"Total Mass", szTotalMass, RGB(160, 205, 250));
        drawStatCell(stat2X, statRow1Y, statColW - 6, L"Eng Mass",   szEngMass,   RGB(245, 175, 80));
        drawStatCell(stat3X, statRow1Y, statColW - 6, L"Units",      szUnits,     RGB(100, 210, 160));

        // Row 2
        drawStatCell(stat1X, statRow2Y, statColW - 6, L"Length",     szLen,       RGB(160, 205, 250));
        drawStatCell(stat2X, statRow2Y, statColW - 6, L"Wag Mass",   szWagMass,   RGB(245, 175, 80));
        drawStatCell(stat3X, statRow2Y, statColW - 6, L"Power",      szPower,     RGB(200, 160, 250));

        // Blit buffer to screen
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!pState) return 0;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (pState->vScroll.OnMouseWheel(delta, 3, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // 1. Resizing splitter (live repositioning right 3D viewport)
        if (pState->isDraggingSplitter)
        {
            int deltaX = pt.x - pState->splitterDragStartX;
            int newW = pState->splitterStartWidth + deltaX;
            RECT rc;
            GetClientRect(hWnd, &rc);
            int minListW = 320;
            int maxListW = (rc.right > (minListW + 250)) ? (rc.right - 250) : minListW;
            if (newW < minListW) newW = minListW;
            if (newW > maxListW) newW = maxListW;

            if (pState->listWidth != newW)
            {
                pState->listWidth = newW;
                SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
                SendMessageW(pState->hParent, WM_CLASSIC_SPLITTER_RESIZED, 0, 0);
            }
            return 0;
        }

        // 2. Resequencing unit drag
        if (pState->isDraggingItem)
        {
            POINT ptScreen = pt;
            ClientToScreen(hWnd, &ptScreen);
            FluentDragGhost::Move(ptScreen, true, L"Resequence Unit");

            int rowH = 48;
            int bodyTop = pState->rcListBody.top;
            int scrollY = pState->vScroll.GetPos();
            int hoveredRow = scrollY + (pt.y - bodyTop) / rowH;
            auto visible = GetFilteredUnitIndices(pState);

            if (hoveredRow < 0) hoveredRow = 0;
            if (hoveredRow > (int)visible.size()) hoveredRow = (int)visible.size();

            int targetRealIdx = (hoveredRow < (int)visible.size()) ? visible[hoveredRow] : (int)pState->units.size();
            if (pState->dropTargetIdx != targetRealIdx)
            {
                pState->dropTargetIdx = targetRealIdx;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        // 3. Hover detection
        int prevHoverRow = pState->hoverRow;
        int prevHoverBtn = pState->hoverBtn;

        pState->hoverRow = -1;
        pState->hoverBtn = 0;

        if (PtInRect(&pState->rcListBody, pt))
        {
            int rowH = 48;
            int bodyTop = pState->rcListBody.top;
            int scrollY = pState->vScroll.GetPos();
            int vIdx = scrollY + (pt.y - bodyTop) / rowH;
            auto visible = GetFilteredUnitIndices(pState);

            if (vIdx >= 0 && vIdx < (int)visible.size())
            {
                pState->hoverRow = visible[vIdx];
                int itemRight = pState->rcListPanel.right - (pState->vScroll.IsVisible() ? 14 : 0);
                int itemY = bodyTop + (vIdx - scrollY) * rowH;

                RECT rcFlip = { itemRight - 68, itemY + 13, itemRight - 28, itemY + 35 };
                RECT rcDel = { itemRight - 24, itemY + 13, itemRight - 4, itemY + 35 };

                if (PtInRect(&rcFlip, pt)) pState->hoverBtn = 1;
                else if (PtInRect(&rcDel, pt)) pState->hoverBtn = 2;
            }
        }

        bool prevHoverDel = pState->isHoverDelete;
        pState->isHoverDelete = PtInRect(&pState->rcBtnDelete, pt);

        if (pState->vScroll.OnMouseMove(pt, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }

        if (prevHoverRow != pState->hoverRow || prevHoverBtn != pState->hoverBtn || prevHoverDel != pState->isHoverDelete)
        {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (!pState) return 0;
        pState->hoverRow = -1;
        pState->hoverBtn = 0;
        pState->isHoverDelete = false;
        pState->vScroll.OnMouseLeave(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (!pState) return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);

        if (PtInRect(&pState->rcSplitter, pt) || pState->isDraggingSplitter)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        if (pState->hoverBtn > 0 || pState->isHoverDelete)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Do not steal focus if user clicked directly on one of our child edit controls
        HWND hHit = ChildWindowFromPoint(hWnd, pt);
        if (hHit && hHit != hWnd)
        {
            SetFocus(hHit);
            return 0;
        }

        SetFocus(hWnd);

        // 1. Splitter drag start
        if (PtInRect(&pState->rcSplitter, pt))
        {
            pState->isDraggingSplitter = true;
            pState->hasUserDraggedSplitter = true;
            pState->splitterDragStartX = pt.x;
            pState->splitterStartWidth = pState->listWidth;
            SetCapture(hWnd);
            InvalidateRect(hWnd, &pState->rcSplitter, FALSE);
            return 0;
        }

        // 2. Scrollbar click
        if (pState->vScroll.OnLButtonDown(pt, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // 2b. Header Delete Button click
        if (PtInRect(&pState->rcBtnDelete, pt))
        {
            SendMessageW(pState->hParent, WM_CLASSIC_UNIT_DELETE, (WPARAM)-1, 0);
            return 0;
        }

        // 3. Middle unit list click
        if (PtInRect(&pState->rcListBody, pt))
        {
            int rowH = 48;
            int bodyTop = pState->rcListBody.top;
            int scrollY = pState->vScroll.GetPos();
            int vIdx = scrollY + (pt.y - bodyTop) / rowH;
            auto visible = GetFilteredUnitIndices(pState);

            if (vIdx >= 0 && vIdx < (int)visible.size())
            {
                int realIdx = visible[vIdx];
                int itemRight = pState->rcListPanel.right - (pState->vScroll.IsVisible() ? 14 : 0);
                int itemY = bodyTop + (vIdx - scrollY) * rowH;

                RECT rcCheck = { 34, itemY + 15, 52, itemY + 33 };
                RECT rcFlip = { itemRight - 68, itemY + 13, itemRight - 28, itemY + 35 };
                RECT rcDel = { itemRight - 24, itemY + 13, itemRight - 4, itemY + 35 };

                // 3a. Checkbox click
                if (PtInRect(&rcCheck, pt))
                {
                    if (pState->checkedUnits.count(realIdx) > 0) pState->checkedUnits.erase(realIdx);
                    else pState->checkedUnits.insert(realIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // 3b. Flip button click
                if (PtInRect(&rcFlip, pt))
                {
                    if (realIdx < (int)pState->units.size())
                    {
                        pState->units[realIdx].isFlipped = !pState->units[realIdx].isFlipped;
                        SendMessageW(pState->hParent, WM_CLASSIC_UNIT_FLIP, (WPARAM)realIdx, 0);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }

                // 3c. Delete button click
                if (PtInRect(&rcDel, pt))
                {
                    SendMessageW(pState->hParent, WM_CLASSIC_UNIT_DELETE, (WPARAM)realIdx, 0);
                    return 0;
                }

                // 3d. Row selection & prepare for drag
                bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (bCtrl)
                {
                    if (pState->selUnits.count(realIdx) > 0) pState->selUnits.erase(realIdx);
                    else pState->selUnits.insert(realIdx);
                    pState->focusedIndex = realIdx;
                }
                else if (bShift && pState->anchorIndex >= 0)
                {
                    pState->selUnits.clear();
                    int start = (std::min)(pState->anchorIndex, realIdx);
                    int end = (std::max)(pState->anchorIndex, realIdx);
                    for (int k = start; k <= end; ++k) pState->selUnits.insert(k);
                    pState->focusedIndex = realIdx;
                }
                else
                {
                    pState->selUnits.clear();
                    pState->selUnits.insert(realIdx);
                    pState->anchorIndex = realIdx;
                    pState->focusedIndex = realIdx;
                }

                pState->isDraggingItem = false;
                pState->dragSourceIdx = realIdx;
                pState->dropTargetIdx = realIdx;
                pState->ptDragStart = pt;
                SetCapture(hWnd);

                // Notify parent immediately about unit selection so 3D UnitPreviewCard updates live!
                SendMessageW(pState->hParent, WM_CLASSIC_UNIT_SELECTED, (WPARAM)realIdx, 0);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) return 0;
        ReleaseCapture();

        if (pState->isDraggingSplitter)
        {
            pState->isDraggingSplitter = false;
            SendMessageW(pState->hParent, WM_CLASSIC_SPLITTER_RESIZED, 0, 0);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->vScroll.OnLButtonUp({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->isDraggingItem)
        {
            pState->isDraggingItem = false;
            FluentDragGhost::Hide();

            if (pState->dragSourceIdx >= 0 && pState->dropTargetIdx >= 0 && pState->dragSourceIdx != pState->dropTargetIdx)
            {
                SendMessageW(pState->hParent, WM_CLASSIC_UNITS_REORDER, (WPARAM)pState->dragSourceIdx, (LPARAM)pState->dropTargetIdx);
            }
            pState->dragSourceIdx = -1;
            pState->dropTargetIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        pState->dragSourceIdx = -1;
        pState->dropTargetIdx = -1;
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&pState->rcListBody, pt))
        {
            int rowH = 48;
            int bodyTop = pState->rcListBody.top;
            int scrollY = pState->vScroll.GetPos();
            int vIdx = scrollY + (pt.y - bodyTop) / rowH;
            auto visible = GetFilteredUnitIndices(pState);

            if (vIdx >= 0 && vIdx < (int)visible.size())
            {
                int realIdx = visible[vIdx];
                if (pState->selUnits.count(realIdx) == 0)
                {
                    pState->selUnits.clear();
                    pState->selUnits.insert(realIdx);
                    pState->focusedIndex = realIdx;
                    SendMessageW(pState->hParent, WM_CLASSIC_UNIT_SELECTED, (WPARAM)realIdx, 0);
                    InvalidateRect(hWnd, NULL, FALSE);
                }

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);
                SendMessageW(pState->hParent, WM_CLASSIC_CONTEXT_MENU, (WPARAM)ptScreen.x, (LPARAM)ptScreen.y);
            }
        }
        return 0;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontHeader) DeleteObject(pState->hFontHeader);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontNormal) DeleteObject(pState->hFontNormal);
            if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
            if (pState->hFontBadges) DeleteObject(pState->hFontBadges);
            if (pState->hFontTelemetryVal) DeleteObject(pState->hFontTelemetryVal);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
            delete pState;
            SetPropW(hWnd, CLASSIC_VIEW_PROP, NULL);
        }
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL RegisterClassicConsistViewClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ClassicConsistViewProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // double buffered
    wc.lpszClassName = CLASSIC_CONSIST_VIEW_CLASS;

    return RegisterClassExW(&wc) != 0;
}

HWND CreateClassicConsistView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, UINT_PTR id)
{
    RegisterClassicConsistViewClass(hInstance);

    return CreateWindowExW(
        WS_EX_CONTROLPARENT, CLASSIC_CONSIST_VIEW_CLASS, L"ClassicConsistView",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, w, h,
        hParent, (HMENU)id, hInstance, NULL
    );
}

void ClassicConsistView_SetUnits(
    HWND hWnd,
    const std::vector<ConsistReader::UnitInfo>& units,
    const std::wstring& basePath,
    const std::wstring& consistName,
    const std::wstring& fileName)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;

    pState->units = units;
    pState->basePath = basePath;
    if (!consistName.empty()) pState->consistName = consistName;
    if (!fileName.empty()) pState->fileName = fileName;

    // Default select unit 0 if nothing selected
    if (pState->focusedIndex < 0 && !pState->units.empty())
    {
        pState->focusedIndex = 0;
        pState->selUnits.insert(0);
    }

    RecalculateMetrics(pState);

    // Update scrollbar bounds & range
    RECT rc;
    GetClientRect(hWnd, &rc);
    SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    InvalidateRect(hWnd, NULL, FALSE);
}

void ClassicConsistView_SetSelectedIndices(HWND hWnd, const std::vector<int>& selIndices)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;

    pState->selUnits.clear();
    for (int idx : selIndices)
    {
        if (idx >= 0 && idx < (int)pState->units.size())
            pState->selUnits.insert(idx);
    }
    if (!selIndices.empty()) pState->focusedIndex = selIndices[0];
    InvalidateRect(hWnd, NULL, FALSE);
}

std::vector<int> ClassicConsistView_GetSelectedIndices(HWND hWnd)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return {};

    std::vector<int> res(pState->selUnits.begin(), pState->selUnits.end());
    std::sort(res.begin(), res.end());
    return res;
}

std::vector<int> ClassicConsistView_GetCheckedIndices(HWND hWnd)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return {};

    std::vector<int> res(pState->checkedUnits.begin(), pState->checkedUnits.end());
    std::sort(res.begin(), res.end());
    return res;
}

void ClassicConsistView_ClearSelection(HWND hWnd)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    pState->selUnits.clear();
    pState->focusedIndex = -1;
    InvalidateRect(hWnd, NULL, FALSE);
}

void ClassicConsistView_SetDarkMode(HWND hWnd, BOOL bDark)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    pState->bDarkMode = bDark;
    InvalidateRect(hWnd, NULL, FALSE);
}

void ClassicConsistView_SetDirty(HWND hWnd, bool isDirty)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    if (pState->isDirty != isDirty)
    {
        pState->isDirty = isDirty;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

void ClassicConsistView_GetPreviewCardRect(HWND hWnd, RECT* outRect)
{
    if (!outRect) return;
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) { *outRect = { 0, 0, 0, 0 }; return; }
    *outRect = pState->rcPreviewContainer;
}

void ClassicConsistView_SetConsistMetadata(
    HWND hWnd,
    const std::wstring& fileName,
    const std::wstring& cfgId,
    const std::wstring& trainName,
    const std::wstring& maxVelocity,
    const std::wstring& perfFactor)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;

    pState->isUpdatingMetadata = true;
    pState->fileName = fileName;
    pState->cfgId = cfgId;
    pState->consistName = trainName;
    pState->maxVelocity = maxVelocity;
    pState->perfFactor = perfFactor;

    if (pState->hEditFileName) SetWindowTextW(pState->hEditFileName, fileName.c_str());
    if (pState->hEditCfgId) SetWindowTextW(pState->hEditCfgId, cfgId.c_str());
    if (pState->hEditTrainName) SetWindowTextW(pState->hEditTrainName, trainName.c_str());
    if (pState->hEditMaxVelocity) SetWindowTextW(pState->hEditMaxVelocity, maxVelocity.c_str());
    if (pState->hEditPerfFactor) SetWindowTextW(pState->hEditPerfFactor, perfFactor.c_str());

    bool bHasConsist = (!fileName.empty() || !trainName.empty() || !pState->units.empty());
    if (pState->hEditFileName) EnableWindow(pState->hEditFileName, bHasConsist);
    if (pState->hEditCfgId) EnableWindow(pState->hEditCfgId, bHasConsist);
    if (pState->hEditTrainName) EnableWindow(pState->hEditTrainName, bHasConsist);
    if (pState->hEditMaxVelocity)
    {
        EnableWindow(pState->hEditMaxVelocity, bHasConsist);
        SendMessageW(pState->hEditMaxVelocity, EM_SETCUEBANNER, TRUE, (LPARAM)L"120");
    }
    if (pState->hEditPerfFactor)
    {
        EnableWindow(pState->hEditPerfFactor, bHasConsist);
        SendMessageW(pState->hEditPerfFactor, EM_SETCUEBANNER, TRUE, (LPARAM)L"1.00");
    }

    pState->isUpdatingMetadata = false;
    InvalidateRect(hWnd, NULL, FALSE);
}

void ClassicConsistView_GetConsistMetadata(
    HWND hWnd,
    std::wstring& outFileName,
    std::wstring& outCfgId,
    std::wstring& outTrainName,
    std::wstring& outMaxVelocity,
    std::wstring& outPerfFactor)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;

    outFileName = pState->fileName;
    outCfgId = pState->cfgId;
    outTrainName = pState->consistName;
    outMaxVelocity = pState->maxVelocity;
    outPerfFactor = pState->perfFactor;
}

void ClassicConsistView_SetMaxVelocity(HWND hWnd, const std::wstring& val)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    pState->isUpdatingMetadata = true;
    pState->maxVelocity = val;
    if (pState->hEditMaxVelocity) SetWindowTextW(pState->hEditMaxVelocity, val.c_str());
    pState->isUpdatingMetadata = false;
    InvalidateRect(hWnd, NULL, FALSE);
}

void ClassicConsistView_SetPerfFactor(HWND hWnd, const std::wstring& val)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    pState->isUpdatingMetadata = true;
    pState->perfFactor = val;
    if (pState->hEditPerfFactor) SetWindowTextW(pState->hEditPerfFactor, val.c_str());
    pState->isUpdatingMetadata = false;
    InvalidateRect(hWnd, NULL, FALSE);
}

int ClassicConsistView_GetDropIndexFromPoint(HWND hWnd, POINT ptScreen)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return -1;

    POINT pt = ptScreen;
    ScreenToClient(hWnd, &pt);

    if (!PtInRect(&pState->rcListBody, pt)) return -1;

    int rowH = 48;
    int bodyTop = pState->rcListBody.top;
    int scrollY = pState->vScroll.GetPos();
    int vIdx = scrollY + (pt.y - bodyTop) / rowH;
    auto visible = GetFilteredUnitIndices(pState);

    if (vIdx < 0) return 0;
    if (vIdx >= (int)visible.size()) return (int)pState->units.size();
    return visible[vIdx];
}

void ClassicConsistView_SetDropHighlight(HWND hWnd, int dropIndex)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    if (pState->stockDropIndex != dropIndex)
    {
        pState->stockDropIndex = dropIndex;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

void ClassicConsistView_ClearDropHighlight(HWND hWnd)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    if (pState->stockDropIndex != -1)
    {
        pState->stockDropIndex = -1;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

void ClassicConsistView_GetUnitListRect(HWND hWnd, RECT* outRect)
{
    if (!outRect) return;
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) { *outRect = { 0, 0, 0, 0 }; return; }
    *outRect = pState->rcListBody;
}

void ClassicConsistView_SetDesiredListWidth(HWND hWnd, int desiredWidth)
{
    if (!hWnd || !IsWindow(hWnd)) return;
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return;
    if (pState->hasUserDraggedSplitter) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    int minListW = 320;
    int minRightW = 250;
    int maxListW = (rc.right > (minListW + minRightW)) ? (rc.right - minRightW) : minListW;
    int newW = desiredWidth;
    if (newW < minListW) newW = minListW;
    if (newW > maxListW) newW = maxListW;

    if (pState->listWidth != newW)
    {
        pState->listWidth = newW;
        SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
        SendMessageW(pState->hParent, WM_CLASSIC_SPLITTER_RESIZED, 0, 0);
    }
}

std::wstring ClassicConsistView_GetSearchQuery(HWND hWnd)
{
    ClassicConsistViewState* pState = (ClassicConsistViewState*)GetPropW(hWnd, CLASSIC_VIEW_PROP);
    if (!pState) return L"";
    return pState->searchQuery;
}
