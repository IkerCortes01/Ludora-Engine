#include "app/HomeScene.h"
#include "app/DemoScene.h"
#include "app/Font5x7.h"
#include "app/GameInstall.h"
#include "app/Session.h"
#include "core/Engine.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <windows.h>

namespace ludora {

using namespace theme;

namespace {
// Grosor de las barras, en pixeles logicos.
constexpr i32 kTopBarThickness  = 50;   // franja superior azul (menu)
constexpr i32 kSideBarThickness = 350;  // franjas rojas laterales (ambos lados)

// Bloques del menu dentro de la franja azul.
constexpr i32 kBlockGap    = 6;   // hueco entre bloques
constexpr i32 kBlockMargin = 6;   // margen vertical dentro de la franja

// Contenido de ejemplo del panel de biblioteca.
constexpr i32 kLibraryItems = 12;   // suficientes para que haya scroll
constexpr i32 kItemH        = 64;
constexpr i32 kItemPad      = 10;
constexpr i32 kScrollBarW   = 14;

// Tienda: un articulo destacado arriba y el resto en dos columnas.
constexpr i32 kStoreHeroW   = 900;  // ARTICULO 1, siempre el primero
constexpr i32 kStoreHeroH   = 200;
constexpr i32 kStoreCardH   = 160;  // tarjetas de las columnas
constexpr i32 kStoreCards   = 24;   // total, incluido el destacado
constexpr i32 kStoreGap     = 28;   // hueco entre tarjetas
constexpr i32 kStoreTopPad  = 28;

// Seccion Chat: panel con scroll a la izquierda, rectangulo vino a la derecha.
constexpr i32 kChatWineW = 870;   // ancho del rectangulo vino
constexpr i32 kChatMsgs  = 40;    // lineas de ejemplo, suficientes para scroll
constexpr i32 kChatLineH = 22;    // alto de cada linea de mensaje
constexpr i32 kChatPad   = 10;

// Seccion Cuenta: fichas largas y poco altas.
constexpr i32 kAccRowW     = 820;   // largas
constexpr i32 kAccRowH     = 72;    // poco altas
constexpr i32 kAccPad      = 16;
constexpr i32 kAccPhotoH   = 128;   // ficha del usuario, con foto al lado
constexpr i32 kAccPhotoSide= 96;
constexpr i32 kAccBtnW     = 90;
constexpr i32 kAccBtnH     = 26;
constexpr i32 kAccDescBtn  = 96;    // boton cuadrado

const wchar_t* kProfileFile = L"perfil.dat";
// Biblioteca de juegos instalados y rutas de Voxel World.
const wchar_t* kLibraryFile = L"biblioteca.dat";
const wchar_t* kVoxelExe    = L"D:\\Respaldo\\Voxel World\\build\\bin\\Release\\VoxelWorld.exe";
// Carpeta real de la galeria de juegos descargados. Ahi vive cada juego como
// un paquete .tomate cifrado (visible como bytes ilegibles en el Explorador).
const wchar_t* kVoxelDest   = L"D:\\Respaldo\\Ludora\\Juegos descargados\\VoxelWorld";
/// Carpeta temporal donde se descifra un juego para ejecutarlo.
const wchar_t* kGameTempDir = L"D:\\Respaldo\\Ludora\\Juegos descargados\\_run\\VoxelWorld";
const wchar_t* kPhotoFile   = L"foto-perfil.bmp";

// Avatar circular de la esquina inferior derecha.
constexpr i32 kAvatarRadius = 42;
constexpr i32 kAvatarMargin = 22;

/// Nombre de cada seccion. La tercera es la que muestra los dos paneles.
const char* kSectionNames[HomeScene::kBlockCount] = {
    "TIENDA", "CHAT", "BIBLIOTECA", "LOGROS", "ESTADISTICAS & HORAS", "CUENTA"
};
} // namespace

HomeScene::HomeScene(std::string sessionEmail) : m_email(std::move(sessionEmail)) {}

void HomeScene::onEnter(Engine& engine) {
    m_titleBar.attach(engine);

    // Perfil guardado: nombre, descripcion y foto.
    m_profileLoaded = Profile::load(kProfileFile, m_profile);

    TextField::Style st{};
    st.bg            = kFieldBg;
    st.bgFocused     = kFieldBgFocus;
    st.border        = kFieldBorder;
    st.borderFocused = kAccent;
    st.text          = kText;
    st.placeholder   = kDim;
    st.caret         = kAccent;
    st.selection     = kSelection;
    st.textScale     = 2;
    m_descField.setStyle(st);
    m_descField.setPlaceholder("escribe tu descripcion publica");
    m_descField.setMaxLength(Profile::kMaxDescription);
    // Barra pegada al borde izquierdo de la ventana, que es donde esta la
    // "pared" en este panel.
    m_leftPanel.setBarOnLeft(true);
    m_leftPanel.setBarWidth(kScrollBarW);
}
void HomeScene::onExit(Engine& engine)  { m_titleBar.detach(engine); }

Recti HomeScene::blockRect(Engine& engine, i32 fbWidth, i32 i) const {
    const i32 barY = m_titleBar.heightLogical(engine);

    // Los seis bloques se reparten el ancho a partes iguales. El reparto se
    // hace con los bordes calculados, no con un ancho fijo por bloque: asi el
    // ultimo llega siempre al borde derecho y no queda un sobrante visible.
    const i32 x0 = (fbWidth * i)       / kBlockCount;
    const i32 x1 = (fbWidth * (i + 1)) / kBlockCount;

    const i32 x = x0 + kBlockGap / 2;
    const i32 w = (x1 - x0) - kBlockGap;

    return Recti{ x, barY + kBlockMargin, std::max(1, w),
                  kTopBarThickness - kBlockMargin * 2 };
}

i32 HomeScene::blockAt(Engine& engine, i32 fbWidth, i32 lx, i32 ly) const {
    for (i32 i = 0; i < kBlockCount; ++i) {
        const Recti r = blockRect(engine, fbWidth, i);
        if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h)
            return i;
    }
    return -1;
}

