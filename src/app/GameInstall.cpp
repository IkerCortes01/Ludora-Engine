#include "app/GameInstall.h"
#include "app/Vault.h"
#include "app/Device.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
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

std::wstring toLowerW(std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    return s;
}

/// true si `dir` contiene una subcarpeta con ese nombre (case-insensitive).
bool hasChildDir(const std::wstring& dir, const std::wstring& childLower) {
    const std::wstring test = dir + L"\\" + childLower;
    const DWORD a = GetFileAttributesW(test.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/// Nombres de carpetas de recursos que un juego carga en runtime. Se instalan
/// junto al .exe aunque esten mas arriba en el arbol; el resto del proyecto
/// (codigo, build, docs...) NO se toca.
const wchar_t* const kResourceDirs[] = {
    L"resourcepacks", L"resourcepack", L"resources", L"resource",
    L"assets", L"asset", L"data", L"content", L"res", L"textures",
    L"gamedata", L"pak", L"paks", L"sounds", L"sound", L"audio",
    L"shaders", L"shader", L"fonts", L"font", L"media", L"levels",
};

/// true si `dir` contiene alguna carpeta de recursos conocida.
bool hasAnyResourceDir(const std::wstring& dir) {
    for (const wchar_t* r : kResourceDirs)
        if (hasChildDir(dir, r)) return true;
    return false;
}

/// Plan de instalacion de un juego: que copiar y donde queda el .exe.
struct GamePlan {
    std::wstring              exeDir;        // carpeta del .exe (sus vecinos van a la raiz del destino)
    std::wstring              exeName;       // nombre del ejecutable
    std::vector<std::wstring> resourceDirs;  // carpetas de recursos (rutas absolutas) a copiar como subcarpetas del destino
};

/// Decide QUE instalar partiendo del .exe. El .exe y sus vecinos van a la raiz
/// del destino. Ademas se buscan las carpetas de recursos (resourcepacks,
/// assets, sounds...) subiendo por el arbol: van como subcarpetas del destino,
/// para que el juego -- que sube desde su .exe buscandolas -- las encuentre.
/// NO se copia el proyecto entero: solo el .exe, sus vecinos y los recursos.
GamePlan planInstall(const std::wstring& sourceExe) {
    GamePlan plan;
    plan.exeDir  = parentDir(sourceExe);
    plan.exeName = sourceExe.substr(sourceExe.find_last_of(L"\\/") + 1);

    // Recopilar carpetas de recursos: primero junto al .exe, luego subiendo.
    // Se registra cada nombre de recurso una sola vez (el mas cercano al .exe).
    auto yaTiene = [&](const std::wstring& nombreLow) {
        for (const auto& d : plan.resourceDirs) {
            const std::wstring n = toLowerW(d.substr(d.find_last_of(L"\\/") + 1));
            if (n == nombreLow) return true;
        }
        return false;
    };

    std::wstring cur = plan.exeDir;
    const int maxSubidas = 6;   // cubre build/bin/Release y anidamientos similares
    for (int nivel = 0; nivel <= maxSubidas; ++nivel) {
        for (const wchar_t* r : kResourceDirs) {
            if (hasChildDir(cur, r) && !yaTiene(toLowerW(r)))
                plan.resourceDirs.push_back(cur + L"\\" + r);
        }
        const std::wstring padre = parentDir(cur);
        if (padre.empty() || padre == cur || padre == L".") break;
        cur = padre;
    }
    return plan;
}

/// Compat: devuelve la carpeta del .exe como "raiz" y el exe relativo. Se
/// conserva para las pruebas y usos que solo necesitan localizar el arbol.
std::wstring findGameRoot(const std::wstring& sourceExe, std::wstring& exeRelOut) {
    const std::wstring exeDir  = parentDir(sourceExe);
    const std::wstring exeName = sourceExe.substr(sourceExe.find_last_of(L"\\/") + 1);
    // Si hay recursos arriba, la "raiz" logica es la primera carpeta que los
    // tiene; si no, la del .exe. El exe se reporta relativo a esa raiz.
    std::wstring rel = exeName;
    std::wstring cur = exeDir;
    if (hasAnyResourceDir(exeDir)) { exeRelOut = exeName; return exeDir; }
    for (int i = 0; i < 6; ++i) {
        const std::wstring padre = parentDir(cur);
        if (padre.empty() || padre == cur || padre == L".") break;
        rel = cur.substr(cur.find_last_of(L"\\/") + 1) + L"\\" + rel;
        if (hasAnyResourceDir(padre)) { exeRelOut = rel; return padre; }
        cur = padre;
    }
    exeRelOut = exeName;
    return exeDir;
}

/// Decide si un archivo del juego es un DATO SENSIBLE (se cifra) o un ASSET
/// (se deja en claro y accesible). Los assets ejecutan el juego: exe, dll,
/// texturas, sonidos, modelos... y quedan expuestos a proposito. Los datos
/// del jugador (partidas, config, progreso) se cifran.
bool isSensitiveData(const std::wstring& rel) {
    const std::wstring low = toLowerW(rel);

    // Assets: nunca se cifran, ni aunque esten en una subcarpeta "protegida".
    static const wchar_t* kAssetExt[] = {
        L".exe", L".dll", L".so", L".dylib",                 // ejecutables/librerias
        L".png", L".jpg", L".jpeg", L".bmp", L".tga", L".dds", L".gif", L".webp", // texturas
        L".wav", L".ogg", L".mp3", L".flac", L".xm", L".mod", // sonidos/musica
        L".ttf", L".otf", L".fnt",                            // fuentes
        L".obj", L".fbx", L".gltf", L".glb", L".vox",         // modelos
        L".shader", L".glsl", L".hlsl", L".spv",              // shaders
        L".pak", L".zip", L".assets", L".bank",               // paquetes de assets
        // Con el cifrado por defecto, la lista blanca tiene que cubrir TODO
        // lo que un juego abre directamente del disco. Si algo de aqui se
        // cifrara, el juego no arrancaria.
        L".ico", L".cur", L".svg", L".psd",                   // mas imagenes
        L".opus", L".aiff", L".mid", L".midi", L".s3m", L".it", // mas audio
        L".dae", L".3ds", L".blend", L".md5mesh", L".mtl",    // mas modelos
        L".vert", L".frag", L".geom", L".comp", L".fx",       // mas shaders
        L".manifest", L".pdb", L".lib", L".pyd", L".jar",     // runtime
        L".txt", L".md", L".license", L".readme",             // texto de acompanamiento
        L".mp4", L".webm", L".avi", L".ogv",                  // video
        L".atlas", L".sprite", L".anim", L".tileset",         // datos de dibujo
    };
    for (const wchar_t* ext : kAssetExt) {
        const std::wstring e = ext;
        if (low.size() >= e.size() &&
            low.compare(low.size() - e.size(), e.size(), e) == 0)
            return false;   // es asset -> NO cifrar
    }

    // Datos sensibles: partidas y configuracion del jugador.
    if (low.find(L"save") != std::wstring::npos ||   // saves\, savegame...
        low.find(L"partida") != std::wstring::npos)
        return true;

    static const wchar_t* kDataExt[] = {
        L".dat", L".sav", L".save", L".cfg", L".ini", L".json",
        L".journal", L".vxr", L".db", L".profile", L".progress",
    };
    for (const wchar_t* ext : kDataExt) {
        const std::wstring e = ext;
        if (low.size() >= e.size() &&
            low.compare(low.size() - e.size(), e.size(), e) == 0)
            return true;    // es dato -> cifrar
    }

    // Por defecto se CIFRA.
    //
    // La lista blanca de arriba (ejecutables, texturas, sonidos, fuentes,
    // modelos, shaders y paquetes de assets) recoge lo que el juego necesita
    // leer directo del disco. Todo lo demas -- scripts, mapas, tablas,
    // formatos propios, extensiones que no conocemos -- se protege.
    //
    // Es lo contrario de lo que hacia antes, y es la decision correcta: si
    // aparece un formato nuevo, el riesgo de dejarlo expuesto es peor que el
    // de cifrarlo. Un asset mal clasificado se arregla anadiendolo a la lista
    // blanca; un dato del jugador expuesto ya no se puede des-exponer.
    return true;
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

    // Un hilo anterior que termino pero no se recogio sigue siendo joinable,
    // y asignar sobre el llama a std::terminate(). Pasa al reinstalar tras un
    // fallo, o cuando nadie llamo a join() entre dos instalaciones.
    if (m_thread.joinable()) m_thread.join();

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
    // Una excepcion que escape de un hilo mata el proceso entero sin dialogo.
    // Se captura aqui y se comunica como un fallo de instalacion normal, que
    // es algo de lo que la interfaz si sabe recuperarse.
    try {
        workerBody(std::move(sourceExe), std::move(destDir), std::move(exeName));
    } catch (const std::exception& e) {
        m_error = std::string("Error al instalar: ") + e.what();
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
    } catch (...) {
        m_error = "Error desconocido al instalar.";
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
    }
}

void GameInstall::workerBody(std::wstring sourceExe, std::wstring destDir,
                             std::wstring exeName) {
    (void)exeName;

    // Plan de instalacion: el .exe y sus vecinos van a la RAIZ del destino;
    // las carpetas de recursos (resourcepacks, assets, sounds...), aunque esten
    // mas arriba en el arbol, van como subcarpetas del destino. Asi el juego
    // -- que sube desde su .exe buscando esas carpetas -- las encuentra. NO se
    // copia el proyecto entero (codigo, build, docs): solo lo jugable.
    const GamePlan plan = planInstall(sourceExe);

    // Recolectar (origen, relativoDestino) de todo lo que hay que copiar.
    struct Item { std::wstring src, rel; };
    std::vector<Item> items;

    // (a) Vecinos del .exe -> a la raiz del destino.
    {
        std::vector<std::wstring> vecinos;
        listFiles(plan.exeDir, L"", vecinos);
        for (const auto& r : vecinos)
            items.push_back({ plan.exeDir + L"\\" + r, r });
    }
    // (b) Carpetas de recursos -> como subcarpeta homonima del destino.
    for (const auto& resDir : plan.resourceDirs) {
        const std::wstring nombre = resDir.substr(resDir.find_last_of(L"\\/") + 1);
        std::vector<std::wstring> dentro;
        listFiles(resDir, L"", dentro);
        for (const auto& r : dentro)
            items.push_back({ resDir + L"\\" + r, nombre + L"\\" + r });
    }

    if (items.empty()) {
        m_error   = "La carpeta de origen esta vacia o no existe.";
        m_failed.store(true);
        m_running.store(false);
        return;
    }

    makeDirs(destDir);

    // 1) Copiar ASSETS en claro; los DATOS sensibles se saltan (van cifrados).
    //    Se empaqueta a la vez la lista de datos sensibles para el paso 2.
    const size_t total = items.size();
    std::vector<Item> sensibles;
    for (size_t i = 0; i < total; ++i) {
        if (gamevault::isSensitiveData(items[i].rel)) {
            sensibles.push_back(items[i]);
            continue;   // dato del jugador: no va en claro
        }
        const std::wstring dst = destDir + L"\\" + items[i].rel;
        makeDirs(parentDir(dst));
        if (!CopyFileW(items[i].src.c_str(), dst.c_str(), FALSE)) {
            m_error = "No se pudo copiar un asset del juego.";
            m_failed.store(true);
            m_running.store(false);
            return;
        }
        m_progress.store(0.05f + 0.70f * static_cast<f32>(i + 1) / total);
    }

    // 2) Cifrar SOLO los datos sensibles (partidas, config) en un paquete
    //    aparte, con sus rutas relativas al destino, para restaurarlos ahi.
    const std::wstring vaultPath = std::wstring(destDir) + L".datos.tomate";
    std::string err;
    if (!gamevault::sealDataItems(sensibles.size(),
            [&](size_t k, std::wstring& outSrc, std::wstring& outRel) {
                outSrc = sensibles[k].src; outRel = sensibles[k].rel;
            }, vaultPath, &m_progress, err)) {
        m_error = err;
        m_failed.store(true);
        m_running.store(false);
        return;
    }

    // El exe queda EN CLARO en la raiz del destino (junto a resourcepacks).
    m_installedExe = std::wstring(destDir) + L"\\" + plan.exeName;
    m_vaultPath    = vaultPath;
    m_exeName      = plan.exeName;
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

    // LZNT1 con el motor ESTANDAR, no el maximo.
    //
    // ENGINE_MAXIMUM es varias veces mas lento y, sobre datos de un juego
    // --texturas y audio que ya vienen comprimidos--, apenas gana nada: el
    // grueso del paquete es incompresible de todas formas. El tiempo que
    // ahorra se nota mucho mas que los pocos puntos que cuesta.
    //
    // El formato no cambia: descomprimir no distingue con que motor se
    // comprimio, asi que los paquetes ya guardados se abren igual.
    const USHORT fmt = 2 /*COMPRESSION_FORMAT_LZNT1*/ | 0x0000 /*ENGINE_STANDARD*/;

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
/// Empaqueta una carpeta. Si `soloSensibles` es true, empaqueta unicamente los
/// archivos de datos del jugador (los assets se dejan para copiarlos en claro).
bool packFolderFiltered(const std::wstring& dir, std::vector<u8>& out,
                        bool soloSensibles,
                        std::atomic<float>* progress = nullptr,
                        f32 desde = 0.0f, f32 hasta = 0.0f) {
    std::vector<std::wstring> todos;
    listFiles(dir, L"", todos);

    // Filtrar segun el modo.
    std::vector<std::wstring> files;
    for (const auto& rel : todos) {
        if (!soloSensibles || isSensitiveData(rel))
            files.push_back(rel);
    }
    // --- se RESERVA el total antes de leer nada ---
    //
    // El blob crece archivo a archivo con resize(). Sin reservar, un paquete
    // de cientos de megas provoca decenas de realojos, y cada uno copia todo
    // lo acumulado hasta entonces: el coste acaba siendo cuadratico y se
    // nota mas que la propia lectura del disco.
    //
    // El tamano se consulta con los metadatos del directorio, que ya estan
    // en cache tras recorrerlo; no cuesta abrir los archivos.
    {
        u64 total = 4;   // el contador de archivos
        for (const auto& rel : files) {
            total += 8 + toUtf8(rel).size();   // long. del nombre + nombre + long. datos
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW((dir + L"\\" + rel).c_str(),
                                     GetFileExInfoStandard, &fad))
                total += (static_cast<u64>(fad.nFileSizeHigh) << 32) |
                         fad.nFileSizeLow;
        }
        // Un paquete de mas de 4 GB no cabe en el formato (las longitudes son
        // de 32 bits), asi que no tiene sentido reservar mas.
        if (total < 0xFFFFFFFFULL) out.reserve(out.size() + static_cast<size_t>(total));
    }

    // Puede que no haya datos sensibles: es valido (paquete de 0 archivos).
    appendU32(out, static_cast<u32>(files.size()));

    // El progreso avanza archivo a archivo. Antes esta fase entera era un
    // salto seco del 5% al 55%: en un juego grande, la barra se quedaba
    // parada medio minuto y parecia colgada.
    const size_t nTotal = files.size();
    size_t hechos = 0;

    for (const auto& rel : files) {
        if (progress && nTotal > 0 && hasta > desde) {
            const f32 t = static_cast<f32>(hechos) / static_cast<f32>(nTotal);
            progress->store(desde + (hasta - desde) * t);
        }
        ++hechos;
        const std::string relU8 = toUtf8(rel);
        appendU32(out, static_cast<u32>(relU8.size()));
        out.insert(out.end(), relU8.begin(), relU8.end());

        // --- lectura con la API de Windows, no con ifstream ---
        //
        // Medido sobre 200 MB en 310 archivos: leer era el 73% del guardado
        // entero (8.8 s de 12 s). ifstream pasa por su propio buffer interno
        // y copia dos veces; ReadFile vuelca directamente sobre el hueco ya
        // reservado del blob, que es lo que aqui hace falta.
        //
        // SEQUENTIAL_SCAN le dice a Windows como se va a leer el archivo, y
        // con eso adelanta las paginas siguientes en vez de ir a tiron por
        // tiron.
        const std::wstring full = dir + L"\\" + rel;
        const HANDLE h = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING,
                                     FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER li{};
        if (!GetFileSizeEx(h, &li)) { CloseHandle(h); return false; }
        const u64 n = static_cast<u64>(li.QuadPart);

        // El formato guarda longitudes de 32 bits: un archivo suelto de 4 GB
        // o mas no cabe. Se detecta aqui y se dice, en vez de truncarlo en
        // silencio y entregar un paquete corrupto.
        if (n > 0xFFFFFFFFULL) { CloseHandle(h); return false; }

        appendU32(out, static_cast<u32>(n));
        const size_t base = out.size();
        out.resize(base + static_cast<size_t>(n));

        // ReadFile admite como mucho un DWORD por llamada, y en archivos
        // grandes puede devolver menos de lo pedido: se repite hasta tenerlo
        // todo. Sin el bucle, un archivo de mas de 4 GB o una lectura corta
        // dejaria ceros dentro del paquete.
        u64 leido = 0;
        while (leido < n) {
            const DWORD pedir = static_cast<DWORD>(
                std::min<u64>(n - leido, 8u * 1024u * 1024u));
            DWORD got = 0;
            if (!ReadFile(h, out.data() + base + leido, pedir, &got, nullptr) ||
                got == 0) {
                CloseHandle(h);
                return false;
            }
            leido += got;
        }
        CloseHandle(h);
    }
    return true;
}

/// Empaqueta una lista explicita de archivos (src en disco, rel = ruta que
/// tendra al restaurar). Mismo formato de blob que packFolderFiltered.
bool packItems(size_t n,
               const std::function<void(size_t, std::wstring&, std::wstring&)>& get,
               std::vector<u8>& out) {
    appendU32(out, static_cast<u32>(n));
    for (size_t i = 0; i < n; ++i) {
        std::wstring src, rel;
        get(i, src, rel);
        const std::string relU8 = toUtf8(rel);
        appendU32(out, static_cast<u32>(relU8.size()));
        out.insert(out.end(), relU8.begin(), relU8.end());

        std::ifstream f(src.c_str(), std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize sz = f.tellg();
        f.seekg(0);
        appendU32(out, static_cast<u32>(sz));
        const size_t base = out.size();
        out.resize(base + static_cast<size_t>(sz));
        if (sz > 0) f.read(reinterpret_cast<char*>(out.data() + base), sz);
    }
    return true;
}

/// Empaqueta el arbol entero. `progress` es opcional: quien no lo pase --
/// los sellados pequenos, que terminan al instante -- no informa de nada.
bool packFolder(const std::wstring& dir, std::vector<u8>& out,
                std::atomic<float>* progress = nullptr,
                f32 desde = 0.0f, f32 hasta = 0.0f) {
    return packFolderFiltered(dir, out, false, progress, desde, hasta);
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

namespace {
/// Secreto de los paquetes DISTRIBUIBLES: los que produce GUARDAR ARCHIVOS.
///
/// A proposito NO es la huella del equipo. Los vaults de datos del jugador si
/// lo son, y eso esta bien -- su partida no debe abrirse en otra maquina. Pero
/// un juego publicado tiene que poder descargarlo cualquiera: sellarlo con la
/// huella del autor daria un paquete que solo se abre en su PC, que es justo
/// lo contrario de publicar.
///
/// El cifrado aqui no guarda un secreto frente al jugador (que va a ejecutar
/// el juego de todos modos): sirve para que el paquete viaje como una unidad
/// integra y no se pueda alterar a medias sin que el Vault lo detecte.
const char* kDistSecret = "ludora-paquete-distribucion-v1";

/// Comprime y cifra un blob ya empaquetado a un archivo.
bool sealBlobToFile(std::vector<u8>& blob, const std::wstring& destVault,
                    std::atomic<float>* progress, std::string& error,
                    const std::string& secretOverride = std::string()) {
    // Las tres fases que quedan, en el orden en que ocurren. Los numeros
    // reflejan lo que cada una tarda de verdad: comprimir es lo mas largo de
    // las tres, cifrar con una sola capa ya es rapido, y escribir depende
    // del disco.
    if (progress) progress->store(0.52f);
    // Las reservas grandes se protegen: en un juego que roza la memoria
    // disponible, un bad_alloc sin capturar se lleva Ludora entera por
    // delante. Capturado, el guardado falla con un aviso y la plataforma
    // sigue en pie -- que es la diferencia entre "no se pudo guardar" y
    // perder lo que estuvieras haciendo.
    std::vector<u8> comprimido;
    try {
        if (!compress(blob, comprimido)) {
            error = "No se pudo comprimir.";
            return false;
        }
    } catch (const std::bad_alloc&) {
        error = "No hay memoria suficiente para comprimir el juego. "
                "Cierra algun programa y vuelve a intentarlo.";
        return false;
    }
    // El blob original ya no hace falta: liberarlo ANTES de cifrar evita
    // tener las dos copias en memoria a la vez, que en un juego grande es la
    // diferencia entre caber en RAM y empezar a paginar.
    blob.clear(); blob.shrink_to_fit();
    if (progress) progress->store(0.80f);

    const std::string secret = secretOverride.empty()
        ? Device::fingerprint("ludora-nube-tomate-game-v1")
        : secretOverride;
    if (secret.empty()) { error = "No se pudo obtener la huella del equipo."; return false; }

    std::vector<u8> sealed;
    try {
        if (!Vault::seal(secret, comprimido, sealed, error)) return false;
    } catch (const std::bad_alloc&) {
        error = "No hay memoria suficiente para cifrar el paquete. "
                "Cierra algun programa y vuelve a intentarlo.";
        return false;
    }
    // Igual que antes: el comprimido ya esta dentro de `sealed`, asi que se
    // suelta antes de escribir en disco.
    comprimido.clear(); comprimido.shrink_to_fit();
    if (progress) progress->store(0.92f);

    makeDirs(parentDir(destVault));

    // --- se escribe a un temporal y se renombra al final ---
    //
    // BUG QUE ESTO ARREGLA: se escribia directamente sobre el .tomate bueno.
    // Si el guardado se cortaba a mitad -- disco lleno, apagon, Ludora
    // cerrada -- el paquete anterior YA estaba machacado y quedaba uno
    // truncado en su sitio. El juego dejaba de poder instalarse y no habia
    // vuelta atras.
    //
    // Con el temporal, el .tomate de siempre no se toca hasta que el nuevo
    // esta entero en disco. MoveFileEx con REPLACE_EXISTING hace el cambio
    // de un golpe, asi que en todo momento hay un paquete valido en esa
    // ruta: el viejo o el nuevo, nunca uno a medias.
    const std::wstring tmp = destVault + L".parcial";
    DeleteFileW(tmp.c_str());   // restos de un intento anterior que se corto

    if (!Vault::saveFile(tmp, sealed)) {
        DeleteFileW(tmp.c_str());
        error = "No se pudo guardar el paquete cifrado. Comprueba que "
                "queda sitio en el disco.";
        return false;
    }
    sealed.clear(); sealed.shrink_to_fit();

    if (!MoveFileExW(tmp.c_str(), destVault.c_str(),
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
} // namespace

bool sealFolder(const std::wstring& sourceExe,
                const std::wstring& destVault,
                std::atomic<float>* progress,
                std::string& error) {
    const std::wstring srcDir = parentDir(sourceExe);
    if (progress) progress->store(0.05f);

    std::vector<u8> blob;
    if (!packFolder(srcDir, blob)) {   // todo
        error = "No se pudo leer la carpeta del juego.";
        return false;
    }
    return sealBlobToFile(blob, destVault, progress, error);
}

bool sealWholeFolder(const std::wstring& srcDir,
                     const std::wstring& destVault,
                     std::atomic<float>* progress,
                     std::string& error) {
    if (srcDir.empty()) { error = "No se eligio ninguna carpeta."; return false; }

    // --- se comprueba ANTES de leer nada que la carpeta cabe ---
    //
    // El guardado llega a tener en RAM el blob y su comprimido a la vez, y
    // LZNT1 trabaja sobre buffers de 32 bits. Sin esta comprobacion, una
    // carpeta demasiado grande se leia entera -- minutos de disco -- para
    // reventar al final por falta de memoria, o peor, para truncar en
    // silencio y dejar un paquete corrupto que fallaba al instalarse.
    //
    // Comprobarlo aqui cuesta un recorrido de metadatos (milisegundos) y
    // convierte un fallo tardio e incomprensible en un aviso inmediato que
    // dice exactamente que pasa y cuanto sobra.
    {
        i32 nf = 0;
        u64 nb = 0;
        if (!measureFolder(srcDir, nf, nb)) {
            error = "No se pudo leer esa carpeta.";
            return false;
        }
        if (nf == 0) {
            error = "Esa carpeta esta vacia: no hay nada que guardar.";
            return false;
        }

        // Tope del formato: las longitudes del blob son de 32 bits.
        constexpr u64 kTopeFormato = 0xFFFFFFFFULL;   // 4 GB
        if (nb >= kTopeFormato) {
            char b[200];
            std::snprintf(b, sizeof(b),
                          "La carpeta ocupa %.1f GB y el maximo son 4 GB. "
                          "Quita lo que no haga falta y vuelve a intentarlo.",
                          nb / (1024.0 * 1024.0 * 1024.0));
            error = b;
            return false;
        }

        // Tope de memoria: hacen falta ~2.2 veces el tamano de la carpeta
        // (blob + comprimido + holgura). Si no hay, se dice ahora.
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            const u64 necesita = nb * 22 / 10;
            if (necesita > ms.ullAvailPhys) {
                char b[260];
                std::snprintf(b, sizeof(b),
                              "Para guardar %.1f GB hacen falta unos %.1f GB "
                              "de memoria libre y hay %.1f GB. Cierra algun "
                              "programa y vuelve a intentarlo.",
                              nb / (1024.0 * 1024.0 * 1024.0),
                              necesita / (1024.0 * 1024.0 * 1024.0),
                              ms.ullAvailPhys / (1024.0 * 1024.0 * 1024.0));
                error = b;
                return false;
            }
        }

        // Tope de disco: el paquete ocupa como mucho lo que la carpeta.
        ULARGE_INTEGER libre{};
        const std::wstring destDir = parentDir(destVault);
        if (!destDir.empty() &&
            GetDiskFreeSpaceExW(destDir.c_str(), &libre, nullptr, nullptr)) {
            if (libre.QuadPart < nb) {
                char b[240];
                std::snprintf(b, sizeof(b),
                              "No hay sitio en el disco: el paquete puede "
                              "ocupar hasta %.1f GB y quedan %.1f GB libres.",
                              nb / (1024.0 * 1024.0 * 1024.0),
                              libre.QuadPart / (1024.0 * 1024.0 * 1024.0));
                error = b;
                return false;
            }
        }
    }

    if (progress) progress->store(0.05f);

    // El empaquetado ocupa del 5% al 50%: es la fase larga en un juego
    // grande, y la barra avanza archivo a archivo en vez de quedarse quieta.
    std::vector<u8> blob;
    if (!packFolder(srcDir, blob, progress, 0.05f, 0.50f)) {
        error = "No se pudo leer la carpeta del juego.";
        return false;
    }
    // Secreto de distribucion, no la huella del equipo: el paquete tiene que
    // abrirse en el PC de quien lo descargue.
    return sealBlobToFile(blob, destVault, progress, error, kDistSecret);
}

bool measureFolder(const std::wstring& srcDir, i32& filesOut, u64& bytesOut) {
    filesOut = 0;
    bytesOut = 0;
    if (srcDir.empty()) return false;
    if (GetFileAttributesW(srcDir.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    // Recorrido iterativo con pila propia: un arbol de recursos puede ser muy
    // profundo y la recursion no aporta nada aqui.
    std::vector<std::wstring> pend{ srcDir };
    while (!pend.empty()) {
        const std::wstring dir = pend.back();
        pend.pop_back();

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring full = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                pend.push_back(full);
            } else {
                ++filesOut;
                bytesOut += (static_cast<u64>(fd.nFileSizeHigh) << 32) |
                            fd.nFileSizeLow;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return true;
}

bool sealDataItems(size_t n,
                   const std::function<void(size_t, std::wstring&, std::wstring&)>& get,
                   const std::wstring& destVault,
                   std::atomic<float>* progress,
                   std::string& error) {
    if (progress) progress->store(0.8f);
    std::vector<u8> blob;
    if (!packItems(n, get, blob)) {
        error = "No se pudieron leer los datos del juego.";
        return false;
    }
    // Aunque no haya datos (n==0), se cifra un blob vacio: el .tomate existe y
    // el flujo de restauracion es uniforme.
    return sealBlobToFile(blob, destVault, progress, error);
}

bool sealDataOnly(const std::wstring& sourceExe,
                  const std::wstring& destVault,
                  std::atomic<float>* progress,
                  std::string& error) {
    const std::wstring srcDir = parentDir(sourceExe);
    if (progress) progress->store(0.8f);

    // Solo los datos sensibles; los assets se copian en claro aparte.
    std::vector<u8> blob;
    if (!packFolderFiltered(srcDir, blob, /*soloSensibles*/ true)) {
        error = "No se pudieron leer los datos del juego.";
        return false;
    }
    // Aunque no haya datos sensibles (blob con 0 archivos), se cifra igual:
    // asi el .tomate existe y el flujo es uniforme.
    return sealBlobToFile(blob, destVault, progress, error);
}

bool releaseGame(const std::wstring& installedExe, const std::wstring& vaultPath,
                 std::string& error) {
    // La carpeta del juego sale del exe instalado.
    std::wstring dir = installedExe;
    const size_t s = dir.find_last_of(L"\\/");
    if (s == std::wstring::npos) { error = "Ruta del juego no valida."; return false; }
    dir = dir.substr(0, s);

    // 1) Volcar los datos cifrados sobre la carpeta, en claro.
    if (!vaultPath.empty() &&
        GetFileAttributesW(vaultPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!restoreData(vaultPath, dir, error)) return false;

        // 2) Borrar el paquete: ya no protege nada, y dejarlo haria creer que
        //    el juego sigue cifrado cuando no lo esta.
        if (!DeleteFileW(vaultPath.c_str())) {
            error = "Los datos se liberaron, pero no se pudo borrar el paquete.";
            return false;
        }
    }
    return true;
}

bool restoreData(const std::wstring& srcVault, const std::wstring& destDir,
                 std::string& error) {
    // Descifra los datos sensibles SOBRE la carpeta ya instalada (con los
    // assets en claro). Deja el juego completo y jugable.
    std::vector<u8> sealed;
    if (!Vault::loadFile(srcVault, sealed) || sealed.empty()) {
        // Sin paquete de datos no es un error: puede que el juego no tuviera
        // datos sensibles. Los assets ya bastan para jugar.
        return true;
    }
    const std::string secret = Device::fingerprint("ludora-nube-tomate-game-v1");
    if (secret.empty()) { error = "No se pudo obtener la huella del equipo."; return false; }

    std::vector<u8> comprimido;
    if (!Vault::open(secret, sealed, comprimido, error)) return false;

    std::vector<u8> blob;
    if (!decompress(comprimido, blob)) { error = "No se pudieron descomprimir los datos."; return false; }

    makeDirs(destDir);
    if (!unpackFolder(blob, destDir)) { error = "No se pudieron restaurar los datos."; return false; }
    return true;
}

bool unsealWholeFolder(const std::wstring& srcVault,
                       const std::wstring& destDir,
                       std::string& error) {
    std::vector<u8> sealed;
    if (!Vault::loadFile(srcVault, sealed) || sealed.empty()) {
        error = "No se encontro el paquete de archivos del juego.";
        return false;
    }

    // Secreto de distribucion: el paquete se abre en cualquier equipo, no solo
    // en el del autor. Ver kDistSecret.
    std::vector<u8> comprimido;
    if (!Vault::open(kDistSecret, sealed, comprimido, error)) {
        error = "El paquete del juego esta danado o alterado.";
        return false;
    }

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

// Puente publico a la clasificacion (la implementacion vive en el namespace
// anonimo de este archivo).
bool isSensitiveData(const std::wstring& rel) {
    return ludora::isSensitiveData(rel);
}

// Puente publico a la deteccion de raiz del juego.
std::wstring findGameRoot(const std::wstring& sourceExe, std::wstring& exeRelOut) {
    return ludora::findGameRoot(sourceExe, exeRelOut);
}

} // namespace gamevault

// -------------------------------------------- guardado de los archivos ---
// Empaqueta la carpeta del proyecto dentro de Ludora. Mismo esqueleto de hilo
// que GameInstall, por las mismas razones: sellar tarda y no puede congelar la
// interfaz, y una excepcion escapada de un hilo mata el proceso sin dialogo.

GameArchive::~GameArchive() { join(); }

void GameArchive::join() {
    if (m_thread.joinable()) m_thread.join();
}

bool GameArchive::start(const std::wstring& srcDir, const std::wstring& destVault) {
    if (m_running.load()) return false;

    // Un hilo que termino pero nadie recogio sigue siendo joinable, y asignar
    // sobre el llama a std::terminate(). Pasa al guardar dos veces seguidas.
    if (m_thread.joinable()) m_thread.join();

    m_running.store(true);
    m_done.store(false);
    m_failed.store(false);
    m_progress.store(0.0f);
    m_error.clear();
    m_vaultPath = destVault;
    m_files = 0;
    m_bytes = 0;

    m_thread = std::thread(&GameArchive::worker, this, srcDir, destVault);
    return true;
}

void GameArchive::worker(std::wstring srcDir, std::wstring destVault) {
    try {
        workerBody(std::move(srcDir), std::move(destVault));
    } catch (const std::exception& e) {
        m_error = std::string("Error al guardar: ") + e.what();
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
    } catch (...) {
        m_error = "Error desconocido al guardar.";
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
    }
}

void GameArchive::workerBody(std::wstring srcDir, std::wstring destVault) {
    // Se mide primero: asi se puede decir cuantos archivos se guardaron, y se
    // detecta la carpeta vacia ANTES de cifrar nada.
    i32 nArchivos = 0;
    u64 nBytes = 0;
    if (!gamevault::measureFolder(srcDir, nArchivos, nBytes)) {
        m_error = "No se pudo leer la carpeta elegida.";
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
        return;
    }
    if (nArchivos == 0) {
        m_error = "La carpeta esta vacia: no hay nada que guardar.";
        m_failed.store(true);
        m_done.store(true);
        m_running.store(false);
        return;
    }
    m_files = nArchivos;
    m_bytes = nBytes;

    std::string err;
    const bool ok = gamevault::sealWholeFolder(srcDir, destVault, &m_progress, err);
    if (!ok) {
        m_error = err.empty() ? "No se pudieron guardar los archivos." : err;
        m_failed.store(true);
    } else {
        m_progress.store(1.0f);
    }
    m_done.store(true);
    m_running.store(false);
}

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

bool entryInstalled(const Entry& e) {
    // Instalado = el .exe en claro existe realmente en disco. Un registro que
    // apunta a un .exe ausente (instalacion borrada) NO cuenta como instalado.
    return !e.exe.empty() &&
           GetFileAttributesW(e.exe.c_str()) != INVALID_FILE_ATTRIBUTES;
}

i32 deleteFiles(const Entry& e, std::string& error) {
    i32 borrados = 0;

    // El paquete cifrado de datos vive al lado de la carpeta, no dentro.
    if (!e.vault.empty() &&
        GetFileAttributesW(e.vault.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (DeleteFileW(e.vault.c_str())) ++borrados;
    }

    if (e.exe.empty()) return borrados;

    // Carpeta del juego, deducida de su ejecutable.
    std::wstring dir = e.exe;
    const size_t s = dir.find_last_of(L"\\/");
    if (s == std::wstring::npos) { error = "Ruta del juego no valida."; return borrados; }
    dir = dir.substr(0, s);

    // --- salvaguardas ---
    // Un registro corrupto o una ruta a medio construir no puede acabar
    // borrando una unidad entera o una carpeta del sistema. Ante la duda no
    // se borra nada: perder un juego se arregla reinstalando, perder
    // C:\Windows no.
    if (dir.size() < 4) {
        error = "Ruta demasiado corta para borrar.";
        return borrados;
    }
    // "C:\" o "C:" a secas: raiz de unidad.
    if (dir.size() <= 3 && dir[1] == L':') {
        error = "No se borra la raiz de una unidad.";
        return borrados;
    }
    {
        std::wstring low = dir;
        for (auto& c : low) c = static_cast<wchar_t>(towlower(c));
        static const wchar_t* kProhibidas[] = {
            L"c:\\windows", L"c:\\program files", L"c:\\program files (x86)",
            L"c:\\users", L"c:\\programdata",
        };
        for (const wchar_t* p : kProhibidas) {
            const std::wstring pr = p;
            if (low.size() >= pr.size() &&
                low.compare(0, pr.size(), pr) == 0 &&
                (low.size() == pr.size() || low[pr.size()] == L'\\')) {
                // Dentro de Usuarios si se permite, pero no la carpeta misma.
                if (pr != L"c:\\users" || low.size() <= pr.size() + 1) {
                    error = "Esa carpeta esta protegida y no se borra.";
                    return borrados;
                }
            }
        }
    }

    // --- borrado recursivo ---
    // Se recorre a mano en vez de usar SHFileOperation: asi no hay dialogos
    // del sistema ni sorpresas con la papelera, y se puede contar lo borrado.
    std::function<void(const std::wstring&)> rec = [&](const std::wstring& d) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring p = d + L"\\" + n;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                rec(p);
            } else {
                // Quitar solo lectura: si no, DeleteFile falla en silencio.
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(p.c_str())) ++borrados;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        RemoveDirectoryW(d.c_str());
    };
    rec(dir);

    return borrados;
}

bool remove(const std::wstring& path, const std::string& name) {
    std::vector<Entry> all = load(path);
    std::vector<Entry> keep;
    keep.reserve(all.size());
    for (auto& x : all)
        if (x.name != name) keep.push_back(x);
    if (keep.size() == all.size()) return false;   // no habia nada que quitar

    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << "# Biblioteca de Ludora.\r\n";
    for (const auto& x : keep)
        out << "name="    << x.name << "\r\n"
            << "exe="     << toUtf8(x.exe) << "\r\n"
            << "vault="   << toUtf8(x.vault) << "\r\n"
            << "exename=" << toUtf8(x.exeName) << "\r\n";
    return out.good();
}

/// Lanza el exe y devuelve el handle del proceso en `outProc` (o nullptr).
/// El llamante debe CloseHandle cuando termine de vigilarlo.
/// Carpeta de trabajo con la que lanzar un juego.
///
/// NO es siempre la del .exe. Un juego compilado deja su ejecutable en
/// `bin\Release\` o similar, pero busca sus texturas y sonidos relativos a la
/// RAIZ del proyecto. Lanzarlo desde la carpeta del exe hace que no encuentre
/// nada y se cierre, o arranque sin recursos.
///
/// Se sube desde el exe mientras los nombres de carpeta parezcan de
/// compilacion, y se para en cuanto aparece una carpeta de recursos.
static std::wstring workDirFor(const std::wstring& exe) {
    static const wchar_t* kCompilacion[] = {
        L"release", L"debug", L"bin", L"build", L"x64", L"win32", L"out"
    };
    static const wchar_t* kRecursos[] = {
        L"resourcepacks", L"assets", L"data", L"res", L"textures",
        L"texturas", L"sounds", L"sonidos", L"shaders", L"fonts", L"levels"
    };

    std::wstring dir = parentDir(exe);

    // Hasta 6 niveles: suficiente para bin\x64\Release y de sobra para el
    // resto. Un limite evita subir hasta la raiz del disco si el arbol no
    // tiene la forma esperada.
    for (i32 i = 0; i < 6; ++i) {
        // Si aqui ya hay recursos, esta es la raiz: no se sube mas.
        bool tieneRecursos = false;
        for (const wchar_t* r : kRecursos) {
            if (GetFileAttributesW((dir + L"\\" + r).c_str()) !=
                INVALID_FILE_ATTRIBUTES) { tieneRecursos = true; break; }
        }
        if (tieneRecursos) break;

        // El nombre de esta carpeta: si no es de compilacion, se para.
        const size_t s = dir.find_last_of(L"\\/");
        if (s == std::wstring::npos) break;
        std::wstring nombre = dir.substr(s + 1);
        for (auto& c : nombre) c = static_cast<wchar_t>(towlower(c));

        bool esDeCompilacion = false;
        for (const wchar_t* k : kCompilacion)
            if (nombre == k) { esDeCompilacion = true; break; }
        if (!esDeCompilacion) break;

        const std::wstring arriba = dir.substr(0, s);
        if (arriba.size() < 3) break;   // no subir hasta "C:\"
        dir = arriba;
    }
    return dir;
}

static bool launchProc(const std::wstring& exe, void** outProc,
                       std::string* error = nullptr) {
    if (outProc) *outProc = nullptr;
    if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (error) *error = "No se encuentra el ejecutable del juego.";
        return false;
    }

    // ShellExecuteEx con SEE_MASK_NOCLOSEPROCESS devuelve el handle del
    // proceso, que ShellExecuteW no da: es lo que permite saber cuando el
    // juego sigue abierto para animar el boton.
    const std::wstring dir = workDirFor(exe);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    // NOASYNC: espera a que el shell termine de arrancar el proceso antes de
    // volver. Sin esto, lanzar y consultar el handle enseguida podia dar un
    // handle que aun no valia.
    //
    // FLAG_NO_UI: el shell NO saca su propio cuadro de error.
    //
    // BUG QUE ESTO ARREGLA: al fallar un lanzamiento, Windows abria un
    // dialogo modal suyo ("no puede encontrar el archivo ...") por encima de
    // Ludora. Son dos problemas en uno:
    //
    //   - El dialogo BLOQUEA el hilo hasta que alguien lo acepta. Ludora se
    //     queda congelada, y en una prueba automatica no hay quien lo cierre.
    //   - El mensaje es del shell, no nuestro: dice la ruta cruda y tapa el
    //     aviso de Ludora, que si explica el motivo (falta una DLL, lo
    //     bloqueo el antivirus, hace falta permiso de admin).
    //
    // Con el flag puesto, el fallo vuelve por GetLastError y lo cuenta la
    // propia interfaz, que es donde el jugador lo esta mirando.
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC |
                       SEE_MASK_FLAG_NO_UI;
    sei.lpVerb       = L"open";
    sei.lpFile       = exe.c_str();
    sei.lpDirectory  = dir.c_str();
    sei.nShow        = SW_SHOWNORMAL;

    const BOOL lanzado = ShellExecuteExW(&sei);

    if (!lanzado) {
        // El motivo importa: "no se pudo lanzar" no dice si falta una DLL,
        // si lo bloqueo el antivirus o si hace falta permiso de admin.
        if (error) {
            const DWORD e = GetLastError();
            switch (e) {
                case ERROR_FILE_NOT_FOUND:
                    *error = "Falta el ejecutable o una de sus librerias.";
                    break;
                case ERROR_PATH_NOT_FOUND:
                    *error = "No se encuentra la carpeta del juego.";
                    break;
                case ERROR_ACCESS_DENIED:
                    *error = "Windows bloqueo la ejecucion. Puede ser el "
                             "antivirus o faltan permisos.";
                    break;
                case ERROR_CANCELLED:
                    *error = "Se cancelo el permiso para abrir el juego.";
                    break;
                default: {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "No se pudo lanzar el juego (error %lu).", e);
                    *error = buf;
                    break;
                }
            }
        }
        return false;
    }
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
    (void)tempDir;
    if (outProc) *outProc = nullptr;

    if (e.exe.empty()) { error = "El juego no tiene ejecutable registrado."; return false; }
    if (GetFileAttributesW(e.exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // El registro apunta a un .exe que ya no esta (instalacion borrada o
        // incompleta). Mensaje accionable: hay que reinstalar el juego.
        error = "El juego no esta instalado (falta el ejecutable). "
                "Vuelve a instalarlo desde la Tienda.";
        return false;
    }

    // Modelo hibrido: el exe y los assets ya estan EN CLARO en la raiz
    // instalada. Solo hay que restaurar los datos sensibles (partidas, config)
    // desde el paquete cifrado, SOBRE la raiz instalada, antes de lanzar. Los
    // datos se empaquetaron con rutas relativas a la raiz (p.ej. saves\...),
    // asi que hay que desplegarlos en la raiz, no en la carpeta del .exe (que
    // puede estar en build\bin\Release, mas abajo).
    if (!e.vault.empty()) {
        // La raiz es el destino de instalacion: el vault es "<raiz>.datos.tomate".
        std::wstring instRoot = e.vault;
        const std::wstring suf = L".datos.tomate";
        if (instRoot.size() > suf.size() &&
            instRoot.compare(instRoot.size() - suf.size(), suf.size(), suf) == 0)
            instRoot.erase(instRoot.size() - suf.size());
        else
            instRoot = parentDir(e.exe);   // respaldo si el nombre cambia

        std::string derr;
        if (!gamevault::restoreData(e.vault, instRoot, derr)) {
            // Si los datos no se pueden restaurar, se avisa pero se intenta
            // jugar igual: el juego generara datos nuevos si no los encuentra.
            error = derr;
        }
    }

    // El motivo del fallo viene de launchProc: decir solo "no se pudo" deja
    // al jugador sin saber si falta una DLL, si lo paro el antivirus o si el
    // archivo no esta.
    //
    // OJO: `error` puede traer ya el aviso de que no se restauraron los
    // datos. Eso no impide jugar, asi que se pisa solo si lanzar falla.
    std::string lerr;
    if (!launchProc(e.exe, outProc, &lerr)) {
        error = lerr.empty() ? "No se pudo lanzar el juego." : lerr;
        return false;
    }
    return true;
}

} // namespace library

// --- helpers compartidos por el catalogo y las resenas -------------------
// Viven fuera de los dos namespaces porque los usan ambos: el formato en
// disco es el mismo (clave=valor, una linea por registro, texto escapado).
namespace {

/// Hora actual en segundos desde epoch. Se usa para fechar cada version.
long long nowEpoch() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    // FILETIME cuenta intervalos de 100 ns desde 1601; epoch Unix empieza en
    // 1970, de ahi la constante de conversion.
    const u64 t = (static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<long long>(t / 10000000ULL) - 11644473600LL;
}

/// Protege un texto para que quepa en UNA linea clave=valor. Los saltos de
/// linea y las barras invertidas se sustituyen por secuencias de escape; sin
/// esto, una descripcion con un Enter partiria el registro en dos y el resto
/// del archivo se leeria mal.
std::string escapeLine(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            // La barra vertical separa los campos de `ver=`: sin escaparla, un
            // changelog que la contuviera partiria el registro y se comeria los
            // campos siguientes.
            case '|':  out += "\\p";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

/// Deshace escapeLine(). Una barra invertida al final sin nada detras se deja
/// tal cual: es un archivo editado a mano, y perder un caracter es mejor que
/// leer fuera del buffer.
std::string unescapeLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out.push_back(s[i]); continue; }
        if (i + 1 >= s.size()) { out.push_back('\\'); break; }
        switch (s[++i]) {
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 'p':  out.push_back('|');  break;
            case '\\': out.push_back('\\'); break;
            default:   out.push_back('\\'); out.push_back(s[i]); break;
        }
    }
    return out;
}

} // namespace (helpers compartidos)

// -------------------------------------------------- catalogo del desarrollador ---
namespace devcatalog {

namespace {

/// Escribe el catalogo entero. Es la unica ruta de escritura: publish(),
/// addVersion() y setPrice() cargan, modifican y vuelven aqui, de modo que el
/// formato en disco se define en un solo sitio.
bool writeAll(const std::wstring& path, const std::vector<Game>& games) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << "# Catalogo del desarrollador de Ludora.\r\n";
    for (const auto& g : games) {
        out << "game="  << g.name << "\r\n"
            << "price=" << g.price.usdCents << "\r\n";
        // Claves nuevas. Un catalogo escrito antes no las trae y load() les
        // pone un valor por defecto, asi que los juegos ya publicados siguen
        // cargando sin tocar nada.
        if (!g.code.empty()) out << "code=" << g.code << "\r\n";
        if (g.revAt > 0)
            out << "rev=" << g.revFiles << "|" << g.revDirs << "|"
                << g.revBytes << "|" << g.revAt << "\r\n";

        // --- ficha de publicacion ---
        // Cada campo en su propia linea con su clave: anadir uno mas adelante
        // no rompe los archivos ya escritos, porque load() ignora las claves
        // que no conoce y deja el valor por defecto en las que faltan.
        //
        // Los textos pueden llevar saltos de linea (la descripcion sobre todo),
        // y el formato es de una linea por clave: se escapan al guardar y se
        // deshacen al cargar, que es mas simple que un formato multilinea.
        const Listing& L = g.listing;
        if (!L.description.empty()) out << "desc="  << escapeLine(L.description) << "\r\n";
        if (!L.genres.empty())      out << "gen="   << escapeLine(L.genres) << "\r\n";
        if (!L.multiplayer.empty()) out << "multi=" << escapeLine(L.multiplayer) << "\r\n";
        // `age` sigue siendo la edad MINIMA y conserva su nombre: los
        // catalogos ya escritos la tienen asi, y cambiarla de clave los
        // dejaria sin edad. La maxima entra como clave nueva.
        out << "fps="    << L.fps       << "\r\n"
            << "age="    << L.minAge    << "\r\n"
            << "agemax=" << L.maxAge    << "\r\n"
            << "piracy=" << L.piracy    << "\r\n"
            << "custom=" << L.customize << "\r\n";
        if (!L.art.empty())    out << "art="  << toUtf8(L.art) << "\r\n";
        if (!L.source.empty()) out << "src="  << toUtf8(L.source) << "\r\n";

        // EULA propio: tres bloques, cada uno de texto libre y largo. Van
        // escapados como la descripcion, que es lo que permite guardar un
        // reglamento con saltos de linea en un formato de una linea por clave.
        if (!L.eulaLocal.empty()) out << "eula1=" << escapeLine(L.eulaLocal) << "\r\n";
        if (!L.eulaMulti.empty()) out << "eula2=" << escapeLine(L.eulaMulti) << "\r\n";
        if (!L.eulaMods.empty())  out << "eula3=" << escapeLine(L.eulaMods)  << "\r\n";

        // Una linea por version, en orden de subida. El separador es '|' igual
        // que en AccountStore, para no inventar un formato distinto por modulo.
        // El changelog va como cuarto campo, escapado: puede llevar saltos de
        // linea y el formato es de una linea por registro. Las versiones sin
        // notas escriben el campo vacio, asi los archivos viejos (de tres
        // campos) siguen leyendose igual.
        for (const auto& v : g.versions)
            out << "ver=" << v.label << "|" << toUtf8(v.exe) << "|"
                << v.uploadedAt << "|" << escapeLine(v.changelog) << "|"
                << toUtf8(v.archive) << "|" << v.archiveFiles << "|"
                << v.archiveBytes << "\r\n";
    }
    return out.good();
}

/// Busca un juego por nombre. Devuelve nullptr si no esta.
Game* findGame(std::vector<Game>& games, const std::string& name) {
    for (auto& g : games)
        if (g.name == name) return &g;
    return nullptr;
}

} // namespace

std::string randomGameCode(const std::wstring& catalogPath) {
    // El mismo alfabeto que makeGameCode: sin O/0 ni I/l/1, porque un codigo
    // acaba tecleandose a mano aunque este para copiarlo.
    static const char kAlfa[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    constexpr i32 kBase = 32;
    constexpr i32 kLargo = 15;

    // Hasta unos cuantos intentos por si saliera uno ya usado. Con 32^15
    // combinaciones no va a pasar, pero comprobarlo cuesta nada y evita que
    // dos juegos compartan codigo, que haria ambigua la busqueda.
    for (i32 intento = 0; intento < 8; ++intento) {
        std::vector<u8> bytes;
        std::string code = "#";

        if (Vault::randomBytes(bytes, static_cast<size_t>(kLargo))) {
            for (i32 i = 0; i < kLargo; ++i)
                code.push_back(kAlfa[bytes[static_cast<size_t>(i)] % kBase]);
        } else {
            // Sin aleatoriedad del sistema no se inventa un codigo debil en
            // silencio: se mezcla el reloj, que al menos no se repite.
            FILETIME ft{};
            GetSystemTimeAsFileTime(&ft);
            u64 v = (static_cast<u64>(ft.dwHighDateTime) << 32) |
                    ft.dwLowDateTime;
            v ^= static_cast<u64>(intento) * 0x9E3779B97F4A7C15ULL;
            for (i32 i = 0; i < kLargo; ++i) {
                v ^= v << 13; v ^= v >> 7; v ^= v << 17;
                code.push_back(kAlfa[v % kBase]);
            }
        }

        bool repetido = false;
        for (const auto& g : load(catalogPath))
            if (g.code == code) { repetido = true; break; }
        if (!repetido)
            for (const auto& r : reservations(catalogPath))
                if (r.code == code) { repetido = true; break; }

        if (!repetido) return code;
    }
    return std::string();
}

std::string makeGameCode(const std::string& name) {
    // Alfabeto sin caracteres que se confunden al teclear un codigo a mano:
    // nada de O/0, I/l/1. Un codigo esta para copiarlo, pero alguien acabara
    // escribiendolo, y "#AB0O1IL23" seria una trampa.
    static const char kAlfa[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    constexpr i32 kBase = 32;   // sizeof(kAlfa) - 1

    // Dos mezclas distintas del nombre, para que dos nombres parecidos no
    // produzcan codigos parecidos. Con una sola, "Juego 1" y "Juego 2" solo
    // se diferenciaban en el ultimo caracter.
    u64 a = 0xCBF29CE484222325ULL;   // FNV-1a
    for (const char c : name) {
        a ^= static_cast<u8>(c);
        a *= 0x100000001B3ULL;
    }
    u64 b = 0x9E3779B97F4A7C15ULL;
    for (const char c : name) {
        b += static_cast<u8>(c);
        b ^= b << 13; b ^= b >> 7; b ^= b << 17;
    }

    std::string out = "#";
    for (i32 i = 0; i < 10; ++i) {
        const u64 v = (i & 1) ? (b >> (i * 5)) : (a >> (i * 5));
        out.push_back(kAlfa[(v ^ (a + b + static_cast<u64>(i))) % kBase]);
    }
    return out;
}

std::string findByCode(const std::wstring& path, const std::string& code) {
    // Se normaliza lo pegado: sin espacios, en mayusculas y sin el '#'. Quien
    // copia un codigo de una lista arrastra espacios o lo escribe en
    // minusculas, y eso no deberia impedir encontrar su juego.
    auto limpia = [](const std::string& s) {
        std::string r;
        for (const char c : s) {
            if (c == ' ' || c == '\t' || c == '#') continue;
            r.push_back(static_cast<char>(std::toupper(
                static_cast<unsigned char>(c))));
        }
        return r;
    };

    const std::string buscado = limpia(code);
    if (buscado.empty()) return std::string();

    for (const auto& g : load(path))
        if (limpia(g.code) == buscado) return g.name;
    return std::string();
}

bool reviewGame(const std::wstring& path, const std::string& name,
                const std::wstring& folder, Revision& out, std::string& error) {
    out = Revision{};

    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g) { error = "Ese juego no esta publicado."; return false; }

    // Cuenta lo que hay AHORA. Las carpetas se cuentan aparte de los
    // archivos: mover un archivo de sitio no cambia el total de archivos,
    // pero si el de carpetas, y es un cambio que conviene ver.
    if (folder.empty() ||
        GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "No se encuentra la carpeta del juego.";
        return false;
    }

    std::vector<std::wstring> pend{ folder };
    while (!pend.empty()) {
        const std::wstring dir = pend.back();
        pend.pop_back();

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ++out.dirs;
                pend.push_back(dir + L"\\" + n);
            } else {
                ++out.files;
                out.bytes += (static_cast<u64>(fd.nFileSizeHigh) << 32) |
                             fd.nFileSizeLow;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // `revAt` a cero significa que nunca se reviso. Es lo que distingue la
    // primera revision -- que no puede comparar nada -- de las siguientes.
    out.primera = (g->revAt <= 0);
    if (!out.primera) {
        out.dFiles = out.files - g->revFiles;
        out.dDirs  = out.dirs  - g->revDirs;
        out.dBytes = static_cast<long long>(out.bytes) -
                     static_cast<long long>(g->revBytes);
        out.cambios = (out.dFiles != 0 || out.dDirs != 0 || out.dBytes != 0);
    }

    // La huella nueva sustituye a la vieja: la proxima revision compara
    // contra esta.
    g->revFiles = out.files;
    g->revDirs  = out.dirs;
    g->revBytes = out.bytes;
    g->revAt    = nowEpoch();

    if (!writeAll(path, all)) {
        error = "No se pudo guardar la revision.";
        return false;
    }
    return true;
}

std::vector<Game> load(const std::wstring& path) {
    std::vector<Game> out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return out;

    std::string line;
    Game cur;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if (k == "game") {
            if (!cur.name.empty()) { out.push_back(cur); cur = Game{}; }
            cur.name = v;
        } else if (k == "code") {
            cur.code = v;
        } else if (k == "rev") {
            // archivos|carpetas|bytes|cuando
            const size_t p1 = v.find('|');
            const size_t p2 = (p1 == std::string::npos) ? p1 : v.find('|', p1 + 1);
            const size_t p3 = (p2 == std::string::npos) ? p2 : v.find('|', p2 + 1);
            if (p3 != std::string::npos) {
                cur.revFiles = std::atoi(v.substr(0, p1).c_str());
                cur.revDirs  = std::atoi(v.substr(p1 + 1, p2 - p1 - 1).c_str());
                cur.revBytes = static_cast<u64>(
                    std::atoll(v.substr(p2 + 1, p3 - p2 - 1).c_str()));
                cur.revAt    = std::atoll(v.substr(p3 + 1).c_str());
            }
        } else if (k == "price") {
            cur.price.usdCents = std::atoi(v.c_str());
        // --- ficha de publicacion ---
        // Los numeros se acotan al cargar, no solo al escribir: el archivo se
        // puede editar a mano, y un fps de 99999 descuadraria el deslizador.
        } else if (k == "desc") {
            cur.listing.description = unescapeLine(v);
        } else if (k == "gen") {
            cur.listing.genres = unescapeLine(v);
        } else if (k == "multi") {
            cur.listing.multiplayer = unescapeLine(v);
        } else if (k == "fps") {
            cur.listing.fps = std::clamp(std::atoi(v.c_str()), kMinFps, kMaxFps);
        } else if (k == "age") {
            cur.listing.minAge = std::clamp(std::atoi(v.c_str()), kMinAge, kMaxAge);
        } else if (k == "agemax") {
            // Una ficha sin esta clave conserva el 90 que trae Listing, que
            // es el equivalente a la edad abierta por arriba de antes.
            cur.listing.maxAge = std::clamp(std::atoi(v.c_str()), kMinAge, kMaxAge);
        } else if (k == "piracy") {
            cur.listing.piracy = std::clamp(std::atoi(v.c_str()), 0, 2);
        } else if (k == "custom") {
            cur.listing.customize = std::clamp(std::atoi(v.c_str()), 0, 2);
        } else if (k == "art") {
            cur.listing.art = fromUtf8(v);
        } else if (k == "src") {
            cur.listing.source = fromUtf8(v);
        } else if (k == "eula1") {
            cur.listing.eulaLocal = unescapeLine(v);
        } else if (k == "eula2") {
            cur.listing.eulaMulti = unescapeLine(v);
        } else if (k == "eula3") {
            cur.listing.eulaMods = unescapeLine(v);
        } else if (k == "ver") {
            // label|exe|uploadedAt[|changelog[|archive|files|bytes]]
            // Los campos del final son opcionales y se leen en bloque: los
            // catalogos viejos tienen tres campos (sin changelog) o cuatro
            // (sin paquete guardado), y tienen que seguir cargando igual.
            const size_t p1 = v.find('|');
            if (p1 == std::string::npos) continue;
            const size_t p2 = v.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;
            const size_t p3 = v.find('|', p2 + 1);

            Version ver;
            ver.label = v.substr(0, p1);
            ver.exe   = fromUtf8(v.substr(p1 + 1, p2 - p1 - 1));
            if (p3 == std::string::npos) {
                ver.uploadedAt = std::atoll(v.substr(p2 + 1).c_str());
            } else {
                ver.uploadedAt = std::atoll(v.substr(p2 + 1, p3 - p2 - 1).c_str());

                const size_t p4 = v.find('|', p3 + 1);
                if (p4 == std::string::npos) {
                    ver.changelog = unescapeLine(v.substr(p3 + 1));
                } else {
                    ver.changelog = unescapeLine(v.substr(p3 + 1, p4 - p3 - 1));

                    // Paquete guardado: ruta|archivos|bytes.
                    const size_t p5 = v.find('|', p4 + 1);
                    if (p5 == std::string::npos) {
                        ver.archive = fromUtf8(v.substr(p4 + 1));
                    } else {
                        ver.archive = fromUtf8(v.substr(p4 + 1, p5 - p4 - 1));
                        const size_t p6 = v.find('|', p5 + 1);
                        if (p6 == std::string::npos) {
                            ver.archiveFiles = std::atoi(v.substr(p5 + 1).c_str());
                        } else {
                            ver.archiveFiles =
                                std::atoi(v.substr(p5 + 1, p6 - p5 - 1).c_str());
                            ver.archiveBytes = static_cast<u64>(
                                std::atoll(v.substr(p6 + 1).c_str()));
                        }
                    }
                    if (ver.archiveFiles < 0) ver.archiveFiles = 0;
                }
            }
            cur.versions.push_back(ver);
        }
    }
    if (!cur.name.empty()) out.push_back(cur);

    // Los juegos publicados antes de que existieran los codigos no traen uno.
    // Se les deriva del NOMBRE, no al azar: asi el mismo juego recibe siempre
    // el mismo codigo aunque el catalogo se recargue, y el que el autor copio
    // ayer le sigue valiendo hoy.
    for (auto& g : out)
        if (g.code.empty()) g.code = makeGameCode(g.name);

    return out;
}

bool save(const std::wstring& path, const std::vector<Game>& games) {
    return writeAll(path, games);
}

bool publish(const std::wstring& path, const std::string& name,
             const std::wstring& exe, std::string& error) {
    if (name.empty()) { error = "El juego necesita un nombre."; return false; }

    std::vector<Game> all = load(path);
    if (findGame(all, name)) {
        error = "Ya publicaste un juego con ese nombre. "
                "Usa ACTUALIZAR JUEGO para subir una version nueva.";
        return false;
    }

    Game g;
    g.name = name;
    g.versions.push_back(Version{ "v1", exe, nowEpoch() });

    // Si habia un codigo RESERVADO para este nombre, el juego lo hereda. Es
    // el que el autor ya copio y pego, asi que generar otro aqui lo dejaria
    // con un codigo que no lleva a ninguna parte.
    //
    // Va antes de escribir el catalogo: `load` solo deriva un codigo cuando
    // el campo viene vacio, y asi el guardado ya sale con el definitivo.
    for (const auto& r : reservations(path)) {
        if (r.game == name) { g.code = r.code; break; }
    }
    if (g.code.empty()) g.code = makeGameCode(name);

    all.push_back(g);

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }

    // La reserva ya cumplio: el juego existe y lleva su codigo.
    clearReservation(path, name);

    // Si el autor guardo los archivos ANTES de publicar -- el orden natural --,
    // la version recien creada los adopta ahora. Y con ellos el precio, si lo
    // puso mientras el juego aun no existia: aceptarselo entonces y perderlo
    // al publicar seria peor que no haberselo dejado poner.
    const Pending pend = getPending(path, name);
    if (!pend.archive.empty() || pend.priceSet) {
        bool adoptado = true;
        std::string ignorado;

        if (!pend.archive.empty())
            adoptado = setArchive(path, name, pend.archive, pend.files,
                                  pend.bytes, ignorado);

        if (pend.priceSet)
            setPrice(path, name, Price{ pend.price }, ignorado);

        if (adoptado) clearPending(path, name);
    }
    return true;
}

bool publishWithListing(const std::wstring& path, const std::string& name,
                        const std::wstring& exe, const Listing& listing,
                        std::string& error) {
    if (!publish(path, name, exe, error)) return false;
    // publish() ya valido el nombre y detecto el duplicado; solo queda pegarle
    // la ficha. Si esto fallara, el juego queda publicado sin ficha, que es
    // recuperable -- al reves (ficha sin juego) no lo seria.
    return setListing(path, name, listing, error);
}

bool setListing(const std::wstring& path, const std::string& name,
                const Listing& listing, std::string& error) {
    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g) { error = "Ese juego no esta publicado."; return false; }

    g->listing = listing;
    // Se acotan aqui tambien, y no solo al cargar: la interfaz podria mandar
    // un valor fuera de rango y el catalogo es el ultimo sitio donde pararlo.
    g->listing.fps       = std::clamp(g->listing.fps,       kMinFps, kMaxFps);
    g->listing.minAge    = std::clamp(g->listing.minAge,    kMinAge, kMaxAge);
    g->listing.maxAge    = std::clamp(g->listing.maxAge,    kMinAge, kMaxAge);
    // La franja tiene que ir en orden. Que la minima supere a la maxima no es
    // un valor "raro" que se pueda acotar: es un tramo imposible, y la tienda
    // lo mostraria al reves. Se intercambian, que conserva la intencion del
    // autor mejor que descartar uno de los dos.
    if (g->listing.minAge > g->listing.maxAge)
        std::swap(g->listing.minAge, g->listing.maxAge);
    g->listing.piracy    = std::clamp(g->listing.piracy,    0, 2);
    g->listing.customize = std::clamp(g->listing.customize, 0, 2);

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }
    return true;
}

bool setArchive(const std::wstring& path, const std::string& name,
                const std::wstring& archive, i32 files, u64 bytes,
                std::string& error) {
    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g) {
        // Aun sin publicar: se anota como pendiente y `publish` lo adoptara.
        // Es el caso NORMAL de un juego nuevo, no un error -- guardar los
        // archivos es justo el paso anterior a subirlo.
        Pending p;
        p.game    = name;
        p.archive = archive;
        p.files   = std::max(0, files);
        p.bytes   = bytes;
        return setPending(path, p, error);
    }

    if (g->versions.empty()) {
        // Un juego sin versiones no deberia existir, pero si el catalogo se
        // edito a mano puede pasar. Se crea la primera en vez de fallar: el
        // autor acaba de guardar archivos de verdad y perderlos seria peor.
        g->versions.push_back(Version{ "v1", std::wstring(), nowEpoch() });
    }
    Version& v = g->versions.back();
    v.archive      = archive;
    v.archiveFiles = std::max(0, files);
    v.archiveBytes = bytes;

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }
    return true;
}

