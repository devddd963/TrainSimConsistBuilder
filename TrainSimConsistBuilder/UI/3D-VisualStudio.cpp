#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "3D-VisualStudio.h"
#include "CustomTitleBar.h"
#include "NavToolbar.h"
#include "CustomTreeView.h"
#include "CustomDropDownMenu.h"
#include "ModernMessageBox.h"
#include "UITheme.h"
#include "../SRC/AppLogging.h"
#include "../SRC/DatabaseManager.h"
#include "../SRC/ShapeReader.h"
#include "../SRC/ShapeAnimator.h"
#include "../SRC/TextureLoader.h"
#include "../SRC/CompositeStockLoader.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <thread>
#include <atomic>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define IDC_VS_TITLEBAR   10001
#define IDC_VS_NAVTOOLBAR 10002
#define IDC_VS_TREEVIEW   10003

#define WM_VS_DEFERRED_LOAD        (WM_USER + 201)
#define WM_VS_SHAPE_LOADED_ASYNC   (WM_USER + 202)
#define WM_VS_STOCK_LOADED_ASYNC   (WM_USER + 203)
#define WM_VS_TEXTURES_READY       (WM_USER + 204)
#define WM_VS_LOAD_FILE_EXTERNAL   (WM_USER + 205)
#define SUBCLASS_TREE_ID 0x5C01
#define TIMER_TREE_FAYT_TIMEOUT 0x5C03
#define TIMER_VIEWPORT_NAV 0x5C05
#define TIMER_ANIM_PLAYBACK 0x5C07

static HWND s_hActiveStudioWnd = NULL;

namespace VisualStudioTheme
{
    constexpr COLORREF Background         = RGB(18, 18, 18);
    constexpr COLORREF RibbonBg           = RGB(38, 14, 18);
    constexpr COLORREF SidebarBg          = RGB(22, 22, 22);
    constexpr COLORREF FooterBg           = RGB(24, 24, 24);
    constexpr COLORREF CardBorder         = RGB(58, 24, 30);
    constexpr COLORREF BorderLine         = RGB(46, 46, 46);
    constexpr COLORREF ButtonBg           = RGB(48, 18, 24);
    constexpr COLORREF ButtonHover        = RGB(68, 26, 34);
    constexpr COLORREF ButtonActive       = RGB(0, 120, 215);
    constexpr COLORREF ButtonActiveHover  = RGB(20, 140, 235);
    constexpr COLORREF TextPrimary        = RGB(245, 245, 245);
    constexpr COLORREF TextSecondary      = RGB(180, 175, 175);
    constexpr COLORREF TextMuted          = RGB(130, 130, 130);
    constexpr COLORREF TagText            = RGB(100, 185, 255);
    constexpr COLORREF ViewportBg         = RGB(12, 12, 14);
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

// Grid Vertex Layout (Position + Color)
struct GridVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

// Constant Buffer for Shaders (Frame / Camera / Lighting)
struct CBPerFrame {
    DirectX::XMMATRIX WorldViewProj;
    DirectX::XMMATRIX World;
    DirectX::XMFLOAT3 LightDir;
    float             LightIntensity;
    DirectX::XMFLOAT3 AmbientColor;
    float             AlphaCutoff;
    float             GlassTintAlpha;
    DirectX::XMFLOAT3 Padding;
};

// Constant Buffer for Hierarchical Bone / Node Animation Matrices
struct CBBones {
    DirectX::XMMATRIX BoneTransforms[256];
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
    std::wstring searchQuery;

    // Navigation History
    std::vector<std::wstring> navHistory;
    int navHistoryIndex = -1;

    // Tree View Root Node
    CustomTreeNode* pTreeRootNode = nullptr;

    // TreeView Floating FAYT (Find-As-You-Type) Search State
    bool bTreeFaytActive = false;
    std::wstring treeFaytQuery;
    std::vector<CustomTreeNode*> treeFaytMatches;
    int treeFaytMatchIndex = -1;
    RECT rcFaytPill = { 0 };
    RECT rcFaytPrevBtn = { 0 };
    RECT rcFaytNextBtn = { 0 };
    RECT rcFaytCloseBtn = { 0 };
    int treeFaytHoverBtn = 0; // 0=none, 1=prev, 2=next, 3=close

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
    RECT rcSplitter = { 0 };
    RECT rcFaytDock = { 0 };
    RECT rcViewport = { 0 };
    RECT rcFooter = { 0 };

    int sidebarWidth = 240;
    bool isDraggingSplitter = false;
    int dragSplitterStartX = 0;
    int dragSidebarStartW = 240;

    // Ribbon Toolbar Buttons
    std::vector<RibbonButton> ribbonButtons;
    std::vector<int> ribbonSeparators;

    // Direct3D 11 Renderer State
    ID3D11Device*           pD3DDevice = nullptr;
    ID3D11DeviceContext*    pD3DContext = nullptr;
    IDXGISwapChain*         pSwapChain = nullptr;
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    ID3D11Texture2D*        pDepthStencilBuffer = nullptr;
    ID3D11DepthStencilView* pDepthStencilView = nullptr;

    ID3D11RasterizerState*  pRasterStateSolid = nullptr;
    ID3D11RasterizerState*  pRasterStateWireframe = nullptr;
    ID3D11RasterizerState*  pRasterStateDecal = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateWrite = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateReadOnly = nullptr;
    ID3D11BlendState*       pBlendStateOpaque = nullptr;
    ID3D11BlendState*       pBlendStateAlpha = nullptr;
    ID3D11SamplerState*     pSamplerState = nullptr;

    ID3D11VertexShader*     pShapeVS = nullptr;
    ID3D11PixelShader*      pShapePS = nullptr;
    ID3D11InputLayout*      pShapeLayout = nullptr;

    ID3D11VertexShader*     pGridVS = nullptr;
    ID3D11PixelShader*      pGridPS = nullptr;
    ID3D11InputLayout*      pGridLayout = nullptr;
    ID3D11Buffer*           pGridVB = nullptr;
    uint32_t                gridVertexCount = 0;

    ID3D11Buffer*           pConstantBuffer = nullptr;
    ID3D11Buffer*           pBoneConstantBuffer = nullptr;

    std::unique_ptr<TextureLoader> pTextureLoader;
    ParsedShape             currentShape;
    CompositeStockUnit      compositeStock;

    // Async Background Shape Loading
    std::atomic<uint64_t>   activeLoadTaskId = 0;
    bool                    isLoadingShape = false;
    std::wstring            loadingShapeName;
    double                  shapeLoadTimeMs = 0.0;

    // 3D Animation Playback Engine State
    bool                    isAnimPlaying = false;
    float                   animCurrentFrame = 0.0f;
    float                   animSpeed = 1.0f;
    float                   animPingPongDir = 1.0f;
    bool                    isPingPongMode = true;
    uint64_t                lastAnimTick = 0;
    int                     activeAnimFilterType = 0; // 0 = All, 1 = Pantos, 2 = Doors, 3 = Wipers, 4 = Wheels, 5 = Fans, 6 = Misc, >=100 Node Index, 20 = Simulated Wheels, >=200 Specific Wheel Node
    std::wstring            activeAnimLabel = L"Animations \x25BE";

    // Procedural Simulated Wheel Roll State
    bool                    enableWheelSpin = true;
    float                   wheelSpinAngle = 0.0f;
    float                   wheelSpinSpeed = 3.5f; // radians per second

    // Camera Orbit / Pan / Zoom
    float camYaw = -45.0f;
    float camPitch = 25.0f;
    float camDistance = 20.0f;
    DirectX::XMFLOAT3 camTarget = { 0.0f, 1.5f, 0.0f };

    bool isOrbiting = false;
    bool isPanning = false;
    POINT lastMousePt = { 0, 0 };

    bool showWireframe = false;
    bool showGroundGrid = true;

