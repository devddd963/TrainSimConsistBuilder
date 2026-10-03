#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VisualConsistView.h"
#include "ModernMessageBox.h"
#include "ModernContextMenu.h"
#include "CustomScrollBar.h"
#include "UITheme.h"
#include "../SRC/Resource.h"
#include "../SRC/ShapeReader.h"
#include "../SRC/TextureLoader.h"
#include "../SRC/CompositeStockLoader.h"
#include "../SRC/StockSpecReader.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <future>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <filesystem>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define WM_VCV_UNIT_LOADED (WM_USER + 501)
#define TIMER_VCV_RENDER   0x6C01

extern HFONT GetAdaptiveSystemFont();

// -------------------------------------------------------------
// Shaders & Constant Buffers (DirectX 11)
// -------------------------------------------------------------
struct CBPerFrame {
    DirectX::XMMATRIX WorldViewProj;
    DirectX::XMMATRIX World;
    DirectX::XMFLOAT3 LightDir;
    float             LightIntensity;
    DirectX::XMFLOAT3 AmbientColor;
    float             AlphaCutoff;
    DirectX::XMFLOAT4 HighlightColor;
};

struct CBBones {
    DirectX::XMMATRIX BoneTransforms[256];
};

struct TrackVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

static const char* g_VcvShapeVSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float4 HighlightColor;\n"
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
"    float4 localPos = float4(input.pos, 1.0f);\n"
"    float4 localNorm = float4(input.normal, 0.0f);\n"
"    if (input.bone > 0 && input.bone < 256) {\n"
"        localPos = mul(localPos, BoneTransforms[input.bone]);\n"
"        localNorm = mul(localNorm, BoneTransforms[input.bone]);\n"
"    }\n"
"    output.pos = mul(localPos, WorldViewProj);\n"
"    output.normal = normalize(mul(localNorm.xyz, (float3x3)World));\n"
"    output.uv = input.uv;\n"
"    return output;\n"
"}\n";

static const char* g_VcvShapePSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float4 HighlightColor;\n"
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
"    float3 N = normalize(input.normal);\n"
"    float3 L = normalize(LightDir);\n"
"    float NdotL = max(0.0f, dot(N, L));\n"
"    float wrapLighting = max(0.0f, (dot(N, L) + 0.35f) / 1.35f);\n"
"    float fillLight = max(0.0f, N.x * 0.35f + N.y * 0.25f);\n"
"    float3 lit = AmbientColor + (wrapLighting * LightIntensity) + fillLight + HighlightColor.rgb;\n"
"    lit = min(lit, float3(1.35f, 1.35f, 1.35f));\n"
"    float3 diffuse = col.rgb * lit;\n"
"    return float4(diffuse, col.a);\n"
"}\n";

static const char* g_VcvTrackVSHLSL =
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
"struct VS_IN {\n"
"    float3 pos : POSITION;\n"
"    float4 col : COLOR0;\n"
"};\n"
"struct PS_IN {\n"
"    float4 pos : SV_POSITION;\n"
"    float4 col : COLOR0;\n"
"};\n"
"PS_IN VSTrackMain(VS_IN input) {\n"
"    PS_IN output;\n"
"    output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);\n"
"    output.col = input.col;\n"
"    return output;\n"
"}\n";

static const char* g_VcvTrackPSHLSL =
"struct PS_IN {\n"
"    float4 pos : SV_POSITION;\n"
"    float4 col : COLOR0;\n"
"};\n"
"float4 PSTrackMain(PS_IN input) : SV_TARGET {\n"
"    return input.col;\n"
"}\n";

// -------------------------------------------------------------
// Consist Unit Data Structure (3D Hybrid View)
// -------------------------------------------------------------
struct Visual3DUnit {
    ConsistReader::UnitInfo info;
    std::wstring stockFilePath;
    bool isMissing = false;
    bool isMissingStock = false;
    bool isMissingShape = false;
    bool isEngine = false;
    bool isFlipped = false;
    bool isPhantom = false; // Zero physical length / sound car / AI horn

    // Physical Dimensions & Coupling (meters)
    float length = 22.0f;
    float width = 3.2f;
    float height = 4.0f;
    float couplerGap = 0.0f;
    float zCenter = 0.0f;
    float zFront = 0.0f;
    float zRear = 0.0f;

    // 3D Composite Model
    CompositeStockUnit compositeStock;
    bool isCpuLoaded = false;
    bool isGpuFinalized = false;
};

// -------------------------------------------------------------
// Visual Consist State
// -------------------------------------------------------------
struct VisualConsistState {
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hViewportWnd = NULL;
    HINSTANCE hInst = NULL;
    int controlId = 0;
    BOOL bDarkMode = TRUE;
    bool isFloating = false;
    bool isCollapsed = false;

    // DirectX 11 Core
    IDXGISwapChain*         pSwapChain = nullptr;
    ID3D11Device*           pD3DDevice = nullptr;
    ID3D11DeviceContext*    pD3DContext = nullptr;
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    ID3D11DepthStencilView* pDepthStencilView = nullptr;
    ID3D11Texture2D*        pDepthStencilBuffer = nullptr;

    ID3D11RasterizerState*   pRasterStateSolid = nullptr;
    ID3D11RasterizerState*   pRasterStateDecal = nullptr;
    ID3D11RasterizerState*   pRasterStateWireframe = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateWrite = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateReadOnly = nullptr;
    ID3D11BlendState*        pBlendStateOpaque = nullptr;
    ID3D11BlendState*        pBlendStateAlpha = nullptr;
    ID3D11SamplerState*      pSamplerState = nullptr;

    ID3D11Buffer*       pConstantBuffer = nullptr;
    ID3D11Buffer*       pBoneConstantBuffer = nullptr;
    ID3D11VertexShader* pShapeVS = nullptr;
    ID3D11PixelShader*  pShapePS = nullptr;
    ID3D11InputLayout*  pShapeLayout = nullptr;
    ID3D11VertexShader* pTrackVS = nullptr;
    ID3D11PixelShader*  pTrackPS = nullptr;
    ID3D11InputLayout*  pTrackLayout = nullptr;

    ID3D11Buffer* pTrackVertexBuffer = nullptr;
    UINT          trackVertexCount = 0;

    std::unique_ptr<TextureLoader> pTextureLoader;

    // Units and Consist Data
    std::wstring basePath;
    std::wstring consistTitle;
    std::wstring consistFileName;
    int  loadedUnitCount = 0;
    bool isLoading = false;
    std::vector<Visual3DUnit> units;
    float trainTotalLength = 0.0f;
    float trainZMin = 0.0f;
    float trainZMax = 0.0f;
    int selectedIndex = -1;
    int hoverIndex = -1;

    // Camera Navigation & 3D Orbit (Default: Left-facing Side-Profile, close-up scaling)
    float camFocusZ = 0.0f;
    float targetFocusZ = 0.0f;
    float camYaw = -1.5707963f;  // -90 deg (Facing Left, Front on Left, Rear on Right)
    float camPitch = 0.03f;      // Slight elevation for depth
    float camDist = 14.0f;       // Crisp scaling to fill viewport height
    float camCenterY = 1.9f;     // Aligned with coach window/livery beltline
    bool  bWireframe = false;
    bool  bOrbitMode = false;    // Disabled by default (when disabled: panning & selection active)

    // Mouse Interaction
    bool  isDragging3D = false;
    bool  isPanningZ = false;
    POINT dragStartPt = { 0, 0 };
    float dragStartYaw = 0.0f;
    float dragStartPitch = 0.0f;
    float dragStartFocusZ = 0.0f;

    // Header Controls & Layout
    RECT rcHeader = { 0 };
    RECT rcUnitBar = { 0 };
    int  hoverUnitBarIndex = -1;
    std::vector<RECT> unitBadgeRects;
    RECT rcBtnSideView = { 0 };
    RECT rcBtnOrbit3D = { 0 };
    RECT rcBtnWireframe = { 0 };
    RECT rcBtnFloat = { 0 };
    RECT rcBtnCollapse = { 0 };
    RECT rcBtnSettings = { 0 };
    RECT rcBtnClose = { 0 };
    RECT rcFooter = { 0 };
    RECT rcBtnFooterClose = { 0 };
    RECT rcChkStartupCollapse = { 0 };

    bool hoverSideView = false;
    bool hoverOrbit3D = false;
    bool hoverWireframe = false;
    bool hoverFloat = false;
    bool hoverCollapse = false;
    bool hoverSettings = false;
    bool hoverClose = false;
    bool hoverFooterClose = false;
    bool hoverChkStartup = false;

    // Horizontal ScrollBar
    CustomScrollBar scrollBar{ ScrollBarOrientation::Horizontal };

    HFONT hFontTitle = NULL;
    HFONT hFontMetrics = NULL;
    HFONT hFontIcons = NULL;
    HFONT hFontBadge = NULL;

    // Async Background Loader
    std::atomic<uint32_t> loadGeneration{ 0 };
};

static const int HEADER_HEIGHT = 28;
static const int EXPANDED_HEIGHT = 180;

// Registry Helper
static bool ReadCollapseOnStartupRegistry()
{
    HKEY hKey;
    DWORD dwVal = 0;
    DWORD dwSize = sizeof(dwVal);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"VisualPreviewCollapsedOnStartup", NULL, NULL, (LPBYTE)&dwVal, &dwSize);
        RegCloseKey(hKey);
    }
    return (dwVal != 0);
}

static void WriteCollapseOnStartupRegistry(bool bCollapsed)
{
    HKEY hKey;
    DWORD dwVal = bCollapsed ? 1 : 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"VisualPreviewCollapsedOnStartup", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));
        RegCloseKey(hKey);
    }
}

// -------------------------------------------------------------
// DirectX 11 Pipeline Management
// -------------------------------------------------------------
static void RebuildTrackMesh(VisualConsistState* pState);

static void ResizeD3D11(VisualConsistState* pState, int width, int height)
{
    if (!pState || !pState->pD3DDevice || !pState->pSwapChain || width <= 0 || height <= 0) return;

    if (pState->pD3DContext)
    {
        ID3D11RenderTargetView* nullRTV = nullptr;
        pState->pD3DContext->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    if (pState->pRenderTargetView)   { pState->pRenderTargetView->Release();   pState->pRenderTargetView = nullptr; }
    if (pState->pDepthStencilView)   { pState->pDepthStencilView->Release();   pState->pDepthStencilView = nullptr; }
    if (pState->pDepthStencilBuffer) { pState->pDepthStencilBuffer->Release(); pState->pDepthStencilBuffer = nullptr; }

    pState->pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(pState->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)) && pBackBuffer)
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
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (SUCCEEDED(pState->pD3DDevice->CreateTexture2D(&depthDesc, NULL, &pState->pDepthStencilBuffer)) && pState->pDepthStencilBuffer)
    {
        pState->pD3DDevice->CreateDepthStencilView(pState->pDepthStencilBuffer, NULL, &pState->pDepthStencilView);
    }
}

