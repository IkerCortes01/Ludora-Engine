#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace ludora {

/// Datos de perfil que acompanan a la cuenta: nombre visible, descripcion
/// publica y foto. Se guardan aparte de las credenciales.
///
/// El texto va en UTF-8 y los limites son en CARACTERES, no en bytes.
class Profile {
public:
    static constexpr size_t kMaxUserName    = 187;
    static constexpr size_t kMaxDescription = 10000;

    std::string userName;      // nombre de usuario
    std::string description;   // descripcion publica
    std::string photoFile;     // ruta relativa de la foto ("" si no hay)

    /// Carga desde disco. Devuelve false si no hay perfil guardado.
    static bool load(const std::wstring& path, Profile& out);
    /// Guarda en disco, sobrescribiendo.
    static bool save(const std::wstring& path, const Profile& p);

    /// Recorta a los limites por caracteres (no parte secuencias UTF-8).
    static std::string clampChars(const std::string& utf8, size_t maxChars);
};

/// Seleccion de imagen para la foto de perfil.
namespace photo {

/// Abre el dialogo estandar de Windows para elegir una imagen.
/// Devuelve "" si el usuario cancela.
std::wstring pickFromGallery(void* ownerHwnd);

/// true si hay alguna camara de video disponible en el equipo.
bool cameraAvailable();

/// Captura un fotograma de la primera camara y lo guarda como BMP en `dest`.
/// Devuelve false y llena `error` si no se pudo (sin camara, permiso denegado).
bool captureFromCamera(const std::wstring& dest, std::string& error);

/// Resultado del control de contenido de una imagen.
struct ContentCheck {
    bool        allowed = true;   // false si se rechaza
    std::string reason;           // motivo legible para el usuario
    f32         skinRatio = 0.0f; // proporcion de pixeles de tono de piel
};

/// Revisa una imagen ya decodificada antes de aceptarla como foto de perfil.
///
/// Detecta desnudez probable por analisis de tono de piel y concentracion.
/// Es un filtro de PRIMERA PASADA con limitaciones reales y conocidas:
///   - Da falsos positivos con primeros planos de cara, playa o piel clara
///     ocupando casi todo el encuadre.
///   - Da falsos negativos con desnudos parciales, vestidos o poco iluminados.
///   - NO detecta ideologias, simbolos ni opiniones: no existe forma fiable
///     de inferir eso de los pixeles, e intentarlo solo produciria un filtro
///     arbitrario e inauditable.
/// Por eso el resultado se comunica al usuario y admite revision manual,
/// en lugar de rechazar en silencio.
ContentCheck checkImageContent(const std::vector<u32>& pixels, i32 w, i32 h);

/// Copia una imagen a `dest` normalizandola a BMP de 32 bits.
/// Admite cualquier formato que reconozca Windows (PNG, JPEG, GIF, TIFF...).
/// Si `check` no es nulo, recibe el resultado del control de contenido; la
/// importacion falla cuando el control no la permite.
bool importImage(const std::wstring& src, const std::wstring& dest,
                 std::string& error, ContentCheck* check = nullptr);

/// Decodifica cualquier formato de imagen soportado por Windows (WIC).
bool loadAnyImage(const std::wstring& file, std::vector<u32>& pixels, i32& w, i32& h);

/// Carga un BMP de 32/24 bits a memoria. Devuelve false si no se pudo.
bool loadBmp(const std::wstring& file, std::vector<u32>& pixels, i32& w, i32& h);

} // namespace photo
} // namespace ludora
