#include "ui/TextField.h"
#include "app/Font5x7.h"
#include "app/TextRender.h"
#include "core/Types.h"
#include "render/Framebuffer.h"
#include <algorithm>
#include <vector>
#include <windows.h>

namespace ludora {

namespace {
constexpr i32 kPadX = 8;          // margen interior horizontal
constexpr i32 kPadY = 6;          // margen superior cuando el texto va arriba
constexpr f64 kBlinkPeriod = 1.0; // ciclo completo del cursor

/// true si el caracter separa palabras. Lo que no es letra, digito ni guion
/// bajo corta: es como se comporta cualquier editor al saltar con Ctrl o al
/// seleccionar una palabra con doble clic.
///
/// Los bytes >= 0x80 cuentan como letra: son la continuacion de una
/// acentuada o una enye en UTF-8, y tratarlos como separador partiria
/// "mañana" por la mitad.
bool esSeparadorChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) return false;
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_');
}

// Aqui habia un `advance(scale)` con un paso fijo por caracter, heredado de
// cuando la fuente era bitmap y monoespaciada. La del sistema es proporcional:
// cada letra se mide con font5x7::measureUtf8 y la pluma avanza con ese ancho.

/// Alto de la letra que de verdad se va a dibujar.
///
/// Por el mismo motivo que el avance: `font5x7::kGlyphH` son los 7 px de la
/// fuente bitmap. Centrar el texto del campo con ese valor, teniendo la del
/// sistema que es mas alta, dejaba las letras pegadas al borde de abajo.
i32 altoTexto(i32 scale) {
    return textr::available() ? textr::glyphHeight(scale)
                              : font5x7::kGlyphH * scale;
}

/// Donde cae cada caracter al repartir el texto en filas.
struct Hueco {
    i32    x    = 0;   // pixel donde empieza el caracter
    i32    fila = 0;   // fila en la que acaba
    i32    ancho = 0;  // lo que mide dibujado
    size_t byte = 0;   // offset en la cadena, por si hace falta
};

