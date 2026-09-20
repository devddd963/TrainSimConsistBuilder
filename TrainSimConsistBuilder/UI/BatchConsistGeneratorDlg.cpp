#include "BatchConsistGeneratorDlg.h"
#include "PoolManagerDlg.h"
#include "ModernContextMenu.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "../SRC/BatchConsistGenerator.h"
#include "../SRC/PoolManager.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include "../SRC/AssetsParser.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

extern HFONT GetAdaptiveSystemFont();
extern HWND g_hAssetList;
extern std::vector<size_t> g_FilteredStockIndices;
extern std::vector<StockItem> g_StockCache;
extern CRITICAL_SECTION g_StockCacheCS;

static HWND g_hBatchGeneratorDlg = NULL;

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

    if (hasIcon && hasText)
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
        COLORREF bgCol = RGB(32, 32, 32);
        COLORREF txtCol = RGB(245, 245, 245);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, txtCol);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(32, 32, 32));
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
        COLORREF bgCol = RGB(24, 24, 24);
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
        COLORREF editBoxBg = RGB(32, 32, 32);
        COLORREF editBoxBorder = RGB(70, 70, 70);

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
        COLORREF winBorderCol = RGB(60, 60, 60);
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
        if (pState->hFontTitle)  DeleteObject(pState->hFontTitle);
        if (pState->hFontPrompt) DeleteObject(pState->hFontPrompt);
        if (pState->hFontMain)   DeleteObject(pState->hFontMain);
        if (pState->hFontBold)   DeleteObject(pState->hFontBold);
        if (pState->hFontIcon)   DeleteObject(pState->hFontIcon);
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
        BTN_ADD_POOL,
        BTN_POOL_RENAME,
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
        BTN_POOL_PASTE_CLIPBOARD,
        BTN_POOL_CLEAR_UNITS,
        BTN_UNIT_REMOVE,
        BTN_UNIT_FLIP_TOGGLE,
        BTN_CLOSE,
        BTN_GENERATE,
        COMBO_PRESET_CLICK
    };

    Type type = NONE;
    RECT rc = { 0 };
    int poolIdx = -1;
    int unitIdx = -1;
};


struct ManualConsistItem
{
    std::wstring name;
    int targetUnits = 0; // 0 = Dynamic Pool-Sum, >0 = Fixed exact count
};

struct ExportDialogState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    RECT rcTopCloseBtn = { 0 };
    bool isTopCloseHover = false;
    HWND hEditBaseName = NULL;
    PoolManager::PoolPreset preset;
    std::wstring targetFolder;

    // Mode: 0 = Auto-Numbered, 1 = Manual List
    int namingMode = 0;

    // Auto-Numbered Mode Settings
    std::wstring baseName;
    int count = 5;
    int styleIndex = 0;     // 0=Numeric, 1=Zero-Padded, 2=Alpha, 3=Roman, 4=Timestamp
    int separatorIndex = 0; // 0="_", 1="-", 2=" ", 3=""

    // Total Units Sizing Mode for Auto:
    // 0 = Dynamic Pool-Sum, 1 = Fixed Total Units
    int autoSizingMode = 0;
    int autoFixedUnits = 6;
    std::vector<int> autoConsistUnits; // Per-consist unit overrides (size = count)

    // Manual Mode Settings
    std::vector<ManualConsistItem> manualItems;

    // Options
    bool overwriteExisting = false;

    // Fonts
    HFONT hFontTitle = NULL;
    HFONT hFontSubtitle = NULL;
    HFONT hFontSection = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontMono = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontIcon = NULL;

    // Scroll
    int scrollY = 0;
    int maxScrollY = 0;

    // Hit Testing Targets
    enum ClickTarget {
        TARGET_NONE,
        TARGET_CLOSE,
        TARGET_PRESET_DROPDOWN,
        TARGET_MANAGE_POOLS_BTN,
        TARGET_TAB_AUTO,
        TARGET_TAB_MANUAL,
        TARGET_COUNT_MINUS,
        TARGET_COUNT_PLUS,
        TARGET_COUNT_BOX,
        TARGET_STYLE_0,
        TARGET_STYLE_1,
        TARGET_STYLE_2,
        TARGET_STYLE_3,
        TARGET_STYLE_4,
        TARGET_SEP_0,
        TARGET_SEP_1,
        TARGET_SEP_2,
        TARGET_SEP_3,
        TARGET_SIZING_DYNAMIC,
        TARGET_SIZING_FIXED,
        TARGET_SIZING_MINUS,
        TARGET_SIZING_PLUS,
        TARGET_SIZING_BOX,
        TARGET_PREVIEW_UNIT_BADGE, // itemIdx = consist index 0..count-1
        TARGET_ADD_MANUAL,
        TARGET_SET_ALL_MANUAL_SIZING,
        TARGET_DELETE_MANUAL, // index in itemIdx
        TARGET_EDIT_MANUAL,   // index in itemIdx
        TARGET_TOGGLE_MANUAL_SIZING, // index in itemIdx
        TARGET_OVERWRITE_CHK,
        TARGET_CANCEL_BTN,
        TARGET_GENERATE_BTN
    };

    struct HitItem {
        ClickTarget target;
        RECT rc;
        int itemIdx = -1;
    };
    std::vector<HitItem> hitItems;

    ClickTarget hoveredTarget = TARGET_NONE;
    int hoveredIdx = -1;

    bool isConfirmed = false;
    std::vector<std::wstring> createdFiles;

    void SyncAutoUnits()
    {
        int defaultVal = (autoSizingMode == 1) ? autoFixedUnits : 0;
        if ((int)autoConsistUnits.size() < count)
        {
            autoConsistUnits.resize(count, defaultVal);
        }
        else if ((int)autoConsistUnits.size() > count)
        {
            autoConsistUnits.resize(count);
        }
    }
};

