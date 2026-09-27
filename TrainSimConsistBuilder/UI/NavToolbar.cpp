#include "NavToolbar.h"
#include "CustomScrollBar.h"
#include "CustomDropDownMenu.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#define IDC_NAV_EDIT_ADDR   2105
#define IDC_NAV_EDIT_SEARCH 2106

enum NavIconIndex
{
    ICON_NONE = -1,
    ICON_BACK = 0,
    ICON_FORWARD = 1,
    ICON_UP = 2,
    ICON_REFRESH = 3,
    ICON_ADDR_PILL = 5,
    ICON_ADDR_CLEAR = 6,
    ICON_SEARCH_CLEAR = 7,
    ICON_BC_BASE = 100 // Indices 100+ reserved for breadcrumb elements
};

enum BreadcrumbType
{
    BC_ROOT_PC,       // "This PC" monitor icon
    BC_CHEVRON_ROOT,  // Chevron after "This PC"
    BC_ELLIPSIS,      // "..." collapsed ancestor indicator
    BC_CHEVRON_ELLIP, // Chevron after "..."
    BC_SEGMENT,       // Folder name or Drive letter
    BC_CHEVRON        // Chevron after folder segment
};

struct BreadcrumbElement
{
    BreadcrumbType type;
    RECT rc;
    std::wstring label;
    std::wstring fullPath;
    int segmentIndex;
};

struct NavMenuItem
{
    std::wstring label;
    std::wstring targetPath;
    bool isEnabled;
};

struct NavToolbarState
{
    BOOL bDarkMode = TRUE;
    HFONT hFontMain = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontChevron = NULL;
    HBRUSH hbrPillDark = NULL;
    HBRUSH hbrPillEditDark = NULL;
    HBRUSH hbrPillLight = NULL;

    HWND hEditAddr = NULL;
    HWND hEditSearch = NULL;

    int hoverIndex = ICON_NONE;
    int pressedIndex = ICON_NONE;
    int hoverBreadcrumb = -1;
    BOOL bTrackingMouse = FALSE;
    BOOL bEditingAddress = FALSE;
    BOOL bSearchFocused = FALSE;
    BOOL bInternalChange = FALSE;

    wchar_t szCurrentPath[MAX_PATH] = L"C:\\TrainSim\\TRAINS\\CONSISTS";
    wchar_t szAddressPlaceholder[256] = L"Enter MSTS / Open Rails Installation Directory...";
    wchar_t szSearchPlaceholder[256] = L"Search Consist Files (*.con)...";
    std::vector<BreadcrumbElement> breadcrumbElements;
    std::vector<std::pair<std::wstring, std::wstring>> collapsedFolders;
};

static HFONT CreateMdl2IconFont(float pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
        hFont = CreateFontIndirectW(&lf);
    }
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}

static HFONT CreateSystemUiFont(float pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}

static void CalculatePillLayout(int clientWidth, int& addrPillLeft, int& addrPillRight, int& searchPillLeft, int& searchPillRight)
{
    addrPillLeft = 8;
    int rightEdge = clientWidth - 8;
    int availableWidth = rightEdge - addrPillLeft;
    if (availableWidth < 120) availableWidth = 120;

    int searchWidth = 260;
    if (clientWidth < 780)
    {
        searchWidth = (availableWidth * 32) / 100;
        if (searchWidth < 110) searchWidth = 110;
        if (searchWidth > 260) searchWidth = 260;
    }

    searchPillRight = rightEdge;
    searchPillLeft = searchPillRight - searchWidth;
    addrPillRight = searchPillLeft - 8;

    if (addrPillRight < addrPillLeft + 40)
    {
        addrPillRight = addrPillLeft + 40;
    }
}





