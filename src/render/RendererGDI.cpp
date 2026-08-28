#include "render/RendererGDI.h"
#include <algorithm>

namespace ludora {

RendererGDI::~RendererGDI() { shutdown(); }

bool RendererGDI::init(HWND hwnd) {
    if (!hwnd) return false;
    m_hwnd = hwnd;

    // DIB top-down (altura negativa) para que la fila 0 sea la de arriba,
    // igual que el orden natural del framebuffer.
    ZeroMemory(&m_bmi, sizeof(m_bmi));
    m_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    m_bmi.bmiHeader.biPlanes      = 1;
    m_bmi.bmiHeader.biBitCount    = 32;
    m_bmi.bmiHeader.biCompression = BI_RGB;
    return true;
}

void RendererGDI::shutdown() {
    if (m_memDC) {
        if (m_oldBmp) SelectObject(m_memDC, m_oldBmp);
        if (m_memBmp) DeleteObject(m_memBmp);
        DeleteDC(m_memDC);
    }
    m_memDC  = nullptr;
    m_memBmp = nullptr;
    m_oldBmp = nullptr;
    m_bbW = m_bbH = 0;
    m_hwnd = nullptr;
}

void RendererGDI::resizeTarget(i32 w, i32 h) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (w == m_fb.width() && h == m_fb.height()) return;

    m_fb.resize(w, h);
    m_bmi.bmiHeader.biWidth  =  w;
    m_bmi.bmiHeader.biHeight = -h;   // negativo = top-down
}

void RendererGDI::ensureBackBuffer(i32 w, i32 h) {
    if (m_memDC && w == m_bbW && h == m_bbH) return;

    HDC winDC = GetDC(m_hwnd);
    if (!winDC) return;

    if (m_memDC) {
        if (m_oldBmp) SelectObject(m_memDC, m_oldBmp);
        if (m_memBmp) DeleteObject(m_memBmp);
        DeleteDC(m_memDC);
        m_memDC = nullptr; m_memBmp = nullptr; m_oldBmp = nullptr;
    }

    m_memDC  = CreateCompatibleDC(winDC);
    m_memBmp = CreateCompatibleBitmap(winDC, w, h);
    if (m_memDC && m_memBmp) {
        m_oldBmp = static_cast<HBITMAP>(SelectObject(m_memDC, m_memBmp));
        m_bbW = w;
        m_bbH = h;
    }
    ReleaseDC(m_hwnd, winDC);
}

void RendererGDI::present(i32 clientW, i32 clientH) {
    if (!m_hwnd || !m_fb.valid() || clientW <= 0 || clientH <= 0) return;

    ensureBackBuffer(clientW, clientH);
    if (!m_memDC) return;

    // HALFTONE requiere origen de pincel fijado para no dejar artefactos.
    if (m_smooth) {
        SetStretchBltMode(m_memDC, HALFTONE);
        SetBrushOrgEx(m_memDC, 0, 0, nullptr);
    } else {
        SetStretchBltMode(m_memDC, COLORONCOLOR);
    }

    StretchDIBits(m_memDC,
                  0, 0, clientW, clientH,                  // destino: area cliente
                  0, 0, m_fb.width(), m_fb.height(),       // origen: framebuffer
                  m_fb.data(), &m_bmi, DIB_RGB_COLORS, SRCCOPY);

    // Un unico BitBlt al final: sin parpadeo.
    HDC winDC = GetDC(m_hwnd);
    if (winDC) {
        BitBlt(winDC, 0, 0, clientW, clientH, m_memDC, 0, 0, SRCCOPY);
        ReleaseDC(m_hwnd, winDC);
    }
}

} // namespace ludora
