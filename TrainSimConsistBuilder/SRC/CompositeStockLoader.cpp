#include "CompositeStockLoader.h"
#include <future>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace CompositeStockLoader
{
    static std::wstring GetDirectoryFromPath(const std::wstring& filePath)
    {
        size_t lastSlash = filePath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            return filePath.substr(0, lastSlash + 1);
        }
        return L"";
    }

    bool LoadCompositeStockCPU(
        const std::wstring& stockFilePath,
        const std::wstring& trainsetBasePath,
        CompositeStockUnit& outUnit
    )
    {
        outUnit.isValid = false;
        outUnit.stockFilePath = stockFilePath;
        outUnit.stockFileName = std::filesystem::path(stockFilePath).filename().wstring();
        outUnit.stockFolderName = std::filesystem::path(stockFilePath).parent_path().filename().wstring();
        outUnit.subShapes.clear();

        // 1. Parse .wag or .eng specifications
        outUnit.spec = StockSpecReader::ReadFullSpec(stockFilePath, trainsetBasePath);
        if (!outUnit.spec.isValid && outUnit.spec.mainShapeFile.empty() && outUnit.spec.freightAnims.empty())
        {
            return false;
        }

        std::wstring stockDir = GetDirectoryFromPath(stockFilePath);

        // 2. Primary Root Shape
        if (!outUnit.spec.fullShapePath.empty() && outUnit.spec.shapeExistsOnDisk)
        {
            auto primarySub = std::make_shared<SubShapeInstance>();
            primarySub->isPrimary = true;
            primarySub->shapePath = outUnit.spec.mainShapeFile;
            primarySub->fullPath = outUnit.spec.fullShapePath;
            primarySub->folderDir = GetDirectoryFromPath(outUnit.spec.fullShapePath);
            if (primarySub->folderDir.empty()) primarySub->folderDir = stockDir;
            outUnit.subShapes.push_back(primarySub);
        }

        // 3. Freight Animations / ORTS Sub-Shapes
        for (const auto& fa : outUnit.spec.freightAnims)
        {
            if (fa.fullPath.empty() || !fa.existsOnDisk) continue;

            auto sub = std::make_shared<SubShapeInstance>();
            sub->isPrimary = false;
            sub->shapePath = fa.shapePath;
            sub->fullPath = fa.fullPath;
            sub->folderDir = GetDirectoryFromPath(fa.fullPath);
            if (sub->folderDir.empty()) sub->folderDir = stockDir;

            sub->offset = DirectX::XMFLOAT3(fa.offsetX, fa.offsetY, fa.offsetZ);
            sub->rotation = DirectX::XMFLOAT3(fa.rotX, fa.rotY, fa.rotZ);
            sub->isDriver = fa.isDriver;
            sub->isContinuous = fa.isContinuous;
            sub->isStatic = fa.isStatic;
            sub->isAddedBoiler = fa.isAddedBoiler;
            sub->isMSTS = fa.isMSTS;

            outUnit.subShapes.push_back(sub);
        }

        if (outUnit.subShapes.empty())
        {
            return false;
        }

        // 4. Parallel Shape Decoding across worker threads
        std::vector<std::future<bool>> parseFutures;
        for (auto& sub : outUnit.subShapes)
        {
            std::wstring fPath = sub->fullPath;
            ParsedShape* pShape = &sub->shape;
            parseFutures.push_back(std::async(std::launch::async, [fPath, pShape]() {
                return ShapeReader::ParseShapeFile(fPath, *pShape);
            }));
        }

        for (size_t i = 0; i < parseFutures.size(); ++i)
        {
            bool ok = parseFutures[i].get();
            outUnit.subShapes[i]->isLoaded = ok && outUnit.subShapes[i]->shape.isValid;
        }

        // 5. Asynchronously pre-decode all referenced textures into CPU memory cache (100% on background worker thread)
        for (const auto& sub : outUnit.subShapes)
        {
            if (sub && sub->isLoaded && !sub->shape.rawImageNames.empty())
            {
                TextureLoader::PredecodeTexturesCPU(sub->folderDir, sub->shape.rawImageNames);
                if (sub->folderDir != stockDir && !stockDir.empty())
                {
                    TextureLoader::PredecodeTexturesCPU(stockDir, sub->shape.rawImageNames);
                }
            }
        }

        return true;
    }

    bool FinalizeGPUResources(
        ID3D11Device* pDevice,
        ID3D11DeviceContext* pContext,
        CompositeStockUnit& unit,
        TextureLoader* pSharedTextureLoader
    )
    {
        if (!pDevice || !pContext || unit.subShapes.empty()) return false;

        unit.totalVertices = 0;
        unit.totalTriangles = 0;
        unit.totalSubMeshes = 0;
        unit.totalTextures = 0;
        unit.hasAnimation = false;
        unit.maxAnimationFrames = 0.0f;
        unit.animationFrameRate = 30.0f;
        unit.hasSimulatedWheels = false;

        DirectX::XMFLOAT3 minB = { 1e9f, 1e9f, 1e9f };
        DirectX::XMFLOAT3 maxB = { -1e9f, -1e9f, -1e9f };
        bool hasAnyValidSub = false;

        std::wstring stockDir = GetDirectoryFromPath(unit.stockFilePath);

        for (auto& sub : unit.subShapes)
        {
            if (!sub->isLoaded || !sub->shape.isValid) continue;

            // 1. Create GPU Vertex Buffer
            if (!sub->shape.vertices.empty())
            {
                D3D11_BUFFER_DESC vbd = {};
                vbd.Usage = D3D11_USAGE_DEFAULT;
                vbd.ByteWidth = (UINT)(sizeof(GPUVertex) * sub->shape.vertices.size());
                vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

                D3D11_SUBRESOURCE_DATA vData = {};
                vData.pSysMem = sub->shape.vertices.data();

                HRESULT hr = pDevice->CreateBuffer(&vbd, &vData, &sub->shape.pVertexBuffer);
                if (FAILED(hr)) continue;
            }

            // 2. Create GPU Index Buffer
            if (!sub->shape.indices.empty())
            {
                D3D11_BUFFER_DESC ibd = {};
                ibd.Usage = D3D11_USAGE_DEFAULT;
                ibd.ByteWidth = (UINT)(sizeof(uint32_t) * sub->shape.indices.size());
                ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

                D3D11_SUBRESOURCE_DATA iData = {};
                iData.pSysMem = sub->shape.indices.data();

                HRESULT hr = pDevice->CreateBuffer(&ibd, &iData, &sub->shape.pIndexBuffer);
                if (FAILED(hr)) continue;
            }

            // 3. Initialize Sub-Shape Texture Loader & Preload Textures (Shared Cache)
            TextureLoader* pTL = pSharedTextureLoader;
            if (!pTL)
            {
                sub->pTextureLoader = std::make_unique<TextureLoader>(pDevice, pContext);
                pTL = sub->pTextureLoader.get();
            }
            pTL->PreloadTextures(sub->folderDir, sub->shape.rawImageNames);

            // 4. Bind SRV for Each SubMesh with Fallback
            for (auto& sm : sub->shape.subMeshes)
            {
                const std::wstring& rawName = (sm.imageIndex >= 0 && (size_t)sm.imageIndex < sub->shape.rawImageNames.size())
                    ? sub->shape.rawImageNames[sm.imageIndex]
                    : sm.textureName;

                if (!rawName.empty())
                {
                    sm.pSRV = pTL->LoadTexture(sub->folderDir, rawName);
                    if (!sm.pSRV || sm.pSRV == pTL->GetDefaultTexture())
                    {
                        // Fallback search in vehicle stock directory
                        if (sub->folderDir != stockDir && !stockDir.empty())
                        {
                            ID3D11ShaderResourceView* fbSRV = pTL->LoadTexture(stockDir, rawName);
                            if (fbSRV && fbSRV != pTL->GetDefaultTexture())
                            {
                                sm.pSRV = fbSRV;
                            }
                        }
                    }

                    // Open Rails pipeline:
                    // 1. If texture has NO real alpha (e.g. body textures, chassis, bogies, trainboards),
                    //    it is strictly OPAQUE (Pass 1). This ensures locomotive body, roofs, and cabs render solid.
                    // 2. If texture has alpha cutout (e.g. couplers, grilles, pantographs, signs, springs),
                    //    it is ALPHA-TEST (Pass 2, depth write enabled).
                    // 3. Only genuine glass/window textures (e.g. Glass.ace) or translucent materials
                    //    render in Pass 3 (Translucent Glass Blend, depth read-only).
                    bool hasTexAlpha = pTL->HasAlpha(sub->folderDir, rawName);
                    if (!hasTexAlpha && sub->folderDir != stockDir && !stockDir.empty())
                    {
                        hasTexAlpha = pTL->HasAlpha(stockDir, rawName);
                    }
                    
                    std::wstring lowerTex = rawName;
                    for (wchar_t& c : lowerTex) c = towlower(c);
                    bool isGlassTex = (lowerTex.find(L"glass") != std::wstring::npos ||
                                       lowerTex.find(L"window") != std::wstring::npos ||
                                       lowerTex.find(L"mirror") != std::wstring::npos);

                    if (!hasTexAlpha)
                    {
                        sm.isTransparent = false;
                        sm.isAlphaTest = false;
                    }
                    else if (!isGlassTex)
                    {
                        sm.isTransparent = true;
                        sm.isAlphaTest = true;
                    }
                    else
                    {
                        sm.isTransparent = true;
                        sm.isAlphaTest = false;
                    }
                }
            }

            // 5. Compute Sub-Shape Local Matrix (Offset + Rotation)
            // Note: ORTS / TSRE5 freight anim longitudinal offset is inverted relative to D3D LH world Z
            DirectX::XMMATRIX rotM = DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(sub->rotation.x),
                DirectX::XMConvertToRadians(sub->rotation.y),
                DirectX::XMConvertToRadians(sub->rotation.z)
            );
            DirectX::XMMATRIX transM = DirectX::XMMatrixTranslation(
                sub->offset.x,
                sub->offset.y,
                -sub->offset.z
            );
            sub->localMatrix = rotM * transM;

            // 6. Initialize Static / Initial Bone Transforms
            ShapeAnimator::ComputeAnimatedMatrices(
                sub->shape,
                0.0f,
                0,
                0.0f,
                true,
                sub->boneMatrices
            );

            // 7. Aggregate Statistics
            unit.totalVertices += sub->shape.vertices.size();
            unit.totalTriangles += sub->shape.indices.size() / 3;
            unit.totalSubMeshes += sub->shape.subMeshes.size();
            unit.totalTextures += sub->shape.rawImageNames.size();

            if (sub->shape.animation.hasAnimation)
            {
                unit.hasAnimation = true;
                if (sub->shape.animation.frameCount > unit.maxAnimationFrames)
                {
                    unit.maxAnimationFrames = sub->shape.animation.frameCount;
                    unit.animationFrameRate = sub->shape.animation.frameRate;
                }
            }
            if (sub->shape.animation.hasSimulatedWheels)
            {
                unit.hasSimulatedWheels = true;
            }

            // 8. Transform Sub-Shape Bounding Box to Aggregate Composite Bounds
            if (sub->isPrimary || (sub->shape.radius > 0.05f))
            {
                DirectX::XMVECTOR vMin = DirectX::XMVectorSet(sub->shape.boundsMin.x, sub->shape.boundsMin.y, sub->shape.boundsMin.z, 1.0f);
                DirectX::XMVECTOR vMax = DirectX::XMVectorSet(sub->shape.boundsMax.x, sub->shape.boundsMax.y, sub->shape.boundsMax.z, 1.0f);

                vMin = DirectX::XMVector3Transform(vMin, sub->localMatrix);
                vMax = DirectX::XMVector3Transform(vMax, sub->localMatrix);

                DirectX::XMFLOAT3 tMin, tMax;
                DirectX::XMStoreFloat3(&tMin, vMin);
                DirectX::XMStoreFloat3(&tMax, vMax);

                minB.x = (std::min)({ minB.x, tMin.x, tMax.x });
                minB.y = (std::min)({ minB.y, tMin.y, tMax.y });
                minB.z = (std::min)({ minB.z, tMin.z, tMax.z });

                maxB.x = (std::max)({ maxB.x, tMin.x, tMax.x });
                maxB.y = (std::max)({ maxB.y, tMin.y, tMax.y });
                maxB.z = (std::max)({ maxB.z, tMin.z, tMax.z });

                hasAnyValidSub = true;
            }
        }

        if (hasAnyValidSub)
        {
            unit.boundsMin = minB;
            unit.boundsMax = maxB;
            unit.center = DirectX::XMFLOAT3(
                (minB.x + maxB.x) * 0.5f,
                (minB.y + maxB.y) * 0.5f,
                (minB.z + maxB.z) * 0.5f
            );

            float dx = maxB.x - minB.x;
            float dy = maxB.y - minB.y;
            float dz = maxB.z - minB.z;
            unit.radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
            if (unit.radius < 2.0f) unit.radius = 2.0f;

            unit.isValid = true;
            return true;
        }

        return false;
    }

    void UpdateCompositeAnimation(
        CompositeStockUnit& unit,
        float animFrame,
        int animFilterType,
        float wheelSpinAngle,
        bool enableWheelSpin
    )
    {
        if (!unit.isValid) return;

        for (auto& sub : unit.subShapes)
        {
            if (!sub->isLoaded || !sub->shape.isValid) continue;

            ShapeAnimator::ComputeAnimatedMatrices(
                sub->shape,
                animFrame,
                animFilterType,
                wheelSpinAngle,
                enableWheelSpin,
                sub->boneMatrices
            );
        }
    }

    void HotSwapTextures(CompositeStockUnit& unit, TextureLoader* pSharedTextureLoader)
    {
        if (unit.subShapes.empty()) return;
        std::wstring stockDir = GetDirectoryFromPath(unit.stockFilePath);

        for (auto& sub : unit.subShapes)
        {
            if (!sub->isLoaded || !sub->shape.isValid) continue;

            TextureLoader* pTL = pSharedTextureLoader ? pSharedTextureLoader : sub->pTextureLoader.get();
            if (!pTL) continue;

            for (auto& sm : sub->shape.subMeshes)
            {
                const std::wstring& rawName = (sm.imageIndex >= 0 && (size_t)sm.imageIndex < sub->shape.rawImageNames.size())
                    ? sub->shape.rawImageNames[sm.imageIndex]
                    : sm.textureName;

                if (!rawName.empty())
                {
                    ID3D11ShaderResourceView* srv = pTL->LoadTexture(sub->folderDir, rawName);
                    if (!srv || srv == pTL->GetDefaultTexture())
                    {
                        if (sub->folderDir != stockDir && !stockDir.empty())
                        {
                            ID3D11ShaderResourceView* fbSRV = pTL->LoadTexture(stockDir, rawName);
                            if (fbSRV && fbSRV != pTL->GetDefaultTexture())
                            {
                                srv = fbSRV;
                            }
                        }
                    }
                    if (srv) sm.pSRV = srv;

                    bool hasTexAlpha = pTL->HasAlpha(sub->folderDir, rawName);
                    if (!hasTexAlpha && sub->folderDir != stockDir && !stockDir.empty())
                    {
                        hasTexAlpha = pTL->HasAlpha(stockDir, rawName);
                    }

                    std::wstring lowerTex = rawName;
                    for (wchar_t& c : lowerTex) c = towlower(c);
                    bool isGlass = (lowerTex.find(L"glass") != std::wstring::npos ||
                                    lowerTex.find(L"window") != std::wstring::npos ||
                                    lowerTex.find(L"mirror") != std::wstring::npos);

                    if (!hasTexAlpha)
                    {
                        sm.isTransparent = false;
                        sm.isAlphaTest = false;
                    }
                    else if (!isGlass)
                    {
                        sm.isTransparent = true;
                        sm.isAlphaTest = true;
                    }
                    else
                    {
                        sm.isTransparent = true;
                        sm.isAlphaTest = false;
                    }
                }
            }
        }
    }

    void ClearCompositeStock(CompositeStockUnit& unit)
    {
        for (auto& sub : unit.subShapes)
        {
            if (sub->shape.pVertexBuffer)
            {
                sub->shape.pVertexBuffer->Release();
                sub->shape.pVertexBuffer = nullptr;
            }
            if (sub->shape.pIndexBuffer)
            {
                sub->shape.pIndexBuffer->Release();
                sub->shape.pIndexBuffer = nullptr;
            }
            if (sub->pTextureLoader)
            {
                sub->pTextureLoader->ClearCache();
                sub->pTextureLoader.reset();
            }
            sub->boneMatrices.clear();
            sub->isLoaded = false;
        }
        unit.subShapes.clear();
        unit.isValid = false;
    }
}