void HomeScene::update(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    // Cerrar sesion solicitado: volver a la pantalla de acceso. Diferido con
    // queueScene porque estamos dentro del update() de esta escena.
    if (m_wantLogout) {
        m_wantLogout = false;
        engine.queueScene(std::make_unique<DemoScene>());
        return;
    }

    // Al terminar de instalar, registrar el juego en la biblioteca. Se hace
    // aqui, fuera de la seccion Tienda, para que se complete aunque el usuario
    // navegue a otra pestana mientras se instala.
    if (m_install.done() && m_installStarted) {
        library::Entry e;
        e.name    = "Voxel World";
        e.vault   = m_install.vaultPath();   // paquete cifrado
        e.exeName = L"VoxelWorld.exe";
        library::add(kLibraryFile, e);
        m_libraryLoaded  = false;   // forzar recarga de la casilla de biblioteca
        m_installStarted = false;
        m_install.join();
    }

    // Juego en ejecucion: avanzar el reloj de la animacion y comprobar si ya
    // se cerro. WAIT_OBJECT_0 en 0 ms = el proceso termino.
    if (m_runningProc) {
        m_playAnimTime += dt;
        if (WaitForSingleObject(m_runningProc, 0) == WAIT_OBJECT_0) {
            CloseHandle(m_runningProc);
            m_runningProc = nullptr;
        }
    }

    // El editor de descripcion se superpone: mientras este abierto se queda
    // con todo el teclado y el raton, salvo los botones de ventana.
    if (m_descOpen) {
        m_titleBar.update(engine);
        updateDescriptionEditor(engine, dt);
        return;
    }

    if (in.keyPressed(VK_ESCAPE)) {
        // En las subsecciones (Resenas, Modo Desarrollador) ESC vuelve atras;
        // en el resto, cierra la aplicacion.
        if (m_section == kReviewsSection) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
        } else if (m_section == kDevSection) {
            m_section = kAccountSection;
        } else {
            engine.requestQuit();
        }
        return;
    }

    // Si el raton esta sobre un boton de ventana, el clic es suyo.
    if (m_titleBar.update(engine)) { m_hovered = -1; return; }

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);
    const i32 fbw = (m_lastFbWidth > 0) ? m_lastFbWidth
                                        : engine.renderer().fb().width();

    // La franja del menu (los 6 bloques) siempre tiene prioridad de clic, en
    // cualquier seccion. Sin esto, un panel que empieza justo debajo del menu
    // se quedaba con clics que caian en su franja de X y bloqueaba el cambio
    // de pestana (bug al estar en Chat y pulsar Tienda). Se resuelve aqui, una
    // sola vez, antes de que ninguna seccion atienda su propio panel.
    {
        const i32 barY = m_titleBar.heightLogical(engine);
        const i32 menuBot = barY + kTopBarThickness;
        if (ly >= barY && ly < menuBot) {
            m_hovered = blockAt(engine, fbw, lx, ly);
            if (in.mousePressed(0)) m_pressed = m_hovered;
            if (in.mouseReleased(0)) {
                if (m_pressed >= 0 && m_pressed == m_hovered) {
                    m_section = m_pressed;
                    // Al cambiar de seccion, soltar estados de arrastre que
                    // pudieran quedar pegados de la seccion anterior.
                    m_chatSelecting = false;
                }
                m_pressed = -1;
            }
            return;   // el clic era del menu; ninguna seccion lo toca
        }
    }

    // Los paneles con scroll se atienden antes que el menu: si se esta
    // arrastrando una barra, el raton no debe activar bloques al pasar.
    if (m_section == kStoreSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutStore(engine, fbw, fbh);

        // Interaccion del boton INSTALAR.
        const f32 sc = std::max(0.01f, engine.window().contentScale());
        const i32 lx = static_cast<i32>(engine.input().mousePos().x / sc);
        const i32 ly = static_cast<i32>(engine.input().mousePos().y / sc);
        m_installHover = (lx >= m_installBtnRect.x && lx < m_installBtnRect.x + m_installBtnRect.w &&
                          ly >= m_installBtnRect.y && ly < m_installBtnRect.y + m_installBtnRect.h);

        if (engine.input().mousePressed(0) && m_installHover) m_installDown = true;
        if (engine.input().mouseReleased(0)) {
            if (m_installDown && m_installHover &&
                !m_install.running() && !library::has(kLibraryFile, "Voxel World")) {
                // Copiar la carpeta de Voxel World a la carpeta de juegos.
                m_install.start(kVoxelExe, kVoxelDest, "Voxel World", L"VoxelWorld.exe");
                m_installStarted = true;
            }
            m_installDown = false;
            if (m_installHover) { m_hovered = -1; m_pressed = -1; return; }
        }

        if (m_storePanel.update(engine) || m_storePanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            return;
        }
    }

    if (m_section == kChatSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutChat(engine, fbw, fbh);
        // El scroll tiene prioridad; si no lo consume el raton, va a la
        // seleccion de mensajes.
        if (m_chatPanel.update(engine) || m_chatPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            m_chatSelecting = false;
            return;
        }
        updateChat(engine, dt);
        // Un clic dentro del panel de chat no debe cambiar de pestana.
        const f32 sc = std::max(0.01f, engine.window().contentScale());
        const i32 lx = static_cast<i32>(engine.input().mousePos().x / sc);
        const Recti cc = m_chatPanel.contentRect();
        if (lx >= cc.x && lx < cc.x + cc.w) { m_hovered = -1; m_pressed = -1; return; }
    }

    if (m_section == kAccountSection) {
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        layoutAccount(engine, fbw, fbh);
        updateAccount(engine, dt);
        if (m_accountPanel.update(engine) || m_accountPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            return;
        }
        // Un clic sobre un boton de la seccion no debe cambiar de pestana.
        if (m_accountHover >= 0) { m_hovered = -1; m_pressed = -1; return; }
    }

    if (m_section == kPanelsSection) {
        // La geometria se fija aqui tambien: update() corre antes que render(),
        // y en el primer frame el panel aun no tendria rectangulo.
        const i32 fbh = (m_lastFbHeight > 0) ? m_lastFbHeight
                                             : engine.renderer().fb().height();
        const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
        const i32 w   = std::min(kSideBarThickness, fbw / 2);
        m_leftPanel.setRect({0, top, w, std::max(0, fbh - top)});
        m_leftPanel.setContentHeight(kItemPad + kLibraryItems * (kItemH + kItemPad));

        // Casilla de biblioteca bajo el raton (solo las de juegos instalados).
        if (!m_libraryLoaded) { m_libraryGames = library::load(kLibraryFile); m_libraryLoaded = true; }
        const Recti cc = m_leftPanel.contentRect();
        const i32 off  = m_leftPanel.offset();
        m_libraryHover = -1;
        for (i32 i = 0; i < static_cast<i32>(m_libraryGames.size()); ++i) {
            const i32 iy = cc.y + kItemPad + i * (kItemH + kItemPad) - off;
            if (ly >= iy && ly < iy + kItemH && ly >= cc.y && ly < cc.y + cc.h &&
                lx >= cc.x && lx < cc.x + cc.w)
                m_libraryHover = i;
        }

        // Clic en una casilla instalada: seleccionarla para el panel de detalle.
        if (in.mouseReleased(0) && m_libraryHover >= 0 &&
            m_libraryHover < static_cast<i32>(m_libraryGames.size())) {
            m_librarySelected = m_libraryHover;
            m_accountMsg.clear();   // limpiar un aviso de un juego anterior
        }

        // --- botones del panel de detalle: Jugar y Resenas ---
        m_playHover = (lx >= m_playBtnRect.x && lx < m_playBtnRect.x + m_playBtnRect.w &&
                       ly >= m_playBtnRect.y && ly < m_playBtnRect.y + m_playBtnRect.h);
        m_reviewsHover = (lx >= m_reviewsBtnRect.x && lx < m_reviewsBtnRect.x + m_reviewsBtnRect.w &&
                          ly >= m_reviewsBtnRect.y && ly < m_reviewsBtnRect.y + m_reviewsBtnRect.h);

        if (in.mousePressed(0)) {
            if (m_playHover)    m_playDown = true;
            if (m_reviewsHover) m_reviewsDown = true;
        }
        if (in.mouseReleased(0)) {
            const bool tieneJuego = (m_librarySelected >= 0 &&
                m_librarySelected < static_cast<i32>(m_libraryGames.size()));
            if (m_playDown && m_playHover && tieneJuego && !m_runningProc) {
                // Descifra el juego a una carpeta temporal y lo ejecuta. Se
                // guarda el handle del proceso para animar el boton mientras
                // el juego siga abierto.
                std::string err;
                void* proc = nullptr;
                if (library::launchEntry(m_libraryGames[m_librarySelected],
                                         kGameTempDir, err, &proc)) {
                    m_runningProc  = proc;
                    m_playAnimTime = 0.0;
                } else {
                    m_accountMsg    = err;   // se muestra el motivo del fallo
                    m_accountMsgErr = true;
                }
            }
            if (m_reviewsDown && m_reviewsHover && tieneJuego) {
                m_reviewsFrom = kPanelsSection;   // para volver aqui
                m_section = kReviewsSection;
            }
            m_playDown = m_reviewsDown = false;
        }

        if (m_leftPanel.update(engine) || m_leftPanel.dragging()) {
            m_hovered = -1;
            m_pressed = -1;
            return;
        }
        if (m_libraryHover >= 0 || m_playHover || m_reviewsHover) {
            m_hovered = -1; m_pressed = -1; return;
        }
    }

    // --- seccion Resenas: scroll y boton volver ---
    if (m_section == kReviewsSection) {
        const i32 top2 = m_titleBar.heightLogical(engine) + kTopBarThickness;
        // Volver con ESC o con el boton "< VOLVER" (esquina superior derecha).
        if (in.keyPressed(VK_ESCAPE)) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
            return;
        }
        const bool enVolver = (ly >= top2 && ly < top2 + 30 && lx > fbw - 120);
        if (in.mouseReleased(0) && enVolver) {
            m_section = (m_reviewsFrom >= 0) ? m_reviewsFrom : kPanelsSection;
            return;
        }
        m_reviewsPanel.update(engine);
        return;
    }

    if (m_section == kDevSection) {
        const i32 top2 = m_titleBar.heightLogical(engine) + kTopBarThickness;
        // Volver con ESC o el boton "< VOLVER" de la esquina superior derecha.
        const bool enVolver = (ly >= top2 && ly < top2 + 30 && lx > fbw - 140);
        if (in.keyPressed(VK_ESCAPE) || (in.mouseReleased(0) && enVolver)) {
            m_section = kAccountSection;   // se vuelve a Cuenta
            return;
        }
        return;
    }

    m_hovered = blockAt(engine, fbw, lx, ly);

    if (in.mousePressed(0)) m_pressed = m_hovered;

    if (in.mouseReleased(0)) {
        // Solo cambia de seccion si se suelta sobre el mismo bloque donde se
        // pulso: arrastrar fuera cancela, como en cualquier boton.
        if (m_pressed >= 0 && m_pressed == m_hovered)
            m_section = m_pressed;
        m_pressed = -1;
    }
}

