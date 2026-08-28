#pragma once
#include "core/Types.h"

namespace ludora {

class Engine;
class Framebuffer;

/// Panel con desplazamiento vertical y barra arrastrable.
///
/// Trabaja en pixeles LOGICOS. No dibuja el contenido: expone el
/// desplazamiento (`offset`) para que quien lo use pinte con ese margen, y se
/// encarga de la rueda, del arrastre del pulgar y de los limites.
class ScrollPanel {
public:
    /// Area del panel, incluida la barra.
    void setRect(Recti r)      { m_rect = r; }
    Recti rect() const         { return m_rect; }

    /// Alto total del contenido. Si no supera al del panel, no hay scroll.
    void setContentHeight(i32 h);
    i32  contentHeight() const { return m_contentH; }

    /// Desplazamiento actual, en pixeles (0 = arriba).
    i32  offset() const        { return m_offset; }
    void setOffset(i32 y);

    /// Ancho de la barra.
    void setBarWidth(i32 w)    { m_barWidth = w; }
    i32  barWidth() const      { return m_barWidth; }

    /// Lado en el que va la barra. En un panel pegado al borde izquierdo de la
    /// ventana, la barra debe ir a la izquierda para quedar junto a la pared.
    void setBarOnLeft(bool on) { m_barLeft = on; }
    bool barOnLeft() const     { return m_barLeft; }

    /// Area util para el contenido (el panel menos la barra).
    Recti contentRect() const;

    /// true si el contenido no cabe y por tanto hay barra.
    bool scrollable() const    { return m_contentH > m_rect.h; }

    /// Procesa rueda y arrastre. Devuelve true si el panel consumio el raton
    /// (esta encima o arrastrando), para que quien lo use no trate ese clic.
    bool update(Engine& engine);

    /// Dibuja el canal y el pulgar. El contenido lo pinta quien use el panel.
    void renderBar(Framebuffer& fb) const;

    bool dragging() const { return m_dragging; }

private:
    /// Rectangulo del pulgar dentro del canal.
    Recti thumbRect() const;
    Recti trackRect() const;

    Recti m_rect{};
    i32   m_contentH = 0;
    i32   m_offset   = 0;
    i32   m_barWidth = 12;

    bool  m_barLeft    = false;
    bool  m_dragging   = false;
    bool  m_hoverThumb = false;
    i32   m_dragGrabDy = 0;   // desfase dentro del pulgar al agarrarlo
};

} // namespace ludora
