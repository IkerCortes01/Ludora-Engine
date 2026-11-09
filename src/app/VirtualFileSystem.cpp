#include "app/VirtualFileSystem.h"
#include "app/Vault.h"
#include "app/Device.h"
#include <cstring>
#include <fstream>
#include <windows.h>

namespace ludora {

// ---------------------------------------------------------------- utilidades ---
namespace {

constexpr u32 kMagic   = 0x4B41504C;   // "LPAK"
constexpr u32 kVersion = 1;
constexpr u32 kFlagXor = 0x1;          // flags bit0: payload ofuscado con XOR

constexpr size_t kHeaderSize = 40;     // ver diagrama en el .h
constexpr size_t kNonceBytes = 12;     // AES-GCM
constexpr size_t kTagBytes   = 16;
constexpr size_t kSaltBytes  = 8;      // master salt

// El secreto del que sale la clave maestra: la huella del equipo, con un
// dominio propio para no colisionar con otros usos de fingerprint.
const char* const kMasterDomain = "ludora-vfs-master-v1";

// --- lectura/escritura de enteros little-endian sobre un vector de bytes ---
void putU16(std::vector<u8>& v, u16 x) {
    v.push_back(u8(x & 0xFF)); v.push_back(u8((x >> 8) & 0xFF));
}
void putU32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(u8((x >> (8 * i)) & 0xFF));
}
void putU64(std::vector<u8>& v, u64 x) {
    for (int i = 0; i < 8; ++i) v.push_back(u8((x >> (8 * i)) & 0xFF));
}
u16 getU16(const u8* p) { return u16(p[0]) | (u16(p[1]) << 8); }
u32 getU32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
u64 getU64(const u8* p) {
    u64 r = 0;
    for (int i = 0; i < 8; ++i) r |= u64(p[i]) << (8 * i);
    return r;
}

// --- Compresion LZNT1 nativa (ntdll), sin dependencias externas. -----------
// Formato de bloque: [origU32][datos]. origU32==dataLen -> guardado crudo.
typedef LONG (WINAPI *GetWs_t)(USHORT, PULONG, PULONG);
typedef LONG (WINAPI *Comp_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);
typedef LONG (WINAPI *Decomp_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

bool compress(const std::vector<u8>& in, std::vector<u8>& out) {
    out.clear();
    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto getWs = reinterpret_cast<GetWs_t>(nt ? GetProcAddress(nt, "RtlGetCompressionWorkSpaceSize") : nullptr);
    auto comp  = reinterpret_cast<Comp_t>(nt ? GetProcAddress(nt, "RtlCompressBuffer") : nullptr);
    const USHORT fmt = 2 /*LZNT1*/ | 0x0100 /*MAXIMUM*/;

    auto crudo = [&]() {
        putU32(out, static_cast<u32>(in.size()));   // orig == dataLen -> crudo
        out.insert(out.end(), in.begin(), in.end());
        return true;
    };
    if (!getWs || !comp || in.empty()) return crudo();

    ULONG ws = 0, frag = 0;
    if (getWs(fmt, &ws, &frag) < 0) return crudo();
    std::vector<u8> work(ws), dst(in.size());
    ULONG produced = 0;
    const LONG r = comp(fmt, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()),
                        dst.data(), static_cast<ULONG>(dst.size()), frag, &produced, work.data());
    if (r < 0 || produced == 0 || produced >= in.size()) return crudo();

    putU32(out, static_cast<u32>(in.size()));       // tamano original
    out.insert(out.end(), dst.begin(), dst.begin() + produced);
    return true;
}

bool decompress(const std::vector<u8>& in, std::vector<u8>& out) {
    if (in.size() < 4) return false;
    const u32 orig = getU32(in.data());
    const u8* data = in.data() + 4;
    const size_t dataLen = in.size() - 4;

    if (orig == dataLen) { out.assign(data, data + dataLen); return true; } // crudo

    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto dec = reinterpret_cast<Decomp_t>(nt ? GetProcAddress(nt, "RtlDecompressBufferEx") : nullptr);
    if (!dec) return false;
    out.assign(orig, 0);
    std::vector<u8> work(1 << 16);
    ULONG produced = 0;
    const LONG r = dec(2 /*LZNT1*/, out.data(), orig,
                       const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen), &produced, work.data());
    if (r < 0) return false;
    out.resize(produced);
    return produced == orig;
}

// --- XOR keystream (SOLO ofuscacion, NO seguridad). Modo demo/alternativo. --
// Se deriva un keystream de la clave + nonce con un contador simple. Un atacante
// con la clave del equipo lo rompe; sirve para ilustrar el pipeline sin cripto.
void xorStream(const std::vector<u8>& key, const std::vector<u8>& nonce,
               std::vector<u8>& buf) {
    for (size_t i = 0; i < buf.size(); ++i) {
        const u8 k = key[i % key.size()];
        const u8 n = nonce[i % nonce.size()];
        buf[i] ^= static_cast<u8>(k ^ (n + static_cast<u8>(i * 131u + 17u)));
    }
}

} // namespace

