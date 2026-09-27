#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ModernContextMenu.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

struct ModernContextMenuState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    std::vector<ContextMenuItem> items;
    BOOL bDarkMode = TRUE;
    int hoveredIndex = -1;
    int selectedId = 0;
    bool isDone = false;
    int maxShortcutW = 0;
    HFONT hFontText = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontShortcuts = NULL;

    // Submenu cascading
    HWND hSubMenuWnd = NULL;
    int activeSubMenuIndex = -1;
    ModernContextMenuState* pSubMenuState = nullptr;
    ModernContextMenuState* pParentMenuState = nullptr;
};

static const int ITEM_HEIGHT = 32;
static const int SEPARATOR_HEIGHT = 9;
static const int PADDING_V = 6;
static const int MIN_MENU_WIDTH = 250;

static LRESULT CALLBACK ModernContextMenuWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static HWND CreateMenuWindow(HWND hParent, int x, int y, const std::vector<ContextMenuItem>& items, BOOL bDarkMode, int minWidth, ModernContextMenuState* pParentState, ModernContextMenuState* pStateOut);

static bool IsPointInMenuChain(ModernContextMenuState* pState, POINT pt)
{
    if (!pState || !pState->hWnd || !IsWindow(pState->hWnd)) return false;
    RECT rc;
    GetWindowRect(pState->hWnd, &rc);
    if (PtInRect(&rc, pt)) return true;

    if (pState->pSubMenuState)
    {
        return IsPointInMenuChain(pState->pSubMenuState, pt);
    }
    return false;
}

