#include "PoolManagerDlg.h"
#include "BatchConsistGeneratorDlg.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "../SRC/PoolManager.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include "../SRC/AssetsParser.h"
#include "FluentDragGhost.h"
#include "CustomScrollBar.h"
#include "CustomTitleBar.h"
#include "ModernContextMenu.h"
#include "CustomDropDownMenu.h"
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
        SIZE iconSz = { 0 };
        GetTextExtentPoint32W(hdc, iconGlyph, (int)wcslen(iconGlyph), &iconSz);
        int iconW = iconSz.cx > 0 ? iconSz.cx : 12;

        SelectObject(hdc, hFont);
        SIZE textSz = { 0 };
        GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSz);
        int textW = textSz.cx;

        int gap = 6;
        int totalW = iconW + gap + textW;
        int minMargin = 6;

        int startX = rc.left + (rc.right - rc.left - totalW) / 2;
        if (startX < rc.left + minMargin)
        {
            startX = rc.left + minMargin;
        }

        SelectObject(hdc, hIconFont);
        RECT rcIcon = { startX, rc.top, startX + iconW, rc.bottom };
        DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, hFont);
        RECT rcText = { startX + iconW + gap, rc.top, rc.right - minMargin, rc.bottom };
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
        return 0;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == EN_UPDATE)
        {
            // Edit changed
        }
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
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool ShowModernInputPrompt(HWND hWndParent, const std::wstring& title, const std::wstring& prompt, const std::wstring& defaultText, std::wstring& outText)
{
    static bool s_PromptRegistered = false;
    const wchar_t* szClassName = L"ModernInputPromptDialogClass";

    if (!s_PromptRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = InputPromptProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(RGB(20, 20, 20));
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_PromptRegistered = true;
    }

    InputPromptState state;
    state.hParent = hWndParent;
    state.title = title;
    state.prompt = prompt;
    state.resultText = defaultText;

    int dlgW = 460;
    int dlgH = 180;

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
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClassName, title.c_str(),
        WS_POPUP | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandleW(NULL), &state
    );

    if (!hDlg) return false;

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, FALSE);
    }

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
                DestroyWindow(hDlg);
                break;
            }
            else if (msg.wParam == VK_ESCAPE)
            {
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
// Main Pool Manager Dialog Implementation
// -------------------------------------------------------------
struct ClickableControl
{
    enum Type {
        NONE,
        // Tab 0 Controls
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
        COMBO_PRESET_CLICK,

        // Tab 1 Controls (Unit Replacement Groups)
        BTN_GROUP_NEW,
        BTN_GROUP_RENAME,
        BTN_GROUP_DELETE,
        BTN_GROUP_COLLAPSE_TOGGLE,
        BTN_GROUP_PASTE_CLIPBOARD,
        BTN_GROUP_CLEAR_UNITS,
        BTN_GROUP_EXPAND_ALL,
        BTN_GROUP_COLLAPSE_ALL,
        BTN_GROUP_UNIT_REMOVE,
        BTN_GROUP_UNIT_FLIP_TOGGLE,
        BTN_GROUP_UNIT_COPY,
        BTN_GROUP_COPY_SELECTED,
        BTN_GROUP_DELETE_SELECTED,
        BTN_GROUP_FLIP_SELECTED,

        // Common Controls
        BTN_CLOSE,
        BTN_GENERATE
    };

    Type type = NONE;
    RECT rc = { 0 };
    int poolIdx = -1; // Also used for groupIdx in Tab 1
    int unitIdx = -1;
};

struct WizardDlgState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hTitleBar = NULL;
    int activeTab = 0; // 0 = Consist Assembly Pools, 1 = Unit Replacement Groups

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

    // Drag over highlights
    int dragOverPoolIdx = -1;
    int dragOverGroupIdx = -1;

    // Card Drag-and-Drop Re-order state (Tab 0)
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

    // Auto-repeat state for +/- buttons (Tab 0)
    ClickableControl::Type repeatAction = ClickableControl::NONE;
    int repeatPoolIdx = -1;
    int repeatHoldCount = 0;

    // Multi-selection state for pool units (Tab 0)
    int selectedPoolIdx = -1;
    std::unordered_set<int> selectedUnitIndices;
    int anchorUnitIdx = -1;

    // Marquee / Rubber-Band selection state (Tab 0)
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

    // Tab 1 (Replacement Groups) Hits & Selection
    struct GroupCardHit {
        int groupIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
    };
    std::vector<GroupCardHit> groupCardHits;

    struct GroupUnitChipHit {
        int groupIdx = -1;
        int unitIdx = -1;
        RECT rcChip = { 0 };
    };
    std::vector<GroupUnitChipHit> groupUnitChipHits;

    struct GroupUnitsBoxHit {
        int groupIdx = -1;
        RECT rcUnitsBox = { 0 };
    };
    std::vector<GroupUnitsBoxHit> groupUnitsBoxHits;

    int selectedGroupIdx = -1;
    std::unordered_set<int> selectedGroupUnitIndices;
    int anchorGroupUnitIdx = -1;

    bool isGroupMarqueeSelecting = false;
    bool isGroupPotentialMarquee = false;
    POINT ptGroupMarqueeStart = { 0, 0 };
    POINT ptGroupMarqueeCurrent = { 0, 0 };
    int marqueeGroupIdx = -1;
    std::unordered_set<int> marqueeGroupInitialSelection;
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

static void RestoreParentWindowFocus(HWND hParent)
{
    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        if (IsIconic(hParent))
        {
            ShowWindow(hParent, SW_RESTORE);
        }
        SetWindowPos(hParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hParent);
        SetActiveWindow(hParent);
        BringWindowToTop(hParent);
        SetFocus(hParent);
    }
}