void HomeScene::drawMenu(Engine& engine, Framebuffer& fb) {
    const i32 barY = m_titleBar.heightLogical(engine);

    // Franja azul de fondo: los bloques se recortan sobre ella.
    fb.fillRect({0, barY, fb.width(), kTopBarThickness}, kDarkBlue);

    for (i32 i = 0; i < kBlockCount; ++i) {
        const Recti r = blockRect(engine, fb.width(), i);

        const bool activo  = (i == m_section);
        const bool pulsado = (m_pressed == i && m_hovered == i);
        const bool sobre   = (m_hovered == i && m_pressed < 0) || pulsado;

        // El bloque activo se marca en rojo para que se vea donde estas.
        Color fondo = kBlueDivider;
        if (activo)       fondo = kLedRed;
        else if (pulsado) fondo = kClosePress;
        else if (sobre)   fondo = kPressed;

        fb.fillRect(r, fondo);
        if (activo || sobre)
            fb.drawRect(r, activo ? kWhite : kLedRed, 1);

        // Etiqueta centrada, recortada si el bloque es estrecho.
        const char* nombre = kSectionNames[i];
        const i32 tw = font5x7::measure(nombre, 1);
        if (tw + 6 <= r.w) {
            font5x7::drawText(fb, r.x + (r.w - tw) / 2,
                              r.y + (r.h - font5x7::kGlyphH) / 2,
                              nombre, (activo || sobre) ? kWhite : kGlyph, 1);
        } else {
            // No cabe el texto: un numero identifica igual el bloque.
            const char num[2] = { static_cast<char>('1' + i), '\0' };
            const i32 nw = font5x7::measure(num, 1);
            if (nw + 2 <= r.w)
                font5x7::drawText(fb, r.x + (r.w - nw) / 2,
                                  r.y + (r.h - font5x7::kGlyphH) / 2,
                                  num, (activo || sobre) ? kWhite : kGlyph, 1);
        }
    }
}

void HomeScene::drawLibraryPanel(Framebuffer& fb) {
    const Recti p = m_leftPanel.rect();
    if (p.w <= 0 || p.h <= 0) return;

    fb.fillRect(p, kPanelDeep);

    // Recargar la lista de juegos instalados si cambio.
    if (!m_libraryLoaded) {
        m_libraryGames = library::load(kLibraryFile);
        m_libraryLoaded = true;
    }

    const Recti c   = m_leftPanel.contentRect();
    const i32   off = m_leftPanel.offset();

    // Primero los juegos instalados (casillas reales), luego huecos vacios.
    const i32 nInstalados = static_cast<i32>(m_libraryGames.size());
    for (i32 i = 0; i < kLibraryItems; ++i) {
        const i32 y = c.y + kItemPad + i * (kItemH + kItemPad) - off;

        // Recorte manual al area visible: el framebuffer no tiene clip por
        // region, asi que sin esto las fichas desplazadas invadirian el menu.
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + kItemH, c.y + c.h);
        if (y1 <= y0) continue;

        const i32 x = c.x + kItemPad;
        const i32 w = std::max(1, c.w - kItemPad * 2);

        const bool instalado = (i < nInstalados);
        const bool hover     = (m_libraryHover == i);
        fb.fillRect({x, y0, w, y1 - y0},
                    instalado ? (hover ? kAccent : kPanelViolet) : kPanelDeep);

        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 2}, instalado ? kOk : kPanelEdge);

        const i32 ty = y + (kItemH - font5x7::kGlyphH * 2) / 2;
        if (ty >= c.y && ty + font5x7::kGlyphH * 2 <= c.y + c.h) {
            if (instalado) {
                // Casilla del juego: nombre y "jugar".
                font5x7::drawTextUtf8(fb, x + 12, ty,
                                      m_libraryGames[i].name.c_str(),
                                      hover ? kWhite : kText, 2);
                if (ty + 22 + font5x7::kGlyphH <= c.y + c.h)
                    font5x7::drawText(fb, x + 12, ty + 22, "> JUGAR",
                                      hover ? kWhite : kOk, 1);
            } else {
                font5x7::drawText(fb, x + 12, ty, "(vacio)", kDim, 1);
            }
        }
    }

    m_leftPanel.renderBar(fb);
}

void HomeScene::layoutStore(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    // El panel ocupa todo el ancho para que su barra quede pegada a la pared
    // derecha de la ventana; las tarjetas se centran dentro.
    m_storePanel.setRect({0, top, fbW, std::max(0, fbH - top)});
    m_storePanel.setBarWidth(kScrollBarW);

    // Alto total: el destacado mas las filas de dos columnas. Las 23 tarjetas
    // restantes se reparten en dos columnas, de ahi el redondeo hacia arriba.
    const i32 restantes = kStoreCards - 1;
    const i32 filas     = (restantes + 1) / 2;
    m_storePanel.setContentHeight(kStoreTopPad + kStoreHeroH + kStoreGap +
                                  filas * (kStoreCardH + kStoreGap));
}

void HomeScene::drawInstallBar(Framebuffer& fb) {
    // Se muestra mientras hay una instalacion en curso, en cualquier seccion.
    if (!m_install.running() &&
        !(m_install.done() && m_install.progress() < 1.0f))
        return;

    const i32 barH = 8;
    const i32 by   = fb.height() - barH;
    fb.fillRect({0, by, fb.width(), barH}, kFieldBg);
    const i32 w = static_cast<i32>(fb.width() * m_install.progress());
    fb.fillRect({0, by, w, barH}, kAccent);

    char pct[32];
    std::snprintf(pct, sizeof(pct), "Instalando Voxel World  %d%%",
                  static_cast<i32>(m_install.progress() * 100.0f + 0.5f));
    font5x7::drawText(fb, 8, by - 14, pct, kWhite, 1);
}

