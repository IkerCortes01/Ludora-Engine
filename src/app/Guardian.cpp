#include "app/Guardian.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <windows.h>

namespace ludora {
namespace guardian {

namespace {

// ------------------------------------------------------------------- AMSI ---
//
// Se carga amsi.dll a mano en vez de enlazar con amsi.lib. Dos razones: no
// obliga a tener el SDK con esa libreria para compilar, y si el equipo no
// tiene AMSI (o esta roto) Ludora arranca igual en vez de negarse a abrir por
// una dependencia que solo usa al descargar un juego.

typedef HRESULT (WINAPI *AmsiInitialize_t)(LPCWSTR, void**);
typedef HRESULT (WINAPI *AmsiOpenSession_t)(void*, void**);
typedef void    (WINAPI *AmsiCloseSession_t)(void*, void*);
typedef void    (WINAPI *AmsiUninitialize_t)(void*);
typedef HRESULT (WINAPI *AmsiScanBuffer_t)(void*, PVOID, ULONG, LPCWSTR,
                                           void*, i32*);

constexpr i32 kAmsiDetected = 32768;   // AMSI_RESULT_DETECTED

/// Envoltorio del motor de firmas del sistema. Se crea una vez.
struct Amsi {
    HMODULE            dll     = nullptr;
    void*              ctx     = nullptr;
    AmsiOpenSession_t  abrir   = nullptr;
    AmsiCloseSession_t cerrarS = nullptr;
    AmsiUninitialize_t fin     = nullptr;
    AmsiScanBuffer_t   escanear= nullptr;
    bool               listo   = false;
    std::string        nombre;

    Amsi() {
        dll = LoadLibraryW(L"amsi.dll");
        if (!dll) return;

        auto ini = reinterpret_cast<AmsiInitialize_t>(
            GetProcAddress(dll, "AmsiInitialize"));
        abrir    = reinterpret_cast<AmsiOpenSession_t>(
            GetProcAddress(dll, "AmsiOpenSession"));
        cerrarS  = reinterpret_cast<AmsiCloseSession_t>(
            GetProcAddress(dll, "AmsiCloseSession"));
        fin      = reinterpret_cast<AmsiUninitialize_t>(
            GetProcAddress(dll, "AmsiUninitialize"));
        escanear = reinterpret_cast<AmsiScanBuffer_t>(
            GetProcAddress(dll, "AmsiScanBuffer"));

        if (!ini || !abrir || !cerrarS || !fin || !escanear) return;
        if (ini(L"Ludora", &ctx) != S_OK || !ctx) return;

        listo = true;
        // AMSI no dice que antivirus hay detras. Se informa de lo que se puede
        // afirmar con certeza -- que hay uno y respondio -- sin inventar
        // nombres: decir "Windows Defender" cuando podria ser otro seria
        // mentirle al usuario sobre quien le esta protegiendo.
        nombre = "el antivirus del sistema (via AMSI)";
    }

    ~Amsi() {
        if (ctx && fin) fin(ctx);
        if (dll) FreeLibrary(dll);
    }

