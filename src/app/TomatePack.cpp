#include "app/TomatePack.h"
#include "app/Vault.h"
#include "app/Blake2b.h"
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <cstring>
#include <mutex>
#include <thread>
#include <windows.h>

namespace ludora {
namespace tomatepack {

namespace {

// ---------------------------------------------------------------- formato ---

constexpr u32 kMagic   = 0x33504D54;   // "TMP3"
constexpr u32 kVersion = 3;

/// Cabecera en claro al principio del archivo. Solo lleva lo imprescindible
/// para poder derivar las claves; nada del contenido.
///
///   magic | version | argonSalt(32) | packId(16) | indexCipherLen(8) | indexTag(16)
///
/// El INDICE va cifrado justo detras: es el que dice que archivos hay, donde
/// empieza cada bloque y cuanto ocupa. Cifrarlo significa que los nombres de
/// los archivos del juego no se leen abriendo el .tomate con un editor.
constexpr size_t kSaltBytes  = 32;
constexpr size_t kPackIdBytes = 16;
constexpr size_t kTagBytes   = 16;
constexpr size_t kNonceBytes = 12;
constexpr size_t kKeyBytes   = 32;
constexpr size_t kHeaderBytes = 4 + 4 + kSaltBytes + kPackIdBytes + 8 + kTagBytes;

/// Bloques en vuelo por hilo. Limita la memoria: con 8 hilos y 8 MiB de
/// bloque, el tope es 8*2*8 MiB = 128 MiB pase lo que pase.
constexpr u32 kQueuePerThread = 2;

void putU32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF));
}
void putU64(std::vector<u8>& v, u64 x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF));
}
u32 getU32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
u64 getU64(const u8* p) {
    u64 x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<u64>(p[i]) << (8 * i);
    return x;
}

// ------------------------------------------------------------- compresion ---

