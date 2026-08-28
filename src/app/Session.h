#pragma once
#include "core/Types.h"
#include <string>

namespace ludora {

/// Sesion persistente: recuerda que ya iniciaste sesion en ESTE equipo, para
/// no volver a pedir credenciales tras registrarte una vez.
///
/// COMO ES SEGURO
/// El token no se guarda en claro. Se cifra con el Vault (Argon2id + 10 capas
/// AES-256-GCM) usando como secreto la huella del dispositivo -- la misma que
/// vincula el equipo a la cuenta, un hash del hardware que nunca se almacena
/// en claro ni contiene ubicacion. Asi el token solo se puede abrir en el
/// mismo equipo que lo creo: copiarlo a otra maquina no sirve.
///
/// QUE GUARDA
/// El correo y el nombre de usuario con sesion activa. Nada mas.
class Session {
public:
    /// Ruta del archivo de sesion recordada. Compartida entre las escenas.
    static constexpr const wchar_t* kFile = L"sesion.tomate";

    /// Datos de la sesion recordada.
    struct Data {
        bool        valid = false;
        std::string email;
        std::string userName;
    };

    /// Guarda una sesion cifrada tras iniciar sesion correctamente.
    static bool save(const std::wstring& path,
                     const std::string& email,
                     const std::string& userName);

    /// Intenta recuperar la sesion de este equipo. `valid` es false si no hay
    /// sesion, si el archivo es de otro equipo o si esta corrupto.
    static Data load(const std::wstring& path);

    /// Borra la sesion (cerrar sesion). La cuenta y el perfil no se tocan.
    static bool clear(const std::wstring& path);

    /// true si hay una sesion recuperable en este equipo.
    static bool exists(const std::wstring& path);

private:
    /// Secreto de cifrado del token: ligado a este equipo, no adivinable.
    static std::string deviceSecret();
};

} // namespace ludora
