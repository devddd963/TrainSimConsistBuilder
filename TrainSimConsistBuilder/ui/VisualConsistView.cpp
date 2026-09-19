#include "ModernMessageBox.h"
#include "ModernContextMenu.h"
#include "CustomScrollBar.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "VisualConsistView.h"
#include "../src/Resource.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

extern HFONT GetAdaptiveSystemFont();

struct VisualUnitItem {
    std::wstring uid;
    std::wstring parentDir;
    bool isEngine;
    bool isFlipped;
    bool isBroken;
    RECT rc; // Computed bounding rect in track coords
};


static bool ReadCollapseOnStartupRegistry()
{
    HKEY hKey;
    DWORD dwVal = 0;
    DWORD dwSize = sizeof(dwVal);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"VisualPreviewCollapsedOnStartup", NULL, NULL, (LPBYTE)&dwVal, &dwSize);
        RegCloseKey(hKey);
    }
    return (dwVal != 0);
}

static void WriteCollapseOnStartupRegistry(bool bCollapsed)
{
    HKEY hKey;
    DWORD dwVal = bCollapsed ? 1 : 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"VisualPreviewCollapsedOnStartup", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));
        RegCloseKey(hKey);
    }
}

struct VisualConsistState {
    HWND hWnd;
    HWND hParent;
    CustomScrollBar m_hScroll;
    HINSTANCE hInst;
    int controlId;
    BOOL bDarkMode;
    bool isFloating;
    bool isCollapsed;
    int scrollX;
    int maxScrollX;
    int selectedIndex;
    int hoverIndex;
    int pressedIndex;
    bool isDragging;
    int dragStartX;
    int dragStartScrollX;

    // Header & Footer buttons
    RECT rcHeader;
    RECT rcBtnCollapse;
    RECT rcBtnFloat;
    RECT rcBtnLivePreview;
    RECT rcBtnSettings;
    RECT rcBtnClose;
    RECT rcFooter;
    RECT rcBtnFooterClose;
    RECT rcChkStartupCollapse;
    bool hoverCollapse;
    bool hoverFloat;
    bool hoverLivePreview;
    bool hoverSettings;
    bool hoverClose;
    bool hoverFooterClose;
    bool hoverChkStartup;

    std::wstring basePath;
    std::vector<VisualUnitItem> units;

    HFONT hFontTitle;
    HFONT hFontCarName;
    HFONT hFontCarNum;
    HFONT hFontIcons;

    // GDI+ Images
    Gdiplus::Image* pImgFront;
    Gdiplus::Image* pImgMiddle;
    Gdiplus::Image* pImgPC;
    Gdiplus::Image* pImgRear;
    Gdiplus::Image* pImgMiddleFlipped;
    Gdiplus::Image* pImgPCFlipped;
};

static const int HEADER_HEIGHT = 28;
static const int H_SCROLLBAR_HEIGHT = 14;
static const int EXPANDED_HEIGHT = 152;
static const int CAR_WIDTH_FRONT = 326;
static const int CAR_WIDTH_MID   = 267;
static const int CAR_HEIGHT      = 90;
static const int CAR_GAP         = 0;   // 0px gap: flush seamless gangway coupling
static const int TRACK_MARGIN_X  = 24;  // Symmetric margin on left and right borders

static int GetTrainTotalWidth(const std::vector<VisualUnitItem>& units)
{
    if (units.empty()) return 0;
    int total = 0;
    for (size_t i = 0; i < units.size(); ++i)
    {
        int unitW = (i == 0 || i == units.size() - 1) ? CAR_WIDTH_FRONT : CAR_WIDTH_MID;
        total += unitW;
        if (i < units.size() - 1)
        {
            total += CAR_GAP;
        }
    }
    return total;
}

// GDI+ Globals
static ULONG_PTR s_gdiplusToken = 0;
static int s_gdiplusRefCount = 0;

static void InitGdiPlus()
{
    if (s_gdiplusRefCount == 0)
    {
        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&s_gdiplusToken, &gsi, NULL);
    }
    s_gdiplusRefCount++;
}

static void ShutdownGdiPlus()
{
    s_gdiplusRefCount--;
    if (s_gdiplusRefCount <= 0)
    {
        s_gdiplusRefCount = 0;
        if (s_gdiplusToken)
        {
            Gdiplus::GdiplusShutdown(s_gdiplusToken);
            s_gdiplusToken = 0;
        }
    }
}

static Gdiplus::Image* LoadImageFromResource(HINSTANCE hInstance, int resId)
{
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return NULL;

    DWORD resSize = SizeofResource(hInstance, hRes);
    HGLOBAL hResData = LoadResource(hInstance, hRes);
    if (!hResData) return NULL;

    void* pData = LockResource(hResData);
    if (!pData || resSize == 0) return NULL;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, resSize);
    if (!hMem) return NULL;

    void* pMem = GlobalLock(hMem);
    if (pMem)
    {
        memcpy(pMem, pData, resSize);
        GlobalUnlock(hMem);
    }

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK)
    {
        Gdiplus::Image* pImage = Gdiplus::Image::FromStream(pStream);
        pStream->Release();
        if (pImage && pImage->GetLastStatus() == Gdiplus::Ok)
        {
            return pImage;
        }
        if (pImage) delete pImage;
    }
    GlobalFree(hMem);
    return NULL;
}

static std::wstring GetExecutableDir()
{
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    wchar_t* pLastSlash = wcsrchr(szPath, L'\\');
    if (pLastSlash) *pLastSlash = L'\0';
    return std::wstring(szPath);
}

