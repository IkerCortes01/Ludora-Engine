#include "app/GameInstall.h"
#include "app/Vault.h"
#include "app/Device.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace ludora {

namespace {

std::wstring parentDir(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? L"." : path.substr(0, s);
}

/// Enumera todos los archivos bajo `dir` (recursivo), como rutas relativas.
void listFiles(const std::wstring& dir, const std::wstring& rel,
               std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring childRel = rel.empty() ? name : (rel + L"\\" + name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            listFiles(dir + L"\\" + name, childRel, out);
        else
            out.push_back(childRel);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/// Crea la carpeta y todos sus padres.
void makeDirs(const std::wstring& dir) {
    if (dir.empty() || GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;
    makeDirs(parentDir(dir));
    CreateDirectoryW(dir.c_str(), nullptr);
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

} // namespace

GameInstall::~GameInstall() { join(); }

void GameInstall::join() {
    if (m_thread.joinable()) m_thread.join();
}

bool GameInstall::start(const std::wstring& sourceExe,
                        const std::wstring& destDir,
                        const std::string&  name,
                        const std::wstring& exeName) {
    if (m_running.load()) return false;

    m_running.store(true);
    m_done.store(false);
    m_failed.store(false);
    m_progress.store(0.0f);
    m_error.clear();
    (void)name;

    // El worker copia; el hilo principal solo consulta el progreso.
    m_thread = std::thread(&GameInstall::worker, this, sourceExe, destDir, exeName);
    return true;
}

void GameInstall::worker(std::wstring sourceExe, std::wstring destDir,
                         std::wstring exeName) {
    const std::wstring srcDir = parentDir(sourceExe);

    std::vector<std::wstring> files;
    listFiles(srcDir, L"", files);

    if (files.empty()) {
        m_error   = "La carpeta de origen esta vacia o no existe.";
        m_failed.store(true);
        m_running.store(false);
        return;
    }

    // La carpeta del juego se guarda CIFRADA en la Nube Tomate (un unico
    // .tomate), no en claro. `destDir` designa la ubicacion; el paquete es
    // destDir + ".tomate". El exe se apunta a donde se descifrara al jugar.
    makeDirs(parentDir(destDir));
    const std::wstring vaultPath = std::wstring(destDir) + L".tomate";

    std::string err;
    if (!gamevault::sealFolder(sourceExe, vaultPath, &m_progress, err)) {
        m_error = err;
        m_failed.store(true);
        m_running.store(false);
        return;
    }

    // El "exe instalado" es el que se lanzara tras descifrar en tiempo de
    // juego; se guarda su ruta prevista y el paquete cifrado que lo contiene.
    m_installedExe = std::wstring(destDir) + L"\\" + exeName;
    m_vaultPath    = vaultPath;
    m_exeName      = exeName;
    m_progress.store(1.0f);
    m_done.store(true);
    m_running.store(false);
}

// -------------------------------------------------------------- gamevault ---
namespace gamevault {

namespace {

void appendU32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF));
}
u32 readU32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

// --- Compresion con la API nativa de Windows (ntdll), sin dependencias. ---
typedef LONG (WINAPI *RtlGetCompressionWorkSpaceSize_t)(USHORT, PULONG, PULONG);
typedef LONG (WINAPI *RtlCompressBuffer_t)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG,
                                           ULONG, PULONG, PVOID);
typedef LONG (WINAPI *RtlDecompressBufferEx_t)(USHORT, PUCHAR, ULONG, PUCHAR,
                                               ULONG, PULONG, PVOID);

/// Cabecera de un bloque comprimido: marca + tamano original.
constexpr u32 kCompMagic = 0x504D4F43;   // "COMP"

/// Comprime `in` a `out`. Si no gana espacio o falla, guarda sin comprimir.
/// El formato siempre lleva cabecera para saber como descomprimir.
bool compress(const std::vector<u8>& in, std::vector<u8>& out) {
    out.clear();
    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto getWs = reinterpret_cast<RtlGetCompressionWorkSpaceSize_t>(
        nt ? GetProcAddress(nt, "RtlGetCompressionWorkSpaceSize") : nullptr);
    auto comp = reinterpret_cast<RtlCompressBuffer_t>(
        nt ? GetProcAddress(nt, "RtlCompressBuffer") : nullptr);

    // Modo LZNT1 con maxima compresion.
    const USHORT fmt = 2 /*COMPRESSION_FORMAT_LZNT1*/ | 0x0100 /*ENGINE_MAXIMUM*/;

    auto guardarSinComprimir = [&]() {
        appendU32(out, kCompMagic);
        appendU32(out, 0);   // 0 = sin comprimir
        appendU32(out, static_cast<u32>(in.size()));
        out.insert(out.end(), in.begin(), in.end());
        return true;
    };

    if (!getWs || !comp || in.empty()) return guardarSinComprimir();

    ULONG wsSize = 0, fragSize = 0;
    if (getWs(fmt, &wsSize, &fragSize) < 0) return guardarSinComprimir();

    std::vector<u8> ws(wsSize);
    std::vector<u8> dst(in.size());   // no aceptamos que crezca
    ULONG finalSize = 0;
    const LONG r = comp(fmt,
                        const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()),
                        dst.data(), static_cast<ULONG>(dst.size()),
                        fragSize, &finalSize, ws.data());
    if (r < 0 || finalSize == 0 || finalSize >= in.size())
        return guardarSinComprimir();   // no comprimio o no gano: crudo

    appendU32(out, kCompMagic);
    appendU32(out, 1);   // 1 = comprimido LZNT1
    appendU32(out, static_cast<u32>(in.size()));   // tamano original
    out.insert(out.end(), dst.begin(), dst.begin() + finalSize);
    return true;
}

/// Deshace compress(). Reconstruye el tamano original exacto.
bool decompress(const std::vector<u8>& in, std::vector<u8>& out) {
    if (in.size() < 12 || readU32(&in[0]) != kCompMagic) return false;
    const u32 modo = readU32(&in[4]);
    const u32 orig = readU32(&in[8]);
    const u8* data = in.data() + 12;
    const size_t dataLen = in.size() - 12;

    if (modo == 0) {   // sin comprimir
        out.assign(data, data + dataLen);
        return out.size() == orig;
    }

    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto dec = reinterpret_cast<RtlDecompressBufferEx_t>(
        nt ? GetProcAddress(nt, "RtlDecompressBufferEx") : nullptr);
    if (!dec) return false;

    const USHORT fmt = 2 /*LZNT1*/;
    out.assign(orig, 0);
    ULONG produced = 0;
    // El workspace de descompresion cabe en el mismo tamano que el de compresion.
    std::vector<u8> ws(1 << 16);
    const LONG r = dec(fmt, out.data(), orig,
                       const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen),
                       &produced, ws.data());
    if (r < 0) return false;
    out.resize(produced);
    return produced == orig;
}

