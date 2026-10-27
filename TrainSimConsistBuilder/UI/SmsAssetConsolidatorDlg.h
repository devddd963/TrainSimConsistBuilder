#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "../SRC/SmsParser.h"

struct SmsConsolidateResult
{
    bool isApplied = false;
    bool docModified = false;
    int filesCopied = 0;
    int filesSkipped = 0;
    int filesMissing = 0;
    std::wstring destFolder;
    std::wstring summaryMessage;
};

// Shows the Sound Pack Bundler & WAV Asset Consolidator Dialog
// Returns true if operation was executed and applied
bool ShowSmsAssetConsolidatorDialog(
    HWND hWndParent,
    SmsDocument& doc,
    const std::wstring& basePath,
    SmsConsolidateResult& outResult
);
