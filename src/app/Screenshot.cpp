#include "app/Screenshot.h"
#include "app/DemoScene.h"
#include "app/Account.h"
#include "app/HomeScene.h"
#include "core/Engine.h"
#include <algorithm>
#include <fstream>
#include <windows.h>

namespace ludora {

namespace {
constexpr f64 kStepDelay = 0.20;
constexpr i32 kShotCount = 4;
} // namespace

bool Screenshot::saveBmp(const Framebuffer& fb, const wchar_t* path) {
    if (!fb.valid()) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const i32 w = fb.width();
    const i32 h = fb.height();
    const u32 pixelBytes = static_cast<u32>(w) * h * 4;

    BITMAPFILEHEADER fh{};
    fh.bfType    = 0x4D42;                                  // "BM"
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + pixelBytes;

    BITMAPINFOHEADER ih{};
    ih.biSize      = sizeof(ih);
    ih.biWidth     = w;
    ih.biHeight    = -h;          // negativo = top-down, igual que el framebuffer
    ih.biPlanes    = 1;
    ih.biBitCount  = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = pixelBytes;

    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    out.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
    out.write(reinterpret_cast<const char*>(fb.data()), pixelBytes);
    return out.good();
}

void Screenshot::update(Engine& engine, f64 dt) {
    if (m_done) return;
    m_timer += dt;
    if (m_timer < kStepDelay) return;
    m_timer = 0.0;

    if (m_step >= kShotCount) {
        m_done = true;
        engine.requestQuit();
        return;
    }
    ++m_step;
}

void Screenshot::render(Engine& engine, Framebuffer& fb) {
    if (m_done) return;

    Window& win = engine.window();
    const i32 barH = win.desc().dragBarHeight;
    const i32 bw   = std::max(barH + barH / 2, 24);
    const i32 cy   = barH / 2;

    // Cada captura: raton, texto a teclear y archivo destino.
    struct Shot { i32 mx, my; const wchar_t* file; const char* email; const char* pass; };
    const Shot shots[kShotCount] = {
        { 400, 300,              L"shot-1-vacio.bmp",       "",                   "" },
        { 400, 300,              L"shot-2-escrito.bmp",     "iker@ludora.engine", "" },
        { fb.width() - bw/2, cy, L"shot-3-hover-close.bmp", "",                   "" },
        { 400, 300,              L"shot-4-home.bmp",        "iker@ludora.engine", "clave123" },
    };

    const Shot& s = shots[std::min(m_step, kShotCount - 1)];
    const bool esHome = (m_step >= kShotCount - 1);

    // Para la pantalla final se parte de cero: sin cuenta previa, para que el
    // formulario este en modo "crear" y el envio produzca el salto a HomeScene.
    if (esHome) DeleteFileW(L"cuenta.dat");

    engine.input().newFrame();
    SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(s.mx, s.my));

    DemoScene scene;
    scene.onEnter(engine);
    // Primer par update/render: fija la geometria antes de interactuar.
    scene.update(engine, 0.016);
    scene.render(engine, fb);

    // Teclear por WM_CHAR, el mismo camino que el teclado real. Un unico
    // update() consume el buffer; repetirlo sin newFrame() duplicaria el texto.
    auto teclear = [&](const char* txt) {
        engine.input().newFrame();
        for (const char* p = txt; *p; ++p)
            SendMessageW(win.handle(), WM_CHAR, static_cast<WPARAM>(*p), 0);
        scene.update(engine, 0.016);
    };

    if (s.email && *s.email) teclear(s.email);
    // Tras teclear, cualquier update() extra debe ir precedido de newFrame()
    // o el buffer de caracteres se insertaria de nuevo.
    engine.input().newFrame();

    if (esHome && s.pass && *s.pass) {
        // Tab al campo de contrasena, escribir y enviar con Enter.
        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYDOWN, VK_TAB, 0);
        scene.update(engine, 0.016);

        teclear(s.pass);

        engine.input().newFrame();
        SendMessageW(win.handle(), WM_KEYDOWN, VK_RETURN, 0);
        scene.update(engine, 0.016);   // aqui se encola HomeScene

        // El motor aplicaria el cambio al cerrar el frame; aqui se dibuja
        // HomeScene directamente para capturarla.
        scene.onExit(engine);
        HomeScene home(s.email);
        home.onEnter(engine);
        home.update(engine, 0.016);
        home.render(engine, fb);
        saveBmp(fb, s.file);

