#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Custom notification message sent when the active tab is switched in the title bar
#define WM_TITLEBAR_TABCHANGED (WM_USER + 105)

struct TitleBarTabItem
{
    std::wstring icon;
    std::wstring text;
};

BOOL RegisterCustomTitleBarClass(HINSTANCE hInstance);
HWND CreateCustomTitleBar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId);
HWND CreateCustomTitleBarEx(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId, const wchar_t* title, const std::vector<TitleBarTabItem>& tabs);

void CustomTitleBar_SetDarkMode(HWND hTitleBar, BOOL bDarkMode);
void CustomTitleBar_SetActiveTab(HWND hTitleBar, int tabIndex);
int  CustomTitleBar_GetActiveTab(HWND hTitleBar);
void CustomTitleBar_UpdateWindowState(HWND hTitleBar);
void CustomTitleBar_SetTitle(HWND hTitleBar, const wchar_t* title);
void CustomTitleBar_SetTabs(HWND hTitleBar, const std::vector<TitleBarTabItem>& tabs);
void CustomTitleBar_SetCloseOnly(HWND hTitleBar, BOOL bCloseOnly);
