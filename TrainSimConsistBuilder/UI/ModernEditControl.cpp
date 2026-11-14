#include "ModernEditControl.h"
#include "UITheme.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <string>
#include <gdiplus.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace {

static ULONG_PTR s_editGdiplusToken = 0;
static int s_editGdiplusRefCount = 0;

static void EnsureGdiPlus()
{
    if (s_editGdiplusRefCount == 0)
    {
        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&s_editGdiplusToken, &gsi, NULL);
    }
    s_editGdiplusRefCount++;
}

static void ReleaseGdiPlus()
{
    s_editGdiplusRefCount--;
    if (s_editGdiplusRefCount <= 0)
    {
        s_editGdiplusRefCount = 0;
        if (s_editGdiplusToken)
        {
            Gdiplus::GdiplusShutdown(s_editGdiplusToken);
            s_editGdiplusToken = 0;
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

// Focus and hover state trackers – exact 1:1 match with TrainSimConsistBuilder.cpp
static HWND s_hFocusedEdit = NULL;
static HWND s_hHoveredEdit = NULL;

struct ModernEditData {
    bool isNumericOnly = false;
    bool allowDecimals = true;
    bool isCentered = true;
    bool notifyParentOnEnter = false;
};

// Dynamically calculate vertical center offset based on active font metrics (exact Editor Workspace formula)
static void ApplyModernEditVerticalCenter(HWND hWnd, bool updateNow = true)
{
    if (!hWnd || !IsWindow(hWnd)) return;
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        RECT rcW;
        GetWindowRect(hWnd, &rcW);
        w = rcW.right - rcW.left;
        h = rcW.bottom - rcW.top;
    }
    if (w <= 0) w = 320;
    if (h <= 0) h = 32;

    HDC hdc = GetDC(hWnd);
    HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
    HGDIOBJ hOldF = SelectObject(hdc, hFont ? hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, hOldF);
    ReleaseDC(hWnd, hdc);

    // Center capital glyphs / numbers vertically within control height
    int topPad = (h > tm.tmHeight) ? ((h - tm.tmHeight + 1) / 2) : 0;
    RECT rcFmt = { 6, topPad, w - 6, h };
    SendMessageW(hWnd, updateNow ? EM_SETRECT : EM_SETRECTNP, 0, (LPARAM)&rcFmt);
}

// ModernEditSubclassProc: Exact clone of TrainSimConsistBuilder.cpp:2293
static LRESULT CALLBACK ModernEditSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    ModernEditData* pData = (ModernEditData*)dwRefData;

    switch (uMsg)
    {
    case WM_SETFOCUS:
    {
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (!isReadOnly)
        {
            s_hFocusedEdit = hWnd;
        }
        else
        {
            s_hFocusedEdit = NULL;
            HideCaret(hWnd);
        }
        if (pData && pData->isCentered)
        {
            ApplyModernEditVerticalCenter(hWnd, false);
        }
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_KILLFOCUS:
    {
        if (s_hFocusedEdit == hWnd) s_hFocusedEdit = NULL;
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_SETCURSOR:
    {
        if (!IsWindowEnabled(hWnd))
        {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (isReadOnly)
        {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
        if (!isReadOnly)
        {
            SetFocus(hWnd);
        }
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (isReadOnly)
        {
            HideCaret(hWnd);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_LBUTTONUP:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_SIZE:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (pData && pData->isCentered)
        {
            ApplyModernEditVerticalCenter(hWnd, true);
        }
        InvalidateRect(hWnd, NULL, TRUE);
        return lr;
    }

    case WM_SETFONT:
    {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (pData && pData->isCentered)
        {
            ApplyModernEditVerticalCenter(hWnd, true);
        }
        return lr;
    }

    case WM_MOUSEMOVE:
    {
        if (!IsWindowEnabled(hWnd))
        {
            if (s_hHoveredEdit == hWnd) s_hHoveredEdit = NULL;
            break;
        }
        if (s_hHoveredEdit != hWnd)
        {
            s_hHoveredEdit = hWnd;

            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);

            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (s_hHoveredEdit == hWnd)
        {
            s_hHoveredEdit = NULL;
            InvalidateRect(GetParent(hWnd), NULL, FALSE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_CHAR:
    {
        wchar_t ch = (wchar_t)wParam;
        if (ch == VK_RETURN)
        {
            if (pData && pData->notifyParentOnEnter)
            {
                HWND hParent = GetParent(hWnd);
                if (hParent) SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)hWnd);
            }
            return 0;
        }
        if (ch == VK_ESCAPE)
        {
            if (pData && pData->notifyParentOnEnter)
            {
                HWND hParent = GetParent(hWnd);
                if (hParent) SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)hWnd);
            }
            return 0;
        }

        if (pData && pData->isNumericOnly)
        {
            if (ch < 32)
            {
                // allow control character
            }
            else if (ch >= L'0' && ch <= L'9')
            {
                // allow digit
            }
            else if (ch == L'.' && pData->allowDecimals)
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
                        return 0; // block extra decimal points
                    }
                }
            }
            else
            {
                return 0; // block letters/symbols
            }
        }

        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_PASTE:
    {
        if (pData && pData->isNumericOnly)
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
                        bool hasDot = false;
                        for (size_t i = 0; pClip[i] != L'\0'; ++i)
                        {
                            if (pClip[i] >= L'0' && pClip[i] <= L'9') filtered += pClip[i];
                            else if (pClip[i] == L'.' && pData->allowDecimals && !hasDot) { filtered += L'.'; hasDot = true; }
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
        }
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }

    case WM_NCPAINT:
        return 0;

    case WM_ERASEBKGND:
        return 1;

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

                bool isEnabled = IsWindowEnabled(hWnd) != FALSE;
                bool focused = isEnabled && (s_hFocusedEdit == hWnd);
                bool hovered = isEnabled && (s_hHoveredEdit == hWnd);

                // 1. Fill parent workspace background to anti-alias corners
                COLORREF parentBg = UITheme::DarkBackground;
                HWND hParent = GetParent(hWnd);
                if (hParent)
                {
                    wchar_t szParentClass[64] = { 0 };
                    GetClassNameW(hParent, szParentClass, 64);
                    if (wcscmp(szParentClass, L"TSCBClassicConsistView") == 0)
                    {
                        parentBg = RGB(22, 24, 30);
                    }
                }
                HBRUSH hbrParent = CreateSolidBrush(parentBg);
                FillRect(hMemDC, &rc, hbrParent);
                DeleteObject(hbrParent);

                // 2. Select edit box background color based on active state (Win11 Explorer spec)
                COLORREF clrBg;
                COLORREF clrBorder;
                if (!isEnabled)
                {
                    clrBg = RGB(26, 26, 26);
                    clrBorder = RGB(40, 40, 40);
                }
                else
                {
                    clrBg = focused ? RGB(30, 30, 30) : (hovered ? RGB(45, 45, 45) : RGB(38, 38, 38));
                    clrBorder = focused ? RGB(70, 70, 70) : (hovered ? RGB(65, 65, 65) : RGB(50, 50, 50));
                }

                EnsureGdiPlus();
                Gdiplus::Graphics g(hMemDC);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

                Gdiplus::RectF rcBox(0.5f, 0.5f, (Gdiplus::REAL)width - 1.0f, (Gdiplus::REAL)height - 1.0f);
                Gdiplus::GraphicsPath boxPath;
                AddRoundedRectPath(boxPath, rcBox, 4.0f);

                Gdiplus::SolidBrush brBg(Gdiplus::Color(255, GetRValue(clrBg), GetGValue(clrBg), GetBValue(clrBg)));
                g.FillPath(&brBg, &boxPath);

                // 3. Render edit control text via WM_PRINTCLIENT (clipped cleanly inside rounded boundaries)
                RECT rcClip = { rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 2 };
                HRGN hRgnClip = CreateRectRgnIndirect(&rcClip);
                SelectClipRgn(hMemDC, hRgnClip);
                DefSubclassProc(hWnd, WM_PRINTCLIENT, (WPARAM)hMemDC, PRF_CLIENT);
                SelectClipRgn(hMemDC, NULL);
                DeleteObject(hRgnClip);

                // 4. Draw anti-aliased border around edit box
                Gdiplus::Pen penBorder(Gdiplus::Color(255, GetRValue(clrBorder), GetGValue(clrBorder), GetBValue(clrBorder)), 1.0f);
                g.DrawPath(&penBorder, &boxPath);

                // 5. Draw focused accent light blue bottom line (Windows 11 Fluent edit highlight with curved corners)
                bool isReadOnly = (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_READONLY) != 0;
                if (focused && !isReadOnly && isEnabled)
                {
                    g.SetClip(&boxPath);
                    Gdiplus::SolidBrush accentBrush(Gdiplus::Color(255, 96, 205, 255));
                    g.FillRectangle(&accentBrush, 0.0f, (Gdiplus::REAL)height - 3.0f, (Gdiplus::REAL)width, 3.0f);
                    g.ResetClip();

                    // Re-stroke border so accent line integrates seamlessly with 1px border
                    g.DrawPath(&penBorder, &boxPath);
                }

                // 6. Blit entire frame to screen in one single operation
                BitBlt(hdc, 0, 0, width, height, hMemDC, 0, 0, SRCCOPY);

                SelectObject(hMemDC, hOldBmp);
                DeleteObject(hMemBmp);
                DeleteDC(hMemDC);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
    {
        if (s_hFocusedEdit == hWnd) s_hFocusedEdit = NULL;
        if (s_hHoveredEdit == hWnd) s_hHoveredEdit = NULL;
        RemovePropW(hWnd, L"TSCB_ModernEditData");
        RemoveWindowSubclass(hWnd, ModernEditSubclassProc, uIdSubclass);
        delete pData;
        break;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace

void ModernEdit_Attach(HWND hEdit, bool isNumericOnly, bool isCentered, bool allowDecimals, bool notifyParentOnEnter)
{
    if (!hEdit || !IsWindow(hEdit)) return;

    // Clear 3D/legacy sunken border styles so Windows non-client area doesn't render white frame notches
    LONG_PTR exStyle = GetWindowLongPtrW(hEdit, GWL_EXSTYLE);
    if (exStyle & WS_EX_CLIENTEDGE)
    {
        SetWindowLongPtrW(hEdit, GWL_EXSTYLE, exStyle & ~WS_EX_CLIENTEDGE);
    }
    LONG_PTR style = GetWindowLongPtrW(hEdit, GWL_STYLE);
    if (style & WS_BORDER)
    {
        SetWindowLongPtrW(hEdit, GWL_STYLE, style & ~WS_BORDER);
    }
    SetWindowPos(hEdit, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    ModernEditData* data = new ModernEditData();
    data->isNumericOnly = isNumericOnly;
    data->isCentered = isCentered;
    data->allowDecimals = allowDecimals;
    data->notifyParentOnEnter = notifyParentOnEnter;

    SetPropW(hEdit, L"TSCB_ModernEditData", (HANDLE)data);
    SetWindowSubclass(hEdit, ModernEditSubclassProc, (UINT_PTR)hEdit, (DWORD_PTR)data);

    if (isCentered)
    {
        ApplyModernEditVerticalCenter(hEdit, true);
    }
}

LRESULT ModernEdit_OnCtlColor(HDC hdc, HWND hEdit)
{
    bool isEnabled = IsWindowEnabled(hEdit) != FALSE;
    bool focused = isEnabled && (s_hFocusedEdit == hEdit);
    bool hovered = isEnabled && (s_hHoveredEdit == hEdit);

    COLORREF bg;
    COLORREF fg;
    if (!isEnabled)
    {
        bg = RGB(26, 26, 26);
        fg = RGB(100, 100, 110);
    }
    else
    {
        bg = focused ? RGB(30, 30, 30) : (hovered ? RGB(45, 45, 45) : RGB(38, 38, 38));
        fg = RGB(240, 240, 240);
    }

    SetTextColor(hdc, fg);
    SetBkColor(hdc, bg);

    static HBRUSH s_hbrDisabled = NULL;
    static HBRUSH s_hbrFocused = NULL;
    static HBRUSH s_hbrHovered = NULL;
    static HBRUSH s_hbrNormal = NULL;

    if (!s_hbrDisabled) s_hbrDisabled = CreateSolidBrush(RGB(26, 26, 26));
    if (!s_hbrFocused)  s_hbrFocused  = CreateSolidBrush(RGB(30, 30, 30));
    if (!s_hbrHovered)  s_hbrHovered  = CreateSolidBrush(RGB(45, 45, 45));
    if (!s_hbrNormal)   s_hbrNormal   = CreateSolidBrush(RGB(38, 38, 38));

    if (!isEnabled) return (LRESULT)s_hbrDisabled;
    return (LRESULT)(focused ? s_hbrFocused : (hovered ? s_hbrHovered : s_hbrNormal));
}

// ---------------------------------------------------------------------------
// Modern Fluent Button Subclass Implementation
// ---------------------------------------------------------------------------
struct ModernBtnData {
    bool isPrimary = false;
    bool isHovered = false;
    bool isPressed = false;
    HFONT hFont = NULL;
    std::wstring text;
};

static LRESULT CALLBACK ModernButtonSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    ModernBtnData* pData = (ModernBtnData*)dwRefData;

    switch (uMsg)
    {
    case WM_MOUSEMOVE:
    {
        if (pData && !pData->isHovered)
        {
            pData->isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE:
    {
        if (pData && pData->isHovered)
        {
            pData->isHovered = false;
            pData->isPressed = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        SetFocus(hWnd);
        SetCapture(hWnd);
        if (pData)
        {
            pData->isPressed = true;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
            bool wasPressed = pData ? pData->isPressed : false;
            if (pData)
            {
                pData->isPressed = false;
                InvalidateRect(hWnd, NULL, FALSE);
            }

            if (wasPressed)
            {
                RECT rc;
                GetClientRect(hWnd, &rc);
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (PtInRect(&rc, pt))
                {
                    HWND hParent = GetParent(hWnd);
                    if (hParent)
                    {
                        UINT ctrlId = (UINT)GetWindowLongPtrW(hWnd, GWLP_ID);
                        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(ctrlId, BN_CLICKED), (LPARAM)hWnd);
                    }
                }
            }
        }
        return 0;
    }
    case BM_SETSTATE:
        return 0;
    case WM_KEYDOWN:
    {
        if (wParam == VK_SPACE || wParam == VK_RETURN)
        {
            if (pData) { pData->isPressed = true; InvalidateRect(hWnd, NULL, FALSE); }
            return 0;
        }
        break;
    }
    case WM_KEYUP:
    {
        if (wParam == VK_SPACE || wParam == VK_RETURN)
        {
            if (pData) { pData->isPressed = false; InvalidateRect(hWnd, NULL, FALSE); }
            HWND hParent = GetParent(hWnd);
            if (hParent)
            {
                UINT ctrlId = (UINT)GetWindowLongPtrW(hWnd, GWLP_ID);
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(ctrlId, BN_CLICKED), (LPARAM)hWnd);
            }
            return 0;
        }
        break;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_UPDATEUISTATE:
    case WM_CHANGEUISTATE:
    {
        return 0;
    }
    case WM_SETTEXT:
    {
        if (pData && lParam)
        {
            pData->text = (const wchar_t*)lParam;
        }
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }
    case WM_SETFONT:
    {
        if (pData) pData->hFont = (HFONT)wParam;
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        InvalidateRect(hWnd, NULL, FALSE);
        return lr;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc && pData)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int w = rc.right;
            int h = rc.bottom;

            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

            // Fill parent workspace background to anti-alias corners
            HBRUSH hbrParent = CreateSolidBrush(UITheme::DarkBackground);
            FillRect(hMemDC, &rc, hbrParent);
            DeleteObject(hbrParent);

            bool isFocused = (GetFocus() == hWnd);
            COLORREF btnBg, btnBorder, btnText;
            if (pData->isPrimary)
            {
                btnBg = pData->isPressed ? RGB(0, 100, 185) : (pData->isHovered ? RGB(20, 140, 235) : RGB(0, 120, 215));
                btnBorder = isFocused ? RGB(140, 205, 255) : btnBg;
                btnText = RGB(255, 255, 255);
            }
            else
            {
                btnBg = pData->isPressed ? RGB(36, 36, 36) : (pData->isHovered ? RGB(52, 52, 52) : RGB(40, 40, 40));
                btnBorder = isFocused ? RGB(100, 150, 210) : (pData->isHovered ? RGB(80, 80, 80) : RGB(60, 60, 60));
                btnText = RGB(245, 245, 245);
            }

            HBRUSH hbrBtn = CreateSolidBrush(btnBg);
            HPEN hPenBtn = CreatePen(PS_SOLID, 1, btnBorder);
            HGDIOBJ hOldBr = SelectObject(hMemDC, hbrBtn);
            HGDIOBJ hOldP = SelectObject(hMemDC, hPenBtn);

            RoundRect(hMemDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);

            SelectObject(hMemDC, hOldP);
            SelectObject(hMemDC, hOldBr);
            DeleteObject(hPenBtn);
            DeleteObject(hbrBtn);

            // Draw button text
            HFONT hFontToUse = pData->hFont ? pData->hFont : (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
            HGDIOBJ hOldFont = SelectObject(hMemDC, hFontToUse ? hFontToUse : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(hMemDC, TRANSPARENT);
            SetTextColor(hMemDC, btnText);
            RECT rcText = rc;
            if (pData->isPressed) OffsetRect(&rcText, 0, 1);
            DrawTextW(hMemDC, pData->text.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hMemDC, hOldFont);

            BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
            SelectObject(hMemDC, hOldBmp);
            DeleteObject(hMemBmp);
            DeleteDC(hMemDC);
            EndPaint(hWnd, &ps);
        }
        return 0;
    }
    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hWnd, ModernButtonSubclassProc, uIdSubclass);
        delete pData;
        break;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ModernButton_Attach(HWND hBtn, bool isPrimary, HFONT hFont)
{
    if (!hBtn || !IsWindow(hBtn)) return;
    ModernBtnData* pData = new ModernBtnData();
    pData->isPrimary = isPrimary;
    pData->hFont = hFont;
    wchar_t szTxt[256] = { 0 };
    GetWindowTextW(hBtn, szTxt, 256);
    pData->text = szTxt;

    SetWindowSubclass(hBtn, ModernButtonSubclassProc, (UINT_PTR)hBtn, (DWORD_PTR)pData);
}

// ShowModernInputPrompt: Universal single/double text input prompt
bool ShowModernInputPrompt(
    HWND hParent,
    const wchar_t* title,
    const wchar_t* promptLabel,
    std::wstring& ioValue,
    bool isNumericOnly,
    bool allowDecimals,
    const wchar_t* placeholder,
    const wchar_t* secondLabel,
    std::wstring* pSecondValue,
    bool secondNumericOnly,
    const wchar_t* secondPlaceholder)
{
    struct PromptState {
        std::wstring title;
        std::wstring label1;
        std::wstring val1;
        std::wstring ph1;
        bool num1 = false;
        bool dec1 = false;

        bool hasSecond = false;
        std::wstring label2;
        std::wstring val2;
        std::wstring ph2;
        bool num2 = false;

        bool ok = false;
        HWND hEdit1 = NULL;
        HWND hEdit2 = NULL;
        HWND hBtnOk = NULL;
        HWND hBtnCancel = NULL;

        HFONT hFontTitle = NULL;
        HFONT hFontNormal = NULL;
        HFONT hFontBold = NULL;
        HBRUSH hbrBg = NULL;
    };

    PromptState st;
    st.title = title ? title : L"Input";
    st.label1 = promptLabel ? promptLabel : L"Enter value:";
    st.val1 = ioValue;
    st.ph1 = placeholder ? placeholder : L"";
    st.num1 = isNumericOnly;
    st.dec1 = allowDecimals;

    if (secondLabel && pSecondValue)
    {
        st.hasSecond = true;
        st.label2 = secondLabel;
        st.val2 = *pSecondValue;
        st.ph2 = secondPlaceholder ? secondPlaceholder : L"";
        st.num2 = secondNumericOnly;
    }

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        PromptState* pSt = (PromptState*)GetPropW(hWnd, L"TSCB_PromptState");
        switch (msg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            SetPropW(hWnd, L"TSCB_PromptState", (HANDLE)cs->lpCreateParams);
            return TRUE;
        }
        case WM_CREATE:
        {
            pSt = (PromptState*)GetPropW(hWnd, L"TSCB_PromptState");
            if (!pSt) return -1;

            BOOL bDark = TRUE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            COLORREF capColor = RGB(28, 30, 38);
            DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &capColor, sizeof(capColor));

            HINSTANCE hInstIcon = ((LPCREATESTRUCT)lParam)->hInstance;
            BYTE andMask = 0xFF;
            BYTE xorMask = 0x00;
            HICON hEmptyIcon = CreateIcon(hInstIcon, 1, 1, 1, 1, &andMask, &xorMask);
            if (hEmptyIcon)
            {
                SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hEmptyIcon);
                SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hEmptyIcon);
            }

            pSt->hbrBg = CreateSolidBrush(RGB(28, 30, 38));
            pSt->hFontTitle = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            pSt->hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            pSt->hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            int y = 58;

            const DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_MULTILINE | ES_AUTOHSCROLL;
            pSt->hEdit1 = CreateWindowExW(0, L"EDIT", pSt->val1.c_str(), dwStyle, 24, y, 328, 32, hWnd, (HMENU)201, hInst, NULL);
            if (pSt->hEdit1)
            {
                SendMessageW(pSt->hEdit1, WM_SETFONT, (WPARAM)pSt->hFontNormal, MAKELPARAM(TRUE, 0));
                if (!pSt->ph1.empty()) SendMessageW(pSt->hEdit1, EM_SETCUEBANNER, TRUE, (LPARAM)pSt->ph1.c_str());
                ModernEdit_Attach(pSt->hEdit1, pSt->num1, true, pSt->dec1, true);
            }
            y += 44;

            if (pSt->hasSecond)
            {
                y += 24;
                pSt->hEdit2 = CreateWindowExW(0, L"EDIT", pSt->val2.c_str(), dwStyle, 24, y, 328, 32, hWnd, (HMENU)202, hInst, NULL);
                if (pSt->hEdit2)
                {
                    SendMessageW(pSt->hEdit2, WM_SETFONT, (WPARAM)pSt->hFontNormal, MAKELPARAM(TRUE, 0));
                    if (!pSt->ph2.empty()) SendMessageW(pSt->hEdit2, EM_SETCUEBANNER, TRUE, (LPARAM)pSt->ph2.c_str());
                    ModernEdit_Attach(pSt->hEdit2, pSt->num2, true, true, true);
                }
                y += 44;
            }

            int btnY = y + 12;
            pSt->hBtnOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                172, btnY, 90, 32, hWnd, (HMENU)IDOK, hInst, NULL);
            pSt->hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                272, btnY, 80, 32, hWnd, (HMENU)IDCANCEL, hInst, NULL);

            if (pSt->hBtnOk)
            {
                SendMessageW(pSt->hBtnOk, WM_SETFONT, (WPARAM)pSt->hFontBold, MAKELPARAM(TRUE, 0));
                ModernButton_Attach(pSt->hBtnOk, true, pSt->hFontBold);
            }
            if (pSt->hBtnCancel)
            {
                SendMessageW(pSt->hBtnCancel, WM_SETFONT, (WPARAM)pSt->hFontNormal, MAKELPARAM(TRUE, 0));
                ModernButton_Attach(pSt->hBtnCancel, false, pSt->hFontNormal);
            }

            if (pSt->hEdit1)
            {
                SetFocus(pSt->hEdit1);
                SendMessageW(pSt->hEdit1, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            if (pSt && pSt->hbrBg) return (LRESULT)pSt->hbrBg;
            break;
        }
        case WM_CTLCOLOREDIT:
        {
            return ModernEdit_OnCtlColor((HDC)wParam, (HWND)lParam);
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            if (pSt && hdc)
            {
                RECT rc;
                GetClientRect(hWnd, &rc);
                FillRect(hdc, &rc, pSt->hbrBg);
                SetBkMode(hdc, TRANSPARENT);

                SelectObject(hdc, pSt->hFontTitle);
                SetTextColor(hdc, RGB(255, 255, 255));
                TextOutW(hdc, 24, 14, pSt->title.c_str(), (int)pSt->title.length());

                SelectObject(hdc, pSt->hFontNormal);
                SetTextColor(hdc, RGB(200, 205, 220));
                TextOutW(hdc, 24, 40, pSt->label1.c_str(), (int)pSt->label1.length());

                if (pSt->hasSecond)
                {
                    TextOutW(hdc, 24, 108, pSt->label2.c_str(), (int)pSt->label2.length());
                }
            }
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_COMMAND:
        {
            WORD cmd = LOWORD(wParam);
            if (cmd == IDOK)
            {
                if (pSt)
                {
                    wchar_t b1[256] = { 0 };
                    if (pSt->hEdit1) GetWindowTextW(pSt->hEdit1, b1, 256);
                    pSt->val1 = b1;
                    if (pSt->hasSecond && pSt->hEdit2)
                    {
                        wchar_t b2[256] = { 0 };
                        GetWindowTextW(pSt->hEdit2, b2, 256);
                        pSt->val2 = b2;
                    }
                    pSt->ok = true;
                }
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (cmd == IDCANCEL)
            {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_NCDESTROY:
            RemovePropW(hWnd, L"TSCB_PromptState");
            if (pSt)
            {
                if (pSt->hbrBg) DeleteObject(pSt->hbrBg);
                if (pSt->hFontTitle) DeleteObject(pSt->hFontTitle);
                if (pSt->hFontNormal) DeleteObject(pSt->hFontNormal);
                if (pSt->hFontBold) DeleteObject(pSt->hFontBold);
            }
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    };
    wc.lpszClassName = L"TSCB_ModernPromptDialog";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    DWORD dwDlgStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD dwDlgExStyle = WS_EX_TOPMOST;
    int clientW = 376;
    int clientH = st.hasSecond ? 260 : 190;

    RECT rcClient = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rcClient, dwDlgStyle, FALSE, dwDlgExStyle);
    int dlgW = rcClient.right - rcClient.left;
    int dlgH = rcClient.bottom - rcClient.top;

    HWND hDlg = CreateWindowExW(
        dwDlgExStyle,
        wc.lpszClassName, st.title.c_str(),
        dwDlgStyle,
        0, 0, dlgW, dlgH,
        hParent, NULL, GetModuleHandle(NULL), &st
    );

    if (!hDlg) return false;

    // Center over parent
    if (hParent && IsWindow(hParent))
    {
        RECT rcP, rcD;
        GetWindowRect(hParent, &rcP);
        GetWindowRect(hDlg, &rcD);
        int w = rcD.right - rcD.left;
        int h = rcD.bottom - rcD.top;
        int x = rcP.left + ((rcP.right - rcP.left) - w) / 2;
        int y = rcP.top + ((rcP.bottom - rcP.top) - h) / 2;
        SetWindowPos(hDlg, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    }
    else
    {
        ShowWindow(hDlg, SW_SHOW);
    }

    EnableWindow(hParent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (!IsDialogMessageW(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hParent, TRUE);
    if (hParent && IsWindow(hParent)) SetActiveWindow(hParent);

    if (st.ok)
    {
        ioValue = st.val1;
        if (pSecondValue) *pSecondValue = st.val2;
        return true;
    }
    return false;
}

bool ShowModernInputPrompt(
    HWND hWndParent,
    const std::wstring& title,
    const std::wstring& prompt,
    const std::wstring& defaultText,
    std::wstring& outText,
    bool isNumericOnly,
    bool allowDecimals)
{
    outText = defaultText;
    return ShowModernInputPrompt(hWndParent, title.c_str(), prompt.c_str(), outText, isNumericOnly, allowDecimals, L"");
}
