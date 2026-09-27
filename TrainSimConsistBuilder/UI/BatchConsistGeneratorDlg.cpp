#include "BatchConsistGeneratorDlg.h"
#include "PoolManagerDlg.h"
#include "CustomTitleBar.h"
#include "ModernContextMenu.h"
#include "CustomDropDownMenu.h"
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
    HWND hTitleBar = NULL;
    HWND hEditBaseName = NULL;
    PoolManager::PoolPreset preset;
    std::wstring targetFolder;

    // Preset & Pool Multi-Select Filtering
    int selectedPresetIdx = 0;
    std::vector<int> selectedPoolIndices; // Subset of pool indices within preset. Empty = All pools in preset

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
        TARGET_POOLS_DROPDOWN,
        TARGET_MANAGE_POOLS_BTN,
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

static PoolManager::PoolPreset GetEffectivePreset(const ExportDialogState* pState)
{
    if (!pState) return PoolManager::PoolPreset();
    PoolManager::PoolPreset eff = pState->preset;
    if (!pState->selectedPoolIndices.empty() && pState->selectedPoolIndices.size() < pState->preset.pools.size())
    {
        eff.pools.clear();
        for (int idx : pState->selectedPoolIndices)
        {
            if (idx >= 0 && idx < (int)pState->preset.pools.size())
            {
                eff.pools.push_back(pState->preset.pools[idx]);
            }
        }
    }
    return eff;
}

static void ShowPresetDropdown(HWND hWnd, ExportDialogState* pState, const RECT& rcAnchor)
{
    PoolManager::InitializePoolPresets();
    if (PoolManager::g_PoolPresetsCache.empty()) return;

    std::vector<DropDownItem> items;
    int currentSelId = 1;
    for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
    {
        const auto& p = PoolManager::g_PoolPresetsCache[i];
        int totalU = 0;
        for (const auto& pl : p.pools) totalU += (int)pl.units.size();

        std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
        std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
        bool isCurrent = ((int)i == pState->selectedPresetIdx);
        if (isCurrent) currentSelId = (int)i + 1;
        items.push_back(DropDownItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, isCurrent, true));
    }

    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items, currentSelId);
    if (chosen > 0)
    {
        int selIdx = chosen - 1;
        pState->selectedPresetIdx = selIdx;
        PoolManager::g_ActivePresetIndex = selIdx;
        pState->preset = PoolManager::g_PoolPresetsCache[selIdx];
        pState->selectedPoolIndices.clear();

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

        InvalidateRect(hWnd, NULL, TRUE);
    }
}

