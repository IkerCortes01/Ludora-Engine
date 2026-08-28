#pragma once
#include "core/Types.h"
#include <array>
#include <string>

namespace ludora {

/// Estado de teclado y raton con deteccion de flanco (pressed/released),
/// no solo nivel (down). Se actualiza una vez por frame via newFrame().
class Input {
public:
    static constexpr int kKeyCount = 256;

    /// Llamar al inicio de cada frame: copia el estado actual a "anterior".
    void newFrame();

    // --- alimentado por el WndProc ---
    void onKey(u32 vk, bool down);
    void onMouseButton(int button, bool down);
    void onMouseMove(i32 x, i32 y);
    void onMouseWheel(f32 delta);
    /// Caracter ya traducido por la distribucion de teclado (WM_CHAR).
    void onChar(wchar_t c);
    void resetAll();

    /// Caracteres tecleados en este frame, en orden. Se vacia en newFrame(),
    /// no al leerse: procesarlo dos veces sin un newFrame() entre medias
    /// inserta el texto por duplicado.
    const std::wstring& charsTyped() const { return m_chars; }

    // --- consultas de teclado (vk = codigo virtual de Windows) ---
    bool keyDown(u32 vk)     const { return vk < kKeyCount && m_keys[vk]; }
    bool keyPressed(u32 vk)  const { return vk < kKeyCount &&  m_keys[vk] && !m_prevKeys[vk]; }
    bool keyReleased(u32 vk) const { return vk < kKeyCount && !m_keys[vk] &&  m_prevKeys[vk]; }

    // --- consultas de raton (0 = izquierdo, 1 = derecho, 2 = medio) ---
    bool mouseDown(int b)    const { return b >= 0 && b < 3 && m_mouse[b]; }
    bool mousePressed(int b) const { return b >= 0 && b < 3 &&  m_mouse[b] && !m_prevMouse[b]; }
    bool mouseReleased(int b)const { return b >= 0 && b < 3 && !m_mouse[b] &&  m_prevMouse[b]; }

    Vec2 mousePos()   const { return m_mousePos; }
    Vec2 mouseDelta() const { return m_mousePos - m_prevMousePos; }
    f32  wheelDelta() const { return m_wheel; }

private:
    std::array<bool, kKeyCount> m_keys{};
    std::array<bool, kKeyCount> m_prevKeys{};
    std::array<bool, 3> m_mouse{};
    std::array<bool, 3> m_prevMouse{};

    Vec2 m_mousePos{};
    Vec2 m_prevMousePos{};
    f32  m_wheel = 0.0f;
    std::wstring m_chars;   // texto tecleado en el frame actual
};

} // namespace ludora
