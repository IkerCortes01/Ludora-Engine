#pragma once
#include "core/Types.h"
#include "render/Framebuffer.h"

namespace ludora {

/// Fuente bitmap 5x7 embebida: el HUD no depende de GDI ni de fuentes del
/// sistema, asi el texto escala junto con el resto del framebuffer.
/// Cubre ASCII 32..126. Cada glifo son 5 columnas, cada byte 7 bits (LSB = fila 0).
namespace font5x7 {

const u8* glyph(char c);

/// Glifo por codigo Unicode. Cubre ASCII 32..126 y un bloque latino
/// (acentos, enye, dieresis). Devuelve nullptr si no hay glifo.
const u8* glyphUnicode(u32 cp);

/// Dibuja texto. scale multiplica el tamano del glifo en pixeles logicos.
void drawText(Framebuffer& fb, i32 x, i32 y, const char* text, Color color, i32 scale = 1);

/// Igual, pero desde UTF-8: permite acentos y enye.
void drawTextUtf8(Framebuffer& fb, i32 x, i32 y, const char* utf8, Color color, i32 scale = 1);

/// Ancho en pixeles que ocupara el texto (incluye 1 px de separacion).
i32 measure(const char* text, i32 scale = 1);

/// Ancho de una cadena UTF-8 (cuenta caracteres, no bytes).
i32 measureUtf8(const char* utf8, i32 scale = 1);

/// Numero de caracteres (no bytes) de una cadena UTF-8.
size_t lengthUtf8(const char* utf8);

/// Decodifica el caracter en `p`; avanza `p` al siguiente. 0 al terminar.
u32 nextUtf8(const char*& p);

constexpr i32 kGlyphW = 5;
constexpr i32 kGlyphH = 7;

} // namespace font5x7
} // namespace ludora
