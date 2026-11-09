#pragma once
#include "core/Types.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace ludora {

/// Instala un juego copiando su carpeta a un directorio propio de Ludora y
/// dejando constancia en la biblioteca. La copia corre en un hilo aparte para
/// no congelar la interfaz, y publica el progreso 0..1 de forma atomica.
class GameInstall {
public:
    ~GameInstall();

    /// Arranca la copia de la carpeta que contiene `sourceExe` hacia
    /// `destDir`. No bloquea: el progreso se consulta con progress().
    /// El nombre visible del juego es `name`, el ejecutable `exeName`.
    bool start(const std::wstring& sourceExe,
               const std::wstring& destDir,
               const std::string&  name,
               const std::wstring& exeName);

    bool  running()  const { return m_running.load(); }
    bool  done()     const { return m_done.load(); }
    bool  failed()   const { return m_failed.load(); }
    f32   progress() const { return m_progress.load(); }   // 0..1
    const std::string& error() const { return m_error; }

    /// Ruta prevista del ejecutable tras descifrar (valida tras done()).
    std::wstring installedExe() const { return m_installedExe; }
    /// Paquete cifrado (.tomate) que contiene el juego.
    std::wstring vaultPath() const    { return m_vaultPath; }

    void join();

private:
    /// Envoltorio del hilo: captura cualquier excepcion para que no mate el
    /// proceso, y la comunica como un fallo de instalacion.
    void worker(std::wstring sourceExe, std::wstring destDir, std::wstring exeName);
    /// El trabajo de verdad. Puede lanzar; worker() se encarga.
    void workerBody(std::wstring sourceExe, std::wstring destDir,
                    std::wstring exeName);

    std::thread        m_thread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_done{false};
    std::atomic<bool>  m_failed{false};
    std::atomic<float> m_progress{0.0f};
    std::string        m_error;
    std::wstring       m_installedExe;
    std::wstring       m_vaultPath;   // paquete cifrado del juego
    std::wstring       m_exeName;     // nombre del exe dentro del paquete
};

