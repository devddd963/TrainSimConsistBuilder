#pragma once

#include <windows.h>
#include <commctrl.h>

// Custom notification message sent to main WndProc when the user hits Enter or clicks "Go"
#define WM_ADDRESSBAR_NAVIGATE (WM_USER + 101)

// Control IDs for internal AddressBar elements
#define IDC_ADDRESSBAR_EDIT 2001
#define IDC_ADDRESSBAR_GO   2002

struct AddressBar
{
    HWND hContainer;
    HWND hEdit;
    HWND hGoBtn;
};

// Public API
HWND CreateAddressBar(HWND hParent, HINSTANCE hInst, int x, int y, int width, int height, UINT_PTR controlId);
void AddressBar_SetPath(HWND hAddressBar, const wchar_t* szPath);
void AddressBar_GetPath(HWND hAddressBar, wchar_t* szBuffer, int maxLen);