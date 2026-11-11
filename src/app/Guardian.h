#pragma once
#include "core/Types.h"
#include <atomic>
#include <string>
#include <vector>

namespace ludora {

/// Revision de seguridad de los juegos que se descargan ("Guardian").
///
/// QUE ES Y QUE NO ES, SIN ADORNOS
/// Esto NO es un antivirus propio. Escribir un motor de firmas de verdad
/// -- y, sobre todo, mantenerlo actualizado a diario -- es el trabajo de una
/// empresa entera, y uno hecho a medias es peor que ninguno: da una sensacion
/// de seguridad que no corresponde a nada.
///
/// Lo que hace es DOS cosas, y las dos son reales:
///
/// 1. Le pasa el contenido al antivirus que ya tiene el equipo, a traves de
///    AMSI (Antimalware Scan Interface, la interfaz de Windows para esto). Si
///    hay Defender o cualquier otro antivirus instalado, es EL quien analiza,
///    con sus firmas de verdad y actualizadas. Asi el analisis es tan bueno
///    como el antivirus del usuario, sin inventarse nada.
///
/// 2. Anade comprobaciones que un antivirus generico NO hace, porque son
///    propias de una plataforma de juegos: un paquete que trae ejecutables
///    donde no deberia, scripts de instalacion, rutas que se salen de su
///    carpeta, o un .exe disfrazado de imagen. Eso no es malware por firma,
///    pero en un juego subido por un desconocido es justo lo que hay que mirar.
///
/// Si el equipo no tiene antivirus o AMSI no esta disponible, se dice
/// claramente en el resultado en vez de callarlo: el usuario tiene que poder
/// saber que la parte de firmas no se hizo.
namespace guardian {

/// Gravedad de un hallazgo.
enum class Nivel : i32 {
    Info    = 0,   // digno de mencion, no preocupante
    Aviso   = 1,   // sospechoso: que lo decida el usuario
    Peligro = 2,   // se bloquea la instalacion
};

/// Un hallazgo concreto, con el archivo al que se refiere.
struct Hallazgo {
    Nivel        nivel = Nivel::Info;
    std::string  motivo;      // que se encontro, en lenguaje llano
    std::wstring archivo;     // ruta relativa dentro del juego
};

/// Resultado de revisar un juego.
struct Informe {
    bool                  hecho       = false;  // la revision termino
    bool                  motorFirmas = false;  // habia antivirus que consultar
    std::string           motorNombre;          // cual respondio, si se supo
    i32                   archivos    = 0;      // cuantos se revisaron
    u64                   bytes       = 0;
    f64                   segundos    = 0.0;
    std::vector<Hallazgo> hallazgos;

    /// true si hay algo de nivel Peligro: la instalacion no debe seguir.
    bool bloqueado() const;
    /// El nivel mas alto encontrado.
    Nivel peor() const;
    /// Resumen de una linea para la interfaz.
    std::string resumen() const;
};

/// Revisa una carpeta de juego ya extraida.
///
/// `progress` (opcional) recibe 0..1. `cancel` (opcional) corta la revision.
/// Siempre devuelve un informe: que no se pueda analizar no es lo mismo que
/// que este limpio, y el informe lo distingue.
Informe revisarCarpeta(const std::wstring& dir,
                       std::atomic<float>* progress = nullptr,
                       const std::atomic<bool>* cancel = nullptr);

/// Revisa un bloque de datos SUELTO, mientras se descarga.
///
/// Esto es lo que permite analizar "mientras descarga" en vez de esperar al
/// final: el paquete se revisa a medida que sus bloques se descifran, asi que
/// cuando termina de instalarse ya esta revisado y no hay una segunda espera.
///
/// `nombreVisible` solo sirve para el mensaje del hallazgo.
bool revisarBloque(const u8* datos, size_t n,
                   const std::wstring& nombreVisible,
                   std::vector<Hallazgo>& hallazgosOut);

/// true si el equipo tiene un antivirus al que consultar por AMSI.
bool motorDisponible();

/// Nombre del motor de firmas que responde, si se puede averiguar.
std::string nombreMotor();

/// Suelta los recursos de AMSI. Se llama al cerrar; no es obligatorio.
void cerrar();

} // namespace guardian
} // namespace ludora
