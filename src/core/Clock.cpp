#include "core/Clock.h"
#include <windows.h>

namespace ludora {

Clock::Clock() {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    m_freq = f.QuadPart ? f.QuadPart : 1;

    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    m_last  = c.QuadPart;
    m_start = c.QuadPart;
}

f64 Clock::tick() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);

    const i64 now = c.QuadPart;
    f64 dt = static_cast<f64>(now - m_last) / static_cast<f64>(m_freq);
    m_last = now;

    if (dt < 0.0)        dt = 0.0;
    if (dt > m_maxDelta) dt = m_maxDelta;

    m_delta = dt;
    m_total = static_cast<f64>(now - m_start) / static_cast<f64>(m_freq);
    ++m_frames;

    // FPS con suavizado exponencial: estable a la vista, sin promediar un buffer.
    if (dt > 0.0) {
        const f64 inst = 1.0 / dt;
        m_fpsAccum = (m_fpsAccum == 0.0) ? inst : (m_fpsAccum * 0.92 + inst * 0.08);
        m_fps = m_fpsAccum;
    }
    return m_delta;
}

} // namespace ludora
