#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "BatchSoundMutatorDlg.h"

// Forwarding helper to open the unified dialog on Tab 0 (SMS Presets Pool)
inline void ShowSmsPoolManagerDialog(
    HWND hWndParent,
    const std::wstring& basePath,
    SmsPoolPresetSelectCallback onApplyCallback = nullptr,
    const std::wstring& initialCategory = L""
)
{
    ShowBatchSoundMutatorDialog(hWndParent, basePath, {}, 0, onApplyCallback);
}

inline bool SmsPoolManagerDlg_IsOpen()
{
    return BatchSoundMutatorDlg_IsOpen();
}
