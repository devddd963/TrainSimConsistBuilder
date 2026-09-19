#include "ModernMessageBox.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

extern HFONT GetAdaptiveSystemFont();

struct MsgBoxButton {
    int id;
    std::wstring text;
    RECT rc;
    bool isDefault;
};

struct MsgBoxState {
    HWND hWnd;
    HWND hParent;
    HWND hTextCtrl;
    bool isScrollable;
    std::wstring text;
    std::wstring caption;
    UINT type;
    int hoverButton;
    int pressedButton;
    int focusedButton;
    int result;
    bool isClosed;
    std::vector<MsgBoxButton> buttons;
    HFONT hFontTitle;
    HFONT hFontText;
    HFONT hFontIcon;
};

static LRESULT CALLBACK ModernMsgBoxWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    MsgBoxState* pState = (MsgBoxState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        if (pState && (HWND)lParam == pState->hTextCtrl)
        {
            HDC hdc = (HDC)wParam;
            COLORREF bgCol = RGB(23, 23, 23);
            COLORREF textCol = RGB(225, 225, 225);
            SetBkColor(hdc, bgCol);
            SetTextColor(hdc, textCol);
            static HBRUSH hbrDark = CreateSolidBrush(RGB(23, 23, 23));
            return (LRESULT)hbrDark;
        }
        break;
    }

    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (MsgBoxState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_NCHITTEST:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        for (const auto& btn : pState->buttons)
        {
            RECT rcBtn = btn.rc;
            InflateRect(&rcBtn, 4, 4);
            if (PtInRect(&rcBtn, pt))
            {
                return HTCLIENT;
            }
        }
        return HTCAPTION;
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

        // 1. Background & 1px rounded border
        COLORREF bgCol = RGB(23, 23, 23);
        COLORREF borderCol = RGB(55, 55, 55);

        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hMemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hMemDC, TRANSPARENT);

        // 2. Determine Icon & Colors
        UINT iconType = pState->type & MB_ICONMASK;
        LPCWSTR iconGlyph = L"\xE946"; // Info
        COLORREF iconCol = RGB(96, 205, 255); // Fluent Sky Blue

        if (iconType == MB_ICONWARNING)
        {
            iconGlyph = L"\xE7BA"; // Warning triangle
            iconCol = RGB(252, 225, 0); // Amber Yellow
        }
        else if (iconType == MB_ICONERROR || iconType == MB_ICONHAND || iconType == MB_ICONSTOP)
        {
            iconGlyph = L"\xEA39"; // Error circle X
            iconCol = RGB(255, 120, 130); // Coral Red
        }
        else if (iconType == MB_ICONQUESTION)
        {
            iconGlyph = L"\xE9CE"; // Question
            iconCol = RGB(96, 205, 255);
        }

        // Draw Icon
        if (pState->hFontIcon)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcon);
            SetTextColor(hMemDC, iconCol);
            RECT rcIcon = { 22, 20, 48, 46 };
            DrawTextW(hMemDC, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hMemDC, hOldF);
        }

        // Draw Title
        if (pState->hFontTitle)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontTitle);
            SetTextColor(hMemDC, RGB(255, 255, 255));
            RECT rcTitle = { 54, 20, w - 24, 46 };
            DrawTextW(hMemDC, pState->caption.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hMemDC, hOldF);
        }

        // Draw Body Text (only if not using scrollable child edit control)
        if (pState->hFontText && !pState->isScrollable)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
            SetTextColor(hMemDC, RGB(225, 225, 225));
            RECT rcText = { 24, 58, w - 24, h - 64 };
            DrawTextW(hMemDC, pState->text.c_str(), -1, &rcText, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hMemDC, hOldF);
        }

        // 3. Draw Bottom Buttons
        for (size_t i = 0; i < pState->buttons.size(); ++i)
        {
            const auto& btn = pState->buttons[i];
            bool isHover = ((int)i == pState->hoverButton);
            bool isPress = ((int)i == pState->pressedButton);
            bool isFocus = ((int)i == pState->focusedButton);

            COLORREF btnBg, btnBorder, btnText;

            if (btn.isDefault)
            {
                // Accent Blue Button
                btnBg = isPress ? RGB(0, 100, 185) : (isHover ? RGB(20, 140, 235) : RGB(0, 120, 215));
                btnBorder = btnBg;
                btnText = RGB(255, 255, 255);
            }
            else
            {
                // Secondary Neutral Button
                btnBg = isPress ? RGB(36, 36, 36) : (isHover ? RGB(54, 54, 54) : RGB(42, 42, 42));
                btnBorder = isHover ? RGB(80, 80, 80) : RGB(60, 60, 60);
                btnText = RGB(255, 255, 255);
            }

            HBRUSH hbrBtn = CreateSolidBrush(btnBg);
            HPEN hPenBtn = CreatePen(PS_SOLID, 1, btnBorder);

            HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrBtn);
            HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenBtn);

            RoundRect(hMemDC, btn.rc.left, btn.rc.top, btn.rc.right, btn.rc.bottom, 8, 8);

            SelectObject(hMemDC, hOldB);
            SelectObject(hMemDC, hOldP);
            DeleteObject(hbrBtn);
            DeleteObject(hPenBtn);

            // Button Label
            if (pState->hFontText)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
                SetTextColor(hMemDC, btnText);
                RECT rcBtnText = btn.rc;
                DrawTextW(hMemDC, btn.text.c_str(), -1, &rcBtnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hMemDC, hOldF);
            }
        }

        // 4. Subtle 1px window border
        HPEN hPenWinBorder = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPenWinBorder);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hMemDC, hNullBrush);

        RoundRect(hMemDC, 0, 0, w, h, 16, 16);

        SelectObject(hMemDC, hOldBr);
        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPenWinBorder);

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

        int newHover = -1;
        for (size_t i = 0; i < pState->buttons.size(); ++i)
        {
            if (PtInRect(&pState->buttons[i].rc, { x, y }))
            {
                newHover = (int)i;
                break;
            }
        }

        if (newHover != pState->hoverButton)
        {
            pState->hoverButton = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        for (size_t i = 0; i < pState->buttons.size(); ++i)
        {
            if (PtInRect(&pState->buttons[i].rc, { x, y }))
            {
                pState->pressedButton = (int)i;
                pState->focusedButton = (int)i;
                SetCapture(hWnd);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) break;
        if (pState->pressedButton != -1)
        {
            ReleaseCapture();
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            int clicked = pState->pressedButton;
            pState->pressedButton = -1;

            if (clicked >= 0 && clicked < (int)pState->buttons.size())
            {
                if (PtInRect(&pState->buttons[clicked].rc, { x, y }))
                {
                    pState->result = pState->buttons[clicked].id;
                    pState->isClosed = true;
                    DestroyWindow(hWnd);
                    return 0;
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (!pState) break;
        if (wParam == VK_ESCAPE)
        {
            // Escape triggers Cancel, No, or OK
            int cancelId = IDCANCEL;
            bool foundCancel = false;
            for (const auto& btn : pState->buttons)
            {
                if (btn.id == IDCANCEL || btn.id == IDNO)
                {
                    cancelId = btn.id;
                    foundCancel = true;
                    break;
                }
            }
            if (!foundCancel && !pState->buttons.empty())
            {
                cancelId = pState->buttons.back().id;
            }
            pState->result = cancelId;
            pState->isClosed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        else if (wParam == VK_RETURN || wParam == VK_SPACE)
        {
            if (pState->focusedButton >= 0 && pState->focusedButton < (int)pState->buttons.size())
            {
                pState->result = pState->buttons[pState->focusedButton].id;
            }
            else if (!pState->buttons.empty())
            {
                pState->result = pState->buttons[0].id;
            }
            pState->isClosed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        else if (wParam == VK_TAB || wParam == VK_RIGHT)
        {
            if (!pState->buttons.empty())
            {
                pState->focusedButton = (pState->focusedButton + 1) % (int)pState->buttons.size();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        else if (wParam == VK_LEFT)
        {
            if (!pState->buttons.empty())
            {
                pState->focusedButton = (pState->focusedButton - 1 + (int)pState->buttons.size()) % (int)pState->buttons.size();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            pState->isClosed = true;
        }
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int ShowModernMessageBoxEx(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, const std::vector<ModernMsgBoxCustomButton>& customButtons, UINT uType)
{
    static bool s_registered = false;
    HINSTANCE hInst = GetModuleHandle(NULL);

    if (!s_registered)
    {
        WNDCLASSEXW wcx = { 0 };
        wcx.cbSize = sizeof(wcx);
        wcx.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wcx.lpfnWndProc = ModernMsgBoxWndProc;
        wcx.hInstance = hInst;
        wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcx.hbrBackground = NULL;
        wcx.lpszClassName = L"ModernMsgBoxWindow";
        RegisterClassExW(&wcx);
        s_registered = true;
    }

    MsgBoxState state;
    state.hWnd = NULL;
    state.hParent = hWndParent;
    state.text = lpText ? lpText : L"";
    state.caption = lpCaption ? lpCaption : L"Message";
    state.type = uType;
    state.hoverButton = -1;
    state.pressedButton = -1;
    state.focusedButton = 0;
    state.result = IDOK;
    state.isClosed = false;

    // Create Fonts
    state.hFontTitle = CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    state.hFontText = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    state.hFontIcon = CreateFontW(
        -22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
    if (!state.hFontIcon)
    {
        state.hFontIcon = CreateFontW(
            -22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

    // Measure Text & Calculate Dimensions
    int dlgWidth = 663;
    HDC hdcScreen = GetDC(NULL);
    HFONT hOldF = (HFONT)SelectObject(hdcScreen, state.hFontText);
    RECT rcCalc = { 0, 0, dlgWidth - 48, 0 };
    DrawTextW(hdcScreen, state.text.c_str(), -1, &rcCalc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdcScreen, hOldF);
    ReleaseDC(NULL, hdcScreen);

    int textHeight = rcCalc.bottom - rcCalc.top;
    if (textHeight < 30) textHeight = 30;

    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxDlgH = (std::min)(540, (int)((rcWork.bottom - rcWork.top) * 0.80));

    int dlgHeight = 60 + textHeight + 68; // Title + Text + Button Bar
    state.isScrollable = false;
    if (dlgHeight > maxDlgH)
    {
        dlgHeight = maxDlgH;
        state.isScrollable = true;
    }
    if (dlgHeight < 160) dlgHeight = 160;

    // Build Buttons
    int btnH = 32;
    int btnY = dlgHeight - 46;

    if (!customButtons.empty())
    {
        int curRight = dlgWidth - 24;
        for (auto it = customButtons.rbegin(); it != customButtons.rend(); ++it)
        {
            int btnW = 90;
            HDC hdcB = GetDC(NULL);
            SIZE szB;
            HFONT hOld = (HFONT)SelectObject(hdcB, state.hFontText);
            GetTextExtentPoint32W(hdcB, it->text.c_str(), (int)it->text.length(), &szB);
            SelectObject(hdcB, hOld);
            ReleaseDC(NULL, hdcB);
            if (szB.cx + 28 > btnW) btnW = szB.cx + 28;

            int x = curRight - btnW;
            state.buttons.insert(state.buttons.begin(), { it->id, it->text, { x, btnY, x + btnW, btnY + btnH }, it->isDefault });
            curRight = x - 10;
        }
    }
    else
    {
        UINT btnType = uType & MB_TYPEMASK;
        int btnW = 90;

        if (btnType == MB_YESNO)
        {
            int x2 = dlgWidth - 24 - btnW;
            int x1 = x2 - 12 - btnW;
            state.buttons.push_back({ IDYES, L"Yes", { x1, btnY, x1 + btnW, btnY + btnH }, true });
            state.buttons.push_back({ IDNO,  L"No",  { x2, btnY, x2 + btnW, btnY + btnH }, false });
        }
        else if (btnType == MB_YESNOCANCEL)
        {
            int x3 = dlgWidth - 24 - btnW;
            int x2 = x3 - 10 - btnW;
            int x1 = x2 - 10 - btnW;
            state.buttons.push_back({ IDYES,    L"Yes",    { x1, btnY, x1 + btnW, btnY + btnH }, true });
            state.buttons.push_back({ IDNO,     L"No",     { x2, btnY, x2 + btnW, btnY + btnH }, false });
            state.buttons.push_back({ IDCANCEL, L"Cancel", { x3, btnY, x3 + btnW, btnY + btnH }, false });
        }
        else if (btnType == MB_OKCANCEL)
        {
            int x2 = dlgWidth - 24 - btnW;
            int x1 = x2 - 12 - btnW;
            state.buttons.push_back({ IDOK,     L"OK",     { x1, btnY, x1 + btnW, btnY + btnH }, true });
            state.buttons.push_back({ IDCANCEL, L"Cancel", { x2, btnY, x2 + btnW, btnY + btnH }, false });
        }
        else // MB_OK
        {
            int x = dlgWidth - 24 - btnW;
            state.buttons.push_back({ IDOK, L"OK", { x, btnY, x + btnW, btnY + btnH }, true });
        }
    }

    // Position Window Centered on Parent or Screen
    RECT rcParent;
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int posX = rcParent.left + (rcParent.right - rcParent.left - dlgWidth) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgHeight) / 2;

    HWND hMsgBox = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ModernMsgBoxWindow", state.caption.c_str(),
        WS_POPUP,
        posX, posY, dlgWidth, dlgHeight,
        hWndParent, NULL, hInst, &state
    );

    if (!hMsgBox)
    {
        if (state.hFontTitle) DeleteObject(state.hFontTitle);
        if (state.hFontText) DeleteObject(state.hFontText);
        if (state.hFontIcon) DeleteObject(state.hFontIcon);
        return MessageBoxW(hWndParent, lpText, lpCaption, uType);
    }

    // DWM Rounded Corners & Dark Mode
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hMsgBox, (DWMWINDOWATTRIBUTE)33, &corner, sizeof(corner));
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hMsgBox, (DWMWINDOWATTRIBUTE)20, &useDark, sizeof(useDark));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = RGB(55, 55, 62);
    DwmSetWindowAttribute(hMsgBox, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hMsgBox, &margins);

    // Disable parent window for modal behavior
    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, FALSE);
    }

    if (state.isScrollable)
    {
        int textAreaH = dlgHeight - 58 - 60;
        state.hTextCtrl = CreateWindowExW(
            0, L"EDIT", state.text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            24, 58, dlgWidth - 48, textAreaH,
            hMsgBox, NULL, hInst, NULL
        );
        if (state.hTextCtrl)
        {
            SetWindowTheme(state.hTextCtrl, L"DarkMode_Explorer", NULL);
            SendMessageW(state.hTextCtrl, WM_SETFONT, (WPARAM)state.hFontText, TRUE);
        }
    }

    ShowWindow(hMsgBox, SW_SHOW);
    UpdateWindow(hMsgBox);
    SetFocus(hMsgBox);

    // Modal Message Loop
    MSG msg;
    while (!state.isClosed && GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, TRUE);
        SetForegroundWindow(hWndParent);
        SetFocus(hWndParent);
    }

    if (state.hFontTitle) DeleteObject(state.hFontTitle);
    if (state.hFontText) DeleteObject(state.hFontText);
    if (state.hFontIcon) DeleteObject(state.hFontIcon);

    return state.result;
}





int ShowModernMessageBox(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    return ShowModernMessageBoxEx(hWndParent, lpText, lpCaption, {}, uType);
}
