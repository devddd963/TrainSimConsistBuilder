#pragma once
#include <windows.h>

// mode: 0 = Speed Limit only, 1 = Performance Factor only, 2 = Both Speed & Performance
bool ShowSetConsistParametersDialog(
    HWND hWndParent,
    int mode,
    double initialSpeed,
    double initialPerf,
    double& outSpeed,
    double& outPerf
);