void HomeScene::drawStore(Engine& engine, Framebuffer& fb) {
    (void)engine;
    const Recti c   = m_storePanel.contentRect();
    const i32   off = m_storePanel.offset();
    if (c.w <= 0 || c.h <= 0) { m_storePanel.renderBar(fb); return; }

    /// Dibuja una tarjeta recortada al area visible del panel. `escala` ajusta
    /// el tamano del titulo, mas grande en el destacado.
    auto tarjeta = [&](i32 x, i32 y, i32 w, i32 h, i32 num, i32 escala) {
        // Sin clip por region en el framebuffer, el recorte se hace aqui: una
        // tarjeta desplazada invadiria el menu de arriba.
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 <= y0 || w <= 0) return;

        fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 3}, kPanelEdge);

        char label[40];
        // El destacado (num == 1) es Voxel World; el resto, articulos genericos.
        if (num == 1) std::snprintf(label, sizeof(label), "Voxel World");
        else          std::snprintf(label, sizeof(label), "ARTICULO %d", num);

        // Reducir la escala hasta que el titulo quepa: con tarjetas estrechas
        // una escala fija desbordaria el borde derecho.
        const i32 margen = std::min<i32>(24, w / 8);
        i32 esc = escala;
        while (esc > 1 && font5x7::measure(label, esc) > w - margen * 2)
            --esc;

        const i32 ty = y + 24;
        if (ty >= c.y && ty + font5x7::kGlyphH * esc <= c.y + c.h)
            font5x7::drawText(fb, x + margen, ty, label, kText, esc);

        char sub[48];
        std::snprintf(sub, sizeof(sub), "%d x %d", w, h);
        const i32 sy = y + 24 + font5x7::kGlyphH * esc + 12;
        if (sy >= c.y && sy + font5x7::kGlyphH <= c.y + c.h &&
            font5x7::measure(sub, 1) <= w - margen * 2)
            font5x7::drawText(fb, x + margen, sy, sub, kDim, 1);
    };

    // --- destacado: ARTICULO 1, arriba del todo y a lo ancho ---
    // Margen minimo de 8 px por lado: con kStoreGap (28) una tarjeta de 900 no
    // cabria en una ventana de 960, y se recortaria sin que se note el motivo.
    const i32 heroW = std::min(kStoreHeroW, c.w - 16);
    const i32 heroX = c.x + (c.w - heroW) / 2;
    const i32 heroY = c.y + kStoreTopPad - off;
    tarjeta(heroX, heroY, heroW, kStoreHeroH, 1, 4);

    // --- boton INSTALAR dentro del destacado, abajo ---
    const bool yaInstalado = library::has(kLibraryFile, "Voxel World");
    const bool instalando  = m_install.running();

    const i32 bw = std::min(200, heroW / 3);
    const i32 bh = 34;
    m_installBtnRect = Recti{ heroX + heroW - bw - 20,
                              heroY + kStoreHeroH - bh - 16, bw, bh };
    if (m_installBtnRect.y >= c.y && m_installBtnRect.y + m_installBtnRect.h <= c.y + c.h) {
        const char* txt;
        Color fondo;
        if (yaInstalado)      { txt = "INSTALADO"; fondo = kFieldBg; }
        else if (instalando)  { txt = "INSTALANDO"; fondo = kFieldBgFocus; }
        else {
            txt = "INSTALAR";
            fondo = (m_installDown && m_installHover) ? kClosePress
                  : (m_installHover ? kAccent : kFieldBg);
        }
        fb.fillRect(m_installBtnRect, fondo);
        fb.drawRect(m_installBtnRect,
                    yaInstalado ? kOk : (m_installHover ? kAccent : kBorder), 2);
        const i32 tw = font5x7::measure(txt, 1);
        font5x7::drawText(fb, m_installBtnRect.x + (m_installBtnRect.w - tw) / 2,
                          m_installBtnRect.y + (m_installBtnRect.h - font5x7::kGlyphH) / 2,
                          txt, yaInstalado ? kOk : (m_installHover ? kWhite : kText), 1);
    }


    // --- resto: dos columnas bajo el destacado, alineadas con sus bordes ---
    const i32 colW = (heroW - kStoreGap) / 2;
    const i32 colLx = heroX;                       // columna izquierda
    const i32 colRx = heroX + heroW - colW;        // columna derecha
    const i32 filaY0 = heroY + kStoreHeroH + kStoreGap;

    for (i32 i = 1; i < kStoreCards; ++i) {
        const i32 idx  = i - 1;          // 0-based dentro de las columnas
        const i32 fila = idx / 2;
        const bool derecha = (idx % 2) != 0;

        const i32 x = derecha ? colRx : colLx;
        const i32 y = filaY0 + fila * (kStoreCardH + kStoreGap);
        tarjeta(x, y, colW, kStoreCardH, i + 1, 2);
    }

    m_storePanel.renderBar(fb);
}

void HomeScene::saveProfile() {
    Profile::save(kProfileFile, m_profile);
}

void HomeScene::ensurePhotoLoaded() {
    if (m_photoTried) return;
    m_photoTried = true;
    if (m_profile.photoFile.empty()) return;

    // La foto ya se normalizo a BMP al importarla.
    if (!photo::loadBmp(kPhotoFile, m_photoPixels, m_photoW, m_photoH)) {
        m_photoPixels.clear();
        m_photoW = m_photoH = 0;
    }
}

void HomeScene::drawAvatar(Framebuffer& fb) {
    ensurePhotoLoaded();

    const i32 r  = kAvatarRadius;
    const i32 cx = fb.width()  - r - kAvatarMargin;
    const i32 cy = fb.height() - r - kAvatarMargin;

    // Fondo del disco: si no hay foto, queda como hueco con las iniciales.
    fb.fillCircle(cx, cy, r, kPanelDeep);

    if (!m_photoPixels.empty()) {
        // Se deja 3 px libres para que el anillo no tape la imagen.
        fb.blitCircular(cx, cy, r - 3, m_photoPixels.data(), m_photoW, m_photoH);
    } else {
        // Inicial del nombre de usuario como marcador de posicion.
        const char* u = m_profile.userName.c_str();
        char ini[2] = { u[0] ? u[0] : '?', '\0' };
        const i32 tw = font5x7::measure(ini, 3);
        font5x7::drawText(fb, cx - tw / 2, cy - font5x7::kGlyphH * 3 / 2,
                          ini, kTurquoise, 3);
    }

    // Anillo turquesa: doble linea, la exterior mas tenue, para que el borde
    // se lea como un aro y no como una silueta recortada.
    fb.drawCircle(cx, cy, r,     kTurquoise, 3);
    fb.drawCircle(cx, cy, r + 4, Color{kTurquoise.r, kTurquoise.g, kTurquoise.b, 90}, 1);
}

void HomeScene::layoutAccount(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    m_accountPanel.setRect({0, top, fbW, std::max(0, fbH - top)});
    m_accountPanel.setBarWidth(kScrollBarW);

    // Foto (ficha alta) + tres fichas largas + boton de cerrar sesion.
    const i32 total = kAccPad + kAccPhotoH + kAccPad +
                      3 * (kAccRowH + kAccPad) +
                      40 + kAccPad * 2;   // boton CERRAR SESION
    m_accountPanel.setContentHeight(total);
}

