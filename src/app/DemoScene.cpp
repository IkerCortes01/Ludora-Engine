#include "app/DemoScene.h"
#include "app/Account.h"
#include "app/Font5x7.h"
#include "app/HomeScene.h"
#include "app/Profile.h"
#include "app/Device.h"
#include "app/Session.h"
#include "app/Vault.h"
#include "core/Engine.h"
#include "ui/Theme.h"
#include <algorithm>
#include <memory>
#include <windows.h>

namespace ludora {

using namespace theme;

namespace {
// Medidas del formulario en pixeles logicos.
constexpr i32 kFormW     = 420;
constexpr i32 kFieldH    = 44;
constexpr i32 kGapFields = 18;   // separacion correo -> contrasena
constexpr i32 kGapAction = 34;   // separacion contrasena -> boton
constexpr i32 kActionH   = 46;
constexpr f64 kCheckAnimSeconds = 1.6;   // duracion de la palomita verde
constexpr i32 kVerifyH   = 34;   // boton VERIFICAR DISPOSITIVO

const wchar_t* kAccountFile = L"cuenta.dat";
const wchar_t* kProfileFile = L"perfil.dat";
/// Almacen cifrado local de la Nube Tomate.
const wchar_t* kVaultFile   = L"tomate.vault";
} // namespace

void DemoScene::onEnter(Engine& engine) {
    m_titleBar.attach(engine);

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

    // Nombre de usuario: hasta 187 caracteres, con acentos y simbolos.
    m_userName.setStyle(st);
    m_userName.setPlaceholder("nombre de usuario");
    m_userName.setMaxLength(Profile::kMaxUserName);

    m_email.setStyle(st);
    m_email.setPlaceholder(std::string("nombre") + Account::kDomain);
    m_email.setMaxLength(48);

    m_password.setStyle(st);
    m_password.setPlaceholder("contrasena");
    m_password.setPassword(true);
    m_password.setMaxLength(48);

    // Tres claves de verificacion, de hasta 26 caracteres cada una.
    TextField::Style vst = st;
    vst.borderFocused = kTurquoise;
    vst.caret         = kTurquoise;
    for (size_t i = 0; i < kVerifyKeys; ++i) {
        m_verifyField[i].setStyle(vst);
        m_verifyField[i].setPassword(true);
        m_verifyField[i].setMaxLength(kVerifyMaxLen);
        m_verifyField[i].setPlaceholder("hasta 26 caracteres");
    }

    // Sesion recordada en este equipo: entrar directo, sin pedir nada. Es la
    // via mas rapida y tiene prioridad sobre el resto. El salto se encola
    // porque onEnter() corre dentro de setScene().
    const Session::Data ses = Session::load(Session::kFile);
    if (ses.valid) {
        m_userName.setText(ses.userName);
        m_email.setText(ses.email);
        m_autoLogin = true;
        m_deviceVerified = true;
        return;
    }

    m_accountExists = Account::exists(kAccountFile);
    if (m_accountExists) {
        // Ya hay cuenta: se precargan usuario y correo, el foco a la clave.
        m_email.setText(Account::storedEmail(kAccountFile));
        Profile p;
        if (Profile::load(kProfileFile, p)) m_userName.setText(p.userName);
        m_password.setFocused(true);

        // Dispositivo ya verificado: se entra sin pedir la contrasena. El
        // salto se encola porque onEnter() corre dentro de setScene().
        const Account::DeviceInfo dev = Account::deviceInfo(kAccountFile);
        m_deviceVerified = dev.verified;
        m_deviceKind     = dev.kindName;
        if (dev.verified)
            m_autoLogin = true;
    } else {
        m_userName.setFocused(true);   // el primer campo del formulario

        // Cuenta nueva: si ya se verifico el dispositivo en un intento
        // anterior, el almacen cifrado sigue en disco y no hay que repetirlo.
        std::vector<u8> blob;
        if (Vault::loadFile(kVaultFile, blob) && !blob.empty()) {
            m_deviceVerified = true;
            m_deviceKind     = Device::kindName(Device::kind());
        }
    }
}

void DemoScene::onExit(Engine& engine) {
    m_titleBar.detach(engine);
}

void DemoScene::layout(i32 fbW, i32 fbH) {
    // Bloque centrado: los tres campos, VERIFICAR DISPOSITIVO y, debajo,
    // GUARDAR Y ENTRAR. Ese orden refleja el flujo: primero se verifica.
    const i32 totalH = kFieldH * 3 + kGapFields * 2 + kGapAction +
                       kVerifyH + 12 + kActionH;
    const i32 w = std::min(kFormW, fbW - 60);
    const i32 x = (fbW - w) / 2;
    i32 y = (fbH - totalH) / 2 + 10;   // +10: deja aire para el titulo de arriba

    m_userName.setRect({x, y, w, kFieldH});
    y += kFieldH + kGapFields;
    m_email.setRect({x, y, w, kFieldH});
    y += kFieldH + kGapFields;
    m_password.setRect({x, y, w, kFieldH});
    y += kFieldH + kGapAction;

    m_verifyBtnRect = Recti{x, y, w, kVerifyH};
    y += kVerifyH + 12;
    m_actionRect = Recti{x, y, w, kActionH};
}

void DemoScene::focusNext() {
    // Ciclo usuario -> correo -> contrasena -> usuario.
    if (m_userName.focused()) {
        m_userName.setFocused(false); m_email.setFocused(true);
    } else if (m_email.focused()) {
        m_email.setFocused(false); m_password.setFocused(true);
    } else {
        m_password.setFocused(false); m_userName.setFocused(true);
    }
}

void DemoScene::submit(Engine& engine) {
    // Sin verificar el dispositivo no se entra: se explica el paso que falta
    // en lugar de dejar el boton muerto sin decir nada.
    if (!m_deviceVerified) {
        m_message = "Primero pulsa VERIFICAR DISPOSITIVO y escribe tus tres claves.";
        m_messageIsError = true;
        return;
    }

    const std::string email = m_email.text();
    const std::string pass  = m_password.text();
    std::string err;

    const bool ok = m_accountExists
        ? Account::verify(kAccountFile, email, pass, err)
        : Account::create(kAccountFile, email, pass, err);

    if (!ok) {
        m_message        = err;
        m_messageIsError = true;
        return;
    }

    // Guardar el nombre de usuario junto al resto del perfil. Se conserva la
    // descripcion y la foto que ya hubiera: aqui solo se toca el nombre.
    Profile p;
    Profile::load(kProfileFile, p);
    p.userName = m_userName.text();
    Profile::save(kProfileFile, p);

    // Guardar la sesion cifrada de este equipo: es lo que hace que la proxima
    // apertura entre directa, sin volver a pedir credenciales.
    Session::save(Session::kFile, email, m_userName.text());

    // Entrada correcta: paso a la pantalla principal. Diferido a propósito,
    // porque estamos dentro del update() de esta misma escena.
    m_password.clear();
    engine.queueScene(std::make_unique<HomeScene>(email));
}

void DemoScene::handleForm(Engine& engine, f64 dt) {
    Input& in = engine.input();

    // Los tres campos se tratan igual: iterar evita repetir la logica y que
    // se olvide actualizar uno al anadir otro.
    TextField* campos[] = { &m_userName, &m_email, &m_password };

    for (TextField* f : campos) f->update(dt);

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    m_actionHover = (lx >= m_actionRect.x && lx < m_actionRect.x + m_actionRect.w &&
                     ly >= m_actionRect.y && ly < m_actionRect.y + m_actionRect.h);
    m_verifyHover = (lx >= m_verifyBtnRect.x && lx < m_verifyBtnRect.x + m_verifyBtnRect.w &&
                     ly >= m_verifyBtnRect.y && ly < m_verifyBtnRect.y + m_verifyBtnRect.h);

    // --- raton ---
    if (in.mousePressed(0)) {
        TextField* clicado = nullptr;
        for (TextField* f : campos)
            if (f->contains(lx, ly)) { clicado = f; break; }

        if (clicado) {
            for (TextField* f : campos) f->setFocused(f == clicado);
            clicado->onMouseDown(lx, ly);
            m_draggingText = true;
        } else if (m_actionHover) {
            m_actionDown = true;
        } else if (m_verifyHover) {
            m_verifyDown = true;
        } else {
            for (TextField* f : campos) f->setFocused(false);
        }
    }

    if (m_draggingText && in.mouseDown(0)) {
        for (TextField* f : campos)
            if (f->focused()) { f->onMouseDrag(lx, ly); break; }
    }

    if (in.mouseReleased(0)) {
        if (m_actionDown && m_actionHover) submit(engine);
        if (m_verifyDown && m_verifyHover) {
            m_verifyOpen = true;
            m_verifyMsg.clear();
            m_verifyField[0].setFocused(true);
        }
        m_actionDown   = false;
        m_verifyDown   = false;
        m_draggingText = false;
    }

    // --- teclado ---
    const bool shift = in.keyDown(VK_SHIFT);
    const bool ctrl  = in.keyDown(VK_CONTROL);

    if (in.keyPressed(VK_TAB)) focusNext();
    if (in.keyPressed(VK_RETURN)) {
        // Enter avanza mientras queden campos por delante; en el ultimo envia.
        const bool ultimo = m_password.focused();
        if (!ultimo) focusNext();
        else         submit(engine);
    }

    // AltGr se reporta como Ctrl+Alt: en teclado español es la unica forma de
    // escribir @ (AltGr+Q) y otros simbolos. Tratarlo como Ctrl bloquearia
    // esas teclas, asi que solo cuenta como atajo el Ctrl SIN Alt.
    const bool altGr     = ctrl && in.keyDown(VK_MENU);
    const bool ctrlAtajo = ctrl && !altGr;

    // Teclas de edicion y atajos: solo las que el campo entiende.
    static const u32 kEditKeys[] = {
        VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_BACK, VK_DELETE, 'A', 'C', 'X', 'V'
    };
    for (u32 vk : kEditKeys) {
        // Las letras solo cuentan como atajo con Ctrl real; si no, son texto y
        // llegan por WM_CHAR (si no, se insertarian dos veces).
        const bool isLetter = (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'V');
        if (isLetter && !ctrlAtajo) continue;
        if (!in.keyPressed(vk)) continue;
        for (TextField* f : campos)
            if (f->focused()) { f->onKey(vk, shift, ctrlAtajo); break; }
    }

    // Texto tecleado (ya traducido por la distribucion del teclado).
    if (!ctrlAtajo) {
        for (wchar_t c : in.charsTyped()) {
            if (c == L'\t' || c == L'\r' || c == L'\n' || c == L'\b') continue;
            for (TextField* f : campos)
                if (f->focused()) { f->onChar(c); break; }
        }
    }
}

void DemoScene::update(Engine& engine, f64 dt) {
    // Dispositivo verificado: se entra directo. Se hace aqui y no en onEnter()
    // porque aquel corre dentro de setScene(), y encolar otra escena en ese
    // punto reemplazaria a esta antes de que llegue a existir del todo.
    if (m_autoLogin) {
        m_autoLogin = false;
        engine.queueScene(std::make_unique<HomeScene>(m_email.text()));
        return;
    }

    // Animacion de "cuenta encontrada" en curso: al terminar, entra a la
    // biblioteca dejando la sesion guardada.
    if (m_checkAnim) {
        m_checkTime += dt;
        if (m_checkTime >= kCheckAnimSeconds) {
            m_checkAnim = false;
            // Guardar la sesion para que ya no vuelva a pedirla, y entrar.
            Session::save(Session::kFile, m_foundEmail, m_foundUser);
            engine.queueScene(std::make_unique<HomeScene>(m_foundEmail));
        }
        m_titleBar.update(engine);
        return;   // durante la animacion no se atiende el formulario
    }

    // El panel de verificacion se superpone: mientras este abierto se queda
    // con el teclado y el raton, salvo los botones de ventana.
    if (m_verifyOpen) {
        m_titleBar.update(engine);
        updateVerifyPanel(engine, dt);
        return;
    }

    if (engine.input().keyPressed(VK_ESCAPE)) { engine.requestQuit(); return; }

    // Tecla B: buscar en el sistema una cuenta ya registrada, sin escribir
    // nada. Si la encuentra, animacion de palomita y a la biblioteca.
    if (engine.input().keyPressed('B')) {
        const Account::Found f = Account::searchSystem();
        if (f.ok) {
            startCheckAnimation(f.email, f.userName);
        } else {
            m_message = "No se encontro ninguna cuenta guardada en el sistema.";
            m_messageIsError = true;
        }
        return;
    }

    // La barra devuelve true si el raton esta sobre un boton de ventana:
    // en ese caso el clic no debe llegar al formulario.
    const bool sobreBoton = m_titleBar.update(engine);
    if (!sobreBoton) handleForm(engine, dt);
    else { m_email.update(dt); m_password.update(dt); }
}

void DemoScene::startCheckAnimation(const std::string& email, const std::string& user) {
    m_checkAnim  = true;
    m_checkTime  = 0.0;
    m_foundEmail = email;
    m_foundUser  = user;
    m_message.clear();
}

void DemoScene::drawCheckAnimation(Framebuffer& fb) {
    // Velo oscuro sobre el formulario.
    fb.fillRect({0, 0, fb.width(), fb.height()}, Color{0, 0, 0, 180});

    const i32 cx = fb.width() / 2;
    const i32 cy = fb.height() / 2 - 10;

    // Progreso 0..1 de la animacion, con un rebote suave al aparecer.
    const f32 t = std::min(1.0f, static_cast<f32>(m_checkTime / kCheckAnimSeconds));
    // Escala que sobrepasa 1 y vuelve (efecto "pop").
    const f32 pop = (t < 0.6f)
        ? (t / 0.6f) * 1.15f
        : (1.15f - (t - 0.6f) / 0.4f * 0.15f);
    const i32 r = static_cast<i32>(46 * std::max(0.0f, pop));

    // Circulo verde de fondo.
    const Color verde = Color::fromHex(0x2ECC71);
    fb.fillCircle(cx, cy, r, verde);

    // La palomita se "dibuja" progresivamente segun avanza t.
    // Dos trazos: bajada corta y subida larga. Cada uno con su grosor.
    if (r > 8) {
        const f32 draw = std::min(1.0f, std::max(0.0f, (t - 0.35f) / 0.5f));
        // Puntos de la palomita, relativos al centro y escalados al radio.
        // Se baja un poco el conjunto para que quede optico-centrado.
        const i32 ax = cx - r * 42 / 100, ay = cy + r * 8  / 100;
        const i32 bx = cx - r * 6  / 100, by = cy + r * 38 / 100;
        const i32 ex = cx + r * 46 / 100, ey = cy - r * 28 / 100;

        // Primer trazo (a -> b) y luego (b -> e), revelados por `draw`.
        auto trazo = [&](i32 x0, i32 y0, i32 x1, i32 y1, f32 frac) {
            const i32 tx = x0 + static_cast<i32>((x1 - x0) * frac);
            const i32 ty = y0 + static_cast<i32>((y1 - y0) * frac);
            for (i32 gx = -2; gx <= 2; ++gx)
                for (i32 gy = -2; gy <= 2; ++gy)
                    fb.drawLine(x0 + gx, y0 + gy, tx + gx, ty + gy, kWhite);
        };
        if (draw <= 0.5f) {
            trazo(ax, ay, bx, by, draw / 0.5f);
        } else {
            trazo(ax, ay, bx, by, 1.0f);
            trazo(bx, by, ex, ey, (draw - 0.5f) / 0.5f);
        }
    }

    // Texto bajo el circulo.
    if (t > 0.5f) {
        const char* msg = "Cuenta encontrada";
        const i32 mw = font5x7::measure(msg, 2);
        font5x7::drawText(fb, cx - mw / 2, cy + 70, msg, kWhite, 2);

        if (!m_foundEmail.empty()) {
            const i32 ew = font5x7::measureUtf8(m_foundEmail.c_str(), 1);
            font5x7::drawTextUtf8(fb, cx - ew / 2, cy + 96,
                                  m_foundEmail.c_str(), kOk, 1);
        }
    }
}

void DemoScene::drawForm(Framebuffer& fb) {
    // El titulo se ancla al primer campo del formulario.
    const Recti er = m_userName.rect();

    // --- titulo del bloque ---
    const char* title = m_accountExists ? "INICIAR SESION" : "CREAR SESION";
    const i32 titleScale = 3;
    const i32 tw = font5x7::measure(title, titleScale);
    const i32 tx = er.x + (er.w - tw) / 2;
    font5x7::drawText(fb, tx, er.y - 62, title, kText, titleScale);
    fb.fillRect({tx, er.y - 62 + font5x7::kGlyphH * titleScale + 8, tw, 2}, kAccent);

    // --- etiquetas y campos ---
    const Recti ur = m_userName.rect();
    font5x7::drawText(fb, ur.x, ur.y - 14, "NOMBRE DE USUARIO", kDim, 1);
    m_userName.render(fb);

    const Recti mr = m_email.rect();
    font5x7::drawText(fb, mr.x, mr.y - 14, "CORREO", kDim, 1);
    m_email.render(fb);

    const Recti pr = m_password.rect();
    font5x7::drawText(fb, pr.x, pr.y - 14, "CONTRASENA", kDim, 1);
    m_password.render(fb);

    // --- boton de accion ---
    // --- 1) boton "Verificar dispositivo", arriba ---
    {
        const bool hecho = m_deviceVerified;
        // Verificado: se marca en verde y deja de reclamar atencion.
        // Pendiente: turquesa, que es el paso que toca.
        const Color borde  = hecho ? kOk : kTurquoise;
        const Color fondo  = (m_verifyDown && m_verifyHover) ? kClosePress
                           : (m_verifyHover ? kPanelEdge : kFieldBg);
        fb.fillRect(m_verifyBtnRect, fondo);
        fb.drawRect(m_verifyBtnRect, m_verifyHover ? kWhite : borde, hecho ? 1 : 2);

        const char* vt = hecho ? "DISPOSITIVO VERIFICADO"
                               : "1. VERIFICAR DISPOSITIVO";
        const i32 vw = font5x7::measure(vt, 1);
        font5x7::drawText(fb, m_verifyBtnRect.x + (m_verifyBtnRect.w - vw) / 2,
                          m_verifyBtnRect.y + (m_verifyBtnRect.h - font5x7::kGlyphH) / 2,
                          vt, m_verifyHover ? kWhite : (hecho ? kOk : kTurquoise), 1);
    }

    // --- 2) boton "Guardar y entrar", debajo y bloqueado hasta verificar ---
    {
        const bool listo = m_deviceVerified;
        const bool down  = m_actionDown && m_actionHover && listo;

        // Bloqueado: se dibuja apagado para que se vea que aun no toca,
        // pero sigue siendo pulsable para poder explicar por que no funciona.
        Color fondo = kFieldBg;
        if (listo) fondo = down ? kClosePress : (m_actionHover ? kAccent : kFieldBg);
        fb.fillRect(m_actionRect, fondo);
        fb.drawRect(m_actionRect,
                    listo ? (m_actionHover ? kAccent : kBorder) : kFieldBorder, 2);

        const char* label = listo ? "GUARDAR Y ENTRAR" : "2. GUARDAR Y ENTRAR";
        const i32 lw = font5x7::measure(label, 2);
        const i32 lh = font5x7::kGlyphH * 2;
        font5x7::drawText(fb, m_actionRect.x + (m_actionRect.w - lw) / 2,
                          m_actionRect.y + (m_actionRect.h - lh) / 2,
                          label,
                          listo ? (m_actionHover ? kWhite : kText) : kDim, 2);
    }

    // --- mensaje de estado ---
    if (!m_message.empty()) {
        const i32 mw = font5x7::measure(m_message.c_str(), 1);
        font5x7::drawText(fb, m_actionRect.x + (m_actionRect.w - mw) / 2,
                          m_actionRect.y + m_actionRect.h + 14,
                          m_message.c_str(), m_messageIsError ? kAccent : kOk, 1);
    }

    // --- estado del dispositivo ---
    if (m_accountExists && !m_deviceKind.empty()) {
        const std::string dev = m_deviceVerified
            ? "Dispositivo verificado: " + m_deviceKind
            : "Dispositivo no reconocido: " + m_deviceKind;
        const i32 dw = font5x7::measure(dev.c_str(), 1);
        font5x7::drawText(fb, (fb.width() - dw) / 2, fb.height() - 46,
                          dev.c_str(), m_deviceVerified ? kOk : kDim, 1);
    }

    // --- pie de ayuda ---
    const char* foot = m_accountExists
        ? "Ya existe una cuenta en este equipo"
        : "El correo debe terminar en @ludora.engine   (AltGr+Q para la arroba)";
    const i32 fw = font5x7::measure(foot, 1);
    font5x7::drawText(fb, (fb.width() - fw) / 2, fb.height() - 28, foot, kDim, 1);

    // Atajo B: buscar una cuenta ya guardada en el sistema.
    const char* atajoB = "Pulsa B para buscar tu cuenta guardada";
    const i32 bw = font5x7::measure(atajoB, 1);
    font5x7::drawText(fb, (fb.width() - bw) / 2, fb.height() - 14, atajoB, kOk, 1);
}

void DemoScene::drawVerifyPanel(Engine& engine, Framebuffer& fb) {
    const i32 barH = m_titleBar.heightLogical(engine);

    fb.fillRect({0, barH, fb.width(), fb.height() - barH}, Color{0, 0, 0, 200});

    const i32 w = std::min(520, fb.width() - 60);
    const i32 h = std::min(400, fb.height() - barH - 40);
    const i32 x = (fb.width() - w) / 2;
    const i32 y = barH + (fb.height() - barH - h) / 2;

    fb.fillRect({x, y, w, h}, kFieldBg);
    fb.drawRect({x, y, w, h}, kTurquoise, 2);

    font5x7::drawText(fb, x + 24, y + 20, "VERIFICAR DISPOSITIVO", kText, 2);
    font5x7::drawText(fb, x + 24, y + 44,
                      "Confirma que eres tu con tus tres claves.", kDim, 1);
    font5x7::drawText(fb, x + 24, y + 58,
                      "Maximo 26 caracteres cada una.", kDim, 1);

    // Tres campos de clave.
    i32 fy = y + 78;
    for (size_t i = 0; i < kVerifyKeys; ++i) {
        char et[24];
        std::snprintf(et, sizeof(et), "CLAVE %zu", i + 1);
        font5x7::drawText(fb, x + 24, fy, et, kDim, 1);

        m_verifyField[i].setRect({x + 24, fy + 12, w - 48, 36});
        m_verifyField[i].render(fb);

        // Contador de caracteres, para que el limite no sorprenda.
        char cnt[24];
        std::snprintf(cnt, sizeof(cnt), "%zu/26", m_verifyField[i].length());
        const i32 cw = font5x7::measure(cnt, 1);
        font5x7::drawText(fb, x + w - 24 - cw, fy, cnt,
                          m_verifyField[i].length() >= kVerifyMaxLen ? kAccent : kDim, 1);

        fy += 62;
    }

    // Boton de confirmar.
    m_verifyOkRect = Recti{x + 24, y + h - 56, w - 48, 36};
    const bool busy = m_verifyBusy;
    fb.fillRect(m_verifyOkRect,
                busy ? kFieldBgFocus
                     : ((m_verifyOkDown && m_verifyOkHover) ? kClosePress
                                                            : (m_verifyOkHover ? kTurquoise : kFieldBg)));
    fb.drawRect(m_verifyOkRect, busy ? kDim : kTurquoise, 2);
    {
        const char* t = busy ? "CIFRANDO..." : "CONFIRMAR";
        const i32 tw = font5x7::measure(t, 2);
        font5x7::drawText(fb, m_verifyOkRect.x + (m_verifyOkRect.w - tw) / 2,
                          m_verifyOkRect.y + (m_verifyOkRect.h - font5x7::kGlyphH * 2) / 2,
                          t, busy ? kDim : kWhite, 2);
    }

    if (!m_verifyMsg.empty()) {
        const i32 mw = font5x7::measure(m_verifyMsg.c_str(), 1);
        font5x7::drawText(fb, x + (w - mw) / 2, y + h - 16,
                          m_verifyMsg.c_str(), m_verifyMsgErr ? kAccent : kOk, 1);
    }
}

void DemoScene::runVerification(Engine& engine) {
    (void)engine;

    // Las tres claves deben estar completas: es el punto del proceso.
    for (size_t i = 0; i < kVerifyKeys; ++i) {
        if (m_verifyField[i].text().empty()) {
            m_verifyMsg    = "Rellena las tres claves.";
            m_verifyMsgErr = true;
            return;
        }
    }

    // 1) Registrar el TIPO de dispositivo. Sin ubicacion, sin IP: solo la
    //    categoria del equipo y una huella hasheada del hardware.
    const std::string tipo = Device::kindName(Device::kind());

    // 2) Material secreto: las tres claves con separador. Nunca se guarda.
    std::string secreto;
    for (size_t i = 0; i < kVerifyKeys; ++i) {
        if (i) secreto += '\x1f';
        secreto += m_verifyField[i].text();
    }

    // 3) Contenido a cifrar: correo, usuario y tipo de dispositivo.
    std::string payload;
    payload += "email=" + m_email.text() + "\n";
    payload += "user="  + m_userName.text() + "\n";
    payload += "device=" + tipo + "\n";

    const std::vector<u8> plain(payload.begin(), payload.end());
    std::vector<u8> blob;
    std::string err;

    // 4) Diez capas de AES-256-GCM, cada una con su clave derivada.
    if (!Vault::seal(secreto, plain, blob, err)) {
        m_verifyMsg    = err;
        m_verifyMsgErr = true;
        return;
    }

    if (!Vault::saveFile(kVaultFile, blob)) {
        m_verifyMsg    = "No se pudo guardar el almacen cifrado.";
        m_verifyMsgErr = true;
        return;
    }

    // Limpiar las claves de memoria y de los campos en cuanto se han usado.
    SecureZeroMemory(&secreto[0], secreto.size());
    for (size_t i = 0; i < kVerifyKeys; ++i) m_verifyField[i].clear();

    // Queda verificado: es lo que desbloquea GUARDAR Y ENTRAR.
    m_deviceVerified = true;
    m_deviceKind     = tipo;

    m_verifyMsg    = "Dispositivo verificado (" + tipo + "). Ya puedes entrar.";
    m_verifyMsgErr = false;
    m_message      = "Dispositivo verificado. Pulsa GUARDAR Y ENTRAR.";
    m_messageIsError = false;
}

void DemoScene::updateVerifyPanel(Engine& engine, f64 dt) {
    Input& in = engine.input();

    for (size_t i = 0; i < kVerifyKeys; ++i) m_verifyField[i].update(dt);

    const f32 sc = std::max(0.01f, engine.window().contentScale());
    const Vec2 mp = in.mousePos();
    const i32 lx = static_cast<i32>(mp.x / sc);
    const i32 ly = static_cast<i32>(mp.y / sc);

    m_verifyOkHover = (lx >= m_verifyOkRect.x && lx < m_verifyOkRect.x + m_verifyOkRect.w &&
                       ly >= m_verifyOkRect.y && ly < m_verifyOkRect.y + m_verifyOkRect.h);

    if (in.mousePressed(0)) {
        TextField* clicado = nullptr;
        for (size_t i = 0; i < kVerifyKeys; ++i)
            if (m_verifyField[i].contains(lx, ly)) { clicado = &m_verifyField[i]; break; }

        if (clicado) {
            for (size_t i = 0; i < kVerifyKeys; ++i)
                m_verifyField[i].setFocused(&m_verifyField[i] == clicado);
            clicado->onMouseDown(lx, ly);
        } else if (m_verifyOkHover) {
            m_verifyOkDown = true;
        }
    }

    if (in.mouseReleased(0)) {
        if (m_verifyOkDown && m_verifyOkHover) runVerification(engine);
        m_verifyOkDown = false;
    }

    // --- teclado ---
    if (in.keyPressed(VK_ESCAPE)) { m_verifyOpen = false; return; }
    if (in.keyPressed(VK_TAB)) {
        // Rotar el foco entre las tres claves.
        size_t actual = kVerifyKeys;
        for (size_t i = 0; i < kVerifyKeys; ++i)
            if (m_verifyField[i].focused()) { actual = i; break; }
        const size_t siguiente = (actual + 1) % kVerifyKeys;
        for (size_t i = 0; i < kVerifyKeys; ++i)
            m_verifyField[i].setFocused(i == siguiente);
    }

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
        if (!in.keyPressed(vk)) continue;
        for (size_t i = 0; i < kVerifyKeys; ++i)
            if (m_verifyField[i].focused()) { m_verifyField[i].onKey(vk, shift, ctrlAtajo); break; }
    }
    if (!ctrlAtajo) {
        for (wchar_t c : in.charsTyped()) {
            if (c == L'\t' || c == L'\r' || c == L'\n' || c == L'\b') continue;
            for (size_t i = 0; i < kVerifyKeys; ++i)
                if (m_verifyField[i].focused()) { m_verifyField[i].onChar(c); break; }
        }
    }
}

void DemoScene::render(Engine& engine, Framebuffer& fb) {
    layout(fb.width(), fb.height());

    fb.clear(kBg);
    fb.drawRect({0, 0, fb.width(), fb.height()}, kBorder, 1);

    drawForm(fb);
    if (m_verifyOpen) drawVerifyPanel(engine, fb);
    if (m_checkAnim)  drawCheckAnimation(fb);   // por encima del formulario
    m_titleBar.render(engine, fb);
}

} // namespace ludora