static void LoadSprites(VisualConsistState* pState)
{
    // 1. Try loading directly from embedded Win32 resources
    if (pState->hInst)
    {
        pState->pImgFront = LoadImageFromResource(pState->hInst, IDR_PNG_FRONT);
        pState->pImgMiddle = LoadImageFromResource(pState->hInst, IDR_PNG_MIDDLE);
        pState->pImgPC = LoadImageFromResource(pState->hInst, IDR_PNG_PC);
    }

    // 2. Fallback to file system if needed
    if (!pState->pImgFront)
    {
        std::wstring exeDir = GetExecutableDir();
        std::vector<std::wstring> frontCandidates = {
            exeDir + L"\\Resources\\FRONT.png",
            exeDir + L"\\FRONT.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\TrainSimConsistBuilder\\Resources\\FRONT.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\Resources\\FRONT.png"
        };
        for (const auto& path : frontCandidates)
        {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                pState->pImgFront = Gdiplus::Image::FromFile(path.c_str());
                if (pState->pImgFront && pState->pImgFront->GetLastStatus() == Gdiplus::Ok) break;
            }
        }
    }

    if (!pState->pImgMiddle)
    {
        std::wstring exeDir = GetExecutableDir();
        std::vector<std::wstring> midCandidates = {
            exeDir + L"\\Resources\\MIDDLE.png",
            exeDir + L"\\MIDDLE.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\TrainSimConsistBuilder\\Resources\\MIDDLE.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\Resources\\MIDDLE.png"
        };
        for (const auto& path : midCandidates)
        {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                pState->pImgMiddle = Gdiplus::Image::FromFile(path.c_str());
                if (pState->pImgMiddle && pState->pImgMiddle->GetLastStatus() == Gdiplus::Ok) break;
            }
        }
    }

    if (!pState->pImgPC)
    {
        std::wstring exeDir = GetExecutableDir();
        std::vector<std::wstring> pcCandidates = {
            exeDir + L"\\Resources\\PC.png",
            exeDir + L"\\PC.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\TrainSimConsistBuilder\\Resources\\PC.png",
            L"d:\\JISOO_FLOWER_903\\TrainSimConsistBuilder\\Resources\\PC.png"
        };
        for (const auto& path : pcCandidates)
        {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                pState->pImgPC = Gdiplus::Image::FromFile(path.c_str());
                if (pState->pImgPC && pState->pImgPC->GetLastStatus() == Gdiplus::Ok) break;
            }
        }
    }

    // 3. Create horizontally mirrored sprites
    if (pState->pImgFront && pState->pImgFront->GetLastStatus() == Gdiplus::Ok)
    {
        pState->pImgRear = pState->pImgFront->Clone();
        if (pState->pImgRear)
        {
            pState->pImgRear->RotateFlip(Gdiplus::RotateNoneFlipX);
        }
    }

    if (pState->pImgMiddle && pState->pImgMiddle->GetLastStatus() == Gdiplus::Ok)
    {
        pState->pImgMiddleFlipped = pState->pImgMiddle->Clone();
        if (pState->pImgMiddleFlipped)
        {
            pState->pImgMiddleFlipped->RotateFlip(Gdiplus::RotateNoneFlipX);
        }
    }

    if (pState->pImgPC && pState->pImgPC->GetLastStatus() == Gdiplus::Ok)
    {
        pState->pImgPCFlipped = pState->pImgPC->Clone();
        if (pState->pImgPCFlipped)
        {
            pState->pImgPCFlipped->RotateFlip(Gdiplus::RotateNoneFlipX);
        }
    }
}

static void UpdateScrollbars(VisualConsistState* pState, int clientW)
{
    if (!pState) return;

    if (pState->isCollapsed && !pState->isFloating)
    {
        pState->m_hScroll.SetVisible(false);
        return;
    }

    int trainW = GetTrainTotalWidth(pState->units);
    int totalTrackWidth = trainW + 2 * TRACK_MARGIN_X;

    bool needH = (totalTrackWidth > clientW);
    pState->m_hScroll.SetVisible(needH);

    if (needH)
    {
        pState->m_hScroll.SetRange(0, totalTrackWidth - 1, clientW);
        pState->m_hScroll.SetPos(pState->scrollX);
        pState->scrollX = pState->m_hScroll.GetPos();
    }
}

static void LayoutScrollbars(VisualConsistState* pState, int clientW, int clientH)
{
    if (!pState) return;

    if (pState->isCollapsed && !pState->isFloating)
    {
        pState->m_hScroll.SetVisible(false);
        return;
    }

    int trainW = GetTrainTotalWidth(pState->units);
    int totalTrackWidth = trainW + 2 * TRACK_MARGIN_X;

    bool needH = (totalTrackWidth > clientW);
    pState->m_hScroll.SetVisible(needH);

    RECT rcH = { 0, clientH - H_SCROLLBAR_HEIGHT, clientW, clientH };
    pState->m_hScroll.SetBounds(rcH);

    UpdateScrollbars(pState, clientW);
}

