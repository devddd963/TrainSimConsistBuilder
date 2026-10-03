#pragma once

#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>

struct LoadedTexture {
    ID3D11ShaderResourceView* pSRV = nullptr;
    ID3D11Texture2D*          pTexture = nullptr;
    uint32_t                  width = 0;
    uint32_t                  height = 0;
    bool                      hasAlpha = false;
    bool                      hasSmoothAlpha = false;
};

class TextureLoader {
public:
    TextureLoader(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
    ~TextureLoader();

    // Resolves a texture name relative to shapeDir using canonical relative path resolution and smart .ace <-> .dds fallback
    static std::wstring ResolveTexturePath(const std::wstring& shapeDir, const std::wstring& rawImageName);

    // Pre-decodes textures into CPU memory cache (can be called safely from background worker threads)
    static void PredecodeTexturesCPU(const std::wstring& shapeDir, const std::vector<std::wstring>& imageNames);

    // Clears global CPU-side decoded texture cache
    static void ClearGlobalCPUCache();

    // Loads a texture (.ace or .dds) and returns a Direct3D 11 Shader Resource View (SRV)
    ID3D11ShaderResourceView* LoadTexture(const std::wstring& shapeDir, const std::wstring& rawImageName);

    // Pre-loads and decodes multiple textures in parallel across CPU worker threads
    void PreloadTextures(const std::wstring& shapeDir, const std::vector<std::wstring>& imageNames);

    // Checks whether a texture has an alpha or mask channel
    bool HasAlpha(const std::wstring& shapeDir, const std::wstring& rawImageName);

    // Checks whether a texture has smooth translucent alpha (e.g. glass)
    bool HasSmoothAlpha(const std::wstring& shapeDir, const std::wstring& rawImageName);

    // Clear texture cache and release GPU resources
    void ClearCache();

    // Returns a fallback 1x1 default texture (neutral gray)
    ID3D11ShaderResourceView* GetDefaultTexture();

private:
    ID3D11Device*        m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    ID3D11ShaderResourceView* m_pDefaultSRV = nullptr;
    ID3D11Texture2D*          m_pDefaultTexture = nullptr;

    std::unordered_map<std::wstring, LoadedTexture> m_cache;
    std::list<std::wstring> m_lruList;
    std::unordered_map<std::wstring, std::list<std::wstring>::iterator> m_lruMap;
    static constexpr size_t MAX_CACHE_SIZE = 256;

    void TouchLRU(const std::wstring& key);
    void InsertCache(const std::wstring& key, const LoadedTexture& tex);
    void EvictOldest();

    bool LoadAceTexture(const std::wstring& filePath, LoadedTexture& outTex);
    bool LoadDdsTexture(const std::wstring& filePath, LoadedTexture& outTex);
    void CreateDefaultTexture();
};