static bool InitD3D11(VisualConsistState* pState, HWND hWndViewport, int width, int height)
{
    if (width <= 0) width = 100;
    if (height <= 0) height = 100;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWndViewport;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        0, featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION, &scd,
        &pState->pSwapChain, &pState->pD3DDevice, &featureLevel, &pState->pD3DContext
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

    D3D11_RASTERIZER_DESC rdDecal = {};
    rdDecal.FillMode = D3D11_FILL_SOLID;
    rdDecal.CullMode = D3D11_CULL_BACK;
    rdDecal.FrontCounterClockwise = FALSE;
    rdDecal.DepthClipEnable = TRUE;
    rdDecal.DepthBias = -50;
    rdDecal.SlopeScaledDepthBias = -0.5f;
    pState->pD3DDevice->CreateRasterizerState(&rdDecal, &pState->pRasterStateDecal);

    // Depth Stencil States
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

    // Sampler State
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    pState->pD3DDevice->CreateSamplerState(&sampDesc, &pState->pSamplerState);

    // Constant Buffers
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBPerFrame);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&cbd, NULL, &pState->pConstantBuffer);

    D3D11_BUFFER_DESC boneCbd = {};
    boneCbd.ByteWidth = sizeof(CBBones);
    boneCbd.Usage = D3D11_USAGE_DEFAULT;
    boneCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&boneCbd, NULL, &pState->pBoneConstantBuffer);

    // Shape Shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    if (SUCCEEDED(D3DCompile(g_VcvShapeVSHLSL, strlen(g_VcvShapeVSHLSL), NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob)))
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

    if (SUCCEEDED(D3DCompile(g_VcvShapePSHLSL, strlen(g_VcvShapePSHLSL), NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &pState->pShapePS);
        psBlob->Release();
    }

    // Track / Line Shaders
    ID3DBlob* tvsBlob = nullptr;
    ID3DBlob* tpsBlob = nullptr;
    if (SUCCEEDED(D3DCompile(g_VcvTrackVSHLSL, strlen(g_VcvTrackVSHLSL), NULL, NULL, NULL, "VSTrackMain", "vs_4_0", 0, 0, &tvsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreateVertexShader(tvsBlob->GetBufferPointer(), tvsBlob->GetBufferSize(), NULL, &pState->pTrackVS);
        D3D11_INPUT_ELEMENT_DESC trackLayoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        pState->pD3DDevice->CreateInputLayout(trackLayoutDesc, _countof(trackLayoutDesc), tvsBlob->GetBufferPointer(), tvsBlob->GetBufferSize(), &pState->pTrackLayout);
        tvsBlob->Release();
    }

    if (SUCCEEDED(D3DCompile(g_VcvTrackPSHLSL, strlen(g_VcvTrackPSHLSL), NULL, NULL, NULL, "PSTrackMain", "ps_4_0", 0, 0, &tpsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(tpsBlob->GetBufferPointer(), tpsBlob->GetBufferSize(), NULL, &pState->pTrackPS);
        tpsBlob->Release();
    }

    RebuildTrackMesh(pState);
    return true;
}

static void ShutdownD3D11(VisualConsistState* pState)
{
    if (!pState) return;

    if (pState->pTrackVertexBuffer)      { pState->pTrackVertexBuffer->Release();      pState->pTrackVertexBuffer = nullptr; }
    if (pState->pTrackLayout)            { pState->pTrackLayout->Release();            pState->pTrackLayout = nullptr; }
    if (pState->pTrackPS)                { pState->pTrackPS->Release();                pState->pTrackPS = nullptr; }
    if (pState->pTrackVS)                { pState->pTrackVS->Release();                pState->pTrackVS = nullptr; }
    if (pState->pShapeLayout)            { pState->pShapeLayout->Release();            pState->pShapeLayout = nullptr; }
    if (pState->pShapePS)                { pState->pShapePS->Release();                pState->pShapePS = nullptr; }
    if (pState->pShapeVS)                { pState->pShapeVS->Release();                pState->pShapeVS = nullptr; }
    if (pState->pBoneConstantBuffer)     { pState->pBoneConstantBuffer->Release();     pState->pBoneConstantBuffer = nullptr; }
    if (pState->pConstantBuffer)         { pState->pConstantBuffer->Release();         pState->pConstantBuffer = nullptr; }
    if (pState->pSamplerState)           { pState->pSamplerState->Release();           pState->pSamplerState = nullptr; }
    if (pState->pBlendStateAlpha)        { pState->pBlendStateAlpha->Release();        pState->pBlendStateAlpha = nullptr; }
    if (pState->pBlendStateOpaque)       { pState->pBlendStateOpaque->Release();       pState->pBlendStateOpaque = nullptr; }
    if (pState->pDepthStencilStateReadOnly) { pState->pDepthStencilStateReadOnly->Release(); pState->pDepthStencilStateReadOnly = nullptr; }
    if (pState->pDepthStencilStateWrite) { pState->pDepthStencilStateWrite->Release(); pState->pDepthStencilStateWrite = nullptr; }
    if (pState->pRasterStateDecal)       { pState->pRasterStateDecal->Release();       pState->pRasterStateDecal = nullptr; }
    if (pState->pRasterStateWireframe)   { pState->pRasterStateWireframe->Release();   pState->pRasterStateWireframe = nullptr; }
    if (pState->pRasterStateSolid)       { pState->pRasterStateSolid->Release();       pState->pRasterStateSolid = nullptr; }
    if (pState->pDepthStencilView)       { pState->pDepthStencilView->Release();       pState->pDepthStencilView = nullptr; }
    if (pState->pDepthStencilBuffer)     { pState->pDepthStencilBuffer->Release();     pState->pDepthStencilBuffer = nullptr; }
    if (pState->pRenderTargetView)       { pState->pRenderTargetView->Release();       pState->pRenderTargetView = nullptr; }
    if (pState->pSwapChain)              { pState->pSwapChain->Release();              pState->pSwapChain = nullptr; }
    if (pState->pD3DContext)             { pState->pD3DContext->Release();             pState->pD3DContext = nullptr; }
    if (pState->pD3DDevice)              { pState->pD3DDevice->Release();              pState->pD3DDevice = nullptr; }
}

// -------------------------------------------------------------
// Procedural Track Bed & Missing Marker Mesh
// -------------------------------------------------------------
static void RebuildTrackMesh(VisualConsistState* pState)
{
    if (!pState || !pState->pD3DDevice) return;

    if (pState->pTrackVertexBuffer)
    {
        pState->pTrackVertexBuffer->Release();
        pState->pTrackVertexBuffer = nullptr;
    }

    std::vector<TrackVertex> vertices;

    float zStart = pState->trainZMin - 35.0f;
    float zEnd   = pState->trainZMax + 35.0f;
    if (zEnd - zStart < 80.0f)
    {
        zStart = -40.0f;
        zEnd = 40.0f;
    }

    float railGaugeHalf = 0.8385f; // Broad Gauge 1.676m / 2
    float railHeight = 0.15f;

    DirectX::XMFLOAT4 colRail = { 0.82f, 0.84f, 0.88f, 1.0f };
    DirectX::XMFLOAT4 colSleeper = { 0.28f, 0.26f, 0.24f, 1.0f };
    DirectX::XMFLOAT4 colBallast = { 0.14f, 0.14f, 0.15f, 1.0f };

    // 1. Continuous Steel Rails (Left & Right)
    vertices.push_back({ { -railGaugeHalf, railHeight, zStart }, colRail });
    vertices.push_back({ { -railGaugeHalf, railHeight, zEnd   }, colRail });

    vertices.push_back({ { +railGaugeHalf, railHeight, zStart }, colRail });
    vertices.push_back({ { +railGaugeHalf, railHeight, zEnd   }, colRail });

    // 2. Ballast Ground Bed Borders
    vertices.push_back({ { -1.8f, 0.0f, zStart }, colBallast });
    vertices.push_back({ { -1.8f, 0.0f, zEnd   }, colBallast });
    vertices.push_back({ { +1.8f, 0.0f, zStart }, colBallast });
    vertices.push_back({ { +1.8f, 0.0f, zEnd   }, colBallast });

    // 3. Ties / Sleepers along track every 0.65m
    for (float z = zStart; z <= zEnd; z += 0.65f)
    {
        vertices.push_back({ { -1.35f, 0.04f, z }, colSleeper });
        vertices.push_back({ { +1.35f, 0.04f, z }, colSleeper });
    }

    // 4. Missing Unit Indicators (Narrow glowing red lines / gates)
    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        const auto& u = pState->units[i];
        if (u.isMissing)
        {
            DirectX::XMFLOAT4 colMissing = { 1.0f, 0.22f, 0.22f, 1.0f };
            float zM = u.zCenter;
            float zHalf = (std::max)(1.0f, u.length * 0.5f);

            // Red vertical marker gate
            vertices.push_back({ { -1.5f, 0.0f, zM }, colMissing });
            vertices.push_back({ { -1.5f, 3.2f, zM }, colMissing });

            vertices.push_back({ { -1.5f, 3.2f, zM }, colMissing });
            vertices.push_back({ { +1.5f, 3.2f, zM }, colMissing });

            vertices.push_back({ { +1.5f, 3.2f, zM }, colMissing });
            vertices.push_back({ { +1.5f, 0.0f, zM }, colMissing });

            // Red bounding track segment
            vertices.push_back({ { -1.5f, 0.05f, zM - zHalf }, colMissing });
            vertices.push_back({ { -1.5f, 0.05f, zM + zHalf }, colMissing });

            vertices.push_back({ { +1.5f, 0.05f, zM - zHalf }, colMissing });
            vertices.push_back({ { +1.5f, 0.05f, zM + zHalf }, colMissing });
        }
    }

    pState->trackVertexCount = (UINT)vertices.size();
    if (pState->trackVertexCount == 0) return;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(vertices.size() * sizeof(TrackVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();

    pState->pD3DDevice->CreateBuffer(&bd, &initData, &pState->pTrackVertexBuffer);
}

// -------------------------------------------------------------
// Render 3D Consist Viewport
// -------------------------------------------------------------
static void Render3DConsist(VisualConsistState* pState)
{
    if (!pState || !pState->pD3DContext || !pState->pSwapChain || !pState->pRenderTargetView || !pState->pDepthStencilView)
        return;

    RECT rc;
    GetClientRect(pState->hViewportWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    // 1. Clear Views (Studio Slate Horizon Background)
    float clearCol[4] = { 0.085f, 0.090f, 0.105f, 1.0f };
    pState->pD3DContext->ClearRenderTargetView(pState->pRenderTargetView, clearCol);
    pState->pD3DContext->ClearDepthStencilView(pState->pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    pState->pD3DContext->RSSetViewports(1, &vp);

    pState->pD3DContext->OMSetRenderTargets(1, &pState->pRenderTargetView, pState->pDepthStencilView);

    // 2. Camera Matrices
    float aspect = (float)width / (float)(std::max)(1, height);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(24.0f), aspect, 0.2f, 1500.0f);

    float eyeX = pState->camDist * cosf(pState->camPitch) * sinf(pState->camYaw);
    float eyeY = pState->camCenterY + pState->camDist * sinf(pState->camPitch);
    float eyeZ = pState->camFocusZ + pState->camDist * cosf(pState->camPitch) * cosf(pState->camYaw);

    DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
    DirectX::XMVECTOR targetPos = DirectX::XMVectorSet(0.0f, pState->camCenterY, pState->camFocusZ, 1.0f);
    DirectX::XMVECTOR upVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePos, targetPos, upVec);
    DirectX::XMMATRIX viewProj = view * proj;

    // 3. Constant Buffer Setup (Studio Showroom Shading)
    CBPerFrame cb = {};
    cb.LightDir = DirectX::XMFLOAT3(0.75f, 0.85f, 0.45f);
    cb.LightIntensity = 0.55f;
    cb.AmbientColor = DirectX::XMFLOAT3(0.78f, 0.78f, 0.80f);
    cb.AlphaCutoff = 0.5f;
    cb.HighlightColor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    // 4. Render Procedural Rails & Missing Markers
    if (pState->pTrackVertexBuffer && pState->trackVertexCount > 0)
    {
        DirectX::XMMATRIX worldTrack = DirectX::XMMatrixIdentity();
        cb.World = DirectX::XMMatrixTranspose(worldTrack);
        cb.WorldViewProj = DirectX::XMMatrixTranspose(worldTrack * view * proj);
        pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);

        pState->pD3DContext->VSSetConstantBuffers(0, 1, &pState->pConstantBuffer);
        pState->pD3DContext->PSSetConstantBuffers(0, 1, &pState->pConstantBuffer);

        pState->pD3DContext->IASetInputLayout(pState->pTrackLayout);
        pState->pD3DContext->VSSetShader(pState->pTrackVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pTrackPS, NULL, 0);

        UINT stride = sizeof(TrackVertex);
        UINT offset = 0;
        pState->pD3DContext->IASetVertexBuffers(0, 1, &pState->pTrackVertexBuffer, &stride, &offset);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        pState->pD3DContext->RSSetState(pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);

        pState->pD3DContext->Draw(pState->trackVertexCount, 0);
    }

    // 5. Render 3D Consist Units (3-Pass Multi-Pass Pipeline with Angle-Aware Frustum Culling)
    float halfVisibleWidthM = pState->camDist * tanf(DirectX::XMConvertToRadians(12.0f)) * aspect;
    float visZMin = -1e9f;
    float visZMax = 1e9f;

    if (!pState->bOrbitMode)
    {
        // Side-View Mode: Tight culling for maximum scrolling FPS
        visZMin = pState->camFocusZ - halfVisibleWidthM - 15.0f;
        visZMax = pState->camFocusZ + halfVisibleWidthM + 15.0f;
    }
    else
    {
        // 3D Orbit Mode: Full perspective visibility along camera line of sight
        float orbitDepth = (std::max)(150.0f, pState->camDist * 4.0f + 250.0f);
        visZMin = pState->camFocusZ - orbitDepth;
        visZMax = pState->camFocusZ + orbitDepth;
    }

    pState->pD3DContext->IASetInputLayout(pState->pShapeLayout);
    pState->pD3DContext->VSSetShader(pState->pShapeVS, NULL, 0);
    pState->pD3DContext->PSSetShader(pState->pShapePS, NULL, 0);
    pState->pD3DContext->PSSetSamplers(0, 1, &pState->pSamplerState);
    pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    CBBones bones = {};
    for (int b = 0; b < 256; ++b) bones.BoneTransforms[b] = DirectX::XMMatrixIdentity();

    auto RenderConsistPass = [&](int passMode) {
        if (passMode == 0)
        {
            // PASS 1: Solid Opaque (DepthWrite = ON, Blend = OFF)
            pState->pD3DContext->RSSetState(pState->bWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
            pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
            pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);
            cb.AlphaCutoff = 0.0f;
        }
        else if (passMode == 1)
        {
            // PASS 2: Alpha Test & Decals (DepthWrite = ON, DepthBias = -50)
            pState->pD3DContext->RSSetState(pState->bWireframe ? pState->pRasterStateWireframe : pState->pRasterStateDecal);
            pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
            pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);
            cb.AlphaCutoff = 0.5f;
        }
        else if (passMode == 2)
        {
            // PASS 3: Translucent Glass (DepthWrite = OFF, Blend = Alpha)
            pState->pD3DContext->RSSetState(pState->bWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
            pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateReadOnly, 0);
            pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
            cb.AlphaCutoff = 0.001f;
        }

        for (size_t uIdx = 0; uIdx < pState->units.size(); ++uIdx)
        {
            auto& unit = pState->units[uIdx];
            if (unit.isMissing || !unit.isGpuFinalized || !unit.compositeStock.isValid)
                continue;

            // Frustum Z-Culling: Skip off-screen units to achieve ultra-fast 120+ FPS rendering
            float uZFront = (std::max)(unit.zFront, unit.zRear);
            float uZRear  = (std::min)(unit.zFront, unit.zRear);
            if (uZFront < visZMin || uZRear > visZMax)
                continue;

            // Interactive Lighting Boost (Selected & Hovered units get luminous spotlight boost; all other units retain 100% full natural showroom lighting)
            int effHover = (pState->hoverIndex >= 0) ? pState->hoverIndex : pState->hoverUnitBarIndex;
            int effSel = pState->selectedIndex;

            if ((int)uIdx == effSel)
            {
                // Selected Unit: Crisp Studio Spotlight Highlight
                cb.AmbientColor = DirectX::XMFLOAT3(0.88f, 0.88f, 0.90f);
                cb.LightIntensity = 0.70f;
                cb.HighlightColor = DirectX::XMFLOAT4(0.20f, 0.24f, 0.30f, 0.0f);
            }
            else if ((int)uIdx == effHover)
            {
                // Hovered Unit: Interactive Glow Boost
                cb.AmbientColor = DirectX::XMFLOAT3(0.85f, 0.85f, 0.88f);
                cb.LightIntensity = 0.65f;
                cb.HighlightColor = DirectX::XMFLOAT4(0.12f, 0.16f, 0.22f, 0.0f);
            }
            else
            {
                // Default Showroom Lighting for all other units (100% natural lighting)
                cb.AmbientColor = DirectX::XMFLOAT3(0.78f, 0.78f, 0.80f);
                cb.LightIntensity = 0.55f;
                cb.HighlightColor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            DirectX::XMMATRIX carWorld = unit.isFlipped ?
                (DirectX::XMMatrixRotationY(DirectX::XM_PI) * DirectX::XMMatrixTranslation(0.0f, 0.0f, unit.zCenter)) :
                DirectX::XMMatrixTranslation(0.0f, 0.0f, unit.zCenter);

            for (const auto& sub : unit.compositeStock.subShapes)
            {
                if (!sub || !sub->isLoaded || !sub->shape.pVertexBuffer) continue;

                bool hasMatching = false;
                for (const auto& sm : sub->shape.subMeshes)
                {
                    if (passMode == 0 && !sm.isTransparent) { hasMatching = true; break; }
                    if (passMode == 1 && (sm.isTransparent && sm.isAlphaTest)) { hasMatching = true; break; }
                    if (passMode == 2 && (sm.isTransparent && !sm.isAlphaTest)) { hasMatching = true; break; }
                }
                if (!hasMatching) continue;

                DirectX::XMMATRIX subWorld = sub->localMatrix * carWorld;
                cb.World = DirectX::XMMatrixTranspose(subWorld);
                cb.WorldViewProj = DirectX::XMMatrixTranspose(subWorld * view * proj);

                if (!sub->boneMatrices.empty())
                {
                    size_t numB = (std::min)((size_t)256, sub->boneMatrices.size());
                    for (size_t b = 0; b < numB; ++b)
                    {
                        bones.BoneTransforms[b] = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&sub->boneMatrices[b]));
                    }
                }
                else
                {
                    for (int b = 0; b < 256; ++b) bones.BoneTransforms[b] = DirectX::XMMatrixIdentity();
                }

                pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);
                pState->pD3DContext->UpdateSubresource(pState->pBoneConstantBuffer, 0, NULL, &bones, 0, 0);

                pState->pD3DContext->VSSetConstantBuffers(0, 1, &pState->pConstantBuffer);
                pState->pD3DContext->VSSetConstantBuffers(1, 1, &pState->pBoneConstantBuffer);
                pState->pD3DContext->PSSetConstantBuffers(0, 1, &pState->pConstantBuffer);

                UINT vstride = sizeof(GPUVertex);
                UINT voffset = 0;
                pState->pD3DContext->IASetVertexBuffers(0, 1, &sub->shape.pVertexBuffer, &vstride, &voffset);
                pState->pD3DContext->IASetIndexBuffer(sub->shape.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

                for (const auto& sm : sub->shape.subMeshes)
                {
                    bool match = (passMode == 0 && !sm.isTransparent) ||
                                 (passMode == 1 && sm.isTransparent && sm.isAlphaTest) ||
                                 (passMode == 2 && sm.isTransparent && !sm.isAlphaTest);
                    if (match)
                    {
                        ID3D11ShaderResourceView* srv = sm.pSRV ? sm.pSRV :
                            (sub->pTextureLoader ? sub->pTextureLoader->GetDefaultTexture() : pState->pTextureLoader->GetDefaultTexture());
                        pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                        pState->pD3DContext->DrawIndexed(sm.indexCount, sm.startIndex, 0);
                    }
                }
            }
        }
    };

    RenderConsistPass(0); // Pass 1: Opaque
    RenderConsistPass(1); // Pass 2: Decals & Cutouts
    RenderConsistPass(2); // Pass 3: Translucent Glass

    pState->pSwapChain->Present(0, 0);
}

// -------------------------------------------------------------
// Layout & Track Coordinates Calculation
// -------------------------------------------------------------
static float CalculateLeftAlignedFocusZ(VisualConsistState* pState, int viewportW, int viewportH)
{
    if (viewportH <= 0) viewportH = 150;
    if (viewportW <= 0) viewportW = 1000;
    float aspect = (float)viewportW / (float)viewportH;
    float halfVisibleWidthM = pState->camDist * tanf(DirectX::XMConvertToRadians(12.0f)) * aspect;
    // Set a clean 3.0m margin ahead of the front locomotive nose (at Z = 0.0m)
    return (3.0f - halfVisibleWidthM);
}

static int HitTestUnit3D(VisualConsistState* pState, int mouseX, int mouseY, int vpW, int vpH)
{
    if (!pState || pState->units.empty() || vpW <= 0 || vpH <= 0) return -1;

    float aspect = (float)vpW / (float)vpH;
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(24.0f), aspect, 0.2f, 1500.0f);

    float eyeX = pState->camDist * cosf(pState->camPitch) * sinf(pState->camYaw);
    float eyeY = pState->camCenterY + pState->camDist * sinf(pState->camPitch);
    float eyeZ = pState->camFocusZ + pState->camDist * cosf(pState->camPitch) * cosf(pState->camYaw);

    DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
    DirectX::XMVECTOR targetPos = DirectX::XMVectorSet(0.0f, pState->camCenterY, pState->camFocusZ, 1.0f);
    DirectX::XMVECTOR upVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePos, targetPos, upVec);

    DirectX::XMMATRIX viewProj = view * proj;
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);

    float ndcX = (2.0f * mouseX / (float)vpW) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / (float)vpH);

    DirectX::XMVECTOR nearPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
    DirectX::XMVECTOR farPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
    DirectX::XMVECTOR rayDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farPoint, nearPoint));

    DirectX::XMFLOAT3 rOrig, rDir;
    DirectX::XMStoreFloat3(&rOrig, nearPoint);
    DirectX::XMStoreFloat3(&rDir, rayDir);

    int bestIdx = -1;
    float bestT = 1e9f;

    float halfVisibleWidthM = pState->camDist * tanf(DirectX::XMConvertToRadians(12.0f)) * aspect;
    float visZMin = -1e9f;
    float visZMax = 1e9f;

    if (!pState->bOrbitMode)
    {
        visZMin = pState->camFocusZ - halfVisibleWidthM - 15.0f;
        visZMax = pState->camFocusZ + halfVisibleWidthM + 15.0f;
    }
    else
    {
        float orbitDepth = (std::max)(150.0f, pState->camDist * 4.0f + 250.0f);
        visZMin = pState->camFocusZ - orbitDepth;
        visZMax = pState->camFocusZ + orbitDepth;
    }

    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        const auto& u = pState->units[i];
        if (u.isPhantom) continue;

        float boxMinZ = (std::min)(u.zRear, u.zFront);
        float boxMaxZ = (std::max)(u.zRear, u.zFront);
        if (boxMaxZ < visZMin || boxMinZ > visZMax) continue;

        float boxMinX = -1.65f, boxMaxX = 1.65f;
        float boxMinY = 0.0f,  boxMaxY = 4.4f;

        float tmin = -1e9f, tmax = 1e9f;

        // X axis
        if (std::abs(rDir.x) > 1e-6f) {
            float t1 = (boxMinX - rOrig.x) / rDir.x;
            float t2 = (boxMaxX - rOrig.x) / rDir.x;
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
        } else if (rOrig.x < boxMinX || rOrig.x > boxMaxX) continue;

        // Y axis
        if (std::abs(rDir.y) > 1e-6f) {
            float t1 = (boxMinY - rOrig.y) / rDir.y;
            float t2 = (boxMaxY - rOrig.y) / rDir.y;
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
        } else if (rOrig.y < boxMinY || rOrig.y > boxMaxY) continue;

        // Z axis
        if (std::abs(rDir.z) > 1e-6f) {
            float t1 = (boxMinZ - rOrig.z) / rDir.z;
            float t2 = (boxMaxZ - rOrig.z) / rDir.z;
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
        } else if (rOrig.z < boxMinZ || rOrig.z > boxMaxZ) continue;

        if (tmax >= tmin && tmax >= 0.0f)
        {
            float hitT = (tmin >= 0.0f) ? tmin : tmax;
            if (hitT < bestT)
            {
                bestT = hitT;
                bestIdx = (int)i;
            }
        }
    }

    return bestIdx;
}

