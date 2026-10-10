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
#include "../SRC/TrainConfig.h"
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
        HFONT hFontIconMed = NULL;
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
        std::vector<int> selectedBlueprintIndices; // Tab 1: subset of blueprints. Empty = All blueprints
        std::vector<std::pair<int, int>> selectedPresetPools;    // Tab 1: (presetIdx, poolIdx)
        std::vector<std::pair<int, int>> selectedBlueprintPools; // Tab 1: (blueprintIdx, poolIdx)
        bool createClones = false;
        std::wstring cloneSuffix = L"_PoolVar";
        int customCount = 20;
        int insertCount = 2;
        int positionIndex = 0;
        std::wstring positionIndexStr = L"1";
        bool isUsingTrainConfig = false;
        std::wstring activeTrainConfigPath;
        PoolManager::PoolPreset activeTrainConfigPreset;

        // Tab 2: Replace / Repair Broken Units & Multi-Source Routing Architecture
        std::vector<PoolMutator::BrokenConsistInfo> brokenConsists;
        int brokenScrollY = 0;
        int brokenTotalContentH = 0;
        std::vector<PoolMutator::SourceNodeConfig> sourceNodes;
        int nextSourceNodeId = 2;

        struct SourceCardUI
        {
            int sourceId = 0;
            RECT rcCard = { 0 };
            RECT rcTypeToggle = { 0 };
            RECT rcPicker = { 0 };
            RECT rcDeleteBtn = { 0 };
            bool isHoverPicker = false;
            bool isHoverToggle = false;
            bool isHoverDelete = false;
        };
        std::vector<SourceCardUI> sourceCardUIs;
        RECT rcAddSourceBtn = { 0 };
        bool isHoverAddSourceBtn = false;

        // Interactive Rectangles
        RECT rcPresetPicker = { 0 };
        RECT rcPoolPicker = { 0 };
        RECT rcGroupPicker = { 0 };
        RECT rcRadioInsertSrc0 = { 0 }; // Favourite Group Radio
        RECT rcRadioInsertSrc1 = { 0 }; // Pool Preset Radio
        RECT rcRadioInsertSrc2 = { 0 }; // Train Blueprint Radio
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

        // Tab 2 Specific Rectangles
        RECT rcBrokenTreeCard = { 0 };
        RECT rcBrokenTreeList = { 0 };
        RECT rcMasterBrokenCheck = { 0 };

        // Tab 2 Actions & Buttons
        RECT rcTab2BtnSelectBroken = { 0 };
        RECT rcTab2BtnScanLibrary = { 0 };

        // Hover States
        bool isHoverPreset = false;
        bool isHoverPool = false;
        bool isHoverGroup = false;
        bool isHoverRadioSrc0 = false;
        bool isHoverRadioSrc1 = false;
        bool isHoverRadioSrc2 = false;
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

        // Tab 2 Hover States
        bool isHoverMasterBroken = false;
        bool isHoverTab2BtnSelectBroken = false;
        bool isHoverTab2BtnScanLibrary = false;
    };

    static MutatorDlgState g_State;

    static const PoolMutator::SourceNodeConfig* FindSourceNodeById(int id);
    static int CycleNextSourceIdForConsist(int currentId, int actionExecutionMode);

    static PoolManager::PoolPreset* GetActiveMutatorPreset()
    {
        if (g_State.isUsingTrainConfig)
        {
            return &g_State.activeTrainConfigPreset;
        }
        return PoolManager::GetPresetByIndex(g_State.selectedPresetIdx);
    }

    static void RefreshBrokenConsists(bool forceScanAll = false)
    {
        g_State.brokenConsists = ScanActiveTargetConsistsForBrokenUnits(
            g_State.targetConsistPaths,
            g_State.targetUnitIndices,
            forceScanAll,
            g_szBasePath
        );
        for (auto& bc : g_State.brokenConsists)
        {
            if (bc.brokenUnits.empty())
            {
                bc.actionExecutionMode = 1;
                bc.isSelected = true;
                bc.isExpanded = true;
                const auto* pCur = FindSourceNodeById(bc.assignedSourceId);
                if (!pCur || pCur->sourceType == 0)
                {
                    bc.assignedSourceId = CycleNextSourceIdForConsist(bc.assignedSourceId, 1);
                }
            }
        }
        g_State.brokenScrollY = 0;
    }

    static void UpdateControlPositions(HWND hWnd)
    {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int w = rcClient.right;
        int h = rcClient.bottom;

        bool isTab0 = (g_State.activeTab == 0);
        bool isTab1 = (g_State.activeTab == 1);
        bool isTab2 = (g_State.activeTab == 2);

        int colSplitX = (w - 60) / 2 + 30;

        // Custom count edit box (Tab 0: Mutate Consist mode)
        if (g_State.hEditCustomCount)
        {
            if (isTab0 && g_State.countMode == PoolMutator::CountMode::CustomUnitCount)
            {
                SetWindowPos(g_State.hEditCustomCount, NULL, 238, 301, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
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
                SetWindowPos(g_State.hEditInsertCount, NULL, 148, 222, 54, 18, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
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
                int posX = (std::max)(colSplitX + 10 + 205, 472);
                SetWindowPos(g_State.hEditPositionIndex, NULL, posX, 332, 110, 18, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
            }
            else
            {
                ShowWindow(g_State.hEditPositionIndex, SW_HIDE);
            }
        }

        // Clone suffix edit box
        if (g_State.hEditCloneSuffix)
        {
            if (isTab2)
            {
                ShowWindow(g_State.hEditCloneSuffix, SW_HIDE);
            }
            else
            {
                SetWindowPos(g_State.hEditCloneSuffix, NULL, 128, h - 89, 154, 18, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
            }
        }

        InvalidateRect(hWnd, NULL, FALSE);
    }

    static COLORREF GetSourceColorByIndex(int idx)
    {
        static const COLORREF s_colors[] = {
            RGB(56, 189, 248),   // Sky blue (Source A)
            RGB(245, 158, 11),   // Amber orange (Source B)
            RGB(16, 185, 129),   // Emerald green (Source C)
            RGB(168, 85, 247),   // Purple (Source D)
            RGB(244, 63, 94),    // Rose red (Source E)
            RGB(234, 179, 8),    // Yellow (Source F)
            RGB(6, 182, 212),    // Cyan (Source G)
            RGB(236, 72, 153)    // Pink (Source H)
        };
        return s_colors[idx % (sizeof(s_colors) / sizeof(s_colors[0]))];
    }

    static void EnsureDefaultSourceNodes()
    {
        if (g_State.sourceNodes.empty())
        {
            PoolMutator::SourceNodeConfig sA;
            sA.sourceId = 0;
            sA.name = L"Source A";
            sA.sourceType = 0; // Favourite Group
            sA.groupIndex = 0;
            sA.color = GetSourceColorByIndex(0);
            g_State.sourceNodes.push_back(sA);

            PoolMutator::SourceNodeConfig sB;
            sB.sourceId = 1;
            sB.name = L"Source B";
            sB.sourceType = 1; // Pool Preset
            sB.presetIndex = 0;
            sB.color = GetSourceColorByIndex(1);
            g_State.sourceNodes.push_back(sB);

            g_State.nextSourceNodeId = 2;
        }
    }

    static const PoolMutator::SourceNodeConfig* FindSourceNodeById(int id)
    {
        for (const auto& node : g_State.sourceNodes)
        {
            if (node.sourceId == id) return &node;
        }
        if (!g_State.sourceNodes.empty()) return &g_State.sourceNodes[0];
        return nullptr;
    }

    static int CycleNextSourceId(int currentId)
    {
        if (g_State.sourceNodes.empty()) return 0;
        for (size_t i = 0; i < g_State.sourceNodes.size(); ++i)
        {
            if (g_State.sourceNodes[i].sourceId == currentId)
            {
                size_t nextIdx = (i + 1) % g_State.sourceNodes.size();
                return g_State.sourceNodes[nextIdx].sourceId;
            }
        }
        return g_State.sourceNodes[0].sourceId;
    }

    static int CycleNextSourceIdForConsist(int currentId, int actionExecutionMode)
    {
        if (g_State.sourceNodes.empty()) return 0;
        if (actionExecutionMode == 1) // Rebuild Consist: only Pool Preset (1) and Train Config (2)
        {
            std::vector<int> validNodeIds;
            for (const auto& node : g_State.sourceNodes)
            {
                if (node.sourceType == 1 || node.sourceType == 2)
                {
                    validNodeIds.push_back(node.sourceId);
                }
            }
            if (validNodeIds.empty())
            {
                return currentId;
            }
            for (size_t i = 0; i < validNodeIds.size(); ++i)
            {
                if (validNodeIds[i] == currentId)
                {
                    size_t nextIdx = (i + 1) % validNodeIds.size();
                    return validNodeIds[nextIdx];
                }
            }
            return validNodeIds[0];
        }
        else
        {
            return CycleNextSourceId(currentId);
        }
    }

    static std::wstring GetSourceNodeSummaryText(const PoolMutator::SourceNodeConfig& node)
    {
        if (node.sourceType == 0) // Favourite Group
        {
            PoolManager::InitializeReplacementGroups();
            size_t totalGroups = PoolManager::g_ReplacementGroupsCache.size();
            if (totalGroups == 0) return L"No Favourite Groups Available";

            bool isAll = (node.selectedGroupIndices.empty() || node.selectedGroupIndices.size() >= totalGroups);
            if (isAll)
            {
                int totalU = 0;
                for (const auto& grp : PoolManager::g_ReplacementGroupsCache) totalU += (int)grp.units.size();
                return L"★ Entire Palette (" + std::to_wstring(totalGroups) + L" Groups • " + std::to_wstring(totalU) + L" units)";
            }
            else if (node.selectedGroupIndices.size() == 1)
            {
                int gIdx = node.selectedGroupIndices[0];
                if (gIdx >= 0 && gIdx < (int)totalGroups)
                {
                    const auto& grp = PoolManager::g_ReplacementGroupsCache[gIdx];
                    return (grp.name.empty() ? L"Group #" + std::to_wstring(gIdx + 1) : grp.name) + L" (" + std::to_wstring(grp.units.size()) + L" units)";
                }
            }
            else
            {
                int totalU = 0;
                for (int gIdx : node.selectedGroupIndices)
                {
                    if (gIdx >= 0 && gIdx < (int)totalGroups) totalU += (int)PoolManager::g_ReplacementGroupsCache[gIdx].units.size();
                }
                return std::to_wstring(node.selectedGroupIndices.size()) + L" of " + std::to_wstring(totalGroups) + L" Groups Selected (" + std::to_wstring(totalU) + L" units)";
            }
        }
        else if (node.sourceType == 2) // Train Blueprint
        {
            TrainConfigManager::ScanTrainConfigs();
            size_t totalBlueprints = TrainConfigManager::g_LoadedConfigsCache.size();
            if (totalBlueprints == 0) return L"No Train Configs Available";

            size_t totalAllPools = 0;
            for (size_t c = 0; c < totalBlueprints; ++c)
            {
                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
                TrainConfigManager::TrainBinding binding;
                TrainConfigManager::LoadTrainBinding(cfg, binding);
                PoolManager::PoolPreset bpPreset;
                TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, bpPreset);
                totalAllPools += bpPreset.pools.size();
            }

            bool isAll = (node.selectedBlueprintPools.empty() || node.selectedBlueprintPools.size() >= totalAllPools);
            if (isAll)
            {
                return L"★ Entire Train Config Library (All " + std::to_wstring(totalBlueprints) + L" Train Configs)";
            }
            else if (node.selectedBlueprintPools.size() == 1)
            {
                auto p = node.selectedBlueprintPools[0];
                if (p.first >= 0 && p.first < (int)totalBlueprints)
                {
                    const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[p.first];
                    TrainConfigManager::TrainBinding binding;
                    TrainConfigManager::LoadTrainBinding(cfg, binding);
                    PoolManager::PoolPreset bpPreset;
                    TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, bpPreset);
                    std::wstring poolName = (p.second >= 0 && p.second < (int)bpPreset.pools.size()) ? bpPreset.pools[p.second].name : L"";
                    return cfg.name + L" ➔ " + (poolName.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : poolName);
                }
            }
            else
            {
                return std::to_wstring(node.selectedBlueprintPools.size()) + L" of " + std::to_wstring(totalAllPools) + L" Train Config Pools Selected";
            }
        }
        else // Pool Preset
        {
            PoolManager::InitializePoolPresets();
            size_t totalPresets = PoolManager::g_PoolPresetsCache.size();
            if (totalPresets == 0) return L"No Pool Presets Available";

            size_t totalAllPools = 0;
            int totalU = 0;
            for (const auto& p : PoolManager::g_PoolPresetsCache)
            {
                totalAllPools += p.pools.size();
                for (const auto& pl : p.pools) totalU += (int)pl.units.size();
            }

            bool isAll = (node.selectedPresetPools.empty() || node.selectedPresetPools.size() >= totalAllPools);
            if (isAll)
            {
                return L"★ Entire Preset Library (All " + std::to_wstring(totalPresets) + L" Presets • " + std::to_wstring(totalU) + L" units)";
            }
            else if (node.selectedPresetPools.size() == 1)
            {
                auto p = node.selectedPresetPools[0];
                if (p.first >= 0 && p.first < (int)totalPresets)
                {
                    const auto& pres = PoolManager::g_PoolPresetsCache[p.first];
                    std::wstring poolName = (p.second >= 0 && p.second < (int)pres.pools.size()) ? pres.pools[p.second].name : L"";
                    return (pres.presetName.empty() ? L"Preset " + std::to_wstring(p.first + 1) : pres.presetName) + L" ➔ " + (poolName.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : poolName);
                }
            }
            else
            {
                int selU = 0;
                for (const auto& pp : node.selectedPresetPools)
                {
                    if (pp.first >= 0 && pp.first < (int)totalPresets)
                    {
                        const auto& pres = PoolManager::g_PoolPresetsCache[pp.first];
                        if (pp.second >= 0 && pp.second < (int)pres.pools.size())
                            selU += (int)pres.pools[pp.second].units.size();
                    }
                }
                return std::to_wstring(node.selectedPresetPools.size()) + L" of " + std::to_wstring(totalAllPools) + L" Preset Pools Selected (" + std::to_wstring(selU) + L" units)";
            }
        }
        return L"Select Source Stock";
    }

    static void ShowBlueprintDropdownForSource(HWND hWnd, int sourceNodeIndex, const RECT& anchorRc)
    {
        if (sourceNodeIndex < 0 || sourceNodeIndex >= (int)g_State.sourceNodes.size()) return;
        auto& node = g_State.sourceNodes[sourceNodeIndex];

        TrainConfigManager::ScanTrainConfigs();
        if (TrainConfigManager::g_LoadedConfigsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Train Configs available (.train files in TRAIN_CONFIGS).", L"Train Configs", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalBlueprints = TrainConfigManager::g_LoadedConfigsCache.size();
        std::vector<PoolManager::PoolPreset> bpPresets;
        size_t totalAllPools = 0;

        for (size_t c = 0; c < totalBlueprints; ++c)
        {
            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
            TrainConfigManager::TrainBinding binding;
            TrainConfigManager::LoadTrainBinding(cfg, binding);
            PoolManager::PoolPreset bpPreset;
            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, bpPreset);
            totalAllPools += bpPreset.pools.size();
            bpPresets.push_back(bpPreset);
        }

        bool isAllSelected = (totalAllPools > 0 && (node.selectedBlueprintPools.empty() || node.selectedBlueprintPools.size() >= totalAllPools));
        bool hasAnySelected = (!node.selectedBlueprintPools.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Train Config Library (All Pools)", L"", isAllSelected, hasAnySelected));

        // Group blueprints by category
        std::vector<std::wstring> categoryOrder;
        std::map<std::wstring, std::vector<size_t>> categorizedConfigs;
        for (size_t c = 0; c < totalBlueprints; ++c)
        {
            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
            std::wstring cat = cfg.category.empty() ? L"General" : cfg.category;
            if (categorizedConfigs.find(cat) == categorizedConfigs.end())
            {
                categoryOrder.push_back(cat);
            }
            categorizedConfigs[cat].push_back(c);
        }

        for (size_t catIdx = 0; catIdx < categoryOrder.size(); ++catIdx)
        {
            const auto& cat = categoryOrder[catIdx];
            const auto& cfgIndices = categorizedConfigs[cat];

            int totalCatPools = 0;
            int selCatPools = 0;
            for (size_t cIdx : cfgIndices)
            {
                const auto& bpPreset = bpPresets[cIdx];
                totalCatPools += (int)bpPreset.pools.size();
                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    if (isAllSelected || std::find(node.selectedBlueprintPools.begin(), node.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != node.selectedBlueprintPools.end())
                    {
                        selCatPools++;
                    }
                }
            }

            bool catAll = (totalCatPools > 0 && selCatPools == totalCatPools);
            bool catIndeterminate = (selCatPools > 0 && !catAll);

            // Category Level 0 Group Header
            items.push_back(DropDownItem::GroupHeader(cat, L"\xE8B7", (int)cfgIndices.size(), false, (int)(10000 + catIdx), catAll, catIndeterminate, std::to_wstring(cfgIndices.size()) + L" Blueprints", L"", 0));

            for (size_t cIdx : cfgIndices)
            {
                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cIdx];
                const auto& bpPreset = bpPresets[cIdx];

                int bpPoolCount = (int)bpPreset.pools.size();
                int bpSelPoolCount = 0;
                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    if (isAllSelected || std::find(node.selectedBlueprintPools.begin(), node.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != node.selectedBlueprintPools.end())
                    {
                        bpSelPoolCount++;
                    }
                }
                bool bpAll = (bpPoolCount > 0 && bpSelPoolCount == bpPoolCount);
                bool bpIndet = (bpSelPoolCount > 0 && !bpAll);
                std::wstring bpGroupKey = L"BP:" + std::to_wstring(cIdx);
                std::wstring bpSecText = std::to_wstring(bpPoolCount) + L" pools";
                if (cfg.maxSpeedKmph > 0) bpSecText += L" • " + std::to_wstring((int)cfg.maxSpeedKmph) + L" km/h";

                // Blueprint Level 1 Group Header
                DropDownItem bpHdr = DropDownItem::GroupHeader(cfg.name, L"\xE7C0", bpPoolCount, false, (int)(20000 + cIdx), bpAll, bpIndet, bpSecText, cat, 1);
                bpHdr.group = bpGroupKey;
                items.push_back(bpHdr);

                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    const auto& pl = bpPreset.pools[plIdx];
                    std::wstring plLabel = std::to_wstring(plIdx + 1) + L". " + (pl.name.empty() ? (L"Pool #" + std::to_wstring(plIdx + 1)) : pl.name);
                    std::wstring tag = std::to_wstring(pl.units.size()) + L" units";

                    int itemId = (int)(cIdx * 100 + plIdx + 2);
                    bool isChecked = isAllSelected || (std::find(node.selectedBlueprintPools.begin(), node.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != node.selectedBlueprintPools.end());
                    // Pool Level 2 leaf item
                    items.push_back(DropDownItem::Action(itemId, L"", plLabel, tag, isChecked, true, bpGroupKey, cat, 2));
                }
            }
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, anchorRc, items, [hWnd, sourceNodeIndex](const std::vector<DropDownItem>& updatedItems) {
            if (sourceNodeIndex >= 0 && sourceNodeIndex < (int)g_State.sourceNodes.size())
            {
                auto& n = g_State.sourceNodes[sourceNodeIndex];
                n.selectedBlueprintPools.clear();
                n.selectedBlueprintIndices.clear();

                for (const auto& itm : updatedItems)
                {
                    if (itm.isChecked && itm.id >= 2 && !itm.isGroupHeader && !itm.isHeader)
                    {
                        int raw = itm.id - 2;
                        int cIdx = raw / 100;
                        int plIdx = raw % 100;
                        n.selectedBlueprintPools.push_back({ cIdx, plIdx });
                        if (std::find(n.selectedBlueprintIndices.begin(), n.selectedBlueprintIndices.end(), cIdx) == n.selectedBlueprintIndices.end())
                        {
                            n.selectedBlueprintIndices.push_back(cIdx);
                        }
                    }
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        });
    }

    static void ShowGroupDropdownForSource(HWND hWnd, int sourceNodeIndex, const RECT& anchorRc)
    {
        if (sourceNodeIndex < 0 || sourceNodeIndex >= (int)g_State.sourceNodes.size()) return;
        auto& node = g_State.sourceNodes[sourceNodeIndex];

        PoolManager::InitializeReplacementGroups();
        if (PoolManager::g_ReplacementGroupsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Favourite Unit Groups created yet. Open Pool Manager to create group palettes first.", L"Favourite Groups", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalGroups = PoolManager::g_ReplacementGroupsCache.size();
        bool isAllSelected = (totalGroups > 0 && (node.selectedGroupIndices.empty() || node.selectedGroupIndices.size() >= totalGroups));
        bool hasAnySelected = (!node.selectedGroupIndices.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Favourite Palette (All Groups)", L"", isAllSelected, hasAnySelected));

        for (size_t i = 0; i < totalGroups; ++i)
        {
            const auto& grp = PoolManager::g_ReplacementGroupsCache[i];
            std::wstring label = grp.name.empty() ? (L"Group #" + std::to_wstring(i + 1)) : grp.name;
            std::wstring cat = grp.category.empty() ? L"General" : grp.category;
            std::wstring tag = cat + L" • " + std::to_wstring(grp.units.size()) + L" units";
            bool isChecked = isAllSelected || (std::find(node.selectedGroupIndices.begin(), node.selectedGroupIndices.end(), (int)i) != node.selectedGroupIndices.end());
            items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, anchorRc, items, [hWnd, sourceNodeIndex](const std::vector<DropDownItem>& updatedItems) {
            if (sourceNodeIndex >= 0 && sourceNodeIndex < (int)g_State.sourceNodes.size())
            {
                auto& n = g_State.sourceNodes[sourceNodeIndex];
                n.selectedGroupIndices.clear();
                for (size_t k = 1; k < updatedItems.size(); ++k)
                {
                    if (updatedItems[k].isChecked && !updatedItems[k].isGroupHeader && !updatedItems[k].isHeader)
                    {
                        n.selectedGroupIndices.push_back(updatedItems[k].id - 2);
                    }
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        });
    }

    static void ShowPresetDropdownForSource(HWND hWnd, int sourceNodeIndex, const RECT& anchorRc)
    {
        if (sourceNodeIndex < 0 || sourceNodeIndex >= (int)g_State.sourceNodes.size()) return;
        auto& node = g_State.sourceNodes[sourceNodeIndex];

        PoolManager::InitializePoolPresets();
        if (PoolManager::g_PoolPresetsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Pool Presets available.", L"Pool Presets", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalPresets = PoolManager::g_PoolPresetsCache.size();
        size_t totalAllPools = 0;
        for (const auto& p : PoolManager::g_PoolPresetsCache) totalAllPools += p.pools.size();

        bool isAllSelected = (totalAllPools > 0 && (node.selectedPresetPools.empty() || node.selectedPresetPools.size() >= totalAllPools));
        bool hasAnySelected = (!node.selectedPresetPools.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Preset Library (All Pools)", L"", isAllSelected, hasAnySelected));

        for (size_t i = 0; i < totalPresets; ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            int presPoolCount = (int)p.pools.size();
            int presSelPoolCount = 0;
            for (size_t plIdx = 0; plIdx < p.pools.size(); ++plIdx)
            {
                if (isAllSelected || std::find(node.selectedPresetPools.begin(), node.selectedPresetPools.end(), std::make_pair((int)i, (int)plIdx)) != node.selectedPresetPools.end())
                {
                    presSelPoolCount++;
                }
            }
            bool presAll = (presPoolCount > 0 && presSelPoolCount == presPoolCount);
            bool presIndet = (presSelPoolCount > 0 && !presAll);
            std::wstring presGroupKey = L"PRESET:" + std::to_wstring(i);
            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring presSecText = std::to_wstring(presPoolCount) + L" pools • " + std::to_wstring(totalU) + L" units";

            // Preset Level 0 Group Header
            DropDownItem presHdr = DropDownItem::GroupHeader(label, L"\xE71D", presPoolCount, false, (int)(30000 + i), presAll, presIndet, presSecText, L"", 0);
            presHdr.group = presGroupKey;
            items.push_back(presHdr);

            for (size_t plIdx = 0; plIdx < p.pools.size(); ++plIdx)
            {
                const auto& pl = p.pools[plIdx];
                std::wstring plLabel = std::to_wstring(plIdx + 1) + L". " + (pl.name.empty() ? (L"Pool #" + std::to_wstring(plIdx + 1)) : pl.name);
                std::wstring modeStr = (pl.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
                std::wstring tag = std::to_wstring(pl.units.size()) + L" units • [" + modeStr + L"]";

                int itemId = (int)(i * 100 + plIdx + 2);
                bool isChecked = isAllSelected || (std::find(node.selectedPresetPools.begin(), node.selectedPresetPools.end(), std::make_pair((int)i, (int)plIdx)) != node.selectedPresetPools.end());
                // Pool Level 1 leaf item
                items.push_back(DropDownItem::Action(itemId, L"", plLabel, tag, isChecked, true, presGroupKey, L"", 1));
            }
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, anchorRc, items, [hWnd, sourceNodeIndex](const std::vector<DropDownItem>& updatedItems) {
            if (sourceNodeIndex >= 0 && sourceNodeIndex < (int)g_State.sourceNodes.size())
            {
                auto& n = g_State.sourceNodes[sourceNodeIndex];
                n.selectedPresetPools.clear();
                n.selectedPresetIndices.clear();

                for (const auto& itm : updatedItems)
                {
                    if (itm.isChecked && itm.id >= 2 && !itm.isGroupHeader && !itm.isHeader)
                    {
                        int raw = itm.id - 2;
                        int pIdx = raw / 100;
                        int plIdx = raw % 100;
                        n.selectedPresetPools.push_back({ pIdx, plIdx });
                        if (std::find(n.selectedPresetIndices.begin(), n.selectedPresetIndices.end(), pIdx) == n.selectedPresetIndices.end())
                        {
                            n.selectedPresetIndices.push_back(pIdx);
                        }
                    }
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        });
    }

    static void ShowBlueprintMultiDropdown(HWND hWnd)
    {
        TrainConfigManager::ScanTrainConfigs();
        if (TrainConfigManager::g_LoadedConfigsCache.empty())
        {
            ShowModernMessageBox(hWnd, L"No Train Configs available (.train files in TRAIN_CONFIGS).", L"Train Configs", MB_OK | MB_ICONINFORMATION);
            return;
        }

        size_t totalBlueprints = TrainConfigManager::g_LoadedConfigsCache.size();
        std::vector<PoolManager::PoolPreset> bpPresets;
        size_t totalAllPools = 0;

        for (size_t c = 0; c < totalBlueprints; ++c)
        {
            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
            TrainConfigManager::TrainBinding binding;
            TrainConfigManager::LoadTrainBinding(cfg, binding);
            PoolManager::PoolPreset bpPreset;
            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, bpPreset);
            totalAllPools += bpPreset.pools.size();
            bpPresets.push_back(bpPreset);
        }

        bool isAllSelected = (totalAllPools > 0 && (g_State.selectedBlueprintPools.empty() || g_State.selectedBlueprintPools.size() >= totalAllPools));
        bool hasAnySelected = (!g_State.selectedBlueprintPools.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Train Config Library (All Pools)", L"", isAllSelected, hasAnySelected));

        // Group blueprints by category
        std::vector<std::wstring> categoryOrder;
        std::map<std::wstring, std::vector<size_t>> categorizedConfigs;
        for (size_t c = 0; c < totalBlueprints; ++c)
        {
            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
            std::wstring cat = cfg.category.empty() ? L"General" : cfg.category;
            if (categorizedConfigs.find(cat) == categorizedConfigs.end())
            {
                categoryOrder.push_back(cat);
            }
            categorizedConfigs[cat].push_back(c);
        }

        for (size_t catIdx = 0; catIdx < categoryOrder.size(); ++catIdx)
        {
            const auto& cat = categoryOrder[catIdx];
            const auto& cfgIndices = categorizedConfigs[cat];

            int totalCatPools = 0;
            int selCatPools = 0;
            for (size_t cIdx : cfgIndices)
            {
                const auto& bpPreset = bpPresets[cIdx];
                totalCatPools += (int)bpPreset.pools.size();
                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    if (isAllSelected || std::find(g_State.selectedBlueprintPools.begin(), g_State.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != g_State.selectedBlueprintPools.end())
                    {
                        selCatPools++;
                    }
                }
            }

            bool catAll = (totalCatPools > 0 && selCatPools == totalCatPools);
            bool catIndeterminate = (selCatPools > 0 && !catAll);

            // Category Level 0 Group Header
            items.push_back(DropDownItem::GroupHeader(cat, L"\xE8B7", (int)cfgIndices.size(), false, (int)(10000 + catIdx), catAll, catIndeterminate, std::to_wstring(cfgIndices.size()) + L" Train Configs", L"", 0));

            for (size_t cIdx : cfgIndices)
            {
                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cIdx];
                const auto& bpPreset = bpPresets[cIdx];

                int bpPoolCount = (int)bpPreset.pools.size();
                int bpSelPoolCount = 0;
                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    if (isAllSelected || std::find(g_State.selectedBlueprintPools.begin(), g_State.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != g_State.selectedBlueprintPools.end())
                    {
                        bpSelPoolCount++;
                    }
                }
                bool bpAll = (bpPoolCount > 0 && bpSelPoolCount == bpPoolCount);
                bool bpIndet = (bpSelPoolCount > 0 && !bpAll);
                std::wstring bpGroupKey = L"BP:" + std::to_wstring(cIdx);
                std::wstring bpSecText = std::to_wstring(bpPoolCount) + L" pools";
                if (cfg.maxSpeedKmph > 0) bpSecText += L" • " + std::to_wstring((int)cfg.maxSpeedKmph) + L" km/h";

                // Blueprint Level 1 Group Header
                DropDownItem bpHdr = DropDownItem::GroupHeader(cfg.name, L"\xE7C0", bpPoolCount, false, (int)(20000 + cIdx), bpAll, bpIndet, bpSecText, cat, 1);
                bpHdr.group = bpGroupKey;
                items.push_back(bpHdr);

                for (size_t plIdx = 0; plIdx < bpPreset.pools.size(); ++plIdx)
                {
                    const auto& pl = bpPreset.pools[plIdx];
                    std::wstring plLabel = std::to_wstring(plIdx + 1) + L". " + (pl.name.empty() ? (L"Pool #" + std::to_wstring(plIdx + 1)) : pl.name);
                    std::wstring tag = std::to_wstring(pl.units.size()) + L" units";

                    int itemId = (int)(cIdx * 100 + plIdx + 2);
                    bool isChecked = isAllSelected || (std::find(g_State.selectedBlueprintPools.begin(), g_State.selectedBlueprintPools.end(), std::make_pair((int)cIdx, (int)plIdx)) != g_State.selectedBlueprintPools.end());
                    // Pool Level 2 leaf item
                    items.push_back(DropDownItem::Action(itemId, L"", plLabel, tag, isChecked, true, bpGroupKey, cat, 2));
                }
            }
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcGroupPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedBlueprintPools.clear();
            g_State.selectedBlueprintIndices.clear();

            for (const auto& itm : updatedItems)
            {
                if (itm.isChecked && itm.id >= 2 && !itm.isGroupHeader && !itm.isHeader)
                {
                    int raw = itm.id - 2;
                    int cIdx = raw / 100;
                    int plIdx = raw % 100;
                    g_State.selectedBlueprintPools.push_back({ cIdx, plIdx });
                    if (std::find(g_State.selectedBlueprintIndices.begin(), g_State.selectedBlueprintIndices.end(), cIdx) == g_State.selectedBlueprintIndices.end())
                    {
                        g_State.selectedBlueprintIndices.push_back(cIdx);
                    }
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        });
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
            std::wstring cat = grp.category.empty() ? L"General" : grp.category;
            std::wstring tag = cat + L" • " + std::to_wstring(grp.units.size()) + L" units";
            bool isChecked = isAllSelected || (std::find(g_State.selectedGroupIndices.begin(), g_State.selectedGroupIndices.end(), (int)i) != g_State.selectedGroupIndices.end());
            items.push_back(DropDownItem::Action((int)i + 2, L"", label, tag, isChecked, true));
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcGroupPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedGroupIndices.clear();
            for (size_t k = 1; k < updatedItems.size(); ++k)
            {
                if (updatedItems[k].isChecked && !updatedItems[k].isGroupHeader && !updatedItems[k].isHeader)
                {
                    g_State.selectedGroupIndices.push_back(updatedItems[k].id - 2);
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
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
        size_t totalAllPools = 0;
        for (const auto& p : PoolManager::g_PoolPresetsCache) totalAllPools += p.pools.size();

        bool isAllSelected = (totalAllPools > 0 && (g_State.selectedPresetPools.empty() || g_State.selectedPresetPools.size() >= totalAllPools));
        bool hasAnySelected = (!g_State.selectedPresetPools.empty() && !isAllSelected);

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(1, L"\xE735", L"Entire Preset Library (All Pools)", L"", isAllSelected, hasAnySelected));

        for (size_t i = 0; i < totalPresets; ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            int presPoolCount = (int)p.pools.size();
            int presSelPoolCount = 0;
            for (size_t plIdx = 0; plIdx < p.pools.size(); ++plIdx)
            {
                if (isAllSelected || std::find(g_State.selectedPresetPools.begin(), g_State.selectedPresetPools.end(), std::make_pair((int)i, (int)plIdx)) != g_State.selectedPresetPools.end())
                {
                    presSelPoolCount++;
                }
            }
            bool presAll = (presPoolCount > 0 && presSelPoolCount == presPoolCount);
            bool presIndet = (presSelPoolCount > 0 && !presAll);
            std::wstring presGroupKey = L"PRESET:" + std::to_wstring(i);
            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring presSecText = std::to_wstring(presPoolCount) + L" pools • " + std::to_wstring(totalU) + L" units";

            // Preset Level 0 Group Header
            DropDownItem presHdr = DropDownItem::GroupHeader(label, L"\xE71D", presPoolCount, false, (int)(30000 + i), presAll, presIndet, presSecText, L"", 0);
            presHdr.group = presGroupKey;
            items.push_back(presHdr);

            for (size_t plIdx = 0; plIdx < p.pools.size(); ++plIdx)
            {
                const auto& pl = p.pools[plIdx];
                std::wstring plLabel = std::to_wstring(plIdx + 1) + L". " + (pl.name.empty() ? (L"Pool #" + std::to_wstring(plIdx + 1)) : pl.name);
                std::wstring modeStr = (pl.pickMode == PoolManager::PoolPickMode::Random) ? L"Rnd" : L"Seq";
                std::wstring tag = std::to_wstring(pl.units.size()) + L" units • [" + modeStr + L"]";

                int itemId = (int)(i * 100 + plIdx + 2);
                bool isChecked = isAllSelected || (std::find(g_State.selectedPresetPools.begin(), g_State.selectedPresetPools.end(), std::make_pair((int)i, (int)plIdx)) != g_State.selectedPresetPools.end());
                // Pool Level 1 leaf item
                items.push_back(DropDownItem::Action(itemId, L"", plLabel, tag, isChecked, true, presGroupKey, L"", 1));
            }
        }

        CustomDropDownMenu::ShowMultiSelect(hWnd, g_State.rcGroupPicker, items, [hWnd](const std::vector<DropDownItem>& updatedItems) {
            g_State.selectedPresetPools.clear();
            g_State.selectedPresetIndices.clear();

            for (const auto& itm : updatedItems)
            {
                if (itm.isChecked && itm.id >= 2 && !itm.isGroupHeader && !itm.isHeader)
                {
                    int raw = itm.id - 2;
                    int pIdx = raw / 100;
                    int plIdx = raw % 100;
                    g_State.selectedPresetPools.push_back({ pIdx, plIdx });
                    if (std::find(g_State.selectedPresetIndices.begin(), g_State.selectedPresetIndices.end(), pIdx) == g_State.selectedPresetIndices.end())
                    {
                        g_State.selectedPresetIndices.push_back(pIdx);
                    }
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        });
    }

    static void ShowPresetDropdown(HWND hWnd)
    {
        PoolManager::InitializePoolPresets();
        TrainConfigManager::ScanTrainConfigs();
        if (PoolManager::g_PoolPresetsCache.empty() && TrainConfigManager::g_LoadedConfigsCache.empty()) return;

        std::vector<DropDownItem> items;
        items.push_back(DropDownItem::Header(L"CUSTOM PRESETS"));

        for (size_t i = 0; i < PoolManager::g_PoolPresetsCache.size(); ++i)
        {
            const auto& p = PoolManager::g_PoolPresetsCache[i];
            int totalU = 0;
            for (const auto& pl : p.pools) totalU += (int)pl.units.size();

            std::wstring label = p.presetName.empty() ? (L"Preset " + std::to_wstring(i + 1)) : p.presetName;
            std::wstring tag = std::to_wstring(p.pools.size()) + L" pools • " + std::to_wstring(totalU) + L" units";
            bool isCurrent = (!g_State.isUsingTrainConfig && (int)i == g_State.selectedPresetIdx);
            items.push_back(DropDownItem::Action((int)i + 1, isCurrent ? L"\xE73E" : L"\xE71D", label, tag, isCurrent, true));
        }

        if (!TrainConfigManager::g_LoadedConfigsCache.empty())
        {
            items.push_back(DropDownItem::Separator());
            items.push_back(DropDownItem::Header(L"TRAIN BLUEPRINTS (.train)"));

            std::vector<std::wstring> categoryOrder;
            std::map<std::wstring, std::vector<size_t>> categorizedConfigs;
            for (size_t c = 0; c < TrainConfigManager::g_LoadedConfigsCache.size(); ++c)
            {
                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
                std::wstring cat = cfg.category.empty() ? L"General" : cfg.category;
                if (categorizedConfigs.find(cat) == categorizedConfigs.end())
                {
                    categoryOrder.push_back(cat);
                }
                categorizedConfigs[cat].push_back(c);
            }

            for (const auto& cat : categoryOrder)
            {
                const auto& cfgIndices = categorizedConfigs[cat];
                items.push_back(DropDownItem::GroupHeader(cat, L"\xE8B7", (int)cfgIndices.size(), false, 0, false, false, std::to_wstring(cfgIndices.size()) + L" Blueprints"));

                for (size_t cIdx : cfgIndices)
                {
                    const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cIdx];
                    std::wstring tag = cfg.category;
                    if (cfg.maxSpeedKmph > 0) tag += L" • " + std::to_wstring((int)cfg.maxSpeedKmph) + L" km/h";
                    bool isCurrent = (g_State.isUsingTrainConfig && g_State.activeTrainConfigPath == cfg.filePath);
                    items.push_back(DropDownItem::Action(1000 + (int)cIdx, isCurrent ? L"\xE73E" : L"\xE7C0", cfg.name, tag, isCurrent, true, cat));
                }
            }
        }

        int currentSelId = (!g_State.isUsingTrainConfig) ? (g_State.selectedPresetIdx + 1) : 0;
        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, g_State.rcPresetPicker, items, currentSelId);
        if (chosen >= 1 && chosen <= (int)PoolManager::g_PoolPresetsCache.size())
        {
            g_State.isUsingTrainConfig = false;
            g_State.activeTrainConfigPath = L"";
            g_State.selectedPresetIdx = chosen - 1;
            g_State.selectedPoolIndices.clear();
            PoolManager::PoolPreset* pNew = GetActiveMutatorPreset();
            if (pNew)
            {
                for (size_t i = 0; i < pNew->pools.size(); ++i)
                {
                    g_State.selectedPoolIndices.push_back((int)i);
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        }
        else if (chosen >= 1000 && chosen < 1000 + (int)TrainConfigManager::g_LoadedConfigsCache.size())
        {
            int cfgIdx = chosen - 1000;
            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cfgIdx];
            TrainConfigManager::TrainBinding binding;
            TrainConfigManager::LoadTrainBinding(cfg, binding);

            g_State.isUsingTrainConfig = true;
            g_State.activeTrainConfigPath = cfg.filePath;
            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, g_State.activeTrainConfigPreset);

            g_State.selectedPoolIndices.clear();
            for (size_t i = 0; i < g_State.activeTrainConfigPreset.pools.size(); ++i)
            {
                g_State.selectedPoolIndices.push_back((int)i);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
        }
    }

    static void ShowPoolDropdown(HWND hWnd)
    {
        PoolManager::PoolPreset* pPreset = GetActiveMutatorPreset();
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
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
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
        if (g_State.activeTab == 2)
        {
            EnsureDefaultSourceNodes();
            PoolMutator::MutatorOptions repOpts;
            repOpts.mode = PoolMutator::MutatorMode::ReplaceBroken;
            repOpts.sourceNodes = g_State.sourceNodes;

            PoolMutator::MutatorResult res;
            bool ok = ApplyPoolMutationToSessions(
                g_State.hParent,
                g_State.targetConsistPaths,
                g_State.targetUnitIndices,
                repOpts,
                res,
                &g_State.brokenConsists
            );

            if (ok && res.success)
            {
                std::wstring successMsg = L"Consist Repair & Rebuild Operation Completed!\n\n";
                successMsg += L"Processed " + std::to_wstring(res.processedCount) + L" consist(s).\n";
                if (res.repairedUnitsCount > 0)
                {
                    successMsg += L"Replaced / Repaired " + std::to_wstring(res.repairedUnitsCount) + L" unit(s).\n";
                }
                successMsg += L"\nChanges staged in session memory (●). Use 'Save Consist(s)' (Ctrl+S) when ready to commit to disk.";

                ShowModernMessageBox(hWnd, successMsg.c_str(), L"Consist Repair & Rebuild", MB_OK | MB_ICONINFORMATION);

                // Refresh broken list in dialog
                RefreshBrokenConsists();

                if (g_State.hParent && IsWindow(g_State.hParent))
                {
                    InvalidateRect(g_State.hParent, NULL, FALSE);
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            else
            {
                std::wstring err = res.errorMessage.empty() ? L"No broken units were selected or an error occurred." : res.errorMessage;
                ShowModernMessageBox(hWnd, err.c_str(), L"Repair Error", MB_OK | MB_ICONERROR);
            }
            return;
        }

        PoolMutator::MutatorOptions opts;
        opts.mode = (g_State.activeTab == 0) ? PoolMutator::MutatorMode::MutateConsists : PoolMutator::MutatorMode::InsertUnits;
        opts.presetIndex = g_State.selectedPresetIdx;
        opts.selectedPoolIndices = g_State.selectedPoolIndices;
        opts.selectedPresetIndices = g_State.selectedPresetIndices;
        opts.selectedBlueprintIndices = g_State.selectedBlueprintIndices;
        opts.selectedPresetPools = g_State.selectedPresetPools;
        opts.selectedBlueprintPools = g_State.selectedBlueprintPools;
        opts.selectedReplacementGroupIndices = g_State.selectedGroupIndices;
        opts.insertSource = g_State.insertSource;
        opts.replacementGroupIndex = g_State.selectedGroupIdx;
        if (g_State.isUsingTrainConfig)
        {
            opts.hasCustomPresetOverride = true;
            opts.customPresetOverride = g_State.activeTrainConfigPreset;
        }

        PoolManager::PoolPreset* pPres = GetActiveMutatorPreset();
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
            else if (g_State.activeTab == 1)
            {
                g_State.mode = PoolMutator::MutatorMode::InsertUnits;
            }
            else if (g_State.activeTab == 2)
            {
                g_State.mode = PoolMutator::MutatorMode::ReplaceBroken;
                EnsureDefaultSourceNodes();
                RefreshBrokenConsists();
            }
            UpdateControlPositions(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (g_State.activeTab == 2)
            {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                g_State.brokenScrollY -= (delta / 120) * 32;
                if (g_State.brokenScrollY < 0) g_State.brokenScrollY = 0;
                int maxScroll = (std::max)(0, g_State.brokenTotalContentH - 300);
                if (g_State.brokenScrollY > maxScroll) g_State.brokenScrollY = maxScroll;
                InvalidateRect(hWnd, &g_State.rcBrokenTreeCard, FALSE);
                return 0;
            }
            break;
        }

        case WM_SIZE:
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (g_State.hTitleBar && IsWindow(g_State.hTitleBar))
            {
                SetWindowPos(g_State.hTitleBar, NULL, 0, 0, w, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS);
                InvalidateRect(g_State.hTitleBar, NULL, FALSE);
                UpdateWindow(g_State.hTitleBar);
            }
            UpdateControlPositions(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);
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
            g_State.hFontIconMed = CreateCustomFont(15, FW_NORMAL, L"Segoe Fluent Icons");
            if (!g_State.hFontIconMed) g_State.hFontIconMed = CreateCustomFont(15, FW_NORMAL, L"Segoe MDL2 Assets");
            g_State.hFontIconLg = CreateCustomFont(24, FW_NORMAL, L"Segoe Fluent Icons");
            if (!g_State.hFontIconLg) g_State.hFontIconLg = CreateCustomFont(24, FW_NORMAL, L"Segoe MDL2 Assets");

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int w = rcClient.right > 0 ? rcClient.right : 600;

            std::vector<TitleBarTabItem> mutatorTabs = {
                { L"\xE7B8", L"Mutate Consist" },
                { L"\xE710", L"Insert Units" },
                { L"\xE896", L"Batch Consist Editing" }
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
                PoolManager::PoolPreset* pInitPres = GetActiveMutatorPreset();
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

            EnsureDefaultSourceNodes();
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

            bool hPreset = (g_State.activeTab == 0) && PtInRect(&g_State.rcPresetPicker, pt);
            bool hPool = (g_State.activeTab == 0) && PtInRect(&g_State.rcPoolPicker, pt);
            bool hGroup = (g_State.activeTab == 1) && PtInRect(&g_State.rcGroupPicker, pt);
            bool hRadSrc0 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioInsertSrc0, pt);
            bool hRadSrc1 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioInsertSrc1, pt);
            bool hRadSrc2 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioInsertSrc2, pt);
            bool hRadC0 = (g_State.activeTab == 0) && PtInRect(&g_State.rcRadioCount0, pt);
            bool hRadC1 = (g_State.activeTab == 0) && PtInRect(&g_State.rcRadioCount1, pt);
            bool hRadC2 = (g_State.activeTab == 0) && PtInRect(&g_State.rcRadioCount2, pt);
            bool hRadP0 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioPos0, pt);
            bool hRadP1 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioPos1, pt);
            bool hRadP2 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioPos2, pt);
            bool hRadP3 = (g_State.activeTab == 1) && PtInRect(&g_State.rcRadioPos3, pt);
            bool hTab2SelBrk = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2BtnSelectBroken, pt);
            bool hTab2ScanLib = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2BtnScanLibrary, pt);
            bool hAddSrc = (g_State.activeTab == 2) && PtInRect(&g_State.rcAddSourceBtn, pt);

            bool cardsChanged = false;
            if (g_State.activeTab == 2)
            {
                for (auto& card : g_State.sourceCardUIs)
                {
                    bool hPick = PtInRect(&card.rcPicker, pt);
                    bool hTog = PtInRect(&card.rcTypeToggle, pt);
                    bool hDel = PtInRect(&card.rcDeleteBtn, pt);
                    if (card.isHoverPicker != hPick || card.isHoverToggle != hTog || card.isHoverDelete != hDel)
                    {
                        card.isHoverPicker = hPick;
                        card.isHoverToggle = hTog;
                        card.isHoverDelete = hDel;
                        cardsChanged = true;
                    }
                }
            }

            bool hCheck = (g_State.activeTab != 2) && PtInRect(&g_State.rcCheckboxClones, pt);
            bool hApply = PtInRect(&g_State.rcApplyBtn, pt);
            bool hCancel = PtInRect(&g_State.rcCancelBtn, pt);

            if (hPreset != g_State.isHoverPreset || hPool != g_State.isHoverPool ||
                hGroup != g_State.isHoverGroup || hRadSrc0 != g_State.isHoverRadioSrc0 ||
                hRadSrc1 != g_State.isHoverRadioSrc1 || hRadSrc2 != g_State.isHoverRadioSrc2 ||
                hRadC0 != g_State.isHoverRadioCount0 || hRadC1 != g_State.isHoverRadioCount1 ||
                hRadC2 != g_State.isHoverRadioCount2 || hRadP0 != g_State.isHoverRadioPos0 ||
                hRadP1 != g_State.isHoverRadioPos1 || hRadP2 != g_State.isHoverRadioPos2 ||
                hRadP3 != g_State.isHoverRadioPos3 || hCheck != g_State.isHoverCheckClones ||
                hAddSrc != g_State.isHoverAddSourceBtn || cardsChanged ||
                hTab2SelBrk != g_State.isHoverTab2BtnSelectBroken || hTab2ScanLib != g_State.isHoverTab2BtnScanLibrary ||
                hApply != g_State.isHoverApply || hCancel != g_State.isHoverCancel)
            {
                g_State.isHoverPreset = hPreset;
                g_State.isHoverPool = hPool;
                g_State.isHoverGroup = hGroup;
                g_State.isHoverRadioSrc0 = hRadSrc0;
                g_State.isHoverRadioSrc1 = hRadSrc1;
                g_State.isHoverRadioSrc2 = hRadSrc2;
                g_State.isHoverRadioCount0 = hRadC0;
                g_State.isHoverRadioCount1 = hRadC1;
                g_State.isHoverRadioCount2 = hRadC2;
                g_State.isHoverRadioPos0 = hRadP0;
                g_State.isHoverRadioPos1 = hRadP1;
                g_State.isHoverRadioPos2 = hRadP2;
                g_State.isHoverRadioPos3 = hRadP3;
                g_State.isHoverCheckClones = hCheck;
                g_State.isHoverAddSourceBtn = hAddSrc;
                g_State.isHoverTab2BtnSelectBroken = hTab2SelBrk;
                g_State.isHoverTab2BtnScanLibrary = hTab2ScanLib;
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
            g_State.isHoverRadioSrc2 = false;
            g_State.isHoverRadioCount0 = false;
            g_State.isHoverRadioCount1 = false;
            g_State.isHoverRadioCount2 = false;
            g_State.isHoverRadioPos0 = false;
            g_State.isHoverRadioPos1 = false;
            g_State.isHoverRadioPos2 = false;
            g_State.isHoverRadioPos3 = false;
            g_State.isHoverCheckClones = false;
            g_State.isHoverAddSourceBtn = false;
            for (auto& card : g_State.sourceCardUIs)
            {
                card.isHoverPicker = false;
                card.isHoverToggle = false;
                card.isHoverDelete = false;
            }
            g_State.isHoverTab2BtnSelectBroken = false;
            g_State.isHoverTab2BtnScanLibrary = false;
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

            // ==========================================
            // TAB 0: MUTATE CONSISTS CLICK TARGETS
            // ==========================================
            if (g_State.activeTab == 0)
            {
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
            // ==========================================
            // TAB 1: INSERT UNITS CLICK TARGETS
            // ==========================================
            else if (g_State.activeTab == 1)
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
                if (PtInRect(&g_State.rcRadioInsertSrc2, pt))
                {
                    g_State.insertSource = PoolMutator::InsertSource::TrainBlueprint;
                    UpdateControlPositions(hWnd);
                    return 0;
                }
                if (PtInRect(&g_State.rcGroupPicker, pt))
                {
                    if (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup)
                    {
                        ShowGroupDropdown(hWnd);
                    }
                    else if (g_State.insertSource == PoolMutator::InsertSource::TrainBlueprint)
                    {
                        ShowBlueprintMultiDropdown(hWnd);
                    }
                    else
                    {
                        ShowPresetMultiDropdown(hWnd);
                    }
                    return 0;
                }
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
            // ==========================================
            // TAB 2: REPLACE / REPAIR BROKEN UNITS
            // ==========================================
            else if (g_State.activeTab == 2)
            {
                // Master Checkbox (Select All / Deselect All)
                if (PtInRect(&g_State.rcMasterBrokenCheck, pt))
                {
                    int totalItems = 0;
                    int selectedItems = 0;
                    for (const auto& bc : g_State.brokenConsists)
                    {
                        if (bc.brokenUnits.empty())
                        {
                            totalItems++;
                            if (bc.isSelected) selectedItems++;
                        }
                        else
                        {
                            for (const auto& bu : bc.brokenUnits)
                            {
                                totalItems++;
                                if (bu.isSelected) selectedItems++;
                            }
                        }
                    }
                    bool newSelState = (totalItems == 0 || selectedItems < totalItems);
                    for (auto& bc : g_State.brokenConsists)
                    {
                        bc.isSelected = newSelState;
                        for (auto& bu : bc.brokenUnits)
                        {
                            bu.isSelected = newSelState;
                        }
                    }
                    InvalidateRect(hWnd, &g_State.rcBrokenTreeCard, FALSE);
                    return 0;
                }

                // Select Broken Units Quick Action Button
                if (PtInRect(&g_State.rcTab2BtnSelectBroken, pt))
                {
                    for (auto& bc : g_State.brokenConsists)
                    {
                        if (bc.brokenUnits.empty())
                        {
                            bc.isSelected = true;
                        }
                        else
                        {
                            bool anyBroken = false;
                            for (auto& bu : bc.brokenUnits)
                            {
                                bu.isSelected = bu.isBroken;
                                if (bu.isSelected) anyBroken = true;
                            }
                            bc.isSelected = anyBroken;
                        }
                    }
                    InvalidateRect(hWnd, &g_State.rcBrokenTreeCard, FALSE);
                    return 0;
                }

                // Scan Entire Library Action Button (Empty State)
                if (PtInRect(&g_State.rcTab2BtnScanLibrary, pt))
                {
                    RefreshBrokenConsists(true);
                    InvalidateRect(hWnd, NULL, TRUE);
                    return 0;
                }

                // Tree List clicks
                if (PtInRect(&g_State.rcBrokenTreeList, pt))
                {
                    int curY = g_State.rcBrokenTreeList.top - g_State.brokenScrollY;
                    for (size_t cIdx = 0; cIdx < g_State.brokenConsists.size(); ++cIdx)
                    {
                        auto& bcon = g_State.brokenConsists[cIdx];

                        int rowH = 26;
                        RECT rcConRow = { g_State.rcBrokenTreeList.left + 4, curY, g_State.rcBrokenTreeList.right - 4, curY + rowH };

                        if (PtInRect(&rcConRow, pt))
                        {
                            RECT rcChev = { rcConRow.left + 4, rcConRow.top, rcConRow.left + 20, rcConRow.bottom };
                            RECT rcConChk = { rcConRow.left + 24, rcConRow.top + 5, rcConRow.left + 40, rcConRow.top + 21 };
                            RECT rcConMode = { rcConRow.right - 164, rcConRow.top + 3, rcConRow.right - 84, rcConRow.top + 23 };
                            RECT rcConPill = { rcConRow.right - 80, rcConRow.top + 3, rcConRow.right - 4, rcConRow.top + 23 };

                            if (PtInRect(&rcChev, pt))
                            {
                                bcon.isExpanded = !bcon.isExpanded;
                                InvalidateRect(hWnd, &g_State.rcBrokenTreeCard, FALSE);
                                return 0;
                            }
                            else if (PtInRect(&rcConMode, pt))
                            {
                                if (bcon.brokenUnits.empty())
                                {
                                    ShowModernMessageBox(hWnd, L"This consist is currently empty (0 units).\n\nReplace mode cannot be used because there are no existing rolling stock units to replace. Rebuild Consist mode will generate new rolling stock from scratch using the assigned source rules.", L"Empty Consist", MB_OK | MB_ICONINFORMATION);
                                    bcon.actionExecutionMode = 1;
                                    return 0;
                                }
                                bcon.actionExecutionMode = (bcon.actionExecutionMode == 0) ? 1 : 0;
                                if (bcon.actionExecutionMode == 1)
                                {
                                    const auto* pCur = FindSourceNodeById(bcon.assignedSourceId);
                                    if (!pCur || pCur->sourceType == 0)
                                    {
                                        bcon.assignedSourceId = CycleNextSourceIdForConsist(bcon.assignedSourceId, 1);
                                        for (auto& bu : bcon.brokenUnits) bu.assignedSourceId = bcon.assignedSourceId;
                                    }
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                                return 0;
                            }
                            else if (PtInRect(&rcConPill, pt))
                            {
                                bcon.assignedSourceId = CycleNextSourceIdForConsist(bcon.assignedSourceId, bcon.actionExecutionMode);
                                for (auto& bu : bcon.brokenUnits)
                                {
                                    bu.assignedSourceId = bcon.assignedSourceId;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                                return 0;
                            }
                            else
                            {
                                bcon.isSelected = !bcon.isSelected;
                                for (auto& bu : bcon.brokenUnits)
                                {
                                    bu.isSelected = bcon.isSelected;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                                return 0;
                            }
                        }
                        curY += rowH + 4;

                        if (bcon.isExpanded && (bcon.actionExecutionMode == 1 || bcon.brokenUnits.empty()))
                        {
                            curY += 48 + 4;
                        }
                        else if (bcon.isExpanded && bcon.actionExecutionMode == 0)
                        {
                            for (size_t uIdx = 0; uIdx < bcon.brokenUnits.size(); ++uIdx)
                            {
                                auto& bu = bcon.brokenUnits[uIdx];
                                int uRowH = 22;
                                RECT rcURow = { g_State.rcBrokenTreeList.left + 28, curY, g_State.rcBrokenTreeList.right - 8, curY + uRowH };
                                if (PtInRect(&rcURow, pt))
                                {
                                    RECT rcUPill = { rcURow.right - 92, rcURow.top + 2, rcURow.right - 58, rcURow.top + 20 };
                                    if (PtInRect(&rcUPill, pt))
                                    {
                                        bu.assignedSourceId = CycleNextSourceId(bu.assignedSourceId);
                                        InvalidateRect(hWnd, NULL, FALSE);
                                        return 0;
                                    }
                                    else
                                    {
                                        bu.isSelected = !bu.isSelected;
                                        bool hasAnySel = false;
                                        for (const auto& u : bcon.brokenUnits) { if (u.isSelected) { hasAnySel = true; break; } }
                                        bcon.isSelected = hasAnySel;
                                        InvalidateRect(hWnd, NULL, FALSE);
                                        return 0;
                                    }
                                }
                                curY += uRowH + 2;
                            }
                        }
                    }
                }

                // Add Source Button Click
                if (PtInRect(&g_State.rcAddSourceBtn, pt))
                {
                    PoolMutator::SourceNodeConfig newSrc;
                    newSrc.sourceId = g_State.nextSourceNodeId++;
                    wchar_t letter = L'A' + (wchar_t)(g_State.sourceNodes.size() % 26);
                    newSrc.name = std::wstring(L"Source ") + letter;
                    newSrc.sourceType = (g_State.sourceNodes.size() % 2 == 0) ? 0 : 1;
                    newSrc.color = GetSourceColorByIndex((int)g_State.sourceNodes.size());
                    g_State.sourceNodes.push_back(newSrc);
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Source Cards Clicks (Type Toggle, Dropdown Picker, Delete Button)
                for (size_t k = 0; k < g_State.sourceCardUIs.size(); ++k)
                {
                    const auto& cardUI = g_State.sourceCardUIs[k];
                    if (cardUI.sourceId < 0 || k >= g_State.sourceNodes.size()) continue;
                    auto& node = g_State.sourceNodes[k];

                    if (PtInRect(&cardUI.rcTypeToggle, pt))
                    {
                        int toggleW = cardUI.rcTypeToggle.right - cardUI.rcTypeToggle.left;
                        int segW = toggleW / 3;
                        int segIdx = (pt.x - cardUI.rcTypeToggle.left) / (segW > 0 ? segW : 1);
                        if (segIdx < 0) segIdx = 0;
                        if (segIdx > 2) segIdx = 2;
                        node.sourceType = segIdx;
                        node.selectedGroupIndices.clear();
                        node.selectedPresetIndices.clear();
                        node.selectedBlueprintIndices.clear();

                        if (node.sourceType == 0)
                        {
                            for (auto& bc : g_State.brokenConsists)
                            {
                                if (bc.actionExecutionMode == 1 && bc.assignedSourceId == node.sourceId)
                                {
                                    bc.assignedSourceId = CycleNextSourceIdForConsist(bc.assignedSourceId, 1);
                                    for (auto& bu : bc.brokenUnits) bu.assignedSourceId = bc.assignedSourceId;
                                }
                            }
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    if (PtInRect(&cardUI.rcDeleteBtn, pt) && g_State.sourceNodes.size() > 1)
                    {
                        int delId = node.sourceId;
                        g_State.sourceNodes.erase(g_State.sourceNodes.begin() + k);
                        for (auto& bc : g_State.brokenConsists)
                        {
                            if (bc.assignedSourceId == delId)
                            {
                                bc.assignedSourceId = CycleNextSourceIdForConsist(delId, bc.actionExecutionMode);
                                for (auto& bu : bc.brokenUnits) bu.assignedSourceId = bc.assignedSourceId;
                            }
                        }
                        InvalidateRect(hWnd, NULL, FALSE);
                        return 0;
                    }
                    if (PtInRect(&cardUI.rcPicker, pt))
                    {
                        if (node.sourceType == 0)
                        {
                            ShowGroupDropdownForSource(hWnd, (int)k, cardUI.rcPicker);
                        }
                        else if (node.sourceType == 1)
                        {
                            ShowPresetDropdownForSource(hWnd, (int)k, cardUI.rcPicker);
                        }
                        else // sourceType == 2
                        {
                            ShowBlueprintDropdownForSource(hWnd, (int)k, cardUI.rcPicker);
                        }
                        return 0;
                    }
                }
            }

            // Common controls
            if (g_State.activeTab != 2 && PtInRect(&g_State.rcCheckboxClones, pt))
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

            SelectObject(hmemDC, g_State.hFontIconMed);
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, accentCol);
            RECT rcIcon = { 20, toolbarY, 44, toolbarY + toolbarH };
            const wchar_t* tabIcon = (g_State.activeTab == 0) ? L"\xE7B8" : ((g_State.activeTab == 1) ? L"\xE710" : L"\xE896");
            DrawTextW(hmemDC, tabIcon, -1, &rcIcon, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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
            else if (g_State.activeTab == 1)
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
            else // Tab 2: Replace / Repair Broken Units
            {
                int totalConsists = 0;
                int totalUnits = 0;
                int totalBrokenUnits = 0;
                int selectedUnits = 0;
                int totalEmptyConsists = 0;
                for (const auto& bc : g_State.brokenConsists)
                {
                    totalConsists++;
                    if (bc.brokenUnits.empty())
                    {
                        totalEmptyConsists++;
                        if (bc.isSelected) selectedUnits++;
                    }
                    else
                    {
                        for (const auto& bu : bc.brokenUnits)
                        {
                            totalUnits++;
                            if (bu.isBroken) totalBrokenUnits++;
                            if (bc.isSelected && bu.isSelected) selectedUnits++;
                        }
                    }
                }
                if (totalConsists == 0)
                {
                    targetSummary = L"No Consist(s) Selected";
                }
                else
                {
                    targetSummary = L"Target: " + std::to_wstring(totalConsists) + L" consist(s)";
                    if (totalUnits > 0 || totalEmptyConsists > 0)
                    {
                        targetSummary += L" \x2022 Selected " + std::to_wstring(selectedUnits) + L" unit(s)/consist(s)";
                    }
                    if (totalBrokenUnits > 0 || totalEmptyConsists > 0)
                    {
                        targetSummary += L" (";
                        if (totalBrokenUnits > 0) targetSummary += std::to_wstring(totalBrokenUnits) + L" missing/broken";
                        if (totalBrokenUnits > 0 && totalEmptyConsists > 0) targetSummary += L", ";
                        if (totalEmptyConsists > 0) targetSummary += std::to_wstring(totalEmptyConsists) + L" empty";
                        targetSummary += L")";
                    }
                    else
                    {
                        targetSummary += L" (All healthy)";
                    }
                }
            }

            SelectObject(hmemDC, g_State.hFontBold);
            SIZE sumSz = { 0 };
            GetTextExtentPoint32W(hmemDC, targetSummary.c_str(), (int)targetSummary.length(), &sumSz);
            int statusW = (std::max)(180, (int)sumSz.cx + 10);
            if (statusW > rcClient.right - 180) statusW = rcClient.right - 180;

            RECT rcStatus = { rcClient.right - statusW - 20, toolbarY, rcClient.right - 20, toolbarY + toolbarH };
            RECT rcTitle = { 54, toolbarY + 5, rcStatus.left - 12, toolbarY + 24 };
            RECT rcSub = { 54, toolbarY + 24, rcStatus.left - 12, toolbarY + 42 };

            SetTextColor(hmemDC, textPrimary);
            const wchar_t* tabTitle = (g_State.activeTab == 0) ? L"Whole Consist Pool Overhaul / Mutation" :
                                      ((g_State.activeTab == 1) ? L"Position-Based Unit Injection" : L"Batch Consist Editing");
            DrawTextW(hmemDC, tabTitle, -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            SelectObject(hmemDC, g_State.hFontSmall);
            SetTextColor(hmemDC, textSecondary);
            const wchar_t* tabSub = (g_State.activeTab == 0) ? L"Regenerate and vary entire consist file(s) drawn from selected Pool Preset" :
                                    ((g_State.activeTab == 1) ? L"Inject new units into consist file(s) at Head, Behind Locos, Tail, or Specific Index" :
                                     L"Batch repair broken units or rebuild entire consist files from presets and train configurations");
            DrawTextW(hmemDC, tabSub, -1, &rcSub, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // Right-aligned status: Target context summary
            SelectObject(hmemDC, g_State.hFontBold);
            SetTextColor(hmemDC, accentCol);
            DrawTextW(hmemDC, targetSummary.c_str(), -1, &rcStatus, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // =========================================================================
            // TAB 0: MUTATE CONSIST
            // =========================================================================
            if (g_State.activeTab == 0)
            {
                g_State.rcGroupPicker = { 0 };
                g_State.rcRadioInsertSrc0 = { 0 };
                g_State.rcRadioInsertSrc1 = { 0 };
                g_State.rcRadioPos0 = { 0 };
                g_State.rcRadioPos1 = { 0 };
                g_State.rcRadioPos2 = { 0 };
                g_State.rcRadioPos3 = { 0 };

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
                PoolManager::PoolPreset* curPres = GetActiveMutatorPreset();
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

                int colSplitX = (rcClient.right - 60) / 2 + 30;

                // Count 0: Keep Original
                g_State.rcRadioCount0 = { 30, 254, colSplitX - 10, 282 };
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
                RECT rcCText0 = { 56, 254, colSplitX - 10, 282 };
                DrawTextW(hmemDC, L"Keep Original Unit Count", -1, &rcCText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Count 1: Dynamic Pool Rules
                g_State.rcRadioCount1 = { colSplitX + 10, 254, rcClient.right - 30, 282 };
                bool isC1 = (g_State.countMode == PoolMutator::CountMode::DynamicPoolRules);
                RECT rcCCount1 = { colSplitX + 14, 260, colSplitX + 28, 274 };
                HBRUSH hbrC1 = CreateSolidBrush(cardBgCol);
                HPEN hpenC1 = CreatePen(PS_SOLID, 1, isC1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrC1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenC1);
                Ellipse(hmemDC, rcCCount1.left, rcCCount1.top, rcCCount1.right, rcCCount1.bottom);
                if (isC1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCCount1.left + 3, rcCCount1.top + 3, rcCCount1.right - 3, rcCCount1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrC1); DeleteObject(hpenC1);
                RECT rcCText1 = { colSplitX + 36, 254, rcClient.right - 30, 282 };
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
            else if (g_State.activeTab == 1)
            {
                g_State.rcPresetPicker = { 0 };
                g_State.rcPoolPicker = { 0 };
                g_State.rcRadioCount0 = { 0 };
                g_State.rcRadioCount1 = { 0 };
                g_State.rcRadioCount2 = { 0 };

                int colSplitX = (rcClient.right - 60) / 2 + 30;

                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);

                RECT rcHeaderSrc = { 30, 126, rcClient.right - 30, 146 };
                DrawTextW(hmemDC, L"Select Insertion Source:", -1, &rcHeaderSrc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

                // Source Radios: Favourite Group vs Pool Preset vs Train Blueprint
                int radioW = (rcClient.right - 60) / 3;
                g_State.rcRadioInsertSrc0 = { 30, 148, 30 + radioW - 10, 174 };
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
                RECT rcSText0 = { 56, 148, 30 + radioW - 10, 174 };
                DrawTextW(hmemDC, L"Favourite Group", -1, &rcSText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                g_State.rcRadioInsertSrc1 = { 30 + radioW, 148, 30 + 2 * radioW - 10, 174 };
                bool isSrc1 = (g_State.insertSource == PoolMutator::InsertSource::PoolPreset);
                RECT rcCSrc1 = { 30 + radioW + 4, 154, 30 + radioW + 18, 168 };
                HBRUSH hbrSrc1 = CreateSolidBrush(cardBgCol);
                HPEN hpenSrc1 = CreatePen(PS_SOLID, 1, isSrc1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrSrc1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenSrc1);
                Ellipse(hmemDC, rcCSrc1.left, rcCSrc1.top, rcCSrc1.right, rcCSrc1.bottom);
                if (isSrc1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCSrc1.left + 3, rcCSrc1.top + 3, rcCSrc1.right - 3, rcCSrc1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrSrc1); DeleteObject(hpenSrc1);
                RECT rcSText1 = { 30 + radioW + 26, 148, 30 + 2 * radioW - 10, 174 };
                DrawTextW(hmemDC, L"Pool Preset Rules", -1, &rcSText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                g_State.rcRadioInsertSrc2 = { 30 + 2 * radioW, 148, rcClient.right - 30, 174 };
                bool isSrc2 = (g_State.insertSource == PoolMutator::InsertSource::TrainBlueprint);
                RECT rcCSrc2 = { 30 + 2 * radioW + 4, 154, 30 + 2 * radioW + 18, 168 };
                HBRUSH hbrSrc2 = CreateSolidBrush(cardBgCol);
                HPEN hpenSrc2 = CreatePen(PS_SOLID, 1, isSrc2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrSrc2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenSrc2);
                Ellipse(hmemDC, rcCSrc2.left, rcCSrc2.top, rcCSrc2.right, rcCSrc2.bottom);
                if (isSrc2) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCSrc2.left + 3, rcCSrc2.top + 3, rcCSrc2.right - 3, rcCSrc2.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrSrc2); DeleteObject(hpenSrc2);
                RECT rcSText2 = { 30 + 2 * radioW + 26, 148, rcClient.right - 30, 174 };
                DrawTextW(hmemDC, L"Train Configs", -1, &rcSText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Dropdown Row
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                RECT rcLblSource = { 30, 182, 140, 210 };
                const wchar_t* lblSource = (g_State.insertSource == PoolMutator::InsertSource::FavouriteGroup) ? L"Select Group(s):" :
                    ((g_State.insertSource == PoolMutator::InsertSource::TrainBlueprint) ? L"Select Train Config(s):" : L"Select Preset(s):");
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
                else if (g_State.insertSource == PoolMutator::InsertSource::TrainBlueprint)
                {
                    TrainConfigManager::ScanTrainConfigs();
                    size_t totalBp = TrainConfigManager::g_LoadedConfigsCache.size();
                    if (totalBp == 0)
                    {
                        sourceStr = L"No Train Configs Available";
                    }
                    else
                    {
                        size_t totalAllPools = 0;
                        std::vector<PoolManager::PoolPreset> bpPresets;
                        for (size_t c = 0; c < totalBp; ++c)
                        {
                            const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];
                            TrainConfigManager::TrainBinding binding;
                            TrainConfigManager::LoadTrainBinding(cfg, binding);
                            PoolManager::PoolPreset bpPreset;
                            TrainConfigManager::BuildPresetFromConfigAndBinding(cfg, binding, bpPreset);
                            totalAllPools += bpPreset.pools.size();
                            bpPresets.push_back(bpPreset);
                        }

                        bool isAll = (totalAllPools > 0 && (g_State.selectedBlueprintPools.empty() || g_State.selectedBlueprintPools.size() >= totalAllPools));
                        if (isAll)
                        {
                            int totalU = 0;
                            for (const auto& bp : bpPresets)
                                for (const auto& pl : bp.pools) totalU += (int)pl.units.size();
                            sourceStr = L"★ Entire Train Config Library (All " + std::to_wstring(totalBp) + L" Train Configs • " + std::to_wstring(totalU) + L" units)";
                        }
                        else if (g_State.selectedBlueprintPools.size() == 1)
                        {
                            auto p = g_State.selectedBlueprintPools[0];
                            if (p.first >= 0 && p.first < (int)totalBp)
                            {
                                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[p.first];
                                const auto& bp = bpPresets[p.first];
                                std::wstring plName = (p.second >= 0 && p.second < (int)bp.pools.size()) ? bp.pools[p.second].name : L"";
                                int plUnits = (p.second >= 0 && p.second < (int)bp.pools.size()) ? (int)bp.pools[p.second].units.size() : 0;
                                sourceStr = cfg.name + L" ➔ " + (plName.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : plName) + L" (" + std::to_wstring(plUnits) + L" units)";
                            }
                        }
                        else
                        {
                            int totalU = 0;
                            std::wstring poolNames = L"";
                            for (size_t k = 0; k < g_State.selectedBlueprintPools.size(); ++k)
                            {
                                auto p = g_State.selectedBlueprintPools[k];
                                if (p.first >= 0 && p.first < (int)totalBp)
                                {
                                    const auto& bp = bpPresets[p.first];
                                    if (p.second >= 0 && p.second < (int)bp.pools.size())
                                    {
                                        totalU += (int)bp.pools[p.second].units.size();
                                        if (k < 3)
                                        {
                                            if (k > 0) poolNames += L", ";
                                            poolNames += bp.pools[p.second].name.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : bp.pools[p.second].name;
                                        }
                                    }
                                }
                            }
                            if (g_State.selectedBlueprintPools.size() > 3) poolNames += L"...";
                            sourceStr = std::to_wstring(g_State.selectedBlueprintPools.size()) + L" of " + std::to_wstring(totalAllPools) + L" Pools: " + poolNames + L" (" + std::to_wstring(totalU) + L" units)";
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
                        size_t totalAllPools = 0;
                        int totalU = 0;
                        for (const auto& p : PoolManager::g_PoolPresetsCache)
                        {
                            totalAllPools += p.pools.size();
                            for (const auto& pl : p.pools) totalU += (int)pl.units.size();
                        }

                        bool isAll = (totalAllPools > 0 && (g_State.selectedPresetPools.empty() || g_State.selectedPresetPools.size() >= totalAllPools));
                        if (isAll)
                        {
                            sourceStr = L"★ Entire Preset Library (All " + std::to_wstring(totalPresets) + L" Presets • " + std::to_wstring(totalU) + L" units)";
                        }
                        else if (g_State.selectedPresetPools.size() == 1)
                        {
                            auto p = g_State.selectedPresetPools[0];
                            if (p.first >= 0 && p.first < (int)totalPresets)
                            {
                                const auto& pres = PoolManager::g_PoolPresetsCache[p.first];
                                std::wstring plName = (p.second >= 0 && p.second < (int)pres.pools.size()) ? pres.pools[p.second].name : L"";
                                int plUnits = (p.second >= 0 && p.second < (int)pres.pools.size()) ? (int)pres.pools[p.second].units.size() : 0;
                                sourceStr = (pres.presetName.empty() ? L"Preset " + std::to_wstring(p.first + 1) : pres.presetName) + L" ➔ " + (plName.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : plName) + L" (" + std::to_wstring(plUnits) + L" units)";
                            }
                        }
                        else
                        {
                            int selU = 0;
                            std::wstring poolNames = L"";
                            for (size_t k = 0; k < g_State.selectedPresetPools.size(); ++k)
                            {
                                auto p = g_State.selectedPresetPools[k];
                                if (p.first >= 0 && p.first < (int)totalPresets)
                                {
                                    const auto& pres = PoolManager::g_PoolPresetsCache[p.first];
                                    if (p.second >= 0 && p.second < (int)pres.pools.size())
                                    {
                                        selU += (int)pres.pools[p.second].units.size();
                                        if (k < 3)
                                        {
                                            if (k > 0) poolNames += L", ";
                                            poolNames += pres.pools[p.second].name.empty() ? (L"Pool #" + std::to_wstring(p.second + 1)) : pres.pools[p.second].name;
                                        }
                                    }
                                }
                            }
                            if (g_State.selectedPresetPools.size() > 3) poolNames += L"...";
                            sourceStr = std::to_wstring(g_State.selectedPresetPools.size()) + L" of " + std::to_wstring(totalAllPools) + L" Pools: " + poolNames + L" (" + std::to_wstring(selU) + L" units)";
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
                g_State.rcRadioPos0 = { 30, 290, colSplitX - 10, 318 };
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
                RECT rcPText0 = { 56, 290, colSplitX - 10, 318 };
                DrawTextW(hmemDC, L"Head (Front of Consist)", -1, &rcPText0, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 1: Behind Engines
                g_State.rcRadioPos1 = { colSplitX + 10, 290, rcClient.right - 30, 318 };
                bool isP1 = (g_State.posMode == PoolMutator::PositionMode::BehindEngines);
                RECT rcCPos1 = { colSplitX + 14, 296, colSplitX + 28, 310 };
                HBRUSH hbrP1 = CreateSolidBrush(cardBgCol);
                HPEN hpenP1 = CreatePen(PS_SOLID, 1, isP1 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP1);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP1);
                Ellipse(hmemDC, rcCPos1.left, rcCPos1.top, rcCPos1.right, rcCPos1.bottom);
                if (isP1) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos1.left + 3, rcCPos1.top + 3, rcCPos1.right - 3, rcCPos1.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP1); DeleteObject(hpenP1);
                RECT rcPText1 = { colSplitX + 36, 290, rcClient.right - 30, 318 };
                DrawTextW(hmemDC, L"Behind Lead Locomotives", -1, &rcPText1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 3: Tail
                g_State.rcRadioPos3 = { 30, 326, colSplitX - 10, 354 };
                bool isP3 = (g_State.posMode == PoolMutator::PositionMode::TailPosition);
                RECT rcCPos3 = { 34, 332, 48, 346 };
                HBRUSH hbrP3 = CreateSolidBrush(cardBgCol);
                HPEN hpenP3 = CreatePen(PS_SOLID, 1, isP3 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP3);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP3);
                Ellipse(hmemDC, rcCPos3.left, rcCPos3.top, rcCPos3.right, rcCPos3.bottom);
                if (isP3) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos3.left + 3, rcCPos3.top + 3, rcCPos3.right - 3, rcCPos3.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP3); DeleteObject(hpenP3);
                RECT rcPText3 = { 56, 326, colSplitX - 10, 354 };
                DrawTextW(hmemDC, L"Tail (End of Consist)", -1, &rcPText3, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Pos 2: Specific Index
                int posBoxX = (std::max)(colSplitX + 10 + 205, 472);
                g_State.rcRadioPos2 = { colSplitX + 10, 326, posBoxX + 115, 354 };
                bool isP2 = (g_State.posMode == PoolMutator::PositionMode::SpecificIndex);
                RECT rcCPos2 = { colSplitX + 14, 332, colSplitX + 28, 346 };
                HBRUSH hbrP2 = CreateSolidBrush(cardBgCol);
                HPEN hpenP2 = CreatePen(PS_SOLID, 1, isP2 ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrP2);
                hOldP = (HPEN)SelectObject(hmemDC, hpenP2);
                Ellipse(hmemDC, rcCPos2.left, rcCPos2.top, rcCPos2.right, rcCPos2.bottom);
                if (isP2) { HBRUSH hbrDot = CreateSolidBrush(accentCol); SelectObject(hmemDC, hbrDot); Ellipse(hmemDC, rcCPos2.left + 3, rcCPos2.top + 3, rcCPos2.right - 3, rcCPos2.bottom - 3); DeleteObject(hbrDot); }
                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP); DeleteObject(hbrP2); DeleteObject(hpenP2);
                RECT rcPText2 = { colSplitX + 36, 326, colSplitX + 210, 354 };
                DrawTextW(hmemDC, L"At Specific Index / Indices:", -1, &rcPText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (isP2)
                {
                    HBRUSH hPosBg = CreateSolidBrush(RGB(32, 32, 34));
                    HPEN hPosPen = CreatePen(PS_SOLID, 1, borderCol);
                    HBRUSH holdBPos = (HBRUSH)SelectObject(hmemDC, hPosBg);
                    HPEN holdPosP = (HPEN)SelectObject(hmemDC, hPosPen);
                    RoundRect(hmemDC, posBoxX - 3, 328, posBoxX + 115, 354, 4, 4);
                    SelectObject(hmemDC, holdBPos);
                    SelectObject(hmemDC, holdPosP);
                    DeleteObject(hPosBg);
                    DeleteObject(hPosPen);

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, textSecondary);
                    RECT rcHintIdx = { posBoxX + 122, 326, rcClient.right - 30, 354 };
                    DrawTextW(hmemDC, L"(e.g. 1; 5; 10)", -1, &rcHintIdx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            // =========================================================================
            // TAB 2: REPLACE / REPAIR BROKEN UNITS
            // =========================================================================
            else if (g_State.activeTab == 2)
            {
                g_State.rcPresetPicker = { 0 };
                g_State.rcPoolPicker = { 0 };
                g_State.rcRadioCount0 = { 0 };
                g_State.rcRadioCount1 = { 0 };
                g_State.rcRadioCount2 = { 0 };
                g_State.rcRadioInsertSrc0 = { 0 };
                g_State.rcRadioInsertSrc1 = { 0 };
                g_State.rcRadioPos0 = { 0 };
                g_State.rcRadioPos1 = { 0 };
                g_State.rcRadioPos2 = { 0 };
                g_State.rcRadioPos3 = { 0 };
                g_State.rcGroupPicker = { 0 };

                int cardBottom = rcClient.bottom - 68;
                int leftColW = (rcClient.right - 60) * 44 / 100;
                int wireChannelW = 54;
                int rightColX = 30 + leftColW + wireChannelW;
                int rightColW = rcClient.right - 30 - rightColX;

                struct WireAnchor
                {
                    POINT ptLeft;
                    int sourceId;
                };
                std::vector<WireAnchor> wireAnchors;

                // -----------------------------------------------------------------
                // LEFT COLUMN: Broken Consists & Units Tree Card
                // -----------------------------------------------------------------
                g_State.rcBrokenTreeCard = { 30, 126, 30 + leftColW, cardBottom };

                HBRUSH hbrTreeCard = CreateSolidBrush(cardBgCol);
                HPEN hpenTreeCard = CreatePen(PS_SOLID, 1, borderCol);
                HBRUSH hOldBTC = (HBRUSH)SelectObject(hmemDC, hbrTreeCard);
                HPEN hOldPTC = (HPEN)SelectObject(hmemDC, hpenTreeCard);
                RoundRect(hmemDC, g_State.rcBrokenTreeCard.left, g_State.rcBrokenTreeCard.top, g_State.rcBrokenTreeCard.right, g_State.rcBrokenTreeCard.bottom, 6, 6);
                SelectObject(hmemDC, hOldBTC);
                SelectObject(hmemDC, hOldPTC);
                DeleteObject(hbrTreeCard);
                DeleteObject(hpenTreeCard);

                // Tree Card Master Checkbox (Select All)
                g_State.rcMasterBrokenCheck = { g_State.rcBrokenTreeCard.right - 92, g_State.rcBrokenTreeCard.top + 6, g_State.rcBrokenTreeCard.right - 8, g_State.rcBrokenTreeCard.top + 30 };
                int totalUnits = 0;
                int selectedUnits = 0;
                int totalBrokenUnits = 0;
                int totalEmptyConsists = 0;
                for (const auto& bc : g_State.brokenConsists)
                {
                    if (bc.brokenUnits.empty())
                    {
                        totalEmptyConsists++;
                        totalUnits++;
                        if (bc.isSelected) selectedUnits++;
                    }
                    else
                    {
                        for (const auto& bu : bc.brokenUnits)
                        {
                            totalUnits++;
                            if (bu.isBroken) totalBrokenUnits++;
                            if (bc.isSelected && bu.isSelected) selectedUnits++;
                        }
                    }
                }
                bool isAllMaster = (totalUnits > 0 && selectedUnits == totalUnits);
                bool isIndeterminateMaster = (selectedUnits > 0 && !isAllMaster);

                // Quick Button: "Select Broken" (if broken units or empty consists exist)
                if (totalBrokenUnits > 0 || totalEmptyConsists > 0)
                {
                    g_State.rcTab2BtnSelectBroken = { g_State.rcMasterBrokenCheck.left - 105, g_State.rcBrokenTreeCard.top + 6, g_State.rcMasterBrokenCheck.left - 6, g_State.rcBrokenTreeCard.top + 30 };
                    COLORREF btnBg = g_State.isHoverTab2BtnSelectBroken ? RGB(55, 40, 42) : RGB(42, 30, 32);
                    COLORREF btnBorder = g_State.isHoverTab2BtnSelectBroken ? RGB(220, 80, 80) : RGB(140, 50, 50);
                    HBRUSH hbrSBBg = CreateSolidBrush(btnBg);
                    HPEN hpenSBBorder = CreatePen(PS_SOLID, 1, btnBorder);
                    SelectObject(hmemDC, hbrSBBg);
                    SelectObject(hmemDC, hpenSBBorder);
                    RoundRect(hmemDC, g_State.rcTab2BtnSelectBroken.left, g_State.rcTab2BtnSelectBroken.top, g_State.rcTab2BtnSelectBroken.right, g_State.rcTab2BtnSelectBroken.bottom, 4, 4);
                    DeleteObject(hbrSBBg);
                    DeleteObject(hpenSBBorder);

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, RGB(255, 120, 110));
                    DrawTextW(hmemDC, (totalEmptyConsists > 0 && totalBrokenUnits == 0) ? L"Select Empty" : L"Select Broken", -1, &g_State.rcTab2BtnSelectBroken, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else
                {
                    g_State.rcTab2BtnSelectBroken = { 0 };
                }

                // Tree Card Header Title
                int headerTitleRight = ((totalBrokenUnits > 0 || totalEmptyConsists > 0) ? (g_State.rcTab2BtnSelectBroken.left - 6) : (g_State.rcMasterBrokenCheck.left - 6));
                RECT rcTreeHeader = { g_State.rcBrokenTreeCard.left + 12, g_State.rcBrokenTreeCard.top + 6, headerTitleRight, g_State.rcBrokenTreeCard.top + 30 };
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                DrawTextW(hmemDC, L"Consist Units:", -1, &rcTreeHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                RECT rcMChkBox = { g_State.rcMasterBrokenCheck.left, g_State.rcMasterBrokenCheck.top + 4, g_State.rcMasterBrokenCheck.left + 16, g_State.rcMasterBrokenCheck.top + 20 };
                HBRUSH hbrMChk = CreateSolidBrush((isAllMaster || isIndeterminateMaster) ? accentCol : RGB(40, 40, 44));
                HPEN hpenMChk = CreatePen(PS_SOLID, 1, (isAllMaster || isIndeterminateMaster) ? accentCol : borderCol);
                hOldB = (HBRUSH)SelectObject(hmemDC, hbrMChk);
                hOldP = (HPEN)SelectObject(hmemDC, hpenMChk);
                RoundRect(hmemDC, rcMChkBox.left, rcMChkBox.top, rcMChkBox.right, rcMChkBox.bottom, 4, 4);
                if (isAllMaster)
                {
                    SelectObject(hmemDC, g_State.hFontIcon);
                    SetTextColor(hmemDC, RGB(255, 255, 255));
                    RECT rcGlyph = { rcMChkBox.left, rcMChkBox.top - 1, rcMChkBox.right, rcMChkBox.bottom };
                    DrawTextW(hmemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                else if (isIndeterminateMaster)
                {
                    HBRUSH hbrInd = CreateSolidBrush(RGB(255, 255, 255));
                    RECT rcInd = { rcMChkBox.left + 3, rcMChkBox.top + 7, rcMChkBox.right - 3, rcMChkBox.top + 9 };
                    FillRect(hmemDC, &rcInd, hbrInd);
                    DeleteObject(hbrInd);
                }
                SelectObject(hmemDC, hOldB);
                SelectObject(hmemDC, hOldP);
                DeleteObject(hbrMChk);
                DeleteObject(hpenMChk);

                SelectObject(hmemDC, g_State.hFontMain);
                SetTextColor(hmemDC, textPrimary);
                RECT rcMText = { g_State.rcMasterBrokenCheck.left + 20, g_State.rcMasterBrokenCheck.top, g_State.rcMasterBrokenCheck.right, g_State.rcMasterBrokenCheck.bottom };
                DrawTextW(hmemDC, L"Select All", -1, &rcMText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // Divider Line under Header
                HPEN hPenTDiv = CreatePen(PS_SOLID, 1, borderCol);
                SelectObject(hmemDC, hPenTDiv);
                MoveToEx(hmemDC, g_State.rcBrokenTreeCard.left + 1, g_State.rcBrokenTreeCard.top + 36, NULL);
                LineTo(hmemDC, g_State.rcBrokenTreeCard.right - 1, g_State.rcBrokenTreeCard.top + 36);
                DeleteObject(hPenTDiv);

                // Tree List Scroll View
                g_State.rcBrokenTreeList = { g_State.rcBrokenTreeCard.left + 4, g_State.rcBrokenTreeCard.top + 38, g_State.rcBrokenTreeCard.right - 4, g_State.rcBrokenTreeCard.bottom - 6 };

                HRGN hTreeClip = CreateRectRgn(g_State.rcBrokenTreeList.left, g_State.rcBrokenTreeList.top, g_State.rcBrokenTreeList.right, g_State.rcBrokenTreeList.bottom);
                SelectClipRgn(hmemDC, hTreeClip);

                if (g_State.brokenConsists.empty())
                {
                    // Empty state: prompt to select consists or scan library
                    SelectObject(hmemDC, g_State.hFontIconLg);
                    SetTextColor(hmemDC, textSecondary);
                    RECT rcEmIcon = { g_State.rcBrokenTreeList.left, g_State.rcBrokenTreeList.top + 45, g_State.rcBrokenTreeList.right, g_State.rcBrokenTreeList.top + 80 };
                    DrawTextW(hmemDC, L"\xE896", -1, &rcEmIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(hmemDC, g_State.hFontBold);
                    SetTextColor(hmemDC, textPrimary);
                    RECT rcEmTitle = { g_State.rcBrokenTreeList.left, g_State.rcBrokenTreeList.top + 88, g_State.rcBrokenTreeList.right, g_State.rcBrokenTreeList.top + 110 };
                    DrawTextW(hmemDC, L"No Consist(s) Selected", -1, &rcEmTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, textSecondary);
                    RECT rcEmSub = { g_State.rcBrokenTreeList.left + 24, g_State.rcBrokenTreeList.top + 116, g_State.rcBrokenTreeList.right - 24, g_State.rcBrokenTreeList.top + 160 };
                    DrawTextW(hmemDC, L"Select consist(s) in Consist Manager before opening, or click below to scan all consists across the library for broken units.", -1, &rcEmSub, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

                    // Scan Library Button
                    int btnW = 200;
                    int btnH = 32;
                    int btnX = (g_State.rcBrokenTreeList.left + g_State.rcBrokenTreeList.right - btnW) / 2;
                    int btnY = g_State.rcBrokenTreeList.top + 172;
                    g_State.rcTab2BtnScanLibrary = { btnX, btnY, btnX + btnW, btnY + btnH };

                    DrawModernButton(hmemDC, g_State.rcTab2BtnScanLibrary, L"Scan Entire Library", g_State.isHoverTab2BtnScanLibrary, false, false, g_State.hFontBold, g_State.hFontIcon, L"\xE721");
                }
                else
                {
                    g_State.rcTab2BtnScanLibrary = { 0 };
                    int curY = g_State.rcBrokenTreeList.top + 4 - g_State.brokenScrollY;
                    int totalContentH = 8;

                    for (size_t cIdx = 0; cIdx < g_State.brokenConsists.size(); ++cIdx)
                    {
                        auto& bcon = g_State.brokenConsists[cIdx];

                        int brokenCountInConsist = 0;
                        for (const auto& bu : bcon.brokenUnits)
                        {
                            if (bu.isBroken) brokenCountInConsist++;
                        }

                        int rowH = 26;
                        RECT rcConRow = { g_State.rcBrokenTreeList.left + 2, curY, g_State.rcBrokenTreeList.right - 2, curY + rowH };

                        if (rcConRow.bottom >= g_State.rcBrokenTreeList.top && rcConRow.top <= g_State.rcBrokenTreeList.bottom)
                        {
                            HBRUSH hbrRowBg = CreateSolidBrush(RGB(36, 36, 40));
                            HPEN hpenRow = CreatePen(PS_SOLID, 1, RGB(55, 55, 60));
                            HBRUSH holdR1 = (HBRUSH)SelectObject(hmemDC, hbrRowBg);
                            HPEN holdR2 = (HPEN)SelectObject(hmemDC, hpenRow);
                            RoundRect(hmemDC, rcConRow.left, rcConRow.top, rcConRow.right, rcConRow.bottom, 4, 4);
                            SelectObject(hmemDC, holdR1);
                            SelectObject(hmemDC, holdR2);
                            DeleteObject(hbrRowBg);
                            DeleteObject(hpenRow);

                            // Chevron
                            RECT rcChev = { rcConRow.left + 4, rcConRow.top, rcConRow.left + 20, rcConRow.bottom };
                            SelectObject(hmemDC, g_State.hFontIcon);
                            SetTextColor(hmemDC, textSecondary);
                            DrawTextW(hmemDC, bcon.isExpanded ? L"\xE70D" : L"\xE76C", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Checkbox
                            RECT rcConChk = { rcConRow.left + 24, rcConRow.top + 5, rcConRow.left + 40, rcConRow.top + 21 };
                            HBRUSH hbrCChk = CreateSolidBrush(bcon.isSelected ? accentCol : cardBgCol);
                            HPEN hpenCChk = CreatePen(PS_SOLID, 1, bcon.isSelected ? accentCol : borderCol);
                            SelectObject(hmemDC, hbrCChk);
                            SelectObject(hmemDC, hpenCChk);
                            RoundRect(hmemDC, rcConChk.left, rcConChk.top, rcConChk.right, rcConChk.bottom, 4, 4);
                            DeleteObject(hbrCChk);
                            DeleteObject(hpenCChk);

                            if (bcon.isSelected)
                            {
                                SelectObject(hmemDC, g_State.hFontIcon);
                                SetTextColor(hmemDC, RGB(255, 255, 255));
                                RECT rcGlyph = { rcConChk.left, rcConChk.top - 1, rcConChk.right, rcConChk.bottom };
                                DrawTextW(hmemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            }

                            // Consist Label
                            SelectObject(hmemDC, g_State.hFontBold);
                            SetTextColor(hmemDC, textPrimary);
                            bool isActivityConsist = (bcon.filePath.rfind(L"ACTIVITY:", 0) == 0);
                            std::wstring conLabel;
                            if (isActivityConsist)
                            {
                                conLabel = L"[Activity Consist] " + bcon.consistName;
                                if (!bcon.fileName.empty() && bcon.fileName != bcon.consistName)
                                {
                                    conLabel += L" (ID: " + bcon.fileName + L")";
                                }
                            }
                            else
                            {
                                conLabel = bcon.fileName;
                                if (conLabel.size() < 4 || _wcsicmp(conLabel.substr(conLabel.size() - 4).c_str(), L".con") != 0)
                                {
                                    conLabel += L".con";
                                }
                                if (!bcon.consistName.empty() && bcon.consistName != bcon.fileName)
                                {
                                    conLabel += L" (" + bcon.consistName + L")";
                                }
                            }
                            RECT rcConText = { rcConRow.left + 46, rcConRow.top, rcConRow.right - 250, rcConRow.bottom };
                            DrawTextW(hmemDC, conLabel.c_str(), -1, &rcConText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                            // Count & Status Badge
                            SelectObject(hmemDC, g_State.hFontSmall);
                            std::wstring badgStr;
                            if (bcon.brokenUnits.empty())
                            {
                                badgStr = L"0 units (Empty)";
                                SetTextColor(hmemDC, RGB(255, 170, 80));
                            }
                            else if (brokenCountInConsist > 0)
                            {
                                badgStr = std::to_wstring(brokenCountInConsist) + L" missing";
                                SetTextColor(hmemDC, RGB(255, 95, 80));
                            }
                            else
                            {
                                badgStr = L"Healthy";
                                SetTextColor(hmemDC, RGB(90, 190, 110));
                            }
                            RECT rcBadge = { rcConRow.right - 246, rcConRow.top, rcConRow.right - 170, rcConRow.bottom };
                            DrawTextW(hmemDC, badgStr.c_str(), -1, &rcBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Consist Action Mode Button [🔄 Rebuild] vs [🔧 Replace]
                            RECT rcConMode = { rcConRow.right - 164, rcConRow.top + 3, rcConRow.right - 84, rcConRow.top + 23 };
                            bool isRebuild = (bcon.actionExecutionMode == 1 || bcon.brokenUnits.empty());
                            COLORREF modeBg = isRebuild ? RGB(46, 26, 60) : RGB(24, 38, 52);
                            COLORREF modeBorder = isRebuild ? RGB(168, 85, 247) : RGB(56, 140, 220);
                            COLORREF modeTextCol = isRebuild ? RGB(226, 190, 255) : RGB(140, 205, 255);
                            const wchar_t* modeText = isRebuild ? L"🔄 Rebuild" : L"🔧 Replace";

                            HBRUSH hbrMode = CreateSolidBrush(modeBg);
                            HPEN hpenMode = CreatePen(PS_SOLID, 1, modeBorder);
                            SelectObject(hmemDC, hbrMode); SelectObject(hmemDC, hpenMode);
                            RoundRect(hmemDC, rcConMode.left, rcConMode.top, rcConMode.right, rcConMode.bottom, 4, 4);
                            DeleteObject(hbrMode); DeleteObject(hpenMode);

                            SelectObject(hmemDC, g_State.hFontSmall);
                            SetTextColor(hmemDC, modeTextCol);
                            DrawTextW(hmemDC, modeText, -1, &rcConMode, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            // Consist Source Pill [● Src A]
                            const auto* pConSrc = FindSourceNodeById(bcon.assignedSourceId);
                            COLORREF conSrcCol = pConSrc ? pConSrc->color : RGB(56, 189, 248);
                            std::wstring conSrcName = pConSrc ? pConSrc->name : L"Source A";

                            RECT rcConPill = { rcConRow.right - 80, rcConRow.top + 3, rcConRow.right - 4, rcConRow.top + 23 };
                            HBRUSH hbrPill = CreateSolidBrush(RGB(30, 30, 36));
                            HPEN hpenPill = CreatePen(PS_SOLID, 1, conSrcCol);
                            SelectObject(hmemDC, hbrPill); SelectObject(hmemDC, hpenPill);
                            RoundRect(hmemDC, rcConPill.left, rcConPill.top, rcConPill.right, rcConPill.bottom, 4, 4);
                            DeleteObject(hbrPill); DeleteObject(hpenPill);

                            HBRUSH hbrDot = CreateSolidBrush(conSrcCol);
                            SelectObject(hmemDC, hbrDot);
                            SelectObject(hmemDC, GetStockObject(NULL_PEN));
                            Ellipse(hmemDC, rcConPill.left + 5, rcConPill.top + 7, rcConPill.left + 11, rcConPill.top + 13);
                            DeleteObject(hbrDot);

                            SelectObject(hmemDC, g_State.hFontSmall);
                            SetTextColor(hmemDC, conSrcCol);
                            RECT rcPillTxt = { rcConPill.left + 14, rcConPill.top, rcConPill.right - 4, rcConPill.bottom };
                            DrawTextW(hmemDC, conSrcName.c_str(), -1, &rcPillTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            if (!bcon.isExpanded && bcon.isSelected)
                            {
                                wireAnchors.push_back({ { g_State.rcBrokenTreeCard.right, (rcConRow.top + rcConRow.bottom) / 2 }, bcon.assignedSourceId });
                            }
                        }

                        curY += rowH + 4;
                        totalContentH += rowH + 4;

                        if (bcon.isExpanded && (bcon.actionExecutionMode == 1 || bcon.brokenUnits.empty()))
                        {
                            // Render Rebuild Plan Banner
                            int planH = 48;
                            RECT rcPlan = { g_State.rcBrokenTreeList.left + 26, curY, g_State.rcBrokenTreeList.right - 6, curY + planH };
                            if (rcPlan.bottom >= g_State.rcBrokenTreeList.top && rcPlan.top <= g_State.rcBrokenTreeList.bottom)
                            {
                                HBRUSH hbrPlan = CreateSolidBrush(RGB(28, 22, 38));
                                HPEN hpenPlan = CreatePen(PS_SOLID, 1, RGB(130, 65, 175));
                                SelectObject(hmemDC, hbrPlan); SelectObject(hmemDC, hpenPlan);
                                RoundRect(hmemDC, rcPlan.left, rcPlan.top, rcPlan.right, rcPlan.bottom, 4, 4);
                                DeleteObject(hbrPlan); DeleteObject(hpenPlan);

                                SelectObject(hmemDC, g_State.hFontBold);
                                SetTextColor(hmemDC, RGB(226, 190, 255));
                                RECT rcPlanTitle = { rcPlan.left + 10, rcPlan.top + 4, rcPlan.right - 10, rcPlan.top + 22 };
                                std::wstring planTitle = bcon.brokenUnits.empty() ? L"⚡ Rebuild Consist: Generate Empty Formation" : L"⚡ Rebuild Consist: Full Formation Generation";
                                DrawTextW(hmemDC, planTitle.c_str(), -1, &rcPlanTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                SelectObject(hmemDC, g_State.hFontSmall);
                                SetTextColor(hmemDC, RGB(195, 185, 215));
                                RECT rcPlanSub = { rcPlan.left + 10, rcPlan.top + 24, rcPlan.right - 10, rcPlan.top + 42 };
                                const auto* pPlanSrc = FindSourceNodeById(bcon.assignedSourceId);
                                std::wstring srcNameStr = pPlanSrc ? pPlanSrc->name : L"Source";
                                std::wstring planDesc = bcon.brokenUnits.empty() ?
                                    (L"Generates complete consist from " + srcNameStr + L" sequence & pool rules.") :
                                    (L"Generates complete consist from " + srcNameStr + L" sequence & pool rules. (Original count ignored)");
                                DrawTextW(hmemDC, planDesc.c_str(), -1, &rcPlanSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                if (bcon.isSelected)
                                {
                                    wireAnchors.push_back({ { g_State.rcBrokenTreeCard.right, (rcPlan.top + rcPlan.bottom) / 2 }, bcon.assignedSourceId });
                                }
                            }
                            curY += planH + 4;
                            totalContentH += planH + 4;
                        }
                        else if (bcon.isExpanded && bcon.actionExecutionMode == 0)
                        {
                            for (size_t uIdx = 0; uIdx < bcon.brokenUnits.size(); ++uIdx)
                            {
                                const auto& bu = bcon.brokenUnits[uIdx];
                                int uH = 22;
                                RECT rcURow = { g_State.rcBrokenTreeList.left + 26, curY, g_State.rcBrokenTreeList.right - 6, curY + uH };

                                if (rcURow.bottom >= g_State.rcBrokenTreeList.top && rcURow.top <= g_State.rcBrokenTreeList.bottom)
                                {
                                    // Child Checkbox
                                    RECT rcUChk = { rcURow.left + 4, rcURow.top + 3, rcURow.left + 18, rcURow.top + 17 };
                                    HBRUSH hbrUChk = CreateSolidBrush(bu.isSelected ? accentCol : cardBgCol);
                                    HPEN hpenUChk = CreatePen(PS_SOLID, 1, bu.isSelected ? accentCol : borderCol);
                                    SelectObject(hmemDC, hbrUChk);
                                    SelectObject(hmemDC, hpenUChk);
                                    RoundRect(hmemDC, rcUChk.left, rcUChk.top, rcUChk.right, rcUChk.bottom, 3, 3);
                                    DeleteObject(hbrUChk);
                                    DeleteObject(hpenUChk);

                                    if (bu.isSelected)
                                    {
                                        SelectObject(hmemDC, g_State.hFontIcon);
                                        SetTextColor(hmemDC, RGB(255, 255, 255));
                                        RECT rcGlyph = { rcUChk.left, rcUChk.top - 1, rcUChk.right, rcUChk.bottom };
                                        DrawTextW(hmemDC, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    }

                                    // Type & Pos Badge
                                    SelectObject(hmemDC, g_State.hFontSmall);
                                    std::wstring typeStr = bu.isEngine ? L"[Engine #" + std::to_wstring(bu.unitIndex + 1) + L"]" : L"[Wagon #" + std::to_wstring(bu.unitIndex + 1) + L"]";
                                    SetTextColor(hmemDC, bu.isEngine ? RGB(100, 180, 255) : RGB(255, 170, 80));
                                    RECT rcUType = { rcURow.left + 24, rcURow.top, rcURow.left + 105, rcURow.bottom };
                                    DrawTextW(hmemDC, typeStr.c_str(), -1, &rcUType, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    // Unit Name & Folder
                                    SelectObject(hmemDC, g_State.hFontMain);
                                    SetTextColor(hmemDC, RGB(230, 230, 230));
                                    std::wstring uLabel = bu.uid + L" (" + bu.parentDir + L")";
                                    RECT rcUName = { rcURow.left + 108, rcURow.top, rcURow.right - 96, rcURow.bottom };
                                    DrawTextW(hmemDC, uLabel.c_str(), -1, &rcUName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                                    // Unit Source Pill [ A ]
                                    const auto* pUnitSrc = FindSourceNodeById(bu.assignedSourceId);
                                    COLORREF uSrcCol = pUnitSrc ? pUnitSrc->color : RGB(56, 189, 248);
                                    std::wstring uSrcLetter = pUnitSrc ? (pUnitSrc->name.size() >= 7 ? pUnitSrc->name.substr(7) : pUnitSrc->name) : L"A";

                                    RECT rcUPill = { rcURow.right - 92, rcURow.top + 2, rcURow.right - 58, rcURow.top + 20 };
                                    HBRUSH hbrUPill = CreateSolidBrush(RGB(30, 30, 36));
                                    HPEN hpenUPill = CreatePen(PS_SOLID, 1, uSrcCol);
                                    SelectObject(hmemDC, hbrUPill); SelectObject(hmemDC, hpenUPill);
                                    RoundRect(hmemDC, rcUPill.left, rcUPill.top, rcUPill.right, rcUPill.bottom, 3, 3);
                                    DeleteObject(hbrUPill); DeleteObject(hpenUPill);

                                    SelectObject(hmemDC, g_State.hFontSmall);
                                    SetTextColor(hmemDC, uSrcCol);
                                    DrawTextW(hmemDC, uSrcLetter.c_str(), -1, &rcUPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                                    if (bu.isSelected)
                                    {
                                        wireAnchors.push_back({ { g_State.rcBrokenTreeCard.right, (rcURow.top + rcURow.bottom) / 2 }, bu.assignedSourceId });
                                    }

                                    // Status Badge (MISSING or HEALTHY)
                                    SelectObject(hmemDC, g_State.hFontSmall);
                                    if (bu.isBroken)
                                    {
                                        SetTextColor(hmemDC, RGB(255, 80, 80));
                                        RECT rcUMiss = { rcURow.right - 54, rcURow.top, rcURow.right - 4, rcURow.bottom };
                                        DrawTextW(hmemDC, L"MISSING", -1, &rcUMiss, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    }
                                    else
                                    {
                                        SetTextColor(hmemDC, RGB(90, 190, 110));
                                        RECT rcUOk = { rcURow.right - 54, rcURow.top, rcURow.right - 4, rcURow.bottom };
                                        DrawTextW(hmemDC, L"HEALTHY", -1, &rcUOk, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                    }
                                }

                                curY += uH + 2;
                                totalContentH += uH + 2;
                            }
                        }
                    }
                    g_State.brokenTotalContentH = totalContentH;
                }

                SelectClipRgn(hmemDC, NULL);
                DeleteObject(hTreeClip);

                // -----------------------------------------------------------------
                // RIGHT COLUMN: Multi-Source Cards Stack & Options
                // -----------------------------------------------------------------
                g_State.sourceCardUIs.clear();

                // Top Header: Title & [+ Add Source] Button
                RECT rcSrcHeader = { rightColX, 126, rightColX + rightColW - 110, 150 };
                SelectObject(hmemDC, g_State.hFontBold);
                SetTextColor(hmemDC, textPrimary);
                DrawTextW(hmemDC, L"Replacement Sources", -1, &rcSrcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                g_State.rcAddSourceBtn = { rightColX + rightColW - 105, 122, rightColX + rightColW, 148 };
                DrawModernButton(hmemDC, g_State.rcAddSourceBtn, L"Add Source", g_State.isHoverAddSourceBtn, false, false, g_State.hFontSmall, g_State.hFontIcon, L"\xE710");

                int curCardY = 154;
                for (size_t k = 0; k < g_State.sourceNodes.size(); ++k)
                {
                    const auto& node = g_State.sourceNodes[k];
                    int cardH = 66;
                    RECT rcCard = { rightColX, curCardY, rightColX + rightColW, curCardY + cardH };

                    MutatorDlgState::SourceCardUI cardUI;
                    cardUI.sourceId = node.sourceId;
                    cardUI.rcCard = rcCard;

                    // Card Background
                    HBRUSH hbrCard = CreateSolidBrush(cardBgCol);
                    HPEN hpenCard = CreatePen(PS_SOLID, 1, borderCol);
                    SelectObject(hmemDC, hbrCard); SelectObject(hmemDC, hpenCard);
                    RoundRect(hmemDC, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 6, 6);
                    DeleteObject(hbrCard); DeleteObject(hpenCard);

                    // Left Accent Color Stripe
                    HBRUSH hbrStripe = CreateSolidBrush(node.color);
                    RECT rcStripe = { rcCard.left + 2, rcCard.top + 6, rcCard.left + 5, rcCard.bottom - 6 };
                    FillRect(hmemDC, &rcStripe, hbrStripe);
                    DeleteObject(hbrStripe);

                    // Color Badge [ A ]
                    RECT rcBadge = { rcCard.left + 12, rcCard.top + 7, rcCard.left + 30, rcCard.top + 23 };
                    HBRUSH hbrBadge = CreateSolidBrush(node.color);
                    SelectObject(hmemDC, hbrBadge);
                    SelectObject(hmemDC, GetStockObject(NULL_PEN));
                    RoundRect(hmemDC, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 3, 3);
                    DeleteObject(hbrBadge);

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, RGB(20, 20, 24));
                    std::wstring badgeLtr = node.name.size() >= 7 ? node.name.substr(7) : L"A";
                    DrawTextW(hmemDC, badgeLtr.c_str(), -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Source Name Title
                    SelectObject(hmemDC, g_State.hFontBold);
                    SetTextColor(hmemDC, textPrimary);
                    RECT rcNodeTitle = { rcCard.left + 36, rcCard.top + 5, rcCard.left + 105, rcCard.top + 25 };
                    DrawTextW(hmemDC, node.name.c_str(), -1, &rcNodeTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Segmented Type Toggle [ Fav Group | Pool Preset | Blueprint ]
                    cardUI.rcTypeToggle = { rcCard.left + 105, rcCard.top + 5, rcCard.right - (g_State.sourceNodes.size() > 1 ? 32 : 10), rcCard.top + 25 };
                    HBRUSH hbrTogBg = CreateSolidBrush(RGB(30, 30, 34));
                    HPEN hpenTogBg = CreatePen(PS_SOLID, 1, RGB(55, 55, 60));
                    SelectObject(hmemDC, hbrTogBg); SelectObject(hmemDC, hpenTogBg);
                    RoundRect(hmemDC, cardUI.rcTypeToggle.left, cardUI.rcTypeToggle.top, cardUI.rcTypeToggle.right, cardUI.rcTypeToggle.bottom, 4, 4);
                    DeleteObject(hbrTogBg); DeleteObject(hpenTogBg);

                    int totalW = cardUI.rcTypeToggle.right - cardUI.rcTypeToggle.left;
                    int segW = totalW / 3;
                    RECT rcTog0 = { cardUI.rcTypeToggle.left + 1, cardUI.rcTypeToggle.top + 1, cardUI.rcTypeToggle.left + segW, cardUI.rcTypeToggle.bottom - 1 };
                    RECT rcTog1 = { cardUI.rcTypeToggle.left + segW, cardUI.rcTypeToggle.top + 1, cardUI.rcTypeToggle.left + segW * 2, cardUI.rcTypeToggle.bottom - 1 };
                    RECT rcTog2 = { cardUI.rcTypeToggle.left + segW * 2, cardUI.rcTypeToggle.top + 1, cardUI.rcTypeToggle.right - 1, cardUI.rcTypeToggle.bottom - 1 };

                    if (node.sourceType == 0) // Fav Group Active
                    {
                        HBRUSH hbrAct = CreateSolidBrush(accentCol);
                        SelectObject(hmemDC, hbrAct);
                        SelectObject(hmemDC, GetStockObject(NULL_PEN));
                        RoundRect(hmemDC, rcTog0.left, rcTog0.top, rcTog0.right, rcTog0.bottom, 3, 3);
                        DeleteObject(hbrAct);
                    }
                    else if (node.sourceType == 1) // Pool Preset Active
                    {
                        HBRUSH hbrAct = CreateSolidBrush(accentCol);
                        SelectObject(hmemDC, hbrAct);
                        SelectObject(hmemDC, GetStockObject(NULL_PEN));
                        RoundRect(hmemDC, rcTog1.left, rcTog1.top, rcTog1.right, rcTog1.bottom, 3, 3);
                        DeleteObject(hbrAct);
                    }
                    else // Train Blueprint Active
                    {
                        HBRUSH hbrAct = CreateSolidBrush(accentCol);
                        SelectObject(hmemDC, hbrAct);
                        SelectObject(hmemDC, GetStockObject(NULL_PEN));
                        RoundRect(hmemDC, rcTog2.left, rcTog2.top, rcTog2.right, rcTog2.bottom, 3, 3);
                        DeleteObject(hbrAct);
                    }

                    SelectObject(hmemDC, g_State.hFontSmall);
                    SetTextColor(hmemDC, node.sourceType == 0 ? RGB(255, 255, 255) : textSecondary);
                    DrawTextW(hmemDC, L"Fav Group", -1, &rcTog0, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SetTextColor(hmemDC, node.sourceType == 1 ? RGB(255, 255, 255) : textSecondary);
                    DrawTextW(hmemDC, L"Pool Preset", -1, &rcTog1, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SetTextColor(hmemDC, node.sourceType == 2 ? RGB(255, 255, 255) : textSecondary);
                    DrawTextW(hmemDC, L"Train Configs", -1, &rcTog2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Delete Button (if > 1 source)
                    if (g_State.sourceNodes.size() > 1)
                    {
                        cardUI.rcDeleteBtn = { rcCard.right - 26, rcCard.top + 5, rcCard.right - 6, rcCard.top + 25 };
                        COLORREF delBg = cardUI.isHoverDelete ? RGB(65, 30, 35) : RGB(36, 36, 40);
                        COLORREF delBorder = cardUI.isHoverDelete ? RGB(200, 60, 60) : borderCol;
                        HBRUSH hbrDel = CreateSolidBrush(delBg);
                        HPEN hpenDel = CreatePen(PS_SOLID, 1, delBorder);
                        SelectObject(hmemDC, hbrDel); SelectObject(hmemDC, hpenDel);
                        RoundRect(hmemDC, cardUI.rcDeleteBtn.left, cardUI.rcDeleteBtn.top, cardUI.rcDeleteBtn.right, cardUI.rcDeleteBtn.bottom, 4, 4);
                        DeleteObject(hbrDel); DeleteObject(hpenDel);

                        SelectObject(hmemDC, g_State.hFontSmall);
                        SetTextColor(hmemDC, cardUI.isHoverDelete ? RGB(255, 100, 100) : textSecondary);
                        DrawTextW(hmemDC, L"✕", -1, &cardUI.rcDeleteBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                    else
                    {
                        cardUI.rcDeleteBtn = { 0 };
                    }

                    // Stock Palette Dropdown Picker
                    cardUI.rcPicker = { rcCard.left + 12, rcCard.top + 30, rcCard.right - 10, rcCard.top + 56 };
                    COLORREF pickBg = cardUI.isHoverPicker ? RGB(45, 45, 50) : RGB(32, 32, 36);
                    COLORREF pickBorder = cardUI.isHoverPicker ? RGB(90, 90, 95) : borderCol;
                    HBRUSH hbrPick = CreateSolidBrush(pickBg);
                    HPEN hpenPick = CreatePen(PS_SOLID, 1, pickBorder);
                    SelectObject(hmemDC, hbrPick); SelectObject(hmemDC, hpenPick);
                    RoundRect(hmemDC, cardUI.rcPicker.left, cardUI.rcPicker.top, cardUI.rcPicker.right, cardUI.rcPicker.bottom, 4, 4);
                    DeleteObject(hbrPick); DeleteObject(hpenPick);

                    std::wstring summaryStr = GetSourceNodeSummaryText(node);
                    SelectObject(hmemDC, g_State.hFontMain);
                    SetTextColor(hmemDC, textPrimary);
                    RECT rcSumm = { cardUI.rcPicker.left + 10, cardUI.rcPicker.top, cardUI.rcPicker.right - 24, cardUI.rcPicker.bottom };
                    DrawTextW(hmemDC, summaryStr.c_str(), -1, &rcSumm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    RECT rcChev = { cardUI.rcPicker.right - 22, cardUI.rcPicker.top, cardUI.rcPicker.right - 6, cardUI.rcPicker.bottom };
                    SelectObject(hmemDC, g_State.hFontIcon);
                    SetTextColor(hmemDC, textSecondary);
                    DrawTextW(hmemDC, L"\xE70D", -1, &rcChev, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    g_State.sourceCardUIs.push_back(cardUI);
                    curCardY += cardH + 8;
                }

                // -----------------------------------------------------------------
                // CIRCUIT WIRING LINES & ARROWHEADS
                // -----------------------------------------------------------------
                for (const auto& anchor : wireAnchors)
                {
                    const MutatorDlgState::SourceCardUI* pTargetCard = nullptr;
                    for (const auto& c : g_State.sourceCardUIs)
                    {
                        if (c.sourceId == anchor.sourceId) { pTargetCard = &c; break; }
                    }
                    if (!pTargetCard && !g_State.sourceCardUIs.empty())
                    {
                        pTargetCard = &g_State.sourceCardUIs[0];
                    }
                    if (!pTargetCard) continue;

                    int x1 = anchor.ptLeft.x + 1;
                    int y1 = anchor.ptLeft.y;
                    int x2 = pTargetCard->rcCard.left - 2;
                    int y2 = pTargetCard->rcCard.top + 16;
                    int midX = (x1 + x2) / 2;

                    const auto* pNode = FindSourceNodeById(anchor.sourceId);
                    COLORREF wireCol = pNode ? pNode->color : RGB(56, 189, 248);

                    HPEN hpenWire = CreatePen(PS_SOLID, 2, wireCol);
                    HPEN holdPen = (HPEN)SelectObject(hmemDC, hpenWire);

                    MoveToEx(hmemDC, x1, y1, NULL);
                    LineTo(hmemDC, midX, y1);
                    LineTo(hmemDC, midX, y2);
                    LineTo(hmemDC, x2, y2);

                    SelectObject(hmemDC, holdPen);
                    DeleteObject(hpenWire);

                    // Solid Arrowhead (►)
                    POINT tri[3] = {
                        { x2, y2 },
                        { x2 - 6, y2 - 4 },
                        { x2 - 6, y2 + 4 }
                    };
                    HBRUSH hbrArrow = CreateSolidBrush(wireCol);
                    SelectObject(hmemDC, hbrArrow);
                    SelectObject(hmemDC, GetStockObject(NULL_PEN));
                    Polygon(hmemDC, tri, 3);
                    DeleteObject(hbrArrow);
                }
            }

            // Bottom Configuration Section (Only for Tab 0 and Tab 1)
            if (g_State.activeTab != 2)
            {
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
            }

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

            // Tab 2: Informational Note in Bottom Footer Bar
            if (g_State.activeTab == 2)
            {
                int optY = rcClient.bottom - 42;
                int optH = 30;

                SelectObject(hmemDC, g_State.hFontIcon);
                SetTextColor(hmemDC, accentCol);
                RECT rcInfoIcon = { 24, optY, 44, optY + optH };
                DrawTextW(hmemDC, L"\xE946", -1, &rcInfoIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hmemDC, g_State.hFontMain);
                SetTextColor(hmemDC, textSecondary);
                RECT rcInfoText = { 48, optY, rcClient.right - 360, optY + optH };
                DrawTextW(hmemDC, L"Picking mode and flip orientation are governed by source Preset, Config, or Group rules.", -1, &rcInfoText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }

            // Action Buttons: [Apply / Insert / Replace] & [Cancel]
            const wchar_t* applyText = (g_State.activeTab == 0) ? L"Apply Mutation" : ((g_State.activeTab == 1) ? L"Insert Units" : L"Execute Repair / Rebuild");
            const wchar_t* applyIcon = (g_State.activeTab == 2) ? L"\xE896" : L"\xE73E";

            SelectObject(hmemDC, g_State.hFontBold);
            SIZE applySz = { 0 };
            GetTextExtentPoint32W(hmemDC, applyText, (int)wcslen(applyText), &applySz);
            int applyBtnW = (std::max)(160, (int)applySz.cx + 56);
            int cancelBtnW = 90;
            int btnGap = 10;
            int btnMarginRight = 20;

            g_State.rcApplyBtn = { rcClient.right - btnMarginRight - applyBtnW, rcClient.bottom - 42, rcClient.right - btnMarginRight, rcClient.bottom - 12 };
            g_State.rcCancelBtn = { g_State.rcApplyBtn.left - btnGap - cancelBtnW, rcClient.bottom - 42, g_State.rcApplyBtn.left - btnGap, rcClient.bottom - 12 };

            DrawModernButton(hmemDC, g_State.rcApplyBtn, applyText, g_State.isHoverApply, false, true, g_State.hFontBold, g_State.hFontIcon, applyIcon);
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
            if (g_State.hFontIconMed) DeleteObject(g_State.hFontIconMed);
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
        if (g_State.activeTab == 2)
        {
            RefreshBrokenConsists();
        }
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
        if (initialMode == PoolMutator::MutatorMode::InsertUnits) g_State.activeTab = 1;
        else if (initialMode == PoolMutator::MutatorMode::ReplaceBroken) g_State.activeTab = 2;
        else g_State.activeTab = 0;
        g_State.targetConsistPaths = targetConsistFilePaths;
        g_State.targetUnitIndices = targetSelectedUnitIndices;
        if (g_State.activeTab == 2)
        {
            RefreshBrokenConsists();
        }
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
    if (initialMode == PoolMutator::MutatorMode::InsertUnits) g_State.activeTab = 1;
    else if (initialMode == PoolMutator::MutatorMode::ReplaceBroken) g_State.activeTab = 2;
    else g_State.activeTab = 0;
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
        else if (initialMode == PoolMutator::MutatorMode::ReplaceBroken)
        {
            g_State.activeTab = 2;
            g_State.mode = PoolMutator::MutatorMode::ReplaceBroken;
        }
    }

    if (g_State.activeTab == 2)
    {
        RefreshBrokenConsists();
    }

    const wchar_t* szClassName = L"PoolMutatorDlgClass";
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = PoolMutatorDlgProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = NULL;
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
        RedrawWindow(g_State.hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    UpdateWindow(hDlg);
}
