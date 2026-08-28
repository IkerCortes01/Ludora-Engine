#pragma once
#include "core/Types.h"
#include <string>

namespace ludora {

/// Cuenta local unica. El motor admite UNA sola cuenta: si ya existe, la
/// pantalla pasa a modo "iniciar sesion" y no deja crear otra.
///
/// La contrasena nunca se guarda: se almacena SHA-256(salt + contrasena)
/// junto al salt aleatorio, ambos en hexadecimal.
class Account {
public:
    /// Dominio obligatorio de los correos.
    static constexpr const char* kDomain = "@ludora.engine";

    /// Valida el formato usuario@ludora.engine.
    /// Devuelve false y llena `error` si no cumple.
    static bool validateEmail(const std::string& email, std::string& error);

    /// Requisitos minimos de la contrasena.
    static bool validatePassword(const std::string& pass, std::string& error);

    /// true si ya hay una cuenta registrada en disco.
    static bool exists(const std::wstring& path);

    /// Crea la cuenta. Falla si ya existe una (solo se permite una).
    static bool create(const std::wstring& path, const std::string& email,
                       const std::string& password, std::string& error);

    /// Comprueba las credenciales contra la cuenta guardada.
    static bool verify(const std::wstring& path, const std::string& email,
                       const std::string& password, std::string& error);

    /// Correo de la cuenta registrada ("" si no hay ninguna).
    static std::string storedEmail(const std::wstring& path);

    /// Resultado de buscar una cuenta guardada en el sistema.
    struct Found {
        bool         ok = false;
        std::wstring path;      // donde se encontro
        std::string  email;     // correo de la cuenta
        std::string  userName;  // nombre de usuario (del perfil, si hay)
    };

    /// Busca una cuenta ya registrada por las ubicaciones habituales del
    /// sistema (carpeta de la app, AppData, Documentos, escritorio, unidades).
    /// Devuelve la primera cuenta valida encontrada. No lee ubicacion ni datos
    /// del usuario mas alla del correo y el nombre.
    static Found searchSystem();

    /// Datos del dispositivo asociado a la cuenta.
    struct DeviceInfo {
        bool        registered = false;   // hay un dispositivo vinculado
        bool        verified   = false;   // la huella coincide con este equipo
        std::string kindName;             // "Escritorio", "Portatil"...
    };

    /// Comprueba si ESTE equipo es el vinculado a la cuenta. Solo compara
    /// hashes: nunca reconstruye el identificador de origen.
    static DeviceInfo deviceInfo(const std::wstring& path);

    /// true si la cuenta puede abrirse sin pedir contrasena: hay cuenta, hay
    /// dispositivo vinculado y la huella de este equipo coincide.
    static bool canAutoLogin(const std::wstring& path);

    /// Desvincula el dispositivo: la proxima vez volvera a pedir contrasena.
    /// La cuenta y su contrasena no se tocan.
    static bool forgetDevice(const std::wstring& path, std::string& error);

private:
    /// Contenido del archivo de cuenta, ya parseado.
    struct Record {
        std::string email;
        std::string salt;        // salt de la contrasena
        std::string hash;        // SHA-256(salt + contrasena)
        std::string devSalt;     // salt de la huella del dispositivo
        std::string devHash;     // SHA-256(devSalt + identificadores)
        std::string devKind;     // tipo generico: "Escritorio", "Portatil"...
        bool valid() const { return !email.empty() && !salt.empty() && !hash.empty(); }
    };

    static bool readRecord(const std::wstring& path, Record& out);
    static bool writeRecord(const std::wstring& path, const Record& rec);

    /// SHA-256 en hexadecimal minusculas usando bcrypt (API de Windows).
    static std::string sha256Hex(const std::string& data);
    /// Salt aleatorio en hexadecimal, criptograficamente seguro.
    static std::string randomSaltHex(size_t bytes = 16);
};

} // namespace ludora