static void RecalcLayout(VisualConsistState* pState, int clientW, int clientH)
{
    pState->rcHeader = { 0, 0, clientW, HEADER_HEIGHT };

    int btnW = 28;
    int btnH = HEADER_HEIGHT - 4;
    int btnY = 2;
    int curRight = clientW - 6;

    if (pState->isFloating)
    {
        // Floating Mode: Only Settings Gear and 3D Shape Preview
        pState->rcBtnClose = { 0, 0, 0, 0 };
        pState->rcBtnFloat = { 0, 0, 0, 0 };
        pState->rcBtnCollapse = { 0, 0, 0, 0 };

        // 1. Settings Gear Button
        pState->rcBtnSettings = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 2. 3D Shape Preview Button
        pState->rcBtnLivePreview = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);
    }
    else
    {
        // Docked Mode: Collapse, Pop-out, Settings Gear, 3D Shape Preview
        pState->rcBtnClose = { 0, 0, 0, 0 };

        // 1. Collapse / Expand Chevron
        pState->rcBtnCollapse = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 2. Pop Out Button
        pState->rcBtnFloat = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 3. Settings Gear Button
        pState->rcBtnSettings = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 4. 3D Shape Preview Button
        pState->rcBtnLivePreview = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);
    }

    if (pState->isFloating)
    {
        int footerH = 44;
        pState->rcFooter = { 0, clientH - footerH, clientW, clientH };
        pState->rcBtnFooterClose = { clientW - 130, clientH - footerH + 7, clientW - 14, clientH - 9 };
        pState->rcChkStartupCollapse = { 16, clientH - footerH + 10, clientW - 150, clientH - 10 };
    }
    else
    {
        pState->rcFooter = { 0, 0, 0, 0 };
        pState->rcBtnFooterClose = { 0, 0, 0, 0 };
        pState->rcChkStartupCollapse = { 0, 0, 0, 0 };
    }

    int trainW = GetTrainTotalWidth(pState->units);
    int totalTrackWidth = trainW + 2 * TRACK_MARGIN_X;

    int startX = TRACK_MARGIN_X;
    if (totalTrackWidth <= clientW)
    {
        // When the trainset fits within viewport width: perfectly center horizontally
        startX = (clientW - trainW) / 2;
        if (startX < TRACK_MARGIN_X) startX = TRACK_MARGIN_X;
        pState->maxScrollX = 0;
        pState->scrollX = 0;
    }
    else
    {
        // When trainset exceeds viewport width: start at left margin and allow scrolling
        // with exactly symmetric TRACK_MARGIN_X spacing on both ends
        startX = TRACK_MARGIN_X;
        pState->maxScrollX = totalTrackWidth - clientW;
        if (pState->scrollX > pState->maxScrollX) pState->scrollX = pState->maxScrollX;
        if (pState->scrollX < 0) pState->scrollX = 0;
    }

    int curX = startX;
    int carY = HEADER_HEIGHT + 2;

    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        int unitW = (i == 0 || i == pState->units.size() - 1) ? CAR_WIDTH_FRONT : CAR_WIDTH_MID;
        pState->units[i].rc = { curX, carY, curX + unitW, carY + CAR_HEIGHT };
        curX += unitW + CAR_GAP;
    }

    LayoutScrollbars(pState, clientW, pState->isFloating ? (clientH - 44) : clientH);
}

static void EnsureVisible(VisualConsistState* pState, int index, int clientW)
{
    if (index < 0 || index >= (int)pState->units.size()) return;
    if (pState->maxScrollX <= 0)
    {
        pState->scrollX = 0;
        return;
    }

    int carLeft = pState->units[index].rc.left;
    int carRight = pState->units[index].rc.right;

    int viewLeft = pState->scrollX;
    int viewRight = pState->scrollX + clientW;

    if (carLeft < viewLeft + TRACK_MARGIN_X)
    {
        pState->scrollX = carLeft - TRACK_MARGIN_X;
    }
    else if (carRight > viewRight - TRACK_MARGIN_X)
    {
        pState->scrollX = carRight - clientW + TRACK_MARGIN_X;
    }

    if (pState->scrollX > pState->maxScrollX) pState->scrollX = pState->maxScrollX;
    if (pState->scrollX < 0) pState->scrollX = 0;

    UpdateScrollbars(pState, clientW);
}

// ---------------------------------------------------------------------------
// Render Huge Shinkansen Unit & Compact Dedicated Info Footer Below 3px Rail
// ---------------------------------------------------------------------------
static bool IsProceduralPantographCar(size_t index, size_t totalCount)
{
    if (totalCount <= 2) return false;
    if (index == 0 || index == totalCount - 1) return false;

    // Determine target number of pantograph power cars K
    int K = 1;
    if (totalCount <= 4)
    {
        K = 1;
    }
    else if (totalCount <= 9)
    {
        K = 2;
    }
    else
    {
        // 2 or 3 (or scaled) for formations > 9 units
        K = (std::max)(3, (int)(totalCount / 4));
    }

    for (int k = 1; k <= K; ++k)
    {
        int targetIdx = (int)std::round(1.0 + (double)(k - 0.5) * (double)(totalCount - 2) / (double)K);
        if (targetIdx < 1) targetIdx = 1;
        if (targetIdx > (int)totalCount - 2) targetIdx = (int)totalCount - 2;
        if ((int)index == targetIdx) return true;
    }
    return false;
}