void HomeScene::drawAccount(Engine& engine, Framebuffer& fb) {
    (void)engine;
    const Recti c   = m_accountPanel.contentRect();
    const i32   off = m_accountPanel.offset();
    if (c.w <= 0 || c.h <= 0) { m_accountPanel.renderBar(fb); return; }

    const i32 w = std::min(kAccRowW, c.w - kAccPad * 2);
    const i32 x = c.x + (c.w - w) / 2;

    /// Ficha con etiqueta y valor, recortada al area visible.
    auto ficha = [&](i32 y, i32 h, const char* etiqueta, const char* valor,
                     bool oculto) -> Recti {
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 <= y0) return Recti{x, y, w, h};

        fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
        if (y >= c.y && y < c.y + c.h)
            fb.fillRect({x, y, w, 2}, kPanelEdge);

        const i32 ey = y + 12;
        if (ey >= c.y && ey + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawText(fb, x + 16, ey, etiqueta, kDim, 1);

        const i32 vy = y + 30;
        if (vy >= c.y && vy + font5x7::kGlyphH * 2 <= c.y + c.h) {
            if (oculto) {
                // La contrasena no se muestra nunca, ni siquiera enmascarada
                // con su longitud real: eso ya filtraria informacion.
                font5x7::drawText(fb, x + 16, vy, "**********", kDim, 2);
            } else {
                font5x7::drawTextUtf8(fb, x + 16, vy, valor, kText, 2);
            }
        }
        return Recti{x, y, w, h};
    };

    i32 y = c.y + kAccPad - off;

    // --- 1) Nombre de usuario, con foto y botones al lado ---
    {
        const i32 h = kAccPhotoH;
        const i32 y0 = std::max(y, c.y);
        const i32 y1 = std::min(y + h, c.y + c.h);
        if (y1 > y0) {
            fb.fillRect({x, y0, w, y1 - y0}, kPanelViolet);
            if (y >= c.y && y < c.y + c.h)
                fb.fillRect({x, y, w, 2}, kPanelEdge);
        }

        // Recuadro de la foto, a la izquierda de la ficha.
        m_photoRect = Recti{x + 16, y + 16, kAccPhotoSide, kAccPhotoSide};
        const i32 py0 = std::max(m_photoRect.y, c.y);
        const i32 py1 = std::min(m_photoRect.y + m_photoRect.h, c.y + c.h);
        if (py1 > py0) {
            fb.fillRect({m_photoRect.x, py0, m_photoRect.w, py1 - py0}, kPanelDeep);
            fb.drawRect({m_photoRect.x, m_photoRect.y, m_photoRect.w, m_photoRect.h},
                        kPanelEdge, 1);

            // Vista previa circular de la foto dentro del recuadro.
            ensurePhotoLoaded();
            if (!m_photoPixels.empty() &&
                m_photoRect.y >= c.y &&
                m_photoRect.y + m_photoRect.h <= c.y + c.h) {
                fb.blitCircular(m_photoRect.x + m_photoRect.w / 2,
                                m_photoRect.y + m_photoRect.h / 2,
                                m_photoRect.w / 2 - 4,
                                m_photoPixels.data(), m_photoW, m_photoH);
                fb.drawCircle(m_photoRect.x + m_photoRect.w / 2,
                              m_photoRect.y + m_photoRect.h / 2,
                              m_photoRect.w / 2 - 4, kTurquoise, 2);
            }

            if (m_profile.photoFile.empty()) {
                const char* sin = "SIN FOTO";
                const i32 sw = font5x7::measure(sin, 1);
                const i32 sy = m_photoRect.y + (m_photoRect.h - font5x7::kGlyphH) / 2;
                if (sy >= c.y && sy + font5x7::kGlyphH <= c.y + c.h)
                    font5x7::drawText(fb, m_photoRect.x + (m_photoRect.w - sw) / 2,
                                      sy, sin, kDim, 1);
            }
        }

        // Nombre de usuario a la derecha de la foto.
        const i32 tx = m_photoRect.x + m_photoRect.w + 16;
        const i32 ey = y + 16;
        if (ey >= c.y && ey + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawText(fb, tx, ey, "NOMBRE DE USUARIO", kDim, 1);
        const i32 vy = y + 34;
        if (vy >= c.y && vy + font5x7::kGlyphH * 2 <= c.y + c.h)
            font5x7::drawTextUtf8(fb, tx, vy, m_profile.userName.c_str(), kText, 2);

        // Botones de foto: galeria y camara.
        const i32 by = y + 66;
        m_galleryRect = Recti{tx, by, kAccBtnW, kAccBtnH};
        m_cameraRect  = Recti{tx + kAccBtnW + 10, by, kAccBtnW, kAccBtnH};

        auto boton = [&](const Recti& r, const char* txt, i32 id) {
            if (r.y < c.y || r.y + r.h > c.y + c.h) return;
            const bool hov = (m_accountHover == id);
            const bool dwn = (m_accountDown == id && hov);
            fb.fillRect(r, dwn ? kClosePress : (hov ? kPanelEdge : kPanelDeep));
            fb.drawRect(r, hov ? kAccent : kPanelEdge, 1);
            const i32 tw = font5x7::measure(txt, 1);
            font5x7::drawText(fb, r.x + (r.w - tw) / 2,
                              r.y + (r.h - font5x7::kGlyphH) / 2,
                              txt, hov ? kWhite : kGlyph, 1);
        };
        boton(m_galleryRect, "GALERIA", 0);
        boton(m_cameraRect,  "CAMARA",  1);

        // Boton cuadrado de la descripcion publica.
        m_descBtnRect = Recti{x + w - kAccDescBtn - 16, y + 16, kAccDescBtn, kAccDescBtn};
        if (m_descBtnRect.y >= c.y && m_descBtnRect.y + m_descBtnRect.h <= c.y + c.h) {
            const bool hov = (m_accountHover == 2);
            const bool dwn = (m_accountDown == 2 && hov);
            fb.fillRect(m_descBtnRect, dwn ? kClosePress : (hov ? kPanelEdge : kPanelDeep));
            fb.drawRect(m_descBtnRect, hov ? kAccent : kPanelEdge, 1);
            // Etiqueta en tres lineas para que quepa en un cuadrado.
            const char* l1 = "DESCRIPCION";
            const char* l2 = "DE CUENTA";
            const char* l3 = "EN PUBLICO";
            const i32 cy0 = m_descBtnRect.y + m_descBtnRect.h / 2 - 16;
            const Color col = hov ? kWhite : kGlyph;
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l1,1))/2, cy0,      l1, col, 1);
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l2,1))/2, cy0 + 11, l2, col, 1);
            font5x7::drawText(fb, m_descBtnRect.x + (m_descBtnRect.w - font5x7::measure(l3,1))/2, cy0 + 22, l3, col, 1);
        }

        y += h + kAccPad;
    }

    // --- 2) Cuenta (correo) ---
    ficha(y, kAccRowH, "CUENTA", m_email.c_str(), false);
    y += kAccRowH + kAccPad;

    // --- 3) Contrasena, siempre oculta ---
    ficha(y, kAccRowH, "CONTRASENA", "", true);
    y += kAccRowH + kAccPad;

    // --- 4) Descripcion publica, en una linea ---
    ficha(y, kAccRowH, "DESCRIPCION PUBLICA",
          m_profile.description.empty() ? "(vacia)" : m_profile.description.c_str(),
          false);
    y += kAccRowH + kAccPad;

    // --- 5) Cerrar sesion ---
    // Deshace la sesion recordada: la proxima apertura pedira credenciales.
    m_logoutRect = Recti{x, y, std::min(240, w), 40};
    {
        const bool hov = (m_accountHover == 3);
        const bool dwn = (m_accountDown == 3 && hov);
        const i32 y0 = std::max(m_logoutRect.y, c.y);
        const i32 y1 = std::min(m_logoutRect.y + m_logoutRect.h, c.y + c.h);
        if (y1 > y0) {
            fb.fillRect({m_logoutRect.x, y0, m_logoutRect.w, y1 - y0},
                        dwn ? kClosePress : (hov ? kAccent : kFieldBg));
            fb.drawRect(m_logoutRect, hov ? kAccent : kBorder, 2);
            const char* t = "CERRAR SESION";
            const i32 tw = font5x7::measure(t, 1);
            const i32 ty = m_logoutRect.y + (m_logoutRect.h - font5x7::kGlyphH) / 2;
            if (ty >= c.y && ty + font5x7::kGlyphH <= c.y + c.h)
                font5x7::drawText(fb, m_logoutRect.x + (m_logoutRect.w - tw) / 2, ty,
                                  t, hov ? kWhite : kDim, 1);
        }
    }

    // --- 6) Modo Desarrollador, junto a Cerrar sesion ---
    // Borde y texto laten entre turquesa y morado con una onda continua.
    m_devBtnRect = Recti{ m_logoutRect.x + m_logoutRect.w + 16, y,
                          std::min(260, w - m_logoutRect.w - 16), 40 };
    if (m_devBtnRect.w > 40) {
        const i32 y0 = std::max(m_devBtnRect.y, c.y);
        const i32 y1 = std::min(m_devBtnRect.y + m_devBtnRect.h, c.y + c.h);
        if (y1 > y0) {
            // Mezcla turquesa<->morado segun una onda seno del reloj.
            const f32 t = 0.5f + 0.5f * std::sin(static_cast<f32>(m_devAnimTime) * 2.2f);
            auto mix = [&](u8 a, u8 b) { return static_cast<u8>(a + (b - a) * t); };
            const Color animColor{ mix(kTurquoise.r, kDevPurple.r),
                                   mix(kTurquoise.g, kDevPurple.g),
                                   mix(kTurquoise.b, kDevPurple.b) };
            const bool hov = (m_accountHover == 4);
            const bool dwn = (m_accountDown == 4 && hov);

            fb.fillRect({m_devBtnRect.x, y0, m_devBtnRect.w, y1 - y0},
                        dwn ? kClosePress : (hov ? animColor : kFieldBg));
            fb.drawRect(m_devBtnRect, animColor, 2);

            const char* dt = "MODO DESARROLLADOR";
            const i32 dtw = font5x7::measure(dt, 1);
            const i32 dty = m_devBtnRect.y + (m_devBtnRect.h - font5x7::kGlyphH) / 2;
            if (dty >= c.y && dty + font5x7::kGlyphH <= c.y + c.h)
                font5x7::drawText(fb, m_devBtnRect.x + (m_devBtnRect.w - dtw) / 2, dty,
                                  dt, hov ? kWhite : animColor, 1);
        }
    }

    m_accountPanel.renderBar(fb);

    if (!m_accountMsg.empty()) {
        const i32 mw = font5x7::measure(m_accountMsg.c_str(), 1);
        font5x7::drawText(fb, (fb.width() - mw) / 2, fb.height() - 24,
                          m_accountMsg.c_str(),
                          m_accountMsgErr ? kAccent : kOk, 1);
    }
}

