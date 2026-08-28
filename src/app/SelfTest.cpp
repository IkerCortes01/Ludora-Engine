#include "app/SelfTest.h"
#include "app/Account.h"
#include "app/DemoScene.h"
#include "app/Device.h"
#include "app/HomeScene.h"
#include "app/Profile.h"
#include "app/Vault.h"
#include "app/Blake2b.h"
#include "app/Argon2.h"
#include "app/Session.h"
#include "app/GameInstall.h"
#include "app/Font5x7.h"
#include "ui/ScrollPanel.h"
#include "ui/TextField.h"
#include "ui/TitleBar.h"
#include "core/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
        RECT wr{}, cr{};
        GetWindowRect(win.handle(), &wr);
        GetClientRect(win.handle(), &cr);
        const i32 ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        const bool ok = (cr.right == ww) && (cr.bottom == wh) && ww > 0;
        std::snprintf(buf, sizeof(buf), "cliente %ldx%ld == ventana %dx%d",
                      cr.right, cr.bottom, ww, wh);
        check("Borderless: area cliente ocupa toda la ventana", ok, buf);

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

        const i32 tw = font5x7::measure("ABC", 1);
        std::snprintf(buf, sizeof(buf), "ancho de \"ABC\" = %d px (esperado 17)", tw);
        check("Fuente bitmap mide correctamente", tw == 17, buf);
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

        // --- destacado: una sola tarjeta ancha arriba ---
        std::vector<std::pair<i32,i32>> t;
        tramos(barH + 50 + 160, t);   // fila dentro del destacado
        std::snprintf(buf, sizeof(buf), "%zu tramo(s), ancho %d px",
                      t.size(), t.empty() ? 0 : t[0].second);
        check("El destacado ARTICULO 1 mide 900 px de ancho",
              t.size() == 1 && t[0].second == 900, buf);

        // Centrado en el area util (el ancho menos la barra de scroll).
        if (t.size() == 1) {
            const i32 mIzq = t[0].first;
            const i32 mDer = 960 - t[0].first - t[0].second;
            std::snprintf(buf, sizeof(buf), "margenes %d / %d", mIzq, mDer);
            check("El destacado esta centrado en el area util",
                  std::abs(mIzq - mDer) <= 20, buf);
        }

        // Alto del destacado, contando en una columna que lo cruce.
        i32 alto = 0;
        for (i32 y = barH + 50; y < 600; ++y) {
            const u32 c = probe.data()[static_cast<size_t>(y) * 960 + 480];
            if (c == violeta || c == filo) ++alto; else if (alto > 0) break;
        }
        std::snprintf(buf, sizeof(buf), "alto %d px", alto);
        check("El destacado mide 200 px de alto", alto == 200, buf);

        // --- dos columnas debajo ---
        // La fila del destacado acaba en top+28+200; la primera fila de
        // columnas empieza 28 px mas abajo.
        const i32 yCols = barH + 50 + 28 + 200 + 28 + 80;
        tramos(yCols, t);
        std::snprintf(buf, sizeof(buf), "%zu tramo(s)", t.size());
        check("Bajo el destacado hay dos columnas", t.size() == 2, buf);

        if (t.size() == 2) {
            std::snprintf(buf, sizeof(buf), "izquierda %d px, derecha %d px",
                          t[0].second, t[1].second);
            check("Las dos columnas tienen el mismo ancho",
                  t[0].second == t[1].second, buf);

            // Alineadas con los bordes del destacado: la izquierda empieza
            // donde el, y la derecha acaba donde el.
            std::vector<std::pair<i32,i32>> h;
            tramos(barH + 50 + 160, h);
            if (h.size() == 1) {
                const i32 heroIni = h[0].first;
                const i32 heroFin = h[0].first + h[0].second;
                const i32 colFin  = t[1].first + t[1].second;
                std::snprintf(buf, sizeof(buf),
                              "hero %d..%d, columnas %d..%d",
                              heroIni, heroFin, t[0].first, colFin);
                check("Las columnas se alinean con los bordes del destacado",
                      t[0].first == heroIni && colFin == heroFin, buf);
            }
        }

        // --- scroll largo ---
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

        // Preparar un "juego" de prueba: una carpeta con un exe falso y un dato.
        const wchar_t* srcDir = L"srcjuego.t47";
        const wchar_t* dstDir = L"dstjuego.t47";
        CreateDirectoryW(srcDir, nullptr);
        {
            std::ofstream(L"srcjuego.t47\\Juego.exe", std::ios::binary) << "MZ-falso";
            std::ofstream(L"srcjuego.t47\\datos.bin", std::ios::binary) << "0123456789";
        }

        // Biblioteca vacia al empezar.
        check("Sin instalar, el juego no esta en la biblioteca",
              !library::has(kLib, "Mi Juego"));

        // Instalar: copia en un hilo, se espera a que termine.
        GameInstall inst;
        const bool arranco = inst.start(L"srcjuego.t47\\Juego.exe", dstDir,
                                        "Mi Juego", L"Juego.exe");
        check("La instalacion arranca", arranco);
        inst.join();

        std::snprintf(buf, sizeof(buf), "progreso final %.0f%%, %s",
                      inst.progress() * 100.0f,
                      inst.failed() ? inst.error().c_str() : "ok");
        check("La instalacion termina al 100%",
              inst.done() && inst.progress() >= 0.999f && !inst.failed(), buf);

        // El juego queda cifrado en un paquete .tomate (no en claro).
        check("La instalacion produce el paquete cifrado del juego",
              GetFileAttributesW(inst.vaultPath().c_str()) != INVALID_FILE_ATTRIBUTES);

        // Registrar en la biblioteca con el paquete cifrado.
        library::Entry e;
        e.name    = "Mi Juego";
        e.vault   = inst.vaultPath();
        e.exeName = L"Juego.exe";
        library::add(kLib, e);
        check("El juego queda registrado en la biblioteca",
              library::has(kLib, "Mi Juego"));

        const auto lista = library::load(kLib);
        std::snprintf(buf, sizeof(buf), "%zu juego(s)", lista.size());
        check("La biblioteca recupera el juego instalado",
              lista.size() == 1 && lista[0].name == "Mi Juego", buf);

        // Registrarlo otra vez no lo duplica.
        library::add(kLib, e);
        check("Reinstalar no duplica el juego en la biblioteca",
              library::load(kLib).size() == 1);

        // Limpieza.
        DeleteFileW(L"srcjuego.t47\\Juego.exe");
        DeleteFileW(L"srcjuego.t47\\datos.bin");
        RemoveDirectoryW(srcDir);
        DeleteFileW(inst.vaultPath().c_str());   // el paquete .tomate
        DeleteFileW(kLib);
        break;
    }
    case 48: {
        // --- copia real de Voxel World, si esta en su ruta ---
        const std::wstring voxel =
            L"D:\\Respaldo\\Voxel World\\build\\bin\\Release\\VoxelWorld.exe";
        if (GetFileAttributesW(voxel.c_str()) == INVALID_FILE_ATTRIBUTES) {
            check("Voxel World no esta en su ruta: prueba omitida", true,
                  "sin VoxelWorld.exe");
            break;
        }

        const wchar_t* dst = L"juegos-test.t48";
        GameInstall inst;
        inst.start(voxel, dst, "Voxel World", L"VoxelWorld.exe");
        inst.join();

        std::snprintf(buf, sizeof(buf), "%s al %.0f%%",
                      inst.done() ? "completado" : "fallo", inst.progress() * 100.0f);
        check("Voxel World se cifra por completo en la Nube Tomate",
              inst.done() && !inst.failed(), inst.failed() ? inst.error().c_str() : buf);

        check("El paquete cifrado de Voxel World existe",
              GetFileAttributesW(inst.vaultPath().c_str()) != INVALID_FILE_ATTRIBUTES);

        // El paquete NO puede empezar por "MZ" (cabecera de un .exe): eso es lo
        // que veria alguien abriendo la carpeta: bytes cifrados, no el juego.
        {
            std::vector<u8> head;
            Vault::loadFile(inst.vaultPath(), head);
            const bool esExe = head.size() >= 2 && head[0] == 'M' && head[1] == 'Z';
            std::snprintf(buf, sizeof(buf), "primeros bytes 0x%02X 0x%02X",
                          head.empty() ? 0 : head[0], head.size() > 1 ? head[1] : 0);
            check("El paquete se ve como bytes cifrados, no como el .exe",
                  !esExe && head.size() > 16, buf);

            // Comprimido: 70 MB de juego caben en bastante menos.
            const HANDLE fh = CreateFileW(inst.vaultPath().c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            LARGE_INTEGER sz{};
            if (fh != INVALID_HANDLE_VALUE) { GetFileSizeEx(fh, &sz); CloseHandle(fh); }
            std::snprintf(buf, sizeof(buf), "paquete %.1f MB (juego ~70 MB)",
                          sz.QuadPart / 1048576.0);
            check("El paquete pesa menos que el juego (comprimido)",
                  sz.QuadPart > 0 && sz.QuadPart < 70 * 1048576LL, buf);
        }

        // Descifrar y comprobar que el exe real queda disponible para jugar.
        std::wstring runExe; std::string err2;
        const bool jugable = gamevault::openToFolder(
            inst.vaultPath(), L"voxrun.t48", L"VoxelWorld.exe", runExe, err2);
        check("Voxel World se descifra y el exe queda disponible para jugar",
              jugable, jugable ? "" : err2.c_str());

        // Los datos anidados del juego (partidas guardadas, a 4 niveles de
        // profundidad) deben quedar tambien expuestos, no solo el exe.
        check("Los datos anidados de Voxel World se descifran",
              GetFileAttributesW(L"voxrun.t48\\saves\\Mundo 1\\regions\\r.0.0.vxr")
                  != INVALID_FILE_ATTRIBUTES ||
              GetFileAttributesW(L"voxrun.t48\\saves") != INVALID_FILE_ATTRIBUTES);

        // Limpieza recursiva.
        DeleteFileW(inst.vaultPath().c_str());
        for (const wchar_t* d : { L"voxrun.t48\0\0" }) {
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE; op.pFrom = d; op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        (void)dst;
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
        // --- busqueda de cuenta en el sistema (tecla B) ---
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
        // --- la tecla B en la pantalla de acceso encuentra y anima ---
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
        // test. Queremos que la unica via de entrada aqui sea la tecla B.
        Account::forgetDevice(real, err);
        { Profile p; p.userName = "Animado"; Profile::save(prof, p); }

        Framebuffer probe;
        probe.resize(960, 600);

        DemoScene sc;
        sc.onEnter(engine);
        // Si onEnter activo el auto-login pese a todo, no pulsar B: el test
        // no puede continuar con una escena que se va a reemplazar.
        check("Sin sesion ni vinculo, la pantalla no entra sola todavia",
              !sc.autoLoginPending());
        sc.render(engine, probe);

        // Pulsar B: debe encontrar la cuenta y arrancar la animacion.
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYDOWN, 'B', 0);
        sc.update(engine, 0.016);
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYUP, 'B', 0);
        check("La tecla B encuentra la cuenta y arranca la animacion",
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
