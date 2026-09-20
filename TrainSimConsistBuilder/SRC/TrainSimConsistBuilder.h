#pragma once

#include "framework.h"
#include "Resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#define DWMSBT_AUTO 0
#define DWMSBT_NONE 1
#define DWMSBT_MAINWINDOW 2
#define DWMSBT_TABBEDWINDOW 4

#include "../UI/NavToolbar.h"
#include "../UI/CommandBar.h"

#define IDC_MAIN_TABCONTROL 1001
#define IDC_NAVTOOLBAR      1002
#define IDC_COMMANDBAR      1003
#define IDC_CONSISTLIST     1004
#define IDC_EDITORPANE      1005
#define IDC_ASSETLIST       1006
#define IDC_STOCKHEADER     1007
#define IDC_CONSISTHEADER   1008
#define IDC_STOCKTREE       1010
#define IDC_ED_TRAINNAME    1011
#define IDC_ED_MAXVELOCITY  1012
#define IDC_ED_PERFFACTOR   1013
#define IDC_ED_UNITLIST     1014
#define IDC_ED_SECTION_CFG  1015
#define IDC_ED_SECTION_UNITS 1016
#define IDC_ED_TRAINCFGID   1017
#define IDC_ROUTETREE       1020
#define IDC_ED_SECTION_METRICS 1025
#define IDC_ED_METRIC_MASS     1026
#define IDC_ED_METRIC_LENGTH   1027
#define IDC_ED_METRIC_POWER    1028
#define IDC_ED_METRIC_RATIO    1029


extern HINSTANCE hInst;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

#include "ConsistReader.h"
#include "PoolMutator.h"

BOOL                IsSystemDarkMode();
void                ApplyTitleBarTheme(HWND hWnd, BOOL bDarkMode);
HFONT               GetAdaptiveSystemFont();
const std::vector<ConsistReader::UnitInfo>& GetAppClipboardUnits();
void SetAppClipboardUnits(const std::vector<ConsistReader::UnitInfo>& units);
std::wstring        GetAppConsistsDirectory();
void                TriggerAppConsistsRescan(HWND hWnd = NULL);
bool                ApplyPoolMutationToSessions(
    HWND hWnd,
    const std::vector<std::wstring>& targetConsistPaths,
    const std::vector<int>& targetUnitIndices,
    const PoolMutator::MutatorOptions& options,
    PoolMutator::MutatorResult& outResult
);