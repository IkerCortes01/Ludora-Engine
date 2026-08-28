#include "core/Engine.h"
#include "app/DemoScene.h"
#include "app/SelfTest.h"
#include "app/Screenshot.h"
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <memory>
#include <string>

using namespace ludora;

namespace {
/// true si la linea de comandos contiene el flag indicado.
bool hasFlag(const wchar_t* flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc && !found; ++i)
        found = (std::wstring(argv[i]) == flag);
    LocalFree(argv);
    return found;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // El manifest ya declara PerMonitorV2; esta llamada es el respaldo por si
    // el .exe se ejecuta sin manifest embebido.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    EngineConfig cfg;
    cfg.window.title          = L"Ludora Engine";
    cfg.window.width          = 960;
    cfg.window.height         = 600;
    cfg.window.minWidth       = 360;
    cfg.window.minHeight      = 240;
    cfg.window.borderless     = true;
    cfg.window.resizable      = true;
    cfg.window.dragBarHeight  = 36;
    cfg.window.resizeBorder   = 8;

    cfg.targetFPS      = 60;
    cfg.contentScale   = 1.0f;
    cfg.smoothScaling  = false;

    Engine engine;
    if (!engine.init(cfg)) {
        MessageBoxW(nullptr, L"No se pudo inicializar el motor.",
                    L"Ludora", MB_ICONERROR | MB_OK);
        return 1;
    }

    // --selftest: ejercita el motor solo, escribe el informe y sale.
    if (hasFlag(L"--selftest"))
        return engine.run(std::make_unique<SelfTest>(L"selftest-report.txt"));

    // --screenshot: vuelca el diseno a .bmp en varios estados y sale.
    if (hasFlag(L"--screenshot"))
        return engine.run(std::make_unique<Screenshot>());

    return engine.run(std::make_unique<DemoScene>());
}
