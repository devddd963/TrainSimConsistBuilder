#include "CompositeStockLoader.h"
#include "AppLogging.h"
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
        CompositeStockUnit& outUnit,
        const std::atomic<bool>* pCancelToken
    )
    {
        if (pCancelToken && *pCancelToken) return false;

        outUnit.isValid = false;
        outUnit.stockFilePath = stockFilePath;
        outUnit.stockFileName = std::filesystem::path(stockFilePath).filename().wstring();
        outUnit.stockFolderName = std::filesystem::path(stockFilePath).parent_path().filename().wstring();
        outUnit.subShapes.clear();

        LOG_INFO_W(L"[CompositeStockLoader] Loading composite rolling stock: %ls", stockFilePath.c_str());

        std::wstring stockDir = GetDirectoryFromPath(stockFilePath);

        // If file directly ends with .s, load as a standalone shape
        std::wstring lowerPath = stockFilePath;
        for (wchar_t& c : lowerPath) c = towlower(c);
        if (lowerPath.size() >= 2 && lowerPath.substr(lowerPath.size() - 2) == L".s")
        {
            auto primarySub = std::make_shared<SubShapeInstance>();
            primarySub->isPrimary = true;
            primarySub->shapePath = std::filesystem::path(stockFilePath).filename().wstring();
            primarySub->fullPath = stockFilePath;
            primarySub->folderDir = stockDir;
            outUnit.subShapes.push_back(primarySub);
            LOG_INFO_W(L"[CompositeStockLoader] Standalone .s shape loaded: %ls", stockFilePath.c_str());
        }
        else
        {
            // 1. Parse .wag or .eng specifications
            outUnit.spec = StockSpecReader::ReadFullSpec(stockFilePath, trainsetBasePath);
            if (pCancelToken && *pCancelToken) return false;

            if (!outUnit.spec.isValid && outUnit.spec.mainShapeFile.empty() && outUnit.spec.freightAnims.empty())
            {
                LOG_STOCK_ERROR_W(L"[CompositeStockLoader] Failed to read spec for: %ls", stockFilePath.c_str());
                return false;
            }

            LOG_STOCK_W(L"--------------------------------------------------------------------------------");
            LOG_STOCK_W(L"Spec Parsed: Name='%ls', Category='%ls', PrimaryShape='%ls' (ExistsOnDisk: %ls), FreightAnims=%zu",
                outUnit.spec.displayName.c_str(), outUnit.spec.category.c_str(),
                outUnit.spec.mainShapeFile.c_str(), outUnit.spec.shapeExistsOnDisk ? L"YES" : L"NO",
                outUnit.spec.freightAnims.size());

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
                LOG_STOCK_W(L"  -> Primary shape resolved: %ls", primarySub->fullPath.c_str());
            }
            else if (!outUnit.spec.mainShapeFile.empty())
            {
                LOG_STOCK_WARN_W(L"  [WARN] Primary shape '%ls' not found on disk for '%ls'",
                    outUnit.spec.mainShapeFile.c_str(), stockFilePath.c_str());
            }

            // 3. Freight Animations / ORTS Sub-Shapes
            for (const auto& fa : outUnit.spec.freightAnims)
            {
                if (fa.fullPath.empty() || !fa.existsOnDisk)
                {
                    LOG_STOCK_WARN_W(L"  [WARN] FreightAnim shape '%ls' not found on disk", fa.shapePath.c_str());
                    continue;
                }

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
                LOG_STOCK_W(L"  -> FreightAnim attached: '%ls' -> Offset=[%.2f, %.2f, %.2f], Rot=[%.1f, %.1f, %.1f]",
                    sub->shapePath.c_str(), sub->offset.x, sub->offset.y, sub->offset.z, sub->rotation.x, sub->rotation.y, sub->rotation.z);
            }
        }

        if (pCancelToken && *pCancelToken) return false;

        if (outUnit.subShapes.empty())
        {
            LOG_STOCK_ERROR_W(L"[CompositeStockLoader] No valid sub-shapes found for vehicle: %ls", stockFilePath.c_str());
            return false;
        }

        // 4. Parallel Shape Decoding across worker threads
        std::vector<std::future<bool>> parseFutures;
        for (auto& sub : outUnit.subShapes)
        {
            if (pCancelToken && *pCancelToken) return false;
            std::wstring fPath = sub->fullPath;
            ParsedShape* pShape = &sub->shape;
            parseFutures.push_back(std::async(std::launch::async, [fPath, pShape, pCancelToken]() {
                return ShapeReader::ParseShapeFile(fPath, *pShape, pCancelToken);
            }));
        }

        for (size_t i = 0; i < parseFutures.size(); ++i)
        {
            if (pCancelToken && *pCancelToken) return false;
            bool ok = parseFutures[i].get();
            outUnit.subShapes[i]->isLoaded = ok && outUnit.subShapes[i]->shape.isValid;
        }

        if (pCancelToken && *pCancelToken) return false;

        LOG_INFO_W(L"[CompositeStockLoader] Parsed %zu sub-shape(s) for '%ls'", outUnit.subShapes.size(), outUnit.stockFileName.c_str());

        // 5. Asynchronously pre-decode all referenced textures into CPU memory cache (100% on background worker thread)
        for (const auto& sub : outUnit.subShapes)
        {
            if (pCancelToken && *pCancelToken) return false;
            if (sub && sub->isLoaded && !sub->shape.rawImageNames.empty())
            {
                TextureLoader::PredecodeTexturesCPU(sub->folderDir, sub->shape.rawImageNames, pCancelToken);
                if (sub->folderDir != stockDir && !stockDir.empty())
                {
                    TextureLoader::PredecodeTexturesCPU(stockDir, sub->shape.rawImageNames, pCancelToken);
                }
            }
        }

        if (pCancelToken && *pCancelToken) return false;

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

                    bool hasTexAlpha = pTL->HasAlpha(sub->folderDir, rawName);
                    bool hasSmoothAlpha = pTL->HasSmoothAlpha(sub->folderDir, rawName);
                    if (!hasTexAlpha && sub->folderDir != stockDir && !stockDir.empty())
                    {
                        hasTexAlpha = pTL->HasAlpha(stockDir, rawName);
                        hasSmoothAlpha = pTL->HasSmoothAlpha(stockDir, rawName);
                    }

                    ShapeReader::ClassifySubMeshMaterial(sm, hasTexAlpha, hasSmoothAlpha, rawName);
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

            LOG_INFO_W(L"[CompositeStockLoader] Finalized GPU Model '%ls': SubShapes=%zu, Vertices=%llu, Triangles=%llu, SubMeshes=%llu, Textures=%llu, Bounds=[(%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)], Radius=%.2fm, Animated=%ls",
                unit.stockFileName.c_str(), unit.subShapes.size(),
                (unsigned long long)unit.totalVertices, (unsigned long long)unit.totalTriangles,
                (unsigned long long)unit.totalSubMeshes, (unsigned long long)unit.totalTextures,
                unit.boundsMin.x, unit.boundsMin.y, unit.boundsMin.z,
                unit.boundsMax.x, unit.boundsMax.y, unit.boundsMax.z,
                unit.radius, unit.hasAnimation ? L"YES" : L"NO");

            LOG_SHAPE_W(L"=== Composite Stock GPU Pipeline Audit: '%ls' (SubShapes: %zu) ===", unit.stockFileName.c_str(), unit.subShapes.size());
            for (size_t sIdx = 0; sIdx < unit.subShapes.size(); ++sIdx)
            {
                const auto& sub = unit.subShapes[sIdx];
                if (!sub || !sub->isLoaded || !sub->shape.isValid) continue;
                LOG_SHAPE_W(L" -> SubShape #%zu: '%ls' (SubMeshes: %zu, Vertices: %zu, Indices: %zu)",
                    sIdx, sub->shapePath.c_str(), sub->shape.subMeshes.size(), sub->shape.vertices.size(), sub->shape.indices.size());
                for (size_t smIdx = 0; smIdx < sub->shape.subMeshes.size(); ++smIdx)
                {
                    const auto& sm = sub->shape.subMeshes[smIdx];
                    const wchar_t* passName = L"Pass 1 [Opaque Solid]";
                    if (sm.isTransparent && sm.isAlphaTest) passName = L"Pass 2 [AlphaTest Cutout]";
                    else if (sm.isTransparent && !sm.isAlphaTest) passName = L"Pass 3 [Smooth Glass Blend]";

                    std::wstring wShader(sm.shaderName.begin(), sm.shaderName.end());
                    LOG_SHAPE_W(L"    [SubMesh #%02zu] Indices: %5u (Start: %5u) | Shader: '%-14ls' | Texture: '%-28ls' | Applied: %-26ls | SRV: %ls",
                        smIdx, sm.indexCount, sm.startIndex,
                        wShader.empty() ? L"(None)" : wShader.c_str(),
                        sm.textureName.empty() ? L"(Untextured)" : sm.textureName.c_str(),
                        passName,
                        sm.pSRV ? L"Allocated" : L"Default");
                }
            }
            LOG_SHAPE_W(L"--------------------------------------------------------------------------------");

            return true;
        }

        LOG_ERROR_W(L"[CompositeStockLoader] GPU finalization failed for '%ls' - no valid subshapes", unit.stockFileName.c_str());
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
                    bool hasSmoothAlpha = pTL->HasSmoothAlpha(sub->folderDir, rawName);
                    if (!hasTexAlpha && sub->folderDir != stockDir && !stockDir.empty())
                    {
                        hasTexAlpha = pTL->HasAlpha(stockDir, rawName);
                        hasSmoothAlpha = pTL->HasSmoothAlpha(stockDir, rawName);
                    }

                    ShapeReader::ClassifySubMeshMaterial(sm, hasTexAlpha, hasSmoothAlpha, rawName);
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
