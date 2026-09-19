#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ModernContextMenu.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

struct ModernContextMenuState {
    HWND hWnd;
    HWND hParent;
    std::vector<ContextMenuItem> items;
    BOOL bDarkMode;
    int hoveredIndex;
    int selectedId;
    bool isDone;
    int maxShortcutW;
    HFONT hFontText;
    HFONT hFontIcons;
    HFONT hFontShortcuts;
};

static const int ITEM_HEIGHT = 32;
static const int SEPARATOR_HEIGHT = 9;
static const int PADDING_V = 6;
static const int MIN_MENU_WIDTH = 260;

static LRESULT CALLBACK ModernContextMenuWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ModernContextMenuState* pState = (ModernContextMenuState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (ModernContextMenuState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
    {
        if (!pState) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        COLORREF bgMenu   = RGB(38, 38, 42);
        COLORREF borderCol= RGB(65, 68, 76);
        COLORREF hoverBg  = RGB(56, 60, 70);
        COLORREF sepColor = RGB(52, 55, 62);
        COLORREF textNorm = RGB(240, 242, 248);
        COLORREF textDis  = RGB(140, 145, 155);
        COLORREF textShort= RGB(160, 168, 180);

        HBRUSH hbrBg = CreateSolidBrush(bgMenu);
        FillRect(hMemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        int curY = PADDING_V;
        for (size_t i = 0; i < pState->items.size(); ++i)
        {
            const auto& item = pState->items[i];
            if (item.isSeparator)
            {
                HPEN hPenSep = CreatePen(PS_SOLID, 1, sepColor);
                HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenSep);
                int sepY = curY + SEPARATOR_HEIGHT / 2;
                MoveToEx(hMemDC, 12, sepY, NULL);
                LineTo(hMemDC, w - 12, sepY);
                SelectObject(hMemDC, hOldP);
                DeleteObject(hPenSep);
                curY += SEPARATOR_HEIGHT;
            }
            else
            {
                RECT rcItem = { 6, curY, w - 6, curY + ITEM_HEIGHT };
                bool isHovered = ((int)i == pState->hoveredIndex) && item.isEnabled;

                if (isHovered)
                {
                    HBRUSH hbrHover = CreateSolidBrush(hoverBg);
                    HPEN hPenHover = CreatePen(PS_SOLID, 1, hoverBg);
                    HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrHover);
                    HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenHover);
                    RoundRect(hMemDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 6, 6);
                    SelectObject(hMemDC, hOldB);
                    SelectObject(hMemDC, hOldP);
                    DeleteObject(hbrHover);
                    DeleteObject(hPenHover);
                }

                SetBkMode(hMemDC, TRANSPARENT);

                // 1. Icon Glyph
                if (pState->hFontIcons && !item.icon.empty())
                {
                    HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcons);
                    SetTextColor(hMemDC, item.isEnabled ? RGB(96, 205, 255) : textDis);
                    RECT rcIcon = { rcItem.left + 6, rcItem.top, rcItem.left + 28, rcItem.bottom };
                    DrawTextW(hMemDC, item.icon.c_str(), -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(hMemDC, hOldF);
                }

                int shortcutW = pState->maxShortcutW;
                int textRight = (shortcutW > 0) ? (rcItem.right - shortcutW - 16) : (rcItem.right - 10);

                // 2. Item Text
                if (pState->hFontText && !item.text.empty())
                {
                    HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
                    SetTextColor(hMemDC, item.isEnabled ? textNorm : textDis);
                    RECT rcText = { rcItem.left + 32, rcItem.top, textRight, rcItem.bottom };
                    DrawTextW(hMemDC, item.text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(hMemDC, hOldF);
                }

                // 3. Shortcut / Tag Text
                if (pState->hFontShortcuts && !item.shortcut.empty())
                {
                    HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontShortcuts);
                    SetTextColor(hMemDC, item.isEnabled ? textShort : textDis);
                    RECT rcShort = { textRight + 6, rcItem.top, rcItem.right - 8, rcItem.bottom };
                    DrawTextW(hMemDC, item.shortcut.c_str(), -1, &rcShort, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(hMemDC, hOldF);
                }

                curY += ITEM_HEIGHT;
            }
        }

        // Draw 1px Outer Frame Border
        HPEN hPenFrame = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldOP = (HPEN)SelectObject(hMemDC, hPenFrame);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hOldOB = (HBRUSH)SelectObject(hMemDC, hNullB);
        RoundRect(hMemDC, 0, 0, w, h, 8, 8);
        SelectObject(hMemDC, hOldOB);
        SelectObject(hMemDC, hOldOP);
        DeleteObject(hPenFrame);

        BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hMemBmp);
        DeleteDC(hMemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        int y = GET_Y_LPARAM(lParam);
        int curY = PADDING_V;
        int newHover = -1;

        for (size_t i = 0; i < pState->items.size(); ++i)
        {
            const auto& item = pState->items[i];
            int itemH = item.isSeparator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
            if (y >= curY && y < curY + itemH)
            {
                if (!item.isSeparator && item.isEnabled)
                {
                    newHover = (int)i;
                }
                break;
            }
            curY += itemH;
        }

        if (newHover != pState->hoveredIndex)
        {
            pState->hoveredIndex = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) break;
        if (pState->hoveredIndex >= 0 && pState->hoveredIndex < (int)pState->items.size())
        {
            const auto& item = pState->items[pState->hoveredIndex];
            if (item.isEnabled && !item.isSeparator)
            {
                pState->selectedId = item.id;
                pState->isDone = true;
                DestroyWindow(hWnd);
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            if (pState)
            {
                pState->selectedId = 0;
                pState->isDone = true;
                DestroyWindow(hWnd);
                return 0;
            }
        }
        break;
    }

    case WM_KILLFOCUS:
    {
        if (pState && !pState->isDone)
        {
            pState->selectedId = 0;
            pState->isDone = true;
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            pState->isDone = true;
        }
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int ModernContextMenu::Show(HWND hParent, int x, int y, const std::vector<ContextMenuItem>& items, BOOL bDarkMode, int minWidth)
{
    if (items.empty()) return 0;

    static bool s_registered = false;
    HINSTANCE hInst = GetModuleHandle(NULL);
    if (!s_registered)
    {
        WNDCLASSEXW wcx = { 0 };
        wcx.cbSize = sizeof(wcx);
        wcx.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcx.lpfnWndProc = ModernContextMenuWndProc;
        wcx.hInstance = hInst;
        wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcx.hbrBackground = NULL;
        wcx.lpszClassName = L"ModernContextMenuClass";
        RegisterClassExW(&wcx);
        s_registered = true;
    }

    ModernContextMenuState state;
    state.hWnd = NULL;
    state.hParent = hParent;
    state.items = items;
    state.bDarkMode = bDarkMode;
    state.hoveredIndex = -1;
    state.selectedId = 0;
    state.isDone = false;
    state.maxShortcutW = 0;

    state.hFontText = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    state.hFontShortcuts = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    state.hFontIcons = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
    if (!state.hFontIcons)
    {
        state.hFontIcons = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

    // Compute total menu height & width with full text and shortcut measurement
    int totalH = PADDING_V * 2;
    int maxTextW = 80;
    int maxShortcutW = 0;
    HDC hScreenDC = GetDC(NULL);
    HFONT hOldF = (HFONT)SelectObject(hScreenDC, state.hFontText);

    for (const auto& item : items)
    {
        if (item.isSeparator)
        {
            totalH += SEPARATOR_HEIGHT;
        }
        else
        {
            totalH += ITEM_HEIGHT;
            if (!item.text.empty())
            {
                SIZE szText;
                GetTextExtentPoint32W(hScreenDC, item.text.c_str(), (int)item.text.size(), &szText);
                if (szText.cx > maxTextW) maxTextW = szText.cx;
            }

            if (!item.shortcut.empty())
            {
                SelectObject(hScreenDC, state.hFontShortcuts);
                SIZE szShort;
                GetTextExtentPoint32W(hScreenDC, item.shortcut.c_str(), (int)item.shortcut.size(), &szShort);
                if (szShort.cx > maxShortcutW) maxShortcutW = szShort.cx;
                SelectObject(hScreenDC, state.hFontText);
            }
        }
    }
    SelectObject(hScreenDC, hOldF);
    ReleaseDC(NULL, hScreenDC);

    state.maxShortcutW = maxShortcutW;

    int leftIconPad = 40; // icon area
    int gap = (maxShortcutW > 0) ? 24 : 16;
    int rightPad = 16;
    int calculatedW = leftIconPad + maxTextW + gap + maxShortcutW + rightPad;
    int menuW = (std::max)({ MIN_MENU_WIDTH, minWidth, calculatedW });

    // Screen bounds adjustment
    RECT rcWork;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x + menuW > rcWork.right) x = rcWork.right - menuW - 6;
    if (y + totalH > rcWork.bottom) y = rcWork.bottom - totalH - 6;
    if (x < rcWork.left) x = rcWork.left + 6;
    if (y < rcWork.top) y = rcWork.top + 6;

    HWND hMenuWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ModernContextMenuClass", L"",
        WS_POPUP | WS_VISIBLE,
        x, y, menuW, totalH,
        hParent, NULL, hInst, &state
    );

    if (!hMenuWnd)
    {
        if (state.hFontText) DeleteObject(state.hFontText);
        if (state.hFontShortcuts) DeleteObject(state.hFontShortcuts);
        if (state.hFontIcons) DeleteObject(state.hFontIcons);
        return 0;
    }

    // Apply DWM Dark Mode & Rounded Corners
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hMenuWnd, (DWMWINDOWATTRIBUTE)20, &useDark, sizeof(useDark));
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hMenuWnd, (DWMWINDOWATTRIBUTE)33, &corner, sizeof(corner));

    SetFocus(hMenuWnd);
    SetCapture(hMenuWnd);

    // Modal message loop
    MSG msg;
    while (!state.isDone && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
        {
            POINT pt = msg.pt;
            RECT rcWindow;
            GetWindowRect(hMenuWnd, &rcWindow);
            if (!PtInRect(&rcWindow, pt))
            {
                // Clicked outside menu -> dismiss
                state.selectedId = 0;
                state.isDone = true;
                break;
            }
        }
        else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            state.selectedId = 0;
            state.isDone = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();
    if (IsWindow(hMenuWnd))
    {
        DestroyWindow(hMenuWnd);
    }

    if (state.hFontText) DeleteObject(state.hFontText);
    if (state.hFontShortcuts) DeleteObject(state.hFontShortcuts);
    if (state.hFontIcons) DeleteObject(state.hFontIcons);

    return state.selectedId;
}