/// Serializa una carpeta a un blob: [n] luego, por archivo,
/// [len ruta][ruta utf8][len datos][datos].
bool packFolder(const std::wstring& dir, std::vector<u8>& out) {
    std::vector<std::wstring> files;
    listFiles(dir, L"", files);
    if (files.empty()) return false;

    appendU32(out, static_cast<u32>(files.size()));
    for (const auto& rel : files) {
        const std::string relU8 = toUtf8(rel);
        appendU32(out, static_cast<u32>(relU8.size()));
        out.insert(out.end(), relU8.begin(), relU8.end());

        const std::wstring full = dir + L"\\" + rel;
        std::ifstream f(full.c_str(), std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize n = f.tellg();
        f.seekg(0);
        appendU32(out, static_cast<u32>(n));
        const size_t base = out.size();
        out.resize(base + static_cast<size_t>(n));
        if (n > 0) f.read(reinterpret_cast<char*>(out.data() + base), n);
    }
    return true;
}

/// Reconstruye la carpeta desde el blob.
bool unpackFolder(const std::vector<u8>& blob, const std::wstring& dir) {
    if (blob.size() < 4) return false;
    size_t p = 0;
    const u32 count = readU32(&blob[p]); p += 4;

    for (u32 i = 0; i < count; ++i) {
        if (p + 4 > blob.size()) return false;
        const u32 rl = readU32(&blob[p]); p += 4;
        if (p + rl > blob.size()) return false;
        const std::string relU8(reinterpret_cast<const char*>(&blob[p]), rl); p += rl;
        const std::wstring rel = fromUtf8(relU8);

        if (p + 4 > blob.size()) return false;
        const u32 dl = readU32(&blob[p]); p += 4;
        if (p + dl > blob.size()) return false;

        const std::wstring dst = dir + L"\\" + rel;
        makeDirs(parentDir(dst));
        std::ofstream f(dst.c_str(), std::ios::binary);
        if (!f) return false;
        if (dl > 0) f.write(reinterpret_cast<const char*>(&blob[p]), dl);
        p += dl;
    }
    return true;
}

} // namespace

