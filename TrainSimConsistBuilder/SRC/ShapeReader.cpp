#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ShapeReader.h"
#include <iostream>
#include <shlwapi.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "d3d11.lib")

using namespace DirectX;

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

    struct FastBuffer {
        uint8_t* data;
        size_t size;
        size_t capacity;

        FastBuffer(size_t initCap) {
            capacity = (initCap > 65536) ? initCap : 65536;
            data = (uint8_t*)std::malloc(capacity);
            size = 0;
        }
        ~FastBuffer() {
            if (data) std::free(data);
        }

        inline void EnsureCapacity(size_t additional) {
            if (size + additional > capacity) {
                size_t newCap = capacity * 2 + additional + 65536;
                uint8_t* newData = (uint8_t*)std::realloc(data, newCap);
                if (newData) {
                    data = newData;
                    capacity = newCap;
                }
            }
        }

        inline void AppendByte(uint8_t b) {
            if (size >= capacity) EnsureCapacity(1);
            data[size++] = b;
        }

        inline void AppendMatch(size_t dist, size_t len) {
            EnsureCapacity(len);
            size_t start = size - dist;
            uint8_t* pDst = data + size;
            const uint8_t* pSrc = data + start;
            if (dist >= len) {
                std::memcpy(pDst, pSrc, len);
            } else {
                for (size_t i = 0; i < len; ++i) {
                    pDst[i] = pSrc[i];
                }
            }
            size += len;
        }
    };

    static bool InflateBlock(BitStream& bs, const FastHuffTable& lt, const FastHuffTable& dt, FastBuffer& out) {
        while (true) {
            int sym = DecodeFastSymbol(bs, lt);
            if (sym < 0 || sym > 285) return false;
            if (sym == 256) break;

            if (sym < 256) {
                out.AppendByte((uint8_t)sym);
            } else {
                sym -= 257;
                int len = kLenBases[sym] + (int)bs.Get(kLenExtra[sym]);
                int distSym = DecodeFastSymbol(bs, dt);
                if (distSym < 0 || distSym >= 30) return false;
                int dist = kDistBases[distSym] + (int)bs.Get(kDistExtra[distSym]);

                if (dist <= 0 || dist > (int)out.size) return false;
                out.AppendMatch((size_t)dist, (size_t)len);
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
        FastBuffer buf(srcLen * 4);

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

                buf.EnsureCapacity(len);
                for (uint16_t i = 0; i < len; ++i) {
                    if (bs.bitsInBuf >= 8) {
                        buf.AppendByte((uint8_t)bs.Get(8));
                    } else if (bs.bytePos < bs.srcLen) {
                        buf.AppendByte(bs.src[bs.bytePos++]);
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

                if (!InflateBlock(bs, lt, dt, buf)) return false;
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

                if (!InflateBlock(bs, lt, dt, buf)) return false;
            } else {
                return false;
            }
        }

        out.resize(buf.size);
        if (buf.size > 0) {
            std::memcpy(out.data(), buf.data, buf.size);
        }
        return true;
    }
}

// =========================================================================
// High-Speed Direct Pointer ASCII Tokenizer (Zero Dynamic Allocations)
// =========================================================================
class FastScanner
{
public:
    const char* p;
    const char* end;

    FastScanner(const char* start, const char* endPtr) : p(start), end(endPtr) {}

    inline void SkipWhitespace() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                p++;
            } else if (c == '#' || c == ';') {
                // Skip line comment
                while (p < end && *p != '\n' && *p != '\r') p++;
            } else {
                break;
            }
        }
    }

    inline bool IsEOF() const {
        return p >= end;
    }

    inline bool Match(char ch) {
        SkipWhitespace();
        if (p < end && *p == ch) {
            p++;
            return true;
        }
        return false;
    }

    inline bool MatchKeyword(const char* kw) {
        SkipWhitespace();
        const char* cur = p;
        while (*kw && cur < end) {
            char c1 = *kw++;
            char c2 = *cur++;
            if (tolower((unsigned char)c1) != tolower((unsigned char)c2)) return false;
        }
        if (*kw != '\0') return false;
        // Verify token boundary
        if (cur < end) {
            char next = *cur;
            if (isalnum((unsigned char)next) || next == '_') return false;
        }
        p = cur;
        return true;
    }

    inline bool ParseInt(int32_t& outVal) {
        SkipWhitespace();
        if (p >= end) return false;

        const char* cur = p;
        bool neg = false;
        if (*cur == '-') { neg = true; cur++; }
        else if (*cur == '+') { cur++; }

        if (cur >= end) return false;

        // Check for hex 0x / 0X
        if (cur + 2 <= end && cur[0] == '0' && (cur[1] == 'x' || cur[1] == 'X')) {
            cur += 2;
            uint32_t hexVal = 0;
            int count = 0;
            while (cur < end) {
                unsigned char c = (unsigned char)*cur;
                if (c >= '0' && c <= '9') hexVal = (hexVal << 4) | (c - '0');
                else if (c >= 'a' && c <= 'f') hexVal = (hexVal << 4) | (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') hexVal = (hexVal << 4) | (c - 'A' + 10);
                else break;
                cur++;
                count++;
            }
            if (count == 0) return false;
            outVal = neg ? -(int32_t)hexVal : (int32_t)hexVal;
            p = cur;
            return true;
        }

        if ((unsigned char)(*cur - '0') >= 10) return false;
        uint32_t val = 0;
        while (cur < end && (unsigned char)(*cur - '0') < 10) {
            val = val * 10 + (*cur - '0');
            cur++;
        }
        outVal = neg ? -(int32_t)val : (int32_t)val;
        p = cur;
        return true;
    }

    inline bool ParseUIntHexOrDec(uint32_t& outVal) {
        SkipWhitespace();
        if (p >= end) return false;

        const char* cur = p;
        if (cur + 2 <= end && cur[0] == '0' && (cur[1] == 'x' || cur[1] == 'X')) {
            cur += 2;
        }

        uint32_t hexVal = 0;
        int count = 0;
        while (cur < end) {
            unsigned char c = (unsigned char)*cur;
            if (c >= '0' && c <= '9') hexVal = (hexVal << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f') hexVal = (hexVal << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') hexVal = (hexVal << 4) | (c - 'A' + 10);
            else break;
            cur++;
            count++;
        }
        if (count == 0) return false;
        outVal = hexVal;
        p = cur;
        return true;
    }

    inline bool ParseFloat(float& outVal) {
        SkipWhitespace();
        if (p >= end) return false;

        const char* cur = p;
        bool neg = false;
        if (*cur == '-') { neg = true; cur++; }
        else if (*cur == '+') { cur++; }

        if (cur >= end) return false;

        uint64_t intPart = 0;
        int digits = 0;
        while (cur < end && (unsigned char)(*cur - '0') < 10) {
            intPart = intPart * 10 + (*cur - '0');
            cur++;
            digits++;
        }

        uint64_t fracPart = 0;
        int fracDigits = 0;
        if (cur < end && *cur == '.') {
            cur++;
            while (cur < end && (unsigned char)(*cur - '0') < 10) {
                if (fracDigits < 17) {
                    fracPart = fracPart * 10 + (*cur - '0');
                    fracDigits++;
                }
                cur++;
                digits++;
            }
        }

        if (digits == 0) return false;

        static const double kPow10[18] = {
            1.0, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9, 1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17
        };

        double dVal = (double)intPart + (double)fracPart * kPow10[fracDigits];

        // Handle scientific exponent 'e' / 'E' if present (e.g. 1.5e-4)
        if (cur < end && (*cur == 'e' || *cur == 'E')) {
            cur++;
            bool expNeg = false;
            if (cur < end && *cur == '-') { expNeg = true; cur++; }
            else if (cur < end && *cur == '+') { cur++; }
            int expVal = 0;
            while (cur < end && (unsigned char)(*cur - '0') < 10) {
                expVal = expVal * 10 + (*cur - '0');
                cur++;
            }
            if (expVal > 0) {
                if (expVal <= 17) {
                    if (expNeg) dVal *= kPow10[expVal];
                    else dVal *= (1.0 / kPow10[expVal]);
                } else {
                    if (expNeg) dVal /= std::pow(10.0, expVal);
                    else dVal *= std::pow(10.0, expVal);
                }
            }
        }

        outVal = (float)(neg ? -dVal : dVal);
        p = cur;
        return true;
    }

    inline bool ParseString(std::string& outStr) {
        SkipWhitespace();
        if (p >= end) return false;
        if (*p == '"') {
            p++; // Skip opening quote
            const char* start = p;
            while (p < end && *p != '"' && *p != '\r' && *p != '\n') p++;
            outStr.assign(start, p - start);
            if (p < end && *p == '"') p++;
            return true;
        } else {
            const char* start = p;
            while (p < end && !isspace((unsigned char)*p) && *p != ')' && *p != '(' && *p != '"') p++;
            if (p == start) return false;
            outStr.assign(start, p - start);
            return true;
        }
    }

    inline void SkipBlock() {
        SkipWhitespace();
        if (p >= end) return;
        if (*p == '(') {
            p++;
            int depth = 1;
            while (p < end && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                else if (*p == '"') {
                    p++;
                    while (p < end && *p != '"' && *p != '\r' && *p != '\n') p++;
                }
                if (p < end) p++;
            }
        } else {
            // Advance past current token
            while (p < end && !isspace((unsigned char)*p) && *p != '(' && *p != ')') p++;
        }
    }

    inline void SkipUnknownElement() {
        SkipWhitespace();
        if (p >= end) return;
        if (*p == ')') return; // Don't consume closing block parenthesis
        if (*p == '(') {
            SkipBlock();
            return;
        }
        // Advance past token identifier
        while (p < end && !isspace((unsigned char)*p) && *p != '(' && *p != ')' && *p != '"') {
            p++;
        }
        SkipWhitespace();
        if (p < end && *p == '(') {
            SkipBlock();
        }
    }
};

// =========================================================================
// Temporary Storage Structs during Parsing
// =========================================================================
struct MstsTexture {
    int32_t imageIdx = -1;
    int32_t filterIdx = 0;
};

struct MstsVtxState {
    uint32_t flags = 0;
    int32_t  matrixIdx = 0;
    int32_t  lightMatIdx = 0;
};

struct MstsPrimState {
    uint32_t flags = 0;
    int32_t  shaderIdx = 0;
    int32_t  texIdx = -1;
    int32_t  vtxStateIdx = 0;
};

struct MstsLocalVertex {
    int32_t pointIdx = 0;
    int32_t normalIdx = 0;
    int32_t uvIdx = 0;
    int32_t vtxStateIdx = 0;
};

// =========================================================================
// Convert UTF-16 Buffer to Flat ASCII Buffer in Ultra-Fast 64-bit Pass
// =========================================================================
static void FastConvertUtf16ToAscii(const wchar_t* src, size_t charCount, std::vector<char>& dst) {
    dst.resize(charCount + 1);
    char* d = dst.data();
    size_t i = 0;
    while (i + 4 <= charCount) {
        uint64_t w4;
        std::memcpy(&w4, src + i, sizeof(w4));
        uint32_t c0 = (uint32_t)(w4 & 0x7F);
        uint32_t c1 = (uint32_t)((w4 >> 16) & 0x7F);
        uint32_t c2 = (uint32_t)((w4 >> 32) & 0x7F);
        uint32_t c3 = (uint32_t)((w4 >> 48) & 0x7F);
        uint32_t packed = c0 | (c1 << 8) | (c2 << 16) | (c3 << 24);
        std::memcpy(d + i, &packed, sizeof(packed));
        i += 4;
    }
    while (i < charCount) {
        wchar_t wc = src[i];
        d[i] = (wc < 128) ? (char)wc : ' ';
        i++;
    }
    d[charCount] = '\0';
}

