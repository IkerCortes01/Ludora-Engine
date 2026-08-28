#include "app/Blake2b.h"
#include <algorithm>
#include <cstring>

namespace ludora {

namespace {

// Vectores de inicializacion (RFC 7693): primeros bits fraccionarios de las
// raices cuadradas de los 8 primeros primos.
constexpr u64 kIV[8] = {
    0x6A09E667F3BCC908ull, 0xBB67AE8584CAA73Bull,
    0x3C6EF372FE94F82Bull, 0xA54FF53A5F1D36F1ull,
    0x510E527FADE682D1ull, 0x9B05688C2B3E6C1Full,
    0x1F83D9ABFB41BD6Bull, 0x5BE0CD19137E2179ull,
};

// Permutaciones de mensaje (sigma), 12 rondas.
constexpr u8 kSigma[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0},
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
};

inline u64 rotr64(u64 x, unsigned n) { return (x >> n) | (x << (64 - n)); }

inline u64 load64(const u8* p) {
    u64 v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<u64>(p[i]) << (8 * i);
    return v;
}

inline void store64(u8* p, u64 v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<u8>(v >> (8 * i));
}

} // namespace

void Blake2b::init(size_t outLen, const u8* key, size_t keyLen) {
    m_outLen = outLen;
    for (int i = 0; i < 8; ++i) m_h[i] = kIV[i];

    // Bloque de parametros: outLen, keyLen, fanout=1, depth=1.
    m_h[0] ^= 0x01010000ull ^ (static_cast<u64>(keyLen) << 8) ^ outLen;

    m_t[0] = m_t[1] = 0;
    m_bufLen = 0;

    if (key && keyLen) {
        u8 block[kBlockBytes]{};
        std::memcpy(block, key, keyLen);
        update(block, kBlockBytes);
        std::memset(block, 0, sizeof(block));
    }
}

void Blake2b::compress(bool last) {
    u64 m[16], v[16];
    for (int i = 0; i < 16; ++i) m[i] = load64(m_buf + i * 8);
    for (int i = 0; i < 8;  ++i) { v[i] = m_h[i]; v[i + 8] = kIV[i]; }

    v[12] ^= m_t[0];
    v[13] ^= m_t[1];
    if (last) v[14] = ~v[14];

    auto G = [&](int a, int b, int c, int d, u64 x, u64 y) {
        v[a] = v[a] + v[b] + x; v[d] = rotr64(v[d] ^ v[a], 32);
        v[c] = v[c] + v[d];     v[b] = rotr64(v[b] ^ v[c], 24);
        v[a] = v[a] + v[b] + y; v[d] = rotr64(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];     v[b] = rotr64(v[b] ^ v[c], 63);
    };

    for (int r = 0; r < 12; ++r) {
        const u8* s = kSigma[r];
        G(0, 4,  8, 12, m[s[0]],  m[s[1]]);
        G(1, 5,  9, 13, m[s[2]],  m[s[3]]);
        G(2, 6, 10, 14, m[s[4]],  m[s[5]]);
        G(3, 7, 11, 15, m[s[6]],  m[s[7]]);
        G(0, 5, 10, 15, m[s[8]],  m[s[9]]);
        G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        G(2, 7,  8, 13, m[s[12]], m[s[13]]);
        G(3, 4,  9, 14, m[s[14]], m[s[15]]);
    }

    for (int i = 0; i < 8; ++i) m_h[i] ^= v[i] ^ v[i + 8];
}

void Blake2b::update(const u8* data, size_t len) {
    while (len > 0) {
        if (m_bufLen == kBlockBytes) {
            // El contador solo avanza al procesar un bloque que NO es el
            // ultimo: por eso se comprime aqui, con mas datos por delante.
            m_t[0] += kBlockBytes;
            if (m_t[0] < kBlockBytes) ++m_t[1];
            compress(false);
            m_bufLen = 0;
        }
        const size_t take = std::min(len, kBlockBytes - m_bufLen);
        std::memcpy(m_buf + m_bufLen, data, take);
        m_bufLen += take;
        data += take;
        len  -= take;
    }
}

void Blake2b::finalize(u8* out) {
    m_t[0] += m_bufLen;
    if (m_t[0] < m_bufLen) ++m_t[1];

    std::memset(m_buf + m_bufLen, 0, kBlockBytes - m_bufLen);
    compress(true);

    u8 full[64];
    for (int i = 0; i < 8; ++i) store64(full + i * 8, m_h[i]);
    std::memcpy(out, full, m_outLen);
    std::memset(full, 0, sizeof(full));
}

void Blake2b::hash(u8* out, size_t outLen,
                   const u8* in, size_t inLen,
                   const u8* key, size_t keyLen) {
    Blake2b b;
    b.init(outLen, key, keyLen);
    b.update(in, inLen);
    b.finalize(out);
}

} // namespace ludora
