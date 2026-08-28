#include "ui/TextField.h"
#include "app/Font5x7.h"
#include "core/Types.h"
#include "render/Framebuffer.h"
#include <algorithm>
#include <windows.h>

namespace ludora {

namespace {
constexpr i32 kPadX = 8;          // margen interior horizontal
constexpr f64 kBlinkPeriod = 1.0; // ciclo completo del cursor

/// Avance por caracter: la fuente es monoespaciada (5 px + 1 de separacion).
i32 advance(i32 scale) { return (font5x7::kGlyphW + 1) * scale; }
} // namespace

size_t TextField::byteOfChar(size_t charIdx) const {
    const char* p = m_text.c_str();
    const char* start = p;
    for (size_t i = 0; i < charIdx && *p; ++i) font5x7::nextUtf8(p);
    return static_cast<size_t>(p - start);
}

size_t TextField::charCount() const {
    return font5x7::lengthUtf8(m_text.c_str());
}

size_t TextField::length() const { return charCount(); }

void TextField::setText(std::string t) {
    m_text = std::move(t);
    // Recortar por caracteres, no por bytes: cortar a mitad de un acento
    // dejaria una secuencia UTF-8 invalida.
    if (font5x7::lengthUtf8(m_text.c_str()) > m_maxLen)
        m_text.resize(byteOfChar(m_maxLen));
    m_caret = m_selAnchor = charCount();
    m_scrollPx = 0;
}

void TextField::clear() {
    m_text.clear();
    m_caret = m_selAnchor = 0;
    m_scrollPx = 0;
}

void TextField::setFocused(bool on) {
    if (m_focused == on) return;
    m_focused = on;
    m_blink = 0.0;
    if (!on) m_selAnchor = m_caret;   // al perder foco se suelta la seleccion
}

bool TextField::contains(i32 x, i32 y) const {
    return x >= m_rect.x && x < m_rect.x + m_rect.w &&
           y >= m_rect.y && y < m_rect.y + m_rect.h;
}

std::string TextField::visibleText() const {
    // Un asterisco por CARACTER: con bytes, un acento mostraria dos.
    if (!m_password) return m_text;
    return std::string(charCount(), '*');
}

i32 TextField::xFromIndex(size_t idx) const {
    return static_cast<i32>(idx) * advance(m_style.textScale);
}

size_t TextField::indexFromX(i32 x) const {
    const i32 adv = advance(m_style.textScale);
    // Coordenada relativa al inicio del texto, compensando el scroll.
    const i32 rel = x - (m_rect.x + kPadX) + m_scrollPx;
    if (rel <= 0) return 0;
    // +adv/2 hace que el cursor salte al hueco mas cercano, no al de la izquierda.
    const size_t idx = static_cast<size_t>((rel + adv / 2) / adv);
    return std::min(idx, charCount());
}

void TextField::deleteSelection() {
    if (!hasSelection()) return;
    const size_t s = selStart(), e = selEnd();
    const size_t bs = byteOfChar(s), be = byteOfChar(e);
    m_text.erase(bs, be - bs);
    m_caret = m_selAnchor = s;
}

void TextField::copySelectionToClipboard() const {
    if (!hasSelection() || m_password) return;   // nunca copiar una contrasena
    const size_t bs = byteOfChar(selStart()), be = byteOfChar(selEnd());
    const std::string sel = m_text.substr(bs, be - bs);

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = sel.size() + 1;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, sel.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void TextField::pasteFromClipboard() {
    if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(nullptr)) return;

    if (HANDLE h = GetClipboardData(CF_TEXT)) {
        if (const char* src = static_cast<const char*>(GlobalLock(h))) {
            deleteSelection();
            // Se reutiliza onChar: asi el pegado aplica el mismo filtro de
            // glifos, el mismo limite y la misma codificacion que el teclado.
            for (const char* p = src; *p; ++p) {
                const unsigned char uc = static_cast<unsigned char>(*p);
                if (uc == '\r' || uc == '\n' || uc == '\t') continue;
                onChar(static_cast<wchar_t>(uc));
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

void TextField::onChar(wchar_t c) {
    if (!m_focused) return;

    const u32 cp = static_cast<u32>(c);
    if (cp < 32 || cp == 127) return;               // codigos de control fuera
    if (!font5x7::glyphUnicode(cp)) return;         // sin glifo no se acepta

    // El limite es por caracteres; la seleccion se va a reemplazar, asi que
    // no cuenta para el tope.
    if (charCount() - (selEnd() - selStart()) >= m_maxLen) return;

    deleteSelection();

    // Codificar a UTF-8 (1 o 2 bytes basta para el rango que cubre la fuente).
    char enc[3];
    size_t n = 0;
    if (cp < 0x80) {
        enc[n++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
        enc[n++] = static_cast<char>(0xC0 | (cp >> 6));
        enc[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        enc[n++] = static_cast<char>(0xE0 | (cp >> 12));
        enc[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        enc[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }

    m_text.insert(byteOfChar(m_caret), enc, n);
    ++m_caret;
    m_selAnchor = m_caret;
    m_blink = 0.0;
}

void TextField::onKey(u32 vk, bool shift, bool ctrl) {
    if (!m_focused) return;
    m_blink = 0.0;

    switch (vk) {
    case VK_LEFT:
        if (m_caret > 0) --m_caret;
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_RIGHT:
        if (m_caret < charCount()) ++m_caret;
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_HOME:
        m_caret = 0;
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_END:
        m_caret = charCount();
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_BACK:
        if (hasSelection()) {
            deleteSelection();
        } else if (m_caret > 0) {
            // Borrar el caracter completo, que puede ocupar 2 bytes.
            const size_t b0 = byteOfChar(m_caret - 1);
            const size_t b1 = byteOfChar(m_caret);
            m_text.erase(b0, b1 - b0);
            --m_caret;
            m_selAnchor = m_caret;
        }
        break;

    case VK_DELETE:
        if (hasSelection()) {
            deleteSelection();
        } else if (m_caret < charCount()) {
            const size_t b0 = byteOfChar(m_caret);
            const size_t b1 = byteOfChar(m_caret + 1);
            m_text.erase(b0, b1 - b0);
        }
        break;

    case 'A':
        if (ctrl) { m_selAnchor = 0; m_caret = charCount(); }
        break;

    case 'C':
        if (ctrl) copySelectionToClipboard();
        break;

    case 'X':
        if (ctrl) { copySelectionToClipboard(); deleteSelection(); }
        break;

    case 'V':
        if (ctrl) pasteFromClipboard();
        break;

    default:
        break;
    }
}

void TextField::onMouseDown(i32 x, i32 y) {
    (void)y;
    m_caret = m_selAnchor = indexFromX(x);
    m_blink = 0.0;
}

void TextField::onMouseDrag(i32 x, i32 y) {
    (void)y;
    m_caret = indexFromX(x);   // el ancla se queda donde empezo el clic
    m_blink = 0.0;
}

void TextField::update(f64 dt) {
    if (!m_focused) return;
    m_blink += dt;
    if (m_blink >= kBlinkPeriod) m_blink -= kBlinkPeriod;
}

void TextField::render(Framebuffer& fb) const {
    const bool foc = m_focused;

    fb.fillRect(m_rect, foc ? m_style.bgFocused : m_style.bg);
    fb.drawRect(m_rect, foc ? m_style.borderFocused : m_style.border, foc ? 2 : 1);

    const i32 scale = m_style.textScale;
    const i32 textH = font5x7::kGlyphH * scale;
    const i32 ty    = m_rect.y + (m_rect.h - textH) / 2;
    const i32 tx    = m_rect.x + kPadX;

    // Region util para el texto, dentro de los margenes.
    const i32 clipL = m_rect.x + kPadX;
    const i32 clipR = m_rect.x + m_rect.w - kPadX;

    const std::string vis = visibleText();

    if (vis.empty() && !foc && !m_placeholder.empty()) {
        font5x7::drawTextUtf8(fb, tx, ty, m_placeholder.c_str(), m_style.placeholder, scale);
        return;
    }

    // Mantener el cursor a la vista desplazando el texto.
    i32 scroll = m_scrollPx;
    const i32 caretX = xFromIndex(m_caret);
    const i32 visW   = clipR - clipL;
    if (caretX - scroll > visW)  scroll = caretX - visW;
    if (caretX - scroll < 0)     scroll = caretX;
    if (scroll < 0)              scroll = 0;
    const_cast<TextField*>(this)->m_scrollPx = scroll;

    // Fondo de la seleccion, recortado a la zona visible.
    if (hasSelection()) {
        const i32 sx = clipL + xFromIndex(selStart()) - scroll;
        const i32 ex = clipL + xFromIndex(selEnd())   - scroll;
        const i32 x0 = std::max(sx, clipL);
        const i32 x1 = std::min(ex, clipR);
        if (x1 > x0)
            fb.fillRect({x0, ty - 2, x1 - x0, textH + 4}, m_style.selection);
    }

    // Texto caracter a caracter: permite recortar por los margenes sin
    // depender de un clip global en el framebuffer. Se recorre por caracteres
    // UTF-8, no por bytes, para no partir un acento.
    const i32 adv = advance(scale);
    i32 idx = 0;
    for (const char* p = vis.c_str(); *p; ++idx) {
        const char* charStart = p;
        const u32 cp = font5x7::nextUtf8(p);
        if (cp == 0) break;

        const i32 cx = clipL + idx * adv - scroll;
        if (cx + adv <= clipL) continue;   // aun a la izquierda del margen
        if (cx >= clipR) break;            // ya fuera por la derecha

        // Copia del caracter completo (1 a 3 bytes) para dibujarlo suelto.
        char s[5] = {};
        const size_t len = static_cast<size_t>(p - charStart);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];
        font5x7::drawTextUtf8(fb, cx, ty, s, m_style.text, scale);
    }

    // Cursor: visible durante la primera mitad del ciclo de parpadeo.
    if (foc && m_blink < kBlinkPeriod / 2.0) {
        const i32 cx = clipL + caretX - scroll;
        if (cx >= clipL && cx < clipR)
            fb.fillRect({cx, ty - 2, std::max(1, scale), textH + 4}, m_style.caret);
    }
}

} // namespace ludora
