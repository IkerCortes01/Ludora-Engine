#include "platform/Window.h"
#include <algorithm>
#include <windowsx.h>

namespace ludora {

namespace {
constexpr wchar_t kClassName[] = L"LudoraEngineWindow";
constexpr f32 kMinScale = 0.25f;
constexpr f32 kMaxScale = 8.0f;
} // namespace

Window::~Window() { destroy(); }

LRESULT CALLBACK Window::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Window::create(const WindowDesc& d) {
    m_desc  = d;
    m_hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    // OWNDC: un DC propio por ventana, evita coste por frame al presentar.
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc   = &Window::wndProcThunk;
    wc.hInstance     = m_hinst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // no borrar fondo: lo pinta el motor
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    DWORD style = m_desc.borderless
        // WS_THICKFRAME incluso sin borde: habilita resize por el gestor de
        // ventanas y Aero Snap. WS_CAPTION queda anulado en WM_NCCALCSIZE.
        ? (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN)
        : (WS_OVERLAPPEDWINDOW);

    if (!m_desc.resizable)
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    RECT r{0, 0, m_desc.width, m_desc.height};
    if (!m_desc.borderless)
        AdjustWindowRectEx(&r, style, FALSE, 0);

    const int winW = r.right - r.left;
    const int winH = r.bottom - r.top;

    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (m_desc.centerOnScreen) {
        px = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
        py = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    }

    m_hwnd = CreateWindowExW(0, kClassName, m_desc.title.c_str(), style,
                             px, py, winW, winH,
                             nullptr, nullptr, m_hinst, this);
    if (!m_hwnd) return false;

    m_dpi = GetDpiForWindow(m_hwnd);
    updateClientSize();

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

void Window::destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Window::updateClientSize() {
    if (!m_hwnd) return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    m_clientSize = Size{rc.right - rc.left, rc.bottom - rc.top};
}

/// Decide que "zona no-cliente" simula cada punto. Aqui vive el arrastre
/// y el redimensionado de la ventana sin bordes.
LRESULT Window::hitTest(POINT pt) const {
    // En pantalla completa no hay arrastre ni resize: todo es area cliente,
    // asi la barra virtual no roba clics a la interfaz.
    if (m_fullscreen) return HTCLIENT;

    RECT rc{};
    GetWindowRect(m_hwnd, &rc);

    // El borde de resize se escala con el DPI del monitor.
    const i32 b = MulDiv(m_desc.resizeBorder, static_cast<int>(m_dpi), 96);

    const bool left   = pt.x < rc.left + b;
    const bool right  = pt.x >= rc.right - b;
    const bool top    = pt.y < rc.top + b;
    const bool bottom = pt.y >= rc.bottom - b;

    if (m_desc.resizable && !IsZoomed(m_hwnd)) {
        if (top    && left)  return HTTOPLEFT;
        if (top    && right) return HTTOPRIGHT;
        if (bottom && left)  return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left)            return HTLEFT;
        if (right)           return HTRIGHT;
        if (top)             return HTTOP;
        if (bottom)          return HTBOTTOM;
    }

    // Franja superior = "barra de titulo" virtual: arrastre, Aero Snap y
    // doble clic para maximizar, todo gestionado por Windows.
    const i32 barH = MulDiv(m_desc.dragBarHeight, static_cast<int>(m_dpi), 96);
    if (pt.y < rc.top + barH) {
        // ...salvo las zonas que la escena declara interactivas (los botones):
        // ahi devolvemos HTCLIENT para que el clic llegue como WM_LBUTTONDOWN.
        if (isInteractiveArea &&
            isInteractiveArea(pt.x - rc.left, pt.y - rc.top))
            return HTCLIENT;
        return HTCAPTION;
    }

    return HTCLIENT;
}

LRESULT Window::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_NCCALCSIZE:
        // Con wParam TRUE devolver 0 elimina el marco no-cliente por completo:
        // el area cliente ocupa toda la ventana. Esto es lo que la hace borderless
        // conservando el comportamiento de resize de WS_THICKFRAME.
        if (m_desc.borderless && wp == TRUE)
            return 0;
        break;

    case WM_NCHITTEST:
        if (m_desc.borderless) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            return hitTest(pt);
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = MulDiv(m_desc.minWidth,  static_cast<int>(m_dpi), 96);
        mmi->ptMinTrackSize.y = MulDiv(m_desc.minHeight, static_cast<int>(m_dpi), 96);