// =========================================================================
// MSTS Binary Token Enumeration & Binary Block Parsing Infrastructure
// =========================================================================
enum TokenID : uint16_t {
    tok_error = 0,
    tok_comment = 1,
    tok_point = 2,
    tok_vector = 3,
    tok_quat = 4,
    tok_normals = 5,
    tok_normal_idxs = 6,
    tok_points = 7,
    tok_uv_point = 8,
    tok_uv_points = 9,
    tok_colour = 10,
    tok_colours = 11,
    tok_packed_colour = 12,
    tok_image = 13,
    tok_images = 14,
    tok_texture = 15,
    tok_textures = 16,
    tok_light_material = 17,
    tok_light_materials = 18,
    tok_linear_key = 19,
    tok_tcb_key = 20,
    tok_linear_pos = 21,
    tok_tcb_pos = 22,
    tok_slerp_rot = 23,
    tok_tcb_rot = 24,
    tok_controllers = 25,
    tok_anim_node = 26,
    tok_anim_nodes = 27,
    tok_animation = 28,
    tok_animations = 29,
    tok_anim = 30,
    tok_lod_controls = 31,
    tok_lod_control = 32,
    tok_distance_levels_header = 33,
    tok_distance_level_header = 34,
    tok_dlevel_selection = 35,
    tok_distance_levels = 36,
    tok_distance_level = 37,
    tok_sub_objects = 38,
    tok_sub_object = 39,
    tok_sub_object_header = 40,
    tok_geometry_info = 41,
    tok_geometry_nodes = 42,
    tok_geometry_node = 43,
    tok_geometry_node_map = 44,
    tok_cullable_prims = 45,
    tok_vtx_state = 46,
    tok_vtx_states = 47,
    tok_vertex = 48,
    tok_vertex_uvs = 49,
    tok_vertices = 50,
    tok_vertex_set = 51,
    tok_vertex_sets = 52,
    tok_primitives = 53,
    tok_prim_state = 54,
    tok_prim_states = 55,
    tok_prim_state_idx = 56,
    tok_indexed_point_list = 57,
    tok_point_list = 58,
    tok_indexed_line_list = 59,
    tok_indexed_trilist = 60,
    tok_tex_idxs = 61,
    tok_tri = 62,
    tok_vertex_idxs = 63,
    tok_flags = 64,
    tok_matrix = 65,
    tok_matrices = 66,
    tok_hierarchy = 67,
    tok_volumes = 68,
    tok_vol_sphere = 69,
    tok_shape_header = 70,
    tok_shape = 71,
    tok_shader_names = 72,
    tok_shader_name = 73,
    tok_texture_filter_names = 74,
    tok_texture_filter_name = 75,
    tok_sort_vectors = 76,
    tok_uvop_arg_sets = 77,
    tok_uvop_arg_set = 78,
    tok_light_model_cfgs = 79,
    tok_light_model_cfg = 80,
    tok_uv_ops = 81,
    tok_uvop_copy = 82,
    tok_uv_op_share = 83,
    tok_uv_op_copy = 84,
    tok_uv_op_uniformscale = 85,
    tok_uv_op_user_uninformscale = 86,
    tok_uv_op_nonuniformscale = 87,
    tok_uv_op_user_nonuninformscale = 88,
    tok_uv_op_transform = 89,
    tok_uv_op_user_transform = 90,
    tok_uv_op_reflectxy = 91,
    tok_uv_op_reflectmap = 92,
    tok_uv_op_reflectmapfull = 93,
    tok_uv_op_spheremap = 94,
    tok_uv_op_spheremapfull = 95,
    tok_uv_op_specularmap = 96,
    tok_uv_op_embossbump = 97,
    tok_user_uv_args = 98,
    tok_io_dev = 99,
    tok_io_map = 100,
    tok_sguid = 101,
    tok_dlev_cfg_table = 102,
    tok_dlev_cfg = 103,
    tok_subobject_shaders = 104,
    tok_subobject_light_cfgs = 105,
    tok_shape_named_data = 106,
    tok_shape_named_data_header = 107,
    tok_shape_named_geometry = 108,
    tok_shape_geom_ref = 109,
    tok_material_palette = 110,
    tok_blend_config = 111,
    tok_blend_config_header = 112,
    tok_filtermode_cfgs = 113,
    tok_filter_mode_cfg = 114,
    tok_blend_mode_cfgs = 115,
    tok_blend_mode_cfg = 116,
    tok_texture_stage_progs = 117,
    tok_texture_stage_prog = 118,
    tok_blend_mode_cfg_refs = 119,
    tok_shader_cfgs = 120,
    tok_shader_cfg = 121,
    tok_texture_slots = 122,
    tok_texture_slot = 123,
    tok_named_filter_modes = 124,
    tok_named_filter_mode = 125,
    tok_filtermode_cfg_refs = 126,
    tok_filtermode_cfg_ref = 127,
    tok_named_shaders = 128,
    tok_named_shader = 129,
    tok_shader_cfg_refs = 130,
    tok_shader_cfg_ref = 131
};

struct BinaryStreamReader {
    const uint8_t* data;
    size_t size;
    size_t pos;

    inline bool HasBytes(size_t n) const { return pos + n <= size; }

    inline uint8_t ReadU8() {
        if (pos < size) return data[pos++];
        return 0;
    }
    inline uint16_t ReadU16() {
        if (pos + 2 <= size) {
            uint16_t val = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);
            pos += 2;
            return val;
        }
        return 0;
    }
    inline int32_t ReadI32() {
        if (pos + 4 <= size) {
            int32_t val;
            std::memcpy(&val, data + pos, 4);
            pos += 4;
            return val;
        }
        return 0;
    }
    inline uint32_t ReadU32() {
        if (pos + 4 <= size) {
            uint32_t val;
            std::memcpy(&val, data + pos, 4);
            pos += 4;
            return val;
        }
        return 0;
    }
    inline float ReadFloat() {
        if (pos + 4 <= size) {
            float val;
            std::memcpy(&val, data + pos, 4);
            pos += 4;
            return val;
        }
        return 0.0f;
    }
    inline std::wstring ReadString() {
        uint16_t count = ReadU16();
        if (count > 0 && pos + count * 2 <= size) {
            std::wstring s((const wchar_t*)(data + pos), count);
            pos += count * 2;
            return s;
        }
        return L"";
    }
};

struct BinaryShapeBlock {
    TokenID id;
    uint16_t flags;
    uint32_t length;
    size_t endPos;
    std::wstring label;

    static BinaryShapeBlock ReadSubBlock(BinaryStreamReader& r) {
        BinaryShapeBlock b = {};
        if (!r.HasBytes(8)) return b;

        b.id = (TokenID)r.ReadU16();
        b.flags = r.ReadU16();
        b.length = r.ReadU32();
        b.endPos = r.pos + b.length;

        // Label (1 byte character length, followed by length*2 bytes UTF-16LE)
        if (r.HasBytes(1)) {
            uint8_t labelLen = r.ReadU8();
            if (labelLen > 0 && r.HasBytes(labelLen * 2)) {
                b.label = std::wstring((const wchar_t*)(r.data + r.pos), labelLen);
                r.pos += labelLen * 2;
            }
        }
        return b;
    }

    inline void Skip(BinaryStreamReader& r) const {
        if (r.pos < endPos && endPos <= r.size) {
            r.pos = endPos;
        }
    }

    inline bool EndOfBlock(const BinaryStreamReader& r) const {
        return r.pos >= endPos;
    }
};

static void FinalizeShapeAnimation(ParsedShape& outShape) {
    size_t numBones = outShape.boneMatrices.size();
    if (numBones == 0) return;

    std::vector<AnimNodeTrack> rawParsedTracks = std::move(outShape.animation.animNodes);
    std::vector<AnimNodeTrack> fullTracks(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        fullTracks[i].nodeIndex = (int32_t)i;
        fullTracks[i].name = (i < outShape.boneNames.size()) ? outShape.boneNames[i] : ("Node_" + std::to_string(i));
        fullTracks[i].type = ShapeReader::CategorizeNodeName(fullTracks[i].name);

        XMMATRIX localMat = XMLoadFloat4x4(&outShape.boneMatrices[i]);
        XMVECTOR s, r, t;
        if (XMMatrixDecompose(&s, &r, &t, localMat)) {
            XMStoreFloat3(&fullTracks[i].bindScale, s);
            XMStoreFloat4(&fullTracks[i].bindRotQuat, r);
            XMStoreFloat3(&fullTracks[i].bindPos, t);
        } else {
            fullTracks[i].bindScale = { 1.0f, 1.0f, 1.0f };
            fullTracks[i].bindRotQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
            fullTracks[i].bindPos = { outShape.boneMatrices[i]._41, outShape.boneMatrices[i]._42, outShape.boneMatrices[i]._43 };
        }
    }

    float maxKeyframeFound = 0.0f;

    for (const auto& track : rawParsedTracks) {
        int targetIdx = -1;

        // Tier 1: Exact Case-Insensitive String Match
        for (size_t j = 0; j < outShape.boneNames.size(); ++j) {
            if (_stricmp(track.name.c_str(), outShape.boneNames[j].c_str()) == 0) {
                targetIdx = (int32_t)j;
                break;
            }
        }

        // Tier 2: Normalized Alphanumeric Match
        if (targetIdx < 0) {
            std::string normTrack = ShapeReader::NormalizeNodeName(track.name);
            if (!normTrack.empty()) {
                for (size_t j = 0; j < outShape.boneNames.size(); ++j) {
                    if (ShapeReader::NormalizeNodeName(outShape.boneNames[j]) == normTrack) {
                        targetIdx = (int32_t)j;
                        break;
                    }
                }
            }
        }

        // Tier 3: Index Fallback
        if (targetIdx < 0 && track.nodeIndex >= 0 && (size_t)track.nodeIndex < numBones) {
            targetIdx = track.nodeIndex;
        }

        if (targetIdx >= 0 && (size_t)targetIdx < numBones) {
            fullTracks[targetIdx].posKeys = track.posKeys;
            fullTracks[targetIdx].rotKeys = track.rotKeys;
            fullTracks[targetIdx].hasAnim = (!track.posKeys.empty() || !track.rotKeys.empty());
            if (!track.name.empty()) fullTracks[targetIdx].name = track.name;
            fullTracks[targetIdx].type = ShapeReader::CategorizeNodeName(fullTracks[targetIdx].name);

            for (const auto& pk : track.posKeys) {
                if (pk.frame > maxKeyframeFound) maxKeyframeFound = pk.frame;
            }
            for (const auto& rk : track.rotKeys) {
                if (rk.frame > maxKeyframeFound) maxKeyframeFound = rk.frame;
            }
        }
    }

    outShape.animation.animNodes = std::move(fullTracks);
    outShape.animation.actualMaxKeyframe = maxKeyframeFound;

    if (maxKeyframeFound > outShape.animation.frameCount || outShape.animation.frameCount <= 0.0f) {
        outShape.animation.frameCount = maxKeyframeFound;
    }
    if (maxKeyframeFound > 0.0f) {
        outShape.animation.hasAnimation = true;
    }

    outShape.animation.countPanto = 0;
    outShape.animation.countDoor = 0;
    outShape.animation.countWiper = 0;
    outShape.animation.countWheel = 0;
    outShape.animation.countFan = 0;
    outShape.animation.countDriver = 0;
    outShape.animation.countDisplay = 0;
    outShape.animation.countCustom = 0;

    for (const auto& track : outShape.animation.animNodes) {
        if (!track.hasAnim) continue;
        switch (track.type) {
        case AnimNodeType::Pantograph:     outShape.animation.countPanto++; break;
        case AnimNodeType::DoorOrMirror:   outShape.animation.countDoor++; break;
        case AnimNodeType::Wiper:          outShape.animation.countWiper++; break;
        case AnimNodeType::WheelOrBogie:   outShape.animation.countWheel++; break;
        case AnimNodeType::FanOrBlower:    outShape.animation.countFan++; break;
        case AnimNodeType::DriverOrCrew:   outShape.animation.countDriver++; break;
        case AnimNodeType::DisplayOrBoard: outShape.animation.countDisplay++; break;
        default:                           outShape.animation.countCustom++; break;
        }
    }

    // Identify Procedural Wheel Nodes
    outShape.animation.simulatedWheels.clear();
    for (size_t i = 0; i < numBones; ++i) {
        std::string rawName = (i < outShape.boneNames.size()) ? outShape.boneNames[i] : "";
        std::string upperName = rawName;
        for (char& c : upperName) c = (char)std::toupper((unsigned char)c);

        if (upperName.find("WHEEL") != std::string::npos ||
            upperName.find("AXLE") != std::string::npos ||
            upperName.rfind("RAD_", 0) == 0)
        {
            outShape.animation.simulatedWheels.push_back({ (int32_t)i, rawName });
        }
    }
    outShape.animation.hasSimulatedWheels = !outShape.animation.simulatedWheels.empty();
}

