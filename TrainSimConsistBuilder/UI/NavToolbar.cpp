#include "NavToolbar.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#define IDC_NAV_EDIT_ADDR   2105
#define IDC_NAV_EDIT_SEARCH 2106

enum NavIconIndex
{
    ICON_NONE = -1,
    ICON_BACK = 0,
    ICON_FORWARD = 1,
    ICON_UP = 2,
    ICON_REFRESH = 3,
    ICON_ADDR_PILL = 5,
    ICON_ADDR_CLEAR = 6,
    ICON_BC_BASE = 100 // Indices 100+ reserved for breadcrumb elements
};

enum BreadcrumbType
{
    BC_ROOT_PC,       // "This PC" monitor icon
    BC_CHEVRON_ROOT,  // Chevron after "This PC"
    BC_ELLIPSIS,      // "..." collapsed ancestor indicator
    BC_CHEVRON_ELLIP, // Chevron after "..."
    BC_SEGMENT,       // Folder name or Drive letter
    BC_CHEVRON        // Chevron after folder segment
};

struct BreadcrumbElement
{
    BreadcrumbType type;
    RECT rc;
    std::wstring label;
    std::wstring fullPath;
    int segmentIndex;
};

struct NavMenuItem
{
    std::wstring label;
    std::wstring targetPath;
    bool isEnabled;
};

struct NavToolbarState
{
    BOOL bDarkMode = TRUE;
    HFONT hFontMain = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontChevron = NULL;
    HBRUSH hbrPillDark = NULL;
    HBRUSH hbrPillEditDark = NULL;
    HBRUSH hbrPillLight = NULL;

    HWND hEditAddr = NULL;
    HWND hEditSearch = NULL;

    int hoverIndex = ICON_NONE;
    int pressedIndex = ICON_NONE;
    int hoverBreadcrumb = -1;
    BOOL bTrackingMouse = FALSE;
    BOOL bEditingAddress = FALSE;
    BOOL bSearchFocused = FALSE;
    BOOL bInternalChange = FALSE;

    wchar_t szCurrentPath[MAX_PATH] = L"C:\\TrainSim\\TRAINS\\CONSISTS";
    std::vector<BreadcrumbElement> breadcrumbElements;
    std::vector<std::pair<std::wstring, std::wstring>> collapsedFolders;
};

static NavToolbarState g_State;

typedef enum _ACCENT_STATE
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5,
    ACCENT_INVALID_STATE = 6
} ACCENT_STATE;

typedef struct _ACCENT_POLICY
{
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef enum _WINDOWCOMPOSITIONATTRIB
{
    WCA_ACCENT_POLICY = 19
} WINDOWCOMPOSITIONATTRIB;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static void EnableWindowAcrylicBlur(HWND hWnd, DWORD gradientColor = 0xD9202020)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser)
    {
        pfnSetWindowCompositionAttribute setWindowCompositionAttribute =
            (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        if (setWindowCompositionAttribute)
        {
            ACCENT_POLICY accent = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 2, gradientColor, 0 };
            WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &accent, sizeof(accent) };
            setWindowCompositionAttribute(hWnd, &data);
        }
    }
}

class NavBreadcrumbMenu
{
public:
    static bool Register(HINSTANCE hInstance)
    {
        WNDCLASSEXW wcx = { 0 };
        wcx.cbSize        = sizeof(wcx);
        wcx.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcx.lpfnWndProc   = NavBreadcrumbMenu::WndProc;
        wcx.cbWndExtra    = sizeof(NavBreadcrumbMenu*);
        wcx.hInstance     = hInstance;
        wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wcx.hbrBackground = NULL;
        wcx.lpszClassName = L"NavBreadcrumbMenuClass";

        return (RegisterClassExW(&wcx) != 0);
    }

    static void Show(HWND hParent, int x, int y, const std::vector<NavMenuItem>& items)
    {
        static NavBreadcrumbMenu s_instance;
        s_instance.ShowInternal(hParent, x, y, items);
    }

private:
    HWND m_hWnd = NULL;
    HWND m_hParent = NULL;
    std::vector<NavMenuItem> m_items;
    int m_hoverIndex = -1;
    bool m_bTrackingMouse = false;
    int m_itemHeight = 32;

