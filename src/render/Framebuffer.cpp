#include "render/Framebuffer.h"
#include <algorithm>
#include <cmath>

namespace ludora {

void Framebuffer::resize(i32 w, i32 h) {
    m_w = std::max(0, w);
    m_h = std::max(0, h);
    m_pixels.assign(static_cast<size_t>(m_w) * m_h, 0u);
}

void Framebuffer::clear(Color c) {
    if (!valid()) return;
    std::fill(m_pixels.begin(), m_pixels.end(), c.packed());
}

void Framebuffer::blendPixel(i32 x, i32 y, Color c) {
    if (x < 0 || y < 0 || x >= m_w || y >= m_h || c.a == 0) return;
    u32& dst = m_pixels[static_cast<size_t>(y) * m_w + x];

    if (c.a == 255) { dst = c.packed(); return; }

    const u32 sa = c.a;
    const u32 ia = 255u - sa;
    const u32 dr = (dst >> 16) & 0xFF;
    const u32 dg = (dst >> 8)  & 0xFF;
    const u32 db =  dst        & 0xFF;

    // +127 redondea al entero mas cercano en la division por 255.
    const u32 r = (c.r * sa + dr * ia + 127) / 255;
    const u32 g = (c.g * sa + dg * ia + 127) / 255;
    const u32 b = (c.b * sa + db * ia + 127) / 255;

    dst = 0xFF000000u | (r << 16) | (g << 8) | b;
}

void Framebuffer::fillRect(Recti r, Color c) {
    if (!valid() || c.a == 0) return;

    // Clip al framebuffer antes de recorrer, no dentro del bucle.
    const i32 x0 = std::max(0, r.x);
    const i32 y0 = std::max(0, r.y);
    const i32 x1 = std::min(m_w, r.x + r.w);
    const i32 y1 = std::min(m_h, r.y + r.h);
    if (x0 >= x1 || y0 >= y1) return;

    if (c.a == 255) {
        const u32 p = c.packed();
        for (i32 y = y0; y < y1; ++y) {
            u32* row = m_pixels.data() + static_cast<size_t>(y) * m_w;
            std::fill(row + x0, row + x1, p);
        }
    } else {
        for (i32 y = y0; y < y1; ++y)
            for (i32 x = x0; x < x1; ++x)
                blendPixel(x, y, c);
    }
}

void Framebuffer::drawRect(Recti r, Color c, i32 thickness) {
    if (thickness <= 0) return;
    const i32 t = std::min(thickness, std::min(r.w, r.h));
    fillRect({r.x,             r.y,             r.w, t          }, c); // arriba
    fillRect({r.x,             r.y + r.h - t,   r.w, t          }, c); // abajo
    fillRect({r.x,             r.y + t,         t,   r.h - 2 * t}, c); // izquierda
    fillRect({r.x + r.w - t,   r.y + t,         t,   r.h - 2 * t}, c); // derecha
}

void Framebuffer::drawLine(i32 x0, i32 y0, i32 x1, i32 y1, Color c) {
    // Bresenham entero, sin divisiones ni flotantes.
    const i32 dx = std::abs(x1 - x0);
    const i32 dy = -std::abs(y1 - y0);
    const i32 sx = (x0 < x1) ? 1 : -1;
    const i32 sy = (y0 < y1) ? 1 : -1;
    i32 err = dx + dy;

    for (;;) {
        blendPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const i32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Framebuffer::fillCircle(i32 cx, i32 cy, i32 radius, Color c) {
    if (radius <= 0) return;
    const i32 r2 = radius * radius;

    const i32 y0 = std::max(0, cy - radius);
    const i32 y1 = std::min(m_h - 1, cy + radius);

    for (i32 y = y0; y <= y1; ++y) {
        const i32 dy = y - cy;
        // Ancho exacto del span para esta fila: evita test por pixel.
        const i32 span = static_cast<i32>(std::sqrt(static_cast<f64>(r2 - dy * dy)));
        const i32 x0 = std::max(0, cx - span);
        const i32 x1 = std::min(m_w - 1, cx + span);
        for (i32 x = x0; x <= x1; ++x)
            blendPixel(x, y, c);
    }
}

void Framebuffer::drawCircle(i32 cx, i32 cy, i32 radius, Color c, i32 thickness) {
    if (radius <= 0 || thickness <= 0) return;

    const f32 rOut = static_cast<f32>(radius) + thickness * 0.5f;
    const f32 rIn  = static_cast<f32>(radius) - thickness * 0.5f;

    const i32 y0 = std::max(0, cy - static_cast<i32>(rOut) - 1);
    const i32 y1 = std::min(m_h - 1, cy + static_cast<i32>(rOut) + 1);
    const i32 x0 = std::max(0, cx - static_cast<i32>(rOut) - 1);
    const i32 x1 = std::min(m_w - 1, cx + static_cast<i32>(rOut) + 1);

    for (i32 y = y0; y <= y1; ++y) {
        const f32 dy = static_cast<f32>(y - cy);
        for (i32 x = x0; x <= x1; ++x) {
            const f32 dx = static_cast<f32>(x - cx);
            const f32 d  = std::sqrt(dx * dx + dy * dy);

            // Cobertura por distancia al anillo: da un borde suave sin
            // necesidad de supermuestreo.
            f32 cov = 0.0f;
            if (d >= rIn - 0.5f && d <= rOut + 0.5f) {
                const f32 fuera = std::min(1.0f, std::max(0.0f, rOut + 0.5f - d));
                const f32 dentro = std::min(1.0f, std::max(0.0f, d - (rIn - 0.5f)));
                cov = std::min(fuera, dentro);
            }
            if (cov <= 0.0f) continue;

            Color cc = c;
            cc.a = static_cast<u8>(c.a * cov);
            blendPixel(x, y, cc);
        }
    }
}

void Framebuffer::blitCircular(i32 cx, i32 cy, i32 radius,
                               const u32* src, i32 srcW, i32 srcH) {
    if (!src || radius <= 0 || srcW <= 0 || srcH <= 0) return;

    // Recorte cuadrado centrado del origen: asi la foto no se deforma al
    // encajarla en un circulo.
    const i32 lado = std::min(srcW, srcH);
    const i32 offX = (srcW - lado) / 2;
    const i32 offY = (srcH - lado) / 2;

    const i32 diam = radius * 2;
    const i32 y0 = std::max(0, cy - radius);
    const i32 y1 = std::min(m_h - 1, cy + radius);
    const i32 x0 = std::max(0, cx - radius);
    const i32 x1 = std::min(m_w - 1, cx + radius);

    const f32 rf = static_cast<f32>(radius);

    for (i32 y = y0; y <= y1; ++y) {
        const f32 dy = static_cast<f32>(y - cy);
        for (i32 x = x0; x <= x1; ++x) {
            const f32 dx = static_cast<f32>(x - cx);
            const f32 d  = std::sqrt(dx * dx + dy * dy);
            if (d > rf) continue;   // fuera del circulo

            // Muestreo por vecino mas proximo, suficiente a este tamano.
            const i32 sx = offX + static_cast<i32>((x - (cx - radius)) * lado / diam);
            const i32 sy = offY + static_cast<i32>((y - (cy - radius)) * lado / diam);
            if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) continue;

            const u32 p = src[static_cast<size_t>(sy) * srcW + sx];
            Color c{ static_cast<u8>((p >> 16) & 0xFF),
                     static_cast<u8>((p >> 8)  & 0xFF),
                     static_cast<u8>( p        & 0xFF), 255 };

            // Suavizado del borde exterior del circulo.
            const f32 cov = std::min(1.0f, std::max(0.0f, rf - d));
            c.a = static_cast<u8>(255 * cov);
            blendPixel(x, y, c);
        }
    }
}

} // namespace ludora