bool sealFolder(const std::wstring& sourceExe,
                const std::wstring& destVault,
                std::atomic<float>* progress,
                std::string& error) {
    const std::wstring srcDir = parentDir(sourceExe);

    if (progress) progress->store(0.05f);
    std::vector<u8> blob;
    if (!packFolder(srcDir, blob)) {
        error = "No se pudo leer la carpeta del juego.";
        return false;
    }

    // Comprimir ANTES de cifrar: cifrar primero haria los datos incompresibles
    // (parecen aleatorios), asi que el orden importa.
    if (progress) progress->store(0.25f);
    std::vector<u8> comprimido;
    if (!compress(blob, comprimido)) {
        error = "No se pudo comprimir el juego.";
        return false;
    }
    blob.clear(); blob.shrink_to_fit();   // liberar el crudo, puede ser grande

    if (progress) progress->store(0.45f);
    // El secreto es la huella del equipo: el paquete solo se abre aqui.
    const std::string secret = Device::fingerprint("ludora-nube-tomate-game-v1");
    if (secret.empty()) { error = "No se pudo obtener la huella del equipo."; return false; }

    std::vector<u8> sealed;
    if (!Vault::seal(secret, comprimido, sealed, error)) return false;

    if (progress) progress->store(0.9f);
    makeDirs(parentDir(destVault));
    if (!Vault::saveFile(destVault, sealed)) {
        error = "No se pudo guardar el paquete cifrado.";
        return false;
    }
    if (progress) progress->store(1.0f);
    return true;
}

bool openToFolder(const std::wstring& srcVault,
                  const std::wstring& destDir,
                  const std::wstring& exeName,
                  std::wstring& outExe,
                  std::string& error) {
    std::vector<u8> sealed;
    if (!Vault::loadFile(srcVault, sealed) || sealed.empty()) {
        error = "No se encontro el paquete cifrado del juego.";
        return false;
    }

    const std::string secret = Device::fingerprint("ludora-nube-tomate-game-v1");
    if (secret.empty()) { error = "No se pudo obtener la huella del equipo."; return false; }

    std::vector<u8> comprimido;
    if (!Vault::open(secret, sealed, comprimido, error)) return false;   // clave/equipo

    // Descomprimir para recuperar el blob original de la carpeta.
    std::vector<u8> blob;
    if (!decompress(comprimido, blob)) {
        error = "No se pudo descomprimir el juego.";
        return false;
    }

    makeDirs(destDir);
    if (!unpackFolder(blob, destDir)) {
        error = "No se pudo desempaquetar el juego.";
        return false;
    }

    outExe = destDir + L"\\" + exeName;
    if (GetFileAttributesW(outExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "El juego no quedo completo al descifrar.";
        return false;
    }

    // Marca de "descifrado completo": se escribe SOLO cuando todos los
    // archivos (exe + texturas + datos) ya estan en su sitio. Asi la proxima
    // vez se puede reutilizar la carpeta con la garantia de que esta entera,
    // en vez de fiarse de que solo el .exe exista (que dejaria texturas a
    // medias si un descifrado anterior se corto).
    std::ofstream(destDir + L"\\.ludora_ready", std::ios::binary) << "ok";
    return true;
}

} // namespace gamevault