        // Una WS_POPUP sin marco se maximiza a pantalla completa y taparia la
        // barra de tareas: acotarla al area de trabajo del monitor actual.
        if (m_desc.borderless) {
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(mon, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left   - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top    - mi.rcMonitor.top;
                mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
                mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
                mmi->ptMaxTrackSize  = mmi->ptMaxSize;
            }
        }
        return 0;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            updateClientSize();
            if (onResize) onResize(m_clientSize.w, m_clientSize.h);
        }
        return 0;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        // Windows sugiere el rectangulo destino; respetarlo evita que la
        // ventana "salte" al cruzar a un monitor con otra escala.
        const RECT* sug = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // el motor pinta cada pixel: no borrar (evita parpadeo)

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        m_input.onKey(static_cast<u32>(wp), true);
        if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) break; // Alt+F4 nativo
        // F11: pantalla completa. El bit 30 de lParam marca la autorrepeticion,
        // que se ignora para no alternar muchas veces al mantener la tecla.
        if (wp == VK_F11 && !(lp & (1 << 30)))
            toggleFullscreen();
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        m_input.onKey(static_cast<u32>(wp), false);
        return 0;

    case WM_CHAR:
        // Caracter ya traducido por la distribucion de teclado (lo produce
        // TranslateMessage en el bucle de mensajes). Los codigos de control
        // los gestiona WM_KEYDOWN, aqui solo interesa texto imprimible.
        if (wp >= 32 && wp != 127)
            m_input.onChar(static_cast<wchar_t>(wp));
        return 0;

    case WM_LBUTTONDOWN: SetCapture(hwnd); m_input.onMouseButton(0, true);  return 0;
    case WM_LBUTTONUP:   ReleaseCapture(); m_input.onMouseButton(0, false); return 0;
    case WM_RBUTTONDOWN: m_input.onMouseButton(1, true);  return 0;
    case WM_RBUTTONUP:   m_input.onMouseButton(1, false); return 0;
    case WM_MBUTTONDOWN: m_input.onMouseButton(2, true);  return 0;
    case WM_MBUTTONUP:   m_input.onMouseButton(2, false); return 0;

    case WM_MOUSEMOVE: {
        m_input.onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        // Pedir aviso al salir: sin esto el hover de un boton se queda
        // "pegado" cuando el cursor abandona la ventana sin mas eventos.
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            if (TrackMouseEvent(&tme)) m_trackingMouse = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        // Fuera del area cliente: ningun boton puede estar bajo el cursor.
        m_input.onMouseMove(-1, -1);
        return 0;

    case WM_MOUSEWHEEL:
        m_input.onMouseWheel(static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
        return 0;

    case WM_KILLFOCUS:
        m_input.resetAll();   // sin esto las teclas se quedan "pegadas"
        return 0;

    case WM_CLOSE:
        m_shouldClose = true;
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        m_shouldClose = true;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Window::pumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_shouldClose = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !m_shouldClose;
}

void Window::setTitle(const std::wstring& t) {
    m_desc.title = t;
    if (m_hwnd) SetWindowTextW(m_hwnd, t.c_str());
}

void Window::setClientSize(i32 w, i32 h) {
    if (!m_hwnd) return;
    w = std::max(m_desc.minWidth,  w);
    h = std::max(m_desc.minHeight, h);

    RECT r{0, 0, w, h};
    if (!m_desc.borderless) {
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        AdjustWindowRectEx(&r, style, FALSE, 0);
    }
    SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateClientSize();
}

void Window::centerOnScreen() {
    if (!m_hwnd) return;
    RECT wr{};
    GetWindowRect(m_hwnd, &wr);
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;

    // Centrar en el monitor actual, no en el primario.
    HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);

    const int x = mi.rcWork.left + (mi.rcWork.right  - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top  + (mi.rcWork.bottom - mi.rcWork.top  - h) / 2;
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::moveBy(i32 dx, i32 dy) {
    if (!m_hwnd) return;
    RECT wr{};
    GetWindowRect(m_hwnd, &wr);
    SetWindowPos(m_hwnd, nullptr, wr.left + dx, wr.top + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::setPosition(i32 x, i32 y) {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::toggleMaximize() {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::minimize() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);
}

bool Window::isMaximized() const {
    return m_hwnd && IsZoomed(m_hwnd);
}

void Window::toggleFullscreen() {
    if (!m_hwnd) return;

    if (!m_fullscreen) {
        // Guardar el estado actual para restaurarlo al salir.
        GetWindowRect(m_hwnd, &m_prevRect);
        m_prevStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));

        // Rectangulo del MONITOR completo (rcMonitor, no rcWork): asi cubre
        // tambien la barra de tareas, que es lo que se espera de pantalla
        // completa.
        HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(mon, &mi)) return;

        // Estilo minimo (WS_POPUP a secas): sin marco ni resize mientras dura
        // el modo. El WM_NCCALCSIZE de la ventana ya deja el area cliente al
        // 100%, asi que el contenido cubre todo el monitor.
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        m_fullscreen = true;
    } else {
        // Restaurar estilo y geometria previos.
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_prevStyle);
        SetWindowPos(m_hwnd, nullptr,
                     m_prevRect.left, m_prevRect.top,
                     m_prevRect.right - m_prevRect.left,
                     m_prevRect.bottom - m_prevRect.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
        m_fullscreen = false;
    }
    updateClientSize();
}

void Window::setOpacity(f32 alpha) {
    if (!m_hwnd) return;
    m_opacity = std::clamp(alpha, 0.1f, 1.0f);   // suelo 0.1: no perder la ventana

    if (!m_layered) {
        const LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        m_layered = true;
    }
    SetLayeredWindowAttributes(m_hwnd, 0,
        static_cast<BYTE>(m_opacity * 255.0f + 0.5f), LWA_ALPHA);
}

void Window::setTopmost(bool on) {
    if (!m_hwnd) return;
    m_topmost = on;
    SetWindowPos(m_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Window::setContentScale(f32 s) {
    m_contentScale = std::clamp(s, kMinScale, kMaxScale);
    if (onResize && m_hwnd) onResize(m_clientSize.w, m_clientSize.h);
}

} // namespace ludora
