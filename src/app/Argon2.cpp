#include "app/Argon2.h"
#include "app/Blake2b.h"
#include <cstring>
#include <vector>

namespace ludora {

namespace {

constexpr u32 kSyncPoints = 4;    // segmentos por carril y pasada
constexpr u32 kBlockSize  = 1024; // bytes por bloque de memoria
constexpr u32 kQWords     = kBlockSize / 8;   // 128 u64 por bloque

// Un bloque de memoria de Argon2. Se envuelve en struct para poder tenerlo
// en un std::vector (un array crudo no es asignable ni copiable por defecto).
struct Block {
    u64 v[kQWords];
    u64&       operator[](size_t i)       { return v[i]; }
    const u64& operator[](size_t i) const { return v[i]; }
};

void store32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v); p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16); p[3] = static_cast<u8>(v >> 24);
}
void store64(u8* p, u64 v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<u8>(v >> (8 * i));
}
u64 load64(const u8* p) {
    u64 v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<u64>(p[i]) << (8 * i);
    return v;
}

/// H': funcion hash de salida variable de Argon2, construida sobre BLAKE2b.
void hashPrime(u8* out, u32 outLen, const u8* in, size_t inLen) {
    u8 lenBuf[4];
    store32(lenBuf, outLen);

    if (outLen <= 64) {
        Blake2b b;
        b.init(outLen);
        b.update(lenBuf, 4);
        b.update(in, inLen);
        b.finalize(out);
        return;
    }

    // Para salidas grandes se encadenan bloques BLAKE2b-64 tomando 32 bytes
    // de cada uno, segun la especificacion.
    u8 v[64];
    Blake2b b;
    b.init(64);
    b.update(lenBuf, 4);
    b.update(in, inLen);
    b.finalize(v);

    u32 pos = 0;
    std::memcpy(out + pos, v, 32); pos += 32;
    u32 remaining = outLen - 32;

    while (remaining > 64) {
        Blake2b::hash(v, 64, v, 64);
        std::memcpy(out + pos, v, 32); pos += 32;
        remaining -= 32;
    }
    Blake2b::hash(v, remaining, v, 64);
    std::memcpy(out + pos, v, remaining);
    std::memset(v, 0, sizeof(v));
}

inline u64 rotr64(u64 x, unsigned n) { return (x >> n) | (x << (64 - n)); }

// Funcion G de BLAKE2 con la multiplicacion truncada de Argon2.
inline void GB(u64& a, u64& b, u64& c, u64& d) {
    a = a + b + 2 * (a & 0xFFFFFFFFull) * (b & 0xFFFFFFFFull);
    d = rotr64(d ^ a, 32);
    c = c + d + 2 * (c & 0xFFFFFFFFull) * (d & 0xFFFFFFFFull);
    b = rotr64(b ^ c, 24);
    a = a + b + 2 * (a & 0xFFFFFFFFull) * (b & 0xFFFFFFFFull);
    d = rotr64(d ^ a, 16);
    c = c + d + 2 * (c & 0xFFFFFFFFull) * (d & 0xFFFFFFFFull);
    b = rotr64(b ^ c, 63);
}

// Permutacion P sobre 16 palabras (8 pares).
inline void P(u64* v) {
    GB(v[0], v[4], v[8],  v[12]);
    GB(v[1], v[5], v[9],  v[13]);
    GB(v[2], v[6], v[10], v[14]);
    GB(v[3], v[7], v[11], v[15]);
    GB(v[0], v[5], v[10], v[15]);
    GB(v[1], v[6], v[11], v[12]);
    GB(v[2], v[7], v[8],  v[13]);
    GB(v[3], v[4], v[9],  v[14]);
}

