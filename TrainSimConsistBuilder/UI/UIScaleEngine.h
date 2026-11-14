#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace UIScale
{
    // Baseline reference resolution where the UI was originally designed
    constexpr float BASELINE_WIDTH  = 1600.0f;
    constexpr float BASELINE_HEIGHT = 900.0f;

    // Minimum and maximum scaling bounds to prevent unreadable text or absurdly huge controls
    constexpr float MIN_SCALE = 0.75f;
    constexpr float MAX_SCALE = 1.75f;

    // Updates the global UI scale based on the window's current dimensions and DPI
    void Update(HWND hWnd, int clientWidth, int clientHeight);

    // Current combined scale factor (Resolution ratio * DPI factor)
    float GetScale();

    // Scale a base pixel dimension (designed for 1600x900 at 96 DPI)
    int Px(int basePx);

    // Scale a float dimension
    float PxF(float basePx);

    // Create a high-quality GDI font scaled with ClearType
    HFONT CreateScaledFont(HWND hWnd, const wchar_t* fontName, int basePointSize, int weight = FW_NORMAL);

    // Create a Segoe MDL2 Assets / Fluent icon font scaled cleanly
    HFONT CreateScaledIconFont(HWND hWnd, float basePointSize);
}
