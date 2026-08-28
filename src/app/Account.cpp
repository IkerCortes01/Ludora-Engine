#include "app/Account.h"
#include "app/Device.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace ludora {

namespace {

constexpr size_t kMinPassword = 6;
constexpr size_t kMaxLocal    = 32;   // parte antes de la @

std::string toHex(const u8* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[ data[i]       & 0x0F]);
    }
    return out;
}

/// Comparacion en tiempo constante: no revela por donde difieren los hashes.
bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

std::string Account::sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    DWORD hashLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &cb, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    std::vector<u8> digest(hashLen);
    std::string out;
    if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), 0) >= 0 &&
        BCryptFinishHash(hash, digest.data(), hashLen, 0) >= 0) {
        out = toHex(digest.data(), digest.size());
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string Account::randomSaltHex(size_t bytes) {
    std::vector<u8> buf(bytes);
    if (BCryptGenRandom(nullptr, buf.data(), static_cast<ULONG>(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return {};
    return toHex(buf.data(), buf.size());
}

bool Account::validateEmail(const std::string& email, std::string& error) {
    const std::string e = trim(email);

    if (e.empty()) { error = "Escribe un correo."; return false; }

    const std::string dom = kDomain;
    if (e.size() <= dom.size() ||
        e.compare(e.size() - dom.size(), dom.size(), dom) != 0) {
        error = "El correo debe terminar en " + dom;
        return false;
    }

    const std::string local = e.substr(0, e.size() - dom.size());
    if (local.empty()) { error = "Falta el nombre antes de la arroba."; return false; }
    if (local.size() > kMaxLocal) { error = "El nombre es demasiado largo."; return false; }

    // Una sola arroba: la del dominio.
    if (local.find('@') != std::string::npos) {
        error = "El correo solo puede tener una arroba.";
        return false;
    }

    for (char c : local) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            error = "Solo letras, numeros, punto, guion y guion bajo.";
            return false;
        }
    }
    return true;
}

bool Account::validatePassword(const std::string& pass, std::string& error) {
    if (pass.size() < kMinPassword) {
        error = "La contrasena necesita al menos 6 caracteres.";
        return false;
    }
    return true;
}

bool Account::readRecord(const std::wstring& path, Record& out) {
    std::ifstream in(path.c_str());
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if      (k == "email")    out.email   = v;
        else if (k == "salt")     out.salt    = v;
        else if (k == "hash")     out.hash    = v;
        else if (k == "devsalt")  out.devSalt = v;
        else if (k == "devhash")  out.devHash = v;
        else if (k == "devkind")  out.devKind = v;
    }
    return out.valid();
}

bool Account::writeRecord(const std::wstring& path, const Record& rec) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;

    out << "# Cuenta de Ludora Engine.\r\n"
        << "# La contrasena NO se guarda: solo SHA-256(salt + contrasena).\r\n"
        << "# El dispositivo se guarda como hash: identifica al equipo sin\r\n"
        << "# almacenar sus identificadores, y NO contiene ubicacion alguna.\r\n"
        << "email=" << rec.email << "\r\n"
        << "salt="  << rec.salt  << "\r\n"
        << "hash="  << rec.hash  << "\r\n";

    if (!rec.devHash.empty()) {
        out << "devsalt=" << rec.devSalt << "\r\n"
            << "devhash=" << rec.devHash << "\r\n"
            << "devkind=" << rec.devKind << "\r\n";
    }
    return out.good();
}

bool Account::exists(const std::wstring& path) {
    std::ifstream in(path.c_str());
    return in.good();
}

std::string Account::storedEmail(const std::wstring& path) {
    Record r;
    if (!readRecord(path, r)) return {};
    return r.email;
}

Account::DeviceInfo Account::deviceInfo(const std::wstring& path) {
    DeviceInfo info;
    Record r;
    if (!readRecord(path, r)) return info;
    if (r.devHash.empty() || r.devSalt.empty()) return info;

    info.registered = true;
    info.kindName   = r.devKind;
    // Solo se comparan hashes: del guardado no se puede volver al original.
    info.verified   = Device::matches(r.devSalt, r.devHash);
    return info;
}

bool Account::canAutoLogin(const std::wstring& path) {
    const DeviceInfo d = deviceInfo(path);
    return d.registered && d.verified;
}

bool Account::forgetDevice(const std::wstring& path, std::string& error) {
    Record r;
    if (!readRecord(path, r)) { error = "No hay ninguna cuenta creada."; return false; }

    r.devSalt.clear();
    r.devHash.clear();
    r.devKind.clear();

    if (!writeRecord(path, r)) { error = "No se pudo actualizar la cuenta."; return false; }
    return true;
}