void HomeScene::drawDescriptionEditor(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    // Velo sobre la seccion para que se lea como un panel superpuesto.
    fb.fillRect({0, top, fb.width(), fb.height() - top}, Color{0, 0, 0, 190});

    const i32 w = std::min(700, fb.width() - 80);
    const i32 h = std::min(320, fb.height() - top - 60);
    const i32 x = (fb.width() - w) / 2;
    const i32 y = top + (fb.height() - top - h) / 2;

    fb.fillRect({x, y, w, h}, kPanelDeep);
    fb.drawRect({x, y, w, h}, kAccent, 2);

    font5x7::drawText(fb, x + 20, y + 18, "DESCRIPCION DE CUENTA EN PUBLICO", kText, 2);

    char info[80];
    std::snprintf(info, sizeof(info), "%zu / %zu caracteres",
                  m_descField.length(), Profile::kMaxDescription);
    font5x7::drawText(fb, x + 20, y + 44, info, kDim, 1);

    m_descField.setRect({x + 20, y + 64, w - 40, 60});
    m_descField.render(fb);

    // Boton de guardar, abajo.
    m_descSaveRect = Recti{x + w - 180 - 20, y + h - 46, 180, 34};
    fb.fillRect(m_descSaveRect,
                (m_descSaveDown && m_descSaveHover) ? kClosePress
                                                    : (m_descSaveHover ? kAccent : kPanelViolet));
    fb.drawRect(m_descSaveRect, m_descSaveHover ? kAccent : kPanelEdge, 2);
    const char* txt = "GUARDAR";
    const i32 tw = font5x7::measure(txt, 2);
    font5x7::drawText(fb, m_descSaveRect.x + (m_descSaveRect.w - tw) / 2,
                      m_descSaveRect.y + (m_descSaveRect.h - font5x7::kGlyphH * 2) / 2,
                      txt, kWhite, 2);
}

void HomeScene::updateDescriptionEditor(Engine& engine, f64 dt) {
    Input& in = engine.input();
    m_descField.update(dt);

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    m_descSaveHover = (lx >= m_descSaveRect.x && lx < m_descSaveRect.x + m_descSaveRect.w &&
                       ly >= m_descSaveRect.y && ly < m_descSaveRect.y + m_descSaveRect.h);

    if (in.mousePressed(0)) {
        if (m_descField.contains(lx, ly)) {
            m_descField.setFocused(true);
            m_descField.onMouseDown(lx, ly);
        } else if (m_descSaveHover) {
            m_descSaveDown = true;
        }
    }
    if (in.mouseDown(0) && m_descField.focused() && !m_descSaveDown)
        m_descField.onMouseDrag(lx, ly);

    if (in.mouseReleased(0)) {
        if (m_descSaveDown && m_descSaveHover) {
            // Guardar y cerrar el panel: vuelve a la seccion Cuenta.
            m_profile.description = m_descField.text();
            saveProfile();
            m_descOpen = false;
            m_accountMsg    = "Descripcion guardada.";
            m_accountMsgErr = false;
        }
        m_descSaveDown = false;
    }

    // --- teclado ---
    if (in.keyPressed(VK_ESCAPE)) { m_descOpen = false; return; }

    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrl  = in.keyDown(VK_CONTROL);
    const bool altGr = ctrl && in.keyDown(VK_MENU);
    const bool ctrlAtajo = ctrl && !altGr;

    static const u32 kEditKeys[] = {
        VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_BACK, VK_DELETE, 'A', 'C', 'X', 'V'
    };
    for (u32 vk : kEditKeys) {
        const bool isLetter = (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'V');
        if (isLetter && !ctrlAtajo) continue;
        if (in.keyPressed(vk)) m_descField.onKey(vk, shift, ctrlAtajo);
    }
    if (!ctrlAtajo) {
        for (wchar_t ch : in.charsTyped()) {
            if (ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'\b') continue;
            m_descField.onChar(ch);
        }
    }
}

void HomeScene::updateAccount(Engine& engine, f64 dt) {
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    auto dentro = [&](const Recti& r) {
        return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
    };

    // La animacion del boton de desarrollador avanza siempre que se ve Cuenta.
    m_devAnimTime += dt;

    m_accountHover = -1;
    if      (dentro(m_galleryRect)) m_accountHover = 0;
    else if (dentro(m_cameraRect))  m_accountHover = 1;
    else if (dentro(m_descBtnRect)) m_accountHover = 2;
    else if (dentro(m_logoutRect))  m_accountHover = 3;
    else if (dentro(m_devBtnRect))  m_accountHover = 4;

    if (in.mousePressed(0)) m_accountDown = m_accountHover;

    if (in.mouseReleased(0)) {
        if (m_accountDown >= 0 && m_accountDown == m_accountHover) {
            std::string err;
            switch (m_accountDown) {
            case 0: {   // galeria
                const std::wstring src = photo::pickFromGallery(engine.window().handle());
                if (!src.empty()) {
                    photo::ContentCheck cc;
                    if (photo::importImage(src, kPhotoFile, err, &cc)) {
                        m_profile.photoFile = "foto-perfil.bmp";
                        saveProfile();
                        m_photoTried = false;   // forzar recarga del avatar
                        m_accountMsg = "Foto actualizada.";
                        m_accountMsgErr = false;
                    } else {
                        // Se muestra el motivo real: si el filtro de contenido
                        // la rechaza, el usuario debe saber por que y poder
                        // elegir otra o pedir revision.
                        m_accountMsg = err;
                        m_accountMsgErr = true;
                    }
                }
                break;
            }
            case 1: {   // camara
                if (photo::captureFromCamera(kPhotoFile, err)) {
                    // La captura pasa por el mismo control que la galeria:
                    // si no, bastaria usar la camara para saltarselo.
                    std::vector<u32> px; i32 pw = 0, ph = 0;
                    photo::ContentCheck cc;
                    if (photo::loadBmp(kPhotoFile, px, pw, ph))
                        cc = photo::checkImageContent(px, pw, ph);

                    if (!cc.allowed) {
                        DeleteFileW(kPhotoFile);
                        m_accountMsg    = cc.reason;
                        m_accountMsgErr = true;
                    } else {
                        m_profile.photoFile = "foto-perfil.bmp";
                        saveProfile();
                        m_photoTried = false;   // forzar recarga del avatar
                        m_accountMsg = "Foto capturada.";
                        m_accountMsgErr = false;
                    }
                } else {
                    m_accountMsg = err;
                    m_accountMsgErr = true;
                }
                break;
            }
            case 2:     // descripcion publica
                m_descField.setText(m_profile.description);
                m_descField.setFocused(true);
                m_descOpen = true;
                break;
            case 3:     // cerrar sesion
                // Borra la sesion recordada y vuelve a la pantalla de acceso,
                // que ahora si pedira credenciales. La cuenta no se toca.
                Session::clear(Session::kFile);
                m_wantLogout = true;
                break;
            case 4:     // modo desarrollador
                m_section = kDevSection;
                break;
            default: break;
            }
        }
        m_accountDown = -1;
    }
}