    // Smooth Continuous Keyboard Navigation State
    bool keyW = false;
    bool keyA = false;
    bool keyS = false;
    bool keyD = false;
    bool keyUp = false;
    bool keyDown = false;
    bool keyLeft = false;
    bool keyRight = false;
    bool keyQ = false;
    bool keyE = false;
    bool keyZ = false;
    bool keyX = false;
    bool keySpace = false;
    bool keyC = false;
    bool isKeyNavActive = false;
    uint64_t lastNavTick = 0;
};

static std::wstring LoadSavedStudioDirectory(int tabIndex)
{
    const wchar_t* valName = (tabIndex == 0) ? L"ShapeViewerLastDirectory" : L"StockViewerLastDirectory";
    return DatabaseManager::GetSetting(valName, L"");
}

static void SaveStudioDirectory(int tabIndex, const std::wstring& path)
{
    if (path.empty()) return;
    const wchar_t* valName = (tabIndex == 0) ? L"ShapeViewerLastDirectory" : L"StockViewerLastDirectory";
    DatabaseManager::SetSetting(valName, path);
}

static int LoadSavedStudioSidebarWidth()
{
    int dwWidth = DatabaseManager::GetSettingInt(L"StudioSidebarWidth", 240);
    if (dwWidth < 140 || dwWidth > 800) dwWidth = 240;
    return dwWidth;
}

static void SaveStudioSidebarWidth(int width)
{
    if (width < 140 || width > 800) return;
    DatabaseManager::SetSettingInt(L"StudioSidebarWidth", width);
}

static void PushNavHistory(VisualStudioState* pState, const std::wstring& path)
{
    if (!pState || path.empty()) return;
    if (pState->navHistoryIndex >= 0 && pState->navHistoryIndex < (int)pState->navHistory.size())
    {
        if (_wcsicmp(pState->navHistory[pState->navHistoryIndex].c_str(), path.c_str()) == 0)
        {
            return;
        }
        pState->navHistory.erase(pState->navHistory.begin() + pState->navHistoryIndex + 1, pState->navHistory.end());
    }
    pState->navHistory.push_back(path);
    pState->navHistoryIndex = (int)pState->navHistory.size() - 1;
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
        { 8, L"\xE80A", L"Ground Grid", true, true },
        { 9, L"\xE768", L"Play Anim", true, false },
        { 10, L"\xE895", L"Restart Anim", false, false },
        { 11, L"\xE8B8", L"Animations \x25BE", false, false }
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
    pState->ribbonSeparators.clear();

    // Responsive width helper per button
    auto getButtonWidth = [](int id, bool compact) -> int {
        if (compact) {
            if (id == 11) return 112;
            if (id == 10) return 92;
            if (id == 9)  return 86;
            if (id == 8)  return 88;
            if (id == 6)  return 82;
            if (id == 1)  return 84;
            if (id == 2)  return 78;
            if (id >= 3 && id <= 5) return 56;
            return 76;
        }
        if (id == 11) return 132;
        if (id == 10) return 106;
        if (id == 9)  return 98;
        if (id == 8)  return 102;
        if (id == 6)  return 94;
        if (id == 1)  return 98;
        if (id == 2)  return 92;
        if (id >= 3 && id <= 5) return 68;
        return 88;
    };

    bool isCompact = (width < 960);

    int totalLeftW = 0;
    int totalRightW = 0;
    for (const auto& btn : pState->ribbonButtons) {
        int w = getButtonWidth(btn.id, isCompact);
        if (btn.id >= 9) {
            totalRightW += w + 4;
        } else {
            totalLeftW += w + 4;
        }
    }
    totalLeftW += 16; // Space for divider between Camera Views and Display Modes

    bool canRightAlignAnim = (width >= (totalLeftW + totalRightW + 48));

    int btnX = 12;
    for (auto& btn : pState->ribbonButtons)
    {
        // Separator between Camera Views and Display Modes (before Wireframe, id 6)
        if (btn.id == 6)
        {
            pState->ribbonSeparators.push_back(btnX + 4);
            btnX += 14;
        }
        // Animation Controls (id >= 9): Auto-align to right if wide, or flow cleanly if compact
        else if (btn.id == 9)
        {
            if (canRightAlignAnim)
            {
                int animStartX = width - totalRightW - 14;
                pState->ribbonSeparators.push_back(animStartX - 10);
                btnX = animStartX;
            }
            else
            {
                pState->ribbonSeparators.push_back(btnX + 4);
                btnX += 14;
            }
        }

        int btnW = getButtonWidth(btn.id, isCompact);
        btn.rc = { btnX, 114, btnX + btnW, 144 };
        btnX += btnW + 4;
    }

    // Y = 148..(height - 26): Main Content Area
    int splitterW = 6;
    int maxSidebarW = (std::max)(140, width - 300);
    int sidebarW = pState->sidebarWidth;
    if (sidebarW < 140) sidebarW = 140;
    if (sidebarW > maxSidebarW) sidebarW = maxSidebarW;
    pState->sidebarWidth = sidebarW;

    int contentTop = 148;
    int footerH = 26;
    int contentH = (std::max)(100, height - contentTop - footerH);

    int faytH = pState->bTreeFaytActive ? 36 : 0;
    int treeH = (std::max)(40, contentH - faytH);

    pState->rcSidebar = { 0, contentTop, sidebarW, contentTop + contentH };
    pState->rcSplitter = { sidebarW, contentTop, sidebarW + splitterW, contentTop + contentH };
    pState->rcFaytDock = { 0, contentTop + contentH - faytH, sidebarW, contentTop + contentH };
    pState->rcViewport = { sidebarW + splitterW, contentTop, width, contentTop + contentH };
    pState->rcFooter = { 0, height - footerH, width, height };

    HDWP hdwp = BeginDeferWindowPos(4);
    if (hdwp)
    {
        if (pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            hdwp = DeferWindowPos(hdwp, pState->hTitleBar, NULL, 0, 0, width, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS);
        }
        if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
        {
            hdwp = DeferWindowPos(hdwp, pState->hNavToolbar, NULL, 0, 66, width, 44, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        if (pState->assetTree.GetHWND())
        {
            hdwp = DeferWindowPos(hdwp, pState->assetTree.GetHWND(), NULL, 0, contentTop, sidebarW, treeH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
        {
            hdwp = DeferWindowPos(hdwp, pState->hViewportWnd, NULL, sidebarW + splitterW, contentTop, width - (sidebarW + splitterW), contentH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        if (hdwp) EndDeferWindowPos(hdwp);
    }
    else
    {
        if (pState->hTitleBar && IsWindow(pState->hTitleBar))
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, width, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS);
        if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
            SetWindowPos(pState->hNavToolbar, NULL, 0, 66, width, 44, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        if (pState->assetTree.GetHWND())
            SetWindowPos(pState->assetTree.GetHWND(), NULL, 0, contentTop, sidebarW, treeH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
            SetWindowPos(pState->hViewportWnd, NULL, sidebarW + splitterW, contentTop, width - (sidebarW + splitterW), contentH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }

    if (pState->hTitleBar && IsWindow(pState->hTitleBar))
        InvalidateRect(pState->hTitleBar, NULL, FALSE);
    if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
        InvalidateRect(pState->hNavToolbar, NULL, FALSE);
    if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
        InvalidateRect(pState->hViewportWnd, NULL, FALSE);
}

// -------------------------------------------------------------
// Folder Population & Dynamic Navigation (Native kernel32.dll APIs)
// -------------------------------------------------------------
static void PopulateFolderChildren(VisualStudioState* pState, CustomTreeNode* folderNode, const std::wstring& folderPath, bool forceReload = false)
{
    if (!pState || !folderNode || !folderNode->isFolder || folderPath.empty()) return;

    if (!forceReload && !folderNode->children.empty())
    {
        return; // Already populated
    }

    pState->assetTree.DeleteAllChildren(folderNode);

    std::wstring searchPattern = folderPath;
    if (searchPattern.back() != L'\\' && searchPattern.back() != L'/')
    {
        searchPattern += L"\\";
    }
    std::wstring baseDir = searchPattern;
    searchPattern += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::pair<std::wstring, std::wstring>> subDirs;
    std::vector<std::pair<std::wstring, std::wstring>> matchingFiles;

    do
    {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
        {
            continue;
        }

        std::wstring fullPath = baseDir + ffd.cFileName;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            subDirs.push_back({ ffd.cFileName, fullPath });
        }
        else
        {
            const wchar_t* ext = PathFindExtensionW(ffd.cFileName);
            if (ext)
            {
                if (pState->activeTab == 0 && _wcsicmp(ext, L".s") == 0)
                {
                    matchingFiles.push_back({ ffd.cFileName, fullPath });
                }
                else if (pState->activeTab == 1 && (_wcsicmp(ext, L".wag") == 0 || _wcsicmp(ext, L".eng") == 0))
                {
                    matchingFiles.push_back({ ffd.cFileName, fullPath });
                }
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    std::sort(subDirs.begin(), subDirs.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
    });
    std::sort(matchingFiles.begin(), matchingFiles.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
    });

    for (const auto& dir : subDirs)
    {
        pState->assetTree.AddChild(folderNode, dir.first, dir.second, 0, true);
    }

    for (const auto& file : matchingFiles)
    {
        pState->assetTree.AddChild(folderNode, file.first, file.second, 0, false);
    }
}

static void PopulateAssetTree(VisualStudioState* pState)
{
    if (!pState || !pState->assetTree.GetHWND()) return;

    pState->assetTree.Clear();
    pState->pTreeRootNode = nullptr;

    std::wstring searchDir = (pState->activeTab == 0) ? pState->shapeDirectory : pState->stockDirectory;
    if (searchDir.empty())
    {
        if (!pState->basePath.empty())
        {
            searchDir = pState->basePath + L"\\TRAINS\\TRAINSET";
            DWORD dwAttr = GetFileAttributesW(searchDir.c_str());
            if (dwAttr == INVALID_FILE_ATTRIBUTES || !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
            {
                searchDir = pState->basePath;
            }
        }
        else
        {
            searchDir = L"C:\\";
        }
    }

    pState->currentDirectory = searchDir;
    PushNavHistory(pState, searchDir);

    wchar_t szRootName[MAX_PATH] = { 0 };
    wcscpy_s(szRootName, searchDir.c_str());
    PathStripPathW(szRootName);
    std::wstring rootName = szRootName;
    if (rootName.empty()) rootName = searchDir;

    auto* rootNode = pState->assetTree.AddRoot(rootName, searchDir, 0, true);
    if (!rootNode) return;

    pState->pTreeRootNode = rootNode;
    // Fast single-level scan using direct kernel32.dll FindFirstFileW
    PopulateFolderChildren(pState, rootNode, searchDir, true);
    pState->assetTree.ExpandNode(rootNode, true);
}

// -------------------------------------------------------------
// Search and Tree Filtering (Native kernel32.dll APIs)
// -------------------------------------------------------------
static void SearchDirectoryRecursive(VisualStudioState* pState, const std::wstring& dirPath, const std::wstring& query, CustomTreeNode* parentNode, int currentDepth, int maxDepth, int& matchCount)
{
    if (currentDepth > maxDepth || matchCount >= 500) return;

    std::wstring searchPattern = dirPath;
    if (searchPattern.back() != L'\\' && searchPattern.back() != L'/')
    {
        searchPattern += L"\\";
    }
    std::wstring baseDir = searchPattern;
    searchPattern += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (matchCount >= 500) break;
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

        std::wstring fullPath = baseDir + ffd.cFileName;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            bool dirNameMatches = (StrStrIW(ffd.cFileName, query.c_str()) != NULL);
            CustomTreeNode* dirNode = nullptr;
            if (dirNameMatches)
            {
                dirNode = pState->assetTree.AddChild(parentNode, ffd.cFileName, fullPath, 0, true);
                matchCount++;
            }

            SearchDirectoryRecursive(pState, fullPath, query, dirNode ? dirNode : parentNode, currentDepth + 1, maxDepth, matchCount);

            if (dirNode)
            {
                pState->assetTree.ExpandNode(dirNode, true);
            }
        }
        else
        {
            const wchar_t* ext = PathFindExtensionW(ffd.cFileName);
            if (ext)
            {
                bool isCandidate = false;
                if (pState->activeTab == 0 && _wcsicmp(ext, L".s") == 0) isCandidate = true;
                else if (pState->activeTab == 1 && (_wcsicmp(ext, L".wag") == 0 || _wcsicmp(ext, L".eng") == 0)) isCandidate = true;

                if (isCandidate)
                {
                    if (StrStrIW(ffd.cFileName, query.c_str()) != NULL)
                    {
                        pState->assetTree.AddChild(parentNode, ffd.cFileName, fullPath, 0, false);
                        matchCount++;
                    }
                }
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
}

static void PerformTreeSearch(VisualStudioState* pState, const std::wstring& query)
{
    if (!pState || !pState->assetTree.GetHWND()) return;

    pState->searchQuery = query;

    if (query.empty())
    {
        PopulateAssetTree(pState);
        return;
    }

    pState->assetTree.Clear();
    pState->pTreeRootNode = nullptr;

    std::wstring searchDir = pState->currentDirectory;
    if (searchDir.empty())
    {
        searchDir = (pState->activeTab == 0) ? pState->shapeDirectory : pState->stockDirectory;
    }
    if (searchDir.empty())
    {
        searchDir = pState->basePath;
    }

    std::wstring rootTitle = L"Search: \"" + query + L"\"";
    auto* rootNode = pState->assetTree.AddRoot(rootTitle, searchDir, 0, true);
    if (!rootNode) return;

    pState->pTreeRootNode = rootNode;
    int matchCount = 0;
    SearchDirectoryRecursive(pState, searchDir, query, rootNode, 1, 5, matchCount);

    pState->assetTree.ExpandNode(rootNode, true);
    if (!rootNode->children.empty())
    {
        pState->assetTree.SelectNode(rootNode->children[0]);
    }
    pState->assetTree.Invalidate();
}

// -------------------------------------------------------------
// Dedicated Sidebar Find-As-You-Type (FAYT) Search Box
// -------------------------------------------------------------
static void CollectAllTreeNodes(CustomTreeNode* node, std::vector<CustomTreeNode*>& outNodes)
{
    if (!node) return;
    outNodes.push_back(node);
    for (auto* child : node->children)
    {
        CollectAllTreeNodes(child, outNodes);
    }
}

static void UpdateTreeFaytMatches(VisualStudioState* pState)
{
    if (!pState) return;
    pState->treeFaytMatches.clear();
    pState->treeFaytMatchIndex = -1;

    if (pState->treeFaytQuery.empty() || !pState->pTreeRootNode)
    {
        pState->assetTree.Invalidate();
        return;
    }

    std::vector<CustomTreeNode*> allNodes;
    CollectAllTreeNodes(pState->pTreeRootNode, allNodes);

    for (auto* node : allNodes)
    {
        if (StrStrIW(node->text.c_str(), pState->treeFaytQuery.c_str()) != NULL)
        {
            pState->treeFaytMatches.push_back(node);
        }
    }

    if (!pState->treeFaytMatches.empty())
    {
        pState->treeFaytMatchIndex = 0;
        CustomTreeNode* match = pState->treeFaytMatches[0];

        CustomTreeNode* p = match->parent;
        while (p)
        {
            pState->assetTree.ExpandNode(p, true);
            p = p->parent;
        }

        pState->assetTree.SelectNode(match);
        pState->assetTree.EnsureVisible(match);
    }

    pState->assetTree.Invalidate();
}

static void StartTreeFayt(VisualStudioState* pState, wchar_t initialChar)
{
    if (!pState) return;
    pState->bTreeFaytActive = true;
    pState->treeFaytQuery = (initialChar >= 32) ? std::wstring(1, initialChar) : L"";

    RECT rcClient;
    GetClientRect(pState->hWnd, &rcClient);
    RecalculateViewerLayout(pState, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);

    UpdateTreeFaytMatches(pState);
    SetTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT, 5000, NULL);
    InvalidateRect(pState->hWnd, &pState->rcSidebar, FALSE);
}

static void CloseTreeFayt(VisualStudioState* pState)
{
    if (!pState || !pState->bTreeFaytActive) return;
    pState->bTreeFaytActive = false;
    pState->treeFaytQuery.clear();
    pState->treeFaytMatches.clear();
    pState->treeFaytMatchIndex = -1;
    pState->treeFaytHoverBtn = 0;
    KillTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT);

    RECT rcClient;
    GetClientRect(pState->hWnd, &rcClient);
    RecalculateViewerLayout(pState, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
    InvalidateRect(pState->hWnd, &pState->rcSidebar, FALSE);
}

static void TreeFaytNext(VisualStudioState* pState)
{
    if (!pState || pState->treeFaytMatches.empty()) return;
    pState->treeFaytMatchIndex = (pState->treeFaytMatchIndex + 1) % (int)pState->treeFaytMatches.size();
    CustomTreeNode* match = pState->treeFaytMatches[pState->treeFaytMatchIndex];

    CustomTreeNode* p = match->parent;
    while (p)
    {
        pState->assetTree.ExpandNode(p, true);
        p = p->parent;
    }

    pState->assetTree.SelectNode(match);
    pState->assetTree.EnsureVisible(match);
    SetTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT, 5000, NULL);
    InvalidateRect(pState->hWnd, &pState->rcFaytDock, FALSE);
}

static void TreeFaytPrev(VisualStudioState* pState)
{
    if (!pState || pState->treeFaytMatches.empty()) return;
    pState->treeFaytMatchIndex = (pState->treeFaytMatchIndex - 1 + (int)pState->treeFaytMatches.size()) % (int)pState->treeFaytMatches.size();
    CustomTreeNode* match = pState->treeFaytMatches[pState->treeFaytMatchIndex];

    CustomTreeNode* p = match->parent;
    while (p)
    {
        pState->assetTree.ExpandNode(p, true);
        p = p->parent;
    }

    pState->assetTree.SelectNode(match);
    pState->assetTree.EnsureVisible(match);
    SetTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT, 5000, NULL);
    InvalidateRect(pState->hWnd, &pState->rcFaytDock, FALSE);
}

static void DrawFloatingTreeFaytPill(HDC hdc, VisualStudioState* pState)
{
    if (!pState || !pState->bTreeFaytActive) return;

    RECT rcDock = pState->rcFaytDock;
    int dockW = rcDock.right - rcDock.left;
    int dockH = rcDock.bottom - rcDock.top;
    if (dockW < 100 || dockH < 20) return;

    // 1. Dedicated tray background
    HBRUSH hbrDock = CreateSolidBrush(RGB(24, 24, 28));
    FillRect(hdc, &rcDock, hbrDock);
    DeleteObject(hbrDock);

    // 2. Dedicated top divider line
    HPEN hPenTop = CreatePen(PS_SOLID, 1, RGB(48, 48, 56));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenTop);
    MoveToEx(hdc, rcDock.left, rcDock.top, NULL);
    LineTo(hdc, rcDock.right, rcDock.top);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPenTop);

    // 3. Search Pill
    int marginX = 6;
    int marginY = 4;
    RECT rcPill = { rcDock.left + marginX, rcDock.top + marginY, rcDock.right - marginX, rcDock.bottom - marginY };
    pState->rcFaytPill = rcPill;

    COLORREF pillBg = RGB(34, 34, 40);
    COLORREF pillBorder = RGB(0, 150, 255);
    HBRUSH hbrPill = CreateSolidBrush(pillBg);
    HPEN hPenPill = CreatePen(PS_SOLID, 1, pillBorder);
    HGDIOBJ oldBr = SelectObject(hdc, hbrPill);
    HGDIOBJ oldPenP = SelectObject(hdc, hPenPill);

    RoundRect(hdc, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 6, 6);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPenP);
    DeleteObject(hbrPill);
    DeleteObject(hPenPill);

    int btnW = 18;
    int btnH = 18;
    int btnY = rcPill.top + ((rcPill.bottom - rcPill.top) - btnH) / 2;

    int rightEdge = rcPill.right - 4;
    pState->rcFaytCloseBtn = { rightEdge - btnW, btnY, rightEdge, btnY + btnH };
    rightEdge -= (btnW + 2);
    pState->rcFaytNextBtn = { rightEdge - btnW, btnY, rightEdge, btnY + btnH };
    rightEdge -= (btnW + 2);
    pState->rcFaytPrevBtn = { rightEdge - btnW, btnY, rightEdge, btnY + btnH };
    rightEdge -= 4;

    // Search Icon 🔍
    RECT rcIcon = { rcPill.left + 4, rcPill.top, rcPill.left + 22, rcPill.bottom };
    HFONT hIconFont = pState->hFontIconSmall ? pState->hFontIconSmall : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hIconFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 160, 255));
    DrawTextW(hdc, L"\xE721", -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    auto drawBtn = [&](const RECT& rcBtn, int btnId, const wchar_t* glyph) {
        bool isHover = (pState->treeFaytHoverBtn == btnId);
        if (isHover)
        {
            HBRUSH hHoverBr = CreateSolidBrush(RGB(55, 55, 68));
            HPEN hHoverPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 95));
            HGDIOBJ ob = SelectObject(hdc, hHoverBr);
            HGDIOBJ op = SelectObject(hdc, hHoverPen);
            RoundRect(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom, 4, 4);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hHoverBr);
            DeleteObject(hHoverPen);
        }
        SetTextColor(hdc, isHover ? RGB(255, 255, 255) : RGB(180, 180, 190));
        DrawTextW(hdc, glyph, -1, const_cast<RECT*>(&rcBtn), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    drawBtn(pState->rcFaytPrevBtn, 1, L"\xE70E");  // Chevron Up ▲
    drawBtn(pState->rcFaytNextBtn, 2, L"\xE70D");  // Chevron Down ▼
    drawBtn(pState->rcFaytCloseBtn, 3, L"\xE711"); // Close ✕

    int textLeft = rcPill.left + 24;
    int textRight = rightEdge;
    if (textRight > textLeft)
    {
        std::wstring matchBadge;
        COLORREF badgeCol = RGB(120, 180, 255);
        if (pState->treeFaytMatches.empty())
        {
            matchBadge = L"0";
            badgeCol = RGB(255, 110, 110);
        }
        else
        {
            matchBadge = std::to_wstring(pState->treeFaytMatchIndex + 1) + L"/" + std::to_wstring(pState->treeFaytMatches.size());
        }

        SelectObject(hdc, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SIZE badgeSize = { 0 };
        GetTextExtentPoint32W(hdc, matchBadge.c_str(), (int)matchBadge.length(), &badgeSize);

        RECT rcBadge = { textRight - badgeSize.cx - 2, rcPill.top, textRight, rcPill.bottom };
        SetTextColor(hdc, badgeCol);
        DrawTextW(hdc, matchBadge.c_str(), -1, &rcBadge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT rcQuery = { textLeft, rcPill.top, rcBadge.left - 4, rcPill.bottom };
        SetTextColor(hdc, pState->treeFaytQuery.empty() ? RGB(130, 130, 140) : RGB(245, 245, 245));
        std::wstring displayText = pState->treeFaytQuery.empty() ? L"Search..." : pState->treeFaytQuery;
        DrawTextW(hdc, displayText.c_str(), -1, &rcQuery, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, hOldFont);
}

static LRESULT CALLBACK TreeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    VisualStudioState* pState = (VisualStudioState*)dwRefData;
    if (!pState) return DefSubclassProc(hWnd, uMsg, wParam, lParam);

    switch (uMsg)
    {
    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hParent = pState->hWnd ? pState->hWnd : GetParent(hWnd);
        if (hParent && !IsZoomed(hParent))
        {
            RECT rcParent;
            GetWindowRect(hParent, &rcParent);
            int b = 8;
            if (pt.x < rcParent.left + b || pt.y >= rcParent.bottom - b)
            {
                return HTTRANSPARENT;
            }
        }
        RECT rcTree;
        GetWindowRect(hWnd, &rcTree);
        if (pt.x >= rcTree.right - 4)
        {
            return HTTRANSPARENT;
        }
        break;
    }

    case WM_CHAR:
    {
        wchar_t ch = (wchar_t)wParam;
        if (ch >= 32)
        {
            bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (!bCtrl && !bAlt)
            {
                if (!pState->bTreeFaytActive)
                {
                    StartTreeFayt(pState, ch);
                }
                else
                {
                    pState->treeFaytQuery += ch;
                    UpdateTreeFaytMatches(pState);
                    SetTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT, 5000, NULL);
                    InvalidateRect(pState->hWnd, &pState->rcFaytDock, FALSE);
                }
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (pState->bTreeFaytActive)
        {
            if (wParam == VK_ESCAPE)
            {
                CloseTreeFayt(pState);
                return 0;
            }
            else if (wParam == VK_BACK)
            {
                if (!pState->treeFaytQuery.empty())
                {
                    pState->treeFaytQuery.pop_back();
                }
                if (pState->treeFaytQuery.empty())
                {
                    CloseTreeFayt(pState);
                }
                else
                {
                    UpdateTreeFaytMatches(pState);
                    SetTimer(pState->hWnd, TIMER_TREE_FAYT_TIMEOUT, 5000, NULL);
                    InvalidateRect(pState->hWnd, &pState->rcFaytDock, FALSE);
                }
                return 0;
            }
            else if (wParam == VK_RETURN || wParam == VK_F3)
            {
                bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (bShift) TreeFaytPrev(pState);
                else TreeFaytNext(pState);
                return 0;
            }
        }
        else if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            StartTreeFayt(pState, 0);
            return 0;
        }
        else if (wParam == VK_F3)
        {
            if (pState->treeFaytQuery.empty() && !pState->searchQuery.empty())
            {
                pState->treeFaytQuery = pState->searchQuery;
            }
            if (!pState->treeFaytQuery.empty())
            {
                StartTreeFayt(pState, 0);
                bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (bShift) TreeFaytPrev(pState);
                else TreeFaytNext(pState);
                return 0;
            }
        }
        break;
    }

    case WM_DROPFILES:
    {
        HWND hStudio = GetParent(hWnd);
        if (hStudio)
        {
            SendMessage(hStudio, WM_DROPFILES, wParam, lParam);
        }
        return 0;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hWnd, TreeSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
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

static const char* g_ShapeVSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float  GlassTintAlpha;\n"
"    float3 Padding;\n"
"};\n"
"cbuffer CBBones : register(b1) {\n"
"    matrix BoneTransforms[256];\n"
"};\n"
"struct VS_INPUT {\n"
"    float3 pos : POSITION;\n"
"    float3 normal : NORMAL;\n"
"    float2 uv : TEXCOORD0;\n"
"    uint bone : BLENDINDICES;\n"
"};\n"
"struct PS_INPUT {\n"
"    float4 pos : SV_POSITION;\n"
"    float3 normal : NORMAL;\n"
"    float2 uv : TEXCOORD0;\n"
"};\n"
"PS_INPUT VSMain(VS_INPUT input) {\n"
"    PS_INPUT output;\n"
"    uint bIdx = (input.bone < 256) ? input.bone : 0;\n"
"    float4 worldPos = mul(float4(input.pos, 1.0f), BoneTransforms[bIdx]);\n"
"    output.pos = mul(worldPos, WorldViewProj);\n"
"    output.normal = mul(float4(input.normal, 0.0f), BoneTransforms[bIdx]).xyz;\n"
"    output.uv = input.uv;\n"
"    return output;\n"
"}\n";

static const char* g_ShapePSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float  GlassTintAlpha;\n"
"    float3 Padding;\n"
"};\n"
"struct PS_INPUT {\n"
"    float4 pos : SV_POSITION;\n"
"    float3 normal : NORMAL;\n"
"    float2 uv : TEXCOORD0;\n"
"};\n"
"Texture2D tex : register(t0);\n"
"SamplerState samLinear : register(s0);\n"
"float4 PSMain(PS_INPUT input) : SV_TARGET {\n"
"    float4 col = tex.Sample(samLinear, input.uv);\n"
"    if (col.a < AlphaCutoff) discard;\n"
"    float lenN = length(input.normal);\n"
"    float3 N = lenN > 0.001f ? (input.normal / lenN) : float3(0.0f, 1.0f, 0.0f);\n"
"    float3 L = normalize(-LightDir);\n"
"    float NdotL = max(0.0f, dot(N, L));\n"
"    float3 groundL = float3(0.0f, -1.0f, 0.0f);\n"
"    float groundBounce = max(0.0f, dot(N, groundL)) * 0.25f;\n"
"    float3 diffuse = col.rgb * (AmbientColor + NdotL * LightIntensity + groundBounce);\n"
"    return float4(diffuse, col.a);\n"
"}\n";

static const char* g_GridVSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float  GlassTintAlpha;\n"
"    float3 Padding;\n"
"};\n"
"struct VS_GRID_INPUT {\n"
"    float3 pos : POSITION;\n"
"    float4 color : COLOR0;\n"
"};\n"
"struct PS_GRID_INPUT {\n"
"    float4 pos : SV_POSITION;\n"
"    float4 color : COLOR0;\n"
"};\n"
"PS_GRID_INPUT VSGridMain(VS_GRID_INPUT input) {\n"
"    PS_GRID_INPUT output;\n"
"    output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n";

static const char* g_GridPSHLSL =
"struct PS_GRID_INPUT {\n"
"    float4 pos : SV_POSITION;\n"
"    float4 color : COLOR0;\n"
"};\n"
"float4 PSGridMain(PS_GRID_INPUT input) : SV_TARGET {\n"
"    return input.color;\n"
"}\n";

static std::wstring ExtractShapeFromStockFile(const std::wstring& stockFilePath)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, stockFilePath.c_str(), L"rb") != 0 || !fp) return L"";
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5000000) { fclose(fp); return L""; }
    std::vector<char> buf(sz + 1);
    fread(buf.data(), 1, sz, fp);
    fclose(fp);
    buf[sz] = '\0';

    std::string text;
    if ((uint8_t)buf[0] == 0xFF && (uint8_t)buf[1] == 0xFE)
    {
        const wchar_t* pw = (const wchar_t*)(buf.data() + 2);
        size_t wlen = (sz - 2) / sizeof(wchar_t);
        text.resize(wlen);
        for (size_t i = 0; i < wlen; ++i) text[i] = (pw[i] < 128) ? (char)pw[i] : ' ';
    }
    else
    {
        text.assign(buf.data(), sz);
    }

    // Search for "Shape ("
    size_t pos = 0;
    while (pos < text.length())
    {
        size_t sIdx = text.find("Shape", pos);
        if (sIdx == std::string::npos) sIdx = text.find("shape", pos);
        if (sIdx == std::string::npos) break;

        size_t pOpen = text.find('(', sIdx);
        if (pOpen != std::string::npos && pOpen - sIdx < 20)
        {
            size_t pClose = text.find(')', pOpen);
            if (pClose != std::string::npos)
            {
                std::string inner = text.substr(pOpen + 1, pClose - pOpen - 1);
                size_t q1 = inner.find('"');
                if (q1 != std::string::npos)
                {
                    size_t q2 = inner.find('"', q1 + 1);
                    if (q2 != std::string::npos) inner = inner.substr(q1 + 1, q2 - q1 - 1);
                }
                else
                {
                    size_t start = inner.find_first_not_of(" \t\r\n");
                    size_t end = inner.find_last_not_of(" \t\r\n");
                    if (start != std::string::npos && end != std::string::npos)
                        inner = inner.substr(start, end - start + 1);
                }
                if (!inner.empty())
                {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, inner.c_str(), -1, NULL, 0);
                    std::wstring wShape(wlen ? wlen - 1 : 0, 0);
                    if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, inner.c_str(), -1, &wShape[0], wlen);

                    wchar_t dir[MAX_PATH] = { 0 };
                    wcscpy_s(dir, stockFilePath.c_str());
                    PathRemoveFileSpecW(dir);
                    wchar_t fullPath[MAX_PATH] = { 0 };
                    PathCombineW(fullPath, dir, wShape.c_str());
                    return fullPath;
                }
            }
        }
        pos = sIdx + 5;
    }
    return L"";
}

static void CreateGroundGrid(VisualStudioState* pState)
{
    if (!pState || !pState->pD3DDevice) return;

    std::vector<GridVertex> gridVerts;
    float gridSize = 60.0f;
    float step = 1.0f;

    DirectX::XMFLOAT4 gridCol = { 0.20f, 0.20f, 0.22f, 0.6f };
    DirectX::XMFLOAT4 majorCol = { 0.32f, 0.32f, 0.36f, 0.8f };
    DirectX::XMFLOAT4 xCol = { 0.85f, 0.20f, 0.20f, 1.0f }; // Red +X
    DirectX::XMFLOAT4 zCol = { 0.20f, 0.50f, 0.95f, 1.0f }; // Blue +Z

    for (float x = -gridSize; x <= gridSize; x += step)
    {
        bool isMajor = (std::fmod(std::abs(x), 5.0f) < 0.01f);
        bool isAxis = (std::abs(x) < 0.01f);
        DirectX::XMFLOAT4 c = isAxis ? zCol : (isMajor ? majorCol : gridCol);

        gridVerts.push_back({ { x, 0.0f, -gridSize }, c });
        gridVerts.push_back({ { x, 0.0f,  gridSize }, c });
    }

    for (float z = -gridSize; z <= gridSize; z += step)
    {
        bool isMajor = (std::fmod(std::abs(z), 5.0f) < 0.01f);
        bool isAxis = (std::abs(z) < 0.01f);
        DirectX::XMFLOAT4 c = isAxis ? xCol : (isMajor ? majorCol : gridCol);

        gridVerts.push_back({ { -gridSize, 0.0f, z }, c });
        gridVerts.push_back({ {  gridSize, 0.0f, z }, c });
    }

    // Y Axis Indicator (Green)
    gridVerts.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.20f, 0.85f, 0.20f, 1.0f } });
    gridVerts.push_back({ { 0.0f, 3.0f, 0.0f }, { 0.20f, 0.85f, 0.20f, 1.0f } });

    pState->gridVertexCount = (uint32_t)gridVerts.size();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (UINT)(gridVerts.size() * sizeof(GridVertex));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = gridVerts.data();

    pState->pD3DDevice->CreateBuffer(&desc, &initData, &pState->pGridVB);
}

