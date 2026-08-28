#pragma once
#include "core/Types.h"

namespace ludora {

/// Reloj de alta precision basado en QueryPerformanceCounter.
/// Provee delta time acotado y FPS suavizado.
class Clock {
public:
    Clock();

    /// Avanza un frame. Devuelve el delta en segundos, acotado a maxDelta
    /// para evitar saltos gigantes tras un breakpoint o un arrastre de ventana.
    f64 tick();

    f64 deltaSeconds() const { return m_delta; }
    f64 totalSeconds() const { return m_total; }
    f64 fps()          const { return m_fps; }
    i64 frameCount()   const { return m_frames; }

    void setMaxDelta(f64 s) { m_maxDelta = s; }

private:
    i64 m_freq   = 1;
    i64 m_last   = 0;
    i64 m_start  = 0;
    i64 m_frames = 0;

    f64 m_delta    = 0.0;
    f64 m_total    = 0.0;
    f64 m_maxDelta = 0.25;   // 250 ms: techo anti-salto
    f64 m_fps      = 0.0;
    f64 m_fpsAccum = 0.0;    // suavizado exponencial
};

} // namespace ludora
