#include "3D-VisualStudio.h"
#include "CustomTitleBar.h"
#include "NavToolbar.h"
#include "CustomTreeView.h"
#include "UITheme.h"
// #include "../SRC/ShapeReader.h"      // [DISABLED] Removed — to be rebuilt from scratch
// #include "../SRC/RenderShapeFile.h"  // [DISABLED] Removed — to be rebuilt from scratch
#include "../SRC/AppLogging.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <cmath>
#include <mmsystem.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define IDC_VS_TITLEBAR   4001
#define IDC_VS_NAVTOOLBAR 4002
#define IDC_VS_TREEVIEW   4003

#define WM_VS_DEFERRED_LOAD (WM_USER + 201)
#define WM_VS_SHAPE_LOADED  (WM_USER + 202)

namespace VisualStudioTheme
{
    constexpr COLORREF Background         = RGB(18, 18, 18);
    constexpr COLORREF RibbonBg           = RGB(38, 14, 18);
    constexpr COLORREF SidebarBg          = RGB(22, 22, 22);
    constexpr COLORREF FooterBg           = RGB(24, 24, 24);
    constexpr COLORREF CardBorder         = RGB(58, 24, 30);
    constexpr COLORREF BorderLine         = RGB(58, 24, 30);
    constexpr COLORREF ButtonBg           = RGB(48, 18, 24);
    constexpr COLORREF ButtonHover        = RGB(68, 26, 34);
    constexpr COLORREF ButtonActive       = RGB(0, 120, 215);
    constexpr COLORREF ButtonActiveHover  = RGB(20, 140, 235);
    constexpr COLORREF TextPrimary        = RGB(245, 245, 245);
    constexpr COLORREF TextSecondary      = RGB(180, 175, 175);
    constexpr COLORREF TextMuted          = RGB(130, 130, 130);
    constexpr COLORREF TagText            = RGB(100, 185, 255);
}

struct RibbonButton
{
    int id;
    const wchar_t* iconGlyph;
    const wchar_t* label;
    bool isToggle;
    bool isActive;
    RECT rc;
    bool isHovered;
    bool isPressed;
};

struct VisualStudioState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hTitleBar = NULL;
    HWND hNavToolbar = NULL;
    CustomTreeView assetTree;
    HWND hViewportWnd = NULL;

    int activeTab = 0; // 0 = 3D Shape Viewer (.s), 1 = Rolling Stock Inspector (.wag/.eng)

    std::wstring shapeFilePath;
    std::wstring stockFilePath;
    std::wstring shapeDirectory;
    std::wstring stockDirectory;
    std::wstring currentDirectory;
    std::wstring basePath;

    // 3D Model & Camera State
    std::shared_ptr<ShapeReader::ShapeModel> pCurrentShape;
    RenderShapeFile renderer;
    std::atomic<uint32_t> currentRequestId{ 0 };
    bool isLoading = false;

    float camYaw = -135.0f;     // Degrees (Default 3/4 view facing -X Port wall where text reads left-to-right)
    float camPitch = 22.0f;     // Degrees
    float camDistance = 25.0f;  // Meters
    ShapeReader::Vector3 camTarget = { 0.0f, 1.5f, 0.0f };
    bool wireframeMode = false;
    bool lightingEnabled = false;
    bool gridEnabled = false;

    // Mouse Interaction
    bool isOrbiting = false;
    bool isPanning = false;
    POINT lastMousePos = { 0, 0 };

    // Fonts
    HFONT hFontTitle = NULL;
    HFONT hFontHeader = NULL;
    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontMono = NULL;
    HFONT hFontIcon = NULL;
    HFONT hFontIconSmall = NULL;

    // Layout
    RECT rcRibbon = { 0 };
    RECT rcSidebar = { 0 };
    RECT rcViewport = { 0 };
    RECT rcFooter = { 0 };

    // Ribbon Toolbar Buttons
    std::vector<RibbonButton> ribbonButtons;
};

static std::wstring LoadSavedStudioDirectory(int tabIndex)
{
    wchar_t szPath[MAX_PATH] = { 0 };
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder\\Settings", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwType = REG_SZ;
        DWORD dwBytes = sizeof(szPath);
        const wchar_t* valName = (tabIndex == 0) ? L"ShapeViewerLastDirectory" : L"StockViewerLastDirectory";
        RegQueryValueExW(hKey, valName, NULL, &dwType, (LPBYTE)szPath, &dwBytes);
        RegCloseKey(hKey);
    }
    return szPath;
}