static void CleanupD3D11(VisualStudioState* pState)
{
    if (!pState) return;

    if (pState->pD3DContext)
    {
        pState->pD3DContext->ClearState();
        pState->pD3DContext->Flush();
    }

    pState->currentShape.ReleaseGPUBuffers();
    pState->pTextureLoader.reset();

    if (pState->pRenderTargetView) { pState->pRenderTargetView->Release(); pState->pRenderTargetView = nullptr; }
    if (pState->pDepthStencilView) { pState->pDepthStencilView->Release(); pState->pDepthStencilView = nullptr; }
    if (pState->pDepthStencilBuffer) { pState->pDepthStencilBuffer->Release(); pState->pDepthStencilBuffer = nullptr; }
    if (pState->pSwapChain) { pState->pSwapChain->Release(); pState->pSwapChain = nullptr; }

    if (pState->pRasterStateSolid) { pState->pRasterStateSolid->Release(); pState->pRasterStateSolid = nullptr; }
    if (pState->pRasterStateWireframe) { pState->pRasterStateWireframe->Release(); pState->pRasterStateWireframe = nullptr; }
    if (pState->pRasterStateDecal) { pState->pRasterStateDecal->Release(); pState->pRasterStateDecal = nullptr; }
    if (pState->pDepthStencilStateWrite) { pState->pDepthStencilStateWrite->Release(); pState->pDepthStencilStateWrite = nullptr; }
    if (pState->pDepthStencilStateReadOnly) { pState->pDepthStencilStateReadOnly->Release(); pState->pDepthStencilStateReadOnly = nullptr; }
    if (pState->pBlendStateOpaque) { pState->pBlendStateOpaque->Release(); pState->pBlendStateOpaque = nullptr; }
    if (pState->pBlendStateAlpha) { pState->pBlendStateAlpha->Release(); pState->pBlendStateAlpha = nullptr; }
    if (pState->pSamplerState) { pState->pSamplerState->Release(); pState->pSamplerState = nullptr; }

    if (pState->pShapeVS) { pState->pShapeVS->Release(); pState->pShapeVS = nullptr; }
    if (pState->pShapePS) { pState->pShapePS->Release(); pState->pShapePS = nullptr; }
    if (pState->pShapeLayout) { pState->pShapeLayout->Release(); pState->pShapeLayout = nullptr; }

    if (pState->pGridVS) { pState->pGridVS->Release(); pState->pGridVS = nullptr; }
    if (pState->pGridPS) { pState->pGridPS->Release(); pState->pGridPS = nullptr; }
    if (pState->pGridLayout) { pState->pGridLayout->Release(); pState->pGridLayout = nullptr; }
    if (pState->pGridVB) { pState->pGridVB->Release(); pState->pGridVB = nullptr; }

    if (pState->pConstantBuffer) { pState->pConstantBuffer->Release(); pState->pConstantBuffer = nullptr; }
    if (pState->pBoneConstantBuffer) { pState->pBoneConstantBuffer->Release(); pState->pBoneConstantBuffer = nullptr; }

    if (pState->pD3DContext) { pState->pD3DContext->Release(); pState->pD3DContext = nullptr; }
    if (pState->pD3DDevice) { pState->pD3DDevice->Release(); pState->pD3DDevice = nullptr; }
}

static void ResizeD3D11(VisualStudioState* pState, int width, int height)
{
    if (!pState || !pState->pD3DDevice || !pState->pSwapChain || width <= 0 || height <= 0) return;

    if (pState->pRenderTargetView) { pState->pRenderTargetView->Release(); pState->pRenderTargetView = nullptr; }
    if (pState->pDepthStencilView) { pState->pDepthStencilView->Release(); pState->pDepthStencilView = nullptr; }
    if (pState->pDepthStencilBuffer) { pState->pDepthStencilBuffer->Release(); pState->pDepthStencilBuffer = nullptr; }

    pState->pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(pState->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)))
    {
        pState->pD3DDevice->CreateRenderTargetView(pBackBuffer, NULL, &pState->pRenderTargetView);
        pBackBuffer->Release();
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (SUCCEEDED(pState->pD3DDevice->CreateTexture2D(&depthDesc, NULL, &pState->pDepthStencilBuffer)))
    {
        pState->pD3DDevice->CreateDepthStencilView(pState->pDepthStencilBuffer, NULL, &pState->pDepthStencilView);
    }
}

