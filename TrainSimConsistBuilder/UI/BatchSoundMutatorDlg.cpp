#include "BatchSoundMutatorDlg.h"
#include "SoundStudio.h"
#include "UITheme.h"
#include "PoolManagerDlg.h"
#include "CustomListControl.h"
#include "CustomTitleBar.h"
#include "ModernMessageBox.h"
#include "CustomDropDownMenu.h"
#include "CustomScrollBar.h"
#include "../SRC/ConsistReader.h"
#include "../SRC/DatabaseManager.h"
#include "../SRC/AppLogging.h"
#include "../SRC/StockSpecReader.h"
#include "../SRC/SoundScanner.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

static HWND g_hBatchSoundMutatorDlg = NULL;
static bool s_bBatchSuppressMultiSmsWarning = false;

namespace
{
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

        bool isDeleteBtn = (iconGlyph && (wcscmp(iconGlyph, L"\xE74D") == 0 || wcscmp(iconGlyph, L"\xE711") == 0));

        if (isAccent)
        {
            bgCol = isPressed ? RGB(0, 100, 185) : (isHovered ? RGB(20, 140, 235) : RGB(0, 120, 215));
            borderCol = bgCol;
            textCol = RGB(255, 255, 255);
        }
        else if (isDeleteBtn && isHovered)
        {
            bgCol = isPressed ? RGB(105, 25, 25) : RGB(85, 25, 25);
            borderCol = RGB(220, 60, 60);
            textCol = RGB(255, 140, 140);
        }
        else if (isDeleteBtn)
        {
            bgCol = RGB(36, 36, 36);
            borderCol = RGB(65, 65, 75);
            textCol = RGB(210, 130, 130);
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

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hPen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);

        bool isTriDown = (iconGlyph && wcscmp(iconGlyph, L"__TRI_DOWN__") == 0);
        bool isTriUp   = (iconGlyph && wcscmp(iconGlyph, L"__TRI_UP__") == 0);

        if (isTriDown || isTriUp)
        {
            int triCX = (rc.left + rc.right) / 2;
            int triCY = (rc.top + rc.bottom) / 2;
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
            return;
        }

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
            int startX = rc.left + (rc.right - rc.left - totalW) / 2;
            if (startX < rc.left + 6) startX = rc.left + 6;

            SelectObject(hdc, hIconFont);
            RECT rcIcon = { startX, rc.top, startX + iconW, rc.bottom };
            DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hdc, hFont);
            RECT rcText = { startX + iconW + gap, rc.top, rc.right - 6, rc.bottom };
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (hasIcon)
        {
            SelectObject(hdc, hIconFont);
            RECT rcCopy = rc;
            DrawTextW(hdc, iconGlyph, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (hasText)
        {
            SelectObject(hdc, hFont);
            RECT rcCopy = rc;
            DrawTextW(hdc, text, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    static void DrawCustomCheckbox(HDC hdc, const RECT& rcBox, bool isChecked, bool isHovered, HFONT hIconFont = NULL)
    {
        COLORREF bg = isChecked ? RGB(0, 120, 215) : (isHovered ? RGB(45, 48, 58) : RGB(28, 30, 38));
        COLORREF border = isChecked ? RGB(0, 150, 255) : (isHovered ? RGB(90, 95, 110) : RGB(60, 65, 75));

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
        HPEN holdPen = (HPEN)SelectObject(hdc, hpen);
        RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);
        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);

        if (isChecked)
        {
            static HFONT s_hFontChk = NULL;
            if (!s_hFontChk)
            {
                s_hFontChk = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe Fluent Icons");
                if (!s_hFontChk)
                    s_hFontChk = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe MDL2 Assets");
            }

            HFONT fontToUse = hIconFont ? hIconFont : s_hFontChk;
            if (fontToUse)
            {
                HGDIOBJ oldF = SelectObject(hdc, fontToUse);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                RECT rcGlyph = { rcBox.left, rcBox.top - 1, rcBox.right, rcBox.bottom };
                DrawTextW(hdc, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, oldF);
            }
            else
            {
                HPEN hpTick = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HGDIOBJ oldTick = SelectObject(hdc, hpTick);
                int l = rcBox.left;
                int t = rcBox.top;
                POINT pts[3] = {
                    { l + 4,  t + 9  },
                    { l + 7,  t + 13 },
                    { l + 14, t + 5  }
                };
                Polyline(hdc, pts, 3);
                SelectObject(hdc, oldTick);
                DeleteObject(hpTick);
            }
        }
    }

    static void DrawCustomRadioButton(HDC hdc, const RECT& rcCircle, bool isSelected, bool isHovered)
    {
        int cx = rcCircle.left + (rcCircle.right - rcCircle.left) / 2;
        int cy = rcCircle.top + (rcCircle.bottom - rcCircle.top) / 2;
        int R = 6; // Outer ring radius (13x13 outer circle centered on (cx, cy))

        COLORREF bg = isHovered ? RGB(45, 45, 50) : RGB(28, 28, 32);
        COLORREF border = isSelected ? RGB(0, 140, 240) : (isHovered ? RGB(90, 90, 95) : RGB(65, 65, 70));

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ holdBr = SelectObject(hdc, hbr);
        HGDIOBJ holdPen = SelectObject(hdc, hpen);

        Ellipse(hdc, cx - R, cy - R, cx + R + 1, cy + R + 1);

        if (isSelected)
        {
            int r = 3; // Inner dot radius (7x7 inner circle, exactly 3px margin on all sides)
            COLORREF dotCol = RGB(0, 140, 240);
            HBRUSH hbrDot = CreateSolidBrush(dotCol);
            HPEN hpenDot = CreatePen(PS_SOLID, 1, dotCol);
            SelectObject(hdc, hbrDot);
            SelectObject(hdc, hpenDot);

            Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);

            DeleteObject(hbrDot);
            DeleteObject(hpenDot);
        }

        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);
    }

    static void DrawCustomDropdownPicker(HDC hdc, const RECT& rcPicker, const wchar_t* text, bool isHovered, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* icon = NULL)
    {
        COLORREF bg = isHovered ? RGB(45, 45, 50) : RGB(30, 30, 34);
        COLORREF border = isHovered ? RGB(90, 90, 95) : RGB(55, 55, 60);

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
        HPEN holdPen = (HPEN)SelectObject(hdc, hpen);
        RoundRect(hdc, rcPicker.left, rcPicker.top, rcPicker.right, rcPicker.bottom, 4, 4);
        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);

        SetBkMode(hdc, TRANSPARENT);
        int curX = rcPicker.left + 8;
        if (icon && hIconFont && wcslen(icon) > 0)
        {
            SelectObject(hdc, hIconFont);
            SetTextColor(hdc, RGB(96, 205, 255));
            RECT rcIcon = { curX, rcPicker.top, curX + 16, rcPicker.bottom };
            DrawTextW(hdc, icon, -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            curX += 20;
        }

        SelectObject(hdc, hFont);
        SetTextColor(hdc, RGB(240, 240, 240));
        RECT rcText = { curX, rcPicker.top, rcPicker.right - 22, rcPicker.bottom };
        DrawTextW(hdc, text ? text : L"", -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Down triangle
        int triCX = rcPicker.right - 12;
        int triCY = (rcPicker.top + rcPicker.bottom) / 2;
        COLORREF arrCol = isHovered ? RGB(240, 240, 240) : RGB(160, 160, 160);
        HBRUSH hBrArr = CreateSolidBrush(arrCol);
        HPEN hPenArr = CreatePen(PS_SOLID, 1, arrCol);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBrArr);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenArr);

        POINT pts[3] = { { triCX - 4, triCY - 2 }, { triCX + 4, triCY - 2 }, { triCX, triCY + 3 } };
        Polygon(hdc, pts, 3);
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrArr);
        DeleteObject(hPenArr);
    }
}

// ------------------------------------------------------------------------------------------------
// Modern Quick Input Prompt Modal (for New Preset & Rename)
// ------------------------------------------------------------------------------------------------
struct InputModalState
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

static LRESULT CALLBACK InputModalProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    InputModalState* pState = (InputModalState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (InputModalState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
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
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            34, 82, 392, 20, hWnd, (HMENU)101, GetModuleHandleW(NULL), NULL
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

        // 4. Custom Rounded Edit Box Container (36px high, edit is 20px high and centered!)
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
        return 0;
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
        return 0;
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

static bool PromptModalText(HWND hParent, const std::wstring& title, const std::wstring& prompt, const std::wstring& defaultVal, std::wstring& outVal)
{
    static bool s_reg = false;
    const wchar_t* szClassName = L"TSCB_SimpleInputModal";
    if (!s_reg)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = InputModalProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = szClassName;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    InputModalState state;
    state.hParent = hParent;
    state.title = title;
    state.prompt = prompt;
    state.resultText = defaultVal;

    int dlgW = 460;
    int dlgH = 195;
    RECT rcParent;
    if (hParent && IsWindow(hParent))
    {
        GetWindowRect(hParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }
    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, szClassName, title.c_str(),
        WS_POPUP | WS_CLIPCHILDREN, x, y, dlgW, dlgH,
        hParent, NULL, GetModuleHandleW(NULL), &state);

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    EnableWindow(hParent, FALSE);
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
                if (hParent && IsWindow(hParent))
                {
                    EnableWindow(hParent, TRUE);
                    SetForegroundWindow(hParent);
                    SetActiveWindow(hParent);
                }
                DestroyWindow(hDlg);
                break;
            }
            else if (msg.wParam == VK_ESCAPE)
            {
                if (hParent && IsWindow(hParent))
                {
                    EnableWindow(hParent, TRUE);
                    SetForegroundWindow(hParent);
                    SetActiveWindow(hParent);
                }
                DestroyWindow(hDlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    if (state.isConfirmed)
    {
        outVal = state.resultText;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Data Structures
// ------------------------------------------------------------------------------------------------
struct SmsPresetEntry
{
    std::wstring fileName;     // e.g. "lhb_coach.sms"
    std::wstring directory;    // e.g. "common.snd\LHB"
    std::wstring fullRelative; // e.g. "common.snd\LHB\lhb_coach.sms"
    std::wstring slot;         // e.g. "Wagon", "Engine", "ORTS3DCab", "Inside"
};

struct SmsPresetCard
{
    int id = 0;
    std::wstring name;
    std::wstring category = L"General";
    std::wstring defaultSlot = L"Wagon";
    bool isCollapsed = false;
    std::vector<SmsPresetEntry> entries;
};

struct BatchStockUnit
{
    bool isSelected = true;
    bool isExpanded = true;
    bool hasMissingStock = false;
    bool hasMissingShape = false;
    std::wstring uid;
    std::wstring folder;
    bool isEngine = false;
    std::wstring fullPath;
    RSSUnitSoundInfo soundInfo;
    std::unordered_set<std::wstring> referencingConsists;
};

enum class StockDisplayFilter
{
    All,
    EnginesOnly,
    WagonsOnly
};

struct BatchSoundMutatorState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    std::wstring basePath;
    int activeTab = 0; // 0 = SMS Presets Pool, 1 = Batch Sound Mutator
    SmsPoolPresetSelectCallback onApplyCallback = nullptr;

    HWND hTitleBar = NULL;

    // Fonts
    HFONT hFontBody = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontTitle = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontBadge = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    // ----------------- TAB 0: SMS Presets Collapsible Cards -----------------
    std::vector<SmsPresetCard> presetCards;
    CustomScrollBar m_vScroll;
    int scrollY = 0;

    RECT rcBtnNewPresetGroup = { 0 };
    RECT rcBtnExpandAll = { 0 };
    RECT rcBtnCollapseAll = { 0 };
    RECT rcBtnCloseTab0 = { 0 };
    RECT rcBtnApplyPresetTab0 = { 0 };

    struct CardHit
    {
        int cardIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
        RECT rcChevron = { 0 };
        RECT rcSlotPill = { 0 };
        RECT rcBtnRename = { 0 };
        RECT rcBtnClone = { 0 };
        RECT rcBtnMoveUp = { 0 };
        RECT rcBtnMoveDown = { 0 };
        RECT rcBtnDelete = { 0 };
        RECT rcBtnAddSms = { 0 };
    };
    std::vector<CardHit> cardHits;

    struct ChipHit
    {
        int cardIdx = -1;
        int entryIdx = -1;
        RECT rcChip = { 0 };
        RECT rcBtnRemove = { 0 };
    };
    std::vector<ChipHit> chipHits;

    int selectedPresetCardIdx = -1;
    int dragHoverCardIdx = -1;

    // ----------------- TAB 1: Batch Sound Mutator -----------------
    enum BatchDisplayRowType
    {
        BATCH_ROW_UNIT,
        BATCH_ROW_STREAM
    };

    struct BatchDisplayRow
    {
        BatchDisplayRowType rowType = BATCH_ROW_UNIT;
        int unitIndex = -1;
        std::wstring category; // "Wagon", "Engine", "ORTS3DCab", "Inside"
        int streamIndex = -1;
    };

    CustomListControl stockList;
    HWND hStockListWnd = NULL;

    std::vector<std::wstring> consistFilePaths;
    std::vector<BatchStockUnit> resolvedUnits;
    std::vector<BatchDisplayRow> displayRows;
    std::vector<int> visibleUnitIndices;
    StockDisplayFilter displayFilter = StockDisplayFilter::All;

    RECT rcBtnExpandAllTab1 = { 0 };
    RECT rcBtnCollapseAllTab1 = { 0 };

    // Mutation Settings
    bool isRemoveMode = true; // true = Remove / Strip, false = Replace / Assign
    bool removeWagon = true;
    bool removeEngine = true;
    bool removeCab = true;
    bool removeInside = true;
    bool removeAllInSlot = true; // true = wipe slot, false = match only
    std::vector<std::wstring> selectedSmsToRemove; // multi-selection of .sms files to strip

    std::wstring assignTargetSlot = L"Wagon";
    bool assignFromPreset = true;
    int selectedPresetForAssign = 0;
    std::wstring customSmsPath;
    bool appendStreamMode = false; // false = Replace / Overwrite, true = Append / Add stream

    // Child edit controls (clean text inputs with parent-drawn borders)
    HWND hEditCustomSms = NULL;

    // Interactive hit regions (Tab 1)
    RECT rcBtnClearConsists = { 0 };

    // Sub-bar buttons below Stock List (Active showcase filters + batch selection)
    RECT rcBtnFilterAll = { 0 };
    RECT rcBtnFilterEngines = { 0 };
    RECT rcBtnFilterWagons = { 0 };
    RECT rcBtnSelectAll = { 0 };
    RECT rcBtnSelectNone = { 0 };

    // Left Panel
    RECT rcLeftPanel = { 0 };
    RECT rcRadioModeRemove = { 0 };
    RECT rcChkSlotWagon = { 0 };
    RECT rcChkSlotEngine = { 0 };
    RECT rcChkSlotCab = { 0 };
    RECT rcChkSlotInside = { 0 };
    RECT rcRadioRemoveAll = { 0 };
    RECT rcRadioRemoveMatch = { 0 };
    RECT rcPickerMatchSms = { 0 };

    // Right Panel
    RECT rcRightPanel = { 0 };
    RECT rcRadioModeAssign = { 0 };
    RECT rcRadioAssignReplace = { 0 };
    RECT rcRadioAssignAppend = { 0 };
    RECT rcPickerAssignSlot = { 0 };
    RECT rcRadioAssignPreset = { 0 };
    RECT rcPickerPreset = { 0 };
    RECT rcRadioAssignCustom = { 0 };
    RECT rcEditCustomSmsBox = { 0 };
    RECT rcBtnBrowseCustom = { 0 };

    // Tab 1 Bottom Actions
    RECT rcBtnPreview = { 0 };
    RECT rcBtnApplyMutations = { 0 };
    RECT rcBtnCloseTab1 = { 0 };

    int hoverBtnId = -1;
};

static const wchar_t* BATCH_SOUND_WNDCLASS = L"TSCB_BatchSoundMutatorWindow";

// ------------------------------------------------------------------------------------------------
// SQLite Persistence for SMS Preset Cards
// ------------------------------------------------------------------------------------------------
static std::wstring SerializeEntries(const std::vector<SmsPresetEntry>& entries)
{
    std::wstring out;
    for (const auto& e : entries)
    {
        if (!out.empty()) out += L"\n";
        out += e.fileName + L"|" + e.directory + L"|" + e.slot + L"|" + e.fullRelative;
    }
    return out;
}

static std::vector<SmsPresetEntry> DeserializeEntries(const std::wstring& payload, const std::wstring& primarySms, const std::wstring& targetSlot)
{
    std::vector<SmsPresetEntry> entries;
    if (payload.find(L'|') != std::wstring::npos)
    {
        std::wstringstream ss(payload);
        std::wstring line;
        while (std::getline(ss, line))
        {
            if (line.empty()) continue;
            std::wstringstream lineSs(line);
            std::wstring fName, dir, slot, fullRel;
            std::getline(lineSs, fName, L'|');
            std::getline(lineSs, dir, L'|');
            std::getline(lineSs, slot, L'|');
            std::getline(lineSs, fullRel, L'|');
            if (fullRel.empty())
            {
                fullRel = dir.empty() ? fName : (dir + L"\\" + fName);
            }
            if (!fName.empty())
            {
                entries.push_back({ fName, dir, fullRel, slot.empty() ? targetSlot : slot });
            }
        }
    }
    else if (!primarySms.empty())
    {
        size_t ls = primarySms.find_last_of(L"\\/");
        std::wstring fName = (ls != std::wstring::npos) ? primarySms.substr(ls + 1) : primarySms;
        std::wstring dir = (ls != std::wstring::npos) ? primarySms.substr(0, ls) : L"";
        entries.push_back({ fName, dir, primarySms, targetSlot });
    }
    return entries;
}

static void LoadPresetCardsFromDb(BatchSoundMutatorState* pState)
{
    pState->presetCards.clear();
    std::vector<DatabaseManager::SmsPoolPresetItem> dbItems;
    DatabaseManager::LoadSmsPoolPresets(dbItems);

    for (const auto& item : dbItems)
    {
        SmsPresetCard card;
        card.id = item.id;
        card.name = item.name;
        card.category = item.category.empty() ? L"General" : item.category;
        card.defaultSlot = item.targetSlot.empty() ? L"Wagon" : item.targetSlot;
        card.isCollapsed = false;
        card.entries = DeserializeEntries(item.payload, item.primarySms, card.defaultSlot);
        pState->presetCards.push_back(std::move(card));
    }
}

static void SavePresetCardToDb(SmsPresetCard& card)
{
    DatabaseManager::SmsPoolPresetItem item;
    item.id = card.id;
    item.name = card.name;
    item.category = card.category;
    item.targetSlot = card.defaultSlot;
    item.payload = SerializeEntries(card.entries);
    if (!card.entries.empty())
    {
        item.primarySms = card.entries[0].fullRelative;
    }
    DatabaseManager::SaveSmsPoolPreset(item);
    card.id = item.id;
}

// ------------------------------------------------------------------------------------------------
// Tab 1: Sound Studio Card-Style Callbacks & List Population
// ------------------------------------------------------------------------------------------------
static void ResizeStockListColumns(BatchSoundMutatorState* pState, int listW)
{
    if (!pState || !pState->hStockListWnd) return;
    pState->stockList.SetAutoFitLastColumn(false);

    if (listW <= 0)
    {
        RECT rcClient = { 0 };
        GetClientRect(pState->hWnd, &rcClient);
        listW = rcClient.right;
    }
    if (listW <= 0) return;

    // Use current client width of list control if available
    RECT rcInner = { 0 };
    if (GetClientRect(pState->hStockListWnd, &rcInner) && rcInner.right > 50)
    {
        listW = rcInner.right;
    }

    // Determine usable width (accounting for vertical scrollbar if present)
    int usableW = pState->stockList.GetUsableWidth();
    if (usableW <= 0 || usableW > listW)
    {
        usableW = listW;
    }

    // Utility columns with clean, comfortable fixed sizes (Col 0, Col 2, Col 3, Col 8)
    int col0 = 55;  // # + chevron + checkbox
    int col2 = 70;  // Type badge (Engine / Wagon)
    int col3 = 130; // Folder
    int col8 = 115; // Status badge
    int col1 = (usableW * 18) / 100;
    if (col1 < 210) col1 = 210; // Name / Stream

    int fixedW = col0 + col1 + col2 + col3 + col8;
    int remAudio = usableW - fixedW;
    if (remAudio < 560) remAudio = 560;
    int audioBase = remAudio / 4;
    if (audioBase < 140) audioBase = 140;

    int col4 = audioBase; // Wagon Sound (Exterior)
    int col5 = audioBase; // Engine Cab Sound
    int col6 = audioBase; // ORTS3DCab Sound
    int col7 = remAudio - (audioBase * 3); // Inside Sound (absorbs remainder)
    if (col7 < 140) col7 = 140;

    pState->stockList.SetColumnWidth(0, col0);
    pState->stockList.SetColumnWidth(1, col1);
    pState->stockList.SetColumnWidth(2, col2);
    pState->stockList.SetColumnWidth(3, col3);
    pState->stockList.SetColumnWidth(4, col4);
    pState->stockList.SetColumnWidth(5, col5);
    pState->stockList.SetColumnWidth(6, col6);
    pState->stockList.SetColumnWidth(7, col7);
    pState->stockList.SetColumnWidth(8, col8);

    pState->stockList.Invalidate();
}

static bool Batch_RowBackgroundCallback(HDC hdc, int itemIndex, const RECT& rcRow, bool isSelected, bool isHovered, void* pParam)
{
    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)pParam;
    if (!pState || itemIndex < 0 || itemIndex >= (int)pState->displayRows.size()) return false;

    const auto& dRow = pState->displayRows[itemIndex];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->resolvedUnits.size()) return false;
    const auto& u = pState->resolvedUnits[dRow.unitIndex];

    bool isEngine = u.isEngine;
    COLORREF accentColor = u.hasMissingStock ? RGB(255, 80, 80) :
        (u.soundInfo.missingCount > 0 ? RGB(255, 140, 60) :
        (isEngine ? RGB(0, 145, 255) : RGB(245, 158, 11)));

    bool isFirstInCard = (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT);
    bool isLastInCard = (!u.isExpanded || (itemIndex + 1 >= (int)pState->displayRows.size()) || (pState->displayRows[itemIndex + 1].unitIndex != dRow.unitIndex));

    RECT rc = rcRow;

    // For multi-row expanded cards, close the 2px gap between rows inside the card
    // so the card body connects seamlessly from top to bottom
    if (!isFirstInCard)
    {
        rc.top = rcRow.top - 1; // Seamless connection to row above
    }
    else if (itemIndex > 0)
    {
        rc.top += 4; // 4px card gap at top of unit card (except unit 0)
    }

    if (!isLastInCard)
    {
        rc.bottom = rcRow.bottom + 1; // Seamless connection to row below
    }
    else
    {
        rc.bottom -= 1; // 1px bottom margin
    }

    COLORREF bgCol;
    COLORREF borderCol = RGB(46, 54, 68);

    if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
    {
        bgCol = u.hasMissingStock ? RGB(36, 24, 26) : (isEngine ? RGB(24, 30, 42) : RGB(32, 28, 22));
        if (isSelected) bgCol = RGB(36, 68, 110);
        else if (isHovered) bgCol = u.hasMissingStock ? RGB(44, 28, 30) : (isEngine ? RGB(30, 40, 56) : RGB(40, 34, 26));
    }
    else // BATCH_ROW_STREAM
    {
        bgCol = (dRow.streamIndex % 2 == 0) ? RGB(20, 24, 32) : RGB(23, 27, 36);
        if (isSelected) bgCol = RGB(28, 52, 85);
        else if (isHovered) bgCol = RGB(30, 36, 48);
    }

    HBRUSH hbrBg = CreateSolidBrush(bgCol);
    HPEN hpenBorder = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ oldBr = SelectObject(hdc, hbrBg);
    HGDIOBJ oldPn = SelectObject(hdc, hpenBorder);

    if (isFirstInCard && isLastInCard)
    {
        // Standalone collapsed unit card (rounded 6px on all sides)
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    }
    else if (isFirstInCard)
    {
        // Top row of expanded card: rounded top corners, flat bottom (strictly clipped to rc)
        int savedDC = SaveDC(hdc);
        IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom + 6, 6, 6);
        RestoreDC(hdc, savedDC);

        // Crisp horizontal divider separating unit header from child stream rows
        HPEN hInnerPen = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ oldP = SelectObject(hdc, hInnerPen);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, oldP);
        DeleteObject(hInnerPen);
    }
    else if (isLastInCard)
    {
        // Bottom row of expanded card: flat top, rounded bottom corners (strictly clipped to rc)
        int savedDC = SaveDC(hdc);
        IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        RoundRect(hdc, rc.left, rc.top - 6, rc.right, rc.bottom, 6, 6);
        RestoreDC(hdc, savedDC);
    }
    else
    {
        // Middle rows of card: fill + left & right borders + bottom divider
        FillRect(hdc, &rc, hbrBg);
        HPEN hSidePen = CreatePen(PS_SOLID, 1, borderCol);
        HGDIOBJ oldP = SelectObject(hdc, hSidePen);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.left, rc.bottom);
        MoveToEx(hdc, rc.right - 1, rc.top, NULL); LineTo(hdc, rc.right - 1, rc.bottom);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, oldP);
        DeleteObject(hSidePen);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(hbrBg);
    DeleteObject(hpenBorder);

    // Left Accent Color Stripe
    int stripeLeft = rc.left + 1;
    int stripeW = 4;
    RECT rcStripe = { stripeLeft, rc.top + (isFirstInCard ? 2 : 0), stripeLeft + stripeW, rc.bottom - (isLastInCard ? 2 : 0) };
    if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
    {
        HBRUSH hbrStripe = CreateSolidBrush(accentColor);
        FillRect(hdc, &rcStripe, hbrStripe);
        DeleteObject(hbrStripe);
    }
    else
    {
        COLORREF mutedAccent = isEngine ? RGB(32, 60, 95) : RGB(85, 60, 20);
        HBRUSH hbrStripe = CreateSolidBrush(mutedAccent);
        FillRect(hdc, &rcStripe, hbrStripe);
        DeleteObject(hbrStripe);
    }

    // Inter-card separator gap line
    if (isFirstInCard && itemIndex > 0)
    {
        HPEN hSepPen = CreatePen(PS_SOLID, 1, RGB(30, 35, 45));
        HGDIOBJ oldSep = SelectObject(hdc, hSepPen);
        MoveToEx(hdc, rc.left - 2, rc.top - 2, NULL);
        LineTo(hdc, rc.right + 2, rc.top - 2);
        SelectObject(hdc, oldSep);
        DeleteObject(hSepPen);
    }

    return true;
}

