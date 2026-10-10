#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "UnitPreviewCard.h"
#include "UITheme.h"
#include "3D-VisualStudio.h"
#include "../SRC/AppLogging.h"
#include "../SRC/ShapeReader.h"
#include "../SRC/TextureLoader.h"
#include "../SRC/CompositeStockLoader.h"
#include "../SRC/StockSpecReader.h"

#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <shlwapi.h>

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shlwapi.lib")

#define TIMER_PREVIEW_RENDER  0x7A01
#define TIMER_PREVIEW_AUTOROT 0x7A02
#define WM_PREVIEW_LOAD_DONE  (WM_USER + 701)

extern HFONT hUIFont;

static HFONT CreatePreviewMdl2Font(float pointSize, int weight = FW_NORMAL)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv((int)(pointSize * 10), GetDpiForSystem(), 720);
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

// -------------------------------------------------------------
// Shaders & Constant Buffers (DirectX 11) - Exact VisualConsistView Pipeline
// -------------------------------------------------------------
struct PreviewCBPerFrame {
    DirectX::XMMATRIX WorldViewProj;
    DirectX::XMMATRIX World;
    DirectX::XMFLOAT3 LightDir;
    float             LightIntensity;
    DirectX::XMFLOAT3 AmbientColor;
    float             AlphaCutoff;
    DirectX::XMFLOAT4 HighlightColor;
};

struct PreviewCBBones {
    DirectX::XMMATRIX BoneTransforms[256];
};

struct PreviewTrackVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

static const char* g_PreviewShapeVSHLSL =
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

static const char* g_PreviewShapePSHLSL =
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

static const char* g_PreviewTrackVSHLSL =
"cbuffer CBPerFrame : register(b0) {\n"
"    matrix WorldViewProj;\n"
"    matrix World;\n"
"    float3 LightDir;\n"
"    float  LightIntensity;\n"
"    float3 AmbientColor;\n"
"    float  AlphaCutoff;\n"
"    float4 HighlightColor;\n"
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

static const char* g_PreviewTrackPSHLSL =
"struct PS_IN {\n"
"    float4 pos : SV_POSITION;\n"
"    float4 col : COLOR0;\n"
"};\n"
"float4 PSTrackMain(PS_IN input) : SV_TARGET {\n"
"    return input.col;\n"
"}\n";

struct UnitPreviewState
{
    HWND hWnd = NULL;
    HWND hParent = NULL;
    HWND hViewportWnd = NULL;
    BOOL bDarkMode = TRUE;

    HFONT hFontMain = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontIcon = NULL;

    // Unit info
    std::wstring currentUnitPath;
    std::wstring currentBasePath;
    std::wstring displayName;
    std::wstring typeName;
    bool isFlipped = false;
    bool hasModel = false;
    bool isLoading = false;
    uint64_t loadTaskId = 0;
    std::shared_ptr<std::atomic<bool>> currentCancelToken;

    // 3D Model
    CompositeStockUnit compositeStock;
    CompositeStockUnit stagedStock;
    std::unique_ptr<TextureLoader> pTextureLoader;
    std::mutex modelMutex;

    // D3D11 Core
    ID3D11Device*           pD3DDevice = nullptr;
    ID3D11DeviceContext*    pD3DContext = nullptr;
    IDXGISwapChain*         pSwapChain = nullptr;
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    ID3D11Texture2D*        pDepthStencilBuffer = nullptr;
    ID3D11DepthStencilView* pDepthStencilView = nullptr;

    ID3D11RasterizerState*   pRasterStateSolid = nullptr;
    ID3D11RasterizerState*   pRasterStateDecal = nullptr;
    ID3D11RasterizerState*   pRasterStateWireframe = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateWrite = nullptr;
    ID3D11DepthStencilState* pDepthStencilStateReadOnly = nullptr;
    ID3D11BlendState*        pBlendStateOpaque = nullptr;
    ID3D11BlendState*        pBlendStateAlpha = nullptr;
    ID3D11SamplerState*      pSamplerState = nullptr;

    ID3D11VertexShader*     pShapeVS = nullptr;
    ID3D11PixelShader*      pShapePS = nullptr;
    ID3D11InputLayout*      pShapeLayout = nullptr;
    ID3D11Buffer*           pConstantBuffer = nullptr;
    ID3D11Buffer*           pBoneConstantBuffer = nullptr;

    ID3D11VertexShader*     pTrackVS = nullptr;
    ID3D11PixelShader*      pTrackPS = nullptr;
    ID3D11InputLayout*      pTrackLayout = nullptr;
    ID3D11Buffer*           pTrackVertexBuffer = nullptr;
    UINT                    trackVertexCount = 0;

    // Camera Orbit / Pan / Zoom
    float camYaw = -45.0f;
    float camPitch = 15.0f;
    float camDistance = 22.0f;
    DirectX::XMFLOAT3 camTarget = { 0.0f, 1.8f, 0.0f };

    bool isOrbiting = false;
    bool isPanning = false;
    POINT lastMousePt = { 0, 0 };

    bool autoRotate = true;
    bool bWireframe = false;

    // Direct Edge & Corner Resize
    bool isResizingEdge = false;
    int resizeHitCode = 0; // 1=Left, 2=Bottom, 3=Bottom-Left, 4=Bottom-Right, 5=Right
    POINT dragStartPt = { 0, 0 };
    int dragStartW = 0;
    int dragStartH = 0;

    // Button Hitboxes
    RECT rcBtnReset = { 0 };
    RECT rcBtnAutoRot = { 0 };
    RECT rcBtnFlip = { 0 };
    RECT rcBtnStudio = { 0 };
    int hoveredBtn = 0; // 1 = Reset, 2 = AutoRot, 3 = Flip, 4 = Studio
};

extern int g_wUnitPreviewWidth;
extern int g_hTopCardsHeight;