typedef LONG (WINAPI *GetWs_t)(USHORT, PULONG, PULONG);
typedef LONG (WINAPI *Comp_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);
typedef LONG (WINAPI *Dec_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

/// Punteros a ntdll resueltos una sola vez. Son estables durante el proceso.
struct Ntdll {
    GetWs_t getWs = nullptr;
    Comp_t  comp  = nullptr;
    Dec_t   dec   = nullptr;
    bool    xpress = false;      // la familia XPRESS esta disponible
    ULONG   wsComp = 0;          // workspace que sirve para CUALQUIER codec
    ULONG   wsFrag = 0;
    ULONG   wsDec  = 0;          // workspace de descompresion

    Ntdll() {
        const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (!nt) return;
        getWs = reinterpret_cast<GetWs_t>(GetProcAddress(nt, "RtlGetCompressionWorkSpaceSize"));
        comp  = reinterpret_cast<Comp_t>(GetProcAddress(nt, "RtlCompressBuffer"));
        dec   = reinterpret_cast<Dec_t>(GetProcAddress(nt, "RtlDecompressBufferEx"));
        if (!getWs || !comp || !dec) { getWs = nullptr; comp = nullptr; dec = nullptr; return; }

        // Se pregunta por los tres y se guarda el workspace MAYOR, que sirve
        // para todos. Reservar uno por codec no aporta nada: son 162 KB en el
        // peor caso, y asi un solo buffer por hilo cubre cualquier bloque.
        //
        // XPRESS y XPRESS_HUFF existen desde Windows 8. Si el sistema no los
        // tiene, getWs falla aqui y se usa LZNT1, que existe en todo Windows.
        bool huff = false, rapido = false;
        ULONG ws = 0, frag = 0;

        if (getWs(4 /*XPRESS_HUFF*/, &ws, &frag) >= 0) {
            huff = true;
            wsComp = (std::max)(wsComp, ws);
            wsFrag = (std::max)(wsFrag, frag);
        }
        if (getWs(3 /*XPRESS*/, &ws, &frag) >= 0) {
            rapido = true;
            wsComp = (std::max)(wsComp, ws);
            wsFrag = (std::max)(wsFrag, frag);
        }
        if (getWs(2 /*LZNT1*/, &ws, &frag) >= 0) {
            wsComp = (std::max)(wsComp, ws);
            wsFrag = (std::max)(wsFrag, frag);
        }

        // Se exige que esten LOS DOS de la familia XPRESS: compressBlock()
        // compara uno contra otro, y con solo uno esa comparacion no tiene
        // sentido. Falte cualquiera, se va por LZNT1.
        xpress = huff && rapido;

        // El workspace de descompresion ha de servir para cualquier codec que
        // aparezca en el paquete, no solo para el preferido de esta maquina:
        // un paquete hecho en otro PC puede traer bloques de otro codec.
        wsDec = wsComp;
        if (wsDec < (1u << 16)) wsDec = 1u << 16;
    }
};

const Ntdll& nt() { static const Ntdll n; return n; }

USHORT fmtOf(Codec c) {
    switch (c) {
        case Codec::XpressHuff: return 4;   // COMPRESSION_FORMAT_XPRESS_HUFF
        case Codec::Xpress:     return 3;   // COMPRESSION_FORMAT_XPRESS
        case Codec::Lznt1:      return 2;   // COMPRESSION_FORMAT_LZNT1
        default:                return 0;
    }
}

/// Comprime `in` en `out` eligiendo el MEJOR codec para ESTE bloque.
///
/// POR QUE SE PRUEBAN DOS Y NO SE FIJA UNO
/// Medido en esta maquina sobre 150 MB de mezcla real de juego (ejecutables,
/// texturas JPG y audio PCM):
///
///   codec         ratio   comprimir   descomprimir
///   LZNT1 std     57.8%    43.8 MB/s     99.5 MB/s   <- lo que se usaba antes
///   XPRESS        50.6%   183.0 MB/s    665.5 MB/s
///   XPRESS_HUFF   42.3%    89.6 MB/s    116.1 MB/s
///
/// XPRESS_HUFF deja el paquete un 27% mas pequeno que LZNT1, pero descomprime
/// casi 6 veces mas lento que XPRESS. Y eso no es simetrico: comprimir lo hace
/// el autor UNA vez, descomprimir lo hacen TODOS los jugadores en CADA
/// instalacion. Fijar XPRESS_HUFF para todo abarataria la descarga a costa de
/// hacer mas lenta cada instalacion.
///
/// La salida es quedarse con XPRESS_HUFF solo cuando de verdad gana espacio
/// apreciable sobre XPRESS; si la diferencia es pequena, manda el que instala
/// rapido. En datos ya comprimidos -- la mayor parte de un juego -- ninguno
/// gana nada y el bloque se guarda crudo, que es lo mas rapido de todo.
///
/// Probar los dos cuesta tiempo de compresion, pero eso se compensa de sobra:
/// ahora los bloques se comprimen en paralelo, antes todo iba en un solo hilo.
/// Los buffers de trabajo son UNO POR HILO y se reutilizan entre bloques: sin
/// eso, cada bloque reservaria y liberaria varios megas, y con siete hilos
/// comprimiendo a la vez el asignador de memoria se convierte en el cuello de
/// botella.
struct Buffers {
    std::vector<u8> ws;        // workspace que pide ntdll
    std::vector<u8> scratch;   // salida del compresor denso, para comparar
    std::vector<u8> cata;      // salida de la cata previa
};

Codec compressBlock(const u8* in, size_t n, std::vector<u8>& out, Buffers& b) {
    std::vector<u8>& ws      = b.ws;
    std::vector<u8>& scratch = b.scratch;
    std::vector<u8>& dstCata = b.cata;
    const Ntdll& N = nt();
    if (!N.comp || n == 0) return Codec::None;

    // Se exige ganar al menos un 2%: descomprimir cuesta tiempo al instalar y
    // no merece la pena por unos pocos bytes.
    const size_t tope = n - n / 50;

    auto intentar = [&](Codec c, std::vector<u8>& dst) -> size_t {
        const USHORT fmt = fmtOf(c);
        if (fmt == 0) return 0;
        dst.resize(n);
        if (ws.size() < N.wsComp) ws.resize(N.wsComp);
        ULONG fin = 0;
        const LONG r = N.comp(fmt, const_cast<PUCHAR>(in), static_cast<ULONG>(n),
                              dst.data(), static_cast<ULONG>(dst.size()),
                              N.wsFrag, &fin, ws.data());
        if (r < 0 || fin == 0 || fin > tope) return 0;
        return static_cast<size_t>(fin);
    };

    // Sin XPRESS (Windows 7 o anterior) se cae a LZNT1, que existe siempre.
    if (!N.xpress) {
        const size_t nl = intentar(Codec::Lznt1, out);
        if (nl == 0) { out.clear(); return Codec::None; }
        out.resize(nl);
        return Codec::Lznt1;
    }

    // --- cata previa: 256 KB con el compresor rapido ---
    //
    // Comprimir el bloque entero con los DOS compresores para quedarse con el
    // mejor costaba mas de lo que valia: medido sobre 400 MB, guardar salia
    // mas LENTO que el formato antiguo, porque la mitad de un juego son
    // texturas y audio en los que NINGUNO de los dos gana nada y el trabajo se
    // tiraba a la basura dos veces.
    //
    // Ahora se cata un trozo pequeno primero. Si ese trozo no se deja
    // comprimir, el bloque se guarda crudo sin tocar nada mas -- que es el
    // caso mayoritario y el mas rapido posible. Si se deja, ya se sabe que
    // merece la pena y se comprime de verdad.
    constexpr size_t kCata = 256 * 1024;
    if (n > kCata) {
        dstCata.resize(kCata);
        if (ws.size() < N.wsComp) ws.resize(N.wsComp);
        ULONG fin = 0;
        const LONG r = N.comp(fmtOf(Codec::Xpress), const_cast<PUCHAR>(in),
                              static_cast<ULONG>(kCata),
                              dstCata.data(), static_cast<ULONG>(dstCata.size()),
                              N.wsFrag, &fin, ws.data());
        // Se exige que la cata gane al menos un 5%: por debajo de eso, lo que
        // venga detras no va a cambiar el resultado.
        if (r < 0 || fin == 0 || fin > kCata - kCata / 20) {
            out.clear();
            return Codec::None;
        }
    }

    const size_t nRapido = intentar(Codec::Xpress, out);
    if (nRapido == 0) { out.clear(); return Codec::None; }

    // El compresor denso solo se prueba cuando el rapido ya demostro que estos
    // datos comprimen BIEN (han bajado de la mitad). Si apenas comprimian, el
    // denso tampoco va a obrar milagros y no vale su coste.
    if (nRapido < n / 2) {
        const size_t nDenso = intentar(Codec::XpressHuff, scratch);
        // Solo se paga la descompresion lenta si el ahorro es real: un 10%
        // menos de lo que ya habia conseguido el rapido.
        if (nDenso != 0 && nDenso < nRapido - nRapido / 10) {
            out.swap(scratch);
            out.resize(nDenso);
            return Codec::XpressHuff;
        }
    }

    out.resize(nRapido);
    return Codec::Xpress;
}

/// Deshace compressBlock(). `orig` es el tamano original exacto, que viene del
/// indice: asi no hay que adivinarlo ni confiar en el bloque.
bool decompressBlock(Codec c, const u8* in, size_t n,
                     std::vector<u8>& out, size_t orig,
                     std::vector<u8>& ws) {
    if (c == Codec::None) {
        if (n != orig) return false;
        out.assign(in, in + n);
        return true;
    }
    const Ntdll& N = nt();
    if (!N.dec) return false;

    out.assign(orig, 0);
    if (ws.size() < N.wsDec) ws.resize(N.wsDec);
    ULONG prod = 0;
    const LONG r = N.dec(fmtOf(c), out.data(), static_cast<ULONG>(orig),
                         const_cast<PUCHAR>(in), static_cast<ULONG>(n),
                         &prod, ws.data());
    if (r < 0 || prod != orig) return false;
    return true;
}

// ----------------------------------------------------------------- claves ---

/// Clave maestra del paquete: el UNICO sitio donde se paga Argon2id.
bool masterKey(const std::string& secret, const std::vector<u8>& salt,
               std::vector<u8>& out) {
    const std::string stretched = Vault::stretchSecret(secret, salt);
    if (stretched.empty()) return false;
    // Una pasada de PBKDF2 sobre el resultado de Argon2, como en el Vault, para
    // no cambiar la forma de derivar que ya estaba revisada.
    return Vault::deriveKey(stretched, salt, 1, out);
}

/// Subclave de un bloque. BLAKE2b con clave sobre (packId | indice | etiqueta).
///
/// Es practicamente gratis -- un hash de 64 bytes -- y aporta lo que importa:
/// el indice del bloque y el identificador del paquete entran en la clave, asi
/// que un bloque solo descifra en SU posicion de SU paquete. Reordenarlos,
/// repetirlos o traer uno de otro paquete hace fallar el tag.
void blockKey(const std::vector<u8>& master, const u8* packId, u64 index,
              const char* etiqueta, std::vector<u8>& key, std::vector<u8>& nonce) {
    u8 buf[kPackIdBytes + 8 + 16];
    size_t p = 0;
    std::memcpy(buf + p, packId, kPackIdBytes); p += kPackIdBytes;
    for (int i = 0; i < 8; ++i) buf[p++] = static_cast<u8>((index >> (8 * i)) & 0xFF);
    const size_t tl = (std::min)(std::strlen(etiqueta), size_t(16));
    std::memcpy(buf + p, etiqueta, tl); p += tl;

    // 48 bytes: 32 de clave + 12 de nonce (y sobra, que se descarta).
    u8 out[48];
    Blake2b::hash(out, sizeof(out), buf, p, master.data(), master.size());
    key.assign(out, out + kKeyBytes);
    nonce.assign(out + kKeyBytes, out + kKeyBytes + kNonceBytes);
    SecureZeroMemory(out, sizeof(out));
}

// ------------------------------------------------------------------ disco ---

std::wstring parentOf(const std::wstring& p) {
    const size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? L"." : p.substr(0, s);
}

void makeDirs(const std::wstring& dir) {
    if (dir.empty() || GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    makeDirs(parentOf(dir));
    CreateDirectoryW(dir.c_str(), nullptr);
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                      nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

/// Un archivo abierto que se cierra solo.
struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }

    bool openRead(const std::wstring& p) {
        h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        return ok();
    }
    bool openWrite(const std::wstring& p) {
        h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return ok();
    }
    /// Lee exactamente `n` bytes. ReadFile puede devolver menos de lo pedido.
    bool readAll(u8* dst, u64 n) {
        u64 hecho = 0;
        while (hecho < n) {
            const DWORD pedir = static_cast<DWORD>((std::min)(n - hecho, 8ull << 20));
            DWORD got = 0;
            if (!ReadFile(h, dst + hecho, pedir, &got, nullptr) || got == 0) return false;
            hecho += got;
        }
        return true;
    }
    /// Escribe exactamente `n` bytes. WriteFile tambien puede quedarse corto.
    bool writeAll(const u8* src, u64 n) {
        u64 hecho = 0;
        while (hecho < n) {
            const DWORD pedir = static_cast<DWORD>((std::min)(n - hecho, 8ull << 20));
            DWORD put = 0;
            if (!WriteFile(h, src + hecho, pedir, &put, nullptr) || put == 0) return false;
            hecho += put;
        }
        return true;
    }
    u64 size() const {
        LARGE_INTEGER li{};
        return GetFileSizeEx(h, &li) ? static_cast<u64>(li.QuadPart) : 0;
    }
    bool seek(u64 off) {
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(off);
        return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) != 0;
    }
};