// ------------------------------------------------------------- AssetBuffer ---
void AssetBuffer::wipe() {
    if (!m_bytes.empty())
        SecureZeroMemory(m_bytes.data(), m_bytes.size());
    m_bytes.clear();
    m_bytes.shrink_to_fit();
}

// -------------------------------------------------- VirtualFileSystem: montar ---
bool VirtualFileSystem::deriveMasterKey(const std::vector<u8>& salt, std::string& error) {
    // Secreto = huella del equipo. La clave maestra sale de estirarla con
    // Argon2id (memoria-dura, una sola vez) y luego PBKDF2, igual que el Vault.
    const std::string secret = Device::fingerprint(kMasterDomain);
    if (secret.empty()) { error = "No se pudo obtener la huella del equipo."; return false; }

    const std::string stretched = Vault::stretchSecret(secret, salt);
    if (stretched.empty()) { error = "No se pudo derivar la clave maestra."; return false; }

    if (!Vault::deriveKey(stretched, salt, Vault::kIterations, m_masterKey) ||
        m_masterKey.size() != static_cast<size_t>(Vault::kKeyBytes)) {
        error = "No se pudo derivar la clave maestra del VFS.";
        return false;
    }
    return true;
}

bool VirtualFileSystem::mount(const std::wstring& pakPath, std::string& error) {
    m_index.clear();
    m_pakPath = pakPath;

    std::ifstream f(pakPath.c_str(), std::ios::binary);
    if (!f) { error = "No se pudo abrir el contenedor .pak."; return false; }

    // 1) Cabecera fija.
    u8 hdr[kHeaderSize];
    f.read(reinterpret_cast<char*>(hdr), kHeaderSize);
    if (!f || getU32(hdr) != kMagic) { error = "El archivo no es un .pak valido."; return false; }
    if (getU32(hdr + 4) != kVersion) { error = "Version de .pak no soportada."; return false; }

    const u32 flags      = getU32(hdr + 8);
    const u32 assetCount = getU32(hdr + 12);
    const u64 tocOffset  = getU64(hdr + 16);
    const u64 tocSize    = getU64(hdr + 24);
    m_xor = (flags & kFlagXor) != 0;

    std::vector<u8> salt(hdr + 32, hdr + 32 + kSaltBytes);

    // 2) Clave maestra (coste memoria-duro pagado UNA vez al montar).
    if (!deriveMasterKey(salt, error)) return false;

    // 3) Tabla de contenidos completa a RAM, indexada por ruta -> O(1).
    std::vector<u8> toc(static_cast<size_t>(tocSize));
    f.seekg(static_cast<std::streamoff>(tocOffset));
    if (tocSize) f.read(reinterpret_cast<char*>(toc.data()), static_cast<std::streamsize>(tocSize));
    if (!f && tocSize) { error = "No se pudo leer la tabla de contenidos."; return false; }

    size_t p = 0;
    for (u32 i = 0; i < assetCount; ++i) {
        if (p + 2 > toc.size()) { error = "TOC corrupta (ruta)."; return false; }
        const u16 pathLen = getU16(&toc[p]); p += 2;
        if (p + pathLen + 24 > toc.size()) { error = "TOC corrupta (entrada)."; return false; }

        Entry e;
        e.path.assign(reinterpret_cast<const char*>(&toc[p]), pathLen); p += pathLen;
        e.offset  = getU64(&toc[p]); p += 8;
        e.encSize = getU64(&toc[p]); p += 8;
        e.rawSize = getU64(&toc[p]); p += 8;
        m_index.emplace(e.path, std::move(e));
    }
    return true;
}

bool VirtualFileSystem::contains(const std::string& logicalPath) const {
    return m_index.find(logicalPath) != m_index.end();
}

// -------------------------------------------- VirtualFileSystem: descifrar ---
bool VirtualFileSystem::decryptBlock(const std::vector<u8>& block, u64 rawSize,
                                     std::vector<u8>& out, std::string& error) const {
    // El bloque en disco es: nonce(12) | tag(16) | payload cifrado(comprimido).
    if (block.size() < kNonceBytes + kTagBytes) { error = "Bloque de asset truncado."; return false; }

    std::vector<u8> nonce(block.begin(), block.begin() + kNonceBytes);
    std::vector<u8> tag(block.begin() + kNonceBytes, block.begin() + kNonceBytes + kTagBytes);
    std::vector<u8> payload(block.begin() + kNonceBytes + kTagBytes, block.end());

    std::vector<u8> comprimido;
    if (m_xor) {
        // Modo ofuscacion: deshacer el XOR (el tag no se usa como AEAD).
        comprimido = payload;
        xorStream(m_masterKey, nonce, comprimido);
    } else {
        // Modo real AES-256-GCM: si el tag no cuadra, aead() FALLA -> se detecta
        // cualquier manipulacion del asset antes de descomprimirlo/usarlo.
        if (!Vault::aead(false, m_masterKey, nonce, payload, tag, comprimido)) {
            error = "Asset alterado o clave incorrecta (fallo el tag GCM).";
            return false;
        }
    }

    // Descomprimir para recuperar los bytes originales del asset.
    if (!decompress(comprimido, out)) { error = "No se pudo descomprimir el asset."; return false; }
    if (out.size() != rawSize)        { error = "El asset descifrado no tiene el tamano esperado."; return false; }
    return true;
}

