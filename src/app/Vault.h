#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace ludora {

/// Almacen cifrado local ("Nube Tomate" en su parte de dispositivo).
///
/// QUE HACE
/// Cifra datos con 10 capas encadenadas de AES-256-GCM. Cada capa usa una
/// clave DISTINTA e independiente, derivada con PBKDF2-HMAC-SHA512 a partir
/// del material secreto del usuario y un salt propio por capa.
///
/// POR QUE 10 CAPAS
/// Con un cifrador solido, 10 capas no multiplican la seguridad por 10: el
/// eslabon debil sigue siendo la contrasena. Lo que si aportan es que
/// comprometer una clave no basta -- hacen falta las 10 -- y que cada capa
/// lleva su propio tag de autenticacion, de modo que cualquier manipulacion
/// se detecta antes de descifrar la siguiente.
/// El coste real de un ataque lo pone el numero de iteraciones de PBKDF2,
/// no el numero de capas.
///
/// QUE NO HACE
/// No sube nada a ningun servidor. El cifrado se hace ANTES de que los datos
/// salgan del dispositivo, de forma que un servidor remoto solo veria bytes
/// que no puede leer (cifrado de extremo a extremo). La parte de red no esta
/// implementada: requiere infraestructura propia.
class Vault {
public:
    static constexpr i32    kLayers      = 10;
    static constexpr i32    kKeyBytes    = 32;      // AES-256
    static constexpr i32    kNonceBytes  = 12;      // GCM
    static constexpr i32    kTagBytes    = 16;
    static constexpr i32    kSaltBytes   = 32;
    /// Iteraciones de PBKDF2 por capa. Es el parametro que encarece un
    /// ataque por fuerza bruta; subirlo hace mas lento abrir el almacen.
    static constexpr u32    kIterations  = 120000;

    /// Cifra `plain` con las 10 capas. `secret` es el material del usuario
    /// (por ejemplo, las tres contrasenas concatenadas con separador).
    /// Devuelve el blob completo, con cabecera y salts, listo para guardar.
    static bool seal(const std::string& secret,
                     const std::vector<u8>& plain,
                     std::vector<u8>& out,
                     std::string& error);

    /// Descifra un blob producido por seal(). Falla si el secreto no coincide
    /// o si el contenido fue manipulado (lo detecta el tag de cada capa).
    static bool open(const std::string& secret,
                     const std::vector<u8>& blob,
                     std::vector<u8>& out,
                     std::string& error);

    /// Guarda y carga el blob en disco.
    static bool saveFile(const std::wstring& path, const std::vector<u8>& blob);
    static bool loadFile(const std::wstring& path, std::vector<u8>& blob);

    /// Bytes aleatorios de calidad criptografica.
    static bool randomBytes(std::vector<u8>& out, size_t n);

    /// Deriva una clave de `kKeyBytes` con PBKDF2-HMAC-SHA512.
    static bool deriveKey(const std::string& secret,
                          const std::vector<u8>& salt,
                          u32 iterations,
                          std::vector<u8>& key);

    /// Estira el secreto con Argon2id (memoria-dura) una sola vez. Su salida
    /// alimenta las capas PBKDF2. Devuelve bytes crudos en un std::string.
    static std::string stretchSecret(const std::string& secret,
                                     const std::vector<u8>& salt);

    /// Huella publica del secreto, para comprobar que coincide sin exponerlo.
    static std::string fingerprint(const std::string& secret,
                                   const std::vector<u8>& salt);

    /// SOLO PARA PRUEBAS: baja el coste de Argon2 para que el autodiagnostico
    /// termine rapido. La aplicacion real jamas lo activa.
    static void setFastModeForTests(bool on) { s_fastMode = on; }

private:
    static bool s_fastMode;
};

} // namespace ludora