static void ShowPoolMultiDropdown(HWND hWnd, ExportDialogState* pState, const RECT& rcAnchor)
{
    if (pState->preset.pools.empty())
    {
        ShowModernMessageBox(hWnd, L"Selected preset has no pools defined.", L"Pools", MB_OK | MB_ICONINFORMATION);
        return;
    }

    size_t numPools = pState->preset.pools.size();
    bool isAllSelected = (numPools > 0 && (pState->selectedPoolIndices.empty() || pState->selectedPoolIndices.size() >= numPools));
    bool hasAnySelected = (!pState->selectedPoolIndices.empty() && !isAllSelected);

    std::vector<DropDownItem> items;
    items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Preset (All Pools)", L"", isAllSelected, hasAnySelected));

    for (size_t i = 0; i < numPools; ++i)
    {
        const auto& pl = pState->preset.pools[i];
        std::wstring label = std::to_wstring(i + 1) + L". " + (pl.name.empty() ? L"Pool #" + std::to_wstring(i + 1) : pl.name);
        std::wstring modeStr = (pl.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
        std::wstring tag = std::to_wstring(pl.units.size()) + L" units • [" + modeStr + L", Min:" + std::to_wstring(pl.minCount) + L", Max:" + std::to_wstring(pl.maxCount) + L"]";
        bool isChecked = isAllSelected || (std::find(pState->selectedPoolIndices.begin(), pState->selectedPoolIndices.end(), (int)i) != pState->selectedPoolIndices.end());
        items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
    }

    CustomDropDownMenu::ShowMultiSelect(hWnd, rcAnchor, items, [hWnd, pState](const std::vector<DropDownItem>& updatedItems) {
        pState->selectedPoolIndices.clear();
        for (size_t k = 1; k < updatedItems.size(); ++k)
        {
            if (updatedItems[k].isChecked)
            {
                pState->selectedPoolIndices.push_back((int)k - 1);
            }
        }

        // Recalculate auto fixed units default from effective preset
        auto eff = GetEffectivePreset(pState);
        int sumMax = 0;
        for (const auto& pl : eff.pools) sumMax += pl.maxCount;
        pState->autoFixedUnits = (sumMax > 0) ? sumMax : 6;
        pState->SyncAutoUnits();

        InvalidateRect(hWnd, NULL, TRUE);
    });
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

static LRESULT CALLBACK ExportDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ExportDialogState* pState = (ExportDialogState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

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

    case WM_ERASEBKGND:
        return TRUE;

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // 1. Border resize handling (6px border when not zoomed)
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

        // 2. Forward to CustomTitleBar for Y = 0..66
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

        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        if (!pState) break;
        pState->namingMode = (int)wParam;
        if (pState->namingMode == 0)
        {
            if (pState->hEditBaseName && IsWindow(pState->hEditBaseName))
                ShowWindow(pState->hEditBaseName, SW_SHOW);
        }
        else
        {
            if (pState->hEditBaseName && IsWindow(pState->hEditBaseName))
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
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            InvalidateRect(pState->hTitleBar, NULL, TRUE);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
    {
        if (pState)
        {
            pState->hoveredTarget = ExportDialogState::TARGET_NONE;
            pState->hoveredIdx = -1;
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

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right > 0 ? rcClient.right : 940;

        std::vector<TitleBarTabItem> tabs = {
            { L"\xE9D9", L"Auto Consist Naming" },
            { L"\xE70F", L"Manual Consists" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, w, 66,
            20002,
            L"Batch Consist Export & Generation - TrainSim Consist Builder",
            tabs
        );

        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->namingMode);
        }

        pState->hEditBaseName = CreateWindowExW(
            0, L"EDIT", pState->baseName.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            28, 186, 380, 20, hWnd, (HMENU)201, GetModuleHandleW(NULL), NULL
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

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
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

        for (const auto& item : pState->hitItems)
        {
            if (PtInRect(&item.rc, pt))
            {
                switch (item.target)
                {
                case ExportDialogState::TARGET_CLOSE:
                case ExportDialogState::TARGET_CANCEL_BTN:
                    RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
                    DestroyWindow(hWnd);
                    return 0;

                case ExportDialogState::TARGET_MANAGE_POOLS_BTN:
                    ShowPoolManagerDialog(pState->hParent);
                    return 0;

                case ExportDialogState::TARGET_PRESET_DROPDOWN:
                    ShowPresetDropdown(hWnd, pState, item.rc);
                    return 0;

                case ExportDialogState::TARGET_POOLS_DROPDOWN:
                    ShowPoolMultiDropdown(hWnd, pState, item.rc);
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

                    auto effPreset = GetEffectivePreset(pState);
                    if (effPreset.pools.empty())
                    {
                        ShowModernMessageBox(hWnd, L"No pools selected for generation. Please select at least one pool from the Pools dropdown.", L"Validation", MB_OK | MB_ICONWARNING);
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
                        effPreset, specs, pState->targetFolder, pState->overwriteExisting, createdFiles, outErr
                    );

                    if (ok)
                    {
                        pState->isConfirmed = true;
                        pState->createdFiles = createdFiles;

                        // Trigger rescan of consists in main app
                        TriggerAppConsistsRescan();

                        std::wstring successMsg = L"Successfully generated " + std::to_wstring(createdFiles.size()) + L" consist files in:\r\n" + pState->targetFolder;
                        ShowModernMessageBox(hWnd, successMsg.c_str(), L"Batch Generation Complete", MB_OK | MB_ICONINFORMATION);

                        RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
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
        COLORREF bgCol = PoolTheme::GutterBackground;
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(memDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        COLORREF borderCol = PoolTheme::BorderLine;
        COLORREF textPrimary = PoolTheme::TextPrimary;
        COLORREF textSecondary = PoolTheme::TextSecondary;
        COLORREF accentCol = PoolTheme::AccentBlue;

        SetBkMode(memDC, TRANSPARENT);

        // 2. Sub-Header Toolbar (Y = 66 to 114, Height = 48px) - Matches Active Tab color seamlessly
        int toolbarY = 66;
        int toolbarH = 48;
        RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
        COLORREF tbBg = PoolTheme::ToolbarBackground; // RGB(52, 22, 27) matching active tab
        HBRUSH hbrTb = CreateSolidBrush(tbBg);
        FillRect(memDC, &rcToolbar, hbrTb);
        DeleteObject(hbrTb);

        // Divider Line Below Toolbar at Y = 114 (Matching tab highlight line)
        HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
        HPEN holdPen = (HPEN)SelectObject(memDC, hPenLine);
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

        // Right of Toolbar: Preset Selector Dropdown, Pools Multi-Dropdown & Manage Pools Button
        int managePoolsW = 120;
        int tbBtnH = 32;
        int tbBtnY = toolbarY + (toolbarH - tbBtnH) / 2;
        RECT rcManagePoolsBtn = { w - 20 - managePoolsW, tbBtnY, w - 20, tbBtnY + tbBtnH };

        int poolPickerW = 180;
        RECT rcPoolPicker = { rcManagePoolsBtn.left - 8 - poolPickerW, tbBtnY, rcManagePoolsBtn.left - 8, tbBtnY + tbBtnH };

        int presetPickerW = 180;
        RECT rcPresetPicker = { rcPoolPicker.left - 8 - presetPickerW, tbBtnY, rcPoolPicker.left - 8, tbBtnY + tbBtnH };

        // Left of Toolbar: Preset & Selection Summary Info
        auto effPreset = GetEffectivePreset(pState);
        int totalUnitsInSelected = 0;
        for (const auto& pl : effPreset.pools) totalUnitsInSelected += (int)pl.units.size();

        if (pState->hFontIcon)
        {
            SelectObject(memDC, pState->hFontIcon);
            SetTextColor(memDC, accentCol);
            RECT rcPoolIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
            DrawTextW(memDC, L"\xE71D", -1, &rcPoolIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, textPrimary);
        RECT rcPoolTitle = { 46, toolbarY, rcPresetPicker.left - 12, toolbarY + toolbarH };
        std::wstring poolSummaryStr = L"Active: " + pState->preset.presetName + L" (" +
            std::to_wstring(effPreset.pools.size()) + L"/" + std::to_wstring(pState->preset.pools.size()) +
            L" pools • " + std::to_wstring(totalUnitsInSelected) + L" units)";
        DrawTextW(memDC, poolSummaryStr.c_str(), -1, &rcPoolTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        bool isHoverPreset = (pState->hoveredTarget == ExportDialogState::TARGET_PRESET_DROPDOWN);
        bool isHoverPools  = (pState->hoveredTarget == ExportDialogState::TARGET_POOLS_DROPDOWN);
        bool isHoverManage = (pState->hoveredTarget == ExportDialogState::TARGET_MANAGE_POOLS_BTN);

        // 1. Draw Preset Selector Dropdown Box
        COLORREF comboBg = isHoverPreset ? RGB(72, 30, 36) : RGB(40, 16, 20);
        COLORREF comboBorder = isHoverPreset ? RGB(110, 45, 54) : RGB(78, 32, 38);
        HBRUSH hbrCombo = CreateSolidBrush(comboBg);
        HPEN hPenCombo = CreatePen(PS_SOLID, 1, comboBorder);
        SelectObject(memDC, hbrCombo);
        SelectObject(memDC, hPenCombo);
        RoundRect(memDC, rcPresetPicker.left, rcPresetPicker.top, rcPresetPicker.right, rcPresetPicker.bottom, 6, 6);
        DeleteObject(hbrCombo);
        DeleteObject(hPenCombo);

        RECT rcComboText = { rcPresetPicker.left + 10, rcPresetPicker.top, rcPresetPicker.right - 22, rcPresetPicker.bottom };
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, textPrimary);
        std::wstring presetLabel = L"Preset: " + pState->preset.presetName;
        DrawTextW(memDC, presetLabel.c_str(), -1, &rcComboText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rcChevron = { rcPresetPicker.right - 20, rcPresetPicker.top, rcPresetPicker.right - 6, rcPresetPicker.bottom };
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

        // 2. Draw Pools Multi-Select Dropdown Box
        COLORREF poolBg = isHoverPools ? RGB(72, 30, 36) : RGB(40, 16, 20);
        COLORREF poolBorder = isHoverPools ? RGB(110, 45, 54) : RGB(78, 32, 38);
        HBRUSH hbrPool = CreateSolidBrush(poolBg);
        HPEN hPenPool = CreatePen(PS_SOLID, 1, poolBorder);
        SelectObject(memDC, hbrPool);
        SelectObject(memDC, hPenPool);
        RoundRect(memDC, rcPoolPicker.left, rcPoolPicker.top, rcPoolPicker.right, rcPoolPicker.bottom, 6, 6);
        DeleteObject(hbrPool);
        DeleteObject(hPenPool);

        size_t numTotalPools = pState->preset.pools.size();
        bool isAllPools = (numTotalPools > 0 && (pState->selectedPoolIndices.empty() || pState->selectedPoolIndices.size() >= numTotalPools));
        std::wstring poolsLabel;
        if (isAllPools)
        {
            poolsLabel = L"Pools: All (" + std::to_wstring(numTotalPools) + L")";
        }
        else if (pState->selectedPoolIndices.size() == 1)
        {
            int pIdx = pState->selectedPoolIndices[0];
            if (pIdx >= 0 && pIdx < (int)numTotalPools)
            {
                std::wstring pName = pState->preset.pools[pIdx].name;
                if (pName.empty()) pName = L"Pool #" + std::to_wstring(pIdx + 1);
                poolsLabel = L"Pool: " + pName;
            }
            else
            {
                poolsLabel = L"1 Pool Selected";
            }
        }
        else if (pState->selectedPoolIndices.empty())
        {
            poolsLabel = L"Pools: None";
        }
        else
        {
            poolsLabel = L"Pools: " + std::to_wstring(pState->selectedPoolIndices.size()) + L"/" + std::to_wstring(numTotalPools);
        }

        RECT rcPoolText = { rcPoolPicker.left + 10, rcPoolPicker.top, rcPoolPicker.right - 22, rcPoolPicker.bottom };
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, textPrimary);
        DrawTextW(memDC, poolsLabel.c_str(), -1, &rcPoolText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rcPoolChevron = { rcPoolPicker.right - 20, rcPoolPicker.top, rcPoolPicker.right - 6, rcPoolPicker.bottom };
        int pChevCX = (rcPoolChevron.left + rcPoolChevron.right) / 2;
        int pChevCY = (rcPoolChevron.top + rcPoolChevron.bottom) / 2;
        COLORREF pArrCol = isHoverPools ? RGB(255, 255, 255) : textSecondary;
        HBRUSH hBrPChev = CreateSolidBrush(pArrCol);
        HPEN hPenPChev = CreatePen(PS_SOLID, 1, pArrCol);
        HBRUSH hOldBrPC = (HBRUSH)SelectObject(memDC, hBrPChev);
        HPEN hOldPenPC = (HPEN)SelectObject(memDC, hPenPChev);
        POINT ptsPChev[3] = { { pChevCX - 4, pChevCY - 2 }, { pChevCX + 4, pChevCY - 2 }, { pChevCX, pChevCY + 3 } };
        Polygon(memDC, ptsPChev, 3);
        SelectObject(memDC, hOldBrPC);
        SelectObject(memDC, hOldPenPC);
        DeleteObject(hBrPChev);
        DeleteObject(hPenPChev);

        pState->hitItems.push_back({ ExportDialogState::TARGET_POOLS_DROPDOWN, rcPoolPicker, -1 });

        // 3. Draw Manage Pools Button
        DrawModernButton(memDC, rcManagePoolsBtn, L"Manage Pools", isHoverManage, false, false, pState->hFontMain, pState->hFontIcon, L"\xE713");
        pState->hitItems.push_back({ ExportDialogState::TARGET_MANAGE_POOLS_BTN, rcManagePoolsBtn, -1 });

        // 3. Target Folder Banner Card (Y = 120..152, Height = 32px)
        RECT rcFolderCard = { 20, 120, w - 20, 152 };
        COLORREF cardBg = PoolTheme::CardBackground;
        COLORREF cardBorder = PoolTheme::CardBorder;
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

        // Calculate pool capacity range from effective preset
        int minTotal = 0, maxTotal = 0;
        for (const auto& pl : effPreset.pools)
        {
            minTotal += (std::max)(0, pl.minCount);
            maxTotal += (std::max)((std::max)(0, pl.minCount), pl.maxCount);
        }

        // 4. Mode Content (starts at Y = 160)
        if (pState->namingMode == 0) // Auto-Numbered Mode
        {
            int colSplitX = 20 + (w - 40 - 12) / 2;

            // Ensure edit control is strictly positioned inside rcEditFrame
            if (pState->hEditBaseName && IsWindow(pState->hEditBaseName))
            {
                SetWindowPos(pState->hEditBaseName, NULL, 28, 186, (colSplitX - 10) - 36, 20, SWP_NOZORDER | SWP_NOACTIVATE);
            }

            // Row A: Base Name & Count
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblBase = { 20, 160, colSplitX - 10, 178 };
            DrawTextW(memDC, L"Consist Base Filename:", -1, &rcLblBase, DT_LEFT | DT_SINGLELINE);

            // Edit frame for base name
            RECT rcEditFrame = { 20, 180, colSplitX - 10, 212 };
            HBRUSH hbrEditF = CreateSolidBrush(RGB(30, 30, 32));
            HPEN hPenEditF = CreatePen(PS_SOLID, 1, RGB(65, 65, 75));
            SelectObject(memDC, hbrEditF);
            SelectObject(memDC, hPenEditF);
            RoundRect(memDC, rcEditFrame.left, rcEditFrame.top, rcEditFrame.right, rcEditFrame.bottom, 6, 6);
            DeleteObject(hbrEditF);
            DeleteObject(hPenEditF);

            RECT rcLblCount = { colSplitX + 10, 160, w - 20, 178 };
            DrawTextW(memDC, L"Number of Consists to Generate:", -1, &rcLblCount, DT_LEFT | DT_SINGLELINE);

            // Consist Count Stepper
            int countRightEdge = w - 20;
            RECT rcMinus = { colSplitX + 10, 180, colSplitX + 10 + 44, 212 };
            RECT rcPlus = { countRightEdge - 44, 180, countRightEdge, 212 };
            RECT rcNumBox = { rcMinus.right + 8, 180, rcPlus.left - 8, 212 };

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
            RECT rcLblStyles = { 20, 220, colSplitX - 10, 238 };
            DrawTextW(memDC, L"Numbering Style:", -1, &rcLblStyles, DT_LEFT | DT_SINGLELINE);

            RECT rcLblSep = { colSplitX + 10, 220, w - 20, 238 };
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
                RECT rcSt = { 20 + s * (sColW + 6), 240, 20 + s * (sColW + 6) + sColW, 272 };
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
                RECT rcSp = { (colSplitX + 10) + sp * (sepColW + 8), 240, (colSplitX + 10) + sp * (sepColW + 8) + sepColW, 272 };
                DrawTabPill(rcSp, sepNames[sp], pState->separatorIndex == sp, pState->hoveredTarget == sepTargets[sp], sepTargets[sp]);
            }

            // Row C: Total Units Sizing Mode (DYNAMIC vs FIXED)
            SetTextColor(memDC, RGB(220, 220, 225));
            SelectObject(memDC, pState->hFontSection);
            RECT rcLblSizing = { 20, 280, w - 20, 298 };
            DrawTextW(memDC, L"Consist Total Units Sizing Mode (Click preview badges below to customize individual consists):", -1, &rcLblSizing, DT_LEFT | DT_SINGLELINE);

            // Dynamic Pill
            std::wstring dynamicLabel = L"Dynamic Pool-Sum (" + std::to_wstring(minTotal) + (minTotal == maxTotal ? L"" : (L"–" + std::to_wstring(maxTotal))) + L" units)";
            RECT rcDynPill = { 20, 300, 20 + 310, 332 };
            DrawTabPill(rcDynPill, dynamicLabel.c_str(), pState->autoSizingMode == 0, pState->hoveredTarget == ExportDialogState::TARGET_SIZING_DYNAMIC, ExportDialogState::TARGET_SIZING_DYNAMIC);

            // Fixed Pill & Stepper
            RECT rcFixPill = { rcDynPill.right + 12, 300, rcDynPill.right + 12 + 150, 332 };
            DrawTabPill(rcFixPill, L"Fixed Total Units", pState->autoSizingMode == 1, pState->hoveredTarget == ExportDialogState::TARGET_SIZING_FIXED, ExportDialogState::TARGET_SIZING_FIXED);

            RECT rcFixMinus = { rcFixPill.right + 12, 300, rcFixPill.right + 12 + 36, 332 };
            RECT rcFixBox   = { rcFixMinus.right + 6, 300, rcFixMinus.right + 6 + 96, 332 };
            RECT rcFixPlus  = { rcFixBox.right + 6, 300, rcFixBox.right + 6 + 36, 332 };

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
            RECT rcLblPrev = { 20, 340, w - 20, 358 };
            std::wstring prevTitle = L"Generated Consist Files Preview (" + std::to_wstring(pState->count) + L" consists \u2022 Click any [u] badge to customize that consist):";
            DrawTextW(memDC, prevTitle.c_str(), -1, &rcLblPrev, DT_LEFT | DT_SINGLELINE);

            RECT rcPrevBox = { 20, 360, w - 20, h - 62 };
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
            RECT rcLblManual = { 20, 160, w - 340, 192 };
            std::wstring manTitle = L"Manual Consist Filenames & Sizes (" + std::to_wstring(pState->manualItems.size()) + L" consists):";
            DrawTextW(memDC, manTitle.c_str(), -1, &rcLblManual, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // [ ⚙ Set Sizing For All... ] button
            RECT rcSetAllBtn = { w - 20 - 300, 160, w - 20 - 145, 192 };
            DrawModernButton(memDC, rcSetAllBtn, L"Set Sizing For All...", pState->hoveredTarget == ExportDialogState::TARGET_SET_ALL_MANUAL_SIZING, false, false, pState->hFontBold, pState->hFontIcon, L"\xE713");
            pState->hitItems.push_back({ ExportDialogState::TARGET_SET_ALL_MANUAL_SIZING, rcSetAllBtn });

            // [+ Add Consist] button
            RECT rcAddBtn = { w - 20 - 135, 160, w - 20, 192 };
            DrawModernButton(memDC, rcAddBtn, L"Add Consist", pState->hoveredTarget == ExportDialogState::TARGET_ADD_MANUAL, false, true, pState->hFontBold, pState->hFontIcon, L"\xE710");
            pState->hitItems.push_back({ ExportDialogState::TARGET_ADD_MANUAL, rcAddBtn });

            // Table Container
            RECT rcTable = { 20, 200, w - 20, h - 62 };
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
                    RECT rcName = { rcRow.left + 50, rcRow.top, rcRow.right - 230, rcRow.bottom };
                    SetTextColor(memDC, RGB(235, 235, 240));
                    SelectObject(memDC, pState->hFontMono);
                    std::wstring fullConName = pState->manualItems[r].name + L".con";
                    DrawTextW(memDC, fullConName.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    pState->hitItems.push_back({ ExportDialogState::TARGET_EDIT_MANUAL, rcName, r });

                    // Edit button hint (widened to 64px for clean margins and proper pencil rendering)
                    RECT rcEditHint = { rcRow.right - 224, rcRow.top + 3, rcRow.right - 160, rcRow.bottom - 3 };
                    DrawModernButton(memDC, rcEditHint, L"Edit", isHoverRow, false, false, pState->hFontSmall, pState->hFontIcon, L"\xE70F");
                    pState->hitItems.push_back({ ExportDialogState::TARGET_EDIT_MANUAL, rcEditHint, r });

                    // Total Units Sizing Badge Button (Dynamic vs Fixed X units)
                    RECT rcUnitsBadge = { rcRow.right - 154, rcRow.top + 3, rcRow.right - 36, rcRow.bottom - 3 };
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

        // 5. Footer Area (Height = 54px)
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
            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
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

    case WM_NCDESTROY:
    {
        HWND hOwner = GetWindow(hWnd, GW_OWNER);
        if (hOwner && IsWindow(hOwner))
        {
            RestoreParentWindowFocus(hOwner);
        }
        break;
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
        pState->selectedPresetIdx = presetIndex;
        PoolManager::g_ActivePresetIndex = presetIndex;
        pState->preset = PoolManager::g_PoolPresetsCache[presetIndex];
    }
    else if (!PoolManager::g_PoolPresetsCache.empty())
    {
        if (PoolManager::g_ActivePresetIndex < 0 || PoolManager::g_ActivePresetIndex >= (int)PoolManager::g_PoolPresetsCache.size())
            PoolManager::g_ActivePresetIndex = 0;
        pState->selectedPresetIdx = PoolManager::g_ActivePresetIndex;
        pState->preset = PoolManager::g_PoolPresetsCache[PoolManager::g_ActivePresetIndex];
    }
    pState->selectedPoolIndices.clear();
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
        0, szClassName, L"Batch Consist Export & Generation",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    SetWindowPos(hDlg, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    ShowWindow(hDlg, SW_SHOW);

    RECT rc;
    GetClientRect(hDlg, &rc);
    int clientW = rc.right - rc.left;
    if (pState->hTitleBar && IsWindow(pState->hTitleBar))
    {
        SetWindowPos(pState->hTitleBar, NULL, 0, 0, clientW, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
    }

    RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
    UpdateWindow(hDlg);
    return;
}

