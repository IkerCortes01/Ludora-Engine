#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace ludora {

/// Almacen PORTATIL de VARIAS cuentas, con VARIOS dispositivos verificados por
/// cuenta.
///
/// ----------------------------------------------------------------------------
/// POR QUE EXISTE ESTE MODULO
/// ----------------------------------------------------------------------------
/// `Account` nacio con una regla escrita en su cabecera: "el motor admite UNA
/// sola cuenta". De ahi salian tres limites que este modulo levanta:
///
///   1. UN solo `cuenta.dat` al lado del .exe, asi que crear una segunda cuenta
///      fallaba con "Ya existe una cuenta en este equipo".
///   2. UN solo dispositivo por cuenta: `devhash` era un campo unico, de modo
///      que verificar un segundo equipo PISABA al primero. Volver al primero
///      obligaba a escribir la contrasena otra vez, y asi en bucle.
///   3. Ubicacion fija: la cuenta se buscaba en rutas del sistema (AppData,
///      Documentos...), asi que el launcher no podia viajar en un USB con sus
///      cuentas dentro.
///
/// ----------------------------------------------------------------------------
/// LO QUE NO CAMBIA
/// ----------------------------------------------------------------------------
/// La criptografia es la MISMA que ya estaba y funcionaba: SHA-256 con salt
/// aleatorio por cuenta, comparacion en tiempo constante, y la huella del
/// equipo derivada en `Device` (MachineGuid + serial de volumen), que se guarda
/// SIEMPRE hasheada y nunca en claro. Este modulo no inventa criptografia:
/// reorganiza el almacenamiento.
///
/// Tampoco cambia la privacidad: no se guarda ubicacion, ni nombre de equipo,
/// ni de usuario, ni nada que salga por red. Un dispositivo se representa por
/// su hash y una etiqueta generica de tipo ("Escritorio", "Portatil").
///
/// ----------------------------------------------------------------------------
/// FORMATO EN DISCO
/// ----------------------------------------------------------------------------
/// Un directorio `cuentas/` con un archivo por cuenta, `<id>.cuenta`, donde el
/// id se deriva del correo (ver `idForEmail`). Un archivo por cuenta, y no uno
/// solo con todas, para que un archivo corrupto se lleve por delante UNA cuenta
/// y no el almacen entero.
///
/// Dentro, el mismo formato clave=valor de siempre, con los dispositivos como
/// lineas repetidas:
///
///     email=iker@ludora.engine
///     salt=<hex>
///     hash=<hex>
///     dev=<devsalt>|<devhash>|<tipo>|<alta>|<ultimo acceso>
///     dev=<devsalt>|<devhash>|<tipo>|<alta>|<ultimo acceso>
///
/// Repetir la clave `dev` (en vez de numerarlas) mantiene el parser tan simple
/// como el original y hace que anadir o quitar un equipo sea anadir o quitar
/// una linea.
class AccountStore {
public:
    /// Un equipo verificado para una cuenta.
    struct DeviceEntry {
        std::string salt;        // salt propio de esta huella
        std::string hash;        // SHA-256(salt + identificadores del equipo)
        std::string kindName;    // "Escritorio", "Portatil", "Tableta"
        long long   addedAt   = 0;   // alta (epoch en segundos)
        long long   lastSeen  = 0;   // ultimo acceso desde este equipo
    };

    /// Una cuenta con todos sus dispositivos.
    struct Record {
        std::string              email;
        std::string              salt;      // salt de la contrasena
        std::string              hash;      // SHA-256(salt + contrasena)
        std::vector<DeviceEntry> devices;

        bool valid() const {
            return !email.empty() && !salt.empty() && !hash.empty();
        }
    };

    /// Resumen de una cuenta para pintarla en el selector, sin exponer hashes.
    struct Summary {
        std::string email;
        std::string userName;          // del perfil, si lo hay
        bool        deviceVerified = false;  // este equipo ya esta verificado
        int         deviceCount    = 0;      // cuantos equipos tiene vinculados
    };

    // ------------------------------------------------------------------------
    // UBICACION PORTATIL
    // ------------------------------------------------------------------------

    /// Carpeta raiz de datos, resuelta una sola vez y cacheada.
    ///
    /// Se prefiere la carpeta del .exe: asi el launcher metido en un USB se
    /// lleva las cuentas consigo. Si esa carpeta NO es escribible (caso tipico:
    /// instalado en "Archivos de programa", donde Windows lo impide), se cae a
    /// %LOCALAPPDATA%\Ludora, que siempre lo es.
    ///
    /// La prueba de escritura es real (se crea y borra un archivo temporal), no
    /// una suposicion por la ruta: los permisos de Windows no se deducen del
    /// nombre de la carpeta.
    static std::wstring dataRoot();