// Build breadcrumb layout with smart ellipsis truncation and proper padding
static void RebuildBreadcrumbs(NavToolbarState* pState, HDC hdc, int addrPillLeft, int addrPillRight)
{
    if (!pState) return;

    pState->breadcrumbElements.clear();
    pState->collapsedFolders.clear();

    std::wstring pathStr(pState->szCurrentPath);
    std::vector<std::wstring> rawSegments;
    std::vector<std::wstring> rawFullPaths;

    size_t start = 0;
    size_t end = 0;
    std::wstring accumulatedPath = L"";

    while ((end = pathStr.find_first_of(L"\\/", start)) != std::wstring::npos)
    {
        if (end > start)
        {
            std::wstring seg = pathStr.substr(start, end - start);
            rawSegments.push_back(seg);
            if (accumulatedPath.empty())
            {
                accumulatedPath = seg + L"\\";
            }
            else
            {
                if (accumulatedPath.back() != L'\\') accumulatedPath += L"\\";
                accumulatedPath += seg;
            }
            rawFullPaths.push_back(accumulatedPath);
        }
        start = end + 1;
    }
    if (start < pathStr.length())
    {
        std::wstring seg = pathStr.substr(start);
        rawSegments.push_back(seg);
        if (accumulatedPath.empty())
        {
            accumulatedPath = seg;
        }
        else
        {
            if (accumulatedPath.back() != L'\\') accumulatedPath += L"\\";
            accumulatedPath += seg;
        }
        rawFullPaths.push_back(accumulatedPath);
    }

    int curX = addrPillLeft + 4;
    int topY = 7;
    int botY = 37;

    // 1. "This PC" Desktop Computer Icon Button
    BreadcrumbElement elemRoot;
    elemRoot.type = BC_ROOT_PC;
    elemRoot.rc = { curX, topY, curX + 28, botY };
    elemRoot.label = L"This PC";
    elemRoot.fullPath = L"";
    elemRoot.segmentIndex = -1;
    pState->breadcrumbElements.push_back(elemRoot);
    curX += 28;

    // 2. Chevron after "This PC"
    BreadcrumbElement elemChevRoot;
    elemChevRoot.type = BC_CHEVRON_ROOT;
    elemChevRoot.rc = { curX, topY, curX + 22, botY };
    elemChevRoot.label = L"";
    elemChevRoot.fullPath = L"";
    elemChevRoot.segmentIndex = -1;
    pState->breadcrumbElements.push_back(elemChevRoot);
    curX += 22;

    if (rawSegments.empty()) return;

    // Measure all segments with 10.5pt font + 16px wide capsule padding (8px left, 8px right)
    HFONT hOldFont = (HFONT)SelectObject(hdc, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    std::vector<int> segWidths;
    int totalNeeded = curX;
    for (size_t i = 0; i < rawSegments.size(); ++i)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdc, rawSegments[i].c_str(), (int)rawSegments[i].length(), &sz);
        int segW = sz.cx + 16; // 8px padding each side
        segWidths.push_back(segW);
        totalNeeded += segW + 22; // seg + chevron
    }
    SelectObject(hdc, hOldFont);

    int maxRight = addrPillRight - 10;
    int available = maxRight - curX;

    if (totalNeeded <= maxRight)
    {
        // All segments fit comfortably!
        for (size_t i = 0; i < rawSegments.size(); ++i)
        {
            BreadcrumbElement segElem;
            segElem.type = BC_SEGMENT;
            segElem.rc = { curX, topY, curX + segWidths[i], botY };
            segElem.label = rawSegments[i];
            segElem.fullPath = rawFullPaths[i];
            segElem.segmentIndex = (int)i;
            pState->breadcrumbElements.push_back(segElem);
            curX += segWidths[i];

            BreadcrumbElement chevElem;
            chevElem.type = BC_CHEVRON;
            chevElem.rc = { curX, topY, curX + 22, botY };
            chevElem.label = L"";
            chevElem.fullPath = rawFullPaths[i];
            chevElem.segmentIndex = (int)i;
            pState->breadcrumbElements.push_back(chevElem);
            curX += 22;
        }
    }
    else
    {
        // Smart Truncation: Collapse intermediary folders into "..."
        // Priority: Keep Leaf (current folder) visible
        int ellipsisWidth = 24;
        int ellipsisChevronWidth = 22;

        int leafIndex = (int)rawSegments.size() - 1;
        int leafW = segWidths[leafIndex];
        if (leafW > available - ellipsisWidth - ellipsisChevronWidth - 22)
        {
            leafW = available - ellipsisWidth - ellipsisChevronWidth - 22;
            if (leafW < 40) leafW = 40;
        }

        // Determine which trailing segments can fit
        int visibleStart = leafIndex;
        int usedWidth = ellipsisWidth + ellipsisChevronWidth + leafW + 22;

        for (int i = leafIndex - 1; i >= 0; --i)
        {
            if (usedWidth + segWidths[i] + 22 <= available)
            {
                usedWidth += segWidths[i] + 22;
                visibleStart = i;
            }
            else
            {
                break;
            }
        }

        // Store collapsed ancestors
        for (int i = 0; i < visibleStart; ++i)
        {
            pState->collapsedFolders.push_back({ rawSegments[i], rawFullPaths[i] });
        }

        // 1. Add "..." button
        BreadcrumbElement ellipElem;
        ellipElem.type = BC_ELLIPSIS;
        ellipElem.rc = { curX, topY, curX + ellipsisWidth, botY };
        ellipElem.label = L"...";
        ellipElem.fullPath = L"";
        ellipElem.segmentIndex = -1;
        pState->breadcrumbElements.push_back(ellipElem);
        curX += ellipsisWidth;

        // 2. Add Chevron after "..."
        BreadcrumbElement ellipChev;
        ellipChev.type = BC_CHEVRON_ELLIP;
        ellipChev.rc = { curX, topY, curX + ellipsisChevronWidth, botY };
        ellipChev.label = L"";
        ellipChev.fullPath = pState->collapsedFolders.empty() ? L"" : pState->collapsedFolders.back().second;
        ellipChev.segmentIndex = -1;
        pState->breadcrumbElements.push_back(ellipChev);
        curX += ellipsisChevronWidth;

        // 3. Add visible trailing segments
        for (size_t i = visibleStart; i < rawSegments.size(); ++i)
        {
            int segW = segWidths[i];
            if (curX + segW > maxRight - 22)
            {
                segW = (maxRight - 22) - curX;
                if (segW < 20) segW = 20;
            }

            BreadcrumbElement segElem;
            segElem.type = BC_SEGMENT;
            segElem.rc = { curX, topY, curX + segW, botY };
            segElem.label = rawSegments[i];
            segElem.fullPath = rawFullPaths[i];
            segElem.segmentIndex = (int)i;
            pState->breadcrumbElements.push_back(segElem);
            curX += segW;

            BreadcrumbElement chevElem;
            chevElem.type = BC_CHEVRON;
            chevElem.rc = { curX, topY, curX + 22, botY };
            chevElem.label = L"";
            chevElem.fullPath = rawFullPaths[i];
            chevElem.segmentIndex = (int)i;
            pState->breadcrumbElements.push_back(chevElem);
            curX += 22;
        }
    }
}