static bool ParseBinaryShape(const uint8_t* pData, size_t dataLen, ParsedShape& outShape) {
    if (!pData || dataLen < 24) return false;

    // Locate tok_shape (0x0047) header within the first 64 bytes
    size_t subHeaderOffset = 16;
    for (size_t off = 0; off + 8 <= dataLen && off <= 64; ++off) {
        if (pData[off] == 0x47 && pData[off + 1] == 0x00) {
            subHeaderOffset = off;
            break;
        }
    }

    BinaryStreamReader r = { pData, dataLen, subHeaderOffset };
    BinaryShapeBlock root = BinaryShapeBlock::ReadSubBlock(r);
    if (root.id != tok_shape) return false;

    std::vector<XMFLOAT3> points;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> uvs;
    std::vector<XMMATRIX> localMatrices;
    std::vector<XMMATRIX> worldMatrices;
    std::vector<int32_t> hierarchy;
    std::vector<std::string> shaderNames;
    std::vector<MstsTexture> textures;
    std::vector<MstsVtxState> vtxStates;
    std::vector<MstsPrimState> primStates;

    while (!root.EndOfBlock(r)) {
        BinaryShapeBlock sub = BinaryShapeBlock::ReadSubBlock(r);
        if (sub.id == tok_shader_names) {
            int count = r.ReadI32();
            shaderNames.reserve(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock sn = BinaryShapeBlock::ReadSubBlock(r);
                std::wstring ws = !sn.label.empty() ? sn.label : r.ReadString();
                std::string s;
                for (wchar_t wc : ws) s.push_back((char)(wc < 128 ? wc : '?'));
                shaderNames.push_back(s);
                sn.Skip(r);
            }
        }
        else if (sub.id == tok_points) {
            int count = r.ReadI32();
            points.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock pb = BinaryShapeBlock::ReadSubBlock(r);
                points[i].x = r.ReadFloat();
                points[i].y = r.ReadFloat();
                points[i].z = r.ReadFloat();
                pb.Skip(r);
            }
        }
        else if (sub.id == tok_uv_points) {
            int count = r.ReadI32();
            uvs.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock uvb = BinaryShapeBlock::ReadSubBlock(r);
                uvs[i].x = r.ReadFloat();
                uvs[i].y = r.ReadFloat();
                uvb.Skip(r);
            }
        }
        else if (sub.id == tok_normals) {
            int count = r.ReadI32();
            normals.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock nb = BinaryShapeBlock::ReadSubBlock(r);
                normals[i].x = r.ReadFloat();
                normals[i].y = r.ReadFloat();
                normals[i].z = r.ReadFloat();
                nb.Skip(r);
            }
        }
        else if (sub.id == tok_matrices) {
            int count = r.ReadI32();
            localMatrices.resize(count, XMMatrixIdentity());
            outShape.boneNames.resize(count);
            outShape.boneMatrices.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock mb = BinaryShapeBlock::ReadSubBlock(r);
                std::string bn;
                for (wchar_t wc : mb.label) bn.push_back((char)(wc < 128 ? wc : '?'));
                outShape.boneNames[i] = bn;
                float ax = r.ReadFloat(), ay = r.ReadFloat(), az = r.ReadFloat();
                float bx = r.ReadFloat(), by = r.ReadFloat(), bz = r.ReadFloat();
                float cx = r.ReadFloat(), cy = r.ReadFloat(), cz = r.ReadFloat();
                float dx = r.ReadFloat(), dy = r.ReadFloat(), dz = r.ReadFloat();
                localMatrices[i] = XMMATRIX(
                    ax, ay, az, 0.0f,
                    bx, by, bz, 0.0f,
                    cx, cy, cz, 0.0f,
                    dx, dy, dz, 1.0f
                );
                XMStoreFloat4x4(&outShape.boneMatrices[i], localMatrices[i]);
                mb.Skip(r);
            }
        }
        else if (sub.id == tok_images) {
            int count = r.ReadI32();
            outShape.rawImageNames.reserve(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock ib = BinaryShapeBlock::ReadSubBlock(r);
                outShape.rawImageNames.push_back(r.ReadString());
                ib.Skip(r);
            }
        }
        else if (sub.id == tok_textures) {
            int count = r.ReadI32();
            textures.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock tb = BinaryShapeBlock::ReadSubBlock(r);
                textures[i].imageIdx = r.ReadI32();
                textures[i].filterIdx = r.ReadI32();
                tb.Skip(r);
            }
        }
        else if (sub.id == tok_vtx_states) {
            int count = r.ReadI32();
            vtxStates.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock vsb = BinaryShapeBlock::ReadSubBlock(r);
                vtxStates[i].flags = r.ReadU32();
                vtxStates[i].matrixIdx = r.ReadI32();
                vtxStates[i].lightMatIdx = r.ReadI32();
                vsb.Skip(r);
            }
        }
        else if (sub.id == tok_prim_states) {
            int count = r.ReadI32();
            primStates.resize(count);
            for (int i = 0; i < count; ++i) {
                BinaryShapeBlock psb = BinaryShapeBlock::ReadSubBlock(r);
                primStates[i].flags = r.ReadU32();
                primStates[i].shaderIdx = r.ReadI32();
                BinaryShapeBlock tib = BinaryShapeBlock::ReadSubBlock(r);
                int tCount = r.ReadI32();
                if (tCount > 0) primStates[i].texIdx = r.ReadI32();
                tib.Skip(r);
                float zbias = r.ReadFloat();
                primStates[i].vtxStateIdx = r.ReadI32();
                psb.Skip(r);
            }
        }
        else if (sub.id == tok_lod_controls) {
            int lcCount = r.ReadI32();
            for (int lc = 0; lc < lcCount; ++lc) {
                BinaryShapeBlock lcb = BinaryShapeBlock::ReadSubBlock(r);
                while (!lcb.EndOfBlock(r)) {
                    BinaryShapeBlock lodSub = BinaryShapeBlock::ReadSubBlock(r);
                    if (lodSub.id == tok_distance_levels_header) {
                        lodSub.Skip(r);
                    }
                    else if (lodSub.id == tok_distance_levels) {
                        int dlCount = r.ReadI32();
                        for (int dl = 0; dl < dlCount; ++dl) {
                            BinaryShapeBlock dlb = BinaryShapeBlock::ReadSubBlock(r);
                            if (dl == 0) {
                                // Highest detail level (LOD 0)
                                while (!dlb.EndOfBlock(r)) {
                                    BinaryShapeBlock dlbSub = BinaryShapeBlock::ReadSubBlock(r);
                                    if (dlbSub.id == tok_distance_level_header) {
                                        while (!dlbSub.EndOfBlock(r)) {
                                            BinaryShapeBlock dlhbSub = BinaryShapeBlock::ReadSubBlock(r);
                                            if (dlhbSub.id == tok_hierarchy) {
                                                int hCount = r.ReadI32();
                                                hierarchy.resize(hCount);
                                                for (int h = 0; h < hCount; ++h) hierarchy[h] = r.ReadI32();
                                            }
                                            dlhbSub.Skip(r);
                                        }
                                    }
                                    else if (dlbSub.id == tok_sub_objects) {
                                        outShape.boneHierarchy = hierarchy;

                                        // Compute hierarchical world matrices
                                        worldMatrices.resize(localMatrices.size(), XMMatrixIdentity());
                                        outShape.bindWorldMatrices.resize(localMatrices.size());
                                        for (size_t m = 0; m < localMatrices.size(); ++m) {
                                            int parent = (m < hierarchy.size()) ? hierarchy[m] : -1;
                                            if (parent >= 0 && (size_t)parent < worldMatrices.size()) {
                                                worldMatrices[m] = XMMatrixMultiply(localMatrices[m], worldMatrices[parent]);
                                            } else {
                                                worldMatrices[m] = localMatrices[m];
                                            }
                                            XMStoreFloat4x4(&outShape.bindWorldMatrices[m], worldMatrices[m]);
                                        }

                                        int soCount = r.ReadI32();
                                        for (int so = 0; so < soCount; ++so) {
                                            BinaryShapeBlock sob = BinaryShapeBlock::ReadSubBlock(r);
                                            std::vector<MstsLocalVertex> subVertices;

                                            while (!sob.EndOfBlock(r)) {
                                                BinaryShapeBlock subElem = BinaryShapeBlock::ReadSubBlock(r);
                                                if (subElem.id == tok_sub_object_header) {
                                                    subElem.Skip(r);
                                                }
                                                else if (subElem.id == tok_vertices) {
                                                    int vCount = r.ReadI32();
                                                    subVertices.resize(vCount);
                                                    for (int vi = 0; vi < vCount; ++vi) {
                                                        BinaryShapeBlock vertb = BinaryShapeBlock::ReadSubBlock(r);
                                                        uint32_t vf = r.ReadU32();
                                                        subVertices[vi].pointIdx = r.ReadI32();
                                                        subVertices[vi].normalIdx = r.ReadI32();
                                                        uint32_t c1 = r.ReadU32(), c2 = r.ReadU32();
                                                        BinaryShapeBlock uvb = BinaryShapeBlock::ReadSubBlock(r);
                                                        int uvCount = r.ReadI32();
                                                        if (uvCount > 0) subVertices[vi].uvIdx = r.ReadI32();
                                                        uvb.Skip(r);
                                                        vertb.Skip(r);
                                                    }
                                                    subElem.Skip(r);
                                                }
                                                else if (subElem.id == tok_primitives) {
                                                    int pCount = r.ReadI32();
                                                    int currentPrimStateIdx = 0;
                                                    for (int pi = 0; pi < pCount; ++pi) {
                                                        BinaryShapeBlock elem = BinaryShapeBlock::ReadSubBlock(r);
                                                        if (elem.id == tok_prim_state_idx) {
                                                            currentPrimStateIdx = r.ReadI32();
                                                            elem.Skip(r);
                                                        }
                                                        else if (elem.id == tok_indexed_trilist) {
                                                            BinaryShapeBlock vib = BinaryShapeBlock::ReadSubBlock(r);
                                                            int numIndices = r.ReadI32();

                                                            ShapeSubMesh subMesh;
                                                            subMesh.startIndex = (uint32_t)outShape.indices.size();
                                                            subMesh.indexCount = (uint32_t)numIndices;

                                                            int imgIdx = -1;
                                                            int vtxStateIdx = 0;
                                                            if (currentPrimStateIdx >= 0 && (size_t)currentPrimStateIdx < primStates.size()) {
                                                                const auto& ps = primStates[currentPrimStateIdx];
                                                                vtxStateIdx = ps.vtxStateIdx;
                                                                if (ps.texIdx >= 0 && (size_t)ps.texIdx < textures.size()) {
                                                                    imgIdx = textures[ps.texIdx].imageIdx;
                                                                }
                                                                if (ps.shaderIdx >= 0 && (size_t)ps.shaderIdx < shaderNames.size()) {
                                                                    const std::string& shName = shaderNames[ps.shaderIdx];
                                                                    if (shName.find("Alph") != std::string::npos || shName.find("Alpha") != std::string::npos || shName.find("BlendATex") != std::string::npos) {
                                                                        subMesh.isTransparent = true;
                                                                        subMesh.isAlphaTest = true;  // 1-bit cutout (AlphATex, AlphATexDiff, BlendATex, BlendATexDiff, trainboards, grilles) -> writes depth
                                                                    } else if (shName.find("Trans") != std::string::npos || shName.find("Blend") != std::string::npos || shName.find("Add") != std::string::npos) {
                                                                        subMesh.isTransparent = true;
                                                                        subMesh.isAlphaTest = false; // Smooth alpha blend (TransNorm, TransDiff, BlendNorm, cabin glass, tinted windows) -> read-only depth
                                                                    } else {
                                                                        subMesh.isTransparent = false;
                                                                        subMesh.isAlphaTest = false;
                                                                    }
                                                                }
                                                                if (imgIdx < 0) {
                                                                    subMesh.isTransparent = false;
                                                                    subMesh.isAlphaTest = false;
                                                                }
                                                            }

                                                            subMesh.imageIndex = imgIdx;
                                                            if (imgIdx >= 0 && (size_t)imgIdx < outShape.rawImageNames.size()) {
                                                                subMesh.textureName = outShape.rawImageNames[imgIdx];
                                                            }

                                                            int matrixIdx = 0;
                                                            if (vtxStateIdx >= 0 && (size_t)vtxStateIdx < vtxStates.size()) {
                                                                matrixIdx = vtxStates[vtxStateIdx].matrixIdx;
                                                            }

                                                            XMMATRIX worldMat = (matrixIdx >= 0 && (size_t)matrixIdx < worldMatrices.size())
                                                                ? worldMatrices[matrixIdx]
                                                                : XMMatrixIdentity();

                                                            uint32_t baseVertexOffset = (uint32_t)outShape.vertices.size();

                                                            for (int k = 0; k < numIndices; ++k) {
                                                                int localIdx = r.ReadI32();
                                                                if (localIdx >= 0 && (size_t)localIdx < subVertices.size()) {
                                                                    const auto& sv = subVertices[localIdx];
                                                                    GPUVertex gv = {};
                                                                    gv.boneIndex = (uint32_t)matrixIdx;

                                                                    // Position (Raw local/bind position)
                                                                    XMFLOAT3 rawPos = (sv.pointIdx >= 0 && (size_t)sv.pointIdx < points.size()) ? points[sv.pointIdx] : XMFLOAT3(0, 0, 0);
                                                                    gv.pos = rawPos;

                                                                    // Normal (Raw local/bind normal)
                                                                    XMFLOAT3 rawNorm = (sv.normalIdx >= 0 && (size_t)sv.normalIdx < normals.size()) ? normals[sv.normalIdx] : XMFLOAT3(0, 1, 0);
                                                                    gv.normal = rawNorm;

                                                                    // UV
                                                                    gv.uv = (sv.uvIdx >= 0 && (size_t)sv.uvIdx < uvs.size()) ? uvs[sv.uvIdx] : XMFLOAT2(0, 0);

                                                                    // Compute World-Space coordinate for Bounding Box
                                                                    XMVECTOR vPos = XMVector3Transform(XMLoadFloat3(&rawPos), worldMat);
                                                                    XMFLOAT3 wp;
                                                                    XMStoreFloat3(&wp, vPos);

                                                                    outShape.boundsMin.x = (std::min)(outShape.boundsMin.x, wp.x);
                                                                    outShape.boundsMin.y = (std::min)(outShape.boundsMin.y, wp.y);
                                                                    outShape.boundsMin.z = (std::min)(outShape.boundsMin.z, wp.z);

                                                                    outShape.boundsMax.x = (std::max)(outShape.boundsMax.x, wp.x);
                                                                    outShape.boundsMax.y = (std::max)(outShape.boundsMax.y, wp.y);
                                                                    outShape.boundsMax.z = (std::max)(outShape.boundsMax.z, wp.z);

                                                                    outShape.vertices.push_back(gv);
                                                                    outShape.indices.push_back(baseVertexOffset + k);
                                                                }
                                                            }

                                                            outShape.subMeshes.push_back(subMesh);
                                                            vib.Skip(r);
                                                            elem.Skip(r);
                                                        }
                                                        else {
                                                            elem.Skip(r);
                                                        }
                                                    }
                                                    subElem.Skip(r);
                                                }
                                                else {
                                                    subElem.Skip(r);
                                                }
                                            }
                                            sob.Skip(r);
                                        }
                                    }
                                    dlbSub.Skip(r);
                                }
                            }
                            dlb.Skip(r);
                        }
                    }
                    lodSub.Skip(r);
                }
                lcb.Skip(r);
            }
        }
        else if (sub.id == tok_animations) {
            int numAnims = r.ReadI32();
            for (int a = 0; a < numAnims; ++a) {
                BinaryShapeBlock ab = BinaryShapeBlock::ReadSubBlock(r);
                if (ab.id == tok_animation) {
                    int frameCount = r.ReadI32();
                    int frameRate = r.ReadI32();
                    outShape.animation.frameCount = (float)frameCount;
                    outShape.animation.frameRate = (frameRate > 0 && frameRate < 1000) ? (float)frameRate : 30.0f;
                    outShape.animation.hasAnimation = (frameCount > 0);

                    while (!ab.EndOfBlock(r)) {
                        BinaryShapeBlock asub = BinaryShapeBlock::ReadSubBlock(r);
                        if (asub.id == tok_anim_nodes) {
                            int numNodes = r.ReadI32();
                            for (int i = 0; i < numNodes; ++i) {
                                BinaryShapeBlock nb = BinaryShapeBlock::ReadSubBlock(r);
                                if (nb.id == tok_anim_node) {
                                    AnimNodeTrack track;
                                    for (wchar_t wc : nb.label) track.name.push_back((char)(wc < 128 ? wc : '?'));
                                    track.nodeIndex = i;

                                    while (!nb.EndOfBlock(r)) {
                                        BinaryShapeBlock nsub = BinaryShapeBlock::ReadSubBlock(r);
                                        if (nsub.id == tok_controllers) {
                                            int numControllers = r.ReadI32();
                                            for (int c = 0; c < numControllers; ++c) {
                                                BinaryShapeBlock cb = BinaryShapeBlock::ReadSubBlock(r);
                                                if (cb.id == tok_linear_pos) {
                                                    int numKeys = r.ReadI32();
                                                    for (int k = 0; k < numKeys; ++k) {
                                                        BinaryShapeBlock kb = BinaryShapeBlock::ReadSubBlock(r);
                                                        int f = r.ReadI32();
                                                        float x = r.ReadFloat(), y = r.ReadFloat(), z = r.ReadFloat();
                                                        track.posKeys.push_back({ (float)f, { x, y, z } });
                                                        kb.Skip(r);
                                                    }
                                                }
                                                else if (cb.id == tok_slerp_rot) {
                                                    int numKeys = r.ReadI32();
                                                    for (int k = 0; k < numKeys; ++k) {
                                                        BinaryShapeBlock kb = BinaryShapeBlock::ReadSubBlock(r);
                                                        int f = r.ReadI32();
                                                        float qx = r.ReadFloat(), qy = r.ReadFloat(), qz = r.ReadFloat(), qw = r.ReadFloat();
                                                        track.rotKeys.push_back({ (float)f, { qx, qy, qz, qw } });
                                                        kb.Skip(r);
                                                    }
                                                }
                                                else if (cb.id == tok_tcb_rot) {
                                                    int numKeys = r.ReadI32();
                                                    for (int k = 0; k < numKeys; ++k) {
                                                        BinaryShapeBlock kb = BinaryShapeBlock::ReadSubBlock(r);
                                                        int f = r.ReadI32();
                                                        float qx = r.ReadFloat(), qy = r.ReadFloat(), qz = r.ReadFloat(), qw = r.ReadFloat();
                                                        while (r.pos + 4 <= kb.endPos) r.ReadFloat();
                                                        track.rotKeys.push_back({ (float)f, { qx, qy, qz, qw } });
                                                        kb.Skip(r);
                                                    }
                                                }
                                                cb.Skip(r);
                                            }
                                        }
                                        nsub.Skip(r);
                                    }
                                    outShape.animation.animNodes.push_back(track);
                                }
                                nb.Skip(r);
                            }
                        }
                        asub.Skip(r);
                    }
                }
                ab.Skip(r);
            }
        }
        else {
            sub.Skip(r);
        }
    }

    FinalizeShapeAnimation(outShape);
    return (!outShape.vertices.empty() && !outShape.indices.empty());
}

