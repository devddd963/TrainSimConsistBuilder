#include "SmsStreamMutatorDlg.h"
#include "UITheme.h"
#include "PoolManagerDlg.h"
#include "CustomTitleBar.h"
#include "ModernMessageBox.h"
#include "CustomDropDownMenu.h"
#include "CustomScrollBar.h"
#include "../SRC/SmsParser.h"
#include "../SRC/ConsistReader.h"
#include "../SRC/StockSpecReader.h"
#include "../SRC/DatabaseManager.h"
#include "../SRC/AppLogging.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include "SoundStudio.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <fstream>
#include "ModernContextMenu.h"
#include "FluentDragGhost.h"
#include <filesystem>
#include <set>
#include <gdiplus.h>

namespace fs = std::filesystem;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

static HWND g_hSmsStreamMutatorDlg = NULL;

static ULONG_PTR s_smsGdiplusToken = 0;
static int s_smsGdiplusRefCount = 0;

static void EnsureGdiPlus()
{
    if (s_smsGdiplusRefCount == 0)
    {
        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&s_smsGdiplusToken, &gsi, NULL);
    }
    s_smsGdiplusRefCount++;
}

static void ReleaseGdiPlus()
{
    s_smsGdiplusRefCount--;
    if (s_smsGdiplusRefCount <= 0)
    {
        s_smsGdiplusRefCount = 0;
        if (s_smsGdiplusToken)
        {
            Gdiplus::GdiplusShutdown(s_smsGdiplusToken);
            s_smsGdiplusToken = 0;
        }
    }
}

