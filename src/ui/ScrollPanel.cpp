#include "ui/ScrollPanel.h"
#include "core/Engine.h"
#include "ui/Theme.h"
#include <algorithm>

namespace ludora {

using namespace theme;

namespace {
constexpr i32 kMinThumb  = 24;   // el pulgar nunca se hace ilegible
constexpr f32 kWheelStep = 48.0f;
} // namespace

void ScrollPanel::setContentHeight(i32 h) {
    m_contentH = std::max(0, h);
    setOffset(m_offset);   // reajusta si el contenido encogio
}

void ScrollPanel::setOffset(i32 y) {
    const i32 maxOff = std::max(0, m_contentH - m_rect.h);
    m_offset = std::clamp(y, 0, maxOff);
}

Recti ScrollPanel::contentRect() const {
    if (!scrollable()) return m_rect;
    const i32 w = std::max(0, m_rect.w - m_barWidth);
    // Con la barra a la izquierda, el contenido empieza despues de ella.
    const i32 x = m_barLeft ? m_rect.x + m_barWidth : m_rect.x;
    return Recti{ x, m_rect.y, w, m_rect.h };
}

Recti ScrollPanel::trackRect() const {
    const i32 x = m_barLeft ? m_rect.x
                            : m_rect.x + m_rect.w - m_barWidth;
    return Recti{ x, m_rect.y, m_barWidth, m_rect.h };
}

Recti ScrollPanel::thumbRect() const {
    const Recti t = trackRect();
    if (!scrollable()) return Recti{ t.x, t.y, t.w, t.h };

    // Alto proporcional a la porcion visible, con un minimo para poder cogerlo.
    const i32 h = std::max(kMinThumb,
                           static_cast<i32>(static_cast<i64>(t.h) * m_rect.h / m_contentH));
    const i32 maxOff = std::max(1, m_contentH - m_rect.h);
    const i32 travel = t.h - h;   // recorrido disponible del pulgar
    const i32 y = t.y + static_cast<i32>(static_cast<i64>(travel) * m_offset / maxOff);
    return Recti{ t.x, y, t.w, h };
}

bool ScrollPanel::update(Engine& engine) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 mx = static_cast<i32>(mp.x / sc);
    const i32 my = static_cast<i32>(mp.y / sc);

    const bool dentro = (mx >= m_rect.x && mx < m_rect.x + m_rect.w &&
                         my >= m_rect.y && my < m_rect.y + m_rect.h);

    if (!scrollable()) {
        m_dragging = m_hoverThumb = false;
        return dentro;
    }

    const Recti th = thumbRect();
    m_hoverThumb = (mx >= th.x && mx < th.x + th.w &&
                    my >= th.y && my < th.y + th.h);

    // --- arrastre del pulgar ---
    if (in.mousePressed(0)) {
        if (m_hoverThumb) {
            m_dragging   = true;
            m_dragGrabDy = my - th.y;   // conserva el punto donde se agarro
        } else {
            const Recti t = trackRect();
            const bool enCanal = (mx >= t.x && mx < t.x + t.w &&
                                  my >= t.y && my < t.y + t.h);
            if (enCanal) {
                // Clic en el canal: salta llevando el pulgar bajo el cursor.
                const i32 travel = std::max(1, t.h - th.h);
                const i32 maxOff = std::max(0, m_contentH - m_rect.h);
                const i32 rel    = std::clamp(my - t.y - th.h / 2, 0, travel);
                setOffset(static_cast<i32>(static_cast<i64>(rel) * maxOff / travel));
                m_dragging   = true;
                m_dragGrabDy = th.h / 2;
            }
        }
    }

    if (m_dragging && in.mouseDown(0)) {
        const Recti t = trackRect();
        const i32 travel = std::max(1, t.h - th.h);
        const i32 maxOff = std::max(0, m_contentH - m_rect.h);
        const i32 rel    = std::clamp(my - t.y - m_dragGrabDy, 0, travel);
        setOffset(static_cast<i32>(static_cast<i64>(rel) * maxOff / travel));
    }

    if (in.mouseReleased(0)) m_dragging = false;

    // --- rueda: solo si el cursor esta sobre el panel ---
    if (dentro) {
        const f32 w = in.wheelDelta();
        if (w != 0.0f)
            setOffset(m_offset - static_cast<i32>(w * kWheelStep));
    }

    return dentro || m_dragging;
}

void ScrollPanel::renderBar(Framebuffer& fb) const {
    if (!scrollable()) return;

    const Recti t = trackRect();
    fb.fillRect(t, kScrollTrack);

    const Recti th = thumbRect();
    const Color c = (m_dragging || m_hoverThumb) ? kScrollHover : kScrollThumb;
    // Pulgar con 2 px de aire a los lados para que se lea como una pieza.
    fb.fillRect({th.x + 2, th.y, std::max(1, th.w - 4), th.h}, c);
}

} // namespace ludora
