#include "SmsEditorView.h"
#include "UITheme.h"
#include "CustomScrollBar.h"
#include "ModernMessageBox.h"
#include "ModernContextMenu.h"
#include "FluentDragGhost.h"
#include "SmsStreamMutatorDlg.h"
#include "SmsAssetConsolidatorDlg.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include <windowsx.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")

static const wchar_t* SMS_EDITOR_WNDCLASS = L"TscbSmsEditorView";

static bool s_isPlayingAudio = false;
static std::wstring s_playingPath = L"";

static void StopAudio()
{
    if (s_isPlayingAudio)
    {
        mciSendStringW(L"stop SmsAudioAlias", NULL, 0, NULL);
        mciSendStringW(L"close SmsAudioAlias", NULL, 0, NULL);
        PlaySoundW(NULL, NULL, 0);
        s_isPlayingAudio = false;
        s_playingPath = L"";
    }
}

static bool PlayAudio(const std::wstring& path, bool repeat = false)
{
    StopAudio();
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    // 1. Convert to 8.3 short path if available to prevent WinMM MCI error 304 on long/hyphenated folder paths
    wchar_t shortBuf[MAX_PATH] = { 0 };
    std::wstring targetPath = path;
    if (GetShortPathNameW(path.c_str(), shortBuf, MAX_PATH) > 0)
    {
        targetPath = shortBuf;
    }

    // 2. Try MCI playback (supports looping)
    std::wstring openCmd = L"open \"" + targetPath + L"\" type waveaudio alias SmsAudioAlias";
    if (mciSendStringW(openCmd.c_str(), NULL, 0, NULL) == 0)
    {
        std::wstring playCmd = repeat ? L"play SmsAudioAlias repeat" : L"play SmsAudioAlias";
        if (mciSendStringW(playCmd.c_str(), NULL, 0, NULL) == 0)
        {
            s_isPlayingAudio = true;
            s_playingPath = path;
            return true;
        }
        mciSendStringW(L"close SmsAudioAlias", NULL, 0, NULL);
    }

    // 3. Resilient fallback to PlaySoundW (direct Windows multimedia wave player, no parser limits)
    DWORD flags = SND_FILENAME | SND_ASYNC;
    if (repeat) flags |= SND_LOOP;
    if (PlaySoundW(path.c_str(), NULL, flags))
    {
        s_isPlayingAudio = true;
        s_playingPath = path;
        return true;
    }

    return false;
}