// ------------------------------------------------------------- biblioteca ---
namespace library {

std::vector<Entry> load(const std::wstring& path) {
    std::vector<Entry> out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return out;

    std::string line;
    Entry cur;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if (k == "name") {
            if (!cur.name.empty()) { out.push_back(cur); cur = Entry{}; }
            cur.name = v;
        } else if (k == "exe") {
            cur.exe = fromUtf8(v);
        } else if (k == "vault") {
            cur.vault = fromUtf8(v);
        } else if (k == "exename") {
            cur.exeName = fromUtf8(v);
        }
    }
    if (!cur.name.empty()) out.push_back(cur);
    return out;
}

bool add(const std::wstring& path, const Entry& e) {
    std::vector<Entry> all = load(path);

    // Reemplaza si ya existe por nombre; si no, lo anade.
    bool found = false;
    for (auto& x : all)
        if (x.name == e.name) { x = e; found = true; break; }
    if (!found) all.push_back(e);

    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << "# Biblioteca de Ludora.\r\n";
    for (const auto& x : all)
        out << "name="    << x.name << "\r\n"
            << "exe="     << toUtf8(x.exe) << "\r\n"
            << "vault="   << toUtf8(x.vault) << "\r\n"
            << "exename=" << toUtf8(x.exeName) << "\r\n";
    return out.good();
}

bool has(const std::wstring& path, const std::string& name) {
    for (const auto& e : load(path))
        if (e.name == name) return true;
    return false;
}

/// Lanza el exe y devuelve el handle del proceso en `outProc` (o nullptr).
/// El llamante debe CloseHandle cuando termine de vigilarlo.
static bool launchProc(const std::wstring& exe, void** outProc) {
    if (outProc) *outProc = nullptr;
    if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    // ShellExecuteEx con SEE_MASK_NOCLOSEPROCESS devuelve el handle del
    // proceso, que ShellExecuteW no da: es lo que permite saber cuando el
    // juego sigue abierto para animar el boton.
    const std::wstring dir = parentDir(exe);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"open";
    sei.lpFile       = exe.c_str();
    sei.lpDirectory  = dir.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (outProc) *outProc = sei.hProcess;
    return true;
}

bool launch(const std::wstring& exe) {
    void* proc = nullptr;
    const bool ok = launchProc(exe, &proc);
    if (proc) CloseHandle(proc);   // esta variante no vigila el proceso
    return ok;
}

bool launchEntry(const Entry& e, const std::wstring& tempDir, std::string& error,
                 void** outProc) {
    if (outProc) *outProc = nullptr;

    // Juego cifrado en la Nube Tomate: descifrar a una carpeta temporal y
    // lanzar desde ahi. Si ya se descifro antes y el exe sigue, se reutiliza.
    if (!e.vault.empty()) {
        std::wstring exe = tempDir + L"\\" + e.exeName;
        // Se reutiliza la carpeta ya descifrada SOLO si esta la marca de
        // "descifrado completo": comprobar solo el .exe dejaria pasar una
        // carpeta con las texturas a medias.
        const std::wstring readyMark = tempDir + L"\\.ludora_ready";
        const bool completo =
            GetFileAttributesW(exe.c_str())       != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(readyMark.c_str()) != INVALID_FILE_ATTRIBUTES;

        if (!completo) {
            if (!gamevault::openToFolder(e.vault, tempDir, e.exeName, exe, error))
                return false;
        }
        if (!launchProc(exe, outProc)) { error = "No se pudo lanzar el juego."; return false; }
        return true;
    }

    // Juego en claro (compatibilidad).
    if (!e.exe.empty()) {
        if (!launchProc(e.exe, outProc)) { error = "No se pudo lanzar el juego."; return false; }
        return true;
    }

    error = "El juego no tiene ni paquete cifrado ni ejecutable.";
    return false;
}

} // namespace library
} // namespace ludora
