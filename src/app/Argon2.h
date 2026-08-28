#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace ludora {

/// Argon2id (RFC 9106): derivacion de clave memoria-dura.
///
/// POR QUE ARGON2id Y NO SOLO PBKDF2
/// PBKDF2 encarece el ataque con iteraciones, pero una GPU o un ASIC hacen
/// muchas iteraciones en paralelo baratas. Argon2 obliga a usar MUCHA memoria
/// por intento: eso es caro de paralelizar en hardware especializado, que es
/// justo donde un atacante tendria ventaja. La variante "id" combina
/// resistencia a ataques de canal lateral (parte i) y a GPU (parte d).
///
/// El usuario legitimo, con las claves correctas, deriva una sola vez y entra.
/// El atacante que prueba millones de claves paga la memoria en cada intento.
class Argon2 {
public:
    /// Parametros. Los valores por defecto buscan ~0.5 s y 64 MiB por derivacion
    /// en un equipo de escritorio: un buen equilibrio entre coste para el
    /// atacante y espera para el usuario.
    struct Params {
        u32 memKiB      = 65536;   // 64 MiB
        u32 iterations  = 3;       // pasadas sobre la memoria
        u32 parallelism = 1;       // carriles
        u32 outLen      = 32;      // bytes de clave a producir
    };

    /// Deriva una clave. `salt` debe tener al menos 8 bytes.
    /// Devuelve false si los parametros son invalidos o falta memoria.
    static bool deriveKey(const std::string& password,
                          const std::vector<u8>& salt,
                          const Params& p,
                          std::vector<u8>& out);
};

} // namespace ludora
