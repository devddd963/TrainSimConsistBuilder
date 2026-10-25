#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "../SRC/DatabaseManager.h"

// Callback type triggered when an SMS preset is selected to apply
typedef std::function<void(const DatabaseManager::SmsPoolPresetItem&)> SmsPoolPresetSelectCallback;

// Shows the Unified Sound Studio Tools Dialog
// initialTab: 0 = SMS Presets Pool, 1 = Batch Sound Mutator
void ShowBatchSoundMutatorDialog(
    HWND hWndParent,
    const std::wstring& basePath,
    const std::vector<std::wstring>& initialConsistFiles = {},
    int initialTab = 0,
    SmsPoolPresetSelectCallback onApplyCallback = nullptr
);

bool BatchSoundMutatorDlg_IsOpen();
void BatchSoundMutatorDlg_UpdateSelection(const std::vector<std::wstring>& targetConsistFilePaths);

// Drag & Drop SMS from library into Tab 0 Preset cards
struct ScannedSoundItem;
bool BatchSoundMutatorDlg_HandleDragHover(POINT ptScreen, const struct ScannedSoundItem* pItem, std::wstring& outActionText);
void BatchSoundMutatorDlg_ClearDragHover();
bool BatchSoundMutatorDlg_HandleDrop(POINT ptScreen, const struct ScannedSoundItem& item);
