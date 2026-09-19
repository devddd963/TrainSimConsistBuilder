#include "FilterPopup.h"
#include <windowsx.h>
#include <algorithm>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

extern HFONT GetAdaptiveSystemFont();

FilterPopup::FilterPopup()
    : m_hWnd(NULL), m_hParent(NULL), m_colIndex(-1), m_callback(NULL), m_callbackParam(NULL),
      m_hoverIndex(-1), m_bTrackingMouse(false), m_hFont(NULL), m_itemHeight(32)
{
}

FilterPopup::~FilterPopup()
{
}

bool FilterPopup::Register(HINSTANCE hInstance)
{
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wcx.lpfnWndProc   = FilterPopup::WndProc;
    wcx.cbWndExtra    = sizeof(FilterPopup*);
    wcx.hInstance     = hInstance;
    wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = NULL; // Double buffered
    wcx.lpszClassName = L"CustomFilterMenu";

    return (RegisterClassExW(&wcx) != 0);
}

void FilterPopup::Show(HWND hParent, int colIndex, int x, int y, const std::vector<std::wstring>& allOptions, const std::vector<std::wstring>& checkedOptions, FilterPopupCallback callback, void* pParam)
{
    if (m_hWnd && IsWindow(m_hWnd))
    {
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }

    m_hParent = hParent;
    m_colIndex = colIndex;
    m_callback = callback;
    m_callbackParam = pParam;
    m_hoverIndex = -1;
    m_bTrackingMouse = false;
    m_hFont = GetAdaptiveSystemFont();
    m_itemHeight = 32;

    m_items.clear();
    for (const auto& opt : allOptions)
    {
        bool checked = (std::find(checkedOptions.begin(), checkedOptions.end(), opt) != checkedOptions.end());
        m_items.push_back({ opt, checked });
    }

    if (m_items.empty()) return;

    // Measure longest label to fit comfortably
    HDC hdcScreen = GetDC(NULL);
    HFONT hOldF = (HFONT)SelectObject(hdcScreen, m_hFont ? m_hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    int maxTextW = 100;
    for (const auto& it : m_items)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdcScreen, it.label.c_str(), (int)it.label.length(), &sz);
        if (sz.cx > maxTextW) maxTextW = sz.cx;
    }
    SelectObject(hdcScreen, hOldF);
    ReleaseDC(NULL, hdcScreen);

    int clientW = maxTextW + 54; // 36px checkbox space + 18px padding
    if (clientW < 180) clientW = 180;
    if (clientW > 400) clientW = 400;

    int clientH = (int)m_items.size() * m_itemHeight;
    int width = clientW;
    int height = clientH;

    // Keep within work area
    HMONITOR hMon = MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    if (x + width > mi.rcWork.right) x = mi.rcWork.right - width - 4;
    if (x < mi.rcWork.left) x = mi.rcWork.left + 4;
    if (y + height > mi.rcWork.bottom) y = y - height - 30;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);
    Register(hInst);

    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CustomFilterMenu", L"",
        WS_POPUP,
        x, y, width, height,
        hParent, NULL, hInst, this
    );

    if (m_hWnd)
    {
        // Windows 11 DWM Rounded Corners & Dark Mode
        DWORD corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(m_hWnd, (DWMWINDOWATTRIBUTE)33, &corner, sizeof(corner));
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(m_hWnd, (DWMWINDOWATTRIBUTE)20, &useDark, sizeof(useDark));

        // 8px rounded window region
        HRGN hRgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, 16, 16);
        SetWindowRgn(m_hWnd, hRgn, TRUE);

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        SetCapture(m_hWnd);
    }
}