static int HitTestPreviewCardEdges(HWND hWnd, int x, int y)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    const int BORDER_SZ = 6;

    bool onLeft = (x >= 0 && x <= BORDER_SZ);
    bool onRight = (x >= w - BORDER_SZ && x <= w);
    bool onBottom = (y >= h - BORDER_SZ && y <= h);

    if (onLeft && onBottom) return 3; // Bottom-Left (IDC_SIZENESW)
    if (onRight && onBottom) return 4; // Bottom-Right (IDC_SIZENWSE)
    if (onLeft) return 1; // Left (IDC_SIZEWE)
    if (onRight) return 5; // Right (IDC_SIZEWE)
    if (onBottom) return 2; // Bottom (IDC_SIZENS)
    return 0;
}

static void RebuildTrackMesh(UnitPreviewState* pState)
{
    if (!pState || !pState->pD3DDevice) return;

    if (pState->pTrackVertexBuffer)
    {
        pState->pTrackVertexBuffer->Release();
        pState->pTrackVertexBuffer = nullptr;
    }
    pState->trackVertexCount = 0;

    float zStart = -18.0f;
    float zEnd = 18.0f;

    if (pState->hasModel)
    {
        float zMin = pState->compositeStock.boundsMin.z - 4.0f;
        float zMax = pState->compositeStock.boundsMax.z + 4.0f;
        if (zMax - zMin > 10.0f)
        {
            zStart = zMin;
            zEnd = zMax;
        }
    }

    std::vector<PreviewTrackVertex> vertices;
    float railGaugeHalf = 0.8385f; // Standard BG 1.676m / 2
    float railHeight = 0.15f;

    DirectX::XMFLOAT4 colRail = { 0.82f, 0.84f, 0.88f, 1.0f };
    DirectX::XMFLOAT4 colSleeper = { 0.28f, 0.26f, 0.24f, 1.0f };
    DirectX::XMFLOAT4 colBallast = { 0.14f, 0.14f, 0.15f, 1.0f };

    // 1. Steel Rails
    vertices.push_back({ { -railGaugeHalf, railHeight, zStart }, colRail });
    vertices.push_back({ { -railGaugeHalf, railHeight, zEnd   }, colRail });

    vertices.push_back({ { +railGaugeHalf, railHeight, zStart }, colRail });
    vertices.push_back({ { +railGaugeHalf, railHeight, zEnd   }, colRail });

    // 2. Ballast Ground Bed Borders
    vertices.push_back({ { -1.8f, 0.0f, zStart }, colBallast });
    vertices.push_back({ { -1.8f, 0.0f, zEnd   }, colBallast });
    vertices.push_back({ { +1.8f, 0.0f, zStart }, colBallast });
    vertices.push_back({ { +1.8f, 0.0f, zEnd   }, colBallast });

    // 3. Sleepers every 0.65m
    for (float z = zStart; z <= zEnd; z += 0.65f)
    {
        vertices.push_back({ { -1.35f, 0.04f, z }, colSleeper });
        vertices.push_back({ { +1.35f, 0.04f, z }, colSleeper });
    }

    pState->trackVertexCount = (UINT)vertices.size();
    if (pState->trackVertexCount == 0) return;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(vertices.size() * sizeof(PreviewTrackVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();
    pState->pD3DDevice->CreateBuffer(&bd, &initData, &pState->pTrackVertexBuffer);
}

static void ResizeD3D11(UnitPreviewState* pState, int width, int height)
{
    if (!pState || !pState->pSwapChain || !pState->pD3DDevice) return;
    if (width < 32) width = 32;
    if (height < 32) height = 32;

    if (pState->pRenderTargetView) { pState->pRenderTargetView->Release(); pState->pRenderTargetView = nullptr; }
    if (pState->pDepthStencilView) { pState->pDepthStencilView->Release(); pState->pDepthStencilView = nullptr; }
    if (pState->pDepthStencilBuffer) { pState->pDepthStencilBuffer->Release(); pState->pDepthStencilBuffer = nullptr; }

    pState->pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    pState->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer)
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
    pState->pD3DDevice->CreateTexture2D(&depthDesc, NULL, &pState->pDepthStencilBuffer);
    if (pState->pDepthStencilBuffer)
    {
        pState->pD3DDevice->CreateDepthStencilView(pState->pDepthStencilBuffer, NULL, &pState->pDepthStencilView);
    }
}

static bool InitD3D11(UnitPreviewState* pState, HWND hViewport, int width, int height)
{
    if (!pState || !hViewport) return false;
    if (width < 32) width = 32;
    if (height < 32) height = 32;

    if (pState->pSwapChain)
    {
        ResizeD3D11(pState, width, height);
        return true;
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hViewport;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    UINT createDeviceFlags = 0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
        featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
        &scd, &pState->pSwapChain, &pState->pD3DDevice, &featureLevel, &pState->pD3DContext);

    if (FAILED(hr))
    {
        hr = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags,
            featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
            &scd, &pState->pSwapChain, &pState->pD3DDevice, &featureLevel, &pState->pD3DContext);
    }
    if (FAILED(hr)) return false;

    pState->pTextureLoader = std::make_unique<TextureLoader>(pState->pD3DDevice, pState->pD3DContext);
    ResizeD3D11(pState, width, height);

    // Rasterizer States
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

    // Sampler State (16x Anisotropic Filtering)
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy = 16;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    pState->pD3DDevice->CreateSamplerState(&sd, &pState->pSamplerState);

    // Constant Buffers
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(PreviewCBPerFrame);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&cbd, NULL, &pState->pConstantBuffer);

    D3D11_BUFFER_DESC boneCbd = {};
    boneCbd.ByteWidth = sizeof(PreviewCBBones);
    boneCbd.Usage = D3D11_USAGE_DEFAULT;
    boneCbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pState->pD3DDevice->CreateBuffer(&boneCbd, NULL, &pState->pBoneConstantBuffer);

    // Shape Shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    if (SUCCEEDED(D3DCompile(g_PreviewShapeVSHLSL, strlen(g_PreviewShapeVSHLSL), NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob)))
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

    if (SUCCEEDED(D3DCompile(g_PreviewShapePSHLSL, strlen(g_PreviewShapePSHLSL), NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &pState->pShapePS);
        psBlob->Release();
    }

    // Track Shaders
    ID3DBlob* tvsBlob = nullptr;
    ID3DBlob* tpsBlob = nullptr;
    if (SUCCEEDED(D3DCompile(g_PreviewTrackVSHLSL, strlen(g_PreviewTrackVSHLSL), NULL, NULL, NULL, "VSTrackMain", "vs_4_0", 0, 0, &tvsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreateVertexShader(tvsBlob->GetBufferPointer(), tvsBlob->GetBufferSize(), NULL, &pState->pTrackVS);
        D3D11_INPUT_ELEMENT_DESC trackLayoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        pState->pD3DDevice->CreateInputLayout(trackLayoutDesc, _countof(trackLayoutDesc), tvsBlob->GetBufferPointer(), tvsBlob->GetBufferSize(), &pState->pTrackLayout);
        tvsBlob->Release();
    }

    if (SUCCEEDED(D3DCompile(g_PreviewTrackPSHLSL, strlen(g_PreviewTrackPSHLSL), NULL, NULL, NULL, "PSTrackMain", "ps_4_0", 0, 0, &tpsBlob, &errBlob)))
    {
        pState->pD3DDevice->CreatePixelShader(tpsBlob->GetBufferPointer(), tpsBlob->GetBufferSize(), NULL, &pState->pTrackPS);
        tpsBlob->Release();
    }

    RebuildTrackMesh(pState);
    return true;
}

static void ShutdownD3D11(UnitPreviewState* pState)
{
    if (!pState) return;

    CompositeStockLoader::ClearCompositeStock(pState->compositeStock);
    pState->pTextureLoader.reset();

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

static void RenderPreviewFrame(UnitPreviewState* pState)
{
    if (!pState || !pState->pD3DDevice || !pState->pD3DContext || !pState->pSwapChain || !pState->pRenderTargetView || !pState->pDepthStencilView)
        return;

    RECT rc;
    GetClientRect(pState->hViewportWnd, &rc);
    int width = rc.right;
    int height = rc.bottom;
    if (width < 10 || height < 10) return;

    // Studio Slate Horizon Background matching VisualConsistView
    float clearCol[4] = { 0.085f, 0.090f, 0.105f, 1.0f };
    pState->pD3DContext->ClearRenderTargetView(pState->pRenderTargetView, clearCol);
    pState->pD3DContext->ClearDepthStencilView(pState->pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    pState->pD3DContext->RSSetViewports(1, &vp);

    pState->pD3DContext->OMSetRenderTargets(1, &pState->pRenderTargetView, pState->pDepthStencilView);

    // Camera Matrices
    float aspect = (float)width / (float)(std::max)(1, height);
    float fov = DirectX::XMConvertToRadians(28.0f);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fov, aspect, 0.2f, 1000.0f);

    float radYaw = DirectX::XMConvertToRadians(pState->camYaw);
    float radPitch = DirectX::XMConvertToRadians(pState->camPitch);

    float eyeX = pState->camTarget.x + pState->camDistance * cosf(radPitch) * sinf(radYaw);
    float eyeY = pState->camTarget.y + pState->camDistance * sinf(radPitch);
    float eyeZ = pState->camTarget.z + pState->camDistance * cosf(radPitch) * cosf(radYaw);

    DirectX::XMVECTOR eye = DirectX::XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
    DirectX::XMVECTOR target = DirectX::XMVectorSet(pState->camTarget.x, pState->camTarget.y, pState->camTarget.z, 1.0f);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, target, up);
    DirectX::XMMATRIX viewProj = view * proj;

    // Constant Buffer Setup (Studio Showroom Shading)
    PreviewCBPerFrame cb = {};
    cb.LightDir = DirectX::XMFLOAT3(0.75f, 0.85f, 0.45f);
    cb.LightIntensity = 0.55f;
    cb.AmbientColor = DirectX::XMFLOAT3(0.78f, 0.78f, 0.80f);
    cb.AlphaCutoff = 0.5f;
    cb.HighlightColor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

    // Render Procedural Rails & Ballast
    if (pState->pTrackVertexBuffer && pState->trackVertexCount > 0)
    {
        DirectX::XMMATRIX worldTrack = DirectX::XMMatrixIdentity();
        cb.World = DirectX::XMMatrixTranspose(worldTrack);
        cb.WorldViewProj = DirectX::XMMatrixTranspose(worldTrack * viewProj);
        pState->pD3DContext->UpdateSubresource(pState->pConstantBuffer, 0, NULL, &cb, 0, 0);

        pState->pD3DContext->VSSetConstantBuffers(0, 1, &pState->pConstantBuffer);
        pState->pD3DContext->PSSetConstantBuffers(0, 1, &pState->pConstantBuffer);

        pState->pD3DContext->IASetInputLayout(pState->pTrackLayout);
        pState->pD3DContext->VSSetShader(pState->pTrackVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pTrackPS, NULL, 0);

        UINT stride = sizeof(PreviewTrackVertex);
        UINT offset = 0;
        pState->pD3DContext->IASetVertexBuffers(0, 1, &pState->pTrackVertexBuffer, &stride, &offset);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        pState->pD3DContext->RSSetState(pState->pRasterStateSolid);
        pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
        pState->pD3DContext->OMSetBlendState(pState->pBlendStateOpaque, NULL, 0xFFFFFFFF);

        pState->pD3DContext->Draw(pState->trackVertexCount, 0);
    }

    // Render Composite Stock Unit (3-Pass Pipeline matching VisualConsistView)
    std::lock_guard<std::mutex> lock(pState->modelMutex);
    if (pState->compositeStock.isValid && !pState->compositeStock.subShapes.empty())
    {
        pState->pD3DContext->IASetInputLayout(pState->pShapeLayout);
        pState->pD3DContext->VSSetShader(pState->pShapeVS, NULL, 0);
        pState->pD3DContext->PSSetShader(pState->pShapePS, NULL, 0);
        pState->pD3DContext->PSSetSamplers(0, 1, &pState->pSamplerState);
        pState->pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        DirectX::XMMATRIX worldBase = pState->isFlipped ?
            DirectX::XMMatrixRotationY(DirectX::XM_PI) :
            DirectX::XMMatrixIdentity();

        PreviewCBBones bones = {};

        auto RenderUnitPass = [&](int passMode)
        {
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
                // PASS 2: Alpha Test & Decals (Open Rails: DepthWrite = ON, BlendState = Alpha, ReferenceAlpha = 10/255)
                pState->pD3DContext->RSSetState(pState->bWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
                pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateWrite, 0);
                pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
                cb.AlphaCutoff = 0.0392f;
            }
            else if (passMode == 2)
            {
                // PASS 3: Translucent Decals, Glass & Details (DepthWrite = OFF, Blend = Alpha)
                pState->pD3DContext->RSSetState(pState->bWireframe ? pState->pRasterStateWireframe : pState->pRasterStateSolid);
                pState->pD3DContext->OMSetDepthStencilState(pState->pDepthStencilStateReadOnly, 0);
                pState->pD3DContext->OMSetBlendState(pState->pBlendStateAlpha, NULL, 0xFFFFFFFF);
                cb.AlphaCutoff = 0.005f;
            }

            for (const auto& pSub : pState->compositeStock.subShapes)
            {
                if (!pSub || !pSub->isLoaded || !pSub->shape.isValid || !pSub->shape.pVertexBuffer || !pSub->shape.pIndexBuffer) continue;

                bool hasMatching = false;
                for (const auto& sm : pSub->shape.subMeshes)
                {
                    if (passMode == 0 && !sm.isTransparent) { hasMatching = true; break; }
                    if (passMode == 1 && (sm.isTransparent && sm.isAlphaTest)) { hasMatching = true; break; }
                    if (passMode == 2 && (sm.isTransparent && !sm.isAlphaTest)) { hasMatching = true; break; }
                }
                if (!hasMatching) continue;

                DirectX::XMMATRIX subWorld = pSub->localMatrix * worldBase;
                cb.World = DirectX::XMMatrixTranspose(subWorld);
                cb.WorldViewProj = DirectX::XMMatrixTranspose(subWorld * viewProj);

                if (!pSub->boneMatrices.empty())
                {
                    size_t numB = (std::min)((size_t)256, pSub->boneMatrices.size());
                    for (size_t b = 0; b < numB; ++b)
                    {
                        bones.BoneTransforms[b] = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&pSub->boneMatrices[b]));
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

                UINT stride = sizeof(GPUVertex);
                UINT offset = 0;
                pState->pD3DContext->IASetVertexBuffers(0, 1, &pSub->shape.pVertexBuffer, &stride, &offset);
                pState->pD3DContext->IASetIndexBuffer(pSub->shape.pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

                for (const auto& sm : pSub->shape.subMeshes)
                {
                    bool match = (passMode == 0 && !sm.isTransparent) ||
                                 (passMode == 1 && sm.isTransparent && sm.isAlphaTest) ||
                                 (passMode == 2 && sm.isTransparent && !sm.isAlphaTest);
                    if (match)
                    {
                        ID3D11ShaderResourceView* srv = sm.pSRV ? sm.pSRV :
                            (pSub->pTextureLoader ? pSub->pTextureLoader->GetDefaultTexture() : (pState->pTextureLoader ? pState->pTextureLoader->GetDefaultTexture() : nullptr));
                        pState->pD3DContext->PSSetShaderResources(0, 1, &srv);
                        pState->pD3DContext->DrawIndexed(sm.indexCount, sm.startIndex, 0);
                    }
                }
            }
        };

        RenderUnitPass(0); // Pass 1: Opaque
        RenderUnitPass(1); // Pass 2: Decals & Cutouts
        RenderUnitPass(2); // Pass 3: Translucent Glass
    }

    pState->pSwapChain->Present(1, 0);
}

static LRESULT CALLBACK PreviewViewportProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)lpcs->lpCreateParams);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (pState)
        {
            RenderPreviewFrame(pState);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (pState && pState->hWnd)
        {
            POINT pt;
            GetCursorPos(&pt);
            POINT ptCard = pt;
            ScreenToClient(pState->hWnd, &ptCard);
            int hit = HitTestPreviewCardEdges(pState->hWnd, ptCard.x, ptCard.y);
            if (hit == 1 || hit == 5) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
            if (hit == 2) { SetCursor(LoadCursor(NULL, IDC_SIZENS)); return TRUE; }
            if (hit == 3) { SetCursor(LoadCursor(NULL, IDC_SIZENESW)); return TRUE; }
            if (hit == 4) { SetCursor(LoadCursor(NULL, IDC_SIZENWSE)); return TRUE; }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        SetFocus(hWnd);
        NMHDR nmhdr = { 0 };
        nmhdr.hwndFrom = hWnd;
        nmhdr.idFrom   = (UINT_PTR)GetWindowLongPtrW(hWnd, GWLP_ID);
        nmhdr.code     = NM_SETFOCUS;
        SendMessageW(GetParent(hWnd), WM_NOTIFY, (WPARAM)nmhdr.idFrom, (LPARAM)&nmhdr);

        POINT ptScreen;
        GetCursorPos(&ptScreen);
        POINT ptCard = ptScreen;
        ScreenToClient(pState->hWnd, &ptCard);
        int hit = HitTestPreviewCardEdges(pState->hWnd, ptCard.x, ptCard.y);
        if (hit != 0)
        {
            pState->isResizingEdge = true;
            pState->resizeHitCode = hit;
            pState->dragStartPt = ptScreen;
            pState->dragStartW = g_wUnitPreviewWidth;
            pState->dragStartH = g_hTopCardsHeight;
            SetCapture(hWnd);
            return 0;
        }
        pState->isOrbiting = true;
        pState->lastMousePt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(hWnd);
        SetFocus(hWnd);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (pState)
        {
            // Reset to clean 3/4 perspective showroom angle
            pState->camYaw = -45.0f;
            pState->camPitch = 15.0f;
            pState->camTarget = pState->compositeStock.isValid ? pState->compositeStock.center : DirectX::XMFLOAT3(0.0f, 1.8f, 0.0f);
            pState->camDistance = pState->compositeStock.isValid ? (std::max)(10.0f, pState->compositeStock.radius * 2.1f) : 22.0f;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        if (!pState) break;
        pState->isPanning = true;
        pState->lastMousePt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(hWnd);
        SetFocus(hWnd);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        if (pState->isResizingEdge)
        {
            POINT ptScreen;
            GetCursorPos(&ptScreen);
            int dx = ptScreen.x - pState->dragStartPt.x;
            int dy = ptScreen.y - pState->dragStartPt.y;

            if (pState->resizeHitCode == 1 || pState->resizeHitCode == 3)
                g_wUnitPreviewWidth = pState->dragStartW - dx;
            else if (pState->resizeHitCode == 5 || pState->resizeHitCode == 4)
                g_wUnitPreviewWidth = pState->dragStartW + dx;

            if (pState->resizeHitCode == 2 || pState->resizeHitCode == 3 || pState->resizeHitCode == 4)
                g_hTopCardsHeight = pState->dragStartH + dy;

            if (g_wUnitPreviewWidth < 220) g_wUnitPreviewWidth = 220;
            if (g_hTopCardsHeight < 140) g_hTopCardsHeight = 140;

            if (pState->hParent)
            {
                RECT rcParent;
                GetClientRect(pState->hParent, &rcParent);
                SendMessage(pState->hParent, WM_SIZE, 0, MAKELPARAM(rcParent.right, rcParent.bottom));
                InvalidateRect(pState->hParent, NULL, TRUE);
                UpdateWindow(pState->hParent);
            }
            return 0;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (pState->isOrbiting)
        {
            int dx = x - pState->lastMousePt.x;
            int dy = y - pState->lastMousePt.y;
            pState->lastMousePt = { x, y };

            pState->camYaw += dx * 0.45f;
            pState->camPitch += dy * 0.45f;

            if (pState->camPitch > 85.0f) pState->camPitch = 85.0f;
            if (pState->camPitch < -85.0f) pState->camPitch = -85.0f;

            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (pState->isPanning)
        {
            int dx = x - pState->lastMousePt.x;
            int dy = y - pState->lastMousePt.y;
            pState->lastMousePt = { x, y };

            float radYaw = DirectX::XMConvertToRadians(pState->camYaw);
            DirectX::XMFLOAT3 right = { cosf(radYaw), 0.0f, -sinf(radYaw) };

            float panSpeed = pState->camDistance * 0.0018f;
            pState->camTarget.x -= right.x * dx * panSpeed;
            pState->camTarget.z -= right.z * dx * panSpeed;
            pState->camTarget.y += dy * panSpeed;

            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_CAPTURECHANGED:
    {
        if (pState)
        {
            if (pState->isResizingEdge)
            {
                pState->isResizingEdge = false;
                if (GetCapture() == hWnd) ReleaseCapture();
                return 0;
            }
            pState->isOrbiting = false;
            pState->isPanning = false;
            if (GetCapture() == hWnd) ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!pState) break;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        float zoomFactor = (delta > 0) ? 0.88f : 1.14f;
        pState->camDistance *= zoomFactor;
        if (pState->camDistance < 2.0f) pState->camDistance = 2.0f;
        if (pState->camDistance > 250.0f) pState->camDistance = 250.0f;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK UnitPreviewCardProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        pState = new UnitPreviewState();
        pState->hWnd = hWnd;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    case WM_CREATE:
    {
        LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lParam;
        if (pState)
        {
            pState->hParent = lpcs->hwndParent;
            pState->hFontMain = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            pState->hFontIcon = CreatePreviewMdl2Font(10.0f, FW_NORMAL);

            // Register viewport child window class
            WNDCLASSEXW vwc = { sizeof(WNDCLASSEXW) };
            vwc.lpfnWndProc = PreviewViewportProc;
            vwc.hInstance = lpcs->hInstance;
            vwc.lpszClassName = L"TSCBPreviewViewport";
            vwc.hCursor = LoadCursor(NULL, IDC_ARROW);
            vwc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
            RegisterClassExW(&vwc);

            RECT rc;
            GetClientRect(hWnd, &rc);
            int vpH = (rc.bottom > 30) ? (rc.bottom - 30) : 10;

            pState->hViewportWnd = CreateWindowExW(
                0, L"TSCBPreviewViewport", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                1, 29, (rc.right > 2) ? (rc.right - 2) : 20, vpH,
                hWnd, (HMENU)9001, lpcs->hInstance, pState
            );

            InitD3D11(pState, pState->hViewportWnd, rc.right - 2, vpH);

            SetTimer(hWnd, TIMER_PREVIEW_RENDER, 33, NULL); // ~30 fps
            SetTimer(hWnd, TIMER_PREVIEW_AUTOROT, 33, NULL);
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_PREVIEW_AUTOROT)
        {
            if (pState && pState->autoRotate && !pState->isOrbiting && !pState->isPanning && pState->hasModel)
            {
                pState->camYaw += 0.35f;
                if (pState->camYaw >= 360.0f) pState->camYaw -= 360.0f;
                if (pState->hViewportWnd)
                {
                    InvalidateRect(pState->hViewportWnd, NULL, FALSE);
                }
            }
        }
        else if (wParam == TIMER_PREVIEW_RENDER)
        {
            if (pState && pState->hViewportWnd)
            {
                RenderPreviewFrame(pState);
            }
        }
        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (pState)
        {
            int vpW = (w > 2) ? (w - 2) : 20;
            int vpH = (h > 30) ? (h - 30) : 10;

            if (pState->hViewportWnd)
            {
                SetWindowPos(pState->hViewportWnd, NULL, 1, 29, vpW, vpH, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            ResizeD3D11(pState, vpW, vpH);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (pState)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            int hit = HitTestPreviewCardEdges(hWnd, pt.x, pt.y);
            if (hit == 1 || hit == 5) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
            if (hit == 2) { SetCursor(LoadCursor(NULL, IDC_SIZENS)); return TRUE; }
            if (hit == 3) { SetCursor(LoadCursor(NULL, IDC_SIZENESW)); return TRUE; }
            if (hit == 4) { SetCursor(LoadCursor(NULL, IDC_SIZENWSE)); return TRUE; }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (!pState) break;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        int hit = HitTestPreviewCardEdges(hWnd, x, y);
        if (hit != 0)
        {
            pState->isResizingEdge = true;
            pState->resizeHitCode = hit;
            GetCursorPos(&pState->dragStartPt);
            pState->dragStartW = g_wUnitPreviewWidth;
            pState->dragStartH = g_hTopCardsHeight;
            SetCapture(hWnd);
            return 0;
        }

        if (PtInRect(&pState->rcBtnReset, pt))
        {
            pState->camYaw = -45.0f;
            pState->camPitch = 15.0f;
            pState->camTarget = pState->compositeStock.isValid ? pState->compositeStock.center : DirectX::XMFLOAT3(0.0f, 1.8f, 0.0f);
            pState->camDistance = pState->compositeStock.isValid ? (std::max)(10.0f, pState->compositeStock.radius * 2.1f) : 22.0f;
            if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            return 0;
        }
        else if (PtInRect(&pState->rcBtnAutoRot, pt))
        {
            pState->autoRotate = !pState->autoRotate;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        else if (PtInRect(&pState->rcBtnFlip, pt))
        {
            pState->isFlipped = !pState->isFlipped;
            SendMessage(pState->hParent, WM_PREVIEW_UNIT_FLIPPED, 0, (LPARAM)pState->isFlipped);
            if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        else if (PtInRect(&pState->rcBtnStudio, pt))
        {
            if (!pState->currentUnitPath.empty())
            {
                Show3DVisualStudioDialog(pState->hParent, pState->currentUnitPath, pState->currentBasePath);
            }
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!pState) break;
        if (pState->isResizingEdge)
        {
            POINT ptScreen;
            GetCursorPos(&ptScreen);
            int dx = ptScreen.x - pState->dragStartPt.x;
            int dy = ptScreen.y - pState->dragStartPt.y;

            if (pState->resizeHitCode == 1 || pState->resizeHitCode == 3)
                g_wUnitPreviewWidth = pState->dragStartW - dx;
            else if (pState->resizeHitCode == 5 || pState->resizeHitCode == 4)
                g_wUnitPreviewWidth = pState->dragStartW + dx;

            if (pState->resizeHitCode == 2 || pState->resizeHitCode == 3 || pState->resizeHitCode == 4)
                g_hTopCardsHeight = pState->dragStartH + dy;

            if (g_wUnitPreviewWidth < 220) g_wUnitPreviewWidth = 220;
            if (g_hTopCardsHeight < 140) g_hTopCardsHeight = 140;

            if (pState->hParent)
            {
                RECT rcParent;
                GetClientRect(pState->hParent, &rcParent);
                SendMessage(pState->hParent, WM_SIZE, 0, MAKELPARAM(rcParent.right, rcParent.bottom));
                InvalidateRect(pState->hParent, NULL, TRUE);
                UpdateWindow(pState->hParent);
            }
            return 0;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        POINT pt = { x, y };

        int newHover = 0;
        if (PtInRect(&pState->rcBtnReset, pt)) newHover = 1;
        else if (PtInRect(&pState->rcBtnAutoRot, pt)) newHover = 2;
        else if (PtInRect(&pState->rcBtnFlip, pt)) newHover = 3;
        else if (PtInRect(&pState->rcBtnStudio, pt)) newHover = 4;

        if (newHover != pState->hoveredBtn)
        {
            pState->hoveredBtn = newHover;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    {
        if (pState && pState->isResizingEdge)
        {
            pState->isResizingEdge = false;
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;
        }
        break;
    }

    case WM_PREVIEW_LOAD_DONE:
    {
        bool ok = (wParam != 0);
        uint64_t msgTaskId = (uint64_t)lParam;
        if (pState && pState->pD3DDevice && pState->pD3DContext)
        {
            if (msgTaskId != 0 && pState->loadTaskId != msgTaskId)
            {
                return 0; // Stale message from superseded loader thread
            }
            if (pState->currentCancelToken && *pState->currentCancelToken)
            {
                return 0; // Cancelled
            }

            std::lock_guard<std::mutex> lock(pState->modelMutex);
            if (pState->currentCancelToken && *pState->currentCancelToken)
            {
                return 0;
            }

            // Release previous Direct3D resources safely on the UI/Direct3D thread
            CompositeStockLoader::ClearCompositeStock(pState->compositeStock);
            pState->compositeStock = std::move(pState->stagedStock);

            if (ok && !pState->compositeStock.subShapes.empty())
            {
                if (!pState->pTextureLoader)
                {
                    pState->pTextureLoader = std::make_unique<TextureLoader>(pState->pD3DDevice, pState->pD3DContext);
                }
                bool gpuOk = CompositeStockLoader::FinalizeGPUResources(pState->pD3DDevice, pState->pD3DContext, pState->compositeStock, pState->pTextureLoader.get());
                pState->hasModel = gpuOk && pState->compositeStock.isValid;
                pState->isLoading = false;

                if (pState->hasModel)
                {
                    // Compute tighter bounding box
                    DirectX::XMFLOAT3 cMin = { 1e9f, 1e9f, 1e9f };
                    DirectX::XMFLOAT3 cMax = { -1e9f, -1e9f, -1e9f };
                    for (const auto& sub : pState->compositeStock.subShapes)
                    {
                        if (sub && sub->shape.isValid)
                        {
                            cMin.x = (std::min)(cMin.x, sub->shape.boundsMin.x);
                            cMin.y = (std::min)(cMin.y, sub->shape.boundsMin.y);
                            cMin.z = (std::min)(cMin.z, sub->shape.boundsMin.z);
                            cMax.x = (std::max)(cMax.x, sub->shape.boundsMax.x);
                            cMax.y = (std::max)(cMax.y, sub->shape.boundsMax.y);
                            cMax.z = (std::max)(cMax.z, sub->shape.boundsMax.z);
                        }
                    }
                    if (cMin.x < cMax.x)
                    {
                        pState->compositeStock.boundsMin = cMin;
                        pState->compositeStock.boundsMax = cMax;
                        pState->compositeStock.center = DirectX::XMFLOAT3((cMin.x + cMax.x) * 0.5f, (cMin.y + cMax.y) * 0.5f, (cMin.z + cMax.z) * 0.5f);
                        float dx = cMax.x - cMin.x;
                        float dy = cMax.y - cMin.y;
                        float dz = cMax.z - cMin.z;
                        pState->compositeStock.radius = sqrtf(dx * dx + dy * dy + dz * dz) * 0.5f;
                    }

                    pState->camTarget = pState->compositeStock.center;
                    pState->camDistance = (std::max)(10.0f, pState->compositeStock.radius * 2.1f);
                    pState->camYaw = -45.0f;
                    pState->camPitch = 15.0f;

                    RebuildTrackMesh(pState);
                }
            }
            else
            {
                pState->hasModel = false;
                pState->isLoading = false;
            }
        }
        if (pState && pState->hViewportWnd)
        {
            InvalidateRect(pState->hViewportWnd, NULL, FALSE);
        }
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (pState)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right;
            int height = rc.bottom;

            // Double buffer for 2D UI overlay
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

            // Fill outer background
            HBRUSH hbrBg = CreateSolidBrush(UITheme::DarkBackground);
            FillRect(hMemDC, &rc, hbrBg);
            DeleteObject(hbrBg);


            // 1. Header Bar (1 to 28px)
            RECT rcHeader = { 1, 1, width - 1, 28 };
            HBRUSH hbrHeader = CreateSolidBrush(RGB(32, 32, 38));
            FillRect(hMemDC, &rcHeader, hbrHeader);
            DeleteObject(hbrHeader);

            // 2. Header Divider line at y = 28
            HPEN hPenDivider = CreatePen(PS_SOLID, 1, RGB(65, 65, 75));
            HGDIOBJ hOldDiv = SelectObject(hMemDC, hPenDivider);
            MoveToEx(hMemDC, 0, 28, NULL);
            LineTo(hMemDC, width, 28);
            SelectObject(hMemDC, hOldDiv);
            DeleteObject(hPenDivider);

            // 3. Crisp Card outline on all 4 sides
            COLORREF clrCardBorder = RGB(65, 65, 75);
            HPEN hPenBorder = CreatePen(PS_SOLID, 1, clrCardBorder);
            HBRUSH hNullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ hOldP = SelectObject(hMemDC, hPenBorder);
            HGDIOBJ hOldB = SelectObject(hMemDC, hNullBr);
            Rectangle(hMemDC, 0, 0, width, height);
            SelectObject(hMemDC, hOldP);
            SelectObject(hMemDC, hOldB);
            DeleteObject(hPenBorder);

            // Header Quick Action Buttons (Reset, AutoRot, Flip, 3D Studio)
            int btnW = 24;
            int btnH = 20;
            int btnY = 4;
            int rightX = width - 8;

            // Button 4: 3D Studio Launch [ ⤢ ]
            rightX -= btnW;
            pState->rcBtnStudio = { rightX, btnY, rightX + btnW, btnY + btnH };

            // Button 3: Flip Orientation [ ↔ ]
            rightX -= (btnW + 4);
            pState->rcBtnFlip = { rightX, btnY, rightX + btnW, btnY + btnH };

            // Button 2: Auto Rotate Toggle [ ⟳ ]
            rightX -= (btnW + 4);
            pState->rcBtnAutoRot = { rightX, btnY, rightX + btnW, btnY + btnH };

            // Button 1: Reset View [ ↺ ]
            rightX -= (btnW + 4);
            pState->rcBtnReset = { rightX, btnY, rightX + btnW, btnY + btnH };

            // Header Title / Unit Information
            SelectObject(hMemDC, hUIFont ? hUIFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(hMemDC, TRANSPARENT);
            SetTextColor(hMemDC, RGB(220, 220, 230));

            RECT rcTitle = { 10, 0, rightX - 6, 28 };
            if (pState->isLoading)
            {
                SetTextColor(hMemDC, RGB(255, 205, 110));
                DrawTextW(hMemDC, L"3D Live Preview • Loading...", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            else if (pState->hasModel && !pState->displayName.empty())
            {
                std::wstring titleText = pState->displayName;
                if (!pState->typeName.empty()) titleText += L" (" + pState->typeName + L")";
                if (pState->isFlipped) titleText += L" [↔]";
                SetTextColor(hMemDC, RGB(245, 248, 255));
                DrawTextW(hMemDC, titleText.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            else
            {
                SetTextColor(hMemDC, RGB(180, 185, 195));
                DrawTextW(hMemDC, L"3D Live Unit Preview", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            auto DrawIconButton = [&](const RECT& rcBtn, const wchar_t* glyph, int btnId, bool isActive = false)
            {
                bool isHover = (pState->hoveredBtn == btnId);
                if (isHover || isActive)
                {
                    HBRUSH hbrHover = CreateSolidBrush(isActive ? RGB(0, 120, 215) : RGB(50, 50, 60));
                    FillRect(hMemDC, &rcBtn, hbrHover);
                    DeleteObject(hbrHover);
                }
                SelectObject(hMemDC, pState->hFontIcon);
                SetTextColor(hMemDC, isHover ? RGB(255, 255, 255) : (isActive ? RGB(255, 255, 255) : RGB(180, 180, 190)));
                RECT rcText = rcBtn;
                DrawTextW(hMemDC, glyph, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };

            DrawIconButton(pState->rcBtnReset,   L"\xE72C", 1); // Reset
            DrawIconButton(pState->rcBtnAutoRot, L"\xE77A", 2, pState->autoRotate); // Turntable / Play
            DrawIconButton(pState->rcBtnFlip,    L"\xE745", 3); // Flip
            DrawIconButton(pState->rcBtnStudio,  L"\xE7F4", 4); // 3D Studio Launch

            // Exclude the 3D viewport child region from parent Blt
            RECT rcVp = { 1, 29, width - 1, height - 1 };
            ExcludeClipRect(hdc, rcVp.left, rcVp.top, rcVp.right, rcVp.bottom);

            BitBlt(hdc, 0, 0, width, height, hMemDC, 0, 0, SRCCOPY);

            SelectObject(hMemDC, hOldBmp);
            DeleteObject(hMemBmp);
            DeleteDC(hMemDC);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
    {
        if (pState)
        {
            KillTimer(hWnd, TIMER_PREVIEW_RENDER);
            KillTimer(hWnd, TIMER_PREVIEW_AUTOROT);
            ShutdownD3D11(pState);
            delete pState;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

HWND CreateUnitPreviewCard(HWND hParent, HINSTANCE hInstance, int x, int y, int w, int h, int id)
{
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = UnitPreviewCardProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TSCBUnitPreviewCard";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0, L"TSCBUnitPreviewCard", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, w, h, hParent, (HMENU)(INT_PTR)id, hInstance, NULL
    );
    return hWnd;
}

void UnitPreviewCard_SetUnit(
    HWND hWnd,
    const std::wstring& unitPath,
    const std::wstring& basePath,
    bool isFlipped,
    const std::wstring& displayName,
    const std::wstring& typeName)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    if (unitPath.empty())
    {
        UnitPreviewCard_Clear(hWnd);
        return;
    }

    if (pState->currentUnitPath == unitPath && pState->isFlipped == isFlipped && (pState->hasModel || pState->isLoading))
    {
        pState->displayName = displayName;
        pState->typeName = typeName;
        InvalidateRect(hWnd, NULL, TRUE);
        return;
    }

    // Cancel any ongoing background task immediately
    if (pState->currentCancelToken)
    {
        *pState->currentCancelToken = true;
    }

    auto cancelToken = std::make_shared<std::atomic<bool>>(false);
    pState->currentCancelToken = cancelToken;

    pState->currentUnitPath = unitPath;
    pState->currentBasePath = basePath;
    pState->displayName = displayName;
    pState->typeName = typeName;
    pState->isFlipped = isFlipped;

    uint64_t taskId = ++pState->loadTaskId;
    pState->isLoading = true;

    // Load composite rolling stock in background thread with single-flight cooperative cancellation
    std::thread([hWnd, pState, unitPath, basePath, taskId, cancelToken]() {
        if (*cancelToken) return;

        CompositeStockUnit unit;
        bool ok = CompositeStockLoader::LoadCompositeStockCPU(unitPath, basePath, unit, cancelToken.get()) && !unit.subShapes.empty();

        if (*cancelToken) return;

        if (pState->loadTaskId == taskId && !*cancelToken)
        {
            {
                std::lock_guard<std::mutex> lock(pState->modelMutex);
                if (*cancelToken) return;
                pState->stagedStock = std::move(unit);
            }
            if (!*cancelToken)
            {
                PostMessage(hWnd, WM_PREVIEW_LOAD_DONE, (WPARAM)ok, (LPARAM)taskId);
            }
        }
    }).detach();

    InvalidateRect(hWnd, NULL, TRUE);
}

void UnitPreviewCard_Clear(HWND hWnd)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;

    if (pState->currentCancelToken)
    {
        *pState->currentCancelToken = true;
    }
    ++pState->loadTaskId;
    pState->isLoading = false;

    std::lock_guard<std::mutex> lock(pState->modelMutex);
    CompositeStockLoader::ClearCompositeStock(pState->compositeStock);
    CompositeStockLoader::ClearCompositeStock(pState->stagedStock);
    pState->hasModel = false;
    pState->currentUnitPath = L"";
    pState->displayName = L"";
    pState->typeName = L"";
    if (pState->hViewportWnd) InvalidateRect(pState->hViewportWnd, NULL, FALSE);
    InvalidateRect(hWnd, NULL, TRUE);
}

void UnitPreviewCard_SetDarkMode(HWND hWnd, BOOL bDark)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!pState) return;
    pState->bDarkMode = bDark;
    InvalidateRect(hWnd, NULL, TRUE);
}

bool UnitPreviewCard_HasModel(HWND hWnd)
{
    UnitPreviewState* pState = (UnitPreviewState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    return pState ? pState->hasModel : false;
}
