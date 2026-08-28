#pragma once
#include "core/Types.h"

namespace ludora {

class Engine;
class Framebuffer;

/// Barra de titulo con los botones de ventana (minimizar, maximizar, cerrar).
///
/// Vive aparte de las escenas porque todas la comparten: duplicar el hit-test
/// y el dibujado en cada una es donde aparecen los desajustes de un pixel.
class TitleBar {
public:
    enum class Button { None = -1, Minimize = 0, Maximize = 1, Close = 2 };
    static constexpr i32 kButtonCount = 3;

    /// Instala en la ventana la zona interactiva (los botones quedan fuera
    /// del arrastre). Llamar desde Scene::onEnter.
    void attach(Engine& engine);
    /// Suelta el callback. Llamar desde Scene::onExit.
    void detach(Engine& engine);

    /// Procesa hover y clics. Devuelve true si el raton esta sobre un boton,
    /// para que la escena no trate ese clic como suyo.
    bool update(Engine& engine);

    void render(Engine& engine, Framebuffer& fb);

    /// Alto de la barra en pixeles logicos.
    i32 heightLogical(Engine& engine) const;

    /// Rectangulo de un boton, en pixeles logicos.
    Recti buttonRect(Engine& engine, i32 fbWidth, Button b) const;

    /// Boton bajo un punto logico, o None.
    Button buttonAt(Engine& engine, i32 fbWidth, i32 lx, i32 ly) const;

private:
    Button m_hovered = Button::None;
    Button m_pressed = Button::None;
    i32    m_lastFbWidth = 0;
};

} // namespace ludora
