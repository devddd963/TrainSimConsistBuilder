#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "CustomDropDownMenu.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <unordered_set>

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
        std::unordered_set<std::wstring> collapsedGroups;
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

    static std::vector<int> GetVisibleIndices(const DropDownInternalState& state)
    {
        std::vector<int> visible;
        visible.reserve(state.items.size());
        for (size_t i = 0; i < state.items.size(); ++i)
        {
            const auto& it = state.items[i];
            
            // If parentGroup is collapsed, hide this item
            if (!it.parentGroup.empty() && state.collapsedGroups.count(it.parentGroup) > 0)
            {
                continue;
            }

            // If this item is a group header, it is visible (since its parentGroup is expanded)
            if (it.isGroupHeader)
            {
                visible.push_back((int)i);
            }
            else if (it.group.empty() || state.collapsedGroups.count(it.group) == 0)
            {
                visible.push_back((int)i);
            }
        }
        return visible;
    }

    static void SyncHeaderCheckStates(DropDownInternalState* pState)
    {
        if (!pState || !pState->isMultiSelect) return;

        // 1. Sync all GroupHeaders (both Level 2 sub-groups and Level 1 top-groups)
        for (auto& hdr : pState->items)
        {
            if (!hdr.isGroupHeader || hdr.id <= 0) continue;

            size_t totalChild = 0;
            size_t checkedChild = 0;

            for (const auto& child : pState->items)
            {
                if (child.isGroupHeader || child.isHeader || child.isSeparator || !child.isEnabled) continue;

                // Match either direct group or matching parentGroup (for category headers)
                bool isMatch = (child.group == hdr.group) || (child.parentGroup == hdr.group);
                if (isMatch)
                {
                    totalChild++;
                    if (child.isChecked) checkedChild++;
                }
            }

            if (totalChild > 0)
            {
                hdr.isChecked = (checkedChild == totalChild);
                hdr.isIndeterminate = (checkedChild > 0 && checkedChild < totalChild);
            }
        }

        // 2. Sync Global Header (id == 1, isHeader && !isGroupHeader)
        size_t totalAllRegular = 0;
        size_t checkedAllRegular = 0;
        for (const auto& item : pState->items)
        {
            if (!item.isHeader && !item.isSeparator && item.isEnabled)
            {
                totalAllRegular++;
                if (item.isChecked) checkedAllRegular++;
            }
        }

        for (auto& item : pState->items)
        {
            if (item.isHeader && !item.isGroupHeader && item.id > 0)
            {
                item.isChecked = (totalAllRegular > 0 && checkedAllRegular == totalAllRegular);
                item.isIndeterminate = (checkedAllRegular > 0 && checkedAllRegular < totalAllRegular);
            }
        }
    }

    static void RecalculateLayout(DropDownInternalState* pState)
    {
        if (!pState) return;
        auto visible = GetVisibleIndices(*pState);
        int totalVis = (int)visible.size();
        pState->contentHeight = 12 + totalVis * pState->itemHeight;
        int maxOffset = (std::max)(0, pState->contentHeight - pState->viewportHeight);
        pState->scrollOffset = (std::max)(0, (std::min)(maxOffset, pState->scrollOffset));
    }

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

            auto visibleIndices = GetVisibleIndices(*pState);
            int startVisIdx = pState->scrollOffset / pState->itemHeight;
            int curY = 6 - (pState->scrollOffset % pState->itemHeight);

            // Clip viewport
            RECT rcClip = { 0, 0, w, h };
            HRGN hRgnClip = CreateRectRgn(rcClip.left, rcClip.top + 4, rcClip.right, rcClip.bottom - 4);
            SelectClipRgn(hMemDC, hRgnClip);

            for (size_t v = startVisIdx; v < visibleIndices.size(); ++v)
            {
                if (curY >= h) break;

                int i = visibleIndices[v];
                const auto& it = pState->items[i];
                RECT rcItem = { 6, curY, itemRight, curY + pState->itemHeight };

                // 1. Separator line rendering
                if (it.isSeparator)
                {
                    HPEN hPenSep = CreatePen(PS_SOLID, 1, RGB(65, 68, 76));
                    HPEN hOldSepP = (HPEN)SelectObject(hMemDC, hPenSep);
                    int midY = (rcItem.top + rcItem.bottom) / 2;
                    MoveToEx(hMemDC, rcItem.left + 8, midY, NULL);
                    LineTo(hMemDC, rcItem.right - 8, midY);
                    SelectObject(hMemDC, hOldSepP);
                    DeleteObject(hPenSep);
                    curY += pState->itemHeight;
                    continue;
                }

                // 2. Collapsible Group Header
                if (it.isGroupHeader)
                {
                    bool isCollapsed = (pState->collapsedGroups.count(it.group) > 0);
                    bool isHover = ((int)i == pState->hoverIndex);

                    COLORREF gBg = isHover ? RGB(48, 52, 62) : ((it.indentLevel > 0) ? RGB(28, 30, 36) : RGB(32, 34, 40));
                    COLORREF gBrd = isHover ? RGB(0, 120, 215) : ((it.indentLevel > 0) ? RGB(44, 48, 58) : RGB(50, 54, 64));
                    HBRUSH hbrGrp = CreateSolidBrush(gBg);
                    HPEN hPenGrp = CreatePen(PS_SOLID, 1, gBrd);
                    HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrGrp);
                    HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenGrp);

                    int leftIndent = (it.indentLevel == 1) ? 14 : ((it.indentLevel >= 2) ? 28 : 0);
                    RECT rcGrpDraw = { rcItem.left + leftIndent, rcItem.top + 2, rcItem.right, rcItem.bottom - 2 };
                    RoundRect(hMemDC, rcGrpDraw.left, rcGrpDraw.top, rcGrpDraw.right, rcGrpDraw.bottom, 6, 6);
                    SelectObject(hMemDC, hOldB);
                    SelectObject(hMemDC, hOldP);
                    DeleteObject(hbrGrp);
                    DeleteObject(hPenGrp);

                    int leftTextOffset = rcGrpDraw.left + 8;

                    // Chevron (Expand / Collapse indicator)
                    SelectObject(hMemDC, pState->hFontIcon);
                    SetTextColor(hMemDC, RGB(96, 205, 255));
                    RECT rcChev = { leftTextOffset, rcItem.top, leftTextOffset + 18, rcItem.bottom };
                    DrawTextW(hMemDC, isCollapsed ? L"\xE70E" : L"\xE70D", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    leftTextOffset += 20;

                    if (pState->isMultiSelect && it.id > 0)
                    {
                        // Checkbox for whole group
                        RECT rcBox = { leftTextOffset, rcItem.top + (pState->itemHeight - 16) / 2, leftTextOffset + 16, rcItem.top + (pState->itemHeight - 16) / 2 + 16 };
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
                        leftTextOffset = rcBox.right + 8;
                    }

                    // Folder / Group Icon
                    SelectObject(hMemDC, pState->hFontIcon);
                    SetTextColor(hMemDC, (it.indentLevel == 0) ? RGB(255, 185, 0) : RGB(96, 205, 255));
                    RECT rcGrpIcon = { leftTextOffset, rcItem.top, leftTextOffset + 18, rcItem.bottom };
                    const wchar_t* gIcoStr = !it.icon.empty() ? it.icon.c_str() : (isCollapsed ? L"\xE8B7" : L"\xE838");
                    DrawTextW(hMemDC, gIcoStr, -1, &rcGrpIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    leftTextOffset += 22;

                    // Group Title & Count
                    int textRight = rcItem.right - 10;
                    std::wstring countStr = it.groupCount > 0 ? (L"(" + std::to_wstring(it.groupCount) + L")") : it.secondaryText;
                    if (!countStr.empty())
                    {
                        SelectObject(hMemDC, pState->hFontSmall);
                        SIZE secSz = { 0 };
                        GetTextExtentPoint32W(hMemDC, countStr.c_str(), (int)countStr.length(), &secSz);

                        RECT rcSec = { rcItem.right - secSz.cx - 8, rcItem.top, rcItem.right - 8, rcItem.bottom };
                        SetTextColor(hMemDC, RGB(160, 168, 180));
                        DrawTextW(hMemDC, countStr.c_str(), -1, &rcSec, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        textRight = rcSec.left - 8;
                    }

                    SelectObject(hMemDC, (it.indentLevel == 0) ? pState->hFontBold : pState->hFontMain);
                    SetTextColor(hMemDC, RGB(240, 245, 255));
                    RECT rcGText = { leftTextOffset, rcItem.top, textRight, rcItem.bottom };
                    DrawTextW(hMemDC, it.text.c_str(), -1, &rcGText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    curY += pState->itemHeight;
                    continue;
                }

                // 3. Non-interactive section category title header (id <= 0)
                if (it.isHeader && it.id <= 0)
                {
                    SelectObject(hMemDC, pState->hFontSmall);
                    SetTextColor(hMemDC, RGB(140, 148, 162));
                    RECT rcHdr = { rcItem.left + 12, rcItem.top + 2, rcItem.right - 10, rcItem.bottom };
                    DrawTextW(hMemDC, it.text.c_str(), -1, &rcHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                    curY += pState->itemHeight;
                    continue;
                }

                bool isHover = ((int)i == pState->hoverIndex && it.isEnabled);
                int leftIndent = (it.indentLevel >= 2) ? 36 : ((it.indentLevel == 1) ? 20 : ((!it.group.empty()) ? 16 : 0));
                RECT rcItemDraw = { rcItem.left + leftIndent, rcItem.top, rcItem.right, rcItem.bottom };

                if (isHover)
                {
                    HBRUSH hbrHover = CreateSolidBrush(hoverBg);
                    HPEN hPenHover = CreatePen(PS_SOLID, 1, hoverBg);
                    HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrHover);
                    HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenHover);
                    RoundRect(hMemDC, rcItemDraw.left, rcItemDraw.top, rcItemDraw.right, rcItemDraw.bottom, 6, 6);
                    SelectObject(hMemDC, hOldB);
                    SelectObject(hMemDC, hOldP);
                    DeleteObject(hbrHover);
                    DeleteObject(hPenHover);
                }

                int leftTextOffset = rcItemDraw.left + 10;

                if (pState->isMultiSelect)
                {
                    // Checkbox
                    RECT rcBox = { rcItemDraw.left + 8, rcItem.top + (pState->itemHeight - 16) / 2, rcItemDraw.left + 24, rcItem.top + (pState->itemHeight - 16) / 2 + 16 };
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

                    // Optional Header star/icon for selectable headers (id > 0)
                    if (it.isHeader && it.id > 0)
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
                        RECT rcIcon = { rcItemDraw.left + 8, rcItem.top, rcItemDraw.left + 26, rcItem.bottom };
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
                    int candidateVis = virtualY / pState->itemHeight;
                    auto visibleIndices = GetVisibleIndices(*pState);
                    if (candidateVis >= 0 && candidateVis < (int)visibleIndices.size())
                    {
                        int realIdx = visibleIndices[candidateVis];
                        const auto& item = pState->items[realIdx];
                        if (!item.isSeparator && !(item.isHeader && item.id <= 0 && !item.isGroupHeader) && item.isEnabled)
                        {
                            hitIdx = realIdx;
                        }
                    }
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
                int candidateVis = virtualY / pState->itemHeight;
                auto visibleIndices = GetVisibleIndices(*pState);
                if (candidateVis >= 0 && candidateVis < (int)visibleIndices.size())
                {
                    int hitIdx = visibleIndices[candidateVis];
                    auto& it = pState->items[hitIdx];
                    if (it.isSeparator || (it.isHeader && it.id <= 0 && !it.isGroupHeader))
                    {
                        return 0;
                    }

                    if (it.isGroupHeader)
                    {
                        // Check if multi-select and clicked checkbox
                        bool clickedBox = false;
                        if (pState->isMultiSelect && it.id > 0)
                        {
                            int leftIndent = (it.indentLevel == 1) ? 14 : ((it.indentLevel >= 2) ? 28 : 0);
                            int boxL = 6 + leftIndent + 8 + 20;
                            int boxR = boxL + 16;
                            if (x >= boxL - 3 && x <= boxR + 4)
                            {
                                clickedBox = true;
                            }
                        }

                        if (clickedBox)
                        {
                            bool nextAll = !it.isChecked;
                            it.isChecked = nextAll;
                            it.isIndeterminate = false;
                            for (auto& item : pState->items)
                            {
                                if (!item.isGroupHeader && !item.isHeader && !item.isSeparator && item.isEnabled)
                                {
                                    if (item.group == it.group || item.parentGroup == it.group)
                                    {
                                        item.isChecked = nextAll;
                                        item.isIndeterminate = false;
                                    }
                                }
                            }

                            SyncHeaderCheckStates(pState);

                            if (pState->onItemToggled) pState->onItemToggled(pState->items);
                            InvalidateRect(hWnd, NULL, FALSE);
                            if (pState->hParent && IsWindow(pState->hParent)) InvalidateRect(pState->hParent, NULL, TRUE);
                            return 0;
                        }
                        else
                        {
                            // Toggle Expand / Collapse
                            if (pState->collapsedGroups.count(it.group) > 0)
                            {
                                pState->collapsedGroups.erase(it.group);
                            }
                            else
                            {
                                pState->collapsedGroups.insert(it.group);
                            }
                            RecalculateLayout(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                            return 0;
                        }
                    }

                    if (it.isEnabled)
                    {
                        if (pState->isMultiSelect)
                        {
                            if (it.isHeader && it.id > 0)
                            {
                                // Header toggles all actionable items
                                bool nextAll = !it.isChecked;
                                for (auto& item : pState->items)
                                {
                                    if (!item.isSeparator && !(item.isHeader && item.id <= 0) && item.isEnabled)
                                    {
                                        item.isChecked = nextAll;
                                        item.isIndeterminate = false;
                                    }
                                }
                            }
                            else
                            {
                                it.isChecked = !it.isChecked;
                            }

                            SyncHeaderCheckStates(pState);

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

    for (const auto& it : state.items)
    {
        if (it.isGroupHeader && it.isCollapsed)
        {
            state.collapsedGroups.insert(it.group);
        }
    }

    int anchorW = rcScreen.right - rcScreen.left;
    int calculatedW = 48 + maxTextW + (maxSecW > 0 ? (maxSecW + 24) : 0) + 24;
    int popupW = (std::max)({ anchorW, minWidth, calculatedW });

    auto visibleIndices = GetVisibleIndices(state);
    int totalItems = (int)visibleIndices.size();
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

    for (const auto& it : state.items)
    {
        if (it.isGroupHeader && it.isCollapsed)
        {
            state.collapsedGroups.insert(it.group);
        }
    }

    int anchorW = rcScreen.right - rcScreen.left;
    int calculatedW = 56 + maxTextW + (maxSecW > 0 ? (maxSecW + 24) : 0) + 24;
    int popupW = (std::max)({ anchorW, minWidth, calculatedW });

    auto visibleIndices = GetVisibleIndices(state);
    int totalItems = (int)visibleIndices.size();
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
