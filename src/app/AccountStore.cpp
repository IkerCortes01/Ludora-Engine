#include "app/AccountStore.h"
#include "app/Device.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace ludora {

namespace {

constexpr size_t kMinPassword = 6;

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

std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    DWORD hashLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &cb, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    std::vector<u8> digest(hashLen);
    std::string out;
    if (BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), 0) >= 0 &&
        BCryptFinishHash(h, digest.data(), hashLen, 0) >= 0) {
        out = toHex(digest.data(), digest.size());
    }

    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::string randomSaltHex(size_t bytes = 16) {
    std::vector<u8> buf(bytes);
    if (BCryptGenRandom(nullptr, buf.data(), static_cast<ULONG>(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return {};
    return toHex(buf.data(), buf.size());
}

/// No revela por donde difieren dos hashes.
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

std::string toLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

long long nowSeconds() {
    return static_cast<long long>(std::time(nullptr));
}

/// Trocea "a|b|c" respetando los campos vacios.
std::vector<std::string> splitPipe(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t bar = s.find('|', start);
        if (bar == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, bar - start));
        start = bar + 1;
    }
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

/// Carpeta donde vive el ejecutable.
std::wstring exeDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".";
    std::wstring p = buf;
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}

/// Prueba de escritura REAL: crea un archivo y lo borra.
///
/// No basta con mirar la ruta. "Archivos de programa" es el caso obvio, pero
/// tambien hay unidades de red y USB protegidos contra escritura, y ninguno se
/// distingue por el nombre de la carpeta.
bool dirIsWritable(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);   // si ya existe, no pasa nada

    const std::wstring probe = dir + L"\\.ludora-escritura";
    const HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                 nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);   // FILE_FLAG_DELETE_ON_CLOSE lo borra solo
    return true;
}

/// Crea una ruta completa, nivel a nivel.
void ensureDir(const std::wstring& dir) {
    if (dir.empty()) return;
    std::wstring acc;
    size_t start = 0;
    while (start <= dir.size()) {
        const size_t slash = dir.find_first_of(L"\\/", start);
        acc = (slash == std::wstring::npos) ? dir : dir.substr(0, slash);
        if (!acc.empty() && !(acc.size() == 2 && acc[1] == L':'))
            CreateDirectoryW(acc.c_str(), nullptr);
        if (slash == std::wstring::npos) break;
        start = slash + 1;
    }
}

/// Raiz forzada por las pruebas. Vacia = deteccion normal.
std::wstring g_testRoot;

} // namespace

// ============================================================================
// UBICACION
// ============================================================================

void AccountStore::setDataRootForTesting(const std::wstring& root) {
    g_testRoot = root;
}

std::wstring AccountStore::dataRoot() {
    if (!g_testRoot.empty()) {
        ensureDir(g_testRoot);
        return g_testRoot;
    }

    // Se resuelve una vez por ejecucion: la escribibilidad de una carpeta no
    // cambia a mitad de sesion, y la prueba toca el disco.
    static std::wstring cached;
    if (!cached.empty()) return cached;

    const std::wstring exe = exeDir();
    if (dirIsWritable(exe)) {
        cached = exe;                     // modo portatil: el USB manda
        return cached;
    }

    // Reserva: %LOCALAPPDATA%\Ludora, que siempre es escribible.
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring p = std::wstring(buf) + L"\\Ludora";
        ensureDir(p);
        if (dirIsWritable(p)) { cached = p; return cached; }
    }

    cached = exe;   // sin alternativa mejor
    return cached;
}

bool AccountStore::isPortable() {
    return dataRoot() == exeDir();
}

std::wstring AccountStore::accountsDir() {
    const std::wstring dir = dataRoot() + L"\\cuentas";
    ensureDir(dir);
    return dir;
}

