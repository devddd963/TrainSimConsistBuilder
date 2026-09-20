#pragma once

#include <windows.h>
#include <string>

// Custom notification message sent when the active tab is switched in the title bar
#define WM_TITLEBAR_TABCHANGED (WM_USER + 105)

BOOL RegisterCustomTitleBarClass(HINSTANCE hInstance);
HWND CreateCustomTitleBar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId);
void CustomTitleBar_SetDarkMode(HWND hTitleBar, BOOL bDarkMode);
void CustomTitleBar_SetActiveTab(HWND hTitleBar, int tabIndex);
int  CustomTitleBar_GetActiveTab(HWND hTitleBar);
void CustomTitleBar_UpdateWindowState(HWND hTitleBar);
