#pragma once
#include "core/Types.h"
#include "core/Clock.h"
#include "core/Scene.h"
#include "platform/Window.h"
#include "render/RendererGDI.h"
#include <memory>

namespace ludora {

struct EngineConfig {
    WindowDesc window{};
    /// 0 = sin limite (gira tan rapido como pueda). >0 = tope de FPS.
    i32  targetFPS   = 60;
    /// Escala inicial del contenido. 2.0 = cada pixel logico ocupa 2x2 fisicos.
    f32  contentScale = 1.0f;
    bool smoothScaling = false;
};

/// Orquestador: posee ventana, renderer, reloj y escena activa,
/// y ejecuta el bucle principal.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(const EngineConfig& cfg);
    void shutdown();

    /// Bucle principal. Devuelve el codigo de salida del proceso.
    int  run(std::unique_ptr<Scene> scene);

    void setScene(std::unique_ptr<Scene> scene);

    /// Cambio de escena diferido al final del frame. Es lo que debe usar una
    /// escena para reemplazarse a si misma: setScene() directo la destruiria
    /// en mitad de su propio update(), y render() usaria memoria liberada.
    void queueScene(std::unique_ptr<Scene> scene);

    void requestQuit() { m_running = false; }

    Window&      window()   { return m_window; }
    RendererGDI& renderer() { return m_renderer; }
    Input&       input()    { return m_window.input(); }
    const Clock& clock() const { return m_clock; }

    /// Tamano del framebuffer logico (area cliente / escala).
    Size logicalSize() const;

    void setTargetFPS(i32 fps) { m_targetFPS = fps < 0 ? 0 : fps; }
    i32  targetFPS() const     { return m_targetFPS; }

    /// Ajusta la escala del contenido y reconstruye el framebuffer.
    void setContentScale(f32 s);
    f32  contentScale() const { return m_window.contentScale(); }

private:
    void rebuildTarget(i32 clientW, i32 clientH);
    void frameLimit();

    Window      m_window;
    RendererGDI m_renderer;
    Clock       m_clock;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_pendingScene;   // se aplica al cerrar el frame

    bool m_running   = false;
    bool m_inited    = false;
    i32  m_targetFPS = 60;
    f64  m_nextFrameTime = 0.0;
};

} // namespace ludora