/// Recorre el arbol y devuelve rutas relativas, en orden estable.
void listFiles(const std::wstring& dir, const std::wstring& rel,
               std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    // Se anota DONDE empieza lo que anade este nivel, para ordenar solo eso.
    const size_t miInicio = out.size();

    // De cada subcarpeta se guardan LAS DOS rutas: el nombre suelto, para
    // bajar por el arbol, y la ruta relativa a la raiz, que es la que va al
    // indice. Confundirlas hacia que la recursion compusiera
    // "src\datos\datos\nivel1" -- la subcarpeta duplicada -- y entonces no
    // encontraba nada: el paquete salia con los archivos de la raiz y SIN
    // ninguno de los de dentro, sin dar error.
    struct Sub { std::wstring nombre, rel; };
    std::vector<Sub> subdirs;

    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring childRel = rel.empty() ? name : (rel + L"\\" + name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            subdirs.push_back({name, childRel});
        else
            out.push_back(childRel);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    // Orden estable: dos guardados de la misma carpeta producen el mismo
    // indice, lo que hace los paquetes reproducibles.
    //
    // Se ordena SOLO desde `miInicio`, no el vector entero.
    //
    // BUG QUE ESTO ARREGLA: ordenar `out` completo en cada nivel de la
    // recursion barajaba tambien las rutas que los niveles de ARRIBA ya habian
    // anadido. El indice quedaba en un orden y el lector reconstruia los
    // archivos en otro, asi que el contenido se escribia en el archivo
    // equivocado: un juego que se guardaba sin dar ningun error y se instalaba
    // con los archivos mezclados. Lo encontraron las pruebas de ida y vuelta,
    // comparando byte a byte lo guardado con lo recuperado.
    std::sort(out.begin() + miInicio, out.end());
    std::sort(subdirs.begin(), subdirs.end(),
              [](const Sub& a, const Sub& b) { return a.rel < b.rel; });

    // Al bajar: `dir` crece con el NOMBRE suelto, y la ruta relativa que se
    // arrastra es la del indice.
    for (const auto& sd : subdirs)
        listFiles(dir + L"\\" + sd.nombre, sd.rel, out);
}

// ------------------------------------------------------------------ indice ---

/// Un archivo dentro del paquete: donde empieza en el flujo logico y cuanto mide.
struct Entry {
    std::string rel;     // ruta relativa en UTF-8
    u64 offset = 0;      // posicion en el flujo concatenado (sin comprimir)
    u64 size   = 0;
};

/// Un bloque del paquete tal como quedo en disco.
struct Block {
    u64   fileOffset = 0;   // donde empieza en el .tomate
    u64   storedLen  = 0;   // lo que ocupa cifrado
    u64   plainLen   = 0;   // lo que mide al descomprimir
    Codec codec      = Codec::None;
    u8    tag[kTagBytes]{};
};

/// Serializa el indice. Se cifra entero antes de escribirlo.
void writeIndex(const std::vector<Entry>& files, const std::vector<Block>& blocks,
                std::vector<u8>& out) {
    putU64(out, files.size());
    for (const auto& e : files) {
        putU32(out, static_cast<u32>(e.rel.size()));
        out.insert(out.end(), e.rel.begin(), e.rel.end());
        putU64(out, e.offset);
        putU64(out, e.size);
    }
    putU64(out, blocks.size());
    for (const auto& b : blocks) {
        putU64(out, b.fileOffset);
        putU64(out, b.storedLen);
        putU64(out, b.plainLen);
        putU32(out, static_cast<u32>(b.codec));
        out.insert(out.end(), b.tag, b.tag + kTagBytes);
    }
}

/// Lee el indice con comprobaciones de limites en CADA campo: el indice viene
/// de un archivo que pudo ser manipulado, aunque el tag ya lo haya validado.
bool readIndex(const std::vector<u8>& in, std::vector<Entry>& files,
               std::vector<Block>& blocks) {
    size_t p = 0;
    auto need = [&](size_t n) { return p + n <= in.size(); };

    if (!need(8)) return false;
    const u64 nf = getU64(&in[p]); p += 8;
    // Tope de cordura: un juego con mas de 10 millones de archivos no es un
    // juego, es un indice corrupto que intentaria reservar memoria absurda.
    if (nf > 10'000'000ull) return false;
    files.clear(); files.reserve(static_cast<size_t>(nf));

    for (u64 i = 0; i < nf; ++i) {
        if (!need(4)) return false;
        const u32 rl = getU32(&in[p]); p += 4;
        if (rl > 32768 || !need(rl)) return false;
        Entry e;
        e.rel.assign(reinterpret_cast<const char*>(&in[p]), rl); p += rl;
        if (!need(16)) return false;
        e.offset = getU64(&in[p]); p += 8;
        e.size   = getU64(&in[p]); p += 8;
        files.push_back(std::move(e));
    }

    if (!need(8)) return false;
    const u64 nb = getU64(&in[p]); p += 8;
    if (nb > 10'000'000ull) return false;
    blocks.clear(); blocks.reserve(static_cast<size_t>(nb));

    for (u64 i = 0; i < nb; ++i) {
        if (!need(8 + 8 + 8 + 4 + kTagBytes)) return false;
        Block b;
        b.fileOffset = getU64(&in[p]); p += 8;
        b.storedLen  = getU64(&in[p]); p += 8;
        b.plainLen   = getU64(&in[p]); p += 8;
        const u32 c  = getU32(&in[p]); p += 4;

        // Se valida contra la lista REAL de codecs, uno por uno.
        //
        // BUG QUE ESTO ARREGLA, y rompia todos los paquetes nuevos: aqui habia
        // un `c > (u32)Codec::XpressHuff`, que vale 2. Cuando se anadio
        // `Xpress` se le dio el valor 3, asi que cualquier bloque comprimido
        // con XPRESS -- el que el escritor elige casi siempre -- se rechazaba
        // al leer el indice. El paquete se guardaba perfectamente y luego era
        // ILEGIBLE: "el indice del paquete esta danado" en un archivo intacto.
        //
        // Comparar con "el ultimo del enum" es justo lo que se rompe al anadir
        // uno nuevo, asi que ahora se enumeran: si manana aparece otro codec y
        // alguien olvida ponerlo aqui, falla al escribirlo y se ve en el
        // momento, en vez de producir paquetes que no se pueden abrir.
        if (c != static_cast<u32>(Codec::None)       &&
            c != static_cast<u32>(Codec::Lznt1)      &&
            c != static_cast<u32>(Codec::XpressHuff) &&
            c != static_cast<u32>(Codec::Xpress))
            return false;
        b.codec = static_cast<Codec>(c);
        // Un bloque no puede decir que al descomprimir mide mas que el tope:
        // sin esto, un indice alterado haria reservar memoria sin limite.
        if (b.plainLen > kBlockBytes) return false;
        std::memcpy(b.tag, &in[p], kTagBytes); p += kTagBytes;
        blocks.push_back(b);
    }
    return true;
}

/// Rechaza rutas que escaparian de la carpeta de destino.
///
/// Un paquete es un archivo que puede venir de cualquiera. Sin esta
/// comprobacion, una entrada con "..\\..\\Windows\\System32\\algo.dll" haria
/// que instalar un juego escribiera fuera de su carpeta. Es la clase de fallo
/// que convierte "instalar un juego" en "dejar que el juego toque el sistema".
bool safeRelPath(const std::wstring& rel) {
    if (rel.empty()) return false;
    if (rel.size() >= 2 && rel[1] == L':') return false;         // C:\...
    if (rel[0] == L'\\' || rel[0] == L'/') return false;          // raiz
    if (rel.find(L"\\\\") != std::wstring::npos) return false;    // UNC o vacio

    // Cada componente por separado: ".." en cualquier posicion esta fuera.
    size_t ini = 0;
    while (ini <= rel.size()) {
        size_t fin = rel.find_first_of(L"\\/", ini);
        if (fin == std::wstring::npos) fin = rel.size();
        const std::wstring comp = rel.substr(ini, fin - ini);
        if (comp == L".." || comp == L"." || comp.empty()) {
            if (!(comp.empty() && fin == rel.size())) return false;
        }
        // Los dos puntos dentro de un nombre serian un flujo alternativo NTFS.
        if (comp.find(L':') != std::wstring::npos) return false;
        if (fin == rel.size()) break;
        ini = fin + 1;
    }
    return true;
}

// -------------------------------------------------------- cifrado de bloque ---

/// Cifra un bloque en su sitio, dejando el tag aparte.
bool sealBlock(const std::vector<u8>& master, const u8* packId, u64 index,
               const std::vector<u8>& plain, std::vector<u8>& out, u8* tagOut) {
    std::vector<u8> key, nonce, tag(kTagBytes, 0);
    blockKey(master, packId, index, "bloque", key, nonce);
    const bool ok = Vault::aead(true, key, nonce, plain, tag, out);
    SecureZeroMemory(key.data(), key.size());
    if (ok) std::memcpy(tagOut, tag.data(), kTagBytes);
    return ok;
}

/// Descifra y verifica. Si el tag no cuadra, falla: el bloque fue alterado.
bool openBlock(const std::vector<u8>& master, const u8* packId, u64 index,
               const std::vector<u8>& cipher, const u8* tagIn,
               std::vector<u8>& out) {
    std::vector<u8> key, nonce;
    blockKey(master, packId, index, "bloque", key, nonce);
    std::vector<u8> tag(tagIn, tagIn + kTagBytes);
    const bool ok = Vault::aead(false, key, nonce, cipher, tag, out);
    SecureZeroMemory(key.data(), key.size());
    return ok;
}

/// El indice usa la MISMA mecanica que un bloque, con otra etiqueta: asi su
/// clave es distinta de la de cualquier bloque aunque comparta el indice 0.
bool sealIndex(const std::vector<u8>& master, const u8* packId,
               const std::vector<u8>& plain, std::vector<u8>& out, u8* tagOut) {
    std::vector<u8> key, nonce, tag(kTagBytes, 0);
    blockKey(master, packId, 0, "indice", key, nonce);
    const bool ok = Vault::aead(true, key, nonce, plain, tag, out);
    SecureZeroMemory(key.data(), key.size());
    if (ok) std::memcpy(tagOut, tag.data(), kTagBytes);
    return ok;
}

bool openIndex(const std::vector<u8>& master, const u8* packId,
               const std::vector<u8>& cipher, const u8* tagIn,
               std::vector<u8>& out) {
    std::vector<u8> key, nonce;
    blockKey(master, packId, 0, "indice", key, nonce);
    std::vector<u8> tag(tagIn, tagIn + kTagBytes);
    const bool ok = Vault::aead(false, key, nonce, cipher, tag, out);
    SecureZeroMemory(key.data(), key.size());
    return ok;
}

u32 hilosUtiles() {
    u32 n = std::thread::hardware_concurrency();
    if (n == 0) n = 2;
    // Se deja un nucleo libre: Ludora sigue dibujando su interfaz mientras
    // guarda, y quedarse sin ningun nucleo para la UI la congela.
    if (n > 2) n -= 1;
    return (std::min)(n, 16u);
}

} // namespace