namespace {
/// Archivo de pendientes, al lado del catalogo: "<catalogo>.pendientes".
std::wstring pendingPath(const std::wstring& catalog) {
    return catalog + L".pendientes";
}

/// Archivo de reservas, al lado del catalogo.
std::wstring reservePath(const std::wstring& catalog) {
    return catalog + L".reservas";
}

/// Lee las reservas tal cual estan en disco.
std::vector<Reserva> leerReservas(const std::wstring& catalog) {
    std::vector<Reserva> out;
    std::ifstream in(reservePath(catalog).c_str(), std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t p1 = line.find('|');
        if (p1 == std::string::npos) continue;
        const size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos) continue;

        Reserva r;
        r.game = unescapeLine(line.substr(0, p1));
        r.code = line.substr(p1 + 1, p2 - p1 - 1);
        r.at   = std::atoll(line.substr(p2 + 1).c_str());
        if (!r.game.empty() && !r.code.empty()) out.push_back(r);
    }
    return out;
}

bool escribirReservas(const std::wstring& catalog,
                      const std::vector<Reserva>& todas) {
    std::ofstream out(reservePath(catalog).c_str(),
                      std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Codigos reservados para juegos aun sin publicar.\r\n";
    for (const auto& r : todas)
        out << escapeLine(r.game) << "|" << r.code << "|" << r.at << "\r\n";
    return out.good();
}
} // namespace