static void DrawUnit(HDC hdc, Gdiplus::Graphics& g, VisualConsistState* pState, size_t index, size_t totalCount, RECT rc, int railY, bool isSelected, bool isHovered)
{
    const auto& item = pState->units[index];
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    bool isFrontNose = (index == 0);
    bool isRearNose  = (index == totalCount - 1) && (totalCount > 1);

    // 1. Draw High-Res Shinkansen Sprite (Fixed cohesive formation)
    Gdiplus::Image* pImg = nullptr;
    if (isFrontNose)
    {
        pImg = pState->pImgFront;
    }
    else if (isRearNose)
    {
        pImg = pState->pImgRear ? pState->pImgRear : pState->pImgFront;
    }
    else
    {
        bool isPowerCar = item.isEngine || IsProceduralPantographCar(index, totalCount);
        if (isPowerCar && pState->pImgPC)
        {
            // For aerodynamic symmetry in real EMUs, mirror pantograph direction on the rear half
            bool isRearHalf = (index > totalCount / 2);
            pImg = isRearHalf ? (pState->pImgPCFlipped ? pState->pImgPCFlipped : pState->pImgPC) : pState->pImgPC;
        }
        else
        {
            pImg = pState->pImgMiddle;
        }
    }

    if (pImg && pImg->GetLastStatus() == Gdiplus::Ok)
    {
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(pImg, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    }
    else
    {
        HBRUSH hbrF = CreateSolidBrush(RGB(40, 45, 55));
        FillRect(hdc, &rc, hbrF);
        DeleteObject(hbrF);
    }

    // 2. Selection / Hover / Broken Frame Indicator on Sprite
    if (isSelected || item.isBroken || isHovered)
    {
        COLORREF outlineCol = isSelected ? RGB(0, 140, 255) : (item.isBroken ? RGB(235, 50, 50) : RGB(100, 160, 220));
        HPEN hPenOut = CreatePen(PS_SOLID, isSelected ? 2 : 1, outlineCol);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN hOldO = (HPEN)SelectObject(hdc, hPenOut);
        HBRUSH hOldOB = (HBRUSH)SelectObject(hdc, hNullB);

        RoundRect(hdc, rc.left - 1, rc.top + 4, rc.right + 1, railY + 1, 6, 6);

        SelectObject(hdc, hOldOB);
        SelectObject(hdc, hOldO);
        DeleteObject(hPenOut);
    }

    // 3. Compact Info Footer Directly Below 3px Rail
    SetBkMode(hdc, TRANSPARENT);
    int footerTop = railY + 4;
    int badgeH = 14;
    int badgeW = item.isFlipped ? 52 : 42;
    int badgeX = rc.left + (w - badgeW) / 2;

    // A. Sequence & Orientation Badge (Compact pill)
    if (pState->hFontCarNum)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, pState->hFontCarNum);
        wchar_t szNum[32];
        swprintf_s(szNum, 32, L"#%d %s", (int)(index + 1), item.isFlipped ? L"🠔" : L"➔");

        RECT rcBadge = { badgeX, footerTop, badgeX + badgeW, footerTop + badgeH };

        if (item.isBroken)
        {
            HBRUSH hbrBadge = CreateSolidBrush(RGB(220, 40, 40));
            HBRUSH hOldPB = (HBRUSH)SelectObject(hdc, hbrBadge);
            HPEN hNullP = (HPEN)GetStockObject(NULL_PEN);
            HPEN hOldPP = (HPEN)SelectObject(hdc, hNullP);
            RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 4, 4);
            SelectObject(hdc, hOldPB);
            SelectObject(hdc, hOldPP);
            DeleteObject(hbrBadge);
            SetTextColor(hdc, RGB(255, 255, 255));
        }
        else if (item.isFlipped)
        {
            HBRUSH hbrPill = CreateSolidBrush(RGB(0, 120, 215));
            HBRUSH hOldPB = (HBRUSH)SelectObject(hdc, hbrPill);
            HPEN hNullP = (HPEN)GetStockObject(NULL_PEN);
            HPEN hOldPP = (HPEN)SelectObject(hdc, hNullP);
            RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 4, 4);
            SelectObject(hdc, hOldPB);
            SelectObject(hdc, hOldPP);
            DeleteObject(hbrPill);
            SetTextColor(hdc, RGB(255, 255, 255));
        }
        else
        {
            HBRUSH hbrPill = CreateSolidBrush(RGB(36, 40, 48));
            HBRUSH hOldPB = (HBRUSH)SelectObject(hdc, hbrPill);
            HPEN hPenP = CreatePen(PS_SOLID, 1, RGB(55, 60, 70));
            HPEN hOldPP = (HPEN)SelectObject(hdc, hPenP);
            RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 4, 4);
            SelectObject(hdc, hOldPB);
            SelectObject(hdc, hOldPP);
            DeleteObject(hbrPill);
            DeleteObject(hPenP);
            SetTextColor(hdc, RGB(205, 212, 222));
        }

        DrawTextW(hdc, szNum, -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hOldF);
    }

    // B. Compact Unit Filename (Centered below badge)
    if (pState->hFontCarName)
    {
        HFONT hOldF = (HFONT)SelectObject(hdc, pState->hFontCarName);
        SetTextColor(hdc, item.isBroken ? RGB(235, 60, 60) : (isSelected ? RGB(0, 140, 255) : RGB(235, 240, 250)));
        RECT rcName = { rc.left + 4, footerTop + badgeH + 2, rc.right - 4, footerTop + badgeH + 18 };
        DrawTextW(hdc, item.uid.c_str(), -1, &rcName, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, hOldF);
    }
}