Caps caps() {
    Caps c;
    c.xpressHuff = nt().xpress;
    c.threads    = hilosUtiles();
    return c;
}

// ----------------------------------------------------------------- sellado ---

namespace {

/// Trabajo de un bloque: datos en claro esperando a comprimirse y cifrarse.
struct Job {
    u64 index = 0;
    std::vector<u8> plain;
};

/// Bloque ya comprimido y cifrado, esperando a escribirse.
struct Hecho {
    u64 index = 0;
    std::vector<u8> cipher;
    u64 plainLen = 0;
    Codec codec = Codec::None;
    u8 tag[kTagBytes]{};
};

/// Estado compartido entre el lector, los trabajadores y el escritor.
///
/// Una sola estructura con un solo mutex: con varios candados sobre cosas que
/// siempre se tocan juntas, el orden en que se cogen se convierte en una
/// fuente de bloqueos mutuos muy difícil de ver.
struct Canal {
    std::mutex mx;
    std::condition_variable cvJob;    // hay trabajo / hay hueco
    std::condition_variable cvDone;   // hay bloque listo
    std::vector<Job>   pendientes;
    std::vector<Hecho> listos;
    bool        finLectura = false;
    bool        fallo      = false;
    std::string falloMsg;

    void marcarFallo(std::string msg) {
        {
            std::lock_guard<std::mutex> lk(mx);
            if (!fallo) { fallo = true; falloMsg = std::move(msg); }
        }
        // Despertar a todos: quien esté esperando trabajo, hueco o un bloque
        // listo tiene que salir en cuanto hay fallo, o se queda colgado.
        cvJob.notify_all();
        cvDone.notify_all();
    }
    bool hayFallo() {
        std::lock_guard<std::mutex> lk(mx);
        return fallo;
    }
};

} // namespace