/// Reparte `txt` en filas ajustando AL ANCHO Y POR PALABRAS.
///
/// Esta funcion es la unica que decide donde se corta una linea. Dibujar y
/// situar el cursor con un clic tienen que repartir el texto EXACTAMENTE
/// igual: con dos copias de la regla, la de abajo acababa discrepando de la de
/// arriba y el cursor caia en una letra distinta de la que se veia.
///
/// EL AJUSTE ES POR PALABRA, no por caracter. Antes se cortaba en seco al
/// llegar al borde, asi que una palabra escrita al final de la linea se partia
/// por la mitad ("extraordina" / "rio"). Ahora, si la palabra que se esta
/// escribiendo no cabe entera, baja ella entera a la linea siguiente.
///
/// Una palabra mas larga que la caja no cabe en ninguna fila: esa SI se parte
/// por caracter, porque la alternativa es dejarla fuera de la vista o una fila
/// vacia tras otra.
///
/// `huecos` recibe una entrada por caracter mas una final, que es donde va el
/// cursor al estar al final del texto. Devuelve el numero de filas usadas.
i32 repartirEnFilas(const std::string& txt, i32 scale, i32 x0, i32 ancho,
                    std::vector<Hueco>& huecos) {
    huecos.clear();

    i32 fila = 0;
    i32 x    = x0;

    // Inicio de la palabra que se esta midiendo: su primer caracter y su
    // posicion en el vector, para poder bajarla entera si no cabe.
    size_t palIni   = 0;      // indice en `huecos` del primer caracter
    i32    palX     = x0;     // x donde empezo la palabra
    bool   enPalabra = false;

    const char* p = txt.c_str();
    for (;;) {
        const char* charStart = p;
        const size_t off = static_cast<size_t>(charStart - txt.c_str());

        const u32 cp = font5x7::nextUtf8(p);
        if (cp == 0) {
            // Hueco final: donde se pinta el cursor al final del texto.
            huecos.push_back(Hueco{ x, fila, 0, off });
            break;
        }

        if (cp == '\n') {
            huecos.push_back(Hueco{ x, fila, 0, off });
            ++fila;
            x = x0;
            enPalabra = false;
            continue;
        }

        char s[5] = {};
        const size_t len = static_cast<size_t>(p - charStart);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];
        const i32 w = font5x7::measureUtf8(s, scale);

        // Un espacio cierra la palabra en curso: lo que venga detras empieza
        // una nueva, y es ahi donde se puede cortar la linea sin partir nada.
        const bool separa = (cp == ' ' || cp == '\t');
        if (separa) {
            enPalabra = false;
        } else if (!enPalabra) {
            enPalabra = true;
            palIni    = huecos.size();
            palX      = x;
        }

        if (x + w > x0 + ancho && x > x0) {
            // No cabe. Si estamos a mitad de una palabra, baja LA PALABRA
            // ENTERA: se reubican los caracteres ya colocados de esta palabra
            // al principio de la fila siguiente.
            //
            // Solo si esa palabra cabe en una fila vacia. Si ni siquiera eso
            // (una palabra mas larga que la caja), bajarla no arreglaria nada
            // y dejaria filas vacias encadenadas: se parte por caracter.
            const i32 anchoPalabra = (x - palX) + w;
            const bool cabeSola    = anchoPalabra <= ancho;

            if (enPalabra && palIni < huecos.size() && cabeSola) {
                ++fila;
                i32 nx = x0;
                for (size_t k = palIni; k < huecos.size(); ++k) {
                    huecos[k].x    = nx;
                    huecos[k].fila = fila;
                    nx += huecos[k].ancho;
                }
                x = nx;
            } else {
                // O es una palabra mas larga que la caja (hay que partirla),
                // o el corte cae justo entre palabras: salto normal.
                ++fila;
                x = x0;
                if (enPalabra) { palIni = huecos.size(); palX = x; }
            }
        }

        huecos.push_back(Hueco{ x, fila, w, off });
        x += w;
    }

    return fila + 1;
}
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
    // Ancho REAL de los `idx` primeros caracteres. La fuente del sistema es
    // proporcional, asi que multiplicar por un paso fijo descuadraba el cursor,
    // la seleccion y el desplazamiento respecto a las letras dibujadas.
    const std::string vis = visibleText();
    const i32 scale = m_style.textScale;
    i32 px = 0;
    size_t n = 0;
    for (const char* p = vis.c_str(); *p && n < idx; ++n) {
        const char* charStart = p;
        if (font5x7::nextUtf8(p) == 0) break;
        char s[5] = {};
        const size_t len = static_cast<size_t>(p - charStart);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];
        px += font5x7::measureUtf8(s, scale);
    }
    return px;
}

size_t TextField::indexFromX(i32 x) const {
    // Coordenada relativa al inicio del texto, compensando el scroll.
    const i32 rel = x - (m_rect.x + kPadX) + m_scrollPx;
    if (rel <= 0) return 0;

    // Se avanza letra a letra hasta pasar el punto; gana el hueco mas cercano,
    // no siempre el de la izquierda.
    const std::string vis = visibleText();
    const i32 scale = m_style.textScale;
    i32 px = 0;
    size_t idx = 0;
    for (const char* p = vis.c_str(); *p; ++idx) {
        const char* charStart = p;
        if (font5x7::nextUtf8(p) == 0) break;
        char s[5] = {};
        const size_t len = static_cast<size_t>(p - charStart);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];
        const i32 ancho = font5x7::measureUtf8(s, scale);
        if (rel < px + ancho / 2) return idx;
        px += ancho;
    }
    return std::min(idx, charCount());
}