bool VirtualFileSystem::read(const std::string& logicalPath, AssetBuffer& out,
                             std::string& error) const {
    const auto it = m_index.find(logicalPath);
    if (it == m_index.end()) { error = "Asset no encontrado: " + logicalPath; return false; }
    const Entry& e = it->second;

    // Leer SOLO el bloque de este asset del disco (no todo el .pak).
    std::ifstream f(m_pakPath.c_str(), std::ios::binary);
    if (!f) { error = "No se pudo abrir el contenedor para leer el asset."; return false; }
    std::vector<u8> block(static_cast<size_t>(e.encSize));
    f.seekg(static_cast<std::streamoff>(e.offset));
    if (e.encSize) f.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(e.encSize));
    if (!f && e.encSize) { error = "No se pudo leer el bloque del asset."; return false; }

    std::vector<u8> plano;
    if (!decryptBlock(block, e.rawSize, plano, error)) return false;

    // Los bytes descifrados salen envueltos en AssetBuffer: se borran solos.
    out = AssetBuffer(std::move(plano));
    return true;
}

// ---------------------------------------------- VirtualFileSystem: construir ---
bool VirtualFileSystem::build(const std::wstring& pakPath,
                              const std::vector<SourceAsset>& assets,
                              bool useXorInstead,
                              std::string& error) {
    // 1) Salt y clave maestra del equipo (una vez para todo el .pak).
    std::vector<u8> salt;
    if (!Vault::randomBytes(salt, kSaltBytes)) { error = "No hay entropia para el salt."; return false; }

    VirtualFileSystem vfs;   // reutiliza deriveMasterKey
    vfs.m_xor = useXorInstead;
    if (!vfs.deriveMasterKey(salt, error)) return false;

    // 2) Cifrar cada asset -> bloque [nonce|tag|payload]. Acumular TOC.
    std::vector<u8> body;   // concatenacion de bloques
    std::vector<Entry> toc;
    toc.reserve(assets.size());

    for (const auto& a : assets) {
        std::vector<u8> comprimido;
        if (!compress(a.bytes, comprimido)) { error = "No se pudo comprimir un asset."; return false; }

        std::vector<u8> nonce;
        if (!Vault::randomBytes(nonce, kNonceBytes)) { error = "No hay entropia para el nonce."; return false; }

        std::vector<u8> payload, tag(kTagBytes, 0);
        if (useXorInstead) {
            payload = comprimido;
            xorStream(vfs.m_masterKey, nonce, payload);   // solo ofuscacion
        } else {
            if (!Vault::aead(true, vfs.m_masterKey, nonce, comprimido, tag, payload)) {
                error = "No se pudo cifrar un asset."; return false;
            }
        }

        Entry e;
        e.path    = a.logicalPath;
        e.offset  = kHeaderSize + body.size();   // offset provisional; se corrige tras conocer el header
        e.encSize = kNonceBytes + kTagBytes + payload.size();
        e.rawSize = a.bytes.size();
        toc.push_back(e);

        body.insert(body.end(), nonce.begin(), nonce.end());
        body.insert(body.end(), tag.begin(), tag.end());
        body.insert(body.end(), payload.begin(), payload.end());
    }

    // 3) Serializar la TOC.
    std::vector<u8> tocBytes;
    for (const auto& e : toc) {
        putU16(tocBytes, static_cast<u16>(e.path.size()));
        tocBytes.insert(tocBytes.end(), e.path.begin(), e.path.end());
        putU64(tocBytes, e.offset);
        putU64(tocBytes, e.encSize);
        putU64(tocBytes, e.rawSize);
    }

    // 4) Cabecera. El cuerpo va justo tras la cabecera; la TOC, tras el cuerpo.
    const u64 tocOffset = kHeaderSize + body.size();
    std::vector<u8> hdr;
    putU32(hdr, kMagic);
    putU32(hdr, kVersion);
    putU32(hdr, useXorInstead ? kFlagXor : 0u);
    putU32(hdr, static_cast<u32>(assets.size()));
    putU64(hdr, tocOffset);
    putU64(hdr, static_cast<u64>(tocBytes.size()));
    hdr.insert(hdr.end(), salt.begin(), salt.end());   // 8 bytes -> total 40

    // 5) Escribir header + body + toc.
    std::ofstream f(pakPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) { error = "No se pudo crear el .pak."; return false; }
    f.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    f.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    f.write(reinterpret_cast<const char*>(tocBytes.data()), static_cast<std::streamsize>(tocBytes.size()));
    if (!f.good()) { error = "Error al escribir el .pak."; return false; }
    return true;
}

} // namespace ludora
