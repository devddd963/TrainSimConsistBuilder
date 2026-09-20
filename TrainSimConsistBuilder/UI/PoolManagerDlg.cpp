#include "PoolManagerDlg.h"
#include "BatchConsistGeneratorDlg.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "../SRC/PoolManager.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include "../SRC/AssetsParser.h"
#include "FluentDragGhost.h"
#include "CustomScrollBar.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>

#pragma comment(lib, "dwmapi.lib")

extern HFONT GetAdaptiveSystemFont();
extern HWND g_hAssetList;
extern std::vector<size_t> g_FilteredStockIndices;
extern std::vector<StockItem> g_StockCache;
extern CRITICAL_SECTION g_StockCacheCS;

namespace PoolTheme
{
    constexpr COLORREF GutterBackground   = RGB(16, 16, 16); // Unified scrollbar gutter & window background
    constexpr COLORREF TitleBackground    = RGB(26, 26, 26); // Top title bar
    constexpr COLORREF ToolbarBackground  = RGB(22, 22, 22); // Preset toolbar
    constexpr COLORREF FooterBackground   = RGB(22, 22, 22); // Bottom footer bar
    constexpr COLORREF CardBackground     = RGB(26, 26, 26); // Pool card surface
    constexpr COLORREF CardHeaderBg       = RGB(32, 32, 32); // Pool card header
    constexpr COLORREF CardBorder         = RGB(46, 46, 46); // Card border
    constexpr COLORREF UnitsBoxBackground = RGB(20, 20, 20); // Inner units container
    constexpr COLORREF UnitsBoxBorder     = RGB(38, 38, 38); // Units container border
    constexpr COLORREF ChipBackground     = RGB(32, 32, 32); // Stock unit chip
    constexpr COLORREF ChipBorder         = RGB(50, 50, 50); // Stock unit chip border
    constexpr COLORREF BorderLine         = RGB(46, 46, 46); // Divider lines & frame borders
    constexpr COLORREF TextPrimary        = RGB(240, 240, 240);
    constexpr COLORREF TextSecondary      = RGB(160, 160, 160);
    constexpr COLORREF TextMuted          = RGB(120, 120, 120);
    constexpr COLORREF AccentBlue         = RGB(0, 120, 215);
    constexpr COLORREF AccentHover        = RGB(0, 140, 240);
    constexpr COLORREF AccentPressed      = RGB(0, 100, 180);
}

static HWND g_hPoolManagerDlg = NULL;
static const UINT_PTR TIMER_REPEAT_ID = 1001;

struct InputPromptState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hEdit = NULL;
    std::wstring title;
    std::wstring prompt;
    std::wstring resultText;
    bool isConfirmed = false;
    HFONT hFontTitle = NULL;
    HFONT hFontPrompt = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontIcon = NULL;
    RECT rcOkBtn = { 0 };
    RECT rcCancelBtn = { 0 };
    RECT rcCloseBtn = { 0 };
    bool isHoverOk = false;
    bool isHoverCancel = false;
    bool isHoverClose = false;
};

