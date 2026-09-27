#pragma once

#include <windows.h>
#include <string>

// Opens the 3D Visual Studio (.s Shape Viewer & .wag / .eng Rolling Stock Inspector)
void Show3DVisualStudioDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath);
void ShowShapeViewerDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath);