static void RecalculateConsist3DLayout(VisualConsistState* pState)
{
    if (!pState || pState->units.empty())
    {
        pState->trainTotalLength = 0.0f;
        pState->trainZMin = 0.0f;
        pState->trainZMax = 0.0f;
        RebuildTrackMesh(pState);
        return;
    }

    float currentCouplerZ = 0.0f;
    bool isFirstPhysical = true;
    size_t lastPhysicalIdx = (size_t)-1;

    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        auto& u = pState->units[i];
        if (u.isPhantom || u.length <= 0.01f)
        {
            u.length = 0.0f;
            if (lastPhysicalIdx != (size_t)-1)
            {
                u.zCenter = pState->units[lastPhysicalIdx].zCenter;
                u.zFront  = pState->units[lastPhysicalIdx].zFront;
                u.zRear   = pState->units[lastPhysicalIdx].zRear;
            }
            else
            {
                u.zCenter = currentCouplerZ;
                u.zFront  = currentCouplerZ;
                u.zRear   = currentCouplerZ;
            }
            continue;
        }

        float halfL = u.length * 0.5f;
        if (isFirstPhysical)
        {
            u.zCenter = -halfL; // Front nose of locomotive at Z = 0.0f
            u.zFront = 0.0f;
            u.zRear = -u.length;
            currentCouplerZ = u.zRear;
            isFirstPhysical = false;
            lastPhysicalIdx = i;

            // Back-propagate to any phantom units that were ahead of first physical
            for (size_t k = 0; k < i; ++k)
            {
                pState->units[k].zCenter = u.zCenter;
                pState->units[k].zFront  = u.zFront;
                pState->units[k].zRear   = u.zRear;
            }
        }
        else
        {
            u.zFront = currentCouplerZ;
            u.zCenter = currentCouplerZ - halfL;
            u.zRear = currentCouplerZ - u.length;
            currentCouplerZ = u.zRear;
            lastPhysicalIdx = i;
        }
    }

    pState->trainZMax = 0.0f;
    pState->trainZMin = currentCouplerZ;
    pState->trainTotalLength = std::abs(pState->trainZMax - pState->trainZMin);

    // Update ScrollBar Range (3.0m margin ahead of front locomotive + 3.0m margin behind rear vehicle)
    RECT rcVp;
    GetClientRect(pState->hViewportWnd ? pState->hViewportWnd : pState->hWnd, &rcVp);
    int vpW = rcVp.right - rcVp.left;
    int vpH = rcVp.bottom - rcVp.top;
    if (vpH <= 0) vpH = 150;
    if (vpW <= 0) vpW = 1000;
    float aspect = (float)vpW / (float)vpH;
    float halfVisibleWidthM = pState->camDist * tanf(DirectX::XMConvertToRadians(12.0f)) * aspect;

    float totalSpanM = pState->trainTotalLength + 6.0f; // 3.0m on left + 3.0m on right
    float visibleWidthM = halfVisibleWidthM * 2.0f;

    pState->scrollBar.SetRange(0, (std::max)(1, (int)(totalSpanM * 10.0f)), (int)(visibleWidthM * 10.0f));

    RebuildTrackMesh(pState);
}

