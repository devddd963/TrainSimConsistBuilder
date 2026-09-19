#pragma once
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

#pragma comment(lib, "uxtheme.lib")

// Windows 11 Dark Explorer Palette
namespace UITheme
{
    constexpr COLORREF DarkBackground      = RGB(29, 29, 29);    // Main Pane Surface
    constexpr COLORREF DarkHeaderBackground= RGB(33, 33, 33);    // Header Bar
    constexpr COLORREF DarkHeaderBorder    = RGB(45, 45, 45);    // Header Bottom Line
    constexpr COLORREF DarkItemHover       = RGB(40, 40, 40);    // Hover Row
    constexpr COLORREF DarkItemSelected    = RGB(50, 50, 50);    // Selected Row
    constexpr COLORREF TextPrimary         = RGB(240, 240, 240); // Bright Text
    constexpr COLORREF TextMuted           = RGB(160, 160, 160); // Column Text
}
