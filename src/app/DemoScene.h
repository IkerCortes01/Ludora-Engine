#pragma once
#include "core/Scene.h"
#include "core/Types.h"
#include "ui/TextField.h"
#include "ui/TitleBar.h"
#include <string>

namespace ludora {

/// Pantalla de acceso: campos de correo y contrasena, y un boton de accion.
/// Solo se admite una cuenta por equipo: si ya existe, la pantalla arranca en
/// modo "iniciar sesion" y deja de ofrecer la creacion.
///
/// Al entrar correctamente cede el paso a HomeScene.
class DemoScene : public Scene {
public:
    // --- accesores para pruebas ---
    Recti verifyButtonRect() const  { return m_verifyBtnRect; }
    Recti actionRect() const        { return m_actionRect; }
    bool  deviceVerified() const    { return m_deviceVerified; }
    bool  autoLoginPending() const  { return m_autoLogin; }
    bool  checkAnimating() const    { return m_checkAnim; }
    const std::string& foundEmailForTest() const { return m_foundEmail; }
    const std::string& statusMessage() const { return m_message; }
    void  setUserNameText(const std::string& s) { m_userName.setText(s); }
    void  setEmailText(const std::string& s)    { m_email.setText(s); }
    void  setPasswordText(const std::string& s) { m_password.setText(s); }
    Recti verifyOkRect() const      { return m_verifyOkRect; }
    bool  verifyOpen() const        { return m_verifyOpen; }
    const std::string& verifyMessage() const { return m_verifyMsg; }
    void  setVerifyKey(size_t i, const std::string& s) {
        if (i < kVerifyKeys) m_verifyField[i].setText(s);
    }
    size_t verifyKeyLength(size_t i) const {
        return i < kVerifyKeys ? m_verifyField[i].length() : 0;
    }

    /// Contenido de los campos (para pruebas).
    const std::string& userNameText() const { return m_userName.text(); }
    const std::string& emailText()    const { return m_email.text(); }
    size_t passwordLength()           const { return m_password.length(); }

    void onEnter(Engine& engine) override;
    void onExit(Engine& engine) override;
    void update(Engine& engine, f64 dt) override;
    void render(Engine& engine, Framebuffer& fb) override;

private:
    void layout(i32 fbW, i32 fbH);
    void handleForm(Engine& engine, f64 dt);
    void submit(Engine& engine);
    void focusNext();
    void drawForm(Framebuffer& fb);

    // --- panel "Verificar dispositivo" ---
    void drawVerifyPanel(Engine& engine, Framebuffer& fb);
    void updateVerifyPanel(Engine& engine, f64 dt);
    /// Cifra las tres claves y vincula este dispositivo.
    void runVerification(Engine& engine);

    TitleBar  m_titleBar;
    TextField m_userName;   // primero: nombre de usuario
    TextField m_email;
    TextField m_password;

    // Verificacion de dispositivo: tres claves de hasta 26 caracteres.
    static constexpr size_t kVerifyKeys    = 3;
    static constexpr size_t kVerifyMaxLen  = 26;

    Recti     m_verifyBtnRect{};       // boton del formulario
    bool      m_verifyHover = false;
    bool      m_verifyDown  = false;
    bool      m_verifyOpen  = false;   // panel abierto

    TextField m_verifyField[kVerifyKeys];
    Recti     m_verifyOkRect{};
    bool      m_verifyOkHover = false;
    bool      m_verifyOkDown  = false;
    std::string m_verifyMsg;
    bool        m_verifyMsgErr = false;
    bool        m_verifyBusy   = false;   // cifrando: puede tardar segundos

    Recti m_actionRect{};           // boton INICIAR / CREAR SESION
    bool  m_actionHover  = false;
    bool  m_actionDown   = false;
    bool  m_draggingText = false;   // arrastre para seleccionar

    bool        m_accountExists  = false;   // ya hay cuenta -> modo iniciar
    std::string m_message;                  // error o confirmacion
    bool        m_messageIsError = false;

    // Dispositivo vinculado a la cuenta.
    bool        m_deviceVerified = false;   // este equipo esta verificado
    std::string m_deviceKind;               // "Escritorio", "Portatil"...
    bool        m_autoLogin      = false;   // entrar sin pedir contrasena

    // Busqueda de cuenta con la tecla B y animacion de palomita verde.
    bool        m_checkAnim     = false;   // animacion en curso
    f64         m_checkTime     = 0.0;     // tiempo transcurrido de la animacion
    std::string m_foundEmail;              // cuenta encontrada, para entrar
    std::string m_foundUser;

    void startCheckAnimation(const std::string& email, const std::string& user);
    void drawCheckAnimation(Framebuffer& fb);
};

} // namespace ludora
