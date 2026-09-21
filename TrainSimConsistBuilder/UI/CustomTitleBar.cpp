#include "CustomTitleBar.h"
#include "UITheme.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

struct TitleBarTab
{
    std::wstring icon;
    std::wstring text;
    RECT rc;
};

struct TitleBarState
{
    BOOL bDarkMode = TRUE;
    HFONT hFontTitle = NULL;
    HFONT hFontTabs = NULL;
    HFONT hFontIcons = NULL;
    HICON hAppIcon = NULL;

    int activeTab = 0; // 0 = MAIN CONSISTS, 1 = ACTIVITY CONSISTS
    int hoverTab = -1;

    // Caption button rects & hover state (0: Min, 1: Max/Restore, 2: Close)
    RECT rcBtnMin = { 0 };
    RECT rcBtnMax = { 0 };
    RECT rcBtnClose = { 0 };
    int hoverBtn = -1;   // 0, 1, 2 or -1
    int pressedBtn = -1; // 0, 1, 2 or -1

    std::vector<TitleBarTab> tabs;
};

static TitleBarState g_TitleBarState;

static HFONT CreateCustomFont(float pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10.0f), GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static LRESULT CALLBACK CustomTitleBarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hMainWnd = GetParent(hWnd);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        g_TitleBarState.hFontTitle = CreateCustomFont(9.0f, FW_NORMAL, L"Segoe UI");
        g_TitleBarState.hFontTabs  = CreateCustomFont(9.0f, FW_SEMIBOLD, L"Segoe UI");
        g_TitleBarState.hFontIcons = CreateCustomFont(9.5f, FW_NORMAL, L"Segoe Fluent Icons");
        if (!g_TitleBarState.hFontIcons)
            g_TitleBarState.hFontIcons = CreateCustomFont(9.5f, FW_NORMAL, L"Segoe MDL2 Assets");

        g_TitleBarState.hAppIcon = (HICON)GetClassLongPtrW(hMainWnd, GCLP_HICONSM);
        if (!g_TitleBarState.hAppIcon)
            g_TitleBarState.hAppIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

        g_TitleBarState.tabs.clear();
        g_TitleBarState.tabs.push_back({ L"\xE7C0", L"MAIN CONSISTS", { 0 } });
        g_TitleBarState.tabs.push_back({ L"\xE707", L"ACTIVITY CONSISTS", { 0 } });
        return 0;
    }

    case WM_DESTROY:
    {
        if (g_TitleBarState.hFontTitle) DeleteObject(g_TitleBarState.hFontTitle);
        if (g_TitleBarState.hFontTabs)  DeleteObject(g_TitleBarState.hFontTabs);
        if (g_TitleBarState.hFontIcons) DeleteObject(g_TitleBarState.hFontIcons);
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        // Top caption buttons handle client clicks
        if (PtInRect(&g_TitleBarState.rcBtnMin, pt) ||
            PtInRect(&g_TitleBarState.rcBtnMax, pt) ||
            PtInRect(&g_TitleBarState.rcBtnClose, pt))
        {
            return HTCLIENT;
        }

        // Tabs in Row 2 handle client clicks
        for (const auto& tab : g_TitleBarState.tabs)
        {
            if (PtInRect(&tab.rc, pt))
                return HTCLIENT;
        }

        // Top Row (0 to 30px) is draggable caption
        if (pt.y < 30)
        {
            return HTTRANSPARENT; // Let main window receive HTCAPTION
        }

        return HTCLIENT;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        int newHoverBtn = -1;
        if (PtInRect(&g_TitleBarState.rcBtnMin, pt))   newHoverBtn = 0;
        else if (PtInRect(&g_TitleBarState.rcBtnMax, pt))   newHoverBtn = 1;
        else if (PtInRect(&g_TitleBarState.rcBtnClose, pt)) newHoverBtn = 2;

        int newHoverTab = -1;
        for (size_t i = 0; i < g_TitleBarState.tabs.size(); ++i)
        {
            if (PtInRect(&g_TitleBarState.tabs[i].rc, pt))
            {
                newHoverTab = (int)i;
                break;
            }
        }

        if (newHoverBtn != g_TitleBarState.hoverBtn || newHoverTab != g_TitleBarState.hoverTab)
        {
            g_TitleBarState.hoverBtn = newHoverBtn;
            g_TitleBarState.hoverTab = newHoverTab;
            InvalidateRect(hWnd, NULL, FALSE);

            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        g_TitleBarState.hoverBtn = -1;
        g_TitleBarState.hoverTab = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&g_TitleBarState.rcBtnMin, pt))
        {
            g_TitleBarState.pressedBtn = 0;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&g_TitleBarState.rcBtnMax, pt))
        {
            g_TitleBarState.pressedBtn = 1;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&g_TitleBarState.rcBtnClose, pt))
        {
            g_TitleBarState.pressedBtn = 2;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        for (size_t i = 0; i < g_TitleBarState.tabs.size(); ++i)
        {
            if (PtInRect(&g_TitleBarState.tabs[i].rc, pt))
            {
                if (g_TitleBarState.activeTab != (int)i)
                {
                    g_TitleBarState.activeTab = (int)i;
                    InvalidateRect(hWnd, NULL, FALSE);
                    SendMessageW(hMainWnd, WM_TITLEBAR_TABCHANGED, (WPARAM)g_TitleBarState.activeTab, 0);
                }
                return 0;
            }
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (GetCapture() == hWnd)
            ReleaseCapture();

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int releasedBtn = g_TitleBarState.pressedBtn;
        g_TitleBarState.pressedBtn = -1;
        InvalidateRect(hWnd, NULL, FALSE);

        if (releasedBtn == 0 && PtInRect(&g_TitleBarState.rcBtnMin, pt))
        {
            ShowWindow(hMainWnd, SW_MINIMIZE);
            return 0;
        }
        if (releasedBtn == 1 && PtInRect(&g_TitleBarState.rcBtnMax, pt))
        {
            if (IsZoomed(hMainWnd))
                ShowWindow(hMainWnd, SW_RESTORE);
            else
                ShowWindow(hMainWnd, SW_MAXIMIZE);
            return 0;
        }
        if (releasedBtn == 2 && PtInRect(&g_TitleBarState.rcBtnClose, pt))
        {
            PostMessageW(hMainWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.y < 30 && !PtInRect(&g_TitleBarState.rcBtnMin, pt) && !PtInRect(&g_TitleBarState.rcBtnMax, pt) && !PtInRect(&g_TitleBarState.rcBtnClose, pt))
        {
            if (IsZoomed(hMainWnd))
                ShowWindow(hMainWnd, SW_RESTORE);
            else
                ShowWindow(hMainWnd, SW_MAXIMIZE);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        // ====================================================================
        // TIER 1: TOP CAPTION BAR (Y = 0 to 30px)
        // ====================================================================
        int row1H = 30;
        RECT rcRow1 = { 0, 0, w, row1H };
        COLORREF bgTop = RGB(28, 9, 12);
        HBRUSH hbrRow1 = CreateSolidBrush(bgTop);
        FillRect(memDC, &rcRow1, hbrRow1);
        DeleteObject(hbrRow1);

        // App Icon
        int iconX = 10;
        int iconY = (row1H - 16) / 2;
        if (g_TitleBarState.hAppIcon)
        {
            DrawIconEx(memDC, iconX, iconY, g_TitleBarState.hAppIcon, 16, 16, 0, NULL, DI_NORMAL);
        }

        // App Title Typography (Segoe UI 9pt)
        SelectObject(memDC, g_TitleBarState.hFontTitle);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(225, 225, 225));

        RECT rcTitleText = { iconX + 22, 0, w - 150, row1H };
        DrawTextW(memDC, L"Train Sim Consist Builder for Open Rails", -1, &rcTitleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Caption Buttons (Flush to top-right corner, 46px x 30px)
        int btnW = 46;
        g_TitleBarState.rcBtnClose = { w - btnW, 0, w, row1H };
        g_TitleBarState.rcBtnMax   = { w - (btnW * 2), 0, w - btnW, row1H };
        g_TitleBarState.rcBtnMin   = { w - (btnW * 3), 0, w - (btnW * 2), row1H };

        // Minimize Button
        if (g_TitleBarState.hoverBtn == 0)
        {
            COLORREF hovMin = (g_TitleBarState.pressedBtn == 0 ? RGB(48, 18, 24) : RGB(38, 14, 18));
            HBRUSH hbrMin = CreateSolidBrush(hovMin);
            FillRect(memDC, &g_TitleBarState.rcBtnMin, hbrMin);
            DeleteObject(hbrMin);
        }
        SelectObject(memDC, g_TitleBarState.hFontIcons);
        SetTextColor(memDC, RGB(200, 200, 205));
        DrawTextW(memDC, L"\xE921", -1, &g_TitleBarState.rcBtnMin, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Maximize / Restore Button
        if (g_TitleBarState.hoverBtn == 1)
        {
            COLORREF hovMax = (g_TitleBarState.pressedBtn == 1 ? RGB(48, 18, 24) : RGB(38, 14, 18));
            HBRUSH hbrMax = CreateSolidBrush(hovMax);
            FillRect(memDC, &g_TitleBarState.rcBtnMax, hbrMax);
            DeleteObject(hbrMax);
        }
        bool isMax = IsZoomed(hMainWnd) != FALSE;
        DrawTextW(memDC, isMax ? L"\xE923" : L"\xE922", -1, &g_TitleBarState.rcBtnMax, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close Button
        if (g_TitleBarState.hoverBtn == 2)
        {
            COLORREF hovClose = g_TitleBarState.pressedBtn == 2 ? RGB(160, 30, 20) : RGB(196, 43, 28);
            HBRUSH hbrClose = CreateSolidBrush(hovClose);
            FillRect(memDC, &g_TitleBarState.rcBtnClose, hbrClose);
            DeleteObject(hbrClose);
            SetTextColor(memDC, RGB(255, 255, 255));
        }
        else
        {
            SetTextColor(memDC, RGB(200, 200, 205));
        }
        DrawTextW(memDC, L"\xE8BB", -1, &g_TitleBarState.rcBtnClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // ====================================================================
        // TIER 2: FULL-WIDTH DARK MICA TAB RIBBON (Y = 30 to 66px)
        // ====================================================================
        RECT rcRow2 = { 0, row1H, w, h };
        COLORREF bgRibbon = RGB(28, 9, 12);
        HBRUSH hbrRibbon = CreateSolidBrush(bgRibbon);
        FillRect(memDC, &rcRow2, hbrRibbon);
        DeleteObject(hbrRibbon);

        int tabStartX = 10;
        int tabW = 235;
        int tabTop = row1H + 3;
        int baselineY = h;
        const int r_b = 6;
        const int r_t = 8;

        for (size_t i = 0; i < g_TitleBarState.tabs.size(); ++i)
        {
            RECT rcTab = { tabStartX + (int)i * (tabW + 2), row1H, tabStartX + (int)i * (tabW + 2) + tabW, h };
            g_TitleBarState.tabs[i].rc = rcTab;

            bool isActive = (g_TitleBarState.activeTab == (int)i);
            bool isHover  = (g_TitleBarState.hoverTab == (int)i);

            int tabLeft   = rcTab.left + r_b;
            int tabRight  = rcTab.right - r_b;
            int tabBottom = baselineY;

            if (isActive)
            {
                // Active Tab Surface (Light Mica Wine tone matching NavToolbar)
                COLORREF clrActiveSurface = RGB(52, 22, 27);
                HBRUSH hbrSurface = CreateSolidBrush(clrActiveSurface);
                HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                HBRUSH hOldBr = (HBRUSH)SelectObject(memDC, hbrSurface);
                HPEN hOldPen = (HPEN)SelectObject(memDC, hNullPen);

                BeginPath(memDC);
                MoveToEx(memDC, tabLeft - r_b, baselineY, NULL);
                AngleArc(memDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
                AngleArc(memDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
                LineTo(memDC, tabRight - r_t, tabTop);
                AngleArc(memDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
                AngleArc(memDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);
                LineTo(memDC, tabRight + r_b, baselineY);
                LineTo(memDC, tabRight + r_b, h);
                LineTo(memDC, tabLeft - r_b, h);
                CloseFigure(memDC);
                EndPath(memDC);
                FillPath(memDC);

                SelectObject(memDC, hOldBr);
                SelectObject(memDC, hOldPen);
                DeleteObject(hbrSurface);
                DeleteObject(hNullPen);

                // Highlight Outline
                COLORREF clOutline = RGB(78, 32, 38);
                HPEN hOutlinePen = CreatePen(PS_SOLID, 1, clOutline);
                HPEN hOldOutlinePen = (HPEN)SelectObject(memDC, hOutlinePen);

                MoveToEx(memDC, tabLeft - r_b, baselineY, NULL);
                AngleArc(memDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
                AngleArc(memDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
                LineTo(memDC, tabRight - r_t, tabTop);
                AngleArc(memDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
                AngleArc(memDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);

                SelectObject(memDC, hOldOutlinePen);
                DeleteObject(hOutlinePen);

                // Draw Tab Icon and Text (Left Aligned like Explorer)
                int contentLeft = tabLeft + 12;
                RECT rcIcon = { contentLeft, row1H + 2, contentLeft + 20, h };
                RECT rcText = { contentLeft + 24, row1H + 2, tabRight - 10, h };

                SelectObject(memDC, g_TitleBarState.hFontIcons);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, g_TitleBarState.tabs[i].icon.c_str(), -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, g_TitleBarState.hFontTabs);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, g_TitleBarState.tabs[i].text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else
            {
                if (isHover)
                {
                    COLORREF clrHov = RGB(38, 14, 18);
                    HBRUSH hbrHov = CreateSolidBrush(clrHov);
                    HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                    SelectObject(memDC, hbrHov);
                    SelectObject(memDC, hNullPen);

                    BeginPath(memDC);
                    MoveToEx(memDC, tabLeft - r_b, baselineY, NULL);
                    AngleArc(memDC, tabLeft - r_b, tabBottom - r_b, r_b, 270.0f, 90.0f);
                    AngleArc(memDC, tabLeft + r_t, tabTop + r_t, r_t, 180.0f, -90.0f);
                    LineTo(memDC, tabRight - r_t, tabTop);
                    AngleArc(memDC, tabRight - r_t, tabTop + r_t, r_t, 90.0f, -90.0f);
                    AngleArc(memDC, tabRight + r_b, tabBottom - r_b, r_b, 180.0f, 90.0f);
                    LineTo(memDC, tabRight + r_b, baselineY);
                    CloseFigure(memDC);
                    EndPath(memDC);
                    FillPath(memDC);

                    DeleteObject(hbrHov);
                    DeleteObject(hNullPen);
                }

                // Draw Tab Icon and Text (Left Aligned like Explorer)
                int contentLeft = tabLeft + 12;
                RECT rcIcon = { contentLeft, row1H + 2, contentLeft + 20, h };
                RECT rcText = { contentLeft + 24, row1H + 2, tabRight - 10, h };

                COLORREF clrTabContent = isHover ? RGB(230, 225, 225) : RGB(180, 175, 175);

                SelectObject(memDC, g_TitleBarState.hFontIcons);
                SetTextColor(memDC, clrTabContent);
                DrawTextW(memDC, g_TitleBarState.tabs[i].icon.c_str(), -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, g_TitleBarState.hFontTabs);
                SetTextColor(memDC, clrTabContent);
                DrawTextW(memDC, g_TitleBarState.tabs[i].text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        // 1px App-Drawn Perimeter Border (Top, Left, Right) when windowed
        if (!IsZoomed(hMainWnd))
        {
            COLORREF clrBorder = RGB(78, 32, 38);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOldBorder = (HPEN)SelectObject(memDC, hPenBorder);

            // Top border
            MoveToEx(memDC, 0, 0, NULL);
            LineTo(memDC, w, 0);
            // Left border
            MoveToEx(memDC, 0, 0, NULL);
            LineTo(memDC, 0, h);
            // Right border
            MoveToEx(memDC, w - 1, 0, NULL);
            LineTo(memDC, w - 1, h);

            SelectObject(memDC, hOldBorder);
            DeleteObject(hPenBorder);
        }

        // Blit to screen
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

BOOL RegisterCustomTitleBarClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = CustomTitleBarProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"CustomTitleBarWindow";

    return RegisterClassExW(&wcex) != 0;
}

HWND CreateCustomTitleBar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId)
{
    RegisterCustomTitleBarClass(hInstance);

    return CreateWindowExW(
        0, L"CustomTitleBarWindow", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, width, height,
        hParent, (HMENU)controlId, hInstance, NULL
    );
}

void CustomTitleBar_SetDarkMode(HWND hTitleBar, BOOL bDarkMode)
{
    g_TitleBarState.bDarkMode = bDarkMode;
    if (hTitleBar && IsWindow(hTitleBar))
        InvalidateRect(hTitleBar, NULL, FALSE);
}

void CustomTitleBar_SetActiveTab(HWND hTitleBar, int tabIndex)
{
    g_TitleBarState.activeTab = tabIndex;
    if (hTitleBar && IsWindow(hTitleBar))
        InvalidateRect(hTitleBar, NULL, FALSE);
}

int CustomTitleBar_GetActiveTab(HWND hTitleBar)
{
    return g_TitleBarState.activeTab;
}

void CustomTitleBar_UpdateWindowState(HWND hTitleBar)
{
    if (hTitleBar && IsWindow(hTitleBar))
        InvalidateRect(hTitleBar, NULL, FALSE);
}