/// Empaqueta y cifra una carpeta de juego para guardarla en la Nube Tomate.
/// El cifrado es el del Vault (Argon2id + 10 capas AES); el secreto es la
/// huella de este equipo, asi que el paquete solo se abre aqui.
namespace gamevault {

/// true si un archivo (ruta relativa) es un DATO sensible del jugador (se
/// cifra). Los assets -- exe, texturas, sonidos, dll -- devuelven false y se
/// dejan en claro para que el juego los ejecute directo.
bool isSensitiveData(const std::wstring& rel);

/// Localiza la carpeta RAIZ del juego a partir de su `.exe`, subiendo por el
/// arbol hasta hallar sus recursos (resourcepacks, assets...). Devuelve la raiz
/// y, en `exeRelOut`, la ruta del .exe relativa a esa raiz. Si no encuentra
/// recursos, devuelve la carpeta del .exe. Es lo que hay que INSTALAR para que
/// el juego encuentre sus texturas.
std::wstring findGameRoot(const std::wstring& sourceExe, std::wstring& exeRelOut);

/// Cifra toda la carpeta de `sourceExe` en un unico archivo `destVault`.
/// Publica el progreso 0..1 por `progress` (puntero atomico opcional).
bool sealFolder(const std::wstring& sourceExe,
                const std::wstring& destVault,
                std::atomic<float>* progress,
                std::string& error);

/// Cifra una carpeta ENTERA (y todo su arbol) en un unico paquete. Igual que
/// sealFolder, pero recibe la carpeta directamente en vez de deducirla de un
/// `.exe`: es lo que hace falta al GUARDAR los archivos de un juego que se va
/// a publicar, donde lo que el autor elige es la raiz del proyecto.
bool sealWholeFolder(const std::wstring& srcDir,
                     const std::wstring& destVault,
                     std::atomic<float>* progress,
                     std::string& error);

/// Extrae un paquete creado por sealWholeFolder sobre `destDir`.
///
/// Es el inverso exacto, y usa el mismo secreto de DISTRIBUCION: a diferencia
/// de los vaults de datos del jugador, este paquete se abre en cualquier
/// equipo. Sin eso, un juego publicado solo podria instalarlo su autor.
bool unsealWholeFolder(const std::wstring& srcVault,
                       const std::wstring& destDir,
                       std::string& error);

/// Cuenta los archivos de un arbol y suma su tamano. Sirve para decirle al
/// autor QUE se va a guardar antes de guardarlo, y para avisar si la carpeta
/// esta vacia -- un paquete sin archivos no es un juego.
bool measureFolder(const std::wstring& srcDir, i32& filesOut, u64& bytesOut);

/// Cifra SOLO los datos sensibles de la carpeta (partidas, config). Los
/// assets se copian en claro aparte, no van aqui.
bool sealDataOnly(const std::wstring& sourceExe,
                  const std::wstring& destVault,
                  std::atomic<float>* progress,
                  std::string& error);

/// Cifra una lista explicita de datos sensibles. `get(i, src, rel)` entrega la
/// ruta de origen en disco y la ruta relativa que tendra al restaurar. Sirve
/// cuando los datos vienen de varias carpetas (raiz del exe + recursos).
bool sealDataItems(size_t n,
                   const std::function<void(size_t, std::wstring&, std::wstring&)>& get,
                   const std::wstring& destVault,
                   std::atomic<float>* progress,
                   std::string& error);

/// Descifra los datos sensibles SOBRE la carpeta ya instalada (assets en
/// claro). Deja el juego completo y jugable. No falla si no habia datos.
bool restoreData(const std::wstring& srcVault, const std::wstring& destDir,
                 std::string& error);

/// LIBERA un juego instalado: descifra sus datos sobre la carpeta y borra el
/// paquete cifrado, dejando todos los archivos en claro y modificables.
///
/// Es lo que ocurre cuando el creador retira el juego de la tienda. El
/// razonamiento: quien lo compro ya es dueno de su copia, asi que retirarlo
/// de la venta no puede dejarle con archivos que no puede abrir. A cambio,
/// pierde la proteccion del cifrado -- que es justo el punto: pasa a poder
/// modificarlo a su antojo.
bool releaseGame(const std::wstring& installedExe, const std::wstring& vaultPath,
                 std::string& error);

/// Descifra un paquete a una carpeta temporal y devuelve la ruta del exe.
/// `exeName` es el nombre del ejecutable dentro del paquete.
bool openToFolder(const std::wstring& srcVault,
                  const std::wstring& destDir,
                  const std::wstring& exeName,
                  std::wstring& outExe,
                  std::string& error);

} // namespace gamevault

/// GUARDA los archivos de un juego que se va a publicar: empaqueta y cifra la
/// carpeta entera del proyecto en un `.tomate` que vive dentro de Ludora.
///
/// Es la pieza que convierte "he apuntado una ruta de mi PC" en "el juego esta
/// de verdad dentro de la plataforma". Sin esto, publicar solo guardaba la
/// ruta del autor: nadie mas podia descargar nada, porque los archivos no
/// habian salido de su disco.
///
/// Corre en un hilo aparte -- sellar un proyecto de varios cientos de megas
/// tarda -- y publica el progreso 0..1 de forma atomica, igual que GameInstall.
class GameArchive {
public:
    ~GameArchive();

    /// Arranca el guardado de `srcDir` hacia `destVault`. No bloquea.
    bool start(const std::wstring& srcDir, const std::wstring& destVault);

    bool  running()  const { return m_running.load(); }
    bool  done()     const { return m_done.load(); }
    bool  failed()   const { return m_failed.load(); }
    f32   progress() const { return m_progress.load(); }   // 0..1
    const std::string& error() const { return m_error; }

    /// Paquete resultante (valido tras done() sin fallo).
    std::wstring vaultPath() const { return m_vaultPath; }
    /// Cuantos archivos y cuantos bytes se guardaron.
    i32 fileCount() const { return m_files; }
    u64 byteCount() const { return m_bytes; }

    void join();

private:
    void worker(std::wstring srcDir, std::wstring destVault);
    void workerBody(std::wstring srcDir, std::wstring destVault);

