#pragma once
#include "core/Scene.h"
#include "core/Types.h"

namespace ludora {

class Framebuffer;

/// Escena utilitaria (--screenshot): pide a DemoScene que se dibuje en varios
/// estados y vuelca cada uno a un .bmp, luego sale.
///
/// Existe porque capturar la pantalla no es fiable: cualquier ventana que se
/// ponga delante arruina la imagen. Leer el framebuffer del motor da siempre
/// exactamente lo que el motor dibuja.
class Screenshot : public Scene {
public:
    void update(Engine& engine, f64 dt) override;
    void render(Engine& engine, Framebuffer& fb) override;

private:
    /// Guarda el framebuffer como BMP de 32 bits (sin dependencias externas).
    static bool saveBmp(const Framebuffer& fb, const wchar_t* path);

    i32  m_step  = 0;
    f64  m_timer = 0.0;
    bool m_done  = false;
};

} // namespace ludora
