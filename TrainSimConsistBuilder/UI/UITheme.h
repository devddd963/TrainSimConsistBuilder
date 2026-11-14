#pragma once
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

#pragma comment(lib, "uxtheme.lib")

// Windows 11 Dark Explorer Palette & Standardized Modern Controls
namespace UITheme
{
    constexpr COLORREF DarkBackground       = RGB(29, 29, 29);    // Main Pane Surface
    constexpr COLORREF DarkHeaderBackground = RGB(33, 33, 33);    // Header Bar
    constexpr COLORREF DarkHeaderBorder     = RGB(45, 45, 45);    // Header Bottom Line
    constexpr COLORREF DarkItemHover        = RGB(40, 40, 40);    // Hover Row
    constexpr COLORREF DarkItemSelected     = RGB(50, 50, 50);    // Selected Row
    constexpr COLORREF TextPrimary          = RGB(240, 240, 240); // Bright Text
    constexpr COLORREF TextMuted            = RGB(160, 160, 160); // Column Text

    // Fluent Checkbox Palette
    constexpr COLORREF ChkBackgroundNormal  = RGB(28, 30, 38);
    constexpr COLORREF ChkBackgroundHover   = RGB(48, 52, 66);
    constexpr COLORREF ChkBorderNormal      = RGB(60, 65, 80);
    constexpr COLORREF ChkBorderHover       = RGB(100, 140, 200);
    constexpr COLORREF ChkAccentBlue        = RGB(0, 120, 215);
    constexpr COLORREF ChkAccentBorder      = RGB(80, 160, 255);

    inline void DrawCheckbox(
        HDC hdc,
        const RECT& rcBox,
        bool isChecked,
        bool isHovered = false,
        bool isIndeterminate = false,
        COLORREF accentColor = ChkAccentBlue,
        HFONT hFontGlyph = NULL)
    {
        bool isFilled = isChecked || isIndeterminate;
        COLORREF bg = isFilled ? accentColor : (isHovered ? ChkBackgroundHover : ChkBackgroundNormal);
        COLORREF border = isFilled ? ChkAccentBorder : (isHovered ? ChkBorderHover : ChkBorderNormal);

        HBRUSH hbr = CreateSolidBrush(bg);
        HPEN hpen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ holdBr = SelectObject(hdc, hbr);
        HGDIOBJ holdPen = SelectObject(hdc, hpen);
        RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 3, 3);
        SelectObject(hdc, holdBr);
        SelectObject(hdc, holdPen);
        DeleteObject(hbr);
        DeleteObject(hpen);

        if (isIndeterminate)
        {
            int midY = (rcBox.top + rcBox.bottom) / 2;
            int boxW = rcBox.right - rcBox.left;
            int pad = (boxW >= 18) ? 4 : 3;
            RECT rcMinus = { rcBox.left + pad, midY - 1, rcBox.right - pad, midY + 1 };
            HBRUSH hbrWhite = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rcMinus, hbrWhite);
            DeleteObject(hbrWhite);
        }
        else if (isChecked)
        {
            // Sound Studio standard: Segoe UI Bold with Unicode checkmark L"✓" (\u2713)
            static HFONT s_hFont18 = NULL;
            static HFONT s_hFont16 = NULL;
            static HFONT s_hFont14 = NULL;
            static bool s_fontsInit = false;
            if (!s_fontsInit)
            {
                s_fontsInit = true;
                s_hFont18 = CreateFontW(
                    -13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI"
                );
                s_hFont16 = CreateFontW(
                    -11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI"
                );
                s_hFont14 = CreateFontW(
                    -10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI"
                );
            }

            int boxH = rcBox.bottom - rcBox.top;
            HFONT fontToUse = (boxH >= 18) ? s_hFont18 : ((boxH >= 15) ? s_hFont16 : s_hFont14);

            HGDIOBJ oldF = fontToUse ? SelectObject(hdc, fontToUse) : NULL;
            int oldBk = SetBkMode(hdc, TRANSPARENT);
            COLORREF oldCol = SetTextColor(hdc, RGB(255, 255, 255));

            RECT rcGlyph = rcBox;
            DrawTextW(hdc, L"✓", -1, &rcGlyph, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(hdc, oldCol);
            SetBkMode(hdc, oldBk);
            if (oldF) SelectObject(hdc, oldF);
        }
    }
}