    /// true si `dataRoot()` esta junto al ejecutable (modo portatil).
    static bool isPortable();

    /// Carpeta `cuentas/` dentro de `dataRoot()`. La crea si no existe.
    static std::wstring accountsDir();

    /// Ruta del archivo de una cuenta concreta.
    static std::wstring pathForEmail(const std::string& email);

    /// Ruta del perfil de una cuenta (nombre de usuario, avatar...).
    static std::wstring profilePathForEmail(const std::string& email);

    /// Identificador de archivo derivado del correo. Minusculas y solo
    /// caracteres seguros para un nombre de archivo, de modo que el correo no
    /// pueda escaparse de la carpeta (`..`, barras, dos puntos).
    static std::string idForEmail(const std::string& email);

    // ------------------------------------------------------------------------
    // CUENTAS
    // ------------------------------------------------------------------------

    /// Todas las cuentas guardadas, ordenadas por correo.
    static std::vector<Summary> list();

    /// Cuantas cuentas hay.
    static size_t count();

    /// true si existe una cuenta con ese correo.
    static bool exists(const std::string& email);

    /// Crea una cuenta NUEVA y vincula este equipo.
    /// Falla solo si YA existe una cuenta con ESE correo -- tener otras cuentas
    /// no impide crear esta, que es justo lo que cambia respecto a `Account`.
    static bool create(const std::string& email, const std::string& password,
                       std::string& error);

    /// Comprueba las credenciales. Si son correctas y este equipo aun no estaba
    /// verificado, lo ANADE a la lista (sin desplazar a los demas).
    static bool verify(const std::string& email, const std::string& password,
                       std::string& error);

    /// Borra una cuenta y su perfil.
    static bool remove(const std::string& email, std::string& error);

    // ------------------------------------------------------------------------
    // DISPOSITIVOS
    // ------------------------------------------------------------------------

    /// true si ESTE equipo esta entre los verificados de la cuenta.
    static bool deviceVerified(const std::string& email);

    /// Dispositivos vinculados a la cuenta (sin sus hashes: solo tipo y fechas).
    struct DeviceView {
        std::string kindName;
        long long   addedAt  = 0;
        long long   lastSeen = 0;
        bool        isThisDevice = false;
    };
    static std::vector<DeviceView> devices(const std::string& email);

    /// Desvincula ESTE equipo de la cuenta. Los demas siguen verificados.
    static bool forgetThisDevice(const std::string& email, std::string& error);

    /// Desvincula TODOS los equipos de la cuenta.
    static bool forgetAllDevices(const std::string& email, std::string& error);

    // ------------------------------------------------------------------------
    // INICIO AUTOMATICO
    // ------------------------------------------------------------------------

    /// Cuentas cuyo dispositivo verificado es ESTE equipo.
    static std::vector<std::string> autoLoginCandidates();

    /// Correo de la unica cuenta que puede entrar sin contrasena, o "" si no
    /// hay ninguna o hay VARIAS.
    ///
    /// Con varias cuentas verificadas en el mismo equipo, entrar solo en una de
    /// ellas seria elegir por el usuario: se devuelve "" y la interfaz muestra
    /// el selector. Elegir ahi no pide contrasena, porque el equipo ya esta
    /// verificado -- solo desambigua quien entra.
    static std::string soleAutoLoginEmail();

    /// Marca que esta cuenta se acaba de usar desde este equipo.
    static void touch(const std::string& email);

    // ------------------------------------------------------------------------
    // MIGRACION
    // ------------------------------------------------------------------------

    /// Convierte un `cuenta.dat` del formato antiguo (una sola cuenta, un solo
    /// dispositivo) al almacen nuevo. Idempotente: si ya se migro, no hace nada.
    ///
    /// El archivo viejo se CONSERVA. Borrarlo dejaria sin vuelta atras a quien
    /// abriera una version anterior del launcher, y el coste de mantenerlo es
    /// un archivo de menos de un kilobyte.
    static bool migrateLegacy(const std::wstring& legacyPath,
                              const std::wstring& legacyProfilePath);

    // ------------------------------------------------------------------------
    // Acceso de bajo nivel (lo usan las pruebas y la migracion)
    // ------------------------------------------------------------------------
    static bool readRecord(const std::wstring& path, Record& out);
    static bool writeRecord(const std::wstring& path, const Record& rec);

    /// Fuerza la carpeta de datos. Solo para las pruebas: permite trabajar
    /// sobre un directorio temporal sin tocar las cuentas reales del usuario.
    /// Con `L""` vuelve a la deteccion normal.
    static void setDataRootForTesting(const std::wstring& root);
};

} // namespace ludora