static bool InitD3D11(HWND hViewportWnd, VisualStudioState* pState)
{
    if (!hViewportWnd || !pState) return false;

    RECT rc;
    GetClientRect(hViewportWnd, &rc);
    int width = (std::max)(10, (int)(rc.right - rc.left));
    int height = (std::max)(10, (int)(rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hViewportWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &scd,
        &pState->pSwapChain,
        &pState->pD3DDevice,
        &featureLevel,
        &pState->pD3DContext
    );

    if (FAILED(hr)) return false;

    pState->pTextureLoader = std::make_unique<TextureLoader>(pState->pD3DDevice, pState->pD3DContext);

    ResizeD3D11(pState, width, height);

    // Rasterizer States (Back-face culling enabled so interior cab walls don't bleed onto exterior)
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    pState->pD3DDevice->CreateRasterizerState(&rd, &pState->pRasterStateSolid);

    rd.FillMode = D3D11_FILL_WIREFRAME;
    rd.CullMode = D3D11_CULL_NONE;
    pState->pD3DDevice->CreateRasterizerState(&rd, &pState->pRasterStateWireframe);

    // Decal Rasterizer State (Biased towards camera to eliminate z-fighting on co-planar trainboards/markings)
    D3D11_RASTERIZER_DESC rdDecal = {};
    rdDecal.FillMode = D3D11_FILL_SOLID;
    rdDecal.CullMode = D3D11_CULL_BACK;
    rdDecal.FrontCounterClockwise = FALSE;
    rdDecal.DepthClipEnable = TRUE;
    rdDecal.DepthBias = -50;
    rdDecal.SlopeScaledDepthBias = -0.5f;
    pState->pD3DDevice->CreateRasterizerState(&rdDecal, &pState->pRasterStateDecal);

    // Depth Stencil States (Write for Opaque/Cutout, Read-Only for Smooth Trans/Glass)
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    pState->pD3DDevice->CreateDepthStencilState(&dsd, &pState->pDepthStencilStateWrite);

    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    pState->pD3DDevice->CreateDepthStencilState(&dsd, &pState->pDepthStencilStateReadOnly);

    // Blend States
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    pState->pD3DDevice->CreateBlendState(&bd, &pState->pBlendStateOpaque);

    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    pState->pD3DDevice->CreateBlendState(&bd, &pState->pBlendStateAlpha);

    // Sampler State (16x Anisotropic Filtering for crystal-sharp textures at acute viewing angles)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    pState->pD3DDevice->CreateSamplerState(&sampDesc, &pState->pSamplerState);

    // Frame Constant Buffer
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBPerFrame);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&cbd, NULL, &pState->pConstantBuffer);

    // Bone Matrices Constant Buffer
    D3D11_BUFFER_DESC boneCbd = {};
    boneCbd.ByteWidth = sizeof(CBBones);
    boneCbd.Usage = D3D11_USAGE_DEFAULT;
    boneCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&boneCbd, NULL, &pState->pBoneConstantBuffer);

    // Compile Shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    if (SUCCEEDED(D3DCompile(g_ShapeVSHLSL, strlen(g_ShapeVSHLSL), NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &pState->pShapeVS);

        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT,        0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        pState->pD3DDevice->CreateInputLayout(layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &pState->pShapeLayout);
        vsBlob->Release();
    }

    if (SUCCEEDED(D3DCompile(g_ShapePSHLSL, strlen(g_ShapePSHLSL), NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &pState->pShapePS);
        psBlob->Release();
    }

    // Grid Shaders
    ID3DBlob* gvsBlob = nullptr;
    ID3DBlob* gpsBlob = nullptr;
    if (SUCCEEDED(D3DCompile(g_GridVSHLSL, strlen(g_GridVSHLSL), NULL, NULL, NULL, "VSGridMain", "vs_4_0", 0, 0, &gvsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreateVertexShader(gvsBlob->GetBufferPointer(), gvsBlob->GetBufferSize(), NULL, &pState->pGridVS);
        D3D11_INPUT_ELEMENT_DESC gridLayoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        pState->pD3DDevice->CreateInputLayout(gridLayoutDesc, _countof(gridLayoutDesc), gvsBlob->GetBufferPointer(), gvsBlob->GetBufferSize(), &pState->pGridLayout);
        gvsBlob->Release();
    }

    if (SUCCEEDED(D3DCompile(g_GridPSHLSL, strlen(g_GridPSHLSL), NULL, NULL, NULL, "PSGridMain", "ps_4_0", 0, 0, &gpsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(gpsBlob->GetBufferPointer(), gpsBlob->GetBufferSize(), NULL, &pState->pGridPS);
        gpsBlob->Release();
    }

    CreateGroundGrid(pState);
    return true;
}

static void RenderViewport(VisualStudioState* pState)
{
    if (!pState || !pState->pD3DContext || !pState->pSwapChain || !pState->pRenderTargetView) return;

    RECT rc;
    GetClientRect(pState->hViewportWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    pState->pD3DContext->RSSetViewports(1, &vp);

    // Clear background (solid dark DirectX studio viewport)
    float clearColor[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
    pState->pD3DContext->ClearRenderTargetView(pState->pRenderTargetView, clearColor);
    if (pState->pDepthStencilView)
    {
        pState->pD3DContext->ClearDepthStencilView(pState->pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    pState->pD3DContext->OMSetRenderTargets(1, &pState->pRenderTargetView, pState->pDepthStencilView);
    pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);

    // Camera LookAtLH
    float radYaw = DirectX::XMConvertToRadians(pState->camYaw);
    float radPitch = DirectX::XMConvertToRadians(pState->camPitch);
    float eyeX = pState->camTarget.x + pState->camDistance * std::cos(radPitch) * std::sin(radYaw);
    float eyeY = pState->camTarget.y + pState->camDistance * std::sin(radPitch);
    float eyeZ = pState->camTarget.z + pState->camDistance * std::cos(radPitch) * std::cos(radYaw);

    DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
    DirectX::XMVECTOR targetPos = DirectX::XMVectorSet(pState->camTarget.x, pState->camTarget.y, pState->camTarget.z, 1.0f);
    DirectX::XMVECTOR upDir = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX viewMat = DirectX::XMMatrixLookAtLH(eyePos, targetPos, upDir);
    float fov = DirectX::XMConvertToRadians(45.0f);
    float aspect = (float)width / (float)height;
    float nearZ = 0.1f;
    float farZ = 1500.0f;
    DirectX::XMMATRIX projMat = DirectX::XMMatrixPerspectiveFovLH(fov, aspect, nearZ, farZ);
    DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(viewMat, projMat);

    // Update Constant Buffer
    CBPerFrame cb = {};
    cb.WorldViewProj = DirectX::XMMatrixTranspose(viewProj);
    cb.World = DirectX::XMMatrixTranspose(DirectX::XMMatrixIdentity());
    cb.LightDir = { -0.577f, -0.577f, 0.577f };
    cb.LightIntensity = 0.85f;
    cb.AmbientColor = { 0.40f, 0.40f, 0.42f };
    cb.AlphaCutoff = 0.5f;

    pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);
    pState->pD3DContext->VSSetConstantBuffers(0, 1, &pState->pConstantBuffer);
    pState->pD3DContext->PSSetConstantBuffers(0, 1, &pState->pConstantBuffer);
    pState->pD3DContext->PSSetSamplers(0, 1, &pState->pSamplerState);

    // 1. Render Ground Grid
    if (pState->showGroundGrid && pState->pGridVB && pState->pGridVS && pState->pGridPS && pState->pGridLayout)
    {
        pState->pD3DContext->RSSetState(pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);

        pState->pD3DContext->IASetInputLayout(pState->pGridLayout);
        UINT stride = sizeof(GridVertex);
        UINT offset = 0;
        pState->pD3DContext->IASetVertexBuffers(0, 1, &pState->pGridVB, &stride, &offset);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        pState->pD3DContext->VSSetShader(pState->pGridVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pGridPS, NULL, 0);

        pState->pD3DContext->Draw(pState->gridVertexCount, 0);
    }

    // 2. Render Single Shape (Tab 0: 3D Shape Viewer)
    if (pState->activeTab == 0 && pState->currentShape.isValid && pState->currentShape.pVertexBuffer && pState->currentShape.pIndexBuffer)
    {
        // Update Bone Constant Buffer for Skinning / Animation
        CBBones bonesCB = {};
        for (int b = 0; b < 256; ++b) {
            bonesCB.BoneTransforms[b] = DirectX::XMMatrixTranspose(DirectX::XMMatrixIdentity());
        }

        if (!pState->currentShape.boneMatrices.empty())
        {
            std::vector<DirectX::XMFLOAT4X4> evaluatedWorldMatrices;
            ShapeAnimator::ComputeAnimatedMatrices(
                pState->currentShape,
                pState->animCurrentFrame,
                pState->activeAnimFilterType,
                pState->wheelSpinAngle,
                pState->enableWheelSpin,
                evaluatedWorldMatrices
            );

            for (size_t b = 0; b < evaluatedWorldMatrices.size() && b < 256; ++b) {
                DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&evaluatedWorldMatrices[b]);
                bonesCB.BoneTransforms[b] = DirectX::XMMatrixTranspose(m);
            }
        }

        if (pState->pBoneConstantBuffer)
        {
            pState->pD3DContext->UpdateSubresource(pState->pBoneConstantBuffer, 0, NULL, &bonesCB, 0, 0);
            pState->pD3DContext->VSSetConstantBuffers(1, 1, &pState->pBoneConstantBuffer);
        }

        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->IASetInputLayout(pState->pShapeLayout);
        UINT stride = sizeof(GPUVertex);
        UINT offset = 0;
        pState->pD3DContext->IASetVertexBuffers(0, 1, &pState->currentShape.pVertexBuffer, &stride, &offset);
        pState->pD3DContext->IASetIndexBuffer(pState->currentShape.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        pState->pD3DContext->VSSetShader(pState->pShapeVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pShapePS, NULL, 0);

        // Pass 1: Opaque SubMeshes (Depth Write Enabled)
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.0f;
        cb.GlassTintAlpha = 0.0f;
        pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);
        for (const auto& subMesh : pState->currentShape.subMeshes)
        {
            if (!subMesh.isTransparent)
            {
                ID3D11ShaderResourceView* srv = subMesh.pSRV ? subMesh.pSRV : pState->pTextureLoader->GetDefaultTexture();
                pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                pState->pD3DContext->DrawIndexed(subMesh.indexCount, subMesh.startIndex, 0);
            }
        }

        // Pass 2: Alpha-Test Cutout SubMeshes (Open Rails: Depth Write Enabled, BlendState = Alpha, AlphaCutoff = 10/255)
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.0392f;
        cb.GlassTintAlpha = 0.0f;
        pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);
        for (const auto& subMesh : pState->currentShape.subMeshes)
        {
            if (subMesh.isTransparent && subMesh.isAlphaTest)
            {
                ID3D11ShaderResourceView* srv = subMesh.pSRV ? subMesh.pSRV : pState->pTextureLoader->GetDefaultTexture();
                pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                pState->pD3DContext->DrawIndexed(subMesh.indexCount, subMesh.startIndex, 0);
            }
        }
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);

        // Pass 3: Smooth Alpha-Blended Decals, Glass & Transparent SubMeshes (Depth Read-Only, Blend Alpha)
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateReadOnly, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.005f;
        cb.GlassTintAlpha = 0.0f;
        pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);
        for (const auto& subMesh : pState->currentShape.subMeshes)
        {
            if (subMesh.isTransparent && !subMesh.isAlphaTest)
            {
                ID3D11ShaderResourceView* srv = subMesh.pSRV ? subMesh.pSRV : pState->pTextureLoader->GetDefaultTexture();
                pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                pState->pD3DContext->DrawIndexed(subMesh.indexCount, subMesh.startIndex, 0);
            }
        }
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
    }
    // 3. Render Composite Rolling Stock (Tab 1: Rolling Stock Inspector - Main Body + ORTS Freight Animations)
    else if (pState->activeTab == 1 && pState->compositeStock.isValid)
    {
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->IASetInputLayout(pState->pShapeLayout);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pState->pD3DContext->VSSetShader(pState->pShapeVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pShapePS, NULL, 0);

        // Helper lambda to setup bone constants and draw submeshes for a specific pass filter
        auto RenderSubShapePass = [&](int passMode) // 0 = Opaque, 1 = AlphaTest, 2 = Smooth Trans
        {
            for (const auto& sub : pState->compositeStock.subShapes)
            {
                if (!sub->isLoaded || !sub->shape.isValid || !sub->shape.pVertexBuffer || !sub->shape.pIndexBuffer) continue;

                // Check if this subshape contains any submeshes matching this pass
                bool hasMatching = false;
                for (const auto& sm : sub->shape.subMeshes)
                {
                    if (passMode == 0 && !sm.isTransparent) { hasMatching = true; break; }
                    if (passMode == 1 && (sm.isTransparent && sm.isAlphaTest)) { hasMatching = true; break; }
                    if (passMode == 2 && (sm.isTransparent && !sm.isAlphaTest)) { hasMatching = true; break; }
                }
                if (!hasMatching) continue;

                // Bone Transforms for this SubShape
                CBBones bonesCB = {};
                for (int b = 0; b < 256; ++b) {
                    bonesCB.BoneTransforms[b] = DirectX::XMMatrixTranspose(DirectX::XMMatrixIdentity());
                }

                if (!sub->shape.boneMatrices.empty())
                {
                    std::vector<DirectX::XMFLOAT4X4> evalWorld;
                    ShapeAnimator::ComputeAnimatedMatrices(
                        sub->shape,
                        pState->animCurrentFrame,
                        pState->activeAnimFilterType,
                        pState->wheelSpinAngle,
                        pState->enableWheelSpin,
                        evalWorld
                    );

                    for (size_t b = 0; b < evalWorld.size() && b < 256; ++b) {
                        DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&evalWorld[b]);
                        bonesCB.BoneTransforms[b] = DirectX::XMMatrixTranspose(m);
                    }
                }

                if (pState->pBoneConstantBuffer)
                {
                    pState->pD3DContext->UpdateSubresource(pState->pBoneConstantBuffer, 0, NULL, &bonesCB, 0, 0);
                    pState->pD3DContext->VSSetConstantBuffers(1, 1, &pState->pBoneConstantBuffer);
                }

                // Transform matrix for this sub-shape
                DirectX::XMMATRIX subWorld = sub->localMatrix;
                cb.World = DirectX::XMMatrixTranspose(subWorld);
                cb.WorldViewProj = DirectX::XMMatrixTranspose(DirectX::XMMatrixMultiply(subWorld, viewProj));
                pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);

                UINT stride = sizeof(GPUVertex);
                UINT offset = 0;
                pState->pD3DContext->IASetVertexBuffers(0, 1, &sub->shape.pVertexBuffer, &stride, &offset);
                pState->pD3DContext->IASetIndexBuffer(sub->shape.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

                for (const auto& sm : sub->shape.subMeshes)
                {
                    bool match = (passMode == 0 && !sm.isTransparent) ||
                                 (passMode == 1 && sm.isTransparent && sm.isAlphaTest) ||
                                 (passMode == 2 && sm.isTransparent && !sm.isAlphaTest);
                    if (match)
                    {
                        ID3D11ShaderResourceView* srv = sm.pSRV ? sm.pSRV : (sub->pTextureLoader ? sub->pTextureLoader->GetDefaultTexture() : pState->pTextureLoader->GetDefaultTexture());
                        pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                        pState->pD3DContext->DrawIndexed(sm.indexCount, sm.startIndex, 0);
                    }
                }
            }
        };

        // Pass 1: Opaque geometry across ALL sub-shapes (Coach body, seats, trainboards, fans, chassis)
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.0f;
        cb.GlassTintAlpha = 0.0f;
        RenderSubShapePass(0);

        // Pass 2: Alpha-test cutout geometry across ALL sub-shapes (Open Rails: body shells, grilles, railings, cutouts, decals)
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.0392f;
        cb.GlassTintAlpha = 0.0f;
        RenderSubShapePass(1);
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);

        // Pass 3: Smooth transparent decals, glass & markings across ALL sub-shapes (Depth Read-Only, Blend Alpha)
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateReadOnly, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
        cb.AlphaCutoff = 0.005f;
        cb.GlassTintAlpha = 0.0f;
        RenderSubShapePass(2);
        pState->pD3DContext->RSSetState(pState->showWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
    }

    pState->pSwapChain->Present(1, 0);
}

static std::wstring FormatNumberWithCommas(uint64_t val)
{
    std::wstring s = std::to_wstring(val);
    int insertPosition = (int)s.length() - 3;
    while (insertPosition > 0) {
        s.insert(insertPosition, L",");
        insertPosition -= 3;
    }
    return s;
}

struct AsyncShapeLoadResult {
    uint64_t taskId = 0;
    std::wstring filePath;
    std::unique_ptr<ParsedShape> pParsedShape;
    double parseTimeMs = 0.0;
    bool success = false;
};

struct AsyncStockLoadResult {
    uint64_t taskId = 0;
    std::wstring filePath;
    std::unique_ptr<CompositeStockUnit> pUnit;
    double parseTimeMs = 0.0;
    bool success = false;
};

static void LoadShapeIntoViewportAsync(VisualStudioState* pState, const std::wstring& path)
{
    if (!pState || path.empty()) return;

    const wchar_t* ext = PathFindExtensionW(path.c_str());
    bool isStock = (pState->activeTab == 1) || (ext && (_wcsicmp(ext, L".wag") == 0 || _wcsicmp(ext, L".eng") == 0));

    LOG_INFO_W(L"[3D-Studio] Initiating asynchronous model load: '%ls' (isStockVehicle: %ls)", path.c_str(), isStock ? L"YES" : L"NO");

    if (isStock)
    {
        if (!PathFileExistsW(path.c_str()))
        {
            LOG_WARN_W(L"[3D-Studio] File does not exist on disk: %ls", path.c_str());
            return;
        }

        uint64_t taskId = ++pState->activeLoadTaskId;
        pState->isLoadingShape = true;
        pState->loadingShapeName = std::filesystem::path(path).filename().wstring();

        if (pState->hWnd && IsWindow(pState->hWnd))
        {
            InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
        }
        if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
        {
            InvalidateRect(pState->hViewportWnd, NULL, FALSE);
        }

        HWND hWndTarget = pState->hWnd;
        std::wstring trainsetBase = pState->basePath;
        std::wstring stockPath = path;

        std::thread([hWndTarget, taskId, stockPath, trainsetBase]() {
            auto pResult = std::make_unique<AsyncStockLoadResult>();
            pResult->taskId = taskId;
            pResult->filePath = stockPath;
            pResult->pUnit = std::make_unique<CompositeStockUnit>();

            LARGE_INTEGER tStart, tEnd, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&tStart);

            bool ok = CompositeStockLoader::LoadCompositeStockCPU(stockPath, trainsetBase, *pResult->pUnit);

            QueryPerformanceCounter(&tEnd);
            pResult->parseTimeMs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / (double)freq.QuadPart;
            pResult->success = ok;

            LOG_INFO_W(L"[3D-Studio] Completed stock load in %.2f ms (success: %ls): '%ls'",
                pResult->parseTimeMs, ok ? L"YES" : L"NO", stockPath.c_str());

            // 1. Dispatch Stock Geometry IMMEDIATELY for Frame-0 Instant Render!
            if (IsWindow(hWndTarget)) {
                PostMessageW(hWndTarget, WM_VS_STOCK_LOADED_ASYNC, (WPARAM)taskId, (LPARAM)pResult.release());
            }

            // 2. Pre-decode textures in background and trigger hot-swap!
            if (ok)
            {
                // Gather folders & images
                std::wstring stockDir = std::filesystem::path(stockPath).parent_path().wstring();
                // Wait briefly or run predecode
                if (IsWindow(hWndTarget)) {
                    PostMessageW(hWndTarget, WM_VS_TEXTURES_READY, (WPARAM)taskId, 0);
                }
            }
        }).detach();
        return;
    }

    // Tab 0: Single .s shape file
    std::wstring actualShapePath = path;
    if (!PathFileExistsW(actualShapePath.c_str()))
    {
        LOG_WARN_W(L"[3D-Studio] Shape file does not exist on disk: %ls", actualShapePath.c_str());
        return;
    }

    uint64_t taskId = ++pState->activeLoadTaskId;
    pState->isLoadingShape = true;
    pState->loadingShapeName = std::filesystem::path(actualShapePath).filename().wstring();

    if (pState->hWnd && IsWindow(pState->hWnd))
    {
        InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
    }
    if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
    {
        InvalidateRect(pState->hViewportWnd, NULL, FALSE);
    }

    HWND hWndTarget = pState->hWnd;
    std::thread([hWndTarget, taskId, actualShapePath]() {
        auto pResult = std::make_unique<AsyncShapeLoadResult>();
        pResult->taskId = taskId;
        pResult->filePath = actualShapePath;
        pResult->pParsedShape = std::make_unique<ParsedShape>();

        LARGE_INTEGER tStart, tEnd, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&tStart);

        bool ok = ShapeReader::ParseShapeFile(actualShapePath, *pResult->pParsedShape);

        QueryPerformanceCounter(&tEnd);
        pResult->parseTimeMs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / (double)freq.QuadPart;
        pResult->success = ok;

        LOG_INFO_W(L"[3D-Studio] Completed single shape load in %.2f ms (success: %ls): '%ls'",
            pResult->parseTimeMs, ok ? L"YES" : L"NO", actualShapePath.c_str());

        std::wstring shapeDir = pResult->pParsedShape ? pResult->pParsedShape->shapeDir : L"";
        std::vector<std::wstring> rawImages = pResult->pParsedShape ? pResult->pParsedShape->rawImageNames : std::vector<std::wstring>{};

        // 1. Dispatch Shape Geometry IMMEDIATELY for Frame-0 Instant Render!
        if (IsWindow(hWndTarget)) {
            PostMessageW(hWndTarget, WM_VS_SHAPE_LOADED_ASYNC, (WPARAM)taskId, (LPARAM)pResult.release());
        }

        // 2. Pre-decode textures in background and trigger hot-swap as soon as they finish!
        if (ok && !rawImages.empty())
        {
            TextureLoader::PredecodeTexturesCPU(shapeDir, rawImages);
            if (IsWindow(hWndTarget)) {
                PostMessageW(hWndTarget, WM_VS_TEXTURES_READY, (WPARAM)taskId, 0);
            }
        }
    }).detach();
}