// =========================================================================
// Main Shape CPU Parsing Implementation (Thread-Safe)
// =========================================================================
bool ShapeReader::ParseShapeFile(
    const std::wstring& shapeFilePath,
    ParsedShape& outShape
) {
    outShape.isValid = false;
    outShape.shapeFilePath = shapeFilePath;

    // Extract Directory
    size_t lastSlash = shapeFilePath.find_last_of(L"\\/");
    outShape.shapeDir = (lastSlash != std::wstring::npos) ? shapeFilePath.substr(0, lastSlash) : L".";

    // 1. Memory-Map the .s file using Kernel32 API
    HANDLE hFile = CreateFileW(
        shapeFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 16) {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pRawBytes = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pRawBytes) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    size_t fileSize = (size_t)liSize.QuadPart;

    // 2. Determine File Format (ASCII text, UTF-16 text, or Compressed Zlib binary)
    bool isUtf16 = (fileSize >= 2 && pRawBytes[0] == 0xFF && pRawBytes[1] == 0xFE);
    bool isCompressed = false;

    if (isUtf16) {
        const wchar_t* pw = (const wchar_t*)(pRawBytes + 2); // Skip BOM
        size_t wlen = (fileSize - 2) / sizeof(wchar_t);
        if (wlen >= 8 && std::wcsncmp(pw, L"SIMISA@F", 8) == 0) {
            isCompressed = true;
        }
    } else {
        if (fileSize >= 8 && std::memcmp(pRawBytes, "SIMISA@F", 8) == 0) {
            isCompressed = true;
        }
    }

    std::vector<uint8_t> decompressedBytes;
    const uint8_t* pPayload = nullptr;
    size_t payloadLen = 0;

    if (isCompressed) {
        size_t zlibStart = (isUtf16 ? 34 : 16);
        for (size_t i = (isUtf16 ? 16 : 8); i < (isUtf16 ? 64 : 32) && i + 1 < fileSize; ++i) {
            if (pRawBytes[i] == 0x78 && (pRawBytes[i + 1] == 0x9C || pRawBytes[i + 1] == 0xDA || pRawBytes[i + 1] == 0x01 || pRawBytes[i + 1] == 0x5E)) {
                zlibStart = i;
                break;
            }
        }

        if (!FastZlib::DecompressZlib(pRawBytes + zlibStart, fileSize - zlibStart, decompressedBytes)) {
            UnmapViewOfFile(pRawBytes);
            CloseHandle(hMap);
            CloseHandle(hFile);
            return false;
        }
        pPayload = decompressedBytes.data();
        payloadLen = decompressedBytes.size();
    } else {
        pPayload = pRawBytes;
        payloadLen = fileSize;
    }

    // 3. Determine whether payload is MSTS Binary Tokenized Shape or ASCII / UTF-16 Text
    bool isBinary = false;
    bool payloadIsUtf16 = (payloadLen >= 2 && pPayload[0] == 0xFF && pPayload[1] == 0xFE);
    if (payloadIsUtf16) {
        const wchar_t* pw = (const wchar_t*)(pPayload + 2);
        size_t wlen = (payloadLen - 2) / sizeof(wchar_t);
        for (size_t i = 0; i + 3 < wlen && i < 32; ++i) {
            if (pw[i] == L'0' && (pw[i + 1] == L's' || pw[i + 1] == L'f' || pw[i + 1] == L'w' || pw[i + 1] == L'a') && (pw[i + 2] == L'1' || pw[i + 2] == L'2')) {
                if (pw[i + 3] == L'b' || pw[i + 3] == L'B') isBinary = true;
                else if (pw[i + 3] == L't' || pw[i + 3] == L'T') isBinary = false;
                break;
            }
        }
    } else {
        const char* pc = (const char*)pPayload;
        for (size_t i = 0; i + 3 < payloadLen && i < 64; ++i) {
            if (pc[i] == '0' && (pc[i + 1] == 's' || pc[i + 1] == 'f' || pc[i + 1] == 'w' || pc[i + 1] == 'a') && (pc[i + 2] == '1' || pc[i + 2] == '2')) {
                if (pc[i + 3] == 'b' || pc[i + 3] == 'B') isBinary = true;
                else if (pc[i + 3] == 't' || pc[i + 3] == 'T') isBinary = false;
                break;
            }
        }
    }

    bool parseSuccess = false;
    if (isBinary) {
        parseSuccess = ParseBinaryShape(pPayload, payloadLen, outShape);
    } else {
        std::vector<char> decompressedAscii;
        const char* textStart = nullptr;
        const char* textEnd = nullptr;

        bool payloadIsUtf16 = (payloadLen >= 2 && pPayload[0] == 0xFF && pPayload[1] == 0xFE);
        if (payloadIsUtf16) {
            const wchar_t* wSrc = (const wchar_t*)(pPayload + 2);
            size_t numChars = (payloadLen - 2) / sizeof(wchar_t);
            FastConvertUtf16ToAscii(wSrc, numChars, decompressedAscii);
            textStart = decompressedAscii.data();
            textEnd = textStart + decompressedAscii.size() - 1;
        } else {
            textStart = (const char*)pPayload;
            textEnd = textStart + payloadLen;
        }

        // Fast Direct Pointer ASCII Parsing
        FastScanner scan(textStart, textEnd);

        std::vector<XMFLOAT3> points;
        std::vector<XMFLOAT3> normals;
        std::vector<XMFLOAT2> uvs;
        std::vector<XMMATRIX> localMatrices;
        std::vector<XMMATRIX> worldMatrices;
        std::vector<int32_t>  hierarchy;
        std::vector<std::string> shaderNames;
        std::vector<MstsTexture> textures;
        std::vector<MstsVtxState> vtxStates;
        std::vector<MstsPrimState> primStates;

        // Scan until "shape ("
        while (!scan.IsEOF()) {
            if (scan.MatchKeyword("shape") || scan.MatchKeyword("shape_file")) {
                if (scan.Match('(')) break;
            }
            scan.p++;
        }

        while (!scan.IsEOF()) {
        scan.SkipWhitespace();
        if (scan.Match(')')) break; // End of shape

        if (scan.MatchKeyword("shader_names")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                shaderNames.clear();
                if (count > 0) shaderNames.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("named_shader")) {
                        if (scan.Match('(')) {
                            std::string sName;
                            scan.ParseString(sName);
                            shaderNames.push_back(sName);
                            scan.Match(')');
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("points")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                points.clear();
                if (count > 0) points.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.p + 5 < scan.end && (scan.p[0] == 'p' || scan.p[0] == 'P') && (scan.p[1] == 'o' || scan.p[1] == 'O') && (scan.p[2] == 'i' || scan.p[2] == 'I') && (scan.p[3] == 'n' || scan.p[3] == 'N') && (scan.p[4] == 't' || scan.p[4] == 'T')) {
                        scan.p += 5;
                        while (scan.p < scan.end && *scan.p != '(') scan.p++;
                        if (scan.p < scan.end && *scan.p == '(') scan.p++;
                        XMFLOAT3 pt(0, 0, 0);
                        scan.ParseFloat(pt.x);
                        scan.ParseFloat(pt.y);
                        scan.ParseFloat(pt.z);
                        while (scan.p < scan.end && *scan.p != ')') scan.p++;
                        if (scan.p < scan.end && *scan.p == ')') scan.p++;
                        points.push_back(pt);
                    } else if (scan.MatchKeyword("point")) {
                        if (scan.Match('(')) {
                            XMFLOAT3 pt(0, 0, 0);
                            scan.ParseFloat(pt.x);
                            scan.ParseFloat(pt.y);
                            scan.ParseFloat(pt.z);
                            scan.Match(')');
                            points.push_back(pt);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("uv_points")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                uvs.clear();
                if (count > 0) uvs.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.p + 8 < scan.end && (scan.p[0] == 'u' || scan.p[0] == 'U') && (scan.p[1] == 'v' || scan.p[1] == 'V') && scan.p[2] == '_' && (scan.p[3] == 'p' || scan.p[3] == 'P') && (scan.p[4] == 'o' || scan.p[4] == 'O') && (scan.p[5] == 'i' || scan.p[5] == 'I') && (scan.p[6] == 'n' || scan.p[6] == 'N') && (scan.p[7] == 't' || scan.p[7] == 'T')) {
                        scan.p += 8;
                        while (scan.p < scan.end && *scan.p != '(') scan.p++;
                        if (scan.p < scan.end && *scan.p == '(') scan.p++;
                        XMFLOAT2 uv(0, 0);
                        scan.ParseFloat(uv.x);
                        scan.ParseFloat(uv.y);
                        while (scan.p < scan.end && *scan.p != ')') scan.p++;
                        if (scan.p < scan.end && *scan.p == ')') scan.p++;
                        uvs.push_back(uv);
                    } else if (scan.MatchKeyword("uv_point")) {
                        if (scan.Match('(')) {
                            XMFLOAT2 uv(0, 0);
                            scan.ParseFloat(uv.x);
                            scan.ParseFloat(uv.y);
                            scan.Match(')');
                            uvs.push_back(uv);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("normals")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                normals.clear();
                if (count > 0) normals.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.p + 6 < scan.end && (scan.p[0] == 'n' || scan.p[0] == 'N') && (scan.p[1] == 'o' || scan.p[1] == 'O') && (scan.p[2] == 'r' || scan.p[2] == 'R') && (scan.p[3] == 'm' || scan.p[3] == 'M') && (scan.p[4] == 'a' || scan.p[4] == 'A') && (scan.p[5] == 'l' || scan.p[5] == 'L')) {
                        scan.p += 6;
                        while (scan.p < scan.end && *scan.p != '(') scan.p++;
                        if (scan.p < scan.end && *scan.p == '(') scan.p++;
                        XMFLOAT3 n(0, 1, 0);
                        scan.ParseFloat(n.x);
                        scan.ParseFloat(n.y);
                        scan.ParseFloat(n.z);
                        while (scan.p < scan.end && *scan.p != ')') scan.p++;
                        if (scan.p < scan.end && *scan.p == ')') scan.p++;
                        normals.push_back(n);
                    } else if (scan.MatchKeyword("normal")) {
                        if (scan.Match('(')) {
                            XMFLOAT3 n(0, 1, 0);
                            scan.ParseFloat(n.x);
                            scan.ParseFloat(n.y);
                            scan.ParseFloat(n.z);
                            scan.Match(')');
                            normals.push_back(n);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("matrices")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                localMatrices.clear();
                outShape.boneNames.clear();
                outShape.boneMatrices.clear();
                if (count > 0) {
                    localMatrices.reserve(count);
                    outShape.boneNames.reserve(count);
                    outShape.boneMatrices.reserve(count);
                }
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("matrix")) {
                        std::string mName;
                        scan.SkipWhitespace();
                        if (scan.p < scan.end && *scan.p == '"') {
                            scan.ParseString(mName);
                        } else {
                            const char* nameStart = scan.p;
                            while (scan.p < scan.end && *scan.p != '(' && *scan.p != '\r' && *scan.p != '\n') {
                                scan.p++;
                            }
                            const char* nameEnd = scan.p;
                            while (nameEnd > nameStart && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) {
                                nameEnd--;
                            }
                            mName.assign(nameStart, nameEnd);
                        }
                        outShape.boneNames.push_back(mName);

                        if (scan.Match('(')) {
                            float m00 = 1, m01 = 0, m02 = 0, m10 = 0, m11 = 1, m12 = 0, m20 = 0, m21 = 0, m22 = 1, tx = 0, ty = 0, tz = 0;
                            scan.ParseFloat(m00); scan.ParseFloat(m01); scan.ParseFloat(m02);
                            scan.ParseFloat(m10); scan.ParseFloat(m11); scan.ParseFloat(m12);
                            scan.ParseFloat(m20); scan.ParseFloat(m21); scan.ParseFloat(m22);
                            scan.ParseFloat(tx);  scan.ParseFloat(ty);  scan.ParseFloat(tz);

                            // Left-Handed Row-Major Matrix for DirectX LookAtLH
                            XMMATRIX mat = XMMATRIX(
                                m00, m01, m02, 0.0f,
                                m10, m11, m12, 0.0f,
                                m20, m21, m22, 0.0f,
                                tx,  ty,  tz,  1.0f
                            );
                            localMatrices.push_back(mat);
                            XMFLOAT4X4 f4;
                            XMStoreFloat4x4(&f4, mat);
                            outShape.boneMatrices.push_back(f4);
                            scan.Match(')');
                        } else {
                            localMatrices.push_back(XMMatrixIdentity());
                            XMFLOAT4X4 f4;
                            XMStoreFloat4x4(&f4, XMMatrixIdentity());
                            outShape.boneMatrices.push_back(f4);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("images")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                outShape.rawImageNames.clear();
                if (count > 0) outShape.rawImageNames.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("image")) {
                        if (scan.Match('(')) {
                            std::string imgStr;
                            scan.ParseString(imgStr);
                            int wlen = MultiByteToWideChar(CP_UTF8, 0, imgStr.c_str(), -1, NULL, 0);
                            std::wstring wImg(wlen ? wlen - 1 : 0, 0);
                            if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, imgStr.c_str(), -1, &wImg[0], wlen);
                            outShape.rawImageNames.push_back(wImg);
                            scan.Match(')');
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("textures")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                textures.clear();
                if (count > 0) textures.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("texture")) {
                        if (scan.Match('(')) {
                            MstsTexture tex;
                            scan.ParseInt(tex.imageIdx);
                            scan.ParseInt(tex.filterIdx);
                            // Skip remaining texture params (mipmap bias, flags, etc.)
                            while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                            scan.Match(')');
                            textures.push_back(tex);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("vtx_states")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                vtxStates.clear();
                if (count > 0) vtxStates.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("vtx_state")) {
                        if (scan.Match('(')) {
                            MstsVtxState vs;
                            scan.ParseUIntHexOrDec(vs.flags);
                            scan.ParseInt(vs.matrixIdx);
                            scan.ParseInt(vs.lightMatIdx);
                            while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                            scan.Match(')');
                            vtxStates.push_back(vs);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("prim_states")) {
            if (scan.Match('(')) {
                int count = 0; scan.ParseInt(count);
                primStates.clear();
                if (count > 0) primStates.reserve(count);
                while (!scan.IsEOF()) {
                    scan.SkipWhitespace();
                    if (scan.Match(')')) break;
                    if (scan.MatchKeyword("prim_state")) {
                        std::string psName;
                        scan.SkipWhitespace();
                        if (scan.p < scan.end && *scan.p == '"') {
                            scan.ParseString(psName);
                        } else if (scan.p < scan.end && *scan.p != '(') {
                            const char* nameStart = scan.p;
                            while (scan.p < scan.end && *scan.p != '(' && *scan.p != '\r' && *scan.p != '\n') {
                                scan.p++;
                            }
                            const char* nameEnd = scan.p;
                            while (nameEnd > nameStart && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) {
                                nameEnd--;
                            }
                            psName.assign(nameStart, nameEnd);
                        }
                        if (scan.Match('(')) {
                            MstsPrimState ps;
                            scan.ParseUIntHexOrDec(ps.flags);
                            scan.ParseInt(ps.shaderIdx);
                            if (scan.MatchKeyword("tex_idxs")) {
                                if (scan.Match('(')) {
                                    int tCount = 0; scan.ParseInt(tCount);
                                    if (tCount > 0) scan.ParseInt(ps.texIdx);
                                    while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                    scan.Match(')');
                                }
                            }
                            float bias = 0.0f;
                            scan.ParseFloat(bias);
                            scan.ParseInt(ps.vtxStateIdx);
                            // Skip remaining fields to matching ')'
                            int depth = 1;
                            while (!scan.IsEOF() && depth > 0) {
                                if (*scan.p == '(') depth++;
                                else if (*scan.p == ')') {
                                    depth--;
                                    if (depth == 0) { scan.p++; break; }
                                }
                                scan.p++;
                            }
                            primStates.push_back(ps);
                        }
                    } else {
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("lod_controls")) {
            if (scan.Match('(')) {
                int lodCtrlCount = 0; scan.ParseInt(lodCtrlCount);
                if (scan.MatchKeyword("lod_control")) {
                    if (scan.Match('(')) {
                        while (!scan.IsEOF()) {
                            scan.SkipWhitespace();
                            if (scan.Match(')')) break;

                            if (scan.MatchKeyword("distance_levels_header")) {
                                if (scan.Match('(')) {
                                    while (!scan.IsEOF()) {
                                        scan.SkipWhitespace();
                                        if (scan.Match(')')) break;
                                        if (scan.MatchKeyword("hierarchy")) {
                                            if (scan.Match('(')) {
                                                int hCount = 0; scan.ParseInt(hCount);
                                                hierarchy.clear();
                                                if (hCount > 0) hierarchy.reserve(hCount);
                                                while (!scan.IsEOF()) {
                                                    scan.SkipWhitespace();
                                                    if (scan.Match(')')) break;
                                                    int nodeVal = 0;
                                                    if (scan.ParseInt(nodeVal)) {
                                                        hierarchy.push_back(nodeVal);
                                                    } else {
                                                        scan.SkipUnknownElement();
                                                    }
                                                }
                                            }
                                        } else {
                                            scan.SkipUnknownElement();
                                        }
                                    }
                                }
                            }
                            else if (scan.MatchKeyword("distance_levels")) {
                                if (scan.Match('(')) {
                                    int dLevelCount = 0; scan.ParseInt(dLevelCount);

                                    // Parse Highest Detail Level (LOD 0)
                                    if (scan.MatchKeyword("distance_level")) {
                                        if (scan.Match('(')) {
                                            while (!scan.IsEOF()) {
                                                scan.SkipWhitespace();
                                                if (scan.Match(')')) break;

                                                if (scan.MatchKeyword("distance_level_header")) {
                                                    if (scan.Match('(')) {
                                                        while (!scan.IsEOF()) {
                                                            scan.SkipWhitespace();
                                                            if (scan.Match(')')) break;
                                                            if (scan.MatchKeyword("hierarchy")) {
                                                                if (scan.Match('(')) {
                                                                    int hCount = 0; scan.ParseInt(hCount);
                                                                    hierarchy.clear();
                                                                    if (hCount > 0) hierarchy.reserve(hCount);
                                                                    while (!scan.IsEOF()) {
                                                                        scan.SkipWhitespace();
                                                                        if (scan.Match(')')) break;
                                                                        int nodeVal = 0;
                                                                        if (scan.ParseInt(nodeVal)) {
                                                                            hierarchy.push_back(nodeVal);
                                                                        } else {
                                                                            scan.SkipUnknownElement();
                                                                        }
                                                                    }
                                                                }
                                                            } else {
                                                                scan.SkipUnknownElement();
                                                            }
                                                        }
                                                    }
                                                }
                                                else if (scan.MatchKeyword("sub_objects")) {
                                                    outShape.boneHierarchy = hierarchy;

                                                    // Compute Hierarchical World Matrices
                                                    worldMatrices.resize(localMatrices.size(), XMMatrixIdentity());
                                                    outShape.bindWorldMatrices.resize(localMatrices.size());
                                                    for (size_t m = 0; m < localMatrices.size(); ++m) {
                                                        int parent = (m < hierarchy.size()) ? hierarchy[m] : -1;
                                                        if (parent >= 0 && (size_t)parent < worldMatrices.size()) {
                                                            worldMatrices[m] = XMMatrixMultiply(localMatrices[m], worldMatrices[parent]);
                                                        } else {
                                                            worldMatrices[m] = localMatrices[m];
                                                        }
                                                        XMStoreFloat4x4(&outShape.bindWorldMatrices[m], worldMatrices[m]);
                                                    }

                                                    if (scan.Match('(')) {
                                                        int subObjCount = 0; scan.ParseInt(subObjCount);
                                                        while (!scan.IsEOF()) {
                                                            scan.SkipWhitespace();
                                                            if (scan.Match(')')) break;

                                                            if (scan.MatchKeyword("sub_object")) {
                                                                if (scan.Match('(')) {
                                                                    std::vector<MstsLocalVertex> subVertices;

                                                                    while (!scan.IsEOF()) {
                                                                        scan.SkipWhitespace();
                                                                        if (scan.Match(')')) break;

                                                                        if (scan.MatchKeyword("sub_object_header")) {
                                                                            scan.SkipUnknownElement();
                                                                        }
                                                                        else if (scan.MatchKeyword("vertices")) {
                                                                            if (scan.Match('(')) {
                                                                                int vCount = 0; scan.ParseInt(vCount);
                                                                                subVertices.clear();
                                                                                if (vCount > 0) subVertices.reserve(vCount);
                                                                                while (!scan.IsEOF()) {
                                                                                    scan.SkipWhitespace();
                                                                                    if (scan.Match(')')) break;

                                                                                    if (scan.p + 6 < scan.end && (scan.p[0] == 'v' || scan.p[0] == 'V') && (scan.p[1] == 'e' || scan.p[1] == 'E') && (scan.p[2] == 'r' || scan.p[2] == 'R') && (scan.p[3] == 't' || scan.p[3] == 'T') && (scan.p[4] == 'e' || scan.p[4] == 'E') && (scan.p[5] == 'x' || scan.p[5] == 'X')) {
                                                                                        scan.p += 6;
                                                                                        while (scan.p < scan.end && *scan.p != '(') scan.p++;
                                                                                        if (scan.p < scan.end && *scan.p == '(') scan.p++;
                                                                                        MstsLocalVertex sv;
                                                                                        uint32_t flags = 0; scan.ParseUIntHexOrDec(flags);
                                                                                        scan.ParseInt(sv.pointIdx);
                                                                                        scan.ParseInt(sv.normalIdx);
                                                                                        uint32_t c1 = 0, c2 = 0;
                                                                                        scan.ParseUIntHexOrDec(c1);
                                                                                        scan.ParseUIntHexOrDec(c2);

                                                                                        if (scan.MatchKeyword("vertex_uvs")) {
                                                                                            if (scan.Match('(')) {
                                                                                                int uvCount = 0; scan.ParseInt(uvCount);
                                                                                                if (uvCount > 0) scan.ParseInt(sv.uvIdx);
                                                                                                while (scan.p < scan.end && *scan.p != ')') scan.p++;
                                                                                                if (scan.p < scan.end && *scan.p == ')') scan.p++;
                                                                                            }
                                                                                        }
                                                                                        while (scan.p < scan.end && *scan.p != ')') scan.p++;
                                                                                        if (scan.p < scan.end && *scan.p == ')') scan.p++;
                                                                                        subVertices.push_back(sv);
                                                                                    } else if (scan.MatchKeyword("vertex")) {
                                                                                        if (scan.Match('(')) {
                                                                                            MstsLocalVertex sv;
                                                                                            uint32_t flags = 0; scan.ParseUIntHexOrDec(flags);
                                                                                            scan.ParseInt(sv.pointIdx);
                                                                                            scan.ParseInt(sv.normalIdx);
                                                                                            uint32_t c1 = 0, c2 = 0;
                                                                                            scan.ParseUIntHexOrDec(c1);
                                                                                            scan.ParseUIntHexOrDec(c2);

                                                                                            if (scan.MatchKeyword("vertex_uvs")) {
                                                                                                if (scan.Match('(')) {
                                                                                                    int uvCount = 0; scan.ParseInt(uvCount);
                                                                                                    if (uvCount > 0) scan.ParseInt(sv.uvIdx);
                                                                                                    while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                                                    scan.Match(')');
                                                                                                }
                                                                                            }
                                                                                            scan.Match(')');
                                                                                            subVertices.push_back(sv);
                                                                                        }
                                                                                    } else {
                                                                                        scan.SkipUnknownElement();
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        else if (scan.MatchKeyword("primitives")) {
                                                                            if (scan.Match('(')) {
                                                                                int primCount = 0; scan.ParseInt(primCount);
                                                                                int currentPrimStateIdx = 0;

                                                                                while (!scan.IsEOF()) {
                                                                                    scan.SkipWhitespace();
                                                                                    if (scan.Match(')')) break;

                                                                                    if (scan.MatchKeyword("prim_state_idx")) {
                                                                                        if (scan.Match('(')) {
                                                                                            scan.ParseInt(currentPrimStateIdx);
                                                                                            scan.Match(')');
                                                                                        }
                                                                                    }
                                                                                    else if (scan.MatchKeyword("indexed_trilist")) {
                                                                                        if (scan.Match('(')) {
                                                                                            while (!scan.IsEOF()) {
                                                                                                scan.SkipWhitespace();
                                                                                                if (scan.Match(')')) break;

                                                                                                if (scan.MatchKeyword("vertex_idxs")) {
                                                                                                    if (scan.Match('(')) {
                                                                                                        int idxCount = 0; scan.ParseInt(idxCount);

                                                                                                        // Determine Material for SubMesh using currentPrimStateIdx
                                                                                                        ShapeSubMesh subMesh;
                                                                                                        subMesh.startIndex = (uint32_t)outShape.indices.size();
                                                                                                        subMesh.indexCount = idxCount;

                                                                                                        int imgIdx = -1;
                                                                                                        int vtxStateIdx = 0;
                                                                                                        if (currentPrimStateIdx >= 0 && (size_t)currentPrimStateIdx < primStates.size()) {
                                                                                                            const auto& ps = primStates[currentPrimStateIdx];
                                                                                                            vtxStateIdx = ps.vtxStateIdx;
                                                                                                            if (ps.texIdx >= 0 && (size_t)ps.texIdx < textures.size()) {
                                                                                                                imgIdx = textures[ps.texIdx].imageIdx;
                                                                                                            }
                                                                                                            if (ps.shaderIdx >= 0 && (size_t)ps.shaderIdx < shaderNames.size()) {
                                                                                                                const std::string& shName = shaderNames[ps.shaderIdx];
                                                                                                                if (shName.find("Alph") != std::string::npos || shName.find("Alpha") != std::string::npos || shName.find("BlendATex") != std::string::npos) {
                                                                                                                    subMesh.isTransparent = true;
                                                                                                                    subMesh.isAlphaTest = true;  // 1-bit cutout (AlphATex, AlphATexDiff, BlendATex, BlendATexDiff, trainboards, grilles) -> writes depth
                                                                                                                } else if (shName.find("Trans") != std::string::npos || shName.find("Blend") != std::string::npos || shName.find("Add") != std::string::npos) {
                                                                                                                    subMesh.isTransparent = true;
                                                                                                                    subMesh.isAlphaTest = false; // Smooth alpha blend (TransNorm, TransDiff, BlendNorm, cabin glass, tinted windows) -> read-only depth
                                                                                                                } else {
                                                                                                                    subMesh.isTransparent = false;
                                                                                                                    subMesh.isAlphaTest = false;
                                                                                                                }
                                                                                                            }
                                                                                                            if (imgIdx < 0) {
                                                                                                                subMesh.isTransparent = false;
                                                                                                                subMesh.isAlphaTest = false;
                                                                                                            }
                                                                                                        }

                                                                                                        subMesh.imageIndex = imgIdx;
                                                                                                        if (imgIdx >= 0 && (size_t)imgIdx < outShape.rawImageNames.size()) {
                                                                                                            subMesh.textureName = outShape.rawImageNames[imgIdx];
                                                                                                        }

                                                                                                        // Matrix transform for these vertices
                                                                                                        int matrixIdx = 0;
                                                                                                        if (vtxStateIdx >= 0 && (size_t)vtxStateIdx < vtxStates.size()) {
                                                                                                            matrixIdx = vtxStates[vtxStateIdx].matrixIdx;
                                                                                                        }

                                                                                                        XMMATRIX worldMat = (matrixIdx >= 0 && (size_t)matrixIdx < worldMatrices.size())
                                                                                                            ? worldMatrices[matrixIdx]
                                                                                                            : XMMatrixIdentity();

                                                                                                        uint32_t baseVertexOffset = (uint32_t)outShape.vertices.size();
                                                                                                        size_t curVSize = outShape.vertices.size();
                                                                                                        outShape.vertices.resize(curVSize + idxCount);
                                                                                                        outShape.indices.resize(curVSize + idxCount);
                                                                                                        GPUVertex* pDstV = outShape.vertices.data() + curVSize;
                                                                                                        uint32_t* pDstI = outShape.indices.data() + curVSize;

                                                                                                        for (int k = 0; k < idxCount; ++k) {
                                                                                                            int localIdx = 0;
                                                                                                            scan.ParseInt(localIdx);

                                                                                                            if (localIdx >= 0 && (size_t)localIdx < subVertices.size()) {
                                                                                                                const auto& sv = subVertices[localIdx];
                                                                                                                GPUVertex& gv = pDstV[k];
                                                                                                                gv.boneIndex = (uint32_t)matrixIdx;

                                                                                                                // Position (Raw local/bind position)
                                                                                                                XMFLOAT3 rawPos = (sv.pointIdx >= 0 && (size_t)sv.pointIdx < points.size()) ? points[sv.pointIdx] : XMFLOAT3(0, 0, 0);
                                                                                                                gv.pos = rawPos;

                                                                                                                // Normal (Raw local/bind normal)
                                                                                                                XMFLOAT3 rawNorm = (sv.normalIdx >= 0 && (size_t)sv.normalIdx < normals.size()) ? normals[sv.normalIdx] : XMFLOAT3(0, 1, 0);
                                                                                                                gv.normal = rawNorm;

                                                                                                                // UV (U, V)
                                                                                                                gv.uv = (sv.uvIdx >= 0 && (size_t)sv.uvIdx < uvs.size()) ? uvs[sv.uvIdx] : XMFLOAT2(0, 0);

                                                                                                                // Update Bounding Box using world-space pos
                                                                                                                XMVECTOR vPos = XMVector3Transform(XMLoadFloat3(&rawPos), worldMat);
                                                                                                                XMFLOAT3 wp;
                                                                                                                XMStoreFloat3(&wp, vPos);

                                                                                                                outShape.boundsMin.x = (std::min)(outShape.boundsMin.x, wp.x);
                                                                                                                outShape.boundsMin.y = (std::min)(outShape.boundsMin.y, wp.y);
                                                                                                                outShape.boundsMin.z = (std::min)(outShape.boundsMin.z, wp.z);

                                                                                                                outShape.boundsMax.x = (std::max)(outShape.boundsMax.x, wp.x);
                                                                                                                outShape.boundsMax.y = (std::max)(outShape.boundsMax.y, wp.y);
                                                                                                                outShape.boundsMax.z = (std::max)(outShape.boundsMax.z, wp.z);

                                                                                                                pDstI[k] = baseVertexOffset + k;
                                                                                                            } else {
                                                                                                                pDstV[k] = {};
                                                                                                                pDstI[k] = baseVertexOffset + k;
                                                                                                            }
                                                                                                        }

                                                                                                        outShape.subMeshes.push_back(subMesh);
                                                                                                        // Consume any remaining integers/tokens before closing ')'
                                                                                                        while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                                                        scan.Match(')'); // Closes vertex_idxs
                                                                                                    }
                                                                                                }
                                                                                                else {
                                                                                                    scan.SkipUnknownElement();
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                    else {
                                                                                        scan.SkipUnknownElement();
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        else {
                                                                            scan.SkipUnknownElement();
                                                                        }
                                                                    }
                                                                } else {
                                                                    scan.SkipUnknownElement();
                                                                }
                                                            } else {
                                                                scan.SkipUnknownElement();
                                                            }
                                                        }
                                                    }
                                                }
                                                else {
                                                    scan.SkipUnknownElement();
                                                }
                                            }
                                        }
                                    }
                                    // Skip other distance levels (LOD 1..N) and close distance_levels
                                    while (!scan.IsEOF()) {
                                        scan.SkipWhitespace();
                                        if (scan.Match(')')) break;
                                        scan.SkipUnknownElement();
                                    }
                                }
                            }
                            else {
                                scan.SkipUnknownElement();
                            }
                        }
                    }
                    // Skip any other lod_control blocks and close lod_controls
                    while (!scan.IsEOF()) {
                        scan.SkipWhitespace();
                        if (scan.Match(')')) break;
                        scan.SkipUnknownElement();
                    }
                }
            }
        }
        else if (scan.MatchKeyword("animations")) {
            if (scan.Match('(')) {
                int numAnims = 0; scan.ParseInt(numAnims);
                if (scan.MatchKeyword("animation")) {
                    if (scan.Match('(')) {
                        int frameCount = 0; scan.ParseInt(frameCount);
                        float frameRate = 30.0f; scan.ParseFloat(frameRate);
                        outShape.animation.frameCount = (float)frameCount;
                        outShape.animation.frameRate = (frameRate > 0.0f) ? frameRate : 30.0f;
                        outShape.animation.hasAnimation = (frameCount > 0);

                        if (scan.MatchKeyword("anim_nodes")) {
                            if (scan.Match('(')) {
                                int numNodes = 0; scan.ParseInt(numNodes);
                                for (int i = 0; i < numNodes && !scan.IsEOF(); ++i) {
                                    if (scan.MatchKeyword("anim_node")) {
                                        std::string nName;
                                        scan.SkipWhitespace();
                                        if (scan.p < scan.end && *scan.p == '"') {
                                            scan.ParseString(nName);
                                        } else {
                                            const char* nameStart = scan.p;
                                            while (scan.p < scan.end && *scan.p != '(' && *scan.p != '\r' && *scan.p != '\n') {
                                                scan.p++;
                                            }
                                            const char* nameEnd = scan.p;
                                            while (nameEnd > nameStart && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t')) {
                                                nameEnd--;
                                            }
                                            nName.assign(nameStart, nameEnd);
                                        }
                                        AnimNodeTrack track;
                                        track.name = nName;
                                        track.nodeIndex = i;

                                        if (scan.Match('(')) {
                                            if (scan.MatchKeyword("controllers")) {
                                                if (scan.Match('(')) {
                                                    int numControllers = 0; scan.ParseInt(numControllers);
                                                    for (int c = 0; c < numControllers && !scan.IsEOF(); ++c) {
                                                        if (scan.MatchKeyword("linear_pos") || scan.MatchKeyword("tcb_pos") || scan.MatchKeyword("pos_controllers")) {
                                                            if (scan.Match('(')) {
                                                                int numKeys = 0; scan.ParseInt(numKeys);
                                                                for (int k = 0; k < numKeys && !scan.IsEOF(); ++k) {
                                                                    scan.SkipWhitespace();
                                                                    if (scan.MatchKeyword("linear_key") || scan.MatchKeyword("tcb_key") || scan.MatchKeyword("pos_key")) {
                                                                        if (scan.Match('(')) {
                                                                            int f = 0; scan.ParseInt(f);
                                                                            float x = 0, y = 0, z = 0;
                                                                            scan.ParseFloat(x); scan.ParseFloat(y); scan.ParseFloat(z);
                                                                            while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                            track.posKeys.push_back({ (float)f, { x, y, z } });
                                                                            scan.Match(')');
                                                                        }
                                                                    } else if (scan.Match('(')) {
                                                                        int f = 0; scan.ParseInt(f);
                                                                        float x = 0, y = 0, z = 0;
                                                                        scan.ParseFloat(x); scan.ParseFloat(y); scan.ParseFloat(z);
                                                                        while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                        track.posKeys.push_back({ (float)f, { x, y, z } });
                                                                        scan.Match(')');
                                                                    } else {
                                                                        scan.SkipUnknownElement();
                                                                    }
                                                                }
                                                                scan.Match(')');
                                                            }
                                                        }
                                                        else if (scan.MatchKeyword("slerp_rot") || scan.MatchKeyword("tcb_rot") || scan.MatchKeyword("rot_controllers")) {
                                                            if (scan.Match('(')) {
                                                                int numKeys = 0; scan.ParseInt(numKeys);
                                                                for (int k = 0; k < numKeys && !scan.IsEOF(); ++k) {
                                                                    scan.SkipWhitespace();
                                                                    if (scan.MatchKeyword("slerp_rot") || scan.MatchKeyword("tcb_key") || scan.MatchKeyword("slerp_key") || scan.MatchKeyword("rot_key")) {
                                                                        if (scan.Match('(')) {
                                                                            int f = 0; scan.ParseInt(f);
                                                                            float qx = 0, qy = 0, qz = 0, qw = 1.0f;
                                                                            scan.ParseFloat(qx); scan.ParseFloat(qy); scan.ParseFloat(qz); scan.ParseFloat(qw);
                                                                            while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                            track.rotKeys.push_back({ (float)f, { qx, qy, qz, qw } });
                                                                            scan.Match(')');
                                                                        }
                                                                    } else if (scan.Match('(')) {
                                                                        int f = 0; scan.ParseInt(f);
                                                                        float qx = 0, qy = 0, qz = 0, qw = 1.0f;
                                                                        scan.ParseFloat(qx); scan.ParseFloat(qy); scan.ParseFloat(qz); scan.ParseFloat(qw);
                                                                        while (!scan.IsEOF() && *scan.p != ')') scan.p++;
                                                                        track.rotKeys.push_back({ (float)f, { qx, qy, qz, qw } });
                                                                        scan.Match(')');
                                                                    } else {
                                                                        scan.SkipUnknownElement();
                                                                    }
                                                                }
                                                                scan.Match(')');
                                                            }
                                                        }
                                                        else {
                                                            scan.SkipUnknownElement();
                                                        }
                                                    }
                                                    scan.Match(')'); // close controllers
                                                }
                                            }
                                            scan.Match(')'); // close anim_node
                                        }
                                        outShape.animation.animNodes.push_back(track);
                                    } else {
                                        scan.SkipUnknownElement();
                                    }
                                }
                                scan.Match(')'); // close anim_nodes
                            }
                        }
                        scan.Match(')'); // close animation
                    }
                }
                scan.Match(')'); // close animations
            }
        }
        else {
            scan.SkipUnknownElement();
        }
    }
    }

    // Clean up memory mapped file handles
    UnmapViewOfFile(pRawBytes);
    CloseHandle(hMap);
    CloseHandle(hFile);

    if (outShape.vertices.empty() || outShape.indices.empty()) {
        return false;
    }

    // 4. Calculate Center and Bounding Radius
    outShape.center.x = (outShape.boundsMin.x + outShape.boundsMax.x) * 0.5f;
    outShape.center.y = (outShape.boundsMin.y + outShape.boundsMax.y) * 0.5f;
    outShape.center.z = (outShape.boundsMin.z + outShape.boundsMax.z) * 0.5f;

    float dx = outShape.boundsMax.x - outShape.boundsMin.x;
    float dy = outShape.boundsMax.y - outShape.boundsMin.y;
    float dz = outShape.boundsMax.z - outShape.boundsMin.z;
    outShape.radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
    if (outShape.radius < 0.1f) outShape.radius = 1.0f;

    // 5. Finalize Animation Track Mapping and Categories
    size_t numBones = outShape.boneMatrices.size();
    if (numBones > 0) {
        if (outShape.boneHierarchy.size() < numBones) {
            outShape.boneHierarchy.resize(numBones, -1);
        }

        if (outShape.bindWorldMatrices.size() < numBones) {
            outShape.bindWorldMatrices.resize(numBones);
            std::vector<XMMATRIX> worldMats(numBones, XMMatrixIdentity());
            for (size_t m = 0; m < numBones; ++m) {
                int parent = (m < outShape.boneHierarchy.size()) ? outShape.boneHierarchy[m] : -1;
                XMMATRIX localM = XMLoadFloat4x4(&outShape.boneMatrices[m]);
                if (parent >= 0 && (size_t)parent < worldMats.size()) {
                    worldMats[m] = XMMatrixMultiply(localM, worldMats[parent]);
                } else {
                    worldMats[m] = localM;
                }
                XMStoreFloat4x4(&outShape.bindWorldMatrices[m], worldMats[m]);
            }
        }

        FinalizeShapeAnimation(outShape);
    }

    outShape.isValid = true;
    return true;
}

static bool IsWindowOrGlassTextureName(const std::wstring& textureName) {
    if (textureName.empty()) return false;
    std::wstring upper = textureName;
    for (wchar_t& c : upper) c = (wchar_t)towupper(c);
    return (upper.find(L"WINDOW") != std::wstring::npos ||
            upper.find(L"GLASS") != std::wstring::npos ||
            upper.find(L"PANE") != std::wstring::npos ||
            upper.find(L"TINT") != std::wstring::npos ||
            upper.find(L"WINDSHIELD") != std::wstring::npos ||
            upper.find(L"CABIN") != std::wstring::npos ||
            upper.find(L"SCREEN") != std::wstring::npos ||
            upper.find(L"MIRROR") != std::wstring::npos);
}

// Direct3D 11 GPU Buffer and Texture Resource Creator (Must run on D3D thread)
bool ShapeReader::CreateGPUBuffers(
    ID3D11Device* pDevice,
    TextureLoader* pTextureLoader,
    ParsedShape& shape
) {
    if (!pDevice || shape.vertices.empty() || shape.indices.empty()) {
        return false;
    }

    shape.ReleaseGPUBuffers();

    // 1. Vertex Buffer (Immutable)
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(shape.vertices.size() * sizeof(GPUVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbInit = {};
    vbInit.pSysMem = shape.vertices.data();

    if (FAILED(pDevice->CreateBuffer(&vbDesc, &vbInit, &shape.pVertexBuffer))) {
        return false;
    }

    // 2. Index Buffer (Immutable)
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = (UINT)(shape.indices.size() * sizeof(uint32_t));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibInit = {};
    ibInit.pSysMem = shape.indices.data();

    if (FAILED(pDevice->CreateBuffer(&ibDesc, &ibInit, &shape.pIndexBuffer))) {
        shape.ReleaseGPUBuffers();
        return false;
    }

    // 3. Resolve Textures using TextureLoader (Batch Pre-load in parallel)
    if (pTextureLoader) {
        pTextureLoader->PreloadTextures(shape.shapeDir, shape.rawImageNames);

        std::vector<ID3D11ShaderResourceView*> imageSRVs(shape.rawImageNames.size(), nullptr);
        std::vector<bool> imageHasAlpha(shape.rawImageNames.size(), false);
        for (size_t i = 0; i < shape.rawImageNames.size(); ++i) {
            imageSRVs[i] = pTextureLoader->LoadTexture(shape.shapeDir, shape.rawImageNames[i]);
            imageHasAlpha[i] = pTextureLoader->HasAlpha(shape.shapeDir, shape.rawImageNames[i]);
        }

        ID3D11ShaderResourceView* pDefaultSRV = pTextureLoader->GetDefaultTexture();

        for (auto& subMesh : shape.subMeshes) {
            const std::wstring& texName = (!subMesh.textureName.empty()) ? subMesh.textureName :
                ((subMesh.imageIndex >= 0 && (size_t)subMesh.imageIndex < shape.rawImageNames.size()) ? shape.rawImageNames[subMesh.imageIndex] : L"");

            bool hasAlphaFlag = false;
            if (subMesh.imageIndex >= 0 && (size_t)subMesh.imageIndex < imageSRVs.size()) {
                subMesh.pSRV = imageSRVs[subMesh.imageIndex] ? imageSRVs[subMesh.imageIndex] : pDefaultSRV;
                hasAlphaFlag = imageHasAlpha[subMesh.imageIndex];
            } else if (!texName.empty()) {
                subMesh.pSRV = pTextureLoader->LoadTexture(shape.shapeDir, texName);
                if (!subMesh.pSRV) subMesh.pSRV = pDefaultSRV;
                hasAlphaFlag = pTextureLoader->HasAlpha(shape.shapeDir, texName);
            } else {
                subMesh.pSRV = pDefaultSRV;
            }

            // Open Rails pipeline:
            // 1. If texture has NO real alpha (e.g. body textures, chassis, bogies, trainboards),
            //    it is strictly OPAQUE (Pass 1). This ensures locomotive body, roofs, and cabs render solid.
            // 2. If texture has alpha cutout (e.g. couplers, grilles, pantographs, signs, springs),
            //    it is ALPHA-TEST (Pass 2, depth write enabled).
            // 3. Only genuine glass/window textures (e.g. Glass.ace) or translucent materials
            //    render in Pass 3 (Translucent Glass Blend, depth read-only).
            if (!texName.empty()) {
                std::wstring lowerTex = texName;
                for (wchar_t& c : lowerTex) c = towlower(c);
                bool isGlassTex = (lowerTex.find(L"glass") != std::wstring::npos ||
                                   lowerTex.find(L"window") != std::wstring::npos ||
                                   lowerTex.find(L"mirror") != std::wstring::npos);

                if (!hasAlphaFlag) {
                    subMesh.isTransparent = false;
                    subMesh.isAlphaTest = false;
                } else if (!isGlassTex) {
                    subMesh.isTransparent = true;
                    subMesh.isAlphaTest = true;
                } else {
                    subMesh.isTransparent = true;
                    subMesh.isAlphaTest = false;
                }
            }
        }
    }

    return true;
}

void ShapeReader::HotSwapTextures(TextureLoader* pTextureLoader, ParsedShape& shape) {
    if (!pTextureLoader || !shape.isValid) return;

    std::vector<ID3D11ShaderResourceView*> imageSRVs(shape.rawImageNames.size(), nullptr);
    std::vector<bool> imageHasAlpha(shape.rawImageNames.size(), false);
    for (size_t i = 0; i < shape.rawImageNames.size(); ++i) {
        imageSRVs[i] = pTextureLoader->LoadTexture(shape.shapeDir, shape.rawImageNames[i]);
        imageHasAlpha[i] = pTextureLoader->HasAlpha(shape.shapeDir, shape.rawImageNames[i]);
    }

    ID3D11ShaderResourceView* pDefaultSRV = pTextureLoader->GetDefaultTexture();

    for (auto& subMesh : shape.subMeshes) {
        const std::wstring& texName = (!subMesh.textureName.empty()) ? subMesh.textureName :
            ((subMesh.imageIndex >= 0 && (size_t)subMesh.imageIndex < shape.rawImageNames.size()) ? shape.rawImageNames[subMesh.imageIndex] : L"");

        bool hasAlphaFlag = false;
        if (subMesh.imageIndex >= 0 && (size_t)subMesh.imageIndex < imageSRVs.size()) {
            subMesh.pSRV = imageSRVs[subMesh.imageIndex] ? imageSRVs[subMesh.imageIndex] : pDefaultSRV;
            hasAlphaFlag = imageHasAlpha[subMesh.imageIndex];
        } else if (!texName.empty()) {
            subMesh.pSRV = pTextureLoader->LoadTexture(shape.shapeDir, texName);
            if (!subMesh.pSRV) subMesh.pSRV = pDefaultSRV;
            hasAlphaFlag = pTextureLoader->HasAlpha(shape.shapeDir, texName);
        } else {
            subMesh.pSRV = pDefaultSRV;
        }

        if (!texName.empty()) {
            std::wstring lowerTex = texName;
            for (wchar_t& c : lowerTex) c = towlower(c);
            bool isGlassTex = (lowerTex.find(L"glass") != std::wstring::npos ||
                               lowerTex.find(L"window") != std::wstring::npos ||
                               lowerTex.find(L"mirror") != std::wstring::npos);

            if (!hasAlphaFlag) {
                subMesh.isTransparent = false;
                subMesh.isAlphaTest = false;
            } else if (!isGlassTex) {
                subMesh.isTransparent = true;
                subMesh.isAlphaTest = true;
            } else {
                subMesh.isTransparent = true;
                subMesh.isAlphaTest = false;
            }
        }
    }
}

// High-performance loader: ParseShapeFile + CreateGPUBuffers
bool ShapeReader::LoadShapeFile(
    const std::wstring& shapeFilePath,
    ID3D11Device* pDevice,
    TextureLoader* pTextureLoader,
    ParsedShape& outShape
) {
    if (!ParseShapeFile(shapeFilePath, outShape)) {
        return false;
    }
    if (pDevice) {
        if (!CreateGPUBuffers(pDevice, pTextureLoader, outShape)) {
            return false;
        }
    }
    return true;
}

// Fast header inspect to query image names without vertex loading
bool ShapeReader::GetShapeImages(
    const std::wstring& shapeFilePath,
    std::vector<std::wstring>& outImageNames
) {
    ParsedShape tempShape;
    if (ParseShapeFile(shapeFilePath, tempShape)) {
        outImageNames = tempShape.rawImageNames;
        return true;
    }
    return false;
}

std::string ShapeReader::NormalizeNodeName(const std::string& name) {
    std::string s;
    s.reserve(name.size());
    for (char c : name) {
        if (std::isalnum((unsigned char)c)) {
            s.push_back((char)std::tolower((unsigned char)c));
        }
    }
    return s;
}

AnimNodeType ShapeReader::CategorizeNodeName(const std::string& rawName) {
    std::string s = rawName;
    for (char& c : s) c = (char)std::toupper((unsigned char)c);

    // 1. Pantographs
    if (s.find("PANTO") != std::string::npos || s.find("PANT") != std::string::npos) {
        return AnimNodeType::Pantograph;
    }
    // 2. Doors, Mirrors, Steps, Gates
    if (s.find("DOOR") != std::string::npos || s.find("MIRROR") != std::string::npos || 
        s.find("STEP") != std::string::npos || s.find("GATE") != std::string::npos) {
        return AnimNodeType::DoorOrMirror;
    }
    // 3. Wipers
    if (s.find("WIPER") != std::string::npos) {
        return AnimNodeType::Wiper;
    }
    // 4. Wheels, Bogies, Axles, Motion Rods
    if (s.find("WHEEL") != std::string::npos || s.find("BOGIE") != std::string::npos || 
        s.find("ROD") != std::string::npos || s.find("AXLE") != std::string::npos ||
        s.find("CRANK") != std::string::npos || s.find("PISTON") != std::string::npos ||
        s.find("LATTICE") != std::string::npos) {
        return AnimNodeType::WheelOrBogie;
    }
    // 5. Fans, Blowers, HVAC, Radiators
    if (s.find("FAN") != std::string::npos || s.find("VENT") != std::string::npos || 
        s.find("BLOWER") != std::string::npos || s.find("COOL") != std::string::npos ||
        s.find("HVAC") != std::string::npos) {
        return AnimNodeType::FanOrBlower;
    }
    // 6. Driver, Crew, Human meshes & rigs
    if (s.find("DRIVER") != std::string::npos || s.find("BODY") != std::string::npos ||
        s.find("RIG") != std::string::npos || s.find("METARIG") != std::string::npos ||
        s.find("HEAD") != std::string::npos || s.find("HAND") != std::string::npos ||
        s.find("ARM") != std::string::npos || s.find("CREW") != std::string::npos) {
        return AnimNodeType::DriverOrCrew;
    }
    // 7. Destination Boards, Digital Displays
    if (s.find("BOARD") != std::string::npos || s.find("DIGI") != std::string::npos ||
        s.find("DEST") != std::string::npos || s.find("SIGN") != std::string::npos ||
        s.find("DISPLAY") != std::string::npos) {
        return AnimNodeType::DisplayOrBoard;
    }
    return AnimNodeType::Custom;
}

#include "ShapeAnimator.h"

void ShapeReader::ComputeAnimatedMatrices(
    const ParsedShape& shape,
    float currentFrame,
    int filterType,
    float wheelSpinAngle,
    bool enableWheelSpin,
    std::vector<DirectX::XMFLOAT4X4>& outWorldMatrices
) {
    ShapeAnimator::ComputeAnimatedMatrices(
        shape,
        currentFrame,
        filterType,
        wheelSpinAngle,
        enableWheelSpin,
        outWorldMatrices
    );
}