bool sealFolder(const std::wstring& srcDir,
                const std::wstring& destPath,
                const std::string&  secret,
                std::atomic<float>* progress,
                std::string&        error,
                const std::atomic<bool>* cancel) {
    if (srcDir.empty()) { error = "No se eligio ninguna carpeta."; return false; }
    if (secret.empty())  { error = "Falta el secreto de cifrado.";  return false; }
    if (GetFileAttributesW(srcDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "No se pudo leer esa carpeta.";
        return false;
    }

    auto cancelado = [&] { return cancel && cancel->load(); };

    // 1) Inventario. El tamano sale de los metadatos del directorio, que ya
    //    estan en cache tras recorrerlo: no cuesta abrir los archivos.
    std::vector<std::wstring> rels;
    listFiles(srcDir, L"", rels);
    if (rels.empty()) {
        error = "Esa carpeta esta vacia: no hay nada que guardar.";
        return false;
    }

    std::vector<Entry> files;
    files.reserve(rels.size());
    u64 totalBytes = 0;
    for (const auto& rel : rels) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW((srcDir + L"\\" + rel).c_str(),
                                  GetFileExInfoStandard, &fad))
            continue;
        Entry e;
        e.rel    = toUtf8(rel);
        if (e.rel.empty()) continue;          // nombre no representable: se salta
        e.offset = totalBytes;
        e.size   = (static_cast<u64>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        totalBytes += e.size;
        files.push_back(std::move(e));
    }
    if (files.empty()) {
        error = "No se pudo leer ningun archivo de la carpeta.";
        return false;
    }

    // 2) Sitio en disco.
    //
    // Ya NO se comprueba la memoria disponible. Antes habia que hacerlo, y era
    // el aviso mas frustrante de todos: el guardado necesitaba 2.2 veces el
    // tamano del juego en RAM, asi que un juego grande se negaba a guardarse
    // en una maquina normal. Ahora la memoria que se usa es fija -- unas
    // decenas de MB -- sea el juego de 10 MB o de 100 GB, asi que ese limite
    // desaparecio y con el su mensaje de error.
    {
        ULARGE_INTEGER libre{};
        const std::wstring destDir = parentOf(destPath);
        if (!destDir.empty() &&
            GetDiskFreeSpaceExW(destDir.c_str(), &libre, nullptr, nullptr) &&
            libre.QuadPart < totalBytes) {
            char b[240];
            std::snprintf(b, sizeof(b),
                          "No hay sitio en el disco: el paquete puede ocupar "
                          "hasta %.1f GB y quedan %.1f GB libres.",
                          totalBytes / (1024.0 * 1024.0 * 1024.0),
                          libre.QuadPart / (1024.0 * 1024.0 * 1024.0));
            error = b;
            return false;
        }
    }

    // 3) Claves. El estirado caro (Argon2id), una sola vez para todo.
    std::vector<u8> argonSalt, packId;
    if (!Vault::randomBytes(argonSalt, kSaltBytes) ||
        !Vault::randomBytes(packId, kPackIdBytes)) {
        error = "No se pudo generar aleatoriedad para el paquete.";
        return false;
    }
    std::vector<u8> master;
    if (!masterKey(secret, argonSalt, master)) {
        error = "Fallo al derivar la clave del paquete.";
        return false;
    }
    if (progress) progress->store(0.02f);

    // Se escribe a un temporal y se renombra al final: el paquete anterior no
    // se toca hasta que el nuevo esta entero en disco, asi que un corte a
    // mitad -- apagon, disco lleno -- no destruye el que ya funcionaba.
    const std::wstring tmp = destPath + L".parcial";
    makeDirs(parentOf(destPath));
    DeleteFileW(tmp.c_str());

    // El numero de bloques se fija AQUI y no cambia. El indice tiene tamano
    // predecible (cada entrada ocupa lo mismo), que es lo que permite
    // reservarle su hueco al principio del archivo y rellenarlo al final.
    const u64 nBlocks = (totalBytes + kBlockBytes - 1) / kBlockBytes;
    std::vector<Block> blocks(static_cast<size_t>(nBlocks));

    // Todo el trabajo sucio va aqui dentro. Devolver un bool en vez de hacer
    // `return false` suelto por media funcion deja UN solo sitio donde limpiar
    // -- borrar el temporal y borrar la clave de memoria -- y asi no hay forma
    // de olvidarse en un camino de error.
    auto construir = [&]() -> bool {
        File out;
        if (!out.openWrite(tmp)) {
            error = "No se pudo crear el paquete. Comprueba que queda sitio "
                    "en el disco.";
            return false;
        }

        std::vector<u8> idxPlain;
        writeIndex(files, blocks, idxPlain);
        const u64 idxLen    = idxPlain.size();
        const u64 dataStart = kHeaderBytes + idxLen;

        if (!out.seek(dataStart)) {
            error = "No se pudo preparar el paquete en disco.";
            return false;
        }

        Canal canal;
        const u32    nHilos   = hilosUtiles();
        const size_t topeCola = static_cast<size_t>(nHilos) * kQueuePerThread;

        // --- trabajadores: comprimen y cifran ---
        //
        // Aqui esta todo el trabajo de CPU del guardado, y es lo que antes se
        // hacia en UN solo hilo sobre el juego entero. Cada uno tiene sus
        // propios buffers de trabajo, asi que no comparten nada mas que las
        // dos colas.
        auto trabajador = [&] {
            Buffers bufs;
            std::vector<u8> comp;
            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> lk(canal.mx);
                    canal.cvJob.wait(lk, [&] {
                        return !canal.pendientes.empty() || canal.finLectura ||
                               canal.fallo;
                    });
                    if (canal.fallo) return;
                    if (canal.pendientes.empty()) {
                        if (canal.finLectura) return;   // no queda nada por venir
                        continue;
                    }
                    job = std::move(canal.pendientes.back());
                    canal.pendientes.pop_back();
                }
                // Hay hueco en la cola de entrada: el lector puede seguir.
                canal.cvJob.notify_one();

                Hecho h;
                h.index    = job.index;
                h.plainLen = job.plain.size();

                comp.clear();
                h.codec = compressBlock(job.plain.data(), job.plain.size(),
                                        comp, bufs);

                const std::vector<u8>& fuente =
                    (h.codec == Codec::None) ? job.plain : comp;
                if (!sealBlock(master, packId.data(), job.index, fuente,
                               h.cipher, h.tag)) {
                    canal.marcarFallo("Fallo al cifrar un bloque del paquete.");
                    return;
                }

                {
                    std::lock_guard<std::mutex> lk(canal.mx);
                    canal.listos.push_back(std::move(h));
                }
                canal.cvDone.notify_one();
            }
        };

        std::vector<std::thread> hilos;
        hilos.reserve(nHilos);
        for (u32 i = 0; i < nHilos; ++i) hilos.emplace_back(trabajador);

        // Los hilos se recogen SIEMPRE al salir de aqui, por cualquier camino.
        // Dejarlos sueltos mientras se destruyen el canal y las colas que usan
        // seria un cuelgue, o memoria pisada.
        struct Recoger {
            std::vector<std::thread>& h;
            Canal& c;
            ~Recoger() {
                {
                    std::lock_guard<std::mutex> lk(c.mx);
                    c.finLectura = true;    // que nadie se quede esperando trabajo
                }
                c.cvJob.notify_all();
                c.cvDone.notify_all();
                for (auto& t : h) if (t.joinable()) t.join();
            }
        } recoger{hilos, canal};

        // --- escritor: vive en ESTE hilo ---
        //
        // Va vaciando la cola de salida en el orden en que los bloques caen,
        // que no tiene que ser el del indice: cada bloque guarda su propio
        // offset, asi que el orden en el archivo da igual.
        u64 escritos = 0;
        u64 posicion = dataStart;

        auto escribirUno = [&](Hecho&& h) -> bool {
            Block& b = blocks[static_cast<size_t>(h.index)];
            b.fileOffset = posicion;
            b.storedLen  = h.cipher.size();
            b.plainLen   = h.plainLen;
            b.codec      = h.codec;
            std::memcpy(b.tag, h.tag, kTagBytes);

            if (!out.writeAll(h.cipher.data(), h.cipher.size())) {
                canal.marcarFallo("No se pudo escribir el paquete. Comprueba "
                                  "que queda sitio en el disco.");
                return false;
            }
            posicion += h.cipher.size();
            ++escritos;

            if (progress && nBlocks > 0) {
                const f32 t = static_cast<f32>(escritos) / static_cast<f32>(nBlocks);
                progress->store(0.02f + 0.94f * t);
            }
            return true;
        };

        // Escribe lo que ya este listo. Si `esperar`, se queda hasta que caiga
        // al menos uno (o hasta que haya fallo).
        auto vaciar = [&](bool esperar) -> bool {
            for (;;) {
                Hecho h;
                {
                    std::unique_lock<std::mutex> lk(canal.mx);
                    if (canal.fallo) return false;
                    if (canal.listos.empty()) {
                        if (!esperar) return true;
                        canal.cvDone.wait(lk, [&] {
                            return !canal.listos.empty() || canal.fallo;
                        });
                        if (canal.fallo) return false;
                    }
                    h = std::move(canal.listos.back());
                    canal.listos.pop_back();
                }
                if (!escribirUno(std::move(h))) return false;
                if (!esperar) {
                    // En modo no bloqueante se sigue solo mientras quede algo.
                    std::lock_guard<std::mutex> lk(canal.mx);
                    if (canal.listos.empty()) return true;
                } else {
                    return true;
                }
            }
        };

        // --- lector: trocea el flujo en bloques ---
        //
        // Los archivos se concatenan en un flujo logico continuo y ESE flujo se
        // corta cada kBlockBytes. Un archivo puede quedar repartido entre dos
        // bloques, y es deseable: alineando bloques con archivos, un juego de
        // miles de archivos pequenos daria miles de bloques diminutos, que
        // comprimen mucho peor.
        bool lecturaOk = true;
        std::string lecturaMsg;
        {
            std::vector<u8> acumula;
            acumula.reserve(static_cast<size_t>(kBlockBytes) * 2);
            u64 indiceBloque = 0;

            auto entregar = [&](bool forzar) -> bool {
                while (acumula.size() >= kBlockBytes ||
                       (forzar && !acumula.empty())) {
                    const size_t corte =
                        (std::min)(static_cast<size_t>(kBlockBytes), acumula.size());

                    // Mas bloques de los previstos solo puede pasar si un
                    // archivo crecio entre medirlo y leerlo. El indice ya tiene
                    // su hueco reservado para nBlocks, asi que no caben: se
                    // para con un aviso en vez de escribir fuera del hueco.
                    if (indiceBloque >= nBlocks) {
                        lecturaMsg = "La carpeta cambio mientras se guardaba. "
                                     "Vuelve a intentarlo.";
                        return false;
                    }

                    Job job;
                    job.index = indiceBloque++;
                    job.plain.assign(acumula.begin(), acumula.begin() + corte);
                    acumula.erase(acumula.begin(), acumula.begin() + corte);

                    {
                        std::unique_lock<std::mutex> lk(canal.mx);
                        // Esperar si la cola esta llena. ESTE freno es lo que
                        // mantiene la memoria acotada: sin el, leer del disco
                        // mas rapido de lo que se comprime acabaria con el
                        // juego entero en RAM, que es el problema que este
                        // formato venia a resolver.
                        canal.cvJob.wait(lk, [&] {
                            return canal.pendientes.size() < topeCola || canal.fallo;
                        });
                        if (canal.fallo) return false;
                        canal.pendientes.push_back(std::move(job));
                    }
                    canal.cvJob.notify_one();

                    // Ir escribiendo lo que haya listo, sin bloquearse.
                    if (!vaciar(false)) return false;
                }
                return true;
            };

            for (const auto& e : files) {
                if (cancelado()) {
                    lecturaOk = false;
                    lecturaMsg = "Se cancelo el guardado.";
                    break;
                }

                File f;
                if (!f.openRead(srcDir + L"\\" + fromUtf8(e.rel))) {
                    lecturaOk = false;
                    lecturaMsg = "No se pudo leer un archivo de la carpeta.";
                    break;
                }
                u64 restan = e.size;
                while (restan > 0) {
                    const size_t trozo =
                        static_cast<size_t>((std::min)(restan, kBlockBytes));
                    const size_t base = acumula.size();
                    acumula.resize(base + trozo);
                    if (!f.readAll(acumula.data() + base, trozo)) {
                        lecturaOk = false;
                        lecturaMsg = "No se pudo leer un archivo de la carpeta.";
                        break;
                    }
                    restan -= trozo;
                    if (!entregar(false)) { lecturaOk = false; break; }
                }
                if (!lecturaOk) break;
            }
            if (lecturaOk && !entregar(true)) lecturaOk = false;

            // Menos bloques de los previstos: un archivo se encogio entre
            // medirlo y leerlo. Se recorta el indice; su hueco en disco sigue
            // siendo el reservado, que es mayor, y eso no estorba.
            if (lecturaOk && indiceBloque < nBlocks)
                blocks.resize(static_cast<size_t>(indiceBloque));
        }

        // Se avisa de que no vendra mas trabajo ANTES de esperar los bloques en
        // vuelo: si no, los trabajadores se quedarian esperando trabajo nuevo
        // y el escritor esperandolos a ellos.
        {
            std::lock_guard<std::mutex> lk(canal.mx);
            canal.finLectura = true;
        }
        canal.cvJob.notify_all();

        if (!lecturaOk) {
            canal.marcarFallo(lecturaMsg.empty()
                ? std::string("No se pudo leer la carpeta del juego.")
                : lecturaMsg);
            error = canal.falloMsg;
            return false;
        }

        // Esperar y escribir los que quedan en vuelo.
        const u64 esperados = blocks.size();
        while (escritos < esperados) {
            if (!vaciar(true)) break;
        }

        if (canal.hayFallo() || escritos != esperados) {
            std::lock_guard<std::mutex> lk(canal.mx);
            error = canal.falloMsg.empty()
                ? std::string("No se pudo guardar el paquete.")
                : canal.falloMsg;
            return false;
        }

        // --- indice ---
        //
        // Ahora si se conocen los tags y offsets de todos los bloques. El
        // indice se cifra con su propia clave y se escribe en el hueco que se
        // le reservo al principio.
        idxPlain.clear();
        writeIndex(files, blocks, idxPlain);
        if (idxPlain.size() > idxLen) {
            // No deberia ocurrir: el indice solo puede haber encogido (menos
            // bloques). Si creciera, escribirlo pisaria los datos.
            error = "El indice del paquete no cabe en su hueco.";
            return false;
        }
        // Si encogio, se rellena hasta el hueco reservado. El lector se guia
        // por los contadores del propio indice, asi que la cola sobrante no
        // se interpreta como contenido.
        idxPlain.resize(static_cast<size_t>(idxLen), 0);

        std::vector<u8> idxCipher;
        u8 idxTag[kTagBytes]{};
        if (!sealIndex(master, packId.data(), idxPlain, idxCipher, idxTag)) {
            error = "Fallo al cifrar el indice del paquete.";
            return false;
        }

        std::vector<u8> head;
        putU32(head, kMagic);
        putU32(head, kVersion);
        head.insert(head.end(), argonSalt.begin(), argonSalt.end());
        head.insert(head.end(), packId.begin(), packId.end());
        putU64(head, idxCipher.size());
        head.insert(head.end(), idxTag, idxTag + kTagBytes);
        if (head.size() != kHeaderBytes) {
            error = "Cabecera del paquete mal formada.";
            return false;
        }

        if (!out.seek(0) ||
            !out.writeAll(head.data(), head.size()) ||
            !out.writeAll(idxCipher.data(), idxCipher.size())) {
            error = "No se pudo escribir el indice del paquete.";
            return false;
        }

        // Al disco de verdad antes de dar el guardado por bueno: sin esto, un
        // apagon justo despues podria dejar el renombrado hecho y el contenido
        // a medias en la cache del sistema.
        FlushFileBuffers(out.h);
        return true;
    };

    const bool ok = construir();

    // Punto unico de limpieza: la clave se borra de memoria pase lo que pase.
    SecureZeroMemory(master.data(), master.size());

    if (!ok) {
        DeleteFileW(tmp.c_str());   // nunca se deja un paquete a medias
        if (error.empty()) error = "No se pudo guardar el paquete.";
        return false;
    }

    if (!MoveFileExW(tmp.c_str(), destPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION)
            error = "El paquete anterior esta en uso. Cierra el juego y "
                    "vuelve a guardar.";
        else
            error = "No se pudo reemplazar el paquete anterior.";
        return false;
    }

    if (progress) progress->store(1.0f);
    return true;
}


