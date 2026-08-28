#include "ui/TitleBar.h"
#include "ui/Theme.h"
#include "app/Font5x7.h"
#include "core/Engine.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace ludora {

using namespace theme;

void TitleBar::attach(Engine& engine) {
    // Los botones deben quedar FUERA del arrastre: si toda la barra fuese
    // HTCAPTION, Windows se quedaria los clics y nunca llegarian aqui.
    engine.window().isInteractiveArea = [this, &engine](i32 px, i32 py) -> bool {
        const f32 sc = std::max(0.01f, engine.window().contentScale());
        const i32 lx = static_cast<i32>(px / sc);
        const i32 ly = static_cast<i32>(py / sc);
        const i32 fbw = (m_lastFbWidth > 0) ? m_lastFbWidth
                                            : engine.renderer().fb().width();
        return buttonAt(engine, fbw, lx, ly) != Button::None;
    };
}

void TitleBar::detach(Engine& engine) {
    engine.window().isInteractiveArea = nullptr;   // captura this: no dejarlo colgando
}

i32 TitleBar::heightLogical(Engine& engine) const {
    const Window& win = engine.window();
    // En pantalla completa no hay barra de titulo: altura 0 para que el
    // contenido de cada seccion ocupe todo el alto y los botones ─ □ X
    // desaparezcan (no hay franja donde dibujarlos).
    if (win.isFullscreen()) return 0;
    const f32 sc = std::max(0.01f, win.contentScale());
    return std::max(12, static_cast<i32>(std::lround(win.desc().dragBarHeight / sc)));
}

Recti TitleBar::buttonRect(Engine& engine, i32 fbWidth, Button b) const {
    const i32 barH = heightLogical(engine);
    const i32 bw   = std::max(barH + barH / 2, 24);
    const i32 fromRight = (kButtonCount - 1 - static_cast<i32>(b));
    return Recti{ fbWidth - bw * (fromRight + 1), 0, bw, barH };
}

TitleBar::Button TitleBar::buttonAt(Engine& engine, i32 fbWidth, i32 lx, i32 ly) const {
    for (i32 i = 0; i < kButtonCount; ++i) {
        const Button b = static_cast<Button>(i);
        const Recti r = buttonRect(engine, fbWidth, b);
        if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
            return b;
    }
    return Button::None;
}

bool TitleBar::update(Engine& engine) {
    Input&  in  = engine.input();
    Window& win = engine.window();

    // Pantalla completa: no hay botones que atender.
    if (win.isFullscreen()) { m_hovered = m_pressed = Button::None; return false; }

    const f32 sc  = std::max(0.01f, win.contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx  = static_cast<i32>(mp.x / sc);
    const i32 ly  = static_cast<i32>(mp.y / sc);
    const i32 fbw = (m_lastFbWidth > 0) ? m_lastFbWidth : engine.renderer().fb().width();

    m_hovered = buttonAt(engine, fbw, lx, ly);

    if (in.mousePressed(0)) m_pressed = m_hovered;

    if (in.mouseReleased(0)) {
        // Solo actua si se suelta sobre el mismo boton donde se pulso.
        if (m_pressed != Button::None && m_pressed == m_hovered) {
            switch (m_pressed) {
            case Button::Minimize: win.minimize();       break;
            case Button::Maximize: win.toggleMaximize(); break;
            case Button::Close:    engine.requestQuit(); break;
            default: break;
            }
        }
        m_pressed = Button::None;
    }

    return m_hovered != Button::None;
}

void TitleBar::render(Engine& engine, Framebuffer& fb) {
    m_lastFbWidth = fb.width();

    const Window& win = engine.window();
    const i32 barH = heightLogical(engine);

    // Pantalla completa: sin barra, sin botones. No se dibuja nada.
    if (barH <= 0) return;

    fb.fillRect({0, 0, fb.width(), barH}, kBarBg);
    fb.fillRect({0, barH - 1, fb.width(), 1}, kBorder);

    const i32 dot = std::max(4, barH / 3);
    fb.fillRect({8, (barH - dot) / 2, dot, dot}, kAccent);
    const i32 ts = std::max(1, barH / 12);
    font5x7::drawText(fb, 8 + dot + 8, (barH - font5x7::kGlyphH * ts) / 2,
                      "LUDORA", kText, ts);

    for (i32 i = 0; i < kButtonCount; ++i) {
        const Button b = static_cast<Button>(i);
        const Recti  r = buttonRect(engine, fb.width(), b);

        const bool isPressed = (m_pressed == b && m_hovered == b);
        const bool isHover   = (m_hovered == b && m_pressed == Button::None) ||
                               (m_hovered == b && m_pressed == b);
        if (isPressed)
            fb.fillRect(r, b == Button::Close ? kClosePress : kPressed);
        else if (isHover)
            fb.fillRect(r, b == Button::Close ? kCloseHover : kHover);

        // Con cualquier realce el icono pasa a blanco: el gris se apagaria
        // sobre el rojo oscuro.
        const Color gc = (isHover || isPressed) ? kWhite : kGlyph;
        const i32 cx = r.x + r.w / 2;
        const i32 cy = r.y + r.h / 2;
        const i32 s  = std::max(3, barH / 6);

        switch (b) {
        case Button::Minimize:
            fb.fillRect({cx - s, cy, s * 2, 1}, gc);
            break;
        case Button::Maximize:
            if (win.isMaximized()) {
                // Restaurar: dos cuadrados solapados.
                fb.drawRect({cx - s, cy - s + 2, s * 2 - 2, s * 2 - 2}, gc, 1);
                fb.drawLine(cx - s + 2, cy - s, cx + s, cy - s, gc);
                fb.drawLine(cx + s, cy - s, cx + s, cy + s - 2, gc);
            } else {
                fb.drawRect({cx - s, cy - s, s * 2, s * 2}, gc, 1);
            }
            break;
        case Button::Close:
            fb.drawLine(cx - s, cy - s, cx + s, cy + s, gc);
            fb.drawLine(cx + s, cy - s, cx - s, cy + s, gc);
            break;
        default: break;
        }
    }
}

} // namespace ludora
