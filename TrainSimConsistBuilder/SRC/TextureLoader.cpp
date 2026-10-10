#include "TextureLoader.h"
#include "AppLogging.h"
#include <shlwapi.h>
#include <algorithm>
#include <future>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include <unordered_set>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// DirectDraw Surface (DDS) Definitions
#define DDS_MAGIC 0x20534444 // "DDS "

struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t        dwSize;
    uint32_t        dwFlags;
    uint32_t        dwHeight;
    uint32_t        dwWidth;
    uint32_t        dwPitchOrLinearSize;
    uint32_t        dwDepth;
    uint32_t        dwMipMapCount;
    uint32_t        dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        dwCaps;
    uint32_t        dwCaps2;
    uint32_t        dwCaps3;
    uint32_t        dwCaps4;
    uint32_t        dwReserved2;
};

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
    ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24))
#endif

// =========================================================================
// High-Performance RFC 1951 Deflate / Zlib Decompressor (FastZlib)
// Direct 9-bit Lookup Table (LUT) + 64-bit sliding bit-buffer for 100x speed
// =========================================================================
namespace FastZlib
{
    static inline uint16_t ReverseBits(uint16_t v, int bits) {
        uint16_t r = 0;
        for (int i = 0; i < bits; ++i) {
            r = (r << 1) | (v & 1);
            v >>= 1;
        }
        return r;
    }

    struct BitStream {
        const uint8_t* src;
        size_t srcLen;
        size_t bytePos;
        uint64_t bitBuf;
        int bitsInBuf;

        inline void Refill() {
            while (bitsInBuf <= 32 && bytePos + 4 <= srcLen) {
                uint32_t val;
                std::memcpy(&val, src + bytePos, 4);
                bitBuf |= ((uint64_t)val) << bitsInBuf;
                bytePos += 4;
                bitsInBuf += 32;
            }
            while (bitsInBuf <= 56 && bytePos < srcLen) {
                bitBuf |= ((uint64_t)src[bytePos++]) << bitsInBuf;
                bitsInBuf += 8;
            }
        }

        inline uint32_t Peek(int n) const {
            return (uint32_t)(bitBuf & ((1ULL << n) - 1));
        }

        inline void Drop(int n) {
            bitBuf >>= n;
            bitsInBuf -= n;
        }

        inline uint32_t Get(int n) {
            Refill();
            uint32_t res = (uint32_t)(bitBuf & ((1ULL << n) - 1));
            bitBuf >>= n;
            bitsInBuf -= n;
            return res;
        }
    };

    struct FastHuffTable {
        uint16_t fast[512]; // 9-bit primary LUT
        struct OverflowEntry {
            uint16_t code;
            uint8_t  len;
            uint16_t sym;
        };
        uint16_t numOverflow;
        OverflowEntry overflow[288];
    };

