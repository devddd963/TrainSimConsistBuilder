#include "SmsAssetConsolidatorDlg.h"
#include "CustomScrollBar.h"
#include "ModernMessageBox.h"
#include "../SRC/SmsParser.h"
#include "../SRC/AppLogging.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <cwctype>
#include <thread>
#include <atomic>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace
{
    enum TargetMode
    {
        MODE_SAME_FOLDER = 0,
        MODE_SUBFOLDER = 1,
        MODE_STANDALONE_EXPORT = 2
    };

#define WM_CONSOLIDATE_PROGRESS (WM_USER + 601)
#define WM_CONSOLIDATE_COMPLETE (WM_USER + 602)

    struct ConsolidateCopyProgress
    {
        int filesCopied = 0;
        int filesSkipped = 0;
        int filesMissing = 0;
        int totalFiles = 0;
        bool wasCancelled = false;
    };

    struct ScannedWavItem
    {
        std::wstring originalRef;
        std::wstring resolvedDiskPath;
        bool existsOnDisk = false;
        std::wstring baseFileName;
        std::wstring targetRef;
        std::wstring targetDiskPath;
        bool alreadyInTarget = false;
        bool hasCollision = false;
        int usageCount = 0;
        std::wstring usageSummary;
        std::vector<SmsFileRef*> pFileRefs;
    };

    struct ConsolidatorDlgState
    {
        HWND hDlg = NULL;
        HWND hParent = NULL;
        SmsDocument* pDoc = NULL;
        std::wstring basePath;
        std::wstring smsDir;
        std::wstring smsFileName;

        TargetMode mode = MODE_SAME_FOLDER;
        std::wstring subfolderName = L""; // blank by default per user request
        std::wstring exportPath = L"";
        std::wstring computedDestDir = L"";

        bool optUpdateRefs = true;
        bool optSkipExisting = true;
        bool optOpenExplorer = true; // per user request
        bool optAutoDisambiguate = true;

        std::vector<ScannedWavItem> items;

        // Statistics
        int countFound = 0;
        int countMissing = 0;
        int countAlreadyInTarget = 0;
        int countWillCopy = 0;

        // Window Controls
        HWND hEditSubfolder = NULL;
        HWND hEditExportPath = NULL;
        HBRUSH hEditBgBrush = NULL;
        bool isSubfolderFocused = false;
        bool isExportFocused = false;

        // Custom Fonts
        HFONT hFontTitle = NULL;
        HFONT hFontSub = NULL;
        HFONT hFontBody = NULL;
        HFONT hFontBold = NULL;
        HFONT hFontSmall = NULL;

        // Scrollbar & table layout
        CustomScrollBar vScroll;
        RECT rcTableCard = { 0 };
        RECT rcTableHeader = { 0 };
        RECT rcTableBody = { 0 };

        // Clickable interactive hit rects
        RECT rcCloseBtn = { 0 };
        RECT rcModeCard = { 0 };
        RECT rcRadioSameFolder = { 0 };
        RECT rcRadioSubfolder = { 0 };
        RECT rcRadioExport = { 0 };
        RECT rcBoxSubfolder = { 0 };
        RECT rcBoxExport = { 0 };
        RECT rcBtnBrowseExport = { 0 };

        RECT rcCheckUpdateRefs = { 0 };
        RECT rcCheckSkipExisting = { 0 };
        RECT rcCheckOpenExplorer = { 0 };
        RECT rcCheckAutoDisambiguate = { 0 };
        RECT rcBtnApply = { 0 };
        RECT rcBtnCancel = { 0 };

        // Interaction state
        POINT ptMouse = { -1, -1 };
        int hoveredRow = -1;
        bool isHoverClose = false;
        bool isHoverBrowse = false;
        bool isHoverApply = false;
        bool isHoverCancel = false;

        bool isConfirmed = false;
        SmsConsolidateResult result;

        // Background worker & progress state
        std::atomic<bool> isConsolidating{ false };
        std::atomic<bool> cancelRequested{ false };
        std::thread workerThread;
        int progressCopied = 0;
        int progressTotal = 0;
        std::wstring progressCurrentFile;
        RECT rcBottomBar = { 0 };
    };

    static HFONT CreateCustomFont(int pointSize, int weight, const wchar_t* faceName)
    {
        LOGFONTW lf = { 0 };
        lf.lfHeight = -MulDiv(pointSize, GetDpiForSystem(), 72);
        lf.lfWeight = weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, faceName);
        return CreateFontIndirectW(&lf);
    }

    static bool PickFolderModern(HWND hWndOwner, const std::wstring& initialFolder, std::wstring& outFolder)
    {
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
            pDlg->SetTitle(L"Select Target Sound Export Directory");
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

    static void ScanDocumentWavs(ConsolidatorDlgState& state)
    {
        state.items.clear();
        if (!state.pDoc) return;

        std::map<std::wstring, size_t> keyToIndex;

        for (auto& sg : state.pDoc->groups)
        {
            for (size_t sIdx = 0; sIdx < sg.streams.size(); ++sIdx)
            {
                auto& st = sg.streams[sIdx];
                for (auto& tr : st.triggers)
                {
                    for (auto& f : tr.files)
                    {
                        if (f.fileName.empty()) continue;

                        if (f.resolvedDiskPath.empty())
                        {
                            f.resolvedDiskPath = SmsParser_ResolveWavPath(state.pDoc->filePath, f.fileName, state.basePath);
                            f.existsOnDisk = (!f.resolvedDiskPath.empty() && GetFileAttributesW(f.resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                        }

                        std::wstring key = f.existsOnDisk ? f.resolvedDiskPath : f.fileName;
                        for (auto& ch : key) ch = (wchar_t)towlower(ch);

                        auto it = keyToIndex.find(key);
                        if (it == keyToIndex.end())
                        {
                            ScannedWavItem item;
                            item.originalRef = f.fileName;
                            item.resolvedDiskPath = f.resolvedDiskPath;
                            item.existsOnDisk = f.existsOnDisk;

                            size_t lastSlash = f.fileName.find_last_of(L"\\/");
                            item.baseFileName = (lastSlash != std::wstring::npos) ? f.fileName.substr(lastSlash + 1) : f.fileName;
                            if (item.baseFileName.empty()) item.baseFileName = L"unnamed.wav";

                            item.usageCount = 1;
                            item.usageSummary = L"SG" + std::to_wstring(sg.groupLevel) + L" S" + std::to_wstring(sIdx + 1);
                            item.pFileRefs.push_back(&f);

                            keyToIndex[key] = state.items.size();
                            state.items.push_back(item);
                        }
                        else
                        {
                            size_t idx = it->second;
                            state.items[idx].usageCount++;
                            if (state.items[idx].usageCount <= 3)
                            {
                                state.items[idx].usageSummary += L", SG" + std::to_wstring(sg.groupLevel) + L" S" + std::to_wstring(sIdx + 1);
                            }
                            else if (state.items[idx].usageCount == 4)
                            {
                                state.items[idx].usageSummary += L", ...";
                            }
                            state.items[idx].pFileRefs.push_back(&f);
                        }
                    }
                }
            }
        }
    }

    static void UpdateTargetPaths(ConsolidatorDlgState& state)
    {
        std::wstring effectiveDestDir = L"";
        std::wstring relPrefix = L"";

        if (state.mode == MODE_SAME_FOLDER)
        {
            effectiveDestDir = state.smsDir;
            relPrefix = L"";
        }
        else if (state.mode == MODE_SUBFOLDER)
        {
            std::wstring sub = state.subfolderName;
            // Support both forward and backslashes for nested subfolder hierarchy (e.g. audio/horns or sounds\engines)
            std::wstring subNorm = sub;
            for (auto& c : subNorm) { if (c == L'\\') c = L'/'; }

            // Trim leading/trailing spaces and slashes
            while (!subNorm.empty() && (subNorm.front() == L'/' || subNorm.front() == L' ')) subNorm.erase(subNorm.begin());
            while (!subNorm.empty() && (subNorm.back() == L'/' || subNorm.back() == L' ')) subNorm.pop_back();

            // Native Windows disk path
            std::wstring subNative = subNorm;
            for (auto& c : subNative) { if (c == L'/') c = L'\\'; }

            // Strip redundant leading ./ or .\\ sequences if user entered them
            while (subNorm.rfind(L"./", 0) == 0) subNorm.erase(0, 2);
            while (subNorm.rfind(L".\\", 0) == 0) subNorm.erase(0, 2);
            while (!subNorm.empty() && (subNorm.front() == L'/' || subNorm.front() == L'\\' || subNorm.front() == L' ')) subNorm.erase(subNorm.begin());
            while (!subNorm.empty() && (subNorm.back() == L'/' || subNorm.back() == L'\\' || subNorm.back() == L' ')) subNorm.pop_back();

            if (subNorm.empty())
            {
                effectiveDestDir = state.smsDir;
                relPrefix = L"";
            }
            else
            {
                effectiveDestDir = state.smsDir + L"\\" + subNative;
                relPrefix = L"./" + subNorm + L"/";
            }
        }
        else if (state.mode == MODE_STANDALONE_EXPORT)
        {
            effectiveDestDir = state.exportPath;
            relPrefix = L"";
        }

        // Detect collisions
        std::map<std::wstring, std::vector<size_t>> nameToIndices;
        for (size_t i = 0; i < state.items.size(); ++i)
        {
            std::wstring lowName = state.items[i].baseFileName;
            for (auto& c : lowName) c = (wchar_t)towlower(c);
            nameToIndices[lowName].push_back(i);
        }

        state.countFound = 0;
        state.countMissing = 0;
        state.countAlreadyInTarget = 0;
        state.countWillCopy = 0;

        for (size_t i = 0; i < state.items.size(); ++i)
        {
            auto& item = state.items[i];
            std::wstring finalName = item.baseFileName;

            std::wstring lowName = item.baseFileName;
            for (auto& c : lowName) c = (wchar_t)towlower(c);

            item.hasCollision = (nameToIndices[lowName].size() > 1);

            if (state.optAutoDisambiguate && item.hasCollision)
            {
                std::wstring parentDir = L"";
                if (!item.resolvedDiskPath.empty())
                {
                    size_t s1 = item.resolvedDiskPath.find_last_of(L"\\/");
                    if (s1 != std::wstring::npos && s1 > 0)
                    {
                        size_t s0 = item.resolvedDiskPath.find_last_of(L"\\/", s1 - 1);
                        if (s0 != std::wstring::npos)
                            parentDir = item.resolvedDiskPath.substr(s0 + 1, s1 - s0 - 1);
                    }
                }
                if (parentDir.empty()) parentDir = L"Asset" + std::to_wstring(i + 1);
                finalName = parentDir + L"_" + item.baseFileName;
            }

            item.targetRef = relPrefix + finalName;
            for (auto& c : item.targetRef) if (c == L'\\') c = L'/';

            if (!effectiveDestDir.empty())
                item.targetDiskPath = effectiveDestDir + L"\\" + finalName;
            else
                item.targetDiskPath = finalName;

            if (item.existsOnDisk)
            {
                state.countFound++;
                if (!effectiveDestDir.empty() && _wcsicmp(item.resolvedDiskPath.c_str(), item.targetDiskPath.c_str()) == 0)
                {
                    item.alreadyInTarget = true;
                    state.countAlreadyInTarget++;
                }
                else
                {
                    item.alreadyInTarget = false;
                    state.countWillCopy++;
                }
            }
            else
            {
                state.countMissing++;
                item.alreadyInTarget = false;
            }
        }

        state.computedDestDir = effectiveDestDir;
    }

    static void LayoutDialogControls(ConsolidatorDlgState* pState, int w, int h)
    {
        if (!pState || !pState->hDlg) return;

        // Card bounds
        pState->rcModeCard = { 20, 74, w - 20, 366 };

        int fontH = 18;
        if (pState->hDlg && pState->hFontBody)
        {
            HDC hdc = GetDC(pState->hDlg);
            HFONT hOld = (HFONT)SelectObject(hdc, pState->hFontBody);
            TEXTMETRICW tm;
            if (GetTextMetricsW(hdc, &tm))
            {
                fontH = tm.tmHeight + 3;
            }
            SelectObject(hdc, hOld);
            ReleaseDC(pState->hDlg, hdc);
        }

        // Subfolder edit box & container
        pState->rcBoxSubfolder = { 48, 196, w - 48, 230 };
        int boxH = pState->rcBoxSubfolder.bottom - pState->rcBoxSubfolder.top;
        int editH = (fontH < boxH - 4) ? fontH : (boxH - 4);
        int editY1 = pState->rcBoxSubfolder.top + (boxH - editH) / 2;

        SetWindowPos(pState->hEditSubfolder, NULL,
            pState->rcBoxSubfolder.left + 10, editY1,
            (pState->rcBoxSubfolder.right - pState->rcBoxSubfolder.left) - 20, editH,
            SWP_NOZORDER);

        // Export edit box & Browse button
        pState->rcBoxExport = { 48, 302, w - 150, 336 };
        pState->rcBtnBrowseExport = { w - 140, 302, w - 48, 336 };
        int editY2 = pState->rcBoxExport.top + (boxH - editH) / 2;
        SetWindowPos(pState->hEditExportPath, NULL,
            pState->rcBoxExport.left + 10, editY2,
            (pState->rcBoxExport.right - pState->rcBoxExport.left) - 20, editH,
            SWP_NOZORDER);

        // Visibility
        if (pState->mode == MODE_SUBFOLDER)
        {
            ShowWindow(pState->hEditSubfolder, SW_SHOW);
            ShowWindow(pState->hEditExportPath, SW_HIDE);
        }
        else if (pState->mode == MODE_STANDALONE_EXPORT)
        {
            ShowWindow(pState->hEditSubfolder, SW_HIDE);
            ShowWindow(pState->hEditExportPath, SW_SHOW);
        }
        else
        {
            ShowWindow(pState->hEditSubfolder, SW_HIDE);
            ShowWindow(pState->hEditExportPath, SW_HIDE);
        }
    }

    static void DrawCustomCheckbox(HDC hdc, const RECT& rcBox, bool isChecked, bool isHovered, HFONT hFontBold = NULL)
    {
        COLORREF bg = isChecked ? RGB(0, 120, 215) : (isHovered ? RGB(48, 52, 66) : RGB(28, 30, 38));
        COLORREF border = isChecked ? RGB(80, 160, 255) : (isHovered ? RGB(100, 140, 200) : RGB(60, 65, 80));

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
        HPEN holdPen = (HPEN)SelectObject(hdc, hpen);
        RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);
        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);

        if (isChecked)
        {
            HGDIOBJ oldF = SelectObject(hdc, hFontBold);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT rcGlyph = rcBox;
            DrawTextW(hdc, L"✓", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldF);
        }
    }

    static void DrawCustomRadioButton(HDC hdc, const RECT& rcCircle, bool isSelected, bool isHovered)
    {
        int cx = rcCircle.left + (rcCircle.right - rcCircle.left) / 2;
        int cy = rcCircle.top + (rcCircle.bottom - rcCircle.top) / 2;
        int R = 7;

        COLORREF bg = isHovered ? RGB(45, 48, 60) : RGB(26, 28, 34);
        COLORREF border = isSelected ? RGB(0, 140, 240) : (isHovered ? RGB(100, 110, 130) : RGB(65, 70, 85));

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ holdBr = SelectObject(hdc, hbr);
        HGDIOBJ holdPen = SelectObject(hdc, hpen);

        Ellipse(hdc, cx - R, cy - R, cx + R + 1, cy + R + 1);

        if (isSelected)
        {
            int r = 3;
            COLORREF dotCol = RGB(0, 150, 255);
            HBRUSH hbrDot = CreateSolidBrush(dotCol);
            HPEN hpenDot = CreatePen(PS_SOLID, 1, dotCol);
            SelectObject(hdc, hbrDot);
            SelectObject(hdc, hpenDot);
            Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
            DeleteObject(hbrDot);
            DeleteObject(hpenDot);
        }

        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);
    }

    static void DrawModernInputContainer(HDC hdc, const RECT& rcBox, bool isFocused, bool isHovered)
    {
        COLORREF bgCol = RGB(26, 29, 38);
        COLORREF borderCol = isFocused ? RGB(0, 140, 240) : (isHovered ? RGB(80, 95, 125) : RGB(52, 60, 78));

        HBRUSH hbr = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
        HPEN holdPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);

        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hPen);
    }

    static void DrawModernButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, HFONT hFont)
    {
        COLORREF bgCol;
        COLORREF borderCol;
        COLORREF textCol;

        if (isAccent)
        {
            bgCol = isPressed ? RGB(0, 100, 185) : (isHovered ? RGB(20, 140, 235) : RGB(0, 120, 215));
            borderCol = isHovered ? RGB(90, 180, 255) : bgCol;
            textCol = RGB(255, 255, 255);
        }
        else
        {
            bgCol = isPressed ? RGB(32, 34, 42) : (isHovered ? RGB(48, 52, 65) : RGB(38, 40, 50));
            borderCol = isHovered ? RGB(80, 120, 180) : RGB(58, 62, 76);
            textCol = RGB(220, 225, 235);
        }

        HBRUSH hbr = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);
        HPEN holdPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 5, 5);

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

    static LRESULT CALLBACK ConsolidatorDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        ConsolidatorDlgState* pState = (ConsolidatorDlgState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            pState = (ConsolidatorDlgState*)cs->lpCreateParams;
            pState->hDlg = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
            return TRUE;
        }

        case WM_CREATE:
        {
            pState->hFontTitle = CreateCustomFont(13, FW_BOLD, L"Segoe UI");
            pState->hFontSub = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
            pState->hFontBody = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
            pState->hFontBold = CreateCustomFont(9, FW_BOLD, L"Segoe UI");
            pState->hFontSmall = CreateCustomFont(8, FW_NORMAL, L"Segoe UI");
            pState->hEditBgBrush = CreateSolidBrush(RGB(26, 29, 38));

            // Create Subfolder edit control (frameless style 0 - no sunken Win32 border!)
            pState->hEditSubfolder = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                0, 0, 100, 24, hWnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
            SendMessageW(pState->hEditSubfolder, WM_SETFONT, (WPARAM)pState->hFontBody, TRUE);
            SendMessageW(pState->hEditSubfolder, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
            SendMessageW(pState->hEditSubfolder, EM_SETCUEBANNER, TRUE, (LPARAM)L"Enter subfolder name (e.g. wav, audio/horns) or leave blank for root...");

            // Create Export path edit control (frameless style 0 - no sunken Win32 border!)
            pState->hEditExportPath = CreateWindowExW(
                0, L"EDIT", pState->exportPath.c_str(),
                WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                0, 0, 100, 24, hWnd, (HMENU)1002, GetModuleHandleW(NULL), NULL);
            SendMessageW(pState->hEditExportPath, WM_SETFONT, (WPARAM)pState->hFontBody, TRUE);
            SendMessageW(pState->hEditExportPath, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
            SendMessageW(pState->hEditExportPath, EM_SETCUEBANNER, TRUE, (LPARAM)L"Select external directory for release pack...");

            UpdateTargetPaths(*pState);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, RGB(245, 248, 255));
            SetBkColor(hdcEdit, RGB(26, 29, 38));
            return (INT_PTR)pState->hEditBgBrush;
        }

        case WM_COMMAND:
        {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == EN_CHANGE)
            {
                if ((HWND)lParam == pState->hEditSubfolder)
                {
                    wchar_t buf[260] = { 0 };
                    GetWindowTextW(pState->hEditSubfolder, buf, 260);
                    pState->subfolderName = buf;
                    UpdateTargetPaths(*pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                else if ((HWND)lParam == pState->hEditExportPath)
                {
                    wchar_t buf[MAX_PATH] = { 0 };
                    GetWindowTextW(pState->hEditExportPath, buf, MAX_PATH);
                    pState->exportPath = buf;
                    UpdateTargetPaths(*pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            else if (code == EN_SETFOCUS)
            {
                if ((HWND)lParam == pState->hEditSubfolder) { pState->isSubfolderFocused = true; InvalidateRect(hWnd, &pState->rcBoxSubfolder, FALSE); }
                else if ((HWND)lParam == pState->hEditExportPath) { pState->isExportFocused = true; InvalidateRect(hWnd, &pState->rcBoxExport, FALSE); }
            }
            else if (code == EN_KILLFOCUS)
            {
                if ((HWND)lParam == pState->hEditSubfolder) { pState->isSubfolderFocused = false; InvalidateRect(hWnd, &pState->rcBoxSubfolder, FALSE); }
                else if ((HWND)lParam == pState->hEditExportPath) { pState->isExportFocused = false; InvalidateRect(hWnd, &pState->rcBoxExport, FALSE); }
            }
            break;
        }

        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            LayoutDialogControls(pState, w, h);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_CONSOLIDATE_PROGRESS:
        {
            if (pState)
            {
                pState->progressCopied = (int)wParam;
                size_t idx = (size_t)lParam;
                if (idx < pState->items.size())
                {
                    pState->progressCurrentFile = pState->items[idx].baseFileName;
                }
                InvalidateRect(hWnd, &pState->rcBottomBar, FALSE);
            }
            return 0;
        }

        case WM_CONSOLIDATE_COMPLETE:
        {
            auto* pProg = (ConsolidateCopyProgress*)lParam;
            if (!pState || !pProg) { delete pProg; return 0; }

            if (pState->workerThread.joinable())
            {
                pState->workerThread.join();
            }

            pState->isConsolidating = false;

            int filesCopied = pProg->filesCopied;
            int filesSkipped = pProg->filesSkipped;
            int filesMissing = pProg->filesMissing;
            bool wasCancelled = pProg->wasCancelled;
            delete pProg;

            if (wasCancelled)
            {
                pState->isConfirmed = false;
                EnableWindow(pState->hEditSubfolder, TRUE);
                EnableWindow(pState->hEditExportPath, TRUE);
                ShowModernMessageBox(hWnd, (L"Consolidation was cancelled by user.\n\nFiles copied before cancel: " + std::to_wstring(filesCopied)).c_str(), L"Consolidation Cancelled", MB_OK | MB_ICONWARNING);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Update SMS document memory references if requested
            if (pState->optUpdateRefs)
            {
                for (auto& item : pState->items)
                {
                    for (auto* pRef : item.pFileRefs)
                    {
                        if (pRef)
                        {
                            pRef->fileName = item.targetRef;
                            pRef->resolvedDiskPath = item.targetDiskPath;
                            pRef->existsOnDisk = true;
                        }
                    }
                }
            }

            // If Standalone Export mode, save copy of SMS file into destination directory
            if (pState->mode == MODE_STANDALONE_EXPORT)
            {
                std::wstring exportSmsFile = pState->computedDestDir + L"\\" + pState->smsFileName;
                SmsParser_SaveToFile(*pState->pDoc, exportSmsFile);
                App_NotifySoundLibraryChanged(exportSmsFile);
            }

            pState->result.isApplied = true;
            pState->result.docModified = (pState->optUpdateRefs && filesCopied > 0);
            pState->result.filesCopied = filesCopied;
            pState->result.filesSkipped = filesSkipped;
            pState->result.filesMissing = filesMissing;
            pState->result.destFolder = pState->computedDestDir;

            // Open destination folder in Windows Explorer if checked
            if (pState->optOpenExplorer)
            {
                ShellExecuteW(NULL, L"explore", pState->computedDestDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }

            // Show success summary
            std::wstring msg = L"WAV Asset Consolidation Complete!\n\n";
            msg += L"• Files copied: " + std::to_wstring(filesCopied) + L"\n";
            msg += L"• Files already in destination: " + std::to_wstring(filesSkipped) + L"\n";
            if (filesMissing > 0)
            {
                msg += L"• Files missing on disk: " + std::to_wstring(filesMissing) + L"\n";
            }
            msg += L"\nDestination:\n" + pState->computedDestDir;

            if (pState->optUpdateRefs)
            {
                msg += L"\n\nSMS document references have been updated successfully.";
            }

            ShowModernMessageBox(hWnd, msg.c_str(), L"Consolidation Complete", MB_OK | MB_ICONINFORMATION);

            pState->isConfirmed = true;
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (pState)
            {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (pState->vScroll.OnMouseWheel(delta, 28, hWnd))
                {
                    InvalidateRect(hWnd, &pState->rcTableBody, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (!pState) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pState->ptMouse = pt;

            bool redrawNeeded = false;
            bool hovClose = PtInRect(&pState->rcCloseBtn, pt);
            bool hovBrowse = !pState->isConsolidating && PtInRect(&pState->rcBtnBrowseExport, pt);
            bool hovApply = !pState->isConsolidating && PtInRect(&pState->rcBtnApply, pt);
            bool hovCancel = PtInRect(&pState->rcBtnCancel, pt);

            if (hovClose != pState->isHoverClose || hovBrowse != pState->isHoverBrowse ||
                hovApply != pState->isHoverApply || hovCancel != pState->isHoverCancel)
            {
                pState->isHoverClose = hovClose;
                pState->isHoverBrowse = hovBrowse;
                pState->isHoverApply = hovApply;
                pState->isHoverCancel = hovCancel;
                redrawNeeded = true;
            }

            if (pState->vScroll.OnMouseMove(pt, hWnd))
            {
                redrawNeeded = true;
            }

            int prevHoverRow = pState->hoveredRow;
            pState->hoveredRow = -1;
            if (PtInRect(&pState->rcTableBody, pt))
            {
                int scrollY = pState->vScroll.GetPos();
                int relY = pt.y - pState->rcTableBody.top + scrollY;
                int rowIdx = relY / 28;
                if (rowIdx >= 0 && rowIdx < (int)pState->items.size())
                {
                    pState->hoveredRow = rowIdx;
                }
            }
            if (pState->hoveredRow != prevHoverRow)
            {
                redrawNeeded = true;
            }

            if (redrawNeeded)
            {
                InvalidateRect(hWnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            if (pState)
            {
                pState->ptMouse = { -1, -1 };
                pState->isHoverClose = false;
                pState->isHoverBrowse = false;
                pState->isHoverApply = false;
                pState->isHoverCancel = false;
                pState->hoveredRow = -1;
                pState->vScroll.OnMouseLeave(hWnd);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_NCHITTEST:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            if (pState && PtInRect(&pState->rcCloseBtn, pt))
            {
                return HTCLIENT;
            }
            if (pt.y >= 0 && pt.y < 70)
            {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_SETCURSOR:
        {
            if (pState)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd, &pt);
                if (PtInRect(&pState->rcBoxSubfolder, pt) ||
                    (pState->mode == MODE_STANDALONE_EXPORT && PtInRect(&pState->rcBoxExport, pt)))
                {
                    SetCursor(LoadCursor(NULL, IDC_IBEAM));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN:
        {
            if (!pState) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

            if (pState->vScroll.OnLButtonDown(pt, hWnd))
            {
                InvalidateRect(hWnd, &pState->rcTableBody, FALSE);
                return 0;
            }

            // Close button [X]
            if (PtInRect(&pState->rcCloseBtn, pt))
            {
                pState->isConfirmed = false;
                DestroyWindow(hWnd);
                return 0;
            }

            // Draggable title bar / header area
            if (pt.y >= 0 && pt.y < 70)
            {
                ReleaseCapture();
                SendMessageW(hWnd, WM_SYSCOMMAND, 0xF012 /* SC_MOVE | HTCAPTION */, 0);
                return 0;
            }

            // Clicking anywhere inside modern input container focuses the respective edit control
            if (pState->mode == MODE_SUBFOLDER && PtInRect(&pState->rcBoxSubfolder, pt))
            {
                SetFocus(pState->hEditSubfolder);
                return 0;
            }
            if (pState->mode == MODE_STANDALONE_EXPORT && PtInRect(&pState->rcBoxExport, pt))
            {
                SetFocus(pState->hEditExportPath);
                return 0;
            }

            // Radio Mode 0: Same folder as SMS
            if (PtInRect(&pState->rcRadioSameFolder, pt))
            {
                pState->mode = MODE_SAME_FOLDER;
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                LayoutDialogControls(pState, rcClient.right, rcClient.bottom);
                UpdateTargetPaths(*pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Radio Mode 1: Subfolder under SMS
            if (PtInRect(&pState->rcRadioSubfolder, pt))
            {
                pState->mode = MODE_SUBFOLDER;
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                LayoutDialogControls(pState, rcClient.right, rcClient.bottom);
                UpdateTargetPaths(*pState);
                SetFocus(pState->hEditSubfolder);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Radio Mode 2: Standalone Export
            if (PtInRect(&pState->rcRadioExport, pt))
            {
                pState->mode = MODE_STANDALONE_EXPORT;
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                LayoutDialogControls(pState, rcClient.right, rcClient.bottom);
                UpdateTargetPaths(*pState);
                SetFocus(pState->hEditExportPath);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Browse button for export folder
            if (pState->mode == MODE_STANDALONE_EXPORT && PtInRect(&pState->rcBtnBrowseExport, pt))
            {
                std::wstring chosen;
                if (PickFolderModern(hWnd, pState->exportPath.empty() ? pState->smsDir : pState->exportPath, chosen))
                {
                    pState->exportPath = chosen;
                    SetWindowTextW(pState->hEditExportPath, chosen.c_str());
                    UpdateTargetPaths(*pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }

            // Checkbox: Update SMS references
            if (PtInRect(&pState->rcCheckUpdateRefs, pt))
            {
                pState->optUpdateRefs = !pState->optUpdateRefs;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Checkbox: Skip already existing files
            if (PtInRect(&pState->rcCheckSkipExisting, pt))
            {
                pState->optSkipExisting = !pState->optSkipExisting;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Checkbox: Open destination folder in Windows Explorer
            if (PtInRect(&pState->rcCheckOpenExplorer, pt))
            {
                pState->optOpenExplorer = !pState->optOpenExplorer;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // Checkbox: Auto disambiguate
            if (PtInRect(&pState->rcCheckAutoDisambiguate, pt))
            {
                pState->optAutoDisambiguate = !pState->optAutoDisambiguate;
                UpdateTargetPaths(*pState);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            // If currently consolidating, only allow Cancel
            if (pState->isConsolidating)
            {
                if (PtInRect(&pState->rcBtnCancel, pt) && !pState->cancelRequested)
                {
                    pState->cancelRequested = true;
                    pState->progressCurrentFile = L"Cancelling consolidation... please wait";
                    InvalidateRect(hWnd, &pState->rcBottomBar, FALSE);
                }
                return 0;
            }

            // Close button (X)
            if (PtInRect(&pState->rcCloseBtn, pt))
            {
                pState->isConfirmed = false;
                DestroyWindow(hWnd);
                return 0;
            }

            // Cancel button
            if (PtInRect(&pState->rcBtnCancel, pt))
            {
                pState->isConfirmed = false;
                DestroyWindow(hWnd);
                return 0;
            }

            // Apply / Consolidate button
            if (PtInRect(&pState->rcBtnApply, pt))
            {
                // Validate destination directory
                if (pState->computedDestDir.empty())
                {
                    ShowModernMessageBox(hWnd, L"Please select or enter a valid destination directory.", L"Invalid Destination", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                // Create destination directory recursively (supports multi-level nested folders)
                int resMkDir = SHCreateDirectoryExW(NULL, pState->computedDestDir.c_str(), NULL);
                if (resMkDir != ERROR_SUCCESS && resMkDir != ERROR_ALREADY_EXISTS && resMkDir != ERROR_FILE_EXISTS)
                {
                    std::wstring err = L"Could not create destination directory:\n" + pState->computedDestDir;
                    ShowModernMessageBox(hWnd, err.c_str(), L"Directory Creation Error", MB_OK | MB_ICONERROR);
                    return 0;
                }

                pState->isConsolidating = true;
                pState->cancelRequested = false;
                pState->progressCopied = 0;
                pState->progressTotal = (int)pState->items.size();
                pState->progressCurrentFile = L"Initializing copy process...";
                EnableWindow(pState->hEditSubfolder, FALSE);
                EnableWindow(pState->hEditExportPath, FALSE);
                InvalidateRect(hWnd, &pState->rcBottomBar, FALSE);

                HWND hDlg = hWnd;
                pState->workerThread = std::thread([hDlg, pState]() {
                    int filesCopied = 0;
                    int filesSkipped = 0;
                    int filesMissing = 0;
                    int total = (int)pState->items.size();

                    for (size_t i = 0; i < pState->items.size(); ++i)
                    {
                        if (pState->cancelRequested.load())
                        {
                            break;
                        }

                        auto& item = pState->items[i];
                        PostMessageW(hDlg, WM_CONSOLIDATE_PROGRESS, (WPARAM)(i + 1), (LPARAM)i);

                        if (!item.existsOnDisk)
                        {
                            filesMissing++;
                            continue;
                        }

                        if (item.alreadyInTarget && pState->optSkipExisting)
                        {
                            filesSkipped++;
                        }
                        else
                        {
                            if (CopyFileW(item.resolvedDiskPath.c_str(), item.targetDiskPath.c_str(), FALSE))
                            {
                                filesCopied++;
                            }
                            else
                            {
                                filesSkipped++;
                            }
                        }
                    }

                    auto* pResult = new ConsolidateCopyProgress();
                    pResult->filesCopied = filesCopied;
                    pResult->filesSkipped = filesSkipped;
                    pResult->filesMissing = filesMissing;
                    pResult->totalFiles = total;
                    pResult->wasCancelled = pState->cancelRequested.load();
                    PostMessageW(hDlg, WM_CONSOLIDATE_COMPLETE, 0, (LPARAM)pResult);
                });

                return 0;
            }

            return 0;
        }

        case WM_LBUTTONUP:
        {
            if (pState)
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (pState->vScroll.OnLButtonUp(pt, hWnd))
                {
                    InvalidateRect(hWnd, &pState->rcTableBody, FALSE);
                }
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int width = rcClient.right;
            int height = rcClient.bottom;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP hbm = CreateCompatibleBitmap(hdc, width, height);
            HGDIOBJ oldBm = SelectObject(memDC, hbm);

            // 1. Background Fill
            HBRUSH bgBr = CreateSolidBrush(RGB(18, 20, 26));
            FillRect(memDC, &rcClient, bgBr);
            DeleteObject(bgBr);

            // 2. Header Area (y: 0 to 68)
            RECT rcHeader = { 0, 0, width, 68 };
            HBRUSH hHeadBr = CreateSolidBrush(RGB(24, 26, 34));
            FillRect(memDC, &rcHeader, hHeadBr);
            DeleteObject(hHeadBr);

            HPEN hHeadBorder = CreatePen(PS_SOLID, 1, RGB(45, 48, 62));
            HGDIOBJ oldPen = SelectObject(memDC, hHeadBorder);
            MoveToEx(memDC, 0, 68, NULL);
            LineTo(memDC, width, 68);
            SelectObject(memDC, oldPen);
            DeleteObject(hHeadBorder);

            // Header Title & Icon
            SelectObject(memDC, pState->hFontTitle);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(255, 255, 255));
            RECT rcTitle = { 24, 12, width - 60, 36 };
            DrawTextW(memDC, L"📦  Consolidate WAV Sound Assets & Bundler", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Header Subtitle: SMS path & Quick stats pill
            SelectObject(memDC, pState->hFontSub);
            SetTextColor(memDC, RGB(140, 150, 175));
            RECT rcSub = { 24, 38, width - 60, 58 };
            std::wstring subTxt = L"SMS: " + pState->smsFileName + L"  •  " + std::to_wstring(pState->items.size()) + L" unique WAV references ("
                + std::to_wstring(pState->countFound) + L" found, "
                + std::to_wstring(pState->countAlreadyInTarget) + L" already local, "
                + std::to_wstring(pState->countMissing) + L" missing)";
            DrawTextW(memDC, subTxt.c_str(), -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // Close button [X] at top-right
            pState->rcCloseBtn = { width - 42, 14, width - 14, 42 };
            HBRUSH hCloseBr = CreateSolidBrush(pState->isHoverClose ? RGB(70, 30, 35) : RGB(32, 34, 44));
            HPEN hClosePen = CreatePen(PS_SOLID, 1, pState->isHoverClose ? RGB(220, 60, 70) : RGB(55, 60, 75));
            HGDIOBJ oB = SelectObject(memDC, hCloseBr);
            HGDIOBJ oP = SelectObject(memDC, hClosePen);
            RoundRect(memDC, pState->rcCloseBtn.left, pState->rcCloseBtn.top, pState->rcCloseBtn.right, pState->rcCloseBtn.bottom, 4, 4);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hCloseBr);
            DeleteObject(hClosePen);

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, pState->isHoverClose ? RGB(255, 120, 130) : RGB(200, 205, 220));
            RECT rcCloseText = pState->rcCloseBtn;
            DrawTextW(memDC, L"✕", -1, &rcCloseText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // 3. Placement Mode Card (y: 74 to 366)
            HBRUSH hCardBr = CreateSolidBrush(RGB(24, 26, 34));
            HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(42, 45, 58));
            oB = SelectObject(memDC, hCardBr);
            oP = SelectObject(memDC, hCardPen);
            RoundRect(memDC, pState->rcModeCard.left, pState->rcModeCard.top, pState->rcModeCard.right, pState->rcModeCard.bottom, 6, 6);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hCardBr);
            DeleteObject(hCardPen);

            // Card Header
            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, RGB(180, 200, 235));
            RECT rcCardHdr = { pState->rcModeCard.left + 16, pState->rcModeCard.top + 8, pState->rcModeCard.right - 16, pState->rcModeCard.top + 28 };
            DrawTextW(memDC, L"TARGET DESTINATION PLACEMENT MODE", -1, &rcCardHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Mode 0: Same folder as SMS
            pState->rcRadioSameFolder = { pState->rcModeCard.left + 16, pState->rcModeCard.top + 34, pState->rcModeCard.right - 16, pState->rcModeCard.top + 54 };
            RECT rcCircle0 = { pState->rcRadioSameFolder.left, pState->rcRadioSameFolder.top + 3, pState->rcRadioSameFolder.left + 16, pState->rcRadioSameFolder.top + 19 };
            DrawCustomRadioButton(memDC, rcCircle0, pState->mode == MODE_SAME_FOLDER, PtInRect(&pState->rcRadioSameFolder, pState->ptMouse));

            SelectObject(memDC, pState->mode == MODE_SAME_FOLDER ? pState->hFontBold : pState->hFontBody);
            SetTextColor(memDC, pState->mode == MODE_SAME_FOLDER ? RGB(255, 255, 255) : RGB(210, 215, 225));
            RECT rcLbl0 = { rcCircle0.right + 8, pState->rcRadioSameFolder.top, pState->rcRadioSameFolder.right, pState->rcRadioSameFolder.bottom };
            DrawTextW(memDC, L"Place directly in the same folder as the SMS  (Flat structure: \"sound.wav\")", -1, &rcLbl0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(130, 140, 160));
            RECT rcHint0 = { rcLbl0.left, pState->rcRadioSameFolder.bottom, pState->rcRadioSameFolder.right, pState->rcRadioSameFolder.bottom + 16 };
            DrawTextW(memDC, L"All referenced sound files are copied directly into the SMS folder. References in SMS will be updated to \"sound.wav\".", -1, &rcHint0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Mode 1: Subfolder under SMS
            pState->rcRadioSubfolder = { pState->rcModeCard.left + 16, pState->rcModeCard.top + 78, pState->rcModeCard.right - 16, pState->rcModeCard.top + 98 };
            RECT rcCircle1 = { pState->rcRadioSubfolder.left, pState->rcRadioSubfolder.top + 3, pState->rcRadioSubfolder.left + 16, pState->rcRadioSubfolder.top + 19 };
            DrawCustomRadioButton(memDC, rcCircle1, pState->mode == MODE_SUBFOLDER, PtInRect(&pState->rcRadioSubfolder, pState->ptMouse));

            SelectObject(memDC, pState->mode == MODE_SUBFOLDER ? pState->hFontBold : pState->hFontBody);
            SetTextColor(memDC, pState->mode == MODE_SUBFOLDER ? RGB(255, 255, 255) : RGB(210, 215, 225));
            RECT rcLbl1 = { rcCircle1.right + 8, pState->rcRadioSubfolder.top, pState->rcRadioSubfolder.right, pState->rcRadioSubfolder.bottom };
            DrawTextW(memDC, L"Place in dedicated subfolder under SMS directory:", -1, &rcLbl1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(130, 140, 160));
            RECT rcHint1 = { rcLbl1.left, pState->rcRadioSubfolder.bottom, pState->rcRadioSubfolder.right, pState->rcRadioSubfolder.bottom + 16 };
            DrawTextW(memDC, L"Enter subfolder name below. Slashes (e.g. \"audio/horns\" or \"sounds\\engines\") will automatically create nested directories.", -1, &rcHint1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Draw modern rounded input container for Subfolder
            DrawModernInputContainer(memDC, pState->rcBoxSubfolder, pState->isSubfolderFocused, PtInRect(&pState->rcBoxSubfolder, pState->ptMouse));

            // Dynamic live path preview line for Subfolder
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(100, 175, 240));
            RECT rcPreview1 = { pState->rcBoxSubfolder.left, pState->rcBoxSubfolder.bottom + 4, pState->rcBoxSubfolder.right, pState->rcBoxSubfolder.bottom + 20 };
            std::wstring prevRef = pState->subfolderName.empty() ? L"" : (L"./" + pState->subfolderName + L"/");
            for (auto& c : prevRef) if (c == L'\\') c = L'/';
            std::wstring prevSubStr = L"Destination preview: " + (pState->computedDestDir.empty() ? pState->smsDir : pState->computedDestDir)
                + L"  •  References: \"" + prevRef + L"sound.wav\"";
            DrawTextW(memDC, prevSubStr.c_str(), -1, &rcPreview1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // Mode 2: Export Standalone Sound Pack
            pState->rcRadioExport = { pState->rcModeCard.left + 16, pState->rcModeCard.top + 186, pState->rcModeCard.right - 16, pState->rcModeCard.top + 206 };
            RECT rcCircle2 = { pState->rcRadioExport.left, pState->rcRadioExport.top + 3, pState->rcRadioExport.left + 16, pState->rcRadioExport.top + 19 };
            DrawCustomRadioButton(memDC, rcCircle2, pState->mode == MODE_STANDALONE_EXPORT, PtInRect(&pState->rcRadioExport, pState->ptMouse));

            SelectObject(memDC, pState->mode == MODE_STANDALONE_EXPORT ? pState->hFontBold : pState->hFontBody);
            SetTextColor(memDC, pState->mode == MODE_STANDALONE_EXPORT ? RGB(255, 255, 255) : RGB(210, 215, 225));
            RECT rcLbl2 = { rcCircle2.right + 8, pState->rcRadioExport.top, pState->rcRadioExport.right, pState->rcRadioExport.bottom };
            DrawTextW(memDC, L"Export Standalone Sound Pack to New Directory... (Copies SMS + all WAVs for release)", -1, &rcLbl2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(130, 140, 160));
            RECT rcHint2 = { rcLbl2.left, pState->rcRadioExport.bottom, pState->rcRadioExport.right, pState->rcRadioExport.bottom + 16 };
            DrawTextW(memDC, L"Copies the .sms file and all referenced WAVs into this folder, creating an independent, self-contained release package.", -1, &rcHint2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Draw modern rounded input container for Export
            DrawModernInputContainer(memDC, pState->rcBoxExport, pState->isExportFocused, PtInRect(&pState->rcBoxExport, pState->ptMouse));
            DrawModernButton(memDC, pState->rcBtnBrowseExport, L"Browse...", pState->isHoverBrowse, false, false, pState->hFontBold);

            // Dynamic live path preview line for Export
            SelectObject(memDC, pState->hFontSmall);
            SetTextColor(memDC, RGB(100, 175, 240));
            RECT rcPreview2 = { pState->rcBoxExport.left, pState->rcBoxExport.bottom + 4, pState->rcBoxExport.right, pState->rcBoxExport.bottom + 20 };
            std::wstring prevExpStr = L"Export destination: " + (pState->exportPath.empty() ? L"(Choose a directory using Browse...)" : pState->exportPath);
            DrawTextW(memDC, prevExpStr.c_str(), -1, &rcPreview2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // 4. Options Row (y: 374 to 406)
            RECT rcOptRow = { 20, 374, width - 20, 406 };
            int chkW = (rcOptRow.right - rcOptRow.left) / 4;

            // Checkbox 1: Update SMS references
            pState->rcCheckUpdateRefs = { rcOptRow.left, rcOptRow.top, rcOptRow.left + chkW - 8, rcOptRow.bottom };
            RECT rcBox1 = { pState->rcCheckUpdateRefs.left, pState->rcCheckUpdateRefs.top + 7, pState->rcCheckUpdateRefs.left + 18, pState->rcCheckUpdateRefs.top + 25 };
            DrawCustomCheckbox(memDC, rcBox1, pState->optUpdateRefs, PtInRect(&pState->rcCheckUpdateRefs, pState->ptMouse), pState->hFontBold);
            SelectObject(memDC, pState->hFontBody);
            SetTextColor(memDC, RGB(220, 225, 235));
            RECT rcTxt1 = { rcBox1.right + 6, pState->rcCheckUpdateRefs.top, pState->rcCheckUpdateRefs.right, pState->rcCheckUpdateRefs.bottom };
            DrawTextW(memDC, L"Update SMS references", -1, &rcTxt1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Checkbox 2: Skip existing local files
            pState->rcCheckSkipExisting = { rcOptRow.left + chkW, rcOptRow.top, rcOptRow.left + chkW * 2 - 8, rcOptRow.bottom };
            RECT rcBox2 = { pState->rcCheckSkipExisting.left, pState->rcCheckSkipExisting.top + 7, pState->rcCheckSkipExisting.left + 18, pState->rcCheckSkipExisting.top + 25 };
            DrawCustomCheckbox(memDC, rcBox2, pState->optSkipExisting, PtInRect(&pState->rcCheckSkipExisting, pState->ptMouse), pState->hFontBold);
            RECT rcTxt2 = { rcBox2.right + 6, pState->rcCheckSkipExisting.top, pState->rcCheckSkipExisting.right, pState->rcCheckSkipExisting.bottom };
            DrawTextW(memDC, L"Skip files already local", -1, &rcTxt2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Checkbox 3: Open destination in Windows Explorer
            pState->rcCheckOpenExplorer = { rcOptRow.left + chkW * 2, rcOptRow.top, rcOptRow.left + chkW * 3 - 8, rcOptRow.bottom };
            RECT rcBox3 = { pState->rcCheckOpenExplorer.left, pState->rcCheckOpenExplorer.top + 7, pState->rcCheckOpenExplorer.left + 18, pState->rcCheckOpenExplorer.top + 25 };
            DrawCustomCheckbox(memDC, rcBox3, pState->optOpenExplorer, PtInRect(&pState->rcCheckOpenExplorer, pState->ptMouse), pState->hFontBold);
            RECT rcTxt3 = { rcBox3.right + 6, pState->rcCheckOpenExplorer.top, pState->rcCheckOpenExplorer.right, pState->rcCheckOpenExplorer.bottom };
            DrawTextW(memDC, L"Open in Explorer when done", -1, &rcTxt3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Checkbox 4: Auto Disambiguate Collisions
            pState->rcCheckAutoDisambiguate = { rcOptRow.left + chkW * 3, rcOptRow.top, rcOptRow.right, rcOptRow.bottom };
            RECT rcBox4 = { pState->rcCheckAutoDisambiguate.left, pState->rcCheckAutoDisambiguate.top + 7, pState->rcCheckAutoDisambiguate.left + 18, pState->rcCheckAutoDisambiguate.top + 25 };
            DrawCustomCheckbox(memDC, rcBox4, pState->optAutoDisambiguate, PtInRect(&pState->rcCheckAutoDisambiguate, pState->ptMouse), pState->hFontBold);
            RECT rcTxt4 = { rcBox4.right + 6, pState->rcCheckAutoDisambiguate.top, pState->rcCheckAutoDisambiguate.right, pState->rcCheckAutoDisambiguate.bottom };
            DrawTextW(memDC, L"Auto-disambiguate names", -1, &rcTxt4, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // 5. Asset Inventory Table (y: 414 to height - 64)
            pState->rcTableCard = { 20, 414, width - 20, height - 64 };
            pState->rcTableHeader = { pState->rcTableCard.left, pState->rcTableCard.top, pState->rcTableCard.right, pState->rcTableCard.top + 26 };
            pState->rcTableBody = { pState->rcTableCard.left, pState->rcTableHeader.bottom, pState->rcTableCard.right, pState->rcTableCard.bottom };

            HBRUSH hTblBr = CreateSolidBrush(RGB(22, 24, 30));
            HPEN hTblPen = CreatePen(PS_SOLID, 1, RGB(42, 45, 58));
            oB = SelectObject(memDC, hTblBr);
            oP = SelectObject(memDC, hTblPen);
            RoundRect(memDC, pState->rcTableCard.left, pState->rcTableCard.top, pState->rcTableCard.right, pState->rcTableCard.bottom, 4, 4);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hTblBr);
            DeleteObject(hTblPen);

            // Table Header Bar
            HBRUSH hThBr = CreateSolidBrush(RGB(30, 33, 44));
            FillRect(memDC, &pState->rcTableHeader, hThBr);
            DeleteObject(hThBr);

            int colStatusW = 85;
            int colTargetW = 180;
            int colOrigW = 190;
            int colUsageW = 90;
            int colSourceW = (pState->rcTableHeader.right - pState->rcTableHeader.left) - (colStatusW + colTargetW + colOrigW + colUsageW + 16);

            SelectObject(memDC, pState->hFontBold);
            SetTextColor(memDC, RGB(160, 175, 205));

            int curX = pState->rcTableHeader.left + 8;
            RECT rcH0 = { curX, pState->rcTableHeader.top, curX + colStatusW, pState->rcTableHeader.bottom };
            DrawTextW(memDC, L"STATUS", -1, &rcH0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            curX += colStatusW;
            RECT rcH1 = { curX, pState->rcTableHeader.top, curX + colTargetW, pState->rcTableHeader.bottom };
            DrawTextW(memDC, L"TARGET REFERENCE", -1, &rcH1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            curX += colTargetW;
            RECT rcH2 = { curX, pState->rcTableHeader.top, curX + colOrigW, pState->rcTableHeader.bottom };
            DrawTextW(memDC, L"ORIGINAL REFERENCE", -1, &rcH2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            curX += colOrigW;
            RECT rcH3 = { curX, pState->rcTableHeader.top, curX + colSourceW, pState->rcTableHeader.bottom };
            DrawTextW(memDC, L"RESOLVED SOURCE PATH ON DISK", -1, &rcH3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            curX += colSourceW;
            RECT rcH4 = { curX, pState->rcTableHeader.top, curX + colUsageW, pState->rcTableHeader.bottom };
            DrawTextW(memDC, L"USAGE", -1, &rcH4, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Setup scrollbar bounds & range
            int totalListHeight = (int)pState->items.size() * 28;
            int viewHeight = pState->rcTableBody.bottom - pState->rcTableBody.top;
            RECT rcScrollBounds = { pState->rcTableBody.right - 12, pState->rcTableBody.top, pState->rcTableBody.right, pState->rcTableBody.bottom };
            pState->vScroll.SetBounds(rcScrollBounds);
            pState->vScroll.SetRange(0, totalListHeight, viewHeight);

            // Clip drawing to table body
            HRGN rgnClip = CreateRectRgn(pState->rcTableBody.left, pState->rcTableBody.top, pState->rcTableBody.right - 14, pState->rcTableBody.bottom);
            SelectClipRgn(memDC, rgnClip);

            int scrollY = pState->vScroll.GetPos();
            int startRow = scrollY / 28;
            int endRow = (std::min)((int)pState->items.size(), startRow + (viewHeight / 28) + 2);

            for (int r = startRow; r < endRow; ++r)
            {
                const auto& item = pState->items[r];
                int itemY = pState->rcTableBody.top + (r * 28) - scrollY;
                RECT rcRow = { pState->rcTableBody.left, itemY, pState->rcTableBody.right - 14, itemY + 28 };

                bool isHov = (r == pState->hoveredRow);
                COLORREF rowBg = isHov ? RGB(36, 40, 54) : ((r % 2 == 1) ? RGB(25, 27, 35) : RGB(20, 22, 28));
                HBRUSH rBr = CreateSolidBrush(rowBg);
                FillRect(memDC, &rcRow, rBr);
                DeleteObject(rBr);

                int cellX = rcRow.left + 8;

                // Status Pill
                RECT rcPill = { cellX, itemY + 5, cellX + 68, itemY + 23 };
                COLORREF pBg, pBrd, pTxt;
                std::wstring pLabel;
                if (!item.existsOnDisk)
                {
                    pBg = RGB(55, 22, 25); pBrd = RGB(160, 45, 55); pTxt = RGB(255, 130, 140);
                    pLabel = L"Missing";
                }
                else if (item.alreadyInTarget)
                {
                    pBg = RGB(50, 42, 20); pBrd = RGB(150, 120, 40); pTxt = RGB(255, 210, 110);
                    pLabel = L"Local";
                }
                else
                {
                    pBg = RGB(20, 45, 30); pBrd = RGB(40, 140, 75); pTxt = RGB(130, 240, 160);
                    pLabel = L"Ready";
                }

                HBRUSH hPillBr = CreateSolidBrush(pBg);
                HPEN hPillPen = CreatePen(PS_SOLID, 1, pBrd);
                oB = SelectObject(memDC, hPillBr);
                oP = SelectObject(memDC, hPillPen);
                RoundRect(memDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
                SelectObject(memDC, oB);
                SelectObject(memDC, oP);
                DeleteObject(hPillBr);
                DeleteObject(hPillPen);

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, pTxt);
                DrawTextW(memDC, pLabel.c_str(), -1, &rcPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                cellX += colStatusW;

                // Target Reference
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(245, 245, 255));
                RECT rcC1 = { cellX, itemY, cellX + colTargetW - 8, itemY + 28 };
                DrawTextW(memDC, item.targetRef.c_str(), -1, &rcC1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                cellX += colTargetW;

                // Original Reference
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, RGB(180, 190, 205));
                RECT rcC2 = { cellX, itemY, cellX + colOrigW - 8, itemY + 28 };
                DrawTextW(memDC, item.originalRef.c_str(), -1, &rcC2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                cellX += colOrigW;

                // Source Path on Disk
                SelectObject(memDC, pState->hFontBody);
                SetTextColor(memDC, item.existsOnDisk ? RGB(140, 180, 240) : RGB(220, 100, 100));
                RECT rcC3 = { cellX, itemY, cellX + colSourceW - 8, itemY + 28 };
                DrawTextW(memDC, item.resolvedDiskPath.c_str(), -1, &rcC3, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                cellX += colSourceW;

                // Usage
                SelectObject(memDC, pState->hFontSmall);
                SetTextColor(memDC, RGB(140, 150, 170));
                RECT rcC4 = { cellX, itemY, cellX + colUsageW, itemY + 28 };
                std::wstring uStr = std::to_wstring(item.usageCount) + L" str (" + item.usageSummary + L")";
                DrawTextW(memDC, uStr.c_str(), -1, &rcC4, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }

            SelectClipRgn(memDC, NULL);
            DeleteObject(rgnClip);

            // Paint Custom Scrollbar
            pState->vScroll.Paint(memDC);

            // 6. Bottom Action Bar (y: height - 64 to height)
            pState->rcBottomBar = { 0, height - 64, width, height };
            HBRUSH hBotBr = CreateSolidBrush(RGB(24, 26, 34));
            FillRect(memDC, &pState->rcBottomBar, hBotBr);
            DeleteObject(hBotBr);

            HPEN hBotPen = CreatePen(PS_SOLID, 1, RGB(45, 48, 62));
            oP = SelectObject(memDC, hBotPen);
            MoveToEx(memDC, 0, height - 64, NULL);
            LineTo(memDC, width, height - 64);
            SelectObject(memDC, oP);
            DeleteObject(hBotPen);

            pState->rcBtnApply = { width - 310, height - 50, width - 110, height - 14 };
            pState->rcBtnCancel = { width - 100, height - 50, width - 20, height - 14 };

            if (pState->isConsolidating)
            {
                int total = pState->progressTotal > 0 ? pState->progressTotal : (int)pState->items.size();
                int cur = pState->progressCopied;
                int pct = (total > 0) ? (cur * 100 / total) : 0;
                if (pct > 100) pct = 100;

                std::wstring statText = pState->cancelRequested ? L"Cancelling consolidation... please wait" :
                    (L"Copying: " + std::to_wstring(cur) + L" of " + std::to_wstring(total) + L" files (" + std::to_wstring(pct) + L"%)");

                if (!pState->cancelRequested && !pState->progressCurrentFile.empty())
                {
                    statText += L"  •  " + pState->progressCurrentFile;
                }

                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, pState->cancelRequested ? RGB(255, 130, 130) : RGB(225, 235, 255));
                RECT rcProgText = { 24, height - 56, width - 120, height - 34 };
                DrawTextW(memDC, statText.c_str(), -1, &rcProgText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                // Modern rounded progress track & fill
                RECT rcProgressBar = { 24, height - 30, width - 120, height - 18 };
                HBRUSH hTrackBr = CreateSolidBrush(RGB(35, 38, 50));
                HPEN hTrackPen = CreatePen(PS_SOLID, 1, RGB(55, 60, 78));
                HGDIOBJ oldTrB = SelectObject(memDC, hTrackBr);
                HGDIOBJ oldTrP = SelectObject(memDC, hTrackPen);
                RoundRect(memDC, rcProgressBar.left, rcProgressBar.top, rcProgressBar.right, rcProgressBar.bottom, 4, 4);
                SelectObject(memDC, oldTrB);
                SelectObject(memDC, oldTrP);
                DeleteObject(hTrackBr);
                DeleteObject(hTrackPen);

                int barW = rcProgressBar.right - rcProgressBar.left;
                int fillW = (total > 0) ? (barW * cur) / total : 0;
                if (fillW > barW) fillW = barW;
                if (fillW > 0)
                {
                    RECT rcFill = { rcProgressBar.left, rcProgressBar.top, rcProgressBar.left + fillW, rcProgressBar.bottom };
                    HBRUSH hFillBr = CreateSolidBrush(RGB(0, 140, 240));
                    HPEN hFillPen = CreatePen(PS_SOLID, 1, RGB(56, 189, 248));
                    HGDIOBJ oldFiB = SelectObject(memDC, hFillBr);
                    HGDIOBJ oldFiP = SelectObject(memDC, hFillPen);
                    RoundRect(memDC, rcFill.left, rcFill.top, rcFill.right, rcFill.bottom, 4, 4);
                    SelectObject(memDC, oldFiB);
                    SelectObject(memDC, oldFiP);
                    DeleteObject(hFillBr);
                    DeleteObject(hFillPen);
                }

                // Active Cancel button to allow aborting safely
                DrawModernButton(memDC, pState->rcBtnCancel, L"Cancel", pState->isHoverCancel, false, false, pState->hFontBody);
            }
            else
            {
                // Left status text in bottom bar
                SelectObject(memDC, pState->hFontBold);
                SetTextColor(memDC, RGB(180, 200, 235));
                RECT rcBotStatus = { 24, height - 52, width - 340, height - 12 };
                std::wstring botTxt = L"Destination: " + (pState->computedDestDir.empty() ? L"(None)" : pState->computedDestDir)
                    + L"  •  " + std::to_wstring(pState->countWillCopy) + L" files to copy";
                DrawTextW(memDC, botTxt.c_str(), -1, &rcBotStatus, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                DrawModernButton(memDC, pState->rcBtnApply, L"Consolidate & Copy Assets", pState->isHoverApply, false, true, pState->hFontBold);
                DrawModernButton(memDC, pState->rcBtnCancel, L"Cancel", pState->isHoverCancel, false, false, pState->hFontBody);
            }

            // BitBlt double-buffer
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBm);
            DeleteObject(hbm);
            DeleteDC(memDC);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
        {
            if (pState)
            {
                pState->cancelRequested = true;
                if (pState->workerThread.joinable())
                {
                    pState->workerThread.join();
                }
                if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
                if (pState->hFontSub) DeleteObject(pState->hFontSub);
                if (pState->hFontBody) DeleteObject(pState->hFontBody);
                if (pState->hFontBold) DeleteObject(pState->hFontBold);
                if (pState->hFontSmall) DeleteObject(pState->hFontSmall);
                if (pState->hEditBgBrush) DeleteObject(pState->hEditBgBrush);
            }
            return 0;
        }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

bool ShowSmsAssetConsolidatorDialog(
    HWND hWndParent,
    SmsDocument& doc,
    const std::wstring& basePath,
    SmsConsolidateResult& outResult
)
{
    static bool s_registered = false;
    const wchar_t* szClassName = L"TSCB_SmsAssetConsolidatorModal";
    if (!s_registered)
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ConsolidatorDlgProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(18, 20, 26));
        wc.lpszClassName = szClassName;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    ConsolidatorDlgState state;
    state.hParent = hWndParent;
    state.pDoc = &doc;
    state.basePath = basePath;

    // Determine SMS directory and file name
    size_t lastSlash = doc.filePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
    {
        state.smsDir = doc.filePath.substr(0, lastSlash);
        state.smsFileName = doc.filePath.substr(lastSlash + 1);
    }
    else
    {
        state.smsDir = L"";
        state.smsFileName = doc.filePath.empty() ? L"Untitled.sms" : doc.filePath;
    }

    state.exportPath = state.smsDir;

    // Scan all WAV files
    ScanDocumentWavs(state);
    UpdateTargetPaths(state);

    int w = 960, h = 740;
    HWND hRoot = hWndParent ? GetAncestor(hWndParent, GA_ROOT) : NULL;
    if (!hRoot || !IsWindow(hRoot)) hRoot = hWndParent;

    HMONITOR hMon = MonitorFromWindow(hRoot ? hRoot : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT rcWork;
    if (GetMonitorInfoW(hMon, &mi)) {
        rcWork = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    }

    int workH = rcWork.bottom - rcWork.top;
    if (h > workH - 40) h = workH - 40;
    int workW = rcWork.right - rcWork.left;
    if (w > workW - 40) w = workW - 40;

    RECT rcParent;
    if (hRoot && IsWindow(hRoot) && !IsIconic(hRoot))
    {
        GetWindowRect(hRoot, &rcParent);
    }
    else
    {
        rcParent = rcWork;
    }

    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    if (x + w > rcWork.right) x = rcWork.right - w;
    if (y + h > rcWork.bottom) y = rcWork.bottom - h;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;
    if (y < rcWork.top) y = rcWork.top;

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClassName, L"Consolidate WAV Sound Assets & Bundler",
        WS_POPUP | WS_VISIBLE, x, y, w, h,
        hWndParent, NULL, GetModuleHandleW(NULL), &state);

    if (!hDlg) return false;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF dwmBorder = RGB(55, 60, 75);
    DwmSetWindowAttribute(hDlg, DWMWA_BORDER_COLOR, &dwmBorder, sizeof(dwmBorder));

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, FALSE);
    }

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_ESCAPE)
            {
                if (state.isConsolidating)
                {
                    state.cancelRequested = true;
                    state.progressCurrentFile = L"Cancelling consolidation... please wait";
                    InvalidateRect(hDlg, &state.rcBottomBar, FALSE);
                    continue;
                }
                state.isConfirmed = false;
                if (hWndParent && IsWindow(hWndParent))
                {
                    EnableWindow(hWndParent, TRUE);
                    SetForegroundWindow(hWndParent);
                    SetActiveWindow(hWndParent);
                }
                DestroyWindow(hDlg);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hWndParent && IsWindow(hWndParent))
    {
        EnableWindow(hWndParent, TRUE);
        SetForegroundWindow(hWndParent);
        SetActiveWindow(hWndParent);
    }

    if (state.workerThread.joinable())
    {
        state.workerThread.join();
    }
    outResult = state.result;
    return state.isConfirmed && state.result.isApplied;
}