size_t TextField::indexFromPoint(i32 x, i32 y) const {
    if (!m_style.multiline) return indexFromX(x);

    // Se recorre el texto repartiendolo igual que al dibujar, y se devuelve el
    // indice del hueco mas cercano al punto. Recorrer es mas fiable que
    // calcular: asi el reparto de filas vive en un solo sitio y el cursor no
    // puede acabar en una fila distinta de la que se ve.
    const i32 scale  = m_style.textScale;
    const i32 textH  = altoTexto(scale);
    const i32 clipL  = m_rect.x + kPadX;
    const i32 clipR  = m_rect.x + m_rect.w - kPadX;
    const i32 ty     = m_style.alignTop ? m_rect.y + kPadY
                                        : m_rect.y + (m_rect.h - textH) / 2;
    const i32 anchoU = std::max(1, clipR - clipL);
    const i32 lineaH = textH + 3;

    // Fila pedida por el clic, acotada a cero por arriba.
    const i32 filaObj = std::max(0, (y - ty) / lineaH);

    // Se reparte con la MISMA funcion que dibuja, asi que el cursor cae
    // siempre en el caracter que se ve, tambien cuando una palabra bajo entera
    // de linea por el ajuste.
    const std::string vis = visibleText();
    std::vector<Hueco> huecos;
    repartirEnFilas(vis, scale, clipL, anchoU, huecos);
    if (huecos.empty()) return 0;

    const size_t total = charCount();

    // Dentro de la fila pedida: el primer hueco cuyo centro ya pasa de la x.
    // Comparar contra el CENTRO y no contra el inicio es lo que hace que
    // pinchar en la mitad derecha de una letra ponga el cursor detras de ella,
    // como en cualquier editor.
    size_t ultimoDeFila = 0;
    bool   hayFila = false;

    for (size_t i = 0; i < huecos.size(); ++i) {
        if (huecos[i].fila != filaObj) continue;
        hayFila = true;
        const i32 centro = huecos[i].x + huecos[i].ancho / 2;
        if (x < centro) return std::min(i, total);
        ultimoDeFila = i;
    }

    // La fila existe pero el clic cayo a su derecha: el cursor va al final de
    // esa fila, o sea en el hueco siguiente al ultimo caracter que contiene.
    if (hayFila) return std::min(ultimoDeFila + 1, total);

    // El clic cayo por debajo de todo el texto: al final.
    return total;
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

    // El salto de linea es el unico codigo de control que se acepta, y solo
    // en modo multilinea: es lo que separa parrafos en un texto largo. No
    // tiene glifo, asi que se deja pasar antes de exigirlo.
    const bool esSalto = (cp == '\n');
    if (esSalto && !m_style.multiline) return;
    if (!esSalto) {
        if (cp < 32 || cp == 127) return;           // codigos de control fuera
        if (!font5x7::glyphUnicode(cp)) return;     // sin glifo no se acepta
    }

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

size_t TextField::wordLeft(size_t desde) const {
    // Primero se saltan los separadores que haya justo detras, y luego la
    // palabra entera: con una sola pasada, Ctrl+Izquierda sobre " hola" se
    // quedaba en el espacio en vez de llegar al principio de la palabra.
    size_t i = desde;
    while (i > 0 && esSeparadorChar(m_text[byteOfChar(i - 1)])) --i;
    while (i > 0 && !esSeparadorChar(m_text[byteOfChar(i - 1)])) --i;
    return i;
}

size_t TextField::wordRight(size_t desde) const {
    const size_t n = charCount();
    size_t i = desde;
    while (i < n && !esSeparadorChar(m_text[byteOfChar(i)])) ++i;
    while (i < n && esSeparadorChar(m_text[byteOfChar(i)])) ++i;
    return i;
}

size_t TextField::lineStart(size_t desde) const {
    size_t i = desde;
    while (i > 0 && m_text[byteOfChar(i - 1)] != '\n') --i;
    return i;
}

size_t TextField::lineEnd(size_t desde) const {
    const size_t n = charCount();
    size_t i = desde;
    while (i < n && m_text[byteOfChar(i)] != '\n') ++i;
    return i;
}

void TextField::onKey(u32 vk, bool shift, bool ctrl) {
    if (!m_focused) return;
    m_blink = 0.0;

    switch (vk) {
    // Ctrl salta de palabra en palabra, como en cualquier editor. Sin esto,
    // recorrer una descripcion de 5000 caracteres era tecla a tecla.
    case VK_LEFT:
        if (ctrl)            m_caret = wordLeft(m_caret);
        else if (hasSelection() && !shift) m_caret = selStart();
        else if (m_caret > 0) --m_caret;
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_RIGHT:
        if (ctrl)            m_caret = wordRight(m_caret);
        else if (hasSelection() && !shift) m_caret = selEnd();
        else if (m_caret < charCount()) ++m_caret;
        if (!shift) m_selAnchor = m_caret;
        break;

    // Arriba y abajo en las cajas de varias lineas: se mantiene la columna,
    // que es lo que se espera al recorrer un texto largo.
    case VK_UP:
    case VK_DOWN: {
        if (!m_style.multiline) {
            // En una linea, arriba/abajo van a los extremos.
            m_caret = (vk == VK_UP) ? 0 : charCount();
            if (!shift) m_selAnchor = m_caret;
            break;
        }
        const size_t ini = lineStart(m_caret);
        const size_t col = m_caret - ini;
        if (vk == VK_UP) {
            if (ini == 0) { m_caret = 0; }
            else {
                const size_t iniPrev = lineStart(ini - 1);
                const size_t finPrev = ini - 1;
                m_caret = std::min(iniPrev + col, finPrev);
            }
        } else {
            const size_t fin = lineEnd(m_caret);
            if (fin >= charCount()) { m_caret = charCount(); }
            else {
                const size_t iniSig = fin + 1;
                m_caret = std::min(iniSig + col, lineEnd(iniSig));
            }
        }
        if (!shift) m_selAnchor = m_caret;
        break;
    }

    // Inicio y fin de LINEA; con Ctrl, de todo el texto. En una caja de una
    // sola linea las dos cosas coinciden.
    case VK_HOME:
        m_caret = ctrl ? 0 : lineStart(m_caret);
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_END:
        m_caret = ctrl ? charCount() : lineEnd(m_caret);
        if (!shift) m_selAnchor = m_caret;
        break;

    case VK_BACK:
        if (hasSelection()) {
            deleteSelection();
        } else if (ctrl && m_caret > 0) {
            // Ctrl+Retroceso borra la palabra entera de la izquierda.
            const size_t ini = wordLeft(m_caret);
            const size_t b0 = byteOfChar(ini);
            const size_t b1 = byteOfChar(m_caret);
            m_text.erase(b0, b1 - b0);
            m_caret = ini;
            m_selAnchor = m_caret;
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
        } else if (ctrl && m_caret < charCount()) {
            // Ctrl+Supr borra la palabra entera de la derecha.
            const size_t fin = wordRight(m_caret);
            const size_t b0 = byteOfChar(m_caret);
            const size_t b1 = byteOfChar(fin);
            m_text.erase(b0, b1 - b0);
            m_selAnchor = m_caret;
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
    const size_t idx = indexFromPoint(x, y);

    // --- doble y triple clic ---
    //
    // Dos clics seguidos y cerca seleccionan la PALABRA; tres, la LINEA
    // entera. Es como se selecciona texto en cualquier parte, y sin esto la
    // unica via era arrastrar con precision sobre una fuente de 5 pixeles.
    //
    // El limite de tiempo es el del sistema, y el de distancia evita que dos
    // clics en sitios distintos cuenten como doble por ir rapido.
    const f64 ahora = m_clockSec;
    const bool seguido = (ahora - m_lastClickAt) < kDobleClic &&
                         std::abs(x - m_lastClickX) <= 4 &&
                         std::abs(y - m_lastClickY) <= 4;
    m_clicks = seguido ? (m_clicks + 1) : 1;
    m_lastClickAt = ahora;
    m_lastClickX = x;
    m_lastClickY = y;

    if (m_clicks >= 3) {
        // Triple: la linea entera.
        m_selAnchor = lineStart(idx);
        m_caret     = lineEnd(idx);
    } else if (m_clicks == 2) {
        // Doble: la palabra bajo el cursor. wordLeft/wordRight saltan
        // separadores, asi que aqui se acotan a la palabra en si.
        const size_t n = charCount();
        size_t i = idx;
        while (i > 0 && !esSeparadorChar(m_text[byteOfChar(i - 1)])) --i;
        size_t j = idx;
        while (j < n && !esSeparadorChar(m_text[byteOfChar(j)])) ++j;
        if (i == j) {           // se pulso sobre un separador: se coge el
            if (j < n) ++j;     // propio separador, no una seleccion vacia
        }
        m_selAnchor = i;
        m_caret     = j;
    } else {
        m_caret = m_selAnchor = idx;
    }
    m_blink = 0.0;
}

void TextField::onMouseDrag(i32 x, i32 y) {
    m_caret = indexFromPoint(x, y);   // el ancla se queda donde empezo el clic
    m_blink = 0.0;
}

void TextField::update(f64 dt) {
    // El reloj corre SIEMPRE, tenga el foco o no: es el que fecha los clics,
    // y si se parase al perder el foco, el primer clic al volver se tomaria
    // como doble por tener la marca de tiempo congelada.
    m_clockSec += dt;

    if (!m_focused) return;
    m_blink += dt;
    if (m_blink >= kBlinkPeriod) m_blink -= kBlinkPeriod;
}

void TextField::render(Framebuffer& fb) const {
    const bool foc = m_focused;

    fb.fillRect(m_rect, foc ? m_style.bgFocused : m_style.bg);
    fb.drawRect(m_rect, foc ? m_style.borderFocused : m_style.border, foc ? 2 : 1);

    const i32 scale = m_style.textScale;
    const i32 textH = altoTexto(scale);
    // Centrado en vertical para una linea; pegado arriba cuando el campo es
    // una caja alta, donde el texto debe arrancar en el borde superior.
    const i32 ty    = m_style.alignTop ? m_rect.y + kPadY
                                       : m_rect.y + (m_rect.h - textH) / 2;
    const i32 tx    = m_rect.x + kPadX;

    // Region util para el texto, dentro de los margenes.
    const i32 clipL = m_rect.x + kPadX;
    const i32 clipR = m_rect.x + m_rect.w - kPadX;

    const std::string vis = visibleText();

    if (vis.empty() && !foc && !m_placeholder.empty()) {
        font5x7::drawTextUtf8(fb, tx, ty, m_placeholder.c_str(), m_style.placeholder, scale);
        return;
    }

    // ----------------------------------------------------------------------
    // Modo multilinea: el texto se reparte en filas, cortando en los saltos
    // de linea y ajustando al ancho del campo. Es lo que necesita un texto
    // largo (un reglamento, una descripcion); el modo de una linea de mas
    // abajo sigue siendo el correcto para un campo de formulario.
    // ----------------------------------------------------------------------
    if (m_style.multiline) {
        const i32 anchoU = std::max(1, clipR - clipL);
        const i32 lineaH = textH + 3;

        // El reparto en filas lo hace repartirEnFilas(), la MISMA funcion que
        // usa el clic para saber donde cae el cursor. Teniendo la regla en un
        // solo sitio no pueden discrepar.
        std::vector<Hueco> huecos;
        repartirEnFilas(vis, scale, clipL, anchoU, huecos);

        const i32 limiteY = m_rect.y + m_rect.h - kPadY;

        // Los caracteres: cada uno en el hueco que le toco.
        const char* p = vis.c_str();
        for (size_t i = 0; i + 1 < huecos.size(); ++i) {
            const char* charStart = p;
            const u32 cp = font5x7::nextUtf8(p);
            if (cp == 0) break;
            if (cp == '\n') continue;           // no se dibuja, solo separa

            char s[5] = {};
            const size_t len = static_cast<size_t>(p - charStart);
            for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];

            const i32 cy = ty + huecos[i].fila * lineaH;
            // Fuera del alto util no se dibuja; el reparto ya esta hecho, asi
            // que el cursor sigue cayendo en su sitio.
            if (cy + textH > limiteY) continue;

            font5x7::drawTextUtf8(fb, huecos[i].x, cy, s, m_style.text, scale);
        }

        // El cursor, en el hueco de su indice.
        if (foc && m_blink < kBlinkPeriod / 2.0 && !huecos.empty()) {
            const size_t k = std::min(m_caret, huecos.size() - 1);
            const i32 cpx = huecos[k].x;
            const i32 cpy = ty + huecos[k].fila * lineaH;
            if (cpy + textH <= limiteY)
                fb.fillRect({cpx, cpy - 1, std::max(1, scale), textH + 2},
                            m_style.caret);
        }
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
    // La pluma avanza por el ancho REAL de cada letra: con un paso fijo, una
    // fuente proporcional deja huecos irregulares entre caracteres.
    i32 px = 0;
    for (const char* p = vis.c_str(); *p; ) {
        const char* charStart = p;
        const u32 cp = font5x7::nextUtf8(p);
        if (cp == 0) break;

        // Copia del caracter completo (1 a 3 bytes) para dibujarlo suelto.
        char s[5] = {};
        const size_t len = static_cast<size_t>(p - charStart);
        for (size_t k = 0; k < len && k < 4; ++k) s[k] = charStart[k];
        const i32 ancho = font5x7::measureUtf8(s, scale);

        const i32 cx = clipL + px - scroll;
        px += ancho;

        if (cx + ancho <= clipL) continue;   // aun a la izquierda del margen
        if (cx >= clipR) break;              // ya fuera por la derecha

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