bool reserveCode(const std::wstring& path, const std::string& name,
                 std::string& codeOut, std::string& error) {
    codeOut.clear();

    const std::string n = trim(name);
    if (n.empty()) {
        error = "Escribe el nombre del juego que vas a subir.";
        return false;
    }

    // Un juego ya publicado tiene su codigo en el catalogo; reservarle otro
    // crearia dos codigos para lo mismo.
    for (const auto& g : load(path)) {
        if (g.name == n) {
            error = "Ese juego ya esta publicado y tiene codigo.";
            return false;
        }
    }

    std::vector<Reserva> todas = leerReservas(path);
    for (const auto& r : todas) {
        if (r.game == n) {
            // No es un fallo: ya lo tiene. Se devuelve el mismo, que es lo
            // util -- el autor queria su codigo y aqui esta.
            codeOut = r.code;
            return true;
        }
    }

    // El codigo sale del nombre, igual que el de un juego publicado. Asi, al
    // publicarlo de verdad, coincide con el que generaria `load` aunque la
    // reserva se hubiera perdido.
    Reserva r;
    r.game = n;
    r.code = makeGameCode(n);
    r.at   = nowEpoch();

    // Colision con un juego ya publicado: se le da la vuelta al nombre para
    // derivar otro distinto. Es improbable con 32^10 combinaciones, pero dos
    // codigos iguales harian que `findByCode` devolviera el juego equivocado.
    for (const auto& g : load(path)) {
        if (g.code == r.code) {
            r.code = makeGameCode(n + "#2");
            break;
        }
    }

    todas.insert(todas.begin(), r);   // la mas reciente, primera
    if (!escribirReservas(path, todas)) {
        error = "No se pudo guardar la reserva.";
        return false;
    }
    codeOut = r.code;
    return true;
}

