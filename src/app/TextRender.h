#pragma once
#include "core/Types.h"
#include "render/Framebuffer.h"
#include <string>

namespace ludora {

/// Texto nitido usando las fuentes del sistema.
///
/// ----------------------------------------------------------------------------
/// POR QUE EXISTE
/// ----------------------------------------------------------------------------
/// La fuente bitmap 5x7 del motor se ve pixelada: sus glifos son mapas de bits
/// de 5x7 que al escalar crecen en bloques cuadrados. Este modulo usa las
/// fuentes reales de Windows (Segoe UI) y las rasteriza CON SUAVIZADO al mismo
/// framebuffer, asi que la letra se ve como en cualquier aplicacion del
/// sistema.
///
/// No anade dependencias: gdi32 ya estaba enlazado para presentar el
/// framebuffer en pantalla. Lo unico que cambia es que ahora tambien se le
/// pide que dibuje glifos.
///
/// ----------------------------------------------------------------------------
/// COMO FUNCIONA
/// ----------------------------------------------------------------------------
/// Rasterizar un glifo con GDI cuesta caro para hacerlo 60 veces por segundo,
/// asi que cada glifo se dibuja UNA vez en un bitmap en memoria y se guarda en
/// una cache indexada por (caracter, tamano). A partir de ahi, pintar texto es
/// copiar mapas de cobertura ya calculados.
///
/// La cobertura se guarda como un byte por pixel (0 = fondo, 255 = trazo
/// pleno). Al dibujar se mezcla con el color pedido usando esa cobertura como
/// alfa, que es lo que da el borde suave.
namespace textr {

/// Alto nominal de la fuente para una "escala" dada.
///
/// El motor pide texto en escalas 1..4 heredadas de la fuente bitmap. Se
/// traducen a tamanos de fuente reales para que el cambio no descoloque las
/// pantallas ya maquetadas.
i32 pixelSize(i32 scale);

/// Ancho en pixeles que ocupara el texto.
i32 measure(const char* utf8, i32 scale);

/// Dibuja texto UTF-8 con suavizado. `y` es el borde SUPERIOR, igual que en
/// la fuente bitmap, para que las llamadas existentes no se descoloquen.
void draw(Framebuffer& fb, i32 x, i32 y, const char* utf8, Color color, i32 scale);

/// Alto de una linea para esa escala (incluye el interlineado habitual).
/// Es lo que hay que avanzar para pasar al RENGLON siguiente.
i32 lineHeight(i32 scale);

/// Alto de la LETRA sola, sin el hueco de separacion entre renglones.
///
/// Es lo que hay que usar para CENTRAR texto en una caja: el interlineado no
/// es tinta, y contarlo desplaza el rotulo hacia abajo.
i32 glyphHeight(i32 scale);

/// Suelta la cache y los recursos de GDI. Se llama al cerrar.
void shutdown();

/// true si el sistema pudo crear la fuente. Si devuelve false, quien llame
/// deberia recurrir a la fuente bitmap: mejor texto feo que ningun texto.
bool available();

} // namespace textr
} // namespace ludora
