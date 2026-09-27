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

    std::wstring titleText = L"Train Sim Consist Builder for Open Rails";

    int activeTab = 0;
    int hoverTab = -1;

    // Caption button rects & hover state (0: Min, 1: Max/Restore, 2: Close)
    RECT rcBtnMin = { 0 };
    RECT rcBtnMax = { 0 };
    RECT rcBtnClose = { 0 };
    int hoverBtn = -1;   // 0, 1, 2 or -1
    int pressedBtn = -1; // 0, 1, 2 or -1

    std::vector<TitleBarTab> tabs;
};

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
    TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    HWND hMainWnd = GetParent(hWnd);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        if (cs->lpCreateParams)
        {
            pState = (TitleBarState*)cs->lpCreateParams;
        }
        else
        {
            pState = new TitleBarState();
        }
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_CREATE:
    {
        if (!pState)
        {
            pState = new TitleBarState();
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        }

        pState->hFontTitle = CreateCustomFont(9.0f, FW_NORMAL, L"Segoe UI");
        pState->hFontTabs  = CreateCustomFont(9.0f, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcons = CreateCustomFont(9.5f, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcons)
            pState->hFontIcons = CreateCustomFont(9.5f, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->hAppIcon = (HICON)GetClassLongPtrW(hMainWnd, GCLP_HICONSM);
        if (!pState->hAppIcon)
            pState->hAppIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

        if (pState->tabs.empty())
        {
            pState->tabs.push_back({ L"\xE7C0", L"MAIN CONSISTS", { 0 } });
            pState->tabs.push_back({ L"\xE707", L"ACTIVITY CONSISTS", { 0 } });
        }
        return 0;
    }

    case WM_SIZE:
    {
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontTabs)  DeleteObject(pState->hFontTabs);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        return 0;
    }

    case WM_NCHITTEST:
    {
        if (!pState) return HTCLIENT;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        // Top caption buttons handle client clicks
        if (PtInRect(&pState->rcBtnMin, pt) ||
            PtInRect(&pState->rcBtnMax, pt) ||
            PtInRect(&pState->rcBtnClose, pt))
        {
            return HTCLIENT;
        }

        // Tabs in Row 2 handle client clicks
        for (const auto& tab : pState->tabs)
        {
            if (PtInRect(&tab.rc, pt))
                return HTCLIENT;
        }

        // Top Row (0 to 30px) is draggable caption
        if (pt.y < 30)
        {
            return HTTRANSPARENT; // Let parent frame receive HTCAPTION for dragging/snapping
        }

        return HTCLIENT;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        int newHoverBtn = -1;
        if (PtInRect(&pState->rcBtnMin, pt))        newHoverBtn = 0;
        else if (PtInRect(&pState->rcBtnMax, pt))   newHoverBtn = 1;
        else if (PtInRect(&pState->rcBtnClose, pt)) newHoverBtn = 2;

        int newHoverTab = -1;
        for (size_t i = 0; i < pState->tabs.size(); ++i)
        {
            if (PtInRect(&pState->tabs[i].rc, pt))
            {
                newHoverTab = (int)i;
                break;
            }
        }

        if (newHoverBtn != pState->hoverBtn || newHoverTab != pState->hoverTab)
        {
            pState->hoverBtn = newHoverBtn;
            pState->hoverTab = newHoverTab;
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
        if (pState)
        {
            pState->hoverBtn = -1;
            pState->hoverTab = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&pState->rcBtnMin, pt))
        {
            pState->pressedBtn = 0;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBtnMax, pt))
        {
            pState->pressedBtn = 1;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBtnClose, pt))
        {
            pState->pressedBtn = 2;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        for (size_t i = 0; i < pState->tabs.size(); ++i)
        {
            if (PtInRect(&pState->tabs[i].rc, pt))
            {
                if (pState->activeTab != (int)i)
                {
                    pState->activeTab = (int)i;
                    InvalidateRect(hWnd, NULL, FALSE);
                    SendMessageW(hMainWnd, WM_TITLEBAR_TABCHANGED, (WPARAM)pState->activeTab, 0);
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

        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int releasedBtn = pState->pressedBtn;
        pState->pressedBtn = -1;
        InvalidateRect(hWnd, NULL, FALSE);

        if (releasedBtn == 0 && PtInRect(&pState->rcBtnMin, pt))
        {
            ShowWindow(hMainWnd, SW_MINIMIZE);
            return 0;
        }
        if (releasedBtn == 1 && PtInRect(&pState->rcBtnMax, pt))
        {
            if (IsZoomed(hMainWnd))
                ShowWindow(hMainWnd, SW_RESTORE);
            else
                ShowWindow(hMainWnd, SW_MAXIMIZE);
            return 0;
        }
        if (releasedBtn == 2 && PtInRect(&pState->rcBtnClose, pt))
        {
            HWND hOwner = GetWindow(hMainWnd, GW_OWNER);
            if (!hOwner || !IsWindow(hOwner))
            {
                hOwner = GetParent(hMainWnd);
            }
            if (hOwner && IsWindow(hOwner))
            {
                EnableWindow(hOwner, TRUE);
                if (IsIconic(hOwner))
                {
                    ShowWindow(hOwner, SW_RESTORE);
                }
                SetWindowPos(hOwner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetForegroundWindow(hOwner);
                SetActiveWindow(hOwner);
                BringWindowToTop(hOwner);
                SetFocus(hOwner);
            }
            PostMessageW(hMainWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.y < 30 && !PtInRect(&pState->rcBtnMin, pt) && !PtInRect(&pState->rcBtnMax, pt) && !PtInRect(&pState->rcBtnClose, pt))
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
        if (!pState) { EndPaint(hWnd, &ps); return 0; }

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
        if (pState->hAppIcon)
        {
            DrawIconEx(memDC, iconX, iconY, pState->hAppIcon, 16, 16, 0, NULL, DI_NORMAL);
        }

        // App Title Typography (Segoe UI 9pt)
        SelectObject(memDC, pState->hFontTitle);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(225, 225, 225));

        RECT rcTitleText = { iconX + 22, 0, w - 150, row1H };
        DrawTextW(memDC, pState->titleText.c_str(), -1, &rcTitleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Caption Buttons (Flush to top-right corner, 46px x 30px)
        int btnW = 46;
        pState->rcBtnClose = { w - btnW, 0, w, row1H };
        pState->rcBtnMax   = { w - (btnW * 2), 0, w - btnW, row1H };
        pState->rcBtnMin   = { w - (btnW * 3), 0, w - (btnW * 2), row1H };

        // Minimize Button
        if (pState->hoverBtn == 0)
        {
            COLORREF hovMin = (pState->pressedBtn == 0 ? RGB(48, 18, 24) : RGB(38, 14, 18));
            HBRUSH hbrMin = CreateSolidBrush(hovMin);
            FillRect(memDC, &pState->rcBtnMin, hbrMin);
            DeleteObject(hbrMin);
        }
        SelectObject(memDC, pState->hFontIcons);
        SetTextColor(memDC, RGB(200, 200, 205));
        DrawTextW(memDC, L"\xE921", -1, &pState->rcBtnMin, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Maximize / Restore Button
        if (pState->hoverBtn == 1)
        {
            COLORREF hovMax = (pState->pressedBtn == 1 ? RGB(48, 18, 24) : RGB(38, 14, 18));
            HBRUSH hbrMax = CreateSolidBrush(hovMax);
            FillRect(memDC, &pState->rcBtnMax, hbrMax);
            DeleteObject(hbrMax);
        }
        bool isMax = IsZoomed(hMainWnd) != FALSE;
        DrawTextW(memDC, isMax ? L"\xE923" : L"\xE922", -1, &pState->rcBtnMax, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close Button
        if (pState->hoverBtn == 2)
        {
            COLORREF hovClose = pState->pressedBtn == 2 ? RGB(160, 30, 20) : RGB(196, 43, 28);
            HBRUSH hbrClose = CreateSolidBrush(hovClose);
            FillRect(memDC, &pState->rcBtnClose, hbrClose);
            DeleteObject(hbrClose);
            SetTextColor(memDC, RGB(255, 255, 255));
        }
        else
        {
            SetTextColor(memDC, RGB(200, 200, 205));
        }
        DrawTextW(memDC, L"\xE8BB", -1, &pState->rcBtnClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

        for (size_t i = 0; i < pState->tabs.size(); ++i)
        {
            RECT rcTab = { tabStartX + (int)i * (tabW + 2), row1H, tabStartX + (int)i * (tabW + 2) + tabW, h };
            pState->tabs[i].rc = rcTab;

            bool isActive = (pState->activeTab == (int)i);
            bool isHover  = (pState->hoverTab == (int)i);

            int tabLeft   = rcTab.left + r_b;
            int tabRight  = rcTab.right - r_b;
            int tabBottom = baselineY;

            if (isActive)
            {
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

                // Draw Tab Icon and Text
                int contentLeft = tabLeft + 12;
                RECT rcIcon = { contentLeft, row1H + 2, contentLeft + 20, h };
                RECT rcText = { contentLeft + 24, row1H + 2, tabRight - 10, h };

                SelectObject(memDC, pState->hFontIcons);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, pState->tabs[i].icon.c_str(), -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, pState->hFontTabs);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, pState->tabs[i].text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

                int contentLeft = tabLeft + 12;
                RECT rcIcon = { contentLeft, row1H + 2, contentLeft + 20, h };
                RECT rcText = { contentLeft + 24, row1H + 2, tabRight - 10, h };

                COLORREF clrTabContent = isHover ? RGB(230, 225, 225) : RGB(180, 175, 175);

                SelectObject(memDC, pState->hFontIcons);
                SetTextColor(memDC, clrTabContent);
                DrawTextW(memDC, pState->tabs[i].icon.c_str(), -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, pState->hFontTabs);
                SetTextColor(memDC, clrTabContent);
                DrawTextW(memDC, pState->tabs[i].text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        // 1px App-Drawn Perimeter Border
        if (!IsZoomed(hMainWnd))
        {
            COLORREF clrBorder = RGB(78, 32, 38);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOldBorder = (HPEN)SelectObject(memDC, hPenBorder);

            MoveToEx(memDC, 0, 0, NULL);
            LineTo(memDC, w, 0);
            MoveToEx(memDC, 0, 0, NULL);
            LineTo(memDC, 0, h);
            MoveToEx(memDC, w - 1, 0, NULL);
            LineTo(memDC, w - 1, h);

            SelectObject(memDC, hOldBorder);
            DeleteObject(hPenBorder);
        }

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

HWND CreateCustomTitleBarEx(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId, const wchar_t* title, const std::vector<TitleBarTabItem>& tabs)
{
    RegisterCustomTitleBarClass(hInstance);

    TitleBarState* pState = new TitleBarState();
    if (title) pState->titleText = title;

    pState->tabs.clear();
    for (const auto& t : tabs)
    {
        pState->tabs.push_back({ t.icon, t.text, { 0 } });
    }

    return CreateWindowExW(
        0, L"CustomTitleBarWindow", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, width, height,
        hParent, (HMENU)controlId, hInstance, (LPVOID)pState
    );
}

void CustomTitleBar_SetDarkMode(HWND hTitleBar, BOOL bDarkMode)
{
    if (hTitleBar && IsWindow(hTitleBar))
    {
        TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hTitleBar, GWLP_USERDATA);
        if (pState) pState->bDarkMode = bDarkMode;
        InvalidateRect(hTitleBar, NULL, FALSE);
    }
}

void CustomTitleBar_SetActiveTab(HWND hTitleBar, int tabIndex)
{
    if (hTitleBar && IsWindow(hTitleBar))
    {
        TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hTitleBar, GWLP_USERDATA);
        if (pState) pState->activeTab = tabIndex;
        InvalidateRect(hTitleBar, NULL, FALSE);
    }
}

int CustomTitleBar_GetActiveTab(HWND hTitleBar)
{
    if (hTitleBar && IsWindow(hTitleBar))
    {
        TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hTitleBar, GWLP_USERDATA);
        if (pState) return pState->activeTab;
    }
    return 0;
}

void CustomTitleBar_UpdateWindowState(HWND hTitleBar)
{
    if (hTitleBar && IsWindow(hTitleBar))
        InvalidateRect(hTitleBar, NULL, FALSE);
}

void CustomTitleBar_SetTitle(HWND hTitleBar, const wchar_t* title)
{
    if (hTitleBar && IsWindow(hTitleBar) && title)
    {
        TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hTitleBar, GWLP_USERDATA);
        if (pState)
        {
            pState->titleText = title;
            InvalidateRect(hTitleBar, NULL, FALSE);
        }
    }
}

void CustomTitleBar_SetTabs(HWND hTitleBar, const std::vector<TitleBarTabItem>& tabs)
{
    if (hTitleBar && IsWindow(hTitleBar))
    {
        TitleBarState* pState = (TitleBarState*)GetWindowLongPtrW(hTitleBar, GWLP_USERDATA);
        if (pState)
        {
            pState->tabs.clear();
            for (const auto& t : tabs)
            {
                pState->tabs.push_back({ t.icon, t.text, { 0 } });
            }
            InvalidateRect(hTitleBar, NULL, FALSE);
        }
    }
}
