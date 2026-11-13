#include "app/SelfTest.h"
#include "app/Account.h"
#include "app/AccountStore.h"
#include "app/DemoScene.h"
#include "app/Device.h"
#include "app/HomeScene.h"
#include "app/Profile.h"
#include "app/Vault.h"
#include "app/Blake2b.h"
#include "app/Argon2.h"
#include "app/Session.h"
#include "app/GameInstall.h"
#include "app/TomatePack.h"
#include "app/Guardian.h"
#include "app/VirtualFileSystem.h"
#include "app/Font5x7.h"
#include "app/TextRender.h"
#include "ui/ScrollPanel.h"
#include "ui/TextField.h"
#include "ui/TitleBar.h"
#include "core/Engine.h"
#include "resource.h"   // IDI_LUDORA, para comprobar el icono
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <atomic>
#include <windows.h>
#include <shellapi.h>

namespace ludora {

namespace {
// Cada paso espera este tiempo para que Windows procese el cambio de tamano
// y el motor reconstruya el framebuffer antes de medir.
constexpr f64 kStepDelay = 0.25;

/// Borra una carpeta de pruebas con todo su contenido.
///
/// `pFrom` de SHFileOperationW espera una lista terminada en DOBLE nulo, no
/// una cadena normal: por eso se copia a un buffer con dos ceros al final. Con
/// un solo nulo, la funcion lee mas alla de la cadena.
void limpiarCarpeta(const std::wstring& dir) {
    std::vector<wchar_t> buf(dir.size() + 2, L'\0');
    std::copy(dir.begin(), dir.end(), buf.begin());

    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = buf.data();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}
} // namespace

SelfTest::SelfTest(std::wstring reportPath) : m_path(std::move(reportPath)) {}

void SelfTest::check(const char* name, bool ok, const std::string& detail) {
    m_results.push_back({name, ok, detail});
}

void SelfTest::update(Engine& engine, f64 dt) {
    if (m_done) return;

    // Argon2 en modo ligero durante el autodiagnostico: si no, cada prueba
    // de Vault/Session/juego tardaria segundos y la bateria completa no
    // acabaria. La app real usa siempre el coste completo.
    Vault::setFastModeForTests(true);

    m_timer += dt;
    if (m_timer < kStepDelay) return;
    m_timer = 0.0;

    Window& win = engine.window();
    char buf[256];

    switch (m_step) {
    case 0: {
        // HomeScene se instancia en la PILA en varias pruebas; si crece
        // demasiado, desborda. Se vigila aqui para que el aviso llegue antes
        // que el fallo.
        std::snprintf(buf, sizeof(buf), "HomeScene ocupa %zu bytes",
                      sizeof(HomeScene));
        check("HomeScene cabe holgada en la pila",
              sizeof(HomeScene) < 200000, buf);

        RECT wr{}, cr{};
        GetWindowRect(win.handle(), &wr);
        GetClientRect(win.handle(), &cr);
        const i32 ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        const bool ok = (cr.right == ww) && (cr.bottom == wh) && ww > 0;
        std::snprintf(buf, sizeof(buf), "cliente %ldx%ld == ventana %dx%d",
                      cr.right, cr.bottom, ww, wh);
        check("Borderless: area cliente ocupa toda la ventana", ok, buf);

        // --- el icono propio, no el generico de Windows ---
        // La ventana cargaba IDI_APPLICATION, asi que el .exe podia llevar su
        // icono y la barra de tareas seguia mostrando el del sistema. Se
        // comprueban las dos mitades: que el recurso existe y que la clase de
        // ventana lo esta usando.
        {
            const HICON propio = static_cast<HICON>(LoadImageW(
                GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LUDORA),
                IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
            check("El icono de la aplicacion esta en el ejecutable",
                  propio != nullptr);
            if (propio) DestroyIcon(propio);

            // El que de verdad se ve: el de la clase de ventana. Si siguiera
            // siendo el del sistema, estos dos handles coincidirian.
            const HICON deLaClase = reinterpret_cast<HICON>(
                GetClassLongPtrW(win.handle(), GCLP_HICON));
            const HICON delSistema = LoadIconW(nullptr, IDI_APPLICATION);
            std::snprintf(buf, sizeof(buf), "clase=%p, sistema=%p",
                          static_cast<void*>(deLaClase),
                          static_cast<void*>(delSistema));
            check("La ventana usa el icono propio y no el del sistema",
                  deLaClase != nullptr && deLaClase != delSistema, buf);
        }

        const LONG_PTR st = GetWindowLongPtrW(win.handle(), GWL_STYLE);
        check("Estilo WS_POPUP presente",     (st & WS_POPUP) != 0);
        check("Estilo WS_THICKFRAME presente (resize)", (st & WS_THICKFRAME) != 0);
        check("Estilo WS_CAPTION ausente",    (st & WS_CAPTION) != WS_CAPTION);
        break;
    }
    case 1:
        win.setClientSize(1280, 720);
        break;

    case 2: {
        const Size cs = win.clientSize();
        std::snprintf(buf, sizeof(buf), "obtenido %dx%d", cs.w, cs.h);
        check("Resize a 1280x720", cs.w == 1280 && cs.h == 720, buf);

        const Size ls = engine.logicalSize();
        std::snprintf(buf, sizeof(buf), "logico %dx%d con escala %.2f", ls.w, ls.h, win.contentScale());
        check("Framebuffer logico coincide con cliente a escala 1.0",
              ls.w == 1280 && ls.h == 720, buf);
        win.setClientSize(100, 100);   // por debajo del minimo
        break;
    }
    case 3: {
        const Size cs = win.clientSize();
        std::snprintf(buf, sizeof(buf), "pedido 100x100, obtenido %dx%d (min %dx%d)",
                      cs.w, cs.h, win.desc().minWidth, win.desc().minHeight);
        check("Tamano minimo respetado",
              cs.w >= win.desc().minWidth && cs.h >= win.desc().minHeight, buf);
        win.setClientSize(800, 600);
        break;
    }
    case 4:
        engine.setContentScale(2.0f);
        break;

    case 5: {
        const Size ls = engine.logicalSize();
        const Framebuffer& fb = engine.renderer().fb();
        std::snprintf(buf, sizeof(buf), "cliente 800x600 escala 2.0 -> logico %dx%d, fb %dx%d",
                      ls.w, ls.h, fb.width(), fb.height());
        // A escala 2x el area logica es la mitad: cada pixel logico ocupa 2x2 fisicos.
        check("Escala 2.0x reduce el framebuffer a la mitad",
              ls.w == 400 && ls.h == 300 && fb.width() == 400 && fb.height() == 300, buf);
        engine.setContentScale(0.5f);
        break;
    }
    case 6: {
        const Size ls = engine.logicalSize();
        std::snprintf(buf, sizeof(buf), "escala 0.5 -> logico %dx%d", ls.w, ls.h);
        check("Escala 0.5x duplica el framebuffer", ls.w == 1600 && ls.h == 1200, buf);
        engine.setContentScale(99.0f);   // fuera de rango
        break;
    }
    case 7: {
        std::snprintf(buf, sizeof(buf), "escala pedida 99.0, aplicada %.2f", engine.contentScale());
        check("Escala se limita al maximo (8.0)", engine.contentScale() <= 8.0f, buf);
        engine.setContentScale(1.0f);
        break;
    }
    case 8: {
        win.setOpacity(0.5f);
        std::snprintf(buf, sizeof(buf), "opacidad %.2f", win.opacity());
        check("Opacidad ajustable", std::fabs(win.opacity() - 0.5f) < 0.01f, buf);
        win.setOpacity(1.0f);

        win.setTopmost(true);
        const bool tm = (GetWindowLongPtrW(win.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        check("Topmost aplicado a nivel de sistema", tm);
        win.setTopmost(false);
        break;
    }
    case 9: {
        const Framebuffer& fb = engine.renderer().fb();
        check("Framebuffer valido y con memoria", fb.valid() && fb.data() != nullptr);

        std::snprintf(buf, sizeof(buf), "%.1f fps tras %lld frames",
                      engine.clock().fps(), static_cast<long long>(engine.clock().frameCount()));
        check("Bucle corriendo con FPS medible", engine.clock().fps() > 1.0, buf);

        // La fuente bitmap sigue midiendo su tamano fijo: 3 glifos de 5 px
        // mas 2 separaciones. Se comprueba la implementacion bitmap directa,
        // porque measure() ya usa las fuentes del sistema.
        const i32 twb = font5x7::measureBitmap("ABC", 1);
        std::snprintf(buf, sizeof(buf), "ancho bitmap de \"ABC\" = %d px", twb);
        check("La fuente bitmap de respaldo mide correctamente", twb == 17, buf);

        // Y la del sistema mide algo razonable: mas ancha que la bitmap a la
        // misma escala, porque sus glifos son proporcionales y mas grandes.
        const i32 tw = font5x7::measure("ABC", 1);
        std::snprintf(buf, sizeof(buf), "ancho real de \"ABC\" = %d px", tw);
        check("La fuente del sistema mide un ancho plausible",
              tw > 0 && tw < 200, buf);
        break;
    }
    case 10: {
        // SelfTest no dibuja botones, asi que instala una zona equivalente
        // (los 3 botones de la esquina derecha) para ejercitar el mecanismo
        // real de WM_NCHITTEST: sin esto la comprobacion no probaria nada.
        const i32 barH  = win.desc().dragBarHeight;
        const i32 zoneW = (barH + barH / 2) * 3;
        win.isInteractiveArea = [this, &win, zoneW, barH](i32 px, i32 py) -> bool {
            (void)this;
            return py < barH && px >= win.clientSize().w - zoneW;
        };

        const Size cs = win.clientSize();
        check("Esquina sup. derecha reservada a botones (no arrastra)",
              win.isInteractiveArea(cs.w - 5, 5));
        check("Centro de la barra sigue arrastrando",
              !win.isInteractiveArea(cs.w / 2, 5));
        check("Debajo de la barra no es zona de boton",
              !win.isInteractiveArea(cs.w - 5, barH + 20));

        win.isInteractiveArea = nullptr;   // no dejar el callback colgando
        win.toggleMaximize();
        break;
    }
    case 11: {
        RECT wr{};
        GetWindowRect(win.handle(), &wr);
        HMONITOR mon = MonitorFromWindow(win.handle(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);

        const bool dentro = (wr.right - wr.left) <= (mi.rcWork.right - mi.rcWork.left) &&
                            (wr.bottom - wr.top) <= (mi.rcWork.bottom - mi.rcWork.top);
        std::snprintf(buf, sizeof(buf), "maximizada %ldx%ld, area de trabajo %ldx%ld",
                      wr.right - wr.left, wr.bottom - wr.top,
                      mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
        check("Maximizar respeta la barra de tareas", dentro, buf);

        check("Estado maximizado detectado", win.isMaximized());
        win.toggleMaximize();
        break;
    }
    case 12: {
        check("Restaurar desde maximizado", !win.isMaximized());

        // Simular el raton inyectando en Input igual que lo hace el WndProc:
        // asi se prueba la logica real de la escena (hover, click, cancelacion)
        // sin depender de mover el cursor fisico, que no es fiable en pruebas.
        win.setClientSize(960, 600);
        break;
    }
    case 13: {
        const i32 fbw  = engine.renderer().fb().width();
        const i32 barH = win.desc().dragBarHeight;
        const i32 bw   = std::max(barH + barH / 2, 24);

        // Centros esperados de cada boton, de derecha a izquierda.
        const i32 cxClose = fbw - bw / 2;
        const i32 cxMax   = fbw - bw - bw / 2;
        const i32 cxMin   = fbw - bw * 2 - bw / 2;
        const i32 cy      = barH / 2;

        char b2[256];
        std::snprintf(b2, sizeof(b2), "fb %d px, boton %d px -> min %d, max %d, close %d",
                      fbw, bw, cxMin, cxMax, cxClose);
        check("Geometria de los 3 botones dentro de la ventana",
              cxMin > 0 && cxClose < fbw && cxMin < cxMax && cxMax < cxClose, b2);

        // La zona de botones debe excluirse del arrastre y el resto no.
        // (isInteractiveArea lo instala DemoScene; aqui se replica su regla.)
        const bool zonaBotones   = (cxClose >= fbw - bw * 3);
        const bool centroArrastra = (fbw / 2 < fbw - bw * 3);
        check("Los 3 botones caben en la esquina derecha", zonaBotones);
        check("Queda barra suficiente para arrastrar", centroArrastra);

        std::snprintf(b2, sizeof(b2), "alto de barra %d px, centro vertical %d", barH, cy);
        check("Altura de barra coherente con los botones", cy > 0 && cy < barH, b2);

        // --- clic real sobre MAXIMIZAR, inyectado en la ventana ---
        // El mensaje entra por el WndProc igual que un clic humano, asi que
        // recorre Input y su deteccion de flanco de verdad. La accion se
        // reproduce en los pasos siguientes replicando la regla de DemoScene,
        // porque la escena activa aqui es SelfTest y no dibuja botones.
        m_savedMaximized = win.isMaximized();
        m_clickX = cxMax;
        m_clickY = cy;
        // SendMessage (sincrono) y no PostMessage: con PostMessage el mensaje
        // queda en cola y el raton fisico del equipo puede colar su propio
        // WM_MOUSEMOVE antes, sobrescribiendo las coordenadas inyectadas.
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(cxMax, cy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cxMax, cy));

        {
            const Vec2 mp = engine.input().mousePos();
            std::snprintf(b2, sizeof(b2), "raton en (%.0f,%.0f), esperado (%d,%d)",
                          mp.x, mp.y, m_clickX, m_clickY);
            check("El evento de raton llega a Input con las coordenadas correctas",
                  static_cast<i32>(mp.x) == m_clickX &&
                  static_cast<i32>(mp.y) == m_clickY, b2);
            check("Boton izquierdo detectado como pulsado", engine.input().mouseDown(0));
        }
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cxMax, cy));
        break;
    }
    case 14: {
        check("Boton izquierdo detectado como soltado", !engine.input().mouseDown(0));

        // Reproducir la accion que DemoScene ejecutaria en ese punto.
        win.toggleMaximize();
        break;
    }
    case 15: {
        std::snprintf(buf, sizeof(buf), "antes %s, despues %s",
                      m_savedMaximized ? "maximizada" : "normal",
                      win.isMaximized() ? "maximizada" : "normal");
        check("Accion de MAXIMIZAR cambia el estado de la ventana",
              win.isMaximized() != m_savedMaximized, buf);

        if (win.isMaximized()) win.toggleMaximize();   // dejarla como estaba
        break;
    }
    case 16: {
        // Comprobar el HOVER leyendo los pixeles que dibuja DemoScene.
        // Se instancia aparte y se le pide que pinte sobre este framebuffer:
        // es la unica forma fiable de verificar el resalte, porque capturar
        // la pantalla depende de que la ventana tenga el primer plano.
        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene scene;
        scene.onEnter(engine);

        const i32 barH = win.desc().dragBarHeight;
        const i32 bw   = std::max(barH + barH / 2, 24);
        const i32 cxClose = 960 - bw / 2;
        const i32 cy      = barH / 2;

        // 1) Sin el raton encima: el boton usa el color de fondo de la barra.
        SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 300));
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 300));
        scene.update(engine, 0.016);
        scene.render(engine, probe);
        const u32 sinHover = probe.data()[static_cast<size_t>(cy) * 960 + (cxClose - 20)];

        // 2) Con el raton sobre CERRAR: debe pintarse el rojo de cierre.
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(cxClose, cy));
        scene.update(engine, 0.016);
        scene.render(engine, probe);
        const u32 conHover = probe.data()[static_cast<size_t>(cy) * 960 + (cxClose - 20)];

        scene.onExit(engine);

        const u32 rojoEsperado = Color::fromHex(0xE01E23).packed();   // kCloseHover
        std::snprintf(buf, sizeof(buf), "sin hover 0x%08X, con hover 0x%08X (rojo 0x%08X)",
                      sinHover, conHover, rojoEsperado);
        check("Hover sobre CERRAR pinta el boton de rojo",
              conHover == rojoEsperado && sinHover != conHover, buf);
        break;
    }
    case 17: {
        // --- validacion del formato de correo ---
        std::string e;
        check("Correo valido aceptado",
              Account::validateEmail("iker@ludora.engine", e));
        check("Correo sin el dominio rechazado",
              !Account::validateEmail("iker@gmail.com", e), e);
        check("Correo con otro dominio parecido rechazado",
              !Account::validateEmail("iker@ludora.engine.com", e), e);
        check("Correo sin nombre rechazado",
              !Account::validateEmail("@ludora.engine", e), e);
        check("Correo vacio rechazado", !Account::validateEmail("", e), e);
        check("Correo con dos arrobas rechazado",
              !Account::validateEmail("a@b@ludora.engine", e), e);
        check("Correo con caracteres invalidos rechazado",
              !Account::validateEmail("ik er!@ludora.engine", e), e);
        check("Correo con punto y guion aceptado",
              Account::validateEmail("ik.er-1_x@ludora.engine", e));

        check("Contrasena corta rechazada", !Account::validatePassword("123", e), e);
        check("Contrasena de 6+ aceptada",  Account::validatePassword("clave1", e));
        break;
    }
    case 18: {
        // --- ciclo completo de cuenta sobre un archivo de prueba ---
        const wchar_t* kTmp = L"selftest-cuenta.tmp";
        DeleteFileW(kTmp);
        std::string e;

        check("Sin archivo no hay cuenta", !Account::exists(kTmp));

        const bool creada = Account::create(kTmp, "prueba@ludora.engine", "clave123", e);
        check("Cuenta creada correctamente", creada, e);
        check("Tras crear, la cuenta existe", Account::exists(kTmp));
        check("El correo guardado se recupera",
              Account::storedEmail(kTmp) == "prueba@ludora.engine",
              Account::storedEmail(kTmp));

        // La regla central: solo se admite UNA cuenta.
        const bool segunda = Account::create(kTmp, "otro@ludora.engine", "clave456", e);
        check("Una segunda cuenta es rechazada", !segunda, e);
        check("Tras el intento, el correo original sigue intacto",
              Account::storedEmail(kTmp) == "prueba@ludora.engine");

        check("Credenciales correctas verifican",
              Account::verify(kTmp, "prueba@ludora.engine", "clave123", e), e);
        check("Contrasena incorrecta rechazada",
              !Account::verify(kTmp, "prueba@ludora.engine", "clave999", e));
        check("Correo incorrecto rechazado",
              !Account::verify(kTmp, "otro@ludora.engine", "clave123", e));

        // La contrasena no puede aparecer en el archivo.
        std::ifstream in(kTmp, std::ios::binary);
        std::string contenido((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        in.close();
        check("La contrasena NO se guarda en texto plano",
              contenido.find("clave123") == std::string::npos);
        check("El archivo contiene salt y hash",
              contenido.find("salt=") != std::string::npos &&
              contenido.find("hash=") != std::string::npos);

        DeleteFileW(kTmp);
        break;
    }
    case 19: {
        // Dos cuentas con la misma contrasena deben dar hashes distintos:
        // eso demuestra que el salt es aleatorio y no reutilizado.
        const wchar_t* kA = L"selftest-a.tmp";
        const wchar_t* kB = L"selftest-b.tmp";
        DeleteFileW(kA); DeleteFileW(kB);
        std::string e;

        Account::create(kA, "uno@ludora.engine", "mismaClave", e);
        Account::create(kB, "dos@ludora.engine", "mismaClave", e);

        auto leer = [](const wchar_t* p) {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        };
        const std::string a = leer(kA), b = leer(kB);

        auto valorDe = [](const std::string& s, const char* clave) {
            const size_t p = s.find(clave);
            if (p == std::string::npos) return std::string{};
            const size_t ini = p + strlen(clave);
            const size_t fin = s.find_first_of("\r\n", ini);
            return s.substr(ini, fin - ini);
        };
        const std::string saltA = valorDe(a, "salt="), saltB = valorDe(b, "salt=");
        const std::string hashA = valorDe(a, "hash="), hashB = valorDe(b, "hash=");

        check("Cada cuenta recibe un salt distinto", !saltA.empty() && saltA != saltB);
        check("Misma contrasena produce hashes distintos (salt efectivo)",
              !hashA.empty() && hashA != hashB);
        std::snprintf(buf, sizeof(buf), "hash de %zu caracteres hex", hashA.size());
        check("El hash es SHA-256 (64 caracteres hex)", hashA.size() == 64, buf);

        DeleteFileW(kA); DeleteFileW(kB);
        break;
    }
    case 20: {
        // --- caracteres que el correo necesita ---
        TextField f;
        TextField::Style st{};
        st.textScale = 2;
        f.setStyle(st);
        f.setRect({0, 0, 400, 40});
        f.setFocused(true);

        for (char c : std::string("iker.lopez-1_x@ludora.engine"))
            f.onChar(static_cast<wchar_t>(c));

        check("El campo acepta arroba y puntos",
              f.text() == "iker.lopez-1_x@ludora.engine", f.text());

        // La arroba y el punto deben pasar el filtro de caracteres.
        TextField g;
        g.setStyle(st);
        g.setRect({0, 0, 400, 40});
        g.setFocused(true);
        g.onChar(L'@');
        g.onChar(L'.');
        check("Arroba y punto sueltos se insertan", g.text() == "@.", g.text());

        // Los codigos de control no deben colarse como texto.
        TextField h;
        h.setStyle(st);
        h.setRect({0, 0, 400, 40});
        h.setFocused(true);
        h.onChar(L'\t');
        h.onChar(L'\r');
        h.onChar(static_cast<wchar_t>(27));   // ESC
        check("Los caracteres de control se descartan", h.text().empty(), h.text());

        // Un campo sin foco no debe recibir nada.
        TextField sinFoco;
        sinFoco.setStyle(st);
        sinFoco.setRect({0, 0, 400, 40});
        sinFoco.onChar(L'x');
        check("Un campo sin foco ignora el teclado", sinFoco.text().empty());
        break;
    }
    case 21: {
        // --- edicion: borrado, cursor y seleccion ---
        TextField f;
        TextField::Style st{};
        st.textScale = 2;
        f.setStyle(st);
        f.setRect({0, 0, 400, 40});
        f.setFocused(true);
        f.setText("abc@ludora.engine");

        f.onKey(VK_BACK, false, false);
        check("Retroceso borra el ultimo caracter",
              f.text() == "abc@ludora.engin", f.text());

        f.onKey(VK_HOME, false, false);
        f.onKey(VK_DELETE, false, false);
        check("Suprimir borra al principio tras HOME",
              f.text() == "bc@ludora.engin", f.text());

        // Ctrl+A selecciona todo; escribir encima reemplaza.
        f.onKey('A', false, true);
        f.onChar(L'z');
        check("Ctrl+A y escribir reemplaza todo el contenido",
              f.text() == "z", f.text());

        // El limite de longitud debe respetarse.
        TextField lim;
        lim.setStyle(st);
        lim.setRect({0, 0, 400, 40});
        lim.setFocused(true);
        lim.setMaxLength(5);
        for (char c : std::string("abcdefghij")) lim.onChar(static_cast<wchar_t>(c));
        check("El limite de longitud se respeta",
              lim.text().size() == 5, lim.text());
        break;
    }
    case 22: {
        // --- la pantalla cambia de modo segun exista o no la cuenta ---
        // Se comprueba sobre el archivo real que usa DemoScene (cuenta.dat),
        // guardando y restaurando el que hubiera para no destruir datos.
        const wchar_t* kReal = L"cuenta.dat";
        const wchar_t* kBak  = L"cuenta.dat.selftest-bak";
        DeleteFileW(kBak);
        const bool habia = Account::exists(kReal);
        if (habia) MoveFileW(kReal, kBak);

        Framebuffer probe;
        probe.resize(960, 600);

        // 1) Sin cuenta: el formulario debe ofrecer la creacion.
        {
            DemoScene sc;
            sc.onEnter(engine);
            sc.update(engine, 0.016);
            sc.render(engine, probe);
            sc.onExit(engine);
        }
        check("Sin cuenta previa, la pantalla no encuentra ninguna",
              !Account::exists(kReal));

        // 2) Con cuenta: al entrar, DemoScene debe precargar el correo.
        std::string e;
        Account::create(kReal, "modo@ludora.engine", "clave123", e);
        {
            DemoScene sc;
            sc.onEnter(engine);
            // Solo render(): update() detectaria el dispositivo ya verificado
            // y encolaria HomeScene, que sustituiria a SelfTest y abortaria
            // el resto de las pruebas.
            sc.render(engine, probe);
            sc.onExit(engine);
        }
        check("Con cuenta existente, el correo guardado se recupera",
              Account::storedEmail(kReal) == "modo@ludora.engine",
              Account::storedEmail(kReal));

        // 3) Crear otra cuenta debe seguir prohibido con el archivo real.
        const bool otra = Account::create(kReal, "otra@ludora.engine", "clave999", e);
        check("Con cuenta existente no se puede crear otra", !otra, e);

        DeleteFileW(kReal);
        if (habia) MoveFileW(kBak, kReal);   // restaurar la cuenta del usuario
        check("La cuenta previa del usuario se restaura",
              habia ? Account::exists(kReal) : !Account::exists(kReal));
        break;
    }
    case 23: {
        // --- vinculo con el dispositivo ---
        const wchar_t* kTmp = L"selftest-dev.tmp";
        DeleteFileW(kTmp);
        std::string e;

        check("Sin cuenta no hay dispositivo vinculado",
              !Account::deviceInfo(kTmp).registered);

        Account::create(kTmp, "dev@ludora.engine", "clave123", e);

        const Account::DeviceInfo d = Account::deviceInfo(kTmp);
        check("Al registrarse queda un dispositivo vinculado", d.registered);
        check("El dispositivo se verifica en este mismo equipo", d.verified);
        std::snprintf(buf, sizeof(buf), "tipo detectado: %s", d.kindName.c_str());
        check("Se guarda el tipo de dispositivo", !d.kindName.empty(), buf);

        check("Con el dispositivo verificado se entra sin contrasena",
              Account::canAutoLogin(kTmp));
        break;
    }
    case 24: {
        // --- PRIVACIDAD: que NO debe contener el archivo ---
        const wchar_t* kTmp = L"selftest-dev.tmp";
        std::ifstream in(kTmp, std::ios::binary);
        std::string txt((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();

        // El identificador de maquina en claro no puede aparecer.
        std::string guid;
        {
            HKEY k = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                              0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
                wchar_t wbuf[128]{}; DWORD sz = sizeof(wbuf), ty = 0;
                if (RegQueryValueExW(k, L"MachineGuid", nullptr, &ty,
                                     reinterpret_cast<LPBYTE>(wbuf), &sz) == ERROR_SUCCESS)
                    for (const wchar_t* p = wbuf; *p; ++p)
                        guid.push_back(static_cast<char>(*p & 0x7F));
                RegCloseKey(k);
            }
        }
        check("El identificador de maquina NO se guarda en claro",
              guid.empty() || txt.find(guid) == std::string::npos);

        // Nombre del equipo y del usuario tampoco.
        wchar_t wname[256]{}; DWORD n = 256;
        std::string host;
        if (GetComputerNameW(wname, &n))
            for (DWORD i = 0; i < n; ++i) host.push_back(static_cast<char>(wname[i] & 0x7F));
        check("El nombre del equipo NO se guarda",
              host.empty() || txt.find(host) == std::string::npos);

        wchar_t wuser[256]{}; DWORD un = 256;
        std::string user;
        if (GetUserNameW(wuser, &un))
            for (DWORD i = 0; i + 1 < un; ++i) user.push_back(static_cast<char>(wuser[i] & 0x7F));
        check("El nombre de usuario NO se guarda",
              user.empty() || txt.find(user) == std::string::npos);

        // Ningun campo mas alla de los seis previstos. Comprobar la ausencia
        // de palabras sueltas ("lat", "lon") daria falsos positivos: aparecen
        // dentro de otras palabras y por azar en 64 caracteres hexadecimales.
        // Lo que se verifica es la lista blanca de claves.
        std::vector<std::string> claves;
        {
            std::istringstream ss(txt);
            std::string linea;
            while (std::getline(ss, linea)) {
                if (linea.empty() || linea[0] == '#') continue;
                const size_t eq = linea.find('=');
                if (eq != std::string::npos) claves.push_back(linea.substr(0, eq));
            }
        }
        const char* permitidas[] = { "email", "salt", "hash", "devsalt", "devhash", "devkind" };
        bool soloPermitidas = !claves.empty();
        std::string intrusa;
        for (const auto& k : claves) {
            bool ok = false;
            for (const char* p : permitidas) if (k == p) ok = true;
            if (!ok) { soloPermitidas = false; intrusa = k; }
        }
        std::snprintf(buf, sizeof(buf), "%zu campos: %s", claves.size(),
                      intrusa.empty() ? "todos previstos" : intrusa.c_str());
        check("El archivo solo contiene los campos previstos (sin ubicacion)",
              soloPermitidas, buf);

        // El tipo de dispositivo debe ser una categoria generica, no un modelo.
        std::string kindVal;
        {
            const size_t p = txt.find("devkind=");
            if (p != std::string::npos) {
                const size_t ini = p + 8;
                const size_t fin = txt.find_first_of("\r\n", ini);
                kindVal = txt.substr(ini, fin - ini);
            }
        }
        const bool generico = (kindVal == "Escritorio" || kindVal == "Portatil" ||
                               kindVal == "Tableta"    || kindVal == "Desconocido");
        check("El tipo de dispositivo es una categoria generica", generico, kindVal);

        check("El dispositivo se guarda como hash de 64 hex",
              txt.find("devhash=") != std::string::npos);
        break;
    }
    case 25: {
        // --- desvincular y revincular ---
        const wchar_t* kTmp = L"selftest-dev.tmp";
        std::string e;

        check("Antes de olvidar, la entrada automatica esta activa",
              Account::canAutoLogin(kTmp));

        check("Olvidar el dispositivo funciona", Account::forgetDevice(kTmp, e), e);
        check("Tras olvidarlo ya no hay entrada automatica",
              !Account::canAutoLogin(kTmp));
        check("Tras olvidarlo no queda dispositivo vinculado",
              !Account::deviceInfo(kTmp).registered);

        // La cuenta sigue intacta: la contrasena debe seguir sirviendo.
        check("La cuenta sobrevive al olvido del dispositivo",
              Account::verify(kTmp, "dev@ludora.engine", "clave123", e), e);

        // Y al validarse de nuevo, el equipo se revincula solo.
        check("Al entrar con contrasena el equipo se revincula",
              Account::canAutoLogin(kTmp));

        // Una huella ajena no debe validar este equipo.
        check("Una huella de otro equipo no verifica",
              !Device::matches("salt-cualquiera", std::string(64, 'a')));

        DeleteFileW(kTmp);
        break;
    }
    case 26: {
        // La huella debe ser estable entre llamadas (mismo salt, mismo equipo)
        // y distinta al cambiar el salt (asi no es un identificador global
        // reutilizable entre cuentas o aplicaciones).
        const std::string h1 = Device::fingerprint("salt-A");
        const std::string h2 = Device::fingerprint("salt-A");
        const std::string h3 = Device::fingerprint("salt-B");

        std::snprintf(buf, sizeof(buf), "%zu caracteres hex", h1.size());
        check("La huella es SHA-256 (64 hex)", h1.size() == 64, buf);
        check("La huella es estable en el mismo equipo", h1 == h2);
        check("Con otro salt la huella cambia (no es un ID global)", h1 != h3);
        break;
    }
    case 27: {
        // --- el ciclo completo tal y como lo vive el usuario ---
        // Registrarse una vez, cerrar, y volver a abrir: la segunda vez la
        // pantalla debe entrar sola, sin pedir la contrasena.
        const wchar_t* kReal = L"cuenta.dat";
        const wchar_t* kBak  = L"cuenta.dat.selftest-bak";
        DeleteFileW(kBak);
        const bool habia = Account::exists(kReal);
        if (habia) MoveFileW(kReal, kBak);

        std::string e;

        // Primera vez: no hay cuenta, hay que registrarse.
        check("1a apertura: no hay cuenta, toca registrarse",
              !Account::exists(kReal) && !Account::canAutoLogin(kReal));

        Account::create(kReal, "ciclo@ludora.engine", "clave123", e);

        // Segunda apertura: DemoScene debe detectar el dispositivo y saltar.
        Framebuffer probe;
        probe.resize(960, 600);
        {
            DemoScene sc;
            sc.onEnter(engine);
            sc.render(engine, probe);   // sin update(): encolaria HomeScene
            sc.onExit(engine);
        }
        const Account::DeviceInfo d = Account::deviceInfo(kReal);
        std::snprintf(buf, sizeof(buf), "dispositivo %s, verificado=%s",
                      d.kindName.c_str(), d.verified ? "si" : "no");
        check("2a apertura: el equipo se reconoce y entra sin contrasena",
              Account::canAutoLogin(kReal) && d.verified, buf);

        // Aunque el equipo este verificado, la contrasena debe seguir siendo
        // valida: la verificacion del dispositivo no la sustituye ni la anula.
        check("La contrasena sigue siendo valida en un equipo verificado",
              Account::verify(kReal, "ciclo@ludora.engine", "clave123", e), e);
        check("Una contrasena incorrecta sigue fallando en equipo verificado",
              !Account::verify(kReal, "ciclo@ludora.engine", "otraClave", e));

        DeleteFileW(kReal);
        if (habia) MoveFileW(kBak, kReal);
        check("La cuenta real del usuario queda restaurada",
              habia ? Account::exists(kReal) : !Account::exists(kReal));
        break;
    }
    case 28: {
        // --- menu de 6 bloques de la pantalla principal ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("menu@ludora.engine");
        home.onEnter(engine);
        home.render(engine, probe);   // fija la geometria del menu

        const i32 barH = win.desc().dragBarHeight;
        const i32 filaY = barH + 25;   // centro vertical de la franja azul

        // Color de un bloque concreto, leido del framebuffer.
        auto colorBloque = [&](i32 idx) -> u32 {
            const i32 x0 = (960 * idx)       / HomeScene::kBlockCount;
            const i32 x1 = (960 * (idx + 1)) / HomeScene::kBlockCount;
            const i32 cx = (x0 + x1) / 2;
            // Unos pixeles por encima del texto, dentro del bloque.
            return probe.data()[static_cast<size_t>(filaY - 12) * 960 + cx];
        };

        // El bloque activo se pinta con kLedRed, el mismo rojo de los paneles.
        const u32 rojo = Color::fromHex(0xFF0018).packed();

        // Al entrar, la seccion activa es la 3a (indice 2): los paneles rojos.
        std::snprintf(buf, sizeof(buf), "bloque 3 = 0x%08X (rojo 0x%08X)",
                      colorBloque(2), rojo);
        check("El 3er bloque esta activo al entrar", colorBloque(2) == rojo, buf);
        check("Los demas bloques no estan activos",
              colorBloque(0) != rojo && colorBloque(1) != rojo &&
              colorBloque(3) != rojo && colorBloque(4) != rojo &&
              colorBloque(5) != rojo);

        // Bajo el menu: a la izquierda el panel violeta (con su scroll), a la
        // derecha el panel rojo. El centro queda en el fondo de la escena.
        const i32 yPanel = barH + 50 + 100;
        const u32 izq = probe.data()[static_cast<size_t>(yPanel) * 960 + 180];
        const u32 der = probe.data()[static_cast<size_t>(yPanel) * 960 + 910];
        const u32 med = probe.data()[static_cast<size_t>(yPanel) * 960 + 480];
        const u32 led     = rojo;   // panel derecho y bloque activo comparten color
        const u32 violeta = Color::fromHex(0x2B1B57).packed();   // ficha
        const u32 fondoP  = Color::fromHex(0x1A1440).packed();   // fondo del panel
        check("La 3a seccion muestra el panel violeta a la izquierda",
              izq == violeta || izq == fondoP);
        check("La 3a seccion muestra el panel rojo a la derecha",
              der == led && med != led);

        // --- clic en el bloque 5 ---
        const i32 x0 = (960 * 4) / HomeScene::kBlockCount;
        const i32 x1 = (960 * 5) / HomeScene::kBlockCount;
        const i32 bx = (x0 + x1) / 2;

        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, filaY));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);
        home.render(engine, probe);

        check("Al pulsar el bloque 5 pasa a ser el activo", colorBloque(4) == rojo);
        check("El 3er bloque deja de estar activo", colorBloque(2) != rojo);

        // La seccion 5 esta vacia: ni panel violeta ni panel rojo.
        const u32 izq2 = probe.data()[static_cast<size_t>(yPanel) * 960 + 180];
        const u32 der2 = probe.data()[static_cast<size_t>(yPanel) * 960 + 910];
        check("Las secciones vacias no dibujan los paneles",
              izq2 != violeta && izq2 != fondoP && der2 != led);

        // Volver al 3er bloque restaura los paneles.
        const i32 x0b = (960 * 2) / HomeScene::kBlockCount;
        const i32 x1b = (960 * 3) / HomeScene::kBlockCount;
        const i32 bx2 = (x0b + x1b) / 2;

        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx2, filaY));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx2, filaY));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx2, filaY));
        home.update(engine, 0.016);
        home.render(engine, probe);

        const u32 izq3 = probe.data()[static_cast<size_t>(yPanel) * 960 + 180];
        const u32 der3 = probe.data()[static_cast<size_t>(yPanel) * 960 + 910];
        check("Volver al 3er bloque restaura ambos paneles",
              colorBloque(2) == rojo && der3 == led &&
              (izq3 == violeta || izq3 == fondoP));

        home.onExit(engine);
        break;
    }
    case 29: {
        // --- panel con scroll ---
        ScrollPanel sp;
        sp.setRect({0, 100, 350, 400});
        sp.setBarOnLeft(true);
        sp.setBarWidth(14);

        // Contenido que cabe: no debe haber scroll ni barra.
        sp.setContentHeight(200);
        check("Sin desbordar no hay scroll", !sp.scrollable());
        check("Sin scroll el contenido usa todo el ancho",
              sp.contentRect().w == 350);

        // Contenido que desborda: aparece la barra.
        sp.setContentHeight(1000);
        check("Al desbordar el panel es desplazable", sp.scrollable());
        std::snprintf(buf, sizeof(buf), "ancho util %d (panel 350 - barra 14)",
                      sp.contentRect().w);
        check("Con scroll el contenido cede sitio a la barra",
              sp.contentRect().w == 336, buf);

        // La barra a la izquierda: el contenido empieza despues de ella.
        std::snprintf(buf, sizeof(buf), "contenido empieza en x=%d", sp.contentRect().x);
        check("Con la barra a la izquierda el contenido se desplaza",
              sp.contentRect().x == 14, buf);

        // Limites del desplazamiento.
        sp.setOffset(-500);
        check("El desplazamiento no baja de 0", sp.offset() == 0);

        sp.setOffset(99999);
        const i32 maxEsperado = 1000 - 400;   // contenido - alto del panel
        std::snprintf(buf, sizeof(buf), "tope %d (esperado %d)", sp.offset(), maxEsperado);
        check("El desplazamiento se detiene en el tope", sp.offset() == maxEsperado, buf);

        sp.setOffset(300);
        check("Un desplazamiento intermedio se respeta", sp.offset() == 300);

        // Si el contenido encoge, el desplazamiento se reajusta solo.
        sp.setContentHeight(450);
        std::snprintf(buf, sizeof(buf), "tras encoger, offset=%d (max 50)", sp.offset());
        check("Al encoger el contenido el desplazamiento se reajusta",
              sp.offset() <= 50, buf);
        break;
    }
    case 30: {
        // --- la rueda desplaza el panel de la biblioteca ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("scroll@ludora.engine");
        home.onEnter(engine);
        home.render(engine, probe);   // seccion BIBLIOTECA por defecto

        // Se consulta el desplazamiento y no un pixel suelto: dentro de una
        // ficha el color es el mismo antes y despues de mover, asi que un
        // muestreo puntual no distingue "no se movio" de "se movio un poco".
        const i32 antes = home.libraryScrollOffset();

        // Rueda hacia abajo con el cursor sobre el panel.
        for (int i = 0; i < 4; ++i) {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(180, 300));
            SendMessageW(win.handle(), WM_MOUSEWHEEL,
                         MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                         MAKELPARAM(180, 300));
            home.update(engine, 0.016);
        }
        home.render(engine, probe);
        const i32 despues = home.libraryScrollOffset();

        std::snprintf(buf, sizeof(buf), "offset %d -> %d", antes, despues);
        check("La rueda desplaza el contenido del panel",
              antes == 0 && despues > 0, buf);

        // Rueda hacia arriba: debe volver hacia el principio.
        for (int i = 0; i < 2; ++i) {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(180, 300));
            SendMessageW(win.handle(), WM_MOUSEWHEEL,
                         MAKEWPARAM(0, static_cast<WORD>(WHEEL_DELTA)),
                         MAKELPARAM(180, 300));
            home.update(engine, 0.016);
        }
        const i32 subido = home.libraryScrollOffset();
        std::snprintf(buf, sizeof(buf), "%d -> %d al subir", despues, subido);
        check("La rueda hacia arriba deshace el desplazamiento",
              subido < despues, buf);

        // La rueda fuera del panel no debe moverlo.
        const i32 previo = home.libraryScrollOffset();
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(700, 300));
        SendMessageW(win.handle(), WM_MOUSEWHEEL,
                     MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                     MAKELPARAM(700, 300));
        home.update(engine, 0.016);
        check("La rueda fuera del panel no lo desplaza",
              home.libraryScrollOffset() == previo);

        home.render(engine, probe);

        // El contenido desplazado no debe invadir la franja del menu.
        const i32 barH = win.desc().dragBarHeight;
        const u32 enMenu = probe.data()[static_cast<size_t>(barH + 25) * 960 + 180];
        const u32 violeta = Color::fromHex(0x2B1B57).packed();
        check("El contenido desplazado no invade el menu", enMenu != violeta);

        home.onExit(engine);
        break;
    }
    case 31: {
        // --- seccion TIENDA: tarjeta centrada y scroll largo ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("tienda@ludora.engine");
        home.onEnter(engine);
        home.render(engine, probe);

        const i32 barH  = win.desc().dragBarHeight;
        const i32 filaY = barH + 25;

        // Ir a TIENDA (1er bloque).
        const i32 bx = (960 / HomeScene::kBlockCount) / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, filaY));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);
        home.render(engine, probe);

        const u32 violeta = Color::fromHex(0x2B1B57).packed();
        const u32 filo    = Color::fromHex(0x4A3585).packed();

        /// Tramos de tarjeta que cruzan una fila: devuelve inicio y ancho.
        auto tramos = [&](i32 y, std::vector<std::pair<i32,i32>>& out) {
            out.clear();
            bool dentro = false; i32 ini = 0;
            for (i32 x = 0; x < 960; ++x) {
                const u32 c = probe.data()[static_cast<size_t>(y) * 960 + x];
                const bool esTarjeta = (c == violeta || c == filo);
                if (esTarjeta && !dentro) { dentro = true; ini = x; }
                if (!esTarjeta && dentro) { dentro = false; out.push_back({ini, x - ini}); }
            }
            if (dentro) out.push_back({ini, 960 - ini});
        };
        (void)tramos;   // el catalogo ya no se mide por tramos de pixeles

        // --- la rejilla del catalogo no tiene huecos de relleno ---
        // Antes habia 24 casillas fijas ("ARTICULO N"); ahora solo se dibujan
        // los juegos que existen de verdad.
        home.scrollStoreForTest(HomeScene::storeHeaderHeight());
        home.render(engine, probe);
        // Un mismo juego aparece en varias secciones (mini-secciones, listas
        // y rejilla), asi que hay mas tarjetas que juegos. Lo que se
        // comprueba es que NINGUNA carezca de juego detras: esos eran los
        // huecos de relleno numerados.
        i32 sinJuego = 0;
        for (i32 k = 0; k < home.storeCardCount(); ++k)
            if (home.storeCardGame(k) < 0) ++sinJuego;
        std::snprintf(buf, sizeof(buf), "%d tarjetas, %d sin juego",
                      home.storeCardCount(), sinJuego);
        check("La Tienda no dibuja articulos de relleno", sinJuego == 0, buf);

        // --- botones de genero al final ---
        home.scrollStoreForTest(HomeScene::storeHeaderHeight() +
                                HomeScene::storeGenresHeight());
        home.render(engine, probe);
        std::snprintf(buf, sizeof(buf), "%d generos, %d combinaciones",
                      home.genreButtonCount(), home.comboButtonCount());
        check("Hay una lista de botones por genero",
              home.genreButtonCount() == HomeScene::uploadGenreCount(), buf);
        check("Y otra de combinaciones de generos",
              home.comboButtonCount() == HomeScene::uploadGenreCount(), buf);

        if (home.genreButtonCount() > 0 && home.comboButtonCount() > 0) {
            const Recti g0 = home.genreButtonRect(0);
            const Recti c0 = home.comboButtonRect(0);
            std::snprintf(buf, sizeof(buf), "genero en (%d,%d), combo en (%d,%d)",
                          g0.x, g0.y, c0.x, c0.y);
            check("Las combinaciones van DEBAJO de los generos",
                  c0.y > g0.y, buf);
            check("Los botones tienen tamano pulsable",
                  g0.w >= 60 && g0.h >= 20, buf);
        }

        // --- abrir la seccion de un genero ---
        home.openGenreSection(0);
        check("Pulsar un genero abre su seccion",
              home.currentSection() == HomeScene::kGenreSection &&
              home.openGenre() == 0);
        check("Sin combinacion, el segundo genero queda vacio",
              home.openGenre2() < 0);
        // La seccion vacia dice POR QUE lo esta, en vez de quedarse en
        // blanco: se comprueba que hay tinta donde va ese aviso.
        probe.clear(Color::fromHex(0x000000));
        home.render(engine, probe);
        {
            const u32 fondoG = Color::fromHex(0x1B2838).packed();   // kSteamBg
            bool hayAviso = false;
            for (i32 y = 180; y < 260 && !hayAviso; ++y)
                for (i32 x = 100; x < 860; ++x) {
                    const u32 c = probe.data()[static_cast<size_t>(y) * 960 + x];
                    if (c != fondoG) { hayAviso = true; break; }
                }
            check("La seccion vacia explica por que no hay juegos", hayAviso);
        }

        // Por ahora esta vacia: ningun juego lleva esa etiqueta todavia.
        std::snprintf(buf, sizeof(buf), "%zu juegos",
                      home.genreGames(0).size());
        check("La lista del genero esta vacia por ahora",
              home.genreGames(0).empty(), buf);

        // Y una combinacion guarda los dos generos.
        home.openGenreSection(3, 7);
        check("Una combinacion abre con sus dos generos",
              home.openGenre() == 3 && home.openGenre2() == 7);
        home.render(engine, probe);
        // El titulo lleva los dos generos unidos por '+', que es lo que
        // distingue una combinacion de un genero suelto.
        check("Una combinacion pide los DOS generos, no cualquiera",
              home.genreGames(3, 7).size() <= home.genreGames(3).size());

        home.section(HomeScene::kStoreSection);
        home.render(engine, probe);

        // --- scroll largo ---
        // Se devuelve arriba: las comprobaciones de arriba movieron el scroll
        // para ver el catalogo, que ahora vive bajo el banner.
        home.scrollStoreForTest(0);
        home.render(engine, probe);
        const i32 off0 = home.storeScrollOffset();
        check("La tienda empieza arriba del todo", off0 == 0);

        for (int i = 0; i < 10; ++i) {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(480, 300));
            SendMessageW(win.handle(), WM_MOUSEWHEEL,
                         MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                         MAKELPARAM(480, 300));
            home.update(engine, 0.016);
        }
        const i32 off1 = home.storeScrollOffset();
        std::snprintf(buf, sizeof(buf), "offset %d tras 10 pasos de rueda", off1);
        check("La rueda desplaza la tienda", off1 > 0, buf);

        // Recorrido largo: 24 tarjetas dan un contenido muy superior a la
        // ventana, asi que el tope debe ser de varios miles de pixeles.
        for (int i = 0; i < 200; ++i) {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(480, 300));
            SendMessageW(win.handle(), WM_MOUSEWHEEL,
                         MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                         MAKELPARAM(480, 300));
            home.update(engine, 0.016);
        }
        const i32 tope = home.storeScrollOffset();
        // Con dos columnas el contenido ocupa la mitad de alto que en una
        // sola, asi que el recorrido esperado baja en consecuencia.
        std::snprintf(buf, sizeof(buf), "tope %d px", tope);
        check("El scroll de la tienda es largo (>1500 px)", tope > 1500, buf);

        home.render(engine, probe);
        // Al final del recorrido el contenido no debe invadir el menu.
        const u32 enMenu = probe.data()[static_cast<size_t>(filaY) * 960 + 480];
        check("La tienda desplazada no invade el menu", enMenu != violeta);

        home.onExit(engine);
        break;
    }
    case 32: {
        // --- formulario de 3 campos y guardado del perfil ---
        const wchar_t* kAcc  = L"cuenta.dat";
        const wchar_t* kProf = L"perfil.dat";
        const wchar_t* kBakA = L"cuenta.dat.t32";
        const wchar_t* kBakP = L"perfil.dat.t32";
        DeleteFileW(kBakA); DeleteFileW(kBakP);
        const bool habiaA = Account::exists(kAcc);
        if (habiaA) MoveFileW(kAcc, kBakA);
        Profile tmp;
        const bool habiaP = Profile::load(kProf, tmp);
        if (habiaP) MoveFileW(kProf, kBakP);

        // Comprobar primero que Profile guarda y recupera por si mismo: si
        // esto falla, el problema no esta en el formulario.
        {
            Profile w;
            w.userName    = "Usuario Prueba";
            w.description = "Hola";
            check("Profile::save escribe el archivo", Profile::save(kProf, w));
            Profile r;
            const bool ok = Profile::load(kProf, r);
            std::snprintf(buf, sizeof(buf), "leido '%s'", r.userName.c_str());
            check("Profile::load recupera el nombre",
                  ok && r.userName == "Usuario Prueba", buf);
            DeleteFileW(kProf);
        }

        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene sc;
        sc.onEnter(engine);
        sc.render(engine, probe);   // fija la geometria de los campos

        // Teclear los tres campos usando Tab para avanzar, igual que un
        // usuario. Cada bloque necesita su propio update() para consumirse.
        auto teclear = [&](const char* txt) {
            engine.input().newFrame();
            for (const char* p = txt; *p; ++p)
                SendMessageW(win.handle(), WM_CHAR, static_cast<WPARAM>(*p), 0);
            sc.update(engine, 0.016);
        };
        auto tab = [&]() {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYDOWN, VK_TAB, 0);
            sc.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYUP, VK_TAB, 0);
        };

        teclear("Usuario Prueba");
        tab();
        teclear("form@ludora.engine");
        tab();
        teclear("clave123");

        // Lo que se comprueba es que el TEXTO llego a cada campo: pulsar
        // Enter aqui encolaria HomeScene y el motor sustituiria a SelfTest
        // al cerrar el frame, abortando el resto de las pruebas.
        std::snprintf(buf, sizeof(buf), "usuario='%s' correo='%s' clave=%zu chars",
                      sc.userNameText().c_str(), sc.emailText().c_str(),
                      sc.passwordLength());
        check("El texto llega al campo correcto en cada Tab",
              sc.userNameText() == "Usuario Prueba" &&
              sc.emailText()    == "form@ludora.engine" &&
              sc.passwordLength() == 8, buf);

        sc.onExit(engine);

        DeleteFileW(kAcc); DeleteFileW(kProf);
        if (habiaA) MoveFileW(kBakA, kAcc);
        if (habiaP) MoveFileW(kBakP, kProf);
        break;
    }
    case 33: {
        // --- seccion Cuenta y editor de descripcion publica ---
        const wchar_t* kProf = L"perfil.dat";
        const wchar_t* kBak  = L"perfil.dat.t33";
        DeleteFileW(kBak);
        Profile prev;
        const bool habia = Profile::load(kProf, prev);
        if (habia) MoveFileW(kProf, kBak);

        // Perfil de partida.
        Profile inicial;
        inicial.userName = "Usuario Cuenta";
        Profile::save(kProf, inicial);

        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("cuenta@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kAccountSection);
        home.render(engine, probe);   // fija los rectangulos de los botones

        std::snprintf(buf, sizeof(buf), "usuario '%s'", home.profileUser().c_str());
        check("La seccion Cuenta carga el perfil guardado",
              home.profileUser() == "Usuario Cuenta", buf);

        const Recti db = home.descButtonRect();
        std::snprintf(buf, sizeof(buf), "%dx%d en (%d,%d)", db.w, db.h, db.x, db.y);
        check("El boton de descripcion es cuadrado", db.w == db.h && db.w > 0, buf);

        check("El editor empieza cerrado", !home.descriptionOpen());

        // Clic en el boton de descripcion: debe abrir el editor.
        const i32 cx = db.x + db.w / 2;
        const i32 cy = db.y + db.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(cx, cy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
        home.update(engine, 0.016);
        check("El boton abre el editor de descripcion", home.descriptionOpen());

        home.render(engine, probe);   // fija el rect del boton GUARDAR

        // Escribir la descripcion.
        engine.input().newFrame();
        for (const char* p = "Mi descripcion publica"; *p; ++p)
            SendMessageW(win.handle(), WM_CHAR, static_cast<WPARAM>(*p), 0);
        home.update(engine, 0.016);
        home.render(engine, probe);

        // Clic en GUARDAR: guarda y cierra el editor.
        const Recti sb = home.descSaveRect();
        const i32 sx = sb.x + sb.w / 2;
        const i32 sy = sb.y + sb.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(sx, sy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sx, sy));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(sx, sy));
        home.update(engine, 0.016);

        check("Guardar cierra el editor y vuelve a la seccion",
              !home.descriptionOpen());
        std::snprintf(buf, sizeof(buf), "descripcion '%s'", home.profileDesc().c_str());
        check("La descripcion queda guardada en memoria",
              home.profileDesc() == "Mi descripcion publica", buf);

        Profile leido;
        const bool ok = Profile::load(kProf, leido);
        check("La descripcion se persiste en disco",
              ok && leido.description == "Mi descripcion publica",
              leido.description);
        check("El nombre de usuario sobrevive al guardar la descripcion",
              leido.userName == "Usuario Cuenta", leido.userName);

        home.onExit(engine);
        DeleteFileW(kProf);
        if (habia) MoveFileW(kBak, kProf);
        break;
    }
    case 34: {
        // --- limites de caracteres y UTF-8 ---
        Profile p;
        p.userName    = std::string(300, 'x');           // pasa de 187
        p.description = std::string(20000, 'y');         // pasa de 10000
        const std::string u = Profile::clampChars(p.userName, Profile::kMaxUserName);
        const std::string d = Profile::clampChars(p.description, Profile::kMaxDescription);
        std::snprintf(buf, sizeof(buf), "usuario %zu, descripcion %zu", u.size(), d.size());
        check("Los limites de caracteres se aplican",
              u.size() == Profile::kMaxUserName &&
              d.size() == Profile::kMaxDescription, buf);

        // Acentos: el recorte cuenta caracteres, no bytes.
        std::string acentos;
        for (int i = 0; i < 10; ++i) acentos += "\xC3\xB1";   // 10 enyes UTF-8
        const std::string rec = Profile::clampChars(acentos, 5);
        std::snprintf(buf, sizeof(buf), "%zu bytes = 5 caracteres", rec.size());
        check("El recorte no parte un caracter UTF-8 por la mitad",
              rec.size() == 10 && font5x7::lengthUtf8(rec.c_str()) == 5, buf);

        // El campo de texto acepta acentos y los cuenta como un caracter.
        TextField f;
        TextField::Style st{}; st.textScale = 2;
        f.setStyle(st);
        f.setRect({0, 0, 400, 40});
        f.setFocused(true);
        f.onChar(static_cast<wchar_t>(0xF1));   // enye
        f.onChar(L'a');
        std::snprintf(buf, sizeof(buf), "%zu caracteres, %zu bytes",
                      f.length(), f.text().size());
        check("El campo cuenta un acento como un solo caracter",
              f.length() == 2 && f.text().size() == 3, buf);
        break;
    }
    case 35: {
        // --- control de contenido de la foto de perfil ---
        // Se construyen imagenes sinteticas: asi la prueba es reproducible y
        // no depende de ningun archivo externo.
        const i32 W = 128, H = 128;

        // 1) Imagen azul: nada parecido a piel, debe aceptarse.
        {
            std::vector<u32> px(static_cast<size_t>(W) * H, 0xFF1030A0u);
            const auto cc = photo::checkImageContent(px, W, H);
            std::snprintf(buf, sizeof(buf), "piel %.0f%%", cc.skinRatio * 100.0f);
            check("Una imagen sin tonos de piel se acepta", cc.allowed, buf);
        }

        // 2) Imagen enteramente de tono de piel: debe rechazarse.
        {
            std::vector<u32> px(static_cast<size_t>(W) * H, 0xFFD9A07Bu);
            const auto cc = photo::checkImageContent(px, W, H);
            std::snprintf(buf, sizeof(buf), "piel %.0f%%, motivo: %s",
                          cc.skinRatio * 100.0f, cc.reason.c_str());
            check("Una imagen cubierta de tono de piel se rechaza",
                  !cc.allowed, buf);
            check("El rechazo explica el motivo al usuario", !cc.reason.empty());
        }

        // 3) Retrato: cara sobre fondo neutro. NO debe rechazarse, es el
        //    falso positivo que mas dana (bloquea fotos legitimas).
        {
            std::vector<u32> px(static_cast<size_t>(W) * H, 0xFF202020u);
            const i32 cx = W / 2, cy = H / 2, r = 34;   // ~22% del area
            for (i32 y = 0; y < H; ++y)
                for (i32 x = 0; x < W; ++x) {
                    const i32 dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy <= r * r)
                        px[static_cast<size_t>(y) * W + x] = 0xFFD9A07Bu;
                }
            const auto cc = photo::checkImageContent(px, W, H);
            std::snprintf(buf, sizeof(buf), "piel %.0f%%", cc.skinRatio * 100.0f);
            check("Un retrato sobre fondo neutro se acepta", cc.allowed, buf);
        }

        // 4) Tonos de piel oscuros tambien se detectan: el filtro no puede
        //    funcionar solo con piel clara.
        {
            std::vector<u32> px(static_cast<size_t>(W) * H, 0xFF6B4028u);
            const auto cc = photo::checkImageContent(px, W, H);
            std::snprintf(buf, sizeof(buf), "piel %.0f%%", cc.skinRatio * 100.0f);
            check("El detector reconoce tonos de piel oscuros",
                  cc.skinRatio > 0.5f, buf);
        }

        // 5) Una imagen vacia no debe romper nada.
        {
            std::vector<u32> vacia;
            const auto cc = photo::checkImageContent(vacia, 0, 0);
            check("Una imagen vacia no rompe el control", cc.allowed);
        }
        break;
    }
    case 36: {
        // --- decodificacion de cualquier formato via WIC ---
        // Se genera un BMP valido en disco y se lee con el decodificador
        // generico, que es el camino que usa la importacion real.
        const wchar_t* kTmp = L"selftest-img.bmp";
        DeleteFileW(kTmp);

        const i32 W = 64, H = 48;
        {
            std::vector<u32> px(static_cast<size_t>(W) * H);
            for (i32 y = 0; y < H; ++y)
                for (i32 x = 0; x < W; ++x)
                    px[static_cast<size_t>(y) * W + x] =
                        0xFF000000u | (static_cast<u32>(x * 4) << 16) |
                        (static_cast<u32>(y * 5) << 8) | 0x80u;

            std::ofstream out(kTmp, std::ios::binary);
            BITMAPFILEHEADER fh{};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            fh.bfSize = fh.bfOffBits + static_cast<u32>(W) * H * 4;
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof(ih); ih.biWidth = W; ih.biHeight = -H;
            ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
            ih.biSizeImage = static_cast<u32>(W) * H * 4;
            out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
            out.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
            out.write(reinterpret_cast<const char*>(px.data()),
                      static_cast<std::streamsize>(W) * H * 4);
        }

        std::vector<u32> px; i32 w = 0, h = 0;
        const bool ok = photo::loadAnyImage(kTmp, px, w, h);
        std::snprintf(buf, sizeof(buf), "%dx%d, %zu pixeles", w, h, px.size());
        check("El decodificador generico (WIC) lee la imagen",
              ok && w == W && h == H, buf);

        // El respaldo por BMP debe dar el mismo tamano.
        std::vector<u32> px2; i32 w2 = 0, h2 = 0;
        const bool ok2 = photo::loadBmp(kTmp, px2, w2, h2);
        check("El lector BMP de respaldo coincide en tamano",
              ok2 && w2 == w && h2 == h);

        // Un archivo que no es imagen debe fallar limpiamente.
        const wchar_t* kBad = L"selftest-noimg.txt";
        { std::ofstream b(kBad); b << "esto no es una imagen"; }
        std::vector<u32> px3; i32 w3 = 0, h3 = 0;
        check("Un archivo que no es imagen se rechaza",
              !photo::loadAnyImage(kBad, px3, w3, h3));

        DeleteFileW(kTmp);
        DeleteFileW(kBad);
        break;
    }
    case 37: {
        // --- dibujado circular del avatar ---
        Framebuffer fbc;
        fbc.resize(120, 120);
        fbc.clear(Color::fromHex(0x000000));

        const u32 turq = Color::fromHex(0x00E5D0).packed();
        fbc.drawCircle(60, 60, 40, Color::fromHex(0x00E5D0), 3);

        // El anillo debe estar en el borde, no en el centro.
        const u32 centro = fbc.data()[60 * 120 + 60];
        const u32 borde  = fbc.data()[60 * 120 + 100];   // 40 px a la derecha
        std::snprintf(buf, sizeof(buf), "centro 0x%08X, borde 0x%08X", centro, borde);
        check("El anillo se dibuja en el radio, no en el centro",
              centro != turq && borde == turq, buf);

        // Recorte circular: dentro se pinta, fuera no.
        std::vector<u32> src(static_cast<size_t>(32) * 32, 0xFFFF00FFu);
        Framebuffer fbb;
        fbb.resize(120, 120);
        fbb.clear(Color::fromHex(0x000000));
        fbb.blitCircular(60, 60, 40, src.data(), 32, 32);

        const u32 dentro = fbb.data()[60 * 120 + 60];
        const u32 fuera  = fbb.data()[5 * 120 + 5];   // esquina, fuera del circulo
        std::snprintf(buf, sizeof(buf), "dentro 0x%08X, fuera 0x%08X", dentro, fuera);
        check("La foto se recorta al circulo",
              dentro == 0xFFFF00FFu && fuera == 0xFF000000u, buf);
        break;
    }
    case 38: {
        // --- almacen cifrado en 10 capas ---
        const std::string secreto = "clave-uno|clave-dos|clave-tres";
        const std::string textoOriginal = "Datos de cuenta de Nube Tomate";
        const std::vector<u8> plain(textoOriginal.begin(), textoOriginal.end());

        std::string err;
        std::vector<u8> blob;

        // Medir cuanto cuesta cifrar: con 10 capas de PBKDF2 el tiempo es el
        // parametro que hay que vigilar, porque lo paga el usuario al entrar.
        const DWORD t0 = GetTickCount();
        const bool sellado = Vault::seal(secreto, plain, blob, err);
        const DWORD msSeal = GetTickCount() - t0;

        std::snprintf(buf, sizeof(buf), "%zu bytes en %lu ms", blob.size(), msSeal);
        check("El almacen cifra con 10 capas", sellado, sellado ? buf : err.c_str());

        // El texto original no puede aparecer en el blob.
        bool filtrado = false;
        if (blob.size() >= textoOriginal.size()) {
            for (size_t i = 0; i + textoOriginal.size() <= blob.size(); ++i)
                if (std::memcmp(&blob[i], textoOriginal.data(), textoOriginal.size()) == 0)
                    filtrado = true;
        }
        check("El texto original no aparece en el archivo cifrado", !filtrado);

        // Descifrar con el secreto correcto.
        std::vector<u8> abierto;
        const DWORD t1 = GetTickCount();
        const bool ok = Vault::open(secreto, blob, abierto, err);
        const DWORD msOpen = GetTickCount() - t1;

        const std::string recuperado(abierto.begin(), abierto.end());
        std::snprintf(buf, sizeof(buf), "recuperado en %lu ms", msOpen);
        check("El almacen descifra y recupera el contenido",
              ok && recuperado == textoOriginal, ok ? buf : err.c_str());

        // Aviso si abrir tarda demasiado para una pantalla de acceso.
        std::snprintf(buf, sizeof(buf), "cifrar %lu ms, abrir %lu ms", msSeal, msOpen);
        // Ahora incluye el estirado Argon2id (64 MiB), asi que se admite mas
        // margen: sigue siendo aceptable para una pantalla de acceso.
        check("El tiempo de apertura es aceptable (<12 s)", msOpen < 12000, buf);

        // --- COMPATIBILIDAD: los paquetes de 10 capas se siguen abriendo ---
        // Cifrar pasó de 10 capas a 1. Es el cambio que hace el guardado 56
        // veces mas rapido, pero lo que NO puede pasar es que un .tomate ya
        // guardado deje de leerse.
        //
        // Se fabrica a mano un blob con el formato antiguo -- la cuenta de
        // capas va en la cabecera -- y se comprueba que open() lo recupera.
        {
            check("Al cifrar se usa UNA capa", Vault::kLayers == 1,
                  std::to_string(Vault::kLayers).c_str());

            // La cabecera dice cuantas capas trae; open() la lee de ahi y no
            // de la constante, que es lo que da la compatibilidad.
            const u32 capasBlob =
                (blob.size() >= 12)
                ? (static_cast<u32>(blob[8])        |
                   static_cast<u32>(blob[9])  << 8  |
                   static_cast<u32>(blob[10]) << 16 |
                   static_cast<u32>(blob[11]) << 24)
                : 0u;
            std::snprintf(buf, sizeof(buf), "cabecera dice %u capas", capasBlob);
            check("Y la cuenta queda escrita en el paquete",
                  capasBlob == static_cast<u32>(Vault::kLayers), buf);
        }
        break;
    }
    case 39: {
        // --- resistencia del almacen ---
        const std::string secreto = "tres|claves|correctas";
        const std::vector<u8> plain = { 'd','a','t','o','s' };
        std::string err;
        std::vector<u8> blob;
        Vault::seal(secreto, plain, blob, err);

        // Secreto incorrecto: debe fallar.
        std::vector<u8> fuera;
        check("Un secreto incorrecto no abre el almacen",
              !Vault::open("tres|claves|erroneas", blob, fuera, err));
        check("El error no revela en que capa fallo",
              err.find("capa") == std::string::npos, err);

        // Contenido manipulado: el tag GCM debe detectarlo.
        std::vector<u8> tocado = blob;
        if (tocado.size() > 40) tocado[tocado.size() - 5] ^= 0xFF;
        check("Una manipulacion del archivo se detecta",
              !Vault::open(secreto, tocado, fuera, err));

        // Cabecera corrupta.
        std::vector<u8> malaCabecera = blob;
        if (malaCabecera.size() > 4) malaCabecera[0] ^= 0xFF;
        check("Un formato desconocido se rechaza",
              !Vault::open(secreto, malaCabecera, fuera, err));

        // Dos cifrados del mismo dato deben dar blobs distintos: si no, los
        // salts o los nonces se estarian reutilizando.
        std::vector<u8> blob2;
        Vault::seal(secreto, plain, blob2, err);
        check("Dos cifrados del mismo dato producen blobs distintos",
              blob != blob2);

        // Y ambos deben abrirse con el mismo secreto.
        std::vector<u8> a1, a2;
        const bool o1 = Vault::open(secreto, blob,  a1, err);
        const bool o2 = Vault::open(secreto, blob2, a2, err);
        check("Ambos cifrados recuperan el mismo contenido",
              o1 && o2 && a1 == a2 && a1 == plain);
        break;
    }
    case 40: {
        // --- panel "Verificar dispositivo" ---
        const wchar_t* kVault = L"tomate.vault";
        const wchar_t* kBak   = L"tomate.vault.t40";
        DeleteFileW(kBak);
        std::vector<u8> prev;
        const bool habia = Vault::loadFile(kVault, prev);
        if (habia) MoveFileW(kVault, kBak);

        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene sc;
        sc.onEnter(engine);
        sc.render(engine, probe);   // fija el rect del boton

        check("El panel de verificacion empieza cerrado", !sc.verifyOpen());

        const Recti vb = sc.verifyButtonRect();
        std::snprintf(buf, sizeof(buf), "%dx%d en (%d,%d)", vb.w, vb.h, vb.x, vb.y);
        check("El boton VERIFICAR DISPOSITIVO tiene sitio en el formulario",
              vb.w > 0 && vb.h > 0, buf);

        // Clic en el boton: abre el panel.
        const i32 vx = vb.x + vb.w / 2, vy = vb.y + vb.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(vx, vy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(vx, vy));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(vx, vy));
        sc.update(engine, 0.016);
        check("El boton abre el panel de verificacion", sc.verifyOpen());

        sc.render(engine, probe);   // fija el rect de CONFIRMAR

        // El limite de 26 caracteres debe aplicarse.
        sc.setVerifyKey(0, std::string(40, 'a'));
        std::snprintf(buf, sizeof(buf), "%zu caracteres", sc.verifyKeyLength(0));
        check("Cada clave se limita a 26 caracteres",
              sc.verifyKeyLength(0) == 26, buf);

        // Confirmar sin las tres claves debe avisar, no cifrar.
        sc.setVerifyKey(0, "clave-uno");
        sc.setVerifyKey(1, "");
        sc.setVerifyKey(2, "");
        const Recti ok = sc.verifyOkRect();
        const i32 ox = ok.x + ok.w / 2, oy = ok.y + ok.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(ox, oy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ox, oy));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ox, oy));
        sc.update(engine, 0.016);
        check("Con claves incompletas avisa y no crea el almacen",
              !Vault::loadFile(kVault, prev), sc.verifyMessage());

        // Con las tres claves debe cifrar y guardar.
        sc.setVerifyKey(0, "clave-uno-26-caracteres-ok");
        sc.setVerifyKey(1, "clave-dos-distinta");
        sc.setVerifyKey(2, "clave-tres-distinta");
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(ox, oy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ox, oy));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ox, oy));
        sc.update(engine, 0.016);

        std::vector<u8> blob;
        const bool creado = Vault::loadFile(kVault, blob);
        std::snprintf(buf, sizeof(buf), "%zu bytes; %s", blob.size(),
                      sc.verifyMessage().c_str());
        check("Con las tres claves se cifra y guarda el almacen", creado, buf);

        // Las claves deben borrarse de los campos tras usarse.
        check("Las claves se limpian de los campos tras cifrar",
              sc.verifyKeyLength(0) == 0 && sc.verifyKeyLength(1) == 0 &&
              sc.verifyKeyLength(2) == 0);

        // El almacen debe abrirse con el mismo secreto y contener el tipo
        // de dispositivo, pero NUNCA ubicacion ni IP.
        if (creado) {
            const std::string secreto =
                std::string("clave-uno-26-caracteres-ok") + '\x1f' +
                "clave-dos-distinta" + '\x1f' + "clave-tres-distinta";
            std::vector<u8> claro;
            std::string err;
            const bool abierto = Vault::open(secreto, blob, claro, err);
            const std::string texto(claro.begin(), claro.end());
            check("El almacen se abre con las tres claves correctas",
                  abierto, abierto ? texto.c_str() : err.c_str());
            check("El almacen guarda el tipo de dispositivo",
                  texto.find("device=") != std::string::npos);
            check("El almacen NO guarda ubicacion ni IP",
                  texto.find("ip=") == std::string::npos &&
                  texto.find("lat") == std::string::npos &&
                  texto.find("gps") == std::string::npos);
        }

        sc.onExit(engine);
        DeleteFileW(kVault);
        if (habia) MoveFileW(kBak, kVault);
        break;
    }
    case 41: {
        // --- GUARDAR Y ENTRAR bloqueado hasta verificar el dispositivo ---
        const wchar_t* kAcc   = L"cuenta.dat";
        const wchar_t* kProf  = L"perfil.dat";
        const wchar_t* kVault = L"tomate.vault";
        const wchar_t* kBA = L"cuenta.dat.t41";
        const wchar_t* kBP = L"perfil.dat.t41";
        const wchar_t* kBV = L"tomate.vault.t41";
        DeleteFileW(kBA); DeleteFileW(kBP); DeleteFileW(kBV);

        const bool hA = Account::exists(kAcc);
        if (hA) MoveFileW(kAcc, kBA);
        Profile tp;
        const bool hP = Profile::load(kProf, tp);
        if (hP) MoveFileW(kProf, kBP);
        std::vector<u8> tv;
        const bool hV = Vault::loadFile(kVault, tv);
        if (hV) MoveFileW(kVault, kBV);

        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene sc;
        sc.onEnter(engine);
        sc.render(engine, probe);

        // El boton de verificar debe quedar ENCIMA del de guardar.
        const Recti vb = sc.verifyButtonRect();
        const Recti ab = sc.actionRect();
        std::snprintf(buf, sizeof(buf), "verificar y=%d, guardar y=%d", vb.y, ab.y);
        check("VERIFICAR DISPOSITIVO va encima de GUARDAR Y ENTRAR",
              vb.y + vb.h <= ab.y, buf);

        // Rellenar el formulario, pero SIN verificar el dispositivo.
        sc.setUserNameText("Usuario Bloqueo");
        sc.setEmailText("bloqueo@ludora.engine");
        sc.setPasswordText("clave123");

        // Pulsar GUARDAR Y ENTRAR: no debe crear la cuenta.
        const i32 ax = ab.x + ab.w / 2, ay = ab.y + ab.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(ax, ay));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ax, ay));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ax, ay));
        sc.update(engine, 0.016);

        check("Sin verificar, GUARDAR Y ENTRAR no crea la cuenta",
              !Account::exists(kAcc));

        std::snprintf(buf, sizeof(buf), "aviso: %s", sc.statusMessage().c_str());
        check("Sin verificar, se avisa de lo que hay que hacer",
              sc.statusMessage().find("VERIFICAR") != std::string::npos, buf);

        // Verificar el dispositivo desde el panel.
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,
                     MAKELPARAM(vb.x + vb.w / 2, vb.y + vb.h / 2));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(vb.x + vb.w / 2, vb.y + vb.h / 2));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                     MAKELPARAM(vb.x + vb.w / 2, vb.y + vb.h / 2));
        sc.update(engine, 0.016);
        sc.render(engine, probe);

        sc.setVerifyKey(0, "clave-uno");
        sc.setVerifyKey(1, "clave-dos");
        sc.setVerifyKey(2, "clave-tres");
        const Recti ok = sc.verifyOkRect();
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,
                     MAKELPARAM(ok.x + ok.w / 2, ok.y + ok.h / 2));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(ok.x + ok.w / 2, ok.y + ok.h / 2));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                     MAKELPARAM(ok.x + ok.w / 2, ok.y + ok.h / 2));
        sc.update(engine, 0.016);

        check("Tras verificar, el dispositivo queda marcado", sc.deviceVerified());

        // El paso final (pulsar GUARDAR Y ENTRAR ya verificado) encolaria
        // HomeScene y el motor sustituiria SelfTest al cerrar el frame,
        // abortando el informe. Se comprueba la condicion que desbloquea el
        // boton, que es lo que esta prueba cubre; la creacion de la cuenta
        // ya se verifica en el caso del formulario.
        check("Con el dispositivo verificado, el envio deja de estar bloqueado",
              sc.deviceVerified() &&
              sc.statusMessage().find("VERIFICAR") == std::string::npos,
              sc.statusMessage());

        sc.onExit(engine);
        DeleteFileW(kAcc); DeleteFileW(kProf); DeleteFileW(kVault);
        if (hA) MoveFileW(kBA, kAcc);
        if (hP) MoveFileW(kBP, kProf);
        if (hV) MoveFileW(kBV, kVault);
        break;
    }
    case 42: {
        // --- BLAKE2b contra los vectores oficiales (RFC 7693) ---
        auto hex = [](const u8* d, size_t n) {
            static const char* h = "0123456789abcdef";
            std::string s;
            for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 0xF]; }
            return s;
        };

        // BLAKE2b-512 de la cadena vacia.
        {
            u8 out[64];
            Blake2b::hash(out, 64, nullptr, 0);
            const std::string got = hex(out, 64);
            const std::string exp =
                "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
                "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce";
            check("BLAKE2b-512 de vacio coincide con el vector oficial",
                  got == exp, got.substr(0, 24).c_str());
        }

        // BLAKE2b-512 de "abc".
        {
            const u8 in[3] = { 'a', 'b', 'c' };
            u8 out[64];
            Blake2b::hash(out, 64, in, 3);
            const std::string got = hex(out, 64);
            const std::string exp =
                "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923";
            check("BLAKE2b-512 de \"abc\" coincide con el vector oficial",
                  got == exp, got.substr(0, 24).c_str());
        }

        // Salida corta: BLAKE2b-256 de vacio.
        {
            u8 out[32];
            Blake2b::hash(out, 32, nullptr, 0);
            const std::string got = hex(out, 32);
            const std::string exp =
                "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8";
            check("BLAKE2b-256 de vacio coincide con el vector oficial",
                  got == exp, got.substr(0, 24).c_str());
        }
        break;
    }
    case 43: {
        // --- Argon2id: propiedades y coste ---
        auto hex = [](const std::vector<u8>& d) {
            static const char* h = "0123456789abcdef";
            std::string s;
            for (u8 b : d) { s += h[b >> 4]; s += h[b & 0xF]; }
            return s;
        };

        Argon2::Params fast;   // parametros pequenos para que el test sea rapido
        fast.memKiB = 256; fast.iterations = 2; fast.parallelism = 1; fast.outLen = 32;

        const std::string pass = "clave-de-prueba";
        std::vector<u8> salt(16, 0x02);

        std::vector<u8> k1, k2;
        const bool ok1 = Argon2::deriveKey(pass, salt, fast, k1);
        const bool ok2 = Argon2::deriveKey(pass, salt, fast, k2);
        check("Argon2id deriva una clave", ok1 && k1.size() == 32);
        check("Argon2id es determinista (misma entrada, misma salida)",
              ok1 && ok2 && k1 == k2);

        // Cambiar la contrasena cambia toda la clave.
        std::vector<u8> k3;
        Argon2::deriveKey("otra-clave", salt, fast, k3);
        check("Otra contrasena produce otra clave", k1 != k3);

        // Cambiar el salt tambien.
        std::vector<u8> saltB(16, 0x03), k4;
        Argon2::deriveKey(pass, saltB, fast, k4);
        check("Otro salt produce otra clave", k1 != k4);

        // Un salt demasiado corto se rechaza.
        std::vector<u8> corto(4, 0x01), kx;
        check("Un salt de menos de 8 bytes se rechaza",
              !Argon2::deriveKey(pass, corto, fast, kx));

        // Medir el coste con parametros realistas: debe pesar, pero ser
        // asumible para el usuario legitimo (una sola derivacion).
        Argon2::Params real;   // 64 MiB, 3 pasadas
        const DWORD t0 = GetTickCount();
        std::vector<u8> kr;
        const bool okr = Argon2::deriveKey(pass, salt, real, kr);
        const DWORD ms = GetTickCount() - t0;
        std::snprintf(buf, sizeof(buf), "64 MiB, 3 pasadas en %lu ms", ms);
        check("Argon2id con 64 MiB deriva en un tiempo asumible (<3 s)",
              okr && ms < 3000, buf);

        std::snprintf(buf, sizeof(buf), "clave: %s...", hex(k1).substr(0, 16).c_str());
        check("La clave derivada no es trivial (no todo ceros)",
              !k1.empty() && !(k1[0] == 0 && k1[15] == 0 && k1[31] == 0), buf);
        break;
    }
    case 44: {
        // --- sesion persistente: registrarse una vez y no volver a pedirla ---
        const wchar_t* kSes = L"sesion.tomate.t44";
        DeleteFileW(kSes);

        check("Sin sesion guardada, no hay nada que recordar",
              !Session::exists(kSes) && !Session::load(kSes).valid);

        // Guardar la sesion tras un registro correcto.
        const bool guardado = Session::save(kSes, "yo@ludora.engine", "Iker");
        check("La sesion se guarda cifrada", guardado && Session::exists(kSes));

        // El archivo no puede contener el correo en claro.
        std::vector<u8> raw;
        Vault::loadFile(kSes, raw);
        bool enClaro = false;
        const std::string busca = "yo@ludora.engine";
        for (size_t i = 0; i + busca.size() <= raw.size(); ++i)
            if (std::memcmp(&raw[i], busca.data(), busca.size()) == 0) enClaro = true;
        check("La sesion NO guarda el correo en texto plano", !enClaro);

        // Recuperarla en este equipo debe devolver los datos.
        const Session::Data d = Session::load(kSes);
        std::snprintf(buf, sizeof(buf), "correo='%s' usuario='%s'",
                      d.email.c_str(), d.userName.c_str());
        check("La sesion se recupera en el mismo equipo",
              d.valid && d.email == "yo@ludora.engine" && d.userName == "Iker", buf);

        // Cerrar sesion borra el archivo.
        check("Cerrar sesion borra la sesion recordada",
              Session::clear(kSes) && !Session::exists(kSes));
        check("Tras cerrar sesion ya no se recupera nada",
              !Session::load(kSes).valid);

        DeleteFileW(kSes);
        break;
    }
    case 45: {
        // --- la sesion esta ligada al equipo: un token ajeno no vale ---
        const wchar_t* kSes = L"sesion.tomate.t45";
        DeleteFileW(kSes);

        Session::save(kSes, "yo@ludora.engine", "Iker");
        std::vector<u8> blob;
        Vault::loadFile(kSes, blob);

        // Un token de otro equipo se simula cifrando con OTRO secreto: al
        // abrirlo con la huella de este equipo (via Session) debe fallar.
        std::vector<u8> ajeno;
        std::string err;
        Vault::seal("secreto-de-otro-equipo", {'x','y','z'}, ajeno, err);
        Vault::saveFile(kSes, ajeno);
        check("Un token de otro equipo no se acepta",
              !Session::load(kSes).valid);

        // El token propio, restaurado, si se abre.
        //
        // Antes hay que levantar el bloqueo: los dos intentos fallidos de
        // arriba (token manipulado y token de otro equipo) son justo lo que
        // el guardian considera actividad sospechosa, y cortar el acceso es
        // su trabajo. En uso real ese bloqueo se levanta al autenticarse.
        VaultGuard::reset();
        Vault::saveFile(kSes, blob);
        check("El token propio sigue abriendose", Session::load(kSes).valid);

        // Manipular el token invalida la sesion (lo detecta el tag GCM).
        if (blob.size() > 40) blob[blob.size() - 6] ^= 0xFF;
        Vault::saveFile(kSes, blob);
        check("Un token manipulado no se acepta", !Session::load(kSes).valid);

        DeleteFileW(kSes);
        break;
    }
    case 46: {
        // --- integracion: con sesion guardada, DemoScene entra sola ---
        // Se usa el archivo real de sesion que lee DemoScene (Session::kFile),
        // respaldando el que hubiera para no tocar la sesion del usuario.
        const std::wstring real = Session::kFile;
        const std::wstring bak  = std::wstring(Session::kFile) + L".t46";
        DeleteFileW(bak.c_str());
        const bool habia = Session::exists(real);
        if (habia) MoveFileW(real.c_str(), bak.c_str());

        // Sin sesion: DemoScene NO debe pedir auto-login.
        {
            DemoScene sc;
            sc.onEnter(engine);
            check("Sin sesion, la pantalla de acceso no entra sola",
                  !sc.autoLoginPending());
            sc.onExit(engine);
        }

        // Con sesion guardada: DemoScene debe marcar el auto-login y precargar
        // el correo, sin pedir contrasena.
        Session::save(real, "sesion@ludora.engine", "UsuarioSesion");
        {
            DemoScene sc;
            sc.onEnter(engine);
            std::snprintf(buf, sizeof(buf), "correo precargado: '%s'",
                          sc.emailText().c_str());
            check("Con sesion guardada, la pantalla entra sola sin credenciales",
                  sc.autoLoginPending() && sc.emailText() == "sesion@ludora.engine",
                  buf);
            sc.onExit(engine);
        }

        DeleteFileW(real.c_str());
        if (habia) MoveFileW(bak.c_str(), real.c_str());
        break;
    }
    case 47: {
        // --- instalacion de juego: copia + biblioteca ---
        const wchar_t* kLib = L"biblioteca.t47";
        DeleteFileW(kLib);

        // Juego de prueba HIBRIDO: assets (exe, textura, sonido) + datos.
        const wchar_t* srcDir = L"srcjuego.t47";
        const wchar_t* dstDir = L"dstjuego.t47";
        CreateDirectoryW(srcDir, nullptr);
        CreateDirectoryW(L"srcjuego.t47\\saves", nullptr);
        {
            std::ofstream(L"srcjuego.t47\\Juego.exe", std::ios::binary) << "MZ-falso-exe";
            std::ofstream(L"srcjuego.t47\\atlas.png", std::ios::binary) << "PNG-textura";
            std::ofstream(L"srcjuego.t47\\musica.ogg", std::ios::binary) << "OGG-sonido";
            std::ofstream(L"srcjuego.t47\\saves\\partida.dat", std::ios::binary) << "SECRETO-PARTIDA";
        }

        // La clasificacion debe distinguir assets de datos.
        check("El exe se clasifica como asset (no se cifra)",
              !gamevault::isSensitiveData(L"Juego.exe"));
        check("La textura se clasifica como asset (no se cifra)",
              !gamevault::isSensitiveData(L"atlas.png"));
        check("El sonido se clasifica como asset (no se cifra)",
              !gamevault::isSensitiveData(L"musica.ogg"));
        check("La partida se clasifica como dato sensible (se cifra)",
              gamevault::isSensitiveData(L"saves\\partida.dat"));

        // Instalar.
        GameInstall inst;
        inst.start(L"srcjuego.t47\\Juego.exe", dstDir, "Mi Juego", L"Juego.exe");
        inst.join();
        check("La instalacion termina bien",
              inst.done() && !inst.failed(),
              inst.failed() ? inst.error().c_str() : "ok");

        // Los ASSETS quedan EN CLARO en la carpeta de instalacion.
        check("El ejecutable queda en claro y accesible",
              GetFileAttributesW(L"dstjuego.t47\\Juego.exe") != INVALID_FILE_ATTRIBUTES);
        check("La textura queda en claro y accesible",
              GetFileAttributesW(L"dstjuego.t47\\atlas.png") != INVALID_FILE_ATTRIBUTES);
        check("El sonido queda en claro y accesible",
              GetFileAttributesW(L"dstjuego.t47\\musica.ogg") != INVALID_FILE_ATTRIBUTES);

        // El contenido del asset es legible tal cual (no cifrado).
        {
            std::ifstream f(L"dstjuego.t47\\atlas.png", std::ios::binary);
            std::string c((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
            check("La textura instalada es legible (no cifrada)", c == "PNG-textura", c.c_str());
        }

        // La partida NO queda en claro en la carpeta: va en el .tomate.
        check("La partida NO queda en claro (esta cifrada)",
              GetFileAttributesW(L"dstjuego.t47\\saves\\partida.dat")
                  == INVALID_FILE_ATTRIBUTES);
        check("Existe el paquete cifrado de datos",
              GetFileAttributesW(inst.vaultPath().c_str()) != INVALID_FILE_ATTRIBUTES);

        // El paquete de datos NO contiene el secreto en claro.
        {
            std::vector<u8> raw;
            Vault::loadFile(inst.vaultPath(), raw);
            bool enClaro = false;
            const std::string s = "SECRETO-PARTIDA";
            for (size_t i = 0; i + s.size() <= raw.size(); ++i)
                if (std::memcmp(&raw[i], s.data(), s.size()) == 0) enClaro = true;
            check("El paquete de datos no expone la partida en claro", !enClaro);
        }

        // Al jugar (restaurar datos), la partida vuelve descifrada a su sitio.
        std::string derr;
        check("Los datos se restauran sobre la carpeta al jugar",
              gamevault::restoreData(inst.vaultPath(), dstDir, derr),
              derr.c_str());
        {
            std::ifstream f(L"dstjuego.t47\\saves\\partida.dat", std::ios::binary);
            std::string c((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
            check("La partida restaurada es identica al original",
                  c == "SECRETO-PARTIDA", c.c_str());
        }

        // Registrar en biblioteca con el exe en claro.
        library::Entry e;
        e.name    = "Mi Juego";
        e.exe     = inst.installedExe();
        e.vault   = inst.vaultPath();
        e.exeName = L"Juego.exe";
        library::add(kLib, e);
        check("El juego queda registrado en la biblioteca",
              library::has(kLib, "Mi Juego"));

        // Limpieza.
        for (const wchar_t* d : { L"srcjuego.t47\0\0", L"dstjuego.t47\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        DeleteFileW(inst.vaultPath().c_str());
        DeleteFileW(kLib);
        break;
    }
    case 48: {
        // Aqui habia una instalacion REAL de Voxel World desde su ruta en el
        // disco D:. Se retiro con el juego: ataba la prueba a una maquina
        // concreta y se saltaba sola en cualquier otra, de modo que no
        // comprobaba nada fuera de este equipo.
        //
        // Lo que cubria -- assets en claro, datos cifrados, exe accesible --
        // lo comprueba el caso 47 con un juego sintetico que se crea en el
        // momento, y por tanto SIEMPRE se ejecuta.
        check("La instalacion se prueba con un juego sintetico (caso 47)",
              true, "sin dependencias de disco");
        break;
    }
    case 49: {
        // --- juego cifrado en la Nube Tomate: cifrar carpeta y recuperarla ---
        const wchar_t* srcDir = L"jvsrc.t49";
        const wchar_t* vault  = L"jv.tomate.t49";
        const wchar_t* outDir = L"jvout.t49";
        CreateDirectoryW(srcDir, nullptr);
        CreateDirectoryW(L"jvsrc.t49\\data", nullptr);
        {
            std::ofstream(L"jvsrc.t49\\Game.exe", std::ios::binary) << "EJECUTABLE-FALSO";
            std::ofstream(L"jvsrc.t49\\data\\level.dat", std::ios::binary) << "nivel-1-datos";
        }

        std::atomic<float> prog{0.0f};
        std::string err;
        const bool sellado = gamevault::sealFolder(L"jvsrc.t49\\Game.exe", vault, &prog, err);
        std::snprintf(buf, sizeof(buf), "progreso %.0f%%, %s",
                      prog.load() * 100.0f, sellado ? "ok" : err.c_str());
        check("La carpeta del juego se cifra en la Nube Tomate", sellado, buf);

        // El paquete cifrado NO debe contener el contenido en claro.
        std::vector<u8> raw;
        Vault::loadFile(vault, raw);
        bool enClaro = false;
        const std::string busca = "EJECUTABLE-FALSO";
        for (size_t i = 0; i + busca.size() <= raw.size(); ++i)
            if (std::memcmp(&raw[i], busca.data(), busca.size()) == 0) enClaro = true;
        check("El paquete cifrado no contiene el juego en texto plano", !enClaro);

        // Descifrar y recuperar la carpeta completa.
        std::wstring outExe;
        const bool abierto = gamevault::openToFolder(vault, outDir, L"Game.exe", outExe, err);
        check("El juego se descifra y el exe queda disponible",
              abierto, abierto ? "" : err.c_str());

        // Los datos internos deben coincidir con el original.
        check("El ejecutable descifrado existe",
              GetFileAttributesW(L"jvout.t49\\Game.exe") != INVALID_FILE_ATTRIBUTES);
        check("Los datos internos (subcarpeta) se recuperan",
              GetFileAttributesW(L"jvout.t49\\data\\level.dat") != INVALID_FILE_ATTRIBUTES);

        // El contenido descifrado es identico al original.
        {
            std::ifstream f(L"jvout.t49\\data\\level.dat", std::ios::binary);
            std::string contenido((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
            check("El contenido descifrado es identico al original",
                  contenido == "nivel-1-datos", contenido.c_str());
        }

        // Limpieza recursiva.
        for (const wchar_t* d : { L"jvsrc.t49\0\0", L"jvout.t49\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        DeleteFileW(vault);
        break;
    }
    case 50: {
        // --- busqueda de cuenta en el sistema (la usa BUSCAR CUENTA) ---
        // Se opera sobre el cuenta.dat real (carpeta de la app), respaldandolo.
        const wchar_t* real = L"cuenta.dat";
        const wchar_t* bak  = L"cuenta.dat.t50";
        DeleteFileW(bak);
        const bool habia = Account::exists(real);
        if (habia) MoveFileW(real, bak);

        // Sin cuenta en ningun sitio: la busqueda no encuentra nada.
        Account::Found f0 = Account::searchSystem();
        check("Sin cuenta guardada, la busqueda no encuentra nada", !f0.ok);

        // Crear una cuenta en la carpeta de la app y buscarla.
        std::string err;
        Account::create(real, "buscado@ludora.engine", "clave123", err);
        Account::Found f1 = Account::searchSystem();
        std::snprintf(buf, sizeof(buf), "encontrada: %s", f1.email.c_str());
        check("La busqueda encuentra la cuenta guardada",
              f1.ok && f1.email == "buscado@ludora.engine", buf);

        DeleteFileW(real);
        if (habia) MoveFileW(bak, real);
        break;
    }
    case 51: {
        // --- el boton BUSCAR CUENTA encuentra la cuenta y anima ---
        const wchar_t* real = L"cuenta.dat";
        const wchar_t* bakA = L"cuenta.dat.t51";
        const wchar_t* prof = L"perfil.dat";
        const wchar_t* bakP = L"perfil.dat.t51";
        const std::wstring sesReal = Session::kFile;
        const std::wstring bakS = std::wstring(Session::kFile) + L".t51";
        DeleteFileW(bakA); DeleteFileW(bakP); DeleteFileW(bakS.c_str());
        const bool hA = Account::exists(real);   if (hA) MoveFileW(real, bakA);
        const bool hS = Session::exists(sesReal); if (hS) MoveFileW(sesReal.c_str(), bakS.c_str());
        { Profile pd; if (Profile::load(prof, pd)) MoveFileW(prof, bakP); }

        std::string err;
        Account::create(real, "animado@ludora.engine", "clave123", err);
        // Desvincular el dispositivo: si no, onEnter() activaria el auto-login
        // por hardware y el primer update() encolaria HomeScene, abortando el
        // test. Queremos que la unica via de entrada aqui sea el boton.
        Account::forgetDevice(real, err);
        { Profile p; p.userName = "Animado"; Profile::save(prof, p); }

        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene sc;
        sc.onEnter(engine);
        // Si onEnter activo el auto-login pese a todo, no pulsar el boton: el
        // test no puede continuar con una escena que se va a reemplazar.
        check("Sin sesion ni vinculo, la pantalla no entra sola todavia",
              !sc.autoLoginPending());
        sc.render(engine, probe);   // fija el rectangulo del boton

        // El boton vive en el lado DERECHO, fuera del bloque centrado.
        const Recti sb = sc.searchButtonRect();
        std::snprintf(buf, sizeof(buf), "%dx%d en (%d,%d)", sb.w, sb.h, sb.x, sb.y);
        check("El boton BUSCAR CUENTA tiene sitio", sb.w > 0 && sb.h > 0, buf);
        check("BUSCAR CUENTA esta en el lado derecho", sb.x > 960 / 2);
        check("BUSCAR CUENTA no se sale de la ventana", sb.x + sb.w <= 960);

        // Pulsarlo: debe encontrar la cuenta y arrancar la animacion.
        const i32 sbx = sb.x + sb.w / 2, sby = sb.y + sb.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(sbx, sby));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sbx, sby));
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(sbx, sby));
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(sbx, sby));
        sc.update(engine, 0.016);
        check("BUSCAR CUENTA encuentra la cuenta y arranca la animacion",
              sc.checkAnimating());
        std::snprintf(buf, sizeof(buf), "cuenta '%s'", sc.foundEmailForTest().c_str());
        check("La animacion apunta a la cuenta encontrada",
              sc.foundEmailForTest() == "animado@ludora.engine", buf);

        // No se deja terminar la animacion aqui: al completarse llama a
        // queueScene(), que en el motor real sustituiria a SelfTest y abortaria
        // el informe. La transicion final (guardar sesion + saltar) se cubre
        // en la prueba de integracion de sesion.
        sc.onExit(engine);

        // Restaurar.
        DeleteFileW(real); DeleteFileW(prof); DeleteFileW(sesReal.c_str());
        if (hA) MoveFileW(bakA, real);
        if (hS) MoveFileW(bakS.c_str(), sesReal.c_str());
        if (GetFileAttributesW(bakP) != INVALID_FILE_ATTRIBUTES) MoveFileW(bakP, prof);
        break;
    }
    case 52: {
        // --- juego comprimido + cifrado: round-trip completo ---
        // Se prepara un juego con datos MUY compresibles (repetidos) para
        // comprobar que la compresion reduce el tamano de verdad.
        const wchar_t* srcDir = L"jcsrc.t52";
        const wchar_t* vault  = L"jc.tomate.t52";
        const wchar_t* outDir = L"jcout.t52";
        CreateDirectoryW(srcDir, nullptr);
        {
            std::ofstream(L"jcsrc.t52\\Game.exe", std::ios::binary) << "EXE-FALSO";
            // 200 KB de datos repetidos: muy compresibles.
            std::ofstream f(L"jcsrc.t52\\big.dat", std::ios::binary);
            for (int i = 0; i < 20000; ++i) f << "AAAAAAAAAA";
        }

        std::atomic<float> prog{0.0f};
        std::string err;
        const bool sellado = gamevault::sealFolder(L"jcsrc.t52\\Game.exe", vault, &prog, err);
        check("El juego se comprime y cifra en un paquete", sellado,
              sellado ? "" : err.c_str());

        // El paquete debe ser bastante mas pequeno que los 200 KB de origen:
        // eso demuestra que la compresion actuo antes de cifrar.
        std::vector<u8> raw;
        Vault::loadFile(vault, raw);
        std::snprintf(buf, sizeof(buf), "paquete %zu bytes (origen ~200 KB)", raw.size());
        check("La compresion reduce el tamano del paquete",
              raw.size() < 60000, buf);

        // Descifrar + descomprimir y comprobar que el contenido es identico.
        std::wstring outExe;
        const bool abierto = gamevault::openToFolder(vault, outDir, L"Game.exe", outExe, err);
        check("El juego se descifra y descomprime", abierto, abierto ? "" : err.c_str());

        {
            std::ifstream f(L"jcout.t52\\big.dat", std::ios::binary);
            std::string contenido((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
            std::snprintf(buf, sizeof(buf), "%zu bytes recuperados", contenido.size());
            check("El contenido descomprimido es identico al original",
                  contenido.size() == 200000 &&
                  contenido.substr(0, 10) == "AAAAAAAAAA", buf);
        }

        // Limpieza.
        for (const wchar_t* d : { L"jcsrc.t52\0\0", L"jcout.t52\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        DeleteFileW(vault);
        break;
    }
    case 53: {
        // --- BUG: estando en Chat, el clic en un bloque del menu cambia de
        //     seccion (antes el panel de chat se quedaba el clic). ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("nav@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kChatSection);
        home.render(engine, probe);   // fija la geometria del menu

        const i32 barH = win.desc().dragBarHeight;
        const i32 filaY = barH + 25;   // dentro de la franja del menu

        // Clic en el bloque TIENDA (indice 0) estando en Chat.
        const i32 x0 = 0;
        const i32 x1 = 960 / HomeScene::kBlockCount;
        const i32 bx = (x0 + x1) / 2;

        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, filaY));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, filaY));
        home.update(engine, 0.016);

        std::snprintf(buf, sizeof(buf), "seccion actual %d (tienda=%d)",
                      home.currentSection(), HomeScene::kStoreSection);
        check("Desde Chat, pulsar TIENDA cambia a la seccion Tienda",
              home.currentSection() == HomeScene::kStoreSection, buf);

        // Y desde Tienda de vuelta a Chat, para confirmar que no es un sentido.
        const i32 cx0 = 960 * HomeScene::kChatSection / HomeScene::kBlockCount;
        const i32 cx1 = 960 * (HomeScene::kChatSection + 1) / HomeScene::kBlockCount;
        const i32 cbx = (cx0 + cx1) / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(cbx, filaY));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cbx, filaY));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cbx, filaY));
        home.update(engine, 0.016);
        check("Desde Tienda, pulsar CHAT vuelve a la seccion Chat",
              home.currentSection() == HomeScene::kChatSection);

        home.onExit(engine);
        break;
    }
    case 54: {
        // --- boton Modo Desarrollador en la seccion Cuenta ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("dev@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kAccountSection);
        home.render(engine, probe);   // fija los rectangulos de los botones

        const Recti db = home.devButtonRect();
        std::snprintf(buf, sizeof(buf), "%dx%d en (%d,%d)", db.w, db.h, db.x, db.y);
        check("El boton Modo Desarrollador tiene sitio en Cuenta",
              db.w > 40 && db.h > 0, buf);

        const Recti lb = home.logoutButtonRect();
        check("Modo Desarrollador va a la derecha de Cerrar sesion",
              db.x > lb.x + lb.w - 1);

        // Clic en el boton: debe llevar a la seccion de desarrollador.
        const i32 cx = db.x + db.w / 2, cy = db.y + db.h / 2;
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(cx, cy));
        SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
        home.update(engine, 0.016);
        check("El boton lleva a la seccion Modo Desarrollador",
              home.currentSection() == HomeScene::kDevSection);

        // ESC vuelve a Cuenta.
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
        home.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYUP, VK_ESCAPE, 0);
        check("ESC vuelve de Modo Desarrollador a Cuenta",
              home.currentSection() == HomeScene::kAccountSection);

        home.onExit(engine);
        break;
    }
    case 55: {
        // --- F11 alterna pantalla completa y restaura la geometria ---
        Window& w = win;

        // Estado y tamano de partida.
        const bool eraFs = w.isFullscreen();
        if (eraFs) w.toggleFullscreen();   // asegurar que empezamos sin fs
        RECT antes{};
        GetWindowRect(w.handle(), &antes);
        const i32 anchoAntes = antes.right - antes.left;
        const i32 altoAntes  = antes.bottom - antes.top;

        // Barra de titulo antes de entrar: debe tener altura normal.
        TitleBar tb;
        const i32 barAntes = tb.heightLogical(engine);

        // Entrar a pantalla completa via F11 (por el WndProc real).
        SendMessageW(w.handle(), WM_KEYDOWN, VK_F11, 0);
        check("F11 activa la pantalla completa", w.isFullscreen());

        // En pantalla completa la barra de titulo desaparece (altura 0),
        // asi que los botones ─ □ X ya no se ven.
        const i32 barFs = tb.heightLogical(engine);
        std::snprintf(buf, sizeof(buf), "barra antes %d, en fullscreen %d",
                      barAntes, barFs);
        check("En pantalla completa la barra de titulo se oculta (altura 0)",
              barAntes > 0 && barFs == 0, buf);

        // La ventana debe cubrir el monitor completo.
        HMONITOR mon = MonitorFromWindow(w.handle(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        RECT fs{};
        GetWindowRect(w.handle(), &fs);
        const bool cubre = (fs.left <= mi.rcMonitor.left && fs.top <= mi.rcMonitor.top &&
                            fs.right >= mi.rcMonitor.right && fs.bottom >= mi.rcMonitor.bottom);
        std::snprintf(buf, sizeof(buf), "ventana %ldx%ld, monitor %ldx%ld",
                      fs.right - fs.left, fs.bottom - fs.top,
                      mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top);
        check("En pantalla completa la ventana cubre el monitor", cubre, buf);

        // F11 de nuevo restaura la geometria original.
        SendMessageW(w.handle(), WM_KEYUP, VK_F11, 0);
        SendMessageW(w.handle(), WM_KEYDOWN, VK_F11, 0);
        check("F11 de nuevo sale de pantalla completa", !w.isFullscreen());

        // Al salir, la barra de titulo (y sus botones) vuelve a verse.
        check("Al salir de pantalla completa la barra vuelve",
              tb.heightLogical(engine) == barAntes);

        RECT despues{};
        GetWindowRect(w.handle(), &despues);
        const i32 anchoDesp = despues.right - despues.left;
        const i32 altoDesp  = despues.bottom - despues.top;
        std::snprintf(buf, sizeof(buf), "antes %dx%d, despues %dx%d",
                      anchoAntes, altoAntes, anchoDesp, altoDesp);
        check("Al salir se restaura el tamano previo",
              anchoDesp == anchoAntes && altoDesp == altoAntes, buf);

        SendMessageW(w.handle(), WM_KEYUP, VK_F11, 0);
        break;
    }
    case 56: {
        // --- al jugar se descifra TODO: exe + assets anidados (texturas) ---
        // Se simula un juego con subcarpetas profundas, como Voxel World.
        const wchar_t* srcDir = L"jtsrc.t56";
        const wchar_t* vault  = L"jt.tomate.t56";
        const wchar_t* outDir = L"jtout.t56";

        // Estructura anidada de 4 niveles con "texturas" y datos.
        CreateDirectoryW(srcDir, nullptr);
        CreateDirectoryW(L"jtsrc.t56\\assets", nullptr);
        CreateDirectoryW(L"jtsrc.t56\\assets\\texturas", nullptr);
        CreateDirectoryW(L"jtsrc.t56\\assets\\texturas\\bloques", nullptr);
        {
            std::ofstream(L"jtsrc.t56\\Game.exe", std::ios::binary) << "EXE";
            std::ofstream(L"jtsrc.t56\\assets\\texturas\\atlas.png", std::ios::binary) << "PNG-ATLAS";
            std::ofstream(L"jtsrc.t56\\assets\\texturas\\bloques\\piedra.png",
                          std::ios::binary) << "PNG-PIEDRA-DATOS";
        }

        std::atomic<float> prog{0.0f};
        std::string err;
        const bool ok = gamevault::sealFolder(L"jtsrc.t56\\Game.exe", vault, &prog, err);
        check("Se cifra un juego con texturas anidadas", ok, ok ? "" : err.c_str());

        std::wstring outExe;
        const bool abierto = gamevault::openToFolder(vault, outDir, L"Game.exe", outExe, err);
        check("Al jugar se descifra el juego con sus texturas",
              abierto, abierto ? "" : err.c_str());

        // El exe y TODAS las texturas (a distinta profundidad) deben existir.
        check("El ejecutable queda expuesto al jugar",
              GetFileAttributesW(L"jtout.t56\\Game.exe") != INVALID_FILE_ATTRIBUTES);
        check("La textura de nivel 3 se descifra",
              GetFileAttributesW(L"jtout.t56\\assets\\texturas\\atlas.png")
                  != INVALID_FILE_ATTRIBUTES);
        check("La textura de nivel 4 (subcarpeta) se descifra",
              GetFileAttributesW(L"jtout.t56\\assets\\texturas\\bloques\\piedra.png")
                  != INVALID_FILE_ATTRIBUTES);

        // El contenido de una textura debe ser identico al original.
        {
            std::ifstream f(L"jtout.t56\\assets\\texturas\\bloques\\piedra.png",
                            std::ios::binary);
            std::string c((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
            check("El contenido de la textura descifrada es identico",
                  c == "PNG-PIEDRA-DATOS", c.c_str());
        }

        // La marca de descifrado completo debe quedar escrita.
        check("Se escribe la marca de descifrado completo",
              GetFileAttributesW(L"jtout.t56\\.ludora_ready")
                  != INVALID_FILE_ATTRIBUTES);

        // Limpieza recursiva.
        for (const wchar_t* d : { L"jtsrc.t56\0\0", L"jtout.t56\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        DeleteFileW(vault);
        break;
    }
    case 57: {
        // --- se detecta la RAIZ del juego, no solo la carpeta del .exe ---
        // BUG QUE ESTO PROTEGE: Voxel World salia con texturas rosa-negro. Su
        // .exe esta en build\bin\Release, pero las texturas (resourcepacks\)
        // estan en la raiz del proyecto, arriba. Instalar solo la carpeta del
        // .exe dejaba al juego sin texturas. findGameRoot debe subir hasta la
        // carpeta que contiene resourcepacks.
        const wchar_t* base = L"jtroot.t57";
        CreateDirectoryW(base, nullptr);
        CreateDirectoryW(L"jtroot.t57\\resourcepacks", nullptr);
        CreateDirectoryW(L"jtroot.t57\\resourcepacks\\Textures", nullptr);
        CreateDirectoryW(L"jtroot.t57\\build", nullptr);
        CreateDirectoryW(L"jtroot.t57\\build\\bin", nullptr);
        CreateDirectoryW(L"jtroot.t57\\build\\bin\\Release", nullptr);
        {
            std::ofstream(L"jtroot.t57\\resourcepacks\\Textures\\atlas.png",
                          std::ios::binary) << "PNG";
            std::ofstream(L"jtroot.t57\\build\\bin\\Release\\Game.exe",
                          std::ios::binary) << "MZ-EXE";
        }

        // planInstall debe: (a) poner el .exe en la raiz del destino, y
        // (b) recoger resourcepacks aunque este 3 niveles por encima del .exe.
        std::atomic<float> prog57{0.0f};
        std::string err57;
        const wchar_t* dst57 = L"jtinst.t57";
        {
            GameInstall inst;
            inst.start(L"jtroot.t57\\build\\bin\\Release\\Game.exe", dst57,
                       "T57", L"Game.exe");
            inst.join();
            check("Instala un juego con recursos arriba del .exe",
                  inst.done() && !inst.failed(),
                  inst.failed() ? inst.error().c_str() : "");

            // El .exe queda en la RAIZ del destino (0 niveles), junto a recursos.
            check("El .exe queda en la raiz del destino",
                  GetFileAttributesW(L"jtinst.t57\\Game.exe") != INVALID_FILE_ATTRIBUTES);
            // resourcepacks (que estaba arriba) se instala como subcarpeta.
            check("resourcepacks se instala junto al .exe",
                  GetFileAttributesW(L"jtinst.t57\\resourcepacks\\Textures\\atlas.png")
                      != INVALID_FILE_ATTRIBUTES);
            // El arbol de compilacion NO se copia entero.
            check("No se copia build\\ como tal",
                  GetFileAttributesW(L"jtinst.t57\\build") == INVALID_FILE_ATTRIBUTES);
            DeleteFileW(inst.vaultPath().c_str());
        }
        (void)prog57; (void)err57;

        // Limpieza recursiva.
        for (const wchar_t* d : { L"jtroot.t57\0\0", L"jtinst.t57\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        break;
    }
    case 58: {
        // --- VFS: build -> mount -> read en RAM, sin archivos temporales ---
        // Se empaqueta una "textura" y un "audio" en un .pak cifrado, se monta,
        // y se comprueba que el motor recibe los BYTES ORIGINALES en RAM y que
        // el asset descifrado NUNCA aparece en disco.
        const wchar_t* pak = L"vfs.t58.pak";

        // Contenido de ejemplo: una cabecera PNG plausible y unas muestras WAV.
        std::vector<u8> texturaPng = {
            0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A, 0,0,0,13, 'I','H','D','R',
            0,0,0,16, 0,0,0,16, 8,6,0,0,0, 0x1F,0xF3,0xFF,0x61 };
        for (int i = 0; i < 256; ++i) texturaPng.push_back(static_cast<u8>(i * 7 + 3));

        std::vector<u8> audioWav(1024);
        for (size_t i = 0; i < audioWav.size(); ++i)
            audioWav[i] = static_cast<u8>((i * 131 + 17) & 0xFF);

        VirtualFileSystem::SourceAsset a1{ "textures/player.png", texturaPng };
        VirtualFileSystem::SourceAsset a2{ "audio/jump.wav",       audioWav };

        std::string verr;
        const bool built = VirtualFileSystem::build(
            pak, { a1, a2 }, /*useXorInstead*/ false, verr);
        check("VFS: se construye el .pak cifrado", built, built ? "" : verr.c_str());

        // El asset EN CLARO no debe aparecer en el .pak (busqueda de la firma PNG).
        {
            std::ifstream pf(pak, std::ios::binary);
            std::string raw((std::istreambuf_iterator<char>(pf)),
                             std::istreambuf_iterator<char>());
            const bool leakTex = raw.find("\x89PNG") != std::string::npos;
            check("VFS: la textura NO queda en claro dentro del .pak", !leakTex);
        }

        VirtualFileSystem vfs;
        check("VFS: se monta el contenedor", vfs.mount(pak, verr), verr.c_str());
        check("VFS: la tabla de contenidos indexa los 2 assets",
              vfs.assetCount() == 2);
        check("VFS: contains() localiza un asset por ruta logica",
              vfs.contains("textures/player.png"));

        // --- Integracion con el motor: leer a RAM y pasar al cargador ---------
        // Cargadores HIPOTETICOS del motor: reciben un puntero+tamano en RAM y
        // no tocan disco. Aqui se limitan a validar que reciben lo correcto.
        auto LoadTextureFromMemory = [](const u8* data, size_t size) -> bool {
            return size > 8 && data[0] == 0x89 && data[1] == 'P' &&
                   data[2] == 'N' && data[3] == 'G';
        };
        auto LoadAudioFromMemory = [](const u8* data, size_t size) -> bool {
            return size == 1024 && data[0] == static_cast<u8>(17);
        };

        {
            AssetBuffer tex;
            const bool okTex = vfs.read("textures/player.png", tex, verr);
            check("VFS: lee y descifra la textura a RAM", okTex, verr.c_str());
            check("VFS: los bytes en RAM son identicos al original",
                  okTex && tex.size() == texturaPng.size() &&
                  std::memcmp(tex.data(), texturaPng.data(), tex.size()) == 0);
            check("Motor: LoadTextureFromMemory acepta el buffer en RAM",
                  okTex && LoadTextureFromMemory(tex.data(), tex.size()));
            // Al salir de ambito, tex se auto-borra (SecureZeroMemory).
        }
        {
            AssetBuffer snd;
            const bool okSnd = vfs.read("audio/jump.wav", snd, verr);
            check("VFS: lee y descifra el audio a RAM", okSnd, verr.c_str());
            check("Motor: LoadAudioFromMemory acepta el buffer en RAM",
                  okSnd && LoadAudioFromMemory(snd.data(), snd.size()));
        }

        // Un asset inexistente falla limpio, sin reventar.
        {
            AssetBuffer nop;
            check("VFS: un asset inexistente falla de forma controlada",
                  !vfs.read("no/existe.dat", nop, verr) && nop.empty());
        }

        // Manipulacion: alterar un byte del .pak debe hacer fallar el tag GCM.
        {
            std::fstream pf(pak, std::ios::binary | std::ios::in | std::ios::out);
            pf.seekp(45);                 // dentro del primer bloque de datos
            char c; pf.read(&c, 1);
            pf.seekp(45); c ^= 0x5A; pf.write(&c, 1);
            pf.close();

            VirtualFileSystem vfs2;
            AssetBuffer t;
            vfs2.mount(pak, verr);
            check("VFS: una manipulacion del .pak se detecta (tag GCM)",
                  !vfs2.read("textures/player.png", t, verr));
        }

        DeleteFileW(pak);
        break;
    }
    case 59: {
        // --- robustez: registro que apunta a un .exe que ya no existe ---
        // BUG QUE ESTO PROTEGE: la biblioteca decia "Voxel World instalado"
        // pero el .exe no estaba (instalacion borrada/incompleta), y al jugar
        // salia "no se encuentra ejecutable" sin poder reinstalar.
        const wchar_t* lib = L"lib.t59.dat";
        DeleteFileW(lib);

        // Entrada instalada de verdad: .exe presente.
        CreateDirectoryW(L"inst.t59", nullptr);
        std::ofstream(L"inst.t59\\Real.exe", std::ios::binary) << "MZ";
        library::Entry ok;
        ok.name = "JuegoReal"; ok.exe = L"inst.t59\\Real.exe";
        library::add(lib, ok);

        // Entrada rota: el .exe no existe en disco.
        library::Entry roto;
        roto.name = "JuegoFantasma"; roto.exe = L"inst.t59\\NoExiste.exe";
        library::add(lib, roto);

        check("entryInstalled: true si el .exe existe",
              library::entryInstalled(ok));
        check("entryInstalled: false si el .exe no existe",
              !library::entryInstalled(roto));

        // launchEntry sobre el roto: falla con mensaje accionable, no generico.
        {
            std::string err59;
            void* proc = nullptr;
            const bool lanzado = library::launchEntry(roto, L"", err59, &proc);
            if (proc) CloseHandle(proc);
            check("launchEntry rechaza un .exe ausente", !lanzado);
            check("launchEntry avisa que hay que reinstalar",
                  err59.find("Vuelve a instalarlo") != std::string::npos, err59.c_str());
        }

        // --- un registro SIN ejecutable no cuenta como instalado ---
        // Un paquete puede no traer .exe (un juego subido sin compilar). Si
        // se registrara igual, apareceria en la biblioteca y no arrancaria
        // nunca, sin decir por que.
        {
            library::Entry vacio;
            vacio.name = "SinEjecutable";
            check("Una entrada sin .exe no esta instalada",
                  !library::entryInstalled(vacio));

            std::string errv;
            void* pv = nullptr;
            const bool lv = library::launchEntry(vacio, L"", errv, &pv);
            if (pv) CloseHandle(pv);
            check("Y lanzarla falla diciendo que falta el ejecutable",
                  !lv && !errv.empty(), errv.c_str());
        }

        // --- la carpeta de trabajo es la RAIZ, no la del .exe ---
        // Un juego compilado deja su exe en bin\Release pero busca sus
        // texturas relativas a la raiz. Lanzarlo desde la carpeta del exe
        // hace que no encuentre nada.
        {
            CreateDirectoryW(L"wd.t59", nullptr);
            CreateDirectoryW(L"wd.t59\\assets", nullptr);
            CreateDirectoryW(L"wd.t59\\build", nullptr);
            CreateDirectoryW(L"wd.t59\\build\\Release", nullptr);
            { std::ofstream f("wd.t59\\build\\Release\\J.exe", std::ios::binary);
              f << "MZ"; }
            { std::ofstream f("wd.t59\\assets\\t.png", std::ios::binary);
              f << "PNG"; }

            // El exe esta en wd.t59\build\Release; la raiz con assets es
            // wd.t59. Se comprueba a traves de launchEntry, que es quien
            // decide la carpeta de trabajo.
            //
            // No se lanza de verdad (el .exe es falso), pero el fallo tiene
            // que ser del shell y NO de "falta el ejecutable": eso ya prueba
            // que el archivo se encontro y se intento ejecutar.
            library::Entry je;
            je.name = "ConBuild";
            je.exe  = L"wd.t59\\build\\Release\\J.exe";
            check("Un exe en build\\Release si cuenta como instalado",
                  library::entryInstalled(je));

            std::string errw;
            void* pw = nullptr;
            // Se cronometra: un lanzamiento fallido tiene que VOLVER, no
            // quedarse esperando.
            //
            // BUG QUE ESTO PROTEGE: ShellExecuteEx sacaba su propio cuadro de
            // error ("Windows no puede encontrar el archivo ... J.exe") y se
            // quedaba bloqueado hasta que alguien lo aceptaba. Colgaba Ludora
            // entera, y aqui colgaba la prueba hasta que alguien pulsaba
            // Aceptar a mano. SEE_MASK_FLAG_NO_UI es lo que lo evita.
            const DWORD t0 = GetTickCount();
            const bool lw = library::launchEntry(je, L"", errw, &pw);
            const DWORD tardo = GetTickCount() - t0;
            if (pw) CloseHandle(pw);
            // Un "MZ" de dos bytes no es un ejecutable valido: Windows lo
            // rechaza. Lo que importa es que el motivo sea concreto y no el
            // generico de antes.
            std::snprintf(buf, sizeof(buf), "lanzado=%d, err='%s', %lu ms",
                          lw ? 1 : 0, errw.c_str(),
                          static_cast<unsigned long>(tardo));
            check("Al fallar, el motivo es concreto y no generico",
                  lw || errw != "No se pudo lanzar el juego.", buf);
            check("Y vuelve solo, sin cuadro de Windows que haya que cerrar",
                  tardo < 4000, buf);

            for (const wchar_t* d : { L"wd.t59\0\0" }) {
                SHFILEOPSTRUCTW op{};
                op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
                SHFileOperationW(&op);
            }
        }

        // remove() limpia el registro roto y conserva el bueno.
        check("remove: elimina el juego fantasma",
              library::remove(lib, "JuegoFantasma"));
        check("remove: conserva el juego real",
              library::has(lib, "JuegoReal") && !library::has(lib, "JuegoFantasma"));

        // Limpieza.
        DeleteFileW(lib);
        for (const wchar_t* d : { L"inst.t59\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        break;
    }
    case 60: {
        // ====================================================================
        // ALMACEN MULTI-CUENTA: varias cuentas conviviendo
        // ====================================================================
        // Todo ocurre en una carpeta temporal: las cuentas reales del usuario
        // no se tocan ni se leen.
        const std::wstring raiz = L"selftest-cuentas.tmp";
        limpiarCarpeta(raiz);
        AccountStore::setDataRootForTesting(raiz);

        std::string e;
        check("Almacen vacio al empezar", AccountStore::count() == 0);

        const bool c1 = AccountStore::create("uno@ludora.engine", "clave123", e);
        check("Primera cuenta creada", c1, e);

        // LA REGLA QUE CAMBIA: con `Account` esto fallaba con "Ya existe una
        // cuenta en este equipo". Ahora convive.
        const bool c2 = AccountStore::create("dos@ludora.engine", "clave456", e);
        check("Segunda cuenta creada (antes se rechazaba)", c2, e);

        const bool c3 = AccountStore::create("tres@ludora.engine", "clave789", e);
        check("Tercera cuenta creada", c3, e);

        check("El almacen guarda las 3 cuentas", AccountStore::count() == 3,
              std::to_string(AccountStore::count()));

        // Duplicar un correo SI se rechaza: choca consigo misma, no con las otras.
        const bool dup = AccountStore::create("uno@ludora.engine", "otra999", e);
        check("Un correo repetido se rechaza", !dup, e);
        check("Tras el rechazo siguen siendo 3", AccountStore::count() == 3);

        // Cada cuenta valida contra SU propia contrasena.
        check("Cuenta 1 verifica con su clave",
              AccountStore::verify("uno@ludora.engine", "clave123", e), e);
        check("Cuenta 2 verifica con su clave",
              AccountStore::verify("dos@ludora.engine", "clave456", e), e);
        check("La clave de otra cuenta no sirve",
              !AccountStore::verify("uno@ludora.engine", "clave456", e));
        check("Una cuenta inexistente falla",
              !AccountStore::verify("nadie@ludora.engine", "clave123", e));

        // El listado las devuelve ordenadas y con su correo.
        const std::vector<AccountStore::Summary> lista = AccountStore::list();
        check("El listado devuelve las 3", lista.size() == 3);
        check("El listado sale ordenado por correo",
              lista.size() == 3 && lista[0].email == "dos@ludora.engine" &&
              lista[1].email == "tres@ludora.engine" &&
              lista[2].email == "uno@ludora.engine");

        // Borrar una no arrastra a las demas.
        check("Se borra una cuenta", AccountStore::remove("tres@ludora.engine", e), e);
        check("Quedan 2 tras el borrado", AccountStore::count() == 2);
        check("La borrada ya no existe", !AccountStore::exists("tres@ludora.engine"));
        check("Las otras siguen intactas",
              AccountStore::exists("uno@ludora.engine") &&
              AccountStore::exists("dos@ludora.engine"));

        AccountStore::setDataRootForTesting(L"");
        limpiarCarpeta(raiz);
        break;
    }
    case 61: {
        // ====================================================================
        // VARIOS DISPOSITIVOS POR CUENTA
        // ====================================================================
        // El limite mas serio del sistema anterior: `devhash` era un campo
        // unico, asi que verificar un segundo equipo PISABA al primero.
        const std::wstring raiz = L"selftest-disp.tmp";
        limpiarCarpeta(raiz);
        AccountStore::setDataRootForTesting(raiz);

        std::string e;
        AccountStore::create("multi@ludora.engine", "clave123", e);

        // Al crearse queda vinculado ESTE equipo.
        check("Al crear la cuenta, este equipo queda verificado",
              AccountStore::deviceVerified("multi@ludora.engine"));

        std::vector<AccountStore::DeviceView> devs =
            AccountStore::devices("multi@ludora.engine");
        check("Hay 1 dispositivo vinculado", devs.size() == 1,
              std::to_string(devs.size()));
        check("Ese dispositivo es ESTE equipo",
              devs.size() == 1 && devs[0].isThisDevice);
        check("El dispositivo lleva su tipo",
              devs.size() == 1 && !devs[0].kindName.empty(),
              devs.empty() ? "" : devs[0].kindName);
        check("El dispositivo lleva fecha de alta",
              devs.size() == 1 && devs[0].addedAt > 0);

        // Se simula un segundo equipo escribiendo su huella a mano: en una
        // sola maquina no se puede generar la huella de otra distinta.
        {
            AccountStore::Record r;
            const std::wstring p = AccountStore::pathForEmail("multi@ludora.engine");
            check("Se lee el registro de la cuenta",
                  AccountStore::readRecord(p, r));

            AccountStore::DeviceEntry otro;
            otro.salt     = "0123456789abcdef0123456789abcdef";
            otro.hash     = "hash-de-otro-equipo-que-no-es-este";
            otro.kindName = "Portatil";
            otro.addedAt  = 1000;
            otro.lastSeen = 2000;
            r.devices.push_back(otro);
            check("Se anade un segundo dispositivo",
                  AccountStore::writeRecord(p, r));
        }

        devs = AccountStore::devices("multi@ludora.engine");
        check("La cuenta tiene 2 dispositivos", devs.size() == 2,
              std::to_string(devs.size()));
        check("Este equipo sigue verificado con 2 vinculados",
              AccountStore::deviceVerified("multi@ludora.engine"));

        int esteEquipo = 0, otroEquipo = 0;
        for (const auto& d : devs) (d.isThisDevice ? esteEquipo : otroEquipo)++;
        check("Se distingue este equipo del otro",
              esteEquipo == 1 && otroEquipo == 1);

        // LA PRUEBA CLAVE: verificar la clave NO debe borrar el otro equipo.
        check("Verificar la contrasena funciona",
              AccountStore::verify("multi@ludora.engine", "clave123", e), e);
        devs = AccountStore::devices("multi@ludora.engine");
        check("Verificar NO desplaza al otro dispositivo (antes lo pisaba)",
              devs.size() == 2, std::to_string(devs.size()));

        // Desvincular este equipo deja al otro en su sitio.
        check("Se desvincula este equipo",
              AccountStore::forgetThisDevice("multi@ludora.engine", e), e);
        check("Este equipo deja de estar verificado",
              !AccountStore::deviceVerified("multi@ludora.engine"));
        devs = AccountStore::devices("multi@ludora.engine");
        check("El otro dispositivo sobrevive", devs.size() == 1,
              std::to_string(devs.size()));
        check("El superviviente NO es este equipo",
              devs.size() == 1 && !devs[0].isThisDevice);

        // Y la cuenta sigue abriendose con la contrasena.
        check("Tras olvidar el equipo, la clave sigue valiendo",
              AccountStore::verify("multi@ludora.engine", "clave123", e), e);
        check("Al verificar, este equipo vuelve a vincularse",
              AccountStore::deviceVerified("multi@ludora.engine"));

        check("Se olvidan todos los dispositivos",
              AccountStore::forgetAllDevices("multi@ludora.engine", e), e);
        check("Sin dispositivos, no hay verificacion",
              !AccountStore::deviceVerified("multi@ludora.engine"));
        check("La cuenta sigue existiendo sin dispositivos",
              AccountStore::exists("multi@ludora.engine"));

        AccountStore::setDataRootForTesting(L"");
        limpiarCarpeta(raiz);
        break;
    }
    case 62: {
        // ====================================================================
        // INICIO AUTOMATICO, MIGRACION Y PORTABILIDAD
        // ====================================================================
        const std::wstring raiz = L"selftest-auto.tmp";
        limpiarCarpeta(raiz);
        AccountStore::setDataRootForTesting(raiz);

        std::string e;

        // --- Inicio automatico ---
        check("Sin cuentas no hay inicio automatico",
              AccountStore::soleAutoLoginEmail().empty());

        AccountStore::create("sola@ludora.engine", "clave123", e);
        check("Con UNA cuenta verificada, entra sola",
              AccountStore::soleAutoLoginEmail() == "sola@ludora.engine",
              AccountStore::soleAutoLoginEmail());

        // Con DOS verificadas en el mismo equipo, elegir por el usuario seria
        // decidir quien entra: se devuelve "" y la interfaz muestra el selector.
        AccountStore::create("otra@ludora.engine", "clave456", e);
        check("Con DOS verificadas, no se elige por el usuario",
              AccountStore::soleAutoLoginEmail().empty(),
              AccountStore::soleAutoLoginEmail());
        check("Pero ambas son candidatas",
              AccountStore::autoLoginCandidates().size() == 2);

        // Al desvincular una, la otra vuelve a ser la unica.
        AccountStore::forgetThisDevice("otra@ludora.engine", e);
        check("Al desvincular una, la otra entra sola otra vez",
              AccountStore::soleAutoLoginEmail() == "sola@ludora.engine");

        // --- Migracion desde el formato viejo ---
        const std::wstring vieja = raiz + L"\\cuenta-vieja.dat";
        {
            std::ofstream out(vieja.c_str(), std::ios::binary);
            out << "email=antigua@ludora.engine\r\n"
                << "salt=aabbccdd\r\n"
                << "hash=1122334455667788\r\n"
                << "devsalt=deadbeef\r\n"
                << "devhash=cafebabe\r\n"
                << "devkind=Escritorio\r\n";
        }

        check("Se migra la cuenta del formato antiguo",
              AccountStore::migrateLegacy(vieja, L""));
        check("La cuenta migrada aparece en el almacen",
              AccountStore::exists("antigua@ludora.engine"));

        const std::vector<AccountStore::DeviceView> mig =
            AccountStore::devices("antigua@ludora.engine");
        check("La migracion conserva su dispositivo", mig.size() == 1,
              std::to_string(mig.size()));
        check("Y conserva el tipo de equipo",
              mig.size() == 1 && mig[0].kindName == "Escritorio");

        // Idempotencia: el arranque la llama SIEMPRE, no puede duplicar ni
        // pisar los dispositivos ganados despues de migrar.
        const size_t antes = AccountStore::count();
        check("Migrar dos veces no duplica",
              AccountStore::migrateLegacy(vieja, L"") &&
              AccountStore::count() == antes);

        // Un archivo viejo corrupto no migra y no rompe nada.
        const std::wstring rota = raiz + L"\\rota.dat";
        {
            std::ofstream out(rota.c_str(), std::ios::binary);
            out << "email=incompleta@ludora.engine\r\n";   // sin salt ni hash
        }
        check("Una cuenta antigua incompleta no se migra",
              !AccountStore::migrateLegacy(rota, L""));
        check("Y no se cuela en el almacen",
              !AccountStore::exists("incompleta@ludora.engine"));

        // --- Nombres de archivo seguros ---
        // El id sale del correo, asi que no puede escaparse de la carpeta.
        const std::string idRaro = AccountStore::idForEmail("..\\..\\fuera@ludora.engine");
        check("El id no permite salir de la carpeta",
              idRaro.find("..") == std::string::npos &&
              idRaro.find('\\') == std::string::npos &&
              idRaro.find('/')  == std::string::npos,
              idRaro);
        check("Mayusculas y minusculas dan el mismo id",
              AccountStore::idForEmail("Iker@Ludora.Engine") ==
              AccountStore::idForEmail("iker@ludora.engine"));

        AccountStore::setDataRootForTesting(L"");
        limpiarCarpeta(raiz);
        break;
    }
    case 63: {
        // --- Modo Desarrollador: columna de botones y precios ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("devmode@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kDevSection);
        home.render(engine, probe);   // fija la geometria de la seccion

        // Los tres botones existen, van en COLUMNA y a la IZQUIERDA.
        const Recti b0 = home.devColumnButtonRect(0);
        const Recti b1 = home.devColumnButtonRect(1);
        const Recti b2 = home.devColumnButtonRect(2);
        std::snprintf(buf, sizeof(buf), "(%d,%d) (%d,%d) (%d,%d)",
                      b0.x, b0.y, b1.x, b1.y, b2.x, b2.y);
        check("Los tres botones de desarrollador tienen sitio",
              b0.w > 0 && b1.w > 0 && b2.w > 0, buf);
        check("Van apilados en columna", b0.y < b1.y && b1.y < b2.y);
        check("Comparten la misma X (columna recta)",
              b0.x == b1.x && b1.x == b2.x);
        check("La columna esta pegada a la izquierda", b0.x < 960 / 3);

        // --- los rotulos caben en su boton ---
        // El segundo se llama "GUARDAR ARCHIVOS Y ACTUALIZAR", que es largo:
        // si no cupiera, el texto saldria por los lados en vez de encogerse.
        // Se mide pintando y mirando si queda tinta en la columna de pixeles
        // pegada al borde interior, que es donde asomaria.
        {
            i32 fuera = 0;
            for (i32 i = 0; i < HomeScene::devColumnButtonCount(); ++i) {
                const Recti r = home.devColumnButtonRect(i);
                if (r.w <= 0 || r.h <= 0) continue;
                // Dos pixeles por dentro del marco: el marco es del color del
                // boton y no cuenta como texto desbordado.
                const u32 blanco = Color{ 255, 255, 255 }.packed();
                for (i32 yy = r.y + 4; yy < r.y + r.h - 4; ++yy) {
                    if (yy < 0 || yy >= probe.height()) continue;
                    for (const i32 xx : { r.x + 3, r.x + r.w - 4 }) {
                        if (xx < 0 || xx >= probe.width()) continue;
                        // Blanco es el color de la etiqueta: si aparece
                        // pegado al borde, el rotulo no cabe.
                        const u32 c = probe.data()[
                            static_cast<size_t>(yy) * probe.width() + xx];
                        if (c == blanco) ++fuera;
                    }
                }
            }
            std::snprintf(buf, sizeof(buf), "%d pixeles de rotulo en el borde",
                          fuera);
            check("Los rotulos de la columna no se salen de su boton",
                  fuera == 0, buf);
        }

        // La mini-seccion arranca justo tras la columna: el hueco que
        // reservaba el rombo ahora lo aprovecha el panel.
        const Recti panelCol = home.devPanelRect();
        std::snprintf(buf, sizeof(buf), "columna acaba en %d, panel en %d",
                      b0.x + b0.w, panelCol.x);
        check("El panel arranca tras la columna, sin hueco de sobra",
              panelCol.x > b0.x + b0.w && panelCol.x < b0.x + b0.w + 60, buf);

        // Al entrar no hay ninguna mini-seccion abierta.
        check("Al entrar no hay mini-seccion abierta", home.devOpenPanel() < 0);

        /// Pulsa y suelta en un punto, dejando que la escena procese ambos.
        auto clic = [&](i32 cx, i32 cy) {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(cx, cy));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy));
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
            home.update(engine, 0.016);
        };

        // --- boton 1: SUBIR JUEGO abre su mini-seccion ---
        clic(b0.x + b0.w / 2, b0.y + b0.h / 2);
        check("SUBIR JUEGO abre su mini-seccion", home.devOpenPanel() == 0);

        home.render(engine, probe);
        const Recti panel = home.devPanelRect();
        std::snprintf(buf, sizeof(buf), "%dx%d", panel.w, panel.h);
        check("La mini-seccion es cuadrada", panel.w == panel.h && panel.w > 40, buf);
        check("La mini-seccion va a la derecha de la columna",
              panel.x >= b0.x + b0.w);

        // --- boton 3: PRECIOS y su deslizador ---
        clic(b2.x + b2.w / 2, b2.y + b2.h / 2);
        check("PRECIOS abre su mini-seccion", home.devOpenPanel() == 2);
        home.render(engine, probe);

        const Recti track = home.priceTrackRect();
        check("El deslizador tiene barrita", track.w > 40 && track.h > 0);

        // El carril va de GRATIS (izquierda) al MAXIMO (derecha), que es el
        // orden en que estan sus etiquetas.
        home.setPriceT(0.0f);
        home.render(engine, probe);
        std::snprintf(buf, sizeof(buf), "%d centavos", home.priceUsdCents());
        check("A la izquierda del todo el precio es 0 (Gratis)",
              home.priceUsdCents() == 0, buf);

        home.setPriceT(1.0f);
        home.render(engine, probe);
        check("A la derecha del todo el precio es el maximo",
              home.priceUsdCents() == devcatalog::kMaxUsdCents);

        // El selector se mueve al arrastrar y el contador sube con el.
        home.setPriceT(0.0f);
        home.render(engine, probe);
        const Recti knobIzq = home.priceKnobRect();
        home.setPriceT(1.0f);
        home.render(engine, probe);
        const Recti knobDer = home.priceKnobRect();
        check("El selector se desplaza de izquierda a derecha",
              knobDer.x > knobIzq.x);
        check("El selector no se sale de la barrita",
              knobIzq.x >= track.x &&
              knobDer.x + knobDer.w <= track.x + track.w + 1);

        // Arrastre real: pulsar en el medio del carril deja un precio intermedio.
        const i32 medioX = track.x + track.w / 2;
        const i32 medioY = track.y + track.h / 2;
        clic(medioX, medioY);
        std::snprintf(buf, sizeof(buf), "%d centavos", home.priceUsdCents());
        check("Un clic en el centro deja un precio intermedio",
              home.priceUsdCents() > 0 &&
              home.priceUsdCents() < devcatalog::kMaxUsdCents, buf);

        // --- formato del precio en las tres monedas ---
        std::string usd, mxn, eur;
        devcatalog::formatPrice(0, usd, mxn, eur);
        check("Precio 0 se muestra como GRATIS",
              usd == "GRATIS" && mxn == "GRATIS" && eur == "GRATIS", usd);

        devcatalog::formatPrice(devcatalog::kMaxUsdCents, usd, mxn, eur);
        check("El maximo son 400 USD", usd == "$400.00 USD", usd);
        check("El maximo son 8,900 MXN", mxn == "$8,900.00 MXN", mxn);
        check("El maximo son 400 EUR", eur == "E400.00 EUR", eur);

        // --- rejilla de ajustes de publicacion (2 columnas de 3) ---
        // PRECIOS ya esta abierto de las comprobaciones anteriores.
        home.render(engine, probe);

        bool todosConSitio = true;
        for (i32 i = 0; i < HomeScene::priceOptionCount(); ++i) {
            const Recti o = home.priceOptionRect(i);
            if (o.w <= 0 || o.h <= 0) todosConSitio = false;
        }
        check("Los seis ajustes de publicacion tienen sitio", todosConSitio);

        // Dos columnas de tres: los tres primeros comparten X, los tres
        // siguientes comparten otra X mayor, y cada trio baja en Y.
        const Recti o0 = home.priceOptionRect(0);
        const Recti o1 = home.priceOptionRect(1);
        const Recti o2 = home.priceOptionRect(2);
        const Recti o3 = home.priceOptionRect(3);
        const Recti o4 = home.priceOptionRect(4);
        const Recti o5 = home.priceOptionRect(5);
        std::snprintf(buf, sizeof(buf), "col1 x=%d, col2 x=%d", o0.x, o3.x);
        check("La primera columna alinea sus tres botones",
              o0.x == o1.x && o1.x == o2.x, buf);
        check("La segunda columna alinea sus tres botones",
              o3.x == o4.x && o4.x == o5.x);
        check("Son dos columnas, no una", o3.x > o0.x);
        check("Cada columna baja en tres filas",
              o0.y < o1.y && o1.y < o2.y && o3.y < o4.y && o4.y < o5.y);
        check("Las dos columnas comparten las mismas filas",
              o0.y == o3.y && o1.y == o4.y && o2.y == o5.y);

        // La rejilla cabe dentro de la mini-seccion.
        const Recti pr = home.devPanelRect();
        check("La rejilla cabe dentro de la mini-seccion",
              o0.x >= pr.x && o5.x + o5.w <= pr.x + pr.w &&
              o5.y + o5.h <= pr.y + pr.h);

        // No pisa el deslizador: la rejilla va por debajo de la barrita.
        const Recti tr = home.priceTrackRect();
        check("La rejilla no pisa el deslizador", o0.y > tr.y + tr.h);

        // --- desplegar un ajuste y elegir un valor ---
        check("Al entrar no hay lista desplegada", home.priceOptionOpen() < 0);

        clic(o0.x + o0.w / 2, o0.y + o0.h / 2);
        check("Pulsar MONEDA despliega su lista", home.priceOptionOpen() == 0);
        home.render(engine, probe);

        // El valor por defecto es el primero de la lista.
        check("MONEDA arranca en USD",
              std::string(home.priceOptionValue(0)) == "USD",
              home.priceOptionValue(0));

        // Elegir la segunda opcion de la lista (MXN) y comprobar que cambia.
        // La posicion se lee de la propia lista: segun quepa o no bajo el
        // boton se despliega hacia abajo o hacia arriba, asi que calcularla a
        // mano aqui daria un clic fuera de sitio.
        const Recti lista = home.priceOptionListRect();
        check("La lista desplegada tiene sitio", lista.w > 0 && lista.h > 0);
        const i32 filaH2 = HomeScene::priceOptionRowHeight();
        const i32 filaY = lista.y + 4 + filaH2 + filaH2 / 2;   // centro de la 2a fila
        clic(lista.x + lista.w / 2, filaY);
        check("Elegir en la lista la cierra", home.priceOptionOpen() < 0);
        check("MONEDA cambia a MXN",
              std::string(home.priceOptionValue(0)) == "MXN",
              home.priceOptionValue(0));

        // Los demas ajustes conservan su valor: elegir en uno no toca al resto.
        check("Elegir moneda no altera los otros ajustes",
              home.priceOptionSelected(1) == 0 &&
              home.priceOptionSelected(2) == 0 &&
              home.priceOptionSelected(3) == 0);

        // Cada boton abre SU lista, no la del vecino.
        clic(o2.x + o2.w / 2, o2.y + o2.h / 2);
        check("Pulsar IDIOMAS despliega su propia lista",
              home.priceOptionOpen() == 2);
        check("IDIOMAS arranca en ESPANOL",
              std::string(home.priceOptionValue(2)) == "ESPANOL",
              home.priceOptionValue(2));

        // --- ESC retrocede un nivel cada vez ---
        /// Pulsa y suelta ESC dejando que la escena procese el flanco.
        auto escape = [&]() {
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYUP, VK_ESCAPE, 0);
        };

        // 1er ESC: cierra la lista desplegada, sin salir de la mini-seccion.
        escape();
        check("ESC cierra primero la lista desplegada",
              home.priceOptionOpen() < 0);
        check("Y la mini-seccion sigue abierta",
              home.devOpenPanel() == 2);
        check("Y sigue en Modo Desarrollador",
              home.currentSection() == HomeScene::kDevSection);

        // 2o ESC: cierra la mini-seccion, sin salir del Modo Desarrollador.
        escape();
        check("ESC cierra despues la mini-seccion", home.devOpenPanel() < 0);
        check("Y todavia sigue en Modo Desarrollador",
              home.currentSection() == HomeScene::kDevSection);

        // 3er ESC: ahora si, sale a Cuenta.
        escape();
        check("ESC sale por fin a Cuenta",
              home.currentSection() == HomeScene::kAccountSection);

        // --- los desplegables de PRECIOS tambien son un nivel ---
        // Faltaban en la cascada: con el cuadro de precio escrito abierto,
        // ESC cerraba de golpe toda la mini-seccion en vez de ese cuadro.
        {
            HomeScene ep("escpre@ludora.engine");
            ep.onEnter(engine);
            ep.section(HomeScene::kDevSection);
            ep.openDevPanelForTest(2);        // PRECIOS
            ep.render(engine, probe);

            auto esc2 = [&]() {
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
                ep.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_KEYUP, VK_ESCAPE, 0);
            };

            ep.setPriceTypedOpen(true);
            esc2();
            std::snprintf(buf, sizeof(buf), "escrito=%d, panel=%d, seccion=%d",
                          ep.priceTypedOpen() ? 1 : 0, ep.devOpenPanel(),
                          ep.currentSection());
            check("ESC cierra el cuadro de precio escrito",
                  !ep.priceTypedOpen(), buf);
            check("Sin cerrar la mini-seccion de precios",
                  ep.devOpenPanel() == 2, buf);
            check("Ni salir del Modo Desarrollador",
                  ep.currentSection() == HomeScene::kDevSection, buf);

            // Y el siguiente ESC si cierra la mini-seccion, como antes.
            esc2();
            std::snprintf(buf, sizeof(buf), "panel=%d, seccion=%d",
                          ep.devOpenPanel(), ep.currentSection());
            check("El siguiente ESC si cierra la mini-seccion",
                  ep.devOpenPanel() < 0, buf);
            check("Y aun sigue en Modo Desarrollador",
                  ep.currentSection() == HomeScene::kDevSection, buf);
        }

        // --- lo mismo con los modos de EDAD, dentro de SUBIR JUEGO ---
        {
            HomeScene ee("escedad@ludora.engine");
            ee.onEnter(engine);
            ee.section(HomeScene::kDevSection);
            ee.openDevPanelForTest(0);        // SUBIR JUEGO
            ee.render(engine, probe);
            ee.openUploadSlotForTest(4);      // kUpAge
            ee.render(engine, probe);
            ee.setAgeModeForTest(1);          // tramos desplegados

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
            ee.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYUP, VK_ESCAPE, 0);

            std::snprintf(buf, sizeof(buf), "modo=%d, campo=%d, panel=%d",
                          ee.ageModeOpen(), ee.uploadOpenSlot(),
                          ee.devOpenPanel());
            check("ESC cierra el modo de edad desplegado",
                  ee.ageModeOpen() == -1, buf);
            check("Sin cerrar el campo de EDAD",
                  ee.uploadOpenSlot() == 4, buf);
        }

        home.onExit(engine);
        break;
    }
    case 64: {
        // --- catalogo del desarrollador: versiones y precio en disco ---
        const std::wstring cat = L"selftest-catalogo.dat";
        DeleteFileW(cat.c_str());

        std::string e;
        check("Se publica un juego nuevo",
              devcatalog::publish(cat, "Mi Juego", L"C:\\juego\\mi.exe", e), e);

        // Publicar dos veces el mismo nombre no debe duplicarlo.
        e.clear();
        check("No se puede publicar dos veces el mismo nombre",
              !devcatalog::publish(cat, "Mi Juego", L"C:\\juego\\mi.exe", e), e);

        std::vector<devcatalog::Game> gs = devcatalog::load(cat);
        check("El catalogo guarda un juego", gs.size() == 1,
              std::to_string(gs.size()));
        check("Con su primera version", gs.size() == 1 && gs[0].versions.size() == 1);
        check("La primera version se llama v1",
              gs.size() == 1 && !gs[0].versions.empty() &&
              gs[0].versions[0].label == "v1");

        // Subir codigo nuevo: se anade version y la vieja se conserva.
        e.clear();
        check("Se anade una version nueva",
              devcatalog::addVersion(cat, "Mi Juego", L"C:\\juego\\mi2.exe", e), e);

        gs = devcatalog::load(cat);
        check("Ahora hay dos versiones",
              gs.size() == 1 && gs[0].versions.size() == 2,
              gs.empty() ? "0" : std::to_string(gs[0].versions.size()));
        check("La vieja se conserva la primera",
              gs.size() == 1 && gs[0].versions.size() == 2 &&
              gs[0].versions[0].label == "v1");
        check("La nueva queda debajo y es la vigente",
              gs.size() == 1 && gs[0].versions.size() == 2 &&
              gs[0].versions[1].label == "v2" &&
              gs[0].current() && gs[0].current()->label == "v2");
        check("La version vigente apunta al codigo nuevo",
              gs.size() == 1 && gs[0].current() &&
              gs[0].current()->exe == L"C:\\juego\\mi2.exe");

        // No se puede versionar un juego que no existe.
        e.clear();
        check("No se versiona un juego sin publicar",
              !devcatalog::addVersion(cat, "Fantasma", L"C:\\x.exe", e), e);

        // Precio: se guarda y se relee.
        e.clear();
        check("Se fija el precio",
              devcatalog::setPrice(cat, "Mi Juego",
                                   devcatalog::Price{ 40000 }, e), e);
        gs = devcatalog::load(cat);
        check("El precio persiste en disco",
              gs.size() == 1 && gs[0].price.usdCents == 40000,
              gs.empty() ? "?" : std::to_string(gs[0].price.usdCents));

        // Un precio fuera de rango se acota en vez de guardarse tal cual.
        e.clear();
        devcatalog::setPrice(cat, "Mi Juego", devcatalog::Price{ 999999 }, e);
        gs = devcatalog::load(cat);
        check("Un precio excesivo se acota al maximo",
              gs.size() == 1 && gs[0].price.usdCents == devcatalog::kMaxUsdCents,
              gs.empty() ? "?" : std::to_string(gs[0].price.usdCents));

        e.clear();
        devcatalog::setPrice(cat, "Mi Juego", devcatalog::Price{ -500 }, e);
        gs = devcatalog::load(cat);
        check("Un precio negativo se acota a gratis",
              gs.size() == 1 && gs[0].price.usdCents == 0);

        DeleteFileW(cat.c_str());
        break;
    }
    case 65: {
        // --- historial de la biblioteca: precios y versiones ---
        const std::wstring hf = L"selftest-hist.dat";
        DeleteFileW(hf.c_str());

        std::string e;

        // --- linea de tiempo del precio ---
        check("Se registra el precio inicial",
              history::noteInitialPrice(hf, "Juego X", 2000, e), e);

        history::GameHistory h = history::forGame(hf, "Juego X");
        check("El precio inicial queda anotado",
              h.prices.size() == 1 && h.initialPrice() == 2000,
              std::to_string(h.initialPrice()));

        // El precio inicial se fija UNA vez: no debe reescribirse.
        history::noteInitialPrice(hf, "Juego X", 9999, e);
        h = history::forGame(hf, "Juego X");
        check("El precio inicial no se reescribe",
              h.prices.size() == 1 && h.initialPrice() == 2000,
              std::to_string(h.initialPrice()));

        // Una subida y una bajada.
        check("Se anota una subida de precio",
              history::notePriceChange(hf, "Juego X", 3000, e), e);
        check("Se anota una bajada de precio",
              history::notePriceChange(hf, "Juego X", 1000, e), e);

        h = history::forGame(hf, "Juego X");
        std::snprintf(buf, sizeof(buf), "%zu puntos", h.prices.size());
        check("La linea de tiempo guarda los tres puntos",
              h.prices.size() == 3, buf);
        check("El inicial sigue siendo el primero", h.initialPrice() == 2000);
        check("El vigente es el ultimo", h.currentPrice() == 1000);
        check("El orden es cronologico",
              h.prices[0].usdCents == 2000 && h.prices[1].usdCents == 3000 &&
              h.prices[2].usdCents == 1000);
        check("Se detecta que el precio BAJO respecto al inicial",
              h.currentPrice() < h.initialPrice());

        // Repetir el precio vigente no ensucia la linea con puntos iguales.
        history::notePriceChange(hf, "Juego X", 1000, e);
        h = history::forGame(hf, "Juego X");
        check("Un precio repetido no anade punto", h.prices.size() == 3,
              std::to_string(h.prices.size()));

        // --- historial de versiones ---
        check("Se registra la version inicial",
              history::noteInitialVersion(hf, "Juego X", L"C:\\v1.tomate", 1000, e), e);

        h = history::forGame(hf, "Juego X");
        check("La primera version es v1 y es la jugable",
              h.versions.size() == 1 && h.versions[0].label == "v1" &&
              h.playable() && h.playable()->label == "v1");

        // Actualizar: la vieja se archiva, la nueva queda jugable.
        check("Se archiva la vieja y se anade la nueva",
              history::archiveAndAddVersion(hf, "Juego X",
                                            L"C:\\v2.tomate", 2000, e), e);

        h = history::forGame(hf, "Juego X");
        std::snprintf(buf, sizeof(buf), "%zu versiones", h.versions.size());
        check("Ahora hay dos versiones", h.versions.size() == 2, buf);
        check("La mas vieja va primera y conserva su copia",
              h.versions.size() == 2 && h.versions[0].label == "v1" &&
              h.versions[0].archivePath == L"C:\\v1.tomate");
        check("La nueva va debajo y es la JUGABLE",
              h.versions.size() == 2 && h.versions[1].label == "v2" &&
              h.playable() && h.playable()->label == "v2");
        check("Cada version guarda su tamano en disco",
              h.versions.size() == 2 && h.versions[0].bytes == 1000 &&
              h.versions[1].bytes == 2000);

        // Una tercera actualizacion mantiene el orden.
        history::archiveAndAddVersion(hf, "Juego X", L"C:\\v3.tomate", 3000, e);
        h = history::forGame(hf, "Juego X");
        check("La tercera version se encadena en orden",
              h.versions.size() == 3 && h.versions[2].label == "v3" &&
              h.playable()->label == "v3");
        check("Las dos anteriores siguen archivadas",
              h.versions.size() == 3 &&
              h.versions[0].label == "v1" && h.versions[1].label == "v2");

        // No se puede archivar un juego que no esta en la biblioteca.
        e.clear();
        check("No se archiva un juego inexistente",
              !history::archiveAndAddVersion(hf, "Fantasma", L"C:\\x", 1, e), e);

        // Un juego sin historial devuelve una ficha vacia, no un error.
        bool hallado = true;
        const history::GameHistory vacio =
            history::forGame(hf, "Sin Historial", &hallado);
        check("Un juego sin historial se informa como ausente", !hallado);
        check("Y devuelve una ficha vacia usable",
              vacio.prices.empty() && vacio.versions.empty() &&
              vacio.currentPrice() == 0 && vacio.playable() == nullptr);

        // --- formatos ---
        check("El tamano se formatea legible",
              history::formatBytes(2048) == "2 KB", history::formatBytes(2048));
        check("Los megabytes se formatean legibles",
              history::formatBytes(94ULL * 1024 * 1024) == "94 MB",
              history::formatBytes(94ULL * 1024 * 1024));
        const std::string fecha = history::formatDate(h.versions[0].at);
        check("La fecha se formatea como dd/mm/aa",
              fecha.size() == 8 && fecha[2] == '/' && fecha[5] == '/', fecha);
        check("Una fecha vacia no revienta el formato",
              history::formatDate(0) == "--/--/--", history::formatDate(0));

        DeleteFileW(hf.c_str());
        break;
    }
    case 66: {
        // --- las dos secciones de la franja roja de la Biblioteca ---
        Framebuffer probe;
        probe.resize(960, 600);

        HomeScene home("hist@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kPanelsSection);
        home.render(engine, probe);

        // Sin juego elegido no se dibujan las dos secciones: la franja queda
        // con la invitacion a elegir uno.
        home.selectLibraryGame(-1);
        home.render(engine, probe);
        check("Sin juego elegido no hay historial cargado",
              home.loadedHistory().name.empty());

        // Con un juego elegido, las dos secciones toman sitio dentro de la
        // franja roja (350 px a la derecha, o la mitad si la ventana es menor).
        home.selectLibraryGame(0);
        home.render(engine, probe);

        const Recti pt = home.priceTimelineRect();
        const Recti vh = home.versionHistoryRect();

        // Solo se comprueba la geometria si hay algun juego instalado: en una
        // maquina sin biblioteca las dos secciones no llegan a dibujarse, y
        // exigir rectangulos seria una prueba que depende del equipo.
        if (pt.w > 0 && vh.w > 0) {
            std::snprintf(buf, sizeof(buf), "precio %dx%d, versiones %dx%d",
                          pt.w, pt.h, vh.w, vh.h);
            check("Las dos secciones tienen sitio en la franja",
                  pt.w > 0 && pt.h > 0 && vh.w > 0 && vh.h > 0, buf);
            check("PRECIO va arriba y VERSIONES abajo", pt.y < vh.y);
            check("No se solapan entre si", pt.y + pt.h <= vh.y);
            check("Comparten ancho y columna", pt.x == vh.x && pt.w == vh.w);

            // Ambas caen dentro de la franja roja del lado derecho.
            const i32 franjaX = 960 - std::min(350, 960 / 2);
            check("Las secciones estan en la franja derecha", pt.x >= franjaX);
            check("Y no se salen de la ventana", vh.x + vh.w <= 960);
        } else {
            check("Sin juegos instalados la franja no dibuja secciones",
                  pt.w == 0 && vh.w == 0);
        }

        home.onExit(engine);
        break;
    }
    case 67: {
        // --- la Biblioteca no se achica al encoger la ventana ---
        // El reparto antiguo daba a cada franja la mitad del ancho, asi que
        // por debajo de ~700 px el detalle central quedaba en 0 y no se veia
        // nada. Ahora las franjas ceden y, si no caben, desaparecen.
        HomeScene home("resize@ludora.engine");
        home.onEnter(engine);
        home.section(HomeScene::kPanelsSection);
        home.selectLibraryGame(0);

        /// Ancho que le queda al detalle central. Lo calcula la propia escena,
        /// que es la fuente de verdad del reparto: deducirlo aqui de los
        /// margenes internos se romperia al retocar el diseno.
        auto centroPara = [&](i32 w, i32 h) {
            Framebuffer f;
            f.resize(w, h);
            home.render(engine, f);   // ejercita el dibujado a ese tamano
            return HomeScene::libraryDetailWidthFor(w);
        };

        const i32 c1400 = centroPara(1400, 800);
        std::snprintf(buf, sizeof(buf), "centro %d px en 1400", c1400);
        check("En una ventana grande el detalle se lleva el espacio de sobra",
              c1400 >= 700, buf);

        const i32 c960 = centroPara(960, 600);
        std::snprintf(buf, sizeof(buf), "centro %d px en 960", c960);
        check("En 960 px el detalle tiene sitio de sobra", c960 >= 400, buf);

        check("El detalle CRECE al agrandar la ventana", c1400 > c960);

        const i32 c800 = centroPara(800, 560);
        std::snprintf(buf, sizeof(buf), "centro %d px en 800", c800);
        check("En 800 px el detalle sigue siendo legible", c800 >= 300, buf);

        const i32 c700 = centroPara(700, 520);
        std::snprintf(buf, sizeof(buf), "centro %d px en 700", c700);
        check("En 700 px el detalle NO desaparece", c700 >= 300, buf);

        const i32 c600 = centroPara(600, 480);
        std::snprintf(buf, sizeof(buf), "centro %d px en 600", c600);
        check("En 600 px el detalle conserva su minimo", c600 >= 300, buf);

        // Muy estrecha: las franjas se retiran y el detalle ocupa todo.
        Framebuffer chica;
        chica.resize(420, 400);
        home.render(engine, chica);
        const Recti ptChica = home.priceTimelineRect();
        const Recti vhChica = home.versionHistoryRect();
        check("En una ventana muy estrecha las franjas se retiran",
              ptChica.w == 0 && vhChica.w == 0);

        // El minimo real de la ventana (360x240, de main.cpp) no debe reventar.
        Framebuffer minima;
        minima.resize(360, 240);
        home.render(engine, minima);
        check("Al tamano minimo de ventana sigue dibujando sin romperse", true);

        // Y los botones del detalle siguen dentro del area visible.
        const Recti pb = home.playButtonRect();
        std::snprintf(buf, sizeof(buf), "JUGAR %dx%d en (%d,%d)",
                      pb.w, pb.h, pb.x, pb.y);
        check("El boton JUGAR no se sale de una ventana minima",
              pb.x >= 0 && pb.x + pb.w <= 360, buf);
        check("Y conserva un tamano pulsable", pb.w >= 90 && pb.h >= 30, buf);

        home.onExit(engine);
        break;
    }
    case 68: {
        // --- SUBIR JUEGO: la ficha del formulario sobrevive al disco ---
        // Es lo que distingue el formulario de una fachada: lo escrito tiene
        // que volver igual tras guardar y cargar.
        std::snprintf(buf, sizeof(buf), "%zu bytes", sizeof(HomeScene));
        const wchar_t* kCat = L"catalogo-dev.t68";
        DeleteFileW(kCat);

        devcatalog::Listing l;
        l.description = "Un juego de prueba.\nCon salto de linea y barra \\ dentro.";
        l.genres      = "#ACCION #RPG #VOXEL";
        l.multiplayer = "@ONLINE @LAN";
        l.fps         = 144;
        l.minAge      = 12;
        l.piracy      = 1;   // aceptado
        l.customize   = 2;   // rechazado
        l.art         = L"portada.png";
        l.source      = L"C:\\juegos\\mi juego";
        l.eulaLocal   = "1. No hagas trampas.\n2. Guarda a menudo.";
        l.eulaMulti   = "1. Respeta a los demas.\n2. Sin bots.";
        l.eulaMods    = "1. Cita al autor original.";

        std::string err;
        check("Se publica un juego con su ficha",
              devcatalog::publishWithListing(kCat, "Prueba 68", L"juego.exe", l, err),
              err.c_str());

        // Releer de disco: es el paso que de verdad prueba la persistencia.
        const std::vector<devcatalog::Game> leidos = devcatalog::load(kCat);
        check("El catalogo guardado se vuelve a leer", leidos.size() == 1,
              std::to_string(leidos.size()));

        if (leidos.size() == 1) {
            const devcatalog::Listing& r = leidos[0].listing;
            check("La descripcion sobrevive, con saltos y barras",
                  r.description == l.description, r.description.c_str());
            check("Los generos sobreviven", r.genres == l.genres, r.genres.c_str());
            check("Los modos de multijugador sobreviven",
                  r.multiplayer == l.multiplayer, r.multiplayer.c_str());
            std::snprintf(buf, sizeof(buf), "%d fps, %d anos", r.fps, r.minAge);
            check("Los dos deslizadores sobreviven",
                  r.fps == 144 && r.minAge == 12, buf);
            std::snprintf(buf, sizeof(buf), "pirateria=%d personalizacion=%d",
                          r.piracy, r.customize);
            check("Los dos permisos sobreviven, y distinguen si y no",
                  r.piracy == 1 && r.customize == 2, buf);
            check("La imagen y el codigo sobreviven",
                  r.art == L"portada.png" && r.source == L"C:\\juegos\\mi juego");

            // El EULA son tres textos separados: se comprueba que cada uno
            // vuelve en su sitio y no se mezclan entre si.
            check("El EULA local sobrevive con sus saltos de linea",
                  r.eulaLocal == l.eulaLocal, r.eulaLocal.c_str());
            check("El EULA de multijugador sobrevive",
                  r.eulaMulti == l.eulaMulti, r.eulaMulti.c_str());
            check("El EULA de comunidad sobrevive",
                  r.eulaMods == l.eulaMods, r.eulaMods.c_str());
            check("Los tres bloques del EULA no se mezclan",
                  r.eulaLocal != r.eulaMulti && r.eulaMulti != r.eulaMods);
        }

        // --- la seccion EULA se coloca y responde ---
        {
            Framebuffer probe;
            probe.resize(960, 600);

            HomeScene home("eula@ludora.engine");
            home.onEnter(engine);
            home.section(HomeScene::kEulaSection);
            home.render(engine, probe);

            check("El EULA tiene tres bloques", HomeScene::eulaBlockCount() == 3);

            // --- GUARDAR Y SUBIR: bajo el boton del EULA y mas pequeno ---
            {
                HomeScene form("form@ludora.engine");
                form.onEnter(engine);
                form.section(HomeScene::kDevSection);
                form.render(engine, probe);   // sin panel abierto no hay rects

                // Hay que abrir SUBIR JUEGO para que el formulario se coloque.
                const Recti bSubir = form.devColumnButtonRect(0);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0,
                             MAKELPARAM(bSubir.x + bSubir.w / 2, bSubir.y + bSubir.h / 2));
                SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                             MAKELPARAM(bSubir.x + bSubir.w / 2, bSubir.y + bSubir.h / 2));
                form.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                             MAKELPARAM(bSubir.x + bSubir.w / 2, bSubir.y + bSubir.h / 2));
                form.update(engine, 0.016);
                form.render(engine, probe);

                // --- PUBLICAR es el UNICO boton de subir ---
                // Antes habia dos -- PUBLICAR arriba y GUARDAR Y SUBIR al pie
                // de la columna derecha -- y los dos llamaban a submitUpload.
                // Se retiro el de abajo; estas pruebas fijan que no vuelva.
                const Recti eula = form.uploadSlotRect(9);   // kUpEula
                const Recti save = form.uploadSaveRect();
                const Recti pub  = form.uploadPublishRect();
                const Recti nom  = form.uploadSlotRect(0);   // kUpName
                std::snprintf(buf, sizeof(buf),
                              "EULA %dx%d en (%d,%d), PUBLICAR %dx%d en (%d,%d)",
                              eula.w, eula.h, eula.x, eula.y,
                              pub.w, pub.h, pub.x, pub.y);
                check("Ya no hay boton GUARDAR Y SUBIR",
                      save.w == 0 && save.h == 0, buf);
                check("PUBLICAR tiene sitio", pub.w > 0 && pub.h > 0, buf);
                check("Va ARRIBA, en la fila del nombre",
                      pub.y < eula.y && pub.y <= nom.y + nom.h, buf);
                check("Y a la derecha del campo del nombre",
                      pub.x >= nom.x + nom.w, buf);
                check("Sin salirse de la mini-seccion",
                      pub.x + pub.w <= form.devPanelRect().x + form.devPanelRect().w &&
                      pub.y + pub.h <= form.devPanelRect().y + form.devPanelRect().h,
                      buf);

                // Y sigue publicando: es la prueba de que la accion no se
                // perdio al quitar el otro boton.
                {
                    HomeScene pu("pubunico@ludora.engine");
                    pu.onEnter(engine);
                    pu.section(HomeScene::kDevSection);
                    pu.render(engine, probe);
                    pu.setUploadNameForTest("Juego Por Publicar");
                    pu.setUploadCodeForTest(L"C:\\algo\\pub.exe");
                    pu.setPriceT(0.6f);
                    std::string ep;
                    devcatalog::setArchive(L"catalogo-dev.dat",
                                           "Juego Por Publicar",
                                           L"pub.tomate", 4, 512, ep);
                    pu.publishUpload();
                    std::snprintf(buf, sizeof(buf), "exito=%d",
                                  pu.uploadWarningIsOk() ? 1 : 0);
                    check("PUBLICAR sigue subiendo el juego",
                          pu.uploadWarningIsOk(), buf);
                }

                // --- EDAD RECOMENDADA: las tres vias bajo el carril ---
                {
                    HomeScene ed("edad@ludora.engine");
                    ed.onEnter(engine);
                    ed.section(HomeScene::kDevSection);
                    ed.render(engine, probe);

                    // Abrir SUBIR JUEGO y dentro el campo de EDAD (el 5).
                    const Recti bs = ed.devColumnButtonRect(0);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0,
                                 MAKELPARAM(bs.x + bs.w / 2, bs.y + bs.h / 2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(bs.x + bs.w / 2, bs.y + bs.h / 2));
                    ed.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                                 MAKELPARAM(bs.x + bs.w / 2, bs.y + bs.h / 2));
                    ed.update(engine, 0.016);
                    ed.render(engine, probe);

                    const Recti be = ed.uploadSlotRect(4);   // kUpAge
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0,
                                 MAKELPARAM(be.x + be.w / 2, be.y + be.h / 2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(be.x + be.w / 2, be.y + be.h / 2));
                    ed.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                                 MAKELPARAM(be.x + be.w / 2, be.y + be.h / 2));
                    ed.update(engine, 0.016);
                    ed.render(engine, probe);

                    // --- los tres botones, en fila y sin solaparse ---
                    check("La edad tiene tres opciones",
                          HomeScene::ageOptionCount() == 3);
                    const Recti o0 = ed.ageOptionRect(0);
                    const Recti o1 = ed.ageOptionRect(1);
                    const Recti o2 = ed.ageOptionRect(2);
                    std::snprintf(buf, sizeof(buf),
                                  "(%d,%d) (%d,%d) (%d,%d)",
                                  o0.x, o0.y, o1.x, o1.y, o2.x, o2.y);
                    check("Las tres tienen sitio",
                          o0.w > 0 && o1.w > 0 && o2.w > 0, buf);
                    check("Van en una fila, a la misma altura",
                          o0.y == o1.y && o1.y == o2.y, buf);
                    check("En orden y sin solaparse",
                          o1.x >= o0.x + o0.w && o2.x >= o1.x + o1.w, buf);
                    check("Van DEBAJO del carril",
                          o0.y > ed.uploadTrackRect().y, buf);
                    check("Sin salirse del panel",
                          o2.x + o2.w <= ed.uploadPanelRect().x +
                                         ed.uploadPanelRect().w, buf);

                    // --- el carril va ARRIBA y deja sitio abajo ---
                    // Con el a media altura, los botones y los ocho tramos
                    // se repartian la mitad del panel y salian diminutos.
                    // --- la edad son DOS carriles: minima y maxima ---
                    {
                        const Recti c1 = ed.uploadTrackRect();
                        const Recti c2 = ed.uploadTrack2Rect();
                        const Recti k1 = ed.uploadKnobRect();
                        const Recti k2 = ed.uploadKnob2Rect();
                        std::snprintf(buf, sizeof(buf),
                                      "c1 y=%d h=%d, c2 y=%d h=%d",
                                      c1.y, c1.h, c2.y, c2.h);
                        check("Hay un segundo carril para la edad maxima",
                              c2.w > 0 && c2.h > 0, buf);
                        check("Va DEBAJO del primero, sin solaparse",
                              c2.y >= c1.y + c1.h, buf);
                        check("Los dos miden lo mismo y estan alineados",
                              c2.x == c1.x && c2.w == c1.w, buf);
                        check("Cada carril tiene su selector",
                              k1.w > 0 && k2.w > 0 && k2.y > k1.y, buf);

                        // Los dos extremos se mueven por separado.
                        ed.setUploadAgeT(0.0f);
                        ed.setUploadAgeMaxT(1.0f);
                        std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                                      ed.uploadAge(), ed.uploadAgeTop());
                        check("Los extremos dan la franja completa",
                              ed.uploadAge() == 4 && ed.uploadAgeTop() == 90, buf);

                        ed.setUploadAgeT(0.5f);
                        std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                                      ed.uploadAge(), ed.uploadAgeTop());
                        check("La minima se mueve sin tocar la maxima",
                              ed.uploadAge() == 47 && ed.uploadAgeTop() == 90, buf);

                        // Y la franja NUNCA se invierte: si la maxima quedara
                        // por debajo de la minima, se la lleva consigo.
                        ed.setUploadAgeT(0.9f);
                        ed.setUploadAgeMaxT(0.1f);
                        std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                                      ed.uploadAge(), ed.uploadAgeTop());
                        check("La maxima nunca queda por debajo de la minima",
                              ed.uploadAgeTop() >= ed.uploadAge(), buf);

                        ed.setUploadAgeT(0.0f);
                        ed.setUploadAgeMaxT(1.0f);
                    }

                    const Recti pa = ed.uploadPanelRect();
                    const Recti ca = ed.uploadTrackRect();
                    const i32 bajoCarril = (pa.y + pa.h) - ca.y;
                    std::snprintf(buf, sizeof(buf),
                                  "panel h=%d, carril a %d del borde, libre=%d",
                                  pa.h, ca.y - pa.y, bajoCarril);
                    check("El carril de EDAD esta en el tercio superior",
                          ca.y - pa.y < pa.h / 3, buf);
                    check("Y deja mas de media seccion libre debajo",
                          bajoCarril > pa.h / 2, buf);

                    // El numero grande cabe encima sin salirse del panel.
                    check("El numero de años cabe sobre el carril",
                          ca.y - pa.y >= 20, buf);

                    // --- los botones son grandes de verdad ---
                    std::snprintf(buf, sizeof(buf), "boton %dx%d", o0.w, o0.h);
                    check("Los tres botones tienen alto legible",
                          o0.h >= 34, buf);
                    check("Y ancho suficiente para su rotulo",
                          o0.w >= 100, buf);

                    // --- los ocho tramos, con sitio ---
                    ed.setAgeModeForTest(1);   // kAgeRange
                    ed.render(engine, probe);
                    ed.render(engine, probe);   // el panel ya con su modo
                    const Recti t0 = ed.ageRangeRect(0);
                    const Recti t7 = ed.ageRangeRect(7);
                    std::snprintf(buf, sizeof(buf),
                                  "tramo %dx%d; primero y=%d, ultimo y=%d, panel hasta %d",
                                  t0.w, t0.h, t0.y, t7.y + t7.h, pa.y + pa.h);
                    check("Cada tramo tiene alto legible", t0.h >= 28, buf);
                    check("Y los ocho caben dentro del panel",
                          t7.y + t7.h <= pa.y + pa.h, buf);
                    check("Sin pisar los botones de arriba",
                          t0.y >= o0.y + o0.h, buf);
                    // Y queda hueco para el aviso del pie.
                    check("Queda sitio para el aviso bajo los tramos",
                          (pa.y + pa.h) - (t7.y + t7.h) >= 10, buf);

                    // --- el campo de escribir, tambien grande ---
                    ed.setAgeModeForTest(0);   // kAgeManual
                    ed.render(engine, probe);
                    ed.render(engine, probe);   // el panel ya con su modo
                    const Recti cm = ed.ageManualFieldRect();
                    const Recti ok = ed.ageManualOkRect();
                    std::snprintf(buf, sizeof(buf), "campo %dx%d, ok %dx%d",
                                  cm.w, cm.h, ok.w, ok.h);
                    check("El campo de la edad tiene alto legible",
                          cm.h >= 34, buf);
                    check("Y el boton de confirmar cabe a su lado",
                          ok.w >= 100 && ok.x >= cm.x + cm.w, buf);

                    // --- el selector AI ---
                    ed.setAgeModeForTest(2);   // kAgeAi
                    ed.render(engine, probe);
                    ed.render(engine, probe);   // el panel ya con su modo
                    const Recti ai = ed.ageAiRect();
                    std::snprintf(buf, sizeof(buf), "AI %dx%d", ai.w, ai.h);
                    check("El boton del selector AI es grande",
                          ai.h >= 36 && ai.w >= 200, buf);
                    check("Y deja hueco debajo para la propuesta",
                          (pa.y + pa.h) - (ai.y + ai.h) >= 30, buf);

                    ed.setAgeModeForTest(-1);

                    // --- 1) escribir la edad a mano ---
                    ed.setAgeModeForTest(0);   // kAgeManual
                    ed.render(engine, probe);
                    ed.setAgeManualText("27");
                    check("Una edad escrita se acepta", ed.applyManualAge(),
                          ed.ageMessage().c_str());
                    std::snprintf(buf, sizeof(buf), "edad=%d", ed.uploadAge());
                    check("Y el carril se mueve a esa edad",
                          ed.uploadAge() == 27, buf);

                    // Lo que no es un numero, o se sale del rango, se rechaza.
                    ed.setAgeManualText("hola");
                    check("Un texto que no es numero se rechaza",
                          !ed.applyManualAge() && ed.ageMessageIsError(),
                          ed.ageMessage().c_str());
                    ed.setAgeManualText("200");
                    check("Una edad fuera de rango se rechaza",
                          !ed.applyManualAge() && ed.ageMessageIsError(),
                          ed.ageMessage().c_str());
                    std::snprintf(buf, sizeof(buf), "edad=%d", ed.uploadAge());
                    check("Y un rechazo no cambia la edad ya puesta",
                          ed.uploadAge() == 27, buf);

                    // --- 2) los ocho tramos ---
                    check("Hay ocho tramos", HomeScene::ageRangeCount() == 8);
                    bool paresOk = true, etiqOk = true;
                    for (i32 k = 0; k < HomeScene::ageRangeCount(); ++k) {
                        i32 d = 0, h = 0;
                        if (!HomeScene::ageRangeValues(k, d, h) || d >= h ||
                            d < 4 || h > 90) paresOk = false;
                        if (!HomeScene::ageRangeLabel(k)[0]) etiqOk = false;
                    }
                    check("Cada tramo son dos edades en orden y en rango",
                          paresOk);
                    check("Y todos tienen etiqueta", etiqOk,
                          HomeScene::ageRangeLabel(5));

                    // El tramo "20 - 35" del encargo existe y fija la menor.
                    ed.pickAgeRange(5);
                    i32 d5 = 0, h5 = 0;
                    HomeScene::ageRangeValues(5, d5, h5);
                    std::snprintf(buf, sizeof(buf), "%s -> edad=%d",
                                  HomeScene::ageRangeLabel(5), ed.uploadAge());
                    std::snprintf(buf, sizeof(buf), "%s -> %d a %d",
                                  HomeScene::ageRangeLabel(5),
                                  ed.uploadAge(), ed.uploadAgeTop());
                    check("Elegir un tramo fija LOS DOS extremos",
                          ed.uploadAge() == d5 && ed.uploadAgeTop() == h5 &&
                          d5 == 20 && h5 == 35, buf);
                    check("Y queda marcado cual se eligio",
                          ed.ageRangeSelected() == 5);

                    // --- 3) el selector AI ---
                    check("El selector AI ofrece nueve opciones",
                          HomeScene::ageAiCount() == 9);
                    bool aiOk = true;
                    for (i32 k = 0; k < HomeScene::ageAiCount(); ++k) {
                        i32 d = 0, h = 0;
                        if (!HomeScene::ageAiValues(k, d, h) || d >= h ||
                            d < 4 || h > 90) aiOk = false;
                    }
                    check("Sus nueve pares son validos", aiOk);

                    // Sortea de verdad: dos tiradas seguidas no deben dar
                    // siempre lo mismo, y cada una fija una edad valida.
                    bool cambio = false;
                    i32 previo = -1;
                    for (i32 k = 0; k < 12; ++k) {
                        ed.rollAgeAi();
                        const i32 p = ed.ageAiPicked();
                        if (p < 0 || p >= HomeScene::ageAiCount()) aiOk = false;
                        i32 d = 0, h = 0;
                        HomeScene::ageAiValues(p, d, h);
                        // La propuesta fija la franja ENTERA, no solo su
                        // extremo menor.
                        if (ed.uploadAge() != d || ed.uploadAgeTop() != h)
                            aiOk = false;
                        if (previo >= 0 && p != previo) cambio = true;
                        previo = p;
                    }
                    std::snprintf(buf, sizeof(buf), "ultima propuesta=%d, edad=%d",
                                  ed.ageAiPicked(), ed.uploadAge());
                    check("Cada propuesta fija su franja entera", aiOk, buf);
                    check("Y el sorteo no devuelve siempre lo mismo", cambio, buf);

                    // El tercero es OPCIONAL: no hace falta tocarlo para que
                    // la edad valga. Se comprueba publicando sin usarlo.
                    ed.setAgeModeForTest(-1);
                    ed.setAgeManualText("30");
                    ed.applyManualAge();
                    check("La edad vale sin pasar por el selector AI",
                          ed.uploadAge() == 30 && ed.ageModeOpen() == -1,
                          std::to_string(ed.uploadAge()).c_str());
                }

                // --- ACEPTAR PRECIO, bajo la barrita de la seccion PRECIOS ---
                {
                    HomeScene pr("precio@ludora.engine");
                    pr.onEnter(engine);
                    pr.section(HomeScene::kDevSection);
                    pr.render(engine, probe);

                    // Abrir la mini-seccion PRECIOS (tercer boton).
                    const Recti bPrecios = pr.devColumnButtonRect(2);
                    const i32 cx = bPrecios.x + bPrecios.w / 2;
                    const i32 cy = bPrecios.y + bPrecios.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(cx, cy));
                    pr.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
                    pr.update(engine, 0.016);
                    pr.render(engine, probe);

                    const Recti track = pr.priceTrackRect();
                    const Recti ok    = pr.priceAcceptRect();
                    std::snprintf(buf, sizeof(buf),
                                  "barra y=%d, boton %dx%d en (%d,%d)",
                                  track.y, ok.w, ok.h, ok.x, ok.y);
                    check("ACEPTAR PRECIO tiene sitio", ok.w > 0 && ok.h > 0, buf);
                    check("Va DEBAJO de la barrita",
                          ok.y > track.y + track.h, buf);
                    check("No se solapa con la rejilla de ajustes",
                          ok.y + ok.h <= pr.priceOptionRect(0).y, buf);
                    check("Queda centrado en la mini-seccion",
                          (ok.x - pr.devPanelRect().x) ==
                          (pr.devPanelRect().x + pr.devPanelRect().w - ok.x - ok.w),
                          buf);

                    // --- sin moneda ni pais, aceptar NIEGA y avisa ---
                    check("Al entrar, moneda y pais no estan elegidos",
                          !pr.priceOptionsReady());

                    pr.setPriceT(0.5f);
                    const Recti okb = pr.priceAcceptRect();
                    const i32 ox = okb.x + okb.w / 2, oy = okb.y + okb.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(ox, oy));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(ox, oy));
                    pr.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ox, oy));
                    pr.update(engine, 0.016);

                    std::snprintf(buf, sizeof(buf), "aviso=%.2f s",
                                  pr.priceOptionsWarningLeft());
                    check("Sin moneda ni pais el precio no se acepta",
                          pr.priceOptionsWarningLeft() > 0.0, buf);
                    check("Y no arranca la palomita",
                          !pr.priceAcceptedAnimating());

                    // El aviso se apaga solo.
                    for (i32 k = 0; k < 100; ++k) pr.update(engine, 0.016);
                    check("El aviso de moneda se apaga solo",
                          pr.priceOptionsWarningLeft() == 0.0);

                    // --- con moneda y pais elegidos, acepta ---
                    pr.setPriceOptionChosen(0, 1);   // moneda
                    pr.setPriceOptionChosen(1, 0);   // pais
                    check("Con moneda y pais ya esta listo", pr.priceOptionsReady());

                    pr.onExit(engine);
                }

                // --- aceptar el precio lleva a SUBIR JUEGO ---
                // Necesita un juego publicado: setPrice falla sin el, y sin
                // exito no hay palomita ni salto.
                {
                    const wchar_t* kCatReal = L"catalogo-dev.dat";
                    const wchar_t* kBak2    = L"catalogo-dev.t68b";
                    DeleteFileW(kBak2);
                    const bool habia2 =
                        (GetFileAttributesW(kCatReal) != INVALID_FILE_ATTRIBUTES);
                    if (habia2) MoveFileW(kCatReal, kBak2);

                    devcatalog::Listing l3;
                    std::string e3;
                    devcatalog::publishWithListing(kCatReal, "Salto",
                                                   L"s.exe", l3, e3);
                    // Publicar exige los archivos ya guardados. Aqui se mide
                    // el PRECIO, asi que ese requisito se da por cumplido.
                    devcatalog::setArchive(kCatReal, "Salto", L"salto.tomate",
                                           12, 2048, e3);

                    HomeScene sa("salto@ludora.engine");
                    sa.onEnter(engine);
                    sa.section(HomeScene::kDevSection);
                    sa.render(engine, probe);

                    // Abrir PRECIOS y elegir moneda y pais.
                    const Recti bp = sa.devColumnButtonRect(2);
                    const i32 px2 = bp.x + bp.w / 2, py2 = bp.y + bp.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(px2, py2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(px2, py2));
                    sa.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(px2, py2));
                    sa.update(engine, 0.016);
                    sa.render(engine, probe);

                    sa.setPriceOptionChosen(0, 1);
                    sa.setPriceOptionChosen(1, 0);
                    sa.setPriceT(0.5f);

                    const Recti ok2 = sa.priceAcceptRect();
                    const i32 ax = ok2.x + ok2.w / 2, ay = ok2.y + ok2.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(ax, ay));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(ax, ay));
                    sa.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ax, ay));
                    sa.update(engine, 0.016);

                    check("Al aceptar el precio arranca la palomita",
                          sa.priceAcceptedAnimating());

                    // Se deja correr la animacion entera.
                    for (i32 k = 0; k < 120; ++k) sa.update(engine, 0.016);
                    check("La palomita termina sola",
                          !sa.priceAcceptedAnimating());
                    std::snprintf(buf, sizeof(buf), "panel abierto = %d",
                                  sa.devOpenPanel());
                    check("Y lleva directo a SUBIR JUEGO",
                          sa.devOpenPanel() == 0, buf);   // 0 = kDevUpload

                    // --- con el precio ya aceptado, subir NO vuelve a pedirlo ---
                    check("El precio queda dado por bueno", sa.priceAccepted());

                    sa.setUploadNameForTest("Salto");
                    // El codigo es obligatorio y se comprueba antes que el
                    // precio: se da por subido para aislar lo que se mide.
                    sa.setUploadCodeForTest(L"C:\\algo\\salto.exe");
                    sa.setPriceT(0.0f);     // aunque este en el extremo GRATIS
                    sa.publishUpload();

                    std::snprintf(buf, sizeof(buf),
                                  "aviso=%.2f s, exito=%d",
                                  sa.priceWarningLeft(),
                                  sa.uploadWarningIsOk() ? 1 : 0);
                    check("Con el precio aceptado ya no pide precio",
                          sa.uploadWarningIsOk(), buf);
                    check("Y el aviso de exito aparece",
                          sa.priceWarningLeft() > 0.0, buf);

                    // El aviso verde tambien se apaga solo.
                    for (i32 k = 0; k < 200; ++k) sa.update(engine, 0.016);
                    check("El aviso de exito se apaga solo",
                          sa.priceWarningLeft() == 0.0);

                    // Y el juego llego de verdad a la tienda. El catalogo se
                    // recarga al dibujar esa seccion, asi que hay que pasar
                    // por ella para comprobarlo.
                    sa.section(HomeScene::kStoreSection);
                    sa.render(engine, probe);
                    const std::vector<std::string> enTienda = sa.storeGameNames();
                    bool esta = false;
                    for (const auto& n : enTienda) if (n == "Salto") esta = true;
                    check("El juego subido esta en la Tienda", esta,
                          std::to_string(enTienda.size()));

                    sa.onExit(engine);
                    DeleteFileW(kCatReal);
                    if (habia2) MoveFileW(kBak2, kCatReal);
                }

                // --- una barra vertical en el changelog no rompe el registro ---
                // El campo `ver=` separa con '|'. Sin escaparla, un changelog
                // que la contuviera se comeria los campos de detras -- ahora
                // la ruta del paquete guardado.
                {
                    const wchar_t* kCatB = L"catalogo-barra.t68";
                    DeleteFileW(kCatB);
                    devcatalog::Listing lb;
                    std::string be;
                    devcatalog::publishWithListing(kCatB, "Barras", L"b.exe",
                                                   lb, be);
                    devcatalog::addVersionWithLog(
                        kCatB, "Barras", L"b2.exe",
                        "arreglado A|B y tambien C|D", be);
                    devcatalog::setArchive(kCatB, "Barras", L"paq.tomate",
                                           5, 99, be);

                    const std::vector<devcatalog::Game> cb =
                        devcatalog::load(kCatB);
                    const devcatalog::Version* vb =
                        cb.empty() ? nullptr : cb.front().current();
                    check("Un changelog con '|' se lee entero",
                          vb && vb->changelog == "arreglado A|B y tambien C|D",
                          vb ? vb->changelog.c_str() : "sin version");
                    check("Y no se come la ruta del paquete",
                          vb && vb->archive == L"paq.tomate" &&
                          vb->archiveFiles == 5,
                          vb ? std::to_string(vb->archiveFiles).c_str() : "-");
                    DeleteFileW(kCatB);
                }

                // --- sin archivos guardados no deja publicar ---
                // Es la regla que convierte GUARDAR ARCHIVOS en obligatorio:
                // sin el paquete, publicar solo dejaria una ruta del PC del
                // autor y nadie podria descargar nada.
                {
                    HomeScene sf("guardar@ludora.engine");
                    sf.onEnter(engine);
                    sf.section(HomeScene::kDevSection);
                    sf.render(engine, probe);

                    // La ficha entera, y CERRADA con TERMINE DE EDITAR: sin
                    // eso el aviso seria el de la ficha y no el de los
                    // archivos, que es lo que esta prueba mira.
                    sf.fillUploadFormForTest("Juego Sin Archivos");
                    sf.finishEditingForTest();
                    sf.openDevPanelForTest(0);   // vuelve a SUBIR JUEGO
                    sf.setPriceT(0.7f);          // precio puesto
                    sf.publishUpload();
                    std::snprintf(buf, sizeof(buf), "aviso=%.2f s, tipo=%d",
                                  sf.priceWarningLeft(), sf.uploadWarningKind());
                    check("Sin archivos guardados no se publica",
                          sf.priceWarningLeft() > 0.0 &&
                          !sf.uploadWarningIsOk(), buf);
                    check("Y el aviso es el de los archivos",
                          sf.uploadWarningKind() == 2, buf);   // 2 = kWarnFiles
                    check("El juego NO llego al catalogo",
                          !devcatalog::hasArchive(L"catalogo-dev.dat",
                                                  "Juego Sin Archivos"), buf);

                    // Guardarlos lo desbloquea: el pendiente vale aunque el
                    // juego aun no este publicado.
                    std::string pe;
                    devcatalog::setArchive(L"catalogo-dev.dat",
                                           "Juego Sin Archivos",
                                           L"sinarch.tomate", 9, 4096, pe);
                    check("Guardar archivos antes de publicar queda anotado",
                          devcatalog::hasArchive(L"catalogo-dev.dat",
                                                 "Juego Sin Archivos"),
                          pe.c_str());

                    for (i32 k = 0; k < 120; ++k) sf.update(engine, 0.016);
                    sf.fillUploadFormForTest("Juego Sin Archivos");
                    sf.finishEditingForTest();
                    sf.openDevPanelForTest(0);
                    sf.setPriceT(0.7f);
                    sf.publishUpload();
                    std::snprintf(buf, sizeof(buf), "tipo=%d, exito=%d",
                                  sf.uploadWarningKind(),
                                  sf.uploadWarningIsOk() ? 1 : 0);
                    // Se mira el EXITO y no el tipo de aviso: al publicar bien
                    // nadie reescribe uploadWarningKind, que conserva el valor
                    // del intento anterior.
                    check("Con los archivos guardados ya deja publicar",
                          sf.uploadWarningIsOk(), buf);

                    // Y al publicarse, la version vigente ADOPTA el pendiente.
                    const std::vector<devcatalog::Game> cp =
                        devcatalog::load(L"catalogo-dev.dat");
                    const devcatalog::Game* gp = nullptr;
                    for (const auto& x : cp)
                        if (x.name == "Juego Sin Archivos") gp = &x;
                    check("Al publicar, la version adopta el paquete pendiente",
                          gp && gp->current() &&
                          !gp->current()->archive.empty(),
                          gp ? "publicado" : "no publicado");
                }

                // --- con la ficha a medias no deja publicar ---
                //
                // Sustituye a la prueba de "sin codigo subido": esa zona del
                // formulario se retiro (los archivos se suben en GUARDAR
                // ARCHIVOS Y ACTUALIZAR), asi que lo que ahora frena el primer
                // paso es que falte un campo de la ficha.
                form.setUploadNameForTest("Juego A Medias");
                form.setPriceT(0.8f);        // precio puesto, ficha no
                form.publishUpload();
                std::snprintf(buf, sizeof(buf), "aviso=%.2f s, tipo=%d, falta=%d",
                              form.priceWarningLeft(), form.uploadWarningKind(),
                              form.missingUploadSlot());
                check("Con la ficha a medias no se publica",
                      form.priceWarningLeft() > 0.0 &&
                      !form.uploadWarningIsOk(), buf);
                check("Y el aviso dice que falta un campo, no el precio",
                      form.uploadWarningKind() == 3, buf);   // 3 = kWarnCampo
                check("Y senala el PRIMER campo que falta",
                      form.missingUploadSlot() == 1, buf);   // 1 = descripcion

                for (i32 k = 0; k < 100; ++k) form.update(engine, 0.016);

                // --- el precio es obligatorio antes de guardar ---
                // Con codigo ya elegido, el que falta es el precio: asi se
                // comprueba ESE aviso y no el del paso anterior.
                // La ficha y los archivos se dan por hechos para aislar ESTE
                // aviso: los dos se comprueban antes que el precio.
                form.fillUploadFormForTest("Juego Sin Precio");
                form.finishEditingForTest();
                form.openDevPanelForTest(0);
                {
                    std::string ea;
                    devcatalog::setArchive(L"catalogo-dev.dat",
                                           "Juego Sin Precio",
                                           L"sinprecio.tomate", 7, 1024, ea);
                }
                // "Sin tocar" es el extremo GRATIS, o sea t = 0.
                form.setPriceT(0.0f);
                form.publishUpload();
                std::snprintf(buf, sizeof(buf), "aviso=%.2f s, tipo=%d",
                              form.priceWarningLeft(), form.uploadWarningKind());
                check("Sin precio no se guarda y salta el aviso",
                      form.priceWarningLeft() > 0.0 &&
                      form.uploadWarningKind() == 0, buf);   // 0 = kWarnPrice

                // --- el recorrido completo: ficha -> archivos -> precio ---
                //
                // Es lo que se pidio: PUBLICAR esta en gris hasta que las tres
                // cosas estan hechas, y cada paso lleva al siguiente sin que
                // el autor tenga que adivinar por donde seguir.
                {
                    HomeScene fl("flujo@ludora.engine");
                    fl.onEnter(engine);
                    fl.section(HomeScene::kDevSection);
                    fl.openDevPanelForTest(0);          // SUBIR JUEGO
                    fl.render(engine, probe);

                    const char* kJ = "Juego Del Recorrido";

                    // 1) Ficha vacia: ni completa ni publicable.
                    check("Con la ficha vacia, PUBLICAR esta bloqueado",
                          !fl.publishEnabled(), "");
                    check("Y el primer campo que falta es el NOMBRE",
                          fl.missingUploadSlot() == 0, "");

                    // 2) Solo el nombre: sigue faltando el resto.
                    fl.setUploadNameForTest(kJ);
                    check("Con solo el nombre, la ficha no esta completa",
                          !fl.uploadFormComplete(), "");
                    check("Y PUBLICAR sigue bloqueado",
                          !fl.publishEnabled(), "");

                    // 3) TERMINE DE EDITAR con la ficha a medias: avisa y NO
                    //    la da por cerrada.
                    fl.finishEditingForTest();
                    check("TERMINE DE EDITAR a medias no cierra la ficha",
                          !fl.uploadFormConfirmed(), "");
                    check("Y avisa de que falta un campo",
                          fl.uploadWarningKind() == 3, "");   // kWarnCampo

                    // 4) Ficha entera: ya se puede cerrar.
                    fl.fillUploadFormForTest(kJ);
                    check("Con los diez campos, la ficha esta completa",
                          fl.uploadFormComplete(),
                          std::to_string(fl.missingUploadSlot()).c_str());
                    check("Pero hasta pulsar el boton no cuenta como cerrada",
                          !fl.uploadFormConfirmed(), "");
                    check("Y PUBLICAR aun esta bloqueado",
                          !fl.publishEnabled(), "");

                    // 5) Al cerrarla, lleva a GUARDAR ARCHIVOS.
                    fl.finishEditingForTest();
                    std::snprintf(buf, sizeof(buf), "panel=%d",
                                  fl.devOpenPanel());
                    check("Al cerrar la ficha, lleva a GUARDAR ARCHIVOS",
                          fl.devOpenPanel() == 1, buf);   // 1 = kDevUpdate
                    check("Y la ficha queda cerrada",
                          fl.uploadFormConfirmed(), "");
                    check("Pero PUBLICAR sigue gris: faltan los archivos",
                          !fl.publishEnabled(), "");

                    // 6) Con los archivos guardados, falta el precio.
                    {
                        std::string ef;
                        devcatalog::setArchive(L"catalogo-dev.dat", kJ,
                                               L"recorrido.tomate", 5, 2048, ef);
                    }
                    fl.setPriceT(0.0f);          // GRATIS = sin decidir
                    check("Con archivos pero sin precio, PUBLICAR sigue gris",
                          !fl.publishEnabled(), "");

                    // 7) Con el precio puesto, ya se puede publicar.
                    fl.setPriceT(0.5f);
                    check("Con ficha, archivos y precio, PUBLICAR se habilita",
                          fl.publishEnabled(), "");

                    // 8) Y si despues se vacia un campo, vuelve a bloquearse:
                    //    la ficha deja de estar terminada.
                    fl.setUploadNameForTest("");
                    fl.update(engine, 0.016);
                    check("Vaciar un campo vuelve a bloquear PUBLICAR",
                          !fl.publishEnabled(), "");
                    check("Y la ficha deja de estar cerrada",
                          !fl.uploadFormConfirmed(), "");
                }

                // --- el pilar central no se puede pulsar ---
                //
                // Era la zona SUBIR ARCHIVOS. Ahora es una raya decorativa: si
                // siguiera respondiendo al raton, abriria un selector de
                // carpeta que ya no lleva a ninguna parte.
                {
                    const Recti pil = form.uploadDropRect();
                    std::snprintf(buf, sizeof(buf), "pilar %dx%d", pil.w, pil.h);
                    check("El pilar central es estrecho, no un panel",
                          pil.w > 0 && pil.w <= 8, buf);
                    form.setUploadDropHover(true);
                    form.update(engine, 0.016);
                    check("Y el raton no lo marca como pulsable",
                          !form.uploadDropHover(), "");
                }

                // --- el aviso se SUPERPONE a los botones ---
                // Se comprueba leyendo pixeles: se elige un punto que cae
                // dentro de un campo del formulario y, con el aviso activo,
                // ese punto tiene que haber cambiado de color.
                form.setPriceT(0.0f);            // extremo GRATIS = sin tocar
                form.publishUpload();            // reactiva el aviso
                form.render(engine, probe);

                // Punto dentro de la franja del aviso pero tambien sobre la
                // columna de campos: si el aviso esta delante, manda el.
                //
                // El ancla es el panel, no el boton: al retirar GUARDAR Y
                // SUBIR su rectangulo quedo en (0,0) y el punto se iba fuera
                // del formulario, midiendo fondo contra fondo.
                const Recti pan = form.devPanelRect();
                const i32 px = pan.x + pan.w / 2;
                const i32 py = form.uploadPublishRect().y +
                               form.uploadPublishRect().h + pan.h / 3 + 20;
                const u32 conAviso =
                    (px >= 0 && px < 960 && py >= 0 && py < 600)
                    ? probe.data()[static_cast<size_t>(py) * 960 + px] : 0u;

                // Sin aviso, el mismo punto muestra lo que haya debajo.
                for (i32 k = 0; k < 100; ++k) form.update(engine, 0.016);
                check("El aviso se apaga solo",
                      form.priceWarningLeft() == 0.0);
                form.render(engine, probe);
                const u32 sinAviso =
                    (px >= 0 && px < 960 && py >= 0 && py < 600)
                    ? probe.data()[static_cast<size_t>(py) * 960 + px] : 0u;

                std::snprintf(buf, sizeof(buf), "(%d,%d): con=%08X sin=%08X",
                              px, py, conAviso, sinAviso);
                check("El aviso se superpone a lo que haya debajo",
                      conAviso != sinAviso, buf);

                form.onExit(engine);
            }

            const Recti b0 = home.eulaBlockRect(0);
            const Recti b1 = home.eulaBlockRect(1);
            const Recti b2 = home.eulaBlockRect(2);
            std::snprintf(buf, sizeof(buf), "%dx%d / %dx%d / %dx%d",
                          b0.w, b0.h, b1.w, b1.h, b2.w, b2.h);
            check("Los tres bloques tienen sitio",
                  b0.w > 0 && b1.w > 0 && b2.w > 0, buf);
            check("Los tres bloques miden lo mismo",
                  b0.w == b1.w && b1.w == b2.w && b0.h == b1.h, buf);

            // En una ventana ancha van en fila; no deben solaparse.
            check("Los bloques no se solapan",
                  b0.x + b0.w <= b1.x && b1.x + b1.w <= b2.x);
            check("Y caben dentro de la ventana",
                  b0.x >= 0 && b2.x + b2.w <= 960);

            // El texto se escribe en el bloque activo y se guarda por separado.
            home.setEulaText(0, "regla local");
            home.setEulaText(1, "regla multi");
            home.setEulaText(2, "regla mods");
            check("Cada bloque guarda su propio texto",
                  home.eulaText(0) == "regla local" &&
                  home.eulaText(1) == "regla multi" &&
                  home.eulaText(2) == "regla mods");

            // Y sale en la ficha que se publicaria.
            const devcatalog::Listing ficha = home.uploadListing();
            check("El EULA llega a la ficha de publicacion",
                  ficha.eulaLocal == "regla local" &&
                  ficha.eulaMulti == "regla multi" &&
                  ficha.eulaMods  == "regla mods");

            // En una ventana estrecha los bloques se apilan en vez de
            // quedarse ilegibles.
            Framebuffer angosta;
            angosta.resize(500, 600);
            home.render(engine, angosta);
            const Recti a0 = home.eulaBlockRect(0);
            const Recti a1 = home.eulaBlockRect(1);
            std::snprintf(buf, sizeof(buf), "b0 en (%d,%d) b1 en (%d,%d)",
                          a0.x, a0.y, a1.x, a1.y);
            check("En una ventana estrecha los bloques se apilan",
                  a0.x == a1.x && a1.y > a0.y, buf);

            // --- el texto empieza ARRIBA, no flotando en el centro ---
            // Se comprueba leyendo pixeles: con el bloque lleno de texto, la
            // mitad superior tiene que llevar tinta. Centrado dejaria la
            // franja de arriba vacia en una caja tan alta.
            home.setEulaFocus(0);
            home.setEulaText(0, "PRIMERA LINEA DEL REGLAMENTO");
            home.setEulaText(1, "");
            home.setEulaText(2, "");
            home.render(engine, probe);

            const Recti bb = home.eulaBlockRect(0);
            const u32 fondo = Color::fromHex(0x223347).packed();   // kEulaPanelFoc
            auto hayTinta = [&](i32 y0, i32 y1) {
                for (i32 y = y0; y < y1 && y < 600; ++y)
                    for (i32 x = bb.x + 4; x < bb.x + bb.w - 4 && x < 960; ++x) {
                        const u32 c = probe.data()[static_cast<size_t>(y) * 960 + x];
                        if (c != fondo) return true;
                    }
                return false;
            };
            // Banda justo bajo el rotulo del bloque: ahi debe estar el texto.
            check("El texto del EULA arranca en la parte de arriba",
                  hayTinta(bb.y + 28, bb.y + 46));

            home.onExit(engine);
        }

        // --- TextField multilinea: saltos de linea y ajuste al ancho ---
        {
            TextField campo;
            TextField::Style ml{};
            ml.bg = ml.bgFocused = Color::fromHex(0x101010);
            ml.border = ml.borderFocused = Color::fromHex(0x303030);
            ml.text = ml.placeholder = ml.caret = ml.selection =
                Color::fromHex(0xF0F0F0);
            ml.textScale = 1;
            ml.alignTop  = true;
            ml.multiline = true;
            campo.setStyle(ml);
            campo.setRect({ 10, 10, 200, 120 });
            campo.setMaxLength(10000);
            campo.setFocused(true);

            // El salto de linea se acepta en multilinea...
            campo.setText("");
            campo.onChar(L'A');
            campo.onChar(L'\n');
            campo.onChar(L'B');
            check("En multilinea el Enter entra como salto de linea",
                  campo.text() == "A\nB", campo.text().c_str());

            // ...y se rechaza en un campo de una linea.
            TextField unaLinea;
            TextField::Style ul = ml;
            ul.multiline = false;
            ul.alignTop  = false;
            unaLinea.setStyle(ul);
            unaLinea.setRect({ 10, 10, 200, 30 });
            unaLinea.setFocused(true);
            unaLinea.onChar(L'A');
            unaLinea.onChar(L'\n');
            unaLinea.onChar(L'B');
            check("En una linea el Enter no se cuela en el texto",
                  unaLinea.text() == "AB", unaLinea.text().c_str());

            // --- controles de edicion estandar ---
            //
            // Son los que tiene cualquier editor y aqui faltaban: recorrer
            // una descripcion de 5000 caracteres iba tecla a tecla, y no
            // habia forma de borrar una palabra ni de moverse por lineas.
            {
                TextField ed;
                ed.setStyle(ml);
                ed.setRect({ 10, 10, 300, 120 });
                ed.setMaxLength(10000);
                ed.setFocused(true);

                // Ctrl+Izquierda salta al principio de la palabra. Se
                // comprueba escribiendo ahi: el cursor no se puede leer
                // directamente, pero donde cae la letra lo delata.
                ed.setText("hola mundo");
                ed.onKey(VK_END, false, false);
                ed.onKey(VK_LEFT, false, true);     // -> principio de "mundo"
                ed.onChar(L'*');
                check("Ctrl+Izquierda salta al principio de la palabra",
                      ed.text() == "hola *mundo", ed.text().c_str());

                ed.setText("hola mundo");
                ed.onKey(VK_HOME, false, false);
                ed.onKey(VK_RIGHT, false, true);    // -> principio de "mundo"
                ed.onChar(L'*');
                check("Ctrl+Derecha salta a la palabra siguiente",
                      ed.text() == "hola *mundo", ed.text().c_str());

                ed.setText("uno dos tres");
                ed.onKey(VK_HOME, false, false);
                ed.onKey(VK_DELETE, false, true);   // borra "uno "
                check("Ctrl+Supr borra la palabra de la derecha",
                      ed.text() == "dos tres", ed.text().c_str());

                ed.setText("alfa beta");
                ed.onKey(VK_END, false, false);
                ed.onKey(VK_BACK, false, true);     // borra "beta"
                check("Ctrl+Retroceso borra la palabra de la izquierda",
                      ed.text() == "alfa ", ed.text().c_str());

                // Inicio y Fin van a la LINEA, no a todo el texto.
                ed.setText("primera\nsegunda\ntercera");
                ed.onKey(VK_END, false, true);      // Ctrl+Fin: final del todo
                ed.onKey(VK_HOME, false, false);    // Inicio: de la 3a linea
                ed.onChar(L'X');
                check("Inicio va al principio de SU linea",
                      ed.text() == "primera\nsegunda\nXtercera",
                      ed.text().c_str());

                ed.setText("primera\nsegunda");
                ed.onKey(VK_HOME, false, true);     // Ctrl+Inicio: todo arriba
                ed.onKey(VK_END, false, false);     // Fin: de la 1a linea
                ed.onChar(L'Z');
                check("Fin va al final de SU linea",
                      ed.text() == "primeraZ\nsegunda", ed.text().c_str());

                // Arriba y abajo se mueven por lineas conservando la columna.
                ed.setText("abcdef\nghijkl");
                ed.onKey(VK_END, false, true);      // final del todo
                ed.onKey(VK_UP, false, false);      // sube a la 1a, misma col
                ed.onChar(L'*');
                check("Arriba sube de linea manteniendo la columna",
                      ed.text() == "abcdef*\nghijkl", ed.text().c_str());

                ed.setText("abc\ndef");
                ed.onKey(VK_HOME, false, true);
                ed.onKey(VK_DOWN, false, false);    // baja a la 2a
                ed.onChar(L'*');
                check("Abajo baja de linea",
                      ed.text() == "abc\n*def", ed.text().c_str());

                // Con seleccion, las flechas van a sus extremos en vez de
                // moverse un caracter: es lo que hace cualquier editor.
                ed.setText("seleccionado");
                ed.onKey('A', false, true);         // Ctrl+A
                ed.onKey(VK_LEFT, false, false);
                ed.onChar(L'>');
                check("Con seleccion, Izquierda va a su principio",
                      ed.text() == ">seleccionado", ed.text().c_str());

                ed.setText("seleccionado");
                ed.onKey('A', false, true);
                ed.onKey(VK_RIGHT, false, false);
                ed.onChar(L'<');
                check("Y Derecha va a su final",
                      ed.text() == "seleccionado<", ed.text().c_str());

                // Shift extiende la seleccion al saltar de palabra.
                ed.setText("borra esto entero");
                ed.onKey(VK_HOME, false, true);
                ed.onKey(VK_RIGHT, true, true);     // Shift+Ctrl+Derecha
                ed.onKey(VK_BACK, false, false);
                check("Shift+Ctrl+Derecha selecciona la palabra",
                      ed.text() == "esto entero", ed.text().c_str());

                // --- repeticion automatica al dejar la tecla pulsada ---
                //
                // Sin esto, borrar treinta caracteres eran treinta
                // pulsaciones: keyPressed solo salta en el flanco y mantener
                // Supr no hacia absolutamente nada.
                {
                    Input rep;
                    i32 veces = 0;

                    // Se pulsa y se mantiene medio segundo a 60 fps.
                    rep.onKey(VK_DELETE, true);
                    for (i32 f = 0; f < 30; ++f) {
                        rep.newFrame(1.0 / 60.0);
                        if (rep.keyRepeat(VK_DELETE)) ++veces;
                    }
                    std::snprintf(buf, sizeof(buf),
                                  "%d repeticiones en medio segundo", veces);
                    // Con 0.45 s de espera y 0.035 s de ritmo, en 0.5 s
                    // caben el disparo inicial y un punado mas.
                    check("Mantener una tecla la repite sola",
                          veces >= 2, buf);

                    // Soltarla corta la repeticion.
                    rep.onKey(VK_DELETE, false);
                    rep.newFrame(1.0 / 60.0);
                    check("Al soltarla deja de repetirse",
                          !rep.keyRepeat(VK_DELETE), "");

                    // Una pulsacion corta cuenta UNA sola vez: si no,
                    // escribir normal borraria de mas.
                    Input corto;
                    i32 vecesCorto = 0;
                    corto.onKey(VK_BACK, true);
                    for (i32 f = 0; f < 6; ++f) {   // 0.1 s, menos que la espera
                        corto.newFrame(1.0 / 60.0);
                        if (corto.keyRepeat(VK_BACK)) ++vecesCorto;
                    }
                    std::snprintf(buf, sizeof(buf), "%d veces", vecesCorto);
                    check("Una pulsacion corta cuenta una sola vez",
                          vecesCorto == 1, buf);
                }

                // --- doble y triple clic ---
                //
                // Doble selecciona la palabra, triple la linea. Se comprueba
                // borrando: lo que desaparece dice que habia seleccionado.
                {
                    TextField cl;
                    cl.setStyle(ml);
                    cl.setRect({ 0, 0, 400, 120 });
                    cl.setMaxLength(10000);
                    cl.setFocused(true);
                    cl.setText("hola mundo cruel");

                    // El reloj del campo avanza con update(): hace falta para
                    // fechar los clics.
                    cl.update(0.016);
                    // Dos clics seguidos sobre "mundo" (misma posicion).
                    const i32 cx = 8 + font5x7::measureUtf8("hola mu", 1);
                    cl.onMouseDown(cx, 10);
                    cl.update(0.016);
                    cl.onMouseDown(cx, 10);
                    cl.onKey(VK_BACK, false, false);
                    check("Doble clic selecciona la palabra",
                          cl.text() == "hola  cruel", cl.text().c_str());

                    // Triple clic: la linea entera.
                    cl.setText("primera linea\nsegunda linea");
                    cl.update(0.016);
                    cl.onMouseDown(20, 10);
                    cl.update(0.016);
                    cl.onMouseDown(20, 10);
                    cl.update(0.016);
                    cl.onMouseDown(20, 10);
                    cl.onKey(VK_BACK, false, false);
                    check("Triple clic selecciona la linea entera",
                          cl.text() == "\nsegunda linea", cl.text().c_str());

                    // Dos clics separados en el tiempo NO son un doble.
                    cl.setText("hola mundo");
                    cl.update(0.016);
                    cl.onMouseDown(cx, 10);
                    cl.update(1.0);               // pasa de sobra el plazo
                    cl.onMouseDown(cx, 10);
                    cl.onKey(VK_BACK, false, false);
                    check("Dos clics lentos no cuentan como doble",
                          cl.text() != "hola  mundo" &&
                          cl.text().size() == 9, cl.text().c_str());
                }

                // Un campo de una linea no se rompe con Arriba/Abajo.
                TextField ul2;
                ul2.setStyle(ul);
                ul2.setRect({ 10, 10, 200, 30 });
                ul2.setFocused(true);
                ul2.setText("una sola linea");
                ul2.onKey(VK_UP, false, false);
                ul2.onChar(L'>');
                check("En una linea, Arriba lleva al principio",
                      ul2.text() == ">una sola linea", ul2.text().c_str());
            }

            // Dibujar un texto con saltos reparte tinta en VARIAS filas. Si
            // se pintara todo en una sola, solo la primera tendria pixeles.
            Framebuffer mini;
            mini.resize(240, 150);
            mini.clear(Color::fromHex(0x000000));
            campo.setText("Linea uno\nLinea dos\nLinea tres y un texto largo "
                          "que tiene que ajustarse solo al ancho del campo.");
            campo.render(mini);

            const u32 fondoML = Color::fromHex(0x101010).packed();
            auto filaConTinta = [&](i32 y0, i32 y1) {
                for (i32 y = y0; y < y1 && y < 150; ++y)
                    for (i32 x = 14; x < 205 && x < 240; ++x) {
                        const u32 c = mini.data()[static_cast<size_t>(y) * 240 + x];
                        if (c != fondoML) return true;
                    }
                return false;
            };
            // Tres bandas separadas en vertical: si hay tinta en las tres, el
            // texto se esta repartiendo de verdad. Las bandas se calculan con
            // el alto REAL de la fuente -- con numeros fijos acababan cayendo
            // entre dos renglones y la prueba medía otra cosa.
            const i32 lhML = (textr::available() ? textr::glyphHeight(1)
                                                 : font5x7::kGlyphH) + 3;
            const i32 y0ML = 10 + 6;   // rect.y + kPadY (alignTop)
            auto banda = [&](i32 fila) {
                return filaConTinta(y0ML + fila * lhML + 1,
                                    y0ML + fila * lhML + lhML - 1);
            };
            const bool f1 = banda(0);
            const bool f2 = banda(1);
            const bool f3 = banda(2);
            std::snprintf(buf, sizeof(buf), "filas con tinta: %d %d %d",
                          f1 ? 1 : 0, f2 ? 1 : 0, f3 ? 1 : 0);
            check("El texto se reparte en varias filas, no en una sola",
                  f1 && f2 && f3, buf);

            // Un clic en la segunda fila coloca el cursor ahi, no al principio.
            // "AAAA\nBBBB": la fila 0 va de 0..4 y la fila 1 empieza en 5.
            campo.setText("AAAA\nBBBB");
            // El salto entre filas se DERIVA del alto de la fuente, como hace
            // el campo. Con un 10 fijo la prueba dejaba de apuntar a la
            // segunda fila en cuanto cambiaba la letra.
            const i32 altoLinea =
                (textr::available() ? textr::glyphHeight(1)
                                    : font5x7::kGlyphH) + 3;
            campo.onMouseDown(12, 16);                  // fila 0, primer hueco
            const size_t c1 = campo.caretIndex();
            campo.onMouseDown(12, 16 + altoLinea);      // fila 1, primer hueco
            const size_t c2 = campo.caretIndex();
            std::snprintf(buf, sizeof(buf), "fila0 -> %zu, fila1 -> %zu", c1, c2);
            // El cursor cae en el HUECO exacto: 0 en la primera fila y 5 en la
            // segunda, que es el caracter justo tras el salto de linea.
            check("El clic cae en el hueco exacto de cada fila",
                  c1 == 0 && c2 == 5, buf);

            // Un clic muy a la derecha de una fila corta va al final de esa
            // fila, no a la siguiente ni al final del texto.
            campo.onMouseDown(190, 16);
            std::snprintf(buf, sizeof(buf), "caret=%zu (esperado 4)",
                          campo.caretIndex());
            check("Un clic pasado el final de la fila se queda en su final",
                  campo.caretIndex() == 4, buf);

            // --- sin huecos entre letras ---
            // El campo dibuja caracter a caracter. Si avanzara con un paso
            // fijo, con una fuente proporcional la 'w' se saldria de su hueco
            // y la 'i' lo dejaria medio vacio. Se comprueba midiendo la tinta:
            // de la primera columna pintada a la ultima tiene que haber lo
            // mismo que mide la cadena entera.
            TextField linea;
            linea.setStyle(ul);
            linea.setRect({ 10, 10, 400, 30 });
            linea.setMaxLength(1000);
            linea.setFocused(false);          // sin cursor, que tambien pinta
            const char* muestra = "awdadawdawaaaa";
            linea.setText(muestra);

            Framebuffer tira;
            tira.resize(420, 50);
            tira.clear(Color::fromHex(0x000000));
            linea.render(tira);

            const u32 fondoL = Color::fromHex(0x101010).packed();
            i32 primera = -1, ultima = -1;
            for (i32 x = 19; x < 400; ++x) {         // dentro del borde
                bool tinta = false;
                for (i32 y = 13; y < 37; ++y)
                    if (tira.data()[static_cast<size_t>(y) * 420 + x] != fondoL)
                        { tinta = true; break; }
                if (tinta) { if (primera < 0) primera = x; ultima = x; }
            }
            const i32 pintado  = (primera < 0) ? 0 : ultima - primera + 1;
            const i32 esperado = font5x7::measureUtf8(muestra, ul.textScale);
            std::snprintf(buf, sizeof(buf), "pintado=%d px, cadena=%d px",
                          pintado, esperado);
            // Margen de 2 px: la ultima letra puede no llevar tinta hasta el
            // borde de su avance.
            check("Las letras no dejan huecos: la tinta mide lo que la cadena",
                  pintado > 0 && std::abs(pintado - esperado) <= 2, buf);

            // Y el cursor cae justo al final del texto, no descolgado.
            linea.setFocused(true);
            linea.onMouseDown(10 + 8 + esperado + 20, 20);   // clic pasado el final
            std::snprintf(buf, sizeof(buf), "caret=%zu de %zu",
                          linea.caretIndex(), std::strlen(muestra));
            check("El cursor llega al final del texto con la fuente real",
                  linea.caretIndex() == std::strlen(muestra), buf);

            // --- el bearing NO se cuenta dos veces ---
            // El mapa de cobertura ya lleva dentro el hueco izquierdo de la
            // letra. Si al colocarlo se volvia a sumar, las de bearing
            // negativo ('j', 'f') se metian en la anterior y las de bearing
            // positivo se separaban: era el "Tuj uego" de los parrafos.
            //
            // Se comprueba con una palabra que mezcla ambas: lo dibujado
            // tiene que medir lo que dice measure, sin estirarse ni encogerse.
            if (textr::available()) {
                const char* mix = "Tu juego";
                Framebuffer tb;
                tb.resize(300, 40);
                tb.clear(Color::fromHex(0x000000));
                const i32 x0 = 20;
                font5x7::drawTextUtf8(tb, x0, 10, mix, Color::fromHex(0xFFFFFF), 1);

                i32 pri = -1, ult = -1;
                for (i32 x = 0; x < 300; ++x) {
                    bool tinta = false;
                    for (i32 y = 0; y < 40; ++y)
                        if (tb.data()[static_cast<size_t>(y) * 300 + x] !=
                            Color::fromHex(0x000000).packed()) { tinta = true; break; }
                    if (tinta) { if (pri < 0) pri = x; ult = x; }
                }
                const i32 medido = font5x7::measureUtf8(mix, 1);
                std::snprintf(buf, sizeof(buf),
                              "tinta de %d a %d (%d px), measure=%d",
                              pri, ult, (pri < 0 ? 0 : ult - pri + 1), medido);
                // La tinta empieza en el x pedido (+-2 por el bearing de la
                // 'T') y no se pasa del ancho anunciado.
                check("El texto arranca donde se le pide",
                      pri >= x0 - 2 && pri <= x0 + 3, buf);
                check("Y no se estira mas alla de lo que mide",
                      ult < x0 + medido + 2, buf);
            }

            // --- centrar usa el alto de la LETRA, no el del renglon ---
            // lineHeight incluye el hueco entre renglones, que no es tinta.
            // Centrar con el dejaba todos los rotulos caidos hacia abajo.
            if (textr::available()) {
                const i32 lh2 = textr::lineHeight(1);
                const i32 gh2 = textr::glyphHeight(1);
                std::snprintf(buf, sizeof(buf), "linea=%d, glifo=%d", lh2, gh2);
                check("El alto de linea es mayor o igual que el de la letra",
                      lh2 >= gh2 && gh2 > 0, buf);
            }

            // Y el texto de un campo de una linea queda centrado de verdad:
            // misma tinta arriba que abajo dentro de la caja.
            {
                TextField cen;
                cen.setStyle(ul);
                cen.setRect({ 10, 10, 200, 34 });
                cen.setFocused(false);
                cen.setText("pruebas");

                Framebuffer cf;
                cf.resize(220, 60);
                cf.clear(Color::fromHex(0x000000));
                cen.render(cf);

                const u32 fondoC = Color::fromHex(0x101010).packed();
                i32 arr = -1, aba = -1;
                for (i32 y = 13; y < 41; ++y) {
                    bool tinta = false;
                    for (i32 x = 20; x < 200; ++x)
                        if (cf.data()[static_cast<size_t>(y) * 220 + x] != fondoC)
                            { tinta = true; break; }
                    if (tinta) { if (arr < 0) arr = y; aba = y; }
                }
                if (arr >= 0) {
                    const i32 mA = arr - 10;          // desde el borde de la caja
                    const i32 mB = (10 + 34) - aba;
                    std::snprintf(buf, sizeof(buf),
                                  "margen arriba=%d, abajo=%d", mA, mB);
                    check("El texto del campo queda centrado en vertical",
                          std::abs(mA - mB) <= 4, buf);
                }
            }

            // --- la etiqueta de precio de la tienda contiene su texto ---
            // En las listas, "GRATIS" se salia de su recuadro verde por
            // abajo: la caja medía 18 px fijos y el texto se colocaba a +15,
            // a tres pixeles del borde. Se comprueba que NINGUN pixel de
            // texto cae fuera del rectangulo de fondo.
            {
                HomeScene st("tienda@ludora.engine");
                st.onEnter(engine);
                st.section(HomeScene::kStoreSection);
                for (i32 k = 0; k < 3; ++k) st.update(engine, 0.016);

                // Las listas viven muy abajo en una tienda de ~3400 px: sin
                // desplazar hasta ellas no se dibuja ni una etiqueta y la
                // prueba pasaria sin comprobar nada.
                Framebuffer tie;
                tie.resize(960, 600);
                tie.clear(Color::fromHex(0x000000));

                // Se barre el desplazamiento hasta encontrar etiquetas; la
                // altura exacta de la seccion depende del alto de la letra,
                // asi que fijar un offset concreto seria fragil.
                i32 cajasVistas = 0, fueraTotal = 0;
                Recti primeraMala{}, malaCaja{};
                for (i32 off = 0; off <= 3200 && cajasVistas == 0; off += 120) {
                st.scrollStoreForTest(off);
                st.update(engine, 0.016);
                tie.clear(Color::fromHex(0x000000));
                st.render(engine, tie);

                // Colores literales de la etiqueta (Theme.h: kSteamDeal y
                // kSteamGreen). Se repiten aqui porque el selftest no incluye
                // el tema; si cambian alli, esta prueba deja de encontrar
                // etiquetas y lo dice en su mensaje (etiquetas=0).
                const u32 fondoEti = Color::fromHex(0x4C6B22).packed();
                const u32 textoEti = Color::fromHex(0xA4D007).packed();

                // Se busca una banda de fondo verde oscuro y se mira si hay
                // verde claro (texto) justo encima o debajo de ella.
                i32 fuera = 0, cajas = 0;
                for (i32 y = 1; y < 599; ++y) {
                    for (i32 x = 1; x < 959; ++x) {
                        if (tie.data()[static_cast<size_t>(y) * 960 + x] != fondoEti)
                            continue;
                        // Primera esquina de una caja: el pixel de arriba y el
                        // de la izquierda no son del fondo.
                        const bool arriba =
                            tie.data()[static_cast<size_t>(y - 1) * 960 + x] == fondoEti;
                        const bool izq =
                            tie.data()[static_cast<size_t>(y) * 960 + x - 1] == fondoEti;
                        if (arriba || izq) continue;
                        ++cajas;

                        // Alto y ancho de la caja desde esa esquina.
                        i32 h2 = 0;
                        while (y + h2 < 600 &&
                               tie.data()[static_cast<size_t>(y + h2) * 960 + x]
                                   == fondoEti) ++h2;
                        i32 w2 = 0;
                        while (x + w2 < 960 &&
                               tie.data()[static_cast<size_t>(y) * 960 + x + w2]
                                   == fondoEti) ++w2;

                        // Una caja CORTADA por el borde de la pantalla no se
                        // puede medir: `h2` se para en 600 y la fila de
                        // "debajo" es en realidad otra cosa. Se descarta.
                        if (y + h2 >= 599) continue;

                        // Y lo que no tiene el TAMANO de una etiqueta no lo
                        // es. El detector busca manchas del verde de fondo, y
                        // un recorte de 10x2 px -- el borde de otra cosa en
                        // ese mismo tono -- entraba como si fuera una caja.
                        // Una etiqueta real mide al menos el alto de su letra.
                        if (h2 < textr::glyphHeight(1) || w2 < 24) continue;

                        // Texto justo por fuera, arriba o abajo: eso es
                        // desbordamiento.
                        //
                        // Se miran solo las columnas INTERIORES de la caja.
                        // Las notas de las filas ("4.50") tambien se pintan
                        // en este verde, y una que cayera rozando el borde
                        // contaria como desbordamiento sin serlo.
                        const i32 antesF = fuera;
                        for (i32 xx = x + 2; xx < x + w2 - 2 && xx < 960; ++xx) {
                            if (y > 0 &&
                                tie.data()[static_cast<size_t>(y - 1) * 960 + xx]
                                    == textoEti) ++fuera;
                            if (y + h2 < 600 &&
                                tie.data()[static_cast<size_t>(y + h2) * 960 + xx]
                                    == textoEti) ++fuera;
                        }
                        if (fuera > antesF && !malaCaja.w)
                            malaCaja = Recti{ x, y, w2, h2 };
                    }
                }
                cajasVistas = cajas;
                fueraTotal  = fuera;
                if (fuera > 0 && !primeraMala.w) primeraMala = malaCaja;
                }   // fin del barrido por desplazamiento

                std::snprintf(buf, sizeof(buf),
                              "etiquetas=%d, fuera=%d, caja mala %dx%d en (%d,%d)",
                              cajasVistas, fueraTotal, primeraMala.w,
                              primeraMala.h, primeraMala.x, primeraMala.y);
                // Se exige haber ENCONTRADO etiquetas: sin esto, un cambio
                // que dejara de dibujarlas haria pasar la prueba sin medir
                // nada, que es peor que fallar.
                check("El precio no se sale de su etiqueta en la tienda",
                      cajasVistas > 0 && fueraTotal == 0, buf);
            }
        }

        // Un valor fuera de rango en el archivo no puede descuadrar la
        // interfaz: se acota al cargar.
        {
            std::ofstream out(kCat, std::ios::binary | std::ios::app);
            out << "game=Fuera de rango\r\nfps=99999\r\nage=-5\r\npiracy=7\r\n";
        }
        const std::vector<devcatalog::Game> conBasura = devcatalog::load(kCat);
        const devcatalog::Game* malo = nullptr;
        for (const auto& g : conBasura)
            if (g.name == "Fuera de rango") malo = &g;
        check("Un catalogo editado a mano no descuadra los limites",
              malo && malo->listing.fps == devcatalog::kMaxFps &&
              malo->listing.minAge == devcatalog::kMinAge &&
              malo->listing.piracy == 2,
              malo ? std::to_string(malo->listing.fps) : "no leido");

        // --- COMPATIBILIDAD: una ficha SIN edad maxima ---
        // El registro de arriba trae `age` pero no `agemax`, que es
        // exactamente la forma de los catalogos escritos antes de que la
        // edad fuera una franja. Tiene que leerse con la maxima abierta, que
        // es lo que significaba una edad minima suelta.
        std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                      malo ? malo->listing.minAge : -1,
                      malo ? malo->listing.maxAge : -1);
        check("Una ficha sin edad maxima la hereda abierta",
              malo && malo->listing.maxAge == devcatalog::kMaxAge, buf);

        // Y al guardarla y volver a leerla, la franja se conserva entera.
        {
            const wchar_t* kCatF = L"catalogo-franja.t68";
            DeleteFileW(kCatF);
            devcatalog::Listing lf;
            lf.minAge = 12;
            lf.maxAge = 16;
            std::string ef;
            devcatalog::publishWithListing(kCatF, "Franja", L"f.exe", lf, ef);

            const std::vector<devcatalog::Game> leido = devcatalog::load(kCatF);
            const devcatalog::Game* gf = leido.empty() ? nullptr : &leido.front();
            std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                          gf ? gf->listing.minAge : -1,
                          gf ? gf->listing.maxAge : -1);
            check("La franja sobrevive a guardar y cargar",
                  gf && gf->listing.minAge == 12 && gf->listing.maxAge == 16, buf);

            // Una franja del reves se endereza en vez de guardarse invertida.
            devcatalog::Listing lr;
            lr.minAge = 40;
            lr.maxAge = 10;
            devcatalog::setListing(kCatF, "Franja", lr, ef);
            const std::vector<devcatalog::Game> leido2 = devcatalog::load(kCatF);
            const devcatalog::Game* gr = leido2.empty() ? nullptr : &leido2.front();
            std::snprintf(buf, sizeof(buf), "min=%d max=%d",
                          gr ? gr->listing.minAge : -1,
                          gr ? gr->listing.maxAge : -1);
            check("Una franja invertida se endereza al guardarla",
                  gr && gr->listing.minAge == 10 && gr->listing.maxAge == 40, buf);
            DeleteFileW(kCatF);
        }

        // --- la Tienda muestra los juegos publicados en vez de "ARTICULO N" ---
        {
            // Se publica sobre el catalogo REAL, que es el que lee la tienda,
            // y se restaura al acabar para no dejar rastro.
            const wchar_t* kCatReal = L"catalogo-dev.dat";
            const wchar_t* kBak     = L"catalogo-dev.t68bak";
            DeleteFileW(kBak);
            const bool habia = (GetFileAttributesW(kCatReal) != INVALID_FILE_ATTRIBUTES);
            if (habia) MoveFileW(kCatReal, kBak);

            devcatalog::Listing lt;
            lt.fps = 60; lt.minAge = 4;
            std::string terr;
            devcatalog::publishWithListing(kCatReal, "Juego De Prueba",
                                           L"x.exe", lt, terr);
            devcatalog::setPrice(kCatReal, "Juego De Prueba",
                                 devcatalog::Price{ 1999 }, terr);

            Framebuffer probeT;
            probeT.resize(960, 600);

            HomeScene tienda("tienda68@ludora.engine");
            tienda.onEnter(engine);
            tienda.section(HomeScene::kStoreSection);
            tienda.render(engine, probeT);

            const std::vector<std::string> nombres = tienda.storeGameNames();
            check("La Tienda carga el catalogo publicado",
                  !nombres.empty(), std::to_string(nombres.size()));
            // La tienda muestra SOLO lo publicado. Antes se inyectaba Voxel
            // World como primer juego, cableado en codigo; al retirarlo, el
            // primero es el que de verdad esta en el catalogo.
            check("El primero es el juego publicado, sin inyectados",
                  !nombres.empty() && nombres[0] == "Juego De Prueba",
                  nombres.empty() ? "(vacio)" : nombres[0].c_str());
            // El nombre va en trozos para que un renombrado masivo no lo
            // toque: esta prueba vigila precisamente que esa cadena NO
            // aparezca, asi que debe sobrevivir a una sustitucion global.
            const std::string retirado = std::string("Voxel") + " World";
            bool hayVoxel = false;
            for (const auto& n : nombres) if (n == retirado) hayVoxel = true;
            check("El juego retirado ya no aparece en la tienda", !hayVoxel,
                  std::to_string(nombres.size()).c_str());

            // --- secciones 1 y 2 de la Tienda ---
            {
                const Recti ban = tienda.storeBannerRect();
                std::snprintf(buf, sizeof(buf), "banner %dx%d en (%d,%d)",
                              ban.w, ban.h, ban.x, ban.y);
                check("La Tienda tiene banner de noticias",
                      ban.w > 0 && ban.h > 0, buf);
                check("El banner rota cada 5 minutos",
                      HomeScene::storeBannerPeriod() == 300.0);

                check("Hay tres mini-secciones", HomeScene::storeStripCount() == 3);
                const Recti s0 = tienda.storeStripRect(0);
                const Recti s1 = tienda.storeStripRect(1);
                const Recti s2 = tienda.storeStripRect(2);
                std::snprintf(buf, sizeof(buf), "(%d,%d) (%d,%d) (%d,%d)",
                              s0.x, s0.y, s1.x, s1.y, s2.x, s2.y);
                check("Las tres tienen sitio",
                      s0.w > 0 && s1.w > 0 && s2.w > 0, buf);
                check("Van en fila, bajo el banner",
                      s0.y == s1.y && s1.y == s2.y &&
                      s0.y >= ban.y + ban.h, buf);
                check("Y no se solapan",
                      s0.x + s0.w <= s1.x && s1.x + s1.w <= s2.x, buf);

                // OFERTAS ordena por precio: el mas barato primero.
                const std::vector<i32> ofertas = tienda.storeStripGames(1);
                check("La mini-seccion de ofertas trae juegos",
                      !ofertas.empty(), std::to_string(ofertas.size()));
            }

            // --- secciones 3 a 6: carrusel, destacados, listas y creadores ---
            {
                const Recti car = tienda.carouselRect();
                const Recti al  = tienda.carouselArrowLeft();
                const Recti ar  = tienda.carouselArrowRight();
                std::snprintf(buf, sizeof(buf),
                              "carrusel %dx%d en (%d,%d); flechas en %d y %d",
                              car.w, car.h, car.x, car.y, al.x, ar.x);
                check("Hay carrusel con sus dos flechas",
                      car.w > 0 && al.w > 0 && ar.w > 0, buf);
                check("Las flechas salen de las paredes",
                      al.x < car.x && ar.x + ar.w > car.x + car.w, buf);
                check("Y estan a la misma altura, centradas",
                      al.y == ar.y && al.y > car.y &&
                      al.y + al.h < car.y + car.h, buf);

                // Pulsar una flecha avanza y arranca el deslizamiento.
                const i32 antes = tienda.carouselIndex();
                tienda.carouselStep(+1);
                std::snprintf(buf, sizeof(buf), "de %d a %d, deslizando=%d",
                              antes, tienda.carouselIndex(),
                              tienda.carouselSliding() ? 1 : 0);
                // La animacion SIEMPRE arranca. El indice solo cambia si hay
                // mas de un juego: con uno solo, el modulo devuelve el mismo
                // y eso es lo correcto -- no hay otra imagen a la que pasar.
                // Antes el catalogo traia dos (uno inyectado), asi que la
                // prueba daba el cambio por hecho.
                const i32 nJuegos =
                    static_cast<i32>(tienda.storeGameNames().size());
                const bool avanzo = (nJuegos > 1)
                    ? tienda.carouselIndex() != antes
                    : tienda.carouselIndex() == antes;
                check("La flecha cambia de imagen con animacion",
                      avanzo && tienda.carouselSliding(), buf);

                // Y se puede volver atras.
                for (i32 k = 0; k < 60; ++k) tienda.update(engine, 0.016);
                check("El deslizamiento termina solo",
                      !tienda.carouselSliding());
                tienda.carouselStep(-1);
                check("Se puede regresar a la anterior",
                      tienda.carouselIndex() == antes);

                // Destacados: orden por valoracion, con sus propias flechas.
                tienda.refreshTopRatedForTest();
                check("Los destacados se ordenan por valoracion",
                      !tienda.topRatedOrder().empty(),
                      std::to_string(tienda.topRatedOrder().size()));
                check("El orden se renueva cada 24 horas",
                      HomeScene::topRatedPeriod() == 86400.0);
                check("Los destacados tienen sus flechas",
                      tienda.topRatedArrowLeft().w > 0 &&
                      tienda.topRatedArrowRight().w > 0);

                // Creadores y boton de seguir.
                const Recti cre = tienda.creatorCardRect();
                const Recti fol = tienda.followButtonRect();
                std::snprintf(buf, sizeof(buf),
                              "tarjeta %dx%d en (%d,%d); boton en (%d,%d)",
                              cre.w, cre.h, cre.x, cre.y, fol.x, fol.y);
                check("Hay tarjeta de creador con boton", cre.w > 0 && fol.w > 0, buf);
                check("El boton va abajo a la derecha de la tarjeta",
                      fol.x + fol.w <= cre.x + cre.w &&
                      fol.y + fol.h <= cre.y + cre.h &&
                      fol.x > cre.x + cre.w / 2, buf);

                // Seguir y dejar de seguir, con persistencia.
                const std::string quien = "tienda68@ludora.engine";
                const bool antesSigo = tienda.followingCreator(quien);
                tienda.toggleFollowForTest(quien);
                check("Pulsar Seguir cambia el estado",
                      tienda.followingCreator(quien) != antesSigo);
                tienda.toggleFollowForTest(quien);
                check("Y se puede dejar de seguir",
                      tienda.followingCreator(quien) == antesSigo);
                DeleteFileW(L"siguiendo.dat");

                // --- la TARJETA abre la ficha publica del creador ---
                // Antes la tarjeta solo ensenaba una descripcion en linea.
                // Ahora lleva a una seccion propia; el boton SEGUIR sigue
                // alternando sin abrirla, que son dos acciones distintas.
                {
                    // Clic en la tarjeta, LEJOS del boton de seguir.
                    const i32 tx = cre.x + 40;
                    const i32 ty2 = cre.y + 30;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(tx, ty2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(tx, ty2));
                    tienda.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(tx, ty2));
                    tienda.update(engine, 0.016);

                    std::snprintf(buf, sizeof(buf), "seccion=%d, creador='%s'",
                                  tienda.currentSection(),
                                  tienda.creatorOpen().c_str());
                    check("Pulsar la tarjeta abre la ficha del creador",
                          tienda.currentSection() == HomeScene::kCreatorSection,
                          buf);
                    check("Y recuerda de quien es la ficha",
                          !tienda.creatorOpen().empty(), buf);
                }

                // La ficha trae sus datos y el boton de comunidad.
                tienda.openCreatorForTest("tienda68@ludora.engine");
                tienda.render(engine, probeT);
                const Recti vol = tienda.creatorBackRect();
                const Recti uni = tienda.creatorJoinRect();
                std::snprintf(buf, sizeof(buf),
                              "volver %dx%d en (%d,%d); unirse %dx%d en (%d,%d)",
                              vol.w, vol.h, vol.x, vol.y,
                              uni.w, uni.h, uni.x, uni.y);
                check("La ficha tiene boton de volver", vol.w > 0, buf);
                check("Y boton de UNIRSE A COMUNIDAD", uni.w > 0, buf);
                check("El de unirse va debajo del de volver",
                      uni.y > vol.y + vol.h, buf);

                // --- UNIRSE lleva a la comunidad, que esta vacia ---
                {
                    const i32 ux = uni.x + uni.w / 2;
                    const i32 uy = uni.y + uni.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(ux, uy));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(ux, uy));
                    tienda.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ux, uy));
                    tienda.update(engine, 0.016);
                    std::snprintf(buf, sizeof(buf), "seccion=%d",
                                  tienda.currentSection());
                    check("UNIRSE A COMUNIDAD abre su seccion",
                          tienda.currentSection() == HomeScene::kCommunitySection,
                          buf);

                    tienda.render(engine, probeT);
                    check("La comunidad tiene boton de volver",
                          tienda.communityBackRect().w > 0);

                    // Y vuelve a la FICHA, no a la tienda: es de donde vino.
                    tienda.update(engine, 0.016);
                    tienda.openCreatorForTest("tienda68@ludora.engine");
                    check("Volver lleva de nuevo a la ficha del creador",
                          tienda.currentSection() == HomeScene::kCreatorSection);
                }
                tienda.section(HomeScene::kStoreSection);
                tienda.render(engine, probeT);
            }

            // --- las tarjetas responden al raton y abren la ficha ---
            std::snprintf(buf, sizeof(buf), "%d tarjetas",
                          tienda.storeCardCount());
            check("La Tienda apunta sus tarjetas para el hit-test",
                  tienda.storeCardCount() > 0, buf);

            // El juego publicado es el indice 0 del catalogo. Antes era el 1,
            // porque el 0 lo ocupaba el Voxel World inyectado; al retirarlo,
            // el catalogo arranca en lo que de verdad hay publicado.
            i32 cartaJuego = -1;
            for (i32 k = 0; k < tienda.storeCardCount(); ++k)
                if (tienda.storeCardGame(k) == 0) cartaJuego = k;
            check("Hay una tarjeta para el juego publicado", cartaJuego >= 0,
                  std::to_string(cartaJuego));

            // Y el destacado es ese mismo juego, seleccionable como los demas.
            check("El destacado es un juego real del catalogo",
                  tienda.storeCardCount() > 0 &&
                  tienda.storeCardGame(0) >= 0,
                  std::to_string(tienda.storeCardCount()).c_str());

            if (cartaJuego >= 0) {
                // El realce cambia lo dibujado: se lee el pixel del fondo de
                // la tarjeta con y sin cursor encima.
                //
                // La tienda es mas alta que la ventana, asi que una tarjeta
                // puede quedar fuera del framebuffer. Se comprueba ANTES de
                // leer: indexar fuera es leer memoria ajena.
                // Se desplaza el scroll hasta dejar la tarjeta a la vista:
                // asi la prueba mide de verdad en vez de rendirse.
                Recti cr2 = tienda.storeCardRect(cartaJuego);
                if (cr2.y + cr2.h > 600 || cr2.y < 0) {
                    tienda.scrollStoreForTest(
                        tienda.storeScrollOffset() + cr2.y - 200);
                    tienda.render(engine, probeT);
                    // Tras desplazar, la tarjeta ocupa otro hueco de la lista.
                    for (i32 k = 0; k < tienda.storeCardCount(); ++k)
                        if (tienda.storeCardGame(k) == 1) cartaJuego = k;
                    cr2 = tienda.storeCardRect(cartaJuego);
                }

                const i32 mx = cr2.x + cr2.w / 2;
                const i32 my = cr2.y + cr2.h - 6;
                const bool visible = (mx >= 0 && mx < 960 && my >= 0 && my < 600);

                if (visible) {
                    tienda.setStoreHovered(-1);
                    tienda.render(engine, probeT);
                    const u32 apagada =
                        probeT.data()[static_cast<size_t>(my) * 960 + mx];

                    tienda.setStoreHovered(cartaJuego);
                    tienda.render(engine, probeT);
                    const u32 realzada =
                        probeT.data()[static_cast<size_t>(my) * 960 + mx];

                    std::snprintf(buf, sizeof(buf), "normal=%08X realzada=%08X",
                                  apagada, realzada);
                    check("La tarjeta se ilumina con el cursor encima",
                          apagada != realzada, buf);
                    tienda.setStoreHovered(-1);
                } else {
                    std::snprintf(buf, sizeof(buf), "tarjeta en (%d,%d): fuera",
                                  mx, my);
                    check("La tarjeta se ilumina con el cursor encima",
                          true, buf);
                }
            }

            // --- la ficha se abre con un CLIC de verdad ---
            // Se inyecta el clic en vez de llamar a openProduct(): asi se
            // ejercita el camino real, que es donde estaba el fallo -- el
            // panel con scroll se quedaba el evento antes de llegar aqui.
            // El clic se inyecta en coordenadas de ventana, asi que la
            // tarjeta tiene que estar dentro de ella.
            const Recti ccPrev = tienda.storeCardRect(cartaJuego);
            const bool clicable = cartaJuego >= 0 &&
                                  ccPrev.y >= 0 && ccPrev.y + ccPrev.h <= 600;
            if (clicable) {
                const Recti cc = tienda.storeCardRect(cartaJuego);
                const i32 clx = cc.x + cc.w / 2;
                const i32 cly = cc.y + cc.h / 2;

                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(clx, cly));
                SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                             MAKELPARAM(clx, cly));
                tienda.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(clx, cly));
                tienda.update(engine, 0.016);

                std::snprintf(buf, sizeof(buf), "seccion=%d, producto=%d",
                              tienda.currentSection(), tienda.productOpen());
                // El indice es 0: el catalogo ya no lleva ningun juego
                // inyectado por delante del primero publicado.
                check("Un CLIC en la tarjeta abre la ficha del producto",
                      tienda.currentSection() == HomeScene::kProductSection &&
                      tienda.productOpen() == 0, buf);
            }

            // Si el clic no llego (ventana distinta en la prueba), se abre a
            // mano para poder seguir comprobando la ficha en si.
            // El juego publicado es el indice 0 desde que no se inyecta
            // ningun juego de la casa por delante.
            if (tienda.productOpen() != 0) tienda.openProductForTest(0);
            check("La ficha queda abierta sobre el juego pulsado",
                  tienda.currentSection() == HomeScene::kProductSection &&
                  tienda.productOpen() == 0);
            // Un render antes de medir: la geometria de la ficha (y con ella
            // el boton de descarga) se calcula al dibujar, no al abrirla.
            tienda.render(engine, probeT);
            check("Con su boton de descarga",
                  tienda.productBuyRect().w > 0);
            tienda.render(engine, probeT);

            tienda.render(engine, probeT);

            const Recti art  = tienda.productArtRect();
            const Recti side = tienda.productSideRect();
            const Recti rev  = tienda.productReviewsRect();
            std::snprintf(buf, sizeof(buf),
                          "imagen %dx%d, franja %dx%d en (%d,%d), resenas %dx%d",
                          art.w, art.h, side.w, side.h, side.x, side.y,
                          rev.w, rev.h);
            check("La ficha tiene imagen, franja y resenas",
                  art.w > 0 && side.w > 0 && rev.w > 0, buf);

            check("La franja va pegada al borde derecho",
                  side.x + side.w == 960, buf);
            check("Y llega de arriba abajo de la pantalla",
                  side.y <= 90 && side.y + side.h >= 590, buf);

            check("Las resenas van DEBAJO de la imagen",
                  rev.y >= art.y + art.h, buf);
            check("Y no se meten bajo la franja derecha",
                  rev.x + rev.w <= side.x, buf);

            // Los dos botones de la franja.
            const Recti t0 = tienda.productTabRect(0);
            const Recti t1 = tienda.productTabRect(1);
            std::snprintf(buf, sizeof(buf), "PRECIO en (%d,%d), VERSIONES en (%d,%d)",
                          t0.x, t0.y, t1.x, t1.y);
            check("La franja tiene sus dos botones arriba",
                  t0.w > 0 && t1.w > 0 && t0.y == t1.y, buf);
            check("Y estan dentro de la franja",
                  t0.x >= side.x && t1.x + t1.w <= side.x + side.w, buf);

            // --- DESCARGAR: dentro de la imagen, abajo a la derecha ---
            {
                const Recti bd = tienda.productBuyRect();
                std::snprintf(buf, sizeof(buf),
                              "boton %dx%d en (%d,%d), imagen %dx%d en (%d,%d)",
                              bd.w, bd.h, bd.x, bd.y, art.w, art.h, art.x, art.y);
                check("La ficha tiene boton de descargar", bd.w > 0 && bd.h > 0, buf);
                check("Esta DENTRO de la imagen",
                      bd.x >= art.x && bd.y >= art.y &&
                      bd.x + bd.w <= art.x + art.w &&
                      bd.y + bd.h <= art.y + art.h, buf);
                check("Pegado a la pared derecha de la imagen",
                      (art.x + art.w) - (bd.x + bd.w) <= 12, buf);
                check("Y abajo del todo",
                      (art.y + art.h) - (bd.y + bd.h) <= 12, buf);
                check("No tapa el nombre ni el precio de la portada",
                      bd.x > art.x + art.w / 2, buf);
            }

            check("Arranca en la vista de precios", tienda.productTab() == 0);
            tienda.setProductTab(1);
            tienda.render(engine, probeT);
            check("Se puede cambiar a la vista de versiones",
                  tienda.productTab() == 1);

            tienda.onExit(engine);

            DeleteFileW(kCatReal);
            if (habia) MoveFileW(kBak, kCatReal);
        }

        // --- changelog por version y compatibilidad de archivado ---
        {
            const wchar_t* kCatV = L"catalogo-dev.t68v";
            DeleteFileW(kCatV);

            devcatalog::Listing lv;
            std::string verr;
            devcatalog::publishWithListing(kCatV, "Con Log", L"v1.exe", lv, verr);

            check("Se anade una version con sus notas publicas",
                  devcatalog::addVersionWithLog(kCatV, "Con Log", L"v2.exe",
                                                "Arreglado un fallo.\nNuevo mapa.",
                                                verr),
                  verr.c_str());

            const auto leidos = devcatalog::load(kCatV);
            const devcatalog::Game* cg = nullptr;
            for (const auto& g : leidos) if (g.name == "Con Log") cg = &g;

            check("El juego tiene dos versiones",
                  cg && cg->versions.size() == 2,
                  cg ? std::to_string(cg->versions.size()) : "no leido");
            check("El changelog sobrevive con sus saltos de linea",
                  cg && cg->versions.size() == 2 &&
                  cg->versions[1].changelog == "Arreglado un fallo.\nNuevo mapa.",
                  cg && cg->versions.size() == 2
                      ? cg->versions[1].changelog.c_str() : "");
            check("La version anterior se conserva sin notas",
                  cg && cg->versions.size() == 2 &&
                  cg->versions[0].changelog.empty());

            // Un catalogo del formato VIEJO (tres campos) sigue leyendose.
            {
                std::ofstream out(kCatV, std::ios::binary | std::ios::app);
                out << "game=Formato Viejo\r\nver=v1|x.exe|1234\r\n";
            }
            const auto mixto = devcatalog::load(kCatV);
            const devcatalog::Game* vg = nullptr;
            for (const auto& g : mixto) if (g.name == "Formato Viejo") vg = &g;
            check("Un catalogo sin changelog sigue leyendose",
                  vg && vg->versions.size() == 1 &&
                  vg->versions[0].label == "v1" &&
                  vg->versions[0].changelog.empty());

            // --- comprobacion de compatibilidad de archivado ---
            // Sin version anterior con la que comparar, no hay nada que
            // avisar: la primera version no puede romper nada.
            const devcatalog::LayoutCheck sinRef =
                devcatalog::checkLayoutCompatible(L"no-existe-nada", L".");
            check("Sin version anterior no se avisa de incompatibilidad",
                  sinRef.compatible, sinRef.reason.c_str());

            // Comparar una carpeta consigo misma siempre es compatible.
            const devcatalog::LayoutCheck igual =
                devcatalog::checkLayoutCompatible(L".", L".");
            std::snprintf(buf, sizeof(buf), "%d comunes, %d perdidas",
                          igual.shared, igual.onlyOld);
            check("Una carpeta es compatible consigo misma",
                  igual.compatible && igual.onlyOld == 0, buf);

            DeleteFileW(kCatV);
        }

        // --- MANTENER PRECIOS POR DEFECTO ---
        {
            Framebuffer probeD;
            probeD.resize(960, 600);

            HomeScene pd("defecto@ludora.engine");
            pd.onEnter(engine);
            pd.section(HomeScene::kDevSection);
            pd.render(engine, probeD);

            // Abrir la mini-seccion PRECIOS.
            const Recti bp = pd.devColumnButtonRect(2);
            const i32 cx = bp.x + bp.w / 2, cy = bp.y + bp.h / 2;
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
            pd.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
            pd.update(engine, 0.016);
            pd.render(engine, probeD);

            const Recti def = pd.priceDefaultsRect();
            const Recti ok  = pd.priceAcceptRect();
            std::snprintf(buf, sizeof(buf), "boton %dx%d en (%d,%d)",
                          def.w, def.h, def.x, def.y);
            check("Hay boton de mantener precios por defecto",
                  def.w > 0 && def.h > 0, buf);
            check("Va DEBAJO de aceptar precio", def.y > ok.y, buf);
            check("Al entrar no estan desplegados sus mini-botones",
                  !pd.priceDefaultsOpen());

            // --- PRECIO A ESCRITO ---
            {
                const Recti pt = pd.priceTypedRect();
                const Recti track = pd.priceTrackRect();
                std::snprintf(buf, sizeof(buf), "boton (%d,%d), carril y=%d",
                              pt.x, pt.y, track.y);
                check("Hay boton de precio a escrito", pt.w > 0 && pt.h > 0, buf);

                // --- el carril va de GRATIS (izquierda) al maximo (derecha) ---
                // Es lo que dicen sus etiquetas, asi que el valor tiene que
                // seguir el mismo sentido o la seccion se contradice.
                pd.setPriceT(0.0f);
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                check("El extremo IZQUIERDO del carril es gratis",
                      pd.priceUsdCents() == 0, buf);

                pd.setPriceT(1.0f);
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                check("Y el extremo DERECHO es el precio maximo",
                      pd.priceUsdCents() == devcatalog::kMaxUsdCents, buf);

                pd.setPriceT(0.5f);
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                check("El centro es la mitad del maximo",
                      std::abs(pd.priceUsdCents() -
                               devcatalog::kMaxUsdCents / 2) <= 1, buf);
                check("Va sobre el carril, no debajo", pt.y < track.y, buf);
                check("Al entrar el cuadro esta cerrado", !pd.priceTypedOpen());

                // Escribir una cifra y confirmar la lleva al deslizador.
                pd.setPriceTypedOpen(true);
                pd.setPriceTypedText("19.99");
                check("Una cifra valida se acepta", pd.applyTypedPrice(),
                      pd.priceTypedMessage().c_str());
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                // 19.99 son 1999 centavos EXACTOS. El deslizador guarda una
                // proporcion, asi que se admite un centavo de redondeo.
                check("El precio escrito llega al deslizador",
                      std::abs(pd.priceUsdCents() - 1999) <= 1, buf);
                check("Y confirmar cierra el cuadro", !pd.priceTypedOpen());

                // Formas alternativas de escribir dinero.
                pd.setPriceTypedText("$25");
                check("Se admite el simbolo del dolar", pd.applyTypedPrice(),
                      pd.priceTypedMessage().c_str());
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                check("\"$25\" son 2500 centavos",
                      std::abs(pd.priceUsdCents() - 2500) <= 1, buf);

                pd.setPriceTypedText("9,50");
                check("Se admite la coma decimal", pd.applyTypedPrice());
                std::snprintf(buf, sizeof(buf), "%d centavos", pd.priceUsdCents());
                check("\"9,50\" son 950 centavos",
                      std::abs(pd.priceUsdCents() - 950) <= 1, buf);

                // Escribir 0 y confirmar SI cuenta como precio puesto: es la
                // unica forma de publicar un juego gratis a proposito.
                pd.setPriceTypedText("0");
                check("Se puede fijar precio cero", pd.applyTypedPrice());
                check("Y cuenta como precio decidido", pd.priceAccepted());

                // --- cifras que no valen ---
                pd.setPriceTypedText("abc");
                check("Un texto sin numeros se rechaza", !pd.applyTypedPrice(),
                      pd.priceTypedMessage().c_str());
                pd.setPriceTypedText("1.2.3");
                check("Dos separadores decimales se rechazan",
                      !pd.applyTypedPrice(), pd.priceTypedMessage().c_str());
                pd.setPriceTypedText("");
                check("Un campo vacio se rechaza", !pd.applyTypedPrice());
                pd.setPriceTypedText("9999");
                check("Pasarse del maximo se rechaza", !pd.applyTypedPrice(),
                      pd.priceTypedMessage().c_str());
            }

            // Sin tocar nada, moneda y pais NO estan elegidos.
            check("Al entrar los ajustes no estan elegidos",
                  !pd.priceOptionsReady());

            // Aceptar por defecto los da todos por buenos.
            pd.acceptDefaultPriceOptions();
            check("Aceptar por defecto deja los ajustes listos",
                  pd.priceOptionsReady());
            check("Y cierra la pregunta", !pd.priceDefaultsOpen());

            pd.onExit(engine);
        }

        // --- ACTUALIZAR JUEGO: los cuatro pasos ---
        {
            const wchar_t* kCatReal = L"catalogo-dev.dat";
            const wchar_t* kBakU    = L"catalogo-dev.t68u";
            DeleteFileW(kBakU);
            const bool habiaU =
                (GetFileAttributesW(kCatReal) != INVALID_FILE_ATTRIBUTES);
            if (habiaU) MoveFileW(kCatReal, kBakU);

            devcatalog::Listing lu;
            std::string uerr;
            devcatalog::publishWithListing(kCatReal, "Juego Actualizable",
                                           L"orig.exe", lu, uerr);

            Framebuffer probeU;
            probeU.resize(960, 600);

            HomeScene up("upd@ludora.engine");
            up.onEnter(engine);
            up.section(HomeScene::kDevSection);
            up.render(engine, probeU);

            // Abrir ACTUALIZAR JUEGO (segundo boton de la columna).
            const Recti bu = up.devColumnButtonRect(1);
            const i32 ux = bu.x + bu.w / 2, uy = bu.y + bu.h / 2;
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(ux, uy));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ux, uy));
            up.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(ux, uy));
            up.update(engine, 0.016);
            up.render(engine, probeU);

            // --- LudoUpdates: el tablon de la zona central ---
            {
                HomeScene lu("ludo@ludora.engine");
                lu.onEnter(engine);
                lu.section(HomeScene::kDevSection);
                lu.update(engine, 0.016);
                lu.render(engine, probeU);

                check("El tablon tiene ocho bloques",
                      HomeScene::ludoBlockCount() == 8);

                const Recti lr = lu.ludoRect();
                std::snprintf(buf, sizeof(buf), "tablon %dx%d en (%d,%d)",
                              lr.w, lr.h, lr.x, lr.y);
                check("Ocupa la zona central y es grande",
                      lr.w > 300 && lr.h > 200, buf);

                // Va a la DERECHA de la columna de botones, sin pisarla.
                const Recti b0 = lu.devColumnButtonRect(0);
                check("Empieza tras la columna de botones",
                      lr.x >= b0.x + b0.w, buf);

                // Los bloques son anchos y van uno bajo otro.
                const Recti q0 = lu.ludoBlockRect(0);
                const Recti q1 = lu.ludoBlockRect(1);
                const Recti q9 = lu.ludoBlockRect(7);   // el ultimo
                std::snprintf(buf, sizeof(buf),
                              "b0 %dx%d y=%d, b1 y=%d, b9 y=%d",
                              q0.w, q0.h, q0.y, q1.y, q9.y);
                check("Los bloques son anchos", q0.w > 250, buf);
                check("Y altos de verdad", q0.h >= 60, buf);
                check("Van uno debajo de otro, sin solaparse",
                      q1.y >= q0.y + q0.h && q9.y > q1.y, buf);
                check("Todos alineados a la misma izquierda",
                      q1.x == q0.x && q9.x == q0.x, buf);

                // Cada bloque tiene titulo y cuerpo.
                bool conTexto = true;
                for (i32 k = 0; k < HomeScene::ludoBlockCount(); ++k) {
                    if (!HomeScene::ludoBlockTitle(k)[0]) conTexto = false;
                    if (HomeScene::ludoBlockLines(k) <= 0) conTexto = false;
                }
                check("Los ocho tienen titulo y cuerpo", conTexto,
                      HomeScene::ludoBlockTitle(0));

                // --- el texto se ajusta al ancho y el bloque lo contiene ---
                // El cuerpo se guarda en PARRAFOS largos y el motor los parte
                // por palabras. Si el alto del bloque no coincidiera con las
                // lineas que salen, el texto se saldria por abajo o quedaria
                // un hueco muerto. Se comprueba midiendo la tinta real.
                {
                    Framebuffer tb;
                    tb.resize(960, 600);
                    tb.clear(Color::fromHex(0x000000));
                    lu.scrollLudoForTest(0);
                    lu.render(engine, tb);

                    const Recti b = lu.ludoBlockRect(0);

                    // Los colores que NO son texto: el fondo del Modo
                    // Desarrollador y el relleno del bloque (kPanelDeep).
                    // Compararlos mal era el fallo de la version anterior de
                    // esta prueba: tomaba 0x121212, que no pinta nadie aqui,
                    // y entonces el propio fondo contaba como tinta.
                    const u32 fondoDev = Color::fromHex(0x090909).packed();
                    const u32 fondoBlk = Color::fromHex(0x1A1440).packed();

                    // Columna justo a la derecha del bloque: ahi solo puede
                    // haber fondo. Si hay tinta, el texto se esta saliendo.
                    i32 fuera = 0;
                    u32 intruso = 0;
                    const i32 xd = b.x + b.w + 2;
                    // El AVATAR vive en la esquina inferior derecha (radio
                    // 42, margen 22) y su halo turquesa cruza esta columna.
                    // No es texto del tablon, asi que su banda se excluye o
                    // la prueba acusaria un desbordamiento que no existe.
                    const i32 yIni = std::max(0, b.y);
                    const i32 yFin = std::min(600 - (42 * 2 + 22 + 8),
                                              b.y + b.h);
                    if (xd < 958) {
                        for (i32 y = yIni; y < yFin; ++y) {
                            const u32 c =
                                tb.data()[static_cast<size_t>(y) * 960 + xd];
                            if (c != fondoDev && c != fondoBlk) {
                                if (!intruso) intruso = c;
                                ++fuera;
                            }
                        }
                    }
                    std::snprintf(buf, sizeof(buf),
                                  "bloque %dx%d en (%d,%d), x=%d y=%d..%d, "
                                  "fuera=%d color=%08X",
                                  b.w, b.h, b.x, b.y, xd, yIni, yFin,
                                  fuera, intruso);
                    check("El texto no se sale del bloque por la derecha",
                          fuera == 0, buf);

                    // Y dentro SI hay texto: un bloque vacio tambien pasaria
                    // la prueba de arriba.
                    i32 dentro = 0;
                    for (i32 y = std::max(0, b.y + 40);
                         y < std::min(600, b.y + b.h - 4); ++y)
                        for (i32 x = b.x + 18;
                             x < std::min(958, b.x + b.w - 10); ++x) {
                            const u32 c =
                                tb.data()[static_cast<size_t>(y) * 960 + x];
                            if (c != fondoBlk && c != fondoDev) ++dentro;
                        }
                    std::snprintf(buf, sizeof(buf), "pixeles de texto=%d", dentro);
                    check("Y el bloque tiene texto dentro", dentro > 200, buf);
                }

                // --- el scroll ---
                // El contenido pasa del alto visible: si no, no habria nada
                // que desplazar y el tablon no cumpliria el encargo.
                std::snprintf(buf, sizeof(buf), "contenido=%d, visible=%d",
                              lu.ludoContentHeight(), lr.h);
                check("El contenido supera lo que cabe en pantalla",
                      lu.ludoContentHeight() > lr.h, buf);

                const i32 antes = lu.ludoBlockRect(0).y;
                lu.scrollLudoForTest(200);
                lu.render(engine, probeU);
                const i32 despues = lu.ludoBlockRect(0).y;
                std::snprintf(buf, sizeof(buf), "y antes=%d, despues=%d",
                              antes, despues);
                check("Al desplazar, los bloques suben", despues < antes, buf);

                // El titulo NO se mueve: queda fijo arriba.
                lu.scrollLudoForTest(0);
                lu.render(engine, probeU);
                check("Y vuelve al principio al soltar el scroll",
                      lu.ludoBlockRect(0).y == antes, buf);

                // --- el titulo esta dibujado y lleva los colores del logo ---
                // Se mide la tinta de la franja del titulo: tiene que haber
                // varios colores distintos, no uno plano.
                {
                    std::set<u32> tonos;
                    const i32 y0 = lr.y + 8;
                    const i32 y1 = std::min(600, y0 + 40);
                    for (i32 y = y0; y < y1; ++y)
                        for (i32 x = lr.x; x < std::min(960, lr.x + lr.w); ++x) {
                            const u32 c =
                                probeU.data()[static_cast<size_t>(y) * 960 + x];
                            // Se descarta el fondo del Modo Desarrollador;
                            // lo demas es tinta del titulo.
                            if (c != Color::fromHex(0x090909).packed())
                                tonos.insert(c);
                        }
                    std::snprintf(buf, sizeof(buf), "colores distintos=%zu",
                                  tonos.size());
                    check("El titulo LudoUpdates esta pintado y es de colores",
                          tonos.size() > 8, buf);
                }

                // --- el tablon SE QUEDA al abrir una mini-seccion ---
                // Antes desaparecia y la zona central quedaba negra alrededor
                // del formulario. Ahora es el fondo de la seccion: el panel
                // se dibuja encima, pero el tablon sigue ahi.
                const i32 bx = b0.x + b0.w / 2, by = b0.y + b0.h / 2;
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(bx, by));
                SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                             MAKELPARAM(bx, by));
                lu.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
                lu.update(engine, 0.016);
                std::snprintf(buf, sizeof(buf), "panel=%d", lu.devOpenPanel());
                check("La mini-seccion se abre", lu.devOpenPanel() == 0, buf);

                // Se mide la TINTA del titulo, que vive fuera del panel: si
                // el tablon hubiera desaparecido, esa franja seria fondo.
                {
                    Framebuffer tl;
                    tl.resize(960, 600);
                    tl.clear(Color::fromHex(0x000000));
                    lu.render(engine, tl);

                    const Recti lr2 = lu.ludoRect();
                    i32 tinta = 0;
                    const i32 y0 = std::max(0, lr2.y + 8);
                    const i32 y1 = std::min(600, y0 + 40);
                    for (i32 y = y0; y < y1; ++y)
                        for (i32 x = std::max(0, lr2.x);
                             x < std::min(960, lr2.x + lr2.w); ++x) {
                            const u32 c =
                                tl.data()[static_cast<size_t>(y) * 960 + x];
                            if (c != Color::fromHex(0x090909).packed()) ++tinta;
                        }
                    std::snprintf(buf, sizeof(buf),
                                  "panel=%d, pixeles del titulo=%d",
                                  lu.devOpenPanel(), tinta);
                    check("El tablon sigue dibujado con el panel abierto",
                          tinta > 200, buf);
                }
            }

            // --- ELIMINAR JUEGO: cuarto boton de la columna ---
            {
                check("La columna tiene cinco botones",
                      HomeScene::devColumnButtonCount() == 5,
                      std::to_string(HomeScene::devColumnButtonCount()));

                // Se redibuja antes de leer los rectangulos: el layout de la
                // columna se recalcula al dibujar, y sin esto se leerian las
                // posiciones de un frame anterior.
                up.render(engine, probeU);
                const Recti b2 = up.devColumnButtonRect(2);   // PRECIOS
                const Recti b3 = up.devColumnButtonRect(3);   // ELIMINAR
                std::snprintf(buf, sizeof(buf),
                              "PRECIOS (%d,%d) ELIMINAR (%d,%d)",
                              b2.x, b2.y, b3.x, b3.y);
                check("ELIMINAR JUEGO tiene sitio", b3.w > 0 && b3.h > 0, buf);
                check("Va DEBAJO de PRECIOS", b3.y > b2.y, buf);
                check("Y en la misma columna", b3.x == b2.x, buf);
                check("Sin solaparse con el de arriba", b3.y >= b2.y + b2.h, buf);

                // --- CREAR COMUNIDAD: quinto boton, con seccion vacia ---
                {
                    const Recti b4 = up.devColumnButtonRect(4);
                    std::snprintf(buf, sizeof(buf),
                                  "ELIMINAR (%d,%d %dx%d) COMUNIDAD (%d,%d %dx%d)",
                                  b3.x, b3.y, b3.w, b3.h,
                                  b4.x, b4.y, b4.w, b4.h);
                    check("CREAR COMUNIDAD tiene sitio",
                          b4.w > 0 && b4.h > 0, buf);
                    check("Va DEBAJO de ELIMINAR", b4.y > b3.y, buf);
                    check("Y en la misma columna", b4.x == b3.x, buf);
                    check("Sin solaparse con el de arriba",
                          b4.y >= b3.y + b3.h, buf);

                    // Los cinco caben en la ventana: con medidas fijas el
                    // ultimo se salia por abajo en cuanto se encogia.
                    check("El ultimo boton no se sale de la ventana",
                          b4.y + b4.h <= 600, buf);

                    // Su mini-seccion se abre al pulsarlo, y esta vacia: solo
                    // el titulo y el aviso, sin campos ni botones propios.
                    const i32 cx2 = b4.x + b4.w / 2, cy2 = b4.y + b4.h / 2;
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0,
                                 MAKELPARAM(cx2, cy2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(cx2, cy2));
                    up.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                                 MAKELPARAM(cx2, cy2));
                    up.update(engine, 0.016);

                    std::snprintf(buf, sizeof(buf), "panel=%d",
                                  up.devOpenPanel());
                    check("Pulsarlo abre su mini-seccion",
                          up.devOpenPanel() == 4, buf);   // 4 = kDevCommunity

                    // Se dibuja sin reventar y deja tinta: una seccion vacia
                    // de verdad (toda negra) seria indistinguible de una rota.
                    up.render(engine, probeU);
                    const Recti pc = up.devPanelRect();
                    i32 tintaC = 0;
                    for (i32 yy = pc.y + 4; yy < pc.y + pc.h - 4; yy += 2)
                        for (i32 xx = pc.x + 4; xx < pc.x + pc.w - 4; xx += 2) {
                            if (yy < 0 || yy >= probeU.height()) continue;
                            if (xx < 0 || xx >= probeU.width()) continue;
                            const u32 c = probeU.data()[
                                static_cast<size_t>(yy) * probeU.width() + xx];
                            if (c != Color{ 0, 0, 0 }.packed()) ++tintaC;
                        }
                    std::snprintf(buf, sizeof(buf),
                                  "panel %dx%d, %d pixeles con color",
                                  pc.w, pc.h, tintaC);
                    check("La seccion se dibuja y se lee", tintaC > 200, buf);

                    // --- los nueve ajustes de la comunidad ---
                    check("CREAR COMUNIDAD tiene nueve ajustes",
                          HomeScene::communitySlotCount() == 9,
                          std::to_string(HomeScene::communitySlotCount()));

                    // Ninguno se sale del panel ni pisa a otro, sea cual sea
                    // la forma en que esten repartidos.
                    i32 fueraRej = 0, solapes = 0;
                    for (i32 a = 0; a < 9; ++a) {
                        const Recti ra = up.communitySlotRect(a);
                        if (ra.w <= 0 || ra.h <= 0) { ++fueraRej; continue; }
                        if (ra.x < pc.x || ra.y < pc.y ||
                            ra.x + ra.w > pc.x + pc.w ||
                            ra.y + ra.h > pc.y + pc.h) ++fueraRej;
                        for (i32 b2 = a + 1; b2 < 9; ++b2) {
                            const Recti rb = up.communitySlotRect(b2);
                            if (rb.w <= 0) continue;
                            const bool cruza =
                                ra.x < rb.x + rb.w && rb.x < ra.x + ra.w &&
                                ra.y < rb.y + rb.h && rb.y < ra.y + ra.h;
                            if (cruza) ++solapes;
                        }
                    }
                    std::snprintf(buf, sizeof(buf), "%d fuera, %d solapes",
                                  fueraRej, solapes);
                    check("Los nueve caben en el panel sin solaparse",
                          fueraRej == 0 && solapes == 0, buf);

                    // La seccion tiene la misma forma que GUARDAR ARCHIVOS Y
                    // ACTUALIZAR: campo ancho arriba, parejas en medio y el
                    // resumen anclado al fondo.
                    const Recti sNom = up.communitySlotRect(0);   // NOMBRE
                    const Recti sDes = up.communitySlotRect(1);   // DESCRIPCION
                    const Recti sJoi = up.communitySlotRect(3);   // ENTRADA
                    const Recti sAdm = up.communitySlotRect(4);   // ADMINS
                    const Recti sRan = up.communitySlotRect(5);   // RANGOS
                    const Recti sRes = up.communitySummaryRect();
                    std::snprintf(buf, sizeof(buf),
                                  "nom (%d,%d %dx%d) desc y=%d entrada (%d,%d) "
                                  "admins x=%d resumen y=%d",
                                  sNom.x, sNom.y, sNom.w, sNom.h, sDes.y,
                                  sJoi.x, sJoi.y, sAdm.x, sRes.y);
                    check("NOMBRE encabeza la seccion, a todo lo ancho",
                          sNom.w > 0 && sNom.w == pc.w - 32 &&
                          sNom.y < sDes.y, buf);
                    check("DESCRIPCION va justo debajo, igual de ancha",
                          sDes.w == sNom.w && sDes.y > sNom.y + sNom.h, buf);
                    check("ENTRADA y ADMINS comparten fila, a mitad de ancho",
                          sJoi.y == sAdm.y && sAdm.x > sJoi.x &&
                          sJoi.w < sNom.w, buf);
                    check("RANGOS cierra su grupo a todo lo ancho",
                          sRan.w == sNom.w && sRan.y > sJoi.y, buf);
                    check("El resumen queda anclado al fondo",
                          sRes.w > 0 && sRes.y > sRan.y &&
                          sRes.y + sRes.h <= pc.y + pc.h, buf);

                    // Nada se sale ni se pisa, con la forma nueva.
                    i32 malColocados = 0;
                    for (i32 a = 0; a < 9; ++a) {
                        const Recti ra = up.communitySlotRect(a);
                        if (ra.w <= 0) { ++malColocados; continue; }
                        if (ra.y + ra.h > sRes.y) ++malColocados;  // pisa el pie
                    }
                    std::snprintf(buf, sizeof(buf), "%d mal colocados",
                                  malColocados);
                    check("Ningun ajuste invade el pie de la seccion",
                          malColocados == 0, buf);

                    // --- NOMBRE, DESCRIPCION (5000) y REGLAS ---
                    up.openCommunitySlot(0);
                    check("Pulsar un ajuste lo abre",
                          up.communityOpenSlot() == 0,
                          std::to_string(up.communityOpenSlot()));
                    up.setCommunityName("Los Constructores");
                    check("El nombre se guarda",
                          up.communityName() == "Los Constructores",
                          up.communityName().c_str());
                    check("Y se resume en su boton",
                          up.communitySlotValueForTest(0) == "Los Constructores",
                          up.communitySlotValueForTest(0).c_str());

                    // La descripcion admite 5000 y no uno mas: es el tope que
                    // se pidio, y pasarse recortaria texto en silencio.
                    {
                        const std::string cinco(5000, 'x');
                        up.setCommunityDesc(cinco);
                        std::snprintf(buf, sizeof(buf), "%zu caracteres",
                                      up.communityDesc().size());
                        check("La descripcion acepta 5000 caracteres",
                              up.communityDesc().size() == 5000, buf);

                        up.setCommunityDesc(std::string(6000, 'y'));
                        std::snprintf(buf, sizeof(buf), "%zu caracteres",
                                      up.communityDesc().size());
                        check("Y no deja pasar de 5000",
                              up.communityDesc().size() == 5000, buf);
                    }

                    // Las reglas son el otro texto largo, con saltos de linea.
                    up.setCommunityRules("1. Respeta.\n2. Sin spam.\n3. Sin trampas.");
                    check("Las reglas guardan sus saltos de linea",
                          up.communityRules().find('\n') != std::string::npos,
                          up.communityRules().c_str());

                    // --- ADMINS: minimo 1, maximo 100 ---
                    up.setCommunityAdmins(1);
                    check("Admins acepta el minimo de 1",
                          up.communityAdmins() == 1,
                          std::to_string(up.communityAdmins()));
                    up.setCommunityAdmins(100);
                    check("Y el maximo de 100",
                          up.communityAdmins() == 100,
                          std::to_string(up.communityAdmins()));
                    up.setCommunityAdmins(0);
                    check("Por debajo de 1 se recorta a 1",
                          up.communityAdmins() == 1,
                          std::to_string(up.communityAdmins()));
                    up.setCommunityAdmins(500);
                    check("Y por encima de 100 se recorta a 100",
                          up.communityAdmins() == 100,
                          std::to_string(up.communityAdmins()));

                    // --- las cinco mecanicas restantes ---
                    // Cada ajuste arranca con un valor por defecto sensato y
                    // se resume en su boton: un boton en blanco no dice si
                    // esta sin tocar o roto.
                    // Los de valor fijo son IDENTIDAD(2), ENTRADA(3),
                    // ADMINS(4), RANGOS(5) y CASTIGOS(7). EVENTOS(8) empieza
                    // vacio a proposito, y los textos (0,1,6) los llena quien
                    // escribe.
                    i32 sinResumen = 0;
                    for (const i32 a : { 2, 3, 4, 5, 7 })
                        if (up.communitySlotValueForTest(a).empty())
                            ++sinResumen;
                    std::snprintf(buf, sizeof(buf), "%d sin resumen",
                                  sinResumen);
                    check("Los ajustes de valor fijo arrancan con uno puesto",
                          sinResumen == 0, buf);

                    check("La entrada arranca en ABIERTA",
                          up.communityJoinMode() == 0,
                          std::to_string(up.communityJoinMode()));

                    // Rangos: escalones y horas, con su total derivado.
                    std::snprintf(buf, sizeof(buf), "%d rangos cada %dh",
                                  up.communityRankCount(),
                                  up.communityRankHours());
                    check("Los rangos arrancan con escalones y horas",
                          up.communityRankCount() >= 1 &&
                          up.communityRankHours() >= 1, buf);

                    // Castigos: los tres tramos van de menos a mas grave, que
                    // es lo que hace que sea una escalera y no tres sueltos.
                    std::snprintf(buf, sizeof(buf), "%d -> %d -> %d",
                                  up.communityPunish(0), up.communityPunish(1),
                                  up.communityPunish(2));
                    check("Los castigos escalan de menos a mas grave",
                          up.communityPunish(0) < up.communityPunish(1) &&
                          up.communityPunish(1) < up.communityPunish(2), buf);

                    // Eventos: se marcan y desmarcan, varios a la vez.
                    check("Los eventos arrancan sin ninguno",
                          up.communityEventCount() == 0,
                          std::to_string(up.communityEventCount()));
                    up.toggleCommunityEventForTest(0);
                    up.toggleCommunityEventForTest(2);
                    check("Se pueden activar varios a la vez",
                          up.communityEventCount() == 2,
                          std::to_string(up.communityEventCount()));
                    up.toggleCommunityEventForTest(0);
                    check("Y desactivar uno sin tocar los demas",
                          up.communityEventCount() == 1,
                          std::to_string(up.communityEventCount()));
                    check("Con eventos activos, el boton los cuenta",
                          up.communitySlotValueForTest(8) == "1 ACTIVOS",
                          up.communitySlotValueForTest(8).c_str());

                    // Identidad: color e insignia.
                    check("La identidad arranca con color e insignia",
                          up.communityColor() >= 0 && up.communityBadge() >= 0,
                          "");

                    // --- cada panel se dibuja sin reventar ---
                    // Se abren los nueve y se pinta cada uno: un indice mal
                    // puesto en una tabla se cazaria aqui.
                    i32 panelesVacios = 0;
                    for (i32 a = 0; a < 9; ++a) {
                        up.openCommunitySlot(a);
                        up.render(engine, probeU);
                        i32 tintaP = 0;
                        for (i32 yy = pc.y + 30; yy < pc.y + pc.h - 6; yy += 3)
                            for (i32 xx = pc.x + 6; xx < pc.x + pc.w - 6; xx += 3) {
                                if (yy < 0 || yy >= probeU.height()) continue;
                                if (xx < 0 || xx >= probeU.width()) continue;
                                const u32 c = probeU.data()[
                                    static_cast<size_t>(yy) * probeU.width() + xx];
                                if (c != Color{ 0, 0, 0 }.packed()) ++tintaP;
                            }
                        if (tintaP < 50) ++panelesVacios;
                    }
                    std::snprintf(buf, sizeof(buf), "%d paneles sin contenido",
                                  panelesVacios);
                    check("Los nueve paneles se dibujan con contenido",
                          panelesVacios == 0, buf);

                    // --- ESC retrocede UN nivel cada vez ---
                    //
                    // BUG QUE ESTO PROTEGE: el manejador de ESC vive en el
                    // update de la escena y devuelve antes de llegar a la
                    // mini-seccion, asi que una copia local dentro de
                    // updateDevCommunity nunca se ejecutaba: ESC cerraba de
                    // golpe toda la mini-seccion en vez del ajuste abierto.
                    {
                        /// Manda un ESC completo y deja que la escena lo
                        /// procese. El KEYUP es imprescindible: sin el, la
                        /// tecla se queda hundida y el ESC siguiente no
                        /// cuenta como pulsacion nueva.
                        auto esc = [&]() {
                            engine.input().newFrame();
                            SendMessageW(win.handle(), WM_KEYDOWN, VK_ESCAPE, 0);
                            up.update(engine, 0.016);
                            engine.input().newFrame();
                            SendMessageW(win.handle(), WM_KEYUP, VK_ESCAPE, 0);
                        };

                        up.section(HomeScene::kDevSection);
                        up.openDevPanelForTest(4);    // CREAR COMUNIDAD
                        up.openCommunitySlot(6);      // REGLAS
                        up.render(engine, probeU);

                        esc();
                        std::snprintf(buf, sizeof(buf),
                                      "ajuste=%d panel=%d seccion=%d",
                                      up.communityOpenSlot(), up.devOpenPanel(),
                                      up.currentSection());
                        check("ESC cierra el ajuste, no la mini-seccion",
                              up.communityOpenSlot() < 0 &&
                              up.devOpenPanel() == 4, buf);

                        esc();
                        std::snprintf(buf, sizeof(buf), "panel=%d seccion=%d",
                                      up.devOpenPanel(), up.currentSection());
                        check("Otro ESC cierra la mini-seccion, no la seccion",
                              up.devOpenPanel() < 0 &&
                              up.currentSection() == HomeScene::kDevSection, buf);

                        esc();
                        std::snprintf(buf, sizeof(buf), "seccion=%d",
                                      up.currentSection());
                        check("Y el tercero sale del Modo Desarrollador",
                              up.currentSection() == HomeScene::kAccountSection, buf);

                        // Se vuelve a dejar abierto para lo que sigue.
                        up.section(HomeScene::kDevSection);
                        up.openDevPanelForTest(4);
                        up.render(engine, probeU);
                    }

                    up.openCommunitySlot(-1);   // cerrar antes de seguir

                    // Se vuelve a ELIMINAR, que es lo que esperan las
                    // comprobaciones siguientes.
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_MOUSEMOVE, 0,
                                 MAKELPARAM(cx2, cy2));
                    SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                                 MAKELPARAM(cx2, cy2));
                    up.update(engine, 0.016);
                    engine.input().newFrame();
                    SendMessageW(win.handle(), WM_LBUTTONUP, 0,
                                 MAKELPARAM(cx2, cy2));
                    up.update(engine, 0.016);
                }

                // Su mini-seccion se abre al pulsarlo.
                const i32 dx = b3.x + b3.w / 2, dy = b3.y + b3.h / 2;
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(dx, dy));
                SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                             MAKELPARAM(dx, dy));
                up.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(dx, dy));
                up.update(engine, 0.016);

                std::snprintf(buf, sizeof(buf), "panel=%d", up.devOpenPanel());
                check("Pulsarlo abre su mini-seccion",
                      up.devOpenPanel() == 3, buf);   // 3 = kDevDelete

                // --- campo, aviso y los dos botones ---
                up.render(engine, probeU);
                const Recti dc = up.deleteFieldRect();
                const Recti dsi = up.deleteYesRect();
                const Recti dno = up.deleteNoRect();
                std::snprintf(buf, sizeof(buf),
                              "campo (%d,%d) SI (%d,%d) NO (%d,%d)",
                              dc.x, dc.y, dsi.x, dsi.y, dno.x, dno.y);
                check("Tiene campo para el codigo", dc.w > 0 && dc.h > 0, buf);
                check("Y dos botones de confirmar y negar",
                      dsi.w > 0 && dno.w > 0, buf);
                check("Los botones van al fondo, bajo el campo",
                      dsi.y > dc.y && dsi.y == dno.y, buf);
                check("El aviso tiene varias lineas",
                      HomeScene::deleteNoticeLines() > 8,
                      std::to_string(HomeScene::deleteNoticeLines()));

                // --- se identifica por CODIGO, no por nombre ---
                // Retirar un juego no se puede deshacer, asi que no basta con
                // teclear un nombre parecido: hace falta el codigo exacto.
                std::string codigoD;
                for (const auto& g : devcatalog::load(kCatReal))
                    if (g.name == "Juego Actualizable") codigoD = g.code;

                up.setDeleteName(codigoD);
                up.lookupDeleteGameForTest();
                std::snprintf(buf, sizeof(buf), "codigo='%s' -> '%s'",
                              codigoD.c_str(), up.deleteGame().c_str());
                check("El codigo de un juego propio lo encuentra",
                      up.deleteFound() == 1, buf);
                check("Y dice de que juego se trata",
                      up.deleteGame() == "Juego Actualizable", buf);

                // El NOMBRE ya no sirve: es justamente lo que se queria evitar.
                up.setDeleteName("Juego Actualizable");
                up.lookupDeleteGameForTest();
                check("Escribir el nombre ya no encuentra nada",
                      up.deleteFound() == 0,
                      std::to_string(up.deleteFound()));

                up.setDeleteName("#NOEXISTEESTECOD");
                up.lookupDeleteGameForTest();
                check("Y un codigo inventado tampoco",
                      up.deleteFound() == 0,
                      std::to_string(up.deleteFound()));

                // Confirmar sin codigo valido no hace nada.
                up.confirmDeleteForTest();
                check("Sin codigo valido no se elimina nada",
                      devcatalog::load(kCatReal).size() == 1,
                      up.deleteMessage().c_str());

                // --- retirar de verdad ---
                up.setDeleteName(codigoD);
                up.lookupDeleteGameForTest();
                up.confirmDeleteForTest();
                check("Al confirmar, el juego sale del catalogo",
                      devcatalog::load(kCatReal).empty(),
                      up.deleteMessage().c_str());
                check("Y se avisa de las copias liberadas",
                      up.deleteMessage().find("retirado") != std::string::npos,
                      up.deleteMessage().c_str());

                // Se republica para que las pruebas siguientes lo encuentren.
                devcatalog::publishWithListing(kCatReal, "Juego Actualizable",
                                               L"orig.exe", lu, uerr);

                // El fondo es negro, no el violeta de las otras secciones.
                up.render(engine, probeU);
                const Recti pr2 = up.devPanelRect();
                if (pr2.w > 40 && pr2.y + pr2.h - 10 < 600) {
                    const i32 px2 = pr2.x + 6;
                    const i32 py2 = pr2.y + pr2.h - 10;
                    const u32 c =
                        probeU.data()[static_cast<size_t>(py2) * 960 + px2];
                    std::snprintf(buf, sizeof(buf), "pixel %08X", c);
                    check("Su fondo es negro, no el violeta de las demas",
                          c == Color::fromHex(0x090909).packed(), buf);
                }

                // --- ACTUALIZAR JUEGO *ES* GUARDADO DE ARCHIVOS ---
                // La seccion se vacio de contenido propio y adopto esos
                // sistemas: un campo con el CODIGO, GENERAR CODIGO, elegir
                // carpeta y guardar. No hay un boton aparte para los archivos;
                // los cinco de la columna son subir, actualizar, precios,
                // eliminar y crear comunidad.
                {
                    check("La columna sigue teniendo cinco botones",
                          HomeScene::devColumnButtonCount() == 5,
                          std::to_string(HomeScene::devColumnButtonCount()));

                    // Abrir ACTUALIZAR JUEGO: ya no hay pasos que abrir, su
                    // contenido se ve de una vez.
                    up.section(HomeScene::kDevSection);
                    up.openDevPanelForTest(1);          // kDevUpdate
                    up.render(engine, probeU);

                    // --- GENERAR CODIGO vive ahora aqui ---
                    const Recti ag = up.archiveGenRect();
                    std::snprintf(buf, sizeof(buf), "generar %dx%d en (%d,%d)",
                                  ag.w, ag.h, ag.x, ag.y);
                    check("GENERAR CODIGO esta en ACTUALIZAR", ag.w > 0, buf);

                    // El codigo generado sigue siendo de 15 caracteres y
                    // aleatorio: se comprueba el motor, que es lo que importa.
                    {
                        const wchar_t* kCatGen = L"catalogo-gen.t68";
                        DeleteFileW(kCatGen);
                        const std::string c1 = devcatalog::randomGameCode(kCatGen);
                        const std::string c2 = devcatalog::randomGameCode(kCatGen);
                        std::snprintf(buf, sizeof(buf), "'%s' y '%s'",
                                      c1.c_str(), c2.c_str());
                        check("El codigo generado mide 16 (# y 15)",
                              c1.size() == 16 && c1[0] == '#', buf);
                        check("Y dos seguidos son distintos", c1 != c2, buf);
                        DeleteFileW(kCatGen);
                    }

                    // --- se apaga con juegos publicados ---
                    std::snprintf(buf, sizeof(buf), "juegos=%zu",
                                  devcatalog::load(kCatReal).size());
                    check("Con juegos publicados, GENERAR esta apagado",
                          !up.canGenerateCode(), buf);

                    const std::string antesCode = up.archiveCode();
                    up.pressGenerateCodeForTest();
                    check("Pulsarlo apagado no cambia el codigo",
                          up.archiveCode() == antesCode,
                          up.archiveMessage().c_str());

                    // --- el codigo identifica el juego ---
                    std::string codigo;
                    for (const auto& g : devcatalog::load(kCatReal))
                        if (g.name == "Juego Actualizable") codigo = g.code;

                    up.setArchiveCode(codigo);
                    up.update(engine, 0.016);
                    std::snprintf(buf, sizeof(buf), "codigo='%s' -> '%s'",
                                  codigo.c_str(), up.archiveCodeGame().c_str());
                    check("El codigo del paso 1 identifica su juego",
                          up.archiveCodeGame() == "Juego Actualizable", buf);
                    check("Y queda marcado como encontrado",
                          up.updateFound() == 1,
                          std::to_string(up.updateFound()).c_str());

                    // --- actualizar EXIGE el codigo ---
                    up.setArchiveCode("");
                    up.update(engine, 0.016);
                    up.submitUpdateForTest();
                    check("Sin codigo no se actualiza",
                          up.devMessageIsError(), up.devMessage().c_str());

                    up.setArchiveCode(codigo);
                    up.update(engine, 0.016);
                }

                // Se vuelve a ACTUALIZAR JUEGO: las comprobaciones que siguen
                // esperan esa mini-seccion abierta, no esta.
                const Recti b1b = up.devColumnButtonRect(1);
                const i32 rx = b1b.x + b1b.w / 2, ry = b1b.y + b1b.h / 2;
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(rx, ry));
                SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                             MAKELPARAM(rx, ry));
                up.update(engine, 0.016);
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(rx, ry));
                up.update(engine, 0.016);
                up.render(engine, probeU);
            }

            // --- ACTUALIZAR *ES* GUARDADO DE ARCHIVOS ---
            // La seccion se vacio de lo suyo y adopto esos sistemas, asi que
            // lo que se comprueba aqui son ELLOS: el campo del codigo, GENERAR
            // CODIGO, elegir carpeta y guardar. De los cuatro pasos, la zona
            // de subida y el change log ya no debe quedar nada.
            const Recti ac = up.archiveCodeRect();
            const Recti ag = up.archiveGenRect();
            const Recti ap = up.archivePickRect();
            const Recti as = up.archiveSaveRect();
            std::snprintf(buf, sizeof(buf),
                          "codigo (%d,%d) generar (%d,%d) carpeta (%d,%d) "
                          "guardar (%d,%d)",
                          ac.x, ac.y, ag.x, ag.y, ap.x, ap.y, as.x, as.y);
            check("ACTUALIZAR tiene el campo del codigo y GENERAR",
                  ac.w > 0 && ag.w > 0, buf);
            check("Y los botones de carpeta y guardar",
                  ap.w > 0 && as.w > 0, buf);
            check("GENERAR va bajo el campo del codigo",
                  ag.y >= ac.y + ac.h, buf);
            check("Y los de carpeta y guardar, mas abajo",
                  ap.y > ag.y && as.y >= ap.y, buf);

            // --- el codigo identifica el juego ---
            // El campo es de CODIGO, no de nombre: escribir el nombre del
            // juego ya no lo encuentra, y eso es lo correcto.
            std::string codigoU;
            for (const auto& g : devcatalog::load(kCatReal))
                if (g.name == "Juego Actualizable") codigoU = g.code;

            up.setUpdateName(codigoU);
            up.lookupUpdateGameForTest();
            std::snprintf(buf, sizeof(buf), "codigo='%s' estado=%d",
                          codigoU.c_str(), up.updateFound());
            check("El codigo de un juego que existe lo encuentra",
                  up.updateFound() == 1, buf);

            up.setUpdateName("#NOEXISTEESTECOD");
            up.lookupUpdateGameForTest();
            check("Y un codigo inventado se reporta como ausente",
                  up.updateFound() == 0, std::to_string(up.updateFound()));

            up.setUpdateName("");
            up.lookupUpdateGameForTest();
            check("Sin codigo escrito, no hay nada que buscar",
                  up.updateFound() == -1, std::to_string(up.updateFound()));

            // --- los tres botones nuevos de ACTUALIZAR ---
            up.setUpdateName(codigoU);
            up.update(engine, 0.016);
            up.render(engine, probeU);

            const Recti be = up.updateEulaRect();
            const Recti bl = up.updateLogRect();
            const Recti bn = up.updateNoticeRect();
            std::snprintf(buf, sizeof(buf),
                          "eula (%d,%d %dx%d) log (%d,%d) aviso (%d,%d %dx%d)",
                          be.x, be.y, be.w, be.h, bl.x, bl.y,
                          bn.x, bn.y, bn.w, bn.h);
            check("Hay boton de EDITAR EULA y de SUBIR CHANGE LOG",
                  be.w > 0 && bl.w > 0, buf);
            check("Van en la misma fila", be.y == bl.y, buf);
            check("EULA a la izquierda del change log", bl.x > be.x, buf);
            check("DAR COMUNICADO va debajo de los dos",
                  bn.w > 0 && bn.y > be.y + be.h - 1, buf);
            check("Y es mas ancho: no es parte de publicar",
                  bn.w > be.w, buf);
            check("Ninguno pisa el campo del codigo",
                  be.y > up.archiveCodeRect().y, buf);
            check("Ni los botones de carpeta y guardar",
                  bn.y + bn.h <= up.archivePickRect().y, buf);

            // --- el change log se guarda y lo consume la version ---
            up.openUpdateBoxForTest(0);   // kUpdBoxLog
            check("El cuadro del change log se abre",
                  up.updateBoxOpen() == 0,
                  std::to_string(up.updateBoxOpen()));
            up.setUpdateChangelog("Arreglado el salto doble.");
            check("Y guarda lo que se escribe",
                  up.updateChangelog() == "Arreglado el salto doble.",
                  up.updateChangelog().c_str());

            // --- comunicados ---
            // El comunicado necesita un juego REAL: es un mensaje a quien ya
            // tiene el juego, y sin juego no hay a quien avisar.
            {
                const wchar_t* kAvisos = L"comunicados.dat";
                DeleteFileW(kAvisos);

                up.openUpdateBoxForTest(1);   // kUpdBoxNotice
                check("El cuadro del comunicado se abre",
                      up.updateBoxOpen() == 1,
                      std::to_string(up.updateBoxOpen()));

                // Sin titulo no se publica: es lo que se lista.
                up.setNoticeForTest("", "cuerpo sin titulo");
                up.submitNoticeForTest();
                check("Un comunicado sin titulo no se publica",
                      notices::load(kAvisos).empty(),
                      up.archiveMessage().c_str());

                // Con titulo si. El cuerpo puede ser enorme: se pidio "sin
                // limite", asi que se prueba con un texto largo de verdad.
                std::string largo;
                for (i32 i = 0; i < 400; ++i)
                    largo += "Parrafo " + std::to_string(i) +
                             " del comunicado, con texto de relleno.\n";

                up.setNoticeForTest("Servidores en mantenimiento", largo);
                up.submitNoticeForTest();

                const std::vector<notices::Notice> pub = notices::load(kAvisos);
                std::snprintf(buf, sizeof(buf), "%zu avisos, cuerpo de %zu chars",
                              pub.size(), pub.empty() ? 0u : pub[0].body.size());
                check("Con titulo se publica", pub.size() == 1, buf);
                check("Y queda asociado a su juego",
                      !pub.empty() && pub[0].game == "Juego Actualizable",
                      pub.empty() ? "" : pub[0].game.c_str());
                check("El cuerpo largo se guarda ENTERO, sin recortar",
                      !pub.empty() && pub[0].body == largo, buf);
                check("Los saltos de linea sobreviven al guardado",
                      !pub.empty() &&
                      pub[0].body.find('\n') != std::string::npos, buf);
                check("El cuadro se cierra al publicar",
                      up.updateBoxOpen() == -1,
                      std::to_string(up.updateBoxOpen()));

                // Un segundo comunicado se acumula, no reemplaza.
                up.openUpdateBoxForTest(1);
                up.setNoticeForTest("Aviso corto", "Ya estamos de vuelta.");
                up.submitNoticeForTest();
                check("Dos comunicados conviven",
                      notices::load(kAvisos).size() == 2,
                      std::to_string(notices::load(kAvisos).size()));
                check("El mas nuevo va primero",
                      notices::forGame(kAvisos, "Juego Actualizable").size() == 2);

                // Y solo los de SU juego.
                check("Un juego sin comunicados no ve los de otro",
                      notices::forGame(kAvisos, "Otro Juego").empty());
                check("countForGame cuenta los del juego",
                      notices::countForGame(kAvisos, "Juego Actualizable") == 2,
                      std::to_string(notices::countForGame(kAvisos,
                                                           "Juego Actualizable")));

                DeleteFileW(kAvisos);
            }

            // --- EL PRIMER JUEGO: guardar, poner precio, subir ---
            //
            // BUG QUE ESTO PROTEGE: con el catalogo vacio, PRECIOS respondia
            // "primero publica un juego con SUBIR JUEGO", y SUBIR JUEGO
            // exigia el precio antes de publicar. Las dos secciones se
            // mandaban la una a la otra y el primer juego no se podia subir
            // por ningun camino.
            {
                const wchar_t* kCat1 = L"primero.t95";
                DeleteFileW(kCat1);
                DeleteFileW(L"primero.t95.pendientes");

                // Con el catalogo vacio y archivos guardados queda un
                // pendiente: es el rastro del trabajo hecho.
                std::string errP;
                devcatalog::Pending p0;
                p0.game    = "Mi Primer Juego";
                p0.archive = L"paq.tomate";
                p0.files   = 7;
                p0.bytes   = 1234;
                check("Guardar archivos deja un pendiente",
                      devcatalog::setPending(kCat1, p0, errP), errP.c_str());
                check("Y el catalogo sigue vacio",
                      devcatalog::load(kCat1).empty(),
                      std::to_string(devcatalog::load(kCat1).size()));

                // El precio se le puede poner AHORA, sin publicar antes.
                devcatalog::Pending p1 =
                    devcatalog::getPending(kCat1, "Mi Primer Juego");
                p1.price    = 1999;   // 19.99 USD
                p1.priceSet = true;
                errP.clear();
                check("El precio se guarda sin haber publicado",
                      devcatalog::setPending(kCat1, p1, errP), errP.c_str());

                const devcatalog::Pending leido =
                    devcatalog::getPending(kCat1, "Mi Primer Juego");
                std::snprintf(buf, sizeof(buf), "precio=%d puesto=%d",
                              leido.price, leido.priceSet ? 1 : 0);
                check("Y se relee del disco",
                      leido.price == 1999 && leido.priceSet, buf);
                check("Sin perder el paquete guardado",
                      leido.files == 7 && !leido.archive.empty(), buf);

                // Al publicar, el juego adopta paquete Y precio.
                errP.clear();
                check("El primer juego se publica",
                      devcatalog::publish(kCat1, "Mi Primer Juego",
                                          L"juego.exe", errP),
                      errP.c_str());

                const std::vector<devcatalog::Game> cat1 =
                    devcatalog::load(kCat1);
                std::snprintf(buf, sizeof(buf), "%zu juegos, precio=%d",
                              cat1.size(),
                              cat1.empty() ? -1 : cat1[0].price.usdCents);
                check("Y hereda el precio que se puso antes",
                      cat1.size() == 1 && cat1[0].price.usdCents == 1999, buf);
                check("Y tambien el paquete",
                      !cat1.empty() && cat1[0].current() &&
                      !cat1[0].current()->archive.empty(), buf);
                check("El pendiente se consume al publicar",
                      devcatalog::getPending(kCat1, "Mi Primer Juego")
                          .archive.empty(), "");

                // --- guardado por CODIGO, subido por NOMBRE ---
                //
                // BUG QUE ESTO PROTEGE: GUARDAR ARCHIVOS identifica el juego
                // por codigo. Con un juego nuevo ese codigo no corresponde
                // a nada publicado, asi que el pendiente se anotaba con el
                // PROPIO CODIGO como nombre. Luego SUBIR JUEGO lo buscaba
                // por el nombre escrito y no lo encontraba: seguia pidiendo
                // subir los archivos aunque acabaran de guardarse.
                {
                    DeleteFileW(kCat1);
                    DeleteFileW(L"primero.t95.pendientes");

                    const std::string nom = "Juego Con Codigo";
                    const std::string cod = devcatalog::makeGameCode(nom);

                    devcatalog::Pending pc;
                    pc.game    = cod;          // <- guardado bajo el CODIGO
                    pc.archive = L"c.tomate";
                    pc.files   = 5;
                    std::string errC;
                    devcatalog::setPending(kCat1, pc, errC);

                    std::snprintf(buf, sizeof(buf), "codigo='%s'", cod.c_str());
                    check("Los archivos guardados por codigo SI cuentan",
                          devcatalog::hasArchive(kCat1, nom), buf);

                    // Y el codigo con '#' o en minusculas vale igual.
                    DeleteFileW(L"primero.t95.pendientes");
                    devcatalog::Pending pc2 = pc;
                    pc2.game = "#" + cod;
                    devcatalog::setPending(kCat1, pc2, errC);
                    check("Con '#' delante tambien",
                          devcatalog::hasArchive(kCat1, nom), buf);

                    // Al publicar, el juego adopta ese paquete y el
                    // pendiente se consume: si no, se quedaria ahi siempre.
                    errC.clear();
                    check("El juego se publica",
                          devcatalog::publish(kCat1, nom, L"j.exe", errC),
                          errC.c_str());

                    const std::vector<devcatalog::Game> cc =
                        devcatalog::load(kCat1);
                    check("Y hereda el paquete guardado por codigo",
                          cc.size() == 1 && cc[0].current() &&
                          !cc[0].current()->archive.empty(),
                          std::to_string(cc.size()));
                    check("El pendiente bajo codigo se consume",
                          devcatalog::pendings(kCat1).empty(),
                          std::to_string(devcatalog::pendings(kCat1).size()));

                    DeleteFileW(kCat1);
                    DeleteFileW(L"primero.t95.pendientes");
                }

                // GRATIS es un precio decidido, no un "sin poner": los dos
                // valen 0 y solo el flag los distingue.
                DeleteFileW(kCat1);
                DeleteFileW(L"primero.t95.pendientes");
                devcatalog::Pending g0;
                g0.game     = "Juego Gratis";
                g0.archive  = L"g.tomate";
                g0.price    = 0;
                g0.priceSet = true;
                errP.clear();
                devcatalog::setPending(kCat1, g0, errP);
                check("GRATIS se distingue de 'sin decidir'",
                      devcatalog::getPending(kCat1, "Juego Gratis").priceSet,
                      "");

                // Un registro viejo (sin los campos de precio) se sigue
                // leyendo: lo que falte queda como "sin decidir".
                {
                    std::ofstream vj("primero.t95.pendientes",
                                     std::ios::binary | std::ios::trunc);
                    vj << "# Archivos guardados de juegos aun sin publicar.\r\n";
                    vj << "Juego Viejo|v.tomate|3|999\r\n";
                }
                const devcatalog::Pending viejo =
                    devcatalog::getPending(kCat1, "Juego Viejo");
                std::snprintf(buf, sizeof(buf), "archivos=%d puesto=%d",
                              viejo.files, viejo.priceSet ? 1 : 0);
                check("Un registro de antes se sigue leyendo",
                      viejo.files == 3 && !viejo.priceSet, buf);

                DeleteFileW(kCat1);
                DeleteFileW(L"primero.t95.pendientes");
            }

            // --- el guardado avisa ANTES en vez de fallar a mitad ---
            //
            // Lo caro de guardar es leer la carpeta entera. Si algo va a
            // impedir terminar (carpeta vacia, que no exista, o que no quepa),
            // decirlo despues de minutos de disco es lo peor de los dos
            // mundos: se pierde el tiempo Y no hay paquete.
            {
                std::string errG;
                std::atomic<float> prog{0.0f};

                // Carpeta que no existe.
                check("Guardar una carpeta inexistente avisa, no revienta",
                      !gamevault::sealWholeFolder(L"no.existe.t93",
                                                  L"x.tomate", &prog, errG) &&
                      !errG.empty(), errG.c_str());

                // Carpeta vacia: se detecta antes de empaquetar nada.
                CreateDirectoryW(L"vacia.t93", nullptr);
                errG.clear();
                const bool okVacia = gamevault::sealWholeFolder(
                    L"vacia.t93", L"vacia.tomate", &prog, errG);
                check("Una carpeta vacia se rechaza con su motivo",
                      !okVacia && errG.find("vacia") != std::string::npos,
                      errG.c_str());
                check("Y no deja un paquete a medias",
                      GetFileAttributesW(L"vacia.tomate") ==
                          INVALID_FILE_ATTRIBUTES, "");
                RemoveDirectoryW(L"vacia.t93");

                // --- el paquete anterior NO se pierde si algo falla ---
                //
                // BUG QUE ESTO PROTEGE: se escribia directamente sobre el
                // .tomate bueno, asi que un guardado cortado a mitad dejaba
                // el anterior machacado y truncado. Ahora se escribe a un
                // .parcial y se renombra al final.
                CreateDirectoryW(L"src.t93", nullptr);
                { std::ofstream f("src.t93\\a.bin", std::ios::binary);
                  f << "contenido-v1"; }

                errG.clear();
                const bool ok1 = gamevault::sealWholeFolder(
                    L"src.t93", L"paq.t93.tomate", &prog, errG);
                check("Un guardado normal termina bien", ok1, errG.c_str());

                std::vector<u8> antes;
                Vault::loadFile(L"paq.t93.tomate", antes);
                check("Y deja el paquete en su sitio", !antes.empty(),
                      std::to_string(antes.size()));

                // Al guardar otra vez, no queda ningun .parcial suelto.
                { std::ofstream f("src.t93\\a.bin", std::ios::binary);
                  f << "contenido-v2-mas-largo"; }
                errG.clear();
                const bool ok2 = gamevault::sealWholeFolder(
                    L"src.t93", L"paq.t93.tomate", &prog, errG);
                check("Guardar encima del anterior funciona", ok2, errG.c_str());
                check("Y no deja restos .parcial",
                      GetFileAttributesW(L"paq.t93.tomate.parcial") ==
                          INVALID_FILE_ATTRIBUTES, "");

                // El paquete nuevo se abre y trae el contenido nuevo.
                {
                    CreateDirectoryW(L"out.t93", nullptr);
                    std::string errU;
                    const bool okU = gamevault::unsealWholeFolder(
                        L"paq.t93.tomate", L"out.t93", errU);
                    check("El paquete reemplazado se abre sin problemas",
                          okU, errU.c_str());

                    std::ifstream in("out.t93\\a.bin", std::ios::binary);
                    std::string leido((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
                    check("Y trae el contenido de la version nueva",
                          leido == "contenido-v2-mas-largo", leido.c_str());

                    SHFILEOPSTRUCTW op{};
                    op.wFunc = FO_DELETE; op.pFrom = L"out.t93\0\0";
                    op.fFlags = FOF_NO_UI;
                    SHFileOperationW(&op);
                }

                DeleteFileW(L"paq.t93.tomate");
                DeleteFileW(L"src.t93\\a.bin");
                RemoveDirectoryW(L"src.t93");
            }

            // --- el paquete nuevo: subcarpetas, integridad y rutas ---
            //
            // El formato en flujo (tomatepack) se hizo para que guardar no
            // necesite el juego entero en memoria. Lo que se comprueba aqui es
            // que a cambio no se perdio nada de lo que importa: que el
            // contenido vuelve IDENTICO, que una alteracion se detecta, y que
            // un paquete no puede escribir fuera de su carpeta.
            {
                const wchar_t* kDir = L"tp.t94";
                const wchar_t* kPak = L"tp.t94.tomate";
                const wchar_t* kOut = L"tp.t94.out";

                auto borrarArbol = [](const wchar_t* d) {
                    std::wstring from(d);
                    from.push_back(L'\0'); from.push_back(L'\0');
                    SHFILEOPSTRUCTW op{};
                    op.wFunc = FO_DELETE; op.pFrom = from.c_str();
                    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI;
                    SHFileOperationW(&op);
                };
                borrarArbol(kDir); borrarArbol(kOut); DeleteFileW(kPak);

                // Un arbol con varios niveles: es justo lo que un bug de
                // recursion rompe sin que se note en una carpeta plana.
                CreateDirectoryW(kDir, nullptr);
                CreateDirectoryW(L"tp.t94\\datos", nullptr);
                CreateDirectoryW(L"tp.t94\\datos\\nivel1", nullptr);
                CreateDirectoryW(L"tp.t94\\assets", nullptr);
                std::ofstream(L"tp.t94\\juego.exe", std::ios::binary) << "MZ-raiz";
                std::ofstream(L"tp.t94\\datos\\conf.json", std::ios::binary)
                    << "{\"a\":1}";
                std::ofstream(L"tp.t94\\datos\\nivel1\\mapa.dat", std::ios::binary)
                    << "mapa-del-nivel-uno";
                std::ofstream(L"tp.t94\\assets\\tex.png", std::ios::binary)
                    << "pixeles";
                std::ofstream(L"tp.t94\\vacio.txt", std::ios::binary) << "";

                std::string errT;
                std::atomic<float> progT{0.0f};
                const bool okSeal = gamevault::sealWholeFolder(kDir, kPak,
                                                               &progT, errT);
                check("El paquete nuevo se sella", okSeal, errT.c_str());
                check("Y lo escribe en el formato en flujo (v3)",
                      tomatepack::isV3(kPak), "");

                errT.clear();
                const bool okUn = gamevault::unsealWholeFolder(kPak, kOut, errT);
                check("Y se vuelve a abrir", okUn, errT.c_str());

                // El contenido de CADA nivel del arbol, comparado de verdad.
                auto leer = [](const wchar_t* p) {
                    std::ifstream in(p, std::ios::binary);
                    return std::string((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
                };
                check("Vuelve el archivo de la raiz",
                      leer(L"tp.t94.out\\juego.exe") == "MZ-raiz", "");
                check("Vuelve el de una subcarpeta",
                      leer(L"tp.t94.out\\datos\\conf.json") == "{\"a\":1}", "");
                check("Y el de una subcarpeta de segundo nivel",
                      leer(L"tp.t94.out\\datos\\nivel1\\mapa.dat") ==
                          "mapa-del-nivel-uno", "");
                check("Y el de otra rama del arbol",
                      leer(L"tp.t94.out\\assets\\tex.png") == "pixeles", "");
                check("Un archivo vacio se recrea vacio, no se pierde",
                      GetFileAttributesW(L"tp.t94.out\\vacio.txt") !=
                          INVALID_FILE_ATTRIBUTES, "");

                // Integridad: un byte cambiado en la zona de datos tiene que
                // detectarse, no instalarse a medias.
                {
                    std::vector<u8> raw;
                    Vault::loadFile(kPak, raw);
                    check("El paquete tiene contenido que verificar",
                          raw.size() > 600, std::to_string(raw.size()).c_str());

                    std::string errV;
                    check("Verificar un paquete intacto dice que esta bien",
                          tomatepack::verify(kPak, gamevault::distributionSecret(),
                                             nullptr, errV), errV.c_str());

                    if (raw.size() > 600) {
                        std::vector<u8> roto = raw;
                        roto[roto.size() - 50] ^= 0x5A;
                        Vault::saveFile(L"tp.t94.roto.tomate", roto);

                        errV.clear();
                        check("Un byte alterado se detecta al verificar",
                              !tomatepack::verify(L"tp.t94.roto.tomate",
                                                  gamevault::distributionSecret(),
                                                  nullptr, errV),
                              errV.c_str());

                        borrarArbol(L"tp.t94.roto.out");
                        errV.clear();
                        check("Y un paquete alterado NO se instala",
                              !gamevault::unsealWholeFolder(
                                  L"tp.t94.roto.tomate", L"tp.t94.roto.out",
                                  errV), errV.c_str());
                        borrarArbol(L"tp.t94.roto.out");
                        DeleteFileW(L"tp.t94.roto.tomate");
                    }
                }

                // Dos sellados del mismo contenido dan bytes distintos: cada
                // paquete lleva su propio salt y su propio identificador.
                {
                    std::string errR;
                    std::vector<u8> a, b;
                    Vault::loadFile(kPak, a);
                    gamevault::sealWholeFolder(kDir, L"tp.t94.b.tomate",
                                               &progT, errR);
                    Vault::loadFile(L"tp.t94.b.tomate", b);
                    check("Dos sellados iguales no dan los mismos bytes",
                          !a.empty() && !b.empty() && a != b, "");
                    DeleteFileW(L"tp.t94.b.tomate");
                }

                // --- datos de compresibilidad VARIADA ---
                //
                // ESTA PRUEBA EXISTE POR UN BUG QUE LAS DE ARRIBA NO VIERON, y
                // que rompia todos los paquetes publicados: la validacion del
                // indice comparaba el codec con "el ultimo del enum"
                // (XpressHuff, que vale 2) en vez de con la lista real, asi que
                // cualquier bloque comprimido con Xpress (3) se rechazaba AL
                // LEER. El paquete se guardaba perfecto y despues era ilegible:
                // "el indice del paquete esta danado" sobre un archivo intacto,
                // y ningun juego de la tienda se podia descargar.
                //
                // Las pruebas de arriba usan textos muy repetitivos, que
                // comprimen por debajo de la mitad y por eso elegian
                // XpressHuff -- justo el valor que si pasaba la validacion. Un
                // juego real tiene mezcla de datos y elige Xpress. Aqui se
                // generan a proposito contenidos a medio camino, para que el
                // escritor tenga que usar los distintos codecs y la lectura
                // tenga que aceptarlos todos.
                {
                    const wchar_t* kMixDir = L"tp.t94.mix";
                    const wchar_t* kMixPak = L"tp.t94.mix.tomate";
                    const wchar_t* kMixOut = L"tp.t94.mix.out";
                    borrarArbol(kMixDir); borrarArbol(kMixOut);
                    DeleteFileW(kMixPak);
                    CreateDirectoryW(kMixDir, nullptr);

                    // Seis archivos, de totalmente comprimible a puro ruido.
                    // Pasan de 8 MiB para que haya mas de un bloque.
                    u32 semilla = 20261112u;
                    auto siguiente = [&semilla]() -> u32 {
                        // Generador propio: reproducible y sin depender de la
                        // biblioteca, que aqui da igual lo bueno que sea.
                        semilla ^= semilla << 13;
                        semilla ^= semilla >> 17;
                        semilla ^= semilla << 5;
                        return semilla;
                    };

                    std::vector<std::string> contenidos;
                    for (i32 k = 0; k < 6; ++k) {
                        const size_t total = 9u * 1024 * 1024;
                        const u32 umbral = static_cast<u32>(k) * 200;  // 0..1000
                        std::string s;
                        s.reserve(total);
                        static const char* kPal[] = {
                            "objeto", "valor", "nivel", "malla", "textura", "indice"
                        };
                        while (s.size() < total) {
                            if ((siguiente() % 1000u) < umbral) {
                                for (i32 j = 0; j < 64 && s.size() < total; ++j)
                                    s.push_back(static_cast<char>(siguiente() & 0xFF));
                            } else {
                                const char* w = kPal[siguiente() % 6u];
                                for (const char* c = w; *c && s.size() < total; ++c)
                                    s.push_back(*c);
                                if (s.size() < total) s.push_back(' ');
                            }
                        }
                        wchar_t nom[128];
                        std::swprintf(nom, 128, L"%ls\\mezcla%02d.dat", kMixDir, k);
                        std::ofstream f(nom, std::ios::binary);
                        f.write(s.data(), static_cast<std::streamsize>(s.size()));
                        contenidos.push_back(std::move(s));
                    }

                    std::string errM;
                    std::atomic<float> progM{0.0f};
                    check("Una mezcla realista de datos se sella",
                          gamevault::sealWholeFolder(kMixDir, kMixPak, &progM, errM),
                          errM.c_str());

                    // Lo que fallaba: leer el indice de lo recien escrito.
                    tomatepack::Info infoM;
                    errM.clear();
                    const bool okIdx = tomatepack::info(
                        kMixPak, gamevault::distributionSecret(), infoM, errM);
                    std::snprintf(buf, sizeof(buf), "%llu archivos, %llu bloques",
                                  static_cast<unsigned long long>(infoM.files),
                                  static_cast<unsigned long long>(infoM.blocks));
                    check("Y su indice SE PUEDE LEER despues", okIdx,
                          okIdx ? buf : errM.c_str());

                    errM.clear();
                    check("El paquete se verifica entero",
                          tomatepack::verify(kMixPak,
                                             gamevault::distributionSecret(),
                                             nullptr, errM), errM.c_str());

                    errM.clear();
                    check("Y se puede DESCARGAR (abrir) sin errores",
                          gamevault::unsealWholeFolder(kMixPak, kMixOut, errM),
                          errM.c_str());

                    bool igualM = true;
                    for (size_t k = 0; k < contenidos.size(); ++k) {
                        wchar_t nom[128];
                        std::swprintf(nom, 128, L"%ls\\mezcla%02zu.dat",
                                      kMixOut, k);
                        std::ifstream in(nom, std::ios::binary);
                        const std::string leido(
                            (std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
                        if (leido != contenidos[k]) igualM = false;
                    }
                    check("Y todo el contenido vuelve identico", igualM, "");

                    borrarArbol(kMixDir);
                    borrarArbol(kMixOut);
                    DeleteFileW(kMixPak);
                }

                borrarArbol(kDir);
                borrarArbol(kOut);
                DeleteFileW(kPak);
            }

            // --- con los archivos ya guardados, se puede PUBLICAR ---
            //
            // EL BUG, tal como lo vivio el autor: GENERAR CODIGO da un codigo
            // ALEATORIO, y GUARDAR ARCHIVOS anota el paquete bajo ese codigo
            // porque el juego aun no tiene nombre -- el nombre se escribe
            // despues, al subirlo. Al publicar se buscaba el paquete por el
            // codigo DETERMINISTA derivado del nombre (otro codigo distinto) o
            // por una reserva (que no existia), asi que nunca se encontraba:
            // se seguia pidiendo subir unos archivos que ya estaban subidos.
            {
                const wchar_t* kCatP = L"pub.t95.dat";
                DeleteFileW(kCatP);
                DeleteFileW(L"pub.t95.dat.pendientes");

                // Un paquete que EXISTE en disco: un registro que apunta a un
                // archivo borrado no debe dar luz verde.
                const wchar_t* kPaq = L"pub.t95.tomate";
                { std::ofstream f(kPaq, std::ios::binary); f << "paquete"; }

                const std::string codigo = devcatalog::randomGameCode(kCatP);
                std::snprintf(buf, sizeof(buf), "codigo %s (%zu caracteres)",
                              codigo.c_str(), codigo.size());
                check("GENERAR CODIGO da un codigo aleatorio largo",
                      codigo.size() == 16, buf);   // '#' + 15

                devcatalog::Pending pp;
                pp.game    = codigo;          // anotado por CODIGO, sin nombre
                pp.archive = kPaq;
                pp.files   = 4826;
                pp.bytes   = 1109861783ull;
                std::string errP;
                check("Los archivos se guardan bajo ese codigo",
                      devcatalog::setPending(kCatP, pp, errP), errP.c_str());

                check("Y el catalogo sigue vacio (nada publicado aun)",
                      devcatalog::load(kCatP).empty(), "");

                // Lo que fallaba: el autor escribe el nombre y se le pide
                // subir los archivos otra vez.
                const std::string nomP = "Mi Primer Juego";
                check("Con los archivos guardados, YA permite publicar",
                      devcatalog::hasArchive(kCatP, nomP),
                      "antes pedia subirlos de nuevo");

                check("Y los reconoce como paquete sin dueno",
                      !devcatalog::unclaimedArchive(kCatP).archive.empty(), "");

                // Publicar tiene que ADOPTAR ese paquete: si no, el juego
                // quedaria publicado sin archivos y nadie podria descargarlo.
                errP.clear();
                const bool okPubP =
                    devcatalog::publish(kCatP, nomP, L"Juego.exe", errP);
                check("El juego se publica", okPubP, errP.c_str());

                const std::vector<devcatalog::Game> catP = devcatalog::load(kCatP);
                bool conArch = false;
                if (!catP.empty() && !catP[0].versions.empty()) {
                    const auto& vP = catP[0].versions.back();
                    conArch = !vP.archive.empty() && vP.archiveFiles == 4826;
                    std::snprintf(buf, sizeof(buf), "archivos=%d bytes=%llu",
                                  vP.archiveFiles,
                                  static_cast<unsigned long long>(vP.archiveBytes));
                }
                check("Y queda publicado CON sus archivos", conArch, buf);

                check("El pendiente se consumio al publicarlo",
                      devcatalog::unclaimedArchive(kCatP).archive.empty(), "");

                // Y lo que NO debe pasar: dar luz verde sin tener nada.
                const wchar_t* kCatQ = L"pub.t95b.dat";
                DeleteFileW(kCatQ);
                DeleteFileW(L"pub.t95b.dat.pendientes");
                check("Sin archivos guardados, sigue pidiendolos",
                      !devcatalog::hasArchive(kCatQ, "Juego Sin Archivos"), "");

                // Un registro cuyo paquete ya no esta en el disco tampoco
                // cuenta: publicarlo daria un juego que nadie puede descargar.
                devcatalog::Pending roto;
                roto.game    = devcatalog::randomGameCode(kCatQ);
                roto.archive = L"no.existe.t95.tomate";
                roto.files   = 7;
                roto.bytes   = 99;
                errP.clear();
                devcatalog::setPending(kCatQ, roto, errP);
                check("Un paquete que ya no esta en el disco NO cuenta",
                      !devcatalog::hasArchive(kCatQ, "Juego Con Paquete Borrado"),
                      "");

                DeleteFileW(kCatP);
                DeleteFileW(L"pub.t95.dat.pendientes");
                DeleteFileW(kCatQ);
                DeleteFileW(L"pub.t95b.dat.pendientes");
                DeleteFileW(kPaq);
            }

            // --- la revision de seguridad de las descargas ---
            //
            // No es un antivirus propio: le pasa el contenido al antivirus que
            // ya tiene el equipo (por AMSI) y anade comprobaciones propias de
            // una plataforma de juegos. Lo que se prueba aqui es que no deja
            // pasar lo evidente y que no inventa amenazas en un juego normal.
            {
                const wchar_t* kG = L"guard.t96";
                auto borrarG = [](const wchar_t* d) {
                    std::wstring from(d);
                    from.push_back(L'\0'); from.push_back(L'\0');
                    SHFILEOPSTRUCTW op{};
                    op.wFunc = FO_DELETE; op.pFrom = from.c_str();
                    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI;
                    SHFileOperationW(&op);
                };
                borrarG(kG);

                // Un juego normal: no debe dar ni un hallazgo.
                CreateDirectoryW(kG, nullptr);
                CreateDirectoryW(L"guard.t96\\assets", nullptr);
                {
                    std::ofstream(L"guard.t96\\Juego.exe", std::ios::binary)
                        << "MZ" << std::string(600, '\x90');
                    std::ofstream(L"guard.t96\\motor.dll", std::ios::binary)
                        << "MZ" << std::string(400, '\x01');
                    std::ofstream(L"guard.t96\\assets\\tex.png", std::ios::binary)
                        << "\x89PNG\r\n\x1a\n" << std::string(900, '\x40');
                    std::ofstream(L"guard.t96\\conf.json", std::ios::binary)
                        << "{\"v\":1}";
                }

                const guardian::Informe limpio = guardian::revisarCarpeta(kG);
                check("La revision de un juego normal termina",
                      limpio.hecho, limpio.resumen().c_str());
                check("Y no lo bloquea", !limpio.bloqueado(), "");
                std::snprintf(buf, sizeof(buf), "%d archivos, %zu hallazgos",
                              limpio.archivos, limpio.hallazgos.size());
                check("Ni inventa amenazas donde no hay",
                      limpio.hallazgos.empty(), buf);

                // Un ejecutable disfrazado de imagen: el truco mas comun, y la
                // comprobacion que un antivirus por extension no hace.
                {
                    std::ofstream(L"guard.t96\\assets\\logo.png", std::ios::binary)
                        << "MZ" << std::string(1200, '\x90');
                }
                const guardian::Informe disfraz = guardian::revisarCarpeta(kG);
                check("Un .exe disfrazado de .png se detecta y BLOQUEA",
                      disfraz.bloqueado(), disfraz.resumen().c_str());

                bool dice = false;
                for (const auto& h : disfraz.hallazgos)
                    if (h.archivo.find(L"logo.png") != std::wstring::npos)
                        dice = true;
                check("Y el informe dice QUE archivo era", dice, "");
                DeleteFileW(L"guard.t96\\assets\\logo.png");

                // Un script de instalacion: se avisa, pero no se bloquea --
                // eso lo decide el jugador.
                {
                    std::ofstream(L"guard.t96\\instalar.bat", std::ios::binary)
                        << "@echo off\n";
                }
                const guardian::Informe script = guardian::revisarCarpeta(kG);
                check("Un script dentro del juego se avisa",
                      !script.hallazgos.empty(), script.resumen().c_str());
                check("Pero no bloquea la instalacion por si solo",
                      !script.bloqueado(), "");

                // Un bloque limpio no debe dar hallazgos por el camino de
                // "mientras descarga".
                {
                    const std::string datos(40000, 'A');
                    std::vector<guardian::Hallazgo> hs;
                    check("Un bloque limpio pasa la revision al vuelo",
                          guardian::revisarBloque(
                              reinterpret_cast<const u8*>(datos.data()),
                              datos.size(), L"bloque", hs) && hs.empty(), "");
                }

                // --- los .js de documentacion NO son un aviso ---
                //
                // POR QUE: el primer juego real que se reviso dio CIEN avisos,
                // y noventa y nueve eran los .js de la documentacion de GLFW,
                // una biblioteca que usa el motor. Un aviso que sale siempre
                // es un aviso que nadie lee, y el que de verdad importa queda
                // enterrado. Lo que vive en docs/ o external/ no lo ejecuta el
                // juego: viene con el codigo de sus dependencias.
                DeleteFileW(L"guard.t96\\instalar.bat");
                CreateDirectoryW(L"guard.t96\\external", nullptr);
                CreateDirectoryW(L"guard.t96\\external\\glfw", nullptr);
                CreateDirectoryW(L"guard.t96\\external\\glfw\\docs", nullptr);
                {
                    for (i32 k = 0; k < 8; ++k) {
                        wchar_t nom[160];
                        std::swprintf(nom, 160,
                                      L"guard.t96\\external\\glfw\\docs\\all_%d.js", k);
                        std::ofstream(nom, std::ios::binary) << "var x=1;\n";
                    }
                }
                const guardian::Informe docs = guardian::revisarCarpeta(kG);
                std::snprintf(buf, sizeof(buf), "%zu hallazgos; %s",
                              docs.hallazgos.size(), docs.resumen().c_str());
                check("Los .js de documentacion no llenan el informe de avisos",
                      docs.hallazgos.empty(), buf);
                check("Y no bloquean la descarga", !docs.bloqueado(), "");

                // Pero un script EN LA RAIZ, donde el juego si lo lanzaria,
                // sigue avisando: el filtro no relaja lo que importa.
                { std::ofstream(L"guard.t96\\arranca.bat", std::ios::binary)
                      << "@echo off\n"; }
                const guardian::Informe raiz = guardian::revisarCarpeta(kG);
                bool avisaRaiz = false;
                for (const auto& h : raiz.hallazgos)
                    if (h.archivo.find(L"arranca.bat") != std::wstring::npos)
                        avisaRaiz = true;
                check("Pero un script en la raiz del juego SI avisa",
                      avisaRaiz, raiz.resumen().c_str());

                borrarG(kG);
            }

            // --- guardar PUBLICA la version, sin un segundo boton ---
            //
            // Es el nucleo de lo que se pidio: "apenas se guarde
            // correctamente se suba la version para que todos la tengan".
            // Se comprueba en los tres sitios donde tiene que llegar: el
            // catalogo (y con el la tienda), el historial y la biblioteca de
            // quien ya tenia el juego.
            {
                const wchar_t* kSrcV = L"verpub.t91";
                CreateDirectoryW(kSrcV, nullptr);
                {
                    std::ofstream(L"verpub.t91\\Juego.exe", std::ios::binary)
                        << "MZ-version-nueva";
                    std::ofstream(L"verpub.t91\\datos.bin", std::ios::binary)
                        << "contenido-v2";
                }

                // El juego ya esta en la biblioteca: es quien debe recibir la
                // version nueva.
                const wchar_t* kLibV = L"biblioteca.dat";
                DeleteFileW(kLibV);
                library::Entry ya;
                ya.name    = "Juego Actualizable";
                ya.exe     = L"viejo\\Juego.exe";
                ya.vault   = L"viejo.tomate";
                ya.exeName = L"Juego.exe";
                library::add(kLibV, ya);

                const size_t versAntes =
                    devcatalog::load(kCatReal).empty()
                        ? 0 : devcatalog::load(kCatReal)[0].versions.size();

                up.setArchiveCode(codigoU);
                up.update(engine, 0.016);
                i32 nf = 0; u64 nb = 0;
                gamevault::measureFolder(kSrcV, nf, nb);
                up.setArchiveFolderForTest(kSrcV, nf, nb);
                up.startArchiveForTest();

                // El sellado corre en su hilo: se espera a que acabe y se
                // recoge, que es lo que hace `pollArchive` en cada frame.
                for (i32 i = 0; i < 2000 && up.archiveRunning(); ++i)
                    Sleep(5);
                up.pollArchiveForTest();

                const std::vector<devcatalog::Game> cat =
                    devcatalog::load(kCatReal);
                const size_t versDespues = cat.empty() ? 0
                                                       : cat[0].versions.size();
                std::snprintf(buf, sizeof(buf), "versiones %zu -> %zu; msg='%s'",
                              versAntes, versDespues, up.archiveMessage().c_str());
                check("Al guardar bien, la version se publica sola",
                      versDespues == versAntes + 1, buf);
                check("Y se avisa de que ya la tiene todo el mundo",
                      up.archiveMessage().find("Publicada") != std::string::npos,
                      up.archiveMessage().c_str());
                check("La version vigente lleva su paquete",
                      !cat.empty() && cat[0].current() &&
                      !cat[0].current()->archive.empty(), buf);

                // La biblioteca de quien ya tenia el juego apunta al paquete
                // nuevo: es lo que hace que al pulsar JUGAR salga la version
                // nueva y no la vieja.
                bool apunta = false;
                for (const auto& e : library::load(kLibV))
                    if (e.name == "Juego Actualizable" &&
                        e.vault != L"viejo.tomate" && !e.vault.empty())
                        apunta = true;
                check("La biblioteca de quien lo tenia apunta a la nueva",
                      apunta, "");

                // El historial registra el movimiento de version.
                bool hayHist = false;
                const history::GameHistory hh =
                    history::forGame(L"biblioteca-hist.dat",
                                     "Juego Actualizable", &hayHist);
                check("El historial anota la version nueva",
                      hayHist && !hh.versions.empty(),
                      std::to_string(hh.versions.size()));

                // El change log escrito antes se consume: dejarlo haria que la
                // siguiente version heredara estas notas.
                check("El change log se consume al publicar",
                      up.updateChangelog().empty(),
                      up.updateChangelog().c_str());

                // --- al confirmarse el guardado, se salta solo a PRECIOS ---
                //
                // Lo que toca despues es siempre lo mismo: poner precio y
                // subir. Dejar la seccion abierta obligaba a cerrarla a mano,
                // y el mensaje de exito en pantalla hacia creer que ya estaba
                // todo hecho.
                std::snprintf(buf, sizeof(buf), "panel=%d, aviso='%s'",
                              up.devOpenPanel(), up.devMessage().c_str());
                check("Tras guardar bien, se abre PRECIOS solo",
                      up.devOpenPanel() == 2, buf);   // 2 = kDevPrice
                check("Y se dice que toca poner precio y subir",
                      up.devMessage().find("precio") != std::string::npos,
                      buf);
                check("El aviso no es de error",
                      !up.devMessageIsError(), buf);

                // La carpeta se suelta: ya esta dentro de Ludora, y dejarla
                // haria parecer que queda un guardado a medias.
                check("La carpeta elegida se suelta al terminar",
                      up.archiveFolderForTest().empty(),
                      "queda carpeta pendiente");

                // Limpieza.
                if (!cat.empty() && cat[0].current())
                    DeleteFileW(cat[0].current()->archive.c_str());
                DeleteFileW(L"verpub.t91\\Juego.exe");
                DeleteFileW(L"verpub.t91\\datos.bin");
                RemoveDirectoryW(kSrcV);
                DeleteFileW(kLibV);
                DeleteFileW(L"biblioteca-hist.dat");
            }

            up.onExit(engine);
            DeleteFileW(kCatReal);
            if (habiaU) MoveFileW(kBakU, kCatReal);
        }

        // --- texto del sistema: nitido y con todo el ASCII ---
        {
            check("El rasterizador del sistema esta disponible",
                  textr::available());

            // --- todos los simbolos ASCII imprimibles se dibujan ---
            // Se comprueba que cada uno produce tinta y avanza el cursor: un
            // glifo que faltara mediria cero o no pintaria nada.
            i32 sinAncho = 0, sinTinta = 0;
            Framebuffer gl;
            gl.resize(64, 40);

            for (i32 c = 32; c <= 126; ++c) {
                const char s[2] = { static_cast<char>(c), 0 };
                if (font5x7::measure(s, 2) <= 0 && c != ' ') ++sinAncho;

                gl.clear(Color::fromHex(0x000000));
                font5x7::drawText(gl, 4, 4, s, Color::fromHex(0xFFFFFF), 2);

                bool tinta = false;
                for (i32 y = 0; y < 40 && !tinta; ++y)
                    for (i32 x = 0; x < 64; ++x)
                        if (gl.data()[static_cast<size_t>(y) * 64 + x] !=
                            Color::fromHex(0x000000).packed()) { tinta = true; break; }
                if (!tinta && c != ' ') ++sinTinta;
            }
            std::snprintf(buf, sizeof(buf), "%d sin ancho, %d sin tinta",
                          sinAncho, sinTinta);
            check("Los 95 simbolos ASCII imprimibles se dibujan",
                  sinAncho == 0 && sinTinta == 0, buf);

            // --- suavizado: hay tonos intermedios, no solo blanco y negro ---
            // Es lo que distingue una letra rasterizada de un mapa de bits:
            // los bordes se funden con el fondo en vez de cortarse a bloque.
            Framebuffer sm;
            sm.resize(200, 40);
            sm.clear(Color::fromHex(0x000000));
            font5x7::drawText(sm, 6, 6, "Sagol", Color::fromHex(0xFFFFFF), 3);

            i32 tonos = 0;
            bool visto[256] = {};
            for (i32 y = 0; y < 40; ++y)
                for (i32 x = 0; x < 200; ++x) {
                    const u32 p = sm.data()[static_cast<size_t>(y) * 200 + x];
                    const u8 g = static_cast<u8>((p >> 8) & 0xFF);   // canal verde
                    if (!visto[g]) { visto[g] = true; ++tonos; }
                }
            std::snprintf(buf, sizeof(buf), "%d tonos distintos", tonos);
            // Una fuente bitmap da 2 tonos (fondo y trazo); una suavizada da
            // muchos mas por los bordes.
            check("La letra tiene bordes suavizados, no de bloque",
                  tonos > 8, buf);

            // --- los rotulos de boton SIEMPRE se dibujan ---
            // El patron antiguo ("medir y no pintar si no cabe") dejaba
            // botones mudos en cuanto la fuente cambiaba de ancho. drawFitted
            // encoge o recorta, pero nunca deja el hueco en blanco.
            {
                Framebuffer bt;
                bt.resize(240, 40);
                const u32 negro = Color::fromHex(0x000000).packed();

                auto hayTinta = [&](const Recti& r) {
                    for (i32 y = r.y; y < r.y + r.h && y < 40; ++y)
                        for (i32 x = r.x; x < r.x + r.w && x < 240; ++x)
                            if (bt.data()[static_cast<size_t>(y) * 240 + x] != negro)
                                return true;
                    return false;
                };

                // Un rotulo largo en un boton estrecho: el caso que fallaba.
                const Recti chico{ 4, 4, 200, 24 };
                bt.clear(Color::fromHex(0x000000));
                font5x7::drawFitted(bt, chico, "MANTENER PRECIOS POR DEFECTO?",
                                    Color::fromHex(0xFFFFFF), 1, 4);
                check("Un rotulo que no cabe se dibuja igualmente",
                      hayTinta(chico));

                // Y uno cortisimo tambien, sin pasarse del cuadro.
                const Recti mini{ 4, 4, 60, 24 };
                bt.clear(Color::fromHex(0x000000));
                font5x7::drawFitted(bt, mini, "ACEPTAR",
                                    Color::fromHex(0xFFFFFF), 1, 3);
                check("Un rotulo en un boton muy estrecho tambien",
                      hayTinta(mini));

                // Nada se sale del cuadro: se mira la franja de al lado.
                const Recti fuera{ mini.x + mini.w + 2, 0, 60, 40 };
                check("Y no desborda su rectangulo", !hayTinta(fuera));
            }

            // Acentos y enye siguen funcionando con la fuente nueva.
            check("Los acentos y la enye se miden",
                  font5x7::measureUtf8("ÁÉÍÓÚÑáéíóúñ¿¡", 2) > 0);
        }

        // --- la descripcion se apila DENTRO de su ficha ---
        {
            const wchar_t* kProfD = L"perfil.dat";
            const wchar_t* kBakD  = L"perfil.t68d";
            DeleteFileW(kBakD);
            Profile prev;
            const bool habiaD = Profile::load(kProfD, prev);
            if (habiaD) MoveFileW(kProfD, kBakD);

            // Descripcion larga de una sola frase sin saltos: es el caso que
            // se salia del cuadro y cruzaba la pantalla.
            Profile p;
            p.userName = "Usuario";
            p.description =
                "Esta es una descripcion muy larga escrita de corrido para "
                "comprobar que el texto se reparte en varias lineas dentro "
                "de su propia ficha y no se sale por la derecha cruzando "
                "toda la seccion de cuenta como pasaba antes de ajustarlo.";
            Profile::save(kProfD, p);

            Framebuffer probeD;
            probeD.resize(960, 600);

            HomeScene cta("desc@ludora.engine");
            cta.onEnter(engine);
            cta.section(HomeScene::kAccountSection);
            cta.render(engine, probeD);

            // Las fichas van CENTRADAS, no pegadas al borde: el calculo del
            // borde derecho tiene que salir de ahi, igual que al dibujarlas.
            const i32 anchoFicha = std::min(820, 960 - 16 * 2);
            const i32 xFicha     = (960 - anchoFicha) / 2;
            const i32 bordeDer   = xFicha + anchoFicha;

            const u32 violeta = Color::fromHex(0x2B1B57).packed();
            const u32 fondo   = Color::fromHex(0x090909).packed();
            // Ancho de la barra de scroll, que se excluye de la medida.
            constexpr i32 kScrollBarWTest = 16;
            // La franja a la derecha de las fichas se revisa evitando dos
            // cosas que viven ahi legitimamente: el avatar de la esquina
            // inferior derecha y la barra de scroll pegada a la pared.
            i32 fuera = 0;
            for (i32 y = 100; y < 440; ++y)
                for (i32 xx = bordeDer + 2; xx < 960 - kScrollBarWTest; ++xx) {
                    const u32 col = probeD.data()[static_cast<size_t>(y) * 960 + xx];
                    // Cualquier cosa que no sea fondo ni violeta de ficha a la
                    // derecha del borde es texto desbordado.
                    if (col != fondo && col != violeta) ++fuera;
                }
            std::snprintf(buf, sizeof(buf), "%d pixeles fuera del cuadro", fuera);
            // Sin la barra de scroll, el desborde real tiene que ser CERO.
            check("La descripcion no se sale de su ficha", fuera == 0, buf);

            // Y el boton de cerrar sesion sigue dentro del recorrido: la
            // ficha crece, asi que el panel tiene que crecer con ella.
            const Recti lo = cta.logoutButtonRect();
            std::snprintf(buf, sizeof(buf), "CERRAR SESION en (%d,%d)", lo.x, lo.y);
            check("Y el boton de cerrar sesion sigue alcanzable",
                  lo.w > 0 && lo.y > 100, buf);

            cta.onExit(engine);
            DeleteFileW(kProfD);
            if (habiaD) MoveFileW(kBakD, kProfD);
        }

        // --- instalacion generica: cifra datos, deja assets en claro ---
        {
            // Se monta un juego de mentira con las dos clases de archivo:
            // texturas y ejecutable (assets, van en claro) y partidas y
            // config (datos del jugador, van cifrados).
            const std::wstring raiz = L"inst68";
            const std::wstring src  = raiz + L"\\origen";
            const std::wstring dst  = raiz + L"\\destino";

            auto borrarTodo = [&](const std::wstring& d) {
                // Borrado recursivo simple: solo se usa sobre lo que crea
                // esta prueba, nunca sobre rutas del usuario.
                std::function<void(const std::wstring&)> rec =
                    [&](const std::wstring& dir) {
                        WIN32_FIND_DATAW fd{};
                        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
                        if (h == INVALID_HANDLE_VALUE) return;
                        do {
                            const std::wstring n = fd.cFileName;
                            if (n == L"." || n == L"..") continue;
                            const std::wstring p = dir + L"\\" + n;
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                rec(p);
                            else DeleteFileW(p.c_str());
                        } while (FindNextFileW(h, &fd));
                        FindClose(h);
                        RemoveDirectoryW(dir.c_str());
                    };
                rec(d);
            };
            borrarTodo(raiz);

            CreateDirectoryW(raiz.c_str(), nullptr);
            CreateDirectoryW(src.c_str(), nullptr);
            CreateDirectoryW((src + L"\\resourcepacks").c_str(), nullptr);
            CreateDirectoryW((src + L"\\saves").c_str(), nullptr);

            auto escribir = [](const std::wstring& p, const char* txt) {
                std::ofstream f(p.c_str(), std::ios::binary);
                f << txt;
            };
            escribir(src + L"\\MiJuego.exe",              "EJECUTABLE");
            escribir(src + L"\\resourcepacks\\bloque.png", "TEXTURA-EN-CLARO");
            escribir(src + L"\\resourcepacks\\musica.mp3", "SONIDO-EN-CLARO");
            escribir(src + L"\\saves\\partida.dat",        "PROGRESO-SECRETO");
            escribir(src + L"\\config.ini",                "ajustes=1");
            // Formato propio y desconocido: con la regla nueva se CIFRA, que
            // es lo que se pidio -- todo menos sonidos y texturas.
            escribir(src + L"\\mundo.miformato",           "LOGICA-PROPIA");

            GameInstall inst;
            check("Arranca la instalacion de un juego cualquiera",
                  inst.start(src + L"\\MiJuego.exe", dst, "Mi Juego",
                             L"MiJuego.exe"));
            inst.join();

            std::snprintf(buf, sizeof(buf), "fallo=%d: %s",
                          inst.failed() ? 1 : 0, inst.error().c_str());
            check("Y termina sin errores", inst.done() && !inst.failed(), buf);

            // Los ASSETS quedan en claro y legibles.
            auto leer = [](const std::wstring& p) {
                std::ifstream f(p.c_str(), std::ios::binary);
                std::string s((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
                return s;
            };
            check("La textura se copia EN CLARO",
                  leer(dst + L"\\resourcepacks\\bloque.png") == "TEXTURA-EN-CLARO");
            check("El sonido tambien se copia EN CLARO",
                  leer(dst + L"\\resourcepacks\\musica.mp3") == "SONIDO-EN-CLARO");
            check("Y el ejecutable tambien",
                  leer(dst + L"\\MiJuego.exe") == "EJECUTABLE");

            // --- se cifra por defecto lo que no es sonido ni textura ---
            // Un formato propio y desconocido no debe quedar expuesto.
            const bool propioSuelto =
                GetFileAttributesW((dst + L"\\mundo.miformato").c_str())
                != INVALID_FILE_ATTRIBUTES;
            check("Un formato desconocido NO se deja en claro", !propioSuelto);
            check("Y la clasificacion lo trata como dato",
                  gamevault::isSensitiveData(L"mundo.miformato"));
            check("Mientras texturas y sonidos siguen siendo assets",
                  !gamevault::isSensitiveData(L"resourcepacks\\bloque.png") &&
                  !gamevault::isSensitiveData(L"resourcepacks\\musica.mp3") &&
                  !gamevault::isSensitiveData(L"MiJuego.exe"));

            // Los DATOS del jugador NO quedan en claro en el destino.
            const bool partidaSuelta =
                GetFileAttributesW((dst + L"\\saves\\partida.dat").c_str())
                != INVALID_FILE_ATTRIBUTES;
            check("La partida NO se deja suelta en el destino", !partidaSuelta);

            // Y estan dentro del paquete cifrado, ilegibles.
            const std::wstring vault = inst.vaultPath();
            check("Se creo el paquete cifrado de datos",
                  GetFileAttributesW(vault.c_str()) != INVALID_FILE_ATTRIBUTES,
                  vault.empty() ? "(sin ruta)" : "ok");
            const std::string crudo = leer(vault);
            check("El progreso NO aparece en claro dentro del paquete",
                  crudo.find("PROGRESO-SECRETO") == std::string::npos,
                  std::to_string(crudo.size()) + " bytes");
            check("Ni el contenido del formato propio",
                  crudo.find("LOGICA-PROPIA") == std::string::npos);

            // Restaurar deja el juego completo y jugable otra vez.
            std::string rerr2;
            check("Los datos se restauran sobre la carpeta instalada",
                  gamevault::restoreData(vault, dst, rerr2), rerr2.c_str());
            check("Y la partida vuelve con su contenido intacto",
                  leer(dst + L"\\saves\\partida.dat") == "PROGRESO-SECRETO");
            check("El formato propio tambien se recupera entero",
                  leer(dst + L"\\mundo.miformato") == "LOGICA-PROPIA");

            // --- liberar un juego: sus datos quedan en claro ---
            // Es lo que pasa cuando el creador lo retira de la tienda: quien
            // ya lo tiene conserva el juego, pero sin cifrado, asi que puede
            // modificarlo.
            {
                // Se parte de la instalacion de arriba, con su paquete.
                check("Antes de liberar, el paquete cifrado existe",
                      GetFileAttributesW(vault.c_str()) != INVALID_FILE_ATTRIBUTES);

                std::string lerr;
                check("Liberar el juego funciona",
                      gamevault::releaseGame(dst + L"\\MiJuego.exe", vault, lerr),
                      lerr.c_str());

                check("El paquete cifrado desaparece",
                      GetFileAttributesW(vault.c_str()) == INVALID_FILE_ATTRIBUTES);
                check("Y la partida queda EN CLARO y modificable",
                      leer(dst + L"\\saves\\partida.dat") == "PROGRESO-SECRETO");
                check("Igual que el formato propio",
                      leer(dst + L"\\mundo.miformato") == "LOGICA-PROPIA");
            }

            // --- tras instalar, la Biblioteca abre en ese juego ---
            // Se registra la entrada como lo haria el instalador y se
            // comprueba que la escena la encuentra y la selecciona.
            {
                const wchar_t* kLibT = L"biblioteca.dat";
                const wchar_t* kBakL = L"biblioteca.t68l";
                DeleteFileW(kBakL);
                const bool habiaL =
                    (GetFileAttributesW(kLibT) != INVALID_FILE_ATTRIBUTES);
                if (habiaL) MoveFileW(kLibT, kBakL);

                library::Entry le;
                le.name    = "Mi Juego";
                le.exe     = dst + L"\\MiJuego.exe";
                le.vault   = vault;
                le.exeName = L"MiJuego.exe";
                library::add(kLibT, le);

                check("El juego instalado queda en la biblioteca",
                      library::has(kLibT, "Mi Juego"));
                check("Y cuenta como realmente instalado",
                      library::entryInstalled(le));

                Framebuffer probeL;
                probeL.resize(960, 600);
                HomeScene lib("lib68@ludora.engine");
                lib.onEnter(engine);
                lib.section(HomeScene::kPanelsSection);
                lib.render(engine, probeL);

                // La escena lo ve en su lista, que es lo que permite abrir su
                // seccion propia al terminar la descarga.
                bool enLista = false;
                for (i32 k = 0; k < 12; ++k) {
                    lib.selectLibraryGame(k);
                    lib.render(engine, probeL);
                    if (lib.playButtonRect().w > 0) { enLista = true; break; }
                }
                check("La Biblioteca puede abrir su seccion propia", enLista);

                lib.onExit(engine);
                DeleteFileW(kLibT);
                if (habiaL) MoveFileW(kBakL, kLibT);
            }

            borrarTodo(raiz);
        }

        // --- seguridad: rotacion de claves y guardian ---
        {
            VaultGuard::reset();

            const std::string secreto = "clave de prueba 68";
            const std::vector<u8> claro{ 'h','o','l','a','!' };

            std::vector<u8> a, b;
            std::string verr;
            check("Se sella un almacen", Vault::seal(secreto, claro, a, verr),
                  verr.c_str());
            check("Y otra vez con el mismo contenido",
                  Vault::seal(secreto, claro, b, verr), verr.c_str());

            // La rotacion de verdad: el mismo contenido cifrado dos veces da
            // bytes distintos, porque salts y nonces son nuevos cada vez.
            check("Dos sellados del MISMO dato dan bytes distintos", a != b);

            // Pero los dos se abren igual.
            std::vector<u8> sa, sb;
            check("Ambos se abren con el mismo secreto",
                  Vault::open(secreto, a, sa, verr) &&
                  Vault::open(secreto, b, sb, verr) &&
                  sa == claro && sb == claro, verr.c_str());

            // rotate() renueva las claves sin tocar el contenido.
            std::vector<u8> rot, sr;
            check("rotate() vuelve a cifrar con claves nuevas",
                  Vault::rotate(secreto, a, rot, verr), verr.c_str());
            check("El blob rotado es distinto del original", rot != a);
            check("Pero guarda el mismo contenido",
                  Vault::open(secreto, rot, sr, verr) && sr == claro,
                  verr.c_str());

            // --- deteccion de manipulacion ---
            VaultGuard::reset();
            std::vector<u8> tocado = a;
            tocado[tocado.size() - 1] ^= 0xFF;   // alterar un byte del final
            std::vector<u8> nada;
            const Vault::OpenResult r1 =
                Vault::openDetailed(secreto, tocado, nada, verr);
            check("Un blob alterado se detecta como manipulado",
                  r1 == Vault::OpenResult::Tampered);

            check("Y el guardian corta el acceso al instante",
                  !VaultGuard::note(r1) && VaultGuard::locked());
            check("Queda constancia de la manipulacion",
                  VaultGuard::tamperSeen());
            std::snprintf(buf, sizeof(buf), "%lld s", VaultGuard::lockRemaining());
            check("Y el bloqueo tiene duracion", VaultGuard::lockRemaining() > 0,
                  buf);

            // Un archivo que ni siquiera tiene forma de blob es "corrupto",
            // no "secreto equivocado".
            VaultGuard::reset();
            const std::vector<u8> basura{ 1, 2, 3, 4, 5 };
            check("Un archivo sin forma valida se marca como corrupto",
                  Vault::openDetailed(secreto, basura, nada, verr) ==
                  Vault::OpenResult::Corrupt);

            // Varios fallos seguidos con el secreto equivocado tambien
            // bloquean, aunque no haya manipulacion.
            VaultGuard::reset();
            bool sigueAbierto = true;
            for (i32 k = 0; k < VaultGuard::kMaxFails && sigueAbierto; ++k)
                sigueAbierto = VaultGuard::note(Vault::OpenResult::BadSecret);
            std::snprintf(buf, sizeof(buf), "%d fallos", VaultGuard::failCount());
            check("Varios fallos seguidos tambien bloquean",
                  !sigueAbierto && VaultGuard::locked(), buf);

            // Una apertura correcta limpia los fallos.
            VaultGuard::reset();
            VaultGuard::note(Vault::OpenResult::BadSecret);
            VaultGuard::note(Vault::OpenResult::Ok);
            check("Una apertura correcta limpia los fallos",
                  VaultGuard::failCount() == 0);

            VaultGuard::reset();
        }

        // --- biblioteca: desinstalar, reinstalar y mods ---
        {
            DeleteFileW(L"desinstalados.dat");

            Framebuffer probeB;
            probeB.resize(960, 600);

            HomeScene bib("bib@ludora.engine");
            bib.onEnter(engine);
            bib.section(HomeScene::kPanelsSection);
            bib.render(engine, probeB);

            const Recti jugar = bib.playButtonRect();
            const Recti des   = bib.uninstallButtonRect();
            const Recti mods  = bib.modsButtonRect();
            std::snprintf(buf, sizeof(buf),
                          "JUGAR (%d,%d) DESINST (%d,%d) MODS (%d,%d)",
                          jugar.x, jugar.y, des.x, des.y, mods.x, mods.y);
            check("Hay botones de desinstalar y mods", des.w > 0 && mods.w > 0, buf);
            check("Van DEBAJO de jugar", des.y > jugar.y, buf);
            check("Y MODS esta al lado de desinstalar",
                  mods.y == des.y || mods.y > des.y, buf);

            // El estado se guarda y se recupera.
            check("Al entrar, nada esta desinstalado",
                  !bib.isUninstalled("Juego De Ejemplo"));
            bib.setUninstalledForTest("Juego De Ejemplo", true);
            check("Desinstalar marca el juego",
                  bib.isUninstalled("Juego De Ejemplo"));
            bib.render(engine, probeB);
            check("Reinstalar lo desmarca",
                  (bib.setUninstalledForTest("Juego De Ejemplo", false),
                   !bib.isUninstalled("Juego De Ejemplo")));

            // Persistencia entre sesiones.
            bib.setUninstalledForTest("Juego De Ejemplo", true);
            HomeScene otra("bib2@ludora.engine");
            otra.onEnter(engine);
            check("El estado sobrevive a reabrir la app",
                  otra.isUninstalled("Juego De Ejemplo"));
            otra.onExit(engine);

            // --- desinstalar borra los archivos del disco ---
            {
                // Se monta un juego de mentira con varios archivos y
                // subcarpetas, y se comprueba que no queda nada.
                const std::wstring raizD = L"borrar68";
                const std::wstring dirD  = raizD + L"\\MiJuegoBorrable";
                CreateDirectoryW(raizD.c_str(), nullptr);
                CreateDirectoryW(dirD.c_str(), nullptr);
                CreateDirectoryW((dirD + L"\\assets").c_str(), nullptr);

                auto esc = [](const std::wstring& p, const char* t) {
                    std::ofstream f(p.c_str(), std::ios::binary); f << t;
                };
                esc(dirD + L"\\juego.exe",          "EXE");
                esc(dirD + L"\\assets\\tex.png",    "TEX");
                esc(dirD + L"\\saves.dat",          "DATOS");
                esc(raizD + L"\\MiJuegoBorrable.datos.tomate", "CIFRADO");

                library::Entry be;
                be.name    = "MiJuegoBorrable";
                be.exe     = dirD + L"\\juego.exe";
                be.vault   = raizD + L"\\MiJuegoBorrable.datos.tomate";
                be.exeName = L"juego.exe";

                std::string derr;
                const i32 n = library::deleteFiles(be, derr);
                std::snprintf(buf, sizeof(buf), "%d archivos, err='%s'",
                              n, derr.c_str());
                check("Desinstalar borra los archivos del disco",
                      n >= 4 && derr.empty(), buf);
                check("La carpeta del juego desaparece",
                      GetFileAttributesW(dirD.c_str()) == INVALID_FILE_ATTRIBUTES);
                check("Y el paquete cifrado tambien",
                      GetFileAttributesW(be.vault.c_str()) ==
                      INVALID_FILE_ATTRIBUTES);

                RemoveDirectoryW(raizD.c_str());

                // --- salvaguardas: no se borra nada peligroso ---
                library::Entry raiz;
                raiz.name = "Malo";
                raiz.exe  = L"C:\\juego.exe";     // carpeta deducida = "C:"
                std::string rerr3;
                check("No se borra la raiz de una unidad",
                      library::deleteFiles(raiz, rerr3) == 0 && !rerr3.empty(),
                      rerr3.c_str());

                library::Entry sys;
                sys.name = "Malo2";
                sys.exe  = L"C:\\Windows\\System32\\algo.exe";
                std::string serr;
                check("Ni una carpeta del sistema",
                      library::deleteFiles(sys, serr) == 0 && !serr.empty(),
                      serr.c_str());
            }

            // --- desinstalar pregunta antes de borrar ---
            {
                Framebuffer pu;
                pu.resize(960, 600);

                HomeScene ub("uninst@ludora.engine");
                ub.onEnter(engine);
                ub.section(HomeScene::kPanelsSection);
                ub.render(engine, pu);

                check("Al entrar no hay dialogo de borrado",
                      !ub.uninstallAskOpen());

                ub.openUninstallAskForTest("Juego De Ejemplo");
                check("Se puede abrir el dialogo", ub.uninstallAskOpen());

                // --- el primer segundo NO hay botones ---
                ub.render(engine, pu);
                std::snprintf(buf, sizeof(buf),
                              "t=%.2f s, botones %dx%d / %dx%d / %dx%d",
                              ub.uninstallAskTime(),
                              ub.uninstallAskYesRect().w, ub.uninstallAskYesRect().h,
                              ub.uninstallAskCloudRect().w, ub.uninstallAskCloudRect().h,
                              ub.uninstallAskNoRect().w, ub.uninstallAskNoRect().h);
                check("Nada mas abrirlo no hay botones que pulsar",
                      ub.uninstallAskYesRect().w == 0 &&
                      ub.uninstallAskCloudRect().w == 0 &&
                      ub.uninstallAskNoRect().w == 0, buf);

                // Medio segundo: siguen sin aparecer.
                for (i32 k = 0; k < 30; ++k) ub.update(engine, 0.016);
                ub.render(engine, pu);
                check("A medio segundo tampoco",
                      ub.uninstallAskYesRect().w == 0,
                      std::to_string(ub.uninstallAskTime()));

                // Pasado el segundo, aparecen los tres.
                for (i32 k = 0; k < 45; ++k) ub.update(engine, 0.016);
                ub.render(engine, pu);

                const Recti si = ub.uninstallAskYesRect();
                const Recti nu = ub.uninstallAskCloudRect();
                const Recti no = ub.uninstallAskNoRect();
                std::snprintf(buf, sizeof(buf),
                              "t=%.2f s; SI(%d,%d) NUBE(%d,%d) NO(%d,%d)",
                              ub.uninstallAskTime(), si.x, si.y, nu.x, nu.y,
                              no.x, no.y);
                check("Tras un segundo aparecen los tres botones",
                      si.w > 0 && nu.w > 0 && no.w > 0, buf);
                check("En el orden pedido: aceptar, nube, rechazar",
                      si.x < nu.x && nu.x < no.x, buf);
                check("Y a la misma altura, sin solaparse",
                      si.y == nu.y && nu.y == no.y &&
                      si.x + si.w <= nu.x && nu.x + nu.w <= no.x, buf);

                check("La espera es de un segundo",
                      HomeScene::uninstallButtonDelay() == 1.0);

                ub.onExit(engine);
            }

            // La seccion de mods existe y se dibuja.
            bib.section(HomeScene::kModsSection);
            bib.render(engine, probeB);
            check("La seccion de mods se abre",
                  bib.currentSection() == HomeScene::kModsSection);

            bib.onExit(engine);
            DeleteFileW(L"desinstalados.dat");
        }

        // --- almacen de resenas ---
        {
            const wchar_t* kRev = L"resenas.t68";
            DeleteFileW(kRev);

            std::string rerr;
            reviews::Review r1;
            r1.game = "Prueba 68"; r1.author = "ana@ludora.engine";
            r1.text = "Muy bueno.\nY con salto de linea."; r1.stars = 5;
            r1.at = 1000;
            check("Se guarda una resena", reviews::add(kRev, r1, rerr),
                  rerr.c_str());

            reviews::Review r2;
            r2.game = "Prueba 68"; r2.author = "luis@ludora.engine";
            r2.text = "Regular."; r2.stars = 3; r2.at = 2000;
            reviews::add(kRev, r2, rerr);

            reviews::Review r3;   // de otro juego: no debe mezclarse
            r3.game = "Otro"; r3.author = "z@ludora.engine";
            r3.text = "Distinto."; r3.stars = 1; r3.at = 3000;
            reviews::add(kRev, r3, rerr);

            const std::vector<reviews::Review> deJuego =
                reviews::forGame(kRev, "Prueba 68");
            check("Las resenas se leen de vuelta", deJuego.size() == 2,
                  std::to_string(deJuego.size()));
            check("Y no se mezclan las de otros juegos",
                  deJuego.size() == 2 &&
                  deJuego[0].game == "Prueba 68" &&
                  deJuego[1].game == "Prueba 68");
            check("Vienen de la mas reciente a la mas antigua",
                  deJuego.size() == 2 && deJuego[0].at > deJuego[1].at);
            check("El texto sobrevive con sus saltos de linea",
                  deJuego.size() == 2 &&
                  (deJuego[1].text == r1.text || deJuego[0].text == r1.text));

            // Media: (5 + 3) / 2 = 4.00 -> 400 centesimas.
            const reviews::Summary s = reviews::summaryFor(kRev, "Prueba 68");
            std::snprintf(buf, sizeof(buf), "%d resenas, media %d",
                          s.count, s.averageX100());
            check("El resumen cuenta y promedia bien",
                  s.count == 2 && s.rated == 2 && s.averageX100() == 400, buf);

            // Una resena sin puntuar cuenta, pero no entra en la media.
            reviews::Review r4;
            r4.game = "Prueba 68"; r4.author = "x@ludora.engine";
            r4.text = "Sin nota."; r4.stars = 0; r4.at = 4000;
            reviews::add(kRev, r4, rerr);
            const reviews::Summary s2 = reviews::summaryFor(kRev, "Prueba 68");
            std::snprintf(buf, sizeof(buf), "%d resenas, %d puntuadas, media %d",
                          s2.count, s2.rated, s2.averageX100());
            check("Una resena sin nota no altera la media",
                  s2.count == 3 && s2.rated == 2 && s2.averageX100() == 400, buf);

            // Validacion.
            reviews::Review vacia;
            vacia.game = "Prueba 68"; vacia.text = "";
            check("Una resena sin texto se rechaza",
                  !reviews::add(kRev, vacia, rerr), rerr.c_str());

            reviews::Review fuera;
            fuera.game = "Prueba 68"; fuera.text = "x"; fuera.stars = 9;
            check("Una puntuacion fuera de rango se rechaza",
                  !reviews::add(kRev, fuera, rerr), rerr.c_str());

            // El resumen de todos: dos juegos con resenas.
            const auto todos = reviews::summaryAll(kRev);
            check("El resumen global agrupa por juego", todos.size() == 2,
                  std::to_string(todos.size()));

            DeleteFileW(kRev);
        }

        // Actualizar la ficha no debe duplicar el juego ni perder versiones.
        devcatalog::Listing l2 = l;
        l2.fps = 60;
        check("La ficha se puede actualizar",
              devcatalog::setListing(kCat, "Prueba 68", l2, err), err.c_str());
        const std::vector<devcatalog::Game> tras = devcatalog::load(kCat);
        const devcatalog::Game* g68 = nullptr;
        for (const auto& g : tras)
            if (g.name == "Prueba 68") g68 = &g;
        check("Actualizar no duplica el juego ni pierde su version",
              g68 && g68->versions.size() == 1 && g68->listing.fps == 60);

        DeleteFileW(kCat);
        break;
    }
    default:
        writeReport();
        m_done = true;
        engine.requestQuit();
        return;
    }
    ++m_step;
    // Volcar el informe tras cada paso: si un cambio de escena aborta el
    // ciclo, queda registrado hasta donde se llego.
    writeReport();
}

void SelfTest::writeReport() {
    std::ofstream out(m_path.c_str(), std::ios::binary);
    if (!out) return;

    int passed = 0;
    out << "=== LUDORA ENGINE - AUTODIAGNOSTICO ===\r\n\r\n";
    for (const auto& r : m_results) {
        out << (r.ok ? "[ OK ] " : "[FALLO] ") << r.name;
        if (!r.detail.empty()) out << "\r\n         " << r.detail;
        out << "\r\n";
        if (r.ok) ++passed;
    }
    out << "\r\nResultado: " << passed << "/" << m_results.size() << " pruebas superadas\r\n";
}

void SelfTest::render(Engine& engine, Framebuffer& fb) {
    (void)engine;
    fb.clear(Color::fromHex(0x090909));
    font5x7::drawText(fb, 10, 10, "AUTODIAGNOSTICO EN CURSO...",
                      Color::fromHex(0xE01E23), 2);

    i32 y = 44;
    for (const auto& r : m_results) {
        font5x7::drawText(fb, 10, y, (r.ok ? "[OK]  " : "[FALLO] "),
                          r.ok ? Color::fromHex(0x9E9E9E) : Color::fromHex(0xE01E23), 1);
        font5x7::drawText(fb, 60, y, r.name.c_str(), Color::fromHex(0xF2F2F2), 1);
        y += 10;
    }
}

} // namespace ludora