    /// Devuelve true si el motor marca el contenido como malware.
    /// `consultado` dice si de verdad se pudo preguntar.
    bool detecta(const u8* datos, size_t n, const std::wstring& nombreVis,
                 bool& consultado) {
        consultado = false;
        if (!listo || n == 0) return false;

        // AMSI toma la longitud en ULONG. Un bloque mayor se analiza por
        // partes en vez de truncarlo en silencio.
        void* ses = nullptr;
        if (abrir(ctx, &ses) != S_OK) return false;

        bool malo = false;
        size_t hecho = 0;
        constexpr size_t kTrozo = 32u * 1024 * 1024;

        while (hecho < n && !malo) {
            const ULONG parte = static_cast<ULONG>(
                (std::min)(n - hecho, kTrozo));
            i32 res = 0;
            const HRESULT hr = escanear(
                ctx, const_cast<u8*>(datos + hecho), parte,
                nombreVis.empty() ? L"juego" : nombreVis.c_str(), ses, &res);
            if (hr == S_OK) {
                consultado = true;
                if (res >= kAmsiDetected) malo = true;
            }
            hecho += parte;
        }

        if (ses) cerrarS(ctx, ses);
        return malo;
    }
};

std::mutex  g_mx;
Amsi*       g_amsi = nullptr;

Amsi& motor() {
    // Una sola instancia, creada la primera vez que se usa. El candado la
    // protege porque la revision corre en su propio hilo.
    if (!g_amsi) g_amsi = new Amsi();
    return *g_amsi;
}

// ------------------------------------------------------- utilidades de ruta ---

std::wstring aMinusculas(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring extensionDe(const std::wstring& rel) {
    const size_t p = rel.find_last_of(L'.');
    const size_t s = rel.find_last_of(L"\\/");
    if (p == std::wstring::npos) return {};
    if (s != std::wstring::npos && p < s) return {};   // el punto iba en la carpeta
    return aMinusculas(rel.substr(p));
}

std::string aUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

/// Extensiones que EJECUTAN algo. Un juego trae su .exe y sus .dll, y eso es
/// normal; el resto de esta lista no tiene ninguna razon de estar dentro de un
/// paquete de juego.
bool esScriptOEjecutable(const std::wstring& ext, bool& esperable) {
    esperable = (ext == L".exe" || ext == L".dll");
    static const wchar_t* kEjec[] = {
        L".exe", L".dll", L".com", L".scr", L".pif", L".cpl", L".msi",
        L".bat", L".cmd", L".ps1", L".psm1", L".vbs", L".vbe", L".js",
        L".jse", L".wsf", L".wsh", L".hta", L".reg", L".lnk", L".jar",
        L".msc", L".sys", L".drv", L".ocx", L".url",
    };
    for (const wchar_t* e : kEjec) if (ext == e) return true;
    return false;
}

/// Firma real del archivo por sus primeros bytes, para pillar un ejecutable
/// con la extension cambiada.
///
/// Es la comprobacion que mas aporta de todas las propias: renombrar
/// "virus.exe" a "textura.png" es el truco mas viejo que hay, y un antivirus
/// que mira por extension no lo ve. Aqui se mira el contenido.
enum class Firma { Desconocida, PE, ELF, MachO, Script, Zip };

Firma firmaDe(const u8* d, size_t n) {
    if (n >= 2 && d[0] == 'M' && d[1] == 'Z') return Firma::PE;       // .exe/.dll
    if (n >= 4 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F')
        return Firma::ELF;
    if (n >= 4 && ((d[0] == 0xCF && d[1] == 0xFA && d[2] == 0xED && d[3] == 0xFE) ||
                   (d[0] == 0xFE && d[1] == 0xED && d[2] == 0xFA && d[3] == 0xCF)))
        return Firma::MachO;
    if (n >= 2 && d[0] == '#' && d[1] == '!') return Firma::Script;
    if (n >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 0x03 && d[3] == 0x04)
        return Firma::Zip;
    return Firma::Desconocida;
}

const wchar_t* nombreFirma(Firma f) {
    switch (f) {
        case Firma::PE:     return L"un ejecutable de Windows";
        case Firma::ELF:    return L"un ejecutable de Linux";
        case Firma::MachO:  return L"un ejecutable de macOS";
        case Firma::Script: return L"un script";
        case Firma::Zip:    return L"un archivo comprimido";
        default:            return L"otra cosa";
    }
}

/// Extensiones que se presentan como datos inofensivos.
bool pareceDatos(const std::wstring& ext) {
    static const wchar_t* kDatos[] = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".webp", L".tga",
        L".wav", L".mp3", L".ogg", L".flac", L".txt", L".json", L".xml",
        L".ini", L".cfg", L".csv", L".md", L".dat", L".obj", L".fbx",
        L".ttf", L".otf", L".mp4", L".webm",
    };
    for (const wchar_t* e : kDatos) if (ext == e) return true;
    return false;
}

void listar(const std::wstring& dir, const std::wstring& rel,
            std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::pair<std::wstring, std::wstring>> subdirs;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        const std::wstring r = rel.empty() ? n : (rel + L"\\" + n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            subdirs.push_back({n, r});
        else
            out.push_back(r);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const auto& sd : subdirs) listar(dir + L"\\" + sd.first, sd.second, out);
}

} // namespace

// ------------------------------------------------------------------ informe ---

bool Informe::bloqueado() const {
    for (const auto& h : hallazgos)
        if (h.nivel == Nivel::Peligro) return true;
    return false;
}

Nivel Informe::peor() const {
    Nivel n = Nivel::Info;
    for (const auto& h : hallazgos)
        if (static_cast<i32>(h.nivel) > static_cast<i32>(n)) n = h.nivel;
    return n;
}

std::string Informe::resumen() const {
    if (!hecho) return "Revision sin terminar.";

    i32 peligros = 0, avisos = 0;
    for (const auto& h : hallazgos) {
        if (h.nivel == Nivel::Peligro) ++peligros;
        else if (h.nivel == Nivel::Aviso) ++avisos;
    }

    char b[300];
    if (peligros > 0) {
        std::snprintf(b, sizeof(b),
                      "Se detuvo la instalacion: %d amenaza%s en %d archivos.",
                      peligros, peligros == 1 ? "" : "s", archivos);
        return b;
    }
    if (avisos > 0) {
        std::snprintf(b, sizeof(b),
                      "%d archivos revisados: %d cosa%s que conviene que veas.",
                      archivos, avisos, avisos == 1 ? "" : "s");
        return b;
    }
    // Sin motor de firmas se dice: "limpio" a secas seria enganoso.
    if (!motorFirmas) {
        std::snprintf(b, sizeof(b),
                      "%d archivos revisados, sin nada raro. No habia "
                      "antivirus en el equipo al que consultar.", archivos);
        return b;
    }
    std::snprintf(b, sizeof(b),
                  "%d archivos revisados con %s: limpio.",
                  archivos, motorNombre.c_str());
    return b;
}

// ----------------------------------------------------------------- escaneo ---

bool motorDisponible() {
    std::lock_guard<std::mutex> lk(g_mx);
    return motor().listo;
}

std::string nombreMotor() {
    std::lock_guard<std::mutex> lk(g_mx);
    return motor().nombre;
}

void cerrar() {
    std::lock_guard<std::mutex> lk(g_mx);
    delete g_amsi;
    g_amsi = nullptr;
}

bool revisarBloque(const u8* datos, size_t n,
                   const std::wstring& nombreVisible,
                   std::vector<Hallazgo>& hallazgosOut) {
    if (!datos || n == 0) return true;

    bool consultado = false;
    bool malo = false;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        malo = motor().detecta(datos, n, nombreVisible, consultado);
    }