static LRESULT CALLBACK VisualConsistWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_GETMINMAXINFO:
    {
        if (pState && pState->isFloating)
        {
            MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
            pMMI->ptMinTrackSize.x = 520;
            pMMI->ptMinTrackSize.y = 190;
            return 0;
        }
        break;
    }

    case WM_NCHITTEST:
    {
        if (pState && pState->isFloating)
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            RECT rcWin;
            GetWindowRect(hWnd, &rcWin);

            int borderSize = 6; // 6px resize border grab area

            bool onLeft = (pt.x >= rcWin.left && pt.x < rcWin.left + borderSize);
            bool onRight = (pt.x >= rcWin.right - borderSize && pt.x < rcWin.right);
            bool onTop = (pt.y >= rcWin.top && pt.y < rcWin.top + borderSize);
            bool onBottom = (pt.y >= rcWin.bottom - borderSize && pt.y < rcWin.bottom);

            if (onTop && onLeft) return HTTOPLEFT;
            if (onTop && onRight) return HTTOPRIGHT;
            if (onBottom && onLeft) return HTBOTTOMLEFT;
            if (onBottom && onRight) return HTBOTTOMRIGHT;
            if (onLeft) return HTLEFT;
            if (onRight) return HTRIGHT;
            if (onTop) return HTTOP;
            if (onBottom) return HTBOTTOM;

            // Check if clicking in header bar for moving (excluding buttons)
            POINT ptClient = pt;
            ScreenToClient(hWnd, &ptClient);
            if (ptClient.y >= 0 && ptClient.y < HEADER_HEIGHT)
            {
                if (!PtInRect(&pState->rcBtnSettings, ptClient) &&
                    !PtInRect(&pState->rcBtnLivePreview, ptClient))
                {
                    return HTCAPTION;
                }
            }
            return HTCLIENT;
        }
        break;
    }

    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (VisualConsistState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_CREATE:
    {
        if (pState)
        {
            LoadSprites(pState);
            pState->m_hScroll.SetOrientation(ScrollBarOrientation::Horizontal);
            pState->m_hScroll.SetGutterColor(RGB(20, 20, 22));
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            RecalcLayout(pState, w, h);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
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

        RecalcLayout(pState, w, h);

        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

        // 1. Overall Background
        COLORREF bgHeader = RGB(32, 32, 32);
        COLORREF bgTrack = RGB(20, 20, 22);
        COLORREF borderCol = RGB(55, 55, 55);

        HBRUSH hbrTrack = CreateSolidBrush(bgTrack);
        FillRect(hMemDC, &rcClient, hbrTrack);
        DeleteObject(hbrTrack);

        // 2. Draw Top Header Bar
        HBRUSH hbrHeader = CreateSolidBrush(bgHeader);
        FillRect(hMemDC, &pState->rcHeader, hbrHeader);
        DeleteObject(hbrHeader);

        // Header bottom divider line
        HPEN hPenDivider = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenDivider);
        MoveToEx(hMemDC, 0, HEADER_HEIGHT - 1, NULL);
        LineTo(hMemDC, w, HEADER_HEIGHT - 1);
        SelectObject(hMemDC, hOldP);
        DeleteObject(hPenDivider);

        SetBkMode(hMemDC, TRANSPARENT);

        // Header Title
        if (pState->hFontTitle)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontTitle);
            SetTextColor(hMemDC, RGB(240, 240, 240));
            RECT rcTitle = { 12, 0, w - 100, HEADER_HEIGHT };
            
            wchar_t szTitle[128];
            swprintf_s(szTitle, 128, L"Visual Consist Preview [ %d Units ]", (int)pState->units.size());
            DrawTextW(hMemDC, szTitle, -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hMemDC, hOldF);
        }

        // Header Buttons: Pop Out, Collapse, 3D Shape Preview, Settings
        if (pState->hFontIcons)
        {
            HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcons);

            // 1. Pop Out Button (docked mode only)
            if (!pState->isFloating && pState->rcBtnFloat.right > 0)
            {
                if (pState->hoverFloat)
                {
                    HBRUSH hbrHov = CreateSolidBrush(RGB(55, 55, 55));
                    FillRect(hMemDC, &pState->rcBtnFloat, hbrHov);
                    DeleteObject(hbrHov);
                }
                SetTextColor(hMemDC, RGB(220, 220, 220));
                DrawTextW(hMemDC, L"\xE8A9", -1, &pState->rcBtnFloat, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // 2. Collapse / Expand Chevron (docked mode only)
            if (!pState->isFloating && pState->rcBtnCollapse.right > 0)
            {
                if (pState->hoverCollapse)
                {
                    HBRUSH hbrHov = CreateSolidBrush(RGB(55, 55, 55));
                    FillRect(hMemDC, &pState->rcBtnCollapse, hbrHov);
                    DeleteObject(hbrHov);
                }
                int colCX = (pState->rcBtnCollapse.left + pState->rcBtnCollapse.right) / 2;
                int colCY = (pState->rcBtnCollapse.top + pState->rcBtnCollapse.bottom) / 2;
                COLORREF arrCol = pState->hoverCollapse ? RGB(255, 255, 255) : RGB(180, 180, 180);
                HBRUSH hBrCol = CreateSolidBrush(arrCol);
                HPEN hPenCol = CreatePen(PS_SOLID, 1, arrCol);
                HBRUSH hOldBCol = (HBRUSH)SelectObject(hMemDC, hBrCol);
                HPEN hOldPCol = (HPEN)SelectObject(hMemDC, hPenCol);
                POINT ptsCol[3];
                if (pState->isCollapsed)
                {
                    // Down arrow when collapsed (click to expand)
                    ptsCol[0] = { colCX - 4, colCY - 2 };
                    ptsCol[1] = { colCX + 4, colCY - 2 };
                    ptsCol[2] = { colCX,     colCY + 3 };
                }
                else
                {
                    // Up arrow when expanded (click to collapse)
                    ptsCol[0] = { colCX,     colCY - 3 };
                    ptsCol[1] = { colCX - 4, colCY + 2 };
                    ptsCol[2] = { colCX + 4, colCY + 2 };
                }
                Polygon(hMemDC, ptsCol, 3);
                SelectObject(hMemDC, hOldBCol);
                SelectObject(hMemDC, hOldPCol);
                DeleteObject(hBrCol);
                DeleteObject(hPenCol);
            }

            // 3. Options / Settings Gear Button (\xE713)
            if (pState->rcBtnSettings.right > 0)
            {
                if (pState->hoverSettings)
                {
                    HBRUSH hbrHov = CreateSolidBrush(RGB(55, 55, 55));
                    FillRect(hMemDC, &pState->rcBtnSettings, hbrHov);
                    DeleteObject(hbrHov);
                }
                SetTextColor(hMemDC, RGB(220, 220, 220));
                DrawTextW(hMemDC, L"\xE713", -1, &pState->rcBtnSettings, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // 4. 3D Shape Live Preview Button (\xF158 3D Isometric Cube)
            if (pState->rcBtnLivePreview.right > 0)
            {
                if (pState->hoverLivePreview)
                {
                    HBRUSH hbrHov = CreateSolidBrush(RGB(55, 55, 55));
                    FillRect(hMemDC, &pState->rcBtnLivePreview, hbrHov);
                    DeleteObject(hbrHov);
                }
                SetTextColor(hMemDC, RGB(100, 180, 255));
                DrawTextW(hMemDC, L"\xF158", -1, &pState->rcBtnLivePreview, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            SelectObject(hMemDC, hOldF);
        }

        // Footer Bar (in floating mode)
        if (pState->isFloating)
        {
            HBRUSH hbrFoot = CreateSolidBrush(RGB(28, 28, 28));
            FillRect(hMemDC, &pState->rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            // Divider line above footer
            HPEN hPenFDiv = CreatePen(PS_SOLID, 1, borderCol);
            HPEN hOldFD = (HPEN)SelectObject(hMemDC, hPenFDiv);
            MoveToEx(hMemDC, 0, pState->rcFooter.top, NULL);
            LineTo(hMemDC, w, pState->rcFooter.top);
            SelectObject(hMemDC, hOldFD);
            DeleteObject(hPenFDiv);

            // Startup Collapse Checkbox Toggle
            bool bStartCollapsed = ReadCollapseOnStartupRegistry();
            if (pState->hFontIcons)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontIcons);
                SetTextColor(hMemDC, bStartCollapsed ? RGB(0, 120, 215) : RGB(180, 180, 180));
                RECT rcBox = { pState->rcChkStartupCollapse.left, pState->rcChkStartupCollapse.top, pState->rcChkStartupCollapse.left + 20, pState->rcChkStartupCollapse.bottom };
                DrawTextW(hMemDC, bStartCollapsed ? L"\xE73E" : L"\xE739", -1, &rcBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hMemDC, hOldF);
            }
            if (pState->hFontCarName)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontCarName);
                SetTextColor(hMemDC, RGB(220, 220, 220));
                RECT rcTxt = { pState->rcChkStartupCollapse.left + 24, pState->rcChkStartupCollapse.top, pState->rcChkStartupCollapse.right, pState->rcChkStartupCollapse.bottom };
                DrawTextW(hMemDC, L"Collapse Visual Preview on app startup", -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hMemDC, hOldF);
            }

            // Dedicated Accent Blue "Close / Dock" Button
            COLORREF btnBlue = pState->hoverFooterClose ? RGB(30, 140, 235) : RGB(0, 120, 215);
            HBRUSH hbrBlue = CreateSolidBrush(btnBlue);
            HBRUSH hOldPB = (HBRUSH)SelectObject(hMemDC, hbrBlue);
            HPEN hPenPB = CreatePen(PS_SOLID, 1, btnBlue);
            HPEN hOldPP = (HPEN)SelectObject(hMemDC, hPenPB);
            RoundRect(hMemDC, pState->rcBtnFooterClose.left, pState->rcBtnFooterClose.top, pState->rcBtnFooterClose.right, pState->rcBtnFooterClose.bottom, 6, 6);
            SelectObject(hMemDC, hOldPB);
            SelectObject(hMemDC, hOldPP);
            DeleteObject(hbrBlue);
            DeleteObject(hPenPB);

            if (pState->hFontTitle)
            {
                HFONT hOldF = (HFONT)SelectObject(hMemDC, pState->hFontTitle);
                SetTextColor(hMemDC, RGB(255, 255, 255));
                DrawTextW(hMemDC, L"Close / Dock", -1, &pState->rcBtnFooterClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hMemDC, hOldF);
            }
        }

        // 3. If Expanded, Draw 3px Technical Rail Line and Huge High-Res Train Cars
        if (!pState->isCollapsed || pState->isFloating)
        {
            // Exact Wheel/Skirt Contact Y Coordinate:
            // carTop = HEADER_HEIGHT + 2 = 30px.
            // CAR_HEIGHT = 90px. Skirt baseline is at 595 / 724 fraction of canvas:
            // 30 + 90 * (595.0 / 724.0) = 30 + 74.0 = 104px.
            int carTop = HEADER_HEIGHT + 2;
            int railY = carTop + (int)(CAR_HEIGHT * 595.0 / 724.0);

            // Draw Clean 2px Continuous Steel Rail Line across client width
            COLORREF railColor = RGB(80, 86, 96);
            HBRUSH hbrRail = CreateSolidBrush(railColor);
            RECT rcRail = { 0, railY, w, railY + 2 };
            FillRect(hMemDC, &rcRail, hbrRail);
            DeleteObject(hbrRail);

            // Subtle 1px Top Steel Highlight
            HPEN hPenRailTop = CreatePen(PS_SOLID, 1, RGB(115, 122, 136));
            HPEN hOldRP = (HPEN)SelectObject(hMemDC, hPenRailTop);
            MoveToEx(hMemDC, 0, railY, NULL);
            LineTo(hMemDC, w, railY);
            SelectObject(hMemDC, hOldRP);
            DeleteObject(hPenRailTop);

            // Initialize GDI+ graphics for MemDC
            Gdiplus::Graphics g(hMemDC);

            // Draw High-Res Train Units
            for (size_t i = 0; i < pState->units.size(); ++i)
            {
                RECT rcScreen = pState->units[i].rc;
                rcScreen.left -= pState->scrollX;
                rcScreen.right -= pState->scrollX;

                // Skip off-screen units
                if (rcScreen.right < -40 || rcScreen.left > w + 40) continue;

                bool isSelected = ((int)i == pState->selectedIndex);
                bool isHovered = ((int)i == pState->hoverIndex);

                DrawUnit(hMemDC, g, pState, i, pState->units.size(), rcScreen, railY, isSelected, isHovered);
            }

            // 3b. Draw Custom Horizontal ScrollBar
            pState->m_hScroll.Paint(hMemDC, bgTrack);
        }

        // 4. Draw 1px outer frame border
        HPEN hPenOuter = CreatePen(PS_SOLID, 1, borderCol);
        HPEN hOldOP = (HPEN)SelectObject(hMemDC, hPenOuter);
        HBRUSH hNullB = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hOldOB = (HBRUSH)SelectObject(hMemDC, hNullB);

        Rectangle(hMemDC, 0, 0, w, h);

        SelectObject(hMemDC, hOldOB);
        SelectObject(hMemDC, hOldOP);
        DeleteObject(hPenOuter);

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
        POINT pt = { x, y };

        if (pState->m_hScroll.OnMouseMove(pt, hWnd))
        {
            pState->scrollX = pState->m_hScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
        }

        if (pState->isDragging)
        {
            int dx = x - pState->dragStartX;
            pState->scrollX = pState->dragStartScrollX - dx;
            if (pState->scrollX > pState->maxScrollX) pState->scrollX = pState->maxScrollX;
            if (pState->scrollX < 0) pState->scrollX = 0;

            RECT rc;
            GetClientRect(hWnd, &rc);
            UpdateScrollbars(pState, rc.right);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        bool prevHoverFloat = pState->hoverFloat;
        bool prevHoverColl = pState->hoverCollapse;
        bool prevHoverLive = pState->hoverLivePreview;
        bool prevHoverSet = pState->hoverSettings;
        bool prevHoverClose = pState->hoverClose;
        bool prevHoverFoot = pState->hoverFooterClose;
        bool prevHoverChk = pState->hoverChkStartup;

        pState->hoverFloat = PtInRect(&pState->rcBtnFloat, { x, y });
        pState->hoverCollapse = PtInRect(&pState->rcBtnCollapse, { x, y });
        pState->hoverLivePreview = PtInRect(&pState->rcBtnLivePreview, { x, y });
        pState->hoverSettings = PtInRect(&pState->rcBtnSettings, { x, y });
        pState->hoverClose = PtInRect(&pState->rcBtnClose, { x, y });
        pState->hoverFooterClose = PtInRect(&pState->rcBtnFooterClose, { x, y });
        pState->hoverChkStartup = PtInRect(&pState->rcChkStartupCollapse, { x, y });

        int newHover = -1;
        if (y >= HEADER_HEIGHT)
        {
            int trackX = x + pState->scrollX;
            for (size_t i = 0; i < pState->units.size(); ++i)
            {
                RECT rcFull = pState->units[i].rc;
                rcFull.bottom += 46; // include info footer
                if (PtInRect(&rcFull, { trackX, y }))
                {
                    newHover = (int)i;
                    break;
                }
            }
        }

        if (newHover != pState->hoverIndex || prevHoverFloat != pState->hoverFloat || prevHoverColl != pState->hoverCollapse || prevHoverLive != pState->hoverLivePreview || prevHoverSet != pState->hoverSettings || prevHoverClose != pState->hoverClose || prevHoverFoot != pState->hoverFooterClose || prevHoverChk != pState->hoverChkStartup)
        {
            pState->hoverIndex = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == CustomScrollBar::TIMER_ANIM_ID && pState)
        {
            if (pState->m_hScroll.OnTimer(hWnd))
            {
                pState->scrollX = pState->m_hScroll.GetPos();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (pState && pState->m_hScroll.OnMouseLeave(hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->m_hScroll.OnLButtonDown(pt, hWnd))
        {
            pState->scrollX = pState->m_hScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Header Buttons
        // 1. Header Action Buttons
        if (PtInRect(&pState->rcBtnClose, { x, y }) || PtInRect(&pState->rcBtnFooterClose, { x, y }))
        {
            VisualConsistView_SetFloating(hWnd, false);
            return 0;
        }
        if (PtInRect(&pState->rcBtnFloat, { x, y }))
        {
            VisualConsistView_SetFloating(hWnd, !pState->isFloating);
            return 0;
        }
        if (!pState->isFloating && PtInRect(&pState->rcBtnCollapse, { x, y }))
        {
            pState->isCollapsed = !pState->isCollapsed;
            SendMessage(pState->hParent, WM_VISUAL_DOCK_CHANGED, 0, 0);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&pState->rcBtnLivePreview, { x, y }))
        {
            ShowModernMessageBox(hWnd, L"Live 3D Shape Preview (.s / shape mesh 3D viewer) is under active development and will be available in an upcoming update!", L"3D Shape Live Preview", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (PtInRect(&pState->rcBtnSettings, { x, y }))
        {
            bool curCollapsedOnStart = ReadCollapseOnStartupRegistry();
            POINT ptMenu = { pState->rcBtnSettings.left, pState->rcBtnSettings.bottom };
            ClientToScreen(hWnd, &ptMenu);

            std::vector<ContextMenuItem> items = {
                ContextMenuItem::Action(101, curCollapsedOnStart ? L"\xE73E" : L"\xE739", L"Collapse Visual Preview on app startup", L"", true)
            };

            int cmd = ModernContextMenu::Show(hWnd, ptMenu.x, ptMenu.y, items, pState->bDarkMode);
            if (cmd == 101)
            {
                bool newVal = !curCollapsedOnStart;
                WriteCollapseOnStartupRegistry(newVal);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        if (pState->isFloating && PtInRect(&pState->rcChkStartupCollapse, { x, y }))
        {
            bool curVal = ReadCollapseOnStartupRegistry();
            WriteCollapseOnStartupRegistry(!curVal);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (pState->isFloating && y < HEADER_HEIGHT)
        {
            // Drag to move window from header
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        if (y >= HEADER_HEIGHT)
        {
            int trackX = x + pState->scrollX;
            int clicked = -1;
            for (size_t i = 0; i < pState->units.size(); ++i)
            {
                RECT rcFull = pState->units[i].rc;
                rcFull.bottom += 46;
                if (PtInRect(&rcFull, { trackX, y }))
                {
                    clicked = (int)i;
                    break;
                }
            }

            if (clicked != -1)
            {
                pState->selectedIndex = clicked;
                SendMessage(pState->hParent, WM_VISUAL_UNIT_SELECTED, (WPARAM)clicked, 0);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            else
            {
                // Start drag panning
                pState->isDragging = true;
                pState->dragStartX = x;
                pState->dragStartScrollX = pState->scrollX;
                SetCapture(hWnd);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pState->m_hScroll.OnLButtonUp(pt, hWnd))
        {
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (pState->isDragging)
        {
            pState->isDragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (y >= HEADER_HEIGHT)
        {
            int trackX = x + pState->scrollX;
            for (size_t i = 0; i < pState->units.size(); ++i)
            {
                RECT rcFull = pState->units[i].rc;
                rcFull.bottom += 46;
                if (PtInRect(&rcFull, { trackX, y }))
                {
                    SendMessage(pState->hParent, WM_VISUAL_UNIT_FLIPPED, (WPARAM)i, 0);
                    return 0;
                }
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (pState->m_hScroll.OnMouseWheel(zDelta, 80, hWnd))
        {
            pState->scrollX = pState->m_hScroll.GetPos();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
    {
        if (pState && pState->isFloating)
        {
            // Close button on floating window docks it back
            VisualConsistView_SetFloating(hWnd, false);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            if (pState->pImgFront) delete pState->pImgFront;
            if (pState->pImgMiddle) delete pState->pImgMiddle;
            if (pState->pImgPC) delete pState->pImgPC;
            if (pState->pImgRear) delete pState->pImgRear;
            if (pState->pImgMiddleFlipped) delete pState->pImgMiddleFlipped;
            if (pState->pImgPCFlipped) delete pState->pImgPCFlipped;

            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontCarName) DeleteObject(pState->hFontCarName);
            if (pState->hFontCarNum) DeleteObject(pState->hFontCarNum);
            if (pState->hFontIcons) DeleteObject(pState->hFontIcons);
            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        ShutdownGdiPlus();
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

HWND CreateVisualConsistView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id)
{
    InitGdiPlus();

    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wcx = { 0 };
        wcx.cbSize = sizeof(wcx);
        wcx.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcx.lpfnWndProc = VisualConsistWndProc;
        wcx.hInstance = hInstance;
        wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcx.hbrBackground = NULL;
        wcx.lpszClassName = L"VisualConsistViewClass";
        RegisterClassExW(&wcx);
        s_registered = true;
    }

    VisualConsistState* pState = new VisualConsistState();
    pState->hWnd = NULL;
    pState->hParent = hParent;
    pState->hInst = hInstance;
    pState->controlId = id;
    pState->bDarkMode = TRUE;
    pState->isFloating = false;
    pState->isCollapsed = ReadCollapseOnStartupRegistry();
    pState->scrollX = 0;
    pState->maxScrollX = 0;
    pState->selectedIndex = -1;
    pState->hoverIndex = -1;
    pState->pressedIndex = -1;
    pState->isDragging = false;
    pState->hoverCollapse = false;
    pState->hoverFloat = false;
    pState->pImgFront = NULL;
    pState->pImgMiddle = NULL;
    pState->pImgPC = NULL;
    pState->pImgRear = NULL;
    pState->pImgMiddleFlipped = NULL;
    pState->pImgPCFlipped = NULL;

    pState->hFontTitle = CreateFontW(
        -12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontCarName = CreateFontW(
        -10, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontCarNum = CreateFontW(
        -9, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontIcons = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
    if (!pState->hFontIcons)
    {
        pState->hFontIcons = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

    HWND hWnd = CreateWindowExW(
        0, L"VisualConsistViewClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, w, h,
        hParent, (HMENU)(INT_PTR)id, hInstance, pState
    );

    return hWnd;
}

void VisualConsistView_SetUnits(HWND hWnd, const std::vector<ConsistReader::UnitInfo>& units, const std::wstring& basePath)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->basePath = basePath;
    pState->units.clear();

    for (const auto& u : units)
    {
        VisualUnitItem item;
        item.uid = u.uid;
        item.parentDir = u.parentDir;
        item.isEngine = u.isEngine;
        item.isFlipped = u.isFlipped;
        item.isBroken = false;

        if (item.uid.empty() || item.parentDir.empty())
        {
            item.isBroken = true;
        }
        else
        {
            std::wstring ext = item.isEngine ? L".eng" : L".wag";
            std::wstring unitPath = basePath;
            if (!unitPath.empty() && unitPath.back() != L'\\') unitPath += L'\\';
            unitPath += L"TRAINS\\TRAINSET\\" + item.parentDir + L"\\" + item.uid + ext;

            DWORD attr = GetFileAttributesW(unitPath.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                item.isBroken = true;
            }
        }
        pState->units.push_back(item);
    }

    RECT rc;
    GetClientRect(hWnd, &rc);
    RecalcLayout(pState, rc.right, rc.bottom);
    InvalidateRect(hWnd, NULL, FALSE);
}

void VisualConsistView_SetSelected(HWND hWnd, int index)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->selectedIndex = index;
    RECT rc;
    GetClientRect(hWnd, &rc);
    EnsureVisible(pState, index, rc.right);
    InvalidateRect(hWnd, NULL, FALSE);
}

void VisualConsistView_SetDarkMode(HWND hWnd, BOOL bDark)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->bDarkMode = TRUE;
    InvalidateRect(hWnd, NULL, FALSE);
}

bool VisualConsistView_IsFloating(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->isFloating : false;
}

bool VisualConsistView_IsCollapsed(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->isCollapsed : false;
}

int VisualConsistView_GetDesiredHeight(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return EXPANDED_HEIGHT;
    if (pState->isFloating) return 0;
    if (pState->isCollapsed) return HEADER_HEIGHT;
    return EXPANDED_HEIGHT;
}

void VisualConsistView_SetFloating(HWND hWnd, bool bFloating)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState || pState->isFloating == bFloating) return;

    pState->isFloating = bFloating;

    if (bFloating)
    {
        // 100% Frameless Modern Popup Dialog attached as tool window
        SetParent(hWnd, NULL);
        DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE;
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        SetWindowLongPtrW(hWnd, GWL_STYLE, style);
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);

        // Windows 11 Rounded Corners
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        RECT rcParent;
        GetWindowRect(pState->hParent, &rcParent);
        int floatW = 1040;
        int floatH = 216;
        int floatX = rcParent.left + (rcParent.right - rcParent.left - floatW) / 2;
        int floatY = rcParent.bottom - floatH - 30;

        SetWindowPos(hWnd, HWND_TOPMOST, floatX, floatY, floatW, floatH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetWindowTextW(hWnd, L"");
    }
    else
    {
        // Re-attach as child docked view
        DWORD style = WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE;
        DWORD exStyle = 0;
        SetWindowLongPtrW(hWnd, GWL_STYLE, style);
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
        SetParent(hWnd, pState->hParent);
        SetWindowTextW(hWnd, L"");
    }

    SendMessage(pState->hParent, WM_VISUAL_DOCK_CHANGED, 0, 0);
    InvalidateRect(hWnd, NULL, FALSE);
}
