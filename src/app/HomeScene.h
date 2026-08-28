#pragma once
#include "core/Scene.h"
#include "core/Types.h"
#include "app/GameInstall.h"
#include "app/Profile.h"
#include "ui/ScrollPanel.h"
#include "ui/TextField.h"
#include "ui/TitleBar.h"
#include <string>
#include <vector>

namespace ludora {

/// Pantalla principal tras iniciar sesion.
///
/// La franja azul superior actua de menu: seis bloques, cada uno abre una
/// seccion. La tercera es la de las dos barras rojas; las demas estan vacias
/// a proposito, listas para colgarles contenido.
class HomeScene : public Scene {
public:
    explicit HomeScene(std::string sessionEmail);

    void onEnter(Engine& engine) override;
    void onExit(Engine& engine) override;
    void update(Engine& engine, f64 dt) override;
    void render(Engine& engine, Framebuffer& fb) override;

    static constexpr i32 kBlockCount = 6;
    /// Indice de la seccion Tienda (1er bloque).
    static constexpr i32 kStoreSection   = 0;
    /// Indice de la seccion Chat (2o bloque).
    static constexpr i32 kChatSection    = 1;
    /// Indice de la seccion Cuenta (6o bloque).
    static constexpr i32 kAccountSection = 5;
    /// Seccion Resenas: no es un bloque del menu, se entra desde la biblioteca.
    static constexpr i32 kReviewsSection = 100;
    /// Seccion Modo Desarrollador: se entra desde el boton en Cuenta.
    static constexpr i32 kDevSection     = 101;

    /// Desplazamiento actual del panel de biblioteca (para pruebas).
    i32 libraryScrollOffset() const { return m_leftPanel.offset(); }
    /// Desplazamiento actual de la tienda (para pruebas).
    i32 storeScrollOffset() const   { return m_storePanel.offset(); }

    // --- accesores para pruebas ---
    void section(i32 s)                  { m_section = s; }
    i32  currentSection() const          { return m_section; }
    Recti devButtonRect() const          { return m_devBtnRect; }
    Recti logoutButtonRect() const       { return m_logoutRect; }
    bool descriptionOpen() const         { return m_descOpen; }
    const std::string& profileUser() const { return m_profile.userName; }
    const std::string& profileDesc() const { return m_profile.description; }
    Recti descButtonRect() const         { return m_descBtnRect; }
    Recti descSaveRect() const           { return m_descSaveRect; }
    /// Indice de la seccion que muestra las dos barras rojas.
    static constexpr i32 kPanelsSection = 2;   // 0-based: es el 3er bloque

private:
    /// Rectangulo del bloque i dentro de la franja azul.
    Recti blockRect(Engine& engine, i32 fbWidth, i32 i) const;
    /// Bloque bajo un punto logico, o -1.
    i32   blockAt(Engine& engine, i32 fbWidth, i32 lx, i32 ly) const;

    void drawMenu(Engine& engine, Framebuffer& fb);
    void drawSection(Engine& engine, Framebuffer& fb);
    /// Dibuja el panel violeta con scroll (lado izquierdo de BIBLIOTECA).
    void drawLibraryPanel(Framebuffer& fb);
    /// Dibuja la seccion TIENDA: tarjetas centradas con scroll largo.
    void drawStore(Engine& engine, Framebuffer& fb);
    /// Coloca el panel de la tienda segun el tamano del framebuffer.
    void layoutStore(Engine& engine, i32 fbW, i32 fbH);

    // --- seccion Chat ---
    void layoutChat(Engine& engine, i32 fbW, i32 fbH);
    void drawChat(Engine& engine, Framebuffer& fb);
    void updateChat(Engine& engine, f64 dt);
    /// Copia los mensajes seleccionados al portapapeles.
    void copyChatSelection();