// ---------------------------------------------------------------- apertura ---

namespace {

/// Cabecera leida y claves ya derivadas: lo comun a abrir, verificar e informar.
struct Abierto {
    std::vector<u8>    master;
    std::vector<u8>    packId;
    std::vector<Entry> files;
    std::vector<Block> blocks;
    u64                dataStart = 0;

    ~Abierto() {
        if (!master.empty()) SecureZeroMemory(master.data(), master.size());
    }
};

/// Lee cabecera e indice de un paquete v3 y deja las claves listas.
bool abrirCabecera(File& f, const std::string& secret, Abierto& a,
                   std::string& error) {
    const u64 total = f.size();
    if (total < kHeaderBytes) {
        error = "El paquete del juego esta incompleto.";
        return false;
    }

    std::vector<u8> head(kHeaderBytes);
    if (!f.seek(0) || !f.readAll(head.data(), head.size())) {
        error = "No se pudo leer el paquete del juego.";
        return false;
    }
    if (getU32(&head[0]) != kMagic || getU32(&head[4]) != kVersion) {
        error = "El paquete del juego no tiene un formato reconocible.";
        return false;
    }

    size_t p = 8;
    const std::vector<u8> argonSalt(head.begin() + p, head.begin() + p + kSaltBytes);
    p += kSaltBytes;
    a.packId.assign(head.begin() + p, head.begin() + p + kPackIdBytes);
    p += kPackIdBytes;
    const u64 idxLen = getU64(&head[p]); p += 8;
    const u8* idxTag = &head[p];

    // El indice se reserva segun lo que dice la cabecera, asi que hay que
    // comprobar que ese tamano cabe de verdad en el archivo ANTES de reservar:
    // una cabecera alterada podria pedir gigabytes.
    if (idxLen == 0 || idxLen > total - kHeaderBytes) {
        error = "El paquete del juego esta danado.";
        return false;
    }

    std::vector<u8> idxCipher(static_cast<size_t>(idxLen));
    if (!f.readAll(idxCipher.data(), idxLen)) {
        error = "No se pudo leer el indice del paquete.";
        return false;
    }

    if (!masterKey(secret, argonSalt, a.master)) {
        error = "Fallo al derivar la clave del paquete.";
        return false;
    }

    std::vector<u8> idxPlain;
    if (!openIndex(a.master, a.packId.data(), idxCipher, idxTag, idxPlain)) {
        // El tag del indice no cuadra: o el secreto no es el que sello el
        // paquete, o el archivo fue alterado. No se distingue a proposito.
        error = "El paquete del juego esta danado o alterado.";
        return false;
    }

    if (!readIndex(idxPlain, a.files, a.blocks)) {
        error = "El indice del paquete del juego esta danado.";
        return false;
    }

    a.dataStart = kHeaderBytes + idxLen;

    // Cada bloque ha de caer DENTRO del archivo. Sin esto, un indice alterado
    // haria leer fuera de rango.
    for (const auto& b : a.blocks) {
        if (b.fileOffset < a.dataStart || b.storedLen == 0 ||
            b.fileOffset + b.storedLen > total) {
            error = "El paquete del juego esta danado.";
            return false;
        }
    }
    return true;
}

/// Descifra y descomprime un bloque ya leido de disco.
bool abrirBloque(const Abierto& a, u64 index, const std::vector<u8>& cipher,
                 std::vector<u8>& plain, std::vector<u8>& ws) {
    const Block& b = a.blocks[static_cast<size_t>(index)];
    std::vector<u8> crudo;
    if (!openBlock(a.master, a.packId.data(), index, cipher, b.tag, crudo))
        return false;
    return decompressBlock(b.codec, crudo.data(), crudo.size(), plain,
                           static_cast<size_t>(b.plainLen), ws);
}

/// Recorre los bloques en orden, descifrandolos en PARALELO, y entrega cada
/// uno en orden a `consumir`.
///
/// Leer del disco es secuencial a proposito (es lo que mas rinde en cualquier
/// disco), pero descifrar y descomprimir se reparte entre los nucleos. Antes
/// todo esto era un solo hilo sobre el paquete completo en memoria.
///
/// `consumir(indice, datos)` se llama SIEMPRE en orden de bloque y SIEMPRE
/// desde este hilo, asi que quien lo use no necesita sincronizar nada.
bool recorrerBloques(File& f, const Abierto& a,
                     std::atomic<float>* progress,
                     const std::atomic<bool>* cancel,
                     const std::function<bool(u64, std::vector<u8>&)>& consumir,
                     std::string& error) {
    const u64 n = a.blocks.size();
    if (n == 0) return true;

    const u32 nHilos = hilosUtiles();

    struct Tarea {
        u64 index = 0;
        std::vector<u8> cipher;
    };
    struct Salida {
        u64 index = 0;
        std::vector<u8> plain;
        bool ok = false;
    };

    std::mutex mx;
    std::condition_variable cvTarea, cvSalida;
    std::vector<Tarea> cola;
    std::vector<Salida> hechos;
    bool fin = false, fallo = false;

    const size_t topeCola = static_cast<size_t>(nHilos) * kQueuePerThread;

    auto trabajador = [&] {
        std::vector<u8> ws;
        for (;;) {
            Tarea t;
            {
                std::unique_lock<std::mutex> lk(mx);
                cvTarea.wait(lk, [&] { return !cola.empty() || fin || fallo; });
                if (fallo) return;
                if (cola.empty()) { if (fin) return; continue; }
                t = std::move(cola.back());
                cola.pop_back();
            }
            cvTarea.notify_one();   // hay hueco

            Salida s;
            s.index = t.index;
            s.ok = abrirBloque(a, t.index, t.cipher, s.plain, ws);

            const bool malo = !s.ok;
            {
                std::lock_guard<std::mutex> lk(mx);
                if (malo) fallo = true;
                hechos.push_back(std::move(s));
            }
            cvSalida.notify_all();
            if (malo) { cvTarea.notify_all(); return; }
        }
    };

    std::vector<std::thread> hilos;
    hilos.reserve(nHilos);
    for (u32 i = 0; i < nHilos; ++i) hilos.emplace_back(trabajador);

    struct Recoger {
        std::vector<std::thread>& h;
        std::mutex& m;
        std::condition_variable& c1;
        std::condition_variable& c2;
        bool& fin;
        ~Recoger() {
            { std::lock_guard<std::mutex> lk(m); fin = true; }
            c1.notify_all();
            c2.notify_all();
            for (auto& t : h) if (t.joinable()) t.join();
        }
    } recoger{hilos, mx, cvTarea, cvSalida, fin};

    // El consumidor necesita los bloques EN ORDEN (los va concatenando para
    // reconstruir los archivos), pero los hilos los terminan desordenados.
    // Esta tabla guarda los que llegan adelantados hasta que toca su turno.
    std::vector<Salida> guardados;
    u64 siguiente = 0;
    u64 leidos = 0;
    bool ok = true;

    auto entregarEnOrden = [&]() -> bool {
        for (;;) {
            size_t k = guardados.size();
            for (size_t i = 0; i < guardados.size(); ++i)
                if (guardados[i].index == siguiente) { k = i; break; }
            if (k == guardados.size()) return true;

            Salida s = std::move(guardados[k]);
            guardados.erase(guardados.begin() + k);
            if (!s.ok) {
                error = "El paquete del juego esta danado o alterado.";
                return false;
            }
            if (!consumir(s.index, s.plain)) return false;
            ++siguiente;

            if (progress) {
                const f32 t = static_cast<f32>(siguiente) / static_cast<f32>(n);
                progress->store(0.02f + 0.96f * t);
            }
        }
    };

    // Cuantos bloques como mucho puede adelantarse la lectura respecto al que
    // toca entregar.
    //
    // ESTO ES LO QUE MANTIENE PLANA LA MEMORIA AL INSTALAR. Sin este tope, la
    // cola de entrada estaba limitada pero la de SALIDA no: los bloques que
    // terminaban antes de su turno se iban acumulando a la espera de que
    // llegara el que faltaba, y como cada uno pesa hasta 8 MiB
    // descomprimido, instalar un juego de 1.2 GB llegaba a 796 MB de memoria
    // en vez de los ~220 MB del guardado. Con el tope, el adelanto maximo es
    // fijo y la memoria no depende del tamano del juego.
    const u64 ventana = static_cast<u64>(topeCola) + nHilos + 4;

    while (siguiente < n && ok) {
        // 1) Alimentar la cola leyendo del archivo, sin pasarse del tope ni
        //    adelantarse mas de `ventana` bloques del que toca entregar.
        while (leidos < n && leidos < siguiente + ventana) {
            {
                std::unique_lock<std::mutex> lk(mx);
                if (fallo) { ok = false; break; }
                if (cola.size() >= topeCola) break;
            }
            if (cancel && cancel->load()) {
                error = "Se cancelo la instalacion.";
                ok = false;
                break;
            }

            const Block& b = a.blocks[static_cast<size_t>(leidos)];
            Tarea t;
            t.index = leidos;
            t.cipher.resize(static_cast<size_t>(b.storedLen));
            if (!f.seek(b.fileOffset) ||
                !f.readAll(t.cipher.data(), b.storedLen)) {
                error = "No se pudo leer el paquete del juego.";
                ok = false;
                break;
            }
            ++leidos;
            {
                std::lock_guard<std::mutex> lk(mx);
                cola.push_back(std::move(t));
            }
            cvTarea.notify_one();
        }
        if (!ok) break;

        // 2) Recoger lo que haya terminado y entregarlo en orden.
        //
        // Hay que DORMIRSE siempre que no se pueda avanzar, y decidirlo bajo el
        // mismo candado con que se mira el estado.
        //
        // BUG QUE ESTO ARREGLA: antes solo se esperaba si `hechos` estaba
        // vacio. Pero tener bloques hechos no significa poder avanzar: si
        // ninguno es el que toca entregar, `entregarEnOrden` no entrega nada,
        // la lectura ya esta frenada por el tope, y el bucle exterior se queda
        // girando en vacio. El sintoma era el autodiagnostico "colgado" en una
        // prueba, con el proceso al 100% de CPU, memoria plana y sin avanzar:
        // no era un bloqueo mutuo, era una espera activa.
        {
            std::unique_lock<std::mutex> lk(mx);

            // Se pasa a la bandeja local lo que haya llegado.
            for (auto& h : hechos) guardados.push_back(std::move(h));
            hechos.clear();

            if (fallo) {
                ok = false;
            } else if (siguiente < n) {
                // ¿Esta ya entre los recogidos el bloque que toca?
                bool tengoElQueToca = false;
                for (const auto& g : guardados)
                    if (g.index == siguiente) { tengoElQueToca = true; break; }

                const bool puedeLeerMas = leidos < n &&
                                          leidos < siguiente + ventana &&
                                          cola.size() < topeCola;

                // Sin el bloque que toca y sin nada que leer, lo unico que
                // puede desatascar es que un trabajador termine. Se espera a
                // eso en vez de volver a dar la vuelta.
                if (!tengoElQueToca && !puedeLeerMas) {
                    cvSalida.wait(lk, [&] { return !hechos.empty() || fallo; });
                    for (auto& h : hechos) guardados.push_back(std::move(h));
                    hechos.clear();
                    if (fallo) ok = false;
                }
            }
        }
        if (!ok) break;
        if (!entregarEnOrden()) { ok = false; break; }
    }

    if (!ok && error.empty())
        error = "El paquete del juego esta danado o alterado.";
    return ok && siguiente == n;
}

} // namespace