std::string AccountStore::idForEmail(const std::string& email) {
    // Solo caracteres seguros para un nombre de archivo. Todo lo demas se
    // sustituye por '_'.
    //
    // Esto es tambien la defensa contra el paso de directorio: un correo con
    // "..\\" o "/" no puede escribir fuera de `cuentas/`, porque esos
    // caracteres no sobreviven a este filtro. La validacion del correo ya lo
    // impide antes, pero el almacen no depende de que el llamador validara.
    const std::string low = toLower(trim(email));
    std::string id;
    id.reserve(low.size());
    for (char c : low) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '-' || c == '_';
        id.push_back(ok ? c : '_');
    }
    if (id.empty()) id = "cuenta";

    // El filtro de arriba ya impide salir de la carpeta: '\' y '/' no
    // sobreviven. Pero el punto SI esta permitido (los correos lo llevan), y
    // eso deja pasar el caso especial de Windows: "." y ".." no son nombres,
    // son el directorio actual y el padre. Un correo como "..\\..\\x@dominio"
    // produce "..____x_dominio", que es inofensivo, pero uno cuyo id quede
    // reducido a puntos apuntaria a una carpeta en vez de a un archivo.
    //
    // Se resuelve de tres formas a la vez:
    //   - dos o mas puntos seguidos se colapsan en uno (".." nunca aparece),
    //   - se quitan los puntos del principio, que ademas es lo que oculta un
    //     archivo en otros sistemas,
    //   - un id que fuera solo puntos pasa a ser "cuenta".
    std::string limpio;
    limpio.reserve(id.size());
    for (const char c : id) {
        if (c == '.' && !limpio.empty() && limpio.back() == '.') continue;
        limpio.push_back(c);
    }
    const size_t primerUtil = limpio.find_first_not_of('.');
    if (primerUtil == std::string::npos) id = "cuenta";
    else                                 id = limpio.substr(primerUtil);

    // Un nombre muy largo puede pasarse del limite de ruta de Windows. Se
    // recorta y se le pega un hash corto del correo completo, para que dos
    // correos largos que empiecen igual no colisionen.
    constexpr size_t kMaxId = 48;
    if (id.size() > kMaxId) {
        const std::string h = sha256Hex(low);
        id = id.substr(0, kMaxId) + "-" + h.substr(0, 8);
    }
    return id;
}

std::wstring AccountStore::pathForEmail(const std::string& email) {
    return accountsDir() + L"\\" + widen(idForEmail(email)) + L".cuenta";
}

std::wstring AccountStore::profilePathForEmail(const std::string& email) {
    return accountsDir() + L"\\" + widen(idForEmail(email)) + L".perfil";
}

// ============================================================================
// LECTURA Y ESCRITURA
// ============================================================================

bool AccountStore::readRecord(const std::wstring& path, Record& out) {
    std::ifstream in(path.c_str());
    if (!in) return false;

    out = Record{};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if      (k == "email") out.email = v;
        else if (k == "salt")  out.salt  = v;
        else if (k == "hash")  out.hash  = v;
        else if (k == "dev") {
            // salt|hash|tipo|alta|ultimo
            const std::vector<std::string> f = splitPipe(v);
            if (f.size() >= 2 && !f[0].empty() && !f[1].empty()) {
                DeviceEntry d;
                d.salt     = f[0];
                d.hash     = f[1];
                d.kindName = (f.size() > 2) ? f[2] : std::string{};
                // Fechas ausentes o ilegibles quedan en 0: se muestra "-" en
                // la interfaz, pero el dispositivo sigue siendo valido. Una
                // fecha rota no puede costarle el acceso a nadie.
                if (f.size() > 3) { try { d.addedAt  = std::stoll(f[3]); } catch (...) {} }
                if (f.size() > 4) { try { d.lastSeen = std::stoll(f[4]); } catch (...) {} }
                out.devices.push_back(d);
            }
        }
    }
    return out.valid();
}

bool AccountStore::writeRecord(const std::wstring& path, const Record& rec) {
    // Escritura atomica: primero a un temporal, luego reemplazo. Un corte a
    // mitad deja intacto el archivo anterior en vez de una cuenta a medias,
    // que dejaria al usuario sin poder entrar.
    const std::wstring tmp = path + L".tmp";

    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << "# Cuenta de Ludora Engine.\r\n"
            << "# La contrasena NO se guarda: solo SHA-256(salt + contrasena).\r\n"
            << "# Cada 'dev' es un equipo verificado, guardado como hash: dice\r\n"
            << "# 'es el mismo equipo, si o no' y nada mas. Sin ubicacion, sin\r\n"
            << "# nombre de equipo, sin numeros de serie en claro.\r\n"
            << "email=" << rec.email << "\r\n"
            << "salt="  << rec.salt  << "\r\n"
            << "hash="  << rec.hash  << "\r\n";

        for (const DeviceEntry& d : rec.devices) {
            out << "dev=" << d.salt << "|" << d.hash << "|" << d.kindName
                << "|" << d.addedAt << "|" << d.lastSeen << "\r\n";
        }

        out.flush();
        if (!out.good()) { out.close(); DeleteFileW(tmp.c_str()); return false; }
    }

    // MOVEFILE_REPLACE_EXISTING + WRITE_THROUGH: el reemplazo se confirma en
    // disco antes de volver.
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ============================================================================
// CUENTAS
// ============================================================================

bool AccountStore::exists(const std::string& email) {
    Record r;
    return readRecord(pathForEmail(email), r);
}

size_t AccountStore::count() {
    return list().size();
}