static void HandleStudioFileLoad(VisualStudioState* pState, const std::wstring& filePath)
{
    if (!pState || filePath.empty()) return;

    if (!PathFileExistsW(filePath.c_str()))
    {
        ShowModernMessageBox(pState->hWnd, (L"File not found:\n\n" + filePath).c_str(), L"3D Visual Studio", MB_OK | MB_ICONWARNING);
        return;
    }

    std::filesystem::path fp(filePath);
    std::wstring ext = fp.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == L".s")
    {
        pState->activeTab = 0;
        pState->shapeFilePath = filePath;
        pState->shapeDirectory = fp.parent_path().wstring();
        pState->currentDirectory = pState->shapeDirectory;
        SaveStudioDirectory(0, pState->shapeDirectory);
        PushNavHistory(pState, pState->shapeDirectory);

        if (pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            CustomTitleBar_SetActiveTab(pState->hTitleBar, 0);
        }
        if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
        {
            UpdateToolbarForTab(pState);
            NavToolbar_SetPath(pState->hNavToolbar, pState->shapeDirectory.c_str());
        }
        PopulateAssetTree(pState);
        LoadShapeIntoViewportAsync(pState, filePath);
        InvalidateRect(pState->hWnd, NULL, FALSE);
    }
    else if (ext == L".wag" || ext == L".eng")
    {
        pState->activeTab = 1;
        pState->stockFilePath = filePath;
        pState->stockDirectory = fp.parent_path().wstring();
        pState->currentDirectory = pState->stockDirectory;
        SaveStudioDirectory(1, pState->stockDirectory);
        PushNavHistory(pState, pState->stockDirectory);

        if (pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            CustomTitleBar_SetActiveTab(pState->hTitleBar, 1);
        }
        if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
        {
            UpdateToolbarForTab(pState);
            NavToolbar_SetPath(pState->hNavToolbar, pState->stockDirectory.c_str());
        }
        PopulateAssetTree(pState);
        LoadShapeIntoViewportAsync(pState, filePath);
        InvalidateRect(pState->hWnd, NULL, FALSE);
    }
    else
    {
        ShowModernMessageBox(pState->hWnd,
            L"Unsupported file format.\n\nPlease drag and drop a 3D Shape file (*.s) for the Shape Viewer, or a Rolling Stock file (*.eng, *.wag) for the Stock Inspector.",
            L"3D Visual Studio - File Format Notice",
            MB_OK | MB_ICONINFORMATION);
    }
}

static bool HasAnyNavKeyPressed(const VisualStudioState* pState)
{
    if (!pState) return false;
    return pState->keyW || pState->keyA || pState->keyS || pState->keyD ||
           pState->keyUp || pState->keyDown || pState->keyLeft || pState->keyRight ||
           pState->keyQ || pState->keyE || pState->keyZ || pState->keyX ||
           pState->keySpace || pState->keyC;
}

static void ProcessSmoothCameraNavigation(HWND hWnd, VisualStudioState* pState, float dt)
{
    if (!pState) return;

    // Movement speed proportional to camera distance (units per second)
    float baseMoveSpeed = (std::max)(1.5f, pState->camDistance * 1.2f);
    float baseRotSpeed = 85.0f; // degrees per second

    float speedMultiplier = 1.0f;
    if (GetKeyState(VK_SHIFT) & 0x8000)
    {
        speedMultiplier = 2.4f; // Balanced smooth boost
    }
    else if (GetKeyState(VK_CONTROL) & 0x8000)
    {
        speedMultiplier = 0.35f; // Precision mode
    }

    float moveStep = baseMoveSpeed * speedMultiplier * dt;
    float rotStep = baseRotSpeed * speedMultiplier * dt;
    float radYaw = DirectX::XMConvertToRadians(pState->camYaw);

    // Forward & Right vectors on ground plane
    float fwdX = -std::sin(radYaw);
    float fwdZ = -std::cos(radYaw);
    float rightX = std::cos(radYaw);
    float rightZ = -std::sin(radYaw);

    bool changed = false;

    // 1. Position Translation: WASD + Vertical
    if (pState->keyW)
    {
        pState->camTarget.x += fwdX * moveStep;
        pState->camTarget.z += fwdZ * moveStep;
        changed = true;
    }
    if (pState->keyS)
    {
        pState->camTarget.x -= fwdX * moveStep;
        pState->camTarget.z -= fwdZ * moveStep;
        changed = true;
    }
    if (pState->keyA)
    {
        pState->camTarget.x -= rightX * moveStep;
        pState->camTarget.z -= rightZ * moveStep;
        changed = true;
    }
    if (pState->keyD)
    {
        pState->camTarget.x += rightX * moveStep;
        pState->camTarget.z += rightZ * moveStep;
        changed = true;
    }
    if (pState->keyE || pState->keySpace)
    {
        pState->camTarget.y += moveStep;
        changed = true;
    }
    if (pState->keyQ || pState->keyC)
    {
        pState->camTarget.y -= moveStep;
        changed = true;
    }

    // 2. Camera Rotation / Orbit: Arrow Keys
    if (pState->keyLeft)
    {
        pState->camYaw -= rotStep;
        changed = true;
    }
    if (pState->keyRight)
    {
        pState->camYaw += rotStep;
        changed = true;
    }
    if (pState->keyUp)
    {
        pState->camPitch += rotStep;
        if (pState->camPitch > 89.0f) pState->camPitch = 89.0f;
        changed = true;
    }
    if (pState->keyDown)
    {
        pState->camPitch -= rotStep;
        if (pState->camPitch < -89.0f) pState->camPitch = -89.0f;
        changed = true;
    }

    // 3. Zooming (Z / X)
    if (pState->keyZ)
    {
        pState->camDistance = (std::max)(0.5f, pState->camDistance - moveStep * 1.5f);
        changed = true;
    }
    if (pState->keyX)
    {
        pState->camDistance = (std::min)(500.0f, pState->camDistance + moveStep * 1.5f);
        changed = true;
    }

    if (changed)
    {
        RenderViewport(pState);
    }
}

