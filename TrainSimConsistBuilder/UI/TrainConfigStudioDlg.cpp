#include "TrainConfigStudioDlg.h"
#include "PoolManagerDlg.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "CustomScrollBar.h"
#include "CustomTitleBar.h"
#include "CustomDropDownMenu.h"
#include "CustomListControl.h"
#include "FluentDragGhost.h"
#include "ModernContextMenu.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include "../SRC/TrainConfig.h"
#include "../SRC/PoolManager.h"
#include "../SRC/AssetsParser.h"
#include "../SRC/ConsistWriter.h"
#include "../SRC/DatabaseManager.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")

namespace fs = std::filesystem;

extern HFONT GetAdaptiveSystemFont();
extern HWND g_hAssetList;
extern std::vector<size_t> g_FilteredStockIndices;
extern std::vector<StockItem> g_StockCache;
extern CRITICAL_SECTION g_StockCacheCS;
extern std::wstring g_szBasePath;

static HWND g_hTrainConfigStudioDlg = NULL;

namespace StudioTheme
{
    constexpr COLORREF Background        = RGB(18, 18, 18);
    constexpr COLORREF SidebarBg         = RGB(24, 24, 24);
    constexpr COLORREF CardBg            = RGB(28, 28, 28);
    constexpr COLORREF CardHeaderBg      = RGB(34, 34, 34);
    constexpr COLORREF CardBorder        = RGB(46, 46, 46);
    constexpr COLORREF CardBorderHover   = RGB(70, 70, 70);
    constexpr COLORREF InputBg           = RGB(30, 30, 30);
    constexpr COLORREF InputBorder       = RGB(55, 55, 55);
    constexpr COLORREF InputBorderFocus  = RGB(0, 120, 215);
    constexpr COLORREF TextPrimary       = RGB(240, 240, 240);
    constexpr COLORREF TextSecondary     = RGB(160, 160, 160);
    constexpr COLORREF TextMuted         = RGB(110, 110, 110);
    constexpr COLORREF AccentBlue        = RGB(0, 120, 215);
    constexpr COLORREF AccentHover       = RGB(20, 140, 235);
    constexpr COLORREF AccentPressed     = RGB(0, 100, 185);
    constexpr COLORREF TagBg             = RGB(38, 38, 38);
    constexpr COLORREF TagBorder         = RGB(60, 60, 60);
}

// ------------------------------------------------------------------------------------------------
// Sleek Modern Input Prompt Dialog (Custom Polished Dark Modal - matching PoolManagerDlg)
// ------------------------------------------------------------------------------------------------

struct StudioPromptState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hEdit = NULL;
    std::wstring title;
    std::wstring prompt;
    std::wstring resultText;
    bool isConfirmed = false;
    bool isNumericOnly = false;
    bool allowDecimal = false;
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

