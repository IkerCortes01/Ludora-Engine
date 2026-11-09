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
    /// `dt` son los segundos del frame, para la repeticion automatica.
    void newFrame(f64 dt = 0.0);

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

    /// Como keyPressed, pero ADEMAS se repite sola si la tecla sigue
    /// hundida, igual que en cualquier editor de texto.
    ///
    /// Sin esto, borrar treinta caracteres eran treinta pulsaciones: el
    /// flanco solo salta una vez y mantener Supr no hacia nada.
    ///
    /// Los tiempos son los de Windows por defecto: una espera inicial para
    /// que una pulsacion normal no dispare la repeticion, y luego un ritmo
    /// rapido y constante.
    bool keyRepeat(u32 vk) const { return vk < kKeyCount && m_repeat[vk]; }

    // --- consultas de raton (0 = izquierdo, 1 = derecho, 2 = medio) ---
    bool mouseDown(int b)    const { return b >= 0 && b < 3 && m_mouse[b]; }
    bool mousePressed(int b) const { return b >= 0 && b < 3 &&  m_mouse[b] && !m_prevMouse[b]; }
    bool mouseReleased(int b)const { return b >= 0 && b < 3 && !m_mouse[b] &&  m_prevMouse[b]; }

    Vec2 mousePos()   const { return m_mousePos; }
    Vec2 mouseDelta() const { return m_mousePos - m_prevMousePos; }
    f32  wheelDelta() const { return m_wheel; }

private:
    /// Espera antes de empezar a repetir, y ritmo una vez arrancada. Son los
    /// valores tipicos de Windows: por debajo, una pulsacion corta acabaria
    /// borrando de mas.
    static constexpr f64 kRepeatDelay = 0.45;   // segundos
    static constexpr f64 kRepeatRate  = 0.035;  // segundos entre repeticiones

    std::array<bool, kKeyCount> m_keys{};
    std::array<bool, kKeyCount> m_prevKeys{};
    /// true solo en los frames en que la tecla "cuenta" otra vez.
    std::array<bool, kKeyCount> m_repeat{};
    /// Cuanto lleva hundida cada tecla, y cuando toca la proxima repeticion.
    std::array<f64, kKeyCount>  m_heldFor{};
    std::array<f64, kKeyCount>  m_nextRepeat{};
    std::array<bool, 3> m_mouse{};
    std::array<bool, 3> m_prevMouse{};

    Vec2 m_mousePos{};
    Vec2 m_prevMousePos{};
    f32  m_wheel = 0.0f;
    std::wstring m_chars;   // texto tecleado en el frame actual
};

} // namespace ludora