static LRESULT CALLBACK ExportDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ExportDialogState* pState = (ExportDialogState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCACTIVATE:
        return TRUE;

    case WM_ERASEBKGND:
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

        // 1. Border resize handling (8px border)
        int b = 8;
        if (pt.y < b && pt.x < b) return HTTOPLEFT;
        if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
        if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
        if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
        if (pt.y < b) return HTTOP;
        if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
        if (pt.x < b) return HTLEFT;
        if (pt.x >= rcClient.right - b) return HTRIGHT;

        // 2. Interactive controls
        for (const auto& hit : pState->hitItems)
        {
            if (PtInRect(&hit.rc, pt)) return HTCLIENT;
        }

        // 3. Header area is draggable (except top right close area)
        if (pt.y <= 42 && pt.x < rcClient.right - 44)
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
    {
        if (pState)
        {
            pState->hoveredTarget = ExportDialogState::TARGET_NONE;
            pState->hoveredIdx = -1;
            pState->isTopCloseHover = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (ExportDialogState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_CREATE:
    {
        pState->hFontTitle    = CreateCustomFont(13, FW_BOLD, L"Segoe UI");
        pState->hFontSubtitle = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontSection  = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontMain     = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontBold     = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontMono     = CreateCustomFont(10, FW_NORMAL, L"Consolas");
        pState->hFontSmall    = CreateCustomFont(8, FW_NORMAL, L"Segoe UI");
        pState->hFontIcon     = CreateCustomFont(10, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateCustomFont(10, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->hEditBaseName = CreateWindowExW(
            0, L"EDIT", pState->baseName.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            34, 194, 380, 20, hWnd, (HMENU)201, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEditBaseName, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEditBaseName, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));

        pState->SyncAutoUnits();
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
        if (wParam == VK_ESCAPE)
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

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        COLORREF bgCol = RGB(30, 30, 32);
        COLORREF txtCol = RGB(245, 245, 245);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, txtCol);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(30, 30, 32));
        return (LRESULT)hbrEditDark;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 201 && HIWORD(wParam) == EN_CHANGE)
        {
            if (pState && pState->hEditBaseName)
            {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(pState->hEditBaseName, buf, 256);
                pState->baseName = buf;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        pState->scrollY -= (delta / WHEEL_DELTA) * 36;
        if (pState->scrollY < 0) pState->scrollY = 0;
        if (pState->scrollY > pState->maxScrollY) pState->scrollY = pState->maxScrollY;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        bool hTopClose = PtInRect(&pState->rcTopCloseBtn, pt) != FALSE;
        if (hTopClose != pState->isTopCloseHover)
        {
            pState->isTopCloseHover = hTopClose;
            InvalidateRect(hWnd, &pState->rcTopCloseBtn, FALSE);
        }

        ExportDialogState::ClickTarget newHover = ExportDialogState::TARGET_NONE;
        int newHoverIdx = -1;

        for (const auto& item : pState->hitItems)
        {
            if (PtInRect(&item.rc, pt))
            {
                newHover = item.target;
                newHoverIdx = item.itemIdx;
                break;
            }
        }

        if (newHover != pState->hoveredTarget || newHoverIdx != pState->hoveredIdx)
        {
            pState->hoveredTarget = newHover;
            pState->hoveredIdx = newHoverIdx;
            InvalidateRect(hWnd, NULL, FALSE);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (!pState) break;
        if (pState->isTopCloseHover)
        {
            pState->isTopCloseHover = false;
            InvalidateRect(hWnd, &pState->rcTopCloseBtn, FALSE);
        }
        if (pState->hoveredTarget != ExportDialogState::TARGET_NONE)
        {
            pState->hoveredTarget = ExportDialogState::TARGET_NONE;
            pState->hoveredIdx = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

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

        for (const auto& item : pState->hitItems)
        {
            if (PtInRect(&item.rc, pt))
            {
                switch (item.target)
                {
                case ExportDialogState::TARGET_CLOSE:
                case ExportDialogState::TARGET_CANCEL_BTN:
                    if (pState && pState->hParent && IsWindow(pState->hParent))
                    {
                        SetForegroundWindow(pState->hParent);
                        SetActiveWindow(pState->hParent);
                    }
                    DestroyWindow(hWnd);
                    return 0;

                case ExportDialogState::TARGET_MANAGE_POOLS_BTN:
                    ShowPoolManagerDialog(pState->hParent);
                    return 0;

                case ExportDialogState::TARGET_PRESET_DROPDOWN:
                {
                    if (PoolManager::g_PoolPresetsCache.empty()) return 0;

                    std::vector<ContextMenuItem> items;
                    for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
                    {
                        const auto& p = PoolManager::g_PoolPresetsCache[i];
                        int totalU = 0;
                        for (const auto& pl : p.pools) totalU += (int)pl.units.size();

                        std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
                        std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
                        bool isCurrent = (p.presetName == pState->preset.presetName);
                        items.push_back(ContextMenuItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, true));
                    }

                    RECT rcScreen = item.rc;
                    MapWindowPoints(hWnd, NULL, (LPPOINT)&rcScreen, 2);
                    int pickerW = rcScreen.right - rcScreen.left;

                    int chosen = ModernContextMenu::Show(hWnd, rcScreen.left, rcScreen.bottom + 2, items, TRUE, pickerW);
                    if (chosen > 0)
                    {
                        int selIdx = chosen - 1;
                        PoolManager::g_ActivePresetIndex = selIdx;
                        pState->preset = PoolManager::g_PoolPresetsCache[selIdx];

                        pState->baseName = pState->preset.presetName;
                        for (auto& ch : pState->baseName)
                        {
                            if (ch == L' ' || ch == L'-') ch = L'_';
                        }
                        if (pState->baseName.empty()) pState->baseName = L"Consist";

                        int sumMax = 0;
                        for (const auto& pl : pState->preset.pools)
                        {
                            sumMax += pl.maxCount;
                        }
                        pState->autoFixedUnits = (sumMax > 0) ? sumMax : 6;
                        pState->SyncAutoUnits();

                        if (pState->hEditBaseName && IsWindow(pState->hEditBaseName))
                        {
                            SetWindowTextW(pState->hEditBaseName, pState->baseName.c_str());
                        }

                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }


                case ExportDialogState::TARGET_TAB_AUTO:
                    pState->namingMode = 0;
                    ShowWindow(pState->hEditBaseName, SW_SHOW);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_TAB_MANUAL:
                    pState->namingMode = 1;
                    ShowWindow(pState->hEditBaseName, SW_HIDE);
                    if (pState->manualItems.empty())
                    {
                        pState->SyncAutoUnits();
                        for (int i = 1; i <= pState->count; ++i)
                        {
                            std::wstring sep = (pState->separatorIndex == 0) ? L"_" : ((pState->separatorIndex == 1) ? L"-" : ((pState->separatorIndex == 2) ? L" " : L""));
                            ManualConsistItem mi;
                            mi.name = BatchConsistGenerator::FormatSerializedName(pState->baseName, i, pState->count, pState->styleIndex, sep);
                            mi.targetUnits = (i - 1 < (int)pState->autoConsistUnits.size()) ? pState->autoConsistUnits[i - 1] : ((pState->autoSizingMode == 1) ? pState->autoFixedUnits : 0);
                            pState->manualItems.push_back(mi);
                        }
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_COUNT_MINUS:
                    if (pState->count > 1)
                    {
                        pState->count--;
                        pState->SyncAutoUnits();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;

                case ExportDialogState::TARGET_COUNT_PLUS:
                    if (pState->count < 100)
                    {
                        pState->count++;
                        pState->SyncAutoUnits();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;

                case ExportDialogState::TARGET_COUNT_BOX:
                {
                    std::wstring valStr = std::to_wstring(pState->count);
                    if (ShowModernInputPrompt(hWnd, L"Consist Generation Count", L"Enter number of consists to generate (1 - 100):", valStr, valStr))
                    {
                        try {
                            int v = std::stoi(valStr);
                            if (v >= 1 && v <= 100)
                            {
                                pState->count = v;
                                pState->SyncAutoUnits();
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        } catch (...) {}
                    }
                    return 0;
                }

                case ExportDialogState::TARGET_STYLE_0:
                    pState->styleIndex = 0;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_STYLE_1:
                    pState->styleIndex = 1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_STYLE_2:
                    pState->styleIndex = 2;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_STYLE_3:
                    pState->styleIndex = 3;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_STYLE_4:
                    pState->styleIndex = 4;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_SEP_0:
                    pState->separatorIndex = 0;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_SEP_1:
                    pState->separatorIndex = 1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_SEP_2:
                    pState->separatorIndex = 2;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                case ExportDialogState::TARGET_SEP_3:
                    pState->separatorIndex = 3;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_SIZING_DYNAMIC:
                    pState->autoSizingMode = 0;
                    pState->SyncAutoUnits();
                    for (auto& u : pState->autoConsistUnits) u = 0;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_SIZING_FIXED:
                    pState->autoSizingMode = 1;
                    pState->SyncAutoUnits();
                    for (auto& u : pState->autoConsistUnits) u = pState->autoFixedUnits;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_SIZING_MINUS:
                    if (pState->autoFixedUnits > 1)
                    {
                        pState->autoFixedUnits--;
                        pState->autoSizingMode = 1;
                        pState->SyncAutoUnits();
                        for (auto& u : pState->autoConsistUnits) u = pState->autoFixedUnits;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;

                case ExportDialogState::TARGET_SIZING_PLUS:
                    if (pState->autoFixedUnits < 150)
                    {
                        pState->autoFixedUnits++;
                        pState->autoSizingMode = 1;
                        pState->SyncAutoUnits();
                        for (auto& u : pState->autoConsistUnits) u = pState->autoFixedUnits;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;

                case ExportDialogState::TARGET_SIZING_BOX:
                {
                    std::wstring valStr = std::to_wstring(pState->autoFixedUnits);
                    if (ShowModernInputPrompt(hWnd, L"Fixed Total Units", L"Enter exact total units per consist (1 - 150):", valStr, valStr))
                    {
                        try {
                            int v = std::stoi(valStr);
                            if (v >= 1 && v <= 150)
                            {
                                pState->autoFixedUnits = v;
                                pState->autoSizingMode = 1;
                                pState->SyncAutoUnits();
                                for (auto& u : pState->autoConsistUnits) u = pState->autoFixedUnits;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        } catch (...) {}
                    }
                    return 0;
                }

                case ExportDialogState::TARGET_PREVIEW_UNIT_BADGE:
                    if (item.itemIdx >= 0 && item.itemIdx < (int)pState->autoConsistUnits.size())
                    {
                        int curUnits = pState->autoConsistUnits[item.itemIdx];
                        std::wstring promptVal = std::to_wstring(curUnits);
                        std::wstring title = L"Customize Consist #" + std::to_wstring(item.itemIdx + 1) + L" Sizing";
                        std::wstring promptText = L"Enter total units for Consist #" + std::to_wstring(item.itemIdx + 1) + L" (0 for Dynamic Pool-Sum, or 1 - 150 for custom units):";

                        if (ShowModernInputPrompt(hWnd, title.c_str(), promptText.c_str(), promptVal, promptVal))
                        {
                            try {
                                int v = std::stoi(promptVal);
                                if (v >= 0 && v <= 150)
                                {
                                    pState->autoConsistUnits[item.itemIdx] = v;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            } catch (...) {}
                        }
                    }
                    return 0;

                case ExportDialogState::TARGET_OVERWRITE_CHK:
                    pState->overwriteExisting = !pState->overwriteExisting;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;

                case ExportDialogState::TARGET_ADD_MANUAL:
                {
                    ManualConsistItem mi;
                    mi.name = pState->baseName + L"_" + std::to_wstring(pState->manualItems.size() + 1);
                    mi.targetUnits = (pState->autoSizingMode == 1) ? pState->autoFixedUnits : 0;
                    pState->manualItems.push_back(mi);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                case ExportDialogState::TARGET_SET_ALL_MANUAL_SIZING:
                {
                    std::wstring valStr = L"0";
                    if (ShowModernInputPrompt(hWnd, L"Batch Set Total Units", L"Enter total units for ALL consists (0 for Dynamic Pool-Sum, or 1-150 for fixed count):", valStr, valStr))
                    {
                        try {
                            int v = std::stoi(valStr);
                            if (v >= 0 && v <= 150)
                            {
                                for (auto& mItem : pState->manualItems)
                                {
                                    mItem.targetUnits = v;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        } catch (...) {}
                    }
                    return 0;
                }

                case ExportDialogState::TARGET_DELETE_MANUAL:
                    if (item.itemIdx >= 0 && item.itemIdx < (int)pState->manualItems.size())
                    {
                        pState->manualItems.erase(pState->manualItems.begin() + item.itemIdx);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;

                case ExportDialogState::TARGET_EDIT_MANUAL:
                    if (item.itemIdx >= 0 && item.itemIdx < (int)pState->manualItems.size())
                    {
                        std::wstring curName = pState->manualItems[item.itemIdx].name;
                        if (ShowModernInputPrompt(hWnd, L"Edit Consist Filename", L"Enter .con filename without extension:", curName, curName))
                        {
                            if (!curName.empty())
                            {
                                pState->manualItems[item.itemIdx].name = curName;
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                    return 0;

                case ExportDialogState::TARGET_TOGGLE_MANUAL_SIZING:
                    if (item.itemIdx >= 0 && item.itemIdx < (int)pState->manualItems.size())
                    {
                        auto& mItem = pState->manualItems[item.itemIdx];
                        std::wstring curVal = std::to_wstring(mItem.targetUnits);
                        std::wstring title = L"Customize Consist #" + std::to_wstring(item.itemIdx + 1) + L" Sizing";
                        std::wstring promptText = L"Enter target total units (0 for Dynamic Pool-Sum, or 1-150 for custom units):";
                        if (ShowModernInputPrompt(hWnd, title.c_str(), promptText.c_str(), curVal, curVal))
                        {
                            try {
                                int v = std::stoi(curVal);
                                if (v >= 0 && v <= 150)
                                {
                                    mItem.targetUnits = v;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            } catch (...) {}
                        }
                    }
                    return 0;

                case ExportDialogState::TARGET_GENERATE_BTN:
                {
                    // Sync edit text first
                    if (pState->hEditBaseName)
                    {
                        wchar_t buf[256] = { 0 };
                        GetWindowTextW(pState->hEditBaseName, buf, 256);
                        pState->baseName = buf;
                    }

                    if (pState->targetFolder.empty())
                    {
                        ShowModernMessageBox(hWnd, L"Train Simulator consists folder not found.\r\nPlease open a valid Train Simulator directory first.", L"Export Error", MB_OK | MB_ICONERROR);
                        return 0;
                    }

                    std::vector<BatchConsistGenerator::GeneratedConsistSpec> specs;
                    if (pState->namingMode == 0) // Auto
                    {
                        if (pState->baseName.empty())
                        {
                            ShowModernMessageBox(hWnd, L"Please enter a valid consist base filename.", L"Validation", MB_OK | MB_ICONWARNING);
                            return 0;
                        }

                        pState->SyncAutoUnits();
                        std::wstring sep = (pState->separatorIndex == 0) ? L"_" : ((pState->separatorIndex == 1) ? L"-" : ((pState->separatorIndex == 2) ? L" " : L""));
                        for (int i = 1; i <= pState->count; ++i)
                        {
                            std::wstring fn = BatchConsistGenerator::FormatSerializedName(pState->baseName, i, pState->count, pState->styleIndex, sep);
                            BatchConsistGenerator::GeneratedConsistSpec sp;
                            sp.fileName = fn;
                            sp.displayName = fn;
                            sp.targetTotalUnits = (i - 1 < (int)pState->autoConsistUnits.size()) ? pState->autoConsistUnits[i - 1] : ((pState->autoSizingMode == 1) ? pState->autoFixedUnits : 0);
                            specs.push_back(sp);
                        }
                    }
                    else // Manual
                    {
                        if (pState->manualItems.empty())
                        {
                            ShowModernMessageBox(hWnd, L"Please add at least one consist filename in the list.", L"Validation", MB_OK | MB_ICONWARNING);
                            return 0;
                        }

                        for (const auto& mi : pState->manualItems)
                        {
                            if (mi.name.empty()) continue;
                            BatchConsistGenerator::GeneratedConsistSpec sp;
                            sp.fileName = mi.name;
                            sp.displayName = mi.name;
                            sp.targetTotalUnits = mi.targetUnits;
                            specs.push_back(sp);
                        }
                    }

                    if (specs.empty())
                    {
                        ShowModernMessageBox(hWnd, L"No consist filenames defined to generate.", L"Validation", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    std::vector<std::wstring> createdFiles;
                    std::wstring outErr;
                    bool ok = BatchConsistGenerator::BatchGenerateConsists(
                        pState->preset, specs, pState->targetFolder, pState->overwriteExisting, createdFiles, outErr
                    );

                    if (ok)
                    {
                        pState->isConfirmed = true;
                        pState->createdFiles = createdFiles;

                        // Trigger rescan of consists in main app
                        TriggerAppConsistsRescan();

                        std::wstring successMsg = L"Successfully generated " + std::to_wstring(createdFiles.size()) + L" consist files in:\r\n" + pState->targetFolder;
                        ShowModernMessageBox(hWnd, successMsg.c_str(), L"Batch Generation Complete", MB_OK | MB_ICONINFORMATION);

                        if (pState && pState->hParent && IsWindow(pState->hParent))
                        {
                            SetForegroundWindow(pState->hParent);
                            SetActiveWindow(pState->hParent);
                        }
                        DestroyWindow(hWnd);
                        return 0;
                    }
                    else
                    {
                        ShowModernMessageBox(hWnd, outErr.c_str(), L"Generation Failed", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                }

                default:
                    break;
                }
            }
        }
        return 0;
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

        pState->hitItems.clear();
        pState->SyncAutoUnits();

        // 1. Background Fill
        COLORREF bgCol = RGB(22, 22, 25);
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(memDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        COLORREF titleBgCol = RGB(32, 32, 34);
        COLORREF borderCol = RGB(55, 55, 62);
        COLORREF textPrimary = RGB(255, 255, 255);
        COLORREF textSecondary = RGB(160, 160, 168);
        COLORREF accentCol = RGB(0, 120, 215);

        // 2. Title Bar (Height = 42px)
        RECT rcTitleBar = { 0, 0, w, 42 };
        HBRUSH hbrTitle = CreateSolidBrush(titleBgCol);
        FillRect(memDC, &rcTitleBar, hbrTitle);
        DeleteObject(hbrTitle);

        SetBkMode(memDC, TRANSPARENT);

        if (pState->hFontIcon)
        {
            SelectObject(memDC, pState->hFontIcon);
            SetTextColor(memDC, accentCol);
            RECT rcIcon = { 16, 0, 42, 42 };
            DrawTextW(memDC, L"\xE768", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (pState->hFontTitle)
        {
            SelectObject(memDC, pState->hFontTitle);
            SetTextColor(memDC, textPrimary);
            RECT rcTitle = { 46, 0, w - 50, 42 };
            DrawTextW(memDC, L"Batch Consist Export & Generation", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Top-Right Close Button [✕]
        pState->rcTopCloseBtn = { w - 44, 0, w, 42 };
        if (pState->isTopCloseHover)
        {
            HBRUSH hbrClose = CreateSolidBrush(RGB(232, 17, 35));
            FillRect(memDC, &pState->rcTopCloseBtn, hbrClose);
            DeleteObject(hbrClose);
            SetTextColor(memDC, RGB(255, 255, 255));
        }
        else
        {
            SetTextColor(memDC, textSecondary);
        }
        SelectObject(memDC, pState->hFontIcon ? pState->hFontIcon : pState->hFontMain);
        DrawTextW(memDC, L"\xE711", -1, &pState->rcTopCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 1st Divider Line (Below Title Bar at Y = 42)
        HPEN hPenLine = CreatePen(PS_SOLID, 1, borderCol);
        HPEN holdPen = (HPEN)SelectObject(memDC, hPenLine);
        MoveToEx(memDC, 0, 42, NULL);
        LineTo(memDC, w, 42);

        // 3. Sub-Header Toolbar (Y = 43 to 89, Height = 46px)
        int toolbarY = 43;
        int toolbarH = 46;
        RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
        COLORREF tbBg = RGB(28, 28, 32);
        HBRUSH hbrTb = CreateSolidBrush(tbBg);
        FillRect(memDC, &rcToolbar, hbrTb);
        DeleteObject(hbrTb);

        // 2nd Divider Line (Below Preset Toolbar at Y = toolbarY + toolbarH = 89)
        MoveToEx(memDC, 0, toolbarY + toolbarH, NULL);
        LineTo(memDC, w, toolbarY + toolbarH);

        auto DrawTabPill = [&](const RECT& rc, const wchar_t* text, bool isSelected, bool isHovered, ExportDialogState::ClickTarget target)
        {
            COLORREF tBg = isSelected ? RGB(0, 120, 215)
                                      : (isHovered ? RGB(40, 40, 45)
                                                   : RGB(32, 32, 36));
            COLORREF tBorder = isSelected ? RGB(0, 140, 240) : RGB(50, 50, 58);
            COLORREF tTxt = isSelected ? RGB(255, 255, 255) : RGB(220, 220, 225);

            HBRUSH hbr = CreateSolidBrush(tBg);
            HPEN hp = CreatePen(PS_SOLID, 1, tBorder);
            SelectObject(memDC, hbr);
            SelectObject(memDC, hp);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            DeleteObject(hbr);
            DeleteObject(hp);

            SetTextColor(memDC, tTxt);
            SelectObject(memDC, isSelected ? pState->hFontBold : pState->hFontMain);
            RECT rcT = rc;
            DrawTextW(memDC, text, -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            pState->hitItems.push_back({ target, rc });
        };

        // Left of Toolbar: Mode Tabs (Height 32px)
        int tabBtnH = 32;
        int tabBtnY = toolbarY + (toolbarH - tabBtnH) / 2;
        RECT rcTabAuto = { 20, tabBtnY, 20 + 220, tabBtnY + tabBtnH };
        RECT rcTabManual = { rcTabAuto.right + 8, tabBtnY, rcTabAuto.right + 8 + 200, tabBtnY + tabBtnH };
        DrawTabPill(rcTabAuto, L"\U0001F522 Auto-Numbered Sequence", pState->namingMode == 0, pState->hoveredTarget == ExportDialogState::TARGET_TAB_AUTO, ExportDialogState::TARGET_TAB_AUTO);
        DrawTabPill(rcTabManual, L"\U0001F4DD Manual Consist Names", pState->namingMode == 1, pState->hoveredTarget == ExportDialogState::TARGET_TAB_MANUAL, ExportDialogState::TARGET_TAB_MANUAL);

        // Right of Toolbar: Preset Selector & Manage Pools Button
        int managePoolsW = 130;
        int tbBtnH = 32;
        int tbBtnY = toolbarY + (toolbarH - tbBtnH) / 2;
        RECT rcManagePoolsBtn = { w - 20 - managePoolsW, tbBtnY, w - 20, tbBtnY + tbBtnH };
        int presetPickerW = 250;
        RECT rcPresetPicker = { rcManagePoolsBtn.left - 10 - presetPickerW, tbBtnY, rcManagePoolsBtn.left - 10, tbBtnY + tbBtnH };

        bool isHoverPreset = (pState->hoveredTarget == ExportDialogState::TARGET_PRESET_DROPDOWN);
        bool isHoverManage = (pState->hoveredTarget == ExportDialogState::TARGET_MANAGE_POOLS_BTN);

        // Draw Preset Selector Dropdown Box
        COLORREF comboBg = isHoverPreset ? RGB(45, 45, 50) : RGB(34, 34, 38);
        COLORREF comboBorder = isHoverPreset ? RGB(90, 90, 100) : RGB(60, 60, 68);
        HBRUSH hbrCombo = CreateSolidBrush(comboBg);
        HPEN hPenCombo = CreatePen(PS_SOLID, 1, comboBorder);
        SelectObject(memDC, hbrCombo);
        SelectObject(memDC, hPenCombo);
        RoundRect(memDC, rcPresetPicker.left, rcPresetPicker.top, rcPresetPicker.right, rcPresetPicker.bottom, 6, 6);
        DeleteObject(hbrCombo);
        DeleteObject(hPenCombo);

        RECT rcComboText = { rcPresetPicker.left + 10, rcPresetPicker.top, rcPresetPicker.right - 26, rcPresetPicker.bottom };
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, textPrimary);
        std::wstring presetLabel = L"Preset: " + pState->preset.presetName;
        DrawTextW(memDC, presetLabel.c_str(), -1, &rcComboText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rcChevron = { rcPresetPicker.right - 24, rcPresetPicker.top, rcPresetPicker.right - 6, rcPresetPicker.bottom };
        int chevCX = (rcChevron.left + rcChevron.right) / 2;
        int chevCY = (rcChevron.top + rcChevron.bottom) / 2;
        COLORREF arrCol = isHoverPreset ? RGB(255, 255, 255) : textSecondary;
        HBRUSH hBrChev = CreateSolidBrush(arrCol);
        HPEN hPenChev = CreatePen(PS_SOLID, 1, arrCol);
        HBRUSH hOldBrC = (HBRUSH)SelectObject(memDC, hBrChev);
        HPEN hOldPenC = (HPEN)SelectObject(memDC, hPenChev);
        POINT ptsChev[3] = { { chevCX - 4, chevCY - 2 }, { chevCX + 4, chevCY - 2 }, { chevCX, chevCY + 3 } };
        Polygon(memDC, ptsChev, 3);
        SelectObject(memDC, hOldBrC);
        SelectObject(memDC, hOldPenC);
        DeleteObject(hBrChev);
        DeleteObject(hPenChev);

        pState->hitItems.push_back({ ExportDialogState::TARGET_PRESET_DROPDOWN, rcPresetPicker, -1 });

        // Draw Manage Pools Button
        DrawModernButton(memDC, rcManagePoolsBtn, L"Manage Pools", isHoverManage, false, false, pState->hFontMain, pState->hFontIcon, L"\xE713");
        pState->hitItems.push_back({ ExportDialogState::TARGET_MANAGE_POOLS_BTN, rcManagePoolsBtn, -1 });

        // 4. Target Folder Banner Card (Y = 97..129, Height = 32px)
        RECT rcFolderCard = { 20, 97, w - 20, 129 };
        COLORREF cardBg = RGB(30, 30, 34);
        COLORREF cardBorder = RGB(48, 48, 54);
        HBRUSH hbrCard = CreateSolidBrush(cardBg);
        HPEN hPenCard = CreatePen(PS_SOLID, 1, cardBorder);
        SelectObject(memDC, hbrCard);
        SelectObject(memDC, hPenCard);
        RoundRect(memDC, rcFolderCard.left, rcFolderCard.top, rcFolderCard.right, rcFolderCard.bottom, 6, 6);
        DeleteObject(hbrCard);
        DeleteObject(hPenCard);

        RECT rcFolderText = { rcFolderCard.left + 12, rcFolderCard.top + 2, rcFolderCard.right - 12, rcFolderCard.bottom - 2 };
        SelectObject(memDC, pState->hFontMain);
        if (!pState->targetFolder.empty())
        {
            SetTextColor(memDC, textSecondary);
            std::wstring destPrefix = L"Destination: ";
            RECT rcPrefix = rcFolderText;
            DrawTextW(memDC, destPrefix.c_str(), -1, &rcPrefix, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_CALCRECT);
            DrawTextW(memDC, destPrefix.c_str(), -1, &rcFolderText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT rcPath = rcFolderText;
            rcPath.left = rcFolderText.left + (rcPrefix.right - rcPrefix.left);
            SetTextColor(memDC, RGB(80, 200, 255));
            DrawTextW(memDC, pState->targetFolder.c_str(), -1, &rcPath, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
        }
        else
        {
            SetTextColor(memDC, RGB(255, 170, 40));
            DrawTextW(memDC, L"\u26A0 Train Simulator directory not set. Consists cannot be saved.", -1, &rcFolderText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // Calculate pool capacity range
        int minTotal = 0, maxTotal = 0;
        for (const auto& pl : pState->preset.pools)
        {
            minTotal += (std::max)(0, pl.minCount);
            maxTotal += (std::max)((std::max)(0, pl.minCount), pl.maxCount);
        }

        // 5. Mode Content
        if (pState->namingMode == 0) // Auto-Numbered Mode
        {
            int colSplitX = 20 + (w - 40 - 12) / 2;

            // Ensure edit control is strictly positioned inside rcEditFrame
            if (pState->hEditBaseName && IsWindow(pState->hEditBaseName))
            {
                SetWindowPos(pState->hEditBaseName, NULL, 28, 163, (colSplitX - 10) - 36, 20, SWP_NOZORDER | SWP_NOACTIVATE);
            }

            // Row A: Base Name & Count
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblBase = { 20, 137, colSplitX - 10, 155 };
            DrawTextW(memDC, L"Consist Base Filename:", -1, &rcLblBase, DT_LEFT | DT_SINGLELINE);

            // Edit frame for base name
            RECT rcEditFrame = { 20, 157, colSplitX - 10, 189 };
            HBRUSH hbrEditF = CreateSolidBrush(RGB(30, 30, 32));
            HPEN hPenEditF = CreatePen(PS_SOLID, 1, RGB(65, 65, 75));
            SelectObject(memDC, hbrEditF);
            SelectObject(memDC, hPenEditF);
            RoundRect(memDC, rcEditFrame.left, rcEditFrame.top, rcEditFrame.right, rcEditFrame.bottom, 6, 6);
            DeleteObject(hbrEditF);
            DeleteObject(hPenEditF);

            RECT rcLblCount = { colSplitX + 10, 137, w - 20, 155 };
            DrawTextW(memDC, L"Number of Consists to Generate:", -1, &rcLblCount, DT_LEFT | DT_SINGLELINE);

            // Consist Count Stepper
            int countRightEdge = w - 20;
            RECT rcMinus = { colSplitX + 10, 157, colSplitX + 10 + 44, 189 };
            RECT rcPlus = { countRightEdge - 44, 157, countRightEdge, 189 };
            RECT rcNumBox = { rcMinus.right + 8, 157, rcPlus.left - 8, 189 };

            DrawModernButton(memDC, rcMinus, L"-", pState->hoveredTarget == ExportDialogState::TARGET_COUNT_MINUS, false, false, pState->hFontMain);
            pState->hitItems.push_back({ ExportDialogState::TARGET_COUNT_MINUS, rcMinus });

            // Count display box
            HBRUSH hbrNum = CreateSolidBrush(RGB(32, 32, 36));
            HPEN hPenNum = CreatePen(PS_SOLID, 1, RGB(55, 55, 62));
            SelectObject(memDC, hbrNum);
            SelectObject(memDC, hPenNum);
            RoundRect(memDC, rcNumBox.left, rcNumBox.top, rcNumBox.right, rcNumBox.bottom, 6, 6);
            DeleteObject(hbrNum);
            DeleteObject(hPenNum);

            SetTextColor(memDC, RGB(255, 255, 255));
            SelectObject(memDC, pState->hFontBold);
            std::wstring countStr = std::to_wstring(pState->count) + L" consists";
            RECT rcNumT = rcNumBox;
            DrawTextW(memDC, countStr.c_str(), -1, &rcNumT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            pState->hitItems.push_back({ ExportDialogState::TARGET_COUNT_BOX, rcNumBox });

            DrawModernButton(memDC, rcPlus, L"+", pState->hoveredTarget == ExportDialogState::TARGET_COUNT_PLUS, false, false, pState->hFontMain);
            pState->hitItems.push_back({ ExportDialogState::TARGET_COUNT_PLUS, rcPlus });

            // Row B: Sequence Styles & Separator
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblStyles = { 20, 197, colSplitX - 10, 215 };
            DrawTextW(memDC, L"Numbering Style:", -1, &rcLblStyles, DT_LEFT | DT_SINGLELINE);

            RECT rcLblSep = { colSplitX + 10, 197, w - 20, 215 };
            DrawTextW(memDC, L"Separator:", -1, &rcLblSep, DT_LEFT | DT_SINGLELINE);

            // 5 styles in left column with 6px gaps
            int leftAvailW = (colSplitX - 10) - 20;
            int sColW = (leftAvailW - 4 * 6) / 5;
            const wchar_t* styleNames[] = { L"1, 2, 3...", L"01, 02...", L"A, B, C...", L"I, II...", L"\u23F1 Time" };
            ExportDialogState::ClickTarget styleTargets[] = {
                ExportDialogState::TARGET_STYLE_0,
                ExportDialogState::TARGET_STYLE_1,
                ExportDialogState::TARGET_STYLE_2,
                ExportDialogState::TARGET_STYLE_3,
                ExportDialogState::TARGET_STYLE_4
            };
            for (int s = 0; s < 5; ++s)
            {
                RECT rcSt = { 20 + s * (sColW + 6), 217, 20 + s * (sColW + 6) + sColW, 249 };
                DrawTabPill(rcSt, styleNames[s], pState->styleIndex == s, pState->hoveredTarget == styleTargets[s], styleTargets[s]);
            }

            // 4 separators in right column with 8px gaps
            int rightAvailW = (w - 20) - (colSplitX + 10);
            int sepColW = (rightAvailW - 3 * 8) / 4;
            const wchar_t* sepNames[] = { L"_", L"-", L"[Space]", L"None" };
            ExportDialogState::ClickTarget sepTargets[] = {
                ExportDialogState::TARGET_SEP_0,
                ExportDialogState::TARGET_SEP_1,
                ExportDialogState::TARGET_SEP_2,
                ExportDialogState::TARGET_SEP_3
            };
            for (int sp = 0; sp < 4; ++sp)
            {
                RECT rcSp = { (colSplitX + 10) + sp * (sepColW + 8), 217, (colSplitX + 10) + sp * (sepColW + 8) + sepColW, 249 };
                DrawTabPill(rcSp, sepNames[sp], pState->separatorIndex == sp, pState->hoveredTarget == sepTargets[sp], sepTargets[sp]);
            }

            // Row C: Total Units Sizing Mode (DYNAMIC vs FIXED)
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblSizing = { 20, 257, w - 20, 275 };
            DrawTextW(memDC, L"Consist Total Units Sizing Mode (Click preview badges below to customize individual consists):", -1, &rcLblSizing, DT_LEFT | DT_SINGLELINE);

            // Dynamic Pill
            std::wstring dynamicLabel = L"Dynamic Pool-Sum (" + std::to_wstring(minTotal) + (minTotal == maxTotal ? L"" : (L"–" + std::to_wstring(maxTotal))) + L" units)";
            RECT rcDynPill = { 20, 277, 20 + 310, 309 };
            DrawTabPill(rcDynPill, dynamicLabel.c_str(), pState->autoSizingMode == 0, pState->hoveredTarget == ExportDialogState::TARGET_SIZING_DYNAMIC, ExportDialogState::TARGET_SIZING_DYNAMIC);

            // Fixed Pill & Stepper
            RECT rcFixPill = { rcDynPill.right + 12, 277, rcDynPill.right + 12 + 150, 309 };
            DrawTabPill(rcFixPill, L"Fixed Total Units", pState->autoSizingMode == 1, pState->hoveredTarget == ExportDialogState::TARGET_SIZING_FIXED, ExportDialogState::TARGET_SIZING_FIXED);

            RECT rcFixMinus = { rcFixPill.right + 12, 277, rcFixPill.right + 12 + 36, 309 };
            RECT rcFixBox   = { rcFixMinus.right + 6, 277, rcFixMinus.right + 6 + 96, 309 };
            RECT rcFixPlus  = { rcFixBox.right + 6, 277, rcFixBox.right + 6 + 36, 309 };

            DrawModernButton(memDC, rcFixMinus, L"-", pState->hoveredTarget == ExportDialogState::TARGET_SIZING_MINUS, false, false, pState->hFontMain);
            pState->hitItems.push_back({ ExportDialogState::TARGET_SIZING_MINUS, rcFixMinus });

            HBRUSH hbrFixBox = CreateSolidBrush(pState->autoSizingMode == 1 ? RGB(32, 32, 40) : RGB(26, 26, 28));
            HPEN hPenFixBox = CreatePen(PS_SOLID, 1, pState->autoSizingMode == 1 ? RGB(0, 140, 240) : RGB(50, 50, 56));
            SelectObject(memDC, hbrFixBox);
            SelectObject(memDC, hPenFixBox);
            RoundRect(memDC, rcFixBox.left, rcFixBox.top, rcFixBox.right, rcFixBox.bottom, 6, 6);
            DeleteObject(hbrFixBox);
            DeleteObject(hPenFixBox);

            SetTextColor(memDC, pState->autoSizingMode == 1 ? RGB(255, 255, 255) : RGB(140, 140, 145));
            SelectObject(memDC, pState->hFontBold);
            std::wstring fixUnitStr = std::to_wstring(pState->autoFixedUnits) + L" units";
            RECT rcFixT = rcFixBox;
            DrawTextW(memDC, fixUnitStr.c_str(), -1, &rcFixT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            pState->hitItems.push_back({ ExportDialogState::TARGET_SIZING_BOX, rcFixBox });

            DrawModernButton(memDC, rcFixPlus, L"+", pState->hoveredTarget == ExportDialogState::TARGET_SIZING_PLUS, false, false, pState->hFontMain);
            pState->hitItems.push_back({ ExportDialogState::TARGET_SIZING_PLUS, rcFixPlus });

            // Row D: Preview Box
            RECT rcLblPrev = { 20, 317, w - 20, 335 };
            std::wstring prevTitle = L"Generated Consist Files Preview (" + std::to_wstring(pState->count) + L" consists \u2022 Click any [u] badge to customize that consist):";
            DrawTextW(memDC, prevTitle.c_str(), -1, &rcLblPrev, DT_LEFT | DT_SINGLELINE);

            RECT rcPrevBox = { 20, 337, w - 20, h - 62 };
            HBRUSH hbrPrev = CreateSolidBrush(RGB(16, 16, 18));
            HPEN hPenPrev = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
            SelectObject(memDC, hbrPrev);
            SelectObject(memDC, hPenPrev);
            RoundRect(memDC, rcPrevBox.left, rcPrevBox.top, rcPrevBox.right, rcPrevBox.bottom, 6, 6);
            DeleteObject(hbrPrev);
            DeleteObject(hPenPrev);

            // Render Preview Items (2 Columns)
            int itemH = 28;
            int colW = (rcPrevBox.right - rcPrevBox.left - 24) / 2;
            int visibleRows = (rcPrevBox.bottom - rcPrevBox.top - 16) / (itemH + 4);
            int maxItems = (std::min)(pState->count, visibleRows * 2);

            std::wstring sep = (pState->separatorIndex == 0) ? L"_" : ((pState->separatorIndex == 1) ? L"-" : ((pState->separatorIndex == 2) ? L" " : L""));

            for (int i = 1; i <= maxItems; ++i)
            {
                int col = (i - 1) % 2;
                int row = (i - 1) / 2;

                int ix = rcPrevBox.left + 12 + col * (colW + 8);
                int iy = rcPrevBox.top + 8 + row * (itemH + 4);
                RECT rcItem = { ix, iy, ix + colW, iy + itemH };

                HBRUSH hbrItem = CreateSolidBrush(RGB(26, 26, 30));
                HPEN hPenItem = CreatePen(PS_SOLID, 1, RGB(42, 42, 48));
                SelectObject(memDC, hbrItem);
                SelectObject(memDC, hPenItem);
                RoundRect(memDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 4, 4);
                DeleteObject(hbrItem);
                DeleteObject(hPenItem);

                // Index badge
                RECT rcIdx = { rcItem.left + 6, rcItem.top + 3, rcItem.left + 36, rcItem.bottom - 3 };
                HBRUSH hbrIdx = CreateSolidBrush(RGB(38, 38, 44));
                FillRect(memDC, &rcIdx, hbrIdx);
                DeleteObject(hbrIdx);
                SetTextColor(memDC, RGB(160, 160, 170));
                SelectObject(memDC, pState->hFontSmall);
                std::wstring sIdx = L"#" + std::to_wstring(i);
                DrawTextW(memDC, sIdx.c_str(), -1, &rcIdx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                // Formatted Filename
                std::wstring fn = BatchConsistGenerator::FormatSerializedName(pState->baseName, i, pState->count, pState->styleIndex, sep) + L".con";
                RECT rcFn = { rcItem.left + 42, rcItem.top, rcItem.right - 92, rcItem.bottom };
                SetTextColor(memDC, RGB(235, 235, 240));
                SelectObject(memDC, pState->hFontMono);
                DrawTextW(memDC, fn.c_str(), -1, &rcFn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);

                // Unit count badge on preview pill (Clickable Button)
                int thisUnits = (i - 1 < (int)pState->autoConsistUnits.size()) ? pState->autoConsistUnits[i - 1] : ((pState->autoSizingMode == 1) ? pState->autoFixedUnits : 0);
                RECT rcPillUnits = { rcItem.right - 88, rcItem.top + 2, rcItem.right - 4, rcItem.bottom - 2 };
                
                bool isHoverBadge = (pState->hoveredTarget == ExportDialogState::TARGET_PREVIEW_UNIT_BADGE && pState->hoveredIdx == i - 1);
                std::wstring uBadgeText = (thisUnits > 0) ? (std::to_wstring(thisUnits) + L" Units") : L"Dynamic";
                
                DrawModernButton(memDC, rcPillUnits, uBadgeText.c_str(), isHoverBadge, false, thisUnits > 0, pState->hFontSmall);
                pState->hitItems.push_back({ ExportDialogState::TARGET_PREVIEW_UNIT_BADGE, rcPillUnits, i - 1 });
            }

            if (pState->count > maxItems)
            {
                RECT rcMore = { rcPrevBox.left, rcPrevBox.bottom - 20, rcPrevBox.right, rcPrevBox.bottom - 2 };
                SetTextColor(memDC, RGB(120, 120, 130));
                SelectObject(memDC, pState->hFontSmall);
                std::wstring moreStr = L"... and " + std::to_wstring(pState->count - maxItems) + L" more consists will be generated";
                DrawTextW(memDC, moreStr.c_str(), -1, &rcMore, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        else // Manual Mode
        {
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblManual = { 20, 137, w - 340, 169 };
            std::wstring manTitle = L"Manual Consist Filenames & Sizes (" + std::to_wstring(pState->manualItems.size()) + L" consists):";
            DrawTextW(memDC, manTitle.c_str(), -1, &rcLblManual, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // [ ⚙ Set Sizing For All... ] button
            RECT rcSetAllBtn = { w - 20 - 300, 137, w - 20 - 145, 169 };
            DrawModernButton(memDC, rcSetAllBtn, L"Set Sizing For All...", pState->hoveredTarget == ExportDialogState::TARGET_SET_ALL_MANUAL_SIZING, false, false, pState->hFontBold, pState->hFontIcon, L"\xE713");
            pState->hitItems.push_back({ ExportDialogState::TARGET_SET_ALL_MANUAL_SIZING, rcSetAllBtn });

            // [+ Add Consist Name] button
            RECT rcAddBtn = { w - 20 - 135, 137, w - 20, 169 };
            DrawModernButton(memDC, rcAddBtn, L"Add Consist", pState->hoveredTarget == ExportDialogState::TARGET_ADD_MANUAL, false, true, pState->hFontBold, pState->hFontIcon, L"\xE710");
            pState->hitItems.push_back({ ExportDialogState::TARGET_ADD_MANUAL, rcAddBtn });

            // Table Container
            RECT rcTable = { 20, 177, w - 20, h - 62 };
            HBRUSH hbrTable = CreateSolidBrush(RGB(16, 16, 18));
            HPEN hPenTable = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
            SelectObject(memDC, hbrTable);
            SelectObject(memDC, hPenTable);
            RoundRect(memDC, rcTable.left, rcTable.top, rcTable.right, rcTable.bottom, 6, 6);
            DeleteObject(hbrTable);
            DeleteObject(hPenTable);

            if (pState->manualItems.empty())
            {
                RECT rcEmpty = rcTable;
                SetTextColor(memDC, RGB(140, 140, 150));
                SelectObject(memDC, pState->hFontMain);
                DrawTextW(memDC, L"No consist filenames defined.\r\nClick [+ Add Consist] above to add consists.", -1, &rcEmpty, DT_CENTER | DT_VCENTER);
            }
            else
            {
                int rowH = 36;
                int maxVisible = (rcTable.bottom - rcTable.top - 8) / rowH;
                int renderCount = (std::min)((int)pState->manualItems.size(), maxVisible);

                for (int r = 0; r < renderCount; ++r)
                {
                    int ry = rcTable.top + 6 + r * rowH;
                    RECT rcRow = { rcTable.left + 8, ry, rcTable.right - 8, ry + rowH - 4 };

                    bool isHoverRow = (pState->hoveredTarget == ExportDialogState::TARGET_EDIT_MANUAL && pState->hoveredIdx == r);
                    COLORREF rowBg = isHoverRow ? RGB(36, 36, 42) : RGB(24, 24, 28);

                    HBRUSH hbrR = CreateSolidBrush(rowBg);
                    HPEN hpR = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
                    SelectObject(memDC, hbrR);
                    SelectObject(memDC, hpR);
                    RoundRect(memDC, rcRow.left, rcRow.top, rcRow.right, rcRow.bottom, 4, 4);
                    DeleteObject(hbrR);
                    DeleteObject(hpR);

                    // Row index badge
                    RECT rcIdx = { rcRow.left + 8, rcRow.top + 3, rcRow.left + 42, rcRow.bottom - 3 };
                    SetTextColor(memDC, RGB(160, 160, 170));
                    SelectObject(memDC, pState->hFontBold);
                    std::wstring rIdxStr = L"#" + std::to_wstring(r + 1);
                    DrawTextW(memDC, rIdxStr.c_str(), -1, &rcIdx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // Filename + edit prompt
                    RECT rcName = { rcRow.left + 50, rcRow.top, rcRow.right - 210, rcRow.bottom };
                    SetTextColor(memDC, RGB(235, 235, 240));
                    SelectObject(memDC, pState->hFontMono);
                    std::wstring fullConName = pState->manualItems[r].name + L".con";
                    DrawTextW(memDC, fullConName.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    pState->hitItems.push_back({ ExportDialogState::TARGET_EDIT_MANUAL, rcName, r });

                    // Edit button hint
                    RECT rcEditHint = { rcRow.right - 200, rcRow.top + 3, rcRow.right - 160, rcRow.bottom - 3 };
                    DrawModernButton(memDC, rcEditHint, L"Edit", isHoverRow, false, false, pState->hFontSmall, pState->hFontIcon, L"\xE70F");
                    pState->hitItems.push_back({ ExportDialogState::TARGET_EDIT_MANUAL, rcEditHint, r });

                    // Total Units Sizing Badge Button (Dynamic vs Fixed X units)
                    RECT rcUnitsBadge = { rcRow.right - 152, rcRow.top + 3, rcRow.right - 36, rcRow.bottom - 3 };
                    int uVal = pState->manualItems[r].targetUnits;
                    std::wstring uBadgeStr = (uVal > 0) ? (std::to_wstring(uVal) + L" Units") : L"Dynamic";
                    bool isHoverU = (pState->hoveredTarget == ExportDialogState::TARGET_TOGGLE_MANUAL_SIZING && pState->hoveredIdx == r);
                    DrawModernButton(memDC, rcUnitsBadge, uBadgeStr.c_str(), isHoverU, false, uVal > 0, pState->hFontSmall);
                    pState->hitItems.push_back({ ExportDialogState::TARGET_TOGGLE_MANUAL_SIZING, rcUnitsBadge, r });

                    // Delete button
                    RECT rcDel = { rcRow.right - 32, rcRow.top + 3, rcRow.right - 6, rcRow.bottom - 3 };
                    bool isHoverDel = (pState->hoveredTarget == ExportDialogState::TARGET_DELETE_MANUAL && pState->hoveredIdx == r);
                    DrawModernButton(memDC, rcDel, L"", isHoverDel, false, false, pState->hFontSmall, pState->hFontIcon, L"\xE74D");
                    pState->hitItems.push_back({ ExportDialogState::TARGET_DELETE_MANUAL, rcDel, r });
                }
            }
        }

        // 6. Footer Area (Height = 54px)
        int footerH = 54;
        int footBtnH = 34;
        int footBtnY = (h - footerH) + (footerH - footBtnH) / 2;

        RECT rcFooter = { 0, h - footerH, w, h };
        HBRUSH hbrF = CreateSolidBrush(RGB(26, 26, 30));
        FillRect(memDC, &rcFooter, hbrF);
        DeleteObject(hbrF);

        HPEN hPenF = CreatePen(PS_SOLID, 1, borderCol);
        SelectObject(memDC, hPenF);
        MoveToEx(memDC, 0, h - footerH, NULL);
        LineTo(memDC, w, h - footerH);
        DeleteObject(hPenF);

        int genBtnW = 210;
        RECT rcGen = { w - 20 - genBtnW, footBtnY, w - 20, footBtnY + footBtnH };

        // Checkbox: Overwrite existing
        int chkBoxSize = 18;
        int chkY = (h - footerH) + (footerH - chkBoxSize) / 2;
        RECT rcChkBox = { 20, chkY, 20 + chkBoxSize, chkY + chkBoxSize };
        COLORREF chkBg = pState->overwriteExisting ? RGB(0, 120, 215) : RGB(36, 36, 40);
        COLORREF chkBorder = pState->overwriteExisting ? RGB(0, 140, 240) : RGB(70, 70, 80);
        HBRUSH hbrChk = CreateSolidBrush(chkBg);
        HPEN hPenChk = CreatePen(PS_SOLID, 1, chkBorder);
        SelectObject(memDC, hbrChk);
        SelectObject(memDC, hPenChk);
        RoundRect(memDC, rcChkBox.left, rcChkBox.top, rcChkBox.right, rcChkBox.bottom, 4, 4);
        DeleteObject(hbrChk);
        DeleteObject(hPenChk);

        if (pState->overwriteExisting)
        {
            SetTextColor(memDC, RGB(255, 255, 255));
            SelectObject(memDC, pState->hFontBold);
            DrawTextW(memDC, L"\u2713", -1, &rcChkBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT rcChkLabel = { 20 + chkBoxSize + 8, footBtnY, rcGen.left - 16, footBtnY + footBtnH };
        SetTextColor(memDC, RGB(210, 210, 215));
        SelectObject(memDC, pState->hFontMain);
        DrawTextW(memDC, L"Overwrite existing files if filenames already exist", -1, &rcChkLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        
        RECT rcChkFull = { 20, footBtnY, rcGen.left - 16, footBtnY + footBtnH };
        pState->hitItems.push_back({ ExportDialogState::TARGET_OVERWRITE_CHK, rcChkFull });

        // Action Button: [Generate N Consists]
        int genCount = (pState->namingMode == 0) ? pState->count : (int)pState->manualItems.size();
        std::wstring genBtnText = L"Generate " + std::to_wstring(genCount) + L" Consists";
        DrawModernButton(memDC, rcGen, genBtnText.c_str(), pState->hoveredTarget == ExportDialogState::TARGET_GENERATE_BTN, false, true, pState->hFontBold, pState->hFontIcon, L"\xE768");
        pState->hitItems.push_back({ ExportDialogState::TARGET_GENERATE_BTN, rcGen });

        // Select old objects
        SelectObject(memDC, holdPen);
        DeleteObject(hPenLine);

        // Outer 1px Self-Drawn Border (Independent of OS version / theme)
        HPEN hPenOuter = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldPenOuter = (HPEN)SelectObject(memDC, hPenOuter);
        HBRUSH hOldBrOuter = (HBRUSH)SelectObject(memDC, hNullBr);
        Rectangle(memDC, 0, 0, w, h);
        SelectObject(memDC, hOldPenOuter);
        SelectObject(memDC, hOldBrOuter);
        DeleteObject(hPenOuter);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
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
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontSubtitle) DeleteObject(pState->hFontSubtitle);
            if (pState->hFontSection) DeleteObject(pState->hFontSection);
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontMono) DeleteObject(pState->hFontMono);
            if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
            delete pState;
        }
        g_hBatchGeneratorDlg = NULL;
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ShowBatchConsistGeneratorDialog(HWND hWndParent, int presetIndex)
{
    if (g_hBatchGeneratorDlg && IsWindow(g_hBatchGeneratorDlg))
    {
        ShowWindow(g_hBatchGeneratorDlg, SW_RESTORE);
        SetForegroundWindow(g_hBatchGeneratorDlg);
        BringWindowToTop(g_hBatchGeneratorDlg);
        return;
    }

    static bool s_ExportRegistered = false;
    const wchar_t* szClassName = L"TSCB_BatchConsistExportDlg";

    if (!s_ExportRegistered)
    {
        WNDCLASSEXW wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcex.lpfnWndProc = ExportDlgProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hIcon = NULL;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(RGB(22, 22, 25));
        wcex.lpszMenuName = NULL;
        wcex.lpszClassName = szClassName;
        wcex.hIconSm = NULL;

        RegisterClassExW(&wcex);
        s_ExportRegistered = true;
    }

    ExportDialogState* pState = new ExportDialogState();
    pState->hParent = hWndParent;
    if (presetIndex >= 0 && presetIndex < (int)PoolManager::g_PoolPresetsCache.size())
    {
        PoolManager::g_ActivePresetIndex = presetIndex;
        pState->preset = PoolManager::g_PoolPresetsCache[presetIndex];
    }
    else if (!PoolManager::g_PoolPresetsCache.empty())
    {
        if (PoolManager::g_ActivePresetIndex < 0 || PoolManager::g_ActivePresetIndex >= (int)PoolManager::g_PoolPresetsCache.size())
            PoolManager::g_ActivePresetIndex = 0;
        pState->preset = PoolManager::g_PoolPresetsCache[PoolManager::g_ActivePresetIndex];
    }
    pState->targetFolder = GetAppConsistsDirectory();
    
    // Clean base name from preset name
    pState->baseName = pState->preset.presetName;
    for (auto& ch : pState->baseName)
    {
        if (ch == L' ' || ch == L'-') ch = L'_';
    }
    if (pState->baseName.empty()) pState->baseName = L"Consist";

    // Set initial auto fixed units to sum of max capacity or 6
    int sumMax = 0;
    for (const auto& pl : pState->preset.pools)
    {
        sumMax += pl.maxCount;
    }
    pState->autoFixedUnits = (sumMax > 0) ? sumMax : 6;
    pState->SyncAutoUnits();

    int dlgW = 940;
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
        WS_EX_APPWINDOW, szClassName, L"Batch Consist Export & Generation",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME,
        x, y, dlgW, dlgH, hWndParent, NULL, GetModuleHandleW(NULL), pState
    );

    if (!hDlg)
    {
        delete pState;
        return;
    }

    g_hBatchGeneratorDlg = hDlg;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = RGB(55, 55, 62);
    DwmSetWindowAttribute(hDlg, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hDlg, &margins);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    return;
}