    std::thread        m_thread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_done{false};
    std::atomic<bool>  m_failed{false};
    std::atomic<float> m_progress{0.0f};
    std::string        m_error;
    std::wstring       m_vaultPath;
    i32                m_files = 0;
    u64                m_bytes = 0;
};

/// Registro de juegos instalados (biblioteca). Persistente en disco.
namespace library {

struct Entry {
    std::string  name;    // nombre visible
    std::wstring exe;     // exe ya instalado en claro ("" si esta cifrado)
    std::wstring vault;   // paquete cifrado .tomate ("" si esta en claro)
    std::wstring exeName; // nombre del exe dentro del paquete cifrado
};

/// Descifra el juego (si esta cifrado) y lo lanza. `tempDir` es donde se
/// descifra. Si `outProc` no es nulo, recibe el handle del proceso lanzado
/// (el llamante lo cierra con CloseHandle). Devuelve false y llena `error`.
bool launchEntry(const Entry& e, const std::wstring& tempDir, std::string& error,
                 void** outProc = nullptr);

/// Carga la lista de juegos instalados.
std::vector<Entry> load(const std::wstring& path);

/// Anade un juego (o actualiza si ya existe por nombre) y guarda.
bool add(const std::wstring& path, const Entry& e);

/// true si ya hay un juego con ese nombre en el registro.
bool has(const std::wstring& path, const std::string& name);

/// true si la entrada esta REALMENTE instalada (su .exe existe en disco). Un
/// registro que apunta a un .exe ausente no cuenta como instalado.
bool entryInstalled(const Entry& e);

/// Quita un juego del registro por nombre. Devuelve true si lo elimino.
bool remove(const std::wstring& path, const std::string& name);

/// Borra del disco TODOS los archivos de un juego instalado: su carpeta
/// entera y el paquete cifrado de datos que vive al lado.
///
/// Es destructivo y no se puede deshacer, de ahi que la interfaz pregunte
/// antes. Devuelve el numero de archivos borrados; 0 no es un fallo (puede
/// que ya no quedara nada).
///
/// Por seguridad NO borra si la carpeta deducida es una raiz de unidad o un
/// directorio del sistema: un registro corrupto no puede llevarse por delante
/// medio disco.
i32 deleteFiles(const Entry& e, std::string& error);

/// Lanza el ejecutable de un juego instalado.
bool launch(const std::wstring& exe);

} // namespace library

/// Catalogo del desarrollador: juegos que ESTE usuario ha subido, con su
/// historial de versiones y su precio.
///
/// Es un registro aparte de `library` a proposito: `library` son los juegos que
/// el usuario tiene INSTALADOS para jugar, esto son los que PUBLICA. Un mismo
/// equipo puede tener ambas cosas sin que una interfiera con la otra.
namespace devcatalog {

/// Una version publicada de un juego. Las versiones se guardan en orden de
/// subida: la primera del vector es la mas antigua, la ultima es la vigente.
struct Version {
    std::string  label;      // etiqueta visible ("v1", "v2"...)
    std::wstring exe;        // ejecutable de origen de esta version
    long long    uploadedAt = 0;   // epoch en segundos
    /// Notas publicas de lo que cambia esta version. Es lo que leen los
    /// jugadores en la tienda: que se arreglo, que se anadio.
    std::string  changelog;

