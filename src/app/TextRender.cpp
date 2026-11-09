#include "app/TextRender.h"
#include "app/Font5x7.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace ludora {
namespace textr {

namespace {

/// Un glifo ya rasterizado: su mapa de cobertura y donde colocarlo.
///
/// `cov` guarda un byte por pixel (0 = fondo, 255 = trazo). Se separa del
/// color a proposito: el mismo glifo sirve para cualquier color, asi que la
/// cache no se multiplica por cada tono que use la interfaz.
struct Glyph {
    std::vector<u8> cov;
    i32 w = 0, h = 0;      // tamano del mapa
    i32 bearingX = 0;      // desplazamiento desde el origen del cursor
    i32 bearingY = 0;      // desde la linea base hacia arriba
    i32 advance  = 0;      // cuanto avanza el cursor tras dibujarlo
};

/// Recursos de GDI de un tamano concreto, creados una vez.
struct FontSize {
    HFONT  font = nullptr;
    i32    ascent = 0;     // de la linea base al tope
    i32    height = 0;     // alto de linea, con el hueco de separacion
    /// Alto de la LETRA sola, sin el hueco entre renglones.
    ///
    /// Los dos se necesitan y no son lo mismo: para saltar de renglon hay que
    /// contar el hueco, pero para CENTRAR una letra en una caja no, porque
    /// ese hueco no es tinta. Usar `height` al centrar empujaba todos los
    /// rotulos hacia abajo.
    i32    glyphH = 0;
    std::unordered_map<u32, Glyph> cache;
};

/// Contexto de dibujado compartido. Se crea perezosamente en el primer uso y
/// se destruye en shutdown(): crear un DC por glifo seria carisimo.
struct Ctx {
    HDC     dc  = nullptr;
    HBITMAP bmp = nullptr;
    u32*    bits = nullptr;
    i32     bmpW = 0, bmpH = 0;
    std::unordered_map<i32, FontSize> sizes;
    bool    ok = false;
    bool    tried = false;
};

Ctx& ctx() {
    static Ctx c;
    return c;
}

/// Prepara el DC en memoria donde GDI dibuja cada glifo.
bool ensureCtx() {
    Ctx& c = ctx();
    if (c.tried) return c.ok;
    c.tried = true;

    c.dc = CreateCompatibleDC(nullptr);
    if (!c.dc) return false;

    // Lienzo de sobra para el glifo mas grande que se vaya a pedir.
    c.bmpW = 128;
    c.bmpH = 128;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = c.bmpW;
    bi.bmiHeader.biHeight      = -c.bmpH;   // negativo = filas de arriba abajo
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    c.bmp = CreateDIBSection(c.dc, &bi, DIB_RGB_COLORS,
                             reinterpret_cast<void**>(&c.bits), nullptr, 0);
    if (!c.bmp || !c.bits) { DeleteDC(c.dc); c.dc = nullptr; return false; }

    SelectObject(c.dc, c.bmp);
    SetBkMode(c.dc, OPAQUE);
    c.ok = true;
    return true;
}

/// Crea (o recupera) los recursos para un tamano de fuente.
FontSize* sizeFor(i32 px) {
    Ctx& c = ctx();
    if (!ensureCtx()) return nullptr;

    const auto it = c.sizes.find(px);
    if (it != c.sizes.end()) return &it->second;

    FontSize fs;
    // CLEARTYPE_QUALITY da el suavizado que hace que la letra no se vea
    // dentada. Segoe UI es la fuente de interfaz de Windows; si no esta, el
    // sistema sustituye por la mas parecida en vez de fallar.
    fs.font = CreateFontW(
        -px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!fs.font) return nullptr;

    SelectObject(c.dc, fs.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(c.dc, &tm);
    fs.ascent = tm.tmAscent;
    fs.height = tm.tmHeight + tm.tmExternalLeading;
    fs.glyphH = tm.tmHeight;   // sin el hueco entre renglones

    return &c.sizes.emplace(px, std::move(fs)).first->second;
}

/// Rasteriza un caracter y lo guarda en la cache del tamano.
const Glyph* glyphFor(FontSize& fs, u32 cp) {
    const auto it = fs.cache.find(cp);
    if (it != fs.cache.end()) return &it->second;

    Ctx& c = ctx();
    SelectObject(c.dc, fs.font);

    wchar_t w[2] = { static_cast<wchar_t>(cp), 0 };

    // Cuanto ocupa y cuanto avanza el cursor. ABC da el espaciado real de la
    // fuente, que es lo que separa bien las letras entre si.
    ABC abc{};
    SIZE sz{};
    GetTextExtentPoint32W(c.dc, w, 1, &sz);
    const bool tieneAbc = GetCharABCWidthsW(c.dc, cp, cp, &abc) != FALSE;

    Glyph g;
    g.advance  = tieneAbc ? (abc.abcA + static_cast<i32>(abc.abcB) + abc.abcC)
                          : sz.cx;

    // ⭐ EL BEARING YA VA DENTRO DEL MAPA: NO SE SUMA OTRA VEZ.
    //
    // ExtTextOut dibuja la letra en su sitio dentro del rectangulo, es decir
    // CON su hueco izquierdo incluido. Guardar ademas `abcA` como bearing y
    // volver a sumarlo al colocar el glifo contaba ese hueco dos veces: las
    // letras de bearing negativo (la 'j', la 'f') se metian en la anterior y
    // las de bearing positivo se separaban de mas. Era el "Tuj uego" de los
    // parrafos.
    //
    // El unico desplazamiento que hace falta es el MARGEN que se deja al
    // rasterizar, para que un trazo que sobresale por la izquierda no se
    // recorte. Se dibuja corrido ese margen y se descuenta al colocar.
    const i32 margen = (tieneAbc && abc.abcA < 0) ? -abc.abcA + 1 : 1;
    g.bearingX = -margen;
    g.bearingY = fs.ascent;

    // SIZE usa LONG; se convierte antes de comparar para no depender de que
    // coincida con i32 en todas las configuraciones.
    const i32 gw = std::min(static_cast<i32>(sz.cx) + margen + 4, c.bmpW);
    const i32 gh = std::min(static_cast<i32>(sz.cy) + 2, c.bmpH);
    if (gw <= 0 || gh <= 0) return &fs.cache.emplace(cp, std::move(g)).first->second;

    // Se dibuja BLANCO sobre NEGRO: el brillo del resultado es directamente
    // la cobertura del trazo, sin tener que separar canales.
    const RECT r{ 0, 0, gw, gh };
    SetBkColor(c.dc, RGB(0, 0, 0));
    SetTextColor(c.dc, RGB(255, 255, 255));
    ExtTextOutW(c.dc, margen, 0, ETO_OPAQUE, &r, w, 1, nullptr);

    g.w = gw;
    g.h = gh;
    g.cov.resize(static_cast<size_t>(gw) * gh);

    for (i32 y = 0; y < gh; ++y) {
        for (i32 x = 0; x < gw; ++x) {
            const u32 p = c.bits[static_cast<size_t>(y) * c.bmpW + x];
            // ClearType deja distinto valor por canal; se promedia para
            // obtener una cobertura en escala de grises. Sin esto, el texto
            // mostraria franjas de color en los bordes.
            const u32 rr = (p >> 16) & 0xFF;
            const u32 gg = (p >> 8)  & 0xFF;
            const u32 bb =  p        & 0xFF;
            g.cov[static_cast<size_t>(y) * gw + x] =
                static_cast<u8>((rr + gg + bb) / 3);
        }
    }

    return &fs.cache.emplace(cp, std::move(g)).first->second;
}

} // namespace

i32 pixelSize(i32 scale) {
    // La escala venia de la fuente bitmap, donde el glifo medía 7 px de alto
    // por unidad. Estos tamanos mantienen proporciones parecidas para que las
    // pantallas ya maquetadas no se descoloquen.
    switch (scale) {
        case 1:  return 12;
        case 2:  return 17;
        case 3:  return 23;
        default: return 12 + 5 * std::max(1, scale) - 5;
    }
}

bool available() { return ensureCtx(); }

i32 lineHeight(i32 scale) {
    FontSize* fs = sizeFor(pixelSize(scale));
    return fs ? fs->height : font5x7::kGlyphH * std::max(1, scale) + 3;
}

i32 glyphHeight(i32 scale) {
    FontSize* fs = sizeFor(pixelSize(scale));
    return fs ? fs->glyphH : font5x7::kGlyphH * std::max(1, scale);
}

i32 measure(const char* utf8, i32 scale) {
    if (!utf8 || !*utf8) return 0;
    FontSize* fs = sizeFor(pixelSize(scale));
    if (!fs) return font5x7::measureUtf8(utf8, scale);   // sin GDI, la bitmap

    i32 ancho = 0;
    for (const char* p = utf8; *p; ) {
        const u32 cp = font5x7::nextUtf8(p);
        if (cp == 0 || cp == '\n') break;
        const Glyph* g = glyphFor(*fs, cp);
        if (g) ancho += g->advance;
    }
    return ancho;
}

void draw(Framebuffer& fb, i32 x, i32 y, const char* utf8, Color color, i32 scale) {
    if (!utf8 || !*utf8) return;

    FontSize* fs = sizeFor(pixelSize(scale));
    if (!fs) {                      // sin GDI se recurre a la fuente bitmap
        font5x7::drawTextUtf8(fb, x, y, utf8, color, scale);
        return;
    }

    i32 penX = x;
    i32 penY = y;

    for (const char* p = utf8; *p; ) {
        const u32 cp = font5x7::nextUtf8(p);
        if (cp == 0) break;
        if (cp == '\n') { penX = x; penY += fs->height; continue; }

        const Glyph* g = glyphFor(*fs, cp);
        if (!g) continue;

        // El mapa se dibujo con el origen en la esquina; al colocarlo se
        // descuenta el bearing para que las letras se alineen entre si.
        const i32 gx = penX + g->bearingX;

        for (i32 yy = 0; yy < g->h; ++yy) {
            const i32 dy = penY + yy;
            if (dy < 0 || dy >= fb.height()) continue;
            for (i32 xx = 0; xx < g->w; ++xx) {
                const u8 cov = g->cov[static_cast<size_t>(yy) * g->w + xx];
                if (cov == 0) continue;      // fondo: nada que mezclar
                const i32 dx = gx + xx;
                if (dx < 0 || dx >= fb.width()) continue;

                // La cobertura modula el alfa del color pedido: asi el borde
                // se funde con el fondo y deja de verse dentado.
                Color c = color;
                c.a = static_cast<u8>((static_cast<u32>(color.a) * cov) / 255);
                fb.blendPixel(dx, dy, c);
            }
        }
        penX += g->advance;
    }
}

void shutdown() {
    Ctx& c = ctx();
    for (auto& par : c.sizes)
        if (par.second.font) DeleteObject(par.second.font);
    c.sizes.clear();

    if (c.bmp) { DeleteObject(c.bmp); c.bmp = nullptr; }
    if (c.dc)  { DeleteDC(c.dc);      c.dc  = nullptr; }
    c.bits = nullptr;
    c.ok = false;
    c.tried = false;
}

} // namespace textr
} // namespace ludora
