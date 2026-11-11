#pragma once
#include "core/Types.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace ludora {

/// Paquete ".tomate" v3: empaquetado, compresion y cifrado EN FLUJO.
///
/// POR QUE EXISTE ESTO
/// La version anterior hacia el paquete entero en memoria: leia el juego
/// completo a un buffer, lo comprimia a un segundo buffer y lo cifraba a un
/// tercero. Tres copias del juego en RAM a la vez. Medido en esta maquina, un
/// juego de 2 GB mataba el proceso (necesitaba ~6 GB), y por eso habia un
/// tope artificial de 4 GB y un aviso de "memoria insuficiente" que al autor
/// no le resolvia nada.
///
/// Aqui el paquete se construye por BLOQUES de tamano fijo. Cada bloque se
/// comprime y se cifra por separado y se escribe a disco en cuanto esta, asi
/// que la memoria que se usa no depende del tamano del juego: depende del
/// numero de bloques en vuelo, que es fijo. Un juego de 100 GB se guarda con
/// la misma RAM que uno de 100 MB.
///
/// QUE SE GANA, EN CONCRETO
///  - Memoria constante (unas decenas de MB), no 2.2 veces el juego.
///  - Sin tope de tamano: las longitudes del indice son de 64 bits.
///  - Velocidad: los bloques se comprimen y cifran en PARALELO, un hilo por
///    nucleo, mientras otro hilo escribe. Antes todo era secuencial y de un
///    solo hilo.
///  - Mejor compresion: se elige el mejor compresor que ofrezca el sistema
///    (XPRESS_HUFF si esta, LZNT1 si no), y por bloque se guarda crudo si
///    comprimir no gana nada -- lo normal en texturas y audio.
///
/// SEGURIDAD
/// El estirado caro (Argon2id) se hace UNA vez por paquete. De su salida se
/// derivan las subclaves por bloque con BLAKE2b con clave, que es practicamente
/// gratis. Hacer el estirado por bloque no anadiria seguridad -- el secreto es
/// el mismo -- y costaria minutos.
///
/// Cada bloque lleva su propio tag AES-GCM: alterar un byte se detecta al
/// abrirlo, sin tener que descifrar el paquete entero. Y como el numero de
/// bloque y el id del paquete entran en la derivacion de cada subclave, un
/// bloque no se puede mover de sitio, duplicar ni traer de otro paquete: su
/// clave no cuadraria. El indice va cifrado y autenticado igual que los datos,
/// de modo que los nombres de archivo tampoco quedan a la vista.
namespace tomatepack {

/// Tamano de bloque. 8 MiB es el punto donde la compresion ya rinde bien y el
/// trabajo por hilo sigue siendo pequeno; con bloques diminutos se pierde
/// ratio, y con bloques enormes vuelve el problema de la memoria.
constexpr u64 kBlockBytes = 8ull * 1024 * 1024;

/// Compresores posibles, por orden de preferencia. Lo que se usa de verdad se
/// decide en tiempo de ejecucion y se anota en cada bloque, asi que un paquete
/// escrito en un Windows se abre en otro aunque no ofrezca el mismo compresor.
/// Compresores posibles. Cada BLOQUE anota el suyo, asi que un paquete puede
/// mezclarlos: el que mejor le vaya a cada trozo de datos.
enum class Codec : u32 {
    None       = 0,   // crudo: comprimir no ganaba nada (texturas, audio)
    Lznt1      = 1,   // respaldo para Windows sin XPRESS
    XpressHuff = 2,   // mejor ratio (42%), descomprime a ~116 MB/s
    Xpress     = 3,   // ratio medio (51%), descomprime a ~665 MB/s
};

/// Resultado de medir que puede hacer este sistema. Util para el diagnostico.
struct Caps {
    bool xpressHuff = false;   // compresor bueno disponible
    u32  threads    = 1;       // hilos que se usaran
};
Caps caps();

/// Empaqueta, comprime y cifra `srcDir` entero en `destPath`.
///
/// `progress` (opcional) recibe 0..1. `cancel` (opcional) se consulta a menudo:
/// si pasa a true, el guardado se aborta sin dejar un paquete a medias.
/// Devuelve false y llena `error` con un mensaje para el autor.
bool sealFolder(const std::wstring& srcDir,
                const std::wstring& destPath,
                const std::string&  secret,
                std::atomic<float>* progress,
                std::string&        error,
                const std::atomic<bool>* cancel = nullptr);

/// Revisa un bloque ya descifrado, MIENTRAS se instala.
///
/// Devolver false aborta la extraccion: es como se corta una descarga en la
/// que se ha encontrado algo, sin esperar a que termine de escribirse.
/// `motivo` explica por que, para podérselo decir al usuario.
using Inspector = std::function<bool(const u8* datos, size_t n,
                                     std::string& motivo)>;

/// Deshace sealFolder() sobre `destDir`.
///
/// Verifica cada bloque antes de escribir nada suyo, asi que un paquete
/// alterado se detecta en vez de instalarse a medias.
///
/// `inspector` (opcional) ve cada bloque ya descifrado ANTES de que llegue al
/// disco. Es lo que permite analizar el juego mientras se instala en vez de
/// hacer una segunda pasada al final: los datos ya estan en memoria y
/// descomprimidos, asi que mirarlos aqui no cuesta ni una lectura mas.
bool unsealFolder(const std::wstring& srcPath,
                  const std::wstring& destDir,
                  const std::string&  secret,
                  std::atomic<float>* progress,
                  std::string&        error,
                  const std::atomic<bool>* cancel = nullptr,
                  const Inspector&         inspector = Inspector());

/// true si el archivo es un paquete v3 (empieza por su marca). Sirve para
/// decidir si abrirlo con este codigo o con el lector del formato viejo.
bool isV3(const std::wstring& path);

/// Comprueba la integridad de un paquete SIN extraerlo: descifra y verifica
/// todos los bloques y los descarta. Es lo que permite decirle al autor "el
/// paquete esta bien" sin gastar disco.
bool verify(const std::wstring& srcPath,
            const std::string&  secret,
            std::atomic<float>* progress,
            std::string&        error);

/// Lo que contiene un paquete, leido solo del indice (rapido: no descifra los
/// datos). `files` y `bytes` son el total original, antes de comprimir.
struct Info {
    u64 files        = 0;
    u64 bytes        = 0;   // tamano original del contenido
    u64 packedBytes  = 0;   // lo que ocupa el paquete en disco
    u64 blocks       = 0;
};
bool info(const std::wstring& srcPath, const std::string& secret,
          Info& out, std::string& error);

} // namespace tomatepack
} // namespace ludora
