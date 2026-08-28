#pragma once
#include "core/Types.h"
#include <string>

namespace ludora {

/// Identidad del dispositivo, para reconocerlo sin pedir credenciales otra vez.
///
/// PRIVACIDAD -- lo que este modulo NO hace:
///   - No consulta ni almacena ubicacion (ni GPS, ni IP, ni zona horaria).
///   - No guarda el nombre del equipo, del usuario ni numeros de serie.
///   - No envia nada por red: todo queda en el disco local.
///
/// Lo unico que se persiste es:
///   - Un HASH del identificador de maquina (SHA-256 con salt). Del hash no se
///     puede recuperar el dato de origen, y solo sirve para comparar "es el
///     mismo equipo, si o no".
///   - Una etiqueta generica del TIPO de dispositivo ("Escritorio", "Portatil"),
///     que no distingue un equipo de otro.
class Device {
public:
    /// Categoria amplia del equipo. Deliberadamente gruesa: describe la clase
    /// de dispositivo, nunca una unidad concreta.
    enum class Kind { Desktop, Laptop, Tablet, Unknown };

    static Kind        kind();
    static const char* kindName(Kind k);

    /// Huella estable del equipo, ya combinada con `salt` y pasada por SHA-256.
    /// El material de origen (identificadores del sistema) no sale de aqui:
    /// se usa para calcular y se descarta.
    static std::string fingerprint(const std::string& salt);

    /// Compara una huella recien calculada con otra guardada, en tiempo
    /// constante para no filtrar informacion por el tiempo de respuesta.
    static bool matches(const std::string& salt, const std::string& storedHash);
};

} // namespace ludora
