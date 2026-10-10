#include "TSCBResources.h"
#include "resource.h"
#include <vector>
#include <mutex>

static HMODULE g_hModule = NULL;
static std::vector<HANDLE> g_hFontMemHandles;
static std::mutex g_fontMutex;
static bool g_fontsLoaded = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        TSCB_UnloadEmbeddedFonts();
        break;
    }
    return TRUE;
}

HMODULE TSCB_GetResourceModule()
{
    return g_hModule;
}

static bool LoadSingleFont(int resId)
{
    if (!g_hModule) return false;

    HRSRC hRes = FindResourceW(g_hModule, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hGlobal = LoadResource(g_hModule, hRes);
    if (!hGlobal) return false;

    DWORD size = SizeofResource(g_hModule, hRes);
    void* pData = LockResource(hGlobal);
    if (!pData || size == 0) return false;

    DWORD numFonts = 0;
    HANDLE hFont = AddFontMemResourceEx(pData, size, NULL, &numFonts);
    if (hFont)
    {
        g_hFontMemHandles.push_back(hFont);
        return true;
    }
    return false;
}

bool TSCB_LoadEmbeddedFonts()
{
    std::lock_guard<std::mutex> lock(g_fontMutex);
    if (g_fontsLoaded) return true;

    bool anyLoaded = false;
    if (LoadSingleFont(IDR_FONT_SEGOE_MDL2))  anyLoaded = true;
    if (LoadSingleFont(IDR_FONT_SEGOE_ICONS)) anyLoaded = true;
    if (LoadSingleFont(IDR_FONT_SEGOE_UI))    anyLoaded = true;
    if (LoadSingleFont(IDR_FONT_SEGOE_SB))    anyLoaded = true;

    g_fontsLoaded = anyLoaded;
    return g_fontsLoaded;
}

void TSCB_UnloadEmbeddedFonts()
{
    std::lock_guard<std::mutex> lock(g_fontMutex);
    for (HANDLE h : g_hFontMemHandles)
    {
        if (h) RemoveFontMemResourceEx(h);
    }
    g_hFontMemHandles.clear();
    g_fontsLoaded = false;
}

HFONT TSCB_CreateIconFont(float pointSize, int weight, int dpi)
{
    if (!g_fontsLoaded)
    {
        TSCB_LoadEmbeddedFonts();
    }

    if (dpi <= 0) dpi = 96;
    int height = -MulDiv((int)(pointSize * 10), dpi, 720);

    LOGFONTW lf = { 0 };
    lf.lfHeight = height;
    lf.lfWeight = weight;
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

HFONT TSCB_CreateTypographyFont(float pointSize, int weight, int dpi)
{
    if (!g_fontsLoaded)
    {
        TSCB_LoadEmbeddedFonts();
    }

    if (dpi <= 0) dpi = 96;
    int height = -MulDiv((int)(pointSize * 10), dpi, 720);

    LOGFONTW lf = { 0 };
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Display");
        hFont = CreateFontIndirectW(&lf);
    }
    return hFont;
}