static LRESULT CALLBACK WizardDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WizardDlgState* pState = (WizardDlgState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
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
                LRESULT hit = SendMessageW(pState->hTitleBar, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                if (hit == HTTRANSPARENT)
                {
                    return HTCAPTION;
                }
            }
        }

        return DefWindowProc(hWnd, uMsg, wParam, lParam);
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

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right > 0 ? rcClient.right : 980;

        // Create CustomTitleBar with 2 Tabs
        std::vector<TitleBarTabItem> poolTabs = {
            { L"\xE77F", L"Consist Assembly Pools" },
            { L"\xE8D7", L"Unit Replacement Groups" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, w, 66,
            10001,
            L"Consist Pool Manager - TrainSim Consist Builder",
            poolTabs
        );


        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
            SendMessage(pState->hTitleBar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, 66));
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

        return 0;
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        if (pState)
        {
            pState->activeTab = (int)wParam;
            pState->m_vScroll.SetPos(0);
            pState->scrollY = 0;
            pState->selectedUnitIndices.clear();
            pState->selectedGroupUnitIndices.clear();
            pState->dragOverPoolIdx = -1;
            pState->dragOverGroupIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (pState->hTitleBar && IsWindow(pState->hTitleBar))
            {
                SetWindowPos(pState->hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS);
                InvalidateRect(pState->hTitleBar, NULL, FALSE);
                UpdateWindow(pState->hTitleBar);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_TIMER:
    {
        if (wParam == TIMER_REPEAT_ID && pState && pState->repeatAction != ClickableControl::NONE)
        {
            pState->repeatHoldCount++;
            int step = 1;
            if (pState->repeatHoldCount > 15) step = 10;
            else if (pState->repeatHoldCount > 8) step = 5;

            ExecuteStepAction(pState->repeatAction, pState->repeatPoolIdx, step);
            SetTimer(hWnd, TIMER_REPEAT_ID, 60, NULL);
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
        pState->cardHits.clear();
        pState->groupCardHits.clear();
        pState->groupUnitChipHits.clear();
        pState->groupUnitsBoxHits.clear();

        // 1. Background Fill
        COLORREF bgCol = PoolTheme::GutterBackground;
        COLORREF borderCol = PoolTheme::BorderLine;
        COLORREF textPrimary = PoolTheme::TextPrimary;
        COLORREF textSecondary = PoolTheme::TextSecondary;
        COLORREF accentCol = PoolTheme::AccentBlue;

        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hmemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hmemDC, TRANSPARENT);

        // =========================================================================
        // TAB 0: CONSIST ASSEMBLY POOLS
        // =========================================================================
        if (pState->activeTab == 0)
        {
            // 1. Preset Toolbar (Y = 66 to 112, Height = 46px)
            int toolbarY = 66;
            int toolbarH = 46;
            RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
            HBRUSH hbrTb = CreateSolidBrush(PoolTheme::ToolbarBackground);
            FillRect(hmemDC, &rcToolbar, hbrTb);
            DeleteObject(hbrTb);

            HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenLine);
            MoveToEx(hmemDC, 0, toolbarY + toolbarH, NULL);
            LineTo(hmemDC, w, toolbarY + toolbarH);

            SelectObject(hmemDC, pState->hFontMainBold);
            SetTextColor(hmemDC, textPrimary);
            RECT rcPresetLabel = { 16, toolbarY, 68, toolbarY + toolbarH };
            DrawTextW(hmemDC, L"Preset:", -1, &rcPresetLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            PoolManager::PoolPreset* pActivePreset = PoolManager::GetActivePreset();
            std::wstring activePresetName = pActivePreset ? pActivePreset->presetName : L"Default Preset";

            int btnH = 30;
            int btnY = toolbarY + (toolbarH - btnH) / 2;

            // [+ Add Pool] and [Expand All] / [Collapse All] Buttons (Right cluster)
            int addPoolW = (w < 880) ? 84 : 96;
            int expandAllW = (w < 880) ? 80 : 92;
            int collapseAllW = (w < 880) ? 84 : 96;
            int curRight = w - 20;

            RECT rcBtnAddPool = { curRight - addPoolW, btnY, curRight, btnY + btnH };
            ClickableControl ccAddPool = { ClickableControl::BTN_ADD_POOL, rcBtnAddPool };
            int idxAddPool = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccAddPool);
            DrawModernButton(hmemDC, rcBtnAddPool, (w < 880) ? L"Add" : L"Add Pool", pState->hoveredControlIdx == idxAddPool, pState->pressedControlIdx == idxAddPool, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE710");

            curRight -= (addPoolW + 6);
            RECT rcBtnExpAll = { curRight - expandAllW, btnY, curRight, btnY + btnH };
            ClickableControl ccExpAll = { ClickableControl::BTN_POOLS_EXPAND_ALL, rcBtnExpAll };
            int idxExpAll = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccExpAll);
            DrawModernButton(hmemDC, rcBtnExpAll, (w < 880) ? L"Expand" : L"Expand All", pState->hoveredControlIdx == idxExpAll, pState->pressedControlIdx == idxExpAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

            curRight -= (expandAllW + 6);
            RECT rcBtnColAll = { curRight - collapseAllW, btnY, curRight, btnY + btnH };
            ClickableControl ccColAll = { ClickableControl::BTN_POOLS_COLLAPSE_ALL, rcBtnColAll };
            int idxColAll = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccColAll);
            DrawModernButton(hmemDC, rcBtnColAll, (w < 880) ? L"Collapse" : L"Collapse All", pState->hoveredControlIdx == idxColAll, pState->pressedControlIdx == idxColAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

            int rightClusterLeft = curRight - 12;

            // Preset Dropdown and Action Buttons (Left cluster)
            int comboMaxW = 180;
            int availForLeft = rightClusterLeft - 70;
            int comboW = (availForLeft < 480) ? (std::max)(110, availForLeft - 260) : comboMaxW;
            RECT rcCombo = { 70, toolbarY + 9, 70 + comboW, toolbarY + toolbarH - 9 };
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
            int btnNewW = (availForLeft < 480) ? 52 : 64;
            int btnRenameW = (availForLeft < 480) ? 68 : 84;
            int btnCloneW = (availForLeft < 480) ? 60 : 74;
            int btnDelW = (availForLeft < 480) ? 60 : 74;

            if (btnX + btnNewW <= rightClusterLeft)
            {
                RECT rcBtnNew = { btnX, btnY, btnX + btnNewW, btnY + btnH };
                ClickableControl ccNew = { ClickableControl::BTN_PRESET_NEW, rcBtnNew };
                int idxNew = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccNew);
                DrawModernButton(hmemDC, rcBtnNew, L"New", pState->hoveredControlIdx == idxNew, pState->pressedControlIdx == idxNew, false, pState->hFontMain, pState->hFontIconSmall, L"\xE710");
                btnX += btnNewW + 6;
            }

            if (btnX + btnRenameW <= rightClusterLeft)
            {
                RECT rcBtnRename = { btnX, btnY, btnX + btnRenameW, btnY + btnH };
                ClickableControl ccRename = { ClickableControl::BTN_PRESET_RENAME, rcBtnRename };
                int idxRename = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccRename);
                DrawModernButton(hmemDC, rcBtnRename, L"Rename", pState->hoveredControlIdx == idxRename, pState->pressedControlIdx == idxRename, false, pState->hFontMain, pState->hFontIconSmall, L"\xE70F");
                btnX += btnRenameW + 6;
            }

            if (btnX + btnCloneW <= rightClusterLeft)
            {
                RECT rcBtnClone = { btnX, btnY, btnX + btnCloneW, btnY + btnH };
                ClickableControl ccClone = { ClickableControl::BTN_PRESET_CLONE, rcBtnClone };
                int idxClone = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccClone);
                DrawModernButton(hmemDC, rcBtnClone, L"Clone", pState->hoveredControlIdx == idxClone, pState->pressedControlIdx == idxClone, false, pState->hFontMain, pState->hFontIconSmall, L"\xE8C8");
                btnX += btnCloneW + 6;
            }

            if (btnX + btnDelW <= rightClusterLeft)
            {
                RECT rcBtnDel = { btnX, btnY, btnX + btnDelW, btnY + btnH };
                ClickableControl ccDel = { ClickableControl::BTN_PRESET_DELETE, rcBtnDel };
                int idxDel = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccDel);
                DrawModernButton(hmemDC, rcBtnDel, L"Delete", pState->hoveredControlIdx == idxDel, pState->pressedControlIdx == idxDel, false, pState->hFontMain, pState->hFontIconSmall, L"\xE74D");
            }

            // 2. Scrollable Content Area: Pool Cards
            int contentY = toolbarY + toolbarH + 1;
            int footerH = 56;
            int contentH = h - footerH - contentY;
            RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

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

            HRGN hRgnClip = CreateRectRgn(rcContentClip.left, rcContentClip.top, rcContentClip.right, rcContentClip.bottom);
            SelectClipRgn(hmemDC, hRgnClip);

            int cardY = contentY - pState->scrollY + 8;
            int cardMargin = 20;
            int cardW = w - (cardMargin * 2) - 14;

            if (pActivePreset && !pActivePreset->pools.empty())
            {
                for (size_t p = 0; p < pActivePreset->pools.size(); ++p)
                {
                    const auto& pool = pActivePreset->pools[p];
                    int unitCount = (int)pool.units.size();
                    int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
                    int cardH = pool.isCollapsed ? 33 : (92 + unitsAreaH + 40);
                    RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

                    bool isHoverPool = (pState->dragOverPoolIdx == (int)p);
                    COLORREF cBg = isHoverPool ? RGB(32, 40, 52) : PoolTheme::CardBackground;
                    COLORREF cBorder = isHoverPool ? accentCol : PoolTheme::CardBorder;

                    HBRUSH hbrCard = CreateSolidBrush(cBg);
                    HPEN hpenCard = CreatePen(PS_SOLID, 1, cBorder);
                    HBRUSH holdBr1 = (HBRUSH)SelectObject(hmemDC, hbrCard);
                    HPEN holdPen1 = (HPEN)SelectObject(hmemDC, hpenCard);
                    RoundRect(hmemDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 10, 10);
                    SelectObject(hmemDC, holdBr1);
                    SelectObject(hmemDC, holdPen1);
                    DeleteObject(hbrCard);
                    DeleteObject(hpenCard);

                    RECT rcCardHeader = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 33 };
                    WizardDlgState::CardHit ch;
                    ch.poolIdx = (int)p;
                    ch.rcCard = rcCard;
                    ch.rcHeader = rcCardHeader;
                    pState->cardHits.push_back(ch);

                    // Header collapse chevron
                    RECT rcChevronBtn = { rcCard.left + 8, rcCard.top + 4, rcCard.left + 30, rcCard.top + 28 };
                    ClickableControl ccColToggle = { ClickableControl::BTN_POOL_COLLAPSE_TOGGLE, rcChevronBtn, (int)p, -1 };
                    int idxCol = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccColToggle);
                    DrawModernButton(hmemDC, rcChevronBtn, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontSmall, pState->hFontIconSmall, pool.isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // Pool Number Badge & Name
                    RECT rcPoolTitle = { rcCard.left + 36, rcCard.top, rcCard.right - 252, rcCard.top + 33 };
                    std::wstring titleText = L"#" + std::to_wstring(p + 1) + L"  " + pool.name;
                    SelectObject(hmemDC, pState->hFontMainBold);
                    SetTextColor(hmemDC, textPrimary);
                    DrawTextW(hmemDC, titleText.c_str(), -1, &rcPoolTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // Header right buttons: [Rename], [Clone], [▲ Up], [▼ Down], [🗑️ Delete]
                    int hBtnR = rcCard.right - 8;
                    int hBtnY = rcCard.top + 5;
                    int hBtnH = 24;

                    hBtnR -= 32;
                    RECT rcBtnPDel = { hBtnR, hBtnY, hBtnR + 28, hBtnY + hBtnH };
                    ClickableControl ccPDel = { ClickableControl::BTN_POOL_DELETE, rcBtnPDel, (int)p, -1 };
                    int idxPDel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDel);
                    DrawModernButton(hmemDC, rcBtnPDel, L"", pState->hoveredControlIdx == idxPDel, pState->pressedControlIdx == idxPDel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    hBtnR -= 28;
                    RECT rcBtnPDn = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    ClickableControl ccPDn = { ClickableControl::BTN_POOL_DOWN, rcBtnPDn, (int)p, -1 };
                    int idxPDn = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPDn);
                    DrawModernButton(hmemDC, rcBtnPDn, L"", pState->hoveredControlIdx == idxPDn, pState->pressedControlIdx == idxPDn, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

                    hBtnR -= 28;
                    RECT rcBtnPUp = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    ClickableControl ccPUp = { ClickableControl::BTN_POOL_UP, rcBtnPUp, (int)p, -1 };
                    int idxPUp = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPUp);
                    DrawModernButton(hmemDC, rcBtnPUp, L"", pState->hoveredControlIdx == idxPUp, pState->pressedControlIdx == idxPUp, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

                    hBtnR -= 70;
                    RECT rcBtnPClone = { hBtnR, hBtnY, hBtnR + 66, hBtnY + hBtnH };
                    ClickableControl ccPClone = { ClickableControl::BTN_POOL_CLONE, rcBtnPClone, (int)p, -1 };
                    int idxPClone = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPClone);
                    DrawModernButton(hmemDC, rcBtnPClone, L"Clone", pState->hoveredControlIdx == idxPClone, pState->pressedControlIdx == idxPClone, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                    hBtnR -= 80;
                    RECT rcBtnPRename = { hBtnR, hBtnY, hBtnR + 76, hBtnY + hBtnH };
                    ClickableControl ccPRename = { ClickableControl::BTN_POOL_RENAME, rcBtnPRename, (int)p, -1 };
                    int idxPRename = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccPRename);
                    DrawModernButton(hmemDC, rcBtnPRename, L"Rename", pState->hoveredControlIdx == idxPRename, pState->pressedControlIdx == idxPRename, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                    if (!pool.isCollapsed)
                    {
                        // Settings row (Y = rcCard.top + 38)
                        int setY = rcCard.top + 38;

                        // Pick Mode Toggle
                        RECT rcMode = { rcCard.left + 16, setY, rcCard.left + 130, setY + 26 };
                        ClickableControl ccMode = { ClickableControl::BTN_POOL_MODE_TOGGLE, rcMode, (int)p, -1 };
                        int idxMode = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMode);
                        const wchar_t* modeStr = (pool.pickMode == PoolManager::PoolPickMode::Random) ? L"Mode: Random" : L"Mode: Order";
                        DrawModernButton(hmemDC, rcMode, modeStr, pState->hoveredControlIdx == idxMode, pState->pressedControlIdx == idxMode, false, pState->hFontSmall, pState->hFontIconSmall, (pool.pickMode == PoolManager::PoolPickMode::Random) ? L"\xE8B9" : L"\xE8D7");

                        // Min Count: [-] [val] [+]
                        int curX = rcCard.left + 144;
                        SelectObject(hmemDC, pState->hFontSmall);
                        SetTextColor(hmemDC, textSecondary);
                        RECT rcMinLbl = { curX, setY, curX + 32, setY + 26 };
                        DrawTextW(hmemDC, L"Min:", -1, &rcMinLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        curX += 32;

                        RECT rcMinDec = { curX, setY, curX + 24, setY + 26 };
                        ClickableControl ccMinDec = { ClickableControl::BTN_POOL_MIN_DEC, rcMinDec, (int)p, -1 };
                        int idxMinDec = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinDec);
                        DrawModernButton(hmemDC, rcMinDec, L"-", pState->hoveredControlIdx == idxMinDec, pState->pressedControlIdx == idxMinDec, false, pState->hFontSmall);
                        curX += 26;

                        RECT rcMinVal = { curX, setY, curX + 36, setY + 26 };
                        ClickableControl ccMinVal = { ClickableControl::BTN_POOL_MIN_EDIT, rcMinVal, (int)p, -1 };
                        int idxMinVal = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinVal);
                        DrawModernButton(hmemDC, rcMinVal, std::to_wstring(pool.minCount).c_str(), pState->hoveredControlIdx == idxMinVal, pState->pressedControlIdx == idxMinVal, false, pState->hFontMainBold);
                        curX += 38;

                        RECT rcMinInc = { curX, setY, curX + 24, setY + 26 };
                        ClickableControl ccMinInc = { ClickableControl::BTN_POOL_MIN_INC, rcMinInc, (int)p, -1 };
                        int idxMinInc = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMinInc);
                        DrawModernButton(hmemDC, rcMinInc, L"+", pState->hoveredControlIdx == idxMinInc, pState->pressedControlIdx == idxMinInc, false, pState->hFontSmall);
                        curX += 34;

                        // Max Count: [-] [val] [+]
                        RECT rcMaxLbl = { curX, setY, curX + 34, setY + 26 };
                        DrawTextW(hmemDC, L"Max:", -1, &rcMaxLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        curX += 34;

                        RECT rcMaxDec = { curX, setY, curX + 24, setY + 26 };
                        ClickableControl ccMaxDec = { ClickableControl::BTN_POOL_MAX_DEC, rcMaxDec, (int)p, -1 };
                        int idxMaxDec = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxDec);
                        DrawModernButton(hmemDC, rcMaxDec, L"-", pState->hoveredControlIdx == idxMaxDec, pState->pressedControlIdx == idxMaxDec, false, pState->hFontSmall);
                        curX += 26;

                        RECT rcMaxVal = { curX, setY, curX + 36, setY + 26 };
                        ClickableControl ccMaxVal = { ClickableControl::BTN_POOL_MAX_EDIT, rcMaxVal, (int)p, -1 };
                        int idxMaxVal = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxVal);
                        DrawModernButton(hmemDC, rcMaxVal, std::to_wstring(pool.maxCount).c_str(), pState->hoveredControlIdx == idxMaxVal, pState->pressedControlIdx == idxMaxVal, false, pState->hFontMainBold);
                        curX += 38;

                        RECT rcMaxInc = { curX, setY, curX + 24, setY + 26 };
                        ClickableControl ccMaxInc = { ClickableControl::BTN_POOL_MAX_INC, rcMaxInc, (int)p, -1 };
                        int idxMaxInc = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccMaxInc);
                        DrawModernButton(hmemDC, rcMaxInc, L"+", pState->hoveredControlIdx == idxMaxInc, pState->pressedControlIdx == idxMaxInc, false, pState->hFontSmall);
                        curX += 38;

                        // Flip Policy Button
                        RECT rcFlipPol = { curX, setY, curX + 138, setY + 26 };
                        ClickableControl ccFlipPol = { ClickableControl::BTN_POOL_FLIP_TOGGLE, rcFlipPol, (int)p, -1 };
                        int idxFlipPol = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccFlipPol);
                        const wchar_t* flipText = L"Flip: Forward";
                        if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AllowRandom) flipText = L"Flip: Random";
                        else if (pool.flipPolicy == PoolManager::PoolFlipPolicy::AlwaysFlipped) flipText = L"Flip: Reversed";
                        DrawModernButton(hmemDC, rcFlipPol, flipText, pState->hoveredControlIdx == idxFlipPol, pState->pressedControlIdx == idxFlipPol, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE745");

                        // Units Area Box
                        int boxY = setY + 34;
                        RECT rcUnitsBox = { rcCard.left + 14, boxY, rcCard.right - 14, boxY + unitsAreaH };

                        HBRUSH hbrUBox = CreateSolidBrush(PoolTheme::UnitsBoxBackground);
                        HPEN hpenUBox = CreatePen(PS_SOLID, 1, PoolTheme::UnitsBoxBorder);
                        SelectObject(hmemDC, hbrUBox);
                        SelectObject(hmemDC, hpenUBox);
                        RoundRect(hmemDC, rcUnitsBox.left, rcUnitsBox.top, rcUnitsBox.right, rcUnitsBox.bottom, 6, 6);
                        DeleteObject(hbrUBox);
                        DeleteObject(hpenUBox);

                        WizardDlgState::PoolUnitsBoxHit pbh;
                        pbh.poolIdx = (int)p;
                        pbh.rcUnitsBox = rcUnitsBox;
                        pState->poolUnitsBoxHits.push_back(pbh);

                        if (unitCount == 0)
                        {
                            SelectObject(hmemDC, pState->hFontSmall);
                            SetTextColor(hmemDC, PoolTheme::TextMuted);
                            DrawTextW(hmemDC, L"Drag and drop rolling stock units here from Stock Library or click Paste below", -1, &rcUnitsBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }
                        else
                        {
                            int chipColW = (rcUnitsBox.right - rcUnitsBox.left - 24) / 2;
                            int chipH = 24;
                            for (int u = 0; u < unitCount; ++u)
                            {
                                const auto& unit = pool.units[u];
                                int col = u % 2;
                                int row = u / 2;
                                int chipX = rcUnitsBox.left + 8 + col * (chipColW + 8);
                                int chipY = rcUnitsBox.top + 8 + row * (chipH + 4);
                                RECT rcChip = { chipX, chipY, chipX + chipColW, chipY + chipH };

                                WizardDlgState::UnitChipHit uch;
                                uch.poolIdx = (int)p;
                                uch.unitIdx = u;
                                uch.rcChip = rcChip;
                                pState->unitChipHits.push_back(uch);

                                bool isSel = (pState->selectedPoolIdx == (int)p && pState->selectedUnitIndices.count(u) > 0);
                                COLORREF chipBg = isSel ? RGB(0, 90, 160) : PoolTheme::ChipBackground;
                                COLORREF chipBdr = isSel ? RGB(0, 160, 255) : PoolTheme::ChipBorder;

                                HBRUSH hbrChip = CreateSolidBrush(chipBg);
                                HPEN hpenChip = CreatePen(PS_SOLID, 1, chipBdr);
                                SelectObject(hmemDC, hbrChip);
                                SelectObject(hmemDC, hpenChip);
                                RoundRect(hmemDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                                DeleteObject(hbrChip);
                                DeleteObject(hpenChip);

                                // Unit Icon
                                SelectObject(hmemDC, pState->hFontIconSmall);
                                SetTextColor(hmemDC, isSel ? RGB(255, 255, 255) : (unit.isEngine ? RGB(96, 205, 255) : RGB(220, 180, 100)));
                                RECT rcUIcon = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                DrawTextW(hmemDC, unit.isEngine ? L"\xE7C0" : L"\xE707", -1, &rcUIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                // Unit Name
                                SelectObject(hmemDC, pState->hFontSmall);
                                SetTextColor(hmemDC, RGB(245, 245, 245));
                                RECT rcUName = { rcChip.left + 24, rcChip.top, rcChip.right - 80, rcChip.bottom };
                                DrawTextW(hmemDC, unit.szFileName.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                // Unit Flip Mode Pill
                                RECT rcFlipPill = { rcChip.right - 76, rcChip.top + 2, rcChip.right - 24, rcChip.bottom - 2 };
                                ClickableControl ccUFlip = { ClickableControl::BTN_UNIT_FLIP_TOGGLE, rcFlipPill, (int)p, u };
                                int idxUFlip = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccUFlip);
                                const wchar_t* ufStr = L"▲ FWD";
                                if (unit.flipMode == PoolManager::UnitFlipMode::Flipped) ufStr = L"▼ FLIP";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Random) ufStr = L"🔀 RND";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Auto) ufStr = L"Auto";
                                DrawModernButton(hmemDC, rcFlipPill, ufStr, pState->hoveredControlIdx == idxUFlip, pState->pressedControlIdx == idxUFlip, false, pState->hFontBadge);

                                // Unit Remove [✕]
                                RECT rcRem = { rcChip.right - 22, rcChip.top + 2, rcChip.right - 4, rcChip.bottom - 2 };
                                ClickableControl ccRem = { ClickableControl::BTN_UNIT_REMOVE, rcRem, (int)p, u };
                                int idxRem = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccRem);
                                DrawModernButton(hmemDC, rcRem, L"✕", pState->hoveredControlIdx == idxRem, pState->pressedControlIdx == idxRem, false, pState->hFontBadge);
                            }
                        }

                        // Pool Action Toolbar (Below Units Box)
                        int actY = boxY + unitsAreaH + 6;
                        int actX = rcCard.left + 14;

                        RECT rcPaste = { actX, actY, actX + 110, actY + 24 };
                        ClickableControl ccPaste = { ClickableControl::BTN_POOL_PASTE_CLIPBOARD, rcPaste, (int)p, -1 };
                        int idxPaste = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccPaste);
                        DrawModernButton(hmemDC, rcPaste, L"Paste Units", pState->hoveredControlIdx == idxPaste, pState->pressedControlIdx == idxPaste, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");

                        actX += 116;
                        RECT rcCopyAll = { actX, actY, actX + 90, actY + 24 };
                        ClickableControl ccCopyAll = { ClickableControl::BTN_POOL_COPY_UNITS, rcCopyAll, (int)p, -1 };
                        int idxCopyAll = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccCopyAll);
                        DrawModernButton(hmemDC, rcCopyAll, L"Copy All", pState->hoveredControlIdx == idxCopyAll, pState->pressedControlIdx == idxCopyAll, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                        actX += 96;
                        RECT rcClear = { actX, actY, actX + 88, actY + 24 };
                        ClickableControl ccClear = { ClickableControl::BTN_POOL_CLEAR_UNITS, rcClear, (int)p, -1 };
                        int idxClear = (int)pState->clickControls.size();
                        pState->clickControls.push_back(ccClear);
                        DrawModernButton(hmemDC, rcClear, L"Clear All", pState->hoveredControlIdx == idxClear, pState->pressedControlIdx == idxClear, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE75C");
                    }

                    cardY += cardH + 14;
                }
            }

            SelectClipRgn(hmemDC, NULL);
            DeleteObject(hRgnClip);

            // 3. Tab 0 Footer Bar (Y = h - footerH to h)
            RECT rcFooter = { 0, h - footerH, w, h };
            HBRUSH hbrFoot = CreateSolidBrush(PoolTheme::FooterBackground);
            FillRect(hmemDC, &rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            HPEN hPenDiv = CreatePen(PS_SOLID, 1, borderCol);
            SelectObject(hmemDC, hPenDiv);
            MoveToEx(hmemDC, 0, h - footerH, NULL);
            LineTo(hmemDC, w, h - footerH);
            DeleteObject(hPenDiv);

            // [Consist Generator Wizard...] button
            RECT rcGen = { 20, h - footerH + 11, 280, h - 13 };
            ClickableControl ccGen = { ClickableControl::BTN_GENERATE, rcGen };
            int idxGen = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccGen);
            DrawModernButton(hmemDC, rcGen, L"Consist Generator Wizard...", pState->hoveredControlIdx == idxGen, pState->pressedControlIdx == idxGen, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE77F");

            // [Close] button
            RECT rcClose = { w - 110, h - footerH + 11, w - 20, h - 13 };
            ClickableControl ccClose = { ClickableControl::BTN_CLOSE, rcClose };
            int idxClose = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccClose);
            DrawModernButton(hmemDC, rcClose, L"Close", pState->hoveredControlIdx == idxClose, pState->pressedControlIdx == idxClose, false, pState->hFontMain);
        }
        // =========================================================================
        // TAB 1: UNIT REPLACEMENT GROUPS (Palettes / Batches)
        // =========================================================================
        else
        {
            // 1. Replacement Groups Toolbar (Y = 66 to 112, Height = 46px)
            int toolbarY = 66;
            int toolbarH = 46;
            RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
            HBRUSH hbrTb = CreateSolidBrush(PoolTheme::ToolbarBackground);
            FillRect(hmemDC, &rcToolbar, hbrTb);
            DeleteObject(hbrTb);

            HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenLine);
            MoveToEx(hmemDC, 0, toolbarY + toolbarH, NULL);
            LineTo(hmemDC, w, toolbarY + toolbarH);

            // Right: [+ Add Group], [Expand All], [Collapse All] Buttons
            int btnH = 30;
            int btnY = toolbarY + (toolbarH - btnH) / 2;
            int addGroupW = (w < 880) ? 90 : 104;
            int expandAllW = (w < 880) ? 80 : 92;
            int collapseAllW = (w < 880) ? 84 : 96;
            int curRight = w - 20;

            RECT rcBtnAddGrp = { curRight - addGroupW, btnY, curRight, btnY + btnH };
            ClickableControl ccAddGrp = { ClickableControl::BTN_GROUP_NEW, rcBtnAddGrp };
            int idxAddGrp = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccAddGrp);
            DrawModernButton(hmemDC, rcBtnAddGrp, (w < 880) ? L"Add Group" : L"Add Group", pState->hoveredControlIdx == idxAddGrp, pState->pressedControlIdx == idxAddGrp, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE710");

            curRight -= (addGroupW + 6);
            RECT rcBtnExpAll = { curRight - expandAllW, btnY, curRight, btnY + btnH };
            ClickableControl ccExpAll = { ClickableControl::BTN_GROUP_EXPAND_ALL, rcBtnExpAll };
            int idxExpAll = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccExpAll);
            DrawModernButton(hmemDC, rcBtnExpAll, (w < 880) ? L"Expand" : L"Expand All", pState->hoveredControlIdx == idxExpAll, pState->pressedControlIdx == idxExpAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

            curRight -= (expandAllW + 6);
            RECT rcBtnColAll = { curRight - collapseAllW, btnY, curRight, btnY + btnH };
            ClickableControl ccColAll = { ClickableControl::BTN_GROUP_COLLAPSE_ALL, rcBtnColAll };
            int idxColAll = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccColAll);
            DrawModernButton(hmemDC, rcBtnColAll, (w < 880) ? L"Collapse" : L"Collapse All", pState->hoveredControlIdx == idxColAll, pState->pressedControlIdx == idxColAll, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

            int maxTitleRight = curRight - 12;

            // Left: Icon + Title + Subtitle
            SelectObject(hmemDC, pState->hFontIcon);
            SetTextColor(hmemDC, accentCol);
            RECT rcIcon = { 18, toolbarY, 44, toolbarY + toolbarH };
            DrawTextW(hmemDC, L"\xE8D7", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hmemDC, pState->hFontMainBold);
            SetTextColor(hmemDC, textPrimary);
            RECT rcTitle = { 46, toolbarY + 5, maxTitleRight, toolbarY + 24 };
            DrawTextW(hmemDC, L"Unit Replacement Batches / Palettes", -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            SelectObject(hmemDC, pState->hFontSmall);
            SetTextColor(hmemDC, textSecondary);
            RECT rcSub = { 46, toolbarY + 24, maxTitleRight, toolbarY + 42 };
            DrawTextW(hmemDC, L"Curated stock groups for instant right-click unit replacement in Consist Editor", -1, &rcSub, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // 2. Scrollable Content Area: Group Cards
            int contentY = toolbarY + toolbarH + 1;
            int footerH = 56;
            int contentH = h - footerH - contentY;
            RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

            int totalContentH = 8;
            if (!PoolManager::g_ReplacementGroupsCache.empty())
            {
                for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
                {
                    const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
                    int unitCount = (int)grp.units.size();
                    int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
                    int cardH = grp.isCollapsed ? 34 : (46 + unitsAreaH + 16);
                    totalContentH += cardH + 14;
                }
            }
            else
            {
                totalContentH = 220;
            }
            totalContentH += 10;

            RECT rcScroll = { w - 12, contentY + 2, w - 2, contentY + contentH - 2 };
            pState->m_vScroll.SetBounds(rcScroll);
            pState->m_vScroll.SetRange(0, totalContentH - 1, contentH);
            pState->scrollY = pState->m_vScroll.GetPos();

            HRGN hRgnClip = CreateRectRgn(rcContentClip.left, rcContentClip.top, rcContentClip.right, rcContentClip.bottom);
            SelectClipRgn(hmemDC, hRgnClip);

            int cardY = contentY - pState->scrollY + 8;
            int cardMargin = 20;
            int cardW = w - (cardMargin * 2) - 14;

            if (PoolManager::g_ReplacementGroupsCache.empty())
            {
                // Sleek Empty State Card
                RECT rcEmpty = { cardMargin + 40, contentY + 40, cardMargin + cardW - 40, contentY + 220 };
                HBRUSH hbrEmpty = CreateSolidBrush(RGB(24, 24, 24));
                HPEN hpenEmpty = CreatePen(PS_SOLID, 1, RGB(42, 42, 42));
                SelectObject(hmemDC, hbrEmpty);
                SelectObject(hmemDC, hpenEmpty);
                RoundRect(hmemDC, rcEmpty.left, rcEmpty.top, rcEmpty.right, rcEmpty.bottom, 12, 12);
                DeleteObject(hbrEmpty);
                DeleteObject(hpenEmpty);

                SelectObject(hmemDC, pState->hFontIcon);
                SetTextColor(hmemDC, RGB(100, 180, 255));
                RECT rcEIcon = { rcEmpty.left, rcEmpty.top + 24, rcEmpty.right, rcEmpty.top + 56 };
                DrawTextW(hmemDC, L"\xE8D7", -1, &rcEIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hmemDC, pState->hFontTitle);
                SetTextColor(hmemDC, RGB(255, 255, 255));
                RECT rcETitle = { rcEmpty.left + 20, rcEmpty.top + 60, rcEmpty.right - 20, rcEmpty.top + 86 };
                DrawTextW(hmemDC, L"No Unit Replacement Groups Defined", -1, &rcETitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hmemDC, pState->hFontSmall);
                SetTextColor(hmemDC, RGB(160, 160, 160));
                RECT rcEDesc = { rcEmpty.left + 30, rcEmpty.top + 90, rcEmpty.right - 30, rcEmpty.top + 130 };
                DrawTextW(hmemDC, L"Create favorite batches or palettes (e.g. 'WAP-7 Locomotives', 'LHB AC Coaches') to replace selected consist units with a single right-click.", -1, &rcEDesc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

                RECT rcEBtn = { (rcEmpty.left + rcEmpty.right) / 2 - 100, rcEmpty.top + 138, (rcEmpty.left + rcEmpty.right) / 2 + 100, rcEmpty.top + 170 };
                ClickableControl ccEAdd = { ClickableControl::BTN_GROUP_NEW, rcEBtn };
                int idxEAdd = (int)pState->clickControls.size();
                pState->clickControls.push_back(ccEAdd);
                DrawModernButton(hmemDC, rcEBtn, L"Add First Group", pState->hoveredControlIdx == idxEAdd, pState->pressedControlIdx == idxEAdd, true, pState->hFontMainBold, pState->hFontIconSmall, L"\xE710");
            }
            else
            {
                for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
                {
                    const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
                    int unitCount = (int)grp.units.size();
                    int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
                    int cardH = grp.isCollapsed ? 34 : (46 + unitsAreaH + 16);
                    RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

                    bool isHoverGroup = (pState->dragOverGroupIdx == (int)g);
                    COLORREF cBg = isHoverGroup ? RGB(32, 40, 52) : PoolTheme::CardBackground;
                    COLORREF cBorder = isHoverGroup ? accentCol : PoolTheme::CardBorder;

                    HBRUSH hbrCard = CreateSolidBrush(cBg);
                    HPEN hpenCard = CreatePen(PS_SOLID, 1, cBorder);
                    HBRUSH holdBr1 = (HBRUSH)SelectObject(hmemDC, hbrCard);
                    HPEN holdPen1 = (HPEN)SelectObject(hmemDC, hpenCard);
                    RoundRect(hmemDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 10, 10);
                    SelectObject(hmemDC, holdBr1);
                    SelectObject(hmemDC, holdPen1);
                    DeleteObject(hbrCard);
                    DeleteObject(hpenCard);

                    RECT rcCardHeader = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 34 };
                    WizardDlgState::GroupCardHit gch;
                    gch.groupIdx = (int)g;
                    gch.rcCard = rcCard;
                    gch.rcHeader = rcCardHeader;
                    pState->groupCardHits.push_back(gch);

                    // Header collapse chevron
                    RECT rcChevronBtn = { rcCard.left + 8, rcCard.top + 5, rcCard.left + 30, rcCard.top + 29 };
                    ClickableControl ccColToggle = { ClickableControl::BTN_GROUP_COLLAPSE_TOGGLE, rcChevronBtn, (int)g, -1 };
                    int idxCol = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccColToggle);
                    DrawModernButton(hmemDC, rcChevronBtn, L"", pState->hoveredControlIdx == idxCol, pState->pressedControlIdx == idxCol, false, pState->hFontSmall, pState->hFontIconSmall, grp.isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // Group Name & Unit Count Badge
                    RECT rcGroupTitle = { rcCard.left + 36, rcCard.top, rcCard.right - 302, rcCard.top + 34 };
                    std::wstring titleText = grp.name + L"  (" + std::to_wstring(unitCount) + (unitCount == 1 ? L" unit)" : L" units)");
                    SelectObject(hmemDC, pState->hFontMainBold);
                    SetTextColor(hmemDC, textPrimary);
                    DrawTextW(hmemDC, titleText.c_str(), -1, &rcGroupTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // Header right buttons: [📋 Paste], [🗑️ Clear], [Rename], [Delete]
                    int hBtnR = rcCard.right - 8;
                    int hBtnY = rcCard.top + 5;
                    int hBtnH = 24;

                    hBtnR -= 32;
                    RECT rcBtnGDel = { hBtnR, hBtnY, hBtnR + 28, hBtnY + hBtnH };
                    ClickableControl ccGDel = { ClickableControl::BTN_GROUP_DELETE, rcBtnGDel, (int)g, -1 };
                    int idxGDel = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccGDel);
                    DrawModernButton(hmemDC, rcBtnGDel, L"", pState->hoveredControlIdx == idxGDel, pState->pressedControlIdx == idxGDel, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    hBtnR -= 80;
                    RECT rcBtnGRename = { hBtnR, hBtnY, hBtnR + 76, hBtnY + hBtnH };
                    ClickableControl ccGRename = { ClickableControl::BTN_GROUP_RENAME, rcBtnGRename, (int)g, -1 };
                    int idxGRename = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccGRename);
                    DrawModernButton(hmemDC, rcBtnGRename, L"Rename", pState->hoveredControlIdx == idxGRename, pState->pressedControlIdx == idxGRename, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                    hBtnR -= 76;
                    RECT rcBtnGClear = { hBtnR, hBtnY, hBtnR + 72, hBtnY + hBtnH };
                    ClickableControl ccGClear = { ClickableControl::BTN_GROUP_CLEAR_UNITS, rcBtnGClear, (int)g, -1 };
                    int idxGClear = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccGClear);
                    DrawModernButton(hmemDC, rcBtnGClear, L"Clear", pState->hoveredControlIdx == idxGClear, pState->pressedControlIdx == idxGClear, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE75C");

                    hBtnR -= 100;
                    RECT rcBtnGPaste = { hBtnR, hBtnY, hBtnR + 96, hBtnY + hBtnH };
                    ClickableControl ccGPaste = { ClickableControl::BTN_GROUP_PASTE_CLIPBOARD, rcBtnGPaste, (int)g, -1 };
                    int idxGPaste = (int)pState->clickControls.size();
                    pState->clickControls.push_back(ccGPaste);
                    DrawModernButton(hmemDC, rcBtnGPaste, L"Paste Units", pState->hoveredControlIdx == idxGPaste, pState->pressedControlIdx == idxGPaste, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");

                    if (!grp.isCollapsed)
                    {
                        // Units Area Box (Simple & Rule-Free)
                        int boxY = rcCard.top + 38;
                        RECT rcUnitsBox = { rcCard.left + 14, boxY, rcCard.right - 14, boxY + unitsAreaH };

                        HBRUSH hbrUBox = CreateSolidBrush(PoolTheme::UnitsBoxBackground);
                        HPEN hpenUBox = CreatePen(PS_SOLID, 1, PoolTheme::UnitsBoxBorder);
                        SelectObject(hmemDC, hbrUBox);
                        SelectObject(hmemDC, hpenUBox);
                        RoundRect(hmemDC, rcUnitsBox.left, rcUnitsBox.top, rcUnitsBox.right, rcUnitsBox.bottom, 6, 6);
                        DeleteObject(hbrUBox);
                        DeleteObject(hpenUBox);

                        WizardDlgState::GroupUnitsBoxHit gbh;
                        gbh.groupIdx = (int)g;
                        gbh.rcUnitsBox = rcUnitsBox;
                        pState->groupUnitsBoxHits.push_back(gbh);

                        if (unitCount == 0)
                        {
                            SelectObject(hmemDC, pState->hFontSmall);
                            SetTextColor(hmemDC, PoolTheme::TextMuted);
                            DrawTextW(hmemDC, L"Drag and drop rolling stock units here from Stock Library, or click [📋 Paste Units]", -1, &rcUnitsBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }
                        else
                        {
                            int chipColW = (rcUnitsBox.right - rcUnitsBox.left - 24) / 2;
                            int chipH = 24;
                            for (int u = 0; u < unitCount; ++u)
                            {
                                const auto& unit = grp.units[u];
                                int col = u % 2;
                                int row = u / 2;
                                int chipX = rcUnitsBox.left + 8 + col * (chipColW + 8);
                                int chipY = rcUnitsBox.top + 8 + row * (chipH + 4);
                                RECT rcChip = { chipX, chipY, chipX + chipColW, chipY + chipH };

                                WizardDlgState::GroupUnitChipHit guch;
                                guch.groupIdx = (int)g;
                                guch.unitIdx = u;
                                guch.rcChip = rcChip;
                                pState->groupUnitChipHits.push_back(guch);

                                bool isSel = (pState->selectedGroupIdx == (int)g && pState->selectedGroupUnitIndices.count(u) > 0);
                                COLORREF chipBg = isSel ? RGB(0, 90, 160) : PoolTheme::ChipBackground;
                                COLORREF chipBdr = isSel ? RGB(0, 160, 255) : PoolTheme::ChipBorder;

                                HBRUSH hbrChip = CreateSolidBrush(chipBg);
                                HPEN hpenChip = CreatePen(PS_SOLID, 1, chipBdr);
                                SelectObject(hmemDC, hbrChip);
                                SelectObject(hmemDC, hpenChip);
                                RoundRect(hmemDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                                DeleteObject(hbrChip);
                                DeleteObject(hpenChip);

                                // Unit Icon
                                SelectObject(hmemDC, pState->hFontIconSmall);
                                SetTextColor(hmemDC, isSel ? RGB(255, 255, 255) : (unit.isEngine ? RGB(96, 205, 255) : RGB(220, 180, 100)));
                                RECT rcUIcon = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                DrawTextW(hmemDC, unit.isEngine ? L"\xE7C0" : L"\xE707", -1, &rcUIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                // Unit Name & folder
                                SelectObject(hmemDC, pState->hFontSmall);
                                SetTextColor(hmemDC, RGB(245, 245, 245));
                                RECT rcUName = { rcChip.left + 24, rcChip.top, rcChip.right - 80, rcChip.bottom };
                                DrawTextW(hmemDC, unit.szFileName.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                // Unit Flip Mode Pill
                                RECT rcFlipPill = { rcChip.right - 76, rcChip.top + 2, rcChip.right - 24, rcChip.bottom - 2 };
                                ClickableControl ccUFlip = { ClickableControl::BTN_GROUP_UNIT_FLIP_TOGGLE, rcFlipPill, (int)g, u };
                                int idxUFlip = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccUFlip);
                                const wchar_t* ufStr = L"▲ FWD";
                                if (unit.flipMode == PoolManager::UnitFlipMode::Flipped) ufStr = L"▼ FLIP";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Random) ufStr = L"🔀 RND";
                                else if (unit.flipMode == PoolManager::UnitFlipMode::Auto) ufStr = L"▲ FWD";
                                DrawModernButton(hmemDC, rcFlipPill, ufStr, pState->hoveredControlIdx == idxUFlip, pState->pressedControlIdx == idxUFlip, false, pState->hFontBadge);

                                // Unit Remove [✕]
                                RECT rcRem = { rcChip.right - 22, rcChip.top + 2, rcChip.right - 4, rcChip.bottom - 2 };
                                ClickableControl ccRem = { ClickableControl::BTN_GROUP_UNIT_REMOVE, rcRem, (int)g, u };
                                int idxRem = (int)pState->clickControls.size();
                                pState->clickControls.push_back(ccRem);
                                DrawModernButton(hmemDC, rcRem, L"✕", pState->hoveredControlIdx == idxRem, pState->pressedControlIdx == idxRem, false, pState->hFontBadge);
                            }
                        }
                    }

                    cardY += cardH + 14;
                }
            }

            SelectClipRgn(hmemDC, NULL);
            DeleteObject(hRgnClip);

            // 3. Tab 1 Footer Bar (Y = h - footerH to h)
            RECT rcFooter = { 0, h - footerH, w, h };
            HBRUSH hbrFoot = CreateSolidBrush(PoolTheme::FooterBackground);
            FillRect(hmemDC, &rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            HPEN hPenDiv = CreatePen(PS_SOLID, 1, borderCol);
            SelectObject(hmemDC, hPenDiv);
            MoveToEx(hmemDC, 0, h - footerH, NULL);
            LineTo(hmemDC, w, h - footerH);
            DeleteObject(hPenDiv);

            // Left: Helpful context tip
            SelectObject(hmemDC, pState->hFontIconSmall);
            SetTextColor(hmemDC, accentCol);
            RECT rcTipIcon = { 20, h - footerH, 44, h };
            DrawTextW(hmemDC, L"\xE946", -1, &rcTipIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hmemDC, pState->hFontSmall);
            SetTextColor(hmemDC, textSecondary);
            RECT rcTipText = { 46, h - footerH, w - 130, h };
            DrawTextW(hmemDC, L"Tip: Right-click any selected unit(s) in Consist Editor to swap with these replacement groups directly.", -1, &rcTipText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Right: [Close] button
            RECT rcClose = { w - 110, h - footerH + 11, w - 20, h - 13 };
            ClickableControl ccClose = { ClickableControl::BTN_CLOSE, rcClose };
            int idxClose = (int)pState->clickControls.size();
            pState->clickControls.push_back(ccClose);
            DrawModernButton(hmemDC, rcClose, L"Close", pState->hoveredControlIdx == idxClose, pState->pressedControlIdx == idxClose, false, pState->hFontMain);
        }

        // Draw Vertical Scrollbar
        pState->m_vScroll.Paint(hmemDC);

        BitBlt(hdc, 0, 0, w, h, hmemDC, 0, 0, SRCCOPY);
        SelectObject(hmemDC, holdBm);
        DeleteObject(hbm);
        DeleteDC(hmemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (pState->m_vScroll.OnMouseMove(pt, hWnd))
        {
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        int newHovered = -1;
        for (int i = 0; i < (int)pState->clickControls.size(); ++i)
        {
            if (PtInRect(&pState->clickControls[i].rc, pt))
            {
                newHovered = i;
                break;
            }
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
        if (pState->repeatAction == ClickableControl::NONE)
            pState->pressedControlIdx = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            pState->m_vScroll.OnMouseWheel((short)delta, 3, hWnd);
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (pState && pState->m_vScroll.OnLButtonDown(pt, hWnd))
        {
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }



        // Unit chip clicks on Tab 0
        if (pState->activeTab == 0)
        {
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
                            if (!isCtrl) pState->selectedUnitIndices.clear();
                            for (int u = minU; u <= maxU; ++u) pState->selectedUnitIndices.insert(u);
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
        }
        // Unit chip clicks on Tab 1
        else
        {
            for (const auto& chip : pState->groupUnitChipHits)
            {
                if (PtInRect(&chip.rcChip, pt))
                {
                    bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    if (chip.groupIdx != pState->selectedGroupIdx)
                    {
                        pState->selectedGroupIdx = chip.groupIdx;
                        pState->selectedGroupUnitIndices.clear();
                        pState->selectedGroupUnitIndices.insert(chip.unitIdx);
                        pState->anchorGroupUnitIdx = chip.unitIdx;
                    }
                    else
                    {
                        if (isShift)
                        {
                            int start = (pState->anchorGroupUnitIdx >= 0) ? pState->anchorGroupUnitIdx : chip.unitIdx;
                            int minU = (std::min)(start, chip.unitIdx);
                            int maxU = (std::max)(start, chip.unitIdx);
                            if (!isCtrl) pState->selectedGroupUnitIndices.clear();
                            for (int u = minU; u <= maxU; ++u) pState->selectedGroupUnitIndices.insert(u);
                        }
                        else if (isCtrl)
                        {
                            if (pState->selectedGroupUnitIndices.count(chip.unitIdx) > 0)
                                pState->selectedGroupUnitIndices.erase(chip.unitIdx);
                            else
                                pState->selectedGroupUnitIndices.insert(chip.unitIdx);
                            pState->anchorGroupUnitIdx = chip.unitIdx;
                        }
                        else
                        {
                            pState->selectedGroupUnitIndices.clear();
                            pState->selectedGroupUnitIndices.insert(chip.unitIdx);
                            pState->anchorGroupUnitIdx = chip.unitIdx;
                        }
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        // Standard Button Clicks
        for (int i = 0; i < (int)pState->clickControls.size(); ++i)
        {
            if (PtInRect(&pState->clickControls[i].rc, pt))
            {
                pState->pressedControlIdx = i;
                const auto& ctrl = pState->clickControls[i];

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

        if (GetCapture() == hWnd) ReleaseCapture();

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
                {
                    if (PoolManager::g_PoolPresetsCache.empty()) break;
                    std::vector<DropDownItem> items;
                    for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
                    {
                        const auto& p = PoolManager::g_PoolPresetsCache[i];
                        int totalU = 0;
                        for (const auto& pl : p.pools) totalU += (int)pl.units.size();
                        std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
                        std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
                        bool isCurrent = ((int)i == PoolManager::g_ActivePresetIndex);
                        items.push_back(DropDownItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, isCurrent, true));
                    }
                    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, ctrl.rc, items, PoolManager::g_ActivePresetIndex + 1);
                    if (chosen > 0)
                    {
                        PoolManager::g_ActivePresetIndex = chosen - 1;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

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
                    if (newIdx >= 0) InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_POOL_UP:
                    if (PoolManager::MovePool(ctrl.poolIdx, ctrl.poolIdx - 1))
                        InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_POOL_DOWN:
                    if (PoolManager::MovePool(ctrl.poolIdx, ctrl.poolIdx + 1))
                        InvalidateRect(hWnd, NULL, FALSE);
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
                                if (pPreset->pools[poolIdx].maxCount < val) pPreset->pools[poolIdx].maxCount = val;
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
                                if (pPreset->pools[poolIdx].minCount > val) pPreset->pools[poolIdx].minCount = val;
                                PoolManager::PersistPoolPresets();
                                InvalidateRect(hWnd, NULL, FALSE);
                            } catch (...) {}
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_POOL_FLIP_TOGGLE:
                    PoolManager::CyclePoolFlipPolicy(ctrl.poolIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_UNIT_FLIP_TOGGLE:
                    PoolManager::CycleUnitFlipMode(ctrl.poolIdx, ctrl.unitIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;

                case ClickableControl::BTN_POOL_COPY_UNITS:
                    if (PoolManager::CopyPoolUnitsToClipboard(ctrl.poolIdx))
                        InvalidateRect(hWnd, NULL, FALSE);
                    break;

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

                // ================= TAB 1 ACTIONS (Unit Replacement Groups) =================
                case ClickableControl::BTN_GROUP_NEW:
                {
                    std::wstring defName = L"Group #" + std::to_wstring(PoolManager::g_ReplacementGroupsCache.size() + 1);
                    std::wstring groupName;
                    if (ShowModernInputPrompt(hWnd, L"Create Replacement Group", L"Enter a descriptive name for this group (e.g. 'WAP-7 Locomotives'):", defName, groupName))
                    {
                        if (!groupName.empty())
                        {
                            PoolManager::AddReplacementGroup(groupName);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_GROUP_RENAME:
                {
                    int gIdx = ctrl.poolIdx;
                    if (gIdx >= 0 && gIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        std::wstring curName = PoolManager::g_ReplacementGroupsCache[gIdx].name;
                        std::wstring newName;
                        if (ShowModernInputPrompt(hWnd, L"Rename Replacement Group", L"Enter new group name:", curName, newName))
                        {
                            if (!newName.empty())
                            {
                                PoolManager::RenameReplacementGroup(gIdx, newName);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    break;
                }

                case ClickableControl::BTN_GROUP_DELETE:
                {
                    int gIdx = ctrl.poolIdx;
                    PoolManager::RemoveReplacementGroup(gIdx);
                    pState->selectedGroupUnitIndices.clear();
                    pState->selectedGroupIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_GROUP_COLLAPSE_TOGGLE:
                {
                    int gIdx = ctrl.poolIdx;
                    if (gIdx >= 0 && gIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        PoolManager::g_ReplacementGroupsCache[gIdx].isCollapsed = !PoolManager::g_ReplacementGroupsCache[gIdx].isCollapsed;
                        PoolManager::PersistReplacementGroups();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                }

                case ClickableControl::BTN_GROUP_EXPAND_ALL:
                {
                    for (auto& grp : PoolManager::g_ReplacementGroupsCache) grp.isCollapsed = false;
                    PoolManager::PersistReplacementGroups();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_GROUP_COLLAPSE_ALL:
                {
                    for (auto& grp : PoolManager::g_ReplacementGroupsCache) grp.isCollapsed = true;
                    PoolManager::PersistReplacementGroups();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_GROUP_PASTE_CLIPBOARD:
                {
                    int gIdx = ctrl.poolIdx;
                    const auto& clipUnits = GetAppClipboardUnits();
                    if (!clipUnits.empty())
                    {
                        PoolManager::PasteUnitsToReplacementGroup(gIdx, clipUnits);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else
                    {
                        ShowModernMessageBox(hWnd, L"Clipboard is empty. Copy units from Stock Library or Consist Editor first, then click Paste.", L"Paste to Group", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }

                case ClickableControl::BTN_GROUP_CLEAR_UNITS:
                {
                    int gIdx = ctrl.poolIdx;
                    PoolManager::ClearReplacementGroupUnits(gIdx);
                    pState->selectedGroupUnitIndices.clear();
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_GROUP_UNIT_REMOVE:
                {
                    int gIdx = ctrl.poolIdx;
                    int uIdx = ctrl.unitIdx;
                    PoolManager::RemoveUnitFromReplacementGroup(gIdx, uIdx);
                    pState->selectedGroupUnitIndices.erase(uIdx);
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }

                case ClickableControl::BTN_GROUP_UNIT_FLIP_TOGGLE:
                {
                    int gIdx = ctrl.poolIdx;
                    int uIdx = ctrl.unitIdx;
                    if (gIdx >= 0 && gIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        auto& grp = PoolManager::g_ReplacementGroupsCache[gIdx];
                        if (uIdx >= 0 && uIdx < (int)grp.units.size())
                        {
                            auto& u = grp.units[uIdx];
                            switch (u.flipMode)
                            {
                            case PoolManager::UnitFlipMode::Auto:
                            case PoolManager::UnitFlipMode::Forward: u.flipMode = PoolManager::UnitFlipMode::Flipped; break;
                            case PoolManager::UnitFlipMode::Flipped: u.flipMode = PoolManager::UnitFlipMode::Random;  break;
                            case PoolManager::UnitFlipMode::Random:  u.flipMode = PoolManager::UnitFlipMode::Forward; break;
                            }
                            PoolManager::PersistReplacementGroups();
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    break;
                }

                // ================= COMMON ACTIONS =================
                case ClickableControl::BTN_CLOSE:
                    RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
                    DestroyWindow(hWnd);
                    break;

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

        if (pState->activeTab == 0)
        {
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
        }
        else
        {
            for (const auto& chip : pState->groupUnitChipHits)
            {
                if (PtInRect(&chip.rcChip, pt))
                {
                    if (pState->selectedGroupIdx != chip.groupIdx || pState->selectedGroupUnitIndices.count(chip.unitIdx) == 0)
                    {
                        pState->selectedGroupIdx = chip.groupIdx;
                        pState->selectedGroupUnitIndices.clear();
                        pState->selectedGroupUnitIndices.insert(chip.unitIdx);
                        pState->anchorGroupUnitIdx = chip.unitIdx;
                    }

                    if (chip.groupIdx >= 0 && chip.groupIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        const auto& grp = PoolManager::g_ReplacementGroupsCache[chip.groupIdx];
                        std::vector<ConsistReader::UnitInfo> clipUnits;
                        for (int uIdx : pState->selectedGroupUnitIndices)
                        {
                            if (uIdx >= 0 && uIdx < (int)grp.units.size())
                            {
                                const auto& pu = grp.units[uIdx];
                                ConsistReader::UnitInfo ui;
                                ui.uid = pu.szFileName;
                                ui.parentDir = pu.szFolder;
                                ui.isEngine = pu.isEngine;
                                ui.isFlipped = (pu.flipMode == PoolManager::UnitFlipMode::Flipped);
                                clipUnits.push_back(ui);
                            }
                        }
                        if (!clipUnits.empty()) SetAppClipboardUnits(clipUnits);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            if (wParam == 'A' || wParam == 'a')
            {
                if (pState->activeTab == 0)
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
                }
                else
                {
                    if (!PoolManager::g_ReplacementGroupsCache.empty())
                    {
                        int targetGrp = (pState->selectedGroupIdx >= 0 && pState->selectedGroupIdx < (int)PoolManager::g_ReplacementGroupsCache.size()) ? pState->selectedGroupIdx : 0;
                        pState->selectedGroupIdx = targetGrp;
                        pState->selectedGroupUnitIndices.clear();
                        for (size_t u = 0; u < PoolManager::g_ReplacementGroupsCache[targetGrp].units.size(); ++u)
                        {
                            pState->selectedGroupUnitIndices.insert((int)u);
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
                return 0;
            }
            else if (wParam == 'C' || wParam == 'c')
            {
                if (pState->activeTab == 0)
                {
                    if (pState->selectedPoolIdx >= 0 && !pState->selectedUnitIndices.empty())
                    {
                        std::vector<int> indices(pState->selectedUnitIndices.begin(), pState->selectedUnitIndices.end());
                        PoolManager::CopyMultipleUnitsToClipboard(pState->selectedPoolIdx, indices);
                    }
                }
                else
                {
                    if (pState->selectedGroupIdx >= 0 && !pState->selectedGroupUnitIndices.empty() && pState->selectedGroupIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
                    {
                        const auto& grp = PoolManager::g_ReplacementGroupsCache[pState->selectedGroupIdx];
                        std::vector<ConsistReader::UnitInfo> clipUnits;
                        for (int uIdx : pState->selectedGroupUnitIndices)
                        {
                            if (uIdx >= 0 && uIdx < (int)grp.units.size())
                            {
                                const auto& pu = grp.units[uIdx];
                                ConsistReader::UnitInfo ui;
                                ui.uid = pu.szFileName;
                                ui.parentDir = pu.szFolder;
                                ui.isEngine = pu.isEngine;
                                ui.isFlipped = (pu.flipMode == PoolManager::UnitFlipMode::Flipped);
                                clipUnits.push_back(ui);
                            }
                        }
                        if (!clipUnits.empty()) SetAppClipboardUnits(clipUnits);
                    }
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (wParam == 'V' || wParam == 'v')
            {
                const auto& clipUnits = GetAppClipboardUnits();
                if (!clipUnits.empty())
                {
                    if (pState->activeTab == 0)
                    {
                        int targetPool = (pState->selectedPoolIdx >= 0) ? pState->selectedPoolIdx : 0;
                        PoolManager::PasteUnitsToPool(targetPool, clipUnits);
                    }
                    else
                    {
                        int targetGrp = (pState->selectedGroupIdx >= 0) ? pState->selectedGroupIdx : 0;
                        PoolManager::PasteUnitsToReplacementGroup(targetGrp, clipUnits);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
        }
        else if (wParam == VK_DELETE || wParam == VK_BACK)
        {
            if (pState->activeTab == 0)
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
            else
            {
                if (pState->selectedGroupIdx >= 0 && !pState->selectedGroupUnitIndices.empty())
                {
                    std::vector<int> indices(pState->selectedGroupUnitIndices.begin(), pState->selectedGroupUnitIndices.end());
                    PoolManager::RemoveMultipleUnitsFromReplacementGroup(pState->selectedGroupIdx, indices);
                    pState->selectedGroupUnitIndices.clear();
                    pState->anchorGroupUnitIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        else if (wParam == 'F' || wParam == 'f')
        {
            if (pState->activeTab == 1 && pState->selectedGroupIdx >= 0 && !pState->selectedGroupUnitIndices.empty() && pState->selectedGroupIdx < (int)PoolManager::g_ReplacementGroupsCache.size())
            {
                auto& grp = PoolManager::g_ReplacementGroupsCache[pState->selectedGroupIdx];
                int firstIdx = *pState->selectedGroupUnitIndices.begin();
                PoolManager::UnitFlipMode curMode = (firstIdx >= 0 && firstIdx < (int)grp.units.size()) ? grp.units[firstIdx].flipMode : PoolManager::UnitFlipMode::Forward;
                PoolManager::UnitFlipMode nextMode = PoolManager::UnitFlipMode::Forward;
                switch (curMode)
                {
                case PoolManager::UnitFlipMode::Auto:
                case PoolManager::UnitFlipMode::Forward: nextMode = PoolManager::UnitFlipMode::Flipped; break;
                case PoolManager::UnitFlipMode::Flipped: nextMode = PoolManager::UnitFlipMode::Random;  break;
                case PoolManager::UnitFlipMode::Random:  nextMode = PoolManager::UnitFlipMode::Forward; break;
                }
                std::vector<int> indices(pState->selectedGroupUnitIndices.begin(), pState->selectedGroupUnitIndices.end());
                PoolManager::SetMultipleUnitsFlipModeInReplacementGroup(pState->selectedGroupIdx, indices, nextMode);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        else if (wParam == VK_ESCAPE)
        {
            if (!pState->selectedUnitIndices.empty() || !pState->selectedGroupUnitIndices.empty())
            {
                pState->selectedUnitIndices.clear();
                pState->selectedGroupUnitIndices.clear();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
    {
        RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
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

    case WM_NCDESTROY:
    {
        HWND hOwner = GetWindow(hWnd, GW_OWNER);
        if (hOwner && IsWindow(hOwner))
        {
            RestoreParentWindowFocus(hOwner);
        }
        break;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// -------------------------------------------------------------
// Public Exported Functions
// -------------------------------------------------------------

void ShowPoolManagerDialog(HWND hWndParent, int initialTab)
{
    if (g_hPoolManagerDlg && IsWindow(g_hPoolManagerDlg))
    {
        WizardDlgState* pState = (WizardDlgState*)GetWindowLongPtrW(g_hPoolManagerDlg, GWLP_USERDATA);
        if (pState)
        {
            pState->activeTab = initialTab;
            if (pState->hTitleBar && IsWindow(pState->hTitleBar))
            {
                CustomTitleBar_SetActiveTab(pState->hTitleBar, initialTab);
            }
            pState->m_vScroll.SetPos(0);
            pState->scrollY = 0;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        }
        ShowWindow(g_hPoolManagerDlg, SW_RESTORE);
        SetForegroundWindow(g_hPoolManagerDlg);
        BringWindowToTop(g_hPoolManagerDlg);
        return;
    }

    PoolManager::InitializePoolPresets();
    PoolManager::InitializeReplacementGroups();

    static bool s_WizardRegistered = false;
    const wchar_t* szClassName = L"BatchConsistGenerationWizardWindow";

    if (!s_WizardRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_DBLCLKS;
        wcex.lpfnWndProc = WizardDlgProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = NULL;
        wcex.lpszClassName = szClassName;
        RegisterClassExW(&wcex);
        s_WizardRegistered = true;
    }

    WizardDlgState* pState = new WizardDlgState();
    pState->hParent = hWndParent;
    pState->activeTab = initialTab;

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
        0, szClassName, L"Consist Pool Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    int toolbarY = 66;
    int toolbarH = 46;
    int contentY = toolbarY + toolbarH + 1;
    int footerH = 56;
    int contentH = h - footerH - contentY;
    RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

    if (!PtInRect(&rcContentClip, ptClient))
    {
        if (pState->dragOverPoolIdx != -1 || pState->dragOverGroupIdx != -1)
        {
            pState->dragOverPoolIdx = -1;
            pState->dragOverGroupIdx = -1;
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

    if (pState->activeTab == 0)
    {
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

        int cardY = contentY - pState->scrollY + 8;
        int cardMargin = 20;
        int cardW = w - (cardMargin * 2) - 14;

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
    }
    else
    {
        if (PoolManager::g_ReplacementGroupsCache.empty())
        {
            if (pState->dragOverGroupIdx != -1)
            {
                pState->dragOverGroupIdx = -1;
                InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
            }
            return false;
        }

        int cardY = contentY - pState->scrollY + 8;
        int cardMargin = 20;
        int cardW = w - (cardMargin * 2) - 14;

        int hitGroup = -1;
        std::wstring hitGroupName = L"";

        for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
        {
            const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
            int unitCount = (int)grp.units.size();
            int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
            int cardH = grp.isCollapsed ? 34 : (46 + unitsAreaH + 16);
            RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

            if (PtInRect(&rcCard, ptClient))
            {
                hitGroup = (int)g;
                hitGroupName = grp.name;
                break;
            }
            cardY += cardH + 14;
        }

        if (hitGroup != pState->dragOverGroupIdx)
        {
            pState->dragOverGroupIdx = hitGroup;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        }

        if (hitGroup != -1)
        {
            FluentDragGhost::Move(ptScreen, true, L"Add to " + hitGroupName);
            return true;
        }
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

    int toolbarY = 66;
    int toolbarH = 46;
    int contentY = toolbarY + toolbarH + 1;
    int footerH = 56;
    int contentH = h - footerH - contentY;
    RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

    if (!PtInRect(&rcContentClip, ptClient))
    {
        pState->dragOverPoolIdx = -1;
        pState->dragOverGroupIdx = -1;
        InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
        return false;
    }

    if (pState->activeTab == 0)
    {
        PoolManager::PoolPreset* pActivePreset = PoolManager::GetActivePreset();
        if (!pActivePreset || pActivePreset->pools.empty())
        {
            pState->dragOverPoolIdx = -1;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
            return false;
        }

        int cardY = contentY - pState->scrollY + 8;
        int cardMargin = 20;
        int cardW = w - (cardMargin * 2) - 14;

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
    }
    else
    {
        if (PoolManager::g_ReplacementGroupsCache.empty())
        {
            pState->dragOverGroupIdx = -1;
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
            return false;
        }

        int cardY = contentY - pState->scrollY + 8;
        int cardMargin = 20;
        int cardW = w - (cardMargin * 2) - 14;

        int hitGroup = -1;
        for (size_t g = 0; g < PoolManager::g_ReplacementGroupsCache.size(); ++g)
        {
            const auto& grp = PoolManager::g_ReplacementGroupsCache[g];
            int unitCount = (int)grp.units.size();
            int unitsAreaH = (unitCount == 0) ? 42 : (28 + ((unitCount + 1) / 2) * 28 + 10);
            int cardH = grp.isCollapsed ? 34 : (46 + unitsAreaH + 16);
            RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

            if (PtInRect(&rcCard, ptClient))
            {
                hitGroup = (int)g;
                break;
            }
            cardY += cardH + 14;
        }

        pState->dragOverGroupIdx = -1;
        if (hitGroup >= 0 && hitGroup < (int)PoolManager::g_ReplacementGroupsCache.size())
        {
            PoolManager::PasteUnitsToReplacementGroup(hitGroup, units);
            InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
            return true;
        }
    }

    InvalidateRect(g_hPoolManagerDlg, NULL, FALSE);
    return false;
}
