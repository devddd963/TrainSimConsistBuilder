#include "SetConsistParametersDlg.h"
#include "ModernEditControl.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

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

struct DlgState {
    int mode = 0; // 0 = Speed, 1 = Perf, 2 = Both
    double speedVal = 0.0;
    double perfVal = 1.0;
    bool resultOk = false;
    bool isClosed = false;

    HWND hParent = NULL;
    HWND hFocusSink = NULL;
    HWND hEditSpeed = NULL;
    HWND hEditPerf = NULL;
    HWND hBtnApply = NULL;
    HWND hBtnCancel = NULL;

    HFONT hFontTitle = NULL;
    HFONT hFontNormal = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontSmall = NULL;
    HBRUSH hbrBg = NULL;
    HBRUSH hbrEditNormal = NULL;
    HBRUSH hbrEditFocus = NULL;
};

static void SafeCloseDialog(DlgState* pState, HWND hWnd, bool ok)
{
    if (!pState)
    {
        DestroyWindow(hWnd);
        return;
    }

    pState->resultOk = ok;
    pState->isClosed = true;

    HWND hParent = pState->hParent;
    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);

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

    DestroyWindow(hWnd);
}

static LRESULT CALLBACK SetParamsDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DlgState* pState = (DlgState*)GetPropW(hWnd, L"TSCB_SET_PARAMS_STATE");

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetPropW(hWnd, L"TSCB_SET_PARAMS_STATE", (HANDLE)cs->lpCreateParams);
        return TRUE;
    }

    case WM_CREATE:
    {
        pState = (DlgState*)GetPropW(hWnd, L"TSCB_SET_PARAMS_STATE");
        if (!pState) return -1;

        // Dark theme titlebar & rounded corners
        BOOL bDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        COLORREF capColor = RGB(28, 30, 38);
        DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &capColor, sizeof(capColor));

        // Clear default application/document titlebar icon
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
        BYTE andMask = 0xFF;
        BYTE xorMask = 0x00;
        HICON hEmptyIcon = CreateIcon(hInst, 1, 1, 1, 1, &andMask, &xorMask);
        if (hEmptyIcon)
        {
            SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hEmptyIcon);
            SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hEmptyIcon);
        }

        pState->hbrBg = CreateSolidBrush(RGB(28, 30, 38));
        pState->hbrEditNormal = CreateSolidBrush(RGB(38, 38, 38));
        pState->hbrEditFocus = CreateSolidBrush(RGB(30, 30, 30));

        pState->hFontTitle = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        pState->hFocusSink = CreateWindowExW(
            0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, -100, -100, 0, 0, hWnd, NULL, hInst, NULL
        );

        auto FormatNumber = [](double v, wchar_t* buf, size_t bufCch) {
            double whole;
            if (std::modf(v, &whole) == 0.0)
                swprintf_s(buf, bufCch, L"%.0f", v);
            else
                swprintf_s(buf, bufCch, L"%.3f", v);
        };

        const DWORD dwEditStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOHSCROLL;
        const int FIELD_H = 32;
        const int VPAD = 7;
        int fieldY = 66;

        if (pState->mode == 0 || pState->mode == 2)
        {
            // Speed Limit
            wchar_t szVel[64];
            FormatNumber(pState->speedVal, szVel, 64);

            pState->hEditSpeed = CreateWindowExW(
                0, L"EDIT", szVel,
                dwEditStyle,
                24, fieldY + 22, 322, FIELD_H, hWnd, (HMENU)101, hInst, NULL
            );
            if (pState->hEditSpeed)
            {
                SendMessageW(pState->hEditSpeed, WM_SETFONT, (WPARAM)pState->hFontNormal, MAKELPARAM(TRUE, 0));
                SendMessageW(pState->hEditSpeed, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(9, 9));
                SendMessageW(pState->hEditSpeed, EM_SETCUEBANNER, TRUE, (LPARAM)L"e.g. 130");
                ModernEdit_Attach(pState->hEditSpeed, true, true, true, true);
            }
            fieldY += 62;
        }

        if (pState->mode == 1 || pState->mode == 2)
        {
            // Performance Factor
            wchar_t szPerf[64];
            FormatNumber(pState->perfVal, szPerf, 64);

            pState->hEditPerf = CreateWindowExW(
                0, L"EDIT", szPerf,
                dwEditStyle,
                24, fieldY + 22, 322, FIELD_H, hWnd, (HMENU)102, hInst, NULL
            );
            if (pState->hEditPerf)
            {
                SendMessageW(pState->hEditPerf, WM_SETFONT, (WPARAM)pState->hFontNormal, MAKELPARAM(TRUE, 0));
                SendMessageW(pState->hEditPerf, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(9, 9));
                SendMessageW(pState->hEditPerf, EM_SETCUEBANNER, TRUE, (LPARAM)L"e.g. 1.00");
                ModernEdit_Attach(pState->hEditPerf, true, true, true, true);
            }
            fieldY += 62;
        }

        // Apply & Cancel buttons (Modern styled, right-aligned to 346)
        int btnY = fieldY + 10;

        pState->hBtnApply = CreateWindowExW(
            0, L"BUTTON", L"Apply Parameters",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            116, btnY, 140, 32, hWnd, (HMENU)IDOK, hInst, NULL
        );
        pState->hBtnCancel = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            266, btnY, 80, 32, hWnd, (HMENU)IDCANCEL, hInst, NULL
        );

        if (pState->hBtnApply)
        {
            ModernButton_Attach(pState->hBtnApply, true, pState->hFontBold);
        }
        if (pState->hBtnCancel)
        {
            ModernButton_Attach(pState->hBtnCancel, false, pState->hFontNormal);
        }

        if (pState->hEditSpeed) SetFocus(pState->hEditSpeed);
        else if (pState->hEditPerf) SetFocus(pState->hEditPerf);

        return 0;
    }

    case WM_SHOWWINDOW:
    {
        if (wParam && pState)
        {
            RECT rcFmt = { 6, 8, 322 - 6, 24 };
            if (pState->hEditSpeed) SendMessageW(pState->hEditSpeed, EM_SETRECT, 0, (LPARAM)&rcFmt);
            if (pState->hEditPerf) SendMessageW(pState->hEditPerf, EM_SETRECT, 0, (LPARAM)&rcFmt);

            HWND hTarget = pState->hEditSpeed ? pState->hEditSpeed : pState->hEditPerf;
            if (hTarget)
            {
                SetFocus(hTarget);
                SendMessageW(hTarget, EM_SETSEL, 0, -1);
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        // Clicking outside any control on the dialog drops focus from the edit box
        if (pState && pState->hFocusSink) SetFocus(pState->hFocusSink);
        else SetFocus(hWnd);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        return ModernEdit_OnCtlColor((HDC)wParam, (HWND)lParam);
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 225, 235));
        SetBkColor(hdcStatic, RGB(28, 30, 38));
        return (LRESULT)pState->hbrBg;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        FillRect(hdc, &rc, pState->hbrBg);
        SetBkMode(hdc, TRANSPARENT);

        // Header Title
        SelectObject(hdc, pState->hFontTitle);
        SetTextColor(hdc, RGB(255, 255, 255));
        const wchar_t* title = (pState->mode == 0) ? L"Set Consist Speed Limit" :
                               ((pState->mode == 1) ? L"Set Performance Factor" : L"Set Consist Parameters");
        TextOutW(hdc, 24, 16, title, (int)wcslen(title));

        // Subtitle
        SelectObject(hdc, pState->hFontSmall);
        SetTextColor(hdc, RGB(145, 150, 165));
        const wchar_t* sub = L"Values will be applied to all selected consists:";
        TextOutW(hdc, 24, 38, sub, (int)wcslen(sub));

        int y = 66;
        SelectObject(hdc, pState->hFontNormal);
        SetTextColor(hdc, RGB(205, 215, 230));

        if (pState->mode == 0 || pState->mode == 2)
        {
            const wchar_t* lblSpeed = L"Speed Limit (km/h):";
            TextOutW(hdc, 24, y, lblSpeed, (int)wcslen(lblSpeed));
            y += 62;
        }

        if (pState->mode == 1 || pState->mode == 2)
        {
            const wchar_t* lblPerf = L"Performance Factor (% or decimal, e.g. 1.00):";
            TextOutW(hdc, 24, y, lblPerf, (int)wcslen(lblPerf));
            y += 62;
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_COMMAND:
    {
        WORD cmdId = LOWORD(wParam);
        if (cmdId == IDOK)
        {
            wchar_t buf[128] = { 0 };
            if (pState->hEditSpeed)
            {
                GetWindowTextW(pState->hEditSpeed, buf, 128);
                pState->speedVal = _wtof(buf);
            }
            if (pState->hEditPerf)
            {
                GetWindowTextW(pState->hEditPerf, buf, 128);
                pState->perfVal = _wtof(buf);
            }
            SafeCloseDialog(pState, hWnd, true);
            return 0;
        }
        else if (cmdId == IDCANCEL)
        {
            SafeCloseDialog(pState, hWnd, false);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
    {
        SafeCloseDialog(pState, hWnd, false);
        return 0;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontNormal) DeleteObject(pState->hFontNormal);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
            if (pState->hbrBg) DeleteObject(pState->hbrBg);
            if (pState->hbrEditNormal) DeleteObject(pState->hbrEditNormal);
            if (pState->hbrEditFocus) DeleteObject(pState->hbrEditFocus);
        }
        RemovePropW(hWnd, L"TSCB_SET_PARAMS_STATE");
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace

bool ShowSetConsistParametersDialog(
    HWND hWndParent,
    int mode,
    double initialSpeed,
    double initialPerf,
    double& outSpeed,
    double& outPerf)
{
    static bool s_classRegistered = false;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    const wchar_t* clsName = L"TSCBSetConsistParametersDlg";

    if (!s_classRegistered)
    {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = SetParamsDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = clsName;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    DlgState state;
    state.mode = mode;
    state.speedVal = initialSpeed > 0 ? initialSpeed : 120.0;
    state.perfVal = initialPerf > 0 ? initialPerf : 1.0;
    state.resultOk = false;
    state.isClosed = false;
    state.hParent = hWndParent;

    int clientW = 370;
    int clientH = (mode == 2) ? 246 : 184;

    RECT rcClient = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rcClient, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    int dlgW = rcClient.right - rcClient.left;
    int dlgH = rcClient.bottom - rcClient.top;

    RECT rcParent;
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        rcParent = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        0,
        clsName,
        (mode == 0) ? L"Set Speed Limit" : ((mode == 1) ? L"Set Performance Factor" : L"Set Consist Parameters"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hWndParent, NULL, hInst, &state
    );

    if (!hDlg) return false;

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, FALSE);
    }

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    RECT rcInitFmt = { 6, 8, 322 - 6, 24 };
    if (state.hEditSpeed)
    {
        SendMessageW(state.hEditSpeed, EM_SETRECT, 0, (LPARAM)&rcInitFmt);
        SetFocus(state.hEditSpeed);
        SendMessageW(state.hEditSpeed, EM_SETSEL, 0, -1);
    }
    else if (state.hEditPerf)
    {
        SendMessageW(state.hEditPerf, EM_SETRECT, 0, (LPARAM)&rcInitFmt);
        SetFocus(state.hEditPerf);
        SendMessageW(state.hEditPerf, EM_SETSEL, 0, -1);
    }

    // Modal message loop
    MSG msg;
    while (!state.isClosed && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            continue;
        }

        if (!IsDialogMessageW(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!IsWindow(hDlg))
        {
            break;
        }
    }

    if (state.resultOk)
    {
        if (mode == 0 || mode == 2) outSpeed = state.speedVal;
        if (mode == 1 || mode == 2) outPerf = state.perfVal;
        return true;
    }
    return false;
}