    /// Paquete `.tomate` con TODOS los archivos del juego, ya dentro de
    /// Ludora. Vacio si el autor aun no los ha guardado.
    ///
    /// `exe` es una ruta en el PC del autor y no sirve para distribuir nada;
    /// esto si: es la copia que se descarga quien compra el juego. Que sean
    /// dos campos distintos es a proposito -- el autor sigue trabajando sobre
    /// su carpeta, y lo publicado es la foto que guardo.
    std::wstring archive;
    i32          archiveFiles = 0;   // cuantos archivos lleva dentro
    u64          archiveBytes = 0;   // cuanto ocupaban sin cifrar
};

/// Precio de un juego. Se guarda en centavos de dolar para no arrastrar
/// errores de coma flotante en dinero: 400 USD son 40000 centavos exactos.
struct Price {
    i32 usdCents = 0;   // 0 = gratis
};

/// Ficha de publicacion: lo que el formulario SUBIR JUEGO rellena sobre el
/// juego, aparte de su nombre y su precio.
///
/// Va en su propia estructura para que `Game` no se convierta en una lista
/// plana de veinte campos sueltos, y para poder pasarla entera de la interfaz
/// al catalogo sin enumerar argumento por argumento.
struct Listing {
    std::string description;   // hasta 1000 caracteres
    std::string genres;        // etiquetas "#ACCION #RPG", tal y como se editan
    std::string multiplayer;   // etiquetas "@ONLINE @LAN"
    i32  fps       = 60;       // rendimiento de fabrica [24..2000]
    /// Franja de edad recomendada, [4..90]. El publico al que apunta el juego
    /// no es un numero sino un tramo: "de 12 a 16" dice algo que "12 o mas"
    /// no dice.
    ///
    /// `maxAge` se anadio despues de `minAge`. Una ficha vieja no lo trae, y
    /// al cargarla se le pone kMaxAge: antes la edad era un minimo abierto
    /// por arriba, asi que ese es su valor equivalente y ningun juego ya
    /// publicado cambia de significado.
    i32  minAge    = 4;
    i32  maxAge    = 90;
    /// Permisos: 0 sin contestar, 1 aceptado, 2 rechazado. Se guarda el "sin
    /// contestar" a proposito -- no es lo mismo que haber rechazado, y de cara
    /// a publicar conviene poder exigir una respuesta explicita.
    i32  piracy    = 0;
    i32  customize = 0;
    std::wstring art;          // imagen promocional de la tienda ("" si no hay)
    std::wstring source;       // codigo subido: archivo o carpeta raiz

    /// EULA propio: las reglas que el desarrollador impone a sus jugadores,
    /// en tres ambitos. Van separadas y no en un solo texto porque las reglas
    /// de un servidor no aplican a quien juega solo, y mezclarlas obligaria a
    /// leerlo todo para saber que te toca.
    std::string eulaLocal;     // Local / Un jugador
    std::string eulaMulti;     // Multijugador / Servidores
    std::string eulaMods;      // Comunidad y moding
};

/// Un juego publicado por el desarrollador.
struct Game {
    std::string          name;
    std::vector<Version> versions;   // [0] la mas vieja, [n-1] la vigente
    Price                price;
    Listing              listing;    // ficha rellenada en SUBIR JUEGO

    /// Codigo del juego: "#" y diez caracteres. Se genera al publicarlo y no
    /// cambia nunca.
    ///
    /// Sirve para ENCONTRAR el juego sin escribir su nombre exacto: el autor
    /// copia el codigo del catalogo y lo pega al actualizar. Un nombre se
    /// escribe mal, cambia de mayusculas o se renombra; el codigo no.
    ///
    /// Vacio en los juegos publicados antes de que esto existiera. `load()`
    /// les asigna uno al leerlos, derivado de su nombre, para que tambien
    /// aparezcan en el catalogo de codigos.
    std::string code;

    /// Huella de la ultima REVISION: cuantos archivos y carpetas tenia el
    /// juego y cuanto ocupaban. Es contra esto que se compara la siguiente.
    ///
    /// Cero en los juegos nunca revisados, que es como se distingue la
    /// primera revision (no hay con que comparar) de las siguientes.
    i32  revFiles = 0;
    i32  revDirs  = 0;
    u64  revBytes = 0;
    long long revAt = 0;   // cuando se reviso, epoch en segundos

