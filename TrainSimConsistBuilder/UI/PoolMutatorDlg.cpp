#include <set>
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

#include "CustomScrollBar.h"

#include "CustomListControl.h"

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



        // Tab 2 Filter (Defaults to All Consists as requested)

        enum class Tab2FilterMode { BrokenOnly, EmptyOnly, BrokenAndEmpty, All };

        Tab2FilterMode tab2Filter = Tab2FilterMode::All;



        // Tab 2 Action

        enum class Tab2ActionMode { ReplaceUnit, RebuildConsist, PurgeUnit };

        Tab2ActionMode tab2Action = Tab2ActionMode::ReplaceUnit;



        // Tab 2 selected consist index (for routing/preview)

        int tab2SelectedConsistIdx = -1;



        // Tab 2 Multi-Consist & Sub-Group Collapsible States

        struct ConsistSectionState

        {

            bool isExpanded = true;

            bool enginesExpanded = true;

            bool wagonsExpanded = true;

        };

        std::map<size_t, ConsistSectionState> tab2ConsistStates;



        // Hierarchical Collapsible Matrix for Tab 2 (identical to Sound Studio)

        enum MutatorDisplayRowType

        {

            MUTATOR_ROW_CONSIST_HEADER,

            MUTATOR_ROW_UNIT_ENTRY,

            MUTATOR_ROW_EMPTY_NOTICE

        };



        struct MutatorDisplayRow

        {

            MutatorDisplayRowType rowType = MUTATOR_ROW_CONSIST_HEADER;

            int consistIndex = 0;

            int unitIndex = -1;

        };



        CustomListControl batchConsistList;

        HWND hBatchConsistListWnd = NULL;

        std::vector<MutatorDisplayRow> displayRows;



        // Tab 2 Sub-Toolbar & Controls

        RECT rcBtnExpandAll = { 0 };

        RECT rcBtnCollapseAll = { 0 };

        RECT rcTab2FilterBtn = { 0 };

        RECT rcTab2ActionReplace = { 0 };

        RECT rcTab2ActionRebuild = { 0 };

        RECT rcTab2ActionPurge = { 0 };



        // Hover states for Tab 2 elements

        bool isHoverBtnExpandAll = false;

        bool isHoverBtnCollapseAll = false;

        bool isHoverTab2FilterBtn = false;

        bool isHoverTab2ActionReplace = false;

        bool isHoverTab2ActionRebuild = false;

        bool isHoverTab2ActionPurge = false;

    };



    static MutatorDlgState g_State;



    static MutatorDlgState::ConsistSectionState& GetConsistSectionState(size_t cIdx)

    {

        auto it = g_State.tab2ConsistStates.find(cIdx);

        if (it == g_State.tab2ConsistStates.end())

        {

            MutatorDlgState::ConsistSectionState newState;

            newState.isExpanded = true;

            newState.enginesExpanded = true;

            newState.wagonsExpanded = true;

            g_State.tab2ConsistStates[cIdx] = newState;

            return g_State.tab2ConsistStates[cIdx];

        }

        return it->second;

    }



    static std::vector<size_t> GetActiveConsistIndices()

    {

        std::vector<size_t> active;

        for (size_t i = 0; i < g_State.brokenConsists.size(); ++i)

        {

            if (g_State.brokenConsists[i].isSelected)

            {

                active.push_back(i);

            }

        }

        if (active.empty() && g_State.tab2SelectedConsistIdx >= 0 && g_State.tab2SelectedConsistIdx < (int)g_State.brokenConsists.size())

        {

            active.push_back((size_t)g_State.tab2SelectedConsistIdx);

        }

        return active;

    }



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



    static void PopulateBatchConsistMatrix();

    static void ResizeBatchConsistColumns(int listW);



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

        PopulateBatchConsistMatrix();

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



        // Batch Consist Hierarchical Matrix (Tab 2)

        if (g_State.hBatchConsistListWnd)

        {

            if (isTab2)

            {

                int leftX = 16;

                int rightMargin = 16;

                int gap = 8;

                int totalW = w - (leftX + rightMargin);

                int rightColW = (totalW - gap) * 34 / 100;

                if (rightColW < 320) rightColW = 320;

                int listW = totalW - gap - rightColW;

                int topY = 118;

                int cardBottom = h - 60;

                int listH = cardBottom - topY;

                if (listH < 100) listH = 100;




                if (g_State.brokenConsists.empty())

                {

                    ShowWindow(g_State.hBatchConsistListWnd, SW_HIDE);

                }

                else

                {

                    ShowWindow(g_State.hBatchConsistListWnd, SW_SHOW);

                    SetWindowPos(g_State.hBatchConsistListWnd, NULL, leftX, topY, listW, listH, SWP_NOZORDER);

                    ResizeBatchConsistColumns(listW);

                }

            }

            else

            {

                ShowWindow(g_State.hBatchConsistListWnd, SW_HIDE);

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



    // -------------------------------------------------------------------------

    // TAB 2 NEW HELPERS

    // -------------------------------------------------------------------------



    static void DrawSolidTriangleDir(HDC hdc, int cx, int cy, COLORREF col, int dir /* 0=down, 1=up, 2=right, 3=left */, int size = 4)

    {

        HBRUSH hbr = CreateSolidBrush(col);

        HPEN hpen = CreatePen(PS_SOLID, 1, col);

        HBRUSH holdBr = (HBRUSH)SelectObject(hdc, hbr);

        HPEN holdPen = (HPEN)SelectObject(hdc, hpen);

        POINT pts[3];

        if (dir == 0) // Down

        {

            pts[0] = { cx - size, cy - (size / 2) };

            pts[1] = { cx + size, cy - (size / 2) };

            pts[2] = { cx,        cy + (size / 2) + 1 };

        }

        else if (dir == 1) // Up

        {

            pts[0] = { cx,        cy - (size / 2) - 1 };

            pts[1] = { cx - size, cy + (size / 2) };

            pts[2] = { cx + size, cy + (size / 2) };

        }

        else if (dir == 2) // Right

        {

            pts[0] = { cx - (size / 2), cy - size };

            pts[1] = { cx - (size / 2), cy + size };

            pts[2] = { cx + (size / 2) + 1, cy };

        }

        else // Left

        {

            pts[0] = { cx + (size / 2), cy - size };

            pts[1] = { cx + (size / 2), cy + size };

            pts[2] = { cx - (size / 2) - 1, cy };

        }

        Polygon(hdc, pts, 3);

        SelectObject(hdc, holdBr);

        SelectObject(hdc, holdPen);

        DeleteObject(hbr);

        DeleteObject(hpen);

    }



    static void DrawSolidTriangle(HDC hdc, int cx, int cy, COLORREF col, bool isDown, int size = 4)

    {

        DrawSolidTriangleDir(hdc, cx, cy, col, isDown ? 0 : 1, size);

    }



    static void ResizeBatchConsistColumns(int listW)

    {

        if (!g_State.hBatchConsistListWnd) return;

        RECT rcInner = { 0 };

        if (GetClientRect(g_State.hBatchConsistListWnd, &rcInner) && rcInner.right > 50)

        {

            listW = rcInner.right;

        }

        if (listW <= 100) return;



        int availW = listW - 14;

        int fixedW = 64 + 85 + 115 + 170; // Col 0 (64px), 2, 4, 5 (170px for full 'Target Mutation / Source')

        int remW = availW - fixedW;

        if (remW < 180) remW = 180;



        int col1 = remW * 55 / 100;

        int col3 = remW - col1;



        g_State.batchConsistList.SetColumnWidth(0, 64);

        g_State.batchConsistList.SetColumnWidth(1, col1);

        g_State.batchConsistList.SetColumnWidth(2, 85);

        g_State.batchConsistList.SetColumnWidth(3, col3);

        g_State.batchConsistList.SetColumnWidth(4, 115);

        g_State.batchConsistList.SetColumnWidth(5, 170);


        g_State.batchConsistList.Invalidate();

    }



    static void PopulateBatchConsistMatrix()

    {

        if (!g_State.hBatchConsistListWnd) return;



        g_State.displayRows.clear();

        g_State.batchConsistList.Clear();



        std::vector<size_t> filteredIndices;

        for (size_t i = 0; i < g_State.brokenConsists.size(); ++i)

        {

            auto& bc = g_State.brokenConsists[i];

            bool hasBroken = false;

            for (const auto& bu : bc.brokenUnits) { if (bu.isBroken) { hasBroken = true; break; } }

            bool isEmpty = bc.brokenUnits.empty();



            bool include = false;

            switch (g_State.tab2Filter)

            {

            case MutatorDlgState::Tab2FilterMode::BrokenOnly:     include = hasBroken; break;

            case MutatorDlgState::Tab2FilterMode::EmptyOnly:      include = isEmpty; break;

            case MutatorDlgState::Tab2FilterMode::BrokenAndEmpty: include = hasBroken || isEmpty; break;

            case MutatorDlgState::Tab2FilterMode::All:            include = true; break;

            }

            if (include) filteredIndices.push_back(i);

        }



        for (size_t fIdx = 0; fIdx < filteredIndices.size(); ++fIdx)

        {

            size_t cIdx = filteredIndices[fIdx];

            auto& bc = g_State.brokenConsists[cIdx];

            auto& cState = GetConsistSectionState(cIdx);



            // 1. Parent Consist Row

            MutatorDlgState::MutatorDisplayRow parentRow;

            parentRow.rowType = MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER;

            parentRow.consistIndex = (int)cIdx;

            parentRow.unitIndex = -1;

            g_State.displayRows.push_back(parentRow);



            int brokenCount = 0;

            for (const auto& bu : bc.brokenUnits) { if (bu.isBroken) brokenCount++; }



            bool isActivity = (bc.filePath.rfind(L"ACTIVITY:", 0) == 0);

            std::wstring conLabel;

            if (isActivity)

            {

                conLabel = L"[Act] " + bc.consistName;

            }

            else

            {

                conLabel = bc.fileName;

                if (conLabel.size() < 4 || _wcsicmp(conLabel.substr(conLabel.size() - 4).c_str(), L".con") != 0)

                {

                    conLabel += L".con";

                }

            }



            std::wstring expandChevron = bc.brokenUnits.empty() ? L"  " : (cState.isExpanded ? L"▾ " : L"▸ ");

            std::wstring col0 = expandChevron + std::to_wstring(fIdx + 1);



            std::wstring typeStr = std::to_wstring(bc.brokenUnits.size()) + L" Units";

            std::wstring pathStr = bc.filePath;



            std::wstring statusStr;

            if (bc.brokenUnits.empty()) statusStr = L"○ 0 Units";

            else if (brokenCount > 0) statusStr = L"⚠ " + std::to_wstring(brokenCount) + L" Missing";

            else statusStr = L"✓ Healthy";



            std::wstring targetStr;

            if (bc.actionExecutionMode == 2)

            {

                targetStr = L"🗑 Purge Marked";

            }

            else if (bc.actionExecutionMode == 1)

            {

                const auto* pSrc = FindSourceNodeById(bc.assignedSourceId);

                targetStr = L"🔄 Rebuild ➔ " + (pSrc ? pSrc->name : L"Source A");

            }

            else

            {

                const auto* pSrc = FindSourceNodeById(bc.assignedSourceId);

                targetStr = L"🔧 " + (pSrc ? pSrc->name : L"Source A");

            }



            g_State.batchConsistList.AddItem({

                col0,

                conLabel,

                typeStr,

                pathStr,

                statusStr,

                targetStr

            });



            // 2. Child Rows if Expanded

            if (cState.isExpanded)

            {

                if (bc.brokenUnits.empty())

                {

                    MutatorDlgState::MutatorDisplayRow noticeRow;

                    noticeRow.rowType = MutatorDlgState::MUTATOR_ROW_EMPTY_NOTICE;

                    noticeRow.consistIndex = (int)cIdx;

                    noticeRow.unitIndex = -1;

                    g_State.displayRows.push_back(noticeRow);



                    g_State.batchConsistList.AddItem({

                        L"   ↳",

                        L"(Empty Formation - 0 units)",

                        L"—",

                        L"—",

                        L"○ 0 Units",

                        L"—"

                    });

                }

                else

                {

                    for (size_t uIdx = 0; uIdx < bc.brokenUnits.size(); ++uIdx)

                    {

                        const auto& bu = bc.brokenUnits[uIdx];

                        MutatorDlgState::MutatorDisplayRow unitRow;

                        unitRow.rowType = MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY;

                        unitRow.consistIndex = (int)cIdx;

                        unitRow.unitIndex = (int)uIdx;

                        g_State.displayRows.push_back(unitRow);



                        std::wstring uSlot = bu.uid;

                        std::wstring uType = bu.isEngine ? L"Engine" : L"Wagon";

                        std::wstring uFolder = bu.parentDir;

                        std::wstring uStatus = bu.isBroken ? L"MISSING" : L"READY";



                        std::wstring uTarget;

                        if (bc.actionExecutionMode == 2)

                        {

                            uTarget = bu.isSelected ? L"➔ Purge Unit" : L"Keep Unit";

                        }

                        else if (bc.actionExecutionMode == 1)

                        {

                            uTarget = L"(Rebuild Entire)";

                        }

                        else

                        {

                            const auto* pSrc = FindSourceNodeById(bu.assignedSourceId);

                            uTarget = L"➔ " + (pSrc ? pSrc->name : L"Source A");

                        }



                        g_State.batchConsistList.AddItem({

                            L"   ↳",

                            uSlot,

                            uType,

                            uFolder,

                            uStatus,

                            uTarget

                        });

                    }

                }

            }

        }



        ResizeBatchConsistColumns(0);

    }



    static bool Mutator_RowBackgroundCallback(HDC hdc, int itemIndex, const RECT& rcRow, bool isSelected, bool isHovered, void* pParam)

    {

        if (itemIndex < 0 || itemIndex >= (int)g_State.displayRows.size()) return false;

        const auto& dRow = g_State.displayRows[itemIndex];

        if (dRow.consistIndex < 0 || dRow.consistIndex >= (int)g_State.brokenConsists.size()) return false;

        const auto& bc = g_State.brokenConsists[dRow.consistIndex];



        int brokenCount = 0;

        for (const auto& bu : bc.brokenUnits) { if (bu.isBroken) brokenCount++; }



        COLORREF accentColor;

        if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

        {

            accentColor = (brokenCount > 0) ? RGB(255, 95, 80) : RGB(0, 145, 255);

        }

        else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

        {

            const auto& bu = bc.brokenUnits[dRow.unitIndex];

            if (bu.isBroken) accentColor = RGB(255, 80, 80);

            else if (bu.isEngine) accentColor = RGB(0, 145, 255);

            else accentColor = RGB(245, 158, 11);

        }

        else

        {

            accentColor = RGB(120, 120, 130);

        }



        // Continuous edge-to-edge row geometry (no rounded cards or pill gaps)

        RECT rc = rcRow;

        rc.left = 0;

        rc.top = rcRow.top - 1;

        rc.bottom = rcRow.bottom + 1;

        rc.right = (std::max)((int)rcRow.right + 10, 2000);



        COLORREF bgCol;

        if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

        {

            // Distinct Consist Section Banner

            if (isSelected)

                bgCol = isHovered ? RGB(44, 78, 122) : RGB(36, 68, 110);

            else if (isHovered)

                bgCol = (brokenCount > 0) ? RGB(44, 34, 46) : RGB(34, 44, 62);

            else

                bgCol = (brokenCount > 0) ? RGB(34, 28, 36) : RGB(26, 34, 48);

        }

        else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY)

        {

            // Alternating zebra striping for child units

            if (isSelected)

                bgCol = isHovered ? RGB(34, 62, 100) : RGB(28, 52, 85);

            else if (isHovered)

                bgCol = RGB(30, 38, 50);

            else

                bgCol = (dRow.unitIndex % 2 == 0) ? RGB(20, 24, 30) : RGB(24, 28, 36);

        }

        else // MUTATOR_ROW_EMPTY_NOTICE

        {

            if (isSelected)

                bgCol = RGB(28, 52, 85);

            else if (isHovered)

                bgCol = RGB(26, 32, 44);

            else

                bgCol = RGB(20, 24, 30);

        }



        // Fill row background

        HBRUSH hbrBg = CreateSolidBrush(bgCol);

        FillRect(hdc, &rc, hbrBg);

        DeleteObject(hbrBg);



        // Horizontal Grid Dividers

        if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

        {

            // Subtle top & bottom framing border for section header

            HPEN hPenHdrTop = CreatePen(PS_SOLID, 1, RGB(52, 64, 82));

            HGDIOBJ oldP = SelectObject(hdc, hPenHdrTop);

            MoveToEx(hdc, rc.left, rc.top, NULL);

            LineTo(hdc, rc.right, rc.top);



            HPEN hPenHdrBtm = CreatePen(PS_SOLID, 1, RGB(40, 50, 66));

            SelectObject(hdc, hPenHdrBtm);

            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);

            LineTo(hdc, rc.right, rc.bottom - 1);



            SelectObject(hdc, oldP);

            DeleteObject(hPenHdrTop);

            DeleteObject(hPenHdrBtm);

        }

        else

        {

            // Subtle 1px horizontal bottom grid line between unit rows

            HPEN hPenGrid = CreatePen(PS_SOLID, 1, RGB(32, 38, 48));

            HGDIOBJ oldP = SelectObject(hdc, hPenGrid);

            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);

            LineTo(hdc, rc.right, rc.bottom - 1);

            SelectObject(hdc, oldP);

            DeleteObject(hPenGrid);

        }



        // Left Accent Color Stripe (clean 3px bar at column 0 left edge)

        RECT rcStripe = { rc.left, rc.top, rc.left + 3, rc.bottom };

        HBRUSH hbrStripe = CreateSolidBrush(accentColor);

        FillRect(hdc, &rcStripe, hbrStripe);

        DeleteObject(hbrStripe);



        return true;

    }

    static bool Mutator_DrawCellCallback(HDC hdc, int itemIndex, int subItemIndex, const RECT& rcCell, bool isSelected, bool isHovered, void* pParam)

    {

        if (itemIndex < 0 || itemIndex >= (int)g_State.displayRows.size()) return false;

        const auto& dRow = g_State.displayRows[itemIndex];

        if (dRow.consistIndex < 0 || dRow.consistIndex >= (int)g_State.brokenConsists.size()) return false;

        const auto& bc = g_State.brokenConsists[dRow.consistIndex];

        auto& cState = GetConsistSectionState(dRow.consistIndex);



        int oldBkMode = SetBkMode(hdc, TRANSPARENT);

        HFONT hOldFont = (HFONT)SelectObject(hdc, g_State.hFontMain ? g_State.hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

        COLORREF accentCol = RGB(0, 120, 215);



        // Column 0: Chevron / Checkbox / Number

        if (subItemIndex == 0)

        {

            RECT rc = rcCell;

            if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

            {

                // Chevron ▾ / ▸

                if (!bc.brokenUnits.empty())

                {

                    SelectObject(hdc, g_State.hFontMain);

                    SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(140, 185, 240));

                    RECT rcChevron = { rc.left + 6, rc.top, rc.left + 18, rc.bottom };

                    DrawTextW(hdc, cState.isExpanded ? L"▾" : L"▸", -1, &rcChevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }



                // Checkbox

                int chkSz = 14;

                int chkY = rc.top + (rc.bottom - rc.top - chkSz) / 2;

                RECT rcChk = { rc.left + 20, chkY, rc.left + 20 + chkSz, chkY + chkSz };



                int selUnits = 0;

                for (const auto& u : bc.brokenUnits) { if (u.isSelected) selUnits++; }

                bool isAllChecked = bc.isSelected && (!bc.brokenUnits.empty() ? (selUnits == (int)bc.brokenUnits.size()) : true);

                bool isIndeterminate = !isAllChecked && (selUnits > 0 || bc.isSelected);



                COLORREF chkBg = (isAllChecked || isIndeterminate) ? accentCol : RGB(36, 40, 48);

                COLORREF chkBdr = (isAllChecked || isIndeterminate) ? accentCol : RGB(70, 78, 92);

                HBRUSH hbrChk = CreateSolidBrush(chkBg);

                HPEN hpChk = CreatePen(PS_SOLID, 1, chkBdr);

                HGDIOBJ oB = SelectObject(hdc, hbrChk);

                HGDIOBJ oP = SelectObject(hdc, hpChk);

                RoundRect(hdc, rcChk.left, rcChk.top, rcChk.right, rcChk.bottom, 3, 3);

                SelectObject(hdc, oB); SelectObject(hdc, oP);

                DeleteObject(hbrChk); DeleteObject(hpChk);



                if (isAllChecked)

                {

                    SelectObject(hdc, g_State.hFontIcon ? g_State.hFontIcon : g_State.hFontMain);

                    SetTextColor(hdc, RGB(255, 255, 255));

                    RECT rcGlyph = { rcChk.left, rcChk.top - 1, rcChk.right, rcChk.bottom };

                    DrawTextW(hdc, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

                else if (isIndeterminate)

                {

                    HBRUSH hbrInd = CreateSolidBrush(RGB(255, 255, 255));

                    RECT rcInd = { rcChk.left + 3, rcChk.top + 6, rcChk.right - 3, rcChk.top + 8 };

                    FillRect(hdc, &rcInd, hbrInd);

                    DeleteObject(hbrInd);

                }



                // Consist Number #

                SelectObject(hdc, g_State.hFontBold);

                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(220, 225, 235));

                RECT rcNum = { rc.left + 38, rc.top, rc.right - 2, rc.bottom };

                DrawTextW(hdc, std::to_wstring(dRow.consistIndex + 1).c_str(), -1, &rcNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

            {

                const auto& bu = bc.brokenUnits[dRow.unitIndex];



                // Branch ↳

                SelectObject(hdc, g_State.hFontMain);

                SetTextColor(hdc, RGB(110, 130, 160));

                RECT rcBranch = { rc.left + 6, rc.top, rc.left + 20, rc.bottom };

                DrawTextW(hdc, L"↳", -1, &rcBranch, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Unit Checkbox

                int chkSz = 14;

                int chkY = rc.top + (rc.bottom - rc.top - chkSz) / 2;

                RECT rcChk = { rc.left + 22, chkY, rc.left + 22 + chkSz, chkY + chkSz };



                HBRUSH hbrChk = CreateSolidBrush(bu.isSelected ? accentCol : RGB(32, 36, 44));

                HPEN hpChk = CreatePen(PS_SOLID, 1, bu.isSelected ? accentCol : RGB(65, 72, 85));

                HGDIOBJ oB = SelectObject(hdc, hbrChk);

                HGDIOBJ oP = SelectObject(hdc, hpChk);

                RoundRect(hdc, rcChk.left, rcChk.top, rcChk.right, rcChk.bottom, 3, 3);

                SelectObject(hdc, oB); SelectObject(hdc, oP);

                DeleteObject(hbrChk); DeleteObject(hpChk);



                if (bu.isSelected)

                {

                    SelectObject(hdc, g_State.hFontIcon ? g_State.hFontIcon : g_State.hFontMain);

                    SetTextColor(hdc, RGB(255, 255, 255));

                    RECT rcGlyph = { rcChk.left, rcChk.top - 1, rcChk.right, rcChk.bottom };

                    DrawTextW(hdc, L"\xE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }



                // Unit Index #

                SelectObject(hdc, g_State.hFontSmall);

                SetTextColor(hdc, RGB(140, 155, 175));

                RECT rcNum = { rc.left + 40, rc.top, rc.right - 2, rc.bottom };

                DrawTextW(hdc, std::to_wstring(dRow.unitIndex + 1).c_str(), -1, &rcNum, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }

            else // Notice row

            {

                SelectObject(hdc, g_State.hFontMain);

                SetTextColor(hdc, RGB(90, 105, 125));

                RECT rcBranch = { rc.left + 6, rc.top, rc.right - 2, rc.bottom };

                DrawTextW(hdc, L"↳", -1, &rcBranch, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        // Column 1: Consist / Unit Name

        if (subItemIndex == 1)

        {

            RECT rc = rcCell;

            rc.left += 6; rc.right -= 6;

            if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

            {

                SelectObject(hdc, g_State.hFontBold);

                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(245, 245, 245));

                bool isActivity = (bc.filePath.rfind(L"ACTIVITY:", 0) == 0);

                std::wstring nameStr = isActivity ? (L"[Act] " + bc.consistName) : bc.fileName;

                DrawTextW(hdc, nameStr.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

            else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

            {

                const auto& bu = bc.brokenUnits[dRow.unitIndex];

                SelectObject(hdc, g_State.hFontMain);

                SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : (bu.isBroken ? RGB(255, 130, 130) : RGB(215, 225, 240)));

                DrawTextW(hdc, bu.uid.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

            else // Notice row

            {

                SelectObject(hdc, g_State.hFontSmall);

                SetTextColor(hdc, RGB(140, 150, 165));

                DrawTextW(hdc, L"(Empty Formation)", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        // Column 2: Type Pill Badge

        if (subItemIndex == 2)

        {

            RECT rc = rcCell;

            rc.top += 4; rc.bottom -= 4;

            rc.left += 4; rc.right -= 4;

            if (rc.right > rc.left)

            {

                if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

                {

                    COLORREF pillBg = RGB(22, 34, 48);

                    COLORREF pillBorder = RGB(44, 68, 96);

                    COLORREF pillText = RGB(120, 180, 245);

                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);

                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                    SelectObject(hdc, oB); SelectObject(hdc, oP);

                    DeleteObject(hbr); DeleteObject(hp);



                    SelectObject(hdc, g_State.hFontSmall);

                    SetTextColor(hdc, pillText);

                    std::wstring cntStr = std::to_wstring(bc.brokenUnits.size()) + L" Units";

                    DrawTextW(hdc, cntStr.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

                else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

                {

                    const auto& bu = bc.brokenUnits[dRow.unitIndex];

                    bool isEng = bu.isEngine;

                    COLORREF pillBg = isEng ? RGB(18, 48, 88) : RGB(48, 36, 18);

                    COLORREF pillBorder = isEng ? RGB(32, 90, 160) : RGB(120, 85, 35);

                    COLORREF pillText = isEng ? RGB(100, 185, 255) : RGB(255, 175, 80);



                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);

                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                    SelectObject(hdc, oB); SelectObject(hdc, oP);

                    DeleteObject(hbr); DeleteObject(hp);



                    SelectObject(hdc, g_State.hFontSmall);

                    SetTextColor(hdc, pillText);

                    DrawTextW(hdc, isEng ? L"Engine" : L"Wagon", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        // Column 3: Parent Directory / File

        if (subItemIndex == 3)

        {

            RECT rc = rcCell;

            rc.left += 6; rc.right -= 6;

            SelectObject(hdc, g_State.hFontSmall);

            SetTextColor(hdc, isSelected ? RGB(255, 255, 255) : RGB(150, 165, 185));



            if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

            {

                DrawTextW(hdc, bc.filePath.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

            else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

            {

                const auto& bu = bc.brokenUnits[dRow.unitIndex];

                DrawTextW(hdc, bu.parentDir.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        // Column 4: Status Pill Badge

        if (subItemIndex == 4)

        {

            RECT rc = rcCell;

            rc.top += 4; rc.bottom -= 4;

            rc.left += 6; rc.right -= 6;

            if (rc.right > rc.left)

            {

                if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

                {

                    int brokenCount = 0;

                    for (const auto& bu : bc.brokenUnits) { if (bu.isBroken) brokenCount++; }



                    COLORREF pillBg;

                    COLORREF pillBorder;

                    COLORREF pillText;

                    std::wstring statText;



                    if (bc.brokenUnits.empty())

                    {

                        pillBg = RGB(42, 34, 20); pillBorder = RGB(95, 75, 40); pillText = RGB(255, 170, 80);

                        statText = L"○ 0 Units";

                    }

                    else if (brokenCount > 0)

                    {

                        pillBg = RGB(56, 26, 28); pillBorder = RGB(130, 50, 55); pillText = RGB(255, 95, 80);

                        statText = L"⚠ " + std::to_wstring(brokenCount) + L" Missing";

                    }

                    else

                    {

                        pillBg = RGB(20, 48, 32); pillBorder = RGB(40, 110, 70); pillText = RGB(90, 190, 110);

                        statText = L"✓ Healthy";

                    }



                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);

                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                    SelectObject(hdc, oB); SelectObject(hdc, oP);

                    DeleteObject(hbr); DeleteObject(hp);



                    SelectObject(hdc, g_State.hFontSmall);

                    SetTextColor(hdc, pillText);

                    DrawTextW(hdc, statText.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

                else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

                {

                    const auto& bu = bc.brokenUnits[dRow.unitIndex];

                    COLORREF pillBg = bu.isBroken ? RGB(56, 22, 26) : RGB(20, 48, 30);

                    COLORREF pillBorder = bu.isBroken ? RGB(140, 45, 52) : RGB(40, 110, 65);

                    COLORREF pillText = bu.isBroken ? RGB(255, 80, 80) : RGB(90, 190, 110);



                    HBRUSH hbr = CreateSolidBrush(pillBg); HPEN hp = CreatePen(PS_SOLID, 1, pillBorder);

                    HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                    SelectObject(hdc, oB); SelectObject(hdc, oP);

                    DeleteObject(hbr); DeleteObject(hp);



                    SelectObject(hdc, g_State.hFontSmall);

                    SetTextColor(hdc, pillText);

                    DrawTextW(hdc, bu.isBroken ? L"MISSING" : L"READY", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        // Column 5: Target Mutation / Routing Pill Button

        if (subItemIndex == 5)

        {

            RECT rc = rcCell;

            rc.top += 3; rc.bottom -= 3;

            rc.left += 4; rc.right -= 6;

            if (rc.right > rc.left)

            {

                if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

                {

                    if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit)

                    {

                        SelectObject(hdc, g_State.hFontSmall);

                        SetTextColor(hdc, RGB(255, 120, 120));

                        DrawTextW(hdc, L"🗑 Purge Marked", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    }

                    else

                    {

                        const auto* pSrc = FindSourceNodeById(bc.assignedSourceId);

                        COLORREF srcCol = pSrc ? pSrc->color : RGB(56, 189, 248);

                        std::wstring srcName = pSrc ? pSrc->name : L"Source A";



                        HBRUSH hbr = CreateSolidBrush(RGB(22, 22, 28));

                        HPEN hp = CreatePen(PS_SOLID, 1, srcCol);

                        HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                        SelectObject(hdc, oB); SelectObject(hdc, oP);

                        DeleteObject(hbr); DeleteObject(hp);



                        // Colored Dot

                        HBRUSH hbrDot = CreateSolidBrush(srcCol);

                        HGDIOBJ oDot = SelectObject(hdc, hbrDot);

                        SelectObject(hdc, GetStockObject(NULL_PEN));

                        Ellipse(hdc, rc.left + 6, rc.top + (rc.bottom - rc.top - 8) / 2, rc.left + 14, rc.top + (rc.bottom - rc.top - 8) / 2 + 8);

                        SelectObject(hdc, oDot);

                        DeleteObject(hbrDot);



                        SelectObject(hdc, g_State.hFontSmall);

                        SetTextColor(hdc, srcCol);

                        RECT rcTxt = { rc.left + 18, rc.top, rc.right - 14, rc.bottom };

                        DrawTextW(hdc, srcName.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                        DrawSolidTriangle(hdc, rc.right - 8, (rc.top + rc.bottom) / 2, srcCol, true, 3);

                    }

                }

                else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

                {

                    const auto& bu = bc.brokenUnits[dRow.unitIndex];

                    if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit)

                    {

                        SelectObject(hdc, g_State.hFontSmall);

                        SetTextColor(hdc, bu.isSelected ? RGB(255, 100, 100) : RGB(140, 140, 150));

                        DrawTextW(hdc, bu.isSelected ? L"➔ Purge Unit" : L"Keep Unit", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    }

                    else if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::RebuildConsist)

                    {

                        SelectObject(hdc, g_State.hFontSmall);

                        SetTextColor(hdc, RGB(200, 160, 255));

                        DrawTextW(hdc, L"(Rebuild Entire)", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    }

                    else // ReplaceUnit

                    {

                        const auto* pSrc = FindSourceNodeById(bu.assignedSourceId);

                        COLORREF srcCol = pSrc ? pSrc->color : RGB(56, 189, 248);

                        std::wstring srcLetter = pSrc ? (pSrc->name.size() >= 7 ? pSrc->name.substr(7) : pSrc->name) : L"A";



                        HBRUSH hbr = CreateSolidBrush(RGB(22, 22, 28));

                        HPEN hp = CreatePen(PS_SOLID, 1, srcCol);

                        HGDIOBJ oB = SelectObject(hdc, hbr); HGDIOBJ oP = SelectObject(hdc, hp);

                        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

                        SelectObject(hdc, oB); SelectObject(hdc, oP);

                        DeleteObject(hbr); DeleteObject(hp);



                        // Colored Dot

                        HBRUSH hbrDot = CreateSolidBrush(srcCol);

                        HGDIOBJ oDot = SelectObject(hdc, hbrDot);

                        SelectObject(hdc, GetStockObject(NULL_PEN));

                        Ellipse(hdc, rc.left + 6, rc.top + (rc.bottom - rc.top - 8) / 2, rc.left + 14, rc.top + (rc.bottom - rc.top - 8) / 2 + 8);

                        SelectObject(hdc, oDot);

                        DeleteObject(hbrDot);



                        SelectObject(hdc, g_State.hFontSmall);

                        SetTextColor(hdc, srcCol);

                        RECT rcTxt = { rc.left + 18, rc.top, rc.right - 14, rc.bottom };

                        std::wstring label = L"SRC " + srcLetter;

                        DrawTextW(hdc, label.c_str(), -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                        DrawSolidTriangle(hdc, rc.right - 8, (rc.top + rc.bottom) / 2, srcCol, true, 3);

                    }

                }

            }



            SelectObject(hdc, hOldFont);

            SetBkMode(hdc, oldBkMode);

            return true;

        }



        SelectObject(hdc, hOldFont);

        SetBkMode(hdc, oldBkMode);

        return false;

    }



    static bool Mutator_IsCellInteractive(int itemIndex, int subItemIndex, void* pParam)

    {

        if (itemIndex < 0 || itemIndex >= (int)g_State.displayRows.size()) return false;

        if (subItemIndex == 0) return true; // Chevron / Checkbox

        if (subItemIndex == 5) return true; // Source Node / Action pill

        return false;

    }



    static void ShowFilterDropdown(HWND hWnd)

    {

        std::vector<DropDownItem> items;

        items.push_back(DropDownItem::Action(1, L"\xE7BA", L"Broken + Empty", L"Show consists with missing stock or 0 units",

            g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenAndEmpty, true));

        items.push_back(DropDownItem::Action(2, L"\xE730", L"Broken Only",    L"Show only consists with missing/broken units",

            g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenOnly, true));

        items.push_back(DropDownItem::Action(3, L"\xE894", L"Empty Only",     L"Show only consists with 0 units",

            g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::EmptyOnly, true));

        items.push_back(DropDownItem::Separator());

        items.push_back(DropDownItem::Action(4, L"\xE74C", L"All Consists",   L"Show all consists including healthy",

            g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::All, true));



        int curId = 4;

        if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenAndEmpty) curId = 1;

        else if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenOnly) curId = 2;

        else if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::EmptyOnly) curId = 3;

        else if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::All) curId = 4;



        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, g_State.rcTab2FilterBtn, items, curId);

        if (chosen == 1) g_State.tab2Filter = MutatorDlgState::Tab2FilterMode::BrokenAndEmpty;

        else if (chosen == 2) g_State.tab2Filter = MutatorDlgState::Tab2FilterMode::BrokenOnly;

        else if (chosen == 3) g_State.tab2Filter = MutatorDlgState::Tab2FilterMode::EmptyOnly;

        else if (chosen == 4) g_State.tab2Filter = MutatorDlgState::Tab2FilterMode::All;

        g_State.tab2SelectedConsistIdx = -1;

        PopulateBatchConsistMatrix();

        InvalidateRect(hWnd, NULL, FALSE);

    }



    static void ShowSourceDropdownForConsist(HWND hWnd, PoolMutator::BrokenConsistInfo& bcon, const RECT& rcAnchor)

    {

        std::vector<DropDownItem> items;

        for (size_t i = 0; i < g_State.sourceNodes.size(); ++i)

        {

            const auto& node = g_State.sourceNodes[i];

            bool isCurrent = (node.sourceId == bcon.assignedSourceId);

            // Only offer Pool Preset / Blueprint for Rebuild action

            bool validForRebuild = (node.sourceType == 1 || node.sourceType == 2);

            bool skip = (bcon.actionExecutionMode == 1 && !validForRebuild);

            if (!skip)

            {

                items.push_back(DropDownItem::Action(node.sourceId + 1, L"", node.name,

                    GetSourceNodeSummaryText(node), isCurrent, true));

            }

        }

        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items, bcon.assignedSourceId + 1);

        if (chosen >= 1)

        {

            int newSrcId = chosen - 1;

            bcon.assignedSourceId = newSrcId;

            for (auto& bu : bcon.brokenUnits) bu.assignedSourceId = newSrcId;

            PopulateBatchConsistMatrix();

        }

        InvalidateRect(hWnd, NULL, FALSE);

    }



    static void ShowSourceDropdownForUnit(HWND hWnd, PoolMutator::BrokenUnitInfo& bu, const RECT& rcAnchor)

    {

        std::vector<DropDownItem> items;

        for (size_t i = 0; i < g_State.sourceNodes.size(); ++i)

        {

            const auto& node = g_State.sourceNodes[i];

            bool isCurrent = (node.sourceId == bu.assignedSourceId);

            items.push_back(DropDownItem::Action(node.sourceId + 1, L"", node.name,

                GetSourceNodeSummaryText(node), isCurrent, true));

        }

        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, rcAnchor, items, bu.assignedSourceId + 1);

        if (chosen >= 1)

        {

            bu.assignedSourceId = chosen - 1;

            PopulateBatchConsistMatrix();

            InvalidateRect(hWnd, NULL, FALSE);

        }

    }



    static void ShowSourceTypeDropdown(HWND hWnd, int sourceNodeIndex, const RECT& anchorRc)

    {

        if (sourceNodeIndex < 0 || sourceNodeIndex >= (int)g_State.sourceNodes.size()) return;

        auto& node = g_State.sourceNodes[sourceNodeIndex];



        std::vector<DropDownItem> items;

        items.push_back(DropDownItem::Action(1, L"\xE734", L"Favourite Group", L"Pick units from custom favourite unit groups", node.sourceType == 0, true));

        items.push_back(DropDownItem::Action(2, L"\xE71D", L"Pool Preset", L"Pick units from library pool presets", node.sourceType == 1, true));

        items.push_back(DropDownItem::Action(3, L"\xE7C0", L"Train Configs", L"Pick units from .train configuration templates", node.sourceType == 2, true));



        int chosen = CustomDropDownMenu::ShowSingleSelect(hWnd, anchorRc, items, node.sourceType + 1);

        if (chosen >= 1 && chosen <= 3)

        {

            int newType = chosen - 1;

            if (newType != node.sourceType)

            {

                node.sourceType = newType;

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

            }

        }

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



            std::set<std::wstring> allFolders;

            std::map<std::wstring, std::vector<size_t>> directConfigs;

            for (size_t c = 0; c < TrainConfigManager::g_LoadedConfigsCache.size(); ++c)

            {

                const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[c];

                std::wstring cat = cfg.category.empty() ? L"General" : cfg.category;

                directConfigs[cat].push_back(c);

                allFolders.insert(cat);



                // Insert all parent prefixes

                size_t pos = 0;

                while ((pos = cat.find(L'\\', pos)) != std::wstring::npos)

                {

                    std::wstring parentPrefix = cat.substr(0, pos);

                    if (!parentPrefix.empty()) allFolders.insert(parentPrefix);

                    pos++;

                }

            }



            auto diskCats = TrainConfigManager::GetCategories();

            for (const auto& dc : diskCats)

            {

                allFolders.insert(dc);

            }



            std::vector<std::wstring> sortedFolders(allFolders.begin(), allFolders.end());

            std::sort(sortedFolders.begin(), sortedFolders.end());



            for (const auto& folder : sortedFolders)

            {

                size_t lastSlash = folder.rfind(L'\\');

                std::wstring parentGroup = (lastSlash != std::wstring::npos) ? folder.substr(0, lastSlash) : L"";

                std::wstring leafName = (lastSlash != std::wstring::npos) ? folder.substr(lastSlash + 1) : folder;

                int indent = 0;

                for (wchar_t ch : folder) { if (ch == L'\\') indent++; }



                int totalInBranch = 0;

                for (const auto& cfg : TrainConfigManager::g_LoadedConfigsCache)

                {

                    std::wstring cCat = cfg.category.empty() ? L"General" : cfg.category;

                    if (cCat == folder || cCat.rfind(folder + L"\\", 0) == 0) totalInBranch++;

                }



                items.push_back(DropDownItem::GroupHeader(leafName, L"\xE8B7", totalInBranch, false, 0, false, false, std::to_wstring(totalInBranch) + L" Blueprints", parentGroup, indent, folder));



                if (directConfigs.find(folder) != directConfigs.end())

                {

                    for (size_t cIdx : directConfigs[folder])

                    {

                        const auto& cfg = TrainConfigManager::g_LoadedConfigsCache[cIdx];

                        std::wstring tag = cfg.category;

                        if (cfg.maxSpeedKmph > 0) tag += L" • " + std::to_wstring((int)cfg.maxSpeedKmph) + L" km/h";

                        bool isCurrent = (g_State.isUsingTrainConfig && g_State.activeTrainConfigPath == cfg.filePath);

                        items.push_back(DropDownItem::Action(1000 + (int)cIdx, isCurrent ? L"\xE73E" : L"\xE7C0", cfg.name, tag, isCurrent, true, folder, folder, indent + 1));

                    }

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



            // Sync actionExecutionMode to all selected consists based on current action selection

            for (auto& bc : g_State.brokenConsists)

            {

                if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::RebuildConsist)

                {

                    bc.actionExecutionMode = 1;

                }

                else if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit)

                {

                    bc.actionExecutionMode = 2;

                }

                else

                {

                    bc.actionExecutionMode = 0;

                }

            }



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

                std::wstring successMsg;

                if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit)

                {

                    successMsg = L"Consist Unit Purge Operation Completed!\n\n";

                    successMsg += L"Processed " + std::to_wstring(res.processedCount) + L" consist(s).\n";

                    if (res.repairedUnitsCount > 0)

                    {

                        successMsg += L"Purged / Removed " + std::to_wstring(res.repairedUnitsCount) + L" unit(s).\n";

                    }

                }

                else if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::RebuildConsist)

                {

                    successMsg = L"Consist Rebuild Operation Completed!\n\n";

                    successMsg += L"Processed " + std::to_wstring(res.processedCount) + L" consist(s).\n";

                    if (res.repairedUnitsCount > 0)

                    {

                        successMsg += L"Rebuilt with " + std::to_wstring(res.repairedUnitsCount) + L" new unit(s).\n";

                    }

                }

                else

                {

                    successMsg = L"Consist Repair Operation Completed!\n\n";

                    successMsg += L"Processed " + std::to_wstring(res.processedCount) + L" consist(s).\n";

                    if (res.repairedUnitsCount > 0)

                    {

                        successMsg += L"Replaced / Repaired " + std::to_wstring(res.repairedUnitsCount) + L" unit(s).\n";

                    }

                }

                successMsg += L"\nChanges staged in session memory (●). Use 'Save Consist(s)' (Ctrl+S) when ready to commit to disk.";



                ShowModernMessageBox(hWnd, successMsg.c_str(), L"Batch Consist Editing", MB_OK | MB_ICONINFORMATION);



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

                std::wstring err = res.errorMessage.empty() ? L"No broken units or consists were selected or an error occurred." : res.errorMessage;

                ShowModernMessageBox(hWnd, err.c_str(), L"Batch Edit Error", MB_OK | MB_ICONERROR);

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



        case WM_NOTIFY:

        {

            if (g_State.hBatchConsistListWnd && lParam && ((NMHDR*)lParam)->hwndFrom == g_State.hBatchConsistListWnd)

            {

                NMHDR* pnmh = (NMHDR*)lParam;

                if (pnmh->code == NM_CELLCLICK)

                {

                    NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;

                    int clickedRow = pCell->itemIndex;

                    int clickedCol = pCell->subItemIndex;



                    if (clickedRow >= 0 && clickedRow < (int)g_State.displayRows.size())

                    {

                        const auto& dRow = g_State.displayRows[clickedRow];

                        if (dRow.consistIndex >= 0 && dRow.consistIndex < (int)g_State.brokenConsists.size())

                        {

                            auto& bc = g_State.brokenConsists[dRow.consistIndex];

                            auto& cState = GetConsistSectionState(dRow.consistIndex);



                            if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

                            {

                                if (clickedCol == 0)

                                {

                                    POINT pt;

                                    GetCursorPos(&pt);

                                    ScreenToClient(g_State.hBatchConsistListWnd, &pt);



                                    // Col 0 is 60px wide: checkbox is at 20..36

                                    if (pt.x >= 18 && pt.x <= 38)

                                    {

                                        bc.isSelected = !bc.isSelected;

                                        for (auto& bu : bc.brokenUnits) bu.isSelected = bc.isSelected;

                                    }

                                    else

                                    {

                                        cState.isExpanded = !cState.isExpanded;

                                    }

                                    PopulateBatchConsistMatrix();

                                    InvalidateRect(hWnd, NULL, FALSE);

                                    return 0;

                                }

                                else if (clickedCol == 5 && g_State.tab2Action != MutatorDlgState::Tab2ActionMode::PurgeUnit)

                                {

                                    RECT rcAnchor = { 0 };

                                    if (g_State.batchConsistList.GetCellRect(clickedRow, 5, rcAnchor))

                                    {

                                        MapWindowPoints(g_State.hBatchConsistListWnd, hWnd, (LPPOINT)&rcAnchor, 2);

                                        rcAnchor.top += 3;

                                        rcAnchor.bottom -= 3;

                                        rcAnchor.left += 4;

                                        rcAnchor.right -= 6;

                                    }

                                    else

                                    {

                                        POINT ptScreen;

                                        GetCursorPos(&ptScreen);

                                        ScreenToClient(hWnd, &ptScreen);

                                        rcAnchor = { ptScreen.x - 50, ptScreen.y, ptScreen.x + 50, ptScreen.y + 20 };

                                    }

                                    ShowSourceDropdownForConsist(hWnd, bc, rcAnchor);

                                    PopulateBatchConsistMatrix();

                                    return 0;

                                }

                                else

                                {

                                    g_State.tab2SelectedConsistIdx = dRow.consistIndex;

                                    PopulateBatchConsistMatrix();

                                    InvalidateRect(hWnd, NULL, FALSE);

                                    return 0;

                                }

                            }

                            else if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_UNIT_ENTRY && dRow.unitIndex >= 0 && dRow.unitIndex < (int)bc.brokenUnits.size())

                            {

                                auto& bu = bc.brokenUnits[dRow.unitIndex];

                                if (clickedCol == 0)

                                {

                                    bu.isSelected = !bu.isSelected;

                                    bool anySel = false;

                                    for (const auto& u : bc.brokenUnits) { if (u.isSelected) { anySel = true; break; } }

                                    bc.isSelected = anySel;

                                    PopulateBatchConsistMatrix();

                                    InvalidateRect(hWnd, NULL, FALSE);

                                    return 0;

                                }

                                else if (clickedCol == 5 && g_State.tab2Action == MutatorDlgState::Tab2ActionMode::ReplaceUnit)

                                {

                                    RECT rcAnchor = { 0 };

                                    if (g_State.batchConsistList.GetCellRect(clickedRow, 5, rcAnchor))

                                    {

                                        MapWindowPoints(g_State.hBatchConsistListWnd, hWnd, (LPPOINT)&rcAnchor, 2);

                                        rcAnchor.top += 3;

                                        rcAnchor.bottom -= 3;

                                        rcAnchor.left += 4;

                                        rcAnchor.right -= 6;

                                    }

                                    else

                                    {

                                        POINT ptScreen;

                                        GetCursorPos(&ptScreen);

                                        ScreenToClient(hWnd, &ptScreen);

                                        rcAnchor = { ptScreen.x - 50, ptScreen.y, ptScreen.x + 50, ptScreen.y + 20 };

                                    }

                                    ShowSourceDropdownForUnit(hWnd, bu, rcAnchor);

                                    PopulateBatchConsistMatrix();

                                    return 0;

                                }

                                else

                                {

                                    bu.isSelected = !bu.isSelected;

                                    bool anySel = false;

                                    for (const auto& u : bc.brokenUnits) { if (u.isSelected) { anySel = true; break; } }

                                    bc.isSelected = anySel;

                                    PopulateBatchConsistMatrix();

                                    InvalidateRect(hWnd, NULL, FALSE);

                                    return 0;

                                }

                            }

                        }

                    }

                }

                else if (pnmh->code == NM_DBLCLK)

                {

                    NMCELLCLICK* pCell = (NMCELLCLICK*)lParam;

                    int clickedRow = (pCell && pCell->itemIndex >= 0) ? pCell->itemIndex : g_State.batchConsistList.GetSelectedIndex();

                    if (clickedRow >= 0 && clickedRow < (int)g_State.displayRows.size())

                    {

                        const auto& dRow = g_State.displayRows[clickedRow];

                        if (dRow.consistIndex >= 0 && dRow.consistIndex < (int)g_State.brokenConsists.size())

                        {

                            if (dRow.rowType == MutatorDlgState::MUTATOR_ROW_CONSIST_HEADER)

                            {

                                auto& cState = GetConsistSectionState(dRow.consistIndex);

                                cState.isExpanded = !cState.isExpanded;

                                PopulateBatchConsistMatrix();

                                InvalidateRect(hWnd, NULL, FALSE);

                                return 0;

                            }

                        }

                    }

                }

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



            // Create Batch Consist Matrix List Control with Sound Studio Card Styling

            g_State.hBatchConsistListWnd = g_State.batchConsistList.Create(hWnd, 16, 118, 600, 400, 205);

            if (g_State.hBatchConsistListWnd)

            {

                g_State.batchConsistList.SetShowSelectionGutter(false);

                g_State.batchConsistList.SetMultiSelect(false);

                g_State.batchConsistList.SetShowVerticalDividers(true);

                g_State.batchConsistList.SetDrawCardBorder(true);

                g_State.batchConsistList.SetAutoFitLastColumn(false);

                g_State.batchConsistList.SetRowHeight(28);

                g_State.batchConsistList.SetRowBackgroundCallback(Mutator_RowBackgroundCallback, &g_State);

                g_State.batchConsistList.SetDrawCellCallback(Mutator_DrawCellCallback, &g_State);

                g_State.batchConsistList.SetCellInteractiveCallback(Mutator_IsCellInteractive, &g_State);



                g_State.batchConsistList.AddColumn(L"#", 64, 0);

                g_State.batchConsistList.AddColumn(L"Consist / Unit Name", 220, 0);

                g_State.batchConsistList.AddColumn(L"Type", 85, 1);

                g_State.batchConsistList.AddColumn(L"Parent Directory / File", 180, 0);

                g_State.batchConsistList.AddColumn(L"Current Status", 115, 1);

                g_State.batchConsistList.AddColumn(L"Target Mutation / Source", 170, 0);



                ShowWindow(g_State.hBatchConsistListWnd, (g_State.activeTab == 2) ? SW_SHOW : SW_HIDE);

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

            bool hTab2Filter = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2FilterBtn, pt);

            bool hTab2ActRep = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2ActionReplace, pt);

            bool hTab2ActReb = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2ActionRebuild, pt);

            bool hTab2ActPrg = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2ActionPurge, pt);

            bool hTab2SelBrk = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2BtnSelectBroken, pt);

            bool hTab2ScanLib = (g_State.activeTab == 2) && PtInRect(&g_State.rcTab2BtnScanLibrary, pt);

            bool hExpAll = (g_State.activeTab == 2) && PtInRect(&g_State.rcBtnExpandAll, pt);

            bool hColAll = (g_State.activeTab == 2) && PtInRect(&g_State.rcBtnCollapseAll, pt);

            bool hMChk = (g_State.activeTab == 2) && PtInRect(&g_State.rcMasterBrokenCheck, pt);

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

                hTab2Filter != g_State.isHoverTab2FilterBtn ||

                hTab2ActRep != g_State.isHoverTab2ActionReplace ||

                hTab2ActReb != g_State.isHoverTab2ActionRebuild ||

                hTab2ActPrg != g_State.isHoverTab2ActionPurge ||

                hTab2SelBrk != g_State.isHoverTab2BtnSelectBroken || hTab2ScanLib != g_State.isHoverTab2BtnScanLibrary ||

                hExpAll != g_State.isHoverBtnExpandAll || hColAll != g_State.isHoverBtnCollapseAll ||

                hMChk != g_State.isHoverMasterBroken ||

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

                g_State.isHoverTab2FilterBtn = hTab2Filter;

                g_State.isHoverTab2ActionReplace = hTab2ActRep;

                g_State.isHoverTab2ActionRebuild = hTab2ActReb;

                g_State.isHoverTab2ActionPurge = hTab2ActPrg;

                g_State.isHoverTab2BtnSelectBroken = hTab2SelBrk;

                g_State.isHoverTab2BtnScanLibrary = hTab2ScanLib;

                g_State.isHoverBtnExpandAll = hExpAll;

                g_State.isHoverBtnCollapseAll = hColAll;

                g_State.isHoverMasterBroken = hMChk;

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

            g_State.isHoverTab2FilterBtn = false;

            g_State.isHoverTab2ActionReplace = false;

            g_State.isHoverTab2ActionRebuild = false;

            g_State.isHoverTab2ActionPurge = false;

            for (auto& card : g_State.sourceCardUIs)

            {

                card.isHoverPicker = false;

                card.isHoverToggle = false;

                card.isHoverDelete = false;

            }

            g_State.isHoverTab2BtnSelectBroken = false;

            g_State.isHoverTab2BtnScanLibrary = false;

            g_State.isHoverBtnExpandAll = false;

            g_State.isHoverBtnCollapseAll = false;

            g_State.isHoverTab2FilterBtn = false;

            g_State.isHoverMasterBroken = false;

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

            // TAB 2: REPLACE / REPAIR / PURGE BROKEN UNITS

            // ==========================================

            else if (g_State.activeTab == 2)

            {

                // Top Ribbon: Filter Dropdown Button

                if (PtInRect(&g_State.rcTab2FilterBtn, pt))

                {

                    ShowFilterDropdown(hWnd);

                    return 0;

                }



                // Header: Expand All Button

                if (PtInRect(&g_State.rcBtnExpandAll, pt))

                {

                    for (size_t i = 0; i < g_State.brokenConsists.size(); ++i)

                    {

                        GetConsistSectionState(i).isExpanded = true;

                    }

                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }



                // Header: Collapse All Button

                if (PtInRect(&g_State.rcBtnCollapseAll, pt))

                {

                    for (size_t i = 0; i < g_State.brokenConsists.size(); ++i)

                    {

                        GetConsistSectionState(i).isExpanded = false;

                    }

                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }



                // Header: Master Checkbox (Select All / Deselect All for filtered consists)

                if (PtInRect(&g_State.rcMasterBrokenCheck, pt))

                {

                    int totalItems = 0;

                    int selectedItems = 0;

                    for (auto& bc : g_State.brokenConsists)

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

                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }



                // Header: "Select Broken Only" Quick Action Button

                if (PtInRect(&g_State.rcTab2BtnSelectBroken, pt))

                {

                    for (auto& con : g_State.brokenConsists)

                    {

                        bool anyBroken = false;

                        for (auto& bu : con.brokenUnits)

                        {

                            bu.isSelected = bu.isBroken;

                            if (bu.isSelected) anyBroken = true;

                        }

                        con.isSelected = anyBroken || con.brokenUnits.empty();

                    }

                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }



                // Empty State: Scan Entire Library Button

                if (PtInRect(&g_State.rcTab2BtnScanLibrary, pt))

                {

                    RefreshBrokenConsists(true);

                    InvalidateRect(hWnd, NULL, TRUE);

                    return 0;

                }



                // Right Pane: Action Mode Segmented Buttons [Replace | Rebuild | Purge]

                if (PtInRect(&g_State.rcTab2ActionReplace, pt))

                {

                    g_State.tab2Action = MutatorDlgState::Tab2ActionMode::ReplaceUnit;

                    bool anySel = false;

                    for (const auto& bc : g_State.brokenConsists) { if (bc.isSelected) { anySel = true; break; } }

                    for (auto& bc : g_State.brokenConsists)

                    {

                        if (!anySel || bc.isSelected) bc.actionExecutionMode = 0;

                    }


                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }

                if (PtInRect(&g_State.rcTab2ActionRebuild, pt))

                {

                    g_State.tab2Action = MutatorDlgState::Tab2ActionMode::RebuildConsist;

                    bool anySel = false;

                    for (const auto& bc : g_State.brokenConsists) { if (bc.isSelected) { anySel = true; break; } }

                    for (auto& bc : g_State.brokenConsists)

                    {

                        if (!anySel || bc.isSelected)

                        {

                            bc.actionExecutionMode = 1;

                            const auto* curSrc = FindSourceNodeById(bc.assignedSourceId);

                            if (!curSrc || (curSrc->sourceType != 1 && curSrc->sourceType != 2))

                            {

                                for (const auto& n : g_State.sourceNodes)

                                {

                                    if (n.sourceType == 1 || n.sourceType == 2)

                                    {

                                        bc.assignedSourceId = n.sourceId;

                                        for (auto& bu : bc.brokenUnits) bu.assignedSourceId = n.sourceId;

                                        break;

                                    }

                                }

                            }

                        }

                    }


                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }

                if (PtInRect(&g_State.rcTab2ActionPurge, pt))

                {

                    g_State.tab2Action = MutatorDlgState::Tab2ActionMode::PurgeUnit;

                    bool anySel = false;

                    for (const auto& bc : g_State.brokenConsists) { if (bc.isSelected) { anySel = true; break; } }

                    for (auto& bc : g_State.brokenConsists)

                    {

                        if (!anySel || bc.isSelected) bc.actionExecutionMode = 2;

                    }


                    PopulateBatchConsistMatrix();

                    InvalidateRect(hWnd, NULL, FALSE);

                    return 0;

                }



                // Right Pane: Source Nodes Configuration

                if (g_State.tab2Action != MutatorDlgState::Tab2ActionMode::PurgeUnit)

                {

                    // Add Source Button

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



                    // Source Cards (Type Dropdown, Delete Button, Picker Dropdown)

                    for (size_t k = 0; k < g_State.sourceCardUIs.size(); ++k)

                    {

                        const auto& cardUI = g_State.sourceCardUIs[k];

                        if (cardUI.sourceId < 0 || k >= g_State.sourceNodes.size()) continue;

                        auto& node = g_State.sourceNodes[k];



                        if (PtInRect(&cardUI.rcTypeToggle, pt))

                        {

                            ShowSourceTypeDropdown(hWnd, (int)k, cardUI.rcTypeToggle);

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

                            else

                            {

                                ShowBlueprintDropdownForSource(hWnd, (int)k, cardUI.rcPicker);

                            }

                            return 0;

                        }

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

        {

            return 0;

        }



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



            if (g_State.activeTab != 2)

            {

                // Tabs 0 & 1: Standard Ribbon Header

                SelectObject(hmemDC, g_State.hFontBold);

                SIZE sumSz = { 0 };

                GetTextExtentPoint32W(hmemDC, targetSummary.c_str(), (int)targetSummary.length(), &sumSz);

                int statusW = (std::max)(180, (int)sumSz.cx + 10);

                if (statusW > rcClient.right - 180) statusW = rcClient.right - 180;



                RECT rcStatus = { rcClient.right - statusW - 20, toolbarY, rcClient.right - 20, toolbarY + toolbarH };

                RECT rcTitle = { 54, toolbarY + 5, rcStatus.left - 16, toolbarY + 24 };

                RECT rcSub = { 54, toolbarY + 24, rcStatus.left - 16, toolbarY + 42 };



                SetTextColor(hmemDC, textPrimary);

                const wchar_t* tabTitle = (g_State.activeTab == 0) ? L"Whole Consist Pool Overhaul / Mutation" : L"Position-Based Unit Injection";

                DrawTextW(hmemDC, tabTitle, -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, textSecondary);

                const wchar_t* tabSub = (g_State.activeTab == 0) ? L"Regenerate and vary entire consist file(s) drawn from selected Pool Preset" :

                                          L"Inject new units into consist file(s) at Head, Behind Locos, Tail, or Specific Index";

                DrawTextW(hmemDC, tabSub, -1, &rcSub, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                SelectObject(hmemDC, g_State.hFontBold);

                SetTextColor(hmemDC, accentCol);

                DrawTextW(hmemDC, targetSummary.c_str(), -1, &rcStatus, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }

            else

            {

                // Tab 2: Integrated Command Bar & Counters in Burgundy Ribbon Header

                int totalConsists = (int)g_State.brokenConsists.size();

                int totalUnits = 0;

                int totalBrokenUnits = 0;

                int selectedUnits = 0;

                for (const auto& bc : g_State.brokenConsists)

                {

                    if (bc.brokenUnits.empty())

                    {

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



                // Command Buttons vertically centered in ribbon (Y = 66..112, H = 46)

                int btnH = 26;

                int btnY = toolbarY + (toolbarH - btnH) / 2; // Y = 76



                // Align buttons over the left grid column width, ending at leftGridRight

                int leftX = 16;

                int rightMargin = 16;

                int gap = 8;

                int totalW = rcClient.right - (leftX + rightMargin);

                int rightColW = (totalW - gap) * 34 / 100;

                if (rightColW < 320) rightColW = 320;

                int listW = totalW - gap - rightColW;

                int leftGridRight = leftX + listW;

                int rightX = leftX + listW + gap;



                int masterW = 58;

                int filtW = 112;

                int brokenBtnW = 100;

                int colBtnW = 84;

                int expBtnW = 74;



                int rightEdge = leftGridRight;

                if (rightEdge < 540) rightEdge = 540;



                g_State.rcMasterBrokenCheck = { rightEdge - masterW, btnY, rightEdge, btnY + btnH };

                int filtX = g_State.rcMasterBrokenCheck.left - filtW - 5;

                g_State.rcTab2FilterBtn = { filtX, btnY, filtX + filtW, btnY + btnH };

                int brokenBtnX = filtX - brokenBtnW - 5;

                g_State.rcTab2BtnSelectBroken = { brokenBtnX, btnY, brokenBtnX + brokenBtnW, btnY + btnH };

                int colBtnX = brokenBtnX - colBtnW - 5;

                g_State.rcBtnCollapseAll = { colBtnX, btnY, colBtnX + colBtnW, btnY + btnH };

                int expBtnX = colBtnX - expBtnW - 5;

                g_State.rcBtnExpandAll = { expBtnX, btnY, expBtnX + expBtnW, btnY + btnH };



                // 1. [ ☵ Expand ] Button

                COLORREF expBg = g_State.isHoverBtnExpandAll ? RGB(64, 28, 36) : RGB(40, 20, 26);

                COLORREF expBdr = g_State.isHoverBtnExpandAll ? RGB(140, 58, 72) : RGB(88, 38, 48);

                HBRUSH hbrExp = CreateSolidBrush(expBg); HPEN hpenExp = CreatePen(PS_SOLID, 1, expBdr);

                SelectObject(hmemDC, hbrExp); SelectObject(hmemDC, hpenExp);

                RoundRect(hmemDC, g_State.rcBtnExpandAll.left, g_State.rcBtnExpandAll.top, g_State.rcBtnExpandAll.right, g_State.rcBtnExpandAll.bottom, 4, 4);

                DeleteObject(hbrExp); DeleteObject(hpenExp);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, textPrimary);

                DrawTextW(hmemDC, L"☵ Expand", -1, &g_State.rcBtnExpandAll, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // 2. [ ☵ Collapse ] Button

                COLORREF colBg = g_State.isHoverBtnCollapseAll ? RGB(64, 28, 36) : RGB(40, 20, 26);

                COLORREF colBdr = g_State.isHoverBtnCollapseAll ? RGB(140, 58, 72) : RGB(88, 38, 48);

                HBRUSH hbrCol = CreateSolidBrush(colBg); HPEN hpenCol = CreatePen(PS_SOLID, 1, colBdr);

                SelectObject(hmemDC, hbrCol); SelectObject(hmemDC, hpenCol);

                RoundRect(hmemDC, g_State.rcBtnCollapseAll.left, g_State.rcBtnCollapseAll.top, g_State.rcBtnCollapseAll.right, g_State.rcBtnCollapseAll.bottom, 4, 4);

                DeleteObject(hbrCol); DeleteObject(hpenCol);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, textPrimary);

                DrawTextW(hmemDC, L"☵ Collapse", -1, &g_State.rcBtnCollapseAll, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // 3. [ ⚠ Broken Only ] Toggle Button

                bool isBrokenActive = (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenOnly);

                COLORREF brkBg = isBrokenActive ? RGB(78, 22, 28) : (g_State.isHoverTab2BtnSelectBroken ? RGB(64, 28, 36) : RGB(40, 20, 26));

                COLORREF brkBdr = isBrokenActive ? RGB(239, 68, 68) : (g_State.isHoverTab2BtnSelectBroken ? RGB(140, 58, 72) : RGB(88, 38, 48));

                COLORREF brkTxt = isBrokenActive ? RGB(255, 160, 160) : textPrimary;

                HBRUSH hbrBrk = CreateSolidBrush(brkBg); HPEN hpenBrk = CreatePen(PS_SOLID, 1, brkBdr);

                SelectObject(hmemDC, hbrBrk); SelectObject(hmemDC, hpenBrk);

                RoundRect(hmemDC, g_State.rcTab2BtnSelectBroken.left, g_State.rcTab2BtnSelectBroken.top, g_State.rcTab2BtnSelectBroken.right, g_State.rcTab2BtnSelectBroken.bottom, 4, 4);

                DeleteObject(hbrBrk); DeleteObject(hpenBrk);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, brkTxt);

                DrawTextW(hmemDC, L"⚠ Broken Only", -1, &g_State.rcTab2BtnSelectBroken, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // 4. [ All Consists ▼ ] Filter Dropdown Button

                COLORREF filtBg = g_State.isHoverTab2FilterBtn ? RGB(64, 28, 36) : RGB(40, 20, 26);

                COLORREF filtBorder = g_State.isHoverTab2FilterBtn ? RGB(140, 58, 72) : RGB(88, 38, 48);

                HBRUSH hbrFilt = CreateSolidBrush(filtBg); HPEN hpenFilt = CreatePen(PS_SOLID, 1, filtBorder);

                SelectObject(hmemDC, hbrFilt); SelectObject(hmemDC, hpenFilt);

                RoundRect(hmemDC, g_State.rcTab2FilterBtn.left, g_State.rcTab2FilterBtn.top, g_State.rcTab2FilterBtn.right, g_State.rcTab2FilterBtn.bottom, 4, 4);

                DeleteObject(hbrFilt); DeleteObject(hpenFilt);

                const wchar_t* filtText = L"All Consists";

                if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenAndEmpty) filtText = L"Broken + Empty";

                else if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::BrokenOnly) filtText = L"Broken Only";

                else if (g_State.tab2Filter == MutatorDlgState::Tab2FilterMode::EmptyOnly) filtText = L"Empty Only";

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, textPrimary);

                RECT rcFiltTxt = { g_State.rcTab2FilterBtn.left + 8, btnY, g_State.rcTab2FilterBtn.right - 18, btnY + btnH };

                DrawTextW(hmemDC, filtText, -1, &rcFiltTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                DrawSolidTriangle(hmemDC, g_State.rcTab2FilterBtn.right - 9, btnY + (btnH / 2), textSecondary, true, 3);



                // 5. [✓ All] Master Checkbox Pill Button

                COLORREF mBtnBg = g_State.isHoverMasterBroken ? RGB(64, 28, 36) : RGB(40, 20, 26);

                COLORREF mBtnBdr = g_State.isHoverMasterBroken ? RGB(140, 58, 72) : RGB(88, 38, 48);

                HBRUSH hbrMBtn = CreateSolidBrush(mBtnBg); HPEN hpMBtn = CreatePen(PS_SOLID, 1, mBtnBdr);

                SelectObject(hmemDC, hbrMBtn); SelectObject(hmemDC, hpMBtn);

                RoundRect(hmemDC, g_State.rcMasterBrokenCheck.left, g_State.rcMasterBrokenCheck.top, g_State.rcMasterBrokenCheck.right, g_State.rcMasterBrokenCheck.bottom, 4, 4);

                DeleteObject(hbrMBtn); DeleteObject(hpMBtn);



                int chkSz = 14;

                int chkBoxX = g_State.rcMasterBrokenCheck.left + 7;

                int chkBoxY = btnY + (btnH - chkSz) / 2;

                RECT rcMChkBox = { chkBoxX, chkBoxY, chkBoxX + chkSz, chkBoxY + chkSz };

                COLORREF chkBg = (isAllMaster || isIndeterminateMaster) ? accentCol : RGB(30, 16, 20);

                COLORREF chkBdr = (isAllMaster || isIndeterminateMaster) ? accentCol : RGB(100, 48, 58);

                HBRUSH hbrMChk = CreateSolidBrush(chkBg);

                HPEN hpenMChk = CreatePen(PS_SOLID, 1, chkBdr);

                hOldB = (HBRUSH)SelectObject(hmemDC, hbrMChk);

                hOldP = (HPEN)SelectObject(hmemDC, hpenMChk);

                RoundRect(hmemDC, rcMChkBox.left, rcMChkBox.top, rcMChkBox.right, rcMChkBox.bottom, 3, 3);

                if (isAllMaster)

                {

                    SelectObject(hmemDC, g_State.hFontIcon ? g_State.hFontIcon : g_State.hFontSmall);

                    SetTextColor(hmemDC, RGB(255, 255, 255));

                    RECT rcGlyph = { rcMChkBox.left, rcMChkBox.top - 1, rcMChkBox.right, rcMChkBox.bottom };

                    DrawTextW(hmemDC, L"\uE73E", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                }

                else if (isIndeterminateMaster)

                {

                    HBRUSH hbrInd = CreateSolidBrush(RGB(255, 255, 255));

                    RECT rcInd = { rcMChkBox.left + 3, rcMChkBox.top + 6, rcMChkBox.right - 3, rcMChkBox.top + 8 };

                    FillRect(hmemDC, &rcInd, hbrInd);

                    DeleteObject(hbrInd);

                }

                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP);

                DeleteObject(hbrMChk); DeleteObject(hpenMChk);



                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, textPrimary);

                RECT rcMText = { chkBoxX + chkSz + 5, btnY, g_State.rcMasterBrokenCheck.right - 4, btnY + btnH };

                DrawTextW(hmemDC, L"All", -1, &rcMText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Left Header Title: "Consists & Formations" (alone, so never truncates!)

                RECT rcLeftTitle = { 46, toolbarY, expBtnX - 12, toolbarY + toolbarH };

                SelectObject(hmemDC, g_State.hFontBold);

                SetTextColor(hmemDC, textPrimary);

                DrawTextW(hmemDC, L"Consists & Formations", -1, &rcLeftTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);



                // Right Section: Consist & Unit Selection Summary in the previously empty area above the right panel!

                RECT rcRightSummary = { rightX, toolbarY, rcClient.right - 16, toolbarY + toolbarH };

                std::wstring sumUnits = std::to_wstring(totalConsists) + L" consist(s), " + std::to_wstring(totalUnits) + L" units \u2022 " + std::to_wstring(selectedUnits) + L" selected";

                if (totalBrokenUnits > 0)

                {

                    sumUnits += L" (⚠ " + std::to_wstring(totalBrokenUnits) + L" missing)";

                }



                // Render Summary pill container on the right

                SIZE rSz = { 0 };

                SelectObject(hmemDC, g_State.hFontBold);

                GetTextExtentPoint32W(hmemDC, sumUnits.c_str(), (int)sumUnits.length(), &rSz);

                int rPillW = (int)rSz.cx + 20;

                if (rPillW > (rcRightSummary.right - rcRightSummary.left)) rPillW = rcRightSummary.right - rcRightSummary.left;

                RECT rcRPill = { rcRightSummary.right - rPillW, btnY, rcRightSummary.right, btnY + btnH };



                HBRUSH hbrRPill = CreateSolidBrush(RGB(42, 20, 26));

                HPEN hpenRPill = CreatePen(PS_SOLID, 1, RGB(92, 40, 50));

                SelectObject(hmemDC, hbrRPill); SelectObject(hmemDC, hpenRPill);

                RoundRect(hmemDC, rcRPill.left, rcRPill.top, rcRPill.right, rcRPill.bottom, 4, 4);

                DeleteObject(hbrRPill); DeleteObject(hpenRPill);



                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, totalBrokenUnits > 0 ? RGB(255, 150, 150) : RGB(220, 225, 235));

                DrawTextW(hmemDC, sumUnits.c_str(), -1, &rcRPill, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            }


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



                int cardBottom = rcClient.bottom - 60;

                int leftX = 16;

                int rightMargin = 16;

                int gap = 8;

                int totalW = rcClient.right - (leftX + rightMargin);



                // 2-Area Distribution:

                // Left Area (~66%): Grid View Matrix (CustomListControl)

                // Right Area (~34%): Batch Action Radios + Source Nodes

                int rightColW = (totalW - gap) * 34 / 100;

                if (rightColW < 320) rightColW = 320;

                int listW = totalW - gap - rightColW;



                int rightX = leftX + listW + gap;



                RECT rcListPane  = { leftX, 118, leftX + listW, cardBottom };

                RECT rcRightPane = { rightX, 118, rightX + rightColW, cardBottom };



                struct WireAnchor

                {

                    POINT ptLeft;

                    int sourceId;

                };

                std::vector<WireAnchor> wireAnchors;



                // Empty state if brokenConsists is empty

                if (g_State.brokenConsists.empty())

                {

                    RECT rcEmptyArea = { leftX, 118, leftX + listW, cardBottom };


                    HBRUSH hbrEm = CreateSolidBrush(cardBgCol);

                    HPEN hpenEm = CreatePen(PS_SOLID, 1, borderCol);

                    SelectObject(hmemDC, hbrEm); SelectObject(hmemDC, hpenEm);

                    RoundRect(hmemDC, rcEmptyArea.left, rcEmptyArea.top, rcEmptyArea.right, rcEmptyArea.bottom, 6, 6);

                    DeleteObject(hbrEm); DeleteObject(hpenEm);



                    SelectObject(hmemDC, g_State.hFontIconLg);

                    SetTextColor(hmemDC, textSecondary);

                    RECT rcEmIcon = { rcEmptyArea.left, rcEmptyArea.top + 60, rcEmptyArea.right, rcEmptyArea.top + 95 };

                    DrawTextW(hmemDC, L"\xE896", -1, &rcEmIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                    SelectObject(hmemDC, g_State.hFontBold);

                    SetTextColor(hmemDC, textPrimary);

                    RECT rcEmTitle = { rcEmptyArea.left, rcEmptyArea.top + 105, rcEmptyArea.right, rcEmptyArea.top + 130 };

                    DrawTextW(hmemDC, L"No Consist(s) Selected", -1, &rcEmTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                    SelectObject(hmemDC, g_State.hFontSmall);

                    SetTextColor(hmemDC, textSecondary);

                    RECT rcEmSub = { rcEmptyArea.left + 40, rcEmptyArea.top + 135, rcEmptyArea.right - 40, rcEmptyArea.top + 180 };

                    DrawTextW(hmemDC, L"Select consist(s) in Consist Manager before opening, or click below to scan all consists across the library for broken units.", -1, &rcEmSub, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);



                    int btnW = 180;

                    int btnH2 = 32;

                    int btnX2 = (rcEmptyArea.left + rcEmptyArea.right - btnW) / 2;

                    int btnY2 = rcEmptyArea.top + 195;

                    g_State.rcTab2BtnScanLibrary = { btnX2, btnY2, btnX2 + btnW, btnY2 + btnH2 };



                    DrawModernButton(hmemDC, g_State.rcTab2BtnScanLibrary, L"Scan Entire Library", g_State.isHoverTab2BtnScanLibrary, false, false, g_State.hFontBold, g_State.hFontIcon, L"\xE721");

                }

                else

                {

                    g_State.rcTab2BtnScanLibrary = { 0 };

                }



                // =============================================================

                // PANE 3: RIGHT PANE (ACTION MODE RADIOS & SOURCE CARDS)

                // =============================================================

                g_State.sourceCardUIs.clear();



                // Right Pane Card Background

                HBRUSH hbrRightCard = CreateSolidBrush(cardBgCol);

                HPEN hpenRightCard = CreatePen(PS_SOLID, 1, borderCol);

                hOldB = (HBRUSH)SelectObject(hmemDC, hbrRightCard);

                hOldP = (HPEN)SelectObject(hmemDC, hpenRightCard);

                RoundRect(hmemDC, rcRightPane.left, rcRightPane.top, rcRightPane.right, rcRightPane.bottom, 6, 6);

                SelectObject(hmemDC, hOldB); SelectObject(hmemDC, hOldP);

                DeleteObject(hbrRightCard); DeleteObject(hpenRightCard);



                // Right Header: Batch Action Title

                RECT rcActHeader = { rcRightPane.left + 10, rcRightPane.top + 6, rcRightPane.right - 10, rcRightPane.top + 28 };

                SelectObject(hmemDC, g_State.hFontBold);

                SetTextColor(hmemDC, textPrimary);

                DrawTextW(hmemDC, L"Batch Action Mode", -1, &rcActHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Action Mode Segmented Buttons [ Replace Units | Rebuild Consist | Purge Units ]

                int actTotalW = (rcRightPane.right - rcRightPane.left - 20);

                int actSegW = (actTotalW - 8) / 3;

                int actTop = rcRightPane.top + 32;

                int actH = 28;



                g_State.rcTab2ActionReplace = { rcRightPane.left + 10, actTop, rcRightPane.left + 10 + actSegW, actTop + actH };

                g_State.rcTab2ActionRebuild = { g_State.rcTab2ActionReplace.right + 4, actTop, g_State.rcTab2ActionReplace.right + 4 + actSegW, actTop + actH };

                g_State.rcTab2ActionPurge   = { g_State.rcTab2ActionRebuild.right + 4, actTop, rcRightPane.right - 10, actTop + actH };



                // Draw Replace Button

                bool isActReplace = (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::ReplaceUnit);

                COLORREF bgRep = isActReplace ? RGB(24, 46, 72) : (g_State.isHoverTab2ActionReplace ? RGB(38, 38, 44) : RGB(28, 28, 32));

                COLORREF bdrRep = isActReplace ? RGB(56, 140, 220) : (g_State.isHoverTab2ActionReplace ? RGB(80, 80, 88) : borderCol);

                COLORREF txtRep = isActReplace ? RGB(140, 205, 255) : (g_State.isHoverTab2ActionReplace ? RGB(240, 240, 240) : textSecondary);

                HBRUSH hbrRep = CreateSolidBrush(bgRep); HPEN hpenRep = CreatePen(PS_SOLID, 1, bdrRep);

                SelectObject(hmemDC, hbrRep); SelectObject(hmemDC, hpenRep);

                RoundRect(hmemDC, g_State.rcTab2ActionReplace.left, g_State.rcTab2ActionReplace.top, g_State.rcTab2ActionReplace.right, g_State.rcTab2ActionReplace.bottom, 4, 4);

                DeleteObject(hbrRep); DeleteObject(hpenRep);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, txtRep);

                DrawTextW(hmemDC, L"🔧 Replace", -1, &g_State.rcTab2ActionReplace, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Draw Rebuild Button

                bool isActRebuild = (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::RebuildConsist);

                COLORREF bgReb = isActRebuild ? RGB(46, 26, 60) : (g_State.isHoverTab2ActionRebuild ? RGB(38, 38, 44) : RGB(28, 28, 32));

                COLORREF bdrReb = isActRebuild ? RGB(168, 85, 247) : (g_State.isHoverTab2ActionRebuild ? RGB(80, 80, 88) : borderCol);

                COLORREF txtReb = isActRebuild ? RGB(226, 190, 255) : (g_State.isHoverTab2ActionRebuild ? RGB(240, 240, 240) : textSecondary);

                HBRUSH hbrReb = CreateSolidBrush(bgReb); HPEN hpenReb = CreatePen(PS_SOLID, 1, bdrReb);

                SelectObject(hmemDC, hbrReb); SelectObject(hmemDC, hpenReb);

                RoundRect(hmemDC, g_State.rcTab2ActionRebuild.left, g_State.rcTab2ActionRebuild.top, g_State.rcTab2ActionRebuild.right, g_State.rcTab2ActionRebuild.bottom, 4, 4);

                DeleteObject(hbrReb); DeleteObject(hpenReb);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, txtReb);

                DrawTextW(hmemDC, L"🔄 Rebuild", -1, &g_State.rcTab2ActionRebuild, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Draw Purge Button

                bool isActPurge = (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit);

                COLORREF bgPrg = isActPurge ? RGB(56, 22, 28) : (g_State.isHoverTab2ActionPurge ? RGB(38, 38, 44) : RGB(28, 28, 32));

                COLORREF bdrPrg = isActPurge ? RGB(220, 68, 76) : (g_State.isHoverTab2ActionPurge ? RGB(80, 80, 88) : borderCol);

                COLORREF txtPrg = isActPurge ? RGB(255, 140, 148) : (g_State.isHoverTab2ActionPurge ? RGB(240, 240, 240) : textSecondary);

                HBRUSH hbrPrg = CreateSolidBrush(bgPrg); HPEN hpenPrg = CreatePen(PS_SOLID, 1, bdrPrg);

                SelectObject(hmemDC, hbrPrg); SelectObject(hmemDC, hpenPrg);

                RoundRect(hmemDC, g_State.rcTab2ActionPurge.left, g_State.rcTab2ActionPurge.top, g_State.rcTab2ActionPurge.right, g_State.rcTab2ActionPurge.bottom, 4, 4);

                DeleteObject(hbrPrg); DeleteObject(hpenPrg);

                SelectObject(hmemDC, g_State.hFontSmall);

                SetTextColor(hmemDC, txtPrg);

                DrawTextW(hmemDC, L"🗑 Purge", -1, &g_State.rcTab2ActionPurge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                // Divider Line under Action Selector

                HPEN hPenRDiv = CreatePen(PS_SOLID, 1, borderCol);

                SelectObject(hmemDC, hPenRDiv);

                MoveToEx(hmemDC, rcRightPane.left + 1, actTop + actH + 10, NULL);

                LineTo(hmemDC, rcRightPane.right - 1, actTop + actH + 10);

                DeleteObject(hPenRDiv);



                int contentTopY = actTop + actH + 18;



                if (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit)

                {

                    g_State.rcAddSourceBtn = { 0 };



                    // Purge Info Notice Card

                    RECT rcNotice = { rcRightPane.left + 12, contentTopY, rcRightPane.right - 12, contentTopY + 160 };

                    HBRUSH hbrNotBg = CreateSolidBrush(RGB(36, 22, 26));

                    HPEN hpenNotBdr = CreatePen(PS_SOLID, 1, RGB(110, 40, 48));

                    SelectObject(hmemDC, hbrNotBg); SelectObject(hmemDC, hpenNotBdr);

                    RoundRect(hmemDC, rcNotice.left, rcNotice.top, rcNotice.right, rcNotice.bottom, 6, 6);

                    DeleteObject(hbrNotBg); DeleteObject(hpenNotBdr);



                    // Notice Icon

                    SelectObject(hmemDC, g_State.hFontIconLg);

                    SetTextColor(hmemDC, RGB(255, 90, 90));

                    RECT rcNotIcon = { rcNotice.left + 12, rcNotice.top + 12, rcNotice.left + 44, rcNotice.top + 44 };

                    DrawTextW(hmemDC, L"\xE74D", -1, &rcNotIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                    // Notice Title

                    SelectObject(hmemDC, g_State.hFontBold);

                    SetTextColor(hmemDC, RGB(255, 160, 160));

                    RECT rcNotTitle = { rcNotice.left + 48, rcNotice.top + 14, rcNotice.right - 12, rcNotice.top + 38 };

                    DrawTextW(hmemDC, L"Purge Selected Units", -1, &rcNotTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                    // Notice Body

                    SelectObject(hmemDC, g_State.hFontSmall);

                    SetTextColor(hmemDC, RGB(220, 200, 205));

                    RECT rcNotBody = { rcNotice.left + 14, rcNotice.top + 48, rcNotice.right - 14, rcNotice.bottom - 10 };

                    DrawTextW(hmemDC, L"Selected units will be completely removed from their consist (.con) files without requiring replacement stock.\n\nUnit orders, couplings, and formation integrity will be preserved and automatically compacted upon clicking Apply.", -1, &rcNotBody, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

                }

                else

                {

                    // Sources Header: Title & [+ Add Source] Button

                    RECT rcSrcHeader = { rcRightPane.left + 10, contentTopY, rcRightPane.right - 120, contentTopY + 24 };

                    SelectObject(hmemDC, g_State.hFontBold);

                    SetTextColor(hmemDC, textPrimary);

                    DrawTextW(hmemDC, L"Replacement Sources", -1, &rcSrcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                    g_State.rcAddSourceBtn = { rcRightPane.right - 114, contentTopY - 2, rcRightPane.right - 10, contentTopY + 24 };

                    DrawModernButton(hmemDC, g_State.rcAddSourceBtn, L"Add Source", g_State.isHoverAddSourceBtn, false, false, g_State.hFontSmall, g_State.hFontIcon, L"\xE710");



                    int curCardY = contentTopY + 30;

                    for (size_t k = 0; k < g_State.sourceNodes.size(); ++k)

                    {

                        const auto& node = g_State.sourceNodes[k];

                        int cardH = 66;

                        RECT rcCard = { rcRightPane.left + 10, curCardY, rcRightPane.right - 10, curCardY + cardH };



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

                        RECT rcBadge = { rcCard.left + 10, rcCard.top + 7, rcCard.left + 28, rcCard.top + 23 };

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

                        RECT rcNodeTitle = { rcCard.left + 32, rcCard.top + 5, rcCard.left + 95, rcCard.top + 25 };

                        DrawTextW(hmemDC, node.name.c_str(), -1, &rcNodeTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                        // Source Type Dropdown Selector Button

                        cardUI.rcTypeToggle = { rcCard.left + 98, rcCard.top + 4, rcCard.right - (g_State.sourceNodes.size() > 1 ? 32 : 8), rcCard.top + 26 };

                        COLORREF togBg = cardUI.isHoverToggle ? RGB(45, 45, 50) : RGB(30, 30, 34);

                        COLORREF togBorder = cardUI.isHoverToggle ? RGB(90, 90, 95) : RGB(55, 55, 60);

                        HBRUSH hbrTogBg = CreateSolidBrush(togBg);

                        HPEN hpenTogBg = CreatePen(PS_SOLID, 1, togBorder);

                        SelectObject(hmemDC, hbrTogBg); SelectObject(hmemDC, hpenTogBg);

                        RoundRect(hmemDC, cardUI.rcTypeToggle.left, cardUI.rcTypeToggle.top, cardUI.rcTypeToggle.right, cardUI.rcTypeToggle.bottom, 4, 4);

                        DeleteObject(hbrTogBg); DeleteObject(hpenTogBg);



                        const wchar_t* typeLabel = (node.sourceType == 0) ? L"Fav Group" : ((node.sourceType == 1) ? L"Pool Preset" : L"Train Blueprint");

                        SelectObject(hmemDC, g_State.hFontSmall);

                        SetTextColor(hmemDC, textPrimary);

                        RECT rcTogTxt = { cardUI.rcTypeToggle.left + 6, cardUI.rcTypeToggle.top, cardUI.rcTypeToggle.right - 18, cardUI.rcTypeToggle.bottom };

                        DrawTextW(hmemDC, typeLabel, -1, &rcTogTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);



                        DrawSolidTriangle(hmemDC, cardUI.rcTypeToggle.right - 10, (cardUI.rcTypeToggle.top + cardUI.rcTypeToggle.bottom) / 2, textSecondary, true, 3);



                        // Delete Button (if > 1 source)

                        if (g_State.sourceNodes.size() > 1)

                        {

                            cardUI.rcDeleteBtn = { rcCard.right - 26, rcCard.top + 4, rcCard.right - 6, rcCard.top + 26 };

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

                        cardUI.rcPicker = { rcCard.left + 10, rcCard.top + 30, rcCard.right - 8, rcCard.top + 58 };

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



                        DrawSolidTriangle(hmemDC, cardUI.rcPicker.right - 14, (cardUI.rcPicker.top + cardUI.rcPicker.bottom) / 2, textSecondary, true, 3);



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

            const wchar_t* applyText = (g_State.activeTab == 0) ? L"Apply Mutation" :

                ((g_State.activeTab == 1) ? L"Insert Units" :

                (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit ? L"Execute Unit Purge" : L"Execute Repair / Rebuild"));

            const wchar_t* applyIcon = (g_State.activeTab == 2) ?

                (g_State.tab2Action == MutatorDlgState::Tab2ActionMode::PurgeUnit ? L"\xE74D" : L"\xE896") : L"\xE73E";



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

    wcex.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107 /* IDI_TRAINSIMCONSISTBUILDER */));

    wcex.hIconSm = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(107), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    if (!wcex.hIconSm) wcex.hIconSm = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(108 /* IDI_SMALL */));

    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);

    wcex.hbrBackground = NULL;

    wcex.lpszClassName = szClassName;



    RegisterClassExW(&wcex);



    int dlgW = 1080;

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

        WS_EX_APPWINDOW, szClassName, L"Consist Pool Mutator & Injector",

        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,

        x, y, dlgW, dlgH,

        hWndParent, NULL, GetModuleHandle(NULL), NULL

    );



    if (!hDlg) return;



    HICON hIconBig = (HICON)GetClassLongPtrW(hDlg, GCLP_HICON);

    HICON hIconSm = (HICON)GetClassLongPtrW(hDlg, GCLP_HICONSM);

    if (hIconBig) SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);

    if (hIconSm)  SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);



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