static void AddRoundedRectPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF rc, Gdiplus::REAL radius)
{
    Gdiplus::REAL d = radius * 2.0f;
    if (d > rc.Width) d = rc.Width;
    if (d > rc.Height) d = rc.Height;

    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

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
            int iconW = iconSz.cx > 0 ? iconSz.cx : 14;

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
            if (rcCopy.right - rcCopy.left > 32) InflateRect(&rcCopy, -8, 0);
            DrawTextW(hdc, iconGlyph, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (hasText)
        {
            SelectObject(hdc, hFont);
            RECT rcCopy = rc;
            if (rcCopy.right - rcCopy.left > 32) InflateRect(&rcCopy, -6, 0);
            DrawTextW(hdc, text, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    static void DrawCustomCheckbox(HDC hdc, const RECT& rcBox, bool isChecked, bool isHovered, HFONT hFontBold = NULL, bool isIndeterminate = false)
    {
        UITheme::DrawCheckbox(hdc, rcBox, isChecked, isHovered, isIndeterminate);
    }

    static void DrawCustomRadioButton(HDC hdc, const RECT& rcCircle, bool isSelected, bool isHovered)
    {
        int cx = rcCircle.left + (rcCircle.right - rcCircle.left) / 2;
        int cy = rcCircle.top + (rcCircle.bottom - rcCircle.top) / 2;
        int R = 6;

        COLORREF bg = isHovered ? RGB(45, 45, 50) : RGB(28, 28, 32);
        COLORREF border = isSelected ? RGB(0, 140, 240) : (isHovered ? RGB(90, 90, 95) : RGB(65, 65, 70));

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ holdBr = SelectObject(hdc, hbr);
        HGDIOBJ holdPen = SelectObject(hdc, hpen);

        Ellipse(hdc, cx - R, cy - R, cx + R + 1, cy + R + 1);

        if (isSelected)
        {
            int r = 3;
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
// Quick Input Modal (for New Preset Card / Rename)
// ------------------------------------------------------------------------------------------------
struct StreamInputModalState
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

static LRESULT CALLBACK StreamInputModalProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    StreamInputModalState* pState = (StreamInputModalState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (StreamInputModalState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    case WM_CREATE:
    {
        pState->hFontTitle  = CreateCustomFont(14, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontPrompt = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMain   = CreateCustomFont(11, FW_NORMAL, L"Segoe UI");
        pState->hFontBold   = CreateCustomFont(11, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon   = CreateCustomFont(12, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateCustomFont(12, FW_NORMAL, L"Segoe MDL2 Assets");

        // dlgW = 500, dlgH = 210
        // rcEditBox is: { 24, 74, 476, 118 } (height 44)
        // Vertically centered: y = 74 + (44 - 24) / 2 = 84
        pState->hEdit = CreateWindowExW(
            0, L"EDIT", pState->resultText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            36, 84, 428, 24, hWnd, (HMENU)101, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEdit, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
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
            (pt.x >= 24 && pt.x <= 476 && pt.y >= 74 && pt.y <= 118))
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
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        COLORREF bgCol = RGB(26, 29, 38);
        COLORREF txtCol = RGB(245, 248, 255);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, txtCol);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(26, 29, 38));
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

        // 1. Background Fill (Windows 11 dark mode canvas)
        COLORREF bgCol = RGB(20, 22, 28);
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hdcMem, &rc, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hdcMem, TRANSPARENT);

        // 2. Header Icon & Title
        if (pState->hFontIcon)
        {
            SelectObject(hdcMem, pState->hFontIcon);
            SetTextColor(hdcMem, RGB(96, 205, 255));
            RECT rcIcon = { 20, 16, 44, 44 };
            DrawTextW(hdcMem, L"\xE70F", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdcMem, pState->hFontTitle);
        SetTextColor(hdcMem, RGB(245, 248, 255));
        RECT rcTitle = { 46, 15, w - 50, 44 };
        DrawTextW(hdcMem, pState->title.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close Button [✕] at top right
        pState->rcCloseBtn = { w - 38, 14, w - 14, 38 };
        if (pState->isHoverClose)
        {
            HBRUSH hbrCloseHover = CreateSolidBrush(RGB(180, 40, 40));
            FillRect(hdcMem, &pState->rcCloseBtn, hbrCloseHover);
            DeleteObject(hbrCloseHover);
        }
        SelectObject(hdcMem, pState->hFontMain);
        SetTextColor(hdcMem, pState->isHoverClose ? RGB(255, 255, 255) : RGB(160, 170, 185));
        DrawTextW(hdcMem, L"✕", -1, &pState->rcCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3. Prompt Subtitle Label
        SelectObject(hdcMem, pState->hFontPrompt);
        SetTextColor(hdcMem, RGB(165, 175, 195));
        RECT rcPrompt = { 24, 48, w - 24, 70 };
        DrawTextW(hdcMem, pState->prompt.c_str(), -1, &rcPrompt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 4. Custom Rounded Edit Box Container (44px high, edit is 24px high and perfectly centered!)
        RECT rcEditBox = { 24, 74, w - 24, 118 };
        COLORREF editBoxBg = RGB(26, 29, 38);
        COLORREF editBoxBorder = RGB(52, 60, 78);

        HBRUSH hbrBox = CreateSolidBrush(editBoxBg);
        HPEN hPenBox = CreatePen(PS_SOLID, 1, editBoxBorder);
        HBRUSH holdB = (HBRUSH)SelectObject(hdcMem, hbrBox);
        HPEN holdP = (HPEN)SelectObject(hdcMem, hPenBox);
        RoundRect(hdcMem, rcEditBox.left, rcEditBox.top, rcEditBox.right, rcEditBox.bottom, 4, 4);
        SelectObject(hdcMem, holdB);
        SelectObject(hdcMem, holdP);
        DeleteObject(hbrBox);
        DeleteObject(hPenBox);

        // 5. Buttons Row
        int btnW = 100;
        int btnH = 36;
        int btnY = h - 50;
        int btnOkX = w - 24 - btnW;
        int btnCancelX = btnOkX - 12 - btnW;

        pState->rcOkBtn = { btnOkX, btnY, btnOkX + btnW, btnY + btnH };
        pState->rcCancelBtn = { btnCancelX, btnY, btnCancelX + btnW, btnY + btnH };

        DrawModernButton(hdcMem, pState->rcCancelBtn, L"Cancel", pState->isHoverCancel, false, false, pState->hFontMain);
        DrawModernButton(hdcMem, pState->rcOkBtn, L"OK", pState->isHoverOk, false, true, pState->hFontBold);

        // 6. Clean 1px Outer Border (no jagged RoundRect clipping with DWM)
        COLORREF winBorderCol = RGB(55, 60, 75);
        HPEN hPenWin = CreatePen(PS_SOLID, 1, winBorderCol);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ oldPen = SelectObject(hdcMem, hPenWin);
        HGDIOBJ oldBr = SelectObject(hdcMem, hNullB);
        Rectangle(hdcMem, 0, 0, w, h);
        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBr);
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

static bool PromptStreamText(HWND hParent, const std::wstring& title, const std::wstring& prompt, const std::wstring& defaultVal, std::wstring& outVal)
{
    static bool s_reg = false;
    const wchar_t* szClassName = L"TSCB_StreamInputModal";
    if (!s_reg)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StreamInputModalProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(20, 22, 28));
        wc.lpszClassName = szClassName;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    StreamInputModalState state;
    state.hParent = hParent;
    state.title = title;
    state.prompt = prompt;
    state.resultText = defaultVal;

    int dlgW = 500;
    int dlgH = 210;

    HMONITOR hMon = MonitorFromWindow(hParent ? hParent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT rcWork;
    if (GetMonitorInfoW(hMon, &mi)) {
        rcWork = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    }

    int x = 0, y = 0;
    if (hParent && IsWindow(hParent))
    {
        RECT rcParent;
        GetWindowRect(hParent, &rcParent);
        x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
        y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;
    }
    else
    {
        x = rcWork.left + (rcWork.right - rcWork.left - dlgW) / 2;
        y = rcWork.top + (rcWork.bottom - rcWork.top - dlgH) / 2;
    }

    // Ensure completely within work area boundaries
    if (x + dlgW > rcWork.right) x = rcWork.right - dlgW;
    if (y + dlgH > rcWork.bottom) y = rcWork.bottom - dlgH;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClassName, title.c_str(),
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, x, y, dlgW, dlgH,
        hParent, NULL, GetModuleHandleW(NULL), &state);

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    COLORREF dwmBorder = RGB(55, 60, 75);
    DwmSetWindowAttribute(hDlg, 34 /* DWMWA_BORDER_COLOR */, &dwmBorder, sizeof(dwmBorder));

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
// Data Structures for Batch Targets
// ------------------------------------------------------------------------------------------------

enum class TargetSection
{
    Engine = 0,
    Wagon = 1
};

enum class TargetFilterMode
{
    All = 0,
    EnginesOnly = 1,
    WagonsOnly = 2
};

struct BatchSmsFileTarget
{
    bool isSelected = true;
    bool isExpanded = false;
    TargetSection section = TargetSection::Engine;
    std::wstring fileName;
    std::wstring fullPath;
    std::wstring folderName;
    SmsDocument doc;
    bool isLoaded = false;
    int declaredStreams = 0;
    int parsedStreams = 0;
    bool hasMismatch = false;
    std::vector<SmsUnitUsageRef> usageRefs;
};

enum class StreamMutationMode
{
    InjectPreset = 0,
    ReplaceMatching = 1,
    BatchPathSwap = 2,
    FixCountsOnly = 3
};

struct SmsStreamMutatorState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    std::wstring basePath;
    int activeTab = 0; // 0 = Stream Presets Library, 1 = Batch Stream Mutator
    SmsStreamPresetSelectCallback onApplyCallback = nullptr;

    HWND hTitleBar = NULL;

    // Fonts
    HFONT hFontBody = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontTitle = NULL;
    HFONT hFontSmall = NULL;
    HFONT hFontBadge = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    // ----------------- TAB 0: Stream Presets Library -----------------
    // Splitter state for resizable left sidebar
    int sidebarWidth = 280;
    bool isDraggingSplitter = false;
    int dragSplitterStartX = 0;
    int dragSidebarStartW = 280;
    RECT rcSplitter = { 0 };

    // Sandboxed File/Preset Tree Explorer State
    struct TreeExplorerItem
    {
        enum ItemType { ITEM_FOLDER, ITEM_PRESET };
        ItemType type = ITEM_FOLDER;

        std::wstring name;
        std::wstring category;
        std::wstring relPath;
        std::wstring fullPath;
        int depth = 0;
        int presetCardIndex = -1;
        int presetCount = 0;

        bool isExpanded = true;
        bool isSelected = false;
        bool isCut = false;

        RECT rcRow = { 0 };
        RECT rcChevron = { 0 };
    };
    std::vector<TreeExplorerItem> treeItems;
    int hoveredTreeItemIdx = -1;
    std::set<std::wstring> collapsedCategories;
    std::wstring selectedCategory = L"AI_HORNS";

    // Sidebar toolbar button rects
    RECT rcBtnTreeNewPreset = { 0 };
    RECT rcBtnTreeNewFolder = { 0 };
    RECT rcBtnTreeToggleAll = { 0 };
    RECT rcBtnTreeSync = { 0 };
    RECT rcBtnTreeOpenFolder = { 0 };

    // Tree drag & drop state
    bool isPotentialTreeDrag = false;
    bool isTreeDragging = false;
    POINT ptTreeDragStart = { 0, 0 };
    int draggedTreeItemIdx = -1;
    int dropTargetTreeFolderIdx = -1;

    // Tree clipboard state
    enum ClipboardOp { CLIP_NONE = 0, CLIP_CUT, CLIP_COPY };
    ClipboardOp clipboardOp = CLIP_NONE;
    bool clipIsPreset = false;
    std::wstring clipSourcePath;
    std::wstring clipName;

    // Sidebar scroll
    CustomScrollBar m_sidebarScroll;
    int sidebarScrollY = 0;
    int totalSidebarH = 0;

    RECT rcBtnOpenCategoryFolder = { 0 };
    std::vector<StreamPresetCard> presetCards;
    CustomScrollBar m_vScrollTab0;
    int scrollYTab0 = 0;
    int totalHeightTab0 = 0;

    RECT rcBtnNewPresetCard = { 0 };
    RECT rcBtnExpandAllTab0 = { 0 };
    RECT rcBtnCollapseAllTab0 = { 0 };

    struct CardHit
    {
        int cardIdx = -1;
        RECT rcCard = { 0 };
        RECT rcHeader = { 0 };
        RECT rcChevron = { 0 };
        RECT rcBtnRename = { 0 };
        RECT rcBtnClone = { 0 };
        RECT rcBtnMoveUp = { 0 };
        RECT rcBtnMoveDown = { 0 };
        RECT rcBtnDelete = { 0 };
        RECT rcBtnAddStream = { 0 };
        RECT rcBtnPasteStream = { 0 };
        RECT rcBtnClearAll = { 0 };
    };
    std::vector<CardHit> cardHits;

    struct StreamChipHit
    {
        int cardIdx = -1;
        int streamIdx = -1;
        RECT rcChip = { 0 };
        RECT rcBtnRemove = { 0 };
    };
    std::vector<StreamChipHit> chipHits;
    int selectedPresetCardIdx = -1;
    int selectedPresetStreamIdx = -1;

    // ----------------- TAB 1: Batch Stream Mutator -----------------
    std::vector<BatchSmsFileTarget> targetFiles;
    CustomScrollBar m_vScrollTab1;
    int scrollYTab1 = 0;
    int totalHeightTab1 = 0;

    RECT rcBtnAddFiles = { 0 };
    RECT rcBtnClearFiles = { 0 };
    RECT rcBtnFilterAll = { 0 };
    RECT rcBtnFilterEngines = { 0 };
    RECT rcBtnFilterWagons = { 0 };
    TargetFilterMode targetFilter = TargetFilterMode::All;

    bool bSectionEnginesExpanded = true;
    bool bSectionWagonsExpanded = true;
    RECT rcSectionEnginesHdr = { 0 };
    RECT rcSectionEnginesCheck = { 0 };
    RECT rcSectionEnginesChev = { 0 };
    RECT rcSectionWagonsHdr = { 0 };
    RECT rcSectionWagonsCheck = { 0 };
    RECT rcSectionWagonsChev = { 0 };

    RECT rcGridHeader = { 0 };
    RECT rcGridMasterCheck = { 0 };
    RECT rcBtnExpandAllTab1 = { 0 };
    RECT rcBtnCollapseAllTab1 = { 0 };

    struct TargetFileHit
    {
        int fileIdx = -1;
        RECT rcHeader = { 0 };
        RECT rcCheck = { 0 };
        RECT rcChevron = { 0 };
        RECT rcBtnOpenFolder = { 0 };
    };
    std::vector<TargetFileHit> targetHits;

    // Mutation Options
    StreamMutationMode mutationMode = StreamMutationMode::InjectPreset;
    int targetSg = 5; // 5 = High, 3 = Med, 0 = Low/Cab, -1 = All
    int insertPosition = 1; // 0 = Start, 1 = End, 2 = After Specific Stream
    int insertAfterStreamIndex = 1; // 1-based stream number to insert after
    bool createBackup = true;
    bool bMouseDownOnBackupCheck = false;

    // Radio button rects
    RECT rcRadioInject = { 0 };
    RECT rcRadioReplace = { 0 };
    RECT rcRadioSwapPath = { 0 };
    RECT rcRadioFixCounts = { 0 };

    RECT rcPickerPreset = { 0 };
    RECT rcPickerSg = { 0 };
    RECT rcPickerPos = { 0 };
    RECT rcEditInsertAfter = { 0 };
    HWND hEditInsertAfter = NULL;
    bool bEditHovered = false;
    bool bEditFocused = false;
    RECT rcCheckBackup = { 0 };

    // Action Buttons
    RECT rcBtnPreviewDryRun = { 0 };
    RECT rcBtnApplyMutation = { 0 };
    RECT rcBtnClose = { 0 };

    // Edits
    std::wstring pathFindStr = L"";
    std::wstring pathReplaceStr = L"";

    POINT ptMouse = { -1, -1 };
    int hoverBtnId = -1;
};

static const wchar_t* SMS_STREAM_MUTATOR_WNDCLASS = L"TSCB_SmsStreamMutatorDialog";

// ------------------------------------------------------------------------------------------------
// Tree Explorer & Context Menu Helpers for Presets
// ------------------------------------------------------------------------------------------------
static void BuildPresetTreeExplorer(SmsStreamMutatorState* pState)
{
    pState->treeItems.clear();
    std::wstring root = DatabaseManager::GetSmsPresetsRootDir();

    std::set<std::wstring> allCategories;

    // Discover existing subdirectories on disk
    try
    {
        if (fs::exists(root))
        {
            for (const auto& entry : fs::recursive_directory_iterator(root))
            {
                if (entry.is_directory())
                {
                    try
                    {
                        std::wstring rel = fs::relative(entry.path(), root).wstring();
                        if (!rel.empty() && rel != L".")
                        {
                            allCategories.insert(rel);
                        }
                    }
                    catch (...) {}
                }
            }
        }
    }
    catch (...) {}

    // Also include categories declared in loaded preset cards
    for (auto& card : pState->presetCards)
    {
        if (card.category.empty()) card.category = L"AI_HORNS";
        allCategories.insert(card.category);
        size_t slashPos = 0;
        while ((slashPos = card.category.find(L'\\', slashPos)) != std::wstring::npos)
        {
            allCategories.insert(card.category.substr(0, slashPos));
            slashPos++;
        }
    }
    if (allCategories.empty()) allCategories.insert(L"AI_HORNS");

    std::vector<std::wstring> sortedCats(allCategories.begin(), allCategories.end());

    // Folders and categorized presets
    for (const auto& cat : sortedCats)
    {
        bool isAncestorCollapsed = false;
        size_t slashPos = 0;
        while ((slashPos = cat.find(L'\\', slashPos)) != std::wstring::npos)
        {
            std::wstring parentPrefix = cat.substr(0, slashPos);
            if (pState->collapsedCategories.count(parentPrefix) > 0)
            {
                isAncestorCollapsed = true;
                break;
            }
            slashPos++;
        }
        if (isAncestorCollapsed) continue;

        int depth = 0;
        for (wchar_t ch : cat) { if (ch == L'\\') depth++; }

        bool isExpanded = (pState->collapsedCategories.count(cat) == 0);
        bool isFolderSelected = (pState->selectedCategory == cat && pState->selectedPresetCardIdx < 0);

        std::wstring leafName = cat;
        size_t lastSlash = cat.rfind(L'\\');
        if (lastSlash != std::wstring::npos) leafName = cat.substr(lastSlash + 1);

        int catCount = 0;
        std::vector<size_t> directPresets;
        for (size_t c = 0; c < pState->presetCards.size(); ++c)
        {
            if (pState->presetCards[c].category == cat)
            {
                catCount++;
                directPresets.push_back(c);
            }
        }

        SmsStreamMutatorState::TreeExplorerItem fItem;
        fItem.type = SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER;
        fItem.name = leafName;
        fItem.category = cat;
        fItem.relPath = cat;
        fItem.fullPath = root + L"\\" + cat;
        fItem.depth = depth;
        fItem.presetCount = catCount;
        fItem.isExpanded = isExpanded;
        fItem.isSelected = isFolderSelected;
        fItem.isCut = (pState->clipboardOp == SmsStreamMutatorState::CLIP_CUT && pState->clipSourcePath == fItem.fullPath);
        pState->treeItems.push_back(fItem);

        if (isExpanded)
        {
            for (size_t cIdx : directPresets)
            {
                const auto& card = pState->presetCards[cIdx];
                SmsStreamMutatorState::TreeExplorerItem pItem;
                pItem.type = SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET;
                pItem.name = card.name;
                pItem.category = cat;
                pItem.relPath = card.relativePath;
                pItem.fullPath = card.filePath;
                pItem.depth = depth + 1;
                pItem.presetCardIndex = (int)cIdx;
                pItem.isSelected = ((int)cIdx == pState->selectedPresetCardIdx);
                pItem.isCut = (pState->clipboardOp == SmsStreamMutatorState::CLIP_CUT && pState->clipSourcePath == pItem.fullPath);
                pState->treeItems.push_back(pItem);
            }
        }
    }
}

static void ExecutePresetTreePaste(HWND hWnd, SmsStreamMutatorState* pState, const std::wstring& targetCategory)
{
    if (pState->clipboardOp == SmsStreamMutatorState::CLIP_NONE || pState->clipSourcePath.empty())
        return;

    if (targetCategory.empty())
    {
        MessageBoxW(hWnd, L"Please select a category folder. Presets cannot be placed directly in the root directory.", L"Invalid Location", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring root = DatabaseManager::GetSmsPresetsRootDir();
    std::wstring targetDir = root + L"\\" + targetCategory;
    try
    {
        fs::create_directories(targetDir);
        fs::path srcP(pState->clipSourcePath);
        if (!fs::exists(srcP)) return;

        std::wstring leaf = srcP.filename().wstring();
        fs::path destP = fs::path(targetDir) / leaf;

        if (pState->clipboardOp == SmsStreamMutatorState::CLIP_COPY)
        {
            if (fs::exists(destP))
            {
                int copyIdx = 1;
                std::wstring stem = srcP.stem().wstring();
                std::wstring ext = srcP.extension().wstring();
                while (fs::exists(destP))
                {
                    std::wstring newLeaf = stem + (copyIdx == 1 ? L" - Copy" : (L" - Copy (" + std::to_wstring(copyIdx) + L")")) + ext;
                    destP = fs::path(targetDir) / newLeaf;
                    copyIdx++;
                }
            }
            fs::copy(srcP, destP, fs::copy_options::recursive);
        }
        else if (pState->clipboardOp == SmsStreamMutatorState::CLIP_CUT)
        {
            if (fs::exists(destP) && fs::canonical(srcP) != fs::canonical(destP))
            {
                int copyIdx = 1;
                std::wstring stem = srcP.stem().wstring();
                std::wstring ext = srcP.extension().wstring();
                while (fs::exists(destP))
                {
                    std::wstring newLeaf = stem + (copyIdx == 1 ? L" - Moved" : (L" - Moved (" + std::to_wstring(copyIdx) + L")")) + ext;
                    destP = fs::path(targetDir) / newLeaf;
                    copyIdx++;
                }
            }
            if (fs::canonical(srcP) != fs::canonical(destP))
            {
                fs::rename(srcP, destP);
            }
            pState->clipboardOp = SmsStreamMutatorState::CLIP_NONE;
            pState->clipSourcePath.clear();
        }

        DatabaseManager::LoadStreamPresets(pState->presetCards);
        pState->selectedCategory = targetCategory;
        InvalidateRect(hWnd, NULL, FALSE);
    }
    catch (...) {}
}

static void HandlePresetTreeContextMenuCmd(HWND hWnd, SmsStreamMutatorState* pState, int cmd, int itemIdx)
{
    std::wstring root = DatabaseManager::GetSmsPresetsRootDir();
    if (cmd == 101) // Duplicate / Clone Preset
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            int cardIdx = pState->treeItems[itemIdx].presetCardIndex;
            if (cardIdx >= 0 && cardIdx < (int)pState->presetCards.size())
            {
                const auto& cur = pState->presetCards[cardIdx];
                std::wstring newName;
                if (PromptStreamText(hWnd, L"Clone Stream Preset", L"Enter new preset name:", cur.name + L" (Copy)", newName) && !newName.empty())
                {
                    StreamPresetCard cloned = cur;
                    cloned.id = (int)pState->presetCards.size() + 1;
                    cloned.name = newName;
                    cloned.filePath.clear();
                    pState->presetCards.push_back(cloned);
                    DatabaseManager::SaveStreamPresets(pState->presetCards);
                    DatabaseManager::LoadStreamPresets(pState->presetCards);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
    }
    else if (cmd == 102) // Cut Preset
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            pState->clipboardOp = SmsStreamMutatorState::CLIP_CUT;
            pState->clipIsPreset = true;
            pState->clipSourcePath = pState->treeItems[itemIdx].fullPath;
            pState->clipName = pState->treeItems[itemIdx].name;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 103) // Copy Preset
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            pState->clipboardOp = SmsStreamMutatorState::CLIP_COPY;
            pState->clipIsPreset = true;
            pState->clipSourcePath = pState->treeItems[itemIdx].fullPath;
            pState->clipName = pState->treeItems[itemIdx].name;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 104) // Rename Preset
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            int cardIdx = pState->treeItems[itemIdx].presetCardIndex;
            if (cardIdx >= 0 && cardIdx < (int)pState->presetCards.size())
            {
                auto& card = pState->presetCards[cardIdx];
                std::wstring newName;
                if (PromptStreamText(hWnd, L"Rename Preset", L"Enter new preset name:", card.name, newName) && !newName.empty() && newName != card.name)
                {
                    card.name = newName;
                    DatabaseManager::SaveStreamPresets(pState->presetCards);
                    DatabaseManager::LoadStreamPresets(pState->presetCards);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
    }
    else if (cmd == 105) // Delete Preset
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            int cardIdx = pState->treeItems[itemIdx].presetCardIndex;
            if (cardIdx >= 0 && cardIdx < (int)pState->presetCards.size())
            {
                const auto& cur = pState->presetCards[cardIdx];
                int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to permanently delete preset '" + cur.name + L"'?").c_str(), L"Delete Preset", MB_YESNO | MB_ICONWARNING);
                if (res == IDYES)
                {
                    DatabaseManager::DeleteStreamPresetFile(cur);
                    pState->presetCards.erase(pState->presetCards.begin() + cardIdx);
                    DatabaseManager::SaveStreamPresets(pState->presetCards);
                    DatabaseManager::LoadStreamPresets(pState->presetCards);
                    pState->selectedPresetCardIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
    }
    else if (cmd == 106) // Reveal Preset on Disk
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            std::wstring fp = pState->treeItems[itemIdx].fullPath;
            if (fs::exists(fp))
            {
                std::wstring param = L"/select,\"" + fp + L"\"";
                ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
            }
        }
    }
    else if (cmd == 201 || cmd == 301) // New Preset
    {
        std::wstring cat = pState->selectedCategory;
        if (cat.empty())
        {
            MessageBoxW(hWnd, L"Please select or create a category folder first. Presets cannot be placed directly in the root directory.", L"Invalid Location", MB_OK | MB_ICONWARNING);
            return;
        }
        std::wstring newName;
        if (PromptStreamText(hWnd, L"New Stream Preset Card", L"Enter preset name:", L"Custom Sound Streams", newName) && !newName.empty())
        {
            StreamPresetCard newCard;
            newCard.id = (int)pState->presetCards.size() + 1;
            newCard.name = newName;
            newCard.category = cat;
            pState->presetCards.push_back(newCard);
            DatabaseManager::SaveStreamPresets(pState->presetCards);
            DatabaseManager::LoadStreamPresets(pState->presetCards);
            pState->selectedCategory = cat;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 202) // New Subfolder under right-clicked folder
    {
        std::wstring parentCat = (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size()) ? pState->treeItems[itemIdx].category : L"";
        std::wstring newFolder;
        std::wstring promptMsg = parentCat.empty() ? L"Enter new folder name:" : (L"Enter new subfolder name under '" + parentCat + L"':");
        if (PromptStreamText(hWnd, L"New Subfolder", promptMsg.c_str(), L"", newFolder) && !newFolder.empty())
        {
            std::replace(newFolder.begin(), newFolder.end(), L'/', L'\\');
            std::wstring fullRel = parentCat.empty() ? newFolder : (parentCat + L"\\" + newFolder);
            std::wstring folderDir = root + L"\\" + fullRel;
            try { fs::create_directories(folderDir); } catch (...) {}
            DatabaseManager::LoadStreamPresets(pState->presetCards);
            pState->selectedCategory = fullRel;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 302) // New Folder directly under SMS_Stream_Presets root
    {
        std::wstring newFolder;
        std::wstring promptMsg = L"Enter new folder name:\n(To create a subfolder, use Path\\Subpath or right-click a folder -> 'New Subfolder...')";
        if (PromptStreamText(hWnd, L"New Category Folder", promptMsg.c_str(), L"", newFolder) && !newFolder.empty())
        {
            std::replace(newFolder.begin(), newFolder.end(), L'/', L'\\');
            std::wstring folderDir = root + L"\\" + newFolder;
            try { fs::create_directories(folderDir); } catch (...) {}
            DatabaseManager::LoadStreamPresets(pState->presetCards);
            pState->selectedCategory = newFolder;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 203) // Cut Folder
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            pState->clipboardOp = SmsStreamMutatorState::CLIP_CUT;
            pState->clipIsPreset = false;
            pState->clipSourcePath = pState->treeItems[itemIdx].fullPath;
            pState->clipName = pState->treeItems[itemIdx].name;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 204) // Copy Folder
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            pState->clipboardOp = SmsStreamMutatorState::CLIP_COPY;
            pState->clipIsPreset = false;
            pState->clipSourcePath = pState->treeItems[itemIdx].fullPath;
            pState->clipName = pState->treeItems[itemIdx].name;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    else if (cmd == 205 || cmd == 303) // Paste
    {
        std::wstring targetCat = (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size() && pState->treeItems[itemIdx].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
            ? pState->treeItems[itemIdx].category : pState->selectedCategory;
        ExecutePresetTreePaste(hWnd, pState, targetCat);
    }
    else if (cmd == 206) // Rename Folder
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            const auto& item = pState->treeItems[itemIdx];
            std::wstring newName;
            if (PromptStreamText(hWnd, L"Rename Folder", L"Enter new folder name:", item.name, newName) && !newName.empty() && newName != item.name)
            {
                try
                {
                    fs::path oldP(item.fullPath);
                    fs::path newP = oldP.parent_path() / newName;
                    if (fs::exists(oldP))
                    {
                        fs::rename(oldP, newP);
                    }
                }
                catch (...) {}
                DatabaseManager::LoadStreamPresets(pState->presetCards);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    else if (cmd == 207) // Delete Folder
    {
        if (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size())
        {
            const auto& item = pState->treeItems[itemIdx];
            int res = ShowModernMessageBox(hWnd, (L"Are you sure you want to delete folder '" + item.name + L"' and all presets inside it?").c_str(), L"Delete Folder", MB_YESNO | MB_ICONWARNING);
            if (res == IDYES)
            {
                try
                {
                    fs::path p(item.fullPath);
                    if (fs::exists(p)) fs::remove_all(p);
                }
                catch (...) {}
                DatabaseManager::LoadStreamPresets(pState->presetCards);
                pState->selectedCategory.clear();
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    else if (cmd == 208 || cmd == 307) // Reveal Folder in Explorer
    {
        std::wstring p = (itemIdx >= 0 && itemIdx < (int)pState->treeItems.size()) ? pState->treeItems[itemIdx].fullPath : root;
        try { if (!fs::exists(p)) fs::create_directories(p); } catch (...) {}
        ShellExecuteW(NULL, L"explore", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else if (cmd == 304) // Expand All
    {
        pState->collapsedCategories.clear();
        InvalidateRect(hWnd, NULL, FALSE);
    }
    else if (cmd == 305) // Collapse All
    {
        for (const auto& it : pState->treeItems)
        {
            if (it.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                pState->collapsedCategories.insert(it.category);
        }
        InvalidateRect(hWnd, NULL, FALSE);
    }
    else if (cmd == 306) // Rescan
    {
        DatabaseManager::LoadStreamPresets(pState->presetCards);
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

// ------------------------------------------------------------------------------------------------
// Persistence for Stream Presets
// ------------------------------------------------------------------------------------------------
static void LoadStreamPresetsFromFile(SmsStreamMutatorState* pState)
{
    if (pState)
    {
        DatabaseManager::LoadStreamPresets(pState->presetCards);
    }
}

static void SaveStreamPresetsToFile(SmsStreamMutatorState* pState)
{
    if (pState)
    {
        DatabaseManager::SaveStreamPresets(pState->presetCards);
    }
}

// ------------------------------------------------------------------------------------------------
// Loading Targets
// ------------------------------------------------------------------------------------------------
static std::wstring FormatSmsUsageSummary(const std::vector<SmsUnitUsageRef>& refs)
{
    if (refs.empty())
    {
        return L"\x21AA Source: Manually imported sound file";
    }

    // Group references by consistName preserving encounter order
    std::vector<std::wstring> consistOrder;
    std::unordered_map<std::wstring, std::vector<SmsUnitUsageRef>> grouped;
    std::wstring primarySlot = L"";

    for (const auto& r : refs)
    {
        if (grouped.find(r.consistName) == grouped.end())
        {
            consistOrder.push_back(r.consistName);
        }
        grouped[r.consistName].push_back(r);
        if (primarySlot.empty() && !r.soundSlot.empty())
        {
            primarySlot = r.soundSlot;
        }
    }

    if (consistOrder.size() == 1)
    {
        const std::wstring& cName = consistOrder[0];
        const auto& cRefs = grouped[cName];

        std::wstring unitSummary = L"";
        if (cRefs.size() == 1)
        {
            unitSummary = L"Unit: #" + std::to_wstring(cRefs[0].unitIndex) + L" " + cRefs[0].unitUid;
        }
        else if (cRefs.size() <= 3)
        {
            unitSummary = L"Units: ";
            for (size_t i = 0; i < cRefs.size(); ++i)
            {
                if (i > 0) unitSummary += L", ";
                unitSummary += L"#" + std::to_wstring(cRefs[i].unitIndex) + L" " + cRefs[i].unitUid;
            }
        }
        else
        {
            int minIdx = cRefs.front().unitIndex;
            int maxIdx = cRefs.back().unitIndex;
            std::wstring baseUid = cRefs[0].unitUid;
            bool sameUid = true;
            for (const auto& r : cRefs)
            {
                if (r.unitUid != baseUid) { sameUid = false; break; }
            }
            if (sameUid)
            {
                unitSummary = L"Units: #" + std::to_wstring(minIdx) + L" to #" + std::to_wstring(maxIdx) + L" " + baseUid + L" (" + std::to_wstring(cRefs.size()) + L" cars)";
            }
            else
            {
                unitSummary = L"Units: " + std::to_wstring(cRefs.size()) + L" cars (#" + std::to_wstring(minIdx) + L"..." + std::to_wstring(maxIdx) + L")";
            }
        }

        std::wstring res = L"\x21AA Consist: " + (cName.empty() ? L"(Active Consist)" : cName) + L"  |  " + unitSummary;
        return res;
    }
    else
    {
        std::wstring res = L"\x21AA Consists: ";
        for (size_t i = 0; i < consistOrder.size(); ++i)
        {
            if (i > 0) res += L", ";
            const auto& cName = consistOrder[i];
            const auto& cRefs = grouped[cName];
            res += (cName.empty() ? L"(Consist)" : cName) + L" (" + std::to_wstring(cRefs.size()) + (cRefs.size() == 1 ? L" unit" : L" units") + L")";
            if (i >= 2 && consistOrder.size() > 3)
            {
                res += L" +" + std::to_wstring(consistOrder.size() - 3) + L" more";
                break;
            }
        }
        return res;
    }
}

static void AddSmsFileTarget(SmsStreamMutatorState* pState, const std::wstring& path, TargetSection section = TargetSection::Wagon, const SmsUnitUsageRef* pUsage = nullptr)
{
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    for (auto& t : pState->targetFiles)
    {
        if (_wcsicmp(t.fullPath.c_str(), path.c_str()) == 0)
        {
            if (section == TargetSection::Engine && t.section != TargetSection::Engine)
            {
                t.section = TargetSection::Engine;
            }
            if (pUsage)
            {
                bool exists = false;
                for (const auto& u : t.usageRefs)
                {
                    if (u.consistName == pUsage->consistName && u.unitIndex == pUsage->unitIndex && u.unitUid == pUsage->unitUid && u.soundSlot == pUsage->soundSlot)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    t.usageRefs.push_back(*pUsage);
                }
            }
            return;
        }
    }

    BatchSmsFileTarget tgt;
    tgt.isSelected = true;
    tgt.isExpanded = false;
    tgt.section = section;
    tgt.fullPath = path;
    if (pUsage)
    {
        tgt.usageRefs.push_back(*pUsage);
    }

    size_t ls = path.find_last_of(L"\\/");
    tgt.fileName = (ls != std::wstring::npos) ? path.substr(ls + 1) : path;
    if (ls != std::wstring::npos)
    {
        size_t prevLs = path.substr(0, ls).find_last_of(L"\\/");
        tgt.folderName = (prevLs != std::wstring::npos) ? path.substr(prevLs + 1) : path.substr(0, ls);
    }

    if (SmsParser_LoadFromFile(path, pState->basePath, tgt.doc))
    {
        tgt.isLoaded = true;
        if (tgt.doc.activeGroupIndex >= 0 && tgt.doc.activeGroupIndex < (int)tgt.doc.groups.size())
        {
            auto& grp = tgt.doc.groups[tgt.doc.activeGroupIndex];
            tgt.declaredStreams = grp.declaredStreams;
            tgt.parsedStreams = (int)grp.streams.size();
            tgt.hasMismatch = (grp.declaredStreams != tgt.parsedStreams);
        }
    }

    pState->targetFiles.push_back(tgt);
}

// ------------------------------------------------------------------------------------------------
// Modern Fluent Subclass for Stream Index Edit Box (matches Consist Editor Train Details)
// ------------------------------------------------------------------------------------------------
static LRESULT CALLBACK StreamMutatorEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    SmsStreamMutatorState* pState = (SmsStreamMutatorState*)dwRefData;

    switch (uMsg)
    {
    case WM_SETFOCUS:
    {
        if (pState) pState->bEditFocused = true;
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_KILLFOCUS:
    {
        if (pState) pState->bEditFocused = false;
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (pState && !pState->bEditHovered)
        {
            pState->bEditHovered = true;
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (pState && pState->bEditHovered)
        {
            pState->bEditHovered = false;
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int h = rc.bottom - rc.top;
        int w = rc.right - rc.left;
        int vPad = (h > 18) ? (h - 18) / 2 : 3;
        RECT rcFmt = { 2, vPad, w - 2, h - vPad };
        SendMessageW(hWnd, EM_SETRECT, 0, (LPARAM)&rcFmt);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
            return 0;
        break;

    case WM_CHAR:
    {
        if (wParam == VK_RETURN)
            return 0;

        // Allow digits (0-9) and standard control keys (backspace, copy, paste, etc.)
        if (wParam >= 32 && (wParam < L'0' || wParam > L'9'))
            return 0;

        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_PASTE:
    {
        if (OpenClipboard(hWnd))
        {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData)
            {
                wchar_t* pClip = (wchar_t*)GlobalLock(hData);
                if (pClip)
                {
                    std::wstring filtered;
                    for (size_t i = 0; pClip[i] != L'\0'; ++i)
                    {
                        if (pClip[i] >= L'0' && pClip[i] <= L'9')
                            filtered += pClip[i];
                    }
                    GlobalUnlock(hData);
                    CloseClipboard();
                    if (!filtered.empty())
                    {
                        SendMessageW(hWnd, EM_REPLACESEL, TRUE, (LPARAM)filtered.c_str());
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
            }
            CloseClipboard();
        }
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_KEYUP:
    case WM_LBUTTONUP:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right;
            int height = rc.bottom;

            if (width > 0 && height > 0)
            {
                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

                bool focused = pState ? pState->bEditFocused : (GetFocus() == hWnd);
                bool hovered = pState ? pState->bEditHovered : false;

                // 1. Fill parent panel background to anti-alias rounded corners smoothly (rcRightPanel = RGB(24, 22, 25))
                HBRUSH hbrParent = CreateSolidBrush(RGB(24, 22, 25));
                FillRect(hMemDC, &rc, hbrParent);
                DeleteObject(hbrParent);

                // 2. Select modern background and border colors (Windows 11 Fluent style)
                COLORREF clrBg = focused ? RGB(30, 30, 30) : (hovered ? RGB(45, 45, 45) : RGB(38, 38, 38));
                COLORREF clrBorder = focused ? RGB(70, 70, 70) : (hovered ? RGB(85, 95, 110) : RGB(50, 50, 50));

                EnsureGdiPlus();
                Gdiplus::Graphics g(hMemDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

                Gdiplus::RectF rcBox(0.5f, 0.5f, (Gdiplus::REAL)width - 1.0f, (Gdiplus::REAL)height - 1.0f);
                Gdiplus::GraphicsPath boxPath;
                AddRoundedRectPath(boxPath, rcBox, 4.0f);

                Gdiplus::SolidBrush brBg(Gdiplus::Color(255, GetRValue(clrBg), GetGValue(clrBg), GetBValue(clrBg)));
                g.FillPath(&brBg, &boxPath);

                // 3. Render edit control text onto memory DC (clipped cleanly within rounded boundaries)
                RECT rcClip = { rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 2 };
                HRGN hRgnClip = CreateRectRgnIndirect(&rcClip);
                SelectClipRgn(hMemDC, hRgnClip);
                DefSubclassProc(hWnd, WM_PRINTCLIENT, (WPARAM)hMemDC, PRF_CLIENT);
                SelectClipRgn(hMemDC, NULL);
                DeleteObject(hRgnClip);

                // 4. Draw anti-aliased border around edit box
                Gdiplus::Pen penBorder(Gdiplus::Color(255, GetRValue(clrBorder), GetGValue(clrBorder), GetBValue(clrBorder)), 1.0f);
                g.DrawPath(&penBorder, &boxPath);

                // 5. Draw focused accent light blue bottom line (Windows 11 Fluent edit highlight with 45-degree chamfered corners)
                bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
                if (focused && !isReadOnly)
                {
                    Gdiplus::GraphicsPath accentPath;
                    Gdiplus::REAL r = 4.0f;
                    if (r * 2.0f > (Gdiplus::REAL)width) r = (Gdiplus::REAL)width / 2.0f;
                    if (r * 2.0f > (Gdiplus::REAL)height) r = (Gdiplus::REAL)height / 2.0f;
                    Gdiplus::REAL d = r * 2.0f;
                    Gdiplus::REAL accentY = (Gdiplus::REAL)height - 1.5f;
                    Gdiplus::REAL arcY = accentY - d;

                    accentPath.AddArc(0.5f, arcY, d, d, 135.0f, -45.0f);
                    accentPath.AddLine(0.5f + r, accentY, (Gdiplus::REAL)width - 0.5f - r, accentY);
                    accentPath.AddArc((Gdiplus::REAL)width - 0.5f - d, arcY, d, d, 90.0f, -45.0f);

                    Gdiplus::Pen penAccent(Gdiplus::Color(255, 96, 205, 255), 2.0f);
                    penAccent.SetStartCap(Gdiplus::LineCapFlat);
                    penAccent.SetEndCap(Gdiplus::LineCapFlat);
                    g.DrawPath(&penAccent, &accentPath);
                }

                BitBlt(hdc, 0, 0, width, height, hMemDC, 0, 0, SRCCOPY);
                SelectObject(hMemDC, hOldBmp);
                DeleteObject(hMemBmp);
                DeleteDC(hMemDC);
            }
            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, StreamMutatorEditSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ------------------------------------------------------------------------------------------------
// Window Procedure
// ------------------------------------------------------------------------------------------------
static LRESULT CALLBACK SmsStreamMutatorProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SmsStreamMutatorState* pState = (SmsStreamMutatorState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (SmsStreamMutatorState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_CREATE:
    {
        pState->hFontBody = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontBold = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontTitle = CreateCustomFont(13, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontSmall = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontBadge = CreateCustomFont(8, FW_BOLD, L"Segoe UI");
        pState->hFontIcon = CreateCustomFont(12, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon) pState->hFontIcon = CreateCustomFont(12, FW_NORMAL, L"Segoe MDL2 Assets");
        pState->hFontIconSmall = CreateCustomFont(9, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIconSmall) pState->hFontIconSmall = CreateCustomFont(9, FW_NORMAL, L"Segoe MDL2 Assets");

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right > 0 ? rcClient.right : 1100;

        std::vector<TitleBarTabItem> soundTabs = {
            { L"\xE8D6", L"Stream Presets Library" },
            { L"\xE916", L"Batch Stream Mutator" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, w, 66,
            1001,
            L"SMS Stream Mutator & Presets Library",
            soundTabs
        );
        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetNeutralDarkTheme(pState->hTitleBar, FALSE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
            SendMessage(pState->hTitleBar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, 66));
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

        LoadStreamPresetsFromFile(pState);
        if (pState->selectedCategory.empty())
        {
            if (!pState->presetCards.empty())
            {
                pState->selectedCategory = pState->presetCards[0].category.empty() ? L"AI_HORNS" : pState->presetCards[0].category;
            }
            else
            {
                pState->selectedCategory = L"AI_HORNS";
            }
        }

        pState->hEditInsertAfter = CreateWindowExW(
            0, L"EDIT", L"1",
            WS_CHILD | ES_NUMBER | ES_CENTER | ES_MULTILINE | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hWnd, (HMENU)2001, GetModuleHandleW(NULL), NULL
        );
        if (pState->hEditInsertAfter)
        {
            SendMessageW(pState->hEditInsertAfter, WM_SETFONT, (WPARAM)pState->hFontBody, TRUE);
            SetWindowSubclass(pState->hEditInsertAfter, StreamMutatorEditSubclassProc, 2001, (DWORD_PTR)pState);
            RECT rcFmt = { 2, 3, 58, 23 };
            SendMessageW(pState->hEditInsertAfter, EM_SETRECT, 0, (LPARAM)&rcFmt);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 2001 && HIWORD(wParam) == EN_CHANGE && pState && pState->hEditInsertAfter)
        {
            wchar_t buf[32] = { 0 };
            GetWindowTextW(pState->hEditInsertAfter, buf, 32);
            int val = _wtoi(buf);
            if (val < 0) val = 0;
            pState->insertAfterStreamIndex = val;
        }
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HWND hCtl = (HWND)lParam;
        if (pState && hCtl == pState->hEditInsertAfter)
        {
            HDC hdcEdit = (HDC)wParam;
            bool focused = pState->bEditFocused || (GetFocus() == hCtl);
            bool hovered = pState->bEditHovered;
            COLORREF clrBg = focused ? RGB(30, 30, 30) : (hovered ? RGB(45, 45, 45) : RGB(38, 38, 38));
            SetBkMode(hdcEdit, OPAQUE);
            SetBkColor(hdcEdit, clrBg);
            SetTextColor(hdcEdit, RGB(240, 240, 245));

            static HBRUSH s_hbrEditFocused = CreateSolidBrush(RGB(30, 30, 30));
            static HBRUSH s_hbrEditHovered = CreateSolidBrush(RGB(45, 45, 45));
            static HBRUSH s_hbrEditNormal  = CreateSolidBrush(RGB(38, 38, 38));

            if (focused) return (INT_PTR)s_hbrEditFocused;
            if (hovered) return (INT_PTR)s_hbrEditHovered;
            return (INT_PTR)s_hbrEditNormal;
        }
        break;
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        int newTab = (int)wParam;
        pState->activeTab = newTab;
        if (pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            CustomTitleBar_SetActiveTab(pState->hTitleBar, newTab);
        }
        if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter))
        {
            ShowWindow(pState->hEditInsertAfter, SW_HIDE);
            pState->bEditFocused = false;
            pState->bEditHovered = false;
        }
        InvalidateRect(hWnd, NULL, TRUE);
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
        return HTCLIENT;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (pState && pState->hTitleBar)
        {
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);

            if (pState->activeTab == 0)
            {
                if (pt.x < pState->sidebarWidth)
                {
                    pState->m_sidebarScroll.OnMouseWheel(delta, 50, hWnd);
                    pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
                }
                else
                {
                    pState->m_vScrollTab0.OnMouseWheel(delta, 50, hWnd);
                    pState->scrollYTab0 = pState->m_vScrollTab0.GetPos();
                }
            }
            else
            {
                pState->m_vScrollTab1.OnMouseWheel(delta, 50, hWnd);
                pState->scrollYTab1 = pState->m_vScrollTab1.GetPos();
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
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
            if (pState->isTreeDragging)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                return TRUE;
            }
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pState->ptMouse = pt;

            if (pState->activeTab == 0)
            {
                // 1. Check splitter dragging
                if (pState->isDraggingSplitter)
                {
                    int deltaX = pt.x - pState->dragSplitterStartX;
                    int newW = pState->dragSidebarStartW + deltaX;
                    RECT rcClient;
                    GetClientRect(hWnd, &rcClient);
                    int minW = 200;
                    int maxW = (std::max<int>)(280, (int)rcClient.right - 460);
                    if (newW < minW) newW = minW;
                    if (newW > maxW) newW = maxW;

                    if (newW != pState->sidebarWidth)
                    {
                        pState->sidebarWidth = newW;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }

                // 2. Potential tree drag initiation (threshold 4px)
                if (pState->isPotentialTreeDrag && !pState->isTreeDragging)
                {
                    if (abs(pt.x - pState->ptTreeDragStart.x) > 4 || abs(pt.y - pState->ptTreeDragStart.y) > 4)
                    {
                        pState->isTreeDragging = true;
                        SetCursor(LoadCursor(NULL, IDC_SIZEALL));

                        std::vector<DragGhostItem> ghostItems;
                        if (pState->draggedTreeItemIdx >= 0 && pState->draggedTreeItemIdx < (int)pState->treeItems.size())
                        {
                            const auto& item = pState->treeItems[pState->draggedTreeItemIdx];
                            DragGhostItem gItem;
                            gItem.type = GhostItemType::SoundFile;
                            gItem.name = item.name;
                            gItem.subtitle = (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                                ? (std::to_wstring(item.presetCount) + L" Presets")
                                : (item.presetCardIndex >= 0 && item.presetCardIndex < (int)pState->presetCards.size()
                                    ? std::to_wstring(pState->presetCards[item.presetCardIndex].streams.size()) + L" Streams"
                                    : L"Preset");
                            gItem.isEngine = true;
                            ghostItems.push_back(gItem);
                        }

                        POINT ptScreen = pt;
                        ClientToScreen(hWnd, &ptScreen);
                        FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
                    }
                }

                // 3. Active tree dragging motion & drop targeting
                if (pState->isTreeDragging)
                {
                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                    POINT ptScreen = pt;
                    ClientToScreen(hWnd, &ptScreen);

                    int targetFolder = -1;
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER &&
                            PtInRect(&pState->treeItems[i].rcRow, pt))
                        {
                            targetFolder = (int)i;
                            break;
                        }
                    }
                    pState->dropTargetTreeFolderIdx = targetFolder;

                    FluentDragGhost::Move(ptScreen, targetFolder >= 0, L"Move into Folder");
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // 4. Hover detection for tree rows
                int newHover = -1;
                if (pt.x < pState->sidebarWidth && pt.y > 112)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (PtInRect(&pState->treeItems[i].rcRow, pt))
                        {
                            newHover = (int)i;
                            break;
                        }
                    }
                }
                if (newHover != pState->hoveredTreeItemIdx)
                {
                    pState->hoveredTreeItemIdx = newHover;
                    InvalidateRect(hWnd, NULL, FALSE);
                }

                bool sbCh1 = pState->m_sidebarScroll.OnMouseMove(pt, hWnd);
                if (sbCh1) pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();

                bool sbCh0 = pState->m_vScrollTab0.OnMouseMove(pt, hWnd);
                if (sbCh0) pState->scrollYTab0 = pState->m_vScrollTab0.GetPos();

                if (sbCh1 || sbCh0) InvalidateRect(hWnd, NULL, FALSE);
            }
            else
            {
                bool sbCh = pState->m_vScrollTab1.OnMouseMove(pt, hWnd);
                if (sbCh)
                {
                    pState->scrollYTab1 = pState->m_vScrollTab1.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pState->activeTab == 0)
            {
                // 1. Splitter initiation
                if (PtInRect(&pState->rcSplitter, pt))
                {
                    pState->isDraggingSplitter = true;
                    pState->dragSplitterStartX = pt.x;
                    pState->dragSidebarStartW = pState->sidebarWidth;
                    SetCapture(hWnd);
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    InvalidateRect(hWnd, &pState->rcSplitter, FALSE);
                    return 0;
                }

                // 2. Sidebar scrollbar
                if (pState->m_sidebarScroll.OnLButtonDown(pt, hWnd))
                {
                    pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // 3. Sidebar toolbar buttons
                if (PtInRect(&pState->rcBtnTreeNewPreset, pt))
                {
                    HandlePresetTreeContextMenuCmd(hWnd, pState, 201, -1);
                    return 0;
                }
                if (PtInRect(&pState->rcBtnTreeNewFolder, pt))
                {
                    HandlePresetTreeContextMenuCmd(hWnd, pState, 302, -1);
                    return 0;
                }
                if (PtInRect(&pState->rcBtnTreeToggleAll, pt))
                {
                    if (pState->collapsedCategories.empty())
                    {
                        for (const auto& item : pState->treeItems)
                        {
                            if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                                pState->collapsedCategories.insert(item.category);
                        }
                    }
                    else
                    {
                        pState->collapsedCategories.clear();
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                if (PtInRect(&pState->rcBtnTreeSync, pt))
                {
                    HandlePresetTreeContextMenuCmd(hWnd, pState, 306, -1);
                    return 0;
                }
                if (PtInRect(&pState->rcBtnTreeOpenFolder, pt))
                {
                    HandlePresetTreeContextMenuCmd(hWnd, pState, 307, -1);
                    return 0;
                }

                // 4. Tree item chevron & row click
                if (pt.x < pState->sidebarWidth && pt.y > 112)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        auto& item = pState->treeItems[i];
                        if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER && PtInRect(&item.rcChevron, pt))
                        {
                            if (pState->collapsedCategories.count(item.category) > 0)
                                pState->collapsedCategories.erase(item.category);
                            else
                                pState->collapsedCategories.insert(item.category);
                            InvalidateRect(hWnd, NULL, FALSE);
                            return 0;
                        }
                        else if (PtInRect(&item.rcRow, pt))
                        {
                            if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                            {
                                pState->selectedCategory = item.category;
                                pState->selectedPresetCardIdx = -1;
                            }
                            else if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET)
                            {
                                pState->selectedPresetCardIdx = item.presetCardIndex;
                                pState->selectedCategory = item.category;
                                if (item.presetCardIndex >= 0 && item.presetCardIndex < (int)pState->presetCards.size())
                                {
                                    pState->presetCards[item.presetCardIndex].isCollapsed = false;
                                }
                            }
                            pState->isPotentialTreeDrag = true;
                            pState->draggedTreeItemIdx = (int)i;
                            pState->ptTreeDragStart = pt;
                            InvalidateRect(hWnd, NULL, FALSE);
                            return 0;
                        }
                    }
                }

                // 5. Right canvas scrollbar
                if (pState->m_vScrollTab0.OnLButtonDown(pt, hWnd))
                {
                    pState->scrollYTab0 = pState->m_vScrollTab0.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            else
            {
                pState->bMouseDownOnBackupCheck = PtInRect(&pState->rcCheckBackup, pt);

                if (pState->m_vScrollTab1.OnLButtonDown(pt, hWnd))
                {
                    pState->scrollYTab1 = pState->m_vScrollTab1.GetPos();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pState->activeTab == 0)
            {
                pState->m_sidebarScroll.OnLButtonUp(pt, hWnd);
                pState->m_vScrollTab0.OnLButtonUp(pt, hWnd);

                if (pState->isDraggingSplitter)
                {
                    pState->isDraggingSplitter = false;
                    if (GetCapture() == hWnd) ReleaseCapture();
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                if (pState->isTreeDragging)
                {
                    pState->isTreeDragging = false;
                    pState->isPotentialTreeDrag = false;
                    FluentDragGhost::Hide();
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    if (GetCapture() == hWnd) ReleaseCapture();

                    int srcIdx = pState->draggedTreeItemIdx;
                    int dstFolderIdx = pState->dropTargetTreeFolderIdx;

                    if (srcIdx >= 0 && srcIdx < (int)pState->treeItems.size() &&
                        dstFolderIdx >= 0 && dstFolderIdx < (int)pState->treeItems.size())
                    {
                        const auto& srcItem = pState->treeItems[srcIdx];
                        const auto& dstItem = pState->treeItems[dstFolderIdx];

                        if (dstItem.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                        {
                            std::wstring root = DatabaseManager::GetSmsPresetsRootDir();
                            std::wstring targetCat = dstItem.category;
                            std::wstring targetDir = root + (targetCat.empty() ? L"" : (L"\\" + targetCat));

                            try
                            {
                                fs::path srcP(srcItem.fullPath);
                                if (fs::exists(srcP))
                                {
                                    std::wstring leaf = srcP.filename().wstring();
                                    fs::path destP = fs::path(targetDir) / leaf;

                                    if (srcItem.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET)
                                    {
                                        if (srcP.parent_path() != fs::path(targetDir))
                                        {
                                            if (fs::exists(destP))
                                            {
                                                int copyIdx = 1;
                                                std::wstring stem = srcP.stem().wstring();
                                                std::wstring ext = srcP.extension().wstring();
                                                while (fs::exists(destP))
                                                {
                                                    std::wstring newLeaf = stem + L"_" + std::to_wstring(copyIdx) + ext;
                                                    destP = fs::path(targetDir) / newLeaf;
                                                    copyIdx++;
                                                }
                                            }
                                            fs::rename(srcP, destP);
                                            DatabaseManager::LoadStreamPresets(pState->presetCards);
                                            pState->selectedCategory = targetCat;
                                            InvalidateRect(hWnd, NULL, FALSE);
                                        }
                                    }
                                    else if (srcItem.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                                    {
                                        if (targetCat.find(srcItem.category) != 0 && srcP != fs::path(targetDir))
                                        {
                                            if (fs::exists(destP))
                                            {
                                                int copyIdx = 1;
                                                while (fs::exists(destP))
                                                {
                                                    std::wstring newLeaf = leaf + L"_" + std::to_wstring(copyIdx);
                                                    destP = fs::path(targetDir) / newLeaf;
                                                    copyIdx++;
                                                }
                                            }
                                            fs::rename(srcP, destP);
                                            DatabaseManager::LoadStreamPresets(pState->presetCards);
                                            pState->selectedCategory = targetCat + L"\\" + leaf;
                                            InvalidateRect(hWnd, NULL, FALSE);
                                        }
                                    }
                                }
                            }
                            catch (...) {}
                        }
                    }

                    pState->draggedTreeItemIdx = -1;
                    pState->dropTargetTreeFolderIdx = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                pState->isPotentialTreeDrag = false;
            }
            else
            {
                pState->m_vScrollTab1.OnLButtonUp(pt, hWnd);
            }

            // TAB 0 Controls
            if (pState->activeTab == 0)
            {
                if (PtInRect(&pState->rcBtnNewPresetCard, pt))
                {
                    std::wstring cat = pState->selectedCategory;
                    if (cat.empty())
                    {
                        MessageBoxW(hWnd, L"Please select or create a category folder first. Presets cannot be placed directly in the root directory.", L"Invalid Location", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    std::wstring newName;
                    if (PromptStreamText(hWnd, L"New Stream Preset Card", L"Enter preset group name:", L"Custom Sound Streams", newName) && !newName.empty())
                    {
                        StreamPresetCard newCard;
                        newCard.id = (int)pState->presetCards.size() + 1;
                        newCard.name = newName;
                        newCard.category = cat;
                        pState->presetCards.push_back(newCard);
                        DatabaseManager::SaveStreamPresets(pState->presetCards);
                        DatabaseManager::LoadStreamPresets(pState->presetCards);
                        pState->selectedCategory = cat;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnOpenCategoryFolder, pt))
                {
                    std::wstring root = DatabaseManager::GetSmsPresetsRootDir();
                    std::wstring target = pState->selectedCategory.empty() ? root : (root + L"\\" + pState->selectedCategory);
                    try { if (!fs::exists(target)) fs::create_directories(target); } catch (...) {}
                    ShellExecuteW(NULL, L"explore", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnExpandAllTab0, pt))
                {
                    for (auto& c : pState->presetCards) c.isCollapsed = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnCollapseAllTab0, pt))
                {
                    for (auto& c : pState->presetCards) c.isCollapsed = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Check card hits
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
                    else if (PtInRect(&ch.rcBtnRename, pt))
                    {
                        std::wstring newName;
                        if (PromptStreamText(hWnd, L"Rename Preset Card", L"Enter new name:", card.name, newName) && !newName.empty())
                        {
                            card.name = newName;
                            SaveStreamPresetsToFile(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnClone, pt))
                    {
                        StreamPresetCard cloned = card;
                        cloned.name += L" (Copy)";
                        pState->presetCards.insert(pState->presetCards.begin() + cIdx + 1, cloned);
                        SaveStreamPresetsToFile(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnDelete, pt))
                    {
                        if (ShowModernMessageBox(hWnd, (L"Delete preset card '" + card.name + L"'?").c_str(), L"Delete Preset", MB_YESNO | MB_ICONQUESTION) == IDYES)
                        {
                            DatabaseManager::DeleteStreamPresetFile(card);
                            pState->presetCards.erase(pState->presetCards.begin() + cIdx);
                            DatabaseManager::SaveStreamPresets(pState->presetCards);
                            DatabaseManager::LoadStreamPresets(pState->presetCards);
                            pState->selectedPresetCardIdx = -1;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnMoveUp, pt) && cIdx > 0)
                    {
                        std::swap(pState->presetCards[cIdx], pState->presetCards[cIdx - 1]);
                        SaveStreamPresetsToFile(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnMoveDown, pt) && cIdx + 1 < (int)pState->presetCards.size())
                    {
                        std::swap(pState->presetCards[cIdx], pState->presetCards[cIdx + 1]);
                        SaveStreamPresetsToFile(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnAddStream, pt))
                    {
                        StreamPresetItem newItem;
                        newItem.name = L"New Stream " + std::to_wstring(card.streams.size() + 1);
                        newItem.category = L"Custom";
                        newItem.priority = 6;
                        newItem.volume = 1.0f;
                        newItem.primaryWav = L"sound.wav";
                        card.streams.push_back(newItem);
                        SaveStreamPresetsToFile(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnPasteStream, pt))
                    {
                        const auto& clip = SmsClipboard_Get();
                        if (!clip.streams.empty())
                        {
                            for (const auto& cItem : clip.streams)
                            {
                                StreamPresetItem newItem;
                                newItem.name = cItem.stream.friendlyTitle;
                                newItem.category = L"Custom";
                                newItem.priority = cItem.stream.priority;
                                newItem.volume = cItem.stream.volume;
                                newItem.isStereo = cItem.stream.isStereo;
                                newItem.triggers = cItem.stream.triggers;
                                newItem.curves = cItem.stream.curves;

                                for (size_t t = 0; t < newItem.triggers.size(); ++t)
                                {
                                    for (size_t f = 0; f < newItem.triggers[t].files.size(); ++f)
                                    {
                                        if (t < cItem.resolvedWavPaths.size() && f < cItem.resolvedWavPaths[t].size())
                                        {
                                            newItem.triggers[t].files[f].resolvedDiskPath = cItem.resolvedWavPaths[t][f];
                                            newItem.triggers[t].files[f].existsOnDisk = (!cItem.resolvedWavPaths[t][f].empty() && GetFileAttributesW(cItem.resolvedWavPaths[t][f].c_str()) != INVALID_FILE_ATTRIBUTES);
                                        }
                                        else if (newItem.triggers[t].files[f].resolvedDiskPath.empty())
                                        {
                                            newItem.triggers[t].files[f].resolvedDiskPath = SmsParser_ResolveWavPath(clip.sourceSmsPath, newItem.triggers[t].files[f].fileName, pState->basePath);
                                            newItem.triggers[t].files[f].existsOnDisk = (!newItem.triggers[t].files[f].resolvedDiskPath.empty() && GetFileAttributesW(newItem.triggers[t].files[f].resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                                        }
                                    }
                                }

                                newItem.primaryWav.clear();
                                for (const auto& tr : newItem.triggers)
                                {
                                    for (const auto& f : tr.files)
                                    {
                                        if (!f.fileName.empty())
                                        {
                                            newItem.primaryWav = f.fileName;
                                            break;
                                        }
                                    }
                                    if (!newItem.primaryWav.empty()) break;
                                }
                                card.streams.push_back(newItem);
                            }
                        }
                        else
                        {
                            StreamPresetItem newItem;
                            newItem.name = L"New Stream " + std::to_wstring(card.streams.size() + 1);
                            newItem.category = L"Custom";
                            newItem.priority = 6;
                            newItem.volume = 1.0f;
                            newItem.primaryWav = L"sound.wav";
                            card.streams.push_back(newItem);
                        }
                        SaveStreamPresetsToFile(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&ch.rcBtnClearAll, pt))
                    {
                        if (!card.streams.empty())
                        {
                            if (ShowModernMessageBox(hWnd, (L"Clear all streams in '" + card.name + L"'?").c_str(), L"Clear Streams", MB_YESNO | MB_ICONQUESTION) == IDYES)
                            {
                                card.streams.clear();
                                SaveStreamPresetsToFile(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }
                }

                // Check chip hits
                for (const auto& chip : pState->chipHits)
                {
                    if (PtInRect(&chip.rcBtnRemove, pt))
                    {
                        if (chip.cardIdx >= 0 && chip.cardIdx < (int)pState->presetCards.size())
                        {
                            auto& c = pState->presetCards[chip.cardIdx];
                            if (chip.streamIdx >= 0 && chip.streamIdx < (int)c.streams.size())
                            {
                                c.streams.erase(c.streams.begin() + chip.streamIdx);
                                SaveStreamPresetsToFile(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }
                }
            }

            // TAB 1 Controls
            if (pState->activeTab == 1)
            {
                if (PtInRect(&pState->rcBtnAddFiles, pt))
                {
                    wchar_t szFiles[4096] = { 0 };
                    OPENFILENAMEW ofn = { sizeof(ofn) };
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFiles;
                    ofn.nMaxFile = 4096;
                    std::wstring trainsetDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                    ofn.lpstrInitialDir = trainsetDir.c_str();
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

                    if (GetOpenFileNameW(&ofn))
                    {
                        TargetSection defaultSec = (pState->targetFilter == TargetFilterMode::WagonsOnly) ? TargetSection::Wagon : TargetSection::Engine;
                        wchar_t* p = szFiles;
                        std::wstring dir = p;
                        p += dir.length() + 1;
                        if (*p == L'\0')
                        {
                            AddSmsFileTarget(pState, dir, defaultSec);
                        }
                        else
                        {
                            while (*p)
                            {
                                std::wstring fName = p;
                                std::wstring full = dir + L"\\" + fName;
                                AddSmsFileTarget(pState, full, defaultSec);
                                p += fName.length() + 1;
                            }
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnClearFiles, pt))
                {
                    pState->targetFiles.clear();
                    pState->targetHits.clear();
                    pState->scrollYTab1 = 0;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnFilterAll, pt))
                {
                    pState->targetFilter = TargetFilterMode::All;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnFilterEngines, pt))
                {
                    pState->targetFilter = TargetFilterMode::EnginesOnly;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnFilterWagons, pt))
                {
                    pState->targetFilter = TargetFilterMode::WagonsOnly;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcGridMasterCheck, pt) || 
                        (PtInRect(&pState->rcGridHeader, pt) && pt.x <= pState->rcGridHeader.left + 50))
                {
                    bool allSelected = false;
                    int visibleCount = 0;
                    for (const auto& t : pState->targetFiles)
                    {
                        if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                        if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                        visibleCount++;
                    }
                    if (visibleCount > 0)
                    {
                        allSelected = true;
                        for (const auto& t : pState->targetFiles)
                        {
                            if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                            if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                            if (!t.isSelected) { allSelected = false; break; }
                        }
                    }
                    bool targetState = !allSelected;
                    for (auto& t : pState->targetFiles)
                    {
                        if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                        if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                        t.isSelected = targetState;
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (pState->targetFilter != TargetFilterMode::WagonsOnly && (PtInRect(&pState->rcSectionEnginesCheck, pt)))
                {
                    bool allEngSel = true;
                    int engCount = 0;
                    for (const auto& t : pState->targetFiles)
                    {
                        if (t.section == TargetSection::Engine)
                        {
                            engCount++;
                            if (!t.isSelected) allEngSel = false;
                        }
                    }
                    bool targetState = (engCount > 0) ? !allEngSel : true;
                    for (auto& t : pState->targetFiles)
                    {
                        if (t.section == TargetSection::Engine) t.isSelected = targetState;
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (pState->targetFilter != TargetFilterMode::WagonsOnly && (PtInRect(&pState->rcSectionEnginesChev, pt) || PtInRect(&pState->rcSectionEnginesHdr, pt)))
                {
                    pState->bSectionEnginesExpanded = !pState->bSectionEnginesExpanded;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (pState->targetFilter != TargetFilterMode::EnginesOnly && (PtInRect(&pState->rcSectionWagonsCheck, pt)))
                {
                    bool allWagSel = true;
                    int wagCount = 0;
                    for (const auto& t : pState->targetFiles)
                    {
                        if (t.section == TargetSection::Wagon)
                        {
                            wagCount++;
                            if (!t.isSelected) allWagSel = false;
                        }
                    }
                    bool targetState = (wagCount > 0) ? !allWagSel : true;
                    for (auto& t : pState->targetFiles)
                    {
                        if (t.section == TargetSection::Wagon) t.isSelected = targetState;
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (pState->targetFilter != TargetFilterMode::EnginesOnly && (PtInRect(&pState->rcSectionWagonsChev, pt) || PtInRect(&pState->rcSectionWagonsHdr, pt)))
                {
                    pState->bSectionWagonsExpanded = !pState->bSectionWagonsExpanded;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnExpandAllTab1, pt))
                {
                    for (auto& t : pState->targetFiles) t.isExpanded = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnCollapseAllTab1, pt))
                {
                    for (auto& t : pState->targetFiles) t.isExpanded = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Check Target File hits
                for (const auto& th : pState->targetHits)
                {
                    if (th.fileIdx < 0 || th.fileIdx >= (int)pState->targetFiles.size()) continue;
                    auto& tgt = pState->targetFiles[th.fileIdx];

                    if (PtInRect(&th.rcCheck, pt))
                    {
                        tgt.isSelected = !tgt.isSelected;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    else if (PtInRect(&th.rcChevron, pt) || PtInRect(&th.rcHeader, pt))
                    {
                        tgt.isExpanded = !tgt.isExpanded;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Check Mutation Radio Modes
                if (PtInRect(&pState->rcRadioInject, pt))
                {
                    pState->mutationMode = StreamMutationMode::InjectPreset;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcRadioReplace, pt))
                {
                    pState->mutationMode = StreamMutationMode::ReplaceMatching;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcRadioSwapPath, pt))
                {
                    pState->mutationMode = StreamMutationMode::BatchPathSwap;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (PtInRect(&pState->rcRadioFixCounts, pt))
                {
                    pState->mutationMode = StreamMutationMode::FixCountsOnly;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Dropdown Pickers (CustomDropDownMenu)
                if (pState->mutationMode == StreamMutationMode::InjectPreset || pState->mutationMode == StreamMutationMode::ReplaceMatching)
                {
                    if (PtInRect(&pState->rcPickerPreset, pt))
                    {
                        if (CustomDropDownMenu::WasClosedByAnchor()) return 0;

                        std::vector<DropDownItem> presetItems;
                        if (pState->presetCards.empty())
                        {
                            presetItems.push_back(DropDownItem::Header(L"No Presets in Library"));
                        }
                        else
                        {
                            std::set<std::wstring> allFolders;
                            std::map<std::wstring, std::vector<size_t>> directCards;

                            // 1. Collect all categories from loaded cards
                            for (size_t cIdx = 0; cIdx < pState->presetCards.size(); ++cIdx)
                            {
                                const auto& card = pState->presetCards[cIdx];
                                std::wstring cat = card.category.empty() ? L"AI_HORNS" : card.category;
                                directCards[cat].push_back(cIdx);
                                allFolders.insert(cat);

                                size_t pos = 0;
                                while ((pos = cat.find(L'\\', pos)) != std::wstring::npos)
                                {
                                    std::wstring parentPrefix = cat.substr(0, pos);
                                    if (!parentPrefix.empty()) allFolders.insert(parentPrefix);
                                    pos++;
                                }
                            }

                            // 2. Also include physical category folders on disk
                            auto diskCats = DatabaseManager::GetSmsPresetCategories();
                            for (const auto& dc : diskCats)
                            {
                                if (!dc.empty())
                                {
                                    allFolders.insert(dc);
                                    size_t pos = 0;
                                    while ((pos = dc.find(L'\\', pos)) != std::wstring::npos)
                                    {
                                        std::wstring parentPrefix = dc.substr(0, pos);
                                        if (!parentPrefix.empty()) allFolders.insert(parentPrefix);
                                        pos++;
                                    }
                                }
                            }

                            std::vector<std::wstring> sortedFolders(allFolders.begin(), allFolders.end());
                            std::sort(sortedFolders.begin(), sortedFolders.end());

                            for (const auto& folder : sortedFolders)
                            {
                                size_t lastSlash = folder.rfind(L'\\');
                                std::wstring parentGroup = (lastSlash != std::wstring::npos) ? folder.substr(0, lastSlash) : L"";
                                std::wstring leafName = (lastSlash != std::wstring::npos) ? folder.substr(lastSlash + 1) : folder;
                                int indent = 0;
                                for (wchar_t ch : folder) { if (ch == L'\\') indent++; }

                                int totalInBranch = 0;
                                for (const auto& card : pState->presetCards)
                                {
                                    std::wstring cCat = card.category.empty() ? L"AI_HORNS" : card.category;
                                    if (cCat == folder || cCat.rfind(folder + L"\\", 0) == 0) totalInBranch++;
                                }

                                std::wstring secText = std::to_wstring(totalInBranch) + L" Preset" + (totalInBranch == 1 ? L"" : L"s");
                                presetItems.push_back(DropDownItem::GroupHeader(leafName, L"\xE8B7", totalInBranch, true /* collapsed by default */, 0, false, false, secText, parentGroup, indent, folder));

                                if (directCards.find(folder) != directCards.end())
                                {
                                    for (size_t cIdx : directCards[folder])
                                    {
                                        const auto& card = pState->presetCards[cIdx];
                                        bool isCurrent = ((int)cIdx == pState->selectedPresetCardIdx);
                                        std::wstring sec = std::to_wstring(card.streams.size()) + L" stream" + (card.streams.size() == 1 ? L"" : L"s");
                                        presetItems.push_back(DropDownItem::Action((int)cIdx + 1, isCurrent ? L"\xE73E" : L"\xE8D6", card.name, sec, isCurrent, true, folder, folder, indent + 1));
                                    }
                                }
                            }
                        }

                        int curSel = (pState->selectedPresetCardIdx >= 0) ? (pState->selectedPresetCardIdx + 1) : -1;
                        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerPreset, presetItems, curSel, 340);
                        int idx = chosen - 1;
                        if (idx >= 0 && idx < (int)pState->presetCards.size())
                        {
                            pState->selectedPresetCardIdx = idx;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }
                    else if (PtInRect(&pState->rcPickerSg, pt))
                    {
                        if (CustomDropDownMenu::WasClosedByAnchor()) return 0;

                        std::vector<DropDownItem> sgItems = {
                            DropDownItem::Action(1, L"\xE7F4", L"ScalabilityGroup 5 (High Detail - Default)", L"Exterior / Standard", pState->targetSg == 5),
                            DropDownItem::Action(2, L"\xE7F4", L"ScalabilityGroup 3 (Medium Detail)", L"Performance", pState->targetSg == 3),
                            DropDownItem::Action(3, L"\xE7F4", L"ScalabilityGroup 0 (Cab / Interior View)", L"Cab view", pState->targetSg == 0),
                            DropDownItem::Action(4, L"\xE71D", L"All ScalabilityGroups (5, 3, and 0)", L"All Levels", pState->targetSg == -1)
                        };
                        int curSgId = (pState->targetSg == 5) ? 1 : ((pState->targetSg == 3) ? 2 : ((pState->targetSg == 0) ? 3 : 4));
                        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerSg, sgItems, curSgId, 320);
                        if (chosen == 1) pState->targetSg = 5;
                        else if (chosen == 2) pState->targetSg = 3;
                        else if (chosen == 3) pState->targetSg = 0;
                        else if (chosen == 4) pState->targetSg = -1;
                        if (chosen > 0) InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }

                if (pState->mutationMode == StreamMutationMode::InjectPreset)
                {
                    if (PtInRect(&pState->rcPickerPos, pt))
                    {
                        if (CustomDropDownMenu::WasClosedByAnchor()) return 0;

                        std::vector<DropDownItem> posItems = {
                            DropDownItem::Action(1, L"\xE8D7", L"At End of Streams (Append)", L"Default", pState->insertPosition == 1),
                            DropDownItem::Action(2, L"\xE8D8", L"At Beginning of Streams (Prepend)", L"Stream 1", pState->insertPosition == 0),
                            DropDownItem::Action(3, L"\xE70F", L"After Specific Stream Number...", L"Splice in between", pState->insertPosition == 2)
                        };
                        int curPosId = (pState->insertPosition == 1) ? 1 : ((pState->insertPosition == 0) ? 2 : 3);
                        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, pState->rcPickerPos, posItems, curPosId, 320);
                        if (chosen == 1) pState->insertPosition = 1;
                        else if (chosen == 2) pState->insertPosition = 0;
                        else if (chosen == 3)
                        {
                            pState->insertPosition = 2;
                            if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter))
                            {
                                SetFocus(pState->hEditInsertAfter);
                            }
                        }
                        if (chosen > 0) InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Check backup checkbox (only if mouse button went down on this checkbox)
                if (pState->bMouseDownOnBackupCheck && PtInRect(&pState->rcCheckBackup, pt))
                {
                    pState->createBackup = !pState->createBackup;
                    pState->bMouseDownOnBackupCheck = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                pState->bMouseDownOnBackupCheck = false;

                // Preview / Dry Run
                if (PtInRect(&pState->rcBtnPreviewDryRun, pt))
                {
                    int selCount = 0;
                    for (const auto& t : pState->targetFiles)
                    {
                        if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                        if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                        if (t.isSelected) ++selCount;
                    }
                    if (selCount == 0)
                    {
                        ShowModernMessageBox(hWnd, L"No SMS files selected. Please check at least one SMS file.", L"Dry Run", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    std::wstring report = L"=== Dry Run Mutation Plan ===\r\n\r\n";
                    report += L"Target Files: " + std::to_wstring(selCount) + L" selected\r\n";
                    if (pState->mutationMode == StreamMutationMode::InjectPreset)
                    {
                        std::wstring presetName = (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size()) ?
                            pState->presetCards[pState->selectedPresetCardIdx].name : L"(None Selected)";
                        report += L"Action: Inject Stream Preset Library Card\r\n";
                        report += L"Source Preset: " + presetName + L"\r\n";
                        std::wstring sgDesc = (pState->targetSg == -1) ? L"All ScalabilityGroups (5, 3, 0)" : (L"ScalabilityGroup " + std::to_wstring(pState->targetSg));
                        report += L"Target SG: " + sgDesc + L"\r\n";
                        if (pState->insertPosition == 0)
                            report += L"Position: At Beginning of Streams (Prepend)\r\n";
                        else if (pState->insertPosition == 2)
                        {
                            if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter))
                            {
                                wchar_t buf[32] = { 0 };
                                GetWindowTextW(pState->hEditInsertAfter, buf, 32);
                                pState->insertAfterStreamIndex = _wtoi(buf);
                            }
                            report += L"Position: After Stream #" + std::to_wstring(pState->insertAfterStreamIndex) + L" (Splice in between)\r\n";
                        }
                        else
                            report += L"Position: At End of Streams (Append)\r\n";
                        report += L"WAV Retargeting: Automatic Canonical Relative Paths\r\n";
                    }
                    else if (pState->mutationMode == StreamMutationMode::ReplaceMatching)
                    {
                        std::wstring presetName = (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size()) ?
                            pState->presetCards[pState->selectedPresetCardIdx].name : L"(None Selected)";
                        report += L"Action: Replace Existing Stream by Name / Trigger\r\n";
                        report += L"Source Preset: " + presetName + L"\r\n";
                        std::wstring sgDesc = (pState->targetSg == -1) ? L"All ScalabilityGroups (5, 3, 0)" : (L"ScalabilityGroup " + std::to_wstring(pState->targetSg));
                        report += L"Target SG: " + sgDesc + L"\r\n";
                    }
                    else if (pState->mutationMode == StreamMutationMode::FixCountsOnly)
                    {
                        report += L"Action: Bulk synchronize Streams(N) count declarations to parsed stream blocks\r\n";
                    }
                    report += L"\r\nReady to apply changes safely!";

                    ShowModernMessageBox(hWnd, report.c_str(), L"Mutation Dry-Run Preview", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                else if (PtInRect(&pState->rcBtnApplyMutation, pt))
                {
                    int selCount = 0;
                    for (const auto& t : pState->targetFiles)
                    {
                        if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                        if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                        if (t.isSelected) ++selCount;
                    }
                    if (selCount == 0)
                    {
                        ShowModernMessageBox(hWnd, L"No SMS files selected. Please select at least one file.", L"Apply Mutation", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    std::wstring confirm = L"Are you sure you want to apply mutations to " + std::to_wstring(selCount) + L" SMS file(s)?";
                    if (pState->createBackup) confirm += L"\r\n(Original files will be backed up as .TSCB_BACKUP)";

                    if (ShowModernMessageBox(hWnd, confirm.c_str(), L"Confirm Batch Mutation", MB_YESNO | MB_ICONQUESTION) != IDYES)
                        return 0;

                    if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter))
                    {
                        wchar_t numBuf[32] = { 0 };
                        GetWindowTextW(pState->hEditInsertAfter, numBuf, 32);
                        if (numBuf[0] != L'\0')
                        {
                            pState->insertAfterStreamIndex = _wtoi(numBuf);
                        }
                    }

                    int successCount = 0;
                    std::unordered_set<std::wstring> processedPaths;
                    for (auto& tgt : pState->targetFiles)
                    {
                        if (pState->targetFilter == TargetFilterMode::EnginesOnly && tgt.section != TargetSection::Engine) continue;
                        if (pState->targetFilter == TargetFilterMode::WagonsOnly && tgt.section != TargetSection::Wagon) continue;
                        if (!tgt.isSelected || !tgt.isLoaded) continue;
                        if (processedPaths.find(tgt.fullPath) != processedPaths.end()) continue;
                        processedPaths.insert(tgt.fullPath);

                        if (pState->createBackup)
                        {
                            std::wstring bakPath = tgt.fullPath + L".TSCB_BACKUP";
                            CopyFileW(tgt.fullPath.c_str(), bakPath.c_str(), FALSE);
                        }

                        if (pState->mutationMode == StreamMutationMode::FixCountsOnly)
                        {
                            SmsParser_FixAllCounts(tgt.doc);
                            SmsParser_SaveToFile(tgt.doc, tgt.fullPath);
                            tgt.hasMismatch = false;
                            tgt.declaredStreams = tgt.parsedStreams;
                            successCount++;
                        }
                        else if (pState->mutationMode == StreamMutationMode::InjectPreset)
                        {
                            // If preset card selected, inject its streams
                            if (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size())
                            {
                                const auto& card = pState->presetCards[pState->selectedPresetCardIdx];
                                if (!card.streams.empty())
                                {
                                    std::vector<int> targetGroupIndices;
                                    for (int g = 0; g < (int)tgt.doc.groups.size(); ++g)
                                    {
                                        if (pState->targetSg == -1 || tgt.doc.groups[g].groupLevel == pState->targetSg)
                                        {
                                            targetGroupIndices.push_back(g);
                                        }
                                    }
                                    if (targetGroupIndices.empty() && !tgt.doc.groups.empty())
                                    {
                                        targetGroupIndices.push_back(tgt.doc.activeGroupIndex >= 0 ? tgt.doc.activeGroupIndex : 0);
                                    }

                                    for (int gIdx : targetGroupIndices)
                                    {
                                        auto& grp = tgt.doc.groups[gIdx];
                                        int insertOffset = 0;
                                        for (const auto& pStream : card.streams)
                                        {
                                            SmsStream st;
                                            st.friendlyTitle = pStream.name;
                                            st.priority = pStream.priority;
                                            st.volume = pStream.volume;
                                            st.isStereo = pStream.isStereo;
                                            st.triggers = pStream.triggers;
                                            st.curves = pStream.curves;
                                            st.isCollapsed = true;

                                            // Automatic Canonical WAV Retargeting (identical to SMS Studio Paste Stream)
                                            for (auto& tr : st.triggers)
                                            {
                                                for (auto& fRef : tr.files)
                                                {
                                                    std::wstring resolved = fRef.resolvedDiskPath;
                                                    if (resolved.empty() || GetFileAttributesW(resolved.c_str()) == INVALID_FILE_ATTRIBUTES)
                                                    {
                                                        resolved = SmsParser_ResolveWavPath(tgt.fullPath, fRef.fileName, pState->basePath);
                                                    }
                                                    if (!resolved.empty() && GetFileAttributesW(resolved.c_str()) != INVALID_FILE_ATTRIBUTES)
                                                    {
                                                        fRef.fileName = SmsParser_RetargetWavRelativePath(tgt.fullPath, resolved, pState->basePath);
                                                        fRef.resolvedDiskPath = resolved;
                                                        fRef.existsOnDisk = true;
                                                    }
                                                }
                                            }

                                            if (pState->insertPosition == 0) // At Beginning
                                            {
                                                grp.streams.insert(grp.streams.begin() + insertOffset, st);
                                                insertOffset++;
                                            }
                                            else if (pState->insertPosition == 2) // After Specific Stream Number
                                            {
                                                int targetIdx = pState->insertAfterStreamIndex;
                                                if (targetIdx < 0) targetIdx = 0;
                                                if (targetIdx > (int)grp.streams.size()) targetIdx = (int)grp.streams.size();
                                                grp.streams.insert(grp.streams.begin() + targetIdx + insertOffset, st);
                                                insertOffset++;
                                            }
                                            else // At End
                                            {
                                                grp.streams.push_back(st);
                                            }
                                        }

                                        for (size_t sIdx = 0; sIdx < grp.streams.size(); ++sIdx)
                                            grp.streams[sIdx].streamIndex = (int)sIdx + 1;

                                        grp.declaredStreams = (int)grp.streams.size();
                                    }

                                    SmsParser_FixAllCounts(tgt.doc);
                                    SmsParser_SaveToFile(tgt.doc, tgt.fullPath);
                                    tgt.declaredStreams = tgt.doc.groups.empty() ? 0 : tgt.doc.groups[tgt.doc.activeGroupIndex].declaredStreams;
                                    tgt.parsedStreams = tgt.doc.groups.empty() ? 0 : (int)tgt.doc.groups[tgt.doc.activeGroupIndex].streams.size();
                                    tgt.hasMismatch = false;
                                    successCount++;
                                }
                            }
                        }
                        else if (pState->mutationMode == StreamMutationMode::ReplaceMatching)
                        {
                            if (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size())
                            {
                                const auto& card = pState->presetCards[pState->selectedPresetCardIdx];
                                if (!card.streams.empty())
                                {
                                    bool anyReplaced = false;
                                    for (auto& grp : tgt.doc.groups)
                                    {
                                        if (pState->targetSg != -1 && grp.groupLevel != pState->targetSg) continue;

                                        for (const auto& pStream : card.streams)
                                        {
                                            for (auto& st : grp.streams)
                                            {
                                                bool isMatch = (_wcsicmp(st.friendlyTitle.c_str(), pStream.name.c_str()) == 0);
                                                if (!isMatch && !st.triggers.empty() && !pStream.triggers.empty())
                                                {
                                                    if (_wcsicmp(st.triggers[0].triggerType.c_str(), pStream.triggers[0].triggerType.c_str()) == 0 &&
                                                        st.triggers[0].triggerId == pStream.triggers[0].triggerId)
                                                    {
                                                        isMatch = true;
                                                    }
                                                }

                                                if (isMatch)
                                                {
                                                    st.priority = pStream.priority;
                                                    st.volume = pStream.volume;
                                                    st.isStereo = pStream.isStereo;
                                                    st.triggers = pStream.triggers;
                                                    st.curves = pStream.curves;

                                                    // Automatic Canonical WAV Retargeting (identical to SMS Studio Paste Stream)
                                                    for (auto& tr : st.triggers)
                                                    {
                                                        for (auto& fRef : tr.files)
                                                        {
                                                            std::wstring resolved = fRef.resolvedDiskPath;
                                                            if (resolved.empty() || GetFileAttributesW(resolved.c_str()) == INVALID_FILE_ATTRIBUTES)
                                                            {
                                                                resolved = SmsParser_ResolveWavPath(tgt.fullPath, fRef.fileName, pState->basePath);
                                                            }
                                                            if (!resolved.empty() && GetFileAttributesW(resolved.c_str()) != INVALID_FILE_ATTRIBUTES)
                                                            {
                                                                fRef.fileName = SmsParser_RetargetWavRelativePath(tgt.fullPath, resolved, pState->basePath);
                                                                fRef.resolvedDiskPath = resolved;
                                                                fRef.existsOnDisk = true;
                                                            }
                                                        }
                                                    }
                                                    anyReplaced = true;
                                                }
                                            }
                                        }
                                    }

                                    if (anyReplaced)
                                    {
                                        SmsParser_FixAllCounts(tgt.doc);
                                        SmsParser_SaveToFile(tgt.doc, tgt.fullPath);
                                        successCount++;
                                    }
                                }
                            }
                        }
                        else if (pState->mutationMode == StreamMutationMode::BatchPathSwap)
                        {
                            if (!pState->pathFindStr.empty())
                            {
                                bool anySwapped = false;
                                for (auto& grp : tgt.doc.groups)
                                {
                                    if (pState->targetSg != -1 && grp.groupLevel != pState->targetSg) continue;

                                    for (auto& st : grp.streams)
                                    {
                                        for (auto& tr : st.triggers)
                                        {
                                            for (auto& fRef : tr.files)
                                            {
                                                size_t pos = 0;
                                                bool fileModified = false;
                                                std::wstring fn = fRef.fileName;
                                                while ((pos = fn.find(pState->pathFindStr, pos)) != std::wstring::npos)
                                                {
                                                    fn.replace(pos, pState->pathFindStr.length(), pState->pathReplaceStr);
                                                    pos += pState->pathReplaceStr.length();
                                                    fileModified = true;
                                                }
                                                if (fileModified)
                                                {
                                                    fRef.fileName = fn;
                                                    fRef.resolvedDiskPath = SmsParser_ResolveWavPath(tgt.fullPath, fRef.fileName, pState->basePath);
                                                    fRef.existsOnDisk = (!fRef.resolvedDiskPath.empty() && GetFileAttributesW(fRef.resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                                                    anySwapped = true;
                                                }
                                            }
                                        }
                                    }
                                }

                                if (anySwapped)
                                {
                                    SmsParser_SaveToFile(tgt.doc, tgt.fullPath);
                                    successCount++;
                                }
                            }
                        }
                    }

                    // Notify Sound Studio and SMS Editor to reload any mutated SMS files live
                    HWND hStudio = GetSoundStudioWindow();
                    if (hStudio && IsWindow(hStudio))
                    {
                        for (const auto& path : processedPaths)
                        {
                            SoundStudio_ReloadSmsIfMatches(hStudio, path);
                        }
                    }

                    std::wstring resMsg = L"Successfully mutated " + std::to_wstring(successCount) + L" SMS file(s)!";
                    ShowModernMessageBox(hWnd, resMsg.c_str(), L"Mutation Complete", MB_OK | MB_ICONINFORMATION);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        if (pState && pState->activeTab == 0)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int topY = 112;
            if (pt.x < pState->sidebarWidth && pt.y > topY)
            {
                int hitIdx = -1;
                for (size_t i = 0; i < pState->treeItems.size(); ++i)
                {
                    if (PtInRect(&pState->treeItems[i].rcRow, pt))
                    {
                        hitIdx = (int)i;
                        break;
                    }
                }

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);

                if (hitIdx >= 0)
                {
                    const auto& item = pState->treeItems[hitIdx];
                    if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET)
                    {
                        pState->selectedPresetCardIdx = item.presetCardIndex;
                        pState->selectedCategory = item.category;
                        InvalidateRect(hWnd, NULL, FALSE);

                        std::vector<ContextMenuItem> mItems;
                        mItems.push_back(ContextMenuItem::Action(101, L"\xE8C8", L"Duplicate / Clone"));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(102, L"\xE8C6", L"Cut Preset", L"Ctrl+X"));
                        mItems.push_back(ContextMenuItem::Action(103, L"\xE8C8", L"Copy Preset", L"Ctrl+C"));
                        mItems.push_back(ContextMenuItem::Action(104, L"\xE70F", L"Rename...", L"F2"));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(105, L"\xE74D", L"Delete Preset...", L"Del"));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(106, L"\xE838", L"Reveal on Disk"));

                        int cmd = ModernContextMenu::Show(hWnd, ptScreen.x, ptScreen.y, mItems, TRUE, 220);
                        if (IsWindow(hWnd)) { SetActiveWindow(hWnd); SetForegroundWindow(hWnd); }
                        HandlePresetTreeContextMenuCmd(hWnd, pState, cmd, hitIdx);
                        return 0;
                    }
                    else if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                    {
                        pState->selectedCategory = item.category;
                        pState->selectedPresetCardIdx = -1;
                        InvalidateRect(hWnd, NULL, FALSE);

                        std::vector<ContextMenuItem> mItems;
                        mItems.push_back(ContextMenuItem::Action(201, L"\xE710", L"New Preset Here..."));
                        mItems.push_back(ContextMenuItem::Action(202, L"\xE8F4", L"New Subfolder..."));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(203, L"\xE8C6", L"Cut Folder", L"Ctrl+X"));
                        mItems.push_back(ContextMenuItem::Action(204, L"\xE8C8", L"Copy Folder", L"Ctrl+C"));
                        bool canPaste = (pState->clipboardOp != SmsStreamMutatorState::CLIP_NONE && !pState->clipSourcePath.empty());
                        mItems.push_back(ContextMenuItem::Action(205, L"\xE77F", L"Paste", L"Ctrl+V", canPaste));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(206, L"\xE70F", L"Rename Folder...", L"F2"));
                        mItems.push_back(ContextMenuItem::Action(207, L"\xE74D", L"Delete Folder...", L"Del"));
                        mItems.push_back(ContextMenuItem::Separator());
                        mItems.push_back(ContextMenuItem::Action(208, L"\xE838", L"Open in Windows Explorer"));

                        int cmd = ModernContextMenu::Show(hWnd, ptScreen.x, ptScreen.y, mItems, TRUE, 220);
                        if (IsWindow(hWnd)) { SetActiveWindow(hWnd); SetForegroundWindow(hWnd); }
                        HandlePresetTreeContextMenuCmd(hWnd, pState, cmd, hitIdx);
                        return 0;
                    }
                }
                else
                {
                    std::vector<ContextMenuItem> mItems;
                    mItems.push_back(ContextMenuItem::Action(301, L"\xE710", L"New Stream Preset..."));
                    mItems.push_back(ContextMenuItem::Action(302, L"\xE8F4", L"New Folder..."));
                    bool canPaste = (pState->clipboardOp != SmsStreamMutatorState::CLIP_NONE && !pState->clipSourcePath.empty());
                    mItems.push_back(ContextMenuItem::Action(303, L"\xE77F", L"Paste", L"Ctrl+V", canPaste));
                    mItems.push_back(ContextMenuItem::Separator());
                    mItems.push_back(ContextMenuItem::Action(304, L"\xE8D5", L"Expand All"));
                    mItems.push_back(ContextMenuItem::Action(305, L"\xE8D5", L"Collapse All"));
                    mItems.push_back(ContextMenuItem::Action(306, L"\xE72C", L"Rescan from Disk"));
                    mItems.push_back(ContextMenuItem::Action(307, L"\xE838", L"Open Presets Folder"));

                    int cmd = ModernContextMenu::Show(hWnd, ptScreen.x, ptScreen.y, mItems, TRUE, 220);
                    if (IsWindow(hWnd)) { SetActiveWindow(hWnd); SetForegroundWindow(hWnd); }
                    HandlePresetTreeContextMenuCmd(hWnd, pState, cmd, -1);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (pState && pState->activeTab == 0)
        {
            bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_F2)
            {
                if (pState->selectedPresetCardIdx >= 0)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET &&
                            pState->treeItems[i].presetCardIndex == pState->selectedPresetCardIdx)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 104, (int)i);
                            break;
                        }
                    }
                }
                else if (!pState->selectedCategory.empty())
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER &&
                            pState->treeItems[i].category == pState->selectedCategory)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 206, (int)i);
                            break;
                        }
                    }
                }
                return 0;
            }
            else if (wParam == VK_DELETE)
            {
                if (pState->selectedPresetCardIdx >= 0)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET &&
                            pState->treeItems[i].presetCardIndex == pState->selectedPresetCardIdx)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 105, (int)i);
                            break;
                        }
                    }
                }
                else if (!pState->selectedCategory.empty())
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER &&
                            pState->treeItems[i].category == pState->selectedCategory)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 207, (int)i);
                            break;
                        }
                    }
                }
                return 0;
            }
            else if (isCtrl && (wParam == 'C' || wParam == 'c'))
            {
                if (pState->selectedPresetCardIdx >= 0)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET &&
                            pState->treeItems[i].presetCardIndex == pState->selectedPresetCardIdx)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 103, (int)i);
                            break;
                        }
                    }
                }
                else if (!pState->selectedCategory.empty())
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER &&
                            pState->treeItems[i].category == pState->selectedCategory)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 204, (int)i);
                            break;
                        }
                    }
                }
                return 0;
            }
            else if (isCtrl && (wParam == 'X' || wParam == 'x'))
            {
                if (pState->selectedPresetCardIdx >= 0)
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET &&
                            pState->treeItems[i].presetCardIndex == pState->selectedPresetCardIdx)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 102, (int)i);
                            break;
                        }
                    }
                }
                else if (!pState->selectedCategory.empty())
                {
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        if (pState->treeItems[i].type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER &&
                            pState->treeItems[i].category == pState->selectedCategory)
                        {
                            HandlePresetTreeContextMenuCmd(hWnd, pState, 203, (int)i);
                            break;
                        }
                    }
                }
                return 0;
            }
            else if (isCtrl && (wParam == 'V' || wParam == 'v'))
            {
                ExecutePresetTreePaste(hWnd, pState, pState->selectedCategory);
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

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HGDIOBJ oldBm = SelectObject(memDC, hbm);

        // 1. Clear background
        HBRUSH bgBrush = CreateSolidBrush(UITheme::DarkBackground);
        FillRect(memDC, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(memDC, TRANSPARENT);

        if (pState)
        {
            pState->cardHits.clear();
            pState->chipHits.clear();
            pState->targetHits.clear();

            // 1. Burgundy Sub-Toolbar Ribbon (Y = 66 to 112, Height = 46px)
            int toolbarY = 66;
            int toolbarH = 46;
            RECT rcToolbar = { 0, toolbarY, rcClient.right, toolbarY + toolbarH };
            HBRUSH hbrTb = CreateSolidBrush(RGB(52, 22, 27)); // Burgundy Wine ribbon surface
            FillRect(memDC, &rcToolbar, hbrTb);
            DeleteObject(hbrTb);

            HPEN hPenLine = CreatePen(PS_SOLID, 1, PoolTheme::BorderLine); // Standard subtle divider line
            HGDIOBJ oldLine = SelectObject(memDC, hPenLine);
            MoveToEx(memDC, 0, toolbarY + toolbarH, NULL);
            LineTo(memDC, rcClient.right, toolbarY + toolbarH);
            SelectObject(memDC, oldLine);
            DeleteObject(hPenLine);

            int btnY = toolbarY + (toolbarH - 32) / 2;

            // ---------------- TAB 0: Stream Presets Library ----------------
            if (pState->activeTab == 0)
            {
                if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter) && IsWindowVisible(pState->hEditInsertAfter))
                {
                    ShowWindow(pState->hEditInsertAfter, SW_HIDE);
                    pState->bEditFocused = false;
                    pState->bEditHovered = false;
                }

                int topY = toolbarY + toolbarH; // 112px
                int sidebarW = pState->sidebarWidth;
                int h = rcClient.bottom;

                // =========================================================================
                // 1. Burgundy Ribbon Info Header & Action Buttons (Y = 66 to 112)
                // =========================================================================
                SetBkMode(memDC, TRANSPARENT);

                SelectObject(memDC, pState->hFontIcon);
                SetTextColor(memDC, RGB(255, 200, 210));
                RECT rcRibbonIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
                DrawTextW(memDC, L"\xE8D6", -1, &rcRibbonIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int rBtnX = rcClient.right - 480;
                RECT rcRibbonText = { 48, toolbarY, rBtnX - 16, toolbarY + toolbarH };

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(245, 245, 245));
                std::wstring rText = L"SMS Stream Presets Explorer";
                if (!pState->selectedCategory.empty())
                {
                    rText += L"  \x2022  Folder: " + pState->selectedCategory;
                }
                if (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size())
                {
                    rText += L"  \x2022  " + pState->presetCards[pState->selectedPresetCardIdx].name + L" (" + std::to_wstring(pState->presetCards[pState->selectedPresetCardIdx].streams.size()) + L" Streams)";
                }
                else
                {
                    rText += L"  \x2022  " + std::to_wstring(pState->presetCards.size()) + L" Presets Available";
                }
                DrawTextW(memDC, rText.c_str(), -1, &rcRibbonText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                // Ribbon action buttons on the right side
                pState->rcBtnNewPresetCard = { rBtnX, btnY, rBtnX + 150, btnY + 32 };
                rBtnX += 156;
                pState->rcBtnExpandAllTab0 = { rBtnX, btnY, rBtnX + 95, btnY + 32 };
                rBtnX += 101;
                pState->rcBtnCollapseAllTab0 = { rBtnX, btnY, rBtnX + 95, btnY + 32 };
                rBtnX += 101;
                pState->rcBtnOpenCategoryFolder = { rBtnX, btnY, rBtnX + 105, btnY + 32 };

                DrawModernButton(memDC, pState->rcBtnNewPresetCard, L"New Preset Card", PtInRect(&pState->rcBtnNewPresetCard, pState->ptMouse), false, true, pState->hFontBold, pState->hFontIcon, L"\xE710");
                DrawModernButton(memDC, pState->rcBtnExpandAllTab0, L"Expand All", PtInRect(&pState->rcBtnExpandAllTab0, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");
                DrawModernButton(memDC, pState->rcBtnCollapseAllTab0, L"Collapse All", PtInRect(&pState->rcBtnCollapseAllTab0, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");
                DrawModernButton(memDC, pState->rcBtnOpenCategoryFolder, L"Open Folder", PtInRect(&pState->rcBtnOpenCategoryFolder, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE838");

                // =========================================================================
                // 2. UNIFIED LEFT SIDEBAR (Explorer Tree)
                // =========================================================================
                RECT rcSidebar = { 0, topY, sidebarW, h };
                HBRUSH hbrSide = CreateSolidBrush(RGB(22, 24, 28));
                FillRect(memDC, &rcSidebar, hbrSide);
                DeleteObject(hbrSide);

                // Splitter Bar (clean double-line splitter)
                int splitterW = 6;
                pState->rcSplitter = { sidebarW, topY, sidebarW + splitterW, h };
                HBRUSH hbrSplitter = CreateSolidBrush(RGB(22, 24, 28));
                FillRect(memDC, &pState->rcSplitter, hbrSplitter);
                DeleteObject(hbrSplitter);

                COLORREF clrSplitter = pState->isDraggingSplitter ? RGB(0, 160, 255) : RGB(46, 46, 46);
                HPEN hPenSplitter = CreatePen(PS_SOLID, 1, clrSplitter);
                HPEN hOldP3 = (HPEN)SelectObject(memDC, hPenSplitter);

                MoveToEx(memDC, pState->rcSplitter.left, pState->rcSplitter.top, NULL);
                LineTo(memDC, pState->rcSplitter.left, pState->rcSplitter.bottom);

                MoveToEx(memDC, pState->rcSplitter.right - 1, pState->rcSplitter.top, NULL);
                LineTo(memDC, pState->rcSplitter.right - 1, pState->rcSplitter.bottom);

                SelectObject(memDC, hOldP3);
                DeleteObject(hPenSplitter);

                // Sidebar Toolbar ([+ Preset], [+ Folder], [Expand/Collapse], [Sync], [Explorer])
                int tbY = topY + 8;
                int btnH = 28;
                int curTbX = 8;

                pState->rcBtnTreeNewPreset = { curTbX, tbY, curTbX + 76, tbY + btnH };
                DrawModernButton(memDC, pState->rcBtnTreeNewPreset, L"Preset", PtInRect(&pState->rcBtnTreeNewPreset, pState->ptMouse), false, true, pState->hFontBold, pState->hFontIconSmall, L"\xE710");
                curTbX += 80;

                pState->rcBtnTreeNewFolder = { curTbX, tbY, curTbX + 80, tbY + btnH };
                DrawModernButton(memDC, pState->rcBtnTreeNewFolder, L"Folder", PtInRect(&pState->rcBtnTreeNewFolder, pState->ptMouse), false, false, pState->hFontBold, pState->hFontIconSmall, L"\xE8F4");
                curTbX += 84;

                pState->rcBtnTreeToggleAll = { curTbX, tbY, curTbX + 30, tbY + btnH };
                DrawModernButton(memDC, pState->rcBtnTreeToggleAll, L"", PtInRect(&pState->rcBtnTreeToggleAll, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8D5");
                curTbX += 34;

                pState->rcBtnTreeSync = { curTbX, tbY, curTbX + 30, tbY + btnH };
                DrawModernButton(memDC, pState->rcBtnTreeSync, L"", PtInRect(&pState->rcBtnTreeSync, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE72C");
                curTbX += 34;

                pState->rcBtnTreeOpenFolder = { curTbX, tbY, curTbX + 30, tbY + btnH };
                DrawModernButton(memDC, pState->rcBtnTreeOpenFolder, L"", PtInRect(&pState->rcBtnTreeOpenFolder, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE838");

                // Tree Explorer Items
                BuildPresetTreeExplorer(pState);

                int contentY = tbY + btnH + 8;
                int contentH = h - contentY;
                RECT rcSidebarClip = { 0, contentY, sidebarW, h };

                int rowH = 28;
                int totalTreeH = (int)pState->treeItems.size() * rowH + 20;
                pState->totalSidebarH = totalTreeH;

                RECT rcSidebarScroll = { sidebarW - 10, contentY + 2, sidebarW - 2, h - 2 };
                pState->m_sidebarScroll.SetBounds(rcSidebarScroll);
                pState->m_sidebarScroll.SetRange(0, (std::max)(0, totalTreeH - 1), contentH);
                pState->sidebarScrollY = pState->m_sidebarScroll.GetPos();

                HRGN hRgnSidebarClip = CreateRectRgn(rcSidebarClip.left, rcSidebarClip.top, rcSidebarClip.right, rcSidebarClip.bottom);
                SelectClipRgn(memDC, hRgnSidebarClip);

                if (pState->treeItems.empty())
                {
                    SelectObject(memDC, pState->hFontSmall);
                    SetTextColor(memDC, RGB(130, 130, 130));
                    RECT rcEmpty = { 20, contentY + 30, sidebarW - 20, contentY + 160 };
                    DrawTextW(memDC, L"No presets found.\n\nClick [+ Preset] or [+ Folder] above to start organizing.", -1, &rcEmpty, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                }
                else
                {
                    int itemY = contentY - pState->sidebarScrollY + 4;
                    for (size_t i = 0; i < pState->treeItems.size(); ++i)
                    {
                        auto& item = pState->treeItems[i];
                        item.rcRow = { 6, itemY, sidebarW - 12, itemY + rowH - 2 };

                        if (item.rcRow.bottom >= contentY && item.rcRow.top <= h)
                        {
                            bool isDropTarget = (pState->isTreeDragging && pState->dropTargetTreeFolderIdx == (int)i);
                            COLORREF bgRow = item.isSelected ? RGB(0, 120, 215) : (isDropTarget ? RGB(50, 75, 110) : (pState->hoveredTreeItemIdx == (int)i ? RGB(36, 38, 44) : RGB(22, 22, 25)));
                            COLORREF bdrRow = item.isSelected ? RGB(0, 120, 215) : (isDropTarget ? RGB(20, 140, 235) : (item.isCut ? RGB(140, 140, 60) : RGB(46, 46, 46)));

                            HBRUSH hbrRow = CreateSolidBrush(bgRow);
                            HPEN hpenRow = CreatePen(item.isCut ? PS_DOT : PS_SOLID, 1, bdrRow);
                            HGDIOBJ holdB = SelectObject(memDC, hbrRow);
                            HGDIOBJ holdP = SelectObject(memDC, hpenRow);
                            RoundRect(memDC, item.rcRow.left, item.rcRow.top, item.rcRow.right, item.rcRow.bottom, 4, 4);
                            SelectObject(memDC, holdB);
                            SelectObject(memDC, holdP);
                            DeleteObject(hbrRow);
                            DeleteObject(hpenRow);

                            int indentX = item.rcRow.left + item.depth * 14 + 4;

                            if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_FOLDER)
                            {
                                RECT rcChev = { indentX, item.rcRow.top, indentX + 16, item.rcRow.bottom };
                                item.rcChevron = rcChev;

                                int midX = (rcChev.left + rcChev.right) / 2;
                                int midY = (rcChev.top + rcChev.bottom) / 2;
                                COLORREF triCol = item.isSelected ? RGB(255, 255, 255) : RGB(175, 180, 190);
                                HBRUSH hbrTri = CreateSolidBrush(triCol);
                                HPEN hpenTri = CreatePen(PS_SOLID, 1, triCol);
                                HGDIOBJ holdBT = SelectObject(memDC, hbrTri);
                                HGDIOBJ holdPT = SelectObject(memDC, hpenTri);

                                if (item.isExpanded)
                                {
                                    POINT pts[3] = { { midX - 4, midY - 2 }, { midX + 4, midY - 2 }, { midX, midY + 3 } };
                                    Polygon(memDC, pts, 3);
                                }
                                else
                                {
                                    POINT pts[3] = { { midX - 2, midY - 4 }, { midX - 2, midY + 4 }, { midX + 3, midY } };
                                    Polygon(memDC, pts, 3);
                                }

                                SelectObject(memDC, holdBT);
                                SelectObject(memDC, holdPT);
                                DeleteObject(hbrTri);
                                DeleteObject(hpenTri);

                                SelectObject(memDC, pState->hFontIconSmall);
                                SetTextColor(memDC, item.isSelected ? RGB(255, 255, 255) : RGB(225, 185, 75));
                                RECT rcIcon = { indentX + 18, item.rcRow.top, indentX + 36, item.rcRow.bottom };
                                DrawTextW(memDC, L"\xE8B7", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                                SelectObject(memDC, pState->hFontBold);
                                SetTextColor(memDC, item.isSelected ? RGB(255, 255, 255) : (item.isCut ? RGB(160, 160, 160) : RGB(240, 240, 240)));
                                RECT rcName = { indentX + 38, item.rcRow.top, item.rcRow.right - 34, item.rcRow.bottom };
                                DrawTextW(memDC, item.name.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                if (item.presetCount > 0)
                                {
                                    SelectObject(memDC, pState->hFontSmall);
                                    SetTextColor(memDC, item.isSelected ? RGB(220, 235, 255) : RGB(130, 130, 130));
                                    RECT rcCnt = { item.rcRow.right - 32, item.rcRow.top, item.rcRow.right - 4, item.rcRow.bottom };
                                    DrawTextW(memDC, std::to_wstring(item.presetCount).c_str(), -1, &rcCnt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                                }
                            }
                            else if (item.type == SmsStreamMutatorState::TreeExplorerItem::ITEM_PRESET)
                            {
                                SelectObject(memDC, pState->hFontIconSmall);
                                SetTextColor(memDC, item.isSelected ? RGB(255, 255, 255) : RGB(80, 175, 250));
                                RECT rcIcon = { indentX + 10, item.rcRow.top, indentX + 28, item.rcRow.bottom };
                                DrawTextW(memDC, L"\xE8D6", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                                SelectObject(memDC, pState->hFontSmall);
                                SetTextColor(memDC, item.isSelected ? RGB(255, 255, 255) : (item.isCut ? RGB(160, 160, 160) : RGB(240, 240, 240)));
                                RECT rcName = { indentX + 30, item.rcRow.top, item.rcRow.right - 54, item.rcRow.bottom };
                                DrawTextW(memDC, item.name.c_str(), -1, &rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                if (item.presetCardIndex >= 0 && item.presetCardIndex < (int)pState->presetCards.size())
                                {
                                    const auto& pc = pState->presetCards[item.presetCardIndex];
                                    std::wstring badge = std::to_wstring(pc.streams.size()) + L"s";
                                    SelectObject(memDC, pState->hFontSmall);
                                    SetTextColor(memDC, item.isSelected ? RGB(210, 230, 255) : RGB(180, 180, 180));
                                    RECT rcBadge = { item.rcRow.right - 52, item.rcRow.top, item.rcRow.right - 4, item.rcRow.bottom };
                                    DrawTextW(memDC, badge.c_str(), -1, &rcBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                                }
                            }
                        }

                        itemY += rowH;
                    }
                }

                SelectClipRgn(memDC, NULL);
                DeleteObject(hRgnSidebarClip);
                pState->m_sidebarScroll.Paint(memDC, CLR_INVALID, RGB(22, 24, 28));

                // =========================================================================
                // 3. RIGHT PRESET CARDS CANVAS (Filtered by selectedCategory)
                // =========================================================================
                int rightX = sidebarW + splitterW + 16;
                int rightW = rcClient.right - rightX - 16;
                RECT rcCanvas = { rightX, topY + 10, rcClient.right - 16, rcClient.bottom - 16 };
                HRGN clipRgn = CreateRectRgn(rcCanvas.left, rcCanvas.top, rcCanvas.right, rcCanvas.bottom);
                SelectClipRgn(memDC, clipRgn);

                int curY = rcCanvas.top + 6 - pState->scrollYTab0;

                std::vector<int> visibleCards;
                for (int c = 0; c < (int)pState->presetCards.size(); ++c)
                {
                    const auto& card = pState->presetCards[c];
                    if (pState->selectedCategory.empty() || card.category == pState->selectedCategory)
                    {
                        visibleCards.push_back(c);
                    }
                }

                if (visibleCards.empty())
                {
                    RECT rcEmpty = { rcCanvas.left + 40, rcCanvas.top + 50, rcCanvas.right - 40, rcCanvas.top + 210 };
                    HBRUSH hbrEmpty = CreateSolidBrush(RGB(24, 24, 24));
                    HPEN hpenEmpty = CreatePen(PS_SOLID, 1, RGB(42, 42, 42));
                    HGDIOBJ oB = SelectObject(memDC, hbrEmpty);
                    HGDIOBJ oP = SelectObject(memDC, hpenEmpty);
                    RoundRect(memDC, rcEmpty.left, rcEmpty.top, rcEmpty.right, rcEmpty.bottom, 12, 12);
                    SelectObject(memDC, oB); SelectObject(memDC, oP);
                    DeleteObject(hbrEmpty); DeleteObject(hpenEmpty);

                    SelectObject(memDC, pState->hFontIcon);
                    SetTextColor(memDC, RGB(100, 180, 255));
                    RECT rcEIcon = { rcEmpty.left, rcEmpty.top + 24, rcEmpty.right, rcEmpty.top + 56 };
                    DrawTextW(memDC, L"\xE8D6", -1, &rcEIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(memDC, pState->hFontTitle);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    RECT rcETitle = { rcEmpty.left + 20, rcEmpty.top + 60, rcEmpty.right - 20, rcEmpty.top + 86 };
                    std::wstring emptyTitle = pState->selectedCategory.empty() ? L"No Stream Preset Cards Found" : (L"Folder '" + pState->selectedCategory + L"' is Empty");
                    DrawTextW(memDC, emptyTitle.c_str(), -1, &rcETitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(memDC, pState->hFontSmall);
                    SetTextColor(memDC, RGB(160, 160, 160));
                    RECT rcEDesc = { rcEmpty.left + 30, rcEmpty.top + 90, rcEmpty.right - 30, rcEmpty.top + 130 };
                    DrawTextW(memDC, L"Click '+ New Preset Card' in the toolbar above to create a preset here, or drag and drop presets into this folder from the left sidebar.", -1, &rcEDesc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                }
                else
                {
                    for (int cIdx : visibleCards)
                    {
                        auto& card = pState->presetCards[cIdx];
                        int streamCount = (int)card.streams.size();
                        int entriesAreaH = (streamCount == 0) ? 42 : (12 + ((streamCount + 1) / 2) * 32 + 6);
                        int cardH = card.isCollapsed ? 34 : (42 + entriesAreaH + 36);

                        RECT rcCard = { rcCanvas.left, curY, rcCanvas.right - 14, curY + cardH };
                        curY += cardH + 12;

                        bool isSelectedCard = (cIdx == pState->selectedPresetCardIdx);
                        HBRUSH cBr = CreateSolidBrush(PoolTheme::CardBackground);
                        HPEN cPen = CreatePen(PS_SOLID, isSelectedCard ? 2 : 1, isSelectedCard ? RGB(0, 140, 240) : PoolTheme::CardBorder);
                        HGDIOBJ oB = SelectObject(memDC, cBr);
                        HGDIOBJ oP = SelectObject(memDC, cPen);
                        RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 8, 8);
                        SelectObject(memDC, oB); SelectObject(memDC, oP);
                        DeleteObject(cBr); DeleteObject(cPen);

                        // Card Header Row
                        RECT rcCardHdr = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 34 };

                        SmsStreamMutatorState::CardHit ch;
                        ch.cardIdx = cIdx;
                        ch.rcCard = rcCard;
                        ch.rcHeader = rcCardHdr;

                        // Header collapse chevron button
                        ch.rcChevron = { rcCard.left + 8, rcCard.top + 5, rcCard.left + 30, rcCard.top + 29 };
                        DrawModernButton(memDC, ch.rcChevron, L"", PtInRect(&ch.rcChevron, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, card.isCollapsed ? L"__TRI_DOWN__" : L"__TRI_UP__");

                        // Card Title: "#1  {Name}"
                        RECT rcTitle = { rcCard.left + 36, rcCard.top, rcCard.right - 260, rcCard.top + 34 };
                        SelectObject(memDC, pState->hFontBold);
                        SetTextColor(memDC, isSelectedCard ? RGB(80, 180, 255) : PoolTheme::TextPrimary);
                        std::wstring titleText = L"#" + std::to_wstring(cIdx + 1) + L"  " + card.name;
                        DrawTextW(memDC, titleText.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                        // Action buttons on card header right (from right to left)
                        int hBtnR = rcCard.right - 8;
                        int hBtnY = rcCard.top + 5;
                        int hBtnH = 24;

                        // [ Delete ]
                        hBtnR -= 28;
                        ch.rcBtnDelete = { hBtnR, hBtnY, hBtnR + 26, hBtnY + hBtnH };
                        DrawModernButton(memDC, ch.rcBtnDelete, L"", PtInRect(&ch.rcBtnDelete, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE74D");

                        // [ Down ]
                        hBtnR -= 26;
                        ch.rcBtnMoveDown = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                        DrawModernButton(memDC, ch.rcBtnMoveDown, L"", PtInRect(&ch.rcBtnMoveDown, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_DOWN__");

                        // [ Up ]
                        hBtnR -= 26;
                        ch.rcBtnMoveUp = { hBtnR, hBtnY, hBtnR + 24, hBtnY + hBtnH };
                        DrawModernButton(memDC, ch.rcBtnMoveUp, L"", PtInRect(&ch.rcBtnMoveUp, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"__TRI_UP__");

                        // [ Clone ]
                        hBtnR -= 84;
                        ch.rcBtnClone = { hBtnR, hBtnY, hBtnR + 80, hBtnY + hBtnH };
                        DrawModernButton(memDC, ch.rcBtnClone, L"Clone", PtInRect(&ch.rcBtnClone, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE8C8");

                        // [ Rename ]
                        hBtnR -= 92;
                        ch.rcBtnRename = { hBtnR, hBtnY, hBtnR + 88, hBtnY + hBtnH };
                        DrawModernButton(memDC, ch.rcBtnRename, L"Rename", PtInRect(&ch.rcBtnRename, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE70F");

                        // Render streams inside card when expanded
                        if (!card.isCollapsed)
                        {
                            int boxY = rcCard.top + 38;
                            RECT rcBox = { rcCard.left + 14, boxY, rcCard.right - 14, boxY + entriesAreaH };

                            HBRUSH hbrBox = CreateSolidBrush(PoolTheme::UnitsBoxBackground);
                            HPEN hpenBox = CreatePen(PS_SOLID, 1, PoolTheme::UnitsBoxBorder);
                            oB = SelectObject(memDC, hbrBox); oP = SelectObject(memDC, hpenBox);
                            RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 6, 6);
                            SelectObject(memDC, oB); SelectObject(memDC, oP);
                            DeleteObject(hbrBox); DeleteObject(hpenBox);

                            if (streamCount == 0)
                            {
                                SelectObject(memDC, pState->hFontSmall);
                                SetTextColor(memDC, PoolTheme::TextMuted);
                                DrawTextW(memDC, L"Click '+ Add Stream' below to add sound streams into this preset card.", -1, &rcBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            }
                            else
                            {
                                int colTotalW = (rcBox.right - rcBox.left - 24) / 2;
                                for (int s = 0; s < streamCount; ++s)
                                {
                                    const auto& st = card.streams[s];
                                    int col = s % 2;
                                    int row = s / 2;
                                    int chipX = rcBox.left + 8 + col * (colTotalW + 8);
                                    int chipY = rcBox.top + 6 + row * 32;

                                    RECT rcChip = { chipX, chipY, chipX + colTotalW, chipY + 28 };

                                    HBRUSH hbrChip = CreateSolidBrush(RGB(32, 34, 42));
                                    HPEN hpenChip = CreatePen(PS_SOLID, 1, RGB(48, 52, 64));
                                    oB = SelectObject(memDC, hbrChip); oP = SelectObject(memDC, hpenChip);
                                    RoundRect(memDC, rcChip.left, rcChip.top, rcChip.right, rcChip.bottom, 4, 4);
                                    SelectObject(memDC, oB); SelectObject(memDC, oP);
                                    DeleteObject(hbrChip); DeleteObject(hpenChip);

                                    // Audio Icon
                                    SelectObject(memDC, pState->hFontIconSmall);
                                    SetTextColor(memDC, RGB(96, 205, 255));
                                    RECT rcAudio = { rcChip.left + 6, rcChip.top, rcChip.left + 22, rcChip.bottom };
                                    DrawTextW(memDC, L"\xE8D6", -1, &rcAudio, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Stream Title / Primary Wav
                                    SelectObject(memDC, pState->hFontSmall);
                                    SetTextColor(memDC, RGB(240, 240, 240));
                                    std::wstring streamLabel = st.name.empty() ? st.primaryWav : st.name;
                                    RECT rcChipText = { rcChip.left + 24, rcChip.top, rcChip.right - 80, rcChip.bottom };
                                    DrawTextW(memDC, streamLabel.c_str(), -1, &rcChipText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                    // Priority & Volume Badge
                                    std::wstring badgeText = L"P:" + std::to_wstring(st.priority) + L" V:" + std::to_wstring((int)(st.volume * 100)) + L"%";
                                    SelectObject(memDC, pState->hFontBadge);
                                    SetTextColor(memDC, RGB(160, 170, 185));
                                    RECT rcBadge = { rcChip.right - 78, rcChip.top, rcChip.right - 26, rcChip.bottom };
                                    DrawTextW(memDC, badgeText.c_str(), -1, &rcBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Delete stream button
                                    RECT rcChipDel = { rcChip.right - 24, rcChip.top + 3, rcChip.right - 4, rcChip.bottom - 3 };
                                    DrawModernButton(memDC, rcChipDel, L"", PtInRect(&rcChipDel, pState->ptMouse), false, false, pState->hFontBadge, pState->hFontIconSmall, L"\xE711");

                                    SmsStreamMutatorState::StreamChipHit scHit;
                                    scHit.cardIdx = cIdx;
                                    scHit.streamIdx = s;
                                    scHit.rcChip = rcChip;
                                    scHit.rcBtnRemove = rcChipDel;
                                    pState->chipHits.push_back(scHit);
                                }
                            }

                            // Card Bottom Action Buttons
                            int actY = rcBox.bottom + 6;
                            int actX = rcCard.left + 14;

                            ch.rcBtnAddStream = { actX, actY, actX + 115, actY + 24 };
                            DrawModernButton(memDC, ch.rcBtnAddStream, L"Add Stream", PtInRect(&ch.rcBtnAddStream, pState->ptMouse), false, true, pState->hFontSmall, pState->hFontIconSmall, L"\xE710");
                            actX += 122;

                            bool hasClipboard = !SmsClipboard_Get().streams.empty();
                            ch.rcBtnPasteStream = { actX, actY, actX + 115, actY + 24 };
                            DrawModernButton(memDC, ch.rcBtnPasteStream, L"Paste Stream", PtInRect(&ch.rcBtnPasteStream, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE77F");
                            actX += 122;

                            ch.rcBtnClearAll = { actX, actY, actX + 95, actY + 24 };
                            DrawModernButton(memDC, ch.rcBtnClearAll, L"Clear All", PtInRect(&ch.rcBtnClearAll, pState->ptMouse), false, false, pState->hFontSmall, pState->hFontIconSmall, L"\xE75C");
                        }

                        pState->cardHits.push_back(ch);
                    }
                }

                SelectClipRgn(memDC, NULL);
                DeleteObject(clipRgn);

                RECT rcScroll0 = { rcCanvas.right - 8, rcCanvas.top, rcCanvas.right, rcCanvas.bottom };
                pState->m_vScrollTab0.SetBounds(rcScroll0);
                pState->totalHeightTab0 = curY + pState->scrollYTab0 - rcCanvas.top + 20;
                pState->m_vScrollTab0.SetRange(0, pState->totalHeightTab0, rcCanvas.bottom - rcCanvas.top);
                pState->m_vScrollTab0.Paint(memDC);
            }
            // ---------------- TAB 1: Batch Stream Mutator ----------------
            if (pState->activeTab == 1)
            {
                int panelSplitX = (rcClient.right * 55) / 100;

                // Left Panel Toolbar: [ + Add SMS Files... ] [ Clear List ] | [ All ] [ Engines Only ] [ Wagons Only ] | Status Counter (Right Corner)
                int addW = 150;
                int clearW = 115;
                pState->rcBtnAddFiles = { 24, btnY, 24 + addW, btnY + 32 };
                pState->rcBtnClearFiles = { 24 + addW + 8, btnY, 24 + addW + 8 + clearW, btnY + 32 };

                DrawModernButton(memDC, pState->rcBtnAddFiles, L"Add SMS Files...", PtInRect(&pState->rcBtnAddFiles, pState->ptMouse), false, true, pState->hFontBold, pState->hFontIcon, L"\xE710");
                DrawModernButton(memDC, pState->rcBtnClearFiles, L"Clear List", PtInRect(&pState->rcBtnClearFiles, pState->ptMouse), false, false, pState->hFontBold, pState->hFontIcon, L"\xE74D");

                // Segmented Filter Pills in the center
                int filterX = pState->rcBtnClearFiles.right + 12;
                pState->rcBtnFilterAll = { filterX, btnY, filterX + 46, btnY + 32 };
                pState->rcBtnFilterEngines = { filterX + 50, btnY, filterX + 152, btnY + 32 };
                pState->rcBtnFilterWagons = { filterX + 156, btnY, filterX + 258, btnY + 32 };

                auto drawFilterPill = [&](const RECT& rc, const wchar_t* label, bool isActive) {
                    bool isHover = PtInRect(&rc, pState->ptMouse);
                    COLORREF bgCol = isActive ? RGB(0, 120, 215) : (isHover ? RGB(45, 50, 65) : RGB(30, 32, 42));
                    COLORREF borderCol = isActive ? RGB(60, 160, 255) : (isHover ? RGB(75, 82, 105) : RGB(50, 54, 70));
                    COLORREF textCol = isActive ? RGB(255, 255, 255) : (isHover ? RGB(230, 235, 245) : RGB(160, 170, 190));
                    HBRUSH br = CreateSolidBrush(bgCol);
                    HPEN pn = CreatePen(PS_SOLID, 1, borderCol);
                    HGDIOBJ ob = SelectObject(memDC, br);
                    HGDIOBJ op = SelectObject(memDC, pn);
                    RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                    SelectObject(memDC, ob); SelectObject(memDC, op);
                    DeleteObject(br); DeleteObject(pn);
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, textCol);
                    RECT rcTxt = rc;
                    DrawTextW(memDC, label, -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };

                drawFilterPill(pState->rcBtnFilterAll, L"All", pState->targetFilter == TargetFilterMode::All);
                drawFilterPill(pState->rcBtnFilterEngines, L"Engines Only", pState->targetFilter == TargetFilterMode::EnginesOnly);
                drawFilterPill(pState->rcBtnFilterWagons, L"Wagons Only", pState->targetFilter == TargetFilterMode::WagonsOnly);

                int selectedCount = 0;
                int visibleCount = 0;
                for (const auto& t : pState->targetFiles)
                {
                    if (pState->targetFilter == TargetFilterMode::EnginesOnly && t.section != TargetSection::Engine) continue;
                    if (pState->targetFilter == TargetFilterMode::WagonsOnly && t.section != TargetSection::Wagon) continue;
                    visibleCount++;
                    if (t.isSelected) selectedCount++;
                }

                // Selection counter badge positioned at the far right corner of the header toolbar
                std::wstring countBadge = std::to_wstring(selectedCount) + L" / " + std::to_wstring(visibleCount) + L" SMS Selected";
                RECT rcCounter = { rcClient.right - 260, btnY, rcClient.right - 24, btnY + 32 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, selectedCount > 0 ? RGB(140, 200, 255) : RGB(150, 155, 170));
                DrawTextW(memDC, countBadge.c_str(), -1, &rcCounter, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Grid-Style SMS Table Header Bar (matching consist workspace grid)
                int gridHdrY = toolbarY + toolbarH + 8;
                int gridHdrH = 30;
                int listRight = panelSplitX - 26; // Exact match with item cards accounting for scrollbar gutter
                pState->rcGridHeader = { 24, gridHdrY, listRight, gridHdrY + gridHdrH };

                HBRUSH ghBr = CreateSolidBrush(RGB(24, 26, 33));
                HPEN ghPen = CreatePen(PS_SOLID, 1, RGB(48, 52, 65));
                HGDIOBJ oldGhB = SelectObject(memDC, ghBr);
                HGDIOBJ oldGhP = SelectObject(memDC, ghPen);
                RoundRect(memDC, pState->rcGridHeader.left, pState->rcGridHeader.top, pState->rcGridHeader.right, pState->rcGridHeader.bottom, 4, 4);
                SelectObject(memDC, oldGhB); SelectObject(memDC, oldGhP);
                DeleteObject(ghBr); DeleteObject(ghPen);

                // Master Tri-State Checkbox
                bool isAllSelected = (visibleCount > 0 && selectedCount == visibleCount);
                bool isIndet = (selectedCount > 0 && !isAllSelected);
                int chkMasterY = pState->rcGridHeader.top + (gridHdrH - 18) / 2;
                pState->rcGridMasterCheck = { pState->rcGridHeader.left + 8, chkMasterY, pState->rcGridHeader.left + 26, chkMasterY + 18 };
                DrawCustomCheckbox(memDC, pState->rcGridMasterCheck, isAllSelected, PtInRect(&pState->rcGridMasterCheck, pState->ptMouse), pState->hFontBold, isIndet);

                // Grid Columns Text (aligned with rows below)
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(145, 160, 185));

                RECT rcCol1 = { pState->rcGridHeader.left + 52, pState->rcGridHeader.top, pState->rcGridHeader.left + 220, pState->rcGridHeader.bottom };
                DrawTextW(memDC, L"SMS TARGET FILE", -1, &rcCol1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                RECT rcCol2 = { pState->rcGridHeader.left + 230, pState->rcGridHeader.top, pState->rcGridHeader.right - 140, pState->rcGridHeader.bottom };
                DrawTextW(memDC, L"TRAINSET SUBFOLDER", -1, &rcCol2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                RECT rcCol3 = { pState->rcGridHeader.right - 130, pState->rcGridHeader.top, pState->rcGridHeader.right - 10, pState->rcGridHeader.bottom };
                DrawTextW(memDC, L"STREAMS COUNT", -1, &rcCol3, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Files Canvas
                RECT rcFilesCanvas = { 24, pState->rcGridHeader.bottom + 4, panelSplitX - 12, rcClient.bottom - 70 };
                HRGN clipRgn1 = CreateRectRgn(rcFilesCanvas.left, rcFilesCanvas.top, rcFilesCanvas.right, rcFilesCanvas.bottom);
                SelectClipRgn(memDC, clipRgn1);

                int fileY = rcFilesCanvas.top + 4 - pState->scrollYTab1;

                if (pState->targetFiles.empty())
                {
                    RECT rcEmpty = { rcFilesCanvas.left + 10, rcFilesCanvas.top + 60, rcFilesCanvas.right - 10, rcFilesCanvas.top + 140 };
                    SelectObject(memDC, pState->hFontBody);
                    SetTextColor(memDC, RGB(130, 140, 160));
                    DrawTextW(memDC, L"No SMS files added yet.\r\nClick [Add SMS Files...] or select consists/units to view sounds.", -1, &rcEmpty, DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                }
                else
                {
                    // Lambda to render a collapsible enclosed section card (matching Sound Matrix styling)
                    auto renderSection = [&](TargetSection sec, const wchar_t* secTitle, const wchar_t* secIcon, bool& bExpanded, RECT& outHdr, RECT& outChk, RECT& outChev, COLORREF bgCardHdr, COLORREF brdCardHdr, COLORREF accentColor) {
                        int secTotal = 0;
                        int secSel = 0;
                        for (const auto& t : pState->targetFiles)
                        {
                            if (t.section == sec)
                            {
                                secTotal++;
                                if (t.isSelected) secSel++;
                            }
                        }

                        int hdrH = 38;
                        int cardTotalH = hdrH;
                        if (bExpanded)
                        {
                            if (secTotal == 0)
                            {
                                cardTotalH += 9 + 26 + 9;
                            }
                            else
                            {
                                int itemsH = 0;
                                for (int f = 0; f < (int)pState->targetFiles.size(); ++f)
                                {
                                    const auto& tgt = pState->targetFiles[f];
                                    if (tgt.section != sec) continue;
                                    int itemH = 50; // Two-line row height
                                    if (tgt.isExpanded && tgt.isLoaded)
                                    {
                                        if (!tgt.usageRefs.empty())
                                        {
                                            itemH += 26 + (int)tgt.usageRefs.size() * 20 + 8;
                                        }
                                        if (tgt.doc.activeGroupIndex >= 0 && tgt.doc.activeGroupIndex < (int)tgt.doc.groups.size())
                                        {
                                            itemH += 30 + (int)tgt.doc.groups[tgt.doc.activeGroupIndex].streams.size() * 26;
                                        }
                                    }
                                    itemsH += itemH + 6;
                                }
                                if (itemsH > 0) itemsH -= 6; // remove trailing gap
                                cardTotalH += 9 + itemsH + 9;
                            }
                        }

                        RECT rcCard = { rcFilesCanvas.left, fileY, listRight, fileY + cardTotalH };

                        // 1. Outer Enclosed Card Container (Shell)
                        HBRUSH hbrCard = CreateSolidBrush(RGB(22, 26, 34));
                        HPEN hpCardBdr = CreatePen(PS_SOLID, 1, RGB(46, 54, 68));
                        HGDIOBJ ob1 = SelectObject(memDC, hbrCard);
                        HGDIOBJ op1 = SelectObject(memDC, hpCardBdr);
                        RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
                        SelectObject(memDC, ob1); SelectObject(memDC, op1);
                        DeleteObject(hbrCard); DeleteObject(hpCardBdr);

                        // 2. Distinct Card Header (38px tall)
                        outHdr = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + hdrH };

                        HBRUSH sBr = CreateSolidBrush(bgCardHdr);
                        HPEN sPen = CreatePen(PS_SOLID, 1, brdCardHdr);
                        HGDIOBJ oB = SelectObject(memDC, sBr);
                        HGDIOBJ oP = SelectObject(memDC, sPen);
                        RoundRect(memDC, outHdr.left, outHdr.top, outHdr.right, outHdr.bottom + (bExpanded ? 6 : 0), 6, 6);
                        SelectObject(memDC, oB); SelectObject(memDC, oP);
                        DeleteObject(sBr); DeleteObject(sPen);

                        // Accent strip on left of header (4px solid bright indicator - matches Sound Matrix)
                        RECT rcAccent = { outHdr.left + 1, outHdr.top + 3, outHdr.left + 5, outHdr.bottom - 3 };
                        HBRUSH hbrAcc = CreateSolidBrush(accentColor);
                        FillRect(memDC, &rcAccent, hbrAcc);
                        DeleteObject(hbrAcc);

                        // Section Multi-select Checkbox
                        outChk = { outHdr.left + 12, outHdr.top + 10, outHdr.left + 30, outHdr.top + 28 };
                        bool isSecAll = (secTotal > 0 && secSel == secTotal);
                        bool isSecIndet = (secSel > 0 && !isSecAll);
                        DrawCustomCheckbox(memDC, outChk, isSecAll, PtInRect(&outChk, pState->ptMouse), pState->hFontBold, isSecIndet);

                        // Section Chevron
                        outChev = { outHdr.left + 34, outHdr.top, outHdr.left + 52, outHdr.bottom };
                        SelectObject(memDC, pState->hFontBold);
                        SetTextColor(memDC, RGB(160, 205, 255));
                        DrawTextW(memDC, bExpanded ? L"▼" : L"▶", -1, &outChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // Section Title & Icon
                        SelectObject(memDC, pState->hFontBold);
                        SetTextColor(memDC, RGB(255, 255, 255));
                        std::wstring fullTitle = std::wstring(secIcon) + L"  " + secTitle;
                        RECT rcTitle = { outHdr.left + 58, outHdr.top, outHdr.right - 140, outHdr.bottom };
                        DrawTextW(memDC, fullTitle.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // Section Count Badge
                        RECT rcSecBadge = { outHdr.right - 130, outHdr.top + 4, outHdr.right - 12, outHdr.bottom - 4 };
                        std::wstring bTxt = std::to_wstring(secSel) + L" / " + std::to_wstring(secTotal) + L" Selected";
                        SelectObject(memDC, pState->hFontSmall);
                        SetTextColor(memDC, secSel > 0 ? accentColor : RGB(140, 145, 160));
                        DrawTextW(memDC, bTxt.c_str(), -1, &rcSecBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // 3. Render Items Enclosed inside the card
                        if (bExpanded)
                        {
                            int itemY = outHdr.bottom + 9;
                            if (secTotal == 0)
                            {
                                RECT rcEmptySec = { rcCard.left + 16, itemY, rcCard.right - 16, itemY + 22 };
                                SelectObject(memDC, pState->hFontSmall);
                                SetTextColor(memDC, RGB(120, 130, 145));
                                DrawTextW(memDC, L"(No sound sets referenced in this category)", -1, &rcEmptySec, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            }
                            else
                            {
                                for (int f = 0; f < (int)pState->targetFiles.size(); ++f)
                                {
                                    auto& tgt = pState->targetFiles[f];
                                    if (tgt.section != sec) continue;

                                    int itemH = 50; // Two-line row
                                    if (tgt.isExpanded && tgt.isLoaded)
                                    {
                                        if (!tgt.usageRefs.empty())
                                        {
                                            itemH += 26 + (int)tgt.usageRefs.size() * 20 + 8;
                                        }
                                        if (tgt.doc.activeGroupIndex >= 0 && tgt.doc.activeGroupIndex < (int)tgt.doc.groups.size())
                                        {
                                            itemH += 30 + (int)tgt.doc.groups[tgt.doc.activeGroupIndex].streams.size() * 26;
                                        }
                                    }

                                    // Inset 9px from left and right of outer card
                                    RECT rcItem = { rcCard.left + 9, itemY, rcCard.right - 9, itemY + itemH };
                                    itemY += itemH + 6;

                                    HBRUSH itBr = CreateSolidBrush(tgt.isSelected ? RGB(26, 36, 52) : RGB(28, 32, 42));
                                    HPEN itPen = CreatePen(PS_SOLID, 1, tgt.isSelected ? RGB(0, 140, 240) : RGB(48, 52, 65));
                                    HGDIOBJ ioB = SelectObject(memDC, itBr); HGDIOBJ ioP = SelectObject(memDC, itPen);
                                    RoundRect(memDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 4, 4);
                                    SelectObject(memDC, ioB); SelectObject(memDC, ioP);
                                    DeleteObject(itBr); DeleteObject(itPen);

                                    // File Header row (50px tall for two lines)
                                    RECT rcHdr = { rcItem.left, rcItem.top, rcItem.right, rcItem.top + 50 };

                                    // Line 1: Checkbox
                                    RECT rcChk = { rcHdr.left + 8, rcHdr.top + 7, rcHdr.left + 26, rcHdr.top + 25 };
                                    DrawCustomCheckbox(memDC, rcChk, tgt.isSelected, PtInRect(&rcChk, pState->ptMouse));

                                    // Line 1: Chevron
                                    RECT rcChev = { rcHdr.left + 32, rcHdr.top + 3, rcHdr.left + 48, rcHdr.top + 27 };
                                    SelectObject(memDC, pState->hFontBold);
                                    SetTextColor(memDC, RGB(140, 180, 240));
                                    DrawTextW(memDC, tgt.isExpanded ? L"▼" : L"▶", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Line 1: File name
                                    SelectObject(memDC, pState->hFontBold);
                                    SetTextColor(memDC, RGB(255, 255, 255));
                                    RECT rcFname = { rcHdr.left + 52, rcHdr.top + 3, rcHdr.left + 215, rcHdr.top + 27 };
                                    DrawTextW(memDC, tgt.fileName.c_str(), -1, &rcFname, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                    // Line 1: Dedicated Slot Badge (Immediate visibility)
                                    std::wstring slotLabel = L"";
                                    for (const auto& r : tgt.usageRefs)
                                    {
                                        if (!r.soundSlot.empty()) { slotLabel = r.soundSlot; break; }
                                    }
                                    if (slotLabel.empty())
                                    {
                                        slotLabel = (tgt.section == TargetSection::Engine) ? L"Engine Sound" : L"Wagon Sound";
                                    }

                                    COLORREF slotBg = RGB(16, 42, 68);
                                    COLORREF slotBdr = RGB(30, 95, 160);
                                    COLORREF slotFg = RGB(120, 200, 255);
                                    const wchar_t* slotIcon = L"🔊";

                                    if (slotLabel.find(L"Cab") != std::wstring::npos)
                                    {
                                        slotBg = RGB(40, 22, 60); slotBdr = RGB(110, 55, 160); slotFg = RGB(220, 165, 255);
                                        slotIcon = L"🎧";
                                    }
                                    else if (slotLabel.find(L"Wagon") != std::wstring::npos || slotLabel.find(L"Tender") != std::wstring::npos)
                                    {
                                        slotBg = RGB(16, 45, 30); slotBdr = RGB(35, 115, 75); slotFg = RGB(120, 230, 160);
                                        slotIcon = L"🚃";
                                    }
                                    else if (slotLabel.find(L"Interior") != std::wstring::npos || slotLabel.find(L"Inside") != std::wstring::npos)
                                    {
                                        slotBg = RGB(50, 36, 16); slotBdr = RGB(140, 95, 30); slotFg = RGB(255, 205, 110);
                                        slotIcon = L"💺";
                                    }

                                    SelectObject(memDC, pState->hFontSmall);
                                    std::wstring slotTxt = std::wstring(slotIcon) + L" " + slotLabel;
                                    SIZE szSlot = { 0, 0 };
                                    GetTextExtentPoint32W(memDC, slotTxt.c_str(), (int)slotTxt.length(), &szSlot);
                                    int badgeW = max(90, szSlot.cx + 18);
                                    RECT rcSlotBadge = { rcHdr.left + 220, rcHdr.top + 5, rcHdr.left + 220 + badgeW, rcHdr.top + 25 };

                                    HBRUSH sBBr = CreateSolidBrush(slotBg); HPEN sBPn = CreatePen(PS_SOLID, 1, slotBdr);
                                    HGDIOBJ sOB = SelectObject(memDC, sBBr); HGDIOBJ sOP = SelectObject(memDC, sBPn);
                                    RoundRect(memDC, rcSlotBadge.left, rcSlotBadge.top, rcSlotBadge.right, rcSlotBadge.bottom, 3, 3);
                                    SelectObject(memDC, sOB); SelectObject(memDC, sOP);
                                    DeleteObject(sBBr); DeleteObject(sBPn);

                                    SetTextColor(memDC, slotFg);
                                    DrawTextW(memDC, slotTxt.c_str(), -1, &rcSlotBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Line 1: Trainset Subfolder
                                    SelectObject(memDC, pState->hFontSmall);
                                    SetTextColor(memDC, RGB(140, 160, 185));
                                    RECT rcFolder = { rcSlotBadge.right + 10, rcHdr.top + 3, rcHdr.right - 140, rcHdr.top + 27 };
                                    DrawTextW(memDC, tgt.folderName.c_str(), -1, &rcFolder, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                    // Line 1: Stream Count badge
                                    RECT rcBadge = { rcHdr.right - 130, rcHdr.top + 5, rcHdr.right - 10, rcHdr.top + 25 };
                                    std::wstring bTxtItem = std::to_wstring(tgt.declaredStreams) + L" decl / " + std::to_wstring(tgt.parsedStreams) + L" parsed";
                                    COLORREF bgB = tgt.hasMismatch ? RGB(55, 36, 16) : RGB(20, 48, 26);
                                    COLORREF brdB = tgt.hasMismatch ? RGB(220, 150, 30) : RGB(45, 125, 55);
                                    COLORREF txtB = tgt.hasMismatch ? RGB(255, 210, 120) : RGB(100, 225, 130);

                                    HBRUSH bBr = CreateSolidBrush(bgB); HPEN bPn = CreatePen(PS_SOLID, 1, brdB);
                                    HGDIOBJ boB = SelectObject(memDC, bBr); HGDIOBJ boP = SelectObject(memDC, bPn);
                                    RoundRect(memDC, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 3, 3);
                                    SelectObject(memDC, boB); SelectObject(memDC, boP);
                                    DeleteObject(bBr); DeleteObject(bPn);

                                    SetTextColor(memDC, txtB);
                                    DrawTextW(memDC, bTxtItem.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Line 2: Secondary Provenance Sub-line (Consist and Units - slot is already shown above!)
                                    RECT rcSubLine = { rcHdr.left + 52, rcHdr.top + 27, rcHdr.right - 12, rcHdr.top + 47 };
                                    std::wstring usageStr = FormatSmsUsageSummary(tgt.usageRefs);
                                    SelectObject(memDC, pState->hFontSmall);
                                    SetTextColor(memDC, RGB(135, 175, 215));
                                    DrawTextW(memDC, usageStr.c_str(), -1, &rcSubLine, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                    SmsStreamMutatorState::TargetFileHit tfh;
                                    tfh.fileIdx = f;
                                    tfh.rcHeader = rcHdr;
                                    tfh.rcCheck = rcChk;
                                    tfh.rcChevron = rcChev;
                                    pState->targetHits.push_back(tfh);

                                    // Expanded content: Bound Units in Consists box + Audio Streams list
                                    if (tgt.isExpanded && tgt.isLoaded)
                                    {
                                        int stY = rcHdr.bottom + 6;

                                        // Detailed Reference Box (Solution 1B)
                                        if (!tgt.usageRefs.empty())
                                        {
                                            int refBoxH = 24 + (int)tgt.usageRefs.size() * 20;
                                            RECT rcRefBox = { rcItem.left + 16, stY, rcItem.right - 16, stY + refBoxH };
                                            stY += refBoxH + 8;

                                            HBRUSH rBr = CreateSolidBrush(RGB(18, 22, 30));
                                            HPEN rPen = CreatePen(PS_SOLID, 1, RGB(40, 52, 70));
                                            HGDIOBJ roB = SelectObject(memDC, rBr); HGDIOBJ roP = SelectObject(memDC, rPen);
                                            RoundRect(memDC, rcRefBox.left, rcRefBox.top, rcRefBox.right, rcRefBox.bottom, 4, 4);
                                            SelectObject(memDC, roB); SelectObject(memDC, roP);
                                            DeleteObject(rBr); DeleteObject(rPen);

                                            RECT rcRefHdr = { rcRefBox.left + 10, rcRefBox.top + 4, rcRefBox.right - 10, rcRefBox.top + 22 };
                                            SelectObject(memDC, pState->hFontBold);
                                            SetTextColor(memDC, RGB(140, 190, 240));
                                            DrawTextW(memDC, L"📌 Bound Units in Consists:", -1, &rcRefHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                            int rowY = rcRefBox.top + 22;
                                            SelectObject(memDC, pState->hFontSmall);
                                            for (const auto& r : tgt.usageRefs)
                                            {
                                                RECT rcRow = { rcRefBox.left + 18, rowY, rcRefBox.right - 10, rowY + 18 };
                                                rowY += 20;
                                                std::wstring line = L"• " + (r.consistName.empty() ? L"(Active Consist)" : r.consistName) +
                                                    L"  ➔  #" + std::to_wstring(r.unitIndex) + L" " + r.unitUid +
                                                    (r.soundSlot.empty() ? L"" : (L" [" + r.soundSlot + L"]"));
                                                SetTextColor(memDC, RGB(210, 225, 245));
                                                DrawTextW(memDC, line.c_str(), -1, &rcRow, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                                            }
                                        }

                                        // Streams list
                                        if (tgt.doc.activeGroupIndex >= 0 && tgt.doc.activeGroupIndex < (int)tgt.doc.groups.size())
                                        {
                                            auto& grp = tgt.doc.groups[tgt.doc.activeGroupIndex];
                                            for (size_t s = 0; s < grp.streams.size(); ++s)
                                            {
                                                const auto& str = grp.streams[s];
                                                RECT rcStRow = { rcItem.left + 36, stY, rcItem.right - 16, stY + 22 };
                                                stY += 26;

                                                SetTextColor(memDC, RGB(180, 200, 225));
                                                SelectObject(memDC, pState->hFontSmall);
                                                std::wstring sDesc = L"#" + std::to_wstring(s + 1) + L"  " + str.friendlyTitle +
                                                    L"  (Pri: " + std::to_wstring(str.priority) + L", Vol: " + std::to_wstring((int)(str.volume * 100)) + L"%)";
                                                DrawTextW(memDC, sDesc.c_str(), -1, &rcStRow, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        fileY += cardTotalH + 12; // Gap between cards
                    };

                    // 1. Engines Section (.eng)
                    if (pState->targetFilter != TargetFilterMode::WagonsOnly)
                    {
                        renderSection(TargetSection::Engine, L"Engine Sound Sets (.eng)", L"🚂", pState->bSectionEnginesExpanded,
                            pState->rcSectionEnginesHdr, pState->rcSectionEnginesCheck, pState->rcSectionEnginesChev,
                            RGB(16, 32, 54), RGB(36, 68, 105), RGB(0, 160, 255));
                    }
                    else
                    {
                        pState->rcSectionEnginesHdr = { 0 };
                        pState->rcSectionEnginesCheck = { 0 };
                        pState->rcSectionEnginesChev = { 0 };
                    }

                    // 2. Wagons & Coaches Section (.wag)
                    if (pState->targetFilter != TargetFilterMode::EnginesOnly)
                    {
                        renderSection(TargetSection::Wagon, L"Wagon & Coach Sound Sets (.wag)", L"🚃", pState->bSectionWagonsExpanded,
                            pState->rcSectionWagonsHdr, pState->rcSectionWagonsCheck, pState->rcSectionWagonsChev,
                            RGB(12, 34, 28), RGB(35, 75, 55), RGB(0, 215, 140));
                    }
                    else
                    {
                        pState->rcSectionWagonsHdr = { 0 };
                        pState->rcSectionWagonsCheck = { 0 };
                        pState->rcSectionWagonsChev = { 0 };
                    }
                }

                SelectClipRgn(memDC, NULL);
                DeleteObject(clipRgn1);

                RECT rcScroll1 = { rcFilesCanvas.right - 8, rcFilesCanvas.top, rcFilesCanvas.right, rcFilesCanvas.bottom };
                pState->m_vScrollTab1.SetBounds(rcScroll1);
                pState->totalHeightTab1 = fileY + pState->scrollYTab1 - rcFilesCanvas.top + 20;
                pState->m_vScrollTab1.SetRange(0, pState->totalHeightTab1, rcFilesCanvas.bottom - rcFilesCanvas.top);
                pState->m_vScrollTab1.Paint(memDC);

                // Right Panel: Mutation Settings & Parameters
                RECT rcRightPanel = { panelSplitX + 12, toolbarY + toolbarH + 10, rcClient.right - 24, rcClient.bottom - 70 };
                HBRUSH rpBr = CreateSolidBrush(RGB(24, 22, 25));
                HPEN rpPen = CreatePen(PS_SOLID, 1, PoolTheme::CardBorder);
                HGDIOBJ oB = SelectObject(memDC, rpBr);
                HGDIOBJ oP = SelectObject(memDC, rpPen);
                RoundRect(memDC, rcRightPanel.left, rcRightPanel.top, rcRightPanel.right, rcRightPanel.bottom, 6, 6);
                SelectObject(memDC, oB); SelectObject(memDC, oP);
                DeleteObject(rpBr); DeleteObject(rpPen);

                // Panel Header
                RECT rcRpHdr = { rcRightPanel.left + 16, rcRightPanel.top + 16, rcRightPanel.right - 16, rcRightPanel.top + 40 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, L"MUTATION RECIPE & ACTIONS", -1, &rcRpHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Radio Options
                int radY = rcRightPanel.top + 50;
                auto drawRadio = [&](RECT& outRc, StreamMutationMode mode, const wchar_t* lbl) {
                    outRc = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 24 };
                    RECT rcCirc = { outRc.left, outRc.top + 4, outRc.left + 16, outRc.top + 20 };
                    DrawCustomRadioButton(memDC, rcCirc, pState->mutationMode == mode, PtInRect(&outRc, pState->ptMouse));
                    RECT rcLbl = { outRc.left + 24, outRc.top, outRc.right, outRc.bottom };
                    SelectObject(memDC, pState->hFontBody);
                    SetTextColor(memDC, pState->mutationMode == mode ? RGB(255, 255, 255) : RGB(190, 200, 215));
                    DrawTextW(memDC, lbl, -1, &rcLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    radY += 30;
                };

                drawRadio(pState->rcRadioInject, StreamMutationMode::InjectPreset, L"Inject Streams from Tab 0 Preset Library");
                drawRadio(pState->rcRadioReplace, StreamMutationMode::ReplaceMatching, L"Replace Existing Stream by Name / Trigger");
                drawRadio(pState->rcRadioSwapPath, StreamMutationMode::BatchPathSwap, L"Batch Find & Replace WAV File Paths");
                drawRadio(pState->rcRadioFixCounts, StreamMutationMode::FixCountsOnly, L"Bulk Fix Streams(N) Header Count Mismatches");

                radY += 12;
                if (pState->mutationMode == StreamMutationMode::InjectPreset || pState->mutationMode == StreamMutationMode::ReplaceMatching)
                {
                    // 1. Source Preset Card Dropdown
                    RECT rcPresetLbl = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 18 };
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(180, 195, 220));
                    DrawTextW(memDC, L"Source Preset Card (from Library):", -1, &rcPresetLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    radY += 20;

                    pState->rcPickerPreset = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 30 };
                    std::wstring presetTitle = L"(No Preset Selected - Choose from Library)";
                    if (pState->selectedPresetCardIdx >= 0 && pState->selectedPresetCardIdx < (int)pState->presetCards.size())
                    {
                        const auto& c = pState->presetCards[pState->selectedPresetCardIdx];
                        presetTitle = c.name + L"  (" + std::to_wstring(c.streams.size()) + L" stream" + (c.streams.size() == 1 ? L"" : L"s") + L")";
                    }
                    DrawCustomDropdownPicker(memDC, pState->rcPickerPreset, presetTitle.c_str(), PtInRect(&pState->rcPickerPreset, pState->ptMouse), pState->hFontBody, pState->hFontIcon, L"\xE8D6");
                    radY += 36;

                    // 2. Target SG Dropdown
                    RECT rcSgLbl = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 18 };
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(180, 195, 220));
                    DrawTextW(memDC, L"Target ScalabilityGroup:", -1, &rcSgLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    radY += 20;

                    pState->rcPickerSg = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 30 };
                    std::wstring sgTitle = L"ScalabilityGroup 5 (High Detail - Default)";
                    if (pState->targetSg == 3) sgTitle = L"ScalabilityGroup 3 (Medium Detail)";
                    else if (pState->targetSg == 0) sgTitle = L"ScalabilityGroup 0 (Cab / Interior View)";
                    else if (pState->targetSg == -1) sgTitle = L"All ScalabilityGroups (5, 3, and 0)";
                    DrawCustomDropdownPicker(memDC, pState->rcPickerSg, sgTitle.c_str(), PtInRect(&pState->rcPickerSg, pState->ptMouse), pState->hFontBody, pState->hFontIcon, L"\xE7F4");
                    radY += 36;

                    // 3. Insertion Position Dropdown (for InjectPreset)
                    if (pState->mutationMode == StreamMutationMode::InjectPreset)
                    {
                        RECT rcPosLbl = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 18 };
                        SelectObject(memDC, pState->hFontBold);
                        SetTextColor(memDC, RGB(180, 195, 220));
                        DrawTextW(memDC, L"Insertion Position:", -1, &rcPosLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        radY += 20;

                        std::wstring posTitle = L"At End of Streams (Append)";
                        if (pState->insertPosition == 0) posTitle = L"At Beginning of Streams (Prepend)";
                        else if (pState->insertPosition == 2) posTitle = L"After Specific Stream Number...";

                        pState->rcPickerPos = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 30 };
                        DrawCustomDropdownPicker(memDC, pState->rcPickerPos, posTitle.c_str(), PtInRect(&pState->rcPickerPos, pState->ptMouse), pState->hFontBody, pState->hFontIcon, pState->insertPosition == 0 ? L"\xE8D8" : (pState->insertPosition == 2 ? L"\xE70F" : L"\xE8D7"));
                        radY += 34;

                        if (pState->insertPosition == 2)
                        {
                            RECT rcNumLbl = { rcRightPanel.left + 18, radY, rcRightPanel.left + 170, radY + 26 };
                            SelectObject(memDC, pState->hFontBody);
                            SetTextColor(memDC, RGB(220, 230, 245));
                            DrawTextW(memDC, L"Insert After Stream #:", -1, &rcNumLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            int editW = 60;
                            int editH = 26;
                            int editX = rcNumLbl.right + 6;
                            pState->rcEditInsertAfter = { editX, radY, editX + editW, radY + editH };

                            if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter))
                            {
                                SetWindowPos(pState->hEditInsertAfter, NULL,
                                    pState->rcEditInsertAfter.left, pState->rcEditInsertAfter.top,
                                    editW, editH,
                                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                                RECT rcFmt = { 2, 3, editW - 2, editH - 3 };
                                SendMessageW(pState->hEditInsertAfter, EM_SETRECT, 0, (LPARAM)&rcFmt);
                            }

                            RECT rcHint = { pState->rcEditInsertAfter.right + 10, radY, rcRightPanel.right - 16, radY + 26 };
                            SelectObject(memDC, pState->hFontSmall);
                            SetTextColor(memDC, RGB(140, 150, 165));
                            DrawTextW(memDC, L"(0 = at beginning, auto-clamped)", -1, &rcHint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            radY += 32;
                        }
                        else
                        {
                            if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter) && IsWindowVisible(pState->hEditInsertAfter))
                            {
                                ShowWindow(pState->hEditInsertAfter, SW_HIDE);
                                pState->bEditFocused = false;
                                pState->bEditHovered = false;
                            }
                        }
                    }
                    else
                    {
                        pState->rcPickerPos = { 0 };
                        if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter) && IsWindowVisible(pState->hEditInsertAfter))
                        {
                            ShowWindow(pState->hEditInsertAfter, SW_HIDE);
                            pState->bEditFocused = false;
                            pState->bEditHovered = false;
                        }
                    }
                }
                else
                {
                    pState->rcPickerPreset = { 0 };
                    pState->rcPickerSg = { 0 };
                    pState->rcPickerPos = { 0 };
                    if (pState->hEditInsertAfter && IsWindow(pState->hEditInsertAfter) && IsWindowVisible(pState->hEditInsertAfter))
                    {
                        ShowWindow(pState->hEditInsertAfter, SW_HIDE);
                        pState->bEditFocused = false;
                        pState->bEditHovered = false;
                    }

                    if (pState->mutationMode == StreamMutationMode::FixCountsOnly)
                    {
                        RECT rcFixNote = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 60 };
                        SelectObject(memDC, pState->hFontBody);
                        SetTextColor(memDC, RGB(160, 215, 170));
                        DrawTextW(memDC, L"Synchronizes declared Streams(N) count with actual parsed Stream(...) blocks across all ScalabilityGroups in selected SMS files.\r\nAuto-heals corrupted and mismatched headers safely.", -1, &rcFixNote, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                        radY += 66;
                    }
                    else if (pState->mutationMode == StreamMutationMode::BatchPathSwap)
                    {
                        RECT rcSwapNote = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + 50 };
                        SelectObject(memDC, pState->hFontBody);
                        SetTextColor(memDC, RGB(220, 200, 160));
                        DrawTextW(memDC, L"Searches all File(...) triggers in selected SMS files and replaces WAV folder/sound paths uniformly.", -1, &rcSwapNote, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                        radY += 56;
                    }
                }

                // Backup Checkbox (with 20px breathing space below dropdown pickers)
                radY += 20;
                int chkRowH = 28;
                int chkSize = 18;
                int chkOffset = (chkRowH - chkSize) / 2;

                pState->rcCheckBackup = { rcRightPanel.left + 16, radY, rcRightPanel.right - 16, radY + chkRowH };
                RECT rcChk2 = { pState->rcCheckBackup.left + 2, pState->rcCheckBackup.top + chkOffset, pState->rcCheckBackup.left + 2 + chkSize, pState->rcCheckBackup.top + chkOffset + chkSize };
                DrawCustomCheckbox(memDC, rcChk2, pState->createBackup, PtInRect(&pState->rcCheckBackup, pState->ptMouse), pState->hFontBold);
                RECT rcLbl2 = { pState->rcCheckBackup.left + 28, pState->rcCheckBackup.top, pState->rcCheckBackup.right, pState->rcCheckBackup.bottom };
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, RGB(220, 230, 245));
                DrawTextW(memDC, L"Create .TSCB_BACKUP backup file before overwriting", -1, &rcLbl2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Bottom Action Bar
                int btmY = rcClient.bottom - 54;
                pState->rcBtnPreviewDryRun = { rcClient.right - 380, btmY, rcClient.right - 200, btmY + 36 };
                pState->rcBtnApplyMutation = { rcClient.right - 190, btmY, rcClient.right - 24, btmY + 36 };

                DrawModernButton(memDC, pState->rcBtnPreviewDryRun, L"🔍 Dry Run Preview", PtInRect(&pState->rcBtnPreviewDryRun, pState->ptMouse), false, false, pState->hFontBold);
                DrawModernButton(memDC, pState->rcBtnApplyMutation, L"⚡ Apply Mutation", PtInRect(&pState->rcBtnApplyMutation, pState->ptMouse), false, true, pState->hFontBold);
            }
        }

        // Blit double buffer to screen
        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBm);
        DeleteObject(hbm);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
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

    case WM_DESTROY:
    {
        if (pState)
        {
            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            SaveStreamPresetsToFile(pState);
            if (pState->hFontBody) DeleteObject(pState->hFontBody);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
            if (pState->hFontBadge) DeleteObject(pState->hFontBadge);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
            if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);
            delete pState;
        }
        g_hSmsStreamMutatorDlg = NULL;
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------
void ShowSmsStreamMutatorDialog(
    HWND hWndParent,
    const std::wstring& basePath,
    const std::vector<std::wstring>& initialSmsFiles,
    int initialTab,
    SmsStreamPresetSelectCallback onApplyCallback,
    const std::vector<std::wstring>& initialEngineSmsFiles,
    const std::vector<std::wstring>& initialWagonSmsFiles,
    const std::vector<SmsCategorizedTargetInput>& initialEngineTargetsWithUsage,
    const std::vector<SmsCategorizedTargetInput>& initialWagonTargetsWithUsage,
    const std::wstring& initialFocusedSms
)
{
    if (g_hSmsStreamMutatorDlg && IsWindow(g_hSmsStreamMutatorDlg))
    {
        SetForegroundWindow(g_hSmsStreamMutatorDlg);
        return;
    }

    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.style = CS_DBLCLKS;
        wcex.lpfnWndProc = SmsStreamMutatorProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107 /* IDI_TRAINSIMCONSISTBUILDER */));
        wcex.hIconSm = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (!wcex.hIconSm) wcex.hIconSm = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(108 /* IDI_SMALL */));
        wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wcex.hbrBackground = NULL;
        wcex.lpszClassName = SMS_STREAM_MUTATOR_WNDCLASS;
        RegisterClassExW(&wcex);
        s_registered = true;
    }

    SmsStreamMutatorState* pState = new SmsStreamMutatorState();
    pState->hParent = hWndParent;
    pState->basePath = basePath;
    pState->activeTab = initialTab;
    pState->onApplyCallback = onApplyCallback;

    if (!initialEngineTargetsWithUsage.empty() || !initialWagonTargetsWithUsage.empty())
    {
        for (const auto& item : initialEngineTargetsWithUsage)
        {
            AddSmsFileTarget(pState, item.fullPath, TargetSection::Engine, &item.usage);
        }
        for (const auto& item : initialWagonTargetsWithUsage)
        {
            AddSmsFileTarget(pState, item.fullPath, TargetSection::Wagon, &item.usage);
        }
    }
    else if (!initialEngineSmsFiles.empty() || !initialWagonSmsFiles.empty())
    {
        for (const auto& f : initialEngineSmsFiles)
        {
            AddSmsFileTarget(pState, f, TargetSection::Engine);
        }
        for (const auto& f : initialWagonSmsFiles)
        {
            AddSmsFileTarget(pState, f, TargetSection::Wagon);
        }
    }
    else
    {
        for (const auto& f : initialSmsFiles)
        {
            AddSmsFileTarget(pState, f, TargetSection::Engine);
        }
    }

    RECT rcParent;
    GetWindowRect(hWndParent, &rcParent);
    int w = 1100, h = 720;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_APPWINDOW,
        SMS_STREAM_MUTATOR_WNDCLASS,
        L"SMS Stream Mutator & Presets Library",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h,
        hWndParent, NULL, GetModuleHandleW(NULL), pState
    );

    if (!hDlg) return;

    HICON hIconBig = (HICON)GetClassLongPtrW(hDlg, GCLP_HICON);
    HICON hIconSm = (HICON)GetClassLongPtrW(hDlg, GCLP_HICONSM);
    if (hIconBig) SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSm)  SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    g_hSmsStreamMutatorDlg = hDlg;

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

bool SmsStreamMutatorDlg_IsOpen()
{
    return (g_hSmsStreamMutatorDlg != NULL && IsWindow(g_hSmsStreamMutatorDlg));
}

void SmsStreamMutatorDlg_UpdateSelection(const std::vector<std::wstring>& targetSmsFilePaths)
{
    if (g_hSmsStreamMutatorDlg && IsWindow(g_hSmsStreamMutatorDlg))
    {
        SmsStreamMutatorState* pState = (SmsStreamMutatorState*)GetWindowLongPtrW(g_hSmsStreamMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            for (const auto& path : targetSmsFilePaths)
            {
                AddSmsFileTarget(pState, path);
            }
            InvalidateRect(g_hSmsStreamMutatorDlg, NULL, TRUE);
        }
    }
}

void SmsStreamMutatorDlg_SetTargets(const std::vector<std::wstring>& targetSmsFilePaths)
{
    if (g_hSmsStreamMutatorDlg && IsWindow(g_hSmsStreamMutatorDlg))
    {
        SmsStreamMutatorState* pState = (SmsStreamMutatorState*)GetWindowLongPtrW(g_hSmsStreamMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            pState->targetFiles.clear();
            pState->targetHits.clear();
            for (const auto& path : targetSmsFilePaths)
            {
                if (!path.empty())
                {
                    AddSmsFileTarget(pState, path, TargetSection::Engine);
                }
            }
            pState->scrollYTab1 = 0;
            InvalidateRect(g_hSmsStreamMutatorDlg, NULL, FALSE);
        }
    }
}

void SmsStreamMutatorDlg_SetCategorizedTargets(
    const std::vector<std::wstring>& engineSmsFiles,
    const std::vector<std::wstring>& wagonSmsFiles
)
{
    if (g_hSmsStreamMutatorDlg && IsWindow(g_hSmsStreamMutatorDlg))
    {
        SmsStreamMutatorState* pState = (SmsStreamMutatorState*)GetWindowLongPtrW(g_hSmsStreamMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            pState->targetFiles.clear();
            pState->targetHits.clear();
            for (const auto& path : engineSmsFiles)
            {
                if (!path.empty())
                {
                    AddSmsFileTarget(pState, path, TargetSection::Engine);
                }
            }
            for (const auto& path : wagonSmsFiles)
            {
                if (!path.empty())
                {
                    AddSmsFileTarget(pState, path, TargetSection::Wagon);
                }
            }
            pState->scrollYTab1 = 0;
            InvalidateRect(g_hSmsStreamMutatorDlg, NULL, FALSE);
        }
    }
}

void SmsStreamMutatorDlg_SetCategorizedTargetsWithUsage(
    const std::vector<SmsCategorizedTargetInput>& engineTargets,
    const std::vector<SmsCategorizedTargetInput>& wagonTargets
)
{
    if (g_hSmsStreamMutatorDlg && IsWindow(g_hSmsStreamMutatorDlg))
    {
        SmsStreamMutatorState* pState = (SmsStreamMutatorState*)GetWindowLongPtrW(g_hSmsStreamMutatorDlg, GWLP_USERDATA);
        if (pState)
        {
            pState->targetFiles.clear();
            pState->targetHits.clear();
            for (const auto& item : engineTargets)
            {
                if (!item.fullPath.empty())
                {
                    AddSmsFileTarget(pState, item.fullPath, TargetSection::Engine, &item.usage);
                }
            }
            for (const auto& item : wagonTargets)
            {
                if (!item.fullPath.empty())
                {
                    AddSmsFileTarget(pState, item.fullPath, TargetSection::Wagon, &item.usage);
                }
            }
            pState->scrollYTab1 = 0;
            InvalidateRect(g_hSmsStreamMutatorDlg, NULL, FALSE);
        }
    }
}
