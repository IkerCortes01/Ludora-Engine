#include "app/Vault.h"
#include "app/Argon2.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace ludora {

namespace {

constexpr u32 kMagic   = 0x544D4E31;   // "TMN1": Tomate Nube
// v2 anade el estirado Argon2id sobre el secreto antes de las capas PBKDF2.
constexpr u32 kVersion = 2;

/// Cabecera del blob. Los salts van en claro a proposito: son publicos por
/// diseno, su papel es impedir tablas precalculadas, no ser secretos.
struct Header {
    u32 magic;
    u32 version;
    u32 layers;
    u32 iterations;
};

bool ntOk(NTSTATUS s) { return s >= 0; }

/// Envuelve un proveedor de algoritmo para no filtrarlo en los retornos.
struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};

/// Una pasada de AES-256-GCM. `encrypt` decide el sentido.
bool aesGcm(bool encrypt,
            const std::vector<u8>& key,
            const std::vector<u8>& nonce,
            const std::vector<u8>& in,
            std::vector<u8>& tag,
            std::vector<u8>& out) {
    AlgHandle alg;
    if (!ntOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;
    if (!ntOk(BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE,
                                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))
        return false;

    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (!ntOk(BCryptGenerateSymmetricKey(alg.h, &hKey, nullptr, 0,
                                         reinterpret_cast<PUCHAR>(const_cast<u8*>(key.data())),
                                         static_cast<ULONG>(key.size()), 0)))
        return false;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce   = const_cast<PUCHAR>(nonce.data());
    info.cbNonce   = static_cast<ULONG>(nonce.size());
    info.pbTag     = tag.data();
    info.cbTag     = static_cast<ULONG>(tag.size());

    ULONG produced = 0;
    bool ok = false;

    if (encrypt) {
        out.assign(in.size(), 0);
        ok = ntOk(BCryptEncrypt(hKey,
                                reinterpret_cast<PUCHAR>(const_cast<u8*>(in.data())),
                                static_cast<ULONG>(in.size()),
                                &info, nullptr, 0,
                                out.data(), static_cast<ULONG>(out.size()),
                                &produced, 0));
    } else {
        out.assign(in.size(), 0);
        // Si el tag no cuadra, BCryptDecrypt falla: asi se detecta cualquier
        // manipulacion del contenido antes de usarlo.
        ok = ntOk(BCryptDecrypt(hKey,
                                reinterpret_cast<PUCHAR>(const_cast<u8*>(in.data())),
                                static_cast<ULONG>(in.size()),
                                &info, nullptr, 0,
                                out.data(), static_cast<ULONG>(out.size()),
                                &produced, 0));
    }

    if (ok) out.resize(produced);
    BCryptDestroyKey(hKey);
    return ok;
}

void appendU32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>( x        & 0xFF));
    v.push_back(static_cast<u8>((x >> 8)  & 0xFF));
    v.push_back(static_cast<u8>((x >> 16) & 0xFF));
    v.push_back(static_cast<u8>((x >> 24) & 0xFF));
}

u32 readU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

} // namespace

