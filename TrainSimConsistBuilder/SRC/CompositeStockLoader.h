#pragma once

#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include "StockSpecReader.h"
#include "ShapeReader.h"
#include "ShapeAnimator.h"
#include "TextureLoader.h"

// Represents a single 3D shape component within a compound rolling stock vehicle
struct SubShapeInstance
{
    std::wstring shapePath;
    std::wstring fullPath;
    std::wstring folderDir;
    
    ParsedShape shape;
    std::unique_ptr<TextureLoader> pTextureLoader;

    DirectX::XMFLOAT3 offset = { 0.0f, 0.0f, 0.0f };   // Sideways (X+), Up (Y+), Forward (Z+) in meters
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f }; // Pitch (X), Yaw (Y), Roll (Z) in degrees
    DirectX::XMMATRIX localMatrix = DirectX::XMMatrixIdentity();

    std::vector<DirectX::XMFLOAT4X4> boneMatrices;

    bool isPrimary = false;
    bool isDriver = false;
    bool isContinuous = false;
    bool isStatic = true;
    bool isAddedBoiler = false;
    bool isMSTS = false;
    bool isLoaded = false;
};

// Represents a fully assembled rolling stock unit (.wag or .eng) with all attached sub-shapes
struct CompositeStockUnit
{
    std::wstring stockFilePath;
    std::wstring stockFileName;
    std::wstring stockFolderName;

    StockSpecReader::StockSpec spec;
    std::vector<std::shared_ptr<SubShapeInstance>> subShapes;

    DirectX::XMFLOAT3 center = { 0.0f, 1.5f, 0.0f };
    float radius = 10.0f;
    DirectX::XMFLOAT3 boundsMin = { -2.0f, 0.0f, -10.0f };
    DirectX::XMFLOAT3 boundsMax = { 2.0f, 4.5f, 10.0f };

    bool isValid = false;
    uint64_t totalVertices = 0;
    uint64_t totalTriangles = 0;
    uint64_t totalSubMeshes = 0;
    uint64_t totalTextures = 0;

    bool hasAnimation = false;
    float maxAnimationFrames = 0.0f;
    float animationFrameRate = 30.0f;
    bool hasSimulatedWheels = false;
};

namespace CompositeStockLoader
{
    // Step 1: Parse .wag/.eng spec and all referenced shape files in parallel (CPU background worker thread)
    bool LoadCompositeStockCPU(
        const std::wstring& stockFilePath,
        const std::wstring& trainsetBasePath,
        CompositeStockUnit& outUnit
    );

    // Step 2: Initialize GPU vertex buffers, index buffers, and load textures for all sub-shapes (Render thread)
    bool FinalizeGPUResources(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        CompositeStockUnit& unit,
        TextureLoader* pSharedTextureLoader = nullptr
    );

    // Step 3: Evaluate hierarchical animations and procedural wheel rotations across all sub-shapes
    void UpdateCompositeAnimation(
        CompositeStockUnit& unit,
        float animFrame,
        int animFilterType,
        float wheelSpinAngle,
        bool enableWheelSpin
    );

    // Step 4: Hot-swap or update texture SRVs for all sub-shapes without rebuilding vertex/index buffers
    void HotSwapTextures(
        CompositeStockUnit& unit,
        TextureLoader* pSharedTextureLoader = nullptr
    );

    // Clean up GPU buffers and textures
    void ClearCompositeStock(CompositeStockUnit& unit);
}
