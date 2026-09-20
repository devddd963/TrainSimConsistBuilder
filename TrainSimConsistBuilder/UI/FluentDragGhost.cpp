#include "FluentDragGhost.h"
#include <algorithm>

HWND FluentDragGhost::s_hWnd = NULL;
HINSTANCE FluentDragGhost::s_hInstance = NULL;
std::vector<DragGhostItem> FluentDragGhost::s_items;
bool FluentDragGhost::s_isValidTarget = false;
std::wstring FluentDragGhost::s_actionText = L"";
HFONT FluentDragGhost::s_hFontText = NULL;
HFONT FluentDragGhost::s_hFontBadge = NULL;
HFONT FluentDragGhost::s_hFontIcon = NULL;

LRESULT CALLBACK FluentDragGhost::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void FluentDragGhost::Initialize(HINSTANCE hInstance)
{
    s_hInstance = hInstance;

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"FluentDragGhostWindow";
    RegisterClassExW(&wcex);

    LOGFONTW lf = { 0 };
    lf.lfHeight = -13; // 10pt
    lf.lfWeight = FW_MEDIUM;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    s_hFontText = CreateFontIndirectW(&lf);

    lf.lfHeight = -12; // 9pt
    lf.lfWeight = FW_SEMIBOLD;
    s_hFontBadge = CreateFontIndirectW(&lf);

    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
    s_hFontIcon = CreateFontIndirectW(&lf);
}

bool FluentDragGhost::IsActive()
{
    return (s_hWnd != NULL && IsWindow(s_hWnd) && IsWindowVisible(s_hWnd));
}

