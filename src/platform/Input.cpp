#include "platform/Input.h"

namespace ludora {

void Input::newFrame(f64 dt) {
    // --- repeticion automatica de las teclas hundidas ---
    //
    // Se calcula ANTES de copiar m_keys a m_prevKeys, porque el primer
    // disparo de la repeticion es el propio flanco de pulsacion: asi
    // keyRepeat() vale desde el frame en que se baja la tecla y el que la
    // use no necesita consultar tambien keyPressed().
    for (int vk = 0; vk < kKeyCount; ++vk) {
        if (!m_keys[vk]) {
            // Soltada: se reinicia para que la proxima pulsacion vuelva a
            // esperar la pausa inicial entera.
            m_repeat[vk]     = false;
            m_heldFor[vk]    = 0.0;
            m_nextRepeat[vk] = 0.0;
            continue;
        }

        if (!m_prevKeys[vk]) {
            // Recien pulsada: cuenta una vez y arranca la cuenta atras.
            m_repeat[vk]     = true;
            m_heldFor[vk]    = 0.0;
            m_nextRepeat[vk] = kRepeatDelay;
            continue;
        }

        // Sigue hundida: cuenta de nuevo cada vez que se cumple el plazo.
        m_heldFor[vk] += dt;
        if (m_heldFor[vk] >= m_nextRepeat[vk]) {
            m_repeat[vk]      = true;
            m_nextRepeat[vk] += kRepeatRate;
        } else {
            m_repeat[vk] = false;
        }
    }

    m_prevKeys     = m_keys;
    m_prevMouse    = m_mouse;
    m_prevMousePos = m_mousePos;
    m_wheel        = 0.0f;   // la rueda es un evento puntual, no un estado
    m_chars.clear();         // el texto tecleado solo vive un frame
}

void Input::onChar(wchar_t c) {
    m_chars.push_back(c);
}

void Input::onKey(u32 vk, bool down) {
    if (vk < kKeyCount) m_keys[vk] = down;
}

void Input::onMouseButton(int button, bool down) {
    if (button >= 0 && button < 3) m_mouse[button] = down;
}

void Input::onMouseMove(i32 x, i32 y) {
    m_mousePos = Vec2{static_cast<f32>(x), static_cast<f32>(y)};
}

void Input::onMouseWheel(f32 delta) {
    m_wheel += delta;
}

void Input::resetAll() {
    // Se llama al perder el foco: si no, una tecla queda "pegada"
    // porque el WM_KEYUP se entrega a otra ventana.
    m_keys.fill(false);
    m_prevKeys.fill(false);
    m_repeat.fill(false);
    m_heldFor.fill(0.0);
    m_nextRepeat.fill(0.0);
    m_mouse.fill(false);
    m_prevMouse.fill(false);
    m_wheel = 0.0f;
    m_chars.clear();
}

} // namespace ludora