    // --- seccion Cuenta ---
    void layoutAccount(Engine& engine, i32 fbW, i32 fbH);
    void drawAccount(Engine& engine, Framebuffer& fb);
    void updateAccount(Engine& engine, f64 dt);
    /// Panel superpuesto para escribir la descripcion publica.
    void drawDescriptionEditor(Engine& engine, Framebuffer& fb);
    void updateDescriptionEditor(Engine& engine, f64 dt);
    void saveProfile();
    /// Avatar circular de la esquina inferior derecha.
    void drawAvatar(Framebuffer& fb);
    /// Carga la foto de perfil a memoria si aun no se ha hecho.
    void ensurePhotoLoaded();

    TitleBar    m_titleBar;
    ScrollPanel m_leftPanel;
    ScrollPanel m_storePanel;
    ScrollPanel m_accountPanel;
    ScrollPanel m_chatPanel;

    // Seleccion de mensajes del chat, por lineas (indices, -1 = sin seleccion).
    i32  m_chatSelStart = -1;   // linea donde empezo el arrastre
    i32  m_chatSelEnd   = -1;   // linea actual del arrastre
    bool m_chatSelecting = false;

    // Instalacion de Voxel World (destacado de la tienda).
    Recti       m_installBtnRect{};
    bool        m_installHover = false;
    bool        m_installDown  = false;
    GameInstall m_install;
    bool        m_installStarted = false;
    std::vector<library::Entry> m_libraryGames;   // juegos ya instalados
    bool        m_libraryLoaded = false;
    i32         m_libraryHover  = -1;   // casilla de biblioteca bajo el raton
    i32         m_librarySelected = -1; // juego mostrado en el panel de detalle

    // Panel de detalle del juego (zona central de la biblioteca).
    Recti m_playBtnRect{};
    Recti m_reviewsBtnRect{};
    bool  m_playHover = false, m_playDown = false;

    // Juego en ejecucion: handle del proceso y animacion del boton Jugar.
    void* m_runningProc = nullptr;   // HANDLE del juego lanzado (nullptr = ninguno)
    f64   m_playAnimTime = 0.0;      // reloj de la animacion de "ejecutando"
    bool  m_reviewsHover = false, m_reviewsDown = false;
    ScrollPanel m_reviewsPanel;
    i32   m_reviewsFrom = -1;   // seccion desde la que se entro a resenas

    void drawGameDetail(Engine& engine, Framebuffer& fb, Recti area);
    void drawReviews(Engine& engine, Framebuffer& fb);
    /// Barra de progreso de instalacion, visible en cualquier seccion.
    void drawInstallBar(Framebuffer& fb);

    Profile   m_profile;
    bool      m_profileLoaded = false;

    // Foto de perfil en memoria, para el avatar y la ficha de Cuenta.
    std::vector<u32> m_photoPixels;
    i32   m_photoW = 0;
    i32   m_photoH = 0;
    bool  m_photoTried = false;   // ya se intento cargar (aunque fallara)

    // Rectangulos de la seccion Cuenta, recalculados al dibujar.
    Recti m_photoRect{};       // foto de perfil
    Recti m_galleryRect{};     // boton "Galeria"
    Recti m_cameraRect{};      // boton "Camara"
    Recti m_descBtnRect{};     // boton "Descripcion de cuenta en publico"
    Recti m_logoutRect{};      // boton "Cerrar sesion"
    Recti m_devBtnRect{};      // boton "Modo Desarrollador"
    f64   m_devAnimTime = 0.0; // reloj del degradado turquesa<->morado

    i32   m_accountHover = -1;   // boton bajo el raton (-1 = ninguno)
    i32   m_accountDown  = -1;
    bool  m_wantLogout   = false;   // se solicito cerrar sesion (diferido)

    // Editor de descripcion (se superpone a la seccion Cuenta).
    bool      m_descOpen = false;
    TextField m_descField;
    Recti     m_descSaveRect{};
    bool      m_descSaveHover = false;
    bool      m_descSaveDown  = false;

    std::string m_accountMsg;    // aviso al pie de la seccion
    bool        m_accountMsgErr = false;
    std::string m_email;        // cuenta con la sesion iniciada

    i32  m_section = kPanelsSection;   // seccion visible
    i32  m_hovered = -1;
    i32  m_pressed = -1;
    i32  m_lastFbWidth  = 0;
    i32  m_lastFbHeight = 0;
};

} // namespace ludora