bool isV3(const std::wstring& path) {
    File f;
    if (!f.openRead(path)) return false;
    u8 head[8];
    if (!f.readAll(head, sizeof(head))) return false;
    return getU32(head) == kMagic && getU32(head + 4) == kVersion;
}

bool unsealFolder(const std::wstring& srcPath,
                  const std::wstring& destDir,
                  const std::string&  secret,
                  std::atomic<float>* progress,
                  std::string&        error,
                  const std::atomic<bool>* cancel,
                  const Inspector&         inspector) {
    File f;
    if (!f.openRead(srcPath)) {
        error = "No se encontro el paquete de archivos del juego.";
        return false;
    }

    Abierto a;
    if (!abrirCabecera(f, secret, a, error)) return false;

    // Todas las rutas se validan ANTES de escribir nada: si una sola intenta
    // salirse de la carpeta, no se crea ni un archivo. Comprobarlo a medias
    // dejaria el destino a medio escribir con parte del contenido malicioso ya
    // en disco.
    for (const auto& e : a.files) {
        if (!safeRelPath(fromUtf8(e.rel))) {
            error = "El paquete del juego contiene una ruta no permitida.";
            return false;
        }
    }

    makeDirs(destDir);

    // Se escriben los archivos siguiendo el flujo logico: cada bloque que
    // llega se reparte entre los archivos que le corresponden por offset.
    size_t idxFile = 0;
    u64    escritoEnFile = 0;
    u64    posFlujo = 0;
    File   actual;
    bool   actualAbierto = false;

    auto cerrarActual = [&] {
        if (actualAbierto) { CloseHandle(actual.h); actual.h = INVALID_HANDLE_VALUE; }
        actualAbierto = false;
    };

    auto consumir = [&](u64 /*index*/, std::vector<u8>& plain) -> bool {
        // --- la revision va ANTES de escribir ---
        //
        // El bloque ya esta descifrado y descomprimido en memoria, asi que
        // mirarlo aqui no cuesta ni una lectura de disco de mas: es analizar
        // "mientras se instala" en el sentido literal. Y si hay algo, se corta
        // antes de que ese contenido toque el disco del jugador.
        if (inspector && !plain.empty()) {
            std::string motivo;
            if (!inspector(plain.data(), plain.size(), motivo)) {
                error = motivo.empty()
                    ? std::string("La revision de seguridad detuvo la "
                                  "instalacion del juego.")
                    : motivo;
                return false;
            }
        }

        size_t off = 0;
        while (off < plain.size()) {
            // Saltar archivos de tamano 0: hay que crearlos, pero no consumen
            // bytes del flujo.
            while (idxFile < a.files.size() && a.files[idxFile].size == 0) {
                const std::wstring dst = destDir + L"\\" + fromUtf8(a.files[idxFile].rel);
                makeDirs(parentOf(dst));
                File vacio;
                if (!vacio.openWrite(dst)) {
                    error = "No se pudo crear un archivo del juego.";
                    return false;
                }
                ++idxFile;
            }
            if (idxFile >= a.files.size()) break;

            const Entry& e = a.files[idxFile];
            if (!actualAbierto) {
                const std::wstring dst = destDir + L"\\" + fromUtf8(e.rel);
                makeDirs(parentOf(dst));
                if (!actual.openWrite(dst)) {
                    error = "No se pudo crear un archivo del juego.";
                    return false;
                }
                actualAbierto = true;
                escritoEnFile = 0;
            }

            const u64 faltan = e.size - escritoEnFile;
            const size_t trozo =
                static_cast<size_t>((std::min)(faltan, plain.size() - off));
            if (trozo > 0 && !actual.writeAll(plain.data() + off, trozo)) {
                error = "No se pudo escribir un archivo del juego. Comprueba "
                        "que queda sitio en el disco.";
                return false;
            }
            off           += trozo;
            escritoEnFile += trozo;
            posFlujo      += trozo;

            if (escritoEnFile >= e.size) { cerrarActual(); ++idxFile; }
        }
        return true;
    };

    const bool ok = recorrerBloques(f, a, progress, cancel, consumir, error);
    cerrarActual();

    if (!ok) return false;

    // Los archivos vacios que quedaran al final del indice.
    while (idxFile < a.files.size()) {
        const Entry& e = a.files[idxFile];
        if (e.size == 0) {
            const std::wstring dst = destDir + L"\\" + fromUtf8(e.rel);
            makeDirs(parentOf(dst));
            File vacio;
            if (!vacio.openWrite(dst)) {
                error = "No se pudo crear un archivo del juego.";
                return false;
            }
        }
        ++idxFile;
    }

    if (progress) progress->store(1.0f);
    return true;
}

