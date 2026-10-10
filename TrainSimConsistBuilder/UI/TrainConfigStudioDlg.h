#pragma once

#include <windows.h>
#include <vector>
#include "../SRC/ConsistReader.h"
#include "../SRC/TrainConfig.h"

// Opens the dedicated Train Config Studio Dialog (0 = Train Blueprints, 1 = Rolling Stock Bindings)
void ShowTrainConfigStudioDialog(HWND hWndParent, int initialTab = 0);

// Drag & Drop Integration for Rolling Stock Bindings Tab
bool TrainConfigStudio_IsActive();
bool TrainConfigStudio_HandleDragHover(POINT ptScreen);
bool TrainConfigStudio_HandleDragDrop(POINT ptScreen, const std::vector<ConsistReader::UnitInfo>& units);
HWND TrainConfigStudio_GetHWND();