    if (malo) {
        Hallazgo h;
        h.nivel   = Nivel::Peligro;
        h.motivo  = "El antivirus del equipo lo identifico como codigo "
                    "malicioso.";
        h.archivo = nombreVisible;
        hallazgosOut.push_back(std::move(h));
        return false;
    }
    return true;
}

Informe revisarCarpeta(const std::wstring& dir,
                       std::atomic<float>* progress,
                       const std::atomic<bool>* cancel) {
    Informe inf;

    LARGE_INTEGER frec{}, t0{}, t1{};
    QueryPerformanceFrequency(&frec);
    QueryPerformanceCounter(&t0);

    {
        std::lock_guard<std::mutex> lk(g_mx);
        inf.motorFirmas = motor().listo;
        inf.motorNombre = motor().nombre;
    }

    std::vector<std::wstring> rels;
    listar(dir, L"", rels);
    if (rels.empty()) {
        inf.hecho = true;
        return inf;
    }

    // Un buffer que se reutiliza: con juegos de miles de archivos, reservar y
    // liberar por archivo se nota mas que la propia lectura.
    std::vector<u8> buf;
    i32 ejecutablesInesperados = 0;

    for (size_t i = 0; i < rels.size(); ++i) {
        if (cancel && cancel->load()) return inf;   // hecho = false: incompleta
        if (progress)
            progress->store(static_cast<f32>(i) / static_cast<f32>(rels.size()));

        const std::wstring& rel = rels[i];
        const std::wstring full = dir + L"\\" + rel;
        const std::wstring ext  = extensionDe(rel);

        HANDLE h = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            // Un archivo que no se puede abrir NO se pasa por alto en
            // silencio: en una revision de seguridad, "no pude mirarlo" no es
            // lo mismo que "esta limpio", y callarlo daria un informe que
            // afirma mas de lo que comprobo.
            //
            // El caso mas comun es bueno: el antivirus del equipo ya se llevo
            // el archivo (proteccion en tiempo real), y entonces la amenaza ni
            // siquiera llego a quedarse. Pero tambien puede ser un archivo
            // bloqueado a proposito, y eso hay que decirlo.
            const DWORD err = GetLastError();
            Hallazgo hh;
            hh.archivo = rel;
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                hh.nivel  = Nivel::Info;
                hh.motivo = "Desaparecio mientras se revisaba: lo mas probable "
                            "es que el antivirus del equipo ya lo haya "
                            "retirado.";
            } else {
                hh.nivel  = Nivel::Aviso;
                hh.motivo = "No se pudo abrir para revisarlo, asi que no hay "
                            "forma de saber que contiene.";
            }
            inf.hallazgos.push_back(std::move(hh));
            continue;
        }