    void ShowInternal(HWND hParent, int x, int y, const std::vector<NavMenuItem>& items)
    {
        if (m_hWnd && IsWindow(m_hWnd))
        {
            DestroyWindow(m_hWnd);
            m_hWnd = NULL;
        }

        m_hParent = hParent;
        m_items = items;
        m_hoverIndex = -1;
        m_bTrackingMouse = false;
        m_itemHeight = 32;

        if (m_items.empty()) return;

        // Measure longest label to set optimal menu width
        HDC hdcScreen = GetDC(NULL);
        HFONT hOldFont = (HFONT)SelectObject(hdcScreen, g_State.hFontMain ? g_State.hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        int maxTextW = 120;
        for (const auto& it : m_items)
        {
            SIZE sz;
            GetTextExtentPoint32W(hdcScreen, it.label.c_str(), (int)it.label.length(), &sz);
            if (sz.cx > maxTextW) maxTextW = sz.cx;
        }
        SelectObject(hdcScreen, hOldFont);
        ReleaseDC(NULL, hdcScreen);

        int clientW = maxTextW + 36; // 18px padding on each side
        if (clientW < 180) clientW = 180;
        if (clientW > 480) clientW = 480;

        int clientH = 8 + (int)m_items.size() * m_itemHeight;
        if (clientH > 520) clientH = 520;

        int width = clientW;
        int height = clientH;

        // Keep within work area
        HMONITOR hMon = MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(hMon, &mi);

        if (x + width > mi.rcWork.right) x = mi.rcWork.right - width - 4;
        if (x < mi.rcWork.left) x = mi.rcWork.left + 4;
        if (y + height > mi.rcWork.bottom) y = y - height - 36;

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
        Register(hInst);

        m_hWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"NavBreadcrumbMenuClass",
            L"",
            WS_POPUP,
            x, y, width, height,
            hParent, NULL, hInst, this
        );

        if (m_hWnd)
        {
            // Windows 11 DWM Rounded Corners & Dark Mode
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            BOOL useDark = TRUE;
            DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

            // Set rounded clipping region (8px radius)
            HRGN hRgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 16, 16);
            SetWindowRgn(m_hWnd, hRgn, TRUE);

            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
            SetCapture(m_hWnd);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        NavBreadcrumbMenu* pThis = (NavBreadcrumbMenu*)GetWindowLongPtr(hWnd, 0);

        switch (msg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            pThis = (NavBreadcrumbMenu*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, 0, (LONG_PTR)pThis);
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }

        case WM_ERASEBKGND:
            return TRUE;

        case WM_MOUSEMOVE:
        {
            if (!pThis) break;
            int y = GET_Y_LPARAM(lParam) - 4;
            int newHover = y >= 0 ? (y / pThis->m_itemHeight) : -1;
            if (newHover >= (int)pThis->m_items.size()) newHover = -1;

            if (!pThis->m_bTrackingMouse)
            {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
                TrackMouseEvent(&tme);
                pThis->m_bTrackingMouse = true;
            }

            if (newHover != pThis->m_hoverIndex)
            {
                pThis->m_hoverIndex = newHover;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            if (pThis)
            {
                pThis->m_bTrackingMouse = false;
                pThis->m_hoverIndex = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            if (!pThis) break;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetClientRect(hWnd, &rc);

            if (!PtInRect(&rc, pt))
            {
                ReleaseCapture();
                DestroyWindow(hWnd);
                return 0;
            }

            int idx = (pt.y - 4) / pThis->m_itemHeight;
            if (idx >= 0 && idx < (int)pThis->m_items.size())
            {
                if (pThis->m_items[idx].isEnabled && !pThis->m_items[idx].targetPath.empty())
                {
                    std::wstring target = pThis->m_items[idx].targetPath;
                    HWND hParent = pThis->m_hParent;
                    ReleaseCapture();
                    DestroyWindow(hWnd);

                    NavToolbar_SetPath(hParent, target.c_str());
                    SendMessage(GetParent(hParent), WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hParent);
                    return 0;
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                ReleaseCapture();
                DestroyWindow(hWnd);
                return 0;
            }
            break;

        case WM_KILLFOCUS:
            ReleaseCapture();
            DestroyWindow(hWnd);
            return 0;

        case WM_PAINT:
        {
            if (!pThis) break;
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
            HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

            // 1. Fill Deep Dark Explorer Surface background (RGB 28, 28, 28)
            COLORREF bgCol = RGB(28, 28, 28);
            COLORREF borderCol = RGB(55, 55, 55);

            HBRUSH hbrBg = CreateSolidBrush(bgCol);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);

            HBRUSH hOldBr = (HBRUSH)SelectObject(memDC, hbrBg);
            HPEN hOldPen = (HPEN)SelectObject(memDC, hPenBorder);

            RoundRect(memDC, 0, 0, rcClient.right, rcClient.bottom, 16, 16);

            SelectObject(memDC, hOldBr);
            SelectObject(memDC, hOldPen);
            DeleteObject(hbrBg);
            DeleteObject(hPenBorder);

            // 2. Draw Menu Items & Hover Capsules
            HFONT hFont = g_State.hFontMain ? g_State.hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT hOldF = (HFONT)SelectObject(memDC, hFont);
            SetBkMode(memDC, TRANSPARENT);

            HPEN hNullP = CreatePen(PS_NULL, 0, 0);
            HPEN hPrevP = (HPEN)SelectObject(memDC, hNullP);

            for (size_t i = 0; i < pThis->m_items.size(); ++i)
            {
                int y = 4 + (int)i * pThis->m_itemHeight;
                RECT rcItem = { 4, y, rcClient.right - 4, y + pThis->m_itemHeight };

                if ((int)i == pThis->m_hoverIndex && pThis->m_items[i].isEnabled)
                {
                    // Fluent rounded hover capsule (radius 4px, RGB 50, 50, 50)
                    HBRUSH hbrHover = CreateSolidBrush(RGB(50, 50, 50));
                    HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hbrHover);
                    RoundRect(memDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 8, 8);
                    SelectObject(memDC, hOldB);
                    DeleteObject(hbrHover);
                }

                COLORREF textCol = pThis->m_items[i].isEnabled ? RGB(255, 255, 255) : RGB(130, 130, 130);
                SetTextColor(memDC, textCol);

                RECT rcText = { rcItem.left + 12, rcItem.top, rcItem.right - 12, rcItem.bottom };
                DrawTextW(memDC, pThis->m_items[i].label.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            SelectObject(memDC, hPrevP);
            DeleteObject(hNullP);
            SelectObject(memDC, hOldF);

            BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (pThis) pThis->m_hWnd = NULL;
            break;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
};

static HFONT CreateMdl2IconFont(float pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
        hFont = CreateFontIndirectW(&lf);
    }
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}

static HFONT CreateSystemUiFont(float pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}

static void CalculatePillLayout(int clientWidth, int& addrPillLeft, int& addrPillRight, int& searchPillLeft, int& searchPillRight)
{
    addrPillLeft = 8;
    int rightEdge = clientWidth - 8;
    int availableWidth = rightEdge - addrPillLeft;
    if (availableWidth < 120) availableWidth = 120;

    int searchWidth = 260;
    if (clientWidth < 780)
    {
        searchWidth = (availableWidth * 32) / 100;
        if (searchWidth < 110) searchWidth = 110;
        if (searchWidth > 260) searchWidth = 260;
    }

    searchPillRight = rightEdge;
    searchPillLeft = searchPillRight - searchWidth;
    addrPillRight = searchPillLeft - 8;

    if (addrPillRight < addrPillLeft + 40)
    {
        addrPillRight = addrPillLeft + 40;
    }
}

static RECT GetIconRect(NavIconIndex idx)
{
    RECT rc = { 0 };
    switch (idx)
    {
    case ICON_BACK:    rc = { 8, 6, 40, 38 }; break;
    case ICON_FORWARD: rc = { 40, 6, 72, 38 }; break;
    case ICON_UP:      rc = { 72, 6, 104, 38 }; break;
    case ICON_REFRESH: rc = { 104, 6, 136, 38 }; break;
    default: break;
    }
    return rc;
}

// Build breadcrumb layout with smart ellipsis truncation and proper padding
static void RebuildBreadcrumbs(HDC hdc, int addrPillLeft, int addrPillRight)
{
    g_State.breadcrumbElements.clear();
    g_State.collapsedFolders.clear();

    std::wstring pathStr(g_State.szCurrentPath);
    std::vector<std::wstring> rawSegments;
    std::vector<std::wstring> rawFullPaths;

    size_t start = 0;
    size_t end = 0;
    std::wstring accumulatedPath = L"";

    while ((end = pathStr.find_first_of(L"\\/", start)) != std::wstring::npos)
    {
        if (end > start)
        {
            std::wstring seg = pathStr.substr(start, end - start);
            rawSegments.push_back(seg);
            if (accumulatedPath.empty())
            {
                accumulatedPath = seg + L"\\";
            }
            else
            {
                if (accumulatedPath.back() != L'\\') accumulatedPath += L"\\";
                accumulatedPath += seg;
            }
            rawFullPaths.push_back(accumulatedPath);
        }
        start = end + 1;
    }
    if (start < pathStr.length())
    {
        std::wstring seg = pathStr.substr(start);
        rawSegments.push_back(seg);
        if (accumulatedPath.empty())
        {
            accumulatedPath = seg;
        }
        else
        {
            if (accumulatedPath.back() != L'\\') accumulatedPath += L"\\";
            accumulatedPath += seg;
        }
        rawFullPaths.push_back(accumulatedPath);
    }

    int curX = addrPillLeft + 4;
    int topY = 7;
    int botY = 37;

    // 1. "This PC" Desktop Computer Icon Button
    BreadcrumbElement elemRoot;
    elemRoot.type = BC_ROOT_PC;
    elemRoot.rc = { curX, topY, curX + 28, botY };
    elemRoot.label = L"This PC";
    elemRoot.fullPath = L"";
    elemRoot.segmentIndex = -1;
    g_State.breadcrumbElements.push_back(elemRoot);
    curX += 28;

    // 2. Chevron after "This PC"
    BreadcrumbElement elemChevRoot;
    elemChevRoot.type = BC_CHEVRON_ROOT;
    elemChevRoot.rc = { curX, topY, curX + 22, botY };
    elemChevRoot.label = L"";
    elemChevRoot.fullPath = L"";
    elemChevRoot.segmentIndex = -1;
    g_State.breadcrumbElements.push_back(elemChevRoot);
    curX += 22;

    if (rawSegments.empty()) return;

    // Measure all segments with 10.5pt font + 16px wide capsule padding (8px left, 8px right)
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_State.hFontMain);
    std::vector<int> segWidths;
    int totalNeeded = curX;
    for (size_t i = 0; i < rawSegments.size(); ++i)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdc, rawSegments[i].c_str(), (int)rawSegments[i].length(), &sz);
        int segW = sz.cx + 16; // 8px padding each side
        segWidths.push_back(segW);
        totalNeeded += segW + 22; // seg + chevron
    }
    SelectObject(hdc, hOldFont);

    int maxRight = addrPillRight - 10;
    int available = maxRight - curX;

    if (totalNeeded <= maxRight)
    {
        // All segments fit comfortably!
        for (size_t i = 0; i < rawSegments.size(); ++i)
        {
            BreadcrumbElement segElem;
            segElem.type = BC_SEGMENT;
            segElem.rc = { curX, topY, curX + segWidths[i], botY };
            segElem.label = rawSegments[i];
            segElem.fullPath = rawFullPaths[i];
            segElem.segmentIndex = (int)i;
            g_State.breadcrumbElements.push_back(segElem);
            curX += segWidths[i];

            BreadcrumbElement chevElem;
            chevElem.type = BC_CHEVRON;
            chevElem.rc = { curX, topY, curX + 22, botY };
            chevElem.label = L"";
            chevElem.fullPath = rawFullPaths[i];
            chevElem.segmentIndex = (int)i;
            g_State.breadcrumbElements.push_back(chevElem);
            curX += 22;
        }
    }
    else
    {
        // Smart Truncation: Collapse intermediary folders into "..."
        // Priority: Keep Leaf (current folder) visible
        int ellipsisWidth = 24;
        int ellipsisChevronWidth = 22;

        int leafIndex = (int)rawSegments.size() - 1;
        int leafW = segWidths[leafIndex];
        if (leafW > available - ellipsisWidth - ellipsisChevronWidth - 22)
        {
            leafW = available - ellipsisWidth - ellipsisChevronWidth - 22;
            if (leafW < 40) leafW = 40;
        }

        // Determine which trailing segments can fit
        int visibleStart = leafIndex;
        int usedWidth = ellipsisWidth + ellipsisChevronWidth + leafW + 22;

        for (int i = leafIndex - 1; i >= 0; --i)
        {
            if (usedWidth + segWidths[i] + 22 <= available)
            {
                usedWidth += segWidths[i] + 22;
                visibleStart = i;
            }
            else
            {
                break;
            }
        }

        // Store collapsed ancestors
        for (int i = 0; i < visibleStart; ++i)
        {
            g_State.collapsedFolders.push_back({ rawSegments[i], rawFullPaths[i] });
        }

        // 1. Add "..." button
        BreadcrumbElement ellipElem;
        ellipElem.type = BC_ELLIPSIS;
        ellipElem.rc = { curX, topY, curX + ellipsisWidth, botY };
        ellipElem.label = L"...";
        ellipElem.fullPath = L"";
        ellipElem.segmentIndex = -1;
        g_State.breadcrumbElements.push_back(ellipElem);
        curX += ellipsisWidth;

        // 2. Add Chevron after "..."
        BreadcrumbElement ellipChev;
        ellipChev.type = BC_CHEVRON_ELLIP;
        ellipChev.rc = { curX, topY, curX + ellipsisChevronWidth, botY };
        ellipChev.label = L"";
        ellipChev.fullPath = g_State.collapsedFolders.empty() ? L"" : g_State.collapsedFolders.back().second;
        ellipChev.segmentIndex = -1;
        g_State.breadcrumbElements.push_back(ellipChev);
        curX += ellipsisChevronWidth;

        // 3. Add visible trailing segments
        for (size_t i = visibleStart; i < rawSegments.size(); ++i)
        {
            int segW = segWidths[i];
            if (curX + segW > maxRight - 22)
            {
                segW = (maxRight - 22) - curX;
                if (segW < 20) segW = 20;
            }

            BreadcrumbElement segElem;
            segElem.type = BC_SEGMENT;
            segElem.rc = { curX, topY, curX + segW, botY };
            segElem.label = rawSegments[i];
            segElem.fullPath = rawFullPaths[i];
            segElem.segmentIndex = (int)i;
            g_State.breadcrumbElements.push_back(segElem);
            curX += segW;

            BreadcrumbElement chevElem;
            chevElem.type = BC_CHEVRON;
            chevElem.rc = { curX, topY, curX + 22, botY };
            chevElem.label = L"";
            chevElem.fullPath = rawFullPaths[i];
            chevElem.segmentIndex = (int)i;
            g_State.breadcrumbElements.push_back(chevElem);
            curX += 22;
        }
    }
}

static NavIconIndex HitTestIcon(int x, int y, int addrPillLeft, int addrPillRight)
{
    POINT pt = { x, y };

    if (g_State.bEditingAddress)
    {
        RECT rcClear = { addrPillRight - 28, 5, addrPillRight - 6, 39 };
        if (PtInRect(&rcClear, pt))
        {
            return ICON_ADDR_CLEAR;
        }
    }

    // Hit test individual breadcrumb elements inside the address bar
    for (size_t i = 0; i < g_State.breadcrumbElements.size(); ++i)
    {
        if (PtInRect(&g_State.breadcrumbElements[i].rc, pt))
        {
            g_State.hoverBreadcrumb = (int)i;
            return (NavIconIndex)(ICON_BC_BASE + i);
        }
    }

    RECT rcPill = { addrPillLeft, 5, addrPillRight, 39 };
    if (PtInRect(&rcPill, pt))
    {
        return ICON_ADDR_PILL;
    }

    return ICON_NONE;
}

// -------------------------------------------------------------
// Interactive Dropdown Menus for Drives, Folders, and Ellipsis
// -------------------------------------------------------------

static void ShowDriveDropdown(HWND hWnd, RECT rcAnchor)
{
    wchar_t szDrives[512] = { 0 };
    DWORD dwLen = GetLogicalDriveStringsW(511, szDrives);
    if (dwLen == 0) return;

    std::vector<NavMenuItem> items;
    const wchar_t* pDrive = szDrives;

    while (*pDrive)
    {
        wchar_t szVolName[MAX_PATH] = { 0 };
        GetVolumeInformationW(pDrive, szVolName, MAX_PATH, NULL, NULL, NULL, NULL, 0);

        std::wstring driveLetter = pDrive;
        if (!driveLetter.empty() && driveLetter.back() == L'\\')
        {
            driveLetter.pop_back();
        }

        std::wstring displayLabel;
        if (wcslen(szVolName) > 0)
        {
            displayLabel = std::wstring(szVolName) + L" (" + driveLetter + L")";
        }
        else
        {
            UINT driveType = GetDriveTypeW(pDrive);
            if (driveType == DRIVE_FIXED)
            {
                displayLabel = L"Local Disk (" + driveLetter + L")";
            }
            else if (driveType == DRIVE_CDROM)
            {
                displayLabel = L"CD Drive (" + driveLetter + L")";
            }
            else if (driveType == DRIVE_REMOVABLE)
            {
                displayLabel = L"USB Drive (" + driveLetter + L")";
            }
            else
            {
                displayLabel = L"Drive (" + driveLetter + L")";
            }
        }

        items.push_back({ displayLabel, pDrive, true });
        pDrive += wcslen(pDrive) + 1;
    }

    POINT pt = { rcAnchor.left, rcAnchor.bottom + 2 };
    ClientToScreen(hWnd, &pt);

    NavBreadcrumbMenu::Show(hWnd, pt.x, pt.y, items);
}

static void ShowFolderDropdown(HWND hWnd, const std::wstring& folderPath, RECT rcAnchor)
{
    if (folderPath.empty())
    {
        ShowDriveDropdown(hWnd, rcAnchor);
        return;
    }

    std::wstring searchPattern = folderPath;
    if (searchPattern.back() != L'\\') searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);

    std::vector<std::wstring> subfolders;
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
            {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
                {
                    subfolders.push_back(fd.cFileName);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(subfolders.begin(), subfolders.end());

    std::vector<NavMenuItem> items;
    if (subfolders.empty())
    {
        items.push_back({ L"(Empty folder)", L"", false });
    }
    else
    {
        for (const auto& sf : subfolders)
        {
            std::wstring target = folderPath;
            if (target.back() != L'\\') target += L"\\";
            target += sf;
            items.push_back({ sf, target, true });
        }
    }

    POINT pt = { rcAnchor.left, rcAnchor.bottom + 2 };
    ClientToScreen(hWnd, &pt);

    NavBreadcrumbMenu::Show(hWnd, pt.x, pt.y, items);
}

static void ShowEllipsisDropdown(HWND hWnd, RECT rcAnchor)
{
    if (g_State.collapsedFolders.empty()) return;

    std::vector<NavMenuItem> items;
    for (const auto& cf : g_State.collapsedFolders)
    {
        items.push_back({ cf.first, cf.second, true });
    }

    POINT pt = { rcAnchor.left, rcAnchor.bottom + 2 };
    ClientToScreen(hWnd, &pt);

    NavBreadcrumbMenu::Show(hWnd, pt.x, pt.y, items);
}

// Subclass for Edit controls to capture Enter Key, Escape, and Focus events
static LRESULT CALLBACK NavEditSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            HWND hToolbar = GetParent(hWnd);
            HWND hMainWnd = GetParent(hToolbar);

            if (uIdSubclass == IDC_NAV_EDIT_ADDR)
            {
                wchar_t szNewPath[MAX_PATH] = { 0 };
                GetWindowTextW(hWnd, szNewPath, MAX_PATH);
                if (wcslen(szNewPath) > 0)
                {
                    wcscpy_s(g_State.szCurrentPath, szNewPath);
                }
                g_State.bEditingAddress = FALSE;
                ShowWindow(hWnd, SW_HIDE);
                InvalidateRect(hToolbar, NULL, TRUE);

                SendMessage(hMainWnd, WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
            }
            else if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
            {
                SendMessage(hMainWnd, WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)hWnd);
            }
            return 0;
        }
        else if (wParam == VK_ESCAPE && uIdSubclass == IDC_NAV_EDIT_ADDR)
        {
            g_State.bEditingAddress = FALSE;
            ShowWindow(hWnd, SW_HIDE);
            InvalidateRect(GetParent(hWnd), NULL, TRUE);
            return 0;
        }
        break;

    case WM_SETFOCUS:
        if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
        {
            g_State.bSearchFocused = TRUE;
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
        }
        break;

    case WM_KILLFOCUS:
        if (uIdSubclass == IDC_NAV_EDIT_ADDR)
        {
            wchar_t szNewPath[MAX_PATH] = { 0 };
            GetWindowTextW(hWnd, szNewPath, MAX_PATH);
            if (wcslen(szNewPath) > 0)
            {
                wcscpy_s(g_State.szCurrentPath, szNewPath);
            }
            g_State.bEditingAddress = FALSE;
            ShowWindow(hWnd, SW_HIDE);
            InvalidateRect(GetParent(hWnd), NULL, TRUE);
        }
        else if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
        {
            g_State.bSearchFocused = FALSE;
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, NavEditSubclass, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK NavToolbarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        g_State.hFontMain = CreateSystemUiFont(10.5f, FW_NORMAL);
        g_State.hFontIcons = CreateMdl2IconFont(11.0f, FW_NORMAL);
        g_State.hFontChevron = CreateMdl2IconFont(9.5f, FW_SEMIBOLD);

        g_State.hbrPillDark = CreateSolidBrush(RGB(62, 26, 31));
        g_State.hbrPillEditDark = CreateSolidBrush(RGB(42, 18, 22));
        g_State.hbrPillLight = CreateSolidBrush(RGB(240, 240, 240));

        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // 1. Address Edit Control (Hidden initially; shown when user clicks Address Pill)
        g_State.hEditAddr = CreateWindowExW(0, L"EDIT", g_State.szCurrentPath,
            WS_CHILD | ES_AUTOHSCROLL,
            156, 11, 400, 22, hWnd, (HMENU)IDC_NAV_EDIT_ADDR, hInst, NULL);

        SetWindowSubclass(g_State.hEditAddr, NavEditSubclass, IDC_NAV_EDIT_ADDR, 0);
        if (g_State.hFontMain) SendMessage(g_State.hEditAddr, WM_SETFONT, (WPARAM)g_State.hFontMain, MAKELPARAM(FALSE, 0));
        SendMessage(g_State.hEditAddr, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SendMessageW(g_State.hEditAddr, EM_SETCUEBANNER, TRUE, (LPARAM)L"Enter MSTS / Open Rails Installation Directory Path...");

        // 2. Search Edit Control (Inside Search Pill)
        g_State.hEditSearch = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            600, 11, 190, 22, hWnd, (HMENU)IDC_NAV_EDIT_SEARCH, hInst, NULL);

        SetWindowSubclass(g_State.hEditSearch, NavEditSubclass, IDC_NAV_EDIT_SEARCH, 0);
        if (g_State.hFontMain) SendMessage(g_State.hEditSearch, WM_SETFONT, (WPARAM)g_State.hFontMain, MAKELPARAM(FALSE, 0));
        SendMessage(g_State.hEditSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

        SendMessageW(g_State.hEditSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search Consists & Assets");
    }
    break;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        HWND hMainWnd = GetParent(hWnd);

        if (code == EN_CHANGE && (HWND)lParam == g_State.hEditSearch)
        {
            if (!g_State.bInternalChange)
            {
                SendMessage(hMainWnd, WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)g_State.hEditSearch);
            }
        }
    }
    break;

    case WM_MOUSEMOVE:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        if (!g_State.bTrackingMouse)
        {
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            g_State.bTrackingMouse = TRUE;
        }

        NavIconIndex hit = HitTestIcon(x, y, addrPillLeft, addrPillRight);
        if (hit != g_State.hoverIndex)
        {
            g_State.hoverIndex = hit;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_MOUSELEAVE:
    {
        g_State.bTrackingMouse = FALSE;
        if (g_State.hoverIndex != ICON_NONE || g_State.pressedIndex != ICON_NONE)
        {
            g_State.hoverIndex = ICON_NONE;
            g_State.pressedIndex = ICON_NONE;
            g_State.hoverBreadcrumb = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        NavIconIndex hit = HitTestIcon(x, y, addrPillLeft, addrPillRight);
        if (hit != ICON_NONE)
        {
            g_State.pressedIndex = hit;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }

        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        NavIconIndex hit = HitTestIcon(x, y, addrPillLeft, addrPillRight);
        NavIconIndex clicked = (NavIconIndex)g_State.pressedIndex;
        g_State.pressedIndex = ICON_NONE;

        InvalidateRect(hWnd, NULL, FALSE);

        if (clicked != ICON_NONE && (clicked == hit || clicked == ICON_ADDR_PILL || clicked == ICON_ADDR_CLEAR || clicked >= ICON_BC_BASE))
        {
            HWND hMainWnd = GetParent(hWnd);
            switch (clicked)
            {
            case ICON_BACK:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_BACK, 0);
                break;
            case ICON_FORWARD:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_FORWARD, 0);
                break;
            case ICON_UP:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_UP, 0);
                break;
            case ICON_REFRESH:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_REFRESH, 0);
                break;
            case ICON_ADDR_CLEAR:
                if (g_State.hEditAddr)
                {
                    SetWindowTextW(g_State.hEditAddr, L"");
                    SetFocus(g_State.hEditAddr);
                }
                break;
            case ICON_ADDR_PILL:
            {
                if (!g_State.bEditingAddress)
                {
                    g_State.bEditingAddress = TRUE;
                    SetWindowTextW(g_State.hEditAddr, g_State.szCurrentPath);
                    ShowWindow(g_State.hEditAddr, SW_SHOW);
                    SetFocus(g_State.hEditAddr);
                    SendMessage(g_State.hEditAddr, EM_SETSEL, 0, -1);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;
            default:
            {
                if (clicked >= ICON_BC_BASE)
                {
                    int bcIdx = clicked - ICON_BC_BASE;
                    if (bcIdx >= 0 && bcIdx < (int)g_State.breadcrumbElements.size())
                    {
                        const BreadcrumbElement& elem = g_State.breadcrumbElements[bcIdx];
                        if (elem.type == BC_ROOT_PC || elem.type == BC_CHEVRON_ROOT)
                        {
                            ShowDriveDropdown(hWnd, elem.rc);
                        }
                        else if (elem.type == BC_ELLIPSIS)
                        {
                            ShowEllipsisDropdown(hWnd, elem.rc);
                        }
                        else if (elem.type == BC_CHEVRON_ELLIP || elem.type == BC_CHEVRON)
                        {
                            ShowFolderDropdown(hWnd, elem.fullPath, elem.rc);
                        }
                        else if (elem.type == BC_SEGMENT)
                        {
                            if (elem.segmentIndex == (int)g_State.breadcrumbElements.size() - 1 || elem.fullPath == g_State.szCurrentPath)
                            {
                                ShowFolderDropdown(hWnd, elem.fullPath, elem.rc);
                            }
                            else
                            {
                                NavToolbar_SetPath(hWnd, elem.fullPath.c_str());
                                SendMessage(hMainWnd, WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
                            }
                        }
                    }
                }
            }
            break;
            }
        }
    }
    break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;

        COLORREF bgCol = g_State.bEditingAddress && (hEdit == g_State.hEditAddr) ?
            RGB(42, 18, 22) : RGB(62, 26, 31);

        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, RGB(245, 245, 245));
        return (LRESULT)(hEdit == g_State.hEditAddr && g_State.bEditingAddress ? g_State.hbrPillEditDark : g_State.hbrPillDark);
    }
    break;

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