std::vector<Reserva> reservations(const std::wstring& path) {
    return leerReservas(path);
}

void clearReservation(const std::wstring& path, const std::string& name) {
    std::vector<Reserva> todas = leerReservas(path);
    std::vector<Reserva> quedan;
    quedan.reserve(todas.size());
    for (const auto& r : todas)
        if (r.game != name) quedan.push_back(r);
    if (quedan.size() != todas.size()) escribirReservas(path, quedan);
}

namespace {

/// Lee una linea del registro de pendientes.
///
/// El formato nacio con cuatro campos (juego|paquete|archivos|bytes) y
/// ahora lleva dos mas para el precio. Un archivo viejo sigue valiendo: lo
/// que falte se queda en su valor por defecto, que es "precio sin decidir".
bool parsePendingLine(const std::string& line, Pending& out) {
    if (line.empty() || line[0] == '#') return false;

    std::vector<std::string> f;
    size_t ini = 0;
    while (true) {
        const size_t b = line.find('|', ini);
        if (b == std::string::npos) { f.push_back(line.substr(ini)); break; }
        f.push_back(line.substr(ini, b - ini));
        ini = b + 1;
    }
    if (f.size() < 4) return false;

    out = Pending{};
    out.game    = unescapeLine(f[0]);
    out.archive = fromUtf8(f[1]);
    out.files   = std::atoi(f[2].c_str());
    out.bytes   = static_cast<u64>(std::atoll(f[3].c_str()));
    if (f.size() >= 6) {
        out.price    = std::atoi(f[4].c_str());
        out.priceSet = (std::atoi(f[5].c_str()) != 0);
    }
    return true;
}

/// Carga el registro entero.
std::vector<Pending> loadPendings(const std::wstring& path) {
    std::vector<Pending> all;
    std::ifstream in(pendingPath(path).c_str(), std::ios::binary);
    std::string line;
    while (in && std::getline(in, line)) {
        Pending q;
        if (parsePendingLine(trim(line), q)) all.push_back(q);
    }
    return all;
}

/// Vuelca el registro entero, sobrescribiendo.
bool writePendings(const std::wstring& path, const std::vector<Pending>& all) {
    std::ofstream out(pendingPath(path).c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Archivos guardados de juegos aun sin publicar.\r\n";
    for (const auto& q : all)
        out << escapeLine(q.game) << "|" << toUtf8(q.archive) << "|"
            << q.files << "|" << q.bytes << "|"
            << q.price << "|" << (q.priceSet ? 1 : 0) << "\r\n";
    return out.good();
}

} // namespace

bool setPending(const std::wstring& path, const Pending& p, std::string& error) {
    if (p.game.empty()) { error = "Falta el nombre del juego."; return false; }

    // Un juego tiene como mucho un paquete pendiente: el ultimo que guardo.
    std::vector<Pending> all = loadPendings(path);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const Pending& q) { return q.game == p.game; }),
              all.end());
    all.push_back(p);

    if (!writePendings(path, all)) {
        error = "No se pudo guardar el registro de archivos.";
        return false;
    }
    return true;
}

