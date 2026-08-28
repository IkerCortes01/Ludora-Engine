#pragma once
#include "core/Types.h"
#include "platform/Input.h"
#include <windows.h>
#include <string>
#include <functional>

namespace ludora {

struct WindowDesc {
    std::wstring title      = L"Ludora";
    i32  width              = 960;
    i32  height             = 600;
    i32  minWidth           = 320;
    i32  minHeight          = 240;
    bool borderless         = true;   // sin barra de titulo nativa
    bool resizable          = true;
    bool centerOnScreen     = true;
    i32  dragBarHeight      = 36;     // franja superior que arrastra la ventana
    i32  resizeBorder       = 8;      // grosor de la zona de resize en px
};

/// Ventana Win32 borderless: arrastrable y redimensionable sin decoracion nativa.
///
/// El truco esta en WM_NCHITTEST: se le miente a Windows sobre que parte de la
/// ventana esta bajo el cursor (HTCAPTION / HTLEFT / HTBOTTOMRIGHT...), y el
/// gestor de ventanas hace el arrastre y el resize por nosotros -- con snap,
/// Aero Snap y doble-clic-para-maximizar incluidos, gratis.
class Window {
public:
    ~Window();

    bool create(const WindowDesc& desc);
    void destroy();

    /// Procesa la cola de mensajes. Devuelve false cuando hay que salir.
    bool pumpMessages();

    HWND handle() const { return m_hwnd; }
    bool shouldClose() const { return m_shouldClose; }
    void requestClose() { m_shouldClose = true; }

    Size clientSize() const { return m_clientSize; }
    Input& input() { return m_input; }

    // --- control de la ventana ---
    void setTitle(const std::wstring& t);
    void setClientSize(i32 w, i32 h);
    void centerOnScreen();
    void moveBy(i32 dx, i32 dy);
    void setPosition(i32 x, i32 y);
    void toggleMaximize();
    void minimize();
    bool isMaximized() const;

    /// Alterna pantalla completa real (cubre todo el monitor, sin barra de
    /// tareas). Guarda la geometria previa para restaurarla al salir.
    void toggleFullscreen();
    bool isFullscreen() const { return m_fullscreen; }

    /// Opacidad de la ventana completa [0..1] (usa capa WS_EX_LAYERED).
    void setOpacity(f32 alpha);
    f32  opacity() const { return m_opacity; }

    /// Mantener la ventana siempre encima.
    void setTopmost(bool on);
    bool topmost() const { return m_topmost; }

    /// Escala del contenido: el motor renderiza a clientSize/scale y luego
    /// se estira al area cliente. scale > 1 = zoom (menos pixeles logicos).
    void  setContentScale(f32 s);
    f32   contentScale() const { return m_contentScale; }

    /// DPI actual del monitor donde esta la ventana (96 = 100%).
    u32 dpi() const { return m_dpi; }

    /// Se dispara cuando cambia el tamano del area cliente.
    std::function<void(i32, i32)> onResize;

    /// Consulta si un punto del area cliente (px fisicos) debe excluirse del
    /// arrastre. Sin esto, la barra entera es HTCAPTION y Windows se queda los
    /// clics: los botones nunca recibirian WM_LBUTTONDOWN.
    /// Devolver true = zona interactiva (HTCLIENT).
    std::function<bool(i32, i32)> isInteractiveArea;

    const WindowDesc& desc() const { return m_desc; }
    void setDragBarHeight(i32 h) { m_desc.dragBarHeight = h; }

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT hitTest(POINT screenPt) const;
    void    updateClientSize();

    HWND       m_hwnd        = nullptr;
    HINSTANCE  m_hinst       = nullptr;
    WindowDesc m_desc{};
    Size       m_clientSize{};
    Input      m_input{};

    bool m_shouldClose   = false;
    bool m_topmost       = false;
    bool m_layered       = false;
    bool m_trackingMouse = false;   // suscrito a WM_MOUSELEAVE
    bool m_fullscreen    = false;
    RECT  m_prevRect{};             // geometria antes de pantalla completa
    DWORD m_prevStyle = 0;          // estilo antes de pantalla completa
    f32  m_opacity      = 1.0f;
    f32  m_contentScale = 1.0f;
    u32  m_dpi          = 96;
};

} // namespace ludora