static NavIconIndex HitTestIcon(NavToolbarState* pState, int x, int y, int addrPillLeft, int addrPillRight, int searchPillLeft, int searchPillRight)
{
    if (!pState) return ICON_NONE;
    POINT pt = { x, y };

    if (pState->bEditingAddress)
    {
        RECT rcClear = { addrPillRight - 28, 5, addrPillRight - 6, 39 };
        if (PtInRect(&rcClear, pt))
        {
            return ICON_ADDR_CLEAR;
        }
    }

    if (pState->hEditSearch && GetWindowTextLengthW(pState->hEditSearch) > 0)
    {
        RECT rcSearchClear = { searchPillRight - 28, 5, searchPillRight - 6, 39 };
        if (PtInRect(&rcSearchClear, pt))
        {
            return ICON_SEARCH_CLEAR;
        }
    }

    // Hit test individual breadcrumb elements inside the address bar
    for (size_t i = 0; i < pState->breadcrumbElements.size(); ++i)
    {
        if (PtInRect(&pState->breadcrumbElements[i].rc, pt))
        {
            pState->hoverBreadcrumb = (int)i;
            return (NavIconIndex)(ICON_BC_BASE + i);
        }
    }

    RECT rcPill = { addrPillLeft, 5, addrPillRight, 39 };
    if (PtInRect(&rcPill, pt))
    {
        return ICON_ADDR_PILL;
    }

    return ICON_NONE;
}

// -------------------------------------------------------------
// Interactive Dropdown Menus for Drives, Folders, and Ellipsis
// -------------------------------------------------------------

static void ShowDriveDropdown(HWND hWnd, RECT rcAnchor)
{
    wchar_t szDrives[512] = { 0 };
    DWORD dwLen = GetLogicalDriveStringsW(511, szDrives);
    if (dwLen == 0) return;

    std::vector<std::wstring> drivePaths;
    std::vector<DropDownItem> items;
    const wchar_t* pDrive = szDrives;

    while (*pDrive)
    {
        wchar_t szVolName[MAX_PATH] = { 0 };
        GetVolumeInformationW(pDrive, szVolName, MAX_PATH, NULL, NULL, NULL, NULL, 0);

        std::wstring driveLetter = pDrive;
        if (!driveLetter.empty() && driveLetter.back() == L'\\')
        {
            driveLetter.pop_back();
        }

        std::wstring displayLabel;
        if (wcslen(szVolName) > 0)
        {
            displayLabel = std::wstring(szVolName) + L" (" + driveLetter + L")";
        }
        else
        {
            UINT driveType = GetDriveTypeW(pDrive);
            if (driveType == DRIVE_FIXED)
            {
                displayLabel = L"Local Disk (" + driveLetter + L")";
            }
            else if (driveType == DRIVE_CDROM)
            {
                displayLabel = L"CD Drive (" + driveLetter + L")";
            }
            else if (driveType == DRIVE_REMOVABLE)
            {
                displayLabel = L"USB Drive (" + driveLetter + L")";
            }
            else
            {
                displayLabel = L"Drive (" + driveLetter + L")";
            }
        }

        drivePaths.push_back(pDrive);
        items.push_back(DropDownItem::Action((int)drivePaths.size(), L"\xE7F1", displayLabel, L"", false, true));
        pDrive += wcslen(pDrive) + 1;
    }

    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items);
    if (chosen > 0 && chosen <= (int)drivePaths.size())
    {
        std::wstring target = drivePaths[chosen - 1];
        NavToolbar_SetPath(hWnd, target.c_str());
        SendMessage(GetParent(hWnd), WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
    }
}