    /// Version vigente (la ultima subida), o nullptr si no hay ninguna.
    const Version* current() const {
        return versions.empty() ? nullptr : &versions.back();
    }
};

/// Codigo de un juego a partir de su nombre: "#" y diez caracteres.
///
/// Es DETERMINISTA: el mismo nombre da siempre el mismo codigo. Tiene que
/// serlo, porque los juegos publicados antes de que existieran los codigos lo
/// reciben al cargarse, y si cambiara entre arranques el que el autor copio
/// ayer dejaria de servirle hoy.
std::string makeGameCode(const std::string& name);

/// Codigo ALEATORIO de 15 caracteres, con '#' delante.
///
/// A diferencia de makeGameCode, este no se deriva de nada: se sortea con
/// aleatoriedad del sistema. Es el que crea GENERAR CODIGO, para un juego
/// que todavia no tiene nombre.
///
/// Comprueba el catalogo para no repetir uno ya usado.
std::string randomGameCode(const std::wstring& catalogPath);

/// Busca un juego por su CODIGO. Devuelve su nombre, o "" si no existe.
///
/// La comparacion ignora mayusculas y espacios sobrantes, y acepta el codigo
/// con o sin el '#': quien lo pega de un catalogo no deberia tener que
/// acertar con el formato exacto.
std::string findByCode(const std::wstring& path, const std::string& code);

/// Resultado de una revision de archivos.
struct Revision {
    bool primera = true;    // no habia revision anterior con que comparar
    i32  files = 0,  dirs = 0;        // lo que hay ahora
    u64  bytes = 0;
    i32  dFiles = 0, dDirs = 0;       // diferencia frente a la anterior
    long long dBytes = 0;
    bool cambios = false;   // algo cambio respecto a la revision anterior
};

/// Revisa la carpeta de un juego y la compara con su revision anterior.
///
/// La PRIMERA vez no hay con que comparar: solo toma la huella y lo dice. A
/// partir de la segunda informa de cuantos archivos y carpetas se anadieron o
/// desaparecieron y cuanto cambio el tamano.
///
/// Guarda la huella nueva en el catalogo, asi que la siguiente revision
/// compara contra esta.
bool reviewGame(const std::wstring& path, const std::string& name,
                const std::wstring& folder, Revision& out, std::string& error);

/// Carga el catalogo del desarrollador.
std::vector<Game> load(const std::wstring& path);

/// Guarda el catalogo completo, sobrescribiendo.
bool save(const std::wstring& path, const std::vector<Game>& games);

/// Registra un juego nuevo con su primera version. Falla si ya existe uno con
/// ese nombre -- para anadir una version a un juego existente esta addVersion.
bool publish(const std::wstring& path, const std::string& name,
             const std::wstring& exe, std::string& error);

/// Igual que publish(), pero guardando ademas la ficha del formulario. Es la
/// via que usa SUBIR JUEGO; la de arriba se mantiene para quien solo quiera
/// registrar nombre y ejecutable.
bool publishWithListing(const std::wstring& path, const std::string& name,
                        const std::wstring& exe, const Listing& listing,
                        std::string& error);

/// Actualiza la ficha de un juego ya publicado, sin tocar sus versiones ni su
/// precio. Devuelve false si ese juego no existe.
bool setListing(const std::wstring& path, const std::string& name,
                const Listing& listing, std::string& error);

/// Anota en la version VIGENTE que sus archivos ya estan guardados dentro de
/// Ludora: la ruta del paquete y lo que contiene.
///
/// Si el juego aun no esta publicado, se guarda igual como pendiente: el autor
/// puede guardar los archivos antes de rellenar la ficha, y de hecho es el
/// orden natural. La interfaz lo recupera con pendingArchive().
bool setArchive(const std::wstring& path, const std::string& name,
                const std::wstring& archive, i32 files, u64 bytes,
                std::string& error);

/// true si la version vigente de ese juego tiene sus archivos guardados.
bool hasArchive(const std::wstring& path, const std::string& name);

/// Archivos guardados para un juego que AUN NO esta publicado.
///
/// Existe para romper un circulo: guardar exige saber a que juego pertenecen
/// los archivos, y publicar exige tenerlos ya guardados. Sin esto, un juego
/// nuevo no podria pasar por ninguno de los dos pasos. Se guardan aparte y
/// `publish` los adopta en cuanto el juego existe.
struct Pending {
    std::string  game;    // nombre al que pertenecen
    std::wstring archive; // paquete
    i32          files = 0;
    u64          bytes = 0;
};

/// Codigo RESERVADO para un juego que todavia no se ha publicado.
///
/// Existe para que el catalogo de codigos sirva desde el primer dia: sin
/// esto, un autor que aun no ha subido nada ve una lista vacia y no tiene
/// codigo que pegar en GUARDAR ARCHIVOS.
///
/// Al publicar un juego con ese nombre, `publish` adopta la reserva en vez
/// de generar un codigo nuevo -- asi el que el autor ya copio le sigue
/// valiendo.
struct Reserva {
    std::string game;   // nombre previsto del juego
    std::string code;   // su codigo
    long long   at = 0; // cuando se reservo, epoch en segundos
};

/// Reserva un codigo para un nombre que aun no esta publicado.
///
/// Falla si el nombre esta vacio, si ya hay un juego publicado con ese
/// nombre (ese ya tiene codigo) o si el nombre ya estaba reservado.
bool reserveCode(const std::wstring& path, const std::string& name,
                 std::string& codeOut, std::string& error);

/// Todas las reservas vigentes, las mas recientes primero.
std::vector<Reserva> reservations(const std::wstring& path);

/// Olvida la reserva de ese nombre. Se llama al publicarlo.
void clearReservation(const std::wstring& path, const std::string& name);

/// Anota un paquete pendiente (sustituye al anterior del mismo juego).
bool setPending(const std::wstring& path, const Pending& p, std::string& error);
/// Devuelve el pendiente de ese juego, o un Pending vacio si no hay.
Pending getPending(const std::wstring& path, const std::string& name);
/// Olvida el pendiente de ese juego.
void clearPending(const std::wstring& path, const std::string& name);

/// Limites de los campos numericos de la ficha. Viven aqui, junto al modelo,
/// para que la interfaz y la validacion no puedan discrepar.
constexpr i32 kMinFps = 24;
constexpr i32 kMaxFps = 2000;
constexpr i32 kMinAge = 4;
constexpr i32 kMaxAge = 90;

/// Anade una version NUEVA a un juego ya publicado. La anterior se conserva en
/// el historial y esta pasa a ser la vigente: es lo que hace que "la vieja se
/// actualice a la nueva" sin perder de vista cual habia antes.
bool addVersion(const std::wstring& path, const std::string& name,
                const std::wstring& exe, std::string& error);

/// Igual, pero guardando ademas las notas publicas de la version.
bool addVersionWithLog(const std::wstring& path, const std::string& name,
                       const std::wstring& exe, const std::string& changelog,
                       std::string& error);

/// Resultado de comparar como esta organizada una version nueva frente a la
/// que ya estaba publicada.
///
/// Sirve para avisar ANTES de subir: si el arbol de archivos cambio de forma
/// radical, quien tenga el juego instalado puede perder sus partidas, porque
/// los datos guardados dejan de encontrarse donde estaban.
struct LayoutCheck {
    bool        compatible = true;  // false si conviene avisar
    std::string reason;             // que cambio, en lenguaje llano
    i32         shared   = 0;       // carpetas y extensiones que coinciden
    i32         onlyOld  = 0;       // las que solo estaban en la vieja
    i32         onlyNew  = 0;       // las que solo estan en la nueva
};

/// Compara la organizacion de archivos de dos versiones. `oldExe` y `newExe`
/// pueden ser un ejecutable (se mira su carpeta) o una carpeta directamente.
/// Si la vieja no existe o no se puede leer, devuelve compatible = true: sin
/// referencia con la que comparar, no hay nada de lo que avisar.
LayoutCheck checkLayoutCompatible(const std::wstring& oldExe,
                                  const std::wstring& newExe);

/// Quita un juego del catalogo: deja de estar en la tienda y nadie mas puede
/// descargarlo. NO toca las bibliotecas de quien ya lo tiene.
bool remove(const std::wstring& path, const std::string& name,
            std::string& error);

/// Fija el precio de un juego publicado.
bool setPrice(const std::wstring& path, const std::string& name,
              const Price& price, std::string& error);

/// Formatea un precio en las tres monedas que muestra la interfaz.
/// `usdCents` = 0 produce "GRATIS" en las tres.
void formatPrice(i32 usdCents, std::string& usd, std::string& mxn, std::string& eur);

/// Precio maximo del deslizador, en centavos de dolar (400.00 USD).
constexpr i32 kMaxUsdCents = 40000;

} // namespace devcatalog

