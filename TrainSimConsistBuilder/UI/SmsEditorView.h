#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "../SRC/SmsParser.h"
#include "SoundStudio.h"

// Forward declaration of internal state
struct SmsEditorViewState;

HWND CreateSmsEditorView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id);
void SmsEditorView_SetConsistContext(HWND hWnd, const std::wstring& consistName, const std::wstring& basePath, const std::vector<RSSUnitSoundInfo>& units);
void SmsEditorView_SetDirectStockContext(HWND hWnd, const StockItem& item, const std::wstring& basePath, const RSSUnitSoundInfo& unitInfo);
void SmsEditorView_OpenSmsFile(HWND hWnd, const std::wstring& filePath, const std::wstring& basePath);
void SmsEditorView_SetDarkMode(HWND hWnd, BOOL bDark);
void SmsEditorView_Resize(HWND hWnd, int x, int y, int w, int h);
bool SmsEditorView_HasUnsavedChanges(HWND hWnd);
bool SmsEditorView_SaveCurrentFile(HWND hWnd);
std::wstring SmsEditorView_GetCurrentFilePath(HWND hWnd);
