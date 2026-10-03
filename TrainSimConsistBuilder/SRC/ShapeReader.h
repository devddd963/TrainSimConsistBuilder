#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include "TextureLoader.h"

// Direct3D 11 Vertex Layout for Shape Rendering (36 bytes)
struct GPUVertex {
    DirectX::XMFLOAT3 pos;       // Position in 3D space (MSTS LH: +X Right, +Y Up, +Z Forward)
    DirectX::XMFLOAT3 normal;    // Vertex Normal
    DirectX::XMFLOAT2 uv;        // Texture coordinates (U, V)
    uint32_t          boneIndex; // Matrix index for node hierarchy transform
};

// SubMesh grouping by Material (Texture + Blend Mode)
struct ShapeSubMesh {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    int32_t  imageIndex = -1;    // Index into ParsedShape::rawImageNames (-1 = untextured)
    uint32_t shaderIndex = 0;
    bool     isTransparent = false;
    bool     isAlphaTest = false;
    std::wstring textureName;
    ID3D11ShaderResourceView* pSRV = nullptr; // Resolved SRV
};

// Animation Node Category Classification
enum class AnimNodeType {
    Unknown = 0,
    Pantograph,
    DoorOrMirror,
    Wiper,
    WheelOrBogie,
    FanOrBlower,
    DriverOrCrew,
    DisplayOrBoard,
    Custom
};

// Keyframe Position Track (x, y, z)
struct KeyframePos {
    float frame = 0.0f;
    DirectX::XMFLOAT3 pos = { 0.0f, 0.0f, 0.0f };
};

// Keyframe Rotation Track (Quaternion: x, y, z, w)
struct KeyframeRot {
    float frame = 0.0f;
    DirectX::XMFLOAT4 quat = { 0.0f, 0.0f, 0.0f, 1.0f };
};

// Animation Track per Hierarchical Node
struct AnimNodeTrack {
    std::string  name;
    int32_t      nodeIndex = -1; // Corresponding index in ParsedShape::boneNames
    AnimNodeType type = AnimNodeType::Custom;
    std::vector<KeyframePos> posKeys;
    std::vector<KeyframeRot> rotKeys;
    DirectX::XMFLOAT3 bindPos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 bindRotQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 bindScale = { 1.0f, 1.0f, 1.0f };
    bool hasAnim = false;
};

// Simulated Procedural Wheel Node Track
struct SimulatedWheelNode {
    int32_t     nodeIndex = -1;
    std::string name;
};

// Complete Animation Sequence for a Shape
struct ShapeAnimation {
    bool  hasAnimation = false;
    float frameCount = 0.0f;
    float actualMaxKeyframe = 0.0f;
    float frameRate = 30.0f;
    std::vector<AnimNodeTrack> animNodes;

    // Simulated Wheel Roll (Procedural rotation without keyframes)
    bool hasSimulatedWheels = false;
    std::vector<SimulatedWheelNode> simulatedWheels;

    // Sub-category node counters
    int countPanto = 0;
    int countDoor = 0;
    int countWiper = 0;
    int countWheel = 0;
    int countFan = 0;
    int countDriver = 0;
    int countDisplay = 0;
    int countCustom = 0;
};

// Complete In-Memory Parsed Shape Model
struct ParsedShape {
    std::wstring shapeFilePath;
    std::wstring shapeDir;

    std::vector<GPUVertex>    vertices;
    std::vector<uint32_t>     indices;
    std::vector<ShapeSubMesh> subMeshes;
    std::vector<std::wstring> rawImageNames; // Images listed in shape file images()

    // Hierarchical Node Matrices
    std::vector<int32_t>             boneHierarchy;     // Parent node index (-1 = root)
    std::vector<DirectX::XMFLOAT4X4> boneMatrices;      // Local bind pose matrices
    std::vector<DirectX::XMFLOAT4X4> bindWorldMatrices; // World bind pose matrices
    std::vector<std::string>         boneNames;

    // 3D Animation Data
    ShapeAnimation animation;

    // Bounding Box and Sphere (Calculated on load)
    DirectX::XMFLOAT3 boundsMin = {  1e9f,  1e9f,  1e9f };
    DirectX::XMFLOAT3 boundsMax = { -1e9f, -1e9f, -1e9f };
    DirectX::XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
    float radius = 1.0f;

    // Direct3D 11 GPU Buffers
    ID3D11Buffer* pVertexBuffer = nullptr;
    ID3D11Buffer* pIndexBuffer = nullptr;

    bool isValid = false;

    void ReleaseGPUBuffers() {
        if (pVertexBuffer) { pVertexBuffer->Release(); pVertexBuffer = nullptr; }
        if (pIndexBuffer) { pIndexBuffer->Release(); pIndexBuffer = nullptr; }
    }

    ~ParsedShape() {
        ReleaseGPUBuffers();
    }
};

class ShapeReader {
public:
    ShapeReader() = default;
    ~ShapeReader() = default;

    // Fast CPU-only shape parser (Thread-safe, no Direct3D device required)
    static bool ParseShapeFile(
        const std::wstring& shapeFilePath,
        ParsedShape& outShape
    );

    // Direct3D 11 GPU Buffer and Texture Resource Creator (Must run on D3D thread)
    static bool CreateGPUBuffers(
        ID3D11Device* pDevice,
        TextureLoader* pTextureLoader,
        ParsedShape& shape
    );

    // Hot-swaps or updates texture SRVs on an already GPU-allocated ParsedShape without reallocating vertex/index buffers
    static void HotSwapTextures(
        TextureLoader* pTextureLoader,
        ParsedShape& shape
    );

    // High-performance loader: ParseShapeFile + CreateGPUBuffers
    static bool LoadShapeFile(
        const std::wstring& shapeFilePath,
        ID3D11Device* pDevice,
        TextureLoader* pTextureLoader,
        ParsedShape& outShape
    );

    // Fast header inspect to query image names without full vertex buffer creation
    static bool GetShapeImages(
        const std::wstring& shapeFilePath,
        std::vector<std::wstring>& outImageNames
    );

    // Automatic classification of animation node by name
    static AnimNodeType CategorizeNodeName(const std::string& rawName);

    // Normalize node name for robust fuzzy matching (lowercase alphanumeric only)
    static std::string NormalizeNodeName(const std::string& name);

    // Real-time animation matrix evaluation with SLERP, LERPs and procedural wheel rotation
    static void ComputeAnimatedMatrices(
        const ParsedShape& shape,
        float currentFrame,
        int filterType, // 0 = All, 1 = Pantos, 2 = Doors, 3 = Wipers, 4 = Keyframed Wheels, 5 = Fans, 6 = Driver, 7 = Displays, 8 = Custom, >=100 specific keyframe node index, 20 = All Simulated Wheels, >=200 specific simulated wheel node index
        float wheelSpinAngle,
        bool enableWheelSpin,
        std::vector<DirectX::XMFLOAT4X4>& outWorldMatrices
    );
};