LRESULT CALLBACK FilterPopup::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    FilterPopup* pThis = NULL;
    if (uMsg == WM_NCCREATE)
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pThis = (FilterPopup*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
    }
    else
    {
        pThis = (FilterPopup*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    }

    if (pThis)
    {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT FilterPopup::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        // Double buffering
        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // 1. Fill Deep Dark Explorer Surface background (RGB 28, 28, 28)
        COLORREF bgCol = RGB(28, 28, 28);
        COLORREF borderCol = RGB(55, 55, 55);

        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hMemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        // 2. Draw items & Full-Width Hover Highlight (no gaps)
        HFONT hOldFont = NULL;
        if (m_hFont) hOldFont = (HFONT)SelectObject(hMemDC, m_hFont);

        SetBkMode(hMemDC, TRANSPARENT);

        for (size_t i = 0; i < m_items.size(); ++i)
        {
            int y = (int)i * m_itemHeight;

            // Full-width hover highlight from edge to edge
            if ((int)i == m_hoverIndex)
            {
                RECT rcHighlight = { 0, y, w, y + m_itemHeight };
                COLORREF hoverCol = RGB(48, 48, 48);
                HBRUSH hbrHover = CreateSolidBrush(hoverCol);
                FillRect(hMemDC, &rcHighlight, hbrHover);
                DeleteObject(hbrHover);
            }

            // Draw Checkbox Box (16x16) - rounded Fluent checkbox
            int boxSize = 16;
            int boxTop = y + (m_itemHeight - boxSize) / 2;
            RECT rcBox = { 14, boxTop, 14 + boxSize, boxTop + boxSize };

            if (m_items[i].checked)
            {
                // Fill Box with Accent Blue (radius 3px)
                COLORREF blueCol = RGB(0, 120, 215);
                HBRUSH hbrBox = CreateSolidBrush(blueCol);
                HPEN hNullP = CreatePen(PS_NULL, 0, 0);
                HBRUSH hOldBBox = (HBRUSH)SelectObject(hMemDC, hbrBox);
                HPEN hOldP = (HPEN)SelectObject(hMemDC, hNullP);

                RoundRect(hMemDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 6, 6);

                SelectObject(hMemDC, hOldBBox);
                SelectObject(hMemDC, hOldP);
                DeleteObject(hbrBox);
                DeleteObject(hNullP);

                // Draw crisp white Checkmark
                HPEN hCheckPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HPEN hOldCheckPen = (HPEN)SelectObject(hMemDC, hCheckPen);
                POINT pts[3];
                pts[0] = { rcBox.left + 4, rcBox.top + 8 };
                pts[1] = { rcBox.left + 7, rcBox.top + 11 };
                pts[2] = { rcBox.left + 12, rcBox.top + 5 };
                Polyline(hMemDC, pts, 3);
                SelectObject(hMemDC, hOldCheckPen);
                DeleteObject(hCheckPen);
            }
            else
            {
                // Draw empty border box with theme-correct bg (radius 3px)
                COLORREF boxBorder = RGB(110, 110, 110);
                HPEN hBoxBorderPen = CreatePen(PS_SOLID, 1, boxBorder);
                HBRUSH hbrEmpty = CreateSolidBrush(bgCol);

                HPEN hOldP2 = (HPEN)SelectObject(hMemDC, hBoxBorderPen);
                HBRUSH hOldB2 = (HBRUSH)SelectObject(hMemDC, hbrEmpty);

                RoundRect(hMemDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 6, 6);

                SelectObject(hMemDC, hOldB2);
                SelectObject(hMemDC, hOldP2);
                DeleteObject(hbrEmpty);
                DeleteObject(hBoxBorderPen);
            }

            // Draw label - perfectly vertically centered
            RECT rcText = { rcBox.right + 12, y, w - 8, y + m_itemHeight };
            SetTextColor(hMemDC, RGB(255, 255, 255));
            DrawTextW(hMemDC, m_items[i].label.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (hOldFont) SelectObject(hMemDC, hOldFont);

        // 3. Draw 1px subtle rounded border around popup
        HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenBorder);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hNullBrush);

        RoundRect(hMemDC, 0, 0, w, h, 16, 16);

        SelectObject(hMemDC, hOldBr);
        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPenBorder);

        BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(m_hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!m_bTrackingMouse)
        {
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hWnd;
            TrackMouseEvent(&tme);
            m_bTrackingMouse = true;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);

        int newHover = -1;
        if (x >= 0 && x < rcClient.right && y >= 0 && y < rcClient.bottom)
        {
            newHover = y / m_itemHeight;
            if (newHover < 0 || newHover >= (int)m_items.size())
                newHover = -1;
        }

        if (newHover != m_hoverIndex)
        {
            m_hoverIndex = newHover;
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_bTrackingMouse = false;
        if (m_hoverIndex != -1)
        {
            m_hoverIndex = -1;
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rcClient;
        GetClientRect(m_hWnd, &rcClient);

        if (!PtInRect(&rcClient, pt))
        {
            ReleaseCapture();
            DestroyWindow(m_hWnd);
            return 0;
        }

        int clickIdx = pt.y / m_itemHeight;
        if (clickIdx >= 0 && clickIdx < (int)m_items.size())
        {
            bool wasChecked = m_items[clickIdx].checked;
            for (auto& item : m_items)
            {
                item.checked = false;
            }
            m_items[clickIdx].checked = !wasChecked;
            InvalidateRect(m_hWnd, NULL, FALSE);

            // Execute callback with current checked states
            if (m_callback)
            {
                std::vector<std::wstring> checkedOptions;
                for (const auto& item : m_items)
                {
                    if (item.checked) checkedOptions.push_back(item.label);
                }
                m_callback(m_colIndex, checkedOptions, m_callbackParam);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            ReleaseCapture();
            DestroyWindow(m_hWnd);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        ReleaseCapture();
        DestroyWindow(m_hWnd);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            ReleaseCapture();
            DestroyWindow(m_hWnd);
        }
        return 0;

    case WM_DESTROY:
        m_hWnd = NULL;
        return 0;
    }
    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}