static void ShowFolderDropdown(HWND hWnd, const std::wstring& folderPath, RECT rcAnchor)
{
    if (folderPath.empty())
    {
        ShowDriveDropdown(hWnd, rcAnchor);
        return;
    }

    std::wstring searchPattern = folderPath;
    if (searchPattern.back() != L'\\') searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);

    std::vector<std::wstring> subfolders;
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
            {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
                {
                    subfolders.push_back(fd.cFileName);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(subfolders.begin(), subfolders.end());

    std::vector<std::wstring> folderPaths;
    std::vector<DropDownItem> items;
    if (subfolders.empty())
    {
        items.push_back(DropDownItem::Action(0, L"", L"(Empty folder)", L"", false, false));
    }
    else
    {
        for (const auto& sf : subfolders)
        {
            std::wstring target = folderPath;
            if (target.back() != L'\\') target += L"\\";
            target += sf;
            folderPaths.push_back(target);
            items.push_back(DropDownItem::Action((int)folderPaths.size(), L"\xE8B7", sf, L"", false, true));
        }
    }

    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items);
    if (chosen > 0 && chosen <= (int)folderPaths.size())
    {
        std::wstring target = folderPaths[chosen - 1];
        NavToolbar_SetPath(hWnd, target.c_str());
        SendMessage(GetParent(hWnd), WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
    }
}

static void ShowEllipsisDropdown(HWND hWnd, NavToolbarState* pState, RECT rcAnchor)
{
    if (!pState || pState->collapsedFolders.empty()) return;

    std::vector<std::wstring> targets;
    std::vector<DropDownItem> items;
    for (const auto& cf : pState->collapsedFolders)
    {
        targets.push_back(cf.second);
        items.push_back(DropDownItem::Action((int)targets.size(), L"\xE8B7", cf.first, L"", false, true));
    }

    int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items);
    if (chosen > 0 && chosen <= (int)targets.size())
    {
        std::wstring target = targets[chosen - 1];
        NavToolbar_SetPath(hWnd, target.c_str());
        SendMessage(GetParent(hWnd), WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
    }
}

// Subclass for Edit controls to capture Enter Key, Escape, and Focus events
static LRESULT CALLBACK NavEditSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    HWND hToolbar = GetParent(hWnd);
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hToolbar, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            HWND hMainWnd = GetParent(hToolbar);

            if (uIdSubclass == IDC_NAV_EDIT_ADDR)
            {
                if (pState)
                {
                    wchar_t szNewPath[MAX_PATH] = { 0 };
                    GetWindowTextW(hWnd, szNewPath, MAX_PATH);
                    if (wcslen(szNewPath) > 0)
                    {
                        wcscpy_s(pState->szCurrentPath, szNewPath);
                    }
                    pState->bEditingAddress = FALSE;
                }
                ShowWindow(hWnd, SW_HIDE);
                InvalidateRect(hToolbar, NULL, TRUE);

                SendMessage(hMainWnd, WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
            }
            else if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
            {
                SendMessage(hMainWnd, WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)hWnd);
            }
            return 0;
        }
        else if (wParam == VK_ESCAPE)
        {
            if (uIdSubclass == IDC_NAV_EDIT_ADDR)
            {
                if (pState) pState->bEditingAddress = FALSE;
                ShowWindow(hWnd, SW_HIDE);
                InvalidateRect(hToolbar, NULL, TRUE);
                return 0;
            }
            else if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
            {
                if (pState && pState->hEditSearch)
                {
                    SetWindowTextW(pState->hEditSearch, L"");
                    SendMessage(GetParent(hToolbar), WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)pState->hEditSearch);
                    InvalidateRect(hToolbar, NULL, FALSE);
                }
                return 0;
            }
        }
        break;

    case WM_SETFOCUS:
        if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
        {
            if (pState) pState->bSearchFocused = TRUE;
            InvalidateRect(hToolbar, NULL, FALSE);
        }
        break;

    case WM_KILLFOCUS:
        if (uIdSubclass == IDC_NAV_EDIT_ADDR)
        {
            if (pState)
            {
                wchar_t szNewPath[MAX_PATH] = { 0 };
                GetWindowTextW(hWnd, szNewPath, MAX_PATH);
                if (wcslen(szNewPath) > 0)
                {
                    wcscpy_s(pState->szCurrentPath, szNewPath);
                }
                pState->bEditingAddress = FALSE;
            }
            ShowWindow(hWnd, SW_HIDE);
            InvalidateRect(hToolbar, NULL, TRUE);
        }
        else if (uIdSubclass == IDC_NAV_EDIT_SEARCH)
        {
            if (pState) pState->bSearchFocused = FALSE;
            InvalidateRect(hToolbar, NULL, FALSE);
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, NavEditSubclass, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK NavToolbarProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        pState = new NavToolbarState();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_CREATE:
    {
        if (!pState)
        {
            pState = new NavToolbarState();
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        }

        pState->hFontMain = CreateSystemUiFont(10.5f, FW_NORMAL);
        pState->hFontIcons = CreateMdl2IconFont(11.0f, FW_NORMAL);
        pState->hFontChevron = CreateMdl2IconFont(9.5f, FW_SEMIBOLD);

        pState->hbrPillDark = CreateSolidBrush(RGB(62, 26, 31));
        pState->hbrPillEditDark = CreateSolidBrush(RGB(42, 18, 22));
        pState->hbrPillLight = CreateSolidBrush(RGB(240, 240, 240));

        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // 1. Address Edit Control (Hidden initially; shown when user clicks Address Pill)
        pState->hEditAddr = CreateWindowExW(0, L"EDIT", pState->szCurrentPath,
            WS_CHILD | ES_AUTOHSCROLL,
            156, 11, 400, 22, hWnd, (HMENU)IDC_NAV_EDIT_ADDR, hInst, NULL);

        SetWindowSubclass(pState->hEditAddr, NavEditSubclass, IDC_NAV_EDIT_ADDR, 0);
        if (pState->hFontMain) SendMessage(pState->hEditAddr, WM_SETFONT, (WPARAM)pState->hFontMain, MAKELPARAM(FALSE, 0));
        SendMessage(pState->hEditAddr, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SendMessageW(pState->hEditAddr, EM_SETCUEBANNER, TRUE, (LPARAM)pState->szAddressPlaceholder);

        // 2. Search Edit Control (Inside Search Pill)
        pState->hEditSearch = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            600, 11, 190, 22, hWnd, (HMENU)IDC_NAV_EDIT_SEARCH, hInst, NULL);

        SetWindowSubclass(pState->hEditSearch, NavEditSubclass, IDC_NAV_EDIT_SEARCH, 0);
        if (pState->hFontMain) SendMessage(pState->hEditSearch, WM_SETFONT, (WPARAM)pState->hFontMain, MAKELPARAM(FALSE, 0));
        SendMessage(pState->hEditSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SendMessageW(pState->hEditSearch, EM_SETCUEBANNER, TRUE, (LPARAM)pState->szSearchPlaceholder);
    }
    break;

    case WM_COMMAND:
    {
        WORD code = HIWORD(wParam);
        HWND hMainWnd = GetParent(hWnd);

        if (pState && code == EN_CHANGE && (HWND)lParam == pState->hEditSearch)
        {
            if (!pState->bInternalChange)
            {
                SendMessage(hMainWnd, WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)pState->hEditSearch);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        if (!pState->bTrackingMouse)
        {
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            pState->bTrackingMouse = TRUE;
        }

        NavIconIndex hit = HitTestIcon(pState, x, y, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);
        if (hit != pState->hoverIndex)
        {
            pState->hoverIndex = hit;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            pState->bTrackingMouse = FALSE;
            if (pState->hoverIndex != ICON_NONE || pState->pressedIndex != ICON_NONE)
            {
                pState->hoverIndex = ICON_NONE;
                pState->pressedIndex = ICON_NONE;
                pState->hoverBreadcrumb = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        NavIconIndex hit = HitTestIcon(pState, x, y, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);
        if (hit != ICON_NONE)
        {
            pState->pressedIndex = hit;
            SetCapture(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        if (!pState) break;
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }

        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        NavIconIndex hit = HitTestIcon(pState, x, y, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);
        NavIconIndex clicked = (NavIconIndex)pState->pressedIndex;
        pState->pressedIndex = ICON_NONE;

        InvalidateRect(hWnd, NULL, FALSE);

        if (clicked != ICON_NONE && (clicked == hit || clicked == ICON_ADDR_PILL || clicked == ICON_ADDR_CLEAR || clicked == ICON_SEARCH_CLEAR || clicked >= ICON_BC_BASE))
        {
            HWND hMainWnd = GetParent(hWnd);
            switch (clicked)
            {
            case ICON_BACK:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_BACK, 0);
                break;
            case ICON_FORWARD:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_FORWARD, 0);
                break;
            case ICON_UP:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_UP, 0);
                break;
            case ICON_REFRESH:
                SendMessage(hMainWnd, WM_NAVTOOLBAR_ACTION, NAV_ACTION_REFRESH, 0);
                break;
            case ICON_ADDR_CLEAR:
                if (pState->hEditAddr)
                {
                    SetWindowTextW(pState->hEditAddr, L"");
                    SetFocus(pState->hEditAddr);
                }
                break;
            case ICON_SEARCH_CLEAR:
                if (pState->hEditSearch)
                {
                    SetWindowTextW(pState->hEditSearch, L"");
                    SetFocus(pState->hEditSearch);
                    SendMessage(hMainWnd, WM_NAVTOOLBAR_SEARCH, 0, (LPARAM)pState->hEditSearch);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                break;
            case ICON_ADDR_PILL:
            {
                if (!pState->bEditingAddress)
                {
                    pState->bEditingAddress = TRUE;
                    SetWindowTextW(pState->hEditAddr, pState->szCurrentPath);
                    ShowWindow(pState->hEditAddr, SW_SHOW);
                    SetFocus(pState->hEditAddr);
                    SendMessage(pState->hEditAddr, EM_SETSEL, 0, -1);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;
            default:
            {
                if (clicked >= ICON_BC_BASE)
                {
                    int bcIdx = clicked - ICON_BC_BASE;
                    if (bcIdx >= 0 && bcIdx < (int)pState->breadcrumbElements.size())
                    {
                        const BreadcrumbElement& elem = pState->breadcrumbElements[bcIdx];
                        if (elem.type == BC_ROOT_PC || elem.type == BC_CHEVRON_ROOT)
                        {
                            ShowDriveDropdown(hWnd, elem.rc);
                        }
                        else if (elem.type == BC_ELLIPSIS)
                        {
                            ShowEllipsisDropdown(hWnd, pState, elem.rc);
                        }
                        else if (elem.type == BC_CHEVRON_ELLIP || elem.type == BC_CHEVRON)
                        {
                            ShowFolderDropdown(hWnd, elem.fullPath, elem.rc);
                        }
                        else if (elem.type == BC_SEGMENT)
                        {
                            if (elem.segmentIndex == (int)pState->breadcrumbElements.size() - 1 || elem.fullPath == pState->szCurrentPath)
                            {
                                ShowFolderDropdown(hWnd, elem.fullPath, elem.rc);
                            }
                            else
                            {
                                NavToolbar_SetPath(hWnd, elem.fullPath.c_str());
                                SendMessage(hMainWnd, WM_NAVTOOLBAR_NAVIGATE, 0, (LPARAM)hWnd);
                            }
                        }
                    }
                }
            }
            break;
            }
        }
    }
    break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;
        if (!pState) break;

        COLORREF bgCol = (pState->bEditingAddress && (hEdit == pState->hEditAddr)) ?
            RGB(42, 18, 22) : RGB(62, 26, 31);

        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, bgCol);
        SetTextColor(hdc, RGB(245, 245, 245));
        return (LRESULT)(hEdit == pState->hEditAddr && pState->bEditingAddress ? pState->hbrPillEditDark : pState->hbrPillDark);
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (!pState) { EndPaint(hWnd, &ps); return 0; }

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Double buffer DC
        HDC hmemDC = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP holdBm = (HBITMAP)SelectObject(hmemDC, hbm);

        // 1. Fill container background (Light Mica tone matching active tab surface RGB 52, 22, 27)
        COLORREF bgToolbar = RGB(52, 22, 27);
        HBRUSH hbrBg = CreateSolidBrush(bgToolbar);
        FillRect(hmemDC, &rcClient, hbrBg);
        DeleteObject(hbrBg);

        COLORREF pillBg = RGB(62, 26, 31);
        COLORREF pillBorder = RGB(78, 32, 38);
        COLORREF accentBlue = RGB(0, 120, 215);

        // Calculate Pill layout coordinates
        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(rcClient.right, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        // 2. Draw Pill Shape for Address Bar
        COLORREF curAddrPillBg = pState->bEditingAddress ? RGB(42, 18, 22) : pillBg;
        COLORREF curAddrBorder = pState->bEditingAddress ? accentBlue : pillBorder;

        HBRUSH hbrAddrPill = CreateSolidBrush(curAddrPillBg);
        HPEN hPenAddrBorder = CreatePen(PS_SOLID, 1, curAddrBorder);

        HBRUSH hOldBr = (HBRUSH)SelectObject(hmemDC, hbrAddrPill);
        HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenAddrBorder);

        RoundRect(hmemDC, addrPillLeft, 5, addrPillRight, 39, 8, 8);

        SelectObject(hmemDC, hOldBr);
        SelectObject(hmemDC, hOldPen);
        DeleteObject(hbrAddrPill);
        DeleteObject(hPenAddrBorder);

        // Active Editing Underline Highlight Line for Address Bar
        if (pState->bEditingAddress)
        {
            HPEN hPenBlue = CreatePen(PS_SOLID, 2, accentBlue);
            HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hPenBlue);

            MoveToEx(hmemDC, addrPillLeft + 8, 38, NULL);
            LineTo(hmemDC, addrPillRight - 8, 38);

            SelectObject(hmemDC, hPrevPen);
            DeleteObject(hPenBlue);

            // Draw Clear 'X' (\xE711) button on right of Address Pill during editing
            RECT rcClearBtn = { addrPillRight - 26, 5, addrPillRight - 6, 39 };
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, pState->hoverIndex == ICON_ADDR_CLEAR ? RGB(255, 255, 255) : RGB(200, 200, 200));
            SelectObject(hmemDC, pState->hFontIcons ? pState->hFontIcons : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(hmemDC, L"\xE711", -1, &rcClearBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // 3. Draw Pill Shape for Search Bar
        COLORREF curSearchBorder = pState->bSearchFocused ? accentBlue : pillBorder;
        HBRUSH hbrSearchPill = CreateSolidBrush(pillBg);
        HPEN hPenSearchBorder = CreatePen(PS_SOLID, 1, curSearchBorder);

        hOldBr = (HBRUSH)SelectObject(hmemDC, hbrSearchPill);
        hOldPen = (HPEN)SelectObject(hmemDC, hPenSearchBorder);

        RoundRect(hmemDC, searchPillLeft, 5, searchPillRight, 39, 8, 8);

        SelectObject(hmemDC, hOldBr);
        SelectObject(hmemDC, hOldPen);
        DeleteObject(hbrSearchPill);
        DeleteObject(hPenSearchBorder);

        if (pState->bSearchFocused)
        {
            HPEN hPenBlue = CreatePen(PS_SOLID, 2, accentBlue);
            HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hPenBlue);

            MoveToEx(hmemDC, searchPillLeft + 8, 38, NULL);
            LineTo(hmemDC, searchPillRight - 8, 38);

            SelectObject(hmemDC, hPrevPen);
            DeleteObject(hPenBlue);
        }

        // Draw Search Icon or Clear 'X' button inside Search Pill
        bool hasSearchText = (pState->hEditSearch && GetWindowTextLengthW(pState->hEditSearch) > 0);
        if (hasSearchText)
        {
            RECT rcSearchClear = { searchPillRight - 28, 5, searchPillRight - 6, 39 };
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, pState->hoverIndex == ICON_SEARCH_CLEAR ? RGB(255, 255, 255) : RGB(200, 200, 200));
            SelectObject(hmemDC, pState->hFontIcons ? pState->hFontIcons : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(hmemDC, L"\xE711", -1, &rcSearchClear, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            RECT rcSearchIcon = { searchPillRight - 28, 5, searchPillRight - 8, 39 };
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, RGB(220, 220, 220));
            SelectObject(hmemDC, pState->hFontIcons ? pState->hFontIcons : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(hmemDC, L"\xE721", -1, &rcSearchIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // 4. Render Breadcrumbs inside Address Pill when NOT editing
        if (!pState->bEditingAddress)
        {
            if (wcslen(pState->szCurrentPath) == 0)
            {
                // Placeholder on first launch / empty path
                RECT rcPlaceholder = { addrPillLeft + 14, 5, addrPillRight - 14, 39 };
                SetBkMode(hmemDC, TRANSPARENT);
                SetTextColor(hmemDC, RGB(180, 150, 160));
                SelectObject(hmemDC, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hmemDC, pState->szAddressPlaceholder, -1, &rcPlaceholder, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else
            {
                RebuildBreadcrumbs(pState, hmemDC, addrPillLeft, addrPillRight);

                HRGN hClip = CreateRectRgn(addrPillLeft + 1, 5, addrPillRight - 1, 39);
                SelectClipRgn(hmemDC, hClip);

                HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                HPEN hPrevPen = (HPEN)SelectObject(hmemDC, hNullPen);

                COLORREF hoverBg = RGB(78, 32, 38);
                COLORREF pressedBg = RGB(90, 38, 45);

                // 1. Draw unified hover / pressed capsules for breadcrumb pairs
                int activeHoverIdx = (pState->hoverIndex >= ICON_BC_BASE) ? (pState->hoverIndex - ICON_BC_BASE) : -1;
                int activePressIdx = (pState->pressedIndex >= ICON_BC_BASE) ? (pState->pressedIndex - ICON_BC_BASE) : -1;

                for (size_t i = 0; i < pState->breadcrumbElements.size(); ++i)
                {
                    const BreadcrumbElement& elem = pState->breadcrumbElements[i];
                    if (elem.type == BC_ROOT_PC || elem.type == BC_ELLIPSIS || elem.type == BC_SEGMENT)
                    {
                        RECT rcCapsule = elem.rc;
                        bool bPairHover = ((int)i == activeHoverIdx);
                        bool bPairPress = ((int)i == activePressIdx);
                        bool bHasChevron = false;
                        bool bChevHover = false;
                        bool bChevPress = false;
                        int sepX = elem.rc.right;

                        // Include adjacent chevron in the unified capsule
                        if (i + 1 < pState->breadcrumbElements.size())
                        {
                            const BreadcrumbElement& nextElem = pState->breadcrumbElements[i + 1];
                            if (nextElem.type == BC_CHEVRON_ROOT || nextElem.type == BC_CHEVRON_ELLIP || nextElem.type == BC_CHEVRON)
                            {
                                rcCapsule.right = nextElem.rc.right;
                                bHasChevron = true;
                                if ((int)(i + 1) == activeHoverIdx) { bPairHover = true; bChevHover = true; }
                                if ((int)(i + 1) == activePressIdx) { bPairPress = true; bChevPress = true; }
                            }
                        }

                        if (bPairHover || bPairPress)
                        {
                            // Outer unified capsule
                            HBRUSH hbrHover = CreateSolidBrush(bPairPress ? pressedBg : hoverBg);
                            HBRUSH hOldBrHover = (HBRUSH)SelectObject(hmemDC, hbrHover);
                            RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                            SelectObject(hmemDC, hOldBrHover);
                            DeleteObject(hbrHover);

                            if (bHasChevron)
                            {
                                // 1. Subtle vertical separator line between text and chevron
                                HPEN hPenSep = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
                                HPEN hOldP = (HPEN)SelectObject(hmemDC, hPenSep);
                                MoveToEx(hmemDC, sepX, elem.rc.top + 5, NULL);
                                LineTo(hmemDC, sepX, elem.rc.bottom - 5);
                                SelectObject(hmemDC, hOldP);
                                DeleteObject(hPenSep);

                                // 2. Active button highlight cleanly overlaps the separator
                                COLORREF subHighlight = RGB(88, 36, 42);
                                if (bChevHover || bChevPress)
                                {
                                    HRGN hChevRgn = CreateRectRgn(sepX, elem.rc.top, rcCapsule.right, rcCapsule.bottom);
                                    HRGN hPrevClip = CreateRectRgn(0, 0, 0, 0);
                                    int hasClip = GetClipRgn(hmemDC, hPrevClip);
                                    ExtSelectClipRgn(hmemDC, hChevRgn, RGN_AND);

                                    HBRUSH hbrSub = CreateSolidBrush(subHighlight);
                                    HBRUSH hOldSub = (HBRUSH)SelectObject(hmemDC, hbrSub);
                                    RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                                    SelectObject(hmemDC, hOldSub);
                                    DeleteObject(hbrSub);

                                    SelectClipRgn(hmemDC, hasClip == 1 ? hPrevClip : NULL);
                                    DeleteObject(hPrevClip);
                                    DeleteObject(hChevRgn);
                                }
                                else if ((int)i == activeHoverIdx || (int)i == activePressIdx)
                                {
                                    HRGN hTextRgn = CreateRectRgn(rcCapsule.left, elem.rc.top, sepX + 1, rcCapsule.bottom);
                                    HRGN hPrevClip = CreateRectRgn(0, 0, 0, 0);
                                    int hasClip = GetClipRgn(hmemDC, hPrevClip);
                                    ExtSelectClipRgn(hmemDC, hTextRgn, RGN_AND);

                                    HBRUSH hbrSub = CreateSolidBrush(subHighlight);
                                    HBRUSH hOldSub = (HBRUSH)SelectObject(hmemDC, hbrSub);
                                    RoundRect(hmemDC, rcCapsule.left, rcCapsule.top, rcCapsule.right, rcCapsule.bottom, 6, 6);
                                    SelectObject(hmemDC, hOldSub);
                                    DeleteObject(hbrSub);

                                    SelectClipRgn(hmemDC, hasClip == 1 ? hPrevClip : NULL);
                                    DeleteObject(hPrevClip);
                                    DeleteObject(hTextRgn);
                                }
                            }
                        }
                    }
                }

                // 2. Draw glyphs and labels
                for (size_t i = 0; i < pState->breadcrumbElements.size(); ++i)
                {
                    const BreadcrumbElement& elem = pState->breadcrumbElements[i];
                    SetBkMode(hmemDC, TRANSPARENT);

                    if (elem.type == BC_ROOT_PC)
                    {
                        // Draw "This PC" Desktop Computer Monitor Icon (\xE7F4)
                        SetTextColor(hmemDC, RGB(245, 245, 245));
                        SelectObject(hmemDC, pState->hFontIcons ? pState->hFontIcons : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        DrawTextW(hmemDC, L"\xE7F4", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    else if (elem.type == BC_CHEVRON_ROOT || elem.type == BC_CHEVRON_ELLIP || elem.type == BC_CHEVRON)
                    {
                        // Draw Chevron (\xE76C)
                        bool bChevActive = ((int)i == activeHoverIdx || (int)i == activePressIdx ||
                            (i > 0 && ((int)(i - 1) == activeHoverIdx || (int)(i - 1) == activePressIdx)));
                        SetTextColor(hmemDC, bChevActive ? RGB(255, 255, 255) : RGB(190, 170, 175));
                        SelectObject(hmemDC, pState->hFontChevron ? pState->hFontChevron : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        DrawTextW(hmemDC, L"\xE76C", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    else if (elem.type == BC_ELLIPSIS)
                    {
                        // Draw "..." Ellipsis Button
                        SetTextColor(hmemDC, RGB(245, 245, 245));
                        SelectObject(hmemDC, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        DrawTextW(hmemDC, L"...", -1, (LPRECT)&elem.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    else if (elem.type == BC_SEGMENT)
                    {
                        // Draw Folder / Drive segment with 8px left/right padding
                        SetTextColor(hmemDC, RGB(255, 255, 255));
                        SelectObject(hmemDC, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
                        RECT rcText = elem.rc;
                        rcText.left += 8;
                        rcText.right -= 8;
                        DrawTextW(hmemDC, elem.label.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    }
                }

                SelectObject(hmemDC, hPrevPen);
                DeleteObject(hNullPen);

                SelectClipRgn(hmemDC, NULL);
                DeleteObject(hClip);
            }
        }

        // Bottom border line
        HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(38, 14, 18));
        HPEN hOldP = (HPEN)SelectObject(hmemDC, hPenLine);
        MoveToEx(hmemDC, 0, rcClient.bottom - 1, NULL);
        LineTo(hmemDC, rcClient.right, rcClient.bottom - 1);
        SelectObject(hmemDC, hOldP);
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
        if (!pState) break;
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int addrPillLeft, addrPillRight, searchPillLeft, searchPillRight;
        CalculatePillLayout(width, addrPillLeft, addrPillRight, searchPillLeft, searchPillRight);

        int addrEditLeft = addrPillLeft + 10;
        int addrEditWidth = addrPillRight - 34 - addrEditLeft;
        if (addrEditWidth < 40) addrEditWidth = 40;

        if (pState->hEditAddr)
        {
            SetWindowPos(pState->hEditAddr, NULL, addrEditLeft, 11, addrEditWidth, 22, SWP_NOZORDER);
        }

        if (pState->hEditSearch)
        {
            int searchEditW = (searchPillRight - searchPillLeft) - 40;
            if (searchEditW < 40) searchEditW = 40;
            SetWindowPos(pState->hEditSearch, NULL, searchPillLeft + 12, 11, searchEditW, 22, SWP_NOZORDER);
        }

        InvalidateRect(hWnd, NULL, TRUE);
    }
    break;

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
            if (pState->hFontChevron) DeleteObject(pState->hFontChevron);
            if (pState->hbrPillDark) DeleteObject(pState->hbrPillDark);
            if (pState->hbrPillEditDark) DeleteObject(pState->hbrPillEditDark);
            if (pState->hbrPillLight) DeleteObject(pState->hbrPillLight);

            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        break;
    }

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL RegisterNavToolbarClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = NavToolbarProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"NavToolbarClass";

    return RegisterClassExW(&wcex) != 0;
}

HWND CreateNavToolbar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId)
{
    RegisterNavToolbarClass(hInstance);

    HWND hToolbar = CreateWindowExW(
        0, L"NavToolbarClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, (HMENU)controlId, hInstance, NULL
    );

    return hToolbar;
}

void NavToolbar_SetPath(HWND hNavToolbar, const wchar_t* szPath)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState)
    {
        wcscpy_s(pState->szCurrentPath, szPath ? szPath : L"");
        if (pState->hEditAddr) SetWindowTextW(pState->hEditAddr, szPath ? szPath : L"");
        InvalidateRect(hNavToolbar, NULL, TRUE);
    }
}

void NavToolbar_GetPath(HWND hNavToolbar, wchar_t* szBuffer, int maxLen)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState && szBuffer && maxLen > 0)
    {
        wcscpy_s(szBuffer, maxLen, pState->szCurrentPath);
    }
}

void NavToolbar_GetSearchQuery(HWND hNavToolbar, wchar_t* szBuffer, int maxLen)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState && pState->hEditSearch && szBuffer && maxLen > 0)
    {
        GetWindowTextW(pState->hEditSearch, szBuffer, maxLen);
    }
}

void NavToolbar_SetSearchQuery(HWND hNavToolbar, const wchar_t* szQuery)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState && pState->hEditSearch)
    {
        pState->bInternalChange = TRUE;
        SetWindowTextW(pState->hEditSearch, szQuery ? szQuery : L"");
        pState->bInternalChange = FALSE;
        InvalidateRect(hNavToolbar, NULL, FALSE);
    }
}

void NavToolbar_SetDarkMode(HWND hNavToolbar, BOOL bDarkMode)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState)
    {
        pState->bDarkMode = bDarkMode;
        InvalidateRect(hNavToolbar, NULL, TRUE);
    }
}

void NavToolbar_SetSearchPlaceholder(HWND hNavToolbar, const wchar_t* szPlaceholder)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState && szPlaceholder)
    {
        wcsncpy_s(pState->szSearchPlaceholder, szPlaceholder, _TRUNCATE);
        if (pState->hEditSearch)
        {
            SendMessageW(pState->hEditSearch, EM_SETCUEBANNER, TRUE, (LPARAM)szPlaceholder);
        }
        InvalidateRect(hNavToolbar, NULL, TRUE);
    }
}

void NavToolbar_SetAddressPlaceholder(HWND hNavToolbar, const wchar_t* szPlaceholder)
{
    NavToolbarState* pState = (NavToolbarState*)GetWindowLongPtrW(hNavToolbar, GWLP_USERDATA);
    if (pState && szPlaceholder)
    {
        wcsncpy_s(pState->szAddressPlaceholder, szPlaceholder, _TRUNCATE);
        if (pState->hEditAddr)
        {
            SendMessageW(pState->hEditAddr, EM_SETCUEBANNER, TRUE, (LPARAM)szPlaceholder);
        }
        InvalidateRect(hNavToolbar, NULL, TRUE);
    }
}
