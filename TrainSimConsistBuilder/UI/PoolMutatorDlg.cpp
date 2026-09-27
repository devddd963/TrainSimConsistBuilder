#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PoolMutatorDlg.h"
#include "PoolManagerDlg.h"
#include "CustomTitleBar.h"
#include "UITheme.h"
#include "ModernMessageBox.h"
#include "ModernContextMenu.h"
#include "CustomDropDownMenu.h"
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
            int iconW = iconSz.cx > 0 ? iconSz.cx : 12;

            SelectObject(hdc, hFont);
            SIZE textSz = { 0 };
            GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSz);
            int textW = textSz.cx;

            int gap = 6;
            int totalW = iconW + gap + textW;
            int minMargin = 6;

            int startX = rc.left + (rc.right - rc.left - totalW) / 2;
            if (startX < rc.left + minMargin)
            {
                startX = rc.left + minMargin;
            }

            SelectObject(hdc, hIconFont);
            RECT rcIcon = { startX, rc.top, startX + iconW, rc.bottom };
            DrawTextW(hdc, iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hdc, hFont);
            RECT rcText = { startX + iconW + gap, rc.top, rc.right - minMargin, rc.bottom };
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
        HWND hTitleBar = NULL;

        int activeTab = 0; // 0 = Mutate Consist, 1 = Insert Units
        PoolMutator::MutatorMode mode = PoolMutator::MutatorMode::MutateConsists;
        PoolMutator::InsertSource insertSource = PoolMutator::InsertSource::FavouriteGroup;

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
        int selectedGroupIdx = 0;
        std::vector<int> selectedPoolIndices; // Tab 0: subset of pools in preset. Empty = Entire Preset
        std::vector<int> selectedGroupIndices; // Tab 1: subset of favourite groups. Empty = All groups
        std::vector<int> selectedPresetIndices; // Tab 1: subset of presets. Empty = All presets
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        int customCount = 20;
        int insertCount = 2;
        int positionIndex = 0;
        std::wstring positionIndexStr = L"1";

        // Interactive Rectangles
        RECT rcPresetPicker = { 0 };
        RECT rcPoolPicker = { 0 };
        RECT rcGroupPicker = { 0 };
        RECT rcRadioInsertSrc0 = { 0 }; // Favourite Group Radio
        RECT rcRadioInsertSrc1 = { 0 }; // Pool Preset Radio
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
        bool isHoverPreset = false;
        bool isHoverPool = false;
        bool isHoverGroup = false;
        bool isHoverRadioSrc0 = false;
        bool isHoverRadioSrc1 = false;
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
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        bool isTab0 = (g_State.activeTab == 0);
        bool isTab1 = (g_State.activeTab == 1);

        // Custom count edit box (Tab 0: Mutate Consist mode)
        if (g_State.hEditCustomCount)
        {
            if (isTab0 && g_State.countMode == PoolMutator::CountMode::CustomUnitCount)
            {
                SetWindowPos(g_State.hEditCustomCount, NULL, 238, 301, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditCustomCount, SW_HIDE);
            }
        }

        // Insert count edit box (Tab 1: Common to both sources)
        if (g_State.hEditInsertCount)
        {
            if (isTab1)
            {
                SetWindowPos(g_State.hEditInsertCount, NULL, 148, 222, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditInsertCount, SW_HIDE);
            }
        }

        // Position index edit box (Tab 1: Specific Index)
        if (g_State.hEditPositionIndex)
        {
            if (isTab1 && g_State.posMode == PoolMutator::PositionMode::SpecificIndex)
            {
                SetWindowPos(g_State.hEditPositionIndex, NULL, 472, 332, 120, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_State.hEditPositionIndex, SW_HIDE);
            }
        }

        // Clone suffix edit box
        if (g_State.hEditCloneSuffix)
        {
            SetWindowPos(g_State.hEditCloneSuffix, NULL, 128, h - 89, 154, 18, SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        InvalidateRect(hWnd, NULL, TRUE);
    }

    static void ShowGroupDropdown(HWND hWnd)
    {
        PoolManager::InitializeReplacementGroups();
        if (PoolManager::g_ReplacementGroupsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Favourite Unit Groups created yet. Open Pool Manager (Tab 2) to create group palettes first.", L"Favourite Groups", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalGroups = PoolManager::g_ReplacementGroupsCache.size();
        bool isAllSelected = (totalGroups > 0 && (g_State.selectedGroupIndices.empty() || g_State.selectedGroupIndices.size() >= totalGroups));
        bool hasAnySelected = (!g_State.selectedGroupIndices.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Favourite Palette (All Groups)", L"", isAllSelected, hasAnySelected));

        for (size_t i = 0; i < totalGroups; ++i)
        {
            const auto& grp = PoolManager::g_ReplacementGroupsCache[i];
            std::wstring label = grp.name.empty() ? (L"Group #" + std::to_wstring(i + 1)) : grp.name;
            std::wstring tag = std::to_wstring(grp.units.size()) + L" units";
            bool isChecked = isAllSelected || (std::find(g_State.selectedGroupIndices.begin(), g_State.selectedGroupIndices.end(), (int)i) != g_State.selectedGroupIndices.end());
            items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcGroupPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedGroupIndices.clear();
            for (size_t k = 1; k < updatedItems.size(); ++k)
            {
                if (updatedItems[k].isChecked)
                {
                    g_State.selectedGroupIndices.push_back((int)k - 1);
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
        });
    }

    static void ShowPresetMultiDropdown(HWND hWnd)
    {
        PoolManager::InitializePoolPresets();
        if (PoolManager::g_PoolPresetsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Pool Presets available.", L"Pool Presets", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalPresets = PoolManager::g_PoolPresetsCache.size();
        bool isAllSelected = (totalPresets > 0 && (g_State.selectedPresetIndices.empty() || g_State.selectedPresetIndices.size() >= totalPresets));
        bool hasAnySelected = (!g_State.selectedPresetIndices.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Preset Library (All Presets)", L"", isAllSelected, hasAnySelected));

        for (size_t i = 0; i < totalPresets; ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
            bool isChecked = isAllSelected || (std::find(g_State.selectedPresetIndices.begin(), g_State.selectedPresetIndices.end(), (int)i) != g_State.selectedPresetIndices.end());
            items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcGroupPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedPresetIndices.clear();
            for (size_t k = 1; k < updatedItems.size(); ++k)
            {
                if (updatedItems[k].isChecked)
                {
                    g_State.selectedPresetIndices.push_back((int)k - 1);
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
        });
    }

    static void ShowPresetDropdown(HWND hWnd)
    {
        if (PoolManager::g_PoolPresetsCache.empty()) return;

        std::vector<DropDownItem> items;
        for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
            bool isCurrent = ((int)i == g_State.selectedPresetIdx);
            items.push_back(DropDownItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, isCurrent, true));
        }

        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, g_State.rcPresetPicker, items, g_State.selectedPresetIdx + 1);
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

    static void ShowPoolDropdown(HWND hWnd)
    {
        PoolManager::PoolPreset* pPreset = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
        if (!pPreset || pPreset->pools.empty()) return;

        size_t numPools = pPreset->pools.size();
        bool isAllSelected = (numPools > 0 && g_State.selectedPoolIndices.size() == numPools);
        bool hasAnySelected = (!g_State.selectedPoolIndices.empty());
        bool isIndeterminate = (hasAnySelected && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Preset (All Pools)", L"", isAllSelected, isIndeterminate));

        for (size_t i = 0; i < numPools; ++i)
        {
            const auto& pl = pPreset->pools[i];
            std::wstring label = std::to_wstring(i + 1) + L". " + (pl.name.empty() ? L"Pool #" + std::to_wstring(i + 1) : pl.name);
            std::wstring modeStr = (pl.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
            std::wstring tag = std::to_wstring(pl.units.size()) + L" units • [" + modeStr + L", Min:" + std::to_wstring(pl.minCount) + L", Max:" + std::to_wstring(pl.maxCount) + L"]";
            bool isChecked = (std::find(g_State.selectedPoolIndices.begin(), g_State.selectedPoolIndices.end(), (int)i) != g_State.selectedPoolIndices.end());
            items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcPoolPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedPoolIndices.clear();
            for (size_t k = 1; k < updatedItems.size(); ++k)
            {
                if (updatedItems[k].isChecked)
                {
                    g_State.selectedPoolIndices.push_back((int)k - 1);
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
        });
    }

    static std::vector<int> ParseIndices(const std::wstring& text)
    {
        std::vector<int> indices;
        std::wstring cur = L"";
        for (wchar_t ch : text)
        {
            if (ch == L';' || ch == L',' || ch == L' ' || ch == L'\t')
            {
                if (!cur.empty())
                {
                    int val = _wtoi(cur.c_str());
                    if (val > 0) indices.push_back(val - 1);
                    cur.clear();
                }
            }
            else if (iswdigit(ch))
            {
                cur += ch;
            }
        }
        if (!cur.empty())
        {
            int val = _wtoi(cur.c_str());
            if (val > 0) indices.push_back(val - 1);
        }
        return indices;
    }

    static void RestoreParentWindowFocus(HWND hParent)
    {
        if (hParent && IsWindow(hParent))
        {
            EnableWindow(hParent, TRUE);
            if (IsIconic(hParent))
            {
                ShowWindow(hParent, SW_RESTORE);
            }
            SetWindowPos(hParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(hParent);
            SetActiveWindow(hParent);
            BringWindowToTop(hParent);
            SetFocus(hParent);
        }
    }

    static void SaveCurrentDialogState()
    {
        PoolMutator::MutatorSavedSettings s;
        s.activeTab = g_State.activeTab;
        s.selectedPresetIdx = g_State.selectedPresetIdx;
        s.selectedPoolIndices = g_State.selectedPoolIndices;
        s.selectedPresetIndices = g_State.selectedPresetIndices;
        s.countMode = (int)g_State.countMode;

        wchar_t buf[64] = { 0 };
        if (g_State.hEditCustomCount && IsWindow(g_State.hEditCustomCount))
        {
            GetWindowTextW(g_State.hEditCustomCount, buf, 64);
            s.customCount = _wtoi(buf);
            if (s.customCount <= 0) s.customCount = 20;
        }
        else
        {
            s.customCount = g_State.customCount;
        }

        s.createClones = g_State.createClones;
        if (g_State.hEditCloneSuffix && IsWindow(g_State.hEditCloneSuffix))
        {
            wchar_t sbuf[128] = { 0 };
            GetWindowTextW(g_State.hEditCloneSuffix, sbuf, 128);
            s.cloneSuffix = sbuf;
        }
        else
        {
            s.cloneSuffix = g_State.cloneSuffix;
        }

        s.insertSource = (int)g_State.insertSource;
        s.selectedGroupIdx = g_State.selectedGroupIdx;
        s.selectedGroupIndices = g_State.selectedGroupIndices;

        if (g_State.hEditInsertCount && IsWindow(g_State.hEditInsertCount))
        {
            GetWindowTextW(g_State.hEditInsertCount, buf, 64);
            s.insertCount = _wtoi(buf);
            if (s.insertCount <= 0) s.insertCount = 2;
        }
        else
        {
            s.insertCount = g_State.insertCount;
        }

        s.posMode = (int)g_State.posMode;
        if (g_State.hEditPositionIndex && IsWindow(g_State.hEditPositionIndex))
        {
            wchar_t posBuf[128] = { 0 };
            GetWindowTextW(g_State.hEditPositionIndex, posBuf, 128);
            s.positionIndexText = posBuf;
        }
        else
        {
            s.positionIndexText = g_State.positionIndexStr.empty() ? L"1" : g_State.positionIndexStr;
        }

        PoolMutator::SaveMutationSettings(s);
    }

    static void ExecuteMutation(HWND hWnd)
    {
        PoolMutator::MutatorOptions opts;
        opts.mode = (g_State.activeTab == 0) ? PoolMutator::MutatorMode::MutateConsists : PoolMutator::MutatorMode::InsertUnits;
        opts.presetIndex = g_State.selectedPresetIdx;
        opts.selectedPoolIndices = g_State.selectedPoolIndices;
        opts.selectedPresetIndices = g_State.selectedPresetIndices;
        opts.selectedReplacementGroupIndices = g_State.selectedGroupIndices;
        opts.insertSource = g_State.insertSource;
        opts.replacementGroupIndex = g_State.selectedGroupIdx;

        PoolManager::PoolPreset* pPres = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
        if (pPres && opts.IsAllPoolsSelected(pPres->pools.size()))
        {
            opts.selectedPoolIndices.clear();
        }

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
        int parsedInsCount = _wtoi(buf);
        if (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup)
        {
            opts.insertCount = (parsedInsCount > 0) ? parsedInsCount : 2;
        }
        else
        {
            opts.insertCount = (parsedInsCount > 0) ? parsedInsCount : 0;
        }

        wchar_t posBuf[128] = { 0 };
        GetWindowTextW(g_State.hEditPositionIndex, posBuf, 128);
        opts.specificIndices = ParseIndices(posBuf);
        if (!opts.specificIndices.empty())
        {
            opts.positionIndex = opts.specificIndices[0];
        }
        else
        {
            opts.positionIndex = 0;
        }

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
            SaveCurrentDialogState();

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
            InvalidateRect(hWnd, NULL, TRUE);
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
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
            HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            if (hMonitor)
            {
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(hMonitor, &mi))
                {
                    pMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    pMMI->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    pMMI->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    pMMI->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
            }
            return 0;
        }

        case WM_NCCALCSIZE:
        {
            if (wParam)
            {
                NCCALCSIZE_PARAMS* pParams = (NCCALCSIZE_PARAMS*)lParam;
                if (IsZoomed(hWnd))
                {
                    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                    if (hMonitor)
                    {
                        MONITORINFO mi = { sizeof(mi) };
                        if (GetMonitorInfoW(hMonitor, &mi))
                        {
                            pParams->rgrc[0] = mi.rcWork;
                        }
                    }
                }
                return 0;
            }
            break;
        }

        case WM_NCPAINT:
            return 0;

        case WM_NCACTIVATE:
            return TRUE;

        case WM_NCHITTEST:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            if (!IsZoomed(hWnd))
            {
                int b = 6;
                if (pt.y < b && pt.x < b) return HTTOPLEFT;
                if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
                if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
                if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
                if (pt.y < b) return HTTOP;
                if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
                if (pt.x < b) return HTLEFT;
                if (pt.x >= rcClient.right - b) return HTRIGHT;
            }

            if (pt.y >= 0 && pt.y < 66)
            {
                if (g_State.hTitleBar && IsWindow(g_State.hTitleBar))
                {
                    LRESULT hit = SendMessageW(g_State.hTitleBar, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                    if (hit == HTTRANSPARENT)
                    {
                        return HTCAPTION;
                    }
                }
            }

            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }

        case WM_TITLEBAR_TABCHANGED:
        {
            g_State.activeTab = (int)wParam;
            if (g_State.activeTab == 0)
            {
                g_State.mode = PoolMutator::MutatorMode::MutateConsists;
            }
            else
            {
                g_State.mode = PoolMutator::MutatorMode::InsertUnits;
            }
            UpdateControlPositions(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (g_State.hTitleBar && IsWindow(g_State.hTitleBar))
            {
                SetWindowPos(g_State.hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                InvalidateRect(g_State.hTitleBar, NULL, TRUE);
            }
            UpdateControlPositions(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            return TRUE;

        case WM_KILLFOCUS:
        case WM_CAPTURECHANGED:
        {
            g_State.isHoverApply = false;
            g_State.isHoverCancel = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_SYSCOMMAND:
        {
            if ((wParam & 0xFFF0) == SC_CLOSE)
            {
                RestoreParentWindowFocus(g_State.hParent ? g_State.hParent : GetWindow(hWnd, GW_OWNER));
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE)
            {
                RestoreParentWindowFocus(g_State.hParent ? g_State.hParent : GetWindow(hWnd, GW_OWNER));
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

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int w = rcClient.right > 0 ? rcClient.right : 600;

            std::vector<TitleBarTabItem> mutatorTabs = {
                { L"\xE7B8", L"Mutate Consist" },
                { L"\xE710", L"Insert Units" }
            };

            g_State.hTitleBar = CreateCustomTitleBarEx(
                hWnd,
                GetModuleHandleW(NULL),
                0, 0, w, 66,
                20001,
                L"Consist Pool Mutator - TrainSim Consist Builder",
                mutatorTabs
            );

            if (g_State.hTitleBar)
            {
                CustomTitleBar_SetDarkMode(g_State.hTitleBar, TRUE);
                CustomTitleBar_SetActiveTab(g_State.hTitleBar, g_State.activeTab);
            }

            // Custom Count Edit Box (Tab 0)
            std::wstring customCntStr = std::to_wstring(g_State.customCount > 0 ? g_State.customCount : 20);
            g_State.hEditCustomCount = CreateWindowExW(0, L"EDIT", customCntStr.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                238, 301, 54, 18, hWnd, (HMENU)201, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditCustomCount, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditCustomCount, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

            // Insert Count Edit Box (Tab 1: Common to both sources)
            std::wstring insertCntStr = std::to_wstring(g_State.insertCount > 0 ? g_State.insertCount : 2);
            g_State.hEditInsertCount = CreateWindowExW(0, L"EDIT", insertCntStr.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                148, 222, 54, 18, hWnd, (HMENU)202, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditInsertCount, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditInsertCount, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

            // Position Index Edit Box (Tab 1: Specific Index)
            std::wstring posIndexStr = g_State.positionIndexStr.empty() ? L"1" : g_State.positionIndexStr;
            g_State.hEditPositionIndex = CreateWindowExW(0, L"EDIT", posIndexStr.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                472, 332, 120, 18, hWnd, (HMENU)203, GetModuleHandle(NULL), NULL);
            SendMessage(g_State.hEditPositionIndex, WM_SETFONT, (WPARAM)g_State.hFontMain, TRUE);
            SendMessage(g_State.hEditPositionIndex, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

            // Clone Suffix Edit Box
            std::wstring cloneSufStr = g_State.cloneSuffix.empty() ? L"_PoolVar" : g_State.cloneSuffix;
            g_State.hEditCloneSuffix = CreateWindowExW(0, L"EDIT", cloneSufStr.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                128, 411, 154, 18, hWnd, (HMENU)204, GetModuleHandle(NULL), NULL);
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

            if (g_State.selectedPoolIndices.empty())
            {
                PoolManager::PoolPreset* pInitPres = PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
                if (pInitPres)
                {
                    for (size_t k = 0; k < pInitPres->pools.size(); ++k)
                    {
                        g_State.selectedPoolIndices.push_back((int)k);
                    }
                }
            }

            PoolManager::InitializeReplacementGroups();
            if (g_State.selectedGroupIndices.empty())
            {
                for (size_t k = 0; k < PoolManager::g_ReplacementGroupsCache.size(); ++k)
                {
                    g_State.selectedGroupIndices.push_back((int)k);
                }
            }

            PoolManager::InitializePoolPresets();
            if (g_State.selectedPresetIndices.empty())
            {
                for (size_t k = 0; k < PoolManager::g_PoolPresetsCache.size(); ++k)
                {
                    g_State.selectedPresetIndices.push_back((int)k);
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

            bool hPreset = PtInRect(&g_State.rcPresetPicker, pt);
            bool hPool = PtInRect(&g_State.rcPoolPicker, pt);
            bool hGroup = PtInRect(&g_State.rcGroupPicker, pt);
            bool hRadSrc0 = PtInRect(&g_State.rcRadioInsertSrc0, pt);
            bool hRadSrc1 = PtInRect(&g_State.rcRadioInsertSrc1, pt);
            bool hRadC0 = PtInRect(&g_State.rcRadioCount0, pt);
            bool hRadC1 = PtInRect(&g_State.rcRadioCount1, pt);
            bool hRadC2 = PtInRect(&g_State.rcRadioCount2, pt);
            bool hRadP0 = PtInRect(&g_State.rcRadioPos0, pt);
            bool hRadP1 = PtInRect(&g_State.rcRadioPos1, pt);
            bool hRadP2 = PtInRect(&g_State.rcRadioPos2, pt);
            bool hRadP3 = PtInRect(&g_State.rcRadioPos3, pt);
            bool hCheck = PtInRect(&g_State.rcCheckboxClones, pt);
            bool hApply = PtInRect(&g_State.rcApplyBtn, pt);
            bool hCancel = PtInRect(&g_State.rcCancelBtn, pt);

            if (hPreset != g_State.isHoverPreset || hPool != g_State.isHoverPool ||
                hGroup != g_State.isHoverGroup || hRadSrc0 != g_State.isHoverRadioSrc0 ||
                hRadSrc1 != g_State.isHoverRadioSrc1 ||
                hRadC0 != g_State.isHoverRadioCount0 || hRadC1 != g_State.isHoverRadioCount1 ||
                hRadC2 != g_State.isHoverRadioCount2 || hRadP0 != g_State.isHoverRadioPos0 ||
                hRadP1 != g_State.isHoverRadioPos1 || hRadP2 != g_State.isHoverRadioPos2 ||
                hRadP3 != g_State.isHoverRadioPos3 || hCheck != g_State.isHoverCheckClones ||
                hApply != g_State.isHoverApply || hCancel != g_State.isHoverCancel)
            {
                g_State.isHoverPreset = hPreset;
                g_State.isHoverPool = hPool;
                g_State.isHoverGroup = hGroup;
                g_State.isHoverRadioSrc0 = hRadSrc0;
                g_State.isHoverRadioSrc1 = hRadSrc1;
                g_State.isHoverRadioCount0 = hRadC0;
                g_State.isHoverRadioCount1 = hRadC1;
                g_State.isHoverRadioCount2 = hRadC2;
                g_State.isHoverRadioPos0 = hRadP0;
                g_State.isHoverRadioPos1 = hRadP1;
                g_State.isHoverRadioPos2 = hRadP2;
                g_State.isHoverRadioPos3 = hRadP3;
                g_State.isHoverCheckClones = hCheck;
                g_State.isHoverApply = hApply;
                g_State.isHoverCancel = hCancel;
                InvalidateRect(hWnd, NULL, FALSE);
            }

            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            g_State.isHoverPreset = false;
            g_State.isHoverPool = false;
            g_State.isHoverGroup = false;
            g_State.isHoverRadioSrc0 = false;
            g_State.isHoverRadioSrc1 = false;
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
            if (PtInRect(&g_State.rcGroupPicker, pt))
            {
                if (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup)
                {
                    ShowGroupDropdown(hWnd);
                }
                else
                {
                    ShowPresetMultiDropdown(hWnd);
                }
                return 0;
            }

            // Tab 1: Insert Source Radios
            if (g_State.activeTab == 1)
            {
                if (PtInRect(&g_State.rcRadioInsertSrc0, pt))
                {
                    g_State.insertSource = PoolMutator::InsertSource::FavouriteGroup;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcRadioInsertSrc1, pt))
                {
                    g_State.insertSource = PoolMutator::InsertSource::PoolPreset;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
            }

            // Tab 0: Count Mode Radios
            if (g_State.activeTab == 0)
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

            // Tab 1: Position Mode Radios
            if (g_State.activeTab == 1)
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

            if (PtInRect(&g_State.rcCancelBtn, pt))
            {
                RestoreParentWindowFocus(g_State.hParent ? g_State.hParent : GetWindow(hWnd, GW_OWNER));
                DestroyWindow(hWnd);
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

            COLORREF bgCol = PoolTheme::GutterBackground;
            COLORREF borderCol = PoolTheme::BorderLine;
            COLORREF cardBgCol = RGB(32, 32, 36);
            COLORREF textPrimary = PoolTheme::TextPrimary;
            COLORREF textSecondary = PoolTheme::TextSecondary;
            COLORREF accentCol = PoolTheme::AccentBlue;

            HBRUSH hbrBg = CreateSolidBrush(bgCol);
            FillRect(hmemDC, &rcClient, hbrBg);
            DeleteObject(hbrBg);

            HBRUSH hOldB = NULL;
            HPEN hOldP = NULL;

            // 1. Toolbar Ribbon (Y = 66 to 112)
            int toolbarY = 66;
            int toolbarH = 46;
            RECT rcToolbar = { 0, toolbarY, rcClient.right, toolbarY + toolbarH };
            HBRUSH hbrTb = CreateSolidBrush(PoolTheme::ToolbarBackground);
            FillRect(hmemDC, &rcToolbar, hbrTb);
            DeleteObject(hbrTb);

            HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            HPEN holdPen = (HPEN)SelectObject(hmemDC, hPenLine);
            MoveToEx(hmemDC, 0, toolbarY + toolbarH, NULL);
            LineTo(hmemDC, rcClient.right, toolbarY + toolbarH);

            SelectObject(hmemDC, g_State.hFontIconLg);
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, accentCol);
            RECT rcIcon = { 18, toolbarY, 44, toolbarY + toolbarH };
            DrawTextW(hmemDC, (g_State.activeTab == 0) ? L"\xE7B8" : L"\xE710", -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            std::wstring targetSummary = L"";
            size_t numConsists = g_State.targetConsistPaths.size();
            size_t numUnits = g_State.targetUnitIndices.size();

            if (g_State.activeTab == 0)
            {
                if (numConsists == 0)
                {
                    targetSummary = L"Target: No consist(s) selected";
                }
                else
                {
                    targetSummary = L"Target: " + std::to_wstring(numConsists) + L" selected consist(s)";
                }
            }
            else
            {
                if (numConsists > 1)
                {
                    targetSummary = L"Target: " + std::to_wstring(numConsists) + L" consist(s) to receive units";
                }
                else if (numConsists == 1)
                {
                    if (numUnits > 0)
                    {
                        targetSummary = L"Target: Active Consist (" + std::to_wstring(numUnits) + L" selected unit(s))";
                    }
                    else
                    {
                        targetSummary = L"Target: 1 consist to receive units";
                    }
                }
                else
                {
                    targetSummary = L"Target: No consist(s) selected";
                }
            }

            int statusW = 340;
            SelectObject(hmemDC, g_State.hFontBold);
            SetTextColor(hmemDC, textPrimary);
            RECT rcTitle = { 46, toolbarY + 5, rcClient.right - statusW - 20, toolbarY + 24 };
            const wchar_t* tabTitle = (g_State.activeTab == 0) ? L"Whole Consist Pool Overhaul / Mutation" : L"Position-Based Unit Injection";
            DrawTextW(hmemDC, tabTitle, -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hmemDC, g_State.hFontSmall);
            SetTextColor(hmemDC, textSecondary);
            RECT rcSub = { 46, toolbarY + 24, rcClient.right - statusW - 20, toolbarY + 42 };
            const wchar_t* tabSub = (g_State.activeTab == 0) ? L"Regenerate and vary entire consist file(s) drawn from selected Pool Preset" : L"Inject new units into consist file(s) at Head, Behind Locos, Tail, or Specific Index";
            DrawTextW(hmemDC, tabSub, -1, &rcSub, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

            // Right-aligned status: Target context summary
            SelectObject(hmemDC, g_State.hFontBold);
            SetTextColor(hmemDC, accentCol);
            RECT rcStatus = { rcClient.right - statusW - 20, toolbarY, rcClient.right - 20, toolbarY + toolbarH };
            DrawTextW(hmemDC, targetSummary.c_str(), -1, &rcStatus, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // =========================================================================
            // TAB 0: MUTATE CONSIST
            // =========================================================================
            if (g_State.activeTab == 0)
            {
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);

                RECT rcLblPreset = { 30, 130, 130, 158 };
                DrawTextW(hmemDC, L"Pool Preset:", -1, &rcLblPreset, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                RECT rcLblPool = { 30, 168, 130, 196 };
                DrawTextW(hmemDC, L"Source Pool:", -1, &rcLblPool, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Preset Dropdown Box
                g_State.rcPresetPicker = { 135, 130, rcClient.right - 30, 158 };
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
                g_State.rcPoolPicker = { 135, 168, rcClient.right - 30, 196 };
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

                // Section Divider
                MoveToEx(hmemDC, 30, 214, NULL);
                LineTo(hmemDC, rcClient.right - 30, 214);

                // Unit Count Mode Section
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                RECT rcHeaderCount = { 30, 226, rcClient.right - 30, 246 };
                DrawTextW(hmemDC, L"Unit Count Mode for Consist Overhaul:", -1, &rcHeaderCount, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                // Count 0: Keep Original
                g_State.rcRadioCount0 = { 30, 254, 250, 282 };
                bool isC0 = (g_State.countMode == PoolMutator::CountMode::KeepOriginalCount);
                RECT rcCCount0 = { 34, 260, 48, 274 };
                HBRUSH hbrC0 = CreateSolidBrush(cardBgCol);
                HPEN hpenC0 = CreatePen(PS_SOLID, 1, isC0 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrC0);
                hOldP = (HPEN)SelectObject(hmemDC, hpenC0);
                Ellipse(hmemDC, rcCCount0.left, rcCCount0.top, rcCCount0.right, rcCCount0.bottom);
                if (isC0) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCCount0.left + 3, rcCCount0.top + 3, rcCCount0.right - 3, rcCCount0.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrC0); DeleteObject(hpenC0);
                SelectObject(hmemDC, g_State.hFontMain);
                RECT rcCText0 = { 56, 254, 250, 282 };
                DrawTextW(hmemDC, L"Keep Original Unit Count", -1, &rcCText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Count 1: Dynamic Pool Rules
                g_State.rcRadioCount1 = { 260, 254, 520, 282 };
                bool isC1 = (g_State.countMode == PoolMutator::CountMode::DynamicPoolRules);
                RECT rcCCount1 = { 264, 260, 278, 274 };
                HBRUSH hbrC1 = CreateSolidBrush(cardBgCol);
                HPEN hpenC1 = CreatePen(PS_SOLID, 1, isC1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrC1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenC1);
                Ellipse(hmemDC, rcCCount1.left, rcCCount1.top, rcCCount1.right, rcCCount1.bottom);
                if (isC1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCCount1.left + 3, rcCCount1.top + 3, rcCCount1.right - 3, rcCCount1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrC1); DeleteObject(hpenC1);
                RECT rcCText1 = { 286, 254, 520, 282 };
                DrawTextW(hmemDC, L"Dynamic (Use Pool Min/Max Rules)", -1, &rcCText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Count 2: Custom Count
                g_State.rcRadioCount2 = { 30, 296, 230, 324 };
                bool isC2 = (g_State.countMode == PoolMutator::CountMode::CustomUnitCount);
                RECT rcCCount2 = { 34, 302, 48, 316 };
                HBRUSH hbrC2 = CreateSolidBrush(cardBgCol);
                HPEN hpenC2 = CreatePen(PS_SOLID, 1, isC2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrC2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenC2);
                Ellipse(hmemDC, rcCCount2.left, rcCCount2.top, rcCCount2.right, rcCCount2.bottom);
                if (isC2) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCCount2.left + 3, rcCCount2.top + 3, rcCCount2.right - 3, rcCCount2.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrC2); DeleteObject(hpenC2);
                RECT rcCText2 = { 56, 296, 230, 324 };
                DrawTextW(hmemDC, L"Custom Exact Unit Count:", -1, &rcCText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (isC2)
                {
                    HBRUSH hCntBg = CreateSolidBrush(RGB(32, 32, 34));
                    HPEN hCntPen = CreatePen(PS_SOLID, 1, borderCol);
                    HBRUSH holdBCnt = (HBRUSH)SelectObject(hmemDC, hCntBg);
                    HPEN holdCntP = (HPEN)SelectObject(hmemDC, hCntPen);
                    RoundRect(hmemDC, 235, 297, 295, 323, 4, 4);
                    SelectObject(hmemDC, holdBCnt);
                    SelectObject(hmemDC, holdCntP);
                    DeleteObject(hCntBg);
                    DeleteObject(hCntPen);
                }
            }
            // =========================================================================
            // TAB 1: INSERT UNITS
            // =========================================================================
            else
            {
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);

                RECT rcHeaderSrc = { 30, 126, rcClient.right - 30, 146 };
                DrawTextW(hmemDC, L"Select Insertion Source:", -1, &rcHeaderSrc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                // Source Radios: Favourite Group vs Pool Preset
                g_State.rcRadioInsertSrc0 = { 30, 148, 240, 174 };
                bool isSrc0 = (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup);
                RECT rcCSrc0 = { 34, 154, 48, 168 };
                HBRUSH hbrSrc0 = CreateSolidBrush(cardBgCol);
                HPEN hpenSrc0 = CreatePen(PS_SOLID, 1, isSrc0 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrSrc0);
                hOldP = (HPEN)SelectObject(hmemDC, hpenSrc0);
                Ellipse(hmemDC, rcCSrc0.left, rcCSrc0.top, rcCSrc0.right, rcCSrc0.bottom);
                if (isSrc0) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCSrc0.left + 3, rcCSrc0.top + 3, rcCSrc0.right - 3, rcCSrc0.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrSrc0); DeleteObject(hpenSrc0);
                SelectObject(hmemDC, g_State.hFontMain);
                RECT rcSText0 = { 56, 148, 240, 174 };
                DrawTextW(hmemDC, L"Favourite Unit Group", -1, &rcSText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                g_State.rcRadioInsertSrc1 = { 250, 148, 480, 174 };
                bool isSrc1 = (g_State.insertSource == PoolMutator::InsertSource::PoolPreset);
                RECT rcCSrc1 = { 254, 154, 268, 168 };
                HBRUSH hbrSrc1 = CreateSolidBrush(cardBgCol);
                HPEN hpenSrc1 = CreatePen(PS_SOLID, 1, isSrc1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrSrc1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenSrc1);
                Ellipse(hmemDC, rcCSrc1.left, rcCSrc1.top, rcCSrc1.right, rcCSrc1.bottom);
                if (isSrc1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCSrc1.left + 3, rcCSrc1.top + 3, rcCSrc1.right - 3, rcCSrc1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrSrc1); DeleteObject(hpenSrc1);
                RECT rcSText1 = { 276, 148, 480, 174 };
                DrawTextW(hmemDC, L"Pool Preset Rules", -1, &rcSText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Dropdown Row
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                RECT rcLblSource = { 30, 182, 140, 210 };
                const wchar_t* lblSource = (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup) ? L"Select Group(s):" : L"Select Preset(s):";
                DrawTextW(hmemDC, lblSource, -1, &rcLblSource, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                g_State.rcGroupPicker = { 145, 182, rcClient.right - 30, 210 };
                COLORREF grpBg = g_State.isHoverGroup ? RGB(45, 45, 48) : cardBgCol;
                COLORREF grpBorder = g_State.isHoverGroup ? RGB(90, 90, 95) : borderCol;
                HBRUSH hbrGrp = CreateSolidBrush(grpBg);
                HPEN hpenGrp = CreatePen(PS_SOLID, 1, grpBorder);
                HBRUSH holdB3 = (HBRUSH)SelectObject(hmemDC, hbrGrp);
                HPEN holdP3 = (HPEN)SelectObject(hmemDC, hpenGrp);
                RoundRect(hmemDC, g_State.rcGroupPicker.left, g_State.rcGroupPicker.top, g_State.rcGroupPicker.right, g_State.rcGroupPicker.bottom, 6, 6);
                SelectObject(hmemDC, holdB3);
                SelectObject(hmemDC, holdP3);
                DeleteObject(hbrGrp);
                DeleteObject(hpenGrp);

                std::wstring sourceStr = L"";
                if (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup)
                {
                    PoolManager::InitializeReplacementGroups();
                    size_t totalGroups = PoolManager::g_ReplacementGroupsCache.size();
                    if (totalGroups == 0)
                    {
                        sourceStr = L"No Favourite Groups Available";
                    }
                    else
                    {
                        bool isAll = (g_State.selectedGroupIndices.empty() || g_State.selectedGroupIndices.size() >= totalGroups);
                        if (isAll)
                        {
                            int totalU = 0;
                            for (const auto& grp : PoolManager::g_ReplacementGroupsCache) totalU += (int)grp.units.size();
                            sourceStr = L"★ Entire Favourite Palette (All " + std::to_wstring(totalGroups) + L" Groups • " + std::to_wstring(totalU) + L" units)";
                        }
                        else if (g_State.selectedGroupIndices.size() == 1)
                        {
                            int gIdx = g_State.selectedGroupIndices[0];
                            if (gIdx >= 0 && gIdx < (int)totalGroups)
                            {
                                const auto& grp = PoolManager::g_ReplacementGroupsCache[gIdx];
                                sourceStr = (grp.name.empty() ? L"Group #" + std::to_wstring(gIdx + 1) : grp.name) + L" (" + std::to_wstring(grp.units.size()) + L" units)";
                            }
                        }
                        else
                        {
                            std::vector<int> sorted = g_State.selectedGroupIndices;
                            std::sort(sorted.begin(), sorted.end());
                            int totalU = 0;
                            std::wstring grpNames = L"";
                            for (size_t k = 0; k < sorted.size(); ++k)
                            {
                                int gIdx = sorted[k];
                                if (gIdx >= 0 && gIdx < (int)totalGroups)
                                {
                                    totalU += (int)PoolManager::g_ReplacementGroupsCache[gIdx].units.size();
                                    if (k > 0) grpNames += L", ";
                                    grpNames += PoolManager::g_ReplacementGroupsCache[gIdx].name.empty() ? (L"Group #" + std::to_wstring(gIdx + 1)) : PoolManager::g_ReplacementGroupsCache[gIdx].name;
                                }
                            }
                            sourceStr = std::to_wstring(sorted.size()) + L" of " + std::to_wstring(totalGroups) + L" Groups Selected: " + grpNames + L" (" + std::to_wstring(totalU) + L" units)";
                        }
                    }
                }
                else
                {
                    PoolManager::InitializePoolPresets();
                    size_t totalPresets = PoolManager::g_PoolPresetsCache.size();
                    if (totalPresets == 0)
                    {
                        sourceStr = L"No Pool Presets Available";
                    }
                    else
                    {
                        bool isAll = (g_State.selectedPresetIndices.empty() || g_State.selectedPresetIndices.size() >= totalPresets);
                        if (isAll)
                        {
                            int totalU = 0;
                            for (const auto& p : PoolManager::g_PoolPresetsCache)
                                for (const auto& pl : p.pools) totalU += (int)pl.units.size();
                            sourceStr = L"★ Entire Preset Library (All " + std::to_wstring(totalPresets) + L" Presets • " + std::to_wstring(totalU) + L" units)";
                        }
                        else if (g_State.selectedPresetIndices.size() == 1)
                        {
                            int pIdx = g_State.selectedPresetIndices[0];
                            if (pIdx >= 0 && pIdx < (int)totalPresets)
                            {
                                const auto& p = PoolManager::g_PoolPresetsCache[pIdx];
                                int totalU = 0;
                                for (const auto& pl : p.pools) totalU += (int)pl.units.size();
                                sourceStr = (p.presetName.empty() ? L"Preset " + std::to_wstring(pIdx + 1) : p.presetName) + L" (" + std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units)";
                            }
                        }
                        else
                        {
                            std::vector<int> sorted = g_State.selectedPresetIndices;
                            std::sort(sorted.begin(), sorted.end());
                            int totalU = 0;
                            std::wstring presNames = L"";
                            for (size_t k = 0; k < sorted.size(); ++k)
                            {
                                int pIdx = sorted[k];
                                if (pIdx >= 0 && pIdx < (int)totalPresets)
                                {
                                    for (const auto& pl : PoolManager::g_PoolPresetsCache[pIdx].pools) totalU += (int)pl.units.size();
                                    if (k > 0) presNames += L", ";
                                    presNames += PoolManager::g_PoolPresetsCache[pIdx].presetName.empty() ? (L"Preset " + std::to_wstring(pIdx + 1)) : PoolManager::g_PoolPresetsCache[pIdx].presetName;
                                }
                            }
                            sourceStr = std::to_wstring(sorted.size()) + L" of " + std::to_wstring(totalPresets) + L" Presets Selected: " + presNames + L" (" + std::to_wstring(totalU) + L" units)";
                        }
                    }
                }

                SelectObject(hmemDC, g_State.hFontMain);
                SetTextColor(hmemDC, textPrimary);
                RECT rcGrpText = { g_State.rcGroupPicker.left + 10, g_State.rcGroupPicker.top, g_State.rcGroupPicker.right - 30, g_State.rcGroupPicker.bottom };
                DrawTextW(hmemDC, sourceStr.c_str(), -1, &rcGrpText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                RECT rcChevG = { g_State.rcGroupPicker.right - 26, g_State.rcGroupPicker.top, g_State.rcGroupPicker.right - 8, g_State.rcGroupPicker.bottom };
                int cgX = (rcChevG.left + rcChevG.right) / 2;
                int cgY = (rcChevG.top + rcChevG.bottom) / 2;
                COLORREF cgCol = g_State.isHoverGroup ? RGB(255, 255, 255) : textSecondary;
                HBRUSH hBrCG = CreateSolidBrush(cgCol);
                HPEN hPenCG = CreatePen(PS_SOLID, 1, cgCol);
                HBRUSH hOldBCG = (HBRUSH)SelectObject(hmemDC, hBrCG);
                HPEN hOldPCG = (HPEN)SelectObject(hmemDC, hPenCG);
                POINT ptsCG[3] = { { cgX - 4, cgY - 2 }, { cgX + 4, cgY - 2 }, { cgX, cgY + 3 } };
                Polygon(hmemDC, ptsCG, 3);
                SelectObject(hmemDC, hOldBCG);
                SelectObject(hmemDC, hOldPCG);
                DeleteObject(hBrCG);
                DeleteObject(hPenCG);

                // Common Units to Insert Row
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                RECT rcLblCnt = { 30, 218, 140, 246 };
                DrawTextW(hmemDC, L"Units to Insert:", -1, &rcLblCnt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                HBRUSH hInsBg = CreateSolidBrush(RGB(32, 32, 34));
                HPEN hInsPen = CreatePen(PS_SOLID, 1, borderCol);
                HBRUSH holdBIns = (HBRUSH)SelectObject(hmemDC, hInsBg);
                HPEN holdInsP = (HPEN)SelectObject(hmemDC, hInsPen);
                RoundRect(hmemDC, 145, 218, 205, 244, 4, 4);
                SelectObject(hmemDC, holdBIns);
                SelectObject(hmemDC, holdInsP);
                DeleteObject(hInsBg);
                DeleteObject(hInsPen);

                SelectObject(hmemDC, g_State.hFontSmall);
                SetTextColor(hmemDC, textSecondary);
                RECT rcHintCnt = { 215, 218, rcClient.right - 30, 246 };
                const wchar_t* hintText = (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup)
                    ? L"(Mandatory count of random units to pick from selected groups)"
                    : L"(Optional: Leave 0 or blank to use dynamic pool rules min/max)";
                DrawTextW(hmemDC, hintText, -1, &rcHintCnt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Section Divider
                MoveToEx(hmemDC, 30, 254, NULL);
                LineTo(hmemDC, rcClient.right - 30, 254);

                // Position Selection Section
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                RECT rcHeaderPos = { 30, 264, rcClient.right - 30, 284 };
                DrawTextW(hmemDC, L"Select Insertion Position:", -1, &rcHeaderPos, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 0: Head
                g_State.rcRadioPos0 = { 30, 290, 250, 318 };
                bool isP0 = (g_State.posMode == PoolMutator::PositionMode::HeadPosition);
                RECT rcCPos0 = { 34, 296, 48, 310 };
                HBRUSH hbrP0 = CreateSolidBrush(cardBgCol);
                HPEN hpenP0 = CreatePen(PS_SOLID, 1, isP0 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP0);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP0);
                Ellipse(hmemDC, rcCPos0.left, rcCPos0.top, rcCPos0.right, rcCPos0.bottom);
                if (isP0) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos0.left + 3, rcCPos0.top + 3, rcCPos0.right - 3, rcCPos0.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP0); DeleteObject(hpenP0);
                SelectObject(hmemDC, g_State.hFontMain);
                RECT rcPText0 = { 56, 290, 250, 318 };
                DrawTextW(hmemDC, L"Head (Front of Consist)", -1, &rcPText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 1: Behind Engines
                g_State.rcRadioPos1 = { 260, 290, 530, 318 };
                bool isP1 = (g_State.posMode == PoolMutator::PositionMode::BehindEngines);
                RECT rcCPos1 = { 264, 296, 278, 310 };
                HBRUSH hbrP1 = CreateSolidBrush(cardBgCol);
                HPEN hpenP1 = CreatePen(PS_SOLID, 1, isP1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP1);
                Ellipse(hmemDC, rcCPos1.left, rcCPos1.top, rcCPos1.right, rcCPos1.bottom);
                if (isP1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos1.left + 3, rcCPos1.top + 3, rcCPos1.right - 3, rcCPos1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP1); DeleteObject(hpenP1);
                RECT rcPText1 = { 286, 290, 530, 318 };
                DrawTextW(hmemDC, L"Behind Lead Locomotives", -1, &rcPText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 3: Tail
                g_State.rcRadioPos3 = { 30, 326, 250, 354 };
                bool isP3 = (g_State.posMode == PoolMutator::PositionMode::TailPosition);
                RECT rcCPos3 = { 34, 332, 48, 346 };
                HBRUSH hbrP3 = CreateSolidBrush(cardBgCol);
                HPEN hpenP3 = CreatePen(PS_SOLID, 1, isP3 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP3);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP3);
                Ellipse(hmemDC, rcCPos3.left, rcCPos3.top, rcCPos3.right, rcCPos3.bottom);
                if (isP3) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos3.left + 3, rcCPos3.top + 3, rcCPos3.right - 3, rcCPos3.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP3); DeleteObject(hpenP3);
                RECT rcPText3 = { 56, 326, 250, 354 };
                DrawTextW(hmemDC, L"Tail (End of Consist)", -1, &rcPText3, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 2: Specific Index
                g_State.rcRadioPos2 = { 260, 326, 465, 354 };
                bool isP2 = (g_State.posMode == PoolMutator::PositionMode::SpecificIndex);
                RECT rcCPos2 = { 264, 332, 278, 346 };
                HBRUSH hbrP2 = CreateSolidBrush(cardBgCol);
                HPEN hpenP2 = CreatePen(PS_SOLID, 1, isP2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP2);
                Ellipse(hmemDC, rcCPos2.left, rcCPos2.top, rcCPos2.right, rcCPos2.bottom);
                if (isP2) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos2.left + 3, rcCPos2.top + 3, rcCPos2.right - 3, rcCPos2.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP2); DeleteObject(hpenP2);
                RECT rcPText2 = { 286, 326, 465, 354 };
                DrawTextW(hmemDC, L"At Specific Index / Indices:", -1, &rcPText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (isP2)
                {
                    HBRUSH hPosBg = CreateSolidBrush(RGB(32, 32, 34));
                    HPEN hPosPen = CreatePen(PS_SOLID, 1, borderCol);
                    HBRUSH holdBPos = (HBRUSH)SelectObject(hmemDC, hPosBg);
                    HPEN holdPosP = (HPEN)SelectObject(hmemDC, hPosPen);
                    RoundRect(hmemDC, 469, 328, 595, 354, 4, 4);
                    SelectObject(hmemDC, holdBPos);
                    SelectObject(hmemDC, holdPosP);
                    DeleteObject(hPosBg);
                    DeleteObject(hPosPen);

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, textSecondary);
                    RECT rcHintIdx = { 605, 326, rcClient.right - 30, 354 };
                    DrawTextW(hmemDC, L"(e.g. 1; 5; 10)", -1, &rcHintIdx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }

            // Bottom Configuration Section (Common to both tabs)
            int bottomY = rcClient.bottom - 120;

            // Checkbox: Create Clones
            g_State.rcCheckboxClones = { 30, bottomY, rcClient.right - 30, bottomY + 24 };
            RECT rcChkBox = { 34, bottomY + 4, 50, bottomY + 20 };
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
            RECT rcChkText = { 58, bottomY, rcClient.right - 30, bottomY + 24 };
            DrawTextW(hmemDC, L"Create cloned variation files (Keep originals unmodified)", -1, &rcChkText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Suffix Label & Border
            RECT rcLblSuf = { 30, bottomY + 30, 120, bottomY + 54 };
            SetTextColor(hmemDC, textSecondary);
            DrawTextW(hmemDC, L"Clone Suffix:", -1, &rcLblSuf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            HBRUSH hSufBg = CreateSolidBrush(RGB(32, 32, 34));
            HPEN hSufPen = CreatePen(PS_SOLID, 1, borderCol);
            HBRUSH holdBSuf = (HBRUSH)SelectObject(hmemDC, hSufBg);
            HPEN holdSufP = (HPEN)SelectObject(hmemDC, hSufPen);
            RoundRect(hmemDC, 124, bottomY + 29, 286, bottomY + 55, 4, 4);
            SelectObject(hmemDC, holdBSuf);
            SelectObject(hmemDC, holdSufP);
            DeleteObject(hSufBg);
            DeleteObject(hSufPen);

            // Footer Bar (Y = rcClient.bottom - 54 to rcClient.bottom)
            int footerH = 54;
            RECT rcFooter = { 0, rcClient.bottom - footerH, rcClient.right, rcClient.bottom };
            HBRUSH hbrFoot = CreateSolidBrush(PoolTheme::FooterBackground);
            FillRect(hmemDC, &rcFooter, hbrFoot);
            DeleteObject(hbrFoot);

            HPEN hPenFootLine = CreatePen(PS_SOLID, 1, RGB(78, 32, 38));
            SelectObject(hmemDC, hPenFootLine);
            MoveToEx(hmemDC, 0, rcClient.bottom - footerH, NULL);
            LineTo(hmemDC, rcClient.right, rcClient.bottom - footerH);
            DeleteObject(hPenFootLine);

            // Action Buttons: [Apply / Insert] & [Cancel]
            const wchar_t* applyText = (g_State.activeTab == 0) ? L"Apply Mutation" : L"Insert Units";
            g_State.rcApplyBtn = { rcClient.right - 180, rcClient.bottom - 42, rcClient.right - 20, rcClient.bottom - 12 };
            DrawModernButton(hmemDC, g_State.rcApplyBtn, applyText, g_State.isHoverApply, false, true, g_State.hFontBold, g_State.hFontIcon, L"\xE73E");

            g_State.rcCancelBtn = { rcClient.right - 280, rcClient.bottom - 42, rcClient.right - 190, rcClient.bottom - 12 };
            DrawModernButton(hmemDC, g_State.rcCancelBtn, L"Cancel", g_State.isHoverCancel, false, false, g_State.hFontMain);

            SelectObject(hmemDC, holdPen);
            DeleteObject(hPenLine);

            BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hmemDC, 0, 0, SRCCOPY);

            SelectObject(hmemDC, holdBm);
            DeleteObject(hbm);
            DeleteDC(hmemDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_CLOSE:
        {
            RestoreParentWindowFocus(g_State.hParent ? g_State.hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY:
        {
            SaveCurrentDialogState();
            RestoreParentWindowFocus(g_State.hParent ? g_State.hParent : GetWindow(hWnd, GW_OWNER));
            if (g_State.hFontTitle) DeleteObject(g_State.hFontTitle);
            if (g_State.hFontMain) DeleteObject(g_State.hFontMain);
            if (g_State.hFontBold) DeleteObject(g_State.hFontBold);
            if (g_State.hFontSmall) DeleteObject(g_State.hFontSmall);
            if (g_State.hFontIcon) DeleteObject(g_State.hFontIcon);
            if (g_State.hFontIconLg) DeleteObject(g_State.hFontIconLg);

            g_hPoolMutatorDlg = NULL;
            return 0;
        }

        case WM_NCDESTROY:
        {
            HWND hOwner = GetWindow(hWnd, GW_OWNER);
            if (hOwner && IsWindow(hOwner))
            {
                RestoreParentWindowFocus(hOwner);
            }
            break;
        }
        }
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

bool PoolMutatorDlg_IsOpen()
{
    return (g_hPoolMutatorDlg != NULL && IsWindow(g_hPoolMutatorDlg));
}

void PoolMutatorDlg_UpdateSelection(
    const std::vector<std::wstring>& targetConsistFilePaths,
    const std::vector<int>& targetSelectedUnitIndices)
{
    if (g_hPoolMutatorDlg && IsWindow(g_hPoolMutatorDlg))
    {
        g_State.targetConsistPaths = targetConsistFilePaths;
        g_State.targetUnitIndices = targetSelectedUnitIndices;
        InvalidateRect(g_hPoolMutatorDlg, NULL, TRUE);
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
        g_State.hParent = hWndParent;
        g_State.mode = initialMode;
        g_State.activeTab = (initialMode == PoolMutator::MutatorMode::InsertUnits) ? 1 : 0;
        g_State.targetConsistPaths = targetConsistFilePaths;
        g_State.targetUnitIndices = targetSelectedUnitIndices;
        if (g_State.hTitleBar && IsWindow(g_State.hTitleBar))
        {
            CustomTitleBar_SetActiveTab(g_State.hTitleBar, g_State.activeTab);
        }
        UpdateControlPositions(g_hPoolMutatorDlg);
        InvalidateRect(g_hPoolMutatorDlg, NULL, TRUE);
        SetForegroundWindow(g_hPoolMutatorDlg);
        return;
    }

    PoolManager::InitializePoolPresets();

    g_State = MutatorDlgState();
    g_State.hParent = hWndParent;
    g_State.mode = initialMode;
    g_State.activeTab = (initialMode == PoolMutator::MutatorMode::InsertUnits) ? 1 : 0;
    g_State.targetConsistPaths = targetConsistFilePaths;
    g_State.targetUnitIndices = targetSelectedUnitIndices;

    PoolMutator::MutatorSavedSettings saved;
    if (PoolMutator::LoadMutationSettings(saved))
    {
        g_State.activeTab = saved.activeTab;
        g_State.selectedPresetIdx = saved.selectedPresetIdx;
        g_State.selectedPoolIndices = saved.selectedPoolIndices;
        g_State.selectedPresetIndices = saved.selectedPresetIndices;
        g_State.countMode = (PoolMutator::CountMode)saved.countMode;
        g_State.customCount = saved.customCount;
        g_State.createClones = saved.createClones;
        if (!saved.cloneSuffix.empty()) g_State.cloneSuffix = saved.cloneSuffix;
        g_State.insertSource = (PoolMutator::InsertSource)saved.insertSource;
        g_State.selectedGroupIdx = saved.selectedGroupIdx;
        g_State.selectedGroupIndices = saved.selectedGroupIndices;
        g_State.insertCount = saved.insertCount;
        g_State.posMode = (PoolMutator::PositionMode)saved.posMode;
        if (!saved.positionIndexText.empty()) g_State.positionIndexStr = saved.positionIndexText;

        if (initialMode == PoolMutator::MutatorMode::InsertUnits)
        {
            g_State.activeTab = 1;
            g_State.mode = PoolMutator::MutatorMode::InsertUnits;
        }
        else if (initialMode == PoolMutator::MutatorMode::MutateConsists)
        {
            g_State.activeTab = 0;
            g_State.mode = PoolMutator::MutatorMode::MutateConsists;
        }
    }

    const wchar_t* szClassName = L"PoolMutatorDlgClass";
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = PoolMutatorDlgProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(PoolTheme::GutterBackground);
    wcex.lpszClassName = szClassName;

    RegisterClassExW(&wcex);

    int dlgW = 980;
    int dlgH = 700;

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
        0, szClassName, L"Consist Pool Mutator & Injector",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, dlgW, dlgH,
        hWndParent, NULL, GetModuleHandle(NULL), NULL
    );

    if (!hDlg) return;

    g_hPoolMutatorDlg = hDlg;

    BOOL bDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    DWM_WINDOW_CORNER_PREFERENCE corner = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
    DwmSetWindowAttribute(hDlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    COLORREF borderColor = PoolTheme::BorderLine;
    DwmSetWindowAttribute(hDlg, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    MARGINS margins = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hDlg, &margins);

    SetWindowPos(hDlg, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    ShowWindow(hDlg, SW_SHOW);

    RECT rc;
    GetClientRect(hDlg, &rc);
    int clientW = rc.right - rc.left;
    if (g_State.hTitleBar && IsWindow(g_State.hTitleBar))
    {
        SetWindowPos(g_State.hTitleBar, NULL, 0, 0, clientW, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(g_State.hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
    }

    RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
    UpdateWindow(hDlg);
}