static HWND FindMenuWindowUnderPoint(ModernContextMenuState* pState, POINT pt)
{
    if (!pState || !pState->hWnd || !IsWindow(pState->hWnd)) return NULL;
    if (pState->pSubMenuState && pState->pSubMenuState->hWnd && IsWindow(pState->pSubMenuState->hWnd))
    {
        HWND hChildHit = FindMenuWindowUnderPoint(pState->pSubMenuState, pt);
        if (hChildHit) return hChildHit;
    }
    RECT rc;
    GetWindowRect(pState->hWnd, &rc);
    if (PtInRect(&rc, pt)) return pState->hWnd;
    return NULL;
}

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
                bool isHovered = (((int)i == pState->hoveredIndex) || ((int)i == pState->activeSubMenuIndex)) && item.isEnabled;

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

                int rightMargin = 12;
                int textRight = rcItem.right - rightMargin;
                if (!item.subItems.empty())
                {
                    textRight = rcItem.right - 26;
                }
                else if (pState->maxShortcutW > 0)
                {
                    textRight = rcItem.right - pState->maxShortcutW - 16;
                }

                // 2. Item Text
                if (pState->hFontText && !item.text.empty())
                {
                    HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
                    SetTextColor(hMemDC, item.isEnabled ? textNorm : textDis);
                    RECT rcText = { rcItem.left + 32, rcItem.top, textRight, rcItem.bottom };
                    DrawTextW(hMemDC, item.text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(hMemDC, hOldF);
                }

                // 3. Submenu Chevron or Shortcut Text
                if (!item.subItems.empty())
                {
                    if (pState->hFontIcons)
                    {
                        HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcons);
                        SetTextColor(hMemDC, item.isEnabled ? textShort : textDis);
                        RECT rcChev = { rcItem.right - 22, rcItem.top, rcItem.right - 6, rcItem.bottom };
                        DrawTextW(hMemDC, L"\xE76C", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        SelectObject(hMemDC, hOldF);
                    }
                }
                else if (pState->hFontShortcuts && !item.shortcut.empty())
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

            // If switching away from currently open submenu item, close it
            if (pState->hSubMenuWnd && newHover != -1 && newHover != pState->activeSubMenuIndex)
            {
                if (IsWindow(pState->hSubMenuWnd))
                {
                    DestroyWindow(pState->hSubMenuWnd);
                }
                pState->hSubMenuWnd = NULL;
                pState->activeSubMenuIndex = -1;
                if (pState->pSubMenuState)
                {
                    delete pState->pSubMenuState;
                    pState->pSubMenuState = nullptr;
                }
            }

            // If hovered over an item with subItems, open the sub-menu!
            if (newHover >= 0 && newHover < (int)pState->items.size())
            {
                const auto& item = pState->items[newHover];
                if (!item.subItems.empty() && item.isEnabled && pState->activeSubMenuIndex != newHover)
                {
                    int itemY = PADDING_V;
                    for (int k = 0; k < newHover; ++k)
                    {
                        itemY += pState->items[k].isSeparator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
                    }

                    RECT rcClient;
                    GetClientRect(hWnd, &rcClient);
                    POINT ptSub = { rcClient.right - 2, itemY - 4 };
                    ClientToScreen(hWnd, &ptSub);

                    if (!pState->pSubMenuState)
                    {
                        pState->pSubMenuState = new ModernContextMenuState();
                    }

                    HWND hSub = CreateMenuWindow(hWnd, ptSub.x, ptSub.y, item.subItems, pState->bDarkMode, 0, pState, pState->pSubMenuState);
                    if (hSub)
                    {
                        pState->hSubMenuWnd = hSub;
                        pState->activeSubMenuIndex = newHover;
                    }
                }
            }

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
                if (!item.subItems.empty())
                {
                    // If clicked on submenu item and not already open, open it
                    if (!pState->hSubMenuWnd)
                    {
                        int itemY = PADDING_V;
                        for (int k = 0; k < pState->hoveredIndex; ++k)
                        {
                            itemY += pState->items[k].isSeparator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
                        }

                        RECT rcClient;
                        GetClientRect(hWnd, &rcClient);
                        POINT ptSub = { rcClient.right - 2, itemY - 4 };
                        ClientToScreen(hWnd, &ptSub);

                        if (!pState->pSubMenuState)
                        {
                            pState->pSubMenuState = new ModernContextMenuState();
                        }

                        HWND hSub = CreateMenuWindow(hWnd, ptSub.x, ptSub.y, item.subItems, pState->bDarkMode, 0, pState, pState->pSubMenuState);
                        if (hSub)
                        {
                            pState->hSubMenuWnd = hSub;
                            pState->activeSubMenuIndex = pState->hoveredIndex;
                        }
                    }
                    return 0;
                }
                else if (item.id > 0)
                {
                    // Action item selected! Bubble up to root
                    int chosenId = item.id;
                    ModernContextMenuState* pCur = pState;
                    while (pCur)
                    {
                        pCur->selectedId = chosenId;
                        pCur->isDone = true;
                        pCur = pCur->pParentMenuState;
                    }
                    DestroyWindow(hWnd);
                    return 0;
                }
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
                if (pState->pParentMenuState)
                {
                    pState->pParentMenuState->selectedId = 0;
                    pState->pParentMenuState->isDone = true;
                }
                DestroyWindow(hWnd);
                return 0;
            }
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hSubMenuWnd && IsWindow(pState->hSubMenuWnd))
            {
                DestroyWindow(pState->hSubMenuWnd);
                pState->hSubMenuWnd = NULL;
                pState->activeSubMenuIndex = -1;
            }
            if (pState->pSubMenuState)
            {
                delete pState->pSubMenuState;
                pState->pSubMenuState = nullptr;
            }
            if (pState->hFontText) DeleteObject(pState->hFontText);
            if (pState->hFontShortcuts) DeleteObject(pState->hFontShortcuts);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
            pState->hFontText = NULL;
            pState->hFontShortcuts = NULL;
            pState->hFontIcons = NULL;
            pState->isDone = true;
        }
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static HWND CreateMenuWindow(HWND hParent, int x, int y, const std::vector<ContextMenuItem>& items, BOOL bDarkMode, int minWidth, ModernContextMenuState* pParentState, ModernContextMenuState* pStateOut)
{
    if (items.empty()) return NULL;

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

    ModernContextMenuState* pState = pStateOut;
    pState->hWnd = NULL;
    pState->hParent = hParent;
    pState->items = items;
    pState->bDarkMode = bDarkMode;
    pState->hoveredIndex = -1;
    pState->selectedId = 0;
    pState->isDone = false;
    pState->maxShortcutW = 0;
    pState->hSubMenuWnd = NULL;
    pState->activeSubMenuIndex = -1;
    pState->pSubMenuState = nullptr;
    pState->pParentMenuState = pParentState;

    pState->hFontText = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontShortcuts = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontIcons = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
    if (!pState->hFontIcons)
    {
        pState->hFontIcons = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

    // Compute total menu height & width
    int totalH = PADDING_V * 2;
    int maxTextW = 80;
    int maxShortcutW = 0;
    bool hasSubmenus = false;
    HDC hScreenDC = GetDC(NULL);
    HFONT hOldF = (HFONT)SelectObject(hScreenDC, pState->hFontText);

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

            if (!item.subItems.empty())
            {
                hasSubmenus = true;
            }
            else if (!item.shortcut.empty())
            {
                SelectObject(hScreenDC, pState->hFontShortcuts);
                SIZE szShort;
                GetTextExtentPoint32W(hScreenDC, item.shortcut.c_str(), (int)item.shortcut.size(), &szShort);
                if (szShort.cx > maxShortcutW) maxShortcutW = szShort.cx;
                SelectObject(hScreenDC, pState->hFontText);
            }
        }
    }
    SelectObject(hScreenDC, hOldF);
    ReleaseDC(NULL, hScreenDC);

    pState->maxShortcutW = maxShortcutW;

    int leftIconPad = 38;
    int gap = (maxShortcutW > 0 || hasSubmenus) ? 24 : 16;
    int rightPad = hasSubmenus ? 24 : 16;
    int calculatedW = leftIconPad + maxTextW + gap + maxShortcutW + rightPad;
    int menuW = (std::max)({ MIN_MENU_WIDTH, minWidth, calculatedW });

    // Screen bounds adjustment
    RECT rcWork;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x + menuW > rcWork.right)
    {
        if (pParentState && pParentState->hWnd && IsWindow(pParentState->hWnd))
        {
            RECT rcParentWin;
            GetWindowRect(pParentState->hWnd, &rcParentWin);
            x = rcParentWin.left - menuW + 2;
        }
        else
        {
            x = rcWork.right - menuW - 6;
        }
    }
    if (y + totalH > rcWork.bottom) y = rcWork.bottom - totalH - 6;
    if (x < rcWork.left) x = rcWork.left + 6;
    if (y < rcWork.top) y = rcWork.top + 6;

    HWND hMenuWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ModernContextMenuClass", L"",
        WS_POPUP | WS_VISIBLE,
        x, y, menuW, totalH,
        hParent, NULL, hInst, pState
    );

    if (!hMenuWnd) return NULL;

    // Apply DWM Dark Mode & Rounded Corners
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hMenuWnd, (DWMWINDOWATTRIBUTE)20, &useDark, sizeof(useDark));
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hMenuWnd, (DWMWINDOWATTRIBUTE)33, &corner, sizeof(corner));

    return hMenuWnd;
}