    static bool BuildFastHuff(FastHuffTable& table, const uint8_t* lengths, int numSymbols) {
        std::memset(table.fast, 0, sizeof(table.fast));
        table.numOverflow = 0;

        uint16_t bl_count[16] = { 0 };
        for (int i = 0; i < numSymbols; ++i) {
            if (lengths[i] > 15) return false;
            bl_count[lengths[i]]++;
        }
        bl_count[0] = 0;

        uint16_t next_code[16] = { 0 };
        uint16_t code = 0;
        for (int bits = 1; bits <= 15; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        for (int i = 0; i < numSymbols; ++i) {
            int len = lengths[i];
            if (len == 0) continue;

            uint16_t c = next_code[len]++;
            uint16_t rev = ReverseBits(c, len);

            if (len <= 9) {
                uint16_t entry = (uint16_t)((i << 4) | len);
                int step = 1 << len;
                for (int idx = rev; idx < 512; idx += step) {
                    table.fast[idx] = entry;
                }
            } else {
                int fastIdx = rev & 0x1FF;
                table.fast[fastIdx] = 0x8000;

                if (table.numOverflow < 288) {
                    table.overflow[table.numOverflow++] = { rev, (uint8_t)len, (uint16_t)i };
                }
            }
        }
        return true;
    }

    static inline int DecodeFastSymbol(BitStream& bs, const FastHuffTable& table) {
        bs.Refill();
        uint32_t peek9 = bs.Peek(9);
        uint16_t entry = table.fast[peek9];
        if ((entry & 0x8000) == 0 && entry != 0) {
            int len = entry & 0x0F;
            bs.Drop(len);
            return entry >> 4;
        }

        if (entry == 0x8000) {
            uint32_t peek15 = bs.Peek(15);
            for (int i = 0; i < table.numOverflow; ++i) {
                const auto& ov = table.overflow[i];
                if ((peek15 & ((1U << ov.len) - 1)) == ov.code) {
                    bs.Drop(ov.len);
                    return ov.sym;
                }
            }
        }

        return -1;
    }

    static const uint16_t kLenBases[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t kLenExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const uint16_t kDistBases[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
        513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static const uint8_t kDistExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static const uint8_t kClenOrder[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    static bool InflateBlock(BitStream& bs, const FastHuffTable& lt, const FastHuffTable& dt, std::vector<uint8_t>& out) {
        while (true) {
            int sym = DecodeFastSymbol(bs, lt);
            if (sym < 0 || sym > 285) return false;
            if (sym == 256) break;

            if (sym < 256) {
                out.push_back((uint8_t)sym);
            } else {
                sym -= 257;
                int len = kLenBases[sym] + (int)bs.Get(kLenExtra[sym]);
                int distSym = DecodeFastSymbol(bs, dt);
                if (distSym < 0 || distSym >= 30) return false;
                int dist = kDistBases[distSym] + (int)bs.Get(kDistExtra[distSym]);

                if (dist <= 0 || dist > (int)out.size()) return false;
                size_t start = out.size() - dist;

                size_t curSize = out.size();
                out.resize(curSize + len);
                uint8_t* pDst = out.data() + curSize;
                const uint8_t* pSrc = out.data() + start;

                if (dist >= len) {
                    std::memcpy(pDst, pSrc, len);
                } else {
                    for (int i = 0; i < len; ++i) {
                        pDst[i] = pSrc[i];
                    }
                }
            }
        }
        return true;
    }

    static bool DecompressZlib(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
        if (!src || srcLen < 2) return false;

        size_t startOffset = 0;
        if (src[0] == 0x78 && (src[1] == 0x9C || src[1] == 0xDA || src[1] == 0x01 || src[1] == 0x5E)) {
            startOffset = 2;
        }

        BitStream bs = { src, srcLen, startOffset, 0, 0 };
        bs.Refill();

        bool isFinal = false;
        out.reserve(srcLen * 4);

        while (!isFinal) {
            bs.Refill();
            if (bs.bytePos >= bs.srcLen && bs.bitsInBuf < 3) break;

            isFinal = bs.Get(1) != 0;
            int type = (int)bs.Get(2);

            if (type == 0) {
                int align = bs.bitsInBuf % 8;
                if (align > 0) bs.Drop(align);

                uint16_t len = (uint16_t)bs.Get(16);
                uint16_t nlen = (uint16_t)bs.Get(16);

                size_t curSize = out.size();
                out.resize(curSize + len);
                uint8_t* pDst = out.data() + curSize;

                for (uint16_t i = 0; i < len; ++i) {
                    if (bs.bitsInBuf >= 8) {
                        pDst[i] = (uint8_t)bs.Get(8);
                    } else if (bs.bytePos < bs.srcLen) {
                        pDst[i] = bs.src[bs.bytePos++];
                    } else {
                        return false;
                    }
                }
            } else if (type == 1) {
                uint8_t ll[288];
                for (int i = 0; i <= 143; ++i) ll[i] = 8;
                for (int i = 144; i <= 255; ++i) ll[i] = 9;
                for (int i = 256; i <= 279; ++i) ll[i] = 7;
                for (int i = 280; i <= 287; ++i) ll[i] = 8;
                uint8_t dl[32];
                for (int i = 0; i < 32; ++i) dl[i] = 5;

                FastHuffTable lt, dt;
                BuildFastHuff(lt, ll, 288);
                BuildFastHuff(dt, dl, 32);

                if (!InflateBlock(bs, lt, dt, out)) return false;
            } else if (type == 2) {
                bs.Refill();
                int hlit = (int)bs.Get(5) + 257;
                int hdist = (int)bs.Get(5) + 1;
                int hclen = (int)bs.Get(4) + 4;

                uint8_t clen[19] = { 0 };
                for (int i = 0; i < hclen; ++i) {
                    clen[kClenOrder[i]] = (uint8_t)bs.Get(3);
                }

                FastHuffTable ct;
                BuildFastHuff(ct, clen, 19);

                uint8_t lens[320] = { 0 };
                int numLens = hlit + hdist;
                int idx = 0;

                while (idx < numLens) {
                    bs.Refill();
                    int sym = DecodeFastSymbol(bs, ct);
                    if (sym < 0) return false;

                    if (sym < 16) {
                        lens[idx++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        if (idx == 0) return false;
                        uint8_t prev = lens[idx - 1];
                        int repeat = 3 + (int)bs.Get(2);
                        while (repeat-- > 0 && idx < numLens) lens[idx++] = prev;
                    } else if (sym == 17) {
                        int repeat = 3 + (int)bs.Get(3);
                        while (repeat-- > 0 && idx < numLens) lens[idx++] = 0;
                    } else if (sym == 18) {
                        int repeat = 11 + (int)bs.Get(7);
                        while (repeat-- > 0 && idx < numLens) lens[idx++] = 0;
                    }
                }

                FastHuffTable lt, dt;
                BuildFastHuff(lt, lens, hlit);
                BuildFastHuff(dt, lens + hlit, hdist);

                if (!InflateBlock(bs, lt, dt, out)) return false;
            } else {
                return false;
            }
        }
        return true;
    }
}

// =========================================================================
// CPU Decoded Texture Intermediate Struct (for Parallel Texture Preload)
// =========================================================================
struct DecodedTextureData {
    std::wstring resolvedPath;
    std::wstring key;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool hasAlpha = false;
    bool hasSmoothAlpha = false;
    bool isValid = false;

    struct MipData {
        std::vector<uint8_t> data;
        uint32_t sysMemPitch;
        uint32_t sysMemSlicePitch;
    };
    std::vector<MipData> mips;
};

struct AceChannelDesc {
    int size;
    int type; // 2=Mask, 3=Red, 4=Green, 5=Blue, 6=Alpha
};

static bool DecodeAceToMemory(const std::wstring& filePath, DecodedTextureData& outData)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < 16)
    {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap)
    {
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pRaw = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pRaw)
    {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pData = pRaw;
    size_t dataLen = fileSize;
    std::vector<uint8_t> decompBuffer;

    // Handle SIMISA Compression Wrappers
    if (dataLen >= 16 && std::memcmp(pData, "SIMISA@F", 8) == 0)
    {
        if (!FastZlib::DecompressZlib(pData + 16, dataLen - 16, decompBuffer))
        {
            LOG_ERROR_W(L"[TextureLoader] FastZlib decompression failed for ACE texture: %ls", filePath.c_str());
            UnmapViewOfFile(pRaw);
            CloseHandle(hMap);
            CloseHandle(hFile);
            return false;
        }
        pData = decompBuffer.data();
        dataLen = decompBuffer.size();
    }
    else if (dataLen >= 16 && std::memcmp(pData, "SIMISA@@", 8) == 0)
    {
        pData += 16;
        dataLen -= 16;
    }

    if (dataLen < 148)
    {
        LOG_WARN_W(L"[TextureLoader] Truncated ACE texture data (%llu bytes): %ls", (unsigned long long)dataLen, filePath.c_str());
        UnmapViewOfFile(pRaw);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    uint32_t signature = *(const uint32_t*)pData;
    if (signature != 1)
    {
        LOG_WARN_W(L"[TextureLoader] Unexpected ACE signature (0x%08X): %ls", signature, filePath.c_str());
        UnmapViewOfFile(pRaw);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    uint32_t options = *(const uint32_t*)(pData + 4);
    uint32_t width = *(const uint32_t*)(pData + 8);
    uint32_t height = *(const uint32_t*)(pData + 12);
    uint32_t surfaceFormat = *(const uint32_t*)(pData + 16);
    uint32_t channelCount = *(const uint32_t*)(pData + 20);

    if (width == 0 || height == 0 || width > 8192 || height > 8192)
    {
        LOG_WARN_W(L"[TextureLoader] Invalid ACE dimensions %ux%u in: %ls", width, height, filePath.c_str());
        UnmapViewOfFile(pRaw);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    bool hasMipMaps = (options & 0x01) != 0;
    bool isRawData = (options & 0x10) != 0;
    int mipCount = 1 + (hasMipMaps ? (int)(std::log2((std::max)(width, height))) : 0);
    if (mipCount < 1) mipCount = 1;

    // ACE Header is 148 bytes after SIMISA prefix:
    // (6 uint32s header fields (24) + 64 bytes creator + 60 bytes comment = 148 bytes)
    size_t curOffset = 148;
    std::vector<AceChannelDesc> channels;
    bool hasAlpha = false;
    bool hasMask = false;

    for (uint32_t c = 0; c < channelCount && curOffset + 16 <= dataLen; ++c)
    {
        uint32_t cFlags = *(const uint32_t*)(pData + curOffset);
        uint32_t cSize = *(const uint32_t*)(pData + curOffset + 4);
        uint32_t cRes = *(const uint32_t*)(pData + curOffset + 8);
        uint32_t cType = *(const uint32_t*)(pData + curOffset + 12);
        curOffset += 16;

        if (cType == 6) hasAlpha = true;
        if (cType == 2) hasMask = true;
        channels.push_back({ (int)cSize, (int)cType });
    }

    outData.width = width;
    outData.height = height;
    outData.hasAlpha = hasAlpha || hasMask;
    outData.hasSmoothAlpha = false;
    outData.mips.clear();

    if (isRawData)
    {
        DXGI_FORMAT dxgiFormat = DXGI_FORMAT_BC1_UNORM;
        uint32_t blockSize = 8;

        switch (surfaceFormat)
        {
        case 0x12:
            dxgiFormat = DXGI_FORMAT_BC1_UNORM;
            blockSize = 8;
            // DXT1 / BC1 supports 1-bit alpha mask when channelCount is 4 or mask/alpha channel is present
            outData.hasAlpha = (hasAlpha || hasMask || channelCount >= 4);
            outData.hasSmoothAlpha = false;
            break;
        case 0x14:
            dxgiFormat = DXGI_FORMAT_BC2_UNORM;
            blockSize = 16;
            outData.hasAlpha = true;
            outData.hasSmoothAlpha = false;
            break;
        case 0x16:
            dxgiFormat = DXGI_FORMAT_BC3_UNORM;
            blockSize = 16;
            outData.hasAlpha = true;
            outData.hasSmoothAlpha = true;
            break;
        default:
            dxgiFormat = DXGI_FORMAT_BC1_UNORM;
            blockSize = 8;
            outData.hasAlpha = (hasAlpha || hasMask || channelCount >= 4);
            outData.hasSmoothAlpha = false;
            break;
        }

        outData.format = dxgiFormat;

        // Read mip offset table
        std::vector<uint32_t> mipOffsets;
        if (curOffset + 4 <= dataLen)
        {
            uint32_t firstVal = *(const uint32_t*)(pData + curOffset);
            if (firstVal == 0) curOffset += 4;
        }

        for (int m = 0; m < mipCount && curOffset + 4 <= dataLen; ++m)
        {
            uint32_t off = *(const uint32_t*)(pData + curOffset);
            mipOffsets.push_back(off);
            curOffset += 4;
        }

        for (int m = 0; m < (int)mipOffsets.size(); ++m)
        {
            uint32_t off = mipOffsets[m];
            if (off + 4 > dataLen) break;
            uint32_t chunkLen = *(const uint32_t*)(pData + off);
            if (off + 4 + chunkLen > dataLen) break;

            uint32_t mw = (std::max)(1u, width >> m);
            uint32_t mh = (std::max)(1u, height >> m);

            // DXT block compression operates on 4x4 blocks minimum
            if (mw < 4 || mh < 4) break;

            uint32_t blocksW = (mw + 3) / 4;
            uint32_t blocksH = (mh + 3) / 4;

            DecodedTextureData::MipData mip = {};
            mip.sysMemPitch = blocksW * blockSize;
            mip.sysMemSlicePitch = blocksW * blocksH * blockSize;
            mip.data.assign(pData + off + 4, pData + off + 4 + chunkLen);
            outData.mips.push_back(std::move(mip));
        }
    }
    else
    {
        // High-Speed Direct Planar Channel Unpacking with Row Offset Table
        outData.format = DXGI_FORMAT_R8G8B8A8_UNORM;

        if (curOffset + 4 <= dataLen)
        {
            uint32_t firstVal = *(const uint32_t*)(pData + curOffset);
            if (firstVal == 0) curOffset += 4;
        }

        const uint32_t* pOffsetsTable = (const uint32_t*)(pData + curOffset);

        uint32_t mw = width;
        uint32_t mh = height;
        std::vector<uint8_t> rgbaPixels(mw * mh * 4);
        uint32_t* pPixels32 = (uint32_t*)rgbaPixels.data();

        bool decodeOk = true;
        for (uint32_t y = 0; y < mh && decodeOk; ++y)
        {
            if (curOffset + (y + 1) * 4 > dataLen) { decodeOk = false; break; }
            size_t rowOff = pOffsetsTable[y];
            if (rowOff >= dataLen) { decodeOk = false; break; }

            const uint8_t* pRowData = pData + rowOff;
            size_t rowCur = 0;

            const uint8_t* pR = nullptr;
            const uint8_t* pG = nullptr;
            const uint8_t* pB = nullptr;
            const uint8_t* pA = nullptr;
            const uint8_t* pMask = nullptr;

            for (const auto& ch : channels)
            {
                if (ch.size == 8)
                {
                    if (rowOff + rowCur + mw > dataLen) { decodeOk = false; break; }
                    if (ch.type == 3) pR = pRowData + rowCur;
                    else if (ch.type == 4) pG = pRowData + rowCur;
                    else if (ch.type == 5) pB = pRowData + rowCur;
                    else if (ch.type == 6) pA = pRowData + rowCur;
                    rowCur += mw;
                }
                else if (ch.size == 1)
                {
                    size_t bytesCount = (mw + 7) / 8;
                    if (rowOff + rowCur + bytesCount > dataLen) { decodeOk = false; break; }
                    if (ch.type == 2 || ch.type == 6) pMask = pRowData + rowCur;
                    rowCur += bytesCount;
                }
            }

            if (!decodeOk) break;

            uint32_t* pRowOut = pPixels32 + y * mw;

            if (pR && pG && pB)
            {
                if (pA)
                {
                    for (uint32_t x = 0; x < mw; ++x)
                    {
                        pRowOut[x] = (uint32_t)pR[x] | ((uint32_t)pG[x] << 8) | ((uint32_t)pB[x] << 16) | ((uint32_t)pA[x] << 24);
                    }
                }
                else if (pMask)
                {
                    for (uint32_t x = 0; x < mw; ++x)
                    {
                        uint32_t a = ((pMask[x >> 3] >> (7 - (x & 7))) & 1) ? 0xFF000000 : 0x00000000;
                        pRowOut[x] = (uint32_t)pR[x] | ((uint32_t)pG[x] << 8) | ((uint32_t)pB[x] << 16) | a;
                    }
                }
                else
                {
                    for (uint32_t x = 0; x < mw; ++x)
                    {
                        pRowOut[x] = (uint32_t)pR[x] | ((uint32_t)pG[x] << 8) | ((uint32_t)pB[x] << 16) | 0xFF000000;
                    }
                }
            }
            else
            {
                // Fallback for monochrome or atypical channels
                const uint8_t* pLum = pR ? pR : (pG ? pG : pB);
                for (uint32_t x = 0; x < mw; ++x)
                {
                    uint8_t lum = pLum ? pLum[x] : 255;
                    uint8_t a = pA ? pA[x] : (pMask ? (((pMask[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0) : 255);
                    pRowOut[x] = (uint32_t)lum | ((uint32_t)lum << 8) | ((uint32_t)lum << 16) | ((uint32_t)a << 24);
                }
            }
        }

        if (decodeOk)
        {
            // Analyze true alpha content across decoded pixels
            bool hasRealAlpha = false;
            bool hasSmoothAlpha = false;
            const uint32_t* pP = (const uint32_t*)rgbaPixels.data();
            size_t totalPixels = mw * mh;
            for (size_t i = 0; i < totalPixels; ++i)
            {
                uint8_t a = (uint8_t)(pP[i] >> 24);
                if (a < 250)
                {
                    hasRealAlpha = true;
                    if (a >= 20 && a <= 235)
                    {
                        hasSmoothAlpha = true;
                    }
                }
            }
            outData.hasAlpha = hasRealAlpha;
            outData.hasSmoothAlpha = hasSmoothAlpha;

            DecodedTextureData::MipData mip = {};
            mip.sysMemPitch = mw * 4;
            mip.sysMemSlicePitch = mw * mh * 4;
            mip.data = std::move(rgbaPixels);
            outData.mips.push_back(std::move(mip));
        }
    }

    UnmapViewOfFile(pRaw);
    CloseHandle(hMap);
    CloseHandle(hFile);

    outData.mipLevels = (uint32_t)outData.mips.size();
    outData.isValid = !outData.mips.empty();
    return outData.isValid;
}

static bool DecodeDdsToMemory(const std::wstring& filePath, DecodedTextureData& outData)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < sizeof(uint32_t) + sizeof(DDS_HEADER))
    {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap)
    {
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pData = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pData)
    {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    uint32_t magic = *(const uint32_t*)pData;
    if (magic != DDS_MAGIC)
    {
        UnmapViewOfFile(pData);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    const DDS_HEADER* pHeader = (const DDS_HEADER*)(pData + sizeof(uint32_t));
    if (pHeader->dwSize != sizeof(DDS_HEADER))
    {
        UnmapViewOfFile(pData);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    uint32_t width = pHeader->dwWidth;
    uint32_t height = pHeader->dwHeight;
    uint32_t mipCount = (pHeader->dwMipMapCount > 0) ? pHeader->dwMipMapCount : 1;

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t blockSize = 16;
    bool isCompressed = true;
    bool is24Bit = false;
    bool is16Bit = false;
    bool hasAlpha = false;
    bool hasSmoothAlpha = false;

    if (pHeader->ddspf.dwFlags & 0x00000004)
    {
        switch (pHeader->ddspf.dwFourCC)
        {
        case MAKEFOURCC('D', 'X', 'T', '1'):
            dxgiFormat = DXGI_FORMAT_BC1_UNORM;
            blockSize = 8;
            isCompressed = true;
            hasAlpha = false;
            hasSmoothAlpha = false;
            break;
        case MAKEFOURCC('D', 'X', 'T', '3'):
            dxgiFormat = DXGI_FORMAT_BC2_UNORM;
            blockSize = 16;
            isCompressed = true;
            hasAlpha = true;
            hasSmoothAlpha = false;
            break;
        case MAKEFOURCC('D', 'X', 'T', '5'):
            dxgiFormat = DXGI_FORMAT_BC3_UNORM;
            blockSize = 16;
            isCompressed = true;
            hasAlpha = true;
            hasSmoothAlpha = true;
            break;
        default:
            dxgiFormat = DXGI_FORMAT_BC1_UNORM;
            blockSize = 8;
            hasAlpha = false;
            hasSmoothAlpha = false;
            break;
        }
    }
    else if (pHeader->ddspf.dwRGBBitCount == 32)
    {
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        blockSize = 4;
        isCompressed = false;
        hasAlpha = (pHeader->ddspf.dwABitMask != 0);
        hasSmoothAlpha = hasAlpha;
    }
    else if (pHeader->ddspf.dwRGBBitCount == 24)
    {
        // Uncompressed 24-bit RGB/BGR texture (e.g. SLPUtk1.dds) -> Expand to 32-bit BGRA
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        blockSize = 4;
        isCompressed = false;
        is24Bit = true;
        hasAlpha = false;
        hasSmoothAlpha = false;
    }
    else if (pHeader->ddspf.dwRGBBitCount == 16)
    {
        // Uncompressed 16-bit texture -> Expand to 32-bit BGRA
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        blockSize = 4;
        isCompressed = false;
        is16Bit = true;
        hasAlpha = (pHeader->ddspf.dwABitMask != 0);
        hasSmoothAlpha = hasAlpha;
    }
    else
    {
        dxgiFormat = DXGI_FORMAT_BC1_UNORM;
        blockSize = 8;
        isCompressed = true;
        hasAlpha = false;
        hasSmoothAlpha = false;
    }

    outData.width = width;
    outData.height = height;
    outData.format = dxgiFormat;
    outData.hasAlpha = hasAlpha;
    outData.hasSmoothAlpha = hasSmoothAlpha;
    outData.mips.clear();

    const uint8_t* pCurrentMip = pData + sizeof(uint32_t) + sizeof(DDS_HEADER);
    uint32_t curW = width;
    uint32_t curH = height;

    for (uint32_t m = 0; m < mipCount; ++m)
    {
        if (pCurrentMip >= pData + fileSize) break;

        DecodedTextureData::MipData mip = {};
        if (isCompressed)
        {
            uint32_t blocksW = (std::max)(1u, (curW + 3) / 4);
            uint32_t blocksH = (std::max)(1u, (curH + 3) / 4);
            mip.sysMemPitch = blocksW * blockSize;
            mip.sysMemSlicePitch = blocksW * blocksH * blockSize;
            uint32_t mipSize = mip.sysMemSlicePitch;
            if (pCurrentMip + mipSize > pData + fileSize) break;
            mip.data.assign(pCurrentMip, pCurrentMip + mipSize);
            pCurrentMip += mipSize;
        }
        else if (is24Bit)
        {
            uint32_t srcPitch = curW * 3;
            uint32_t srcSlice = srcPitch * curH;
            if (pCurrentMip + srcSlice > pData + fileSize) break;

            mip.sysMemPitch = curW * 4;
            mip.sysMemSlicePitch = curW * curH * 4;
            mip.data.resize(mip.sysMemSlicePitch);

            const uint8_t* pSrc = pCurrentMip;
            uint8_t* pDst = mip.data.data();
            uint32_t totalPixels = curW * curH;
            bool isRgb = (pHeader->ddspf.dwRBitMask == 0x000000FF);

            if (isRgb)
            {
                for (uint32_t px = 0; px < totalPixels; ++px)
                {
                    pDst[0] = pSrc[2]; // B
                    pDst[1] = pSrc[1]; // G
                    pDst[2] = pSrc[0]; // R
                    pDst[3] = 255;     // A
                    pSrc += 3;
                    pDst += 4;
                }
            }
            else // BGR (dwRBitMask == 0x00FF0000)
            {
                for (uint32_t px = 0; px < totalPixels; ++px)
                {
                    pDst[0] = pSrc[0]; // B
                    pDst[1] = pSrc[1]; // G
                    pDst[2] = pSrc[2]; // R
                    pDst[3] = 255;     // A
                    pSrc += 3;
                    pDst += 4;
                }
            }
            pCurrentMip += srcSlice;
        }
        else if (is16Bit)
        {
            uint32_t srcPitch = curW * 2;
            uint32_t srcSlice = srcPitch * curH;
            if (pCurrentMip + srcSlice > pData + fileSize) break;

            mip.sysMemPitch = curW * 4;
            mip.sysMemSlicePitch = curW * curH * 4;
            mip.data.resize(mip.sysMemSlicePitch);

            const uint16_t* pSrc = (const uint16_t*)pCurrentMip;
            uint8_t* pDst = mip.data.data();
            uint32_t totalPixels = curW * curH;

            bool is565 = (pHeader->ddspf.dwGBitMask == 0x07E0);
            if (is565)
            {
                for (uint32_t px = 0; px < totalPixels; ++px)
                {
                    uint16_t c = *pSrc++;
                    pDst[0] = (uint8_t)(((c & 0x001F) * 255) / 31);
                    pDst[1] = (uint8_t)((((c >> 5) & 0x003F) * 255) / 63);
                    pDst[2] = (uint8_t)((((c >> 11) & 0x001F) * 255) / 31);
                    pDst[3] = 255;
                    pDst += 4;
                }
            }
            else
            {
                for (uint32_t px = 0; px < totalPixels; ++px)
                {
                    uint16_t c = *pSrc++;
                    pDst[0] = (uint8_t)(((c & 0x001F) * 255) / 31);
                    pDst[1] = (uint8_t)((((c >> 5) & 0x001F) * 255) / 31);
                    pDst[2] = (uint8_t)((((c >> 10) & 0x001F) * 255) / 31);
                    pDst[3] = (c & 0x8000) ? 255 : 0;
                    pDst += 4;
                }
            }
            pCurrentMip += srcSlice;
        }
        else // 32-bit
        {
            mip.sysMemPitch = curW * 4;
            mip.sysMemSlicePitch = curW * curH * 4;
            uint32_t mipSize = mip.sysMemSlicePitch;
            if (pCurrentMip + mipSize > pData + fileSize) break;

            mip.data.assign(pCurrentMip, pCurrentMip + mipSize);
            if (pHeader->ddspf.dwRBitMask == 0x000000FF)
            {
                uint8_t* p = mip.data.data();
                uint32_t totalPixels = curW * curH;
                for (uint32_t px = 0; px < totalPixels; ++px)
                {
                    std::swap(p[0], p[2]);
                    p += 4;
                }
            }
            pCurrentMip += mipSize;
        }

        outData.mips.push_back(std::move(mip));
        curW = (std::max)(1u, curW / 2);
        curH = (std::max)(1u, curH / 2);
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMap);
    CloseHandle(hFile);

    outData.mipLevels = (uint32_t)outData.mips.size();
    outData.isValid = !outData.mips.empty();
    return outData.isValid;
}

// =========================================================================
// TextureLoader Implementation
// =========================================================================

TextureLoader::TextureLoader(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
    : m_pDevice(pDevice), m_pContext(pContext), m_pDefaultSRV(nullptr), m_pDefaultTexture(nullptr)
{
    CreateDefaultTexture();
}

TextureLoader::~TextureLoader()
{
    ClearCache();
    if (m_pDefaultSRV) { m_pDefaultSRV->Release(); m_pDefaultSRV = nullptr; }
    if (m_pDefaultTexture) { m_pDefaultTexture->Release(); m_pDefaultTexture = nullptr; }
}

void TextureLoader::ClearCache()
{
    for (auto& pair : m_cache)
    {
        if (pair.second.pSRV) pair.second.pSRV->Release();
        if (pair.second.pTexture) pair.second.pTexture->Release();
    }
    m_cache.clear();
    m_lruList.clear();
    m_lruMap.clear();
}

void TextureLoader::TouchLRU(const std::wstring& key)
{
    auto it = m_lruMap.find(key);
    if (it != m_lruMap.end())
    {
        m_lruList.erase(it->second);
    }
    m_lruList.push_back(key);
    m_lruMap[key] = std::prev(m_lruList.end());
}

void TextureLoader::InsertCache(const std::wstring& key, const LoadedTexture& tex)
{
    if (m_cache.size() >= MAX_CACHE_SIZE)
    {
        EvictOldest();
    }
    m_cache[key] = tex;
    TouchLRU(key);
}

void TextureLoader::EvictOldest()
{
    if (m_lruList.empty()) return;

    std::wstring oldestKey = m_lruList.front();
    m_lruList.pop_front();
    m_lruMap.erase(oldestKey);

    auto it = m_cache.find(oldestKey);
    if (it != m_cache.end())
    {
        if (it->second.pSRV) it->second.pSRV->Release();
        if (it->second.pTexture) it->second.pTexture->Release();
        m_cache.erase(it);
    }
}

ID3D11ShaderResourceView* TextureLoader::GetDefaultTexture()
{
    return m_pDefaultSRV;
}

void TextureLoader::CreateDefaultTexture()
{
    if (!m_pDevice || m_pDefaultSRV) return;

    // 4x4 solid neutral gray texture (RGBA 180, 180, 180, 255)
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) pixels[i] = 0xFFB4B4B4;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 4;
    desc.Height = 4;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels;
    initData.SysMemPitch = 4 * sizeof(uint32_t);

    if (SUCCEEDED(m_pDevice->CreateTexture2D(&desc, &initData, &m_pDefaultTexture)))
    {
        m_pDevice->CreateShaderResourceView(m_pDefaultTexture, NULL, &m_pDefaultSRV);
    }
}

std::wstring TextureLoader::ResolveTexturePath(const std::wstring& shapeDir, const std::wstring& rawImageName)
{
    if (shapeDir.empty() || rawImageName.empty()) return L"";

    // 1. Clean up and normalize slashes
    std::wstring cleanRel = rawImageName;
    for (wchar_t& c : cleanRel)
    {
        if (c == L'/') c = L'\\';
    }

    // Strip wrapping quotes
    while (!cleanRel.empty() && (cleanRel.front() == L'"' || cleanRel.front() == L'\''))
    {
        cleanRel.erase(cleanRel.begin());
    }
    while (!cleanRel.empty() && (cleanRel.back() == L'"' || cleanRel.back() == L'\''))
    {
        cleanRel.pop_back();
    }

    // Strip leading duplicate slashes
    while (cleanRel.length() >= 2 && cleanRel[0] == L'\\' && cleanRel[1] == L'\\')
    {
        cleanRel.erase(0, 1);
    }
    if (!cleanRel.empty() && cleanRel[0] == L'\\' && (cleanRel.length() < 2 || cleanRel[1] != L':'))
    {
        cleanRel.erase(0, 1);
    }

    // Helper to test if file exists (.ace or .dds fallback) with arbitrary length paths
    auto TestPath = [](const std::wstring& candidate) -> std::wstring {
        if (candidate.empty()) return L"";
        DWORD dwAttr = GetFileAttributesW(candidate.c_str());
        if (dwAttr != INVALID_FILE_ATTRIBUTES && !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return candidate;
        }
        // Try DDS fallback if candidate was ACE (arbitrary path length safe)
        size_t dotPos = candidate.find_last_of(L'.');
        if (dotPos != std::wstring::npos)
        {
            std::wstring ddsCandidate = candidate.substr(0, dotPos) + L".dds";
            DWORD dwAttrDds = GetFileAttributesW(ddsCandidate.c_str());
            if (dwAttrDds != INVALID_FILE_ATTRIBUTES && !(dwAttrDds & FILE_ATTRIBUTE_DIRECTORY))
            {
                return ddsCandidate;
            }
        }
        return L"";
    };

    // 2. Candidate 1: Direct in shapeDir
    std::wstring directPath = shapeDir + (shapeDir.empty() || shapeDir.back() == L'\\' ? L"" : L"\\") + cleanRel;
    std::wstring match = TestPath(directPath);
    if (!match.empty())
    {
        return match;
    }

    // 3. Candidate 2: In shapeDir TEXTURES subfolder
    std::wstring texturesSub = shapeDir + (shapeDir.empty() || shapeDir.back() == L'\\' ? L"" : L"\\") + L"TEXTURES\\" + cleanRel;
    match = TestPath(texturesSub);
    if (!match.empty())
    {
        return match;
    }

    // 4. Candidate 3: In parent TRAINSET COMMON.TEXTURES folder
    std::wstring commonTex1 = shapeDir + (shapeDir.empty() || shapeDir.back() == L'\\' ? L"" : L"\\") + L"..\\COMMON.TEXTURES\\" + cleanRel;
    match = TestPath(commonTex1);
    if (!match.empty())
    {
        return match;
    }

    // 5. Candidate 4: In 2-level parent COMMON.TEXTURES folder
    std::wstring commonTex2 = shapeDir + (shapeDir.empty() || shapeDir.back() == L'\\' ? L"" : L"\\") + L"..\\..\\COMMON.TEXTURES\\" + cleanRel;
    match = TestPath(commonTex2);
    if (!match.empty())
    {
        return match;
    }

    LOG_TEXTURE_WARN_W(L"[MISSING_TEXTURE] Could not resolve texture '%ls' in shapeDir '%ls'", rawImageName.c_str(), shapeDir.c_str());
    return L"";
}

static std::unordered_map<std::wstring, std::shared_ptr<DecodedTextureData>> s_cpuDecodedCache;
static std::mutex s_cpuCacheMutex;

void TextureLoader::ClearGlobalCPUCache()
{
    std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
    s_cpuDecodedCache.clear();
}

void TextureLoader::PredecodeTexturesCPU(const std::wstring& shapeDir, const std::vector<std::wstring>& imageNames, const std::atomic<bool>* pCancelToken)
{
    if (shapeDir.empty() || imageNames.empty()) return;
    if (pCancelToken && *pCancelToken) return;

    std::vector<std::pair<std::wstring, std::wstring>> toLoad; // {resolvedPath, key}

    for (const auto& rawName : imageNames)
    {
        if (pCancelToken && *pCancelToken) return;
        std::wstring resolved = ResolveTexturePath(shapeDir, rawName);
        if (resolved.empty()) continue;

        std::wstring key = resolved;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        {
            std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
            if (s_cpuDecodedCache.find(key) != s_cpuDecodedCache.end())
            {
                continue; // Already decoded in CPU memory cache
            }
        }

        toLoad.push_back({ resolved, key });
    }

    if (toLoad.empty() || (pCancelToken && *pCancelToken)) return;

    // Decode in parallel across worker tasks
    std::vector<std::future<std::shared_ptr<DecodedTextureData>>> futures;
    futures.reserve(toLoad.size());

    for (const auto& item : toLoad)
    {
        if (pCancelToken && *pCancelToken) return;
        futures.push_back(std::async(std::launch::async, [item, pCancelToken]() -> std::shared_ptr<DecodedTextureData> {
            if (pCancelToken && *pCancelToken) return nullptr;
            auto pDec = std::make_shared<DecodedTextureData>();
            pDec->resolvedPath = item.first;
            pDec->key = item.second;

            const wchar_t* pExt = PathFindExtensionW(pDec->resolvedPath.c_str());
            if (pExt && _wcsicmp(pExt, L".dds") == 0)
            {
                DecodeDdsToMemory(pDec->resolvedPath, *pDec);
            }
            else
            {
                DecodeAceToMemory(pDec->resolvedPath, *pDec);
            }
            return pDec;
        }));
    }

    for (auto& fut : futures)
    {
        auto pDec = fut.get();
        if (pCancelToken && *pCancelToken) return;
        if (pDec && pDec->isValid && !pDec->mips.empty())
        {
            LOG_TEXTURE_W(L"[Predecode CPU] Texture Decoded: '%ls' -> %ux%u, Mips=%u, Format=0x%x (Alpha: %ls, SmoothAlpha: %ls)",
                pDec->resolvedPath.c_str(), pDec->width, pDec->height, pDec->mipLevels, (uint32_t)pDec->format,
                pDec->hasAlpha ? L"YES" : L"NO", pDec->hasSmoothAlpha ? L"YES" : L"NO");
            std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
            s_cpuDecodedCache[pDec->key] = pDec;
        }
        else if (pDec && !pDec->resolvedPath.empty())
        {
            LOG_TEXTURE_WARN_W(L"[Predecode CPU] Failed to decode texture: '%ls'", pDec->resolvedPath.c_str());
        }
    }
}

void TextureLoader::PreloadTextures(const std::wstring& shapeDir, const std::vector<std::wstring>& imageNames)
{
    if (!m_pDevice || imageNames.empty()) return;

    std::vector<std::pair<std::wstring, std::wstring>> toLoad; // {resolvedPath, key}
    std::unordered_set<std::wstring> seen;

    for (const auto& rawName : imageNames)
    {
        std::wstring resolved = ResolveTexturePath(shapeDir, rawName);
        if (resolved.empty()) continue;

        std::wstring key = resolved;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (m_cache.find(key) != m_cache.end())
        {
            TouchLRU(key); // Already cached, mark as recently used
            continue;
        }

        if (seen.find(key) == seen.end())
        {
            seen.insert(key);
            toLoad.push_back({ resolved, key });
        }
    }

    if (toLoad.empty()) return;

    // 1. First check CPU decoded memory cache
    std::vector<std::shared_ptr<DecodedTextureData>> readyDecoded;
    readyDecoded.reserve(toLoad.size());
    std::vector<std::pair<std::wstring, std::wstring>> needDecode;

    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        for (const auto& item : toLoad)
        {
            auto it = s_cpuDecodedCache.find(item.second);
            if (it != s_cpuDecodedCache.end() && it->second && it->second->isValid && !it->second->mips.empty())
            {
                readyDecoded.push_back(it->second); // Zero allocation pointer copy!
            }
            else
            {
                needDecode.push_back(item);
            }
        }
    }

    // 2. Decode any missing textures in parallel worker threads
    if (!needDecode.empty())
    {
        std::vector<std::future<std::shared_ptr<DecodedTextureData>>> futures;
        futures.reserve(needDecode.size());

        for (const auto& item : needDecode)
        {
            futures.push_back(std::async(std::launch::async, [item]() -> std::shared_ptr<DecodedTextureData> {
                auto pDec = std::make_shared<DecodedTextureData>();
                pDec->resolvedPath = item.first;
                pDec->key = item.second;

                const wchar_t* pExt = PathFindExtensionW(pDec->resolvedPath.c_str());
                if (pExt && _wcsicmp(pExt, L".dds") == 0)
                {
                    DecodeDdsToMemory(pDec->resolvedPath, *pDec);
                }
                else
                {
                    DecodeAceToMemory(pDec->resolvedPath, *pDec);
                }
                return pDec;
            }));
        }

        for (auto& fut : futures)
        {
            auto pDec = fut.get();
            if (pDec && pDec->isValid && !pDec->mips.empty())
            {
                LOG_TEXTURE_W(L"[Preload Worker] Texture Decoded: '%ls' -> %ux%u, Mips=%u, Format=0x%x (Alpha: %ls, SmoothAlpha: %ls)",
                    pDec->resolvedPath.c_str(), pDec->width, pDec->height, pDec->mipLevels, (uint32_t)pDec->format,
                    pDec->hasAlpha ? L"YES" : L"NO", pDec->hasSmoothAlpha ? L"YES" : L"NO");
                readyDecoded.push_back(pDec);
                std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
                s_cpuDecodedCache[pDec->key] = pDec;
            }
            else if (pDec && !pDec->resolvedPath.empty())
            {
                LOG_TEXTURE_WARN_W(L"[Preload Worker] Failed to decode texture: '%ls'", pDec->resolvedPath.c_str());
            }
        }
    }

    // 3. Main thread: Instant D3D11 resource creation from memory buffers (< 0.01 ms)
    for (const auto& pDec : readyDecoded)
    {
        if (!pDec || !pDec->isValid || pDec->mips.empty()) continue;

        if (m_cache.find(pDec->key) != m_cache.end())
        {
            TouchLRU(pDec->key);
            continue;
        }

        std::vector<D3D11_SUBRESOURCE_DATA> subData(pDec->mips.size());
        for (size_t i = 0; i < pDec->mips.size(); ++i)
        {
            subData[i].pSysMem = pDec->mips[i].data.data();
            subData[i].SysMemPitch = pDec->mips[i].sysMemPitch;
            subData[i].SysMemSlicePitch = pDec->mips[i].sysMemSlicePitch;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = pDec->width;
        desc.Height = pDec->height;
        desc.MipLevels = pDec->mipLevels;
        desc.ArraySize = 1;
        desc.Format = pDec->format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        LoadedTexture loaded = {};
        if (SUCCEEDED(m_pDevice->CreateTexture2D(&desc, subData.data(), &loaded.pTexture)))
        {
            if (SUCCEEDED(m_pDevice->CreateShaderResourceView(loaded.pTexture, NULL, &loaded.pSRV)))
            {
                loaded.width = pDec->width;
                loaded.height = pDec->height;
                loaded.hasAlpha = pDec->hasAlpha;
                loaded.hasSmoothAlpha = pDec->hasSmoothAlpha;
                InsertCache(pDec->key, loaded);
                LOG_TEXTURE_W(L"[GPU Upload] Texture Uploaded to D3D11: '%ls' -> %ux%u, Mips=%u, Format=0x%x (Alpha: %ls, SmoothAlpha: %ls)",
                    pDec->resolvedPath.c_str(), loaded.width, loaded.height, desc.MipLevels, (uint32_t)desc.Format,
                    loaded.hasAlpha ? L"YES" : L"NO", loaded.hasSmoothAlpha ? L"YES" : L"NO");
            }
            else
            {
                loaded.pTexture->Release();
                loaded.pTexture = nullptr;
                LOG_TEXTURE_ERROR_W(L"[GPU Upload] CreateShaderResourceView failed for: '%ls'", pDec->resolvedPath.c_str());
            }
        }
        else
        {
            LOG_TEXTURE_ERROR_W(L"[GPU Upload] CreateTexture2D failed for: '%ls'", pDec->resolvedPath.c_str());
        }
    }
}

ID3D11ShaderResourceView* TextureLoader::LoadTexture(const std::wstring& shapeDir, const std::wstring& rawImageName)
{
    if (!m_pDevice) return m_pDefaultSRV;

    std::wstring resolvedPath = ResolveTexturePath(shapeDir, rawImageName);
    if (resolvedPath.empty())
    {
        return m_pDefaultSRV;
    }

    // Check GPU cache
    std::wstring key = resolvedPath;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        TouchLRU(key);
        return it->second.pSRV ? it->second.pSRV : m_pDefaultSRV;
    }

    // Check CPU decoded memory cache
    std::shared_ptr<DecodedTextureData> pDec;
    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        auto cit = s_cpuDecodedCache.find(key);
        if (cit != s_cpuDecodedCache.end() && cit->second && cit->second->isValid && !cit->second->mips.empty())
        {
            pDec = cit->second;
        }
    }

    if (!pDec)
    {
        pDec = std::make_shared<DecodedTextureData>();
        pDec->resolvedPath = resolvedPath;
        pDec->key = key;
        const wchar_t* pExt = PathFindExtensionW(resolvedPath.c_str());
        bool haveDec = false;
        if (pExt && _wcsicmp(pExt, L".dds") == 0)
        {
            haveDec = DecodeDdsToMemory(resolvedPath, *pDec);
        }
        else
        {
            haveDec = DecodeAceToMemory(resolvedPath, *pDec);
        }
        if (haveDec && !pDec->mips.empty())
        {
            std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
            s_cpuDecodedCache[key] = pDec;
        }
        else
        {
            pDec = nullptr;
        }
    }

    if (pDec && !pDec->mips.empty())
    {
        std::vector<D3D11_SUBRESOURCE_DATA> subData(pDec->mips.size());
        for (size_t i = 0; i < pDec->mips.size(); ++i)
        {
            subData[i].pSysMem = pDec->mips[i].data.data();
            subData[i].SysMemPitch = pDec->mips[i].sysMemPitch;
            subData[i].SysMemSlicePitch = pDec->mips[i].sysMemSlicePitch;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = pDec->width;
        desc.Height = pDec->height;
        desc.MipLevels = pDec->mipLevels;
        desc.ArraySize = 1;
        desc.Format = pDec->format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        LoadedTexture loaded = {};
        if (SUCCEEDED(m_pDevice->CreateTexture2D(&desc, subData.data(), &loaded.pTexture)))
        {
            if (SUCCEEDED(m_pDevice->CreateShaderResourceView(loaded.pTexture, NULL, &loaded.pSRV)))
            {
                loaded.width = pDec->width;
                loaded.height = pDec->height;
                loaded.hasAlpha = pDec->hasAlpha;
                loaded.hasSmoothAlpha = pDec->hasSmoothAlpha;
                InsertCache(key, loaded);
                LOG_TEXTURE_W(L"GPU Texture Uploaded: '%ls' -> %ux%u, Mips=%u, Format=0x%x (Alpha: %ls, Smooth: %ls)",
                    resolvedPath.c_str(), loaded.width, loaded.height, desc.MipLevels, (uint32_t)desc.Format,
                    loaded.hasAlpha ? L"YES" : L"NO", loaded.hasSmoothAlpha ? L"YES" : L"NO");
                return loaded.pSRV;
            }
            loaded.pTexture->Release();
            loaded.pTexture = nullptr;
        }
    }

    LOG_TEXTURE_WARN_W(L"Decode Failed for '%ls' -> using Default Fallback Texture", resolvedPath.c_str());
    return m_pDefaultSRV;
}

bool TextureLoader::HasAlpha(const std::wstring& shapeDir, const std::wstring& rawImageName)
{
    if (shapeDir.empty() || rawImageName.empty()) return false;
    std::wstring resolvedPath = ResolveTexturePath(shapeDir, rawImageName);
    if (resolvedPath.empty()) return false;

    std::wstring key = resolvedPath;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        return it->second.hasAlpha;
    }

    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        auto cit = s_cpuDecodedCache.find(key);
        if (cit != s_cpuDecodedCache.end() && cit->second && cit->second->isValid)
        {
            return cit->second->hasAlpha;
        }
    }

    // Load into cache if not yet cached
    LoadTexture(shapeDir, rawImageName);
    it = m_cache.find(key);
    if (it != m_cache.end())
    {
        return it->second.hasAlpha;
    }
    return false;
}

bool TextureLoader::HasSmoothAlpha(const std::wstring& shapeDir, const std::wstring& rawImageName)
{
    if (shapeDir.empty() || rawImageName.empty()) return false;
    std::wstring resolvedPath = ResolveTexturePath(shapeDir, rawImageName);
    if (resolvedPath.empty()) return false;

    std::wstring key = resolvedPath;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        return it->second.hasSmoothAlpha;
    }

    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        auto cit = s_cpuDecodedCache.find(key);
        if (cit != s_cpuDecodedCache.end() && cit->second && cit->second->isValid)
        {
            return cit->second->hasSmoothAlpha;
        }
    }

    // Load into cache if not yet cached
    LoadTexture(shapeDir, rawImageName);
    it = m_cache.find(key);
    if (it != m_cache.end())
    {
        return it->second.hasSmoothAlpha;
    }
    return false;
}

bool TextureLoader::LoadAceTexture(const std::wstring& filePath, LoadedTexture& outTex)
{
    std::shared_ptr<DecodedTextureData> pDec;
    std::wstring key = filePath;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        auto cit = s_cpuDecodedCache.find(key);
        if (cit != s_cpuDecodedCache.end() && cit->second && cit->second->isValid && !cit->second->mips.empty())
        {
            pDec = cit->second;
        }
    }

    if (!pDec)
    {
        pDec = std::make_shared<DecodedTextureData>();
        if (!DecodeAceToMemory(filePath, *pDec) || pDec->mips.empty()) return false;
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        s_cpuDecodedCache[key] = pDec;
    }

    std::vector<D3D11_SUBRESOURCE_DATA> subData(pDec->mips.size());
    for (size_t i = 0; i < pDec->mips.size(); ++i)
    {
        subData[i].pSysMem = pDec->mips[i].data.data();
        subData[i].SysMemPitch = pDec->mips[i].sysMemPitch;
        subData[i].SysMemSlicePitch = pDec->mips[i].sysMemSlicePitch;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = pDec->width;
    desc.Height = pDec->height;
    desc.MipLevels = pDec->mipLevels;
    desc.ArraySize = 1;
    desc.Format = pDec->format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_pDevice->CreateTexture2D(&desc, subData.data(), &outTex.pTexture);
    if (SUCCEEDED(hr))
    {
        hr = m_pDevice->CreateShaderResourceView(outTex.pTexture, NULL, &outTex.pSRV);
        if (SUCCEEDED(hr))
        {
            outTex.width = pDec->width;
            outTex.height = pDec->height;
            outTex.hasAlpha = pDec->hasAlpha;
            outTex.hasSmoothAlpha = pDec->hasSmoothAlpha;
            return true;
        }
        outTex.pTexture->Release();
        outTex.pTexture = nullptr;
    }

    return false;
}

bool TextureLoader::LoadDdsTexture(const std::wstring& filePath, LoadedTexture& outTex)
{
    std::shared_ptr<DecodedTextureData> pDec;
    std::wstring key = filePath;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    {
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        auto cit = s_cpuDecodedCache.find(key);
        if (cit != s_cpuDecodedCache.end() && cit->second && cit->second->isValid && !cit->second->mips.empty())
        {
            pDec = cit->second;
        }
    }

    if (!pDec)
    {
        pDec = std::make_shared<DecodedTextureData>();
        if (!DecodeDdsToMemory(filePath, *pDec) || pDec->mips.empty()) return false;
        std::lock_guard<std::mutex> lock(s_cpuCacheMutex);
        s_cpuDecodedCache[key] = pDec;
    }

    std::vector<D3D11_SUBRESOURCE_DATA> subData(pDec->mips.size());
    for (size_t i = 0; i < pDec->mips.size(); ++i)
    {
        subData[i].pSysMem = pDec->mips[i].data.data();
        subData[i].SysMemPitch = pDec->mips[i].sysMemPitch;
        subData[i].SysMemSlicePitch = pDec->mips[i].sysMemSlicePitch;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = pDec->width;
    desc.Height = pDec->height;
    desc.MipLevels = pDec->mipLevels;
    desc.ArraySize = 1;
    desc.Format = pDec->format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_pDevice->CreateTexture2D(&desc, subData.data(), &outTex.pTexture);
    if (SUCCEEDED(hr))
    {
        hr = m_pDevice->CreateShaderResourceView(outTex.pTexture, NULL, &outTex.pSRV);
        if (SUCCEEDED(hr))
        {
            outTex.width = pDec->width;
            outTex.height = pDec->height;
            outTex.hasAlpha = pDec->hasAlpha;
            outTex.hasSmoothAlpha = pDec->hasSmoothAlpha;
            return true;
        }
        outTex.pTexture->Release();
        outTex.pTexture = nullptr;
    }

    return false;
}