std::vector<AccountStore::Summary> AccountStore::list() {
    std::vector<Summary> out;

    const std::wstring pattern = accountsDir() + L"\\*.cuenta";
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        const std::wstring full = accountsDir() + L"\\" + fd.cFileName;
        Record r;
        if (!readRecord(full, r)) continue;   // se ignora lo ilegible

        Summary s;
        s.email       = r.email;
        s.deviceCount = static_cast<int>(r.devices.size());
        for (const DeviceEntry& d : r.devices) {
            if (Device::matches(d.salt, d.hash)) { s.deviceVerified = true; break; }
        }

        // Nombre de usuario del perfil, si lo hay.
        std::ifstream pf(profilePathForEmail(r.email).c_str(), std::ios::binary);
        if (pf) {
            std::string line;
            while (std::getline(pf, line)) {
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                if (trim(line.substr(0, eq)) == "user")
                    s.userName = trim(line.substr(eq + 1));
            }
        }
        out.push_back(s);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(out.begin(), out.end(), [](const Summary& a, const Summary& b) {
        return a.email < b.email;
    });
    return out;
}

bool AccountStore::create(const std::string& email, const std::string& password,
                          std::string& error) {
    const std::string mail = trim(email);

    // Solo choca consigo misma: tener OTRAS cuentas no impide crear esta.
    if (exists(mail)) {
        error = "Ya existe una cuenta con ese correo.";
        return false;
    }
    if (password.size() < kMinPassword) {
        error = "La contrasena necesita al menos 6 caracteres.";
        return false;
    }

    const std::string salt = randomSaltHex();
    if (salt.empty()) { error = "No se pudo generar el salt."; return false; }

    const std::string hash = sha256Hex(salt + password);
    if (hash.empty()) { error = "No se pudo cifrar la contrasena."; return false; }

    Record rec;
    rec.email = mail;
    rec.salt  = salt;
    rec.hash  = hash;

    // Vincular este equipo al registrarse evita pedir la clave la proxima vez.
    const std::string devSalt = randomSaltHex();
    if (!devSalt.empty()) {
        const std::string devHash = Device::fingerprint(devSalt);
        if (!devHash.empty()) {
            DeviceEntry d;
            d.salt     = devSalt;
            d.hash     = devHash;
            d.kindName = Device::kindName(Device::kind());
            d.addedAt  = nowSeconds();
            d.lastSeen = d.addedAt;
            rec.devices.push_back(d);
        }
    }
    // Si la huella falla, la cuenta se crea igual: solo implica escribir la
    // contrasena la proxima vez.

    if (!writeRecord(pathForEmail(mail), rec)) {
        error = "No se pudo guardar la cuenta.";
        return false;
    }
    return true;
}

bool AccountStore::verify(const std::string& email, const std::string& password,
                          std::string& error) {
    const std::wstring path = pathForEmail(email);

    Record rec;
    if (!readRecord(path, rec)) {
        error = "No hay ninguna cuenta con ese correo.";
        return false;
    }

    // Mensaje generico: no distingue correo malo de contrasena mala.
    const std::string calc = sha256Hex(rec.salt + password);
    if (toLower(trim(email)) != toLower(rec.email) ||
        !constantTimeEquals(calc, rec.hash)) {
        error = "Correo o contrasena incorrectos.";
        return false;
    }

    // Credenciales correctas. Si este equipo no estaba, se ANADE.
    //
    // Aqui esta la diferencia con el sistema de una sola cuenta: alli el nuevo
    // dispositivo SUSTITUIA al anterior, asi que alternar entre dos equipos
    // obligaba a teclear la clave cada vez. Ahora conviven.
    bool found = false;
    const long long now = nowSeconds();
    for (DeviceEntry& d : rec.devices) {
        if (Device::matches(d.salt, d.hash)) { d.lastSeen = now; found = true; break; }
    }

    if (!found) {
        const std::string devSalt = randomSaltHex();
        if (!devSalt.empty()) {
            const std::string devHash = Device::fingerprint(devSalt);
            if (!devHash.empty()) {
                DeviceEntry d;
                d.salt     = devSalt;
                d.hash     = devHash;
                d.kindName = Device::kindName(Device::kind());
                d.addedAt  = now;
                d.lastSeen = now;
                rec.devices.push_back(d);
            }
        }
    }

    // Si no se puede escribir, se entra igual: solo se volvera a pedir la
    // clave. Un disco lleno no debe dejar a nadie fuera de su cuenta.
    writeRecord(path, rec);
    return true;
}

bool AccountStore::remove(const std::string& email, std::string& error) {
    const std::wstring path = pathForEmail(email);
    if (!DeleteFileW(path.c_str())) {
        error = "No se pudo borrar la cuenta.";
        return false;
    }
    DeleteFileW(profilePathForEmail(email).c_str());   // puede no existir
    return true;
}

// ============================================================================
// DISPOSITIVOS
// ============================================================================