int ModernContextMenu::Show(HWND hParent, int x, int y, const std::vector<ContextMenuItem>& items, BOOL bDarkMode, int minWidth)
{
    if (items.empty()) return 0;

    ModernContextMenuState rootState;
    HWND hRootMenuWnd = CreateMenuWindow(hParent, x, y, items, bDarkMode, minWidth, nullptr, &rootState);
    if (!hRootMenuWnd) return 0;

    SetFocus(hRootMenuWnd);
    SetCapture(hRootMenuWnd);

    // Modal message loop with intelligent routing across parent and cascading submenus
    MSG msg;
    while (!rootState.isDone && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
        {
            POINT ptScreen = msg.pt;
            HWND hTargetWnd = FindMenuWindowUnderPoint(&rootState, ptScreen);

            if (hTargetWnd)
            {
                // Route message directly to targeted menu window with client coordinates
                POINT ptLocal = ptScreen;
                ScreenToClient(hTargetWnd, &ptLocal);
                msg.hwnd = hTargetWnd;
                msg.lParam = MAKELPARAM(ptLocal.x, ptLocal.y);
            }
            else
            {
                // Mouse event outside all menu windows
                if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
                {
                    rootState.selectedId = 0;
                    rootState.isDone = true;

                    POINT ptScreen = msg.pt;
                    ReleaseCapture();
                    if (IsWindow(hRootMenuWnd))
                    {
                        DestroyWindow(hRootMenuWnd);
                        hRootMenuWnd = NULL;
                    }

                    HWND hClicked = WindowFromPoint(ptScreen);
                    if (hClicked && IsWindow(hClicked))
                    {
                        LRESULT ht = SendMessageW(hClicked, WM_NCHITTEST, 0, MAKELPARAM(ptScreen.x, ptScreen.y));
                        if (ht == HTTRANSPARENT)
                        {
                            HWND hParentWin = GetParent(hClicked);
                            if (hParentWin && IsWindow(hParentWin))
                            {
                                hClicked = hParentWin;
                                ht = SendMessageW(hClicked, WM_NCHITTEST, 0, MAKELPARAM(ptScreen.x, ptScreen.y));
                            }
                        }

                        if (ht == HTCLIENT)
                        {
                            POINT ptLocal = ptScreen;
                            ScreenToClient(hClicked, &ptLocal);
                            UINT postMsg = (msg.message == WM_RBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
                            PostMessageW(hClicked, postMsg, msg.wParam, MAKELPARAM(ptLocal.x, ptLocal.y));
                        }
                        else if (ht != HTNOWHERE && ht != HTERROR)
                        {
                            UINT postNcMsg = (msg.message == WM_RBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN) ? WM_NCRBUTTONDOWN : WM_NCLBUTTONDOWN;
                            PostMessageW(hClicked, postNcMsg, (WPARAM)ht, MAKELPARAM(ptScreen.x, ptScreen.y));
                        }
                    }
                    break;
                }
            }
        }
        else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            rootState.selectedId = 0;
            rootState.isDone = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();
    if (IsWindow(hRootMenuWnd))
    {
        DestroyWindow(hRootMenuWnd);
    }

    return rootState.selectedId;
}