/// Compresion G(X, Y): rellena `out` a partir de dos bloques.
void fillBlock(const Block& x, const Block& y, Block& out, bool withXor) {
    Block r, z;
    for (u32 i = 0; i < kQWords; ++i) r[i] = x[i] ^ y[i];
    z = r;

    // Rondas por filas (8 grupos de 16).
    for (u32 i = 0; i < 8; ++i) {
        u64* p = z.v + 16 * i;
        u64 v[16];
        for (int k = 0; k < 16; ++k) v[k] = p[k];
        P(v);
        for (int k = 0; k < 16; ++k) p[k] = v[k];
    }
    // Rondas por columnas.
    for (u32 i = 0; i < 8; ++i) {
        u64 v[16];
        for (int k = 0; k < 8; ++k) {
            v[2 * k]     = z[16 * k + 2 * i];
            v[2 * k + 1] = z[16 * k + 2 * i + 1];
        }
        P(v);
        for (int k = 0; k < 8; ++k) {
            z[16 * k + 2 * i]     = v[2 * k];
            z[16 * k + 2 * i + 1] = v[2 * k + 1];
        }
    }

    for (u32 i = 0; i < kQWords; ++i) {
        const u64 val = z[i] ^ r[i];
        out[i] = withXor ? (out[i] ^ val) : val;
    }
}

} // namespace

