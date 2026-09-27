#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "CustomDropDownMenu.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>

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

namespace
{
    static HWND g_hActiveDropDownWnd = NULL;

    struct DropDownInternalState
    {
        HWND hWnd = NULL;
        HWND hParent = NULL;
        RECT rcAnchorScreen = { 0 };
        std::vector<DropDownItem> items;
        bool isMultiSelect = false;
        int selectedId = 0;
        int hoverIndex = -1;
        bool isDone = false;
        int itemHeight = 34;
        int scrollOffset = 0;
        int maxVisibleItems = 10;
        int contentHeight = 0;
        int viewportHeight = 0;
        bool isDraggingScroll = false;
        int dragStartY = 0;
        int dragStartOffset = 0;

        // Callbacks
        std::function<void(const std::vector<DropDownItem>&)> onItemToggled = nullptr;

        // GDI Resources
        HFONT hFontMain = NULL;
        HFONT hFontBold = NULL;
        HFONT hFontSmall = NULL;
        HFONT hFontIcon = NULL;
        HFONT hFontIconLg = NULL;
    };

    static DropDownInternalState* g_pCurrentState = nullptr;

    static HFONT CreateFluentFont(int pointSize, int weight, const wchar_t* faceName)
    {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
        lf.lfWeight = weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, faceName);
        return CreateFontIndirectW(&lf);
    }

    static void ForwardOutsideClick(HWND hPopupWnd, POINT ptScreen, UINT msg, WPARAM wParam)
    {
        ReleaseCapture();
        if (hPopupWnd && IsWindow(hPopupWnd))
        {
            DestroyWindow(hPopupWnd);
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
                UINT postMsg = (msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
                PostMessageW(hClicked, postMsg, wParam, MAKELPARAM(ptLocal.x, ptLocal.y));
            }
            else if (ht != HTNOWHERE && ht != HTERROR)
            {
                UINT postNcMsg = (msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN) ? WM_NCRBUTTONDOWN : WM_NCLBUTTONDOWN;
                PostMessageW(hClicked, postNcMsg, (WPARAM)ht, MAKELPARAM(ptScreen.x, ptScreen.y));
            }
        }
    }

    static LRESULT CALLBACK DropDownWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        DropDownInternalState* pState = (DropDownInternalState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_NCCREATE:
        {
            LPCREATESTRUCTW cs = (LPCREATESTRUCTW)lParam;
            pState = (DropDownInternalState*)cs->lpCreateParams;
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

            COLORREF bgMenu      = RGB(38, 38, 42);
            COLORREF borderCol   = RGB(65, 68, 76);
            COLORREF hoverBg     = RGB(56, 60, 70);
            COLORREF textNorm    = RGB(240, 242, 248);
            COLORREF textSec     = RGB(160, 168, 180);
            COLORREF textDis     = RGB(110, 115, 125);
            COLORREF accentCol   = RGB(0, 120, 215);
            COLORREF goldCol     = RGB(255, 185, 0);

            HBRUSH hbrBg = CreateSolidBrush(bgMenu);
            FillRect(hMemDC, &rcClient, hbrBg);
            DeleteObject(hbrBg);

            SetBkMode(hMemDC, TRANSPARENT);

            bool hasScroll = (pState->contentHeight > pState->viewportHeight);
            int itemRight = hasScroll ? (w - 14) : (w - 6);

            int startIdx = pState->scrollOffset / pState->itemHeight;
            int curY = 6 - (pState->scrollOffset % pState->itemHeight);

            // Clip viewport
            RECT rcClip = { 0, 0, w, h };
            HRGN hRgnClip = CreateRectRgn(rcClip.left, rcClip.top + 4, rcClip.right, rcClip.bottom - 4);
            SelectClipRgn(hMemDC, hRgnClip);

            for (size_t i = startIdx; i < pState->items.size(); ++i)
            {
                if (curY >= h) break;

                const auto& it = pState->items[i];
                RECT rcItem = { 6, curY, itemRight, curY + pState->itemHeight };
                bool isHover = ((int)i == pState->hoverIndex && it.isEnabled);

                if (isHover)
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

                int leftTextOffset = rcItem.left + 10;

                if (pState->isMultiSelect)
                {
                    // Checkbox
                    RECT rcBox = { rcItem.left + 8, rcItem.top + (pState->itemHeight - 16) / 2, rcItem.left + 24, rcItem.top + (pState->itemHeight - 16) / 2 + 16 };
                    bool isBoxFilled = (it.isChecked || it.isIndeterminate);

                    HBRUSH hBoxBr = CreateSolidBrush(isBoxFilled ? accentCol : RGB(32, 32, 34));
                    HPEN hBoxPen = CreatePen(PS_SOLID, 1, isBoxFilled ? accentCol : borderCol);
                    HBRUSH hOldBoxB = (HBRUSH)SelectObject(hMemDC, hBoxBr);
                    HPEN hOldBoxP = (HPEN)SelectObject(hMemDC, hBoxPen);
                    RoundRect(hMemDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);
                    SelectObject(hMemDC, hOldBoxB);
                    SelectObject(hMemDC, hOldBoxP);
                    DeleteObject(hBoxBr);
                    DeleteObject(hBoxPen);

                    if (it.isChecked)
                    {
                        SelectObject(hMemDC, pState->hFontIcon);
                        SetTextColor(hMemDC, RGB(255, 255, 255));
                        RECT rcGlyph = { rcBox.left, rcBox.top - 1, rcBox.right, rcBox.bottom };
                        DrawTextW(hMemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                    else if (it.isIndeterminate)
                    {
                        HBRUSH hDotBr = CreateSolidBrush(RGB(255, 255, 255));
                        RECT rcDot = { rcBox.left + 4, rcBox.top + 4, rcBox.right - 4, rcBox.bottom - 4 };
                        FillRect(hMemDC, &rcDot, hDotBr);
                        DeleteObject(hDotBr);
                    }

                    leftTextOffset = rcBox.right + 10;

                    // Optional Header star/icon
                    if (it.isHeader)
                    {
                        SelectObject(hMemDC, pState->hFontIcon);
                        SetTextColor(hMemDC, goldCol);
                        RECT rcStar = { leftTextOffset, rcItem.top, leftTextOffset + 18, rcItem.bottom };
                        DrawTextW(hMemDC, L"\xE735", -1, &rcStar, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        leftTextOffset += 22;
                    }
                }
                else
                {
                    // Single select icon or check
                    if (!it.icon.empty())
                    {
                        SelectObject(hMemDC, pState->hFontIcon);
                        SetTextColor(hMemDC, it.isChecked ? accentCol : (it.isEnabled ? textSec : textDis));
                        RECT rcIcon = { rcItem.left + 8, rcItem.top, rcItem.left + 26, rcItem.bottom };
                        DrawTextW(hMemDC, it.icon.c_str(), -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        leftTextOffset = rcIcon.right + 8;
                    }
                }

                // Main Text
                SelectObject(hMemDC, it.isHeader ? pState->hFontBold : pState->hFontMain);
                SetTextColor(hMemDC, it.isEnabled ? (it.isChecked && !pState->isMultiSelect ? accentCol : textNorm) : textDis);

                int textRight = rcItem.right - 10;
                if (!it.secondaryText.empty())
                {
                    // Measure secondary text
                    SelectObject(hMemDC, pState->hFontSmall);
                    SIZE secSz = { 0 };
                    GetTextExtentPoint32W(hMemDC, it.secondaryText.c_str(), (int)it.secondaryText.length(), &secSz);

                    RECT rcSec = { rcItem.right - secSz.cx - 8, rcItem.top, rcItem.right - 8, rcItem.bottom };
                    SetTextColor(hMemDC, it.isEnabled ? textSec : textDis);
                    DrawTextW(hMemDC, it.secondaryText.c_str(), -1, &rcSec, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    textRight = rcSec.left - 8;
                }

                SelectObject(hMemDC, it.isHeader ? pState->hFontBold : pState->hFontMain);
                SetTextColor(hMemDC, it.isEnabled ? (it.isChecked && !pState->isMultiSelect ? accentCol : textNorm) : textDis);
                RECT rcText = { leftTextOffset, rcItem.top, textRight, rcItem.bottom };
                DrawTextW(hMemDC, it.text.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                curY += pState->itemHeight;
            }

            SelectClipRgn(hMemDC, NULL);
            DeleteObject(hRgnClip);

            // Draw Scrollbar if needed
            if (hasScroll)
            {
                int trackX = w - 10;
                int trackY = 6;
                int trackH = h - 12;

                int thumbH = (std::max)(18, trackH * pState->viewportHeight / pState->contentHeight);
                int maxOffset = pState->contentHeight - pState->viewportHeight;
                int thumbY = trackY + (maxOffset > 0 ? (pState->scrollOffset * (trackH - thumbH) / maxOffset) : 0);

                RECT rcThumb = { trackX, thumbY, trackX + 4, thumbY + thumbH };
                HBRUSH hbrThumb = CreateSolidBrush(pState->isDraggingScroll ? RGB(120, 125, 135) : RGB(80, 85, 95));
                FillRect(hMemDC, &rcThumb, hbrThumb);
                DeleteObject(hbrThumb);
            }

            // Outer Frame Border
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
            SelectObject(hMemDC, hPenBorder);
            SelectObject(hMemDC, GetStockObject(NULL_BRUSH));
            RoundRect(hMemDC, 0, 0, w, h, 8, 8);
            DeleteObject(hPenBorder);

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
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (pState->isDraggingScroll)
            {
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                int trackH = rcClient.bottom - 12;
                int thumbH = (std::max)(18, trackH * pState->viewportHeight / pState->contentHeight);
                int maxOffset = pState->contentHeight - pState->viewportHeight;
                int deltaY = y - pState->dragStartY;
                int trackTravel = trackH - thumbH;
                if (trackTravel > 0)
                {
                    int newOffset = pState->dragStartOffset + (deltaY * maxOffset / trackTravel);
                    pState->scrollOffset = (std::max)(0, (std::min)(maxOffset, newOffset));
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            int hitIdx = -1;
            if (x >= 0 && x <= rcClient.right && y >= 4 && y < rcClient.bottom - 4)
            {
                int virtualY = y - 6 + pState->scrollOffset;
                if (virtualY >= 0)
                {
                    hitIdx = virtualY / pState->itemHeight;
                    if (hitIdx >= (int)pState->items.size()) hitIdx = -1;
                }
            }

            if (hitIdx != pState->hoverIndex)
            {
                pState->hoverIndex = hitIdx;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (!pState) break;
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int maxOffset = (std::max)(0, pState->contentHeight - pState->viewportHeight);
            if (maxOffset > 0)
            {
                int step = pState->itemHeight * 2;
                pState->scrollOffset -= (zDelta > 0 ? step : -step);
                pState->scrollOffset = (std::max)(0, (std::min)(maxOffset, pState->scrollOffset));
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            if (!pState) break;
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            // Outside click handling
            if (x < 0 || y < 0 || x >= rcClient.right || y >= rcClient.bottom)
            {
                POINT ptScreen = { x, y };
                ClientToScreen(hWnd, &ptScreen);

                pState->isDone = true;

                // Check if click was inside anchor rect (toggle behavior)
                if (PtInRect(&pState->rcAnchorScreen, ptScreen))
                {
                    ReleaseCapture();
                    DestroyWindow(hWnd);
                    return 0;
                }

                // Click is on another window or titlebar: forward cleanly
                ForwardOutsideClick(hWnd, ptScreen, uMsg, wParam);
                return 0;
            }

            // Scrollbar track hit test
            bool hasScroll = (pState->contentHeight > pState->viewportHeight);
            if (hasScroll && x >= rcClient.right - 14)
            {
                pState->isDraggingScroll = true;
                pState->dragStartY = y;
                pState->dragStartOffset = pState->scrollOffset;
                SetCapture(hWnd);
                return 0;
            }

            // Item click
            int virtualY = y - 6 + pState->scrollOffset;
            if (virtualY >= 0)
            {
                int hitIdx = virtualY / pState->itemHeight;
                if (hitIdx >= 0 && hitIdx < (int)pState->items.size())
                {
                    auto& it = pState->items[hitIdx];
                    if (it.isEnabled)
                    {
                        if (pState->isMultiSelect)
                        {
                            if (it.isHeader)
                            {
                                // Header toggles all
                                bool nextAll = !it.isChecked;
                                for (auto& item : pState->items)
                                {
                                    item.isChecked = nextAll;
                                    item.isIndeterminate = false;
                                }
                            }
                            else
                            {
                                it.isChecked = !it.isChecked;

                                // Update header indeterminate / checked state
                                size_t totalRegular = 0;
                                size_t checkedRegular = 0;
                                for (size_t k = 0; k < pState->items.size(); ++k)
                                {
                                    if (!pState->items[k].isHeader)
                                    {
                                        totalRegular++;
                                        if (pState->items[k].isChecked) checkedRegular++;
                                    }
                                }
                                for (auto& item : pState->items)
                                {
                                    if (item.isHeader)
                                    {
                                        item.isChecked = (totalRegular > 0 && checkedRegular == totalRegular);
                                        item.isIndeterminate = (checkedRegular > 0 && checkedRegular < totalRegular);
                                    }
                                }
                            }

                            if (pState->onItemToggled)
                            {
                                pState->onItemToggled(pState->items);
                            }

                            InvalidateRect(hWnd, NULL, FALSE);
                            if (pState->hParent && IsWindow(pState->hParent))
                            {
                                InvalidateRect(pState->hParent, NULL, TRUE);
                            }
                            return 0;
                        }
                        else
                        {
                            // Single-select: choose and close
                            pState->selectedId = it.id;
                            pState->isDone = true;
                            ReleaseCapture();
                            DestroyWindow(hWnd);
                            return 0;
                        }
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONUP:
        {
            if (pState && pState->isDraggingScroll)
            {
                pState->isDraggingScroll = false;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
        {
            if (!pState) break;
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            POINT ptScreen = { x, y };
            ClientToScreen(hWnd, &ptScreen);
            pState->isDone = true;

            if (PtInRect(&pState->rcAnchorScreen, ptScreen))
            {
                ReleaseCapture();
                DestroyWindow(hWnd);
                return 0;
            }

            ForwardOutsideClick(hWnd, ptScreen, uMsg, wParam);
            return 0;
        }

        case WM_KEYDOWN:
        {
            if (!pState) break;
            if (wParam == VK_ESCAPE)
            {
                pState->selectedId = 0;
                pState->isDone = true;
                ReleaseCapture();
                DestroyWindow(hWnd);
                return 0;
            }
            else if (wParam == VK_UP)
            {
                pState->hoverIndex = (std::max)(0, pState->hoverIndex - 1);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (wParam == VK_DOWN)
            {
                pState->hoverIndex = (std::min)((int)pState->items.size() - 1, pState->hoverIndex + 1);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (wParam == VK_RETURN || (wParam == VK_SPACE && pState->isMultiSelect))
            {
                if (pState->hoverIndex >= 0 && pState->hoverIndex < (int)pState->items.size())
                {
                    if (pState->isMultiSelect)
                    {
                        pState->items[pState->hoverIndex].isChecked = !pState->items[pState->hoverIndex].isChecked;
                        if (pState->onItemToggled) pState->onItemToggled(pState->items);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else
                    {
                        pState->selectedId = pState->items[pState->hoverIndex].id;
                        pState->isDone = true;
                        ReleaseCapture();
                        DestroyWindow(hWnd);
                    }
                }
                return 0;
            }
            break;
        }

        case WM_DESTROY:
        {
            if (pState)
            {
                if (pState->hFontMain) DeleteObject(pState->hFontMain);
                if (pState->hFontBold) DeleteObject(pState->hFontBold);
                if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
                if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
                if (pState->hFontIconLg) DeleteObject(pState->hFontIconLg);
                pState->hFontMain = NULL;
                pState->hFontBold = NULL;
                pState->hFontSmall = NULL;
                pState->hFontIcon = NULL;
                pState->hFontIconLg = NULL;
                pState->isDone = true;
            }
            g_hActiveDropDownWnd = NULL;
            return 0;
        }
        }

        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    static void RegisterDropDownClass()
    {
        static bool s_registered = false;
        if (s_registered) return;

        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcex.lpfnWndProc = DropDownWndProc;
        wcex.hInstance = GetModuleHandleW(NULL);
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = CreateSolidBrush(RGB(38, 38, 42));
        wcex.lpszClassName = L"FluentCustomDropDownMenu";
        RegisterClassExW(&wcex);
        s_registered = true;
    }
}

int CustomDropDownMenu::ShowSingleSelect(
    HWND hParent,
    const RECT& rcAnchor,
    const std::vector<DropDownItem>& items,
    int selectedId,
    int minWidth)
{
    if (items.empty()) return 0;
    CloseActive();

    RegisterDropDownClass();

    DropDownInternalState state;
    state.hParent = hParent;
    state.items = items;
    state.isMultiSelect = false;
    state.selectedId = 0;
    state.hoverIndex = -1;
    state.isDone = false;
    state.itemHeight = 34;
    state.maxVisibleItems = 12;

    for (size_t i = 0; i < state.items.size(); ++i)
    {
        if (state.items[i].id == selectedId)
        {
            state.items[i].isChecked = true;
            state.hoverIndex = (int)i;
        }
    }

    state.hFontMain = CreateFluentFont(10, FW_NORMAL, L"Segoe UI Variable Text");
    if (!state.hFontMain) state.hFontMain = CreateFluentFont(10, FW_NORMAL, L"Segoe UI");
    state.hFontBold = CreateFluentFont(10, FW_SEMIBOLD, L"Segoe UI Variable Text");
    if (!state.hFontBold) state.hFontBold = CreateFluentFont(10, FW_SEMIBOLD, L"Segoe UI");
    state.hFontSmall = CreateFluentFont(9, FW_NORMAL, L"Segoe UI Variable Text");
    if (!state.hFontSmall) state.hFontSmall = CreateFluentFont(9, FW_NORMAL, L"Segoe UI");
    state.hFontIcon = CreateFluentFont(11, FW_NORMAL, L"Segoe Fluent Icons");
    if (!state.hFontIcon) state.hFontIcon = CreateFluentFont(11, FW_NORMAL, L"Segoe MDL2 Assets");
    state.hFontIconLg = CreateFluentFont(13, FW_NORMAL, L"Segoe Fluent Icons");
    if (!state.hFontIconLg) state.hFontIconLg = CreateFluentFont(13, FW_NORMAL, L"Segoe MDL2 Assets");

    // Measure widths
    HDC hdcScreen = GetDC(NULL);
    HFONT holdF = (HFONT)SelectObject(hdcScreen, state.hFontMain);
    int maxTextW = 120;
    int maxSecW = 0;

    for (const auto& it : state.items)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdcScreen, it.text.c_str(), (int)it.text.length(), &sz);
        if (sz.cx > maxTextW) maxTextW = sz.cx;

        if (!it.secondaryText.empty())
        {
            SelectObject(hdcScreen, state.hFontSmall);
            SIZE secSz;
            GetTextExtentPoint32W(hdcScreen, it.secondaryText.c_str(), (int)it.secondaryText.length(), &secSz);
            if (secSz.cx > maxSecW) maxSecW = secSz.cx;
            SelectObject(hdcScreen, state.hFontMain);
        }
    }
    SelectObject(hdcScreen, holdF);
    ReleaseDC(NULL, hdcScreen);

    RECT rcScreen = rcAnchor;
    MapWindowPoints(hParent, NULL, (LPPOINT)&rcScreen, 2);
    state.rcAnchorScreen = rcScreen;

    int anchorW = rcScreen.right - rcScreen.left;
    int calculatedW = 48 + maxTextW + (maxSecW > 0 ? (maxSecW + 24) : 0) + 24;
    int popupW = (std::max)({ anchorW, minWidth, calculatedW });

    int totalItems = (int)state.items.size();
    int visibleItems = (std::min)(totalItems, state.maxVisibleItems);
    state.contentHeight = 12 + totalItems * state.itemHeight;
    state.viewportHeight = 12 + visibleItems * state.itemHeight;
    int popupH = state.viewportHeight;

    int x = rcScreen.left;
    int y = rcScreen.bottom + 4;

    HMONITOR hMon = MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (x + popupW > mi.rcWork.right) x = mi.rcWork.right - popupW - 6;
        if (x < mi.rcWork.left) x = mi.rcWork.left + 6;
        if (y + popupH > mi.rcWork.bottom)
        {
            y = rcScreen.top - popupH - 4; // Flip above anchor
        }
    }

    g_pCurrentState = &state;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"FluentCustomDropDownMenu", L"",
        WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
        x, y, popupW, popupH,
        hParent, NULL, GetModuleHandleW(NULL), &state
    );

    if (!hWnd) return 0;
    g_hActiveDropDownWnd = hWnd;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    SetCapture(hWnd);

    // Modal message loop
    MSG msg;
    while (!state.isDone && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            state.selectedId = 0;
            state.isDone = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();
    if (IsWindow(hWnd))
    {
        DestroyWindow(hWnd);
    }
    g_hActiveDropDownWnd = NULL;
    g_pCurrentState = nullptr;

    return state.selectedId;
}

bool CustomDropDownMenu::ShowMultiSelect(
    HWND hParent,
    const RECT& rcAnchor,
    std::vector<DropDownItem>& items,
    std::function<void(const std::vector<DropDownItem>&)> onItemToggledCallback,
    int minWidth)
{
    if (items.empty()) return false;
    CloseActive();

    RegisterDropDownClass();

    DropDownInternalState state;
    state.hParent = hParent;
    state.items = items;
    state.isMultiSelect = true;
    state.selectedId = 0;
    state.hoverIndex = -1;
    state.isDone = false;
    state.itemHeight = 34;
    state.maxVisibleItems = 12;
    state.onItemToggled = onItemToggledCallback;

    state.hFontMain = CreateFluentFont(10, FW_NORMAL, L"Segoe UI Variable Text");
    if (!state.hFontMain) state.hFontMain = CreateFluentFont(10, FW_NORMAL, L"Segoe UI");
    state.hFontBold = CreateFluentFont(10, FW_SEMIBOLD, L"Segoe UI Variable Text");
    if (!state.hFontBold) state.hFontBold = CreateFluentFont(10, FW_SEMIBOLD, L"Segoe UI");
    state.hFontSmall = CreateFluentFont(9, FW_NORMAL, L"Segoe UI Variable Text");
    if (!state.hFontSmall) state.hFontSmall = CreateFluentFont(9, FW_NORMAL, L"Segoe UI");
    state.hFontIcon = CreateFluentFont(11, FW_NORMAL, L"Segoe Fluent Icons");
    if (!state.hFontIcon) state.hFontIcon = CreateFluentFont(11, FW_NORMAL, L"Segoe MDL2 Assets");
    state.hFontIconLg = CreateFluentFont(13, FW_NORMAL, L"Segoe Fluent Icons");
    if (!state.hFontIconLg) state.hFontIconLg = CreateFluentFont(13, FW_NORMAL, L"Segoe MDL2 Assets");

    // Measure widths
    HDC hdcScreen = GetDC(NULL);
    HFONT holdF = (HFONT)SelectObject(hdcScreen, state.hFontMain);
    int maxTextW = 120;
    int maxSecW = 0;

    for (const auto& it : state.items)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdcScreen, it.text.c_str(), (int)it.text.length(), &sz);
        if (sz.cx > maxTextW) maxTextW = sz.cx;

        if (!it.secondaryText.empty())
        {
            SelectObject(hdcScreen, state.hFontSmall);
            SIZE secSz;
            GetTextExtentPoint32W(hdcScreen, it.secondaryText.c_str(), (int)it.secondaryText.length(), &secSz);
            if (secSz.cx > maxSecW) maxSecW = secSz.cx;
            SelectObject(hdcScreen, state.hFontMain);
        }
    }
    SelectObject(hdcScreen, holdF);
    ReleaseDC(NULL, hdcScreen);

    RECT rcScreen = rcAnchor;
    MapWindowPoints(hParent, NULL, (LPPOINT)&rcScreen, 2);
    state.rcAnchorScreen = rcScreen;

    int anchorW = rcScreen.right - rcScreen.left;
    int calculatedW = 56 + maxTextW + (maxSecW > 0 ? (maxSecW + 24) : 0) + 24;
    int popupW = (std::max)({ anchorW, minWidth, calculatedW });

    int totalItems = (int)state.items.size();
    int visibleItems = (std::min)(totalItems, state.maxVisibleItems);
    state.contentHeight = 12 + totalItems * state.itemHeight;
    state.viewportHeight = 12 + visibleItems * state.itemHeight;
    int popupH = state.viewportHeight;

    int x = rcScreen.left;
    int y = rcScreen.bottom + 4;

    HMONITOR hMon = MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (x + popupW > mi.rcWork.right) x = mi.rcWork.right - popupW - 6;
        if (x < mi.rcWork.left) x = mi.rcWork.left + 6;
        if (y + popupH > mi.rcWork.bottom)
        {
            y = rcScreen.top - popupH - 4; // Flip above anchor
        }
    }

    g_pCurrentState = &state;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"FluentCustomDropDownMenu", L"",
        WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
        x, y, popupW, popupH,
        hParent, NULL, GetModuleHandleW(NULL), &state
    );

    if (!hWnd) return false;
    g_hActiveDropDownWnd = hWnd;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    SetCapture(hWnd);

    // Modal message loop
    MSG msg;
    while (!state.isDone && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            state.isDone = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();
    if (IsWindow(hWnd))
    {
        DestroyWindow(hWnd);
    }
    g_hActiveDropDownWnd = NULL;
    g_pCurrentState = nullptr;

    items = state.items;
    return true;
}

void CustomDropDownMenu::CloseActive()
{
    if (g_hActiveDropDownWnd && IsWindow(g_hActiveDropDownWnd))
    {
        ReleaseCapture();
        DestroyWindow(g_hActiveDropDownWnd);
        g_hActiveDropDownWnd = NULL;
    }
}

bool CustomDropDownMenu::IsActive()
{
    return (g_hActiveDropDownWnd != NULL && IsWindow(g_hActiveDropDownWnd));
}
