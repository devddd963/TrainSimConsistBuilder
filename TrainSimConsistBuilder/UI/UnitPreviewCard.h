#pragma once
#include <windows.h>
#include <string>

#define WM_PREVIEW_UNIT_FLIPPED (WM_USER + 601)

// Creates the embedded Live 3D Rolling Stock Preview Card
HWND CreateUnitPreviewCard(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id);

// Sets the unit to display in 3D (.eng, .wag, or .s file path)
void UnitPreviewCard_SetUnit(
    HWND hWnd,
    const std::wstring& unitPath,
    const std::wstring& basePath,
    bool isFlipped = false,
    const std::wstring& displayName = L"",
    const std::wstring& typeName = L""
);

// Clears the current 3D preview
void UnitPreviewCard_Clear(HWND hWnd);

// Toggles or updates theme
void UnitPreviewCard_SetDarkMode(HWND hWnd, BOOL bDark);

// Returns true if a unit is currently loaded and displaying
bool UnitPreviewCard_HasModel(HWND hWnd);
