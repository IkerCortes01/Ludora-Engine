#pragma once
#include "core/Types.h"
#include "render/Framebuffer.h"
#include <windows.h>

namespace ludora {

/// Presenta el Framebuffer en un HWND usando StretchDIBits.
///
/// Separacion clave del motor:
///   - resolucion LOGICA  = tamano del framebuffer (donde dibuja la escena)
///   - resolucion FISICA  = area cliente de la ventana (donde se presenta)
/// El escalado entre ambas es lo que permite hacer zoom sin redibujar la escena
/// a otra resolucion.
class RendererGDI {
public:
    ~RendererGDI();

    bool init(HWND hwnd);
    void shutdown();

    /// Redimensiona el framebuffer logico.
    void resizeTarget(i32 w, i32 h);

    /// Vuelca el framebuffer al area cliente, escalando a clientW x clientH.
    void present(i32 clientW, i32 clientH);

    Framebuffer&       fb()       { return m_fb; }
    const Framebuffer& fb() const { return m_fb; }

    /// Interpolacion al escalar: true = suavizado (HALFTONE), false = nearest.
    /// Nearest conserva el look de pixel art al ampliar.
    void setSmoothScaling(bool on) { m_smooth = on; }
    bool smoothScaling() const     { return m_smooth; }

private:
    void ensureBackBuffer(i32 w, i32 h);

    HWND    m_hwnd    = nullptr;
    HDC     m_memDC   = nullptr;   // back buffer para eliminar parpadeo
    HBITMAP m_memBmp  = nullptr;
    HBITMAP m_oldBmp  = nullptr;
    i32     m_bbW     = 0;
    i32     m_bbH     = 0;

    Framebuffer m_fb;
    BITMAPINFO  m_bmi{};
    bool        m_smooth = false;
};

} // namespace ludora