/// Resenas de los jugadores sobre un juego.
///
/// Es un registro propio y no parte de `devcatalog` a proposito: el catalogo
/// describe lo que el creador PUBLICA, y esto es lo que opinan los demas. Un
/// juego puede tener resenas de versiones que ya no son la vigente, asi que
/// cada una guarda a que version se refiere.
namespace reviews {

/// Una resena. La puntuacion va de 1 a 5 estrellas; 0 significa que se
/// escribio sin puntuar, que es distinto de puntuar con la nota minima.
struct Review {
    std::string game;       // juego resenado
    std::string version;    // version a la que se refiere ("" = la vigente)
    std::string author;     // correo de quien la escribe
    std::string text;
    i32         stars = 0;  // 1..5, o 0 si no puntuo
    long long   at    = 0;  // epoch en segundos
};

/// Resumen de un juego, para ordenar la tienda por valoracion.
struct Summary {
    i32 count   = 0;    // cuantas resenas tiene
    i32 sumStars= 0;    // suma de las puntuaciones (solo las que puntuaron)
    i32 rated   = 0;    // cuantas de ellas puntuaron

    /// Media de estrellas en centesimas (450 = 4.50), o 0 si nadie puntuo.
    /// Se devuelve en enteros para no arrastrar coma flotante al ordenar.
    i32 averageX100() const {
        return rated > 0 ? (sumStars * 100) / rated : 0;
    }
};

/// Carga todas las resenas guardadas.
std::vector<Review> load(const std::wstring& path);

/// Guarda la lista completa, sobrescribiendo.
bool save(const std::wstring& path, const std::vector<Review>& all);

/// Anade una resena. Falla si el texto esta vacio o las estrellas se salen
/// del rango.
bool add(const std::wstring& path, const Review& r, std::string& error);

/// Resenas de un juego, de la mas reciente a la mas antigua.
std::vector<Review> forGame(const std::wstring& path, const std::string& game);

/// Resumen de valoracion de un juego.
Summary summaryFor(const std::wstring& path, const std::string& game);

/// Resumen de TODOS los juegos con resenas, indexado por nombre. Una sola
/// lectura del archivo: ordenar la tienda por valoracion pide los datos de
/// todos a la vez, y hacerlo juego a juego releeria el archivo N veces.
std::vector<std::pair<std::string, Summary>> summaryAll(const std::wstring& path);

} // namespace reviews