static bool Batch_DrawCellCallback(HDC hdc, int itemIndex, int subItemIndex, const RECT& rcCell, bool isSelected, bool isHovered, void* pParam)
{
    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)pParam;
    if (!pState || itemIndex < 0 || itemIndex >= (int)pState->displayRows.size()) return false;

    const auto& dRow = pState->displayRows[itemIndex];
    if (dRow.unitIndex < 0 || dRow.unitIndex >= (int)pState->resolvedUnits.size()) return false;
    const auto& u = pState->resolvedUnits[dRow.unitIndex];

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, pState->hFontBody ? pState->hFontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    bool isFirstInCard = (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT);
    bool isLastInCard = (!u.isExpanded || (itemIndex + 1 >= (int)pState->displayRows.size()) || (pState->displayRows[itemIndex + 1].unitIndex != dRow.unitIndex));

    RECT rc = rcCell;
    if (isFirstInCard && itemIndex > 0)
    {
        rc.top += 4;
    }
    if (isLastInCard)
    {
        rc.bottom -= 2;
    }

    // Column 0: Chevron / Checkbox / Number #
    if (subItemIndex == 0)
    {
        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
        {
            int totalStreams = (int)(u.soundInfo.wagonSounds.size() + u.soundInfo.engineSounds.size() +
                                     u.soundInfo.orts3dCabSounds.size() + u.soundInfo.insideSounds.size());

            // Chevron ▾ / ▸
            if (totalStreams > 0)
            {
                SelectObject(hdc, pState->hFontBody);
                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(140, 185, 240));
                RECT rcChevron = { rc.left + 5, rc.top, rc.left + 16, rc.bottom };
                DrawTextW(hdc, u.isExpanded ? L"▾" : L"▸", -1, &rcChevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            // Checkbox (14x14)
            int chkSz = 14;
            int chkY = rc.top + (rc.bottom - rc.top - chkSz) / 2;
            RECT rcChk = { rc.left + 18, chkY, rc.left + 18 + chkSz, chkY + chkSz };

            if (u.hasMissingStock)
            {
                HBRUSH hbrChk = CreateSolidBrush(RGB(56, 24, 28));
                HPEN hpChk = CreatePen(PS_SOLID, 1, RGB(180, 50, 50));
                HGDIOBJ oB = SelectObject(hdc, hbrChk); HGDIOBJ oP = SelectObject(hdc, hpChk);
                RoundRect(hdc, rcChk.left, rcChk.top, rcChk.right, rcChk.bottom, 3, 3);
                SelectObject(hdc, oB); SelectObject(hdc, oP);
                DeleteObject(hbrChk); DeleteObject(hpChk);

                // Crisp, mathematically centered diagonal cross (equal margins on all 4 sides)
                HPEN hpCross = CreatePen(PS_SOLID, 1, RGB(255, 120, 120));
                HGDIOBJ oldPen = SelectObject(hdc, hpCross);
                MoveToEx(hdc, rcChk.left + 3, rcChk.top + 3, NULL);
                LineTo(hdc, rcChk.right - 3, rcChk.bottom - 3);
                MoveToEx(hdc, rcChk.left + 3, rcChk.bottom - 4, NULL);
                LineTo(hdc, rcChk.right - 3, rcChk.top + 2);
                SelectObject(hdc, oldPen);
                DeleteObject(hpCross);
            }
            else
            {
                COLORREF chkBg = u.isSelected ? RGB(0, 120, 215) : RGB(36, 40, 48);
                COLORREF chkBdr = u.isSelected ? RGB(0, 120, 215) : RGB(70, 78, 92);
                HBRUSH hbrChk = CreateSolidBrush(chkBg);
                HPEN hpChk = CreatePen(PS_SOLID, 1, chkBdr);
                HGDIOBJ oB = SelectObject(hdc, hbrChk); HGDIOBJ oP = SelectObject(hdc, hpChk);
                RoundRect(hdc, rcChk.left, rcChk.top, rcChk.right, rcChk.bottom, 3, 3);
                SelectObject(hdc, oB); SelectObject(hdc, oP);
                DeleteObject(hbrChk); DeleteObject(hpChk);

                if (u.isSelected)
                {
                    HPEN hpTick = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                    HGDIOBJ oldTick = SelectObject(hdc, hpTick);
                    POINT pts[3] = {
                        { rcChk.left + 3, rcChk.top + 7 },
                        { rcChk.left + 6, rcChk.top + 10 },
                        { rcChk.left + 11, rcChk.top + 4 }
                    };
                    Polyline(hdc, pts, 3);
                    SelectObject(hdc, oldTick);
                    DeleteObject(hpTick);
                }
            }

            // Unit Number #
            SelectObject(hdc, pState->hFontBold);
            SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(220, 225, 235));
            RECT rcNum = { rc.left + 36, rc.top, rc.right - 2, rc.bottom };
            int visibleNum = 1;
            auto itVis = std::find(pState->visibleUnitIndices.begin(), pState->visibleUnitIndices.end(), dRow.unitIndex);
            if (itVis != pState->visibleUnitIndices.end())
            {
                visibleNum = (int)(std::distance(pState->visibleUnitIndices.begin(), itVis) + 1);
            }
            DrawTextW(hdc, std::to_wstring(visibleNum).c_str(), -1, &rcNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else // BATCH_ROW_STREAM
        {
            SelectObject(hdc, pState->hFontBody);
            SetTextColor(hdc, RGB(110, 130, 160));
            RECT rcBranch = { rc.left + 8, rc.top, rc.right - 2, rc.bottom };
            DrawTextW(hdc, L"↳", -1, &rcBranch, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    // Column 1: Name / Stream Slot
    if (subItemIndex == 1)
    {
        RECT rcName = rc;
        rcName.left += 6; rcName.right -= 6;
        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
        {
            SelectObject(hdc, pState->hFontBold);
            SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : (u.hasMissingStock ? RGB(255, 120, 120) : RGB(245, 245, 245)));
            std::wstring nameStr = u.uid;
            if (u.hasMissingStock) nameStr += L" [MISSING]";
            DrawTextW(hdc, nameStr.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        else // BATCH_ROW_STREAM
        {
            SelectObject(hdc, pState->hFontBody);
            SetTextColor(hdc, RGB(160, 185, 215));
            std::wstring slotLabel;
            if (dRow.category == L"Wagon") slotLabel = L"↳ Wagon Slot #" + std::to_wstring(dRow.streamIndex + 1);
            else if (dRow.category == L"Engine") slotLabel = L"↳ Cab Sound #" + std::to_wstring(dRow.streamIndex + 1);
            else if (dRow.category == L"ORTS3DCab") slotLabel = L"↳ 3D Cab #" + std::to_wstring(dRow.streamIndex + 1);
            else slotLabel = L"↳ Inside Sound #" + std::to_wstring(dRow.streamIndex + 1);
            DrawTextW(hdc, slotLabel.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    // Column 2: Type Pill Badge
    if (subItemIndex == 2)
    {
        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
        {
            int cellW = rc.right - rc.left;
            int innerTop = rc.top + 2;
            int innerBottom = rc.bottom - 2;
            int innerH = innerBottom - innerTop;
            int pillH = (innerH >= 24) ? 20 : 18;
            int pillY = innerTop + (innerH - pillH) / 2;
            int pillW = (std::min)(cellW - 8, 54);
            int pillX = rc.left + (cellW - pillW) / 2;
            RECT rcPill = { pillX, pillY, pillX + pillW, pillY + pillH };
            if (rcPill.right > rcPill.left)
            {
                bool isEng = u.isEngine;
                COLORREF pillBg = isEng ? RGB(18, 48, 88) : RGB(48, 36, 18);
                COLORREF pillBorder = isEng ? RGB(32, 90, 160) : RGB(120, 85, 35);
                COLORREF pillText = isEng ? RGB(100, 185, 255) : RGB(255, 175, 80);

                HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);
                HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);
                RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                SelectObject(hdc, oB); SelectObject(hdc, oP);
                DeleteObject(hbr); DeleteObject(hp);

                SelectObject(hdc, pState->hFontSmall);
                SetTextColor(hdc, pillText);
                std::wstring typeLabel = isEng ? L"Engine" : L"Wagon";
                DrawTextW(hdc, typeLabel.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        else // BATCH_ROW_STREAM
        {
            SelectObject(hdc, pState->hFontSmall);
            SetTextColor(hdc, RGB(120, 130, 145));
            RECT rcBadge = rc;
            rcBadge.left += 6; rcBadge.right -= 6;
            std::wstring streamType = (dRow.category == L"Wagon") ? L"Exterior" :
                                      (dRow.category == L"Engine") ? L"Cab" :
                                      (dRow.category == L"ORTS3DCab") ? L"3D Cab" : L"Inside";
            DrawTextW(hdc, streamType.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    // Column 3: Parent Directory / Folder
    if (subItemIndex == 3)
    {
        RECT rcFolder = rc;
        rcFolder.left += 6; rcFolder.right -= 6;
        SelectObject(hdc, pState->hFontSmall);
        SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(160, 175, 195));

        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
        {
            DrawTextW(hdc, u.folder.c_str(), -1, &rcFolder, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        else // BATCH_ROW_STREAM
        {
            const RSSSoundEntry* pEntry = nullptr;
            if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.soundInfo.wagonSounds.size()) pEntry = &u.soundInfo.wagonSounds[dRow.streamIndex];
            else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.soundInfo.engineSounds.size()) pEntry = &u.soundInfo.engineSounds[dRow.streamIndex];
            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.soundInfo.orts3dCabSounds.size()) pEntry = &u.soundInfo.orts3dCabSounds[dRow.streamIndex];
            else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.soundInfo.insideSounds.size()) pEntry = &u.soundInfo.insideSounds[dRow.streamIndex];

            std::wstring fld = pEntry ? pEntry->parentFolder : L"";
            DrawTextW(hdc, fld.c_str(), -1, &rcFolder, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    // Columns 4, 5, 6, 7: Sound stream slots
    if (subItemIndex >= 4 && subItemIndex <= 7)
    {
        std::wstring colCat = L"Wagon";
        if (subItemIndex == 5) colCat = L"Engine";
        else if (subItemIndex == 6) colCat = L"ORTS3DCab";
        else if (subItemIndex == 7) colCat = L"Inside";

        int cellW = rc.right - rc.left;
        int innerTop = rc.top + 2;
        int innerBottom = rc.bottom - 2;
        int innerH = innerBottom - innerTop;
        if (innerH < 18) innerH = 18;
        int chipH = (innerH >= 24) ? 20 : 18;
        int chipY = innerTop + (innerH - chipH) / 2;

        const RSSSoundEntry* pEntry = nullptr;
        std::wstring chipTextStr;

        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_STREAM)
        {
            if (dRow.category == colCat)
            {
                if (colCat == L"Wagon" && dRow.streamIndex < (int)u.soundInfo.wagonSounds.size()) pEntry = &u.soundInfo.wagonSounds[dRow.streamIndex];
                else if (colCat == L"Engine" && dRow.streamIndex < (int)u.soundInfo.engineSounds.size()) pEntry = &u.soundInfo.engineSounds[dRow.streamIndex];
                else if (colCat == L"ORTS3DCab" && dRow.streamIndex < (int)u.soundInfo.orts3dCabSounds.size()) pEntry = &u.soundInfo.orts3dCabSounds[dRow.streamIndex];
                else if (colCat == L"Inside" && dRow.streamIndex < (int)u.soundInfo.insideSounds.size()) pEntry = &u.soundInfo.insideSounds[dRow.streamIndex];

                if (pEntry)
                {
                    chipTextStr = pEntry->smsFileName;
                }
            }
        }
        else // BATCH_ROW_UNIT
        {
            const std::vector<RSSSoundEntry>* pVec = nullptr;
            if (colCat == L"Wagon") pVec = &u.soundInfo.wagonSounds;
            else if (colCat == L"Engine") pVec = &u.soundInfo.engineSounds;
            else if (colCat == L"ORTS3DCab") pVec = &u.soundInfo.orts3dCabSounds;
            else if (colCat == L"Inside") pVec = &u.soundInfo.insideSounds;

            if (pVec && !pVec->empty())
            {
                pEntry = &(*pVec)[0];
                chipTextStr = pEntry->smsFileName;
                if (pVec->size() > 1 && !u.isExpanded)
                {
                    chipTextStr += L" (+" + std::to_wstring(pVec->size() - 1) + L")";
                }
            }
        }

        if (pEntry && !chipTextStr.empty())
        {
            SelectObject(hdc, pState->hFontSmall);
            RECT rcMeasure = { 0, 0, 0, 0 };
            DrawTextW(hdc, chipTextStr.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            int textW = rcMeasure.right - rcMeasure.left;
            int chipW = 24 + textW + 14;
            if (chipW > cellW - 8) chipW = cellW - 8;
            if (chipW < 60) chipW = 60;
            int chipX = rc.left + (cellW - chipW) / 2;
            RECT rcChip = { chipX, chipY, chipX + chipW, chipY + chipH };

            COLORREF chipBg = pEntry->existsOnDisk ? RGB(28, 36, 48) : RGB(46, 28, 28);
            COLORREF chipBorder = pEntry->existsOnDisk ? RGB(50, 75, 110) : RGB(105, 50, 50);
            COLORREF chipText = pEntry->existsOnDisk ? RGB(220, 230, 245) : RGB(255, 150, 140);

            HBRUSH hbr = CreateSolidBrush(chipBg); HPEN hp = CreatePen(PS_SOLID, 1, chipBorder);
            HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);
            RoundRect(hdc, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
            SelectObject(hdc, oB); SelectObject(hdc, oP);
            DeleteObject(hbr); DeleteObject(hp);

            SelectObject(hdc, pState->hFontSmall);
            SetTextColor(hdc, pEntry->existsOnDisk ? RGB(0, 160, 240) : RGB(255, 120, 100));
            RECT rcIcon = { rcChip.left + 5, rcChip.top, rcChip.left + 19, rcChip.bottom };
            DrawTextW(hdc, L"♪", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(hdc, chipText);
            RECT rcTxt = { rcChip.left + 21, rcChip.top, rcChip.right - 5, rcChip.bottom };
            DrawTextW(hdc, chipTextStr.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        else
        {
            RECT rcDash = rc;
            SelectObject(hdc, pState->hFontSmall);
            SetTextColor(hdc, RGB(75, 85, 100));
            DrawTextW(hdc, L"—", -1, &rcDash, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    // Column 8: Status Pill Badge
    if (subItemIndex == 8)
    {
        std::wstring statusLabel;
        COLORREF pillBg = RGB(20, 48, 30);
        COLORREF pillBorder = RGB(40, 110, 65);
        COLORREF pillText = RGB(90, 190, 110);

        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
        {
            if (u.hasMissingStock)
            {
                pillBg = RGB(56, 22, 26); pillBorder = RGB(140, 45, 52); pillText = RGB(255, 80, 80);
                statusLabel = L"✕   Missing Stock";
            }
            else if (u.soundInfo.missingCount > 0)
            {
                pillBg = RGB(56, 36, 20); pillBorder = RGB(140, 90, 40); pillText = RGB(255, 160, 60);
                statusLabel = L"⚠   Missing Sound";
            }
            else
            {
                pillBg = RGB(20, 48, 30); pillBorder = RGB(40, 110, 65); pillText = RGB(90, 190, 110);
                statusLabel = L"✓   Ready";
            }
        }
        else // BATCH_ROW_STREAM
        {
            const RSSSoundEntry* pEntry = nullptr;
            if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.soundInfo.wagonSounds.size()) pEntry = &u.soundInfo.wagonSounds[dRow.streamIndex];
            else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.soundInfo.engineSounds.size()) pEntry = &u.soundInfo.engineSounds[dRow.streamIndex];
            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.soundInfo.orts3dCabSounds.size()) pEntry = &u.soundInfo.orts3dCabSounds[dRow.streamIndex];
            else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.soundInfo.insideSounds.size()) pEntry = &u.soundInfo.insideSounds[dRow.streamIndex];

            bool ready = pEntry ? pEntry->existsOnDisk : false;
            pillBg = ready ? RGB(16, 52, 28) : RGB(64, 30, 20);
            pillBorder = ready ? RGB(32, 110, 56) : RGB(130, 60, 40);
            pillText = ready ? RGB(90, 220, 125) : RGB(255, 120, 95);
            statusLabel = ready ? L"✓   OK" : L"✕   Missing";
        }

        SelectObject(hdc, pState->hFontSmall);
        RECT rcMeasure = { 0, 0, 0, 0 };
        DrawTextW(hdc, statusLabel.c_str(), -1, &rcMeasure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        int textW = rcMeasure.right - rcMeasure.left;
        int pillW = textW + 24;
        int cellW = rc.right - rc.left;
        if (pillW > cellW - 8)
            pillW = cellW - 8;
        if (pillW < 40) pillW = 40;

        int innerTop = rc.top + 2;
        int innerBottom = rc.bottom - 2;
        int innerH = innerBottom - innerTop;
        int pillH = (innerH >= 24) ? 20 : 18;
        int pillX = rc.left + (cellW - pillW) / 2;
        int pillY = innerTop + (innerH - pillH) / 2;
        RECT rcPill = { pillX, pillY, pillX + pillW, pillY + pillH };

        if (rcPill.right > rcPill.left)
        {
            HBRUSH hbr = CreateSolidBrush(pillBg);
            HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);
            HGDIOBJ oB = SelectObject(hdc, hbr);
            HGDIOBJ oP = SelectObject(hdc, hp);
            RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);
            SelectObject(hdc, oB);
            SelectObject(hdc, oP);
            DeleteObject(hbr);
            DeleteObject(hp);

            SetTextColor(hdc, pillText);
            DrawTextW(hdc, statusLabel.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdc, hOldFont);
        SetBkMode(hdc, oldBkMode);
        return true;
    }

    SelectObject(hdc, hOldFont);
    SetBkMode(hdc, oldBkMode);
    return false;
}

static void PopulateStockListControl(BatchSoundMutatorState* pState)
{
    pState->stockList.Clear();
    pState->displayRows.clear();
    pState->visibleUnitIndices.clear();

    for (size_t i = 0; i < pState->resolvedUnits.size(); ++i)
    {
        auto& u = pState->resolvedUnits[i];

        // Active showcase filter
        if (pState->displayFilter == StockDisplayFilter::EnginesOnly && !u.isEngine) continue;
        if (pState->displayFilter == StockDisplayFilter::WagonsOnly && u.isEngine) continue;

        pState->visibleUnitIndices.push_back((int)i);

        // 1. Parent Unit Row
        BatchSoundMutatorState::BatchDisplayRow parentRow;
        parentRow.rowType = BatchSoundMutatorState::BATCH_ROW_UNIT;
        parentRow.unitIndex = (int)i;
        parentRow.streamIndex = -1;
        pState->displayRows.push_back(parentRow);

        int totalStreams = (int)(u.soundInfo.wagonSounds.size() + u.soundInfo.engineSounds.size() +
                                 u.soundInfo.orts3dCabSounds.size() + u.soundInfo.insideSounds.size());

        std::wstring expandIcon = (totalStreams > 0) ? (u.isExpanded ? L"▾ " : L"▸ ") : L"  ";
        std::wstring col0 = expandIcon + std::to_wstring(pState->visibleUnitIndices.size());

        std::wstring wagSummary = u.soundInfo.wagonSounds.empty() ? L"—" : u.soundInfo.wagonSounds[0].smsFileName;
        if (u.soundInfo.wagonSounds.size() > 1 && !u.isExpanded)
        {
            wagSummary += L" (+" + std::to_wstring(u.soundInfo.wagonSounds.size() - 1) + L")";
        }
        std::wstring engSummary = u.soundInfo.engineSounds.empty() ? L"—" : u.soundInfo.engineSounds[0].smsFileName;
        if (u.soundInfo.engineSounds.size() > 1 && !u.isExpanded)
        {
            engSummary += L" (+" + std::to_wstring(u.soundInfo.engineSounds.size() - 1) + L")";
        }
        std::wstring ortsSummary = u.soundInfo.orts3dCabSounds.empty() ? L"—" : u.soundInfo.orts3dCabSounds[0].smsFileName;
        std::wstring insideSummary = u.soundInfo.insideSounds.empty() ? L"—" : u.soundInfo.insideSounds[0].smsFileName;

        std::wstring statusStr = u.hasMissingStock ? L"Missing Stock" : (u.soundInfo.missingCount > 0 ? L"Missing Sound" : L"Ready");

        pState->stockList.AddItem({
            col0,
            u.uid,
            u.isEngine ? L"Engine" : L"Wagon",
            u.folder,
            wagSummary,
            engSummary,
            ortsSummary,
            insideSummary,
            statusStr
        });

        // 2. Child Stream Rows if expanded
        if (u.isExpanded && !u.hasMissingStock)
        {
            // Wagon Exterior Sounds
            for (size_t sIdx = 0; sIdx < u.soundInfo.wagonSounds.size(); ++sIdx)
            {
                const auto& entry = u.soundInfo.wagonSounds[sIdx];
                BatchSoundMutatorState::BatchDisplayRow streamRow;
                streamRow.rowType = BatchSoundMutatorState::BATCH_ROW_STREAM;
                streamRow.unitIndex = (int)i;
                streamRow.category = L"Wagon";
                streamRow.streamIndex = (int)sIdx;
                pState->displayRows.push_back(streamRow);

                std::wstring slotName = L"   ↳ Wagon Slot " + std::to_wstring(sIdx + 1);
                std::wstring st = entry.existsOnDisk ? L"OK" : L"Missing";

                pState->stockList.AddItem({
                    L"   ↳",
                    slotName,
                    L"Exterior",
                    entry.parentFolder,
                    entry.smsFileName,
                    L"—",
                    L"—",
                    L"—",
                    st
                });
            }

            // Engine Cab Sounds
            for (size_t sIdx = 0; sIdx < u.soundInfo.engineSounds.size(); ++sIdx)
            {
                const auto& entry = u.soundInfo.engineSounds[sIdx];
                BatchSoundMutatorState::BatchDisplayRow streamRow;
                streamRow.rowType = BatchSoundMutatorState::BATCH_ROW_STREAM;
                streamRow.unitIndex = (int)i;
                streamRow.category = L"Engine";
                streamRow.streamIndex = (int)sIdx;
                pState->displayRows.push_back(streamRow);

                std::wstring slotName = L"   ↳ Cab Slot " + std::to_wstring(sIdx + 1);
                std::wstring st = entry.existsOnDisk ? L"OK" : L"Missing";

                pState->stockList.AddItem({
                    L"   ↳",
                    slotName,
                    L"Engine Cab",
                    entry.parentFolder,
                    L"—",
                    entry.smsFileName,
                    L"—",
                    L"—",
                    st
                });
            }

            // ORTS3DCab Sounds
            for (size_t sIdx = 0; sIdx < u.soundInfo.orts3dCabSounds.size(); ++sIdx)
            {
                const auto& entry = u.soundInfo.orts3dCabSounds[sIdx];
                BatchSoundMutatorState::BatchDisplayRow streamRow;
                streamRow.rowType = BatchSoundMutatorState::BATCH_ROW_STREAM;
                streamRow.unitIndex = (int)i;
                streamRow.category = L"ORTS3DCab";
                streamRow.streamIndex = (int)sIdx;
                pState->displayRows.push_back(streamRow);

                std::wstring slotName = L"   ↳ ORTS3D Slot " + std::to_wstring(sIdx + 1);
                std::wstring st = entry.existsOnDisk ? L"OK" : L"Missing";

                pState->stockList.AddItem({
                    L"   ↳",
                    slotName,
                    L"ORTS3DCab",
                    entry.parentFolder,
                    L"—",
                    L"—",
                    entry.smsFileName,
                    L"—",
                    st
                });
            }

            // Inside Sounds
            for (size_t sIdx = 0; sIdx < u.soundInfo.insideSounds.size(); ++sIdx)
            {
                const auto& entry = u.soundInfo.insideSounds[sIdx];
                BatchSoundMutatorState::BatchDisplayRow streamRow;
                streamRow.rowType = BatchSoundMutatorState::BATCH_ROW_STREAM;
                streamRow.unitIndex = (int)i;
                streamRow.category = L"Inside";
                streamRow.streamIndex = (int)sIdx;
                pState->displayRows.push_back(streamRow);

                std::wstring slotName = L"   ↳ Inside Slot " + std::to_wstring(sIdx + 1);
                std::wstring st = entry.existsOnDisk ? L"OK" : L"Missing";

                pState->stockList.AddItem({
                    L"   ↳",
                    slotName,
                    L"Inside",
                    entry.parentFolder,
                    L"—",
                    L"—",
                    L"—",
                    entry.smsFileName,
                    st
                });
            }
        }
    }

    RECT rcClient = { 0 };
    GetClientRect(pState->hWnd, &rcClient);
    ResizeStockListColumns(pState, rcClient.right);
}

static std::vector<std::wstring> GetDetectedSmsList(BatchSoundMutatorState* pState)
{
    std::vector<std::wstring> result;
    std::unordered_set<std::wstring> seen;
    if (!pState) return result;

    for (const auto& u : pState->resolvedUnits)
    {
        if (pState->removeWagon)
        {
            for (const auto& s : u.soundInfo.wagonSounds)
            {
                if (!s.smsFileName.empty() && seen.insert(s.smsFileName).second)
                {
                    result.push_back(s.smsFileName);
                }
            }
        }
        if (pState->removeEngine)
        {
            for (const auto& s : u.soundInfo.engineSounds)
            {
                if (!s.smsFileName.empty() && seen.insert(s.smsFileName).second)
                {
                    result.push_back(s.smsFileName);
                }
            }
        }
        if (pState->removeCab)
        {
            for (const auto& s : u.soundInfo.orts3dCabSounds)
            {
                if (!s.smsFileName.empty() && seen.insert(s.smsFileName).second)
                {
                    result.push_back(s.smsFileName);
                }
            }
        }
        if (pState->removeInside)
        {
            for (const auto& s : u.soundInfo.insideSounds)
            {
                if (!s.smsFileName.empty() && seen.insert(s.smsFileName).second)
                {
                    result.push_back(s.smsFileName);
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

static void ScanAndResolveConsists(BatchSoundMutatorState* pState)
{
    pState->resolvedUnits.clear();
    std::map<std::wstring, BatchStockUnit> stockMap;

    std::wstring bp = pState->basePath;
    if (!bp.empty() && bp.back() != L'\\') bp += L'\\';

    for (const auto& conPath : pState->consistFilePaths)
    {
        try
        {
            ConsistReader::ConsistData cData = ConsistReader::LoadConsist(conPath);
            std::wstring conFileName = conPath;
            size_t lastSlash = conPath.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) conFileName = conPath.substr(lastSlash + 1);

            for (const auto& u : cData.units)
            {
                std::wstring stockFolder = bp + L"TRAINS\\TRAINSET\\" + u.parentDir;
                std::wstring ext = u.isEngine ? L".eng" : L".wag";
                std::wstring fullStockPath = stockFolder + L"\\" + u.uid + ext;
                bool stockFound = false;

                if (GetFileAttributesW(fullStockPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    stockFound = true;
                }
                else
                {
                    std::wstring altExt = u.isEngine ? L".wag" : L".eng";
                    std::wstring altPath = stockFolder + L"\\" + u.uid + altExt;
                    if (GetFileAttributesW(altPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        fullStockPath = altPath;
                        ext = altExt;
                        stockFound = true;
                    }
                }

                auto toLower = [](std::wstring s) {
                    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
                    return s;
                };

                std::wstring lookupKey = toLower(fullStockPath);
                if (stockMap.find(lookupKey) == stockMap.end())
                {
                    BatchStockUnit bu;
                    bu.uid = u.uid;
                    bu.folder = u.parentDir;
                    bu.isEngine = (ext == L".eng");
                    bu.fullPath = fullStockPath;
                    bu.soundInfo.name = u.uid;
                    bu.soundInfo.folder = u.parentDir;
                    bu.soundInfo.type = bu.isEngine ? L"Engine" : L"Wagon";
                    bu.soundInfo.ext = ext;
                    bu.soundInfo.fullPath = fullStockPath;

                    if (!stockFound)
                    {
                        bu.hasMissingStock = true;
                        bu.isSelected = false; // Block selection for missing stock
                    }
                    else
                    {
                        bu.hasMissingStock = false;
                        bu.isSelected = true;

                        SoundStudio_ParseUnitSound(fullStockPath, stockFolder, bp, bu.soundInfo);

                        // Check if 3D shape is missing (allowed to mutate sound, but flag noted)
                        StockSpecReader::StockSpec spec = StockSpecReader::ReadFullSpec(fullStockPath, bp);
                        if (!spec.shapeExistsOnDisk && !spec.mainShapeFile.empty())
                        {
                            bu.hasMissingShape = true;
                        }
                    }

                    bu.referencingConsists.insert(conFileName);
                    stockMap[lookupKey] = bu;
                }
                else
                {
                    stockMap[lookupKey].referencingConsists.insert(conFileName);
                }
            }
        }
        catch (...)
        {
        }
    }

    for (auto& pair : stockMap)
    {
        pState->resolvedUnits.push_back(std::move(pair.second));
    }

    std::sort(pState->resolvedUnits.begin(), pState->resolvedUnits.end(), [](const BatchStockUnit& a, const BatchStockUnit& b) {
        if (a.isEngine != b.isEngine) return a.isEngine > b.isEngine;
        return a.uid < b.uid;
    });

    PopulateStockListControl(pState);
}

static void OpenSmsInDefaultEditor(const std::wstring& smsPath, const std::wstring& basePath)
{
    if (smsPath.empty()) return;

    std::wstring fullPath = smsPath;
    if (GetFileAttributesW(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::wstring candidate1 = basePath + L"TRAINS\\TRAINSET\\" + smsPath;
        if (GetFileAttributesW(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) fullPath = candidate1;
        else
        {
            std::wstring candidate2 = basePath + L"TRAINS\\TRAINSET\\common.snd\\" + smsPath;
            if (GetFileAttributesW(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) fullPath = candidate2;
        }
    }

    HINSTANCE hInst = ShellExecuteW(NULL, L"open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst <= 32)
    {
        ShellExecuteW(NULL, L"open", L"notepad.exe", (L"\"" + fullPath + L"\"").c_str(), NULL, SW_SHOWNORMAL);
    }
}

static void SwitchActiveTab(BatchSoundMutatorState* pState, int newTab)
{
    pState->activeTab = newTab;

    int showTab1 = (newTab == 1) ? SW_SHOW : SW_HIDE;
    if (pState->hStockListWnd) ShowWindow(pState->hStockListWnd, showTab1);
    if (pState->hEditCustomSms) ShowWindow(pState->hEditCustomSms, showTab1);

    if (newTab == 0)
    {
        LoadPresetCardsFromDb(pState);
    }
    else if (newTab == 1)
    {
        RECT rcClient = { 0 };
        GetClientRect(pState->hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;
        int footerH = 46;
        int bottomY = h - footerH;
        int panelH = 140;
        int panelBottom = bottomY - 8;
        int panelTop = panelBottom - panelH;
        int subBarH = 26;
        int subBarY = panelTop - subBarH - 8;
        int stockListY = 118;
        int stockListH = subBarY - 8 - stockListY;
        if (stockListH < 80) stockListH = 80;

        if (pState->hStockListWnd)
        {
            SetWindowPos(pState->hStockListWnd, NULL, 0, stockListY, w, stockListH, SWP_NOZORDER | SWP_SHOWWINDOW);
            ResizeStockListColumns(pState, w);
        }
    }

    InvalidateRect(pState->hWnd, NULL, TRUE);
}

static void AddConsistFilesViaDialog(BatchSoundMutatorState* pState)
{
    const int bufferSize = 65536;
    std::vector<wchar_t> buffer(bufferSize, 0);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = pState->hWnd;
    ofn.lpstrFilter = L"MSTS/ORTS Consist Files (*.con)\0*.con\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = bufferSize;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    std::wstring consistDir = pState->basePath + L"TRAINS\\CONSISTS";
    ofn.lpstrInitialDir = consistDir.c_str();

    if (GetOpenFileNameW(&ofn))
    {
        wchar_t* ptr = buffer.data();
        std::wstring dir = ptr;
        ptr += dir.length() + 1;

        if (*ptr == L'\0')
        {
            if (std::find(pState->consistFilePaths.begin(), pState->consistFilePaths.end(), dir) == pState->consistFilePaths.end())
            {
                pState->consistFilePaths.push_back(dir);
            }
        }
        else
        {
            if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
            while (*ptr != L'\0')
            {
                std::wstring fn = ptr;
                std::wstring fullPath = dir + fn;
                if (std::find(pState->consistFilePaths.begin(), pState->consistFilePaths.end(), fullPath) == pState->consistFilePaths.end())
                {
                    pState->consistFilePaths.push_back(fullPath);
                }
                ptr += fn.length() + 1;
            }
        }

        ScanAndResolveConsists(pState);
    }
}

static void AddAllConsistsInDirectory(BatchSoundMutatorState* pState)
{
    std::wstring consistDir = pState->basePath + L"TRAINS\\CONSISTS\\";
    std::wstring searchPattern = consistDir + L"*.con";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wstring fullPath = consistDir + ffd.cFileName;
                if (std::find(pState->consistFilePaths.begin(), pState->consistFilePaths.end(), fullPath) == pState->consistFilePaths.end())
                {
                    pState->consistFilePaths.push_back(fullPath);
                }
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);

        ScanAndResolveConsists(pState);
    }
}

static void ExecuteBatchSoundMutation(BatchSoundMutatorState* pState, bool dryRunOnly)
{
    wchar_t customBuf[512] = { 0 };
    if (pState->hEditCustomSms) GetWindowTextW(pState->hEditCustomSms, customBuf, 512);
    pState->customSmsPath = customBuf;

    auto toLower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    };

    std::unordered_set<std::wstring> targetsLower;
    for (const auto& s : pState->selectedSmsToRemove)
    {
        if (!s.empty()) targetsLower.insert(toLower(s));
    }

    bool isMutationQueued = false;
    if (pState->isRemoveMode)
    {
        isMutationQueued = pState->removeAllInSlot || !targetsLower.empty();
    }
    else
    {
        std::wstring checkSms;
        if (pState->assignFromPreset && pState->selectedPresetForAssign >= 0 && pState->selectedPresetForAssign < (int)pState->presetCards.size())
        {
            const auto& card = pState->presetCards[pState->selectedPresetForAssign];
            if (!card.entries.empty()) checkSms = card.entries[0].fullRelative;
        }
        else
        {
            checkSms = pState->customSmsPath;
        }
        isMutationQueued = !checkSms.empty();
    }

    int modifiedCount = 0;
    int successCount = 0;
    std::wstring dryRunReport;

    IncludeSoundAction batchIncAction = IncludeSoundAction::PromptUser;
    bool batchApplyToAll = false;

    // Check Open Rails multi-SMS compatibility notice if appending an additional stream to an existing slot
    if (pState->appendStreamMode && !pState->isRemoveMode && !s_bBatchSuppressMultiSmsWarning)
    {
        bool anyHasExisting = false;
        for (const auto& u : pState->resolvedUnits)
        {
            if (!u.isSelected || u.hasMissingStock) continue;
            if (pState->displayFilter == StockDisplayFilter::EnginesOnly && !u.isEngine) continue;
            if (pState->displayFilter == StockDisplayFilter::WagonsOnly && u.isEngine) continue;

            if ((pState->assignTargetSlot == L"Wagon" || (pState->assignTargetSlot == L"All Compatible" && !u.isEngine)) && !u.soundInfo.wagonSounds.empty())
                { anyHasExisting = true; break; }
            if ((pState->assignTargetSlot == L"Engine" || (pState->assignTargetSlot == L"All Compatible" && u.isEngine)) && !u.soundInfo.engineSounds.empty())
                { anyHasExisting = true; break; }
            if (pState->assignTargetSlot == L"ORTS3DCab" && !u.soundInfo.orts3dCabSounds.empty())
                { anyHasExisting = true; break; }
            if (pState->assignTargetSlot == L"Inside" && !u.soundInfo.insideSounds.empty())
                { anyHasExisting = true; break; }
        }

        if (anyHasExisting)
        {
            bool bDontShowAgain = false;
            ShowModernMessageBoxWithCheckbox(
                pState->hWnd,
                L"Multiple sound files in a single sound block (separated by spaces) are only supported in Open Rails Testing / Unstable versions (and upcoming Open Rails 1.7+).\r\n\r\nStable releases of Open Rails will only play the first sound stream and ignore subsequent streams.",
                L"Open Rails Compatibility Notice",
                L"Do not show this warning again",
                &bDontShowAgain,
                MB_OK | MB_ICONINFORMATION
            );
            if (bDontShowAgain)
            {
                s_bBatchSuppressMultiSmsWarning = true;
            }
        }
    }

    for (auto& u : pState->resolvedUnits)
    {
        if (!u.isSelected || u.hasMissingStock) continue;
        if (pState->displayFilter == StockDisplayFilter::EnginesOnly && !u.isEngine) continue;
        if (pState->displayFilter == StockDisplayFilter::WagonsOnly && u.isEngine) continue;

        RSSUnitSoundInfo mutatedInfo = u.soundInfo;
        bool willChange = false;

        if (isMutationQueued)
        {
            if (pState->isRemoveMode)
            {
                auto stripStream = [&](std::vector<RSSSoundEntry>& streams) {
                    if (streams.empty()) return;
                    if (pState->removeAllInSlot)
                    {
                        streams.clear();
                        willChange = true;
                    }
                    else if (!targetsLower.empty())
                    {
                        size_t origSize = streams.size();
                        streams.erase(std::remove_if(streams.begin(), streams.end(), [&](const RSSSoundEntry& se) {
                            std::wstring fnLower = toLower(se.smsFileName);
                            if (targetsLower.count(fnLower) > 0) return true;
                            std::wstring tokLower = toLower(se.rawToken);
                            for (const auto& t : targetsLower)
                            {
                                if (tokLower.find(t) != std::wstring::npos) return true;
                            }
                            return false;
                        }), streams.end());
                        if (streams.size() != origSize) willChange = true;
                    }
                };

                if (pState->removeWagon) stripStream(mutatedInfo.wagonSounds);
                if (pState->removeEngine) stripStream(mutatedInfo.engineSounds);
                if (pState->removeCab) stripStream(mutatedInfo.orts3dCabSounds);
                if (pState->removeInside) stripStream(mutatedInfo.insideSounds);
            }
            else // REPLACE / ASSIGN MODE
            {
                RSSSoundEntry newEntry;
                std::wstring priSms;

                if (pState->assignFromPreset && pState->selectedPresetForAssign >= 0 && pState->selectedPresetForAssign < (int)pState->presetCards.size())
                {
                    const auto& card = pState->presetCards[pState->selectedPresetForAssign];
                    if (!card.entries.empty())
                    {
                        priSms = card.entries[0].fullRelative;
                    }
                }
                else
                {
                    priSms = pState->customSmsPath;
                }

                if (!priSms.empty())
                {
                    newEntry.rawToken = priSms;
                    size_t ls = priSms.find_last_of(L"\\/");
                    newEntry.smsFileName = (ls != std::wstring::npos) ? priSms.substr(ls + 1) : priSms;
                    newEntry.parentFolder = (ls != std::wstring::npos) ? priSms.substr(0, ls) : L"";

                    if (pState->assignTargetSlot == L"Wagon" || (pState->assignTargetSlot == L"All Compatible" && !u.isEngine))
                    {
                        if (!pState->appendStreamMode) mutatedInfo.wagonSounds.clear();
                        mutatedInfo.wagonSounds.push_back(newEntry);
                        willChange = true;
                    }
                    else if (pState->assignTargetSlot == L"Engine" || (pState->assignTargetSlot == L"All Compatible" && u.isEngine))
                    {
                        if (!pState->appendStreamMode) mutatedInfo.engineSounds.clear();
                        mutatedInfo.engineSounds.push_back(newEntry);
                        willChange = true;
                    }
                    else if (pState->assignTargetSlot == L"ORTS3DCab")
                    {
                        if (!pState->appendStreamMode) mutatedInfo.orts3dCabSounds.clear();
                        mutatedInfo.orts3dCabSounds.push_back(newEntry);
                        willChange = true;
                    }
                    else if (pState->assignTargetSlot == L"Inside")
                    {
                        if (!pState->appendStreamMode) mutatedInfo.insideSounds.clear();
                        mutatedInfo.insideSounds.push_back(newEntry);
                        willChange = true;
                    }
                }
            }
        }
        else
        {
            // Flexible pass-through / normalize mode:
            // No explicit mutation queued, so re-save the exact existing sound profile
            // displayed in the batch preview, standardizing all paths to clean '/'
            willChange = true;
        }

        if (willChange)
        {
            modifiedCount++;
            if (dryRunOnly)
            {
                if (dryRunReport.length() < 3000)
                {
                    dryRunReport += L"• " + u.uid + (u.isEngine ? L" (.eng)" : L" (.wag)") +
                                    L" [" + u.folder + L"]" +
                                    (!isMutationQueued ? L" (Re-save & Normalize Slashes)\r\n" :
                                     (pState->appendStreamMode && !pState->isRemoveMode ? L" (Append Multi-SMS Stream)\r\n" : L"\r\n"));
                }
            }
            else
            {
                std::wstring bakPath = u.fullPath + L".TSCB_BACKUP";
                CopyFileW(u.fullPath.c_str(), bakPath.c_str(), FALSE);

                if (SoundStudio_UpdateUnitSoundInStockFile(mutatedInfo, pState->basePath, pState->hWnd, batchIncAction, &batchIncAction, &batchApplyToAll))
                {
                    u.soundInfo = mutatedInfo;
                    successCount++;
                }
                else if (batchIncAction == IncludeSoundAction::Cancel)
                {
                    break;
                }
            }
        }
    }

    if (batchIncAction == IncludeSoundAction::Cancel)
    {
        ShowModernMessageBox(pState->hWnd, L"Batch sound mutation cancelled by user.", L"Batch Sound Mutator", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (modifiedCount == 0)
    {
        ShowModernMessageBox(pState->hWnd, L"Please select at least one valid rolling stock unit to process.\r\n(Note: Units with Missing Stock cannot be processed.)", L"Batch Sound Mutator", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (dryRunOnly)
    {
        std::wstring title = L"Batch Sound Mutation Preview";
        std::wstring modeStr;
        if (!isMutationQueued)
            modeStr = L"Re-save & Normalize Sound Paths (Clean '/')";
        else
            modeStr = pState->isRemoveMode ? L"Strip / Remove Sounds" : (pState->appendStreamMode ? L"Append Additional Sound Stream (Open Rails Multi-SMS)" : L"Assign / Replace Sounds");

        std::wstring filterStr = L"All Units";
        if (pState->displayFilter == StockDisplayFilter::EnginesOnly) filterStr = L"Engines Only";
        else if (pState->displayFilter == StockDisplayFilter::WagonsOnly) filterStr = L"Wagons Only";

        std::wstring msg = L"Action Mode: " + modeStr + L"\r\n"
                           L"Target Filter: " + filterStr + L"\r\n"
                           L"Units that will be updated: " + std::to_wstring(modifiedCount) + L"\r\n\r\n"
                           L"Sample files to be modified:\r\n" + dryRunReport;
        if (modifiedCount > 20) msg += L"\r\n...and " + std::to_wstring(modifiedCount - 20) + L" more units.";
        ShowModernMessageBox(pState->hWnd, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        // Re-read mutated files to sync sound info
        std::wstring bp = pState->basePath;
        if (!bp.empty() && bp.back() != L'\\') bp += L'\\';
        for (auto& u : pState->resolvedUnits)
        {
            if (u.isSelected && !u.hasMissingStock)
            {
                if (pState->displayFilter == StockDisplayFilter::EnginesOnly && !u.isEngine) continue;
                if (pState->displayFilter == StockDisplayFilter::WagonsOnly && u.isEngine) continue;
                std::wstring stockFolder = bp + L"TRAINS\\TRAINSET\\" + u.folder;
                SoundStudio_ParseUnitSound(u.fullPath, stockFolder, bp, u.soundInfo);
            }
        }

        PopulateStockListControl(pState);
        std::wstring title = !isMutationQueued ? L"Sound Path Normalization Complete" : L"Batch Sound Mutation Complete";
        std::wstring actionDone = !isMutationQueued ? L"re-saved and normalized sound paths for " :
                                  (pState->appendStreamMode && !pState->isRemoveMode ? L"appended additional sound streams for " : L"modified sound configurations for ");
        std::wstring filterUnitStr = L"";
        if (pState->displayFilter == StockDisplayFilter::EnginesOnly) filterUnitStr = L" engine";
        else if (pState->displayFilter == StockDisplayFilter::WagonsOnly) filterUnitStr = L" wagon";

        std::wstring msg = L"Successfully " + actionDone + std::to_wstring(successCount) +
                           L" of " + std::to_wstring(modifiedCount) + L" selected" + filterUnitStr + L" rolling stock file(s).\r\n"
                           L"Backups (.TSCB_BACKUP) were safely created for each file.";
        ShowModernMessageBox(pState->hWnd, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

// ------------------------------------------------------------------------------------------------
// Window Procedure
// ------------------------------------------------------------------------------------------------
static LRESULT CALLBACK BatchSoundMutatorWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (BatchSoundMutatorState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_CREATE:
    {
        DragAcceptFiles(hWnd, TRUE);

        pState->hFontBody = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontBold = CreateCustomFont(9, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontTitle = CreateCustomFont(12, FW_BOLD, L"Segoe UI");
        pState->hFontSmall = CreateCustomFont(8, FW_NORMAL, L"Segoe UI");
        pState->hFontBadge = CreateCustomFont(8, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon = CreateCustomFont(10, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon) pState->hFontIcon = CreateCustomFont(10, FW_NORMAL, L"Segoe MDL2 Assets");
        pState->hFontIconSmall = CreateCustomFont(8, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIconSmall) pState->hFontIconSmall = CreateCustomFont(8, FW_NORMAL, L"Segoe MDL2 Assets");

        // Custom Titlebar with 2 Tabs (Height = 66 for tab ribbon)
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right > 0 ? rcClient.right : 1060;

        std::vector<TitleBarTabItem> soundTabs = {
            { L"\xE8D6", L"SMS Presets Pool" },
            { L"\xE916", L"Batch Sound Mutator" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, w, 66,
            1001,
            L"Sound Studio - Presets & Batch Mutator",
            soundTabs
        );
        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
            SendMessage(pState->hTitleBar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, 66));
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

        // Tab 0 Initialization
        LoadPresetCardsFromDb(pState);

        // Tab 1 Child List & Edit Controls
        pState->hStockListWnd = pState->stockList.Create(hWnd, 0, 112, w, 250, 3002);
        pState->stockList.SetShowSelectionGutter(false);
        pState->stockList.SetMultiSelect(false);
        pState->stockList.SetRowHeight(30);
        pState->stockList.SetShowVerticalDividers(true);
        pState->stockList.SetAutoFitLastColumn(false);
        pState->stockList.SetRowBackgroundCallback(Batch_RowBackgroundCallback, pState);
        pState->stockList.SetDrawCellCallback(Batch_DrawCellCallback, pState);

        pState->stockList.AddColumn(L"#", 60);
        pState->stockList.AddColumn(L"Name / Stream", 180);
        pState->stockList.AddColumn(L"Type", 90);
        pState->stockList.AddColumn(L"Folder", 160);
        pState->stockList.AddColumn(L"Wagon Sound (Exterior)", 170);
        pState->stockList.AddColumn(L"Engine Cab Sound", 160);
        pState->stockList.AddColumn(L"ORTS3DCab Sound", 130);
        pState->stockList.AddColumn(L"Inside Sound", 110);
        pState->stockList.AddColumn(L"Status", 110);
        ResizeStockListColumns(pState, w);

        pState->hEditCustomSms = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL, 10, 10, 10, 10, hWnd, (HMENU)4015, GetModuleHandleW(NULL), NULL);
        SendMessageW(pState->hEditCustomSms, WM_SETFONT, (WPARAM)pState->hFontBody, TRUE);
        SendMessageW(pState->hEditCustomSms, EM_SETCUEBANNER, TRUE, (LPARAM)L"Browse or enter .sms relative path...");

        SwitchActiveTab(pState, pState->activeTab);

        if (!pState->consistFilePaths.empty())
        {
            ScanAndResolveConsists(pState);
        }

        return 0;
    }

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

    case WM_TITLEBAR_TABCHANGED:
    {
        int newTab = (int)wParam;
        SwitchActiveTab(pState, newTab);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(28, 28, 30));
        SetTextColor(hdc, RGB(245, 245, 245));
        static HBRUSH hbrEdit = CreateSolidBrush(RGB(28, 28, 30));
        return (LRESULT)hbrEdit;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 240, 240));
        static HBRUSH hbrBg = CreateSolidBrush(PoolTheme::CardBackground);
        return (LRESULT)hbrBg;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        if (id == 4015 && code == EN_CHANGE)
        {
            if (pState && pState->hEditCustomSms)
            {
                wchar_t buf[MAX_PATH] = { 0 };
                GetWindowTextW(pState->hEditCustomSms, buf, MAX_PATH);
                pState->customSmsPath = buf;
                if (!pState->customSmsPath.empty())
                {
                    pState->assignFromPreset = false;
                    pState->isRemoveMode = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
        break;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (pState->hTitleBar) SetWindowPos(pState->hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER);

        // Sub-toolbar ribbon is Y = 66 to 112 (Height = 46px)
        int toolbarY = 66;
        int toolbarH = 46;
        int btnY = toolbarY + (toolbarH - 30) / 2; // = 74
        int btnH = 30;

        // Tab 0 Top Buttons (on burgundy toolbar ribbon)
        int curRight0 = w - 16;
        int newPresetW = (w < 920) ? 120 : 150;
        pState->rcBtnNewPresetGroup = { curRight0 - newPresetW, btnY, curRight0, btnY + btnH };
        curRight0 -= (newPresetW + 8);
        int expW = (w < 920) ? 76 : 92;
        pState->rcBtnExpandAll = { curRight0 - expW, btnY, curRight0, btnY + btnH };
        curRight0 -= (expW + 6);
        int colW = (w < 920) ? 80 : 96;
        pState->rcBtnCollapseAll = { curRight0 - colW, btnY, curRight0, btnY + btnH };

        // Bottom Bar Common Geometry (Y = h - footerH to h)
        int footerH = 46;
        int bottomY = h - footerH;

        // Tab 0 Bottom Buttons
        pState->rcBtnCloseTab0 = { w - 110, bottomY + 8, w - 20, bottomY + 38 };
        pState->rcBtnApplyPresetTab0 = { w - 310, bottomY + 8, w - 120, bottomY + 38 };

        // Tab 1 Top Buttons (on burgundy toolbar ribbon)
        int curRight1 = w - 16;
        int clearW = (w < 920) ? 68 : 80;
        pState->rcBtnClearConsists = { curRight1 - clearW, btnY, curRight1, btnY + btnH };
        curRight1 -= (clearW + 10);
        int colW1 = (w < 920) ? 80 : 96;
        pState->rcBtnCollapseAllTab1 = { curRight1 - colW1, btnY, curRight1, btnY + btnH };
        curRight1 -= (colW1 + 6);
        int expW1 = (w < 920) ? 80 : 92;
        pState->rcBtnExpandAllTab1 = { curRight1 - expW1, btnY, curRight1, btnY + btnH };

        // Tab 1 Bottom Panels & Stock List Geometry
        int panelH = 140;
        int panelBottom = bottomY - 8;
        int panelTop = panelBottom - panelH;
        int subBarH = 26;
        int subBarY = panelTop - subBarH - 8;
        int stockListY = 118;
        int stockListH = subBarY - 8 - stockListY;
        if (stockListH < 80) stockListH = 80;

        if (pState->hStockListWnd)
        {
            SetWindowPos(pState->hStockListWnd, NULL, 0, stockListY, w, stockListH, SWP_NOZORDER);
            ResizeStockListColumns(pState, w);
        }

        int curX = 24;
        int qH = 26;
        int gap = 8;

        // Showcase Filter group
        int fAllW = 88;
        pState->rcBtnFilterAll = { curX, subBarY, curX + fAllW, subBarY + qH };
        curX += fAllW + gap;

        int fEngW = 110;
        pState->rcBtnFilterEngines = { curX, subBarY, curX + fEngW, subBarY + qH };
        curX += fEngW + gap;

        int fWagW = 110;
        pState->rcBtnFilterWagons = { curX, subBarY, curX + fWagW, subBarY + qH };
        curX += fWagW + gap + 16; // extra spacing before batch selection group

        // Batch Selection group
        int selW = 95;
        pState->rcBtnSelectAll = { curX, subBarY, curX + selW, subBarY + qH };
        curX += selW + gap;

        int clrW = 95;
        pState->rcBtnSelectNone = { curX, subBarY, curX + clrW, subBarY + qH };

        // Operation Cards (Bottom)
        int panelMid = w / 2;
        pState->rcLeftPanel = { 24, panelTop, panelMid - 8, panelBottom };
        pState->rcRightPanel = { panelMid + 8, panelTop, w - 24, panelBottom };

        // Left Panel interactive rects (row height 24-26px, uniform vertical alignment)
        int row1Y = panelTop + 8;
        pState->rcRadioModeRemove = { pState->rcLeftPanel.left + 14, row1Y, pState->rcLeftPanel.right - 14, row1Y + 24 };

        int chkY = row1Y + 30;
        int chkW = (pState->rcLeftPanel.right - pState->rcLeftPanel.left - 28) / 4;
        pState->rcChkSlotWagon  = { pState->rcLeftPanel.left + 14, chkY, pState->rcLeftPanel.left + 14 + chkW, chkY + 24 };
        pState->rcChkSlotEngine = { pState->rcLeftPanel.left + 14 + chkW, chkY, pState->rcLeftPanel.left + 14 + 2 * chkW, chkY + 24 };
        pState->rcChkSlotCab    = { pState->rcLeftPanel.left + 14 + 2 * chkW, chkY, pState->rcLeftPanel.left + 14 + 3 * chkW, chkY + 24 };
        pState->rcChkSlotInside = { pState->rcLeftPanel.left + 14 + 3 * chkW, chkY, pState->rcLeftPanel.right - 14, chkY + 24 };

        int wipeY = chkY + 28;
        pState->rcRadioRemoveAll = { pState->rcLeftPanel.left + 14, wipeY, pState->rcLeftPanel.right - 14, wipeY + 24 };

        int matchY = wipeY + 28;
        pState->rcRadioRemoveMatch = { pState->rcLeftPanel.left + 14, matchY, pState->rcLeftPanel.left + 225, matchY + 26 };
        pState->rcPickerMatchSms = { pState->rcLeftPanel.left + 230, matchY + 1, pState->rcLeftPanel.right - 14, matchY + 25 };

        // Right Panel interactive rects
        pState->rcPickerAssignSlot = { pState->rcRightPanel.right - 145, row1Y, pState->rcRightPanel.right - 14, row1Y + 24 };
        pState->rcRadioModeAssign = { pState->rcRightPanel.left + 14, row1Y, pState->rcPickerAssignSlot.left - 8, row1Y + 24 };

        int actY = row1Y + 28;
        pState->rcRadioAssignReplace = { pState->rcRightPanel.left + 36, actY, pState->rcRightPanel.left + 230, actY + 24 };
        pState->rcRadioAssignAppend  = { pState->rcRightPanel.left + 240, actY, pState->rcRightPanel.right - 14, actY + 24 };

        int prY = actY + 28;
        pState->rcRadioAssignPreset = { pState->rcRightPanel.left + 14, prY, pState->rcRightPanel.left + 185, prY + 26 };
        pState->rcPickerPreset = { pState->rcRightPanel.left + 190, prY + 1, pState->rcRightPanel.right - 14, prY + 25 };

        int custY = prY + 30;
        int browseW = 75;
        pState->rcRadioAssignCustom = { pState->rcRightPanel.left + 14, custY, pState->rcRightPanel.left + 155, custY + 26 };
        pState->rcBtnBrowseCustom = { pState->rcRightPanel.right - 14 - browseW, custY + 1, pState->rcRightPanel.right - 14, custY + 25 };
        pState->rcEditCustomSmsBox = { pState->rcRightPanel.left + 160, custY + 1, pState->rcBtnBrowseCustom.left - 6, custY + 25 };
        if (pState->hEditCustomSms)
        {
            SetWindowPos(pState->hEditCustomSms, NULL, pState->rcEditCustomSmsBox.left + 4, pState->rcEditCustomSmsBox.top + 3,
                pState->rcEditCustomSmsBox.right - pState->rcEditCustomSmsBox.left - 8, 18, SWP_NOZORDER);
        }

        // Tab 1 Bottom Actions
        pState->rcBtnCloseTab1 = { w - 110, bottomY + 8, w - 20, bottomY + 38 };
        pState->rcBtnApplyMutations = { w - 340, bottomY + 8, w - 120, bottomY + 38 };
        pState->rcBtnPreview = { w - 505, bottomY + 8, w - 350, bottomY + 38 };

        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState->activeTab == 0)
        {
            short zDelta = (short)HIWORD(wParam);
            pState->m_vScroll.OnMouseWheel(zDelta, 24, hWnd);
            pState->scrollY = pState->m_vScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int newHover = -1;

        if (pState->activeTab == 0)
        {
            if (pState->m_vScroll.OnMouseMove(pt, hWnd))
            {
                pState->scrollY = pState->m_vScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }

            if (PtInRect(&pState->rcBtnNewPresetGroup, pt)) newHover = 1;
            else if (PtInRect(&pState->rcBtnExpandAll, pt)) newHover = 2;
            else if (PtInRect(&pState->rcBtnCollapseAll, pt)) newHover = 3;
            else if (PtInRect(&pState->rcBtnCloseTab0, pt)) newHover = 4;
            else if (pState->onApplyCallback && PtInRect(&pState->rcBtnApplyPresetTab0, pt)) newHover = 5;
        }
        else
        {
            if (PtInRect(&pState->rcBtnClearConsists, pt)) newHover = 13;
            else if (PtInRect(&pState->rcBtnExpandAllTab1, pt)) newHover = 25;
            else if (PtInRect(&pState->rcBtnCollapseAllTab1, pt)) newHover = 26;
            else if (PtInRect(&pState->rcBtnFilterAll, pt)) newHover = 14;
            else if (PtInRect(&pState->rcBtnFilterEngines, pt)) newHover = 15;
            else if (PtInRect(&pState->rcBtnFilterWagons, pt)) newHover = 16;
            else if (PtInRect(&pState->rcBtnSelectAll, pt)) newHover = 17;
            else if (PtInRect(&pState->rcBtnSelectNone, pt)) newHover = 18;
            else if (PtInRect(&pState->rcPickerMatchSms, pt)) newHover = 27;
            else if (PtInRect(&pState->rcBtnBrowseCustom, pt)) newHover = 19;
            else if (PtInRect(&pState->rcBtnPreview, pt)) newHover = 20;
            else if (PtInRect(&pState->rcBtnApplyMutations, pt)) newHover = 21;
            else if (PtInRect(&pState->rcBtnCloseTab1, pt)) newHover = 22;
        }

        if (newHover != pState->hoverBtnId)
        {
            pState->hoverBtnId = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pState->activeTab == 0)
        {
            if (pState->m_vScroll.OnLButtonUp(pt, hWnd))
            {
                pState->scrollY = pState->m_vScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pState->activeTab == 0)
        {
            for (const auto& ch : pState->chipHits)
            {
                if (PtInRect(&ch.rcChip, pt))
                {
                    if (ch.cardIdx >= 0 && ch.cardIdx < (int)pState->presetCards.size())
                    {
                        const auto& card = pState->presetCards[ch.cardIdx];
                        if (ch.entryIdx >= 0 && ch.entryIdx < (int)card.entries.size())
                        {
                            OpenSmsInDefaultEditor(card.entries[ch.entryIdx].fullRelative, pState->basePath);
                            return 0;
                        }
                    }
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // ----------------- TAB 0 INTERACTIONS -----------------
        if (pState->activeTab == 0)
        {
            if (pState->m_vScroll.OnLButtonDown(pt, hWnd))
            {
                pState->scrollY = pState->m_vScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            if (PtInRect(&pState->rcBtnNewPresetGroup, pt))
            {
                std::wstring newName;
                if (PromptModalText(hWnd, L"Create Sound Preset Group", L"Enter a name for the new preset group (e.g. 'LHB Sounds'):", L"New Sound Preset", newName) && !newName.empty())
                {
                    SmsPresetCard card;
                    card.name = newName;
                    card.category = L"General";
                    card.defaultSlot = L"Wagon";
                    card.isCollapsed = false;
                    SavePresetCardToDb(card);
                    pState->presetCards.push_back(card);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            else if (PtInRect(&pState->rcBtnExpandAll, pt))
            {
                for (auto& c : pState->presetCards) c.isCollapsed = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnCollapseAll, pt))
            {
                for (auto& c : pState->presetCards) c.isCollapsed = true;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnCloseTab0, pt))
            {
                DestroyWindow(hWnd);
                return 0;
            }
            else if (pState->onApplyCallback && PtInRect(&pState->rcBtnApplyPresetTab0, pt))
            {
                if (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size())
                {
                    const auto& card = pState->presetCards[pState->selectedPresetCardIdx];
                    DatabaseManager::SmsPoolPresetItem item;
                    item.id = card.id;
                    item.name = card.name;
                    item.category = card.category;
                    item.targetSlot = card.defaultSlot;
                    if (!card.entries.empty()) item.primarySms = card.entries[0].fullRelative;
                    item.payload = SerializeEntries(card.entries);
                    pState->onApplyCallback(item);
                    DestroyWindow(hWnd);
                }
                return 0;
            }

            // Check chip clicks (Remove or Double Click)
            for (const auto& ch : pState->chipHits)
            {
                if (PtInRect(&ch.rcBtnRemove, pt))
                {
                    if (ch.cardIdx >= 0 && ch.cardIdx < (int)pState->presetCards.size())
                    {
                        auto& card = pState->presetCards[ch.cardIdx];
                        if (ch.entryIdx >= 0 && ch.entryIdx < (int)card.entries.size())
                        {
                            card.entries.erase(card.entries.begin() + ch.entryIdx);
                            SavePresetCardToDb(card);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }
                else if (PtInRect(&ch.rcChip, pt))
                {
                    pState->selectedPresetCardIdx = ch.cardIdx;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }

            // Check card action buttons
            for (const auto& ch : pState->cardHits)
            {
                int cIdx = ch.cardIdx;
                if (cIdx < 0 || cIdx >= (int)pState->presetCards.size()) continue;
                auto& card = pState->presetCards[cIdx];

                if (PtInRect(&ch.rcChevron, pt))
                {
                    card.isCollapsed = !card.isCollapsed;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&ch.rcSlotPill, pt))
                {
                    std::vector<DropDownItem> slotItems = {
                        DropDownItem::Action(1, L"\xE8D6", L"Wagon", L"Track & coach rumble sound", card.defaultSlot == L"Wagon"),
                        DropDownItem::Action(2, L"\xE7C0", L"Engine", L"Locomotive propulsion & horn", card.defaultSlot == L"Engine"),
                        DropDownItem::Action(3, L"\xE7F4", L"ORTS3DCab", L"Open Rails interactive cab interior", card.defaultSlot == L"ORTS3DCab"),
                        DropDownItem::Action(4, L"\xE707", L"Inside", L"Passenger salon ambience", card.defaultSlot == L"Inside")
                    };
                    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, ch.rcSlotPill, slotItems, -1, 180);
                    if (chosen == 1) card.defaultSlot = L"Wagon";
                    else if (chosen == 2) card.defaultSlot = L"Engine";
                    else if (chosen == 3) card.defaultSlot = L"ORTS3DCab";
                    else if (chosen == 4) card.defaultSlot = L"Inside";
                    if (chosen > 0)
                    {
                        SavePresetCardToDb(card);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnRename, pt))
                {
                    std::wstring newName;
                    if (PromptModalText(hWnd, L"Rename Preset Group", L"Enter new name:", card.name, newName) && !newName.empty())
                    {
                        card.name = newName;
                        SavePresetCardToDb(card);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnClone, pt))
                {
                    SmsPresetCard cloned = card;
                    cloned.id = 0;
                    cloned.name += L" (Copy)";
                    SavePresetCardToDb(cloned);
                    pState->presetCards.insert(pState->presetCards.begin() + cIdx + 1, cloned);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnMoveUp, pt) && cIdx > 0)
                {
                    std::swap(pState->presetCards[cIdx], pState->presetCards[cIdx - 1]);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnMoveDown, pt) && cIdx + 1 < (int)pState->presetCards.size())
                {
                    std::swap(pState->presetCards[cIdx], pState->presetCards[cIdx + 1]);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnDelete, pt))
                {
                    if (ShowModernMessageBox(hWnd, (L"Are you sure you want to delete preset '" + card.name + L"'?").c_str(), L"Delete Preset Group", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        DatabaseManager::DeleteSmsPoolPreset(card.id);
                        pState->presetCards.erase(pState->presetCards.begin() + cIdx);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&ch.rcBtnAddSms, pt))
                {
                    wchar_t szFile[MAX_PATH] = { 0 };
                    OPENFILENAMEW ofn = { 0 };
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                    std::wstring initialDir = pState->basePath + L"TRAINS\\TRAINSET";
                    ofn.lpstrInitialDir = initialDir.c_str();

                    if (GetOpenFileNameW(&ofn))
                    {
                        std::wstring fullPath = szFile;
                        std::wstring relativePath = fullPath;
                        std::wstring trainsetDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                        if (_wcsnicmp(fullPath.c_str(), trainsetDir.c_str(), trainsetDir.length()) == 0)
                        {
                            relativePath = fullPath.substr(trainsetDir.length());
                        }

                        size_t ls = relativePath.find_last_of(L"\\/");
                        std::wstring fName = (ls != std::wstring::npos) ? relativePath.substr(ls + 1) : relativePath;
                        std::wstring dir = (ls != std::wstring::npos) ? relativePath.substr(0, ls) : L"";

                        card.entries.push_back({ fName, dir, relativePath, card.defaultSlot });
                        SavePresetCardToDb(card);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&ch.rcHeader, pt))
                {
                    pState->selectedPresetCardIdx = cIdx;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        // ----------------- TAB 1 INTERACTIONS -----------------
        else
        {
            if (PtInRect(&pState->rcBtnClearConsists, pt))
            {
                pState->consistFilePaths.clear();
                pState->resolvedUnits.clear();
                pState->displayRows.clear();
                pState->visibleUnitIndices.clear();
                pState->stockList.Clear();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnExpandAllTab1, pt))
            {
                for (auto& u : pState->resolvedUnits) u.isExpanded = true;
                PopulateStockListControl(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnCollapseAllTab1, pt))
            {
                for (auto& u : pState->resolvedUnits) u.isExpanded = false;
                PopulateStockListControl(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnFilterAll, pt))
            {
                pState->displayFilter = StockDisplayFilter::All;
                PopulateStockListControl(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnFilterEngines, pt))
            {
                pState->displayFilter = StockDisplayFilter::EnginesOnly;
                PopulateStockListControl(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnFilterWagons, pt))
            {
                pState->displayFilter = StockDisplayFilter::WagonsOnly;
                PopulateStockListControl(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnSelectAll, pt))
            {
                for (int idx : pState->visibleUnitIndices)
                {
                    if (idx >= 0 && idx < (int)pState->resolvedUnits.size())
                    {
                        if (!pState->resolvedUnits[idx].hasMissingStock)
                        {
                            pState->resolvedUnits[idx].isSelected = true;
                        }
                    }
                }
                PopulateStockListControl(pState);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnSelectNone, pt))
            {
                for (int idx : pState->visibleUnitIndices)
                {
                    if (idx >= 0 && idx < (int)pState->resolvedUnits.size())
                    {
                        pState->resolvedUnits[idx].isSelected = false;
                    }
                }
                PopulateStockListControl(pState);
                return 0;
            }
            // Left Panel Controls
            else if (PtInRect(&pState->rcRadioModeRemove, pt))
            {
                pState->isRemoveMode = true;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcChkSlotWagon, pt))
            {
                pState->removeWagon = !pState->removeWagon;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcChkSlotEngine, pt))
            {
                pState->removeEngine = !pState->removeEngine;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcChkSlotCab, pt))
            {
                pState->removeCab = !pState->removeCab;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcChkSlotInside, pt))
            {
                pState->removeInside = !pState->removeInside;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcRadioRemoveAll, pt))
            {
                pState->removeAllInSlot = true;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcRadioRemoveMatch, pt))
            {
                pState->removeAllInSlot = false;
                pState->isRemoveMode = true;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcPickerMatchSms, pt))
            {
                std::vector<std::wstring> detected = GetDetectedSmsList(pState);
                if (detected.empty())
                {
                    std::vector<DropDownItem> emptyItems;
                    emptyItems.push_back(DropDownItem::Header(L"No .sms found in selected slot(s)"));
                    CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerMatchSms, emptyItems, -1, 260);
                    return 0;
                }

                std::vector<DropDownItem> smsItems;
                bool allChecked = true;
                bool anyChecked = false;
                for (size_t i = 0; i < detected.size(); ++i)
                {
                    bool isCur = (std::find(pState->selectedSmsToRemove.begin(), pState->selectedSmsToRemove.end(), detected[i]) != pState->selectedSmsToRemove.end());
                    if (isCur) anyChecked = true;
                    else allChecked = false;
                    smsItems.push_back(DropDownItem::Action((int)i + 2, L"\xE8D6", detected[i], L"", isCur));
                }

                bool isIndeterminate = anyChecked && !allChecked;
                smsItems.insert(smsItems.begin(), DropDownItem::Header(1, L"\xE73E", L"Select All Detected SMS", L"", allChecked, isIndeterminate));

                CustomDropDownMenu::ShowMultiSelect(hWnd, pState->rcPickerMatchSms, smsItems, [pState, hWnd, detected](const std::vector<DropDownItem>& updatedItems) {
                    pState->selectedSmsToRemove.clear();
                    for (size_t k = 1; k < updatedItems.size(); ++k)
                    {
                        if (updatedItems[k].isChecked && !updatedItems[k].isHeader && !updatedItems[k].isGroupHeader)
                        {
                            size_t dIdx = (size_t)(updatedItems[k].id - 2);
                            if (dIdx < detected.size())
                            {
                                pState->selectedSmsToRemove.push_back(detected[dIdx]);
                            }
                        }
                    }
                    if (!pState->selectedSmsToRemove.empty())
                    {
                        pState->removeAllInSlot = false;
                        pState->isRemoveMode = true;
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    UpdateWindow(hWnd);
                }, 280);
                return 0;
            }
            // Right Panel Controls
            else if (PtInRect(&pState->rcRadioModeAssign, pt))
            {
                pState->isRemoveMode = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcRadioAssignReplace, pt))
            {
                pState->isRemoveMode = false;
                pState->appendStreamMode = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcRadioAssignAppend, pt))
            {
                pState->isRemoveMode = false;
                pState->appendStreamMode = true;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcPickerAssignSlot, pt))
            {
                POINT ptScreen = { pState->rcPickerAssignSlot.left, pState->rcPickerAssignSlot.bottom };
                ClientToScreen(hWnd, &ptScreen);
                std::vector<DropDownItem> slotItems = {
                    DropDownItem::Action(1, L"\xE8D6", L"Wagon", L"Passenger / Freight rolling stock sound", pState->assignTargetSlot == L"Wagon"),
                    DropDownItem::Action(2, L"\xE7C0", L"Engine", L"Locomotive main engine sound", pState->assignTargetSlot == L"Engine"),
                    DropDownItem::Action(3, L"\xE7F4", L"ORTS3DCab", L"3D interactive interior cab sound", pState->assignTargetSlot == L"ORTS3DCab"),
                    DropDownItem::Action(4, L"\xE707", L"Inside", L"Passenger compartment ambience", pState->assignTargetSlot == L"Inside"),
                    DropDownItem::Action(5, L"\xE8AB", L"All Compatible", L"Auto-routes Wagon vs Engine sound", pState->assignTargetSlot == L"All Compatible")
                };
                int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerAssignSlot, slotItems, -1, 240);
                if (chosen == 1) pState->assignTargetSlot = L"Wagon";
                else if (chosen == 2) pState->assignTargetSlot = L"Engine";
                else if (chosen == 3) pState->assignTargetSlot = L"ORTS3DCab";
                else if (chosen == 4) pState->assignTargetSlot = L"Inside";
                else if (chosen == 5) pState->assignTargetSlot = L"All Compatible";
                if (chosen > 0) InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcRadioAssignPreset, pt))
            {
                pState->assignFromPreset = true;
                pState->isRemoveMode = false;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcPickerPreset, pt))
            {
                pState->assignFromPreset = true;
                pState->isRemoveMode = false;

                std::vector<DropDownItem> presetItems;
                if (pState->presetCards.empty())
                {
                    presetItems.push_back(DropDownItem::Header(L"No presets created yet."));
                }
                else
                {
                    for (size_t i = 0; i < pState->presetCards.size(); ++i)
                    {
                        const auto& card = pState->presetCards[i];
                        std::wstring sec = card.entries.empty() ? L"(0 files)" : (card.entries[0].fileName + (card.entries.size() > 1 ? L" +" + std::to_wstring(card.entries.size() - 1) : L""));
                        presetItems.push_back(DropDownItem::Action((int)i + 1, L"\xE8D6", card.name, sec, (int)i == pState->selectedPresetForAssign));
                    }
                }

                int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerPreset, presetItems, -1, 300);
                int idx = chosen - 1;
                if (idx >= 0 && idx < (int)pState->presetCards.size())
                {
                    pState->selectedPresetForAssign = idx;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            else if (PtInRect(&pState->rcRadioAssignCustom, pt))
            {
                pState->assignFromPreset = false;
                pState->isRemoveMode = false;
                SetFocus(pState->hEditCustomSms);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnBrowseCustom, pt))
            {
                pState->assignFromPreset = false;
                pState->isRemoveMode = false;
                wchar_t szFile[MAX_PATH] = { 0 };
                OPENFILENAMEW ofn = { 0 };
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                std::wstring initialDir = pState->basePath + L"TRAINS\\TRAINSET";
                ofn.lpstrInitialDir = initialDir.c_str();

                if (GetOpenFileNameW(&ofn))
                {
                    std::wstring fullPath = szFile;
                    std::wstring relativePath = fullPath;
                    std::wstring trainsetDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                    if (_wcsnicmp(fullPath.c_str(), trainsetDir.c_str(), trainsetDir.length()) == 0)
                    {
                        relativePath = fullPath.substr(trainsetDir.length());
                    }
                    pState->customSmsPath = relativePath;
                    SetWindowTextW(pState->hEditCustomSms, relativePath.c_str());
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            // Tab 1 Bottom Actions
            else if (PtInRect(&pState->rcBtnPreview, pt))
            {
                ExecuteBatchSoundMutation(pState, true);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnApplyMutations, pt))
            {
                ExecuteBatchSoundMutation(pState, false);
                return 0;
            }
            else if (PtInRect(&pState->rcBtnCloseTab1, pt))
            {
                DestroyWindow(hWnd);
                return 0;
            }
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

        // Dark surface background
        HBRUSH hBg = CreateSolidBrush(PoolTheme::GutterBackground);
        FillRect(memDC, &rcClient, hBg);
        DeleteObject(hBg);

        SetBkMode(memDC, TRANSPARENT);

        // -------------------------------------------------------------------------
        // BURGUNDY SUB-TOOLBAR RIBBON (Y = 66 to 112, Height = 46px)
        // Seamlessly extends active tab surface, exactly matching PoolManagerDlg!
        // -------------------------------------------------------------------------
        int toolbarY = 66;
        int toolbarH = 46;
        RECT rcToolbar = { 0, toolbarY, w, toolbarY + toolbarH };
        HBRUSH hbrTb = CreateSolidBrush(PoolTheme::ToolbarBackground); // RGB(52, 22, 27)
        FillRect(memDC, &rcToolbar, hbrTb);
        DeleteObject(hbrTb);

        HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
        HPEN hOldPen = (HPEN)SelectObject(memDC, hPenLine);
        MoveToEx(memDC, 0, toolbarY + toolbarH, NULL);
        LineTo(memDC, w, toolbarY + toolbarH);
        SelectObject(memDC, hOldPen);
        DeleteObject(hPenLine);

        // =========================================================================
        // TAB 0: SMS PRESETS POOL (COLLAPSIBLE CARDS)
        // =========================================================================
        if (pState->activeTab == 0)
        {
            pState->cardHits.clear();
            pState->chipHits.clear();

            // Left cluster inside burgundy toolbar ribbon (Y = 66 to 112)
            if (pState->hFontIcon)
            {
                SelectObject(memDC, pState->hFontIcon);
                SetTextColor(memDC, RGB(255, 180, 190));
                RECT rcIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
                DrawTextW(memDC, L"\xE8D6", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, PoolTheme::TextPrimary);
            RECT rcTitle = { 46, toolbarY, 260, toolbarY + toolbarH };
            DrawTextW(memDC, L"SMS Sound Presets Pool", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(220, 180, 190));
            std::wstring presetSummary = L"(" + std::to_wstring(pState->presetCards.size()) + L" Sound Groups)";
            RECT rcSub = { 265, toolbarY, 450, toolbarY + toolbarH };
            DrawTextW(memDC, presetSummary.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Right cluster inside burgundy toolbar ribbon
            DrawModernButton(memDC, pState->rcBtnNewPresetGroup, L"New Sound Preset", pState->hoverBtnId == 1, false, true, pState->hFontBold, pState->hFontIconSmall, L"\xE710");
            DrawModernButton(memDC, pState->rcBtnExpandAll, (w < 920) ? L"▾ Expand" : L"▾ Expand All", pState->hoverBtnId == 2, false, false, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnCollapseAll, (w < 920) ? L"▴ Collapse" : L"▴ Collapse All", pState->hoverBtnId == 3, false, false, pState->hFontSmall);

            // Scrollable Cards View
            int contentY = 118;
            int footerH = 46;
            int contentH = h - footerH - contentY;
            RECT rcContentClip = { 0, contentY, w - 1, contentY + contentH };

            // Calculate card heights
            int totalContentH = 8;
            for (const auto& card : pState->presetCards)
            {
                int entryCount = (int)card.entries.size();
                int entriesAreaH = (entryCount == 0) ? 38 : (24 + ((entryCount + 1) / 2) * 32 + 8);
                int cardH = card.isCollapsed ? 36 : (42 + entriesAreaH + 36);
                totalContentH += cardH + 12;
            }
            if (pState->presetCards.empty()) totalContentH = 180;
            totalContentH += 10;

            RECT rcScroll = { w - 12, contentY + 2, w - 2, contentY + contentH - 2 };
            pState->m_vScroll.SetBounds(rcScroll);
            pState->m_vScroll.SetRange(0, totalContentH - 1, contentH);
            pState->scrollY = pState->m_vScroll.GetPos();

            HRGN hRgnClip = CreateRectRgn(rcContentClip.left, rcContentClip.top, rcContentClip.right, rcContentClip.bottom);
            SelectClipRgn(memDC, hRgnClip);

            int cardY = contentY - pState->scrollY + 8;
            int cardMargin = 24;
            int cardW = w - (cardMargin * 2) - 14;

            if (pState->presetCards.empty())
            {
                RECT rcEmpty = { cardMargin + 40, contentY + 30, cardMargin + cardW - 40, contentY + 180 };
                HBRUSH hbrEmpty = CreateSolidBrush(PoolTheme::CardBackground);
                HPEN hpenEmpty = CreatePen(PS_SOLID, 1, PoolTheme::CardBorder);
                SelectObject(memDC, hbrEmpty); SelectObject(memDC, hpenEmpty);
                RoundRect(memDC, rcEmpty.left, rcEmpty.top, rcEmpty.right, rcEmpty.bottom, 10, 10);
                DeleteObject(hbrEmpty); DeleteObject(hpenEmpty);

                SelectObject(memDC, pState->hFontIcon);
                SetTextColor(memDC, RGB(96, 205, 255));
                RECT rcEIcon = { rcEmpty.left, rcEmpty.top + 20, rcEmpty.right, rcEmpty.top + 48 };
                DrawTextW(memDC, L"\xE8D6", -1, &rcEIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                RECT rcETitle = { rcEmpty.left + 20, rcEmpty.top + 52, rcEmpty.right - 20, rcEmpty.top + 76 };
                DrawTextW(memDC, L"No SMS Presets Created Yet", -1, &rcETitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(memDC, pState->hFontSmall);
                SetTextColor(memDC, PoolTheme::TextSecondary);
                RECT rcEDesc = { rcEmpty.left + 30, rcEmpty.top + 78, rcEmpty.right - 30, rcEmpty.top + 110 };
                DrawTextW(memDC, L"Click '+ New Sound Preset' above to create your first sound preset group and store your .sms soundsets.", -1, &rcEDesc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            else
            {
                for (size_t c = 0; c < pState->presetCards.size(); ++c)
                {
                    auto& card = pState->presetCards[c];
                    int entryCount = (int)card.entries.size();
                    int entriesAreaH = (entryCount == 0) ? 38 : (24 + ((entryCount + 1) / 2) * 32 + 8);
                    int cardH = card.isCollapsed ? 36 : (42 + entriesAreaH + 36);
                    RECT rcCard = { cardMargin, cardY, cardMargin + cardW, cardY + cardH };

                    bool isCardSel = ((int)c == pState->selectedPresetCardIdx);
                    bool isDragTarget = ((int)c == pState->dragHoverCardIdx);
                    COLORREF cBg = isDragTarget ? RGB(26, 42, 58) : (isCardSel ? RGB(30, 36, 44) : PoolTheme::CardBackground);
                    COLORREF cBorder = isDragTarget ? RGB(0, 180, 255) : (isCardSel ? RGB(0, 140, 240) : PoolTheme::CardBorder);

                    HBRUSH hbrCard = CreateSolidBrush(cBg);
                    HPEN hpenCard = CreatePen(PS_SOLID, isDragTarget ? 2 : 1, cBorder);
                    SelectObject(memDC, hbrCard); SelectObject(memDC, hpenCard);
                    RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);
                    DeleteObject(hbrCard); DeleteObject(hpenCard);

                    BatchSoundMutatorState::CardHit ch;
                    ch.cardIdx = (int)c;
                    ch.rcCard = rcCard;
                    ch.rcHeader = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 36 };

                    // 1. Collapse toggle button
                    ch.rcChevron = { rcCard.left + 8, rcCard.top + 6, rcCard.left + 30, rcCard.top + 30 };
                    DrawModernButton(memDC, ch.rcChevron, L"", false, false, false, pState->hFontSmall, pState->hFontIconSmall, card.isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                    // 2. Right action buttons: [🗑️ Delete], [▼], [▲], [Clone], [Rename]
                    int hBtnR = rcCard.right - 8;
                    int hBtnY = rcCard.top + 6;
                    int hBtnH = 24;

                    hBtnR -= 28;
                    ch.rcBtnDelete = { hBtnR, hBtnY, hBtnR + 26, hBtnY + hBtnH };
                    DrawModernButton(memDC, ch.rcBtnDelete, L"", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                    hBtnR -= 26;
                    ch.rcBtnMoveDown = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    DrawModernButton(memDC, ch.rcBtnMoveDown, L"", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

                    hBtnR -= 26;
                    ch.rcBtnMoveUp = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                    DrawModernButton(memDC, ch.rcBtnMoveUp, L"", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

                    // [ 📋 Clone ] (Width = 80, Gap = 4)
                    hBtnR -= 84;
                    ch.rcBtnClone = { hBtnR, hBtnY, hBtnR + 80, hBtnY + hBtnH };
                    DrawModernButton(memDC, ch.rcBtnClone, L"Clone", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                    // [ ✏ Rename ] (Width = 88, Gap = 4)
                    hBtnR -= 92;
                    ch.rcBtnRename = { hBtnR, hBtnY, hBtnR + 88, hBtnY + hBtnH };
                    DrawModernButton(memDC, ch.rcBtnRename, L"Rename", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                    // 3. Preset Title, Count & Slot Pill
                    int titleLeft = rcCard.left + 36;
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, PoolTheme::TextPrimary);
                    RECT rcTitle = { titleLeft, rcCard.top, titleLeft + 220, rcCard.top + 36 };
                    DrawTextW(memDC, card.name.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    std::wstring cntStr = L"(" + std::to_wstring(entryCount) + (entryCount == 1 ? L" file)" : L" files)");
                    SelectObject(memDC, pState->hFontSmall);
                    SetTextColor(memDC, PoolTheme::TextSecondary);
                    RECT rcCount = { titleLeft + 224, rcCard.top, titleLeft + 310, rcCard.top + 36 };
                    DrawTextW(memDC, cntStr.c_str(), -1, &rcCount, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    ch.rcSlotPill = { titleLeft + 316, rcCard.top + 6, titleLeft + 416, rcCard.top + 30 };
                    std::wstring slotPillText = card.defaultSlot;
                    DrawCustomDropdownPicker(memDC, ch.rcSlotPill, slotPillText.c_str(), false, pState->hFontBadge, pState->hFontIconSmall, L"\xE8D6");

                    // 4. Expanded Card Body
                    if (!card.isCollapsed)
                    {
                        int boxY = rcCard.top + 38;
                        RECT rcBox = { rcCard.left + 14, boxY, rcCard.right - 14, boxY + entriesAreaH };

                        HBRUSH hbrBox = CreateSolidBrush(PoolTheme::UnitsBoxBackground);
                        HPEN hpenBox = CreatePen(PS_SOLID, 1, PoolTheme::UnitsBoxBorder);
                        SelectObject(memDC, hbrBox); SelectObject(memDC, hpenBox);
                        RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 6, 6);
                        DeleteObject(hbrBox); DeleteObject(hpenBox);

                        if (entryCount == 0)
                        {
                            SelectObject(memDC, pState->hFontSmall);
                            SetTextColor(memDC, PoolTheme::TextMuted);
                            DrawTextW(memDC, L"Click '+ Add .SMS File...' below to add sound files into this preset group.", -1, &rcBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }
                        else
                        {
                            int colTotalW = (rcBox.right - rcBox.left - 24) / 2;
                            int chipH = 26;
                            for (int u = 0; u < entryCount; ++u)
                            {
                                const auto& entry = card.entries[u];
                                int col = u % 2;
                                int row = u / 2;
                                int colX = rcBox.left + 8 + col * (colTotalW + 8);
                                int chipY = rcBox.top + 8 + row * (chipH + 6);
                                RECT rcChip = { colX, chipY, colX + colTotalW, chipY + chipH };

                                BatchSoundMutatorState::ChipHit chipH;
                                chipH.cardIdx = (int)c;
                                chipH.entryIdx = u;
                                chipH.rcChip = rcChip;
                                chipH.rcBtnRemove = { rcChip.right - 24, rcChip.top + 2, rcChip.right - 4, rcChip.bottom - 2 };
                                pState->chipHits.push_back(chipH);

                                HBRUSH hbrChip = CreateSolidBrush(PoolTheme::ChipBackground);
                                HPEN hpenChip = CreatePen(PS_SOLID, 1, PoolTheme::ChipBorder);
                                SelectObject(memDC, hbrChip); SelectObject(memDC, hpenChip);
                                RoundRect(memDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                                DeleteObject(hbrChip); DeleteObject(hpenChip);

                                // Audio Glyph
                                SelectObject(memDC, pState->hFontIconSmall);
                                SetTextColor(memDC, RGB(96, 205, 255));
                                RECT rcIcon = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                DrawTextW(memDC, L"\xE8D6", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                // Sound File Display: "filename.sms (directory)"
                                SelectObject(memDC, pState->hFontSmall);
                                SetTextColor(memDC, PoolTheme::TextPrimary);
                                std::wstring displayText = entry.fileName;
                                if (!entry.directory.empty())
                                {
                                    displayText += L" (" + entry.directory + L")";
                                }
                                RECT rcText = { rcChip.left + 26, rcChip.top, rcChip.right - 28, rcChip.bottom };
                                DrawTextW(memDC, displayText.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                // Remove button [🗑 Delete]
                                DrawModernButton(memDC, chipH.rcBtnRemove, L"", false, false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");
                            }
                        }

                        // Bottom toolbar inside card: [+ Add .SMS File...]
                        int actY = boxY + entriesAreaH + 4;
                        ch.rcBtnAddSms = { rcCard.left + 14, actY, rcCard.left + 140, actY + 26 };
                        DrawModernButton(memDC, ch.rcBtnAddSms, L"Add .SMS File...", false, false, true, pState->hFontSmall, pState->hFontIconSmall, L"\xE710");
                    }

                    pState->cardHits.push_back(ch);
                    cardY += cardH + 12;
                }
            }

            SelectClipRgn(memDC, NULL);
            DeleteObject(hRgnClip);

            pState->m_vScroll.Paint(memDC);

            // Footer Bar (Tab 0)
            int footerTop = h - footerH;
            RECT rcFooter = { 0, footerTop, w, h };
            HBRUSH hbrFoot = CreateSolidBrush(PoolTheme::FooterBackground);
            FillRect(memDC, &rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            HPEN hPenDiv = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            SelectObject(memDC, hPenDiv);
            MoveToEx(memDC, 0, footerTop, NULL);
            LineTo(memDC, w, footerTop);
            DeleteObject(hPenDiv);

            if (pState->onApplyCallback)
            {
                DrawModernButton(memDC, pState->rcBtnApplyPresetTab0, L"Apply Preset to Consist", pState->hoverBtnId == 5, false, true, pState->hFontBold, pState->hFontIconSmall, L"\xE73E");
            }
            DrawModernButton(memDC, pState->rcBtnCloseTab0, L"Close", pState->hoverBtnId == 4, false, false, pState->hFontBold);
        }
        // =========================================================================
        // TAB 1: BATCH SOUND MUTATOR
        // =========================================================================
        else
        {
            // Left cluster inside burgundy toolbar ribbon (Y = 66 to 112)
            if (pState->hFontIcon)
            {
                SelectObject(memDC, pState->hFontIcon);
                SetTextColor(memDC, RGB(255, 180, 190));
                RECT rcIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
                DrawTextW(memDC, L"\xE916", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, PoolTheme::TextPrimary);
            RECT rcTitle = { 46, toolbarY, 230, toolbarY + toolbarH };
            DrawTextW(memDC, L"Consist Sound Queue", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(220, 180, 190));
            std::wstring qStatus;
            if (pState->displayFilter == StockDisplayFilter::All)
            {
                qStatus = L"(" + std::to_wstring(pState->consistFilePaths.size()) + L" Consists  •  " +
                          std::to_wstring(pState->resolvedUnits.size()) + L" Unique Units)";
            }
            else
            {
                qStatus = L"(" + std::to_wstring(pState->visibleUnitIndices.size()) + L" Shown / " +
                          std::to_wstring(pState->resolvedUnits.size()) + L" Total  •  " +
                          std::to_wstring(pState->consistFilePaths.size()) + L" Consists)";
            }
            int subRight = (pState->rcBtnExpandAllTab1.left > 0) ? pState->rcBtnExpandAllTab1.left - 10 : (w - 420);
            RECT rcSub = { 235, toolbarY, subRight, toolbarY + toolbarH };
            DrawTextW(memDC, qStatus.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // Right cluster inside burgundy toolbar ribbon
            DrawModernButton(memDC, pState->rcBtnExpandAllTab1, (w < 920) ? L"▾ Expand" : L"▾ Expand All", pState->hoverBtnId == 25, false, false, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnCollapseAllTab1, (w < 920) ? L"▴ Collapse" : L"▴ Collapse All", pState->hoverBtnId == 26, false, false, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnClearConsists, L"Clear", pState->hoverBtnId == 13, false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

            // Sub-bar buttons below Stock List (Active showcase filters + batch selection)
            bool isAllActive = (pState->displayFilter == StockDisplayFilter::All);
            bool isEngActive = (pState->displayFilter == StockDisplayFilter::EnginesOnly);
            bool isWagActive = (pState->displayFilter == StockDisplayFilter::WagonsOnly);

            DrawModernButton(memDC, pState->rcBtnFilterAll, L"All Units", pState->hoverBtnId == 14, false, isAllActive, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnFilterEngines, L"Engines Only", pState->hoverBtnId == 15, false, isEngActive, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnFilterWagons, L"Wagons Only", pState->hoverBtnId == 16, false, isWagActive, pState->hFontSmall);

            DrawModernButton(memDC, pState->rcBtnSelectAll, L"Select All", pState->hoverBtnId == 17, false, false, pState->hFontSmall);
            DrawModernButton(memDC, pState->rcBtnSelectNone, L"Clear All", pState->hoverBtnId == 18, false, false, pState->hFontSmall);

            // Left & Right Operation Cards
            HBRUSH hPanelB = CreateSolidBrush(PoolTheme::CardBackground);
            HPEN hPanelP = CreatePen(PS_SOLID, 1, PoolTheme::CardBorder);
            HGDIOBJ oB = SelectObject(memDC, hPanelB);
            HGDIOBJ oP = SelectObject(memDC, hPanelP);
            RoundRect(memDC, pState->rcLeftPanel.left, pState->rcLeftPanel.top, pState->rcLeftPanel.right, pState->rcLeftPanel.bottom, 8, 8);
            RoundRect(memDC, pState->rcRightPanel.left, pState->rcRightPanel.top, pState->rcRightPanel.right, pState->rcRightPanel.bottom, 8, 8);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hPanelB);
            DeleteObject(hPanelP);

            // ----------------- LEFT PANEL PAINTING -----------------
            // Row 1: Mode Remove (height 24px, 14x14 radio circle centered at top+5)
            RECT rcRadRemove = { pState->rcRadioModeRemove.left + 2, pState->rcRadioModeRemove.top + 5, pState->rcRadioModeRemove.left + 16, pState->rcRadioModeRemove.top + 19 };
            DrawCustomRadioButton(memDC, rcRadRemove, pState->isRemoveMode, false);
            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, pState->isRemoveMode ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblRem = { rcRadRemove.right + 8, pState->rcRadioModeRemove.top, pState->rcRadioModeRemove.right, pState->rcRadioModeRemove.bottom };
            DrawTextW(memDC, L"Mode: Remove / Strip Sound Streams", -1, &rcLblRem, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Row 2: Target slot checkboxes (height 24px, 14x14 box centered at top+5)
            auto drawSlotChk = [&](const RECT& rcSlot, bool val, const wchar_t* label) {
                RECT rcBox = { rcSlot.left + 2, rcSlot.top + 5, rcSlot.left + 16, rcSlot.top + 19 };
                DrawCustomCheckbox(memDC, rcBox, val, false, pState->hFontIconSmall);
                SelectObject(memDC, pState->hFontSmall);
                SetTextColor(memDC, val ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
                RECT rcLbl = { rcBox.right + 6, rcSlot.top, rcSlot.right, rcSlot.bottom };
                DrawTextW(memDC, label, -1, &rcLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };

            drawSlotChk(pState->rcChkSlotWagon, pState->removeWagon, L"Wagon Sound");
            drawSlotChk(pState->rcChkSlotEngine, pState->removeEngine, L"Engine Sound");
            drawSlotChk(pState->rcChkSlotCab, pState->removeCab, L"ORTS 3D Cab");
            drawSlotChk(pState->rcChkSlotInside, pState->removeInside, L"Inside Sound");

            // Row 3: Sub-radio Wipe (height 24px, 14x14 radio centered at top+5)
            RECT rcRadWipe = { pState->rcRadioRemoveAll.left + 2, pState->rcRadioRemoveAll.top + 5, pState->rcRadioRemoveAll.left + 16, pState->rcRadioRemoveAll.top + 19 };
            DrawCustomRadioButton(memDC, rcRadWipe, pState->removeAllInSlot, false);
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, pState->removeAllInSlot ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblWipe = { rcRadWipe.right + 8, pState->rcRadioRemoveAll.top, pState->rcRadioRemoveAll.right, pState->rcRadioRemoveAll.bottom };
            DrawTextW(memDC, L"Remove ALL sounds in slot (Slot Wipe)", -1, &rcLblWipe, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Row 4: Sub-radio Match + Detected SMS picker (height 26px, radio centered at top+6)
            RECT rcRadMatch = { pState->rcRadioRemoveMatch.left + 2, pState->rcRadioRemoveMatch.top + 6, pState->rcRadioRemoveMatch.left + 16, pState->rcRadioRemoveMatch.top + 20 };
            DrawCustomRadioButton(memDC, rcRadMatch, !pState->removeAllInSlot, false);
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, !pState->removeAllInSlot ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblMatch = { rcRadMatch.right + 8, pState->rcRadioRemoveMatch.top, pState->rcRadioRemoveMatch.right, pState->rcRadioRemoveMatch.bottom };
            DrawTextW(memDC, L"Remove ONLY matching SMS:", -1, &rcLblMatch, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Detected SMS Multi-Select Dropdown Picker
            std::wstring pickerLabel;
            if (pState->selectedSmsToRemove.empty())
            {
                pickerLabel = L"Select SMS to remove ▾";
            }
            else if (pState->selectedSmsToRemove.size() == 1)
            {
                pickerLabel = pState->selectedSmsToRemove[0] + L" ▾";
            }
            else
            {
                pickerLabel = std::to_wstring(pState->selectedSmsToRemove.size()) + L" SMS Selected ▾";
            }
            DrawCustomDropdownPicker(memDC, pState->rcPickerMatchSms, pickerLabel.c_str(), pState->hoverBtnId == 27, pState->hFontSmall, pState->hFontIconSmall, L"\xE8D6");

            // ----------------- RIGHT PANEL PAINTING -----------------
            // Row 1: Mode Assign (height 24px, 14x14 radio centered at top+5)
            RECT rcRadAssign = { pState->rcRightPanel.left + 16, pState->rcRadioModeAssign.top + 5, pState->rcRightPanel.left + 30, pState->rcRadioModeAssign.top + 19 };
            DrawCustomRadioButton(memDC, rcRadAssign, !pState->isRemoveMode, false);
            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, !pState->isRemoveMode ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblAss = { rcRadAssign.right + 8, pState->rcRadioModeAssign.top, pState->rcRadioModeAssign.right, pState->rcRadioModeAssign.bottom };
            DrawTextW(memDC, L"Mode: Assign / Mutate Sound Streams", -1, &rcLblAss, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Target Slot picker dropdown
            DrawCustomDropdownPicker(memDC, pState->rcPickerAssignSlot, pState->assignTargetSlot.c_str(), false, pState->hFontBadge, pState->hFontIconSmall, L"\xE8D6");

            // Row 2: Action Choice: [ (•) Replace Slot (Overwrite) ]  [ ( ) Append Stream (Multi-SMS) ]
            RECT rcRadRep = { pState->rcRadioAssignReplace.left + 2, pState->rcRadioAssignReplace.top + 5, pState->rcRadioAssignReplace.left + 16, pState->rcRadioAssignReplace.top + 19 };
            DrawCustomRadioButton(memDC, rcRadRep, !pState->isRemoveMode && !pState->appendStreamMode, false);
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, (!pState->isRemoveMode && !pState->appendStreamMode) ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblRep = { rcRadRep.right + 6, pState->rcRadioAssignReplace.top, pState->rcRadioAssignReplace.right, pState->rcRadioAssignReplace.bottom };
            DrawTextW(memDC, L"Replace Slot (Overwrite)", -1, &rcLblRep, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT rcRadApp = { pState->rcRadioAssignAppend.left + 2, pState->rcRadioAssignAppend.top + 5, pState->rcRadioAssignAppend.left + 16, pState->rcRadioAssignAppend.top + 19 };
            DrawCustomRadioButton(memDC, rcRadApp, !pState->isRemoveMode && pState->appendStreamMode, false);
            SetTextColor(memDC, (!pState->isRemoveMode && pState->appendStreamMode) ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblApp = { rcRadApp.right + 6, pState->rcRadioAssignAppend.top, pState->rcRadioAssignAppend.right, pState->rcRadioAssignAppend.bottom };
            DrawTextW(memDC, L"Append Stream (Multi-SMS)", -1, &rcLblApp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Row 2: From Preset (height 26px, 14x14 radio centered at top+6)
            RECT rcRadPr = { pState->rcRadioAssignPreset.left + 2, pState->rcRadioAssignPreset.top + 6, pState->rcRadioAssignPreset.left + 16, pState->rcRadioAssignPreset.top + 20 };
            DrawCustomRadioButton(memDC, rcRadPr, pState->assignFromPreset, false);
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, pState->assignFromPreset ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblPr = { rcRadPr.right + 8, pState->rcRadioAssignPreset.top, pState->rcRadioAssignPreset.right, pState->rcRadioAssignPreset.bottom };
            DrawTextW(memDC, L"From SMS Pool Preset:", -1, &rcLblPr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            std::wstring prDisplayName = L"(Select from SMS Presets)";
            if (pState->selectedPresetForAssign >= 0 && pState->selectedPresetForAssign < (int)pState->presetCards.size())
            {
                prDisplayName = pState->presetCards[pState->selectedPresetForAssign].name;
            }
            DrawCustomDropdownPicker(memDC, pState->rcPickerPreset, prDisplayName.c_str(), false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8D6");

            // Row 3: Custom SMS Path (height 26px, 14x14 radio centered at top+6)
            RECT rcRadCust = { pState->rcRadioAssignCustom.left + 2, pState->rcRadioAssignCustom.top + 6, pState->rcRadioAssignCustom.left + 16, pState->rcRadioAssignCustom.top + 20 };
            DrawCustomRadioButton(memDC, rcRadCust, !pState->assignFromPreset, false);
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, !pState->assignFromPreset ? PoolTheme::TextPrimary : PoolTheme::TextSecondary);
            RECT rcLblCust = { rcRadCust.right + 8, pState->rcRadioAssignCustom.top, pState->rcRadioAssignCustom.right, pState->rcRadioAssignCustom.bottom };
            DrawTextW(memDC, L"Custom SMS Path:", -1, &rcLblCust, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Custom SMS edit box container
            HBRUSH hCustBg = CreateSolidBrush(RGB(28, 28, 30));
            HPEN hCustPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 65));
            SelectObject(memDC, hCustBg); SelectObject(memDC, hCustPen);
            RoundRect(memDC, pState->rcEditCustomSmsBox.left, pState->rcEditCustomSmsBox.top, pState->rcEditCustomSmsBox.right, pState->rcEditCustomSmsBox.bottom, 4, 4);
            DeleteObject(hCustBg); DeleteObject(hCustPen);

            DrawModernButton(memDC, pState->rcBtnBrowseCustom, L"Browse...", pState->hoverBtnId == 19, false, false, pState->hFontSmall);

            // Footer Bar (Tab 1)
            int footerH = 46;
            int footerTop = h - footerH;
            RECT rcFooter = { 0, footerTop, w, h };
            HBRUSH hbrFoot = CreateSolidBrush(PoolTheme::FooterBackground);
            FillRect(memDC, &rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            HPEN hPenDiv = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            SelectObject(memDC, hPenDiv);
            MoveToEx(memDC, 0, footerTop, NULL);
            LineTo(memDC, w, footerTop);
            DeleteObject(hPenDiv);

            DrawModernButton(memDC, pState->rcBtnPreview, L"Preview Changes", pState->hoverBtnId == 20, false, false, pState->hFontBold, pState->hFontIconSmall, L"\xE721");
            DrawModernButton(memDC, pState->rcBtnApplyMutations, L"Execute Sound Mutation", pState->hoverBtnId == 21, false, true, pState->hFontBold, pState->hFontIconSmall, L"\xE768");
            DrawModernButton(memDC, pState->rcBtnCloseTab1, L"Close", pState->hoverBtnId == 22, false, false, pState->hFontBold);
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NOTIFY:
    {
        NMHDR* pnmh = (NMHDR*)lParam;
        if (pnmh && pState && pnmh->hwndFrom == pState->stockList.GetHWND())
        {
            if (pnmh->code == NM_CLICK || pnmh->code == NM_CELLCLICK)
            {
                NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
                int row = (pnmh->code == NM_CELLCLICK && pCell && pCell->itemIndex >= 0) ? pCell->itemIndex : pState->stockList.GetSelectedIndex();
                int col = pCell ? pCell->subItemIndex : 0;
                if (row >= 0 && row < (int)pState->displayRows.size())
                {
                    const auto& dRow = pState->displayRows[row];
                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->resolvedUnits.size())
                    {
                        auto& u = pState->resolvedUnits[dRow.unitIndex];
                        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
                        {
                            if (col == 0)
                            {
                                POINT pt;
                                GetCursorPos(&pt);
                                ScreenToClient(pState->stockList.GetHWND(), &pt);

                                // Col 0 is 60px wide: chevron is at 0..18, checkbox is at 18..38
                                if (pt.x < 18)
                                {
                                    // Chevron clicked -> toggle expansion
                                    u.isExpanded = !u.isExpanded;
                                    PopulateStockListControl(pState);
                                    return 0;
                                }
                                else if (pt.x >= 18 && pt.x <= 38)
                                {
                                    // Checkbox clicked
                                    if (!u.hasMissingStock)
                                    {
                                        u.isSelected = !u.isSelected;
                                        PopulateStockListControl(pState);
                                    }
                                    return 0;
                                }
                                else
                                {
                                    // Clicked outside checkbox in col 0 -> toggle expansion
                                    u.isExpanded = !u.isExpanded;
                                    PopulateStockListControl(pState);
                                    return 0;
                                }
                            }
                        }
                    }
                }
                return 0;
            }
            else if (pnmh->code == NM_DBLCLK)
            {
                NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;
                int row = (pCell && pCell->itemIndex >= 0) ? pCell->itemIndex : pState->stockList.GetSelectedIndex();
                int col = pCell ? pCell->subItemIndex : 0;
                if (row >= 0 && row < (int)pState->displayRows.size())
                {
                    const auto& dRow = pState->displayRows[row];
                    if (dRow.unitIndex >= 0 && dRow.unitIndex < (int)pState->resolvedUnits.size())
                    {
                        auto& u = pState->resolvedUnits[dRow.unitIndex];
                        if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_UNIT)
                        {
                            // Double-click parent unit row -> toggle expansion
                            u.isExpanded = !u.isExpanded;
                            PopulateStockListControl(pState);
                            return 0;
                        }
                        else if (dRow.rowType == BatchSoundMutatorState::BATCH_ROW_STREAM)
                        {
                            // Double click stream row -> open sms file
                            const RSSSoundEntry* pEntry = nullptr;
                            if (dRow.category == L"Wagon" && dRow.streamIndex < (int)u.soundInfo.wagonSounds.size()) pEntry = &u.soundInfo.wagonSounds[dRow.streamIndex];
                            else if (dRow.category == L"Engine" && dRow.streamIndex < (int)u.soundInfo.engineSounds.size()) pEntry = &u.soundInfo.engineSounds[dRow.streamIndex];
                            else if (dRow.category == L"ORTS3DCab" && dRow.streamIndex < (int)u.soundInfo.orts3dCabSounds.size()) pEntry = &u.soundInfo.orts3dCabSounds[dRow.streamIndex];
                            else if (dRow.category == L"Inside" && dRow.streamIndex < (int)u.soundInfo.insideSounds.size()) pEntry = &u.soundInfo.insideSounds[dRow.streamIndex];

                            if (pEntry && !pEntry->smsFileName.empty())
                            {
                                OpenSmsInDefaultEditor(pEntry->smsFileName, pState->basePath);
                                return 0;
                            }
                        }
                    }
                }
                return 0;
            }
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState->hFontBody) DeleteObject(pState->hFontBody);
        if (pState->hFontBold) DeleteObject(pState->hFontBold);
        if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
        if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
        if (pState->hFontBadge) DeleteObject(pState->hFontBadge);
        if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
        if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);

        g_hBatchSoundMutatorDlg = NULL;
        delete pState;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ShowBatchSoundMutatorDialog(HWND hWndParent, const std::wstring& basePath, const std::vector<std::wstring>& initialConsistFiles, int initialTab, SmsPoolPresetSelectCallback onApplyCallback)
{
    if (g_hBatchSoundMutatorDlg && IsWindow(g_hBatchSoundMutatorDlg))
    {
        BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(g_hBatchSoundMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            if (pState->hTitleBar)
            {
                CustomTitleBar_SetActiveTab(pState->hTitleBar, initialTab);
            }
            SwitchActiveTab(pState, initialTab);
            if (!initialConsistFiles.empty())
            {
                pState->consistFilePaths = initialConsistFiles;
                ScanAndResolveConsists(pState);
            }
        }
        ShowWindow(g_hBatchSoundMutatorDlg, SW_RESTORE);
        SetForegroundWindow(g_hBatchSoundMutatorDlg);
        return;
    }

    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = BatchSoundMutatorWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107 /* IDI_TRAINSIMCONSISTBUILDER */));
        wc.hIconSm = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (!wc.hIconSm) wc.hIconSm = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(108 /* IDI_SMALL */));
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = BATCH_SOUND_WNDCLASS;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    BatchSoundMutatorState* pState = new BatchSoundMutatorState();
    pState->hParent = hWndParent;
    pState->basePath = basePath;
    pState->consistFilePaths = initialConsistFiles;
    pState->activeTab = initialTab;
    pState->onApplyCallback = onApplyCallback;

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 1280, h = 750;
    HMONITOR hMon = MonitorFromWindow(hWndParent ? hWndParent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    if (hMon)
    {
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi))
        {
            int maxW = (mi.rcWork.right - mi.rcWork.left) - 40;
            int maxH = (mi.rcWork.bottom - mi.rcWork.top) - 40;
            if (w > maxW) w = maxW;
            if (h > maxH) h = maxH;
        }
    }
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_APPWINDOW,
        BATCH_SOUND_WNDCLASS,
        L"Sound Studio - Presets & Batch Mutator",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hWndParent, NULL, GetModuleHandleW(NULL), pState
    );

    if (!hDlg) return;

    HICON hIconBig = (HICON)GetClassLongPtrW(hDlg, GCLP_HICON);
    HICON hIconSm = (HICON)GetClassLongPtrW(hDlg, GCLP_HICONSM);
    if (hIconBig) SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSm)  SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    g_hBatchSoundMutatorDlg = hDlg;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    COLORREF borderColor = PoolTheme::BorderLine;
    DwmSetWindowAttribute(hDlg, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hDlg, &margins);

    SetWindowPos(hDlg, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
}

bool BatchSoundMutatorDlg_IsOpen()
{
    return (g_hBatchSoundMutatorDlg != NULL && IsWindow(g_hBatchSoundMutatorDlg));
}

void BatchSoundMutatorDlg_UpdateSelection(const std::vector<std::wstring>& targetConsistFilePaths)
{
    if (g_hBatchSoundMutatorDlg && IsWindow(g_hBatchSoundMutatorDlg))
    {
        BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(g_hBatchSoundMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            if (pState->consistFilePaths != targetConsistFilePaths)
            {
                pState->consistFilePaths = targetConsistFilePaths;
                ScanAndResolveConsists(pState);
                InvalidateRect(g_hBatchSoundMutatorDlg, NULL, TRUE);
            }
        }
    }
}

bool BatchSoundMutatorDlg_HandleDragHover(POINT ptScreen, const ScannedSoundItem* pItem, std::wstring& outActionText)
{
    if (!g_hBatchSoundMutatorDlg || !IsWindow(g_hBatchSoundMutatorDlg) || !IsWindowVisible(g_hBatchSoundMutatorDlg))
        return false;

    RECT rcDlg = { 0 };
    GetWindowRect(g_hBatchSoundMutatorDlg, &rcDlg);
    if (!PtInRect(&rcDlg, ptScreen))
    {
        BatchSoundMutatorDlg_ClearDragHover();
        return false;
    }

    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(g_hBatchSoundMutatorDlg, GWLP_USERDATA);
    if (!pState || pState->activeTab != 0)
    {
        BatchSoundMutatorDlg_ClearDragHover();
        return false;
    }

    POINT ptClient = ptScreen;
    ScreenToClient(g_hBatchSoundMutatorDlg, &ptClient);

    int hoverCard = -1;
    for (const auto& ch : pState->cardHits)
    {
        if (PtInRect(&ch.rcCard, ptClient))
        {
            hoverCard = ch.cardIdx;
            break;
        }
    }

    if (hoverCard < 0 || hoverCard >= (int)pState->presetCards.size())
    {
        BatchSoundMutatorDlg_ClearDragHover();
        outActionText = L"";
        return false;
    }

    const auto& card = pState->presetCards[hoverCard];
    outActionText = L"+ Add to Preset: " + card.name;

    if (pState->dragHoverCardIdx != hoverCard)
    {
        pState->dragHoverCardIdx = hoverCard;
        InvalidateRect(g_hBatchSoundMutatorDlg, NULL, FALSE);
    }
    return true;
}

void BatchSoundMutatorDlg_ClearDragHover()
{
    if (!g_hBatchSoundMutatorDlg || !IsWindow(g_hBatchSoundMutatorDlg)) return;
    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(g_hBatchSoundMutatorDlg, GWLP_USERDATA);
    if (!pState) return;

    if (pState->dragHoverCardIdx != -1)
    {
        pState->dragHoverCardIdx = -1;
        InvalidateRect(g_hBatchSoundMutatorDlg, NULL, FALSE);
    }
}

bool BatchSoundMutatorDlg_HandleDrop(POINT ptScreen, const ScannedSoundItem& item)
{
    if (!g_hBatchSoundMutatorDlg || !IsWindow(g_hBatchSoundMutatorDlg) || !IsWindowVisible(g_hBatchSoundMutatorDlg))
        return false;

    RECT rcDlg = { 0 };
    GetWindowRect(g_hBatchSoundMutatorDlg, &rcDlg);
    if (!PtInRect(&rcDlg, ptScreen))
    {
        BatchSoundMutatorDlg_ClearDragHover();
        return false;
    }

    BatchSoundMutatorState* pState = (BatchSoundMutatorState*)GetWindowLongPtrW(g_hBatchSoundMutatorDlg, GWLP_USERDATA);
    if (!pState || pState->activeTab != 0)
    {
        BatchSoundMutatorDlg_ClearDragHover();
        return false;
    }

    int dropCard = pState->dragHoverCardIdx;
    if (dropCard < 0 || dropCard >= (int)pState->presetCards.size())
    {
        POINT ptClient = ptScreen;
        ScreenToClient(g_hBatchSoundMutatorDlg, &ptClient);
        for (const auto& ch : pState->cardHits)
        {
            if (PtInRect(&ch.rcCard, ptClient))
            {
                dropCard = ch.cardIdx;
                break;
            }
        }
    }

    if (dropCard < 0 || dropCard >= (int)pState->presetCards.size())
    {
        BatchSoundMutatorDlg_ClearDragHover();
        return false;
    }

    auto& card = pState->presetCards[dropCard];
    std::wstring relativePath = item.szRelativePath;
    if (relativePath.empty())
    {
        relativePath = item.szFullPath;
        std::wstring trainsetDir = pState->basePath + L"TRAINS\\TRAINSET\\";
        if (_wcsnicmp(relativePath.c_str(), trainsetDir.c_str(), trainsetDir.length()) == 0)
        {
            relativePath = relativePath.substr(trainsetDir.length());
        }
    }

    size_t ls = relativePath.find_last_of(L"\\/");
    std::wstring fName = !item.szFileName.empty() ? item.szFileName : ((ls != std::wstring::npos) ? relativePath.substr(ls + 1) : relativePath);
    std::wstring dir = !item.szFolder.empty() ? item.szFolder : ((ls != std::wstring::npos) ? relativePath.substr(0, ls) : L"");

    // Avoid duplicate entry in the same preset card
    bool alreadyExists = false;
    for (const auto& e : card.entries)
    {
        if (_wcsicmp(e.fullRelative.c_str(), relativePath.c_str()) == 0)
        {
            alreadyExists = true;
            break;
        }
    }

    if (!alreadyExists)
    {
        card.entries.push_back({ fName, dir, relativePath, card.defaultSlot });
        SavePresetCardToDb(card);
    }

    pState->dragHoverCardIdx = -1;
    InvalidateRect(g_hBatchSoundMutatorDlg, NULL, FALSE);
    return true;
}