static void OpenFileInDefaultEditor(HWND hWnd, const std::wstring& filePath)
{
    if (filePath.empty()) return;
    if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ShowModernMessageBox(hWnd, (L"The sound file was not found on disk:\r\n\r\n" + filePath).c_str(), L"File Not Found", MB_OK | MB_ICONWARNING);
        return;
    }

    // Launch with user's default system association for .sms files (e.g. VS Code, Notepad++, Notepad)
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst <= 32)
    {
        // Fallback: If no associated editor is configured for .sms, launch in Notepad
        HINSTANCE hNotepad = ShellExecuteW(NULL, L"open", L"notepad.exe", filePath.c_str(), NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hNotepad <= 32)
        {
            // If notepad fails as well, invoke the system 'Open With' dialog
            ShellExecuteW(NULL, L"openas", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

struct ContextTreeItem {
    enum Type { Header, Unit, SoundSlot, Standalone } type;
    std::wstring label;
    std::wstring subLabel;
    std::wstring filePath;
    bool isExpanded = true;
    bool isSelected = false;
    bool exists = true;
};

#define s_streamClipboard SmsClipboard_Get()

static HFONT CreateSmsCustomFont(int pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static void DrawSmsModernButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* iconGlyph = NULL)
{
    COLORREF bgCol;
    COLORREF borderCol;
    COLORREF textCol;

    if (isAccent)
    {
        bgCol = isPressed ? RGB(0, 100, 185) : (isHovered ? RGB(20, 140, 235) : RGB(0, 120, 215));
        borderCol = bgCol;
        textCol = RGB(255, 255, 255);
    }
    else
    {
        bgCol = isPressed ? RGB(36, 36, 36) : (isHovered ? RGB(52, 52, 52) : RGB(40, 40, 40));
        borderCol = isHovered ? RGB(80, 80, 80) : RGB(60, 60, 60);
        textCol = RGB(245, 245, 245);
    }

    HBRUSH hbr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
    HPEN holdPen = (HPEN)SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, holdBr);
    SelectObject(hdc, holdPen);
    DeleteObject(hbr);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);
    SelectObject(hdc, hFont);
    RECT rcText = rc;
    DrawTextW(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

struct SmsInputModalState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hEdit = NULL;
    std::wstring title;
    std::wstring prompt;
    std::wstring resultText;
    bool isConfirmed = false;
    HFONT hFontTitle = NULL;
    HFONT hFontPrompt = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontIcon = NULL;
    RECT rcOkBtn = { 0 };
    RECT rcCancelBtn = { 0 };
    RECT rcCloseBtn = { 0 };
    bool isHoverOk = false;
    bool isHoverCancel = false;
    bool isHoverClose = false;
};

static LRESULT CALLBACK SmsInputModalProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SmsInputModalState* pState = (SmsInputModalState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (SmsInputModalState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    case WM_CREATE:
    {
        pState->hFontTitle  = CreateSmsCustomFont(14, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontPrompt = CreateSmsCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMain   = CreateSmsCustomFont(11, FW_NORMAL, L"Segoe UI");
        pState->hFontBold   = CreateSmsCustomFont(11, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontIcon   = CreateSmsCustomFont(12, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateSmsCustomFont(12, FW_NORMAL, L"Segoe MDL2 Assets");

        // dlgW = 500, dlgH = 210
        // rcEditBox is: { 24, 74, 476, 118 } (height 44)
        // Vertically centered: y = 74 + (44 - 24) / 2 = 84
        pState->hEdit = CreateWindowExW(
            0, L"EDIT", pState->resultText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            36, 84, 428, 24, hWnd, (HMENU)101, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEdit, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        SendMessageW(pState->hEdit, EM_SETSEL, 0, -1);
        SetFocus(pState->hEdit);
        return 0;
    }
    case WM_NCHITTEST:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        if (PtInRect(&pState->rcOkBtn, pt) ||
            PtInRect(&pState->rcCancelBtn, pt) ||
            PtInRect(&pState->rcCloseBtn, pt) ||
            (pt.x >= 24 && pt.x <= 476 && pt.y >= 74 && pt.y <= 118))
        {
            return HTCLIENT;
        }

        if (pt.y < 50)
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        COLORREF bgCol = RGB(26, 29, 38);
        COLORREF txtCol = RGB(245, 248, 255);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, txtCol);
        static HBRUSH hbrEditDark = CreateSolidBrush(RGB(26, 29, 38));
        return (LRESULT)hbrEditDark;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP holdBmp = (HBITMAP)SelectObject(hdcMem, hbm);

        // 1. Background Fill (Windows 11 dark mode canvas)
        COLORREF bgCol = RGB(20, 22, 28);
        HBRUSH hbrBg = CreateSolidBrush(bgCol);
        FillRect(hdcMem, &rc, hbrBg);
        DeleteObject(hbrBg);

        SetBkMode(hdcMem, TRANSPARENT);

        // 2. Header Icon & Title
        if (pState->hFontIcon)
        {
            SelectObject(hdcMem, pState->hFontIcon);
            SetTextColor(hdcMem, RGB(96, 205, 255));
            RECT rcIcon = { 20, 16, 44, 44 };
            DrawTextW(hdcMem, L"\xE70F", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(hdcMem, pState->hFontTitle);
        SetTextColor(hdcMem, RGB(245, 248, 255));
        RECT rcTitle = { 46, 15, w - 50, 44 };
        DrawTextW(hdcMem, pState->title.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close Button [✕] at top right
        pState->rcCloseBtn = { w - 38, 14, w - 14, 38 };
        if (pState->isHoverClose)
        {
            HBRUSH hbrCloseHover = CreateSolidBrush(RGB(180, 40, 40));
            FillRect(hdcMem, &pState->rcCloseBtn, hbrCloseHover);
            DeleteObject(hbrCloseHover);
        }
        SelectObject(hdcMem, pState->hFontMain);
        SetTextColor(hdcMem, pState->isHoverClose ? RGB(255, 255, 255) : RGB(160, 170, 185));
        DrawTextW(hdcMem, L"✕", -1, &pState->rcCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 3. Prompt Subtitle Label
        SelectObject(hdcMem, pState->hFontPrompt);
        SetTextColor(hdcMem, RGB(165, 175, 195));
        RECT rcPrompt = { 24, 48, w - 24, 70 };
        DrawTextW(hdcMem, pState->prompt.c_str(), -1, &rcPrompt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 4. Custom Rounded Edit Box Container (44px high, edit is 24px high and perfectly centered!)
        RECT rcEditBox = { 24, 74, w - 24, 118 };
        COLORREF editBoxBg = RGB(26, 29, 38);
        COLORREF editBoxBorder = RGB(52, 60, 78);

        HBRUSH hbrBox = CreateSolidBrush(editBoxBg);
        HPEN hPenBox = CreatePen(PS_SOLID, 1, editBoxBorder);
        HBRUSH holdB = (HBRUSH)SelectObject(hdcMem, hbrBox);
        HPEN holdP = (HPEN)SelectObject(hdcMem, hPenBox);
        RoundRect(hdcMem, rcEditBox.left, rcEditBox.top, rcEditBox.right, rcEditBox.bottom, 4, 4);
        SelectObject(hdcMem, holdB);
        SelectObject(hdcMem, holdP);
        DeleteObject(hbrBox);
        DeleteObject(hPenBox);

        // 5. Buttons Row
        int btnW = 100;
        int btnH = 36;
        int btnY = h - 50;
        int btnOkX = w - 24 - btnW;
        int btnCancelX = btnOkX - 12 - btnW;

        pState->rcOkBtn = { btnOkX, btnY, btnOkX + btnW, btnY + btnH };
        pState->rcCancelBtn = { btnCancelX, btnY, btnCancelX + btnW, btnY + btnH };

        DrawSmsModernButton(hdcMem, pState->rcCancelBtn, L"Cancel", pState->isHoverCancel, false, false, pState->hFontMain);
        DrawSmsModernButton(hdcMem, pState->rcOkBtn, L"OK", pState->isHoverOk, false, true, pState->hFontBold);

        // 6. Clean 1px Outer Border (no jagged RoundRect clipping with DWM)
        COLORREF winBorderCol = RGB(55, 60, 75);
        HPEN hPenWin = CreatePen(PS_SOLID, 1, winBorderCol);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ oldPen = SelectObject(hdcMem, hPenWin);
        HGDIOBJ oldBr = SelectObject(hdcMem, hNullB);
        Rectangle(hdcMem, 0, 0, w, h);
        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBr);
        DeleteObject(hPenWin);

        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, holdBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool hOk = PtInRect(&pState->rcOkBtn, pt) != FALSE;
        bool hCancel = PtInRect(&pState->rcCancelBtn, pt) != FALSE;
        bool hClose = PtInRect(&pState->rcCloseBtn, pt) != FALSE;

        if (hOk != pState->isHoverOk || hCancel != pState->isHoverCancel || hClose != pState->isHoverClose)
        {
            pState->isHoverOk = hOk;
            pState->isHoverCancel = hCancel;
            pState->isHoverClose = hClose;
            InvalidateRect(hWnd, NULL, FALSE);

            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        pState->isHoverOk = false;
        pState->isHoverCancel = false;
        pState->isHoverClose = false;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&pState->rcOkBtn, pt))
        {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(pState->hEdit, buf, 256);
            pState->resultText = buf;
            pState->isConfirmed = true;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (PtInRect(&pState->rcCancelBtn, pt) || PtInRect(&pState->rcCloseBtn, pt))
        {
            pState->isConfirmed = false;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
    {
        if (wParam == VK_RETURN)
        {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(pState->hEdit, buf, 256);
            pState->resultText = buf;
            pState->isConfirmed = true;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        else if (wParam == VK_ESCAPE)
        {
            pState->isConfirmed = false;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
    {
        if (pState->hFontTitle)  DeleteObject(pState->hFontTitle);
        if (pState->hFontPrompt) DeleteObject(pState->hFontPrompt);
        if (pState->hFontMain)   DeleteObject(pState->hFontMain);
        if (pState->hFontBold)   DeleteObject(pState->hFontBold);
        if (pState->hFontIcon)   DeleteObject(pState->hFontIcon);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool PromptSmsModalText(HWND hParent, const std::wstring& title, const std::wstring& prompt, const std::wstring& defaultVal, std::wstring& outVal)
{
    static bool s_reg = false;
    const wchar_t* szClassName = L"TSCB_SmsInputModal";
    if (!s_reg)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SmsInputModalProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(24, 24, 24));
        wc.lpszClassName = szClassName;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    SmsInputModalState state;
    state.hParent = hParent;
    state.title = title;
    state.prompt = prompt;
    state.resultText = defaultVal;

    int w = 500, h = 210;
    HMONITOR hMon = MonitorFromWindow(hParent ? hParent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT rcWork;
    if (GetMonitorInfoW(hMon, &mi)) {
        rcWork = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    }

    int x = 0, y = 0;
    if (hParent && IsWindow(hParent))
    {
        RECT rcParent;
        GetWindowRect(hParent, &rcParent);
        x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
        y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;
    }
    else
    {
        x = rcWork.left + (rcWork.right - rcWork.left - w) / 2;
        y = rcWork.top + (rcWork.bottom - rcWork.top - h) / 2;
    }

    // Ensure completely within work area boundaries
    if (x + w > rcWork.right) x = rcWork.right - w;
    if (y + h > rcWork.bottom) y = rcWork.bottom - h;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClassName, title.c_str(),
        WS_POPUP | WS_VISIBLE, x, y, w, h,
        hParent, NULL, GetModuleHandleW(NULL), &state);

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    COLORREF dwmBorder = RGB(55, 60, 75);
    DwmSetWindowAttribute(hDlg, 34 /* DWMWA_BORDER_COLOR */, &dwmBorder, sizeof(dwmBorder));

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, FALSE);
    }

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_RETURN)
            {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(state.hEdit, buf, 256);
                state.resultText = buf;
                state.isConfirmed = true;
                if (hParent && IsWindow(hParent))
                {
                    EnableWindow(hParent, TRUE);
                    SetForegroundWindow(hParent);
                    SetActiveWindow(hParent);
                }
                DestroyWindow(hDlg);
                break;
            }
            else if (msg.wParam == VK_ESCAPE)
            {
                state.isConfirmed = false;
                if (hParent && IsWindow(hParent))
                {
                    EnableWindow(hParent, TRUE);
                    SetForegroundWindow(hParent);
                    SetActiveWindow(hParent);
                }
                DestroyWindow(hDlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        SetFocus(hParent);
    }

    if (state.isConfirmed)
    {
        outVal = state.resultText;
        return true;
    }
    return false;
}

static std::wstring GetApplicationDirectory()
{
    wchar_t szExe[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, szExe, MAX_PATH);
    std::wstring path = szExe;
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        return path.substr(0, lastSlash + 1);
    return L"";
}

static bool EqualsI(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static bool PickFolderModern(HWND hWndOwner, const std::wstring& initialFolder, std::wstring& outFolder)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* pDlg = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&pDlg);
    if (SUCCEEDED(hr))
    {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pDlg->GetOptions(&dwOptions)))
        {
            pDlg->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        if (!initialFolder.empty())
        {
            IShellItem* pItem = NULL;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), NULL, IID_PPV_ARGS(&pItem))))
            {
                pDlg->SetFolder(pItem);
                pItem->Release();
            }
        }
        pDlg->SetTitle(L"Select Target Sound Directory");
        if (SUCCEEDED(pDlg->Show(hWndOwner)))
        {
            IShellItem* pResult = NULL;
            if (SUCCEEDED(pDlg->GetResult(&pResult)))
            {
                PWSTR pszPath = NULL;
                if (SUCCEEDED(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                {
                    outFolder = pszPath;
                    CoTaskMemFree(pszPath);
                    pResult->Release();
                    pDlg->Release();
                    return true;
                }
                pResult->Release();
            }
        }
        pDlg->Release();
    }
    return false;
}

struct SmsNewModalState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    int targetMode = 0; // 0 = Sound Studio Workspace (App Directory), 1 = TRAINSET Directory (In-Game Simulator)
    std::wstring basePath;
    std::wstring appDir;

    HWND hEditSubfolder = NULL;
    HWND hEditFileName = NULL;

    std::wstring subfolderText;
    std::wstring fileNameText;
    std::wstring resultFullPath;
    bool isConfirmed = false;

    RECT rcModeWorkspace = { 0 };
    RECT rcModeTrainset = { 0 };
    RECT rcBrowseBtn = { 0 };
    RECT rcNewFolderBtn = { 0 };
    RECT rcOkBtn = { 0 };
    RECT rcCancelBtn = { 0 };
    RECT rcCloseBtn = { 0 };

    bool isHoverBrowse = false;
    bool isHoverNewFolder = false;
    bool isHoverOk = false;
    bool isHoverCancel = false;
    bool isHoverClose = false;

    HFONT hFontTitle = NULL;
    HFONT hFontPrompt = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontSub = NULL;
    HFONT hFontIcon = NULL;

    std::wstring GetBaseRoot() const {
        if (targetMode == 0) {
            return appDir + L"SoundStudio\\CustomSMS\\";
        } else {
            std::wstring root = basePath;
            if (!root.empty() && root.back() != L'\\') root += L'\\';
            root += L"TRAINS\\TRAINSET\\";
            return root;
        }
    }

    std::wstring GetResolvedDestination() const {
        std::wstring base = GetBaseRoot();
        std::wstring sub;
        if (hEditSubfolder && IsWindow(hEditSubfolder)) {
            wchar_t buf[512] = { 0 };
            GetWindowTextW(hEditSubfolder, buf, 512);
            sub = buf;
        } else {
            sub = subfolderText;
        }
        while (!sub.empty() && (sub.front() == L'\\' || sub.front() == L'/')) sub.erase(0, 1);
        if (!sub.empty() && sub.back() != L'\\') sub += L'\\';
        std::wstring fn;
        if (hEditFileName && IsWindow(hEditFileName)) {
            wchar_t buf[256] = { 0 };
            GetWindowTextW(hEditFileName, buf, 256);
            fn = buf;
        } else {
            fn = fileNameText;
        }
        if (fn.empty()) fn = L"New_Soundset.sms";
        if (fn.size() < 4 || _wcsicmp(fn.substr(fn.size() - 4).c_str(), L".sms") != 0) {
            fn += L".sms";
        }
        return base + sub + fn;
    }
};

static LRESULT CALLBACK SmsNewFileModalProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SmsNewModalState* pState = (SmsNewModalState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (SmsNewModalState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    case WM_CREATE:
    {
        pState->hFontTitle  = CreateSmsCustomFont(15, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontPrompt = CreateSmsCustomFont(10, FW_NORMAL, L"Segoe UI");
        pState->hFontMain   = CreateSmsCustomFont(11, FW_NORMAL, L"Segoe UI");
        pState->hFontBold   = CreateSmsCustomFont(11, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontSub    = CreateSmsCustomFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontIcon   = CreateSmsCustomFont(11, FW_NORMAL, L"Segoe Fluent Icons");
        if (!pState->hFontIcon)
            pState->hFontIcon = CreateSmsCustomFont(11, FW_NORMAL, L"Segoe MDL2 Assets");

        pState->subfolderText = (pState->targetMode == 0) ? L"Projects\\CustomSound" : L"common.sound\\CustomSound";
        pState->fileNameText  = L"New_Soundset.sms";

        // Box 1 visual is: { 32, 208, 560, 252 } (height 44)
        // Edit control height 24, vertically centered: y = 208 + (44 - 24)/2 = 218.
        // x from 44 to 548 (width 504)
        pState->hEditSubfolder = CreateWindowExW(
            0, L"EDIT", pState->subfolderText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            44, 218, 504, 24, hWnd, (HMENU)201, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEditSubfolder, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEditSubfolder, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));

        // Box 2 visual is: { 32, 298, 848, 342 } (height 44)
        // Edit control height 24, vertically centered: y = 298 + (44 - 24)/2 = 308.
        // x from 44 to 836 (width 792)
        pState->hEditFileName = CreateWindowExW(
            0, L"EDIT", pState->fileNameText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            44, 308, 792, 24, hWnd, (HMENU)202, GetModuleHandleW(NULL), NULL
        );
        SendMessageW(pState->hEditFileName, WM_SETFONT, (WPARAM)pState->hFontMain, TRUE);
        SendMessageW(pState->hEditFileName, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));

        SetFocus(pState->hEditFileName);
        return 0;
    }
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == EN_CHANGE && pState)
        {
            if (LOWORD(wParam) == 201)
            {
                wchar_t buf[512] = { 0 };
                GetWindowTextW(pState->hEditSubfolder, buf, 512);
                pState->subfolderText = buf;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            else if (LOWORD(wParam) == 202)
            {
                wchar_t buf[256] = { 0 };
                GetWindowTextW(pState->hEditFileName, buf, 256);
                pState->fileNameText = buf;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_NCHITTEST:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        if (PtInRect(&pState->rcOkBtn, pt) ||
            PtInRect(&pState->rcCancelBtn, pt) ||
            PtInRect(&pState->rcCloseBtn, pt) ||
            PtInRect(&pState->rcBrowseBtn, pt) ||
            PtInRect(&pState->rcNewFolderBtn, pt) ||
            PtInRect(&pState->rcModeWorkspace, pt) ||
            PtInRect(&pState->rcModeTrainset, pt) ||
            (pt.x >= 32 && pt.x <= 560 && pt.y >= 208 && pt.y <= 252) ||
            (pt.x >= 32 && pt.x <= 848 && pt.y >= 298 && pt.y <= 342))
        {
            return HTCLIENT;
        }

        if (pt.y < 56)
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(245, 248, 255));
        SetBkColor(hdc, RGB(26, 29, 38));
        static HBRUSH hBrEdit = CreateSolidBrush(RGB(26, 29, 38));
        return (LRESULT)hBrEdit;
    }
    case WM_MOUSEMOVE:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            bool hovB  = PtInRect(&pState->rcBrowseBtn, pt);
            bool hovNf = PtInRect(&pState->rcNewFolderBtn, pt);
            bool hovOk = PtInRect(&pState->rcOkBtn, pt);
            bool hovCa = PtInRect(&pState->rcCancelBtn, pt);
            bool hovCl = PtInRect(&pState->rcCloseBtn, pt);

            if (hovB != pState->isHoverBrowse || hovNf != pState->isHoverNewFolder ||
                hovOk != pState->isHoverOk || hovCa != pState->isHoverCancel || hovCl != pState->isHoverClose)
            {
                pState->isHoverBrowse = hovB;
                pState->isHoverNewFolder = hovNf;
                pState->isHoverOk = hovOk;
                pState->isHoverCancel = hovCa;
                pState->isHoverClose = hovCl;
                InvalidateRect(hWnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            pState->isHoverBrowse = false;
            pState->isHoverNewFolder = false;
            pState->isHoverOk = false;
            pState->isHoverCancel = false;
            pState->isHoverClose = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&pState->rcModeWorkspace, pt))
        {
            pState->targetMode = 0;
            pState->subfolderText = L"Projects\\CustomSound";
            SetWindowTextW(pState->hEditSubfolder, pState->subfolderText.c_str());
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcModeTrainset, pt))
        {
            pState->targetMode = 1;
            pState->subfolderText = L"common.sound\\CustomSound";
            SetWindowTextW(pState->hEditSubfolder, pState->subfolderText.c_str());
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBrowseBtn, pt))
        {
            std::wstring initialFolder = pState->GetBaseRoot();
            std::wstring pickedFolder;
            if (PickFolderModern(hWnd, initialFolder, pickedFolder))
            {
                std::wstring base = pState->GetBaseRoot();
                std::wstring rel = pickedFolder;
                if (_wcsnicmp(pickedFolder.c_str(), base.c_str(), base.size()) == 0)
                {
                    rel = pickedFolder.substr(base.size());
                }
                SetWindowTextW(pState->hEditSubfolder, rel.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        if (PtInRect(&pState->rcNewFolderBtn, pt))
        {
            std::wstring newFolder;
            if (PromptSmsModalText(hWnd, L"New Subfolder", L"Enter name for the new subfolder (e.g. MySound or Electric\\Cab):", L"My_Sound", newFolder))
            {
                if (!newFolder.empty())
                {
                    wchar_t cur[512] = { 0 };
                    GetWindowTextW(pState->hEditSubfolder, cur, 512);
                    std::wstring sCur = cur;
                    while (!sCur.empty() && sCur.back() == L'\\') sCur.pop_back();
                    if (!sCur.empty()) sCur += L"\\";
                    sCur += newFolder;
                    SetWindowTextW(pState->hEditSubfolder, sCur.c_str());
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            return 0;
        }
        if (PtInRect(&pState->rcCloseBtn, pt) || PtInRect(&pState->rcCancelBtn, pt))
        {
            pState->isConfirmed = false;
            if (pState->hParent && IsWindow(pState->hParent))
            {
                EnableWindow(pState->hParent, TRUE);
                SetForegroundWindow(pState->hParent);
                SetActiveWindow(pState->hParent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
        if (PtInRect(&pState->rcOkBtn, pt))
        {
            wchar_t subBuf[512] = { 0 };
            wchar_t fnBuf[256] = { 0 };
            GetWindowTextW(pState->hEditSubfolder, subBuf, 512);
            GetWindowTextW(pState->hEditFileName, fnBuf, 256);
            pState->subfolderText = subBuf;
            pState->fileNameText = fnBuf;

            std::wstring fullPath = pState->GetResolvedDestination();
            size_t lastSlash = fullPath.find_last_of(L"\\/");
            std::wstring targetDir = (lastSlash != std::wstring::npos) ? fullPath.substr(0, lastSlash) : L"";

            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            if (std::filesystem::exists(fullPath))
            {
                if (ShowModernMessageBox(hWnd, L"A sound file with this name already exists in this folder!\nDo you want to overwrite it?", L"File Exists", MB_YESNO | MB_ICONWARNING) != IDYES)
                {
                    return 0;
                }
            }

            SmsDocument starterDoc;
            starterDoc.filePath = fullPath;
            starterDoc.encoding = SmsFileEncoding::Utf16LeBom;
            starterDoc.headerComments.push_back(L"Created with Train Sim Consist Builder - Sound Studio");

            SmsScalabilityGroup sg5;
            sg5.groupLevel = 5;
            sg5.hasExternalCam = true;
            sg5.activationDistance = 900.0f;
            sg5.deactivationDistance = 900.0f;
            sg5.groupVolume = 1.0f;
            sg5.hasGroupVolume = true;
            sg5.declaredStreams = 1;

            SmsStream st1;
            st1.streamIndex = 0;
            st1.priority = 6;
            st1.volume = 1.0f;
            st1.hasVolume = true;
            st1.friendlyTitle = L"Stream 1 (Starter)";
            st1.declaredTriggers = 1;

            SmsTrigger tr1;
            tr1.triggerType = L"Discrete_Trigger";
            tr1.triggerId = 8; // Horn High (Start)
            tr1.actionType = L"PlayOneShot";
            tr1.declaredFiles = 1;
            SmsFileRef f1;
            f1.fileName = L"sample.wav";
            f1.flag = -1;
            tr1.files.push_back(f1);
            tr1.selectionMethod = L"SequentialSelection";
            tr1.isCollapsed = false;

            st1.triggers.push_back(tr1);
            sg5.streams.push_back(st1);
            starterDoc.groups.push_back(sg5);

            if (SmsParser_SaveToFile(starterDoc, fullPath))
            {
                pState->resultFullPath = fullPath;
                pState->isConfirmed = true;
                if (pState->hParent && IsWindow(pState->hParent))
                {
                    EnableWindow(pState->hParent, TRUE);
                    SetForegroundWindow(pState->hParent);
                    SetActiveWindow(pState->hParent);
                }
                DestroyWindow(hWnd);
            }
            else
            {
                ShowModernMessageBox(hWnd, L"Failed to write the new sound file to the specified location. Check directory permissions.", L"Creation Error", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP hBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

        // Background
        HBRUSH bgBr = CreateSolidBrush(RGB(24, 26, 32));
        FillRect(memDC, &rcClient, bgBr);
        DeleteObject(bgBr);

        // Window border
        HPEN bdrPen = CreatePen(PS_SOLID, 1, RGB(55, 60, 75));
        HGDIOBJ oldPen = SelectObject(memDC, bdrPen);
        HGDIOBJ oldBr = SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBr);
        DeleteObject(bdrPen);

        // Title row
        SelectObject(memDC, pState->hFontTitle);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(245, 248, 255));
        RECT rcTitle = { 24, 14, 500, 38 };
        DrawTextW(memDC, L"➕ Create New Soundset (.SMS)", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Close button
        pState->rcCloseBtn = { rcClient.right - 36, 12, rcClient.right - 12, 36 };
        if (pState->isHoverClose)
        {
            HBRUSH cBr = CreateSolidBrush(RGB(180, 40, 40));
            FillRect(memDC, &pState->rcCloseBtn, cBr);
            DeleteObject(cBr);
        }
        SelectObject(memDC, pState->hFontMain);
        SetTextColor(memDC, pState->isHoverClose ? RGB(255, 255, 255) : RGB(160, 170, 185));
        DrawTextW(memDC, L"✕", -1, &pState->rcCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Divider
        HPEN divPen = CreatePen(PS_SOLID, 1, RGB(44, 48, 60));
        oldPen = SelectObject(memDC, divPen);
        MoveToEx(memDC, 24, 46, NULL);
        LineTo(memDC, rcClient.right - 24, 46);
        SelectObject(memDC, oldPen);
        DeleteObject(divPen);

        // Mode selection
        pState->rcModeWorkspace = { 32, 68, 430, 140 };
        pState->rcModeTrainset  = { 450, 68, 848, 140 };

        auto drawModePill = [&](const RECT& rc, bool active, const wchar_t* title, const wchar_t* subtitle) {
            HBRUSH mBr = CreateSolidBrush(active ? RGB(18, 44, 72) : RGB(28, 31, 40));
            HPEN mPen = CreatePen(PS_SOLID, 1, active ? RGB(52, 148, 250) : RGB(46, 52, 68));
            SelectObject(memDC, mBr); SelectObject(memDC, mPen);
            RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
            DeleteObject(mBr); DeleteObject(mPen);

            SelectObject(memDC, active ? pState->hFontBold : pState->hFontMain);
            SetTextColor(memDC, active ? RGB(160, 225, 255) : RGB(200, 212, 228));
            RECT rcT1 = { rc.left + 16, rc.top + 10, rc.right - 16, rc.top + 32 };
            std::wstring tStr = (active ? L"✓ " : L"") + std::wstring(title);
            DrawTextW(memDC, tStr.c_str(), -1, &rcT1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, pState->hFontPrompt);
            SetTextColor(memDC, active ? RGB(120, 180, 235) : RGB(140, 150, 168));
            RECT rcT2 = { rc.left + 16, rc.top + 34, rc.right - 16, rc.bottom - 8 };
            DrawTextW(memDC, subtitle, -1, &rcT2, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        };

        drawModePill(pState->rcModeWorkspace, pState->targetMode == 0, L"Sound Studio Workspace (App Directory)", L"Self-contained workspace in app folder — ideal for isolated building & testing");
        drawModePill(pState->rcModeTrainset, pState->targetMode == 1, L"TRAINSET Directory (In-Game Simulator)", L"Standard simulator tree in TRAINS\\TRAINSET — ready for immediate use in activities");

        // Subfolder section
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, RGB(220, 230, 245));
        RECT rcLblSub = { 32, 154, 848, 176 };
        DrawTextW(memDC, L"Subfolder Hierarchy (Multi-level custom folders auto-created):", -1, &rcLblSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(memDC, pState->hFontPrompt);
        SetTextColor(memDC, RGB(135, 155, 185));
        RECT rcLblBase = { 32, 178, 848, 200 };
        std::wstring baseStr = L"Base Root: " + pState->GetBaseRoot();
        DrawTextW(memDC, baseStr.c_str(), -1, &rcLblBase, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Edit box background rect (44px tall, matches hEditSubfolder vertically centered inside)
        RECT rcBox1 = { 32, 208, 560, 252 };
        HBRUSH ebBr1 = CreateSolidBrush(RGB(26, 29, 38));
        HPEN ebPen1 = CreatePen(PS_SOLID, 1, RGB(52, 60, 78));
        SelectObject(memDC, ebBr1); SelectObject(memDC, ebPen1);
        RoundRect(memDC, rcBox1.left, rcBox1.top, rcBox1.right, rcBox1.bottom, 4, 4);
        DeleteObject(ebBr1); DeleteObject(ebPen1);

        // Browse button
        pState->rcBrowseBtn = { 575, 208, 700, 252 };
        HBRUSH brB = CreateSolidBrush(pState->isHoverBrowse ? RGB(45, 54, 74) : RGB(32, 38, 52));
        HPEN brP = CreatePen(PS_SOLID, 1, pState->isHoverBrowse ? RGB(85, 160, 250) : RGB(60, 75, 105));
        SelectObject(memDC, brB); SelectObject(memDC, brP);
        RoundRect(memDC, pState->rcBrowseBtn.left, pState->rcBrowseBtn.top, pState->rcBrowseBtn.right, pState->rcBrowseBtn.bottom, 4, 4);
        DeleteObject(brB); DeleteObject(brP);
        SelectObject(memDC, pState->hFontMain);
        SetTextColor(memDC, pState->isHoverBrowse ? RGB(255, 255, 255) : RGB(210, 225, 245));
        DrawTextW(memDC, L"📂 Browse...", -1, &pState->rcBrowseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // New folder button
        pState->rcNewFolderBtn = { 715, 208, 848, 252 };
        HBRUSH nfB = CreateSolidBrush(pState->isHoverNewFolder ? RGB(26, 68, 48) : RGB(20, 52, 36));
        HPEN nfP = CreatePen(PS_SOLID, 1, pState->isHoverNewFolder ? RGB(65, 195, 120) : RGB(45, 135, 85));
        SelectObject(memDC, nfB); SelectObject(memDC, nfP);
        RoundRect(memDC, pState->rcNewFolderBtn.left, pState->rcNewFolderBtn.top, pState->rcNewFolderBtn.right, pState->rcNewFolderBtn.bottom, 4, 4);
        DeleteObject(nfB); DeleteObject(nfP);
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, pState->isHoverNewFolder ? RGB(255, 255, 255) : RGB(140, 245, 180));
        DrawTextW(memDC, L"📁+ New Folder", -1, &pState->rcNewFolderBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // File name section
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, RGB(220, 230, 245));
        RECT rcLblFn = { 32, 268, 848, 290 };
        DrawTextW(memDC, L"SMS File Name (.sms):", -1, &rcLblFn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Edit box background rect (44px tall, matches hEditFileName vertically centered inside)
        RECT rcBox2 = { 32, 298, 848, 342 };
        HBRUSH ebBr2 = CreateSolidBrush(RGB(26, 29, 38));
        HPEN ebPen2 = CreatePen(PS_SOLID, 1, RGB(52, 60, 78));
        SelectObject(memDC, ebBr2); SelectObject(memDC, ebPen2);
        RoundRect(memDC, rcBox2.left, rcBox2.top, rcBox2.right, rcBox2.bottom, 4, 4);
        DeleteObject(ebBr2); DeleteObject(ebPen2);

        // Resolved path preview box
        RECT rcDestBox = { 32, 360, 848, 492 };
        HBRUSH dbB = CreateSolidBrush(RGB(15, 17, 22));
        HPEN dbP = CreatePen(PS_SOLID, 1, RGB(42, 48, 64));
        SelectObject(memDC, dbB); SelectObject(memDC, dbP);
        RoundRect(memDC, rcDestBox.left, rcDestBox.top, rcDestBox.right, rcDestBox.bottom, 4, 4);
        DeleteObject(dbB); DeleteObject(dbP);

        SelectObject(memDC, pState->hFontPrompt);
        SetTextColor(memDC, RGB(95, 170, 245));
        RECT rcDbLbl = { rcDestBox.left + 14, rcDestBox.top + 10, rcDestBox.right - 14, rcDestBox.top + 32 };
        DrawTextW(memDC, L"Resolved Output Destination (Intermediate folders auto-created):", -1, &rcDbLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(memDC, pState->hFontMain);
        SetTextColor(memDC, RGB(230, 240, 255));
        RECT rcDbTxt = { rcDestBox.left + 14, rcDestBox.top + 38, rcDestBox.right - 14, rcDestBox.bottom - 10 };
        std::wstring destPath = pState->GetResolvedDestination();
        DrawTextW(memDC, destPath.c_str(), -1, &rcDbTxt, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        // OK Button
        pState->rcOkBtn = { 540, 520, 735, 568 };
        HBRUSH okB = CreateSolidBrush(pState->isHoverOk ? RGB(28, 95, 155) : RGB(22, 72, 122));
        HPEN okP = CreatePen(PS_SOLID, 1, pState->isHoverOk ? RGB(85, 195, 255) : RGB(50, 140, 230));
        SelectObject(memDC, okB); SelectObject(memDC, okP);
        RoundRect(memDC, pState->rcOkBtn.left, pState->rcOkBtn.top, pState->rcOkBtn.right, pState->rcOkBtn.bottom, 4, 4);
        DeleteObject(okB); DeleteObject(okP);
        SelectObject(memDC, pState->hFontBold);
        SetTextColor(memDC, RGB(255, 255, 255));
        DrawTextW(memDC, L"🚀 Create & Open SMS", -1, &pState->rcOkBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Cancel Button
        pState->rcCancelBtn = { 750, 520, 848, 568 };
        HBRUSH canB = CreateSolidBrush(pState->isHoverCancel ? RGB(52, 58, 74) : RGB(36, 40, 52));
        HPEN canP = CreatePen(PS_SOLID, 1, pState->isHoverCancel ? RGB(95, 108, 135) : RGB(60, 68, 86));
        SelectObject(memDC, canB); SelectObject(memDC, canP);
        RoundRect(memDC, pState->rcCancelBtn.left, pState->rcCancelBtn.top, pState->rcCancelBtn.right, pState->rcCancelBtn.bottom, 4, 4);
        DeleteObject(canB); DeleteObject(canP);
        SelectObject(memDC, pState->hFontMain);
        SetTextColor(memDC, pState->isHoverCancel ? RGB(255, 255, 255) : RGB(190, 200, 215));
        DrawTextW(memDC, L"Cancel", -1, &pState->rcCancelBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontPrompt) DeleteObject(pState->hFontPrompt);
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontSub) DeleteObject(pState->hFontSub);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool PromptSmsNewFileModal(HWND hParent, const std::wstring& basePath, std::wstring& outCreatedPath)
{
    static bool s_reg = false;
    const wchar_t* szClassName = L"TSCB_SmsNewFileModal";
    if (!s_reg)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SmsNewFileModalProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(20, 22, 28));
        wc.lpszClassName = szClassName;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    SmsNewModalState state;
    state.hParent = hParent;
    state.basePath = basePath;
    state.appDir = GetApplicationDirectory();

    int w = 880, h = 600;
    HMONITOR hMon = MonitorFromWindow(hParent ? hParent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT rcWork;
    if (GetMonitorInfoW(hMon, &mi)) {
        rcWork = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    }

    int x = 0, y = 0;
    if (hParent && IsWindow(hParent)) {
        RECT rcParent;
        GetWindowRect(hParent, &rcParent);
        x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
        y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;
    } else {
        x = rcWork.left + (rcWork.right - rcWork.left - w) / 2;
        y = rcWork.top + (rcWork.bottom - rcWork.top - h) / 2;
    }

    // Ensure completely within work area boundaries
    if (x + w > rcWork.right) x = rcWork.right - w;
    if (y + h > rcWork.bottom) y = rcWork.bottom - h;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClassName, L"Create New Soundset",
        WS_POPUP | WS_VISIBLE, x, y, w, h,
        hParent, NULL, GetModuleHandleW(NULL), &state);

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, FALSE);
    }

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_ESCAPE)
            {
                state.isConfirmed = false;
                if (hParent && IsWindow(hParent))
                {
                    EnableWindow(hParent, TRUE);
                    SetForegroundWindow(hParent);
                    SetActiveWindow(hParent);
                }
                DestroyWindow(hDlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        SetFocus(hParent);
    }

    if (state.isConfirmed)
    {
        outCreatedPath = state.resultFullPath;
        return true;
    }
    return false;
}

static std::wstring GetTrackTypeLabel(int trackType)
{
    switch (trackType)
    {
    case -1: return L"None (Standard Rolling Stock)";
    case 0: return L"0: Default Track";
    case 1: return L"1: Concrete Supported";
    case 2: return L"2: Wood Supported";
    case 3: return L"3: In Tunnel";
    case 4: return L"4: Steel Bridge";
    case 5: return L"5: Girder Bridge";
    case 6: return L"6: Under Bridge";
    case 7: return L"7: Concrete Bridge";
    case 8: return L"8: Wooden Trestle Bridge";
    case 9: return L"9: Switches & Turnouts (Open Rails)";
    case 10: return L"10: Curve Squeal (Open Rails)";
    default:
        return L"Custom Track (" + std::to_wstring(trackType) + L")";
    }
}

struct ClickableRegion {
    enum Type {
        SgTab,
        FixCountBtn,
        NewSmsBtn,
        SaveBtn,
        ConsolidateWavsBtn,
        OpenFileBtn,
        OpenFolderBtn,
        CollapseAllBtn,
        ExpandAllBtn,
        CopyStreamsBtn,
        PasteStreamsBtn,
        SelectAllStreamsBtn,
        StreamMasterCheck,
        StreamSelectCheck,
        StreamToggle,
        StreamPlayLoop,
        WavPlay,
        VolumeSlider,
        PriorityUp,
        PriorityDown,
        // Scalability Group Properties & Activation Card
        SgPropsCardToggle,
        SgLevelBtn,
        SgMasterVolToggle,
        SgMasterVolBtn,
        SgStereoToggle,
        SgActCamCab,
        SgActCamPass,
        SgActCamExt,
        SgActDistBtn,
        SgTrackTypeBtn,
        SgDeactDistBtn,
        SgDeactCamCab,
        SgDeactCamPass,
        SgDeactCamExt,
        SgDeactTrackTypeBtn,
        // Stream In-Card Controls
        StreamEditTitleBtn,
        StreamPriorityDown,
        StreamPriorityUp,
        StreamPriorityVal,
        StreamVolumeToggle,
        StreamVolumeDown,
        StreamVolumeUp,
        StreamVolumeVal,
        StreamStereoToggle,
        StreamDeleteBtn,
        StreamAddBtn,
        StreamAddTriggerBtn,
        StreamAddCurveBtn,
        // Trigger Sub-Card Controls
        TriggerToggle,
        TriggerDeleteBtn,
        TriggerTypeBtn,
        TriggerActionBtn,
        TriggerActionParamBtn,
        TriggerParamBtn,
        TriggerSelMethodBtn,
        TriggerAddWavBtn,
        TriggerWavEditBtn,
        TriggerWavDeleteBtn,
        // Curve Sub-Card Controls
        CurveToggle,
        CurveDeleteBtn,
        CurveTypeBtn,
        CurveControlBtn,
        CurveGranularityBtn,
        CurveAddPointBtn,
        CurvePointEditBtn,
        CurvePointDeleteBtn
    } type;
    RECT rect;
    int index1 = 0; // stream index or sg index or tree index
    int index2 = 0; // trigger index or curve index
    std::wstring strParam;
    int index3 = 0; // wav index or point index
};

struct SmsEditorHistoryStep {
    std::wstring actionDesc;
    std::vector<SmsScalabilityGroup> groups;
    int activeGroupIndex;
};

struct SmsEditorViewState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HINSTANCE hInstance = NULL;

    SmsDocument doc;
    std::vector<SmsScalabilityGroup> savedGroups;
    std::vector<SmsEditorHistoryStep> undoStack;
    std::vector<SmsEditorHistoryStep> redoStack;
    std::wstring basePath;
    std::vector<ContextTreeItem> treeItems;
    int selectedTreeIdx = -1;

    // Layout metrics
    int sidebarWidth = 260;
    int headerHeight = 96;

    // Scrollbar for streams canvas
    CustomScrollBar vScrollBar;
    int scrollY = 0;
    int totalContentHeight = 0;

    // Sidebar scroll
    CustomScrollBar treeScrollBar;
    int treeScrollY = 0;
    int totalTreeHeight = 0;

    // Clickable hit targets
    std::vector<ClickableRegion> hitRegions;
    std::vector<std::pair<RECT, int>> treeHitRegions;

    // Hover state
    POINT ptMouse = { -1, -1 };
    int hoverRegionIdx = -1;
    bool isTrackingMouse = false;
    int lastSelectedStreamIndex = -1;

    HFONT hFontTitle = NULL;
    HFONT hFontSubtitle = NULL;
    HFONT hFontBody = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontMono = NULL;
    HFONT hFontIcons = NULL;

    BOOL bDarkMode = TRUE;

    // Drag and Drop stream reordering
    bool isDraggingStreams = false;
    bool isPotentialDrag = false;
    POINT ptDragStart = { 0, 0 };
    int draggedStreamIndex = -1;
    std::vector<int> draggedIndices;
    int dropTargetIndex = -1;
    std::vector<RECT> streamCardRects;
    RECT rcStreamsCard = { 0 };
    RECT rcStreamsViewport = { 0 };
};

static void RebuildContextTree(SmsEditorViewState* pState, const std::vector<RSSUnitSoundInfo>& units, const std::wstring& activeFile)
{
    pState->treeItems.clear();

    if (!units.empty())
    {
        pState->treeItems.push_back({ ContextTreeItem::Header, L"CONSIST SOUNDSETS", L"", L"", true, false, true });

        for (size_t u = 0; u < units.size(); ++u)
        {
            const auto& unit = units[u];
            ContextTreeItem unitNode;
            unitNode.type = ContextTreeItem::Unit;
            unitNode.label = L"#" + std::to_wstring(u + 1) + L" " + unit.name;
            unitNode.subLabel = unit.type;
            unitNode.isExpanded = true;
            pState->treeItems.push_back(unitNode);

            auto addSlot = [&](const std::wstring& slotType, const std::vector<RSSSoundEntry>& entries) {
                for (const auto& e : entries)
                {
                    ContextTreeItem slot;
                    slot.type = ContextTreeItem::SoundSlot;
                    slot.label = slotType + L": " + e.smsFileName;
                    slot.subLabel = e.parentFolder;
                    slot.filePath = e.fullDiskPath;
                    slot.exists = e.existsOnDisk;
                    slot.isSelected = (!activeFile.empty() && _wcsicmp(activeFile.c_str(), e.fullDiskPath.c_str()) == 0);
                    pState->treeItems.push_back(slot);
                }
            };

            addSlot(L"Wagon Sound", unit.wagonSounds);
            addSlot(L"Cab Sound", unit.engineSounds);
            addSlot(L"3D Cab Sound", unit.orts3dCabSounds);
            addSlot(L"Inside Sound", unit.insideSounds);
        }
    }

    if (!activeFile.empty())
    {
        bool found = false;
        for (const auto& it : pState->treeItems)
        {
            if (_wcsicmp(it.filePath.c_str(), activeFile.c_str()) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            pState->treeItems.push_back({ ContextTreeItem::Header, L"DIRECT SOUND FILE", L"", L"", true, false, true });
            size_t slash = activeFile.find_last_of(L"\\/");
            std::wstring fName = (slash != std::wstring::npos) ? activeFile.substr(slash + 1) : activeFile;
            pState->treeItems.push_back({ ContextTreeItem::Standalone, fName, activeFile, activeFile, true, true, (GetFileAttributesW(activeFile.c_str()) != INVALID_FILE_ATTRIBUTES) });
        }
    }
}

static void Sms_NotifyDocModified(SmsEditorViewState* pState)
{
    if (!pState) return;
    pState->doc.isModified = (pState->doc.groups != pState->savedGroups);
    if (pState->hWnd) InvalidateRect(pState->hWnd, NULL, FALSE);
}

static void LoadFileIntoEditor(SmsEditorViewState* pState, const std::wstring& path)
{
    StopAudio();
    if (path.empty()) return;

    if (SmsParser_LoadFromFile(path, pState->basePath, pState->doc))
    {
        pState->savedGroups = pState->doc.groups;
        pState->doc.isModified = false;
        pState->undoStack.clear();
        pState->redoStack.clear();

        // By default, keep all streams collapsed upon loading
        for (auto& grp : pState->doc.groups)
        {
            for (auto& st : grp.streams)
            {
                st.isCollapsed = true;
            }
        }
        pState->isDraggingStreams = false;
        pState->isPotentialDrag = false;
        pState->dropTargetIndex = -1;
        pState->scrollY = 0;
        pState->vScrollBar.SetPos(0);
        for (auto& item : pState->treeItems)
        {
            item.isSelected = (_wcsicmp(item.filePath.c_str(), path.c_str()) == 0);
        }
        InvalidateRect(pState->hWnd, NULL, FALSE);
    }
    else
    {
        ShowModernMessageBox(pState->hWnd, (L"Failed to load or parse sound management file:\r\n\r\n" + path).c_str(), L"SMS Parse Error", MB_OK | MB_ICONERROR);
    }
}

static void Sms_PushUndo(SmsEditorViewState* pState, const std::wstring& actionDesc);
static void Sms_PerformUndo(SmsEditorViewState* pState);
static void Sms_PerformRedo(SmsEditorViewState* pState);

static void CopySelectedStreams(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;

    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
    s_streamClipboard.sourceSmsPath = pState->doc.filePath;
    s_streamClipboard.streams.clear();

    for (const auto& st : grp.streams)
    {
        if (st.isSelected)
        {
            SmsClipboardStream cItem;
            cItem.stream = st;
            cItem.stream.isSelected = false;

            cItem.resolvedWavPaths.resize(st.triggers.size());
            for (size_t t = 0; t < st.triggers.size(); ++t)
            {
                cItem.resolvedWavPaths[t].resize(st.triggers[t].files.size());
                for (size_t f = 0; f < st.triggers[t].files.size(); ++f)
                {
                    cItem.resolvedWavPaths[t][f] = st.triggers[t].files[f].resolvedDiskPath;
                }
            }
            s_streamClipboard.streams.push_back(cItem);
        }
    }
}

static void PasteClipboardStreams(SmsEditorViewState* pState)
{
    if (!pState || s_streamClipboard.streams.empty() ||
        pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;

    Sms_PushUndo(pState, L"Paste Streams");
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

    for (const auto& cItem : s_streamClipboard.streams)
    {
        SmsStream newStream = cItem.stream;
        newStream.streamIndex = (int)grp.streams.size() + 1;
        newStream.isCollapsed = true;
        newStream.isPlayingPreview = false;
        newStream.isSelected = false;

        // Smart Retargeting for every trigger file
        for (size_t t = 0; t < newStream.triggers.size(); ++t)
        {
            auto& trig = newStream.triggers[t];
            for (size_t f = 0; f < trig.files.size(); ++f)
            {
                auto& fileRef = trig.files[f];
                std::wstring resolvedPath;
                if (t < cItem.resolvedWavPaths.size() && f < cItem.resolvedWavPaths[t].size())
                {
                    resolvedPath = cItem.resolvedWavPaths[t][f];
                }

                if (!resolvedPath.empty())
                {
                    // Retarget relative path from target SMS directory
                    fileRef.fileName = SmsParser_RetargetWavRelativePath(pState->doc.filePath, resolvedPath, pState->basePath);
                    fileRef.resolvedDiskPath = SmsParser_ResolveWavPath(pState->doc.filePath, fileRef.fileName, pState->basePath);
                    fileRef.existsOnDisk = (!fileRef.resolvedDiskPath.empty() && GetFileAttributesW(fileRef.resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                }
                else
                {
                    fileRef.resolvedDiskPath = SmsParser_ResolveWavPath(pState->doc.filePath, fileRef.fileName, pState->basePath);
                    fileRef.existsOnDisk = (!fileRef.resolvedDiskPath.empty() && GetFileAttributesW(fileRef.resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                }
            }
        }

        grp.streams.push_back(newStream);
    }

    // Auto-synchronize declaredStreams count
    grp.declaredStreams = (int)grp.streams.size();
    Sms_NotifyDocModified(pState);
}

static void SelectAllStreams(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;

    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
    bool allSel = true;
    for (const auto& s : grp.streams) if (!s.isSelected) { allSel = false; break; }
    for (auto& s : grp.streams) s.isSelected = !allSel;
}

static void ReorderStreams(std::vector<SmsStream>& streams, const std::vector<int>& selectedIndices, int targetInsertPos)
{
    if (selectedIndices.empty() || targetInsertPos < 0 || targetInsertPos > (int)streams.size())
        return;

    std::vector<bool> isSel(streams.size(), false);
    for (int idx : selectedIndices)
    {
        if (idx >= 0 && idx < (int)streams.size()) isSel[idx] = true;
    }

    std::vector<SmsStream> movingStreams;
    std::vector<SmsStream> remainingStreams;

    int adjustedTarget = 0;
    for (int i = 0; i < (int)streams.size(); ++i)
    {
        if (i < targetInsertPos && !isSel[i])
        {
            adjustedTarget++;
        }
        if (isSel[i])
        {
            movingStreams.push_back(streams[i]);
        }
        else
        {
            remainingStreams.push_back(streams[i]);
        }
    }

    remainingStreams.insert(remainingStreams.begin() + adjustedTarget, movingStreams.begin(), movingStreams.end());

    for (size_t i = 0; i < remainingStreams.size(); ++i)
    {
        remainingStreams[i].streamIndex = (int)i + 1;
    }

    streams = std::move(remainingStreams);
}

static void MoveSelectedStreamsUp(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
    if (grp.streams.empty() || grp.streams[0].isSelected) return;
    Sms_PushUndo(pState, L"Move Stream Up");

    for (size_t i = 1; i < grp.streams.size(); ++i)
    {
        if (grp.streams[i].isSelected && !grp.streams[i - 1].isSelected)
        {
            std::swap(grp.streams[i], grp.streams[i - 1]);
        }
    }
    for (size_t i = 0; i < grp.streams.size(); ++i) grp.streams[i].streamIndex = (int)i + 1;
    Sms_NotifyDocModified(pState);
}

static void MoveSelectedStreamsDown(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
    if (grp.streams.empty() || grp.streams.back().isSelected) return;
    Sms_PushUndo(pState, L"Move Stream Down");

    for (int i = (int)grp.streams.size() - 2; i >= 0; --i)
    {
        if (grp.streams[i].isSelected && !grp.streams[i + 1].isSelected)
        {
            std::swap(grp.streams[i], grp.streams[i + 1]);
        }
    }
    for (size_t i = 0; i < grp.streams.size(); ++i) grp.streams[i].streamIndex = (int)i + 1;
    Sms_NotifyDocModified(pState);
}

static void CutSelectedStreams(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

    CopySelectedStreams(pState);

    StopAudio();
    Sms_PushUndo(pState, L"Delete Stream(s)");
    std::vector<SmsStream> remaining;
    for (const auto& st : grp.streams)
    {
        if (!st.isSelected) remaining.push_back(st);
    }
    for (size_t i = 0; i < remaining.size(); ++i) remaining[i].streamIndex = (int)i + 1;
    grp.streams = std::move(remaining);
    grp.declaredStreams = (int)grp.streams.size();
    Sms_NotifyDocModified(pState);
}

static void DuplicateSelectedStreams(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;
    Sms_PushUndo(pState, L"Duplicate Stream(s)");
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

    std::vector<SmsStream> newStreams;
    for (const auto& st : grp.streams)
    {
        newStreams.push_back(st);
        if (st.isSelected)
        {
            SmsStream dup = st;
            dup.isSelected = true;
            dup.isCollapsed = true;
            dup.isPlayingPreview = false;
            dup.friendlyTitle += L" (Copy)";
            newStreams.push_back(dup);
        }
    }
    for (size_t i = 0; i < newStreams.size(); ++i) newStreams[i].streamIndex = (int)i + 1;
    grp.streams = std::move(newStreams);
    grp.declaredStreams = (int)grp.streams.size();
    Sms_NotifyDocModified(pState);
}

static void Sms_PushUndo(SmsEditorViewState* pState, const std::wstring& actionDesc)
{
    if (!pState) return;
    SmsEditorHistoryStep step;
    step.actionDesc = actionDesc;
    step.groups = pState->doc.groups;
    step.activeGroupIndex = pState->doc.activeGroupIndex;
    pState->undoStack.push_back(step);
    pState->redoStack.clear();
    if (pState->undoStack.size() > 50)
    {
        pState->undoStack.erase(pState->undoStack.begin());
    }
}

static void Sms_PerformUndo(SmsEditorViewState* pState)
{
    if (!pState || pState->undoStack.empty()) return;
    SmsEditorHistoryStep step = pState->undoStack.back();
    pState->undoStack.pop_back();

    SmsEditorHistoryStep redoStep;
    redoStep.actionDesc = step.actionDesc;
    redoStep.groups = pState->doc.groups;
    redoStep.activeGroupIndex = pState->doc.activeGroupIndex;
    pState->redoStack.push_back(redoStep);

    pState->doc.groups = step.groups;
    pState->doc.activeGroupIndex = step.activeGroupIndex;
    Sms_NotifyDocModified(pState);
}

static void Sms_PerformRedo(SmsEditorViewState* pState)
{
    if (!pState || pState->redoStack.empty()) return;
    SmsEditorHistoryStep step = pState->redoStack.back();
    pState->redoStack.pop_back();

    SmsEditorHistoryStep undoStep;
    undoStep.actionDesc = step.actionDesc;
    undoStep.groups = pState->doc.groups;
    undoStep.activeGroupIndex = pState->doc.activeGroupIndex;
    pState->undoStack.push_back(undoStep);

    pState->doc.groups = step.groups;
    pState->doc.activeGroupIndex = step.activeGroupIndex;
    Sms_NotifyDocModified(pState);
}

static void DeleteSelectedStreams(SmsEditorViewState* pState)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;
    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

    int count = 0;
    for (const auto& s : grp.streams) if (s.isSelected) ++count;
    if (count == 0) return;

    std::wstring msg = L"Are you sure you want to delete " + std::to_wstring(count) + L" selected stream" + (count > 1 ? L"s" : L"") + L"?";
    if (ShowModernMessageBox(pState->hWnd, msg.c_str(), L"Delete Stream(s)", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    StopAudio();
    Sms_PushUndo(pState, L"Delete Stream(s)");
    std::vector<SmsStream> remaining;
    for (const auto& st : grp.streams)
    {
        if (!st.isSelected) remaining.push_back(st);
    }
    for (size_t i = 0; i < remaining.size(); ++i) remaining[i].streamIndex = (int)i + 1;
    grp.streams = std::move(remaining);
    grp.declaredStreams = (int)grp.streams.size();
    Sms_NotifyDocModified(pState);
}

enum StreamMenuCmd {
    CmdUndo = 999,
    CmdRedo = 1000,
    CmdCut = 1001,
    CmdCopy,
    CmdPaste,
    CmdDelete,
    CmdDuplicate,
    CmdMoveUp,
    CmdMoveDown,
    CmdSelectAll,
    CmdDeselectAll,
    CmdInvertSelection,
    CmdToggleExpandCollapse,
    CmdPlayPreview,
    CmdConsolidateWavs
};

static void ShowStreamContextMenu(SmsEditorViewState* pState, int screenX, int screenY, int clickedStreamIdx)
{
    if (!pState || pState->doc.activeGroupIndex < 0 || pState->doc.activeGroupIndex >= (int)pState->doc.groups.size())
        return;

    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

    // If clicked on an unselected stream, make it the sole selection
    if (clickedStreamIdx >= 0 && clickedStreamIdx < (int)grp.streams.size())
    {
        if (!grp.streams[clickedStreamIdx].isSelected)
        {
            for (auto& s : grp.streams) s.isSelected = false;
            grp.streams[clickedStreamIdx].isSelected = true;
            InvalidateRect(pState->hWnd, NULL, FALSE);
        }
    }

    int selCount = 0;
    for (const auto& s : grp.streams) if (s.isSelected) ++selCount;
    int clipCount = (int)s_streamClipboard.streams.size();

    std::vector<ContextMenuItem> menuItems;

    std::wstring undoDesc = pState->undoStack.empty() ? L"" : (L" " + pState->undoStack.back().actionDesc);
    std::wstring redoDesc = pState->redoStack.empty() ? L"" : (L" " + pState->redoStack.back().actionDesc);
    menuItems.push_back(ContextMenuItem::Action(CmdUndo, L"\xE7A7", L"Undo" + undoDesc, L"Ctrl+Z", !pState->undoStack.empty()));
    menuItems.push_back(ContextMenuItem::Action(CmdRedo, L"\xE7A6", L"Redo" + redoDesc, L"Ctrl+Y", !pState->redoStack.empty()));
    menuItems.push_back(ContextMenuItem::Separator());

    std::wstring cutTxt = std::wstring(L"Cut Stream") + (selCount > 1 ? (L"s (" + std::to_wstring(selCount) + L")") : L"");
    std::wstring copyTxt = std::wstring(L"Copy Stream") + (selCount > 1 ? (L"s (" + std::to_wstring(selCount) + L")") : L"");
    std::wstring pasteTxt = std::wstring(L"Paste Stream") + (clipCount > 0 ? (L"s (" + std::to_wstring(clipCount) + L")") : L"");
    std::wstring dupTxt = std::wstring(L"Duplicate Stream") + (selCount > 1 ? L"s" : L"");
    std::wstring delTxt = std::wstring(L"Delete Stream") + (selCount > 1 ? L"s" : L"");

    menuItems.push_back(ContextMenuItem::Action(CmdCut, L"\xE8C6", cutTxt, L"Ctrl+X", selCount > 0));
    menuItems.push_back(ContextMenuItem::Action(CmdCopy, L"\xE8C8", copyTxt, L"Ctrl+C", selCount > 0));
    menuItems.push_back(ContextMenuItem::Action(CmdPaste, L"\xE77F", pasteTxt, L"Ctrl+V", clipCount > 0));
    menuItems.push_back(ContextMenuItem::Separator());

    menuItems.push_back(ContextMenuItem::Action(CmdMoveUp, L"\xE70E", L"Move Up", L"Alt+Up", selCount > 0));
    menuItems.push_back(ContextMenuItem::Action(CmdMoveDown, L"\xE70D", L"Move Down", L"Alt+Down", selCount > 0));
    menuItems.push_back(ContextMenuItem::Separator());

    menuItems.push_back(ContextMenuItem::Action(CmdDuplicate, L"\xE8C8", dupTxt, L"Ctrl+D", selCount > 0));
    menuItems.push_back(ContextMenuItem::Action(CmdDelete, L"\xE74D", delTxt, L"Del", selCount > 0));
    menuItems.push_back(ContextMenuItem::Separator());

    menuItems.push_back(ContextMenuItem::Action(CmdToggleExpandCollapse, L"\xE740", L"Toggle Expand / Collapse", L"", selCount > 0));
    menuItems.push_back(ContextMenuItem::Separator());

    menuItems.push_back(ContextMenuItem::Action(CmdSelectAll, L"\xE762", L"Select All", L"Ctrl+A", !grp.streams.empty()));
    if (selCount > 0)
    {
        menuItems.push_back(ContextMenuItem::Action(CmdDeselectAll, L"\xE711", L"Deselect All", L"", true));
        menuItems.push_back(ContextMenuItem::Action(CmdInvertSelection, L"\xE8AB", L"Invert Selection", L"", true));
    }

    if (clickedStreamIdx >= 0 && clickedStreamIdx < (int)grp.streams.size())
    {
        menuItems.push_back(ContextMenuItem::Separator());
        bool isPl = grp.streams[clickedStreamIdx].isPlayingPreview;
        menuItems.push_back(ContextMenuItem::Action(CmdPlayPreview, isPl ? L"\xE71A" : L"\xE768", isPl ? L"Stop Loop" : L"Play Stream Loop", L"Space", true));
    }

    menuItems.push_back(ContextMenuItem::Separator());
    menuItems.push_back(ContextMenuItem::Action(CmdConsolidateWavs, L"\xE8B7", L"Consolidate WAV Sound Assets...", L""));

    int cmd = ModernContextMenu::Show(pState->hWnd, screenX, screenY, menuItems, pState->bDarkMode, 210);

    switch (cmd)
    {
    case CmdUndo:
        Sms_PerformUndo(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdRedo:
        Sms_PerformRedo(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdCut:
        CutSelectedStreams(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdCopy:
        CopySelectedStreams(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdPaste:
        PasteClipboardStreams(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdDuplicate:
        DuplicateSelectedStreams(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdDelete:
        DeleteSelectedStreams(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdMoveUp:
        MoveSelectedStreamsUp(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdMoveDown:
        MoveSelectedStreamsDown(pState);
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdToggleExpandCollapse:
        for (auto& s : grp.streams) if (s.isSelected) s.isCollapsed = !s.isCollapsed;
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdSelectAll:
        for (auto& s : grp.streams) s.isSelected = true;
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdDeselectAll:
        for (auto& s : grp.streams) s.isSelected = false;
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdInvertSelection:
        for (auto& s : grp.streams) s.isSelected = !s.isSelected;
        InvalidateRect(pState->hWnd, NULL, FALSE);
        break;
    case CmdPlayPreview:
        if (clickedStreamIdx >= 0 && clickedStreamIdx < (int)grp.streams.size())
        {
            auto& st = grp.streams[clickedStreamIdx];
            if (st.isPlayingPreview)
            {
                StopAudio();
                st.isPlayingPreview = false;
            }
            else
            {
                StopAudio();
                for (auto& s : grp.streams) s.isPlayingPreview = false;
                std::wstring wavPath;
                for (const auto& tr : st.triggers)
                {
                    for (const auto& f : tr.files)
                    {
                        if (f.existsOnDisk) { wavPath = f.resolvedDiskPath; break; }
                    }
                    if (!wavPath.empty()) break;
                }
                if (!wavPath.empty()) st.isPlayingPreview = PlayAudio(wavPath, true);
            }
            InvalidateRect(pState->hWnd, NULL, FALSE);
        }
        break;
    case CmdConsolidateWavs:
    {
        if (pState->doc.filePath.empty())
        {
            ShowModernMessageBox(pState->hWnd, L"Please open or save an SMS file first before consolidating sound assets.", L"No Active SMS", MB_OK | MB_ICONINFORMATION);
            break;
        }
        SmsConsolidateResult cRes;
        if (ShowSmsAssetConsolidatorDialog(pState->hWnd, pState->doc, pState->basePath, cRes))
        {
            if (cRes.docModified)
            {
                Sms_PushUndo(pState, L"Consolidate WAV Sound Assets");
                pState->doc.isModified = true;
            }
            InvalidateRect(pState->hWnd, NULL, FALSE);
        }
        break;
    }
    default:
        break;
    }
}

static LRESULT CALLBACK SmsEditorViewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* pCS = (CREATESTRUCTW*)lParam;
        pState = (SmsEditorViewState*)pCS->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        FluentDragGhost::Initialize(pCS->hInstance);

        HDC hdc = GetDC(hWnd);
        int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(hWnd, hdc);

        pState->hFontTitle = CreateFontW(-MulDiv(13, logPixelsY, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontSubtitle = CreateFontW(-MulDiv(10, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontBody = CreateFontW(-MulDiv(10, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontBold = CreateFontW(-MulDiv(10, logPixelsY, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        pState->hFontMono = CreateFontW(-MulDiv(9, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        pState->hFontIcons = CreateFontW(-MulDiv(11, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
        if (!pState->hFontIcons)
            pState->hFontIcons = CreateFontW(-MulDiv(11, logPixelsY, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

        return 0;
    }

    case WM_DESTROY:
    {
        StopAudio();
        if (pState)
        {
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontSubtitle) DeleteObject(pState->hFontSubtitle);
            if (pState->hFontBody) DeleteObject(pState->hFontBody);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontMono) DeleteObject(pState->hFontMono);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            RECT rcCanvas = { pState->sidebarWidth + 1, pState->headerHeight, w, h };
            if (pState->rcStreamsCard.bottom > pState->rcStreamsCard.top)
            {
                pState->vScrollBar.SetBounds({ pState->rcStreamsCard.right - 13, pState->rcStreamsCard.top + 37, pState->rcStreamsCard.right - 1, pState->rcStreamsCard.bottom - 2 });
                pState->vScrollBar.SetRange(0, pState->totalContentHeight, (pState->rcStreamsViewport.bottom - pState->rcStreamsViewport.top));
            }
            else
            {
                pState->vScrollBar.SetBounds({ w - 14, pState->headerHeight, w, h });
                pState->vScrollBar.SetRange(0, pState->totalContentHeight, h - pState->headerHeight);
            }

            pState->treeScrollBar.SetBounds({ pState->sidebarWidth - 12, 0, pState->sidebarWidth, h });
            pState->treeScrollBar.SetRange(0, pState->totalTreeHeight, h);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);

            if (pt.x < pState->sidebarWidth)
            {
                pState->treeScrollBar.OnMouseWheel(delta, 30, hWnd);
                pState->treeScrollY = pState->treeScrollBar.GetPos();
            }
            else
            {
                pState->vScrollBar.OnMouseWheel(delta, 45, hWnd);
                pState->scrollY = pState->vScrollBar.GetPos();
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_SETCURSOR:
    {
        if (pState)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);

            for (const auto& r : pState->hitRegions)
            {
                if (PtInRect(&r.rect, pt))
                {
                    bool isFixedTopControl = (r.type >= ClickableRegion::SgPropsCardToggle && r.type <= ClickableRegion::SgDeactTrackTypeBtn) ||
                        r.type == ClickableRegion::SgTab ||
                        r.type == ClickableRegion::SaveBtn ||
                        r.type == ClickableRegion::ConsolidateWavsBtn ||
                        r.type == ClickableRegion::NewSmsBtn ||
                        r.type == ClickableRegion::OpenFileBtn ||
                        r.type == ClickableRegion::OpenFolderBtn ||
                        r.type == ClickableRegion::SelectAllStreamsBtn ||
                        r.type == ClickableRegion::CopyStreamsBtn ||
                        r.type == ClickableRegion::PasteStreamsBtn ||
                        r.type == ClickableRegion::ExpandAllBtn ||
                        r.type == ClickableRegion::CollapseAllBtn ||
                        r.type == ClickableRegion::FixCountBtn;

                    bool isPinnedHdrBtn = ((r.type == ClickableRegion::StreamAddBtn || r.type == ClickableRegion::StreamMasterCheck) && pt.y < pState->rcStreamsViewport.top);

                    if (!isFixedTopControl && !isPinnedHdrBtn)
                    {
                        if (pState->rcStreamsViewport.bottom > pState->rcStreamsViewport.top &&
                            (pt.y < pState->rcStreamsViewport.top || pt.y > pState->rcStreamsViewport.bottom))
                            continue;
                    }

                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            }
            if (pt.x < pState->sidebarWidth)
            {
                for (const auto& hit : pState->treeHitRegions)
                {
                    if (PtInRect(&hit.first, pt))
                    {
                        int idx = hit.second;
                        if (idx >= 0 && idx < (int)pState->treeItems.size())
                        {
                            auto& it = pState->treeItems[idx];
                            if (it.type == ContextTreeItem::SoundSlot || it.type == ContextTreeItem::Standalone || it.type == ContextTreeItem::Unit)
                            {
                                SetCursor(LoadCursor(NULL, IDC_HAND));
                                return TRUE;
                            }
                        }
                    }
                }
            }
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_MOUSEMOVE:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pState->ptMouse = pt;

            if (!pState->isTrackingMouse)
            {
                TRACKMOUSEEVENT tme = { 0 };
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hWnd;
                TrackMouseEvent(&tme);
                pState->isTrackingMouse = true;
            }

            bool sbChange = false;
            if (pt.x >= pState->sidebarWidth)
                sbChange = pState->vScrollBar.OnMouseMove(pt, hWnd);
            else
                sbChange = pState->treeScrollBar.OnMouseMove(pt, hWnd);

            if (sbChange)
            {
                pState->scrollY = pState->vScrollBar.GetPos();
                pState->treeScrollY = pState->treeScrollBar.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            else if (!pState->isDraggingStreams)
            {
                // Smooth real-time hover updates for all stream cards, buttons, and chips
                InvalidateRect(hWnd, NULL, FALSE);
            }

            // Check if we should initiate stream drag
            if (pState->isPotentialDrag && !pState->isDraggingStreams && (wParam & MK_LBUTTON))
            {
                int dx = abs(pt.x - pState->ptDragStart.x);
                int dy = abs(pt.y - pState->ptDragStart.y);
                if (dx > 4 || dy > 4)
                {
                    if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                    {
                        auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                        pState->isDraggingStreams = true;
                        SetCapture(hWnd);

                        pState->draggedIndices.clear();
                        bool clickedIsSelected = (pState->draggedStreamIndex >= 0 && pState->draggedStreamIndex < (int)grp.streams.size() && grp.streams[pState->draggedStreamIndex].isSelected);

                        if (clickedIsSelected)
                        {
                            for (int i = 0; i < (int)grp.streams.size(); ++i)
                            {
                                if (grp.streams[i].isSelected)
                                    pState->draggedIndices.push_back(i);
                            }
                        }
                        else if (pState->draggedStreamIndex >= 0 && pState->draggedStreamIndex < (int)grp.streams.size())
                        {
                            pState->draggedIndices.push_back(pState->draggedStreamIndex);
                        }

                        std::vector<DragGhostItem> ghostItems;
                        for (int idx : pState->draggedIndices)
                        {
                            DragGhostItem gItem;
                            gItem.type = GhostItemType::SoundFile;
                            gItem.name = L"#" + std::to_wstring(idx + 1) + L" " + grp.streams[idx].friendlyTitle;
                            if (pState->draggedIndices.size() > 1)
                            {
                                gItem.subtitle = std::to_wstring(pState->draggedIndices.size()) + L" Streams Selected";
                            }
                            else
                            {
                                gItem.subtitle = grp.streams[idx].triggers.empty() ? L"" : (grp.streams[idx].triggers[0].files.empty() ? L"" : grp.streams[idx].triggers[0].files[0].fileName);
                            }
                            ghostItems.push_back(gItem);
                        }

                        POINT ptScreen = pt;
                        ClientToScreen(hWnd, &ptScreen);
                        FluentDragGhost::Show(hWnd, ptScreen, ghostItems);
                    }
                }
            }

            // Handle active stream dragging
            if (pState->isDraggingStreams)
            {
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                RECT rcCanvas = { pState->sidebarWidth + 1, pState->headerHeight, rcClient.right - 14, rcClient.bottom };

                // Auto-scroll when near edges of stream viewport
                int vpTop = (pState->rcStreamsViewport.bottom > pState->rcStreamsViewport.top) ? pState->rcStreamsViewport.top : rcCanvas.top;
                int vpBottom = (pState->rcStreamsViewport.bottom > pState->rcStreamsViewport.top) ? pState->rcStreamsViewport.bottom : rcCanvas.bottom;

                if (pt.y < vpTop + 30 && pState->scrollY > 0)
                {
                    pState->scrollY = (std::max)(0, pState->scrollY - 20);
                    pState->vScrollBar.SetPos(pState->scrollY);
                }
                else if (pt.y > vpBottom - 30)
                {
                    pState->scrollY = (std::min)(pState->vScrollBar.GetMax(), pState->scrollY + 20);
                    pState->vScrollBar.SetPos(pState->scrollY);
                }

                // Determine dropTargetIndex based on streamCardRects
                int dropIdx = (int)pState->streamCardRects.size();
                for (int i = 0; i < (int)pState->streamCardRects.size(); ++i)
                {
                    int midY = (pState->streamCardRects[i].top + pState->streamCardRects[i].bottom) / 2;
                    if (pt.y < midY)
                    {
                        dropIdx = i;
                        break;
                    }
                }
                pState->dropTargetIndex = dropIdx;

                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);

                std::wstring actionText;
                if (pState->draggedIndices.size() > 1)
                {
                    actionText = L"Move " + std::to_wstring(pState->draggedIndices.size()) + L" Streams to Pos #" + std::to_wstring(dropIdx + 1);
                }
                else
                {
                    actionText = L"Move Stream to Pos #" + std::to_wstring(dropIdx + 1);
                }

                FluentDragGhost::Move(ptScreen, true, actionText);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            pState->isTrackingMouse = false;
            pState->ptMouse = { -1, -1 };
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (pState)
        {
            SetFocus(hWnd);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            // Check scrollbars
            if (pt.x >= pState->sidebarWidth && pState->vScrollBar.OnLButtonDown(pt, hWnd))
            {
                pState->scrollY = pState->vScrollBar.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (pt.x < pState->sidebarWidth && pState->treeScrollBar.OnLButtonDown(pt, hWnd))
            {
                pState->treeScrollY = pState->treeScrollBar.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Check Sidebar tree hit
            if (pt.x < pState->sidebarWidth)
            {
                for (const auto& hit : pState->treeHitRegions)
                {
                    if (PtInRect(&hit.first, pt))
                    {
                        int idx = hit.second;
                        if (idx >= 0 && idx < (int)pState->treeItems.size())
                        {
                            auto& it = pState->treeItems[idx];
                            if (it.type == ContextTreeItem::SoundSlot || it.type == ContextTreeItem::Standalone)
                            {
                                LoadFileIntoEditor(pState, it.filePath);
                                return 0;
                            }
                        }
                    }
                }
                return 0;
            }

            // Check Canvas clickable regions: Pass 0 for buttons, Pass 1 for card toggles
            for (int pass = 0; pass < 2; ++pass)
            {
                for (const auto& region : pState->hitRegions)
                {
                    if (pass == 0 && region.type == ClickableRegion::StreamToggle) continue;
                    if (pass == 1 && region.type != ClickableRegion::StreamToggle) continue;

                    if (PtInRect(&region.rect, pt))
                    {
                        bool isFixedTopControl = (region.type >= ClickableRegion::SgPropsCardToggle && region.type <= ClickableRegion::SgDeactTrackTypeBtn) ||
                            region.type == ClickableRegion::SgTab ||
                            region.type == ClickableRegion::SaveBtn ||
                            region.type == ClickableRegion::ConsolidateWavsBtn ||
                            region.type == ClickableRegion::NewSmsBtn ||
                            region.type == ClickableRegion::OpenFileBtn ||
                            region.type == ClickableRegion::OpenFolderBtn ||
                            region.type == ClickableRegion::SelectAllStreamsBtn ||
                            region.type == ClickableRegion::CopyStreamsBtn ||
                            region.type == ClickableRegion::PasteStreamsBtn ||
                            region.type == ClickableRegion::ExpandAllBtn ||
                            region.type == ClickableRegion::CollapseAllBtn ||
                            region.type == ClickableRegion::FixCountBtn;

                        bool isPinnedHdrBtn = ((region.type == ClickableRegion::StreamAddBtn || region.type == ClickableRegion::StreamMasterCheck) && pt.y < pState->rcStreamsViewport.top);

                        if (!isFixedTopControl && !isPinnedHdrBtn)
                        {
                            if (pState->rcStreamsViewport.bottom > pState->rcStreamsViewport.top &&
                                (pt.y < pState->rcStreamsViewport.top || pt.y > pState->rcStreamsViewport.bottom))
                                continue;
                        }

                    switch (region.type)
                    {
                    case ClickableRegion::SgTab:
                        pState->isDraggingStreams = false;
                        pState->isPotentialDrag = false;
                        pState->dropTargetIndex = -1;
                        pState->doc.activeGroupIndex = region.index1;
                        pState->scrollY = 0;
                        pState->vScrollBar.SetPos(0);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;

                    case ClickableRegion::SgPropsCardToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.isSgPropsCollapsed = !grp.isSgPropsCollapsed;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgLevelBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 == -2)
                            {
                                std::wstring outVal;
                                if (PromptSmsModalText(hWnd, L"Custom Scalability Group Level",
                                    L"Enter Scalability Group level number (e.g. 5, 3, 1, 0, or custom):",
                                    std::to_wstring(grp.groupLevel), outVal))
                                {
                                    try {
                                        int lvl = std::stoi(outVal);
                                        grp.groupLevel = lvl;
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    } catch (...) {}
                                }
                            }
                            else if (region.index1 >= 0)
                            {
                                grp.groupLevel = region.index1;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SgMasterVolToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.hasGroupVolume = !grp.hasGroupVolume;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgMasterVolBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 == -1) // Step down
                            {
                                grp.groupVolume = (std::max)(0.0f, grp.groupVolume - 0.05f);
                                grp.hasGroupVolume = true;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else if (region.index1 == 1) // Step up
                            {
                                grp.groupVolume = (std::min)(50.0f, grp.groupVolume + 0.05f);
                                grp.hasGroupVolume = true;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else // Preset menu / Custom
                            {
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(1, L"", L"0.25 (Quarter Volume)"),
                                    ContextMenuItem::Action(2, L"", L"0.50 (Half Volume)"),
                                    ContextMenuItem::Action(3, L"", L"0.75 (Three Quarters)"),
                                    ContextMenuItem::Action(4, L"", L"1.00 (Standard 100% Gain)"),
                                    ContextMenuItem::Action(5, L"", L"1.25 (High Gain)"),
                                    ContextMenuItem::Action(6, L"", L"1.50 (Boosted +50%)"),
                                    ContextMenuItem::Action(7, L"", L"2.00 (Double Volume)"),
                                    ContextMenuItem::Action(8, L"", L"3.00 (Triple Volume)"),
                                    ContextMenuItem::Action(9, L"", L"5.00 (500% Gain)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(99, L"\xE70F", L"Custom Volume...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd == 1) { grp.groupVolume = 0.25f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 2) { grp.groupVolume = 0.50f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 3) { grp.groupVolume = 0.75f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 4) { grp.groupVolume = 1.00f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 5) { grp.groupVolume = 1.25f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 6) { grp.groupVolume = 1.50f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 7) { grp.groupVolume = 2.00f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 8) { grp.groupVolume = 3.00f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 9) { grp.groupVolume = 5.00f; grp.hasGroupVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 99)
                                {
                                    wchar_t curBuf[32]; swprintf_s(curBuf, L"%.2f", grp.groupVolume);
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Custom Master Volume", L"Enter Master Volume multiplier (e.g. 0.85, 1.20, 2.50, 5.00+):", curBuf, outVal))
                                    {
                                        try {
                                            float v = std::stof(outVal);
                                            grp.groupVolume = (std::max)(0.0f, v);
                                            grp.hasGroupVolume = true;
                                            Sms_NotifyDocModified(pState);
                                        } catch (...) {}
                                    }
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SgStereoToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.isStereo = !grp.isStereo;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgActCamCab:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.hasCabCam = !grp.hasCabCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgActCamPass:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.hasPassengerCam = !grp.hasPassengerCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgActCamExt:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.hasExternalCam = !grp.hasExternalCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgActDistBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 != 0) // Step
                            {
                                grp.activationDistance = (std::max)(1.0f, grp.activationDistance + (float)region.index1);
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else // Preset menu
                            {
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(50, L"", L"50 m (Very Close / Interior)"),
                                    ContextMenuItem::Action(100, L"", L"100 m (Standard Cab / In-Cab cutoff)"),
                                    ContextMenuItem::Action(250, L"", L"250 m"),
                                    ContextMenuItem::Action(500, L"", L"500 m (Medium Yard)"),
                                    ContextMenuItem::Action(630, L"", L"630 m (Standard MSTS Exterior)"),
                                    ContextMenuItem::Action(900, L"", L"900 m"),
                                    ContextMenuItem::Action(1000, L"", L"1000 m (1 km - Standard ORTS)"),
                                    ContextMenuItem::Action(1500, L"", L"1500 m"),
                                    ContextMenuItem::Action(2000, L"", L"2000 m (2 km - High Spec)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(999, L"\xE70F", L"Custom Distance...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd == 999)
                                {
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Custom Activation Distance", L"Enter Activation Distance in meters:", std::to_wstring((int)grp.activationDistance), outVal))
                                    {
                                        try {
                                            float d = std::stof(outVal);
                                            grp.activationDistance = (std::max)(1.0f, d);
                                            Sms_NotifyDocModified(pState);
                                        } catch (...) {}
                                    }
                                }
                                else if (cmd > 0)
                                {
                                    grp.activationDistance = (float)cmd;
                                    Sms_NotifyDocModified(pState);
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SgTrackTypeBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            RECT rcAnchorScreen = region.rect;
                            MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);

                            std::vector<ContextMenuItem> items = {
                                ContextMenuItem::Action(100, grp.trackType == -1 ? L"✓" : L"", L"None (Standard Rolling Stock / Engines)"),
                                ContextMenuItem::Separator(),
                                ContextMenuItem::Action(101, grp.trackType == 0 ? L"✓" : L"", L"0: Default Track"),
                                ContextMenuItem::Action(102, grp.trackType == 1 ? L"✓" : L"", L"1: Concrete Supported Track"),
                                ContextMenuItem::Action(103, grp.trackType == 2 ? L"✓" : L"", L"2: Wood Supported Track"),
                                ContextMenuItem::Action(104, grp.trackType == 3 ? L"✓" : L"", L"3: In Tunnel"),
                                ContextMenuItem::Action(105, grp.trackType == 4 ? L"✓" : L"", L"4: Steel Bridge"),
                                ContextMenuItem::Action(106, grp.trackType == 5 ? L"✓" : L"", L"5: Girder Bridge"),
                                ContextMenuItem::Action(107, grp.trackType == 6 ? L"✓" : L"", L"6: Under Bridge"),
                                ContextMenuItem::Action(108, grp.trackType == 7 ? L"✓" : L"", L"7: Concrete Bridge"),
                                ContextMenuItem::Action(109, grp.trackType == 8 ? L"✓" : L"", L"8: Wooden Trestle Bridge"),
                                ContextMenuItem::Action(110, grp.trackType == 9 ? L"✓" : L"", L"9: Switches & Turnouts (Open Rails)"),
                                ContextMenuItem::Action(111, grp.trackType == 10 ? L"✓" : L"", L"10: Curve Squeal (Open Rails)"),
                                ContextMenuItem::Separator(),
                                ContextMenuItem::Action(199, L"\xE70F", L"Enter Custom Track Index...")
                            };

                            int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                            if (cmd >= 100 && cmd <= 111)
                            {
                                int newTt = (cmd == 100) ? -1 : (cmd - 101);
                                grp.trackType = newTt;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else if (cmd == 199)
                            {
                                std::wstring outVal;
                                std::wstring def = (grp.trackType >= 0) ? std::to_wstring(grp.trackType) : L"0";
                                if (PromptSmsModalText(hWnd, L"Custom TrackType Index", L"Enter TrackType index number as defined in route ttype.dat:", def, outVal))
                                {
                                    try {
                                        int tt = std::stoi(outVal);
                                        grp.trackType = (std::max)(-1, tt);
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    } catch (...) {}
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SgDeactDistBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 != 0) // Step
                            {
                                grp.deactivationDistance = (std::max)(1.0f, grp.deactivationDistance + (float)region.index1);
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else // Preset menu
                            {
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(100, L"", L"100 m (Cab / Interior cutoff)"),
                                    ContextMenuItem::Action(300, L"", L"300 m"),
                                    ContextMenuItem::Action(500, L"", L"500 m"),
                                    ContextMenuItem::Action(630, L"", L"630 m (Standard MSTS Exterior default)"),
                                    ContextMenuItem::Action(1000, L"", L"1000 m (1 km - Standard ORTS Cutoff)"),
                                    ContextMenuItem::Action(1500, L"", L"1500 m"),
                                    ContextMenuItem::Action(2000, L"", L"2000 m (2 km - High Spec)"),
                                    ContextMenuItem::Action(3000, L"", L"3000 m (3 km - Distant Horns)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(999, L"\xE70F", L"Custom Distance...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd == 999)
                                {
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Custom Deactivation Distance", L"Enter Deactivation Distance in meters:", std::to_wstring((int)grp.deactivationDistance), outVal))
                                    {
                                        try {
                                            float d = std::stof(outVal);
                                            grp.deactivationDistance = (std::max)(1.0f, d);
                                            Sms_NotifyDocModified(pState);
                                        } catch (...) {}
                                    }
                                }
                                else if (cmd > 0)
                                {
                                    grp.deactivationDistance = (float)cmd;
                                    Sms_NotifyDocModified(pState);
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SgDeactCamCab:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.deactCabCam = !grp.deactCabCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgDeactCamPass:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.deactPassengerCam = !grp.deactPassengerCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgDeactCamExt:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            grp.deactExternalCam = !grp.deactExternalCam;
                            Sms_NotifyDocModified(pState);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::SgDeactTrackTypeBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            RECT rcAnchorScreen = region.rect;
                            MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);

                            int curTt = (grp.deactTrackType >= 0) ? grp.deactTrackType : grp.trackType;

                            std::vector<ContextMenuItem> items = {
                                ContextMenuItem::Action(100, curTt == -1 ? L"✓" : L"", L"None (Standard Rolling Stock / Engines)"),
                                ContextMenuItem::Separator(),
                                ContextMenuItem::Action(101, curTt == 0 ? L"✓" : L"", L"0: Default Track"),
                                ContextMenuItem::Action(102, curTt == 1 ? L"✓" : L"", L"1: Concrete Supported Track"),
                                ContextMenuItem::Action(103, curTt == 2 ? L"✓" : L"", L"2: Wood Supported Track"),
                                ContextMenuItem::Action(104, curTt == 3 ? L"✓" : L"", L"3: In Tunnel"),
                                ContextMenuItem::Action(105, curTt == 4 ? L"✓" : L"", L"4: Steel Bridge"),
                                ContextMenuItem::Action(106, curTt == 5 ? L"✓" : L"", L"5: Girder Bridge"),
                                ContextMenuItem::Action(107, curTt == 6 ? L"✓" : L"", L"6: Under Bridge"),
                                ContextMenuItem::Action(108, curTt == 7 ? L"✓" : L"", L"7: Concrete Bridge"),
                                ContextMenuItem::Action(109, curTt == 8 ? L"✓" : L"", L"8: Wooden Trestle Bridge"),
                                ContextMenuItem::Action(110, curTt == 9 ? L"✓" : L"", L"9: Switches & Turnouts (Open Rails)"),
                                ContextMenuItem::Action(111, curTt == 10 ? L"✓" : L"", L"10: Curve Squeal (Open Rails)"),
                                ContextMenuItem::Separator(),
                                ContextMenuItem::Action(199, L"\xE70F", L"Enter Custom Track Index...")
                            };

                            int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                            if (cmd >= 100 && cmd <= 111)
                            {
                                int newTt = (cmd == 100) ? -1 : (cmd - 101);
                                grp.deactTrackType = newTt;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                            else if (cmd == 199)
                            {
                                std::wstring outVal;
                                std::wstring def = (curTt >= 0) ? std::to_wstring(curTt) : L"0";
                                if (PromptSmsModalText(hWnd, L"Custom Deactivation TrackType Index", L"Enter TrackType index number as defined in route ttype.dat:", def, outVal))
                                {
                                    try {
                                        int tt = std::stoi(outVal);
                                        grp.deactTrackType = (std::max)(-1, tt);
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    } catch (...) {}
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::FixCountBtn:
                        SmsParser_AutoRepairAndNormalize(pState->doc);
                        SmsEditorView_SaveCurrentFile(hWnd);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;

                    case ClickableRegion::SaveBtn:
                        SmsEditorView_SaveCurrentFile(hWnd);
                        return 0;

                    case ClickableRegion::ConsolidateWavsBtn:
                    {
                        if (pState->doc.filePath.empty())
                        {
                            ShowModernMessageBox(hWnd, L"Please open or save an SMS file first before consolidating sound assets.", L"No Active SMS", MB_OK | MB_ICONINFORMATION);
                            return 0;
                        }
                        SmsConsolidateResult cRes;
                        if (ShowSmsAssetConsolidatorDialog(hWnd, pState->doc, pState->basePath, cRes))
                        {
                            if (cRes.docModified)
                            {
                                Sms_PushUndo(pState, L"Consolidate WAV Sound Assets");
                                pState->doc.isModified = true;
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::NewSmsBtn:
                    {
                        std::wstring createdPath;
                        if (PromptSmsNewFileModal(hWnd, pState->basePath, createdPath))
                        {
                            App_NotifySoundLibraryChanged(createdPath);
                            SmsEditorView_OpenSmsFile(hWnd, createdPath, pState->basePath);
                        }
                        return 0;
                    }

                    case ClickableRegion::OpenFileBtn:
                    {
                        wchar_t szFile[MAX_PATH] = { 0 };
                        OPENFILENAMEW ofn = { sizeof(ofn) };
                        ofn.hwndOwner = hWnd;
                        ofn.lpstrFilter = L"Sound Management System (*.sms)\0*.sms\0All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = MAX_PATH;
                        std::wstring trainsetDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                        ofn.lpstrInitialDir = trainsetDir.c_str();
                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                        if (GetOpenFileNameW(&ofn))
                        {
                            SmsEditorView_OpenSmsFile(hWnd, szFile, pState->basePath);
                        }
                        return 0;
                    }

                    case ClickableRegion::OpenFolderBtn:
                    {
                        std::wstring targetPath = !region.strParam.empty() ? region.strParam : pState->doc.filePath;
                        if (!targetPath.empty())
                        {
                            if (GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                            {
                                std::wstring exploreCmd = L"/select,\"" + targetPath + L"\"";
                                HINSTANCE hInst = ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
                                if ((INT_PTR)hInst <= 32)
                                {
                                    size_t lSlash = targetPath.find_last_of(L"\\/");
                                    if (lSlash != std::wstring::npos)
                                    {
                                        std::wstring folder = targetPath.substr(0, lSlash);
                                        ShellExecuteW(NULL, L"open", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                    }
                                }
                            }
                            else
                            {
                                size_t lSlash = targetPath.find_last_of(L"\\/");
                                if (lSlash != std::wstring::npos)
                                {
                                    std::wstring folder = targetPath.substr(0, lSlash);
                                    ShellExecuteW(NULL, L"open", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CollapseAllBtn:
                    {
                        for (auto& grp : pState->doc.groups)
                        {
                            for (auto& s : grp.streams) s.isCollapsed = true;
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::ExpandAllBtn:
                    {
                        for (auto& grp : pState->doc.groups)
                        {
                            for (auto& s : grp.streams) s.isCollapsed = false;
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::StreamMasterCheck:
                    {
                        SelectAllStreams(pState);
                        pState->lastSelectedStreamIndex = -1;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::StreamSelectCheck:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                                if (isShift && pState->lastSelectedStreamIndex >= 0 && pState->lastSelectedStreamIndex < (int)grp.streams.size())
                                {
                                    int startIdx = (std::min)(pState->lastSelectedStreamIndex, region.index1);
                                    int endIdx = (std::max)(pState->lastSelectedStreamIndex, region.index1);
                                    for (int i = startIdx; i <= endIdx; ++i)
                                    {
                                        grp.streams[i].isSelected = true;
                                    }
                                }
                                else
                                {
                                    grp.streams[region.index1].isSelected = !grp.streams[region.index1].isSelected;
                                    pState->lastSelectedStreamIndex = region.index1;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::SelectAllStreamsBtn:
                    {
                        SelectAllStreams(pState);
                        pState->lastSelectedStreamIndex = -1;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::CopyStreamsBtn:
                    {
                        CopySelectedStreams(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::PasteStreamsBtn:
                    {
                        PasteClipboardStreams(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }

                    case ClickableRegion::StreamToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                if (GetKeyState(VK_SHIFT) & 0x8000)
                                {
                                    if (pState->lastSelectedStreamIndex >= 0 && pState->lastSelectedStreamIndex < (int)grp.streams.size())
                                    {
                                        int startIdx = (std::min)(pState->lastSelectedStreamIndex, region.index1);
                                        int endIdx = (std::max)(pState->lastSelectedStreamIndex, region.index1);
                                        for (int i = startIdx; i <= endIdx; ++i)
                                        {
                                            grp.streams[i].isSelected = true;
                                        }
                                    }
                                    else
                                    {
                                        grp.streams[region.index1].isSelected = true;
                                        pState->lastSelectedStreamIndex = region.index1;
                                    }
                                    InvalidateRect(hWnd, NULL, FALSE);
                                    return 0;
                                }

                                if (GetKeyState(VK_CONTROL) & 0x8000)
                                {
                                    grp.streams[region.index1].isSelected = !grp.streams[region.index1].isSelected;
                                    pState->lastSelectedStreamIndex = region.index1;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                    return 0;
                                }

                                pState->lastSelectedStreamIndex = region.index1;
                                pState->isPotentialDrag = true;
                                pState->ptDragStart = pt;
                                pState->draggedStreamIndex = region.index1;
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamPlayLoop:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (st.isPlayingPreview)
                                {
                                    StopAudio();
                                    st.isPlayingPreview = false;
                                }
                                else
                                {
                                    StopAudio();
                                    for (auto& s : grp.streams) s.isPlayingPreview = false;

                                    // Find initial wav to play
                                    std::wstring wavPath;
                                    for (const auto& tr : st.triggers)
                                    {
                                        for (const auto& f : tr.files)
                                        {
                                            if (f.existsOnDisk)
                                            {
                                                wavPath = f.resolvedDiskPath;
                                                break;
                                            }
                                        }
                                        if (!wavPath.empty()) break;
                                    }

                                    if (!wavPath.empty())
                                    {
                                        st.isPlayingPreview = PlayAudio(wavPath, true);
                                    }
                                    else
                                    {
                                        ShowModernMessageBox(hWnd, L"No valid audio file found on disk for this stream.", L"Audio Preview", MB_OK | MB_ICONWARNING);
                                    }
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::WavPlay:
                    {
                        if (!region.strParam.empty())
                        {
                            if (s_isPlayingAudio && s_playingPath == region.strParam)
                            {
                                StopAudio();
                            }
                            else
                            {
                                PlayAudio(region.strParam, false);
                            }
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamEditTitleBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                std::wstring outVal;
                                if (PromptSmsModalText(hWnd, L"Edit Stream Title (Skip Block)",
                                    L"Enter comment or title for this stream (saved as Skip comment in SMS):",
                                    st.friendlyTitle, outVal))
                                {
                                    st.friendlyTitle = outVal;
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamPriorityDown:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (st.priority > 1)
                                {
                                    st.priority--;
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamPriorityUp:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (st.priority < 10)
                                {
                                    st.priority++;
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamPriorityVal:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items;
                                for (int p = 10; p >= 1; --p)
                                {
                                    std::wstring label = L"Priority " + std::to_wstring(p);
                                    if (p == 10) label += L" (Highest / Critical - Open Rails Protected)";
                                    else if (p == 6) label += L" (Default / Standard Priority)";
                                    else if (p == 1) label += L" (Lowest / Background Ambience)";
                                    items.push_back(ContextMenuItem::Action(p, p == st.priority ? L"✓" : L"", label));
                                }
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd >= 1 && cmd <= 10)
                                {
                                    st.priority = cmd;
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamVolumeToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                st.hasVolume = !st.hasVolume;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamVolumeDown:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                st.volume = (std::max)(0.0f, st.volume - 0.05f);
                                st.hasVolume = true;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamVolumeUp:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                st.volume = (std::min)(50.0f, st.volume + 0.05f);
                                st.hasVolume = true;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamVolumeVal:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(1, L"", L"0.25 (25% Volume)"),
                                    ContextMenuItem::Action(2, L"", L"0.50 (50% Volume)"),
                                    ContextMenuItem::Action(3, L"", L"0.75 (75% Volume)"),
                                    ContextMenuItem::Action(4, L"", L"1.00 (100% Standard Gain)"),
                                    ContextMenuItem::Action(5, L"", L"1.25 (125% High Gain)"),
                                    ContextMenuItem::Action(6, L"", L"1.50 (150% Boosted)"),
                                    ContextMenuItem::Action(7, L"", L"2.00 (200% Gain)"),
                                    ContextMenuItem::Action(8, L"", L"3.00 (300% Gain)"),
                                    ContextMenuItem::Action(9, L"", L"5.00 (500% Gain)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(99, L"\xE70F", L"Custom Volume...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd == 1) { st.volume = 0.25f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 2) { st.volume = 0.50f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 3) { st.volume = 0.75f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 4) { st.volume = 1.00f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 5) { st.volume = 1.25f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 6) { st.volume = 1.50f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 7) { st.volume = 2.00f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 8) { st.volume = 3.00f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 9) { st.volume = 5.00f; st.hasVolume = true; Sms_NotifyDocModified(pState); }
                                else if (cmd == 99)
                                {
                                    wchar_t curBuf[32]; swprintf_s(curBuf, L"%.2f", st.volume);
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Custom Stream Volume", L"Enter Volume multiplier (e.g. 1.00 = standard, 2.00, 5.00, 10.00+):", curBuf, outVal))
                                    {
                                        try {
                                            float v = std::stof(outVal);
                                            st.volume = (std::max)(0.0f, v);
                                            st.hasVolume = true;
                                            Sms_NotifyDocModified(pState);
                                        } catch (...) {}
                                    }
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamStereoToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                st.isStereo = !st.isStereo;
                                Sms_NotifyDocModified(pState);
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamDeleteBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                std::wstring q = L"Delete Stream #" + std::to_wstring(region.index1 + 1) + L" (\"" + grp.streams[region.index1].friendlyTitle + L"\")?";
                                if (ShowModernMessageBox(hWnd, q.c_str(), L"Confirm Delete Stream", MB_YESNO | MB_ICONQUESTION) == IDYES)
                                {
                                    Sms_PushUndo(pState, L"Delete Stream");
                                    grp.streams.erase(grp.streams.begin() + region.index1);
                                    grp.declaredStreams = (int)grp.streams.size();
                                    for (int i = 0; i < (int)grp.streams.size(); ++i)
                                        grp.streams[i].streamIndex = i;
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamAddBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            Sms_PushUndo(pState, L"Add Stream");
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            SmsStream newStream;
                            newStream.streamIndex = (int)grp.streams.size();
                            newStream.priority = 6;
                            newStream.hasVolume = true;
                            newStream.volume = 1.0f;
                            newStream.isStereo = grp.isStereo;
                            newStream.isCollapsed = false;
                            newStream.isSelected = false;
                            newStream.friendlyTitle = L"Stream " + std::to_wstring(grp.streams.size() + 1);

                            // Add 1 default template trigger
                            SmsTrigger defTrig;
                            defTrig.triggerType = L"Discrete_Trigger";
                            defTrig.triggerId = 8; // Horn High (Start)
                            defTrig.actionType = L"PlayOneShot";
                            defTrig.selectionMethod = L"SequentialSelection";
                            defTrig.isCollapsed = false;
                            SmsFileRef defFile;
                            defFile.fileName = L"sample.wav";
                            defFile.flag = -1;
                            defTrig.files.push_back(defFile);
                            defTrig.declaredFiles = 1;
                            newStream.triggers.push_back(defTrig);
                            newStream.declaredTriggers = 1;

                            grp.streams.push_back(newStream);
                            grp.declaredStreams = (int)grp.streams.size();
                            Sms_NotifyDocModified(pState);

                            // Auto scroll down to reveal new stream
                            pState->scrollY = (std::max)(0, pState->vScrollBar.GetMax());
                            pState->vScrollBar.SetPos(pState->scrollY);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamAddTriggerBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(1, L"", L"Discrete_Trigger (Controls / Key Events)"),
                                    ContextMenuItem::Action(2, L"", L"Initial_Trigger (Stream Initialization / Start)"),
                                    ContextMenuItem::Action(3, L"", L"Variable_Trigger (Speed / RPM / Notch Thresholds)"),
                                    ContextMenuItem::Action(4, L"", L"Random_Trigger (Timed Ambient Sounds)"),
                                    ContextMenuItem::Action(5, L"", L"Dist_Travelled_Trigger (Odometer / Distance Based)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(10, L"", L"Track Switch Triggers (Open Rails)..."),
                                    ContextMenuItem::Action(20, L"", L"Track Joint Triggers (Open Rails)..."),
                                    ContextMenuItem::Action(30, L"", L"Track Xover Triggers (Open Rails)..."),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(99, L"\xE70F", L"Custom Trigger Type...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd > 0)
                                {
                                    Sms_PushUndo(pState, L"Add Trigger");
                                    SmsTrigger newTrig;
                                    newTrig.actionType = L"PlayOneShot";
                                    newTrig.selectionMethod = L"SequentialSelection";
                                    newTrig.isCollapsed = false;

                                    if (cmd == 1)
                                    {
                                        newTrig.triggerType = L"Discrete_Trigger";
                                        newTrig.triggerId = 8; // Horn start default
                                    }
                                    else if (cmd == 2)
                                    {
                                        newTrig.triggerType = L"Initial_Trigger";
                                        newTrig.actionType = L"StartLoop";
                                    }
                                    else if (cmd == 3)
                                    {
                                        newTrig.triggerType = L"Variable_Trigger";
                                        newTrig.variableCondition = L"Speed_Inc_Past 15.0";
                                    }
                                    else if (cmd == 4)
                                    {
                                        newTrig.triggerType = L"Random_Trigger";
                                        newTrig.delayMin = 5.0f;
                                        newTrig.delayMax = 15.0f;
                                    }
                                    else if (cmd == 5)
                                    {
                                        newTrig.triggerType = L"Dist_Travelled_Trigger";
                                        newTrig.distMin = 50.0f;
                                        newTrig.distMax = 150.0f;
                                    }
                                    else if (cmd == 10)
                                    {
                                        std::vector<ContextMenuItem> swItems = {
                                            ContextMenuItem::Action(102, L"", L"Switch_2Axle_Trigger"),
                                            ContextMenuItem::Action(103, L"", L"Switch_3Axle_Trigger"),
                                            ContextMenuItem::Action(104, L"", L"Switch_4Axle_Trigger"),
                                            ContextMenuItem::Action(106, L"", L"Switch_6Axle_Trigger"),
                                            ContextMenuItem::Action(108, L"", L"Switch_8Axle_Trigger")
                                        };
                                        int sCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, swItems, TRUE, 0, &rcAnchorScreen);
                                        if (sCmd == 102) newTrig.triggerType = L"Switch_2Axle_Trigger";
                                        else if (sCmd == 103) newTrig.triggerType = L"Switch_3Axle_Trigger";
                                        else if (sCmd == 104) newTrig.triggerType = L"Switch_4Axle_Trigger";
                                        else if (sCmd == 106) newTrig.triggerType = L"Switch_6Axle_Trigger";
                                        else if (sCmd == 108) newTrig.triggerType = L"Switch_8Axle_Trigger";
                                        else return 0;
                                        newTrig.carCameraDist = 80.0f;
                                    }
                                    else if (cmd == 20)
                                    {
                                        std::vector<ContextMenuItem> jtItems = {
                                            ContextMenuItem::Action(202, L"", L"Joint_2Axle_Trigger"),
                                            ContextMenuItem::Action(203, L"", L"Joint_3Axle_Trigger"),
                                            ContextMenuItem::Action(204, L"", L"Joint_4Axle_Trigger"),
                                            ContextMenuItem::Action(206, L"", L"Joint_6Axle_Trigger"),
                                            ContextMenuItem::Action(208, L"", L"Joint_8Axle_Trigger")
                                        };
                                        int jCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, jtItems, TRUE, 0, &rcAnchorScreen);
                                        if (jCmd == 202) newTrig.triggerType = L"Joint_2Axle_Trigger";
                                        else if (jCmd == 203) newTrig.triggerType = L"Joint_3Axle_Trigger";
                                        else if (jCmd == 204) newTrig.triggerType = L"Joint_4Axle_Trigger";
                                        else if (jCmd == 206) newTrig.triggerType = L"Joint_6Axle_Trigger";
                                        else if (jCmd == 208) newTrig.triggerType = L"Joint_8Axle_Trigger";
                                        else return 0;
                                        newTrig.carCameraDist = 80.0f;
                                    }
                                    else if (cmd == 30)
                                    {
                                        std::vector<ContextMenuItem> xoItems = {
                                            ContextMenuItem::Action(302, L"", L"Xover_2Axle_Trigger"),
                                            ContextMenuItem::Action(303, L"", L"Xover_3Axle_Trigger"),
                                            ContextMenuItem::Action(304, L"", L"Xover_4Axle_Trigger"),
                                            ContextMenuItem::Action(306, L"", L"Xover_6Axle_Trigger"),
                                            ContextMenuItem::Action(308, L"", L"Xover_8Axle_Trigger")
                                        };
                                        int xCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, xoItems, TRUE, 0, &rcAnchorScreen);
                                        if (xCmd == 302) newTrig.triggerType = L"Xover_2Axle_Trigger";
                                        else if (xCmd == 303) newTrig.triggerType = L"Xover_3Axle_Trigger";
                                        else if (xCmd == 304) newTrig.triggerType = L"Xover_4Axle_Trigger";
                                        else if (xCmd == 306) newTrig.triggerType = L"Xover_6Axle_Trigger";
                                        else if (xCmd == 308) newTrig.triggerType = L"Xover_8Axle_Trigger";
                                        else return 0;
                                        newTrig.carCameraDist = 80.0f;
                                    }
                                    else if (cmd == 99)
                                    {
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Custom Trigger Type", L"Enter Open Rails trigger keyword:", L"Discrete_Trigger", outVal))
                                        {
                                            newTrig.triggerType = outVal;
                                        }
                                        else
                                        {
                                            return 0;
                                        }
                                    }

                                    st.triggers.push_back(newTrig);
                                    st.declaredTriggers = (int)st.triggers.size();
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    st.triggers[region.index2].isCollapsed = !st.triggers[region.index2].isCollapsed;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerDeleteBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    Sms_PushUndo(pState, L"Delete Trigger");
                                    st.triggers.erase(st.triggers.begin() + region.index2);
                                    st.declaredTriggers = (int)st.triggers.size();
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerTypeBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    RECT rcAnchorScreen = region.rect;
                                    MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                    std::vector<ContextMenuItem> items = {
                                        ContextMenuItem::Action(1, tr.triggerType == L"Discrete_Trigger" ? L"✓" : L"", L"Discrete_Trigger (Controls / Key Events)"),
                                        ContextMenuItem::Action(2, tr.triggerType == L"Initial_Trigger" ? L"✓" : L"", L"Initial_Trigger (Stream Initialization / Start)"),
                                        ContextMenuItem::Action(3, tr.triggerType == L"Variable_Trigger" ? L"✓" : L"", L"Variable_Trigger (Speed / RPM / Notch Thresholds)"),
                                        ContextMenuItem::Action(4, tr.triggerType == L"Random_Trigger" ? L"✓" : L"", L"Random_Trigger (Timed Ambient Sounds)"),
                                        ContextMenuItem::Action(5, tr.triggerType == L"Dist_Travelled_Trigger" ? L"✓" : L"", L"Dist_Travelled_Trigger (Odometer / Distance Based)"),
                                        ContextMenuItem::Separator(),
                                        ContextMenuItem::Action(10, L"", L"Track Switch Triggers (Open Rails)..."),
                                        ContextMenuItem::Action(20, L"", L"Track Joint Triggers (Open Rails)..."),
                                        ContextMenuItem::Action(30, L"", L"Track Xover Triggers (Open Rails)..."),
                                        ContextMenuItem::Separator(),
                                        ContextMenuItem::Action(99, L"\xE70F", L"Custom Trigger Type...")
                                    };
                                    int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                    if (cmd == 1) { tr.triggerType = L"Discrete_Trigger"; Sms_NotifyDocModified(pState); }
                                    else if (cmd == 2) { tr.triggerType = L"Initial_Trigger"; Sms_NotifyDocModified(pState); }
                                    else if (cmd == 3) { tr.triggerType = L"Variable_Trigger"; if (tr.variableCondition.empty()) tr.variableCondition = L"Speed_Inc_Past 10.0"; Sms_NotifyDocModified(pState); }
                                    else if (cmd == 4) { tr.triggerType = L"Random_Trigger"; if (tr.delayMax <= 0.0f) { tr.delayMin = 5.0f; tr.delayMax = 15.0f; } Sms_NotifyDocModified(pState); }
                                    else if (cmd == 5) { tr.triggerType = L"Dist_Travelled_Trigger"; if (tr.distMax <= 0.0f) { tr.distMin = 50.0f; tr.distMax = 150.0f; } Sms_NotifyDocModified(pState); }
                                    else if (cmd == 10)
                                    {
                                        std::vector<ContextMenuItem> swItems = {
                                            ContextMenuItem::Action(102, L"", L"Switch_2Axle_Trigger"),
                                            ContextMenuItem::Action(103, L"", L"Switch_3Axle_Trigger"),
                                            ContextMenuItem::Action(104, L"", L"Switch_4Axle_Trigger"),
                                            ContextMenuItem::Action(106, L"", L"Switch_6Axle_Trigger"),
                                            ContextMenuItem::Action(108, L"", L"Switch_8Axle_Trigger")
                                        };
                                        int sCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, swItems, TRUE, 0, &rcAnchorScreen);
                                        if (sCmd == 102) tr.triggerType = L"Switch_2Axle_Trigger";
                                        else if (sCmd == 103) tr.triggerType = L"Switch_3Axle_Trigger";
                                        else if (sCmd == 104) tr.triggerType = L"Switch_4Axle_Trigger";
                                        else if (sCmd == 106) tr.triggerType = L"Switch_6Axle_Trigger";
                                        else if (sCmd == 108) tr.triggerType = L"Switch_8Axle_Trigger";
                                        if (sCmd > 0) Sms_NotifyDocModified(pState);
                                    }
                                    else if (cmd == 20)
                                    {
                                        std::vector<ContextMenuItem> jtItems = {
                                            ContextMenuItem::Action(202, L"", L"Joint_2Axle_Trigger"),
                                            ContextMenuItem::Action(203, L"", L"Joint_3Axle_Trigger"),
                                            ContextMenuItem::Action(204, L"", L"Joint_4Axle_Trigger"),
                                            ContextMenuItem::Action(206, L"", L"Joint_6Axle_Trigger"),
                                            ContextMenuItem::Action(208, L"", L"Joint_8Axle_Trigger")
                                        };
                                        int jCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, jtItems, TRUE, 0, &rcAnchorScreen);
                                        if (jCmd == 202) tr.triggerType = L"Joint_2Axle_Trigger";
                                        else if (jCmd == 203) tr.triggerType = L"Joint_3Axle_Trigger";
                                        else if (jCmd == 204) tr.triggerType = L"Joint_4Axle_Trigger";
                                        else if (jCmd == 206) tr.triggerType = L"Joint_6Axle_Trigger";
                                        else if (jCmd == 208) tr.triggerType = L"Joint_8Axle_Trigger";
                                        if (jCmd > 0) Sms_NotifyDocModified(pState);
                                    }
                                    else if (cmd == 30)
                                    {
                                        std::vector<ContextMenuItem> xoItems = {
                                            ContextMenuItem::Action(302, L"", L"Xover_2Axle_Trigger"),
                                            ContextMenuItem::Action(303, L"", L"Xover_3Axle_Trigger"),
                                            ContextMenuItem::Action(304, L"", L"Xover_4Axle_Trigger"),
                                            ContextMenuItem::Action(306, L"", L"Xover_6Axle_Trigger"),
                                            ContextMenuItem::Action(308, L"", L"Xover_8Axle_Trigger")
                                        };
                                        int xCmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, xoItems, TRUE, 0, &rcAnchorScreen);
                                        if (xCmd == 302) tr.triggerType = L"Xover_2Axle_Trigger";
                                        else if (xCmd == 303) tr.triggerType = L"Xover_3Axle_Trigger";
                                        else if (xCmd == 304) tr.triggerType = L"Xover_4Axle_Trigger";
                                        else if (xCmd == 306) tr.triggerType = L"Xover_6Axle_Trigger";
                                        else if (xCmd == 308) tr.triggerType = L"Xover_8Axle_Trigger";
                                        if (xCmd > 0) Sms_NotifyDocModified(pState);
                                    }
                                    else if (cmd == 99)
                                    {
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Custom Trigger Type", L"Enter Open Rails trigger keyword:", tr.triggerType, outVal))
                                        {
                                            tr.triggerType = outVal;
                                            Sms_NotifyDocModified(pState);
                                        }
                                    }
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerActionBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    RECT rcAnchorScreen = region.rect;
                                    MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                    std::vector<ContextMenuItem> items = {
                                        ContextMenuItem::Action(1, tr.actionType == L"PlayOneShot" ? L"✓" : L"", L"PlayOneShot (Single Sound Playback)"),
                                        ContextMenuItem::Action(2, tr.actionType == L"StartLoop" ? L"✓" : L"", L"StartLoop (Continuous Looping Audio)"),
                                        ContextMenuItem::Action(3, tr.actionType == L"StartLoopRelease" ? L"✓" : L"", L"StartLoopRelease (Loop Until Trigger Released)"),
                                        ContextMenuItem::Action(4, tr.actionType == L"ReleaseLoopRelease" ? L"✓" : L"", L"ReleaseLoopRelease (Halt Current Sound Loop)"),
                                        ContextMenuItem::Action(5, tr.actionType == L"ReleaseLoopReleaseWithJump" ? L"✓" : L"", L"ReleaseLoopReleaseWithJump (Jump to Loop Tail)"),
                                        ContextMenuItem::Separator(),
                                        ContextMenuItem::Action(6, tr.actionType == L"SetStreamVolume" ? L"✓" : L"", L"SetStreamVolume (Modify Stream Gain Dynamically)"),
                                        ContextMenuItem::Action(7, tr.actionType == L"DisableTrigger" ? L"✓" : L"", L"DisableTrigger (Deactivate Trigger ID)"),
                                        ContextMenuItem::Action(8, tr.actionType == L"EnableTrigger" ? L"✓" : L"", L"EnableTrigger (Activate Trigger ID)")
                                    };
                                    int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                    if (cmd == 1) tr.actionType = L"PlayOneShot";
                                    else if (cmd == 2) tr.actionType = L"StartLoop";
                                    else if (cmd == 3) tr.actionType = L"StartLoopRelease";
                                    else if (cmd == 4) tr.actionType = L"ReleaseLoopRelease";
                                    else if (cmd == 5) tr.actionType = L"ReleaseLoopReleaseWithJump";
                                    else if (cmd == 6) tr.actionType = L"SetStreamVolume";
                                    else if (cmd == 7) tr.actionType = L"DisableTrigger";
                                    else if (cmd == 8) tr.actionType = L"EnableTrigger";
                                    if (cmd > 0)
                                    {
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerActionParamBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    if (EqualsI(tr.actionType, L"DisableTrigger") || EqualsI(tr.actionType, L"EnableTrigger"))
                                    {
                                        std::wstring defVal = (tr.actionParam > 0.0f) ? std::to_wstring((int)tr.actionParam) : L"0";
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Target Trigger ID", L"Enter target trigger ID integer to enable/disable (or 0 for all):", defVal, outVal))
                                        {
                                            try {
                                                tr.actionParam = (float)std::stoi(outVal);
                                                Sms_NotifyDocModified(pState);
                                                InvalidateRect(hWnd, NULL, FALSE);
                                            } catch (...) {}
                                        }
                                    }
                                    else if (EqualsI(tr.actionType, L"SetStreamVolume") || EqualsI(tr.actionType, L"SetVolume"))
                                    {
                                        wchar_t defBuf[32];
                                        swprintf_s(defBuf, L"%.2f", tr.actionParam);
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Set Stream Volume", L"Enter stream volume gain (0.00 to 1.00):", defBuf, outVal))
                                        {
                                            try {
                                                float v = std::stof(outVal);
                                                if (v < 0.0f) v = 0.0f;
                                                if (v > 1.0f) v = 1.0f;
                                                tr.actionParam = v;
                                                Sms_NotifyDocModified(pState);
                                                InvalidateRect(hWnd, NULL, FALSE);
                                            } catch (...) {}
                                        }
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerParamBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    if (tr.triggerType == L"Discrete_Trigger")
                                    {
                                        RECT rcAnchorScreen = region.rect;
                                        MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                        std::vector<ContextMenuItem> items = {
                                            ContextMenuItem::Action(8, tr.triggerId == 8 ? L"✓" : L"", L"8: Horn High (Start)"),
                                            ContextMenuItem::Action(9, tr.triggerId == 9 ? L"✓" : L"", L"9: Horn High (Release)"),
                                            ContextMenuItem::Action(10, tr.triggerId == 10 ? L"✓" : L"", L"10: Bell (Start / Toggle)"),
                                            ContextMenuItem::Action(11, tr.triggerId == 11 ? L"✓" : L"", L"11: Bell (Release / Stop)"),
                                            ContextMenuItem::Action(4, tr.triggerId == 4 ? L"✓" : L"", L"4: Sander ON"),
                                            ContextMenuItem::Action(5, tr.triggerId == 5 ? L"✓" : L"", L"5: Sander OFF"),
                                            ContextMenuItem::Action(6, tr.triggerId == 6 ? L"✓" : L"", L"6: Wipers ON"),
                                            ContextMenuItem::Action(7, tr.triggerId == 7 ? L"✓" : L"", L"7: Wipers OFF"),
                                            ContextMenuItem::Action(14, tr.triggerId == 14 ? L"✓" : L"", L"14: Train Brake Application"),
                                            ContextMenuItem::Action(15, tr.triggerId == 15 ? L"✓" : L"", L"15: Train Brake Release"),
                                            ContextMenuItem::Action(23, tr.triggerId == 23 ? L"✓" : L"", L"23: Power ON (Pantograph / Diesel Start)"),
                                            ContextMenuItem::Action(24, tr.triggerId == 24 ? L"✓" : L"", L"24: Power OFF (Diesel Stop)"),
                                            ContextMenuItem::Action(58, tr.triggerId == 58 ? L"✓" : L"", L"58: Coupling Action"),
                                            ContextMenuItem::Action(56, tr.triggerId == 56 ? L"✓" : L"", L"56: Uncoupling Action"),
                                            ContextMenuItem::Action(105, tr.triggerId == 105 ? L"✓" : L"", L"105: Passenger Doors Open (ORTS)"),
                                            ContextMenuItem::Action(106, tr.triggerId == 106 ? L"✓" : L"", L"106: Passenger Doors Close (ORTS)"),
                                            ContextMenuItem::Action(150, tr.triggerId == 150 ? L"✓" : L"", L"150: Circuit Breaker Closing (ORTS)"),
                                            ContextMenuItem::Action(152, tr.triggerId == 152 ? L"✓" : L"", L"152: Circuit Breaker Opening (ORTS)"),
                                            ContextMenuItem::Action(155, tr.triggerId == 155 ? L"✓" : L"", L"155: Pantograph 2 Up (ORTS)"),
                                            ContextMenuItem::Action(156, tr.triggerId == 156 ? L"✓" : L"", L"156: Pantograph 2 Down (ORTS)"),
                                            ContextMenuItem::Action(157, tr.triggerId == 157 ? L"✓" : L"", L"157: Pantograph 3 Up (ORTS)"),
                                            ContextMenuItem::Action(158, tr.triggerId == 158 ? L"✓" : L"", L"158: Pantograph 3 Down (ORTS)"),
                                            ContextMenuItem::Action(159, tr.triggerId == 159 ? L"✓" : L"", L"159: Pantograph 4 Up (ORTS)"),
                                            ContextMenuItem::Action(160, tr.triggerId == 160 ? L"✓" : L"", L"160: Pantograph 4 Down (ORTS)"),
                                            ContextMenuItem::Action(164, tr.triggerId == 164 ? L"✓" : L"", L"164: Steam Blowdown Toggle (ORTS)"),
                                            ContextMenuItem::Action(165, tr.triggerId == 165 ? L"✓" : L"", L"165: Steam Safety Valves (ORTS)"),
                                            ContextMenuItem::Action(166, tr.triggerId == 166 ? L"✓" : L"", L"166: Steam Injectors (ORTS)"),
                                            ContextMenuItem::Action(169, tr.triggerId == 169 ? L"✓" : L"", L"169: Vigilance Alarm VCD (ORTS)"),
                                            ContextMenuItem::Action(170, tr.triggerId == 170 ? L"✓" : L"", L"170: AWS / TPWS Sunflower (ORTS)"),
                                            ContextMenuItem::Separator(),
                                            ContextMenuItem::Action(999, L"\xE70F", L"Custom Discrete Trigger ID...")
                                        };
                                        int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                        if (cmd > 0 && cmd != 999)
                                        {
                                            tr.triggerId = cmd;
                                            Sms_NotifyDocModified(pState);
                                        }
                                        else if (cmd == 999)
                                        {
                                            std::wstring outVal;
                                            if (PromptSmsModalText(hWnd, L"Custom Discrete Trigger ID", L"Enter Discrete Trigger ID integer (e.g. 8, 14, 58, 105):", std::to_wstring(tr.triggerId), outVal))
                                            {
                                                try {
                                                    tr.triggerId = std::stoi(outVal);
                                                    Sms_NotifyDocModified(pState);
                                                } catch (...) {}
                                            }
                                        }
                                    }
                                    else if (tr.triggerType == L"Variable_Trigger")
                                    {
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Edit Variable Condition", L"Enter Variable condition (e.g. Speed_Inc_Past 15.0 or Variable1_Dec_Past 0.5):", tr.variableCondition, outVal))
                                        {
                                            tr.variableCondition = outVal;
                                            Sms_NotifyDocModified(pState);
                                        }
                                    }
                                    else if (tr.triggerType == L"Random_Trigger")
                                    {
                                        wchar_t curBuf[64];
                                        swprintf_s(curBuf, L"%.2f %.2f", tr.delayMin, tr.delayMax);
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Edit Random Delay", L"Enter Delay Min and Max in seconds separated by a space (e.g. 5.0 15.0):", curBuf, outVal))
                                        {
                                            try {
                                                float dmin = 0.0f, dmax = 0.0f;
                                                if (swscanf_s(outVal.c_str(), L"%f %f", &dmin, &dmax) == 2)
                                                {
                                                    tr.delayMin = dmin;
                                                    tr.delayMax = dmax;
                                                    Sms_NotifyDocModified(pState);
                                                }
                                            } catch (...) {}
                                        }
                                    }
                                    else if (tr.triggerType == L"Dist_Travelled_Trigger")
                                    {
                                        wchar_t curBuf[64];
                                        swprintf_s(curBuf, L"%.1f %.1f", tr.distMin, tr.distMax);
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Edit Travel Distance", L"Enter Dist Min and Max in meters separated by a space (e.g. 50.0 150.0):", curBuf, outVal))
                                        {
                                            try {
                                                float dmin = 0.0f, dmax = 0.0f;
                                                if (swscanf_s(outVal.c_str(), L"%f %f", &dmin, &dmax) == 2)
                                                {
                                                    tr.distMin = dmin;
                                                    tr.distMax = dmax;
                                                    Sms_NotifyDocModified(pState);
                                                }
                                            } catch (...) {}
                                        }
                                    }
                                    else if (SmsParser_IsTrackAxleTrigger(tr.triggerType))
                                    {
                                        wchar_t curBuf[32];
                                        swprintf_s(curBuf, L"%.1f", tr.carCameraDist);
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Edit Car Camera Distance", L"Enter Car_Camera_Dist cutoff in meters (default 80.0):", curBuf, outVal))
                                        {
                                            try {
                                                tr.carCameraDist = std::stof(outVal);
                                                tr.hasCarCameraDist = true;
                                                Sms_NotifyDocModified(pState);
                                            } catch (...) {}
                                        }
                                    }
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerSelMethodBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    if (tr.selectionMethod == L"RandomSelection")
                                        tr.selectionMethod = L"SequentialSelection";
                                    else
                                        tr.selectionMethod = L"RandomSelection";
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerAddWavBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    wchar_t szFile[MAX_PATH] = { 0 };
                                    OPENFILENAMEW ofn = { sizeof(ofn) };
                                    ofn.hwndOwner = hWnd;
                                    ofn.lpstrFilter = L"Wave Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
                                    ofn.lpstrFile = szFile;
                                    ofn.nMaxFile = MAX_PATH;

                                    std::wstring initDir;
                                    if (!pState->doc.filePath.empty())
                                    {
                                        size_t lastSlash = pState->doc.filePath.find_last_of(L"\\/");
                                        if (lastSlash != std::wstring::npos)
                                            initDir = pState->doc.filePath.substr(0, lastSlash);
                                    }
                                    if (initDir.empty() && !pState->basePath.empty())
                                    {
                                        initDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                                    }
                                    ofn.lpstrInitialDir = initDir.c_str();
                                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                                    if (GetOpenFileNameW(&ofn))
                                    {
                                        std::wstring chosenPath = szFile;
                                        std::wstring lowerChosen = chosenPath;
                                        for (auto& c : lowerChosen) c = towlower(c);
                                        std::wstring lowerBase = pState->basePath;
                                        for (auto& c : lowerBase) c = towlower(c);

                                        bool isInTrainset = (lowerChosen.find(L"\\trains\\trainset\\") != std::wstring::npos) ||
                                                            (!lowerBase.empty() && lowerChosen.find(lowerBase) != std::wstring::npos);

                                        if (!isInTrainset)
                                        {
                                            ShowModernMessageBox(hWnd,
                                                L"The selected audio file must reside inside the TRAINS\\TRAINSET\\ folder or the Train Simulator root folder for Open Rails compatibility.",
                                                L"Invalid WAV File Location",
                                                MB_ICONWARNING | MB_OK);
                                        }
                                        else
                                        {
                                            Sms_PushUndo(pState, L"Add WAV File");
                                            std::wstring relToken = SmsParser_RetargetWavRelativePath(pState->doc.filePath, chosenPath, pState->basePath);
                                            SmsFileRef fRef;
                                            fRef.fileName = relToken;
                                            fRef.flag = -1;
                                            fRef.resolvedDiskPath = chosenPath;
                                            fRef.existsOnDisk = (GetFileAttributesW(chosenPath.c_str()) != INVALID_FILE_ATTRIBUTES);

                                            tr.files.push_back(fRef);
                                            tr.declaredFiles = (int)tr.files.size();
                                            Sms_NotifyDocModified(pState);
                                            InvalidateRect(hWnd, NULL, FALSE);
                                        }
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerWavEditBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    if (region.index3 >= 0 && region.index3 < (int)tr.files.size())
                                    {
                                        wchar_t szFile[MAX_PATH] = { 0 };
                                        OPENFILENAMEW ofn = { sizeof(ofn) };
                                        ofn.hwndOwner = hWnd;
                                        ofn.lpstrFilter = L"Wave Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
                                        ofn.lpstrFile = szFile;
                                        ofn.nMaxFile = MAX_PATH;

                                        std::wstring initDir;
                                        const auto& curWav = tr.files[region.index3];
                                        if (!curWav.resolvedDiskPath.empty())
                                        {
                                            size_t lastSlash = curWav.resolvedDiskPath.find_last_of(L"\\/");
                                            if (lastSlash != std::wstring::npos)
                                                initDir = curWav.resolvedDiskPath.substr(0, lastSlash);
                                        }
                                        if (initDir.empty() && !pState->doc.filePath.empty())
                                        {
                                            size_t lastSlash = pState->doc.filePath.find_last_of(L"\\/");
                                            if (lastSlash != std::wstring::npos)
                                                initDir = pState->doc.filePath.substr(0, lastSlash);
                                        }
                                        if (initDir.empty() && !pState->basePath.empty())
                                        {
                                            initDir = pState->basePath + L"TRAINS\\TRAINSET\\";
                                        }
                                        ofn.lpstrInitialDir = initDir.c_str();
                                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                                        if (GetOpenFileNameW(&ofn))
                                        {
                                            std::wstring chosenPath = szFile;
                                            std::wstring lowerChosen = chosenPath;
                                            for (auto& c : lowerChosen) c = towlower(c);
                                            std::wstring lowerBase = pState->basePath;
                                            for (auto& c : lowerBase) c = towlower(c);

                                            bool isInTrainset = (lowerChosen.find(L"\\trains\\trainset\\") != std::wstring::npos) ||
                                                                (!lowerBase.empty() && lowerChosen.find(lowerBase) != std::wstring::npos);

                                            if (!isInTrainset)
                                            {
                                                ShowModernMessageBox(hWnd,
                                                    L"The selected audio file must reside inside the TRAINS\\TRAINSET\\ folder or the Train Simulator root folder for Open Rails compatibility.",
                                                    L"Invalid WAV File Location",
                                                    MB_ICONWARNING | MB_OK);
                                            }
                                            else
                                            {
                                                Sms_PushUndo(pState, L"Change WAV File");
                                                std::wstring relToken = SmsParser_RetargetWavRelativePath(pState->doc.filePath, chosenPath, pState->basePath);
                                                auto& fRef = tr.files[region.index3];
                                                fRef.fileName = relToken;
                                                fRef.flag = -1;
                                                fRef.resolvedDiskPath = chosenPath;
                                                fRef.existsOnDisk = (GetFileAttributesW(chosenPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                                                Sms_NotifyDocModified(pState);
                                                InvalidateRect(hWnd, NULL, FALSE);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::TriggerWavDeleteBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.triggers.size())
                                {
                                    auto& tr = st.triggers[region.index2];
                                    if (region.index3 >= 0 && region.index3 < (int)tr.files.size())
                                    {
                                        Sms_PushUndo(pState, L"Delete WAV File");
                                        tr.files.erase(tr.files.begin() + region.index3);
                                        tr.declaredFiles = (int)tr.files.size();
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::StreamAddCurveBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                RECT rcAnchorScreen = region.rect;
                                MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(1, L"", L"VolumeCurve (SpeedControlled)"),
                                    ContextMenuItem::Action(2, L"", L"VolumeCurve (Variable1Controlled / Throttle)"),
                                    ContextMenuItem::Action(3, L"", L"VolumeCurve (Variable2Controlled / Train Brake)"),
                                    ContextMenuItem::Action(4, L"", L"VolumeCurve (Variable3Controlled / Dynamic Brake)"),
                                    ContextMenuItem::Action(5, L"", L"VolumeCurve (BrakeCylControlled / Brake Cyl)"),
                                    ContextMenuItem::Action(6, L"", L"VolumeCurve (DistanceControlled / Attenuation)"),
                                    ContextMenuItem::Action(7, L"", L"VolumeCurve (DynamicBrakeControlled - ORTS)"),
                                    ContextMenuItem::Action(8, L"", L"VolumeCurve (AirBrakePipeControlled - ORTS)"),
                                    ContextMenuItem::Action(9, L"", L"VolumeCurve (AngleOfAttackControlled / Flange Squeal - ORTS)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(10, L"", L"FrequencyCurve (SpeedControlled)"),
                                    ContextMenuItem::Action(11, L"", L"FrequencyCurve (Variable1Controlled / Throttle)"),
                                    ContextMenuItem::Action(12, L"", L"FrequencyCurve (EngineRPMControlled / Diesel RPM)"),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(99, L"\xE70F", L"Custom Curve...")
                                };
                                int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                if (cmd > 0)
                                {
                                    Sms_PushUndo(pState, L"Add Curve");
                                    SmsCurve newCv;
                                    newCv.granularity = 0.05f;
                                    newCv.isCollapsed = false;

                                    if (cmd >= 1 && cmd <= 9)
                                    {
                                        newCv.curveType = L"VolumeCurve";
                                        newCv.points = { { 0.0f, 0.0f }, { 100.0f, 1.0f } };
                                        if (cmd == 1) newCv.controlType = L"SpeedControlled";
                                        else if (cmd == 2) newCv.controlType = L"Variable1Controlled";
                                        else if (cmd == 3) newCv.controlType = L"Variable2Controlled";
                                        else if (cmd == 4) newCv.controlType = L"Variable3Controlled";
                                        else if (cmd == 5) newCv.controlType = L"BrakeCylControlled";
                                        else if (cmd == 6) { newCv.controlType = L"DistanceControlled"; newCv.points = { { 0.0f, 1.0f }, { 500.0f, 0.0f } }; }
                                        else if (cmd == 7) newCv.controlType = L"DynamicBrakeControlled";
                                        else if (cmd == 8) newCv.controlType = L"AirBrakePipeControlled";
                                        else if (cmd == 9) { newCv.controlType = L"AngleOfAttackControlled"; newCv.points = { { 0.0f, 0.0f }, { 0.05f, 1.0f } }; }
                                    }
                                    else if (cmd >= 10 && cmd <= 12)
                                    {
                                        newCv.curveType = L"FrequencyCurve";
                                        newCv.points = { { 0.0f, 11025.0f }, { 100.0f, 44100.0f } };
                                        if (cmd == 10) newCv.controlType = L"SpeedControlled";
                                        else if (cmd == 11) newCv.controlType = L"Variable1Controlled";
                                        else if (cmd == 12) { newCv.controlType = L"EngineRPMControlled"; newCv.points = { { 300.0f, 11025.0f }, { 1050.0f, 32000.0f } }; }
                                    }
                                    else if (cmd == 99)
                                    {
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Custom Curve Type", L"Enter Curve Type (e.g. VolumeCurve or FrequencyCurve):", L"VolumeCurve", outVal))
                                        {
                                            newCv.curveType = outVal;
                                            std::wstring outCtrl;
                                            if (PromptSmsModalText(hWnd, L"Curve Control Type", L"Enter Control Type (e.g. SpeedControlled, Variable1Controlled):", L"SpeedControlled", outCtrl))
                                            {
                                                newCv.controlType = outCtrl;
                                                newCv.points = { { 0.0f, 0.0f }, { 100.0f, 1.0f } };
                                            }
                                            else return 0;
                                        }
                                        else return 0;
                                    }

                                    newCv.declaredPoints = (int)newCv.points.size();
                                    st.curves.push_back(newCv);
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveToggle:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    st.curves[region.index2].isCollapsed = !st.curves[region.index2].isCollapsed;
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveDeleteBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    Sms_PushUndo(pState, L"Delete Curve");
                                    st.curves.erase(st.curves.begin() + region.index2);
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveTypeBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    if (cv.curveType == L"FrequencyCurve")
                                        cv.curveType = L"VolumeCurve";
                                    else
                                        cv.curveType = L"FrequencyCurve";
                                    Sms_NotifyDocModified(pState);
                                    InvalidateRect(hWnd, NULL, FALSE);
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveControlBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    RECT rcAnchorScreen = region.rect;
                                    MapWindowPoints(hWnd, NULL, (LPPOINT)&rcAnchorScreen, 2);
                                    std::vector<ContextMenuItem> items = {
                                        ContextMenuItem::Action(1, cv.controlType == L"SpeedControlled" ? L"✓" : L"", L"SpeedControlled (Locomotive / Train Speed)"),
                                        ContextMenuItem::Action(2, cv.controlType == L"Variable1Controlled" ? L"✓" : L"", L"Variable1Controlled (Throttle / Dynamic Notch)"),
                                        ContextMenuItem::Action(3, cv.controlType == L"Variable2Controlled" ? L"✓" : L"", L"Variable2Controlled (Train Brake Pressure)"),
                                        ContextMenuItem::Action(4, cv.controlType == L"Variable3Controlled" ? L"✓" : L"", L"Variable3Controlled (Engine Brake / Ammeter Current)"),
                                        ContextMenuItem::Action(5, cv.controlType == L"DistanceControlled" ? L"✓" : L"", L"DistanceControlled (Camera / Listener Distance)"),
                                        ContextMenuItem::Action(6, cv.controlType == L"BrakeCylControlled" ? L"✓" : L"", L"BrakeCylControlled (Brake Cylinder Pressure)"),
                                        ContextMenuItem::Action(7, cv.controlType == L"EngineRPMControlled" ? L"✓" : L"", L"EngineRPMControlled (Diesel RPM Curve - ORTS)"),
                                        ContextMenuItem::Action(8, cv.controlType == L"TractiveEffortControlled" ? L"✓" : L"", L"TractiveEffortControlled (Motor Force - ORTS)"),
                                        ContextMenuItem::Action(9, cv.controlType == L"DynamicBrakeControlled" ? L"✓" : L"", L"DynamicBrakeControlled (Dynamic Brake Effort - ORTS)"),
                                        ContextMenuItem::Action(10, cv.controlType == L"AirBrakePipeControlled" ? L"✓" : L"", L"AirBrakePipeControlled (Train Brake Pipe Pressure - ORTS)"),
                                        ContextMenuItem::Action(11, cv.controlType == L"AngleOfAttackControlled" ? L"✓" : L"", L"AngleOfAttackControlled (Wheel Flange Squeal - ORTS)"),
                                        ContextMenuItem::Separator(),
                                        ContextMenuItem::Action(99, L"\xE70F", L"Custom Controlled...")
                                    };
                                    int cmd = ModernContextMenu::Show(hWnd, rcAnchorScreen.left, rcAnchorScreen.bottom + 2, items, TRUE, 0, &rcAnchorScreen);
                                    if (cmd == 1) cv.controlType = L"SpeedControlled";
                                    else if (cmd == 2) cv.controlType = L"Variable1Controlled";
                                    else if (cmd == 3) cv.controlType = L"Variable2Controlled";
                                    else if (cmd == 4) cv.controlType = L"Variable3Controlled";
                                    else if (cmd == 5) cv.controlType = L"DistanceControlled";
                                    else if (cmd == 6) cv.controlType = L"BrakeCylControlled";
                                    else if (cmd == 7) cv.controlType = L"EngineRPMControlled";
                                    else if (cmd == 8) cv.controlType = L"TractiveEffortControlled";
                                    else if (cmd == 9) cv.controlType = L"DynamicBrakeControlled";
                                    else if (cmd == 10) cv.controlType = L"AirBrakePipeControlled";
                                    else if (cmd == 11) cv.controlType = L"AngleOfAttackControlled";
                                    else if (cmd == 99)
                                    {
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, L"Custom Curve Control", L"Enter Open Rails *Controlled keyword:", cv.controlType, outVal))
                                        {
                                            cv.controlType = outVal;
                                        }
                                    }
                                    if (cmd > 0)
                                    {
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveGranularityBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    wchar_t curBuf[32];
                                    swprintf_s(curBuf, L"%.4f", cv.granularity);
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Curve Granularity", L"Enter Curve Granularity step (e.g. 0.05, 0.01, 0.0):", curBuf, outVal))
                                    {
                                        try {
                                            cv.granularity = std::stof(outVal);
                                            Sms_NotifyDocModified(pState);
                                            InvalidateRect(hWnd, NULL, FALSE);
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurveAddPointBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    float defaultX = cv.points.empty() ? 0.0f : cv.points.back().x + 10.0f;
                                    float defaultY = cv.points.empty() ? 1.0f : cv.points.back().y;
                                    wchar_t curBuf[64];
                                    swprintf_s(curBuf, L"%.2f %.2f", defaultX, defaultY);
                                    std::wstring outVal;
                                    if (PromptSmsModalText(hWnd, L"Add Curve Point", L"Enter Input (X) and Output (Y) values separated by a space (e.g. 25.0 0.75):", curBuf, outVal))
                                    {
                                        try {
                                            float x = 0.0f, y = 0.0f;
                                            if (swscanf_s(outVal.c_str(), L"%f %f", &x, &y) == 2)
                                            {
                                                cv.points.push_back({ x, y });
                                                std::sort(cv.points.begin(), cv.points.end(), [](const SmsCurvePoint& a, const SmsCurvePoint& b) {
                                                    return a.x < b.x;
                                                });
                                                cv.declaredPoints = (int)cv.points.size();
                                                Sms_NotifyDocModified(pState);
                                                InvalidateRect(hWnd, NULL, FALSE);
                                            }
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurvePointEditBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    if (region.index3 >= 0 && region.index3 < (int)cv.points.size())
                                    {
                                        auto& pt = cv.points[region.index3];
                                        wchar_t curBuf[64];
                                        swprintf_s(curBuf, L"%.2f %.4f", pt.x, pt.y);
                                        std::wstring promptTitle = L"Edit Curve Point #" + std::to_wstring(region.index3 + 1);
                                        std::wstring outVal;
                                        if (PromptSmsModalText(hWnd, promptTitle, L"Enter new Input (X) and Output (Y) values separated by a space:", curBuf, outVal))
                                        {
                                            try {
                                                float x = 0.0f, y = 0.0f;
                                                if (swscanf_s(outVal.c_str(), L"%f %f", &x, &y) == 2)
                                                {
                                                    pt.x = x;
                                                    pt.y = y;
                                                    std::sort(cv.points.begin(), cv.points.end(), [](const SmsCurvePoint& a, const SmsCurvePoint& b) {
                                                        return a.x < b.x;
                                                    });
                                                    cv.declaredPoints = (int)cv.points.size();
                                                    Sms_NotifyDocModified(pState);
                                                    InvalidateRect(hWnd, NULL, FALSE);
                                                }
                                            } catch (...) {}
                                        }
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    case ClickableRegion::CurvePointDeleteBtn:
                    {
                        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                        {
                            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                            if (region.index1 >= 0 && region.index1 < (int)grp.streams.size())
                            {
                                auto& st = grp.streams[region.index1];
                                if (region.index2 >= 0 && region.index2 < (int)st.curves.size())
                                {
                                    auto& cv = st.curves[region.index2];
                                    if (region.index3 >= 0 && region.index3 < (int)cv.points.size())
                                    {
                                        cv.points.erase(cv.points.begin() + region.index3);
                                        cv.declaredPoints = (int)cv.points.size();
                                        Sms_NotifyDocModified(pState);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                    }
                                }
                            }
                        }
                        return 0;
                    }

                    default:
                        break;
                    }
                }
            }
        }
    }
    break;
    }

    case WM_LBUTTONUP:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pState->vScrollBar.OnLButtonUp(pt, hWnd);
            pState->treeScrollBar.OnLButtonUp(pt, hWnd);

            if (pState->isDraggingStreams)
            {
                pState->isDraggingStreams = false;
                pState->isPotentialDrag = false;
                FluentDragGhost::Hide();
                ReleaseCapture();

                if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                {
                    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                    if (pState->dropTargetIndex >= 0 && pState->dropTargetIndex <= (int)grp.streams.size())
                    {
                        Sms_PushUndo(pState, L"Reorder Streams");
                        ReorderStreams(grp.streams, pState->draggedIndices, pState->dropTargetIndex);
                        Sms_NotifyDocModified(pState);
                    }
                }
                pState->dropTargetIndex = -1;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (pState->isPotentialDrag)
            {
                pState->isPotentialDrag = false;
                if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                {
                    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                    if (pState->draggedStreamIndex >= 0 && pState->draggedStreamIndex < (int)grp.streams.size())
                    {
                        grp.streams[pState->draggedStreamIndex].isCollapsed = !grp.streams[pState->draggedStreamIndex].isCollapsed;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x < pState->sidebarWidth)
            {
                for (const auto& hit : pState->treeHitRegions)
                {
                    if (PtInRect(&hit.first, pt))
                    {
                        int idx = hit.second;
                        if (idx >= 0 && idx < (int)pState->treeItems.size())
                        {
                            auto& it = pState->treeItems[idx];
                            if ((it.type == ContextTreeItem::SoundSlot || it.type == ContextTreeItem::Standalone) && !it.filePath.empty())
                            {
                                OpenFileInDefaultEditor(hWnd, it.filePath);
                                return 0;
                            }
                        }
                    }
                }
                return 0;
            }
            else
            {
                // Also support double-clicking the top bar title area to open the active SMS file
                RECT rcTitleArea = { pState->sidebarWidth + 16, 0, pState->sidebarWidth + 360, pState->headerHeight };
                if (PtInRect(&rcTitleArea, pt) && !pState->doc.filePath.empty())
                {
                    OpenFileInDefaultEditor(hWnd, pState->doc.filePath);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x < pState->sidebarWidth)
            {
                for (const auto& hit : pState->treeHitRegions)
                {
                    if (PtInRect(&hit.first, pt))
                    {
                        int idx = hit.second;
                        if (idx >= 0 && idx < (int)pState->treeItems.size())
                        {
                            auto& it = pState->treeItems[idx];
                            if ((it.type == ContextTreeItem::SoundSlot || it.type == ContextTreeItem::Standalone) && !it.filePath.empty())
                            {
                                POINT ptScreen = pt;
                                ClientToScreen(hWnd, &ptScreen);

                                std::vector<ContextMenuItem> items = {
                                    ContextMenuItem::Action(1, L"\xE8DA", L"Open Containing Folder in Explorer", L"", it.exists),
                                    ContextMenuItem::Action(2, L"\xE8E5", L"Load SMS into Editor", L"", it.exists),
                                    ContextMenuItem::Action(4, L"\xE70F", L"Open in Default Text Editor", L"Dbl-Click", it.exists),
                                    ContextMenuItem::Separator(),
                                    ContextMenuItem::Action(3, L"\xE8C8", L"Copy File Path", L"", true)
                                };

                                int cmd = ModernContextMenu::Show(hWnd, ptScreen.x, ptScreen.y, items, TRUE);
                                if (cmd == 1)
                                {
                                    std::wstring exploreCmd = L"/select,\"" + it.filePath + L"\"";
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", exploreCmd.c_str(), NULL, SW_SHOWNORMAL);
                                }
                                else if (cmd == 2)
                                {
                                    LoadFileIntoEditor(pState, it.filePath);
                                }
                                else if (cmd == 4)
                                {
                                    OpenFileInDefaultEditor(hWnd, it.filePath);
                                }
                                else if (cmd == 3)
                                {
                                    if (OpenClipboard(hWnd))
                                    {
                                        EmptyClipboard();
                                        size_t cbBytes = (it.filePath.size() + 1) * sizeof(wchar_t);
                                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cbBytes);
                                        if (hMem)
                                        {
                                            memcpy(GlobalLock(hMem), it.filePath.c_str(), cbBytes);
                                            GlobalUnlock(hMem);
                                            SetClipboardData(CF_UNICODETEXT, hMem);
                                        }
                                        CloseClipboard();
                                    }
                                }
                                return 0;
                            }
                        }
                    }
                }
            }
            else
            {
                // Right-click in Canvas area: show stream context menu
                int clickedStreamIdx = -1;
                for (int i = 0; i < (int)pState->streamCardRects.size(); ++i)
                {
                    if (PtInRect(&pState->streamCardRects[i], pt))
                    {
                        clickedStreamIdx = i;
                        break;
                    }
                }
                POINT ptScreen = pt;
                ClientToScreen(hWnd, &ptScreen);
                ShowStreamContextMenu(pState, ptScreen.x, ptScreen.y, clickedStreamIdx);
                return 0;
            }
        }
        break;
    }

    case WM_SYSKEYDOWN:
    {
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (bAlt && (wParam == 'V' || wParam == 'v'))
        {
            App_ShowClipboardContents(hWnd);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (pState)
        {
            if (wParam == VK_ESCAPE)
            {
                if (pState->isDraggingStreams)
                {
                    pState->isDraggingStreams = false;
                    pState->isPotentialDrag = false;
                    FluentDragGhost::Hide();
                    ReleaseCapture();
                    pState->dropTargetIndex = -1;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
            else if (wParam == VK_DELETE)
            {
                DeleteSelectedStreams(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            else if (wParam == VK_SPACE)
            {
                if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
                {
                    auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
                    int targetIdx = -1;
                    for (int i = 0; i < (int)grp.streams.size(); ++i)
                    {
                        if (grp.streams[i].isSelected) { targetIdx = i; break; }
                    }
                    if (targetIdx >= 0)
                    {
                        auto& st = grp.streams[targetIdx];
                        if (st.isPlayingPreview)
                        {
                            StopAudio();
                            st.isPlayingPreview = false;
                        }
                        else
                        {
                            StopAudio();
                            for (auto& s : grp.streams) s.isPlayingPreview = false;
                            std::wstring wavPath;
                            for (const auto& tr : st.triggers)
                            {
                                for (const auto& f : tr.files)
                                {
                                    if (f.existsOnDisk) { wavPath = f.resolvedDiskPath; break; }
                                }
                                if (!wavPath.empty()) break;
                            }
                            if (!wavPath.empty()) st.isPlayingPreview = PlayAudio(wavPath, true);
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                }
            }

            bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (altDown)
            {
                if (wParam == 'V' || wParam == 'v')
                {
                    App_ShowClipboardContents(hWnd);
                    return 0;
                }
                else if (wParam == VK_UP)
                {
                    MoveSelectedStreamsUp(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == VK_DOWN)
                {
                    MoveSelectedStreamsDown(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }

            bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrlDown)
            {
                if (wParam == 'Z' || wParam == 'z')
                {
                    bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (shiftDown)
                    {
                        Sms_PerformRedo(pState);
                    }
                    else
                    {
                        Sms_PerformUndo(pState);
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'Y' || wParam == 'y')
                {
                    Sms_PerformRedo(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'A' || wParam == 'a')
                {
                    SelectAllStreams(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'C' || wParam == 'c')
                {
                    CopySelectedStreams(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'V' || wParam == 'v')
                {
                    PasteClipboardStreams(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'X' || wParam == 'x')
                {
                    CutSelectedStreams(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else if (wParam == 'D' || wParam == 'd')
                {
                    DuplicateSelectedStreams(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Double buffer
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HGDIOBJ oldBm = SelectObject(memDC, hbm);

        SetBkMode(memDC, TRANSPARENT);
        pState->hitRegions.clear();
        pState->treeHitRegions.clear();

        // 1. Clear background
        HBRUSH bgBrush = CreateSolidBrush(UITheme::DarkBackground);
        FillRect(memDC, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        // 2. Render Sidebar Context Tree
        RECT rcSidebar = { 0, 0, pState->sidebarWidth, rcClient.bottom };
        HBRUSH sbBrush = CreateSolidBrush(RGB(22, 23, 27));
        FillRect(memDC, &rcSidebar, sbBrush);
        DeleteObject(sbBrush);

        // Sidebar vertical line
        HPEN hLinePen = CreatePen(PS_SOLID, 1, RGB(44, 46, 56));
        HGDIOBJ oldPen = SelectObject(memDC, hLinePen);
        MoveToEx(memDC, pState->sidebarWidth, 0, NULL);
        LineTo(memDC, pState->sidebarWidth, rcClient.bottom);
        SelectObject(memDC, oldPen);
        DeleteObject(hLinePen);

        int curTreeY = 12 - pState->treeScrollY;
        for (int i = 0; i < (int)pState->treeItems.size(); ++i)
        {
            const auto& item = pState->treeItems[i];
            RECT rcItem = { 10, curTreeY, pState->sidebarWidth - 14, curTreeY + 28 };

            if (item.type == ContextTreeItem::Header)
            {
                rcItem.bottom = curTreeY + 24;
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(120, 140, 170));
                DrawTextW(memDC, item.label.c_str(), -1, &rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                curTreeY += 26;
            }
            else if (item.type == ContextTreeItem::Unit)
            {
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(220, 225, 235));
                DrawTextW(memDC, item.label.c_str(), -1, &rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                curTreeY += 28;
            }
            else // SoundSlot or Standalone
            {
                RECT rcSlot = { 22, curTreeY, pState->sidebarWidth - 14, curTreeY + 26 };
                bool isHover = PtInRect(&rcSlot, pState->ptMouse);

                if (item.isSelected)
                {
                    HBRUSH selBr = CreateSolidBrush(RGB(32, 64, 110));
                    FillRect(memDC, &rcSlot, selBr);
                    DeleteObject(selBr);
                }
                else if (isHover)
                {
                    HBRUSH hovBr = CreateSolidBrush(RGB(34, 36, 44));
                    FillRect(memDC, &rcSlot, hovBr);
                    DeleteObject(hovBr);
                }

                // Dot indicator
                COLORREF dotCol = item.exists ? RGB(40, 200, 80) : RGB(230, 60, 60);
                HBRUSH dotBr = CreateSolidBrush(dotCol);
                RECT rcDot = { rcSlot.left + 4, rcSlot.top + 9, rcSlot.left + 12, rcSlot.top + 17 };
                HGDIOBJ oB = SelectObject(memDC, dotBr);
                HGDIOBJ oP = SelectObject(memDC, GetStockObject(NULL_PEN));
                Ellipse(memDC, rcDot.left, rcDot.top, rcDot.right, rcDot.bottom);
                SelectObject(memDC, oB);
                SelectObject(memDC, oP);
                DeleteObject(dotBr);

                RECT rcText = { rcSlot.left + 18, rcSlot.top, rcSlot.right - 4, rcSlot.bottom };
                SelectObject(memDC, item.isSelected ? pState->hFontBold : pState->hFontBody);
                SetTextColor(memDC, item.isSelected ? RGB(255, 255, 255) : (item.exists ? RGB(190, 200, 215) : RGB(220, 140, 140)));
                DrawTextW(memDC, item.label.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                pState->treeHitRegions.push_back({ rcSlot, i });
                curTreeY += 28;
            }
        }
        pState->totalTreeHeight = curTreeY + pState->treeScrollY + 20;
        pState->treeScrollBar.SetRange(0, pState->totalTreeHeight, rcClient.bottom);
        pState->treeScrollBar.Paint(memDC, true);

        // 3. Render Canvas Header (Top bar)
        RECT rcTopBar = { pState->sidebarWidth + 1, 0, rcClient.right, pState->headerHeight };
        HBRUSH topBr = CreateSolidBrush(RGB(28, 30, 36));
        FillRect(memDC, &rcTopBar, topBr);
        DeleteObject(topBr);

        // Bottom border of top bar
        HPEN hTopPen = CreatePen(PS_SOLID, 1, RGB(44, 46, 56));
        oldPen = SelectObject(memDC, hTopPen);
        MoveToEx(memDC, rcTopBar.left, rcTopBar.bottom, NULL);
        LineTo(memDC, rcTopBar.right, rcTopBar.bottom);
        SelectObject(memDC, oldPen);
        DeleteObject(hTopPen);

        // Calculate selected count and clipboard count
        int selCount = 0;
        int totalStreamsInGroup = 0;
        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
        {
            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];
            totalStreamsInGroup = (int)grp.streams.size();
            for (const auto& s : grp.streams)
            {
                if (s.isSelected) ++selCount;
            }
        }
        int clipCount = (int)s_streamClipboard.streams.size();

        // Action Buttons: Save, Open, Folder, Select All, Copy, Paste, Expand All, Collapse All (drawn right-to-left)
        int btnRight = rcTopBar.right - 20;

        auto drawBtn = [&](const std::wstring& txt, ClickableRegion::Type cType, int minW,
            COLORREF bgNormal = RGB(38, 40, 50),
            COLORREF borderNormal = RGB(56, 60, 75),
            COLORREF textNormal = RGB(220, 225, 235)) -> RECT {
            SelectObject(memDC, pState->hFontBold);
            SIZE szText = { 0 };
            GetTextExtentPoint32W(memDC, txt.c_str(), (int)txt.length(), &szText);
            int w = (std::max)(minW, (int)szText.cx + 26);
            RECT rcB = { btnRight - w, 12, btnRight, 40 };
            btnRight -= (w + 8);
            bool hov = PtInRect(&rcB, pState->ptMouse);
            HBRUSH bBr = CreateSolidBrush(hov ? RGB(48, 52, 66) : bgNormal);
            HPEN bPen = CreatePen(PS_SOLID, 1, hov ? RGB(80, 140, 220) : borderNormal);
            HGDIOBJ oB = SelectObject(memDC, bBr);
            HGDIOBJ oP = SelectObject(memDC, bPen);
            RoundRect(memDC, rcB.left, rcB.top, rcB.right, rcB.bottom, 4, 4);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(bBr);
            DeleteObject(bPen);

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, hov ? RGB(255, 255, 255) : textNormal);
            DrawTextW(memDC, txt.c_str(), -1, &rcB, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            pState->hitRegions.push_back({ cType, rcB, 0, 0, L"" });
            return rcB;
        };

        // Draw buttons from right to left so they appear left-to-right as:
        // [💾 Save SMS] [📂 Open...] [📁 Open Folder] [✓ Select All] [📋 Copy (N)] [📋 Paste (M)] [▾ Expand All] [▸ Collapse All]
        drawBtn(L"▸ Collapse All", ClickableRegion::CollapseAllBtn, 105);
        drawBtn(L"▾ Expand All", ClickableRegion::ExpandAllBtn, 95);

        if (clipCount > 0)
        {
            std::wstring pasteTxt = L"📋 Paste (" + std::to_wstring(clipCount) + L")";
            drawBtn(pasteTxt, ClickableRegion::PasteStreamsBtn, 105, RGB(26, 42, 60), RGB(40, 130, 220), RGB(130, 200, 255));
        }

        if (selCount > 0)
        {
            std::wstring copyTxt = L"📋 Copy (" + std::to_wstring(selCount) + L")";
            drawBtn(copyTxt, ClickableRegion::CopyStreamsBtn, 100, RGB(26, 50, 36), RGB(45, 160, 85), RGB(130, 240, 165));
        }

        if (totalStreamsInGroup > 0)
        {
            bool allSel = (selCount == totalStreamsInGroup);
            std::wstring selTxt = allSel ? L"✕ Deselect All" : L"✓ Select All";
            drawBtn(selTxt, ClickableRegion::SelectAllStreamsBtn, 110);
        }

        drawBtn(L"📁 Open Folder", ClickableRegion::OpenFolderBtn, 115);
        drawBtn(L"📂 Open...", ClickableRegion::OpenFileBtn, 90);
        drawBtn(L"➕ New SMS", ClickableRegion::NewSmsBtn, 100, RGB(22, 52, 85), RGB(40, 130, 220), RGB(160, 220, 255));
        drawBtn(L"💾 Save SMS", ClickableRegion::SaveBtn, 105,
            pState->doc.isModified ? RGB(52, 38, 18) : RGB(38, 40, 50),
            pState->doc.isModified ? RGB(220, 150, 30) : RGB(56, 60, 75),
            pState->doc.isModified ? RGB(255, 210, 120) : RGB(220, 225, 235));
        drawBtn(L"📦 Consolidate WAVs...", ClickableRegion::ConsolidateWavsBtn, 160,
            RGB(35, 30, 55), RGB(130, 80, 220), RGB(215, 185, 255));

        // Top bar line 1: File name & Action buttons
        size_t slashIdx = pState->doc.filePath.find_last_of(L"\\/");
        std::wstring fileName = (slashIdx != std::wstring::npos) ? pState->doc.filePath.substr(slashIdx + 1) : (pState->doc.filePath.empty() ? L"No SMS File Open" : pState->doc.filePath);
        if (pState->doc.isModified) fileName += L" ●";

        RECT rcTitle = { rcTopBar.left + 16, 8, btnRight - 12, 32 };
        SelectObject(memDC, pState->hFontTitle);
        SetTextColor(memDC, RGB(255, 255, 255));
        DrawTextW(memDC, fileName.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rcPathSub = { rcTopBar.left + 16, 30, btnRight - 12, 48 };
        bool hovPath = PtInRect(&rcPathSub, pState->ptMouse);
        SelectObject(memDC, pState->hFontSubtitle);
        SetTextColor(memDC, hovPath ? RGB(100, 180, 255) : RGB(130, 140, 160));
        DrawTextW(memDC, pState->doc.filePath.c_str(), -1, &rcPathSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (!pState->doc.filePath.empty())
        {
            pState->hitRegions.push_back({ ClickableRegion::OpenFolderBtn, rcPathSub, 0, 0, pState->doc.filePath });
        }

        // Top bar line 2: Scalability Group Tabs
        int tabX = rcTopBar.left + 16;
        for (int g = 0; g < (int)pState->doc.groups.size(); ++g)
        {
            const auto& grp = pState->doc.groups[g];
            std::wstring tabText = L"SG " + std::to_wstring(grp.groupLevel);
            if (grp.groupLevel == 5) tabText += L" (High)";
            else if (grp.groupLevel == 3) tabText += L" (Medium)";
            else if (grp.groupLevel == 1) tabText += L" (Low)";
            else if (grp.groupLevel == 0) tabText += L" (AI Trains)";

            bool isCur = (g == pState->doc.activeGroupIndex);
            SelectObject(memDC, isCur ? pState->hFontBold : pState->hFontBody);
            SIZE szTab = { 0, 0 };
            GetTextExtentPoint32W(memDC, tabText.c_str(), (int)tabText.size(), &szTab);
            int tabW = (std::max)(110, (int)szTab.cx + 24);

            RECT rcTab = { tabX, 54, tabX + tabW, 84 };
            tabX += tabW + 7;
            bool isHov = PtInRect(&rcTab, pState->ptMouse);

            HBRUSH tBr = CreateSolidBrush(isCur ? RGB(0, 120, 215) : (isHov ? RGB(45, 48, 60) : RGB(34, 36, 45)));
            HPEN tPen = CreatePen(PS_SOLID, 1, isCur ? RGB(0, 160, 255) : RGB(50, 54, 66));
            HGDIOBJ oB = SelectObject(memDC, tBr);
            HGDIOBJ oP = SelectObject(memDC, tPen);
            RoundRect(memDC, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom, 4, 4);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(tBr);
            DeleteObject(tPen);

            SelectObject(memDC, isCur ? pState->hFontBold : pState->hFontBody);
            SetTextColor(memDC, isCur ? RGB(255, 255, 255) : RGB(190, 200, 215));
            DrawTextW(memDC, tabText.c_str(), -1, &rcTab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            pState->hitRegions.push_back({ ClickableRegion::SgTab, rcTab, g, 0, L"" });
        }

        // Active Scalability Group details pill & Streams Count Badge
        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
        {
            const auto& curGrp = pState->doc.groups[pState->doc.activeGroupIndex];
            int actualStreams = (int)curGrp.streams.size();
            bool isMismatch = (curGrp.declaredStreams != actualStreams);

            // 1. Streams Count Badge: shows both defined Streams( N ) and actual parsed streams
            std::wstring stBadgeText;
            if (!isMismatch)
            {
                stBadgeText = L"Streams: " + std::to_wstring(curGrp.declaredStreams) + L" declared / " + std::to_wstring(actualStreams) + L" parsed ✓";
            }
            else
            {
                stBadgeText = L"⚠ Streams: " + std::to_wstring(curGrp.declaredStreams) + L" declared / " + std::to_wstring(actualStreams) + L" actual (Mismatch)";
            }

            SelectObject(memDC, pState->hFontBold);
            SIZE szBadge;
            GetTextExtentPoint32W(memDC, stBadgeText.c_str(), (int)stBadgeText.size(), &szBadge);
            int badgeW = szBadge.cx + 20;
            RECT rcStBadge = { tabX + 12, 54, tabX + 12 + badgeW, 84 };

            COLORREF bgCol = isMismatch ? RGB(55, 36, 16) : RGB(20, 48, 26);
            COLORREF borderCol = isMismatch ? RGB(220, 150, 30) : RGB(45, 125, 55);
            COLORREF textCol = isMismatch ? RGB(255, 210, 120) : RGB(100, 225, 130);

            HBRUSH bBr = CreateSolidBrush(bgCol);
            HPEN bPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oB = SelectObject(memDC, bBr);
            HGDIOBJ oP = SelectObject(memDC, bPen);
            RoundRect(memDC, rcStBadge.left, rcStBadge.top, rcStBadge.right, rcStBadge.bottom, 4, 4);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(bBr);
            DeleteObject(bPen);

            SetTextColor(memDC, textCol);
            DrawTextW(memDC, stBadgeText.c_str(), -1, &rcStBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            if (isMismatch)
            {
                pState->hitRegions.push_back({ ClickableRegion::FixCountBtn, rcStBadge, 0, 0, L"" });
            }

            // 2. Activation, Distance, TrackType, Volume, Stereo Info
            std::wstring sgInfo = L"Activation: [ ";
            if (curGrp.hasCabCam) sgInfo += L"CabCam ";
            if (curGrp.hasPassengerCam) sgInfo += L"PassengerCam ";
            if (curGrp.hasExternalCam) sgInfo += L"ExternalCam ";
            sgInfo += L"]  Dist: " + std::to_wstring((int)curGrp.activationDistance) + L"m / " + std::to_wstring((int)curGrp.deactivationDistance) + L"m";
            if (curGrp.trackType >= 0)
            {
                sgInfo += L"  TrackType: " + std::to_wstring(curGrp.trackType);
                if (curGrp.trackType == 0) sgInfo += L" (Default)";
            }
            if (curGrp.hasGroupVolume)
            {
                wchar_t gv[32]; swprintf_s(gv, L"  Master Vol: %.2f", curGrp.groupVolume);
                sgInfo += gv;
            }
            if (curGrp.isStereo) sgInfo += L"  Stereo: Yes";

            RECT rcSgInfo = { rcStBadge.right + 12, 54, rcTopBar.right - 20, 84 };
            bool hovSgInfo = PtInRect(&rcSgInfo, pState->ptMouse);
            SelectObject(memDC, pState->hFontBody);
            SetTextColor(memDC, hovSgInfo ? RGB(110, 190, 255) : RGB(140, 160, 185));
            DrawTextW(memDC, sgInfo.c_str(), -1, &rcSgInfo, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            pState->hitRegions.push_back({ ClickableRegion::SgPropsCardToggle, rcSgInfo, 0, 0, L"" });
        }

        // 4. Render Canvas Body (Scalability Group Properties & Encapsulated Streams Deck)
        RECT rcCanvas = { pState->sidebarWidth + 1, pState->headerHeight, rcClient.right, rcClient.bottom };
        int curY = rcCanvas.top + 10;

        if (pState->doc.activeGroupIndex >= 0 && pState->doc.activeGroupIndex < (int)pState->doc.groups.size())
        {
            auto& grp = pState->doc.groups[pState->doc.activeGroupIndex];

            // 4a. Check stream count mismatch banner (Dynamic multi-line height)
            int actualStreams = (int)grp.streams.size();
            bool hasRepairs = !pState->doc.autoRepairLog.empty();
            bool hasMismatch = (grp.declaredStreams != actualStreams);

            if (hasMismatch || hasRepairs)
            {
                std::wstring warnTitle;
                std::wstring warnDetail;

                if (hasRepairs)
                {
                    warnTitle = L"⚠ Open Rails Structural Defects Detected (" + std::to_wstring(pState->doc.autoRepairLog.size()) +
                        L" Auto-Healed in Memory):";
                    for (size_t r = 0; r < min((size_t)2, pState->doc.autoRepairLog.size()); ++r)
                    {
                        if (r > 0) warnDetail += L" • ";
                        warnDetail += pState->doc.autoRepairLog[r];
                    }
                    if (pState->doc.autoRepairLog.size() > 2)
                    {
                        warnDetail += L" (+ " + std::to_wstring(pState->doc.autoRepairLog.size() - 2) + L" more)";
                    }
                    if (hasMismatch)
                    {
                        warnDetail += L" • Streams count auto-updated to " + std::to_wstring(actualStreams);
                    }
                    warnDetail += L" • Click 'Save Healed SMS' to persist clean file to disk.";
                }
                else
                {
                    warnTitle = L"⚠ Streams Count Mismatch: Defined in Streams ( " + std::to_wstring(grp.declaredStreams) +
                        L" ), but " + std::to_wstring(actualStreams) + L" Stream blocks are parsed.";
                    if (actualStreams > grp.declaredStreams)
                        warnDetail = L"Open Rails will only load the first " + std::to_wstring(grp.declaredStreams) +
                            L" streams; streams #" + std::to_wstring(grp.declaredStreams + 1) + L" to #" + std::to_wstring(actualStreams) + L" are SILENTLY DROPPED!";
                    else
                        warnDetail = L"Open Rails expects " + std::to_wstring(grp.declaredStreams) +
                            L" streams, which will cause playback warnings or missing sound stream slots.";
                }

                std::wstring warnFullText = warnTitle + L"\r\n" + warnDetail;

                // Calculate available text width
                int totalBannerW = (rcCanvas.right - 20) - (rcCanvas.left + 16);
                int btnW = hasRepairs ? 165 : 145;
                int textAvailW = max(100, totalBannerW - btnW - 36);

                // Dynamically calculate height needed for multi-line text
                RECT rcCalc = { 0, 0, textAvailW, 0 };
                SelectObject(memDC, pState->hFontBold);
                DrawTextW(memDC, warnFullText.c_str(), -1, &rcCalc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                int textH = rcCalc.bottom - rcCalc.top;
                int bannerH = max(54, textH + 20);

                RECT rcBanner = { rcCanvas.left + 16, curY, rcCanvas.right - 20, curY + bannerH };
                COLORREF bgCol = hasRepairs ? RGB(48, 18, 22) : RGB(50, 36, 16);
                COLORREF borderCol = hasRepairs ? RGB(225, 60, 70) : RGB(220, 150, 30);
                COLORREF textCol = hasRepairs ? RGB(255, 205, 215) : RGB(255, 210, 120);

                HBRUSH banBr = CreateSolidBrush(bgCol);
                HPEN banPen = CreatePen(PS_SOLID, 1, borderCol);
                HGDIOBJ oB = SelectObject(memDC, banBr);
                HGDIOBJ oP = SelectObject(memDC, banPen);
                RoundRect(memDC, rcBanner.left, rcBanner.top, rcBanner.right, rcBanner.bottom, 6, 6);
                SelectObject(memDC, oB);
                SelectObject(memDC, oP);
                DeleteObject(banBr);
                DeleteObject(banPen);

                // Multi-line text area
                RECT rcWarnText = { rcBanner.left + 14, rcBanner.top + 10, rcBanner.right - btnW - 20, rcBanner.bottom - 10 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, textCol);
                DrawTextW(memDC, warnFullText.c_str(), -1, &rcWarnText, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                // Vertically centered Fix Button
                int btnH = 32;
                int btnTop = rcBanner.top + (bannerH - btnH) / 2;
                RECT rcFixBtn = { rcBanner.right - btnW - 10, btnTop, rcBanner.right - 10, btnTop + btnH };
                bool hovFix = PtInRect(&rcFixBtn, pState->ptMouse);

                HBRUSH fBr = CreateSolidBrush(hasRepairs ? (hovFix ? RGB(40, 190, 125) : RGB(28, 150, 95)) : (hovFix ? RGB(240, 160, 40) : RGB(200, 130, 20)));
                HPEN fPen = CreatePen(PS_SOLID, 1, hasRepairs ? (hovFix ? RGB(110, 255, 190) : RGB(45, 175, 115)) : (hovFix ? RGB(255, 200, 80) : RGB(220, 150, 30)));
                HGDIOBJ obF = SelectObject(memDC, fBr);
                HGDIOBJ opF = SelectObject(memDC, fPen);
                RoundRect(memDC, rcFixBtn.left, rcFixBtn.top, rcFixBtn.right, rcFixBtn.bottom, 4, 4);
                SelectObject(memDC, obF);
                SelectObject(memDC, opF);
                DeleteObject(fBr);
                DeleteObject(fPen);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, hasRepairs ? RGB(255, 255, 255) : RGB(20, 20, 20));
                std::wstring fixLbl = hasRepairs ? L"💾 Save Healed SMS" : (L"Fix Count to " + std::to_wstring(actualStreams));
                DrawTextW(memDC, fixLbl.c_str(), -1, &rcFixBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                pState->hitRegions.push_back({ ClickableRegion::FixCountBtn, rcFixBtn, 0, 0, L"" });
                curY += bannerH + 12;
            }

            // 4a-2. Render Scalability Group Properties & Environment Card
            int sgCardLeft = rcCanvas.left + 16;
            int sgCardRight = rcCanvas.right - 20;
            int sgCardW = sgCardRight - sgCardLeft;
            int sgCardH = grp.isSgPropsCollapsed ? 42 : 288;

            RECT rcSgCard = { sgCardLeft, curY, sgCardRight, curY + sgCardH };

            // Card background & border
            HBRUSH sgBr = CreateSolidBrush(RGB(28, 32, 42));
            HPEN sgPen = CreatePen(PS_SOLID, 1, RGB(55, 65, 85));
            HGDIOBJ oB_sg = SelectObject(memDC, sgBr);
            HGDIOBJ oP_sg = SelectObject(memDC, sgPen);
            RoundRect(memDC, rcSgCard.left, rcSgCard.top, rcSgCard.right, rcSgCard.bottom, 6, 6);
            SelectObject(memDC, oB_sg);
            SelectObject(memDC, oP_sg);
            DeleteObject(sgBr);
            DeleteObject(sgPen);

            // Card Header Row (40px)
            RECT rcSgHdr = { rcSgCard.left, rcSgCard.top, rcSgCard.right, rcSgCard.top + 40 };
            HBRUSH sgHdrBr = CreateSolidBrush(RGB(34, 40, 54));
            FillRect(memDC, &rcSgHdr, sgHdrBr);
            DeleteObject(sgHdrBr);

            // Left icon & title
            RECT rcSgHdrTitle = { rcSgHdr.left + 14, rcSgHdr.top, rcSgHdr.left + 460, rcSgHdr.bottom };
            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, RGB(100, 190, 255));
            std::wstring sgDesc = L"";
            if (grp.groupLevel == 5) sgDesc = L" (High Quality - Player)";
            else if (grp.groupLevel == 3) sgDesc = L" (Medium Quality)";
            else if (grp.groupLevel == 1) sgDesc = L" (Low Quality)";
            else if (grp.groupLevel == 0) sgDesc = L" (AI Trains / Traffic)";
            std::wstring sgTitle = L"⚙ Scalability Group " + std::to_wstring(grp.groupLevel) + sgDesc + L" Properties & Rules";
            DrawTextW(memDC, sgTitle.c_str(), -1, &rcSgHdrTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // If collapsed, display a concise summary in header
            if (grp.isSgPropsCollapsed)
            {
                std::wstring sgSumm = L"Activation: [ ";
                if (grp.hasCabCam) sgSumm += L"CabCam ";
                if (grp.hasPassengerCam) sgSumm += L"PassengerCam ";
                if (grp.hasExternalCam) sgSumm += L"ExternalCam ";
                sgSumm += L"]  Dist: " + std::to_wstring((int)grp.activationDistance) + L"m";
                if (grp.trackType >= 0) sgSumm += L"  Track: " + std::to_wstring(grp.trackType);
                sgSumm += L"  |  Deactivation: [ ";
                if (grp.deactCabCam) sgSumm += L"CabCam ";
                if (grp.deactPassengerCam) sgSumm += L"PassengerCam ";
                if (grp.deactExternalCam) sgSumm += L"ExternalCam ";
                sgSumm += L"]  Cutoff: " + std::to_wstring((int)grp.deactivationDistance) + L"m";
                int dTt = (grp.deactTrackType >= 0) ? grp.deactTrackType : grp.trackType;
                if (dTt >= 0) sgSumm += L"  Track: " + std::to_wstring(dTt);
                if (grp.hasGroupVolume) { wchar_t vb[32]; swprintf_s(vb, L"  |  Vol: %.2f", grp.groupVolume); sgSumm += vb; }
                if (grp.isStereo) sgSumm += L"  |  Stereo";

                RECT rcSumm = { rcSgHdrTitle.right + 10, rcSgHdr.top, rcSgHdr.right - 110, rcSgHdr.bottom };
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, RGB(140, 155, 175));
                DrawTextW(memDC, sgSumm.c_str(), -1, &rcSumm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }

            // Right Expand / Collapse Button
            RECT rcSgTglBtn = { rcSgHdr.right - 100, rcSgHdr.top + 6, rcSgHdr.right - 10, rcSgHdr.bottom - 6 };
            bool hovSgTgl = PtInRect(&rcSgTglBtn, pState->ptMouse);
            HBRUSH tglBr = CreateSolidBrush(hovSgTgl ? RGB(48, 56, 75) : RGB(38, 44, 60));
            HPEN tglPen = CreatePen(PS_SOLID, 1, hovSgTgl ? RGB(90, 150, 240) : RGB(60, 70, 95));
            SelectObject(memDC, tglBr); SelectObject(memDC, tglPen);
            RoundRect(memDC, rcSgTglBtn.left, rcSgTglBtn.top, rcSgTglBtn.right, rcSgTglBtn.bottom, 4, 4);
            DeleteObject(tglBr); DeleteObject(tglPen);

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, hovSgTgl ? RGB(255, 255, 255) : RGB(190, 205, 230));
            std::wstring tglTxt = grp.isSgPropsCollapsed ? L"▾ Expand" : L"▴ Collapse";
            DrawTextW(memDC, tglTxt.c_str(), -1, &rcSgTglBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            pState->hitRegions.push_back({ ClickableRegion::SgPropsCardToggle, rcSgHdr, 0, 0, L"" });

            if (!grp.isSgPropsCollapsed)
            {
                HPEN divPen = CreatePen(PS_SOLID, 1, RGB(45, 52, 68));
                HGDIOBJ oldDivPen = SelectObject(memDC, divPen);
                MoveToEx(memDC, rcSgCard.left, rcSgHdr.bottom, NULL);
                LineTo(memDC, rcSgCard.right, rcSgHdr.bottom);

                // ========================================================
                // ROW 1: Group Level & Master Audio
                // ========================================================
                int r1Y = rcSgHdr.bottom + 8;
                int r1H = 34;

                // Label: Group Level:
                RECT rcLblLevel = { rcSgCard.left + 16, r1Y, rcSgCard.left + 105, r1Y + r1H };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Group Level:", -1, &rcLblLevel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int lvlX = rcLblLevel.right + 4;
                struct LevelBtnDef { int lvl; const wchar_t* label; int width; };
                LevelBtnDef lvlBtns[] = {
                    { 5, L"5 (High)", 76 },
                    { 3, L"3 (Med)", 72 },
                    { 1, L"1 (Low)", 70 },
                    { 0, L"0 (AI Trains)", 92 },
                    { -2, L"Custom...", 82 }
                };

                for (const auto& lb : lvlBtns)
                {
                    bool isSelected = (lb.lvl == -2) ? (grp.groupLevel != 5 && grp.groupLevel != 3 && grp.groupLevel != 1 && grp.groupLevel != 0) : (grp.groupLevel == lb.lvl);
                    RECT rcLvl = { lvlX, r1Y + 2, lvlX + lb.width, r1Y + r1H - 2 };
                    lvlX += lb.width + 6;

                    bool hovL = PtInRect(&rcLvl, pState->ptMouse);
                    HBRUSH lBr = CreateSolidBrush(isSelected ? RGB(0, 120, 215) : (hovL ? RGB(45, 50, 65) : RGB(34, 38, 50)));
                    HPEN lPen = CreatePen(PS_SOLID, 1, isSelected ? RGB(60, 160, 255) : (hovL ? RGB(80, 100, 130) : RGB(52, 58, 76)));
                    SelectObject(memDC, lBr); SelectObject(memDC, lPen);
                    RoundRect(memDC, rcLvl.left, rcLvl.top, rcLvl.right, rcLvl.bottom, 4, 4);
                    DeleteObject(lBr); DeleteObject(lPen);

                    SelectObject(memDC, isSelected ? pState->hFontBold : pState->hFontBody);
                    SetTextColor(memDC, isSelected ? RGB(255, 255, 255) : RGB(190, 200, 220));
                    std::wstring btnStr = (lb.lvl == -2 && isSelected) ? (L"Custom (" + std::to_wstring(grp.groupLevel) + L")") : lb.label;
                    DrawTextW(memDC, btnStr.c_str(), -1, &rcLvl, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::SgLevelBtn, rcLvl, lb.lvl, 0, L"" });
                }

                // Vertical separator in Row 1
                int sep1X = lvlX + 12;
                MoveToEx(memDC, sep1X, r1Y + 4, NULL);
                LineTo(memDC, sep1X, r1Y + r1H - 4);

                // Master Volume Checkbox & Label
                int volX = sep1X + 16;
                RECT rcVolChk = { volX, r1Y + 7, volX + 20, r1Y + 27 };
                bool hovVolChk = PtInRect(&rcVolChk, pState->ptMouse);
                HBRUSH vChkBr = CreateSolidBrush(grp.hasGroupVolume ? RGB(0, 120, 215) : (hovVolChk ? RGB(45, 50, 65) : RGB(30, 34, 46)));
                HPEN vChkPen = CreatePen(PS_SOLID, 1, grp.hasGroupVolume ? RGB(60, 160, 255) : RGB(60, 68, 88));
                SelectObject(memDC, vChkBr); SelectObject(memDC, vChkPen);
                RoundRect(memDC, rcVolChk.left, rcVolChk.top, rcVolChk.right, rcVolChk.bottom, 3, 3);
                DeleteObject(vChkBr); DeleteObject(vChkPen);
                if (grp.hasGroupVolume)
                {
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    DrawTextW(memDC, L"✓", -1, &rcVolChk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                pState->hitRegions.push_back({ ClickableRegion::SgMasterVolToggle, rcVolChk, 0, 0, L"" });

                RECT rcLblVol = { rcVolChk.right + 8, r1Y, rcVolChk.right + 95, r1Y + r1H };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, grp.hasGroupVolume ? RGB(210, 225, 245) : RGB(130, 140, 155));
                DrawTextW(memDC, L"Master Vol:", -1, &rcLblVol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Volume [-] [ 1.00 ] [+]
                int vbX = rcLblVol.right + 4;
                RECT rcVolDown = { vbX, r1Y + 3, vbX + 26, r1Y + r1H - 3 };
                RECT rcVolVal = { rcVolDown.right + 4, r1Y + 3, rcVolDown.right + 65, r1Y + r1H - 3 };
                RECT rcVolUp = { rcVolVal.right + 4, r1Y + 3, rcVolVal.right + 30, r1Y + r1H - 3 };

                bool hovVd = PtInRect(&rcVolDown, pState->ptMouse);
                HBRUSH vdB = CreateSolidBrush(hovVd ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN vdP = CreatePen(PS_SOLID, 1, hovVd ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, vdB); SelectObject(memDC, vdP);
                RoundRect(memDC, rcVolDown.left, rcVolDown.top, rcVolDown.right, rcVolDown.bottom, 3, 3);
                DeleteObject(vdB); DeleteObject(vdP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"-", -1, &rcVolDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgMasterVolBtn, rcVolDown, -1, 0, L"" });

                bool hovVv = PtInRect(&rcVolVal, pState->ptMouse);
                HBRUSH vvB = CreateSolidBrush(hovVv ? RGB(45, 52, 70) : RGB(30, 34, 46));
                HPEN vvP = CreatePen(PS_SOLID, 1, hovVv ? RGB(90, 160, 240) : RGB(55, 65, 88));
                SelectObject(memDC, vvB); SelectObject(memDC, vvP);
                RoundRect(memDC, rcVolVal.left, rcVolVal.top, rcVolVal.right, rcVolVal.bottom, 3, 3);
                DeleteObject(vvB); DeleteObject(vvP);
                wchar_t vStr[32]; swprintf_s(vStr, L"%.2f", grp.groupVolume);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, grp.hasGroupVolume ? RGB(255, 255, 255) : RGB(140, 150, 165));
                DrawTextW(memDC, vStr, -1, &rcVolVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgMasterVolBtn, rcVolVal, 0, 0, L"" });

                bool hovVu = PtInRect(&rcVolUp, pState->ptMouse);
                HBRUSH vuB = CreateSolidBrush(hovVu ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN vuP = CreatePen(PS_SOLID, 1, hovVu ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, vuB); SelectObject(memDC, vuP);
                RoundRect(memDC, rcVolUp.left, rcVolUp.top, rcVolUp.right, rcVolUp.bottom, 3, 3);
                DeleteObject(vuB); DeleteObject(vuP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"+", -1, &rcVolUp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgMasterVolBtn, rcVolUp, 1, 0, L"" });

                // Stereo Toggle Pill
                int stX = rcVolUp.right + 20;
                RECT rcStereo = { stX, r1Y + 3, stX + 125, r1Y + r1H - 3 };
                bool hovSt = PtInRect(&rcStereo, pState->ptMouse);
                HBRUSH stB = CreateSolidBrush(grp.isStereo ? RGB(22, 54, 34) : (hovSt ? RGB(45, 50, 65) : RGB(34, 38, 50)));
                HPEN stP = CreatePen(PS_SOLID, 1, grp.isStereo ? RGB(45, 160, 85) : (hovSt ? RGB(80, 100, 130) : RGB(52, 58, 76)));
                SelectObject(memDC, stB); SelectObject(memDC, stP);
                RoundRect(memDC, rcStereo.left, rcStereo.top, rcStereo.right, rcStereo.bottom, 4, 4);
                DeleteObject(stB); DeleteObject(stP);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, grp.isStereo ? RGB(110, 240, 150) : RGB(170, 180, 195));
                std::wstring stereoLbl = grp.isStereo ? L"✓ Stereo Audio" : L"Stereo Audio";
                DrawTextW(memDC, stereoLbl.c_str(), -1, &rcStereo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgStereoToggle, rcStereo, 0, 0, L"" });

                // ========================================================
                // SECTION 2: ACTIVATION RULES (Two Spacious Sub-Rows)
                // ========================================================
                int actY = r1Y + r1H + 10;
                MoveToEx(memDC, rcSgCard.left + 14, actY - 5, NULL);
                LineTo(memDC, rcSgCard.right - 14, actY - 5);

                // Section Title Tag
                RECT rcActTag = { rcSgCard.left + 16, actY, rcSgCard.right - 16, actY + 22 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(70, 215, 130));
                DrawTextW(memDC, L"▶ ACTIVATION RULES (Sound Trigger Conditions)", -1, &rcActTag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Sub-row 2A: Camera Views
                int aRow1Y = actY + 24;
                RECT rcLblActCam = { rcSgCard.left + 24, aRow1Y, rcSgCard.left + 140, aRow1Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Camera Views:", -1, &rcLblActCam, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                auto drawActCamPill = [&](int x, int w, const std::wstring& name, bool active, ClickableRegion::Type regType) -> RECT {
                    RECT rcCam = { x, aRow1Y, x + w, aRow1Y + 28 };
                    bool hovCam = PtInRect(&rcCam, pState->ptMouse);
                    HBRUSH cB = CreateSolidBrush(active ? RGB(18, 55, 35) : (hovCam ? RGB(45, 50, 62) : RGB(32, 36, 46)));
                    HPEN cP = CreatePen(PS_SOLID, 1, active ? RGB(45, 160, 85) : (hovCam ? RGB(80, 100, 130) : RGB(52, 58, 74)));
                    SelectObject(memDC, cB); SelectObject(memDC, cP);
                    RoundRect(memDC, rcCam.left, rcCam.top, rcCam.right, rcCam.bottom, 4, 4);
                    DeleteObject(cB); DeleteObject(cP);

                    SelectObject(memDC, active ? pState->hFontBold : pState->hFontBody);
                    SetTextColor(memDC, active ? RGB(110, 240, 150) : RGB(170, 180, 200));
                    std::wstring fullLbl = (active ? L"✓ " : L"") + name;
                    DrawTextW(memDC, fullLbl.c_str(), -1, &rcCam, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ regType, rcCam, 0, 0, L"" });
                    return rcCam;
                };

                int acX = rcLblActCam.right + 4;
                RECT rActCab = drawActCamPill(acX, 140, L"CabCam (View 1)", grp.hasCabCam, ClickableRegion::SgActCamCab);
                RECT rActPass = drawActCamPill(rActCab.right + 10, 165, L"PassengerCam (View 5)", grp.hasPassengerCam, ClickableRegion::SgActCamPass);
                RECT rActExt = drawActCamPill(rActPass.right + 10, 140, L"ExternalCam (Ext)", grp.hasExternalCam, ClickableRegion::SgActCamExt);

                // Sub-row 2B: Trigger Distance & Track Profile
                int aRow2Y = aRow1Y + 34;
                RECT rcLblActDist = { rcSgCard.left + 24, aRow2Y, rcSgCard.left + 140, aRow2Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Trigger Distance:", -1, &rcLblActDist, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int adX = rcLblActDist.right + 4;
                RECT rcAdDown = { adX, aRow2Y, adX + 26, aRow2Y + 28 };
                RECT rcAdVal = { rcAdDown.right + 4, aRow2Y, rcAdDown.right + 80, aRow2Y + 28 };
                RECT rcAdUp = { rcAdVal.right + 4, aRow2Y, rcAdVal.right + 30, aRow2Y + 28 };

                bool hovAdD = PtInRect(&rcAdDown, pState->ptMouse);
                HBRUSH adB = CreateSolidBrush(hovAdD ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN adP = CreatePen(PS_SOLID, 1, hovAdD ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, adB); SelectObject(memDC, adP);
                RoundRect(memDC, rcAdDown.left, rcAdDown.top, rcAdDown.right, rcAdDown.bottom, 3, 3);
                DeleteObject(adB); DeleteObject(adP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"-", -1, &rcAdDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgActDistBtn, rcAdDown, -50, 0, L"" });

                bool hovAdV = PtInRect(&rcAdVal, pState->ptMouse);
                HBRUSH advB = CreateSolidBrush(hovAdV ? RGB(45, 52, 70) : RGB(30, 34, 46));
                HPEN advP = CreatePen(PS_SOLID, 1, hovAdV ? RGB(90, 160, 240) : RGB(55, 65, 88));
                SelectObject(memDC, advB); SelectObject(memDC, advP);
                RoundRect(memDC, rcAdVal.left, rcAdVal.top, rcAdVal.right, rcAdVal.bottom, 3, 3);
                DeleteObject(advB); DeleteObject(advP);
                std::wstring adStr = std::to_wstring((int)grp.activationDistance) + L" m";
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, adStr.c_str(), -1, &rcAdVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgActDistBtn, rcAdVal, 0, 0, L"" });

                bool hovAdU = PtInRect(&rcAdUp, pState->ptMouse);
                HBRUSH aduB = CreateSolidBrush(hovAdU ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN aduP = CreatePen(PS_SOLID, 1, hovAdU ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, aduB); SelectObject(memDC, aduP);
                RoundRect(memDC, rcAdUp.left, rcAdUp.top, rcAdUp.right, rcAdUp.bottom, 3, 3);
                DeleteObject(aduB); DeleteObject(aduP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"+", -1, &rcAdUp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgActDistBtn, rcAdUp, 50, 0, L"" });

                // Track Profile Dropdown for Activation
                int atX = rcAdUp.right + 36;
                RECT rcLblActTt = { atX, aRow2Y, atX + 90, aRow2Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Track Profile:", -1, &rcLblActTt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int atBtnW = (std::min)(320, (int)(rcSgCard.right - 20 - (rcLblActTt.right + 6)));
                RECT rcActTtBtn = { rcLblActTt.right + 6, aRow2Y, rcLblActTt.right + 6 + atBtnW, aRow2Y + 28 };
                bool hovActTt = PtInRect(&rcActTtBtn, pState->ptMouse);
                HBRUSH atB = CreateSolidBrush(hovActTt ? RGB(45, 52, 70) : RGB(32, 36, 48));
                HPEN atP = CreatePen(PS_SOLID, 1, hovActTt ? RGB(90, 150, 240) : RGB(55, 65, 85));
                SelectObject(memDC, atB); SelectObject(memDC, atP);
                RoundRect(memDC, rcActTtBtn.left, rcActTtBtn.top, rcActTtBtn.right, rcActTtBtn.bottom, 4, 4);
                DeleteObject(atB); DeleteObject(atP);

                std::wstring actTtLabel = L"▾ " + GetTrackTypeLabel(grp.trackType);
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, (grp.trackType >= 0) ? RGB(255, 215, 120) : RGB(180, 195, 215));
                RECT rcActTtTxt = { rcActTtBtn.left + 8, rcActTtBtn.top, rcActTtBtn.right - 8, rcActTtBtn.bottom };
                DrawTextW(memDC, actTtLabel.c_str(), -1, &rcActTtTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                pState->hitRegions.push_back({ ClickableRegion::SgTrackTypeBtn, rcActTtBtn, 0, 0, L"" });

                // ========================================================
                // SECTION 3: DEACTIVATION RULES (Symmetric Two Sub-Rows)
                // ========================================================
                int deactY = aRow2Y + 36;
                MoveToEx(memDC, rcSgCard.left + 14, deactY - 5, NULL);
                LineTo(memDC, rcSgCard.right - 14, deactY - 5);

                // Section Title Tag
                RECT rcDeactTag = { rcSgCard.left + 16, deactY, rcSgCard.right - 16, deactY + 22 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 165, 80));
                DrawTextW(memDC, L"■ DEACTIVATION RULES (Sound Cutoff / Unload Conditions)", -1, &rcDeactTag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Sub-row 3A: Cutoff Camera Views
                int dRow1Y = deactY + 24;
                RECT rcLblDeactCam = { rcSgCard.left + 24, dRow1Y, rcSgCard.left + 140, dRow1Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Cutoff Views:", -1, &rcLblDeactCam, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                auto drawDeactCamPill = [&](int x, int w, const std::wstring& name, bool active, ClickableRegion::Type regType) -> RECT {
                    RECT rcCam = { x, dRow1Y, x + w, dRow1Y + 28 };
                    bool hovCam = PtInRect(&rcCam, pState->ptMouse);
                    HBRUSH cB = CreateSolidBrush(active ? RGB(55, 30, 20) : (hovCam ? RGB(45, 50, 62) : RGB(32, 36, 46)));
                    HPEN cP = CreatePen(PS_SOLID, 1, active ? RGB(215, 110, 45) : (hovCam ? RGB(80, 100, 130) : RGB(52, 58, 74)));
                    SelectObject(memDC, cB); SelectObject(memDC, cP);
                    RoundRect(memDC, rcCam.left, rcCam.top, rcCam.right, rcCam.bottom, 4, 4);
                    DeleteObject(cB); DeleteObject(cP);

                    SelectObject(memDC, active ? pState->hFontBold : pState->hFontBody);
                    SetTextColor(memDC, active ? RGB(255, 185, 120) : RGB(170, 180, 200));
                    std::wstring fullLbl = (active ? L"✓ " : L"") + name;
                    DrawTextW(memDC, fullLbl.c_str(), -1, &rcCam, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ regType, rcCam, 0, 0, L"" });
                    return rcCam;
                };

                int dcX = rcLblDeactCam.right + 4;
                RECT rDeactCab = drawDeactCamPill(dcX, 140, L"CabCam (View 1)", grp.deactCabCam, ClickableRegion::SgDeactCamCab);
                RECT rDeactPass = drawDeactCamPill(rDeactCab.right + 10, 165, L"PassengerCam (View 5)", grp.deactPassengerCam, ClickableRegion::SgDeactCamPass);
                RECT rDeactExt = drawDeactCamPill(rDeactPass.right + 10, 140, L"ExternalCam (Ext)", grp.deactExternalCam, ClickableRegion::SgDeactCamExt);

                // Sub-row 3B: Cutoff Distance & Track Profile (Symmetric with Activation!)
                int dRow2Y = dRow1Y + 34;
                RECT rcLblDeactDist = { rcSgCard.left + 24, dRow2Y, rcSgCard.left + 140, dRow2Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Cutoff Distance:", -1, &rcLblDeactDist, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int ddX = rcLblDeactDist.right + 4;
                RECT rcDdDown = { ddX, dRow2Y, ddX + 26, dRow2Y + 28 };
                RECT rcDdVal = { rcDdDown.right + 4, dRow2Y, rcDdDown.right + 80, dRow2Y + 28 };
                RECT rcDdUp = { rcDdVal.right + 4, dRow2Y, rcDdVal.right + 30, dRow2Y + 28 };

                bool hovDdD = PtInRect(&rcDdDown, pState->ptMouse);
                HBRUSH ddB = CreateSolidBrush(hovDdD ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN ddP = CreatePen(PS_SOLID, 1, hovDdD ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, ddB); SelectObject(memDC, ddP);
                RoundRect(memDC, rcDdDown.left, rcDdDown.top, rcDdDown.right, rcDdDown.bottom, 3, 3);
                DeleteObject(ddB); DeleteObject(ddP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"-", -1, &rcDdDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgDeactDistBtn, rcDdDown, -100, 0, L"" });

                bool hovDdV = PtInRect(&rcDdVal, pState->ptMouse);
                HBRUSH ddvB = CreateSolidBrush(hovDdV ? RGB(45, 52, 70) : RGB(30, 34, 46));
                HPEN ddvP = CreatePen(PS_SOLID, 1, hovDdV ? RGB(90, 160, 240) : RGB(55, 65, 88));
                SelectObject(memDC, ddvB); SelectObject(memDC, ddvP);
                RoundRect(memDC, rcDdVal.left, rcDdVal.top, rcDdVal.right, rcDdVal.bottom, 3, 3);
                DeleteObject(ddvB); DeleteObject(ddvP);
                std::wstring ddStr = std::to_wstring((int)grp.deactivationDistance) + L" m";
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, ddStr.c_str(), -1, &rcDdVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgDeactDistBtn, rcDdVal, 0, 0, L"" });

                bool hovDdU = PtInRect(&rcDdUp, pState->ptMouse);
                HBRUSH dduB = CreateSolidBrush(hovDdU ? RGB(50, 56, 75) : RGB(36, 40, 54));
                HPEN dduP = CreatePen(PS_SOLID, 1, hovDdU ? RGB(90, 140, 220) : RGB(55, 62, 82));
                SelectObject(memDC, dduB); SelectObject(memDC, dduP);
                RoundRect(memDC, rcDdUp.left, rcDdUp.top, rcDdUp.right, rcDdUp.bottom, 3, 3);
                DeleteObject(dduB); DeleteObject(dduP);
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(210, 220, 240));
                DrawTextW(memDC, L"+", -1, &rcDdUp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::SgDeactDistBtn, rcDdUp, 100, 0, L"" });

                // Track Profile Dropdown for Deactivation (Symmetric!)
                int dtX = rcDdUp.right + 36;
                RECT rcLblDeactTt = { dtX, dRow2Y, dtX + 90, dRow2Y + 28 };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 195, 215));
                DrawTextW(memDC, L"Track Profile:", -1, &rcLblDeactTt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                int dtBtnW = (std::min)(320, (int)(rcSgCard.right - 20 - (rcLblDeactTt.right + 6)));
                RECT rcDeactTtBtn = { rcLblDeactTt.right + 6, dRow2Y, rcLblDeactTt.right + 6 + dtBtnW, dRow2Y + 28 };
                bool hovDeactTt = PtInRect(&rcDeactTtBtn, pState->ptMouse);
                HBRUSH dtB = CreateSolidBrush(hovDeactTt ? RGB(45, 52, 70) : RGB(32, 36, 48));
                HPEN dtP = CreatePen(PS_SOLID, 1, hovDeactTt ? RGB(90, 150, 240) : RGB(55, 65, 85));
                SelectObject(memDC, dtB); SelectObject(memDC, dtP);
                RoundRect(memDC, rcDeactTtBtn.left, rcDeactTtBtn.top, rcDeactTtBtn.right, rcDeactTtBtn.bottom, 4, 4);
                DeleteObject(dtB); DeleteObject(dtP);

                int effectiveDeactTt = (grp.deactTrackType >= 0) ? grp.deactTrackType : grp.trackType;
                std::wstring deactTtLabel = L"▾ " + GetTrackTypeLabel(effectiveDeactTt);
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, (effectiveDeactTt >= 0) ? RGB(255, 215, 120) : RGB(180, 195, 215));
                RECT rcDeactTtTxt = { rcDeactTtBtn.left + 8, rcDeactTtBtn.top, rcDeactTtBtn.right - 8, rcDeactTtBtn.bottom };
                DrawTextW(memDC, deactTtLabel.c_str(), -1, &rcDeactTtTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                pState->hitRegions.push_back({ ClickableRegion::SgDeactTrackTypeBtn, rcDeactTtBtn, 0, 0, L"" });

                SelectObject(memDC, oldDivPen);
                DeleteObject(divPen);
            }

            curY += (sgCardH + 12);

            // 4b. Encapsulate Streams Deck into a dedicated Bounded Card Container
            int streamsCardTop = curY;
            int streamsCardBottom = (std::max)(streamsCardTop + 120, (int)rcClient.bottom - 10);
            RECT rcStreamsCard = { sgCardLeft, streamsCardTop, sgCardRight, streamsCardBottom };
            pState->rcStreamsCard = rcStreamsCard;

            // Draw outer streams container card background & border
            HBRUSH scBr = CreateSolidBrush(RGB(22, 26, 35));
            HPEN scPen = CreatePen(PS_SOLID, 1, RGB(46, 54, 72));
            HGDIOBJ oscB = SelectObject(memDC, scBr);
            HGDIOBJ oscP = SelectObject(memDC, scPen);
            RoundRect(memDC, rcStreamsCard.left, rcStreamsCard.top, rcStreamsCard.right, rcStreamsCard.bottom, 6, 6);
            SelectObject(memDC, oscB);
            SelectObject(memDC, oscP);
            DeleteObject(scBr);
            DeleteObject(scPen);

            // Fixed Pinned Header for Sound Streams Container (36px)
            RECT rcStreamsHdr = { rcStreamsCard.left, rcStreamsCard.top, rcStreamsCard.right, rcStreamsCard.top + 36 };
            HBRUSH shBr = CreateSolidBrush(RGB(28, 34, 46));
            FillRect(memDC, &rcStreamsHdr, shBr);
            DeleteObject(shBr);

            // Pinned Header bottom separator line
            HPEN shPen = CreatePen(PS_SOLID, 1, RGB(46, 54, 72));
            HGDIOBJ oshP = SelectObject(memDC, shPen);
            MoveToEx(memDC, rcStreamsCard.left, rcStreamsHdr.bottom, NULL);
            LineTo(memDC, rcStreamsCard.right, rcStreamsHdr.bottom);
            SelectObject(memDC, oshP);
            DeleteObject(shPen);

            int totalStreams = (int)grp.streams.size();
            int selCount = 0;
            for (const auto& s : grp.streams) if (s.isSelected) ++selCount;

            // Master Checkbox: Column-aligned with stream cards below (X = rcStreamsCard.left + 19, width = 18, height = 18)
            RECT rcMasterCheck = { rcStreamsCard.left + 19, rcStreamsHdr.top + 9, rcStreamsCard.left + 37, rcStreamsHdr.top + 27 };
            bool hovMaster = PtInRect(&rcMasterCheck, pState->ptMouse);

            bool allSelected = (totalStreams > 0 && selCount == totalStreams);
            bool partialSelected = (selCount > 0 && selCount < totalStreams);

            COLORREF mBg = allSelected ? RGB(0, 120, 215) : (hovMaster ? RGB(48, 52, 66) : RGB(28, 30, 38));
            COLORREF mBorder = (allSelected || partialSelected) ? RGB(80, 160, 255) : (hovMaster ? RGB(100, 140, 200) : RGB(60, 65, 80));

            HBRUSH mChkBr = CreateSolidBrush(mBg);
            HPEN mChkPen = CreatePen(PS_SOLID, 1, mBorder);
            HGDIOBJ omB = SelectObject(memDC, mChkBr);
            HGDIOBJ omP = SelectObject(memDC, mChkPen);
            RoundRect(memDC, rcMasterCheck.left, rcMasterCheck.top, rcMasterCheck.right, rcMasterCheck.bottom, 3, 3);
            SelectObject(memDC, omB);
            SelectObject(memDC, omP);
            DeleteObject(mChkBr);
            DeleteObject(mChkPen);

            if (allSelected)
            {
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, L"✓", -1, &rcMasterCheck, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else if (partialSelected)
            {
                // Indeterminate dash in center
                RECT rcDash = { rcMasterCheck.left + 4, rcMasterCheck.top + 8, rcMasterCheck.right - 4, rcMasterCheck.top + 10 };
                HBRUSH dashBr = CreateSolidBrush(RGB(80, 160, 255));
                FillRect(memDC, &rcDash, dashBr);
                DeleteObject(dashBr);
            }
            pState->hitRegions.push_back({ ClickableRegion::StreamMasterCheck, rcMasterCheck, 0, 0, L"" });

            // Left Section Title Tag (shifted right to accommodate master checkbox)
            RECT rcStreamsHdrTitle = { rcStreamsCard.left + 46, rcStreamsHdr.top, rcStreamsHdr.right - 140, rcStreamsHdr.bottom };
            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, RGB(160, 180, 210));
            std::wstring stHdrTxt = L"▼ SOUND STREAMS (" + std::to_wstring(totalStreams) + L" Streams in Group " + std::to_wstring(grp.groupLevel);
            if (selCount > 0)
            {
                stHdrTxt += L" • " + std::to_wstring(selCount) + L" Selected";
            }
            stHdrTxt += L")";
            DrawTextW(memDC, stHdrTxt.c_str(), -1, &rcStreamsHdrTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Pinned Quick-Access "+ Add Stream" button in the fixed header (always visible)
            RECT rcAddHdr = { rcStreamsHdr.right - 125, rcStreamsHdr.top + 5, rcStreamsHdr.right - 10, rcStreamsHdr.bottom - 5 };
            bool hovAh = PtInRect(&rcAddHdr, pState->ptMouse);
            HBRUSH ahB = CreateSolidBrush(hovAh ? RGB(26, 75, 120) : RGB(20, 50, 80));
            HPEN ahP = CreatePen(PS_SOLID, 1, hovAh ? RGB(70, 175, 255) : RGB(45, 115, 185));
            HGDIOBJ oAhB = SelectObject(memDC, ahB);
            HGDIOBJ oAhP = SelectObject(memDC, ahP);
            RoundRect(memDC, rcAddHdr.left, rcAddHdr.top, rcAddHdr.right, rcAddHdr.bottom, 4, 4);
            SelectObject(memDC, oAhB); SelectObject(memDC, oAhP);
            DeleteObject(ahB); DeleteObject(ahP);
            SetTextColor(memDC, hovAh ? RGB(255, 255, 255) : RGB(210, 235, 255));
            DrawTextW(memDC, L"+ Add Stream", -1, &rcAddHdr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            pState->hitRegions.push_back({ ClickableRegion::StreamAddBtn, rcAddHdr, 0, 0, L"" });

            // Inner Viewport for scrollable stream deck (inset 14px on right for vertical scrollbar)
            RECT rcStreamsViewport = { rcStreamsCard.left + 1, rcStreamsHdr.bottom + 1, rcStreamsCard.right - 14, rcStreamsCard.bottom - 1 };
            pState->rcStreamsViewport = rcStreamsViewport;

            HRGN streamClipRgn = CreateRectRgn(rcStreamsViewport.left, rcStreamsViewport.top, rcStreamsViewport.right, rcStreamsViewport.bottom);
            SelectClipRgn(memDC, streamClipRgn);

            int streamCardLeft = rcStreamsViewport.left + 8;
            int streamCardRight = rcStreamsViewport.right - 8;
            int curStreamY = rcStreamsViewport.top + 8 - pState->scrollY;

            // 4c. Render Stream Cards inside Inner Viewport
            pState->streamCardRects.clear();
            for (int s = 0; s < (int)grp.streams.size(); ++s)
            {
                auto& st = grp.streams[s];

                // Compute card height dynamically
                int cardH = 42;
                if (!st.isCollapsed)
                {
                    cardH += 40; // Controls row (Priority, Volume, Stereo, Delete Stream)

                    // Triggers Section
                    cardH += 30; // Triggers header row
                    if (st.triggers.empty())
                    {
                        cardH += 28; // Empty placeholder hint
                    }
                    else
                    {
                        for (const auto& tr : st.triggers)
                        {
                            if (tr.isCollapsed)
                            {
                                cardH += 34; // Collapsed trigger sub-card
                            }
                            else
                            {
                                int subH = 32; // Sub-card header
                                subH += 28;    // Row 1: Parameters & SelectionMethod
                                subH += 26;    // Row 2: Action Type pill
                                subH += (int)tr.files.size() * 26; // Each WAV row
                                subH += 30;    // "+ Add WAV File..." button
                                subH += 8;     // Bottom padding
                                cardH += subH;
                            }
                            cardH += 6; // Spacing between triggers
                        }
                    }

                    // Curves Section
                    cardH += 30; // Curves header row
                    if (st.curves.empty())
                    {
                        cardH += 28; // Empty placeholder hint
                    }
                    else
                    {
                        for (const auto& cv : st.curves)
                        {
                            if (cv.isCollapsed)
                            {
                                cardH += 34; // Collapsed curve sub-card
                            }
                            else
                            {
                                int subH = 32; // Sub-card header
                                subH += 28;    // Row 1: Control type & granularity
                                subH += 70;    // Mini graphical curve visualizer preview box!
                                subH += (int)cv.points.size() * 24; // Each point row
                                subH += 30;    // "+ Add Point" button
                                subH += 8;     // Bottom padding
                                cardH += subH;
                            }
                            cardH += 6; // Spacing between curves
                        }
                    }

                    cardH += 16; // Bottom padding
                }

                RECT rcCard = { streamCardLeft, curStreamY, streamCardRight, curStreamY + cardH };
                pState->streamCardRects.push_back(rcCard);
                curStreamY += (cardH + 10);

                if (rcCard.bottom < rcStreamsViewport.top || rcCard.top > rcStreamsViewport.bottom)
                {
                    continue; // Skip off-screen stream card
                }

                // Card background & border
                HBRUSH cBr = CreateSolidBrush(st.isSelected ? RGB(26, 36, 52) : RGB(32, 34, 42));
                HPEN cPen = CreatePen(PS_SOLID, 1, st.isSelected ? RGB(60, 130, 220) : (st.isCollapsed ? RGB(48, 52, 65) : RGB(65, 75, 95)));
                HGDIOBJ oB = SelectObject(memDC, cBr);
                HGDIOBJ oP = SelectObject(memDC, cPen);
                RoundRect(memDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
                SelectObject(memDC, oB);
                SelectObject(memDC, oP);
                DeleteObject(cBr);
                DeleteObject(cPen);

                // Card Header Row (42px)
                RECT rcCardHdr = { rcCard.left, rcCard.top, rcCard.right, rcCard.top + 42 };
                HBRUSH hdrBr = CreateSolidBrush(st.isSelected ? RGB(32, 48, 72) : (st.isCollapsed ? RGB(36, 38, 48) : RGB(42, 46, 58)));
                FillRect(memDC, &rcCardHdr, hdrBr);
                DeleteObject(hdrBr);

                // Checkbox: [✓] or [ ]
                RECT rcCheck = { rcCardHdr.left + 10, rcCardHdr.top + 12, rcCardHdr.left + 28, rcCardHdr.top + 30 };
                bool hovCheck = PtInRect(&rcCheck, pState->ptMouse);
                HBRUSH chkBr = CreateSolidBrush(st.isSelected ? RGB(0, 120, 215) : (hovCheck ? RGB(48, 52, 66) : RGB(28, 30, 38)));
                HPEN chkPen = CreatePen(PS_SOLID, 1, st.isSelected ? RGB(80, 160, 255) : (hovCheck ? RGB(100, 140, 200) : RGB(60, 65, 80)));
                oB = SelectObject(memDC, chkBr);
                oP = SelectObject(memDC, chkPen);
                RoundRect(memDC, rcCheck.left, rcCheck.top, rcCheck.right, rcCheck.bottom, 3, 3);
                SelectObject(memDC, oB); SelectObject(memDC, oP);
                DeleteObject(chkBr); DeleteObject(chkPen);

                if (st.isSelected)
                {
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    DrawTextW(memDC, L"✓", -1, &rcCheck, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                pState->hitRegions.push_back({ ClickableRegion::StreamSelectCheck, rcCheck, s, 0, L"" });

                // Chevron ▾ / ▸
                RECT rcChev = { rcCardHdr.left + 34, rcCardHdr.top, rcCardHdr.left + 50, rcCardHdr.bottom };
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(140, 180, 240));
                DrawTextW(memDC, st.isCollapsed ? L"▸" : L"▾", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Stream Index Pill
                RECT rcIdxPill = { rcCardHdr.left + 54, rcCardHdr.top + 8, rcCardHdr.left + 142, rcCardHdr.bottom - 8 };
                HBRUSH idxBr = CreateSolidBrush(RGB(20, 22, 28));
                HPEN idxPen = CreatePen(PS_SOLID, 1, RGB(55, 60, 75));
                oB = SelectObject(memDC, idxBr);
                oP = SelectObject(memDC, idxPen);
                RoundRect(memDC, rcIdxPill.left, rcIdxPill.top, rcIdxPill.right, rcIdxPill.bottom, 4, 4);
                SelectObject(memDC, oB); SelectObject(memDC, oP);
                DeleteObject(idxBr); DeleteObject(idxPen);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(220, 230, 245));
                std::wstring idxTxt = L"STREAM #" + std::to_wstring(s + 1);
                DrawTextW(memDC, idxTxt.c_str(), -1, &rcIdxPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // StreamToggle region strictly covers chevron and index pill
                RECT rcToggleHdr = { rcChev.left, rcCardHdr.top, rcIdxPill.right + 6, rcCardHdr.bottom };
                pState->hitRegions.push_back({ ClickableRegion::StreamToggle, rcToggleHdr, s, 0, L"" });

                // Play Stream Loop Button (placed on far right)
                RECT rcPlayBtn = { rcCardHdr.right - 105, rcCardHdr.top + 7, rcCardHdr.right - 10, rcCardHdr.bottom - 7 };

                // Priority & Volume Mini Badges (between Title and Play button)
                RECT rcMetaBadge = { rcPlayBtn.left - 170, rcCardHdr.top, rcPlayBtn.left - 10, rcCardHdr.bottom };
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, RGB(160, 175, 200));
                wchar_t metaBuf[64];
                swprintf_s(metaBuf, L"Pri: %d | Vol: %.2f", st.priority, st.volume);
                DrawTextW(memDC, metaBuf, -1, &rcMetaBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Interactive Editable Friendly Title / Skip Block Pill
                RECT rcTitleEdit = { rcIdxPill.right + 10, rcCardHdr.top + 7, rcMetaBadge.left - 10, rcCardHdr.bottom - 7 };
                bool hovTitle = PtInRect(&rcTitleEdit, pState->ptMouse);
                HBRUSH tBr = CreateSolidBrush(hovTitle ? RGB(38, 44, 58) : RGB(25, 28, 38));
                HPEN tPen = CreatePen(PS_SOLID, 1, hovTitle ? RGB(90, 160, 255) : RGB(50, 58, 76));
                oB = SelectObject(memDC, tBr);
                oP = SelectObject(memDC, tPen);
                RoundRect(memDC, rcTitleEdit.left, rcTitleEdit.top, rcTitleEdit.right, rcTitleEdit.bottom, 4, 4);
                SelectObject(memDC, oB); SelectObject(memDC, oP);
                DeleteObject(tBr); DeleteObject(tPen);

                std::wstring dispTitle = st.friendlyTitle.empty() ? L"Untitled Stream (Click to set Skip comment)" : st.friendlyTitle;
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, st.friendlyTitle.empty() ? RGB(140, 150, 170) : RGB(255, 255, 255));
                RECT rcTitleText = { rcTitleEdit.left + 8, rcTitleEdit.top, rcTitleEdit.right - 24, rcTitleEdit.bottom };
                DrawTextW(memDC, dispTitle.c_str(), -1, &rcTitleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                // Pencil icon
                RECT rcPencil = { rcTitleEdit.right - 22, rcTitleEdit.top, rcTitleEdit.right - 4, rcTitleEdit.bottom };
                SetTextColor(memDC, hovTitle ? RGB(120, 190, 255) : RGB(130, 140, 165));
                DrawTextW(memDC, L"✏", -1, &rcPencil, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::StreamEditTitleBtn, rcTitleEdit, s, 0, L"" });

                // Play Stream Loop Button
                bool isPl = st.isPlayingPreview;
                bool hovPlay = PtInRect(&rcPlayBtn, pState->ptMouse);
                COLORREF playBg = isPl ? (hovPlay ? RGB(220, 60, 60) : RGB(190, 40, 40)) : (hovPlay ? RGB(35, 155, 90) : RGB(25, 125, 70));
                COLORREF playBrd = isPl ? RGB(245, 90, 90) : RGB(50, 185, 110);
                HBRUSH pBr = CreateSolidBrush(playBg);
                HPEN pPen = CreatePen(PS_SOLID, 1, playBrd);
                HGDIOBJ opb = SelectObject(memDC, pBr);
                HGDIOBJ opp = SelectObject(memDC, pPen);
                RoundRect(memDC, rcPlayBtn.left, rcPlayBtn.top, rcPlayBtn.right, rcPlayBtn.bottom, 4, 4);
                SelectObject(memDC, opb); SelectObject(memDC, opp);
                DeleteObject(pBr); DeleteObject(pPen);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, isPl ? L"■ Stop Loop" : L"▶ Play Loop", -1, &rcPlayBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                pState->hitRegions.push_back({ ClickableRegion::StreamPlayLoop, rcPlayBtn, s, 0, L"" });

                // If expanded, render details
                if (!st.isCollapsed)
                {
                    int bodyY = rcCardHdr.bottom + 8;

                    // ========================================================
                    // ROW 1: STREAM CONTROLS (Priority Stepper, Volume Stepper, Stereo, Delete)
                    // ========================================================
                    RECT rcCtrlRow = { rcCard.left + 14, bodyY, rcCard.right - 14, bodyY + 32 };
                    HBRUSH crBr = CreateSolidBrush(RGB(22, 25, 34));
                    HPEN crPen = CreatePen(PS_SOLID, 1, RGB(45, 52, 70));
                    oB = SelectObject(memDC, crBr);
                    oP = SelectObject(memDC, crPen);
                    RoundRect(memDC, rcCtrlRow.left, rcCtrlRow.top, rcCtrlRow.right, rcCtrlRow.bottom, 4, 4);
                    SelectObject(memDC, oB); SelectObject(memDC, oP);
                    DeleteObject(crBr); DeleteObject(crPen);

                    int ctrlX = rcCtrlRow.left + 10;

                    // Priority Stepper
                    RECT rcLblPri = { ctrlX, rcCtrlRow.top, ctrlX + 68, rcCtrlRow.bottom };
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(160, 180, 210));
                    DrawTextW(memDC, L"PRIORITY:", -1, &rcLblPri, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    ctrlX = rcLblPri.right + 4;

                    RECT rcPriDown = { ctrlX, rcCtrlRow.top + 4, ctrlX + 24, rcCtrlRow.bottom - 4 };
                    bool hovPd = PtInRect(&rcPriDown, pState->ptMouse);
                    HBRUSH pdB = CreateSolidBrush(hovPd ? RGB(50, 56, 75) : RGB(36, 40, 54));
                    HPEN pdP = CreatePen(PS_SOLID, 1, hovPd ? RGB(90, 140, 220) : RGB(55, 62, 82));
                    SelectObject(memDC, pdB); SelectObject(memDC, pdP);
                    RoundRect(memDC, rcPriDown.left, rcPriDown.top, rcPriDown.right, rcPriDown.bottom, 3, 3);
                    DeleteObject(pdB); DeleteObject(pdP);
                    SetTextColor(memDC, RGB(210, 220, 240));
                    DrawTextW(memDC, L"-", -1, &rcPriDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamPriorityDown, rcPriDown, s, 0, L"" });
                    ctrlX = rcPriDown.right + 3;

                    RECT rcPriVal = { ctrlX, rcCtrlRow.top + 4, ctrlX + 32, rcCtrlRow.bottom - 4 };
                    bool hovPv = PtInRect(&rcPriVal, pState->ptMouse);
                    HBRUSH pvB = CreateSolidBrush(hovPv ? RGB(45, 52, 70) : RGB(30, 34, 46));
                    HPEN pvP = CreatePen(PS_SOLID, 1, hovPv ? RGB(90, 160, 240) : RGB(55, 65, 88));
                    SelectObject(memDC, pvB); SelectObject(memDC, pvP);
                    RoundRect(memDC, rcPriVal.left, rcPriVal.top, rcPriVal.right, rcPriVal.bottom, 3, 3);
                    DeleteObject(pvB); DeleteObject(pvP);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    DrawTextW(memDC, std::to_wstring(st.priority).c_str(), -1, &rcPriVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamPriorityVal, rcPriVal, s, 0, L"" });
                    ctrlX = rcPriVal.right + 3;

                    RECT rcPriUp = { ctrlX, rcCtrlRow.top + 4, ctrlX + 24, rcCtrlRow.bottom - 4 };
                    bool hovPu = PtInRect(&rcPriUp, pState->ptMouse);
                    HBRUSH puB = CreateSolidBrush(hovPu ? RGB(50, 56, 75) : RGB(36, 40, 54));
                    HPEN puP = CreatePen(PS_SOLID, 1, hovPu ? RGB(90, 140, 220) : RGB(55, 62, 82));
                    SelectObject(memDC, puB); SelectObject(memDC, puP);
                    RoundRect(memDC, rcPriUp.left, rcPriUp.top, rcPriUp.right, rcPriUp.bottom, 3, 3);
                    DeleteObject(puB); DeleteObject(puP);
                    SetTextColor(memDC, RGB(210, 220, 240));
                    DrawTextW(memDC, L"+", -1, &rcPriUp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamPriorityUp, rcPriUp, s, 0, L"" });
                    ctrlX = rcPriUp.right + 16;

                    // Divider
                    HPEN divP = CreatePen(PS_SOLID, 1, RGB(45, 52, 70));
                    oP = SelectObject(memDC, divP);
                    MoveToEx(memDC, ctrlX, rcCtrlRow.top + 6, NULL);
                    LineTo(memDC, ctrlX, rcCtrlRow.bottom - 6);
                    SelectObject(memDC, oP);
                    DeleteObject(divP);
                    ctrlX += 16;

                    // Volume Check & Stepper
                    RECT rcVolChk = { ctrlX, rcCtrlRow.top + 4, ctrlX + 80, rcCtrlRow.bottom - 4 };
                    bool hovVc = PtInRect(&rcVolChk, pState->ptMouse);
                    HBRUSH vcB = CreateSolidBrush(st.hasVolume ? RGB(20, 48, 75) : (hovVc ? RGB(45, 50, 65) : RGB(30, 34, 46)));
                    HPEN vcP = CreatePen(PS_SOLID, 1, st.hasVolume ? RGB(40, 140, 240) : (hovVc ? RGB(80, 100, 130) : RGB(50, 58, 76)));
                    SelectObject(memDC, vcB); SelectObject(memDC, vcP);
                    RoundRect(memDC, rcVolChk.left, rcVolChk.top, rcVolChk.right, rcVolChk.bottom, 3, 3);
                    DeleteObject(vcB); DeleteObject(vcP);
                    SetTextColor(memDC, st.hasVolume ? RGB(120, 200, 255) : RGB(160, 170, 185));
                    std::wstring vChkTxt = st.hasVolume ? L"✓ Volume" : L"Volume";
                    DrawTextW(memDC, vChkTxt.c_str(), -1, &rcVolChk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamVolumeToggle, rcVolChk, s, 0, L"" });
                    ctrlX = rcVolChk.right + 8;

                    RECT rcVolDown = { ctrlX, rcCtrlRow.top + 4, ctrlX + 24, rcCtrlRow.bottom - 4 };
                    bool hovVd = PtInRect(&rcVolDown, pState->ptMouse);
                    HBRUSH vdB = CreateSolidBrush(hovVd ? RGB(50, 56, 75) : RGB(36, 40, 54));
                    HPEN vdP = CreatePen(PS_SOLID, 1, hovVd ? RGB(90, 140, 220) : RGB(55, 62, 82));
                    SelectObject(memDC, vdB); SelectObject(memDC, vdP);
                    RoundRect(memDC, rcVolDown.left, rcVolDown.top, rcVolDown.right, rcVolDown.bottom, 3, 3);
                    DeleteObject(vdB); DeleteObject(vdP);
                    SetTextColor(memDC, RGB(210, 220, 240));
                    DrawTextW(memDC, L"-", -1, &rcVolDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamVolumeDown, rcVolDown, s, 0, L"" });
                    ctrlX = rcVolDown.right + 3;

                    RECT rcVolVal = { ctrlX, rcCtrlRow.top + 4, ctrlX + 50, rcCtrlRow.bottom - 4 };
                    bool hovVv = PtInRect(&rcVolVal, pState->ptMouse);
                    HBRUSH vvB = CreateSolidBrush(hovVv ? RGB(45, 52, 70) : RGB(30, 34, 46));
                    HPEN vvP = CreatePen(PS_SOLID, 1, hovVv ? RGB(90, 160, 240) : RGB(55, 65, 88));
                    SelectObject(memDC, vvB); SelectObject(memDC, vvP);
                    RoundRect(memDC, rcVolVal.left, rcVolVal.top, rcVolVal.right, rcVolVal.bottom, 3, 3);
                    DeleteObject(vvB); DeleteObject(vvP);
                    wchar_t svStr[32]; swprintf_s(svStr, L"%.2f", st.volume);
                    SetTextColor(memDC, st.hasVolume ? RGB(255, 255, 255) : RGB(140, 150, 165));
                    DrawTextW(memDC, svStr, -1, &rcVolVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamVolumeVal, rcVolVal, s, 0, L"" });
                    ctrlX = rcVolVal.right + 3;

                    RECT rcVolUp = { ctrlX, rcCtrlRow.top + 4, ctrlX + 24, rcCtrlRow.bottom - 4 };
                    bool hovVu = PtInRect(&rcVolUp, pState->ptMouse);
                    HBRUSH vuB = CreateSolidBrush(hovVu ? RGB(50, 56, 75) : RGB(36, 40, 54));
                    HPEN vuP = CreatePen(PS_SOLID, 1, hovVu ? RGB(90, 140, 220) : RGB(55, 62, 82));
                    SelectObject(memDC, vuB); SelectObject(memDC, vuP);
                    RoundRect(memDC, rcVolUp.left, rcVolUp.top, rcVolUp.right, rcVolUp.bottom, 3, 3);
                    DeleteObject(vuB); DeleteObject(vuP);
                    SetTextColor(memDC, RGB(210, 220, 240));
                    DrawTextW(memDC, L"+", -1, &rcVolUp, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamVolumeUp, rcVolUp, s, 0, L"" });
                    ctrlX = rcVolUp.right + 16;

                    // Divider
                    divP = CreatePen(PS_SOLID, 1, RGB(45, 52, 70));
                    oP = SelectObject(memDC, divP);
                    MoveToEx(memDC, ctrlX, rcCtrlRow.top + 6, NULL);
                    LineTo(memDC, ctrlX, rcCtrlRow.bottom - 6);
                    SelectObject(memDC, oP);
                    DeleteObject(divP);
                    ctrlX += 16;

                    // Stereo Toggle
                    RECT rcStereoChk = { ctrlX, rcCtrlRow.top + 4, ctrlX + 85, rcCtrlRow.bottom - 4 };
                    bool hovSt = PtInRect(&rcStereoChk, pState->ptMouse);
                    HBRUSH stB = CreateSolidBrush(st.isStereo ? RGB(22, 54, 34) : (hovSt ? RGB(45, 50, 65) : RGB(34, 38, 50)));
                    HPEN stP = CreatePen(PS_SOLID, 1, st.isStereo ? RGB(45, 160, 85) : (hovSt ? RGB(80, 100, 130) : RGB(52, 58, 76)));
                    SelectObject(memDC, stB); SelectObject(memDC, stP);
                    RoundRect(memDC, rcStereoChk.left, rcStereoChk.top, rcStereoChk.right, rcStereoChk.bottom, 3, 3);
                    DeleteObject(stB); DeleteObject(stP);
                    SetTextColor(memDC, st.isStereo ? RGB(110, 240, 150) : RGB(170, 180, 195));
                    std::wstring stereoLbl = st.isStereo ? L"✓ Stereo" : L"Stereo";
                    DrawTextW(memDC, stereoLbl.c_str(), -1, &rcStereoChk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamStereoToggle, rcStereoChk, s, 0, L"" });

                    // Delete Stream Button (Far Right)
                    RECT rcDelStream = { rcCtrlRow.right - 120, rcCtrlRow.top + 4, rcCtrlRow.right - 8, rcCtrlRow.bottom - 4 };
                    bool hovDs = PtInRect(&rcDelStream, pState->ptMouse);
                    HBRUSH dsB = CreateSolidBrush(hovDs ? RGB(75, 25, 25) : RGB(45, 26, 30));
                    HPEN dsP = CreatePen(PS_SOLID, 1, hovDs ? RGB(220, 70, 70) : RGB(110, 45, 50));
                    SelectObject(memDC, dsB); SelectObject(memDC, dsP);
                    RoundRect(memDC, rcDelStream.left, rcDelStream.top, rcDelStream.right, rcDelStream.bottom, 3, 3);
                    DeleteObject(dsB); DeleteObject(dsP);
                    SetTextColor(memDC, hovDs ? RGB(255, 140, 140) : RGB(215, 110, 110));
                    DrawTextW(memDC, L"✕ Delete Stream", -1, &rcDelStream, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamDeleteBtn, rcDelStream, s, 0, L"" });

                    bodyY += 40;

                    // ========================================================
                    // ROW 2: TRIGGERS SECTION
                    // ========================================================
                    RECT rcTrigHdr = { rcCard.left + 16, bodyY, rcCard.right - 16, bodyY + 26 };
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(130, 165, 220));
                    std::wstring trigHdrTxt = L"▼ TRIGGERS (" + std::to_wstring(st.declaredTriggers) + L" declared / " + std::to_wstring(st.triggers.size()) + L" parsed):";
                    DrawTextW(memDC, trigHdrTxt.c_str(), -1, &rcTrigHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Add Trigger Button
                    RECT rcAddTrig = { rcTrigHdr.right - 115, rcTrigHdr.top, rcTrigHdr.right, rcTrigHdr.bottom };
                    bool hovAt = PtInRect(&rcAddTrig, pState->ptMouse);
                    HBRUSH atB = CreateSolidBrush(hovAt ? RGB(25, 65, 45) : RGB(20, 46, 32));
                    HPEN atP = CreatePen(PS_SOLID, 1, hovAt ? RGB(50, 185, 110) : RGB(35, 130, 80));
                    SelectObject(memDC, atB); SelectObject(memDC, atP);
                    RoundRect(memDC, rcAddTrig.left, rcAddTrig.top, rcAddTrig.right, rcAddTrig.bottom, 3, 3);
                    DeleteObject(atB); DeleteObject(atP);
                    SetTextColor(memDC, RGB(120, 240, 160));
                    DrawTextW(memDC, L"+ Add Trigger", -1, &rcAddTrig, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamAddTriggerBtn, rcAddTrig, s, 0, L"" });

                    bodyY += 30;

                    if (st.triggers.empty())
                    {
                        RECT rcEmptyTrig = { rcCard.left + 24, bodyY, rcCard.right - 24, bodyY + 24 };
                        SelectObject(memDC, pState->hFontBody);
                        SetTextColor(memDC, RGB(140, 150, 170));
                        DrawTextW(memDC, L"No triggers defined for this stream. Click [ + Add Trigger ] to add a trigger.", -1, &rcEmptyTrig, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        bodyY += 28;
                    }
                    else
                    {
                        for (int t = 0; t < (int)st.triggers.size(); ++t)
                        {
                            auto& tr = st.triggers[t];
                            int subH = tr.isCollapsed ? 34 : (32 + 28 + 26 + (int)tr.files.size() * 26 + 30 + 8);

                            RECT rcTrigCard = { rcCard.left + 20, bodyY, rcCard.right - 20, bodyY + subH };
                            HBRUSH tcBr = CreateSolidBrush(RGB(20, 23, 31));
                            HPEN tcPen = CreatePen(PS_SOLID, 1, tr.isCollapsed ? RGB(42, 48, 64) : RGB(55, 65, 88));
                            SelectObject(memDC, tcBr); SelectObject(memDC, tcPen);
                            RoundRect(memDC, rcTrigCard.left, rcTrigCard.top, rcTrigCard.right, rcTrigCard.bottom, 4, 4);
                            DeleteObject(tcBr); DeleteObject(tcPen);

                            // Trigger Sub-card Header Row (32px)
                            RECT rcTrHdr = { rcTrigCard.left, rcTrigCard.top, rcTrigCard.right, rcTrigCard.top + 32 };

                            // Chevron
                            RECT rcTrChev = { rcTrHdr.left + 6, rcTrHdr.top, rcTrHdr.left + 22, rcTrHdr.bottom };
                            SelectObject(memDC, pState->hFontBold);
                            SetTextColor(memDC, RGB(140, 180, 240));
                            DrawTextW(memDC, tr.isCollapsed ? L"▸" : L"▾", -1, &rcTrChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Trigger Type Badge
                            std::wstring trigDisplay = L"T" + std::to_wstring(t + 1) + L": " + SmsParser_FormatTriggerDisplayName(tr);
                            RECT rcTrBadge = { rcTrChev.right + 4, rcTrHdr.top + 4, rcTrChev.right + 260, rcTrHdr.bottom - 4 };
                            bool hovTb = PtInRect(&rcTrBadge, pState->ptMouse);
                            HBRUSH tbB = CreateSolidBrush(hovTb ? RGB(32, 58, 85) : RGB(24, 45, 68));
                            HPEN tbP = CreatePen(PS_SOLID, 1, hovTb ? RGB(70, 150, 240) : RGB(45, 90, 145));
                            SelectObject(memDC, tbB); SelectObject(memDC, tbP);
                            RoundRect(memDC, rcTrBadge.left, rcTrBadge.top, rcTrBadge.right, rcTrBadge.bottom, 3, 3);
                            DeleteObject(tbB); DeleteObject(tbP);
                            SetTextColor(memDC, RGB(180, 220, 255));
                            DrawTextW(memDC, trigDisplay.c_str(), -1, &rcTrBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                            pState->hitRegions.push_back({ ClickableRegion::TriggerTypeBtn, rcTrBadge, s, t, L"" });

                            // Action Tag
                            RECT rcTrActionTag = { rcTrBadge.right + 10, rcTrHdr.top + 4, rcTrBadge.right + 230, rcTrHdr.bottom - 4 };
                            bool hovTa = PtInRect(&rcTrActionTag, pState->ptMouse);
                            HBRUSH taB = CreateSolidBrush(hovTa ? RGB(45, 48, 62) : RGB(32, 35, 48));
                            HPEN taP = CreatePen(PS_SOLID, 1, hovTa ? RGB(100, 115, 145) : RGB(60, 68, 90));
                            SelectObject(memDC, taB); SelectObject(memDC, taP);
                            RoundRect(memDC, rcTrActionTag.left, rcTrActionTag.top, rcTrActionTag.right, rcTrActionTag.bottom, 3, 3);
                            DeleteObject(taB); DeleteObject(taP);
                            std::wstring actTagTxt = L"↳ " + tr.actionType + (tr.declaredFiles > 0 ? L" (" + std::to_wstring(tr.files.size()) + L" WAVs)" : L"");
                            SetTextColor(memDC, RGB(225, 235, 250));
                            DrawTextW(memDC, actTagTxt.c_str(), -1, &rcTrActionTag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                            pState->hitRegions.push_back({ ClickableRegion::TriggerActionBtn, rcTrActionTag, s, t, L"" });

                            // Delete Trigger Button (Right)
                            RECT rcTrDel = { rcTrHdr.right - 30, rcTrHdr.top + 4, rcTrHdr.right - 6, rcTrHdr.bottom - 4 };
                            bool hovTd = PtInRect(&rcTrDel, pState->ptMouse);
                            HBRUSH tdB = CreateSolidBrush(hovTd ? RGB(85, 25, 25) : RGB(42, 24, 28));
                            HPEN tdP = CreatePen(PS_SOLID, 1, hovTd ? RGB(230, 70, 70) : RGB(100, 45, 52));
                            SelectObject(memDC, tdB); SelectObject(memDC, tdP);
                            RoundRect(memDC, rcTrDel.left, rcTrDel.top, rcTrDel.right, rcTrDel.bottom, 3, 3);
                            DeleteObject(tdB); DeleteObject(tdP);
                            SetTextColor(memDC, hovTd ? RGB(255, 140, 140) : RGB(200, 100, 100));
                            DrawTextW(memDC, L"✕", -1, &rcTrDel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            pState->hitRegions.push_back({ ClickableRegion::TriggerDeleteBtn, rcTrDel, s, t, L"" });

                            // Trigger Sub-card toggle covers rest of header
                            RECT rcTrTog = { rcTrHdr.left, rcTrHdr.top, rcTrDel.left - 4, rcTrHdr.bottom };
                            pState->hitRegions.push_back({ ClickableRegion::TriggerToggle, rcTrTog, s, t, L"" });

                            // When expanded, render details
                            if (!tr.isCollapsed)
                            {
                                int inY = rcTrHdr.bottom + 4;

                                // Sub-row 1: Parameters & SelectionMethod
                                RECT rcTrParam = { rcTrigCard.left + 16, inY, rcTrigCard.left + 340, inY + 24 };
                                bool hovTp = PtInRect(&rcTrParam, pState->ptMouse);
                                HBRUSH tpB = CreateSolidBrush(hovTp ? RGB(36, 42, 56) : RGB(28, 32, 44));
                                HPEN tpP = CreatePen(PS_SOLID, 1, hovTp ? RGB(80, 140, 230) : RGB(50, 58, 78));
                                SelectObject(memDC, tpB); SelectObject(memDC, tpP);
                                RoundRect(memDC, rcTrParam.left, rcTrParam.top, rcTrParam.right, rcTrParam.bottom, 3, 3);
                                DeleteObject(tpB); DeleteObject(tpP);

                                std::wstring paramTxt;
                                if (tr.triggerType == L"Discrete_Trigger")
                                    paramTxt = L"Trigger ID: " + std::to_wstring(tr.triggerId) + L" (" + SmsParser_GetDiscreteTriggerName(tr.triggerId) + L") ▾";
                                else if (tr.triggerType == L"Variable_Trigger")
                                    paramTxt = L"Condition: \"" + tr.variableCondition + L"\" ✏";
                                else if (tr.triggerType == L"Random_Trigger")
                                {
                                    wchar_t dBuf[64]; swprintf_s(dBuf, L"Delay: %.2fs to %.2fs ✏", tr.delayMin, tr.delayMax);
                                    paramTxt = dBuf;
                                }
                                else if (tr.triggerType == L"Dist_Travelled_Trigger")
                                {
                                    wchar_t dBuf[64]; swprintf_s(dBuf, L"Distance: %.1fm to %.1fm ✏", tr.distMin, tr.distMax);
                                    paramTxt = dBuf;
                                }
                                else if (SmsParser_IsTrackAxleTrigger(tr.triggerType))
                                {
                                    wchar_t dBuf[64]; swprintf_s(dBuf, L"Car Camera Dist: %.1fm ✏", tr.carCameraDist);
                                    paramTxt = dBuf;
                                }
                                else
                                    paramTxt = L"Initial Trigger (Starts automatically on stream activation)";

                                SelectObject(memDC, pState->hFontBody);
                                SetTextColor(memDC, RGB(220, 230, 245));
                                RECT rcParamIn = { rcTrParam.left + 8, rcTrParam.top, rcTrParam.right - 8, rcTrParam.bottom };
                                DrawTextW(memDC, paramTxt.c_str(), -1, &rcParamIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                                pState->hitRegions.push_back({ ClickableRegion::TriggerParamBtn, rcTrParam, s, t, L"" });

                                // Selection Method Pill
                                if (tr.declaredFiles > 0 || !tr.selectionMethod.empty())
                                {
                                    std::wstring smTxt = L"Selection: " + (tr.selectionMethod.empty() ? L"SequentialSelection" : tr.selectionMethod) + L" ▾";
                                    SelectObject(memDC, pState->hFontBody);
                                    SIZE smSz = { 0 };
                                    GetTextExtentPoint32W(memDC, smTxt.c_str(), (int)smTxt.size(), &smSz);
                                    int smW = (std::max)(180, (int)smSz.cx + 24);
                                    RECT rcSelMethod = { rcTrParam.right + 10, inY, rcTrParam.right + 10 + smW, inY + 24 };
                                    bool hovSm = PtInRect(&rcSelMethod, pState->ptMouse);
                                    HBRUSH smB = CreateSolidBrush(hovSm ? RGB(36, 42, 56) : RGB(28, 32, 44));
                                    HPEN smP = CreatePen(PS_SOLID, 1, hovSm ? RGB(80, 140, 230) : RGB(50, 58, 78));
                                    SelectObject(memDC, smB); SelectObject(memDC, smP);
                                    RoundRect(memDC, rcSelMethod.left, rcSelMethod.top, rcSelMethod.right, rcSelMethod.bottom, 3, 3);
                                    DeleteObject(smB); DeleteObject(smP);

                                    RECT rcSmIn = { rcSelMethod.left + 10, rcSelMethod.top, rcSelMethod.right - 10, rcSelMethod.bottom };
                                    SetTextColor(memDC, RGB(180, 210, 240));
                                    DrawTextW(memDC, smTxt.c_str(), -1, &rcSmIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    pState->hitRegions.push_back({ ClickableRegion::TriggerSelMethodBtn, rcSelMethod, s, t, L"" });
                                }

                                inY += 28;

                                // Sub-row 2: Action Type Button (Dynamically measured to fit all command names)
                                std::wstring actBtnTxt = L"Action Command: " + tr.actionType + L" ▾";
                                SelectObject(memDC, pState->hFontBody);
                                SIZE actSz = { 0 };
                                GetTextExtentPoint32W(memDC, actBtnTxt.c_str(), (int)actBtnTxt.size(), &actSz);
                                int pillW = (std::max)(220, (int)actSz.cx + 28);
                                RECT rcActPill = { rcTrigCard.left + 16, inY, rcTrigCard.left + 16 + pillW, inY + 24 };
                                bool hovAp = PtInRect(&rcActPill, pState->ptMouse);
                                HBRUSH apB = CreateSolidBrush(hovAp ? RGB(38, 45, 60) : RGB(26, 30, 42));
                                HPEN apP = CreatePen(PS_SOLID, 1, hovAp ? RGB(80, 150, 240) : RGB(52, 60, 80));
                                SelectObject(memDC, apB); SelectObject(memDC, apP);
                                RoundRect(memDC, rcActPill.left, rcActPill.top, rcActPill.right, rcActPill.bottom, 3, 3);
                                DeleteObject(apB); DeleteObject(apP);

                                RECT rcActIn = { rcActPill.left + 10, rcActPill.top, rcActPill.right - 10, rcActPill.bottom };
                                SetTextColor(memDC, RGB(235, 240, 255));
                                DrawTextW(memDC, actBtnTxt.c_str(), -1, &rcActIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                pState->hitRegions.push_back({ ClickableRegion::TriggerActionBtn, rcActPill, s, t, L"" });

                                // Action Command Parameter Pill (for SetStreamVolume, DisableTrigger, EnableTrigger)
                                bool hasActionParam = (EqualsI(tr.actionType, L"SetStreamVolume") ||
                                                       EqualsI(tr.actionType, L"SetVolume") ||
                                                       EqualsI(tr.actionType, L"DisableTrigger") ||
                                                       EqualsI(tr.actionType, L"EnableTrigger"));
                                if (hasActionParam)
                                {
                                    std::wstring paramTxt;
                                    if (EqualsI(tr.actionType, L"DisableTrigger") || EqualsI(tr.actionType, L"EnableTrigger"))
                                    {
                                        paramTxt = L"Target ID: " + ((tr.actionParam > 0.0f) ? std::to_wstring((int)tr.actionParam) : L"All Triggers") + L" ✎";
                                    }
                                    else
                                    {
                                        wchar_t bufP[32];
                                        swprintf_s(bufP, L"Volume: %.2f ✎", tr.actionParam);
                                        paramTxt = bufP;
                                    }

                                    SelectObject(memDC, pState->hFontBody);
                                    SIZE pSz = { 0 };
                                    GetTextExtentPoint32W(memDC, paramTxt.c_str(), (int)paramTxt.size(), &pSz);
                                    int pW = (std::max)(140, (int)pSz.cx + 24);
                                    RECT rcParamPill = { rcActPill.right + 10, inY, rcActPill.right + 10 + pW, inY + 24 };
                                    bool hovPp = PtInRect(&rcParamPill, pState->ptMouse);
                                    HBRUSH ppB = CreateSolidBrush(hovPp ? RGB(32, 54, 44) : RGB(22, 38, 30));
                                    HPEN ppP = CreatePen(PS_SOLID, 1, hovPp ? RGB(65, 200, 120) : RGB(40, 120, 75));
                                    SelectObject(memDC, ppB); SelectObject(memDC, ppP);
                                    RoundRect(memDC, rcParamPill.left, rcParamPill.top, rcParamPill.right, rcParamPill.bottom, 3, 3);
                                    DeleteObject(ppB); DeleteObject(ppP);

                                    RECT rcParamIn = { rcParamPill.left + 10, rcParamPill.top, rcParamPill.right - 10, rcParamPill.bottom };
                                    SetTextColor(memDC, RGB(180, 240, 200));
                                    DrawTextW(memDC, paramTxt.c_str(), -1, &rcParamIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    pState->hitRegions.push_back({ ClickableRegion::TriggerActionParamBtn, rcParamPill, s, t, L"" });
                                }

                                inY += 28;

                                // WAV Files List
                                for (int f = 0; f < (int)tr.files.size(); ++f)
                                {
                                    const auto& wav = tr.files[f];
                                    RECT rcWavRow = { rcTrigCard.left + 24, inY, rcTrigCard.right - 16, inY + 22 };

                                    // Status Dot
                                    COLORREF wDotCol = wav.existsOnDisk ? RGB(40, 200, 80) : RGB(240, 60, 60);
                                    HBRUSH wDotBr = CreateSolidBrush(wDotCol);
                                    RECT rcWDot = { rcWavRow.left, rcWavRow.top + 7, rcWavRow.left + 8, rcWavRow.top + 15 };
                                    HGDIOBJ oWB = SelectObject(memDC, wDotBr);
                                    HGDIOBJ oWP = SelectObject(memDC, GetStockObject(NULL_PEN));
                                    Ellipse(memDC, rcWDot.left, rcWDot.top, rcWDot.right, rcWDot.bottom);
                                    SelectObject(memDC, oWB); SelectObject(memDC, oWP);
                                    DeleteObject(wDotBr);

                                    // Wav path
                                    RECT rcWavText = { rcWavRow.left + 16, rcWavRow.top, rcWavRow.right - 100, rcWavRow.bottom };
                                    bool hovWt = PtInRect(&rcWavText, pState->ptMouse);
                                    if (hovWt)
                                    {
                                        HBRUSH wtB = CreateSolidBrush(RGB(32, 42, 58));
                                        FillRect(memDC, &rcWavText, wtB);
                                        DeleteObject(wtB);
                                    }
                                    SelectObject(memDC, pState->hFontMono);
                                    SetTextColor(memDC, hovWt ? RGB(255, 255, 255) : (wav.existsOnDisk ? RGB(200, 215, 235) : RGB(245, 140, 140)));
                                    DrawTextW(memDC, wav.fileName.c_str(), -1, &rcWavText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                                    pState->hitRegions.push_back({ ClickableRegion::TriggerWavEditBtn, rcWavText, s, t, L"", f });

                                    // Play WAV Button
                                    RECT rcPlayWav = { rcWavRow.right - 90, rcWavRow.top, rcWavRow.right - 30, rcWavRow.bottom };
                                    bool isThisPlaying = (s_isPlayingAudio && s_playingPath == wav.resolvedDiskPath);
                                    HBRUSH pbBr = CreateSolidBrush(isThisPlaying ? RGB(180, 50, 50) : (wav.existsOnDisk ? RGB(35, 45, 60) : RGB(50, 30, 30)));
                                    FillRect(memDC, &rcPlayWav, pbBr);
                                    DeleteObject(pbBr);

                                    SelectObject(memDC, pState->hFontBold);
                                    SetTextColor(memDC, isThisPlaying ? RGB(255, 255, 255) : (wav.existsOnDisk ? RGB(140, 180, 255) : RGB(160, 100, 100)));
                                    DrawTextW(memDC, isThisPlaying ? L"■ Stop" : L"▶ Test", -1, &rcPlayWav, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    if (wav.existsOnDisk)
                                    {
                                        ClickableRegion cr = { ClickableRegion::WavPlay, rcPlayWav, s, t, wav.resolvedDiskPath, f };
                                        pState->hitRegions.push_back(cr);
                                    }

                                    // Delete WAV Button
                                    RECT rcDelWav = { rcWavRow.right - 24, rcWavRow.top, rcWavRow.right, rcWavRow.bottom };
                                    bool hovDw = PtInRect(&rcDelWav, pState->ptMouse);
                                    HBRUSH dwB = CreateSolidBrush(hovDw ? RGB(85, 25, 25) : RGB(38, 22, 26));
                                    HPEN dwP = CreatePen(PS_SOLID, 1, hovDw ? RGB(230, 70, 70) : RGB(90, 42, 48));
                                    SelectObject(memDC, dwB); SelectObject(memDC, dwP);
                                    RoundRect(memDC, rcDelWav.left, rcDelWav.top, rcDelWav.right, rcDelWav.bottom, 3, 3);
                                    DeleteObject(dwB); DeleteObject(dwP);
                                    SetTextColor(memDC, hovDw ? RGB(255, 140, 140) : RGB(190, 90, 90));
                                    DrawTextW(memDC, L"✕", -1, &rcDelWav, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    ClickableRegion dcr = { ClickableRegion::TriggerWavDeleteBtn, rcDelWav, s, t, L"", f };
                                    pState->hitRegions.push_back(dcr);

                                    inY += 26;
                                }

                                // "+ Add WAV File..." Button
                                RECT rcAddWav = { rcTrigCard.left + 24, inY, rcTrigCard.left + 160, inY + 24 };
                                bool hovAw = PtInRect(&rcAddWav, pState->ptMouse);
                                HBRUSH awB = CreateSolidBrush(hovAw ? RGB(28, 55, 42) : RGB(22, 42, 32));
                                HPEN awP = CreatePen(PS_SOLID, 1, hovAw ? RGB(60, 180, 110) : RGB(40, 125, 75));
                                SelectObject(memDC, awB); SelectObject(memDC, awP);
                                RoundRect(memDC, rcAddWav.left, rcAddWav.top, rcAddWav.right, rcAddWav.bottom, 3, 3);
                                DeleteObject(awB); DeleteObject(awP);
                                SetTextColor(memDC, RGB(130, 240, 170));
                                DrawTextW(memDC, L"+ Add WAV File...", -1, &rcAddWav, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                pState->hitRegions.push_back({ ClickableRegion::TriggerAddWavBtn, rcAddWav, s, t, L"" });
                            }

                            bodyY += (subH + 6);
                        }
                    }

                    // ========================================================
                    // ROW 3: DYNAMIC CURVES SECTION
                    // ========================================================
                    RECT rcCvHdr = { rcCard.left + 16, bodyY, rcCard.right - 16, bodyY + 26 };
                    SelectObject(memDC, pState->hFontBold);
                    SetTextColor(memDC, RGB(150, 185, 225));
                    std::wstring cvHdrTxt = L"▼ CURVES (" + std::to_wstring(st.curves.size()) + L" curves):";
                    DrawTextW(memDC, cvHdrTxt.c_str(), -1, &rcCvHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Add Curve Button
                    RECT rcAddCv = { rcCvHdr.right - 115, rcCvHdr.top, rcCvHdr.right, rcCvHdr.bottom };
                    bool hovAc = PtInRect(&rcAddCv, pState->ptMouse);
                    HBRUSH acB = CreateSolidBrush(hovAc ? RGB(65, 45, 20) : RGB(48, 34, 16));
                    HPEN acP = CreatePen(PS_SOLID, 1, hovAc ? RGB(220, 160, 50) : RGB(150, 105, 35));
                    SelectObject(memDC, acB); SelectObject(memDC, acP);
                    RoundRect(memDC, rcAddCv.left, rcAddCv.top, rcAddCv.right, rcAddCv.bottom, 3, 3);
                    DeleteObject(acB); DeleteObject(acP);
                    SetTextColor(memDC, RGB(255, 210, 130));
                    DrawTextW(memDC, L"+ Add Curve", -1, &rcAddCv, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    pState->hitRegions.push_back({ ClickableRegion::StreamAddCurveBtn, rcAddCv, s, 0, L"" });

                    bodyY += 30;

                    if (st.curves.empty())
                    {
                        RECT rcEmptyCv = { rcCard.left + 24, bodyY, rcCard.right - 24, bodyY + 24 };
                        SelectObject(memDC, pState->hFontBody);
                        SetTextColor(memDC, RGB(140, 150, 170));
                        DrawTextW(memDC, L"No dynamic curves defined. Click [ + Add Curve ] to add a Volume/Frequency curve.", -1, &rcEmptyCv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        bodyY += 28;
                    }
                    else
                    {
                        for (int c = 0; c < (int)st.curves.size(); ++c)
                        {
                            auto& cv = st.curves[c];
                            int subH = cv.isCollapsed ? 34 : (32 + 28 + 70 + (int)cv.points.size() * 24 + 30 + 8);

                            RECT rcCvCard = { rcCard.left + 20, bodyY, rcCard.right - 20, bodyY + subH };
                            HBRUSH ccBr = CreateSolidBrush(RGB(18, 22, 30));
                            HPEN ccPen = CreatePen(PS_SOLID, 1, cv.isCollapsed ? RGB(40, 48, 64) : RGB(52, 64, 88));
                            SelectObject(memDC, ccBr); SelectObject(memDC, ccPen);
                            RoundRect(memDC, rcCvCard.left, rcCvCard.top, rcCvCard.right, rcCvCard.bottom, 4, 4);
                            DeleteObject(ccBr); DeleteObject(ccPen);

                            // Curve Sub-card Header Row (32px)
                            RECT rcChHdr = { rcCvCard.left, rcCvCard.top, rcCvCard.right, rcCvCard.top + 32 };

                            // Chevron
                            RECT rcCvChev = { rcChHdr.left + 6, rcChHdr.top, rcChHdr.left + 22, rcChHdr.bottom };
                            SelectObject(memDC, pState->hFontBold);
                            SetTextColor(memDC, RGB(140, 180, 240));
                            DrawTextW(memDC, cv.isCollapsed ? L"▸" : L"▾", -1, &rcCvChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Curve Type Badge
                            RECT rcCvBadge = { rcCvChev.right + 4, rcChHdr.top + 4, rcCvChev.right + 150, rcChHdr.bottom - 4 };
                            bool hovCb = PtInRect(&rcCvBadge, pState->ptMouse);
                            bool isVol = (cv.curveType != L"FrequencyCurve");
                            HBRUSH cbB = CreateSolidBrush(isVol ? (hovCb ? RGB(20, 60, 75) : RGB(16, 46, 58)) : (hovCb ? RGB(70, 50, 20) : RGB(52, 38, 16)));
                            HPEN cbP = CreatePen(PS_SOLID, 1, isVol ? (hovCb ? RGB(40, 165, 210) : RGB(30, 120, 155)) : (hovCb ? RGB(230, 170, 50) : RGB(170, 120, 35)));
                            SelectObject(memDC, cbB); SelectObject(memDC, cbP);
                            RoundRect(memDC, rcCvBadge.left, rcCvBadge.top, rcCvBadge.right, rcCvBadge.bottom, 3, 3);
                            DeleteObject(cbB); DeleteObject(cbP);
                            SetTextColor(memDC, isVol ? RGB(140, 230, 255) : RGB(255, 215, 130));
                            DrawTextW(memDC, cv.curveType.c_str(), -1, &rcCvBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            pState->hitRegions.push_back({ ClickableRegion::CurveTypeBtn, rcCvBadge, s, c, L"" });

                            // Control Tag
                            RECT rcCvInfo = { rcCvBadge.right + 10, rcChHdr.top, rcChHdr.right - 40, rcChHdr.bottom };
                            SelectObject(memDC, pState->hFontBody);
                            SetTextColor(memDC, RGB(190, 205, 225));
                            std::wstring cvInfoTxt = cv.controlType + L"  (" + std::to_wstring(cv.points.size()) + L" points)";
                            DrawTextW(memDC, cvInfoTxt.c_str(), -1, &rcCvInfo, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Delete Curve Button (Right)
                            RECT rcCvDel = { rcChHdr.right - 30, rcChHdr.top + 4, rcChHdr.right - 6, rcChHdr.bottom - 4 };
                            bool hovCd = PtInRect(&rcCvDel, pState->ptMouse);
                            HBRUSH cdB = CreateSolidBrush(hovCd ? RGB(85, 25, 25) : RGB(42, 24, 28));
                            HPEN cdP = CreatePen(PS_SOLID, 1, hovCd ? RGB(230, 70, 70) : RGB(100, 45, 52));
                            SelectObject(memDC, cdB); SelectObject(memDC, cdP);
                            RoundRect(memDC, rcCvDel.left, rcCvDel.top, rcCvDel.right, rcCvDel.bottom, 3, 3);
                            DeleteObject(cdB); DeleteObject(cdP);
                            SetTextColor(memDC, hovCd ? RGB(255, 140, 140) : RGB(200, 100, 100));
                            DrawTextW(memDC, L"✕", -1, &rcCvDel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            pState->hitRegions.push_back({ ClickableRegion::CurveDeleteBtn, rcCvDel, s, c, L"" });

                            // Curve Sub-card toggle
                            RECT rcCvTog = { rcChHdr.left, rcChHdr.top, rcCvDel.left - 4, rcChHdr.bottom };
                            pState->hitRegions.push_back({ ClickableRegion::CurveToggle, rcCvTog, s, c, L"" });

                            // When expanded, render details & visual graph!
                            if (!cv.isCollapsed)
                            {
                                int inY = rcChHdr.bottom + 4;

                                // Sub-row 1: Control Type & Granularity (Dynamically measured)
                                std::wstring ctrlTxt = L"Controlled By: " + cv.controlType + L" ▾";
                                SelectObject(memDC, pState->hFontBody);
                                SIZE ccSz = { 0 };
                                GetTextExtentPoint32W(memDC, ctrlTxt.c_str(), (int)ctrlTxt.size(), &ccSz);
                                int ccW = (std::max)(220, (int)ccSz.cx + 24);
                                RECT rcCvCtrl = { rcCvCard.left + 16, inY, rcCvCard.left + 16 + ccW, inY + 24 };
                                bool hovCc = PtInRect(&rcCvCtrl, pState->ptMouse);
                                HBRUSH ccB = CreateSolidBrush(hovCc ? RGB(36, 42, 56) : RGB(26, 30, 42));
                                HPEN ccP = CreatePen(PS_SOLID, 1, hovCc ? RGB(80, 140, 230) : RGB(50, 58, 78));
                                SelectObject(memDC, ccB); SelectObject(memDC, ccP);
                                RoundRect(memDC, rcCvCtrl.left, rcCvCtrl.top, rcCvCtrl.right, rcCvCtrl.bottom, 3, 3);
                                DeleteObject(ccB); DeleteObject(ccP);

                                RECT rcCvCtrlIn = { rcCvCtrl.left + 10, rcCvCtrl.top, rcCvCtrl.right - 10, rcCvCtrl.bottom };
                                SetTextColor(memDC, RGB(220, 235, 255));
                                DrawTextW(memDC, ctrlTxt.c_str(), -1, &rcCvCtrlIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                pState->hitRegions.push_back({ ClickableRegion::CurveControlBtn, rcCvCtrl, s, c, L"" });

                                // Granularity pill
                                wchar_t gBuf[64]; swprintf_s(gBuf, L"Granularity: %.4f ✏", cv.granularity);
                                SIZE gSz = { 0 };
                                GetTextExtentPoint32W(memDC, gBuf, (int)wcslen(gBuf), &gSz);
                                int gW = (std::max)(150, (int)gSz.cx + 20);
                                RECT rcCvGran = { rcCvCtrl.right + 10, inY, rcCvCtrl.right + 10 + gW, inY + 24 };
                                bool hovCg = PtInRect(&rcCvGran, pState->ptMouse);
                                HBRUSH cgB = CreateSolidBrush(hovCg ? RGB(36, 42, 56) : RGB(26, 30, 42));
                                HPEN cgP = CreatePen(PS_SOLID, 1, hovCg ? RGB(80, 140, 230) : RGB(50, 58, 78));
                                SelectObject(memDC, cgB); SelectObject(memDC, cgP);
                                RoundRect(memDC, rcCvGran.left, rcCvGran.top, rcCvGran.right, rcCvGran.bottom, 3, 3);
                                DeleteObject(cgB); DeleteObject(cgP);

                                RECT rcCvGranIn = { rcCvGran.left + 8, rcCvGran.top, rcCvGran.right - 8, rcCvGran.bottom };
                                SetTextColor(memDC, RGB(180, 210, 240));
                                DrawTextW(memDC, gBuf, -1, &rcCvGranIn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                pState->hitRegions.push_back({ ClickableRegion::CurveGranularityBtn, rcCvGran, s, c, L"" });

                                inY += 28;

                                // Sub-row 2: MINI GRAPHICAL CURVE VISUALIZER PREVIEW (64px)
                                RECT rcGraph = { rcCvCard.left + 16, inY, rcCvCard.right - 16, inY + 64 };
                                HBRUSH gBg = CreateSolidBrush(RGB(13, 15, 22));
                                HPEN gBrd = CreatePen(PS_SOLID, 1, RGB(38, 44, 60));
                                SelectObject(memDC, gBg); SelectObject(memDC, gBrd);
                                RoundRect(memDC, rcGraph.left, rcGraph.top, rcGraph.right, rcGraph.bottom, 4, 4);
                                DeleteObject(gBg); DeleteObject(gBrd);

                                // Dotted grid lines inside graph
                                HPEN gGrid = CreatePen(PS_DOT, 1, RGB(28, 32, 44));
                                oP = SelectObject(memDC, gGrid);
                                for (int gy = 1; gy <= 2; ++gy)
                                {
                                    int yLine = rcGraph.top + gy * (rcGraph.bottom - rcGraph.top) / 3;
                                    MoveToEx(memDC, rcGraph.left + 4, yLine, NULL);
                                    LineTo(memDC, rcGraph.right - 4, yLine);
                                }
                                for (int gx = 1; gx <= 4; ++gx)
                                {
                                    int xLine = rcGraph.left + gx * (rcGraph.right - rcGraph.left) / 5;
                                    MoveToEx(memDC, xLine, rcGraph.top + 4, NULL);
                                    LineTo(memDC, xLine, rcGraph.bottom - 4);
                                }
                                SelectObject(memDC, oP);
                                DeleteObject(gGrid);

                                if (cv.points.size() >= 2)
                                {
                                    float minX = cv.points.front().x;
                                    float maxX = cv.points.back().x;
                                    float minY = cv.points.front().y;
                                    float maxY = cv.points.front().y;
                                    for (const auto& p : cv.points)
                                    {
                                        if (p.x < minX) minX = p.x;
                                        if (p.x > maxX) maxX = p.x;
                                        if (p.y < minY) minY = p.y;
                                        if (p.y > maxY) maxY = p.y;
                                    }
                                    if (maxX <= minX) maxX = minX + 1.0f;
                                    if (maxY <= minY) maxY = minY + 1.0f;

                                    int padLeft = 40, padRight = 30, padTop = 10, padBottom = 12;
                                    int gw = rcGraph.right - rcGraph.left - padLeft - padRight;
                                    int gh = rcGraph.bottom - rcGraph.top - padTop - padBottom;

                                    COLORREF lineCol = isVol ? RGB(0, 215, 255) : RGB(255, 185, 45);
                                    HPEN cvLinePen = CreatePen(PS_SOLID, 2, lineCol);
                                    HBRUSH cvPtBr = CreateSolidBrush(RGB(255, 255, 255));
                                    HPEN cvPtPen = CreatePen(PS_SOLID, 1, lineCol);

                                    std::vector<POINT> ptArr;
                                    for (const auto& p : cv.points)
                                    {
                                        int px = rcGraph.left + padLeft + (int)((p.x - minX) / (maxX - minX) * gw);
                                        int py = rcGraph.bottom - padBottom - (int)((p.y - minY) / (maxY - minY) * gh);
                                        ptArr.push_back({ px, py });
                                    }

                                    oP = SelectObject(memDC, cvLinePen);
                                    for (size_t i = 0; i < ptArr.size(); ++i)
                                    {
                                        if (i == 0) MoveToEx(memDC, ptArr[i].x, ptArr[i].y, NULL);
                                        else LineTo(memDC, ptArr[i].x, ptArr[i].y);
                                    }
                                    SelectObject(memDC, oP);
                                    DeleteObject(cvLinePen);

                                    // Point markers
                                    SelectObject(memDC, cvPtBr);
                                    SelectObject(memDC, cvPtPen);
                                    for (const auto& pt : ptArr)
                                    {
                                        Ellipse(memDC, pt.x - 3, pt.y - 3, pt.x + 4, pt.y + 4);
                                    }
                                    SelectObject(memDC, oB);
                                    SelectObject(memDC, oP);
                                    DeleteObject(cvPtBr);
                                    DeleteObject(cvPtPen);

                                    // Graph corner labels
                                    SelectObject(memDC, pState->hFontSubtitle);
                                    SetTextColor(memDC, RGB(125, 140, 165));
                                    wchar_t lblBuf[64];

                                    // Max Y
                                    swprintf_s(lblBuf, L"%.2f", maxY);
                                    RECT rcMaxY = { rcGraph.left + 4, rcGraph.top + 4, rcGraph.left + 36, rcGraph.top + 20 };
                                    DrawTextW(memDC, lblBuf, -1, &rcMaxY, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                                    // Min Y
                                    swprintf_s(lblBuf, L"%.2f", minY);
                                    RECT rcMinY = { rcGraph.left + 4, rcGraph.bottom - 20, rcGraph.left + 36, rcGraph.bottom - 4 };
                                    DrawTextW(memDC, lblBuf, -1, &rcMinY, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

                                    // Min X
                                    swprintf_s(lblBuf, L"X:%.0f", minX);
                                    RECT rcMinX = { rcGraph.left + padLeft, rcGraph.bottom - 14, rcGraph.left + padLeft + 45, rcGraph.bottom };
                                    DrawTextW(memDC, lblBuf, -1, &rcMinX, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

                                    // Max X
                                    swprintf_s(lblBuf, L"X:%.0f", maxX);
                                    RECT rcMaxX = { rcGraph.right - 55, rcGraph.bottom - 14, rcGraph.right - 6, rcGraph.bottom };
                                    DrawTextW(memDC, lblBuf, -1, &rcMaxX, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
                                }
                                else
                                {
                                    SelectObject(memDC, pState->hFontBody);
                                    SetTextColor(memDC, RGB(130, 140, 160));
                                    DrawTextW(memDC, L"Add at least 2 points to render visual curve slope.", -1, &rcGraph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                }

                                inY += 70;

                                // Sub-row 3: Points Table
                                for (int p = 0; p < (int)cv.points.size(); ++p)
                                {
                                    const auto& pt = cv.points[p];
                                    RECT rcPtRow = { rcCvCard.left + 24, inY, rcCvCard.right - 16, inY + 20 };

                                    SelectObject(memDC, pState->hFontMono);
                                    wchar_t ptBuf[64];
                                    swprintf_s(ptBuf, L"Point #%d:   Input (X) = %8.2f   --->   Output (Y) = %8.4f   [Click to Edit]", p + 1, pt.x, pt.y);
                                    RECT rcPtText = { rcPtRow.left, rcPtRow.top, rcPtRow.right - 30, rcPtRow.bottom };
                                    bool hovPt = PtInRect(&rcPtText, pState->ptMouse);
                                    if (hovPt)
                                    {
                                        HBRUSH ptHovBr = CreateSolidBrush(RGB(30, 38, 54));
                                        FillRect(memDC, &rcPtText, ptHovBr);
                                        DeleteObject(ptHovBr);
                                    }
                                    SetTextColor(memDC, hovPt ? RGB(255, 230, 150) : RGB(185, 200, 220));
                                    DrawTextW(memDC, ptBuf, -1, &rcPtText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    pState->hitRegions.push_back({ ClickableRegion::CurvePointEditBtn, rcPtText, s, c, L"", p });

                                    // Delete Point Button
                                    RECT rcDelPt = { rcPtRow.right - 24, rcPtRow.top, rcPtRow.right, rcPtRow.bottom };
                                    bool hovDp = PtInRect(&rcDelPt, pState->ptMouse);
                                    HBRUSH dpB = CreateSolidBrush(hovDp ? RGB(85, 25, 25) : RGB(38, 22, 26));
                                    HPEN dpP = CreatePen(PS_SOLID, 1, hovDp ? RGB(230, 70, 70) : RGB(90, 42, 48));
                                    SelectObject(memDC, dpB); SelectObject(memDC, dpP);
                                    RoundRect(memDC, rcDelPt.left, rcDelPt.top, rcDelPt.right, rcDelPt.bottom, 3, 3);
                                    DeleteObject(dpB); DeleteObject(dpP);
                                    SetTextColor(memDC, hovDp ? RGB(255, 140, 140) : RGB(190, 90, 90));
                                    DrawTextW(memDC, L"✕", -1, &rcDelPt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    ClickableRegion pcr = { ClickableRegion::CurvePointDeleteBtn, rcDelPt, s, c, L"", p };
                                    pState->hitRegions.push_back(pcr);

                                    inY += 24;
                                }

                                // "+ Add Point" Button
                                RECT rcAddPt = { rcCvCard.left + 24, inY, rcCvCard.left + 130, inY + 24 };
                                bool hovAp = PtInRect(&rcAddPt, pState->ptMouse);
                                HBRUSH apB = CreateSolidBrush(hovAp ? RGB(60, 45, 20) : RGB(42, 32, 16));
                                HPEN apP = CreatePen(PS_SOLID, 1, hovAp ? RGB(210, 155, 45) : RGB(140, 100, 30));
                                SelectObject(memDC, apB); SelectObject(memDC, apP);
                                RoundRect(memDC, rcAddPt.left, rcAddPt.top, rcAddPt.right, rcAddPt.bottom, 3, 3);
                                DeleteObject(apB); DeleteObject(apP);
                                SetTextColor(memDC, RGB(255, 210, 130));
                                DrawTextW(memDC, L"+ Add Point", -1, &rcAddPt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                pState->hitRegions.push_back({ ClickableRegion::CurveAddPointBtn, rcAddPt, s, c, L"" });
                            }

                            bodyY += (subH + 6);
                        }
                    }
                }
            }

            // Bottom "Add New Stream" card button inside the streams container
            curStreamY += 4;
            RECT rcAddCard = { streamCardLeft, curStreamY, streamCardRight, curStreamY + 44 };
            bool hovAddCard = PtInRect(&rcAddCard, pState->ptMouse);
            HBRUSH acBr = CreateSolidBrush(hovAddCard ? RGB(28, 48, 72) : RGB(22, 28, 38));
            HPEN acPen = CreatePen(PS_SOLID, 1, hovAddCard ? RGB(60, 160, 255) : RGB(48, 70, 98));
            HGDIOBJ oAb = SelectObject(memDC, acBr);
            HGDIOBJ oAp = SelectObject(memDC, acPen);
            RoundRect(memDC, rcAddCard.left, rcAddCard.top, rcAddCard.right, rcAddCard.bottom, 6, 6);
            SelectObject(memDC, oAb); SelectObject(memDC, oAp);
            DeleteObject(acBr); DeleteObject(acPen);

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, hovAddCard ? RGB(140, 220, 255) : RGB(140, 175, 215));
            std::wstring addCardTxt = L"+ Add New Stream to Group " + std::to_wstring(grp.groupLevel);
            DrawTextW(memDC, addCardTxt.c_str(), -1, &rcAddCard, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            pState->hitRegions.push_back({ ClickableRegion::StreamAddBtn, rcAddCard, 0, 0, L"" });
            curStreamY += 56;

            // Draw Drag & Drop insertion indicator line
            if (pState->isDraggingStreams && pState->dropTargetIndex >= 0 && !pState->streamCardRects.empty())
            {
                int lineY = 0;
                if (pState->dropTargetIndex < (int)pState->streamCardRects.size())
                    lineY = pState->streamCardRects[pState->dropTargetIndex].top - 6;
                else
                    lineY = pState->streamCardRects.back().bottom + 6;

                RECT rcLine = { streamCardLeft, lineY - 1, streamCardRight, lineY + 2 };
                HBRUSH lineBr = CreateSolidBrush(RGB(0, 195, 255));
                FillRect(memDC, &rcLine, lineBr);
                DeleteObject(lineBr);

                // Indicator circular pins at endpoints
                HBRUSH dotBr = CreateSolidBrush(RGB(0, 225, 255));
                HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                HGDIOBJ oB = SelectObject(memDC, dotBr);
                HGDIOBJ oP = SelectObject(memDC, dotPen);
                Ellipse(memDC, rcLine.left - 5, lineY - 5, rcLine.left + 5, lineY + 5);
                Ellipse(memDC, rcLine.right - 5, lineY - 5, rcLine.right + 5, lineY + 5);
                SelectObject(memDC, oB);
                SelectObject(memDC, oP);
                DeleteObject(dotBr);
                DeleteObject(dotPen);
            }

            // Restore clipping after rendering inner stream viewport
            SelectClipRgn(memDC, NULL);
            DeleteObject(streamClipRgn);

            // Update streams deck inner scroll range and paint scrollbar attached to rcStreamsCard
            int totalStreamsContentH = (curStreamY + pState->scrollY) - (rcStreamsViewport.top + 8);
            int viewportH = rcStreamsViewport.bottom - rcStreamsViewport.top;
            pState->totalContentHeight = totalStreamsContentH;

            RECT rcSbTrack = { rcStreamsCard.right - 13, rcStreamsHdr.bottom + 1, rcStreamsCard.right - 1, rcStreamsCard.bottom - 2 };
            pState->vScrollBar.SetBounds(rcSbTrack);
            pState->vScrollBar.SetRange(0, totalStreamsContentH, viewportH);
            if (pState->scrollY > pState->vScrollBar.GetMax())
            {
                pState->scrollY = pState->vScrollBar.GetMax();
                pState->vScrollBar.SetPos(pState->scrollY);
            }
            pState->vScrollBar.Paint(memDC, true);
        }
        else
        {
            // Empty state
            pState->rcStreamsCard = { 0 };
            pState->rcStreamsViewport = { 0 };
            RECT rcEmpty = { rcCanvas.left + 20, rcCanvas.top + 60, rcCanvas.right - 20, rcCanvas.top + 160 };
            SelectObject(memDC, pState->hFontTitle);
            SetTextColor(memDC, RGB(140, 150, 170));
            DrawTextW(memDC, L"Select an SMS sound file from the left sidebar to inspect and edit its streams.", -1, &rcEmpty, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Blit double buffer to screen
        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBm);
        DeleteObject(hbm);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

HWND CreateSmsEditorView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id)
{
    static bool s_classRegistered = false;
    if (!s_classRegistered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = SmsEditorViewProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = SMS_EDITOR_WNDCLASS;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    SmsEditorViewState* pState = new SmsEditorViewState();
    pState->hInstance = hInstance;
    pState->hParent = hParent;

    HWND hWnd = CreateWindowExW(
        0,
        SMS_EDITOR_WNDCLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, w, h,
        hParent,
        (HMENU)(INT_PTR)id,
        hInstance,
        pState
    );

    return hWnd;
}

void SmsEditorView_SetConsistContext(HWND hWnd, const std::wstring& consistName, const std::wstring& basePath, const std::vector<RSSUnitSoundInfo>& units)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->basePath = basePath;
    std::wstring currentFile = pState->doc.filePath;
    RebuildContextTree(pState, units, currentFile);

    // If no file loaded yet, auto load first sound file found in consist
    if (pState->doc.groups.empty())
    {
        for (const auto& u : units)
        {
            if (!u.engineSounds.empty() && u.engineSounds[0].existsOnDisk)
            {
                LoadFileIntoEditor(pState, u.engineSounds[0].fullDiskPath);
                break;
            }
            if (!u.wagonSounds.empty() && u.wagonSounds[0].existsOnDisk)
            {
                LoadFileIntoEditor(pState, u.wagonSounds[0].fullDiskPath);
                break;
            }
        }
    }

    InvalidateRect(hWnd, NULL, FALSE);
}

void SmsEditorView_SetDirectStockContext(HWND hWnd, const StockItem& item, const std::wstring& basePath, const RSSUnitSoundInfo& unitInfo)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->basePath = basePath;
    std::vector<RSSUnitSoundInfo> singleList = { unitInfo };
    RebuildContextTree(pState, singleList, pState->doc.filePath);

    if (pState->doc.groups.empty())
    {
        if (!unitInfo.engineSounds.empty() && unitInfo.engineSounds[0].existsOnDisk)
            LoadFileIntoEditor(pState, unitInfo.engineSounds[0].fullDiskPath);
        else if (!unitInfo.wagonSounds.empty() && unitInfo.wagonSounds[0].existsOnDisk)
            LoadFileIntoEditor(pState, unitInfo.wagonSounds[0].fullDiskPath);
    }

    InvalidateRect(hWnd, NULL, FALSE);
}

void SmsEditorView_OpenSmsFile(HWND hWnd, const std::wstring& filePath, const std::wstring& basePath)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->basePath = basePath;
    LoadFileIntoEditor(pState, filePath);
    RebuildContextTree(pState, {}, filePath);
    InvalidateRect(hWnd, NULL, FALSE);
}

void SmsEditorView_SetDarkMode(HWND hWnd, BOOL bDark)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;
    pState->bDarkMode = bDark;
    InvalidateRect(hWnd, NULL, FALSE);
}

void SmsEditorView_Resize(HWND hWnd, int x, int y, int w, int h)
{
    if (hWnd && IsWindow(hWnd))
    {
        SetWindowPos(hWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

bool SmsEditorView_HasUnsavedChanges(HWND hWnd)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->doc.isModified : false;
}

std::wstring SmsEditorView_GetCurrentFilePath(HWND hWnd)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return (pState && pState->doc.isModified) ? pState->doc.filePath : L"";
}

bool SmsEditorView_SaveCurrentFile(HWND hWnd)
{
    SmsEditorViewState* pState = (SmsEditorViewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState || pState->doc.filePath.empty()) return false;

    SmsParser_AutoRepairAndNormalize(pState->doc);
    if (SmsParser_SaveToFile(pState->doc, pState->doc.filePath))
    {
        pState->savedGroups = pState->doc.groups;
        pState->doc.isModified = false;
        pState->doc.autoRepairLog.clear();
        App_NotifySoundLibraryChanged(pState->doc.filePath);
        ShowModernMessageBox(hWnd, L"Sound Management System (.sms) file saved successfully!", L"SMS Saved", MB_OK | MB_ICONINFORMATION);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }
    else
    {
        ShowModernMessageBox(hWnd, L"Failed to save SMS file to disk. Check file permissions.", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }
}
