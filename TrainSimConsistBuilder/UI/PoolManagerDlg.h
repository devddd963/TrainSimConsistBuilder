#pragma once

#include <windows.h>
#include <vector>
#include "../SRC/ConsistReader.h"

namespace PoolTheme
{
    constexpr COLORREF GutterBackground   = RGB(16, 16, 16); // Unified scrollbar gutter & window background
    constexpr COLORREF TitleBackground    = RGB(28, 9, 12);  // Top title bar
    constexpr COLORREF ToolbarBackground  = RGB(52, 22, 27); // Preset toolbar (matches active tab)
    constexpr COLORREF FooterBackground   = RGB(22, 22, 22); // Bottom footer bar
    constexpr COLORREF CardBackground     = RGB(26, 26, 26); // Pool card surface
    constexpr COLORREF CardHeaderBg       = RGB(32, 32, 32); // Pool card header
    constexpr COLORREF CardBorder         = RGB(46, 46, 46); // Card border
    constexpr COLORREF UnitsBoxBackground = RGB(20, 20, 20); // Inner units container
    constexpr COLORREF UnitsBoxBorder     = RGB(38, 38, 38); // Units container border
    constexpr COLORREF ChipBackground     = RGB(32, 32, 32); // Stock unit chip
    constexpr COLORREF ChipBorder         = RGB(50, 50, 50); // Stock unit chip border
    constexpr COLORREF BorderLine         = RGB(46, 46, 46); // Divider lines & frame borders
    constexpr COLORREF TextPrimary        = RGB(240, 240, 240);
    constexpr COLORREF TextSecondary      = RGB(160, 160, 160);
    constexpr COLORREF TextMuted          = RGB(120, 120, 120);
    constexpr COLORREF AccentBlue         = RGB(0, 120, 215);
    constexpr COLORREF AccentHover        = RGB(0, 140, 240);
    constexpr COLORREF AccentPressed      = RGB(0, 100, 180);
}

// Displays the Consist Pool Manager Dialog (initialTab: 0 = Consist Assembly Pools, 1 = Unit Replacement Groups)
void ShowPoolManagerDialog(HWND hWndParent, int initialTab = 0);

// Drag & Drop Integration
bool PoolManager_IsActive();
bool PoolManager_HandleDragHover(POINT ptScreen);
bool PoolManager_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units);

// Backward compatibility alias
inline void ShowBatchConsistGenerationWizard(HWND hWndParent) { ShowPoolManagerDialog(hWndParent); }
inline bool BatchWizard_IsActive() { return PoolManager_IsActive(); }
inline bool BatchWizard_HandleDragHover(POINT ptScreen) { return PoolManager_HandleDragHover(ptScreen); }
inline bool BatchWizard_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units) { return PoolManager_HandleDragDrop(ptScreen, units); }