void HomeScene::drawGameDetail(Engine& engine, Framebuffer& fb, Recti area) {
    (void)engine;
    if (area.w <= 0 || area.h <= 0) return;

    fb.fillRect(area, kBg);   // fondo del panel central

    // Sin juego seleccionado: invitacion a elegir uno.
    if (m_librarySelected < 0 ||
        m_librarySelected >= static_cast<i32>(m_libraryGames.size())) {
        const char* msg = "Selecciona un juego de la biblioteca";
        const i32 mw = font5x7::measure(msg, 1);
        font5x7::drawText(fb, area.x + (area.w - mw) / 2,
                          area.y + area.h / 2, msg, kDim, 1);
        return;
    }

    const library::Entry& g = m_libraryGames[m_librarySelected];

    // --- imagen del juego ---
    // Marco 16:9 centrado arriba. Si el juego trae una portada BMP se muestra;
    // si no, un recuadro con el nombre como marcador.
    const i32 imgW = std::min(area.w - 48, 480);
    const i32 imgH = imgW * 9 / 16;
    const i32 imgX = area.x + (area.w - imgW) / 2;
    const i32 imgY = area.y + 30;

    fb.fillRect({imgX, imgY, imgW, imgH}, kPanelDeep);
    fb.drawRect({imgX, imgY, imgW, imgH}, kPanelEdge, 2);

    // Portada: <carpeta-del-exe>\portada.bmp, si existe.
    std::wstring cover = g.exe;
    const size_t s = cover.find_last_of(L"\\/");
    cover = (s == std::wstring::npos ? L"." : cover.substr(0, s)) + L"\\portada.bmp";
    std::vector<u32> px; i32 pw = 0, ph = 0;
    if (photo::loadBmp(cover, px, pw, ph) && pw > 0 && ph > 0) {
        // Encajar centrado dentro del marco (con 4 px de margen).
        for (i32 yy = 0; yy < imgH - 8; ++yy)
            for (i32 xx = 0; xx < imgW - 8; ++xx) {
                const i32 sx = xx * pw / (imgW - 8);
                const i32 sy = yy * ph / (imgH - 8);
                fb.setPixel(imgX + 4 + xx, imgY + 4 + yy, px[static_cast<size_t>(sy) * pw + sx]);
            }
    } else {
        const i32 tw = font5x7::measure(g.name.c_str(), 3);
        font5x7::drawTextUtf8(fb, imgX + (imgW - tw) / 2,
                              imgY + (imgH - font5x7::kGlyphH * 3) / 2,
                              g.name.c_str(), kText, 3);
    }

    // --- titulo ---
    const i32 tw = font5x7::measureUtf8(g.name.c_str(), 3);
    font5x7::drawTextUtf8(fb, area.x + (area.w - tw) / 2, imgY + imgH + 20,
                          g.name.c_str(), kText, 3);

    // --- botones JUGAR y RESENAS ---
    const i32 bw = 150, bh = 44, gap = 20;
    const i32 by = imgY + imgH + 60;
    const i32 bx = area.x + (area.w - (bw * 2 + gap)) / 2;

    m_playBtnRect    = Recti{ bx, by, bw, bh };
    m_reviewsBtnRect = Recti{ bx + bw + gap, by, bw, bh };

    auto boton = [&](const Recti& r, const char* txt, bool hov, bool down, Color acc) {
        fb.fillRect(r, down ? kClosePress : (hov ? acc : kFieldBg));
        fb.drawRect(r, hov ? acc : kBorder, 2);
        const i32 lw = font5x7::measure(txt, 2);
        font5x7::drawText(fb, r.x + (r.w - lw) / 2,
                          r.y + (r.h - font5x7::kGlyphH * 2) / 2,
                          txt, hov ? kWhite : kText, 2);
    };
    // Boton JUGAR: si el juego esta corriendo, se anima con una secuencia de
    // puntos ( .. -> . -> .. -> ... ) en vez del texto normal.
    if (m_runningProc) {
        // Cuatro fotogramas de la secuencia, a ~3 por segundo.
        static const char* kFrames[4] = { "..", ".", "..", "..." };
        const int f = static_cast<int>(m_playAnimTime * 3.0) % 4;
        // Fondo animado (pulso) para que se note que esta activo.
        fb.fillRect(m_playBtnRect, kFieldBgFocus);
        fb.drawRect(m_playBtnRect, kOk, 2);
        const char* jug = "JUGANDO";
        const i32 jw = font5x7::measure(jug, 2);
        const i32 dw = font5x7::measure("...", 2);   // reserva ancho fijo
        const i32 tot = jw + 6 + dw;
        const i32 tx = m_playBtnRect.x + (m_playBtnRect.w - tot) / 2;
        const i32 ty = m_playBtnRect.y + (m_playBtnRect.h - font5x7::kGlyphH * 2) / 2;
        font5x7::drawText(fb, tx, ty, jug, kOk, 2);
        font5x7::drawText(fb, tx + jw + 6, ty, kFrames[f], kWhite, 2);
    } else {
        boton(m_playBtnRect, "JUGAR", m_playHover, m_playDown, kOk);
    }
    boton(m_reviewsBtnRect, "RESENAS", m_reviewsHover, m_reviewsDown, kAccent);

    // Aviso (p. ej. si el juego no se pudo descifrar/lanzar).
    if (!m_accountMsg.empty() && m_accountMsgErr) {
        const i32 mw = font5x7::measure(m_accountMsg.c_str(), 1);
        if (mw < area.w)
            font5x7::drawText(fb, area.x + (area.w - mw) / 2, by + bh + 16,
                              m_accountMsg.c_str(), kAccent, 1);
    }
}

void HomeScene::drawReviews(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    fb.fillRect({0, top, fb.width(), fb.height() - top}, kBg);

    // Cabecera con el nombre del juego y un boton para volver.
    std::string titulo = "RESENAS";
    if (m_librarySelected >= 0 &&
        m_librarySelected < static_cast<i32>(m_libraryGames.size()))
        titulo = "RESENAS - " + m_libraryGames[m_librarySelected].name;
    font5x7::drawTextUtf8(fb, 24, top + 16, titulo.c_str(), kText, 2);

    const char* volver = "< VOLVER";
    const i32 vw = font5x7::measure(volver, 1);
    font5x7::drawText(fb, fb.width() - vw - 24, top + 20, volver, kDim, 1);

    // Panel con scroll para las resenas (vacio por ahora).
    const i32 pTop = top + 48;
    m_reviewsPanel.setRect({0, pTop, fb.width(), fb.height() - pTop});
    m_reviewsPanel.setBarWidth(kScrollBarW);
    m_reviewsPanel.setContentHeight(fb.height() * 2);   // deja recorrido de scroll

    fb.fillRect({0, pTop, fb.width(), fb.height() - pTop}, kPanelDeep);
    const char* msg = "Aun no hay resenas de jugadores.";
    const i32 mw = font5x7::measure(msg, 1);
    font5x7::drawText(fb, (fb.width() - mw) / 2, pTop + 40, msg, kDim, 1);

    m_reviewsPanel.renderBar(fb);
}

void HomeScene::layoutChat(Engine& engine, i32 fbW, i32 fbH) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    // Rectangulo vino de 870 px pegado a la derecha; el panel con scroll ocupa
    // lo que queda a la izquierda. Se acota para que en una ventana estrecha
    // no se solapen.
    const i32 wineW = std::min(kChatWineW, fbW - kScrollBarW - 40);
    const i32 leftW = fbW - wineW;

    m_chatPanel.setRect({0, top, std::max(0, leftW), std::max(0, fbH - top)});
    m_chatPanel.setBarOnLeft(true);   // barra pegada a la pared izquierda
    m_chatPanel.setBarWidth(kScrollBarW);
    // Las lineas dan altura para el scroll.
    m_chatPanel.setContentHeight(kChatPad * 2 + kChatMsgs * kChatLineH);
}

namespace {
/// Texto de una linea del chat. Placeholder hasta tener el chat real.
std::string chatLine(i32 i) {
    char b[48];
    std::snprintf(b, sizeof(b), "Mensaje de ejemplo numero %d", i + 1);
    return b;
}
i32 chatSelLo(i32 a, i32 b) { return a < b ? a : b; }
i32 chatSelHi(i32 a, i32 b) { return a < b ? b : a; }
} // namespace