/// Historial de un juego EN LA BIBLIOTECA: como ha cambiado su precio y que
/// versiones ha tenido desde que se instalo.
///
/// Es un registro propio, separado de `devcatalog`: aquel describe lo que el
/// usuario PUBLICA, este lo que tiene INSTALADO. Un juego puede estar en la
/// biblioteca sin haberlo publicado nunca (bajado de la tienda), y entonces su
/// historial se alimenta de la tienda y no del catalogo de desarrollador.
namespace history {

/// Un punto de la linea de tiempo del precio.
struct PricePoint {
    i32       usdCents = 0;   // precio en ese momento (0 = gratis)
    long long at       = 0;   // epoch en segundos
};

/// Una version del juego. La ultima de la lista es la JUGABLE; las anteriores
/// quedan archivadas, con sus archivos conservados en `archivePath`.
struct VersionRecord {
    std::string  label;        // "v1", "v2"...
    long long    at = 0;       // cuando se instalo o actualizo
    std::wstring archivePath;  // copia cifrada de esta version ("" si es la viva)
    u64          bytes = 0;    // tamano de la copia, para mostrarlo
};

/// Historial completo de un juego instalado.
struct GameHistory {
    std::string                name;
    std::vector<PricePoint>    prices;     // en orden cronologico
    std::vector<VersionRecord> versions;   // [0] la mas vieja, [n-1] la jugable