void FluentDragGhost::Show(HWND hParent, POINT ptScreen, const std::vector<DragGhostItem>& items)
{
    if (items.empty()) return;

    if (!s_hWnd || !IsWindow(s_hWnd))
    {
        if (!s_hInstance) s_hInstance = GetModuleHandle(NULL);
        Initialize(s_hInstance);

        s_hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            L"FluentDragGhostWindow", L"",
            WS_POPUP | WS_DISABLED,
            ptScreen.x + 14, ptScreen.y + 14, 300, 150,
            hParent, NULL, s_hInstance, NULL
        );
    }

    s_items = items;
    s_isValidTarget = false;
    s_actionText = L"";

    RenderGhost();
    SetWindowPos(s_hWnd, HWND_TOPMOST, ptScreen.x + 14, ptScreen.y + 14, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void FluentDragGhost::Move(POINT ptScreen, bool isValidDropTarget, const std::wstring& actionText)
{
    if (!s_hWnd || !IsWindow(s_hWnd)) return;

    bool needsRerender = (s_isValidTarget != isValidDropTarget || s_actionText != actionText);
    s_isValidTarget = isValidDropTarget;
    s_actionText = actionText;

    if (needsRerender)
    {
        RenderGhost();
    }

    SetWindowPos(s_hWnd, HWND_TOPMOST, ptScreen.x + 14, ptScreen.y + 14, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void FluentDragGhost::Hide()
{
    if (s_hWnd && IsWindow(s_hWnd))
    {
        ShowWindow(s_hWnd, SW_HIDE);
    }
}

void FluentDragGhost::RenderGhost()
{
    if (!s_hWnd || s_items.empty()) return;

    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);

    // Handle PoolCard ghost type
    if (s_items[0].type == GhostItemType::PoolCard)
    {
        int cardH = 34;
        int totalH = cardH + (s_isValidTarget ? 28 : 0);

        HFONT hOldFont = (HFONT)SelectObject(hMemDC, s_hFontText ? s_hFontText : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SIZE szTitle, szSub;
        GetTextExtentPoint32W(hMemDC, s_items[0].name.c_str(), (int)s_items[0].name.length(), &szTitle);
        SelectObject(hMemDC, s_hFontBadge);
        GetTextExtentPoint32W(hMemDC, s_items[0].subtitle.c_str(), (int)s_items[0].subtitle.length(), &szSub);
        SelectObject(hMemDC, hOldFont);

        int totalW = (std::max)(szTitle.cx + szSub.cx + 70, (LONG)220);
        if (totalW > 380) totalW = 380;

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = totalW;
        bmi.bmiHeader.biHeight = -totalH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        RGBQUAD* pPixels = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);

        memset(pPixels, 0, totalW * totalH * sizeof(RGBQUAD));

        RECT rcCard = { 0, 0, totalW - 10, cardH };
        HBRUSH hbrCard = CreateSolidBrush(RGB(20, 36, 52));
        HPEN hPenCard = CreatePen(PS_SOLID, 1, RGB(0, 140, 220));
        HGDIOBJ oldB = SelectObject(hMemDC, hbrCard);
        HGDIOBJ oldP = SelectObject(hMemDC, hPenCard);
        RoundRect(hMemDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
        SelectObject(hMemDC, oldB);
        SelectObject(hMemDC, oldP);
        DeleteObject(hbrCard);
        DeleteObject(hPenCard);

        // Icon
        RECT rcIcon = { rcCard.left + 6, rcCard.top, rcCard.left + 26, rcCard.bottom };
        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(0, 180, 255));
        SelectObject(hMemDC, s_hFontIcon ? s_hFontIcon : s_hFontText);
        DrawTextW(hMemDC, L"\xE8F1", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Pool Title
        RECT rcTitle = { rcCard.left + 28, rcCard.top, rcCard.left + 28 + szTitle.cx + 8, rcCard.bottom };
        SetTextColor(hMemDC, RGB(255, 255, 255));
        SelectObject(hMemDC, s_hFontText);
        DrawTextW(hMemDC, s_items[0].name.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Subtitle
        if (!s_items[0].subtitle.empty())
        {
            RECT rcSub = { rcTitle.right, rcCard.top, rcCard.right - 8, rcCard.bottom };
            SetTextColor(hMemDC, RGB(160, 200, 225));
            SelectObject(hMemDC, s_hFontBadge);
            DrawTextW(hMemDC, s_items[0].subtitle.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        // Action badge
        if (s_isValidTarget)
        {
            std::wstring badgeText = s_actionText.empty() ? L"Reorder Pool" : s_actionText;
            RECT rcBadge = { 20, cardH + 2, totalW - 4, cardH + 24 };

            HBRUSH hbrBadge = CreateSolidBrush(RGB(255, 255, 255));
            HPEN hPenBadge = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
            HGDIOBJ bOld = SelectObject(hMemDC, hbrBadge);
            HGDIOBJ pOld = SelectObject(hMemDC, hPenBadge);
            RoundRect(hMemDC, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 6, 6);
            SelectObject(hMemDC, bOld);
            SelectObject(hMemDC, pOld);
            DeleteObject(hbrBadge);
            DeleteObject(hPenBadge);

            SetBkMode(hMemDC, TRANSPARENT);
            SetTextColor(hMemDC, RGB(0, 80, 180));
            SelectObject(hMemDC, s_hFontBadge);
            DrawTextW(hMemDC, badgeText.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Alpha channel
        for (int y = 0; y < totalH; ++y)
        {
            for (int x = 0; x < totalW; ++x)
            {
                RGBQUAD& pixel = pPixels[y * totalW + x];
                if (pixel.rgbRed != 0 || pixel.rgbGreen != 0 || pixel.rgbBlue != 0)
                {
                    pixel.rgbReserved = 220;
                    pixel.rgbRed   = (BYTE)((pixel.rgbRed   * 220) / 255);
                    pixel.rgbGreen = (BYTE)((pixel.rgbGreen * 220) / 255);
                    pixel.rgbBlue  = (BYTE)((pixel.rgbBlue  * 220) / 255);
                }
            }
        }

        POINT ptSrc = { 0, 0 };
        SIZE szWnd = { totalW, totalH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(s_hWnd, hScreenDC, NULL, &szWnd, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hMemDC, hOldBmp);
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    // Standard Vehicle Unit rendering
    size_t displayCount = (std::min)(s_items.size(), (size_t)3);
    int itemH = 24;
    int gap = 3;
    int totalH = (int)displayCount * (itemH + gap) + (s_items.size() > 3 ? (itemH + gap) : 0);

    if (s_isValidTarget)
    {
        totalH += 28; // Space for action badge pill
    }

    int maxTextW = 100;
    HFONT hOldFont = (HFONT)SelectObject(hMemDC, s_hFontText ? s_hFontText : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

    for (size_t i = 0; i < displayCount; ++i)
    {
        SIZE sz;
        GetTextExtentPoint32W(hMemDC, s_items[i].name.c_str(), (int)s_items[i].name.length(), &sz);
        if (sz.cx > maxTextW) maxTextW = sz.cx;
    }
    SelectObject(hMemDC, hOldFont);

    int totalW = maxTextW + 48; // padding + icon
    if (totalW < 160) totalW = 160;
    if (totalW > 350) totalW = 350;

    // Create 32-bit ARGB DIBSection for smooth alpha blending
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = totalW;
    bmi.bmiHeader.biHeight = -totalH; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    RGBQUAD* pPixels = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // Clear background to full transparent black (Alpha 0)
    memset(pPixels, 0, totalW * totalH * sizeof(RGBQUAD));

    int curY = 0;

    // Draw stacked Fluent-style translucent capsules
    for (size_t i = 0; i < displayCount; ++i)
    {
        RECT rcPill = { 0, curY, totalW - 10, curY + itemH };

        // 1. Draw translucent capsule background with rounded rectangle
        HBRUSH hbrCap = CreateSolidBrush(RGB(18, 55, 68)); // Dark Teal
        HPEN hPenCap = CreatePen(PS_SOLID, 1, RGB(40, 110, 130));

        HGDIOBJ oldB = SelectObject(hMemDC, hbrCap);
        HGDIOBJ oldP = SelectObject(hMemDC, hPenCap);
        RoundRect(hMemDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);
        SelectObject(hMemDC, oldB);
        SelectObject(hMemDC, oldP);
        DeleteObject(hbrCap);
        DeleteObject(hPenCap);

        // 2. Draw vehicle icon (Locomotive / Carriage)
        RECT rcIcon = { rcPill.left + 6, rcPill.top, rcPill.left + 24, rcPill.bottom };
        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(245, 180, 50)); // Warm gold icon
        SelectObject(hMemDC, s_hFontText);
        DrawTextW(hMemDC, s_items[i].isEngine ? L"\xD83D\xDE82" : L"\xD83D\xDE83", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3. Draw vehicle name
        RECT rcText = { rcPill.left + 26, rcPill.top, rcPill.right - 6, rcPill.bottom };
        SetTextColor(hMemDC, RGB(225, 240, 245));
        SelectObject(hMemDC, s_hFontText);
        DrawTextW(hMemDC, s_items[i].name.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        curY += itemH + gap;
    }

    if (s_items.size() > 3)
    {
        RECT rcMore = { 0, curY, totalW - 10, curY + itemH };
        HBRUSH hbrMore = CreateSolidBrush(RGB(15, 40, 50));
        HPEN hPenMore = CreatePen(PS_SOLID, 1, RGB(30, 80, 100));
        HGDIOBJ oldB = SelectObject(hMemDC, hbrMore);
        HGDIOBJ oldP = SelectObject(hMemDC, hPenMore);
        RoundRect(hMemDC, rcMore.left, rcMore.top, rcMore.right, rcMore.bottom, 6, 6);
        SelectObject(hMemDC, oldB);
        SelectObject(hMemDC, oldP);
        DeleteObject(hbrMore);
        DeleteObject(hPenMore);

        std::wstring moreStr = L"+ " + std::to_wstring(s_items.size() - 3) + L" more units";
        RECT rcMoreText = { rcMore.left + 10, rcMore.top, rcMore.right - 6, rcMore.bottom };
        SetTextColor(hMemDC, RGB(180, 210, 220));
        SelectObject(hMemDC, s_hFontBadge);
        DrawTextW(hMemDC, moreStr.c_str(), -1, &rcMoreText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        curY += itemH + gap;
    }

    // Draw bright action badge pill when hovering over valid drop target
    if (s_isValidTarget)
    {
        std::wstring badgeText = s_actionText.empty() ?
            (L"+ Insert " + std::to_wstring(s_items.size()) + L" Unit" + (s_items.size() > 1 ? L"s" : L"") + L" to Consist") : s_actionText;

        RECT rcBadge = { 20, curY + 2, totalW - 4, curY + 24 };

        HBRUSH hbrBadge = CreateSolidBrush(RGB(255, 255, 255));
        HPEN hPenBadge = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
        HGDIOBJ oldB = SelectObject(hMemDC, hbrBadge);
        HGDIOBJ oldP = SelectObject(hMemDC, hPenBadge);
        RoundRect(hMemDC, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 6, 6);
        SelectObject(hMemDC, oldB);
        SelectObject(hMemDC, oldP);
        DeleteObject(hbrBadge);
        DeleteObject(hPenBadge);

        SetBkMode(hMemDC, TRANSPARENT);
        SetTextColor(hMemDC, RGB(0, 80, 180));
        SelectObject(hMemDC, s_hFontBadge);
        DrawTextW(hMemDC, badgeText.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Set alpha channels for non-zero pixels (~86% opacity = 220 alpha)
    for (int y = 0; y < totalH; ++y)
    {
        for (int x = 0; x < totalW; ++x)
        {
            RGBQUAD& pixel = pPixels[y * totalW + x];
            if (pixel.rgbRed != 0 || pixel.rgbGreen != 0 || pixel.rgbBlue != 0)
            {
                pixel.rgbReserved = 220; // Crisp, translucent alpha
                // Premultiply alpha
                pixel.rgbRed   = (BYTE)((pixel.rgbRed   * 220) / 255);
                pixel.rgbGreen = (BYTE)((pixel.rgbGreen * 220) / 255);
                pixel.rgbBlue  = (BYTE)((pixel.rgbBlue  * 220) / 255);
            }
        }
    }

    POINT ptSrc = { 0, 0 };
    SIZE szWnd = { totalW, totalH };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(s_hWnd, hScreenDC, NULL, &szWnd, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
}
