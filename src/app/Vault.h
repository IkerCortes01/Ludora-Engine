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
    /// Capas AES-GCM que se aplican al CIFRAR.
    ///
    /// Era 10, y no daba diez veces mas seguridad: las diez derivaban del
    /// MISMO secreto, asi que quien rompiera una las rompia todas. Lo que de
    /// verdad protege es el estirado (Argon2id + 120.000 iteraciones de
    /// PBKDF2), que no se ha tocado.
    ///
    /// Lo que si costaban era tiempo: diez pasadas de AES sobre el paquete
    /// entero, cada una con su derivacion y una copia completa del buffer.
    /// Para un juego de 300 MB eso son 3 GB movidos de mas.
    ///
    /// ABRIR sigue admitiendo cualquier numero: la cuenta va escrita en la
    /// cabecera de cada paquete, asi que los .tomate de 10 capas ya
    /// guardados se leen exactamente igual.
    static constexpr i32    kLayers      = 1;
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

    /// Una pasada de AES-256-GCM autenticada (AEAD). Cifra o descifra `in` con
    /// `key` (32 bytes) y `nonce` (12 bytes), produciendo/verificando `tag`
    /// (16 bytes). Al descifrar, un tag que no cuadra hace fallar la llamada:
    /// asi se detecta cualquier manipulacion antes de usar los datos.
    /// Es la primitiva de bajo nivel que usa el VFS para cifrar cada asset con
    /// la clave maestra, sin repetir el estirado Argon2id por asset.
    static bool aead(bool encrypt,
                     const std::vector<u8>& key,
                     const std::vector<u8>& nonce,
                     const std::vector<u8>& in,
                     std::vector<u8>& tag,
                     std::vector<u8>& out);

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

    // ------------------------------------------------------------------
    //  ROTACION DE CLAVES Y DETECCION DE MANIPULACION
    // ------------------------------------------------------------------
    //
    // QUE ES LA ROTACION AQUI, Y QUE NO ES
    // Cada llamada a seal() genera salts y nonces NUEVOS, asi que el mismo
    // contenido cifrado dos veces produce bytes distintos y claves distintas.
    // Eso es rotacion real: un atacante que capture una version del archivo
    // no obtiene nada util para la siguiente.
    //
    // Lo que NO hace, dicho sin rodeos: no rota "cada milisegundo" ni
    // coordina nada por red. Sin un servidor que custodie claves, rotar mas
    // a menudo que al guardar no aporta seguridad -- solo gasta CPU, porque
    // el secreto del que todo deriva sigue siendo el mismo.

    /// Estado de un almacen tras intentar abrirlo.
    enum class OpenResult {
        Ok,            // se abrio y el contenido es integro
        BadSecret,     // el secreto no coincide
        Tampered,      // el contenido fue alterado (tag GCM invalido)
        Corrupt,       // el archivo no tiene forma de blob valido
    };

    /// Igual que open(), pero distinguiendo POR QUE fallo. La interfaz normal
    /// sigue usando open() y viendo un error generico; esto es para el
    /// guardian, que necesita saber si hubo manipulacion.
    static OpenResult openDetailed(const std::string& secret,
                                   const std::vector<u8>& blob,
                                   std::vector<u8>& out,
                                   std::string& error);

    /// Vuelve a cifrar un blob con claves NUEVAS, sin cambiar su contenido.
    /// Es la rotacion propiamente dicha: se llama al guardar, y tambien
    /// puede llamarse periodicamente para renovar un archivo en reposo.
    static bool rotate(const std::string& secret,
                       const std::vector<u8>& blob,
                       std::vector<u8>& out,
                       std::string& error);

private:
    static bool s_fastMode;
};

/// Guardian de los almacenes cifrados.
///
/// Vigila los intentos de apertura y corta el acceso cuando el patron deja de
/// parecer el de un usuario legitimo: un archivo alterado, o varios fallos
/// seguidos. No se anuncia en la interfaz -- si un atacante ve el contador,
/// sabe cuantos intentos le quedan.
class VaultGuard {
public:
    /// Intentos fallidos seguidos antes de bloquear.
    static constexpr i32 kMaxFails = 5;
    /// Cuanto dura el bloqueo, en segundos.
    static constexpr long long kLockSeconds = 300;   // 5 minutos

    /// Registra el resultado de una apertura. Devuelve false si a partir de
    /// ahora el acceso queda bloqueado.
    static bool note(Vault::OpenResult r);

    /// true si el acceso esta bloqueado ahora mismo.
    static bool locked();

    /// Segundos que quedan de bloqueo (0 si no lo esta).
    static long long lockRemaining();

    /// Fallos seguidos acumulados.
    static i32 failCount();

    /// Levanta el bloqueo y pone el contador a cero. Lo usa el arranque tras
    /// una autenticacion correcta, y las pruebas.
    static void reset();

    /// true si se detecto manipulacion en algun momento de esta sesion. Una
    /// vez visto, no se olvida: un archivo alterado no vuelve a ser fiable
    /// porque el siguiente intento salga bien.
    static bool tamperSeen();

private:
    static i32       s_fails;
    static long long s_lockedUntil;
    static bool      s_tamper;
};

} // namespace ludora