        // 1. Fill container background (Light Mica tone matching active tab surface RGB 52, 22, 27)
        COLORREF bgToolbar = RGB(52, 22, 27);
        HBRUSH hbrBg = CreateSolidBrush(bgToolbar);
        FillRect(hmemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        COLORREF pillBg = RGB(62, 26, 31);
        COLORREF pillBorder = RGB(78, 32, 38);
        COLORREF accentBlue = RGB(0, 120, 215);

        // Calculate Pill layout coordinates
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        // 2. Draw Pill Shape for Address Bar
        COLORREF curAddrPillBg = g_State.bEditingAddress ? RGB(42, 18, 22) : pillBg;

        COLORREF curAddrBorder = g_State.bEditingAddress ? accentBlue : pillBorder;

        HBRUSH hbrAddrPill = CreateSolidBrush(curAddrPillBg);
        HPEN hPenAddrBorder = CreatePen(PS_SOLID, 1, curAddrBorder);

        HBRUSH hOldBr = (HBRUSH)SelectObject(hmemDC, hbrAddrPill);
        HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenAddrBorder);

        RoundRect(hmemDC, addrPillLeft, 5, addrPillRight, 39, 8, 8);

        SelectObject(hmemDC, hOldBr);
        SelectObject(hmemDC, hOldPen);
        DeleteObject(hbrAddrPill);
        DeleteObject(hPenAddrBorder);

