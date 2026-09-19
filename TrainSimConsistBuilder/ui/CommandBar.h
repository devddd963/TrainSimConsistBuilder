#pragma once

#include <windows.h>
#include <commctrl.h>

// Custom notification message sent from CommandBar to Parent Window
#define WM_COMMANDBAR_ACTION (WM_USER + 104)

// Command Bar Action IDs
#define CMD_ACTION_NEW_CONSIST        1
#define CMD_ACTION_CLONE_CONSIST      2
#define CMD_ACTION_SAVE_CONSISTS      3
#define CMD_ACTION_DELETE_CONSISTS    4
#define CMD_ACTION_REVERSE_CONSIST    5
#define CMD_ACTION_REFRESH_CONSISTS   6
#define CMD_ACTION_REFRESH_STOCKS     7
#define CMD_ACTION_SORT               8
#define CMD_ACTION_BATCH_WIZARD       9
#define CMD_ACTION_POOL_MANAGER       10
#define CMD_ACTION_POOL_MUTATOR       13
#define CMD_ACTION_MORE               11
#define CMD_ACTION_DETAILS            12

BOOL RegisterCommandBarClass(HINSTANCE hInstance);
HWND CreateCommandBar(HWND hParent, HINSTANCE hInstance, int x, int y, int width, int height, UINT_PTR controlId);
void CommandBar_SetDarkMode(HWND hCommandBar, BOOL bDarkMode);
void CommandBar_SetButtonText(HWND hCommandBar, int actionId, const wchar_t* newLabel, int newWidth = 0);