std::vector<Pending> pendings(const std::wstring& path) {
    return loadPendings(path);
}

Pending getPending(const std::wstring& path, const std::string& name) {
    for (const Pending& q : loadPendings(path))
        if (q.game == name) return q;
    return Pending{};
}

void clearPending(const std::wstring& path, const std::string& name) {
    std::vector<Pending> all = loadPendings(path);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const Pending& q) { return q.game == name; }),
              all.end());
    writePendings(path, all);
}

bool hasArchive(const std::wstring& path, const std::string& name) {
    std::vector<Game> all = load(path);
    const Game* g = nullptr;
    for (const auto& x : all)
        if (x.name == name) { g = &x; break; }
    if (g && !g->versions.empty() && !g->versions.back().archive.empty())
        return true;

    // Tambien cuenta lo guardado antes de publicar: para el autor es el mismo
    // trabajo hecho, y es el orden natural (guardar y luego subir).
    return !getPending(path, name).archive.empty();
}

bool addVersion(const std::wstring& path, const std::string& name,
                const std::wstring& exe, std::string& error) {
    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g) {
        error = "Ese juego no esta publicado. Subelo primero con SUBIR JUEGO.";
        return false;
    }

    // La etiqueta sale del numero de versiones ya existentes, asi que la
    // secuencia es continua aunque el catalogo se edite a mano.
    char label[16];
    std::snprintf(label, sizeof(label), "v%zu", g->versions.size() + 1);
    g->versions.push_back(Version{ label, exe, nowEpoch() });

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }
    return true;
}

