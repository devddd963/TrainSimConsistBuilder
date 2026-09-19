#pragma once

#include <windows.h>
#include <vector>
#include "../src/ConsistReader.h"

// Displays the Consist Pool Manager Dialog
void ShowPoolManagerDialog(HWND hWndParent);

// Drag & Drop Integration
bool PoolManager_IsActive();
bool PoolManager_HandleDragHover(POINT ptScreen);
bool PoolManager_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units);

// Backward compatibility alias
inline void ShowBatchConsistGenerationWizard(HWND hWndParent) { ShowPoolManagerDialog(hWndParent); }
inline bool BatchWizard_IsActive() { return PoolManager_IsActive(); }
inline bool BatchWizard_HandleDragHover(POINT ptScreen) { return PoolManager_HandleDragHover(ptScreen); }
inline bool BatchWizard_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units) { return PoolManager_HandleDragDrop(ptScreen, units); }
