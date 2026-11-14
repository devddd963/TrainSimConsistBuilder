#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "../SRC/ConsistReader.h"

// Custom window class name
#define CLASSIC_CONSIST_VIEW_CLASS L"ClassicConsistView"

// Custom parent notifications from ClassicConsistView
#define WM_CLASSIC_UNIT_SELECTED   (WM_USER + 701)
#define WM_CLASSIC_UNITS_REORDER   (WM_USER + 702)
#define WM_CLASSIC_UNIT_FLIP       (WM_USER + 703)
#define WM_CLASSIC_UNIT_DELETE     (WM_USER + 704)
#define WM_CLASSIC_SAVE_REQUEST    (WM_USER + 705)
#define WM_CLASSIC_SAVE_AS_REQUEST (WM_USER + 706)
#define WM_CLASSIC_CLEAR_REQUEST   (WM_USER + 707)
#define WM_CLASSIC_FLIP_ALL        (WM_USER + 708)
#define WM_CLASSIC_CONTEXT_MENU    (WM_USER + 709)
#define WM_CLASSIC_METADATA_CHANGE (WM_USER + 710)
#define WM_CLASSIC_SPLITTER_RESIZED (WM_USER + 711)

// Registration and creation
BOOL RegisterClassicConsistViewClass(HINSTANCE hInstance);
HWND CreateClassicConsistView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, UINT_PTR id);

// Consist data synchronization
void ClassicConsistView_SetUnits(
    HWND hWnd,
    const std::vector<ConsistReader::UnitInfo>& units,
    const std::wstring& basePath,
    const std::wstring& consistName = L"",
    const std::wstring& fileName = L""
);

void ClassicConsistView_SetSelectedIndices(HWND hWnd, const std::vector<int>& selIndices);
std::vector<int> ClassicConsistView_GetSelectedIndices(HWND hWnd);
std::vector<int> ClassicConsistView_GetCheckedIndices(HWND hWnd);
void ClassicConsistView_ClearSelection(HWND hWnd);

// Theme and visual updates
void ClassicConsistView_SetDarkMode(HWND hWnd, BOOL bDark);
void ClassicConsistView_SetDirty(HWND hWnd, bool isDirty);
void ClassicConsistView_GetPreviewCardRect(HWND hWnd, RECT* outRect);
void ClassicConsistView_GetUnitListRect(HWND hWnd, RECT* outRect);
void ClassicConsistView_SetDesiredListWidth(HWND hWnd, int desiredWidth);
std::wstring ClassicConsistView_GetSearchQuery(HWND hWnd);

// Metadata fields (Consist Name, File Name, Max Velocity, Performance Factor)
void ClassicConsistView_SetConsistMetadata(
    HWND hWnd,
    const std::wstring& fileName,
    const std::wstring& cfgId,
    const std::wstring& trainName,
    const std::wstring& maxVelocity,
    const std::wstring& perfFactor
);

void ClassicConsistView_GetConsistMetadata(
    HWND hWnd,
    std::wstring& outFileName,
    std::wstring& outCfgId,
    std::wstring& outTrainName,
    std::wstring& outMaxVelocity,
    std::wstring& outPerfFactor
);

void ClassicConsistView_SetMaxVelocity(HWND hWnd, const std::wstring& val);
void ClassicConsistView_SetPerfFactor(HWND hWnd, const std::wstring& val);

// Stock Drag & Drop support (from stock picker decks)
int  ClassicConsistView_GetDropIndexFromPoint(HWND hWnd, POINT ptScreen);
void ClassicConsistView_SetDropHighlight(HWND hWnd, int dropIndex);
void ClassicConsistView_ClearDropHighlight(HWND hWnd);