bool addVersionWithLog(const std::wstring& path, const std::string& name,
                       const std::wstring& exe, const std::string& changelog,
                       std::string& error) {
    if (!addVersion(path, name, exe, error)) return false;

    // La version ya esta anadida; solo queda pegarle sus notas. Se hace en
    // dos pasos para no duplicar la logica de etiquetado de addVersion.
    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g || g->versions.empty()) {
        error = "No se encontro la version recien anadida.";
        return false;
    }
    g->versions.back().changelog = changelog;

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }
    return true;
}

LayoutCheck checkLayoutCompatible(const std::wstring& oldExe,
                                  const std::wstring& newExe) {
    LayoutCheck out;

    // Se compara como esta ORGANIZADO cada arbol, no archivo por archivo: lo
    // que rompe una actualizacion es que las carpetas de datos cambien de
    // sitio o de formato, no que haya mas o menos texturas.
    auto huella = [](const std::wstring& ruta) {
        std::vector<std::wstring> marcas;

        std::wstring raiz = ruta;
        const DWORD at = GetFileAttributesW(ruta.c_str());
        if (at == INVALID_FILE_ATTRIBUTES) return marcas;
        if (!(at & FILE_ATTRIBUTE_DIRECTORY)) {
            const size_t s = ruta.find_last_of(L"\\/");
            raiz = (s == std::wstring::npos) ? L"." : ruta.substr(0, s);
        }

        std::vector<std::wstring> archivos;
        listFiles(raiz, L"", archivos);

        for (const auto& rel : archivos) {
            // Primera carpeta del camino: dice donde viven los datos.
            const size_t bar = rel.find_first_of(L"\\/");
            if (bar != std::wstring::npos) {
                std::wstring dir = rel.substr(0, bar);
                for (auto& c : dir) c = static_cast<wchar_t>(towlower(c));
                if (std::find(marcas.begin(), marcas.end(), L"D:" + dir) ==
                    marcas.end())
                    marcas.push_back(L"D:" + dir);
            }
            // Extension: dice en que formato se guardan.
            const size_t pt = rel.find_last_of(L'.');
            if (pt != std::wstring::npos) {
                std::wstring ext = rel.substr(pt);
                for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                if (std::find(marcas.begin(), marcas.end(), L"E:" + ext) ==
                    marcas.end())
                    marcas.push_back(L"E:" + ext);
            }
        }
        return marcas;
    };

    const std::vector<std::wstring> vieja = huella(oldExe);
    const std::vector<std::wstring> nueva = huella(newExe);

    // Sin referencia no hay nada que avisar: la primera version de un juego
    // no puede ser incompatible con nada.
    if (vieja.empty()) return out;

    for (const auto& m : vieja) {
        if (std::find(nueva.begin(), nueva.end(), m) != nueva.end()) ++out.shared;
        else ++out.onlyOld;
    }
    for (const auto& m : nueva)
        if (std::find(vieja.begin(), vieja.end(), m) == vieja.end()) ++out.onlyNew;

    // Se avisa cuando se perdio mas de la mitad de lo que habia: a partir de
    // ahi es probable que las partidas guardadas ya no se encuentren.
    const i32 total = out.shared + out.onlyOld;
    if (total > 0 && out.onlyOld * 2 > total) {
        out.compatible = false;
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "La organizacion cambio mucho: %d de %d partes ya no "
                      "estan. Las partidas guardadas podrian no encontrarse.",
                      out.onlyOld, total);
        out.reason = buf;
    } else {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "Compatible: %d partes coinciden, %d nuevas.",
                      out.shared, out.onlyNew);
        out.reason = buf;
    }
    return out;
}

