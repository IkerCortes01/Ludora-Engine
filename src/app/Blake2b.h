#pragma once
#include "core/Types.h"
#include <cstddef>

namespace ludora {

/// BLAKE2b (RFC 7693). Funcion hash rapida y segura, base de Argon2.
/// Implementacion propia porque Windows CNG no la ofrece.
class Blake2b {
public:
    static constexpr size_t kBlockBytes = 128;
    static constexpr size_t kMaxOut     = 64;

    /// Inicia un hash de `outLen` bytes (1..64), con clave opcional.
    void init(size_t outLen, const u8* key = nullptr, size_t keyLen = 0);
    void update(const u8* data, size_t len);
    void finalize(u8* out);

    /// Atajo de una sola llamada.
    static void hash(u8* out, size_t outLen,
                     const u8* in, size_t inLen,
                     const u8* key = nullptr, size_t keyLen = 0);

private:
    void compress(bool last);

    u64    m_h[8];            // estado
    u64    m_t[2];            // contador de bytes procesados
    u8     m_buf[kBlockBytes];
    size_t m_bufLen = 0;
    size_t m_outLen = 0;
};

} // namespace ludora
