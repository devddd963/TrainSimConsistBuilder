#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ModernMsgBoxCustomButton {
    int id;
    std::wstring text;
    bool isDefault;
};

// Modern Windows 11 Fluent Message Box
int ShowModernMessageBox(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
int ShowModernMessageBoxEx(HWND hWndParent, LPCWSTR lpText, LPCWSTR lpCaption, const std::vector<ModernMsgBoxCustomButton>& customButtons, UINT uType = MB_ICONINFORMATION);