static std::wstring ResolveStockPath(const std::wstring& basePath, const std::wstring& folder, const std::wstring& uid, bool isEngine)
{
    if (folder.empty() || uid.empty()) return L"";
    std::wstring p = basePath;
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"TRAINS\\TRAINSET\\" + folder + L"\\" + uid;

    // 1. Direct check
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;

    // 2. Check if filename already ends with .eng or .wag
    std::wstring lowerP = p;
    for (wchar_t& c : lowerP) c = towlower(c);
    if (lowerP.size() >= 4 && (lowerP.substr(lowerP.size() - 4) == L".eng" || lowerP.substr(lowerP.size() - 4) == L".wag"))
    {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }

    std::wstring extPrimary = isEngine ? L".eng" : L".wag";
    std::wstring extSecondary = isEngine ? L".wag" : L".eng";

    if (GetFileAttributesW((p + extPrimary).c_str()) != INVALID_FILE_ATTRIBUTES) return p + extPrimary;
    if (GetFileAttributesW((p + extSecondary).c_str()) != INVALID_FILE_ATTRIBUTES) return p + extSecondary;
    if (GetFileAttributesW((p + L".ENG").c_str()) != INVALID_FILE_ATTRIBUTES) return p + L".ENG";
    if (GetFileAttributesW((p + L".WAG").c_str()) != INVALID_FILE_ATTRIBUTES) return p + L".WAG";

    // 3. Direct folder relative check (in case basePath is already TRAINSET)
    std::wstring p2 = basePath;
    if (!p2.empty() && p2.back() != L'\\' && p2.back() != L'/') p2 += L'\\';
    p2 += folder + L"\\" + uid;
    if (GetFileAttributesW((p2 + extPrimary).c_str()) != INVALID_FILE_ATTRIBUTES) return p2 + extPrimary;
    if (GetFileAttributesW((p2 + extSecondary).c_str()) != INVALID_FILE_ATTRIBUTES) return p2 + extSecondary;
    if (GetFileAttributesW(p2.c_str()) != INVALID_FILE_ATTRIBUTES) return p2;

    return L"";
}

struct UnitLoadedPayload {
    uint32_t gen;
    size_t index;
    CompositeStockUnit* pUnit;
};

// -------------------------------------------------------------
// Asynchronous Background Consist Loader with Shared Model Caching & Gen Safety
// -------------------------------------------------------------
static void StartBackgroundLoading(VisualConsistState* pState, HWND hWnd)
{
    if (!pState || pState->units.empty()) return;

    uint32_t currentGen = ++pState->loadGeneration;
    std::wstring basePath = pState->basePath;

    struct UnitTask {
        size_t index;
        std::wstring stockPath;
        bool isEngine;
        bool isMissing;
    };

    std::vector<UnitTask> tasks;
    tasks.reserve(pState->units.size());
    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        UnitTask t;
        t.index = i;
        t.stockPath = pState->units[i].stockFilePath;
        t.isEngine = pState->units[i].isEngine;
        t.isMissing = pState->units[i].isMissing;
        tasks.push_back(t);
    }

    std::thread([hWnd, currentGen, basePath, tasks, pState]() {
        // Step 1: Collect unique stock paths to eliminate duplicate disk reads
        std::unordered_set<std::wstring> uniquePaths;
        for (const auto& t : tasks)
        {
            if (!t.isMissing && !t.stockPath.empty())
            {
                uniquePaths.insert(t.stockPath);
            }
            else
            {
                // Immediate dispatch for missing stock files
                if (pState->loadGeneration == currentGen)
                {
                    auto pPayload = new UnitLoadedPayload{ currentGen, t.index, nullptr };
                    PostMessageW(hWnd, WM_VCV_UNIT_LOADED, 0, (LPARAM)pPayload);
                }
            }
        }

        // Step 2: Load unique stock models in parallel and dispatch to UI
        std::vector<std::future<void>> futures;
        futures.reserve(uniquePaths.size());

        for (const auto& path : uniquePaths)
        {
            futures.push_back(std::async(std::launch::async, [hWnd, path, basePath, currentGen, pState, tasks]() {
                if (pState->loadGeneration != currentGen) return;

                CompositeStockUnit unit;
                bool ok = CompositeStockLoader::LoadCompositeStockCPU(path, basePath, unit) && !unit.subShapes.empty();
                if (pState->loadGeneration != currentGen) return;

                // Dispatch to UI thread for all matching units in consist
                for (const auto& t : tasks)
                {
                    if (pState->loadGeneration != currentGen) return;
                    if (t.stockPath == path)
                    {
                        auto pUnit = ok ? new CompositeStockUnit(unit) : nullptr;
                        auto pPayload = new UnitLoadedPayload{ currentGen, t.index, pUnit };
                        PostMessageW(hWnd, WM_VCV_UNIT_LOADED, 0, (LPARAM)pPayload);
                    }
                }
            }));
        }

        // Keep thread alive until all async jobs finish
        for (auto& fut : futures)
        {
            fut.get();
        }
    }).detach();
}

