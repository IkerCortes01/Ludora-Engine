#pragma once
#include "core/Types.h"

namespace ludora {

class Engine;
class Framebuffer;

/// Contrato de contenido del motor. El Engine no sabe que dibuja una escena:
/// solo la actualiza y le pide que se pinte. Cambiar de escena no toca el core.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter(Engine&) {}
    virtual void onExit(Engine&)  {}

    /// Logica del frame. dt en segundos.
    virtual void update(Engine& engine, f64 dt) = 0;

    /// Dibujado sobre el framebuffer logico.
    virtual void render(Engine& engine, Framebuffer& fb) = 0;

    /// El framebuffer logico cambio de tamano (resize o cambio de escala).
    virtual void onResize(i32 /*w*/, i32 /*h*/) {}
};

} // namespace ludora