bool AccountStore::deviceVerified(const std::string& email) {
    Record r;
    if (!readRecord(pathForEmail(email), r)) return false;
    for (const DeviceEntry& d : r.devices)
        if (Device::matches(d.salt, d.hash)) return true;
    return false;
}

std::vector<AccountStore::DeviceView> AccountStore::devices(const std::string& email) {
    std::vector<DeviceView> out;
    Record r;
    if (!readRecord(pathForEmail(email), r)) return out;

    for (const DeviceEntry& d : r.devices) {
        DeviceView v;
        v.kindName     = d.kindName;
        v.addedAt      = d.addedAt;
        v.lastSeen     = d.lastSeen;
        v.isThisDevice = Device::matches(d.salt, d.hash);
        out.push_back(v);
    }
    return out;
}

bool AccountStore::forgetThisDevice(const std::string& email, std::string& error) {
    const std::wstring path = pathForEmail(email);
    Record r;
    if (!readRecord(path, r)) { error = "No hay ninguna cuenta con ese correo."; return false; }

    const size_t before = r.devices.size();
    r.devices.erase(
        std::remove_if(r.devices.begin(), r.devices.end(),
                       [](const DeviceEntry& d) { return Device::matches(d.salt, d.hash); }),
        r.devices.end());

    if (r.devices.size() == before) {
        error = "Este equipo no estaba vinculado.";
        return false;
    }
    if (!writeRecord(path, r)) { error = "No se pudo actualizar la cuenta."; return false; }
    return true;
}

bool AccountStore::forgetAllDevices(const std::string& email, std::string& error) {
    const std::wstring path = pathForEmail(email);
    Record r;
    if (!readRecord(path, r)) { error = "No hay ninguna cuenta con ese correo."; return false; }

    r.devices.clear();
    if (!writeRecord(path, r)) { error = "No se pudo actualizar la cuenta."; return false; }
    return true;
}

// ============================================================================
// INICIO AUTOMATICO
// ============================================================================

std::vector<std::string> AccountStore::autoLoginCandidates() {
    std::vector<std::string> out;
    for (const Summary& s : list())
        if (s.deviceVerified) out.push_back(s.email);
    return out;
}

std::string AccountStore::soleAutoLoginEmail() {
    const std::vector<std::string> c = autoLoginCandidates();
    return (c.size() == 1) ? c[0] : std::string{};
}

void AccountStore::touch(const std::string& email) {
    const std::wstring path = pathForEmail(email);
    Record r;
    if (!readRecord(path, r)) return;

    const long long now = nowSeconds();
    bool changed = false;
    for (DeviceEntry& d : r.devices) {
        if (Device::matches(d.salt, d.hash)) { d.lastSeen = now; changed = true; break; }
    }
    if (changed) writeRecord(path, r);
}

// ============================================================================
// MIGRACION
// ============================================================================

bool AccountStore::migrateLegacy(const std::wstring& legacyPath,
                                 const std::wstring& legacyProfilePath) {
    // Formato viejo: mismas claves, pero devsalt/devhash/devkind sueltos.
    std::ifstream in(legacyPath.c_str());
    if (!in) return false;

    Record rec;
    std::string devSalt, devHash, devKind;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if      (k == "email")   rec.email = v;
        else if (k == "salt")    rec.salt  = v;
        else if (k == "hash")    rec.hash  = v;
        else if (k == "devsalt") devSalt   = v;
        else if (k == "devhash") devHash   = v;
        else if (k == "devkind") devKind   = v;
    }
    in.close();

    if (!rec.valid()) return false;

    // Idempotente: si ya existe en el almacen nuevo, no se pisa. Importa
    // porque el arranque llama a esto SIEMPRE, y la cuenta migrada pudo
    // ganar dispositivos despues que no estan en el archivo viejo.
    if (exists(rec.email)) return true;

    if (!devSalt.empty() && !devHash.empty()) {
        DeviceEntry d;
        d.salt     = devSalt;
        d.hash     = devHash;
        d.kindName = devKind.empty() ? std::string("Desconocido") : devKind;
        d.addedAt  = nowSeconds();
        d.lastSeen = d.addedAt;
        rec.devices.push_back(d);
    }

    if (!writeRecord(pathForEmail(rec.email), rec)) return false;

    // El perfil viaja con la cuenta, si lo hay.
    if (!legacyProfilePath.empty()) {
        std::ifstream pf(legacyProfilePath.c_str(), std::ios::binary);
        if (pf) {
            std::ostringstream ss;
            ss << pf.rdbuf();
            pf.close();
            const std::string data = ss.str();
            if (!data.empty()) {
                std::ofstream out(profilePathForEmail(rec.email).c_str(), std::ios::binary);
                if (out) out.write(data.data(), static_cast<std::streamsize>(data.size()));
            }
        }
    }

    // El archivo viejo se conserva a proposito (ver la cabecera).
    return true;
}

} // namespace ludora