bool Vault::randomBytes(std::vector<u8>& out, size_t n) {
    out.assign(n, 0);
    return ntOk(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(n),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

bool Vault::deriveKey(const std::string& secret,
                      const std::vector<u8>& salt,
                      u32 iterations,
                      std::vector<u8>& key) {
    AlgHandle alg;
    // SHA-512 con HMAC: el coste por iteracion es mayor que con SHA-256, lo
    // que encarece la fuerza bruta sin penalizar al usuario legitimo.
    if (!ntOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA512_ALGORITHM,
                                          nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return false;

    key.assign(kKeyBytes, 0);
    return ntOk(BCryptDeriveKeyPBKDF2(
        alg.h,
        reinterpret_cast<PUCHAR>(const_cast<char*>(secret.data())),
        static_cast<ULONG>(secret.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        iterations,
        key.data(), static_cast<ULONG>(key.size()), 0));
}

bool Vault::s_fastMode = false;

std::string Vault::stretchSecret(const std::string& secret,
                                 const std::vector<u8>& salt) {
    // Argon2id memoria-dura: se aplica UNA vez al secreto. Su salida alimenta
    // las 10 capas de PBKDF2. Asi el coste memoria-duro (lo que frena a una
    // GPU) se paga una sola vez por intento, no diez, y el usuario legitimo
    // solo espera una derivacion Argon2 al abrir.
    Argon2::Params p;
    if (s_fastMode) {
        // Solo el autodiagnostico activa esto: parametros ligeros para que la
        // bateria de pruebas termine rapido. La app real NUNCA lo toca.
        p.memKiB = 1024; p.iterations = 1;
    } else {
        p.memKiB = 65536;    // 64 MiB por intento: caro de paralelizar en ASIC
        p.iterations = 3;
    }
    p.parallelism = 1;
    p.outLen = 64;

    std::vector<u8> out;
    if (!Argon2::deriveKey(secret, salt, p, out)) return {};

    // Se devuelve como bytes crudos dentro de un std::string; solo se usa como
    // material de entrada a PBKDF2, no como texto.
    return std::string(reinterpret_cast<const char*>(out.data()), out.size());
}

std::string Vault::fingerprint(const std::string& secret,
                               const std::vector<u8>& salt) {
    std::vector<u8> k;
    // Menos iteraciones: esto solo comprueba coincidencia, no protege datos.
    if (!deriveKey(secret, salt, 20000, k)) return {};

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(k.size() * 2);
    for (u8 b : k) { out.push_back(kHex[(b >> 4) & 0xF]); out.push_back(kHex[b & 0xF]); }
    return out;
}

bool Vault::seal(const std::string& secret,
                 const std::vector<u8>& plain,
                 std::vector<u8>& out,
                 std::string& error) {
    if (secret.empty()) { error = "Falta el secreto de cifrado."; return false; }

    // Salt de Argon2, publico como los demas. Va tras la cabecera.
    std::vector<u8> argonSalt;
    if (!randomBytes(argonSalt, kSaltBytes)) {
        error = "No se pudo generar el salt del estirado.";
        return false;
    }

    // Estirado memoria-duro una sola vez. El resto de capas parten de aqui.
    const std::string stretched = stretchSecret(secret, argonSalt);
    if (stretched.empty()) { error = "Fallo en el estirado Argon2."; return false; }

    out.clear();
    appendU32(out, kMagic);
    appendU32(out, kVersion);
    appendU32(out, static_cast<u32>(kLayers));
    appendU32(out, kIterations);
    out.insert(out.end(), argonSalt.begin(), argonSalt.end());

    std::vector<u8> datos = plain;

    // Cada capa: salt propio -> clave propia -> nonce propio -> AES-GCM.
    // Los salts y nonces se guardan en la cabecera de cada capa; el secreto
    // nunca se escribe.
    for (i32 capa = 0; capa < kLayers; ++capa) {
        std::vector<u8> salt, nonce;
        if (!randomBytes(salt, kSaltBytes) || !randomBytes(nonce, kNonceBytes)) {
            error = "No se pudo generar aleatoriedad para la capa.";
            return false;
        }

        // El indice de capa entra en la derivacion: dos capas con el mismo
        // salt seguirian dando claves distintas. Se parte del secreto ya
        // estirado con Argon2, no del original.
        std::string material = stretched;
        material += "|capa:";
        material += static_cast<char>('0' + capa);

        std::vector<u8> key;
        if (!deriveKey(material, salt, kIterations, key)) {
            error = "Fallo al derivar la clave de la capa.";
            return false;
        }

        std::vector<u8> tag(kTagBytes, 0), cifrado;
        if (!aesGcm(true, key, nonce, datos, tag, cifrado)) {
            error = "Fallo al cifrar la capa.";
            return false;
        }

        // Limpiar la clave de memoria en cuanto deja de hacer falta.
        SecureZeroMemory(key.data(), key.size());

        out.insert(out.end(), salt.begin(),  salt.end());
        out.insert(out.end(), nonce.begin(), nonce.end());
        out.insert(out.end(), tag.begin(),   tag.end());
        appendU32(out, static_cast<u32>(cifrado.size()));

        datos.swap(cifrado);
    }

    out.insert(out.end(), datos.begin(), datos.end());
    return true;
}

bool Vault::open(const std::string& secret,
                 const std::vector<u8>& blob,
                 std::vector<u8>& out,
                 std::string& error) {
    // 16 de cabecera + salt de Argon2 que va justo despues.
    constexpr size_t kHeadBytes  = 16;
    const     size_t kHeadTotal  = kHeadBytes + kSaltBytes;
    constexpr size_t kLayerBytes = kSaltBytes + kNonceBytes + kTagBytes + 4;

    if (blob.size() < kHeadTotal) { error = "Archivo cifrado incompleto."; return false; }
    if (readU32(&blob[0]) != kMagic) { error = "Formato no reconocido."; return false; }

    const u32 version = readU32(&blob[4]);
    const u32 layers  = readU32(&blob[8]);
    const u32 iters   = readU32(&blob[12]);
    if (version != kVersion) { error = "Version del almacen no compatible."; return false; }
    if (layers == 0 || layers > 64) { error = "Numero de capas invalido."; return false; }

    if (blob.size() < kHeadTotal + kLayerBytes * layers) {
        error = "Archivo cifrado incompleto.";
        return false;
    }

    // Salt de Argon2 y estirado del secreto, igual que en seal().
    const std::vector<u8> argonSalt(blob.begin() + kHeadBytes,
                                    blob.begin() + kHeadBytes + kSaltBytes);
    const std::string stretched = stretchSecret(secret, argonSalt);
    if (stretched.empty()) { error = "Fallo en el estirado Argon2."; return false; }

    // Las capas se leen en orden y se deshacen en orden inverso.
    struct Capa { std::vector<u8> salt, nonce, tag; u32 size; };
    std::vector<Capa> capas(layers);

    size_t p = kHeadTotal;
    for (u32 i = 0; i < layers; ++i) {
        capas[i].salt.assign (blob.begin() + p, blob.begin() + p + kSaltBytes);  p += kSaltBytes;
        capas[i].nonce.assign(blob.begin() + p, blob.begin() + p + kNonceBytes); p += kNonceBytes;
        capas[i].tag.assign  (blob.begin() + p, blob.begin() + p + kTagBytes);   p += kTagBytes;
        capas[i].size = readU32(&blob[p]);                                       p += 4;
    }

    std::vector<u8> datos(blob.begin() + p, blob.end());

    for (i32 i = static_cast<i32>(layers) - 1; i >= 0; --i) {
        std::string material = stretched;
        material += "|capa:";
        material += static_cast<char>('0' + i);

        std::vector<u8> key;
        if (!deriveKey(material, capas[i].salt, iters, key)) {
            error = "Fallo al derivar la clave de la capa.";
            return false;
        }

        std::vector<u8> descifrado;
        const bool ok = aesGcm(false, key, capas[i].nonce, datos, capas[i].tag, descifrado);
        SecureZeroMemory(key.data(), key.size());

        if (!ok) {
            // Mensaje unico para clave incorrecta y contenido manipulado:
            // distinguirlos le diria a un atacante en que punto ha fallado.
            error = "No se pudo abrir el almacen: clave incorrecta o datos alterados.";
            return false;
        }
        datos.swap(descifrado);
    }

    out.swap(datos);
    return true;
}

bool Vault::saveFile(const std::wstring& path, const std::vector<u8>& blob) {
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(blob.data()),
            static_cast<std::streamsize>(blob.size()));
    return f.good();
}

bool Vault::loadFile(const std::wstring& path, std::vector<u8>& blob) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    blob.assign(static_cast<size_t>(n), 0);
    f.read(reinterpret_cast<char*>(blob.data()), n);
    return f.good();
}

} // namespace ludora
