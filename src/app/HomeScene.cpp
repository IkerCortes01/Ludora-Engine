#include "app/HomeScene.h"
#include "app/DemoScene.h"
#include "app/Font5x7.h"
#include "app/GameInstall.h"
#include "app/Session.h"
#include "app/TextRender.h"   // alto real de linea de la fuente del sistema
#include "core/Engine.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>   // lista de creadores seguidos
#include <memory>
#include <windows.h>
#include <commdlg.h>   // GetOpenFileNameW: WIN32_LEAN_AND_MEAN lo excluye
#include <shlobj.h>    // SHBrowseForFolderW: elegir una carpeta de juego

namespace ludora {

using namespace theme;

namespace {
/// Conversion entre la cadena ancha de Windows y el UTF-8 que usa la interfaz
/// para medir y dibujar texto. Las rutas de archivo llegan en wchar_t y hay
/// que mostrarlas; sin esto saldrian recortadas en el primer acento.
std::string rutaAUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring utf8ARuta(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &w[0], n);
    return w;
}

/// Busca el primer `.exe` del arbol, con la raiz primero.
///
/// Un paquete guardado no dice cual es el ejecutable -- el autor eligio una
/// carpeta, no un archivo --, asi que al instalarlo hay que deducirlo. Se
/// prefiere la raiz porque es donde suele estar el lanzador; las subcarpetas
/// (bin, build) se miran despues.
/// Elige el ejecutable PRINCIPAL de un juego recien desempaquetado.
///
/// Antes devolvia el primer .exe que encontraba recorriendo en profundidad,
/// y el orden lo decidia el sistema de archivos. En un paquete que trae el
/// proyecto entero -- con `build\CMakeFiles\CompilerIdC.exe`, pruebas,
/// instaladores -- eso acababa registrando cualquier cosa menos el juego, y
/// pulsar JUGAR no abria nada.
///
/// Ahora se PUNTUAN todos los candidatos y gana el mejor:
///   - el que se llama como la carpeta del juego es casi seguro el bueno;
///   - los de carpetas de compilacion valen, pero menos;
///   - los de CMake, pruebas, desinstaladores y herramientas se descartan;
///   - a igualdad, el mas grande y el menos profundo.
std::wstring findFirstExe(const std::wstring& dir) {
    // Nombre de la carpeta raiz: lo que suele compartir el ejecutable.
    std::wstring nombreJuego;
    {
        const size_t s = dir.find_last_of(L"\\/");
        nombreJuego = (s == std::wstring::npos) ? dir : dir.substr(s + 1);
        for (auto& c : nombreJuego) c = static_cast<wchar_t>(towlower(c));
        // Sin espacios: "Voxel World" -> "voxelworld", que es como se suele
        // llamar el binario.
        std::wstring sinEsp;
        for (const wchar_t c : nombreJuego) if (c != L' ') sinEsp.push_back(c);
        nombreJuego = sinEsp;
    }

    // Lo que NUNCA es el juego, por nombre o por carpeta.
    static const wchar_t* kDescartar[] = {
        L"compilerid", L"cmake", L"test", L"tests", L"unins", L"uninstall",
        L"setup", L"installer", L"crashpad", L"vcredist", L"redist",
        L"benchmark", L"tool", L"editor", L"launcher_old"
    };

    std::wstring mejor;
    i64 mejorPunt = -1;

    // Recorrido en ANCHURA: lo mas cercano a la raiz se ve primero, y a
    // igualdad de puntos gana lo menos profundo.
    std::vector<std::pair<std::wstring, i32>> pend{ { dir, 0 } };
    while (!pend.empty()) {
        const std::wstring d = pend.front().first;
        const i32 prof       = pend.front().second;
        pend.erase(pend.begin());
        if (prof > 6) continue;   // un arbol mas hondo no es de un juego

        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                pend.push_back({ d + L"\\" + n, prof + 1 });
                continue;
            }
            if (n.size() <= 4) continue;
            if (_wcsicmp(n.substr(n.size() - 4).c_str(), L".exe") != 0) continue;

            const std::wstring full = d + L"\\" + n;
            std::wstring baja = full;
            for (auto& c : baja) c = static_cast<wchar_t>(towlower(c));

            bool fuera = false;
            for (const wchar_t* mal : kDescartar)
                if (baja.find(mal) != std::wstring::npos) { fuera = true; break; }
            if (fuera) continue;

            // --- puntuacion ---
            i64 punt = 0;

            // El nombre del juego pesa mas que nada: es la senal mas fiable.
            std::wstring base = n.substr(0, n.size() - 4);
            for (auto& c : base) c = static_cast<wchar_t>(towlower(c));
            std::wstring baseSinEsp;
            for (const wchar_t c : base) if (c != L' ') baseSinEsp.push_back(c);
            if (!nombreJuego.empty() && baseSinEsp == nombreJuego) punt += 1000000;

            // Cuanto menos profundo, mejor: el exe de un juego suele estar
            // arriba o en su carpeta de salida, no enterrado.
            punt += static_cast<i64>(6 - prof) * 10000;

            // A igualdad, el mas grande: el juego pesa mas que una utilidad
            // suelta que haya quedado en el paquete.
            const i64 bytes = (static_cast<i64>(fd.nFileSizeHigh) << 32) |
                              fd.nFileSizeLow;
            punt += bytes / 1024;

            if (punt > mejorPunt) { mejorPunt = punt; mejor = full; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return mejor;
}

// Grosor de las barras, en pixeles logicos.
constexpr i32 kTopBarThickness  = 50;   // franja superior azul (menu)
constexpr i32 kSideBarThickness = 350;  // franjas rojas laterales (ambos lados)

// Bloques del menu dentro de la franja azul.
constexpr i32 kBlockGap    = 6;   // hueco entre bloques
constexpr i32 kBlockMargin = 6;   // margen vertical dentro de la franja

// Contenido de ejemplo del panel de biblioteca.
constexpr i32 kLibraryItems = 12;   // suficientes para que haya scroll
constexpr i32 kItemH        = 64;
constexpr i32 kItemPad      = 10;
constexpr i32 kScrollBarW   = 14;

// Tienda: un articulo destacado arriba y el resto en dos columnas.
constexpr i32 kStoreHeroW   = 900;  // ARTICULO 1, siempre el primero
constexpr i32 kStoreHeroH   = 200;
constexpr i32 kStoreCardH   = 160;  // tarjetas de las columnas
constexpr i32 kStoreCards   = 24;   // total, incluido el destacado
constexpr i32 kStoreGap     = 28;   // hueco entre tarjetas
constexpr i32 kStoreTopPad  = 28;

// Seccion Chat: panel con scroll a la izquierda, rectangulo vino a la derecha.
constexpr i32 kChatWineW = 870;   // ancho del rectangulo vino
constexpr i32 kChatMsgs  = 40;    // lineas de ejemplo, suficientes para scroll
constexpr i32 kChatLineH = 22;    // alto de cada linea de mensaje
constexpr i32 kChatPad   = 10;

// Seccion Cuenta: fichas largas y poco altas.
constexpr i32 kAccRowW     = 820;   // largas
constexpr i32 kAccRowH     = 72;    // poco altas
constexpr i32 kAccPad      = 16;
constexpr i32 kAccPhotoH   = 128;   // ficha del usuario, con foto al lado
constexpr i32 kAccPhotoSide= 96;
constexpr i32 kAccBtnW     = 90;
constexpr i32 kAccBtnH     = 26;
constexpr i32 kAccDescBtn  = 96;    // boton cuadrado

const wchar_t* kProfileFile = L"perfil.dat";
// Biblioteca de juegos instalados.
const wchar_t* kLibraryFile = L"biblioteca.dat";

// Aqui vivian kVoxelExe y kVoxelDest, las rutas fijas de Voxel World. Se
// retiraron con el juego: ataban la plataforma a un disco concreto.

/// Carpeta temporal donde se descifra un juego para ejecutarlo.
///
/// Es COMUN a todos: antes terminaba en "\\VoxelWorld" y cualquier juego que
/// se lanzara se descifraba encima del anterior. library::launchEntry recibe
/// esta carpeta y cuelga de ella lo que necesite.
const wchar_t* kGameTempDir = L"D:\\Respaldo\\Ludora\\Juegos descargados\\_run";
/// Galeria de juegos instalados: cada uno en su propia subcarpeta.
const wchar_t* kGamesDir    = L"D:\\Respaldo\\Ludora\\Juegos descargados";
const wchar_t* kPhotoFile   = L"foto-perfil.bmp";

// Avatar circular de la esquina inferior derecha.
constexpr i32 kAvatarRadius = 42;
constexpr i32 kAvatarMargin = 22;

// --- Modo Desarrollador: columna de botones a la izquierda ---
constexpr i32 kDevBtnW      = 250;   // ancho de cada boton de la columna
constexpr i32 kDevBtnH      = 62;
constexpr i32 kDevBtnGap    = 22;    // hueco vertical entre botones
constexpr i32 kDevColMargin = 28;    // margen desde el borde izquierdo
constexpr i32 kDevPanelPad  = 30;    // aire entre la columna y la mini-seccion

// Deslizador de precio de la mini-seccion Precios.
constexpr i32 kPriceTrackH = 10;    // grosor de la barrita
constexpr i32 kPriceKnobW  = 20;    // selector rectangular
constexpr i32 kPriceKnobH  = 30;

/// Catalogo de juegos publicados por el desarrollador.
const wchar_t* kDevCatalogFile = L"catalogo-dev.dat";
/// Historial de la biblioteca: precios y versiones de cada juego instalado.
const wchar_t* kHistoryFile = L"biblioteca-hist.dat";
/// Resenas de los jugadores sobre cada juego.
const wchar_t* kReviewsFile = L"resenas.dat";
/// Creadores a los que se sigue, uno por linea.
const wchar_t* kFollowFile  = L"siguiendo.dat";
/// Juegos desinstalados: bloqueados, pero con sus archivos intactos.
const wchar_t* kUninstFile  = L"desinstalados.dat";
/// Comunicados que los creadores publican para sus jugadores.
const wchar_t* kNoticesFile = L"comunicados.dat";

// Rejilla de ajustes de publicacion dentro de la mini-seccion PRECIOS:
// dos columnas de tres botones.
constexpr i32 kPriceOptCols = 2;
constexpr i32 kPriceOptRows = 3;
constexpr i32 kPriceOptGap  = 10;   // hueco entre botones
constexpr i32 kPriceOptH    = 44;   // alto de cada boton

// ---------------------------------------------------------------------------
// Formulario de SUBIR JUEGO: cinco campos por lado y la zona de subida en medio
// ---------------------------------------------------------------------------
// Los diez botones van en dos columnas (cinco a la izquierda, cinco a la
// derecha) con el rectangulo vertical de subida entre ambas. El campo 1 es la
// excepcion: se coloca centrado arriba, a lo ancho de la seccion, porque es el
// titulo del juego y encabeza el formulario.
constexpr i32 kUpSlots     = 10;   // total de botones del formulario
constexpr i32 kUpPerSide   = 5;    // cinco por columna
constexpr i32 kUpGap       = 8;    // hueco vertical entre botones
constexpr i32 kUpMargin    = 12;   // margen contra el borde de la mini-seccion
constexpr i32 kUpNameH     = 34;   // alto del campo 1 (nombre, arriba)
constexpr i32 kUpDropW     = 74;   // ancho del rectangulo vertical de subida
constexpr i32 kUpPlusR     = 17;   // radio del simbolo + giratorio
constexpr i32 kUpGenreRowH = 20;   // alto de cada mini-boton de genero
constexpr i32 kUpTrackH    = 9;    // grosor de los carriles (fps y edad)
constexpr i32 kUpKnobW     = 18;   // selector de los carriles
constexpr i32 kUpKnobH     = 26;

// Limites de los dos deslizadores. Son de fabrica: el valor que el juego
// tomara al arrancar si el jugador no lo cambia.
constexpr i32 kUpFpsMin = 24;      // por debajo el movimiento se ve a golpes
constexpr i32 kUpFpsMax = 2000;    // techo para monitores de alta frecuencia
constexpr i32 kUpAgeMin = 4;
constexpr i32 kUpAgeMax = 90;

/// Limite de la descripcion del juego (campo 2), en caracteres.
constexpr size_t kUpDescMax = 1000;

/// Generos de videojuego. Se ofrecen todos los reconocidos del medio para que
/// elegir sea marcar de una lista y no escribir a mano: asi dos juegos del
/// mismo genero comparten exactamente la misma etiqueta y la tienda puede
/// agruparlos. Se guardan como etiquetas con '#', que es el formato que el
/// campo 3 escribe.
const char* kUpGenres[] = {
    "ACCIÓN", "AVENTURA", "ARCADE", "BEATEMUP", "BATTLEROYALE", "CARTAS",
    "CARRERAS", "CASUAL", "CIUDADES", "CONSTRUCCIÓN", "COOPERATIVO",
    "DEPORTES", "DISPAROS", "EDUCATIVO", "ESTRATEGIA", "EXPLORACIÓN",
    "FPS", "GESTIÓN", "HACKANDSLASH", "IDLE", "INDIE", "JRPG", "LUCHA",
    "METROIDVANIA", "MMO", "MOBA", "MUNDOABIERTO", "MUSICAL", "NARRATIVO",
    "NOVELAVISUAL", "PELEAS", "PLATAFORMAS", "PUZZLE", "REFLEJOS", "RITMO",
    "ROGUELIKE", "ROGUELITE", "RPG", "RTS", "SANDBOX", "SIGILO",
    "SIMULACIÓN", "SOULSLIKE", "SUPERVIVENCIA", "TÁCTICO", "TERROR",
    "TOWERDEFENSE", "TURNOS", "VOXEL", "WARGAME",
};
constexpr i32 kUpGenreCount = static_cast<i32>(sizeof(kUpGenres) / sizeof(kUpGenres[0]));

/// Modos de multijugador. Cubren las formas en que un juego puede jugarse
/// con mas de una persona: por red, en el mismo equipo, por turnos o sin
/// conexion. Se marcan igual que los generos, pero con '@' en vez de '#'
/// para que las dos listas no se confundan de un vistazo.
const char* kUpMultiModes[] = {
    "LOCAL", "PANTALLADIVIDIDA", "COOPLOCAL", "COOPONLINE", "LAN",
    "ONLINE", "PORTURNOS", "HOTSEAT", "PVP", "PVE", "JVJ", "MMO",
    "MMORPG", "BATTLEROYALE", "ARENA", "EQUIPOS", "CLANES", "GREMIOS",
    "MUNDOPERSISTENTE", "SERVIDORDEDICADO", "SERVIDORLOCAL",
    "PEERTOPEER", "ANFITRION", "DEDICADOOFICIAL", "MULTIPLATAFORMA",
    "CROSSPLAY", "CROSSPROGRESION", "ASIMETRICO", "COOPERATIVO4",
    "COOPERATIVO8", "MASIVO", "ESPECTADOR", "MODOFIESTA", "MATCHMAKING",
    "SALAPRIVADA", "INVITACION", "SOLO", "DUOS", "ESCUADRAS",
    "TORNEO", "CLASIFICATORIO", "CASUAL", "SANDBOXCOMPARTIDO",
    "CONSTRUCCIONCONJUNTA", "OFFLINE", "SINCONEXION", "BOTS",
    "IACOMPANERA", "REPLAY", "ASINCRONO",
};
constexpr i32 kUpMultiCount = static_cast<i32>(sizeof(kUpMultiModes) / sizeof(kUpMultiModes[0]));

/// Aviso del campo 7 (permiso de pirateria). Se guarda partido en lineas
/// porque la fuente del motor no sabe ajustar parrafos: cada linea se mide y
/// se dibuja por separado.
const char* const kPirateNotice[] = {
    "Este es un aviso de que al acceder a que tu juego sea",
    "pirateado por mas personas sin control es responsabilidad",
    "tuya y de nosotros. Si aceptas, ten en cuenta que si tu",
    "borras o bajas de las tiendas el producto y ya no este en",
    "venta, los usuarios que compraron o descargaron tu",
    "producto se les liberara todo el codigo al completo.",
    "",
    "Te recordamos que estara moderada la pirateria: si se",
    "vende algo ilegal, hay abusos, ventajas ilegales, etc.,",
    "nosotros nos encargaremos de avisarte y bajar ese codigo",
    "pirateado.",
    "",
    "Ademas, si el producto era un juego multijugador, sera",
    "obligatorio que se crearan servidores locales y offline",
    "para que no se haga fraude por el creador.",
};
constexpr i32 kPirateNoticeLines =
    static_cast<i32>(sizeof(kPirateNotice) / sizeof(kPirateNotice[0]));

/// Aviso del campo 9 (permitir personalizacion).
const char* const kCustomNotice[] = {
    "Esto hace que tu le des la posibilidad gratuita a",
    "cualquier usuario de descargar todas las texturas,",
    "imagenes, sprites, animaciones y todo el apartado de",
    "diseno, y personalice con total libertad su biblioteca",
    "en la seccion de tu producto.",
};
constexpr i32 kCustomNoticeLines =
    static_cast<i32>(sizeof(kCustomNotice) / sizeof(kCustomNotice[0]));

/// Alto de cada linea del aviso y de los botones de respuesta.
constexpr i32 kUpNoticeLineH = 12;
constexpr i32 kUpConsentBtnH = 34;

/// Duracion de la palomita de precio aceptado, en segundos. Lo bastante para
/// que se vea el trazo dibujarse, y lo bastante corta para no estorbar.
constexpr f64 kPriceOkAnimDur = 1.1;

/// Ultimo tramo de una ruta (archivo o carpeta), pasado a texto dibujable.
/// La fuente del motor no cubre todo Unicode, asi que lo que no entra se
/// sustituye por '?' en vez de dejar un hueco mudo.
std::string nombreVisible(const std::wstring& ruta) {
    // Una carpeta puede venir con barra final: se ignora para no devolver "".
    size_t fin = ruta.size();
    while (fin > 0 && (ruta[fin - 1] == L'\\' || ruta[fin - 1] == L'/')) --fin;

    const size_t s = ruta.find_last_of(L"\\/", fin ? fin - 1 : 0);
    const std::wstring solo = (s == std::wstring::npos)
                                ? ruta.substr(0, fin)
                                : ruta.substr(s + 1, fin - s - 1);

    std::string out;
    out.reserve(solo.size());
    for (const wchar_t c : solo)
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

/// Un ajuste de publicacion: su titulo y las opciones entre las que elegir.
/// Las listas son fijas porque describen decisiones de negocio (que monedas
/// se aceptan, a que idiomas se traduce), no datos que el usuario invente.
struct PriceOptionDef {
    const char*        title;
    const char* const* values;
    i32                count;
};

const char* const kCurrencyValues[] = { "USD", "MXN", "EUR", "GBP", "BRL", "ARS" };
const char* const kCountryValues[]  = { "MÉXICO", "ESPAÑA", "ARGENTINA",
                                        "COLOMBIA", "CHILE", "EE.UU." };
// Los idiomas son acumulativos de menos a mas: cada opcion incluye la anterior.
const char* const kLanguageValues[] = { "ESPANOL", "ES + INGLES", "ES+EN+PORT",
                                        "5 IDIOMAS", "10 IDIOMAS", "TODOS" };
const char* const kRegionValues[]   = { "LATAM", "NORTEAMERICA", "EUROPA",
                                        "ASIA", "GLOBAL", "SOLO MEXICO" };
const char* const kTaxValues[]      = { "IVA INCLUIDO", "IVA APARTE", "SIN IMPUESTO" };
const char* const kRoundingValues[] = { "EXACTO", "A .99", "ENTERO", "A LA DECENA" };

const PriceOptionDef kPriceOptions[HomeScene::kPriceOptCount] = {
    { "MONEDA",   kCurrencyValues, 6 },
    { "PAIS 1o",  kCountryValues,  6 },
    { "IDIOMAS",  kLanguageValues, 6 },
    { "REGION",   kRegionValues,   6 },
    { "IMPUESTOS",kTaxValues,      3 },
    { "REDONDEO", kRoundingValues, 4 },
};

/// Nombre de cada seccion. La tercera es la que muestra los dos paneles.
const char* kSectionNames[HomeScene::kBlockCount] = {
    "TIENDA", "CHAT", "BIBLIOTECA", "COMUNIDADES", "ESTADISTICAS & HORAS", "CUENTA"
};

/// Ancho minimo que debe conservar el detalle del juego en el centro de la
/// Biblioteca. Por debajo la portada y los botones no caben y el panel deja de
/// ser util.
constexpr i32 kDetailMinW = 300;
/// Proporcion maxima del ancho que puede llevarse CADA franja. Con el ancho
/// de diseno fijo (350) una ventana de 960 dejaba al detalle justo en su
/// minimo; atando las franjas a un porcentaje, el centro crece con la ventana
/// en vez de quedarse clavado.
constexpr f32 kSideMaxFraction = 0.27f;
/// Ancho por debajo del cual una franja lateral ya no puede mostrar nada
/// legible: mejor ocultarla entera que dejar una tira de texto cortado.
constexpr i32 kSideMinW = 150;

/// Ancho de CADA franja lateral de la Biblioteca para un framebuffer dado.
///
/// Las franjas ceden espacio antes que el centro: primero se reparte lo que
/// sobra tras reservar `kDetailMinW` para el detalle, y si ni asi caben con un
/// ancho legible se devuelven 0 -- la Biblioteca pasa a mostrar solo el detalle,
/// a pantalla completa, en vez de tres columnas ilegibles.
///
/// Existe como funcion unica porque la usan el layout (update) y el dibujado
/// (render): calcularlo por separado en cada sitio los desincronizaria y los
/// clics dejarian de caer donde se ve el panel.
i32 librarySideWidth(i32 fbWidth) {
    // Lo que puede ocupar cada lado sin invadir el minimo del centro.
    const i32 disponible = (fbWidth - kDetailMinW) / 2;
    if (disponible < kSideMinW) return 0;          // no caben: fuera las dos

    // Tres topes, y manda el menor:
    //   - el ancho de diseno de la franja,
    //   - una fraccion del ancho total, para que el centro crezca con la
    //     ventana en vez de quedarse siempre en su minimo,
    //   - lo que quede tras reservar el minimo del detalle.
    const i32 porFraccion = static_cast<i32>(fbWidth * kSideMaxFraction);
    return std::min({ kSideBarThickness, porFraccion, disponible });
}

/// Tamano de un archivo en bytes, o 0 si no existe. Se usa para anotar cuanto
/// ocupa la copia archivada de cada version.
u64 fileSizeOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return (static_cast<u64>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
}

// Aqui estaba `voxelInstalado()`, que comprobaba si Voxel World seguia en la
// biblioteca. Se retiro con el juego. La limpieza de registros rotos que
// hacia de paso no se pierde: `cargarBiblioteca()` ya la aplica a TODOS los
// juegos, no solo a uno.

/// Carga la biblioteca mostrando SOLO juegos realmente instalados (su .exe
/// existe). Las entradas rotas -- registro que apunta a un .exe ausente -- se
/// eliminan del disco, para no mostrar juegos fantasma que no se pueden jugar.
std::vector<library::Entry> cargarBiblioteca() {
    std::vector<library::Entry> todas = library::load(kLibraryFile);
    std::vector<library::Entry> instaladas;
    for (const auto& e : todas) {
        if (library::entryInstalled(e)) instaladas.push_back(e);
        else library::remove(kLibraryFile, e.name);   // purgar registro roto
    }
    return instaladas;
}
} // namespace

HomeScene::HomeScene(std::string sessionEmail) : m_email(std::move(sessionEmail)) {}

void HomeScene::onEnter(Engine& engine) {
    m_titleBar.attach(engine);

    // Perfil guardado: nombre, descripcion y foto.
    m_profileLoaded = Profile::load(kProfileFile, m_profile);

    TextField::Style st{};
    st.bg            = kFieldBg;
    st.bgFocused     = kFieldBgFocus;
    st.border        = kFieldBorder;
    st.borderFocused = kAccent;
    st.text          = kText;
    st.placeholder   = kDim;
    st.caret         = kAccent;
    st.selection     = kSelection;
    st.textScale     = 2;
    // La descripcion publica tambien es una caja alta: el texto arranca en el
    // borde de arriba. El estilo base se restaura despues para los campos de
    // una linea que lo reutilizan.
    st.alignTop      = true;
    st.multiline     = true;
    m_descField.setStyle(st);
    st.alignTop      = false;
    st.multiline     = false;
    m_descField.setPlaceholder("escribe tu descripcion publica");
    m_descField.setMaxLength(Profile::kMaxDescription);

    // --- campos del formulario de SUBIR JUEGO ---
    // Cada uno toma el color de su campo en el cursor y el marco enfocado, de
    // modo que el cuadro de texto se lee como parte del boton que lo abrio.
    TextField::Style up = st;
    up.borderFocused = kUpIndigo;
    up.caret         = kUpIndigo;
    m_upNameField.setStyle(up);
    m_upNameField.setPlaceholder("nombre del juego");
    m_upNameField.setMaxLength(120);

    up.borderFocused = kUpPurple;
    up.caret         = kUpPurple;
    // La descripcion es una caja alta: el texto empieza arriba, no flotando
    // en el centro. El resto de campos del formulario son de una linea y
    // siguen centrados.
    up.alignTop      = true;
    up.multiline     = true;   // 1000 caracteres no caben en una fila
    m_upDescField.setStyle(up);
    m_upDescField.setPlaceholder("de qué va el juego");
    m_upDescField.setMaxLength(kUpDescMax);

    up.borderFocused = kUpPink;
    up.caret         = kUpPink;
    up.textScale     = 1;      // las etiquetas son varias y encadenadas
    up.alignTop      = false;  // vuelve a una linea: centrado
    up.multiline     = false;
    m_upGenreField.setStyle(up);
    m_upGenreField.setPlaceholder("#GÉNERO");
    m_upGenreField.setMaxLength(600);
    m_upGenreField.setText("#");   // el formato de etiqueta, ya puesto

    // Campo 6: mismo mecanismo que el de generos, pero con '@' y en rojo.
    up.borderFocused = kUpMultiRed;
    up.caret         = kUpMultiRed;
    m_upMultiField.setStyle(up);
    m_upMultiField.setPlaceholder("@MODO");
    m_upMultiField.setMaxLength(600);
    m_upMultiField.setText("@");

    // --- campos de ACTUALIZAR JUEGO ---
    up.borderFocused = kDevBrown;
    up.caret         = kDevBrown;
    up.textScale     = 2;
    m_arcCodeField.setStyle(up);
    m_arcCodeField.setPlaceholder("#codigo del juego");
    m_arcCodeField.setMaxLength(120);

    // --- change log y comunicado: textos largos ---
    // Se pidio "mucho o poco texto sin limite". El maximo que admite
    // TextField es un entero, asi que se pone uno tan alto que ningun texto
    // escrito a mano lo alcanza, en vez de dejar un tope bajo que recorte.
    up.borderFocused = kDevGold;
    up.caret         = kDevGold;
    up.textScale     = 1;
    up.alignTop      = true;
    up.multiline     = true;
    m_updLogField.setStyle(up);
    m_updLogField.setPlaceholder("que cambia en esta version");
    m_updLogField.setMaxLength(1000000);
    m_updNoticeBody.setStyle(up);
    m_updNoticeBody.setPlaceholder("el mensaje para tus jugadores");
    m_updNoticeBody.setMaxLength(1000000);
    up.alignTop  = false;
    up.multiline = false;

    up.borderFocused = kAccent;
    up.caret         = kAccent;
    up.textScale     = 2;
    m_updNoticeTitle.setStyle(up);
    m_updNoticeTitle.setPlaceholder("titulo del comunicado");
    m_updNoticeTitle.setMaxLength(200);

    // --- campos de CREAR COMUNIDAD ---
    up.borderFocused = kUpIndigo;
    up.caret         = kUpIndigo;
    up.textScale     = 2;
    m_comNameField.setStyle(up);
    m_comNameField.setPlaceholder("nombre de la comunidad");
    m_comNameField.setMaxLength(120);

    // Descripcion y reglas son cajas altas de 5000 caracteres: el texto
    // empieza arriba y el Enter hace parrafo.
    up.textScale = 1;
    up.alignTop  = true;
    up.multiline = true;
    up.borderFocused = kUpPurple;
    up.caret         = kUpPurple;
    m_comDescField.setStyle(up);
    m_comDescField.setPlaceholder("de que va tu comunidad");
    m_comDescField.setMaxLength(5000);

    up.borderFocused = kUpPink;
    up.caret         = kUpPink;
    m_comRulesField.setStyle(up);
    m_comRulesField.setPlaceholder("reglas que deben seguir los que se unan");
    m_comRulesField.setMaxLength(5000);
    up.alignTop  = false;
    up.multiline = false;

    // --- campo de PRECIO A ESCRITO ---
    up.borderFocused = kSteamBlue;
    up.caret         = kSteamBlue;
    up.textScale     = 2;
    m_priceTypeField.setStyle(up);
    m_priceTypeField.setPlaceholder("19.99");
    m_priceTypeField.setMaxLength(12);   // "400.00" y de sobra

    // --- campo de ELIMINAR JUEGO ---
    up.borderFocused = kConsentRed;
    up.caret         = kConsentRed;
    up.textScale     = 2;
    m_delCodeField.setStyle(up);
    m_delCodeField.setPlaceholder("#codigo del juego a retirar");
    m_delCodeField.setMaxLength(120);

    // --- campo de GUARDAR ARCHIVOS ---
    up.borderFocused = kSaveGreen;
    up.caret         = kSaveGreen;
    // El unico campo de GUARDAR ARCHIVOS es el del codigo. 18 caracteres de
    // margen: '#' y 15, mas algun espacio si se pega de otro sitio.
    up.borderFocused = kSaveGreen;
    up.caret         = kSaveGreen;
    m_arcCodeField.setStyle(up);
    m_arcCodeField.setPlaceholder("#CODIGO DEL JUEGO (obligatorio)");
    m_arcCodeField.setMaxLength(18);

    // --- campo de EDAD escrita a mano ---
    // Tres digitos bastan: el rango llega a 90 y el validador acota.
    up.borderFocused = kUpMint;
    up.caret         = kUpMint;
    m_ageManualField.setStyle(up);
    m_ageManualField.setPlaceholder("edad");
    m_ageManualField.setMaxLength(3);

    m_ludoPanel.setBarWidth(kScrollBarW);
    m_upGenrePanel.setBarWidth(kScrollBarW);
    m_upMultiPanel.setBarWidth(kScrollBarW);
    m_upNoticePanel.setBarWidth(kScrollBarW);
    m_delNoticePanel.setBarWidth(kScrollBarW);

    // --- campo 10: los tres bloques del EULA ---
    // Van en la paleta azulada y apagada de su seccion, no en la roja del
    // resto del motor: es una pantalla de lectura larga y el contraste fuerte
    // cansa. El limite es alto a proposito -- un reglamento no deberia
    // toparse con el.
    TextField::Style eu{};
    eu.bg            = kEulaPanel;
    eu.bgFocused     = kEulaPanelFoc;
    eu.border        = kEulaEdge;
    eu.borderFocused = kEulaEdgeFoc;
    eu.text          = kEulaText;
    eu.placeholder   = kEulaEdgeFoc;
    eu.caret         = kEulaTitle;
    eu.selection     = kEulaEdge;
    eu.textScale     = 1;
    eu.alignTop      = true;   // el reglamento empieza arriba del bloque
    eu.multiline     = true;   // y se reparte en filas, no en una sola

    const char* pistas[kEulaBlocks] = {
        "reglas para quien juega solo",
        "reglas de servidores y partidas",
        "reglas de mods y comunidad",
    };
    for (i32 i = 0; i < kEulaBlocks; ++i) {
        m_eulaField[i].setStyle(eu);
        m_eulaField[i].setPlaceholder(pistas[i]);
        m_eulaField[i].setMaxLength(100000);
        m_eulaPanel[i].setBarWidth(kScrollBarW);
    }

    // Barra pegada al borde izquierdo de la ventana, que es donde esta la
    // "pared" en este panel.
    m_leftPanel.setBarOnLeft(true);
    m_leftPanel.setBarWidth(kScrollBarW);
}
void HomeScene::onExit(Engine& engine)  { m_titleBar.detach(engine); }

Recti HomeScene::blockRect(Engine& engine, i32 fbWidth, i32 i) const {
    const i32 barY = m_titleBar.heightLogical(engine);

    // Los seis bloques se reparten el ancho a partes iguales. El reparto se
    // hace con los bordes calculados, no con un ancho fijo por bloque: asi el
    // ultimo llega siempre al borde derecho y no queda un sobrante visible.
    const i32 x0 = (fbWidth * i)       / kBlockCount;
    const i32 x1 = (fbWidth * (i + 1)) / kBlockCount;

    const i32 x = x0 + kBlockGap / 2;
    const i32 w = (x1 - x0) - kBlockGap;

    return Recti{ x, barY + kBlockMargin, std::max(1, w),
                  kTopBarThickness - kBlockMargin * 2 };
}

i32 HomeScene::blockAt(Engine& engine, i32 fbWidth, i32 lx, i32 ly) const {
    for (i32 i = 0; i < kBlockCount; ++i) {
        const Recti r = blockRect(engine, fbWidth, i);
        if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
            return i;
    }
    return -1;
}

void HomeScene::update(Engine& engine, f64 dt) {
    Input& in = engine.input();

    // Cerrar sesion solicitado: volver a la pantalla de acceso. Diferido con
    // queueScene porque estamos dentro del update() de esta escena.
    if (m_wantLogout) {
        m_wantLogout = false;
        engine.queueScene(std::make_unique<DemoScene>());
        return;
    }

    // Al terminar de instalar, registrar el juego en la biblioteca. Se hace
    // aqui, fuera de la seccion Tienda, para que se complete aunque el usuario
    // navegue a otra pestana mientras se instala.
    if (m_install.done() && m_installStarted) {
        // Si la instalacion fallo, se dice y no se registra nada: una entrada
        // que apunta a archivos que no existen es peor que ninguna.
        if (m_install.failed()) {
            m_prodBuyMsg     = m_install.error().empty()
                             ? std::string("No se pudo instalar el juego.")
                             : m_install.error();
            m_prodBuyMsgErr  = true;
            m_prodBuyMsgTime = 3.0;
            m_installStarted = false;
            m_installingName.clear();
            m_install.join();
            return;
        }

        // El nombre lo fija quien lanzo la instalacion. Antes caia a "Voxel
        // World" cuando venia vacio, por el boton INSTALAR de la tienda; ese
        // boton y ese juego ya no existen, asi que un nombre vacio seria un
        // error de programacion y no un caso normal.
        library::Entry e;
        e.name    = m_installingName.empty() ? std::string("Juego")
                                             : m_installingName;
        e.exe     = m_install.installedExe();   // exe EN CLARO en su carpeta
        e.vault   = m_install.vaultPath();       // paquete cifrado de datos

        // El nombre del exe se saca de la ruta ya instalada, no de una
        // constante: cada juego tiene el suyo.
        const std::wstring inst = m_install.installedExe();
        const size_t sb = inst.find_last_of(L"\\/");
        e.exeName = (sb == std::wstring::npos) ? inst : inst.substr(sb + 1);

        library::add(kLibraryFile, e);

        // Instalar deshace un "desinstalado" anterior: el juego vuelve a
        // estar accesible.
        setUninstalled(e.name, false);

        // Historial: si el juego ya estaba en la biblioteca, esta instalacion
        // es una ACTUALIZACION -- se archiva la version que habia y la nueva
        // pasa a ser la jugable. Si es la primera vez, se registra v1. Ambas
        // funciones son idempotentes, asi que reinstalar no falsea el historial.
        {
            const u64 bytes = fileSizeOf(m_install.vaultPath());
            bool tenia = false;
            const history::GameHistory h =
                history::forGame(kHistoryFile, e.name, &tenia);

            std::string herr;
            if (tenia && !h.versions.empty())
                history::archiveAndAddVersion(kHistoryFile, e.name,
                                              m_install.vaultPath(), bytes, herr);
            else
                history::noteInitialVersion(kHistoryFile, e.name,
                                            m_install.vaultPath(), bytes, herr);

            // Precio de partida: el que tenia en la tienda al descargarlo. Si
            // la instalacion no vino de ahi, se busca en el catalogo propio.
            i32 precio = m_installingCents;
            if (precio == 0 && m_installingName.empty())
                for (const auto& g : devcatalog::load(kDevCatalogFile))
                    if (g.name == e.name) { precio = g.price.usdCents; break; }
            history::noteInitialPrice(kHistoryFile, e.name, precio, herr);
        }

        m_libraryLoaded  = false;   // forzar recarga de la casilla de biblioteca
        m_historyLoaded  = false;   // y del historial, que acaba de cambiar
        m_installStarted = false;

        // --- llevar al usuario a la ficha del juego recien instalado ---
        // Se recarga la biblioteca ya, para poder seleccionarlo: esperar al
        // siguiente frame dejaria la seccion abierta sin nada elegido.
        m_libraryGames  = cargarBiblioteca();
        m_libraryLoaded = true;

        for (i32 i = 0; i < static_cast<i32>(m_libraryGames.size()); ++i)
            if (m_libraryGames[static_cast<size_t>(i)].name == e.name) {
                m_librarySelected = i;
                break;
            }

        // Y se abre la Biblioteca en ese juego. Es lo que se espera tras
        // descargar: ver lo que acabas de conseguir, no volver a buscarlo.
        m_section       = kPanelsSection;
        m_historyLoaded = false;   // su linea de tiempo se carga al dibujar

        m_prodBuyMsg     = "Instalado: " + e.name;
        m_prodBuyMsgErr  = false;
        m_prodBuyMsgTime = 2.5;

        m_installingName.clear();
        m_installingCents = 0;
        m_install.join();
    }

    // Juego en ejecucion: avanzar el reloj de la animacion y comprobar si ya
    // se cerro. WAIT_OBJECT_0 en 0 ms = el proceso termino.
    if (m_runningProc) {
        m_playAnimTime += dt;
        if (WaitForSingleObject(m_runningProc, 0) == WAIT_OBJECT_0) {
            CloseHandle(m_runningProc);
            m_runningProc = nullptr;
        }
    }

    // El editor de descripcion se superpone: mientras este abierto se queda
    // con todo el teclado y el raton, salvo los botones de ventana.
    if (m_descOpen) {
        m_titleBar.update(engine);
        updateDescriptionEditor(engine, dt);
        return;
    }

    if (in.keyPressed(VK_ESCAPE)) {
        // En las subsecciones ESC vuelve atras; en el resto, cierra la app.
        if (m_section == kReviewsSection) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
        } else if (m_section == kProductSection) {
            m_section   = kStoreSection;   // vuelve a la Tienda
            m_storeOpen = -1;
        } else if (m_section == kGenreSection) {
            m_section = kStoreSection;
            m_genreOpen = m_genreOpen2 = -1;
        } else if (m_section == kModsSection) {
            m_section = kPanelsSection;   // vuelve a la Biblioteca
        } else if (m_section == kEulaSection) {
            m_section = kDevSection;   // vuelve al formulario de subida
        } else if (m_section == kDevSection) {
            // En el Modo Desarrollador ESC retrocede UN nivel cada vez, en vez
            // de salir de golpe. Se resuelve aqui, junto al resto del teclado,
            // para que funcione tambien con el cursor sobre la franja del menu
            // -- mas abajo esa franja se queda con el evento y devuelve antes
            // de llegar a la seccion.
            //
            // El orden va de DENTRO hacia FUERA, y cada nivel desplegable
            // tiene que estar en la lista. Los cuatro primeros faltaban: con
            // el cuadro de precio escrito abierto, por ejemplo, ESC cerraba
            // de golpe toda la mini-seccion PRECIOS en vez de ese cuadro.
            if (m_priceTypeOpen)             m_priceTypeOpen = false;
            else if (m_priceDefOpen)         m_priceDefOpen  = false;
            else if (m_ageMode != kAgeNone)  { m_ageMode = kAgeNone; m_ageMsg.clear(); }
            else if (m_priceOptOpen >= 0)    m_priceOptOpen = -1;
            else if (m_upOpen >= 0)          m_upOpen = kUpNone;
            // CREAR COMUNIDAD: primero la lista desplegada de un tramo de
            // castigos, luego el ajuste abierto, y solo despues la seccion.
            else if (m_comPunishOpen >= 0)   m_comPunishOpen = -1;
            else if (m_comOpen >= 0)         { m_comOpen = -1; m_comAdminsDrag = false; }
            // GUARDAR ARCHIVOS Y ACTUALIZAR: el cuadro del change log o del
            // comunicado se cierra antes que su mini-seccion.
            else if (m_updBoxOpen >= 0)      m_updBoxOpen = -1;
            else if (m_devPanel != kDevNone) { m_devPanel = kDevNone; m_devMsg.clear(); }
            else                             m_section = kAccountSection;
        } else {
            engine.requestQuit();
        }
        return;
    }

    // Si el raton esta sobre un boton de ventana, el clic es suyo.
    if (m_titleBar.update(engine)) { m_hovered = -1; return; }

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);
    const i32 fbw = (m_lastFbWidth > 0) ? m_lastFbWidth
                                        : engine.renderer().fb().width();

    // La franja del menu (los 6 bloques) siempre tiene prioridad de clic, en
    // cualquier seccion. Sin esto, un panel que empieza justo debajo del menu
    // se quedaba con clics que caian en su franja de X y bloqueaba el cambio
    // de pestana (bug al estar en Chat y pulsar Tienda). Se resuelve aqui, una
    // sola vez, antes de que ninguna seccion atienda su propio panel.
    {
        const i32 barY = m_titleBar.heightLogical(engine);
        const i32 menuBot = barY + kTopBarThickness;
        if (ly >= barY && ly < menuBot) {
            m_hovered = blockAt(engine, fbw, lx, ly);
            if (in.mousePressed(0)) m_pressed = m_hovered;
            if (in.mouseReleased(0)) {
                if (m_pressed >= 0 && m_pressed == m_hovered) {
                    m_section = m_pressed;
                    // Al cambiar de seccion, soltar estados de arrastre que
                    // pudieran quedar pegados de la seccion anterior.
                    m_chatSelecting = false;
                }
                m_pressed = -1;
            }
            return;   // el clic era del menu; ninguna seccion lo toca
        }
    }

    // Los paneles con scroll se atienden antes que el menu: si se esta
    // arrastrando una barra, el raton no debe activar bloques al pasar.
    if (m_section == kStoreSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutStore(engine, fbw, fbh);

        // El banner pasa al juego siguiente cada cinco minutos. Solo corre
        // con la Tienda a la vista: fuera de ella no hay nada que rotar.
        if (!m_storeGames.empty()) {
            m_bannerTime += dt;
            if (m_bannerTime >= storeBannerPeriod()) {
                m_bannerTime = 0.0;
                m_bannerIndex = (m_bannerIndex + 1) %
                                static_cast<i32>(m_storeGames.size());
            }
        }

        // Deslizamiento de los dos carruseles.
        updateCarousels(dt);

        // El orden por valoracion se renueva cada 24 horas: las resenas
        // nuevas cambian quien merece estar destacado.
        m_topRefresh += dt;
        if (m_topRefresh >= topRatedPeriod()) refreshTopRated();

        // Aqui vivia el boton INSTALAR de Voxel World, que copiaba una ruta
        // fija del disco. Se retiro con el juego: los juegos se instalan
        // desde su ficha en la tienda, que es el camino que vale para
        // cualquiera. El rectangulo se queda en cero y nada responde.
        m_installHover = false;

        // El panel atiende rueda y barra. OJO: su update() devuelve true con
        // solo tener el cursor DENTRO, no unicamente al arrastrar; como las
        // tarjetas viven dentro del panel, devolver aqui por ese true dejaria
        // el clic sin llegarles nunca. Se consulta, pero solo se corta cuando
        // se esta arrastrando la barra de verdad.
        m_storePanel.update(engine);
        if (m_storePanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            m_storeHover = -1;   // arrastrando la barra no se resalta nada
            return;
        }

        // --- flechas de los dos carruseles ---
        // Se atienden antes que las tarjetas: salen de las paredes y podrian
        // caer sobre una casilla de lista.
        auto en = [&](const Recti& r) {
            return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                              ly >= r.y && ly < r.y + r.h;
        };
        m_carArrowLHover = en(m_carArrowL);
        m_carArrowRHover = en(m_carArrowR);
        m_topArrowLHover = en(m_topArrowL);
        m_topArrowRHover = en(m_topArrowR);

        if (in.mouseReleased(0)) {
            if (m_carArrowLHover) { carouselStep(-1); return; }
            if (m_carArrowRHover) { carouselStep(+1); return; }
            if ((m_topArrowLHover || m_topArrowRHover) && m_topDir == 0 &&
                !m_topOrder.empty()) {
                const i32 dir = m_topArrowLHover ? -1 : +1;
                const i32 n = static_cast<i32>(m_topOrder.size());
                m_topIndex = ((m_topIndex + dir) % n + n) % n;
                m_topSlide = static_cast<f32>(dir);
                m_topDir   = dir;
                m_topOf.clear();
                return;
            }
        }

        // --- botones de genero y de combinacion ---
        m_genreHover = -1;
        m_genreHoverCombo = false;
        for (i32 i = 0; i < static_cast<i32>(m_genreBtns.size()); ++i)
            if (en(m_genreBtns[static_cast<size_t>(i)])) m_genreHover = i;
        if (m_genreHover < 0)
            for (i32 i = 0; i < static_cast<i32>(m_comboBtns.size()); ++i)
                if (en(m_comboBtns[static_cast<size_t>(i)])) {
                    m_genreHover = i;
                    m_genreHoverCombo = true;
                }

        if (in.mouseReleased(0) && m_genreHover >= 0) {
            if (m_genreHoverCombo) {
                // La combinacion empareja cada genero con el siguiente, igual
                // que al dibujarla.
                openGenreSection(m_genreHover,
                                 (m_genreHover + 1) % HomeScene::uploadGenreCount());
            } else {
                openGenreSection(m_genreHover);
            }
            return;
        }

        // --- boton SEGUIR del creador ---
        // Se mira ANTES que la tarjeta: el boton esta DENTRO de ella, y si
        // ganara la tarjeta seria imposible seguir a nadie sin que se abriera
        // su ficha encima.
        m_followHover = en(m_followRect);
        if (in.mousePressed(0) && m_followHover) { m_followDown = true; return; }
        if (in.mouseReleased(0) && m_followDown) {
            const bool pulsado = m_followHover;
            m_followDown = false;
            if (pulsado) { toggleFollowForTest(m_email); return; }
        }

        // --- la TARJETA abre la ficha publica del creador ---
        // Cualquier punto de la tarjeta que no sea el boton SEGUIR.
        m_creatorHover = en(m_creatorRect) && !m_followHover;
        if (in.mousePressed(0) && m_creatorHover) { m_creatorDown = true; return; }
        if (in.mouseReleased(0) && m_creatorDown) {
            const bool pulsado = m_creatorHover;
            m_creatorDown = false;
            if (pulsado) {
                m_creatorOpen = m_email;   // hoy la unica cuenta es la local
                m_section     = kCreatorSection;
                m_creatorPhotoTried = false;   // recarga la foto al abrir
                return;
            }
        }

        // --- tarjetas: realce y apertura de la ficha ---
        // Los rectangulos los dejo drawStore en el frame anterior, porque su
        // sitio depende del scroll y solo se sabe al dibujar.
        m_storeHover = -1;
        for (i32 i = 0; i < static_cast<i32>(m_storeCards.size()); ++i) {
            const Recti& r = m_storeCards[static_cast<size_t>(i)].rect;
            if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
                m_storeHover = i;
        }

        if (in.mousePressed(0) && m_storeHover >= 0) m_storeDown = m_storeHover;

        if (in.mouseReleased(0)) {
            if (m_storeDown >= 0 && m_storeDown == m_storeHover) {
                const i32 juego =
                    m_storeCards[static_cast<size_t>(m_storeDown)].gameIndex;
                // Solo los huecos con juego publicado abren ficha; los
                // genericos siguen sin hacer nada.
                if (juego >= 0) {
                    openProduct(juego);
                    m_storeDown = -1;
                    m_hovered = -1;
                    m_pressed = -1;
                    return;
                }
            }
            m_storeDown = -1;
        }

        if (m_storeHover >= 0) { m_hovered = -1; m_pressed = -1; return; }
    }

    if (m_section == kChatSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutChat(engine, fbw, fbh);
        // El scroll tiene prioridad; si no lo consume el raton, va a la
        // seleccion de mensajes.
        if (m_chatPanel.update(engine) || m_chatPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            m_chatSelecting = false;
            return;
        }
        updateChat(engine, dt);
        // Un clic dentro del panel de chat no debe cambiar de pestana.
        const f32 sc = std::max(0.01f, engine.window().contentScale());
        const i32 lx = static_cast<i32>(engine.input().mousePos().x / sc);
        const Recti cc = m_chatPanel.contentRect();
        if (lx >= cc.x && lx < cc.x + cc.w) { m_hovered = -1; m_pressed = -1; return; }
    }

    if (m_section == kAccountSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutAccount(engine, fbw, fbh);
        updateAccount(engine, dt);
        if (m_accountPanel.update(engine) || m_accountPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            return;
        }
        // Un clic sobre un boton de la seccion no debe cambiar de pestana.
        if (m_accountHover >= 0) { m_hovered = -1; m_pressed = -1; return; }
    }

    if (m_section == kPanelsSection) {
        // El dialogo de borrar se lleva el raton mientras este abierto: es
        // una decision que no se deshace y no debe poder esquivarse pulsando
        // detras.
        if (updateUninstallAsk(engine, dt)) { m_hovered = -1; return; }

        // La geometria se fija aqui tambien: update() corre antes que render(),
        // y en el primer frame el panel aun no tendria rectangulo.
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
        // Mismo reparto que al dibujar: si el helper cambia, ambos lo siguen.
        const i32 w   = librarySideWidth(fbw);
        m_leftPanel.setRect({0, top, w, std::max(0, fbh - top)});
        m_leftPanel.setContentHeight(kItemPad + kLibraryItems * (kItemH + kItemPad));

        // Casilla de biblioteca bajo el raton (solo las de juegos instalados).
        if (!m_libraryLoaded) { m_libraryGames = cargarBiblioteca(); m_libraryLoaded = true; }
        const Recti cc = m_leftPanel.contentRect();
        const i32 off  = m_leftPanel.offset();
        m_libraryHover = -1;
        for (i32 i = 0; i < static_cast<i32>(m_libraryGames.size()); ++i) {
            const i32 iy = cc.y + kItemPad + i * (kItemH + kItemPad) - off;
            if (ly >= iy && ly < iy + kItemH && ly >= cc.y && ly < cc.y + cc.h &&
                lx >= cc.x && lx < cc.x + cc.w)
                m_libraryHover = i;
        }

        // Clic en una casilla instalada: seleccionarla para el panel de detalle.
        if (in.mouseReleased(0) && m_libraryHover >= 0 &&
            m_libraryHover < static_cast<i32>(m_libraryGames.size())) {
            m_librarySelected = m_libraryHover;
            m_accountMsg.clear();   // limpiar un aviso de un juego anterior
            m_noticesLoaded = false;   // los comunicados son de otro juego
        }

        // --- botones del panel de detalle: Jugar y Resenas ---
        m_playHover = (lx >= m_playBtnRect.x && lx < m_playBtnRect.x + m_playBtnRect.w &&
                       ly >= m_playBtnRect.y && ly < m_playBtnRect.y + m_playBtnRect.h);
        m_reviewsHover = (lx >= m_reviewsBtnRect.x && lx < m_reviewsBtnRect.x + m_reviewsBtnRect.w &&
                          ly >= m_reviewsBtnRect.y && ly < m_reviewsBtnRect.y + m_reviewsBtnRect.h);

        // Los dos botones de abajo: desinstalar/reinstalar y mods.
        auto enRect = [&](const Recti& r) {
            return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                              ly >= r.y && ly < r.y + r.h;
        };
        m_uninstallHover = enRect(m_uninstallRect);
        m_modsHover      = enRect(m_modsRect);
        m_noticesHover   = enRect(m_noticesRect);

        if (in.mousePressed(0)) {
            if (m_playHover)      m_playDown = true;
            if (m_reviewsHover)   m_reviewsDown = true;
            if (m_uninstallHover) m_uninstallDown = true;
            if (m_modsHover)      m_modsDown = true;
            if (m_noticesHover)   m_noticesDown = true;
        }
        if (in.mouseReleased(0)) {
            const bool tieneJuego = (m_librarySelected >= 0 &&
                m_librarySelected < static_cast<i32>(m_libraryGames.size()));

            if (m_uninstallDown && m_uninstallHover && tieneJuego) {
                const std::string& n =
                    m_libraryGames[static_cast<size_t>(m_librarySelected)].name;

                if (isUninstalled(n)) {
                    // REINSTALAR: desbloquear no destruye nada, asi que no
                    // hace falta preguntar.
                    setUninstalled(n, false);
                    m_accountMsg = "Juego reinstalado. Tu progreso sigue ahi.";
                    m_accountMsgErr = false;
                } else {
                    // DESINSTALAR si borra: se pregunta antes, con el aviso
                    // completo y la espera de un segundo.
                    m_uninstAsk     = true;
                    m_uninstAskTime = 0.0;
                    m_uninstTarget  = n;
                    m_accountMsg.clear();
                }
                m_uninstallDown = m_modsDown = m_noticesDown = false;
                m_playDown = m_reviewsDown = false;
                return;
            }
            if (m_modsDown && m_modsHover && tieneJuego) {
                m_section = kModsSection;
                m_uninstallDown = m_modsDown = m_noticesDown = false;
                m_playDown = m_reviewsDown = false;
                return;
            }
            if (m_noticesDown && m_noticesHover && tieneJuego) {
                // NOTIFICACIONES: los comunicados del creador de ESTE juego.
                m_section = kNoticesSection;
                m_noticesPanel.setOffset(0);
                m_uninstallDown = m_modsDown = m_noticesDown = false;
                m_playDown = m_reviewsDown = false;
                return;
            }

            // Un juego desinstalado no se lanza: el acceso esta bloqueado.
            const bool bloqueado = tieneJuego &&
                isUninstalled(m_libraryGames[static_cast<size_t>(m_librarySelected)].name);

            if (m_playDown && m_playHover && tieneJuego && bloqueado) {
                m_accountMsg    = "El juego esta desinstalado. Pulsa REINSTALAR.";
                m_accountMsgErr = true;
                m_playDown = m_reviewsDown = false;
                m_uninstallDown = m_modsDown = m_noticesDown = false;
                return;
            }

            if (m_playDown && m_playHover && tieneJuego && !m_runningProc) {
                // Descifra el juego a una carpeta temporal y lo ejecuta. Se
                // guarda el handle del proceso para animar el boton mientras
                // el juego siga abierto.
                std::string err;
                void* proc = nullptr;
                if (library::launchEntry(m_libraryGames[m_librarySelected],
                                         kGameTempDir, err, &proc)) {
                    m_runningProc  = proc;
                    m_playAnimTime = 0.0;
                } else {
                    m_accountMsg    = err;   // se muestra el motivo del fallo
                    m_accountMsgErr = true;
                }
            }
            if (m_reviewsDown && m_reviewsHover && tieneJuego) {
                m_reviewsFrom = kPanelsSection;   // para volver aqui
                m_section = kReviewsSection;
            }
            m_playDown = m_reviewsDown = false;
            m_uninstallDown = m_modsDown = m_noticesDown = false;
        }

        if (m_leftPanel.update(engine) || m_leftPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            return;
        }
        if (m_libraryHover >= 0 || m_playHover || m_reviewsHover ||
            m_noticesHover) {
            m_hovered = -1; m_pressed = -1; return;
        }
    }

    // --- seccion Resenas: scroll y boton volver ---
    if (m_section == kReviewsSection) {
        const i32 top2 = m_titleBar.heightLogical(engine) + kTopBarThickness;
        // Volver con ESC o con el boton "< VOLVER" (esquina superior derecha).
        if (in.keyPressed(VK_ESCAPE)) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
            return;
        }
        const bool enVolver = (ly >= top2 && ly < top2 + 30 && lx > fbw - 120);
        if (in.mouseReleased(0) && enVolver) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
            return;
        }
        m_reviewsPanel.update(engine);
        return;
    }

    if (m_section == kDevSection) {
        const i32 top2 = m_titleBar.heightLogical(engine) + kTopBarThickness;

        // ESC ya se atendio arriba, junto al resto del teclado.
        // El boton "< VOLVER" sale del Modo Desarrollador de una vez.
        const bool enVolver = (ly >= top2 && ly < top2 + 30 && lx > fbw - 140);
        if (in.mouseReleased(0) && enVolver) {
            m_section = kAccountSection;   // se vuelve a Cuenta
            return;
        }
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutDev(engine, fbw, fbh);
        updateDev(engine, dt);
        return;
    }

    if (m_section == kEulaSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutEula(engine, fbw, fbh);
        updateEula(engine, dt);
        return;
    }

    if (m_section == kProductSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutProduct(engine, fbw, fbh);
        updateProduct(engine, dt);
        return;
    }

    if (m_section == kModsSection) {
        updateModsSection(engine, dt);
        return;
    }

    if (m_section == kNoticesSection) {
        updateNoticesSection(engine, dt);
        return;
    }

    if (m_section == kCreatorSection) {
        updateCreatorSection(engine, dt);
        return;
    }

    if (m_section == kCommunitySection) {
        updateCommunitySection(engine, dt);
        return;
    }

    if (m_section == kGenreSection) {
        updateGenreSection(engine, dt);

        // Las filas de la lista abren la ficha del juego, igual que las
        // tarjetas de la Tienda.
        m_storeHover = -1;
        for (i32 i = 0; i < static_cast<i32>(m_storeCards.size()); ++i) {
            const Recti& r = m_storeCards[static_cast<size_t>(i)].rect;
            if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
                m_storeHover = i;
        }
        if (in.mouseReleased(0) && m_storeHover >= 0) {
            const i32 jg = m_storeCards[static_cast<size_t>(m_storeHover)].gameIndex;
            if (jg >= 0) openProduct(jg);
        }
        return;
    }

    m_hovered = blockAt(engine, fbw, lx, ly);

    if (in.mousePressed(0)) m_pressed = m_hovered;

    if (in.mouseReleased(0)) {
        // Solo cambia de seccion si se suelta sobre el mismo bloque donde se
        // pulso: arrastrar fuera cancela, como en cualquier boton.
        if (m_pressed >= 0 && m_pressed == m_hovered)
            m_section = m_pressed;
        m_pressed = -1;
    }
}

void HomeScene::drawMenu(Engine& engine, Framebuffer& fb) {
    const i32 barY = m_titleBar.heightLogical(engine);

    // Franja azul de fondo: los bloques se recortan sobre ella.
    fb.fillRect({0, barY, fb.width(), kTopBarThickness}, kDarkBlue);

    for (i32 i = 0; i < kBlockCount; ++i) {
        const Recti r = blockRect(engine, fb.width(), i);

        const bool activo  = (i == m_section);
        const bool pulsado = (m_pressed == i && m_hovered == i);
        const bool sobre   = (m_hovered == i && m_pressed < 0) || pulsado;

        // El bloque activo se marca en rojo para que se vea donde estas.
        Color fondo = kBlueDivider;
        if (activo)       fondo = kLedRed;
        else if (pulsado) fondo = kClosePress;
        else if (sobre)   fondo = kPressed;

        fb.fillRect(r, fondo);
        if (activo || sobre)
            fb.drawRect(r, activo ? kWhite : kLedRed, 1);

        // Etiqueta centrada, recortada si el bloque es estrecho.
        const char* nombre = kSectionNames[i];
        const i32 tw = font5x7::measure(nombre, 1);
        if (tw + 6 <= r.w) {
            font5x7::drawText(fb, r.x + (r.w - tw) / 2,
                              r.y + (r.h - textr::glyphHeight(1)) / 2,
                              nombre, (activo || sobre) ? kWhite : kGlyph, 1);
        } else {
            // No cabe el texto: un numero identifica igual el bloque.
            const char num[2] = { static_cast<char>('1' + i), '\0' };
            const i32 nw = font5x7::measure(num, 1);
            if (nw + 2 <= r.w)
                font5x7::drawText(fb, r.x + (r.w - nw) / 2,
                                  r.y + (r.h - textr::glyphHeight(1)) / 2,
                                  num, (activo || sobre) ? kWhite : kGlyph, 1);
        }
    }
}

void HomeScene::drawLibraryPanel(Framebuffer& fb) {
    const Recti p = m_leftPanel.rect();
    if (p.w <= 0 || p.h <= 0) return;

    fb.fillRect(p, kPanelDeep);

    // Recargar la lista de juegos instalados si cambio.
    if (!m_libraryLoaded) {
        m_libraryGames = cargarBiblioteca();
        m_libraryLoaded = true;
    }

    const Recti c   = m_leftPanel.contentRect();
    const i32   off = m_leftPanel.offset();

    // Primero los juegos instalados (casillas reales), luego huecos vacios.
    const i32 nInstalados = static_cast<i32>(m_libraryGames.size());
    for (i32 i = 0; i < kLibraryItems; ++i) {
        const i32 y = c.y + kItemPad + i * (kItemH + kItemPad) - off;

        // Recorte manual al area visible: el framebuffer no tiene clip por
        // region, asi que sin esto las fichas desplazadas invadirian el menu.
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + kItemH, c.y + c.h);
        if (y1 <= y0) continue;

        const i32 x = c.x + kItemPad;
        const i32 w = std::max(1, c.w - kItemPad * 2);

        const bool instalado = (i < nInstalados);
        const bool hover     = (m_libraryHover == i);
        fb.fillRect({x, y0, w, y1 - y0},
                    instalado ? (hover ? kAccent : kPanelViolet) : kPanelDeep);

        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 2}, instalado ? kOk : kPanelEdge);

        const i32 ty = y + (kItemH - textr::glyphHeight(2)) / 2;
        if (ty >= c.y && ty + font5x7::kGlyphH * 2 <= c.y + c.h) {
            if (instalado) {
                // Casilla del juego: nombre y "jugar".
                font5x7::drawTextUtf8(fb, x + 12, ty,
                                      m_libraryGames[i].name.c_str(),
                                      hover ? kWhite : kText, 2);
                if (ty + 22 + font5x7::kGlyphH <= c.y + c.h)
                    font5x7::drawText(fb, x + 12, ty + 22, "> JUGAR",
                                      hover ? kWhite : kOk, 1);
            } else {
                font5x7::drawText(fb, x + 12, ty, "(vacio)", kDim, 1);
            }
        }
    }

    m_leftPanel.renderBar(fb);
}

void HomeScene::layoutStore(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    // El panel ocupa todo el ancho para que su barra quede pegada a la pared
    // derecha de la ventana; las tarjetas se centran dentro.
    m_storePanel.setRect({0, top, fbW, std::max(0, fbH - top)});
    m_storePanel.setBarWidth(kScrollBarW);

    // Alto total: la cabecera (banner, tiras, carrusel, destacados, listas y
    // creadores), la rejilla del catalogo -- que crece con los juegos que
    // haya, no con un numero fijo de huecos -- y las dos rejillas de generos.
    const i32 cabecera = storeHeaderHeight();

    const i32 anchoUtil = std::min(kStoreHeroW, std::max(1, fbW - 16));
    const i32 cols  = std::max(1, anchoUtil / 300);
    const i32 juegos = static_cast<i32>(m_storeGames.size());
    const i32 filas = (juegos + cols - 1) / cols;
    const i32 rejilla = juegos > 0
        ? textr::glyphHeight(2) + 12 + filas * (150 + kStoreGap) + 20
        : 0;

    m_storePanel.setContentHeight(kStoreTopPad + cabecera + rejilla +
                                  storeGenresHeight());
}

void HomeScene::drawInstallBar(Framebuffer& fb) {
    // Se muestra mientras hay una instalacion en curso, en cualquier seccion.
    if (!m_install.running() &&
        !(m_install.done() && m_install.progress() < 1.0f))
        return;

    const i32 barH = 8;
    const i32 by   = fb.height() - barH;
    fb.fillRect({0, by, fb.width(), barH}, kFieldBg);
    const i32 w = static_cast<i32>(fb.width() * m_install.progress());
    fb.fillRect({0, by, w, barH}, kAccent);

    // El nombre sale de la instalacion en curso, no cableado: antes decia
    // siempre "Voxel World" aunque se estuviera instalando otra cosa.
    char pct[128];
    std::snprintf(pct, sizeof(pct), "Instalando %s  %d%%",
                  m_installingName.empty() ? "juego" : m_installingName.c_str(),
                  static_cast<i32>(m_install.progress() * 100.0f + 0.5f));
    font5x7::drawText(fb, 8, by - 14, pct, kWhite, 1);
}

// ===========================================================================
//  Ficha del producto: imagen, resenas y franja derecha con dos vistas
// ===========================================================================

void HomeScene::openProduct(i32 idx) {
    if (idx < 0 || idx >= static_cast<i32>(m_storeGames.size())) return;
    m_storeOpen = idx;
    m_section   = kProductSection;
    m_prodTab   = kTabPrices;
    m_prodReviewsPanel.setOffset(0);
    m_prodSidePanel.setOffset(0);
}

void HomeScene::ensureProductLoaded() {
    if (m_storeOpen < 0 ||
        m_storeOpen >= static_cast<i32>(m_storeGames.size())) return;

    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(m_storeOpen)];

    // Imagen promocional: se decodifica una vez por juego, no cada frame.
    if (m_prodArtOf != g.name) {
        m_prodArtOf = g.name;
        m_prodArtPixels.clear();
        m_prodArtW = m_prodArtH = 0;
        if (!g.listing.art.empty()) {
            std::vector<u32> px;
            i32 w = 0, h = 0;
            if (photo::loadAnyImage(g.listing.art, px, w, h) && w > 0 && h > 0) {
                m_prodArtPixels = std::move(px);
                m_prodArtW = w;
                m_prodArtH = h;
            }
        }
    }

    // Historial: igual, una lectura por juego.
    if (m_prodHistoryOf != g.name) {
        m_prodHistoryOf = g.name;
        bool hubo = false;
        m_prodHistory = history::forGame(kHistoryFile, g.name, &hubo);
        if (!hubo) {
            // Sin historial guardado se compone uno minimo con lo que se
            // sabe: el precio actual del catalogo y su version vigente. Asi
            // la linea cronologica dice algo en vez de quedarse vacia.
            m_prodHistory = history::GameHistory{};
            m_prodHistory.name = g.name;
            m_prodHistory.prices.push_back(
                history::PricePoint{ g.price.usdCents, 0 });
            for (const auto& v : g.versions)
                m_prodHistory.versions.push_back(
                    history::VersionRecord{ v.label, v.uploadedAt, L"", 0 });
        }
    }
}

void HomeScene::layoutProduct(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 margen = 16;

    // --- franja derecha: 800 px de alto, de arriba abajo de la pantalla ---
    // Se pide 800; si la ventana no da para tanto, ocupa lo que haya. Lo que
    // no cambia es que va pegada al borde derecho y llega a los dos extremos.
    const i32 sideW = std::max(1, std::min(320, fbW / 3));
    const i32 sideH = std::min(productSideHeight(), std::max(1, fbH - top));
    m_prodSideRect = Recti{ fbW - sideW, top, sideW, sideH };

    // Los dos botones de la franja, arriba del todo.
    const i32 tabH = 30;
    const i32 tabW = std::max(1, (sideW - 12) / 2);
    m_prodTabRect[kTabPrices]   = Recti{ m_prodSideRect.x + 4, top + 4,
                                         tabW, tabH };
    m_prodTabRect[kTabVersions] = Recti{ m_prodSideRect.x + 8 + tabW, top + 4,
                                         tabW, tabH };

    // El contenido de la franja empieza bajo los botones.
    m_prodSidePanel.setRect({ m_prodSideRect.x + 4, top + tabH + 10,
                              std::max(1, sideW - 8),
                              std::max(1, sideH - tabH - 14) });

    // --- zona central: imagen arriba, resenas abajo ---
    const i32 centroW = std::max(1, fbW - sideW - margen * 2);
    const i32 centroX = margen;

    // La imagen se lleva el tercio superior; las resenas, el resto.
    const i32 artH = std::max(1, std::min((fbH - top) / 2, centroW * 9 / 16));
    m_prodArtRect = Recti{ centroX, top + 40, centroW, artH };

    // --- DESCARGAR, dentro de la imagen: pared derecha y abajo del todo ---
    // Va sobre la imagen porque es la accion principal de la ficha; pegarlo a
    // la esquina lo deja donde se mira al final, sin tapar el centro de la
    // portada.
    {
        const i32 pad = 10;
        const i32 bw = std::max(1, std::min(170, m_prodArtRect.w / 3));
        const i32 bh = 38;
        m_prodBuyRect = Recti{
            m_prodArtRect.x + m_prodArtRect.w - bw - pad,
            m_prodArtRect.y + m_prodArtRect.h - bh - pad,
            bw, bh };
    }

    const i32 revY = m_prodArtRect.y + artH + 14;
    m_prodReviewsPanel.setRect({ centroX, revY, centroW,
                                 std::max(1, fbH - revY - margen) });

    const char* volver = "< VOLVER A LA TIENDA";
    const i32 vw = font5x7::measureUtf8(volver, 1);
    m_prodBackRect = Recti{ centroX, top + 10, vw + 12, 20 };
}

void HomeScene::updateProduct(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    // El aviso de la descarga se agota solo.
    if (m_prodBuyMsgTime > 0.0) {
        m_prodBuyMsgTime -= dt;
        if (m_prodBuyMsgTime < 0.0) m_prodBuyMsgTime = 0.0;
    }

    // Volver a la Tienda.
    m_prodBackHover = dentro(m_prodBackRect);
    if (in.mouseReleased(0) && m_prodBackHover) {
        m_section   = kStoreSection;
        m_storeOpen = -1;
        return;
    }

    // --- DESCARGAR ---
    // Se atiende antes que los paneles: cae sobre la imagen, y no debe
    // competir con el scroll de nada.
    m_prodBuyHover = dentro(m_prodBuyRect);
    if (in.mousePressed(0) && m_prodBuyHover) { m_prodBuyDown = true; return; }
    if (in.mouseReleased(0) && m_prodBuyDown) {
        const bool pulsado = m_prodBuyHover;
        m_prodBuyDown = false;
        if (pulsado && m_storeOpen >= 0 &&
            m_storeOpen < static_cast<i32>(m_storeGames.size())) {
            const devcatalog::Game& g =
                m_storeGames[static_cast<size_t>(m_storeOpen)];

            // Ya en la biblioteca: se dice en vez de duplicar la entrada.
            if (library::has(kLibraryFile, g.name)) {
                m_prodBuyMsg     = "Ya lo tienes en tu biblioteca.";
                m_prodBuyMsgErr  = false;
                m_prodBuyMsgTime = 2.0;
                return;
            }

            // --- instalacion real, igual para todos los juegos ---
            // El instalador copia TODOS los archivos del juego y cifra solo
            // los datos del jugador; las texturas, sonidos y ejecutables
            // quedan en claro para que el juego los lea directo. Es el mismo
            // proceso es el mismo para todos los juegos.
            if (m_install.running()) {
                m_prodBuyMsg     = "Ya hay una instalacion en curso...";
                m_prodBuyMsgErr  = false;
                m_prodBuyMsgTime = 2.0;
                return;
            }

            // --- paquete guardado: el camino bueno ---
            // Si el creador uso GUARDAR ARCHIVOS, el juego entero vive dentro
            // de Ludora y se instala desde ahi. Es lo unico que funciona en un
            // equipo que no sea el suyo: las dos ramas de abajo dependen de
            // rutas de SU disco.
            const devcatalog::Version* vv = g.current();
            if (vv && !vv->archive.empty()) {
                std::wstring carpetaP;
                for (const char c : g.name) {
                    const wchar_t w = static_cast<wchar_t>(
                        static_cast<unsigned char>(c));
                    carpetaP.push_back((w == L'\\' || w == L'/' || w == L':' ||
                                        w == L'*'  || w == L'?' || w == L'"' ||
                                        w == L'<'  || w == L'>' || w == L'|')
                                       ? L'_' : w);
                }
                if (carpetaP.empty()) carpetaP = L"juego";
                const std::wstring destinoP =
                    std::wstring(kGamesDir) + L"\\" + carpetaP;

                std::string perr;
                if (!gamevault::unsealWholeFolder(vv->archive, destinoP, perr)) {
                    m_prodBuyMsg     = perr.empty()
                                     ? "No se pudo abrir el paquete del juego."
                                     : perr;
                    m_prodBuyMsgErr  = true;
                    m_prodBuyMsgTime = 3.0;
                    return;
                }

                // El paquete puede no traer ningun .exe -- un juego subido
                // sin compilar, por ejemplo. Antes se registraba igual, con
                // el ejecutable vacio: el juego aparecia instalado en la
                // biblioteca y no arrancaba nunca, sin explicar por que.
                const std::wstring exeJuego = findFirstExe(destinoP);
                if (exeJuego.empty()) {
                    m_prodBuyMsg     = "Los archivos se descargaron, pero el "
                                       "paquete no trae ningun ejecutable.";
                    m_prodBuyMsgErr  = true;
                    m_prodBuyMsgTime = 4.0;
                    return;
                }

                // Queda en la biblioteca con sus archivos en claro: el paquete
                // ya cumplio su funcion de transporte.
                library::Entry e;
                e.name = g.name;
                e.exe  = exeJuego;
                library::add(kLibraryFile, e);
                m_libraryLoaded = false;   // la biblioteca se relee

                m_prodBuyMsg     = "Descargado: el juego ya esta en tu biblioteca.";
                m_prodBuyMsgErr  = false;
                m_prodBuyMsgTime = 3.0;
                return;
            }

            // Origen: el codigo que subio el creador; si no lo hay, el
            // ejecutable de la version vigente.
            std::wstring origen = g.listing.source;
            if (origen.empty() && g.current()) origen = g.current()->exe;

            if (origen.empty()) {
                m_prodBuyMsg    = "Ese juego no tiene archivos para instalar.";
                m_prodBuyMsgErr = true;
                m_prodBuyMsgTime = 2.0;
                return;
            }

            // Destino: una carpeta por juego dentro de la galeria, con el
            // nombre saneado para que sirva de nombre de carpeta.
            std::wstring carpeta;
            for (const char c : g.name) {
                const wchar_t w = static_cast<wchar_t>(
                    static_cast<unsigned char>(c));
                carpeta.push_back((w == L'\\' || w == L'/' || w == L':' ||
                                   w == L'*'  || w == L'?' || w == L'"' ||
                                   w == L'<'  || w == L'>' || w == L'|')
                                  ? L'_' : w);
            }
            if (carpeta.empty()) carpeta = L"juego";

            const std::wstring destino =
                std::wstring(kGamesDir) + L"\\" + carpeta;

            // El nombre del ejecutable sale del origen: es lo que habra que
            // lanzar despues desde la biblioteca.
            const size_t s = origen.find_last_of(L"\\/");
            const std::wstring exeName =
                (s == std::wstring::npos) ? origen : origen.substr(s + 1);

            m_install.start(origen, destino, g.name, exeName);
            m_installStarted   = true;
            m_installingName   = g.name;
            m_installingCents  = g.price.usdCents;
            m_prodBuyMsg       = "Descargando e instalando...";
            m_prodBuyMsgErr    = false;
            m_prodBuyMsgTime   = 2.0;
        }
        return;
    }

    // Los dos botones de la franja derecha.
    m_prodTabHover = -1;
    for (i32 i = 0; i < 2; ++i)
        if (dentro(m_prodTabRect[i])) m_prodTabHover = i;

    if (in.mouseReleased(0) && m_prodTabHover >= 0) {
        if (m_prodTab != m_prodTabHover) {
            m_prodTab = m_prodTabHover;
            m_prodSidePanel.setOffset(0);   // cada vista empieza arriba
        }
        return;
    }

    // Los dos paneles con scroll. El que esta bajo el cursor se lleva la
    // rueda, para poder leer las resenas y la franja por separado.
    if (dentro(m_prodSidePanel.rect()))    m_prodSidePanel.update(engine);
    if (dentro(m_prodReviewsPanel.rect())) m_prodReviewsPanel.update(engine);
}

std::vector<std::string> HomeScene::storeGameNames() const {
    std::vector<std::string> out;
    out.reserve(m_storeGames.size());
    for (const auto& g : m_storeGames) out.push_back(g.name);
    return out;
}

void HomeScene::drawProductPrices(Framebuffer& fb, Recti area) {
    const auto& puntos = m_prodHistory.prices;
    if (puntos.empty()) {
        const char* v = "Sin datos de precio";
        const i32 w = font5x7::measureUtf8(v, 1);
        if (w < area.w - 8)
            font5x7::drawTextUtf8(fb, area.x + (area.w - w) / 2,
                                  area.y + 20, v, kDim, 1);
        return;
    }

    // Una casilla por punto, de la mas antigua arriba a la vigente abajo.
    // Cada cambio de precio que registre el creador anade una.
    constexpr i32 kCasillaH = 54;
    const i32 alto = static_cast<i32>(puntos.size()) * kCasillaH + 8;
    m_prodSidePanel.setContentHeight(alto);
    m_prodSidePanel.renderBar(fb);

    const Recti cr = m_prodSidePanel.contentRect();

    for (size_t i = 0; i < puntos.size(); ++i) {
        const i32 y = cr.y + static_cast<i32>(i) * kCasillaH -
                      m_prodSidePanel.offset();
        if (y + kCasillaH <= cr.y || y >= cr.y + cr.h) continue;

        const bool vigente = (i + 1 == puntos.size());
        const Recti caja{ cr.x + 4, y, std::max(1, cr.w - 8), kCasillaH - 8 };

        fb.fillRect(caja, vigente ? kPanelViolet : kFieldBg);
        fb.drawRect(caja, vigente ? kOk : kPanelEdge, vigente ? 2 : 1);

        // Hilo vertical que une las casillas: es lo que la hace leerse como
        // una linea de tiempo y no como una lista suelta.
        if (i + 1 < puntos.size())
            fb.fillRect({ caja.x + caja.w / 2 - 1, caja.y + caja.h,
                          2, kCasillaH - caja.h }, kPanelEdge);

        std::string usd, mxn, eur;
        devcatalog::formatPrice(puntos[i].usdCents, usd, mxn, eur);

        font5x7::drawTextUtf8(fb, caja.x + 8, caja.y + 6, usd.c_str(),
                              vigente ? kWhite : kText, 2);

        // Fecha y etiqueta de que punto es, para situarlo en el tiempo.
        const std::string fecha = history::formatDate(puntos[i].at);
        const char* etiqueta = (i == 0) ? "PRECIO INICIAL"
                             : vigente   ? "PRECIO ACTUAL" : "CAMBIO";
        const std::string pie = std::string(etiqueta) + "  " + fecha;
        const i32 pw = font5x7::measureUtf8(pie.c_str(), 1);
        if (pw < caja.w - 12)
            font5x7::drawTextUtf8(fb, caja.x + 8, caja.y + caja.h - 12,
                                  pie.c_str(), vigente ? kOk : kDim, 1);
    }
}

void HomeScene::drawProductVersions(Framebuffer& fb, Recti area) {
    const auto& vers = m_prodHistory.versions;
    if (vers.empty()) {
        const char* v = "Aun no hay versiones publicadas";
        i32 e = 1;
        const i32 w = font5x7::measureUtf8(v, e);
        if (w < area.w - 8)
            font5x7::drawTextUtf8(fb, area.x + (area.w - w) / 2,
                                  area.y + 20, v, kDim, e);
        return;
    }

    // Por ahora la lista muestra cada version con su fecha. Los tres botones
    // de cada casilla (resumen, descargar y resenar) llegan en la siguiente
    // tanda: necesitan su propio almacen y su seccion de descargas.
    constexpr i32 kCasillaH = 62;
    const i32 alto = static_cast<i32>(vers.size()) * kCasillaH + 8;
    m_prodSidePanel.setContentHeight(alto);
    m_prodSidePanel.renderBar(fb);

    const Recti cr = m_prodSidePanel.contentRect();

    for (size_t i = 0; i < vers.size(); ++i) {
        const i32 y = cr.y + static_cast<i32>(i) * kCasillaH -
                      m_prodSidePanel.offset();
        if (y + kCasillaH <= cr.y || y >= cr.y + cr.h) continue;

        const bool jugable = (i + 1 == vers.size());
        const Recti caja{ cr.x + 4, y, std::max(1, cr.w - 8), kCasillaH - 8 };

        fb.fillRect(caja, jugable ? kPanelViolet : kFieldBg);
        fb.drawRect(caja, jugable ? kOk : kPanelEdge, jugable ? 2 : 1);

        if (i + 1 < vers.size())
            fb.fillRect({ caja.x + caja.w / 2 - 1, caja.y + caja.h,
                          2, kCasillaH - caja.h }, kPanelEdge);

        std::string titulo = vers[i].label;
        if (jugable) titulo += "  (VIGENTE)";
        font5x7::drawTextUtf8(fb, caja.x + 8, caja.y + 6, titulo.c_str(),
                              jugable ? kWhite : kText, 2);

        const std::string fecha = history::formatDate(vers[i].at);
        const i32 fw = font5x7::measureUtf8(fecha.c_str(), 1);
        if (fw < caja.w - 12)
            font5x7::drawTextUtf8(fb, caja.x + 8, caja.y + caja.h - 12,
                                  fecha.c_str(), jugable ? kOk : kDim, 1);
    }
}

void HomeScene::drawProduct(Engine& engine, Framebuffer& fb) {
    layoutProduct(engine, fb.width(), fb.height());
    ensureProductLoaded();

    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    fb.fillRect({0, top, fb.width(), fb.height() - top}, kBg);

    if (m_storeOpen < 0 ||
        m_storeOpen >= static_cast<i32>(m_storeGames.size())) return;
    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(m_storeOpen)];

    // --- cabecera: volver y nombre ---
    font5x7::drawTextUtf8(fb, m_prodBackRect.x + 6, m_prodBackRect.y + 6,
                          "< VOLVER A LA TIENDA",
                          m_prodBackHover ? kWhite : kDim, 1);

    // --- imagen que puso el creador ---
    const Recti a = m_prodArtRect;
    if (a.w > 0 && a.h > 0) {
        fb.fillRect(a, kPanelDeep);

        if (m_prodArtW > 0 && m_prodArtH > 0 && !m_prodArtPixels.empty()) {
            // Encajada conservando la proporcion: deformar la portada de un
            // juego es justo lo que el creador no quiere.
            const i32 dispW = a.w - 8, dispH = a.h - 8;
            const i32 porAncho = dispW * m_prodArtH / std::max(1, m_prodArtW);
            const i32 dstW = (porAncho <= dispH)
                           ? dispW : dispH * m_prodArtW / std::max(1, m_prodArtH);
            const i32 dstH = (porAncho <= dispH) ? porAncho : dispH;
            const i32 ox = a.x + (a.w - dstW) / 2;
            const i32 oy = a.y + (a.h - dstH) / 2;

            for (i32 yy = 0; yy < dstH; ++yy)
                for (i32 xx = 0; xx < dstW; ++xx) {
                    const i32 sx = std::min(m_prodArtW - 1,
                                            xx * m_prodArtW / std::max(1, dstW));
                    const i32 sy = std::min(m_prodArtH - 1,
                                            yy * m_prodArtH / std::max(1, dstH));
                    fb.setPixel(ox + xx, oy + yy,
                        m_prodArtPixels[static_cast<size_t>(sy) * m_prodArtW + sx]);
                }
        } else {
            // Sin imagen: el nombre del juego ocupa su sitio, para que la
            // ficha no se vea rota.
            i32 e = 4;
            while (e > 1 && font5x7::measureUtf8(g.name.c_str(), e) > a.w - 24) --e;
            const i32 w = font5x7::measureUtf8(g.name.c_str(), e);
            font5x7::drawTextUtf8(fb, a.x + (a.w - w) / 2,
                                  a.y + a.h / 2 - textr::glyphHeight(e) / 2,
                                  g.name.c_str(), kDim, e);
        }
        fb.drawRect(a, kPanelEdge, 2);

        // Nombre y precio sobre la imagen, abajo a la izquierda.
        std::string usd, mxn, eur;
        devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);
        i32 en = 3;
        while (en > 1 && font5x7::measureUtf8(g.name.c_str(), en) > a.w / 2) --en;
        font5x7::drawTextUtf8(fb, a.x + 10, a.y + a.h - 34, g.name.c_str(),
                              kWhite, en);
        font5x7::drawTextUtf8(fb, a.x + 10, a.y + a.h - 14, usd.c_str(), kOk, 1);

        // --- DESCARGAR, en la esquina inferior derecha de la imagen ---
        // Se dibuja despues de la portada para quedar encima de ella.
        const Recti b = m_prodBuyRect;
        if (b.w > 0 && b.h > 0) {
            // Tres estados: ya lo tienes, instalandose, o disponible. Se
            // compara con el juego que de verdad se esta instalando, no con
            // un nombre fijo.
            const bool instalando = m_install.running() &&
                                    m_installingName == g.name;
            const bool tengo = !instalando && library::has(kLibraryFile, g.name);
            const bool hov   = m_prodBuyHover && !tengo && !instalando;

            auto ajusta = [&](Color c, f32 k) {
                return Color{ static_cast<u8>(std::clamp(c.r * k, 0.0f, 255.0f)),
                              static_cast<u8>(std::clamp(c.g * k, 0.0f, 255.0f)),
                              static_cast<u8>(std::clamp(c.b * k, 0.0f, 255.0f)) };
            };
            // Verde cuando se puede descargar; apagado cuando ya se tiene,
            // para que se vea antes de pulsarlo.
            const Color base = tengo      ? kFieldBg
                             : instalando ? kDevGold : kConsentGreen;
            fb.fillRect(b, m_prodBuyDown ? ajusta(base, 0.7f)
                        : hov            ? ajusta(base, 1.25f) : base);
            fb.drawRect(b, tengo ? kPanelEdge
                        : instalando ? kDevGold
                        : (hov ? kWhite : kConsentGreen), 2);

            // Mientras instala, el relleno avanza con el progreso: el propio
            // boton hace de barra, sin ocupar mas sitio en la ficha.
            if (instalando) {
                const i32 lleno = static_cast<i32>(b.w * m_install.progress());
                fb.fillRect({ b.x, b.y, std::max(0, lleno), b.h },
                            ajusta(kDevGold, 1.4f));
            }

            const char* txt = tengo      ? "EN TU BIBLIOTECA"
                            : instalando ? "INSTALANDO..." : "DESCARGAR";
            i32 e = 2;
            while (e > 1 && font5x7::measureUtf8(txt, e) > b.w - 10) --e;
            const i32 tw = font5x7::measureUtf8(txt, e);
            font5x7::drawTextUtf8(fb, b.x + (b.w - tw) / 2,
                                  b.y + (b.h - textr::glyphHeight(e)) / 2,
                                  txt, tengo ? kDim : (instalando ? kBg : kWhite), e);
        }
    }

    // --- aviso de la descarga, bajo el boton ---
    // Fuera del bloque de la imagen para que se lea aunque la portada no se
    // haya podido dibujar.
    if (m_prodBuyMsgTime > 0.0 && !m_prodBuyMsg.empty()) {
        const i32 mw = font5x7::measureUtf8(m_prodBuyMsg.c_str(), 1);
        const i32 mx = m_prodBuyRect.x + m_prodBuyRect.w - mw;
        const i32 my = m_prodBuyRect.y + m_prodBuyRect.h + 6;
        if (mx > m_prodArtRect.x && my + font5x7::kGlyphH < fb.height()) {
            // Fondo propio: el aviso cae sobre la zona de resenas y sin el se
            // mezclaria con su texto.
            fb.fillRect({ mx - 6, my - 4, mw + 12, textr::glyphHeight(1) + 8 }, kBg);
            font5x7::drawTextUtf8(fb, mx, my, m_prodBuyMsg.c_str(),
                                  m_prodBuyMsgErr ? kAccent : kOk, 1);
        }
    }

    // --- resenas, abajo y con scroll ---
    const Recti rp = m_prodReviewsPanel.rect();
    if (rp.w > 0 && rp.h > 0) {
        fb.fillRect(rp, kPanelDeep);
        fb.drawRect(rp, kPanelEdge, 1);
        font5x7::drawTextUtf8(fb, rp.x + 10, rp.y + 8, "RESEÑAS", kAccent, 2);

        // Aun no hay almacen de resenas: se dice, en vez de fingir que si.
        m_prodReviewsPanel.setContentHeight(rp.h + 1);
        const char* vacio = "Aun no hay resenas de jugadores.";
        const i32 vw = font5x7::measureUtf8(vacio, 1);
        if (vw < rp.w - 20)
            font5x7::drawTextUtf8(fb, rp.x + (rp.w - vw) / 2,
                                  rp.y + rp.h / 2, vacio, kDim, 1);
    }

    // --- franja derecha: 800 px, de arriba abajo ---
    const Recti s = m_prodSideRect;
    fb.fillRect(s, kPanelDeep);
    fb.drawRect(s, kBorder, 2);

    // Los dos botones que alternan la vista.
    const char* tabs[2] = { "PRECIO", "VERSIONES" };
    for (i32 i = 0; i < 2; ++i) {
        const Recti r = m_prodTabRect[i];
        if (r.w <= 0) continue;
        const bool act = (m_prodTab == i);
        const bool hov = (m_prodTabHover == i);
        fb.fillRect(r, act ? kAccent : (hov ? kHover : kFieldBg));
        fb.drawRect(r, act ? kWhite : kBorder, act ? 2 : 1);

        font5x7::drawFitted(fb, r, tabs[i],
                            act || hov ? kWhite : kGlyph, 1, 4);
    }

    if (m_prodTab == kTabPrices) drawProductPrices(fb, m_prodSidePanel.rect());
    else                         drawProductVersions(fb, m_prodSidePanel.rect());
}

i32 HomeScene::storeHeaderHeight() {
    // Un solo sitio donde vive este calculo: lo usan el alto del scroll, el
    // desplazamiento del catalogo y las pruebas. Repartido, cualquier cambio
    // de maquetacion dejaria una de las tres partes descuadrada.
    const i32 bannerYTiras = 190 + 14 + 132 + 24;
    const i32 carrusel     = 240 + 30;
    const i32 destacados   = 220 + 30;
    // El +10 tras el titulo es el MISMO avance que hace drawStoreLists: si
    // los dos no coinciden, el ultimo renglon se queda sin sitio al final del
    // desplazamiento.
    const i32 listas       = (textr::glyphHeight(2) + 10 + 44 * 5 + 18) * 2 + 10;
    const i32 creadores    = 150 + 40;
    return bannerYTiras + carrusel + destacados + listas + creadores;
}

i32 HomeScene::storeGenresHeight() {
    // Las dos rejillas de botones, con el ancho tipico de la tienda (900).
    // Se calcula aparte del resto porque depende del numero de generos, que
    // puede crecer.
    constexpr i32 kBtnH = 30, kBtnGap = 8;
    const i32 ancho = 900;
    const i32 cols  = std::max(1, ancho / 170);
    const i32 colsC = std::max(1, ancho / 230);
    const i32 filas1 = (kUpGenreCount + cols  - 1) / cols;
    const i32 filas2 = (kUpGenreCount + colsC - 1) / colsC;
    return textr::glyphHeight(2) + 12 + filas1 * (kBtnH + kBtnGap) + 24
         + textr::glyphHeight(2) + 12 + filas2 * (kBtnH + kBtnGap) + 20;
}

void HomeScene::drawCarouselArrow(Framebuffer& fb, Recti r, bool derecha,
                                  bool hover) const {
    if (r.w <= 0 || r.h <= 0) return;

    // Pastilla gris translucida, como las de una tienda de juegos: discreta
    // en reposo y clara al pasar el cursor, sin robar atencion a la imagen.
    Color fondo = kSteamHeader;
    fondo.a = hover ? 230 : 150;
    fb.fillRect(r, fondo);
    fb.drawRect(r, hover ? kSteamArrowHi : kSteamEdge, 1);

    // Triangulo macizo, rasterizado por filas: a esta escala queda mas
    // limpio que dibujar tres lineas y rellenar.
    const i32 cx = r.x + r.w / 2;
    const i32 cy = r.y + r.h / 2;
    const i32 alto = std::min(r.h - 8, 18);
    const i32 ancho = alto / 2 + 2;
    const Color c = hover ? kSteamArrowHi : kSteamArrow;

    for (i32 i = 0; i < alto; ++i) {
        // Ancho de la fila: maximo en la base, cero en la punta.
        const i32 desde = std::abs(i - alto / 2);
        const i32 w = std::max(0, ancho - desde * ancho * 2 / std::max(1, alto));
        const i32 y = cy - alto / 2 + i;
        if (derecha) fb.fillRect({ cx - ancho / 2, y, w, 1 }, c);
        else         fb.fillRect({ cx + ancho / 2 - w, y, w, 1 }, c);
    }
}

void HomeScene::updateCarousels(f64 dt) {
    // El deslizamiento avanza hasta asentarse. La velocidad es constante y el
    // dibujado aplica la suavizacion, que es donde se nota.
    constexpr f32 kVel = 3.2f;   // pantallas por segundo

    if (m_carDir != 0) {
        m_carSlide -= m_carDir * kVel * static_cast<f32>(dt);
        if ((m_carDir > 0 && m_carSlide <= 0.0f) ||
            (m_carDir < 0 && m_carSlide >= 0.0f)) {
            m_carSlide = 0.0f;
            m_carDir   = 0;
        }
    }

    if (m_topDir != 0) {
        m_topSlide -= m_topDir * kVel * static_cast<f32>(dt);
        if ((m_topDir > 0 && m_topSlide <= 0.0f) ||
            (m_topDir < 0 && m_topSlide >= 0.0f)) {
            m_topSlide = 0.0f;
            m_topDir   = 0;
        }
    }
}

void HomeScene::carouselStep(i32 dir) {
    if (m_carDir != 0 || m_storeGames.empty()) return;   // ya se mueve
    const i32 n = static_cast<i32>(m_storeGames.size());
    m_carIndex = ((m_carIndex + dir) % n + n) % n;
    // La imagen nueva entra desde el lado contrario al del movimiento.
    m_carSlide = static_cast<f32>(dir);
    m_carDir   = dir;
    m_carOf.clear();   // forzar recarga de la imagen
}

void HomeScene::refreshTopRated() {
    m_topOrder.clear();
    const i32 n = static_cast<i32>(m_storeGames.size());
    if (n == 0) return;

    // Una sola lectura del archivo de resenas para todos los juegos: pedir el
    // resumen uno a uno lo releeria N veces.
    const auto resumenes = reviews::summaryAll(kReviewsFile);

    auto notaDe = [&](i32 idx) {
        const std::string& nombre = m_storeGames[static_cast<size_t>(idx)].name;
        for (const auto& par : resumenes)
            if (par.first == nombre) return par.second;
        return reviews::Summary{};
    };

    for (i32 i = 0; i < n; ++i) m_topOrder.push_back(i);

    // Mejor valorado primero. A igual nota manda quien tiene mas resenas: una
    // media de 5 con una sola opinion no deberia ganar a un 4.8 con cien.
    std::sort(m_topOrder.begin(), m_topOrder.end(), [&](i32 a, i32 b) {
        const reviews::Summary sa = notaDe(a), sb = notaDe(b);
        if (sa.averageX100() != sb.averageX100())
            return sa.averageX100() > sb.averageX100();
        return sa.count > sb.count;
    });

    m_topRefresh = 0.0;
}

void HomeScene::ensureFollowingLoaded() {
    if (m_followingLoaded) return;
    m_followingLoaded = true;
    m_following.clear();

    std::ifstream in(kFollowFile, std::ios::binary);
    if (!in) return;
    std::string linea;
    while (std::getline(in, linea)) {
        // Quitar retorno de carro y espacios: el archivo se escribe con CRLF.
        while (!linea.empty() &&
               (linea.back() == '\r' || linea.back() == '\n' ||
                linea.back() == ' '  || linea.back() == '\t'))
            linea.pop_back();
        if (linea.empty() || linea[0] == '#') continue;
        m_following.push_back(linea);
    }
}

void HomeScene::saveFollowing() const {
    std::ofstream out(kFollowFile, std::ios::binary);
    if (!out) return;
    out << "# Creadores seguidos.\r\n";
    for (const auto& e : m_following) out << e << "\r\n";
}

bool HomeScene::isFollowing(const std::string& email) const {
    return std::find(m_following.begin(), m_following.end(), email) !=
           m_following.end();
}

void HomeScene::toggleFollowForTest(const std::string& e) {
    ensureFollowingLoaded();
    const auto it = std::find(m_following.begin(), m_following.end(), e);
    if (it != m_following.end()) m_following.erase(it);
    else                         m_following.push_back(e);
    saveFollowing();
}

std::vector<i32> HomeScene::stripGames(i32 strip) const {
    std::vector<i32> out;
    const i32 n = static_cast<i32>(m_storeGames.size());

    // Cada mini-seccion filtra el catalogo de una forma. Los criterios salen
    // de datos que EXISTEN, no de etiquetas inventadas:
    //   - proximamente: los que aun no tienen version publicada,
    //   - ofertas: los mas baratos que no son gratis, y los gratis,
    //   - recien publicados: los ultimos del catalogo, que es el orden de alta.
    if (strip == kStripSoon) {
        for (i32 i = 0; i < n; ++i)
            if (m_storeGames[static_cast<size_t>(i)].versions.empty())
                out.push_back(i);
    } else if (strip == kStripOffers) {
        std::vector<i32> todos;
        for (i32 i = 0; i < n; ++i) todos.push_back(i);
        // Del mas barato al mas caro; el gratis entra primero.
        std::sort(todos.begin(), todos.end(), [&](i32 a, i32 b) {
            return m_storeGames[static_cast<size_t>(a)].price.usdCents <
                   m_storeGames[static_cast<size_t>(b)].price.usdCents;
        });
        for (i32 i = 0; i < n && i < 6; ++i) out.push_back(todos[static_cast<size_t>(i)]);
    } else {
        // Recien publicados: los ultimos, del mas nuevo al mas viejo.
        for (i32 i = n - 1; i >= 0 && static_cast<i32>(out.size()) < 6; --i)
            if (!m_storeGames[static_cast<size_t>(i)].versions.empty())
                out.push_back(i);
    }
    return out;
}

void HomeScene::ensureBannerLoaded() {
    if (m_storeGames.empty()) return;
    const i32 idx = m_bannerIndex % static_cast<i32>(m_storeGames.size());
    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(idx)];
    if (m_bannerOf == g.name) return;

    // Una decodificacion por juego, no por frame.
    m_bannerOf = g.name;
    m_bannerPixels.clear();
    m_bannerW = m_bannerH = 0;
    if (g.listing.art.empty()) return;

    std::vector<u32> px;
    i32 w = 0, h = 0;
    if (photo::loadAnyImage(g.listing.art, px, w, h) && w > 0 && h > 0) {
        m_bannerPixels = std::move(px);
        m_bannerW = w;
        m_bannerH = h;
    }
}

void HomeScene::drawStoreBanner(Framebuffer& fb, Recti area) {
    if (area.w <= 0 || area.h <= 0) return;
    ensureBannerLoaded();

    fb.fillRect(area, kPanelDeep);

    if (m_storeGames.empty()) {
        const char* v = "Aun no hay juegos en la tienda";
        const i32 w = font5x7::measureUtf8(v, 2);
        if (w < area.w - 20)
            font5x7::drawTextUtf8(fb, area.x + (area.w - w) / 2,
                                  area.y + area.h / 2, v, kDim, 2);
        fb.drawRect(area, kBorder, 2);
        return;
    }

    const i32 idx = m_bannerIndex % static_cast<i32>(m_storeGames.size());
    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(idx)];

    // --- imagen a lo ancho ---
    if (m_bannerW > 0 && m_bannerH > 0 && !m_bannerPixels.empty()) {
        // El banner es apaisado: se recorta la imagen por el centro en vez de
        // deformarla, que es como se comporta una cabecera de tienda.
        for (i32 yy = 0; yy < area.h; ++yy) {
            for (i32 xx = 0; xx < area.w; ++xx) {
                // Escala para CUBRIR el area, y desplazamiento al centro.
                const i32 escalaX = m_bannerW * 1000 / std::max(1, area.w);
                const i32 escalaY = m_bannerH * 1000 / std::max(1, area.h);
                const i32 esc = std::min(escalaX, escalaY);
                const i32 sx = (xx * esc) / 1000 +
                               (m_bannerW - (area.w * esc) / 1000) / 2;
                const i32 sy = (yy * esc) / 1000 +
                               (m_bannerH - (area.h * esc) / 1000) / 2;
                if (sx < 0 || sy < 0 || sx >= m_bannerW || sy >= m_bannerH) continue;
                fb.setPixel(area.x + xx, area.y + yy,
                            m_bannerPixels[static_cast<size_t>(sy) * m_bannerW + sx]);
            }
        }

        // Velo oscuro en la franja baja: sin el, el texto encima de una
        // imagen clara no se leeria.
        const i32 velo = std::min(area.h, 56);
        for (i32 yy = 0; yy < velo; ++yy) {
            Color c = kBg;
            c.a = static_cast<u8>(40 + (215 * yy) / std::max(1, velo - 1));
            fb.fillRect({ area.x, area.y + area.h - velo + yy, area.w, 1 }, c);
        }
    }

    // --- rotulo y precio ---
    const char* eyebrow = "DESTACADO DE LA SEMANA";
    font5x7::drawTextUtf8(fb, area.x + 16, area.y + 12, eyebrow, kAccent, 1);

    i32 e = 4;
    while (e > 1 && font5x7::measureUtf8(g.name.c_str(), e) > area.w - 32) --e;
    font5x7::drawTextUtf8(fb, area.x + 16, area.y + area.h - 40,
                          g.name.c_str(), kWhite, e);

    std::string usd, mxn, eur;
    devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);
    font5x7::drawTextUtf8(fb, area.x + 16, area.y + area.h - 16,
                          usd.c_str(), kOk, 2);

    // Puntitos: cuantos juegos rotan y cual se ve ahora.
    const i32 n = static_cast<i32>(m_storeGames.size());
    if (n > 1) {
        const i32 dr = 4, sep = 12;
        const i32 total = n * sep;
        const i32 px0 = area.x + area.w - total - 12;
        for (i32 i = 0; i < n; ++i)
            fb.fillCircle(px0 + i * sep, area.y + area.h - 16, dr,
                          i == idx ? kWhite : kPanelEdge);
    }

    fb.drawRect(area, kBorder, 2);
}

void HomeScene::drawStoreCarousel(Framebuffer& fb, Recti area) {
    if (area.w <= 0 || area.h <= 0 || m_storeGames.empty()) return;

    const i32 n = static_cast<i32>(m_storeGames.size());
    const i32 idx = ((m_carIndex % n) + n) % n;
    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(idx)];

    // Imagen del juego actual, cargada una vez por juego.
    if (m_carOf != g.name) {
        m_carOf = g.name;
        m_carPixels.clear();
        m_carW = m_carH = 0;
        if (!g.listing.art.empty()) {
            std::vector<u32> px; i32 w = 0, h = 0;
            if (photo::loadAnyImage(g.listing.art, px, w, h) && w > 0 && h > 0) {
                m_carPixels = std::move(px); m_carW = w; m_carH = h;
            }
        }
    }

    fb.fillRect(area, kSteamPanel);

    // Suavizado del deslizamiento: arranca rapido y frena al llegar, que es
    // lo que hace que se lea como un gesto y no como un salto.
    const f32 s = m_carSlide;
    const f32 e = (s < 0.0f) ? -(1.0f - (1.0f + s) * (1.0f + s))
                             :  (1.0f - (1.0f - s) * (1.0f - s));
    const i32 desliz = static_cast<i32>(e * area.w);

    if (m_carW > 0 && m_carH > 0 && !m_carPixels.empty()) {
        // La imagen se dibuja desplazada: durante la animacion entra por un
        // lado mientras la anterior sale por el otro.
        for (i32 yy = 0; yy < area.h; ++yy) {
            for (i32 xx = 0; xx < area.w; ++xx) {
                const i32 dx = xx + desliz;
                if (dx < 0 || dx >= area.w) continue;
                const i32 ex = m_carW * 1000 / std::max(1, area.w);
                const i32 ey = m_carH * 1000 / std::max(1, area.h);
                const i32 esc = std::min(ex, ey);
                const i32 sx = (xx * esc) / 1000 +
                               (m_carW - (area.w * esc) / 1000) / 2;
                const i32 sy = (yy * esc) / 1000 +
                               (m_carH - (area.h * esc) / 1000) / 2;
                if (sx < 0 || sy < 0 || sx >= m_carW || sy >= m_carH) continue;
                fb.setPixel(area.x + dx, area.y + yy,
                            m_carPixels[static_cast<size_t>(sy) * m_carW + sx]);
            }
        }
    }

    // Franja inferior con nombre y precio, como las cabeceras de tienda.
    const i32 franjaH = 46;
    Color velo = kSteamHeader; velo.a = 225;
    fb.fillRect({ area.x, area.y + area.h - franjaH, area.w, franjaH }, velo);

    i32 en = 3;
    while (en > 1 && font5x7::measureUtf8(g.name.c_str(), en) > area.w - 200) --en;
    font5x7::drawTextUtf8(fb, area.x + 14, area.y + area.h - franjaH + 8,
                          g.name.c_str(), kSteamText, en);

    std::string usd, mxn, eur;
    devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);
    const i32 pw = font5x7::measureUtf8(usd.c_str(), 2);
    // Precio en verde sobre su pastilla, al modo de las tiendas. El alto sale
    // de la letra, no de un 22 fijo: con la fuente del sistema el rotulo se
    // salia por abajo de la pastilla.
    const i32 pAlto = textr::glyphHeight(2) + 10;
    const Recti pastilla{ area.x + area.w - pw - 26,
                          area.y + area.h - franjaH + 10, pw + 18, pAlto };
    fb.fillRect(pastilla, kSteamDeal);
    font5x7::drawFitted(fb, pastilla, usd.c_str(), kSteamGreen, 2, 6);

    fb.drawRect(area, kSteamEdge, 1);

    // --- flechas, salidas de las paredes ---
    drawCarouselArrow(fb, m_carArrowL, false, m_carArrowLHover);
    drawCarouselArrow(fb, m_carArrowR, true,  m_carArrowRHover);
}

void HomeScene::drawStoreTopRated(Framebuffer& fb, Recti area) {
    if (area.w <= 0 || area.h <= 0) return;

    // El orden se recalcula si esta vacio o si quedo desfasado respecto al
    // catalogo: publicar un juego cambia el tamano y los indices viejos
    // apuntarian fuera.
    if (m_topOrder.size() != m_storeGames.size()) refreshTopRated();
    if (m_topOrder.empty()) {
        fb.fillRect(area, kSteamPanel);
        fb.drawRect(area, kSteamEdge, 1);
        return;
    }

    const i32 n = static_cast<i32>(m_topOrder.size());
    const i32 pos = ((m_topIndex % n) + n) % n;
    const i32 idx = m_topOrder[static_cast<size_t>(pos)];
    if (idx < 0 || idx >= static_cast<i32>(m_storeGames.size())) return;
    const devcatalog::Game& g = m_storeGames[static_cast<size_t>(idx)];

    if (m_topOf != g.name) {
        m_topOf = g.name;
        m_topPixels.clear();
        m_topW = m_topH = 0;
        if (!g.listing.art.empty()) {
            std::vector<u32> px; i32 w = 0, h = 0;
            if (photo::loadAnyImage(g.listing.art, px, w, h) && w > 0 && h > 0) {
                m_topPixels = std::move(px); m_topW = w; m_topH = h;
            }
        }
    }

    fb.fillRect(area, kSteamPanel);

    // Imagen a la izquierda, texto a la derecha.
    const i32 imgW = area.w * 55 / 100;
    const Recti img{ area.x, area.y, imgW, area.h };
    const Recti txt{ area.x + imgW + 12, area.y,
                     std::max(1, area.w - imgW - 12), area.h };

    const f32 s = m_topSlide;
    const f32 e = (s < 0.0f) ? -(1.0f - (1.0f + s) * (1.0f + s))
                             :  (1.0f - (1.0f - s) * (1.0f - s));
    const i32 desliz = static_cast<i32>(e * imgW);

    if (m_topW > 0 && m_topH > 0 && !m_topPixels.empty()) {
        for (i32 yy = 0; yy < img.h; ++yy) {
            for (i32 xx = 0; xx < img.w; ++xx) {
                const i32 dx = xx + desliz;
                if (dx < 0 || dx >= img.w) continue;
                const i32 ex = m_topW * 1000 / std::max(1, img.w);
                const i32 ey = m_topH * 1000 / std::max(1, img.h);
                const i32 esc = std::min(ex, ey);
                const i32 sx = (xx * esc) / 1000 +
                               (m_topW - (img.w * esc) / 1000) / 2;
                const i32 sy = (yy * esc) / 1000 +
                               (m_topH - (img.h * esc) / 1000) / 2;
                if (sx < 0 || sy < 0 || sx >= m_topW || sy >= m_topH) continue;
                fb.setPixel(img.x + dx, img.y + yy,
                            m_topPixels[static_cast<size_t>(sy) * m_topW + sx]);
            }
        }
    } else {
        fb.fillRect(img, kSteamBg);
    }

    // --- columna de texto: nombre, nota, descripcion y resenas ---
    i32 ty = txt.y + 8;

    i32 en = 2;
    while (en > 1 && font5x7::measureUtf8(g.name.c_str(), en) > txt.w - 8) --en;
    font5x7::drawTextUtf8(fb, txt.x, ty, g.name.c_str(), kSteamText, en);
    ty += textr::glyphHeight(en) + 6;

    // Nota media en estrellas, con el numero de resenas al lado.
    const reviews::Summary sum = reviews::summaryFor(kReviewsFile, g.name);
    if (sum.rated > 0) {
        const i32 llenas = sum.averageX100() / 100;
        for (i32 i = 0; i < 5; ++i)
            fb.fillRect({ txt.x + i * 12, ty + 1, 8, 8 },
                        i < llenas ? kSteamGreen : kSteamEdge);
        char nota[48];
        std::snprintf(nota, sizeof(nota), "%d.%02d  (%d resenas)",
                      sum.averageX100() / 100, sum.averageX100() % 100,
                      sum.count);
        font5x7::drawTextUtf8(fb, txt.x + 70, ty + 1, nota, kSteamDim, 1);
    } else {
        font5x7::drawTextUtf8(fb, txt.x, ty + 1, "Sin valoraciones aun",
                              kSteamDim, 1);
    }
    ty += 18;

    // Descripcion, partida a lo ancho de la columna.
    if (!g.listing.description.empty()) {
        const i32 porFila = std::max(1, (txt.w - 4) / 6);
        std::string linea;
        i32 escritas = 0;
        for (const char c : g.listing.description) {
            if (c == '\n' || static_cast<i32>(linea.size()) >= porFila) {
                if (ty + font5x7::kGlyphH > txt.y + txt.h - 60) break;
                font5x7::drawTextUtf8(fb, txt.x, ty, linea.c_str(), kSteamText, 1);
                ty += textr::glyphHeight(1) + 3;
                linea.clear();
                if (++escritas > 4) break;
                if (c == '\n') continue;
            }
            if (c != '\n') linea.push_back(c);
        }
        if (!linea.empty() && ty + font5x7::kGlyphH <= txt.y + txt.h - 60) {
            font5x7::drawTextUtf8(fb, txt.x, ty, linea.c_str(), kSteamText, 1);
            ty += textr::glyphHeight(1) + 3;
        }
    }

    // Una resena destacada: la mejor valorada de las que tenga.
    const std::vector<reviews::Review> revs =
        reviews::forGame(kReviewsFile, g.name);
    if (!revs.empty()) {
        const reviews::Review* mejor = &revs[0];
        for (const auto& r : revs) if (r.stars > mejor->stars) mejor = &r;

        ty += 4;
        font5x7::drawTextUtf8(fb, txt.x, ty, "LO QUE DICEN:", kSteamBlue, 1);
        ty += textr::glyphHeight(1) + 4;

        std::string cita = "\"" + mejor->text + "\"";
        for (char& c : cita) if (c == '\n') c = ' ';
        // El recorte reserva sitio para los tres puntos. Restar sin mas
        // daria un size_t gigantesco cuando el tope es menor que 3, y el
        // substr lanzaria: con una columna estrecha eso ocurre de verdad.
        const i32 porFila = std::max(1, (txt.w - 4) / 6);
        const i32 tope = porFila * 2;
        if (static_cast<i32>(cita.size()) > tope)
            cita = (tope > 3)
                 ? cita.substr(0, static_cast<size_t>(tope - 3)) + "..."
                 : cita.substr(0, static_cast<size_t>(tope));

        for (size_t i = 0; i < cita.size(); i += static_cast<size_t>(porFila)) {
            if (ty + font5x7::kGlyphH > txt.y + txt.h - 4) break;
            font5x7::drawTextUtf8(fb, txt.x, ty,
                                  cita.substr(i, static_cast<size_t>(porFila)).c_str(),
                                  kSteamDim, 1);
            ty += textr::glyphHeight(1) + 3;
        }
    }

    fb.drawRect(area, kSteamEdge, 1);

    drawCarouselArrow(fb, m_topArrowL, false, m_topArrowLHover);
    drawCarouselArrow(fb, m_topArrowR, true,  m_topArrowRHover);
}

void HomeScene::drawStoreStrips(Framebuffer& fb, Recti area, i32 yBase) {
    const char* titulos[kStripCount] = {
        "PROXIMAMENTE", "OFERTAS", "RECIEN PUBLICADOS"
    };

    const i32 hueco = 12;
    const i32 anchoCol = std::max(1, (area.w - hueco * 2) / kStripCount);
    const i32 altoCol  = 132;

    for (i32 s = 0; s < kStripCount; ++s) {
        const Recti r{ area.x + s * (anchoCol + hueco), yBase,
                       anchoCol, altoCol };
        m_stripRect[s] = r;

        fb.fillRect(r, kPanelViolet);
        fb.drawRect(r, kPanelEdge, 1);
        font5x7::drawTextUtf8(fb, r.x + 8, r.y + 8, titulos[s], kAccent, 1);

        const std::vector<i32> juegos = stripGames(s);
        if (juegos.empty()) {
            const char* v = "(sin juegos)";
            const i32 w = font5x7::measureUtf8(v, 1);
            if (w < r.w - 10)
                font5x7::drawTextUtf8(fb, r.x + (r.w - w) / 2,
                                      r.y + r.h / 2, v, kDim, 1);
            continue;
        }

        // Cada juego, una casilla pulsable que lleva a su ficha. Se apuntan
        // como tarjetas normales para reutilizar el hit-test que ya existe.
        const i32 filaH = 26;
        const i32 caben = std::max(1, (r.h - 26) / filaH);
        for (i32 i = 0; i < static_cast<i32>(juegos.size()) && i < caben; ++i) {
            const i32 jg = juegos[static_cast<size_t>(i)];
            const Recti casilla{ r.x + 6, r.y + 24 + i * filaH,
                                 std::max(1, r.w - 12), filaH - 4 };

            const i32 miCarta = static_cast<i32>(m_storeCards.size());
            m_storeCards.push_back(StoreCard{ casilla, jg });
            const bool hov = (m_storeHover == miCarta);

            fb.fillRect(casilla, hov ? Color::fromHex(0x3A2570) : kPanelDeep);
            if (hov) fb.drawRect(casilla, kScrollThumb, 1);

            const auto& g = m_storeGames[static_cast<size_t>(jg)];
            std::string usd, mxn, eur;
            devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);

            // Nombre a la izquierda, precio a la derecha; el nombre se
            // recorta si no cabe, para que el precio no se pierda.
            const i32 pw = font5x7::measureUtf8(usd.c_str(), 1);
            const i32 libre = casilla.w - pw - 16;
            std::string nom = g.name;
            while (!nom.empty() && font5x7::measureUtf8(nom.c_str(), 1) > libre)
                nom.pop_back();

            font5x7::drawTextUtf8(fb, casilla.x + 6, casilla.y + 6,
                                  nom.c_str(), hov ? kWhite : kText, 1);
            font5x7::drawTextUtf8(fb, casilla.x + casilla.w - pw - 6,
                                  casilla.y + 6, usd.c_str(), kOk, 1);
        }
    }
}

void HomeScene::drawStoreLists(Framebuffer& fb, Recti area, i32 yBase) {
    // Dos listas largas, una sobre otra: ofertas y mejor valorados. Filas
    // estrechas con imagen pequena, nombre, nota y precio -- la forma en que
    // una tienda ensena muchos juegos sin ocupar toda la pantalla.
    struct Lista { const char* titulo; bool porNota; };
    const Lista listas[2] = {
        { "OFERTAS ESPECIALES", false },
        { "MEJOR VALORADOS",    true  },
    };

    // Igual que en los destacados: si el catalogo cambio, el orden por
    // valoracion se rehace antes de usarlo.
    if (m_topOrder.size() != m_storeGames.size()) refreshTopRated();

    i32 y = yBase;
    for (const auto& L : listas) {
        font5x7::drawTextUtf8(fb, area.x, y, L.titulo, kSteamText, 2);
        // El salto es el alto REAL del titulo mas su aire. Con kGlyphH (los
        // 7 px de la fuente bitmap) el rotulo se quedaba pegado a la primera
        // fila de la lista.
        y += textr::glyphHeight(2) + 10;

        // Los indices salen del orden ya calculado o del precio.
        //
        // OJO: m_topOrder se calculo con el catalogo que hubiera entonces. Si
        // el catalogo cambio despues (se publico un juego, o se recargo), sus
        // indices pueden apuntar fuera del vector actual. Se filtran aqui en
        // vez de confiar en que esten al dia.
        std::vector<i32> orden;
        if (L.porNota) {
            for (const i32 i : m_topOrder)
                if (i >= 0 && i < static_cast<i32>(m_storeGames.size()))
                    orden.push_back(i);
        } else {
            for (i32 i = 0; i < static_cast<i32>(m_storeGames.size()); ++i)
                orden.push_back(i);
            std::sort(orden.begin(), orden.end(), [&](i32 a, i32 b) {
                return m_storeGames[static_cast<size_t>(a)].price.usdCents <
                       m_storeGames[static_cast<size_t>(b)].price.usdCents;
            });
        }

        constexpr i32 kFilaH = 44;
        for (i32 i = 0; i < static_cast<i32>(orden.size()) && i < 5; ++i) {
            const i32 jg = orden[static_cast<size_t>(i)];
            const auto& g = m_storeGames[static_cast<size_t>(jg)];
            const Recti fila{ area.x, y, area.w, kFilaH - 4 };

            const i32 miCarta = static_cast<i32>(m_storeCards.size());
            m_storeCards.push_back(StoreCard{ fila, jg });
            const bool hov = (m_storeHover == miCarta);

            fb.fillRect(fila, hov ? kSteamPanelHi : kSteamPanel);

            // Miniatura a la izquierda: un bloque de color, que a este tamano
            // rinde mas que decodificar la imagen entera por fila.
            fb.fillRect({ fila.x + 4, fila.y + 4, 56, kFilaH - 12 }, kSteamBg);

            // Nombre y nota, uno sobre otro y centrados COMO PAR en la fila.
            // Antes iban a +8 y +22: catorce pixeles de salto para una letra
            // que mide quince, asi que el nombre y la nota se rozaban y el
            // conjunto quedaba bajo.
            const i32 lhFila  = textr::glyphHeight(1);
            const i32 altoPar = lhFila * 2 + 4;
            const i32 yNombre = fila.y + (fila.h - altoPar) / 2;
            const i32 yNota   = yNombre + lhFila + 4;

            font5x7::drawTextUtf8(fb, fila.x + 68, yNombre,
                                  g.name.c_str(), hov ? kWhite : kSteamText, 1);

            const reviews::Summary sm = reviews::summaryFor(kReviewsFile, g.name);
            if (sm.rated > 0) {
                char nota[32];
                std::snprintf(nota, sizeof(nota), "%d.%02d",
                              sm.averageX100() / 100, sm.averageX100() % 100);
                font5x7::drawTextUtf8(fb, fila.x + 68, yNota,
                                      nota, kSteamGreen, 1);
            } else {
                font5x7::drawTextUtf8(fb, fila.x + 68, yNota,
                                      "sin nota", kSteamDim, 1);
            }

            std::string usd, mxn, eur;
            devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);

            // La etiqueta se dimensiona a partir del TEXTO: ancho medido mas
            // un acolchado, y alto el de la letra mas otro tanto. Con la caja
            // fija de 18 px y el texto a +15, la letra se salia por abajo en
            // cuanto la fuente crecio.
            const i32 pw   = font5x7::measureUtf8(usd.c_str(), 1);
            const i32 alto = textr::glyphHeight(1) + 8;
            const Recti eti{ fila.x + fila.w - pw - 20,
                             fila.y + (fila.h - alto) / 2,
                             pw + 14, alto };
            fb.fillRect(eti, kSteamDeal);
            // drawFitted centra en los dos ejes y reduce la escala si hiciera
            // falta, asi que el rotulo nunca se sale de su recuadro.
            font5x7::drawFitted(fb, eti, usd.c_str(), kSteamGreen, 1, 4);

            y += kFilaH;
        }
        y += 18;
    }
}

std::vector<i32> HomeScene::gamesOfGenre(i32 g1, i32 g2) const {
    std::vector<i32> out;
    if (g1 < 0 || g1 >= kUpGenreCount) return out;

    // Las etiquetas se guardan en el texto del campo ("#ACCION #RPG"), asi
    // que pertenecer a un genero es contener su etiqueta. Se compara la
    // etiqueta COMPLETA: sin eso, "#RPG" tambien casaria con "#RPGTACTICO".
    const std::string e1 = uploadGenreLabel(g1);
    const std::string e2 = (g2 >= 0 && g2 < kUpGenreCount)
                         ? uploadGenreLabel(g2) : std::string();

    auto tiene = [](const std::string& txt, const std::string& et) {
        if (et.empty()) return true;
        size_t pos = txt.find(et);
        while (pos != std::string::npos) {
            const size_t fin = pos + et.size();
            const bool izq = (pos == 0) || txt[pos - 1] == ' ';
            const bool der = (fin >= txt.size()) || txt[fin] == ' ' ||
                             txt[fin] == '#';
            if (izq && der) return true;
            pos = txt.find(et, pos + 1);
        }
        return false;
    };

    for (i32 i = 0; i < static_cast<i32>(m_storeGames.size()); ++i) {
        const std::string& gen = m_storeGames[static_cast<size_t>(i)].listing.genres;
        // En una combinacion hacen falta LOS DOS generos, no cualquiera.
        if (tiene(gen, e1) && (e2.empty() || tiene(gen, e2))) out.push_back(i);
    }

    // Del PEOR al MEJOR valorado, que es el orden que se pidio: la lista se
    // recorre hacia abajo y termina en lo mejor.
    const auto resumenes = reviews::summaryAll(kReviewsFile);
    auto nota = [&](i32 idx) {
        const std::string& n = m_storeGames[static_cast<size_t>(idx)].name;
        for (const auto& par : resumenes)
            if (par.first == n) return par.second.averageX100();
        return 0;
    };
    std::sort(out.begin(), out.end(),
              [&](i32 a, i32 b) { return nota(a) < nota(b); });
    return out;
}

void HomeScene::openGenreSection(i32 g1, i32 g2) {
    if (g1 < 0 || g1 >= kUpGenreCount) return;
    m_genreOpen  = g1;
    m_genreOpen2 = (g2 >= 0 && g2 < kUpGenreCount) ? g2 : -1;
    m_section    = kGenreSection;
    m_genrePanel.setOffset(0);
}

i32 HomeScene::drawStoreGenres(Framebuffer& fb, Recti area, i32 yBase) {
    m_genreBtns.clear();
    m_comboBtns.clear();

    // --- lista 1: un boton por genero ---
    font5x7::drawTextUtf8(fb, area.x, yBase, "EXPLORAR POR GENERO",
                          kSteamText, 2);
    i32 y = yBase + textr::glyphHeight(2) + 12;

    constexpr i32 kBtnH = 30, kBtnGap = 8;
    const i32 cols = std::max(1, area.w / 170);
    const i32 btnW = (area.w - kBtnGap * (cols - 1)) / cols;

    for (i32 i = 0; i < kUpGenreCount; ++i) {
        const i32 fila = i / cols, col = i % cols;
        const Recti r{ area.x + col * (btnW + kBtnGap),
                       y + fila * (kBtnH + kBtnGap), btnW, kBtnH };
        m_genreBtns.push_back(r);

        const bool hov = (m_genreHover == i && !m_genreHoverCombo);
        fb.fillRect(r, hov ? kSteamPanelHi : kSteamPanel);
        fb.drawRect(r, hov ? kSteamBlue : kSteamEdge, 1);

        const std::string et = uploadGenreLabel(i);
        font5x7::drawFitted(fb, r, et.c_str(),
                            hov ? kWhite : kSteamText, 1, 5);
    }

    const i32 filas1 = (kUpGenreCount + cols - 1) / cols;
    y += filas1 * (kBtnH + kBtnGap) + 24;

    // --- lista 2: combinaciones de dos generos ---
    // No se generan las 1225 parejas posibles: serian ilegibles. Se toman
    // las que de verdad se usan juntas, encadenando cada genero con el
    // siguiente, que da una lista larga pero recorrible.
    font5x7::drawTextUtf8(fb, area.x, y, "COMBINACIONES DE GENEROS",
                          kSteamText, 2);
    y += textr::glyphHeight(2) + 12;

    const i32 colsC = std::max(1, area.w / 230);
    const i32 btnWC = (area.w - kBtnGap * (colsC - 1)) / colsC;

    for (i32 i = 0; i < kUpGenreCount; ++i) {
        const i32 j = (i + 1) % kUpGenreCount;   // pareja con el siguiente
        const i32 fila = i / colsC, col = i % colsC;
        const Recti r{ area.x + col * (btnWC + kBtnGap),
                       y + fila * (kBtnH + kBtnGap), btnWC, kBtnH };
        m_comboBtns.push_back(r);

        const bool hov = (m_genreHover == i && m_genreHoverCombo);
        fb.fillRect(r, hov ? kSteamPanelHi : kSteamPanel);
        fb.drawRect(r, hov ? kSteamBlue : kSteamEdge, 1);

        const std::string et = uploadGenreLabel(i) + " + " + uploadGenreLabel(j);
        font5x7::drawFitted(fb, r, et.c_str(),
                            hov ? kWhite : kSteamDim, 1, 5);
    }

    const i32 filas2 = (kUpGenreCount + colsC - 1) / colsC;
    return y + filas2 * (kBtnH + kBtnGap) + 20;
}

void HomeScene::updateGenreSection(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    m_genreBackHover = (m_genreBackRect.w > 0 &&
                        lx >= m_genreBackRect.x &&
                        lx < m_genreBackRect.x + m_genreBackRect.w &&
                        ly >= m_genreBackRect.y &&
                        ly < m_genreBackRect.y + m_genreBackRect.h);

    if (in.mouseReleased(0) && m_genreBackHover) {
        m_section = kStoreSection;
        m_genreOpen = m_genreOpen2 = -1;
        return;
    }

    m_genrePanel.update(engine);
}

// ===========================================================================
//  FICHA PUBLICA DE UN CREADOR
//
//  Se abre al pulsar su tarjeta en la tienda. Muestra lo que cualquiera puede
//  ver de una cuenta: foto, nombre, antiguedad y seguidores. La descripcion
//  NO va aqui -- era lo unico que se ensenaba antes, en una linea suelta, y
//  el encargo es justamente darle una seccion propia.
// ===========================================================================

i32 HomeScene::followerCount(const std::string& email) const {
    // Solo existe la cuenta local, asi que el unico seguidor posible es el
    // propio usuario. Cuando haya mas cuentas, esto leera el registro de
    // seguimientos de todas; la interfaz no cambia.
    i32 n = 0;
    for (const auto& e : m_following) if (e == email) ++n;
    return n;
}

void HomeScene::drawCreatorSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 h   = std::max(0, fb.height() - top);
    fb.fillRect({ 0, top, fb.width(), h }, kSteamBg);

    const Recti area{ 24, top + 20, std::max(1, fb.width() - 48),
                      std::max(1, h - 40) };

    // --- volver ---
    m_creatorBackRect = Recti{ area.x, area.y, 150, 32 };
    fb.fillRect(m_creatorBackRect,
                m_creatorBackHover ? kSteamPanelHi : kSteamPanel);
    fb.drawRect(m_creatorBackRect, kSteamEdge, 1);
    font5x7::drawFitted(fb, m_creatorBackRect, "< VOLVER", kSteamText, 1, 6);

    // Perfil del creador. Hoy solo existe la cuenta local, asi que se lee el
    // perfil propio; con mas cuentas, se leeria el del correo pedido.
    Profile p;
    Profile::load(kProfileFile, p);

    // Color propio de la cuenta, derivado del correo: el mismo que usa su
    // tarjeta en la tienda, para que se reconozca al abrirla.
    u32 semilla = 0;
    for (const char c : m_creatorOpen) semilla = semilla * 131u + static_cast<u8>(c);
    const Color banda{ static_cast<u8>(60 + (semilla >> 16) % 120),
                       static_cast<u8>(60 + (semilla >> 8)  % 120),
                       static_cast<u8>(60 +  semilla        % 120) };

    // --- ficha ---
    const i32 fichaY = m_creatorBackRect.y + m_creatorBackRect.h + 18;
    const Recti ficha{ area.x, fichaY, std::min(area.w, 620),
                       std::max(1, std::min(area.h - 70, 300)) };
    fb.fillRect(ficha, kSteamPanel);
    fb.fillRect({ ficha.x, ficha.y, ficha.w, 8 }, banda);
    fb.drawRect(ficha, kSteamEdge, 1);

    // Foto, circular arriba a la izquierda.
    if (!m_creatorPhotoTried) {
        m_creatorPhotoTried = true;
        photo::loadBmp(kPhotoFile, m_creatorPhoto,
                       m_creatorPhotoW, m_creatorPhotoH);
    }
    const i32 rad = 48;
    const i32 cx  = ficha.x + 24 + rad, cy = ficha.y + 28 + rad;
    if (m_creatorPhotoW > 0 && !m_creatorPhoto.empty())
        fb.blitCircular(cx, cy, rad, m_creatorPhoto.data(),
                        m_creatorPhotoW, m_creatorPhotoH);
    else
        fb.fillCircle(cx, cy, rad, banda);
    fb.drawCircle(cx, cy, rad, kSteamEdge, 2);

    // --- datos, a la derecha de la foto ---
    const i32 dx = cx + rad + 24;
    i32 dy = ficha.y + 30;
    const i32 anchoDatos = std::max(1, ficha.x + ficha.w - dx - 20);

    const std::string nombre = p.userName.empty() ? m_creatorOpen : p.userName;
    font5x7::drawFitted(fb, { dx, dy, anchoDatos, textr::glyphHeight(3) + 4 },
                        nombre.c_str(), kWhite, 3, 0);
    dy += textr::glyphHeight(3) + 10;

    font5x7::drawFitted(fb, { dx, dy, anchoDatos, textr::glyphHeight(1) + 2 },
                        m_creatorOpen.c_str(), kSteamDim, 1, 0);
    dy += textr::glyphHeight(1) + 16;

    // Antiguedad. Sin fecha registrada se dice, en vez de inventar una.
    char linea[160];
    if (p.createdAt > 0) {
        const std::time_t t = static_cast<std::time_t>(p.createdAt);
        std::tm tm{};
        if (localtime_s(&tm, &t) == 0)
            std::snprintf(linea, sizeof(linea), "En Ludora desde %02d/%02d/%04d",
                          tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
        else
            std::snprintf(linea, sizeof(linea), "Fecha de alta no disponible");
    } else {
        std::snprintf(linea, sizeof(linea), "Fecha de alta no registrada");
    }
    font5x7::drawTextUtf8(fb, dx, dy, linea, kSteamText, 2);
    dy += textr::glyphHeight(2) + 10;

    const i32 segs = followerCount(m_creatorOpen);
    std::snprintf(linea, sizeof(linea), "%d seguidor%s", segs,
                  segs == 1 ? "" : "es");
    font5x7::drawTextUtf8(fb, dx, dy, linea, kSteamGreen, 2);
    dy += textr::glyphHeight(2) + 10;

    // Cuantos juegos tiene publicados: es el dato que dice si merece la pena
    // seguirle, y ya esta cargado.
    i32 juegos = 0;
    for (const auto& g : m_storeGames) { (void)g; ++juegos; }
    std::snprintf(linea, sizeof(linea), "%d juego%s en la tienda", juegos,
                  juegos == 1 ? "" : "s");
    font5x7::drawTextUtf8(fb, dx, dy, linea, kSteamText, 1);

    // --- UNIRSE A COMUNIDAD ---
    const i32 bw = std::min(280, ficha.w - 48);
    m_creatorJoinRect = Recti{ ficha.x + 24,
                               ficha.y + ficha.h - 54, bw, 38 };
    fb.fillRect(m_creatorJoinRect,
                m_creatorJoinDown ? kSteamEdge
                : (m_creatorJoinHover ? kFollowBlue : kSteamPanelHi));
    fb.drawRect(m_creatorJoinRect, kFollowBlue, 2);
    font5x7::drawFitted(fb, m_creatorJoinRect, "UNIRSE A COMUNIDAD",
                        m_creatorJoinHover ? kBg : kWhite, 2, 8);
}

void HomeScene::updateCreatorSection(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();
    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);
    auto en = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    m_creatorBackHover = en(m_creatorBackRect);
    m_creatorJoinHover = en(m_creatorJoinRect);
    if (in.mousePressed(0) && m_creatorJoinHover) m_creatorJoinDown = true;

    if (in.mouseReleased(0)) {
        if (m_creatorBackHover) {
            m_section = kStoreSection;
        } else if (m_creatorJoinDown && m_creatorJoinHover) {
            m_section = kCommunitySection;
        }
        m_creatorJoinDown = false;
    }

    // ESC vuelve, igual que en el resto de secciones de pantalla completa.
    if (in.keyPressed(VK_ESCAPE)) m_section = kStoreSection;
}

void HomeScene::drawCommunitySection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 h   = std::max(0, fb.height() - top);
    fb.fillRect({ 0, top, fb.width(), h }, kSteamBg);

    const Recti area{ 24, top + 20, std::max(1, fb.width() - 48),
                      std::max(1, h - 40) };

    m_communityBackRect = Recti{ area.x, area.y, 150, 32 };
    fb.fillRect(m_communityBackRect,
                m_communityBackHover ? kSteamPanelHi : kSteamPanel);
    fb.drawRect(m_communityBackRect, kSteamEdge, 1);
    font5x7::drawFitted(fb, m_communityBackRect, "< VOLVER", kSteamText, 1, 6);

    // Titulo con el nombre del creador, para saber de que comunidad se trata.
    Profile p;
    Profile::load(kProfileFile, p);
    const std::string quien = p.userName.empty() ? m_creatorOpen : p.userName;

    char titulo[256];
    std::snprintf(titulo, sizeof(titulo), "COMUNIDAD DE %s", quien.c_str());
    const i32 ty = m_communityBackRect.y + m_communityBackRect.h + 24;
    font5x7::drawFitted(fb, { area.x, ty, area.w, textr::glyphHeight(3) + 4 },
                        titulo, kWhite, 3, 0);

    // La seccion esta VACIA a proposito: el encargo pedia el sitio, no el
    // contenido. Se dice en pantalla para que no parezca un fallo.
    const i32 my = ty + textr::glyphHeight(3) + 30;
    font5x7::drawFitted(fb, { area.x, my, area.w, textr::glyphHeight(2) + 4 },
                        "Esta comunidad aun no tiene contenido.", kSteamDim, 2, 0);
}

void HomeScene::updateCommunitySection(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();
    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    m_communityBackHover =
        m_communityBackRect.w > 0 &&
        lx >= m_communityBackRect.x &&
        lx < m_communityBackRect.x + m_communityBackRect.w &&
        ly >= m_communityBackRect.y &&
        ly < m_communityBackRect.y + m_communityBackRect.h;

    // Vuelve a la FICHA del creador, no a la tienda: es de donde se entro.
    if (in.mouseReleased(0) && m_communityBackHover)
        m_section = kCreatorSection;
    if (in.keyPressed(VK_ESCAPE)) m_section = kCreatorSection;
}

void HomeScene::drawGenreSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    fb.fillRect({0, top, fb.width(), fb.height() - top}, kSteamBg);

    if (m_genreOpen < 0) return;

    // Cabecera con el genero (o la pareja) y el boton de volver.
    std::string titulo = uploadGenreLabel(m_genreOpen);
    if (m_genreOpen2 >= 0) titulo += " + " + uploadGenreLabel(m_genreOpen2);
    font5x7::drawTextUtf8(fb, 24, top + 16, titulo.c_str(), kSteamBlue, 3);

    const char* volver = "< VOLVER A LA TIENDA";
    const i32 vw = font5x7::measureUtf8(volver, 1);
    m_genreBackRect = Recti{ fb.width() - vw - 30, top + 20, vw + 12, 20 };
    font5x7::drawTextUtf8(fb, m_genreBackRect.x + 6, m_genreBackRect.y + 6,
                          volver, m_genreBackHover ? kWhite : kSteamDim, 1);

    // --- lista de juegos del genero ---
    const i32 listaY = top + 60;
    m_genrePanel.setRect({ 24, listaY, fb.width() - 48,
                           std::max(1, fb.height() - listaY - 20) });

    const std::vector<i32> juegos = gamesOfGenre(m_genreOpen, m_genreOpen2);

    if (juegos.empty()) {
        // Vacio a proposito por ahora: no hay juegos con estas etiquetas. Se
        // dice cual es el motivo, que es mas util que una lista en blanco.
        const char* v1 = "Todavia no hay juegos en este genero.";
        const char* v2 = "Apareceran aqui cuando se publiquen con esta etiqueta.";
        const i32 w1 = font5x7::measureUtf8(v1, 2);
        const i32 w2 = font5x7::measureUtf8(v2, 1);
        font5x7::drawTextUtf8(fb, (fb.width() - w1) / 2,
                              listaY + 60, v1, kSteamDim, 2);
        font5x7::drawTextUtf8(fb, (fb.width() - w2) / 2,
                              listaY + 90, v2, kSteamEdge, 1);
        m_genrePanel.setContentHeight(0);
        return;
    }

    // Filas anchas: portada, nombre, nota y precio. Del peor al mejor.
    constexpr i32 kFilaH = 84;
    m_genrePanel.setContentHeight(
        static_cast<i32>(juegos.size()) * kFilaH + 10);
    m_genrePanel.renderBar(fb);

    const Recti cr = m_genrePanel.contentRect();
    for (size_t i = 0; i < juegos.size(); ++i) {
        const i32 y = cr.y + static_cast<i32>(i) * kFilaH -
                      m_genrePanel.offset();
        if (y + kFilaH <= cr.y || y >= cr.y + cr.h) continue;

        const i32 jg = juegos[i];
        const auto& g = m_storeGames[static_cast<size_t>(jg)];
        const Recti fila{ cr.x, y, cr.w, kFilaH - 8 };

        const i32 miCarta = static_cast<i32>(m_storeCards.size());
        m_storeCards.push_back(StoreCard{ fila, jg });
        const bool hov = (m_storeHover == miCarta);

        fb.fillRect(fila, hov ? kSteamPanelHi : kSteamPanel);
        fb.drawRect(fila, hov ? kSteamBlue : kSteamEdge, 1);

        fb.fillRect({ fila.x + 6, fila.y + 6, 120, kFilaH - 20 }, kSteamBg);

        font5x7::drawTextUtf8(fb, fila.x + 136, fila.y + 12,
                              g.name.c_str(), hov ? kWhite : kSteamText, 2);

        const reviews::Summary sm = reviews::summaryFor(kReviewsFile, g.name);
        char nota[48];
        if (sm.rated > 0)
            std::snprintf(nota, sizeof(nota), "%d.%02d  (%d resenas)",
                          sm.averageX100() / 100, sm.averageX100() % 100,
                          sm.count);
        else
            std::snprintf(nota, sizeof(nota), "sin valoraciones");
        font5x7::drawTextUtf8(fb, fila.x + 136, fila.y + 36, nota,
                              sm.rated > 0 ? kSteamGreen : kSteamDim, 1);

        std::string usd, mxn, eur;
        devcatalog::formatPrice(g.price.usdCents, usd, mxn, eur);
        // Igual que en las listas: la caja la marca el texto y el rotulo se
        // centra dentro, en vez de colocarlo con numeros a ojo.
        const i32 pw   = font5x7::measureUtf8(usd.c_str(), 2);
        const i32 alto = textr::glyphHeight(2) + 10;
        const Recti eti{ fila.x + fila.w - pw - 26,
                         fila.y + (fila.h - alto) / 2,
                         pw + 18, alto };
        fb.fillRect(eti, kSteamDeal);
        font5x7::drawFitted(fb, eti, usd.c_str(), kSteamGreen, 2, 6);
    }
}

void HomeScene::drawStoreCreators(Framebuffer& fb, Recti area, i32 yBase) {
    ensureFollowingLoaded();

    font5x7::drawTextUtf8(fb, area.x, yBase, "CREADORES A LOS QUE SEGUIR",
                          kSteamText, 2);
    const i32 y = yBase + textr::glyphHeight(2) + 10;

    // Una sola cuenta local, asi que el unico creador es el propio usuario.
    // La tarjeta esta hecha para repetirse: cuando existan mas cuentas, se
    // dibuja una por creador sin tocar nada de esto.
    const i32 alto = 150;
    m_creatorRect = Recti{ area.x, y, std::min(area.w, 560), alto };
    const Recti r = m_creatorRect;

    // Color personalizable de la cuenta: se deriva del correo para que cada
    // creador tenga el suyo y sea estable entre sesiones.
    u32 semilla = 0;
    for (const char c : m_email) semilla = semilla * 131u + static_cast<u8>(c);
    const Color banda{ static_cast<u8>(60 + (semilla >> 16) % 120),
                       static_cast<u8>(60 + (semilla >> 8)  % 120),
                       static_cast<u8>(60 +  semilla        % 120) };

    // Con el cursor encima se aclara y el marco se refuerza: la tarjeta abre
    // la ficha publica del creador, y tiene que verse que es pulsable.
    fb.fillRect(r, m_creatorHover ? kSteamPanelHi : kSteamPanel);
    fb.fillRect({ r.x, r.y, r.w, 6 }, banda);   // franja de color arriba
    fb.drawRect(r, m_creatorHover ? kWhite : kSteamEdge, m_creatorHover ? 2 : 1);

    // Foto de perfil, circular a la izquierda.
    if (!m_creatorPhotoTried) {
        m_creatorPhotoTried = true;
        photo::loadBmp(kPhotoFile, m_creatorPhoto,
                       m_creatorPhotoW, m_creatorPhotoH);
    }
    const i32 rad = 34;
    const i32 cx = r.x + 16 + rad, cy = r.y + 22 + rad;
    if (m_creatorPhotoW > 0 && !m_creatorPhoto.empty()) {
        fb.blitCircular(cx, cy, rad, m_creatorPhoto.data(),
                        m_creatorPhotoW, m_creatorPhotoH);
    } else {
        fb.fillCircle(cx, cy, rad, banda);
    }
    fb.drawCircle(cx, cy, rad, kSteamEdge, 2);

    // Nombre, descripcion y juegos publicados.
    const i32 tx = r.x + 16 + rad * 2 + 14;
    const std::string nombre = m_profile.userName.empty() ? m_email
                                                          : m_profile.userName;
    font5x7::drawTextUtf8(fb, tx, r.y + 18, nombre.c_str(), kSteamText, 2);

    std::string desc = m_profile.description;
    for (char& c : desc) if (c == '\n') c = ' ';
    if (desc.empty()) desc = "Sin descripcion.";
    // Mismo cuidado que arriba: con la tarjeta estrecha, maxDesc puede ser
    // menor que los tres puntos que se le anaden.
    const i32 maxDesc = std::max(1, (r.x + r.w - tx - 12) / 6);
    if (static_cast<i32>(desc.size()) > maxDesc)
        desc = (maxDesc > 3)
             ? desc.substr(0, static_cast<size_t>(maxDesc - 3)) + "..."
             : desc.substr(0, static_cast<size_t>(maxDesc));
    font5x7::drawTextUtf8(fb, tx, r.y + 40, desc.c_str(), kSteamDim, 1);

    // Juegos del creador, del primero al mas reciente.
    i32 ly = r.y + 58;
    font5x7::drawTextUtf8(fb, tx, ly, "JUEGOS PUBLICADOS:", kSteamBlue, 1);
    ly += textr::glyphHeight(1) + 4;

    const auto propios = devcatalog::load(kDevCatalogFile);
    if (propios.empty()) {
        font5x7::drawTextUtf8(fb, tx, ly, "(ninguno todavia)", kSteamDim, 1);
    } else {
        for (size_t i = 0; i < propios.size() && i < 3; ++i) {
            char linea[128];
            std::snprintf(linea, sizeof(linea), "%s  -  %zu version(es)",
                          propios[i].name.c_str(), propios[i].versions.size());
            font5x7::drawTextUtf8(fb, tx, ly, linea, kSteamText, 1);
            ly += textr::glyphHeight(1) + 3;
        }
    }

    // --- boton SEGUIR, abajo a la derecha de la tarjeta ---
    const bool sigo = isFollowing(m_email);
    const i32 bw = 120, bh = 28;
    m_followRect = Recti{ r.x + r.w - bw - 12, r.y + r.h - bh - 12, bw, bh };

    const Color fondo = sigo ? kFollowBlue
                             : (m_followHover ? kSteamPanelHi : kSteamBg);
    fb.fillRect(m_followRect, m_followDown ? kSteamEdge : fondo);
    fb.drawRect(m_followRect, sigo ? kWhite : kSteamEdge, 1);

    const char* txt = sigo ? "Siguiendo :)" : "Seguir +";
    const i32 tw = font5x7::measureUtf8(txt, 1);
    font5x7::drawTextUtf8(fb, m_followRect.x + (bw - tw) / 2,
                          m_followRect.y + (bh - textr::glyphHeight(1)) / 2,
                          txt, sigo ? kSteamHeader : kSteamText, 1);
}

void HomeScene::drawStore(Engine& engine, Framebuffer& fb) {
    (void)engine;
    const Recti c   = m_storePanel.contentRect();
    const i32   off = m_storePanel.offset();
    if (c.w <= 0 || c.h <= 0) { m_storePanel.renderBar(fb); return; }

    // Catalogo de la tienda, cargado una vez y refrescado al publicar.
    //
    // Aqui se inyectaba Voxel World como primer juego, con su ficha cableada
    // en codigo. Se retiro: la tienda muestra EXCLUSIVAMENTE lo que hay en el
    // catalogo, sin casos especiales. Un juego de demostracion metido a mano
    // tapaba el caso real -- una tienda vacia -- y ataba la plataforma a una
    // ruta concreta de un disco concreto.
    if (!m_storeLoaded) {
        m_storeGames.clear();

        for (auto& g : devcatalog::load(kDevCatalogFile))
            m_storeGames.push_back(std::move(g));

        m_storeLoaded = true;
    }

    // Las tarjetas se vuelven a apuntar en cada frame: su posicion depende
    // del scroll y del tamano de la ventana.
    m_storeCards.clear();

    /// Dibuja una tarjeta recortada al area visible del panel. `escala` ajusta
    /// el tamano del titulo, mas grande en el destacado.
    auto tarjeta = [&](i32 x, i32 y, i32 w, i32 h, i32 num, i32 escala) {
        // Sin clip por region en el framebuffer, el recorte se hace aqui: una
        // tarjeta desplazada invadiria el menu de arriba.
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 <= y0 || w <= 0) return;

        // Se apunta la tarjeta para el hit-test del frame siguiente: su sitio
        // depende del scroll, asi que solo se conoce al dibujar. El indice de
        // juego se resuelve igual que el rotulo, mas abajo.
        // Correspondencia directa: la tarjeta `num` muestra el juego `num-1`
        // del catalogo. El destacado (num 1) es el primero que haya publicado,
        // sin ninguno fijo. Las tarjetas que sobran no tienen juego detras.
        const i32 idxJuego = (num - 1 < static_cast<i32>(m_storeGames.size()))
                           ? num - 1 : -1;
        const i32 miCarta = static_cast<i32>(m_storeCards.size());
        m_storeCards.push_back(StoreCard{ Recti{ x, y0, w, y1 - y0 }, idxJuego });

        // Realce suave al pasar el cursor: un violeta un punto mas claro. Muy
        // poco a proposito -- tiene que notarse que la tarjeta responde, sin
        // que parezca que ya esta pulsada.
        const bool hov = (m_storeHover == miCarta);
        const Color fondo = hov ? Color::fromHex(0x3A2570) : kPanelViolet;
        fb.fillRect({x, y0, w, y1 - y0}, fondo);

        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 3}, hov ? kScrollThumb : kPanelEdge);

        // Con el raton encima, un marco fino remata el realce.
        if (hov) fb.drawRect({x, y0, w, y1 - y0}, kScrollThumb, 1);

        // Los huecos los ocupan los juegos PUBLICADOS, en orden de catalogo;
        // los que sobran siguen siendo articulos genericos hasta que se
        // publique mas.
        // El rotulo sale del MISMO indice que usa el hit-test: si se
        // calcularan por separado, una tarjeta podria decir un nombre y abrir
        // otro juego.
        char label[128];
        bool publicado = false;
        i32  precioCents = 0;

        // Sin juego detras no hay tarjeta que dibujar: los huecos de relleno
        // numerados se eliminaron, asi que esto solo pasa si el catalogo
        // cambio entre el layout y el dibujado.
        if (idxJuego < 0) return;

        const auto& g = m_storeGames[static_cast<size_t>(idxJuego)];
        std::snprintf(label, sizeof(label), "%s", g.name.c_str());
        publicado   = true;
        precioCents = g.price.usdCents;

        // Reducir la escala hasta que el titulo quepa: con tarjetas estrechas
        // una escala fija desbordaria el borde derecho.
        const i32 margen = std::min<i32>(24, w / 8);
        i32 esc = escala;
        while (esc > 1 && font5x7::measureUtf8(label, esc) > w - margen * 2)
            --esc;

        const i32 ty = y + 24;
        if (ty >= c.y && ty + font5x7::kGlyphH * esc <= c.y + c.h)
            font5x7::drawTextUtf8(fb, x + margen, ty, label,
                                  publicado ? kWhite : kText, esc);

        // Subtitulo: el PRECIO si el juego esta publicado; si no, el tamano
        // de la tarjeta, que es lo que habia como relleno.
        char sub[64];
        if (publicado) {
            std::string usd, mxn, eur;
            devcatalog::formatPrice(precioCents, usd, mxn, eur);
            std::snprintf(sub, sizeof(sub), "%s", usd.c_str());
        } else {
            std::snprintf(sub, sizeof(sub), "%d x %d", w, h);
        }

        const i32 sy = y + 24 + textr::glyphHeight(esc) + 12;
        if (sy >= c.y && sy + font5x7::kGlyphH <= c.y + c.h &&
            font5x7::measureUtf8(sub, 1) <= w - margen * 2)
            font5x7::drawTextUtf8(fb, x + margen, sy, sub,
                                  publicado ? kOk : kDim, 1);
    };

    // --- seccion 1: banner de noticias, arriba del todo ---
    const i32 anchoUtil = std::min(kStoreHeroW, c.w - 16);
    const i32 bannerX = c.x + (c.w - anchoUtil) / 2;
    const i32 bannerH = 190;
    m_bannerRect = Recti{ bannerX, c.y + kStoreTopPad - off,
                          anchoUtil, bannerH };
    drawStoreBanner(fb, m_bannerRect);

    // --- seccion 2: las tres mini-secciones, justo debajo ---
    drawStoreStrips(fb, Recti{ bannerX, 0, anchoUtil, 0 },
                    m_bannerRect.y + bannerH + 14);

    // --- seccion 3: carrusel con flechas ---
    const i32 carY = m_bannerRect.y + bannerH + 14 + 132 + 24;
    const i32 carH = 240;
    m_carRect = Recti{ bannerX, carY, anchoUtil, carH };

    // Las flechas salen de las paredes: media pastilla fuera del cuadro, que
    // es como se ven en las tiendas al uso.
    const i32 flechaW = 30, flechaH = 62;
    m_carArrowL = Recti{ bannerX - flechaW / 2, carY + (carH - flechaH) / 2,
                         flechaW, flechaH };
    m_carArrowR = Recti{ bannerX + anchoUtil - flechaW / 2,
                         carY + (carH - flechaH) / 2, flechaW, flechaH };
    drawStoreCarousel(fb, m_carRect);

    // --- seccion 4: destacados y mejor valorados ---
    const i32 topY = carY + carH + 30;
    const i32 topH = 220;
    m_topRect = Recti{ bannerX, topY, anchoUtil, topH };
    m_topArrowL = Recti{ bannerX - flechaW / 2, topY + (topH - flechaH) / 2,
                         flechaW, flechaH };
    m_topArrowR = Recti{ bannerX + anchoUtil - flechaW / 2,
                         topY + (topH - flechaH) / 2, flechaW, flechaH };
    font5x7::drawTextUtf8(fb, bannerX, topY - 22,
                          "DESTACADOS Y MEJOR VALORADOS", kSteamText, 2);
    drawStoreTopRated(fb, m_topRect);

    // --- seccion 5: listas largas ---
    const i32 listY = topY + topH + 30;
    drawStoreLists(fb, Recti{ bannerX, 0, anchoUtil, 0 }, listY);

    // --- seccion 6: creadores, al final del scroll ---
    const i32 creY = listY + (textr::glyphHeight(2) + 8 + 44 * 5 + 18) * 2 + 10;
    drawStoreCreators(fb, Recti{ bannerX, 0, anchoUtil, 0 }, creY);

    // --- seccion 7: rejilla del catalogo completo ---
    // Solo juegos REALES: los huecos de relleno ("ARTICULO N") se fueron. Una
    // tienda con casillas vacias numeradas se lee como algo sin terminar; es
    // mejor mostrar lo que hay y dejar que la rejilla crezca sola.
    const i32 rejillaY = creY + 150 + 40;
    i32 trasRejilla = rejillaY;

    if (!m_storeGames.empty()) {
        font5x7::drawTextUtf8(fb, bannerX, rejillaY, "TODO EL CATALOGO",
                              kSteamText, 2);

        const i32 cols = std::max(1, anchoUtil / 300);
        const i32 colW = (anchoUtil - kStoreGap * (cols - 1)) / cols;
        const i32 filaH = 150;
        const i32 y0 = rejillaY + textr::glyphHeight(2) + 12;

        for (i32 i = 0; i < static_cast<i32>(m_storeGames.size()); ++i) {
            const i32 fila = i / cols, col = i % cols;
            tarjeta(bannerX + col * (colW + kStoreGap),
                    y0 + fila * (filaH + kStoreGap),
                    colW, filaH, i + 1, 2);
        }

        const i32 filas = (static_cast<i32>(m_storeGames.size()) + cols - 1) / cols;
        trasRejilla = y0 + filas * (filaH + kStoreGap) + 20;
    }

    // --- secciones 8 y 9: generos y combinaciones ---
    drawStoreGenres(fb, Recti{ bannerX, 0, anchoUtil, 0 }, trasRejilla);

    m_storePanel.renderBar(fb);
}

void HomeScene::saveProfile() {
    Profile::save(kProfileFile, m_profile);
}

void HomeScene::ensurePhotoLoaded() {
    if (m_photoTried) return;
    m_photoTried = true;
    if (m_profile.photoFile.empty()) return;

    // La foto ya se normalizo a BMP al importarla.
    if (!photo::loadBmp(kPhotoFile, m_photoPixels, m_photoW, m_photoH)) {
        m_photoPixels.clear();
        m_photoW = m_photoH = 0;
    }
}

void HomeScene::drawAvatar(Framebuffer& fb) {
    ensurePhotoLoaded();

    const i32 r  = kAvatarRadius;
    const i32 cx = fb.width()  - r - kAvatarMargin;
    const i32 cy = fb.height() - r - kAvatarMargin;

    // Fondo del disco: si no hay foto, queda como hueco con las iniciales.
    fb.fillCircle(cx, cy, r, kPanelDeep);

    if (!m_photoPixels.empty()) {
        // Se deja 3 px libres para que el anillo no tape la imagen.
        fb.blitCircular(cx, cy, r - 3, m_photoPixels.data(), m_photoW, m_photoH);
    } else {
        // Inicial del nombre de usuario como marcador de posicion.
        const char* u = m_profile.userName.c_str();
        char ini[2] = { u[0] ? u[0] : '?', '\0' };
        const i32 tw = font5x7::measure(ini, 3);
        font5x7::drawText(fb, cx - tw / 2, cy - font5x7::kGlyphH * 3 / 2,
                          ini, kTurquoise, 3);
    }

    // Anillo turquesa: doble linea, la exterior mas tenue, para que el borde
    // se lea como un aro y no como una silueta recortada.
    fb.drawCircle(cx, cy, r,     kTurquoise, 3);
    fb.drawCircle(cx, cy, r + 4, Color{kTurquoise.r, kTurquoise.g, kTurquoise.b, 90}, 1);
}

void HomeScene::layoutAccount(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    m_accountPanel.setRect({0, top, fbW, std::max(0, fbH - top)});
    m_accountPanel.setBarWidth(kScrollBarW);

    // Foto (ficha alta) + cuenta + contrasena + descripcion + cerrar sesion.
    //
    // La ficha de la descripcion CRECE con su contenido, asi que su alto se
    // estima igual que al dibujarla. Con un alto fijo, una descripcion larga
    // empujaba el boton de cerrar sesion fuera del recorrido del scroll.
    const i32 anchoFicha = std::min(kAccRowW, std::max(1, fbW - kAccPad * 2));
    const i32 porFila = std::max(1, (anchoFicha - 32) / (font5x7::kGlyphW + 1));
    const i32 lineaH  = textr::glyphHeight(1) + 4;

    i32 filas = 1;   // "(vacia)" ocupa una
    if (!m_profile.description.empty()) {
        // Estimacion por caracteres: no hace falta repetir el ajuste por
        // palabras, solo acotar el alto por arriba.
        filas = static_cast<i32>(
            (m_profile.description.size() + static_cast<size_t>(porFila) - 1) /
            static_cast<size_t>(porFila));
        filas = std::clamp(filas, 1, 12);   // mismo tope que al dibujar
    }
    const i32 altoDesc = std::max(kAccRowH, 30 + filas * lineaH + 10);

    const i32 total = kAccPad + kAccPhotoH + kAccPad +
                      2 * (kAccRowH + kAccPad) +   // cuenta y contrasena
                      altoDesc + kAccPad +
                      40 + kAccPad * 2;            // boton CERRAR SESION
    m_accountPanel.setContentHeight(total);
}

void HomeScene::drawAccount(Engine& engine, Framebuffer& fb) {
    (void)engine;
    const Recti c   = m_accountPanel.contentRect();
    const i32   off = m_accountPanel.offset();
    if (c.w <= 0 || c.h <= 0) { m_accountPanel.renderBar(fb); return; }

    const i32 w = std::min(kAccRowW, c.w - kAccPad * 2);
    const i32 x = c.x + (c.w - w) / 2;

    /// Ficha con etiqueta y valor, recortada al area visible.
    auto ficha = [&](i32 y, i32 h, const char* etiqueta, const char* valor,
                     bool oculto) -> Recti {
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 <= y0) return Recti{x, y, w, h};

        fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 2}, kPanelEdge);

        const i32 ey = y + 12;
        if (ey >= c.y && ey + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawText(fb, x + 16, ey, etiqueta, kDim, 1);

        const i32 vy = y + 30;
        if (vy >= c.y && vy + font5x7::kGlyphH * 2 <= c.y + c.h) {
            if (oculto) {
                // La contrasena no se muestra nunca, ni siquiera enmascarada
                // con su longitud real: eso ya filtraria informacion.
                font5x7::drawText(fb, x + 16, vy, "**********", kDim, 2);
            } else {
                font5x7::drawTextUtf8(fb, x + 16, vy, valor, kText, 2);
            }
        }
        return Recti{x, y, w, h};
    };

    i32 y = c.y + kAccPad - off;

    // --- 1) Nombre de usuario, con foto y botones al lado ---
    {
        const i32 h = kAccPhotoH;
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 > y0) {
            fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
            if (y >= c.y && y < c.y + c.h)
                fb.fillRect({x, y, w, 2}, kPanelEdge);
        }

        // Recuadro de la foto, a la izquierda de la ficha.
        m_photoRect = Recti{x + 16, y + 16, kAccPhotoSide, kAccPhotoSide};
        const i32 py0 = std::max(m_photoRect.y, c.y);
        const i32 py1 = std::min(m_photoRect.y + m_photoRect.h, c.y + c.h);
        if (py1 > py0) {
            fb.fillRect({m_photoRect.x, py0, m_photoRect.w, py1 - py0}, kPanelDeep);
            fb.drawRect({m_photoRect.x, m_photoRect.y, m_photoRect.w, m_photoRect.h},
                        kPanelEdge, 1);

            // Vista previa circular de la foto dentro del recuadro.
            ensurePhotoLoaded();
            if (!m_photoPixels.empty() &&
                m_photoRect.y >= c.y &&
                m_photoRect.y + m_photoRect.h <= c.y + c.h) {
                fb.blitCircular(m_photoRect.x + m_photoRect.w / 2,
                                m_photoRect.y + m_photoRect.h / 2,
                                m_photoRect.w / 2 - 4,
                                m_photoPixels.data(), m_photoW, m_photoH);
                fb.drawCircle(m_photoRect.x + m_photoRect.w / 2,
                              m_photoRect.y + m_photoRect.h / 2,
                              m_photoRect.w / 2 - 4, kTurquoise, 2);
            }

            if (m_profile.photoFile.empty()) {
                const char* sin = "SIN FOTO";
                const i32 sw = font5x7::measure(sin, 1);
                const i32 sy = m_photoRect.y + (m_photoRect.h - textr::glyphHeight(1)) / 2;
                if (sy >= c.y && sy + font5x7::kGlyphH <= c.y + c.h)
                    font5x7::drawText(fb, m_photoRect.x + (m_photoRect.w - sw) / 2,
                                      sy, sin, kDim, 1);
            }
        }

        // Nombre de usuario a la derecha de la foto.
        const i32 tx = m_photoRect.x + m_photoRect.w + 16;
        const i32 ey = y + 16;
        if (ey >= c.y && ey + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawText(fb, tx, ey, "NOMBRE DE USUARIO", kDim, 1);
        const i32 vy = y + 34;
        if (vy >= c.y && vy + font5x7::kGlyphH * 2 <= c.y + c.h)
            font5x7::drawTextUtf8(fb, tx, vy, m_profile.userName.c_str(), kText, 2);

        // Botones de foto: galeria y camara.
        const i32 by = y + 66;
        m_galleryRect = Recti{tx, by, kAccBtnW, kAccBtnH};
        m_cameraRect  = Recti{tx + kAccBtnW + 10, by, kAccBtnW, kAccBtnH};

        auto boton = [&](const Recti& r, const char* txt, i32 id) {
            if (r.y < c.y || r.y + r.h > c.y + c.h) return;
            const bool hov = (m_accountHover == id);
            const bool dwn = (m_accountDown == id && hov);
            fb.fillRect(r, dwn ? kClosePress : (hov ? kPanelEdge : kPanelDeep));
            fb.drawRect(r, hov ? kAccent : kPanelEdge, 1);
            const i32 tw = font5x7::measure(txt, 1);
            font5x7::drawText(fb, r.x + (r.w - tw) / 2,
                              r.y + (r.h - textr::glyphHeight(1)) / 2,
                              txt, hov ? kWhite : kGlyph, 1);
        };
        boton(m_galleryRect, "GALERIA", 0);
        boton(m_cameraRect,  "CAMARA",  1);

        // Boton cuadrado de la descripcion publica.
        m_descBtnRect = Recti{x + w - kAccDescBtn - 16, y + 16, kAccDescBtn, kAccDescBtn};
        if (m_descBtnRect.y >= c.y && m_descBtnRect.y + m_descBtnRect.h <= c.y + c.h) {
            const bool hov = (m_accountHover == 2);
            const bool dwn = (m_accountDown == 2 && hov);
            fb.fillRect(m_descBtnRect, dwn ? kClosePress : (hov ? kPanelEdge : kPanelDeep));
            fb.drawRect(m_descBtnRect, hov ? kAccent : kPanelEdge, 1);
            // Etiqueta en tres lineas para que quepa en un cuadrado.
            const char* l1 = "DESCRIPCION";
            const char* l2 = "DE CUENTA";
            const char* l3 = "EN PUBLICO";
            const i32 cy0 = m_descBtnRect.y + m_descBtnRect.h / 2 - 16;
            const Color col = hov ? kWhite : kGlyph;
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l1,1))/2, cy0,      l1, col, 1);
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l2,1))/2, cy0 + 11, l2, col, 1);
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l3,1))/2, cy0 + 22, l3, col, 1);
        }

        y += h + kAccPad;
    }

    // --- 2) Cuenta (correo) ---
    ficha(y, kAccRowH, "CUENTA", m_email.c_str(), false);
    y += kAccRowH + kAccPad;

    // --- 3) Contrasena, siempre oculta ---
    ficha(y, kAccRowH, "CONTRASENA", "", true);
    y += kAccRowH + kAccPad;

    // --- 4) Descripcion publica, ajustada a varias lineas ---
    // La descripcion admite 10.000 caracteres, asi que en una sola linea se
    // salia de la ficha y atravesaba la pantalla. Se parte al ancho del cuadro
    // y la ficha crece para contenerla.
    {
        const std::string& d = m_profile.description;
        const bool vacia = d.empty();

        // Escala 1: a escala 2 caben la mitad de caracteres por linea y una
        // descripcion normal se estiraria a demasiadas filas.
        constexpr i32 kEsc = 1;
        const i32 avance = (font5x7::kGlyphW + 1) * kEsc;
        const i32 anchoUtil = std::max(1, w - 32);
        const i32 porFila = std::max(1, anchoUtil / avance);
        const i32 lineaH = textr::glyphHeight(kEsc) + 4;

        // Se parte por PALABRAS, no por caracteres: cortar palabras a media
        // letra hace ilegible un texto de lectura.
        std::vector<std::string> lineas;
        if (vacia) {
            lineas.push_back("(vacia)");
        } else {
            std::string actual;
            std::string palabra;
            auto cierraLinea = [&]() {
                if (!actual.empty()) { lineas.push_back(actual); actual.clear(); }
            };
            auto metePalabra = [&]() {
                if (palabra.empty()) return;
                // Una palabra mas larga que la fila se corta: es la unica
                // forma de que quepa.
                while (static_cast<i32>(palabra.size()) > porFila) {
                    cierraLinea();
                    lineas.push_back(palabra.substr(0, static_cast<size_t>(porFila)));
                    palabra.erase(0, static_cast<size_t>(porFila));
                }
                if (static_cast<i32>(actual.size() + palabra.size() + 1) > porFila)
                    cierraLinea();
                if (!actual.empty()) actual += ' ';
                actual += palabra;
                palabra.clear();
            };

            for (const char ch : d) {
                if (ch == '\n') { metePalabra(); cierraLinea(); continue; }
                if (ch == ' ' || ch == '\t') { metePalabra(); continue; }
                palabra.push_back(ch);
            }
            metePalabra();
            cierraLinea();
            if (lineas.empty()) lineas.push_back("(vacia)");
        }

        // Tope de filas: una descripcion enorme no debe empujar el boton de
        // cerrar sesion fuera de la pantalla. El resto se marca con puntos.
        constexpr size_t kMaxLineas = 12;
        bool recortada = false;
        if (lineas.size() > kMaxLineas) {
            lineas.resize(kMaxLineas);
            recortada = true;
        }

        // El cuadro crece con el contenido, sin bajar del alto normal.
        const i32 alto = std::max<i32>(
            kAccRowH,
            30 + static_cast<i32>(lineas.size()) * lineaH + 10);

        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + alto, c.y + c.h);
        if (y1 > y0) {
            fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
            if (y >= c.y && y < c.y + c.h)
                fb.fillRect({x, y, w, 2}, kPanelEdge);

            const i32 ey = y + 12;
            if (ey >= c.y && ey + font5x7::kGlyphH <= c.y + c.h)
                font5x7::drawText(fb, x + 16, ey, "DESCRIPCION PUBLICA", kDim, 1);

            // Cada linea se recorta al area visible del panel: sin esto, al
            // desplazar se dibujarian sobre el menu de arriba.
            for (size_t i = 0; i < lineas.size(); ++i) {
                const i32 ly = y + 30 + static_cast<i32>(i) * lineaH;
                if (ly < c.y || ly + font5x7::kGlyphH * kEsc > c.y + c.h) continue;
                font5x7::drawTextUtf8(fb, x + 16, ly, lineas[i].c_str(),
                                      vacia ? kDim : kText, kEsc);
            }

            if (recortada) {
                const i32 ly = y + 30 + static_cast<i32>(lineas.size()) * lineaH;
                if (ly >= c.y && ly + font5x7::kGlyphH <= c.y + c.h)
                    font5x7::drawText(fb, x + 16, ly, "...", kDim, 1);
            }
        }

        y += alto + kAccPad;
    }

    // --- 5) Cerrar sesion ---
    // Deshace la sesion recordada: la proxima apertura pedira credenciales.
    m_logoutRect = Recti{x, y, std::min(240, w), 40};
    {
        const bool hov = (m_accountHover == 3);
        const bool dwn = (m_accountDown == 3 && hov);
        const i32 y0 = std::max(m_logoutRect.y, c.y);
        const i32 y1 = std::min(m_logoutRect.y + m_logoutRect.h, c.y + c.h);
        if (y1 > y0) {
            fb.fillRect({m_logoutRect.x, y0, m_logoutRect.w, y1 - y0},
                        dwn ? kClosePress : (hov ? kAccent : kFieldBg));
            fb.drawRect(m_logoutRect, hov ? kAccent : kBorder, 2);
            const char* t = "CERRAR SESION";
            const i32 tw = font5x7::measure(t, 1);
            const i32 ty = m_logoutRect.y + (m_logoutRect.h - textr::glyphHeight(1)) / 2;
            if (ty >= c.y && ty + font5x7::kGlyphH <= c.y + c.h)
                font5x7::drawText(fb, m_logoutRect.x + (m_logoutRect.w - tw) / 2, ty,
                                  t, hov ? kWhite : kDim, 1);
        }
    }

    // --- 6) Modo Desarrollador, junto a Cerrar sesion ---
    // Borde y texto laten entre turquesa y morado con una onda continua.
    m_devBtnRect = Recti{ m_logoutRect.x + m_logoutRect.w + 16, y,
                          std::min(260, w - m_logoutRect.w - 16), 40 };
    if (m_devBtnRect.w > 40) {
        const i32 y0 = std::max(m_devBtnRect.y, c.y);
        const i32 y1 = std::min(m_devBtnRect.y + m_devBtnRect.h, c.y + c.h);
        if (y1 > y0) {
            // Mezcla turquesa<->morado segun una onda seno del reloj.
            const f32 t = 0.5f + 0.5f * std::sin(static_cast<f32>(m_devAnimTime) * 2.2f);
            auto mix = [&](u8 a, u8 b) { return static_cast<u8>(a + (b - a) * t); };
            const Color animColor{ mix(kTurquoise.r, kDevPurple.r),
                                   mix(kTurquoise.g, kDevPurple.g),
                                   mix(kTurquoise.b, kDevPurple.b) };
            const bool hov = (m_accountHover == 4);
            const bool dwn = (m_accountDown == 4 && hov);

            fb.fillRect({m_devBtnRect.x, y0, m_devBtnRect.w, y1 - y0},
                        dwn ? kClosePress : (hov ? animColor : kFieldBg));
            fb.drawRect(m_devBtnRect, animColor, 2);

            const char* dt = "MODO DESARROLLADOR";
            const i32 dtw = font5x7::measure(dt, 1);
            const i32 dty = m_devBtnRect.y + (m_devBtnRect.h - textr::glyphHeight(1)) / 2;
            if (dty >= c.y && dty + font5x7::kGlyphH <= c.y + c.h)
                font5x7::drawText(fb, m_devBtnRect.x + (m_devBtnRect.w - dtw) / 2, dty,
                                  dt, hov ? kWhite : animColor, 1);
        }
    }

    m_accountPanel.renderBar(fb);

    if (!m_accountMsg.empty()) {
        const i32 mw = font5x7::measure(m_accountMsg.c_str(), 1);
        font5x7::drawText(fb, (fb.width() - mw) / 2, fb.height() - 24,
                          m_accountMsg.c_str(),
                          m_accountMsgErr ? kAccent : kOk, 1);
    }
}

void HomeScene::drawDescriptionEditor(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    // Velo sobre la seccion para que se lea como un panel superpuesto.
    fb.fillRect({0, top, fb.width(), fb.height() - top}, Color{0, 0, 0, 190});

    const i32 w = std::min(700, fb.width() - 80);
    const i32 h = std::min(320, fb.height() - top - 60);
    const i32 x = (fb.width() - w) / 2;
    const i32 y = top + (fb.height() - top - h) / 2;

    fb.fillRect({x, y, w, h}, kPanelDeep);
    fb.drawRect({x, y, w, h}, kAccent, 2);

    font5x7::drawText(fb, x + 20, y + 18, "DESCRIPCION DE CUENTA EN PUBLICO", kText, 2);

    char info[80];
    std::snprintf(info, sizeof(info), "%zu / %zu caracteres",
                  m_descField.length(), Profile::kMaxDescription);
    font5x7::drawText(fb, x + 20, y + 44, info, kDim, 1);

    m_descField.setRect({x + 20, y + 64, w - 40, 60});
    m_descField.render(fb);

    // Boton de guardar, abajo.
    m_descSaveRect = Recti{x + w - 180 - 20, y + h - 46, 180, 34};
    fb.fillRect(m_descSaveRect,
                (m_descSaveDown && m_descSaveHover) ? kClosePress
                                                    : (m_descSaveHover ? kAccent : kPanelViolet));
    fb.drawRect(m_descSaveRect, m_descSaveHover ? kAccent : kPanelEdge, 2);
    const char* txt = "GUARDAR";
    const i32 tw = font5x7::measure(txt, 2);
    font5x7::drawText(fb, m_descSaveRect.x + (m_descSaveRect.w - tw) / 2,
                      m_descSaveRect.y + (m_descSaveRect.h - textr::glyphHeight(2)) / 2,
                      txt, kWhite, 2);
}

void HomeScene::updateDescriptionEditor(Engine& engine, f64 dt) {
    Input& in = engine.input();
    m_descField.update(dt);

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    m_descSaveHover = (lx >= m_descSaveRect.x && lx < m_descSaveRect.x + m_descSaveRect.w &&
                       ly >= m_descSaveRect.y && ly < m_descSaveRect.y + m_descSaveRect.h);

    if (in.mousePressed(0)) {
        if (m_descField.contains(lx, ly)) {
            m_descField.setFocused(true);
            m_descField.onMouseDown(lx, ly);
        } else if (m_descSaveHover) {
            m_descSaveDown = true;
        }
    }
    if (in.mouseDown(0) && m_descField.focused() && !m_descSaveDown)
        m_descField.onMouseDrag(lx, ly);

    if (in.mouseReleased(0)) {
        if (m_descSaveDown && m_descSaveHover) {
            // Guardar y cerrar el panel: vuelve a la seccion Cuenta.
            m_profile.description = m_descField.text();
            saveProfile();
            m_descOpen = false;
            m_accountMsg    = "Descripcion guardada.";
            m_accountMsgErr = false;
        }
        m_descSaveDown = false;
    }

    // --- teclado ---
    if (in.keyPressed(VK_ESCAPE)) { m_descOpen = false; return; }

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrl  = in.keyDown(VK_CONTROL);
    const bool altGr = ctrl && in.keyDown(VK_MENU);
    const bool ctrlAtajo = ctrl && !altGr;

    static const u32 kEditKeys[] = {
        VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_BACK, VK_DELETE,
        VK_UP, VK_DOWN, 'A', 'C', 'X', 'V'
    };
    for (u32 vk : kEditKeys) {
        const bool isLetter = (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'V');
        if (isLetter && !ctrlAtajo) continue;
        if (in.keyRepeat(vk)) m_descField.onKey(vk, shift, ctrlAtajo);
    }
    if (!ctrlAtajo) {
        for (wchar_t ch : in.charsTyped()) {
            // Enter separa parrafos; el tabulador y el retroceso no son texto.
            if (ch == L'\r' || ch == L'\n') { m_descField.onChar(L'\n'); continue; }
            if (ch == L'\t' || ch == L'\b') continue;
            m_descField.onChar(ch);
        }
    }
}

void HomeScene::updateAccount(Engine& engine, f64 dt) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    auto dentro = [&](const Recti& r) {
        return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
    };

    // La animacion del boton de desarrollador avanza siempre que se ve Cuenta.
    m_devAnimTime += dt;

    m_accountHover = -1;
    if      (dentro(m_galleryRect)) m_accountHover = 0;
    else if (dentro(m_cameraRect))  m_accountHover = 1;
    else if (dentro(m_descBtnRect)) m_accountHover = 2;
    else if (dentro(m_logoutRect))  m_accountHover = 3;
    else if (dentro(m_devBtnRect))  m_accountHover = 4;

    if (in.mousePressed(0)) m_accountDown = m_accountHover;

    if (in.mouseReleased(0)) {
        if (m_accountDown >= 0 && m_accountDown == m_accountHover) {
            std::string err;
            switch (m_accountDown) {
            case 0: {   // galeria
                const std::wstring src = photo::pickFromGallery(engine.window().handle());
                if (!src.empty()) {
                    photo::ContentCheck cc;
                    if (photo::importImage(src, kPhotoFile, err, &cc)) {
                        m_profile.photoFile = "foto-perfil.bmp";
                        saveProfile();
                        m_photoTried = false;   // forzar recarga del avatar
                        m_accountMsg = "Foto actualizada.";
                        m_accountMsgErr = false;
                    } else {
                        // Se muestra el motivo real: si el filtro de contenido
                        // la rechaza, el usuario debe saber por que y poder
                        // elegir otra o pedir revision.
                        m_accountMsg = err;
                        m_accountMsgErr = true;
                    }
                }
                break;
            }
            case 1: {   // camara
                if (photo::captureFromCamera(kPhotoFile, err)) {
                    // La captura pasa por el mismo control que la galeria:
                    // si no, bastaria usar la camara para saltarselo.
                    std::vector<u32> px; i32 pw = 0, ph = 0;
                    photo::ContentCheck cc;
                    if (photo::loadBmp(kPhotoFile, px, pw, ph))
                        cc = photo::checkImageContent(px, pw, ph);

                    if (!cc.allowed) {
                        DeleteFileW(kPhotoFile);
                        m_accountMsg    = cc.reason;
                        m_accountMsgErr = true;
                    } else {
                        m_profile.photoFile = "foto-perfil.bmp";
                        saveProfile();
                        m_photoTried = false;   // forzar recarga del avatar
                        m_accountMsg = "Foto capturada.";
                        m_accountMsgErr = false;
                    }
                } else {
                    m_accountMsg = err;
                    m_accountMsgErr = true;
                }
                break;
            }
            case 2:     // descripcion publica
                m_descField.setText(m_profile.description);
                m_descField.setFocused(true);
                m_descOpen = true;
                break;
            case 3:     // cerrar sesion
                // Borra la sesion recordada y vuelve a la pantalla de acceso,
                // que ahora si pedira credenciales. La cuenta no se toca.
                Session::clear(Session::kFile);
                m_wantLogout = true;
                break;
            case 4:     // modo desarrollador
                m_section = kDevSection;
                break;
            default: break;
            }
        }
        m_accountDown = -1;
    }
}

void HomeScene::ensureUninstalledLoaded() const {
    if (m_uninstalledLoaded) return;
    m_uninstalledLoaded = true;
    m_uninstalled.clear();

    std::ifstream in(kUninstFile, std::ios::binary);
    if (!in) return;
    std::string l;
    while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n' ||
                              l.back() == ' '  || l.back() == '\t'))
            l.pop_back();
        if (!l.empty() && l[0] != '#') m_uninstalled.push_back(l);
    }
}

void HomeScene::saveUninstalled() const {
    std::ofstream out(kUninstFile, std::ios::binary);
    if (!out) return;
    out << "# Juegos desinstalados (bloqueados, sin borrar archivos).\r\n";
    for (const auto& g : m_uninstalled) out << g << "\r\n";
}

bool HomeScene::isUninstalled(const std::string& game) const {
    ensureUninstalledLoaded();
    return std::find(m_uninstalled.begin(), m_uninstalled.end(), game) !=
           m_uninstalled.end();
}

void HomeScene::setUninstalled(const std::string& game, bool on) {
    ensureUninstalledLoaded();
    const auto it = std::find(m_uninstalled.begin(), m_uninstalled.end(), game);
    if (on && it == m_uninstalled.end())      m_uninstalled.push_back(game);
    else if (!on && it != m_uninstalled.end()) m_uninstalled.erase(it);
    saveUninstalled();
}

/// Aviso que se lee antes de borrar un juego del equipo.
const char* const kUninstNotice[] = {
    "¡Aviso! Estas a punto de borrar para siempre este",
    "juego y toda tu informacion: avances, mejoras y mas",
    "estas a punto de perderlo.",
    "",
    "¿Estas seguro de esta decision?",
};
constexpr i32 kUninstNoticeLines =
    static_cast<i32>(sizeof(kUninstNotice) / sizeof(kUninstNotice[0]));

void HomeScene::layoutUninstallAsk(Recti area) {
    if (!m_uninstAsk || area.w <= 60 || area.h <= 60) {
        m_uninstPanelRect = Recti{};
        m_uninstYesRect = m_uninstCloudRect = m_uninstNoRect = Recti{};
        return;
    }

    // Panel centrado sobre el detalle del juego.
    const i32 pw = std::max(1, std::min(area.w - 24, 420));
    const i32 ph = std::max(1, std::min(area.h - 24, 220));
    m_uninstPanelRect = Recti{ area.x + (area.w - pw) / 2,
                               area.y + (area.h - ph) / 2, pw, ph };

    // Los tres botones solo existen tras la espera: sin rectangulo no hay
    // nada que pulsar, que es justo lo que se quiere el primer segundo.
    if (m_uninstAskTime < uninstallButtonDelay()) {
        m_uninstYesRect = m_uninstCloudRect = m_uninstNoRect = Recti{};
        return;
    }

    const i32 m = 12, hueco = 8;
    const i32 bh = 34;
    const i32 by = m_uninstPanelRect.y + ph - m - bh;
    const i32 bw = std::max(1, (pw - m * 2 - hueco * 2) / 3);

    // Orden pedido: aceptar, copia en la nube, rechazar.
    m_uninstYesRect   = Recti{ m_uninstPanelRect.x + m, by, bw, bh };
    m_uninstCloudRect = Recti{ m_uninstPanelRect.x + m + bw + hueco, by, bw, bh };
    m_uninstNoRect    = Recti{ m_uninstPanelRect.x + m + (bw + hueco) * 2,
                               by, bw, bh };
}

void HomeScene::doUninstall(const std::string& game, bool copiaNube) {
    // "Copia en la nube" no sube nada: no hay red. Lo que hace es lo unico
    // honesto sin servidor -- guardar el progreso cifrado antes de borrar,
    // para poder recuperarlo. Se dice tal cual en el mensaje.
    i32 guardadas = 0;
    for (const auto& e : library::load(kLibraryFile)) {
        if (e.name != game) continue;

        if (copiaNube && !e.exe.empty()) {
            // El paquete de datos se conserva aparte del juego: al borrar la
            // carpeta seguira existiendo para restaurar el progreso.
            std::wstring dir = e.exe;
            const size_t s = dir.find_last_of(L"\\/");
            if (s != std::wstring::npos) dir = dir.substr(0, s);

            const std::wstring respaldo =
                std::wstring(kGamesDir) + L"\\_respaldos";
            CreateDirectoryW(respaldo.c_str(), nullptr);

            std::wstring destino = respaldo + L"\\";
            for (const char c : game) {
                const wchar_t w = static_cast<wchar_t>(
                    static_cast<unsigned char>(c));
                destino.push_back((w == L'\\' || w == L'/' || w == L':')
                                  ? L'_' : w);
            }
            destino += L".datos.tomate";

            std::string err;
            if (gamevault::sealDataOnly(e.exe, destino, nullptr, err))
                ++guardadas;
        }
    }

    // --- borrado real de los archivos del disco ---
    // Se hace ANTES de quitar la entrada: una vez fuera del registro ya no
    // se sabria donde estaban los archivos, y quedarian ocupando espacio
    // para siempre.
    i32 archivos = 0;
    std::string derr;
    for (const auto& e : library::load(kLibraryFile))
        if (e.name == game) archivos += library::deleteFiles(e, derr);

    // Se quita de la biblioteca: el juego deja de existir para la app.
    library::remove(kLibraryFile, game);
    setUninstalled(game, false);   // ya no esta "bloqueado", esta borrado

    m_libraryLoaded   = false;
    m_librarySelected = -1;
    m_historyLoaded   = false;

    char msg[192];
    if (!derr.empty()) {
        std::snprintf(msg, sizeof(msg),
                      "Quitado de la biblioteca, pero: %s", derr.c_str());
        m_accountMsg    = msg;
        m_accountMsgErr = true;
        return;
    }

    if (copiaNube) {
        std::snprintf(msg, sizeof(msg),
                      "Borrado: %d archivo(s). %s", archivos,
                      guardadas > 0 ? "Tu progreso quedo en una copia local."
                                    : "No habia progreso que guardar.");
    } else {
        std::snprintf(msg, sizeof(msg),
                      "Borrado del equipo: %d archivo(s) eliminados.", archivos);
    }
    m_accountMsg    = msg;
    m_accountMsgErr = false;
}

bool HomeScene::updateUninstallAsk(Engine& engine, f64 dt) {
    if (!m_uninstAsk) return false;

    m_uninstAskTime += dt;

    // El dialogo se coloca aqui y no solo al dibujar: drawGameDetail retorna
    // antes de llegar a el si no hay juego seleccionado, y entonces sus
    // botones se quedarian sin rectangulo para siempre.
    {
        Framebuffer& fb = engine.renderer().fb();
        const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
        const i32 lado = librarySideWidthFor(fb.width());
        layoutUninstallAsk(Recti{ lado, top,
                                  std::max(1, fb.width() - lado * 2),
                                  std::max(1, fb.height() - top) });
    }

    Input& in = engine.input();
    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    m_uninstHoverBtn = -1;
    if (dentro(m_uninstYesRect))        m_uninstHoverBtn = 0;
    else if (dentro(m_uninstCloudRect)) m_uninstHoverBtn = 1;
    else if (dentro(m_uninstNoRect))    m_uninstHoverBtn = 2;

    if (in.mousePressed(0) && m_uninstHoverBtn >= 0)
        m_uninstDownBtn = m_uninstHoverBtn;

    if (in.mouseReleased(0)) {
        if (m_uninstDownBtn >= 0 && m_uninstDownBtn == m_uninstHoverBtn) {
            switch (m_uninstDownBtn) {
                case 0: doUninstall(m_uninstTarget, false); break;
                case 1: doUninstall(m_uninstTarget, true);  break;
                default:
                    m_accountMsg    = "Operacion cancelada.";
                    m_accountMsgErr = false;
                    break;
            }
            m_uninstAsk = false;
            m_uninstTarget.clear();
        }
        m_uninstDownBtn = -1;
    }

    // ESC cancela, igual que el boton de rechazar.
    if (in.keyPressed(VK_ESCAPE)) {
        m_uninstAsk = false;
        m_uninstTarget.clear();
    }

    return true;   // con el dialogo abierto, el raton es suyo
}

void HomeScene::drawUninstallAsk(Framebuffer& fb, Recti area) {
    if (!m_uninstAsk) return;
    // Se recoloca siempre: el area del detalle puede cambiar de tamano, y
    // tras la espera hay que crear los rectangulos de los tres botones.
    layoutUninstallAsk(area);

    const Recti p = m_uninstPanelRect;
    if (p.w <= 0) return;

    // Velo sobre el detalle: separa el dialogo de lo que hay detras.
    Color velo = kBg; velo.a = 200;
    fb.fillRect(area, velo);

    fb.fillRect(p, kPanelDeep);
    fb.drawRect(p, kAccent, 3);

    // --- el aviso ---
    i32 y = p.y + 14;
    for (i32 i = 0; i < kUninstNoticeLines; ++i) {
        if (!kUninstNotice[i][0]) { y += 8; continue; }
        const i32 w = font5x7::measureUtf8(kUninstNotice[i], 1);
        // La primera linea lleva el "¡Aviso!" y va en rojo; la ultima es la
        // pregunta y va en blanco, que es donde se decide.
        const Color c = (i == 0) ? kAccent
                      : (i == kUninstNoticeLines - 1) ? kWhite : kText;
        if (w < p.w - 16)
            font5x7::drawTextUtf8(fb, p.x + (p.w - w) / 2, y,
                                  kUninstNotice[i], c, 1);
        y += textr::glyphHeight(1) + 6;
    }

    // Nombre del juego, para que no haya duda de cual se borra.
    if (!m_uninstTarget.empty()) {
        const i32 nw = font5x7::measureUtf8(m_uninstTarget.c_str(), 2);
        if (nw < p.w - 16)
            font5x7::drawTextUtf8(fb, p.x + (p.w - nw) / 2, y + 4,
                                  m_uninstTarget.c_str(), kAccent, 2);
    }

    // --- los tres botones, tras la espera ---
    if (m_uninstAskTime < uninstallButtonDelay()) {
        // Mientras tanto, una cuenta atras: explica por que no hay botones
        // todavia, en vez de parecer que el dialogo esta roto.
        const char* esp = "Lee el aviso...";
        const i32 ew = font5x7::measureUtf8(esp, 1);
        font5x7::drawTextUtf8(fb, p.x + (p.w - ew) / 2,
                              p.y + p.h - 26, esp, kDim, 1);
        return;
    }

    struct Btn { Recti r; const char* txt; Color c; };
    const Btn btns[3] = {
        { m_uninstYesRect,   "ACEPTAR",       kAccent       },
        { m_uninstCloudRect, "COPIA EN NUBE", kSteamBlue    },
        { m_uninstNoRect,    "RECHAZAR",      kConsentGreen },
    };

    for (i32 i = 0; i < 3; ++i) {
        const Recti r = btns[i].r;
        if (r.w <= 0) continue;
        const bool hov  = (m_uninstHoverBtn == i);
        const bool down = (m_uninstDownBtn == i && hov);

        fb.fillRect(r, down ? kSteamEdge : (hov ? btns[i].c : kFieldBg));
        fb.drawRect(r, btns[i].c, 2);

        font5x7::drawFitted(fb, r, btns[i].txt, hov ? kBg : kText, 1, 4);
    }
}

void HomeScene::updateModsSection(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    // Volver: la franja superior derecha, igual que en las demas secciones.
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const bool enVolver = (ly >= top && ly < top + 36 &&
                           lx > engine.renderer().fb().width() - 200);
    if (in.mouseReleased(0) && enVolver) m_section = kPanelsSection;
}

void HomeScene::drawModsSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    fb.fillRect({0, top, fb.width(), fb.height() - top}, kSteamBg);

    std::string titulo = "MODS";
    if (m_librarySelected >= 0 &&
        m_librarySelected < static_cast<i32>(m_libraryGames.size()))
        titulo = "MODS - " + m_libraryGames[static_cast<size_t>(m_librarySelected)].name;
    font5x7::drawTextUtf8(fb, 24, top + 16, titulo.c_str(), kSteamBlue, 3);

    const char* volver = "< VOLVER";
    const i32 vw = font5x7::measureUtf8(volver, 1);
    font5x7::drawTextUtf8(fb, fb.width() - vw - 24, top + 22, volver, kSteamDim, 1);

    // Maquetada como una tienda, pero sin contenido todavia: se dice en vez
    // de dejar la pantalla en blanco.
    const i32 m = 24;
    const Recti panel{ m, top + 60, fb.width() - m * 2,
                       std::max(1, fb.height() - top - 80) };
    fb.fillRect(panel, kSteamPanel);
    fb.drawRect(panel, kSteamEdge, 1);

    const char* l1 = "TIENDA DE MODS";
    const char* l2 = "Proximamente: texturas, mapas y contenido de la comunidad.";
    const i32 w1 = font5x7::measureUtf8(l1, 3);
    const i32 w2 = font5x7::measureUtf8(l2, 1);
    font5x7::drawTextUtf8(fb, panel.x + (panel.w - w1) / 2,
                          panel.y + panel.h / 2 - 20, l1, kSteamDim, 3);
    if (w2 < panel.w - 20)
        font5x7::drawTextUtf8(fb, panel.x + (panel.w - w2) / 2,
                              panel.y + panel.h / 2 + 14, l2, kSteamEdge, 1);
}

namespace {

/// Parte el cuerpo de un comunicado en lineas dibujables.
///
/// Respeta los saltos de linea escritos -- un comunicado se redacta en
/// parrafos -- y, dentro de cada parrafo, corta por palabras al ancho dado.
/// Devuelve las lineas en vez de usar un callback porque aqui se recorren dos
/// veces (medir el alto y dibujar), y hacerlo con el mismo vector garantiza
/// que las dos pasadas cuentan exactamente lo mismo.
std::vector<std::string> parteComunicado(const std::string& texto, i32 ancho) {
    std::vector<std::string> out;
    if (ancho <= 8) return out;

    size_t ini = 0;
    while (ini <= texto.size()) {
        const size_t fin = texto.find('\n', ini);
        const std::string parrafo =
            texto.substr(ini, (fin == std::string::npos) ? std::string::npos
                                                         : fin - ini);

        // Un parrafo vacio es una linea en blanco a proposito: separa bloques.
        if (parrafo.empty()) {
            out.push_back(std::string());
        } else {
            std::string linea;
            size_t p = 0;
            while (p < parrafo.size()) {
                while (p < parrafo.size() && parrafo[p] == ' ') ++p;
                const size_t pi = p;
                while (p < parrafo.size() && parrafo[p] != ' ') ++p;
                if (p == pi) break;
                const std::string palabra = parrafo.substr(pi, p - pi);

                const std::string prueba = linea.empty() ? palabra
                                                         : linea + " " + palabra;
                if (!linea.empty() &&
                    font5x7::measureUtf8(prueba.c_str(), 1) > ancho) {
                    out.push_back(linea);
                    linea = palabra;
                } else {
                    linea = prueba;
                }
            }
            if (!linea.empty()) out.push_back(linea);
        }

        if (fin == std::string::npos) break;
        ini = fin + 1;
    }
    return out;
}

} // namespace

void HomeScene::ensureNoticesLoaded() {
    // El juego seleccionado manda. Si cambio, se relee: los comunicados son
    // de un juego concreto y mezclarlos no tendria sentido.
    const bool hay = (m_librarySelected >= 0 &&
                      m_librarySelected < static_cast<i32>(m_libraryGames.size()));
    const std::string juego = hay
        ? m_libraryGames[static_cast<size_t>(m_librarySelected)].name
        : std::string();

    if (m_noticesLoaded && m_noticesGame == juego) return;

    m_notices.clear();
    m_noticesGame  = juego;
    m_noticesLoaded = true;
    if (juego.empty()) return;

    m_notices = notices::forGame(kNoticesFile, juego);
}

void HomeScene::updateNoticesSection(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 fbw = (m_lastFbWidth > 0) ? m_lastFbWidth
                                        : engine.renderer().fb().width();

    // ESC y el boton "< VOLVER" devuelven a la biblioteca, que es de donde se
    // entra: igual que MODS y RESENAS.
    if (in.keyPressed(VK_ESCAPE)) { m_section = kPanelsSection; return; }
    const bool enVolver = (ly >= top && ly < top + 30 && lx > fbw - 120);
    if (in.mouseReleased(0) && enVolver) { m_section = kPanelsSection; return; }

    ensureNoticesLoaded();
    m_noticesPanel.update(engine);
}

void HomeScene::drawNoticesSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    fb.fillRect({ 0, top, fb.width(), fb.height() - top }, kSteamBg);

    ensureNoticesLoaded();

    std::string titulo = "NOTIFICACIONES";
    if (!m_noticesGame.empty()) titulo += " - " + m_noticesGame;
    font5x7::drawTextUtf8(fb, 24, top + 16, titulo.c_str(), kDevGold, 3);

    const char* volver = "< VOLVER";
    const i32 vw = font5x7::measureUtf8(volver, 1);
    font5x7::drawTextUtf8(fb, fb.width() - vw - 24, top + 22, volver,
                          kSteamDim, 1);

    const i32 m = 24;
    const Recti marco{ m, top + 60, fb.width() - m * 2,
                       std::max(1, fb.height() - top - 80) };
    fb.fillRect(marco, kSteamPanel);
    fb.drawRect(marco, kSteamEdge, 1);

    // Sin comunicados se dice, en vez de dejar un panel vacio que parece roto.
    if (m_notices.empty()) {
        const char* l1 = "SIN COMUNICADOS";
        const char* l2 = "Aqui apareceran los avisos del creador de este juego.";
        const i32 w1 = font5x7::measureUtf8(l1, 3);
        const i32 w2 = font5x7::measureUtf8(l2, 1);
        font5x7::drawTextUtf8(fb, marco.x + (marco.w - w1) / 2,
                              marco.y + marco.h / 2 - 20, l1, kSteamDim, 3);
        if (w2 < marco.w - 20)
            font5x7::drawTextUtf8(fb, marco.x + (marco.w - w2) / 2,
                                  marco.y + marco.h / 2 + 14, l2, kSteamEdge, 1);
        return;
    }

    // --- los comunicados, con scroll ---
    m_noticesPanel.setRect({ marco.x + 2, marco.y + 2,
                             std::max(1, marco.w - 4),
                             std::max(1, marco.h - 4) });
    const Recti cr = m_noticesPanel.contentRect();
    const i32 anchoTexto = std::max(40, cr.w - 24);
    const i32 lineaH = textr::lineHeight(1);

    // El alto total se calcula con el MISMO partidor que dibuja, para que el
    // scroll no se quede corto ni sobre: una divergencia aqui deja el ultimo
    // comunicado inalcanzable.
    i32 alto = 8;
    for (const notices::Notice& n : m_notices) {
        alto += textr::lineHeight(2) + 4;          // titulo
        alto += lineaH + 6;                        // fecha
        alto += static_cast<i32>(parteComunicado(n.body, anchoTexto).size()) *
                lineaH;
        if (!n.image.empty()) alto += 150 + 8;     // hueco de la imagen
        alto += 18;                                // separacion entre avisos
    }
    m_noticesPanel.setContentHeight(alto);
    m_noticesPanel.renderBar(fb);

    i32 y = cr.y + 8 - m_noticesPanel.offset();
    for (const notices::Notice& n : m_notices) {
        // Titular, en el dorado de la seccion.
        if (y + textr::lineHeight(2) > cr.y && y < cr.y + cr.h)
            font5x7::drawTextUtf8(fb, cr.x + 12, y, n.title.c_str(),
                                  kDevGold, 2);
        y += textr::lineHeight(2) + 4;

        // Fecha: un comunicado sin fecha no se sabe si es de hoy o de hace un
        // ano, y eso cambia como se lee.
        const std::string fecha = history::formatDate(n.at);
        if (y + lineaH > cr.y && y < cr.y + cr.h)
            font5x7::drawTextUtf8(fb, cr.x + 12, y, fecha.c_str(), kSteamDim, 1);
        y += lineaH + 6;

        // Cuerpo: se parte por palabras y se pinta linea a linea, saltando lo
        // que cae fuera del panel. Sin limite de longitud.
        for (const std::string& linea : parteComunicado(n.body, anchoTexto)) {
            if (y + lineaH > cr.y && y < cr.y + cr.h && !linea.empty())
                font5x7::drawTextUtf8(fb, cr.x + 12, y, linea.c_str(), kText, 1);
            y += lineaH;
        }

        // Imagen adjunta, encajada conservando la proporcion. Se decodifica
        // una sola vez: hacerlo cada frame pararia el desplazamiento.
        if (!n.image.empty()) {
            if (m_noticeImgLoaded != n.image) {
                m_noticeImgPix.clear();
                m_noticeImgW = m_noticeImgH = 0;
                if (photo::loadAnyImage(n.image, m_noticeImgPix,
                                        m_noticeImgW, m_noticeImgH))
                    m_noticeImgLoaded = n.image;
            }
            if (m_noticeImgW > 0 && m_noticeImgH > 0 &&
                m_noticeImgLoaded == n.image) {
                const i32 maxH = 150;
                const i32 maxW = anchoTexto;
                const i32 porAncho = maxW * m_noticeImgH /
                                     std::max(1, m_noticeImgW);
                const i32 dstW = (porAncho <= maxH)
                               ? maxW
                               : maxH * m_noticeImgW / std::max(1, m_noticeImgH);
                const i32 dstH = (porAncho <= maxH) ? porAncho : maxH;

                for (i32 yy = 0; yy < dstH; ++yy) {
                    const i32 py = y + yy;
                    if (py < cr.y || py >= cr.y + cr.h) continue;   // recorte
                    for (i32 xx = 0; xx < dstW; ++xx) {
                        const i32 sx = std::min(m_noticeImgW - 1,
                                                xx * m_noticeImgW /
                                                std::max(1, dstW));
                        const i32 sy = std::min(m_noticeImgH - 1,
                                                yy * m_noticeImgH /
                                                std::max(1, dstH));
                        fb.setPixel(cr.x + 12 + xx, py,
                                    m_noticeImgPix[
                                        static_cast<size_t>(sy) * m_noticeImgW + sx]);
                    }
                }
            }
            y += 150 + 8;
        }

        // Raya de separacion: con varios comunicados seguidos, sin ella no se
        // ve donde acaba uno y empieza el siguiente.
        y += 8;
        if (y > cr.y && y < cr.y + cr.h)
            fb.fillRect({ cr.x + 12, y, anchoTexto, 1 }, kSteamEdge);
        y += 10;
    }
}

void HomeScene::drawGameDetail(Engine& engine, Framebuffer& fb, Recti area) {
    (void)engine;
    if (area.w <= 0 || area.h <= 0) return;

    fb.fillRect(area, kBg);   // fondo del panel central

    // Sin juego seleccionado: invitacion a elegir uno.
    if (m_librarySelected < 0 ||
        m_librarySelected >= static_cast<i32>(m_libraryGames.size())) {
        const char* msg = "Selecciona un juego de la biblioteca";
        const i32 mw = font5x7::measure(msg, 1);
        font5x7::drawText(fb, area.x + (area.w - mw) / 2,
                          area.y + area.h / 2, msg, kDim, 1);
        // Los botones no se dibujan, pero sus rectangulos no pueden quedar en
        // cero: quien los consulte (pruebas, hit-test) leeria un boton
        // degenerado en el origen. Se dejan con la geometria que tendrian,
        // fuera de la vista, para que sigan siendo coherentes.
        const i32 gapV = 20, bhV = 44;
        const i32 anchoParV = area.w - 24 - gapV;
        const bool apilarV  = (anchoParV / 2) < 110;
        const i32 bwV = apilarV ? std::max(90, std::min(150, area.w - 24))
                                : std::max(110, std::min(150, anchoParV / 2));
        const i32 bxV = apilarV ? area.x + (area.w - bwV) / 2
                                : area.x + (area.w - (bwV * 2 + gapV)) / 2;
        const i32 byV = area.y + area.h;   // justo bajo el area: no se ve
        m_playBtnRect    = Recti{ bxV, byV, bwV, bhV };
        m_reviewsBtnRect = apilarV ? Recti{ bxV, byV + bhV + 10, bwV, bhV }
                                   : Recti{ bxV + bwV + gapV, byV, bwV, bhV };

        // Los dos de abajo, por el mismo motivo: sin rectangulo, quien los
        // consulte leeria un boton degenerado en el origen.
        const i32 bh2V = 32;
        const i32 by2V = (apilarV ? m_reviewsBtnRect.y + bhV + 10 : byV + bhV + 12);
        m_uninstallRect = Recti{ bxV, by2V, bwV, bh2V };
        m_modsRect      = apilarV ? Recti{ bxV, by2V + bh2V + 8, bwV, bh2V }
                                  : Recti{ bxV + bwV + gapV, by2V, bwV, bh2V };
        const i32 by3V = (apilarV ? m_modsRect.y + bh2V + 8 : by2V + bh2V + 8);
        m_noticesRect = Recti{ bxV, by3V,
                               apilarV ? bwV : bwV * 2 + gapV, bh2V };
        return;
    }

    const library::Entry& g = m_libraryGames[m_librarySelected];

    // --- imagen del juego ---
    // Marco 16:9 centrado arriba. Si el juego trae una portada BMP se muestra;
    // si no, un recuadro con el nombre como marcador.
    //
    // El alto tambien limita: en una ventana baja, calcular la portada solo
    // por el ancho la dejaria tapando el titulo y los botones. Se reserva de
    // antemano lo que ocupan (titulo + botones + margenes) y la portada se
    // queda con lo que sobra.
    const i32 kReservaBajo = 130;   // titulo, botones y aire inferior
    const i32 anchoMax = std::max(0, area.w - 48);
    const i32 altoMax  = std::max(0, area.h - 30 - kReservaBajo);

    // El marco nunca baja de 160 px de ancho: por debajo la portada se ve como
    // una miniatura y el nombre encima deja de leerse.
    const i32 imgW = std::max(0, std::min({ anchoMax, 480, altoMax * 16 / 9 }));
    const i32 imgH = imgW * 9 / 16;
    const i32 imgX = area.x + (area.w - imgW) / 2;
    const i32 imgY = area.y + 30;

    // Con menos de 160 px la portada no aporta y se omite entera, dejando el
    // sitio al titulo y los botones, que es lo que de verdad hace falta.
    const bool hayPortada = (imgW >= 160 && imgH > 0);

    if (hayPortada) {
        fb.fillRect({imgX, imgY, imgW, imgH}, kPanelDeep);
        fb.drawRect({imgX, imgY, imgW, imgH}, kPanelEdge, 2);

        // Portada: <carpeta-del-exe>\portada.bmp, si existe.
        std::wstring cover = g.exe;
        const size_t s = cover.find_last_of(L"\\/");
        cover = (s == std::wstring::npos ? L"." : cover.substr(0, s)) + L"\\portada.bmp";
        std::vector<u32> px; i32 pw = 0, ph = 0;

        // El interior deja 4 px de margen por lado. Se comprueba que quede
        // area util: sin esto, un marco pequeno hace dividir por cero al
        // calcular el pixel de origen.
        const i32 dstW = imgW - 8, dstH = imgH - 8;
        if (dstW > 0 && dstH > 0 &&
            photo::loadBmp(cover, px, pw, ph) && pw > 0 && ph > 0) {
            for (i32 yy = 0; yy < dstH; ++yy)
                for (i32 xx = 0; xx < dstW; ++xx) {
                    const i32 sx = std::min(pw - 1, xx * pw / dstW);
                    const i32 sy = std::min(ph - 1, yy * ph / dstH);
                    fb.setPixel(imgX + 4 + xx, imgY + 4 + yy,
                                px[static_cast<size_t>(sy) * pw + sx]);
                }
        } else {
            // Sin portada: el nombre dentro del marco, a la escala que quepa.
            i32 esc = 3;
            while (esc > 1 && font5x7::measureUtf8(g.name.c_str(), esc) > imgW - 16)
                --esc;
            const i32 tw = font5x7::measureUtf8(g.name.c_str(), esc);
            font5x7::drawTextUtf8(fb, imgX + (imgW - tw) / 2,
                                  imgY + (imgH - textr::glyphHeight(esc)) / 2,
                                  g.name.c_str(), kText, esc);
        }
    }

    // --- titulo ---
    // La escala baja solo si el nombre no cabe: asi en una ventana estrecha el
    // texto sigue siendo legible en vez de salirse por los lados.
    const i32 tituloY = hayPortada ? imgY + imgH + 20 : imgY + 10;
    i32 escT = 3;
    while (escT > 1 && font5x7::measureUtf8(g.name.c_str(), escT) > area.w - 24)
        --escT;
    const i32 tw = font5x7::measureUtf8(g.name.c_str(), escT);
    font5x7::drawTextUtf8(fb, area.x + (area.w - tw) / 2, tituloY,
                          g.name.c_str(), kText, escT);

    // --- botones JUGAR y RESENAS ---
    // Se encogen si el centro es estrecho, pero no por debajo de lo que hace
    // falta para leer su etiqueta; si aun asi no caben lado a lado, se apilan.
    const i32 gap = 20;
    const i32 bh  = 44;
    const i32 anchoPar = area.w - 24 - gap;          // para los dos en fila
    const bool apilar  = (anchoPar / 2) < 110;
    const i32 bw = apilar ? std::max(90, std::min(150, area.w - 24))
                          : std::max(110, std::min(150, anchoPar / 2));

    const i32 by = tituloY + textr::glyphHeight(escT) + 22;
    const i32 bx = apilar ? area.x + (area.w - bw) / 2
                          : area.x + (area.w - (bw * 2 + gap)) / 2;

    m_playBtnRect = Recti{ bx, by, bw, bh };
    // Apilado: RESENAS baja bajo JUGAR en vez de salirse por la derecha.
    m_reviewsBtnRect = apilar ? Recti{ bx, by + bh + 10, bw, bh }
                              : Recti{ bx + bw + gap, by, bw, bh };

    // --- DESINSTALAR y MODS, en la fila de abajo ---
    // Mas bajos que JUGAR: son acciones secundarias, no la principal.
    ensureUninstalledLoaded();
    const i32 bh2 = 32;
    const i32 by2 = (apilar ? m_reviewsBtnRect.y + bh + 10 : by + bh + 12);
    m_uninstallRect = Recti{ bx, by2, bw, bh2 };
    m_modsRect      = apilar ? Recti{ bx, by2 + bh2 + 8, bw, bh2 }
                             : Recti{ bx + bw + gap, by2, bw, bh2 };

    // --- NOTIFICACIONES, bajo las dos anteriores ---
    // A todo lo ancho de la pareja: es donde se leen los comunicados del
    // creador, y conviene que se vea que existe aunque no haya ninguno.
    const i32 by3 = (apilar ? m_modsRect.y + bh2 + 8 : by2 + bh2 + 8);
    m_noticesRect = Recti{ bx, by3, apilar ? bw : bw * 2 + gap, bh2 };

    auto boton = [&](const Recti& r, const char* txt, bool hov, bool down, Color acc) {
        fb.fillRect(r, down ? kClosePress : (hov ? acc : kFieldBg));
        fb.drawRect(r, hov ? acc : kBorder, 2);
        // La etiqueta se reduce si el boton encogio, para no desbordarlo.
        i32 esc = 2;
        while (esc > 1 && font5x7::measure(txt, esc) > r.w - 12) --esc;
        const i32 lw = font5x7::measure(txt, esc);
        font5x7::drawText(fb, r.x + (r.w - lw) / 2,
                          r.y + (r.h - textr::glyphHeight(esc)) / 2,
                          txt, hov ? kWhite : kText, esc);
    };
    // Boton JUGAR: si el juego esta corriendo, se anima con una secuencia de
    // puntos ( .. -> . -> .. -> ... ) en vez del texto normal.
    if (m_runningProc) {
        // Cuatro fotogramas de la secuencia, a ~3 por segundo.
        static const char* kFrames[4] = { "..", ".", "..", "..." };
        const int f = static_cast<int>(m_playAnimTime * 3.0) % 4;
        // Fondo animado (pulso) para que se note que esta activo.
        fb.fillRect(m_playBtnRect, kFieldBgFocus);
        fb.drawRect(m_playBtnRect, kOk, 2);
        const char* jug = "JUGANDO";
        const i32 jw = font5x7::measure(jug, 2);
        const i32 dw = font5x7::measure("...", 2);   // reserva ancho fijo
        const i32 tot = jw + 6 + dw;
        const i32 tx = m_playBtnRect.x + (m_playBtnRect.w - tot) / 2;
        const i32 ty = m_playBtnRect.y + (m_playBtnRect.h - textr::glyphHeight(2)) / 2;
        font5x7::drawText(fb, tx, ty, jug, kOk, 2);
        font5x7::drawText(fb, tx + jw + 6, ty, kFrames[f], kWhite, 2);
    } else {
        boton(m_playBtnRect, "JUGAR", m_playHover, m_playDown, kOk);
    }
    boton(m_reviewsBtnRect, "RESENAS", m_reviewsHover, m_reviewsDown, kAccent);

    // --- DESINSTALAR / REINSTALAR y MODS ---
    const bool desinst = isUninstalled(g.name);

    // Desinstalado: todo el detalle se oscurece. No se borra nada, solo se
    // bloquea el acceso, de ahi que baste con un velo encima.
    if (desinst) {
        Color velo = kBg; velo.a = 170;
        fb.fillRect({ area.x, area.y, area.w,
                      std::max(0, m_uninstallRect.y - area.y) }, velo);

        const char* av = "JUEGO DESINSTALADO";
        const i32 aw = font5x7::measureUtf8(av, 2);
        if (aw < area.w - 20)
            font5x7::drawTextUtf8(fb, area.x + (area.w - aw) / 2,
                                  m_uninstallRect.y - 26, av, kDim, 2);
    }

    auto boton2 = [&](const Recti& r, const char* txt, bool hov, bool down,
                      Color acc) {
        if (r.w <= 0) return;
        fb.fillRect(r, down ? kClosePress : (hov ? acc : kFieldBg));
        fb.drawRect(r, hov ? acc : kBorder, 2);
        i32 esc = 1;
        const i32 lw = font5x7::measureUtf8(txt, esc);
        if (lw < r.w - 8)
            font5x7::drawTextUtf8(fb, r.x + (r.w - lw) / 2,
                                  r.y + (r.h - textr::glyphHeight(esc)) / 2,
                                  txt, hov ? kWhite : kText, esc);
    };

    // El boton cambia de papel segun el estado: REINSTALAR solo existe
    // mientras el juego esta desinstalado, que es lo que se pidio.
    boton2(m_uninstallRect, desinst ? "REINSTALAR" : "DESINSTALAR",
           m_uninstallHover, m_uninstallDown, desinst ? kOk : kAccent);
    boton2(m_modsRect, "MODS", m_modsHover, m_modsDown, kSteamBlue);

    // --- NOTIFICACIONES, con el numero de comunicados ---
    // El numero va en la etiqueta: un comunicado publicado que no se anuncie
    // en ningun sitio no lo lee nadie, y entonces la funcion no sirve.
    {
        ensureNoticesLoaded();
        const i32 n = static_cast<i32>(m_notices.size());
        std::string et = "NOTIFICACIONES";
        if (n > 0) et += " (" + std::to_string(n) + ")";
        boton2(m_noticesRect, et.c_str(), m_noticesHover, m_noticesDown,
               n > 0 ? kDevGold : kSteamBlue);
    }

    // Aviso (p. ej. si el juego no se pudo descifrar/lanzar).
    //
    // drawFitted y no "medir y callar": los motivos de fallo son frases
    // largas ("Windows bloqueo la ejecucion..."), y con la comprobacion
    // anterior el aviso simplemente no se dibujaba. Justo cuando mas falta
    // hace saber que paso.
    if (!m_accountMsg.empty() && m_accountMsgErr) {
        font5x7::drawFitted(fb, { area.x + 8, by + bh + 16,
                                  std::max(1, area.w - 16),
                                  textr::glyphHeight(1) + 2 },
                            m_accountMsg.c_str(), kAccent, 1, 0);
    }

    // El dialogo de borrar va el ultimo: se superpone a todo el detalle.
    drawUninstallAsk(fb, area);
}

void HomeScene::drawReviews(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    fb.fillRect({0, top, fb.width(), fb.height() - top}, kBg);

    // Cabecera con el nombre del juego y un boton para volver.
    std::string titulo = "RESENAS";
    if (m_librarySelected >= 0 &&
        m_librarySelected < static_cast<i32>(m_libraryGames.size()))
        titulo = "RESENAS - " + m_libraryGames[m_librarySelected].name;
    font5x7::drawTextUtf8(fb, 24, top + 16, titulo.c_str(), kText, 2);

    const char* volver = "< VOLVER";
    const i32 vw = font5x7::measure(volver, 1);
    font5x7::drawText(fb, fb.width() - vw - 24, top + 20, volver, kDim, 1);

    // Panel con scroll para las resenas (vacio por ahora).
    const i32 pTop = top + 48;
    m_reviewsPanel.setRect({0, pTop, fb.width(), fb.height() - pTop});
    m_reviewsPanel.setBarWidth(kScrollBarW);
    m_reviewsPanel.setContentHeight(fb.height() * 2);   // deja recorrido de scroll

    fb.fillRect({0, pTop, fb.width(), fb.height() - pTop}, kPanelDeep);
    const char* msg = "Aun no hay resenas de jugadores.";
    const i32 mw = font5x7::measure(msg, 1);
    font5x7::drawText(fb, (fb.width() - mw) / 2, pTop + 40, msg, kDim, 1);

    m_reviewsPanel.renderBar(fb);
}

void HomeScene::layoutChat(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    // Rectangulo vino de 870 px pegado a la derecha; el panel con scroll ocupa
    // lo que queda a la izquierda. Se acota para que en una ventana estrecha
    // no se solapen.
    const i32 wineW = std::min(kChatWineW, fbW - kScrollBarW - 40);
    const i32 leftW = fbW - wineW;

    m_chatPanel.setRect({0, top, std::max(0, leftW), std::max(0, fbH - top)});
    m_chatPanel.setBarOnLeft(true);   // barra pegada a la pared izquierda
    m_chatPanel.setBarWidth(kScrollBarW);
    // Las lineas dan altura para el scroll.
    m_chatPanel.setContentHeight(kChatPad * 2 + kChatMsgs * kChatLineH);
}

namespace {
/// Texto de una linea del chat. Placeholder hasta tener el chat real.
std::string chatLine(i32 i) {
    char b[48];
    std::snprintf(b, sizeof(b), "Mensaje de ejemplo numero %d", i + 1);
    return b;
}
i32 chatSelLo(i32 a, i32 b) { return a < b ? a : b; }
i32 chatSelHi(i32 a, i32 b) { return a < b ? b : a; }
} // namespace

void HomeScene::updateChat(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    const Recti c   = m_chatPanel.contentRect();
    const i32   off = m_chatPanel.offset();

    // Convierte una y de pantalla en indice de linea de chat.
    auto lineaEn = [&](i32 y) -> i32 {
        if (y < c.y || y >= c.y + c.h) return -1;
        const i32 rel = y - (c.y + kChatPad) + off;
        if (rel < 0) return -1;
        const i32 idx = rel / kChatLineH;
        return (idx >= 0 && idx < kChatMsgs) ? idx : -1;
    };

    const bool dentro = (lx >= c.x && lx < c.x + c.w);

    if (in.mousePressed(0) && dentro) {
        const i32 l = lineaEn(ly);
        if (l >= 0) {
            m_chatSelStart = m_chatSelEnd = l;
            m_chatSelecting = true;
        } else {
            m_chatSelStart = m_chatSelEnd = -1;   // clic en vacio: deselecciona
        }
    }

    if (m_chatSelecting && in.mouseDown(0)) {
        const i32 l = lineaEn(ly);
        if (l >= 0) m_chatSelEnd = l;
    }

    if (in.mouseReleased(0)) m_chatSelecting = false;

    // Ctrl+C copia la seleccion.
    if (in.keyDown(VK_CONTROL) && in.keyPressed('C'))
        copyChatSelection();
}

void HomeScene::copyChatSelection() {
    if (m_chatSelStart < 0 || m_chatSelEnd < 0) return;

    std::string texto;
    const i32 lo = chatSelLo(m_chatSelStart, m_chatSelEnd);
    const i32 hi = chatSelHi(m_chatSelStart, m_chatSelEnd);
    for (i32 i = lo; i <= hi; ++i) {
        texto += chatLine(i);
        if (i < hi) texto += "\r\n";
    }

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = texto.size() + 1;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, texto.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void HomeScene::drawChat(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    const i32 wineW = std::min(kChatWineW, fb.width() - kScrollBarW - 40);
    const i32 leftW = fb.width() - wineW;

    // --- panel izquierdo: mensajes como texto suelto y seleccionable ---
    fb.fillRect({0, top, leftW, fb.height() - top}, kPanelDeep);

    const Recti c   = m_chatPanel.contentRect();
    const i32   off = m_chatPanel.offset();

    const i32 selLo = (m_chatSelStart >= 0) ? chatSelLo(m_chatSelStart, m_chatSelEnd) : -1;
    const i32 selHi = (m_chatSelStart >= 0) ? chatSelHi(m_chatSelStart, m_chatSelEnd) : -1;

    for (i32 i = 0; i < kChatMsgs; ++i) {
        const i32 y = c.y + kChatPad + i * kChatLineH - off;
        if (y + kChatLineH <= c.y || y >= c.y + c.h) continue;   // fuera de vista

        const i32 x = c.x + 12;

        // Resalte de la linea seleccionada (recortado al area visible).
        const bool sel = (i >= selLo && i <= selHi);
        if (sel) {
            const i32 y0 = std::max(y, c.y);
            const i32 y1 = std::min(y + kChatLineH, c.y + c.h);
            if (y1 > y0)
                fb.fillRect({c.x, y0, c.w, y1 - y0}, kSelection);
        }

        const i32 ty = y + (kChatLineH - textr::glyphHeight(1)) / 2;
        if (ty >= c.y && ty + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawTextUtf8(fb, x, ty, chatLine(i).c_str(),
                                  sel ? kWhite : kText, 1);
    }
    m_chatPanel.renderBar(fb);

    // --- rectangulo vino de 870 px, pegado a la pared derecha ---
    fb.fillRect({fb.width() - wineW, top, wineW, fb.height() - top}, kWine);
    fb.fillRect({fb.width() - wineW, top, 2, fb.height() - top}, kWineEdge);
}

// =========================================================================
//  Franja derecha de la Biblioteca: precio e historial de versiones
// =========================================================================

void HomeScene::ensureHistoryLoaded() {
    const bool haySeleccion = (m_librarySelected >= 0 &&
        m_librarySelected < static_cast<i32>(m_libraryGames.size()));
    const std::string nombre = haySeleccion
        ? m_libraryGames[m_librarySelected].name : std::string();

    // Se recarga solo al cambiar de juego: leer el archivo cada frame seria
    // tocar disco 60 veces por segundo para un dato que casi nunca cambia.
    if (m_historyLoaded && m_historyOf == nombre) return;

    m_historyOf    = nombre;
    m_historyLoaded = true;
    if (nombre.empty()) { m_history = history::GameHistory{}; return; }

    m_history = history::forGame(kHistoryFile, nombre);
}

void HomeScene::drawPriceTimeline(Framebuffer& fb, Recti area) {
    if (area.w <= 20 || area.h <= 30) return;

    font5x7::drawText(fb, area.x + 10, area.y + 8, "PRECIO", kWhite, 1);

    const auto& pts = m_history.prices;
    if (pts.empty()) {
        font5x7::drawText(fb, area.x + 10, area.y + 26, "sin datos", kDim, 1);
        return;
    }

    // --- precio inicial y vigente ---
    const i32 ini = m_history.initialPrice();
    const i32 act = m_history.currentPrice();

    std::string usd, mxn, eur;
    devcatalog::formatPrice(ini, usd, mxn, eur);
    font5x7::drawText(fb, area.x + 10, area.y + 24, "INICIAL", kGlyph, 1);
    font5x7::drawText(fb, area.x + 62, area.y + 24, usd.c_str(), kWhite, 1);

    devcatalog::formatPrice(act, usd, mxn, eur);
    font5x7::drawText(fb, area.x + 10, area.y + 38, "AHORA", kGlyph, 1);
    // Verde si bajo de precio, rojo claro si subio, blanco si sigue igual: el
    // color responde de un vistazo a "me conviene comprarlo ahora?".
    const Color cAct = (act < ini) ? kOk : (act > ini) ? kDevGold : kWhite;
    font5x7::drawText(fb, area.x + 62, area.y + 38, usd.c_str(), cAct, 1);

    // Variacion respecto al inicial, en porcentaje. Solo tiene sentido si el
    // juego no empezo siendo gratis: sobre 0 no hay porcentaje que calcular.
    if (ini > 0 && act != ini) {
        const i32 pct = (act - ini) * 100 / ini;
        char var[24];
        std::snprintf(var, sizeof(var), "%s%d%%", pct > 0 ? "+" : "", pct);
        font5x7::drawText(fb, area.x + area.w - 10 - font5x7::measure(var, 1),
                          area.y + 38, var, cAct, 1);
    }

    // --- linea de tiempo ---
    // Grafico de puntos unidos: el eje X es el orden de los cambios y el Y el
    // precio, normalizado al maximo historico para que use todo el alto.
    const i32 gx = area.x + 10;
    const i32 gy = area.y + 54;
    const i32 gw = std::max(1, area.w - 20);
    const i32 gh = std::max(1, area.h - 54 - 18);
    if (gh < 12) return;

    fb.fillRect({gx, gy, gw, gh}, Color{0, 0, 0, 90});

    i32 maxP = 1;   // evita dividir por cero si todo es gratis
    for (const auto& p : pts) maxP = std::max(maxP, p.usdCents);

    /// Punto (x,y) del grafico para el cambio i.
    auto puntoX = [&](size_t i) {
        if (pts.size() <= 1) return gx + gw / 2;
        return gx + static_cast<i32>(static_cast<i64>(gw - 1) * i / (pts.size() - 1));
    };
    auto puntoY = [&](i32 cents) {
        return gy + gh - 1 - static_cast<i32>(static_cast<i64>(gh - 1) * cents / maxP);
    };

    for (size_t i = 0; i < pts.size(); ++i) {
        const i32 px = puntoX(i);
        const i32 py = puntoY(pts[i].usdCents);

        // Tramo hasta el punto anterior, coloreado segun subio o bajo.
        if (i > 0) {
            const i32 ax = puntoX(i - 1);
            const i32 ay = puntoY(pts[i - 1].usdCents);
            const Color c = (pts[i].usdCents > pts[i - 1].usdCents) ? kDevGold
                          : (pts[i].usdCents < pts[i - 1].usdCents) ? kOk
                                                                    : kWhite;
            fb.drawLine(ax, ay, px, py, c);
        }
        fb.fillRect({px - 1, py - 1, 3, 3}, kWhite);
    }

    // Fechas de los extremos, para saber que periodo abarca la linea.
    const std::string d0 = history::formatDate(pts.front().at);
    font5x7::drawText(fb, gx, gy + gh + 4, d0.c_str(), kGlyph, 1);
    if (pts.size() > 1) {
        const std::string d1 = history::formatDate(pts.back().at);
        font5x7::drawText(fb, gx + gw - font5x7::measure(d1.c_str(), 1),
                          gy + gh + 4, d1.c_str(), kGlyph, 1);
    }
}

void HomeScene::drawVersionHistory(Framebuffer& fb, Recti area) {
    if (area.w <= 20 || area.h <= 30) return;

    font5x7::drawText(fb, area.x + 10, area.y + 8, "VERSIONES", kWhite, 1);

    const auto& vers = m_history.versions;
    if (vers.empty()) {
        font5x7::drawText(fb, area.x + 10, area.y + 26, "sin datos", kDim, 1);
        return;
    }

    // De la mas VIEJA arriba a la JUGABLE abajo. Si no caben todas, se muestran
    // las ultimas: interesa mas lo reciente que el origen.
    const i32 filaH = 26;
    const i32 dispo = std::max(1, (area.h - 24) / filaH);
    const size_t desde = (static_cast<i32>(vers.size()) > dispo)
        ? vers.size() - static_cast<size_t>(dispo) : 0;

    i32 y = area.y + 24;
    for (size_t i = desde; i < vers.size(); ++i) {
        const bool jugable = (i + 1 == vers.size());
        const Recti fila{ area.x + 8, y, std::max(1, area.w - 16), filaH - 4 };

        // La jugable resalta; las archivadas quedan traslucidas sobre el rojo.
        fb.fillRect(fila, jugable ? Color{0, 0, 0, 150} : Color{0, 0, 0, 70});
        if (jugable) fb.drawRect(fila, kOk, 1);

        font5x7::drawText(fb, fila.x + 6, fila.y + 4, vers[i].label.c_str(),
                          jugable ? kWhite : kGlyph, 1);

        const std::string fecha = history::formatDate(vers[i].at);
        font5x7::drawText(fb, fila.x + 34, fila.y + 4, fecha.c_str(), kGlyph, 1);

        const char* estado = jugable ? "JUGABLE" : "archivada";
        font5x7::drawText(fb, fila.x + 6, fila.y + 13, estado,
                          jugable ? kOk : kDim, 1);

        // Tamano de la copia archivada, a la derecha.
        if (vers[i].bytes > 0) {
            const std::string tam = history::formatBytes(vers[i].bytes);
            const i32 tw = font5x7::measure(tam.c_str(), 1);
            if (tw + 40 < fila.w)
                font5x7::drawText(fb, fila.x + fila.w - tw - 6, fila.y + 13,
                                  tam.c_str(), kGlyph, 1);
        }
        y += filaH;
    }

    // Aviso de cuantas quedan fuera, para que no parezca que se perdieron.
    if (desde > 0) {
        char mas[32];
        std::snprintf(mas, sizeof(mas), "+%zu anteriores", desde);
        font5x7::drawText(fb, area.x + 10, area.y + area.h - 12, mas, kGlyph, 1);
    }
}

void HomeScene::drawLibrarySidePanels(Framebuffer& fb, Recti area) {
    ensureHistoryLoaded();

    // Sin juego seleccionado no hay historial que mostrar: se deja la franja
    // limpia con una invitacion, en vez de dos cuadros vacios.
    if (m_historyOf.empty()) {
        const char* msg = "Elige un juego";
        const i32 mw = font5x7::measure(msg, 1);
        if (mw < area.w)
            font5x7::drawText(fb, area.x + (area.w - mw) / 2,
                              area.y + area.h / 2, msg, kWhite, 1);
        return;
    }

    // Las dos secciones se reparten la franja a lo alto, con un hueco entre
    // ellas por el que se ve el rojo y las separa sin necesidad de una linea.
    const i32 margen = 12;
    const i32 hueco  = 12;
    const i32 dispo  = area.h - margen * 2 - hueco;
    if (dispo < 60) return;   // franja demasiado corta para dos secciones

    const i32 mitad = dispo / 2;
    m_priceTimelineRect = Recti{ area.x + margen, area.y + margen,
                                 std::max(1, area.w - margen * 2), mitad };
    m_versionHistRect   = Recti{ area.x + margen,
                                 area.y + margen + mitad + hueco,
                                 std::max(1, area.w - margen * 2), mitad };

    // Fondo oscuro traslucido: el texto blanco sobre el rojo pleno se lee mal.
    fb.fillRect(m_priceTimelineRect, Color{0, 0, 0, 120});
    fb.fillRect(m_versionHistRect,   Color{0, 0, 0, 120});

    drawPriceTimeline(fb, m_priceTimelineRect);
    drawVersionHistory(fb, m_versionHistRect);
}

// =========================================================================
//  Modo Desarrollador
// =========================================================================

i32 HomeScene::librarySideWidthFor(i32 fbWidth) {
    return librarySideWidth(fbWidth);
}

i32 HomeScene::priceUsdCents() const {
    // El deslizador es continuo, pero el dinero no: se redondea al centavo
    // para que el valor guardado sea exactamente el que se muestra.
    //
    // El carril va de GRATIS (izquierda) al maximo (derecha), que es donde
    // estan sus etiquetas y el orden natural de menos a mas.
    const f32 t = std::clamp(m_priceT, 0.0f, 1.0f);
    return static_cast<i32>(t * devcatalog::kMaxUsdCents + 0.5f);
}

void HomeScene::layoutDev(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    // --- columna de botones, pegada al borde izquierdo ---
    const i32 colY = top + 40;   // deja aire bajo el menu

    // El alto de cada boton y su hueco se encogen si los cinco no caben.
    //
    // Con cinco botones ya no sobra sitio: a lo alto de 600 px entran justos,
    // y por debajo de eso se saldrian por abajo. Antes, con cuatro y medidas
    // fijas, el ultimo se escapaba de la ventana en cuanto se encogia; esto
    // reparte lo que haya y deja de depender de que la ventana sea grande.
    const i32 dispCol = std::max(1, fbH - colY - kDevColMargin);
    const i32 idealH  = kDevBtnH * kDevButtons + kDevBtnGap * (kDevButtons - 1);

    i32 btnH = kDevBtnH;
    i32 btnGap = kDevBtnGap;
    if (idealH > dispCol) {
        // Se reparte proporcionalmente, con un minimo legible: por debajo de
        // 26 px de alto la etiqueta ya no cabe ni a escala 1.
        const f32 k = static_cast<f32>(dispCol) / static_cast<f32>(idealH);
        btnH   = std::max(26, static_cast<i32>(kDevBtnH * k));
        btnGap = std::max(4,  static_cast<i32>(kDevBtnGap * k));
    }

    for (i32 i = 0; i < kDevButtons; ++i) {
        m_devBtnRects[i] = Recti{ kDevColMargin,
                                  colY + i * (btnH + btnGap),
                                  std::min(kDevBtnW, std::max(1, fbW / 3)),
                                  btnH };
    }


    // --- mini-seccion cuadrada, a la derecha de la columna ---
    // Cuadrada de verdad: el lado es el minimo entre el espacio horizontal
    // libre y el vertical, asi nunca se deforma ni se sale de la ventana.
    //
    // Arranca justo tras la columna: el hueco que reservaba el rombo ahora lo
    // aprovecha el panel, que sale mas grande.
    const i32 panelX0 = m_devBtnRects[0].x + m_devBtnRects[0].w + kDevPanelPad;
    const i32 dispW = std::max(0, fbW - panelX0 - kDevColMargin);
    const i32 dispH = std::max(0, fbH - colY - kDevColMargin);
    const i32 lado  = std::max(0, std::min(dispW, dispH));
    m_devPanelRect = Recti{ panelX0, colY, lado, lado };

    // --- LudoUpdates: ocupa la zona central cuando no hay panel abierto ---
    // Toma TODO el ancho libre, no el lado del cuadrado: es un tablon de
    // texto y cuanto mas ancho, menos renglones parte.
    m_ludoRect = Recti{ panelX0, colY, dispW, dispH };
    layoutLudoUpdates(m_ludoRect);

    // --- deslizador de precio, dentro de la mini-seccion ---
    // Va en el TERCIO superior, no centrado: la mitad de abajo la ocupa la
    // rejilla de ajustes, y el contador necesita aire justo debajo de la barra.
    const i32 margen = 34;
    m_priceTrackRect = Recti{ m_devPanelRect.x + margen,
                              m_devPanelRect.y + m_devPanelRect.h / 4,
                              std::max(1, m_devPanelRect.w - margen * 2),
                              kPriceTrackH };

    // El selector se mueve dentro del recorrido util (el ancho de la barrita
    // menos el suyo propio), de modo que en los extremos queda alineado con
    // los topes de la barra en vez de sobresalir.
    const i32 travel = std::max(1, m_priceTrackRect.w - kPriceKnobW);
    const i32 kx = m_priceTrackRect.x +
                   static_cast<i32>(std::clamp(m_priceT, 0.0f, 1.0f) * travel);
    m_priceKnobRect = Recti{ kx,
                             m_priceTrackRect.y + kPriceTrackH / 2 - kPriceKnobH / 2,
                             kPriceKnobW, kPriceKnobH };

    // --- ACEPTAR PRECIO, bajo la barrita ---
    // Se coloca en el hueco que queda entre el contador (que va justo debajo
    // de la barra) y la rejilla de ajustes (anclada al fondo). Si ese hueco
    // no da de si -- panel muy bajo --, el boton se retira en vez de
    // solaparse con lo que ya hay.
    {
        const i32 okW = std::max(1, std::min(m_devPanelRect.w - margen * 2, 200));
        const i32 okH = 34;
        // El contador ocupa 3 lineas bajo la barra (USD grande + las dos
        // conversiones). La reserva replica EXACTAMENTE los avances que hace
        // el dibujo -- ver el contador en drawDevPrice --, ni uno mas: debajo
        // van DOS botones y luego la rejilla, y cada pixel de mas aqui deja
        // al segundo boton sin sitio y lo hace desaparecer.
        const i32 tras = m_priceTrackRect.y + kPriceTrackH + 14 +
                         textr::lineHeight(2) + 2 +
                         (textr::lineHeight(1) + 1) * 2 + 8;
        const i32 altoRejilla = kPriceOptRows * kPriceOptH +
                                (kPriceOptRows - 1) * kPriceOptGap;
        const i32 techoRejilla = m_devPanelRect.y + m_devPanelRect.h - 22 -
                                 altoRejilla;

        m_priceOkRect = (tras + okH <= techoRejilla - 6)
            ? Recti{ m_devPanelRect.x + (m_devPanelRect.w - okW) / 2,
                     tras, okW, okH }
            : Recti{};

        // --- MANTENER PRECIOS POR DEFECTO, bajo ACEPTAR PRECIO ---
        // Mas estrecho y bajo que el de aceptar: es un atajo, no la accion
        // principal de la seccion.
        const i32 defH = 24;
        const i32 defY = tras + okH + 5;
        m_priceDefRect = (m_priceOkRect.w > 0 && defY + defH <= techoRejilla - 6)
            ? Recti{ m_devPanelRect.x + (m_devPanelRect.w - okW) / 2,
                     defY, okW, defH }
            : Recti{};

        // --- PRECIO A ESCRITO, sobre el carril ---
        // Va arriba porque es una alternativa al deslizador, no un paso
        // posterior: se elige una via o la otra antes de aceptar.
        const i32 escW = std::max(1, std::min(m_devPanelRect.w - margen * 2, 150));
        const i32 escH = 22;
        m_priceTypeRect = Recti{
            m_devPanelRect.x + m_devPanelRect.w - margen - escW,
            m_priceTrackRect.y - 40, escW, escH };

        // El cuadro de escritura y su boton de confirmar salen bajo el boton,
        // superpuestos al carril: solo existen con el cuadro abierto.
        if (m_priceTypeOpen) {
            const i32 cw = std::max(1, m_devPanelRect.w - margen * 2);
            const i32 cx = m_devPanelRect.x + margen;
            const i32 cy = m_priceTypeRect.y + escH + 6;
            const i32 okw = 90;
            m_priceTypeFieldRect = Recti{ cx, cy, std::max(1, cw - okw - 8), 30 };
            m_priceTypeOkRect    = Recti{ cx + cw - okw, cy, okw, 30 };
        } else {
            m_priceTypeFieldRect = m_priceTypeOkRect = Recti{};
        }

        // Los dos mini-botones salen a los lados, bajo el anterior.
        if (m_priceDefOpen && m_priceDefRect.w > 0) {
            const i32 miniH = 22;
            const i32 miniW = (m_priceDefRect.w - 8) / 2;
            const i32 miniY = m_priceDefRect.y + defH + 4;
            m_priceDefYes = Recti{ m_priceDefRect.x, miniY, miniW, miniH };
            m_priceDefNo  = Recti{ m_priceDefRect.x + miniW + 8, miniY,
                                   miniW, miniH };
        } else {
            m_priceDefYes = m_priceDefNo = Recti{};
        }
    }

    // La rejilla se coloca aqui tambien, y no solo al dibujar: update() corre
    // antes que render(), asi que sin esto los botones no tendrian rectangulo
    // en el primer frame y el primer clic se perderia.
    layoutPriceOptions(m_devPanelRect);
    // Por lo mismo, el formulario de subida se coloca antes de que nadie lo
    // pulse. Es barato: solo aritmetica de rectangulos.
    layoutUpload(m_devPanelRect);
}

// ===========================================================================
//  Formulario de SUBIR JUEGO
// ===========================================================================

i32 HomeScene::uploadGenreCount() { return kUpGenreCount; }

std::string HomeScene::uploadGenreLabel(i32 i) {
    if (i < 0 || i >= kUpGenreCount) return {};
    return std::string("#") + kUpGenres[i];
}

i32 HomeScene::uploadFps() const {
    const f32 t = std::clamp(m_upFpsT, 0.0f, 1.0f);
    return kUpFpsMin + static_cast<i32>(std::lround(t * (kUpFpsMax - kUpFpsMin)));
}

i32 HomeScene::uploadAge() const {
    const f32 t = std::clamp(m_upAgeT, 0.0f, 1.0f);
    return kUpAgeMin + static_cast<i32>(std::lround(t * (kUpAgeMax - kUpAgeMin)));
}

i32 HomeScene::uploadAgeTop() const {
    const f32 t = std::clamp(m_upAgeMaxT, 0.0f, 1.0f);
    const i32 n = kUpAgeMin +
                  static_cast<i32>(std::lround(t * (kUpAgeMax - kUpAgeMin)));
    // La maxima nunca queda por debajo de la minima. Se corrige AQUI y no
    // solo al arrastrar: las tres vias de fijar la edad (a mano, tramos,
    // selector AI) tocan los dos valores, y asi ninguna puede dejar la
    // franja del reves.
    return std::max(n, uploadAge());
}

// ---------------------------------------------------------------------------
//  EDAD RECOMENDADA: los tramos y las propuestas
// ---------------------------------------------------------------------------
namespace {
/// Un par de edades con su etiqueta ya escrita.
struct TramoEdad { const char* txt; i32 desde; i32 hasta; };

/// Los OCHO tramos fijos. Siguen las franjas que de verdad usan las
/// clasificaciones por edades (PEGI, ESRB) y los grupos de publico habituales,
/// en vez de repartir 4..90 en ocho partes iguales -- que daria cortes sin
/// sentido como "25 - 35".
const TramoEdad kTramos[] = {
    { "4 - 7 AÑOS",    4,  7  },   // primera infancia
    { "7 - 12 AÑOS",   7,  12 },   // infantil
    { "12 - 16 AÑOS",  12, 16 },   // adolescente
    { "16 - 18 AÑOS",  16, 18 },   // juvenil
    { "18 - 25 AÑOS",  18, 25 },   // adulto joven
    { "20 - 35 AÑOS",  20, 35 },   // el ejemplo que pedia el encargo
    { "35 - 50 AÑOS",  35, 50 },   // adulto
    { "50 - 90 AÑOS",  50, 90 },   // senior
};

/// Las NUEVE propuestas del selector AI. Son tramos mas finos y con nombre,
/// porque aqui lo que se ofrece no es un rango a secas sino una lectura del
/// publico al que apunta el juego.
const TramoEdad kPropuestasAi[] = {
    { "6 - 10  familiar",        6,  10 },
    { "9 - 14  escolar",         9,  14 },
    { "13 - 17 adolescente",     13, 17 },
    { "15 - 21 juvenil",         15, 21 },
    { "18 - 24 universitario",   18, 24 },
    { "20 - 35 adulto joven",    20, 35 },
    { "25 - 40 veterano",        25, 40 },
    { "30 - 55 nostalgico",      30, 55 },
    { "40 - 70 publico amplio",  40, 70 },
};
static_assert(sizeof(kTramos) / sizeof(kTramos[0]) ==
              static_cast<size_t>(HomeScene::ageRangeCount()),
              "la tabla de tramos y kAgeRanges deben coincidir");
static_assert(sizeof(kPropuestasAi) / sizeof(kPropuestasAi[0]) ==
              static_cast<size_t>(HomeScene::ageAiCount()),
              "la tabla del selector AI y kAgeAiOpts deben coincidir");
} // namespace

const char* HomeScene::ageRangeLabel(i32 i) {
    return (i >= 0 && i < kAgeRanges) ? kTramos[i].txt : "";
}

bool HomeScene::ageRangeValues(i32 i, i32& desde, i32& hasta) {
    if (i < 0 || i >= kAgeRanges) return false;
    desde = kTramos[i].desde;
    hasta = kTramos[i].hasta;
    return true;
}

const char* HomeScene::ageAiLabel(i32 i) {
    return (i >= 0 && i < kAgeAiOpts) ? kPropuestasAi[i].txt : "";
}

bool HomeScene::ageAiValues(i32 i, i32& desde, i32& hasta) {
    if (i < 0 || i >= kAgeAiOpts) return false;
    desde = kPropuestasAi[i].desde;
    hasta = kPropuestasAi[i].hasta;
    return true;
}

bool HomeScene::applyManualAge() {
    const std::string s = m_ageManualField.text();
    if (s.empty()) {
        m_ageMsg    = "Escribe la edad minima recomendada.";
        m_ageMsgErr = true;
        return false;
    }

    // Se lee a mano y no con atoi: atoi devuelve 0 tanto para "0" como para
    // "hola", y aqui esa diferencia importa -- una cifra fuera de rango no es
    // lo mismo que algo que ni siquiera es un numero.
    i32 n = 0;
    for (const char c : s) {
        if (c == ' ') continue;
        if (c < '0' || c > '9') {
            m_ageMsg    = "Solo numeros: escribe la edad en años.";
            m_ageMsgErr = true;
            return false;
        }
        n = n * 10 + (c - '0');
        if (n > 999) break;          // corta antes de desbordar
    }

    if (n < kUpAgeMin || n > kUpAgeMax) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "La edad va de %d a %d años.",
                      kUpAgeMin, kUpAgeMax);
        m_ageMsg    = buf;
        m_ageMsgErr = true;
        return false;
    }

    m_upAgeT = static_cast<f32>(n - kUpAgeMin) /
               static_cast<f32>(kUpAgeMax - kUpAgeMin);
    // Lo escrito es la MINIMA. Si la maxima quedara por debajo, sube con
    // ella: una franja invertida no significa nada.
    if (m_upAgeMaxT < m_upAgeT) m_upAgeMaxT = m_upAgeT;
    m_ageRangeSel = -1;     // deja de haber tramo elegido: manda lo escrito
    m_ageAiPick   = -1;

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Edad fijada en %d años.", n);
    m_ageMsg    = buf;
    m_ageMsgErr = false;
    return true;
}

void HomeScene::pickAgeRange(i32 i) {
    if (i < 0 || i >= kAgeRanges) return;
    m_ageRangeSel = i;
    m_ageAiPick   = -1;

    // Un tramo son DOS edades, y ahora la ficha guarda las dos: se fijan los
    // dos carriles de una vez. Antes solo entraba la menor porque no habia
    // donde poner la otra.
    const i32 n = kTramos[i].desde;
    const i32 x = kTramos[i].hasta;
    m_upAgeT    = static_cast<f32>(n - kUpAgeMin) /
                  static_cast<f32>(kUpAgeMax - kUpAgeMin);
    m_upAgeMaxT = static_cast<f32>(x - kUpAgeMin) /
                  static_cast<f32>(kUpAgeMax - kUpAgeMin);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Tramo %s aplicado.", kTramos[i].txt);
    m_ageMsg    = buf;
    m_ageMsgErr = false;
}

void HomeScene::rollAgeAi() {
    // Generador propio (xorshift) en vez de rand(): rand() arrastra estado
    // global, y con semilla propia el sorteo se puede reproducir en una
    // prueba. La semilla avanza en cada tirada, asi que pulsar dos veces da
    // propuestas distintas.
    u32 v = m_ageAiSeed;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    m_ageAiSeed = v;

    m_ageAiPick   = static_cast<i32>(v % static_cast<u32>(kAgeAiOpts));
    m_ageRangeSel = -1;
    m_ageAiSpin   = 0.9;        // el parpadeo del sorteo

    const i32 n = kPropuestasAi[m_ageAiPick].desde;
    const i32 x = kPropuestasAi[m_ageAiPick].hasta;
    m_upAgeT    = static_cast<f32>(n - kUpAgeMin) /
                  static_cast<f32>(kUpAgeMax - kUpAgeMin);
    m_upAgeMaxT = static_cast<f32>(x - kUpAgeMin) /
                  static_cast<f32>(kUpAgeMax - kUpAgeMin);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "Propuesta: %s (%d a %d).",
                  kPropuestasAi[m_ageAiPick].txt, n, x);
    m_ageMsg    = buf;
    m_ageMsgErr = false;
}

Color HomeScene::uploadAgeBackground() const {
    // El fondo de la seccion viaja de azul cielo (4 anos) a violeta intenso
    // (90): el color mismo dice para quien es el juego, sin leer el numero.
    const f32 t = std::clamp(m_upAgeT, 0.0f, 1.0f);
    auto lerp = [&](u8 a, u8 b) {
        return static_cast<u8>(std::lround(a + (b - a) * t));
    };
    return Color{ lerp(kAgeSky.r, kAgeViolet.r),
                  lerp(kAgeSky.g, kAgeViolet.g),
                  lerp(kAgeSky.b, kAgeViolet.b) };
}

void HomeScene::toggleUploadGenre(i32 i) {
    if (i < 0 || i >= kUpGenreCount) return;

    const auto it = std::find(m_upGenres.begin(), m_upGenres.end(), i);
    if (it != m_upGenres.end()) m_upGenres.erase(it);
    else                       m_upGenres.push_back(i);

    // El campo 3 refleja lo marcado: siempre empieza por '#' y encadena las
    // etiquetas en el orden en que se eligieron.
    std::string txt;
    for (const i32 g : m_upGenres) {
        if (!txt.empty()) txt += " ";
        txt += uploadGenreLabel(g);
    }
    if (txt.empty()) txt = "#";
    m_upGenreField.setText(txt);
}

i32 HomeScene::uploadMultiCount() { return kUpMultiCount; }

std::string HomeScene::uploadMultiLabel(i32 i) {
    if (i < 0 || i >= kUpMultiCount) return {};
    return std::string("@") + kUpMultiModes[i];
}

void HomeScene::toggleUploadMulti(i32 i) {
    if (i < 0 || i >= kUpMultiCount) return;

    const auto it = std::find(m_upMulti.begin(), m_upMulti.end(), i);
    if (it != m_upMulti.end()) m_upMulti.erase(it);
    else                       m_upMulti.push_back(i);

    // El campo 6 refleja lo marcado. Mismo mecanismo que los generos, pero
    // con '@' como caracter inicial.
    std::string txt;
    for (const i32 m : m_upMulti) {
        if (!txt.empty()) txt += " ";
        txt += uploadMultiLabel(m);
    }
    if (txt.empty()) txt = "@";
    m_upMultiField.setText(txt);
}

void HomeScene::layoutUpload(Recti area) {
    if (area.w <= 40 || area.h <= 40) {
        for (i32 i = 0; i < kUploadSlots; ++i) m_upRects[i] = Recti{};
        m_upDropRect = m_upPanelRect = m_upPublishRect = m_upSaveRect = Recti{};
        return;
    }

    // --- campo 1 y boton PUBLICAR, compartiendo la fila de arriba ---
    // El nombre encabeza el formulario (es el titulo del juego) y PUBLICAR va
    // a su derecha: la accion final queda junto al dato que la identifica, y
    // no compite por sitio con las dos columnas.
    const i32 pubW  = std::max(1, std::min(110, area.w / 4));
    const i32 nameW = std::max(1, std::min(area.w - kUpMargin * 2 - pubW - kUpGap,
                                           320));
    const i32 filaW = nameW + kUpGap + pubW;
    const i32 filaX = area.x + (area.w - filaW) / 2;

    m_upRects[kUpName] = Recti{ filaX, area.y + kUpMargin, nameW, kUpNameH };
    m_upPublishRect    = Recti{ filaX + nameW + kUpGap, area.y + kUpMargin,
                                pubW, kUpNameH };

    // --- dos columnas de cinco, con la zona de subida entre ellas ---
    const i32 filasY0 = m_upRects[kUpName].y + kUpNameH + kUpGap * 2;
    const i32 dispH   = std::max(1, area.y + area.h - kUpMargin - filasY0);
    // Alto de cada boton: lo que queda repartido entre cinco filas.
    const i32 btnH = std::max(1, (dispH - kUpGap * (kUpPerSide - 1)) / kUpPerSide);

    const i32 dropW = std::min(kUpDropW, std::max(1, area.w / 4));
    const i32 colW  = std::max(1, (area.w - kUpMargin * 2 - dropW - kUpGap * 2) / 2);

    const i32 izqX = area.x + kUpMargin;
    const i32 derX = izqX + colW + kUpGap + dropW + kUpGap;

    // Orden de cada columna, de arriba abajo. La izquierda lleva los campos
    // que describen el juego; la derecha, los permisos y el material grafico.
    // El 6 (multijugador) va justo debajo del 5 (edad), y el 7 (pirateria)
    // encabeza la columna derecha.
    const i32 colIzq[kUpPerSide] = { kUpDesc, kUpGenre, kUpPerf, kUpAge, kUpMulti };
    const i32 colDer[kUpPerSide] = { kUpPirate, kUpArt, kUpCustom, kUpEula, -1 };

    for (i32 f = 0; f < kUpPerSide; ++f) {
        const i32 y = filasY0 + f * (btnH + kUpGap);
        m_upRects[colIzq[f]] = Recti{ izqX, y, colW, btnH };
        if (colDer[f] >= 0) m_upRects[colDer[f]] = Recti{ derX, y, colW, btnH };
    }

    // Aqui iba GUARDAR Y SUBIR, bajo el EULA. Se retiro: hacia exactamente lo
    // mismo que PUBLICAR (los dos llamaban a submitUpload), asi que eran dos
    // botones para una sola accion. Queda el de arriba, junto al nombre, que
    // es donde empieza el formulario.
    //
    // El rectangulo se conserva VACIO en vez de borrar el miembro: el aviso
    // de "pon precio antes" se coloca a partir de el, y varias pruebas lo
    // consultan. Con w = 0 nada se dibuja ni responde al raton.
    m_upSaveRect = Recti{};

    // --- rectangulo vertical de subida, entre las dos columnas ---
    const i32 dropY = filasY0;
    const i32 dropH = std::max(1, (filasY0 + kUpPerSide * btnH +
                                   (kUpPerSide - 1) * kUpGap) - dropY);
    m_upDropRect = Recti{ izqX + colW + kUpGap, dropY, dropW, dropH };

    // --- panel del campo abierto ---
    // Se superpone al formulario, anclado dentro de la mini-seccion. Ocupa
    // casi toda la seccion porque los paneles llevan texto largo o listas.
    if (m_upOpen >= 0 && m_upOpen < kUploadSlots && m_upRects[m_upOpen].w > 0) {
        const i32 pw = std::max(1, area.w - kUpMargin * 2);
        // EDAD necesita mas alto que el resto: bajo sus DOS carriles (minima
        // y maxima) van tres botones y, desplegados, ocho tramos. Con el 0.62
        // comun la rejilla no cabia; con 0.80 cabia pero las celdas quedaban
        // a 26 px al añadirse el segundo carril, que se come 42.
        const f32 propAlto = (m_upOpen == kUpAge) ? 0.88f : 0.62f;
        const i32 ph = std::max(1, static_cast<i32>(area.h * propAlto));
        m_upPanelRect = Recti{ area.x + kUpMargin,
                               area.y + area.h - kUpMargin - ph, pw, ph };

        // Los dos deslizadores comparten geometria: carril a lo ancho del
        // panel y selector que recorre el hueco util.
        if (m_upOpen == kUpPerf || m_upOpen == kUpAge) {
            const i32 m = 26;

            // EDAD lleva tres opciones debajo, asi que su carril sube: con el
            // a media altura solo quedaba la mitad del panel para los
            // botones, los ocho tramos y el aviso, y todo salia diminuto.
            //
            // Se deja arriba justo lo que ocupa el numero grande (que se
            // dibuja sobre el carril) mas su aire. RENDIMIENTO no cambia: no
            // tiene nada debajo y centrado se lee mejor.
            const i32 carrilY = (m_upOpen == kUpAge)
                ? m_upPanelRect.y + textr::glyphHeight(3) + 20
                : m_upPanelRect.y + m_upPanelRect.h / 2;

            m_upTrackRect = Recti{ m_upPanelRect.x + m, carrilY,
                                   std::max(1, m_upPanelRect.w - m * 2),
                                   kUpTrackH };
            const f32 t = (m_upOpen == kUpPerf) ? m_upFpsT : m_upAgeT;
            const i32 travel = std::max(1, m_upTrackRect.w - kUpKnobW);
            m_upKnobRect = Recti{
                m_upTrackRect.x +
                    static_cast<i32>(std::clamp(t, 0.0f, 1.0f) * travel),
                m_upTrackRect.y + kUpTrackH / 2 - kUpKnobH / 2,
                kUpKnobW, kUpKnobH };

            // EDAD lleva un SEGUNDO carril para la maxima, justo debajo del
            // primero. Dos carriles y no uno con dos selectores: con ambos en
            // el mismo riel, al juntarse los dos extremos es imposible saber
            // cual se esta agarrando.
            if (m_upOpen == kUpAge) {
                const i32 sep = kUpKnobH + 16;
                m_upTrack2Rect = Recti{ m_upTrackRect.x,
                                        m_upTrackRect.y + sep,
                                        m_upTrackRect.w, kUpTrackH };
                m_upKnob2Rect = Recti{
                    m_upTrack2Rect.x +
                        static_cast<i32>(std::clamp(m_upAgeMaxT, 0.0f, 1.0f) * travel),
                    m_upTrack2Rect.y + kUpTrackH / 2 - kUpKnobH / 2,
                    kUpKnobW, kUpKnobH };
            } else {
                m_upTrack2Rect = m_upKnob2Rect = Recti{};
            }
        } else {
            m_upTrackRect = m_upKnobRect = Recti{};
            m_upTrack2Rect = m_upKnob2Rect = Recti{};
        }

        // Las tres vias de fijar la edad cuelgan del carril, asi que se
        // colocan despues de el.
        layoutAgeOptions(m_upPanelRect);

        // Los campos 3 y 6 llevan una lista con scroll bajo su cuadro de
        // texto; el panel reserva la mitad inferior para ella.
        if (m_upOpen == kUpGenre || m_upOpen == kUpMulti) {
            const i32 listaY = m_upPanelRect.y + 74;
            const Recti lr{ m_upPanelRect.x + 8, listaY,
                            std::max(1, m_upPanelRect.w - 16),
                            std::max(1, m_upPanelRect.y +
                                     m_upPanelRect.h - 10 - listaY) };
            if (m_upOpen == kUpGenre) m_upGenrePanel.setRect(lr);
            else                      m_upMultiPanel.setRect(lr);
        }

        // --- avisos con aceptar/rechazar (campos 7 y 9) ---
        // El texto ocupa la parte de arriba con scroll propio, y los dos
        // botones se anclan al fondo: asi siguen alcanzables por larga que
        // sea la lectura.
        if (m_upOpen == kUpPirate || m_upOpen == kUpCustom) {
            const i32 btnY = m_upPanelRect.y + m_upPanelRect.h - 12 - kUpConsentBtnH;
            const i32 textoY = m_upPanelRect.y + 30;
            m_upNoticePanel.setRect({ m_upPanelRect.x + 10, textoY,
                                      std::max(1, m_upPanelRect.w - 20),
                                      std::max(1, btnY - 12 - textoY) });

            // Dos botones a lo ancho, con hueco en medio.
            const i32 hueco = 14;
            const i32 bw = std::max(1, (m_upPanelRect.w - 20 - hueco) / 2);
            m_upYesRect = Recti{ m_upPanelRect.x + 10, btnY, bw, kUpConsentBtnH };
            m_upNoRect  = Recti{ m_upPanelRect.x + 10 + bw + hueco, btnY,
                                 bw, kUpConsentBtnH };
        } else {
            m_upYesRect = m_upNoRect = Recti{};
        }

        // --- campo 8: rectangulo largo donde va la imagen promocional ---
        // Proporcion apaisada (16:6), que es como se ve una cabecera de
        // tienda: ancha y baja.
        if (m_upOpen == kUpArt) {
            const i32 dw = std::max(1, m_upPanelRect.w - 24);
            const i32 dh = std::max(1, std::min(dw * 6 / 16,
                                                m_upPanelRect.h - 90));
            m_upArtDropRect = Recti{ m_upPanelRect.x + 12,
                                     m_upPanelRect.y + 36, dw, dh };
        } else {
            m_upArtDropRect = Recti{};
        }
    } else {
        m_upPanelRect = m_upTrackRect = m_upKnobRect = Recti{};
        m_upYesRect = m_upNoRect = m_upArtDropRect = Recti{};
    }
}

void HomeScene::updateDev(Engine& engine, f64 dt) {
    Input& in = engine.input();

    // Reloj de las animaciones de la seccion (el remolino de ACEPTAR PRECIO).
    m_devAnimClock += dt;

    // LudoUpdates se VE siempre, pero solo responde a la rueda cuando no hay
    // mini-seccion abierta. Con una abierta, el raton esta trabajando sobre
    // ella y desplazar el fondo por debajo seria desconcertante.
    //
    // El reloj del degradado del titulo si corre en ambos casos: si no, el
    // color del rotulo se congelaria al abrir un panel.
    m_ludoClock += dt;
    if (m_devPanel == kDevNone) updateLudoUpdates(engine, dt);

    // El aviso de "elige moneda y paises" se agota solo.
    if (m_priceOptWarn > 0.0) {
        m_priceOptWarn -= dt;
        if (m_priceOptWarn < 0.0) m_priceOptWarn = 0.0;
    }

    // Palomita de precio aceptado. Al completarse lleva a SUBIR JUEGO, que es
    // lo que toca hacer despues de fijar el precio.
    if (m_priceOkAnimOn) {
        m_priceOkAnim += dt;
        if (m_priceOkAnim >= kPriceOkAnimDur) {
            m_priceOkAnimOn = false;
            m_priceOkAnim   = 0.0;
            m_devPanel      = kDevUpload;
            m_devMsg        = "Precio listo. Ya puedes subir el juego.";
            m_devMsgErr     = false;

            // El formulario arranca con la ficha ya publicada, igual que al
            // abrirlo a mano desde el boton de la columna.
            if (m_upEditing.empty() && !m_devGames.empty()) {
                const auto& g = m_devGames.front();
                m_upNameField.setText(g.name);
                loadListing(g.listing);
                m_upEditing = g.name;
            }
        }
        return;   // durante la palomita no se atiende nada mas
    }

    if (!m_devGamesLoaded) {
        m_devGames = devcatalog::load(kDevCatalogFile);
        m_devGamesLoaded = true;
    }

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
    };

    // --- deslizador de precio: tiene prioridad mientras se arrastra ---
    // Se atiende antes que los botones porque un arrastre que sale del carril
    // no debe activar lo que haya debajo del cursor.
    if (m_priceDragging) {
        if (in.mouseDown(0)) {
            const i32 travel = std::max(1, m_priceTrackRect.w - kPriceKnobW);
            const i32 rel = std::clamp(lx - m_priceTrackRect.x - kPriceKnobW / 2,
                                       0, travel);
            m_priceT = static_cast<f32>(rel) / static_cast<f32>(travel);
        }
        if (in.mouseReleased(0)) {
            m_priceDragging = false;
            // Al soltar se guarda el precio del juego vigente, si lo hay.
            if (!m_devGames.empty()) {
                const std::string juego = m_devGames.front().name;
                std::string err;
                if (devcatalog::setPrice(kDevCatalogFile, juego,
                                         devcatalog::Price{ priceUsdCents() }, err)) {
                    m_devGamesLoaded = false;   // recargar con el precio nuevo
                    // El cambio queda anotado en la linea de tiempo de la
                    // biblioteca, que es donde se ve como ha ido variando.
                    history::notePriceChange(kHistoryFile, juego,
                                             priceUsdCents(), err);
                    m_historyLoaded = false;
                } else {
                    m_devMsg = err;
                    m_devMsgErr = true;
                }
            }
        }
        return;
    }

    // --- formulario de SUBIR JUEGO ---
    // Se atiende antes que los botones de la columna: mientras esta abierto,
    // sus campos y su zona de subida se quedan el raton y el teclado.
    // --- ACTUALIZAR JUEGO ---
    // --- ELIMINAR JUEGO ---
    if (m_devPanel == kDevDelete) {
        layoutDevDelete(m_devPanelRect);
        updateDevDelete(engine, dt);
        // Igual que ACTUALIZAR: solo se queda el evento dentro de su panel,
        // para que los botones de la columna sigan respondiendo.
        if (dentro(m_devPanelRect)) return;
    }

    if (m_devPanel == kDevUpdate) {
        layoutDevUpdate(m_devPanelRect);
        updateDevUpdate(engine, dt);

        // Al terminar un guardado, esa llamada cambia sola de mini-seccion.
        // Si pasa, el frame acaba aqui: los bloques de abajo miran
        // `m_devPanel` y atenderian la seccion NUEVA con la geometria de la
        // vieja, que aun no se ha recalculado.
        if (m_devPanel != kDevUpdate) return;

        // Solo se queda el evento si el raton esta DENTRO de su mini-seccion.
        // Con un return incondicional, los botones de la columna dejaban de
        // responder en cuanto se abria ACTUALIZAR JUEGO.
        if (dentro(m_devPanelRect)) return;
    }

    if (m_devPanel == kDevCommunity) {
        layoutDevCommunity(m_devPanelRect);
        updateDevCommunity(engine, dt);
        // Con un ajuste abierto se queda TODO el teclado: se esta escribiendo
        // la descripcion o las reglas, y las teclas no deben llegar a nadie
        // mas. Cerrado, solo se queda lo que caiga dentro del panel.
        if (m_comOpen >= 0 || dentro(m_devPanelRect)) return;
    }

    if (m_devPanel == kDevUpload) {
        const bool habiaPanel = (m_upOpen >= 0);
        updateUpload(engine, dt);
        // Si el formulario consumio el clic (panel abierto, deslizador
        // agarrado o cursor sobre uno de sus elementos), no sigue bajando.
        if (habiaPanel || m_upOpen >= 0 || m_upFpsDrag || m_upAgeDrag ||
            m_upHover >= 0 || m_upDropHover)
            return;
    }

    // --- rejilla de ajustes de publicacion ---
    // Se atiende antes que el deslizador y los botones de la columna: con una
    // lista desplegada, el clic es suyo y de nadie mas.
    if (m_devPanel == kDevPrice) {
        const bool listaAbierta = (m_priceOptOpen >= 0);
        updatePriceOptions(engine);
        // Si habia (o hay) lista abierta, el clic ya se consumio ahi.
        if (listaAbierta || m_priceOptOpen >= 0) return;
        // Un clic sobre la rejilla tampoco debe llegar al deslizador.
        if (m_priceOptHover >= 0) return;
    }

    // --- PRECIO A ESCRITO ---
    // Antes que el deslizador: su cuadro se superpone al carril, y sin esto
    // escribir en el moveria la barra de debajo.
    if (m_devPanel == kDevPrice) {
        m_priceTypeHover   = dentro(m_priceTypeRect);
        m_priceTypeOkHover = m_priceTypeOpen && dentro(m_priceTypeOkRect);

        if (m_priceTypeOpen) {
            // El cuadro abierto se queda el teclado.
            m_priceTypeField.setFocused(true);
            m_priceTypeField.update(dt);

            if (in.mousePressed(0) && m_priceTypeField.contains(lx, ly))
                m_priceTypeField.onMouseDown(lx, ly);
            else if (in.mouseDown(0) && m_priceTypeField.focused())
                m_priceTypeField.onMouseDrag(lx, ly);

            const bool shift = in.keyDown(VK_SHIFT);
            const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);
            const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                                   VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };
            for (const u32 vk : teclas)
                if (in.keyRepeat(vk)) m_priceTypeField.onKey(vk, shift, ctrlAtajo);

            for (const wchar_t ch : in.charsTyped()) {
                if (ctrlAtajo || ch < 32) continue;
                m_priceTypeField.onChar(ch);
            }

            // Enter confirma, igual que el boton.
            if (in.keyPressed(VK_RETURN)) { applyTypedPrice(); return; }
        }

        if (in.mousePressed(0) && (m_priceTypeHover || m_priceTypeOkHover)) {
            m_priceTypeDown   = m_priceTypeHover;
            m_priceTypeOkDown = m_priceTypeOkHover;
            return;
        }
        if (in.mouseReleased(0)) {
            if (m_priceTypeOkDown && m_priceTypeOkHover) {
                applyTypedPrice();
                m_priceTypeDown = m_priceTypeOkDown = false;
                return;
            }
            if (m_priceTypeDown && m_priceTypeHover) {
                m_priceTypeOpen = !m_priceTypeOpen;
                if (m_priceTypeOpen) {
                    // Arranca con el precio que marque el deslizador: asi se
                    // ajusta sobre lo que ya hay en vez de empezar en blanco.
                    char cifra[32];
                    const i32 cents = priceUsdCents();
                    std::snprintf(cifra, sizeof(cifra), "%d.%02d",
                                  cents / 100, cents % 100);
                    m_priceTypeField.setText(cifra);
                    m_priceTypeMsg.clear();
                }
                m_priceTypeDown = m_priceTypeOkDown = false;
                return;
            }
            m_priceTypeDown = m_priceTypeOkDown = false;
        }

        // Con el cuadro abierto, nada mas de la seccion responde al raton.
        if (m_priceTypeOpen) return;
    }

    // --- MANTENER PRECIOS POR DEFECTO ---
    // Antes que ACEPTAR PRECIO: queda justo debajo y sus mini-botones caen
    // sobre la rejilla de ajustes.
    if (m_devPanel == kDevPrice) {
        m_priceDefHover    = dentro(m_priceDefRect);
        m_priceDefYesHover = m_priceDefOpen && dentro(m_priceDefYes);
        m_priceDefNoHover  = m_priceDefOpen && dentro(m_priceDefNo);

        if (in.mousePressed(0) &&
            (m_priceDefHover || m_priceDefYesHover || m_priceDefNoHover)) {
            m_priceDefDown = m_priceDefHover;
            return;
        }
        if (in.mouseReleased(0)) {
            if (m_priceDefYesHover) {
                acceptDefaultPriceOptions();
                m_priceDefDown = false;
                return;
            }
            if (m_priceDefNoHover) {
                // Negar cierra la pregunta y deja los ajustes como estaban:
                // habra que elegirlos a mano.
                m_priceDefOpen = false;
                m_devMsg    = "Elige los ajustes uno a uno.";
                m_devMsgErr = false;
                m_priceDefDown = false;
                return;
            }
            if (m_priceDefDown && m_priceDefHover) {
                m_priceDefOpen = !m_priceDefOpen;   // abrir o cerrar
                m_priceDefDown = false;
                return;
            }
            m_priceDefDown = false;
        }
    }

    // --- ACEPTAR PRECIO ---
    // Se atiende antes que el deslizador: el boton queda bajo el carril, y
    // sin esto un clic suyo tambien movaria la barra.
    m_priceOkHover = (m_devPanel == kDevPrice) && dentro(m_priceOkRect);
    if (m_devPanel == kDevPrice) {
        if (in.mousePressed(0) && m_priceOkHover) { m_priceOkDown = true; return; }
        if (in.mouseReleased(0) && m_priceOkDown) {
            const bool acepto = m_priceOkHover;
            m_priceOkDown = false;
            if (acepto) {
                // Moneda y pais son obligatorios: sin ellos el precio no
                // significa nada concreto para el jugador.
                if (!priceOptionsReady()) {
                    m_priceOptWarn = 1.2;   // tiembla y avisa
                    m_devMsg.clear();
                    return;
                }

                // Fija el precio del juego vigente, igual que soltar el
                // deslizador, pero de forma explicita.
                if (m_devGames.empty()) {
                    m_devMsg    = "Primero publica un juego con SUBIR JUEGO.";
                    m_devMsgErr = true;
                } else {
                    const std::string juego = m_devGames.front().name;
                    std::string err;
                    if (devcatalog::setPrice(kDevCatalogFile, juego,
                                             devcatalog::Price{ priceUsdCents() },
                                             err)) {
                        m_devGamesLoaded = false;
                        m_storeLoaded    = false;   // la tienda ve el precio ya
                        history::notePriceChange(kHistoryFile, juego,
                                                 priceUsdCents(), err);
                        m_historyLoaded = false;

                        std::string usd, mxn, eur;
                        devcatalog::formatPrice(priceUsdCents(), usd, mxn, eur);
                        m_devMsg    = "Precio aceptado: " + usd;
                        m_devMsgErr = false;

                        // El precio queda dado por bueno: SUBIR JUEGO ya no
                        // volvera a pedirlo.
                        m_priceAccepted = true;

                        // Arranca la palomita. Al acabar salta sola a SUBIR
                        // JUEGO: es el paso siguiente, y encadenarlo evita
                        // tener que buscar el boton a mano.
                        m_priceOkAnim   = 0.0;
                        m_priceOkAnimOn = true;
                    } else {
                        m_devMsg    = err;
                        m_devMsgErr = true;
                    }
                }
                return;
            }
        }
    }

    m_priceKnobHover = (m_devPanel == kDevPrice) && dentro(m_priceKnobRect);

    if (m_devPanel == kDevPrice && in.mousePressed(0)) {
        // Agarrar el selector, o saltar a donde se pulse dentro del carril.
        const bool enCarril = (lx >= m_priceTrackRect.x &&
                               lx < m_priceTrackRect.x + m_priceTrackRect.w &&
                               ly >= m_priceKnobRect.y &&
                               ly < m_priceKnobRect.y + m_priceKnobRect.h);
        if (m_priceKnobHover || enCarril) {
            m_priceDragging = true;
            const i32 travel = std::max(1, m_priceTrackRect.w - kPriceKnobW);
            const i32 rel = std::clamp(lx - m_priceTrackRect.x - kPriceKnobW / 2,
                                       0, travel);
            m_priceT = static_cast<f32>(rel) / static_cast<f32>(travel);
            return;
        }
    }

    // --- botones de la columna ---
    m_devHover = -1;
    for (i32 i = 0; i < kDevButtons; ++i)
        if (dentro(m_devBtnRects[i])) m_devHover = i;

    if (in.mousePressed(0) && m_devHover >= 0) m_devDown = m_devHover;

    if (in.mouseReleased(0)) {
        if (m_devDown >= 0 && m_devDown == m_devHover) {
            // Cada boton abre su mini-seccion; volver a pulsarlo la cierra.
            openDevPanel((m_devPanel == m_devDown) ? kDevNone : m_devDown);
        }
        m_devDown = -1;
    }
}

void HomeScene::openDevPanel(i32 panel) {
    m_devPanel = panel;
    m_devMsg.clear();

    // Al abrir Precios, el deslizador arranca en el precio guardado.
    if (m_devPanel == kDevPrice && !m_devGames.empty()) {
        m_priceT = static_cast<f32>(m_devGames.front().price.usdCents) /
                   static_cast<f32>(devcatalog::kMaxUsdCents);
        m_priceT = std::clamp(m_priceT, 0.0f, 1.0f);
    }

    // Al abrir SUBIR JUEGO, el formulario arranca con la ficha ya publicada:
    // sin esto, reabrirlo mostraria campos en blanco y publicar otra vez
    // pareceria crear un juego distinto.
    if (m_devPanel == kDevUpload && m_upEditing.empty() &&
        !m_devGames.empty()) {
        const auto& g = m_devGames.front();
        m_upNameField.setText(g.name);
        loadListing(g.listing);
        m_upEditing = g.name;
    }
}

void HomeScene::pickGameFolder(Engine& engine) {
    // Un juego es una CARPETA (ejecutable + recursos), no un archivo suelto:
    // es lo UNICO que la zona de subida acepta desde que se retiro el modo de
    // archivo. SHBrowseForFolder es la via sin dependencias para pedirla: la
    // alternativa moderna (IFileDialog con FOS_PICKFOLDERS) obliga a COM y a
    // enlazar ole32, y aqui no compensa.
    BROWSEINFOW bi{};
    bi.hwndOwner = engine.window().handle();
    bi.lpszTitle = L"Subir los archivos del juego (carpeta completa)";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;   // cancelar no es un error

    wchar_t ruta[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(pidl, ruta) != FALSE;
    CoTaskMemFree(pidl);   // el shell reserva el PIDL; liberarlo es del llamante
    if (!ok) return;

    m_upCodePath = ruta;
    m_devMsg     = "Carpeta recibida: " + nombreVisible(m_upCodePath);
    m_devMsgErr  = false;
}

void HomeScene::pickPromoArt(Engine& engine) {
    const std::wstring elegida = photo::pickFromGallery(engine.window().handle());
    if (elegida.empty()) return;   // cancelado

    // Se decodifica ya para poder previsualizarla en el propio panel: si el
    // archivo no sirve, se sabe aqui y no al publicar.
    std::vector<u32> px;
    i32 w = 0, h = 0;
    if (!photo::loadAnyImage(elegida, px, w, h) || w <= 0 || h <= 0) {
        m_upArtMsg = "No se pudo leer esa imagen.";
        m_devMsg    = m_upArtMsg;
        m_devMsgErr = true;
        return;
    }

    m_upArtPath   = elegida;
    m_upArtPixels = std::move(px);
    m_upArtW      = w;
    m_upArtH      = h;

    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "Imagen cargada (%dx%d)", w, h);
    m_upArtMsg  = buf;
    m_devMsg    = m_upArtMsg;
    m_devMsgErr = false;
}

bool HomeScene::applyTypedPrice() {
    const std::string& t = m_priceTypeField.text();

    // Se aceptan las formas en que la gente escribe dinero: "19.99", "19,99",
    // "$19.99", "20". Lo que no sea cifra, punto, coma o el simbolo se
    // rechaza en vez de interpretarse a medias.
    std::string limpio;
    i32 puntos = 0;
    for (const char c : t) {
        if (c == '$' || c == ' ') continue;
        if (c == '.' || c == ',') { limpio.push_back('.'); ++puntos; continue; }
        if (c < '0' || c > '9') {
            m_priceTypeMsg    = "Escribe solo numeros, por ejemplo 19.99";
            m_priceTypeMsgErr = true;
            return false;
        }
        limpio.push_back(c);
    }

    if (limpio.empty() || puntos > 1) {
        m_priceTypeMsg    = "Escribe una cifra valida, por ejemplo 19.99";
        m_priceTypeMsgErr = true;
        return false;
    }

    // Se parte en enteros y centavos a mano en vez de usar coma flotante:
    // 19.99 en double no es exactamente 1999 centavos, y el dinero no admite
    // ese error.
    const size_t pt = limpio.find('.');
    const std::string ent = (pt == std::string::npos) ? limpio : limpio.substr(0, pt);
    std::string cnt = (pt == std::string::npos) ? "" : limpio.substr(pt + 1);

    cnt.resize(2, '0');            // "9" -> "90", "" -> "00"
    if (cnt.size() > 2) cnt.resize(2);   // "999" -> "99", se descarta el resto

    const long long dolares = ent.empty() ? 0 : std::atoll(ent.c_str());
    const long long centavos = std::atoll(cnt.c_str());
    long long total = dolares * 100 + centavos;

    if (total > devcatalog::kMaxUsdCents) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "El maximo es %d.00 USD",
                      devcatalog::kMaxUsdCents / 100);
        m_priceTypeMsg    = buf;
        m_priceTypeMsgErr = true;
        return false;
    }

    // El deslizador se mueve a esa cifra: las dos formas de fijar el precio
    // quedan sincronizadas, y el resto de la seccion (contador, aceptar,
    // guardar) sigue leyendo de m_priceT como siempre.
    const f32 prop = static_cast<f32>(total) /
                     static_cast<f32>(devcatalog::kMaxUsdCents);
    m_priceT = std::clamp(prop, 0.0f, 1.0f);

    // Escribir la cifra Y confirmar es una decision explicita, asi que el
    // precio queda dado por bueno -- incluso si es 0 (gratis). Sin esto,
    // publicar un juego gratis seria imposible: el deslizador en cero no se
    // distingue de "no lo he tocado".
    m_priceAccepted = true;

    std::string usd, mxn, eur;
    devcatalog::formatPrice(static_cast<i32>(total), usd, mxn, eur);
    m_priceTypeMsg    = "Precio fijado: " + usd;
    m_priceTypeMsgErr = false;
    m_priceTypeOpen   = false;   // confirmar cierra el cuadro
    return true;
}

void HomeScene::acceptDefaultPriceOptions() {
    // Toma los seis ajustes tal y como estan y los marca como elegidos. El
    // valor no cambia: lo que cambia es que dejan de estar "sin contestar",
    // que es lo que impedia aceptar el precio.
    for (i32 i = 0; i < kPriceOptCount; ++i) m_priceOptSet[i] = true;
    m_priceDefOpen = false;
    m_devMsg    = "Ajustes por defecto aceptados.";
    m_devMsgErr = false;
}

bool HomeScene::priceOptionsReady() const {
    // Moneda y pais son los dos que deciden cuanto paga de verdad el jugador;
    // los otros cuatro ajustes tienen un valor por defecto razonable.
    return m_priceOptSet[kOptCurrency] && m_priceOptSet[kOptCountry];
}

void HomeScene::drawCheckmark(Framebuffer& fb, i32 cx, i32 cy,
                              i32 radius, f32 t) const {
    if (radius <= 0) return;
    const f32 p = std::clamp(t, 0.0f, 1.0f);

    // La animacion va en tres tramos encadenados, no a la vez: primero
    // aparece el disco, luego se traza la palomita, y al final el conjunto se
    // desvanece. Encadenarlos es lo que hace que se lea como un gesto y no
    // como tres cosas sueltas.
    const f32 tDisco = std::clamp(p / 0.30f, 0.0f, 1.0f);
    const f32 tTrazo = std::clamp((p - 0.25f) / 0.40f, 0.0f, 1.0f);
    const f32 tSalida= std::clamp((p - 0.80f) / 0.20f, 0.0f, 1.0f);

    // Rebote al aparecer: se pasa de tamano y vuelve. Un crecimiento lineal
    // se ve mecanico; este sobreimpulso es lo que da la sensacion de "pop".
    const f32 e = 1.0f - (1.0f - tDisco) * (1.0f - tDisco);     // suaviza el final
    const f32 rebote = 1.0f + 0.18f * std::sin(tDisco * 3.14159f);
    const i32 r = static_cast<i32>(std::lround(radius * e * rebote));
    if (r <= 0) return;

    // Opacidad: entra con el disco y sale al final.
    const f32 alfa = std::clamp(tDisco, 0.0f, 1.0f) * (1.0f - tSalida);
    const u8  a    = static_cast<u8>(std::lround(alfa * 255.0f));
    if (a == 0) return;

    // Disco verde con un anillo mas claro: el anillo separa la palomita del
    // fondo cuando cae sobre un boton de color.
    Color relleno = kOk;    relleno.a = a;
    Color anillo  = kNeonGreen; anillo.a = a;
    fb.fillCircle(cx, cy, r, relleno);
    fb.drawCircle(cx, cy, r, anillo, 2);

    if (tTrazo <= 0.0f) return;

    // --- trazo de la palomita, dibujandose solo ---
    // Dos segmentos: el corto baja a la izquierda, el largo sube a la
    // derecha. Se avanza por longitud acumulada para que la velocidad del
    // trazo sea constante entre ambos, en vez de acelerar en el cambio.
    const f32 x0 = cx - r * 0.42f, y0 = cy + r * 0.02f;
    const f32 x1 = cx - r * 0.12f, y1 = cy + r * 0.34f;
    const f32 x2 = cx + r * 0.46f, y2 = cy - r * 0.36f;

    const f32 l1 = std::sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
    const f32 l2 = std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
    const f32 total = std::max(0.001f, l1 + l2);
    const f32 hecho = tTrazo * total;

    Color trazo = kWhite; trazo.a = a;
    const i32 grosor = std::max(2, r / 6);

    /// Segmento grueso: se dibuja como una sucesion de discos, que es la
    /// forma simple de tener extremos redondeados sin un rasterizador de
    /// lineas con grosor.
    auto segmento = [&](f32 ax, f32 ay, f32 bx, f32 by, f32 frac) {
        const f32 f = std::clamp(frac, 0.0f, 1.0f);
        const i32 pasos = std::max(1, static_cast<i32>(
            std::sqrt((bx-ax)*(bx-ax) + (by-ay)*(by-ay))));
        const i32 hasta = static_cast<i32>(pasos * f);
        for (i32 i = 0; i <= hasta; ++i) {
            const f32 u = static_cast<f32>(i) / pasos;
            fb.fillCircle(static_cast<i32>(std::lround(ax + (bx-ax) * u)),
                          static_cast<i32>(std::lround(ay + (by-ay) * u)),
                          grosor / 2 + 1, trazo);
        }
    };

    segmento(x0, y0, x1, y1, hecho / l1);
    if (hecho > l1) segmento(x1, y1, x2, y2, (hecho - l1) / l2);
}

void HomeScene::drawSwirlButton(Framebuffer& fb, Recti r, f64 t,
                                bool hover, bool down,
                                const char* entero,
                                const char* l1, const char* l2) const {
    if (r.w <= 0 || r.h <= 0) return;

    // Relleno columna a columna: la fase depende de la posicion Y del tiempo,
    // asi que el verde y el violeta se arrastran por dentro del boton en vez
    // de latir los dos a la vez.
    for (i32 x = 0; x < r.w; ++x) {
        const f64 u = static_cast<f64>(x) / std::max(1, r.w - 1);
        const f64 fase = t * 1.9 + u * 6.4;
        // Dos senos de periodo distinto: el recorrido no es un barrido
        // regular, que es lo que delataria una interpolacion simple.
        const f64 s = std::sin(fase) * 0.6 +
                      std::sin(fase * 0.43 + t * 0.8) * 0.4;
        const f32 k = static_cast<f32>((s + 1.0) * 0.5);

        auto lerp = [&](u8 a, u8 b) {
            return static_cast<u8>(std::lround(a + (b - a) * k));
        };
        Color c{ lerp(kSaveGreen.r, kSaveViolet.r),
                 lerp(kSaveGreen.g, kSaveViolet.g),
                 lerp(kSaveGreen.b, kSaveViolet.b) };

        // Pulsado oscurece; el raton encima aviva.
        const f32 f = down ? 0.6f : (hover ? 1.25f : 1.0f);
        c = Color{ static_cast<u8>(std::clamp(c.r * f, 0.0f, 255.0f)),
                   static_cast<u8>(std::clamp(c.g * f, 0.0f, 255.0f)),
                   static_cast<u8>(std::clamp(c.b * f, 0.0f, 255.0f)) };

        fb.fillRect({ r.x + x, r.y, 1, r.h }, c);
    }

    // Marco en el color contrario al del centro, para que el borde no se
    // pierda cuando el remolino pasa por ahi.
    const Color cMarco = mixAnimated(kSaveViolet, kSaveGreen, t, 3.1f);
    fb.drawRect(r, hover ? kWhite : cMarco, 2);

    // Etiqueta entera si cabe; si no, partida en dos lineas.
    const i32 we = font5x7::measureUtf8(entero, 1);
    if (we < r.w - 8) {
        font5x7::drawTextUtf8(fb, r.x + (r.w - we) / 2,
                              r.y + (r.h - textr::glyphHeight(1)) / 2,
                              entero, kWhite, 1);
    } else if (l1 && l2) {
        const i32 w1 = font5x7::measureUtf8(l1, 1);
        const i32 w2 = font5x7::measureUtf8(l2, 1);
        if (w1 < r.w - 4) {
            const i32 y0 = r.y + r.h / 2 - font5x7::kGlyphH - 1;
            font5x7::drawTextUtf8(fb, r.x + (r.w - w1) / 2, y0, l1, kWhite, 1);
            font5x7::drawTextUtf8(fb, r.x + (r.w - w2) / 2,
                                  y0 + textr::glyphHeight(1) + 3, l2, kWhite, 1);
        }
    }
}

Color HomeScene::mixAnimated(Color a, Color b, f64 t, f32 semilla) {
    // Mezcla irregular: tres senos de periodos que no son multiplos entre si
    // (0.7, 1.3 y 2.1 rad/s). Su suma no vuelve al mismo punto en un ciclo
    // corto, asi que el color no se ve "latir" con un pulso regular, que es lo
    // que delata una interpolacion simple.
    const f64 fase = t + semilla;
    const f64 s = std::sin(fase * 0.7) * 0.5 +
                  std::sin(fase * 1.3) * 0.3 +
                  std::sin(fase * 2.1) * 0.2;
    // s va de -1 a 1; se lleva a 0..1 para usarlo como proporcion de mezcla.
    const f32 k = static_cast<f32>((s + 1.0) * 0.5);

    auto lerp = [&](u8 ca, u8 cb) {
        return static_cast<u8>(std::lround(ca + (cb - ca) * k));
    };
    return Color{ lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b) };
}

const char* HomeScene::eulaBlockTitle(i32 i) {
    switch (i) {
        case kEulaLocal: return "LOCAL / UN JUGADOR";
        case kEulaMulti: return "MULTIJUGADOR / SERVIDORES";
        case kEulaMods:  return "COMUNIDAD Y MODING";
        default:         return "";
    }
}

const std::string& HomeScene::eulaText(i32 i) const {
    static const std::string vacio;
    return (i >= 0 && i < kEulaBlocks) ? m_eulaField[i].text() : vacio;
}

void HomeScene::setEulaText(i32 i, const std::string& s) {
    if (i >= 0 && i < kEulaBlocks) m_eulaField[i].setText(s);
}

devcatalog::Listing HomeScene::buildListing() const {
    devcatalog::Listing l;
    l.description = m_upDescField.text();
    l.genres      = m_upGenreField.text();
    l.multiplayer = m_upMultiField.text();
    l.fps         = uploadFps();
    l.minAge      = uploadAge();
    l.maxAge      = uploadAgeTop();
    l.piracy      = m_upPirateConsent;
    l.customize   = m_upCustomConsent;
    l.art         = m_upArtPath;
    l.source      = m_upCodePath;
    l.eulaLocal   = m_eulaField[kEulaLocal].text();
    l.eulaMulti   = m_eulaField[kEulaMulti].text();
    l.eulaMods    = m_eulaField[kEulaMods].text();
    return l;
}

void HomeScene::loadListing(const devcatalog::Listing& l) {
    m_upDescField.setText(l.description);
    m_upGenreField.setText(l.genres.empty() ? "#" : l.genres);
    m_upMultiField.setText(l.multiplayer.empty() ? "@" : l.multiplayer);

    // Los deslizadores guardan una posicion normalizada, no el valor: se
    // deshace la conversion para dejar el selector donde corresponde.
    const f32 rangoFps = static_cast<f32>(kUpFpsMax - kUpFpsMin);
    const f32 rangoAge = static_cast<f32>(kUpAgeMax - kUpAgeMin);
    m_upFpsT = std::clamp((l.fps    - kUpFpsMin) / rangoFps, 0.0f, 1.0f);
    m_upAgeT = std::clamp((l.minAge - kUpAgeMin) / rangoAge, 0.0f, 1.0f);
    // Una ficha vieja no trae maxima; Listing la deja en kUpAgeMax, que es
    // el extremo derecho -- la edad abierta por arriba que tenia antes.
    m_upAgeMaxT = std::clamp((l.maxAge - kUpAgeMin) / rangoAge, 0.0f, 1.0f);
    if (m_upAgeMaxT < m_upAgeT) m_upAgeMaxT = m_upAgeT;

    m_upPirateConsent = l.piracy;
    m_upCustomConsent = l.customize;
    m_upCodePath      = l.source;

    m_eulaField[kEulaLocal].setText(l.eulaLocal);
    m_eulaField[kEulaMulti].setText(l.eulaMulti);
    m_eulaField[kEulaMods].setText(l.eulaMods);

    // La imagen se recarga para poder previsualizarla; si ya no esta en su
    // sitio, se conserva la ruta pero sin vista previa.
    m_upArtPath = l.art;
    m_upArtPixels.clear();
    m_upArtW = m_upArtH = 0;
    m_upArtMsg.clear();
    if (!m_upArtPath.empty()) {
        std::vector<u32> px;
        i32 w = 0, h = 0;
        if (photo::loadAnyImage(m_upArtPath, px, w, h) && w > 0 && h > 0) {
            m_upArtPixels = std::move(px);
            m_upArtW = w;
            m_upArtH = h;
        } else {
            m_upArtMsg = "La imagen guardada ya no esta disponible.";
        }
    }

    // Las listas de marcados se reconstruyen desde el texto, que es lo que se
    // persiste: asi editar el campo a mano y volver a abrir sigue cuadrando.
    auto marcarDesdeTexto = [](const std::string& txt, char inicial,
                               i32 total,
                               const std::function<std::string(i32)>& etiqueta,
                               std::vector<i32>& out) {
        out.clear();
        for (i32 i = 0; i < total; ++i) {
            const std::string et = etiqueta(i);   // ya incluye el '#' o '@'
            if (et.size() <= 1) continue;
            // Se busca la etiqueta COMPLETA: sin comprobar el final, "#RPG"
            // tambien marcaria "#RPGTACTICO".
            size_t pos = txt.find(et);
            while (pos != std::string::npos) {
                const size_t fin = pos + et.size();
                const bool bordeIzq = (pos == 0) || txt[pos - 1] == ' ';
                const bool bordeDer = (fin >= txt.size()) || txt[fin] == ' ' ||
                                      txt[fin] == inicial;
                if (bordeIzq && bordeDer) { out.push_back(i); break; }
                pos = txt.find(et, pos + 1);
            }
        }
    };

    marcarDesdeTexto(l.genres, '#', kUpGenreCount,
                     [](i32 i) { return uploadGenreLabel(i); }, m_upGenres);
    marcarDesdeTexto(l.multiplayer, '@', kUpMultiCount,
                     [](i32 i) { return uploadMultiLabel(i); }, m_upMulti);
}

void HomeScene::submitUpload() {
    const std::string nombre = m_upNameField.text();
    if (nombre.empty()) {
        m_devMsg    = "Primero escribe el nombre del juego.";
        m_devMsgErr = true;
        return;
    }

    // --- el codigo es obligatorio ---
    // Se comprueba ANTES del precio porque es el paso anterior: sin archivos
    // no hay juego que publicar, y avisar del precio primero mandaria a
    // arreglar lo segundo antes que lo primero.
    if (m_upCodePath.empty()) {
        m_upPriceWarn = 1.2;
        m_upWarnOk    = false;
        m_upWarnKind  = kWarnCode;
        m_devMsg.clear();
        return;
    }

    // --- los archivos son obligatorios ---
    // Sin el paquete guardado, publicar solo deja una ruta al PC del autor y
    // nadie podria descargar nada. Es el mismo criterio que un .jar: lo que se
    // distribuye lleva dentro todo lo que el juego necesita.
    //
    // Se comprueba contra el CATALOGO y no contra una variable: los archivos
    // se guardan en otra mini-seccion, y pueden haberse guardado en otra
    // sesion distinta de esta.
    if (!devcatalog::hasArchive(kDevCatalogFile, nombre)) {
        m_upPriceWarn = 1.2;
        m_upWarnOk    = false;
        m_upWarnKind  = kWarnFiles;
        m_devMsg.clear();
        return;
    }

    // --- el precio es obligatorio ---
    // Basta con haberlo ACEPTADO en la seccion de precios: una vez dado por
    // bueno ahi, subir no vuelve a preguntarlo. Si no se paso por ese paso,
    // se acepta tambien un deslizador ya movido, para no obligar a volver
    // atras a quien solo fijo el precio y no pulso el boton.
    // "Sin tocar" es el selector en el extremo GRATIS, o sea t = 0.
    if (!m_priceAccepted && m_priceT <= 0.0f) {
        m_upPriceWarn = 1.2;   // arranca el temblor y el aviso
        m_upWarnOk    = false; // en rojo: es un error
        m_upWarnKind  = kWarnPrice;
        m_devMsg.clear();      // el aviso grande sustituye al del pie
        return;
    }

    const devcatalog::Listing ficha = buildListing();
    std::string err;

    // Si la ficha cargada es la de este mismo juego, se actualiza; si no, se
    // intenta publicar como nuevo. Publicar algo que ya existe lo dice el
    // propio catalogo, asi que no hace falta comprobarlo aqui por duplicado.
    const bool editando = (!m_upEditing.empty() && m_upEditing == nombre);
    const bool ok = editando
        ? devcatalog::setListing(kDevCatalogFile, nombre, ficha, err)
        : devcatalog::publishWithListing(kDevCatalogFile, nombre,
                                         m_upCodePath, ficha, err);

    if (!ok) {
        // Publicar un nombre ya existente no es un error del usuario: es que
        // queria actualizarlo. Se reintenta como actualizacion.
        std::string err2;
        if (!editando &&
            devcatalog::setListing(kDevCatalogFile, nombre, ficha, err2)) {
            m_upEditing      = nombre;
            m_devGamesLoaded = false;
            m_devMsg         = "Ficha actualizada: " + nombre;
            m_devMsgErr      = false;
            return;
        }
        m_devMsg    = err;
        m_devMsgErr = true;
        return;
    }

    // El precio del deslizador se guarda junto a la ficha: sin esto el juego
    // entraria en la tienda como gratis pese a haberlo fijado.
    std::string perr;
    devcatalog::setPrice(kDevCatalogFile, nombre,
                         devcatalog::Price{ priceUsdCents() }, perr);

    m_upEditing      = nombre;
    m_devGamesLoaded = false;   // recargar para ver el juego nuevo
    m_storeLoaded    = false;   // y que la Tienda lo muestre ya
    m_devMsg         = editando ? "Ficha actualizada: " + nombre
                                : "Juego publicado: " + nombre;
    m_devMsgErr      = false;

    // Confirmacion grande: la misma franja que avisa de lo que falta sirve
    // para decir que salio bien, en verde y sin temblor.
    m_upPriceWarn = 2.0;   // algo mas larga: es una buena noticia, se lee
    m_upWarnOk    = true;
}

// ===========================================================================
//  EULA propio: tres bloques de reglas, a pantalla casi completa
// ===========================================================================

void HomeScene::layoutEula(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 cabecera = 52;   // titulo y boton de volver

    const i32 margen = 18;
    const i32 hueco  = 14;
    const i32 y0 = top + cabecera;
    const i32 alto = std::max(1, fbH - y0 - margen);

    // Tres columnas iguales. En una ventana estrecha no caben tres textos
    // legibles lado a lado, asi que por debajo de cierto ancho se apilan.
    const bool apilar = (fbW < 720);

    if (!apilar) {
        const i32 colW = std::max(1, (fbW - margen * 2 - hueco * 2) / 3);
        for (i32 i = 0; i < kEulaBlocks; ++i)
            m_eulaRects[i] = Recti{ margen + i * (colW + hueco), y0, colW, alto };
    } else {
        const i32 filaH = std::max(1, (alto - hueco * 2) / 3);
        for (i32 i = 0; i < kEulaBlocks; ++i)
            m_eulaRects[i] = Recti{ margen, y0 + i * (filaH + hueco),
                                    std::max(1, fbW - margen * 2), filaH };
    }

    // Cada bloque tiene su rotulo arriba y, debajo, el texto con scroll.
    for (i32 i = 0; i < kEulaBlocks; ++i) {
        const Recti& r = m_eulaRects[i];
        const i32 textoY = r.y + 26;
        const i32 textoH = std::max(1, r.h - 26 - 8);
        m_eulaPanel[i].setRect({ r.x + 6, textoY,
                                 std::max(1, r.w - 12), textoH });
        // El campo ocupa el ancho util; su alto es el del panel, y el propio
        // TextField se encarga de desplazarse si el texto no cabe.
        m_eulaField[i].setRect({ r.x + 10, textoY + 4,
                                 std::max(1, r.w - 20 - kScrollBarW),
                                 std::max(1, textoH - 8) });
    }

    const char* volver = "< VOLVER (ESC)";
    const i32 vw = font5x7::measureUtf8(volver, 1);
    m_eulaCloseRect = Recti{ fbW - vw - 30, top + 16, vw + 12, 20 };
}

void HomeScene::updateEula(Engine& engine, f64 dt) {
    Input& in = engine.input();
    m_eulaAnimTime += dt;

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    m_eulaCloseHover = dentro(m_eulaCloseRect);
    if (in.mouseReleased(0) && m_eulaCloseHover) {
        m_section = kDevSection;   // volver al formulario
        return;
    }

    // Un clic dentro de un bloque lo hace el activo: es lo que decide a donde
    // van las teclas, porque solo hay un cursor para los tres.
    if (in.mousePressed(0)) {
        for (i32 i = 0; i < kEulaBlocks; ++i) {
            if (!dentro(m_eulaRects[i])) continue;
            m_eulaFocus = i;
            if (m_eulaField[i].contains(lx, ly))
                m_eulaField[i].onMouseDown(lx, ly);
        }
    }

    // Arrastre para seleccionar, solo en el bloque activo.
    if (in.mouseDown(0) && m_eulaField[m_eulaFocus].focused())
        m_eulaField[m_eulaFocus].onMouseDrag(lx, ly);

    // La rueda desplaza el bloque que este bajo el cursor, no el activo: es
    // lo que se espera al leer uno mientras se escribe en otro.
    for (i32 i = 0; i < kEulaBlocks; ++i)
        if (dentro(m_eulaRects[i])) m_eulaPanel[i].update(engine);

    // --- teclado, al bloque activo ---
    TextField& campo = m_eulaField[m_eulaFocus];
    for (i32 i = 0; i < kEulaBlocks; ++i)
        m_eulaField[i].setFocused(i == m_eulaFocus);
    campo.update(dt);

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);

    // TAB salta al siguiente bloque: es mas rapido que ir al raton para
    // cambiar de ambito mientras se redactan las reglas.
    if (in.keyPressed(VK_TAB)) {
        m_eulaFocus = (m_eulaFocus + (shift ? kEulaBlocks - 1 : 1)) % kEulaBlocks;
        return;
    }

    const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                           VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };
    for (const u32 vk : teclas)
        if (in.keyRepeat(vk)) campo.onKey(vk, shift, ctrlAtajo);

    for (const wchar_t ch : in.charsTyped()) {
        if (ch < 32 && ch != '\r' && ch != '\n') continue;
        if (ctrlAtajo) continue;
        // El Enter entra como salto de linea: un reglamento se escribe en
        // parrafos, y sin esto no habria forma de separarlos.
        campo.onChar((ch == '\r') ? L'\n' : ch);
    }
}

void HomeScene::drawEula(Engine& engine, Framebuffer& fb) {
    layoutEula(engine, fb.width(), fb.height());

    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    fb.fillRect({0, top, fb.width(), fb.height() - top}, kEulaBg);

    // --- cabecera ---
    // El titulo late entre rojo y naranja, igual que el boton que abre esta
    // seccion: asi se lee como su continuacion.
    const Color cTitulo = mixAnimated(kUpEulaRed, kUpEulaOrange, m_eulaAnimTime, 0.0f);
    font5x7::drawTextUtf8(fb, 24, top + 16, "EULA PROPIO", cTitulo, 2);

    const char* sub = "Las reglas que tus jugadores aceptan al jugar";
    const i32 sw = font5x7::measureUtf8(sub, 1);
    if (24 + font5x7::measureUtf8("EULA PROPIO", 2) + 16 + sw < fb.width() - 160)
        font5x7::drawTextUtf8(fb, 24 + font5x7::measureUtf8("EULA PROPIO", 2) + 16,
                              top + 22, sub, kEulaTitle, 1);

    const char* volver = "< VOLVER (ESC)";
    font5x7::drawTextUtf8(fb, m_eulaCloseRect.x + 6, m_eulaCloseRect.y + 6,
                          volver, m_eulaCloseHover ? kWhite : kEulaTitle, 1);

    // --- los tres bloques ---
    for (i32 i = 0; i < kEulaBlocks; ++i) {
        const Recti& r = m_eulaRects[i];
        if (r.w <= 0 || r.h <= 0) continue;

        const bool activo = (m_eulaFocus == i);

        fb.fillRect(r, activo ? kEulaPanelFoc : kEulaPanel);
        fb.drawRect(r, activo ? kEulaEdgeFoc : kEulaEdge, activo ? 2 : 1);

        // Rotulo del ambito, reducido si no cabe.
        const char* t = eulaBlockTitle(i);
        i32 esc = 1;
        const i32 tw = font5x7::measureUtf8(t, esc);
        if (tw < r.w - 12)
            font5x7::drawTextUtf8(fb, r.x + 8, r.y + 8, t,
                                  activo ? kWhite : kEulaTitle, esc);

        // Linea bajo el rotulo, que separa cabecera de contenido.
        fb.fillRect({ r.x + 6, r.y + 22, std::max(1, r.w - 12), 1 },
                    activo ? kEulaEdgeFoc : kEulaEdge);

        // El texto se dibuja con el propio TextField, que ya gestiona cursor,
        // seleccion y portapapeles.
        m_eulaField[i].render(fb);

        // Contador de caracteres al pie: no hay limite, pero saber cuanto
        // llevas escrito de un reglamento es util.
        char cnt[48]{};
        std::snprintf(cnt, sizeof(cnt), "%zu caracteres",
                      m_eulaField[i].length());
        const i32 cw = font5x7::measureUtf8(cnt, 1);
        if (cw < r.w - 12)
            font5x7::drawTextUtf8(fb, r.x + r.w - cw - 8,
                                  r.y + r.h - font5x7::kGlyphH - 5, cnt,
                                  kEulaEdgeFoc, 1);
    }

    // Ayuda al pie, centrada: TAB es lo unico no evidente de esta pantalla.
    const char* ayuda = "TAB cambia de bloque  ·  el texto no tiene limite";
    const i32 aw = font5x7::measureUtf8(ayuda, 1);
    if (aw < fb.width() - 40)
        font5x7::drawTextUtf8(fb, (fb.width() - aw) / 2, top + 34, ayuda,
                              kEulaEdge, 1);
}

void HomeScene::drawConsentNotice(Framebuffer& fb, Recti panel,
                                  const char* const* lineas, i32 nLineas,
                                  i32 consent, Color cYes, Color cNo,
                                  const char* txtYes, const char* txtNo) {
    // --- texto del aviso, con scroll propio ---
    const Recti cr = m_upNoticePanel.contentRect();
    m_upNoticePanel.setContentHeight(nLineas * kUpNoticeLineH + 8);
    m_upNoticePanel.renderBar(fb);

    for (i32 i = 0; i < nLineas; ++i) {
        const i32 ly = cr.y + i * kUpNoticeLineH - m_upNoticePanel.offset();
        // Recorte al area visible: sin esto el texto se saldria del panel al
        // desplazar y se pintaria sobre el resto de la seccion.
        if (ly + kUpNoticeLineH <= cr.y || ly >= cr.y + cr.h) continue;
        if (!lineas[i][0]) continue;   // linea en blanco: separa parrafos
        font5x7::drawTextUtf8(fb, cr.x + 2, ly, lineas[i], kText, 1);
    }

    // --- los dos botones de respuesta ---
    // El elegido se queda relleno de su color; el otro baja a un contorno,
    // de modo que la decision tomada se ve sin leer.
    auto boton = [&](Recti r, const char* txt, Color c, bool elegido,
                     bool hov, bool down) {
        if (r.w <= 0 || r.h <= 0) return;
        auto ajusta = [&](Color k, f32 f) {
            return Color{ static_cast<u8>(std::clamp(k.r * f, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(k.g * f, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(k.b * f, 0.0f, 255.0f)) };
        };
        const Color fondo = elegido ? (down ? ajusta(c, 0.7f) : c)
                                    : (hov ? ajusta(c, 0.35f) : kFieldBg);
        fb.fillRect(r, fondo);
        fb.drawRect(r, (hov || elegido) ? c : ajusta(c, 0.6f), elegido ? 3 : 2);

        font5x7::drawFitted(fb, r, txt, elegido ? kWhite : kText, 1, 5);
    };

    boton(m_upYesRect, txtYes, cYes, consent == kConsentYes,
          m_upYesHover, m_upYesDown);
    boton(m_upNoRect,  txtNo,  cNo,  consent == kConsentNo,
          m_upNoHover,  m_upNoDown);

    (void)panel;
}

void HomeScene::updateConsentNotice(Engine& engine, i32& consent) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    m_upNoticePanel.update(engine);   // rueda y arrastre del texto

    m_upYesHover = dentro(m_upYesRect);
    m_upNoHover  = dentro(m_upNoRect);

    if (in.mousePressed(0)) {
        if (m_upYesHover) m_upYesDown = true;
        if (m_upNoHover)  m_upNoDown  = true;
    }

    if (in.mouseReleased(0)) {
        if (m_upYesDown && m_upYesHover) {
            consent     = kConsentYes;
            m_devMsg    = "Condiciones aceptadas.";
            m_devMsgErr = false;
        } else if (m_upNoDown && m_upNoHover) {
            consent     = kConsentNo;
            m_devMsg    = "Condiciones rechazadas.";
            m_devMsgErr = false;
        }
        m_upYesDown = m_upNoDown = false;
    }
}

void HomeScene::routeUploadKeys(Engine& engine) {
    // Solo escribe el campo abierto, y solo si es de texto. Sin esto las
    // teclas llegarian a los tres campos a la vez.
    TextField* campo = nullptr;
    if      (m_upOpen == kUpName)  campo = &m_upNameField;
    else if (m_upOpen == kUpDesc)  campo = &m_upDescField;
    else if (m_upOpen == kUpGenre) campo = &m_upGenreField;
    else if (m_upOpen == kUpMulti) campo = &m_upMultiField;
    if (!campo) return;

    Input& in = engine.input();
    campo->setFocused(true);

    const bool shift = in.keyDown(VK_SHIFT);
    // AltGr llega como Ctrl+Alt: sin distinguirlo, la arroba y los acentos
    // quedarian bloqueados como si fueran atajos.
    const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);

    const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                           VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };
    for (const u32 vk : teclas)
        if (in.keyRepeat(vk)) campo->onKey(vk, shift, ctrlAtajo);

    for (const wchar_t ch : in.charsTyped()) {
        if (ctrlAtajo) continue;               // atajo, no texto
        // El Enter pasa como salto de linea en la descripcion, que es de
        // varias lineas; en los campos de una linea el propio TextField lo
        // rechaza, asi que no hace falta filtrarlo aqui.
        if (ch == L'\r' || ch == L'\n') { campo->onChar(L'\n'); continue; }
        if (ch < 32) continue;                 // resto de control: lo trata onKey
        campo->onChar(ch);
    }

    // Los dos campos de etiquetas conservan siempre su caracter inicial: es
    // el formato, no algo que el usuario deba recordar escribir. El genero
    // empieza por '#' y el multijugador por '@'.
    if (m_upOpen == kUpGenre) {
        const std::string& t = m_upGenreField.text();
        if (t.empty() || t[0] != '#') m_upGenreField.setText("#" + t);
    } else if (m_upOpen == kUpMulti) {
        const std::string& t = m_upMultiField.text();
        if (t.empty() || t[0] != '@') m_upMultiField.setText("@" + t);
    }
}

void HomeScene::updateUpload(Engine& engine, f64 dt) {
    Input& in = engine.input();

    // El simbolo + gira mientras la seccion este visible.
    m_upPlusTime += dt;

    // El aviso de "falta el precio" se agota solo.
    if (m_upPriceWarn > 0.0) {
        m_upPriceWarn -= dt;
        if (m_upPriceWarn < 0.0) m_upPriceWarn = 0.0;
    }

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    // --- arrastre de un deslizador: tiene prioridad sobre todo lo demas ---
    // Un arrastre que se sale del carril no debe activar lo que haya debajo.
    if (m_upFpsDrag || m_upAgeDrag || m_upAgeMaxDrag) {
        if (in.mouseDown(0) && m_upTrackRect.w > 0) {
            // Los dos carriles de EDAD tienen el mismo ancho y la misma x,
            // asi que el recorrido se calcula una vez.
            const i32 travel = std::max(1, m_upTrackRect.w - kUpKnobW);
            const i32 rel = std::clamp(lx - m_upTrackRect.x - kUpKnobW / 2,
                                       0, travel);
            const f32 t = static_cast<f32>(rel) / static_cast<f32>(travel);

            if (m_upFpsDrag) {
                m_upFpsT = t;
            } else if (m_upAgeMaxDrag) {
                // Topa en la minima: la franja no se invierte.
                m_upAgeMaxT = std::max(t, m_upAgeT);
            } else {
                m_upAgeT = t;
                if (m_upAgeT > m_upAgeMaxT) m_upAgeMaxT = m_upAgeT;
            }
        }
        if (in.mouseReleased(0))
            m_upFpsDrag = m_upAgeDrag = m_upAgeMaxDrag = false;
        return;
    }

    // --- panel abierto: se lleva el raton mientras este desplegado ---
    if (m_upOpen >= 0) {
        routeUploadKeys(engine);

        // Los campos de texto necesitan cursor y seleccion con el raton.
        TextField* campo = (m_upOpen == kUpName)  ? &m_upNameField
                         : (m_upOpen == kUpDesc)  ? &m_upDescField
                         : (m_upOpen == kUpGenre) ? &m_upGenreField
                         : (m_upOpen == kUpMulti) ? &m_upMultiField : nullptr;
        if (campo) {
            campo->update(dt);
            if (in.mousePressed(0) && campo->contains(lx, ly))
                campo->onMouseDown(lx, ly);
            else if (in.mouseDown(0) && campo->focused())
                campo->onMouseDrag(lx, ly);
        }

        // --- listas de etiquetas (generos y modos de multijugador) ---
        // Las dos se comportan igual: rejilla de ovalos con scroll, y un clic
        // marca o desmarca. Solo cambian el panel y que se marca.
        if (m_upOpen == kUpGenre || m_upOpen == kUpMulti) {
            const bool esGenero = (m_upOpen == kUpGenre);
            ScrollPanel& panel = esGenero ? m_upGenrePanel : m_upMultiPanel;

            panel.update(engine);
            if (in.mouseReleased(0)) {
                const Recti cr = panel.contentRect();
                if (dentro(cr) && !panel.dragging()) {
                    // Que etiqueta se pulso: se deduce de la fila y la columna.
                    const i32 porFila = std::max(1, cr.w / 104);
                    const i32 rel = ly - cr.y + panel.offset();
                    const i32 fila = rel / kUpGenreRowH;
                    const i32 col  = (lx - cr.x) / std::max(1, cr.w / porFila);
                    const i32 idx  = fila * porFila + col;
                    if (fila >= 0 && col >= 0 && col < porFila) {
                        if (esGenero) toggleUploadGenre(idx);
                        else          toggleUploadMulti(idx);
                    }
                }
            }
        }

        // --- avisos de condiciones (campos 7 y 9) ---
        if (m_upOpen == kUpPirate) updateConsentNotice(engine, m_upPirateConsent);
        if (m_upOpen == kUpCustom) updateConsentNotice(engine, m_upCustomConsent);

        // --- campo 8: soltar la imagen promocional ---
        if (m_upOpen == kUpArt) {
            m_upArtHover = dentro(m_upArtDropRect);
            if (in.mousePressed(0) && m_upArtHover) m_upArtDown = true;
            if (in.mouseReleased(0)) {
                if (m_upArtDown && m_upArtHover) pickPromoArt(engine);
                m_upArtDown = false;
            }
        }

        // Las tres vias de fijar la EDAD se atienden ANTES que el carril:
        // estan debajo de el, y si el carril mirase primero, un clic en un
        // tramo tambien moveria el selector.
        updateAgeOptions(engine, dt);

        // Agarrar un deslizador, o saltar a donde se pulse en el carril.
        if ((m_upOpen == kUpPerf || m_upOpen == kUpAge) &&
            in.mousePressed(0) && m_upTrackRect.w > 0) {
            // Posicion 0..1 que corresponde a una x dentro de un carril.
            auto tDe = [&](const Recti& carril) {
                const i32 travel = std::max(1, carril.w - kUpKnobW);
                const i32 rel = std::clamp(lx - carril.x - kUpKnobW / 2,
                                           0, travel);
                return static_cast<f32>(rel) / static_cast<f32>(travel);
            };
            auto sobre = [&](const Recti& carril, const Recti& selector) {
                return carril.w > 0 &&
                       lx >= carril.x && lx < carril.x + carril.w &&
                       ly >= selector.y && ly < selector.y + selector.h;
            };

            // El SEGUNDO carril (la maxima) se mira primero: esta debajo, y
            // asi un clic entre los dos va al que de verdad se ve ahi.
            if (m_upOpen == kUpAge && m_upTrack2Rect.w > 0 &&
                (dentro(m_upKnob2Rect) || sobre(m_upTrack2Rect, m_upKnob2Rect))) {
                // La maxima no baja de la minima: la franja no puede
                // invertirse arrastrando.
                m_upAgeMaxT    = std::max(tDe(m_upTrack2Rect), m_upAgeT);
                m_upAgeMaxDrag = true;
                return;
            }

            if (dentro(m_upKnobRect) || sobre(m_upTrackRect, m_upKnobRect)) {
                const f32 t = tDe(m_upTrackRect);
                if (m_upOpen == kUpPerf) { m_upFpsT = t; m_upFpsDrag = true; }
                else {
                    // Y la minima no sube por encima de la maxima, por lo
                    // mismo. Se empuja la otra en vez de bloquear el
                    // arrastre: bloquear se siente como que el raton falla.
                    m_upAgeT = t;
                    if (m_upAgeT > m_upAgeMaxT) m_upAgeMaxT = m_upAgeT;
                    m_upAgeDrag = true;
                }
                return;
            }
        }

        // Clic fuera del panel y fuera de su boton: cerrar.
        if (in.mouseReleased(0) && !dentro(m_upPanelRect) &&
            !dentro(m_upRects[m_upOpen]))
            m_upOpen = kUpNone;

        return;   // con un panel abierto, los botones no responden
    }

    // --- zona de subida central ---
    // Guarda CARPETAS COMPLETAS, y solo eso. Antes el clic izquierdo elegia un
    // archivo suelto y el derecho una carpeta, pero un juego es un arbol
    // (ejecutable + recursos): subir un archivo solo dejaba publicado algo que
    // no se podia jugar. Ahora cualquiera de los dos botones pide la carpeta.
    m_upDropHover = dentro(m_upDropRect);
    if (in.mousePressed(0) && m_upDropHover) m_upDropDown = true;
    if (in.mouseReleased(1) && m_upDropHover) pickGameFolder(engine);

    // --- boton PUBLICAR ---
    m_upPublishHover = dentro(m_upPublishRect);
    if (in.mousePressed(0) && m_upPublishHover) m_upPublishDown = true;

    // GUARDAR Y SUBIR se retiro: hacia lo mismo que PUBLICAR. Su rectangulo
    // queda vacio, asi que `dentro` nunca da true y el estado se mantiene
    // apagado sin casos especiales.
    m_upSaveHover = false;

    // --- los diez botones ---
    m_upHover = kUpNone;
    for (i32 i = 0; i < kUploadSlots; ++i)
        if (dentro(m_upRects[i])) m_upHover = i;

    if (in.mousePressed(0) && m_upHover >= 0) m_upDown = m_upHover;

    if (in.mouseReleased(0)) {
        if (m_upPublishDown && m_upPublishHover) {
            submitUpload();
        } else if (m_upDropDown && m_upDropHover) {
            pickGameFolder(engine);
        } else if (m_upDown >= 0 && m_upDown == m_upHover) {
            // El EULA no abre un panel dentro del formulario: se lleva toda
            // la ventana, porque son tres textos largos que hay que poder
            // leer y escribir con holgura.
            if (m_upDown == kUpEula) {
                m_upOpen = kUpNone;
                m_section = kEulaSection;
                m_devMsg.clear();
            } else {
                m_upOpen = m_upDown;
                m_devMsg.clear();

                // Los campos de etiquetas arrancan con su caracter inicial.
                if (m_upOpen == kUpGenre && m_upGenreField.text().empty())
                    m_upGenreField.setText("#");
                if (m_upOpen == kUpMulti && m_upMultiField.text().empty())
                    m_upMultiField.setText("@");
            }
        }
        m_upDown        = kUpNone;
        m_upDropDown    = false;
        m_upPublishDown = false;
        m_upSaveDown    = false;
    }
}

void HomeScene::drawSpinningPlus(Framebuffer& fb, i32 cx, i32 cy, i32 r,
                                 f64 t, Color c) const {
    // Cruz girada: se rasteriza en el espacio ROTADO. Para cada pixel se
    // deshace el giro y se comprueba si cae en uno de los dos brazos, que es
    // como se consigue que gire de verdad y no a saltos de 90 grados.
    const f32 ang = static_cast<f32>(t) * 1.6f;   // rad/s
    const f32 ca = std::cos(-ang), sa = std::sin(-ang);
    const i32 grosor = std::max(2, r / 4);

    for (i32 y = -r; y <= r; ++y) {
        for (i32 x = -r; x <= r; ++x) {
            // Coordenadas del pixel en el sistema sin girar.
            const f32 rx = x * ca - y * sa;
            const f32 ry = x * sa + y * ca;
            const bool brazoH = std::fabs(ry) <= grosor * 0.5f &&
                                std::fabs(rx) <= static_cast<f32>(r);
            const bool brazoV = std::fabs(rx) <= grosor * 0.5f &&
                                std::fabs(ry) <= static_cast<f32>(r);
            if (brazoH || brazoV) fb.blendPixel(cx + x, cy + y, c);
        }
    }
}

// ===========================================================================
//  LudoUpdates: el tablon del Modo Desarrollador
//
//  Ocupa la zona central cuando no hay ninguna mini-seccion abierta, que
//  antes era negro vacio. Diez bloques anchos con scroll.
//
//  EL TEXTO ES PROVISIONAL. Los titulos si son los definitivos -- marcan de
//  que va cada bloque --, pero el cuerpo es relleno que se sustituye sin
//  tocar una linea de maquetacion: `kLudoCuerpos` es una tabla, y el alto de
//  cada bloque se calcula contando sus lineas.
// ===========================================================================
namespace {

struct BloqueLudo {
    const char*        titulo;
    const char* const* parrafos;
    i32                n;
};

// ---------------------------------------------------------------------------
//  EL TEXTO SE GUARDA EN PARRAFOS, NO EN LINEAS
//
//  Antes cada bloque traia sus lineas ya cortadas a mano. Con un reglamento de
//  varias paginas eso es inmanejable: cualquier retoque obliga a recolocar el
//  corte de todo lo que sigue, y al cambiar el ancho del panel el reparto deja
//  de valer.
//
//  Ahora se guarda el parrafo entero y el motor lo parte POR PALABRAS al ancho
//  que haya. Escribir mas texto es escribirlo y ya.
//
//  Una linea vacia ("") es un hueco entre parrafos.
// ---------------------------------------------------------------------------

const char* kLudo1[] = {
    "Bienvenido a esta plataforma donde el objetivo de su desarrollo es "
    "respetar y darle importancia a tu comodidad, tu dinero y tus juegos. "
    "Sabemos que es lo que quieres y donde debemos ayudarte.",
    "",
    "En Ludora Engine tenemos la filosofia de que si un juego es solo una "
    "licencia por la que te cobran por tenerlo, piratearlo es conseguirlo "
    "para que legalmente sea tuyo. Aqui se aplica esta ley: todo objeto o "
    "articulo que compres o desarrolles tendra derechos y politicas que "
    "beneficien a ambas partes, al consumidor y al desarrollador.",
    "",
    "Esta plataforma esta especializada en ofrecer ingresos y mejor control "
    "de la industria Indie, y en desarrolladores que no tienen suficiente "
    "publicidad, poder economico o cualquier otro problema para vender. "
    "Aqui se ayuda a vender y a dar algo justo. Y a los usuarios que no "
    "desean mas abusos, precios injustos ni explicaciones vagas y hambreadas "
    "por las empresas.",
    "",
    "Ludora Engine es el lugar indicado para que los juegos legalmente sean "
    "tuyos y nadie mas te los quite.",
    "",
    "SISTEMA DE 3 STRIKES",
    "El primero es delito menor, el segundo delito medio y el tercero delito "
    "alto. Cada nivel depende de la agresividad del hate, de las denuncias "
    "validadas y de las opiniones que expongan un descuido de lo que el "
    "desarrollador vende.",
    "",
    "Strike 1: quita la posibilidad de monetizar durante 2 meses.",
    "Strike 2: bloquea la publicidad del juego denunciado.",
    "Strike 3: elimina el juego y banea al desarrollador durante 4 anos.",
};

const char* kLudo2[] = {
    "Para entender como un desarrollador puede publicar, promocionar y "
    "monetizar con el juego, articulo o producto, debe cumplir requisitos "
    "muy importantes antes siquiera de subir algo a la tienda.",
    "",
    "1ra: ¡NO PUEDES MENTIR!",
    "Si un desarrollador hace una descripcion o una promocion muy buena del "
    "juego y habla de todo, pero al comprarlo es totalmente diferente, "
    "horrible y mal programado, los usuarios pueden reportar el juego y dar "
    "una opinion y pruebas validas de por que quieren reembolsar el dinero. "
    "Se verifica todo esto y el desarrollador recibe un Strike que le "
    "suspendera la venta de juegos 1 mes.",
    "",
    "Pero si se detecta que las denuncias son falsas, troll o grief, el "
    "desarrollador recupera su cuenta, su juego vuelve a tiendas y recibe un "
    "5% mas de publicidad.",
    "",
    "2da: ¡NO SEAS HAMBREADO!",
    "Esta regla prohibe que un desarrollador abuse de sus consumidores burda "
    "y descaradamente. Se detecta por tres vias.",
    "",
    "Primero: si un juego recibe actualizaciones que cada vez afectan mas a "
    "la comunidad y se pone en evidencia la inactividad en cuanto al respeto "
    "de las reglas y al descontento, el juego recibe una advertencia de tener "
    "que actualizar lo que la comunidad vote. Si el desarrollador no cumple, "
    "la comunidad puede votar cuantos Strikes merece y se le dara el castigo "
    "correspondiente, asegurando antes que no sea una broma ni un troleo. Si "
    "lo es, todos los usuarios que votaron tendran baneada durante 1 semana "
    "la posibilidad de escribir en juegos online, chat y comunidad, hacer "
    "videollamadas y llamadas, y crear juegos.",
    "",
    "Segundo: si el desarrollador sube los precios de forma extremadamente "
    "injusta y encima avisa despues de la actualizacion, y se denuncia o "
    "detecta, el juego tendra sanciones y se le obligara a bajar precios. "
    "Pero si da razones y ademas hay un enorme salto tecnico en el juego, "
    "revisado por usuarios y empleados, se determinara un precio alto pero "
    "no excesivo.",
    "",
    "Tercero: si el desarrollador incumple derechos de autor, ataca "
    "ciberneticamente a usuarios y comunidades, y todo ello por razones "
    "personales sin criterio, se baneara de por vida y se eliminaran sus "
    "juegos. Pero si tenia motivos y atacaba una comunidad enferma, pedofila "
    "o dedicada a cosas ilegales, se le apoyara; incluso se eliminaria la "
    "comunidad denunciada si hay razones, pruebas y testimonios, y se le dara "
    "un 20% mas en su monetizacion si tiene antecedentes limpios y la gente "
    "lo apoya.",
    "",
    "3ra: ¡SI NO SABES, MEJOR VETE!",
    "Esta regla promueve el movimiento Stop Killing Games. Si haces un acto "
    "totalmente ilegal en la plataforma y declaras que no sabias de las "
    "reglas, te banean: un desarrollador esta obligado a confirmar que "
    "siempre leera las actualizaciones, los sistemas nuevos y los "
    "reglamentos nuevos. Es ilegal desconocerlos, porque es necesario "
    "saberlos para publicar juegos y conocer los castigos.",
    "",
    "Ademas, si no apoyas la regla de que el usuario debe tener el juego y lo "
    "manejas en forma de licencia, como otras empresas grandes (Xbox, "
    "PlayStation, Nintendo, Steam, que aqui no se apoyan), te banean: no le "
    "estas dando a los usuarios el derecho de mantener el juego en sus "
    "bibliotecas.",
};

const char* kLudo3[] = {
    "Cualquier articulo, juego u objeto que el usuario haya comprado y "
    "guardado sera para siempre suyo, hasta que lo reembolse o el propio "
    "usuario lo borre y desinstale.",
    "",
    "Todos los articulos de la tienda deben llevar informacion de lo que se "
    "vende, quien es el desarrollador y una descripcion del juego o articulo. "
    "Siempre debe ser justo y totalmente visible lo que se promociona y lo "
    "que se ofrece a los usuarios.",
    "",
    "Un desarrollador no puede dar informacion a medias o mal hecha, ni "
    "vender un juego que cobre de forma injusta el online, ni que cobre por "
    "el modo offline: es ilegal vender eso y el juego puede reportarse con "
    "validez.",
};

const char* kLudo4[] = {
    "Todo usuario que compre algo es su dueno, y nadie mas puede negarle ese "
    "derecho.",
    "",
    "Otro derecho es el de opinar y proponer mejoras. Todos los usuarios "
    "pueden dar sugerencias y someterlas a votacion para que se implementen, "
    "tanto para los desarrolladores como para la plataforma. Aqui se "
    "escuchara a toda la comunidad y todos tendran derecho a expresar si algo "
    "les disgusta o les resulta comodo, sean mayoria o minoria.",
    "",
    "Los desarrolladores estan obligados a respetar estas leyes del usuario: "
    "mantener precios justos, dar avisos entendibles para la comunidad y "
    "respetar la cuenta de todo usuario.",
    "",
    "El usuario, a su vez, debe respetar a los desarrolladores, siempre que "
    "estos sean buenas personas y den prioridad a la comunidad y a quienes "
    "usan la plataforma y compran en ella.",
};

const char* kLudo5[] = {
    "El online se cobrara de forma justa, sin abusos extremos ni absurdos. "
    "Es necesario para mantener los servidores y para tener ganancias por dar "
    "la comodidad de un multijugador sin problemas. Solo se subiran los "
    "precios del online si estamos en una situacion critica y extrema.",
    "",
    "El usuario paga mensualmente una suscripcion, definida en tres "
    "categorias con sus ventajas.",
    "",
    "1. LUDORA ESTANDAR - La experiencia accesible. $39 MXN al mes.",
    "Pensado para jugadores que usan la plataforma con frecuencia y desean "
    "beneficios adicionales sin pagar una mensualidad elevada. Mejora tu "
    "experiencia con almacenamiento en la nube, opciones de personalizacion y "
    "descuentos exclusivos.",
    "",
    "El multijugador online, las amistades y las funciones esenciales de "
    "seguridad seguiran disponibles sin necesidad de contratar este plan.",
    "",
    "- 5 GB de almacenamiento cifrado mediante Nube Tomate.",
    "- Descuento adicional del 3% en titulos participantes.",
    "- Personalizacion de perfil: marcos, insignias y fondos exclusivos.",
    "- Sincronizacion multidispositivo con Ludora Device Shield.",
    "- Panel de gestion de almacenamiento y sincronizaciones pendientes.",
    "- Soporte estandar para incidencias de suscripcion y servicios.",
    "",
    "2. LUDORA PLUS - La experiencia equilibrada. $79 MXN al mes.",
    "Para quienes juegan con regularidad, prueban titulos nuevos y quieren "
    "mas valor de su suscripcion. Incluye todo lo de Estandar, mas capacidad "
    "de almacenamiento y acceso a una seleccion de videojuegos incluidos.",
    "",
    "- 25 GB de almacenamiento cifrado.",
    "- Descuento adicional del 5% en titulos participantes.",
    "- Catalogo Ludora Plus: seleccion rotativa para suscriptores activos.",
    "- Acceso a betas y demostraciones cuando el desarrollador lo autorice.",
    "- Gestion de versiones de partidas y recuperacion de copias.",
    "- Personalizacion ampliada: estilos, efectos, insignias y fondos.",
    "- Soporte prioritario moderado, sin retrasar incidencias criticas.",
    "",
    "3. LUDORA MAX - La experiencia completa. $129 MXN al mes.",
    "El plan mas completo, para quienes usan Ludora intensamente y tienen "
    "bibliotecas grandes. Reune las ventajas de Estandar y Plus, con mayor "
    "capacidad en la nube y catalogo ampliado.",
    "",
    "- 100 GB de almacenamiento cifrado.",
    "- Descuento adicional del 8% en compras participantes.",
    "- Catalogo Ludora Max: todo lo de Plus y una seleccion ampliada.",
    "- Acceso anticipado ampliado a pruebas, demos y eventos.",
    "- Historial de partidas en la nube con mas versiones recuperables.",
    "- Personalizacion completa, con elementos exclusivos de Max.",
    "- Soporte preferente, sin comprometer la atencion de seguridad.",
    "",
    "Ninguno de los tres planes limitara artificialmente la calidad grafica, "
    "el rendimiento ni el acceso a las funciones esenciales.",
};

const char* kLudo6[] = {
    "Ludora protege tus videojuegos, tu cuenta y tus datos personales "
    "mediante autenticacion, cifrado y verificacion de integridad. La "
    "plataforma esta disenada para ofrecer seguridad sin imponer "
    "restricciones innecesarias a quienes adquieren videojuegos legalmente.",
    "",
    "PROTECCION DE VIDEOJUEGOS",
    "Los videojuegos distribuidos por Ludora podran incorporar archivos "
    "cifrados, firmas digitales y verificaciones de integridad para prevenir "
    "modificaciones no autorizadas. La proteccion tecnica no garantiza que "
    "los archivos sean imposibles de copiar o analizar.",
    "",
    "DERECHOS DE LOS COMPRADORES",
    "Los videojuegos adquiridos individualmente permaneceran asociados a la "
    "biblioteca del usuario conforme a sus condiciones de licencia. La "
    "finalizacion de una suscripcion no revocara por si sola las compras "
    "individuales.",
    "",
    "JUEGOS RETIRADOS DE LA TIENDA",
    "Cuando un videojuego deje de comercializarse, Ludora procurara conservar "
    "su acceso para los compradores anteriores, siempre que los acuerdos de "
    "distribucion y las obligaciones legales lo permitan.",
    "",
    "SUSCRIPCIONES VENCIDAS",
    "Cuando una suscripcion finalice, dejaran de estar disponibles los "
    "videojuegos cuyo acceso dependa exclusivamente de ella. Los juegos "
    "comprados individualmente no perderan su licencia por ese motivo.",
    "",
    "PIRATERIA Y DISTRIBUCION NO AUTORIZADA",
    "Ludora no autoriza la distribucion ilegal de videojuegos, licencias, "
    "archivos protegidos o contenido de terceros. La plataforma respetara las "
    "excepciones y los derechos reconocidos por la legislacion aplicable.",
    "",
    "PRESERVACION DIGITAL",
    "Ludora promovera acuerdos con desarrolladores para conservar videojuegos "
    "descontinuados, facilitar el acceso offline y proteger el patrimonio "
    "digital de los jugadores.",
    "",
    "Nuestro principio es sencillo: proteger a los desarrolladores sin "
    "castigar a los compradores legitimos.",
};

const char* kLudo7[] = {
    "FILOSOFIA DE COMERCIO JUSTO",
    "Creemos que los videojuegos deben ser accesibles, que los "
    "desarrolladores merecen una remuneracion justa y que ninguna empresa "
    "deberia aprovecharse de sus usuarios para obtener mayores ganancias.",
    "",
    "1. El dinero del usuario merece respeto. Cada compra y suscripcion debe "
    "ofrecer un valor real. No habra cargos ocultos, practicas enganosas ni "
    "restricciones artificiales para presionar a los jugadores.",
    "",
    "2. Comprar no significa pagar indefinidamente. Los videojuegos "
    "adquiridos individualmente no requeriran una suscripcion para seguir "
    "utilizandose conforme a su licencia.",
    "",
    "3. La seguridad es un derecho, no un producto de lujo. Todos los "
    "usuarios tendran acceso a las protecciones fundamentales de cuenta, "
    "privacidad y seguridad, sea cual sea su suscripcion.",
    "",
    "4. Los desarrolladores merecen un trato transparente: comisiones "
    "competitivas, condiciones comprensibles y pagos puntuales, sin tarifas "
    "ocultas ni cambios contractuales inesperados.",
    "",
    "5. Las suscripciones deben ser voluntarias. Ningun plan debera usarse "
    "para restringir injustificadamente las funciones esenciales.",
    "",
    "6. Los precios deben ser claros y razonables. El usuario conocera el "
    "coste total antes de confirmar. Las ofertas deberan ser autenticas y "
    "verificables.",
    "",
    "7. Cancelar debe ser tan sencillo como contratar, sin obstaculos "
    "innecesarios ni penalizaciones ocultas.",
    "",
    "8. Las ganancias deben permitir mejorar la plataforma, equilibrando "
    "precios accesibles, pago justo a los desarrolladores, infraestructura y "
    "sostenibilidad.",
    "",
    "MODERACION Y SERVICIOS",
    "Ludora existe para que las personas disfruten de los videojuegos en un "
    "entorno seguro, libre y respetuoso. Una comunidad sana no se construye "
    "con miedo al castigo, sino con reglas comprensibles, decisiones justas y "
    "una atencion que escuche.",
    "",
    "Ningun usuario debe ser tratado como inferior por no pagar. Ningun "
    "usuario debe recibir privilegios disciplinarios por pagar mas.",
    "",
    "1. Respeto y libertad. Todos pueden expresar opiniones, compartir "
    "criticas y participar en comunidades, respetando los derechos y la "
    "seguridad de los demas. Ludora no sancionara criticas legitimas contra "
    "sus servicios ni sus decisiones comerciales.",
    "",
    "2. Moderacion proporcional. Las medidas responderan a la gravedad, el "
    "contexto, la reincidencia y el riesgo para otros. No habra expulsiones "
    "permanentes automaticas por errores menores ni por acumular denuncias.",
    "",
    "3. Derecho a una explicacion. Toda sancion llevara una justificacion "
    "comprensible, su duracion y las opciones de revision.",
    "",
    "4. Derecho a apelar. Las decisiones graves contaran con supervision "
    "humana y procedimientos que reduzcan los conflictos de interes.",
    "",
    "5. Proteccion de las compras. Las sanciones afectaran solo a las "
    "funciones relacionadas con la infraccion. Una restriccion de "
    "comunicacion no eliminara por si sola los juegos comprados.",
    "",
    "6. Atencion accesible. Habra soporte de cuentas, compras, pagos y "
    "seguridad para todos, sea cual sea su plan. No se cobrara por recuperar "
    "una cuenta legitima ni por investigar un cobro indebido.",
    "",
    "7. Transparencia en el servicio. El usuario podra consultar el estado de "
    "sus solicitudes y recibira explicaciones claras.",
    "",
    "8. Proteccion de datos. La moderacion usara solo los datos necesarios. "
    "No se vendera informacion personal ni se usara la seguridad como excusa "
    "para vigilar indiscriminadamente.",
    "",
    "9. Responsabilidad de Ludora. El personal estara sujeto a reglas de "
    "conducta y revision. Los errores de moderacion deberan corregirse y, "
    "cuando corresponda, repararse.",
    "",
    "10. Mejora continua. Se evaluaran periodicamente las reglas, los tiempos "
    "de atencion, las apelaciones y la experiencia de los usuarios.",
    "",
    "No queremos una comunidad donde todos tengan miedo de hablar, ni una "
    "donde el acoso quede sin consecuencias. Queremos reglas claras, personas "
    "escuchadas y decisiones revisables.",
    "",
    "Ludora - El videojuego es diversion, no explotacion.",
};

const char* kLudo8[] = {
    "Nuestra comunidad, nuestras reglas.",
    "",
    "En Ludora queremos que todos puedan jugar, hablar, conocer personas y "
    "pasarla bien sin tener que preocuparse por estafas, abusos o gente que "
    "solamente busca arruinarles el dia.",
    "",
    "No queremos llenar la plataforma de reglas absurdas ni castigar a "
    "alguien por cualquier tonteria. Solamente pedimos ocho cosas bastante "
    "sencillas: respeta a los demas, piensa antes de actuar y no hagas lo que "
    "no te gustaria que te hicieran.",
    "",
    "1. NO SEAS RATA",
    "No te aproveches de los demas. No robes cuentas, juegos, objetos, dinero "
    "ni cosas que no te pertenecen. Tampoco intentes enganar a alguien para "
    "conseguir algo gratis a costa de su esfuerzo.",
    "",
    "Ser listo no significa aprovecharte de quien confia en ti. Si quieres "
    "algo, consiguelo de forma justa.",
    "",
    "2. NO SEAS GRIEFER",
    "Sabemos que hay juegos donde destruir, pelear o hacer travesuras forma "
    "parte de la diversion, y eso esta bien cuando todos saben a lo que estan "
    "jugando.",
    "",
    "Lo que no esta bien es entrar a una partida solamente para destruir las "
    "construcciones de alguien, arruinar su progreso, molestar constantemente "
    "o echar a perder horas de trabajo sin que eso forme parte de las reglas "
    "del servidor.",
    "",
    "Diviertete, pero deja que los demas tambien se diviertan.",
    "",
    "3. JAMAS ATAQUES SIN PENSAR",
    "Antes de insultar, acusar, amenazar o atacar a alguien, piensa un "
    "momento en lo que estas haciendo.",
    "",
    "No todo desacuerdo es una provocacion y no todo lo que parece ofensivo "
    "fue hecho con mala intencion. Si tienes un problema con alguien, intenta "
    "resolverlo hablando o utiliza las herramientas de bloqueo y denuncia.",
    "",
    "Si estas jugando un titulo donde combatir es parte de las reglas, "
    "adelante. Esta ley trata de no convertir un conflicto personal en una "
    "agresion innecesaria.",
    "",
    "4. NO DENUNCIES SI NO TE HICIERON NADA",
    "Las denuncias estan para ayudar a quienes realmente tienen un problema, "
    "no para vengarte de alguien porque te gano una partida, no te cayo bien "
    "o dijo algo con lo que no estas de acuerdo.",
    "",
    "No inventes historias, no manipules pruebas y no organices denuncias "
    "falsas para conseguir que castiguen a otra persona.",
    "",
    "Eso si: no necesitas ser la victima directa para denunciar una estafa, "
    "una amenaza o un abuso que hayas presenciado de buena fe. Si crees que "
    "algo esta mal, puedes reportarlo honestamente, incluso si despues "
    "resulta que hubo un malentendido.",
    "",
    "5. NO ESTAFES O TE ESTAFARAN",
    "No enganes a otros jugadores con intercambios falsos, promesas de "
    "regalos, ventas fraudulentas, enlaces sospechosos o supuestos premios "
    "que solamente buscan quitarles su dinero o sus cuentas.",
    "",
    "La confianza es importante en una comunidad. Cuando estafas a alguien, "
    "no solamente perjudicas a esa persona: tambien haces que los demas dejen "
    "de confiar.",
    "",
    "Y recuerda que nadie tiene derecho a estafarte como venganza. Si alguien "
    "te engano, denuncialo; no hagas lo mismo.",
    "",
    "6. NO ABUSES DE TUS USUARIOS",
    "Si administras un servidor, una comunidad, un grupo o un videojuego "
    "dentro de Ludora, recuerda que tener autoridad no significa poder hacer "
    "lo que quieras.",
    "",
    "No amenaces a tus jugadores, no les cobres por cosas que prometiste "
    "gratis, no utilices tus permisos para humillarlos y no impongas castigos "
    "injustos porque alguien te contradijo.",
    "",
    "Esto tambien se aplica al propio equipo de Ludora. Nuestros moderadores "
    "y administradores deben respetar las mismas reglas y responder por sus "
    "decisiones.",
    "",
    "La autoridad sirve para cuidar una comunidad, no para aprovecharse de "
    "ella.",
    "",
    "7. NO MANDES TONTERIAS EN GRUPOS",
    "Los grupos estan para conversar, organizar partidas, compartir cosas y "
    "pasarla bien. Puedes bromear, mandar memes y divertirte, pero tambien "
    "debes saber cuando parar.",
    "",
    "No llenes los chats de spam, enlaces peligrosos, mensajes repetidos, "
    "contenido sexual no solicitado, amenazas o cosas que incomoden "
    "deliberadamente a los demas.",
    "",
    "Cada grupo puede tener su propio ambiente y sus propias reglas, siempre "
    "que respete las normas generales de Ludora.",
    "",
    "El humor es bienvenido. Molestar a todos sin parar, no.",
    "",
    "8. NO HAGAS TRATOS ILEGALES",
    "Ludora no es un lugar para vender cuentas robadas, distribuir contenido "
    "ilegal, cometer fraudes, lavar dinero, comercializar productos "
    "prohibidos o realizar acuerdos que perjudiquen a otras personas.",
    "",
    "Si haces intercambios, compras o ventas permitidas dentro de la "
    "plataforma, asegurate de que sean legitimos y de que ambas partes "
    "conozcan las condiciones.",
    "",
    "No utilices Ludora para organizar actividades ilegales ni para "
    "aprovecharte de otros usuarios.",
    "",
    "¿QUE PASA SI ROMPES UNA LEY?",
    "No queremos expulsar a las personas por cualquier error. Dependiendo de "
    "lo ocurrido, podemos darte un aviso, pedirte que detengas una conducta, "
    "restringir temporalmente alguna funcion o aplicar una suspension.",
    "",
    "Las situaciones graves, como estafas, amenazas o abusos importantes, "
    "pueden requerir medidas inmediatas.",
    "",
    "Siempre que sea posible, tendras derecho a conocer el motivo de una "
    "sancion y solicitar que se revise. Nadie deberia recibir un castigo "
    "solamente porque otro usuario lo acuso sin pruebas suficientes.",
    "",
    "UN ULTIMO MENSAJE",
    "En Ludora no nos importa si tienes una computadora cara o sencilla, si "
    "eres nuevo o veterano, si pagas Ludora Max o utilizas la plataforma "
    "gratis. Todos merecen el mismo respeto.",
    "",
    "No te pedimos que seas perfecto. Puedes equivocarte, bromear, discutir y "
    "tener un mal dia. Lo importante es que entiendas que detras de cada "
    "cuenta hay una persona.",
    "",
    "Juega, diviertete, haz amigos y no le arruines la experiencia a los "
    "demas.",
    "",
    "Ludora - Aqui nadie vale mas por pagar mas.",
};

const BloqueLudo kLudoBloques[] = {
    { "¡BIENVENIDO A LUDORA!",             kLudo1, static_cast<i32>(sizeof(kLudo1) / sizeof(kLudo1[0])) },
    { "1. COMO PUBLICO JUEGOS Y ANUNCIOS", kLudo2, static_cast<i32>(sizeof(kLudo2) / sizeof(kLudo2[0])) },
    { "2. ACTUALIZACIONES DE TIENDA",      kLudo3, static_cast<i32>(sizeof(kLudo3) / sizeof(kLudo3[0])) },
    { "3. DERECHOS DE LOS USUARIOS",       kLudo4, static_cast<i32>(sizeof(kLudo4) / sizeof(kLudo4[0])) },
    { "4. CONTROL DEL ONLINE Y OFFLINE",   kLudo5, static_cast<i32>(sizeof(kLudo5) / sizeof(kLudo5[0])) },
    { "5. SEGURIDAD Y PROPIEDAD DIGITAL",  kLudo6, static_cast<i32>(sizeof(kLudo6) / sizeof(kLudo6[0])) },
    { "6. ECONOMIA Y MODERACION",          kLudo7, static_cast<i32>(sizeof(kLudo7) / sizeof(kLudo7[0])) },
    { "7. LAS 8 LEYES DE LUDORA",          kLudo8, static_cast<i32>(sizeof(kLudo8) / sizeof(kLudo8[0])) },
};

static_assert(sizeof(kLudoBloques) / sizeof(kLudoBloques[0]) ==
              static_cast<size_t>(HomeScene::ludoBlockCount()),
              "la tabla de bloques y kLudoBlocks deben coincidir");

/// Los cinco tonos del logo, en el orden en que los recorre la L.
const Color kLudoPaleta[] = {
    kLogoCyan, kLogoBlue, kLogoViolet, kLogoMagenta, kLogoRed
};
constexpr i32 kLudoTonos = 5;

/// Color del degradado en la posicion `t` (0..1), interpolando la paleta.
Color ludoTono(f32 t) {
    t = std::clamp(t, 0.0f, 0.9999f);
    const f32 esc = t * (kLudoTonos - 1);
    const i32 i   = static_cast<i32>(esc);
    const f32 f   = esc - static_cast<f32>(i);
    const Color a = kLudoPaleta[i];
    const Color b = kLudoPaleta[std::min(i + 1, kLudoTonos - 1)];
    auto mez = [&](u8 x, u8 y) {
        return static_cast<u8>(x + (static_cast<f32>(y) - x) * f);
    };
    return Color{ mez(a.r, b.r), mez(a.g, b.g), mez(a.b, b.b) };
}

/// Parte un parrafo en lineas que quepan en `ancho`, cortando POR PALABRAS.
///
/// Recibe un callback en vez de devolver un vector porque la llaman dos
/// sitios con necesidades distintas: la maquetacion solo cuenta cuantas
/// lineas salen, y el dibujo las necesita una a una. Con el callback, el
/// reparto vive en UN solo lugar y los dos no pueden discrepar -- que es lo
/// que dejaria huecos muertos o bloques solapados.
///
/// Una palabra mas larga que el ancho se deja sola en su linea: romperla por
/// la mitad se lee peor que dejar que sobresalga, y con texto normal no pasa.
template <typename F>
void ludoParteParrafo(const char* texto, i32 ancho, F&& porLinea) {
    if (!texto || !*texto) { porLinea("", 0); return; }

    std::string linea;
    const char* p = texto;
    while (*p) {
        // Siguiente palabra (hasta el proximo espacio).
        const char* ini = p;
        while (*p && *p != ' ') ++p;
        const std::string palabra(ini, static_cast<size_t>(p - ini));
        while (*p == ' ') ++p;

        if (palabra.empty()) continue;

        const std::string prueba = linea.empty() ? palabra
                                                 : linea + " " + palabra;
        if (!linea.empty() &&
            font5x7::measureUtf8(prueba.c_str(), 1) > ancho) {
            porLinea(linea.c_str(), static_cast<i32>(linea.size()));
            linea = palabra;
        } else {
            linea = prueba;
        }
    }
    if (!linea.empty()) porLinea(linea.c_str(), static_cast<i32>(linea.size()));
}

/// Cuantas lineas ocupa un parrafo a ese ancho. Un parrafo vacio ocupa UNA:
/// es el hueco de separacion, y si no contara, el alto del bloque se
/// quedaria corto.
i32 ludoCuentaLineas(const char* texto, i32 ancho) {
    if (!texto || !*texto) return 1;
    i32 n = 0;
    ludoParteParrafo(texto, ancho, [&](const char*, i32) { ++n; });
    return std::max(1, n);
}

} // namespace

const char* HomeScene::ludoBlockTitle(i32 i) {
    return (i >= 0 && i < kLudoBlocks) ? kLudoBloques[i].titulo : "";
}

i32 HomeScene::ludoBlockLines(i32 i) {
    return (i >= 0 && i < kLudoBlocks) ? kLudoBloques[i].n : 0;
}

const char* HomeScene::ludoBlockLine(i32 i, i32 linea) {
    if (i < 0 || i >= kLudoBlocks) return "";
    if (linea < 0 || linea >= kLudoBloques[i].n) return "";
    return kLudoBloques[i].parrafos[linea];
}

void HomeScene::layoutLudoUpdates(Recti area) {
    m_ludoRect = area;
    if (area.w <= 60 || area.h <= 60) {
        for (i32 i = 0; i < kLudoBlocks; ++i) m_ludoBlockRect[i] = Recti{};
        return;
    }

    // El titulo se queda FIJO arriba y solo la lista se desplaza: si el
    // titulo subiera con el scroll, al bajar no se sabria que seccion es.
    //
    // La escala se decide con la MISMA cuenta que el dibujo: si aqui se
    // reservara el alto de la escala 5 y alli se pintara a la 3 por no
    // caber, la lista arrancaria con un hueco muerto encima.
    i32 escT = 5;
    while (escT > 2 && font5x7::measureUtf8("LudoUpdates", escT) > area.w - 24)
        --escT;
    const i32 tituloH = textr::glyphHeight(escT) + 26;
    const Recti lista{ area.x, area.y + tituloH,
                       area.w, std::max(1, area.h - tituloH) };
    m_ludoPanel.setRect(lista);

    // Alto de cada bloque: su titulo mas las lineas que SALGAN de partir sus
    // parrafos al ancho util. Se cuenta de verdad en vez de estimar: si el
    // alto no coincidiera con lo que se dibuja, unos bloques se solaparian y
    // otros dejarian un hueco muerto.
    const i32 lh    = textr::glyphHeight(1) + 4;
    const i32 padY  = 14;
    const i32 hueco = 16;

    // Se reserva SIEMPRE el ancho de la barra, haya scroll o no.
    //
    // No se usa contentRect() porque seria circular: ese ancho depende de si
    // el panel tiene scroll, y eso depende del alto que se esta calculando
    // justo aqui. Reservar siempre cuesta 14 px y evita que el ancho del
    // texto baile entre un frame y el siguiente.
    const i32 anchoBloque = std::max(1, lista.w - 10 - kScrollBarW - 6);
    const i32 anchoTexto  = std::max(40, anchoBloque - 36);

    i32 y = 0;
    for (i32 i = 0; i < kLudoBlocks; ++i) {
        i32 lineas = 0;
        for (i32 k = 0; k < kLudoBloques[i].n; ++k)
            lineas += ludoCuentaLineas(kLudoBloques[i].parrafos[k], anchoTexto);

        const i32 alto = padY + textr::glyphHeight(2) + 10 +
                         lineas * lh + padY;
        // Se guarda en coordenadas de PANTALLA, ya descontado el scroll: es
        // lo que necesitan tanto el dibujo como las pruebas.
        m_ludoBlockRect[i] = Recti{ lista.x + 10,
                                    lista.y + y - m_ludoPanel.offset(),
                                    anchoBloque, alto };
        y += alto + hueco;
    }
    m_ludoPanel.setContentHeight(y);
}

void HomeScene::drawLudoUpdates(Framebuffer& fb) {
    const Recti a = m_ludoRect;
    if (a.w <= 60 || a.h <= 60) return;

    // --- el titulo, en los colores del logo ---
    // El degradado recorre la palabra letra a letra y se desplaza despacio:
    // es el mismo barrido de tonos que tiene la L del icono.
    const char* kTitulo = "LudoUpdates";
    i32 esc = 5;
    while (esc > 2 && font5x7::measureUtf8(kTitulo, esc) > a.w - 24) --esc;

    const i32 tw = font5x7::measureUtf8(kTitulo, esc);
    i32 tx = a.x + (a.w - tw) / 2;
    const i32 ty = a.y + 8;

    // Letra a letra, cada una con su tono. Se dibuja DOS veces con un pixel
    // de desplazamiento para engrosar el trazo: la fuente del sistema no
    // tiene negrita y asi el titulo pesa lo que debe.
    const f32 desf = static_cast<f32>(std::fmod(m_ludoClock * 0.12, 1.0));
    i32 n = 0;
    for (const char* p = kTitulo; *p; ) { font5x7::nextUtf8(p); ++n; }

    i32 idx = 0;
    for (const char* p = kTitulo; *p; ++idx) {
        const char* ini = p;
        if (font5x7::nextUtf8(p) == 0) break;
        char s[5] = {};
        const size_t len = static_cast<size_t>(p - ini);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = ini[k];

        const f32 t = std::fmod(static_cast<f32>(idx) /
                                static_cast<f32>(std::max(1, n - 1)) + desf, 1.0f);
        const Color c = ludoTono(t);
        font5x7::drawTextUtf8(fb, tx,     ty, s, c, esc);
        font5x7::drawTextUtf8(fb, tx + 1, ty, s, c, esc);   // engrosado
        tx += font5x7::measureUtf8(s, esc);
    }

    // Raya bajo el titulo, con el mismo degradado.
    const i32 rayaY = ty + textr::glyphHeight(esc) + 8;
    for (i32 x = 0; x < a.w - 20; ++x) {
        const Color c = ludoTono(static_cast<f32>(x) /
                                 static_cast<f32>(std::max(1, a.w - 20)));
        fb.fillRect({ a.x + 10 + x, rayaY, 1, 3 }, c);
    }

    // --- los diez bloques, con scroll ---
    const Recti cr = m_ludoPanel.contentRect();
    m_ludoPanel.renderBar(fb);

    const i32 lh   = textr::glyphHeight(1) + 4;
    const i32 padY = 14;

    for (i32 i = 0; i < kLudoBlocks; ++i) {
        const Recti r = m_ludoBlockRect[i];
        if (r.w <= 0) continue;
        // Fuera de la ventana visible: ni se dibuja. Con diez bloques da
        // igual, pero es la diferencia cuando sean cien.
        if (r.y + r.h <= cr.y || r.y >= cr.y + cr.h) continue;

        // Cada bloque toma su tono de la paleta segun su posicion en la
        // lista: el tablon entero recorre el logo de arriba abajo.
        const Color tono = ludoTono(static_cast<f32>(i) /
                                    static_cast<f32>(kLudoBlocks - 1));

        // El recorte vertical se hace a mano: el framebuffer no tiene clip,
        // y sin esto un bloque a medio entrar pintaria sobre el titulo.
        auto rectoRecortado = [&](Recti q, Color c) {
            const i32 y0 = std::max(q.y, cr.y);
            const i32 y1 = std::min(q.y + q.h, cr.y + cr.h);
            if (y1 > y0) fb.fillRect({ q.x, y0, q.w, y1 - y0 }, c);
        };

        rectoRecortado(r, kPanelDeep);
        // Banda de color a la izquierda: identifica el bloque sin depender
        // del texto.
        rectoRecortado({ r.x, r.y, 5, r.h }, tono);

        // Marco, solo donde se ve.
        if (r.y >= cr.y && r.y < cr.y + cr.h)
            fb.fillRect({ r.x, r.y, r.w, 1 }, tono);
        if (r.y + r.h - 1 >= cr.y && r.y + r.h - 1 < cr.y + cr.h)
            fb.fillRect({ r.x, r.y + r.h - 1, r.w, 1 }, tono);

        // Titulo del bloque. drawFitted y no drawTextUtf8: un titulo largo
        // ("1. COMO PUBLICO JUEGOS Y ANUNCIOS") a escala 2 se salia por la
        // derecha del bloque. Asi baja de escala en vez de desbordarse.
        //
        // Se le da una caja alineada a la IZQUIERDA sumandole el ancho justo
        // del texto cuando cabe; si no cabe, drawFitted lo encoge y queda
        // centrado en el hueco, que es lo correcto para un rotulo recortado.
        const i32 tiY = r.y + padY;
        if (tiY >= cr.y && tiY + textr::glyphHeight(2) <= cr.y + cr.h) {
            const i32 util = std::max(1, r.w - 36);
            const i32 anchoTit =
                std::min(util, font5x7::measureUtf8(kLudoBloques[i].titulo, 2));
            font5x7::drawFitted(fb,
                { r.x + 18, tiY, anchoTit, textr::glyphHeight(2) + 2 },
                kLudoBloques[i].titulo, tono, 2, 0);
        }

        // Cuerpo: los parrafos se parten con la MISMA funcion que uso la
        // maquetacion para calcular el alto, asi que el texto siempre acaba
        // justo donde el bloque termina.
        const i32 anchoTexto = std::max(40, r.w - 36);
        i32 ly = tiY + textr::glyphHeight(2) + 10;

        for (i32 k = 0; k < kLudoBloques[i].n; ++k) {
            const char* par = kLudoBloques[i].parrafos[k];
            if (!par || !*par) { ly += lh; continue; }   // hueco entre parrafos

            ludoParteParrafo(par, anchoTexto, [&](const char* linea, i32) {
                if (ly >= cr.y && ly + textr::glyphHeight(1) <= cr.y + cr.h)
                    font5x7::drawTextUtf8(fb, r.x + 18, ly, linea, kText, 1);
                ly += lh;
            });
        }
    }
}

void HomeScene::updateLudoUpdates(Engine& engine, f64 dt) {
    m_ludoClock += dt;
    // El panel gestiona rueda y arrastre de su barra. Se llama SIEMPRE que
    // el tablon este visible: si no, la rueda no haria nada.
    m_ludoPanel.update(engine);
}

void HomeScene::drawDevUpload(Framebuffer& fb, Recti area) {
    layoutUpload(area);

    /// Etiqueta y pareja de colores de cada campo. Los cinco reservados van en
    /// gris apagado: se ven, pero se distingue de un vistazo que no hacen nada.
    struct Campo { const char* txt; Color linea; Color relleno; };
    const Campo campos[kUploadSlots] = {
        { "ESCRIBIR NOMBRE DEL JUEGO", kUpIndigo, kUpIndigo },
        { "DESCRIPCIÓN DEL JUEGO",     kUpPurple, kBg       },
        { "ELEGIR GÉNERO",             kUpPink,   kUpBlue   },
        { "ELEGIR RENDIMIENTO",    kNeonYellow,   kFieldBg  },
        { "EDAD RECOMENDADA",          kUpMint,   kFieldBg  },
        { "TIPO DE MULTIJUGADOR",  kUpMultiRed,   kUpMultiBlue },
        { "PERMISO DE PIRATERÍA",  kConsentGreen, kFieldBg  },
        { "DISEÑO DE SECCIÓN",     kUpArtEdge,    kFieldBg  },
        { "PERMITIR PERSONALIZACIÓN", kNeonGreen, kFieldBg  },
        { "EULA PROPIO",           kUpEulaRed,    kFieldBg  },
    };

    // Cada boton tiene su pareja de colores para la animacion y su propia
    // fase, de modo que laten a destiempo en vez de a la vez -- si fueran
    // sincronos el formulario entero parpadearia como un solo bloque.
    struct Anim { Color a, b; };
    const Anim animar[kUploadSlots] = {
        { kUpIndigo,     kDarkBlue     },   // 1 nombre
        { kUpPurple,     kPanelViolet  },   // 2 descripcion
        { kUpBlue,       kUpPink       },   // 3 genero
        { kNeonYellow,   kNeonYellowDim},   // 4 rendimiento
        { kUpMint,       kTurquoise    },   // 5 edad
        { kUpMultiRed,   kUpMultiBlue  },   // 6 multijugador
        { kConsentGreen, kConsentRed   },   // 7 pirateria
        { kUpArtEdge,    kUpArtHover   },   // 8 diseno
        { kNeonGreen,    kNeonRed      },   // 9 personalizacion
        { kUpEulaRed,    kUpEulaOrange },   // 10 EULA
    };

    for (i32 i = 0; i < kUploadSlots; ++i) {
        const Recti r = m_upRects[i];
        if (r.w <= 0 || r.h <= 0) continue;

        const bool hov    = (m_upHover == i && m_upOpen < 0);
        const bool down   = (m_upDown == i && hov);
        const bool activo = (m_upOpen == i);
        const bool libre = false;   // ya no queda ningun hueco sin uso

        // Color animado de este boton: mezcla irregular de su pareja, con una
        // fase propia derivada del indice.
        const Color cAnim = mixAnimated(animar[i].a, animar[i].b,
                                        m_upPlusTime,
                                        static_cast<f32>(i) * 1.7f);

        auto ajusta = [&](Color c, f32 k) {
            return Color{ static_cast<u8>(std::clamp(c.r * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.g * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.b * k, 0.0f, 255.0f)) };
        };
        const Color fondo = down ? ajusta(campos[i].relleno, 0.65f)
                          : (hov || activo) ? ajusta(campos[i].relleno, 1.35f)
                          : campos[i].relleno;
        fb.fillRect(r, fondo);

        // Los campos 3 y 6 llevan el marco ENTRELAZADO: el borde alterna dos
        // colores cada cuatro pixeles en lugar de ser de un tono. El 3 va en
        // azul y rosa, el 6 en rojo y azul, asi que no se confunden aunque
        // los dos sean listas de etiquetas.
        if (i == kUpGenre || i == kUpMulti) {
            // El entrelazado tambien se mueve: el desfase avanza con el reloj,
            // asi que las bandas recorren el marco en vez de quedarse fijas.
            const Color cA = (i == kUpGenre) ? kUpPink : kUpMultiRed;
            const Color cB = (i == kUpGenre) ? kUpBlue : kUpMultiBlue;
            const i32 desf = static_cast<i32>(m_upPlusTime * 9.0) & 0x7FFFFFFF;
            for (i32 x = r.x; x < r.x + r.w; ++x) {
                const Color c = ((((x - r.x) + desf) / 4) % 2) ? cA : cB;
                fb.fillRect({ x, r.y, 1, 2 }, c);
                fb.fillRect({ x, r.y + r.h - 2, 1, 2 }, c);
            }
            for (i32 y = r.y; y < r.y + r.h; ++y) {
                const Color c = ((((y - r.y) + desf) / 4) % 2) ? cA : cB;
                fb.fillRect({ r.x, y, 2, 1 }, c);
                fb.fillRect({ r.x + r.w - 2, y, 2, 1 }, c);
            }
        } else {
            // El marco toma el color animado; con el raton encima pasa a
            // blanco, que es la senal de "pulsable" en todo el motor.
            fb.drawRect(r, (hov || activo) ? kWhite : cAnim, activo ? 3 : 2);
        }

        // Etiqueta centrada, reduciendo la escala si no cabe en vez de
        // desbordar el boton.
        // Las etiquetas llevan acentos y enye, asi que se miden y dibujan por
        // la via UTF-8: la version ASCII contaria bytes (2 por acento) y
        // dejaria un hueco donde deberia ir la letra.
        const Color ct = libre ? kDim : (hov || activo ? kWhite : kText);

        // Los dos permisos llevan etiquetas largas que, en una sola linea,
        // obligaban a bajar a la escala minima y quedaban diminutas. Se
        // parten en dos lineas para poder dibujarlas al doble de tamano, que
        // es lo que las hace legibles de un vistazo.
        // Cada permiso se parte donde haga falta para caber GRANDE: la
        // palabra mas larga es la que marca la escala, asi que
        // "PERSONALIZACION" se corta en dos para no quedarse en la escala
        // minima por un solo termino.
        const char* partes[3] = { nullptr, nullptr, nullptr };
        i32 nPartes = 0;
        if (i == kUpPirate) {
            partes[0] = "PERMISO DE"; partes[1] = "PIRATERÍA"; nPartes = 2;
        } else if (i == kUpCustom) {
            partes[0] = "PERMITIR"; partes[1] = "PERSONA-";
            partes[2] = "LIZACIÓN"; nPartes = 3;
        }

        if (nPartes > 0 && r.h >= 40) {
            // Escala comun a todas las lineas: manda la mas ancha, para que
            // no queden de tamanos distintos dentro del mismo boton.
            i32 e2 = 3;
            while (e2 > 1) {
                bool cabe = (font5x7::kGlyphH * e2 * nPartes +
                             2 * (nPartes - 1) <= r.h - 12);
                for (i32 k = 0; k < nPartes && cabe; ++k)
                    if (font5x7::measureUtf8(partes[k], e2) > r.w - 10)
                        cabe = false;
                if (cabe) break;
                --e2;
            }

            const i32 lineaH = textr::glyphHeight(e2) + 2;
            const i32 alto = lineaH * nPartes - 2;
            i32 ly = r.y + (r.h - alto) / 2 - 4;
            for (i32 k = 0; k < nPartes; ++k) {
                const i32 wk = font5x7::measureUtf8(partes[k], e2);
                font5x7::drawTextUtf8(fb, r.x + (r.w - wk) / 2, ly,
                                      partes[k], ct, e2);
                ly += lineaH;
            }
        } else {
            i32 esc = (r.h >= 44) ? 2 : 1;
            while (esc > 1 && font5x7::measureUtf8(campos[i].txt, esc) > r.w - 12)
                --esc;
            const i32 tw = font5x7::measureUtf8(campos[i].txt, esc);
            font5x7::drawTextUtf8(fb, r.x + (r.w - tw) / 2,
                                  r.y + (r.h - textr::glyphHeight(esc)) / 2,
                                  campos[i].txt, ct, esc);
        }

        // Los campos con valor elegido lo muestran bajo la etiqueta, para
        // saber como estan ajustados sin tener que abrirlos.
        if (!libre && r.h >= 46) {
            char val[64]{};
            if      (i == kUpPerf) snprintf(val, sizeof(val), "%d FPS", uploadFps());
            else if (i == kUpAge)  snprintf(val, sizeof(val), "%d-%d AÑOS",
                                            uploadAge(), uploadAgeTop());
            else if (i == kUpGenre && !m_upGenres.empty())
                snprintf(val, sizeof(val), "%d MARCADOS",
                         static_cast<int>(m_upGenres.size()));
            else if (i == kUpMulti && !m_upMulti.empty())
                snprintf(val, sizeof(val), "%d MARCADOS",
                         static_cast<int>(m_upMulti.size()));
            else if (i == kUpDesc && m_upDescField.length() > 0)
                snprintf(val, sizeof(val), "%zu/%zu",
                         m_upDescField.length(), kUpDescMax);
            // Los dos permisos muestran la respuesta dada, que es justo lo
            // que interesa saber sin abrir el aviso otra vez.
            else if (i == kUpPirate && m_upPirateConsent != kConsentNone)
                snprintf(val, sizeof(val), "%s",
                         m_upPirateConsent == kConsentYes ? "ACEPTADO" : "RECHAZADO");
            else if (i == kUpCustom && m_upCustomConsent != kConsentNone)
                snprintf(val, sizeof(val), "%s",
                         m_upCustomConsent == kConsentYes ? "ACEPTADO" : "RECHAZADO");
            else if (i == kUpArt && m_upArtW > 0)
                snprintf(val, sizeof(val), "%dx%d", m_upArtW, m_upArtH);
            else if (i == kUpEula) {
                // Cuantos de los tres bloques llevan reglas escritas.
                i32 escritos = 0;
                for (i32 b = 0; b < kEulaBlocks; ++b)
                    if (!m_eulaField[b].text().empty()) ++escritos;
                if (escritos > 0)
                    snprintf(val, sizeof(val), "%d/3 BLOQUES", escritos);
            }
            if (val[0]) {
                const i32 vw = font5x7::measureUtf8(val, 1);   // "AÑOS"
                font5x7::drawTextUtf8(fb, r.x + (r.w - vw) / 2,
                                      r.y + r.h - font5x7::kGlyphH - 5, val,
                                      hov || activo ? kWhite : kGlyph, 1);
            }
        }
    }

    // El campo 1 muestra el nombre escrito dentro del propio boton.
    if (m_upRects[kUpName].w > 0 && !m_upNameField.text().empty() &&
        m_upOpen != kUpName) {
        const Recti& r = m_upRects[kUpName];
        const char* s = m_upNameField.text().c_str();
        i32 esc = 2;
        while (esc > 1 && font5x7::measureUtf8(s, esc) > r.w - 12) --esc;
        const i32 tw = font5x7::measureUtf8(s, esc);
        // Tapa la etiqueta: con nombre puesto, el nombre es la informacion.
        fb.fillRect({ r.x + 2, r.y + 2, r.w - 4, r.h - 4 }, kUpIndigo);
        fb.drawRect(r, kWhite, 2);
        font5x7::drawTextUtf8(fb, r.x + (r.w - tw) / 2,
                              r.y + (r.h - textr::glyphHeight(esc)) / 2,
                              s, kWhite, esc);
    }

    // --- boton PUBLICAR, a la derecha del nombre ---
    // Es el UNICO boton de publicar del formulario. Antes habia tambien un
    // GUARDAR Y SUBIR abajo que llamaba a la misma funcion; se retiro, y con
    // el vino aqui su aviso: el temblor y el rojo de "falta algo".
    //
    // Verde cuando hay nombre; apagado cuando no, porque sin nombre no se
    // puede publicar y conviene que se vea antes de pulsarlo.
    if (m_upPublishRect.w > 0) {
        Recti pr = m_upPublishRect;
        const bool listo = !m_upNameField.text().empty();
        const bool hovP  = m_upPublishHover && m_upOpen < 0;

        auto ajustaP = [&](Color c, f32 k) {
            return Color{ static_cast<u8>(std::clamp(c.r * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.g * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.b * k, 0.0f, 255.0f)) };
        };

        // --- aviso de "falta algo": tiembla y se pone rojo ---
        // El temblor es horizontal y rapido, con la amplitud cayendo segun se
        // agota el aviso: empieza fuerte y se posa, que es como tiembla algo
        // de verdad en vez de oscilar siempre igual.
        //
        // Solo el aviso de ERROR se apodera del boton. El de exito lo deja
        // verde: el juego ya subio, no hay nada que corregir ahi.
        const bool avisando = (m_upPriceWarn > 0.0) && !m_upWarnOk;
        if (avisando) {
            const f64 restante = m_upPriceWarn / 1.2;           // 1 -> 0
            const i32 amp = static_cast<i32>(std::lround(5.0 * restante));
            pr.x += static_cast<i32>(std::lround(
                        std::sin(m_upPriceWarn * 42.0) * amp));

            const f32 pulso = 0.75f +
                0.25f * static_cast<f32>(std::sin(m_upPriceWarn * 20.0));
            const Color rojo{
                static_cast<u8>(std::clamp(kAccent.r * pulso, 0.0f, 255.0f)),
                static_cast<u8>(std::clamp(kAccent.g * pulso, 0.0f, 255.0f)),
                static_cast<u8>(std::clamp(kAccent.b * pulso, 0.0f, 255.0f)) };
            fb.fillRect(pr, rojo);
            fb.drawRect(pr, kWhite, 2);
        } else {
            const Color base = listo ? kConsentGreen : kFieldBg;
            fb.fillRect(pr, m_upPublishDown ? ajustaP(base, 0.7f)
                          : hovP            ? ajustaP(base, 1.3f) : base);
            fb.drawRect(pr, listo ? (hovP ? kWhite : kConsentGreen)
                                  : kPanelEdge, 2);
        }

        // Dice si va a crear o a actualizar: es la diferencia que importa
        // cuando ya publicaste el juego una vez.
        const char* txtP = (!m_upEditing.empty() &&
                            m_upEditing == m_upNameField.text())
                           ? "GUARDAR" : "PUBLICAR";
        // drawFitted en vez de medir y callar: si el rotulo no entrase, el
        // boton se quedaba mudo. Es el patron del resto de la interfaz.
        font5x7::drawFitted(fb, pr, txtP,
                            avisando ? kWhite : (listo ? kWhite : kDim), 1, 4);
    }

    // --- rectangulo vertical de subida, en el centro ---
    const Recti d = m_upDropRect;
    if (d.w > 0 && d.h > 0) {
        const bool hov = m_upDropHover && m_upOpen < 0;
        // Verde solo con el cursor encima: es la senal de que ahi se suelta.
        const Color borde = hov ? kUpDropHover : kUpDropEdge;
        fb.fillRect(d, hov ? Color{ 16, 46, 26 } : kFieldBg);
        fb.drawRect(d, borde, hov ? 3 : 2);

        // Simbolo + giratorio, centrado en el rectangulo.
        const i32 cx = d.x + d.w / 2;
        const i32 cy = d.y + d.h / 2;
        const i32 r  = std::min(kUpPlusR, std::max(4, std::min(d.w, d.h) / 3));
        drawSpinningPlus(fb, cx, cy, r, m_upPlusTime, hov ? kUpDropHover : kGlyph);

        // Rotulo en dos lineas: "SUBIR" / "ARCHIVOS". En una sola no cabria en
        // un rectangulo estrecho y vertical.
        const char* l1 = "SUBIR";
        const char* l2 = "ARCHIVOS";
        const i32 w1 = font5x7::measure(l1, 1);
        const i32 w2 = font5x7::measure(l2, 1);
        if (w2 < d.w - 4) {
            const Color ct = hov ? kUpDropHover : kGlyph;
            font5x7::drawText(fb, cx - w1 / 2, cy + r + 10, l1, ct, 1);
            font5x7::drawText(fb, cx - w2 / 2, cy + r + 21, l2, ct, 1);

            // Se dice que pide una CARPETA ENTERA, que es lo unico que acepta:
            // si no, se queda esperando que alguien adivine por que el
            // selector no deja elegir un archivo.
            const char* h1 = "CARPETA";
            const char* h2 = "COMPLETA";
            const i32 hw1 = font5x7::measure(h1, 1);
            const i32 hw2 = font5x7::measure(h2, 1);
            if (hw2 < d.w - 4 && cy + r + 56 < d.y + d.h) {
                font5x7::drawText(fb, cx - hw1 / 2, cy + r + 38, h1, kDim, 1);
                font5x7::drawText(fb, cx - hw2 / 2, cy + r + 49, h2, kDim, 1);
            }
        }

        // Si ya se eligio una carpeta, se marca con una palomita verde.
        if (!m_upCodePath.empty()) {
            const char* ok = "OK";
            const i32 ow = font5x7::measure(ok, 1);
            font5x7::drawText(fb, cx - ow / 2, d.y + 8, ok, kOk, 1);
        }
    }

    // --- panel del campo abierto, por encima del formulario ---
    drawUploadPanel(fb);

    // --- aviso "Pon precio antes!", SOBRE todo lo demas ---
    // Va el ultimo a proposito: es una interrupcion, y tiene que leerse
    // aunque caiga encima de los botones o de un panel abierto. Dibujarlo
    // antes lo dejaria tapado por los campos que se pintan despues.
    if (m_upPriceWarn > 0.0 && area.w > 40) {
        // La misma franja dice las dos cosas: lo que falta (rojo) o que el
        // juego ya esta en la tienda (verde).
        const bool exito = m_upWarnOk;
        const Color base = exito ? kNeonGreen : kAccent;

        // El mensaje de exito es largo: en una linea caeria a la escala
        // minima y se leeria mas pequeno que el error, justo al reves de lo
        // que toca. Se parte en dos para poder dibujarlo grande.
        const char* l1 = exito ? "¡Subido a la Tienda!"
                       : (m_upWarnKind == kWarnCode)  ? "Sube los archivos primero!"
                       : (m_upWarnKind == kWarnFiles) ? "Guarda los archivos!"
                                                      : "Pon precio antes!";
        const char* l2 = exito ? "gracias por dar tu proyecto"
                       : (m_upWarnKind == kWarnCode)
                           ? "la carpeta completa del juego"
                       : (m_upWarnKind == kWarnFiles)
                           ? "usa ACTUALIZAR JUEGO" : nullptr;

        i32 escA = 4;
        while (escA > 1 &&
               (font5x7::measureUtf8(l1, escA) > area.w - 24 ||
                (l2 && font5x7::measureUtf8(l2, escA) > area.w - 24)))
            --escA;

        // La segunda linea va un punto mas pequena: es la coletilla, y que
        // pese menos que el titular es lo que hace que se lean en orden.
        const i32 escB = std::max(1, escA - 1);
        const i32 mw = font5x7::measureUtf8(l1, escA);
        const i32 mh = font5x7::kGlyphH * escA +
                       (l2 ? textr::glyphHeight(escB) + 6 : 0);

        // Franja opaca de lado a lado: sin ella el texto se mezclaria con los
        // botones de debajo y seria justo lo que no se quiere de un aviso.
        //
        // Cuelga del boton que lanzo la accion. Antes se anclaba a GUARDAR Y
        // SUBIR; al retirarse ese boton, el ancla es PUBLICAR -- pero el
        // aviso NO va pegado a el, que esta en la fila de arriba: se deja
        // caer hacia el centro del formulario, que es donde se lee sin tapar
        // los campos.
        const i32 pad = 10;
        const i32 anclaY = (m_upSaveRect.w > 0)
            ? m_upSaveRect.y + m_upSaveRect.h + 12
            : m_upPublishRect.y + m_upPublishRect.h + area.h / 3;
        const i32 franjaY = std::min(anclaY,
                                     area.y + area.h - mh - pad * 2 - 4);
        const Recti franja{ area.x + 6, franjaY,
                            std::max(1, area.w - 12), mh + pad * 2 };

        fb.fillRect(franja, kBg);

        // Borde que late con el mismo pulso que el boton, para que se lean
        // como una sola cosa. El de exito late mas despacio: es una
        // confirmacion, no una llamada de atencion.
        const f64 vel = exito ? 8.0 : 20.0;
        const f32 pulso = 0.6f +
            0.4f * static_cast<f32>(std::sin(m_upPriceWarn * vel));
        const Color borde{
            static_cast<u8>(std::clamp(base.r * pulso, 0.0f, 255.0f)),
            static_cast<u8>(std::clamp(base.g * pulso, 0.0f, 255.0f)),
            static_cast<u8>(std::clamp(base.b * pulso, 0.0f, 255.0f)) };
        fb.drawRect(franja, borde, 3);

        font5x7::drawTextUtf8(fb, franja.x + (franja.w - mw) / 2,
                              franja.y + pad, l1, base, escA);
        if (l2) {
            const i32 w2 = font5x7::measureUtf8(l2, escB);
            font5x7::drawTextUtf8(fb, franja.x + (franja.w - w2) / 2,
                                  franja.y + pad + textr::glyphHeight(escA) + 6,
                                  l2, base, escB);
        }
    }
}

// ---------------------------------------------------------------------------
//  EDAD RECOMENDADA: las tres opciones bajo el carril
//
//  El carril sigue mandando sobre el valor; esto son tres atajos para llegar
//  a el. Se despliega uno cada vez -- los tres a la vez no caben y, sobre
//  todo, elegir por dos vias a la vez no significa nada.
// ---------------------------------------------------------------------------

void HomeScene::layoutAgeOptions(Recti panel) {
    if (m_upOpen != kUpAge || panel.w <= 40) {
        for (i32 i = 0; i < kAgeOpts; ++i) m_ageOptRect[i] = Recti{};
        for (i32 i = 0; i < kAgeRanges; ++i) m_ageRangeRect[i] = Recti{};
        m_ageManualRect = m_ageManualOkRect = m_ageAiRect = Recti{};
        return;
    }

    const i32 m = 26;
    const i32 anchoU = std::max(1, panel.w - m * 2);
    const i32 hueco = 12;

    // El aviso del pie se reserva ANTES de repartir: es la linea que de
    // verdad se comia la ultima fila de tramos cuando se restaba al final.
    const i32 avisoH = textr::glyphHeight(1) + 10;
    const i32 fondoU = panel.y + panel.h - avisoH - 8;

    // Los tres botones, bajo los topes del carril. Altos de verdad: son el
    // mando de la seccion y antes quedaban como una tira fina.
    const i32 btnH  = 38;
    const i32 btnW  = std::max(1, (anchoU - hueco * 2) / kAgeOpts);
    // Cuelgan del ULTIMO carril, que es el de la maxima. Colgarlos del
    // primero los dejaria encima del segundo.
    const i32 bajo  = (m_upTrack2Rect.w > 0) ? m_upTrack2Rect.y
                                             : m_upTrackRect.y;
    // El hueco bajo el ultimo carril: los topes del rango (4 y 90) mas aire.
    // Con dos carriles cada pixel de mas aqui sale de la rejilla de tramos,
    // asi que se deja lo justo para que los topes no rocen los botones.
    const i32 btnY  = bajo + kUpTrackH + 10 + textr::glyphHeight(1) + 10;

    for (i32 i = 0; i < kAgeOpts; ++i)
        m_ageOptRect[i] = Recti{ panel.x + m + i * (btnW + hueco), btnY,
                                 btnW, btnH };

    // Zona desplegada: desde bajo los botones hasta donde empieza el aviso.
    // El "(opcional)" del tercero vive en ese hueco, asi que se cuenta.
    const i32 zonaY = btnY + btnH + textr::glyphHeight(1) + 12;
    const i32 zonaH = std::max(0, fondoU - zonaY);

    if (m_ageMode == kAgeManual) {
        // Campo y boton de confirmar, en una fila centrada y mas alta: es
        // donde se escribe, y un campo de 30 px con la fuente del sistema
        // deja la cifra pegada a los bordes.
        const i32 filaH  = 38;
        const i32 okW    = 118;
        const i32 campoW = std::min(220, std::max(1, anchoU - okW - hueco));
        const i32 filaW  = campoW + hueco + okW;
        const i32 filaX  = panel.x + (panel.w - filaW) / 2;
        m_ageManualRect   = Recti{ filaX, zonaY, campoW, filaH };
        m_ageManualOkRect = Recti{ filaX + campoW + hueco, zonaY, okW, filaH };
        for (i32 i = 0; i < kAgeRanges; ++i) m_ageRangeRect[i] = Recti{};
        m_ageAiRect = Recti{};
    } else if (m_ageMode == kAgeRange) {
        // Rejilla de 2 columnas x 4 filas: ocho tramos que se leen de un
        // vistazo sin tener que desplazar nada.
        //
        // La altura de celda se REPARTE el hueco en vez de toparse en 30 px.
        // Con el tope, el espacio que gano el carril al subir se quedaba sin
        // usar y los tramos seguian igual de apretados.
        const i32 cols = 2, filas = (kAgeRanges + cols - 1) / cols;
        const i32 cW = std::max(1, (anchoU - hueco) / cols);

        // El hueco ENTRE filas cede antes que la celda: si se mantuviera fijo
        // y la celda se acotara por abajo, la rejilla acabaria saliendose del
        // panel -- que es justo lo que paso. Primero se intenta con el hueco
        // normal; si no da, se aprieta hasta 2 px antes de encoger la celda.
        i32 sep = hueco;
        i32 cH  = (zonaH - sep * (filas - 1)) / filas;
        while (cH < 30 && sep > 2) {
            --sep;
            cH = (zonaH - sep * (filas - 1)) / filas;
        }
        cH = std::clamp(cH, 14, 46);

        // Y el reparto se COMPRUEBA: si aun asi no entra, se recorta la celda
        // hasta que entre. Mas vale una fila apretada que una fuera del
        // panel, donde no se ve ni se puede pulsar.
        const i32 altoTotal = cH * filas + sep * (filas - 1);
        if (altoTotal > zonaH && filas > 0)
            cH = std::max(12, (zonaH - sep * (filas - 1)) / filas);

        for (i32 i = 0; i < kAgeRanges; ++i) {
            const i32 c = i % cols, f = i / cols;
            m_ageRangeRect[i] = Recti{ panel.x + m + c * (cW + sep),
                                       zonaY + f * (cH + sep), cW, cH };
        }
        m_ageManualRect = m_ageManualOkRect = m_ageAiRect = Recti{};
    } else if (m_ageMode == kAgeAi) {
        const i32 aiW = std::min(280, anchoU);
        m_ageAiRect = Recti{ panel.x + (panel.w - aiW) / 2, zonaY, aiW, 40 };
        for (i32 i = 0; i < kAgeRanges; ++i) m_ageRangeRect[i] = Recti{};
        m_ageManualRect = m_ageManualOkRect = Recti{};
    } else {
        for (i32 i = 0; i < kAgeRanges; ++i) m_ageRangeRect[i] = Recti{};
        m_ageManualRect = m_ageManualOkRect = m_ageAiRect = Recti{};
    }
}

void HomeScene::drawAgeOptions(Framebuffer& fb, Recti panel) {
    if (m_upOpen != kUpAge || m_ageOptRect[0].w <= 0) return;

    // El panel de EDAD tiene fondo CLARO en las edades bajas: el texto tiene
    // que seguirlo o se vuelve ilegible. Es la misma regla que usa el resto
    // de la seccion.
    const bool claro = uploadAgeBackground().r > 150;
    const Color cTxt   = claro ? kBg : kText;
    const Color cTenue = claro ? kBg : kDim;

    static const char* kRotulos[kAgeOpts] = {
        "ESCRIBIR EDAD", "TRAMOS", "SELECTOR POR AI"
    };

    for (i32 i = 0; i < kAgeOpts; ++i) {
        const Recti r = m_ageOptRect[i];
        if (r.w <= 0) continue;
        const bool abierto = (m_ageMode == i);
        const bool hov     = (m_ageOptHover == i);

        // El selector AI va en violeta y los otros dos en verde menta: es el
        // opcional, y el color lo dice antes de leerlo.
        const Color propio = (i == kAgeAi) ? kUpPurple : kUpMint;
        fb.fillRect(r, abierto ? propio : (hov ? kFieldBg : kBg));
        fb.drawRect(r, abierto ? kWhite : propio, abierto ? 2 : 1);
        // drawFitted baja la escala sola si el rotulo no entra, asi que se le
        // pide 2 y el decide: en un panel ancho se leen grandes, y en uno
        // estrecho encogen en vez de recortarse.
        font5x7::drawFitted(fb, r, kRotulos[i],
                            abierto ? kBg : (claro ? kBg : kText), 2, 6);
    }

    // "opcional" bajo el tercero, que es lo que lo distingue de los otros dos.
    const Recti ai = m_ageOptRect[kAgeAi];
    if (ai.w > 0) {
        const char* op = "(opcional)";
        const i32 ow = font5x7::measureUtf8(op, 1);
        if (ow < ai.w)
            font5x7::drawTextUtf8(fb, ai.x + (ai.w - ow) / 2,
                                  ai.y + ai.h + 2, op, cTenue, 1);
    }

    // --- lo desplegado ---
    if (m_ageMode == kAgeManual && m_ageManualRect.w > 0) {
        m_ageManualField.setRect(m_ageManualRect);
        m_ageManualField.render(fb);

        const Recti ok = m_ageManualOkRect;
        fb.fillRect(ok, m_ageManualOkDown ? kFieldBg
                      : (m_ageManualOkHover ? kUpMint : kBg));
        fb.drawRect(ok, kUpMint, 2);
        font5x7::drawFitted(fb, ok, "CONFIRMAR",
                            m_ageManualOkHover ? kBg : cTxt, 2, 6);
    } else if (m_ageMode == kAgeRange) {
        for (i32 i = 0; i < kAgeRanges; ++i) {
            const Recti r = m_ageRangeRect[i];
            if (r.w <= 0) continue;
            const bool sel = (m_ageRangeSel == i);
            const bool hov = (m_ageRangeHover == i);
            fb.fillRect(r, sel ? kUpMint : (hov ? kFieldBg : kBg));
            fb.drawRect(r, sel ? kWhite : kUpMint, sel ? 2 : 1);
            font5x7::drawFitted(fb, r, ageRangeLabel(i),
                                sel ? kBg : (claro ? kBg : kText), 2, 8);
        }
    } else if (m_ageMode == kAgeAi && m_ageAiRect.w > 0) {
        const Recti r = m_ageAiRect;
        fb.fillRect(r, m_ageAiDown ? kFieldBg
                      : (m_ageAiHover ? kUpPurple : kBg));
        fb.drawRect(r, kUpPurple, 2);
        font5x7::drawFitted(fb, r,
                            m_ageAiSpin > 0.0 ? "CALCULANDO..." : "PROPONER EDAD",
                            m_ageAiHover ? kBg : cTxt, 2, 8);

        // La propuesta, debajo. Mientras dura el sorteo se van mostrando
        // opciones al azar: el numero final se lee como el resultado de algo,
        // no como un texto que aparece de golpe.
        i32 mostrar = m_ageAiPick;
        if (m_ageAiSpin > 0.0) {
            const u32 v = static_cast<u32>(m_ageAiSpin * 997.0);
            mostrar = static_cast<i32>(v % static_cast<u32>(kAgeAiOpts));
        }
        if (mostrar >= 0) {
            // La propuesta es el RESULTADO: se le da una caja ancha y alta
            // en el hueco que queda hasta el aviso, y drawFitted la dibuja
            // tan grande como quepa. Antes se pintaba en una linea justa y
            // el dato principal se leia igual de pequeno que el resto.
            const char* txt  = ageAiLabel(mostrar);
            const i32 avisoH = textr::glyphHeight(1) + 10;
            const i32 ty     = r.y + r.h + 14;
            const i32 alto   = std::max(0, panel.y + panel.h - avisoH - 8 - ty);
            if (alto >= textr::glyphHeight(1))
                font5x7::drawFitted(fb,
                    { panel.x + 12, ty, std::max(1, panel.w - 24),
                      std::min(alto, textr::glyphHeight(3) + 8) },
                    txt, m_ageAiSpin > 0.0 ? cTenue : kUpPurple, 3, 8);
        }
    }

    // Aviso al pie del panel, comun a las tres vias.
    //
    // El hueco lo reserva layoutAgeOptions con esta MISMA cuenta
    // (glyphHeight(1) + 10). Si las dos dejaran de coincidir, el aviso se
    // montaria sobre la ultima fila de tramos.
    if (!m_ageMsg.empty()) {
        const i32 my = panel.y + panel.h - (textr::glyphHeight(1) + 10) + 2;
        font5x7::drawFitted(fb, { panel.x + 12, my,
                                  std::max(1, panel.w - 24),
                                  textr::glyphHeight(1) },
                            m_ageMsg.c_str(),
                            m_ageMsgErr ? kConsentRed : cTxt, 1, 0);
    }
}

void HomeScene::updateAgeOptions(Engine& engine, f64 dt) {
    if (m_upOpen != kUpAge) return;

    Input& in = engine.input();
    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);
    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    if (m_ageAiSpin > 0.0) m_ageAiSpin = std::max(0.0, m_ageAiSpin - dt);

    // --- los tres botones de modo ---
    m_ageOptHover = kAgeNone;
    for (i32 i = 0; i < kAgeOpts; ++i)
        if (dentro(m_ageOptRect[i])) m_ageOptHover = i;

    if (in.mousePressed(0) && m_ageOptHover >= 0) m_ageOptDown = m_ageOptHover;

    // --- lo desplegado ---
    m_ageRangeHover = -1;
    if (m_ageMode == kAgeRange)
        for (i32 i = 0; i < kAgeRanges; ++i)
            if (dentro(m_ageRangeRect[i])) m_ageRangeHover = i;

    m_ageManualOkHover = dentro(m_ageManualOkRect);
    m_ageAiHover       = dentro(m_ageAiRect);
    if (in.mousePressed(0)) {
        if (m_ageManualOkHover) m_ageManualOkDown = true;
        if (m_ageAiHover)       m_ageAiDown       = true;
    }

    // El campo de la cifra solo escucha con su modo abierto.
    if (m_ageMode == kAgeManual && m_ageManualRect.w > 0) {
        m_ageManualField.setFocused(true);
        m_ageManualField.update(dt);

        if (in.mousePressed(0) && m_ageManualField.contains(lx, ly))
            m_ageManualField.onMouseDown(lx, ly);

        const bool shift = in.keyDown(VK_SHIFT);
        const bool ctrl  = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);
        const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                               VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };
        for (const u32 vk : teclas)
            if (in.keyRepeat(vk)) m_ageManualField.onKey(vk, shift, ctrl);
        for (const wchar_t ch : in.charsTyped()) {
            if (ctrl || ch < 32) continue;
            m_ageManualField.onChar(ch);
        }
        // Enter confirma: es lo que se espera tras escribir una cifra.
        if (in.keyPressed(VK_RETURN)) applyManualAge();
    }

    if (in.mouseReleased(0)) {
        if (m_ageOptDown >= 0 && m_ageOptDown == m_ageOptHover) {
            // Volver a pulsar el mismo lo cierra: el panel recupera su sitio
            // y el carril queda despejado.
            m_ageMode = (m_ageMode == m_ageOptDown) ? kAgeNone : m_ageOptDown;
            m_ageMsg.clear();
        } else if (m_ageManualOkDown && m_ageManualOkHover) {
            applyManualAge();
        } else if (m_ageAiDown && m_ageAiHover) {
            rollAgeAi();
        } else if (m_ageRangeHover >= 0) {
            pickAgeRange(m_ageRangeHover);
        }
        m_ageOptDown      = kAgeNone;
        m_ageManualOkDown = false;
        m_ageAiDown       = false;
    }
}

void HomeScene::drawUploadPanel(Framebuffer& fb) {
    if (m_upOpen < 0 || m_upOpen >= kUploadSlots || m_upPanelRect.w <= 0) return;

    const Recti p = m_upPanelRect;

    // Dos secciones tintan su propio fondo: EDAD segun la edad marcada, y
    // RENDIMIENTO con un amarillo neon rebajado -- a plena saturacion el
    // texto encima no se leeria, asi que se usa muy oscurecido y es el carril
    // el que lleva el neon puro.
    const Color fondo = (m_upOpen == kUpAge)  ? uploadAgeBackground()
                      : (m_upOpen == kUpPerf) ? Color{ 26, 28, 6 }
                                              : kPanelDeep;
    fb.fillRect(p, fondo);

    const Color marco = (m_upOpen == kUpName)   ? kUpIndigo
                      : (m_upOpen == kUpDesc)   ? kUpPurple
                      : (m_upOpen == kUpGenre)  ? kUpPink
                      : (m_upOpen == kUpPerf)   ? kNeonYellow
                      : (m_upOpen == kUpAge)    ? kUpMint
                      : (m_upOpen == kUpMulti)  ? kUpMultiRed
                      : (m_upOpen == kUpPirate) ? kConsentGreen
                      : (m_upOpen == kUpArt)    ? kUpArtEdge : kNeonGreen;
    fb.drawRect(p, marco, 2);

    // En la seccion EDAD el fondo puede ser claro (azul cielo), asi que el
    // texto se elige por luminosidad para que siempre se lea.
    const bool fondoClaro = (m_upOpen == kUpAge) &&
        (fondo.r * 299 + fondo.g * 587 + fondo.b * 114) / 1000 > 128;
    const Color cTexto = fondoClaro ? kBg : kText;
    const Color cTenue = fondoClaro ? Color{ 40, 40, 40 } : kDim;

    const char* titulos[kUploadSlots] = {
        "NOMBRE DEL JUEGO", "DESCRIPCIÓN DEL JUEGO", "ELEGIR GÉNERO",
        "ELEGIR RENDIMIENTO", "EDAD RECOMENDADA", "TIPO DE MULTIJUGADOR",
        "PERMISO DE PIRATERÍA", "DISEÑO DE SECCIÓN",
        "PERMITIR PERSONALIZACIÓN", ""
    };
    font5x7::drawTextUtf8(fb, p.x + 12, p.y + 10, titulos[m_upOpen], marco, 1);

    const char* cerrar = "ESC CIERRA";
    const i32 cw = font5x7::measure(cerrar, 1);
    if (cw < p.w - 24)
        font5x7::drawText(fb, p.x + p.w - cw - 12, p.y + 10, cerrar, cTenue, 1);

    // ---- campos de texto: nombre, descripcion, generos y multijugador ----
    if (m_upOpen == kUpName || m_upOpen == kUpDesc ||
        m_upOpen == kUpGenre || m_upOpen == kUpMulti) {
        TextField& campo = (m_upOpen == kUpName)  ? m_upNameField
                         : (m_upOpen == kUpDesc)  ? m_upDescField
                         : (m_upOpen == kUpGenre) ? m_upGenreField
                                                  : m_upMultiField;
        // La descripcion es la que necesita sitio: 1000 caracteres piden un
        // cuadro alto, los otros dos van en una linea.
        const i32 alto = (m_upOpen == kUpDesc) ? std::min(96, p.h / 2) : 34;
        campo.setRect({ p.x + 10, p.y + 30, std::max(1, p.w - 20), alto });
        campo.render(fb);

        if (m_upOpen == kUpDesc) {
            char cnt[48]{};
            snprintf(cnt, sizeof(cnt), "%zu / %zu CARACTERES",
                     m_upDescField.length(), kUpDescMax);
            font5x7::drawText(fb, p.x + 12, p.y + 30 + alto + 8, cnt, cTenue, 1);
        }
    }

    // ---- listas de etiquetas: generos (campo 3) y multijugador (campo 6) ----
    // Comparten dibujado: solo cambian los datos, los colores del ovalo y el
    // rotulo. Duplicar el bucle seria dos sitios donde corregir el recorte.
    if (m_upOpen == kUpGenre || m_upOpen == kUpMulti) {
        const bool esGenero = (m_upOpen == kUpGenre);
        ScrollPanel& panel  = esGenero ? m_upGenrePanel : m_upMultiPanel;
        const i32 total     = esGenero ? kUpGenreCount : kUpMultiCount;
        const auto& marcados = esGenero ? m_upGenres : m_upMulti;
        const Color cRelleno = esGenero ? kUpPink : kUpMultiRed;
        const Color cFilo    = esGenero ? kUpBlue : kUpMultiBlue;

        font5x7::drawTextUtf8(fb, p.x + 12, p.y + 66,
                              esGenero ? "MARCA LOS GÉNEROS QUE APLIQUEN:"
                                       : "MARCA LOS MODOS QUE APLIQUEN:",
                              cTenue, 1);

        const Recti cr = panel.contentRect();
        const i32 porFila = std::max(1, cr.w / 104);
        const i32 celdaW  = std::max(1, cr.w / porFila);
        const i32 filas   = (total + porFila - 1) / porFila;
        panel.setContentHeight(filas * kUpGenreRowH + 6);
        panel.renderBar(fb);

        for (i32 i = 0; i < total; ++i) {
            const i32 fila = i / porFila;
            const i32 col  = i % porFila;
            const i32 x = cr.x + col * celdaW;
            const i32 y = cr.y + fila * kUpGenreRowH - panel.offset();

            // Recorte al area visible del panel: sin esto las etiquetas se
            // dibujarian por encima del resto de la seccion al desplazar.
            if (y + kUpGenreRowH <= cr.y || y >= cr.y + cr.h) continue;

            const bool marcado =
                std::find(marcados.begin(), marcados.end(), i) != marcados.end();

            // Ovalo acotado: rectangulo con dos semicirculos en los extremos.
            const i32 ow = celdaW - 6, oh = kUpGenreRowH - 5;
            const i32 rr = oh / 2;
            const Color oc = marcado ? cRelleno : kFieldBg;
            fb.fillRect({ x + rr, y, std::max(1, ow - oh), oh }, oc);
            fb.fillCircle(x + rr, y + rr, rr, oc);
            fb.fillCircle(x + ow - rr, y + rr, rr, oc);
            if (marcado) {
                fb.drawCircle(x + rr, y + rr, rr, cFilo, 1);
                fb.drawCircle(x + ow - rr, y + rr, rr, cFilo, 1);
            }

            const std::string et = esGenero ? uploadGenreLabel(i)
                                            : uploadMultiLabel(i);
            i32 esc = 1;
            const i32 tw = font5x7::measureUtf8(et.c_str(), esc);   // ACCIÓN...
            if (tw < ow - 8)
                font5x7::drawTextUtf8(fb, x + (ow - tw) / 2,
                                      y + (oh - textr::glyphHeight(1)) / 2,
                                      et.c_str(), marcado ? kWhite : kGlyph, esc);
        }
    }

    // ---- avisos de condiciones: pirateria (7) y personalizacion (9) ----
    if (m_upOpen == kUpPirate)
        drawConsentNotice(fb, p, kPirateNotice, kPirateNoticeLines,
                          m_upPirateConsent, kConsentGreen, kConsentRed,
                          "ACEPTO LAS CONDICIONES", "NO ACEPTO LAS CONDICIONES");

    if (m_upOpen == kUpCustom)
        drawConsentNotice(fb, p, kCustomNotice, kCustomNoticeLines,
                          m_upCustomConsent, kNeonGreen, kNeonRed,
                          "ACEPTO", "RECHAZO");

    // ---- campo 8: imagen promocional de la tienda ----
    if (m_upOpen == kUpArt) {
        const Recti d = m_upArtDropRect;
        if (d.w > 0 && d.h > 0) {
            fb.fillRect(d, kBg);

            if (m_upArtW > 0 && m_upArtH > 0 && !m_upArtPixels.empty()) {
                // Vista previa encajada en el rectangulo, conservando la
                // proporcion: deformarla enganaria sobre como se vera.
                const i32 escalaW = d.w - 8, escalaH = d.h - 8;
                const i32 porAncho = escalaW * m_upArtH / std::max(1, m_upArtW);
                const i32 dstW = (porAncho <= escalaH) ? escalaW
                                                       : escalaH * m_upArtW /
                                                         std::max(1, m_upArtH);
                const i32 dstH = (porAncho <= escalaH) ? porAncho : escalaH;
                const i32 ox = d.x + (d.w - dstW) / 2;
                const i32 oy = d.y + (d.h - dstH) / 2;

                for (i32 yy = 0; yy < dstH; ++yy) {
                    for (i32 xx = 0; xx < dstW; ++xx) {
                        const i32 sx = std::min(m_upArtW - 1,
                                                xx * m_upArtW / std::max(1, dstW));
                        const i32 sy = std::min(m_upArtH - 1,
                                                yy * m_upArtH / std::max(1, dstH));
                        fb.setPixel(ox + xx, oy + yy,
                                    m_upArtPixels[static_cast<size_t>(sy) * m_upArtW + sx]);
                    }
                }
            } else {
                // Sin imagen: se explica que va aqui y como ponerla.
                const char* l1 = "PULSA PARA ELEGIR LA IMAGEN PROMOCIONAL";
                const char* l2 = "Es la que se vera en la tienda";
                const i32 w1 = font5x7::measureUtf8(l1, 1);
                const i32 w2 = font5x7::measureUtf8(l2, 1);
                if (w1 < d.w - 10) {
                    font5x7::drawTextUtf8(fb, d.x + (d.w - w1) / 2,
                                          d.y + d.h / 2 - 10, l1,
                                          m_upArtHover ? kUpArtHover : kDim, 1);
                    font5x7::drawTextUtf8(fb, d.x + (d.w - w2) / 2,
                                          d.y + d.h / 2 + 4, l2, kDim, 1);
                }
            }

            fb.drawRect(d, m_upArtHover ? kUpArtHover : kUpArtEdge,
                        m_upArtDown ? 3 : 2);
        }

        // Nombre del archivo y resultado de la carga, bajo el rectangulo.
        if (!m_upArtPath.empty()) {
            const std::string n = "Imagen: " + nombreVisible(m_upArtPath);
            const i32 nw = font5x7::measureUtf8(n.c_str(), 1);
            if (nw < p.w - 24)
                font5x7::drawTextUtf8(fb, p.x + 12, d.y + d.h + 10,
                                      n.c_str(), kText, 1);
        }
        if (!m_upArtMsg.empty()) {
            const i32 mw = font5x7::measureUtf8(m_upArtMsg.c_str(), 1);
            if (mw < p.w - 24)
                font5x7::drawTextUtf8(fb, p.x + 12, d.y + d.h + 24,
                                      m_upArtMsg.c_str(), kGlyph, 1);
        }
    }

    // ---- deslizadores: rendimiento y edad ----
    if (m_upOpen == kUpPerf || m_upOpen == kUpAge) {
        const bool esFps = (m_upOpen == kUpPerf);
        // Rendimiento: carril marron con selector negro. Edad: carril rosa con
        // selector verde menta.
        const Color cCarril = esFps ? kNeonYellow : kUpPink;
        const Color cKnob   = esFps ? kBg      : kUpMint;

        fb.fillRect(m_upTrackRect, cCarril);
        fb.drawRect(m_upTrackRect, fondoClaro ? kBg : kPanelEdge, 1);

        const bool dosCarriles = (!esFps && m_upTrack2Rect.w > 0);
        if (dosCarriles) {
            fb.fillRect(m_upTrack2Rect, cCarril);
            fb.drawRect(m_upTrack2Rect, fondoClaro ? kBg : kPanelEdge, 1);

            // La franja ELEGIDA, de un selector al otro. Va ANTES que los
            // selectores: si se pintara despues, los taparia.
            const i32 x1 = m_upKnobRect.x + kUpKnobW / 2;
            const i32 x2 = m_upKnob2Rect.x + kUpKnobW / 2;
            const i32 xa = std::min(x1, x2), xb = std::max(x1, x2);
            if (xb > xa) {
                const Color cFranja{ kUpMint.r, kUpMint.g, kUpMint.b, 90 };
                for (i32 x = xa; x < xb; ++x)
                    for (i32 y = m_upTrackRect.y;
                         y < m_upTrack2Rect.y + kUpTrackH; ++y)
                        fb.blendPixel(x, y, cFranja);
            }

            // Cada carril dice cual es. Sin esto son dos barras iguales y hay
            // que adivinar cual mueve que.
            const Color cEtiq = fondoClaro ? kBg : kDim;
            font5x7::drawTextUtf8(fb, m_upTrackRect.x,
                                  m_upTrackRect.y - textr::glyphHeight(1) - 3,
                                  "MINIMA", cEtiq, 1);
            font5x7::drawTextUtf8(fb, m_upTrack2Rect.x,
                                  m_upTrack2Rect.y - textr::glyphHeight(1) - 3,
                                  "MAXIMA", cEtiq, 1);
        }

        fb.fillRect(m_upKnobRect, cKnob);
        const bool arr = esFps ? m_upFpsDrag : m_upAgeDrag;
        fb.drawRect(m_upKnobRect, arr ? kWhite : (fondoClaro ? kBg : kGlyph), 2);

        if (dosCarriles) {
            fb.fillRect(m_upKnob2Rect, cKnob);
            fb.drawRect(m_upKnob2Rect,
                        m_upAgeMaxDrag ? kWhite : (fondoClaro ? kBg : kGlyph), 2);
        }

        // Valor grande, centrado en el carril (no en el selector) para que el
        // numero cambie sin que su sitio baile al arrastrar.
        char val[48]{};
        if (esFps) snprintf(val, sizeof(val), "%d FPS", uploadFps());
        // La franja entera, que es el dato: "12 - 16 AÑOS" dice a quien va
        // dirigido el juego, y un numero solo no.
        else       snprintf(val, sizeof(val), "%d - %d AÑOS",
                            uploadAge(), uploadAgeTop());

        // El numero grande, encima del carril. La separacion usa el alto REAL
        // de la letra: con kGlyphH (los 7 px de la bitmap) el hueco se
        // quedaba corto, y al subir el carril de EDAD el numero se habria
        // salido por arriba del panel.
        const i32 centroX = m_upTrackRect.x + m_upTrackRect.w / 2;
        const i32 vw = font5x7::measureUtf8(val, 3);   // "AÑOS"
        const i32 valY = std::max(p.y + 6,
                                  m_upTrackRect.y - textr::glyphHeight(3) - 14);
        font5x7::drawTextUtf8(fb, centroX - vw / 2, valY, val,
                              esFps ? kNeonYellow : cTexto, 3);

        // Topes del carril, para que se lea el rango sin adivinarlo.
        char lo[24]{}, hi[24]{};
        if (esFps) {
            snprintf(lo, sizeof(lo), "%d", kUpFpsMin);
            snprintf(hi, sizeof(hi), "%d", kUpFpsMax);
        } else {
            snprintf(lo, sizeof(lo), "%d", kUpAgeMin);
            snprintf(hi, sizeof(hi), "%d", kUpAgeMax);
        }
        // Van bajo el ULTIMO carril: con dos, ponerlos bajo el primero los
        // dejaria encima del segundo. El rango es el mismo para los dos, asi
        // que basta con rotularlo una vez, al pie.
        const i32 hw = font5x7::measure(hi, 1);
        const Recti ultimo = dosCarriles ? m_upTrack2Rect : m_upTrackRect;
        const i32 topeY = ultimo.y + kUpTrackH + 10;
        font5x7::drawText(fb, ultimo.x, topeY, lo, cTenue, 1);
        font5x7::drawText(fb, ultimo.x + ultimo.w - hw, topeY, hi, cTenue, 1);

        // El rendimiento se explica: el rango es enorme y el numero solo no
        // dice si la eleccion es razonable.
        if (esFps) {
            const i32 f = uploadFps();
            const char* nota = (f < 30)  ? "MÍNIMO JUGABLE"
                             : (f < 60)  ? "SUAVE"
                             : (f < 120) ? "MUY FLUIDO"
                             : (f < 500) ? "ALTA FRECUENCIA"
                                         : "SOLO MONITORES ESPECIALES";
            const i32 nw = font5x7::measureUtf8(nota, 1);   // "MÍNIMO"
            if (nw < p.w - 24)
                font5x7::drawTextUtf8(fb, centroX - nw / 2, topeY + 18, nota,
                                      kNeonYellow, 1);
        }

        // Bajo el carril de EDAD: las tres formas de fijarla.
        if (!esFps) drawAgeOptions(fb, p);
    }
}

// ===========================================================================
//  ACTUALIZAR JUEGO *ES* GUARDAR ARCHIVOS
//
//  La seccion se vacio por completo y adopto el sistema de GUARDAR ARCHIVOS,
//  que es el que de verdad mete el juego en la plataforma: un campo con el
//  CODIGO, el boton GENERAR CODIGO, elegir carpeta y guardar -- empaquetando
//  y cifrando, no apuntando una ruta.
//
//  Lo que habia antes (cuatro pasos, zona de subida, change log, el aviso de
//  compatibilidad) se retiro. Las tres funciones delegan en las de archivos
//  en vez de duplicarlas: una sola implementacion que mantener.
// ===========================================================================

void HomeScene::layoutDevUpdate(Recti area) {
    layoutDevFiles(area);
}

void HomeScene::lookupUpdateGame() {
    // El paso 1 identifica por CODIGO desde que GUARDAR ARCHIVOS se fundio
    // aqui. `lookupArchiveCode` ya lo resolvio contra el catalogo; esto solo
    // traduce el resultado al indicador de los tres estados.
    //
    //   -1 = nada escrito
    //    0 = hay codigo pero no cuadra con ningun juego publicado
    //    1 = cuadra
    //
    // Un 0 NO impide actualizar: quien genero su codigo y aun no ha
    // publicado tiene un paquete pendiente que la primera publicacion
    // adopta. Solo sirve para enseñar a que juego apunta.
    if (m_arcCodeField.text().empty()) { m_updFound = -1; return; }
    m_updFound = m_arcCodeJuego.empty() ? 0 : 1;
}

void HomeScene::submitUpdate() {
    // Registra la version en el catalogo y en el historial. El empaquetado y
    // cifrado los hace `startArchive`, que es el boton GUARDAR de la seccion;
    // esto es lo que corre despues, cuando el paquete ya existe.
    //
    // El CODIGO identifica el juego: es el unico campo que hay.
    const std::string code = m_arcCodeField.text();
    if (code.empty()) {
        m_devMsg    = "Escribe el codigo del juego.";
        m_devMsgErr = true;
        return;
    }

    // El nombre sale del codigo. Si aun no corresponde a nada publicado, se
    // usa el propio codigo: el paquete queda pendiente y lo adopta la primera
    // publicacion.
    const std::string nombre = m_arcCodeJuego.empty() ? code : m_arcCodeJuego;

    // La carpeta es la que se eligio con ELEGIR CARPETA.
    if (m_arcFolder.empty()) {
        m_devMsg    = "Elige la carpeta del juego.";
        m_devMsgErr = true;
        return;
    }

    std::string err;
    if (!devcatalog::addVersionWithLog(kDevCatalogFile, nombre, m_arcFolder,
                                       std::string(), err)) {
        m_devMsg    = err;
        m_devMsgErr = true;
        return;
    }

    // La version anterior se archiva y la nueva pasa a ser la jugable, con su
    // fecha: es lo que alimenta la linea de tiempo de versiones que ya se ve
    // en la ficha del producto y en la biblioteca.
    std::string herr;
    bool tenia = false;
    const history::GameHistory h =
        history::forGame(kHistoryFile, nombre, &tenia);
    if (tenia && !h.versions.empty())
        history::archiveAndAddVersion(kHistoryFile, nombre, m_arcFolder,
                                      fileSizeOf(m_arcFolder), herr);
    else
        history::noteInitialVersion(kHistoryFile, nombre, m_arcFolder,
                                    fileSizeOf(m_arcFolder), herr);

    m_devGamesLoaded = false;   // recargar el catalogo del desarrollador
    m_storeLoaded    = false;   // y que la Tienda vea la version nueva
    m_historyLoaded  = false;   // y la biblioteca su linea de tiempo

    m_devMsg    = "Version publicada: " + nombre;
    m_devMsgErr = false;
}

void HomeScene::updateDevUpdate(Engine& engine, f64 dt) {
    updateDevFiles(engine, dt);
}

void HomeScene::layoutDevDelete(Recti area) {
    if (area.w <= 40 || area.h <= 40) {
        m_delFieldRect = m_delYesRect = m_delNoRect = Recti{};
        return;
    }

    const i32 m = 16;
    m_delFieldRect = Recti{ area.x + m, area.y + 58,
                            std::max(1, area.w - m * 2), 32 };

    // Los dos botones al fondo, y el aviso ocupando lo que queda en medio.
    const i32 btnH = 30;
    const i32 btnY = area.y + area.h - 14 - btnH;
    const i32 hueco = 12;
    const i32 btnW = std::max(1, (area.w - m * 2 - hueco) / 2);
    m_delYesRect = Recti{ area.x + m, btnY, btnW, btnH };
    m_delNoRect  = Recti{ area.x + m + btnW + hueco, btnY, btnW, btnH };

    const i32 avisoY = m_delFieldRect.y + m_delFieldRect.h + 26;
    m_delNoticePanel.setRect({ area.x + m, avisoY,
                               std::max(1, area.w - m * 2),
                               std::max(1, btnY - 12 - avisoY) });
}

void HomeScene::lookupDeleteGame() {
    // Se identifica por CODIGO, igual que ACTUALIZAR. Un nombre se escribe mal
    // con facilidad y dos juegos pueden llamarse parecido; el codigo es unico y
    // no se teclea de memoria, asi que retirar el juego equivocado -- que es
    // irreversible -- deja de ser un descuido posible.
    const std::string code = m_delCodeField.text();
    if (code.empty()) { m_delFound = -1; m_delJuego.clear(); return; }

    // findByCode acepta el codigo con o sin '#' y en cualquier caja: quien lo
    // pega de un sitio no deberia tener que acertar con el formato.
    m_delJuego = devcatalog::findByCode(kDevCatalogFile, code);
    m_delFound = m_delJuego.empty() ? 0 : 1;
}

void HomeScene::confirmDeleteGame() {
    // El codigo ya se resolvio al nombre; de aqui en adelante se trabaja con
    // el nombre, que es con lo que el catalogo y las bibliotecas indexan.
    const std::string n = m_delJuego;
    if (n.empty() || m_delFound != 1) {
        m_delMsg    = "Escribe el codigo de un juego tuyo.";
        m_delMsgErr = true;
        return;
    }

    // 1) Fuera de la tienda: nadie mas puede descargarlo.
    std::string err;
    if (!devcatalog::remove(kDevCatalogFile, n, err)) {
        m_delMsg    = err;
        m_delMsgErr = true;
        return;
    }

    // 2) LIBERAR a quien ya lo tenga. No se borra de su biblioteca -- el
    //    juego es suyo -- pero se le quita el cifrado, asi que pasa a tener
    //    los archivos completos y modificables.
    i32 liberados = 0;
    for (const auto& e : library::load(kLibraryFile)) {
        if (e.name != n) continue;
        std::string rerr;
        if (gamevault::releaseGame(e.exe, e.vault, rerr)) ++liberados;

        // La entrada se reescribe sin paquete cifrado: ya no hay nada que
        // descifrar al jugar.
        library::Entry libre = e;
        libre.vault.clear();
        library::add(kLibraryFile, libre);
    }

    m_storeLoaded    = false;   // la Tienda deja de mostrarlo
    m_devGamesLoaded = false;   // y el catalogo propio
    m_libraryLoaded  = false;   // la biblioteca refleja los archivos libres

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "%s retirado de la tienda. %d copia(s) liberada(s).",
                  n.c_str(), liberados);
    m_delMsg    = buf;
    m_delMsgErr = false;

    m_delCodeField.setText("");
    m_delFound = -1;
    m_delJuego.clear();
}

void HomeScene::updateDevDelete(Engine& engine, f64 dt) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    // --- campo del codigo ---
    m_delCodeField.setFocused(true);
    m_delCodeField.update(dt);

    if (in.mousePressed(0) && m_delCodeField.contains(lx, ly))
        m_delCodeField.onMouseDown(lx, ly);
    else if (in.mouseDown(0) && m_delCodeField.focused())
        m_delCodeField.onMouseDrag(lx, ly);

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);
    const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                           VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };
    for (const u32 vk : teclas)
        if (in.keyRepeat(vk)) m_delCodeField.onKey(vk, shift, ctrlAtajo);

    for (const wchar_t ch : in.charsTyped()) {
        if (ctrlAtajo || ch < 32) continue;
        m_delCodeField.onChar(ch);
    }
    lookupDeleteGame();   // se comprueba mientras se escribe

    m_delNoticePanel.update(engine);

    // --- los dos botones ---
    m_delYesHover = dentro(m_delYesRect);
    m_delNoHover  = dentro(m_delNoRect);

    if (in.mousePressed(0)) {
        if (m_delYesHover) m_delYesDown = true;
        if (m_delNoHover)  m_delNoDown  = true;
    }
    if (in.mouseReleased(0)) {
        if (m_delYesDown && m_delYesHover) {
            confirmDeleteGame();
        } else if (m_delNoDown && m_delNoHover) {
            // Negar limpia el campo: obliga a volver a escribir el codigo
            // para intentarlo otra vez, que es una friccion deseable aqui.
            m_delCodeField.setText("");
            m_delFound  = -1;
            m_delJuego.clear();
            m_delMsg    = "Operacion cancelada.";
            m_delMsgErr = false;
        }
        m_delYesDown = m_delNoDown = false;
    }
}

// ---------------------------------------------------------------------------
//  GUARDAR ARCHIVOS
//
//  Empaqueta la carpeta entera del proyecto -- codigo y recursos -- dentro de
//  Ludora, cifrada. Hasta aqui, publicar solo guardaba la RUTA del PC del
//  autor: el juego no existia fuera de su disco y nadie podia descargarlo.
//  Esto es lo que lo mete de verdad en la plataforma.
// ---------------------------------------------------------------------------

void HomeScene::layoutDevFiles(Recti area) {
    if (area.w <= 40 || area.h <= 40) {
        m_arcCodeRect = m_arcGenRect = Recti{};
        m_arcPickRect = m_arcSaveRect = Recti{};
        m_updEulaRect = m_updLogRect = m_updNoticeRect = Recti{};
        return;
    }

    const i32 m = 16;
    const i32 util = std::max(1, area.w - m * 2);

    // --- arriba: el CODIGO y, debajo, GENERAR CODIGO ---
    // El codigo es obligatorio: es lo que dice QUE juego se esta actualizando.
    i32 y = area.y + 14 + textr::lineHeight(2) + 18;
    m_arcCodeRect = Recti{ area.x + m, y, util, 34 };
    y += 34 + textr::glyphHeight(1) + 10;
    m_arcGenRect  = Recti{ area.x + m, y, util, 34 };

    // --- los tres botones de edicion ---
    // EDITAR EULA y SUBIR CHANGE LOG comparten fila: son las dos cosas que se
    // retocan al publicar una version. DAR COMUNICADO va solo debajo, a todo
    // lo ancho, porque no pertenece a publicar: es un mensaje a los jugadores.
    const i32 btnH = 30;
    const i32 hueco = 10;
    y += 34 + 14;
    const i32 mitad = std::max(1, (util - hueco) / 2);
    m_updEulaRect = Recti{ area.x + m, y, mitad, btnH };
    m_updLogRect  = Recti{ area.x + m + mitad + hueco, y, mitad, btnH };
    y += btnH + hueco;
    m_updNoticeRect = Recti{ area.x + m, y, util, btnH };

    // --- abajo: elegir carpeta y guardar ---
    // En ese orden porque es el orden en que se usan. Entre medias queda la
    // banda de estado (ruta / barra / aviso), que drawDevFiles coloca.
    const i32 btnH2 = 34;
    const i32 guardarY = area.y + area.h - 14 - btnH2;
    const i32 elegirY  = guardarY - 10 - btnH2;

    m_arcSaveRect = Recti{ area.x + m, guardarY, util, btnH2 };
    m_arcPickRect = Recti{ area.x + m, elegirY,  util, btnH2 };

    layoutUpdateBox(area);
}

void HomeScene::layoutUpdateBox(Recti area) {
    if (m_updBoxOpen < 0 || area.w <= 60 || area.h <= 60) {
        m_updBoxRect = m_updBoxOkRect = Recti{};
        m_updLogFieldRect = Recti{};
        m_updNoticeTitleRect = m_updNoticeBodyRect = m_updNoticeImgRect = Recti{};
        return;
    }

    // El cuadro se come casi toda la mini-seccion: los dos textos que se
    // escriben aqui son largos y necesitan sitio para leerse mientras se
    // redactan.
    const i32 m = 12;
    m_updBoxRect = Recti{ area.x + m, area.y + m,
                          std::max(1, area.w - m * 2),
                          std::max(1, area.h - m * 2) };

    const Recti& b = m_updBoxRect;
    const i32 pad = 12;
    const i32 util = std::max(1, b.w - pad * 2);
    const i32 btnH = 32;
    const i32 okY  = b.y + b.h - pad - btnH;

    m_updBoxOkRect = Recti{ b.x + pad, okY, util, btnH };

    // Bajo el titulo del cuadro empieza lo editable.
    const i32 y0 = b.y + pad + textr::lineHeight(2) + 10;

    if (m_updBoxOpen == kUpdBoxLog) {
        // Solo un campo, todo el hueco: el change log es un texto corrido.
        m_updLogFieldRect = Recti{ b.x + pad, y0, util,
                                   std::max(1, okY - 12 - y0) };
    } else {
        // Titulo (una linea), imagen (una franja) y cuerpo (el resto).
        const i32 tituloH = 32;
        const i32 imgH    = 34;
        m_updNoticeTitleRect = Recti{ b.x + pad, y0, util, tituloH };
        m_updNoticeImgRect   = Recti{ b.x + pad, y0 + tituloH + 8, util, imgH };
        const i32 cuerpoY = y0 + tituloH + 8 + imgH + 8;
        m_updNoticeBodyRect = Recti{ b.x + pad, cuerpoY, util,
                                     std::max(1, okY - 12 - cuerpoY) };
    }
}

bool HomeScene::canGenerateCode() const {
    // El boton se apaga en cuanto hay ALGUN juego publicado. A partir de
    // ahi el autor ya tiene su codigo en el catalogo, y generar otro solo
    // crearia dudas sobre cual usar al actualizar.
    return devcatalog::load(kDevCatalogFile).empty();
}

void HomeScene::generateGameCode() {
    if (!canGenerateCode()) {
        m_arcMsg    = "Ya tienes un juego publicado: usa su codigo.";
        m_arcMsgErr = true;
        return;
    }

    const std::string code = devcatalog::randomGameCode(kDevCatalogFile);
    if (code.empty()) {
        m_arcMsg    = "No se pudo generar el codigo. Intentalo de nuevo.";
        m_arcMsgErr = true;
        return;
    }

    // Se deja escrito en el campo: lo normal tras generarlo es usarlo, y
    // copiarlo a mano de un aviso seria un paso de mas.
    m_arcGenerado = code;
    m_arcCodeField.setText(code);
    m_arcCodeLast.clear();   // forzar que se vuelva a resolver
    lookupArchiveCode();

    m_arcMsg    = "Codigo generado: " + code;
    m_arcMsgErr = false;
}

void HomeScene::pickArchiveFolder(Engine& engine) {
    (void)engine;
    // Mismo dialogo que usa SUBIR JUEGO para elegir carpeta. Aqui SIEMPRE es
    // una carpeta: lo que se guarda es el proyecto entero, no un archivo.
    BROWSEINFOW bi{};
    bi.hwndOwner = engine.window().handle();
    bi.lpszTitle = L"Elige la carpeta RAIZ del juego: se guardara entera";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;

    wchar_t ruta[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, ruta) != FALSE;
    CoTaskMemFree(pidl);
    if (!ok || !ruta[0]) return;

    m_arcFolder = ruta;

    // Se mide ya: el autor ve QUE va a guardar antes de guardarlo, y una
    // carpeta vacia se detecta aqui y no tras cifrar.
    i32 n = 0;
    u64 b = 0;
    if (!gamevault::measureFolder(m_arcFolder, n, b)) {
        m_arcMsg    = "No se pudo leer esa carpeta.";
        m_arcMsgErr = true;
        m_arcFolder.clear();
        m_arcFiles = 0;
        m_arcBytes = 0;
        return;
    }
    m_arcFiles = n;
    m_arcBytes = b;

    if (n == 0) {
        m_arcMsg    = "Esa carpeta esta vacia: no hay nada que guardar.";
        m_arcMsgErr = true;
        return;
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d archivos listos (%.1f MB)",
                  n, static_cast<double>(b) / (1024.0 * 1024.0));
    m_arcMsg    = buf;
    m_arcMsgErr = false;
}

void HomeScene::startArchive() {
    // El juego hace falta -- hay que saber a cual pertenecen los archivos --,
    // pero da igual COMO se haya indicado: pegando el codigo o escribiendo el
    // nombre. Por eso el mensaje menciona las dos vias.
    // El CODIGO es obligatorio: es lo que dice QUE juego se actualiza.
    const std::string code = m_arcCodeField.text();
    if (code.empty()) {
        m_arcMsg    = "Escribe el codigo del juego.";
        m_arcMsgErr = true;
        return;
    }

    // El nombre sale del codigo. Si el codigo no corresponde a ningun juego
    // publicado, el paquete se guarda igual como PENDIENTE y la primera
    // publicacion lo adopta: es el caso normal de quien genero su codigo y
    // aun no ha subido nada.
    const std::string nombre = m_arcCodeJuego.empty() ? code : m_arcCodeJuego;
    if (m_arcFolder.empty() || m_arcFiles <= 0) {
        m_arcMsg    = "Elige primero la carpeta del juego.";
        m_arcMsgErr = true;
        return;
    }
    if (m_archive.running()) return;

    // El paquete vive junto al resto de datos de Ludora, con el nombre del
    // juego saneado: dos juegos distintos no pueden pisarse el archivo.
    std::wstring seguro;
    for (const wchar_t c : utf8ARuta(nombre)) {
        if (iswalnum(c) || c == L' ' || c == L'-' || c == L'_')
            seguro.push_back(c);
    }
    if (seguro.empty()) seguro = L"juego";

    const std::wstring destino =
        std::wstring(kGamesDir) + L"\\_publicados\\" + seguro + L".tomate";

    if (!m_archive.start(m_arcFolder, destino)) {
        m_arcMsg    = "Ya hay un guardado en curso.";
        m_arcMsgErr = true;
        return;
    }
    m_arcStarted = true;
    m_arcMsg     = "Guardando los archivos...";
    m_arcMsgErr  = false;
}

void HomeScene::pollArchive() {
    if (!m_arcStarted || !m_archive.done()) return;

    m_arcStarted = false;
    m_archive.join();

    if (m_archive.failed()) {
        m_arcMsg    = m_archive.error().empty()
                    ? "No se pudieron guardar los archivos."
                    : m_archive.error();
        m_arcMsgErr = true;
        return;
    }

    const std::string nombre = m_arcCodeJuego.empty() ? m_arcCodeField.text()
                                                      : m_arcCodeJuego;

    // --- se PUBLICA sola, sin un segundo boton ---
    //
    // En cuanto el paquete esta bien guardado la version sale: a la tienda y a
    // la biblioteca de quien ya tenga el juego. Antes el guardado dejaba el
    // paquete listo y hacia falta otra accion para publicarlo, con lo que un
    // autor podia creer que ya habia actualizado cuando no.
    //
    // Solo se publica si el codigo corresponde a un juego REAL del catalogo.
    // Con un codigo recien generado y nada publicado todavia no hay version
    // que mover: el paquete queda pendiente y lo adopta SUBIR JUEGO, y basta
    // con dejar anotado el paquete en la version vigente.
    bool publicada = false;
    if (!m_arcCodeJuego.empty()) {
        publishSavedVersion(nombre, publicada);
    }

    if (!publicada) {
        // Paquete pendiente: se anota en la version vigente (o en ninguna, si
        // el juego aun no existe) para que la primera publicacion lo adopte.
        std::string err;
        if (!devcatalog::setArchive(kDevCatalogFile, nombre,
                                    m_archive.vaultPath(),
                                    m_archive.fileCount(),
                                    m_archive.byteCount(), err)) {
            m_arcMsg    = err.empty() ? "No se pudo anotar en el catalogo." : err;
            m_arcMsgErr = true;
            return;
        }
    }

    m_devGamesLoaded = false;   // la columna y la tienda releen el catalogo
    m_storeLoaded    = false;   // la tienda ve la version nueva
    m_libraryLoaded  = false;   // y la biblioteca, sus archivos nuevos
    m_historyLoaded  = false;   // y su linea de tiempo

    char buf[220];
    if (publicada) {
        std::snprintf(buf, sizeof(buf),
                      "Publicada: %d archivos (%.1f MB) ya los tiene todo el mundo",
                      m_archive.fileCount(),
                      static_cast<double>(m_archive.byteCount()) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof(buf),
                      "Guardado: %d archivos (%.1f MB) ya estan en Ludora",
                      m_archive.fileCount(),
                      static_cast<double>(m_archive.byteCount()) / (1024.0 * 1024.0));
    }
    m_arcMsg     = buf;
    m_arcMsgErr  = false;
    m_arcDoneAnim = 1.6;   // el tic verde

    // --- confirmado el guardado, la seccion se cierra sola ---
    //
    // Lo que toca despues de guardar los archivos es SIEMPRE lo mismo: poner
    // precio y publicar. Dejar la seccion abierta obligaba a cerrarla a mano
    // y acordarse del siguiente paso; peor aun, el mensaje de exito se quedaba
    // en pantalla y era facil creer que ya estaba todo hecho.
    //
    // Se salta directamente a PRECIOS. El catalogo se relee ANTES de abrirlo
    // porque el deslizador arranca en el precio guardado del juego: con la
    // lista vieja, un juego recien publicado no estaria y el carril
    // empezaria en cero en vez de en su precio.
    m_devGames = devcatalog::load(kDevCatalogFile);
    m_devGamesLoaded = true;
    openDevPanel(kDevPrice);

    // El aviso viaja con el: en PRECIOS se lee lo que acaba de pasar y por
    // que se esta ahi, en vez de aparecer de golpe en otra seccion.
    m_devMsg    = publicada
                ? "Archivos publicados. Ahora pon el precio y sube el juego."
                : "Archivos guardados. Ahora pon el precio y sube el juego.";
    m_devMsgErr = false;

    // La carpeta elegida se suelta: ya esta empaquetada y dentro de Ludora.
    // Dejarla haria que volver a la seccion pareciera tener un guardado a
    // medias pendiente.
    m_arcFolder.clear();
    m_arcFiles = 0;
    m_arcBytes = 0;
}

void HomeScene::publishSavedVersion(const std::string& nombre, bool& publicada) {
    publicada = false;

    // 1) Nueva version en el catalogo, con el change log que se haya escrito.
    //    Es lo que ve la tienda: la vieja se archiva y esta pasa a vigente.
    std::string err;
    if (!devcatalog::addVersionWithLog(kDevCatalogFile, nombre, m_arcFolder,
                                       m_updLogField.text(), err)) {
        // No se pierde el guardado por esto: el paquete ya esta sellado en su
        // sitio. Se avisa y se deja que el autor reintente.
        m_devMsg    = err.empty() ? "No se pudo publicar la version." : err;
        m_devMsgErr = true;
        return;
    }

    // 2) El paquete se cuelga de la version RECIEN creada, no de la anterior.
    //    El orden importa: `setArchive` escribe sobre la version vigente, asi
    //    que llamarlo antes de anadir la nueva dejaba el paquete en la vieja y
    //    la nueva sin archivos -- es decir, publicada pero no descargable.
    if (!devcatalog::setArchive(kDevCatalogFile, nombre,
                                m_archive.vaultPath(), m_archive.fileCount(),
                                m_archive.byteCount(), err)) {
        m_devMsg    = err.empty() ? "No se pudo anotar el paquete." : err;
        m_devMsgErr = true;
        return;
    }

    // 3) Linea de tiempo: la version anterior se archiva y la nueva queda
    //    jugable, con su fecha. Alimenta la ficha del producto y la biblioteca.
    std::string herr;
    bool tenia = false;
    const history::GameHistory h = history::forGame(kHistoryFile, nombre, &tenia);
    if (tenia && !h.versions.empty())
        history::archiveAndAddVersion(kHistoryFile, nombre,
                                      m_archive.vaultPath(),
                                      m_archive.byteCount(), herr);
    else
        history::noteInitialVersion(kHistoryFile, nombre,
                                    m_archive.vaultPath(),
                                    m_archive.byteCount(), herr);

    // 4) La biblioteca de quien YA tiene el juego apunta al paquete nuevo.
    //
    //    Esto es lo que hace que "todos la tengan": la entrada se reescribe
    //    con el .tomate recien sellado, asi que al pulsar JUGAR se descifra la
    //    version nueva. Quien no tenga el juego no se ve afectado -- no hay
    //    entrada que actualizar --, y el bucle no anade a nadie: solo toca a
    //    los que ya lo tenian.
    for (const auto& e : library::load(kLibraryFile)) {
        if (e.name != nombre) continue;
        library::Entry act = e;
        act.vault = m_archive.vaultPath();
        act.exe.clear();   // el exe en claro de la version vieja ya no vale
        library::add(kLibraryFile, act);
    }

    // 5) El change log se consume: dejarlo escrito haria que la siguiente
    //    version heredara las notas de esta.
    m_updLogField.setText("");

    publicada = true;
}

void HomeScene::updateDevFiles(Engine& engine, f64 dt) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    pollArchive();
    if (m_arcDoneAnim > 0.0) m_arcDoneAnim = std::max(0.0, m_arcDoneAnim - dt);

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);
    const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                           VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };

    // --- cuadro abierto: se queda TODO el teclado y el raton ---
    // Mientras se redacta un change log o un comunicado, el campo del codigo
    // no debe robar las teclas: se escribe en el cuadro.
    if (m_updBoxOpen >= 0) {
        // ESC lo resuelve el manejador central de la escena, que cierra el
        // cuadro antes que la mini-seccion. No borra lo escrito: cerrar por
        // error no debe costar un comunicado entero.
        m_updBoxOkHover = dentro(m_updBoxOkRect);
        if (in.mousePressed(0) && m_updBoxOkHover) m_updBoxOkDown = true;

        if (m_updBoxOpen == kUpdBoxNotice) {
            m_updNoticeImgHover = dentro(m_updNoticeImgRect);
            if (in.mousePressed(0) && m_updNoticeImgHover)
                m_updNoticeImgDown = true;

            // Un clic elige a que campo van las teclas.
            if (in.mousePressed(0)) {
                if (dentro(m_updNoticeTitleRect)) m_updNoticeFocus = 0;
                else if (dentro(m_updNoticeBodyRect)) m_updNoticeFocus = 1;
            }
        }

        if (in.mouseReleased(0)) {
            if (m_updBoxOkDown && m_updBoxOkHover) {
                if (m_updBoxOpen == kUpdBoxNotice) submitNotice();
                else {
                    // El change log no se publica solo: queda guardado y lo
                    // consume la siguiente version al guardarse.
                    m_arcMsg    = "Change log listo: se publicara con la version.";
                    m_arcMsgErr = false;
                    m_updBoxOpen = -1;
                }
            } else if (m_updNoticeImgDown && m_updNoticeImgHover) {
                pickNoticeImage(engine);
            }
            m_updBoxOkDown = m_updNoticeImgDown = false;
        }

        // --- teclado al campo que toque ---
        TextField* campo = nullptr;
        if (m_updBoxOpen == kUpdBoxLog) campo = &m_updLogField;
        else campo = (m_updNoticeFocus == 0) ? &m_updNoticeTitle
                                             : &m_updNoticeBody;

        m_updLogField.setFocused(m_updBoxOpen == kUpdBoxLog);
        m_updNoticeTitle.setFocused(m_updBoxOpen == kUpdBoxNotice &&
                                    m_updNoticeFocus == 0);
        m_updNoticeBody.setFocused(m_updBoxOpen == kUpdBoxNotice &&
                                   m_updNoticeFocus == 1);
        campo->update(dt);

        if (in.mousePressed(0) && campo->contains(lx, ly))
            campo->onMouseDown(lx, ly);
        else if (in.mouseDown(0) && campo->focused())
            campo->onMouseDrag(lx, ly);

        // TAB cambia de campo en el comunicado: titulo y cuerpo se escriben
        // seguidos, y llegar al raton para eso sobra.
        if (m_updBoxOpen == kUpdBoxNotice && in.keyPressed(VK_TAB)) {
            m_updNoticeFocus = (m_updNoticeFocus == 0) ? 1 : 0;
            return;
        }

        for (const u32 vk : teclas)
            if (in.keyRepeat(vk)) campo->onKey(vk, shift, ctrlAtajo);

        for (const wchar_t ch : in.charsTyped()) {
            if (ctrlAtajo) continue;
            // El Enter entra como salto de linea en los textos largos: un
            // comunicado o un change log se escriben en parrafos.
            const bool largo = (campo == &m_updLogField ||
                                campo == &m_updNoticeBody);
            if (ch < 32 && !(largo && (ch == '\r' || ch == '\n'))) continue;
            campo->onChar((largo && ch == '\r') ? L'\n' : ch);
        }
        return;
    }

    // --- el campo del codigo ---
    // Con el cuadro cerrado es el unico campo de la seccion, asi que siempre
    // tiene el foco: no hay con quien repartirlo.
    m_arcCodeField.setFocused(true);
    m_arcCodeField.update(dt);

    if (in.mousePressed(0) && m_arcCodeField.contains(lx, ly))
        m_arcCodeField.onMouseDown(lx, ly);
    else if (in.mouseDown(0) && m_arcCodeField.focused())
        m_arcCodeField.onMouseDrag(lx, ly);

    for (const u32 vk : teclas)
        if (in.keyRepeat(vk)) m_arcCodeField.onKey(vk, shift, ctrlAtajo);

    for (const wchar_t ch : in.charsTyped()) {
        if (ctrlAtajo || ch < 32) continue;
        m_arcCodeField.onChar(ch);
    }

    // Se resuelve mientras se escribe: en cuanto el codigo cuadra con un
    // juego, la seccion ya sabe cual se esta actualizando.
    lookupArchiveCode();
    lookupUpdateGame();   // y se refleja en el indicador de los tres estados

    // --- los botones ---
    // Mientras se guarda no se toca nada: el hilo esta leyendo la carpeta.
    const bool ocupado = m_archive.running();
    const bool puedeGen = canGenerateCode();

    m_arcGenHover  = !ocupado && puedeGen && dentro(m_arcGenRect);
    m_arcPickHover = !ocupado && dentro(m_arcPickRect);
    m_arcSaveHover = !ocupado && dentro(m_arcSaveRect);

    m_updEulaHover   = !ocupado && dentro(m_updEulaRect);
    m_updLogHover    = !ocupado && dentro(m_updLogRect);
    m_updNoticeHover = !ocupado && dentro(m_updNoticeRect);

    if (in.mousePressed(0)) {
        if (m_arcGenHover)  m_arcGenDown  = true;
        if (m_arcPickHover) m_arcPickDown = true;
        if (m_arcSaveHover) m_arcSaveDown = true;
        if (m_updEulaHover)   m_updEulaDown   = true;
        if (m_updLogHover)    m_updLogDown    = true;
        if (m_updNoticeHover) m_updNoticeDown = true;
    }
    if (in.mouseReleased(0)) {
        if (m_arcGenDown && m_arcGenHover)        generateGameCode();
        else if (m_arcPickDown && m_arcPickHover) pickArchiveFolder(engine);
        else if (m_arcSaveDown && m_arcSaveHover) startArchive();
        else if (m_updEulaDown && m_updEulaHover) {
            // El EULA ya tiene su propia seccion a pantalla completa, con los
            // tres bloques de reglas. Se reutiliza en vez de hacer otra.
            m_section = kEulaSection;
            m_devMsg.clear();
        }
        else if (m_updLogDown && m_updLogHover) {
            m_updBoxOpen = kUpdBoxLog;
            m_arcMsg.clear();
        }
        else if (m_updNoticeDown && m_updNoticeHover) {
            m_updBoxOpen = kUpdBoxNotice;
            m_updNoticeFocus = 0;
            m_arcMsg.clear();
        }
        m_arcGenDown = m_arcPickDown = m_arcSaveDown = false;
        m_updEulaDown = m_updLogDown = m_updNoticeDown = false;
    }
}

void HomeScene::pickNoticeImage(Engine& engine) {
    const std::wstring elegida = photo::pickFromGallery(engine.window().handle());
    if (elegida.empty()) return;   // cancelar no es un error

    std::vector<u32> px;
    i32 w = 0, h = 0;
    if (!photo::loadAnyImage(elegida, px, w, h) || w <= 0 || h <= 0) {
        m_arcMsg    = "No se pudo leer esa imagen.";
        m_arcMsgErr = true;
        return;
    }

    m_updNoticeImg    = elegida;
    m_updNoticeImgPix = std::move(px);
    m_updNoticeImgW   = w;
    m_updNoticeImgH   = h;
    m_arcMsgErr = false;
}

void HomeScene::submitNotice() {
    // El juego sale del codigo, igual que todo en esta seccion. Sin un juego
    // real no hay a quien avisar: el comunicado lo leen los que lo tienen.
    if (m_arcCodeJuego.empty()) {
        m_arcMsg    = "Escribe el codigo de un juego tuyo ya publicado.";
        m_arcMsgErr = true;
        return;
    }
    if (m_updNoticeTitle.text().empty()) {
        m_arcMsg    = "El comunicado necesita un titulo.";
        m_arcMsgErr = true;
        return;
    }

    notices::Notice n;
    n.game  = m_arcCodeJuego;
    n.title = m_updNoticeTitle.text();
    n.body  = m_updNoticeBody.text();
    n.image = m_updNoticeImg;

    std::string err;
    if (!notices::publish(kNoticesFile, n, err)) {
        m_arcMsg    = err;
        m_arcMsgErr = true;
        return;
    }

    // Se limpia: el siguiente comunicado empieza en blanco, y dejar el texto
    // haria facil publicar dos veces lo mismo.
    m_updNoticeTitle.setText("");
    m_updNoticeBody.setText("");
    m_updNoticeImg.clear();
    m_updNoticeImgPix.clear();
    m_updNoticeImgW = m_updNoticeImgH = 0;
    m_updBoxOpen = -1;
    m_noticesLoaded = false;   // la biblioteca lo vera

    m_arcMsg    = "Comunicado publicado para " + n.game;
    m_arcMsgErr = false;
}

void HomeScene::lookupArchiveCode() {
    const std::string code = m_arcCodeField.text();
    if (code == m_arcCodeLast) return;   // ya resuelto, no repetir la busqueda
    m_arcCodeLast = code;

    if (code.empty()) { m_arcCodeJuego.clear(); return; }

    // findByCode acepta el codigo con o sin '#' y en cualquier caja: quien
    // lo pega de un sitio no deberia tener que acertar con el formato.
    m_arcCodeJuego = devcatalog::findByCode(kDevCatalogFile, code);
}

void HomeScene::drawDevFiles(Framebuffer& fb, Recti area) {
    layoutDevFiles(area);

    // --- titulo, centrado sobre su raya ---
    // La separacion sale del alto REAL de la fuente y no de numeros fijos:
    // con valores a ojo (y + 16, raya en y + 40) el rotulo quedaba descolgado
    // de su linea en cuanto la letra cambiaba de tamano.
    // Mismo nombre que su boton en la columna. Se reduce la escala si el
    // panel es estrecho, en vez de salirse: el rotulo es largo.
    const char* titulo = "GUARDAR ARCHIVOS Y ACTUALIZAR";
    i32 tEsc = 2;
    while (tEsc > 1 && font5x7::measureUtf8(titulo, tEsc) > area.w - 32) --tEsc;
    const i32 tw    = font5x7::measureUtf8(titulo, tEsc);
    const i32 tAlto = textr::lineHeight(tEsc);
    const i32 tY    = area.y + 14;
    font5x7::drawTextUtf8(fb, area.x + (area.w - tw) / 2, tY, titulo,
                          kSaveGreen, tEsc);
    fb.fillRect({ area.x + 16, tY + tAlto + 6,
                  std::max(1, area.w - 32), 1 }, kSaveGreen);

    // --- el CODIGO, obligatorio ---
    // Es lo unico que se escribe aqui: identifica QUE juego se actualiza.
    m_arcCodeField.setRect(m_arcCodeRect);
    m_arcCodeField.render(fb);

    // Bajo el campo, a que juego apunta. Se dice solo cuando el codigo
    // CUADRA: medio codigo tecleado no es un error, es uno incompleto, y
    // marcarlo en rojo mientras se escribe seria ruido.
    {
        const i32 iy = m_arcCodeRect.y + m_arcCodeRect.h + 4;
        if (!m_arcCodeJuego.empty()) {
            const std::string t = "JUEGO: " + m_arcCodeJuego;
            font5x7::drawFitted(fb, { m_arcCodeRect.x, iy,
                                      m_arcCodeRect.w,
                                      textr::glyphHeight(1) + 2 },
                                t.c_str(), kOk, 1, 0);
        } else {
            font5x7::drawTextUtf8(fb, m_arcCodeRect.x, iy,
                                  "Obligatorio: el codigo de tu juego",
                                  kDim, 1);
        }
    }

    // --- GENERAR CODIGO ---
    // Se apaga en cuanto hay un juego publicado. Apagado se ve gris y no
    // responde: el autor ya tiene su codigo y generar otro solo crearia
    // dudas sobre cual usar.
    {
        const Recti r = m_arcGenRect;
        if (r.w > 0) {
            const bool puede = canGenerateCode();
            const bool hov   = m_arcGenHover && puede;

            fb.fillRect(r, !puede ? kBg
                        : (m_arcGenDown ? kSteamEdge
                           : (hov ? kSaveGreen : kBg)));
            fb.drawRect(r, puede ? kSaveGreen : kPanelEdge, puede ? 2 : 1);
            font5x7::drawFitted(fb, r, "GENERAR CODIGO",
                                !puede ? kDim : (hov ? kBg : kText), 2, 6);

            // Apagado se explica el motivo: un boton gris sin razon parece
            // una funcion rota.
            if (!puede)
                font5x7::drawFitted(fb, { r.x, r.y + r.h + 4, r.w,
                                          textr::glyphHeight(1) + 2 },
                                    "Ya tienes un juego: usa su codigo",
                                    kDim, 1, 0);
        }
    }

    // --- EDITAR EULA / SUBIR CHANGE LOG / DAR COMUNICADO ---
    //
    // Los tres necesitan un juego identificado, menos EDITAR EULA: el EULA se
    // redacta antes de publicar nada, asi que exigir codigo lo bloquearia
    // justo cuando hace falta.
    {
        const bool hayJuego = !m_arcCodeJuego.empty();

        auto botonEd = [&](Recti r, const char* txt, Color c, bool hov,
                           bool down, bool activo) {
            if (r.w <= 0) return;
            fb.fillRect(r, down ? kSteamEdge : (hov && activo ? c : kFieldBg));
            fb.drawRect(r, activo ? c : kSteamEdge, 2);
            font5x7::drawFitted(fb, r, txt,
                                activo ? (hov ? kBg : kText) : kDim, 1, 5);
        };

        botonEd(m_updEulaRect, "EDITAR EULA", kDevGold,
                m_updEulaHover, m_updEulaDown, true);

        // El change log avisa de que lleva texto: si no, no habria forma de
        // saber que la proxima version ya tiene notas escritas.
        const bool hayLog = !m_updLogField.text().empty();
        botonEd(m_updLogRect, hayLog ? "CHANGE LOG LISTO" : "SUBIR CHANGE LOG",
                hayLog ? kOk : kDevGold, m_updLogHover, m_updLogDown, true);

        botonEd(m_updNoticeRect, "DAR COMUNICADO", kAccent,
                m_updNoticeHover, m_updNoticeDown, hayJuego);

        if (!hayJuego && m_updNoticeRect.w > 0)
            font5x7::drawFitted(fb, { m_updNoticeRect.x,
                                      m_updNoticeRect.y + m_updNoticeRect.h + 3,
                                      m_updNoticeRect.w,
                                      textr::glyphHeight(1) + 2 },
                                "Necesita un juego ya publicado", kDim, 1, 0);
    }

    // --- carpeta elegida y progreso ---
    // Las tres cosas (ruta, barra, tic) comparten la misma banda justo encima
    // de los botones, para que al cambiar de una a otra nada salte de sitio.
    // Tres renglones: ruta, barra o tic, y el aviso. Se cuentan de verdad en
    // vez de dejar un hueco fijo, que es lo que hacia que el aviso se pegara
    // al boton o se montara sobre la barra.
    const i32 lineaH = textr::lineHeight(1);
    const i32 infoY  = m_arcPickRect.y - 10 - (lineaH * 3 + 6);
    if (!m_arcFolder.empty()) {
        const std::string ruta = rutaAUtf8(m_arcFolder);
        font5x7::drawFitted(fb, { area.x + 16, infoY,
                                  std::max(1, area.w - 32), lineaH },
                            ruta.c_str(), kText, 1, 0);
    }

    const i32 bandaY = infoY + lineaH + 4;
    if (m_archive.running()) {
        // Barra de progreso: sellar un proyecto grande tarda y hay que ver
        // que avanza. Centrada en la banda, con el mismo margen que todo.
        const Recti barra{ area.x + 16, bandaY + (lineaH - 10) / 2,
                           std::max(1, area.w - 32), 10 };
        fb.fillRect(barra, kFieldBg);
        const i32 w = static_cast<i32>(barra.w * m_archive.progress());
        if (w > 0) fb.fillRect({ barra.x, barra.y, w, barra.h }, kSaveGreen);
        fb.drawRect(barra, kSteamEdge, 1);
    } else if (m_arcDoneAnim > 0.0) {
        // Tic verde al terminar, que se desvanece. Va centrado en el panel,
        // no clavado al margen izquierdo.
        const i32 cx = area.x + area.w / 2 - 4;
        const i32 cy = bandaY + lineaH / 2 - 2;
        const u8 a = static_cast<u8>(std::clamp(m_arcDoneAnim / 1.6, 0.0, 1.0) * 255.0);
        const Color c{ kSaveGreen.r, kSaveGreen.g, kSaveGreen.b, a };
        for (i32 i = 0; i < 5; ++i)  fb.blendPixel(cx - 4 + i, cy + i, c);
        for (i32 i = 0; i < 9; ++i)  fb.blendPixel(cx + i, cy + 4 - i, c);
        for (i32 i = 0; i < 5; ++i)  fb.blendPixel(cx - 4 + i, cy + 1 + i, c);
        for (i32 i = 0; i < 9; ++i)  fb.blendPixel(cx + i, cy + 5 - i, c);
    }

    // --- los dos botones ---
    const bool ocupado  = m_archive.running();
    const bool puedeEl  = !ocupado;
    // Guardar necesita CODIGO y carpeta. El codigo no tiene que existir ya
    // en el catalogo: si no, el paquete queda pendiente y lo adopta la
    // primera publicacion.
    const bool puedeGu  = !ocupado && !m_arcCodeField.text().empty() &&
                          m_arcFiles > 0;

    auto boton = [&](Recti r, const char* txt, Color c, bool hov, bool down,
                     bool activo) {
        if (r.w <= 0) return;
        fb.fillRect(r, down ? kSteamEdge : (hov && activo ? c : kFieldBg));
        fb.drawRect(r, activo ? c : kSteamEdge, 2);
        font5x7::drawFitted(fb, r, txt, activo ? (hov ? kBg : kText) : kDim, 2, 6);
    };

    boton(m_arcPickRect, m_arcFolder.empty() ? "ELEGIR CARPETA DEL JUEGO"
                                             : "CAMBIAR CARPETA",
          kSaveGreen, m_arcPickHover, m_arcPickDown, puedeEl);
    boton(m_arcSaveRect, ocupado ? "GUARDANDO..." : "GUARDAR TODO",
          kSaveGreen, m_arcSaveHover, m_arcSaveDown, puedeGu);

    // Aviso al pie, justo encima de los botones. Comparte la banda de estado
    // y se centra en ella: drawFitted ya centra en la caja que se le da.
    if (!m_arcMsg.empty()) {
        font5x7::drawFitted(fb, { area.x + 16, bandaY + lineaH + 2,
                                  std::max(1, area.w - 32), lineaH },
                            m_arcMsg.c_str(),
                            m_arcMsgErr ? kConsentRed : kOk, 1, 0);
    }

    // El cuadro va el ULTIMO: se superpone a todo lo demas.
    drawUpdateBox(fb, area);
}

void HomeScene::drawUpdateBox(Framebuffer& fb, Recti area) {
    layoutUpdateBox(area);
    if (m_updBoxOpen < 0 || m_updBoxRect.w <= 0) return;

    const Recti& b = m_updBoxRect;
    const bool esAviso = (m_updBoxOpen == kUpdBoxNotice);
    const Color acc = esAviso ? kAccent : kDevGold;

    fb.fillRect(b, kPanelDeep);
    fb.drawRect(b, acc, 2);

    const i32 pad = 12;
    font5x7::drawTextUtf8(fb, b.x + pad, b.y + pad,
                          esAviso ? "DAR COMUNICADO" : "CHANGE LOG", acc, 2);

    if (!esAviso) {
        // --- change log: un solo campo, todo el hueco ---
        m_updLogField.setRect(m_updLogFieldRect);
        m_updLogField.render(fb);

        font5x7::drawFitted(fb, { b.x + pad,
                                  m_updBoxOkRect.y - textr::glyphHeight(1) - 6,
                                  std::max(1, b.w - pad * 2),
                                  textr::glyphHeight(1) + 2 },
                            "Lo leeran los jugadores en la tienda", kDim, 1, 0);
    } else {
        // --- comunicado: titulo, imagen y cuerpo ---
        m_updNoticeTitle.setRect(m_updNoticeTitleRect);
        m_updNoticeTitle.render(fb);

        // Franja de la imagen: dice si lleva una y deja cambiarla.
        const Recti ir = m_updNoticeImgRect;
        if (ir.w > 0) {
            const bool hov = m_updNoticeImgHover;
            fb.fillRect(ir, m_updNoticeImgDown ? kSteamEdge
                                               : (hov ? kFieldBgFocus : kFieldBg));
            fb.drawRect(ir, hov ? acc : kSteamEdge, 1);

            if (m_updNoticeImgW > 0) {
                // Miniatura a la izquierda y el tamano al lado: se ve QUE se
                // adjunto, no solo que hay algo adjunto.
                const i32 miniH = ir.h - 6;
                const i32 miniW = std::min(ir.w / 3,
                                           miniH * m_updNoticeImgW /
                                           std::max(1, m_updNoticeImgH));
                for (i32 yy = 0; yy < miniH; ++yy) {
                    for (i32 xx = 0; xx < miniW; ++xx) {
                        const i32 sx = std::min(m_updNoticeImgW - 1,
                                                xx * m_updNoticeImgW /
                                                std::max(1, miniW));
                        const i32 sy = std::min(m_updNoticeImgH - 1,
                                                yy * m_updNoticeImgH /
                                                std::max(1, miniH));
                        fb.setPixel(ir.x + 3 + xx, ir.y + 3 + yy,
                                    m_updNoticeImgPix[
                                        static_cast<size_t>(sy) * m_updNoticeImgW + sx]);
                    }
                }
                char t[80];
                std::snprintf(t, sizeof(t), "%dx%d  (pulsa para cambiarla)",
                              m_updNoticeImgW, m_updNoticeImgH);
                font5x7::drawFitted(fb, { ir.x + 6 + miniW, ir.y,
                                          std::max(1, ir.w - miniW - 12), ir.h },
                                    t, hov ? kWhite : kText, 1, 0);
            } else {
                font5x7::drawFitted(fb, ir, "ADJUNTAR IMAGEN (opcional)",
                                    hov ? kWhite : kDim, 1, 0);
            }
        }

        m_updNoticeBody.setRect(m_updNoticeBodyRect);
        m_updNoticeBody.render(fb);
    }

    // --- confirmar ---
    const Recti& ok = m_updBoxOkRect;
    if (ok.w > 0) {
        fb.fillRect(ok, m_updBoxOkDown ? kSteamEdge
                                       : (m_updBoxOkHover ? acc : kFieldBg));
        fb.drawRect(ok, acc, 2);
        font5x7::drawFitted(fb, ok,
                            esAviso ? "PUBLICAR COMUNICADO" : "GUARDAR CHANGE LOG",
                            m_updBoxOkHover ? kBg : kText, 2, 6);
    }
}

// Aviso de ELIMINAR JUEGO. Se lee entero antes de poder confirmar, porque lo
// que se borra y lo que NO se borra no es evidente: el juego sale de la
// tienda, pero quien ya lo compro se lo queda.
static const char* const kDeleteNotice[] = {
    "\xC2\xA1" "Aviso! Esto no se puede deshacer.",
    "",
    "Al eliminar el juego:",
    "",
    "  - Sale de la Tienda y nadie mas podra conseguirlo.",
    "  - Su ficha, su genero y sus capturas dejan de publicarse.",
    "  - Las reseñas y la valoracion se archivan contigo, no se borran.",
    "  - El codigo del juego queda consumido: no se reutiliza.",
    "",
    "Lo que NO se elimina:",
    "",
    "  - Sigue en la biblioteca de todo el que ya lo tenga.",
    "  - Sus archivos se LIBERAN en esas bibliotecas: el juego ya es de",
    "    quien lo compro, y seguira pudiendo jugarlo e instalarlo aunque",
    "    tu cuenta o la plataforma dejen de ofrecerlo.",
    "  - Las versiones antiguas que alguien tenga descargadas se quedan.",
    "",
    "Si lo que quieres es dejar de venderlo sin retirarlo, cambia su precio",
    "o despublicalo desde ACTUALIZAR JUEGO en vez de eliminarlo.",
};
static constexpr i32 kDeleteNoticeLines =
    static_cast<i32>(sizeof(kDeleteNotice) / sizeof(kDeleteNotice[0]));

i32 HomeScene::deleteNoticeLines() { return kDeleteNoticeLines; }

const char* HomeScene::priceOptionValue(i32 i) const {
    // Valor elegido de la opcion `i`, para pintarlo en su boton. Si todavia no
    // se ha tocado, se devuelve el primero de su lista: es el que la seccion
    // aplicaria tal cual esta.
    if (i < 0 || i >= kPriceOptCount) return "";
    const PriceOptionDef& d = kPriceOptions[i];
    const i32 sel = (m_priceOptSel[i] >= 0 && m_priceOptSel[i] < d.count)
                        ? m_priceOptSel[i] : 0;
    return d.values[sel];
}

// ===========================================================================
//  CREAR COMUNIDAD
//
//  Nueve ajustes en rejilla. Cada uno abre su panel, igual que SUBIR JUEGO:
//  es el patron que ya existe para formularios largos y no conviene inventar
//  otro al lado.
// ===========================================================================

namespace {

/// Rotulo y color de cada ajuste. El color lo comparten el boton, el marco de
/// su panel y el valor que muestra, para que se lea como una sola pieza.
struct ComSlotDef { const char* title; Color accent; };

// El orden sigue al del enum ComSlot, que es el de la pantalla.
const ComSlotDef kComSlotDefs[9] = {
    { "NOMBRE",      kUpIndigo   },
    { "DESCRIPCION", kUpPurple   },
    { "IDENTIDAD",   kDevPurple  },
    { "ENTRADA",     kUpMint     },
    { "ADMINS",      kDevGold    },
    { "RANGOS",      kNeonGreen  },
    { "REGLAS",      kUpPink     },
    { "CASTIGOS",    kConsentRed },
    { "EVENTOS",     kSteamBlue  },
};

/// Como se entra a la comunidad. El orden va de mas abierto a mas cerrado.
struct ComJoinDef { const char* name; const char* help; };
const ComJoinDef kComJoinModes[] = {
    { "ABIERTA",     "Cualquiera entra al instante." },
    { "POR SOLICITUD", "Un admin aprueba cada entrada." },
    { "POR INVITACION", "Solo con enlace de un miembro." },
    { "CON EL JUEGO", "Solo quien tenga el juego en su biblioteca." },
};
constexpr i32 kComJoinCount =
    static_cast<i32>(sizeof(kComJoinModes) / sizeof(kComJoinModes[0]));

/// Castigos, de menos a mas grave. Los tres tramos eligen de aqui.
const char* const kComPunishSteps[] = {
    "AVISO", "SILENCIO 1 DIA", "SILENCIO 7 DIAS",
    "EXPULSION TEMPORAL", "EXPULSION DEFINITIVA",
};
constexpr i32 kComPunishCount =
    static_cast<i32>(sizeof(kComPunishSteps) / sizeof(kComPunishSteps[0]));

/// Eventos recurrentes que una comunidad puede programar.
struct ComEventDef { const char* name; const char* when; };
const ComEventDef kComEventKinds[] = {
    { "TORNEO",            "cada mes"    },
    { "PARTIDA COMUNITARIA", "cada semana" },
    { "CONCURSO DE MODS",  "cada mes"    },
    { "RETO SEMANAL",      "cada semana" },
    { "CHARLA CON EL AUTOR", "cada mes"  },
    { "NOCHE DE RECORDS",  "cada semana" },
};
constexpr i32 kComEventCount =
    static_cast<i32>(sizeof(kComEventKinds) / sizeof(kComEventKinds[0]));

/// Colores e insignias de la identidad de la comunidad.
struct ComColorDef { const char* name; Color c; };
const ComColorDef kComColors[] = {
    { "MORADO",   kDevPurple  },
    { "VERDE",    kNeonGreen  },
    { "AZUL",     kSteamBlue  },
    { "DORADO",   kDevGold    },
    { "CARMESI",  kConsentRed },
    { "MENTA",    kUpMint     },
};
constexpr i32 kComColorCount =
    static_cast<i32>(sizeof(kComColors) / sizeof(kComColors[0]));

const char* const kComBadges[] = {
    "ESCUDO", "CORONA", "RAYO", "ESTRELLA", "CALAVERA", "ENGRANAJE",
};
constexpr i32 kComBadgeCount =
    static_cast<i32>(sizeof(kComBadges) / sizeof(kComBadges[0]));

/// Limites de los admins. Se pidieron explicitamente: minimo 1, maximo 100.
constexpr i32 kComAdminsMin = 1;
constexpr i32 kComAdminsMax = 100;

/// Tope de la descripcion. Se pidieron 5000 caracteres.
constexpr size_t kComDescMax = 5000;
/// Las reglas son el otro texto largo de la seccion.
constexpr size_t kComRulesMax = 5000;

} // namespace

void HomeScene::setCommunityAdmins(i32 n) {
    // Se recorta a [1, 100]: es el rango que se pidio, y dejar pasar un 0 o
    // un 200 por venir de fuera haria que el carril y la cifra discreparan.
    m_comAdmins = std::clamp(n, kComAdminsMin, kComAdminsMax);
}

void HomeScene::toggleCommunityEventForTest(i32 i) {
    if (i < 0 || i >= kComEventCount) return;
    const auto it = std::find(m_comEvents.begin(), m_comEvents.end(), i);
    if (it == m_comEvents.end()) m_comEvents.push_back(i);
    else                         m_comEvents.erase(it);
}

void HomeScene::updateDevCommunity(Engine& engine, f64 dt) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w &&
                          ly >= r.y && ly < r.y + r.h;
    };

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrlAtajo = in.keyDown(VK_CONTROL) && !in.keyDown(VK_MENU);
    const u32 teclas[] = { VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
                           VK_HOME, VK_END, VK_UP, VK_DOWN,
                           'A', 'C', 'X', 'V' };

    // --- panel abierto: se queda el teclado y el raton ---
    if (m_comOpen >= 0) {
        // ESC NO se atiende aqui: lo resuelve el manejador central de la
        // escena, que retrocede un nivel cada vez (lista -> ajuste ->
        // mini-seccion -> Cuenta). Ese manejador corre antes que esto y
        // devuelve, asi que una copia local nunca llegaria a ejecutarse.

        // Cual de las opciones apuntadas al dibujar esta bajo el cursor.
        m_comOptHover = -1;
        for (i32 i = 0; i < m_comOptCount; ++i)
            if (dentro(m_comOptRects[i])) m_comOptHover = i;

        // --- textos ---
        TextField* campo = nullptr;
        if      (m_comOpen == kComName)  campo = &m_comNameField;
        else if (m_comOpen == kComDesc)  campo = &m_comDescField;
        else if (m_comOpen == kComRules) campo = &m_comRulesField;

        m_comNameField.setFocused(m_comOpen == kComName);
        m_comDescField.setFocused(m_comOpen == kComDesc);
        m_comRulesField.setFocused(m_comOpen == kComRules);

        if (campo) {
            campo->update(dt);
            if (in.mousePressed(0) && campo->contains(lx, ly))
                campo->onMouseDown(lx, ly);
            else if (in.mouseDown(0) && campo->focused())
                campo->onMouseDrag(lx, ly);

            for (const u32 vk : teclas)
                if (in.keyRepeat(vk)) campo->onKey(vk, shift, ctrlAtajo);

            // En descripcion y reglas el Enter hace parrafo; el nombre es de
            // una linea y ahi un salto no significa nada.
            const bool largo = (campo != &m_comNameField);
            for (const wchar_t ch : in.charsTyped()) {
                if (ctrlAtajo) continue;
                if (ch < 32 && !(largo && (ch == '\r' || ch == '\n'))) continue;
                campo->onChar((largo && ch == '\r') ? L'\n' : ch);
            }
            return;
        }

        // --- ADMINS: carril y los dos botones de +-1 ---
        if (m_comOpen == kComAdmins) {
            if (in.mousePressed(0)) {
                if (dentro(m_comAdminsLess))
                    m_comAdmins = std::max(kComAdminsMin, m_comAdmins - 1);
                else if (dentro(m_comAdminsMore))
                    m_comAdmins = std::min(kComAdminsMax, m_comAdmins + 1);
                else if (dentro(m_comAdminsTrack))
                    m_comAdminsDrag = true;
            }
            if (!in.mouseDown(0)) m_comAdminsDrag = false;

            if (m_comAdminsDrag && m_comAdminsTrack.w > 0) {
                const i32 rel = std::clamp(lx - m_comAdminsTrack.x, 0,
                                           m_comAdminsTrack.w);
                const f32 t = static_cast<f32>(rel) /
                              static_cast<f32>(m_comAdminsTrack.w);
                // +0.5 para que el extremo derecho de verdad de 100 y no 99.
                m_comAdmins = kComAdminsMin +
                    static_cast<i32>(t * (kComAdminsMax - kComAdminsMin) + 0.5f);
                m_comAdmins = std::clamp(m_comAdmins, kComAdminsMin, kComAdminsMax);
            }
            return;
        }

        // --- listas ---
        if (in.mouseReleased(0) && m_comOptHover >= 0) {
            const i32 k = m_comOptHover;
            switch (m_comOpen) {
                case kComJoin:
                    if (k < kComJoinCount) m_comJoinMode = k;
                    break;

                case kComRanks:
                    // 0/1 escalones, 2/3 horas. Los topes evitan una escalera
                    // de un solo rango o de horas imposibles.
                    if      (k == 0) m_comRankCount = std::max(1, m_comRankCount - 1);
                    else if (k == 1) m_comRankCount = std::min(20, m_comRankCount + 1);
                    else if (k == 2) m_comRankHours = std::max(1, m_comRankHours - 5);
                    else if (k == 3) m_comRankHours = std::min(500, m_comRankHours + 5);
                    break;

                case kComPunish:
                    if (k < 3) {
                        // Pulsar el tramo lo despliega; volver a pulsarlo lo cierra.
                        m_comPunishOpen = (m_comPunishOpen == k) ? -1 : k;
                    } else if (m_comPunishOpen >= 0) {
                        m_comPunish[m_comPunishOpen] =
                            std::clamp(k - 3, 0, kComPunishCount - 1);
                        m_comPunishOpen = -1;
                    }
                    break;

                case kComEvents: {
                    // Marcar y desmarcar: una comunidad puede tener varios
                    // eventos a la vez, asi que no es una eleccion unica.
                    if (k >= kComEventCount) break;
                    const auto it = std::find(m_comEvents.begin(),
                                              m_comEvents.end(), k);
                    if (it == m_comEvents.end()) m_comEvents.push_back(k);
                    else                         m_comEvents.erase(it);
                    break;
                }

                case kComBrand:
                    if (k < kComColorCount) m_comColor = k;
                    else m_comBadge = std::clamp(k - kComColorCount, 0,
                                                 kComBadgeCount - 1);
                    break;

                default: break;
            }
        }
        return;
    }

    // --- rejilla: abrir un ajuste ---
    m_comHover = -1;
    for (i32 i = 0; i < kComSlots; ++i)
        if (dentro(m_comRects[i])) m_comHover = i;

    if (in.mousePressed(0) && m_comHover >= 0) m_comDown = m_comHover;
    if (in.mouseReleased(0)) {
        if (m_comDown >= 0 && m_comDown == m_comHover) {
            m_comOpen = m_comDown;
            m_comPunishOpen = -1;
            m_comOptHover = -1;
        }
        m_comDown = -1;
    }
}

void HomeScene::layoutDevCommunity(Recti area) {
    if (area.w <= 60 || area.h <= 60) {
        for (i32 i = 0; i < kComSlots; ++i) m_comRects[i] = Recti{};
        m_comPanelRect = Recti{};
        for (i32 g = 0; g < kComGroups; ++g) m_comGroupY[g] = 0;
        return;
    }

    // --- la misma forma que GUARDAR ARCHIVOS Y ACTUALIZAR ---
    //
    // Arriba el dato que manda (el nombre) en un campo ancho; debajo su
    // aclaracion; luego la accion principal a todo lo ancho; despues los
    // ajustes por parejas, cada fila con su rotulo de grupo; y al fondo,
    // anclada abajo, la accion que cierra la seccion.
    //
    // Es la forma que ya tienen las otras mini-secciones, y leerla de arriba
    // abajo cuenta el orden en que se rellena: primero como se llama, luego
    // de que va, y al final las normas y el dia a dia.
    const i32 m = 16;
    const i32 hueco = 8;
    const i32 dispW = std::max(1, area.w - m * 2);
    const i32 x = area.x + m;

    const i32 lineaH = textr::glyphHeight(1);
    const i32 rotuloH = lineaH + 6;

    // --- los altos salen del sitio que HAY, no de numeros fijos ---
    //
    // Debajo del titulo caben: dos campos anchos, tres rotulos de grupo,
    // cinco filas de botones y el pie. Con alturas fijas, en un panel algo
    // mas bajo el ultimo boton se metia debajo del pie; repartiendo, la
    // seccion se encoge entera y sigue cuadrando.
    const i32 yIni = area.y + 14 + textr::lineHeight(2) + 16;
    const i32 pieH = 30;
    const i32 dispV = std::max(1, area.y + area.h - m - pieH - 8 - yIni);

    // Lo que gastan los huecos y las aclaraciones, que no se encogen.
    const i32 fijo = lineaH + 8      // aclaracion bajo el nombre
                   + 14              // aire tras la descripcion
                   + rotuloH * kComGroups
                   + hueco * 6 + 12; // huecos entre filas y entre grupos
    const i32 paraFilas = std::max(5 * 20, dispV - fijo);

    // Siete filas de alto: dos anchas (nombre y descripcion) y cinco barras.
    // Las anchas piden algo mas de sitio, de ahi el reparto 2:1.
    const i32 altoAncho = std::clamp(paraFilas * 2 / 9, 26, 34);
    const i32 btnH      = std::clamp((paraFilas - altoAncho * 2) / 5, 20, 30);

    // --- arriba: NOMBRE, con su aclaracion debajo ---
    i32 y = yIni;
    m_comRects[kComName] = Recti{ x, y, dispW, altoAncho };
    y += altoAncho + lineaH + 8;

    // --- la accion principal: DESCRIPCION, a todo lo ancho ---
    // Es el campo largo de la seccion (5000 caracteres) y el que mas trabajo
    // lleva, asi que ocupa el sitio destacado.
    m_comRects[kComDesc] = Recti{ x, y, dispW, altoAncho };
    y += altoAncho + 14;

    // Grupo 1: lo que queda de "QUE ES".
    m_comGroupY[0] = y;
    y += rotuloH;
    m_comRects[kComBrand] = Recti{ x, y, dispW, btnH };
    y += btnH + hueco + 6;

    // Grupo 2: quien entra y como sube. Tres botones en dos filas: dos
    // arriba y el tercero a todo lo ancho, para no dejar un hueco muerto.
    const i32 mitad = std::max(1, (dispW - hueco) / 2);
    m_comGroupY[1] = y;
    y += rotuloH;
    m_comRects[kComJoin]   = Recti{ x, y, mitad, btnH };
    m_comRects[kComAdmins] = Recti{ x + mitad + hueco, y, mitad, btnH };
    y += btnH + hueco;
    m_comRects[kComRanks]  = Recti{ x, y, dispW, btnH };
    y += btnH + hueco + 6;

    // Grupo 3: sus normas y su dia.
    m_comGroupY[2] = y;
    y += rotuloH;
    m_comRects[kComRules]  = Recti{ x, y, mitad, btnH };
    m_comRects[kComPunish] = Recti{ x + mitad + hueco, y, mitad, btnH };
    y += btnH + hueco;
    m_comRects[kComEvents] = Recti{ x, y, dispW, btnH };

    // --- al fondo: el resumen de lo que falta ---
    // Anclado abajo como GUARDAR TODO en la otra seccion, para que no baile
    // de sitio segun lo que se haya rellenado.
    m_comSummaryRect = Recti{ x, area.y + area.h - m - pieH, dispW, pieH };

    // El panel del ajuste abierto se superpone a casi toda la seccion: los
    // textos largos y las listas necesitan sitio para leerse.
    if (m_comOpen >= 0 && m_comOpen < kComSlots) {
        const i32 py = area.y + 14 + textr::lineHeight(2) + 16;
        m_comPanelRect = Recti{ x, py, dispW,
                                std::max(1, area.y + area.h - m - py) };
    } else {
        m_comPanelRect = Recti{};
    }
}

std::string HomeScene::communitySlotValue(i32 slot) const {
    // Resumen que cada boton muestra debajo de su rotulo. Vacio = sin tocar,
    // y entonces el boton no dice nada: un "0" o un "-" pareceria un valor
    // puesto a proposito.
    switch (slot) {
        case kComName:
            return m_comNameField.text();
        case kComDesc: {
            if (m_comDescField.text().empty()) return std::string();
            char b[48];
            std::snprintf(b, sizeof(b), "%zu / %zu",
                          m_comDescField.length(), kComDescMax);
            return b;
        }
        case kComRules: {
            if (m_comRulesField.text().empty()) return std::string();
            char b[48];
            std::snprintf(b, sizeof(b), "%zu / %zu",
                          m_comRulesField.length(), kComRulesMax);
            return b;
        }
        case kComAdmins: {
            char b[32];
            std::snprintf(b, sizeof(b), "%d", m_comAdmins);
            return b;
        }
        case kComJoin:
            return kComJoinModes[std::clamp(m_comJoinMode, 0,
                                            kComJoinCount - 1)].name;
        case kComRanks: {
            char b[48];
            std::snprintf(b, sizeof(b), "%d x %dh", m_comRankCount,
                          m_comRankHours);
            return b;
        }
        case kComPunish:
            return kComPunishSteps[std::clamp(m_comPunish[0], 0,
                                              kComPunishCount - 1)];
        case kComEvents: {
            if (m_comEvents.empty()) return std::string();
            char b[32];
            std::snprintf(b, sizeof(b), "%zu ACTIVOS", m_comEvents.size());
            return b;
        }
        case kComBrand:
            return kComColors[std::clamp(m_comColor, 0,
                                         kComColorCount - 1)].name;
        default:
            return std::string();
    }
}

void HomeScene::drawDevCommunity(Framebuffer& fb, Recti area) {
    layoutDevCommunity(area);

    // --- titulo ---
    const char* titulo = "CREAR COMUNIDAD";
    i32 tEsc = 2;
    while (tEsc > 1 && font5x7::measureUtf8(titulo, tEsc) > area.w - 32) --tEsc;
    const i32 tw    = font5x7::measureUtf8(titulo, tEsc);
    const i32 tAlto = textr::lineHeight(tEsc);
    const i32 tY    = area.y + 14;
    font5x7::drawTextUtf8(fb, area.x + (area.w - tw) / 2, tY, titulo,
                          kDevPurple, tEsc);
    fb.fillRect({ area.x + 16, tY + tAlto + 6,
                  std::max(1, area.w - 32), 1 }, kDevPurple);

    const i32 lineaH = textr::glyphHeight(1);

    /// Pinta un ajuste. Los dos de arriba son anchos y van a escala 2, con su
    /// valor dentro; los de abajo son barras de una linea.
    auto boton = [&](i32 i, bool grande) {
        const Recti r = m_comRects[i];
        if (r.w <= 0 || r.h <= 0) return;

        const bool hov    = (m_comHover == i && m_comOpen < 0);
        const bool down   = (m_comDown == i && hov);
        const bool activo = (m_comOpen == i);
        const Color acc   = kComSlotDefs[i].accent;
        const std::string val = communitySlotValue(i);
        const bool puesto = !val.empty();

        fb.fillRect(r, down ? kClosePress : (hov ? kPanelViolet : kFieldBg));
        // El marco se enciende con su color cuando el ajuste YA tiene valor:
        // de un vistazo se ve que falta por rellenar y que no, como el verde
        // y el gris de la otra mini-seccion.
        fb.drawRect(r, (hov || activo) ? acc : (puesto ? acc : kPanelEdge),
                    (hov || activo || puesto) ? 2 : 1);

        if (grande) {
            // Rotulo a la izquierda y valor a la derecha, en la misma linea.
            const char* t = kComSlotDefs[i].title;
            const i32 tw2 = font5x7::measureUtf8(t, 2);
            font5x7::drawTextUtf8(fb, r.x + 10,
                                  r.y + (r.h - textr::glyphHeight(2)) / 2, t,
                                  hov ? kWhite : acc, 2);
            if (puesto) {
                const i32 libre = r.w - tw2 - 28;
                if (libre > 40)
                    font5x7::drawFitted(fb, { r.x + tw2 + 18,
                                              r.y + (r.h - lineaH) / 2,
                                              libre, lineaH },
                                        val.c_str(), kText, 1, 0);
            }
        } else {
            // Barra de una linea: rotulo a la izquierda, valor a la derecha.
            const char* t = kComSlotDefs[i].title;
            const i32 tw2 = font5x7::measureUtf8(t, 1);
            font5x7::drawTextUtf8(fb, r.x + 10, r.y + (r.h - lineaH) / 2, t,
                                  hov ? kWhite : acc, 1);

            const std::string der = puesto ? val : std::string("sin poner");
            const i32 libre = r.w - tw2 - 28;
            if (libre > 30)
                font5x7::drawFitted(fb, { r.x + tw2 + 18,
                                          r.y + (r.h - lineaH) / 2,
                                          libre, lineaH },
                                    der.c_str(),
                                    puesto ? (hov ? kWhite : kText) : kDim,
                                    1, 0);
        }
    };

    // --- arriba: NOMBRE, con su aclaracion debajo ---
    boton(kComName, true);
    {
        const Recti& r = m_comRects[kComName];
        const bool hay = !m_comNameField.text().empty();
        font5x7::drawTextUtf8(fb, r.x + 2, r.y + r.h + 4,
                              hay ? "Asi se llamara tu comunidad"
                                  : "Obligatorio: el nombre de tu comunidad",
                              hay ? kDim : kConsentRed, 1);
    }

    // --- la descripcion, a todo lo ancho ---
    boton(kComDesc, true);

    // --- rotulo de cada grupo, con su raya hasta el borde ---
    const char* grupos[kComGroups] = {
        "QUE ES", "QUIEN ENTRA Y COMO SUBE", "SUS NORMAS Y SU DIA"
    };
    for (i32 g = 0; g < kComGroups; ++g) {
        const i32 gy = m_comGroupY[g];
        if (gy <= 0) continue;
        font5x7::drawTextUtf8(fb, area.x + 16, gy, grupos[g], kDevPurple, 1);
        const i32 gw = font5x7::measureUtf8(grupos[g], 1);
        const i32 rayaX = area.x + 16 + gw + 8;
        const i32 rayaW = std::max(0, area.x + area.w - 16 - rayaX);
        if (rayaW > 8)
            fb.fillRect({ rayaX, gy + lineaH / 2, rayaW, 1 }, kPanelEdge);
    }

    // --- los siete restantes, en barras ---
    for (const i32 i : { kComBrand, kComJoin, kComAdmins, kComRanks,
                         kComRules, kComPunish, kComEvents })
        boton(i, false);

    // --- al fondo: cuanto falta ---
    // No es un boton, es el estado de la seccion: dice si ya se puede dar por
    // hecha o que queda. Ocupa el sitio que en la otra mini-seccion tiene
    // GUARDAR TODO, para que las dos se lean igual.
    {
        const Recti& r = m_comSummaryRect;
        if (r.w > 0) {
            // Solo cuentan los que de verdad hay que escribir: los demas
            // arrancan con un valor puesto y nunca estarian "pendientes".
            i32 faltan = 0;
            if (m_comNameField.text().empty())  ++faltan;
            if (m_comDescField.text().empty())  ++faltan;
            if (m_comRulesField.text().empty()) ++faltan;

            const bool lista = (faltan == 0);
            fb.fillRect(r, kFieldBg);
            fb.drawRect(r, lista ? kOk : kPanelEdge, 2);

            char t[96];
            if (lista)
                std::snprintf(t, sizeof(t), "COMUNIDAD LISTA");
            else
                std::snprintf(t, sizeof(t), "FALTA%s %d CAMPO%s POR ESCRIBIR",
                              faltan == 1 ? "" : "N", faltan,
                              faltan == 1 ? "" : "S");
            font5x7::drawFitted(fb, r, t, lista ? kOk : kDim, 2, 6);
        }
    }

    // El panel va encima de la rejilla.
    drawCommunityPanel(fb);
}

void HomeScene::drawCommunityPanel(Framebuffer& fb) {
    if (m_comOpen < 0 || m_comOpen >= kComSlots || m_comPanelRect.w <= 0) return;

    const Recti p = m_comPanelRect;
    const Color acc = kComSlotDefs[m_comOpen].accent;

    fb.fillRect(p, kPanelDeep);
    fb.drawRect(p, acc, 2);

    font5x7::drawTextUtf8(fb, p.x + 12, p.y + 10,
                          kComSlotDefs[m_comOpen].title, acc, 1);
    const char* cerrar = "ESC CIERRA";
    const i32 cw = font5x7::measure(cerrar, 1);
    if (cw < p.w - 24)
        font5x7::drawText(fb, p.x + p.w - cw - 12, p.y + 10, cerrar, kDim, 1);

    const i32 x0 = p.x + 12;
    const i32 ancho = std::max(1, p.w - 24);
    const i32 y0 = p.y + 30;
    const i32 lineaH = textr::lineHeight(1);

    // Los rectangulos de las opciones se apuntan al dibujar: su sitio depende
    // del panel, y el raton los lee en el frame siguiente.
    m_comOptCount = 0;
    auto apunta = [&](Recti r) {
        if (m_comOptCount < 12) m_comOptRects[m_comOptCount++] = r;
    };

    /// Fila seleccionable de una lista. `sel` la marca como elegida.
    auto fila = [&](Recti r, const char* txt, const char* ayuda, bool sel,
                    i32 idx) {
        const bool hov = (m_comOptHover == idx);
        fb.fillRect(r, sel ? kPanelViolet : (hov ? kFieldBgFocus : kFieldBg));
        fb.drawRect(r, sel ? acc : (hov ? acc : kPanelEdge), sel ? 2 : 1);
        font5x7::drawTextUtf8(fb, r.x + 8, r.y + 5, txt,
                              sel ? kWhite : (hov ? kWhite : kText), 1);
        if (ayuda && *ayuda) {
            const i32 aw = font5x7::measureUtf8(ayuda, 1);
            if (aw < r.w - 16)
                font5x7::drawTextUtf8(fb, r.x + r.w - aw - 8, r.y + 5, ayuda,
                                      kDim, 1);
        }
    };

    switch (m_comOpen) {
        case kComName: {
            m_comNameField.setRect({ x0, y0, ancho, 34 });
            m_comNameField.render(fb);
            font5x7::drawTextUtf8(fb, x0, y0 + 42,
                                  "Como se llamara tu comunidad.", kDim, 1);
            break;
        }

        case kComDesc:
        case kComRules: {
            const bool esDesc = (m_comOpen == kComDesc);
            TextField& campo = esDesc ? m_comDescField : m_comRulesField;
            // Caja alta: 5000 caracteres no caben en una linea.
            const i32 alto = std::max(60, p.h - 30 - lineaH * 2 - 16);
            campo.setRect({ x0, y0, ancho, alto });
            campo.render(fb);

            char cnt[64];
            std::snprintf(cnt, sizeof(cnt), "%zu / %zu CARACTERES",
                          campo.length(), esDesc ? kComDescMax : kComRulesMax);
            font5x7::drawTextUtf8(fb, x0, y0 + alto + 4, cnt,
                                  campo.length() >= (esDesc ? kComDescMax
                                                            : kComRulesMax)
                                      ? kConsentRed : kDim, 1);
            font5x7::drawTextUtf8(fb, x0, y0 + alto + 4 + lineaH,
                                  esDesc
                                      ? "Enter hace parrafo."
                                      : "Una regla por linea. Enter hace parrafo.",
                                  kDim, 1);
            break;
        }

        case kComAdmins: {
            // Carril de 1 a 100 con dos botones de +-1 al lado: el carril da
            // el barrido rapido y los botones la cifra exacta, que con 100
            // pasos a lo ancho del panel es imposible de clavar arrastrando.
            char cifra[64];
            std::snprintf(cifra, sizeof(cifra), "%d ADMIN%s", m_comAdmins,
                          m_comAdmins == 1 ? "" : "S");
            const i32 cw2 = font5x7::measureUtf8(cifra, 3);
            font5x7::drawTextUtf8(fb, p.x + (p.w - cw2) / 2, y0 + 6, cifra,
                                  acc, 3);

            const i32 carrilY = y0 + 6 + textr::lineHeight(3) + 14;
            const i32 btnW = 44;
            m_comAdminsLess = Recti{ x0, carrilY - 6, btnW, 30 };
            m_comAdminsMore = Recti{ x0 + ancho - btnW, carrilY - 6, btnW, 30 };
            m_comAdminsTrack = Recti{ x0 + btnW + 10, carrilY,
                                      std::max(1, ancho - btnW * 2 - 20), 16 };

            auto mini = [&](Recti r, const char* t, bool activo) {
                fb.fillRect(r, activo ? kFieldBg : kBg);
                fb.drawRect(r, activo ? acc : kPanelEdge, 1);
                font5x7::drawFitted(fb, r, t, activo ? kText : kDim, 2, 4);
            };
            mini(m_comAdminsLess, "-", m_comAdmins > kComAdminsMin);
            mini(m_comAdminsMore, "+", m_comAdmins < kComAdminsMax);

            fb.fillRect(m_comAdminsTrack, kFieldBg);
            fb.drawRect(m_comAdminsTrack, kPanelEdge, 1);
            const f32 t = static_cast<f32>(m_comAdmins - kComAdminsMin) /
                          static_cast<f32>(kComAdminsMax - kComAdminsMin);
            const i32 lleno = static_cast<i32>(m_comAdminsTrack.w * t);
            if (lleno > 0)
                fb.fillRect({ m_comAdminsTrack.x, m_comAdminsTrack.y,
                              lleno, m_comAdminsTrack.h }, acc);
            // Selector sobre el carril.
            const i32 kx = m_comAdminsTrack.x +
                           std::clamp(lleno, 0, m_comAdminsTrack.w) - 4;
            fb.fillRect({ kx, m_comAdminsTrack.y - 4, 8,
                          m_comAdminsTrack.h + 8 }, kWhite);

            font5x7::drawTextUtf8(fb, x0, carrilY + 28,
                                  "Minimo 1, maximo 100.", kDim, 1);
            break;
        }

        case kComJoin: {
            const i32 fh = std::min(34, std::max(20,
                               (p.h - 40 - lineaH) / kComJoinCount - 4));
            for (i32 i = 0; i < kComJoinCount; ++i) {
                const Recti r{ x0, y0 + i * (fh + 4), ancho, fh };
                apunta(r);
                fila(r, kComJoinModes[i].name, kComJoinModes[i].help,
                     m_comJoinMode == i, i);
            }
            break;
        }

        case kComRanks: {
            // Dos cifras: cuantos rangos y cada cuantas horas se sube uno.
            // El total se dice abajo, porque es lo que de verdad importa:
            // "el ultimo rango llega a las 60 horas".
            char res[96];
            std::snprintf(res, sizeof(res), "%d RANGOS, UNO CADA %dh",
                          m_comRankCount, m_comRankHours);
            const i32 rw = font5x7::measureUtf8(res, 2);
            font5x7::drawTextUtf8(fb, p.x + (p.w - rw) / 2, y0 + 4, res, acc, 2);

            const i32 filaY = y0 + 4 + textr::lineHeight(2) + 12;
            const i32 mitad = std::max(1, (ancho - 10) / 2);
            const i32 fh = 30;

            // Fila de rangos: -1 / +1
            font5x7::drawTextUtf8(fb, x0, filaY, "ESCALONES", kDim, 1);
            const Recti rMenos{ x0, filaY + lineaH + 2, 40, fh };
            const Recti rMas{ x0 + 46, filaY + lineaH + 2, 40, fh };
            apunta(rMenos); apunta(rMas);
            fila(rMenos, "-", nullptr, false, 0);
            fila(rMas,   "+", nullptr, false, 1);

            // Fila de horas: -5 / +5
            font5x7::drawTextUtf8(fb, x0 + mitad + 10, filaY, "HORAS", kDim, 1);
            const Recti hMenos{ x0 + mitad + 10, filaY + lineaH + 2, 40, fh };
            const Recti hMas{ x0 + mitad + 56, filaY + lineaH + 2, 40, fh };
            apunta(hMenos); apunta(hMas);
            fila(hMenos, "-", nullptr, false, 2);
            fila(hMas,   "+", nullptr, false, 3);

            char tot[128];
            std::snprintf(tot, sizeof(tot),
                          "El ultimo rango se alcanza a las %dh jugadas.",
                          m_comRankCount * m_comRankHours);
            font5x7::drawFitted(fb, { x0, filaY + lineaH + 2 + fh + 10,
                                      ancho, lineaH },
                                tot, kText, 1, 0);
            font5x7::drawFitted(fb, { x0, filaY + lineaH + 2 + fh + 10 + lineaH,
                                      ancho, lineaH },
                                "Se ganan jugando, no se reparten a dedo.",
                                kDim, 1, 0);
            break;
        }

        case kComPunish: {
            // Tres tramos. Cada uno abre su lista; fuera de ella se ve que
            // castigo lleva, para leer la escalera completa de un vistazo.
            const char* etiquetas[3] = { "1a VEZ", "2a VEZ", "3a VEZ" };
            const i32 fh = 28;
            i32 y = y0 + 2;
            for (i32 i = 0; i < 3; ++i) {
                font5x7::drawTextUtf8(fb, x0, y, etiquetas[i], kDim, 1);
                const Recti r{ x0 + 64, y - 4, std::max(1, ancho - 64), fh };
                apunta(r);
                const bool abierto = (m_comPunishOpen == i);
                fila(r, kComPunishSteps[std::clamp(m_comPunish[i], 0,
                                                   kComPunishCount - 1)],
                     abierto ? "ELIGE ABAJO" : nullptr, abierto, i);
                y += fh + 6;
            }

            // Lista del tramo desplegado.
            if (m_comPunishOpen >= 0 && m_comPunishOpen < 3) {
                y += 4;
                for (i32 k = 0; k < kComPunishCount; ++k) {
                    const Recti r{ x0, y, ancho, 24 };
                    if (r.y + r.h > p.y + p.h - 6) break;
                    apunta(r);
                    fila(r, kComPunishSteps[k], nullptr,
                         m_comPunish[m_comPunishOpen] == k, 3 + k);
                    y += 26;
                }
            } else {
                font5x7::drawFitted(fb, { x0, y + 6, ancho, lineaH },
                                    "Pulsa un tramo para cambiar su castigo.",
                                    kDim, 1, 0);
            }
            break;
        }

        case kComEvents: {
            const i32 fh = std::min(28, std::max(20,
                               (p.h - 40 - lineaH) / kComEventCount - 4));
            for (i32 i = 0; i < kComEventCount; ++i) {
                const Recti r{ x0, y0 + i * (fh + 3), ancho, fh };
                if (r.y + r.h > p.y + p.h - lineaH - 8) break;
                apunta(r);
                const bool on = std::find(m_comEvents.begin(), m_comEvents.end(),
                                          i) != m_comEvents.end();
                fila(r, kComEventKinds[i].name, kComEventKinds[i].when, on, i);
            }
            font5x7::drawFitted(fb, { x0, p.y + p.h - lineaH - 6, ancho, lineaH },
                                "Se repiten solos. Pulsa para activar.",
                                kDim, 1, 0);
            break;
        }

        case kComBrand: {
            // Color arriba, insignia abajo. La muestra se pinta con el color
            // elegido: elegirlo por su nombre a ciegas no dice como se vera.
            font5x7::drawTextUtf8(fb, x0, y0, "COLOR", kDim, 1);
            const i32 cajaW = std::max(1, (ancho - 5 * 4) / 6);
            const i32 cy = y0 + lineaH + 4;
            for (i32 i = 0; i < kComColorCount; ++i) {
                const Recti r{ x0 + i * (cajaW + 4), cy, cajaW, 28 };
                apunta(r);
                fb.fillRect(r, kComColors[i].c);
                const bool sel = (m_comColor == i);
                fb.drawRect(r, sel ? kWhite : kPanelEdge, sel ? 3 : 1);
            }

            const i32 by = cy + 28 + 12;
            font5x7::drawTextUtf8(fb, x0, by, "INSIGNIA", kDim, 1);
            const i32 insY = by + lineaH + 4;
            const i32 insH = 26;
            for (i32 i = 0; i < kComBadgeCount; ++i) {
                const i32 col = i % 2;
                const i32 f2  = i / 2;
                const i32 w2  = std::max(1, (ancho - 6) / 2);
                const Recti r{ x0 + col * (w2 + 6), insY + f2 * (insH + 4),
                               w2, insH };
                if (r.y + r.h > p.y + p.h - 6) break;
                apunta(r);
                fila(r, kComBadges[i], nullptr, m_comBadge == i,
                     kComColorCount + i);
            }
            break;
        }

        default: break;
    }
}

void HomeScene::drawDevDelete(Framebuffer& fb, Recti area) {
    layoutDevDelete(area);

    font5x7::drawTextUtf8(fb, area.x + 16, area.y + 16, "ELIMINAR JUEGO",
                          kConsentRed, 2);
    fb.fillRect({ area.x + 16, area.y + 40,
                  std::max(1, area.w - 32), 1 }, kConsentRed);

    // --- campo del codigo, con el juego al que apunta ---
    m_delCodeField.setRect(m_delFieldRect);
    m_delCodeField.render(fb);

    if (m_delFound >= 0) {
        // Con el codigo resuelto se ensena el NOMBRE del juego, no un simple
        // "encontrado": es la ultima oportunidad de ver que se va a retirar
        // antes de confirmar algo que no se puede deshacer.
        std::string r = (m_delFound == 1)
                            ? ("Se retirara: " + m_delJuego)
                            : std::string("Ningun juego tuyo tiene ese codigo");
        i32 esc = 1;
        if (font5x7::measureUtf8(r.c_str(), esc) > area.w - 32)
            r = (m_delFound == 1) ? m_delJuego : std::string("Codigo no valido");
        if (font5x7::measureUtf8(r.c_str(), esc) < area.w - 32)
            font5x7::drawTextUtf8(fb, area.x + 16,
                                  m_delFieldRect.y + m_delFieldRect.h + 6,
                                  r.c_str(),
                                  m_delFound == 1 ? kOk : kConsentRed, esc);
    }

    // --- el aviso, con scroll ---
    const Recti cr = m_delNoticePanel.contentRect();
    m_delNoticePanel.setContentHeight(kDeleteNoticeLines * 12 + 8);
    m_delNoticePanel.renderBar(fb);

    for (i32 i = 0; i < kDeleteNoticeLines; ++i) {
        const i32 y = cr.y + i * 12 - m_delNoticePanel.offset();
        if (y + 12 <= cr.y || y >= cr.y + cr.h) continue;
        if (!kDeleteNotice[i][0]) continue;
        // La primera linea lleva el "¡Aviso!" y va en carmesi; el resto en
        // texto normal, que es mas legible en un parrafo largo.
        font5x7::drawTextUtf8(fb, cr.x + 2, y, kDeleteNotice[i],
                              i == 0 ? kConsentRed : kText, 1);
    }

    // --- los dos botones ---
    // Solo se pueden pulsar con un juego encontrado: confirmar sin nombre
    // valido no tendria a que aplicarse.
    const bool listo = (m_delFound == 1);

    auto boton = [&](Recti r, const char* txt, Color c, bool hov, bool down,
                     bool activo) {
        if (r.w <= 0) return;
        fb.fillRect(r, down ? kSteamEdge : (hov && activo ? c : kFieldBg));
        fb.drawRect(r, activo ? c : kSteamEdge, 2);
        font5x7::drawFitted(fb, r, txt,
                            activo ? (hov ? kBg : kText) : kDim, 1, 4);
    };

    boton(m_delYesRect, "ACEPTAR Y RETIRAR", kConsentRed,
          m_delYesHover, m_delYesDown, listo);
    boton(m_delNoRect,  "NO ELIMINAR", kConsentGreen,
          m_delNoHover, m_delNoDown, true);

    // Aviso del resultado, bajo los botones.
    if (!m_delMsg.empty()) {
        const i32 mw = font5x7::measureUtf8(m_delMsg.c_str(), 1);
        if (mw < area.w - 20)
            font5x7::drawTextUtf8(fb, area.x + (area.w - mw) / 2,
                                  area.y + area.h - 12, m_delMsg.c_str(),
                                  m_delMsgErr ? kConsentRed : kOk, 1);
    }
}

void HomeScene::drawDevUpdate(Framebuffer& fb, Recti area) {
    drawDevFiles(fb, area);
}

void HomeScene::layoutPriceOptions(Recti area) {
    // La rejilla vive en la mitad INFERIOR de la mini-seccion: arriba manda el
    // deslizador con su contador, que es lo que se mira al fijar el precio.
    const i32 margen = 22;
    const i32 anchoUtil = std::max(1, area.w - margen * 2);
    const i32 colW = (anchoUtil - kPriceOptGap) / kPriceOptCols;

    // Se ancla al fondo del cuadro y se sube lo que ocupan las tres filas, de
    // modo que la rejilla queda pegada abajo aunque el panel cambie de tamano.
    const i32 altoRejilla = kPriceOptRows * kPriceOptH +
                            (kPriceOptRows - 1) * kPriceOptGap;
    const i32 y0 = area.y + area.h - margen - altoRejilla;

    for (i32 i = 0; i < kPriceOptCount; ++i) {
        // Se llena por COLUMNAS: dos columnas de tres, como se pidio.
        const i32 col = i / kPriceOptRows;
        const i32 fila = i % kPriceOptRows;
        m_priceOptRects[i] = Recti{
            area.x + margen + col * (colW + kPriceOptGap),
            y0 + fila * (kPriceOptH + kPriceOptGap),
            std::max(1, colW), kPriceOptH
        };
    }

    // Lista desplegable del boton abierto: se pinta SOBRE la rejilla, anclada
    // al boton que la abrio y acotada para no salirse del cuadro.
    if (m_priceOptOpen >= 0 && m_priceOptOpen < kPriceOptCount) {
        const Recti& b = m_priceOptRects[m_priceOptOpen];
        const i32 filaH = priceOptionRowHeight();
        const i32 n = kPriceOptions[m_priceOptOpen].count;
        const i32 alto = n * filaH + 8;
        // Si no cabe hacia abajo, se despliega hacia arriba.
        i32 ly = b.y + b.h + 4;
        if (ly + alto > area.y + area.h) ly = std::max(area.y + 4, b.y - alto - 4);
        m_priceOptListRect = Recti{ b.x, ly, b.w, alto };
    } else {
        m_priceOptListRect = Recti{};
    }
}

void HomeScene::updatePriceOptions(Engine& engine) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const i32 lx = static_cast<i32>(in.mousePos().x / sc);
    const i32 ly = static_cast<i32>(in.mousePos().y / sc);

    auto dentro = [&](const Recti& r) {
        return r.w > 0 && lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
    };

    // --- lista desplegada: tiene prioridad sobre la rejilla que hay debajo ---
    if (m_priceOptOpen >= 0) {
        const i32 filaH = priceOptionRowHeight();
        const i32 n = kPriceOptions[m_priceOptOpen].count;
        m_priceOptListHover = -1;
        if (dentro(m_priceOptListRect)) {
            const i32 idx = (ly - m_priceOptListRect.y - 4) / filaH;
            if (idx >= 0 && idx < n) m_priceOptListHover = idx;
        }

        if (in.mouseReleased(0)) {
            if (m_priceOptListHover >= 0) {
                m_priceOptSel[m_priceOptOpen] = m_priceOptListHover;
                m_priceOptSet[m_priceOptOpen] = true;   // elegido de verdad
                m_priceOptOpen = -1;   // elegir cierra la lista
            } else if (!dentro(m_priceOptRects[m_priceOptOpen])) {
                m_priceOptOpen = -1;   // clic fuera: cerrar sin cambiar nada
            }
        }
        return;   // con la lista abierta, nada mas de la rejilla responde
    }

    // --- rejilla de seis botones ---
    m_priceOptHover = -1;
    for (i32 i = 0; i < kPriceOptCount; ++i)
        if (dentro(m_priceOptRects[i])) m_priceOptHover = i;

    if (in.mousePressed(0) && m_priceOptHover >= 0)
        m_priceOptDown = m_priceOptHover;

    if (in.mouseReleased(0)) {
        if (m_priceOptDown >= 0 && m_priceOptDown == m_priceOptHover)
            m_priceOptOpen = m_priceOptDown;   // abrir su lista
        m_priceOptDown = -1;
    }
}

void HomeScene::drawPriceOptions(Framebuffer& fb) {
    // --- los seis botones ---
    for (i32 i = 0; i < kPriceOptCount; ++i) {
        const Recti r = m_priceOptRects[i];
        if (r.w <= 0 || r.h <= 0) continue;

        const bool hov    = (m_priceOptHover == i && m_priceOptOpen < 0);
        const bool down   = (m_priceOptDown == i && hov);
        const bool activo = (m_priceOptOpen == i);

        fb.fillRect(r, down ? kClosePress : (hov || activo) ? kPanelViolet : kFieldBg);
        fb.drawRect(r, (hov || activo) ? kDevGold : kPanelEdge, activo ? 2 : 1);

        // Titulo arriba en dorado apagado, valor elegido debajo en blanco: el
        // boton dice a la vez que ajusta y como esta ajustado.
        font5x7::drawText(fb, r.x + 8, r.y + 8, kPriceOptions[i].title,
                          (hov || activo) ? kDevGold : kDim, 1);

        const char* val = priceOptionValue(i);
        // Se reduce la escala si el valor no cabe, en vez de desbordar.
        i32 esc = 2;
        while (esc > 1 && font5x7::measureUtf8(val, esc) > r.w - 16) --esc;
        font5x7::drawTextUtf8(fb, r.x + 8, r.y + 22, val,
                              (hov || activo) ? kWhite : kText, esc);
    }

    // --- lista desplegada, encima de todo lo demas ---
    if (m_priceOptOpen < 0 || m_priceOptListRect.w <= 0) return;

    const PriceOptionDef& d = kPriceOptions[m_priceOptOpen];
    const i32 filaH = priceOptionRowHeight();

    fb.fillRect(m_priceOptListRect, kPanelDeep);
    fb.drawRect(m_priceOptListRect, kDevGold, 2);

    for (i32 i = 0; i < d.count; ++i) {
        const Recti fila{ m_priceOptListRect.x + 3,
                          m_priceOptListRect.y + 4 + i * filaH,
                          std::max(1, m_priceOptListRect.w - 6), filaH - 2 };

        const bool hov = (m_priceOptListHover == i);
        const bool sel = (m_priceOptSel[m_priceOptOpen] == i);
        if (hov || sel) fb.fillRect(fila, hov ? kDevGold : kPanelViolet);

        // El elegido se marca con un punto, para que se distinga del que solo
        // esta bajo el cursor.
        if (sel) fb.fillRect({fila.x + 4, fila.y + fila.h / 2 - 2, 4, 4},
                             hov ? kBg : kDevGold);

        // Los valores llevan acentos y enye ("MÉXICO", "ESPAÑA").
        font5x7::drawTextUtf8(fb, fila.x + 14,
                              fila.y + (fila.h - textr::glyphHeight(1)) / 2,
                              d.values[i], hov ? kBg : (sel ? kWhite : kGlyph), 1);
    }
}

void HomeScene::drawDevPrice(Framebuffer& fb, Recti area) {
    const char* titulo = "PRECIO";
    font5x7::drawText(fb, area.x + 22, area.y + 22, titulo, kDevGold, 1);

    const f32 t = std::clamp(m_priceT, 0.0f, 1.0f);

    /// Color del deslizador segun la posicion: azul a la izquierda (Gratis),
    /// rojo a la derecha (maximo), interpolado linealmente en medio.
    auto mezcla = [&](f32 k) {
        auto lerp = [&](u8 a, u8 b) {
            return static_cast<u8>(a + (b - a) * k);
        };
        return Color{ lerp(kPriceFree.r, kPriceMax.r),
                      lerp(kPriceFree.g, kPriceMax.g),
                      lerp(kPriceFree.b, kPriceMax.b) };
    };

    // --- barrita ---
    // Canal apagado de fondo y, encima, la parte recorrida con el degradado
    // azul->rojo: el color del tramo lleno ya dice cuanto cuesta.
    fb.fillRect(m_priceTrackRect, kFieldBg);
    fb.drawRect(m_priceTrackRect, kPanelEdge, 1);

    const i32 lleno = m_priceKnobRect.x + kPriceKnobW / 2 - m_priceTrackRect.x;
    for (i32 i = 0; i < lleno && i < m_priceTrackRect.w; ++i) {
        const f32 k = static_cast<f32>(i) / std::max(1, m_priceTrackRect.w - 1);
        fb.fillRect({ m_priceTrackRect.x + i, m_priceTrackRect.y,
                      1, m_priceTrackRect.h }, mezcla(k));
    }

    // --- selector rectangular ---
    const Color cKnob = mezcla(t);
    fb.fillRect(m_priceKnobRect, cKnob);
    fb.drawRect(m_priceKnobRect,
                (m_priceKnobHover || m_priceDragging) ? kWhite : kDevGold, 2);

    // --- contador, DEBAJO y centrado respecto a la barrita ---
    // Sube de GRATIS al maximo conforme el selector avanza. Va centrado en la
    // barra (no en el selector) para que no baile de un lado a otro al
    // arrastrar: el numero cambia, su sitio no.
    std::string usd, mxn, eur;
    devcatalog::formatPrice(priceUsdCents(), usd, mxn, eur);

    const i32 centroX = m_priceTrackRect.x + m_priceTrackRect.w / 2;
    // El hueco bajo la barra se reparte con el alto REAL de linea. Antes se
    // usaba kGlyphH (7 px, el de la fuente bitmap), y con la fuente del
    // sistema -- mas alta -- el contador quedaba muy por debajo de su sitio.
    i32 cy = m_priceTrackRect.y + kPriceTrackH + 14;

    if (priceUsdCents() <= 0) {
        // En el extremo izquierdo el contador es una sola palabra, en el mismo
        // azul del deslizador para que se lean como una sola pieza.
        const char* libre = "GRATIS";
        const i32 lw = font5x7::measure(libre, 3);
        font5x7::drawText(fb, centroX - lw / 2, cy, libre, kPriceFree, 3);
    } else {
        // USD grande (es el precio base) y las conversiones debajo, menores.
        const i32 uw = font5x7::measure(usd.c_str(), 2);
        font5x7::drawText(fb, centroX - uw / 2, cy, usd.c_str(), cKnob, 2);
        cy += textr::lineHeight(2) + 2;

        const i32 mw = font5x7::measure(mxn.c_str(), 1);
        font5x7::drawText(fb, centroX - mw / 2, cy, mxn.c_str(), kText, 1);
        cy += textr::lineHeight(1) + 1;

        const i32 ew = font5x7::measure(eur.c_str(), 1);
        font5x7::drawText(fb, centroX - ew / 2, cy, eur.c_str(), kText, 1);
    }

    // --- topes del carril ---
    // GRATIS a la IZQUIERDA y la moneda a la derecha, que es como se lee de
    // menos a mas. La separacion usa el alto real de la fuente: con los 18 px
    // fijos de antes, la etiqueta se montaba sobre la barra al crecer la
    // letra.
    const i32 topeY = m_priceTrackRect.y - textr::lineHeight(1) - 6;

    const char* libre2 = "GRATIS";
    font5x7::drawText(fb, m_priceTrackRect.x, topeY, libre2, kPriceFree, 1);

    const char* maximo = "400 USD";
    const i32 mxw = font5x7::measure(maximo, 1);
    font5x7::drawText(fb, m_priceTrackRect.x + m_priceTrackRect.w - mxw,
                      topeY, maximo, kPriceMax, 1);

    // --- ACEPTAR PRECIO, bajo la barrita y su contador ---
    // Mismo remolino que GUARDAR Y SUBIR: las dos cierran su seccion, y el
    // aspecto compartido lo dice sin necesidad de explicarlo.
    if (m_priceOkRect.w > 0) {
        Recti okr = m_priceOkRect;
        const bool negando = (m_priceOptWarn > 0.0);

        if (negando) {
            // Mismo temblor que el aviso de GUARDAR Y SUBIR: horizontal,
            // rapido y con la amplitud cayendo segun se agota.
            const f64 restante = m_priceOptWarn / 1.2;
            const i32 amp = static_cast<i32>(std::lround(5.0 * restante));
            okr.x += static_cast<i32>(std::lround(
                         std::sin(m_priceOptWarn * 42.0) * amp));

            const f32 pulso = 0.75f +
                0.25f * static_cast<f32>(std::sin(m_priceOptWarn * 20.0));
            const Color rojo{
                static_cast<u8>(std::clamp(kAccent.r * pulso, 0.0f, 255.0f)),
                static_cast<u8>(std::clamp(kAccent.g * pulso, 0.0f, 255.0f)),
                static_cast<u8>(std::clamp(kAccent.b * pulso, 0.0f, 255.0f)) };
            fb.fillRect(okr, rojo);
            fb.drawRect(okr, kWhite, 2);

            const char* av = "ACEPTAR PRECIO";
            const i32 aw = font5x7::measureUtf8(av, 1);
            if (aw < okr.w - 8)
                font5x7::drawTextUtf8(fb, okr.x + (okr.w - aw) / 2,
                                      okr.y + (okr.h - textr::glyphHeight(1)) / 2,
                                      av, kWhite, 1);
        } else {
            drawSwirlButton(fb, okr, m_devAnimClock,
                            m_priceOkHover, m_priceOkDown,
                            "ACEPTAR PRECIO", "ACEPTAR", "PRECIO");
        }
    }

    // --- PRECIO A ESCRITO ---
    if (m_priceTypeRect.w > 0) {
        const Recti r = m_priceTypeRect;
        fb.fillRect(r, m_priceTypeDown ? kSteamEdge
                    : (m_priceTypeHover || m_priceTypeOpen) ? kSteamPanelHi
                                                            : kFieldBg);
        fb.drawRect(r, (m_priceTypeHover || m_priceTypeOpen) ? kSteamBlue
                                                            : kPanelEdge, 1);
        font5x7::drawFitted(fb, r, "PRECIO A ESCRITO",
                            m_priceTypeOpen ? kSteamBlue : kText, 1, 4);

        // Cuadro de escritura y confirmar, solo con el cuadro abierto.
        if (m_priceTypeOpen && m_priceTypeFieldRect.w > 0) {
            // Fondo propio: se superpone al carril y sin el se leerian
            // mezclados.
            const Recti caja{ m_priceTypeFieldRect.x - 6,
                              m_priceTypeFieldRect.y - 6,
                              m_priceTypeOkRect.x + m_priceTypeOkRect.w -
                                  m_priceTypeFieldRect.x + 12,
                              m_priceTypeFieldRect.h + 34 };
            fb.fillRect(caja, kPanelDeep);
            fb.drawRect(caja, kSteamBlue, 2);

            m_priceTypeField.setRect(m_priceTypeFieldRect);
            m_priceTypeField.render(fb);

            const Recti ok = m_priceTypeOkRect;
            fb.fillRect(ok, m_priceTypeOkDown ? kSteamEdge
                        : (m_priceTypeOkHover ? kConsentGreen : kFieldBg));
            fb.drawRect(ok, kConsentGreen, 2);
            font5x7::drawFitted(fb, ok, "CONFIRMAR",
                                m_priceTypeOkHover ? kBg : kText, 1, 4);

            // Pista de formato y, tras confirmar, el resultado.
            const char* pista = "Escribe la cifra en dolares: 19.99";
            const i32 pw = font5x7::measureUtf8(pista, 1);
            if (pw < caja.w - 12)
                font5x7::drawTextUtf8(fb, caja.x + 6,
                                      m_priceTypeFieldRect.y +
                                      m_priceTypeFieldRect.h + 8,
                                      pista, kDim, 1);
        }

        // El aviso se muestra aunque el cuadro ya este cerrado: es la
        // confirmacion de que el precio quedo fijado.
        if (!m_priceTypeMsg.empty()) {
            const i32 mw = font5x7::measureUtf8(m_priceTypeMsg.c_str(), 1);
            if (mw < area.w - 20)
                font5x7::drawTextUtf8(fb, area.x + (area.w - mw) / 2,
                                      m_priceTypeRect.y - 16,
                                      m_priceTypeMsg.c_str(),
                                      m_priceTypeMsgErr ? kAccent : kOk, 1);
        }
    }

    // --- MANTENER PRECIOS POR DEFECTO ---
    if (m_priceDefRect.w > 0) {
        const Recti r = m_priceDefRect;
        const bool listo = priceOptionsReady();

        fb.fillRect(r, m_priceDefDown ? kSteamEdge
                    : (m_priceDefHover || m_priceDefOpen) ? kSteamPanelHi
                                                          : kFieldBg);
        fb.drawRect(r, listo ? kOk
                    : (m_priceDefHover || m_priceDefOpen) ? kSteamBlue
                                                          : kPanelEdge, 1);

        // El rotulo se encoge hasta caber: con la fuente del sistema no
        // entraba a escala 1 y el boton se quedaba mudo.
        font5x7::drawFitted(fb, r, "MANTENER PRECIOS POR DEFECTO?",
                            listo ? kOk : kText, 1, 4);

        // Los dos mini-botones, solo con la pregunta abierta.
        if (m_priceDefOpen) {
            auto mini = [&](Recti m, const char* txt, Color c, bool hov) {
                if (m.w <= 0) return;
                fb.fillRect(m, hov ? c : kFieldBg);
                fb.drawRect(m, c, 1);
                font5x7::drawFitted(fb, m, txt, hov ? kBg : c, 1, 3);
            };
            mini(m_priceDefYes, "ACEPTAR", kConsentGreen, m_priceDefYesHover);
            mini(m_priceDefNo,  "NEGAR",   kConsentRed,   m_priceDefNoHover);
        }
    }

    // --- rejilla de ajustes de publicacion, en la mitad inferior ---
    layoutPriceOptions(area);
    drawPriceOptions(fb);

    // --- aviso "Elige el tipo de Moneda y paises!!", SOBRE todo lo demas ---
    // Va el ultimo, igual que el de GUARDAR Y SUBIR: es una interrupcion y
    // tiene que leerse aunque caiga sobre la rejilla de ajustes.
    if (m_priceOptWarn > 0.0 && area.w > 40) {
        const char* msg = "Elige el tipo de Moneda y paises!!";

        i32 escA = 3;
        while (escA > 1 && font5x7::measureUtf8(msg, escA) > area.w - 24) --escA;
        const i32 mw = font5x7::measureUtf8(msg, escA);
        const i32 mh = font5x7::kGlyphH * escA;

        const i32 pad = 10;
        const i32 franjaY = std::min(m_priceOkRect.y + m_priceOkRect.h + 12,
                                     area.y + area.h - mh - pad * 2 - 4);
        const Recti franja{ area.x + 6, franjaY,
                            std::max(1, area.w - 12), mh + pad * 2 };

        fb.fillRect(franja, kBg);

        const f32 pulso = 0.6f +
            0.4f * static_cast<f32>(std::sin(m_priceOptWarn * 20.0));
        const Color borde{
            static_cast<u8>(std::clamp(kAccent.r * pulso, 0.0f, 255.0f)),
            static_cast<u8>(std::clamp(kAccent.g * pulso, 0.0f, 255.0f)),
            static_cast<u8>(std::clamp(kAccent.b * pulso, 0.0f, 255.0f)) };
        fb.drawRect(franja, borde, 3);

        font5x7::drawTextUtf8(fb, franja.x + (franja.w - mw) / 2,
                              franja.y + pad, msg, kAccent, escA);
    }

    // --- palomita de precio aceptado, encima de todo ---
    // Se dibuja al final para que no la tape nada: es la confirmacion de que
    // la accion salio bien, y dura lo justo antes de saltar a SUBIR JUEGO.
    if (m_priceOkAnimOn) {
        const f32 t = static_cast<f32>(m_priceOkAnim / kPriceOkAnimDur);

        // Velo oscuro que entra y sale con ella: concentra la vista en la
        // palomita y separa el gesto del resto del panel.
        const f32 vel = std::clamp(t / 0.25f, 0.0f, 1.0f) *
                        (1.0f - std::clamp((t - 0.8f) / 0.2f, 0.0f, 1.0f));
        Color velo = kBg;
        velo.a = static_cast<u8>(std::lround(vel * 190.0f));
        fb.fillRect(area, velo);

        const i32 radio = std::max(12, std::min(area.w, area.h) / 6);
        drawCheckmark(fb, area.x + area.w / 2, area.y + area.h / 2 - 10,
                      radio, t);

        // Rotulo bajo la palomita, entrando algo despues que ella.
        const f32 tTexto = std::clamp((t - 0.35f) / 0.25f, 0.0f, 1.0f);
        if (tTexto > 0.0f) {
            const char* ok = "PRECIO ACEPTADO";
            i32 e = 2;
            while (e > 1 && font5x7::measureUtf8(ok, e) > area.w - 24) --e;
            const i32 w = font5x7::measureUtf8(ok, e);
            Color c = kNeonGreen;
            c.a = static_cast<u8>(std::lround(
                      tTexto * (1.0f - std::clamp((t - 0.8f) / 0.2f, 0.0f, 1.0f))
                      * 255.0f));
            font5x7::drawTextUtf8(fb, area.x + (area.w - w) / 2,
                                  area.y + area.h / 2 + radio + 14, ok, c, e);
        }
    }
}

void HomeScene::drawDev(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 h   = fb.height() - top;

    fb.fillRect({0, top, fb.width(), h}, kBg);

    // Cabecera y salida.
    font5x7::drawText(fb, kDevColMargin, top + 14, "MODO DESARROLLADOR",
                      kDevPurple, 2);
    const char* volver = "< VOLVER (ESC)";
    const i32 vw = font5x7::measure(volver, 1);
    font5x7::drawText(fb, fb.width() - vw - 24, top + 20, volver, kDim, 1);

    // --- los botones de la columna ---
    // Cada uno con su pareja lineas/relleno. El texto va en blanco en todos:
    // es el color que mejor contrasta con los rellenos a la vez.
    struct Boton { const char* txt; Color linea; Color relleno; };
    const Boton botones[kDevButtons] = {
        { "SUBIR JUEGO",      kDevMint,  kDevTeal   },
        // El nombre dice las DOS cosas que hace la seccion desde que se
        // fusionaron: guardar los archivos dentro de Ludora y publicar con
        // ello la version nueva. Llamarla solo "ACTUALIZAR" escondia la mitad.
        { "GUARDAR ARCHIVOS Y ACTUALIZAR", kDevBrown, kDevIndigo },
        { "PRECIOS",          kDevGold,  kDevLime   },
        // Eliminar va en carmesi sobre negro: el color avisa de que esta
        // accion no se deshace, sin necesidad de decirlo.
        { "ELIMINAR JUEGO",   kConsentRed, kBg      },
        { "CREAR COMUNIDAD",  kDevPurple, kPanelViolet },
    };

    for (i32 i = 0; i < kDevButtons; ++i) {
        const Recti r = m_devBtnRects[i];
        if (r.w <= 0 || r.h <= 0) continue;

        const bool hov    = (m_devHover == i);
        const bool down   = (m_devDown == i && hov);
        const bool activo = (m_devPanel == i);

        // Pulsado oscurece el relleno; abierto o con el raton encima lo aclara.
        // Asi el boton dice a la vez si esta bajo el cursor y si su panel esta
        // abierto, sin necesidad de un indicador aparte.
        auto ajusta = [&](Color c, f32 k) {
            return Color{ static_cast<u8>(std::clamp(c.r * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.g * k, 0.0f, 255.0f)),
                          static_cast<u8>(std::clamp(c.b * k, 0.0f, 255.0f)) };
        };
        const Color fondo = down   ? ajusta(botones[i].relleno, 0.65f)
                          : (hov || activo) ? ajusta(botones[i].relleno, 1.35f)
                          : botones[i].relleno;

        fb.fillRect(r, fondo);
        // Marco de "lineas" del color propio: mas grueso cuando esta abierto.
        fb.drawRect(r, botones[i].linea, activo ? 3 : 2);

        // Etiqueta blanca centrada. Se reduce la escala si no cabe, en vez de
        // dejar que el texto se salga del boton.
        i32 esc = 2;
        while (esc > 1 && font5x7::measure(botones[i].txt, esc) > r.w - 20) --esc;
        const i32 tw = font5x7::measure(botones[i].txt, esc);
        font5x7::drawText(fb, r.x + (r.w - tw) / 2,
                          r.y + (r.h - textr::glyphHeight(esc)) / 2,
                          botones[i].txt, kWhite, esc);
    }

    // --- LudoUpdates, en la zona central ---
    // Se dibuja SIEMPRE, tambien con una mini-seccion abierta: el reglamento
    // es el fondo de la seccion y desaparecer al pulsar SUBIR JUEGO dejaba
    // un hueco negro donde antes habia contenido.
    //
    // Va ANTES que el panel, que es opaco y se pinta encima. Lo que queda a
    // los lados del panel sigue mostrando el tablon.
    drawLudoUpdates(fb);

    // --- mini-seccion cuadrada de la derecha ---
    if (m_devPanel != kDevNone && m_devPanelRect.w > 40) {
        // ELIMINAR JUEGO va sobre negro, no sobre el violeta de las demas:
        // el contraste con el carmesi del marco es lo que la separa del
        // resto de secciones de un vistazo.
        fb.fillRect(m_devPanelRect,
                    m_devPanel == kDevDelete ? kBg : kPanelDeep);

        // El marco toma el color del boton que la abrio, para que se vea de
        // donde salio el panel.
        const Color marco = (m_devPanel == kDevUpload) ? kDevMint
                          : (m_devPanel == kDevUpdate) ? kDevBrown
                          : (m_devPanel == kDevDelete) ? kConsentRed
                          : (m_devPanel == kDevCommunity) ? kDevPurple
                                                       : kDevGold;
        fb.drawRect(m_devPanelRect, marco, 2);

        if      (m_devPanel == kDevUpload) drawDevUpload(fb, m_devPanelRect);
        else if (m_devPanel == kDevUpdate) drawDevUpdate(fb, m_devPanelRect);
        else if (m_devPanel == kDevDelete) drawDevDelete(fb, m_devPanelRect);
        else if (m_devPanel == kDevCommunity)
            drawDevCommunity(fb, m_devPanelRect);
        else                               drawDevPrice(fb, m_devPanelRect);

        // Aviso al pie de la mini-seccion.
        if (!m_devMsg.empty()) {
            // El aviso puede llevar acentos ("está reservado, aún sin uso").
            const i32 mw = font5x7::measureUtf8(m_devMsg.c_str(), 1);
            if (mw < m_devPanelRect.w)
                font5x7::drawTextUtf8(fb,
                    m_devPanelRect.x + (m_devPanelRect.w - mw) / 2,
                    m_devPanelRect.y + m_devPanelRect.h - 20,
                    m_devMsg.c_str(), m_devMsgErr ? kAccent : kOk, 1);
        }
    }
}

void HomeScene::drawSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 h   = fb.height() - top;
    if (h <= 0) return;

    if (m_section == kStoreSection) {
        // Lienzo azul oscuro de tienda, no el negro del resto del motor: es
        // lo que deja respirar a las portadas.
        const i32 topS = m_titleBar.heightLogical(engine) + kTopBarThickness;
        fb.fillRect({0, topS, fb.width(), fb.height() - topS}, kSteamBg);
        layoutStore(engine, fb.width(), fb.height());
        drawStore(engine, fb);
        return;
    }

    if (m_section == kChatSection) {
        layoutChat(engine, fb.width(), fb.height());
        drawChat(engine, fb);
        return;
    }

    if (m_section == kAccountSection) {
        layoutAccount(engine, fb.width(), fb.height());
        drawAccount(engine, fb);
        return;
    }

    if (m_section == kReviewsSection) {
        drawReviews(engine, fb);
        return;
    }

    if (m_section == kDevSection) {
        layoutDev(engine, fb.width(), fb.height());
        drawDev(engine, fb);
        return;
    }

    if (m_section == kEulaSection) {
        drawEula(engine, fb);   // coloca y dibuja: es toda la pantalla
        return;
    }

    if (m_section == kProductSection) {
        drawProduct(engine, fb);   // coloca y dibuja
        return;
    }

    if (m_section == kGenreSection) {
        m_storeCards.clear();      // las filas se vuelven a apuntar al dibujar
        drawGenreSection(engine, fb);
        return;
    }

    if (m_section == kCreatorSection) {
        drawCreatorSection(engine, fb);
        return;
    }

    if (m_section == kCommunitySection) {
        drawCommunitySection(engine, fb);
        return;
    }

    if (m_section == kModsSection) {
        drawModsSection(engine, fb);
        return;
    }

    if (m_section == kNoticesSection) {
        drawNoticesSection(engine, fb);
        return;
    }

    if (m_section == kPanelsSection) {
        // Las franjas ceden ante el centro: con la ventana pequena se estrechan
        // y, si no caben con un ancho legible, desaparecen y el detalle ocupa
        // todo. Antes se llevaban la mitad del ancho cada una, de modo que por
        // debajo de ~700 px el detalle quedaba en 0 y no se veia nada.
        const i32 w = librarySideWidth(fb.width());

        // Izquierda: panel violeta con scroll. Derecha: se mantiene en rojo.
        if (w > 0) {
            m_leftPanel.setRect({0, top, w, h});
            m_leftPanel.setContentHeight(kItemPad + kLibraryItems * (kItemH + kItemPad));
            drawLibraryPanel(fb);
        } else {
            // Sin sitio para el panel: se anula su rectangulo para que no
            // capture clics de una zona que ya no se dibuja.
            m_leftPanel.setRect({0, top, 0, h});
        }

        // Centro: detalle del juego seleccionado, con su imagen y los botones.
        const Recti centro{ w, top, fb.width() - 2 * w, h };
        drawGameDetail(engine, fb, centro);

        // Derecha: la franja roja con las dos secciones dentro.
        if (w > 0) {
            const Recti franja{ fb.width() - w, top, w, h };
            fb.fillRect(franja, kLedRed);
            drawLibrarySidePanels(fb, franja);
        } else {
            // Sin franja no hay secciones que dibujar: se limpian sus
            // rectangulos para que las pruebas y los clics no apunten a
            // una zona fantasma.
            m_priceTimelineRect = Recti{};
            m_versionHistRect   = Recti{};
        }
        return;
    }

    // Las demas secciones estan vacias a proposito: solo se indica cual es,
    // para que se note que la navegacion funciona.
    const char* nombre = kSectionNames[m_section];
    const i32 tw = font5x7::measure(nombre, 3);
    font5x7::drawText(fb, (fb.width() - tw) / 2, top + h / 2 - 20,
                      nombre, kBlueDivider, 3);

    const char* vacio = "seccion vacia";
    const i32 vw = font5x7::measure(vacio, 1);
    font5x7::drawText(fb, (fb.width() - vw) / 2, top + h / 2 + 14,
                      vacio, kDim, 1);
}

void HomeScene::render(Engine& engine, Framebuffer& fb) {
    m_lastFbWidth  = fb.width();
    m_lastFbHeight = fb.height();

    fb.clear(kBg);
    fb.drawRect({0, 0, fb.width(), fb.height()}, kBorder, 1);

    drawSection(engine, fb);   // primero el contenido...
    drawMenu(engine, fb);      // ...y encima el menu, que no debe taparse
    drawAvatar(fb);            // avatar siempre visible, en cualquier seccion
    drawInstallBar(fb);        // barra de instalacion, visible en toda seccion

    // El editor va por encima de todo menos de la barra de titulo.
    if (m_descOpen) drawDescriptionEditor(engine, fb);

    // Constancia de quien tiene la sesion abierta. En la seccion de paneles
    // va centrado: pegado a un borde quedaria bajo una franja roja.
    if (!m_email.empty()) {
        const i32 w = font5x7::measure(m_email.c_str(), 1);
        const i32 x = (m_section == kPanelsSection)
            ? (fb.width() - w) / 2
            : fb.width() - w - 12;
        font5x7::drawText(fb, x, fb.height() - 22, m_email.c_str(), kDim, 1);
    }

    m_titleBar.render(engine, fb);
}

} // namespace ludora