        // Active Editing Underline Highlight Line for Address Bar
        if (g_State.bEditingAddress)
        {
            HPEN hPenBlue = CreatePen(PS_SOLID, 2, accentBlue);
            HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hPenBlue);

            MoveToEx(hmemDC, addrPillLeft + 8, 38, NULL);
            LineTo(hmemDC, addrPillRight - 8, 38);

            SelectObject(hmemDC, hPrevPen);
            DeleteObject(hPenBlue);

            // Draw Clear 'X' (\xE711) button on right of Address Pill during editing
            RECT rcClearBtn = { addrPillRight - 26, 5, addrPillRight - 6, 39 };
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, g_State.hoverIndex == ICON_ADDR_CLEAR ? RGB(255, 255, 255) : RGB(200, 200, 200));
            SelectObject(hmemDC, g_State.hFontIcons);
            DrawTextW(hmemDC, L"\xE711", -1, &rcClearBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // 3. Draw Pill Shape for Search Bar
        COLORREF curSearchBorder = g_State.bSearchFocused ? accentBlue : pillBorder;
        HBRUSH hbrSearchPill = CreateSolidBrush(pillBg);
        HPEN hPenSearchBorder = CreatePen(PS_SOLID, 1, curSearchBorder);

        hOldBr = (HBRUSH)SelectObject(hmemDC, hbrSearchPill);
        hOldPen = (HPEN)SelectObject(hmemDC, hPenSearchBorder);

        RoundRect(hmemDC, searchPillLeft, 5, searchPillRight, 39, 8, 8);

        SelectObject(hmemDC, hOldBr);
        SelectObject(hmemDC, hOldPen);
        DeleteObject(hbrSearchPill);
        DeleteObject(hPenSearchBorder);

        if (g_State.bSearchFocused)
        {
            HPEN hPenBlue = CreatePen(PS_SOLID, 2, accentBlue);
            HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hPenBlue);

            MoveToEx(hmemDC, searchPillLeft + 8, 38, NULL);
            LineTo(hmemDC, searchPillRight - 8, 38);

            SelectObject(hmemDC, hPrevPen);
            DeleteObject(hPenBlue);
        }

        // Draw Search Icon (\xE721) inside Search Pill - bright & centered
        RECT rcSearchIcon = { searchPillRight - 28, 5, searchPillRight - 8, 39 };
        SetBkMode(hmemDC, TRANSPARENT);
        SetTextColor(hmemDC, RGB(220, 220, 220));
        SelectObject(hmemDC, g_State.hFontIcons ? g_State.hFontIcons : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(hmemDC, L"\xE721", -1, &rcSearchIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 4. Render Breadcrumbs inside Address Pill when NOT editing
        if (!g_State.bEditingAddress)
        {
            if (wcslen(g_State.szCurrentPath) == 0)
            {
                // Placeholder on first launch / empty path
                RECT rcPlaceholder = { addrPillLeft + 14, 5, addrPillRight - 14, 39 };
                SetBkMode(hmemDC, TRANSPARENT);
                SetTextColor(hmemDC, RGB(180, 150, 160));
                SelectObject(hmemDC, g_State.hFontMain ? g_State.hFontMain : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hmemDC, L"Enter MSTS / Open Rails Installation Directory...", -1, &rcPlaceholder, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else
            {
                RebuildBreadcrumbs(hmemDC, addrPillLeft, addrPillRight);

            HRGN hClip = CreateRectRgn(addrPillLeft + 1, 5, addrPillRight - 1, 39);
            SelectClipRgn(hmemDC, hClip);

            HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
            HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hNullPen);

            COLORREF hoverBg = RGB(78, 32, 38);
            COLORREF pressedBg = RGB(90, 38, 45);

            // 1. Draw unified hover / pressed capsules for breadcrumb pairs
            int activeHoverIdx = (g_State.hoverIndex >= ICON_BC_BASE) ? (g_State.hoverIndex - ICON_BC_BASE) : -1;
            int activePressIdx = (g_State.pressedIndex >= ICON_BC_BASE) ? (g_State.pressedIndex - ICON_BC_BASE) : -1;

            for (size_t i = 0; i < g_State.breadcrumbElements.size(); ++i)
            {
                const BreadcrumbElement& elem = g_State.breadcrumbElements[i];
                if (elem.type == BC_ROOT_PC || elem.type == BC_ELLIPSIS || elem.type == BC_SEGMENT)
                {
                    RECT rcCapsule = elem.rc;
                    bool bPairHover = ((int)i == activeHoverIdx);
                    bool bPairPress = ((int)i == activePressIdx);
                    bool bHasChevron = false;
                    bool bChevHover = false;
                    bool bChevPress = false;
                    int sepX = elem.rc.right;

                    // Include adjacent chevron in the unified capsule
                    if (i + 1 < g_State.breadcrumbElements.size())
                    {
                        const BreadcrumbElement& nextElem = g_State.breadcrumbElements[i + 1];
                        if (nextElem.type == BC_CHEVRON_ROOT || nextElem.type == BC_CHEVRON_ELLIP || nextElem.type == BC_CHEVRON)
                        {
                            rcCapsule.right = nextElem.rc.right;
                            bHasChevron = true;
                            if ((int)(i + 1) == activeHoverIdx) { bPairHover = true; bChevHover = true; }
                            if ((int)(i + 1) == activePressIdx) { bPairPress = true; bChevPress = true; }
                        }
                    }

                    if (bPairHover || bPairPress)
                    {
                        // Outer unified capsule
                        HBRUSH hbrHover = CreateSolidBrush(bPairPress ? pressedBg : hoverBg);
                        HBRUSH hOldBrHover = (HBRUSH)SelectObject(hmemDC, hbrHover);
                        RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                        SelectObject(hmemDC, hOldBrHover);
                        DeleteObject(hbrHover);

                        if (bHasChevron)
                        {
                            // 1. Subtle vertical separator line between text and chevron
                            HPEN hPenSep = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
                            HPEN hOldP = (HPEN)SelectObject(hmemDC, hPenSep);
                            MoveToEx(hmemDC, sepX, elem.rc.top + 5, NULL);
                            LineTo(hmemDC, sepX, elem.rc.bottom - 5);
                            SelectObject(hmemDC, hOldP);
                            DeleteObject(hPenSep);

                            // 2. Active button highlight cleanly overlaps the separator
                            COLORREF subHighlight = RGB(88, 36, 42);
                            if (bChevHover || bChevPress)
                            {
                                HRGN hChevRgn = CreateRectRgn(sepX, elem.rc.top, rcCapsule.right, rcCapsule.bottom);
                                HRGN hPrevClip = CreateRectRgn(0, 0, 0, 0);
                                int hasClip = GetClipRgn(hmemDC, hPrevClip);
                                ExtSelectClipRgn(hmemDC, hChevRgn, RGN_AND);

                                HBRUSH hbrSub = CreateSolidBrush(subHighlight);
                                HBRUSH hOldSub = (HBRUSH)SelectObject(hmemDC, hbrSub);
                                RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                                SelectObject(hmemDC, hOldSub);
                                DeleteObject(hbrSub);

                                SelectClipRgn(hmemDC, hasClip == 1 ? hPrevClip : NULL);
                                DeleteObject(hPrevClip);
                                DeleteObject(hChevRgn);
                            }
                            else if ((int)i == activeHoverIdx || (int)i == activePressIdx)
                            {
                                HRGN hTextRgn = CreateRectRgn(rcCapsule.left, elem.rc.top, sepX + 1, rcCapsule.bottom);
                                HRGN hPrevClip = CreateRectRgn(0, 0, 0, 0);
                                int hasClip = GetClipRgn(hmemDC, hPrevClip);
                                ExtSelectClipRgn(hmemDC, hTextRgn, RGN_AND);

                                HBRUSH hbrSub = CreateSolidBrush(subHighlight);
                                HBRUSH hOldSub = (HBRUSH)SelectObject(hmemDC, hbrSub);
                                RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                                SelectObject(hmemDC, hOldSub);
                                DeleteObject(hbrSub);

                                SelectClipRgn(hmemDC, hasClip == 1 ? hPrevClip : NULL);
                                DeleteObject(hPrevClip);
                                DeleteObject(hTextRgn);
                            }
                        }
                    }
                }
            }

            // 2. Draw glyphs and labels
            for (size_t i = 0; i < g_State.breadcrumbElements.size(); ++i)
            {
                const BreadcrumbElement& elem = g_State.breadcrumbElements[i];
                SetBkMode(hmemDC, TRANSPARENT);

                if (elem.type == BC_ROOT_PC)
                {
                    // Draw "This PC" Desktop Computer Monitor Icon (\xE7F4)
                    SetTextColor(hmemDC, RGB(245, 245, 245));
                    SelectObject(hmemDC, g_State.hFontIcons);
                    DrawTextW(hmemDC, L"\xE7F4", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else if (elem.type == BC_CHEVRON_ROOT || elem.type == BC_CHEVRON_ELLIP || elem.type == BC_CHEVRON)
                {
                    // Draw Chevron (\xE76C)
                    bool bChevActive = ((int)i == activeHoverIdx || (int)i == activePressIdx ||
                        (i > 0 && ((int)(i - 1) == activeHoverIdx || (int)(i - 1) == activePressIdx)));
                    SetTextColor(hmemDC, bChevActive ? RGB(255, 255, 255) : RGB(190, 170, 175));
                    SelectObject(hmemDC, g_State.hFontChevron);
                    DrawTextW(hmemDC, L"\xE76C", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else if (elem.type == BC_ELLIPSIS)
                {
                    // Draw "..." Ellipsis Button
                    SetTextColor(hmemDC, RGB(245, 245, 245));
                    SelectObject(hmemDC, g_State.hFontMain);
                    DrawTextW(hmemDC, L"...", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else if (elem.type == BC_SEGMENT)
                {
                    // Draw Folder / Drive segment with 8px left/right padding
                    SetTextColor(hmemDC, RGB(255, 255, 255));
                    SelectObject(hmemDC, g_State.hFontMain);
                    RECT rcText = elem.rc;
                    rcText.left += 8;
                    rcText.right -= 8;
                    DrawTextW(hmemDC, elem.label.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
            }

            SelectObject(hmemDC, hPrevPen);
            DeleteObject(hNullPen);

            SelectClipRgn(hmemDC, NULL);
            DeleteObject(hClip);
            }
        }

        // Bottom border line
        HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(38, 14, 18));
        HPEN hOldP = (HPEN)SelectObject(hmemDC, hPenLine);
        MoveToEx(hmemDC, 0, rcClient.bottom - 1, NULL);
        LineTo(hmemDC, rcClient.right, rcClient.bottom - 1);
        SelectObject(hmemDC, hOldP);
        DeleteObject(hPenLine);

        // 1px App-Drawn Perimeter Border (Left & Right) when windowed
        HWND hMainWnd = GetParent(hWnd);
        if (!IsZoomed(hMainWnd))
        {
            COLORREF clrBorder = RGB(78, 32, 38);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOldBorder = (HPEN)SelectObject(hmemDC, hPenBorder);

            MoveToEx(hmemDC, 0, 0, NULL);
            LineTo(hmemDC, 0, rcClient.bottom);
            MoveToEx(hmemDC, rcClient.right - 1, 0, NULL);
            LineTo(hmemDC, rcClient.right - 1, rcClient.bottom);

            SelectObject(hmemDC, hOldBorder);
            DeleteObject(hPenBorder);
        }

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hmemDC, 0, 0, SRCCOPY);

        SelectObject(hmemDC, holdBm);
        DeleteObject(hbm);
        DeleteDC(hmemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    break;

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(width, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        int addrEditLeft = addrPillLeft + 10;
        int addrEditWidth = addrPillRight - 34 - addrEditLeft;
        if (addrEditWidth < 40) addrEditWidth = 40;

        if (g_State.hEditAddr)
        {
            SetWindowPos(g_State.hEditAddr, NULL, addrEditLeft, 11, addrEditWidth, 22, SWP_NOZORDER);
        }

        if (g_State.hEditSearch)
        {
            int searchEditW = (searchPillRight - searchPillLeft) - 40;
            if (searchEditW < 40) searchEditW = 40;
            SetWindowPos(g_State.hEditSearch, NULL, searchPillLeft + 12, 11, searchEditW, 22, SWP_NOZORDER);
        }

        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;

    case WM_DESTROY:
        if (g_State.hFontMain) DeleteObject(g_State.hFontMain);
        if (g_State.hFontIcons) DeleteObject(g_State.hFontIcons);
        if (g_State.hFontChevron) DeleteObject(g_State.hFontChevron);
        if (g_State.hbrPillDark) DeleteObject(g_State.hbrPillDark);
        if (g_State.hbrPillEditDark) DeleteObject(g_State.hbrPillEditDark);
        if (g_State.hbrPillLight) DeleteObject(g_State.hbrPillLight);
        break;

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL RegisterNavToolbarClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = NavToolbarProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"NavToolbarClass";

    return RegisterClassExW(&wcex) != 0;
}

HWND CreateNavToolbar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId)
{
    RegisterNavToolbarClass(hInstance);

    HWND hToolbar = CreateWindowExW(
        0, L"NavToolbarClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, (HMENU)controlId, hInstance, NULL
    );

    return hToolbar;
}

void NavToolbar_SetPath(HWND hNavToolbar, const wchar_t* szPath)
{
    wcscpy_s(g_State.szCurrentPath, szPath);
    if (g_State.hEditAddr) SetWindowTextW(g_State.hEditAddr, szPath);
    InvalidateRect(hNavToolbar, NULL, TRUE);
}

void NavToolbar_GetPath(HWND hNavToolbar, wchar_t* szBuffer, int maxLen)
{
    wcscpy_s(szBuffer, maxLen, g_State.szCurrentPath);
}

void NavToolbar_GetSearchQuery(HWND hNavToolbar, wchar_t* szBuffer, int maxLen)
{
    if (g_State.hEditSearch) GetWindowTextW(g_State.hEditSearch, szBuffer, maxLen);
}

void NavToolbar_SetSearchQuery(HWND hNavToolbar, const wchar_t* szQuery)
{
    if (g_State.hEditSearch)
    {
        g_State.bInternalChange = TRUE;
        SetWindowTextW(g_State.hEditSearch, szQuery);
        g_State.bInternalChange = FALSE;
    }
}

void NavToolbar_SetDarkMode(HWND hNavToolbar, BOOL bDarkMode)
{
    g_State.bDarkMode = bDarkMode;
    InvalidateRect(hNavToolbar, NULL, TRUE);
}
