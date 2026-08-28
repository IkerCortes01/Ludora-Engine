#pragma once
#include "core/Types.h"
#include <string>

namespace ludora {

class Framebuffer;
class Input;

/// Campo de texto de una linea: cursor, seleccion, portapapeles y modo
/// contrasena. Trabaja en pixeles LOGICOS (los del framebuffer).
///
/// No conoce la ventana ni el motor: recibe eventos ya traducidos, asi que
/// se puede reutilizar en cualquier escena.
class TextField {
public:
    void setRect(Recti r)                 { m_rect = r; }
    Recti rect() const                    { return m_rect; }

    void setPlaceholder(std::string p)    { m_placeholder = std::move(p); }
    void setPassword(bool on)             { m_password = on; }
    /// Limite en CARACTERES (no bytes): un acento ocupa 2 bytes en UTF-8.
    void setMaxLength(size_t n)           { m_maxLen = n; }

    /// Numero de caracteres escritos (no bytes).
    size_t length() const;

    const std::string& text() const       { return m_text; }
    void setText(std::string t);
    void clear();

    bool focused() const                  { return m_focused; }
    void setFocused(bool on);

    bool contains(i32 x, i32 y) const;

    // --- eventos ---
    /// Caracter imprimible tecleado (ya filtrado de teclas de control).
    void onChar(wchar_t c);
    /// Tecla especial (VK_*). shift/ctrl para seleccion y atajos.
    void onKey(u32 vk, bool shift, bool ctrl);
    /// Clic dentro del campo: coloca el cursor y arranca la seleccion.
    void onMouseDown(i32 x, i32 y);
    /// Arrastre con el boton pulsado: extiende la seleccion.
    void onMouseDrag(i32 x, i32 y);

    /// Avanza el parpadeo del cursor.
    void update(f64 dt);

    void render(Framebuffer& fb) const;

    /// Colores (se fijan una vez desde la escena).
    struct Style {
        Color bg;
        Color bgFocused;
        Color border;
        Color borderFocused;
        Color text;
        Color placeholder;
        Color caret;
        Color selection;
        i32   textScale = 2;
    };
    void setStyle(const Style& s) { m_style = s; }

private:
    // El cursor y la seleccion se miden en CARACTERES, pero std::string
    // guarda bytes: en UTF-8 un acento ocupa 2. Estos dos conversores evitan
    // partir un caracter por la mitad al borrar o al seleccionar.
    size_t byteOfChar(size_t charIdx) const;
    size_t charCount() const;

    /// Texto tal y como se pinta (asteriscos si es contrasena).
    std::string visibleText() const;
    /// Indice de caracter mas cercano a una x en pixeles.
    size_t indexFromX(i32 x) const;
    /// x en pixeles del inicio del caracter idx.
    i32 xFromIndex(size_t idx) const;

    void deleteSelection();
    bool hasSelection() const { return m_selAnchor != m_caret; }
    size_t selStart() const   { return m_selAnchor < m_caret ? m_selAnchor : m_caret; }
    size_t selEnd() const     { return m_selAnchor < m_caret ? m_caret : m_selAnchor; }
    void copySelectionToClipboard() const;
    void pasteFromClipboard();

    Recti       m_rect{};
    std::string m_text;
    std::string m_placeholder;
    Style       m_style{};

    size_t m_caret     = 0;   // posicion del cursor (en caracteres)
    size_t m_selAnchor = 0;   // ancla de la seleccion; == m_caret si no hay
    size_t m_maxLen    = 64;

    bool m_focused  = false;
    bool m_password = false;

    f64  m_blink    = 0.0;
    i32  m_scrollPx = 0;      // desplazamiento cuando el texto no cabe
};

} // namespace ludora