        LARGE_INTEGER li{};
        if (!GetFileSizeEx(h, &li)) { CloseHandle(h); continue; }
        const u64 n = static_cast<u64>(li.QuadPart);

        ++inf.archivos;
        inf.bytes += n;

        // --- 1) la cabecera, para saber QUE es de verdad ---
        u8 cab[64] = {0};
        DWORD leido = 0;
        ReadFile(h, cab, sizeof(cab), &leido, nullptr);
        const Firma firma = firmaDe(cab, leido);

        // Un ejecutable disfrazado de datos. Esto es peligro, no aviso: no hay
        // ninguna razon legitima para que una textura empiece por "MZ".
        if (pareceDatos(ext) &&
            (firma == Firma::PE || firma == Firma::ELF || firma == Firma::MachO)) {
            Hallazgo hh;
            hh.nivel   = Nivel::Peligro;
            hh.motivo  = std::string("Dice ser ") + aUtf8(ext) +
                         " pero en realidad es " + aUtf8(nombreFirma(firma)) +
                         ". Es la forma mas comun de colar un programa "
                         "dentro de un juego.";
            hh.archivo = rel;
            inf.hallazgos.push_back(std::move(hh));
        }

        // --- 2) scripts y ejecutables donde no toca ---
        bool esperable = false;
        const bool ejecutable = esScriptOEjecutable(ext, esperable);
        if (ejecutable && !esperable) {
            Hallazgo hh;
            hh.nivel   = Nivel::Aviso;
            hh.motivo  = "Un juego no necesita este tipo de archivo (" +
                         aUtf8(ext) + "): puede ejecutar ordenes en tu equipo.";
            hh.archivo = rel;
            inf.hallazgos.push_back(std::move(hh));
            ++ejecutablesInesperados;
        }

        // --- 3) el motor de firmas, sobre el contenido ---
        //
        // Se le pasan los archivos que PUEDEN ejecutar algo y los que mienten
        // sobre lo que son. Pasarle las texturas y el audio tambien
        // multiplicaria el tiempo sin ganar casi nada: el antivirus del
        // sistema ya vigila el disco, y lo que aqui importa es lo ejecutable.
        const bool vigilar =
            ejecutable || firma == Firma::PE || firma == Firma::ELF ||
            firma == Firma::MachO || firma == Firma::Script;

        if (vigilar && inf.motorFirmas && n > 0) {
            // Tope por archivo: un .exe de 500 MB no aporta mas que sus
            // primeros megas para una deteccion por firma, y analizarlo
            // entero haria eterna la instalacion.
            constexpr u64 kTope = 64ull * 1024 * 1024;
            const size_t cuanto = static_cast<size_t>((std::min)(n, kTope));
            buf.resize(cuanto);

            LARGE_INTEGER cero{};
            SetFilePointerEx(h, cero, nullptr, FILE_BEGIN);

            size_t hecho = 0;
            bool okLectura = true;
            while (hecho < cuanto) {
                const DWORD pedir = static_cast<DWORD>(
                    (std::min)(cuanto - hecho, size_t(8u << 20)));
                DWORD got = 0;
                if (!ReadFile(h, buf.data() + hecho, pedir, &got, nullptr) ||
                    got == 0) { okLectura = false; break; }
                hecho += got;
            }

            if (okLectura) {
                std::vector<Hallazgo> hs;
                if (!revisarBloque(buf.data(), hecho, rel, hs)) {
                    for (auto& x : hs) inf.hallazgos.push_back(std::move(x));
                }
            }
        }

        CloseHandle(h);
    }

    // --- 4) el conjunto, no solo cada archivo ---
    //
    // Un solo script suelto puede ser legitimo. Diez ya describen un paquete
    // que no es un juego, sino un instalador, y eso merece decirse aparte
    // aunque ninguno de los archivos sea malware por si mismo.
    if (ejecutablesInesperados >= 5) {
        Hallazgo hh;
        hh.nivel  = Nivel::Aviso;
        hh.motivo = "El paquete trae " + std::to_string(ejecutablesInesperados) +
                    " archivos que ejecutan ordenes. Un juego normal no "
                    "necesita ninguno.";
        inf.hallazgos.push_back(std::move(hh));
    }

    if (progress) progress->store(1.0f);
    QueryPerformanceCounter(&t1);
    if (frec.QuadPart > 0)
        inf.segundos = static_cast<f64>(t1.QuadPart - t0.QuadPart) /
                       static_cast<f64>(frec.QuadPart);
    inf.hecho = true;
    return inf;
}

} // namespace guardian
} // namespace ludora