static void SaveStudioDirectory(int tabIndex, const std::wstring& path)
{
    if (path.empty()) return;
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder\\Settings", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        const wchar_t* valName = (tabIndex == 0) ? L"ShapeViewerLastDirectory" : L"StockViewerLastDirectory";
        RegSetValueExW(hKey, valName, 0, REG_SZ, (const BYTE*)path.c_str(), (DWORD)((path.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

static HFONT CreateDpiFont(int pointSize, int weight, const wchar_t* faceName)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pointSize * 10, GetDpiForSystem(), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

static void InitRibbonButtons(VisualStudioState* pState)
{
    pState->ribbonButtons = {
        { 1, L"\xE72C", L"Reset View", false, false },
        { 2, L"\xE809", L"Isometric", false, false },
        { 3, L"\xE8A9", L"Front", false, false },
        { 4, L"\xE8A9", L"Side", false, false },
        { 5, L"\xE8A9", L"Top", false, false },
        { 6, L"\xE81E", L"Wireframe", true, false },
        { 8, L"\xE80A", L"Ground Grid", true, false },
        { 9, L"\xE768", L"Play Anim", true, false },
        { 10, L"\xE895", L"Restart Anim", false, false }
    };
}

static void RecalculateViewerLayout(VisualStudioState* pState, int width, int height)
{
    if (!pState) return;

    // Y = 0..66: CustomTitleBar
    if (pState->hTitleBar && IsWindow(pState->hTitleBar))
    {
        SetWindowPos(pState->hTitleBar, NULL, 0, 0, width, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        InvalidateRect(pState->hTitleBar, NULL, TRUE);
    }

    // Y = 66..110: NavToolbar
    if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
    {
        SetWindowPos(pState->hNavToolbar, NULL, 0, 66, width, 44, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(pState->hNavToolbar, NULL, TRUE);
    }

    // Y = 110..148: Ribbon Control Bar
    pState->rcRibbon = { 0, 110, width, 148 };

    int btnX = 12;
    for (auto& btn : pState->ribbonButtons)
    {
        int btnW = (btn.id >= 9) ? 104 : 92;
        btn.rc = { btnX, 114, btnX + btnW, 144 };
        btnX += btnW + 4;
    }

    // Y = 148..(height - 26): Main Content Area
    int sidebarW = 240;
    int contentTop = 148;
    int footerH = 26;
    int contentH = (std::max)(100, height - contentTop - footerH);

    pState->rcSidebar = { 0, contentTop, sidebarW, contentTop + contentH };
    pState->rcViewport = { sidebarW, contentTop, width, contentTop + contentH };
    pState->rcFooter = { 0, height - footerH, width, height };

    if (pState->assetTree.GetHWND())
    {
        SetWindowPos(pState->assetTree.GetHWND(), NULL, 0, contentTop, sidebarW, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (pState->hViewportWnd)
    {
        SetWindowPos(pState->hViewportWnd, NULL, sidebarW, contentTop, width - sidebarW, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void PopulateAssetTree(VisualStudioState* pState)
{
    if (!pState || !pState->assetTree.GetHWND()) return;

    pState->assetTree.Clear();

    std::wstring searchDir = (pState->activeTab == 0) ? pState->shapeDirectory : pState->stockDirectory;
    if (searchDir.empty() || !std::filesystem::exists(searchDir))
    {
        if (!pState->basePath.empty() && std::filesystem::exists(pState->basePath))
        {
            searchDir = pState->basePath + L"\\TRAINS\\TRAINSET";
            if (!std::filesystem::exists(searchDir)) searchDir = pState->basePath;
        }
        else
        {
            searchDir = L"C:\\";
        }
    }

    std::wstring rootName = std::filesystem::path(searchDir).filename().wstring();
    if (rootName.empty()) rootName = searchDir;

    auto* rootNode = pState->assetTree.AddRoot(rootName, searchDir);
    if (!rootNode) return;

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(searchDir, std::filesystem::directory_options::skip_permission_denied))
        {
            if (entry.is_directory())
            {
                std::wstring dirName = entry.path().filename().wstring();
                pState->assetTree.AddChild(rootNode, dirName, entry.path().wstring(), true);
            }
            else if (entry.is_regular_file())
            {
                std::wstring ext = entry.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (pState->activeTab == 0 && ext == L".s")
                {
                    std::wstring fName = entry.path().filename().wstring();
                    pState->assetTree.AddChild(rootNode, fName, entry.path().wstring(), false);
                }
                else if (pState->activeTab == 1 && (ext == L".wag" || ext == L".eng"))
                {
                    std::wstring fName = entry.path().filename().wstring();
                    pState->assetTree.AddChild(rootNode, fName, entry.path().wstring(), false);
                }
            }
        }
    }
    catch (...)
    {
    }

    pState->assetTree.ExpandNode(rootNode, true);
}

static void UpdateToolbarForTab(VisualStudioState* pState)
{
    if (!pState || !pState->hNavToolbar) return;

    if (pState->activeTab == 0)
    {
        NavToolbar_SetAddressPlaceholder(pState->hNavToolbar, L"Enter 3D Shape File (.s) or Trainset Folder Path...");
        NavToolbar_SetSearchPlaceholder(pState->hNavToolbar, L"Search 3D Shape Files (*.s)...");
        if (!pState->shapeDirectory.empty())
        {
            NavToolbar_SetPath(pState->hNavToolbar, pState->shapeDirectory.c_str());
        }
    }
    else
    {
        NavToolbar_SetAddressPlaceholder(pState->hNavToolbar, L"Enter Rolling Stock (.wag / .eng) Folder Path...");
        NavToolbar_SetSearchPlaceholder(pState->hNavToolbar, L"Search Rolling Stock (*.wag, *.eng)...");
        if (!pState->stockDirectory.empty())
        {
            NavToolbar_SetPath(pState->hNavToolbar, pState->stockDirectory.c_str());
        }
    }
}

// =============================================================
// Viewport Window Procedure (DirectX 11 GPU-Accelerated)
// =============================================================
static LRESULT CALLBACK ViewportProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualStudioState* pState = (VisualStudioState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pState = (VisualStudioState*)cs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pState);

        timeBeginPeriod(1);
        SetTimer(hWnd, 1001, 1, NULL);

        if (pState)
        {
            pState->renderer.Initialize(hWnd);
            if (pState->pCurrentShape && pState->pCurrentShape->isLoaded)
            {
                std::wstring modelDir = std::filesystem::path(pState->shapeFilePath).parent_path().wstring();
                pState->renderer.UploadModel(*pState->pCurrentShape, modelDir);
            }
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 1001 && pState)
        {
            if (pState->renderer.IsInitialized() && IsWindowVisible(hWnd))
            {
                pState->renderer.SetCamera(pState->camYaw, pState->camPitch, pState->camDistance, pState->camTarget);
                pState->renderer.SetRenderOptions(pState->wireframeMode, pState->lightingEnabled, pState->gridEnabled);
                pState->renderer.Render();
            }
        }
        return 0;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 1001);
        timeEndPeriod(1);
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            pState->renderer.Resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (pState)
        {
            pState->isOrbiting = true;
            pState->lastMousePos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        if (pState)
        {
            pState->isPanning = true;
            pState->lastMousePos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) return 0;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int dx = x - pState->lastMousePos.x;
        int dy = y - pState->lastMousePos.y;
        pState->lastMousePos = { x, y };

        if (pState->isOrbiting)
        {
            pState->camYaw += (float)dx * 0.45f;
            pState->camPitch += (float)dy * 0.45f;
            if (pState->camPitch > 89.0f) pState->camPitch = 89.0f;
            if (pState->camPitch < -89.0f) pState->camPitch = -89.0f;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (pState->isPanning)
        {
            float panSpeed = pState->camDistance * 0.0015f;
            float radYaw = pState->camYaw * 3.14159265f / 180.0f;
            float cosY = cosf(radYaw);
            float sinY = sinf(radYaw);

            pState->camTarget.x -= (-cosY * (float)dx) * panSpeed;
            pState->camTarget.z -= (sinY * (float)dx) * panSpeed;
            pState->camTarget.y += (float)dy * panSpeed;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        if (pState && (pState->isOrbiting || pState->isPanning))
        {
            pState->isOrbiting = false;
            pState->isPanning = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float factor = (delta > 0) ? 0.88f : 1.14f;
            pState->camDistance *= factor;
            if (pState->camDistance < 0.5f) pState->camDistance = 0.5f;
            if (pState->camDistance > 5000.0f) pState->camDistance = 5000.0f;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (pState)
        {
            if (!pState->renderer.IsInitialized())
            {
                pState->renderer.Initialize(hWnd);
                if (pState->pCurrentShape && pState->pCurrentShape->isLoaded)
                {
                    std::wstring modelDir = std::filesystem::path(pState->shapeFilePath).parent_path().wstring();
                    pState->renderer.UploadModel(*pState->pCurrentShape, modelDir);
                }
            }

            if (pState->renderer.IsInitialized())
            {
                pState->renderer.SetCamera(pState->camYaw, pState->camPitch, pState->camDistance, pState->camTarget);
                pState->renderer.SetRenderOptions(pState->wireframeMode, pState->lightingEnabled, pState->gridEnabled);
                pState->renderer.Render();
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

static void RestoreParentWindowFocus(HWND hParent)
{
    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        if (IsIconic(hParent))
        {
            ShowWindow(hParent, SW_RESTORE);
        }
        SetWindowPos(hParent, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hParent);
        SetActiveWindow(hParent);
        BringWindowToTop(hParent);
        SetFocus(hParent);
    }
}

static std::mutex s_shapeCacheMutex;
static std::unordered_map<std::wstring, std::shared_ptr<ShapeReader::ShapeModel>> s_shapeCache;

static void TriggerAsyncShapeLoad(VisualStudioState* pState, const std::wstring& filePath)
{
    if (!pState || filePath.empty() || !pState->hWnd) return;

    pState->shapeFilePath = filePath;
    pState->isLoading = true;
    uint32_t reqId = ++pState->currentRequestId;

    HWND hWnd = pState->hWnd;
    InvalidateRect(hWnd, &pState->rcFooter, FALSE);

    std::wstring key = filePath;
    std::transform(key.begin(), key.end(), key.begin(), ::towlower);

    // Fast 0ms cache check
    {
        std::lock_guard<std::mutex> lock(s_shapeCacheMutex);
        auto it = s_shapeCache.find(key);
        if (it != s_shapeCache.end() && it->second)
        {
            LOG_INFO("3D-VisualStudio: Instant 0ms cache hit for shape '%ls' (Request #%u)", filePath.c_str(), reqId);
            PostMessage(hWnd, WM_VS_SHAPE_LOADED, (WPARAM)reqId, (LPARAM)it->second.get());
            return;
        }
    }

    std::thread([hWnd, filePath, key, reqId]() {
        LOG_INFO("3D-VisualStudio [BG Thread]: Loading shape '%ls' (Request #%u)", filePath.c_str(), reqId);
        auto pModel = std::make_shared<ShapeReader::ShapeModel>();
        std::wstring err;
        if (ShapeReader::LoadUncompressedShape(filePath, *pModel, err))
        {
            std::wstring modelDir = std::filesystem::path(filePath).parent_path().wstring();
            ApplyTextures::PreDecodeTexturesForModel(modelDir, pModel->images);

            LOG_INFO("3D-VisualStudio [BG Thread]: Successfully loaded '%ls' (Request #%u) - %u vertices, %u triangles, %u submeshes in %.2f ms. Bounds: Center=(%.2f, %.2f, %.2f), Size=(%.2f, %.2f, %.2f), Radius=%.2f",
                pModel->fileName.c_str(), reqId, pModel->totalVertices, pModel->totalTriangles, (uint32_t)pModel->subMeshes.size(), pModel->loadTimeMs,
                pModel->bounds.center.x, pModel->bounds.center.y, pModel->bounds.center.z,
                pModel->bounds.size.x, pModel->bounds.size.y, pModel->bounds.size.z, pModel->bounds.radius);

            ShapeReader::ShapeModel* rawPtr = nullptr;
            {
                std::lock_guard<std::mutex> lock(s_shapeCacheMutex);
                if (s_shapeCache.size() > 64)
                {
                    s_shapeCache.clear();
                }
                s_shapeCache[key] = pModel;
                rawPtr = pModel.get();
            }

            PostMessage(hWnd, WM_VS_SHAPE_LOADED, (WPARAM)reqId, (LPARAM)rawPtr);
        }
        else
        {
            LOG_WARN("3D-VisualStudio [BG Thread]: Failed to load shape '%ls' (Request #%u): %ls",
                filePath.c_str(), reqId, err.c_str());
            PostMessage(hWnd, WM_VS_SHAPE_LOADED, (WPARAM)reqId, (LPARAM)nullptr);
        }
    }).detach();
}

// =============================================================
// Visual 3D Studio Main Frame Window Procedure
// =============================================================
static LRESULT CALLBACK Visual3DStudioProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualStudioState* pState = (VisualStudioState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pState = (VisualStudioState*)cs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pState);

        // Windows 11 DWM rounded corners
        DWORD corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // Create DPI-aware fonts
        pState->hFontTitle = CreateDpiFont(12, FW_BOLD, L"Segoe UI");
        pState->hFontHeader = CreateDpiFont(11, FW_SEMIBOLD, L"Segoe UI");
        pState->hFontMain = CreateDpiFont(9, FW_NORMAL, L"Segoe UI");
        pState->hFontBold = CreateDpiFont(9, FW_BOLD, L"Segoe UI");
        pState->hFontMono = CreateDpiFont(9, FW_NORMAL, L"Consolas");
        pState->hFontIcon = CreateDpiFont(11, FW_NORMAL, L"Segoe Fluent Icons");
        pState->hFontIconSmall = CreateDpiFont(9, FW_NORMAL, L"Segoe Fluent Icons");

        InitRibbonButtons(pState);

        // Tier 1: Create CustomTitleBar with the 2 Studio tabs (Y = 0..66)
        RECT rcClient = { 0 };
        GetClientRect(hWnd, &rcClient);
        int initW = (rcClient.right > 0) ? rcClient.right : cs->cx;
        if (initW <= 0) initW = 1280;
        int initH = (rcClient.bottom > 0) ? rcClient.bottom : cs->cy;
        if (initH <= 0) initH = 820;

        std::vector<TitleBarTabItem> studioTabs = {
            { L"\xE8E5", L"3D SHAPE VIEWER (.S)" },
            { L"\xE7C0", L"ROLLING STOCK INSPECTOR (.WAG / .ENG)" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            cs->hInstance,
            0, 0, initW, 66,
            IDC_VS_TITLEBAR,
            L"3D Visual Studio - TrainSim Consist Builder",
            studioTabs
        );

        if (pState->hTitleBar)
        {
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
        }

        // Tier 2: Create NavToolbar seamlessly below CustomTitleBar (Y = 66..110)
        pState->hNavToolbar = CreateNavToolbar(hWnd, cs->hInstance, 0, 66, initW, 44, IDC_VS_NAVTOOLBAR);
        if (pState->hNavToolbar)
        {
            NavToolbar_SetDarkMode(pState->hNavToolbar, TRUE);
            UpdateToolbarForTab(pState);
        }

        // Tier 4 Left: Create Child CustomTreeView sidebar (Y = 148)
        pState->assetTree.Create(hWnd, 0, 148, 240, initH - 148 - 26, IDC_VS_TREEVIEW);
        pState->assetTree.SetDarkMode(true);
        pState->assetTree.SetFont(pState->hFontMain);
        pState->assetTree.SetSelectionCallback([pState](CustomTreeNode* node) {
            if (!node || node->isFolder || node->tag.empty()) return;

            if (pState->activeTab == 0)
            {
                TriggerAsyncShapeLoad(pState, node->tag);
            }
            else
            {
                pState->stockFilePath = node->tag;
                InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
            }
        });

        // Tier 4 Right: Create Child Viewport Window
        WNDCLASSEXW wcv = { 0 };
        wcv.cbSize = sizeof(WNDCLASSEXW);
        wcv.style = CS_HREDRAW | CS_VREDRAW;
        wcv.lpfnWndProc = ViewportProc;
        wcv.hInstance = cs->hInstance;
        wcv.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcv.hbrBackground = NULL;
        wcv.lpszClassName = L"TSCB_3DStudioViewport";
        RegisterClassExW(&wcv);

        pState->hViewportWnd = CreateWindowExW(
            0,
            L"TSCB_3DStudioViewport",
            NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            240, 148, initW - 240, initH - 148 - 26,
            hWnd,
            NULL,
            cs->hInstance,
            pState
        );

        RecalculateViewerLayout(pState, initW, initH);
        PostMessage(hWnd, WM_VS_DEFERRED_LOAD, 0, 0);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
        pMMI->ptMinTrackSize.x = 900;
        pMMI->ptMinTrackSize.y = 550;
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (hMonitor)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(hMonitor, &mi))
            {
                pMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                pMMI->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                pMMI->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                pMMI->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
        }
        return 0;
    }

    case WM_NCCALCSIZE:
    {
        if (wParam)
        {
            NCCALCSIZE_PARAMS* pParams = (NCCALCSIZE_PARAMS*)lParam;
            if (IsZoomed(hWnd))
            {
                HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                if (hMonitor)
                {
                    MONITORINFO mi = { sizeof(mi) };
                    if (GetMonitorInfoW(hMonitor, &mi))
                    {
                        pParams->rgrc[0] = mi.rcWork;
                    }
                }
            }
            return 0;
        }
        break;
    }

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_VS_DEFERRED_LOAD:
    {
        if (pState)
        {
            PopulateAssetTree(pState);
            if (pState->activeTab == 0 && !pState->shapeFilePath.empty())
            {
                TriggerAsyncShapeLoad(pState, pState->shapeFilePath);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_VS_SHAPE_LOADED:
    {
        uint32_t reqId = (uint32_t)wParam;
        auto* pModel = (ShapeReader::ShapeModel*)lParam;
        if (pState)
        {
            if (reqId == pState->currentRequestId.load())
            {
                pState->isLoading = false;
                if (pModel)
                {
                    {
                        std::lock_guard<std::mutex> lock(s_shapeCacheMutex);
                        std::wstring key = pState->shapeFilePath;
                        std::transform(key.begin(), key.end(), key.begin(), ::towlower);
                        auto it = s_shapeCache.find(key);
                        if (it != s_shapeCache.end())
                        {
                            pState->pCurrentShape = it->second;
                        }
                    }

                    pState->camTarget = pModel->bounds.center;
                    float r = pModel->bounds.radius;
                    pState->camDistance = (r > 0.1f) ? (r * 2.2f) : 25.0f;
                    if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;
                    pState->camYaw = -135.0f;
                    pState->camPitch = 22.0f;

                    if (pState->renderer.IsInitialized())
                    {
                        std::wstring modelDir = std::filesystem::path(pState->shapeFilePath).parent_path().wstring();
                        pState->renderer.UploadModel(*pModel, modelDir);
                    }
                    if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
                    {
                        InvalidateRect(pState->hViewportWnd, NULL, FALSE);
                        UpdateWindow(pState->hViewportWnd);
                    }
                }
                InvalidateRect(hWnd, &pState->rcFooter, FALSE);
            }
        }
        return 0;
    }

    case WM_NAVTOOLBAR_NAVIGATE:
    {
        if (pState && pState->hNavToolbar)
        {
            wchar_t szPath[MAX_PATH] = { 0 };
            NavToolbar_GetPath(pState->hNavToolbar, szPath, MAX_PATH);
            if (szPath[0] != L'\0')
            {
                pState->currentDirectory = szPath;
                if (pState->activeTab == 0)
                    pState->shapeDirectory = szPath;
                else
                    pState->stockDirectory = szPath;

                SaveStudioDirectory(pState->activeTab, pState->currentDirectory);
                PopulateAssetTree(pState);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        if (!IsZoomed(hWnd))
        {
            int b = 6;
            if (pt.y < b && pt.x < b) return HTTOPLEFT;
            if (pt.y < b && pt.x >= rcClient.right - b) return HTTOPRIGHT;
            if (pt.y >= rcClient.bottom - b && pt.x < b) return HTBOTTOMLEFT;
            if (pt.y >= rcClient.bottom - b && pt.x >= rcClient.right - b) return HTBOTTOMRIGHT;
            if (pt.y < b) return HTTOP;
            if (pt.y >= rcClient.bottom - b) return HTBOTTOM;
            if (pt.x < b) return HTLEFT;
            if (pt.x >= rcClient.right - b) return HTRIGHT;
        }

        if (pt.y >= 0 && pt.y < 66)
        {
            if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
            {
                LRESULT hit = SendMessageW(pState->hTitleBar, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                if (hit == HTTRANSPARENT)
                {
                    return HTCAPTION;
                }
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_TITLEBAR_TABCHANGED:
    {
        if (pState)
        {
            pState->activeTab = (int)wParam;
            UpdateToolbarForTab(pState);
            PopulateAssetTree(pState);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            RecalculateViewerLayout(pState, w, h);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
    {
        RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            RestoreParentWindowFocus(pState ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            DestroyWindow(hWnd);
            return 0;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        for (auto& btn : pState->ribbonButtons)
        {
            if (PtInRect(&btn.rc, pt))
            {
                btn.isPressed = true;
                InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        for (auto& btn : pState->ribbonButtons)
        {
            if (btn.isPressed)
            {
                btn.isPressed = false;
                if (PtInRect(&btn.rc, pt))
                {
                    if (btn.isToggle)
                    {
                        btn.isActive = !btn.isActive;
                    }

                    // Handle actions
                    switch (btn.id)
                    {
                    case 1: // Reset View
                        if (pState->pCurrentShape && pState->pCurrentShape->isLoaded)
                        {
                            pState->camTarget = pState->pCurrentShape->bounds.center;
                            float r = pState->pCurrentShape->bounds.radius;
                            pState->camDistance = (r > 0.1f) ? (r * 2.2f) : 25.0f;
                            if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;
                        }
                        else
                        {
                            pState->camTarget = { 0.0f, 0.0f, 0.0f };
                            pState->camDistance = 25.0f;
                        }
                        pState->camYaw = -135.0f;
                        pState->camPitch = 22.0f;
                        break;

                    case 2: // Isometric
                        pState->camYaw = -135.0f;
                        pState->camPitch = 35.264f;
                        break;

                    case 3: // Front
                        pState->camYaw = 180.0f;
                        pState->camPitch = 0.0f;
                        break;

                    case 4: // Left Side (Port Wall - text reads left-to-right)
                        pState->camYaw = -90.0f;
                        pState->camPitch = 0.0f;
                        break;

                    case 5: // Top
                        pState->camYaw = 0.0f;
                        pState->camPitch = 89.0f;
                        break;

                    case 6: // Wireframe
                        pState->wireframeMode = btn.isActive;
                        break;

                    case 7: // Lighting
                        pState->lightingEnabled = !btn.isActive;
                        break;

                    case 8: // Ground Grid
                        pState->gridEnabled = btn.isActive;
                        break;

                    case 9: // Animation Play/Pause
                        pState->renderer.SetAnimationEnabled(btn.isActive);
                        btn.iconGlyph = btn.isActive ? L"\xE769" : L"\xE768";
                        btn.label = btn.isActive ? L"Pause Anim" : L"Play Anim";
                        break;

                    case 10: // Restart Anim
                        pState->renderer.SetAnimationTime(0.0f);
                        break;
                    }

                    if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
                    {
                        InvalidateRect(pState->hViewportWnd, NULL, FALSE);
                    }
                }
                InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
                break;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        bool needRepaintRibbon = false;
        for (auto& btn : pState->ribbonButtons)
        {
            bool hov = PtInRect(&btn.rc, pt);
            if (hov != btn.isHovered)
            {
                btn.isHovered = hov;
                needRepaintRibbon = true;
            }
        }

        if (needRepaintRibbon) InvalidateRect(hWnd, &pState->rcRibbon, FALSE);

        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (pState)
        {
            for (auto& btn : pState->ribbonButtons) btn.isHovered = false;
            InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (!pState) { EndPaint(hWnd, &ps); return 0; }

        int width = pState->rcRibbon.right;
        int height = pState->rcFooter.bottom;

        // 1. Ribbon Toolbar Background
        HBRUSH hbrRibbon = CreateSolidBrush(VisualStudioTheme::RibbonBg);
        FillRect(hdc, &pState->rcRibbon, hbrRibbon);
        DeleteObject(hbrRibbon);

        HPEN hPenBorder = CreatePen(PS_SOLID, 1, VisualStudioTheme::BorderLine);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenBorder);
        MoveToEx(hdc, 0, pState->rcRibbon.bottom - 1, NULL);
        LineTo(hdc, width, pState->rcRibbon.bottom - 1);

        // Draw Ribbon Buttons
        for (const auto& btn : pState->ribbonButtons)
        {
            COLORREF btnBg = VisualStudioTheme::ButtonBg;
            if (btn.isActive)
                btnBg = btn.isHovered ? VisualStudioTheme::ButtonActiveHover : VisualStudioTheme::ButtonActive;
            else if (btn.isHovered)
                btnBg = VisualStudioTheme::ButtonHover;

            HBRUSH hbrBtn = CreateSolidBrush(btnBg);
            HPEN hPenBtn = CreatePen(PS_SOLID, 1, VisualStudioTheme::CardBorder);
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hbrBtn);
            HPEN hOldP = (HPEN)SelectObject(hdc, hPenBtn);

            RoundRect(hdc, btn.rc.left, btn.rc.top, btn.rc.right, btn.rc.bottom, 6, 6);

            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldP);
            DeleteObject(hbrBtn);
            DeleteObject(hPenBtn);

            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, pState->hFontIconSmall ? pState->hFontIconSmall : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, VisualStudioTheme::TagText);
            RECT rcIcon = { btn.rc.left + 6, btn.rc.top + 6, btn.rc.left + 24, btn.rc.bottom - 6 };
            DrawTextW(hdc, btn.iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, VisualStudioTheme::TextPrimary);
            RECT rcLabel = { btn.rc.left + 26, btn.rc.top, btn.rc.right - 6, btn.rc.bottom };
            DrawTextW(hdc, btn.label, -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // 2. Sidebar Border
        MoveToEx(hdc, pState->rcSidebar.right - 1, pState->rcSidebar.top, NULL);
        LineTo(hdc, pState->rcSidebar.right - 1, pState->rcSidebar.bottom);

        // 3. Footer Status Bar
        HBRUSH hbrFooter = CreateSolidBrush(VisualStudioTheme::FooterBg);
        FillRect(hdc, &pState->rcFooter, hbrFooter);
        DeleteObject(hbrFooter);

        MoveToEx(hdc, 0, pState->rcFooter.top, NULL);
        LineTo(hdc, width, pState->rcFooter.top);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPenBorder);

        // Footer Text
        const std::wstring& activePath = (pState->activeTab == 0) ? pState->shapeFilePath : pState->stockFilePath;
        if (!activePath.empty())
        {
            std::wstring fName = std::filesystem::path(activePath).filename().wstring();
            std::wstring statusLeft;
            if (pState->activeTab == 0)
            {
                if (pState->isLoading)
                {
                    statusLeft = L"Shape: " + fName + L"  |  Loading 3D mesh in background...";
                }
                else if (pState->pCurrentShape && pState->pCurrentShape->isLoaded)
                {
                    std::wstringstream ss;
                    ss << L"Shape: " << fName
                       << L"  |  Vertices: " << pState->pCurrentShape->totalVertices
                       << L"  |  Triangles: " << pState->pCurrentShape->totalTriangles
                       << L"  |  Submeshes: " << pState->pCurrentShape->subMeshes.size();
                    if (!pState->pCurrentShape->animations.empty() && pState->pCurrentShape->animations[0].frameCount > 0)
                    {
                        ss << L"  |  Animation: " << pState->pCurrentShape->animations[0].frameCount << L" frames @ " << pState->pCurrentShape->animations[0].frameRate << L" fps";
                    }
                    ss << L"  |  Load Time: " << std::fixed << std::setprecision(1) << pState->pCurrentShape->loadTimeMs << L" ms";
                    statusLeft = ss.str();
                }
                else
                {
                    statusLeft = L"Shape: " + fName;
                }
            }
            else
            {
                statusLeft = L"Rolling Stock: " + fName;
            }

            SelectObject(hdc, pState->hFontBold ? pState->hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, VisualStudioTheme::TextPrimary);
            TextOutW(hdc, 16, pState->rcFooter.top + (26 - 15) / 2, statusLeft.c_str(), (int)statusLeft.length());
        }
        else
        {
            std::wstring noShapeStr = (pState->activeTab == 0)
                ? L"No Shape File Selected (Choose a .s model from the tree)"
                : L"No Stock File Selected (Choose a .wag / .eng from the tree)";
            SelectObject(hdc, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, VisualStudioTheme::TextMuted);
            TextOutW(hdc, 16, pState->rcFooter.top + (26 - 15) / 2, noShapeStr.c_str(), (int)noShapeStr.length());
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
            if (pState->hFontTitle) DeleteObject(pState->hFontTitle);
            if (pState->hFontHeader) DeleteObject(pState->hFontHeader);
            if (pState->hFontMain) DeleteObject(pState->hFontMain);
            if (pState->hFontBold) DeleteObject(pState->hFontBold);
            if (pState->hFontMono) DeleteObject(pState->hFontMono);
            if (pState->hFontIcon) DeleteObject(pState->hFontIcon);
            if (pState->hFontIconSmall) DeleteObject(pState->hFontIconSmall);

            delete pState;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        return 0;
    }

    case WM_NCDESTROY:
    {
        HWND hOwner = GetWindow(hWnd, GW_OWNER);
        if (hOwner && IsWindow(hOwner))
        {
            RestoreParentWindowFocus(hOwner);
        }
        break;
    }

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

void Show3DVisualStudioDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath)
{
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWndParent, GWLP_HINSTANCE);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = Visual3DStudioProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"TSCB_Visual3DStudioWnd";

    RegisterClassExW(&wc);

    VisualStudioState* pState = new VisualStudioState();
    pState->basePath = basePath;

    pState->shapeDirectory = LoadSavedStudioDirectory(0);
    pState->stockDirectory = LoadSavedStudioDirectory(1);

    size_t dotPos = filePath.find_last_of(L'.');
    std::wstring ext = (dotPos != std::wstring::npos) ? filePath.substr(dotPos) : L"";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == L".wag" || ext == L".eng")
    {
        pState->activeTab = 1;
        pState->stockFilePath = filePath;
        size_t lastSlash = filePath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            pState->stockDirectory = filePath.substr(0, lastSlash);
            SaveStudioDirectory(1, pState->stockDirectory);
        }
    }
    else if (ext == L".s")
    {
        pState->activeTab = 0;
        pState->shapeFilePath = filePath;
        size_t lastSlash = filePath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            pState->shapeDirectory = filePath.substr(0, lastSlash);
            SaveStudioDirectory(0, pState->shapeDirectory);
        }
    }
    else
    {
        pState->activeTab = 0;
        pState->shapeFilePath = filePath;
    }

    pState->currentDirectory = (pState->activeTab == 0) ? pState->shapeDirectory : pState->stockDirectory;

    int winW = 1280;
    int winH = 820;

    RECT rcParent = { 0 };
    if (hWndParent && IsWindow(hWndParent))
    {
        GetWindowRect(hWndParent, &rcParent);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }

    int posX = rcParent.left + ((rcParent.right - rcParent.left) - winW) / 2;
    int posY = rcParent.top + ((rcParent.bottom - rcParent.top) - winH) / 2;
    if (posX < 20) posX = 20;
    if (posY < 20) posY = 20;

    HWND hWnd = CreateWindowExW(
        0,
        L"TSCB_Visual3DStudioWnd",
        L"3D Visual Studio",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        hWndParent,
        NULL,
        hInstance,
        pState
    );

    if (hWnd)
    {
        BOOL bDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
        DWM_WINDOW_CORNER_PREFERENCE corner = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
        COLORREF borderColor = RGB(46, 46, 46);
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        MARGINS margins = { 0, 0, 0, 0 };
        DwmExtendFrameIntoClientArea(hWnd, &margins);

        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        ShowWindow(hWnd, SW_SHOW);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, clientW, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
        }

        RecalculateViewerLayout(pState, clientW, clientH);
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
        UpdateWindow(hWnd);
        SetFocus(hWnd);
    }
}

void ShowShapeViewerDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath)
{
    Show3DVisualStudioDialog(hWndParent, filePath, basePath);
}