// -------------------------------------------------------------
// UI Layout Calculation (Header, Viewport, ScrollBar, Footer)
// -------------------------------------------------------------
static void RecalcVisualConsistLayout(VisualConsistState* pState, int clientW, int clientH)
{
    pState->rcHeader = { 0, 0, clientW, HEADER_HEIGHT };

    int btnW = 28;
    int btnH = HEADER_HEIGHT - 4;
    int btnY = 2;
    int curRight = clientW - 6;

    if (pState->isFloating)
    {
        pState->rcBtnClose = { 0, 0, 0, 0 };
        pState->rcBtnFloat = { 0, 0, 0, 0 };
        pState->rcBtnCollapse = { 0, 0, 0, 0 };

        // 1. Settings Button
        pState->rcBtnSettings = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 2. Wireframe Button
        pState->rcBtnWireframe = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 3. 3D Orbit Toggle Button
        pState->rcBtnOrbit3D = { curRight - 94, btnY, curRight, btnY + btnH };
        curRight -= (94 + 4);

        // 4. Side View Reset Button (TSRE5 Mode)
        pState->rcBtnSideView = { curRight - 76, btnY, curRight, btnY + btnH };
        curRight -= (76 + 4);
    }
    else
    {
        pState->rcBtnClose = { 0, 0, 0, 0 };

        // 1. Collapse / Expand Chevron
        pState->rcBtnCollapse = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 2. Pop-out / Float Button
        pState->rcBtnFloat = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 3. Settings Button
        pState->rcBtnSettings = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 4. Wireframe Button
        pState->rcBtnWireframe = { curRight - btnW, btnY, curRight, btnY + btnH };
        curRight -= (btnW + 4);

        // 5. 3D Orbit Toggle Button
        pState->rcBtnOrbit3D = { curRight - 94, btnY, curRight, btnY + btnH };
        curRight -= (94 + 4);

        // 6. Side View Reset Button
        pState->rcBtnSideView = { curRight - 76, btnY, curRight, btnY + btnH };
        curRight -= (76 + 4);
    }

    int footerH = pState->isFloating ? 40 : 0;
    int scrollbarH = 12;
    int unitBarH = 22; // Unit numbering display strip (1, 2, 3...)
    pState->rcFooter = { 0, clientH - footerH, clientW, clientH };
    pState->rcBtnFooterClose = { clientW - 120, clientH - footerH + 6, clientW - 12, clientH - 8 };
    pState->rcChkStartupCollapse = { 16, clientH - footerH + 8, 300, clientH - 8 };

    // Viewport bounds
    int vpTop = HEADER_HEIGHT;
    int vpH = (std::max)(10, clientH - vpTop - footerH - unitBarH - scrollbarH);

    if (pState->hViewportWnd && IsWindow(pState->hViewportWnd))
    {
        SetWindowPos(pState->hViewportWnd, NULL, 0, vpTop, clientW, vpH, SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeD3D11(pState, clientW, vpH);
    }

    pState->rcUnitBar = { 0, vpTop + vpH, clientW, vpTop + vpH + unitBarH };

    RECT rcScroll = { 0, vpTop + vpH + unitBarH, clientW, vpTop + vpH + unitBarH + scrollbarH };
    pState->scrollBar.SetBounds(rcScroll);
}

// -------------------------------------------------------------
// Bottom Unit Numbers Strip (1, 2, 3... with Badges & Flip Tags)
// -------------------------------------------------------------
static void PaintUnitNumbersBar(HWND hWnd, HDC hdc, VisualConsistState* pState)
{
    if (!pState || pState->rcUnitBar.bottom <= pState->rcUnitBar.top)
        return;

    RECT rc = pState->rcUnitBar;
    int barW = rc.right - rc.left;
    int barH = rc.bottom - rc.top;
    if (barW <= 0 || barH <= 0) return;

    // Reset unitBadgeRects
    pState->unitBadgeRects.assign(pState->units.size(), RECT{ 0, 0, 0, 0 });

    // Background
    HBRUSH hbrBg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &rc, hbrBg);
    DeleteObject(hbrBg);

    // Top hairline border
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
    HPEN hOldP = (HPEN)SelectObject(hdc, hPenBorder);
    MoveToEx(hdc, rc.left, rc.top, NULL);
    LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, hOldP);
    DeleteObject(hPenBorder);

    if (pState->units.empty())
        return;

    RECT rcVp;
    GetClientRect(pState->hViewportWnd ? pState->hViewportWnd : hWnd, &rcVp);
    int vpW = rcVp.right - rcVp.left;
    int vpH = rcVp.bottom - rcVp.top;
    if (vpW <= 0 || vpH <= 0) return;

    float aspect = (float)vpW / (float)(std::max)(1, vpH);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(24.0f), aspect, 0.2f, 1500.0f);

    float eyeX = pState->camDist * cosf(pState->camPitch) * sinf(pState->camYaw);
    float eyeY = pState->camCenterY + pState->camDist * sinf(pState->camPitch);
    float eyeZ = pState->camFocusZ + pState->camDist * cosf(pState->camPitch) * cosf(pState->camYaw);

    DirectX::XMVECTOR eyePos = DirectX::XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
    DirectX::XMVECTOR targetPos = DirectX::XMVectorSet(0.0f, pState->camCenterY, pState->camFocusZ, 1.0f);
    DirectX::XMVECTOR upVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePos, targetPos, upVec);
    DirectX::XMMATRIX viewProj = view * proj;

    HFONT hOldFont = pState->hFontBadge ? (HFONT)SelectObject(hdc, pState->hFontBadge) : (pState->hFontTitle ? (HFONT)SelectObject(hdc, pState->hFontTitle) : NULL);
    SetBkMode(hdc, TRANSPARENT);

    // Group units into physical host vehicle slots
    struct ConsistSlot {
        size_t hostUnitIndex = (size_t)-1;
        float zFront = 0.0f;
        float zRear = 0.0f;
        float zCenter = 0.0f;
        std::vector<size_t> unitIndices;
    };
    std::vector<ConsistSlot> slots;

    // Step 1: Create slots for every physical host vehicle (units with length > 0.1m)
    for (size_t i = 0; i < pState->units.size(); ++i)
    {
        if (!pState->units[i].isPhantom && pState->units[i].length > 0.1f)
        {
            ConsistSlot slot;
            slot.hostUnitIndex = i;
            slot.zFront = pState->units[i].zFront;
            slot.zRear = pState->units[i].zRear;
            slot.zCenter = pState->units[i].zCenter;
            slots.push_back(slot);
        }
    }

    if (slots.empty())
    {
        // Fallback: If no physical units exist in consist
        ConsistSlot slot;
        slot.hostUnitIndex = 0;
        slot.zFront = 0.0f;
        slot.zRear = -10.0f;
        slot.zCenter = -5.0f;
        for (size_t i = 0; i < pState->units.size(); ++i)
            slot.unitIndices.push_back(i);
        slots.push_back(slot);
    }
    else
    {
        // Step 2: Assign all units (0 ... N-1) to the appropriate host vehicle slot
        size_t currentSlotIdx = 0;
        for (size_t i = 0; i < pState->units.size(); ++i)
        {
            if (pState->units[i].isPhantom || pState->units[i].length <= 0.1f)
            {
                // Dummy/phantom units before first physical vehicle go into slots[0],
                // otherwise into the current physical host vehicle's slot
                slots[currentSlotIdx].unitIndices.push_back(i);
            }
            else
            {
                for (size_t s = 0; s < slots.size(); ++s)
                {
                    if (slots[s].hostUnitIndex == i)
                    {
                        currentSlotIdx = s;
                        slots[currentSlotIdx].unitIndices.push_back(i);
                        break;
                    }
                }
            }
        }
    }

    for (const auto& slot : slots)
    {
        // Project slot endpoints to screen X
        DirectX::XMVECTOR vF = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, slot.zFront, 1.0f), viewProj);
        DirectX::XMVECTOR vR = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, slot.zRear, 1.0f), viewProj);
        DirectX::XMVECTOR vC = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(0.0f, 0.0f, slot.zCenter, 1.0f), viewProj);

        float xF = (DirectX::XMVectorGetX(vF) + 1.0f) * 0.5f * (float)vpW;
        float xR = (DirectX::XMVectorGetX(vR) + 1.0f) * 0.5f * (float)vpW;
        float xC = (DirectX::XMVectorGetX(vC) + 1.0f) * 0.5f * (float)vpW;

        int leftX = (int)std::floor((std::min)(xF, xR));
        int rightX = (int)std::ceil((std::max)(xF, xR));
        int centerX = (int)std::round(xC);

        if (rightX < 0 || leftX > barW) continue; // Outside visible screen

        int spanW = rightX - leftX;
        if (spanW < 20) spanW = 20;

        size_t count = slot.unitIndices.size();
        int spacing = 3;
        int badgeH = barH - 4;
        int badgeY = rc.top + 2;

        int badgeW = 0;
        if (count == 1)
        {
            badgeW = (std::max)(26, (std::min)(spanW - 4, 80));
        }
        else
        {
            int maxAllowed = (spanW - 4 - (int)(count - 1) * spacing) / (int)count;
            badgeW = (std::max)(22, (std::min)(maxAllowed, 56));
        }

        int totalClusterW = (int)count * badgeW + (int)(count - 1) * spacing;
        int startX = centerX - totalClusterW / 2;
        if (startX < leftX + 1) startX = leftX + 1;
        if (startX + totalClusterW > rightX - 1) startX = (rightX - 1) - totalClusterW;
        if (startX < 0) startX = 0;

        for (size_t k = 0; k < count; ++k)
        {
            size_t uIdx = slot.unitIndices[k];
            const auto& u = pState->units[uIdx];

            int curBadgeX = startX + (int)k * (badgeW + spacing);
            RECT rcBadge = { curBadgeX, badgeY, curBadgeX + badgeW, badgeY + badgeH };
            pState->unitBadgeRects[uIdx] = rcBadge;

            bool isSel = ((int)uIdx == pState->selectedIndex);
            bool isHov = ((int)uIdx == pState->hoverIndex || (int)uIdx == pState->hoverUnitBarIndex);

            // Badge styling
            COLORREF bgCol, borderCol, textCol;
            if (u.isMissingStock)
            {
                // Crimson Red (Missing .eng / .wag file or folder)
                bgCol = isSel ? RGB(210, 30, 40) : (isHov ? RGB(160, 25, 35) : RGB(115, 20, 25));
                borderCol = isSel ? RGB(255, 90, 100) : (isHov ? RGB(230, 60, 70) : RGB(180, 40, 45));
                textCol = RGB(255, 235, 235);
            }
            else if (u.isMissingShape)
            {
                // Warning Amber / Orange (Missing .s shape file)
                bgCol = isSel ? RGB(215, 120, 20) : (isHov ? RGB(170, 95, 15) : RGB(125, 68, 10));
                borderCol = isSel ? RGB(255, 175, 50) : (isHov ? RGB(235, 150, 35) : RGB(185, 115, 25));
                textCol = RGB(255, 245, 230);
            }
            else if (isSel)
            {
                bgCol = RGB(220, 25, 125);
                borderCol = RGB(255, 60, 160);
                textCol = RGB(255, 255, 255);
            }
            else if (isHov)
            {
                bgCol = RGB(52, 58, 72);
                borderCol = RGB(85, 95, 115);
                textCol = RGB(245, 248, 255);
            }
            else if (u.isPhantom)
            {
                bgCol = RGB(24, 28, 38);
                borderCol = RGB(40, 48, 62);
                textCol = RGB(160, 175, 195);
            }
            else
            {
                bgCol = RGB(28, 31, 38);
                borderCol = RGB(45, 50, 60);
                textCol = RGB(175, 185, 200);
            }

            HBRUSH hbrBadge = CreateSolidBrush(bgCol);
            HPEN hpenBadge = CreatePen(PS_SOLID, 1, borderCol);
            HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hbrBadge);
            HPEN hOldP2 = (HPEN)SelectObject(hdc, hpenBadge);

            RoundRect(hdc, rcBadge.left, rcBadge.top, rcBadge.right, rcBadge.bottom, 4, 4);

            SelectObject(hdc, hOldB);
            SelectObject(hdc, hOldP2);
            DeleteObject(hbrBadge);
            DeleteObject(hpenBadge);

            wchar_t szNum[64];
            if (u.isMissing)
            {
                swprintf_s(szNum, 64, L"#%d \x26A0", (int)(uIdx + 1));
            }
            else if (u.isFlipped)
            {
                swprintf_s(szNum, 64, L"#%d \x2B82", (int)(uIdx + 1));
            }
            else
            {
                swprintf_s(szNum, 64, L"#%d", (int)(uIdx + 1));
            }

            SetTextColor(hdc, textCol);
            DrawTextW(hdc, szNum, -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    if (hOldFont) SelectObject(hdc, hOldFont);
}