bool remove(const std::wstring& path, const std::string& name,
            std::string& error) {
    std::vector<Game> all = load(path);
    const size_t antes = all.size();

    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const Game& g) { return g.name == name; }),
              all.end());

    if (all.size() == antes) {
        error = "Ese juego no esta publicado.";
        return false;
    }
    if (!writeAll(path, all)) {
        error = "No se pudo guardar el catalogo.";
        return false;
    }
    return true;
}

bool setPrice(const std::wstring& path, const std::string& name,
              const Price& price, std::string& error) {
    std::vector<Game> all = load(path);
    Game* g = findGame(all, name);
    if (!g) { error = "Ese juego no esta publicado."; return false; }

    // Se acota al rango del deslizador: un precio negativo o por encima del
    // maximo solo puede venir de un archivo editado a mano.
    g->price.usdCents = std::clamp(price.usdCents, 0, kMaxUsdCents);

    if (!writeAll(path, all)) { error = "No se pudo guardar el catalogo."; return false; }
    return true;
}

void formatPrice(i32 usdCents, std::string& usd, std::string& mxn, std::string& eur) {
    if (usdCents <= 0) {
        usd = mxn = eur = "GRATIS";
        return;
    }

    /// Formatea centavos como "1,234.56" con separador de millares.
    auto conMillares = [](long long cents) {
        char raw[32];
        std::snprintf(raw, sizeof(raw), "%lld", cents / 100);
        const std::string enteros = raw;

        std::string agrupado;
        for (size_t i = 0; i < enteros.size(); ++i) {
            // Coma cada tres digitos contando desde la derecha.
            if (i > 0 && (enteros.size() - i) % 3 == 0) agrupado += ',';
            agrupado += enteros[i];
        }
        char dec[8];
        std::snprintf(dec, sizeof(dec), ".%02lld", cents % 100);
        return agrupado + dec;
    };

    // Tipos de cambio FIJOS escritos aqui. No son cotizaciones en vivo: sin
    // servidor no hay forma de mantenerlos al dia, asi que se dejan explicitos
    // y redondos en vez de fingir una precision que no existe. 400 USD ->
    // 8900 MXN es la equivalencia que fija el maximo del deslizador.
    const long long mxnCents = static_cast<long long>(usdCents) * 8900 / 400;
    const long long eurCents = usdCents;   // paridad, como en el enunciado

    usd = "$" + conMillares(usdCents)  + " USD";
    mxn = "$" + conMillares(mxnCents)  + " MXN";
    eur = "E" + conMillares(eurCents)  + " EUR";   // la fuente 5x7 no tiene glifo del euro
}

} // namespace devcatalog

// ------------------------------------------------- resenas de jugadores ---
namespace reviews {

namespace {

/// Escribe la lista entera. Unica ruta de escritura, igual que el catalogo.
bool writeAll(const std::wstring& path, const std::vector<Review>& all) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << "# Resenas de Ludora.\r\n";
    for (const auto& r : all) {
        // Una linea por resena, con los campos separados por '|'. El texto va
        // el ultimo y escapado, que es lo que permite que lleve saltos de
        // linea sin partir el registro.
        out << "rev=" << r.game << "|" << r.version << "|" << r.author << "|"
            << r.stars << "|" << r.at << "|" << escapeLine(r.text) << "\r\n";
    }
    return out.good();
}

} // namespace

std::vector<Review> load(const std::wstring& path) {
    std::vector<Review> out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return out;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trim(line.substr(0, eq)) != "rev") continue;

        // Se parte en 6 campos; el ultimo se toma entero porque el texto
        // puede contener cualquier cosa menos un salto de linea sin escapar.
        std::string v = trim(line.substr(eq + 1));
        std::string campo[6];
        size_t ini = 0;
        for (i32 i = 0; i < 5; ++i) {
            const size_t bar = v.find('|', ini);
            if (bar == std::string::npos) { ini = std::string::npos; break; }
            campo[i] = v.substr(ini, bar - ini);
            ini = bar + 1;
        }
        if (ini == std::string::npos) continue;   // registro incompleto
        campo[5] = v.substr(ini);

        Review r;
        r.game    = campo[0];
        r.version = campo[1];
        r.author  = campo[2];
        r.stars   = std::clamp(std::atoi(campo[3].c_str()), 0, 5);
        r.at      = std::atoll(campo[4].c_str());
        r.text    = unescapeLine(campo[5]);
        if (!r.game.empty()) out.push_back(std::move(r));
    }
    return out;
}

bool save(const std::wstring& path, const std::vector<Review>& all) {
    return writeAll(path, all);
}