    /// Version jugable ahora mismo, o nullptr si no hay ninguna.
    const VersionRecord* playable() const {
        return versions.empty() ? nullptr : &versions.back();
    }
    /// Precio vigente, o 0 (gratis) si no hay ningun punto registrado.
    i32 currentPrice() const {
        return prices.empty() ? 0 : prices.back().usdCents;
    }
    /// Precio inicial: el primero que tuvo el juego. Es la referencia contra la
    /// que se mide si ha subido o bajado.
    i32 initialPrice() const {
        return prices.empty() ? 0 : prices.front().usdCents;
    }
};

/// Carga el historial de todos los juegos.
std::vector<GameHistory> load(const std::wstring& path);

/// Guarda el historial completo, sobrescribiendo.
bool save(const std::wstring& path, const std::vector<GameHistory>& all);

/// Historial de un juego concreto. `found` dice si existia.
GameHistory forGame(const std::wstring& path, const std::string& name, bool* found = nullptr);

/// Registra el PRIMER precio de un juego al entrar en la biblioteca. Si el
/// juego ya tenia historial no hace nada: el precio inicial se fija una vez.
bool noteInitialPrice(const std::wstring& path, const std::string& name,
                      i32 usdCents, std::string& error);

/// Anota un cambio de precio. Si coincide con el vigente no anade punto, para
/// que la linea de tiempo no se llene de repeticiones sin informacion.
bool notePriceChange(const std::wstring& path, const std::string& name,
                     i32 usdCents, std::string& error);

/// Registra la version inicial de un juego recien instalado.
bool noteInitialVersion(const std::wstring& path, const std::string& name,
                        const std::wstring& archivePath, u64 bytes,
                        std::string& error);

/// Archiva la version jugable actual y anade una nueva, que pasa a ser la
/// jugable. Es el movimiento que pidio el enunciado: al actualizar, la vieja
/// se archiva y la nueva queda activa.
bool archiveAndAddVersion(const std::wstring& path, const std::string& name,
                          const std::wstring& newArchivePath, u64 newBytes,
                          std::string& error);

/// Formatea una fecha epoch como "dd/mm/aa" para la linea de tiempo.
std::string formatDate(long long epoch);

/// Formatea un tamano en bytes como "94 MB" / "812 KB".
std::string formatBytes(u64 bytes);

} // namespace history

/// Comunicados: avisos que el CREADOR de un juego publica y que leen quienes
/// lo tienen en su biblioteca.
///
/// Van aparte del changelog a proposito. El changelog dice que cambio en una
/// version y se lee en la tienda antes de comprar; un comunicado es un mensaje
/// del autor a sus jugadores -- un servidor que cierra, una disculpa, un
/// aviso de mantenimiento -- y puede no tener nada que ver con una version.
namespace notices {

/// Un comunicado. El texto NO tiene limite: el formato de guardado escapa los
/// saltos de linea, asi que un aviso de treinta parrafos ocupa una sola linea
/// en disco y se recupera intacto.
struct Notice {
    std::string  game;      // juego al que pertenece
    std::string  title;     // titular corto, es lo que se lista
    std::string  body;      // cuerpo, sin limite de longitud
    std::wstring image;     // imagen adjunta ("" si no lleva)
    long long    at = 0;    // cuando se publico, epoch en segundos
};

/// Carga todos los comunicados, del mas nuevo al mas viejo.
std::vector<Notice> load(const std::wstring& path);

/// Carga los de UN juego, del mas nuevo al mas viejo.
std::vector<Notice> forGame(const std::wstring& path, const std::string& game);

/// Publica un comunicado. El titular es obligatorio -- es lo que se lista --;
/// el cuerpo y la imagen son opcionales.
bool publish(const std::wstring& path, const Notice& n, std::string& error);

/// Cuantos comunicados tiene un juego. Es lo que la biblioteca consulta para
/// saber si pintar el aviso en el boton.
i32 countForGame(const std::wstring& path, const std::string& game);

} // namespace notices
} // namespace ludora
