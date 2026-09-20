#pragma once

#include <windows.h>
#include <commctrl.h>

// Custom messages sent from NavToolbar to Parent Window
#define WM_NAVTOOLBAR_NAVIGATE (WM_USER + 101)
#define WM_NAVTOOLBAR_SEARCH   (WM_USER + 102)
#define WM_NAVTOOLBAR_ACTION   (WM_USER + 103)

// Action IDs passed in wParam for WM_NAVTOOLBAR_ACTION
#define NAV_ACTION_BACK    1
#define NAV_ACTION_FORWARD 2
#define NAV_ACTION_UP      3
#define NAV_ACTION_REFRESH 4

BOOL RegisterNavToolbarClass(HINSTANCE hInstance);
HWND CreateNavToolbar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId);

void NavToolbar_SetPath(HWND hNavToolbar, const wchar_t* szPath);
void NavToolbar_GetPath(HWND hNavToolbar, wchar_t* szBuffer, int maxLen);
void NavToolbar_GetSearchQuery(HWND hNavToolbar, wchar_t* szBuffer, int maxLen);
void NavToolbar_SetSearchQuery(HWND hNavToolbar, const wchar_t* szQuery);
void NavToolbar_SetDarkMode(HWND hNavToolbar, BOOL bDarkMode);