// -------------------------------------------------------------
// Header Painting (GDI / Direct Paint)
// -------------------------------------------------------------
static void PaintHeader(HWND hWnd, HDC hdc, VisualConsistState* pState)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
    HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

    // Background
    HBRUSH hbrBg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(memDC, &pState->rcHeader, hbrBg);
    DeleteObject(hbrBg);

    // Top Separator Border Line (Section 3 Dock Separator)
    HPEN hPenTopBorder = CreatePen(PS_SOLID, 1, RGB(65, 65, 65));
    HPEN hOldPT = (HPEN)SelectObject(memDC, hPenTopBorder);
    MoveToEx(memDC, 0, 0, NULL);
    LineTo(memDC, rcClient.right, 0);
    SelectObject(memDC, hOldPT);
    DeleteObject(hPenTopBorder);

    // Bottom Border Line
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
    HPEN hOldP = (HPEN)SelectObject(memDC, hPenBorder);
    MoveToEx(memDC, 0, HEADER_HEIGHT - 1, NULL);
    LineTo(memDC, rcClient.right, HEADER_HEIGHT - 1);
    SelectObject(memDC, hOldP);
    DeleteObject(hPenBorder);

    SetBkMode(memDC, TRANSPARENT);

    // 1. Title
    int curX = 12;
    if (pState->hFontTitle)
    {
        SelectObject(memDC, pState->hFontTitle);
        SetTextColor(memDC, RGB(240, 243, 248));
        RECT rcTitle = { curX, 0, curX + 160, HEADER_HEIGHT };
        DrawTextW(memDC, L"3D Visual Consist View", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        curX += 170;
    }

    // Right boundary for badges is curRight of buttons
    int maxBadgeRight = pState->rcBtnSideView.left > 0 ? (pState->rcBtnSideView.left - 10) : (rcClient.right - 320);

    // 2. Consist Name Badge
    std::wstring dispConsist = pState->consistTitle;
    if (dispConsist.empty() && !pState->consistFileName.empty()) dispConsist = pState->consistFileName;
    else if (!dispConsist.empty() && !pState->consistFileName.empty() && dispConsist != pState->consistFileName) {
        dispConsist += L" [" + pState->consistFileName + L"]";
    }

    if (!dispConsist.empty() && curX < maxBadgeRight - 120)
    {
        SIZE szCon = {};
        if (pState->hFontMetrics) SelectObject(memDC, pState->hFontMetrics);
        GetTextExtentPoint32W(memDC, dispConsist.c_str(), (int)dispConsist.length(), &szCon);
        int badgeW = szCon.cx + 18;
        int maxAllowedW = maxBadgeRight - curX - 170; // Reserve at least 170px for status pill
        if (badgeW > maxAllowedW) badgeW = maxAllowedW;
        if (badgeW > 50)
        {
            RECT rcConBadge = { curX, 3, curX + badgeW, HEADER_HEIGHT - 3 };
            HBRUSH hbrCon = CreateSolidBrush(RGB(28, 34, 46));
            HPEN hpCon = CreatePen(PS_SOLID, 1, RGB(55, 70, 95));
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hbrCon);
            HPEN hOldP = (HPEN)SelectObject(memDC, hpCon);
            RoundRect(memDC, rcConBadge.left, rcConBadge.top, rcConBadge.right, rcConBadge.bottom, 4, 4);
            SelectObject(memDC, hOldB);
            SelectObject(memDC, hOldP);
            DeleteObject(hbrCon);
            DeleteObject(hpCon);

            RECT rcText = { rcConBadge.left + 8, rcConBadge.top, rcConBadge.right - 8, rcConBadge.bottom };
            SetTextColor(memDC, RGB(170, 215, 255));
            DrawTextW(memDC, dispConsist.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            curX += badgeW + 8;
        }
    }

    // 3. Status & Metrics Pill
    if (curX < maxBadgeRight - 30)
    {
        wchar_t szStatus[128] = { 0 };
        COLORREF bgPill = RGB(28, 32, 40);
        COLORREF borderPill = RGB(48, 56, 70);
        COLORREF textPill = RGB(160, 175, 195);

        int missingStockCount = 0;
        int missingShapeCount = 0;
        for (const auto& u : pState->units)
        {
            if (u.isMissingStock) missingStockCount++;
            else if (u.isMissingShape) missingShapeCount++;
        }

        if (pState->units.empty())
        {
            wcscpy_s(szStatus, L"● Standby");
            bgPill = RGB(26, 28, 34);
            borderPill = RGB(45, 48, 58);
            textPill = RGB(140, 145, 160);
        }
        else if (pState->isLoading && pState->loadedUnitCount < (int)pState->units.size())
        {
            int pct = (int)pState->units.size() > 0 ? (pState->loadedUnitCount * 100 / (int)pState->units.size()) : 0;
            swprintf_s(szStatus, 128, L"● Loading %d / %d (%d%%)", pState->loadedUnitCount, (int)pState->units.size(), pct);
            bgPill = RGB(45, 35, 18);
            borderPill = RGB(120, 90, 30);
            textPill = RGB(255, 205, 110);
        }
        else if (missingStockCount > 0 || missingShapeCount > 0)
        {
            if (missingStockCount > 0 && missingShapeCount > 0)
            {
                swprintf_s(szStatus, 128, L"● Ready • %d Units (%.1f m) • %d Missing Stock • %d Missing Shape \x26A0", 
                    (int)pState->units.size(), pState->trainTotalLength, missingStockCount, missingShapeCount);
                bgPill = RGB(55, 24, 26);
                borderPill = RGB(150, 42, 48);
                textPill = RGB(255, 140, 140);
            }
            else if (missingStockCount > 0)
            {
                swprintf_s(szStatus, 128, L"● Ready • %d Units (%.1f m) • %d Missing Stock \x26A0", 
                    (int)pState->units.size(), pState->trainTotalLength, missingStockCount);
                bgPill = RGB(55, 24, 26);
                borderPill = RGB(150, 42, 48);
                textPill = RGB(255, 140, 140);
            }
            else
            {
                swprintf_s(szStatus, 128, L"● Ready • %d Units (%.1f m) • %d Missing Shape \x26A0", 
                    (int)pState->units.size(), pState->trainTotalLength, missingShapeCount);
                bgPill = RGB(55, 36, 15);
                borderPill = RGB(155, 95, 25);
                textPill = RGB(255, 195, 110);
            }
        }
        else
        {
            swprintf_s(szStatus, 128, L"● Ready • %d Units (%.1f m)", (int)pState->units.size(), pState->trainTotalLength);
            bgPill = RGB(18, 42, 28);
            borderPill = RGB(35, 95, 55);
            textPill = RGB(120, 235, 160);
        }

        SIZE szSt = {};
        if (pState->hFontMetrics) SelectObject(memDC, pState->hFontMetrics);
        GetTextExtentPoint32W(memDC, szStatus, (int)wcslen(szStatus), &szSt);
        int pillW = szSt.cx + 18;
        if (curX + pillW > maxBadgeRight) pillW = maxBadgeRight - curX;

        if (pillW > 30)
        {
            RECT rcPill = { curX, 3, curX + pillW, HEADER_HEIGHT - 3 };
            HBRUSH hbrP = CreateSolidBrush(bgPill);
            HPEN hpP = CreatePen(PS_SOLID, 1, borderPill);
            HBRUSH hOldB = (HBRUSH)SelectObject(memDC, hbrP);
            HPEN hOldP = (HPEN)SelectObject(memDC, hpP);
            RoundRect(memDC, rcPill.left, rcPill.top, rcPill.right, rcPill.bottom, 4, 4);
            SelectObject(memDC, hOldB);
            SelectObject(memDC, hOldP);
            DeleteObject(hbrP);
            DeleteObject(hpP);

            RECT rcText = { rcPill.left + 8, rcPill.top, rcPill.right - 8, rcPill.bottom };
            SetTextColor(memDC, textPill);
            DrawTextW(memDC, szStatus, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    // Helper: Draw Button
    auto DrawHeaderButton = [&](RECT rc, const wchar_t* icon, const wchar_t* text, bool isHover, bool isActive) {
        if (rc.right <= rc.left) return;

        if (isHover || isActive)
        {
            HBRUSH hbrBtn = CreateSolidBrush(isActive ? RGB(0, 120, 215) : RGB(45, 50, 60));
            FillRect(memDC, &rc, hbrBtn);
            DeleteObject(hbrBtn);
        }

        if (text && pState->hFontTitle)
        {
            SelectObject(memDC, pState->hFontTitle);
            SetTextColor(memDC, isHover ? RGB(255, 255, 255) : (isActive ? RGB(255, 255, 255) : RGB(200, 205, 215)));
            DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (icon && pState->hFontIcons)
        {
            SelectObject(memDC, pState->hFontIcons);
            SetTextColor(memDC, isHover ? RGB(255, 255, 255) : RGB(190, 195, 205));
            DrawTextW(memDC, icon, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };

    // Render Header Action Buttons
    DrawHeaderButton(pState->rcBtnSideView, NULL, L"⟲ Side View", pState->hoverSideView, false);
    DrawHeaderButton(pState->rcBtnOrbit3D, NULL, pState->bOrbitMode ? L"3D Orbit: ON" : L"3D Orbit: OFF", pState->hoverOrbit3D, pState->bOrbitMode);
    DrawHeaderButton(pState->rcBtnWireframe, L"\xE81E", NULL, pState->hoverWireframe, pState->bWireframe);
    DrawHeaderButton(pState->rcBtnSettings, L"\xE713", NULL, pState->hoverSettings, false);

    if (!pState->isFloating)
    {
        DrawHeaderButton(pState->rcBtnFloat, L"\xE8A7", NULL, pState->hoverFloat, false);
        DrawHeaderButton(pState->rcBtnCollapse, pState->isCollapsed ? L"\xE70E" : L"\xE70D", NULL, pState->hoverCollapse, false);
    }

    // Floating Footer Bar
    if (pState->isFloating)
    {
        HBRUSH hbrF = CreateSolidBrush(RGB(22, 24, 28));
        FillRect(memDC, &pState->rcFooter, hbrF);
        DeleteObject(hbrF);

        // Footer Close Button
        HBRUSH hbrClose = CreateSolidBrush(pState->hoverFooterClose ? RGB(60, 65, 75) : RGB(40, 44, 52));
        FillRect(memDC, &pState->rcBtnFooterClose, hbrClose);
        DeleteObject(hbrClose);

        SelectObject(memDC, pState->hFontTitle);
        SetTextColor(memDC, RGB(235, 240, 250));
        DrawTextW(memDC, L"Dock to Main Window", -1, &pState->rcBtnFooterClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

// -------------------------------------------------------------
// 3D Viewport Window Procedure
// -------------------------------------------------------------
static LRESULT CALLBACK ViewportWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (pState) Render3DConsist(pState);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            ResizeD3D11(pState, w, h);
            Render3DConsist(pState);
        }
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (pState)
        {
            if (pState->bOrbitMode)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                return TRUE;
            }
            else
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd, &pt);
                RECT rc;
                GetClientRect(hWnd, &rc);
                int vpW = rc.right - rc.left;
                int vpH = rc.bottom - rc.top;
                int hitIdx = HitTestUnit3D(pState, pt.x, pt.y, vpW, vpH);
                if (hitIdx >= 0)
                {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
                else
                {
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        SetCapture(hWnd);
        SetFocus(hWnd);

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (pState->bOrbitMode)
        {
            pState->isDragging3D = true;
            pState->isPanningZ = (wParam & MK_SHIFT) != 0;
            pState->dragStartPt = { x, y };
            pState->dragStartYaw = pState->camYaw;
            pState->dragStartPitch = pState->camPitch;
            pState->dragStartFocusZ = pState->camFocusZ;
        }
        else
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int vpW = rc.right - rc.left;
            int vpH = rc.bottom - rc.top;
            int hitIdx = HitTestUnit3D(pState, x, y, vpW, vpH);
            if (hitIdx >= 0)
            {
                pState->selectedIndex = hitIdx;
                SendMessage(pState->hParent, WM_COMMAND, MAKEWPARAM(pState->controlId, LBN_SELCHANGE), (LPARAM)(INT_PTR)hitIdx);
                RebuildTrackMesh(pState);
                InvalidateRect(hWnd, NULL, FALSE);
            }

            pState->isDragging3D = true;
            pState->isPanningZ = true; // Horizontal pan along track
            pState->dragStartPt = { x, y };
            pState->dragStartFocusZ = pState->camFocusZ;
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        if (!pState) break;
        SetCapture(hWnd);
        SetFocus(hWnd);
        pState->isDragging3D = true;
        pState->isPanningZ = true;
        pState->dragStartPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        pState->dragStartFocusZ = pState->camFocusZ;
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (pState->isDragging3D)
        {
            int dx = x - pState->dragStartPt.x;
            int dy = y - pState->dragStartPt.y;

            if (pState->isPanningZ)
            {
                pState->camFocusZ = pState->dragStartFocusZ - dx * 0.15f;

                RECT rc;
                GetClientRect(hWnd, &rc);
                float startFocusZ = CalculateLeftAlignedFocusZ(pState, rc.right - rc.left, rc.bottom - rc.top);
                int scrollVal = (int)((startFocusZ - pState->camFocusZ) * 10.0f);
                pState->scrollBar.SetPos((std::max)(0, (std::min)(pState->scrollBar.GetMaxScrollPos(), scrollVal)));
                InvalidateRect(pState->hWnd, &pState->scrollBar.GetBounds(), FALSE);
            }
            else
            {
                pState->camYaw = pState->dragStartYaw + dx * 0.008f;
                pState->camPitch = pState->dragStartPitch + dy * 0.008f;

                // Clamp Pitch
                pState->camPitch = (std::max)(-1.35f, (std::min)(1.35f, pState->camPitch));
            }

            Render3DConsist(pState);
            return 0;
        }
        else
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int vpW = rc.right - rc.left;
            int vpH = rc.bottom - rc.top;
            int oldHover = pState->hoverIndex;
            pState->hoverIndex = HitTestUnit3D(pState, x, y, vpW, vpH);

            if (oldHover != pState->hoverIndex)
            {
                RebuildTrackMesh(pState);
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        if (!pState) break;
        if (pState->hoverIndex != -1)
        {
            pState->hoverIndex = -1;
            RebuildTrackMesh(pState);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
        if (!pState) break;
        if (pState->isDragging3D)
        {
            pState->isDragging3D = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (!pState) break;
        // Snap back to side-profile view (Facing Left, Left-aligned, Close Scaling)
        RECT rc;
        GetClientRect(hWnd, &rc);
        pState->camYaw = -1.5707963f; // -90 deg
        pState->camPitch = 0.03f;
        pState->camDist = 14.0f;
        pState->camCenterY = 1.9f;
        pState->camFocusZ = CalculateLeftAlignedFocusZ(pState, rc.right - rc.left, rc.bottom - rc.top);
        pState->bOrbitMode = false;
        pState->scrollBar.SetPos(0);
        InvalidateRect(pState->hWnd, &pState->rcHeader, FALSE);
        InvalidateRect(pState->hWnd, &pState->scrollBar.GetBounds(), FALSE);
        Render3DConsist(pState);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        float delta = (float)zDelta / 120.0f;

        if (wParam & MK_CONTROL)
        {
            // Zoom Camera Distance
            pState->camDist = (std::max)(3.0f, (std::min)(120.0f, pState->camDist - delta * 1.5f));
        }
        else
        {
            // Pan along track length: since train goes from left (Z=0) to right (Z<0),
            // scrolling wheel down pans towards rear (negative Z), wheel up pans towards front (positive Z)
            pState->camFocusZ -= delta * 4.5f;

            RECT rc;
            GetClientRect(hWnd, &rc);
            float startFocusZ = CalculateLeftAlignedFocusZ(pState, rc.right - rc.left, rc.bottom - rc.top);
            int scrollVal = (int)((startFocusZ - pState->camFocusZ) * 10.0f);
            pState->scrollBar.SetPos((std::max)(0, (std::min)(pState->scrollBar.GetMaxScrollPos(), scrollVal)));
            InvalidateRect(pState->hWnd, &pState->scrollBar.GetBounds(), FALSE);
        }

        Render3DConsist(pState);
        return 0;
    }

    case WM_MBUTTONDOWN:
    {
        if (!pState) break;
        SetCapture(hWnd);
        pState->isDragging3D = true;
        pState->isPanningZ = true;
        pState->dragStartPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        pState->dragStartFocusZ = pState->camFocusZ;
        return 0;
    }

    case WM_MBUTTONUP:
    {
        if (!pState) break;
        if (pState->isDragging3D)
        {
            pState->isDragging3D = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// -------------------------------------------------------------
// Main Visual Consist View Window Procedure
// -------------------------------------------------------------
static LRESULT CALLBACK VisualConsistWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* pcs = (CREATESTRUCTW*)lParam;
        pState = (VisualConsistState*)pcs->lpCreateParams;
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);

        // Register Viewport Class
        static bool s_vpRegistered = false;
        if (!s_vpRegistered)
        {
            WNDCLASSEXW wcv = { 0 };
            wcv.cbSize = sizeof(wcv);
            wcv.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
            wcv.lpfnWndProc = ViewportWndProc;
            wcv.hInstance = pState->hInst;
            wcv.hCursor = LoadCursor(NULL, IDC_ARROW);
            wcv.hbrBackground = NULL;
            wcv.lpszClassName = L"VisualConsist3DViewportClass";
            RegisterClassExW(&wcv);
            s_vpRegistered = true;
        }

        int scrollbarH = 14;
        int vpH = pcs->cy - HEADER_HEIGHT - scrollbarH;
        if (vpH < 10) vpH = 10;

        pState->hViewportWnd = CreateWindowExW(
            0, L"VisualConsist3DViewportClass", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, HEADER_HEIGHT, pcs->cx, vpH,
            hWnd, (HMENU)9901, pState->hInst, NULL
        );

        SetWindowLongPtrW(pState->hViewportWnd, GWLP_USERDATA, (LONG_PTR)pState);
        InitD3D11(pState, pState->hViewportWnd, pcs->cx, vpH);
        return 0;
    }

    case WM_SIZE:
    {
        if (pState)
        {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            RecalcVisualConsistLayout(pState, w, h);
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
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            HBRUSH hbrBg = CreateSolidBrush(RGB(18, 20, 24));
            FillRect(hdc, &rcClient, hbrBg);
            DeleteObject(hbrBg);

            PaintHeader(hWnd, hdc, pState);
            PaintUnitNumbersBar(hWnd, hdc, pState);
            pState->scrollBar.Paint(hdc, RGB(22, 24, 28), RGB(18, 20, 24));
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
    {
        if (pState && wParam == CustomScrollBar::TIMER_ANIM_ID)
        {
            if (pState->scrollBar.OnTimer(hWnd))
            {
                InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_VCV_UNIT_LOADED:
    {
        if (!pState) break;
        UnitLoadedPayload* pPayload = (UnitLoadedPayload*)lParam;
        if (!pPayload) break;

        if (pPayload->gen != pState->loadGeneration)
        {
            // Stale consist payload from previous selection! Discard immediately with zero overhead!
            delete pPayload->pUnit;
            delete pPayload;
            break;
        }

        size_t idx = pPayload->index;
        CompositeStockUnit* pLoaded = pPayload->pUnit;
        delete pPayload;

        if (idx < pState->units.size())
        {
            auto& unit = pState->units[idx];
            if (pLoaded)
            {
                unit.compositeStock = std::move(*pLoaded);
                delete pLoaded;

                unit.isGpuFinalized = CompositeStockLoader::FinalizeGPUResources(pState->pD3DDevice, pState->pD3DContext, unit.compositeStock, pState->pTextureLoader.get());
                unit.isMissing = false;
                unit.isMissingStock = false;
                unit.isMissingShape = false;

                // Open Rails Placement Algorithm:
                // Calculate effective placement step: effStep = spec.size.lengthM + spec.couplers[0].r0_min
                if (unit.compositeStock.isValid)
                {
                    float effStep = unit.compositeStock.spec.GetEffectivePlacementStepM();
                    float stockLen = (float)unit.compositeStock.spec.size.lengthM;
                    float shapeLen = 0.0f;
                    for (const auto& sub : unit.compositeStock.subShapes)
                    {
                        if (sub->isPrimary && sub->shape.isValid)
                        {
                            shapeLen = sub->shape.boundsMax.z - sub->shape.boundsMin.z;
                            break;
                        }
                    }
                    if (stockLen <= 0.01f) stockLen = shapeLen;

                    // If effective placement step <= 0.1m, this unit sits directly inside/co-located with its coupled partner
                    if (effStep <= 0.1f)
                    {
                        unit.isPhantom = true;
                        unit.length = 0.0f;
                    }
                    else
                    {
                        unit.isPhantom = false;
                        unit.length = (effStep > 0.1f) ? effStep : ((stockLen > 0.1f) ? stockLen : ((shapeLen > 0.1f) ? shapeLen : (unit.isEngine ? 21.0f : 24.0f)));
                    }
                }
            }
            else
            {
                // Missing file or missing/broken .s shape file!
                unit.isMissing = true;
                if (unit.stockFilePath.empty())
                {
                    unit.isMissingStock = true;
                    unit.isMissingShape = false;
                }
                else
                {
                    unit.isMissingStock = false;
                    unit.isMissingShape = true;
                }
                unit.isGpuFinalized = false;
                unit.isPhantom = false;
                unit.length = unit.isEngine ? 21.0f : 24.0f;
            }

            pState->loadedUnitCount++;
            if (pState->loadedUnitCount >= (int)pState->units.size())
            {
                pState->isLoading = false;
            }

            RecalculateConsist3DLayout(pState);

            RECT rcVp;
            GetClientRect(pState->hViewportWnd ? pState->hViewportWnd : hWnd, &rcVp);
            int vpW = rcVp.right - rcVp.left;
            int vpH = rcVp.bottom - rcVp.top;

            if (pState->scrollBar.GetPos() == 0 && !pState->isDragging3D)
            {
                pState->camFocusZ = CalculateLeftAlignedFocusZ(pState, vpW, vpH);
            }

            InvalidateRect(hWnd, &pState->rcHeader, FALSE);
            InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
            InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
            if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->scrollBar.OnMouseMove(pt, hWnd))
        {
            RECT rcVp;
            GetClientRect(pState->hViewportWnd, &rcVp);
            float startFocusZ = CalculateLeftAlignedFocusZ(pState, rcVp.right - rcVp.left, rcVp.bottom - rcVp.top);
            pState->camFocusZ = startFocusZ - (float)pState->scrollBar.GetPos() / 10.0f;
            Render3DConsist(pState);
            InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
            InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
        }

        int oldUnitBarHov = pState->hoverUnitBarIndex;
        pState->hoverUnitBarIndex = -1;
        if (PtInRect(&pState->rcUnitBar, pt))
        {
            for (size_t i = 0; i < pState->unitBadgeRects.size(); ++i)
            {
                if (PtInRect(&pState->unitBadgeRects[i], pt))
                {
                    pState->hoverUnitBarIndex = (int)i;
                    break;
                }
            }
        }
        if (oldUnitBarHov != pState->hoverUnitBarIndex)
        {
            InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
        }

        bool oldSide = pState->hoverSideView;
        bool oldOrbit = pState->hoverOrbit3D;
        bool oldWire = pState->hoverWireframe;
        bool oldFloat = pState->hoverFloat;
        bool oldCol = pState->hoverCollapse;
        bool oldSet = pState->hoverSettings;
        bool oldClose = pState->hoverFooterClose;

        pState->hoverSideView = PtInRect(&pState->rcBtnSideView, pt);
        pState->hoverOrbit3D = PtInRect(&pState->rcBtnOrbit3D, pt);
        pState->hoverWireframe = PtInRect(&pState->rcBtnWireframe, pt);
        pState->hoverFloat = PtInRect(&pState->rcBtnFloat, pt);
        pState->hoverCollapse = PtInRect(&pState->rcBtnCollapse, pt);
        pState->hoverSettings = PtInRect(&pState->rcBtnSettings, pt);
        pState->hoverFooterClose = PtInRect(&pState->rcBtnFooterClose, pt);

        if (oldSide != pState->hoverSideView || oldOrbit != pState->hoverOrbit3D ||
            oldWire != pState->hoverWireframe || oldFloat != pState->hoverFloat ||
            oldCol != pState->hoverCollapse || oldSet != pState->hoverSettings ||
            oldClose != pState->hoverFooterClose)
        {
            InvalidateRect(hWnd, &pState->rcHeader, FALSE);
            if (pState->isFloating) InvalidateRect(hWnd, &pState->rcFooter, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        // 1. Check click on bottom unit numbers bar
        if (PtInRect(&pState->rcUnitBar, pt))
        {
            int clickedUnit = -1;
            for (size_t i = 0; i < pState->unitBadgeRects.size(); ++i)
            {
                if (PtInRect(&pState->unitBadgeRects[i], pt))
                {
                    clickedUnit = (int)i;
                    break;
                }
            }

            // If clicked near a badge horizontally
            if (clickedUnit == -1)
            {
                for (size_t i = 0; i < pState->unitBadgeRects.size(); ++i)
                {
                    if (pState->unitBadgeRects[i].right > pState->unitBadgeRects[i].left &&
                        x >= pState->unitBadgeRects[i].left - 3 && x <= pState->unitBadgeRects[i].right + 3)
                    {
                        clickedUnit = (int)i;
                        break;
                    }
                }
            }

            if (clickedUnit >= 0 && clickedUnit < (int)pState->units.size())
            {
                pState->selectedIndex = clickedUnit;
                SendMessage(pState->hParent, WM_COMMAND, MAKEWPARAM(pState->controlId, LBN_SELCHANGE), (LPARAM)(INT_PTR)clickedUnit);
                RebuildTrackMesh(pState);
                InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
                if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            }
            return 0;
        }

        if (PtInRect(&pState->scrollBar.GetBounds(), pt))
        {
            if (pState->scrollBar.OnLButtonDown(pt, hWnd))
            {
                RECT rcVp;
                GetClientRect(pState->hViewportWnd, &rcVp);
                float startFocusZ = CalculateLeftAlignedFocusZ(pState, rcVp.right - rcVp.left, rcVp.bottom - rcVp.top);
                pState->camFocusZ = startFocusZ - (float)pState->scrollBar.GetPos() / 10.0f;
                Render3DConsist(pState);
                InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
                InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
            }
            return 0;
        }

        if (PtInRect(&pState->rcBtnSideView, pt))
        {
            // Reset to Side View (TSRE5 Mode, Facing Left, Left-aligned, Close Scaling)
            RECT rcVp;
            GetClientRect(pState->hViewportWnd, &rcVp);
            pState->camYaw = -1.5707963f;
            pState->camPitch = 0.03f;
            pState->camDist = 14.0f;
            pState->camCenterY = 1.9f;
            pState->camFocusZ = CalculateLeftAlignedFocusZ(pState, rcVp.right - rcVp.left, rcVp.bottom - rcVp.top);
            pState->bOrbitMode = false;
            pState->scrollBar.SetPos(0);
            InvalidateRect(hWnd, NULL, FALSE);
            Render3DConsist(pState);
            return 0;
        }
        if (PtInRect(&pState->rcBtnOrbit3D, pt))
        {
            pState->bOrbitMode = !pState->bOrbitMode;
            InvalidateRect(hWnd, &pState->rcHeader, FALSE);
            Render3DConsist(pState);
            return 0;
        }
        if (PtInRect(&pState->rcBtnWireframe, pt))
        {
            pState->bWireframe = !pState->bWireframe;
            InvalidateRect(hWnd, &pState->rcHeader, FALSE);
            Render3DConsist(pState);
            return 0;
        }
        if (PtInRect(&pState->rcBtnFloat, pt))
        {
            VisualConsistView_SetFloating(hWnd, !pState->isFloating);
            return 0;
        }
        if (!pState->isFloating && PtInRect(&pState->rcBtnCollapse, pt))
        {
            pState->isCollapsed = !pState->isCollapsed;
            SendMessage(pState->hParent, WM_VISUAL_DOCK_CHANGED, 0, 0);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (pState->isFloating && PtInRect(&pState->rcBtnFooterClose, pt))
        {
            VisualConsistView_SetFloating(hWnd, false);
            return 0;
        }
        if (PtInRect(&pState->rcBtnSettings, pt))
        {
            bool curCollapsed = ReadCollapseOnStartupRegistry();
            POINT ptMenu = { pState->rcBtnSettings.left, pState->rcBtnSettings.bottom };
            ClientToScreen(hWnd, &ptMenu);

            std::vector<ContextMenuItem> items = {
                ContextMenuItem::Action(101, curCollapsed ? L"\xE73E" : L"\xE739", L"Collapse Visual Preview on startup", L"", true)
            };

            int cmd = ModernContextMenu::Show(hWnd, ptMenu.x, ptMenu.y, items, pState->bDarkMode);
            if (cmd == 101)
            {
                WriteCollapseOnStartupRegistry(!curCollapsed);
            }
            return 0;
        }
        if (pState->isFloating && y < HEADER_HEIGHT)
        {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        if (pState->scrollBar.OnLButtonUp(pt, hWnd))
        {
            InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        if (!pState) break;
        if (pState->scrollBar.OnMouseLeave(hWnd))
        {
            InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
        }
        return 0;
    }

    case WM_DESTROY:
    {
        if (pState)
        {
            pState->loadGeneration++;
            ShutdownD3D11(pState);

            if (pState->hFontTitle)   DeleteObject(pState->hFontTitle);
            if (pState->hFontMetrics) DeleteObject(pState->hFontMetrics);
            if (pState->hFontIcons)   DeleteObject(pState->hFontIcons);
            if (pState->hFontBadge)   DeleteObject(pState->hFontBadge);

            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// -------------------------------------------------------------
// Public Visual Consist View API
// -------------------------------------------------------------
HWND CreateVisualConsistView(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id)
{
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wcx = { 0 };
        wcx.cbSize = sizeof(wcx);
        wcx.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcx.lpfnWndProc = VisualConsistWndProc;
        wcx.hInstance = hInstance;
        wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcx.hbrBackground = NULL;
        wcx.lpszClassName = L"VisualConsistViewClass";
        RegisterClassExW(&wcx);
        s_registered = true;
    }

    VisualConsistState* pState = new VisualConsistState();
    pState->hParent = hParent;
    pState->hInst = hInstance;
    pState->controlId = id;
    pState->bDarkMode = TRUE;
    pState->isFloating = false;
    pState->isCollapsed = ReadCollapseOnStartupRegistry();

    pState->hFontTitle = CreateFontW(
        -12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontMetrics = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    pState->hFontIcons = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
    if (!pState->hFontIcons)
    {
        pState->hFontIcons = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    }

    pState->hFontBadge = CreateFontW(
        -11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    if (!pState->hFontBadge)
    {
        pState->hFontBadge = CreateFontW(
            -11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    HWND hWnd = CreateWindowExW(
        0, L"VisualConsistViewClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, w, h,
        hParent, (HMENU)(INT_PTR)id, hInstance, pState
    );

    return hWnd;
}

void VisualConsistView_SetUnits(HWND hWnd, const std::vector<ConsistReader::UnitInfo>& units, const std::wstring& basePath, const std::wstring& consistName, const std::wstring& fileName)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->basePath = basePath;
    pState->consistTitle = consistName;
    pState->consistFileName = fileName;
    pState->loadedUnitCount = 0;

    if (units.empty())
    {
        // Cancel all pending loading and clear
        ++pState->loadGeneration;
        pState->units.clear();
        pState->trainTotalLength = 0.0f;
        pState->trainZMin = 0.0f;
        pState->trainZMax = 0.0f;
        pState->selectedIndex = -1;
        pState->isLoading = false;
        pState->scrollBar.SetRange(0, 100, 100);
        pState->scrollBar.SetPos(0);
        RebuildTrackMesh(pState);
        Render3DConsist(pState);
        InvalidateRect(hWnd, NULL, FALSE);
        if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
        return;
    }

    // Check if only flip orientation or selection changed on an existing loaded consist
    bool sameStockList = (pState->units.size() == units.size() && !units.empty());
    if (sameStockList)
    {
        for (size_t i = 0; i < units.size(); ++i)
        {
            if (pState->units[i].info.uid != units[i].uid ||
                pState->units[i].info.parentDir != units[i].parentDir ||
                pState->units[i].isEngine != units[i].isEngine)
            {
                sameStockList = false;
                break;
            }
        }
    }

    if (sameStockList)
    {
        // FAST IN-PLACE UPDATE: Keep all loaded 3D models & GPU resources intact!
        // No reloading, no background thread parsing, zero blinking!
        for (size_t i = 0; i < units.size(); ++i)
        {
            pState->units[i].info = units[i];
            pState->units[i].isFlipped = units[i].isFlipped;
        }

        RecalculateConsist3DLayout(pState);
        Render3DConsist(pState);
        InvalidateRect(hWnd, &pState->rcHeader, FALSE);
        InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
        InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
        if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
        return;
    }

    // New consist: Cancel previous in-flight generation
    pState->units.clear();
    pState->isLoading = true;

    for (const auto& u : units)
    {
        Visual3DUnit unit;
        unit.info = u;
        unit.isEngine = u.isEngine;
        unit.isFlipped = u.isFlipped;
        unit.stockFilePath = ResolveStockPath(basePath, u.parentDir, u.uid, u.isEngine);
        unit.isMissingStock = unit.stockFilePath.empty();
        unit.isMissingShape = false;
        unit.isMissing = unit.isMissingStock;
        unit.length = u.isEngine ? 21.0f : 24.0f;
        unit.isPhantom = false;

        pState->units.push_back(unit);
    }

    RecalculateConsist3DLayout(pState);

    RECT rcVp;
    GetClientRect(pState->hViewportWnd ? pState->hViewportWnd : hWnd, &rcVp);
    int vpW = rcVp.right - rcVp.left;
    int vpH = rcVp.bottom - rcVp.top;

    pState->camYaw = -1.5707963f; // -90 deg (facing left)
    pState->camPitch = 0.03f;
    pState->camDist = 14.0f;
    pState->camCenterY = 1.9f;
    pState->camFocusZ = CalculateLeftAlignedFocusZ(pState, vpW, vpH);
    pState->scrollBar.SetPos(0);
    pState->selectedIndex = 0;

    StartBackgroundLoading(pState, hWnd);

    InvalidateRect(hWnd, NULL, FALSE);
    if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
}

void VisualConsistView_SetSelected(HWND hWnd, int index)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->selectedIndex = index;
    if (index >= 0 && index < (int)pState->units.size())
    {
        // Smoothly focus camera on the selected car
        pState->camFocusZ = pState->units[index].zCenter;

        RECT rcVp;
        GetClientRect(pState->hViewportWnd ? pState->hViewportWnd : hWnd, &rcVp);
        float startFocusZ = CalculateLeftAlignedFocusZ(pState, rcVp.right - rcVp.left, rcVp.bottom - rcVp.top);
        int scrollVal = (int)((startFocusZ - pState->camFocusZ) * 10.0f);
        pState->scrollBar.SetPos((std::max)(0, (std::min)(pState->scrollBar.GetMaxScrollPos(), scrollVal)));
        InvalidateRect(hWnd, &pState->scrollBar.GetBounds(), FALSE);
    }

    RebuildTrackMesh(pState);
    InvalidateRect(hWnd, &pState->rcUnitBar, FALSE);
    if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
}

void VisualConsistView_SetDarkMode(HWND hWnd, BOOL bDark)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    pState->bDarkMode = bDark;
    InvalidateRect(hWnd, NULL, FALSE);
}

bool VisualConsistView_IsFloating(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->isFloating : false;
}

bool VisualConsistView_IsCollapsed(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->isCollapsed : false;
}

int VisualConsistView_GetDesiredHeight(HWND hWnd)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return EXPANDED_HEIGHT;
    if (pState->isFloating) return 0;
    if (pState->isCollapsed) return HEADER_HEIGHT;
    return EXPANDED_HEIGHT;
}

void VisualConsistView_SetFloating(HWND hWnd, bool bFloating)
{
    VisualConsistState* pState = (VisualConsistState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState || pState->isFloating == bFloating) return;

    pState->isFloating = bFloating;

    if (bFloating)
    {
        SetParent(hWnd, NULL);
        DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE;
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
        SetWindowLongPtrW(hWnd, GWL_STYLE, style);
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);

        DWM_WINDOW_CORNER_PREFERENCE corner = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        RECT rcParent;
        GetWindowRect(pState->hParent, &rcParent);
        int floatW = 1080;
        int floatH = 260;
        int floatX = rcParent.left + (rcParent.right - rcParent.left - floatW) / 2;
        int floatY = rcParent.bottom - floatH - 30;

        SetWindowPos(hWnd, HWND_TOPMOST, floatX, floatY, floatW, floatH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        DWORD style = WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE;
        DWORD exStyle = 0;
        SetWindowLongPtrW(hWnd, GWL_STYLE, style);
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle);
        SetParent(hWnd, pState->hParent);
    }

    SendMessage(pState->hParent, WM_VISUAL_DOCK_CHANGED, 0, 0);
    InvalidateRect(hWnd, NULL, FALSE);
}