bool Account::create(const std::wstring& path, const std::string& email,
                     const std::string& password, std::string& error) {
    if (exists(path)) {
        error = "Ya existe una cuenta en este equipo.";
        return false;
    }
    if (!validateEmail(email, error))    return false;
    if (!validatePassword(password, error)) return false;

    const std::string salt = randomSaltHex();
    if (salt.empty()) { error = "No se pudo generar el salt."; return false; }

    const std::string hash = sha256Hex(salt + password);
    if (hash.empty()) { error = "No se pudo cifrar la contrasena."; return false; }

    Record rec;
    rec.email = trim(email);
    rec.salt  = salt;
    rec.hash  = hash;

    // Vincular este equipo al registrarse: es lo que evita tener que volver a
    // introducir la contrasena. Se guarda el hash de la huella, nunca la huella.
    const std::string devSalt = randomSaltHex();
    const std::string devHash = devSalt.empty() ? std::string{}
                                                : Device::fingerprint(devSalt);
    if (!devHash.empty()) {
        rec.devSalt = devSalt;
        rec.devHash = devHash;
        rec.devKind = Device::kindName(Device::kind());
    }
    // Si la huella falla, la cuenta se crea igualmente: solo implica que la
    // proxima vez habra que escribir la contrasena.

    if (!writeRecord(path, rec)) { error = "No se pudo guardar la cuenta."; return false; }
    return true;
}

bool Account::verify(const std::wstring& path, const std::string& email,
                     const std::string& password, std::string& error) {
    Record rec;
    if (!readRecord(path, rec)) {
        error = exists(path) ? "El archivo de cuenta esta dañado."
                             : "No hay ninguna cuenta creada.";
        return false;
    }

    // Mensaje generico: no revelar si fallo el correo o la contrasena.
    const std::string calc = sha256Hex(rec.salt + password);
    if (trim(email) != rec.email || !constantTimeEquals(calc, rec.hash)) {
        error = "Correo o contrasena incorrectos.";
        return false;
    }

    // Credenciales correctas: si el equipo aun no estaba vinculado (o la
    // huella cambio), se vincula ahora. Asi el usuario solo escribe la
    // contrasena una vez por dispositivo.
    if (rec.devHash.empty() || !Device::matches(rec.devSalt, rec.devHash)) {
        const std::string devSalt = randomSaltHex();
        const std::string devHash = devSalt.empty() ? std::string{}
                                                    : Device::fingerprint(devSalt);
        if (!devHash.empty()) {
            rec.devSalt = devSalt;
            rec.devHash = devHash;
            rec.devKind = Device::kindName(Device::kind());
            writeRecord(path, rec);   // si falla, solo se pedira la clave otra vez
        }
    }
    return true;
}

Account::Found Account::searchSystem() {
    Found out;

    // Ubicaciones donde una cuenta de Ludora pudo quedar guardada. Se evita
    // recorrer todo el disco (seria lento e invasivo): solo las carpetas
    // habituales de datos de aplicacion y del usuario.
    std::vector<std::wstring> dirs;
    dirs.push_back(L".");   // carpeta de la app

    auto addEnv = [&](const wchar_t* var, const wchar_t* sub) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(var, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring p = buf;
            if (sub) p += sub;
            dirs.push_back(p);
        }
    };
    addEnv(L"APPDATA",      L"\\Ludora");
    addEnv(L"LOCALAPPDATA", L"\\Ludora");
    addEnv(L"USERPROFILE",  L"\\Documents\\Ludora");
    addEnv(L"USERPROFILE",  L"\\Desktop");
    addEnv(L"USERPROFILE",  nullptr);

    for (const std::wstring& dir : dirs) {
        const std::wstring path = dir + L"\\cuenta.dat";
        Record r;
        if (readRecord(path, r) && r.valid()) {
            out.ok    = true;
            out.path  = path;
            out.email = r.email;

            // Nombre de usuario del perfil, si esta junto a la cuenta.
            Record perfilDummy;
            std::ifstream pf((dir + L"\\perfil.dat").c_str(), std::ios::binary);
            if (pf) {
                std::string line;
                while (std::getline(pf, line)) {
                    const size_t eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    if (trim(line.substr(0, eq)) == "user")
                        out.userName = trim(line.substr(eq + 1));
                }
            }
            return out;   // la primera cuenta valida encontrada
        }
    }
    return out;
}

} // namespace ludora