// =============================================================
// Viewport Window Procedure (DirectX 11 3D Canvas)
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
        DragAcceptFiles(hWnd, TRUE);
        InitD3D11(hWnd, pState);
        return 0;
    }

    case WM_DROPFILES:
    {
        HWND hParent = GetParent(hWnd);
        if (hParent)
        {
            SendMessage(hParent, WM_DROPFILES, wParam, lParam);
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            ResizeD3D11(pState, w, h);
            RenderViewport(pState);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Handled in Direct3D 11

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (pState)
        {
            RenderViewport(pState);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_SETFOCUS:
        return 0;

    case WM_KILLFOCUS:
    {
        if (pState)
        {
            pState->keyW = pState->keyA = pState->keyS = pState->keyD = false;
            pState->keyUp = pState->keyDown = pState->keyLeft = pState->keyRight = false;
            pState->keyQ = pState->keyE = pState->keyZ = pState->keyX = false;
            pState->keySpace = pState->keyC = false;
            if (pState->isKeyNavActive)
            {
                KillTimer(hWnd, TIMER_VIEWPORT_NAV);
                pState->isKeyNavActive = false;
            }
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (pState && wParam == TIMER_VIEWPORT_NAV)
        {
            uint64_t now = GetTickCount64();
            float dt = (pState->lastNavTick > 0) ? (float)(now - pState->lastNavTick) / 1000.0f : 0.016f;
            if (dt > 0.05f) dt = 0.016f;
            if (dt < 0.001f) dt = 0.001f;
            pState->lastNavTick = now;

            ProcessSmoothCameraNavigation(hWnd, pState, dt);
            return 0;
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (pState)
        {
            bool handled = true;
            switch (wParam)
            {
            case 'W': pState->keyW = true; break;
            case 'S': pState->keyS = true; break;
            case 'A': pState->keyA = true; break;
            case 'D': pState->keyD = true; break;

            case VK_UP:    pState->keyUp = true; break;
            case VK_DOWN:  pState->keyDown = true; break;
            case VK_LEFT:  pState->keyLeft = true; break;
            case VK_RIGHT: pState->keyRight = true; break;

            case 'E':
            case VK_PRIOR:
            case VK_SPACE:
                pState->keyE = true;
                break;

            case 'Q':
            case VK_NEXT:
            case 'C':
                pState->keyQ = true;
                break;

            case 'Z':
            case VK_OEM_PLUS:
            case VK_ADD:
                pState->keyZ = true;
                break;

            case 'X':
            case VK_OEM_MINUS:
            case VK_SUBTRACT:
                pState->keyX = true;
                break;

            case VK_HOME:
            case 'R':
                pState->camTarget = pState->currentShape.center;
                pState->camDistance = (pState->currentShape.radius > 0.1f) ? (pState->currentShape.radius * 2.2f) : 20.0f;
                if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;
                pState->camYaw = -45.0f;
                pState->camPitch = 25.0f;
                RenderViewport(pState);
                return 0;

            case 'P':
            {
                pState->isAnimPlaying = !pState->isAnimPlaying;
                for (auto& btn : pState->ribbonButtons) {
                    if (btn.id == 9) {
                        btn.isActive = pState->isAnimPlaying;
                        btn.iconGlyph = pState->isAnimPlaying ? L"\xE769" : L"\xE768";
                        btn.label = pState->isAnimPlaying ? L"Pause Anim" : L"Play Anim";
                    }
                }
                if (pState->isAnimPlaying) {
                    pState->lastAnimTick = GetTickCount64();
                    SetTimer(pState->hWnd, TIMER_ANIM_PLAYBACK, 16, NULL);
                } else {
                    KillTimer(pState->hWnd, TIMER_ANIM_PLAYBACK);
                }
                InvalidateRect(pState->hWnd, &pState->rcRibbon, FALSE);
                InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
                return 0;
            }

            case VK_OEM_4: // '[' key - step frame backward
            {
                if (pState->currentShape.isValid && pState->currentShape.animation.hasAnimation) {
                    pState->animCurrentFrame -= 1.0f;
                    if (pState->animCurrentFrame < 0.0f) pState->animCurrentFrame += pState->currentShape.animation.frameCount;
                    RenderViewport(pState);
                    InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
                }
                return 0;
            }

            case VK_OEM_6: // ']' key - step frame forward
            {
                if (pState->currentShape.isValid && pState->currentShape.animation.hasAnimation) {
                    pState->animCurrentFrame += 1.0f;
                    if (pState->animCurrentFrame > pState->currentShape.animation.frameCount) pState->animCurrentFrame = std::fmod(pState->animCurrentFrame, pState->currentShape.animation.frameCount);
                    RenderViewport(pState);
                    InvalidateRect(pState->hWnd, &pState->rcFooter, FALSE);
                }
                return 0;
            }

            default:
                handled = false;
                break;
            }

            if (handled)
            {
                if (!pState->isKeyNavActive)
                {
                    pState->isKeyNavActive = true;
                    pState->lastNavTick = GetTickCount64();
                    SetTimer(hWnd, TIMER_VIEWPORT_NAV, 16, NULL);
                }
                ProcessSmoothCameraNavigation(hWnd, pState, 0.016f);
                return 0;
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_KEYUP:
    {
        if (pState)
        {
            switch (wParam)
            {
            case 'W': pState->keyW = false; break;
            case 'S': pState->keyS = false; break;
            case 'A': pState->keyA = false; break;
            case 'D': pState->keyD = false; break;

            case VK_UP:    pState->keyUp = false; break;
            case VK_DOWN:  pState->keyDown = false; break;
            case VK_LEFT:  pState->keyLeft = false; break;
            case VK_RIGHT: pState->keyRight = false; break;

            case 'E':
            case VK_PRIOR:
            case VK_SPACE:
                pState->keyE = false;
                break;

            case 'Q':
            case VK_NEXT:
            case 'C':
                pState->keyQ = false;
                break;

            case 'Z':
            case VK_OEM_PLUS:
            case VK_ADD:
                pState->keyZ = false;
                break;

            case 'X':
            case VK_OEM_MINUS:
            case VK_SUBTRACT:
                pState->keyX = false;
                break;
            }

            if (!HasAnyNavKeyPressed(pState) && pState->isKeyNavActive)
            {
                KillTimer(hWnd, TIMER_VIEWPORT_NAV);
                pState->isKeyNavActive = false;
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (pState)
        {
            SetFocus(hWnd);
            SetCapture(hWnd);
            pState->isOrbiting = true;
            pState->lastMousePt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (pState && pState->isOrbiting)
        {
            pState->isOrbiting = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        if (pState)
        {
            SetFocus(hWnd);
            SetCapture(hWnd);
            pState->isPanning = true;
            pState->lastMousePt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        }
        return 0;
    }

    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        if (pState && pState->isPanning)
        {
            pState->isPanning = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (pState)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int dx = pt.x - pState->lastMousePt.x;
            int dy = pt.y - pState->lastMousePt.y;
            pState->lastMousePt = pt;

            if (pState->isOrbiting)
            {
                pState->camYaw += dx * 0.4f;
                pState->camPitch += dy * 0.4f;
                if (pState->camPitch > 89.0f) pState->camPitch = 89.0f;
                if (pState->camPitch < -89.0f) pState->camPitch = -89.0f;
                RenderViewport(pState);
            }
            else if (pState->isPanning)
            {
                float panFactor = pState->camDistance * 0.0015f;
                float radYaw = DirectX::XMConvertToRadians(pState->camYaw);
                pState->camTarget.x -= (std::cos(radYaw) * dx) * panFactor;
                pState->camTarget.z += (std::sin(radYaw) * dx) * panFactor;
                pState->camTarget.y += dy * panFactor;
                RenderViewport(pState);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (pState)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float zoomStep = (pState->camDistance * 0.1f);
            if (zoomStep < 0.2f) zoomStep = 0.2f;
            if (delta > 0) pState->camDistance -= zoomStep;
            else pState->camDistance += zoomStep;
            if (pState->camDistance < 0.5f) pState->camDistance = 0.5f;
            if (pState->camDistance > 500.0f) pState->camDistance = 500.0f;
            RenderViewport(pState);
        }
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hParent = pState ? pState->hWnd : GetParent(hWnd);
        if (hParent && !IsZoomed(hParent))
        {
            RECT rcParent;
            GetWindowRect(hParent, &rcParent);
            int b = 8;
            if (pt.x >= rcParent.right - b || pt.y >= rcParent.bottom - b)
            {
                return HTTRANSPARENT;
            }
        }
        RECT rcVP;
        GetWindowRect(hWnd, &rcVP);
        if (pt.x <= rcVP.left + 4)
        {
            return HTTRANSPARENT;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_DESTROY:
    {
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        return 0;
    }

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

// =============================================================
// Visual 3D Studio Main Frame Window Procedure
// =============================================================
static LRESULT CALLBACK Visual3DStudioProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualStudioState* pState = (VisualStudioState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pState = (VisualStudioState*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hWnd = hWnd;
        return TRUE;
    }

    case WM_CREATE:
    {
        DragAcceptFiles(hWnd, TRUE);

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
        int initW = (rcClient.right > 0) ? rcClient.right : 1280;
        int initH = (rcClient.bottom > 0) ? rcClient.bottom : 820;

        std::vector<TitleBarTabItem> studioTabs = {
            { L"\xE8E5", L"3D SHAPE VIEWER (.S)" },
            { L"\xE7C0", L"ROLLING STOCK INSPECTOR (.WAG / .ENG)" }
        };

        pState->hTitleBar = CreateCustomTitleBarEx(
            hWnd,
            GetModuleHandleW(NULL),
            0, 0, initW, 66,
            IDC_VS_TITLEBAR,
            L"3D Visual Studio - TrainSim Consist Builder",
            studioTabs
        );

        if (pState->hTitleBar)
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
            SendMessage(pState->hTitleBar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(initW, 66));
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

        // Tier 2: Create NavToolbar seamlessly below CustomTitleBar (Y = 66..110)
        pState->hNavToolbar = CreateNavToolbar(hWnd, GetModuleHandleW(NULL), 0, 66, initW, 44, IDC_VS_NAVTOOLBAR);
        if (pState->hNavToolbar)
        {
            NavToolbar_SetDarkMode(pState->hNavToolbar, TRUE);
            UpdateToolbarForTab(pState);
        }

        // Tier 4 Left: Create Child CustomTreeView sidebar (Y = 148)
        pState->sidebarWidth = LoadSavedStudioSidebarWidth();
        int curSidebarW = pState->sidebarWidth;
        int splitterW = 6;
        pState->assetTree.Create(hWnd, 0, 148, curSidebarW, initH - 148 - 26, IDC_VS_TREEVIEW);
        pState->assetTree.SetDarkMode(true);
        pState->assetTree.SetFont(pState->hFontMain);
        DragAcceptFiles(pState->assetTree.GetHWND(), TRUE);

        // Selection Callback - dynamic folder expansion and file selection
        pState->assetTree.SetSelectionCallback([pState](CustomTreeNode* node) {
            if (!node) return;

            if (node->isFolder)
            {
                if (!node->tag.empty())
                {
                    PopulateFolderChildren(pState, node, node->tag);
                    pState->assetTree.ExpandNode(node, true);
                    pState->currentDirectory = node->tag;
                    if (pState->activeTab == 0) pState->shapeDirectory = node->tag;
                    else pState->stockDirectory = node->tag;

                    PushNavHistory(pState, node->tag);
                    NavToolbar_SetPath(pState->hNavToolbar, node->tag.c_str());
                    SaveStudioDirectory(pState->activeTab, node->tag);
                }
            }
            else
            {
                if (pState->activeTab == 0)
                {
                    pState->shapeFilePath = node->tag;
                }
                else
                {
                    pState->stockFilePath = node->tag;
                }
                LoadShapeIntoViewportAsync(pState, node->tag);
            }
        });

        // Double-Click Callback - toggle folder expand and navigate
        pState->assetTree.SetDoubleClickCallback([pState](CustomTreeNode* node) {
            if (!node) return;

            if (node->isFolder)
            {
                if (!node->tag.empty())
                {
                    PopulateFolderChildren(pState, node, node->tag);
                    pState->assetTree.ToggleExpand(node);
                    pState->currentDirectory = node->tag;
                    if (pState->activeTab == 0) pState->shapeDirectory = node->tag;
                    else pState->stockDirectory = node->tag;

                    PushNavHistory(pState, node->tag);
                    NavToolbar_SetPath(pState->hNavToolbar, node->tag.c_str());
                    SaveStudioDirectory(pState->activeTab, node->tag);
                }
            }
            else
            {
                if (pState->activeTab == 0)
                {
                    pState->shapeFilePath = node->tag;
                }
                else
                {
                    pState->stockFilePath = node->tag;
                }
                LoadShapeIntoViewportAsync(pState, node->tag);
            }
        });

        // Attach subclass to TreeView for Floating FAYT Search Box
        SetWindowSubclass(pState->assetTree.GetHWND(), TreeSubclassProc, SUBCLASS_TREE_ID, (DWORD_PTR)pState);

        // Tier 4 Right: Create Child Viewport Window
        WNDCLASSEXW wcv = { 0 };
        wcv.cbSize = sizeof(WNDCLASSEXW);
        wcv.style = CS_DBLCLKS;
        wcv.lpfnWndProc = ViewportProc;
        wcv.hInstance = GetModuleHandleW(NULL);
        wcv.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcv.hbrBackground = NULL;
        wcv.lpszClassName = L"TSCB_3DStudioViewport";
        RegisterClassExW(&wcv);

        pState->hViewportWnd = CreateWindowExW(
            0,
            L"TSCB_3DStudioViewport",
            NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            curSidebarW + splitterW, 148, initW - (curSidebarW + splitterW), initH - 148 - 26,
            hWnd,
            NULL,
            GetModuleHandleW(NULL),
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

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t droppedPath[MAX_PATH] = { 0 };
        UINT numFiles = DragQueryFileW(hDrop, 0, droppedPath, MAX_PATH);
        DragFinish(hDrop);
        if (numFiles > 0 && pState)
        {
            HandleStudioFileLoad(pState, droppedPath);
        }
        return 0;
    }

    case WM_VS_LOAD_FILE_EXTERNAL:
    {
        const wchar_t* pFilePath = (const wchar_t*)lParam;
        if (pState && pFilePath && pFilePath[0] != L'\0')
        {
            HandleStudioFileLoad(pState, pFilePath);
        }
        return 0;
    }

    case WM_VS_DEFERRED_LOAD:
    {
        if (pState)
        {
            PopulateAssetTree(pState);
            const std::wstring& targetPath = (pState->activeTab == 0) ? pState->shapeFilePath : pState->stockFilePath;
            if (!targetPath.empty())
            {
                LoadShapeIntoViewportAsync(pState, targetPath);
            }
            InvalidateRect(hWnd, NULL, FALSE);
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

                PushNavHistory(pState, pState->currentDirectory);
                SaveStudioDirectory(pState->activeTab, pState->currentDirectory);
                PopulateAssetTree(pState);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_NAVTOOLBAR_SEARCH:
    {
        if (pState && pState->hNavToolbar)
        {
            wchar_t szSearch[256] = { 0 };
            NavToolbar_GetSearchQuery(pState->hNavToolbar, szSearch, 256);
            PerformTreeSearch(pState, szSearch);
        }
        return 0;
    }

    case WM_NAVTOOLBAR_ACTION:
    {
        if (pState)
        {
            int actionId = (int)wParam;
            switch (actionId)
            {
            case NAV_ACTION_UP:
            {
                if (!pState->currentDirectory.empty())
                {
                    std::filesystem::path cur(pState->currentDirectory);
                    if (cur.has_parent_path())
                    {
                        std::wstring parentPath = cur.parent_path().wstring();
                        pState->currentDirectory = parentPath;
                        if (pState->activeTab == 0) pState->shapeDirectory = parentPath;
                        else pState->stockDirectory = parentPath;

                        PushNavHistory(pState, parentPath);
                        NavToolbar_SetPath(pState->hNavToolbar, parentPath.c_str());
                        SaveStudioDirectory(pState->activeTab, parentPath);
                        PopulateAssetTree(pState);
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
                break;
            }
            case NAV_ACTION_REFRESH:
            {
                PopulateAssetTree(pState);
                InvalidateRect(hWnd, NULL, FALSE);
                break;
            }
            case NAV_ACTION_BACK:
            {
                if (pState->navHistoryIndex > 0)
                {
                    pState->navHistoryIndex--;
                    std::wstring prevPath = pState->navHistory[pState->navHistoryIndex];
                    pState->currentDirectory = prevPath;
                    if (pState->activeTab == 0) pState->shapeDirectory = prevPath;
                    else pState->stockDirectory = prevPath;

                    NavToolbar_SetPath(pState->hNavToolbar, prevPath.c_str());
                    SaveStudioDirectory(pState->activeTab, prevPath);
                    PopulateAssetTree(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                break;
            }
            case NAV_ACTION_FORWARD:
            {
                if (pState->navHistoryIndex + 1 < (int)pState->navHistory.size())
                {
                    pState->navHistoryIndex++;
                    std::wstring nextPath = pState->navHistory[pState->navHistoryIndex];
                    pState->currentDirectory = nextPath;
                    if (pState->activeTab == 0) pState->shapeDirectory = nextPath;
                    else pState->stockDirectory = nextPath;

                    NavToolbar_SetPath(pState->hNavToolbar, nextPath.c_str());
                    SaveStudioDirectory(pState->activeTab, nextPath);
                    PopulateAssetTree(pState);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                break;
            }
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
            int b = 8;
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
                LRESULT hit = SendMessageW(pState->hTitleBar, WM_NCHITTEST, 0, lParam);
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
            UpdateWindow(hWnd);
            if (pState->hTitleBar && IsWindow(pState->hTitleBar))
            {
                InvalidateRect(pState->hTitleBar, NULL, FALSE);
                UpdateWindow(pState->hTitleBar);
            }
            if (pState->hNavToolbar && IsWindow(pState->hNavToolbar))
            {
                InvalidateRect(pState->hNavToolbar, NULL, FALSE);
                UpdateWindow(pState->hNavToolbar);
            }
            if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
            {
                UpdateWindow(pState->hViewportWnd);
            }
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
        if (pState && pState->hViewportWnd && IsWindow(pState->hViewportWnd))
        {
            HWND hFocus = GetFocus();
            if (hFocus != pState->assetTree.GetHWND())
            {
                SendMessage(pState->hViewportWnd, WM_KEYDOWN, wParam, lParam);
                return 0;
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_KEYUP:
    {
        if (pState && pState->hViewportWnd && IsWindow(pState->hViewportWnd))
        {
            SendMessage(pState->hViewportWnd, WM_KEYUP, wParam, lParam);
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    case WM_VS_SHAPE_LOADED_ASYNC:
    {
        uint64_t taskId = (uint64_t)wParam;
        AsyncShapeLoadResult* pResult = (AsyncShapeLoadResult*)lParam;
        if (!pResult) return 0;

        if (pState && taskId == pState->activeLoadTaskId)
        {
            LARGE_INTEGER tStart, tEnd, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&tStart);

            pState->isLoadingShape = false;
            if (pResult->success && pResult->pParsedShape && !pResult->pParsedShape->vertices.empty())
            {
                pState->currentShape.ReleaseGPUBuffers();
                pState->currentShape = std::move(*pResult->pParsedShape);

                // Create Direct3D 11 GPU resources (Vertex Buffer, Index Buffer, Textures) on D3D thread
                ShapeReader::CreateGPUBuffers(pState->pD3DDevice, pState->pTextureLoader.get(), pState->currentShape);

                QueryPerformanceCounter(&tEnd);
                double gpuTimeMs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / (double)freq.QuadPart;
                pState->shapeLoadTimeMs = pResult->parseTimeMs + gpuTimeMs;

                pState->camTarget = pState->currentShape.center;
                pState->camDistance = (pState->currentShape.radius > 0.1f) ? (pState->currentShape.radius * 2.2f) : 20.0f;
                if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;

                pState->animCurrentFrame = 0.0f;
                pState->wheelSpinAngle = 0.0f;
                pState->animPingPongDir = 1.0f;
                pState->activeAnimFilterType = 0;
                pState->activeAnimLabel = L"Animations \x25BE";

                pState->isAnimPlaying = false;

                for (auto& btn : pState->ribbonButtons) {
                    if (btn.id == 11) btn.label = L"Animations \x25BE";
                    if (btn.id == 9) {
                        btn.label = L"Play Anim";
                        btn.iconGlyph = L"\xE768";
                        btn.isActive = false;
                    }
                }

                KillTimer(hWnd, TIMER_ANIM_PLAYBACK);
            }
            else
            {
                pState->shapeLoadTimeMs = 0.0;
            }

            if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
            {
                InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            }
            InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
            InvalidateRect(hWnd, &pState->rcFooter, FALSE);
        }

        delete pResult;
        return 0;
    }

    case WM_VS_STOCK_LOADED_ASYNC:
    {
        uint64_t taskId = (uint64_t)wParam;
        AsyncStockLoadResult* pResult = (AsyncStockLoadResult*)lParam;
        if (!pResult) return 0;

        if (pState && taskId == pState->activeLoadTaskId)
        {
            LARGE_INTEGER tStart, tEnd, freq;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&tStart);

            pState->isLoadingShape = false;
            if (pResult->success && pResult->pUnit && !pResult->pUnit->subShapes.empty())
            {
                CompositeStockLoader::ClearCompositeStock(pState->compositeStock);
                pState->compositeStock = std::move(*pResult->pUnit);

                // Create Direct3D 11 GPU resources (Vertex Buffer, Index Buffer, Textures) on D3D thread
                CompositeStockLoader::FinalizeGPUResources(pState->pD3DDevice, pState->pD3DContext, pState->compositeStock, pState->pTextureLoader.get());

                QueryPerformanceCounter(&tEnd);
                double gpuTimeMs = (double)(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / (double)freq.QuadPart;
                pState->shapeLoadTimeMs = pResult->parseTimeMs + gpuTimeMs;

                pState->camTarget = pState->compositeStock.center;
                pState->camDistance = (pState->compositeStock.radius > 0.1f) ? (pState->compositeStock.radius * 2.2f) : 20.0f;
                if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;

                pState->animCurrentFrame = 0.0f;
                pState->wheelSpinAngle = 0.0f;
                pState->animPingPongDir = 1.0f;
                pState->activeAnimFilterType = 0;
                pState->activeAnimLabel = L"Animations \x25BE";

                pState->isAnimPlaying = false;

                for (auto& btn : pState->ribbonButtons) {
                    if (btn.id == 11) btn.label = L"Animations \x25BE";
                    if (btn.id == 9) {
                        btn.label = L"Play Anim";
                        btn.iconGlyph = L"\xE768";
                        btn.isActive = false;
                    }
                }

                KillTimer(hWnd, TIMER_ANIM_PLAYBACK);
            }
            else
            {
                pState->shapeLoadTimeMs = 0.0;
            }

            if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
            {
                InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            }
            InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
            InvalidateRect(hWnd, &pState->rcFooter, FALSE);
        }

        delete pResult;
        return 0;
    }

    case WM_VS_TEXTURES_READY:
    {
        uint64_t taskId = (uint64_t)wParam;
        if (pState && taskId == pState->activeLoadTaskId)
        {
            if (pState->activeTab == 0 && pState->currentShape.isValid)
            {
                ShapeReader::HotSwapTextures(pState->pTextureLoader.get(), pState->currentShape);
            }
            else if (pState->activeTab == 1 && pState->compositeStock.isValid)
            {
                CompositeStockLoader::HotSwapTextures(pState->compositeStock, pState->pTextureLoader.get());
            }

            if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
            {
                InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (pState && wParam == TIMER_ANIM_PLAYBACK)
        {
            bool hasValidModel = (pState->activeTab == 0 && pState->currentShape.isValid) ||
                                 (pState->activeTab == 1 && pState->compositeStock.isValid);

            if (pState->isAnimPlaying && hasValidModel)
            {
                uint64_t now = GetTickCount64();
                float dt = (pState->lastAnimTick > 0) ? (float)(now - pState->lastAnimTick) / 1000.0f : 0.016f;
                if (dt > 0.05f) dt = 0.016f;
                if (dt < 0.001f) dt = 0.001f;
                pState->lastAnimTick = now;

                bool hasSimWheels = (pState->activeTab == 0)
                    ? pState->currentShape.animation.hasSimulatedWheels
                    : pState->compositeStock.hasSimulatedWheels;

                bool hasAnim = (pState->activeTab == 0)
                    ? pState->currentShape.animation.hasAnimation
                    : pState->compositeStock.hasAnimation;

                float maxFrames = (pState->activeTab == 0)
                    ? pState->currentShape.animation.frameCount
                    : pState->compositeStock.maxAnimationFrames;

                float baseFps = (pState->activeTab == 0)
                    ? pState->currentShape.animation.frameRate
                    : pState->compositeStock.animationFrameRate;

                // 1. Advance simulated wheel rotation
                if (pState->enableWheelSpin && hasSimWheels)
                {
                    pState->wheelSpinAngle += pState->wheelSpinSpeed * pState->animSpeed * dt;
                    if (pState->wheelSpinAngle > DirectX::XM_2PI)
                    {
                        pState->wheelSpinAngle = std::fmod(pState->wheelSpinAngle, DirectX::XM_2PI);
                    }
                }

                // 2. Advance keyframe tracks
                if (hasAnim && maxFrames > 0.0f)
                {
                    if (maxFrames <= 32.0f)
                    {
                        baseFps = maxFrames / 2.5f;
                    }

                    if (pState->isPingPongMode)
                    {
                        pState->animCurrentFrame += pState->animPingPongDir * baseFps * pState->animSpeed * dt;
                        if (pState->animCurrentFrame >= maxFrames)
                        {
                            pState->animCurrentFrame = maxFrames;
                            pState->animPingPongDir = -1.0f;
                        }
                        else if (pState->animCurrentFrame <= 0.0f)
                        {
                            pState->animCurrentFrame = 0.0f;
                            pState->animPingPongDir = 1.0f;
                        }
                    }
                    else
                    {
                        pState->animCurrentFrame += baseFps * pState->animSpeed * dt;
                        if (pState->animCurrentFrame > maxFrames)
                        {
                            pState->animCurrentFrame = std::fmod(pState->animCurrentFrame, maxFrames);
                        }
                    }
                }

                RenderViewport(pState);
                InvalidateRect(hWnd, &pState->rcFooter, FALSE);
            }
            return 0;
        }
        if (pState && wParam == TIMER_TREE_FAYT_TIMEOUT)
        {
            CloseTreeFayt(pState);
            return 0;
        }
        break;
    }

    case WM_SETCURSOR:
    {
        if (pState)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (pState->isDraggingSplitter || PtInRect(&pState->rcSplitter, pt))
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&pState->rcSplitter, pt))
        {
            SetCapture(hWnd);
            pState->isDraggingSplitter = true;
            pState->dragSplitterStartX = pt.x;
            pState->dragSidebarStartW = pState->sidebarWidth;
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return 0;
        }

        if (pState->bTreeFaytActive && PtInRect(&pState->rcFaytDock, pt))
        {
            if (PtInRect(&pState->rcFaytPrevBtn, pt))
            {
                TreeFaytPrev(pState);
            }
            else if (PtInRect(&pState->rcFaytNextBtn, pt))
            {
                TreeFaytNext(pState);
            }
            else if (PtInRect(&pState->rcFaytCloseBtn, pt))
            {
                CloseTreeFayt(pState);
            }
            return 0;
        }

        for (auto& btn : pState->ribbonButtons)
        {
            if (PtInRect(&btn.rc, pt))
            {
                btn.isPressed = true;
                InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
                return 0; // Consumed — ribbon button hit
            }
        }
        break; // Not consumed — let DefWindowProc handle (needed for HTCAPTION drag)
    }

    case WM_LBUTTONUP:
    {
        if (!pState) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (pState->isDraggingSplitter)
        {
            pState->isDraggingSplitter = false;
            ReleaseCapture();
            SaveStudioSidebarWidth(pState->sidebarWidth);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

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

                    if (btn.id == 1) // Reset View
                    {
                        pState->camYaw = -45.0f;
                        pState->camPitch = 25.0f;
                        if (pState->activeTab == 1 && pState->compositeStock.isValid)
                        {
                            pState->camTarget = pState->compositeStock.center;
                            pState->camDistance = (pState->compositeStock.radius > 0.1f) ? (pState->compositeStock.radius * 2.2f) : 20.0f;
                        }
                        else
                        {
                            pState->camTarget = pState->currentShape.isValid ? pState->currentShape.center : DirectX::XMFLOAT3(0.0f, 1.5f, 0.0f);
                            pState->camDistance = (pState->currentShape.radius > 0.1f) ? (pState->currentShape.radius * 2.2f) : 20.0f;
                        }
                        if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 2) // Isometric
                    {
                        pState->camYaw = -45.0f;
                        pState->camPitch = 35.264f;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 3) // Front
                    {
                        pState->camYaw = 0.0f;
                        pState->camPitch = 0.0f;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 4) // Side
                    {
                        pState->camYaw = -90.0f;
                        pState->camPitch = 0.0f;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 5) // Top
                    {
                        pState->camYaw = 0.0f;
                        pState->camPitch = 89.0f;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 6) // Wireframe
                    {
                        pState->showWireframe = btn.isActive;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 8) // Ground Grid
                    {
                        pState->showGroundGrid = btn.isActive;
                        RenderViewport(pState);
                    }
                    else if (btn.id == 9) // Play / Pause Animation
                    {
                        pState->isAnimPlaying = !pState->isAnimPlaying;
                        btn.isActive = pState->isAnimPlaying;
                        btn.iconGlyph = pState->isAnimPlaying ? L"\xE769" : L"\xE768";
                        btn.label = pState->isAnimPlaying ? L"Pause Anim" : L"Play Anim";

                        if (pState->isAnimPlaying)
                        {
                            pState->lastAnimTick = GetTickCount64();
                            SetTimer(hWnd, TIMER_ANIM_PLAYBACK, 16, NULL);
                        }
                        else
                        {
                            KillTimer(hWnd, TIMER_ANIM_PLAYBACK);
                        }
                        InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
                        InvalidateRect(hWnd, &pState->rcFooter, FALSE);
                    }
                    else if (btn.id == 10) // Restart Animation
                    {
                        pState->animCurrentFrame = 0.0f;
                        pState->wheelSpinAngle = 0.0f;
                        pState->animPingPongDir = 1.0f;
                        RenderViewport(pState);
                        InvalidateRect(hWnd, &pState->rcFooter, FALSE);
                    }
                    else if (btn.id == 11) // Animations Dropdown Menu
                    {
                        const ShapeAnimation* pAnim = nullptr;
                        if (pState->activeTab == 0 && pState->currentShape.isValid)
                        {
                            pAnim = &pState->currentShape.animation;
                        }
                        else if (pState->activeTab == 1 && pState->compositeStock.isValid)
                        {
                            for (const auto& sub : pState->compositeStock.subShapes)
                            {
                                if (sub->isLoaded && sub->shape.isValid && (sub->shape.animation.hasAnimation || sub->shape.animation.hasSimulatedWheels))
                                {
                                    pAnim = &sub->shape.animation;
                                    break;
                                }
                            }
                            if (!pAnim && !pState->compositeStock.subShapes.empty())
                            {
                                pAnim = &pState->compositeStock.subShapes[0]->shape.animation;
                            }
                        }

                        if (pAnim)
                        {
                            const auto& anim = *pAnim;
                            std::vector<DropDownItem> items;

                            // 1. Keyframed Animations Section
                            items.push_back(DropDownItem::Header(0, L"\xE768", L"Keyframed Tracks"));
                            items.push_back(DropDownItem::Action(1, L"\xE768", L"All Keyframe Tracks", anim.hasAnimation ? (std::to_wstring(anim.animNodes.size()) + L" nodes") : L"No keyframes", pState->activeAnimFilterType == 0));

                            if (anim.countPanto > 0) {
                                items.push_back(DropDownItem::Action(2, L"\xE7AF", L"Pantographs", std::to_wstring(anim.countPanto) + L" nodes", pState->activeAnimFilterType == 1));
                            }
                            if (anim.countDoor > 0) {
                                items.push_back(DropDownItem::Action(3, L"\xE737", L"Doors, Mirrors & Steps", std::to_wstring(anim.countDoor) + L" nodes", pState->activeAnimFilterType == 2));
                            }
                            if (anim.countWiper > 0) {
                                items.push_back(DropDownItem::Action(4, L"\xE74E", L"Wipers", std::to_wstring(anim.countWiper) + L" nodes", pState->activeAnimFilterType == 3));
                            }
                            if (anim.countWheel > 0) {
                                items.push_back(DropDownItem::Action(5, L"\xE7F4", L"Wheels & Running Gear (Keyframed)", std::to_wstring(anim.countWheel) + L" nodes", pState->activeAnimFilterType == 4));
                            }
                            if (anim.countFan > 0) {
                                items.push_back(DropDownItem::Action(6, L"\xE774", L"Cooling Fans & HVAC", std::to_wstring(anim.countFan) + L" nodes", pState->activeAnimFilterType == 5));
                            }
                            if (anim.countDriver > 0) {
                                items.push_back(DropDownItem::Action(7, L"\xE77B", L"Driver & Crew Rigs", std::to_wstring(anim.countDriver) + L" nodes", pState->activeAnimFilterType == 6));
                            }
                            if (anim.countDisplay > 0) {
                                items.push_back(DropDownItem::Action(8, L"\xE7B7", L"Digi Boards & Displays", std::to_wstring(anim.countDisplay) + L" nodes", pState->activeAnimFilterType == 7));
                            }
                            if (anim.countCustom > 0) {
                                items.push_back(DropDownItem::Action(9, L"\xE71E", L"Custom / Other Parts", std::to_wstring(anim.countCustom) + L" nodes", pState->activeAnimFilterType == 8));
                            }

                            bool hasIndividual = false;
                            for (size_t idx = 0; idx < anim.animNodes.size(); ++idx) {
                                if (anim.animNodes[idx].hasAnim) {
                                    if (!hasIndividual) {
                                        items.push_back(DropDownItem::Header(0, L"\xE81E", L"Individual Keyframe Tracks"));
                                        hasIndividual = true;
                                    }
                                    const auto& tr = anim.animNodes[idx];
                                    int wlen = MultiByteToWideChar(CP_UTF8, 0, tr.name.c_str(), -1, NULL, 0);
                                    std::wstring wNodeName(wlen ? wlen - 1 : 0, 0);
                                    if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, tr.name.c_str(), -1, &wNodeName[0], wlen);

                                    std::wstring keyCountStr = std::to_wstring(tr.posKeys.size() + tr.rotKeys.size()) + L" keys";
                                    int itemId = 100 + (int)idx;
                                    items.push_back(DropDownItem::Action(itemId, L"\xE729", wNodeName, keyCountStr, pState->activeAnimFilterType == itemId));
                                }
                            }

                            // 2. Simulated Procedural Wheels Section
                            if (anim.hasSimulatedWheels) {
                                items.push_back(DropDownItem::Header(0, L"\xE7F4", L"🎡 Simulated Wheel Roll (Procedural)"));
                                items.push_back(DropDownItem::Action(20, L"\xE7F4", L"All Wheels (Simulated Roll)", std::to_wstring(anim.simulatedWheels.size()) + L" wheel nodes", pState->activeAnimFilterType == 20));

                                for (size_t wIdx = 0; wIdx < anim.simulatedWheels.size(); ++wIdx) {
                                    const auto& wn = anim.simulatedWheels[wIdx];
                                    int wlen = MultiByteToWideChar(CP_UTF8, 0, wn.name.c_str(), -1, NULL, 0);
                                    std::wstring wWheelName(wlen ? wlen - 1 : 0, 0);
                                    if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, wn.name.c_str(), -1, &wWheelName[0], wlen);

                                    int itemId = 200 + (int)wIdx;
                                    items.push_back(DropDownItem::Action(itemId, L"\xE729", wWheelName, L"Pitch Roll", pState->activeAnimFilterType == itemId));
                                }

                                items.push_back(DropDownItem::Action(21, pState->enableWheelSpin ? L"\xE73A" : L"\xE73B", 
                                    pState->enableWheelSpin ? L"Simulated Wheels: Enabled \x2714" : L"Simulated Wheels: Disabled", 
                                    pState->enableWheelSpin ? L"Rotating" : L"Stationary", false));
                            }

                            // 3. Playback Speed & Modes Section
                            items.push_back(DropDownItem::Header(0, L"\xE713", L"Playback Speed & Modes"));
                            items.push_back(DropDownItem::Action(30, L"\xE768", L"Speed: 0.25x (Quarter Speed)", L"", pState->animSpeed == 0.25f));
                            items.push_back(DropDownItem::Action(31, L"\xE768", L"Speed: 0.50x (Half Speed)", L"", pState->animSpeed == 0.5f));
                            items.push_back(DropDownItem::Action(32, L"\xE768", L"Speed: 1.00x (Normal Speed)", L"", pState->animSpeed == 1.0f));
                            items.push_back(DropDownItem::Action(33, L"\xE768", L"Speed: 1.50x (Fast)", L"", pState->animSpeed == 1.5f));
                            items.push_back(DropDownItem::Action(34, L"\xE768", L"Speed: 2.00x (Double Speed)", L"", pState->animSpeed == 2.0f));
                            items.push_back(DropDownItem::Action(35, pState->isPingPongMode ? L"\xE895" : L"\xE777", 
                                pState->isPingPongMode ? L"Ping-Pong Loop: ON" : L"Ping-Pong Loop: OFF", 
                                pState->isPingPongMode ? L"Forward-Reverse" : L"Repeat", pState->isPingPongMode));

                            int curSelId = pState->activeAnimFilterType;
                            if (curSelId >= 0 && curSelId <= 8) curSelId += 1;

                            int selected = CustomDropDownMenu::ShowSingleSelect(hWnd, btn.rc, items, curSelId, 260);
                            if (selected > 0)
                            {
                                if (selected == 1) { pState->activeAnimFilterType = 0; pState->activeAnimLabel = L"Anim: All \x25BE"; }
                                else if (selected == 2) { pState->activeAnimFilterType = 1; pState->activeAnimLabel = L"Anim: Pantos \x25BE"; }
                                else if (selected == 3) { pState->activeAnimFilterType = 2; pState->activeAnimLabel = L"Anim: Doors \x25BE"; }
                                else if (selected == 4) { pState->activeAnimFilterType = 3; pState->activeAnimLabel = L"Anim: Wipers \x25BE"; }
                                else if (selected == 5) { pState->activeAnimFilterType = 4; pState->activeAnimLabel = L"Anim: Wheels \x25BE"; }
                                else if (selected == 6) { pState->activeAnimFilterType = 5; pState->activeAnimLabel = L"Anim: Fans \x25BE"; }
                                else if (selected == 7) { pState->activeAnimFilterType = 6; pState->activeAnimLabel = L"Anim: Driver \x25BE"; }
                                else if (selected == 8) { pState->activeAnimFilterType = 7; pState->activeAnimLabel = L"Anim: Displays \x25BE"; }
                                else if (selected == 9) { pState->activeAnimFilterType = 8; pState->activeAnimLabel = L"Anim: Other \x25BE"; }
                                else if (selected == 20) { pState->activeAnimFilterType = 20; pState->activeAnimLabel = L"Wheels: All \x25BE"; }
                                else if (selected == 21) { pState->enableWheelSpin = !pState->enableWheelSpin; }
                                else if (selected == 30) { pState->animSpeed = 0.25f; }
                                else if (selected == 31) { pState->animSpeed = 0.5f; }
                                else if (selected == 32) { pState->animSpeed = 1.0f; }
                                else if (selected == 33) { pState->animSpeed = 1.5f; }
                                else if (selected == 34) { pState->animSpeed = 2.0f; }
                                else if (selected == 35) { pState->isPingPongMode = !pState->isPingPongMode; }
                                else if (selected >= 100 && selected < 200)
                                {
                                    pState->activeAnimFilterType = selected;
                                    int nIdx = selected - 100;
                                    if (nIdx >= 0 && (size_t)nIdx < anim.animNodes.size()) {
                                        int wlen = MultiByteToWideChar(CP_UTF8, 0, anim.animNodes[nIdx].name.c_str(), -1, NULL, 0);
                                        std::wstring wnn(wlen ? wlen - 1 : 0, 0);
                                        if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, anim.animNodes[nIdx].name.c_str(), -1, &wnn[0], wlen);
                                        if (wnn.length() > 10) wnn = wnn.substr(0, 9) + L"..";
                                        pState->activeAnimLabel = L"Anim: " + wnn + L" \x25BE";
                                    }
                                }
                                else if (selected >= 200)
                                {
                                    pState->activeAnimFilterType = selected;
                                    int wIdx = selected - 200;
                                    if (wIdx >= 0 && (size_t)wIdx < anim.simulatedWheels.size()) {
                                        int wlen = MultiByteToWideChar(CP_UTF8, 0, anim.simulatedWheels[wIdx].name.c_str(), -1, NULL, 0);
                                        std::wstring wnn(wlen ? wlen - 1 : 0, 0);
                                        if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, anim.simulatedWheels[wIdx].name.c_str(), -1, &wnn[0], wlen);
                                        if (wnn.length() > 10) wnn = wnn.substr(0, 9) + L"..";
                                        pState->activeAnimLabel = L"Wheel: " + wnn + L" \x25BE";
                                    }
                                }

                                for (auto& rb : pState->ribbonButtons) {
                                    if (rb.id == 11) rb.label = pState->activeAnimLabel.c_str();
                                }
                                RenderViewport(pState);
                                InvalidateRect(hWnd, &pState->rcRibbon, FALSE);
                                InvalidateRect(hWnd, &pState->rcFooter, FALSE);
                            }
                        }
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

        if (pState->isDraggingSplitter)
        {
            int deltaX = pt.x - pState->dragSplitterStartX;
            int newW = pState->dragSidebarStartW + deltaX;
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int maxW = (std::max<int>)(140, (int)rcClient.right - 300);
            if (newW < 140) newW = 140;
            if (newW > maxW) newW = maxW;

            if (newW != pState->sidebarWidth)
            {
                pState->sidebarWidth = newW;
                RecalculateViewerLayout(pState, rcClient.right, rcClient.bottom);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return 0;
        }

        if (PtInRect(&pState->rcSplitter, pt))
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        }

        if (pState->bTreeFaytActive)
        {
            int newHover = 0;
            if (PtInRect(&pState->rcFaytPrevBtn, pt)) newHover = 1;
            else if (PtInRect(&pState->rcFaytNextBtn, pt)) newHover = 2;
            else if (PtInRect(&pState->rcFaytCloseBtn, pt)) newHover = 3;

            if (newHover != pState->treeFaytHoverBtn)
            {
                pState->treeFaytHoverBtn = newHover;
                InvalidateRect(hWnd, &pState->rcFaytDock, FALSE);
            }
        }

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

        RECT rcClient = { 0 };
        GetClientRect(hWnd, &rcClient);
        int width = rcClient.right;
        int height = rcClient.bottom;
        if (width <= 0 || height <= 0) { EndPaint(hWnd, &ps); return 0; }

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP holdBmp = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // Fill background
        HBRUSH hbrMainBg = CreateSolidBrush(VisualStudioTheme::Background);
        FillRect(hdcMem, &rcClient, hbrMainBg);
        DeleteObject(hbrMainBg);

        // 1. Ribbon Toolbar Background
        HBRUSH hbrRibbon = CreateSolidBrush(VisualStudioTheme::RibbonBg);
        FillRect(hdcMem, &pState->rcRibbon, hbrRibbon);
        DeleteObject(hbrRibbon);

        HPEN hPenBorder = CreatePen(PS_SOLID, 1, VisualStudioTheme::BorderLine);
        HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPenBorder);
        MoveToEx(hdcMem, 0, pState->rcRibbon.bottom - 1, NULL);
        LineTo(hdcMem, width, pState->rcRibbon.bottom - 1);

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
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdcMem, hbrBtn);
            HPEN hOldP = (HPEN)SelectObject(hdcMem, hPenBtn);

            RoundRect(hdcMem, btn.rc.left, btn.rc.top, btn.rc.right, btn.rc.bottom, 6, 6);

            SelectObject(hdcMem, hOldBr);
            SelectObject(hdcMem, hOldP);
            DeleteObject(hbrBtn);
            DeleteObject(hPenBtn);

            SetBkMode(hdcMem, TRANSPARENT);
            SelectObject(hdcMem, pState->hFontIconSmall ? pState->hFontIconSmall : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, VisualStudioTheme::TagText);
            RECT rcIcon = { btn.rc.left + 6, btn.rc.top + 6, btn.rc.left + 24, btn.rc.bottom - 6 };
            DrawTextW(hdcMem, btn.iconGlyph, -1, &rcIcon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdcMem, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, VisualStudioTheme::TextPrimary);
            RECT rcLabel = { btn.rc.left + 26, btn.rc.top, btn.rc.right - 6, btn.rc.bottom };
            DrawTextW(hdcMem, btn.label, -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // Draw Ribbon Separators
        HPEN hPenSep = CreatePen(PS_SOLID, 1, VisualStudioTheme::BorderLine);
        HPEN hOldSepP = (HPEN)SelectObject(hdcMem, hPenSep);
        for (int sepX : pState->ribbonSeparators)
        {
            MoveToEx(hdcMem, sepX, pState->rcRibbon.top + 7, NULL);
            LineTo(hdcMem, sepX, pState->rcRibbon.bottom - 7);
        }
        SelectObject(hdcMem, hOldSepP);
        DeleteObject(hPenSep);

        // 2. Splitter Bar & Sidebar Border
        HBRUSH hbrSplitter = CreateSolidBrush(VisualStudioTheme::SidebarBg);
        FillRect(hdcMem, &pState->rcSplitter, hbrSplitter);
        DeleteObject(hbrSplitter);

        HPEN hPenSplitter = CreatePen(PS_SOLID, 1, VisualStudioTheme::BorderLine);
        HPEN hOldP3 = (HPEN)SelectObject(hdcMem, hPenSplitter);
        MoveToEx(hdcMem, pState->rcSplitter.left, pState->rcSplitter.top, NULL);
        LineTo(hdcMem, pState->rcSplitter.left, pState->rcSplitter.bottom);
        MoveToEx(hdcMem, pState->rcSplitter.right - 1, pState->rcSplitter.top, NULL);
        LineTo(hdcMem, pState->rcSplitter.right - 1, pState->rcSplitter.bottom);

        // Subtle centered grip handle
        int midX = (pState->rcSplitter.left + pState->rcSplitter.right) / 2;
        int midY = (pState->rcSplitter.top + pState->rcSplitter.bottom) / 2;
        HPEN hPenGrip = CreatePen(PS_SOLID, 1, VisualStudioTheme::CardBorder);
        SelectObject(hdcMem, hPenGrip);
        MoveToEx(hdcMem, midX, midY - 14, NULL);
        LineTo(hdcMem, midX, midY + 14);
        SelectObject(hdcMem, hOldP3);
        DeleteObject(hPenGrip);
        DeleteObject(hPenSplitter);

        // Draw dedicated FAYT search dock bar if active
        if (pState->bTreeFaytActive)
        {
            DrawFloatingTreeFaytPill(hdcMem, pState);
        }

        // 3. Footer Status Bar
        HBRUSH hbrFooter = CreateSolidBrush(VisualStudioTheme::FooterBg);
        FillRect(hdcMem, &pState->rcFooter, hbrFooter);
        DeleteObject(hbrFooter);

        MoveToEx(hdcMem, 0, pState->rcFooter.top, NULL);
        LineTo(hdcMem, width, pState->rcFooter.top);

        SelectObject(hdcMem, hOldPen);
        DeleteObject(hPenBorder);

        // Footer Telemetry Display
        if (pState->isLoadingShape)
        {
            std::wstring loadingText = L"\x26A1 Loading " + pState->loadingShapeName + L"... (Parsing geometry & textures in background)";
            SelectObject(hdcMem, pState->hFontBold ? pState->hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, RGB(255, 200, 80));
            TextOutW(hdcMem, 16, pState->rcFooter.top + (26 - 15) / 2, loadingText.c_str(), (int)loadingText.length());
        }
        else if (pState->activeTab == 1 && pState->compositeStock.isValid)
        {
            std::wstring fName = pState->compositeStock.stockFileName;
            uint64_t vCount = pState->compositeStock.totalVertices;
            uint64_t tCount = pState->compositeStock.totalTriangles;
            size_t subShapeCount = pState->compositeStock.subShapes.size();
            uint64_t imgCount = pState->compositeStock.totalTextures;

            wchar_t szStatus[320] = { 0 };
            swprintf_s(szStatus, L"Stock: %s (%s) | %zu Shape(s) | Verts: %s | Tris: %s | Textures: %llu | \x26A1 %.1f ms",
                fName.c_str(),
                pState->compositeStock.spec.category.empty() ? L"Stock" : pState->compositeStock.spec.category.c_str(),
                subShapeCount,
                FormatNumberWithCommas(vCount).c_str(),
                FormatNumberWithCommas(tCount).c_str(),
                imgCount,
                pState->shapeLoadTimeMs);

            SelectObject(hdcMem, pState->hFontBold ? pState->hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, VisualStudioTheme::TextPrimary);
            TextOutW(hdcMem, 16, pState->rcFooter.top + (26 - 15) / 2, szStatus, (int)wcslen(szStatus));

            // Right side: Animation status, speed, & wheel roll telemetry
            std::wstring animInfo;
            if (pState->compositeStock.hasAnimation)
            {
                wchar_t szFrame[240] = { 0 };
                swprintf_s(szFrame, L"Anim: [%.1f / %.1f] (%.0f fps) | %.2fx %s | %s%s",
                    pState->animCurrentFrame,
                    pState->compositeStock.maxAnimationFrames,
                    pState->compositeStock.animationFrameRate,
                    pState->animSpeed,
                    pState->isAnimPlaying ? L"\x25B6" : L"\x23F8",
                    pState->enableWheelSpin && pState->compositeStock.hasSimulatedWheels ? L"Wheels: \x2714 | " : L"",
                    pState->activeAnimLabel.c_str());
                animInfo = szFrame;
            }
            else if (pState->compositeStock.hasSimulatedWheels)
            {
                wchar_t szFrame[240] = { 0 };
                swprintf_s(szFrame, L"Simulated Wheels: %s (%.0f\x00B0) | %.2fx %s | %s",
                    pState->enableWheelSpin ? (pState->isAnimPlaying ? L"Rolling \x2699" : L"Ready") : L"Disabled",
                    DirectX::XMConvertToDegrees(pState->wheelSpinAngle),
                    pState->animSpeed,
                    pState->isAnimPlaying ? L"\x25B6" : L"\x23F8",
                    pState->activeAnimLabel.c_str());
                animInfo = szFrame;
            }
            else
            {
                animInfo = L"Static Stock Unit (0 anim tracks)";
            }

            SelectObject(hdcMem, pState->hFontMono ? pState->hFontMono : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, (pState->compositeStock.hasAnimation || (pState->enableWheelSpin && pState->compositeStock.hasSimulatedWheels)) ? RGB(100, 200, 255) : VisualStudioTheme::TextMuted);
            SIZE aiSize = { 0 };
            GetTextExtentPoint32W(hdcMem, animInfo.c_str(), (int)animInfo.length(), &aiSize);
            int textX = (std::max)(10, (int)(width - (int)aiSize.cx - 16));
            int textY = (int)(pState->rcFooter.top + (26 - 15) / 2);
            TextOutW(hdcMem, textX, textY, animInfo.c_str(), (int)animInfo.length());
        }
        else if (pState->currentShape.isValid)
        {
            const std::wstring& activePath = (pState->activeTab == 0) ? pState->shapeFilePath : pState->stockFilePath;
            std::wstring fName = std::filesystem::path(activePath).filename().wstring();
            if (fName.empty() && !pState->currentShape.shapeFilePath.empty()) {
                fName = std::filesystem::path(pState->currentShape.shapeFilePath).filename().wstring();
            }

            uint64_t vCount = pState->currentShape.vertices.size();
            uint64_t tCount = pState->currentShape.indices.size() / 3;
            uint64_t subMeshCount = pState->currentShape.subMeshes.size();
            uint64_t boneCount = pState->currentShape.boneMatrices.size();
            uint64_t imgCount = pState->currentShape.rawImageNames.size();

            wchar_t szStatus[320] = { 0 };
            swprintf_s(szStatus, L"Shape: %s | Verts: %s | Tris: %s | SubMeshes: %llu | Nodes: %llu | Textures: %llu | \x26A1 %.1f ms",
                fName.c_str(),
                FormatNumberWithCommas(vCount).c_str(),
                FormatNumberWithCommas(tCount).c_str(),
                subMeshCount,
                boneCount,
                imgCount,
                pState->shapeLoadTimeMs);

            SelectObject(hdcMem, pState->hFontBold ? pState->hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, VisualStudioTheme::TextPrimary);
            TextOutW(hdcMem, 16, pState->rcFooter.top + (26 - 15) / 2, szStatus, (int)wcslen(szStatus));

            // Right side: Animation status, speed, & wheel roll telemetry
            std::wstring animInfo;
            if (pState->currentShape.animation.hasAnimation)
            {
                wchar_t szFrame[240] = { 0 };
                swprintf_s(szFrame, L"Anim: [%.1f / %.1f] (%.0f fps) | %.2fx %s | %s%s",
                    pState->animCurrentFrame,
                    pState->currentShape.animation.frameCount,
                    pState->currentShape.animation.frameRate,
                    pState->animSpeed,
                    pState->isAnimPlaying ? L"\x25B6" : L"\x23F8",
                    pState->enableWheelSpin && pState->currentShape.animation.hasSimulatedWheels ? L"Wheels: \x2714 | " : L"",
                    pState->activeAnimLabel.c_str());
                animInfo = szFrame;
            }
            else if (pState->currentShape.animation.hasSimulatedWheels)
            {
                wchar_t szFrame[240] = { 0 };
                swprintf_s(szFrame, L"Simulated Wheels: %s (%.0f\x00B0) | %.2fx %s | %s",
                    pState->enableWheelSpin ? (pState->isAnimPlaying ? L"Rolling \x2699" : L"Ready") : L"Disabled",
                    DirectX::XMConvertToDegrees(pState->wheelSpinAngle),
                    pState->animSpeed,
                    pState->isAnimPlaying ? L"\x25B6" : L"\x23F8",
                    pState->activeAnimLabel.c_str());
                animInfo = szFrame;
            }
            else
            {
                animInfo = L"Static Model (0 anim tracks)";
            }

            SelectObject(hdcMem, pState->hFontMono ? pState->hFontMono : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, (pState->currentShape.animation.hasAnimation || (pState->enableWheelSpin && pState->currentShape.animation.hasSimulatedWheels)) ? RGB(100, 200, 255) : VisualStudioTheme::TextMuted);
            SIZE aiSize = { 0 };
            GetTextExtentPoint32W(hdcMem, animInfo.c_str(), (int)animInfo.length(), &aiSize);
            int textX = (std::max)(10, (int)(width - (int)aiSize.cx - 16));
            int textY = (int)(pState->rcFooter.top + (26 - 15) / 2);
            TextOutW(hdcMem, textX, textY, animInfo.c_str(), (int)animInfo.length());
        }
        else
        {
            std::wstring noShapeStr = (pState->activeTab == 0)
                ? L"No Shape File Selected (Choose a .s model from the tree)"
                : L"No Stock File Selected (Choose a .wag / .eng from the tree)";
            SelectObject(hdcMem, pState->hFontMain ? pState->hFontMain : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdcMem, VisualStudioTheme::TextMuted);
            TextOutW(hdcMem, 16, pState->rcFooter.top + (26 - 15) / 2, noShapeStr.c_str(), (int)noShapeStr.length());
        }

        BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, holdBmp);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
    {
        if (s_hActiveStudioWnd == hWnd) s_hActiveStudioWnd = NULL;
        if (pState)
        {
            KillTimer(hWnd, TIMER_TREE_FAYT_TIMEOUT);

            if (pState->assetTree.GetHWND())
            {
                RemoveWindowSubclass(pState->assetTree.GetHWND(), TreeSubclassProc, SUBCLASS_TREE_ID);
            }

            CleanupD3D11(pState);

            if (pState->hFontTitle) { DeleteObject(pState->hFontTitle); pState->hFontTitle = NULL; }
            if (pState->hFontHeader) { DeleteObject(pState->hFontHeader); pState->hFontHeader = NULL; }
            if (pState->hFontMain) { DeleteObject(pState->hFontMain); pState->hFontMain = NULL; }
            if (pState->hFontBold) { DeleteObject(pState->hFontBold); pState->hFontBold = NULL; }
            if (pState->hFontMono) { DeleteObject(pState->hFontMono); pState->hFontMono = NULL; }
            if (pState->hFontIcon) { DeleteObject(pState->hFontIcon); pState->hFontIcon = NULL; }
            if (pState->hFontIconSmall) { DeleteObject(pState->hFontIconSmall); pState->hFontIconSmall = NULL; }

            RestoreParentWindowFocus(pState->hParent ? pState->hParent : GetWindow(hWnd, GW_OWNER));
        }
        return 0;
    }

    case WM_NCDESTROY:
    {
        if (s_hActiveStudioWnd == hWnd) s_hActiveStudioWnd = NULL;
        HWND hOwner = pState ? pState->hParent : GetWindow(hWnd, GW_OWNER);
        if (pState)
        {
            delete pState;
            pState = nullptr;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        if (hOwner && IsWindow(hOwner))
        {
            RestoreParentWindowFocus(hOwner);
        }
        break;
    }

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void Show3DVisualStudioDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath)
{
    if (s_hActiveStudioWnd && IsWindow(s_hActiveStudioWnd))
    {
        if (IsIconic(s_hActiveStudioWnd))
        {
            ShowWindow(s_hActiveStudioWnd, SW_RESTORE);
        }
        SetWindowPos(s_hActiveStudioWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(s_hActiveStudioWnd);

        if (!filePath.empty())
        {
            SendMessageW(s_hActiveStudioWnd, WM_VS_LOAD_FILE_EXTERNAL, 0, (LPARAM)filePath.c_str());
        }
        return;
    }

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = Visual3DStudioProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"TSCB_Visual3DStudioWnd";

    RegisterClassExW(&wc);

    VisualStudioState* pState = new VisualStudioState();
    pState->basePath = basePath;
    pState->hParent = hWndParent;

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
        GetModuleHandleW(NULL),
        pState
    );

    if (hWnd)
    {
        s_hActiveStudioWnd = hWnd;

        BOOL bDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
        DWM_WINDOW_CORNER_PREFERENCE corner = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
        COLORREF borderColor = VisualStudioTheme::BorderLine;
        DwmSetWindowAttribute(hWnd, (DWMWINDOWATTRIBUTE)DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        MARGINS margins = { 0, 0, 0, 0 };
        DwmExtendFrameIntoClientArea(hWnd, &margins);

        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        ShowWindow(hWnd, SW_SHOW);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;

        if (pState && pState->hTitleBar && IsWindow(pState->hTitleBar))
        {
            CustomTitleBar_SetDarkMode(pState->hTitleBar, TRUE);
            CustomTitleBar_SetActiveTab(pState->hTitleBar, pState->activeTab);
            SetWindowPos(pState->hTitleBar, NULL, 0, 0, clientW, 66, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            SendMessage(pState->hTitleBar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(clientW, 66));
            RedrawWindow(pState->hTitleBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        UpdateWindow(hWnd);
        SetFocus(hWnd);
    }
    else
    {
        s_hActiveStudioWnd = NULL;
        delete pState;
    }
}

void ShowShapeViewerDialog(HWND hWndParent, const std::wstring& filePath, const std::wstring& basePath)
{
    Show3DVisualStudioDialog(hWndParent, filePath, basePath);
}
