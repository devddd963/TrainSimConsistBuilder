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
    bool hasCheckbox;
    std::wstring checkboxText;
    bool isChecked;
    RECT rcCheckbox;
    bool isCheckboxHovered;
};

static void SafeCloseMessageBox(MsgBoxState* pState, HWND hWnd, int resultId)
{
    if (!pState)
    {
        DestroyWindow(hWnd);
        return;
    }

    pState->result = resultId;
    pState->isClosed = true;

    // 1. Re-enable parent window BEFORE destroying modal dialog so Windows has an eligible window to activate:
    HWND hParent = pState->hParent;
    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);

        // AttachThreadInput guard to guarantee foreground activation permission:
        DWORD dwForeThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
        DWORD dwAppThread = GetCurrentThreadId();
        if (dwForeThread != 0 && dwForeThread != dwAppThread)
        {
            AttachThreadInput(dwAppThread, dwForeThread, TRUE);
        }

        if (IsIconic(hParent))
        {
            ShowWindow(hParent, SW_RESTORE);
        }
        SetWindowPos(hParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetActiveWindow(hParent);
        SetForegroundWindow(hParent);
        BringWindowToTop(hParent);
        SetFocus(hParent);

        if (dwForeThread != 0 && dwForeThread != dwAppThread)
        {
            AttachThreadInput(dwAppThread, dwForeThread, FALSE);
        }
    }

    // 2. Now destroy the message box window:
    DestroyWindow(hWnd);
}

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
            int textBottom = pState->hasCheckbox ? (pState->rcCheckbox.top - 12) : (pState->buttons.empty() ? (h - 20) : (pState->buttons[0].rc.top - 14));
            RECT rcText = { 24, 56, w - 24, textBottom };
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

        // Draw Checkbox if requested
        if (pState->hasCheckbox && pState->rcCheckbox.right > pState->rcCheckbox.left)
        {
            int chkY = pState->rcCheckbox.top + (pState->rcCheckbox.bottom - pState->rcCheckbox.top - 18) / 2;
            RECT rcBox = { pState->rcCheckbox.left, chkY, pState->rcCheckbox.left + 18, chkY + 18 };

            COLORREF chkBg = pState->isChecked ? RGB(0, 120, 215) : (pState->isCheckboxHovered ? RGB(48, 48, 54) : RGB(32, 32, 36));
            COLORREF chkBorder = pState->isChecked ? RGB(0, 140, 240) : (pState->isCheckboxHovered ? RGB(110, 110, 120) : RGB(70, 70, 75));

            HBRUSH hbrChk = CreateSolidBrush(chkBg);
            HPEN hpChk = CreatePen(PS_SOLID, 1, chkBorder);
            HGDIOBJ oB = SelectObject(hMemDC, hbrChk);
            HGDIOBJ oP = SelectObject(hMemDC, hpChk);
            RoundRect(hMemDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);
            SelectObject(hMemDC, oB);
            SelectObject(hMemDC, oP);
            DeleteObject(hbrChk);
            DeleteObject(hpChk);

            if (pState->isChecked)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
                SetTextColor(hMemDC, RGB(255, 255, 255));
                DrawTextW(hMemDC, L"✓", -1, &rcBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hMemDC, hOldF);
            }

            // Checkbox Label
            if (pState->hFontText)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontText);
                SetTextColor(hMemDC, pState->isCheckboxHovered ? RGB(255, 255, 255) : RGB(210, 210, 215));
                RECT rcTxt = { rcBox.right + 8, pState->rcCheckbox.top, pState->rcCheckbox.right, pState->rcCheckbox.bottom };
                DrawTextW(hMemDC, pState->checkboxText.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

    case WM_SETCURSOR:
    {
        if (pState)
        {
            if (pState->hoverButton != -1 || (pState->hasCheckbox && pState->isCheckboxHovered))
            {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
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

        if (pState->hasCheckbox)
        {
            bool hoverChk = PtInRect(&pState->rcCheckbox, { x, y });
            if (hoverChk != pState->isCheckboxHovered)
            {
                pState->isCheckboxHovered = hoverChk;
                InvalidateRect(hWnd, &pState->rcCheckbox, FALSE);
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

        if (pState->hasCheckbox && PtInRect(&pState->rcCheckbox, { x, y }))
        {
            pState->isChecked = !pState->isChecked;
            InvalidateRect(hWnd, &pState->rcCheckbox, FALSE);
            return 0;
        }

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
                    SafeCloseMessageBox(pState, hWnd, pState->buttons[clicked].id);
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
            SafeCloseMessageBox(pState, hWnd, cancelId);
            return 0;
        }
        else if (wParam == VK_RETURN || wParam == VK_SPACE)
        {
            int retId = 0;
            if (pState->focusedButton >= 0 && pState->focusedButton < (int)pState->buttons.size())
            {
                retId = pState->buttons[pState->focusedButton].id;
            }
            else if (!pState->buttons.empty())
            {
                retId = pState->buttons[0].id;
            }
            SafeCloseMessageBox(pState, hWnd, retId);
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

    case WM_CLOSE:
    {
        int cancelId = IDCANCEL;
        if (pState && !pState->buttons.empty())
        {
            for (const auto& btn : pState->buttons)
            {
                if (btn.id == IDCANCEL || btn.id == IDNO) { cancelId = btn.id; break; }
            }
        }
        SafeCloseMessageBox(pState, hWnd, cancelId);
        return 0;
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

static int ShowModernMessageBoxImpl(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, const std::vector<ModernMsgBoxCustomButton>& customButtons, UINT uType, LPCWSTR lpCheckboxText = NULL, bool* pbChecked = NULL)
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
        wcx.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(107 /* IDI_TRAINSIMCONSISTBUILDER */));
        wcx.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(107), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (!wcx.hIconSm) wcx.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(108 /* IDI_SMALL */));
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

    // 1. Measure Buttons to calculate total button row width
    std::vector<int> measuredBtnWidths;
    int totalBtnsWidth = 0;
    if (!customButtons.empty())
    {
        HDC hdcB = GetDC(NULL);
        HFONT hOldB = (HFONT)SelectObject(hdcB, state.hFontText);
        for (const auto& btn : customButtons)
        {
            int bw = 90;
            SIZE szB = { 0 };
            GetTextExtentPoint32W(hdcB, btn.text.c_str(), (int)btn.text.length(), &szB);
            if (szB.cx + 28 > bw) bw = szB.cx + 28;
            measuredBtnWidths.push_back(bw);
            totalBtnsWidth += bw;
        }
        SelectObject(hdcB, hOldB);
        ReleaseDC(NULL, hdcB);
        totalBtnsWidth += (int)(customButtons.size() - 1) * 10;
    }
    else
    {
        UINT btnType = uType & MB_TYPEMASK;
        int count = (btnType == MB_YESNOCANCEL) ? 3 : ((btnType == MB_YESNO || btnType == MB_OKCANCEL) ? 2 : 1);
        totalBtnsWidth = count * 90 + (count - 1) * 12;
    }

    // 2. Measure Checkbox text
    SIZE szCheckbox = { 0 };
    if (lpCheckboxText && lpCheckboxText[0] != L'\0')
    {
        state.hasCheckbox = true;
        state.checkboxText = lpCheckboxText;
        state.isChecked = (pbChecked && *pbChecked);
        state.isCheckboxHovered = false;

        HDC hdcB = GetDC(NULL);
        HFONT hOld = (HFONT)SelectObject(hdcB, state.hFontText);
        GetTextExtentPoint32W(hdcB, state.checkboxText.c_str(), (int)state.checkboxText.length(), &szCheckbox);
        SelectObject(hdcB, hOld);
        ReleaseDC(NULL, hdcB);
    }
    else
    {
        state.hasCheckbox = false;
        state.isChecked = false;
        state.isCheckboxHovered = false;
        state.rcCheckbox = { 0, 0, 0, 0 };
    }

    // 3. Determine Dialog Width dynamically (ensures spacious layout without overlap)
    int dlgWidth = 680;
    if (totalBtnsWidth + 56 > dlgWidth) dlgWidth = totalBtnsWidth + 56;
    if (state.hasCheckbox && (int)szCheckbox.cx + 60 > dlgWidth) dlgWidth = (int)szCheckbox.cx + 60;

    // 4. Measure Body Text Height with determined dlgWidth
    HDC hdcScreen = GetDC(NULL);
    HFONT hOldF = (HFONT)SelectObject(hdcScreen, state.hFontText);
    RECT rcCalc = { 0, 0, dlgWidth - 48, 0 };
    DrawTextW(hdcScreen, state.text.c_str(), -1, &rcCalc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdcScreen, hOldF);
    ReleaseDC(NULL, hdcScreen);

    int textHeight = rcCalc.bottom - rcCalc.top;
    if (textHeight < 30) textHeight = 30;

    // 5. Calculate Heights & Separate Rows for Checkbox and Buttons
    // Header (56px) + Body Text (textHeight) + Checkbox Row (if present: 38px) + Button Bar (68px)
    int btnH = 32;
    int chkH = 22;
    int requiredH = 56 + textHeight + (state.hasCheckbox ? 38 : 0) + 68;

    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxDlgH = (std::min)(720, (int)((rcWork.bottom - rcWork.top) * 0.85));

    int dlgHeight = requiredH;
    state.isScrollable = false;
    if (dlgHeight > maxDlgH)
    {
        dlgHeight = maxDlgH;
        state.isScrollable = true;
    }
    if (dlgHeight < 160) dlgHeight = 160;

    int btnY = dlgHeight - 48;
    int chkY = state.hasCheckbox ? (btnY - 34) : 0;
    if (!state.isScrollable)
    {
        chkY = 56 + textHeight + 14;
        btnY = state.hasCheckbox ? (chkY + chkH + 16) : (56 + textHeight + 18);
    }

    if (state.hasCheckbox)
    {
        state.rcCheckbox = { 24, chkY, 24 + 18 + 8 + (int)szCheckbox.cx + 8, chkY + chkH };
    }

    // 6. Build Buttons on their own separate bottom row
    if (!customButtons.empty())
    {
        int curRight = dlgWidth - 24;
        for (int bIdx = (int)customButtons.size() - 1; bIdx >= 0; --bIdx)
        {
            const auto& cb = customButtons[bIdx];
            int btnW = measuredBtnWidths[bIdx];
            int x = curRight - btnW;
            state.buttons.insert(state.buttons.begin(), { cb.id, cb.text, { x, btnY, x + btnW, btnY + btnH }, cb.isDefault });
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

    // Position Window Centered on Root Parent Window or Screen Work Area
    HWND hRootParent = (hWndParent && IsWindow(hWndParent)) ? GetAncestor(hWndParent, GA_ROOT) : NULL;
    if (!hRootParent && hWndParent && IsWindow(hWndParent)) hRootParent = hWndParent;

    RECT rcParent;
    if (hRootParent && IsWindow(hRootParent) && !IsIconic(hRootParent))
    {
        GetWindowRect(hRootParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int posX = rcParent.left + (rcParent.right - rcParent.left - dlgWidth) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - dlgHeight) / 2;

    RECT rcWorkArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWorkArea, 0);
    if (posX < rcWorkArea.left) posX = rcWorkArea.left;
    if (posY < rcWorkArea.top) posY = rcWorkArea.top;
    if (posX + dlgWidth > rcWorkArea.right) posX = rcWorkArea.right - dlgWidth;
    if (posY + dlgHeight > rcWorkArea.bottom) posY = rcWorkArea.bottom - dlgHeight;

    DWORD dwExStyle = (hWndParent && IsWindow(hWndParent)) ? WS_EX_TOOLWINDOW : (WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    HWND hMsgBox = CreateWindowExW(
        dwExStyle,
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
        int textCtrlTop = 56;
        int textCtrlBottom = state.hasCheckbox ? (state.rcCheckbox.top - 12) : (state.buttons.empty() ? (dlgHeight - 60) : (state.buttons[0].rc.top - 14));
        int textAreaH = textCtrlBottom - textCtrlTop;
        if (textAreaH < 40) textAreaH = 40;

        state.hTextCtrl = CreateWindowExW(
            0, L"EDIT", state.text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            24, textCtrlTop, dlgWidth - 48, textAreaH,
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
        if (IsIconic(hWndParent))
        {
            ShowWindow(hWndParent, SW_RESTORE);
        }
        SetWindowPos(hWndParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hWndParent);
        SetActiveWindow(hWndParent);
        BringWindowToTop(hWndParent);
        SetFocus(hWndParent);
    }

    if (pbChecked) *pbChecked = state.isChecked;

    if (state.hFontTitle) DeleteObject(state.hFontTitle);
    if (state.hFontText) DeleteObject(state.hFontText);
    if (state.hFontIcon) DeleteObject(state.hFontIcon);

    return state.result;
}

int ShowModernMessageBoxEx(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, const std::vector<ModernMsgBoxCustomButton>& customButtons, UINT uType)
{
    return ShowModernMessageBoxImpl(hWndParent, lpText, lpCaption, customButtons, uType, NULL, NULL);
}

int ShowModernMessageBoxWithCheckbox(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, LPCWSTR lpCheckboxText, bool* pbChecked, UINT uType)
{
    return ShowModernMessageBoxImpl(hWndParent, lpText, lpCaption, {}, uType, lpCheckboxText, pbChecked);
}

int ShowModernMessageBoxExWithCheckbox(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, const std::vector<ModernMsgBoxCustomButton>& customButtons, LPCWSTR lpCheckboxText, bool* pbChecked, UINT uType)
{
    return ShowModernMessageBoxImpl(hWndParent, lpText, lpCaption, customButtons, uType, lpCheckboxText, pbChecked);
}

int ShowModernMessageBox(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    return ShowModernMessageBoxEx(hWndParent, lpText, lpCaption, {}, uType);
}
