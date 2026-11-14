#include "UIScaleEngine.h"

namespace UIScale
{
    static float s_Scale = 1.0f;
    static UINT s_CurrentDpi = 96;

    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);

    static UINT QueryWindowDpi(HWND hWnd)
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32)
        {
            auto pfn = (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow");
            if (pfn && hWnd && IsWindow(hWnd))
            {
                UINT dpi = pfn(hWnd);
                if (dpi > 0) return dpi;
            }
        }
        return GetDpiForSystem();
    }

    void Update(HWND hWnd, int clientWidth, int clientHeight)
    {
        if (clientWidth <= 0)
        {
            if (hWnd && IsWindow(hWnd))
            {
                RECT rc;
                GetClientRect(hWnd, &rc);
                clientWidth = rc.right - rc.left;
                clientHeight = rc.bottom - rc.top;
            }
        }

        s_CurrentDpi = QueryWindowDpi(hWnd);
        if (s_CurrentDpi == 0) s_CurrentDpi = 96;

        // Effective width normalized to 96 DPI
        float effectiveWidth = (float)clientWidth;

        // If clientWidth is provided in physical pixels at high DPI (e.g. 125%, 150%),
        // normalize to logical points first:
        float dpiRatio = (float)s_CurrentDpi / 96.0f;
        float logicalWidth = (dpiRatio > 0.01f) ? (effectiveWidth / dpiRatio) : effectiveWidth;

        // Compute ratio against baseline 1600px width
        float ratio = 1.0f;
        if (logicalWidth > 0.0f)
        {
            ratio = logicalWidth / BASELINE_WIDTH;
        }

        // Clamp ratio to safe readable bounds
        s_Scale = (std::max)(MIN_SCALE, (std::min)(MAX_SCALE, ratio));
    }

    float GetScale()
    {
        return s_Scale;
    }

    int Px(int basePx)
    {
        return (int)std::round((float)basePx * s_Scale);
    }

    float PxF(float basePx)
    {
        return basePx * s_Scale;
    }

    HFONT CreateScaledFont(HWND hWnd, const wchar_t* fontName, int basePointSize, int weight)
    {
        UINT dpi = s_CurrentDpi;
        if (dpi == 0) dpi = QueryWindowDpi(hWnd);

        // Apply scale factor to point size
        float scaledPointSize = (float)basePointSize * s_Scale;
        int nHeight = -MulDiv((int)std::round(scaledPointSize * 10.0f), (int)dpi, 720);

        LOGFONTW lf = { 0 };
        lf.lfHeight = nHeight;
        lf.lfWeight = weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        if (fontName && wcslen(fontName) > 0)
        {
            wcscpy_s(lf.lfFaceName, fontName);
        }
        else
        {
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
        }

        HFONT hFont = CreateFontIndirectW(&lf);
        if (!hFont)
        {
            NONCLIENTMETRICS ncm = { 0 };
            ncm.cbSize = sizeof(NONCLIENTMETRICS);
            if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
            {
                ncm.lfMessageFont.lfHeight = lf.lfHeight;
                ncm.lfMessageFont.lfWeight = weight;
                ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
                hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            }
        }
        return hFont;
    }

    HFONT CreateScaledIconFont(HWND hWnd, float basePointSize)
    {
        UINT dpi = s_CurrentDpi;
        if (dpi == 0) dpi = QueryWindowDpi(hWnd);

        float scaledPointSize = basePointSize * s_Scale;
        int nHeight = -MulDiv((int)std::round(scaledPointSize * 10.0f), (int)dpi, 720);

        LOGFONTW lf = { 0 };
        lf.lfHeight = nHeight;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");

        HFONT hFont = CreateFontIndirectW(&lf);
        if (!hFont)
        {
            wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
            hFont = CreateFontIndirectW(&lf);
        }
        if (!hFont)
        {
            wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
            hFont = CreateFontIndirectW(&lf);
        }
        return hFont;
    }
}
