#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// Callback triggered when a stream preset or stream action is applied
typedef std::function<void(const std::wstring& streamName)> SmsStreamPresetSelectCallback;

struct SmsUnitUsageRef
{
    std::wstring consistName; // e.g. "12430_Rajdhani.con"
    std::wstring unitUid;     // e.g. "WAP7_30201"
    int unitIndex = 0;        // 1-based position in consist (#1, #2...)
    bool isEngine = false;
    std::wstring soundSlot;   // "Engine (Ext)", "Cab Sound", "Wagon Sound", "Interior"
};

struct SmsCategorizedTargetInput
{
    std::wstring fullPath;
    SmsUnitUsageRef usage;
};

// Shows the SMS Stream Mutator & Stream Presets Library Dialog
// initialTab: 0 = Stream Presets Library, 1 = Batch Stream Mutator
void ShowSmsStreamMutatorDialog(
    HWND hWndParent,
    const std::wstring& basePath,
    const std::vector<std::wstring>& initialSmsFiles = {},
    int initialTab = 0,
    SmsStreamPresetSelectCallback onApplyCallback = nullptr,
    const std::vector<std::wstring>& initialEngineSmsFiles = {},
    const std::vector<std::wstring>& initialWagonSmsFiles = {},
    const std::vector<SmsCategorizedTargetInput>& initialEngineTargetsWithUsage = {},
    const std::vector<SmsCategorizedTargetInput>& initialWagonTargetsWithUsage = {}
);

bool SmsStreamMutatorDlg_IsOpen();
void SmsStreamMutatorDlg_UpdateSelection(const std::vector<std::wstring>& targetSmsFilePaths);
void SmsStreamMutatorDlg_SetTargets(const std::vector<std::wstring>& targetSmsFilePaths);
void SmsStreamMutatorDlg_SetCategorizedTargets(
    const std::vector<std::wstring>& engineSmsFiles,
    const std::vector<std::wstring>& wagonSmsFiles
);
void SmsStreamMutatorDlg_SetCategorizedTargetsWithUsage(
    const std::vector<SmsCategorizedTargetInput>& engineTargets,
    const std::vector<SmsCategorizedTargetInput>& wagonTargets
);
