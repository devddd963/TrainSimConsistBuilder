#pragma once

#include <windows.h>
#include <string>
#include "../SRC/StockSpecReader.h"

// Opens the Stock Specification & Diagnostic Inspection Window
void ShowStockInfoDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath);
