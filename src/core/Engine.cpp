#include "core/Engine.h"
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod: WIN32_LEAN_AND_MEAN lo excluye de windows.h

namespace ludora {

Engine::Engine()  = default;
Engine::~Engine() { shutdown(); }

bool Engine::init(const EngineConfig& cfg) {
    if (m_inited) return true;

    m_targetFPS = cfg.targetFPS < 0 ? 0 : cfg.targetFPS;

    if (!m_window.create(cfg.window))     return false;
    if (!m_renderer.init(m_window.handle())) return false;

    m_renderer.setSmoothScaling(cfg.smoothScaling);
    m_window.setContentScale(cfg.contentScale);

    // Cuando la ventana cambia de tamano o de escala, el framebuffer logico
    // se reconstruye y la escena recibe el aviso.
    m_window.onResize = [this](i32 w, i32 h) { rebuildTarget(w, h); };

    const Size cs = m_window.clientSize();
    rebuildTarget(cs.w, cs.h);

    m_inited = true;
    return true;
}

void Engine::shutdown() {
    if (m_scene) {
        m_scene->onExit(*this);
        m_scene.reset();
    }
    m_renderer.shutdown();
    m_window.destroy();
    m_inited = false;
}

Size Engine::logicalSize() const {
    const Size cs = m_window.clientSize();
    const f32  s  = std::max(0.01f, m_window.contentScale());
    return Size{ std::max(1, static_cast<i32>(std::lround(cs.w / s))),
                 std::max(1, static_cast<i32>(std::lround(cs.h / s))) };
}

void Engine::rebuildTarget(i32 clientW, i32 clientH) {
    if (clientW <= 0 || clientH <= 0) return;

    const f32 s = std::max(0.01f, m_window.contentScale());
    const i32 lw = std::max(1, static_cast<i32>(std::lround(clientW / s)));
    const i32 lh = std::max(1, static_cast<i32>(std::lround(clientH / s)));

    m_renderer.resizeTarget(lw, lh);
    if (m_scene) m_scene->onResize(lw, lh);
}

void Engine::setContentScale(f32 s) {
    m_window.setContentScale(s);   // dispara onResize -> rebuildTarget
}

void Engine::queueScene(std::unique_ptr<Scene> scene) {
    m_pendingScene = std::move(scene);
}

void Engine::setScene(std::unique_ptr<Scene> scene) {
    if (m_scene) m_scene->onExit(*this);
    m_scene = std::move(scene);
    if (m_scene) {
        m_scene->onEnter(*this);
        const Size ls = logicalSize();
        m_scene->onResize(ls.w, ls.h);
    }
}

namespace {
/// Tiempo actual en segundos leido del contador de rendimiento.
/// El limitador NO puede usar Clock::totalSeconds(): ese valor solo se
/// refresca en Clock::tick() (una vez por frame), asi que dentro de una
/// espera activa se queda congelado y el bucle no termina nunca.
f64 nowSeconds() {
    static const f64 invFreq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart ? 1.0 / static_cast<f64>(f.QuadPart) : 0.0;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<f64>(c.QuadPart) * invFreq;
}
} // namespace

void Engine::frameLimit() {
    if (m_targetFPS <= 0) return;

    const f64 frameTime = 1.0 / static_cast<f64>(m_targetFPS);
    const f64 now = nowSeconds();

    if (m_nextFrameTime == 0.0) {
        m_nextFrameTime = now + frameTime;
        return;
    }

    const f64 remaining = m_nextFrameTime - now;
    if (remaining > 0.0) {
        // Sleep con margen (2 ms) y luego espera activa cediendo el quantum:
        // Sleep solo no tiene la resolucion suficiente para un ritmo estable.
        const int ms = static_cast<int>((remaining - 0.002) * 1000.0);
        if (ms > 0) Sleep(static_cast<DWORD>(ms));
        while (nowSeconds() < m_nextFrameTime) YieldProcessor();
    }

    m_nextFrameTime += frameTime;
    // Si nos hemos quedado muy atras, resincronizar en vez de acumular deuda.
    const f64 t = nowSeconds();
    if (m_nextFrameTime < t) m_nextFrameTime = t + frameTime;
}

int Engine::run(std::unique_ptr<Scene> scene) {
    if (!m_inited) return -1;

    setScene(std::move(scene));
    m_running = true;

    // Precision de 1 ms para Sleep durante el bucle.
    timeBeginPeriod(1);

    while (m_running) {
        m_window.input().newFrame();

        if (!m_window.pumpMessages() || m_window.shouldClose()) {
            m_running = false;
            break;
        }

        const f64 dt = m_clock.tick();

        if (m_scene) {
            m_scene->update(*this, dt);

            // Minimizada: sin area cliente, no tiene sentido renderizar.
            const Size cs = m_window.clientSize();
            if (cs.w > 0 && cs.h > 0 && m_renderer.fb().valid()) {
                m_scene->render(*this, m_renderer.fb());
                m_renderer.present(cs.w, cs.h);
            }
        }

        // Cambio de escena solicitado durante el frame: se aplica ahora, con
        // update() y render() ya terminados, para no destruir la escena viva.
        if (m_pendingScene)
            setScene(std::move(m_pendingScene));

        frameLimit();
    }

    timeEndPeriod(1);
    shutdown();
    return 0;
}

} // namespace ludora
