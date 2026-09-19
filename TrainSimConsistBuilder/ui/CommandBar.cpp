#include "CommandBar.h"
#include <uxtheme.h>
#include <vsstyle.h>

struct CommandItem
{
    int id;
    const wchar_t* iconGlyph;
    const wchar_t* label;
    BOOL hasChevron;
    BOOL isSeparator;
    BOOL isRightAligned;
    int fixedWidth;
    RECT rc;
};

struct CommandBarState
{
    BOOL bDarkMode = TRUE;
    HFONT hFontMain = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontIconsSmall = NULL;
    int hoverIndex = -1;
    BOOL hoverInDropdown = FALSE;
    int pressedIndex = -1;
    BOOL pressedInDropdown = FALSE;
    BOOL bTrackingMouse = FALSE;
};

static CommandBarState g_State;

static HFONT CreateMdl2IconFont(float pointSize)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        hFont = CreateFontIndirectW(&lf);
    }
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}

static HFONT CreateSystemUiFont(int pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        NONCLIENTMETRICS ncm = { 0 };
        ncm.cbSize = sizeof(NONCLIENTMETRICS);
        if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
        {
            ncm.lfMessageFont.lfHeight = lf.lfHeight;
            ncm.lfMessageFont.lfWeight = weight;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        }
    }
    return hFont;
}

static CommandItem g_Items[] = {
    { CMD_ACTION_NEW_CONSIST,        L"\xE710", L"New Consist",        FALSE, FALSE, FALSE, 100, {0} },
    { CMD_ACTION_CLONE_CONSIST,      L"\xE8C8", L"Clone Consist",      FALSE, FALSE, FALSE, 105, {0} },
    { CMD_ACTION_SAVE_CONSISTS,      L"\xE74E", L"Save Consist(s)",    TRUE,  FALSE, FALSE, 130, {0} },
    { CMD_ACTION_REVERSE_CONSIST,    L"\xE8EC", L"Reverse Consist",    FALSE, FALSE, FALSE, 115, {0} },
    { CMD_ACTION_DELETE_CONSISTS,    L"\xE74D", L"Delete Consist(s)",   FALSE, FALSE, FALSE, 120, {0} },
    { CMD_ACTION_POOL_MANAGER,       L"\xE71D", L"Pool Manager",       FALSE, FALSE, TRUE,  105, {0} },
    { CMD_ACTION_POOL_MUTATOR,       L"\xE790", L"Pool Mutator",       FALSE, FALSE, TRUE,  105, {0} },
    { CMD_ACTION_BATCH_WIZARD,       L"\xE9D9", L"Batch Consists",     FALSE, FALSE, TRUE,  105, {0} },
    { CMD_ACTION_REFRESH_CONSISTS,   L"\xE72C", L"Refresh Consists",   FALSE, FALSE, TRUE,  120, {0} },
    { CMD_ACTION_REFRESH_STOCKS,     L"\xE777", L"Refresh Stock Library", FALSE, FALSE, TRUE, 145, {0} }
};

static const int g_NumItems = sizeof(g_Items) / sizeof(g_Items[0]);

static void RecalculateItemLayout(int barWidth, int barHeight, HDC hdc = NULL)
{
    bool releaseDC = false;
    if (!hdc)
    {
        hdc = GetDC(NULL);
        releaseDC = true;
    }

    HFONT hOldFont = NULL;
    if (g_State.hFontMain)
    {
        hOldFont = (HFONT)SelectObject(hdc, g_State.hFontMain);
    }

    // Auto-calculate exact compact fixedWidth for each item with 4-6px side padding
    for (int i = 0; i < g_NumItems; ++i)
    {
        SIZE sz = { 0 };
        if (g_Items[i].label && wcslen(g_Items[i].label) > 0)
        {
            GetTextExtentPoint32W(hdc, g_Items[i].label, (int)wcslen(g_Items[i].label), &sz);
        }
        int iconW = (g_Items[i].iconGlyph && wcslen(g_Items[i].iconGlyph) > 0) ? 16 : 0;
        int gap = (iconW > 0 && sz.cx > 0) ? 5 : 0;
        int pad = 6; // Compact 6px padding on each side for a proper button
        int chevronW = g_Items[i].hasChevron ? 20 : 0;
        int minW = pad + iconW + gap + sz.cx + pad + chevronW;
        g_Items[i].fixedWidth = minW;
    }

    if (hOldFont)
    {
        SelectObject(hdc, hOldFont);
    }
    if (releaseDC)
    {
        ReleaseDC(NULL, hdc);
    }

    int currentLeftX = 0;
    int currentRightX = barWidth;
    int topY = 0;
    int itemHeight = barHeight;

    // Layout left-aligned items
    for (int i = 0; i < g_NumItems; ++i)
    {
        if (!g_Items[i].isRightAligned)
        {
            g_Items[i].rc.left = currentLeftX;
            g_Items[i].rc.right = currentLeftX + g_Items[i].fixedWidth;
            g_Items[i].rc.top = topY;
            g_Items[i].rc.bottom = topY + itemHeight;

            currentLeftX += g_Items[i].fixedWidth;
        }
    }

    // Layout right-aligned items from right edge inward (in reverse order)
    for (int i = g_NumItems - 1; i >= 0; --i)
    {
        if (g_Items[i].isRightAligned)
        {
            g_Items[i].rc.right = currentRightX;
            g_Items[i].rc.left = currentRightX - g_Items[i].fixedWidth;
            g_Items[i].rc.top = topY;
            g_Items[i].rc.bottom = topY + itemHeight;

            currentRightX -= g_Items[i].fixedWidth;
        }
    }
}