bool Argon2::deriveKey(const std::string& password,
                       const std::vector<u8>& salt,
                       const Params& p,
                       std::vector<u8>& out) {
    if (salt.size() < 8) return false;
    if (p.parallelism == 0 || p.iterations == 0 || p.outLen == 0) return false;

    const u32 lanes = p.parallelism;

    // La memoria se ajusta a un multiplo de 4*lanes bloques.
    u32 memBlocks = p.memKiB;
    if (memBlocks < 8 * lanes) memBlocks = 8 * lanes;
    memBlocks -= memBlocks % (kSyncPoints * lanes);

    const u32 laneLen    = memBlocks / lanes;
    const u32 segmentLen = laneLen / kSyncPoints;

    std::vector<Block> mem;
    try {
        mem.resize(memBlocks);
    } catch (...) {
        return false;   // sin memoria para los parametros pedidos
    }

    // --- H0: hash inicial de todos los parametros ---
    u8 h0[72];
    {
        Blake2b b;
        b.init(64);
        u8 t[4];
        store32(t, lanes);            b.update(t, 4);
        store32(t, p.outLen);         b.update(t, 4);
        store32(t, memBlocks);        b.update(t, 4);   // el m real, ya ajustado
        store32(t, p.iterations);     b.update(t, 4);
        store32(t, 0x13);             b.update(t, 4);   // version 1.3
        store32(t, 2);               b.update(t, 4);    // tipo 2 = Argon2id
        store32(t, static_cast<u32>(password.size())); b.update(t, 4);
        b.update(reinterpret_cast<const u8*>(password.data()), password.size());
        store32(t, static_cast<u32>(salt.size()));      b.update(t, 4);
        b.update(salt.data(), salt.size());
        store32(t, 0); b.update(t, 4);   // sin secreto
        store32(t, 0); b.update(t, 4);   // sin datos asociados
        b.finalize(h0);
    }

    // --- primeros dos bloques de cada carril ---
    for (u32 lane = 0; lane < lanes; ++lane) {
        u8 in[72], blk[kBlockSize];
        std::memcpy(in, h0, 64);
        store32(in + 64, 0);
        store32(in + 68, lane);
        hashPrime(blk, kBlockSize, in, 72);
        for (u32 i = 0; i < kQWords; ++i) mem[lane * laneLen + 0][i] = load64(blk + i * 8);

        store32(in + 64, 1);
        hashPrime(blk, kBlockSize, in, 72);
        for (u32 i = 0; i < kQWords; ++i) mem[lane * laneLen + 1][i] = load64(blk + i * 8);
    }

    // --- pasadas ---
    for (u32 pass = 0; pass < p.iterations; ++pass) {
        for (u32 slice = 0; slice < kSyncPoints; ++slice) {
            for (u32 lane = 0; lane < lanes; ++lane) {

                // Argon2id: la primera mitad de la primera pasada usa el modo
                // "i" (indices independientes de los datos, resistente a canal
                // lateral); el resto usa el modo "d" (dependiente de datos).
                const bool dataIndependent =
                    (pass == 0 && slice < kSyncPoints / 2);

                u32 startIndex = 0;
                if (pass == 0 && slice == 0) startIndex = 2;

                // Para el modo "i" se genera un flujo pseudoaleatorio de
                // direcciones con la propia compresion.
                Block addr{}, inputBlk{}, zero{};
                u32 addrIndex = 0;
                if (dataIndependent) {
                    inputBlk[0] = pass;
                    inputBlk[1] = lane;
                    inputBlk[2] = slice;
                    inputBlk[3] = memBlocks;
                    inputBlk[4] = p.iterations;
                    inputBlk[5] = 2;   // Argon2id
                }

                for (u32 i = startIndex; i < segmentLen; ++i) {
                    const u32 idx = slice * segmentLen + i;

                    // pseudoRand: aleatorio para elegir el bloque de referencia.
                    u64 pseudoRand;
                    if (dataIndependent) {
                        if (addrIndex % kQWords == 0) {
                            inputBlk[6]++;
                            fillBlock(zero, inputBlk, addr, false);
                            fillBlock(zero, addr, addr, false);
                        }
                        pseudoRand = addr[addrIndex % kQWords];
                        addrIndex++;
                    } else {
                        const u32 prev = (idx == 0) ? (lane * laneLen + laneLen - 1)
                                                    : (lane * laneLen + idx - 1);
                        pseudoRand = mem[prev][0];
                    }

                    // --- indexado del bloque de referencia (un solo carril) ---
                    // Cuantos bloques anteriores son elegibles como referencia.
                    u32 refAreaSize;
                    if (pass == 0)
                        refAreaSize = slice * segmentLen + i - 1;   // lo ya escrito
                    else
                        refAreaSize = laneLen - segmentLen + i - 1; // todo menos el segmento en curso

                    const u64 rand32 = pseudoRand & 0xFFFFFFFFull;
                    u64 relPos = (rand32 * rand32) >> 32;
                    relPos = refAreaSize - 1 - ((refAreaSize * relPos) >> 32);

                    u32 startPos = 0;
                    if (pass != 0)
                        startPos = (slice == kSyncPoints - 1) ? 0 : (slice + 1) * segmentLen;

                    const u32 refIndex =
                        static_cast<u32>((startPos + relPos) % laneLen);

                    const Block& refBlock = mem[lane * laneLen + refIndex];
                    const u32 prevIdx = (idx == 0) ? (lane * laneLen + laneLen - 1)
                                                   : (lane * laneLen + idx - 1);
                    // Tras la primera pasada, el bloque nuevo se XORea con el viejo.
                    fillBlock(mem[prevIdx], refBlock,
                              mem[lane * laneLen + idx], pass != 0);
                }
            }
        }
    }

    // --- salida: XOR del ultimo bloque de cada carril, luego H' ---
    Block finalBlock = mem[laneLen - 1];
    for (u32 lane = 1; lane < lanes; ++lane)
        for (u32 i = 0; i < kQWords; ++i)
            finalBlock[i] ^= mem[lane * laneLen + laneLen - 1][i];

    u8 finalBytes[kBlockSize];
    for (u32 i = 0; i < kQWords; ++i) store64(finalBytes + i * 8, finalBlock.v[i]);

    out.assign(p.outLen, 0);
    hashPrime(out.data(), p.outLen, finalBytes, kBlockSize);

    // Limpiar la memoria: no dejar el estado intermedio en RAM.
    for (auto& b : mem) std::memset(b.v, 0, sizeof(b.v));
    std::memset(h0, 0, sizeof(h0));
    return true;
}

} // namespace ludora