static HFONT CreateCustomFont(int pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static void DrawModernButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* iconGlyph = NULL)
{
    COLORREF bgCol;
    COLORREF borderCol;
    COLORREF textCol;

    if (isAccent)
    {
        bgCol = isPressed ? RGB(0, 100, 185) : (isHovered ? RGB(20, 140, 235) : RGB(0, 120, 215));
        borderCol = bgCol;
        textCol = RGB(255, 255, 255);
    }
    else
    {
        bgCol = isPressed ? RGB(36, 36, 36) : (isHovered ? RGB(52, 52, 52) : RGB(40, 40, 40));
        borderCol = isHovered ? RGB(80, 80, 80) : RGB(60, 60, 60);
        textCol = RGB(245, 245, 245);
    }

    HBRUSH hbr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
    HPEN holdPen = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);

    SelectObject(hdc, holdBr);
    SelectObject(hdc, holdPen);
    DeleteObject(hbr);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);

    bool hasIcon = (iconGlyph && hIconFont && wcslen(iconGlyph) > 0);
    bool hasText = (text && wcslen(text) > 0);

    bool isTriDown = (iconGlyph && wcscmp(iconGlyph, L"__TRI_DOWN__") == 0);
    bool isTriUp = (iconGlyph && wcscmp(iconGlyph, L"__TRI_UP__") == 0);
    bool isCustomTriangle = isTriDown || isTriUp;

    if (isCustomTriangle)
    {
        int triW = 10;
        int triCX = (rc.left + rc.right) / 2;
        int triCY = (rc.top + rc.bottom) / 2;

        if (hasText)
        {
            SelectObject(hdc, hFont);
            RECT rcTextMeasure = { 0, 0, 0, 0 };
            DrawTextW(hdc, text, -1, &rcTextMeasure, DT_CALCRECT | DT_NOPREFIX);
            int textW = rcTextMeasure.right - rcTextMeasure.left;
            int gap = 6;
            int totalW = triW + gap + textW;
            int startX = rc.left + (rc.right - rc.left - totalW) / 2;

            triCX = startX + triW / 2;
            RECT rcText = { startX + triW + gap, rc.top, rc.right, rc.bottom };
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        COLORREF arrCol = textCol;
        HBRUSH hBrArr = CreateSolidBrush(arrCol);
        HPEN hPenArr = CreatePen(PS_SOLID, 1, arrCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBrArr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenArr);

        POINT pts[3];
        if (isTriDown)
        {
            pts[0] = { triCX - 4, triCY - 2 };
            pts[1] = { triCX + 4, triCY - 2 };
            pts[2] = { triCX,     triCY + 3 };
        }
        else
        {
            pts[0] = { triCX,     triCY - 3 };
            pts[1] = { triCX - 4, triCY + 2 };
            pts[2] = { triCX + 4, triCY + 2 };
        }
        Polygon(hdc, pts, 3);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrArr);
        DeleteObject(hPenArr);
    }
    else if (hasIcon && hasText)
    {
        SelectObject(hdc, hIconFont);
        RECT rcIconMeasure = { 0, 0, 0, 0 };
        DrawTextW(hdc, iconGlyph, -1, &rcIconMeasure, DT_CALCRECT | DT_NOPREFIX);
        int iconW = rcIconMeasure.right - rcIconMeasure.left;
        if (iconW <= 0) iconW = 12;

        SelectObject(hdc, hFont);
        RECT rcTextMeasure = { 0, 0, 0, 0 };
        DrawTextW(hdc, text, -1, &rcTextMeasure, DT_CALCRECT | DT_NOPREFIX);
        int textW = rcTextMeasure.right - rcTextMeasure.left;

        int gap = 6;
        int totalW = iconW + gap + textW;
        int startX = rc.left + (rc.right - rc.left - totalW) / 2;

        SelectObject(hdc, hIconFont);
        RECT rcIcon = { startX - 2, rc.top, startX + iconW + 2, rc.bottom };
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, hFont);
        RECT rcText = { startX + iconW + gap, rc.top, rc.right, rc.bottom };
        DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else if (hasIcon)
    {
        SelectObject(hdc, hIconFont);
        RECT rcIcon = rc;
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else if (hasText)
    {
        SelectObject(hdc, hFont);
        RECT rcText = rc;
        DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// -------------------------------------------------------------
// Sleek Modern Input Prompt Dialog (Custom Polished Dark Modal)
// -------------------------------------------------------------
static LRESULT CALLBACK InputPromptProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    InputPromptState* pState = (InputPromptState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (InputPromptState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_CREATE:
    {
        pState->hFontTitle  = CreateCustomFont(12, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontPrompt = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMain   = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontBold   = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon   = CreateCustomFont(10, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateCustomFont(10, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->hEdit = CreateWindowExW(
            0, L"EDIT", pState->resultText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            34, 82, 388, 20, hWnd, (HMENU)101, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEdit, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        SendMessageW(pState->hEdit, EM_SETSEL, 0, -1);
        SetFocus(pState->hEdit);
        return 0;
    }

    case WM_NCHITTEST:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        if (PtInRect(&pState->rcOkBtn, pt) ||
            PtInRect(&pState->rcCancelBtn, pt) ||
            PtInRect(&pState->rcCloseBtn, pt) ||
            (pt.x >= 24 && pt.x <= 436 && pt.y >= 74 && pt.y <= 110))
        {
            return HTCLIENT;
        }

        if (pt.y < 50)
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        COLORREF bgCol = RGB(28, 28, 28);
        COLORREF txtCol = RGB(245, 245, 245);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, txtCol);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(28, 28, 28));
        return (LRESULT)hbrEditDark;
    }

    case WM_CTLCOLORSCROLLBAR:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, PoolTheme::GutterBackground);
        static HBRUSH hbrGutterDark = CreateSolidBrush(PoolTheme::GutterBackground);
        return (LRESULT)hbrGutterDark;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP holdBmp = (HBITMAP)SelectObject(hdcMem, hbm);

        // 1. Background Fill
        COLORREF bgCol = RGB(20, 20, 20);
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hdcMem, &rc, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hdcMem, TRANSPARENT);

        // 2. Header Icon & Title
        if (pState->hFontIcon)
        {
            SelectObject(hdcMem, pState->hFontIcon);
            SetTextColor(hdcMem, RGB(96, 205, 255));
            RECT rcIcon = { 20, 16, 44, 42 };
            DrawTextW(hdcMem, L"\xE70F", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdcMem, pState->hFontTitle);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        RECT rcTitle = { 46, 15, w - 50, 42 };
        DrawTextW(hdcMem, pState->title.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close Button [✕] at top right
        pState->rcCloseBtn = { w - 38, 14, w - 14, 38 };
        if (pState->isHoverClose)
        {
            HBRUSH hbrCloseHover = CreateSolidBrush(RGB(55, 55, 55));
            FillRect(hdcMem, &pState->rcCloseBtn, hbrCloseHover);
            DeleteObject(hbrCloseHover);
        }
        SelectObject(hdcMem, pState->hFontMain);
        SetTextColor(hdcMem, pState->isHoverClose ? RGB(255, 100, 100) : RGB(160, 160, 160));
        DrawTextW(hdcMem, L"✕", -1, &pState->rcCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3. Prompt Subtitle Label
        SelectObject(hdcMem, pState->hFontPrompt);
        SetTextColor(hdcMem, RGB(170, 170, 170));
        RECT rcPrompt = { 24, 48, w - 24, 70 };
        DrawTextW(hdcMem, pState->prompt.c_str(), -1, &rcPrompt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 4. Custom Rounded Edit Box Container
        RECT rcEditBox = { 24, 74, w - 24, 110 };
        COLORREF editBoxBg = RGB(28, 28, 28);
        COLORREF editBoxBorder = RGB(60, 60, 60);

        HBRUSH hbrBox = CreateSolidBrush(editBoxBg);
        HPEN hPenBox = CreatePen(PS_SOLID, 1, editBoxBorder);
        HBRUSH holdB = (HBRUSH)SelectObject(hdcMem, hbrBox);
        HPEN holdP = (HPEN)SelectObject(hdcMem, hPenBox);
        RoundRect(hdcMem, rcEditBox.left, rcEditBox.top, rcEditBox.right, rcEditBox.bottom, 6, 6);
        SelectObject(hdcMem, holdB);
        SelectObject(hdcMem, holdP);
        DeleteObject(hbrBox);
        DeleteObject(hPenBox);

        // 5. Buttons Row
        int btnW = 90;
        int btnH = 32;
        int btnY = h - 48;
        int btnOkX = w - 24 - btnW;
        int btnCancelX = btnOkX - 10 - btnW;

        pState->rcOkBtn = { btnOkX, btnY, btnOkX + btnW, btnY + btnH };
        pState->rcCancelBtn = { btnCancelX, btnY, btnCancelX + btnW, btnY + btnH };

        DrawModernButton(hdcMem, pState->rcCancelBtn, L"Cancel", pState->isHoverCancel, false, false, pState->hFontMain);
        DrawModernButton(hdcMem, pState->rcOkBtn, L"OK", pState->isHoverOk, false, true, pState->hFontBold);

        // 6. 1px Outer Border
        COLORREF winBorderCol = RGB(50, 50, 50);
        HPEN hPenWin = CreatePen(PS_SOLID, 1, winBorderCol);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        SelectObject(hdcMem, hPenWin);
        SelectObject(hdcMem, hNullB);
        RoundRect(hdcMem, 0, 0, w, h, 12, 12);
        DeleteObject(hPenWin);

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, holdBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool hOk = PtInRect(&pState->rcOkBtn, pt) != FALSE;
        bool hCancel = PtInRect(&pState->rcCancelBtn, pt) != FALSE;
        bool hClose = PtInRect(&pState->rcCloseBtn, pt) != FALSE;

        if (hOk != pState->isHoverOk || hCancel != pState->isHoverCancel || hClose != pState->isHoverClose)
        {
            pState->isHoverOk = hOk;
            pState->isHoverCancel = hCancel;
            pState->isHoverClose = hClose;
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
        pState->isHoverOk = false;
        pState->isHoverCancel = false;
        pState->isHoverClose = false;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&pState->rcOkBtn, pt))
        {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(pState->hEdit, buf, 256);
            pState->resultText = buf;
            pState->isConfirmed = true;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (PtInRect(&pState->rcCancelBtn, pt) || PtInRect(&pState->rcCloseBtn, pt))
        {
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(pState->hEdit, buf, 256);
            pState->resultText = buf;
            pState->isConfirmed = true;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontTitle)  DeleteObject(pState->hFontTitle);
            if (pState->hFontPrompt) DeleteObject(pState->hFontPrompt);
            if (pState->hFontMain)   DeleteObject(pState->hFontMain);
            if (pState->hFontBold)   DeleteObject(pState->hFontBold);
            if (pState->hFontIcon)   DeleteObject(pState->hFontIcon);
        }
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool ShowModernInputPrompt(HWND hWndParent, const wchar_t* title, const wchar_t* promptLabel, const std::wstring& initialText, std::wstring& outText)
{
    static bool s_PromptRegistered = false;
    const wchar_t* szClassName = L"ModernPromptDialog";

    if (!s_PromptRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcex.lpfnWndProc = InputPromptProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = NULL;
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_PromptRegistered = true;
    }

    InputPromptState state;
    state.hParent = hWndParent;
    state.title = title;
    state.prompt = promptLabel;
    state.resultText = initialText;

    int dlgW = 460;
    int dlgH = 195;

    RECT rcParent;
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, szClassName, title,
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, dlgW, dlgH, hWndParent, NULL, GetModuleHandleW(NULL), &state
    );

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    EnableWindow(hWndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    SetForegroundWindow(hDlg);
    SetFocus(state.hEdit);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_RETURN)
            {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(state.hEdit, buf, 256);
                state.resultText = buf;
                state.isConfirmed = true;
                if (hWndParent && IsWindow(hWndParent))
                {
                    EnableWindow(hWndParent, TRUE);
                    SetForegroundWindow(hWndParent);
                    SetActiveWindow(hWndParent);
                }
                DestroyWindow(hDlg);
                break;
            }
            else if (msg.wParam == VK_ESCAPE)
            {
                if (hWndParent && IsWindow(hWndParent))
                {
                    EnableWindow(hWndParent, TRUE);
                    SetForegroundWindow(hWndParent);
                    SetActiveWindow(hWndParent);
                }
                DestroyWindow(hDlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, TRUE);
        SetForegroundWindow(hWndParent);
        SetActiveWindow(hWndParent);
    }

    if (state.isConfirmed)
    {
        outText = state.resultText;
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// Main Wizard Dialog Implementation
// -------------------------------------------------------------
struct ClickableControl
{
    enum Type {
        NONE,
        BTN_PRESET_NEW,
        BTN_PRESET_RENAME,
        BTN_PRESET_CLONE,
        BTN_PRESET_DELETE,
        BTN_POOLS_EXPAND_ALL,
        BTN_POOLS_COLLAPSE_ALL,
        BTN_ADD_POOL,
        BTN_POOL_COLLAPSE_TOGGLE,
        BTN_POOL_RENAME,
        BTN_POOL_CLONE,
        BTN_POOL_UP,
        BTN_POOL_DOWN,
        BTN_POOL_DELETE,
        BTN_POOL_MODE_TOGGLE,
        BTN_POOL_MIN_DEC,
        BTN_POOL_MIN_INC,
        BTN_POOL_MIN_EDIT,
        BTN_POOL_MAX_DEC,
        BTN_POOL_MAX_INC,
        BTN_POOL_MAX_EDIT,
        BTN_POOL_FLIP_TOGGLE,
        BTN_POOL_COPY_UNITS,
        BTN_POOL_PASTE_CLIPBOARD,
        BTN_POOL_CLEAR_UNITS,
        BTN_POOL_COPY_SELECTED,
        BTN_POOL_DELETE_SELECTED,
        BTN_POOL_FLIP_SELECTED,
        BTN_UNIT_REMOVE,
        BTN_UNIT_FLIP_TOGGLE,
        BTN_UNIT_COPY,
        BTN_CLOSE,
        BTN_GENERATE,
        COMBO_PRESET_CLICK
    };

    Type type = NONE;
    RECT rc = { 0 };
    int poolIdx = -1;
    int unitIdx = -1;
};


static const int POOL_VSCROLL_WIDTH = 13;

struct WizardDlgState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    RECT rcTopCloseBtn = { 0 };
    bool isTopCloseHover = false;
    HFONT hFontTitle = NULL;
    HFONT hFontSub = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontMainBold = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontBadge = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    CustomScrollBar m_vScroll;

    int scrollY = 0;
    int maxScrollY = 0;

    int hoveredControlIdx = -1;
    int pressedControlIdx = -1;
    std::vector<ClickableControl> clickControls;

    int dragOverPoolIdx = -1;

    // Card Drag-and-Drop Re-order state
    int draggingPoolIdx = -1;
    int cardDropTargetIdx = -1;
    bool isPotentialCardDrag = false;
    int potentialCardDragIdx = -1;
    POINT ptCardDragStart = { 0, 0 };

    struct CardHit {
        int poolIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
    };
    std::vector<CardHit> cardHits;

    bool isPresetDropdownOpen = false;
    RECT rcPresetDropdown = { 0 };
    std::vector<RECT> presetItemRects;

    // Auto-repeat state for +/- buttons
    ClickableControl::Type repeatAction = ClickableControl::NONE;
    int repeatPoolIdx = -1;
    int repeatHoldCount = 0;

    // Multi-selection state for pool units
    int selectedPoolIdx = -1;
    std::unordered_set<int> selectedUnitIndices;
    int anchorUnitIdx = -1;

    // Marquee / Rubber-Band selection state
    bool isMarqueeSelecting = false;
    bool isPotentialMarquee = false;
    POINT ptMarqueeStart = { 0, 0 };
    POINT ptMarqueeCurrent = { 0, 0 };
    int marqueePoolIdx = -1;
    std::unordered_set<int> marqueeInitialSelection;

    struct UnitChipHit {
        int poolIdx = -1;
        int unitIdx = -1;
        RECT rcChip = { 0 };
    };
    std::vector<UnitChipHit> unitChipHits;

    struct PoolUnitsBoxHit {
        int poolIdx = -1;
        RECT rcUnitsBox = { 0 };
    };
    std::vector<PoolUnitsBoxHit> poolUnitsBoxHits;
};

static void ExecuteStepAction(ClickableControl::Type type, int poolIdx, int step)
{
    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
    if (!pPreset || poolIdx < 0 || poolIdx >= (int)pPreset->pools.size())
        return;

    auto& pool = pPreset->pools[poolIdx];
    switch (type)
    {
    case ClickableControl::BTN_POOL_MIN_DEC:
        pool.minCount = (std::max)(0, pool.minCount - step);
        break;

    case ClickableControl::BTN_POOL_MIN_INC:
        pool.minCount += step;
        if (pool.minCount > pool.maxCount)
            pool.maxCount = pool.minCount;
        break;

    case ClickableControl::BTN_POOL_MAX_DEC:
        pool.maxCount = (std::max)(1, pool.maxCount - step);
        if (pool.minCount > pool.maxCount)
            pool.minCount = pool.maxCount;
        break;

    case ClickableControl::BTN_POOL_MAX_INC:
        pool.maxCount += step;
        break;

    default:
        break;
    }
    PoolManager::PersistPoolPresets();
}

static LRESULT CALLBACK WizardDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WizardDlgState* pState = (WizardDlgState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        RECT rcTopClose = { rcClient.right - 44, 0, rcClient.right, 42 };
        if (PtInRect(&rcTopClose, pt)) return HTCLIENT;

        // 2. Border resize handling (8px border)
        int b = 8;
        if (pt.y < b && pt.x < b) return HTTOPLEFT;
        if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
        if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
        if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
        if (pt.y < b) return HTTOP;
        if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
        if (pt.x < b) return HTLEFT;
        if (pt.x >= rcClient.right - b) return HTRIGHT;

        // 3. Interactive controls
        for (const auto& cc : pState->clickControls)
        {
            if (PtInRect(&cc.rc, pt)) return HTCLIENT;
        }
        if (pState->isPresetDropdownOpen) return HTCLIENT;

        // 4. Header area is draggable (except top right close area)
        if (pt.y <= 42 && pt.x < rcClient.right - 44)
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (WizardDlgState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_CREATE:
    {
        pState->hFontTitle     = CreateCustomFont(13, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontSub       = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontMain      = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMainBold  = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontSmall     = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontBadge     = CreateCustomFont(8, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon      = CreateCustomFont(11, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateCustomFont(11, FW_NORMAL, L"Segoe MDL2 Assets");
        pState->hFontIconSmall = CreateCustomFont(10, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIconSmall)
            pState->hFontIconSmall = CreateCustomFont(10, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->m_vScroll.SetOrientation(ScrollBarOrientation::Vertical);
        pState->m_vScroll.SetGutterColor(PoolTheme::GutterBackground);
        return 0;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_SIZE:
    {
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (pState->m_vScroll.OnMouseWheel(zDelta, 48, hWnd))
        {
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == CustomScrollBar::TIMER_ANIM_ID && pState)
        {
            if (pState->m_vScroll.OnTimer(hWnd))
            {
                pState->scrollY = pState->m_vScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        if (wParam == TIMER_REPEAT_ID && pState)
        {
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0)
            {
                KillTimer(hWnd, TIMER_REPEAT_ID);
                if (GetCapture() == hWnd) ReleaseCapture();
                pState->repeatAction = ClickableControl::NONE;
                pState->repeatPoolIdx = -1;
                pState->repeatHoldCount = 0;
                pState->pressedControlIdx = -1;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            pState->repeatHoldCount++;

            if (pState->repeatHoldCount == 1)
            {
                SetTimer(hWnd, TIMER_REPEAT_ID, 45, NULL);
            }

            int step = 1;
            if (pState->repeatHoldCount > 35)
                step = 5;
            else if (pState->repeatHoldCount > 15)
                step = 2;

            ExecuteStepAction(pState->repeatAction, pState->repeatPoolIdx, step);
            InvalidateRect(hWnd, NULL, FALSE);
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

        HDC hmemDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP holdBm = (HBITMAP)SelectObject(hmemDC, hbm);

        pState->clickControls.clear();
        pState->unitChipHits.clear();
        pState->poolUnitsBoxHits.clear();

        // 1. Background Fill
        COLORREF bgCol = PoolTheme::GutterBackground;
        COLORREF titleBgCol = PoolTheme::TitleBackground;
        COLORREF borderCol = PoolTheme::BorderLine;
        COLORREF textPrimary = PoolTheme::TextPrimary;
        COLORREF textSecondary = PoolTheme::TextSecondary;
        COLORREF accentCol = PoolTheme::AccentBlue;

        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hmemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        // 2. Title Bar (Height = 42px)
        RECT rcTitleBar = { 0, 0, w, 42 };
        HBRUSH hbrTitle = CreateSolidBrush(titleBgCol);
        FillRect(hmemDC, &rcTitleBar, hbrTitle);
        DeleteObject(hbrTitle);

        SetBkMode(hmemDC, TRANSPARENT);
        if (pState->hFontIcon)
        {
            SelectObject(hmemDC, pState->hFontIcon);
            SetTextColor(hmemDC, accentCol);
            RECT rcIcon = { 16, 0, 42, 42 };
            DrawTextW(hmemDC, L"\xE71D", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (pState->hFontTitle)
        {
            SelectObject(hmemDC, pState->hFontTitle);
            SetTextColor(hmemDC, textPrimary);
            RECT rcTitle = { 46, 0, w - 50, 42 };
            DrawTextW(hmemDC, L"Consist Pool Manager", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Top-Right Close Button [✕]
        pState->rcTopCloseBtn = { w - 44, 0, w, 42 };
        if (pState->isTopCloseHover)
        {
            HBRUSH hbrClose = CreateSolidBrush(RGB(232, 17, 35));
            FillRect(hmemDC, &pState->rcTopCloseBtn, hbrClose);
            DeleteObject(hbrClose);
            SetTextColor(hmemDC, RGB(255, 255, 255));
        }
        else
        {
            SetTextColor(hmemDC, textSecondary);
        }
        SelectObject(hmemDC, pState->hFontIconSmall ? pState->hFontIconSmall : pState->hFontMain);
        DrawTextW(hmemDC, L"\xE711", -1, &pState->rcTopCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 1st Divider Line (Below Title Bar at Y = 42)
        HPEN hPenLine = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenLine);
        MoveToEx(hmemDC, 0, 42, NULL);
        LineTo(hmemDC, w, 42);

        // 3. Preset Toolbar (Y = 43 to 89, Height = 46px)
        int toolbarY = 43;
        int toolbarH = 46;
        RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
        COLORREF tbBg = PoolTheme::ToolbarBackground;
        HBRUSH hbrTb = CreateSolidBrush(tbBg);
        FillRect(hmemDC, &rcToolbar, hbrTb);
        DeleteObject(hbrTb);

        // 2nd Divider Line (Below Preset Toolbar at Y = toolbarY + toolbarH)
        MoveToEx(hmemDC, 0, toolbarY + toolbarH, NULL);
        LineTo(hmemDC, w, toolbarY + toolbarH);

        SelectObject(hmemDC, pState->hFontMainBold);
        SetTextColor(hmemDC, textPrimary);
        RECT rcPresetLabel = { 16, toolbarY, 68, toolbarY + toolbarH };
        DrawTextW(hmemDC, L"Preset:", -1, &rcPresetLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        PoolManager::PoolPreset* pActivePreset = PoolManager::GetActivePreset();
        std::wstring activePresetName = pActivePreset ? pActivePreset->presetName : L"Default Preset";

        RECT rcCombo = { 70, toolbarY + 9, 250, toolbarY + toolbarH - 9 };
        pState->rcPresetDropdown = rcCombo;

        bool isComboHover = (pState->hoveredControlIdx >= 0 && pState->hoveredControlIdx < (int)pState->clickControls.size() &&
                             pState->clickControls[pState->hoveredControlIdx].type == ClickableControl::COMBO_PRESET_CLICK);

        COLORREF comboBg = isComboHover ? RGB(45, 45, 45) : RGB(34, 34, 34);
        COLORREF comboBorder = isComboHover ? RGB(90, 90, 90) : RGB(60, 60, 60);

        HBRUSH hbrCombo = CreateSolidBrush(comboBg);
        HPEN hPenCombo = CreatePen(PS_SOLID, 1, comboBorder);
        SelectObject(hmemDC, hbrCombo);
        SelectObject(hmemDC, hPenCombo);
        RoundRect(hmemDC, rcCombo.left, rcCombo.top, rcCombo.right, rcCombo.bottom, 6, 6);
        DeleteObject(hbrCombo);
        DeleteObject(hPenCombo);

        RECT rcComboText = rcCombo;
        rcComboText.left += 10;
        rcComboText.right -= 28;
        SelectObject(hmemDC, pState->hFontMain);
        SetTextColor(hmemDC, RGB(255, 255, 255));
        DrawTextW(hmemDC, activePresetName.c_str(), -1, &rcComboText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rcChevron = { rcCombo.right - 24, rcCombo.top, rcCombo.right, rcCombo.bottom };
        int chevCX = (rcChevron.left + rcChevron.right) / 2;
        int chevCY = (rcChevron.top + rcChevron.bottom) / 2;
        COLORREF arrCol = isComboHover ? RGB(255, 255, 255) : RGB(180, 180, 180);
        HBRUSH hBrChev = CreateSolidBrush(arrCol);
        HPEN hPenChev = CreatePen(PS_SOLID, 1, arrCol);
        HBRUSH hOldBrC = (HBRUSH)SelectObject(hmemDC, hBrChev);
        HPEN hOldPenC = (HPEN)SelectObject(hmemDC, hPenChev);
        POINT ptsChev[3] = { { chevCX - 4, chevCY - 2 }, { chevCX + 4, chevCY - 2 }, { chevCX, chevCY + 3 } };
        Polygon(hmemDC, ptsChev, 3);
        SelectObject(hmemDC, hOldBrC);
        SelectObject(hmemDC, hOldPenC);
        DeleteObject(hBrChev);
        DeleteObject(hPenChev);

        ClickableControl ccCombo;
        ccCombo.type = ClickableControl::COMBO_PRESET_CLICK;
        ccCombo.rc = rcCombo;
        pState->clickControls.push_back(ccCombo);

        // Preset action buttons: [+ New], [✏️ Rename], [Clone], [Delete]
        int btnX = rcCombo.right + 8;
        int btnH = 30;
        int btnY = toolbarY + (toolbarH - btnH) / 2;

        RECT rcBtnNew = { btnX, btnY, btnX + 62, btnY + btnH };
        ClickableControl ccNew = { ClickableControl::BTN_PRESET_NEW, rcBtnNew };
        int idxNew = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccNew);
        DrawModernButton(hmemDC, rcBtnNew, L"New", pState->hoveredControlIdx == idxNew, pState->pressedControlIdx == idxNew, false, pState->hFontMain, pState->hFontIconSmall, L"\xE710");

        btnX += 62 + 6;
        RECT rcBtnRename = { btnX, btnY, btnX + 74, btnY + btnH };
        ClickableControl ccRename = { ClickableControl::BTN_PRESET_RENAME, rcBtnRename };
        int idxRename = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccRename);
        DrawModernButton(hmemDC, rcBtnRename, L"Rename", pState->hoveredControlIdx == idxRename, pState->pressedControlIdx == idxRename, false, pState->hFontMain, pState->hFontIconSmall, L"\xE70F");

        btnX += 74 + 6;
        RECT rcBtnClone = { btnX, btnY, btnX + 64, btnY + btnH };
        ClickableControl ccClone = { ClickableControl::BTN_PRESET_CLONE, rcBtnClone };
        int idxClone = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccClone);
        DrawModernButton(hmemDC, rcBtnClone, L"Clone", pState->hoveredControlIdx == idxClone, pState->pressedControlIdx == idxClone, false, pState->hFontMain, pState->hFontIconSmall, L"\xE8C8");

        btnX += 64 + 6;
        RECT rcBtnDel = { btnX, btnY, btnX + 68, btnY + btnH };
        ClickableControl ccDel = { ClickableControl::BTN_PRESET_DELETE, rcBtnDel };
        int idxDel = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccDel);
        DrawModernButton(hmemDC, rcBtnDel, L"Delete", pState->hoveredControlIdx == idxDel, pState->pressedControlIdx == idxDel, false, pState->hFontMain, pState->hFontIconSmall, L"\xE74D");

        // [+ Add Pool] and [Expand All] / [Collapse All] Buttons
        int addPoolW = 96;
        int expandAllW = 92;
        int collapseAllW = 96;
        int curRight = w - 20;

        RECT rcBtnAddPool = { curRight - addPoolW, btnY, curRight, btnY + btnH };
        ClickableControl ccAddPool = { ClickableControl::BTN_ADD_POOL, rcBtnAddPool };
        int idxAddPool = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccAddPool);
        DrawModernButton(hmemDC, rcBtnAddPool, L"Add Pool", pState->hoveredControlIdx == idxAddPool, pState->pressedControlIdx == idxAddPool, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE710");

        curRight -= (addPoolW + 6);
        RECT rcBtnExpAll = { curRight - expandAllW, btnY, curRight, btnY + btnH };
        ClickableControl ccExpAll = { ClickableControl::BTN_POOLS_EXPAND_ALL, rcBtnExpAll };
        int idxExpAll = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccExpAll);
        DrawModernButton(hmemDC, rcBtnExpAll, L"Expand All", pState->hoveredControlIdx == idxExpAll, pState->pressedControlIdx == idxExpAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

        curRight -= (expandAllW + 6);
        RECT rcBtnColAll = { curRight - collapseAllW, btnY, curRight, btnY + btnH };
        ClickableControl ccColAll = { ClickableControl::BTN_POOLS_COLLAPSE_ALL, rcBtnColAll };
        int idxColAll = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccColAll);
        DrawModernButton(hmemDC, rcBtnColAll, L"Collapse All", pState->hoveredControlIdx == idxColAll, pState->pressedControlIdx == idxColAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

        // 4. Scrollable Content Area: Pool Cards
        int contentY = toolbarY + toolbarH + 1;
        int footerH = 56;
        int contentH = h - footerH - contentY;
        RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

        pState->unitChipHits.clear();
        pState->poolUnitsBoxHits.clear();
        pState->cardHits.clear();

        int totalContentH = 8;
        if (pActivePreset && !pActivePreset->pools.empty())
        {
            for (size_t p = 0; p < pActivePreset->pools.size(); ++p)
            {
                const auto& pool = pActivePreset->pools[p];
                int unitCount = (int)pool.units.size();
                int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
                int cardH = pool.isCollapsed ? 33 : (92 + unitsAreaH + 40);
                totalContentH += cardH + 14;
            }
        }
        totalContentH += 10;

        RECT rcScroll = { w - 12, contentY + 2, w - 2, contentY + contentH - 2 };
        pState->m_vScroll.SetBounds(rcScroll);
        pState->m_vScroll.SetRange(0, totalContentH - 1, contentH);
        pState->scrollY = pState->m_vScroll.GetPos();
        pState->maxScrollY = pState->m_vScroll.GetMaxScrollPos();

        HRGN hRgnClip = CreateRectRgn(rcContentClip.left, rcContentClip.top, rcContentClip.right, rcContentClip.bottom);
        SelectClipRgn(hmemDC, hRgnClip);

        int cardY = contentY - pState->scrollY + 8;
        int cardMargin = 20;
        int cardW = w - (cardMargin * 2);

        if (pActivePreset && !pActivePreset->pools.empty())
        {
            for (size_t p = 0; p < pActivePreset->pools.size(); ++p)
            {
                const auto& pool = pActivePreset->pools[p];
                int unitCount = (int)pool.units.size();

                int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
                int cardH = pool.isCollapsed ? 33 : (92 + unitsAreaH + 40);

                RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };
                RECT rcCardHeader = { rcCard.left + 1, rcCard.top + 1, rcCard.right - 1, pool.isCollapsed ? (rcCard.bottom - 1) : (rcCard.top + 34) };
                pState->cardHits.push_back({ (int)p, rcCard, rcCardHeader });

                bool isDragOver = (pState->dragOverPoolIdx == (int)p);
                COLORREF cardBg = isDragOver ? RGB(22, 38, 54) : PoolTheme::CardBackground;
                COLORREF cardBorder = isDragOver ? RGB(0, 160, 255) : PoolTheme::CardBorder;

                HBRUSH hbrCard = CreateSolidBrush(cardBg);
                HPEN hPenCard = CreatePen(PS_SOLID, isDragOver ? 2 : 1, cardBorder);
                SelectObject(hmemDC, hbrCard);
                SelectObject(hmemDC, hPenCard);
                RoundRect(hmemDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);
                DeleteObject(hbrCard);
                DeleteObject(hPenCard);

                // --- Card Header Bar ---
                COLORREF cardHeadBg = isDragOver ? RGB(28, 48, 68) : PoolTheme::CardHeaderBg;
                HBRUSH hbrCardHead = CreateSolidBrush(cardHeadBg);
                FillRect(hmemDC, &rcCardHeader, hbrCardHead);
                DeleteObject(hbrCardHead);

                // Chevron Expand/Collapse Toggle Button on Left
                int chevH = 22;
                int chevW = 24;
                int chevY = rcCardHeader.top + ((rcCardHeader.bottom - rcCardHeader.top) - chevH) / 2;
                RECT rcChev = { rcCardHeader.left + 6, chevY, rcCardHeader.left + 6 + chevW, chevY + chevH };
                ClickableControl ccCol = { ClickableControl::BTN_POOL_COLLAPSE_TOGGLE, rcChev, (int)p };
                int idxCol = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccCol);
                DrawModernButton(hmemDC, rcChev, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontMain, pState->hFontIconSmall, pool.isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                // Pool Title
                std::wstring poolTitle = L"Pool " + std::to_wstring(p + 1) + L": " + pool.name;
                SelectObject(hmemDC, pState->hFontMainBold);
                SetTextColor(hmemDC, isDragOver ? RGB(0, 180, 255) : RGB(255, 255, 255));
                SIZE szTitle;
                GetTextExtentPoint32W(hmemDC, poolTitle.c_str(), (int)poolTitle.length(), &szTitle);

                int titleX = rcChev.right + 8;
                int titleW = (std::min)((int)szTitle.cx, (int)(rcCardHeader.right - 180 - titleX));
                RECT rcPoolTitle = { titleX, rcCardHeader.top, titleX + titleW, rcCardHeader.bottom };
                DrawTextW(hmemDC, poolTitle.c_str(), -1, &rcPoolTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                // Summary Badge on Header when collapsed
                if (pool.isCollapsed)
                {
                    std::wstring summary = std::to_wstring(unitCount) + (unitCount == 1 ? L" Unit" : L" Units") + L" · " + (pool.pickMode == PoolManager::PoolPickMode::Random ? L"Random" : L"Sequential") + L" (" + std::to_wstring(pool.minCount) + L"-" + std::to_wstring(pool.maxCount) + L")";
                    SelectObject(hmemDC, pState->hFontBadge);
                    SIZE szSum;
                    GetTextExtentPoint32W(hmemDC, summary.c_str(), (int)summary.length(), &szSum);

                    int badgeX = rcPoolTitle.right + 10;
                    int badgeW = szSum.cx + 14;
                    if (badgeX + badgeW < rcCardHeader.right - 175)
                    {
                        RECT rcSumBadge = { badgeX, rcCardHeader.top + 5, badgeX + badgeW, rcCardHeader.bottom - 5 };
                        HBRUSH hbrSum = CreateSolidBrush(RGB(22, 38, 54));
                        HPEN hPenSum = CreatePen(PS_SOLID, 1, RGB(40, 85, 120));
                        HGDIOBJ bO = SelectObject(hmemDC, hbrSum);
                        HGDIOBJ pO = SelectObject(hmemDC, hPenSum);
                        RoundRect(hmemDC, rcSumBadge.left, rcSumBadge.top, rcSumBadge.right, rcSumBadge.bottom, 4, 4);
                        SelectObject(hmemDC, bO);
                        SelectObject(hmemDC, pO);
                        DeleteObject(hbrSum);
                        DeleteObject(hPenSum);

                        SetBkMode(hmemDC, TRANSPARENT);
                        SetTextColor(hmemDC, RGB(140, 200, 240));
                        DrawTextW(hmemDC, summary.c_str(), -1, &rcSumBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                }

                // Buttons on header right: [✏️ Rename], [📋 Clone], [▲ Up], [▼ Down], [🗑 Delete]
                int hdrBtnW = 28;
                int hdrBtnH = 24;
                int hdrBtnY = rcCardHeader.top + ((rcCardHeader.bottom - rcCardHeader.top) - hdrBtnH) / 2;
                int hdrBtnX = rcCardHeader.right - 164;

                // Rename Pool
                RECT rcRenP = { hdrBtnX, hdrBtnY, hdrBtnX + hdrBtnW, hdrBtnY + hdrBtnH };
                ClickableControl ccRenP = { ClickableControl::BTN_POOL_RENAME, rcRenP, (int)p };
                int idxRenP = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccRenP);
                DrawModernButton(hmemDC, rcRenP, L"", pState->hoveredControlIdx == idxRenP, pState->pressedControlIdx == idxRenP, false, pState->hFontMain, pState->hFontIconSmall, L"\xE70F");

                // Clone Pool
                hdrBtnX += 32;
                RECT rcCloneP = { hdrBtnX, hdrBtnY, hdrBtnX + hdrBtnW, hdrBtnY + hdrBtnH };
                ClickableControl ccCloneP = { ClickableControl::BTN_POOL_CLONE, rcCloneP, (int)p };
                int idxCloneP = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccCloneP);
                DrawModernButton(hmemDC, rcCloneP, L"", pState->hoveredControlIdx == idxCloneP, pState->pressedControlIdx == idxCloneP, false, pState->hFontMain, pState->hFontIconSmall, L"\xE8C8");

                // Move Up
                hdrBtnX += 32;
                RECT rcUp = { hdrBtnX, hdrBtnY, hdrBtnX + hdrBtnW, hdrBtnY + hdrBtnH };
                ClickableControl ccUp = { ClickableControl::BTN_POOL_UP, rcUp, (int)p };
                int idxUp = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccUp);
                DrawModernButton(hmemDC, rcUp, L"", pState->hoveredControlIdx == idxUp, pState->pressedControlIdx == idxUp, false, pState->hFontMain, pState->hFontIconSmall, L"\xE74A");

                // Move Down
                hdrBtnX += 32;
                RECT rcDown = { hdrBtnX, hdrBtnY, hdrBtnX + hdrBtnW, hdrBtnY + hdrBtnH };
                ClickableControl ccDown = { ClickableControl::BTN_POOL_DOWN, rcDown, (int)p };
                int idxDown = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccDown);
                DrawModernButton(hmemDC, rcDown, L"", pState->hoveredControlIdx == idxDown, pState->pressedControlIdx == idxDown, false, pState->hFontMain, pState->hFontIconSmall, L"\xE74B");

                // Delete Pool
                hdrBtnX += 32;
                RECT rcDelPool = { hdrBtnX, hdrBtnY, hdrBtnX + hdrBtnW, hdrBtnY + hdrBtnH };
                ClickableControl ccDelP = { ClickableControl::BTN_POOL_DELETE, rcDelPool, (int)p };
                int idxDelP = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccDelP);
                DrawModernButton(hmemDC, rcDelPool, L"", pState->hoveredControlIdx == idxDelP, pState->pressedControlIdx == idxDelP, false, pState->hFontMain, pState->hFontIconSmall, L"\xE74D");

                if (!pool.isCollapsed)
                {
                    // --- Rules Row ---
                int rulesY = rcCard.top + 42;

                std::wstring modeStr = (pool.pickMode == PoolManager::PoolPickMode::Random) ? L"Pick: Random" : L"Pick: Sequential";
                RECT rcMode = { rcCard.left + 14, rulesY, rcCard.left + 130, rulesY + 26 };
                ClickableControl ccMode = { ClickableControl::BTN_POOL_MODE_TOGGLE, rcMode, (int)p };
                int idxMode = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMode);
                DrawModernButton(hmemDC, rcMode, modeStr.c_str(), pState->hoveredControlIdx == idxMode, pState->pressedControlIdx == idxMode, false, pState->hFontSmall);

                int countX = rcCard.left + 145;
                SelectObject(hmemDC, pState->hFontSmall);
                SetTextColor(hmemDC, RGB(180, 180, 180));
                RECT rcMinLbl = { countX, rulesY, countX + 30, rulesY + 26 };
                DrawTextW(hmemDC, L"Min:", -1, &rcMinLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Min [-]
                countX += 32;
                RECT rcMinDec = { countX, rulesY, countX + 22, rulesY + 26 };
                ClickableControl ccMinDec = { ClickableControl::BTN_POOL_MIN_DEC, rcMinDec, (int)p };
                int idxMinDec = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMinDec);
                DrawModernButton(hmemDC, rcMinDec, L"-", pState->hoveredControlIdx == idxMinDec, pState->pressedControlIdx == idxMinDec, false, pState->hFontSmall);

                // Min Value Box (Interactive & Clickable to edit)
                countX += 24;
                RECT rcMinVal = { countX, rulesY, countX + 38, rulesY + 26 };
                ClickableControl ccMinVal = { ClickableControl::BTN_POOL_MIN_EDIT, rcMinVal, (int)p };
                int idxMinVal = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMinVal);

                bool isMinValHover = (pState->hoveredControlIdx == idxMinVal);
                COLORREF minValBg = isMinValHover ? RGB(45, 45, 45) : RGB(22, 22, 22);
                COLORREF minValBorder = isMinValHover ? RGB(0, 160, 255) : RGB(60, 60, 60);

                HBRUSH hbrMinVal = CreateSolidBrush(minValBg);
                HPEN hPenMinVal = CreatePen(PS_SOLID, 1, minValBorder);
                SelectObject(hmemDC, hbrMinVal);
                SelectObject(hmemDC, hPenMinVal);
                RoundRect(hmemDC, rcMinVal.left, rcMinVal.top, rcMinVal.right, rcMinVal.bottom, 4, 4);
                DeleteObject(hbrMinVal);
                DeleteObject(hPenMinVal);

                std::wstring minValStr = std::to_wstring(pool.minCount);
                SelectObject(hmemDC, pState->hFontMainBold);
                SetTextColor(hmemDC, RGB(255, 255, 255));
                DrawTextW(hmemDC, minValStr.c_str(), -1, &rcMinVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Min [+]
                countX += 40;
                RECT rcMinInc = { countX, rulesY, countX + 22, rulesY + 26 };
                ClickableControl ccMinInc = { ClickableControl::BTN_POOL_MIN_INC, rcMinInc, (int)p };
                int idxMinInc = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMinInc);
                DrawModernButton(hmemDC, rcMinInc, L"+", pState->hoveredControlIdx == idxMinInc, pState->pressedControlIdx == idxMinInc, false, pState->hFontSmall);

                // Max Label
                countX += 34;
                SelectObject(hmemDC, pState->hFontSmall);
                SetTextColor(hmemDC, RGB(180, 180, 180));
                RECT rcMaxLbl = { countX, rulesY, countX + 32, rulesY + 26 };
                DrawTextW(hmemDC, L"Max:", -1, &rcMaxLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Max [-]
                countX += 34;
                RECT rcMaxDec = { countX, rulesY, countX + 22, rulesY + 26 };
                ClickableControl ccMaxDec = { ClickableControl::BTN_POOL_MAX_DEC, rcMaxDec, (int)p };
                int idxMaxDec = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMaxDec);
                DrawModernButton(hmemDC, rcMaxDec, L"-", pState->hoveredControlIdx == idxMaxDec, pState->pressedControlIdx == idxMaxDec, false, pState->hFontSmall);

                // Max Value Box (Interactive & Clickable to edit)
                countX += 24;
                RECT rcMaxVal = { countX, rulesY, countX + 38, rulesY + 26 };
                ClickableControl ccMaxVal = { ClickableControl::BTN_POOL_MAX_EDIT, rcMaxVal, (int)p };
                int idxMaxVal = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMaxVal);

                bool isMaxValHover = (pState->hoveredControlIdx == idxMaxVal);
                COLORREF maxValBg = isMaxValHover ? RGB(45, 45, 45) : RGB(22, 22, 22);
                COLORREF maxValBorder = isMaxValHover ? RGB(0, 160, 255) : RGB(60, 60, 60);

                HBRUSH hbrMaxVal = CreateSolidBrush(maxValBg);
                HPEN hPenMaxVal = CreatePen(PS_SOLID, 1, maxValBorder);
                SelectObject(hmemDC, hbrMaxVal);
                SelectObject(hmemDC, hPenMaxVal);
                RoundRect(hmemDC, rcMaxVal.left, rcMaxVal.top, rcMaxVal.right, rcMaxVal.bottom, 4, 4);
                DeleteObject(hbrMaxVal);
                DeleteObject(hPenMaxVal);

                std::wstring maxValStr = std::to_wstring(pool.maxCount);
                SelectObject(hmemDC, pState->hFontMainBold);
                SetTextColor(hmemDC, RGB(255, 255, 255));
                DrawTextW(hmemDC, maxValStr.c_str(), -1, &rcMaxVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Max [+]
                countX += 40;
                RECT rcMaxInc = { countX, rulesY, countX + 22, rulesY + 26 };
                ClickableControl ccMaxInc = { ClickableControl::BTN_POOL_MAX_INC, rcMaxInc, (int)p };
                int idxMaxInc = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccMaxInc);
                DrawModernButton(hmemDC, rcMaxInc, L"+", pState->hoveredControlIdx == idxMaxInc, pState->pressedControlIdx == idxMaxInc, false, pState->hFontSmall);

                // Pool Flip Policy Toggle Button
                countX += 36;
                RECT rcFlip = { countX, rulesY, rcCard.right - 14, rulesY + 26 };
                std::wstring flipStr = L"Orientation: Forward Only";
                if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                    flipStr = L"Orientation: 🔀 Random Flip";
                else if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped)
                    flipStr = L"Orientation: 🔄 Always Flipped";

                ClickableControl ccFlip = { ClickableControl::BTN_POOL_FLIP_TOGGLE, rcFlip, (int)p };
                int idxFlip = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccFlip);
                DrawModernButton(hmemDC, rcFlip, flipStr.c_str(), pState->hoveredControlIdx == idxFlip, pState->pressedControlIdx == idxFlip, pool.flipPolicy != PoolManager::PoolFlipPolicy::ForwardOnly, pState->hFontSmall);

                // --- Units Area ---
                int unitsBoxY = rulesY + 34;
                RECT rcUnitsBox = { rcCard.left + 12, unitsBoxY, rcCard.right - 12, unitsBoxY + unitsAreaH };
                pState->poolUnitsBoxHits.push_back({ (int)p, rcUnitsBox });

                COLORREF unitsBoxBg = isDragOver ? RGB(18, 30, 44) : PoolTheme::UnitsBoxBackground;
                COLORREF unitsBoxBorder = isDragOver ? RGB(0, 160, 255) : PoolTheme::UnitsBoxBorder;

                HBRUSH hbrUnitsBox = CreateSolidBrush(unitsBoxBg);
                HPEN hPenUnitsBox = CreatePen(isDragOver ? PS_SOLID : PS_SOLID, isDragOver ? 2 : 1, unitsBoxBorder);
                SelectObject(hmemDC, hbrUnitsBox);
                SelectObject(hmemDC, hPenUnitsBox);
                RoundRect(hmemDC, rcUnitsBox.left, rcUnitsBox.top, rcUnitsBox.right, rcUnitsBox.bottom, 6, 6);
                DeleteObject(hbrUnitsBox);
                DeleteObject(hPenUnitsBox);

                if (isDragOver)
                {
                    SelectObject(hmemDC, pState->hFontMainBold);
                    SetTextColor(hmemDC, RGB(0, 180, 255));
                    DrawTextW(hmemDC, L"➕ Drop rolling stock units here to add to this pool", -1, &rcUnitsBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else if (unitCount == 0)
                {
                    SelectObject(hmemDC, pState->hFontSmall);
                    SetTextColor(hmemDC, RGB(130, 130, 130));
                    DrawTextW(hmemDC, L"Drag & Drop units here from stock library, or click Paste from Clipboard below.", -1, &rcUnitsBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else
                {
                    int chipW = (rcUnitsBox.right - rcUnitsBox.left - 18) / 2;
                    int chipH = 24;

                    for (int u = 0; u < unitCount; ++u)
                    {
                        int col = u % 2;
                        int row = u / 2;
                        int cx = rcUnitsBox.left + 6 + col * (chipW + 6);
                        int cy = rcUnitsBox.top + 6 + row * (chipH + 4);

                        RECT rcChip = { cx, cy, cx + chipW, cy + chipH };
                        pState->unitChipHits.push_back({ (int)p, u, rcChip });

                        bool isSelected = (pState->selectedPoolIdx == (int)p && pState->selectedUnitIndices.count(u) > 0);

                        COLORREF chipBg = isSelected ? RGB(0, 100, 180) : PoolTheme::ChipBackground;
                        COLORREF chipBorder = isSelected ? RGB(0, 180, 255) : PoolTheme::ChipBorder;

                        HBRUSH hbrChip = CreateSolidBrush(chipBg);
                        HPEN hPenChip = CreatePen(PS_SOLID, 1, chipBorder);
                        SelectObject(hmemDC, hbrChip);
                        SelectObject(hmemDC, hPenChip);
                        RoundRect(hmemDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                        DeleteObject(hbrChip);
                        DeleteObject(hPenChip);

                        // Icon (Engine / Wagon)
                        SelectObject(hmemDC, pState->hFontIconSmall);
                        COLORREF iconCol;
                        if (isSelected)
                            iconCol = pool.units[u].isEngine ? RGB(180, 235, 255) : RGB(255, 230, 150);
                        else
                            iconCol = pool.units[u].isEngine ? RGB(0, 180, 255) : RGB(255, 170, 0);

                        SetTextColor(hmemDC, iconCol);
                        RECT rcUnitIcon = { rcChip.left + 4, rcChip.top, rcChip.left + 20, rcChip.bottom };
                        DrawTextW(hmemDC, pool.units[u].isEngine ? L"\xE7C3" : L"\xE8EC", -1, &rcUnitIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // Delete button [✕]
                        RECT rcRemoveBtn = { rcChip.right - 20, rcChip.top + 2, rcChip.right - 2, rcChip.bottom - 2 };
                        ClickableControl ccRem = { ClickableControl::BTN_UNIT_REMOVE, rcRemoveBtn, (int)p, u };
                        int idxRem = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccRem);

                        // Unit Flip Mode Badge [Auto / ➡ Fwd / 🔄 Rev / 🔀 Rnd]
                        RECT rcUnitFlip = { rcRemoveBtn.left - 52, rcChip.top + 3, rcRemoveBtn.left - 4, rcChip.bottom - 3 };
                        ClickableControl ccUnitFlip = { ClickableControl::BTN_UNIT_FLIP_TOGGLE, rcUnitFlip, (int)p, u };
                        int idxUnitFlip = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccUnitFlip);

                        bool isFlipHover = (pState->hoveredControlIdx == idxUnitFlip);
                        std::wstring flipBadgeText = L"Auto";
                        COLORREF flipBadgeBg = isFlipHover ? RGB(55, 55, 55) : RGB(44, 44, 44);
                        COLORREF flipBadgeBorder = RGB(70, 70, 70);
                        COLORREF flipBadgeTextCol = RGB(160, 160, 160);

                        switch (pool.units[u].flipMode)
                        {
                        case PoolManager::UnitFlipMode::Forward:
                            flipBadgeText = L"➡ Fwd";
                            flipBadgeBg = isFlipHover ? RGB(18, 62, 38) : RGB(14, 48, 28);
                            flipBadgeBorder = RGB(40, 180, 100);
                            flipBadgeTextCol = RGB(100, 240, 150);
                            break;

                        case PoolManager::UnitFlipMode::Flipped:
                            flipBadgeText = L"🔄 Rev";
                            flipBadgeBg = isFlipHover ? RGB(65, 36, 18) : RGB(52, 28, 14);
                            flipBadgeBorder = RGB(240, 130, 40);
                            flipBadgeTextCol = RGB(255, 170, 80);
                            break;

                        case PoolManager::UnitFlipMode::Random:
                            flipBadgeText = L"🔀 Rnd";
                            flipBadgeBg = isFlipHover ? RGB(52, 28, 68) : RGB(40, 20, 54);
                            flipBadgeBorder = RGB(170, 100, 240);
                            flipBadgeTextCol = RGB(215, 155, 255);
                            break;

                        default:
                            break;
                        }

                        if (isSelected)
                        {
                            flipBadgeBg = isFlipHover ? RGB(0, 80, 150) : RGB(0, 60, 120);
                            flipBadgeBorder = RGB(0, 190, 255);
                            flipBadgeTextCol = RGB(255, 255, 255);
                        }

                        HBRUSH hbrFBadge = CreateSolidBrush(flipBadgeBg);
                        HPEN hPenFBadge = CreatePen(PS_SOLID, 1, flipBadgeBorder);
                        SelectObject(hmemDC, hbrFBadge);
                        SelectObject(hmemDC, hPenFBadge);
                        RoundRect(hmemDC, rcUnitFlip.left, rcUnitFlip.top, rcUnitFlip.right, rcUnitFlip.bottom, 3, 3);
                        DeleteObject(hbrFBadge);
                        DeleteObject(hPenFBadge);

                        SelectObject(hmemDC, pState->hFontBadge);
                        SetTextColor(hmemDC, flipBadgeTextCol);
                        DrawTextW(hmemDC, flipBadgeText.c_str(), -1, &rcUnitFlip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // File name and folder text (clickable to copy unit)
                        std::wstring uText = pool.units[u].szFileName + L" [" + pool.units[u].szFolder + L"]";
                        SelectObject(hmemDC, pState->hFontSmall);
                        SetTextColor(hmemDC, isSelected ? RGB(255, 255, 255) : RGB(225, 225, 225));
                        RECT rcUText = { rcChip.left + 22, rcChip.top, rcUnitFlip.left - 4, rcChip.bottom };
                        DrawTextW(hmemDC, uText.c_str(), -1, &rcUText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                        ClickableControl ccUnitCopy = { ClickableControl::BTN_UNIT_COPY, rcUText, (int)p, (int)u };
                        pState->clickControls.push_back(ccUnitCopy);

                        // Draw delete button [✕]
                        COLORREF remCol = (pState->hoveredControlIdx == idxRem) ? RGB(255, 80, 80) : (isSelected ? RGB(240, 240, 240) : RGB(140, 140, 140));
                        SetTextColor(hmemDC, remCol);
                        SelectObject(hmemDC, pState->hFontSmall);
                        DrawTextW(hmemDC, L"✕", -1, &rcRemoveBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                }

                // --- Card Footer ---
                int cardFootY = rcUnitsBox.bottom + 6;
                int cfBtnH = 26;
                bool hasSelection = (pState->selectedPoolIdx == (int)p && !pState->selectedUnitIndices.empty());
                int numSel = hasSelection ? (int)pState->selectedUnitIndices.size() : 0;

                int footCurX = rcCard.left + 12;

                if (hasSelection)
                {
                    // [📋 Copy Selected (N)]
                    std::wstring copySelStr = L"Copy Selected (" + std::to_wstring(numSel) + L")";
                    int btnCopyW = 125 + (numSel > 9 ? 10 : 0);
                    RECT rcCopySelBtn = { footCurX, cardFootY, footCurX + btnCopyW, cardFootY + cfBtnH };
                    ClickableControl ccCopySel = { ClickableControl::BTN_POOL_COPY_SELECTED, rcCopySelBtn, (int)p };
                    int idxCopySel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccCopySel);
                    DrawModernButton(hmemDC, rcCopySelBtn, copySelStr.c_str(), pState->hoveredControlIdx == idxCopySel, pState->pressedControlIdx == idxCopySel, true, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");
                    footCurX += btnCopyW + 8;

                    // [🔄 Flip (N)]
                    std::wstring flipSelStr = L"Flip (" + std::to_wstring(numSel) + L")";
                    int btnFlipW = 85 + (numSel > 9 ? 10 : 0);
                    RECT rcFlipSelBtn = { footCurX, cardFootY, footCurX + btnFlipW, cardFootY + cfBtnH };
                    ClickableControl ccFlipSel = { ClickableControl::BTN_POOL_FLIP_SELECTED, rcFlipSelBtn, (int)p };
                    int idxFlipSel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccFlipSel);
                    DrawModernButton(hmemDC, rcFlipSelBtn, flipSelStr.c_str(), pState->hoveredControlIdx == idxFlipSel, pState->pressedControlIdx == idxFlipSel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE777");
                    footCurX += btnFlipW + 8;

                    // [🗑 Delete (N)]
                    std::wstring delSelStr = L"Delete (" + std::to_wstring(numSel) + L")";
                    int btnDelW = 95 + (numSel > 9 ? 10 : 0);
                    RECT rcDelSelBtn = { footCurX, cardFootY, footCurX + btnDelW, cardFootY + cfBtnH };
                    ClickableControl ccDelSel = { ClickableControl::BTN_POOL_DELETE_SELECTED, rcDelSelBtn, (int)p };
                    int idxDelSel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccDelSel);
                    DrawModernButton(hmemDC, rcDelSelBtn, delSelStr.c_str(), pState->hoveredControlIdx == idxDelSel, pState->pressedControlIdx == idxDelSel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");
                    footCurX += btnDelW + 8;

                    // [📋 Paste]
                    RECT rcPasteBtn = { footCurX, cardFootY, footCurX + 70, cardFootY + cfBtnH };
                    ClickableControl ccPaste = { ClickableControl::BTN_POOL_PASTE_CLIPBOARD, rcPasteBtn, (int)p };
                    int idxPaste = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPaste);
                    DrawModernButton(hmemDC, rcPasteBtn, L"Paste", pState->hoveredControlIdx == idxPaste, pState->pressedControlIdx == idxPaste, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");
                    footCurX += 70 + 8;

                    RECT rcCountBadge = { footCurX, cardFootY, rcCard.right - 12, cardFootY + cfBtnH };
                    std::wstring countStr = std::to_wstring(numSel) + L" of " + std::to_wstring(unitCount) + L" selected";
                    SelectObject(hmemDC, pState->hFontSmall);
                    SetTextColor(hmemDC, RGB(96, 205, 255));
                    DrawTextW(hmemDC, countStr.c_str(), -1, &rcCountBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else
                {
                    // [📋 Copy Units]
                    RECT rcCopyBtn = { footCurX, cardFootY, footCurX + 105, cardFootY + cfBtnH };
                    ClickableControl ccCopy = { ClickableControl::BTN_POOL_COPY_UNITS, rcCopyBtn, (int)p };
                    int idxCopy = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccCopy);
                    DrawModernButton(hmemDC, rcCopyBtn, L"Copy Units", pState->hoveredControlIdx == idxCopy, pState->pressedControlIdx == idxCopy, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");
                    footCurX += 105 + 8;

                    // [📋 Paste from Clipboard]
                    RECT rcPasteBtn = { footCurX, cardFootY, footCurX + 155, cardFootY + cfBtnH };
                    ClickableControl ccPaste = { ClickableControl::BTN_POOL_PASTE_CLIPBOARD, rcPasteBtn, (int)p };
                    int idxPaste = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPaste);
                    DrawModernButton(hmemDC, rcPasteBtn, L"Paste from Clipboard", pState->hoveredControlIdx == idxPaste, pState->pressedControlIdx == idxPaste, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");
                    footCurX += 155 + 8;

                    // [🗑 Clear Units]
                    RECT rcClearBtn = { footCurX, cardFootY, footCurX + 95, cardFootY + cfBtnH };
                    ClickableControl ccClear = { ClickableControl::BTN_POOL_CLEAR_UNITS, rcClearBtn, (int)p };
                    int idxClear = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccClear);
                    DrawModernButton(hmemDC, rcClearBtn, L"Clear Units", pState->hoveredControlIdx == idxClear, pState->pressedControlIdx == idxClear, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");
                    footCurX += 95 + 8;

                    RECT rcCountBadge = { footCurX, cardFootY, rcCard.right - 12, cardFootY + cfBtnH };
                    std::wstring countStr = std::to_wstring(unitCount) + L" unit(s) in pool";
                    SelectObject(hmemDC, pState->hFontSmall);
                    SetTextColor(hmemDC, RGB(160, 160, 160));
                    DrawTextW(hmemDC, countStr.c_str(), -1, &rcCountBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }

                } // end if (!pool.isCollapsed)

                cardY += cardH + 14;
            }
        }

        // Draw Card Drag Drop Insertion Indicator Line
        if (pState->draggingPoolIdx >= 0 && pState->cardDropTargetIdx >= 0 && !pState->cardHits.empty())
        {
            int yLine = 0;
            if (pState->cardDropTargetIdx == 0)
            {
                yLine = pState->cardHits[0].rcCard.top - 7;
            }
            else if (pState->cardDropTargetIdx < (int)pState->cardHits.size())
            {
                yLine = (pState->cardHits[pState->cardDropTargetIdx - 1].rcCard.bottom + pState->cardHits[pState->cardDropTargetIdx].rcCard.top) / 2;
            }
            else
            {
                yLine = pState->cardHits.back().rcCard.bottom + 7;
            }

            if (yLine >= contentY && yLine <= contentY + contentH)
            {
                HPEN hPenLine = CreatePen(PS_SOLID, 3, RGB(0, 160, 255));
                HPEN hOldP = (HPEN)SelectObject(hmemDC, hPenLine);
                MoveToEx(hmemDC, cardMargin, yLine, NULL);
                LineTo(hmemDC, cardMargin + cardW, yLine);
                SelectObject(hmemDC, hOldP);
                DeleteObject(hPenLine);

                // Left arrow indicator ◄
                POINT ptLeft[3] = { { cardMargin, yLine - 6 }, { cardMargin, yLine + 6 }, { cardMargin + 10, yLine } };
                HBRUSH hBrTri = CreateSolidBrush(RGB(0, 160, 255));
                HBRUSH hOldBrTri = (HBRUSH)SelectObject(hmemDC, hBrTri);
                HPEN hPenTri = CreatePen(PS_SOLID, 1, RGB(0, 160, 255));
                HPEN hOldPTri = (HPEN)SelectObject(hmemDC, hPenTri);
                Polygon(hmemDC, ptLeft, 3);

                // Right arrow indicator ►
                POINT ptRight[3] = { { cardMargin + cardW, yLine - 6 }, { cardMargin + cardW, yLine + 6 }, { cardMargin + cardW - 10, yLine } };
                Polygon(hmemDC, ptRight, 3);

                SelectObject(hmemDC, hOldBrTri);
                SelectObject(hmemDC, hOldPTri);
                DeleteObject(hBrTri);
                DeleteObject(hPenTri);
            }
        }

        // Render Rubber-Band / Marquee Selection Box if active
        if (pState->isMarqueeSelecting && pState->marqueePoolIdx >= 0)
        {
            RECT rcBox = { 0 };
            for (const auto& bh : pState->poolUnitsBoxHits)
            {
                if (bh.poolIdx == pState->marqueePoolIdx) { rcBox = bh.rcUnitsBox; break; }
            }

            RECT rcNorm;
            rcNorm.left   = (std::min)(pState->ptMarqueeStart.x, pState->ptMarqueeCurrent.x);
            rcNorm.right  = (std::max)(pState->ptMarqueeStart.x, pState->ptMarqueeCurrent.x);
            rcNorm.top    = (std::min)(pState->ptMarqueeStart.y, pState->ptMarqueeCurrent.y);
            rcNorm.bottom = (std::max)(pState->ptMarqueeStart.y, pState->ptMarqueeCurrent.y);

            RECT rcClipped;
            if (IntersectRect(&rcClipped, &rcNorm, &rcBox))
            {
                int bw = rcClipped.right - rcClipped.left;
                int bh = rcClipped.bottom - rcClipped.top;
                if (bw > 2 && bh > 2)
                {
                    HDC hBoxDC = CreateCompatibleDC(hmemDC);
                    HBITMAP hBoxBmp = CreateCompatibleBitmap(hmemDC, bw, bh);
                    HBITMAP hOldBB = (HBITMAP)SelectObject(hBoxDC, hBoxBmp);
                    HBRUSH hbrBlue = CreateSolidBrush(RGB(0, 120, 215));
                    RECT rcFullBox = { 0, 0, bw, bh };
                    FillRect(hBoxDC, &rcFullBox, hbrBlue);
                    DeleteObject(hbrBlue);

                    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 60, 0 };
                    AlphaBlend(hmemDC, rcClipped.left, rcClipped.top, bw, bh, hBoxDC, 0, 0, bw, bh, bf);

                    SelectObject(hBoxDC, hOldBB);
                    DeleteObject(hBoxBmp);
                    DeleteDC(hBoxDC);

                    HPEN hPenMarquee = CreatePen(PS_SOLID, 1, RGB(0, 160, 255));
                    HGDIOBJ oldP = SelectObject(hmemDC, hPenMarquee);
                    HGDIOBJ oldB = SelectObject(hmemDC, GetStockObject(NULL_BRUSH));
                    Rectangle(hmemDC, rcClipped.left, rcClipped.top, rcClipped.right, rcClipped.bottom);
                    SelectObject(hmemDC, oldB);
                    SelectObject(hmemDC, oldP);
                    DeleteObject(hPenMarquee);
                }
            }
        }

        // Reset clipping
        SelectClipRgn(hmemDC, NULL);
        DeleteObject(hRgnClip);

        // 4b. Draw Custom Fluent ScrollBar
        pState->m_vScroll.Paint(hmemDC, PoolTheme::GutterBackground);

        // 5. Bottom Footer Action Bar (Height = 56px)
        int footerY = h - footerH;
        RECT rcFooter = { 0, footerY, w, h };
        COLORREF footerBg = PoolTheme::FooterBackground;
        HBRUSH hbrFooter = CreateSolidBrush(footerBg);
        FillRect(hmemDC, &rcFooter, hbrFooter);
        DeleteObject(hbrFooter);

        SelectObject(hmemDC, hPenLine);
        MoveToEx(hmemDC, 0, footerY, NULL);
        LineTo(hmemDC, w, footerY);
        SelectObject(hmemDC, hOldPen);
        DeleteObject(hPenLine);

        // Footer Action Button: [Generate Consists...] (Accent)
        int footBtnH = 34;
        int footBtnY = footerY + (footerH - footBtnH) / 2;
        int genBtnW = 190;

        RECT rcGen = { w - genBtnW - 20, footBtnY, w - 20, footBtnY + footBtnH };
        ClickableControl ccGen = { ClickableControl::BTN_GENERATE, rcGen };
        int idxGen = (int)pState->clickControls.size();
        pState->clickControls.push_back(ccGen);
        DrawModernButton(hmemDC, rcGen, L"Generate Consists...", pState->hoveredControlIdx == idxGen, pState->pressedControlIdx == idxGen, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE768");

        // 6. Draw Preset Dropdown Menu if open
        if (pState->isPresetDropdownOpen)
        {
            pState->presetItemRects.clear();
            const auto& allPresets = PoolManager::g_PoolPresetsCache;
            int itemH = 28;
            int dropW = 214;
            int dropH = (int)allPresets.size() * itemH + 8;
            int dropX = pState->rcPresetDropdown.left;
            int dropY = pState->rcPresetDropdown.bottom + 2;

            RECT rcDropMenu = { dropX, dropY, dropX + dropW, dropY + dropH };
            COLORREF dropBg = RGB(34, 34, 34);
            COLORREF dropBorder = RGB(60, 60, 60);

            HBRUSH hbrDrop = CreateSolidBrush(dropBg);
            HPEN hPenDrop = CreatePen(PS_SOLID, 1, dropBorder);
            SelectObject(hmemDC, hbrDrop);
            SelectObject(hmemDC, hPenDrop);
            RoundRect(hmemDC, rcDropMenu.left, rcDropMenu.top, rcDropMenu.right, rcDropMenu.bottom, 6, 6);
            DeleteObject(hbrDrop);
            DeleteObject(hPenDrop);

            int activeIdx = PoolManager::g_ActivePresetIndex;
            for (size_t i = 0; i < allPresets.size(); ++i)
            {
                int itemY = dropY + 4 + (int)i * itemH;
                RECT rcItem = { dropX + 4, itemY, dropX + dropW - 4, itemY + itemH };
                pState->presetItemRects.push_back(rcItem);

                bool isItemActive = ((int)i == activeIdx);
                if (isItemActive)
                {
                    COLORREF itemBg = RGB(50, 50, 50);
                    HBRUSH hbrItem = CreateSolidBrush(itemBg);
                    FillRect(hmemDC, &rcItem, hbrItem);
                    DeleteObject(hbrItem);
                }

                SelectObject(hmemDC, isItemActive ? pState->hFontMainBold : pState->hFontMain);
                SetTextColor(hmemDC, isItemActive ? RGB(0, 180, 255) : RGB(240, 240, 240));
                RECT rcItemText = rcItem;
                rcItemText.left += 8;
                DrawTextW(hmemDC, allPresets[i].presetName.c_str(), -1, &rcItemText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }

        // 7. Outer 1px Self-Drawn Border (Independent of OS version / theme)
        HPEN hPenOuter = CreatePen(PS_SOLID, 1, PoolTheme::BorderLine);
        HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldPenOuter = (HPEN)SelectObject(hmemDC, hPenOuter);
        HBRUSH hOldBrOuter = (HBRUSH)SelectObject(hmemDC, hNullBr);
        Rectangle(hmemDC, 0, 0, w, h);
        SelectObject(hmemDC, hOldPenOuter);
        SelectObject(hmemDC, hOldBrOuter);
        DeleteObject(hPenOuter);

        BitBlt(hdc, 0, 0, w, h, hmemDC, 0, 0, SRCCOPY);
        SelectObject(hmemDC, holdBm);
        DeleteObject(hbm);
        DeleteDC(hmemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;
        lpmmi->ptMinTrackSize.x = 880;
        lpmmi->ptMinTrackSize.y = 520;
        return 0;
    }

    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
    {
        if (pState)
        {
            pState->m_vScroll.OnLButtonUp({ 0, 0 }, hWnd);
            KillTimer(hWnd, TIMER_REPEAT_ID);
            pState->isMarqueeSelecting = false;
            pState->isPotentialMarquee = false;
            pState->repeatAction = ClickableControl::NONE;
            pState->repeatPoolIdx = -1;
            pState->repeatHoldCount = 0;
            pState->pressedControlIdx = -1;
            pState->isTopCloseHover = false;
            pState->dragOverPoolIdx = -1;

            if (pState->draggingPoolIdx >= 0 || pState->isPotentialCardDrag)
            {
                FluentDragGhost::Hide();
                pState->draggingPoolIdx = -1;
                pState->cardDropTargetIdx = -1;
                pState->isPotentialCardDrag = false;
            }

            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            if (pState && pState->hParent && IsWindow(pState->hParent))
            {
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pState && pState->m_vScroll.OnMouseMove(pt, hWnd))
        {
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
        }

        // Handle Potential Card Drag initiation
        if (pState && pState->isPotentialCardDrag && pState->draggingPoolIdx < 0)
        {
            int dx = abs(pt.x - pState->ptCardDragStart.x);
            int dy = abs(pt.y - pState->ptCardDragStart.y);
            if (dx > 4 || dy > 4)
            {
                pState->draggingPoolIdx = pState->potentialCardDragIdx;
                pState->isPotentialCardDrag = false;

                // Initialize FluentDragGhost
                PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                if (pPreset && pState->draggingPoolIdx >= 0 && pState->draggingPoolIdx < (int)pPreset->pools.size())
                {
                    const auto& pool = pPreset->pools[pState->draggingPoolIdx];
                    std::vector<DragGhostItem> ghostItems;
                    DragGhostItem gItem;
                    gItem.type = GhostItemType::PoolCard;
                    gItem.name = L"Pool " + std::to_wstring(pState->draggingPoolIdx + 1) + L": " + pool.name;
                    int uCount = (int)pool.units.size();
                    gItem.subtitle = std::to_wstring(uCount) + (uCount == 1 ? L" Unit" : L" Units") + L" · " + (pool.pickMode == PoolManager::PoolPickMode::Random ? L"Random" : L"Sequential") + L" (" + std::to_wstring(pool.minCount) + L"-" + std::to_wstring(pool.maxCount) + L")";
                    gItem.isEngine = false;
                    ghostItems.push_back(gItem);

                    POINT ptScreen = pt;
                    ClientToScreen(hWnd, &ptScreen);
                    FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
                }
            }
        }

        // Handle Active Card Dragging
        if (pState && pState->draggingPoolIdx >= 0)
        {
            // Auto-scroll near edge of content area
            int contentY = 43 + 46 + 1; // 90
            int footerH = 56;
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int contentH = rcClient.bottom - footerH - contentY;

            if (pt.y < contentY + 30 && pState->scrollY > 0)
            {
                pState->m_vScroll.SetPos((std::max)(0, pState->scrollY - 15));
                pState->scrollY = pState->m_vScroll.GetPos();
            }
            else if (pt.y > contentY + contentH - 30 && pState->scrollY < pState->maxScrollY)
            {
                pState->m_vScroll.SetPos((std::min)(pState->maxScrollY, pState->scrollY + 15));
                pState->scrollY = pState->m_vScroll.GetPos();
            }

            // Calculate drop target index
            int dropIdx = (int)pState->cardHits.size();
            for (int i = 0; i < (int)pState->cardHits.size(); ++i)
            {
                int midY = (pState->cardHits[i].rcCard.top + pState->cardHits[i].rcCard.bottom) / 2;
                if (pt.y < midY)
                {
                    dropIdx = i;
                    break;
                }
            }

            if (dropIdx != pState->cardDropTargetIdx)
            {
                pState->cardDropTargetIdx = dropIdx;
            }

            POINT ptScreen = pt;
            ClientToScreen(hWnd, &ptScreen);
            std::wstring badgeText = L"Move to Position #" + std::to_wstring(dropIdx + 1);
            FluentDragGhost::Move(ptScreen, true, badgeText);

            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        int newHovered = -1;

        bool hTopClose = PtInRect(&pState->rcTopCloseBtn, pt) != FALSE;
        if (hTopClose != pState->isTopCloseHover)
        {
            pState->isTopCloseHover = hTopClose;
            InvalidateRect(hWnd, &pState->rcTopCloseBtn, FALSE);
        }

        if (!pState->isPresetDropdownOpen && !pState->isMarqueeSelecting)
        {
            for (int i = 0; i < (int)pState->clickControls.size(); ++i)
            {
                if (PtInRect(&pState->clickControls[i].rc, pt))
                {
                    newHovered = i;
                    break;
                }
            }
        }

        if (pState->isPotentialMarquee)
        {
            if (abs(pt.x - pState->ptMarqueeStart.x) > 3 || abs(pt.y - pState->ptMarqueeStart.y) > 3)
            {
                pState->isMarqueeSelecting = true;
            }
        }

        if (pState->isMarqueeSelecting && pState->marqueePoolIdx >= 0)
        {
            pState->ptMarqueeCurrent = pt;

            RECT rcNorm;
            rcNorm.left   = (std::min)(pState->ptMarqueeStart.x, pState->ptMarqueeCurrent.x);
            rcNorm.right  = (std::max)(pState->ptMarqueeStart.x, pState->ptMarqueeCurrent.x);
            rcNorm.top    = (std::min)(pState->ptMarqueeStart.y, pState->ptMarqueeCurrent.y);
            rcNorm.bottom = (std::max)(pState->ptMarqueeStart.y, pState->ptMarqueeCurrent.y);

            bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            std::unordered_set<int> newSel = isCtrl ? pState->marqueeInitialSelection : std::unordered_set<int>();

            for (const auto& chip : pState->unitChipHits)
            {
                if (chip.poolIdx == pState->marqueePoolIdx)
                {
                    RECT rcInter;
                    if (IntersectRect(&rcInter, &rcNorm, &chip.rcChip))
                    {
                        newSel.insert(chip.unitIdx);
                    }
                }
            }

            pState->selectedPoolIdx = pState->marqueePoolIdx;
            pState->selectedUnitIndices = newSel;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (newHovered != pState->hoveredControlIdx)
        {
            pState->hoveredControlIdx = newHovered;
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
        if (pState && pState->m_vScroll.OnMouseLeave(hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        pState->hoveredControlIdx = -1;
        if (pState->isTopCloseHover)
        {
            pState->isTopCloseHover = false;
            InvalidateRect(hWnd, &pState->rcTopCloseBtn, FALSE);
        }
        if (pState->repeatAction == ClickableControl::NONE)
            pState->pressedControlIdx = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&pState->rcTopCloseBtn, pt))
        {
            return 0;
        }

        if (pState && pState->m_vScroll.OnLButtonDown(pt, hWnd))
        {
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Handle Preset Dropdown open clicks
        if (pState->isPresetDropdownOpen)
        {
            for (size_t i = 0; i < pState->presetItemRects.size(); ++i)
            {
                if (PtInRect(&pState->presetItemRects[i], pt))
                {
                    PoolManager::g_ActivePresetIndex = (int)i;
                    pState->isPresetDropdownOpen = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            pState->isPresetDropdownOpen = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // 1. Check unit specific buttons first (Remove [✕], Flip Toggle)
        for (int i = 0; i < (int)pState->clickControls.size(); ++i)
        {
            const auto& ctrl = pState->clickControls[i];
            if (ctrl.type == ClickableControl::BTN_UNIT_REMOVE || ctrl.type == ClickableControl::BTN_UNIT_FLIP_TOGGLE)
            {
                if (PtInRect(&ctrl.rc, pt))
                {
                    pState->pressedControlIdx = i;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        // 2. Check if clicked on a Unit Chip for Selection (Click, Ctrl+Click, Shift+Click)
        for (const auto& chip : pState->unitChipHits)
        {
            if (PtInRect(&chip.rcChip, pt))
            {
                bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (chip.poolIdx != pState->selectedPoolIdx)
                {
                    pState->selectedPoolIdx = chip.poolIdx;
                    pState->selectedUnitIndices.clear();
                    pState->selectedUnitIndices.insert(chip.unitIdx);
                    pState->anchorUnitIdx = chip.unitIdx;
                }
                else
                {
                    if (isShift)
                    {
                        int start = (pState->anchorUnitIdx >= 0) ? pState->anchorUnitIdx : chip.unitIdx;
                        int minU = (std::min)(start, chip.unitIdx);
                        int maxU = (std::max)(start, chip.unitIdx);
                        if (!isCtrl)
                            pState->selectedUnitIndices.clear();
                        for (int u = minU; u <= maxU; ++u)
                        {
                            pState->selectedUnitIndices.insert(u);
                        }
                    }
                    else if (isCtrl)
                    {
                        if (pState->selectedUnitIndices.count(chip.unitIdx) > 0)
                            pState->selectedUnitIndices.erase(chip.unitIdx);
                        else
                            pState->selectedUnitIndices.insert(chip.unitIdx);
                        pState->anchorUnitIdx = chip.unitIdx;
                    }
                    else
                    {
                        pState->selectedUnitIndices.clear();
                        pState->selectedUnitIndices.insert(chip.unitIdx);
                        pState->anchorUnitIdx = chip.unitIdx;
                    }
                }

                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // 3. Check if clicked in empty area of a Pool's Units Box (Initiate Rubber-Band / Marquee Selection)
        for (const auto& bh : pState->poolUnitsBoxHits)
        {
            if (PtInRect(&bh.rcUnitsBox, pt))
            {
                bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (!isCtrl)
                {
                    pState->selectedUnitIndices.clear();
                    pState->anchorUnitIdx = -1;
                }
                pState->selectedPoolIdx = bh.poolIdx;
                pState->isPotentialMarquee = true;
                pState->isMarqueeSelecting = false;
                pState->ptMarqueeStart = pt;
                pState->ptMarqueeCurrent = pt;
                pState->marqueePoolIdx = bh.poolIdx;
                pState->marqueeInitialSelection = pState->selectedUnitIndices;
                SetCapture(hWnd);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // 4. Other Standard Buttons
        for (int i = 0; i < (int)pState->clickControls.size(); ++i)
        {
            if (PtInRect(&pState->clickControls[i].rc, pt))
            {
                pState->pressedControlIdx = i;
                const auto& ctrl = pState->clickControls[i];

                // Auto-repeat for Min/Max +/- buttons
                if (ctrl.type == ClickableControl::BTN_POOL_MIN_DEC ||
                    ctrl.type == ClickableControl::BTN_POOL_MIN_INC ||
                    ctrl.type == ClickableControl::BTN_POOL_MAX_DEC ||
                    ctrl.type == ClickableControl::BTN_POOL_MAX_INC)
                {
                    pState->repeatAction = ctrl.type;
                    pState->repeatPoolIdx = ctrl.poolIdx;
                    pState->repeatHoldCount = 0;

                    ExecuteStepAction(ctrl.type, ctrl.poolIdx, 1);
                    SetCapture(hWnd);
                    SetTimer(hWnd, TIMER_REPEAT_ID, 320, NULL);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // 5. Check if clicked on a Card Header to initiate Card Drag & Drop Reordering
        for (const auto& ch : pState->cardHits)
        {
            if (PtInRect(&ch.rcHeader, pt))
            {
                pState->isPotentialCardDrag = true;
                pState->potentialCardDragIdx = ch.poolIdx;
                pState->ptCardDragStart = pt;
                pState->draggingPoolIdx = -1;
                pState->cardDropTargetIdx = -1;
                SetCapture(hWnd);
                return 0;
            }
        }

        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (pState && pState->m_vScroll.OnLButtonUp(pt, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->repeatAction != ClickableControl::NONE)
        {
            KillTimer(hWnd, TIMER_REPEAT_ID);
            if (GetCapture() == hWnd) ReleaseCapture();
            pState->repeatAction = ClickableControl::NONE;
            pState->repeatPoolIdx = -1;
            pState->repeatHoldCount = 0;
        }

        // Handle Pool Card Drag Drop Completion
        if (pState->draggingPoolIdx >= 0)
        {
            int from = pState->draggingPoolIdx;
            int to = pState->cardDropTargetIdx;
            if (to > from) to--; // Adjust for element removal

            PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
            if (pPreset && from >= 0 && from < (int)pPreset->pools.size() && to >= 0 && to < (int)pPreset->pools.size() && from != to)
            {
                auto pool = pPreset->pools[from];
                pPreset->pools.erase(pPreset->pools.begin() + from);
                pPreset->pools.insert(pPreset->pools.begin() + to, pool);
                PoolManager::PersistPoolPresets();
            }

            FluentDragGhost::Hide();
            pState->draggingPoolIdx = -1;
            pState->cardDropTargetIdx = -1;
            pState->isPotentialCardDrag = false;
            if (GetCapture() == hWnd) ReleaseCapture();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->isPotentialCardDrag)
        {
            pState->isPotentialCardDrag = false;
            pState->potentialCardDragIdx = -1;
            if (GetCapture() == hWnd) ReleaseCapture();
        }

        if (pState->isMarqueeSelecting || pState->isPotentialMarquee)
        {
            pState->isMarqueeSelecting = false;
            pState->isPotentialMarquee = false;
            pState->marqueePoolIdx = -1;
            pState->marqueeInitialSelection.clear();
            if (GetCapture() == hWnd) ReleaseCapture();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (GetCapture() == hWnd) ReleaseCapture();

        if (PtInRect(&pState->rcTopCloseBtn, pt))
        {
            if (pState && pState->hParent && IsWindow(pState->hParent))
            {
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }

        int pressed = pState->pressedControlIdx;
        pState->pressedControlIdx = -1;

        int targetIdx = pressed;
        if (targetIdx < 0 || targetIdx >= (int)pState->clickControls.size())
        {
            for (int i = 0; i < (int)pState->clickControls.size(); ++i)
            {
                if (PtInRect(&pState->clickControls[i].rc, pt))
                {
                    targetIdx = i;
                    break;
                }
            }
        }

        if (targetIdx >= 0 && targetIdx < (int)pState->clickControls.size())
        {
            if (PtInRect(&pState->clickControls[targetIdx].rc, pt))
            {
                const auto& ctrl = pState->clickControls[targetIdx];
                switch (ctrl.type)
                {
                case ClickableControl::COMBO_PRESET_CLICK:
                    pState->isPresetDropdownOpen = !pState->isPresetDropdownOpen;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_PRESET_NEW:
                    PoolManager::CreateNewPreset(L"New Preset");
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_PRESET_RENAME:
                {
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset)
                    {
                        std::wstring newName;
                        if (ShowModernInputPrompt(hWnd, L"Rename Preset", L"Enter new preset name:", pPreset->presetName, newName))
                        {
                            if (!newName.empty())
                            {
                                PoolManager::RenameActivePreset(newName);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_PRESET_CLONE:
                    PoolManager::CloneActivePreset();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_PRESET_DELETE:
                    PoolManager::DeleteActivePreset();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_POOLS_EXPAND_ALL:
                {
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset)
                    {
                        for (auto& pool : pPreset->pools) pool.isCollapsed = false;
                        PoolManager::PersistPoolPresets();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOLS_COLLAPSE_ALL:
                {
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset)
                    {
                        for (auto& pool : pPreset->pools) pool.isCollapsed = true;
                        PoolManager::PersistPoolPresets();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_ADD_POOL:
                    PoolManager::AddPoolToActivePreset(L"New Pool");
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_POOL_COLLAPSE_TOGGLE:
                {
                    int poolIdx = ctrl.poolIdx;
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset && poolIdx >= 0 && poolIdx < (int)pPreset->pools.size())
                    {
                        pPreset->pools[poolIdx].isCollapsed = !pPreset->pools[poolIdx].isCollapsed;
                        PoolManager::PersistPoolPresets();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_RENAME:
                {
                    int poolIdx = ctrl.poolIdx;
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset && poolIdx >= 0 && poolIdx < (int)pPreset->pools.size())
                    {
                        std::wstring newPoolName;
                        if (ShowModernInputPrompt(hWnd, L"Rename Pool", L"Enter new pool name:", pPreset->pools[poolIdx].name, newPoolName))
                        {
                            if (!newPoolName.empty())
                            {
                                PoolManager::RenamePool(poolIdx, newPoolName);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_CLONE:
                {
                    int newIdx = PoolManager::ClonePoolInActivePreset(ctrl.poolIdx);
                    if (newIdx >= 0)
                    {
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_UP:
                    if (PoolManager::MovePool(ctrl.poolIdx, ctrl.poolIdx - 1))
                    {
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;

                case ClickableControl::BTN_POOL_DOWN:
                    if (PoolManager::MovePool(ctrl.poolIdx, ctrl.poolIdx + 1))
                    {
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;

                case ClickableControl::BTN_POOL_DELETE:
                    PoolManager::RemovePoolFromActivePreset(ctrl.poolIdx);
                    pState->selectedUnitIndices.clear();
                    pState->selectedPoolIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_POOL_MODE_TOGGLE:
                {
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset && ctrl.poolIdx >= 0 && ctrl.poolIdx < (int)pPreset->pools.size())
                    {
                        auto& pool = pPreset->pools[ctrl.poolIdx];
                        pool.pickMode = (pool.pickMode == PoolManager::PoolPickMode::Random) ?
                                         PoolManager::PoolPickMode::Sequential :
                                         PoolManager::PoolPickMode::Random;
                        PoolManager::PersistPoolPresets();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_MIN_EDIT:
                {
                    int poolIdx = ctrl.poolIdx;
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset && poolIdx >= 0 && poolIdx < (int)pPreset->pools.size())
                    {
                        std::wstring valStr = std::to_wstring(pPreset->pools[poolIdx].minCount);
                        std::wstring outStr;
                        if (ShowModernInputPrompt(hWnd, L"Set Minimum Units", L"Enter minimum unit count for this pool:", valStr, outStr))
                        {
                            try {
                                int val = std::stoi(outStr);
                                if (val < 0) val = 0;
                                pPreset->pools[poolIdx].minCount = val;
                                if (pPreset->pools[poolIdx].maxCount < val)
                                    pPreset->pools[poolIdx].maxCount = val;
                                PoolManager::PersistPoolPresets();
                                InvalidateRect(hWnd, NULL, FALSE);
                            } catch (...) {}
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_MAX_EDIT:
                {
                    int poolIdx = ctrl.poolIdx;
                    PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                    if (pPreset && poolIdx >= 0 && poolIdx < (int)pPreset->pools.size())
                    {
                        std::wstring valStr = std::to_wstring(pPreset->pools[poolIdx].maxCount);
                        std::wstring outStr;
                        if (ShowModernInputPrompt(hWnd, L"Set Maximum Units", L"Enter maximum unit count for this pool:", valStr, outStr))
                        {
                            try {
                                int val = std::stoi(outStr);
                                if (val < 1) val = 1;
                                pPreset->pools[poolIdx].maxCount = val;
                                if (pPreset->pools[poolIdx].minCount > val)
                                    pPreset->pools[poolIdx].minCount = val;
                                PoolManager::PersistPoolPresets();
                                InvalidateRect(hWnd, NULL, FALSE);
                            } catch (...) {}
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_FLIP_TOGGLE:
                {
                    PoolManager::CyclePoolFlipPolicy(ctrl.poolIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_UNIT_FLIP_TOGGLE:
                {
                    PoolManager::CycleUnitFlipMode(ctrl.poolIdx, ctrl.unitIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_POOL_COPY_UNITS:
                {
                    if (PoolManager::CopyPoolUnitsToClipboard(ctrl.poolIdx))
                    {
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_COPY_SELECTED:
                {
                    if (pState->selectedPoolIdx == ctrl.poolIdx && !pState->selectedUnitIndices.empty())
                    {
                        std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                        PoolManager::CopyMultipleUnitsToClipboard(ctrl.poolIdx, indices);
                    }
                    else
                    {
                        PoolManager::CopyPoolUnitsToClipboard(ctrl.poolIdx);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_POOL_FLIP_SELECTED:
                {
                    if (pState->selectedPoolIdx == ctrl.poolIdx && !pState->selectedUnitIndices.empty())
                    {
                        PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                        if (pPreset && ctrl.poolIdx >= 0 && ctrl.poolIdx < (int)pPreset->pools.size())
                        {
                            const auto& pool = pPreset->pools[ctrl.poolIdx];
                            int firstIdx = *pState->selectedUnitIndices.begin();
                            PoolManager::UnitFlipMode curMode = (firstIdx >= 0 && firstIdx < (int)pool.units.size()) ? pool.units[firstIdx].flipMode : PoolManager::UnitFlipMode::Auto;
                            PoolManager::UnitFlipMode nextMode = PoolManager::UnitFlipMode::Auto;
                            switch (curMode)
                            {
                            case PoolManager::UnitFlipMode::Auto:    nextMode = PoolManager::UnitFlipMode::Forward; break;
                            case PoolManager::UnitFlipMode::Forward: nextMode = PoolManager::UnitFlipMode::Flipped; break;
                            case PoolManager::UnitFlipMode::Flipped: nextMode = PoolManager::UnitFlipMode::Random;  break;
                            case PoolManager::UnitFlipMode::Random:  nextMode = PoolManager::UnitFlipMode::Auto;    break;
                            }
                            std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                            PoolManager::SetMultipleUnitsFlipMode(ctrl.poolIdx, indices, nextMode);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_DELETE_SELECTED:
                {
                    if (pState->selectedPoolIdx == ctrl.poolIdx && !pState->selectedUnitIndices.empty())
                    {
                        std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                        PoolManager::RemoveMultipleUnitsFromPool(ctrl.poolIdx, indices);
                        pState->selectedUnitIndices.clear();
                        pState->anchorUnitIdx = -1;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_UNIT_COPY:
                {
                    if (PoolManager::CopySingleUnitToClipboard(ctrl.poolIdx, ctrl.unitIdx))
                    {
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_PASTE_CLIPBOARD:
                {
                    const auto& clipUnits = GetAppClipboardUnits();
                    if (!clipUnits.empty())
                    {
                        PoolManager::PasteUnitsToPool(ctrl.poolIdx, clipUnits);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_CLEAR_UNITS:
                    PoolManager::ClearPoolUnits(ctrl.poolIdx);
                    pState->selectedUnitIndices.clear();
                    pState->anchorUnitIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_UNIT_REMOVE:
                    PoolManager::RemoveUnitFromPool(ctrl.poolIdx, ctrl.unitIdx);
                    pState->selectedUnitIndices.erase(ctrl.unitIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_CLOSE:
                    if (pState && pState->hParent && IsWindow(pState->hParent))
                    {
                        SetForegroundWindow(pState->hParent);
                        SetActiveWindow(pState->hParent);
                    }
                    DestroyWindow(hWnd);
                    break;

                case ClickableControl::BTN_GENERATE:
                {
                    PoolManager::PoolPreset* pCurPreset = PoolManager::GetActivePreset();
                    if (!pCurPreset || pCurPreset->pools.empty())
                    {
                        ShowModernMessageBox(hWnd, L"Active preset contains no pools. Please add at least one pool before generating consists.", L"Batch Consist Wizard", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    bool hasAnyUnits = false;
                    for (const auto& pl : pCurPreset->pools)
                    {
                        if (!pl.units.empty()) { hasAnyUnits = true; break; }
                    }

                    if (!hasAnyUnits)
                    {
                        ShowModernMessageBox(hWnd, L"All pools in the current preset are empty. Please add locomotives or wagons to the pools before generating consists.", L"Batch Consist Wizard", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    ShowBatchConsistGeneratorDialog(hWnd, PoolManager::g_ActivePresetIndex);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                default:
                    break;
                }
                return 0;
            }
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        for (const auto& chip : pState->unitChipHits)
        {
            if (PtInRect(&chip.rcChip, pt))
            {
                if (pState->selectedPoolIdx != chip.poolIdx || pState->selectedUnitIndices.count(chip.unitIdx) == 0)
                {
                    pState->selectedPoolIdx = chip.poolIdx;
                    pState->selectedUnitIndices.clear();
                    pState->selectedUnitIndices.insert(chip.unitIdx);
                    pState->anchorUnitIdx = chip.unitIdx;
                }

                std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                PoolManager::CopyMultipleUnitsToClipboard(chip.poolIdx, indices);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_CLOSE:
    {
        if (pState && pState->hParent && IsWindow(pState->hParent))
        {
            SetForegroundWindow(pState->hParent);
            SetActiveWindow(pState->hParent);
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            if (wParam == 'A' || wParam == 'a')
            {
                PoolManager::PoolPreset* pPreset = PoolManager::GetActivePreset();
                if (pPreset && !pPreset->pools.empty())
                {
                    int targetPool = (pState->selectedPoolIdx >= 0 && pState->selectedPoolIdx < (int)pPreset->pools.size()) ? pState->selectedPoolIdx : 0;
                    pState->selectedPoolIdx = targetPool;
                    pState->selectedUnitIndices.clear();
                    for (size_t u = 0; u < pPreset->pools[targetPool].units.size(); ++u)
                    {
                        pState->selectedUnitIndices.insert((int)u);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            else if (wParam == 'C' || wParam == 'c')
            {
                if (pState->selectedPoolIdx >= 0 && !pState->selectedUnitIndices.empty())
                {
                    std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                    PoolManager::CopyMultipleUnitsToClipboard(pState->selectedPoolIdx, indices);
                }
                else if (pState->selectedPoolIdx >= 0)
                {
                    PoolManager::CopyPoolUnitsToClipboard(pState->selectedPoolIdx);
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (wParam == 'V' || wParam == 'v')
            {
                const auto& clipUnits = GetAppClipboardUnits();
                if (!clipUnits.empty())
                {
                    int targetPool = (pState->selectedPoolIdx >= 0) ? pState->selectedPoolIdx : 0;
                    PoolManager::PasteUnitsToPool(targetPool, clipUnits);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
        }
        else if (wParam == VK_DELETE || wParam == VK_BACK)
        {
            if (pState->selectedPoolIdx >= 0 && !pState->selectedUnitIndices.empty())
            {
                std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                PoolManager::RemoveMultipleUnitsFromPool(pState->selectedPoolIdx, indices);
                pState->selectedUnitIndices.clear();
                pState->anchorUnitIdx = -1;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        else if (wParam == VK_ESCAPE)
        {
            if (!pState->selectedUnitIndices.empty())
            {
                pState->selectedUnitIndices.clear();
                pState->anchorUnitIdx = -1;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (pState && pState->hParent && IsWindow(pState->hParent))
            {
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hParent && IsWindow(pState->hParent))
            {
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            KillTimer(hWnd, TIMER_REPEAT_ID);
            if (pState->hFontTitle)     DeleteObject(pState->hFontTitle);
            if (pState->hFontSub)       DeleteObject(pState->hFontSub);
            if (pState->hFontMain)      DeleteObject(pState->hFontMain);
            if (pState->hFontMainBold)  DeleteObject(pState->hFontMainBold);
            if (pState->hFontSmall)     DeleteObject(pState->hFontSmall);
            if (pState->hFontBadge)     DeleteObject(pState->hFontBadge);
            if (pState->hFontIcon)      DeleteObject(pState->hFontIcon);
            if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);
            delete pState;
        }
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        g_hPoolManagerDlg = NULL;
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// -------------------------------------------------------------
// Public Exported Functions
// -------------------------------------------------------------

// ============================================================================

void ShowPoolManagerDialog(HWND hWndParent)
{
    if (g_hPoolManagerDlg && IsWindow(g_hPoolManagerDlg))
    {
        ShowWindow(g_hPoolManagerDlg, SW_RESTORE);
        SetForegroundWindow(g_hPoolManagerDlg);
        BringWindowToTop(g_hPoolManagerDlg);
        return;
    }

    PoolManager::InitializePoolPresets();

    static bool s_WizardRegistered = false;
    const wchar_t* szClassName = L"BatchConsistGenerationWizardWindow";

    if (!s_WizardRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WizardDlgProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(PoolTheme::GutterBackground);
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_WizardRegistered = true;
    }

    WizardDlgState* pState = new WizardDlgState();
    pState->hParent = hWndParent;

    int dlgW = 980;
    int dlgH = 700;

    RECT rcParent;
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_APPWINDOW, szClassName, L"Consist Pool Manager",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME,
        x, y, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandleW(NULL), pState
    );

    if (!hDlg)
    {
        delete pState;
        return;
    }

    g_hPoolManagerDlg = hDlg;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = PoolTheme::BorderLine;
    DwmSetWindowAttribute(hDlg, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hDlg, &margins);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
}

bool PoolManager_IsActive()
{
    return (g_hPoolManagerDlg && IsWindow(g_hPoolManagerDlg) && IsWindowVisible(g_hPoolManagerDlg));
}

bool PoolManager_HandleDragHover(POINT ptScreen)
{
    if (!PoolManager_IsActive())
        return false;

    WizardDlgState* pState = (WizardDlgState*)GetWindowLongPtrW(g_hPoolManagerDlg, GWLP_USERDATA);
    if (!pState) return false;

    POINT ptClient = ptScreen;
    ScreenToClient(g_hPoolManagerDlg, &ptClient);

    RECT rcClient;
    GetClientRect(g_hPoolManagerDlg, &rcClient);
    int w = rcClient.right;
    int h = rcClient.bottom;

    int toolbarY = 43;
    int toolbarH = 46;
    int contentY = toolbarY + toolbarH + 1;
    int footerH = 56;
    int contentH = h - footerH - contentY;
    RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

    if (!PtInRect(&rcContentClip, ptClient))
    {
        if (pState->dragOverPoolIdx != -1)
        {
            pState->dragOverPoolIdx = -1;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        }
        return false;
    }

    // Auto-scroll when dragging near top or bottom edges of content area
    int autoDelta = 0;
    if (pState->m_vScroll.CheckAutoScroll(ptClient, 32, 16, autoDelta))
    {
        pState->m_vScroll.SetPos(pState->m_vScroll.GetPos() + autoDelta);
        pState->scrollY = pState->m_vScroll.GetPos();
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
    }

    PoolManager::PoolPreset* pActivePreset = PoolManager::GetActivePreset();
    if (!pActivePreset || pActivePreset->pools.empty())
    {
        if (pState->dragOverPoolIdx != -1)
        {
            pState->dragOverPoolIdx = -1;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        }
        return false;
    }

    bool needV = (pState->maxScrollY > 0);
    int cardY = contentY - pState->scrollY + 8;
    int cardMargin = 20;
    int cardW = w - (cardMargin * 2);

    int hitPool = -1;
    std::wstring hitPoolName = L"";

    for (size_t p = 0; p < pActivePreset->pools.size(); ++p)
    {
        const auto& pool = pActivePreset->pools[p];
        int unitCount = (int)pool.units.size();
        int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
        int cardH = pool.isCollapsed ? 33 : (92 + unitsAreaH + 40);
        RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

        if (PtInRect(&rcCard, ptClient))
        {
            hitPool = (int)p;
            hitPoolName = pool.name;
            break;
        }
        cardY += cardH + 14;
    }

    if (hitPool != pState->dragOverPoolIdx)
    {
        pState->dragOverPoolIdx = hitPool;
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
    }

    if (hitPool != -1)
    {
        FluentDragGhost::Move(ptScreen, true, L"Add to " + hitPoolName);
        return true;
    }

    return false;
}

bool PoolManager_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units)
{
    if (!PoolManager_IsActive())
        return false;

    WizardDlgState* pState = (WizardDlgState*)GetWindowLongPtrW(g_hPoolManagerDlg, GWLP_USERDATA);
    if (!pState) return false;

    POINT ptClient = ptScreen;
    ScreenToClient(g_hPoolManagerDlg, &ptClient);

    RECT rcClient;
    GetClientRect(g_hPoolManagerDlg, &rcClient);
    int w = rcClient.right;
    int h = rcClient.bottom;

    int toolbarY = 43;
    int toolbarH = 46;
    int contentY = toolbarY + toolbarH + 1;
    int footerH = 56;
    int contentH = h - footerH - contentY;
    RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

    if (!PtInRect(&rcContentClip, ptClient))
    {
        pState->dragOverPoolIdx = -1;
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        return false;
    }

    PoolManager::PoolPreset* pActivePreset = PoolManager::GetActivePreset();
    if (!pActivePreset || pActivePreset->pools.empty())
    {
        pState->dragOverPoolIdx = -1;
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        return false;
    }

    bool needV = (pState->maxScrollY > 0);
    int cardY = contentY - pState->scrollY + 8;
    int cardMargin = 20;
    int cardW = w - (cardMargin * 2);

    int hitPool = -1;
    for (size_t p = 0; p < pActivePreset->pools.size(); ++p)
    {
        const auto& pool = pActivePreset->pools[p];
        int unitCount = (int)pool.units.size();
        int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
        int cardH = pool.isCollapsed ? 33 : (92 + unitsAreaH + 40);
        RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

        if (PtInRect(&rcCard, ptClient))
        {
            hitPool = (int)p;
            break;
        }
        cardY += cardH + 14;
    }

    pState->dragOverPoolIdx = -1;
    if (hitPool >= 0 && hitPool < (int)pActivePreset->pools.size())
    {
        for (const auto& u : units)
        {
            PoolManager::PoolUnit pu;
            pu.szFileName = u.uid;
            pu.szFolder = u.parentDir;
            pu.isEngine = u.isEngine;
            pu.flipMode = u.isFlipped ? PoolManager::UnitFlipMode::Flipped : PoolManager::UnitFlipMode::Auto;
            PoolManager::AddUnitToPool(hitPool, pu);
        }
        PoolManager::PersistPoolPresets();
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        return true;
    }

    InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
    return false;
}
