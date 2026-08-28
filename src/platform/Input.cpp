#include "platform/Input.h"

namespace ludora {

void Input::newFrame() {
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
    m_mouse.fill(false);
    m_prevMouse.fill(false);
    m_wheel = 0.0f;
    m_chars.clear();
}

} // namespace ludora