        // Segunda toma: clic en el bloque 5 para ver una seccion vacia y el
        // menu con otro bloque activo. El clic se inyecta por el WndProc real.
        {
            const i32 idx = 4;   // bloque "CINCO"
            const i32 x0 = (fb.width() * idx)       / HomeScene::kBlockCount;
            const i32 x1 = (fb.width() * (idx + 1)) / HomeScene::kBlockCount;
            const i32 bx = (x0 + x1) / 2;
            const i32 by = barH + 25;   // centro vertical de la franja azul

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, by));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            home.update(engine, 0.016);

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            home.update(engine, 0.016);   // aqui cambia de seccion

            home.render(engine, fb);
            saveBmp(fb, L"shot-5-seccion.bmp");
        }

        // Tercera toma: volver a BIBLIOTECA y desplazar el panel con la rueda,
        // para dejar constancia de que el scroll mueve el contenido.
        {
            const i32 x0 = (fb.width() * 2) / HomeScene::kBlockCount;
            const i32 x1 = (fb.width() * 3) / HomeScene::kBlockCount;
            const i32 bx = (x0 + x1) / 2;
            const i32 by = barH + 25;

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, by));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            home.update(engine, 0.016);

            // Rueda hacia abajo con el cursor sobre el panel izquierdo.
            for (int i = 0; i < 4; ++i) {
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(180, 300));
                SendMessageW(win.handle(), WM_MOUSEWHEEL,
                             MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                             MAKELPARAM(180, 300));
                home.update(engine, 0.016);
            }
            home.render(engine, fb);
            saveBmp(fb, L"shot-6-scroll.bmp");
        }

        // Cuarta toma: seccion TIENDA, arriba del todo y luego desplazada.
        {
            const i32 x1t = fb.width() / HomeScene::kBlockCount;
            const i32 bx = x1t / 2;
            const i32 by = barH + 25;

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, by));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            home.render(engine, fb);
            saveBmp(fb, L"shot-7-tienda.bmp");

            // Desplazar bastante para comprobar el recorrido largo.
            for (int i = 0; i < 12; ++i) {
                engine.input().newFrame();
                SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(480, 300));
                SendMessageW(win.handle(), WM_MOUSEWHEEL,
                             MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                             MAKELPARAM(480, 300));
                home.update(engine, 0.016);
            }
            home.render(engine, fb);
            saveBmp(fb, L"shot-8-tienda-scroll.bmp");
        }

        // Quinta toma: seccion CUENTA y su editor de descripcion.
        {
            const i32 idx = HomeScene::kAccountSection;
            const i32 x0 = (fb.width() * idx)       / HomeScene::kBlockCount;
            const i32 x1 = (fb.width() * (idx + 1)) / HomeScene::kBlockCount;
            const i32 bx = (x0 + x1) / 2;
            const i32 by = barH + 25;

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, by));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            // Alejar el raton para que no quede ningun boton resaltado.
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(480, 560));
            home.update(engine, 0.016);
            home.render(engine, fb);
            saveBmp(fb, L"shot-9-cuenta.bmp");
        }

        // Sexta toma: seccion CHAT (panel con scroll + rectangulo vino).
        {
            const i32 idx = HomeScene::kChatSection;
            const i32 x0 = (fb.width() * idx)       / HomeScene::kBlockCount;
            const i32 x1 = (fb.width() * (idx + 1)) / HomeScene::kBlockCount;
            const i32 bx = (x0 + x1) / 2;
            const i32 by = barH + 25;

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(bx, by));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
            home.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(200, 400));
            home.update(engine, 0.016);
            home.render(engine, fb);
            saveBmp(fb, L"shot-10-chat.bmp");
        }

        // Septima toma: panel de detalle del juego en la biblioteca.
        // Se registra un juego de prueba, se navega a Biblioteca y se
        // selecciona la primera casilla.
        {
            library::Entry e;
            e.name    = "Voxel World";
            e.vault   = L"juegos\\_demo.tomate";   // no hace falta que exista
            e.exeName = L"VoxelWorld.exe";
            library::add(L"biblioteca.dat", e);

            HomeScene h2("demo@ludora.engine");
            h2.onEnter(engine);
            h2.section(HomeScene::kPanelsSection);
            h2.render(engine, fb);   // fija la geometria de las casillas

            // Clic en la primera casilla de la biblioteca (panel izquierdo).
            const i32 barH2 = win.desc().dragBarHeight;
            const i32 casillaY = barH2 + 50 + 40;
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE,   0,          MAKELPARAM(120, casillaY));
            SendMessageW(win.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, casillaY));
            h2.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_LBUTTONUP, 0, MAKELPARAM(120, casillaY));
            h2.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_MOUSEMOVE, 0, MAKELPARAM(480, 560));
            h2.update(engine, 0.016);
            h2.render(engine, fb);
            saveBmp(fb, L"shot-11-detalle.bmp");

            h2.onExit(engine);
            DeleteFileW(L"biblioteca.dat");
        }

        // Octava toma: animacion de palomita verde al buscar cuenta con B.
        {
            // Cuenta de prueba en la carpeta de la app, para que B la encuentre.
            std::string aerr;
            DeleteFileW(L"cuenta.dat");
            Account::create(L"cuenta.dat", "encontrada@ludora.engine", "clave123", aerr);
            Account::forgetDevice(L"cuenta.dat", aerr);   // que B sea la unica via
            DeleteFileW(L"sesion.tomate");

            DemoScene dsc;
            dsc.onEnter(engine);
            dsc.render(engine, fb);

            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYDOWN, 'B', 0);
            dsc.update(engine, 0.016);
            engine.input().newFrame();
            SendMessageW(win.handle(), WM_KEYUP, 'B', 0);

            // Avanzar la animacion a ~70%: la palomita ya esta dibujada, pero
            // sin llegar al final (que saltaria a HomeScene).
            for (int i = 0; i < 22; ++i) dsc.update(engine, 0.05);
            dsc.render(engine, fb);
            saveBmp(fb, L"shot-12-palomita.bmp");

            dsc.onExit(engine);
            DeleteFileW(L"cuenta.dat");
            DeleteFileW(L"sesion.tomate");
        }

        home.onExit(engine);
        DeleteFileW(L"cuenta.dat");    // no dejar rastro de la prueba
        return;
    }

    scene.update(engine, 0.016);
    scene.render(engine, fb);
    scene.onExit(engine);

    saveBmp(fb, s.file);
}

} // namespace ludora
