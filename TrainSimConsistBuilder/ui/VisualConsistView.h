#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "../src/ConsistReader.h"

#define WM_VISUAL_UNIT_SELECTED (WM_USER + 401)
#define WM_VISUAL_UNIT_FLIPPED  (WM_USER + 402)
#define WM_VISUAL_DOCK_CHANGED  (WM_USER + 403)

HWND CreateVisualConsistView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id);
void VisualConsistView_SetUnits(HWND hWnd, const std::vector<ConsistReader::UnitInfo>& units, const std::wstring& basePath);
void VisualConsistView_SetSelected(HWND hWnd, int index);
void VisualConsistView_SetDarkMode(HWND hWnd, BOOL bDark);
bool VisualConsistView_IsFloating(HWND hWnd);
bool VisualConsistView_IsCollapsed(HWND hWnd);
int  VisualConsistView_GetDesiredHeight(HWND hWnd);
void VisualConsistView_SetFloating(HWND hWnd, bool bFloating);