static int HitTestCommandItem(int x, int y)
{
    POINT pt = { x, y };
    for (int i = 0; i < g_NumItems; ++i)
    {
        if (PtInRect(&g_Items[i].rc, pt))
        {
            return i;
        }
    }
    return -1;
}

static LRESULT CALLBACK CommandBarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        g_State.hFontMain = CreateSystemUiFont(10, FW_NORMAL);
        g_State.hFontIcons = CreateMdl2IconFont(10.0f);
        g_State.hFontIconsSmall = CreateMdl2IconFont(7.5f);

        RECT rc;
        GetClientRect(hWnd, &rc);
        HDC hdc = GetDC(hWnd);
        RecalculateItemLayout(rc.right - rc.left, rc.bottom - rc.top, hdc);
        ReleaseDC(hWnd, hdc);
    }
    break;

    case WM_MOUSEMOVE:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        if (!g_State.bTrackingMouse)
        {
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            g_State.bTrackingMouse = TRUE;
        }

        int hit = HitTestCommandItem(x, y);
        BOOL inDropdown = FALSE;
        if (hit != -1 && g_Items[hit].hasChevron)
        {
            if (x >= g_Items[hit].rc.right - 20)
            {
                inDropdown = TRUE;
            }
        }

        if (hit != g_State.hoverIndex || inDropdown != g_State.hoverInDropdown)
        {
            g_State.hoverIndex = hit;
            g_State.hoverInDropdown = inDropdown;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_MOUSELEAVE:
    {
        g_State.bTrackingMouse = FALSE;
        if (g_State.hoverIndex != -1 || g_State.pressedIndex != -1)
        {
            g_State.hoverIndex = -1;
            g_State.hoverInDropdown = FALSE;
            g_State.pressedIndex = -1;
            g_State.pressedInDropdown = FALSE;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        int hit = HitTestCommandItem(x, y);
        if (hit != -1)
        {
            g_State.pressedIndex = hit;
            g_State.pressedInDropdown = FALSE;
            if (g_Items[hit].hasChevron && x >= g_Items[hit].rc.right - 20)
            {
                g_State.pressedInDropdown = TRUE;
            }
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }

        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        int hit = HitTestCommandItem(x, y);
        int clicked = g_State.pressedIndex;
        BOOL wasInDropdown = g_State.pressedInDropdown;
        g_State.pressedIndex = -1;
        g_State.pressedInDropdown = FALSE;

        InvalidateRect(hWnd, NULL, FALSE);

        if (clicked != -1 && clicked == hit)
        {
            HWND hMainWnd = GetParent(hWnd);
            SendMessage(hMainWnd, WM_COMMANDBAR_ACTION, g_Items[clicked].id, wasInDropdown ? 1 : 0);
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Double buffer DC
        HDC hmemDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP holdBm = (HBITMAP)SelectObject(hmemDC, hbm);

        // 1. Fill background (matches header background)
        COLORREF bgBar = RGB(32, 32, 32);
        COLORREF lineCol = RGB(48, 48, 48);
        HBRUSH hbrBg = CreateSolidBrush(bgBar);
        FillRect(hmemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        // 2. Draw Command Items
        for (int i = 0; i < g_NumItems; ++i)
        {
            RECT rcCell = g_Items[i].rc;

            RECT rcLeftHighlight = rcCell;
            RECT rcRightHighlight = rcCell;
            if (g_Items[i].hasChevron)
            {
                rcLeftHighlight.right -= 20;
                rcRightHighlight.left = rcLeftHighlight.right;
            }

            COLORREF leftBgColor = CLR_INVALID;
            COLORREF rightBgColor = CLR_INVALID;

            if (i == g_State.pressedIndex)
            {
                if (g_Items[i].hasChevron)
                {
                    if (g_State.pressedInDropdown)
                    {
                        leftBgColor = RGB(45, 45, 45);
                        rightBgColor = RGB(60, 60, 60);
                    }
                    else
                    {
                        leftBgColor = RGB(55, 55, 55);
                        rightBgColor = RGB(55, 55, 55);
                    }
                }
                else
                {
                    leftBgColor = RGB(55, 55, 55);
                }
            }
            else if (i == g_State.hoverIndex)
            {
                if (g_Items[i].hasChevron)
                {
                    leftBgColor = RGB(45, 45, 45);
                    if (g_State.hoverInDropdown)
                    {
                        rightBgColor = RGB(55, 55, 55);
                    }
                    else
                    {
                        rightBgColor = RGB(45, 45, 45);
                    }
                }
                else
                {
                    leftBgColor = RGB(45, 45, 45);
                }
            }

            if (leftBgColor != CLR_INVALID)
            {
                HBRUSH hbrLeft = CreateSolidBrush(leftBgColor);
                FillRect(hmemDC, &rcLeftHighlight, hbrLeft);
                DeleteObject(hbrLeft);
            }
            if (g_Items[i].hasChevron && rightBgColor != CLR_INVALID)
            {
                HBRUSH hbrRight = CreateSolidBrush(rightBgColor);
                FillRect(hmemDC, &rcRightHighlight, hbrRight);
                DeleteObject(hbrRight);
            }

            // Divider between Left and Right zones for split button (full vertical height)
            if (g_Items[i].hasChevron)
            {
                HPEN hDivPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
                HPEN hOldDivPen = (HPEN)SelectObject(hmemDC, hDivPen);
                MoveToEx(hmemDC, rcRightHighlight.left, 0, NULL);
                LineTo(hmemDC, rcRightHighlight.left, rcClient.bottom);
                SelectObject(hmemDC, hOldDivPen);
                DeleteObject(hDivPen);

                int arrowX = (rcRightHighlight.left + rcRightHighlight.right) / 2;
                int arrowY = (rcCell.top + rcCell.bottom) / 2;

                COLORREF arrowColor = (i == g_State.hoverIndex || i == g_State.pressedIndex) ?
                    RGB(220, 220, 220) : RGB(160, 160, 160);

                HPEN hArrowPen = CreatePen(PS_SOLID, 1, arrowColor);
                HPEN hOldArrowPen = (HPEN)SelectObject(hmemDC, hArrowPen);
                HBRUSH hArrowBrush = CreateSolidBrush(arrowColor);
                HBRUSH hOldArrowBrush = (HBRUSH)SelectObject(hmemDC, hArrowBrush);

                POINT pts[3];
                pts[0] = { arrowX, arrowY + 2 };
                pts[1] = { arrowX - 3, arrowY - 2 };
                pts[2] = { arrowX + 3, arrowY - 2 };
                Polygon(hmemDC, pts, 3);

                SelectObject(hmemDC, hOldArrowPen);
                DeleteObject(hArrowPen);
                SelectObject(hmemDC, hOldArrowBrush);
                DeleteObject(hArrowBrush);
            }

            // Vertical separator line between cells (full vertical height)
            HPEN hCellSep = CreatePen(PS_SOLID, 1, lineCol);
            HPEN hOldCellSep = (HPEN)SelectObject(hmemDC, hCellSep);
            if (g_Items[i].isRightAligned)
            {
                // Left border for right-aligned items
                MoveToEx(hmemDC, rcCell.left, 0, NULL);
                LineTo(hmemDC, rcCell.left, rcClient.bottom);
            }
            else
            {
                // Right border for left-aligned items
                MoveToEx(hmemDC, rcCell.right - 1, 0, NULL);
                LineTo(hmemDC, rcCell.right - 1, rcClient.bottom);
            }
            SelectObject(hmemDC, hOldCellSep);
            DeleteObject(hCellSep);

            COLORREF textColor = (i == g_State.hoverIndex || i == g_State.pressedIndex) ?
                RGB(255, 255, 255) : RGB(200, 200, 200);

            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, textColor);

            // Measure text for proper centering
            SIZE szText = { 0 };
            if (g_Items[i].label && wcslen(g_Items[i].label) > 0)
            {
                SelectObject(hmemDC, g_State.hFontMain ? g_State.hFontMain : GetStockObject(DEFAULT_GUI_FONT));
                GetTextExtentPoint32W(hmemDC, g_Items[i].label, (int)wcslen(g_Items[i].label), &szText);
            }

            int iconW = (g_Items[i].iconGlyph && wcslen(g_Items[i].iconGlyph) > 0) ? 16 : 0;
            int gap = (iconW > 0 && szText.cx > 0) ? 5 : 0;
            int totalContentW = iconW + gap + szText.cx;

            RECT rcTarget = rcLeftHighlight;
            int startX = rcTarget.left + (rcTarget.right - rcTarget.left - totalContentW) / 2;
            if (startX < rcTarget.left + 2) startX = rcTarget.left + 2;

            // Draw Icon
            if (iconW > 0)
            {
                RECT rcIcon = { startX, rcTarget.top, startX + iconW, rcTarget.bottom };
                HFONT hIconFontToUse = g_State.hFontIcons;
                if (g_Items[i].id == CMD_ACTION_NEW_CONSIST)
                {
                    hIconFontToUse = g_State.hFontIconsSmall;
                }

                SelectObject(hmemDC, hIconFontToUse ? hIconFontToUse : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hmemDC, g_Items[i].iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            // Draw Text Label
            if (szText.cx > 0)
            {
                RECT rcText = { startX + iconW + gap, rcTarget.top, rcTarget.right - 2, rcTarget.bottom };
                SelectObject(hmemDC, g_State.hFontMain ? g_State.hFontMain : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hmemDC, g_Items[i].label, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        // 3. Draw bottom separator line
        HPEN hPenLine = CreatePen(PS_SOLID, 1, lineCol);
        HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenLine);

        MoveToEx(hmemDC, 0, rcClient.bottom - 1, NULL);
        LineTo(hmemDC, rcClient.right, rcClient.bottom - 1);

        SelectObject(hmemDC, hOldPen);
        DeleteObject(hPenLine);

        // 1px App-Drawn Perimeter Border (Left & Right) when windowed
        HWND hMainWnd = GetParent(hWnd);
        if (!IsZoomed(hMainWnd))
        {
            COLORREF clrBorder = RGB(78, 32, 38);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrBorder);
            HPEN hOldBorder = (HPEN)SelectObject(hmemDC, hPenBorder);

            MoveToEx(hmemDC, 0, 0, NULL);
            LineTo(hmemDC, 0, rcClient.bottom);
            MoveToEx(hmemDC, rcClient.right - 1, 0, NULL);
            LineTo(hmemDC, rcClient.right - 1, rcClient.bottom);

            SelectObject(hmemDC, hOldBorder);
            DeleteObject(hPenBorder);
        }

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hmemDC, 0, 0, SRCCOPY);

        SelectObject(hmemDC, holdBm);
        DeleteObject(hbm);
        DeleteDC(hmemDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    break;

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        RecalculateItemLayout(width, height);
        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;

    case WM_DESTROY:
        if (g_State.hFontMain) DeleteObject(g_State.hFontMain);
        if (g_State.hFontIcons) DeleteObject(g_State.hFontIcons);
        if (g_State.hFontIconsSmall) DeleteObject(g_State.hFontIconsSmall);
        break;

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL RegisterCommandBarClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = CommandBarProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"CommandBarClass";

    return RegisterClassExW(&wcex) != 0;
}

HWND CreateCommandBar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId)
{
    RegisterCommandBarClass(hInstance);

    HWND hBar = CreateWindowExW(
        0, L"CommandBarClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, (HMENU)controlId, hInstance, NULL
    );

    return hBar;
}

void CommandBar_SetDarkMode(HWND hCommandBar, BOOL bDarkMode)
{
    g_State.bDarkMode = bDarkMode;
    InvalidateRect(hCommandBar, NULL, TRUE);
}

void CommandBar_SetButtonText(HWND hCommandBar, int actionId, const wchar_t* newLabel, int newWidth)
{
    for (int i = 0; i < g_NumItems; ++i)
    {
        if (g_Items[i].id == actionId)
        {
            g_Items[i].label = newLabel;
            if (newWidth > 0)
            {
                g_Items[i].fixedWidth = newWidth;
            }
            if (hCommandBar && IsWindow(hCommandBar))
            {
                RECT rc;
                GetClientRect(hCommandBar, &rc);
                RecalculateItemLayout(rc.right - rc.left, rc.bottom - rc.top);
                InvalidateRect(hCommandBar, NULL, TRUE);
            }
            break;
        }
    }
}

