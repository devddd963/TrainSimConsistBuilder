#pragma once
#include <windows.h>
#include <string>

// Attaches modern Explorer-style dark theme subclass with hover, focus accent line, and optional numeric validation
void ModernEdit_Attach(
    HWND hEdit,
    bool isNumericOnly = false,
    bool isCentered = true,
    bool allowDecimals = true,
    bool notifyParentOnEnter = false
);

// Call from parent WM_CTLCOLOREDIT to get the cohesive dark background matching hover and focus states
LRESULT ModernEdit_OnCtlColor(HDC hdcEdit, HWND hEdit);

// Attaches modern Explorer-style Fluent dark button subclass with hover, press, and rounded corners
void ModernButton_Attach(HWND hBtn, bool isPrimary = false, HFONT hFont = NULL);

// Displays the sleek modern input prompt modal dialog (dark theme, DWM rounded corners, no white border frames, Esc/Enter, defocus support)
bool ShowModernInputPrompt(
    HWND hWndParent,
    const std::wstring& title,
    const std::wstring& prompt,
    const std::wstring& defaultText,
    std::wstring& outText,
    bool isNumericOnly = false,
    bool allowDecimals = true
);

bool ShowModernInputPrompt(
    HWND hParent,
    const wchar_t* title,
    const wchar_t* promptLabel,
    std::wstring& ioValue,
    bool isNumericOnly = false,
    bool allowDecimals = true,
    const wchar_t* placeholder = L"",
    const wchar_t* secondLabel = nullptr,
    std::wstring* pSecondValue = nullptr,
    bool secondNumericOnly = false,
    const wchar_t* secondPlaceholder = L""
);
