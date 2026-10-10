#pragma once
#include <windows.h>

#ifdef TSCBRESOURCES_EXPORTS
#define TSCBRESOURCES_API __declspec(dllexport)
#else
#define TSCBRESOURCES_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load and register all embedded TrueType fonts in memory (AddFontMemResourceEx).
 * This guarantees 100% reliable icon glyph rendering on all Windows versions.
 */
TSCBRESOURCES_API bool TSCB_LoadEmbeddedFonts();

/**
 * Unload registered in-memory font resources.
 */
TSCBRESOURCES_API void TSCB_UnloadEmbeddedFonts();

/**
 * Create Segoe MDL2 / Fluent Icons HFONT from embedded memory resources.
 */
TSCBRESOURCES_API HFONT TSCB_CreateIconFont(float pointSize, int weight, int dpi);

/**
 * Create Segoe UI typography HFONT from embedded memory resources.
 */
TSCBRESOURCES_API HFONT TSCB_CreateTypographyFont(float pointSize, int weight, int dpi);

/**
 * Returns the HMODULE instance of TSCBResources.dll.
 */
TSCBRESOURCES_API HMODULE TSCB_GetResourceModule();

#ifdef __cplusplus
}
#endif
