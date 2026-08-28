#include "app/Session.h"
#include "app/Device.h"
#include "app/Vault.h"
#include <vector>
#include <windows.h>

namespace ludora {

namespace {

/// Marca del contenido, para detectar un descifrado que "cuadra" por azar.
constexpr const char* kTokenMagic = "TOMATE-SESSION-1\n";

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

std::string Session::deviceSecret() {
    // Salt fijo de aplicacion: el secreto debe ser el MISMO en cada arranque
    // de este equipo, por eso no es aleatorio. No protege datos de terceros,
    // solo ata el token a esta maquina; la huella en si ya es un hash del
    // hardware que nunca sale en claro.
    return Device::fingerprint("ludora-nube-tomate-session-v1");
}

bool Session::save(const std::wstring& path,
                   const std::string& email,
                   const std::string& userName) {
    const std::string secret = deviceSecret();
    if (secret.empty()) return false;   // sin huella no se puede atar al equipo

    std::string payload = kTokenMagic;
    payload += "email=" + email + "\n";
    payload += "user="  + userName + "\n";

    const std::vector<u8> plain(payload.begin(), payload.end());
    std::vector<u8> blob;
    std::string err;
    if (!Vault::seal(secret, plain, blob, err)) return false;

    return Vault::saveFile(path, blob);
}

Session::Data Session::load(const std::wstring& path) {
    Data out;

    std::vector<u8> blob;
    if (!Vault::loadFile(path, blob) || blob.empty()) return out;

    const std::string secret = deviceSecret();
    if (secret.empty()) return out;

    std::vector<u8> plain;
    std::string err;
    // Si el archivo es de otro equipo, la huella no coincide y el descifrado
    // falla: eso es justo lo que impide reutilizar el token en otra maquina.
    if (!Vault::open(secret, blob, plain, err)) return out;

    const std::string texto(plain.begin(), plain.end());
    if (texto.rfind(kTokenMagic, 0) != 0) return out;   // marca ausente

    size_t pos = 0;
    while (pos < texto.size()) {
        const size_t nl = texto.find('\n', pos);
        const std::string line = texto.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? texto.size() : nl + 1;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if      (k == "email") out.email    = v;
        else if (k == "user")  out.userName = v;
    }

    out.valid = !out.email.empty();
    return out;
}

bool Session::clear(const std::wstring& path) {
    // Borrar el archivo cifrado basta: sin el, la proxima apertura pedira
    // credenciales de nuevo.
    if (!exists(path)) return true;
    return DeleteFileW(path.c_str()) != 0;
}

bool Session::exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace ludora
