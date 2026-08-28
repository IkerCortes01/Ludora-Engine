#pragma once
#include "core/Types.h"
#include <vector>

namespace ludora {

/// Superficie de pixeles en RAM, formato BGRA de 32 bits (compatible DIB top-down).
/// Es el destino de todo el dibujado del motor; la ventana solo la presenta.
class Framebuffer {
public:
    void resize(i32 w, i32 h);

    i32 width()  const { return m_w; }
    i32 height() const { return m_h; }
    bool valid() const { return m_w > 0 && m_h > 0 && !m_pixels.empty(); }

    u32*       data()       { return m_pixels.data(); }
    const u32* data() const { return m_pixels.data(); }

    void clear(Color c);

    /// Escribe un pixel con clipping. Alpha se ignora (escritura opaca).
    void setPixel(i32 x, i32 y, u32 packed) {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return;
        m_pixels[static_cast<size_t>(y) * m_w + x] = packed;
    }

    /// Mezcla alpha (src-over) en un pixel. c.a == 255 equivale a setPixel.
    void blendPixel(i32 x, i32 y, Color c);

    void fillRect(Recti r, Color c);
    void drawRect(Recti r, Color c, i32 thickness = 1);
    void drawLine(i32 x0, i32 y0, i32 x1, i32 y1, Color c);
    void fillCircle(i32 cx, i32 cy, i32 radius, Color c);

    /// Anillo de grosor `thickness` centrado en el radio dado. Con suavizado
    /// en el borde para que no se vea escalonado.
    void drawCircle(i32 cx, i32 cy, i32 radius, Color c, i32 thickness = 1);

    /// Copia `src` (w x h) recortada a un circulo, escalada al diametro.
    /// Se usa para las fotos de perfil: la imagen llega rectangular.
    void blitCircular(i32 cx, i32 cy, i32 radius,
                      const u32* src, i32 srcW, i32 srcH);

private:
    i32 m_w = 0;
    i32 m_h = 0;
    std::vector<u32> m_pixels;
};

} // namespace ludora