static HFONT StudioCreateFont(int pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static LRESULT CALLBACK StudioPromptEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    StudioPromptState* pState = (StudioPromptState*)dwRefData;
    if (uMsg == WM_CHAR && pState && pState->isNumericOnly)
    {
        wchar_t ch = (wchar_t)wParam;
        if (ch < 32)
        {
            // allow control keys (backspace, enter, escape, ctrl combinations)
        }
        else if (ch >= L'0' && ch <= L'9')
        {
            // allow digit
        }
        else if (ch == L'.' && pState->allowDecimal)
        {
            wchar_t szBuf[128] = { 0 };
            GetWindowTextW(hWnd, szBuf, 128);
            DWORD dwSel = (DWORD)SendMessageW(hWnd, EM_GETSEL, 0, 0);
            int selStart = LOWORD(dwSel);
            int selEnd = HIWORD(dwSel);

            wchar_t* pDot = wcschr(szBuf, L'.');
            if (pDot)
            {
                int dotPos = (int)(pDot - szBuf);
                if (!(selStart <= dotPos && selEnd > dotPos))
                {
                    return 0; // extra decimal point rejected
                }
            }
        }
        else
        {
            return 0; // letters, spaces and symbols rejected
        }
    }
    else if (uMsg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        HWND hParent = GetParent(hWnd);
        if (hParent && pState)
        {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(hWnd, buf, 256);
            pState->resultText = buf;
            pState->isConfirmed = true;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hParent);
            return 0;
        }
    }
    else if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        HWND hParent = GetParent(hWnd);
        if (hParent && pState)
        {
            pState->isConfirmed = false;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hParent);
            return 0;
        }
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, StudioPromptEditSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void StudioDrawPromptButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, HFONT hFont)
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
    SelectObject(hdc, hFont);
    RECT rcText = rc;
    DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static LRESULT CALLBACK StudioPromptWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    StudioPromptState* pState = (StudioPromptState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (StudioPromptState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return TRUE;
    }

    case WM_CREATE:
    {
        pState->hFontTitle  = StudioCreateFont(12, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontPrompt = StudioCreateFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMain   = StudioCreateFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontBold   = StudioCreateFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon   = StudioCreateFont(10, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = StudioCreateFont(10, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->hEdit = CreateWindowExW(
            0, L"EDIT", pState->resultText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            34, 82, 388, 20, hWnd, (HMENU)101, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEdit, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        SendMessageW(pState->hEdit, EM_SETSEL, 0, -1);
        if (pState->hEdit)
        {
            SetWindowSubclass(pState->hEdit, StudioPromptEditSubclassProc, 1, (DWORD_PTR)pState);
        }
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

        StudioDrawPromptButton(hdcMem, pState->rcCancelBtn, L"Cancel", pState->isHoverCancel, false, false, pState->hFontMain);
        StudioDrawPromptButton(hdcMem, pState->rcOkBtn, L"OK", pState->isHoverOk, false, true, pState->hFontBold);

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
        return 0;
    }

    case WM_CLOSE:
    {
        pState->isConfirmed = false;
        if (pState->hParent && IsWindow(pState->hParent))
        {
            EnableWindow(pState->hParent, TRUE);
            SetForegroundWindow(pState->hParent);
            SetActiveWindow(pState->hParent);
        }
        DestroyWindow(hWnd);
        return 0;
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
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool ShowStudioPrompt(HWND hParent, const std::wstring& title, const std::wstring& prompt, const std::wstring& initialVal, std::wstring& outVal, bool isNumericOnly = false, bool allowDecimal = false)
{
    static bool s_PromptRegistered = false;
    const wchar_t* szClassName = L"TrainConfigStudioPromptModalClass";
    if (!s_PromptRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = StudioPromptWndProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(RGB(20, 20, 20));
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_PromptRegistered = true;
    }

    StudioPromptState state;
    state.hParent = hParent;
    state.title = title;
    state.prompt = prompt;
    state.resultText = initialVal;
    state.isNumericOnly = isNumericOnly;
    state.allowDecimal = allowDecimal;

    int dlgW = 460;
    int dlgH = 180;
    RECT rcParent;
    if (hParent && IsWindow(hParent)) GetWindowRect(hParent, &rcParent);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);

    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hPrompt = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        szClassName, title.c_str(),
        WS_POPUP | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hParent, NULL, GetModuleHandleW(NULL), &state
    );

    if (!hPrompt) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hPrompt, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hPrompt, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = RGB(50, 50, 50);
    DwmSetWindowAttribute(hPrompt, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, FALSE);
    }

    MSG msg;
    while (IsWindow(hPrompt) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_RETURN)
            {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(state.hEdit, buf, 256);
                state.resultText = buf;
                state.isConfirmed = true;
                DestroyWindow(hPrompt);
                break;
            }
            else if (msg.wParam == VK_ESCAPE)
            {
                DestroyWindow(hPrompt);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        SetActiveWindow(hParent);
    }

    if (state.isConfirmed)
    {
        outVal = state.resultText;
        return true;
    }
    return false;
}

// Studio Dialog State
// ------------------------------------------------------------------------------------------------

struct ClickControl
{
    enum Type
    {
        NONE = 0,
        TAB_0,
        TAB_1,
        BTN_NEW_BLUEPRINT,
        BTN_NEW_CATEGORY,
        BTN_DELETE_CATEGORY,
        BTN_CLONE_BLUEPRINT,
        BTN_DELETE_BLUEPRINT,
        BTN_OPEN_FOLDER,
        BTN_RESCAN,
        BTN_CAT_COLLAPSE_TOGGLE,
        BTN_CAT_HEADER,
        BTN_CAT_RENAME,
        BTN_CAT_CLONE,
        BTN_CAT_UP,
        BTN_CAT_DOWN,
        BTN_CAT_DELETE,
        CATEGORY_HEADER_CLICK,
        CONFIG_ITEM_CLICK,
        BTN_ADD_POOL_TO_RAKE,
        BTN_SAVE_BLUEPRINT,
        BTN_SWITCH_TO_BINDINGS,
        BTN_POOL_COLLAPSE_TOGGLE,
        BTN_POOL_ROLE_PICK,
        BTN_POOL_RENAME,
        BTN_POOL_CLONE,
        BTN_POOL_UP,
        BTN_POOL_DOWN,
        BTN_POOL_DELETE,
        BTN_POOL_MIN_DEC,
        BTN_POOL_MIN_INC,
        BTN_POOL_MIN_EDIT,
        BTN_POOL_MAX_DEC,
        BTN_POOL_MAX_INC,
        BTN_POOL_MAX_EDIT,
        BTN_POOL_MODE_TOGGLE,
        BTN_POOL_FLIP_TOGGLE,
        BTN_POOL_EDIT,
        BTN_TOTAL_MIN_DEC,
        BTN_TOTAL_MIN_INC,
        BTN_TOTAL_MIN_EDIT,
        BTN_TOTAL_MAX_DEC,
        BTN_TOTAL_MAX_INC,
        BTN_TOTAL_MAX_EDIT,
        BTN_EDIT_NAME,
        BTN_EDIT_ID,
        BTN_EDIT_TYPE,
        BTN_EDIT_CATEGORY,
        BTN_EDIT_MAXSPEED,
        BTN_EDIT_PERFFACTOR,
        BTN_EDIT_DESC,
        BTN_BIND_SMART_MATCH,
        BTN_BIND_SAVE,
        BTN_BIND_OPEN_FOLDER,
        BTN_BIND_POOL_COLLAPSE_TOGGLE,
        BTN_BIND_POOL_MODE_TOGGLE,
        BTN_BIND_POOL_FLIP_TOGGLE,
        BTN_BIND_POOL_COPY_UNITS,
        BTN_BIND_POOL_PASTE_UNITS,
        BTN_BIND_POOL_CLEAR_UNITS,
        BTN_BIND_TEST_GENERATE,
        BTN_UNIT_REMOVE,
        BTN_UNIT_FLIP_TOGGLE
    } type = NONE;

    RECT rc = { 0 };
    int index = -1;
    int subIndex = -1;
    std::wstring tag;
};

struct StudioDlgState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hTitleBar = NULL;
    int activeTab = 0; // 0 = Train Blueprints, 1 = Rolling Stock Bindings

    // Selected Blueprint & Categories
    int selectedConfigIdx = -1;
    std::wstring selectedCategory = L"";
    std::vector<std::wstring> categoryOrder;
    std::unordered_set<std::wstring> collapsedCategories;

    // Category Card Drag & Hits
    struct CategoryCardHit {
        int catIdx = -1;
        std::wstring category;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
    };
    std::vector<CategoryCardHit> categoryCardHits;

    bool isPotentialCatDrag = false;
    bool isCatDragging = false;
    int potentialCatDragIdx = -1;
    int draggingCatIdx = -1;
    int catDropTargetIdx = -1;
    RECT rcCatDropIndicator = { 0 };
    POINT ptCatDragStart = { 0, 0 };

    // Blueprint Pool Cards Drag & Collapse
    struct BlueprintPoolCardHit {
        int poolIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
    };
    std::vector<BlueprintPoolCardHit> bpPoolCardHits;
    std::unordered_set<int> collapsedBlueprintPools;
    std::unordered_set<int> collapsedBindingPools;

    bool isPotentialBpPoolDrag = false;
    bool isBpPoolDragging = false;
    int potentialBpPoolDragIdx = -1;
    int draggingBpPoolIdx = -1;
    int bpPoolDropTargetIdx = -1;
    RECT rcBpPoolDropIndicator = { 0 };
    POINT ptBpPoolDragStart = { 0, 0 };

    // Rolling Stock Bindings (Tab 1) Hits & Unit Drag State
    struct BindingPoolCardHit {
        int poolIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
    };
    std::vector<BindingPoolCardHit> bindingPoolCardHits;

    struct BindingUnitChipHit {
        int poolIdx = -1;
        int unitIdx = -1;
        RECT rcChip = { 0 };
    };
    std::vector<BindingUnitChipHit> bindingUnitChipHits;

    struct BindingUnitsBoxHit {
        int poolIdx = -1;
        RECT rcUnitsBox = { 0 };
    };
    std::vector<BindingUnitsBoxHit> bindingUnitsBoxHits;

    bool isPotentialUnitDrag = false;
    bool isUnitDragging = false;
    POINT ptUnitDragStart = { 0, 0 };
    int unitDragSourcePool = -1;
    int unitDragSourceUnitIdx = -1;
    int unitDropTargetPool = -1;
    int unitDropTargetUnitIdx = -1;
    RECT rcUnitDropIndicator = { 0 };

    TrainConfigManager::TrainConfig activeConfig;
    TrainConfigManager::TrainBinding activeBinding;
    bool isDirtyBlueprint = false;
    bool isDirtyBindings = false;

    // Scroll States
    int sidebarScrollY = 0;
    int editorScrollY = 0;
    int bindingsScrollY = 0;
    int totalSidebarH = 0;
    int totalEditorH = 0;
    int totalBindingsH = 0;

    CustomScrollBar m_sidebarScroll;
    CustomScrollBar m_editorScroll;
    CustomScrollBar m_bindingsScroll;

    // Fonts
    HFONT hFontTitle = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontBadge = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    // Interactive Controls
    std::vector<ClickControl> clickControls;
    int hoveredControlIdx = -1;
    int pressedControlIdx = -1;

    // Auto-repeat on button hold (parity with PoolManagerDlg)
    ClickControl::Type repeatAction = ClickControl::NONE;
    int repeatPoolIdx = -1;
    int repeatHoldCount = 0;

    // Splitter state for resizable left sidebar
    int sidebarWidth = 380;
    bool isDraggingSplitter = false;
    int dragSplitterStartX = 0;
    int dragSidebarStartW = 380;
    RECT rcSplitter = { 0 };

    // Drag-Drop state into Tab 1
    int dragHoverPoolIdx = -1;

    void RefreshCategoryOrder()
    {
        auto discovered = TrainConfigManager::GetCategories();
        std::vector<std::wstring> newOrder;
        for (const auto& c : categoryOrder)
        {
            if (std::find(discovered.begin(), discovered.end(), c) != discovered.end())
            {
                newOrder.push_back(c);
            }
        }
        for (const auto& c : discovered)
        {
            if (std::find(newOrder.begin(), newOrder.end(), c) == newOrder.end())
            {
                newOrder.push_back(c);
            }
        }
        categoryOrder = newOrder;
        if (categoryOrder.empty() && !discovered.empty())
        {
            categoryOrder = discovered;
        }
        if (!categoryOrder.empty())
        {
            if (selectedCategory.empty() || std::find(categoryOrder.begin(), categoryOrder.end(), selectedCategory) == categoryOrder.end())
            {
                selectedCategory = categoryOrder[0];
            }
        }
        else
        {
            selectedCategory = L"";
        }
    }

    void SwitchToTab(int tabIndex)
    {
        activeTab = tabIndex;
        if (hTitleBar && IsWindow(hTitleBar))
        {
            CustomTitleBar_SetActiveTab(hTitleBar, tabIndex);
            InvalidateRect(hTitleBar, NULL, TRUE);
            UpdateWindow(hTitleBar);
        }

        // Reset editor scroll positions on tab change (preserve sidebar scroll)
        m_editorScroll.SetPos(0);
        m_bindingsScroll.SetPos(0);
        editorScrollY = 0;
        bindingsScrollY = 0;

        if (activeTab == 1)
        {
            TrainConfigManager::LoadTrainBinding(activeConfig, activeBinding);
            if (activeBinding.pools.size() < activeConfig.pools.size())
            {
                activeBinding.pools.resize(activeConfig.pools.size());
            }
            for (size_t p = 0; p < activeConfig.pools.size(); ++p)
            {
                activeBinding.pools[p].poolName = activeConfig.pools[p].poolName;
            }
        }

        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
    }

    void PopulateFieldsFromActiveConfig()
    {
        TrainConfigManager::LoadTrainBinding(activeConfig, activeBinding);
        if (activeBinding.pools.size() < activeConfig.pools.size())
        {
            activeBinding.pools.resize(activeConfig.pools.size());
        }
        for (size_t p = 0; p < activeConfig.pools.size(); ++p)
        {
            activeBinding.pools[p].poolName = activeConfig.pools[p].poolName;
        }
    }
    void SyncActiveConfigFromFields() {}
    void ShowEditorFields(bool) {}
};

#define TIMER_STUDIO_REPEAT_ID 8899

static void ExecuteStudioStepAction(StudioDlgState* pState, ClickControl::Type type, int poolIdx, int step)
{
    if (!pState) return;

    if (type == ClickControl::BTN_TOTAL_MIN_DEC)
    {
        pState->activeConfig.minLength = (std::max)(0, pState->activeConfig.minLength - step);
        pState->isDirtyBlueprint = true;
    }
    else if (type == ClickControl::BTN_TOTAL_MIN_INC)
    {
        pState->activeConfig.minLength += step;
        if (pState->activeConfig.minLength > pState->activeConfig.maxLength)
            pState->activeConfig.maxLength = pState->activeConfig.minLength;
        pState->isDirtyBlueprint = true;
    }
    else if (type == ClickControl::BTN_TOTAL_MAX_DEC)
    {
        pState->activeConfig.maxLength = (std::max)(1, pState->activeConfig.maxLength - step);
        if (pState->activeConfig.minLength > pState->activeConfig.maxLength)
            pState->activeConfig.minLength = pState->activeConfig.maxLength;
        pState->isDirtyBlueprint = true;
    }
    else if (type == ClickControl::BTN_TOTAL_MAX_INC)
    {
        pState->activeConfig.maxLength += step;
        pState->isDirtyBlueprint = true;
    }
    else if (poolIdx >= 0 && poolIdx < (int)pState->activeConfig.pools.size())
    {
        auto& pool = pState->activeConfig.pools[poolIdx];
        if (type == ClickControl::BTN_POOL_MIN_DEC)
        {
            pool.minCount = (std::max)(0, pool.minCount - step);
            pState->isDirtyBlueprint = true;
        }
        else if (type == ClickControl::BTN_POOL_MIN_INC)
        {
            pool.minCount += step;
            if (pool.minCount > pool.maxCount)
                pool.maxCount = pool.minCount;
            pState->isDirtyBlueprint = true;
        }
        else if (type == ClickControl::BTN_POOL_MAX_DEC)
        {
            pool.maxCount = (std::max)(1, pool.maxCount - step);
            if (pool.minCount > pool.maxCount)
                pool.minCount = pool.maxCount;
            pState->isDirtyBlueprint = true;
        }
        else if (type == ClickControl::BTN_POOL_MAX_INC)
        {
            pool.maxCount += step;
            pState->isDirtyBlueprint = true;
        }
    }
}

static void DrawSolidYellowDropIndicator(HDC hdc, const RECT& rc)
{
    if (rc.right <= rc.left && rc.bottom <= rc.top) return;

    COLORREF yellowCol = RGB(255, 215, 0);
    HBRUSH hbrYellow = CreateSolidBrush(yellowCol);
    HPEN hpenYellow = CreatePen(PS_SOLID, 1, yellowCol);
    HGDIOBJ holdBr = SelectObject(hdc, hbrYellow);
    HGDIOBJ holdPen = SelectObject(hdc, hpenYellow);

    int midY = (rc.top + rc.bottom) / 2;
    int barH = 3;
    RECT rcBar = { rc.left, midY - barH / 2, rc.right, midY + barH / 2 + 1 };
    FillRect(hdc, &rcBar, hbrYellow);

    // Left triangle
    POINT ptLeft[3] = { { rc.left, midY - 5 }, { rc.left, midY + 5 }, { rc.left + 7, midY } };
    Polygon(hdc, ptLeft, 3);

    // Right triangle
    POINT ptRight[3] = { { rc.right, midY - 5 }, { rc.right, midY + 5 }, { rc.right - 7, midY } };
    Polygon(hdc, ptRight, 3);

    SelectObject(hdc, holdBr);
    SelectObject(hdc, holdPen);
    DeleteObject(hbrYellow);
    DeleteObject(hpenYellow);
}

static void DrawModernBtn(HDC hdc, const RECT& rc, const wchar_t* text, bool isHover, bool isPressed, bool isAccent, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* iconGlyph = NULL)
{
    COLORREF bgCol;
    COLORREF borderCol;
    COLORREF textCol;

    if (isAccent)
    {
        bgCol = isPressed ? StudioTheme::AccentPressed : (isHover ? StudioTheme::AccentHover : StudioTheme::AccentBlue);
        borderCol = bgCol;
        textCol = RGB(255, 255, 255);
    }
    else
    {
        bgCol = isPressed ? RGB(32, 32, 32) : (isHover ? RGB(46, 46, 46) : RGB(36, 36, 36));
        borderCol = isHover ? RGB(75, 75, 75) : StudioTheme::CardBorder;
        textCol = isHover ? RGB(255, 255, 255) : StudioTheme::TextPrimary;
    }

    HBRUSH hbr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
    HPEN holdPen = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, holdBr);
    SelectObject(hdc, holdPen);
    DeleteObject(hbr);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);

    if (iconGlyph && wcscmp(iconGlyph, L"__TRI_DOWN__") == 0)
    {
        int midX = (rc.left + rc.right) / 2;
        int midY = (rc.top + rc.bottom) / 2;
        POINT pts[3] = { { midX - 4, midY - 2 }, { midX + 4, midY - 2 }, { midX, midY + 3 } };
        HBRUSH hbrTri = CreateSolidBrush(textCol);
        HPEN hpenTri = CreatePen(PS_SOLID, 1, textCol);
        SelectObject(hdc, hbrTri);
        SelectObject(hdc, hpenTri);
        Polygon(hdc, pts, 3);
        DeleteObject(hbrTri);
        DeleteObject(hpenTri);
        return;
    }
    if (iconGlyph && wcscmp(iconGlyph, L"__TRI_UP__") == 0)
    {
        int midX = (rc.left + rc.right) / 2;
        int midY = (rc.top + rc.bottom) / 2;
        POINT pts[3] = { { midX - 4, midY + 2 }, { midX + 4, midY + 2 }, { midX, midY - 3 } };
        HBRUSH hbrTri = CreateSolidBrush(textCol);
        HPEN hpenTri = CreatePen(PS_SOLID, 1, textCol);
        SelectObject(hdc, hbrTri);
        SelectObject(hdc, hpenTri);
        Polygon(hdc, pts, 3);
        DeleteObject(hbrTri);
        DeleteObject(hpenTri);
        return;
    }

    if (iconGlyph && iconGlyph[0] != L'\0' && text && text[0] != L'\0')
    {
        SIZE szText = { 0 };
        SelectObject(hdc, hFont);
        GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &szText);

        SIZE szIcon = { 0 };
        SelectObject(hdc, hIconFont ? hIconFont : hFont);
        GetTextExtentPoint32W(hdc, iconGlyph, (int)wcslen(iconGlyph), &szIcon);

        int iconW = (szIcon.cx > 12) ? szIcon.cx : 14;
        int spacing = 4;
        int totalW = iconW + spacing + szText.cx;
        int btnW = rc.right - rc.left;
        int startX = rc.left + (btnW - totalW) / 2;
        if (startX < rc.left + 4) startX = rc.left + 4;

        RECT rcIcon = { startX, rc.top, startX + iconW, rc.bottom };
        SelectObject(hdc, hIconFont ? hIconFont : hFont);
        SetTextColor(hdc, textCol);
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rcText = { startX + iconW + spacing, rc.top, rc.right - 2, rc.bottom };
        SelectObject(hdc, hFont);
        DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else if (iconGlyph && iconGlyph[0] != L'\0')
    {
        RECT rcIcon = rc;
        SelectObject(hdc, hIconFont ? hIconFont : hFont);
        SetTextColor(hdc, textCol);
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        RECT rcText = rc;
        SelectObject(hdc, hFont);
        SetTextColor(hdc, textCol);
        DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static TrainConfigManager::TrainBindingPool* GetBindingPoolByIndex(TrainConfigManager::TrainBinding& binding, size_t poolIdx, const std::wstring& poolName = L"")
{
    if (poolIdx >= binding.pools.size())
    {
        binding.pools.resize(poolIdx + 1);
    }
    if (!poolName.empty())
    {
        binding.pools[poolIdx].poolName = poolName;
    }
    return &binding.pools[poolIdx];
}

static int AutoPopulateBlueprintBindings(const TrainConfigManager::TrainConfig& cfg, TrainConfigManager::TrainBinding& outBinding)
{
    EnterCriticalSection(&g_StockCacheCS);
    if (g_StockCache.empty())
    {
        LeaveCriticalSection(&g_StockCacheCS);
        return 0;
    }

    int totalAdded = 0;
    for (size_t p = 0; p < cfg.pools.size(); ++p)
    {
        const auto& pool = cfg.pools[p];
        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(outBinding, p, pool.poolName);
        if (!pBP) continue;

        std::vector<std::wstring> tags;
        std::wstringstream ss(pool.filterTag);
        std::wstring item;
        while (std::getline(ss, item, L','))
        {
            size_t first = item.find_first_not_of(L" \t\r\n");
            size_t last = item.find_last_not_of(L" \t\r\n");
            if (first != std::wstring::npos && last != std::wstring::npos)
            {
                tags.push_back(item.substr(first, (last - first + 1)));
            }
        }

        for (const auto& stock : g_StockCache)
        {
            if (pBP->units.size() >= 24) break;

            bool tagMatch = false;
            if (tags.empty())
            {
                tagMatch = true;
            }
            else
            {
                for (const auto& t : tags)
                {
                    if (stock.szFileName.find(t) != std::wstring::npos ||
                        stock.szFolder.find(t) != std::wstring::npos ||
                        stock.szCategory.find(t) != std::wstring::npos ||
                        stock.szDetails.find(t) != std::wstring::npos)
                    {
                        tagMatch = true;
                        break;
                    }
                }
            }

            if (tagMatch)
            {
                bool exists = false;
                for (const auto& u : pBP->units)
                {
                    if (_wcsicmp(u.szFileName.c_str(), stock.szFileName.c_str()) == 0 &&
                        _wcsicmp(u.szFolder.c_str(), stock.szFolder.c_str()) == 0)
                    {
                        exists = true;
                        break;
                    }
                }

                if (!exists)
                {
                    PoolManager::PoolUnit pu;
                    pu.szFileName = stock.szFileName;
                    pu.szFolder = stock.szFolder;
                    pu.isEngine = (_wcsicmp(stock.szExtension.c_str(), L".eng") == 0);
                    pu.flipMode = PoolManager::UnitFlipMode::Auto;
                    pBP->units.push_back(pu);
                    totalAdded++;
                }
            }
        }
    }

    LeaveCriticalSection(&g_StockCacheCS);
    return totalAdded;
}



// ------------------------------------------------------------------------------------------------
// Helper: Calculate Target Slot & Drop Indicator for Binding Unit Dragging (Tab 1)
// ------------------------------------------------------------------------------------------------

static int GetBindingUnitDropTarget(const StudioDlgState* pState, int hitPool, POINT ptClient, RECT& outDropIndicator)
{
    outDropIndicator = { 0, 0, 0, 0 };
    if (!pState || hitPool < 0 || hitPool >= (int)pState->activeConfig.pools.size())
        return 0;

    const auto& bp = pState->activeConfig.pools[hitPool];
    int unitCount = 0;
    for (const auto& bpool : pState->activeBinding.pools)
    {
        if (bpool.poolName == bp.poolName)
        {
            unitCount = (int)bpool.units.size();
            break;
        }
    }

    if (unitCount == 0)
    {
        for (const auto& ubh : pState->bindingUnitsBoxHits)
        {
            if (ubh.poolIdx == hitPool)
            {
                outDropIndicator = { ubh.rcUnitsBox.left + 10, ubh.rcUnitsBox.top + 10, ubh.rcUnitsBox.right - 10, ubh.rcUnitsBox.top + 14 };
                break;
            }
        }
        return 0;
    }

    // 1. Direct hit on an existing chip
    for (const auto& uch : pState->bindingUnitChipHits)
    {
        if (uch.poolIdx == hitPool)
        {
            if (PtInRect(&uch.rcChip, ptClient))
            {
                int midY = (uch.rcChip.top + uch.rcChip.bottom) / 2;
                if (ptClient.y < midY)
                {
                    outDropIndicator = { uch.rcChip.left - 2, uch.rcChip.top - 2, uch.rcChip.right + 2, uch.rcChip.top + 2 };
                    return uch.unitIdx;
                }
                else
                {
                    outDropIndicator = { uch.rcChip.left - 2, uch.rcChip.bottom - 2, uch.rcChip.right + 2, uch.rcChip.bottom + 2 };
                    return uch.unitIdx + 1;
                }
            }
        }
    }

    // 2. Cursor in units box or between chips: find closest chip
    for (const auto& ubh : pState->bindingUnitsBoxHits)
    {
        if (ubh.poolIdx == hitPool)
        {
            if (PtInRect(&ubh.rcUnitsBox, ptClient))
            {
                int minDistance = 999999;
                const StudioDlgState::BindingUnitChipHit* pClosest = nullptr;

                for (const auto& uch : pState->bindingUnitChipHits)
                {
                    if (uch.poolIdx == hitPool)
                    {
                        int cx = (uch.rcChip.left + uch.rcChip.right) / 2;
                        int cy = (uch.rcChip.top + uch.rcChip.bottom) / 2;
                        int dist = (ptClient.x - cx) * (ptClient.x - cx) + (ptClient.y - cy) * (ptClient.y - cy);
                        if (dist < minDistance)
                        {
                            minDistance = dist;
                            pClosest = &uch;
                        }
                    }
                }

                if (pClosest)
                {
                    int cy = (pClosest->rcChip.top + pClosest->rcChip.bottom) / 2;
                    if (ptClient.y < cy)
                    {
                        outDropIndicator = { pClosest->rcChip.left - 2, pClosest->rcChip.top - 2, pClosest->rcChip.right + 2, pClosest->rcChip.top + 2 };
                        return pClosest->unitIdx;
                    }
                    else
                    {
                        outDropIndicator = { pClosest->rcChip.left - 2, pClosest->rcChip.bottom - 2, pClosest->rcChip.right + 2, pClosest->rcChip.bottom + 2 };
                        return pClosest->unitIdx + 1;
                    }
                }
                return unitCount;
            }
        }
    }

    return unitCount;
}

// ------------------------------------------------------------------------------------------------
// Studio Window Proc
// ------------------------------------------------------------------------------------------------

static LRESULT CALLBACK TrainConfigStudioWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    StudioDlgState* pState = (StudioDlgState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (StudioDlgState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return TRUE;
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

        if (pt.y >= 0 && pt.y < 66)
        {
            if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
            {
                LRESULT hit = SendMessageW(pState->hTitleBar, WM_NCHITTEST, 0, lParam);
                if (hit == HTTRANSPARENT)
                {
                    return HTCAPTION;
                }
            }
        }

        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS);
            InvalidateRect(pState->hTitleBar, NULL, FALSE);
            UpdateWindow(pState->hTitleBar);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        UpdateWindow(hWnd);
        return 0;
    }

    case WM_CREATE:
    {
        pState->hFontTitle = CreateFontW(-MulDiv(13, GetDpiForSystem(), 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        pState->hFontMain = CreateFontW(-MulDiv(10, GetDpiForSystem(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        pState->hFontBold = CreateFontW(-MulDiv(10, GetDpiForSystem(), 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        pState->hFontSmall = CreateFontW(-MulDiv(9, GetDpiForSystem(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Small");
        pState->hFontBadge = CreateFontW(-MulDiv(8, GetDpiForSystem(), 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Small");
        pState->hFontIcon = CreateFontW(-MulDiv(14, GetDpiForSystem(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
        pState->hFontIconSmall = CreateFontW(-MulDiv(11, GetDpiForSystem(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right > 0 ? rcClient.right : 1140;

        std::vector<TitleBarTabItem> studioTabs = {
            { L"\xE7C0", L"Train Blueprints" },
            { L"\xE8EC", L"Rolling Stock Bindings" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, w, 66,
            30001,
            L"Train Config Studio - TrainSim Consist Builder",
            studioTabs
        );

        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
        }

        TrainConfigManager::ScanTrainConfigs();
        pState->RefreshCategoryOrder();
        if (!pState->categoryOrder.empty())
        {
            pState->selectedCategory = pState->categoryOrder[0];
            pState->selectedConfigIdx = -1;
            for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
            {
                if (TrainConfigManager::g_LoadedConfigsCache[i].category == pState->selectedCategory)
                {
                    pState->selectedConfigIdx = (int)i;
                    pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[i];
                    TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                    break;
                }
            }
        }
        else
        {
            pState->selectedConfigIdx = -1;
            pState->selectedCategory = L"";
            pState->activeConfig = TrainConfigManager::TrainConfig();
            pState->activeBinding = TrainConfigManager::TrainBinding();
        }

        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right, h = rc.bottom;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP holdBmp = (HBITMAP)SelectObject(hdcMem, hbm);

        HBRUSH hbrBg = CreateSolidBrush(PoolTheme::GutterBackground);
        FillRect(hdcMem, &rc, hbrBg);
        DeleteObject(hbrBg);

        pState->clickControls.clear();
        pState->categoryCardHits.clear();
        pState->bpPoolCardHits.clear();
        pState->bindingPoolCardHits.clear();
        pState->bindingUnitChipHits.clear();
        pState->bindingUnitsBoxHits.clear();

        // 1. Toolbar Ribbon (Y = 66 to 112)
        int toolbarY = 66;
        int toolbarH = 46;
        RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
        HBRUSH hbrTb = CreateSolidBrush(PoolTheme::ToolbarBackground);
        FillRect(hdcMem, &rcToolbar, hbrTb);
        DeleteObject(hbrTb);

        HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
        HPEN holdPen = (HPEN)SelectObject(hdcMem, hPenLine);
        MoveToEx(hdcMem, 0, toolbarY + toolbarH, NULL);
        LineTo(hdcMem, w, toolbarY + toolbarH);
        SelectObject(hdcMem, holdPen);
        DeleteObject(hPenLine);

        // Ribbon Icon & Summary Info
        SelectObject(hdcMem, pState->hFontIcon);
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 200, 210));
        RECT rcRibbonIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
        const wchar_t* rIcon = (pState->activeTab == 0) ? L"\xE7C0" : L"\xE8EC";
        DrawTextW(hdcMem, rIcon, -1, &rcRibbonIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdcMem, pState->hFontBold);
        SetTextColor(hdcMem, RGB(245, 245, 245));
        RECT rcRibbonText = { 48, toolbarY, w - 24, toolbarY + toolbarH };
        std::wstring rText = L"";
        if (pState->activeTab == 0)
        {
            rText = L"Train Blueprint Studio";
            if (!pState->selectedCategory.empty())
            {
                rText += L"  •  Folder: " + pState->selectedCategory;
            }
            if (pState->selectedConfigIdx >= 0 && !pState->activeConfig.name.empty())
            {
                rText += L"  •  " + pState->activeConfig.name;
                if (pState->activeConfig.maxSpeedKmph > 0) rText += L"  •  " + std::to_wstring((int)pState->activeConfig.maxSpeedKmph) + L" km/h";
                rText += L"  •  " + std::to_wstring(pState->activeConfig.pools.size()) + L" Pools";
            }
        }
        else
        {
            rText = L"Rolling Stock Bindings";
            if (!pState->selectedCategory.empty())
            {
                rText += L"  •  Folder: " + pState->selectedCategory;
            }
            if (pState->selectedConfigIdx >= 0 && !pState->activeConfig.name.empty())
            {
                rText += L"  •  " + pState->activeConfig.name;
                if (!pState->activeConfig.id.empty()) rText += L" (" + pState->activeConfig.id + L")";
            }
        }
        DrawTextW(hdcMem, rText.c_str(), -1, &rcRibbonText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        int topY = toolbarY + toolbarH; // 112px below ribbon

        // =========================================================================
        // UNIFIED LEFT SIDEBAR (Shared across both Tab 0 and Tab 1)
        // =========================================================================
        int sidebarW = pState->sidebarWidth;
        RECT rcSidebar = { 0, topY, sidebarW, h };
        HBRUSH hbrSide = CreateSolidBrush(StudioTheme::SidebarBg);
        FillRect(hdcMem, &rcSidebar, hbrSide);
        DeleteObject(hbrSide);

            // Splitter Bar (clean double-line splitter)
            int splitterW = 6;
            pState->rcSplitter = { sidebarW, topY, sidebarW + splitterW, h };
            HBRUSH hbrSplitter = CreateSolidBrush(StudioTheme::SidebarBg);
            FillRect(hdcMem, &pState->rcSplitter, hbrSplitter);
            DeleteObject(hbrSplitter);

            HPEN hPenSplitter = CreatePen(PS_SOLID, 1, StudioTheme::CardBorder);
            HPEN hOldP3 = (HPEN)SelectObject(hdcMem, hPenSplitter);

            // Left line of the splitter
            MoveToEx(hdcMem, pState->rcSplitter.left, pState->rcSplitter.top, NULL);
            LineTo(hdcMem, pState->rcSplitter.left, pState->rcSplitter.bottom);

            // Right line of the splitter
            MoveToEx(hdcMem, pState->rcSplitter.right - 1, pState->rcSplitter.top, NULL);
            LineTo(hdcMem, pState->rcSplitter.right - 1, pState->rcSplitter.bottom);

            SelectObject(hdcMem, hOldP3);
            DeleteObject(hPenSplitter);

            // 1. Sidebar Toolbar ([+ New Folder] (Instant creation, no popup), [Open Explorer], [Sync])
            int tbY = topY + 12;
            int btnH = 28;
            int leftMargin = 14;
            int curX = leftMargin;

            // [+ New Folder]
            RECT rcNewFolder = { curX, tbY, curX + 130, tbY + btnH };
            ClickControl ccNewFolder = { ClickControl::BTN_NEW_CATEGORY, rcNewFolder };
            int idxNewFolder = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccNewFolder);
            DrawModernBtn(hdcMem, rcNewFolder, L"New Folder", pState->hoveredControlIdx == idxNewFolder, pState->pressedControlIdx == idxNewFolder, true, pState->hFontBold, pState->hFontIconSmall, L"\xE8F4");
            curX += 136;

            // [Open Explorer]
            RECT rcFolder = { curX, tbY, curX + 44, tbY + btnH };
            ClickControl ccFolder = { ClickControl::BTN_OPEN_FOLDER, rcFolder };
            int idxFolder = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccFolder);
            DrawModernBtn(hdcMem, rcFolder, L"", pState->hoveredControlIdx == idxFolder, pState->pressedControlIdx == idxFolder, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE838");
            curX += 48;

            // [Sync]
            RECT rcRescan = { curX, tbY, curX + 44, tbY + btnH };
            ClickControl ccRescan = { ClickControl::BTN_RESCAN, rcRescan };
            int idxRescan = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccRescan);
            DrawModernBtn(hdcMem, rcRescan, L"", pState->hoveredControlIdx == idxRescan, pState->pressedControlIdx == idxRescan, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE72C");

            // 2. Scrollable Category Cards Area
            pState->RefreshCategoryOrder();
            pState->categoryCardHits.clear();

            int contentY = tbY + btnH + 12;
            int contentH = h - contentY;
            RECT rcSidebarClip = { 0, contentY, sidebarW, h };

            int totalCatH = 10;
            for (size_t p = 0; p < pState->categoryOrder.size(); ++p)
            {
                const auto& cat = pState->categoryOrder[p];
                bool isCollapsed = (pState->collapsedCategories.find(cat) != pState->collapsedCategories.end());
                int catCount = 0;
                for (const auto& cfg : TrainConfigManager::g_LoadedConfigsCache)
                {
                    if (cfg.category == cat) catCount++;
                }
                int bodyH = (catCount == 0) ? 46 : (catCount * 46 + 10);
                int cardH = isCollapsed ? 34 : (34 + bodyH + 8);
                totalCatH += cardH + 12;
            }
            totalCatH += 20;
            pState->totalSidebarH = totalCatH;

            RECT rcSidebarScroll = { sidebarW - 10, contentY + 2, sidebarW - 2, h - 2 };
            pState->m_sidebarScroll.SetBounds(rcSidebarScroll);
            pState->m_sidebarScroll.SetRange(0, (std::max)(0, totalCatH - 1), contentH);
            pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();

            HRGN hRgnSidebarClip = CreateRectRgn(rcSidebarClip.left, rcSidebarClip.top, rcSidebarClip.right, rcSidebarClip.bottom);
            SelectClipRgn(hdcMem, hRgnSidebarClip);

            int cardY = contentY - pState->sidebarScrollY + 6;
            int cardMargin = 12;
            int cardW = sidebarW - (cardMargin * 2);

            if (pState->categoryOrder.empty())
            {
                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcEmpty = { 20, cardY + 30, sidebarW - 20, cardY + 160 };
                DrawTextW(hdcMem, L"No category folders found.\n\nClick [+ New Folder] above to quickly create a folder for organizing your train blueprints.", -1, &rcEmpty, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            else
            {
                for (size_t p = 0; p < pState->categoryOrder.size(); ++p)
                {
                    const auto& cat = pState->categoryOrder[p];
                    bool isCollapsed = (pState->collapsedCategories.find(cat) != pState->collapsedCategories.end());
                    bool isCatSelected = (pState->selectedCategory == cat);

                    int catCount = 0;
                    std::vector<size_t> catCfgIndices;
                    for (size_t c = 0; c < TrainConfigManager::g_LoadedConfigsCache.size(); ++c)
                    {
                        if (TrainConfigManager::g_LoadedConfigsCache[c].category == cat)
                        {
                            catCount++;
                            catCfgIndices.push_back(c);
                        }
                    }

                    int bodyH = (catCount == 0) ? 46 : (catCount * 46 + 10);
                    int cardH = isCollapsed ? 34 : (34 + bodyH + 8);
                    RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

                    bool isHoverCat = (pState->draggingCatIdx == (int)p);
                    COLORREF cBg = isCatSelected ? RGB(32, 40, 52) : PoolTheme::CardBackground;
                    COLORREF cBorder = isCatSelected ? StudioTheme::AccentBlue : (isHoverCat ? StudioTheme::AccentHover : PoolTheme::CardBorder);

                    HBRUSH hbrCard = CreateSolidBrush(cBg);
                    HPEN hpenCard = CreatePen(PS_SOLID, 1, cBorder);
                    HBRUSH holdBr1 = (HBRUSH)SelectObject(hdcMem, hbrCard);
                    HPEN holdPen1 = (HPEN)SelectObject(hdcMem, hpenCard);
                    RoundRect(hdcMem, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 10, 10);
                    SelectObject(hdcMem, holdBr1);
                    SelectObject(hdcMem, holdPen1);
                    DeleteObject(hbrCard);
                    DeleteObject(hpenCard);

                    RECT rcCardHeader = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 34 };
                    StudioDlgState::CategoryCardHit cch;
                    cch.catIdx = (int)p;
                    cch.category = cat;
                    cch.rcCard = rcCard;
                    cch.rcHeader = rcCardHeader;
                    pState->categoryCardHits.push_back(cch);

                    // 1. Collapse Chevron Button
                    RECT rcChevronBtn = { rcCard.left + 8, rcCard.top + 5, rcCard.left + 30, rcCard.top + 29 };
                    ClickControl ccColToggle = { ClickControl::BTN_CAT_COLLAPSE_TOGGLE, rcChevronBtn, (int)p, -1, cat };
                    int idxCol = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccColToggle);
                    DrawModernBtn(hdcMem, rcChevronBtn, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontSmall, pState->hFontIconSmall, isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // 2. Category Title & Badge (Clickable to select category)
                    RECT rcCatTitle = { rcCard.left + 34, rcCard.top, rcCard.right - 238, rcCard.top + 34 };
                    ClickControl ccCatHeader = { ClickControl::BTN_CAT_HEADER, rcCatTitle, (int)p, -1, cat };
                    int idxCatHdr = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccCatHeader);

                    std::wstring titleText = L"#" + std::to_wstring(p + 1) + L"  " + cat + L" (" + std::to_wstring(catCount) + L")";
                    SelectObject(hdcMem, pState->hFontBold);
                    SetTextColor(hdcMem, isCatSelected ? RGB(255, 255, 255) : StudioTheme::TextPrimary);
                    DrawTextW(hdcMem, titleText.c_str(), -1, &rcCatTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // 3. Header Action Buttons: [Rename], [Clone], [▲ Up], [▼ Down], [🗑️ Delete]
                    int hBtnR = rcCard.right - 8;
                    int hBtnY = rcCard.top + 5;
                    int hBtnH = 24;

                    // [Delete]
                    hBtnR -= 30;
                    RECT rcBtnPDel = { hBtnR, hBtnY, hBtnR + 26, hBtnY + hBtnH };
                    ClickControl ccPDel = { ClickControl::BTN_CAT_DELETE, rcBtnPDel, (int)p, -1, cat };
                    int idxPDel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDel);
                    DrawModernBtn(hdcMem, rcBtnPDel, L"", pState->hoveredControlIdx == idxPDel, pState->pressedControlIdx == idxPDel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    // [▼ Down]
                    hBtnR -= 26;
                    RECT rcBtnPDn = { hBtnR, hBtnY, hBtnR + 22, hBtnY + hBtnH };
                    ClickControl ccPDn = { ClickControl::BTN_CAT_DOWN, rcBtnPDn, (int)p, -1, cat };
                    int idxPDn = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDn);
                    DrawModernBtn(hdcMem, rcBtnPDn, L"", pState->hoveredControlIdx == idxPDn, pState->pressedControlIdx == idxPDn, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

                    // [▲ Up]
                    hBtnR -= 26;
                    RECT rcBtnPUp = { hBtnR, hBtnY, hBtnR + 22, hBtnY + hBtnH };
                    ClickControl ccPUp = { ClickControl::BTN_CAT_UP, rcBtnPUp, (int)p, -1, cat };
                    int idxPUp = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPUp);
                    DrawModernBtn(hdcMem, rcBtnPUp, L"", pState->hoveredControlIdx == idxPUp, pState->pressedControlIdx == idxPUp, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

                    // [Clone]
                    hBtnR -= 70;
                    RECT rcBtnPClone = { hBtnR, hBtnY, hBtnR + 66, hBtnY + hBtnH };
                    ClickControl ccPClone = { ClickControl::BTN_CAT_CLONE, rcBtnPClone, (int)p, -1, cat };
                    int idxPClone = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPClone);
                    DrawModernBtn(hdcMem, rcBtnPClone, L"Clone", pState->hoveredControlIdx == idxPClone, pState->pressedControlIdx == idxPClone, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                    // [Rename]
                    hBtnR -= 78;
                    RECT rcBtnPRename = { hBtnR, hBtnY, hBtnR + 74, hBtnY + hBtnH };
                    ClickControl ccPRename = { ClickControl::BTN_CAT_RENAME, rcBtnPRename, (int)p, -1, cat };
                    int idxPRename = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPRename);
                    DrawModernBtn(hdcMem, rcBtnPRename, L"Rename", pState->hoveredControlIdx == idxPRename, pState->pressedControlIdx == idxPRename, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                    if (!isCollapsed)
                    {
                        int itY = rcCard.top + 38;
                        if (catCount == 0)
                        {
                            RECT rcEmptyBox = { rcCard.left + 10, itY, rcCard.right - 10, itY + 44 };
                            HBRUSH hbrEmp = CreateSolidBrush(StudioTheme::TagBg);
                            HPEN hpenEmp = CreatePen(PS_SOLID, 1, StudioTheme::TagBorder);
                            SelectObject(hdcMem, hbrEmp);
                            SelectObject(hdcMem, hpenEmp);
                            RoundRect(hdcMem, rcEmptyBox.left, rcEmptyBox.top, rcEmptyBox.right, rcEmptyBox.bottom, 6, 6);
                            DeleteObject(hbrEmp);
                            DeleteObject(hpenEmp);

                            SelectObject(hdcMem, pState->hFontSmall);
                            SetTextColor(hdcMem, StudioTheme::TextMuted);
                            DrawTextW(hdcMem, L"No train blueprints inside this folder yet.", -1, &rcEmptyBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }
                        else
                        {
                            for (size_t cIdx : catCfgIndices)
                            {
                                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cIdx];
                                bool isSelected = ((int)cIdx == pState->selectedConfigIdx);
                                RECT rcItem = { rcCard.left + 10, itY, rcCard.right - 10, itY + 42 };
                                ClickControl ccItem = { ClickControl::CONFIG_ITEM_CLICK, rcItem, (int)cIdx };
                                int idxItem = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccItem);

                                bool isHover = (pState->hoveredControlIdx == idxItem);
                                COLORREF itemBg = isSelected ? RGB(0, 90, 160) : (isHover ? RGB(38, 38, 38) : RGB(24, 24, 24));
                                COLORREF itemBdr = isSelected ? StudioTheme::AccentBlue : (isHover ? RGB(65, 65, 65) : StudioTheme::CardBorder);

                                HBRUSH hbrIt = CreateSolidBrush(itemBg);
                                HPEN hPenIt = CreatePen(PS_SOLID, 1, itemBdr);
                                SelectObject(hdcMem, hbrIt);
                                SelectObject(hdcMem, hPenIt);
                                RoundRect(hdcMem, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 6, 6);
                                DeleteObject(hbrIt);
                                DeleteObject(hPenIt);

                                // Train Icon - Vertically centered across the entire card height (42px)
                                SelectObject(hdcMem, pState->hFontIcon);
                                SetTextColor(hdcMem, isSelected ? RGB(220, 240, 255) : RGB(140, 140, 140));
                                RECT rcFileIcon = { rcItem.left + 12, rcItem.top, rcItem.left + 34, rcItem.bottom };
                                DrawTextW(hdcMem, L"\xE7C0", -1, &rcFileIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                                // Title
                                SelectObject(hdcMem, pState->hFontBold);
                                SetTextColor(hdcMem, RGB(255, 255, 255));
                                RECT rcName = { rcItem.left + 46, rcItem.top + 4, rcItem.right - 8, rcItem.top + 22 };
                                DrawTextW(hdcMem, cfg.name.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                // Subtitle
                                SelectObject(hdcMem, pState->hFontSmall);
                                SetTextColor(hdcMem, isSelected ? RGB(210, 230, 255) : StudioTheme::TextSecondary);
                                std::wstring sub = std::to_wstring(cfg.pools.size()) + L" pools";
                                if (cfg.maxSpeedKmph > 0) sub += L" • " + std::to_wstring((int)cfg.maxSpeedKmph) + L" km/h";
                                RECT rcSub = { rcItem.left + 46, rcItem.top + 22, rcItem.right - 8, rcItem.bottom - 4 };
                                DrawTextW(hdcMem, sub.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                itY += 46;
                            }
                        }
                    }

                    cardY += cardH + 12;
                }
            }

            if (pState->isCatDragging)
            {
                DrawSolidYellowDropIndicator(hdcMem, pState->rcCatDropIndicator);
            }

            SelectClipRgn(hdcMem, NULL);
            DeleteObject(hRgnSidebarClip);
            pState->m_sidebarScroll.Paint(hdcMem, CLR_INVALID, StudioTheme::SidebarBg);

            // =========================================================================
            // 3. Right Editor Area Logic
            // =========================================================================
            int editorX = sidebarW + splitterW + 24;
            int editorW = w - editorX - 24;

            if (pState->activeTab == 0)
            {
                // =========================================================================
                // TAB 0: TRAIN BLUEPRINTS EDITOR
                // =========================================================================
                int editorTop = topY + 1;
                int editorH = h - editorTop;

            RECT rcEditorScroll = { w - 10, editorTop + 2, w - 2, h - 2 };
            pState->m_editorScroll.SetBounds(rcEditorScroll);
            pState->m_editorScroll.SetRange(0, (std::max)(0, pState->totalEditorH - 1), editorH);
            pState->editorScrollY = pState->m_editorScroll.GetPos();

            int edY = topY + 16 - pState->editorScrollY;

            RECT rcEditorClip = { editorX - 4, topY, w, h };
            HRGN hRgnEditorClip = CreateRectRgn(rcEditorClip.left, rcEditorClip.top, rcEditorClip.right, rcEditorClip.bottom);
            SelectClipRgn(hdcMem, hRgnEditorClip);

            if (pState->selectedCategory.empty())
            {
                // Condition 1: No Folder Selected or No Folders Exist -> BLANK / PLACEHOLDER
                pState->ShowEditorFields(false);

                SelectObject(hdcMem, pState->hFontIcon);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcIconPlace = { editorX, topY + 120, editorX + editorW, topY + 160 };
                DrawTextW(hdcMem, L"\xE838", -1, &rcIconPlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontTitle);
                SetTextColor(hdcMem, StudioTheme::TextSecondary);
                RECT rcTitlePlace = { editorX, topY + 165, editorX + editorW, topY + 195 };
                DrawTextW(hdcMem, L"No Folder Selected", -1, &rcTitlePlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcDescPlace = { editorX + 40, topY + 200, editorX + editorW - 40, topY + 260 };
                DrawTextW(hdcMem, L"Select or create a category folder on the left panel to begin creating and organizing train blueprints.", -1, &rcDescPlace, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            else if (pState->selectedConfigIdx < 0)
            {
                // Condition 2: Folder Selected, but No Train Blueprint Selected
                pState->ShowEditorFields(false);

                // Top Header with [+ New Train Blueprint] button
                SelectObject(hdcMem, pState->hFontTitle);
                SetTextColor(hdcMem, StudioTheme::TextPrimary);
                RECT rcHeader = { editorX, edY, editorX + editorW - 160, edY + 30 };
                std::wstring fldHdr = L"📁 Folder: " + pState->selectedCategory;
                DrawTextW(hdcMem, fldHdr.c_str(), -1, &rcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // [+ New Train Blueprint]
                RECT rcNewTrainBtn = { editorX + editorW - 170, edY, editorX + editorW, edY + 32 };
                ClickControl ccNewTrain = { ClickControl::BTN_NEW_BLUEPRINT, rcNewTrainBtn };
                int idxNewTrain = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccNewTrain);
                DrawModernBtn(hdcMem, rcNewTrainBtn, L"New Train", pState->hoveredControlIdx == idxNewTrain, pState->pressedControlIdx == idxNewTrain, true, pState->hFontBold, pState->hFontIconSmall, L"\xE710");

                // Empty state inside folder
                SelectObject(hdcMem, pState->hFontIcon);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcIconPlace = { editorX, topY + 130, editorX + editorW, topY + 170 };
                DrawTextW(hdcMem, L"\xE7C0", -1, &rcIconPlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontTitle);
                SetTextColor(hdcMem, StudioTheme::TextSecondary);
                RECT rcTitlePlace = { editorX, topY + 175, editorX + editorW, topY + 205 };
                DrawTextW(hdcMem, L"No Train Blueprint Selected", -1, &rcTitlePlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcDescPlace = { editorX + 40, topY + 210, editorX + editorW - 40, topY + 270 };
                std::wstring subMsg = L"Click [+ New Train] above to create a new .train blueprint inside '" + pState->selectedCategory + L"', or select an existing blueprint from the left list.";
                DrawTextW(hdcMem, subMsg.c_str(), -1, &rcDescPlace, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            else
            {
                // Condition 3: Folder & Blueprint Selected -> FULL EDITOR
                pState->ShowEditorFields(true);

                // Top Header Row with Action Buttons
                SelectObject(hdcMem, pState->hFontTitle);
                SetTextColor(hdcMem, StudioTheme::TextPrimary);
                RECT rcHeader = { editorX, edY, editorX + 320, edY + 30 };
                DrawTextW(hdcMem, L"🚆 Train Blueprint Specification", -1, &rcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Top Action Buttons: [+ New Train] [Clone] [Delete]
                int rCurX = editorX + editorW - 280;
                int rBtnH = 30;

                RECT rcNewT = { rCurX, edY, rCurX + 110, edY + rBtnH };
                ClickControl ccNewT = { ClickControl::BTN_NEW_BLUEPRINT, rcNewT };
                int idxNewT = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccNewT);
                DrawModernBtn(hdcMem, rcNewT, L"New Train", pState->hoveredControlIdx == idxNewT, pState->pressedControlIdx == idxNewT, true, pState->hFontSmall, pState->hFontIconSmall, L"\xE710");
                rCurX += 116;

                RECT rcCloneT = { rCurX, edY, rCurX + 76, edY + rBtnH };
                ClickControl ccCloneT = { ClickControl::BTN_CLONE_BLUEPRINT, rcCloneT };
                int idxCloneT = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccCloneT);
                DrawModernBtn(hdcMem, rcCloneT, L"Clone", pState->hoveredControlIdx == idxCloneT, pState->pressedControlIdx == idxCloneT, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");
                rCurX += 82;

                RECT rcDelT = { rCurX, edY, rCurX + 80, edY + rBtnH };
                ClickControl ccDelT = { ClickControl::BTN_DELETE_BLUEPRINT, rcDelT };
                int idxDelT = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccDelT);
                DrawModernBtn(hdcMem, rcDelT, L"Delete", pState->hoveredControlIdx == idxDelT, pState->pressedControlIdx == idxDelT, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                edY += 38;

                // Train Metadata Card
                int cardPad = 16;
                int fieldH = 32;
                int colGap = 20;
                int rowGap = 10;
                int labelW = 120;
                int cardH = cardPad * 2 + (fieldH * 4) + (rowGap * 3); // 190px

                RECT rcMetaCard = { editorX, edY, editorX + editorW, edY + cardH };
                HBRUSH hbrCard = CreateSolidBrush(StudioTheme::CardBg);
                HPEN hPenCard = CreatePen(PS_SOLID, 1, StudioTheme::CardBorder);
                SelectObject(hdcMem, hbrCard);
                SelectObject(hdcMem, hPenCard);
                RoundRect(hdcMem, rcMetaCard.left, rcMetaCard.top, rcMetaCard.right, rcMetaCard.bottom, 8, 8);
                DeleteObject(hbrCard);
                DeleteObject(hPenCard);

                // Labels & Positioning of Edit Controls
                int halfW = (editorW - (cardPad * 2) - colGap) / 2;
                int col1LabelX = editorX + cardPad;
                int col1ValX = col1LabelX + labelW + 8;
                int col1ValW = halfW - labelW - 8;

                int col2LabelX = col1LabelX + halfW + colGap;
                int col2ValX = col2LabelX + labelW + 8;
                int col2ValW = halfW - labelW - 8;

                int curRowY = edY + cardPad;

                auto drawStudioInputField = [&](const wchar_t* label, const std::wstring& val, const wchar_t* placeholder, int lblX, int valX, int valW, int rowY, ClickControl::Type ctrlType) {
                    RECT rcLbl = { lblX, rowY, valX - 8, rowY + fieldH };
                    SelectObject(hdcMem, pState->hFontBold);
                    SetTextColor(hdcMem, StudioTheme::TextSecondary);
                    DrawTextW(hdcMem, label, -1, &rcLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    RECT rcVal = { valX, rowY, valX + valW, rowY + fieldH };
                    ClickControl cc = { ctrlType, rcVal };
                    int idx = (int)pState->clickControls.size();
                    pState->clickControls.push_back(cc);

                    bool isHover = (pState->hoveredControlIdx == idx);
                    bool isPressed = (pState->pressedControlIdx == idx);

                    COLORREF bgCol = isPressed ? RGB(26, 26, 26) : (isHover ? RGB(45, 45, 45) : RGB(34, 34, 34));
                    COLORREF borderCol = isPressed ? StudioTheme::AccentBlue : (isHover ? RGB(80, 80, 80) : StudioTheme::CardBorder);

                    HBRUSH hbr = CreateSolidBrush(bgCol);
                    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
                    SelectObject(hdcMem, hbr);
                    SelectObject(hdcMem, hPen);
                    RoundRect(hdcMem, rcVal.left, rcVal.top, rcVal.right, rcVal.bottom, 6, 6);
                    DeleteObject(hbr);
                    DeleteObject(hPen);

                    RECT rcText = { rcVal.left + 10, rcVal.top, rcVal.right - (isHover ? 26 : 10), rcVal.bottom };
                    SelectObject(hdcMem, val.empty() ? pState->hFontSmall : pState->hFontBold);
                    SetTextColor(hdcMem, val.empty() ? StudioTheme::TextMuted : StudioTheme::TextPrimary);
                    DrawTextW(hdcMem, val.empty() ? placeholder : val.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    if (isHover) {
                        SelectObject(hdcMem, pState->hFontIconSmall);
                        SetTextColor(hdcMem, StudioTheme::AccentBlue);
                        RECT rcEditIcon = { rcVal.right - 24, rcVal.top, rcVal.right - 6, rcVal.bottom };
                        DrawTextW(hdcMem, L"\xE70F", -1, &rcEditIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                };

                // Row 1: Train Name & Train ID
                drawStudioInputField(L"Train Name", pState->activeConfig.name, L"Enter Train Name...", col1LabelX, col1ValX, col1ValW, curRowY, ClickControl::BTN_EDIT_NAME);
                drawStudioInputField(L"Train ID", pState->activeConfig.id, L"Enter Unique ID...", col2LabelX, col2ValX, col2ValW, curRowY, ClickControl::BTN_EDIT_ID);

                curRowY += fieldH + rowGap;

                // Row 2: Type / Power & Category
                drawStudioInputField(L"Type / Power", pState->activeConfig.trainType, L"e.g. Electric / Diesel / EMU", col1LabelX, col1ValX, col1ValW, curRowY, ClickControl::BTN_EDIT_TYPE);
                drawStudioInputField(L"Category", pState->activeConfig.category, L"e.g. Express / Freight", col2LabelX, col2ValX, col2ValW, curRowY, ClickControl::BTN_EDIT_CATEGORY);

                curRowY += fieldH + rowGap;

                // Row 3: Speed Limit & Performance Factor
                std::wstring speedStr = (pState->activeConfig.maxSpeedKmph > 0.0) ? std::to_wstring((int)pState->activeConfig.maxSpeedKmph) : L"";
                drawStudioInputField(L"Speed Limit (km/h)", speedStr, L"e.g. 130", col1LabelX, col1ValX, col1ValW, curRowY, ClickControl::BTN_EDIT_MAXSPEED);

                wchar_t szPerf[64] = { 0 };
                swprintf_s(szPerf, 64, (pState->activeConfig.perfFactor == (int)pState->activeConfig.perfFactor) ? L"%.1f" : L"%.3f", pState->activeConfig.perfFactor);
                std::wstring perfStr = (pState->activeConfig.perfFactor > 0.0) ? szPerf : L"1.0";
                drawStudioInputField(L"Performance Factor (%)", perfStr, L"e.g. 1.0 or 0.100", col2LabelX, col2ValX, col2ValW, curRowY, ClickControl::BTN_EDIT_PERFFACTOR);

                curRowY += fieldH + rowGap;

                // Row 4: Description (Full row width)
                int descValW = editorW - (cardPad * 2) - labelW - 8;
                drawStudioInputField(L"Description", pState->activeConfig.description, L"Enter train description, rake details or operational notes...", col1LabelX, col1ValX, descValW, curRowY, ClickControl::BTN_EDIT_DESC);

                edY += cardH + 16;

                // 4. Consist Sequence Order Flow Visualizer
                RECT rcSeqCard = { editorX, edY, editorX + editorW, edY + 68 };
                HBRUSH hbrSeq = CreateSolidBrush(StudioTheme::CardBg);
                HPEN hPenSeq = CreatePen(PS_SOLID, 1, StudioTheme::CardBorder);
                SelectObject(hdcMem, hbrSeq);
                SelectObject(hdcMem, hPenSeq);
                RoundRect(hdcMem, rcSeqCard.left, rcSeqCard.top, rcSeqCard.right, rcSeqCard.bottom, 6, 6);
                DeleteObject(hbrSeq);
                DeleteObject(hPenSeq);

                SelectObject(hdcMem, pState->hFontBold);
                SetTextColor(hdcMem, RGB(255, 200, 100));
                RECT rcSeqTitle = { rcSeqCard.left + 14, rcSeqCard.top + 6, rcSeqCard.left + 350, rcSeqCard.top + 24 };
                DrawTextW(hdcMem, L"🔗 Consist Sequence Order (Front ➔ Rear Formation)", -1, &rcSeqTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int seqX = rcSeqCard.left + 14;
                int seqY = rcSeqCard.top + 32;
                if (pState->activeConfig.pools.empty())
                {
                    SelectObject(hdcMem, pState->hFontSmall);
                    SetTextColor(hdcMem, StudioTheme::TextMuted);
                    RECT rcEmptySeq = { seqX, seqY, rcSeqCard.right - 14, seqY + 24 };
                    DrawTextW(hdcMem, L"No pools defined yet. Click [+ Add Pool to Rake] below to add coaches and locomotives.", -1, &rcEmptySeq, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else
                {
                    for (size_t s = 0; s < pState->activeConfig.pools.size(); ++s)
                    {
                        const auto& pool = pState->activeConfig.pools[s];
                        std::wstring chipText = std::to_wstring(s + 1) + L". " + pool.poolName;
                        SIZE szChip;
                        SelectObject(hdcMem, pState->hFontSmall);
                        GetTextExtentPoint32W(hdcMem, chipText.c_str(), (int)chipText.length(), &szChip);
                        int chipW = szChip.cx + 20;

                        if (seqX + chipW > rcSeqCard.right - 20)
                        {
                            std::wstring moreStr = L"+" + std::to_wstring(pState->activeConfig.pools.size() - s) + L" more";
                            RECT rcMore = { seqX, seqY, seqX + 60, seqY + 24 };
                            HBRUSH hbrM = CreateSolidBrush(StudioTheme::TagBg);
                            SelectObject(hdcMem, hbrM);
                            SelectObject(hdcMem, GetStockObject(NULL_PEN));
                            RoundRect(hdcMem, rcMore.left, rcMore.top, rcMore.right, rcMore.bottom, 4, 4);
                            DeleteObject(hbrM);
                            SetTextColor(hdcMem, StudioTheme::TextSecondary);
                            DrawTextW(hdcMem, moreStr.c_str(), -1, &rcMore, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            break;
                        }

                        RECT rcChip = { seqX, seqY, seqX + chipW, seqY + 24 };
                        HBRUSH hbrChip = CreateSolidBrush(RGB(38, 44, 54));
                        HPEN hPenChip = CreatePen(PS_SOLID, 1, RGB(60, 75, 95));
                        SelectObject(hdcMem, hbrChip);
                        SelectObject(hdcMem, hPenChip);
                        RoundRect(hdcMem, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                        DeleteObject(hbrChip);
                        DeleteObject(hPenChip);

                        SetTextColor(hdcMem, RGB(220, 235, 255));
                        DrawTextW(hdcMem, chipText.c_str(), -1, &rcChip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        seqX += chipW + 6;

                        if (s + 1 < pState->activeConfig.pools.size())
                        {
                            SelectObject(hdcMem, pState->hFontBold);
                            SetTextColor(hdcMem, StudioTheme::TextMuted);
                            RECT rcArr = { seqX, seqY, seqX + 14, seqY + 24 };
                            DrawTextW(hdcMem, L"➔", -1, &rcArr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            seqX += 18;
                        }
                    }
                }

                edY += 80;

                // 5. Composition Pools Header & [+ Add Pool to Rake]
                SelectObject(hdcMem, pState->hFontBold);
                SetTextColor(hdcMem, StudioTheme::TextPrimary);
                RECT rcPoolHeader = { editorX, edY, editorX + 250, edY + 28 };
                DrawTextW(hdcMem, L"📦 Defined Composition Pools", -1, &rcPoolHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT rcBtnAddPool = { editorX + editorW - 160, edY - 2, editorX + editorW, edY + 28 };
                ClickControl ccAddPool = { ClickControl::BTN_ADD_POOL_TO_RAKE, rcBtnAddPool };
                int idxAddPool = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccAddPool);
                DrawModernBtn(hdcMem, rcBtnAddPool, L"Add Pool to Rake", pState->hoveredControlIdx == idxAddPool, pState->pressedControlIdx == idxAddPool, true, pState->hFontMain, pState->hFontIconSmall, L"\xE710");

                edY += 36;

                // Total Units (Rake Boundary) Card - Placed above/before the first pool
                RECT rcTotalBar = { editorX, edY, editorX + editorW, edY + 44 };
                HBRUSH hbrTot = CreateSolidBrush(StudioTheme::CardBg);
                HPEN hPenTot = CreatePen(PS_SOLID, 1, StudioTheme::CardBorder);
                SelectObject(hdcMem, hbrTot);
                SelectObject(hdcMem, hPenTot);
                RoundRect(hdcMem, rcTotalBar.left, rcTotalBar.top, rcTotalBar.right, rcTotalBar.bottom, 6, 6);
                DeleteObject(hbrTot);
                DeleteObject(hPenTot);

                RECT rcTotTitle = { rcTotalBar.left + 14, rcTotalBar.top, rcTotalBar.left + 230, rcTotalBar.bottom };
                SelectObject(hdcMem, pState->hFontBold);
                SetTextColor(hdcMem, RGB(240, 240, 240));
                DrawTextW(hdcMem, L"Total Units (Rake Length):", -1, &rcTotTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int totX = rcTotalBar.left + 230;
                int totSetY = rcTotalBar.top + 9;

                // Min: [-] [val] [+]
                RECT rcTotMinLbl = { totX, totSetY, totX + 34, totSetY + 26 };
                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextSecondary);
                DrawTextW(hdcMem, L"Min:", -1, &rcTotMinLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                totX += 34;

                RECT rcTotMinDec = { totX, totSetY, totX + 24, totSetY + 26 };
                ClickControl ccTotMinDec = { ClickControl::BTN_TOTAL_MIN_DEC, rcTotMinDec };
                int idxTotMinDec = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMinDec);
                DrawModernBtn(hdcMem, rcTotMinDec, L"-", pState->hoveredControlIdx == idxTotMinDec, pState->pressedControlIdx == idxTotMinDec, false, pState->hFontSmall);
                totX += 26;

                RECT rcTotMinVal = { totX, totSetY, totX + 40, totSetY + 26 };
                ClickControl ccTotMinVal = { ClickControl::BTN_TOTAL_MIN_EDIT, rcTotMinVal };
                int idxTotMinVal = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMinVal);
                DrawModernBtn(hdcMem, rcTotMinVal, std::to_wstring(pState->activeConfig.minLength).c_str(), pState->hoveredControlIdx == idxTotMinVal, pState->pressedControlIdx == idxTotMinVal, false, pState->hFontBold);
                totX += 42;

                RECT rcTotMinInc = { totX, totSetY, totX + 24, totSetY + 26 };
                ClickControl ccTotMinInc = { ClickControl::BTN_TOTAL_MIN_INC, rcTotMinInc };
                int idxTotMinInc = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMinInc);
                DrawModernBtn(hdcMem, rcTotMinInc, L"+", pState->hoveredControlIdx == idxTotMinInc, pState->pressedControlIdx == idxTotMinInc, false, pState->hFontSmall);
                totX += 36;

                // Max: [-] [val] [+]
                RECT rcTotMaxLbl = { totX, totSetY, totX + 34, totSetY + 26 };
                DrawTextW(hdcMem, L"Max:", -1, &rcTotMaxLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                totX += 34;

                RECT rcTotMaxDec = { totX, totSetY, totX + 24, totSetY + 26 };
                ClickControl ccTotMaxDec = { ClickControl::BTN_TOTAL_MAX_DEC, rcTotMaxDec };
                int idxTotMaxDec = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMaxDec);
                DrawModernBtn(hdcMem, rcTotMaxDec, L"-", pState->hoveredControlIdx == idxTotMaxDec, pState->pressedControlIdx == idxTotMaxDec, false, pState->hFontSmall);
                totX += 26;

                RECT rcTotMaxVal = { totX, totSetY, totX + 40, totSetY + 26 };
                ClickControl ccTotMaxVal = { ClickControl::BTN_TOTAL_MAX_EDIT, rcTotMaxVal };
                int idxTotMaxVal = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMaxVal);
                DrawModernBtn(hdcMem, rcTotMaxVal, std::to_wstring(pState->activeConfig.maxLength).c_str(), pState->hoveredControlIdx == idxTotMaxVal, pState->pressedControlIdx == idxTotMaxVal, false, pState->hFontBold);
                totX += 42;

                RECT rcTotMaxInc = { totX, totSetY, totX + 24, totSetY + 26 };
                ClickControl ccTotMaxInc = { ClickControl::BTN_TOTAL_MAX_INC, rcTotMaxInc };
                int idxTotMaxInc = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccTotMaxInc);
                DrawModernBtn(hdcMem, rcTotMaxInc, L"+", pState->hoveredControlIdx == idxTotMaxInc, pState->pressedControlIdx == idxTotMaxInc, false, pState->hFontSmall);
                totX += 36;

                edY += 56;

                // 6. Pool Rule Cards (Full Pool-Card Parity)
                pState->bpPoolCardHits.clear();
                for (size_t p = 0; p < pState->activeConfig.pools.size(); ++p)
                {
                    const auto& pool = pState->activeConfig.pools[p];
                    bool isCollapsed = (pState->collapsedBlueprintPools.find((int)p) != pState->collapsedBlueprintPools.end());
                    int cardH = isCollapsed ? 34 : 76;
                    RECT rcPoolCard = { editorX, edY, editorX + editorW, edY + cardH };

                    bool isHoverPool = (pState->draggingBpPoolIdx == (int)p);
                    COLORREF cBg = isHoverPool ? RGB(32, 40, 52) : PoolTheme::CardBackground;
                    COLORREF cBorder = isHoverPool ? StudioTheme::AccentHover : PoolTheme::CardBorder;

                    HBRUSH hbrPC = CreateSolidBrush(cBg);
                    HPEN hPenPC = CreatePen(PS_SOLID, 1, cBorder);
                    HBRUSH holdBr1 = (HBRUSH)SelectObject(hdcMem, hbrPC);
                    HPEN holdPen1 = (HPEN)SelectObject(hdcMem, hPenPC);
                    RoundRect(hdcMem, rcPoolCard.left, rcPoolCard.top, rcPoolCard.right, rcPoolCard.bottom, 10, 10);
                    SelectObject(hdcMem, holdBr1);
                    SelectObject(hdcMem, holdPen1);
                    DeleteObject(hbrPC);
                    DeleteObject(hPenPC);

                    RECT rcCardHeader = { rcPoolCard.left, rcPoolCard.top, rcPoolCard.right, rcPoolCard.top + 34 };
                    StudioDlgState::BlueprintPoolCardHit bpch;
                    bpch.poolIdx = (int)p;
                    bpch.rcCard = rcPoolCard;
                    bpch.rcHeader = rcCardHeader;
                    pState->bpPoolCardHits.push_back(bpch);

                    // 1. Collapse Chevron Button
                    RECT rcChevronBtn = { rcPoolCard.left + 8, rcPoolCard.top + 5, rcPoolCard.left + 30, rcPoolCard.top + 29 };
                    ClickControl ccColToggle = { ClickControl::BTN_POOL_COLLAPSE_TOGGLE, rcChevronBtn, (int)p, -1 };
                    int idxCol = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccColToggle);
                    DrawModernBtn(hdcMem, rcChevronBtn, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontSmall, pState->hFontIconSmall, isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // 2. Pool Number & Name Text
                    std::wstring pNameText = L"#" + std::to_wstring(p + 1) + L"  " + pool.poolName;
                    SIZE szPName = { 0 };
                    SelectObject(hdcMem, pState->hFontBold);
                    GetTextExtentPoint32W(hdcMem, pNameText.c_str(), (int)pNameText.length(), &szPName);

                    int pNameLeft = rcPoolCard.left + 36;
                    int maxPNameW = rcPoolCard.right - 280 - pNameLeft - 110;
                    int pNameW = szPName.cx;
                    if (maxPNameW > 60 && pNameW > maxPNameW) pNameW = maxPNameW;

                    RECT rcPoolTitle = { pNameLeft, rcPoolCard.top, pNameLeft + pNameW, rcPoolCard.top + 34 };
                    SetTextColor(hdcMem, RGB(255, 255, 255));
                    DrawTextW(hdcMem, pNameText.c_str(), -1, &rcPoolTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // 3. Interactive Role Badge Button (e.g. "• LeadLoco ▾")
                    int roleBadgeX = rcPoolTitle.right + 10;
                    std::wstring roleBadgeText = (pool.role.empty() ? L"Role: (None)" : (L"•  " + pool.role)) + L"  ▾";
                    SIZE szRole = { 0 };
                    SelectObject(hdcMem, pState->hFontSmall);
                    GetTextExtentPoint32W(hdcMem, roleBadgeText.c_str(), (int)roleBadgeText.length(), &szRole);
                    int roleBadgeW = szRole.cx + 20;
                    int roleBadgeMaxR = rcPoolCard.right - 280;
                    if (roleBadgeX + roleBadgeW > roleBadgeMaxR) {
                        roleBadgeW = roleBadgeMaxR - roleBadgeX;
                    }

                    if (roleBadgeW >= 40)
                    {
                        RECT rcRoleBadge = { roleBadgeX, rcPoolCard.top + 5, roleBadgeX + roleBadgeW, rcPoolCard.top + 29 };
                        ClickControl ccRole = { ClickControl::BTN_POOL_ROLE_PICK, rcRoleBadge, (int)p, -1 };
                        int idxRole = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccRole);
                        DrawModernBtn(hdcMem, rcRoleBadge, roleBadgeText.c_str(), pState->hoveredControlIdx == idxRole, pState->pressedControlIdx == idxRole, false, pState->hFontSmall);
                    }

                    // 4. Header Action Buttons: [Rename], [Clone], [▲ Up], [▼ Down], [🗑️ Delete]
                    int hBtnR = rcPoolCard.right - 8;
                    int hBtnY = rcPoolCard.top + 5;
                    int hBtnH = 24;

                    // [Delete]
                    hBtnR -= 30;
                    RECT rcBtnPDel = { hBtnR, hBtnY, hBtnR + 28, hBtnY + hBtnH };
                    ClickControl ccPDel = { ClickControl::BTN_POOL_DELETE, rcBtnPDel, (int)p, -1 };
                    int idxPDel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDel);
                    DrawModernBtn(hdcMem, rcBtnPDel, L"", pState->hoveredControlIdx == idxPDel, pState->pressedControlIdx == idxPDel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    // [▼ Down]
                    hBtnR -= 26;
                    RECT rcBtnPDn = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    ClickControl ccPDn = { ClickControl::BTN_POOL_DOWN, rcBtnPDn, (int)p, -1 };
                    int idxPDn = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDn);
                    DrawModernBtn(hdcMem, rcBtnPDn, L"", pState->hoveredControlIdx == idxPDn, pState->pressedControlIdx == idxPDn, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

                    // [▲ Up]
                    hBtnR -= 26;
                    RECT rcBtnPUp = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    ClickControl ccPUp = { ClickControl::BTN_POOL_UP, rcBtnPUp, (int)p, -1 };
                    int idxPUp = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPUp);
                    DrawModernBtn(hdcMem, rcBtnPUp, L"", pState->hoveredControlIdx == idxPUp, pState->pressedControlIdx == idxPUp, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

                    // [Clone]
                    hBtnR -= 70;
                    RECT rcBtnPClone = { hBtnR, hBtnY, hBtnR + 66, hBtnY + hBtnH };
                    ClickControl ccPClone = { ClickControl::BTN_POOL_CLONE, rcBtnPClone, (int)p, -1 };
                    int idxPClone = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPClone);
                    DrawModernBtn(hdcMem, rcBtnPClone, L"Clone", pState->hoveredControlIdx == idxPClone, pState->pressedControlIdx == idxPClone, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                    // [Rename]
                    hBtnR -= 78;
                    RECT rcBtnPRename = { hBtnR, hBtnY, hBtnR + 74, hBtnY + hBtnH };
                    ClickControl ccPRename = { ClickControl::BTN_POOL_RENAME, rcBtnPRename, (int)p, -1 };
                    int idxPRename = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPRename);
                    DrawModernBtn(hdcMem, rcBtnPRename, L"Rename", pState->hoveredControlIdx == idxPRename, pState->pressedControlIdx == idxPRename, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                    if (!isCollapsed)
                    {
                        // Settings row (Y = rcPoolCard.top + 38)
                        int setY = rcPoolCard.top + 38;
                        int curX = rcPoolCard.left + 16;

                        // Min Count: [-] [val] [+]
                        SelectObject(hdcMem, pState->hFontSmall);
                        SetTextColor(hdcMem, StudioTheme::TextSecondary);
                        RECT rcMinLbl = { curX, setY, curX + 32, setY + 26 };
                        DrawTextW(hdcMem, L"Min:", -1, &rcMinLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        curX += 32;

                        RECT rcMinDec = { curX, setY, curX + 24, setY + 26 };
                        ClickControl ccMinDec = { ClickControl::BTN_POOL_MIN_DEC, rcMinDec, (int)p, -1 };
                        int idxMinDec = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinDec);
                        DrawModernBtn(hdcMem, rcMinDec, L"-", pState->hoveredControlIdx == idxMinDec, pState->pressedControlIdx == idxMinDec, false, pState->hFontSmall);
                        curX += 26;

                        RECT rcMinVal = { curX, setY, curX + 36, setY + 26 };
                        ClickControl ccMinVal = { ClickControl::BTN_POOL_MIN_EDIT, rcMinVal, (int)p, -1 };
                        int idxMinVal = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinVal);
                        DrawModernBtn(hdcMem, rcMinVal, std::to_wstring(pool.minCount).c_str(), pState->hoveredControlIdx == idxMinVal, pState->pressedControlIdx == idxMinVal, false, pState->hFontBold);
                        curX += 38;

                        RECT rcMinInc = { curX, setY, curX + 24, setY + 26 };
                        ClickControl ccMinInc = { ClickControl::BTN_POOL_MIN_INC, rcMinInc, (int)p, -1 };
                        int idxMinInc = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinInc);
                        DrawModernBtn(hdcMem, rcMinInc, L"+", pState->hoveredControlIdx == idxMinInc, pState->pressedControlIdx == idxMinInc, false, pState->hFontSmall);
                        curX += 34;

                        // Max Count: [-] [val] [+]
                        RECT rcMaxLbl = { curX, setY, curX + 34, setY + 26 };
                        DrawTextW(hdcMem, L"Max:", -1, &rcMaxLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        curX += 34;

                        RECT rcMaxDec = { curX, setY, curX + 24, setY + 26 };
                        ClickControl ccMaxDec = { ClickControl::BTN_POOL_MAX_DEC, rcMaxDec, (int)p, -1 };
                        int idxMaxDec = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxDec);
                        DrawModernBtn(hdcMem, rcMaxDec, L"-", pState->hoveredControlIdx == idxMaxDec, pState->pressedControlIdx == idxMaxDec, false, pState->hFontSmall);
                        curX += 26;

                        RECT rcMaxVal = { curX, setY, curX + 36, setY + 26 };
                        ClickControl ccMaxVal = { ClickControl::BTN_POOL_MAX_EDIT, rcMaxVal, (int)p, -1 };
                        int idxMaxVal = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxVal);
                        DrawModernBtn(hdcMem, rcMaxVal, std::to_wstring(pool.maxCount).c_str(), pState->hoveredControlIdx == idxMaxVal, pState->pressedControlIdx == idxMaxVal, false, pState->hFontBold);
                        curX += 38;

                        RECT rcMaxInc = { curX, setY, curX + 24, setY + 26 };
                        ClickControl ccMaxInc = { ClickControl::BTN_POOL_MAX_INC, rcMaxInc, (int)p, -1 };
                        int idxMaxInc = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxInc);
                        DrawModernBtn(hdcMem, rcMaxInc, L"+", pState->hoveredControlIdx == idxMaxInc, pState->pressedControlIdx == idxMaxInc, false, pState->hFontSmall);
                        curX += 38;

                        // Filter Tags Button (Spans the rest of the parameter row)
                        int tagsW = (rcPoolCard.right - 14) - curX;
                        if (tagsW > 60)
                        {
                            RECT rcTagsBtn = { curX, setY, rcPoolCard.right - 14, setY + 26 };
                            ClickControl ccTags = { ClickControl::BTN_POOL_EDIT, rcTagsBtn, (int)p, -1 };
                            int idxTags = (int)pState->clickControls.size();
                            pState->clickControls.push_back(ccTags);
                            std::wstring tText = pool.filterTag.empty() ? L"Tags: (None)" : (L"Tags: " + pool.filterTag);
                            DrawModernBtn(hdcMem, rcTagsBtn, tText.c_str(), pState->hoveredControlIdx == idxTags, pState->pressedControlIdx == idxTags, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8EC");
                        }
                    }

                    edY += cardH + 10;
                }

                if (pState->isBpPoolDragging)
                {
                    DrawSolidYellowDropIndicator(hdcMem, pState->rcBpPoolDropIndicator);
                }

                // Bottom Actions: [💾 Save .train Blueprint], [➡️ Switch to Bindings Tab]
                edY += 12;
                RECT rcBtnSave = { editorX, edY, editorX + 210, edY + 36 };
                ClickControl ccSave = { ClickControl::BTN_SAVE_BLUEPRINT, rcBtnSave };
                int idxSave = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccSave);
                DrawModernBtn(hdcMem, rcBtnSave, L"Save .train Blueprint", pState->hoveredControlIdx == idxSave, pState->pressedControlIdx == idxSave, true, pState->hFontBold, pState->hFontIcon, L"\xE74E");

                RECT rcBtnSwitch = { editorX + 224, edY, editorX + 464, edY + 36 };
                ClickControl ccSwitch = { ClickControl::BTN_SWITCH_TO_BINDINGS, rcBtnSwitch };
                int idxSwitch = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccSwitch);
                DrawModernBtn(hdcMem, rcBtnSwitch, L"Configure Stock Bindings ➔", pState->hoveredControlIdx == idxSwitch, pState->pressedControlIdx == idxSwitch, false, pState->hFontBold, pState->hFontIconSmall, L"\xE71D");

                pState->totalEditorH = (edY + pState->editorScrollY) - topY + 60;
            }

            SelectClipRgn(hdcMem, NULL);
            DeleteObject(hRgnEditorClip);
            pState->m_editorScroll.Paint(hdcMem, CLR_INVALID, PoolTheme::GutterBackground);
        }
        else
        {
            // =========================================================================
            // TAB 1: ROLLING STOCK BINDINGS (.bindings)
            // =========================================================================

            pState->ShowEditorFields(false);

            int bindingsTop = topY + 1;
            int bindingsH = h - bindingsTop;

            RECT rcBindingsScroll = { w - 10, bindingsTop + 2, w - 2, h - 2 };
            pState->m_bindingsScroll.SetBounds(rcBindingsScroll);
            pState->m_bindingsScroll.SetRange(0, (std::max)(0, pState->totalBindingsH - 1), bindingsH);
            pState->bindingsScrollY = pState->m_bindingsScroll.GetPos();

            int bndX = editorX;
            int bndW = editorW;
            int bndY = topY + 16 - pState->bindingsScrollY;

            RECT rcBindingsClip = { bndX - 4, topY, w, h };
            HRGN hRgnBindingsClip = CreateRectRgn(rcBindingsClip.left, rcBindingsClip.top, rcBindingsClip.right, rcBindingsClip.bottom);
            SelectClipRgn(hdcMem, hRgnBindingsClip);

            if (pState->selectedConfigIdx < 0 || pState->activeConfig.id.empty())
            {
                SelectObject(hdcMem, pState->hFontIcon);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcIconPlace = { bndX, topY + 130, bndX + bndW, topY + 170 };
                DrawTextW(hdcMem, L"\xE8EC", -1, &rcIconPlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontTitle);
                SetTextColor(hdcMem, StudioTheme::TextSecondary);
                RECT rcTitlePlace = { bndX, topY + 175, bndX + bndW, topY + 205 };
                DrawTextW(hdcMem, L"No Train Blueprint Selected", -1, &rcTitlePlace, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcDescPlace = { bndX + 40, topY + 210, bndX + bndW - 40, topY + 270 };
                DrawTextW(hdcMem, L"Select a train blueprint from the left panel to configure its rolling stock bindings.", -1, &rcDescPlace, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            else
            {
                // Top Blueprint Header Card
                RECT rcTopCard = { bndX, bndY, bndX + bndW, bndY + 76 };
                HBRUSH hbrTop = CreateSolidBrush(StudioTheme::CardBg);
                HPEN hPenTop = CreatePen(PS_SOLID, 1, StudioTheme::CardBorder);
                SelectObject(hdcMem, hbrTop);
                SelectObject(hdcMem, hPenTop);
                RoundRect(hdcMem, rcTopCard.left, rcTopCard.top, rcTopCard.right, rcTopCard.bottom, 8, 8);
                DeleteObject(hbrTop);
                DeleteObject(hPenTop);

                SelectObject(hdcMem, pState->hFontBold);
                SetTextColor(hdcMem, RGB(255, 255, 255));
                RECT rcTopTitle = { bndX + 16, bndY + 10, bndX + bndW - 350, bndY + 30 };
                DrawTextW(hdcMem, (pState->activeConfig.name.empty() ? L"No Blueprint Selected" : pState->activeConfig.name).c_str(), -1, &rcTopTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hdcMem, pState->hFontSmall);
                SetTextColor(hdcMem, StudioTheme::TextSecondary);
                std::wstring topSub = L"Blueprint ID: " + pState->activeConfig.id + L"  •  Category: " + pState->activeConfig.category + L"  •  Type: " + pState->activeConfig.trainType;
                RECT rcTopSub = { bndX + 16, bndY + 32, bndX + bndW - 350, bndY + 50 };
                DrawTextW(hdcMem, topSub.c_str(), -1, &rcTopSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                std::wstring bindPath = TrainConfigManager::GetBindingFilePathForConfig(pState->activeConfig);
                SetTextColor(hdcMem, StudioTheme::TextMuted);
                RECT rcTopPath = { bndX + 16, bndY + 50, bndX + bndW - 350, bndY + 68 };
                DrawTextW(hdcMem, (L"Binding File: " + bindPath).c_str(), -1, &rcTopPath, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);

                // Top Buttons: [✨ Smart Match], [💾 Save Bindings], [📂 Open Folder]
                int tbX = bndX + bndW - 340;
                int tbBtnY = bndY + 22;

                RECT rcSmart = { tbX, tbBtnY, tbX + 130, tbBtnY + 32 };
                ClickControl ccSmart = { ClickControl::BTN_BIND_SMART_MATCH, rcSmart };
                int idxSmart = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccSmart);
                DrawModernBtn(hdcMem, rcSmart, L"Smart Match", pState->hoveredControlIdx == idxSmart, pState->pressedControlIdx == idxSmart, false, pState->hFontBold, pState->hFontIconSmall, L"\xE706");
                tbX += 136;

                RECT rcSaveB = { tbX, tbBtnY, tbX + 140, tbBtnY + 32 };
                ClickControl ccSaveB = { ClickControl::BTN_BIND_SAVE, rcSaveB };
                int idxSaveB = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccSaveB);
                DrawModernBtn(hdcMem, rcSaveB, L"Save Bindings", pState->hoveredControlIdx == idxSaveB, pState->pressedControlIdx == idxSaveB, true, pState->hFontBold, pState->hFontIcon, L"\xE74E");
                tbX += 146;

                RECT rcOpenB = { tbX, tbBtnY, tbX + 38, tbBtnY + 32 };
                ClickControl ccOpenB = { ClickControl::BTN_BIND_OPEN_FOLDER, rcOpenB };
                int idxOpenB = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccOpenB);
                DrawModernBtn(hdcMem, rcOpenB, L"", pState->hoveredControlIdx == idxOpenB, pState->pressedControlIdx == idxOpenB, false, pState->hFontBold, pState->hFontIconSmall, L"\xE838");

                bndY += 92;

                // Pool Stock Allotment Cards
                pState->bindingPoolCardHits.clear();
                pState->bindingUnitChipHits.clear();
                pState->bindingUnitsBoxHits.clear();

                for (size_t p = 0; p < pState->activeConfig.pools.size(); ++p)
                {
                    const auto& pool = pState->activeConfig.pools[p];
                    TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                    int unitCount = pBP ? (int)pBP->units.size() : 0;
                    bool isCollapsed = pState->collapsedBindingPools.count((int)p) > 0;

                    int chipH = 24;
                    int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * (chipH + 4) + 10);
                    int cardH = isCollapsed ? 34 : (44 + unitsAreaH + 10);

                    RECT rcCard = { bndX, bndY, bndX + bndW, bndY + cardH };

                    bool isHoverPool = (pState->dragHoverPoolIdx == (int)p) || (pState->isUnitDragging && pState->unitDropTargetPool == (int)p);
                    COLORREF bg = isHoverPool ? RGB(32, 40, 52) : StudioTheme::CardBg;
                    COLORREF bdr = isHoverPool ? RGB(0, 160, 255) : StudioTheme::CardBorder;

                    HBRUSH hbr = CreateSolidBrush(bg);
                    HPEN hPen = CreatePen(PS_SOLID, isHoverPool ? 2 : 1, bdr);
                    SelectObject(hdcMem, hbr);
                    SelectObject(hdcMem, hPen);
                    RoundRect(hdcMem, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);
                    DeleteObject(hbr);
                    DeleteObject(hPen);

                    // Register BindingPoolCardHit
                    StudioDlgState::BindingPoolCardHit bch;
                    bch.poolIdx = (int)p;
                    bch.rcCard = rcCard;
                    bch.rcHeader = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 34 };
                    pState->bindingPoolCardHits.push_back(bch);

                    // 1. Collapse Chevron Button
                    RECT rcChevronBtn = { rcCard.left + 8, rcCard.top + 5, rcCard.left + 30, rcCard.top + 29 };
                    ClickControl ccColToggle = { ClickControl::BTN_BIND_POOL_COLLAPSE_TOGGLE, rcChevronBtn, (int)p, -1 };
                    int idxCol = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccColToggle);
                    DrawModernBtn(hdcMem, rcChevronBtn, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontSmall, pState->hFontIconSmall, isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // 2. Pool Number Badge & Name: e.g. "#1  DTC-FRONT"
                    std::wstring pNameText = L"#" + std::to_wstring(p + 1) + L"  " + pool.poolName;
                    SIZE szPName = { 0 };
                    SelectObject(hdcMem, pState->hFontBold);
                    GetTextExtentPoint32W(hdcMem, pNameText.c_str(), (int)pNameText.length(), &szPName);

                    int pNameLeft = rcCard.left + 36;
                    int maxPNameW = rcCard.right - 580 - pNameLeft;
                    int pNameW = szPName.cx;
                    if (maxPNameW > 60 && pNameW > maxPNameW) pNameW = maxPNameW;

                    RECT rcPName = { pNameLeft, rcCard.top, pNameLeft + pNameW, rcCard.top + 34 };
                    SetTextColor(hdcMem, RGB(255, 255, 255));
                    DrawTextW(hdcMem, pNameText.c_str(), -1, &rcPName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // 3. Header Action Buttons on Right: [Flip Policy], [Mode], [Copy], [Paste], [Clear All], Unit Count text
                    int hBtnR = rcCard.right - 8;
                    int hBtnY = rcCard.top + 5;
                    int hBtnH = 24;

                    // [Clear All]
                    hBtnR -= 74;
                    RECT rcClearBtn = { hBtnR, hBtnY, hBtnR + 70, hBtnY + hBtnH };
                    ClickControl ccClear = { ClickControl::BTN_BIND_POOL_CLEAR_UNITS, rcClearBtn, (int)p, -1 };
                    int idxClear = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccClear);
                    DrawModernBtn(hdcMem, rcClearBtn, L"Clear", pState->hoveredControlIdx == idxClear, pState->pressedControlIdx == idxClear, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    // [Paste]
                    hBtnR -= 70;
                    RECT rcPasteBtn = { hBtnR, hBtnY, hBtnR + 66, hBtnY + hBtnH };
                    ClickControl ccPaste = { ClickControl::BTN_BIND_POOL_PASTE_UNITS, rcPasteBtn, (int)p, -1 };
                    int idxPaste = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPaste);
                    DrawModernBtn(hdcMem, rcPasteBtn, L"Paste", pState->hoveredControlIdx == idxPaste, pState->pressedControlIdx == idxPaste, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");

                    // [Copy]
                    hBtnR -= 68;
                    RECT rcCopyBtn = { hBtnR, hBtnY, hBtnR + 64, hBtnY + hBtnH };
                    ClickControl ccCopy = { ClickControl::BTN_BIND_POOL_COPY_UNITS, rcCopyBtn, (int)p, -1 };
                    int idxCopy = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccCopy);
                    DrawModernBtn(hdcMem, rcCopyBtn, L"Copy", pState->hoveredControlIdx == idxCopy, pState->pressedControlIdx == idxCopy, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                    // [Pick Mode]
                    hBtnR -= 114;
                    RECT rcBndMode = { hBtnR, hBtnY, hBtnR + 110, hBtnY + hBtnH };
                    ClickControl ccBndMode = { ClickControl::BTN_BIND_POOL_MODE_TOGGLE, rcBndMode, (int)p, -1 };
                    int idxBndMode = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccBndMode);
                    PoolManager::PoolPickMode pMode = pBP ? pBP->pickMode : PoolManager::PoolPickMode::Random;
                    const wchar_t* bndModeStr = (pMode == PoolManager::PoolPickMode::Random) ? L"Mode: Random" : L"Mode: Order";
                    const wchar_t* bndModeIcon = (pMode == PoolManager::PoolPickMode::Random) ? L"\xE8B9" : L"\xE8D7";
                    DrawModernBtn(hdcMem, rcBndMode, bndModeStr, pState->hoveredControlIdx == idxBndMode, pState->pressedControlIdx == idxBndMode, false, pState->hFontSmall, pState->hFontIconSmall, bndModeIcon);

                    // [Flip Policy]
                    hBtnR -= 120;
                    RECT rcBndFlip = { hBtnR, hBtnY, hBtnR + 116, hBtnY + hBtnH };
                    ClickControl ccBndFlip = { ClickControl::BTN_BIND_POOL_FLIP_TOGGLE, rcBndFlip, (int)p, -1 };
                    int idxBndFlip = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccBndFlip);
                    PoolManager::PoolFlipPolicy pPolicy = pBP ? pBP->flipPolicy : PoolManager::PoolFlipPolicy::ForwardOnly;
                    const wchar_t* flipText = L"Flip: Forward";
                    const wchar_t* flipIcon = L"\xE72B";
                    if (pPolicy == PoolManager::PoolFlipPolicy::AllowRandom) {
                        flipText = L"Flip: Random";
                        flipIcon = L"\xE745";
                    }
                    else if (pPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped) {
                        flipText = L"Flip: Reversed";
                        flipIcon = L"\xE72A";
                    }
                    DrawModernBtn(hdcMem, rcBndFlip, flipText, pState->hoveredControlIdx == idxBndFlip, pState->pressedControlIdx == idxBndFlip, false, pState->hFontSmall, pState->hFontIconSmall, flipIcon);

                    // Unit Count & Missing badge
                    int missingCount = 0;
                    if (pBP)
                    {
                        for (const auto& u : pBP->units)
                        {
                            if (u.szFolder.empty()) missingCount++;
                        }
                    }

                    SelectObject(hdcMem, pState->hFontSmall);
                    if (missingCount > 0)
                    {
                        SetTextColor(hdcMem, RGB(255, 110, 110));
                        std::wstring cntStr = std::to_wstring(unitCount) + L" allocated (" + std::to_wstring(missingCount) + L" missing \x26A0)";
                        RECT rcCount = { hBtnR - 200, rcCard.top, hBtnR - 6, rcCard.top + 34 };
                        DrawTextW(hdcMem, cntStr.c_str(), -1, &rcCount, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }
                    else
                    {
                        SetTextColor(hdcMem, StudioTheme::TextMuted);
                        RECT rcCount = { hBtnR - 92, rcCard.top, hBtnR - 6, rcCard.top + 34 };
                        std::wstring cntStr = std::to_wstring(unitCount) + L" allocated";
                        DrawTextW(hdcMem, cntStr.c_str(), -1, &rcCount, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Role & Tags Subtitle
                    int roleLeft = rcPName.right + 12;
                    SetTextColor(hdcMem, RGB(96, 205, 255));
                    RECT rcRole = { roleLeft, rcCard.top, hBtnR - 206, rcCard.top + 34 };
                    std::wstring rTextP = L"Role: " + pool.role + L" • Tags: " + (pool.filterTag.empty() ? L"(Any)" : pool.filterTag);
                    DrawTextW(hdcMem, rTextP.c_str(), -1, &rcRole, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    if (!isCollapsed)
                    {
                        // Units Box Area (matching PoolManagerDlg style)
                        int boxY = rcCard.top + 38;
                        RECT rcUnitsBox = { rcCard.left + 14, boxY, rcCard.right - 14, boxY + unitsAreaH };

                        HBRUSH hbrUBox = CreateSolidBrush(PoolTheme::UnitsBoxBackground);
                        HPEN hpenUBox = CreatePen(PS_SOLID, 1, PoolTheme::UnitsBoxBorder);
                        SelectObject(hdcMem, hbrUBox);
                        SelectObject(hdcMem, hpenUBox);
                        RoundRect(hdcMem, rcUnitsBox.left, rcUnitsBox.top, rcUnitsBox.right, rcUnitsBox.bottom, 6, 6);
                        DeleteObject(hbrUBox);
                        DeleteObject(hpenUBox);

                        StudioDlgState::BindingUnitsBoxHit ubh;
                        ubh.poolIdx = (int)p;
                        ubh.rcUnitsBox = rcUnitsBox;
                        pState->bindingUnitsBoxHits.push_back(ubh);

                        if (unitCount == 0)
                        {
                            SelectObject(hdcMem, pState->hFontSmall);
                            SetTextColor(hdcMem, PoolTheme::TextMuted);
                            DrawTextW(hdcMem, L"Drag and drop rolling stock units here from Stock Library or click Smart Match above", -1, &rcUnitsBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }
                        else
                        {
                            int colTotalW = (rcUnitsBox.right - rcUnitsBox.left - 24) / 2;
                            int numW = 20;
                            for (int u = 0; u < unitCount; ++u)
                            {
                                const auto& unit = pBP->units[u];
                                int col = u % 2;
                                int row = u / 2;
                                int colX = rcUnitsBox.left + 8 + col * (colTotalW + 8);
                                int chipY = rcUnitsBox.top + 8 + row * (chipH + 4);

                                // Number label outside card towards left
                                RECT rcNum = { colX, chipY, colX + numW - 4, chipY + chipH };
                                SelectObject(hdcMem, pState->hFontBadge ? pState->hFontBadge : pState->hFontSmall);
                                SetTextColor(hdcMem, RGB(255, 255, 255));
                                std::wstring numStr = std::to_wstring(u + 1) + L".";
                                DrawTextW(hdcMem, numStr.c_str(), -1, &rcNum, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                int chipX = colX + numW;
                                int chipW = colTotalW - numW;
                                RECT rcChip = { chipX, chipY, chipX + chipW, chipY + chipH };

                                StudioDlgState::BindingUnitChipHit uch;
                                uch.poolIdx = (int)p;
                                uch.unitIdx = u;
                                uch.rcChip = { colX, chipY, chipX + chipW, chipY + chipH };
                                pState->bindingUnitChipHits.push_back(uch);

                                bool isMissing = unit.szFolder.empty();
                                COLORREF chipBg = isMissing ? RGB(55, 20, 20) : PoolTheme::ChipBackground;
                                COLORREF chipBdr = isMissing ? RGB(200, 50, 50) : PoolTheme::ChipBorder;

                                HBRUSH hbrChip = CreateSolidBrush(chipBg);
                                HPEN hpenChip = CreatePen(PS_SOLID, 1, chipBdr);
                                SelectObject(hdcMem, hbrChip);
                                SelectObject(hdcMem, hpenChip);
                                RoundRect(hdcMem, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                                DeleteObject(hbrChip);
                                DeleteObject(hpenChip);

                                // Unit Icon
                                SelectObject(hdcMem, pState->hFontIconSmall);
                                if (isMissing)
                                {
                                    SetTextColor(hdcMem, RGB(255, 90, 90));
                                    RECT rcUIcon = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                    DrawTextW(hdcMem, L"\xE7BA", -1, &rcUIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                }
                                else
                                {
                                    SetTextColor(hdcMem, unit.isEngine ? RGB(96, 205, 255) : RGB(220, 180, 100));
                                    RECT rcUIcon = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                    DrawTextW(hdcMem, unit.isEngine ? L"\xE7C0" : L"\xE707", -1, &rcUIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                }

                                // Unit Name
                                SelectObject(hdcMem, pState->hFontSmall);
                                SetTextColor(hdcMem, isMissing ? RGB(255, 120, 120) : RGB(245, 245, 245));
                                std::wstring uDisplayName = isMissing ? (unit.szFileName + L" (Missing)") : unit.szFileName;
                                RECT rcUName = { rcChip.left + 24, rcChip.top, rcChip.right - 80, rcChip.bottom };
                                DrawTextW(hdcMem, uDisplayName.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                // Unit Flip Mode Pill
                                RECT rcFlipPill = { rcChip.right - 76, rcChip.top + 2, rcChip.right - 24, rcChip.bottom - 2 };
                                ClickControl ccUFlip = { ClickControl::BTN_UNIT_FLIP_TOGGLE, rcFlipPill, (int)p, u };
                                int idxUFlip = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccUFlip);
                                const wchar_t* ufStr = L"Auto";
                                if (unit.flipMode == PoolManager::UnitFlipMode::Forward) ufStr = L"▲ FWD";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Flipped) ufStr = L"▼ FLIP";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Random) ufStr = L"🔀 RND";
                                DrawModernBtn(hdcMem, rcFlipPill, ufStr, pState->hoveredControlIdx == idxUFlip, pState->pressedControlIdx == idxUFlip, false, pState->hFontBadge ? pState->hFontBadge : pState->hFontSmall);

                                // Unit Remove [✕]
                                RECT rcRem = { rcChip.right - 22, rcChip.top + 2, rcChip.right - 4, rcChip.bottom - 2 };
                                ClickControl ccRem = { ClickControl::BTN_UNIT_REMOVE, rcRem, (int)p, u };
                                int idxRem = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccRem);
                                DrawModernBtn(hdcMem, rcRem, L"✕", pState->hoveredControlIdx == idxRem, pState->pressedControlIdx == idxRem, false, pState->hFontBadge ? pState->hFontBadge : pState->hFontSmall);
                            }
                        }
                    }

                    bndY += cardH + 14;
                }

                if (pState->isUnitDragging && pState->unitDropTargetPool >= 0 && pState->unitDropTargetUnitIdx >= 0)
                {
                    DrawSolidYellowDropIndicator(hdcMem, pState->rcUnitDropIndicator);
                }

                pState->totalBindingsH = (bndY + pState->bindingsScrollY) - topY + 60;
            }

            SelectClipRgn(hdcMem, NULL);
            DeleteObject(hRgnBindingsClip);
            pState->m_bindingsScroll.Paint(hdcMem, CLR_INVALID, PoolTheme::GutterBackground);
        }

        // 1px App-Drawn Perimeter Border (seamlessly aligned with CustomTitleBar)
        if (!IsZoomed(hWnd))
        {
            COLORREF winBorderCol = RGB(78, 32, 38);
            HPEN hPenWin = CreatePen(PS_SOLID, 1, winBorderCol);
            HPEN hOldWinPen = (HPEN)SelectObject(hdcMem, hPenWin);

            // Left border (continues from title bar Y = 66)
            MoveToEx(hdcMem, 0, 66, NULL);
            LineTo(hdcMem, 0, h);

            // Right border (continues from title bar Y = 66)
            MoveToEx(hdcMem, w - 1, 66, NULL);
            LineTo(hdcMem, w - 1, h);

            // Bottom border
            MoveToEx(hdcMem, 0, h - 1, NULL);
            LineTo(hdcMem, w, h - 1);

            SelectObject(hdcMem, hOldWinPen);
            DeleteObject(hPenWin);
        }

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, holdBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (pState && pState->activeTab == 0)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (pState->isDraggingSplitter || PtInRect(&pState->rcSplitter, pt))
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // 0. Check splitter dragging
        if (pState->activeTab == 0 && pState->isDraggingSplitter)
        {
            int deltaX = pt.x - pState->dragSplitterStartX;
            int newW = pState->dragSidebarStartW + deltaX;
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int minW = 280;
            int maxW = (std::max<int>)(280, (int)rcClient.right - 460);
            if (newW < minW) newW = minW;
            if (newW > maxW) newW = maxW;

            if (newW != pState->sidebarWidth)
            {
                pState->sidebarWidth = newW;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return 0;
        }

        if (PtInRect(&pState->rcSplitter, pt))
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }

        // 0.5. Route mouse move to CustomScrollBars
        if (pState)
        {
            bool rep1 = pState->m_sidebarScroll.OnMouseMove(pt, hWnd);
            bool rep2 = (pState->activeTab == 0) ? pState->m_editorScroll.OnMouseMove(pt, hWnd) : pState->m_bindingsScroll.OnMouseMove(pt, hWnd);
            if (rep1 || rep2)
            {
                pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
                if (pState->activeTab == 0) pState->editorScrollY = pState->m_editorScroll.GetPos();
                else pState->bindingsScrollY = pState->m_bindingsScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }

        // 1. Check if potential category drag crossed the 4px threshold
        if (pState->isPotentialCatDrag && !pState->isCatDragging)
        {
            if (abs(pt.x - pState->ptCatDragStart.x) > 4 || abs(pt.y - pState->ptCatDragStart.y) > 4)
            {
                pState->isCatDragging = true;
                pState->draggingCatIdx = pState->potentialCatDragIdx;
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));

                std::vector<DragGhostItem> ghostItems;
                if (pState->draggingCatIdx >= 0 && pState->draggingCatIdx < (int)pState->categoryOrder.size())
                {
                    const auto& cat = pState->categoryOrder[pState->draggingCatIdx];
                    int catCount = 0;
                    for (const auto& cfg : TrainConfigManager::g_LoadedConfigsCache) { if (cfg.category == cat) catCount++; }
                    DragGhostItem item;
                    item.type = GhostItemType::PoolCard;
                    item.name = L"#" + std::to_wstring(pState->draggingCatIdx + 1) + L"  " + cat;
                    item.subtitle = std::to_wstring(catCount) + L" train blueprints";
                    item.isEngine = true;
                    ghostItems.push_back(item);
                }

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);
                FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
            }
        }

        // 2. Active category dragging motion & drop targeting
        if (pState->activeTab == 0 && pState->isCatDragging)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            POINT ptScreen = pt;
            ClientToScreen(hWnd, &ptScreen);

            int targetSlot = -1;
            RECT rcInd = { 0, 0, 0, 0 };

            if (!pState->categoryCardHits.empty())
            {
                int cardCount = (int)pState->categoryCardHits.size();
                for (int i = 0; i < cardCount; ++i)
                {
                    const auto& ch = pState->categoryCardHits[i];
                    int midY = (ch.rcCard.top + ch.rcCard.bottom) / 2;
                    if (pt.y < midY)
                    {
                        targetSlot = i;
                        int indY = ch.rcCard.top - 6;
                        rcInd = { ch.rcCard.left, indY, ch.rcCard.right, indY + 4 };
                        break;
                    }
                }

                if (targetSlot == -1)
                {
                    targetSlot = cardCount;
                    const auto& lastCh = pState->categoryCardHits.back();
                    int indY = lastCh.rcCard.bottom + 4;
                    rcInd = { lastCh.rcCard.left, indY, lastCh.rcCard.right, indY + 4 };
                }
            }

            pState->catDropTargetIdx = targetSlot;
            pState->rcCatDropIndicator = rcInd;

            FluentDragGhost::Move(ptScreen, true, L"Reorder Category Folder");
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // 3. Check start of blueprint pool drag
        if (pState->activeTab == 0 && pState->isPotentialBpPoolDrag && !pState->isBpPoolDragging)
        {
            if (abs(pt.x - pState->ptBpPoolDragStart.x) > 4 || abs(pt.y - pState->ptBpPoolDragStart.y) > 4)
            {
                pState->isBpPoolDragging = true;
                pState->draggingBpPoolIdx = pState->potentialBpPoolDragIdx;
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));

                std::vector<DragGhostItem> ghostItems;
                if (pState->draggingBpPoolIdx >= 0 && pState->draggingBpPoolIdx < (int)pState->activeConfig.pools.size())
                {
                    const auto& pool = pState->activeConfig.pools[pState->draggingBpPoolIdx];
                    DragGhostItem item;
                    item.type = GhostItemType::PoolCard;
                    item.name = L"#" + std::to_wstring(pState->draggingBpPoolIdx + 1) + L"  " + pool.poolName;
                    item.subtitle = pool.role + L" (" + std::to_wstring(pool.minCount) + L"-" + std::to_wstring(pool.maxCount) + L")";
                    item.isEngine = (pool.unitType == L"Electric" || pool.unitType == L"Diesel" || pool.unitType == L"Steam");
                    ghostItems.push_back(item);
                }

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);
                FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
            }
        }

        // 4. Active blueprint pool dragging motion & drop targeting
        if (pState->activeTab == 0 && pState->isBpPoolDragging)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            POINT ptScreen = pt;
            ClientToScreen(hWnd, &ptScreen);

            int targetSlot = -1;
            RECT rcInd = { 0, 0, 0, 0 };

            if (!pState->bpPoolCardHits.empty())
            {
                int cardCount = (int)pState->bpPoolCardHits.size();
                for (int i = 0; i < cardCount; ++i)
                {
                    const auto& ch = pState->bpPoolCardHits[i];
                    int midY = (ch.rcCard.top + ch.rcCard.bottom) / 2;
                    if (pt.y < midY)
                    {
                        targetSlot = i;
                        int indY = ch.rcCard.top - 6;
                        rcInd = { ch.rcCard.left, indY, ch.rcCard.right, indY + 4 };
                        break;
                    }
                }

                if (targetSlot == -1)
                {
                    targetSlot = cardCount;
                    const auto& lastCh = pState->bpPoolCardHits.back();
                    int indY = lastCh.rcCard.bottom + 4;
                    rcInd = { lastCh.rcCard.left, indY, lastCh.rcCard.right, indY + 4 };
                }
            }

            pState->bpPoolDropTargetIdx = targetSlot;
            pState->rcBpPoolDropIndicator = rcInd;

            FluentDragGhost::Move(ptScreen, true, L"Reorder Pool Sequence");
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // 5. Check start of Tab 1 binding unit drag
        if (pState->activeTab == 1 && pState->isPotentialUnitDrag && !pState->isUnitDragging)
        {
            if (abs(pt.x - pState->ptUnitDragStart.x) > 4 || abs(pt.y - pState->ptUnitDragStart.y) > 4)
            {
                pState->isUnitDragging = true;
                SetCapture(hWnd);
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));

                std::vector<DragGhostItem> ghostItems;
                if (pState->unitDragSourcePool >= 0 && pState->unitDragSourcePool < (int)pState->activeConfig.pools.size())
                {
                    const auto& bp = pState->activeConfig.pools[pState->unitDragSourcePool];
                    TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, pState->unitDragSourcePool, bp.poolName);
                    if (pBP && pState->unitDragSourceUnitIdx >= 0 && pState->unitDragSourceUnitIdx < (int)pBP->units.size())
                    {
                        const auto& u = pBP->units[pState->unitDragSourceUnitIdx];
                        DragGhostItem item;
                        item.name = u.szFileName;
                        item.subtitle = u.szFolder.empty() ? bp.poolName : u.szFolder;
                        item.isEngine = u.isEngine;
                        ghostItems.push_back(item);
                    }
                }

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);
                FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
            }
        }

        // 6. Active Tab 1 binding unit dragging motion & drop targeting
        if (pState->activeTab == 1 && pState->isUnitDragging)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            POINT ptScreen = pt;
            ClientToScreen(hWnd, &ptScreen);

            int hitPool = -1;
            for (const auto& bch : pState->bindingPoolCardHits)
            {
                if (PtInRect(&bch.rcCard, pt))
                {
                    hitPool = bch.poolIdx;
                    break;
                }
            }

            if (hitPool != -1 && hitPool < (int)pState->activeConfig.pools.size())
            {
                RECT rcInd = { 0 };
                int dropIdx = GetBindingUnitDropTarget(pState, hitPool, pt, rcInd);
                pState->unitDropTargetPool = hitPool;
                pState->unitDropTargetUnitIdx = dropIdx;
                pState->rcUnitDropIndicator = rcInd;

                bool isSame = (pState->unitDragSourcePool == hitPool);
                const auto& pool = pState->activeConfig.pools[hitPool];
                std::wstring actionText = isSame ? L"Reorder in " + pool.poolName : L"Move to " + pool.poolName;
                FluentDragGhost::Move(ptScreen, true, actionText);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            else
            {
                pState->unitDropTargetPool = -1;
                pState->unitDropTargetUnitIdx = -1;
                pState->rcUnitDropIndicator = { 0, 0, 0, 0 };
                FluentDragGhost::Move(ptScreen, false);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        TRACKMOUSEEVENT tme = { 0 };
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int h = rcClient.bottom;

        int newHover = -1;
        for (size_t i = 0; i < pState->clickControls.size(); ++i)
        {
            const auto& ctrl = pState->clickControls[i];
            if (PtInRect(&ctrl.rc, pt))
            {
                if (ctrl.rc.bottom > 112 && ctrl.rc.top < h)
                {
                    newHover = (int)i;
                    break;
                }
            }
        }

        if (newHover != pState->hoveredControlIdx)
        {
            pState->hoveredControlIdx = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            pState->m_sidebarScroll.OnMouseLeave(hWnd);
            if (pState->activeTab == 0)
            {
                pState->m_editorScroll.OnMouseLeave(hWnd);
            }
            else
            {
                pState->m_bindingsScroll.OnMouseLeave(hWnd);
            }
            pState->hoveredControlIdx = -1;
            if (pState->repeatAction != ClickControl::NONE)
            {
                KillTimer(hWnd, TIMER_STUDIO_REPEAT_ID);
                if (GetCapture() == hWnd) ReleaseCapture();
                pState->repeatAction = ClickControl::NONE;
                pState->repeatPoolIdx = -1;
                pState->repeatHoldCount = 0;
            }
            pState->pressedControlIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == CustomScrollBar::TIMER_ANIM_ID && pState)
        {
            bool update = false;
            update |= pState->m_sidebarScroll.OnTimer(hWnd);
            if (pState->activeTab == 0)
            {
                update |= pState->m_editorScroll.OnTimer(hWnd);
            }
            else
            {
                update |= pState->m_bindingsScroll.OnTimer(hWnd);
            }
            if (update)
            {
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        if (wParam == TIMER_STUDIO_REPEAT_ID && pState && pState->repeatAction != ClickControl::NONE)
        {
            pState->repeatHoldCount++;
            int step = 1;
            if (pState->repeatHoldCount > 15) step = 10;
            else if (pState->repeatHoldCount > 8) step = 5;

            ExecuteStudioStepAction(pState, pState->repeatAction, pState->repeatPoolIdx, step);
            SetTimer(hWnd, TIMER_STUDIO_REPEAT_ID, 60, NULL);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int h = rcClient.bottom;

        // CustomScrollBar hit tests
        if (pState)
        {
            if (pState->m_sidebarScroll.OnLButtonDown(pt, hWnd))
            {
                pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (pState->activeTab == 0)
            {
                if (pState->m_editorScroll.OnLButtonDown(pt, hWnd))
                {
                    pState->editorScrollY = pState->m_editorScroll.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            else
            {
                if (pState->m_bindingsScroll.OnLButtonDown(pt, hWnd))
                {
                    pState->bindingsScrollY = pState->m_bindingsScroll.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        // Splitter initiation
        if (PtInRect(&pState->rcSplitter, pt))
        {
            pState->isDraggingSplitter = true;
            pState->dragSplitterStartX = pt.x;
            pState->dragSidebarStartW = pState->sidebarWidth;
            SetCapture(hWnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return 0;
        }

        pState->pressedControlIdx = -1;
        for (size_t i = 0; i < pState->clickControls.size(); ++i)
        {
            const auto& ctrl = pState->clickControls[i];
            if (PtInRect(&ctrl.rc, pt))
            {
                if (ctrl.rc.bottom > 112 && ctrl.rc.top < h)
                {
                    pState->pressedControlIdx = (int)i;
                    break;
                }
            }
        }

        if (pState->pressedControlIdx >= 0)
        {
            const auto& ctrl = pState->clickControls[pState->pressedControlIdx];
            if (ctrl.type == ClickControl::BTN_POOL_MIN_DEC ||
                ctrl.type == ClickControl::BTN_POOL_MIN_INC ||
                ctrl.type == ClickControl::BTN_POOL_MAX_DEC ||
                ctrl.type == ClickControl::BTN_POOL_MAX_INC ||
                ctrl.type == ClickControl::BTN_TOTAL_MIN_DEC ||
                ctrl.type == ClickControl::BTN_TOTAL_MIN_INC ||
                ctrl.type == ClickControl::BTN_TOTAL_MAX_DEC ||
                ctrl.type == ClickControl::BTN_TOTAL_MAX_INC)
            {
                pState->repeatAction = ctrl.type;
                pState->repeatPoolIdx = ctrl.index;
                pState->repeatHoldCount = 0;
                ExecuteStudioStepAction(pState, ctrl.type, ctrl.index, 1);
                SetCapture(hWnd);
                SetTimer(hWnd, TIMER_STUDIO_REPEAT_ID, 320, NULL);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // Check potential drag on category card header
        if (pt.x < pState->sidebarWidth)
        {
            bool hitChildBtn = false;
            if (pState->pressedControlIdx >= 0)
            {
                auto t = pState->clickControls[pState->pressedControlIdx].type;
                if (t == ClickControl::BTN_CAT_RENAME || t == ClickControl::BTN_CAT_CLONE ||
                    t == ClickControl::BTN_CAT_UP || t == ClickControl::BTN_CAT_DOWN ||
                    t == ClickControl::BTN_CAT_DELETE || t == ClickControl::BTN_CAT_COLLAPSE_TOGGLE)
                {
                    hitChildBtn = true;
                }
            }

            if (!hitChildBtn)
            {
                for (const auto& cch : pState->categoryCardHits)
                {
                    if (PtInRect(&cch.rcHeader, pt))
                    {
                        pState->isPotentialCatDrag = true;
                        pState->potentialCatDragIdx = cch.catIdx;
                        pState->ptCatDragStart = pt;
                        break;
                    }
                }
            }
        }

        // Check potential drag on Blueprint Pool card header
        if (pState->activeTab == 0 && pt.x >= pState->sidebarWidth + 6)
        {
            bool hitChildBtn = false;
            if (pState->pressedControlIdx >= 0)
            {
                auto t = pState->clickControls[pState->pressedControlIdx].type;
                if (t == ClickControl::BTN_POOL_RENAME || t == ClickControl::BTN_POOL_CLONE ||
                    t == ClickControl::BTN_POOL_UP || t == ClickControl::BTN_POOL_DOWN ||
                    t == ClickControl::BTN_POOL_DELETE || t == ClickControl::BTN_POOL_COLLAPSE_TOGGLE ||
                    t == ClickControl::BTN_POOL_MIN_DEC || t == ClickControl::BTN_POOL_MIN_INC ||
                    t == ClickControl::BTN_POOL_MIN_EDIT || t == ClickControl::BTN_POOL_MAX_DEC ||
                    t == ClickControl::BTN_POOL_MAX_INC || t == ClickControl::BTN_POOL_MAX_EDIT ||
                    t == ClickControl::BTN_POOL_FLIP_TOGGLE || t == ClickControl::BTN_POOL_EDIT ||
                    t == ClickControl::BTN_POOL_ROLE_PICK || t == ClickControl::BTN_POOL_MODE_TOGGLE)
                {
                    hitChildBtn = true;
                }
            }

            if (!hitChildBtn)
            {
                for (const auto& bch : pState->bpPoolCardHits)
                {
                    if (PtInRect(&bch.rcHeader, pt))
                    {
                        pState->isPotentialBpPoolDrag = true;
                        pState->potentialBpPoolDragIdx = bch.poolIdx;
                        pState->ptBpPoolDragStart = pt;
                        break;
                    }
                }
            }
        }

        // Check potential drag on Binding Unit Chips (Tab 1)
        if (pState->activeTab == 1)
        {
            bool hitChildBtn = false;
            if (pState->pressedControlIdx >= 0)
            {
                auto t = pState->clickControls[pState->pressedControlIdx].type;
                if (t == ClickControl::BTN_UNIT_REMOVE || t == ClickControl::BTN_UNIT_FLIP_TOGGLE ||
                    t == ClickControl::BTN_BIND_POOL_COLLAPSE_TOGGLE || t == ClickControl::BTN_BIND_POOL_MODE_TOGGLE ||
                    t == ClickControl::BTN_BIND_POOL_CLEAR_UNITS || t == ClickControl::BTN_BIND_SMART_MATCH ||
                    t == ClickControl::BTN_BIND_SAVE || t == ClickControl::BTN_BIND_OPEN_FOLDER)
                {
                    hitChildBtn = true;
                }
            }

            if (!hitChildBtn)
            {
                for (const auto& uch : pState->bindingUnitChipHits)
                {
                    if (PtInRect(&uch.rcChip, pt))
                    {
                        pState->isPotentialUnitDrag = true;
                        pState->isUnitDragging = false;
                        pState->ptUnitDragStart = pt;
                        pState->unitDragSourcePool = uch.poolIdx;
                        pState->unitDragSourceUnitIdx = uch.unitIdx;
                        break;
                    }
                }
            }
        }

        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (pState)
        {
            bool rep1 = pState->m_sidebarScroll.OnLButtonUp(pt, hWnd);
            bool rep2 = (pState->activeTab == 0) ? pState->m_editorScroll.OnLButtonUp(pt, hWnd) : pState->m_bindingsScroll.OnLButtonUp(pt, hWnd);
            if (rep1 || rep2)
            {
                pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
                if (pState->activeTab == 0) pState->editorScrollY = pState->m_editorScroll.GetPos();
                else pState->bindingsScrollY = pState->m_bindingsScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }

        if (pState->isDraggingSplitter)
        {
            pState->isDraggingSplitter = false;
            if (GetCapture() == hWnd) ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->repeatAction != ClickControl::NONE)
        {
            KillTimer(hWnd, TIMER_STUDIO_REPEAT_ID);
            if (GetCapture() == hWnd) ReleaseCapture();
            pState->repeatAction = ClickControl::NONE;
            pState->repeatPoolIdx = -1;
            pState->repeatHoldCount = 0;
            pState->pressedControlIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (pState->activeTab == 0 && pState->isCatDragging)
        {
            pState->isCatDragging = false;
            pState->isPotentialCatDrag = false;
            FluentDragGhost::Hide();
            SetCursor(LoadCursor(NULL, IDC_ARROW));

            int src = pState->draggingCatIdx;
            int dst = pState->catDropTargetIdx;

            if (src >= 0 && src < (int)pState->categoryOrder.size() && dst >= 0 && dst <= (int)pState->categoryOrder.size())
            {
                if (dst > src) dst--;
                if (src != dst)
                {
                    std::wstring item = pState->categoryOrder[src];
                    pState->categoryOrder.erase(pState->categoryOrder.begin() + src);
                    pState->categoryOrder.insert(pState->categoryOrder.begin() + dst, item);
                }
            }

            pState->draggingCatIdx = -1;
            pState->catDropTargetIdx = -1;
            pState->rcCatDropIndicator = { 0, 0, 0, 0 };
            pState->pressedControlIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        pState->isPotentialCatDrag = false;

        if (pState->activeTab == 0 && pState->isBpPoolDragging)
        {
            pState->isBpPoolDragging = false;
            pState->isPotentialBpPoolDrag = false;
            FluentDragGhost::Hide();
            SetCursor(LoadCursor(NULL, IDC_ARROW));

            int src = pState->draggingBpPoolIdx;
            int dst = pState->bpPoolDropTargetIdx;

            if (src >= 0 && src < (int)pState->activeConfig.pools.size() && dst >= 0 && dst <= (int)pState->activeConfig.pools.size())
            {
                if (dst > src) dst--;
                if (src != dst)
                {
                    auto item = pState->activeConfig.pools[src];
                    pState->activeConfig.pools.erase(pState->activeConfig.pools.begin() + src);
                    pState->activeConfig.pools.insert(pState->activeConfig.pools.begin() + dst, item);

                    pState->activeConfig.sequence.clear();
                    for (const auto& p : pState->activeConfig.pools) {
                        pState->activeConfig.sequence.push_back(p.poolName);
                    }

                    if (src < (int)pState->activeBinding.pools.size())
                    {
                        auto bItem = pState->activeBinding.pools[src];
                        pState->activeBinding.pools.erase(pState->activeBinding.pools.begin() + src);
                        int finalDstB = (std::min<int>)(dst, (int)pState->activeBinding.pools.size());
                        pState->activeBinding.pools.insert(pState->activeBinding.pools.begin() + finalDstB, bItem);
                        pState->isDirtyBindings = true;
                    }
                }
            }

            pState->draggingBpPoolIdx = -1;
            pState->bpPoolDropTargetIdx = -1;
            pState->rcBpPoolDropIndicator = { 0, 0, 0, 0 };
            pState->pressedControlIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        pState->isPotentialBpPoolDrag = false;

        // Handle Active Unit Drag Drop (Tab 1)
        if (pState->activeTab == 1 && pState->isUnitDragging)
        {
            int srcPool = pState->unitDragSourcePool;
            int srcUnit = pState->unitDragSourceUnitIdx;
            int dstPool = pState->unitDropTargetPool;
            int dstUnit = pState->unitDropTargetUnitIdx;

            pState->isPotentialUnitDrag = false;
            pState->isUnitDragging = false;
            pState->unitDragSourcePool = -1;
            pState->unitDragSourceUnitIdx = -1;
            pState->unitDropTargetPool = -1;
            pState->unitDropTargetUnitIdx = -1;
            pState->rcUnitDropIndicator = { 0, 0, 0, 0 };

            FluentDragGhost::Hide();
            if (GetCapture() == hWnd) ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));

            if (srcPool >= 0 && srcPool < (int)pState->activeConfig.pools.size() &&
                dstPool >= 0 && dstPool < (int)pState->activeConfig.pools.size() &&
                dstUnit >= 0)
            {
                const auto& srcBPName = pState->activeConfig.pools[srcPool].poolName;
                const auto& dstBPName = pState->activeConfig.pools[dstPool].poolName;

                auto* pSrcBP = GetBindingPoolByIndex(pState->activeBinding, srcPool, srcBPName);
                auto* pDstBP = GetBindingPoolByIndex(pState->activeBinding, dstPool, dstBPName);

                if (pSrcBP && pDstBP && srcUnit >= 0 && srcUnit < (int)pSrcBP->units.size())
                {
                    auto movingUnit = pSrcBP->units[srcUnit];
                    pSrcBP->units.erase(pSrcBP->units.begin() + srcUnit);

                    int finalDst = dstUnit;
                    if (srcPool == dstPool && srcUnit < dstUnit)
                    {
                        finalDst--;
                    }
                    if (finalDst < 0) finalDst = 0;
                    if (finalDst > (int)pDstBP->units.size()) finalDst = (int)pDstBP->units.size();

                    pDstBP->units.insert(pDstBP->units.begin() + finalDst, movingUnit);
                    pState->isDirtyBindings = true;
                }
            }

            pState->pressedControlIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        pState->isPotentialUnitDrag = false;

        if (pState->pressedControlIdx >= 0 && pState->pressedControlIdx < (int)pState->clickControls.size())
        {
            const auto& ctrl = pState->clickControls[pState->pressedControlIdx];
            if (PtInRect(&ctrl.rc, pt))
            {
                switch (ctrl.type)
                {
                case ClickControl::BTN_CAT_COLLAPSE_TOGGLE:
                {
                    if (pState->collapsedCategories.find(ctrl.tag) != pState->collapsedCategories.end())
                    {
                        pState->collapsedCategories.erase(ctrl.tag);
                    }
                    else
                    {
                        pState->collapsedCategories.insert(ctrl.tag);
                    }
                    pState->selectedCategory = ctrl.tag;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_CAT_HEADER:
                case ClickControl::CATEGORY_HEADER_CLICK:
                {
                    pState->selectedCategory = ctrl.tag;
                    bool hasCurrent = false;
                    for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                    {
                        if ((int)i == pState->selectedConfigIdx && TrainConfigManager::g_LoadedConfigsCache[i].category == pState->selectedCategory)
                        {
                            hasCurrent = true;
                            break;
                        }
                    }
                    if (!hasCurrent)
                    {
                        pState->selectedConfigIdx = -1;
                        for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                        {
                            if (TrainConfigManager::g_LoadedConfigsCache[i].category == pState->selectedCategory)
                            {
                                pState->selectedConfigIdx = (int)i;
                                pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[i];
                                TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                                pState->PopulateFieldsFromActiveConfig();
                                break;
                            }
                        }
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::CONFIG_ITEM_CLICK:
                {
                    pState->SyncActiveConfigFromFields();
                    int idx = ctrl.index;
                    if (idx >= 0 && idx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                    {
                        pState->selectedConfigIdx = idx;
                        pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[idx];
                        pState->selectedCategory = pState->activeConfig.category;
                        TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                        pState->PopulateFieldsFromActiveConfig();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_NEW_CATEGORY:
                {
                    // Instant category folder creation without popup
                    std::wstring root = TrainConfigManager::GetTrainConfigsRootDir();
                    std::wstring baseName = L"Category ";
                    int num = 1;
                    std::wstring candidate;
                    while (true)
                    {
                        candidate = baseName + std::to_wstring(num);
                        std::wstring p = root + L"\\" + candidate;
                        if (!fs::exists(p)) break;
                        num++;
                    }
                    try { fs::create_directories(root + L"\\" + candidate); } catch (...) {}
                    TrainConfigManager::ScanTrainConfigs();
                    pState->RefreshCategoryOrder();
                    pState->selectedCategory = candidate;
                    pState->selectedConfigIdx = -1;
                    pState->activeConfig = TrainConfigManager::TrainConfig();
                    pState->activeBinding = TrainConfigManager::TrainBinding();
                    pState->PopulateFieldsFromActiveConfig();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_CAT_RENAME:
                {
                    std::wstring oldCat = ctrl.tag;
                    std::wstring newCat;
                    if (ShowStudioPrompt(hWnd, L"Rename Folder", L"Enter new folder name:", oldCat, newCat))
                    {
                        if (!newCat.empty() && newCat != oldCat)
                        {
                            std::wstring root = TrainConfigManager::GetTrainConfigsRootDir();
                            std::wstring oldPath = root + L"\\" + oldCat;
                            std::wstring newPath = root + L"\\" + newCat;
                            try
                            {
                                if (fs::exists(oldPath)) fs::rename(oldPath, newPath);
                                else fs::create_directories(newPath);
                            }
                            catch (...) {}

                            TrainConfigManager::ScanTrainConfigs();
                            for (auto& c : pState->categoryOrder)
                            {
                                if (c == oldCat) c = newCat;
                            }
                            pState->RefreshCategoryOrder();
                            pState->selectedCategory = newCat;
                            if (pState->activeConfig.category == oldCat)
                            {
                                pState->activeConfig.category = newCat;
                                pState->PopulateFieldsFromActiveConfig();
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_CAT_CLONE:
                {
                    std::wstring srcCat = ctrl.tag;
                    std::wstring root = TrainConfigManager::GetTrainConfigsRootDir();
                    std::wstring baseCopy = srcCat + L"_Copy";
                    std::wstring candidate = baseCopy;
                    int copyNum = 2;
                    while (fs::exists(root + L"\\" + candidate))
                    {
                        candidate = baseCopy + L"_" + std::to_wstring(copyNum++);
                    }
                    std::wstring srcDir = root + L"\\" + srcCat;
                    std::wstring dstDir = root + L"\\" + candidate;
                    try
                    {
                        fs::create_directories(dstDir);
                        if (fs::exists(srcDir))
                        {
                            for (const auto& entry : fs::directory_iterator(srcDir))
                            {
                                if (entry.is_regular_file())
                                {
                                    std::wstring ext = entry.path().extension().wstring();
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                                    if (ext == L".train" || ext == L".config")
                                    {
                                        TrainConfigManager::TrainConfig cfg;
                                        if (TrainConfigManager::LoadTrainConfig(entry.path().wstring(), cfg))
                                        {
                                            cfg.category = candidate;
                                            std::wstring newFilePath = dstDir + L"\\" + entry.path().filename().wstring();
                                            cfg.filePath = newFilePath;
                                            cfg.relativePath = candidate + L"\\" + entry.path().filename().wstring();
                                            TrainConfigManager::SaveTrainConfig(newFilePath, cfg);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    catch (...) {}

                    TrainConfigManager::ScanTrainConfigs();
                    pState->RefreshCategoryOrder();
                    pState->selectedCategory = candidate;
                    pState->selectedConfigIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_CAT_UP:
                {
                    int catIdx = ctrl.index;
                    if (catIdx > 0 && catIdx < (int)pState->categoryOrder.size())
                    {
                        std::swap(pState->categoryOrder[catIdx], pState->categoryOrder[catIdx - 1]);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_CAT_DOWN:
                {
                    int catIdx = ctrl.index;
                    if (catIdx >= 0 && catIdx + 1 < (int)pState->categoryOrder.size())
                    {
                        std::swap(pState->categoryOrder[catIdx], pState->categoryOrder[catIdx + 1]);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_CAT_DELETE:
                case ClickControl::BTN_DELETE_CATEGORY:
                {
                    std::wstring delCat = !ctrl.tag.empty() ? ctrl.tag : pState->selectedCategory;
                    if (!delCat.empty())
                    {
                        std::wstring catDir = TrainConfigManager::GetTrainConfigsRootDir() + L"\\" + delCat;
                        int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to delete the category folder '" + delCat + L"' and all blueprints within it?\n\n" + catDir).c_str(), L"Delete Category Folder", MB_YESNO | MB_ICONWARNING);
                        if (res == IDYES)
                        {
                            try { fs::remove_all(catDir); } catch (...) {}
                            TrainConfigManager::ScanTrainConfigs();
                            pState->RefreshCategoryOrder();
                            pState->selectedConfigIdx = -1;
                            if (!pState->categoryOrder.empty())
                            {
                                pState->selectedCategory = pState->categoryOrder[0];
                            }
                            else
                            {
                                pState->selectedCategory = L"";
                                pState->activeConfig = TrainConfigManager::TrainConfig();
                                pState->activeBinding = TrainConfigManager::TrainBinding();
                                pState->PopulateFieldsFromActiveConfig();
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_NEW_BLUEPRINT:
                {
                    pState->SyncActiveConfigFromFields();
                    std::wstring cat = pState->selectedCategory;
                    if (cat.empty())
                    {
                        auto cats = TrainConfigManager::GetCategories();
                        if (!cats.empty()) cat = cats[0];
                        else cat = L"Express";
                    }

                    std::wstring newID;
                    if (ShowStudioPrompt(hWnd, L"New Train Blueprint", L"Enter unique Train ID (without spaces):", L"", newID))
                    {
                        if (!newID.empty())
                        {
                            std::replace(newID.begin(), newID.end(), L' ', L'_');
                            TrainConfigManager::TrainConfig cfg;
                            cfg.id = newID;
                            cfg.name = newID;
                            cfg.category = cat;
                            cfg.trainType = L"LocomotiveHauled";
                            cfg.maxSpeedKmph = 130.0;
                            cfg.minLength = 12;
                            cfg.maxLength = 24;
                            cfg.description = L"Consist blueprint for " + newID;

                            TrainConfigManager::TrainBlueprintPool p1;
                            p1.poolName = L"Lead_Locomotive";
                            p1.role = L"LeadLoco";
                            p1.unitType = L"Electric";
                            p1.filterTag = L"WAP7, WAP4";
                            p1.minCount = 1; p1.maxCount = 1;
                            cfg.pools.push_back(p1);
                            cfg.sequence.push_back(p1.poolName);

                            TrainConfigManager::TrainBlueprintPool p2;
                            p2.poolName = L"Coaches";
                            p2.role = L"Passenger";
                            p2.unitType = L"Passenger";
                            p2.filterTag = L"ICF_AC, LHB_AC";
                            p2.minCount = 8; p2.maxCount = 16;
                            cfg.pools.push_back(p2);
                            cfg.sequence.push_back(p2.poolName);

                            std::wstring trainFolder = TrainConfigManager::SanitizeFileName(cfg.name);
                            std::wstring trainDir = TrainConfigManager::GetTrainConfigsRootDir() + L"\\" + cat + L"\\" + trainFolder;
                            try { fs::create_directories(trainDir); } catch (...) {}
                            std::wstring fileName = trainFolder + L".train";
                            std::wstring filePath = trainDir + L"\\" + fileName;
                            cfg.filePath = filePath;
                            cfg.relativePath = cat + L"\\" + trainFolder + L"\\" + fileName;
                            TrainConfigManager::SaveTrainConfig(filePath, cfg);
                            TrainConfigManager::ScanTrainConfigs();

                            pState->selectedCategory = cat;
                            for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                            {
                                if (TrainConfigManager::g_LoadedConfigsCache[i].id == newID)
                                {
                                    pState->selectedConfigIdx = (int)i;
                                    pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[i];
                                    TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                                    pState->PopulateFieldsFromActiveConfig();
                                    break;
                                }
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_CLONE_BLUEPRINT:
                {
                    if (pState->selectedConfigIdx >= 0 && pState->selectedConfigIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                    {
                        const auto& cur = TrainConfigManager::g_LoadedConfigsCache[pState->selectedConfigIdx];
                        std::wstring newID;
                        if (ShowStudioPrompt(hWnd, L"Clone Train Blueprint", L"Enter new unique Train ID for cloned blueprint (no spaces):", cur.id + L"_COPY", newID))
                        {
                            if (!newID.empty())
                            {
                                TrainConfigManager::TrainConfig cfg = cur;
                                cfg.id = newID;
                                cfg.name = cur.name + L" (Copy)";

                                std::wstring trainFolder = TrainConfigManager::SanitizeFileName(cfg.name);
                                std::wstring trainDir = TrainConfigManager::GetTrainConfigsRootDir() + L"\\" + cfg.category + L"\\" + trainFolder;
                                try { fs::create_directories(trainDir); } catch (...) {}
                                std::wstring fileName = trainFolder + L".train";
                                std::wstring filePath = trainDir + L"\\" + fileName;
                                cfg.filePath = filePath;
                                cfg.relativePath = cfg.category + L"\\" + trainFolder + L"\\" + fileName;

                                TrainConfigManager::SaveTrainConfig(filePath, cfg);

                                // Clone bindings if present
                                TrainConfigManager::TrainBinding srcBinding;
                                if (TrainConfigManager::LoadTrainBinding(cur, srcBinding))
                                {
                                    srcBinding.trainID = cfg.id;
                                    srcBinding.configName = cfg.name;
                                    TrainConfigManager::SaveTrainBinding(cfg, srcBinding);
                                }

                                TrainConfigManager::ScanTrainConfigs();

                                for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                                {
                                    if (TrainConfigManager::g_LoadedConfigsCache[i].id == newID)
                                    {
                                        pState->selectedConfigIdx = (int)i;
                                        pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[i];
                                        TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                                        pState->PopulateFieldsFromActiveConfig();
                                        break;
                                    }
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }
                case ClickControl::BTN_DELETE_BLUEPRINT:
                {
                    if (pState->selectedConfigIdx >= 0 && pState->selectedConfigIdx < (int)TrainConfigManager::g_LoadedConfigsCache.size())
                    {
                        const auto& cur = TrainConfigManager::g_LoadedConfigsCache[pState->selectedConfigIdx];
                        int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to permanently delete this .train blueprint?\n\n" + cur.filePath).c_str(), L"Delete Blueprint", MB_YESNO | MB_ICONWARNING);
                        if (res == IDYES)
                        {
                            try
                            {
                                fs::path p(cur.filePath);
                                if (p.has_parent_path() && p.parent_path() != fs::path(TrainConfigManager::GetTrainConfigsRootDir()) && p.parent_path().parent_path() != fs::path(TrainConfigManager::GetTrainConfigsRootDir()))
                                {
                                    // Train has its own dedicated folder (Category / TrainFolder / file.train)
                                    fs::remove_all(p.parent_path());
                                }
                                else
                                {
                                    DeleteFileW(cur.filePath.c_str());
                                    std::wstring bPath = TrainConfigManager::GetBindingFilePathForConfig(cur);
                                    DeleteFileW(bPath.c_str());
                                }
                            }
                            catch (...)
                            {
                                DeleteFileW(cur.filePath.c_str());
                            }

                            TrainConfigManager::ScanTrainConfigs();
                            pState->selectedConfigIdx = -1;
                            for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                            {
                                if (TrainConfigManager::g_LoadedConfigsCache[i].category == pState->selectedCategory)
                                {
                                    pState->selectedConfigIdx = (int)i;
                                    pState->activeConfig = TrainConfigManager::g_LoadedConfigsCache[i];
                                    TrainConfigManager::LoadTrainBinding(pState->activeConfig, pState->activeBinding);
                                    pState->PopulateFieldsFromActiveConfig();
                                    break;
                                }
                            }
                            if (pState->selectedConfigIdx == -1)
                            {
                                pState->activeConfig = TrainConfigManager::TrainConfig();
                                pState->activeBinding = TrainConfigManager::TrainBinding();
                                pState->PopulateFieldsFromActiveConfig();
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_OPEN_FOLDER:
                {
                    std::wstring dir = TrainConfigManager::GetTrainConfigsRootDir();
                    if (!pState->selectedCategory.empty())
                    {
                        std::wstring sub = dir + L"\\" + pState->selectedCategory;
                        if (fs::exists(sub)) dir = sub;
                    }
                    ShellExecuteW(NULL, L"open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                }
                case ClickControl::BTN_RESCAN:
                {
                    TrainConfigManager::ScanTrainConfigs();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_ADD_POOL_TO_RAKE:
                {
                    // Instant pool addition without popup
                    int poolNum = 1;
                    std::wstring candidate;
                    while (true)
                    {
                        candidate = L"Pool_" + std::to_wstring(poolNum);
                        bool exists = false;
                        for (const auto& p : pState->activeConfig.pools)
                        {
                            if (_wcsicmp(p.poolName.c_str(), candidate.c_str()) == 0)
                            {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) break;
                        poolNum++;
                    }

                    TrainConfigManager::TrainBlueprintPool bp;
                    bp.poolName = candidate;
                    bp.role = L"Coach";
                    bp.unitType = L"Passenger";
                    bp.minCount = 1;
                    bp.maxCount = 4;
                    pState->activeConfig.pools.push_back(bp);
                    pState->activeConfig.sequence.push_back(candidate);
                    TrainConfigManager::TrainBindingPool bnd;
                    bnd.poolName = candidate;
                    pState->activeBinding.pools.push_back(bnd);
                    pState->isDirtyBindings = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_POOL_COLLAPSE_TOGGLE:
                {
                    int p = ctrl.index;
                    if (pState->collapsedBlueprintPools.find(p) != pState->collapsedBlueprintPools.end())
                    {
                        pState->collapsedBlueprintPools.erase(p);
                    }
                    else
                    {
                        pState->collapsedBlueprintPools.insert(p);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_POOL_ROLE_PICK:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        std::vector<DatabaseManager::BlueprintRoleItem> dbRoles;
                        DatabaseManager::LoadBlueprintRoles(dbRoles);

                        std::map<std::wstring, std::vector<DatabaseManager::BlueprintRoleItem>> catGroups;
                        for (const auto& r : dbRoles)
                        {
                            catGroups[r.category].push_back(r);
                        }

                        std::vector<ContextMenuItem> menuItems;
                        int roleCmdBase = 1000;
                        std::map<int, std::wstring> cmdToRoleName;

                        // 1. Locomotives & Cabs
                        if (catGroups.find(L"Locomotives & Cabs") != catGroups.end())
                        {
                            std::vector<ContextMenuItem> locoSub;
                            for (const auto& r : catGroups[L"Locomotives & Cabs"])
                            {
                                int cmdId = roleCmdBase++;
                                cmdToRoleName[cmdId] = r.roleName;
                                std::wstring icon = (pool.role == r.roleName ? L"\xE73E" : L"\xE7C0");
                                locoSub.push_back(ContextMenuItem::Action(cmdId, icon, r.roleName));
                            }
                            menuItems.push_back(ContextMenuItem::SubMenu(L"\xE7C0", L"Locomotives & Cabs", locoSub));
                        }

                        // 2. Coaches
                        if (catGroups.find(L"Coaches") != catGroups.end())
                        {
                            std::vector<ContextMenuItem> coachSub;
                            for (const auto& r : catGroups[L"Coaches"])
                            {
                                int cmdId = roleCmdBase++;
                                cmdToRoleName[cmdId] = r.roleName;
                                std::wstring icon = (pool.role == r.roleName ? L"\xE73E" : L"\xE7C0");
                                coachSub.push_back(ContextMenuItem::Action(cmdId, icon, r.roleName));
                            }
                            menuItems.push_back(ContextMenuItem::SubMenu(L"\xE7C0", L"Passenger Coaches", coachSub));
                        }

                        // 3. Freight
                        if (catGroups.find(L"Freight") != catGroups.end())
                        {
                            std::vector<ContextMenuItem> freightSub;
                            for (const auto& r : catGroups[L"Freight"])
                            {
                                int cmdId = roleCmdBase++;
                                cmdToRoleName[cmdId] = r.roleName;
                                std::wstring icon = (pool.role == r.roleName ? L"\xE73E" : L"\xE7B8");
                                freightSub.push_back(ContextMenuItem::Action(cmdId, icon, r.roleName));
                            }
                            menuItems.push_back(ContextMenuItem::SubMenu(L"\xE7B8", L"Freight Stock", freightSub));
                        }

                        // 4. Custom / Other categories
                        for (const auto& pair : catGroups)
                        {
                            if (pair.first != L"Locomotives & Cabs" && pair.first != L"Coaches" && pair.first != L"Freight")
                            {
                                std::vector<ContextMenuItem> otherSub;
                                for (const auto& r : pair.second)
                                {
                                    int cmdId = roleCmdBase++;
                                    cmdToRoleName[cmdId] = r.roleName;
                                    std::wstring icon = (pool.role == r.roleName ? L"\xE73E" : L"\xE77B");
                                    otherSub.push_back(ContextMenuItem::Action(cmdId, icon, r.roleName));
                                }
                                menuItems.push_back(ContextMenuItem::SubMenu(L"\xE77B", pair.first, otherSub));
                            }
                        }

                        // 5. Delete Role from DB Submenu
                        int delCmdBase = 5000;
                        std::map<int, std::wstring> delCmdToRoleName;
                        if (!dbRoles.empty())
                        {
                            std::vector<ContextMenuItem> delSub;
                            for (const auto& r : dbRoles)
                            {
                                int cmdId = delCmdBase++;
                                delCmdToRoleName[cmdId] = r.roleName;
                                std::wstring label = r.roleName + L" (" + r.category + L")";
                                delSub.push_back(ContextMenuItem::Action(cmdId, L"\xE74D", label));
                            }
                            menuItems.push_back(ContextMenuItem::Separator());
                            menuItems.push_back(ContextMenuItem::SubMenu(L"\xE74D", L"Delete Role from DB...", delSub));
                        }

                        menuItems.push_back(ContextMenuItem::Separator());
                        const int ID_ADD_CUSTOM = 9001;
                        const int ID_RESTORE_DEFAULTS = 9002;
                        menuItems.push_back(ContextMenuItem::Action(ID_ADD_CUSTOM, L"\xE710", L"Add New Role to DB..."));
                        menuItems.push_back(ContextMenuItem::Action(ID_RESTORE_DEFAULTS, L"\xE777", L"Restore Default Roles"));

                        POINT ptMenu = { ctrl.rc.left, ctrl.rc.bottom + 2 };
                        ClientToScreen(hWnd, &ptMenu);
                        int choice = ModernContextMenu::Show(hWnd, ptMenu.x, ptMenu.y, menuItems, TRUE, 220);

                        if (choice >= 1000 && choice < roleCmdBase)
                        {
                            pool.role = cmdToRoleName[choice];
                            pState->isDirtyBlueprint = true;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        else if (choice >= 5000 && choice < delCmdBase)
                        {
                            std::wstring delRole = delCmdToRoleName[choice];
                            if (!delRole.empty())
                            {
                                int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to permanently delete role '" + delRole + L"' from the database?\n\nExisting blueprints that reference this role will keep their text, but it will be removed from the selection catalog.").c_str(), L"Delete Blueprint Role", MB_YESNO | MB_ICONWARNING);
                                if (res == IDYES)
                                {
                                    DatabaseManager::DeleteBlueprintRole(delRole);
                                    pState->isDirtyBlueprint = true;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        else if (choice == ID_ADD_CUSTOM)
                        {
                            std::wstring newRole;
                            if (ShowStudioPrompt(hWnd, L"Add Blueprint Role (Role Name)", L"Enter custom role name (e.g. GeneratorCar, InspectionCar, Saloon, BrakeVan):", L"", newRole))
                            {
                                if (!newRole.empty())
                                {
                                    std::replace(newRole.begin(), newRole.end(), L' ', L'_');
                                    std::wstring newCat;
                                    if (ShowStudioPrompt(hWnd, L"Add Blueprint Role (Category)", L"Enter Category Name for this role (e.g. Locomotives & Cabs, Coaches, Freight, Departmental, Special Stock):", L"Custom", newCat))
                                    {
                                        if (newCat.empty()) newCat = L"Custom";
                                        DatabaseManager::AddBlueprintRole(newRole, newCat, false);
                                        pool.role = newRole;
                                        pState->isDirtyBlueprint = true;
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    }
                                }
                            }
                        }
                        else if (choice == ID_RESTORE_DEFAULTS)
                        {
                            DatabaseManager::RestoreDefaultBlueprintRoles();
                            ShowModernMessageBox(hWnd, L"Default blueprint roles restored successfully in the database.", L"Restore Default Roles", MB_OK | MB_ICONINFORMATION);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_POOL_RENAME:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        std::wstring oldName = pool.poolName;
                        std::wstring newName;
                        if (ShowStudioPrompt(hWnd, L"Rename Pool", L"Enter new pool name:", oldName, newName))
                        {
                            if (!newName.empty() && newName != oldName)
                            {
                                std::replace(newName.begin(), newName.end(), L' ', L'_');
                                pool.poolName = newName;
                                if (p < (int)pState->activeConfig.sequence.size())
                                {
                                    pState->activeConfig.sequence[p] = newName;
                                }
                                if (p < (int)pState->activeBinding.pools.size())
                                {
                                    pState->activeBinding.pools[p].poolName = newName;
                                    pState->isDirtyBindings = true;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }
                case ClickControl::BTN_POOL_CLONE:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto copyPool = pState->activeConfig.pools[p];
                        int cNum = 2;
                        std::wstring base = copyPool.poolName + L"_Copy";
                        std::wstring candidate = base;
                        while (true)
                        {
                            bool exists = false;
                            for (const auto& pool : pState->activeConfig.pools)
                            {
                                if (_wcsicmp(pool.poolName.c_str(), candidate.c_str()) == 0) { exists = true; break; }
                            }
                            if (!exists) break;
                            candidate = base + L"_" + std::to_wstring(cNum++);
                        }
                        copyPool.poolName = candidate;
                        pState->activeConfig.pools.insert(pState->activeConfig.pools.begin() + p + 1, copyPool);
                        pState->activeConfig.sequence.clear();
                        for (const auto& pl : pState->activeConfig.pools) {
                            pState->activeConfig.sequence.push_back(pl.poolName);
                        }

                        if (p < (int)pState->activeBinding.pools.size())
                        {
                            auto copyBnd = pState->activeBinding.pools[p];
                            copyBnd.poolName = candidate;
                            pState->activeBinding.pools.insert(pState->activeBinding.pools.begin() + p + 1, copyBnd);
                            pState->isDirtyBindings = true;
                        }

                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MIN_DEC:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        if (pool.minCount > 0) pool.minCount--;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MIN_INC:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        pool.minCount++;
                        if (pool.minCount > pool.maxCount) pool.maxCount = pool.minCount;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MIN_EDIT:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        std::wstring res;
                        if (ShowStudioPrompt(hWnd, L"Edit Minimum Count", L"Enter minimum vehicle count for pool (numbers only):", std::to_wstring(pool.minCount), res, true, false))
                        {
                            try
                            {
                                int val = std::stoi(res);
                                if (val >= 0)
                                {
                                    pool.minCount = val;
                                    if (pool.minCount > pool.maxCount) pool.maxCount = pool.minCount;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                            catch (...) {}
                        }
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MAX_DEC:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        if (pool.maxCount > 1 && pool.maxCount > pool.minCount) pool.maxCount--;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MAX_INC:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        pool.maxCount++;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_MAX_EDIT:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        std::wstring res;
                        if (ShowStudioPrompt(hWnd, L"Edit Maximum Count", L"Enter maximum vehicle count for pool (numbers only):", std::to_wstring(pool.maxCount), res, true, false))
                        {
                            try
                            {
                                int val = std::stoi(res);
                                if (val >= 1)
                                {
                                    pool.maxCount = val;
                                    if (pool.maxCount < pool.minCount) pool.minCount = pool.maxCount;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                            catch (...) {}
                        }
                    }
                    break;
                }
                case ClickControl::BTN_TOTAL_MIN_DEC:
                {
                    if (pState->activeConfig.minLength > 0)
                    {
                        pState->activeConfig.minLength--;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_TOTAL_MIN_INC:
                {
                    pState->activeConfig.minLength++;
                    if (pState->activeConfig.minLength > pState->activeConfig.maxLength)
                        pState->activeConfig.maxLength = pState->activeConfig.minLength;
                    pState->isDirtyBlueprint = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_TOTAL_MIN_EDIT:
                {
                    std::wstring res;
                    if (ShowStudioPrompt(hWnd, L"Edit Total Min Units", L"Enter minimum total train units (numbers only):", std::to_wstring(pState->activeConfig.minLength), res, true, false))
                    {
                        try
                        {
                            int val = std::stoi(res);
                            if (val >= 0)
                            {
                                pState->activeConfig.minLength = val;
                                if (pState->activeConfig.maxLength < pState->activeConfig.minLength)
                                    pState->activeConfig.maxLength = pState->activeConfig.minLength;
                                pState->isDirtyBlueprint = true;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        catch (...) {}
                    }
                    break;
                }
                case ClickControl::BTN_TOTAL_MAX_DEC:
                {
                    if (pState->activeConfig.maxLength > 1 && pState->activeConfig.maxLength > pState->activeConfig.minLength)
                    {
                        pState->activeConfig.maxLength--;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_TOTAL_MAX_INC:
                {
                    pState->activeConfig.maxLength++;
                    pState->isDirtyBlueprint = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_TOTAL_MAX_EDIT:
                {
                    std::wstring res;
                    if (ShowStudioPrompt(hWnd, L"Edit Total Max Units", L"Enter maximum total train units (numbers only):", std::to_wstring(pState->activeConfig.maxLength), res, true, false))
                    {
                        try
                        {
                            int val = std::stoi(res);
                            if (val >= 1)
                            {
                                pState->activeConfig.maxLength = val;
                                if (pState->activeConfig.minLength > pState->activeConfig.maxLength)
                                    pState->activeConfig.minLength = pState->activeConfig.maxLength;
                                pState->isDirtyBlueprint = true;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        catch (...) {}
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_NAME:
                {
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Train Name", L"Enter the display name for this train configuration:", pState->activeConfig.name, val))
                    {
                        pState->activeConfig.name = val;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_ID:
                {
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Train ID", L"Enter the unique Train Identifier (used in activities/consists):", pState->activeConfig.id, val))
                    {
                        pState->activeConfig.id = val;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_TYPE:
                {
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Train Type / Power", L"Enter locomotive / power traction type (e.g. Electric, Diesel, EMU):", pState->activeConfig.trainType, val))
                    {
                        pState->activeConfig.trainType = val;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_CATEGORY:
                {
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Category", L"Enter service category (e.g. Express, Superfast, Freight, Passenger):", pState->activeConfig.category, val))
                    {
                        pState->activeConfig.category = val;
                        pState->selectedCategory = val;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_MAXSPEED:
                {
                    std::wstring initVal = (pState->activeConfig.maxSpeedKmph > 0.0) ? std::to_wstring((int)pState->activeConfig.maxSpeedKmph) : L"";
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Speed Limit", L"Enter maximum speed limit in km/h (numbers only):", initVal, val, true, true))
                    {
                        try {
                            pState->activeConfig.maxSpeedKmph = std::stod(val);
                        } catch (...) {
                            pState->activeConfig.maxSpeedKmph = 0.0;
                        }
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_PERFFACTOR:
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L"%.2f", pState->activeConfig.perfFactor);
                    std::wstring initVal = buf;
                    while (initVal.size() > 1 && initVal.find(L'.') != std::wstring::npos && (initVal.back() == L'0' || initVal.back() == L'.')) {
                        if (initVal.back() == L'.') { initVal.pop_back(); break; }
                        initVal.pop_back();
                    }
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Performance Factor", L"Enter performance factor % (e.g. 1.0 or 0.85):", initVal, val, true, true))
                    {
                        try {
                            double pf = std::stod(val);
                            if (pf > 0.0) pState->activeConfig.perfFactor = pf;
                        } catch (...) {
                            pState->activeConfig.perfFactor = 1.0;
                        }
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_EDIT_DESC:
                {
                    std::wstring val;
                    if (ShowStudioPrompt(hWnd, L"Edit Description", L"Enter train description, rake details or operational notes:", pState->activeConfig.description, val))
                    {
                        pState->activeConfig.description = val;
                        pState->isDirtyBlueprint = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_SAVE_BLUEPRINT:
                {
                    pState->SyncActiveConfigFromFields();

                    if (pState->activeConfig.id.empty())
                    {
                        ShowModernMessageBox(hWnd, L"Please enter a valid unique Train ID before saving.", L"Missing Train ID", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    if (pState->activeConfig.category.empty())
                    {
                        std::wstring newCat;
                        if (ShowStudioPrompt(hWnd, L"Specify Category", L"Enter category folder name (no spaces):", L"", newCat))
                        {
                            pState->activeConfig.category = newCat;
                            pState->selectedCategory = newCat;
                        }
                        else
                        {
                            pState->activeConfig.category = L"Express";
                        }
                    }

                    // Sync sequence with current pools
                    pState->activeConfig.sequence.clear();
                    for (const auto& p : pState->activeConfig.pools)
                    {
                        pState->activeConfig.sequence.push_back(p.poolName);
                    }

                    std::wstring trainFolder = TrainConfigManager::SanitizeFileName(pState->activeConfig.name);
                    if (trainFolder.empty()) trainFolder = TrainConfigManager::SanitizeID(pState->activeConfig.id);
                    std::wstring trainDir = TrainConfigManager::GetTrainConfigsRootDir() + L"\\" + pState->activeConfig.category + L"\\" + trainFolder;
                    try { fs::create_directories(trainDir); } catch (...) {}
                    std::wstring fileName = trainFolder + L".train";
                    std::wstring newFilePath = trainDir + L"\\" + fileName;
                    if (!pState->activeConfig.filePath.empty() && _wcsicmp(pState->activeConfig.filePath.c_str(), newFilePath.c_str()) != 0) { DeleteFileW(pState->activeConfig.filePath.c_str()); }
                    pState->activeConfig.filePath = newFilePath;
                    pState->activeConfig.relativePath = pState->activeConfig.category + L"\\" + trainFolder + L"\\" + fileName;

                    if (TrainConfigManager::SaveTrainConfig(newFilePath, pState->activeConfig))
                    {
                        TrainConfigManager::ScanTrainConfigs();
                        for (size_t i = 0; i < TrainConfigManager::g_LoadedConfigsCache.size(); ++i)
                        {
                            if (TrainConfigManager::g_LoadedConfigsCache[i].id == pState->activeConfig.id)
                            {
                                pState->selectedConfigIdx = (int)i;
                                break;
                            }
                        }
                        ShowModernMessageBox(hWnd, (L"Successfully saved .train blueprint:\n" + newFilePath).c_str(), L"Blueprint Saved", MB_OK | MB_ICONINFORMATION);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else
                    {
                        ShowModernMessageBox(hWnd, (L"Failed to write .train file:\n" + newFilePath).c_str(), L"Save Error", MB_OK | MB_ICONERROR);
                    }
                    break;
                }
                case ClickControl::BTN_SWITCH_TO_BINDINGS:
                {
                    pState->SwitchToTab(1);
                    break;
                }
                case ClickControl::BTN_POOL_UP:
                {
                    int p = ctrl.index;
                    if (p > 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        std::swap(pState->activeConfig.pools[p], pState->activeConfig.pools[p - 1]);
                        if (p < (int)pState->activeBinding.pools.size())
                        {
                            std::swap(pState->activeBinding.pools[p], pState->activeBinding.pools[p - 1]);
                            pState->isDirtyBindings = true;
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_DOWN:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p + 1 < (int)pState->activeConfig.pools.size())
                    {
                        std::swap(pState->activeConfig.pools[p], pState->activeConfig.pools[p + 1]);
                        if (p + 1 < (int)pState->activeBinding.pools.size())
                        {
                            std::swap(pState->activeBinding.pools[p], pState->activeBinding.pools[p + 1]);
                            pState->isDirtyBindings = true;
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_DELETE:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        pState->activeConfig.pools.erase(pState->activeConfig.pools.begin() + p);
                        if (p < (int)pState->activeBinding.pools.size())
                        {
                            pState->activeBinding.pools.erase(pState->activeBinding.pools.begin() + p);
                            pState->isDirtyBindings = true;
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_POOL_EDIT:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        auto& pool = pState->activeConfig.pools[p];
                        std::wstring newTags;
                        if (ShowStudioPrompt(hWnd, L"Edit Filter Tags (" + pool.poolName + L")", L"Enter comma-separated matching tags / keywords:", pool.filterTag, newTags))
                        {
                            pool.filterTag = newTags;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_BIND_SMART_MATCH:
                {
                    int added = AutoPopulateBlueprintBindings(pState->activeConfig, pState->activeBinding);
                    ShowModernMessageBox(hWnd, (L"Smart Match successfully allocated " + std::to_wstring(added) + L" matching rolling stock units across all pools!").c_str(), L"Smart Library Match", MB_OK | MB_ICONINFORMATION);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_BIND_SAVE:
                {
                    pState->activeBinding.trainID = pState->activeConfig.id;
                    pState->activeBinding.configName = pState->activeConfig.name;
                    if (TrainConfigManager::SaveTrainBinding(pState->activeConfig, pState->activeBinding))
                    {
                        std::wstring p = TrainConfigManager::GetBindingFilePathForConfig(pState->activeConfig);
                        ShowModernMessageBox(hWnd, (L"Successfully saved local rolling stock bindings:\n" + p).c_str(), L"Bindings Saved", MB_OK | MB_ICONINFORMATION);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
                case ClickControl::BTN_BIND_OPEN_FOLDER:
                {
                    std::wstring dir = TrainConfigManager::GetTrainConfigsRootDir();
                    if (!pState->activeConfig.filePath.empty())
                    {
                        try {
                            fs::path p(pState->activeConfig.filePath);
                            if (fs::exists(p.parent_path())) dir = p.parent_path().wstring();
                        } catch (...) {}
                    }
                    else if (!pState->selectedCategory.empty())
                    {
                        std::wstring sub = dir + L"\\" + pState->selectedCategory;
                        if (fs::exists(sub)) dir = sub;
                    }
                    ShellExecuteW(NULL, L"open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                }
                case ClickControl::BTN_BIND_POOL_COLLAPSE_TOGGLE:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        if (pState->collapsedBindingPools.count(p))
                            pState->collapsedBindingPools.erase(p);
                        else
                            pState->collapsedBindingPools.insert(p);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }
                case ClickControl::BTN_BIND_POOL_MODE_TOGGLE:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP)
                        {
                            pBP->pickMode = (pBP->pickMode == PoolManager::PoolPickMode::Random) ?
                                            PoolManager::PoolPickMode::Sequential :
                                            PoolManager::PoolPickMode::Random;
                            pState->isDirtyBindings = true;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_BIND_POOL_FLIP_TOGGLE:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP)
                        {
                            if (pBP->flipPolicy == PoolManager::PoolFlipPolicy::ForwardOnly)
                                pBP->flipPolicy = PoolManager::PoolFlipPolicy::AllowRandom;
                            else if (pBP->flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom)
                                pBP->flipPolicy = PoolManager::PoolFlipPolicy::AlwaysFlipped;
                            else
                                pBP->flipPolicy = PoolManager::PoolFlipPolicy::ForwardOnly;

                            pState->isDirtyBindings = true;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_BIND_POOL_COPY_UNITS:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP && !pBP->units.empty())
                        {
                            std::vector<ConsistReader::UnitInfo> clipUnits;
                            for (const auto& pu : pBP->units)
                            {
                                ConsistReader::UnitInfo ui;
                                ui.uid = pu.szFileName;
                                ui.parentDir = pu.szFolder;
                                ui.isEngine = pu.isEngine;
                                ui.isFlipped = (pu.flipMode == PoolManager::UnitFlipMode::Flipped);
                                clipUnits.push_back(ui);
                            }
                            SetAppClipboardUnits(clipUnits);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_BIND_POOL_PASTE_UNITS:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        const auto& clipUnits = GetAppClipboardUnits();
                        if (pBP && !clipUnits.empty())
                        {
                            int added = 0;
                            for (const auto& u : clipUnits)
                            {
                                bool exists = false;
                                for (const auto& eu : pBP->units)
                                {
                                    if (_wcsicmp(eu.szFileName.c_str(), u.uid.c_str()) == 0 &&
                                        _wcsicmp(eu.szFolder.c_str(), u.parentDir.c_str()) == 0)
                                    {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (!exists)
                                {
                                    PoolManager::PoolUnit pu;
                                    pu.szFileName = u.uid;
                                    pu.szFolder = u.parentDir;
                                    pu.isEngine = u.isEngine;
                                    pu.flipMode = u.isFlipped ? PoolManager::UnitFlipMode::Flipped : PoolManager::UnitFlipMode::Auto;
                                    pBP->units.push_back(pu);
                                    added++;
                                }
                            }
                            if (added > 0)
                            {
                                pState->isDirtyBindings = true;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }
                case ClickControl::BTN_BIND_POOL_CLEAR_UNITS:
                {
                    int p = ctrl.index;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP && !pBP->units.empty())
                        {
                            int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to clear all " + std::to_wstring(pBP->units.size()) + L" allocated units from pool '" + pool.poolName + L"'?").c_str(), L"Clear Pool Units", MB_YESNO | MB_ICONWARNING);
                            if (res == IDYES)
                            {
                                pBP->units.clear();
                                pState->isDirtyBindings = true;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }
                case ClickControl::BTN_UNIT_REMOVE:
                {
                    int p = ctrl.index;
                    int u = ctrl.subIndex;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP && u >= 0 && u < (int)pBP->units.size())
                        {
                            pBP->units.erase(pBP->units.begin() + u);
                            pState->isDirtyBindings = true;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                case ClickControl::BTN_UNIT_FLIP_TOGGLE:
                {
                    int p = ctrl.index;
                    int u = ctrl.subIndex;
                    if (p >= 0 && p < (int)pState->activeConfig.pools.size())
                    {
                        const auto& pool = pState->activeConfig.pools[p];
                        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, p, pool.poolName);
                        if (pBP && u >= 0 && u < (int)pBP->units.size())
                        {
                            auto& pu = pBP->units[u];
                            if (pu.flipMode == PoolManager::UnitFlipMode::Auto)
                                pu.flipMode = PoolManager::UnitFlipMode::Forward;
                            else if (pu.flipMode == PoolManager::UnitFlipMode::Forward)
                                pu.flipMode = PoolManager::UnitFlipMode::Flipped;
                            else if (pu.flipMode == PoolManager::UnitFlipMode::Flipped)
                                pu.flipMode = PoolManager::UnitFlipMode::Random;
                            else
                                pu.flipMode = PoolManager::UnitFlipMode::Auto;

                            pState->isDirtyBindings = true;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }
        pState->pressedControlIdx = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        if (pState)
        {
            int tabIndex = (int)wParam;
            pState->SwitchToTab(tabIndex);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        WORD wmEvent = HIWORD(wParam);
        HWND hwndCtrl = (HWND)lParam;
        if (wmEvent == EN_CHANGE && hwndCtrl && pState)
        {
            pState->isDirtyBlueprint = true;
            InvalidateRect(hwndCtrl, NULL, TRUE);
        }

        // Custom TitleBar Navigation Commands (Fallback)
        if (LOWORD(wParam) == 30001)
        {
            int tabIndex = (int)lParam;
            if (pState) pState->SwitchToTab(tabIndex);
            return 0;
        }

        // TitleBar Close / Minimize
        if (LOWORD(wParam) == 30002)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wParam) == 30003)
        {
            ShowWindow(hWnd, SW_MINIMIZE);
            return 0;
        }
        if (LOWORD(wParam) == 30004)
        {
            if (IsZoomed(hWnd)) ShowWindow(hWnd, SW_RESTORE);
            else ShowWindow(hWnd, SW_MAXIMIZE);
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            short delta = (short)GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);

            if (pt.x < pState->sidebarWidth)
            {
                pState->m_sidebarScroll.OnMouseWheel(delta, 48, hWnd);
                pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
            }
            else
            {
                if (pState->activeTab == 0)
                {
                    pState->m_editorScroll.OnMouseWheel(delta, 48, hWnd);
                    pState->editorScrollY = pState->m_editorScroll.GetPos();
                }
                else
                {
                    pState->m_bindingsScroll.OnMouseWheel(delta, 48, hWnd);
                    pState->bindingsScrollY = pState->m_bindingsScroll.GetPos();
                }
            }

            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
            if (pState->hFontBadge) DeleteObject(pState->hFontBadge);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
            if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);

            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        g_hTrainConfigStudioDlg = NULL;
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// ------------------------------------------------------------------------------------------------
// Public Dialog Launcher
// ------------------------------------------------------------------------------------------------

void ShowTrainConfigStudioDialog(HWND hWndParent, int initialTab)
{
    if (g_hTrainConfigStudioDlg && IsWindow(g_hTrainConfigStudioDlg))
    {
        SetForegroundWindow(g_hTrainConfigStudioDlg);
        return;
    }

    static bool s_Registered = false;
    const wchar_t* szClassName = L"TrainConfigStudioWindowClass";

    if (!s_Registered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_DBLCLKS;
        wcex.lpfnWndProc = TrainConfigStudioWndProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = NULL;
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_Registered = true;
    }

    StudioDlgState* pState = new StudioDlgState();
    pState->hParent = hWndParent;
    pState->activeTab = initialTab;

    int dlgW = 1140;
    int dlgH = 780;

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
        0, szClassName, L"Train Config Studio",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandleW(NULL), pState
    );

    if (!hDlg)
    {
        delete pState;
        return;
    }

    g_hTrainConfigStudioDlg = hDlg;

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

    SetWindowPos(hDlg, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    ShowWindow(hDlg, SW_SHOW);

    RECT rc;
    GetClientRect(hDlg, &rc);
    int clientW = rc.right - rc.left;
    if (pState->hTitleBar && IsWindow(pState->hTitleBar))
    {
        SetWindowPos(pState->hTitleBar, NULL, 0, 0, clientW, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// ------------------------------------------------------------------------------------------------
// Drag & Drop Handlers
// ------------------------------------------------------------------------------------------------

bool TrainConfigStudio_IsActive()
{
    return (g_hTrainConfigStudioDlg && IsWindow(g_hTrainConfigStudioDlg) && IsWindowVisible(g_hTrainConfigStudioDlg));
}

HWND TrainConfigStudio_GetHWND()
{
    return g_hTrainConfigStudioDlg;
}

bool TrainConfigStudio_HandleDragHover(POINT ptScreen)
{
    if (!TrainConfigStudio_IsActive()) return false;

    StudioDlgState* pState = (StudioDlgState*)GetWindowLongPtrW(g_hTrainConfigStudioDlg, GWLP_USERDATA);
    if (!pState || pState->activeTab != 1) return false;

    POINT ptClient = ptScreen;
    ScreenToClient(g_hTrainConfigStudioDlg, &ptClient);

    RECT rcClient;
    GetClientRect(g_hTrainConfigStudioDlg, &rcClient);

    // Auto-scroll check for bindings view
    int autoDelta = 0;
    if (pState->m_bindingsScroll.CheckAutoScroll(ptClient, 32, 16, autoDelta))
    {
        int maxScroll = (std::max<int>)(0, pState->totalBindingsH - ((int)rcClient.bottom - 112));
        int newScroll = std::clamp(pState->bindingsScrollY + autoDelta, 0, maxScroll);
        if (newScroll != pState->bindingsScrollY)
        {
            pState->bindingsScrollY = newScroll;
            pState->m_bindingsScroll.SetPos(newScroll);
            InvalidateRect(g_hTrainConfigStudioDlg, NULL, FALSE);
            UpdateWindow(g_hTrainConfigStudioDlg);
        }
    }

    int hitPool = -1;
    for (const auto& hit : pState->bindingPoolCardHits)
    {
        if (PtInRect(&hit.rcCard, ptClient))
        {
            hitPool = hit.poolIdx;
            break;
        }
    }

    if (hitPool != pState->dragHoverPoolIdx)
    {
        pState->dragHoverPoolIdx = hitPool;
        InvalidateRect(g_hTrainConfigStudioDlg, NULL, FALSE);
    }

    if (hitPool != -1 && hitPool >= 0 && hitPool < (int)pState->activeConfig.pools.size())
    {
        std::wstring pName = pState->activeConfig.pools[hitPool].poolName;
        FluentDragGhost::Move(ptScreen, true, L"Add to Pool: " + pName);
        return true;
    }

    return false;
}

bool TrainConfigStudio_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units)
{
    if (!TrainConfigStudio_IsActive()) return false;

    StudioDlgState* pState = (StudioDlgState*)GetWindowLongPtrW(g_hTrainConfigStudioDlg, GWLP_USERDATA);
    if (!pState || pState->activeTab != 1) return false;

    POINT ptClient = ptScreen;
    ScreenToClient(g_hTrainConfigStudioDlg, &ptClient);

    int hitPool = -1;
    for (const auto& hit : pState->bindingPoolCardHits)
    {
        if (PtInRect(&hit.rcCard, ptClient))
        {
            hitPool = hit.poolIdx;
            break;
        }
    }

    pState->dragHoverPoolIdx = -1;

    if (hitPool >= 0 && hitPool < (int)pState->activeConfig.pools.size())
    {
        const auto& pool = pState->activeConfig.pools[hitPool];
        TrainConfigManager::TrainBindingPool* pBP = GetBindingPoolByIndex(pState->activeBinding, hitPool, pool.poolName);
        if (pBP)
        {
            for (const auto& u : units)
            {
                bool exists = false;
                for (const auto& eu : pBP->units)
                {
                    if (_wcsicmp(eu.szFileName.c_str(), u.uid.c_str()) == 0 &&
                        _wcsicmp(eu.szFolder.c_str(), u.parentDir.c_str()) == 0)
                    {
                        exists = true;
                        break;
                    }
                }

                if (!exists)
                {
                    PoolManager::PoolUnit pu;
                    pu.szFileName = u.uid;
                    pu.szFolder = u.parentDir;
                    pu.isEngine = u.isEngine;
                    pu.flipMode = u.isFlipped ? PoolManager::UnitFlipMode::Flipped : PoolManager::UnitFlipMode::Auto;
                    pBP->units.push_back(pu);
                }
            }
            pState->isDirtyBindings = true;
            InvalidateRect(g_hTrainConfigStudioDlg, NULL, FALSE);
            return true;
        }
    }

    InvalidateRect(g_hTrainConfigStudioDlg, NULL, FALSE);
    return false;
}