void HomeScene::updateChat(Engine& engine, f64 dt) {
    (void)dt;
    Input& in = engine.input();

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    const Recti c   = m_chatPanel.contentRect();
    const i32   off = m_chatPanel.offset();

    // Convierte una y de pantalla en indice de linea de chat.
    auto lineaEn = [&](i32 y) -> i32 {
        if (y < c.y || y >= c.y + c.h) return -1;
        const i32 rel = y - (c.y + kChatPad) + off;
        if (rel < 0) return -1;
        const i32 idx = rel / kChatLineH;
        return (idx >= 0 && idx < kChatMsgs) ? idx : -1;
    };

    const bool dentro = (lx >= c.x && lx < c.x + c.w);

    if (in.mousePressed(0) && dentro) {
        const i32 l = lineaEn(ly);
        if (l >= 0) {
            m_chatSelStart = m_chatSelEnd = l;
            m_chatSelecting = true;
        } else {
            m_chatSelStart = m_chatSelEnd = -1;   // clic en vacio: deselecciona
        }
    }

    if (m_chatSelecting && in.mouseDown(0)) {
        const i32 l = lineaEn(ly);
        if (l >= 0) m_chatSelEnd = l;
    }

    if (in.mouseReleased(0)) m_chatSelecting = false;

    // Ctrl+C copia la seleccion.
    if (in.keyDown(VK_CONTROL) && in.keyPressed('C'))
        copyChatSelection();
}

void HomeScene::copyChatSelection() {
    if (m_chatSelStart < 0 || m_chatSelEnd < 0) return;

    std::string texto;
    const i32 lo = chatSelLo(m_chatSelStart, m_chatSelEnd);
    const i32 hi = chatSelHi(m_chatSelStart, m_chatSelEnd);
    for (i32 i = lo; i <= hi; ++i) {
        texto += chatLine(i);
        if (i < hi) texto += "\r\n";
    }

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = texto.size() + 1;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, texto.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void HomeScene::drawChat(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;

    const i32 wineW = std::min(kChatWineW, fb.width() - kScrollBarW - 40);
    const i32 leftW = fb.width() - wineW;

    // --- panel izquierdo: mensajes como texto suelto y seleccionable ---
    fb.fillRect({0, top, leftW, fb.height() - top}, kPanelDeep);

    const Recti c   = m_chatPanel.contentRect();
    const i32   off = m_chatPanel.offset();

    const i32 selLo = (m_chatSelStart >= 0) ? chatSelLo(m_chatSelStart, m_chatSelEnd) : -1;
    const i32 selHi = (m_chatSelStart >= 0) ? chatSelHi(m_chatSelStart, m_chatSelEnd) : -1;

    for (i32 i = 0; i < kChatMsgs; ++i) {
        const i32 y = c.y + kChatPad + i * kChatLineH - off;
        if (y + kChatLineH <= c.y || y >= c.y + c.h) continue;   // fuera de vista

        const i32 x = c.x + 12;

        // Resalte de la linea seleccionada (recortado al area visible).
        const bool sel = (i >= selLo && i <= selHi);
        if (sel) {
            const i32 y0 = std::max(y, c.y);
            const i32 y1 = std::min(y + kChatLineH, c.y + c.h);
            if (y1 > y0)
                fb.fillRect({c.x, y0, c.w, y1 - y0}, kSelection);
        }

        const i32 ty = y + (kChatLineH - font5x7::kGlyphH) / 2;
        if (ty >= c.y && ty + font5x7::kGlyphH <= c.y + c.h)
            font5x7::drawTextUtf8(fb, x, ty, chatLine(i).c_str(),
                                  sel ? kWhite : kText, 1);
    }
    m_chatPanel.renderBar(fb);

    // --- rectangulo vino de 870 px, pegado a la pared derecha ---
    fb.fillRect({fb.width() - wineW, top, wineW, fb.height() - top}, kWine);
    fb.fillRect({fb.width() - wineW, top, 2, fb.height() - top}, kWineEdge);
}

void HomeScene::drawSection(Engine& engine, Framebuffer& fb) {
    const i32 top = m_titleBar.heightLogical(engine) + kTopBarThickness;
    const i32 h   = fb.height() - top;
    if (h <= 0) return;

    if (m_section == kStoreSection) {
        layoutStore(engine, fb.width(), fb.height());
        drawStore(engine, fb);
        return;
    }

    if (m_section == kChatSection) {
        layoutChat(engine, fb.width(), fb.height());
        drawChat(engine, fb);
        return;
    }

    if (m_section == kAccountSection) {
        layoutAccount(engine, fb.width(), fb.height());
        drawAccount(engine, fb);
        return;
    }

    if (m_section == kReviewsSection) {
        drawReviews(engine, fb);
        return;
    }

    if (m_section == kDevSection) {
        // Seccion Modo Desarrollador: vacia por ahora, con un boton para volver.
        fb.fillRect({0, top, fb.width(), h}, kBg);
        const char* titulo = "MODO DESARROLLADOR";
        const i32 tw = font5x7::measure(titulo, 3);
        font5x7::drawText(fb, (fb.width() - tw) / 2, top + h / 2 - 30,
                          titulo, kDevPurple, 3);
        const char* sub = "(en construccion)";
        const i32 sw = font5x7::measure(sub, 1);
        font5x7::drawText(fb, (fb.width() - sw) / 2, top + h / 2 + 6, sub, kDim, 1);
        const char* volver = "< VOLVER (ESC)";
        const i32 vw = font5x7::measure(volver, 1);
        font5x7::drawText(fb, fb.width() - vw - 24, top + 20, volver, kDim, 1);
        return;
    }

    if (m_section == kPanelsSection) {
        const i32 w = std::min(kSideBarThickness, fb.width() / 2);

        // Izquierda: panel violeta con scroll. Derecha: se mantiene en rojo.
        m_leftPanel.setRect({0, top, w, h});
        m_leftPanel.setContentHeight(kItemPad + kLibraryItems * (kItemH + kItemPad));
        drawLibraryPanel(fb);

        // Centro (antes fondo negro): detalle del juego seleccionado, con su
        // imagen y los botones Jugar / Resenas.
        const Recti centro{ w, top, fb.width() - 2 * w, h };
        drawGameDetail(engine, fb, centro);

        fb.fillRect({fb.width() - w, top, w, h}, kLedRed);
        return;
    }

    // Las demas secciones estan vacias a proposito: solo se indica cual es,
    // para que se note que la navegacion funciona.
    const char* nombre = kSectionNames[m_section];
    const i32 tw = font5x7::measure(nombre, 3);
    font5x7::drawText(fb, (fb.width() - tw) / 2, top + h / 2 - 20,
                      nombre, kBlueDivider, 3);

    const char* vacio = "seccion vacia";
    const i32 vw = font5x7::measure(vacio, 1);
    font5x7::drawText(fb, (fb.width() - vw) / 2, top + h / 2 + 14,
                      vacio, kDim, 1);
}

void HomeScene::render(Engine& engine, Framebuffer& fb) {
    m_lastFbWidth  = fb.width();
    m_lastFbHeight = fb.height();

    fb.clear(kBg);
    fb.drawRect({0, 0, fb.width(), fb.height()}, kBorder, 1);

    drawSection(engine, fb);   // primero el contenido...
    drawMenu(engine, fb);      // ...y encima el menu, que no debe taparse
    drawAvatar(fb);            // avatar siempre visible, en cualquier seccion
    drawInstallBar(fb);        // barra de instalacion, visible en toda seccion

    // El editor va por encima de todo menos de la barra de titulo.
    if (m_descOpen) drawDescriptionEditor(engine, fb);

    // Constancia de quien tiene la sesion abierta. En la seccion de paneles
    // va centrado: pegado a un borde quedaria bajo una franja roja.
    if (!m_email.empty()) {
        const i32 w = font5x7::measure(m_email.c_str(), 1);
        const i32 x = (m_section == kPanelsSection)
            ? (fb.width() - w) / 2
            : fb.width() - w - 12;
        font5x7::drawText(fb, x, fb.height() - 22, m_email.c_str(), kDim, 1);
    }

    m_titleBar.render(engine, fb);
}

} // namespace ludora
