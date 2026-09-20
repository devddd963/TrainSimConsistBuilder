#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PoolMutatorDlg.h"
#include "PoolManagerDlg.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "ModernContextMenu.h"
#include "../SRC/PoolMutator.h"
#include "../SRC/PoolManager.h"
#include "../SRC/TrainSimConsistBuilder.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <shlwapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

extern HFONT GetAdaptiveSystemFont();
extern std::wstring g_szBasePath;

namespace
{
    static HWND g_hPoolMutatorDlg = NULL;

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

    static void DrawModernButton(HDC hdc, const RECT& rc, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, HFONT hFont, HFONT hIconFont = NULL, const wchar_t* iconGlyph = NULL)
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

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);

        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hPen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textCol);

        bool hasIcon = (iconGlyph && hIconFont && wcslen(iconGlyph) > 0);
        bool hasText = (text && wcslen(text) > 0);

        if (hasIcon && hasText)
        {
            SelectObject(hdc, hIconFont);
            SIZE iconSz = { 0 };
            GetTextExtentPoint32W(hdc, iconGlyph, (int)wcslen(iconGlyph), &iconSz);

            SelectObject(hdc, hFont);
            SIZE textSz = { 0 };
            GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSz);

            int totalW = iconSz.cx + 6 + textSz.cx;
            int startX = rc.left + (rc.right - rc.left - totalW) / 2;
            int cy = rc.top + (rc.bottom - rc.top) / 2;

            RECT rcIcon = { startX, cy - iconSz.cy / 2, startX + iconSz.cx, cy + iconSz.cy / 2 + 2 };
            SelectObject(hdc, hIconFont);
            DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT rcText = { startX + iconSz.cx + 6, cy - textSz.cy / 2, startX + totalW, cy + textSz.cy / 2 + 2 };
            SelectObject(hdc, hFont);
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (hasIcon)
        {
            SelectObject(hdc, hIconFont);
            RECT rcCopy = rc;
            DrawTextW(hdc, iconGlyph, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else if (hasText)
        {
            SelectObject(hdc, hFont);
            RECT rcCopy = rc;
            DrawTextW(hdc, text, -1, &rcCopy, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    struct MutatorDlgState
    {
        HWND hWnd = NULL;
        HWND hParent = NULL;

        PoolMutator::MutatorMode mode = PoolMutator::MutatorMode::MutateConsists;
        std::vector<std::wstring> targetConsistPaths;
        std::vector<int> targetUnitIndices;

        // Fonts
        HFONT hFontTitle = NULL;
        HFONT hFontMain = NULL;
        HFONT hFontBold = NULL;
        HFONT hFontSmall = NULL;
        HFONT hFontIcon = NULL;
        HFONT hFontIconLg = NULL;

        // Child Edit Controls
        HWND hEditCustomCount = NULL;
        HWND hEditInsertCount = NULL;
        HWND hEditPositionIndex = NULL;
        HWND hEditCloneSuffix = NULL;

        // Configuration states
        PoolMutator::CountMode countMode = PoolMutator::CountMode::KeepOriginalCount;
        PoolMutator::PositionMode posMode = PoolMutator::PositionMode::TailPosition;
        int selectedPresetIdx = 0;
        std::vector<int> selectedPoolIndices; // Empty = Entire Preset (All Pools)
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        int customCount = 20;
        int insertCount = 2;
        int positionIndex = 0;

        // Interactive Rectangles
        RECT rcTitleBar = { 0 };
        RECT rcCloseBtn = { 0 };
        RECT rcTab0 = { 0 };
        RECT rcTab1 = { 0 };
        RECT rcTab2 = { 0 };
        RECT rcPresetPicker = { 0 };
        RECT rcPoolPicker = { 0 };
        RECT rcRadioCount0 = { 0 };
        RECT rcRadioCount1 = { 0 };
        RECT rcRadioCount2 = { 0 };
        RECT rcRadioPos0 = { 0 };
        RECT rcRadioPos1 = { 0 };
        RECT rcRadioPos2 = { 0 };
        RECT rcRadioPos3 = { 0 };
        RECT rcCheckboxClones = { 0 };
        RECT rcEditCloneSuffixBorder = { 0 };
        RECT rcEditCustomCountBorder = { 0 };
        RECT rcEditInsertCountBorder = { 0 };
        RECT rcEditPositionIndexBorder = { 0 };
        RECT rcApplyBtn = { 0 };
        RECT rcCancelBtn = { 0 };

        // Hover States
        bool isHoverClose = false;
        bool isHoverTab0 = false;
        bool isHoverTab1 = false;
        bool isHoverTab2 = false;
        bool isHoverPreset = false;
        bool isHoverPool = false;
        bool isHoverRadioCount0 = false;
        bool isHoverRadioCount1 = false;
        bool isHoverRadioCount2 = false;
        bool isHoverRadioPos0 = false;
        bool isHoverRadioPos1 = false;
        bool isHoverRadioPos2 = false;
        bool isHoverRadioPos3 = false;
        bool isHoverCheckClones = false;
        bool isHoverApply = false;
        bool isHoverCancel = false;
    };

    static MutatorDlgState g_State;

    static void UpdateControlPositions(HWND hWnd)
    {
        bool isMutate = (g_State.mode == PoolMutator::MutatorMode::MutateConsists);
        bool isReplace = (g_State.mode == PoolMutator::MutatorMode::ReplaceSelected);
        bool isInsert = (g_State.mode == PoolMutator::MutatorMode::InsertUnits);

        // Custom count edit box (Mutate Consists mode, border at 235, 295, 295, 321)
        if (g_State.hEditCustomCount)
        {
            if (isMutate && g_State.countMode == PoolMutator::CountMode::CustomUnitCount)
            {
                SetWindowPos(g_State.hEditCustomCount, NULL, 238, 299, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditCustomCount, SW_HIDE);
            }
        }

        // Insert count edit box (Insert Units mode, border at 150, 213, 210, 239)
        if (g_State.hEditInsertCount)
        {
            if (isInsert)
            {
                SetWindowPos(g_State.hEditInsertCount, NULL, 153, 217, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditInsertCount, SW_HIDE);
            }
        }

        // Position index edit box (Insert Units mode - Specific Index, border at 424, 299, 484, 325)
        if (g_State.hEditPositionIndex)
        {
            if (isInsert && g_State.posMode == PoolMutator::PositionMode::SpecificIndex)
            {
                SetWindowPos(g_State.hEditPositionIndex, NULL, 427, 303, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditPositionIndex, SW_HIDE);
            }
        }

        // Clone suffix edit box (border at 124, 369, 286, 395)
        if (g_State.hEditCloneSuffix)
        {
            SetWindowPos(g_State.hEditCloneSuffix, NULL, 128, 373, 154, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        InvalidateRect(hWnd, NULL, TRUE);
    }

    static void ShowPresetDropdown(HWND hWnd)
    {
        if (PoolManager::g_PoolPresetsCache.empty()) return;

        std::vector<ContextMenuItem> items;
        for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
            bool isCurrent = ((int)i == g_State.selectedPresetIdx);
            items.push_back(ContextMenuItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, true));
        }

        RECT rcScreen = g_State.rcPresetPicker;
        MapWindowPoints(hWnd, NULL, (LPPOINT)&rcScreen, 2);
        int pickerW = rcScreen.right - rcScreen.left;

        int chosen = ModernContextMenu::Show(hWnd, rcScreen.left, rcScreen.bottom, items, TRUE, pickerW);
        if (chosen > 0)
        {
            g_State.selectedPresetIdx = chosen - 1;
            g_State.selectedPoolIndices.clear();
            PoolManager::PoolPreset* pNew = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
            if (pNew)
            {
                for (size_t i = 0; i < pNew->pools.size(); ++i)
                {
                    g_State.selectedPoolIndices.push_back((int)i);
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
        }
    }

    class PoolPickerPopup
    {
    private:
        HWND m_hWnd = NULL;
        HWND m_hParent = NULL;
        int m_presetIdx = 0;
        int m_hoverIndex = -1;
        bool m_bTrackingMouse = false;
        int m_itemHeight = 34;
        int m_sepHeight = 10;

    public:
        static PoolPickerPopup& Instance()
        {
            static PoolPickerPopup s_inst;
            return s_inst;
        }

        void Show(HWND hParent, int presetIdx, const RECT& rcAnchor)
        {
            if (m_hWnd && IsWindow(m_hWnd))
            {
                ReleaseCapture();
                DestroyWindow(m_hWnd);
                m_hWnd = NULL;
            }

            m_hParent = hParent;
            m_presetIdx = presetIdx;
            m_hoverIndex = -1;
            m_bTrackingMouse = false;

            PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(presetIdx);
            if (!pPreset) return;

            size_t numPools = pPreset->pools.size();
            int totalItems = 1 + (int)numPools;

            // Measure width to accommodate labels
            HDC hdcScreen = GetDC(NULL);
            HFONT holdF = (HFONT)SelectObject(hdcScreen, g_State.hFontMain ? g_State.hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            int maxTextW = 220;
            for (size_t i = 0; i < numPools; ++i)
            {
                const auto& pl = pPreset->pools[i];
                std::wstring label = std::to_wstring(i + 1) + L". " + (pl.name.empty() ? L"Pool #" + std::to_wstring(i + 1) : pl.name);
                std::wstring modeStr = (pl.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
                std::wstring tag = std::to_wstring(pl.units.size()) + L" units • [" + modeStr + L", Min:" + std::to_wstring(pl.minCount) + L", Max:" + std::to_wstring(pl.maxCount) + L"]";
                std::wstring fullStr = label + L"       " + tag;
                SIZE sz;
                GetTextExtentPoint32W(hdcScreen, fullStr.c_str(), (int)fullStr.length(), &sz);
                if (sz.cx > maxTextW) maxTextW = sz.cx;
            }
            SelectObject(hdcScreen, holdF);
            ReleaseDC(NULL, hdcScreen);

            RECT rcScreen = rcAnchor;
            MapWindowPoints(hParent, NULL, (LPPOINT)&rcScreen, 2);
            int anchorW = rcScreen.right - rcScreen.left;
            int popupW = (std::max)(anchorW, maxTextW + 60);
            int popupH = 40 + totalItems * 32;

            int x = rcScreen.left;
            int y = rcScreen.bottom + 4;

            HMONITOR hMon = MonitorFromPoint({ x, y }, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfo(hMon, &mi))
            {
                if (x + popupW > mi.rcWork.right) x = mi.rcWork.right - popupW - 8;
                if (y + popupH > mi.rcWork.bottom) y = rcScreen.top - popupH - 4;
            }

            const wchar_t* szClass = L"PoolPickerPopupClass";
            static bool s_registered = false;
            if (!s_registered)
            {
                WNDCLASSEXW wcex = { sizeof(wcex) };
                wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
                wcex.lpfnWndProc = PoolPickerPopup::WndProc;
                wcex.hInstance = GetModuleHandle(NULL);
                wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
                wcex.hbrBackground = CreateSolidBrush(RGB(38, 38, 42));
                wcex.lpszClassName = szClass;
                RegisterClassExW(&wcex);
                s_registered = true;
            }

            m_hWnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW, szClass, L"",
                WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                x, y, popupW, popupH,
                hParent, NULL, GetModuleHandle(NULL), this
            );

            if (m_hWnd)
            {
                BOOL bDark = TRUE;
                DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
                DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
                DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
                SetCapture(m_hWnd);
                SetFocus(m_hWnd);
            }
        }

    private:
        static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
        {
            PoolPickerPopup* pThis = (PoolPickerPopup*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (uMsg == WM_NCCREATE)
            {
                LPCREATESTRUCTW cs = (LPCREATESTRUCTW)lParam;
                pThis = (PoolPickerPopup*)cs->lpCreateParams;
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
                pThis->m_hWnd = hWnd;
                return TRUE;
            }
            if (pThis)
            {
                return pThis->HandleMessage(uMsg, wParam, lParam);
            }
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }

        LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
        {
            switch (uMsg)
            {
            case WM_ERASEBKGND:
                return TRUE;

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hWnd, &ps);

                RECT rcClient;
                GetClientRect(m_hWnd, &rcClient);
                int w = rcClient.right;
                int h = rcClient.bottom;

                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, w, h);
                HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

                COLORREF bgMenu   = RGB(38, 38, 42);
                COLORREF borderCol= RGB(65, 68, 76);
                COLORREF hoverBg  = RGB(56, 60, 70);
                COLORREF sepColor = RGB(52, 55, 62);
                COLORREF textNorm = RGB(240, 242, 248);
                COLORREF textSec  = RGB(160, 168, 180);
                COLORREF accentCol= RGB(0, 120, 215);

                HBRUSH hbrBg = CreateSolidBrush(bgMenu);
                FillRect(hMemDC, &rcClient, hbrBg);
                DeleteObject(hbrBg);

                SetBkMode(hMemDC, TRANSPARENT);

                PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(m_presetIdx);
                size_t numPools = pPreset ? pPreset->pools.size() : 0;
                bool isAllSelected = (numPools > 0 && g_State.selectedPoolIndices.size() == numPools);
                bool hasAnySelected = (!g_State.selectedPoolIndices.empty());
                bool isIndeterminate = (hasAnySelected && !isAllSelected);

                int curY = 6;

                // --- 1. Item 0: Entire Preset (All Pools) ---
                RECT rcItem0 = { 6, curY, w - 6, curY + m_itemHeight };
                bool isHover0 = (m_hoverIndex == 0);
                if (isHover0)
                {
                    HBRUSH hbrHover = CreateSolidBrush(hoverBg);
                    HPEN hPenHover = CreatePen(PS_SOLID, 1, hoverBg);
                    HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrHover);
                    HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenHover);
                    RoundRect(hMemDC, rcItem0.left, rcItem0.top, rcItem0.right, rcItem0.bottom, 6, 6);
                    SelectObject(hMemDC, hOldB);
                    SelectObject(hMemDC, hOldP);
                    DeleteObject(hbrHover);
                    DeleteObject(hPenHover);
                }

                // Checkbox for Item 0
                RECT rcBox0 = { rcItem0.left + 8, rcItem0.top + (m_itemHeight - 16) / 2, rcItem0.left + 24, rcItem0.top + (m_itemHeight - 16) / 2 + 16 };
                bool isBox0Filled = (isAllSelected || isIndeterminate);
                HBRUSH hBoxBr0 = CreateSolidBrush(isBox0Filled ? accentCol : RGB(32, 32, 34));
                HPEN hBoxPen0 = CreatePen(PS_SOLID, 1, isBox0Filled ? accentCol : borderCol);
                HBRUSH hOldBoxB0 = (HBRUSH)SelectObject(hMemDC, hBoxBr0);
                HPEN hOldBoxP0 = (HPEN)SelectObject(hMemDC, hBoxPen0);
                RoundRect(hMemDC, rcBox0.left, rcBox0.top, rcBox0.right, rcBox0.bottom, 4, 4);
                SelectObject(hMemDC, hOldBoxB0);
                SelectObject(hMemDC, hOldBoxP0);
                DeleteObject(hBoxBr0);
                DeleteObject(hBoxPen0);

                if (isAllSelected)
                {
                    SelectObject(hMemDC, g_State.hFontIcon);
                    SetTextColor(hMemDC, RGB(255, 255, 255));
                    RECT rcGlyph = { rcBox0.left, rcBox0.top - 1, rcBox0.right, rcBox0.bottom };
                    DrawTextW(hMemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else if (isIndeterminate)
                {
                    HBRUSH hDotBr = CreateSolidBrush(RGB(255, 255, 255));
                    RECT rcDot = { rcBox0.left + 4, rcBox0.top + 4, rcBox0.right - 4, rcBox0.bottom - 4 };
                    FillRect(hMemDC, &rcDot, hDotBr);
                    DeleteObject(hDotBr);
                }

                // Star Icon
                SelectObject(hMemDC, g_State.hFontIcon);
                SetTextColor(hMemDC, RGB(255, 185, 0));
                RECT rcStar = { rcBox0.right + 8, rcItem0.top, rcBox0.right + 26, rcItem0.bottom };
                DrawTextW(hMemDC, L"\xE735", -1, &rcStar, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Entire Preset Label
                SelectObject(hMemDC, g_State.hFontBold);
                SetTextColor(hMemDC, textNorm);
                RECT rcText0 = { rcStar.right + 6, rcItem0.top, w - 160, rcItem0.bottom };
                DrawTextW(hMemDC, L"★ Entire Preset (All Pools)", -1, &rcText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Tag for Item 0
                int totalUnits = 0;
                if (pPreset)
                {
                    for (const auto& pl : pPreset->pools) totalUnits += (int)pl.units.size();
                }
                std::wstring tag0 = std::to_wstring(numPools) + L" pools • " + std::to_wstring(totalUnits) + L" units";
                SelectObject(hMemDC, g_State.hFontSmall);
                SetTextColor(hMemDC, textSec);
                RECT rcTag0 = { w - 160, rcItem0.top, w - 16, rcItem0.bottom };
                DrawTextW(hMemDC, tag0.c_str(), -1, &rcTag0, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                curY += m_itemHeight + 2;

                // --- 2. Separator Line ---
                HPEN hPenSep = CreatePen(PS_SOLID, 1, sepColor);
                HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenSep);
                int sepY = curY + m_sepHeight / 2;
                MoveToEx(hMemDC, 12, sepY, NULL);
                LineTo(hMemDC, w - 12, sepY);
                SelectObject(hMemDC, hOldP);
                DeleteObject(hPenSep);
                curY += m_sepHeight;

                // --- 3. Individual Pools ---
                if (pPreset)
                {
                    for (size_t i = 0; i < numPools; ++i)
                    {
                        const auto& pool = pPreset->pools[i];
                        int itemIdx = 1 + (int)i;
                        RECT rcItem = { 6, curY, w - 6, curY + m_itemHeight };
                        bool isHover = (m_hoverIndex == itemIdx);

                        if (isHover)
                        {
                            HBRUSH hbrHover = CreateSolidBrush(hoverBg);
                            HPEN hPenHover = CreatePen(PS_SOLID, 1, hoverBg);
                            HBRUSH hOldB = (HBRUSH)SelectObject(hMemDC, hbrHover);
                            HPEN hOldP = (HPEN)SelectObject(hMemDC, hPenHover);
                            RoundRect(hMemDC, rcItem.left, rcItem.top, rcItem.right, rcItem.bottom, 6, 6);
                            SelectObject(hMemDC, hOldB);
                            SelectObject(hMemDC, hOldP);
                            DeleteObject(hbrHover);
                            DeleteObject(hPenHover);
                        }

                        bool isChecked = (std::find(g_State.selectedPoolIndices.begin(), g_State.selectedPoolIndices.end(), (int)i) != g_State.selectedPoolIndices.end());

                        // Checkbox Box
                        RECT rcBox = { rcItem.left + 8, rcItem.top + (m_itemHeight - 16) / 2, rcItem.left + 24, rcItem.top + (m_itemHeight - 16) / 2 + 16 };
                        HBRUSH hBoxBr = CreateSolidBrush(isChecked ? accentCol : RGB(32, 32, 34));
                        HPEN hBoxPen = CreatePen(PS_SOLID, 1, isChecked ? accentCol : borderCol);
                        HBRUSH hOldBoxB = (HBRUSH)SelectObject(hMemDC, hBoxBr);
                        HPEN hOldBoxP = (HPEN)SelectObject(hMemDC, hBoxPen);
                        RoundRect(hMemDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);
                        SelectObject(hMemDC, hOldBoxB);
                        SelectObject(hMemDC, hOldBoxP);
                        DeleteObject(hBoxBr);
                        DeleteObject(hBoxPen);

                        if (isChecked)
                        {
                            SelectObject(hMemDC, g_State.hFontIcon);
                            SetTextColor(hMemDC, RGB(255, 255, 255));
                            RECT rcGlyph = { rcBox.left, rcBox.top - 1, rcBox.right, rcBox.bottom };
                            DrawTextW(hMemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }

                        // Pool Type Icon
                        std::wstring iconGlyph = pool.units.empty() ? L"\xE7BA" : (pool.units[0].isEngine ? L"\xE7C3" : L"\xE8EC");
                        SelectObject(hMemDC, g_State.hFontIcon);
                        SetTextColor(hMemDC, pool.units.empty() ? textSec : RGB(96, 205, 255));
                        RECT rcTypeIcon = { rcBox.right + 8, rcItem.top, rcBox.right + 26, rcItem.bottom };
                        DrawTextW(hMemDC, iconGlyph.c_str(), -1, &rcTypeIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        // Pool Name Label
                        std::wstring poolName = pool.name.empty() ? (L"Pool #" + std::to_wstring(i + 1)) : pool.name;
                        std::wstring label = std::to_wstring(i + 1) + L". " + poolName;
                        SelectObject(hMemDC, g_State.hFontMain);
                        SetTextColor(hMemDC, textNorm);
                        RECT rcText = { rcTypeIcon.right + 6, rcItem.top, w - 190, rcItem.bottom };
                        DrawTextW(hMemDC, label.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                        // Tag info
                        std::wstring modeStr = (pool.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
                        std::wstring tag = std::to_wstring(pool.units.size()) + L" units • [" + modeStr + L", " + std::to_wstring(pool.minCount) + L"-" + std::to_wstring(pool.maxCount) + L"]";
                        SelectObject(hMemDC, g_State.hFontSmall);
                        SetTextColor(hMemDC, textSec);
                        RECT rcTag = { w - 190, rcItem.top, w - 16, rcItem.bottom };
                        DrawTextW(hMemDC, tag.c_str(), -1, &rcTag, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        curY += m_itemHeight;
                    }
                }

                // Perimeter Border
                HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
                SelectObject(hMemDC, hPenBorder);
                SelectObject(hMemDC, GetStockObject(NULL_BRUSH));
                RoundRect(hMemDC, 0, 0, w, h, 10, 10);
                DeleteObject(hPenBorder);

                BitBlt(hdc, 0, 0, w, h, hMemDC, 0, 0, SRCCOPY);
                SelectObject(hMemDC, hOldBmp);
                DeleteObject(hMemBmp);
                DeleteDC(hMemDC);

                EndPaint(m_hWnd, &ps);
                return 0;
            }

            case WM_MOUSEMOVE:
            {
                if (!m_bTrackingMouse)
                {
                    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
                    TrackMouseEvent(&tme);
                    m_bTrackingMouse = true;
                }

                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                RECT rcClient;
                GetClientRect(m_hWnd, &rcClient);

                int newHover = -1;
                if (x >= 6 && x < rcClient.right - 6)
                {
                    if (y >= 6 && y < 6 + m_itemHeight)
                    {
                        newHover = 0;
                    }
                    else if (y >= 6 + m_itemHeight + m_sepHeight)
                    {
                        newHover = 1 + (y - (6 + m_itemHeight + m_sepHeight)) / m_itemHeight;
                    }
                }

                PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(m_presetIdx);
                int totalItems = 1 + (pPreset ? (int)pPreset->pools.size() : 0);
                if (newHover >= totalItems) newHover = -1;

                if (newHover != m_hoverIndex)
                {
                    m_hoverIndex = newHover;
                    InvalidateRect(m_hWnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_MOUSELEAVE:
            {
                m_bTrackingMouse = false;
                if (m_hoverIndex != -1)
                {
                    m_hoverIndex = -1;
                    InvalidateRect(m_hWnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_LBUTTONDOWN:
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT rcClient;
                GetClientRect(m_hWnd, &rcClient);

                if (!PtInRect(&rcClient, pt))
                {
                    ReleaseCapture();
                    DestroyWindow(m_hWnd);
                    return 0;
                }

                PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(m_presetIdx);
                if (!pPreset) return 0;
                size_t numPools = pPreset->pools.size();
                int totalItems = 1 + (int)numPools;

                int clickIdx = -1;
                if (pt.y >= 6 && pt.y < 6 + m_itemHeight)
                {
                    clickIdx = 0;
                }
                else if (pt.y >= 6 + m_itemHeight + m_sepHeight)
                {
                    clickIdx = 1 + (pt.y - (6 + m_itemHeight + m_sepHeight)) / m_itemHeight;
                }

                if (clickIdx >= 0 && clickIdx < totalItems)
                {
                    bool hasAnySelected = (!g_State.selectedPoolIndices.empty());

                    if (clickIdx == 0) // Entire Preset (Master Checkbox)
                    {
                        if (hasAnySelected)
                        {
                            // If any or all are ticked, remove all ticks
                            g_State.selectedPoolIndices.clear();
                        }
                        else
                        {
                            // If none are ticked, tick all pools
                            g_State.selectedPoolIndices.clear();
                            for (size_t k = 0; k < numPools; ++k)
                            {
                                g_State.selectedPoolIndices.push_back((int)k);
                            }
                        }
                    }
                    else // Individual pool at index (clickIdx - 1)
                    {
                        int poolIdx = clickIdx - 1;
                        auto it = std::find(g_State.selectedPoolIndices.begin(), g_State.selectedPoolIndices.end(), poolIdx);
                        if (it != g_State.selectedPoolIndices.end())
                        {
                            g_State.selectedPoolIndices.erase(it);
                        }
                        else
                        {
                            g_State.selectedPoolIndices.push_back(poolIdx);
                            std::sort(g_State.selectedPoolIndices.begin(), g_State.selectedPoolIndices.end());
                        }
                    }

                    InvalidateRect(m_hWnd, NULL, FALSE);
                    if (m_hParent && IsWindow(m_hParent))
                    {
                        InvalidateRect(m_hParent, NULL, TRUE);
                    }
                }
                return 0;
            }

            case WM_KEYDOWN:
            {
                if (wParam == VK_ESCAPE)
                {
                    ReleaseCapture();
                    DestroyWindow(m_hWnd);
                    return 0;
                }
                break;
            }

            case WM_KILLFOCUS:
            {
                ReleaseCapture();
                DestroyWindow(m_hWnd);
                return 0;
            }

            case WM_ACTIVATE:
            {
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    ReleaseCapture();
                    DestroyWindow(m_hWnd);
                }
                return 0;
            }

            case WM_DESTROY:
            {
                m_hWnd = NULL;
                return 0;
            }
            }
            return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
        }
    };

    static void ShowPoolDropdown(HWND hWnd)
    {
        PoolPickerPopup::Instance().Show(hWnd, g_State.selectedPresetIdx, g_State.rcPoolPicker);
    }

    static void ExecuteMutation(HWND hWnd)
    {
        if (g_State.selectedPoolIndices.empty())
        {
            ShowModernMessageBox(hWnd, L"Please select at least one source pool to proceed.", L"No Pools Selected", MB_OK | MB_ICONWARNING);
            return;
        }

        PoolMutator::MutatorOptions opts;
        opts.mode = g_State.mode;
        opts.presetIndex = g_State.selectedPresetIdx;
        opts.selectedPoolIndices = g_State.selectedPoolIndices;
        if (opts.selectedPoolIndices.size() == 1)
        {
            opts.poolIndex = opts.selectedPoolIndices[0];
        }
        else
        {
            opts.poolIndex = -1;
        }
        opts.countMode = g_State.countMode;

        wchar_t buf[64] = { 0 };
        GetWindowTextW(g_State.hEditCustomCount, buf, 64);
        opts.customCount = _wtoi(buf);
        if (opts.customCount <= 0) opts.customCount = 20;

        opts.createClones = g_State.createClones;
        wchar_t sbuf[128] = { 0 };
        GetWindowTextW(g_State.hEditCloneSuffix, sbuf, 128);
        opts.cloneSuffix = sbuf;
        if (opts.cloneSuffix.empty()) opts.cloneSuffix = L"_PoolVar";

        opts.posMode = g_State.posMode;
        GetWindowTextW(g_State.hEditInsertCount, buf, 64);
        opts.insertCount = _wtoi(buf);
        if (opts.insertCount <= 0) opts.insertCount = 1;

        GetWindowTextW(g_State.hEditPositionIndex, buf, 64);
        int uiPos = _wtoi(buf);
        opts.positionIndex = (uiPos > 0) ? (uiPos - 1) : 0;

        PoolMutator::MutatorResult res;
        bool ok = ApplyPoolMutationToSessions(
            g_State.hParent,
            g_State.targetConsistPaths,
            g_State.targetUnitIndices,
            opts,
            res
        );

        if (ok && res.success)
        {
            std::wstring successMsg = L"Operation completed successfully!\n\n";
            successMsg += L"Updated " + std::to_wstring(res.processedCount) + L" consist(s).\n";
            if (!opts.createClones)
            {
                successMsg += L"Changes staged in memory (●). Use 'Save Consist(s)' (Ctrl+S) when ready to save to disk.\n";
            }
            if (!res.affectedFiles.empty())
            {
                successMsg += L"\nFirst modified consist:\n" + res.affectedFiles[0];
                if (res.affectedFiles.size() > 1)
                {
                    successMsg += L"\n...and " + std::to_wstring(res.affectedFiles.size() - 1) + L" other(s).";
                }
            }

            ShowModernMessageBox(hWnd, successMsg.c_str(), L"Pool Mutator & Injector", MB_OK | MB_ICONINFORMATION);
            DestroyWindow(hWnd);
        }
        else
        {
            std::wstring err = res.errorMessage.empty() ? L"No consists were modified or an error occurred." : res.errorMessage;
            ShowModernMessageBox(hWnd, err.c_str(), L"Pool Mutator Error", MB_OK | MB_ICONERROR);
        }
    }

    static LRESULT CALLBACK PoolMutatorDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_NCACTIVATE:
            return TRUE;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_NCHITTEST:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            RECT rcTopClose = { rcClient.right - 44, 0, rcClient.right, 42 };
            if (PtInRect(&rcTopClose, pt)) return HTCLIENT;

            int b = 8;
            if (pt.y < b && pt.x < b) return HTTOPLEFT;
            if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
            if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
            if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
            if (pt.y < b) return HTTOP;
            if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
            if (pt.x < b) return HTLEFT;
            if (pt.x >= rcClient.right - b) return HTRIGHT;

            if (pt.y <= 42 && pt.x < rcClient.right - 44)
            {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_KILLFOCUS:
        case WM_CAPTURECHANGED:
        {
            g_State.isHoverClose = false;
            g_State.isHoverApply = false;
            g_State.isHoverCancel = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_SYSCOMMAND:
        {
            if ((wParam & 0xFFF0) == SC_CLOSE)
            {
                if (g_State.hParent && IsWindow(g_State.hParent))
                {
                    SetForegroundWindow(g_State.hParent);
                    SetActiveWindow(g_State.hParent);
                }
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE)
            {
                if (g_State.hParent && IsWindow(g_State.hParent))
                {
                    SetForegroundWindow(g_State.hParent);
                    SetActiveWindow(g_State.hParent);
                }
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_CREATE:
        {
            g_State.hWnd = hWnd;
            g_State.hFontTitle = CreateCustomFont(13, FW_SEMIBOLD, L"Segoe UI");
            g_State.hFontMain = CreateCustomFont(10, FW_NORMAL, L"Segoe UI");
            g_State.hFontBold = CreateCustomFont(10, FW_SEMIBOLD, L"Segoe UI");
            g_State.hFontSmall = CreateCustomFont(9, FW_NORMAL, L"Segoe UI");
            g_State.hFontIcon = CreateCustomFont(11, FW_NORMAL, L"Segoe Fluent Icons");
            if (!g_State.hFontIcon) g_State.hFontIcon = CreateCustomFont(11, FW_NORMAL, L"Segoe MDL2 Assets");
            g_State.hFontIconLg = CreateCustomFont(14, FW_NORMAL, L"Segoe Fluent Icons");
            if (!g_State.hFontIconLg) g_State.hFontIconLg = CreateCustomFont(14, FW_NORMAL, L"Segoe MDL2 Assets");

            // Custom Count Edit Box
            g_State.hEditCustomCount = CreateWindowExW(0, L"EDIT", L"20",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                238, 299, 54, 18, hWnd, (HMENU)201, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditCustomCount, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditCustomCount, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

            // Insert Count Edit Box
            g_State.hEditInsertCount = CreateWindowExW(0, L"EDIT", L"2",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                153, 217, 54, 18, hWnd, (HMENU)202, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditInsertCount, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditInsertCount, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

            // Position Index Edit Box (1-based for UI)
            g_State.hEditPositionIndex = CreateWindowExW(0, L"EDIT", L"1",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                427, 303, 54, 18, hWnd, (HMENU)203, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditPositionIndex, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditPositionIndex, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

            // Clone Suffix Edit Box
            g_State.hEditCloneSuffix = CreateWindowExW(0, L"EDIT", L"_PoolVar",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                128, 373, 154, 18, hWnd, (HMENU)204, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditCloneSuffix, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditCloneSuffix, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

            if (g_State.selectedPresetIdx < 0 || g_State.selectedPresetIdx >= (int)PoolManager::g_PoolPresetsCache.size())
            {
                g_State.selectedPresetIdx = PoolManager::g_ActivePresetIndex;
            }
            if (g_State.selectedPresetIdx < 0 || g_State.selectedPresetIdx >= (int)PoolManager::g_PoolPresetsCache.size())
            {
                g_State.selectedPresetIdx = 0;
            }

            g_State.selectedPoolIndices.clear();
            PoolManager::PoolPreset* pInitPres = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
            if (pInitPres)
            {
                for (size_t k = 0; k < pInitPres->pools.size(); ++k)
                {
                    g_State.selectedPoolIndices.push_back((int)k);
                }
            }

            UpdateControlPositions(hWnd);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            HDC hdcCtl = (HDC)wParam;
            COLORREF bgCol = RGB(32, 32, 34);
            COLORREF txtCol = RGB(245, 245, 245);
            SetBkColor(hdcCtl, bgCol);
            SetTextColor(hdcCtl, txtCol);
            static HBRUSH hbrEditDark = CreateSolidBrush(RGB(32, 32, 34));
            return (LRESULT)hbrEditDark;
        }

        case WM_MOUSEMOVE:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            POINT pt = { x, y };

            bool hClose = PtInRect(&g_State.rcCloseBtn, pt);
            bool hTab0 = PtInRect(&g_State.rcTab0, pt);
            bool hTab1 = PtInRect(&g_State.rcTab1, pt);
            bool hTab2 = PtInRect(&g_State.rcTab2, pt);
            bool hPreset = PtInRect(&g_State.rcPresetPicker, pt);
            bool hPool = PtInRect(&g_State.rcPoolPicker, pt);
            bool hRadC0 = PtInRect(&g_State.rcRadioCount0, pt);
            bool hRadC1 = PtInRect(&g_State.rcRadioCount1, pt);
            bool hRadC2 = PtInRect(&g_State.rcRadioCount2, pt);
            bool hRadP0 = PtInRect(&g_State.rcRadioPos0, pt);
            bool hRadP1 = PtInRect(&g_State.rcRadioPos1, pt);
            bool hRadP2 = PtInRect(&g_State.rcRadioPos2, pt);
            bool hRadP3 = PtInRect(&g_State.rcRadioPos3, pt);
            bool hCheck = PtInRect(&g_State.rcCheckboxClones, pt);
            bool hApply = PtInRect(&g_State.rcApplyBtn, pt);

            if (hClose != g_State.isHoverClose || hTab0 != g_State.isHoverTab0 ||
                hTab1 != g_State.isHoverTab1 || hTab2 != g_State.isHoverTab2 ||
                hPreset != g_State.isHoverPreset || hPool != g_State.isHoverPool ||
                hRadC0 != g_State.isHoverRadioCount0 || hRadC1 != g_State.isHoverRadioCount1 ||
                hRadC2 != g_State.isHoverRadioCount2 || hRadP0 != g_State.isHoverRadioPos0 ||
                hRadP1 != g_State.isHoverRadioPos1 || hRadP2 != g_State.isHoverRadioPos2 ||
                hRadP3 != g_State.isHoverRadioPos3 || hCheck != g_State.isHoverCheckClones ||
                hApply != g_State.isHoverApply)
            {
                g_State.isHoverClose = hClose;
                g_State.isHoverTab0 = hTab0;
                g_State.isHoverTab1 = hTab1;
                g_State.isHoverTab2 = hTab2;
                g_State.isHoverPreset = hPreset;
                g_State.isHoverPool = hPool;
                g_State.isHoverRadioCount0 = hRadC0;
                g_State.isHoverRadioCount1 = hRadC1;
                g_State.isHoverRadioCount2 = hRadC2;
                g_State.isHoverRadioPos0 = hRadP0;
                g_State.isHoverRadioPos1 = hRadP1;
                g_State.isHoverRadioPos2 = hRadP2;
                g_State.isHoverRadioPos3 = hRadP3;
                g_State.isHoverCheckClones = hCheck;
                g_State.isHoverApply = hApply;
                InvalidateRect(hWnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            g_State.isHoverClose = false;
            g_State.isHoverTab0 = false;
            g_State.isHoverTab1 = false;
            g_State.isHoverTab2 = false;
            g_State.isHoverPreset = false;
            g_State.isHoverPool = false;
            g_State.isHoverRadioCount0 = false;
            g_State.isHoverRadioCount1 = false;
            g_State.isHoverRadioCount2 = false;
            g_State.isHoverRadioPos0 = false;
            g_State.isHoverRadioPos1 = false;
            g_State.isHoverRadioPos2 = false;
            g_State.isHoverRadioPos3 = false;
            g_State.isHoverCheckClones = false;
            g_State.isHoverApply = false;
            g_State.isHoverCancel = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            POINT pt = { x, y };

            if (PtInRect(&g_State.rcCloseBtn, pt))
            {
                if (g_State.hParent && IsWindow(g_State.hParent))
                {
                    SetForegroundWindow(g_State.hParent);
                    SetActiveWindow(g_State.hParent);
                }
                DestroyWindow(hWnd);
                return 0;
            }

            // Mode Tabs
            if (PtInRect(&g_State.rcTab0, pt))
            {
                g_State.mode = PoolMutator::MutatorMode::MutateConsists;
                UpdateControlPositions(hWnd);
                return 0;
            }
            if (PtInRect(&g_State.rcTab1, pt))
            {
                g_State.mode = PoolMutator::MutatorMode::ReplaceSelected;
                UpdateControlPositions(hWnd);
                return 0;
            }
            if (PtInRect(&g_State.rcTab2, pt))
            {
                g_State.mode = PoolMutator::MutatorMode::InsertUnits;
                UpdateControlPositions(hWnd);
                return 0;
            }

            // Dropdown pickers
            if (PtInRect(&g_State.rcPresetPicker, pt))
            {
                ShowPresetDropdown(hWnd);
                return 0;
            }
            if (PtInRect(&g_State.rcPoolPicker, pt))
            {
                ShowPoolDropdown(hWnd);
                return 0;
            }

            // Mode 0: Count Mode Radios
            if (g_State.mode == PoolMutator::MutatorMode::MutateConsists)
            {
                if (PtInRect(&g_State.rcRadioCount0, pt))
                {
                    g_State.countMode = PoolMutator::CountMode::KeepOriginalCount;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioCount1, pt))
                {
                    g_State.countMode = PoolMutator::CountMode::DynamicPoolRules;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioCount2, pt))
                {
                    g_State.countMode = PoolMutator::CountMode::CustomUnitCount;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
            }

            // Mode 2: Position Mode Radios
            if (g_State.mode == PoolMutator::MutatorMode::InsertUnits)
            {
                if (PtInRect(&g_State.rcRadioPos0, pt))
                {
                    g_State.posMode = PoolMutator::PositionMode::HeadPosition;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioPos1, pt))
                {
                    g_State.posMode = PoolMutator::PositionMode::BehindEngines;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioPos2, pt))
                {
                    g_State.posMode = PoolMutator::PositionMode::SpecificIndex;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioPos3, pt))
                {
                    g_State.posMode = PoolMutator::PositionMode::TailPosition;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
            }

            // Checkbox for clones
            if (PtInRect(&g_State.rcCheckboxClones, pt))
            {
                g_State.createClones = !g_State.createClones;
                InvalidateRect(hWnd, &g_State.rcCheckboxClones, FALSE);
                return 0;
            }

            if (PtInRect(&g_State.rcApplyBtn, pt))
            {
                ExecuteMutation(hWnd);
                return 0;
            }
            return 0;
        }

        case WM_LBUTTONUP:
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            HDC hmemDC = CreateCompatibleDC(hdc);
            HBITMAP hbm = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
            HBITMAP holdBm = (HBITMAP)SelectObject(hmemDC, hbm);

            COLORREF bgCol = RGB(26, 26, 28);
            COLORREF titleBgCol = RGB(32, 32, 34);
            COLORREF borderCol = RGB(55, 55, 62);
            COLORREF cardBgCol = RGB(36, 36, 38);
            COLORREF textPrimary = RGB(245, 245, 245);
            COLORREF textSecondary = RGB(170, 170, 170);
            COLORREF accentCol = RGB(0, 120, 215);

            HBRUSH hbrBg = CreateSolidBrush(bgCol);
            FillRect(hmemDC, &rcClient, hbrBg);
            DeleteObject(hbrBg);

            HBRUSH hOldB = NULL;
            HPEN hOldP = NULL;

            // 1. Title Bar
            g_State.rcTitleBar = { 0, 0, rcClient.right, 42 };
            HBRUSH hbrTitle = CreateSolidBrush(titleBgCol);
            FillRect(hmemDC, &g_State.rcTitleBar, hbrTitle);
            DeleteObject(hbrTitle);

            // Title Icon & Text
            SelectObject(hmemDC, g_State.hFontIconLg);
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, accentCol);
            RECT rcIcon = { 16, 0, 42, 42 };
            DrawTextW(hmemDC, L"\xE790", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hmemDC, g_State.hFontTitle);
            SetTextColor(hmemDC, textPrimary);
            RECT rcTitleText = { 46, 0, rcClient.right - 50, 42 };
            DrawTextW(hmemDC, L"Consist Pool Mutator & Injector", -1, &rcTitleText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Close Button
            g_State.rcCloseBtn = { rcClient.right - 44, 0, rcClient.right, 42 };
            if (g_State.isHoverClose)
            {
                HBRUSH hbrClose = CreateSolidBrush(RGB(232, 17, 35));
                FillRect(hmemDC, &g_State.rcCloseBtn, hbrClose);
                DeleteObject(hbrClose);
                SetTextColor(hmemDC, RGB(255, 255, 255));
            }
            else
            {
                SetTextColor(hmemDC, textSecondary);
            }
            SelectObject(hmemDC, g_State.hFontIcon);
            DrawTextW(hmemDC, L"\xE711", -1, &g_State.rcCloseBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Title bottom divider
            HPEN hPenLine = CreatePen(PS_SOLID, 1, borderCol);
            HPEN holdPen = (HPEN)SelectObject(hmemDC, hPenLine);
            MoveToEx(hmemDC, 0, 42, NULL);
            LineTo(hmemDC, rcClient.right, 42);

            // 2. Mode Tabs
            int tabW = (rcClient.right - 50) / 3;
            g_State.rcTab0 = { 20, 52, 20 + tabW, 86 };
            g_State.rcTab1 = { 20 + tabW + 5, 52, 20 + tabW * 2 + 5, 86 };
            g_State.rcTab2 = { 20 + tabW * 2 + 10, 52, rcClient.right - 20, 86 };

            DrawModernButton(hmemDC, g_State.rcTab0, L"Mutate Consists", g_State.isHoverTab0, false, g_State.mode == PoolMutator::MutatorMode::MutateConsists, g_State.hFontBold, g_State.hFontIcon, L"\xE790");
            DrawModernButton(hmemDC, g_State.rcTab1, L"Replace Units", g_State.isHoverTab1, false, g_State.mode == PoolMutator::MutatorMode::ReplaceSelected, g_State.hFontBold, g_State.hFontIcon, L"\xE777");
            DrawModernButton(hmemDC, g_State.rcTab2, L"Insert Units", g_State.isHoverTab2, false, g_State.mode == PoolMutator::MutatorMode::InsertUnits, g_State.hFontBold, g_State.hFontIcon, L"\xE77F");

            // 3. Preset & Pool Selectors (Custom Dropdowns)
            SelectObject(hmemDC, g_State.hFontBold);
            SetTextColor(hmemDC, textPrimary);

            RECT rcLblPreset = { 30, 100, 130, 128 };
            DrawTextW(hmemDC, L"Pool Preset:", -1, &rcLblPreset, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT rcLblPool = { 30, 138, 130, 166 };
            DrawTextW(hmemDC, L"Source Pool:", -1, &rcLblPool, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Preset Dropdown Box
            g_State.rcPresetPicker = { 135, 100, rcClient.right - 30, 128 };
            COLORREF presetBg = g_State.isHoverPreset ? RGB(45, 45, 48) : cardBgCol;
            COLORREF presetBorder = g_State.isHoverPreset ? RGB(90, 90, 95) : borderCol;
            HBRUSH hbrPres = CreateSolidBrush(presetBg);
            HPEN hpenPres = CreatePen(PS_SOLID, 1, presetBorder);
            HBRUSH holdB1 = (HBRUSH)SelectObject(hmemDC, hbrPres);
            HPEN holdP1 = (HPEN)SelectObject(hmemDC, hpenPres);
            RoundRect(hmemDC, g_State.rcPresetPicker.left, g_State.rcPresetPicker.top, g_State.rcPresetPicker.right, g_State.rcPresetPicker.bottom, 6, 6);
            SelectObject(hmemDC, holdB1);
            SelectObject(hmemDC, holdP1);
            DeleteObject(hbrPres);
            DeleteObject(hpenPres);

            std::wstring presetStr = L"No Presets Available";
            PoolManager::PoolPreset* curPres = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
            if (curPres)
            {
                int totalU = 0;
                for (const auto& pl : curPres->pools) totalU += (int)pl.units.size();
                presetStr = curPres->presetName.empty() ? (L"Preset " + std::to_wstring(g_State.selectedPresetIdx + 1)) : curPres->presetName;
                presetStr += L" (" + std::to_wstring(curPres->pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units)";
            }
            SelectObject(hmemDC, g_State.hFontMain);
            SetTextColor(hmemDC, textPrimary);
            RECT rcPresetText = { g_State.rcPresetPicker.left + 10, g_State.rcPresetPicker.top, g_State.rcPresetPicker.right - 30, g_State.rcPresetPicker.bottom };
            DrawTextW(hmemDC, presetStr.c_str(), -1, &rcPresetText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT rcChev1 = { g_State.rcPresetPicker.right - 26, g_State.rcPresetPicker.top, g_State.rcPresetPicker.right - 8, g_State.rcPresetPicker.bottom };
            int c1X = (rcChev1.left + rcChev1.right) / 2;
            int c1Y = (rcChev1.top + rcChev1.bottom) / 2;
            COLORREF c1Col = g_State.isHoverPreset ? RGB(255, 255, 255) : textSecondary;
            HBRUSH hBrC1 = CreateSolidBrush(c1Col);
            HPEN hPenC1 = CreatePen(PS_SOLID, 1, c1Col);
            HBRUSH hOldB1 = (HBRUSH)SelectObject(hmemDC, hBrC1);
            HPEN hOldP1 = (HPEN)SelectObject(hmemDC, hPenC1);
            POINT ptsC1[3] = { { c1X - 4, c1Y - 2 }, { c1X + 4, c1Y - 2 }, { c1X, c1Y + 3 } };
            Polygon(hmemDC, ptsC1, 3);
            SelectObject(hmemDC, hOldB1);
            SelectObject(hmemDC, hOldP1);
            DeleteObject(hBrC1);
            DeleteObject(hPenC1);

            // Pool Dropdown Box
            g_State.rcPoolPicker = { 135, 138, rcClient.right - 30, 166 };
            COLORREF poolBg = g_State.isHoverPool ? RGB(45, 45, 48) : cardBgCol;
            COLORREF poolBorder = g_State.isHoverPool ? RGB(90, 90, 95) : borderCol;
            HBRUSH hbrPool = CreateSolidBrush(poolBg);
            HPEN hpenPool = CreatePen(PS_SOLID, 1, poolBorder);
            HBRUSH holdB2 = (HBRUSH)SelectObject(hmemDC, hbrPool);
            HPEN holdP2 = (HPEN)SelectObject(hmemDC, hpenPool);
            RoundRect(hmemDC, g_State.rcPoolPicker.left, g_State.rcPoolPicker.top, g_State.rcPoolPicker.right, g_State.rcPoolPicker.bottom, 6, 6);
            SelectObject(hmemDC, holdB2);
            SelectObject(hmemDC, holdP2);
            DeleteObject(hbrPool);
            DeleteObject(hpenPool);

            std::wstring poolStr = L"★ Entire Preset (All Pools)";
            if (curPres)
            {
                size_t numTotalPools = curPres->pools.size();
                bool isAll = (g_State.selectedPoolIndices.empty() || g_State.selectedPoolIndices.size() >= numTotalPools);

                if (isAll)
                {
                    int totalU = 0;
                    for (const auto& pl : curPres->pools) totalU += (int)pl.units.size();
                    poolStr = L"★ Entire Preset: " + curPres->presetName + L" (All " + std::to_wstring(numTotalPools) + L" Pools • " + std::to_wstring(totalU) + L" units)";
                }
                else if (g_State.selectedPoolIndices.size() == 1)
                {
                    int pIdx = g_State.selectedPoolIndices[0];
                    if (pIdx >= 0 && pIdx < (int)numTotalPools)
                    {
                        const auto& pl = curPres->pools[pIdx];
                        std::wstring plName = pl.name.empty() ? (L"Pool #" + std::to_wstring(pIdx + 1)) : pl.name;
                        poolStr = curPres->presetName + L" ➔ Pool " + std::to_wstring(pIdx + 1) + L": " + plName + L" [" + std::to_wstring(pl.units.size()) + L" units]";
                    }
                }
                else
                {
                    std::vector<int> sorted = g_State.selectedPoolIndices;
                    std::sort(sorted.begin(), sorted.end());
                    int totalU = 0;
                    std::wstring poolNames = L"";
                    for (size_t k = 0; k < sorted.size(); ++k)
                    {
                        int pIdx = sorted[k];
                        if (pIdx >= 0 && pIdx < (int)numTotalPools)
                        {
                            totalU += (int)curPres->pools[pIdx].units.size();
                            if (k > 0) poolNames += L", ";
                            poolNames += curPres->pools[pIdx].name.empty() ? (L"Pool #" + std::to_wstring(pIdx + 1)) : curPres->pools[pIdx].name;
                        }
                    }
                    poolStr = std::to_wstring(sorted.size()) + L" of " + std::to_wstring(numTotalPools) + L" Pools Selected: " + poolNames + L" (" + std::to_wstring(totalU) + L" units)";
                }
            }
            SelectObject(hmemDC, g_State.hFontMain);
            SetTextColor(hmemDC, textPrimary);
            RECT rcPoolText = { g_State.rcPoolPicker.left + 10, g_State.rcPoolPicker.top, g_State.rcPoolPicker.right - 30, g_State.rcPoolPicker.bottom };
            DrawTextW(hmemDC, poolStr.c_str(), -1, &rcPoolText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT rcChev2 = { g_State.rcPoolPicker.right - 26, g_State.rcPoolPicker.top, g_State.rcPoolPicker.right - 8, g_State.rcPoolPicker.bottom };
            int c2X = (rcChev2.left + rcChev2.right) / 2;
            int c2Y = (rcChev2.top + rcChev2.bottom) / 2;
            COLORREF c2Col = g_State.isHoverPool ? RGB(255, 255, 255) : textSecondary;
            HBRUSH hBrC2 = CreateSolidBrush(c2Col);
            HPEN hPenC2 = CreatePen(PS_SOLID, 1, c2Col);
            HBRUSH hOldB2C = (HBRUSH)SelectObject(hmemDC, hBrC2);
            HPEN hOldP2C = (HPEN)SelectObject(hmemDC, hPenC2);
            POINT ptsC2[3] = { { c2X - 4, c2Y - 2 }, { c2X + 4, c2Y - 2 }, { c2X, c2Y + 3 } };
            Polygon(hmemDC, ptsC2, 3);
            SelectObject(hmemDC, hOldB2C);
            SelectObject(hmemDC, hOldP2C);
            DeleteObject(hBrC2);
            DeleteObject(hPenC2);

            // Divider before Target context
            MoveToEx(hmemDC, 20, 178, NULL);
            LineTo(hmemDC, rcClient.right - 20, 178);

            // Target Context Summary
            SelectObject(hmemDC, g_State.hFontBold);
            SetTextColor(hmemDC, accentCol);

            std::wstring targetSummary = L"";
            if (g_State.mode == PoolMutator::MutatorMode::MutateConsists)
            {
                targetSummary = L"Target: " + std::to_wstring(g_State.targetConsistPaths.size()) + L" selected consist(s)";
            }
            else if (g_State.mode == PoolMutator::MutatorMode::ReplaceSelected)
            {
                targetSummary = L"Target: " + std::to_wstring(g_State.targetUnitIndices.size()) + L" selected unit(s) in active consist";
            }
            else if (g_State.mode == PoolMutator::MutatorMode::InsertUnits)
            {
                targetSummary = L"Target: " + std::to_wstring(g_State.targetConsistPaths.size()) + L" consist(s) to receive new units";
            }

            RECT rcSumText = { 30, 186, rcClient.right - 30, 206 };
            DrawTextW(hmemDC, targetSummary.c_str(), -1, &rcSumText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // 4. Mode-Specific Controls Rendering
            SelectObject(hmemDC, g_State.hFontMain);
            SetTextColor(hmemDC, textPrimary);

            if (g_State.mode == PoolMutator::MutatorMode::MutateConsists)
            {
                RECT rcSecCount = { 30, 214, 400, 234 };
                SelectObject(hmemDC, g_State.hFontBold);
                DrawTextW(hmemDC, L"Consist Length & Count Generation Rules:", -1, &rcSecCount, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hmemDC, g_State.hFontMain);

                // Radio 0: Keep Original
                g_State.rcRadioCount0 = { 30, 240, 380, 264 };
                bool isSel0 = (g_State.countMode == PoolMutator::CountMode::KeepOriginalCount);
                RECT rcCircle0 = { 34, 245, 48, 259 };
                HBRUSH hbrRad0 = CreateSolidBrush(cardBgCol);
                HPEN hpenRad0 = CreatePen(PS_SOLID, 1, isSel0 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrRad0);
                hOldP = (HPEN)SelectObject(hmemDC, hpenRad0);
                Ellipse(hmemDC, rcCircle0.left, rcCircle0.top, rcCircle0.right, rcCircle0.bottom);
                if (isSel0)
                {
                    HBRUSH hbrDot = CreateSolidBrush(accentCol);
                    SelectObject(hmemDC, hbrDot);
                    Ellipse(hmemDC, rcCircle0.left + 3, rcCircle0.top + 3, rcCircle0.right - 3, rcCircle0.bottom - 3);
                    DeleteObject(hbrDot);
                }
                SelectObject(hmemDC, hOldB);
                SelectObject(hmemDC, hOldP);
                DeleteObject(hbrRad0);
                DeleteObject(hpenRad0);

                RECT rcRadText0 = { 56, 240, 380, 264 };
                DrawTextW(hmemDC, L"Keep Original Unit Count", -1, &rcRadText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Radio 1: Dynamic Rules
                g_State.rcRadioCount1 = { 30, 268, 480, 292 };
                bool isSel1 = (g_State.countMode == PoolMutator::CountMode::DynamicPoolRules);
                RECT rcCircle1 = { 34, 273, 48, 287 };
                HBRUSH hbrRad1 = CreateSolidBrush(cardBgCol);
                HPEN hpenRad1 = CreatePen(PS_SOLID, 1, isSel1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrRad1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenRad1);
                Ellipse(hmemDC, rcCircle1.left, rcCircle1.top, rcCircle1.right, rcCircle1.bottom);
                if (isSel1)
                {
                    HBRUSH hbrDot = CreateSolidBrush(accentCol);
                    SelectObject(hmemDC, hbrDot);
                    Ellipse(hmemDC, rcCircle1.left + 3, rcCircle1.top + 3, rcCircle1.right - 3, rcCircle1.bottom - 3);
                    DeleteObject(hbrDot);
                }
                SelectObject(hmemDC, hOldB);
                SelectObject(hmemDC, hOldP);
                DeleteObject(hbrRad1);
                DeleteObject(hpenRad1);

                RECT rcRadText1 = { 56, 268, 480, 292 };
                DrawTextW(hmemDC, L"Dynamic from Pool Rules (Min / Max)", -1, &rcRadText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Radio 2: Custom Fixed
                g_State.rcRadioCount2 = { 30, 296, 230, 320 };
                bool isSel2 = (g_State.countMode == PoolMutator::CountMode::CustomUnitCount);
                RECT rcCircle2 = { 34, 301, 48, 315 };
                HBRUSH hbrRad2 = CreateSolidBrush(cardBgCol);
                HPEN hpenRad2 = CreatePen(PS_SOLID, 1, isSel2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrRad2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenRad2);
                Ellipse(hmemDC, rcCircle2.left, rcCircle2.top, rcCircle2.right, rcCircle2.bottom);
                if (isSel2)
                {
                    HBRUSH hbrDot = CreateSolidBrush(accentCol);
                    SelectObject(hmemDC, hbrDot);
                    Ellipse(hmemDC, rcCircle2.left + 3, rcCircle2.top + 3, rcCircle2.right - 3, rcCircle2.bottom - 3);
                    DeleteObject(hbrDot);
                }
                SelectObject(hmemDC, hOldB);
                SelectObject(hmemDC, hOldP);
                DeleteObject(hbrRad2);
                DeleteObject(hpenRad2);

                RECT rcRadText2 = { 56, 296, 230, 320 };
                DrawTextW(hmemDC, L"Fixed Target Unit Count:", -1, &rcRadText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Custom count border
                if (isSel2)
                {
                    g_State.rcEditCustomCountBorder = { 235, 295, 295, 321 };
                    HBRUSH hEditBg = CreateSolidBrush(RGB(32, 32, 34));
                    HPEN hEditPen = CreatePen(PS_SOLID, 1, borderCol);
                    HBRUSH holdB = (HBRUSH)SelectObject(hmemDC, hEditBg);
                    HPEN holdEP = (HPEN)SelectObject(hmemDC, hEditPen);
                    RoundRect(hmemDC, 235, 295, 295, 321, 4, 4);
                    SelectObject(hmemDC, holdB);
                    SelectObject(hmemDC, holdEP);
                    DeleteObject(hEditBg);
                    DeleteObject(hEditPen);
                }
            }
            else if (g_State.mode == PoolMutator::MutatorMode::ReplaceSelected)
            {
                RECT rcNote = { 30, 218, rcClient.right - 30, 305 };
                std::wstring note = L"The selected unit(s) in the active consist will be replaced with matching randomized\nor sequential units from the chosen pool preset.\n\nOriginal orientations, flips, and locomotive rules are applied automatically.";
                DrawTextW(hmemDC, note.c_str(), -1, &rcNote, DT_LEFT | DT_TOP | DT_NOPREFIX);
            }
            else if (g_State.mode == PoolMutator::MutatorMode::InsertUnits)
            {
                // Units to Insert Row
                RECT rcLblInsCount = { 30, 214, 145, 238 };
                SelectObject(hmemDC, g_State.hFontBold);
                DrawTextW(hmemDC, L"Units to Insert:", -1, &rcLblInsCount, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Border for insert count edit
                HBRUSH hEditBg = CreateSolidBrush(RGB(32, 32, 34));
                HPEN hEditPen = CreatePen(PS_SOLID, 1, borderCol);
                HBRUSH holdB = (HBRUSH)SelectObject(hmemDC, hEditBg);
                HPEN holdEP = (HPEN)SelectObject(hmemDC, hEditPen);
                RoundRect(hmemDC, 150, 213, 210, 239, 4, 4);
                SelectObject(hmemDC, holdB);
                SelectObject(hmemDC, holdEP);
                DeleteObject(hEditBg);
                DeleteObject(hEditPen);

                // Units to Insert Hint
                RECT rcInsHint = { 220, 214, rcClient.right - 30, 238 };
                SelectObject(hmemDC, g_State.hFontSmall);
                SetTextColor(hmemDC, textSecondary);
                DrawTextW(hmemDC, L"unit(s) per consist", -1, &rcInsHint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Insertion Position Section
                RECT rcSecPos = { 30, 248, 300, 268 };
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                DrawTextW(hmemDC, L"Insertion Position:", -1, &rcSecPos, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hmemDC, g_State.hFontMain);

                // Pos 0: Head (Left col, Row 1)
                g_State.rcRadioPos0 = { 30, 272, 250, 296 };
                bool isP0 = (g_State.posMode == PoolMutator::PositionMode::HeadPosition);
                RECT rcCPos0 = { 34, 277, 48, 291 };
                HBRUSH hbrP0 = CreateSolidBrush(cardBgCol);
                HPEN hpenP0 = CreatePen(PS_SOLID, 1, isP0 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP0);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP0);
                Ellipse(hmemDC, rcCPos0.left, rcCPos0.top, rcCPos0.right, rcCPos0.bottom);
                if (isP0) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos0.left + 3, rcCPos0.top + 3, rcCPos0.right - 3, rcCPos0.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP0); DeleteObject(hpenP0);
                RECT rcPText0 = { 56, 272, 250, 296 };
                DrawTextW(hmemDC, L"Head (Position 1)", -1, &rcPText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 1: Behind Engines (Right col, Row 1)
                g_State.rcRadioPos1 = { 260, 272, 530, 296 };
                bool isP1 = (g_State.posMode == PoolMutator::PositionMode::BehindEngines);
                RECT rcCPos1 = { 264, 277, 278, 291 };
                HBRUSH hbrP1 = CreateSolidBrush(cardBgCol);
                HPEN hpenP1 = CreatePen(PS_SOLID, 1, isP1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP1);
                Ellipse(hmemDC, rcCPos1.left, rcCPos1.top, rcCPos1.right, rcCPos1.bottom);
                if (isP1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos1.left + 3, rcCPos1.top + 3, rcCPos1.right - 3, rcCPos1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP1); DeleteObject(hpenP1);
                RECT rcPText1 = { 286, 272, 530, 296 };
                DrawTextW(hmemDC, L"Behind Lead Locomotives", -1, &rcPText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 3: Tail (Left col, Row 2)
                g_State.rcRadioPos3 = { 30, 300, 250, 324 };
                bool isP3 = (g_State.posMode == PoolMutator::PositionMode::TailPosition);
                RECT rcCPos3 = { 34, 305, 48, 319 };
                HBRUSH hbrP3 = CreateSolidBrush(cardBgCol);
                HPEN hpenP3 = CreatePen(PS_SOLID, 1, isP3 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP3);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP3);
                Ellipse(hmemDC, rcCPos3.left, rcCPos3.top, rcCPos3.right, rcCPos3.bottom);
                if (isP3) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos3.left + 3, rcCPos3.top + 3, rcCPos3.right - 3, rcCPos3.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP3); DeleteObject(hpenP3);
                RECT rcPText3 = { 56, 300, 250, 324 };
                DrawTextW(hmemDC, L"Tail (End of Consist)", -1, &rcPText3, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 2: Specific Index (Right col, Row 2)
                g_State.rcRadioPos2 = { 260, 300, 420, 324 };
                bool isP2 = (g_State.posMode == PoolMutator::PositionMode::SpecificIndex);
                RECT rcCPos2 = { 264, 305, 278, 319 };
                HBRUSH hbrP2 = CreateSolidBrush(cardBgCol);
                HPEN hpenP2 = CreatePen(PS_SOLID, 1, isP2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP2);
                Ellipse(hmemDC, rcCPos2.left, rcCPos2.top, rcCPos2.right, rcCPos2.bottom);
                if (isP2) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos2.left + 3, rcCPos2.top + 3, rcCPos2.right - 3, rcCPos2.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP2); DeleteObject(hpenP2);
                RECT rcPText2 = { 286, 300, 420, 324 };
                DrawTextW(hmemDC, L"At Specific Index:", -1, &rcPText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (isP2)
                {
                    HBRUSH hPosBg = CreateSolidBrush(RGB(32, 32, 34));
                    HPEN hPosPen = CreatePen(PS_SOLID, 1, borderCol);
                    HBRUSH holdBPos = (HBRUSH)SelectObject(hmemDC, hPosBg);
                    HPEN holdPosP = (HPEN)SelectObject(hmemDC, hPosPen);
                    RoundRect(hmemDC, 424, 299, 484, 325, 4, 4);
                    SelectObject(hmemDC, holdBPos);
                    SelectObject(hmemDC, holdPosP);
                    DeleteObject(hPosBg);
                    DeleteObject(hPosPen);
                }
            }

            // 5. Checkbox: Create Clones
            g_State.rcCheckboxClones = { 30, 338, rcClient.right - 30, 362 };
            RECT rcChkBox = { 34, 342, 50, 358 };
            HBRUSH hbrChk = CreateSolidBrush(g_State.createClones ? accentCol : cardBgCol);
            HPEN hpenChk = CreatePen(PS_SOLID, 1, g_State.createClones ? accentCol : borderCol);
            hOldB = (HBRUSH)SelectObject(hmemDC, hbrChk);
            hOldP = (HPEN)SelectObject(hmemDC, hpenChk);
            RoundRect(hmemDC, rcChkBox.left, rcChkBox.top, rcChkBox.right, rcChkBox.bottom, 4, 4);
            SelectObject(hmemDC, hOldB);
            SelectObject(hmemDC, hOldP);
            DeleteObject(hbrChk);
            DeleteObject(hpenChk);

            if (g_State.createClones)
            {
                SelectObject(hmemDC, g_State.hFontIcon);
                SetTextColor(hmemDC, RGB(255, 255, 255));
                RECT rcChkGlyph = { rcChkBox.left, rcChkBox.top - 1, rcChkBox.right, rcChkBox.bottom };
                DrawTextW(hmemDC, L"\xE73E", -1, &rcChkGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            SelectObject(hmemDC, g_State.hFontMain);
            SetTextColor(hmemDC, textPrimary);
            RECT rcChkText = { 58, 338, rcClient.right - 30, 362 };
            DrawTextW(hmemDC, L"Create cloned variation files (Keep originals unmodified)", -1, &rcChkText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Suffix Label & Border
            RECT rcLblSuf = { 30, 370, 120, 394 };
            SetTextColor(hmemDC, textSecondary);
            DrawTextW(hmemDC, L"Clone Suffix:", -1, &rcLblSuf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            HBRUSH hSufBg = CreateSolidBrush(RGB(32, 32, 34));
            HPEN hSufPen = CreatePen(PS_SOLID, 1, borderCol);
            HBRUSH holdBSuf = (HBRUSH)SelectObject(hmemDC, hSufBg);
            HPEN holdSufP = (HPEN)SelectObject(hmemDC, hSufPen);
            RoundRect(hmemDC, 124, 369, 286, 395, 4, 4);
            SelectObject(hmemDC, holdBSuf);
            SelectObject(hmemDC, holdSufP);
            DeleteObject(hSufBg);
            DeleteObject(hSufPen);

            // Bottom Action Bar Divider
            MoveToEx(hmemDC, 0, rcClient.bottom - 54, NULL);
            LineTo(hmemDC, rcClient.right, rcClient.bottom - 54);

            // 6. Action Button: [Apply Mutation] (Accent)
            const wchar_t* applyText = L"Apply Mutation";
            if (g_State.mode == PoolMutator::MutatorMode::ReplaceSelected) applyText = L"Replace Selected";
            else if (g_State.mode == PoolMutator::MutatorMode::InsertUnits) applyText = L"Insert Units";

            g_State.rcApplyBtn = { rcClient.right - 180, rcClient.bottom - 44, rcClient.right - 20, rcClient.bottom - 12 };
            DrawModernButton(hmemDC, g_State.rcApplyBtn, applyText, g_State.isHoverApply, false, true, g_State.hFontBold, g_State.hFontIcon, L"\xE73E");

            // Perimeter Border
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
            SelectObject(hmemDC, hPenBorder);
            SelectObject(hmemDC, GetStockObject(NULL_BRUSH));
            Rectangle(hmemDC, 0, 0, rcClient.right, rcClient.bottom);
            DeleteObject(hPenBorder);

            SelectObject(hmemDC, holdPen);
            DeleteObject(hPenLine);

            BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hmemDC, 0, 0, SRCCOPY);

            SelectObject(hmemDC, holdBm);
            DeleteObject(hbm);
            DeleteDC(hmemDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
        {
            if (g_State.hFontTitle) DeleteObject(g_State.hFontTitle);
            if (g_State.hFontMain) DeleteObject(g_State.hFontMain);
            if (g_State.hFontBold) DeleteObject(g_State.hFontBold);
            if (g_State.hFontSmall) DeleteObject(g_State.hFontSmall);
            if (g_State.hFontIcon) DeleteObject(g_State.hFontIcon);
            if (g_State.hFontIconLg) DeleteObject(g_State.hFontIconLg);

            g_hPoolMutatorDlg = NULL;
            return 0;
        }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

void ShowPoolMutatorDialog(
    HWND hWndParent,
    PoolMutator::MutatorMode initialMode,
    const std::vector<std::wstring>& targetConsistFilePaths,
    const std::vector<int>& targetSelectedUnitIndices)
{
    if (g_hPoolMutatorDlg && IsWindow(g_hPoolMutatorDlg))
    {
        SetForegroundWindow(g_hPoolMutatorDlg);
        return;
    }

    PoolManager::InitializePoolPresets();

    g_State = MutatorDlgState();
    g_State.hParent = hWndParent;
    g_State.mode = initialMode;
    g_State.targetConsistPaths = targetConsistFilePaths;
    g_State.targetUnitIndices = targetSelectedUnitIndices;

    const wchar_t* szClassName = L"PoolMutatorDlgClass";
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = PoolMutatorDlgProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(26, 26, 28));
    wcex.lpszClassName = szClassName;

    RegisterClassExW(&wcex);

    int dlgW = 580;
    int dlgH = 480;

    RECT rcParent = { 0 };
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int x = rcParent.left + (rcParent.right - rcParent.left - dlgW) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - dlgH) / 2;
    if (x < 10) x = 10;
    if (y < 10) y = 10;

    HWND hDlg = CreateWindowExW(
        WS_EX_APPWINDOW, szClassName, L"Consist Pool Mutator & Injector",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME,
        x, y, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandle(NULL), NULL
    );

    if (!hDlg) return;

    g_hPoolMutatorDlg = hDlg;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = RGB(55, 55, 62);
    DwmSetWindowAttribute(hDlg, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hDlg, &margins);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
}