bool verify(const std::wstring& srcPath,
            const std::string&  secret,
            std::atomic<float>* progress,
            std::string&        error) {
    File f;
    if (!f.openRead(srcPath)) {
        error = "No se encontro el paquete del juego.";
        return false;
    }

    Abierto a;
    if (!abrirCabecera(f, secret, a, error)) return false;

    // Se descifra TODO y se descarta: no toca el disco, solo comprueba que
    // cada bloque cuadra con su tag. Es lo que permite decir "el paquete esta
    // bien" sin gastar espacio ni extraer nada.
    u64 suma = 0;
    auto consumir = [&](u64, std::vector<u8>& plain) -> bool {
        suma += plain.size();
        return true;
    };
    if (!recorrerBloques(f, a, progress, nullptr, consumir, error)) return false;

    // El total descomprimido tiene que coincidir con lo que dice el indice.
    u64 esperado = 0;
    for (const auto& e : a.files) esperado += e.size;
    if (suma != esperado) {
        error = "El paquete del juego no cuadra con su indice.";
        return false;
    }
    return true;
}

bool info(const std::wstring& srcPath, const std::string& secret,
          Info& out, std::string& error) {
    File f;
    if (!f.openRead(srcPath)) {
        error = "No se encontro el paquete del juego.";
        return false;
    }
    const u64 enDisco = f.size();

    Abierto a;
    if (!abrirCabecera(f, secret, a, error)) return false;

    out.files  = a.files.size();
    out.bytes  = 0;
    for (const auto& e : a.files) out.bytes += e.size;
    out.blocks = a.blocks.size();
    out.packedBytes = enDisco;
    return true;
}

} // namespace tomatepack
} // namespace ludora