bool add(const std::wstring& path, const Review& r, std::string& error) {
    if (r.game.empty())  { error = "La resena necesita un juego."; return false; }
    if (r.text.empty())  { error = "Escribe algo en la resena.";   return false; }
    if (r.stars < 0 || r.stars > 5) {
        error = "La puntuacion va de 1 a 5 estrellas.";
        return false;
    }

    std::vector<Review> all = load(path);
    Review copia = r;
    if (copia.at == 0) copia.at = nowEpoch();
    all.push_back(std::move(copia));

    if (!writeAll(path, all)) { error = "No se pudieron guardar las resenas."; return false; }
    return true;
}

std::vector<Review> forGame(const std::wstring& path, const std::string& game) {
    std::vector<Review> out;
    for (auto& r : load(path))
        if (r.game == game) out.push_back(std::move(r));

    // De la mas reciente a la mas antigua: es el orden en que se leen.
    std::sort(out.begin(), out.end(),
              [](const Review& a, const Review& b) { return a.at > b.at; });
    return out;
}

Summary summaryFor(const std::wstring& path, const std::string& game) {
    Summary s;
    for (const auto& r : load(path)) {
        if (r.game != game) continue;
        ++s.count;
        if (r.stars > 0) { s.sumStars += r.stars; ++s.rated; }
    }
    return s;
}

std::vector<std::pair<std::string, Summary>> summaryAll(const std::wstring& path) {
    std::vector<std::pair<std::string, Summary>> out;
    for (const auto& r : load(path)) {
        Summary* s = nullptr;
        for (auto& par : out)
            if (par.first == r.game) { s = &par.second; break; }
        if (!s) {
            out.push_back({ r.game, Summary{} });
            s = &out.back().second;
        }
        ++s->count;
        if (r.stars > 0) { s->sumStars += r.stars; ++s->rated; }
    }
    return out;
}

} // namespace reviews

// ------------------------------------------- historial de la biblioteca ---
namespace history {

namespace {

/// Escribe el historial entero. Unica ruta de escritura: todas las funciones
/// que modifican cargan, cambian y vuelven aqui.
bool writeAll(const std::wstring& path, const std::vector<GameHistory>& all) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << "# Historial de la biblioteca de Ludora.\r\n";
    for (const auto& g : all) {
        out << "game=" << g.name << "\r\n";
        for (const auto& p : g.prices)
            out << "price=" << p.usdCents << "|" << p.at << "\r\n";
        for (const auto& v : g.versions)
            out << "ver=" << v.label << "|" << v.at << "|"
                << toUtf8(v.archivePath) << "|" << v.bytes << "\r\n";
    }
    return out.good();
}

GameHistory* findGame(std::vector<GameHistory>& all, const std::string& name) {
    for (auto& g : all)
        if (g.name == name) return &g;
    return nullptr;
}

/// Hora actual en segundos desde epoch. Misma conversion que en devcatalog:
/// FILETIME cuenta intervalos de 100 ns desde 1601, epoch Unix desde 1970.
long long nowEpochHist() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const u64 t = (static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<long long>(t / 10000000ULL) - 11644473600LL;
}

/// Trocea "a|b|c|d" en sus campos.
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t ini = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(ini, i - ini));
            ini = i + 1;
        }
    }
    return out;
}

} // namespace

std::vector<GameHistory> load(const std::wstring& path) {
    std::vector<GameHistory> out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return out;

    std::string line;
    GameHistory cur;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if (k == "game") {
            if (!cur.name.empty()) { out.push_back(cur); cur = GameHistory{}; }
            cur.name = v;
        } else if (k == "price") {
            const std::vector<std::string> f = split(v, '|');
            if (f.size() < 2) continue;
            PricePoint p;
            p.usdCents = std::atoi(f[0].c_str());
            p.at       = std::atoll(f[1].c_str());
            cur.prices.push_back(p);
        } else if (k == "ver") {
            const std::vector<std::string> f = split(v, '|');
            if (f.size() < 4) continue;
            VersionRecord r;
            r.label       = f[0];
            r.at          = std::atoll(f[1].c_str());
            r.archivePath = fromUtf8(f[2]);
            r.bytes       = static_cast<u64>(std::atoll(f[3].c_str()));
            cur.versions.push_back(r);
        }
    }
    if (!cur.name.empty()) out.push_back(cur);
    return out;
}

bool save(const std::wstring& path, const std::vector<GameHistory>& all) {
    return writeAll(path, all);
}

GameHistory forGame(const std::wstring& path, const std::string& name, bool* found) {
    std::vector<GameHistory> all = load(path);
    if (const GameHistory* g = findGame(all, name)) {
        if (found) *found = true;
        return *g;
    }
    if (found) *found = false;
    GameHistory vacio;
    vacio.name = name;
    return vacio;
}

bool noteInitialPrice(const std::wstring& path, const std::string& name,
                      i32 usdCents, std::string& error) {
    if (name.empty()) { error = "El juego necesita un nombre."; return false; }

    std::vector<GameHistory> all = load(path);
    GameHistory* g = findGame(all, name);
    if (!g) {
        all.push_back(GameHistory{ name, {}, {} });
        g = &all.back();
    }
    // El precio inicial se fija UNA vez: si ya hay puntos, esta llamada no
    // debe reescribir el origen de la linea de tiempo.
    if (!g->prices.empty()) return true;

    g->prices.push_back(PricePoint{ std::clamp(usdCents, 0, devcatalog::kMaxUsdCents),
                                    nowEpochHist() });
    if (!writeAll(path, all)) { error = "No se pudo guardar el historial."; return false; }
    return true;
}

bool notePriceChange(const std::wstring& path, const std::string& name,
                     i32 usdCents, std::string& error) {
    const i32 precio = std::clamp(usdCents, 0, devcatalog::kMaxUsdCents);

    std::vector<GameHistory> all = load(path);
    GameHistory* g = findGame(all, name);
    if (!g) {
        all.push_back(GameHistory{ name, {}, {} });
        g = &all.back();
    }
    // Un precio identico al vigente no aporta nada a la linea de tiempo y la
    // llenaria de puntos repetidos.
    if (!g->prices.empty() && g->prices.back().usdCents == precio) return true;

    g->prices.push_back(PricePoint{ precio, nowEpochHist() });
    if (!writeAll(path, all)) { error = "No se pudo guardar el historial."; return false; }
    return true;
}

bool noteInitialVersion(const std::wstring& path, const std::string& name,
                        const std::wstring& archivePath, u64 bytes,
                        std::string& error) {
    std::vector<GameHistory> all = load(path);
    GameHistory* g = findGame(all, name);
    if (!g) {
        all.push_back(GameHistory{ name, {}, {} });
        g = &all.back();
    }
    if (!g->versions.empty()) return true;   // ya tenia version registrada

    g->versions.push_back(VersionRecord{ "v1", nowEpochHist(), archivePath, bytes });
    if (!writeAll(path, all)) { error = "No se pudo guardar el historial."; return false; }
    return true;
}

bool archiveAndAddVersion(const std::wstring& path, const std::string& name,
                          const std::wstring& newArchivePath, u64 newBytes,
                          std::string& error) {
    std::vector<GameHistory> all = load(path);
    GameHistory* g = findGame(all, name);
    if (!g) {
        error = "Ese juego no esta en la biblioteca.";
        return false;
    }
    if (g->versions.empty()) {
        error = "El juego no tiene una version registrada que archivar.";
        return false;
    }

    // La anterior se queda como esta -- ya lleva su archivePath, que es lo que
    // la mantiene recuperable -- y la nueva se anade al final, pasando a ser
    // la jugable por el mero hecho de ser la ultima.
    char label[16];
    std::snprintf(label, sizeof(label), "v%zu", g->versions.size() + 1);
    g->versions.push_back(VersionRecord{ label, nowEpochHist(),
                                         newArchivePath, newBytes });

    if (!writeAll(path, all)) { error = "No se pudo guardar el historial."; return false; }
    return true;
}

std::string formatDate(long long epoch) {
    if (epoch <= 0) return "--/--/--";

    // De epoch a FILETIME y de ahi a hora local: es la conversion inversa de
    // nowEpochHist(), y deja que Windows haga el calendario (bisiestos, huso).
    const u64 t = (static_cast<u64>(epoch) + 11644473600ULL) * 10000000ULL;
    FILETIME ft{};
    ft.dwLowDateTime  = static_cast<DWORD>(t & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(t >> 32);

    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st))
        return "--/--/--";

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%02d",
                  st.wDay, st.wMonth, st.wYear % 100);
    return buf;
}

std::string formatBytes(u64 bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        std::snprintf(buf, sizeof(buf), "%.1f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        std::snprintf(buf, sizeof(buf), "%llu MB",
                      static_cast<unsigned long long>(bytes / (1024ULL * 1024ULL)));
    else if (bytes >= 1024ULL)
        std::snprintf(buf, sizeof(buf), "%llu KB",
                      static_cast<unsigned long long>(bytes / 1024ULL));
    else
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    return buf;
}

} // namespace history

// ----------------------------------------------------------- comunicados ---
namespace notices {

namespace {

/// Trocea "a|b|c|d|e" en sus campos. Propio de este namespace: el de
/// `history` no es visible aqui.
///
/// Los campos ya vienen escapados, y escapeLine convierte cualquier '|' del
/// texto en "\p", asi que un cuerpo que contenga barras no parte el registro.
std::vector<std::string> splitNotice(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t ini = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(ini, i - ini));
            ini = i + 1;
        }
    }
    return out;
}

/// Escribe todos los comunicados. Es la unica ruta de escritura.
///
/// El cuerpo pasa por escapeLine: un comunicado puede tener treinta parrafos
/// -- se dijo "sin limite" -- y sin escapar los saltos, el primer Enter
/// partiria el registro y el resto del archivo se leeria como basura.
bool writeAllNotices(const std::wstring& path, const std::vector<Notice>& all) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "# Comunicados de los creadores de Ludora.\r\n";
    for (const Notice& n : all) {
        if (n.game.empty() || n.title.empty()) continue;
        out << "aviso=" << escapeLine(n.game)  << "|"
                        << escapeLine(n.title) << "|"
                        << n.at                << "|"
                        << escapeLine(toUtf8(n.image)) << "|"
                        << escapeLine(n.body)  << "\r\n";
    }
    return out.good();
}

} // namespace

std::vector<Notice> load(const std::wstring& path) {
    std::vector<Notice> out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return out;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trim(line.substr(0, eq)) != "aviso") continue;

        const std::string v = line.substr(eq + 1);
        const std::vector<std::string> f = splitNotice(v, '|');
        if (f.size() < 5) continue;

        Notice n;
        n.game  = unescapeLine(f[0]);
        n.title = unescapeLine(f[1]);
        n.at    = std::atoll(f[2].c_str());
        n.image = fromUtf8(unescapeLine(f[3]));
        n.body  = unescapeLine(f[4]);
        out.push_back(n);
    }

    // Del mas nuevo al mas viejo: es el orden en que se quieren leer.
    std::stable_sort(out.begin(), out.end(),
                     [](const Notice& a, const Notice& b) { return a.at > b.at; });
    return out;
}

std::vector<Notice> forGame(const std::wstring& path, const std::string& game) {
    std::vector<Notice> out;
    if (game.empty()) return out;
    for (const Notice& n : load(path))
        if (n.game == game) out.push_back(n);
    return out;
}

bool publish(const std::wstring& path, const Notice& n, std::string& error) {
    if (n.game.empty())  { error = "El comunicado necesita un juego.";  return false; }
    if (n.title.empty()) { error = "Escribe el titulo del comunicado."; return false; }

    std::vector<Notice> all = load(path);
    Notice copia = n;
    if (copia.at <= 0) copia.at = nowEpoch();
    all.push_back(copia);

    if (!writeAllNotices(path, all)) {
        error = "No se pudieron guardar los comunicados.";
        return false;
    }
    return true;
}

i32 countForGame(const std::wstring& path, const std::string& game) {
    return static_cast<i32>(forGame(path, game).size());
}

} // namespace notices
} // namespace ludora
