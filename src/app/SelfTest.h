#pragma once
#include "core/Scene.h"
#include "core/Types.h"
#include <string>
#include <vector>

namespace ludora {

/// Escena de autodiagnostico (--selftest): ejercita ventana, escala y
/// framebuffer desde dentro del bucle del motor y escribe un informe.
/// Verificar desde otro proceso no sirve: SetWindowPos cross-process
/// bloquea hasta que el hilo dueno de la ventana atiende el mensaje.
class SelfTest : public Scene {
public:
    explicit SelfTest(std::wstring reportPath);

    void update(Engine& engine, f64 dt) override;
    void render(Engine& engine, Framebuffer& fb) override;

private:
    void check(const char* name, bool ok, const std::string& detail = {});
    void writeReport();

    struct Result { std::string name; bool ok; std::string detail; };

    std::vector<Result> m_results;
    std::wstring m_path;
    i32 m_step   = 0;
    f64 m_timer  = 0.0;
    bool m_done  = false;

    // Estado de la prueba de clic sobre los botones de ventana.
    bool m_savedMaximized = false;
    i32  m_clickX = 0;
    i32  m_clickY = 0;
};

} // namespace ludora
