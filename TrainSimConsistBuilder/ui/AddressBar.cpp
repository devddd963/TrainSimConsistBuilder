#include "AddressBar.h"

// Subclass Procedure to capture Enter Key in the Edit Box
static LRESULT CALLBACK AddressBarEditSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            // Send navigation signal to parent frame (WndProc)
            HWND hParent = GetParent(GetParent(hWnd));
            SendMessage(hParent, WM_ADDRESSBAR_NAVIGATE, 0, (LPARAM)hWnd);
            return 0; // Consume the Enter key press
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, AddressBarEditSubclass, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass Procedure for the AddressBar Container Window
static LRESULT CALLBACK AddressBarContainerProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_COMMAND:
        // Handle "Go" button click
        if (LOWORD(wParam) == IDC_ADDRESSBAR_GO && HIWORD(wParam) == BN_CLICKED)
        {
            HWND hEdit = GetDlgItem(hWnd, IDC_ADDRESSBAR_EDIT);
            HWND hParent = GetParent(hWnd);
            SendMessage(hParent, WM_ADDRESSBAR_NAVIGATE, 0, (LPARAM)hEdit);
            return 0;
        }
        break;

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        HWND hEdit = GetDlgItem(hWnd, IDC_ADDRESSBAR_EDIT);
        HWND hGoBtn = GetDlgItem(hWnd, IDC_ADDRESSBAR_GO);

        if (hEdit && hGoBtn)
        {
            SetWindowPos(hEdit, NULL, 2, 2, width - 50, height - 4, SWP_NOZORDER);
            SetWindowPos(hGoBtn, NULL, width - 46, 2, 44, height - 4, SWP_NOZORDER);
        }
        break;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, AddressBarContainerProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

HWND CreateAddressBar(HWND hParent, HINSTANCE hInst, int x, int y, int width, int height, UINT_PTR controlId)
{
    // 1. Create Container Control
    HWND hContainer = CreateWindowEx(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        hParent, (HMENU)controlId, hInst, NULL
    );

    if (!hContainer) return NULL;

    // Subclass Container for internal message handling
    SetWindowSubclass(hContainer, AddressBarContainerProc, 1, 0);

    // 2. Create Address Edit Box
    HWND hEdit = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        2, 2, width - 50, height - 4,
        hContainer, (HMENU)IDC_ADDRESSBAR_EDIT, hInst, NULL
    );

    // Subclass Edit Box to catch VK_RETURN
    SetWindowSubclass(hEdit, AddressBarEditSubclass, 2, 0);

    // 3. Create "Go" Action Button
    HWND hGoBtn = CreateWindowEx(
        0, L"BUTTON", L"Go",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        width - 46, 2, 44, height - 4,
        hContainer, (HMENU)IDC_ADDRESSBAR_GO, hInst, NULL
    );

    // 4. Set Modern System Font on controls
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
    SendMessage(hGoBtn, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));

    return hContainer;
}

void AddressBar_SetPath(HWND hAddressBar, const wchar_t* szPath)
{
    HWND hEdit = GetDlgItem(hAddressBar, IDC_ADDRESSBAR_EDIT);
    if (hEdit) SetWindowTextW(hEdit, szPath);
}

void AddressBar_GetPath(HWND hAddressBar, wchar_t* szBuffer, int maxLen)
{
    HWND hEdit = GetDlgItem(hAddressBar, IDC_ADDRESSBAR_EDIT);
    if (hEdit) GetWindowTextW(hEdit, szBuffer, maxLen);
}